/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : cmd.c                                                 */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains command interface functions.               */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/
#include "cprecomp.h"
#include "../hal/hpreg.h"


u16_t zfWlanReset(zdev_t *dev);
u32_t zfUpdateRxRate(zdev_t *dev);


extern void zfiUsbRecv(zdev_t *dev, zbuf_t *buf);
extern void zfiUsbRegIn(zdev_t *dev, u32_t *rsp, u16_t rspLen);
extern void zfiUsbOutComplete(zdev_t *dev, zbuf_t *buf, u8_t status, u8_t *hdr);
extern void zfiUsbRegOutComplete(zdev_t *dev);
extern u16_t zfHpReinit(zdev_t *dev, u32_t frequency);

/* Get size (byte) of driver core global data structure.    */
/* This size will be used by driver wrapper to allocate     */
/* a memory space for driver core to store global variables */
u16_t zfiGlobalDataSize(zdev_t *dev)
{
	u32_t ret;
	ret = (sizeof(struct zsWlanDev));
	zm_assert((ret>>16) == 0);
	return (u16_t)ret;
}


/* Initialize WLAN hardware and software, resource will be allocated */
/* for WLAN operation, must be called first before other function.   */
extern u16_t zfiWlanOpen(zdev_t *dev, struct zsCbFuncTbl *cbFuncTbl)
{
	/* u16_t ret;
	   u32_t i;
	   u8_t* ch;
	   u8_t  bPassive;
	*/
	u32_t devSize;
	struct zfCbUsbFuncTbl cbUsbFuncTbl;
	zmw_get_wlan_dev(dev);

	zm_debug_msg0("start");

	devSize = sizeof(struct zsWlanDev);
	/* Zeroize zsWlanDev struct */
	zfZeroMemory((u8_t *)wd, (u16_t)devSize);

#ifdef ZM_ENABLE_AGGREGATION
	zfAggInit(dev);
#endif

	zfCwmInit(dev);

	wd->commTally.RateCtrlTxMPDU = 0;
	wd->commTally.RateCtrlBAFail = 0;
	wd->preambleTypeInUsed = ZM_PREAMBLE_TYPE_SHORT;

	if (cbFuncTbl == NULL) {
		/* zfcbRecvEth() is mandatory */
		zm_assert(0);
	} else {
		if (cbFuncTbl->zfcbRecvEth == NULL) {
			/* zfcbRecvEth() is mandatory */
			zm_assert(0);
		}
		wd->zfcbAuthNotify = cbFuncTbl->zfcbAuthNotify;
		wd->zfcbAuthNotify = cbFuncTbl->zfcbAuthNotify;
		wd->zfcbAsocNotify = cbFuncTbl->zfcbAsocNotify;
		wd->zfcbDisAsocNotify = cbFuncTbl->zfcbDisAsocNotify;
		wd->zfcbApConnectNotify = cbFuncTbl->zfcbApConnectNotify;
		wd->zfcbConnectNotify = cbFuncTbl->zfcbConnectNotify;
		wd->zfcbScanNotify = cbFuncTbl->zfcbScanNotify;
		wd->zfcbMicFailureNotify = cbFuncTbl->zfcbMicFailureNotify;
		wd->zfcbApMicFailureNotify = cbFuncTbl->zfcbApMicFailureNotify;
		wd->zfcbIbssPartnerNotify = cbFuncTbl->zfcbIbssPartnerNotify;
		wd->zfcbMacAddressNotify = cbFuncTbl->zfcbMacAddressNotify;
		wd->zfcbSendCompleteIndication =
					cbFuncTbl->zfcbSendCompleteIndication;
		wd->zfcbRecvEth = cbFuncTbl->zfcbRecvEth;
		wd->zfcbRestoreBufData = cbFuncTbl->zfcbRestoreBufData;
		wd->zfcbRecv80211 = cbFuncTbl->zfcbRecv80211;
#ifdef ZM_ENABLE_CENC
		wd->zfcbCencAsocNotify = cbFuncTbl->zfcbCencAsocNotify;
#endif /* ZM_ENABLE_CENC */
		wd->zfcbClassifyTxPacket = cbFuncTbl->zfcbClassifyTxPacket;
		wd->zfcbHwWatchDogNotify = cbFuncTbl->zfcbHwWatchDogNotify;
	}

	/* add by honda 0330 */
	cbUsbFuncTbl.zfcbUsbRecv = zfiUsbRecv;
	cbUsbFuncTbl.zfcbUsbRegIn = zfiUsbRegIn;
	cbUsbFuncTbl.zfcbUsbOutComplete = zfiUsbOutComplete;
	cbUsbFuncTbl.zfcbUsbRegOutComplete = zfiUsbRegOutComplete;
	zfwUsbRegisterCallBack(dev, &cbUsbFuncTbl);
	/* Init OWN MAC address */
	wd->macAddr[0] = 0x8000;
	wd->macAddr[1] = 0x0000;
	wd->macAddr[2] = 0x0000;

	wd->regulationTable.regionCode = 0xffff;

	zfHpInit(dev, wd->frequency);

	/* init region code */
	/* wd->regulationTable.regionCode = NULL1_WORLD; //Only 2.4g RegCode */
	/* zfHpGetRegulationTablefromRegionCode(dev, NULL1_WORLD); */
	/* zfiWlanSetDot11DMode(dev , 1); //Enable 802.11d */
	/* Get the first channel */
	/* wd->frequency = zfChGetFirstChannel(dev, &bPassive); */
#ifdef ZM_AP_DEBUG
	/* wd->frequency = 2437; */
#endif

	/* STA mode */
	wd->sta.mTxRate = 0x0;
	wd->sta.uTxRate = 0x3;
	wd->sta.mmTxRate = 0x0;
	wd->sta.adapterState = ZM_STA_STATE_DISCONNECT;
	wd->sta.capability[0] = 0x01;
	wd->sta.capability[1] = 0x00;

	wd->sta.preambleTypeHT = 0;
	wd->sta.htCtrlBandwidth = 0;
	wd->sta.htCtrlSTBC = 0;
	wd->sta.htCtrlSG = 0;
	wd->sta.defaultTA = 0;
	/*wd->sta.activescanTickPerChannel =
	*ZM_TIME_ACTIVE_SCAN/ZM_MS_PER_TICK;
	*/
	{
		u8_t Dur = ZM_TIME_ACTIVE_SCAN;
		zfwGetActiveScanDur(dev, &Dur);
		wd->sta.activescanTickPerChannel = Dur / ZM_MS_PER_TICK;

	}
	wd->sta.passiveScanTickPerChannel = ZM_TIME_PASSIVE_SCAN/ZM_MS_PER_TICK;
	wd->sta.bAutoReconnect = TRUE;
	wd->sta.dropUnencryptedPkts = FALSE;

	/* set default to bypass all multicast packet for linux,
	*  window XP would set 0 by wrapper initialization
	*/
	wd->sta.bAllMulticast = 1;

	/* Initial the RIFS Status / RIFS-like frame count / RIFS count */
	wd->sta.rifsState = ZM_RIFS_STATE_DETECTING;
	wd->sta.rifsLikeFrameCnt = 0;
	wd->sta.rifsCount = 0;

	wd->sta.osRxFilter = 0;
	wd->sta.bSafeMode = 0;

	/* Common */
	zfResetSupportRate(dev, ZM_DEFAULT_SUPPORT_RATE_DISCONNECT);
	wd->beaconInterval = 100;
	wd->rtsThreshold = 2346;
	wd->fragThreshold = 32767;
	wd->wlanMode = ZM_MODE_INFRASTRUCTURE;
	wd->txMCS = 0xff;    /* AUTO */
	wd->dtim = 1;
	/* wd->txMT = 1;       *//*OFDM */
	wd->tick = 1;
	wd->maxTxPower2 = 0xff;
	wd->maxTxPower5 = 0xff;
	wd->supportMode = 0xffffffff;
	wd->ws.adhocMode = ZM_ADHOCBAND_G;
	wd->ws.autoSetFrequency = 0xff;

	/* AP mode */
	/* wd->bgMode = wd->ws.bgMode; */
	wd->ap.ssidLen[0] = 6;
	wd->ap.ssid[0][0] = 'Z';
	wd->ap.ssid[0][1] = 'D';
	wd->ap.ssid[0][2] = '1';
	wd->ap.ssid[0][3] = '2';
	wd->ap.ssid[0][4] = '2';
	wd->ap.ssid[0][5] = '1';

	/* Init the country iso name as NA */
	wd->ws.countryIsoName[0] = 0;
	wd->ws.countryIsoName[1] = 0;
	wd->ws.countryIsoName[2] = '\0';

	/* init fragmentation is disabled */
	/* zfiWlanSetFragThreshold(dev, 0); */

	/* airopeek : swSniffer 1=>on  0=>off */
	wd->swSniffer = 0;
	wd->XLinkMode = 0;

	/* jhlee HT 0 */
#if 1
	/* AP Mode*/
	/* Init HT Capability Info */
	wd->ap.HTCap.Data.ElementID = ZM_WLAN_EID_HT_CAPABILITY;
	wd->ap.HTCap.Data.Length = 26;
	/*wd->ap.HTCap.Data.SupChannelWidthSet = 0;
	wd->ap.HTCap.Data.MIMOPowerSave = 3;
	wd->ap.HTCap.Data.ShortGIfor40MHz = 0;
	wd->ap.HTCap.Data.ShortGIfor20MHz = 0;
	wd->ap.HTCap.Data.DSSSandCCKin40MHz = 0;
	*/
	wd->ap.HTCap.Data.AMPDUParam |= HTCAP_MaxRxAMPDU3;
	wd->ap.HTCap.Data.MCSSet[0] = 0xFF; /* MCS 0 ~  7 */
	wd->ap.HTCap.Data.MCSSet[1] = 0xFF; /* MCS 8 ~ 15 */

	/* Init Extended HT Capability Info */
	wd->ap.ExtHTCap.Data.ElementID = ZM_WLAN_EID_EXTENDED_HT_CAPABILITY;
	wd->ap.ExtHTCap.Data.Length = 22;
	wd->ap.ExtHTCap.Data.ControlChannel = 6;
	/* wd->ap.ExtHTCap.Data.ExtChannelOffset = 3; */
	wd->ap.ExtHTCap.Data.ChannelInfo |= ExtHtCap_RecomTxWidthSet;
	/* wd->ap.ExtHTCap.Data.RIFSMode = 1; */
	wd->ap.ExtHTCap.Data.OperatingInfo |= 1;

	/* STA Mode*/
	/* Init HT Capability Info */
	wd->sta.HTCap.Data.ElementID = ZM_WLAN_EID_HT_CAPABILITY;
	wd->sta.HTCap.Data.Length = 26;

	/* Test with 5G-AP : 7603 */
	/* wd->sta.HTCap.Data.SupChannelWidthSet = 1; */
	wd->sta.HTCap.Data.HtCapInfo |= HTCAP_SMEnabled;
	wd->sta.HTCap.Data.HtCapInfo |= HTCAP_SupChannelWidthSet;
	wd->sta.HTCap.Data.HtCapInfo |= HTCAP_ShortGIfor40MHz;
	wd->sta.HTCap.Data.HtCapInfo |= HTCAP_DSSSandCCKin40MHz;
#ifndef ZM_DISABLE_AMSDU8K_SUPPORT
	wd->sta.HTCap.Data.HtCapInfo |= HTCAP_MaxAMSDULength;
#endif
	/*wd->sta.HTCap.Data.MIMOPowerSave = 0;
	wd->sta.HTCap.Data.ShortGIfor40MHz = 0;
	wd->sta.HTCap.Data.ShortGIfor20MHz = 0;
	wd->sta.HTCap.Data.DSSSandCCKin40MHz = 0;
	*/
	wd->sta.HTCap.Data.AMPDUParam |= HTCAP_MaxRxAMPDU3;
	wd->sta.HTCap.Data.MCSSet[0] = 0xFF; /* MCS 0 ~  7 */
	wd->sta.HTCap.Data.MCSSet[1] = 0xFF; /* MCS 8 ~ 15 */
	wd->sta.HTCap.Data.PCO |= HTCAP_TransmissionTime3;
	/* wd->sta.HTCap.Data.TransmissionTime = 0; */
	/* Init Extended HT Capability Info */
	wd->sta.ExtHTCap.Data.ElementID = ZM_WLAN_EID_EXTENDED_HT_CAPABILITY;
	wd->sta.ExtHTCap.Data.Length = 22;
	wd->sta.ExtHTCap.Data.ControlChannel = 6;

	/* wd->sta.ExtHTCap.Data.ExtChannelOffset |= 3; */
	wd->sta.ExtHTCap.Data.ChannelInfo |= ExtHtCap_ExtChannelOffsetBelow;

	/* wd->sta.ExtHTCap.Data.RecomTxWidthSet = 1; */
	/* wd->sta.ExtHTCap.Data.RIFSMode = 1; */
	wd->sta.ExtHTCap.Data.OperatingInfo |= 1;
#endif

#if 0
	/* WME test code */
	wd->ap.qosMode[0] = 1;
#endif

	wd->ledStruct.ledMode[0] = 0x2221;
	wd->ledStruct.ledMode[1] = 0x2221;

	zfTimerInit(dev);

	ZM_PERFORMANCE_INIT(dev);

	zfBssInfoCreate(dev);
	zfScanMgrInit(dev);
	zfPowerSavingMgrInit(dev);

#if 0
	/* Test code */
	{
		u32_t key[4] = {0xffffffff, 0xff, 0, 0};
		u16_t addr[3] = {0x8000, 0x01ab, 0x0000};
		/*zfSetKey(dev, 0, 0, ZM_WEP64, addr, key);
		zfSetKey(dev, 0, 0, ZM_AES, addr, key);
		zfSetKey(dev, 64, 0, 1, wd->macAddr, key);
		*/
	}
#endif

	/* WME settings */
	wd->ws.staWmeEnabled = 1;           /* Enable WME by default */
#define ZM_UAPSD_Q_SIZE 32 /* 2^N */
	wd->ap.uapsdQ = zfQueueCreate(dev, ZM_UAPSD_Q_SIZE);
	zm_assert(wd->ap.uapsdQ != NULL);
	wd->sta.uapsdQ = zfQueueCreate(dev, ZM_UAPSD_Q_SIZE);
	zm_assert(wd->sta.uapsdQ != NULL);

	/* zfHpInit(dev, wd->frequency); */

	/* MAC address */
	/* zfHpSetMacAddress(dev, wd->macAddr, 0); */
	zfHpGetMacAddress(dev);

	zfCoreSetFrequency(dev, wd->frequency);

#if ZM_PCI_LOOP_BACK == 1
	zfwWriteReg(dev, ZM_REG_PCI_CONTROL, 6);
#endif /* #if ZM_PCI_LOOP_BACK == 1 */

	/* zfiWlanSetDot11DMode(dev , 1); // Enable 802.11d */
	/* zfiWlanSetDot11HDFSMode(dev , 1); // Enable 802.11h DFS */
	wd->sta.DFSEnable = 1;
	wd->sta.capability[1] |= ZM_BIT_0;

	/* zfiWlanSetFrequency(dev, 5260000, TRUE); */
	/* zfiWlanSetAniMode(dev , 1); // Enable ANI */

	/* Trgger Rx DMA */
	zfHpStartRecv(dev);

	zm_debug_msg0("end");

	return 0;
}

