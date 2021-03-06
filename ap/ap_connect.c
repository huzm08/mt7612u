/****************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 * (c) Copyright 2002, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ****************************************************************************

    Module Name:
    connect.c

    Abstract:
    Routines to deal Link UP/DOWN and build/update BEACON frame contents

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
    John Chang  08-04-2003    created for 11g soft-AP
 */

#include "rt_config.h"

u8 PowerConstraintIE[3] = {IE_POWER_CONSTRAINT, 1, 3};


/*
	==========================================================================
	Description:
		Used to check the necessary to send Beancon.
	return value
		0: mean no necessary.
		0: mean need to send Beacon for the service.
	==========================================================================
*/
bool BeaconTransmitRequired(struct rtmp_adapter *pAd, INT apidx, MULTISSID_STRUCT *pMbss)
{
	bool result = false;

	do
	{



		if (pMbss->BcnBufIdx >= HW_BEACON_MAX_NUM)
			break;
		if (pAd->BeaconOffset[pMbss->BcnBufIdx] == 0)
			break;

		if (apidx == MAIN_MBSSID)
		{
			if (pMbss->bBcnSntReq == true)
			{
				result = true;
				break;
			}
		}
		else
		{
			if (pMbss->bBcnSntReq == true)
				result = true;
		}
	}
	while (false);

	return result;
}