/* WLAN hardware will be shutdown and all resource will be release */
u16_t zfiWlanClose(zdev_t *dev)
{
	zmw_get_wlan_dev(dev);

	zm_msg0_init(ZM_LV_0, "enter");

	wd->state = ZM_WLAN_STATE_CLOSEDED;

	/* zfiWlanDisable(dev, 1); */
	zfWlanReset(dev);

	zfHpStopRecv(dev);

	/* Disable MAC */
	/* Disable PHY */
	/* Disable RF */

	zfHpRelease(dev);

	zfQueueDestroy(dev, wd->ap.uapsdQ);
	zfQueueDestroy(dev, wd->sta.uapsdQ);

	zfBssInfoDestroy(dev);

#ifdef ZM_ENABLE_AGGREGATION
	/* add by honda */
	zfAggRxFreeBuf(dev, 1);  /* 1 for release structure memory */
	/* end of add by honda */
#endif

	zm_msg0_init(ZM_LV_0, "exit");

	return 0;
}

void zfGetWrapperSetting(zdev_t *dev)
{
	u8_t bPassive;
	u16_t vapId = 0;

	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();
#if 0
	if ((wd->ws.countryIsoName[0] != 0)
		|| (wd->ws.countryIsoName[1] != 0)
		|| (wd->ws.countryIsoName[2] != '\0')) {
		zfHpGetRegulationTablefromRegionCode(dev,
		zfHpGetRegionCodeFromIsoName(dev, wd->ws.countryIsoName));
	}
#endif
	zmw_enter_critical_section(dev);

	wd->wlanMode = wd->ws.wlanMode;

	/* set channel */
	if (wd->ws.frequency) {
		wd->frequency = wd->ws.frequency;
		wd->ws.frequency = 0;
	} else {
		wd->frequency = zfChGetFirstChannel(dev, &bPassive);

		if (wd->wlanMode == ZM_MODE_IBSS) {
			if (wd->ws.adhocMode == ZM_ADHOCBAND_A)
				wd->frequency = ZM_CH_A_36;
			else
				wd->frequency = ZM_CH_G_6;
		}
	}
#ifdef ZM_AP_DEBUG
	/* honda add for debug, 2437 channel 6, 2452 channel 9 */
	wd->frequency = 2437;
	/* end of add by honda */
#endif

	/* set preamble type */
	switch (wd->ws.preambleType) {
	case ZM_PREAMBLE_TYPE_AUTO:
	case ZM_PREAMBLE_TYPE_SHORT:
	case ZM_PREAMBLE_TYPE_LONG:
		wd->preambleType = wd->ws.preambleType;
		break;
	default:
		wd->preambleType = ZM_PREAMBLE_TYPE_SHORT;
		break;
	}
	wd->ws.preambleType = 0;

	if (wd->wlanMode == ZM_MODE_AP) {
		vapId = zfwGetVapId(dev);

		if (vapId == 0xffff) {
			wd->ap.authAlgo[0] = wd->ws.authMode;
			wd->ap.encryMode[0] = wd->ws.encryMode;
		} else {
			wd->ap.authAlgo[vapId + 1] = wd->ws.authMode;
			wd->ap.encryMode[vapId + 1] = wd->ws.encryMode;
		}
		wd->ws.authMode = 0;
		wd->ws.encryMode = ZM_NO_WEP;

		/* Get beaconInterval from WrapperSetting */
		if ((wd->ws.beaconInterval >= 20) &&
					(wd->ws.beaconInterval <= 1000))
			wd->beaconInterval = wd->ws.beaconInterval;
		else
			wd->beaconInterval = 100; /* 100ms */

		if (wd->ws.dtim > 0)
			wd->dtim = wd->ws.dtim;
		else
			wd->dtim = 1;


		wd->ap.qosMode = wd->ws.apWmeEnabled & 0x1;
		wd->ap.uapsdEnabled = (wd->ws.apWmeEnabled & 0x2) >> 1;
	} else {
		wd->sta.authMode = wd->ws.authMode;
		wd->sta.currentAuthMode = wd->ws.authMode;
		wd->sta.wepStatus = wd->ws.wepStatus;

		if (wd->ws.beaconInterval)
			wd->beaconInterval = wd->ws.beaconInterval;
		else
			wd->beaconInterval = 0x64;

		if (wd->wlanMode == ZM_MODE_IBSS) {
			/* 1. Set default channel 6 (2437MHz) */
			/* wd->frequency = 2437; */

			/* 2. Otus support 802.11g Mode */
			if ((wd->ws.adhocMode == ZM_ADHOCBAND_G) ||
				(wd->ws.adhocMode == ZM_ADHOCBAND_BG) ||
				(wd->ws.adhocMode == ZM_ADHOCBAND_ABG))
				wd->wfc.bIbssGMode = 1;
			else
				wd->wfc.bIbssGMode = 0;

			/* 3. set short preamble  */
			/* wd->sta.preambleType = ZM_PREAMBLE_TYPE_SHORT; */
		}

		/* set ATIM window */
		if (wd->ws.atimWindow)
			wd->sta.atimWindow = wd->ws.atimWindow;
		else {
			/* wd->sta.atimWindow = 0x0a; */
			wd->sta.atimWindow = 0;
		}

		/* wd->sta.connectingHiddenAP = 1;
		   wd->ws.connectingHiddenAP;
		*/
		wd->sta.dropUnencryptedPkts = wd->ws.dropUnencryptedPkts;
		wd->sta.ibssJoinOnly = wd->ws.ibssJoinOnly;

		if (wd->ws.bDesiredBssid) {
			zfMemoryCopy(wd->sta.desiredBssid,
						wd->ws.desiredBssid, 6);
			wd->sta.bDesiredBssid = TRUE;
			wd->ws.bDesiredBssid = FALSE;
		} else
			wd->sta.bDesiredBssid = FALSE;

		/* check ssid */
		if (wd->ws.ssidLen != 0) {
			if ((!zfMemoryIsEqual(wd->ws.ssid, wd->sta.ssid,
				wd->sta.ssidLen)) ||
				(wd->ws.ssidLen != wd->sta.ssidLen) ||
				(wd->sta.authMode == ZM_AUTH_MODE_WPA) ||
				(wd->sta.authMode == ZM_AUTH_MODE_WPAPSK) ||
				(wd->ws.staWmeQosInfo != 0)) {
				/* if u-APSD test(set QosInfo), clear
				   connectByReasso to do association
				   (not reassociation)
				*/
				wd->sta.connectByReasso = FALSE;
				wd->sta.failCntOfReasso = 0;
				wd->sta.pmkidInfo.bssidCount = 0;

				wd->sta.ssidLen = wd->ws.ssidLen;
				zfMemoryCopy(wd->sta.ssid, wd->ws.ssid,
							wd->sta.ssidLen);

				if (wd->sta.ssidLen < 32)
					wd->sta.ssid[wd->sta.ssidLen] = 0;
			}
		} else {
			/* ANY BSS */
			wd->sta.ssid[0] = 0;
			wd->sta.ssidLen = 0;
		}

		wd->sta.wmeEnabled = wd->ws.staWmeEnabled;
		wd->sta.wmeQosInfo = wd->ws.staWmeQosInfo;

	}

	zmw_leave_critical_section(dev);
}