/*
	==========================================================================
	Description:
		Pre-build a BEACON frame in the shared memory
	==========================================================================
*/
VOID APMakeBssBeacon(struct rtmp_adapter *pAd, INT apidx)
{
	u8 DsLen = 1, SsidLen;
	HEADER_802_11 BcnHdr;
	LARGE_INTEGER FakeTimestamp;
	ULONG FrameLen = 0;
	u8 *pBeaconFrame = (u8 *)pAd->ApCfg.MBSSID[apidx].BeaconBuf;
	u8 *ptr;
	UINT i;
	uint32_t reg_base;
	HTTRANSMIT_SETTING BeaconTransmit = {.word = 0};   /* MGMT frame PHY rate setting when operatin at Ht rate. */
	u8 PhyMode, SupRateLen;
	UINT8 TXWISize = pAd->chipCap.TXWISize;
	MULTISSID_STRUCT *pMbss = &pAd->ApCfg.MBSSID[apidx];
#ifdef SPECIFIC_TX_POWER_SUPPORT
	u8 TxPwrAdj = 0;
#endif /* SPECIFIC_TX_POWER_SUPPORT */

	if(!BeaconTransmitRequired(pAd, apidx, pMbss))
		return;

	PhyMode = pMbss->wdev.PhyMode;

	if (pMbss->bHideSsid)
		SsidLen = 0;
	else
		SsidLen = pMbss->SsidLen;

	MgtMacHeaderInit(pAd, &BcnHdr, SUBTYPE_BEACON, 0, BROADCAST_ADDR,
						pMbss->wdev.if_addr,
						pMbss->wdev.bssid);

	/* for update framelen to TxWI later. */
	SupRateLen = pAd->CommonCfg.SupRateLen;
	if (PhyMode == WMODE_B)
		SupRateLen = 4;

	MakeOutgoingFrame(pBeaconFrame,                  &FrameLen,
					sizeof(HEADER_802_11),           &BcnHdr,
					TIMESTAMP_LEN,                   &FakeTimestamp,
					2,                               &pAd->CommonCfg.BeaconPeriod,
					2,                               &pMbss->CapabilityInfo,
					1,                               &SsidIe,
					1,                               &SsidLen,
					SsidLen,                      pMbss->Ssid,
					1,                               &SupRateIe,
					1,                               &SupRateLen,
					SupRateLen,                pAd->CommonCfg.SupRate,
					1,                               &DsIe,
					1,                               &DsLen,
					1,                               &pAd->CommonCfg.Channel,
					END_OF_ARGS);

	if ((pAd->CommonCfg.ExtRateLen) && (PhyMode != WMODE_B))
	{
		ULONG TmpLen;
		MakeOutgoingFrame(pBeaconFrame+FrameLen,         &TmpLen,
						1,                               &ExtRateIe,
						1,                               &pAd->CommonCfg.ExtRateLen,
						pAd->CommonCfg.ExtRateLen,           pAd->CommonCfg.ExtRate,
						END_OF_ARGS);
		FrameLen += TmpLen;
	}


    /* add country IE, power constraint IE */
	if (pAd->CommonCfg.bCountryFlag)
	{
		ULONG TmpLen, TmpLen2=0;
		u8 *TmpFrame = NULL;
		u8 CountryIe = IE_COUNTRY;

		TmpFrame = kmalloc(256, GFP_ATOMIC);
		if (TmpFrame != NULL)
		{
			memset(TmpFrame, 0, 256);

			/* prepare channel information */
			{
				u8 MaxTxPower = GetCuntryMaxTxPwr(pAd, pAd->CommonCfg.Channel);
				MakeOutgoingFrame(TmpFrame+TmpLen2,     &TmpLen,
									1,                 	&pAd->ChannelList[0].Channel,
									1,                 	&pAd->ChannelListNum,
									1,                 	&MaxTxPower,
									END_OF_ARGS);
				TmpLen2 += TmpLen;
			}

			/* need to do the padding bit check, and concatenate it */
			if ((TmpLen2%2) == 0)
			{
				u8 TmpLen3 = TmpLen2+4;
				MakeOutgoingFrame(pBeaconFrame+FrameLen,&TmpLen,
				                  1,                 	&CountryIe,
				                  1,                 	&TmpLen3,
				                  3,                 	pAd->CommonCfg.CountryCode,
				                  TmpLen2+1,				TmpFrame,
				                  END_OF_ARGS);
			}
			else
			{
				u8 TmpLen3 = TmpLen2+3;
				MakeOutgoingFrame(pBeaconFrame+FrameLen,&TmpLen,
				                  1,                 	&CountryIe,
				                  1,                 	&TmpLen3,
				                  3,                 	pAd->CommonCfg.CountryCode,
				                  TmpLen2,				TmpFrame,
				                  END_OF_ARGS);
			}
			FrameLen += TmpLen;

			kfree(TmpFrame);
		}
		else
			DBGPRINT(RT_DEBUG_ERROR, ("%s: Allocate memory fail!!!\n", __FUNCTION__));
	}

	/* AP Channel Report */
	{
		u8 APChannelReportIe = IE_AP_CHANNEL_REPORT;
		ULONG	TmpLen;

		/*
			802.11n D2.0 Annex J, USA regulatory
				class 32, channel set 1~7
				class 33, channel set 5-11
		*/
		u8 rclass32[]={32, 1, 2, 3, 4, 5, 6, 7};
        u8 rclass33[]={33, 5, 6, 7, 8, 9, 10, 11};
		u8 rclasslen = 8; /*sizeof(rclass32); */
		if (PhyMode == (WMODE_B | WMODE_G | WMODE_GN))
		{
			MakeOutgoingFrame(pBeaconFrame+FrameLen,&TmpLen,
							  1,                    &APChannelReportIe,
							  1,                    &rclasslen,
							  rclasslen,            rclass32,
   							  1,                    &APChannelReportIe,
							  1,                    &rclasslen,
							  rclasslen,            rclass33,
							  END_OF_ARGS);
			FrameLen += TmpLen;
		}
	}


	BeaconTransmit.word = 0;

#ifdef SPECIFIC_TX_POWER_SUPPORT
        /* Specific Power for Long-Range Beacon */
	if ((pAd->ApCfg.MBSSID[apidx].TxPwrAdj != -1) /* &&
	    (BeaconTransmit.field.MODE == MODE_CCK)*/)
	{
		TxPwrAdj = pAd->ApCfg.MBSSID[apidx].TxPwrAdj;
	}
#endif /* SPECIFIC_TX_POWER_SUPPORT */

	RTMPWriteTxWI(pAd, &pAd->BeaconTxWI, false, false, true, false, false, true, 0, BSS0Mcast_WCID,
					FrameLen, PID_MGMT, 0, 0,IFS_HTTXOP, &BeaconTransmit);

#ifdef SPECIFIC_TX_POWER_SUPPORT
		if  ((IS_RT6352(pAd) || IS_MT76x2(pAd)) && (pAd->chipCap.hif_type == HIF_RLT))
			pAd->BeaconTxWI.TXWI_N.TxPwrAdj = TxPwrAdj;
#endif /* SPECIFIC_TX_POWER_SUPPORT */

	/*
		step 6. move BEACON TXD and frame content to on-chip memory
	*/
	ptr = (u8 *)&pAd->BeaconTxWI;
#ifdef RT_BIG_ENDIAN
    RTMPWIEndianChange(pAd, ptr, TYPE_TXWI);
#endif


	reg_base = pAd->BeaconOffset[pMbss->BcnBufIdx];
	for (i = 0; i < TXWISize; i += 4) {
		u32 dword;

		dword =  *ptr +
			(*(ptr + 1) << 8);
			(*(ptr + 2) << 16);
			(*(ptr + 3) << 24);

		mt7612u_write32(pAd, reg_base + i, dword);
		ptr += 4;
	}

	/* update BEACON frame content. start right after the TXWI field. */
	ptr = (u8 *)pMbss->BeaconBuf;
#ifdef RT_BIG_ENDIAN
	RTMPFrameEndianChange(pAd, ptr, DIR_WRITE, false);
#endif

	reg_base = pAd->BeaconOffset[pMbss->BcnBufIdx] + TXWISize;
	for ( i= 0; i< FrameLen; i+=4) {
		u32 dword;

		dword =  *ptr +
			(*(ptr + 1) << 8);
			(*(ptr + 2) << 16);
			(*(ptr + 3) << 24);

		mt7612u_write32(pAd, reg_base + i, dword);
		ptr += 4;
	}

	pMbss->TimIELocationInBeacon = (u8)FrameLen;
	pMbss->CapabilityInfoLocationInBeacon = sizeof(HEADER_802_11) + TIMESTAMP_LEN + 2;
}