u16_t zfWlanEnable(zdev_t *dev)
{
	u8_t bssid[6] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
	u16_t i;

	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();

	if (wd->wlanMode == ZM_MODE_UNKNOWN) {
		zm_debug_msg0("Unknown Mode...Skip...");
		return 0;
	}

	if (wd->wlanMode == ZM_MODE_AP) {
		u16_t vapId;

		vapId = zfwGetVapId(dev);

		if (vapId == 0xffff) {
			/* AP mode */
			zfApInitStaTbl(dev);

			/* AP default parameters */
			wd->bRate = 0xf;
			wd->gRate = 0xff;
			wd->bRateBasic = 0xf;
			wd->gRateBasic = 0x0;
			/* wd->beaconInterval = 100; */
			wd->ap.apBitmap = 1;
			wd->ap.beaconCounter = 0;
			/* wd->ap.vapNumber = 1; //mark by ygwei for Vap */

			wd->ap.hideSsid[0] = 0;
			wd->ap.staAgingTimeSec = 10*60;
			wd->ap.staProbingTimeSec = 60;

			for (i = 0; i < ZM_MAX_AP_SUPPORT; i++)
				wd->ap.bcmcHead[i] = wd->ap.bcmcTail[i] = 0;

			/* wd->ap.uniHead = wd->ap.uniTail = 0; */

			/* load AP parameters */
			wd->bRateBasic = wd->ws.bRateBasic;
			wd->gRateBasic = wd->ws.gRateBasic;
			wd->bgMode = wd->ws.bgMode;
			if ((wd->ws.ssidLen <= 32) && (wd->ws.ssidLen != 0)) {
				wd->ap.ssidLen[0] = wd->ws.ssidLen;
				for (i = 0; i < wd->ws.ssidLen; i++)
					wd->ap.ssid[0][i] = wd->ws.ssid[i];
				wd->ws.ssidLen = 0; /* Reset Wrapper Variable */
			}

			if (wd->ap.encryMode[0] == 0)
				wd->ap.capab[0] = 0x001;
			else
				wd->ap.capab[0] = 0x011;
			/* set Short Slot Time bit if not 11b */
			if (wd->ap.wlanType[0] != ZM_WLAN_TYPE_PURE_B)
				wd->ap.capab[0] |= 0x400;

			/* wd->ap.vapNumber = 1; //mark by ygwei for Vap Test */
		} else {
#if 0
			/* VAP Test Code */
			wd->ap.apBitmap = 0x3;
			wd->ap.capab[1] = 0x401;
			wd->ap.ssidLen[1] = 4;
			wd->ap.ssid[1][0] = 'v';
			wd->ap.ssid[1][1] = 'a';
			wd->ap.ssid[1][2] = 'p';
			wd->ap.ssid[1][3] = '1';
			wd->ap.authAlgo[1] = wd->ws.authMode;
			wd->ap.encryMode[1] = wd->ws.encryMode;
			wd->ap.vapNumber = 2;
#else
			/* VAP Test Code */
			wd->ap.apBitmap = 0x1 | (0x01 << (vapId+1));

			if ((wd->ws.ssidLen <= 32) && (wd->ws.ssidLen != 0)) {
				wd->ap.ssidLen[vapId+1] = wd->ws.ssidLen;
				for (i = 0; i < wd->ws.ssidLen; i++)
					wd->ap.ssid[vapId+1][i] =
								wd->ws.ssid[i];
				wd->ws.ssidLen = 0; /* Reset Wrapper Variable */
			}

			if (wd->ap.encryMode[vapId+1] == 0)
				wd->ap.capab[vapId+1] = 0x401;
			else
				wd->ap.capab[vapId+1] = 0x411;

			wd->ap.authAlgo[vapId+1] = wd->ws.authMode;
			wd->ap.encryMode[vapId+1] = wd->ws.encryMode;

			/* Need to be modified when VAP is used */
			/* wd->ap.vapNumber = 2; */
#endif
		}

		wd->ap.vapNumber++;

		zfCoreSetFrequency(dev, wd->frequency);

		zfInitMacApMode(dev);

		/* Disable protection mode */
		zfApSetProtectionMode(dev, 0);

		zfApSendBeacon(dev);
	} else { /*if (wd->wlanMode == ZM_MODE_AP) */

		zfScanMgrScanStop(dev, ZM_SCAN_MGR_SCAN_INTERNAL);
		zfScanMgrScanStop(dev, ZM_SCAN_MGR_SCAN_EXTERNAL);

		zmw_enter_critical_section(dev);
		wd->sta.oppositeCount = 0;    /* reset opposite count */
		/* wd->sta.bAutoReconnect = wd->sta.bAutoReconnectEnabled; */
		/* wd->sta.scanWithSSID = 0; */
		zfStaInitOppositeInfo(dev);
		zmw_leave_critical_section(dev);

		zfStaResetStatus(dev, 0);

		if ((wd->sta.cmDisallowSsidLength != 0) &&
		(wd->sta.ssidLen == wd->sta.cmDisallowSsidLength) &&
		(zfMemoryIsEqual(wd->sta.ssid, wd->sta.cmDisallowSsid,
		wd->sta.ssidLen)) &&
		(wd->sta.wepStatus == ZM_ENCRYPTION_TKIP)) {/*countermeasures*/
			zm_debug_msg0("countermeasures disallow association");
		} else {
			switch (wd->wlanMode) {
			case ZM_MODE_IBSS:
				/* some registers may be set here */
				if (wd->sta.authMode == ZM_AUTH_MODE_WPA2PSK)
					zfHpSetApStaMode(dev,
					ZM_HAL_80211_MODE_IBSS_WPA2PSK);
				else
					zfHpSetApStaMode(dev,
					ZM_HAL_80211_MODE_IBSS_GENERAL);

				zm_msg0_mm(ZM_LV_0, "ZM_MODE_IBSS");
				zfIbssConnectNetwork(dev);
				break;

			case ZM_MODE_INFRASTRUCTURE:
				/* some registers may be set here */
				zfHpSetApStaMode(dev, ZM_HAL_80211_MODE_STA);

				zfInfraConnectNetwork(dev);
				break;

			case ZM_MODE_PSEUDO:
				/* some registers may be set here */
				zfHpSetApStaMode(dev, ZM_HAL_80211_MODE_STA);

				zfUpdateBssid(dev, bssid);
				zfCoreSetFrequency(dev, wd->frequency);
				break;

			default:
				break;
			}
		}

	}


	/* if ((wd->wlanMode != ZM_MODE_INFRASTRUCTURE) &&
		(wd->wlanMode != ZM_MODE_AP))
	*/
	if (wd->wlanMode == ZM_MODE_PSEUDO) {
		/* Reset Wlan status */
		zfWlanReset(dev);

		if (wd->zfcbConnectNotify != NULL)
			wd->zfcbConnectNotify(dev, ZM_STATUS_MEDIA_CONNECT,
								wd->sta.bssid);
		zfChangeAdapterState(dev, ZM_STA_STATE_CONNECTED);
	}


	if (wd->wlanMode == ZM_MODE_AP) {
		if (wd->zfcbConnectNotify != NULL)
			wd->zfcbConnectNotify(dev, ZM_STATUS_MEDIA_CONNECT,
								wd->sta.bssid);
		/* zfChangeAdapterState(dev, ZM_STA_STATE_CONNECTED); */
	}

	/* Assign default Tx Rate */
	if (wd->sta.EnableHT) {
		u32_t oneTxStreamCap;
		oneTxStreamCap = (zfHpCapability(dev) &
						ZM_HP_CAP_11N_ONE_TX_STREAM);
		if (oneTxStreamCap)
			wd->CurrentTxRateKbps = 135000;
		else
			wd->CurrentTxRateKbps = 270000;
		wd->CurrentRxRateKbps = 270000;
	} else {
		wd->CurrentTxRateKbps = 54000;
		wd->CurrentRxRateKbps = 54000;
	}

	wd->state = ZM_WLAN_STATE_ENABLED;

	return 0;
}