/*
	==========================================================================
	Description:
		Update the BEACON frame in the shared memory. Because TIM IE is variable
		length. other IEs after TIM has to shift and total frame length may change
		for each BEACON period.
	Output:
		pAd->ApCfg.MBSSID[apidx].CapabilityInfo
		pAd->ApCfg.ErpIeContent
	==========================================================================
*/
VOID APUpdateBeaconFrame(struct rtmp_adapter *pAd, INT apidx)
{
	u8 *pBeaconFrame;
	u8 *ptr;
	ULONG FrameLen;
	ULONG UpdatePos;
	u8 RSNIe=IE_WPA, RSNIe2=IE_WPA2;
	u8 ID_1B, TimFirst, TimLast, *pTim;
	MULTISSID_STRUCT *pMbss;
	COMMON_CONFIG *pComCfg;
	u8 PhyMode;
	bool bHasWpsIE = false;
	UINT  i;
	HTTRANSMIT_SETTING	BeaconTransmit = {.word = 0};   /* MGMT frame PHY rate setting when operatin at Ht rate. */
	struct rtmp_wifi_dev *wdev;
#ifdef SPECIFIC_TX_POWER_SUPPORT
	u8 TxPwrAdj = 0;
#endif /* SPECIFIC_TX_POWER_SUPPORT */

	pComCfg = &pAd->CommonCfg;
	pMbss = &pAd->ApCfg.MBSSID[apidx];
	wdev = &pMbss->wdev;

	pBeaconFrame = (u8 *)&pMbss->BeaconBuf[0];
	FrameLen = UpdatePos = pMbss->TimIELocationInBeacon;
	PhyMode = wdev->PhyMode;

	if(!BeaconTransmitRequired(pAd, apidx, pMbss))
		return;

	/*
		step 1 - update BEACON's Capability
	*/
	ptr = pBeaconFrame + pMbss->CapabilityInfoLocationInBeacon;
	*ptr = (u8)(pMbss->CapabilityInfo & 0x00ff);
	*(ptr+1) = (u8)((pMbss->CapabilityInfo & 0xff00) >> 8);

	/*
		step 2 - update TIM IE
		TODO: enlarge TIM bitmap to support up to 64 STAs
		TODO: re-measure if RT2600 TBTT interrupt happens faster than BEACON sent out time
	*/
	ptr = pBeaconFrame + pMbss->TimIELocationInBeacon;
	*ptr = IE_TIM;
	*(ptr + 2) = pAd->ApCfg.DtimCount;
	*(ptr + 3) = pAd->ApCfg.DtimPeriod;

	/* find the smallest AID (PS mode) */
	TimFirst = 0; /* record first TIM byte != 0x00 */
	TimLast = 0;  /* record last  TIM byte != 0x00 */
	pTim = pMbss->TimBitmaps;

	for(ID_1B=0; ID_1B<WLAN_MAX_NUM_OF_TIM; ID_1B++)
	{
		/* get the TIM indicating PS packets for 8 stations */
		u8 tim_1B = pTim[ID_1B];

		if (ID_1B == 0)
			tim_1B &= 0xfe; /* skip bit0 bc/mc */

		if (tim_1B == 0)
			continue; /* find next 1B */

		if (TimFirst == 0)
			TimFirst = ID_1B;

		TimLast = ID_1B;
	}

	/* fill TIM content to beacon buffer */
	if (TimFirst & 0x01)
		TimFirst --; /* find the even offset byte */

	*(ptr + 1) = 3+(TimLast-TimFirst+1); /* TIM IE length */
	*(ptr + 4) = TimFirst;

	for(i=TimFirst; i<=TimLast; i++)
		*(ptr + 5 + i - TimFirst) = pTim[i];

	/* bit0 means backlogged mcast/bcast */
    if (pAd->ApCfg.DtimCount == 0)
		*(ptr + 4) |= (pMbss->TimBitmaps[WLAN_CT_TIM_BCMC_OFFSET] & 0x01);

	/* adjust BEACON length according to the new TIM */
	FrameLen += (2 + *(ptr+1));

	/* move RSN IE from below to here for Ralink Win7 v3.0.0.61 version parse beacon issue. */
	/* sync the order with BRCM's AP. */
	if ((wdev->AuthMode == Ndis802_11AuthModeWPA) ||
		(wdev->AuthMode == Ndis802_11AuthModeWPAPSK))
		RSNIe = IE_WPA;
	else if ((wdev->AuthMode == Ndis802_11AuthModeWPA2) ||
		(wdev->AuthMode == Ndis802_11AuthModeWPA2PSK))
		RSNIe = IE_WPA2;

	/* Append RSN_IE when  WPA OR WPAPSK, */
	if ((wdev->AuthMode == Ndis802_11AuthModeWPA1WPA2) ||
		(wdev->AuthMode == Ndis802_11AuthModeWPA1PSKWPA2PSK))
	{
		ULONG TmpLen;
		MakeOutgoingFrame(pBeaconFrame+FrameLen,        &TmpLen,
						  1,                            &RSNIe,
						  1,                            &pMbss->RSNIE_Len[0],
						  pMbss->RSNIE_Len[0],      pMbss->RSN_IE[0],
						  1,                            &RSNIe2,
						  1,                            &pMbss->RSNIE_Len[1],
						  pMbss->RSNIE_Len[1],      pMbss->RSN_IE[1],
						  END_OF_ARGS);
		FrameLen += TmpLen;
	}
	else if (wdev->AuthMode >= Ndis802_11AuthModeWPA)
	{
		ULONG TmpLen;
		MakeOutgoingFrame(pBeaconFrame+FrameLen,        &TmpLen,
						  1,                            &RSNIe,
						  1,                            &pMbss->RSNIE_Len[0],
						  pMbss->RSNIE_Len[0],      pMbss->RSN_IE[0],
						  END_OF_ARGS);
		FrameLen += TmpLen;
	}

#ifdef HOSTAPD_SUPPORT
	if (pMbss->HostapdWPS && (pMbss->WscIEBeacon.ValueLen))
		bHasWpsIE = true;
#endif


	if (bHasWpsIE)
	{
		ULONG WscTmpLen = 0;

		MakeOutgoingFrame(pBeaconFrame+FrameLen, &WscTmpLen,
						pMbss->WscIEBeacon.ValueLen, pMbss->WscIEBeacon.Value,
						END_OF_ARGS);
		FrameLen += WscTmpLen;
	}



	/* Update ERP */
    if ((pComCfg->ExtRateLen) && (PhyMode != WMODE_B))
    {
        /* fill ERP IE */
        ptr = (u8 *)pBeaconFrame + FrameLen; /* pTxD->DataByteCnt; */
        *ptr = IE_ERP;
        *(ptr + 1) = 1;
        *(ptr + 2) = pAd->ApCfg.ErpIeContent;
		FrameLen += 3;
	}

	/* fill up Channel Switch Announcement Element */
	if ((pComCfg->Channel > 14)
		&& (pComCfg->bIEEE80211H == 1)
		&& (pAd->Dot11_H.RDMode == RD_SWITCHING_MODE))
	{
		ptr = pBeaconFrame + FrameLen;
		*ptr = IE_CHANNEL_SWITCH_ANNOUNCEMENT;
		*(ptr + 1) = 3;
		*(ptr + 2) = 1;
		*(ptr + 3) = pComCfg->Channel;
		*(ptr + 4) = (pAd->Dot11_H.CSPeriod - pAd->Dot11_H.CSCount - 1);
		ptr += 5;
		FrameLen += 5;

		/* Extended Channel Switch Announcement Element */
		if (pComCfg->bExtChannelSwitchAnnouncement)
		{
			HT_EXT_CHANNEL_SWITCH_ANNOUNCEMENT_IE	HtExtChannelSwitchIe;
			build_ext_channel_switch_ie(pAd, &HtExtChannelSwitchIe);
			memmove(ptr, &HtExtChannelSwitchIe, sizeof(HT_EXT_CHANNEL_SWITCH_ANNOUNCEMENT_IE));
			ptr += sizeof(HT_EXT_CHANNEL_SWITCH_ANNOUNCEMENT_IE);
			FrameLen += sizeof(HT_EXT_CHANNEL_SWITCH_ANNOUNCEMENT_IE);
		}

		if (WMODE_CAP_AC(PhyMode)) {
			INT tp_len, wb_len = 0;
			u8 *ch_sw_wrapper;
			VHT_TXPWR_ENV_IE txpwr_env;


			*ptr = IE_CH_SWITCH_WRAPPER;
			ch_sw_wrapper = (u8 *)(ptr + 1); // reserve for length
			ptr += 2; // skip len

			if (pComCfg->RegTransmitSetting.field.BW == BW_40) {
				WIDE_BW_CH_SWITCH_ELEMENT wb_info;

				*ptr = IE_WIDE_BW_CH_SWITCH;
				*(ptr + 1) = sizeof(WIDE_BW_CH_SWITCH_ELEMENT);
				ptr += 2;
				memset(&wb_info, 0, sizeof(WIDE_BW_CH_SWITCH_ELEMENT));
				if (pComCfg->vht_bw == VHT_BW_2040)
					wb_info.new_ch_width = 0;
				else
					wb_info.new_ch_width = 1;

				if (pComCfg->vht_bw == VHT_BW_80) {
					wb_info.center_freq_1 = vht_cent_ch_freq(pAd, pComCfg->Channel);
					wb_info.center_freq_2 = 0;
				}
				memmove(ptr, &wb_info, sizeof(WIDE_BW_CH_SWITCH_ELEMENT));
				wb_len = sizeof(WIDE_BW_CH_SWITCH_ELEMENT);
				ptr += wb_len;
				wb_len += 2;
			}

			*ptr = IE_VHT_TXPWR_ENV;
			memset(&txpwr_env, 0, sizeof(VHT_TXPWR_ENV_IE));
			tp_len = build_vht_txpwr_envelope(pAd, (u8 *)&txpwr_env);
			*(ptr + 1) = tp_len;
			ptr += 2;
			memmove(ptr, &txpwr_env, tp_len);
			ptr += tp_len;
			tp_len += 2;
			*ch_sw_wrapper = wb_len + tp_len;

			FrameLen += (2 + wb_len + tp_len);
		}

	}

	/* step 5. Update HT. Since some fields might change in the same BSS. */
	if (WMODE_CAP_N(PhyMode) && (wdev->DesiredHtPhyInfo.bHtEnable))
	{
		ULONG TmpLen;
		u8 HtLen, HtLen1;
		/*u8 i; */

#ifdef RT_BIG_ENDIAN
		HT_CAPABILITY_IE HtCapabilityTmp;
		ADD_HT_INFO_IE	addHTInfoTmp;
/*		unsigned short b2lTmp, b2lTmp2; // no use */
#endif

		/* add HT Capability IE */
		HtLen = sizeof(pComCfg->HtCapability);
		HtLen1 = sizeof(pComCfg->AddHTInfo);
#ifndef RT_BIG_ENDIAN
		MakeOutgoingFrame(pBeaconFrame+FrameLen,         &TmpLen,
								  1,                                &HtCapIe,
								  1,                                &HtLen,
								 HtLen,          &pComCfg->HtCapability,
								  1,                                &AddHtInfoIe,
								  1,                                &HtLen1,
								 HtLen1,          &pComCfg->AddHTInfo,
						  END_OF_ARGS);
#else
		memmove(&HtCapabilityTmp, &pComCfg->HtCapability, HtLen);
		*(unsigned short *)(&HtCapabilityTmp.HtCapInfo) = SWAP16(*(unsigned short *)(&HtCapabilityTmp.HtCapInfo));
#ifdef UNALIGNMENT_SUPPORT
		{
			EXT_HT_CAP_INFO extHtCapInfo;

			memmove(&extHtCapInfo), &HtCapabilityTmp.ExtHtCapInfo, sizeof(EXT_HT_CAP_INFO));
			*(unsigned short *)(&extHtCapInfo) = cpu2le16(*(unsigned short *)(&extHtCapInfo));
			memmove(&HtCapabilityTmp.ExtHtCapInfo), &extHtCapInfo, sizeof(EXT_HT_CAP_INFO));
		}