/* Enable/disable Wlan operation */
u16_t zfiWlanEnable(zdev_t *dev)
{
	u16_t ret;

	zmw_get_wlan_dev(dev);

	zm_msg0_mm(ZM_LV_1, "Enable Wlan");

	zfGetWrapperSetting(dev);

	zfZeroMemory((u8_t *) &wd->trafTally, sizeof(struct zsTrafTally));

	/* Reset cmMicFailureCount to 0 for new association request */
	if (wd->sta.cmMicFailureCount == 1) {
		zfTimerCancel(dev, ZM_EVENT_CM_TIMER);
		wd->sta.cmMicFailureCount = 0;
	}

	zfFlushVtxq(dev);
	if ((wd->queueFlushed & 0x10) != 0)
		zfHpUsbReset(dev);

	ret = zfWlanEnable(dev);

	return ret;
}
/* Add a flag named ResetKeyCache to show if KeyCache should be cleared.
   for hostapd in AP mode, if driver receives iwconfig ioctl
   after setting group key, it shouldn't clear KeyCache.
*/
u16_t zfiWlanDisable(zdev_t *dev, u8_t ResetKeyCache)
{
	u16_t  i;
	u8_t isConnected;

	zmw_get_wlan_dev(dev);

#ifdef ZM_ENABLE_IBSS_WPA2PSK
	zmw_declare_for_critical_section();
#endif
	wd->state = ZM_WLAN_STATE_DISABLED;

	zm_msg0_mm(ZM_LV_1, "Disable Wlan");

	if (wd->wlanMode != ZM_MODE_AP) {
		isConnected = zfStaIsConnected(dev);

		if ((wd->wlanMode == ZM_MODE_INFRASTRUCTURE) &&
			(wd->sta.currentAuthMode != ZM_AUTH_MODE_WPA2)) {
			/* send deauthentication frame */
			if (isConnected) {
				/* zfiWlanDeauth(dev, NULL, 0); */
				zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH,
						wd->sta.bssid, 3, 0, 0);
				/* zmw_debug_msg0("send a Deauth frame!"); */
			}
		}

		/* Remove all the connected peer stations */
		if (wd->wlanMode == ZM_MODE_IBSS) {
			wd->sta.ibssBssIsCreator = 0;
			zfTimerCancel(dev, ZM_EVENT_IBSS_MONITOR);
			zfStaIbssMonitoring(dev, 1);
		}

#ifdef ZM_ENABLE_IBSS_WPA2PSK
		zmw_enter_critical_section(dev);
		wd->sta.ibssWpa2Psk = 0;
		zmw_leave_critical_section(dev);
#endif

		wd->sta.wpaState = ZM_STA_WPA_STATE_INIT;

		/* reset connect timeout counter */
		wd->sta.connectTimeoutCount = 0;

		/* reset connectState to None */
		wd->sta.connectState = ZM_STA_CONN_STATE_NONE;

		/* reset leap enable variable */
		wd->sta.leapEnabled = 0;

		/* Disable the RIFS Status/RIFS-like frame count/RIFS count */
		if (wd->sta.rifsState == ZM_RIFS_STATE_DETECTED)
			zfHpDisableRifs(dev);
		wd->sta.rifsState = ZM_RIFS_STATE_DETECTING;
		wd->sta.rifsLikeFrameCnt = 0;
		wd->sta.rifsCount = 0;

		wd->sta.osRxFilter = 0;
		wd->sta.bSafeMode = 0;

		zfChangeAdapterState(dev, ZM_STA_STATE_DISCONNECT);
		if (ResetKeyCache)
			zfHpResetKeyCache(dev);

		if (isConnected) {
			if (wd->zfcbConnectNotify != NULL)
				wd->zfcbConnectNotify(dev,
				ZM_STATUS_MEDIA_CONNECTION_DISABLED,
				wd->sta.bssid);
		} else {
			if (wd->zfcbConnectNotify != NULL)
				wd->zfcbConnectNotify(dev,
				ZM_STATUS_MEDIA_DISABLED, wd->sta.bssid);
		}
	} else { /* if (wd->wlanMode == ZM_MODE_AP) */
		for (i = 0; i < ZM_MAX_STA_SUPPORT; i++) {
			/* send deauthentication frame */
			if (wd->ap.staTable[i].valid == 1) {
				/* Reason : Sending station is leaving */
				zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH,
				wd->ap.staTable[i].addr, 3, 0, 0);
			}
		}

		if (ResetKeyCache)
			zfHpResetKeyCache(dev);

		wd->ap.vapNumber--;
	}

	/* stop beacon */
	zfHpDisableBeacon(dev);

	/* Flush VTxQ and MmQ */
	zfFlushVtxq(dev);
	/* Flush AP PS queues */
	zfApFlushBufferedPsFrame(dev);
	/* Free buffer in defragment list*/
	zfAgingDefragList(dev, 1);

#ifdef ZM_ENABLE_AGGREGATION
	/* add by honda */
	zfAggRxFreeBuf(dev, 0);  /* 1 for release structure memory */
	/* end of add by honda */
#endif

	/* Clear the information for the peer stations
	of IBSS or AP of Station mode
	*/
	zfZeroMemory((u8_t *)wd->sta.oppositeInfo,
			sizeof(struct zsOppositeInfo) * ZM_MAX_OPPOSITE_COUNT);

	/* Turn off Software WEP/TKIP */
	if (wd->sta.SWEncryptEnable != 0) {
		zm_debug_msg0("Disable software encryption");
		zfStaDisableSWEncryption(dev);
	}

	/* Improve WEP/TKIP performace with HT AP,
	detail information please look bug#32495 */
	/* zfHpSetTTSIFSTime(dev, 0x8); */

	return 0;
}

u16_t zfiWlanSuspend(zdev_t *dev)
{
	zmw_get_wlan_dev(dev);
	zmw_declare_for_critical_section();

	/* Change the HAL state to init so that any packet
	can't be transmitted between resume & HAL reinit.
	This would cause the chip hang issue in OTUS.
	*/
	zmw_enter_critical_section(dev);
	wd->halState = ZM_HAL_STATE_INIT;
	zmw_leave_critical_section(dev);

	return 0;
}

u16_t zfiWlanResume(zdev_t *dev, u8_t doReconn)
{
	u16_t ret;
	zmw_get_wlan_dev(dev);
	zmw_declare_for_critical_section();

	/* Redownload firmware, Reinit MAC,PHY,RF */
	zfHpReinit(dev, wd->frequency);

	/* Set channel according to AP's configuration */
	zfCoreSetFrequencyExV2(dev, wd->frequency, wd->BandWidth40,
		wd->ExtOffset, NULL, 1);

	zfHpSetMacAddress(dev, wd->macAddr, 0);

	/* Start Rx */
	zfHpStartRecv(dev);

	zfFlushVtxq(dev);

	if (wd->wlanMode != ZM_MODE_INFRASTRUCTURE &&
			wd->wlanMode != ZM_MODE_IBSS)
		return 1;

	zm_msg0_mm(ZM_LV_1, "Resume Wlan");
	if ((zfStaIsConnected(dev)) || (zfStaIsConnecting(dev))) {
		if (doReconn == 1) {
			zm_msg0_mm(ZM_LV_1, "Re-connect...");
			zmw_enter_critical_section(dev);
			wd->sta.connectByReasso = FALSE;
			zmw_leave_critical_section(dev);

			zfWlanEnable(dev);
		} else if (doReconn == 0)
			zfHpSetRollCallTable(dev);
	}

	ret = 0;

	return ret;
}

/************************************************************************/
/*                                                                      */
/*    FUNCTION DESCRIPTION                 zfiWlanFlushAllQueuedBuffers */
/*      Flush Virtual TxQ, MmQ, PS frames and defragment list           */
/*                                                                      */
/*    INPUTS                                                            */
/*      dev : device pointer                                            */
/*                                                                      */
/*    OUTPUTS                                                           */
/*      None                                                            */
/*                                                                      */
/*    AUTHOR                                                            */
/*      Stephen Chen        Atheros Communications, INC.    2007.1      */
/*                                                                      */
/************************************************************************/
void zfiWlanFlushAllQueuedBuffers(zdev_t *dev)
{
	/* Flush VTxQ and MmQ */
	zfFlushVtxq(dev);
	/* Flush AP PS queues */
	zfApFlushBufferedPsFrame(dev);
	/* Free buffer in defragment list*/
	zfAgingDefragList(dev, 1);
}

/* Do WLAN site survey */
u16_t zfiWlanScan(zdev_t *dev)
{
	u16_t ret = 1;
	zmw_get_wlan_dev(dev);

	zm_debug_msg0("");

	zmw_declare_for_critical_section();

	zmw_enter_critical_section(dev);

	if (wd->wlanMode == ZM_MODE_AP) {
		wd->heartBeatNotification |= ZM_BSSID_LIST_SCAN;
		wd->sta.scanFrequency = 0;
		/* wd->sta.pUpdateBssList->bssCount = 0; */
		ret = 0;
	} else {
#if 0
		if (!zfStaBlockWlanScan(dev)) {
			zm_debug_msg0("scan request");
			/*zfTimerSchedule(dev, ZM_EVENT_SCAN, ZM_TICK_ZERO);*/
			ret = 0;
			goto start_scan;
		}
#else
		goto start_scan;
#endif
	}

	zmw_leave_critical_section(dev);

	return ret;

start_scan:
	zmw_leave_critical_section(dev);

	if (wd->ledStruct.LEDCtrlFlagFromReg & ZM_LED_CTRL_FLAG_ALPHA) {
		/* flag for Alpha */
		wd->ledStruct.LEDCtrlFlag |= ZM_LED_CTRL_FLAG_ALPHA;
	}

	ret = zfScanMgrScanStart(dev, ZM_SCAN_MGR_SCAN_EXTERNAL);

	zm_debug_msg1("ret = ", ret);

	return ret;
}


/* rate         	*/
/*    0 : AUTO  	*/
/*    1 : CCK 1M 	*/
/*    2 : CCK 2M 	*/
/*    3 : CCK 5.5M 	*/
/*    4 : CCK 11M 	*/
/*    5 : OFDM 6M 	*/
/*    6 : OFDM 9M 	*/
/*    7 : OFDM 12M 	*/
/*    8 : OFDM 18M 	*/
/*    9 : OFDM 24M 	*/
/*    10 : OFDM 36M 	*/
/*    11 : OFDM 48M 	*/
/*    12 : OFDM 54M 	*/
/*    13 : MCS 0 	*/
/*    28 : MCS 15 	*/
u16_t zcRateToMCS[] =
    {0xff, 0, 1, 2, 3, 0xb, 0xf, 0xa, 0xe, 0x9, 0xd, 0x8, 0xc};
u16_t zcRateToMT[] = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};

u16_t zfiWlanSetTxRate(zdev_t *dev, u16_t rate)
{
	/* jhlee HT 0 */
	zmw_get_wlan_dev(dev);

	if (rate <= 12) {
		wd->txMCS = zcRateToMCS[rate];
		wd->txMT = zcRateToMT[rate];
		return ZM_SUCCESS;
	} else if ((rate <= 28) || (rate == 13 + 32)) {
		wd->txMCS = rate - 12 - 1;
		wd->txMT = 2;
		return ZM_SUCCESS;
	}

	return ZM_ERR_INVALID_TX_RATE;
}

const u32_t zcRateIdToKbps40M[] =
{
	1000, 2000, 5500, 11000, /* 1M, 2M, 5M, 11M ,  0  1  2  3             */
	6000, 9000, 12000, 18000, /* 6M  9M  12M  18M ,  4  5  6  7           */
	24000, 36000, 48000, 54000, /* 24M  36M  48M  54M ,  8  9 10 11       */
	13500, 27000, 40500, 54000, /* MCS0 MCS1 MCS2 MCS3 , 12 13 14 15      */
	81000, 108000, 121500, 135000, /* MCS4 MCS5 MCS6 MCS7 , 16 17 18 19   */
	27000, 54000, 81000, 108000, /* MCS8 MCS9 MCS10 MCS11 , 20 21 22 23   */
	162000, 216000, 243000, 270000, /*MCS12 MCS13 MCS14 MCS15, 24 25 26 27*/
	270000, 300000, 150000  /* MCS14SG, MCS15SG, MCS7SG , 28 29 30        */
};