#else
		*(unsigned short *)(&HtCapabilityTmp.ExtHtCapInfo) = SWAP16(*(unsigned short *)(&HtCapabilityTmp.ExtHtCapInfo));
#endif /* UNALIGNMENT_SUPPORT */

		memmove(&addHTInfoTmp, &pComCfg->AddHTInfo, HtLen1);
		*(unsigned short *)(&addHTInfoTmp.AddHtInfo2) = SWAP16(*(unsigned short *)(&addHTInfoTmp.AddHtInfo2));
		*(unsigned short *)(&addHTInfoTmp.AddHtInfo3) = SWAP16(*(unsigned short *)(&addHTInfoTmp.AddHtInfo3));

		MakeOutgoingFrame(pBeaconFrame+FrameLen,         &TmpLen,
								  1,                                &HtCapIe,
								  1,                                &HtLen,
								 HtLen,                   &HtCapabilityTmp,
								  1,                                &AddHtInfoIe,
								  1,                                &HtLen1,
								 HtLen1,                   &addHTInfoTmp,
						  END_OF_ARGS);
#endif
		FrameLen += TmpLen;

	 	/*
			P802.11n_D3.03, 7.3.2.60 Overlapping BSS Scan Parameters IE
		*/
	 	if ((pComCfg->Channel <= 14) &&
			(pComCfg->HtCapability.HtCapInfo.ChannelWidth == 1))
	 	{
			OVERLAP_BSS_SCAN_IE  OverlapScanParam;
			ULONG	TmpLen;
			u8 OverlapScanIE, ScanIELen;

			OverlapScanIE = IE_OVERLAPBSS_SCAN_PARM;
			ScanIELen = 14;
			OverlapScanParam.ScanPassiveDwell = cpu2le16(pComCfg->Dot11OBssScanPassiveDwell);
			OverlapScanParam.ScanActiveDwell = cpu2le16(pComCfg->Dot11OBssScanActiveDwell);
			OverlapScanParam.TriggerScanInt = cpu2le16(pComCfg->Dot11BssWidthTriggerScanInt);
			OverlapScanParam.PassiveTalPerChannel = cpu2le16(pComCfg->Dot11OBssScanPassiveTotalPerChannel);
			OverlapScanParam.ActiveTalPerChannel = cpu2le16(pComCfg->Dot11OBssScanActiveTotalPerChannel);
			OverlapScanParam.DelayFactor = cpu2le16(pComCfg->Dot11BssWidthChanTranDelayFactor);
			OverlapScanParam.ScanActThre = cpu2le16(pComCfg->Dot11OBssScanActivityThre);

			MakeOutgoingFrame(pBeaconFrame + FrameLen, &TmpLen,
								1,			&OverlapScanIE,
								1,			&ScanIELen,
								ScanIELen,	&OverlapScanParam,
								END_OF_ARGS);

			FrameLen += TmpLen;
	 	}


		if (WMODE_CAP_AC(PhyMode) && (pComCfg->Channel > 14))
		{
			int _len = build_vht_ies(pAd, (u8 *)(pBeaconFrame+FrameLen), SUBTYPE_BEACON);
			FrameLen += _len;
		}
	}

	/* 7.3.2.27 Extended Capabilities IE */
	{
		ULONG TmpLen, infoPos;
		u8 *pInfo;
		u8 extInfoLen;
		bool	bNeedAppendExtIE = false;
		EXT_CAP_INFO_ELEMENT	extCapInfo;


		extInfoLen = sizeof(EXT_CAP_INFO_ELEMENT);
		memset(&extCapInfo, 0, extInfoLen);

		/* P802.11n_D1.10, HT Information Exchange Support */
		if (WMODE_CAP_N(PhyMode) && (pComCfg->Channel <= 14) &&
			(pMbss->wdev.DesiredHtPhyInfo.bHtEnable) &&
			(pComCfg->bBssCoexEnable == true)
		)
		{
			extCapInfo.BssCoexistMgmtSupport = 1;
		}



		if (WMODE_CAP_AC(PhyMode) &&
			(pAd->CommonCfg.Channel > 14))
			extCapInfo.operating_mode_notification = 1;

		pInfo = (u8 *)(&extCapInfo);
		for (infoPos = 0; infoPos < extInfoLen; infoPos++)
		{
			if (pInfo[infoPos] != 0)
			{
				bNeedAppendExtIE = true;
				break;
			}
		}

		if (bNeedAppendExtIE == true)
		{
			MakeOutgoingFrame(pBeaconFrame+FrameLen, &TmpLen,
							1, &ExtCapIe,
							1, &extInfoLen,
							extInfoLen, &extCapInfo,
							END_OF_ARGS);
			FrameLen += TmpLen;
		}
	}