const u32_t zcRateIdToKbps20M[] =
{
	1000, 2000, 5500, 11000, /* 1M, 2M, 5M, 11M ,  0  1  2  3             */
	6000, 9000, 12000, 18000, /* 6M  9M  12M  18M  ,  4  5  6  7          */
	24000, 36000, 48000, 54000, /* 24M  36M  48M  54M ,  8  9 10 11       */
	6500, 13000, 19500, 26000, /* MCS0 MCS1 MCS2 MCS3 , 12 13 14 15       */
	39000, 52000, 58500, 65000, /* MCS4 MCS5 MCS6 MCS7 , 16 17 18 19      */
	13000, 26000, 39000, 52000, /* MCS8 MCS9 MCS10 MCS11 , 20 21 22 23    */
	78000, 104000, 117000, 130000, /* MCS12 MCS13 MCS14 MCS15, 24 25 26 27*/
	130000, 144400, 72200  /* MCS14SG, MCS15SG, MSG7SG , 28 29 30         */
};

u32_t zfiWlanQueryTxRate(zdev_t *dev)
{
	u8_t rateId = 0xff;
	zmw_get_wlan_dev(dev);
	zmw_declare_for_critical_section();

	/* If Tx rate had not been trained, return maximum Tx rate instead */
	if ((wd->wlanMode == ZM_MODE_INFRASTRUCTURE) &&
						(zfStaIsConnected(dev))) {
		zmw_enter_critical_section(dev);
		/* Not in fixed rate mode */
		if (wd->txMCS == 0xff) {
			if ((wd->sta.oppositeInfo[0].rcCell.flag &
							ZM_RC_TRAINED_BIT) == 0)
				rateId = wd->sta.oppositeInfo[0].rcCell. \
				operationRateSet[wd->sta.oppositeInfo[0]. \
				rcCell.operationRateCount-1];
			else
				rateId = wd->sta.oppositeInfo[0].rcCell. \
				operationRateSet[wd->sta.oppositeInfo[0]. \
				rcCell.currentRateIndex];
		}
		zmw_leave_critical_section(dev);
	}

	if (rateId != 0xff) {
		if (wd->sta.htCtrlBandwidth)
			return zcRateIdToKbps40M[rateId];
		else
			return zcRateIdToKbps20M[rateId];
	} else
		return wd->CurrentTxRateKbps;
}

void zfWlanUpdateRxRate(zdev_t *dev, struct zsAdditionInfo *addInfo)
{
	u32_t rxRateKbps;
	zmw_get_wlan_dev(dev);
	/* zm_msg1_mm(ZM_LV_0, "addInfo->Tail.Data.RxMacStatus =",
	*  addInfo->Tail.Data.RxMacStatus & 0x03);
	*/

	/* b5~b4: MPDU indication.                */
	/*        00: Single MPDU.                */
	/*        10: First MPDU of A-MPDU.       */
	/*        11: Middle MPDU of A-MPDU.      */
	/*        01: Last MPDU of A-MPDU.        */
	/* Only First MPDU and Single MPDU have PLCP header */
	/* First MPDU  : (mpduInd & 0x30) == 0x00 */
	/* Single MPDU : (mpduInd & 0x30) == 0x20 */
	if ((addInfo->Tail.Data.RxMacStatus & 0x10) == 0) {
		/* Modulation type */
		wd->modulationType = addInfo->Tail.Data.RxMacStatus & 0x03;
		switch (wd->modulationType) {
		/* CCK mode */
		case 0x0:
			wd->rateField = addInfo->PlcpHeader[0] & 0xff;
			wd->rxInfo = 0;
			break;
		/* Legacy-OFDM mode */
		case 0x1:
			wd->rateField = addInfo->PlcpHeader[0] & 0x0f;
			wd->rxInfo = 0;
			break;
		/* HT-OFDM mode */
		case 0x2:
			wd->rateField = addInfo->PlcpHeader[3];
			wd->rxInfo = addInfo->PlcpHeader[6];
			break;
		default:
			break;
		}

		rxRateKbps = zfUpdateRxRate(dev);
		if (wd->CurrentRxRateUpdated == 1) {
			if (rxRateKbps > wd->CurrentRxRateKbps)
				wd->CurrentRxRateKbps = rxRateKbps;
		} else {
			wd->CurrentRxRateKbps = rxRateKbps;
			wd->CurrentRxRateUpdated = 1;
		}
	}
}

#if 0
u16_t zcIndextoRateBG[16] = {1000, 2000, 5500, 11000, 0, 0, 0, 0, 48000,
			24000, 12000, 6000, 54000, 36000, 18000, 9000};
u32_t zcIndextoRateN20L[16] = {6500, 13000, 19500, 26000, 39000, 52000, 58500,
			65000, 13000, 26000, 39000, 52000, 78000, 104000,
			117000, 130000};
u32_t zcIndextoRateN20S[16] = {7200, 14400, 21700, 28900, 43300, 57800, 65000,
			72200, 14400, 28900, 43300, 57800, 86700, 115600,
			130000, 144400};
u32_t zcIndextoRateN40L[16] = {13500, 27000, 40500, 54000, 81000, 108000,
			121500, 135000, 27000, 54000, 81000, 108000,
			162000, 216000, 243000, 270000};
u32_t zcIndextoRateN40S[16] = {15000, 30000, 45000, 60000, 90000, 120000,
			135000, 150000, 30000, 60000, 90000, 120000,
			180000, 240000, 270000, 300000};
#endif

extern u16_t zcIndextoRateBG[16];
extern u32_t zcIndextoRateN20L[16];
extern u32_t zcIndextoRateN20S[16];
extern u32_t zcIndextoRateN40L[16];
extern u32_t zcIndextoRateN40S[16];

u32_t zfiWlanQueryRxRate(zdev_t *dev)
{
	zmw_get_wlan_dev(dev);

	wd->CurrentRxRateUpdated = 0;
	return wd->CurrentRxRateKbps;
}

u32_t zfUpdateRxRate(zdev_t *dev)
{
	u8_t mcs, bandwidth;
	u32_t rxRateKbps = 130000;
	zmw_get_wlan_dev(dev);

	switch (wd->modulationType) {
	/* CCK mode */
	case 0x0:
		switch (wd->rateField) {
		case 0x0a:
			rxRateKbps = 1000;
			break;
		case 0x14:
			rxRateKbps = 2000;

		case 0x37:
			rxRateKbps = 5500;
			break;
		case 0x6e:
			rxRateKbps = 11000;
			break;
		default:
			break;
		}
		break;
	/* Legacy-OFDM mode */
	case 0x1:
		if (wd->rateField <= 15)
			rxRateKbps = zcIndextoRateBG[wd->rateField];
		break;
	/* HT-OFDM mode */
	case 0x2:
		mcs = wd->rateField & 0x7F;
		bandwidth = wd->rateField & 0x80;
		if (mcs <= 15) {
			if (bandwidth != 0) {
				if ((wd->rxInfo & 0x80) != 0) {
					/* Short GI 40 MHz MIMO Rate */
					rxRateKbps = zcIndextoRateN40S[mcs];
				} else {
					/* Long GI 40 MHz MIMO Rate */
					rxRateKbps = zcIndextoRateN40L[mcs];
				}
			} else {
				if ((wd->rxInfo & 0x80) != 0) {
					/* Short GI 20 MHz MIMO Rate */
					rxRateKbps = zcIndextoRateN20S[mcs];
				} else {
					/* Long GI 20 MHz MIMO Rate */
					rxRateKbps = zcIndextoRateN20L[mcs];
				}
			}
		}
		break;
	default:
		break;
	}
	/*	zm_msg1_mm(ZM_LV_0, "wd->CurrentRxRateKbps=",
		wd->CurrentRxRateKbps);
	*/

	/* ToDo: use bandwith field to define 40MB */
	return rxRateKbps;
}

/* Get WLAN stastics */
u16_t zfiWlanGetStatistics(zdev_t *dev)
{
	/* Return link statistics */
	return 0;
}

u16_t zfiWlanReset(zdev_t *dev)
{
	zmw_get_wlan_dev(dev);

	wd->state = ZM_WLAN_STATE_DISABLED;

	return zfWlanReset(dev);
}

/* Reset WLAN */
u16_t zfWlanReset(zdev_t *dev)
{
	u8_t isConnected;
	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();

	zm_debug_msg0("zfWlanReset");

	isConnected = zfStaIsConnected(dev);

	/* if ( wd->wlanMode != ZM_MODE_AP ) */
	{
		if ((wd->wlanMode == ZM_MODE_INFRASTRUCTURE) &&
		(wd->sta.currentAuthMode != ZM_AUTH_MODE_WPA2)) {
			/* send deauthentication frame */
			if (isConnected) {
				/* zfiWlanDeauth(dev, NULL, 0); */
				zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH,
						wd->sta.bssid, 3, 0, 0);
				/* zmw_debug_msg0("send a Deauth frame!"); */
			}
		}
	}

	zfChangeAdapterState(dev, ZM_STA_STATE_DISCONNECT);
	zfHpResetKeyCache(dev);

	if (isConnected) {
		/* zfiWlanDisable(dev); */
		if (wd->zfcbConnectNotify != NULL)
			wd->zfcbConnectNotify(dev,
			ZM_STATUS_MEDIA_CONNECTION_RESET, wd->sta.bssid);
	} else {
		if (wd->zfcbConnectNotify != NULL)
			wd->zfcbConnectNotify(dev, ZM_STATUS_MEDIA_RESET,
								wd->sta.bssid);
	}

	/* stop beacon */
	zfHpDisableBeacon(dev);

	/* Free buffer in defragment list*/
	zfAgingDefragList(dev, 1);

	/* Flush VTxQ and MmQ */
	zfFlushVtxq(dev);

#ifdef ZM_ENABLE_AGGREGATION
	/* add by honda */
	zfAggRxFreeBuf(dev, 0);  /* 1 for release structure memory */
	/* end of add by honda */
#endif

	zfStaRefreshBlockList(dev, 1);

	zmw_enter_critical_section(dev);

	zfTimerCancel(dev, ZM_EVENT_IBSS_MONITOR);
	zfTimerCancel(dev, ZM_EVENT_CM_BLOCK_TIMER);
	zfTimerCancel(dev, ZM_EVENT_CM_DISCONNECT);

	wd->sta.connectState = ZM_STA_CONN_STATE_NONE;
	wd->sta.connectByReasso = FALSE;
	wd->sta.cmDisallowSsidLength = 0;
	wd->sta.bAutoReconnect = 0;
	wd->sta.InternalScanReq = 0;
	wd->sta.encryMode = ZM_NO_WEP;
	wd->sta.wepStatus = ZM_ENCRYPTION_WEP_DISABLED;
	wd->sta.wpaState = ZM_STA_WPA_STATE_INIT;
	wd->sta.cmMicFailureCount = 0;
	wd->sta.ibssBssIsCreator = 0;
#ifdef ZM_ENABLE_IBSS_WPA2PSK
	wd->sta.ibssWpa2Psk = 0;
#endif
	/* reset connect timeout counter */
	wd->sta.connectTimeoutCount = 0;

	/* reset leap enable variable */
	wd->sta.leapEnabled = 0;

	/* Reset the RIFS Status / RIFS-like frame count / RIFS count */
	if (wd->sta.rifsState == ZM_RIFS_STATE_DETECTED)
		zfHpDisableRifs(dev);
	wd->sta.rifsState = ZM_RIFS_STATE_DETECTING;
	wd->sta.rifsLikeFrameCnt = 0;
	wd->sta.rifsCount = 0;

	wd->sta.osRxFilter = 0;
	wd->sta.bSafeMode = 0;

	/* 	Clear the information for the peer
		stations of IBSS or AP of Station mode
	*/
	zfZeroMemory((u8_t *)wd->sta.oppositeInfo,
			sizeof(struct zsOppositeInfo) * ZM_MAX_OPPOSITE_COUNT);

	zmw_leave_critical_section(dev);

	zfScanMgrScanStop(dev, ZM_SCAN_MGR_SCAN_INTERNAL);
	zfScanMgrScanStop(dev, ZM_SCAN_MGR_SCAN_EXTERNAL);

	/* Turn off Software WEP/TKIP */
	if (wd->sta.SWEncryptEnable != 0) {
		zm_debug_msg0("Disable software encryption");
		zfStaDisableSWEncryption(dev);
	}

	/* 	Improve WEP/TKIP performace with HT AP,
		detail information please look bug#32495
	*/
	/* zfHpSetTTSIFSTime(dev, 0x8); */

	/* Keep Pseudo mode */
	if (wd->wlanMode != ZM_MODE_PSEUDO)
		wd->wlanMode = ZM_MODE_INFRASTRUCTURE;

	return 0;
}

/* Deauthenticate a STA */
u16_t zfiWlanDeauth(zdev_t *dev, u16_t *macAddr, u16_t reason)
{
	zmw_get_wlan_dev(dev);

	if (wd->wlanMode == ZM_MODE_AP) {
		/* u16_t id; */

		/*
		* we will reset all key in zfHpResetKeyCache() when call
		* zfiWlanDisable(), if we want to reset PairwiseKey for each
		* sta, need to use a nullAddr to let keyindex not match.
		* otherwise hardware will still find PairwiseKey when AP change
		* encryption mode from WPA to WEP
		*/

		/*
		if ((id = zfApFindSta(dev, macAddr)) != 0xffff)
		{
			u32_t key[8];
			u16_t nullAddr[3] = { 0x0, 0x0, 0x0 };

			if (wd->ap.staTable[i].encryMode != ZM_NO_WEP)
			{
				zfHpSetApPairwiseKey(dev, nullAddr,
				ZM_NO_WEP, &key[0], &key[4], i+1);
			}
			//zfHpSetApPairwiseKey(dev, (u16_t *)macAddr,
			//        ZM_NO_WEP, &key[0], &key[4], id+1);
		wd->ap.staTable[id].encryMode = ZM_NO_WEP;
		wd->ap.staTable[id].keyIdx = 0xff;
		}
		*/

		zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH, macAddr,
								reason, 0, 0);
	} else
		zfSendMmFrame(dev, ZM_WLAN_FRAME_TYPE_DEAUTH,
						wd->sta.bssid, 3, 0, 0);

	/* Issue DEAUTH command to FW */
	return 0;
}


/* XP packet filter feature : */
/* 1=>enable: All multicast address packets, not just the ones */
/* enumerated in the multicast address list. */
/* 0=>disable */
void zfiWlanSetAllMulticast(zdev_t *dev, u32_t setting)
{
	zmw_get_wlan_dev(dev);
	zm_msg1_mm(ZM_LV_0, "sta.bAllMulticast = ", setting);
	wd->sta.bAllMulticast = (u8_t)setting;
}


/* HT configure API */
void zfiWlanSetHTCtrl(zdev_t *dev, u32_t *setting, u32_t forceTxTPC)
{
	zmw_get_wlan_dev(dev);

	wd->preambleType        = (u8_t)setting[0];
	wd->sta.preambleTypeHT  = (u8_t)setting[1];
	wd->sta.htCtrlBandwidth = (u8_t)setting[2];
	wd->sta.htCtrlSTBC      = (u8_t)setting[3];
	wd->sta.htCtrlSG        = (u8_t)setting[4];
	wd->sta.defaultTA       = (u8_t)setting[5];
	wd->enableAggregation   = (u8_t)setting[6];
	wd->enableWDS           = (u8_t)setting[7];

	wd->forceTxTPC          = forceTxTPC;
}

/* FB50 in OS XP, RD private test code */
void zfiWlanQueryHTCtrl(zdev_t *dev, u32_t *setting, u32_t *forceTxTPC)
{
	zmw_get_wlan_dev(dev);

	setting[0] = wd->preambleType;
	setting[1] = wd->sta.preambleTypeHT;
	setting[2] = wd->sta.htCtrlBandwidth;
	setting[3] = wd->sta.htCtrlSTBC;
	setting[4] = wd->sta.htCtrlSG;
	setting[5] = wd->sta.defaultTA;
	setting[6] = wd->enableAggregation;
	setting[7] = wd->enableWDS;

	*forceTxTPC = wd->forceTxTPC;
}

void zfiWlanDbg(zdev_t *dev, u8_t setting)
{
	zmw_get_wlan_dev(dev);

	wd->enableHALDbgInfo = setting;
}

/* FB50 in OS XP, RD private test code */
void zfiWlanSetRxPacketDump(zdev_t *dev, u32_t setting)
{
	zmw_get_wlan_dev(dev);
	if (setting)
		wd->rxPacketDump = 1;   /* enable */
	else
		wd->rxPacketDump = 0;   /* disable */
}