#ifdef WFA_VHT_PF
	if (pAd->force_vht_op_mode == true)
	{
		ULONG TmpLen;
		u8 operating_ie = IE_OPERATING_MODE_NOTIFY, operating_len = 1;
		OPERATING_MODE operating_mode;

		operating_mode.rx_nss_type = 0;
		operating_mode.rx_nss = (pAd->vht_pf_op_ss - 1);
		operating_mode.ch_width = pAd->vht_pf_op_bw;

		MakeOutgoingFrame(pBeaconFrame+FrameLen, &TmpLen,
						  1,	&operating_ie,
						  1,	&operating_len,
						  1,	&operating_mode,
						  END_OF_ARGS);
		FrameLen += TmpLen;
	}
#endif /* WFA_VHT_PF */

	/* add WMM IE here */
	if (pMbss->wdev.bWmmCapable)
	{
		ULONG TmpLen;
		u8 i;
		u8 WmeParmIe[26] = {IE_VENDOR_SPECIFIC, 24, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0, 0};
		UINT8 AIFSN[4];

		WmeParmIe[8] = pAd->ApCfg.BssEdcaParm.EdcaUpdateCount & 0x0f;


		memmove(AIFSN, pAd->ApCfg.BssEdcaParm.Aifsn, sizeof(AIFSN));


		for (i=QID_AC_BE; i<=QID_AC_VO; i++)
		{
			WmeParmIe[10+ (i*4)] = (i << 5)                                         +     /* b5-6 is ACI */
								   ((u8)pAd->ApCfg.BssEdcaParm.bACM[i] << 4)     +     /* b4 is ACM */
								   (AIFSN[i] & 0x0f);              /* b0-3 is AIFSN */
			WmeParmIe[11+ (i*4)] = (pAd->ApCfg.BssEdcaParm.Cwmax[i] << 4)           +     /* b5-8 is CWMAX */
								   (pAd->ApCfg.BssEdcaParm.Cwmin[i] & 0x0f);              /* b0-3 is CWMIN */
			WmeParmIe[12+ (i*4)] = (u8)(pAd->ApCfg.BssEdcaParm.Txop[i] & 0xff);        /* low byte of TXOP */
			WmeParmIe[13+ (i*4)] = (u8)(pAd->ApCfg.BssEdcaParm.Txop[i] >> 8);          /* high byte of TXOP */
		}

		MakeOutgoingFrame(pBeaconFrame+FrameLen,         &TmpLen,
						  26,                            WmeParmIe,
						  END_OF_ARGS);
		FrameLen += TmpLen;
	}

#ifdef AP_QLOAD_SUPPORT
	if (pAd->phy_ctrl.FlgQloadEnable != 0)
		FrameLen += QBSS_LoadElementAppend(pAd, pBeaconFrame+FrameLen);
#endif /* AP_QLOAD_SUPPORT */

	/*
		Only 802.11a APs that comply with 802.11h are required to include a
		Power Constrint Element(IE=32) in beacons and probe response frames
	*/
	if (((pComCfg->Channel > 14) && pComCfg->bIEEE80211H == true)
		)
	{
		ULONG TmpLen;
		UINT8 PwrConstraintIE = IE_POWER_CONSTRAINT;
		UINT8 PwrConstraintLen = 1;
		UINT8 PwrConstraint = pComCfg->PwrConstraint;

		/* prepare power constraint IE */
		MakeOutgoingFrame(pBeaconFrame+FrameLen,	&TmpLen,
						1,							&PwrConstraintIE,
						1,							&PwrConstraintLen,
						1,							&PwrConstraint,
						END_OF_ARGS);
		FrameLen += TmpLen;

		if (WMODE_CAP_AC(PhyMode)) {
			ULONG TmpLen;
			UINT8 vht_txpwr_env_ie = IE_VHT_TXPWR_ENV;
			UINT8 ie_len;
			VHT_TXPWR_ENV_IE txpwr_env;

			ie_len = build_vht_txpwr_envelope(pAd, (u8 *)&txpwr_env);
			MakeOutgoingFrame(pBeaconFrame+FrameLen, &TmpLen,
						1,							&vht_txpwr_env_ie,
						1,							&ie_len,
						ie_len,						&txpwr_env,
						END_OF_ARGS);
			FrameLen += TmpLen;
		}

	}


	if (WMODE_CAP_N(PhyMode) &&
		(wdev->DesiredHtPhyInfo.bHtEnable))
	{
		ULONG TmpLen;
		u8 HtLen, HtLen1;
#ifdef RT_BIG_ENDIAN
		HT_CAPABILITY_IE HtCapabilityTmp;
		ADD_HT_INFO_IE	addHTInfoTmp;
#endif
		/* add HT Capability IE */
		HtLen = sizeof(pComCfg->HtCapability);
		HtLen1 = sizeof(pComCfg->AddHTInfo);

		if (pAd->bBroadComHT == true)
		{
			u8 epigram_ie_len;
			u8 BROADCOM_HTC[4] = {0x0, 0x90, 0x4c, 0x33};
			u8 BROADCOM_AHTINFO[4] = {0x0, 0x90, 0x4c, 0x34};


			epigram_ie_len = HtLen + 4;
#ifndef RT_BIG_ENDIAN
			MakeOutgoingFrame(pBeaconFrame + FrameLen,      &TmpLen,
						  1,                                &WpaIe,
						  1,                                &epigram_ie_len,
						  4,                                &BROADCOM_HTC[0],
						  HtLen,          					&pComCfg->HtCapability,
						  END_OF_ARGS);
#else
			memmove(&HtCapabilityTmp, &pComCfg->HtCapability, HtLen);
			*(unsigned short *)(&HtCapabilityTmp.HtCapInfo) = SWAP16(*(unsigned short *)(&HtCapabilityTmp.HtCapInfo));
#ifdef UNALIGNMENT_SUPPORT
		{
			EXT_HT_CAP_INFO extHtCapInfo;

			memmove(&extHtCapInfo, &HtCapabilityTmp.ExtHtCapInfo, sizeof(EXT_HT_CAP_INFO));
			*(unsigned short *)(&extHtCapInfo) = cpu2le16(*(unsigned short *)(&extHtCapInfo));
			memmove(&HtCapabilityTmp.ExtHtCapInfo, &extHtCapInfo, sizeof(EXT_HT_CAP_INFO));
		}
#else
			*(unsigned short *)(&HtCapabilityTmp.ExtHtCapInfo) = SWAP16(*(unsigned short *)(&HtCapabilityTmp.ExtHtCapInfo));
#endif /* UNALIGNMENT_SUPPORT */

			MakeOutgoingFrame(pBeaconFrame + FrameLen,       &TmpLen,
						1,                               &WpaIe,
						1,                               &epigram_ie_len,
						4,                               &BROADCOM_HTC[0],
						HtLen,                           &HtCapabilityTmp,
						END_OF_ARGS);
#endif

			FrameLen += TmpLen;

			epigram_ie_len = HtLen1 + 4;
#ifndef RT_BIG_ENDIAN
			MakeOutgoingFrame(pBeaconFrame + FrameLen,        &TmpLen,
						  1,                                &WpaIe,
						  1,                                &epigram_ie_len,
						  4,                                &BROADCOM_AHTINFO[0],
						  HtLen1, 							&pComCfg->AddHTInfo,
						  END_OF_ARGS);
#else
			memmove(&addHTInfoTmp, &pComCfg->AddHTInfo, HtLen1);
			*(unsigned short *)(&addHTInfoTmp.AddHtInfo2) = SWAP16(*(unsigned short *)(&addHTInfoTmp.AddHtInfo2));
			*(unsigned short *)(&addHTInfoTmp.AddHtInfo3) = SWAP16(*(unsigned short *)(&addHTInfoTmp.AddHtInfo3));

			MakeOutgoingFrame(pBeaconFrame + FrameLen,         &TmpLen,
							1,                             &WpaIe,
							1,                             &epigram_ie_len,
							4,                             &BROADCOM_AHTINFO[0],
							HtLen1,                        &addHTInfoTmp,
							END_OF_ARGS);
#endif
			FrameLen += TmpLen;
		}
	}

   	/* add Ralink-specific IE here - Byte0.b0=1 for aggregation, Byte0.b1=1 for piggy-back */
{
	ULONG TmpLen;
	u8 RalinkSpecificIe[9] = {IE_VENDOR_SPECIFIC, 7, 0x00, 0x0c, 0x43, 0x00, 0x00, 0x00, 0x00};

	if (pComCfg->bAggregationCapable)
		RalinkSpecificIe[5] |= 0x1;
	if (pComCfg->bPiggyBackCapable)
		RalinkSpecificIe[5] |= 0x2;
	if (pComCfg->bRdg)
		RalinkSpecificIe[5] |= 0x4;

	if (pComCfg->b256QAM_2G && WMODE_2G_ONLY(pComCfg->PhyMode))
		RalinkSpecificIe[5] |= 0x8;

	MakeOutgoingFrame(pBeaconFrame+FrameLen, &TmpLen,
						9,                   RalinkSpecificIe,
						END_OF_ARGS);
	FrameLen += TmpLen;

}


	/* step 6. Since FrameLen may change, update TXWI. */
	if (pAd->CommonCfg.Channel > 14) {
		BeaconTransmit.field.MODE = MODE_OFDM;
		BeaconTransmit.field.MCS = MCS_RATE_6;
	}