/* FB50 in OS XP, RD private test code */
/* Tally */
void zfiWlanResetTally(zdev_t *dev)
{
	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();

	zmw_enter_critical_section(dev);

	wd->commTally.txUnicastFrm = 0;		/* txUnicastFrames */
	wd->commTally.txMulticastFrm = 0;	/* txMulticastFrames */
	wd->commTally.txUnicastOctets = 0;	/* txUniOctets  byte size */
	wd->commTally.txMulticastOctets = 0;	/* txMultiOctets  byte size */
	wd->commTally.txFrmUpperNDIS = 0;
	wd->commTally.txFrmDrvMgt = 0;
	wd->commTally.RetryFailCnt = 0;
	wd->commTally.Hw_TotalTxFrm = 0;	/* Hardware total Tx Frame */
	wd->commTally.Hw_RetryCnt = 0;		/* txMultipleRetriesFrames */
	wd->commTally.Hw_UnderrunCnt = 0;
	wd->commTally.DriverRxFrmCnt = 0;
	wd->commTally.rxUnicastFrm = 0;		/* rxUnicastFrames */
	wd->commTally.rxMulticastFrm = 0;	/* rxMulticastFrames */
	wd->commTally.NotifyNDISRxFrmCnt = 0;
	wd->commTally.rxUnicastOctets = 0;	/* rxUniOctets  byte size */
	wd->commTally.rxMulticastOctets = 0;	/* rxMultiOctets  byte size */
	wd->commTally.DriverDiscardedFrm = 0;	/* Discard by ValidateFrame */
	wd->commTally.LessThanDataMinLen = 0;
	wd->commTally.GreaterThanMaxLen = 0;
	wd->commTally.DriverDiscardedFrmCauseByMulticastList = 0;
	wd->commTally.DriverDiscardedFrmCauseByFrmCtrl = 0;
	wd->commTally.rxNeedFrgFrm = 0;		/* need more frg frm */
	wd->commTally.DriverRxMgtFrmCnt = 0;
	wd->commTally.rxBroadcastFrm = 0;/* Receive broadcast frame count */
	wd->commTally.rxBroadcastOctets = 0;/*Receive broadcast framebyte size*/
	wd->commTally.Hw_TotalRxFrm = 0;
	wd->commTally.Hw_CRC16Cnt = 0;		/* rxPLCPCRCErrCnt */
	wd->commTally.Hw_CRC32Cnt = 0;		/* rxCRC32ErrCnt */
	wd->commTally.Hw_DecrypErr_UNI = 0;
	wd->commTally.Hw_DecrypErr_Mul = 0;
	wd->commTally.Hw_RxFIFOOverrun = 0;
	wd->commTally.Hw_RxTimeOut = 0;
	wd->commTally.LossAP = 0;

	wd->commTally.Tx_MPDU = 0;
	wd->commTally.BA_Fail = 0;
	wd->commTally.Hw_Tx_AMPDU = 0;
	wd->commTally.Hw_Tx_MPDU = 0;

	wd->commTally.txQosDropCount[0] = 0;
	wd->commTally.txQosDropCount[1] = 0;
	wd->commTally.txQosDropCount[2] = 0;
	wd->commTally.txQosDropCount[3] = 0;
	wd->commTally.txQosDropCount[4] = 0;

	wd->commTally.Hw_RxMPDU = 0;
	wd->commTally.Hw_RxDropMPDU = 0;
	wd->commTally.Hw_RxDelMPDU = 0;

	wd->commTally.Hw_RxPhyMiscError = 0;
	wd->commTally.Hw_RxPhyXRError = 0;
	wd->commTally.Hw_RxPhyOFDMError = 0;
	wd->commTally.Hw_RxPhyCCKError = 0;
	wd->commTally.Hw_RxPhyHTError = 0;
	wd->commTally.Hw_RxPhyTotalCount = 0;

#if (defined(GCCK) && defined(OFDM))
	wd->commTally.rx11bDataFrame = 0;
	wd->commTally.rxOFDMDataFrame = 0;
#endif

	zmw_leave_critical_section(dev);
}

/* FB50 in OS XP, RD private test code */
void zfiWlanQueryTally(zdev_t *dev, struct zsCommTally *tally)
{
	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();

	zmw_enter_critical_section(dev);
	zfMemoryCopy((u8_t *)tally, (u8_t *)&wd->commTally,
						sizeof(struct zsCommTally));
	zmw_leave_critical_section(dev);
}

void zfiWlanQueryTrafTally(zdev_t *dev, struct zsTrafTally *tally)
{
	zmw_get_wlan_dev(dev);

	zmw_declare_for_critical_section();

	zmw_enter_critical_section(dev);
	zfMemoryCopy((u8_t *)tally, (u8_t *)&wd->trafTally,
						sizeof(struct zsTrafTally));
	zmw_leave_critical_section(dev);
}

void zfiWlanQueryMonHalRxInfo(zdev_t *dev, struct zsMonHalRxInfo *monHalRxInfo)
{
	zfHpQueryMonHalRxInfo(dev, (u8_t *)monHalRxInfo);
}

/* parse the modeMDKEnable to DrvCore */
void zfiDKEnable(zdev_t *dev, u32_t enable)
{
	zmw_get_wlan_dev(dev);

	wd->modeMDKEnable = enable;
	zm_debug_msg1("modeMDKEnable = ", wd->modeMDKEnable);
}

/* airoPeek */
u32_t zfiWlanQueryPacketTypePromiscuous(zdev_t *dev)
{
	zmw_get_wlan_dev(dev);

	return wd->swSniffer;
}

/* airoPeek */
void zfiWlanSetPacketTypePromiscuous(zdev_t *dev, u32_t setValue)
{
	zmw_get_wlan_dev(dev);

	wd->swSniffer = setValue;
	zm_msg1_mm(ZM_LV_0, "wd->swSniffer ", wd->swSniffer);
	if (setValue) {
		/* write register for sniffer mode */
		zfHpSetSnifferMode(dev, 1);
		zm_msg0_mm(ZM_LV_1, "enalbe sniffer mode");
	} else {
		zfHpSetSnifferMode(dev, 0);
		zm_msg0_mm(ZM_LV_0, "disalbe sniffer mode");
	}
}

void zfiWlanSetXLinkMode(zdev_t *dev, u32_t setValue)
{
	zmw_get_wlan_dev(dev);

	wd->XLinkMode = setValue;
	if (setValue) {
		/* write register for sniffer mode */
		zfHpSetSnifferMode(dev, 1);
	} else
		zfHpSetSnifferMode(dev, 0);
}

extern void zfStaChannelManagement(zdev_t *dev, u8_t scan);

void zfiSetChannelManagement(zdev_t *dev, u32_t setting)
{
	zmw_get_wlan_dev(dev);

	switch (setting) {
	case 1:
		wd->sta.EnableHT = 1;
		wd->BandWidth40 = 1;
		wd->ExtOffset   = 1;
		break;
	case 3:
		wd->sta.EnableHT = 1;
		wd->BandWidth40 = 1;
		wd->ExtOffset   = 3;
		break;
	case 0:
		wd->sta.EnableHT = 1;
		wd->BandWidth40 = 0;
		wd->ExtOffset   = 0;
		break;
	default:
		wd->BandWidth40 = 0;
		wd->ExtOffset   = 0;
		break;
	}

	zfCoreSetFrequencyEx(dev, wd->frequency, wd->BandWidth40,
							wd->ExtOffset, NULL);
}

void zfiSetRifs(zdev_t *dev, u16_t setting)
{
	zmw_get_wlan_dev(dev);

	wd->sta.ie.HtInfo.ChannelInfo |= ExtHtCap_RIFSMode;
	wd->sta.EnableHT = 1;

	switch (setting) {
	case 0:
		wd->sta.HT2040 = 0;
		/* zfHpSetRifs(dev, 1, 0,
		*  (wd->sta.currentFrequency < 3000)? 1:0);
		*/
		break;
	case 1:
		wd->sta.HT2040 = 1;
		/* zfHpSetRifs(dev, 1, 1,
		*  (wd->sta.currentFrequency < 3000)? 1:0);
		*/
		break;
	default:
		wd->sta.HT2040 = 0;
		/* zfHpSetRifs(dev, 1, 0,
		*  (wd->sta.currentFrequency < 3000)? 1:0);
		*/
		break;
	}
}

void zfiCheckRifs(zdev_t *dev)
{
	zmw_get_wlan_dev(dev);

	if (wd->sta.ie.HtInfo.ChannelInfo & ExtHtCap_RIFSMode)
		;
		/* zfHpSetRifs(dev, wd->sta.EnableHT, wd->sta.HT2040,
		*  (wd->sta.currentFrequency < 3000)? 1:0);
		*/
}

void zfiSetReorder(zdev_t *dev, u16_t value)
{
	zmw_get_wlan_dev(dev);

	wd->reorder = value;
}

void zfiSetSeqDebug(zdev_t *dev, u16_t value)
{
	zmw_get_wlan_dev(dev);

	wd->seq_debug = value;
}