#ifdef SPECIFIC_TX_POWER_SUPPORT
	/* Specific Power for Long-Range Beacon */
        if ((pAd->ApCfg.MBSSID[apidx].TxPwrAdj != -1) /* &&
            (BeaconTransmit.field.MODE == MODE_CCK)*/)
        {
                TxPwrAdj = pAd->ApCfg.MBSSID[apidx].TxPwrAdj;
        }
#endif /* SPECIFIC_TX_POWER_SUPPORT */

	RTMPWriteTxWI(pAd, &pAd->BeaconTxWI, false, false, true, false, false, true, 0, RESERVED_WCID,
					FrameLen, PID_MGMT, 0 /*QID_MGMT*/, 0, IFS_HTTXOP, &BeaconTransmit);

#ifdef SPECIFIC_TX_POWER_SUPPORT
		if ((IS_RT6352(pAd) || IS_MT76x2(pAd)) && (pAd->chipCap.hif_type == HIF_RLT))
			pAd->BeaconTxWI.TXWI_N.TxPwrAdj = TxPwrAdj;
#endif /* SPECIFIC_TX_POWER_SUPPORT */

	/* step 7. move BEACON TXWI and frame content to on-chip memory */
	RT28xx_UpdateBeaconToAsic(pAd, apidx, FrameLen, UpdatePos);

}


/*
    ==========================================================================
    Description:
        Pre-build All BEACON frame in the shared memory
    ==========================================================================
*/
static u8 GetBcnNum(struct rtmp_adapter *pAd)
{
	int i;
	int NumBcn;

	NumBcn = 0;
	for (i=0; i<pAd->ApCfg.BssidNum; i++)
	{
		if (pAd->ApCfg.MBSSID[i].bBcnSntReq)
		{
			pAd->ApCfg.MBSSID[i].BcnBufIdx = NumBcn;
			NumBcn ++;
		}
	}


	return NumBcn;
}


VOID APMakeAllBssBeacon(struct rtmp_adapter *pAd)
{
	INT		i, j;
	uint32_t regValue;
	u8 NumOfMacs;
	u8 NumOfBcns;
	UINT8 TXWISize = pAd->chipCap.TXWISize;

	/* before MakeBssBeacon, clear all beacon TxD's valid bit */
    /* Note: can not use MAX_MBSSID_NUM here, or
			 1. when MBSS_SUPPORT is enabled;
             2. MAX_MBSSID_NUM will be 8;
			 3. if HW_BEACON_OFFSET is 0x0200,
             we will overwrite other shared memory SRAM of chip */
	/* use pAd->ApCfg.BssidNum to avoid the case is best */

	/* choose the Beacon number */
	NumOfBcns = GetBcnNum(pAd);

	for (i=0; i<HW_BEACON_MAX_COUNT(pAd); i++)
	{
		for (j=0; j < TXWISize; j+=4)
	    {
			mt7612u_write32(pAd, pAd->BeaconOffset[i] + j, 0);
	    }
	}

	RTUSBBssBeaconStop(pAd);

	for(i=0; i<pAd->ApCfg.BssidNum; i++)
	{
		APMakeBssBeacon(pAd, i);
	}

	regValue = mt7612u_read32(pAd, MAC_BSSID_DW1);
	regValue &= 0x0000FFFF;


	/*
		Note:
			1.The MAC address of Mesh and AP-Client link are different from Main BSSID.
			2.If the Mesh link is included, its MAC address shall follow the last MBSSID's MAC by increasing 1.
			3.If the AP-Client link is included, its MAC address shall follow the Mesh interface MAC by increasing 1.
	*/
	NumOfMacs = pAd->ApCfg.BssidNum + MAX_MESH_NUM + MAX_APCLI_NUM;


	/* set Multiple BSSID mode */
	if (NumOfMacs <= 1)
	{
		pAd->ApCfg.MacMask = ~(1-1);
		/*regValue |= 0x0; */
	}
	else if (NumOfMacs <= 2)
	{
		if ((pAd->CurrentAddress[5] % 2 != 0)
		)
			DBGPRINT(RT_DEBUG_ERROR, ("The 2-BSSID mode is enabled, the BSSID byte5 MUST be the multiple of 2\n"));

		regValue |= (1<<16);
		pAd->ApCfg.MacMask = ~(2-1);
	}
	else if (NumOfMacs <= 4)
	{
		if (pAd->CurrentAddress[5] % 4 != 0)
			DBGPRINT(RT_DEBUG_ERROR, ("The 4-BSSID mode is enabled, the BSSID byte5 MUST be the multiple of 4\n"));

		regValue |= (2<<16);
		pAd->ApCfg.MacMask = ~(4-1);
	}
	else if (NumOfMacs <= 8)
	{
		if (pAd->CurrentAddress[5] % 8 != 0)
			DBGPRINT(RT_DEBUG_ERROR, ("The 8-BSSID mode is enabled, the BSSID byte5 MUST be the multiple of 8\n"));

		regValue |= (3<<16);
		pAd->ApCfg.MacMask = ~(8-1);
	}
	else if (NumOfMacs <= 16)
	{
		/* Set MULTI_BSSID_MODE_BIT4 in MAC register 0x1014 */
		regValue |= (1<<22);
		pAd->ApCfg.MacMask = ~(16-1);
	}

	/* set Multiple BSSID Beacon number */
	if (NumOfBcns > 1)
	{
		if (NumOfBcns > 8)
			regValue |= (((NumOfBcns - 1) >> 3) << 23);
		regValue |= (((NumOfBcns - 1) & 0x7)  << 18);
	}

	/* 	set as 0/1 bit-21 of MAC_BSSID_DW1(offset: 0x1014)
		to disable/enable the new MAC address assignment.  */
	if (pAd->chipCap.MBSSIDMode >= MBSSID_MODE1)
	{
		regValue |= (1 << 21);
	}

	mt7612u_write32(pAd, MAC_BSSID_DW1, regValue);


#ifndef RT_CFG80211_SUPPORT
	RTUSBBssBeaconStart(pAd);
#endif /* RT_CFG80211_SUPPORT */

}


/*
    ==========================================================================
    Description:
        Pre-build All BEACON frame in the shared memory
    ==========================================================================
*/
VOID APUpdateAllBeaconFrame(struct rtmp_adapter *pAd)
{
	INT		i;
	bool FlgQloadIsAlarmIssued = false;

	if (pAd->ApCfg.DtimCount == 0)
		pAd->ApCfg.DtimCount = pAd->ApCfg.DtimPeriod - 1;
	else
		pAd->ApCfg.DtimCount -= 1;
	/* QLOAD ALARM */
#ifdef AP_QLOAD_SUPPORT
	FlgQloadIsAlarmIssued = QBSS_LoadIsAlarmIssued(pAd);
#endif /* AP_QLOAD_SUPPORT */

	if ((pAd->ApCfg.DtimCount == 0) &&
		(((pAd->CommonCfg.Bss2040CoexistFlag & BSS_2040_COEXIST_INFO_SYNC) &&
		  (pAd->CommonCfg.bForty_Mhz_Intolerant == false)) ||
		(FlgQloadIsAlarmIssued == true)))
	{
		u8 prevBW, prevExtChOffset;
		DBGPRINT(RT_DEBUG_TRACE, ("DTIM Period reached, BSS20WidthReq=%d, Intolerant40=%d!\n",
				pAd->CommonCfg.LastBSSCoexist2040.field.BSS20WidthReq, pAd->CommonCfg.LastBSSCoexist2040.field.Intolerant40));
		pAd->CommonCfg.Bss2040CoexistFlag &= (~BSS_2040_COEXIST_INFO_SYNC);

		prevBW = pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth;
		prevExtChOffset = pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset;

		if (pAd->CommonCfg.LastBSSCoexist2040.field.BSS20WidthReq ||
			pAd->CommonCfg.LastBSSCoexist2040.field.Intolerant40 ||
			(pAd->MacTab.fAnyStaFortyIntolerant == true) ||
			(FlgQloadIsAlarmIssued == true))
		{
			pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth = 0;
			pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset = 0;
		}
		else
		{
			pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth = pAd->CommonCfg.RegTransmitSetting.field.BW;
			pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset = pAd->CommonCfg.RegTransmitSetting.field.EXTCHA;
		}
		DBGPRINT(RT_DEBUG_TRACE,("\tNow RecomWidth=%d, ExtChanOffset=%d, prevBW=%d, prevExtOffset=%d\n",
				pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth,
				pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset,
				prevBW, prevExtChOffset));
		pAd->CommonCfg.Bss2040CoexistFlag |= BSS_2040_COEXIST_INFO_NOTIFY;
	}

	for(i=0; i<pAd->ApCfg.BssidNum; i++)
	{
		APUpdateBeaconFrame(pAd, i);
	}
}


