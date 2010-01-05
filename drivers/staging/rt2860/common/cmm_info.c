/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************
 */

#include <linux/sched.h>
#include "../rt_config.h"

/*
	========================================================================

	Routine Description:
		Remove WPA Key process

	Arguments:
		pAd 					Pointer to our adapter
		pBuf							Pointer to the where the key stored

	Return Value:
		NDIS_SUCCESS					Add key successfully

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void RTMPSetDesiredRates(struct rt_rtmp_adapter *pAdapter, long Rates)
{
	NDIS_802_11_RATES aryRates;

	memset(&aryRates, 0x00, sizeof(NDIS_802_11_RATES));
	switch (pAdapter->CommonCfg.PhyMode) {
	case PHY_11A:		/* A only */
		switch (Rates) {
		case 6000000:	/*6M */
			aryRates[0] = 0x0c;	/* 6M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_0;
			break;
		case 9000000:	/*9M */
			aryRates[0] = 0x12;	/* 9M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_1;
			break;
		case 12000000:	/*12M */
			aryRates[0] = 0x18;	/* 12M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_2;
			break;
		case 18000000:	/*18M */
			aryRates[0] = 0x24;	/* 18M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_3;
			break;
		case 24000000:	/*24M */
			aryRates[0] = 0x30;	/* 24M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_4;
			break;
		case 36000000:	/*36M */
			aryRates[0] = 0x48;	/* 36M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_5;
			break;
		case 48000000:	/*48M */
			aryRates[0] = 0x60;	/* 48M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_6;
			break;
		case 54000000:	/*54M */
			aryRates[0] = 0x6c;	/* 54M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_7;
			break;
		case -1:	/*Auto */
		default:
			aryRates[0] = 0x6c;	/* 54Mbps */
			aryRates[1] = 0x60;	/* 48Mbps */
			aryRates[2] = 0x48;	/* 36Mbps */
			aryRates[3] = 0x30;	/* 24Mbps */
			aryRates[4] = 0x24;	/* 18M */
			aryRates[5] = 0x18;	/* 12M */
			aryRates[6] = 0x12;	/* 9M */
			aryRates[7] = 0x0c;	/* 6M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_AUTO;
			break;
		}
		break;
	case PHY_11BG_MIXED:	/* B/G Mixed */
	case PHY_11B:		/* B only */
	case PHY_11ABG_MIXED:	/* A/B/G Mixed */
	default:
		switch (Rates) {
		case 1000000:	/*1M */
			aryRates[0] = 0x02;
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_0;
			break;
		case 2000000:	/*2M */
			aryRates[0] = 0x04;
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_1;
			break;
		case 5000000:	/*5.5M */
			aryRates[0] = 0x0b;	/* 5.5M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_2;
			break;
		case 11000000:	/*11M */
			aryRates[0] = 0x16;	/* 11M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_3;
			break;
		case 6000000:	/*6M */
			aryRates[0] = 0x0c;	/* 6M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_0;
			break;
		case 9000000:	/*9M */
			aryRates[0] = 0x12;	/* 9M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_1;
			break;
		case 12000000:	/*12M */
			aryRates[0] = 0x18;	/* 12M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_2;
			break;
		case 18000000:	/*18M */
			aryRates[0] = 0x24;	/* 18M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_3;
			break;
		case 24000000:	/*24M */
			aryRates[0] = 0x30;	/* 24M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_4;
			break;
		case 36000000:	/*36M */
			aryRates[0] = 0x48;	/* 36M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_5;
			break;
		case 48000000:	/*48M */
			aryRates[0] = 0x60;	/* 48M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_6;
			break;
		case 54000000:	/*54M */
			aryRates[0] = 0x6c;	/* 54M */
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_7;
			break;
		case -1:	/*Auto */
		default:
			if (pAdapter->CommonCfg.PhyMode == PHY_11B) {	/*B Only */
				aryRates[0] = 0x16;	/* 11Mbps */
				aryRates[1] = 0x0b;	/* 5.5Mbps */
				aryRates[2] = 0x04;	/* 2Mbps */
				aryRates[3] = 0x02;	/* 1Mbps */
			} else {	/*(B/G) Mixed or (A/B/G) Mixed */
				aryRates[0] = 0x6c;	/* 54Mbps */
				aryRates[1] = 0x60;	/* 48Mbps */
				aryRates[2] = 0x48;	/* 36Mbps */
				aryRates[3] = 0x30;	/* 24Mbps */
				aryRates[4] = 0x16;	/* 11Mbps */
				aryRates[5] = 0x0b;	/* 5.5Mbps */
				aryRates[6] = 0x04;	/* 2Mbps */
				aryRates[7] = 0x02;	/* 1Mbps */
			}
			pAdapter->StaCfg.DesiredTransmitSetting.field.MCS =
			    MCS_AUTO;
			break;
		}
		break;
	}

	NdisZeroMemory(pAdapter->CommonCfg.DesireRate,
		       MAX_LEN_OF_SUPPORTED_RATES);
	NdisMoveMemory(pAdapter->CommonCfg.DesireRate, &aryRates,
		       sizeof(NDIS_802_11_RATES));
	DBGPRINT(RT_DEBUG_TRACE,
		 (" RTMPSetDesiredRates (%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x)\n",
		  pAdapter->CommonCfg.DesireRate[0],
		  pAdapter->CommonCfg.DesireRate[1],
		  pAdapter->CommonCfg.DesireRate[2],
		  pAdapter->CommonCfg.DesireRate[3],
		  pAdapter->CommonCfg.DesireRate[4],
		  pAdapter->CommonCfg.DesireRate[5],
		  pAdapter->CommonCfg.DesireRate[6],
		  pAdapter->CommonCfg.DesireRate[7]));
	/* Changing DesiredRate may affect the MAX TX rate we used to TX frames out */
	MlmeUpdateTxRates(pAdapter, FALSE, 0);
}

/*
	========================================================================

	Routine Description:
		Remove All WPA Keys

	Arguments:
		pAd 					Pointer to our adapter

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void RTMPWPARemoveAllKeys(struct rt_rtmp_adapter *pAd)
{

	u8 i;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPWPARemoveAllKeys(AuthMode=%d, WepStatus=%d)\n",
		  pAd->StaCfg.AuthMode, pAd->StaCfg.WepStatus));
	RTMP_CLEAR_PSFLAG(pAd, fRTMP_PS_CAN_GO_SLEEP);
	/* For WEP/CKIP, there is no need to remove it, since WinXP won't set it again after */
	/* Link up. And it will be replaced if user changed it. */
	if (pAd->StaCfg.AuthMode < Ndis802_11AuthModeWPA)
		return;

	/* For WPA-None, there is no need to remove it, since WinXP won't set it again after */
	/* Link up. And it will be replaced if user changed it. */
	if (pAd->StaCfg.AuthMode == Ndis802_11AuthModeWPANone)
		return;

	/* set BSSID wcid entry of the Pair-wise Key table as no-security mode */
	AsicRemovePairwiseKeyEntry(pAd, BSS0, BSSID_WCID);

	/* set all shared key mode as no-security. */
	for (i = 0; i < SHARE_KEY_NUM; i++) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("remove %s key #%d\n",
			  CipherName[pAd->SharedKey[BSS0][i].CipherAlg], i));
		NdisZeroMemory(&pAd->SharedKey[BSS0][i], sizeof(struct rt_cipher_key));

		AsicRemoveSharedKeyEntry(pAd, BSS0, i);
	}
	RTMP_SET_PSFLAG(pAd, fRTMP_PS_CAN_GO_SLEEP);
}

/*
	========================================================================

	Routine Description:
		As STA's BSSID is a WC too, it uses shared key table.
		This function write correct unicast TX key to ASIC WCID.
		And we still make a copy in our MacTab.Content[BSSID_WCID].PairwiseKey.
		Caller guarantee TKIP/AES always has keyidx = 0. (pairwise key)
		Caller guarantee WEP calls this function when set Txkey,  default key index=0~3.

	Arguments:
		pAd					Pointer to our adapter
		pKey							Pointer to the where the key stored

	Return Value:
		NDIS_SUCCESS					Add key successfully

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
/*
	========================================================================
	Routine Description:
		Change NIC PHY mode. Re-association may be necessary. possible settings
		include - PHY_11B, PHY_11BG_MIXED, PHY_11A, and PHY_11ABG_MIXED

	Arguments:
		pAd - Pointer to our adapter
		phymode  -

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	========================================================================
*/
void RTMPSetPhyMode(struct rt_rtmp_adapter *pAd, unsigned long phymode)
{
	int i;
	/* the selected phymode must be supported by the RF IC encoded in E2PROM */

	/* if no change, do nothing */
	/* bug fix
	   if (pAd->CommonCfg.PhyMode == phymode)
	   return;
	 */
	pAd->CommonCfg.PhyMode = (u8)phymode;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPSetPhyMode : PhyMode=%d, channel=%d \n",
		  pAd->CommonCfg.PhyMode, pAd->CommonCfg.Channel));

	BuildChannelList(pAd);

	/* sanity check user setting */
	for (i = 0; i < pAd->ChannelListNum; i++) {
		if (pAd->CommonCfg.Channel == pAd->ChannelList[i].Channel)
			break;
	}

	if (i == pAd->ChannelListNum) {
		pAd->CommonCfg.Channel = FirstChannel(pAd);
		DBGPRINT(RT_DEBUG_ERROR,
			 ("RTMPSetPhyMode: channel is out of range, use first channel=%d \n",
			  pAd->CommonCfg.Channel));
	}

	NdisZeroMemory(pAd->CommonCfg.SupRate, MAX_LEN_OF_SUPPORTED_RATES);
	NdisZeroMemory(pAd->CommonCfg.ExtRate, MAX_LEN_OF_SUPPORTED_RATES);
	NdisZeroMemory(pAd->CommonCfg.DesireRate, MAX_LEN_OF_SUPPORTED_RATES);
	switch (phymode) {
	case PHY_11B:
		pAd->CommonCfg.SupRate[0] = 0x82;	/* 1 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[1] = 0x84;	/* 2 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[2] = 0x8B;	/* 5.5 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[3] = 0x96;	/* 11 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRateLen = 4;
		pAd->CommonCfg.ExtRateLen = 0;
		pAd->CommonCfg.DesireRate[0] = 2;	/* 1 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[1] = 4;	/* 2 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[2] = 11;	/* 5.5 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[3] = 22;	/* 11 mbps, in units of 0.5 Mbps */
		/*pAd->CommonCfg.HTPhyMode.field.MODE = MODE_CCK; // This MODE is only FYI. not use */
		break;

	case PHY_11G:
	case PHY_11BG_MIXED:
	case PHY_11ABG_MIXED:
	case PHY_11N_2_4G:
	case PHY_11ABGN_MIXED:
	case PHY_11BGN_MIXED:
	case PHY_11GN_MIXED:
		pAd->CommonCfg.SupRate[0] = 0x82;	/* 1 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[1] = 0x84;	/* 2 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[2] = 0x8B;	/* 5.5 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[3] = 0x96;	/* 11 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[4] = 0x12;	/* 9 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.SupRate[5] = 0x24;	/* 18 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.SupRate[6] = 0x48;	/* 36 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.SupRate[7] = 0x6c;	/* 54 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.SupRateLen = 8;
		pAd->CommonCfg.ExtRate[0] = 0x0C;	/* 6 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.ExtRate[1] = 0x18;	/* 12 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.ExtRate[2] = 0x30;	/* 24 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.ExtRate[3] = 0x60;	/* 48 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.ExtRateLen = 4;
		pAd->CommonCfg.DesireRate[0] = 2;	/* 1 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[1] = 4;	/* 2 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[2] = 11;	/* 5.5 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[3] = 22;	/* 11 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[4] = 12;	/* 6 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[5] = 18;	/* 9 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[6] = 24;	/* 12 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[7] = 36;	/* 18 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[8] = 48;	/* 24 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[9] = 72;	/* 36 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[10] = 96;	/* 48 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[11] = 108;	/* 54 mbps, in units of 0.5 Mbps */
		break;

	case PHY_11A:
	case PHY_11AN_MIXED:
	case PHY_11AGN_MIXED:
	case PHY_11N_5G:
		pAd->CommonCfg.SupRate[0] = 0x8C;	/* 6 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[1] = 0x12;	/* 9 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.SupRate[2] = 0x98;	/* 12 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[3] = 0x24;	/* 18 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.SupRate[4] = 0xb0;	/* 24 mbps, in units of 0.5 Mbps, basic rate */
		pAd->CommonCfg.SupRate[5] = 0x48;	/* 36 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.SupRate[6] = 0x60;	/* 48 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.SupRate[7] = 0x6c;	/* 54 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.SupRateLen = 8;
		pAd->CommonCfg.ExtRateLen = 0;
		pAd->CommonCfg.DesireRate[0] = 12;	/* 6 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[1] = 18;	/* 9 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[2] = 24;	/* 12 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[3] = 36;	/* 18 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[4] = 48;	/* 24 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[5] = 72;	/* 36 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[6] = 96;	/* 48 mbps, in units of 0.5 Mbps */
		pAd->CommonCfg.DesireRate[7] = 108;	/* 54 mbps, in units of 0.5 Mbps */
		/*pAd->CommonCfg.HTPhyMode.field.MODE = MODE_OFDM; // This MODE is only FYI. not use */
		break;

	default:
		break;
	}

	pAd->CommonCfg.BandState = UNKNOWN_BAND;
}

/*
	========================================================================
	Routine Description:
		Caller ensures we has 802.11n support.
		Calls at setting HT from AP/STASetinformation

	Arguments:
		pAd - Pointer to our adapter
		phymode  -

	========================================================================
*/
void RTMPSetHT(struct rt_rtmp_adapter *pAd, struct rt_oid_set_ht_phymode *pHTPhyMode)
{
	/*unsigned long *pmcs; */
	u32 Value = 0;
	u8 BBPValue = 0;
	u8 BBP3Value = 0;
	u8 RxStream = pAd->CommonCfg.RxStream;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPSetHT : HT_mode(%d), ExtOffset(%d), MCS(%d), BW(%d), STBC(%d), SHORTGI(%d)\n",
		  pHTPhyMode->HtMode, pHTPhyMode->ExtOffset, pHTPhyMode->MCS,
		  pHTPhyMode->BW, pHTPhyMode->STBC, pHTPhyMode->SHORTGI));

	/* Don't zero supportedHyPhy structure. */
	RTMPZeroMemory(&pAd->CommonCfg.HtCapability,
		       sizeof(pAd->CommonCfg.HtCapability));
	RTMPZeroMemory(&pAd->CommonCfg.AddHTInfo,
		       sizeof(pAd->CommonCfg.AddHTInfo));
	RTMPZeroMemory(&pAd->CommonCfg.NewExtChanOffset,
		       sizeof(pAd->CommonCfg.NewExtChanOffset));
	RTMPZeroMemory(&pAd->CommonCfg.DesiredHtPhy,
		       sizeof(pAd->CommonCfg.DesiredHtPhy));

	if (pAd->CommonCfg.bRdg) {
		pAd->CommonCfg.HtCapability.ExtHtCapInfo.PlusHTC = 1;
		pAd->CommonCfg.HtCapability.ExtHtCapInfo.RDGSupport = 1;
	} else {
		pAd->CommonCfg.HtCapability.ExtHtCapInfo.PlusHTC = 0;
		pAd->CommonCfg.HtCapability.ExtHtCapInfo.RDGSupport = 0;
	}

	pAd->CommonCfg.HtCapability.HtCapParm.MaxRAmpduFactor = 3;
	pAd->CommonCfg.DesiredHtPhy.MaxRAmpduFactor = 3;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPSetHT : RxBAWinLimit = %d\n",
		  pAd->CommonCfg.BACapability.field.RxBAWinLimit));

	/* Mimo power save, A-MSDU size, */
	pAd->CommonCfg.DesiredHtPhy.AmsduEnable =
	    (u16)pAd->CommonCfg.BACapability.field.AmsduEnable;
	pAd->CommonCfg.DesiredHtPhy.AmsduSize =
	    (u8)pAd->CommonCfg.BACapability.field.AmsduSize;
	pAd->CommonCfg.DesiredHtPhy.MimoPs =
	    (u8)pAd->CommonCfg.BACapability.field.MMPSmode;
	pAd->CommonCfg.DesiredHtPhy.MpduDensity =
	    (u8)pAd->CommonCfg.BACapability.field.MpduDensity;

	pAd->CommonCfg.HtCapability.HtCapInfo.AMsduSize =
	    (u16)pAd->CommonCfg.BACapability.field.AmsduSize;
	pAd->CommonCfg.HtCapability.HtCapInfo.MimoPs =
	    (u16)pAd->CommonCfg.BACapability.field.MMPSmode;
	pAd->CommonCfg.HtCapability.HtCapParm.MpduDensity =
	    (u8)pAd->CommonCfg.BACapability.field.MpduDensity;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPSetHT : AMsduSize = %d, MimoPs = %d, MpduDensity = %d, MaxRAmpduFactor = %d\n",
		  pAd->CommonCfg.DesiredHtPhy.AmsduSize,
		  pAd->CommonCfg.DesiredHtPhy.MimoPs,
		  pAd->CommonCfg.DesiredHtPhy.MpduDensity,
		  pAd->CommonCfg.DesiredHtPhy.MaxRAmpduFactor));

	if (pHTPhyMode->HtMode == HTMODE_GF) {
		pAd->CommonCfg.HtCapability.HtCapInfo.GF = 1;
		pAd->CommonCfg.DesiredHtPhy.GF = 1;
	} else
		pAd->CommonCfg.DesiredHtPhy.GF = 0;

	/* Decide Rx MCSSet */
	switch (RxStream) {
	case 1:
		pAd->CommonCfg.HtCapability.MCSSet[0] = 0xff;
		pAd->CommonCfg.HtCapability.MCSSet[1] = 0x00;
		break;

	case 2:
		pAd->CommonCfg.HtCapability.MCSSet[0] = 0xff;
		pAd->CommonCfg.HtCapability.MCSSet[1] = 0xff;
		break;

	case 3:		/* 3*3 */
		pAd->CommonCfg.HtCapability.MCSSet[0] = 0xff;
		pAd->CommonCfg.HtCapability.MCSSet[1] = 0xff;
		pAd->CommonCfg.HtCapability.MCSSet[2] = 0xff;
		break;
	}

	if (pAd->CommonCfg.bForty_Mhz_Intolerant
	    && (pAd->CommonCfg.Channel <= 14) && (pHTPhyMode->BW == BW_40)) {
		pHTPhyMode->BW = BW_20;
		pAd->CommonCfg.HtCapability.HtCapInfo.Forty_Mhz_Intolerant = 1;
	}

	if (pHTPhyMode->BW == BW_40) {
		pAd->CommonCfg.HtCapability.MCSSet[4] = 0x1;	/* MCS 32 */
		pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth = 1;
		if (pAd->CommonCfg.Channel <= 14)
			pAd->CommonCfg.HtCapability.HtCapInfo.CCKmodein40 = 1;

		pAd->CommonCfg.DesiredHtPhy.ChannelWidth = 1;
		pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth = 1;
		pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset =
		    (pHTPhyMode->ExtOffset ==
		     EXTCHA_BELOW) ? (EXTCHA_BELOW) : EXTCHA_ABOVE;
		/* Set Regsiter for extension channel position. */
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BBP3Value);
		if ((pHTPhyMode->ExtOffset == EXTCHA_BELOW)) {
			Value |= 0x1;
			BBP3Value |= (0x20);
			RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);
		} else if ((pHTPhyMode->ExtOffset == EXTCHA_ABOVE)) {
			Value &= 0xfe;
			BBP3Value &= (~0x20);
			RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);
		}
		/* Turn on BBP 40MHz mode now only as AP . */
		/* Sta can turn on BBP 40MHz after connection with 40MHz AP. Sta only broadcast 40MHz capability before connection. */
		if ((pAd->OpMode == OPMODE_AP) || INFRA_ON(pAd) || ADHOC_ON(pAd)
		    ) {
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
			BBPValue &= (~0x18);
			BBPValue |= 0x10;
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);

			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BBP3Value);
			pAd->CommonCfg.BBPCurrentBW = BW_40;
		}
	} else {
		pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth = 0;
		pAd->CommonCfg.DesiredHtPhy.ChannelWidth = 0;
		pAd->CommonCfg.AddHTInfo.AddHtInfo.RecomWidth = 0;
		pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset = EXTCHA_NONE;
		pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel;
		/* Turn on BBP 20MHz mode by request here. */
		{
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BBPValue);
			BBPValue &= (~0x18);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BBPValue);
			pAd->CommonCfg.BBPCurrentBW = BW_20;
		}
	}

	if (pHTPhyMode->STBC == STBC_USE) {
		pAd->CommonCfg.HtCapability.HtCapInfo.TxSTBC = 1;
		pAd->CommonCfg.DesiredHtPhy.TxSTBC = 1;
		pAd->CommonCfg.HtCapability.HtCapInfo.RxSTBC = 1;
		pAd->CommonCfg.DesiredHtPhy.RxSTBC = 1;
	} else {
		pAd->CommonCfg.DesiredHtPhy.TxSTBC = 0;
		pAd->CommonCfg.DesiredHtPhy.RxSTBC = 0;
	}

	if (pHTPhyMode->SHORTGI == GI_400) {
		pAd->CommonCfg.HtCapability.HtCapInfo.ShortGIfor20 = 1;
		pAd->CommonCfg.HtCapability.HtCapInfo.ShortGIfor40 = 1;
		pAd->CommonCfg.DesiredHtPhy.ShortGIfor20 = 1;
		pAd->CommonCfg.DesiredHtPhy.ShortGIfor40 = 1;
	} else {
		pAd->CommonCfg.HtCapability.HtCapInfo.ShortGIfor20 = 0;
		pAd->CommonCfg.HtCapability.HtCapInfo.ShortGIfor40 = 0;
		pAd->CommonCfg.DesiredHtPhy.ShortGIfor20 = 0;
		pAd->CommonCfg.DesiredHtPhy.ShortGIfor40 = 0;
	}

	/* We support link adaptation for unsolicit MCS feedback, set to 2. */
	pAd->CommonCfg.HtCapability.ExtHtCapInfo.MCSFeedback = MCSFBK_NONE;	/*MCSFBK_UNSOLICIT; */
	pAd->CommonCfg.AddHTInfo.ControlChan = pAd->CommonCfg.Channel;
	/* 1, the extension channel above the control channel. */

	/* EDCA parameters used for AP's own transmission */
	if (pAd->CommonCfg.APEdcaParm.bValid == FALSE) {
		pAd->CommonCfg.APEdcaParm.bValid = TRUE;
		pAd->CommonCfg.APEdcaParm.Aifsn[0] = 3;
		pAd->CommonCfg.APEdcaParm.Aifsn[1] = 7;
		pAd->CommonCfg.APEdcaParm.Aifsn[2] = 1;
		pAd->CommonCfg.APEdcaParm.Aifsn[3] = 1;

		pAd->CommonCfg.APEdcaParm.Cwmin[0] = 4;
		pAd->CommonCfg.APEdcaParm.Cwmin[1] = 4;
		pAd->CommonCfg.APEdcaParm.Cwmin[2] = 3;
		pAd->CommonCfg.APEdcaParm.Cwmin[3] = 2;

		pAd->CommonCfg.APEdcaParm.Cwmax[0] = 6;
		pAd->CommonCfg.APEdcaParm.Cwmax[1] = 10;
		pAd->CommonCfg.APEdcaParm.Cwmax[2] = 4;
		pAd->CommonCfg.APEdcaParm.Cwmax[3] = 3;

		pAd->CommonCfg.APEdcaParm.Txop[0] = 0;
		pAd->CommonCfg.APEdcaParm.Txop[1] = 0;
		pAd->CommonCfg.APEdcaParm.Txop[2] = 94;
		pAd->CommonCfg.APEdcaParm.Txop[3] = 47;
	}
	AsicSetEdcaParm(pAd, &pAd->CommonCfg.APEdcaParm);

	{
		RTMPSetIndividualHT(pAd, 0);
	}

}

/*
	========================================================================
	Routine Description:
		Caller ensures we has 802.11n support.
		Calls at setting HT from AP/STASetinformation

	Arguments:
		pAd - Pointer to our adapter
		phymode  -

	========================================================================
*/
void RTMPSetIndividualHT(struct rt_rtmp_adapter *pAd, u8 apidx)
{
	struct rt_ht_phy_info *pDesired_ht_phy = NULL;
	u8 TxStream = pAd->CommonCfg.TxStream;
	u8 DesiredMcs = MCS_AUTO;

	do {
		{
			pDesired_ht_phy = &pAd->StaCfg.DesiredHtPhyInfo;
			DesiredMcs =
			    pAd->StaCfg.DesiredTransmitSetting.field.MCS;
			/*pAd->StaCfg.bAutoTxRateSwitch = (DesiredMcs == MCS_AUTO) ? TRUE : FALSE; */
			break;
		}
	} while (FALSE);

	if (pDesired_ht_phy == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("RTMPSetIndividualHT: invalid apidx(%d)\n", apidx));
		return;
	}
	RTMPZeroMemory(pDesired_ht_phy, sizeof(struct rt_ht_phy_info));

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPSetIndividualHT : Desired MCS = %d\n", DesiredMcs));
	/* Check the validity of MCS */
	if ((TxStream == 1)
	    && ((DesiredMcs >= MCS_8) && (DesiredMcs <= MCS_15))) {
		DBGPRINT(RT_DEBUG_WARN,
			 ("RTMPSetIndividualHT: MCS(%d) is invalid in 1S, reset it as MCS_7\n",
			  DesiredMcs));
		DesiredMcs = MCS_7;
	}

	if ((pAd->CommonCfg.DesiredHtPhy.ChannelWidth == BW_20)
	    && (DesiredMcs == MCS_32)) {
		DBGPRINT(RT_DEBUG_WARN,
			 ("RTMPSetIndividualHT: MCS_32 is only supported in 40-MHz, reset it as MCS_0\n"));
		DesiredMcs = MCS_0;
	}

	pDesired_ht_phy->bHtEnable = TRUE;

	/* Decide desired Tx MCS */
	switch (TxStream) {
	case 1:
		if (DesiredMcs == MCS_AUTO) {
			pDesired_ht_phy->MCSSet[0] = 0xff;
			pDesired_ht_phy->MCSSet[1] = 0x00;
		} else if (DesiredMcs <= MCS_7) {
			pDesired_ht_phy->MCSSet[0] = 1 << DesiredMcs;
			pDesired_ht_phy->MCSSet[1] = 0x00;
		}
		break;

	case 2:
		if (DesiredMcs == MCS_AUTO) {
			pDesired_ht_phy->MCSSet[0] = 0xff;
			pDesired_ht_phy->MCSSet[1] = 0xff;
		} else if (DesiredMcs <= MCS_15) {
			unsigned long mode;

			mode = DesiredMcs / 8;
			if (mode < 2)
				pDesired_ht_phy->MCSSet[mode] =
				    (1 << (DesiredMcs - mode * 8));
		}
		break;

	case 3:		/* 3*3 */
		if (DesiredMcs == MCS_AUTO) {
			/* MCS0 ~ MCS23, 3 bytes */
			pDesired_ht_phy->MCSSet[0] = 0xff;
			pDesired_ht_phy->MCSSet[1] = 0xff;
			pDesired_ht_phy->MCSSet[2] = 0xff;
		} else if (DesiredMcs <= MCS_23) {
			unsigned long mode;

			mode = DesiredMcs / 8;
			if (mode < 3)
				pDesired_ht_phy->MCSSet[mode] =
				    (1 << (DesiredMcs - mode * 8));
		}
		break;
	}

	if (pAd->CommonCfg.DesiredHtPhy.ChannelWidth == BW_40) {
		if (DesiredMcs == MCS_AUTO || DesiredMcs == MCS_32)
			pDesired_ht_phy->MCSSet[4] = 0x1;
	}
	/* update HT Rate setting */
	if (pAd->OpMode == OPMODE_STA)
		MlmeUpdateHtTxRates(pAd, BSS0);
	else
		MlmeUpdateHtTxRates(pAd, apidx);
}

/*
	========================================================================
	Routine Description:
		Update HT IE from our capability.

	Arguments:
		Send all HT IE in beacon/probe rsp/assoc rsp/action frame.

	========================================================================
*/
void RTMPUpdateHTIE(struct rt_ht_capability *pRtHt,
		    u8 * pMcsSet,
		    struct rt_ht_capability_ie * pHtCapability,
		    struct rt_add_ht_info_ie * pAddHtInfo)
{
	RTMPZeroMemory(pHtCapability, sizeof(struct rt_ht_capability_ie));
	RTMPZeroMemory(pAddHtInfo, sizeof(struct rt_add_ht_info_ie));

	pHtCapability->HtCapInfo.ChannelWidth = pRtHt->ChannelWidth;
	pHtCapability->HtCapInfo.MimoPs = pRtHt->MimoPs;
	pHtCapability->HtCapInfo.GF = pRtHt->GF;
	pHtCapability->HtCapInfo.ShortGIfor20 = pRtHt->ShortGIfor20;
	pHtCapability->HtCapInfo.ShortGIfor40 = pRtHt->ShortGIfor40;
	pHtCapability->HtCapInfo.TxSTBC = pRtHt->TxSTBC;
	pHtCapability->HtCapInfo.RxSTBC = pRtHt->RxSTBC;
	pHtCapability->HtCapInfo.AMsduSize = pRtHt->AmsduSize;
	pHtCapability->HtCapParm.MaxRAmpduFactor = pRtHt->MaxRAmpduFactor;
	pHtCapability->HtCapParm.MpduDensity = pRtHt->MpduDensity;

	pAddHtInfo->AddHtInfo.ExtChanOffset = pRtHt->ExtChanOffset;
	pAddHtInfo->AddHtInfo.RecomWidth = pRtHt->RecomWidth;
	pAddHtInfo->AddHtInfo2.OperaionMode = pRtHt->OperaionMode;
	pAddHtInfo->AddHtInfo2.NonGfPresent = pRtHt->NonGfPresent;
	RTMPMoveMemory(pAddHtInfo->MCSSet, /*pRtHt->MCSSet */ pMcsSet, 4);	/* rt2860 only support MCS max=32, no need to copy all 16 uchar. */

	DBGPRINT(RT_DEBUG_TRACE, ("RTMPUpdateHTIE <== \n"));
}

/*
	========================================================================
	Description:
		Add Client security information into ASIC WCID table and IVEIV table.
    Return:
	========================================================================
*/
void RTMPAddWcidAttributeEntry(struct rt_rtmp_adapter *pAd,
			       u8 BssIdx,
			       u8 KeyIdx,
			       u8 CipherAlg, struct rt_mac_table_entry *pEntry)
{
	u32 WCIDAttri = 0;
	u16 offset;
	u8 IVEIV = 0;
	u16 Wcid = 0;

	{
		{
			if (BssIdx > BSS0) {
				DBGPRINT(RT_DEBUG_ERROR,
					 ("RTMPAddWcidAttributeEntry: The BSS-index(%d) is out of range for Infra link. \n",
					  BssIdx));
				return;
			}
			/* 1.   In ADHOC mode, the AID is wcid number. And NO mesh link exists. */
			/* 2.   In Infra mode, the AID:1 MUST be wcid of infra STA. */
			/*                                         the AID:2~ assign to mesh link entry. */
			if (pEntry)
				Wcid = pEntry->Aid;
			else
				Wcid = MCAST_WCID;
		}
	}

	/* Update WCID attribute table */
	offset = MAC_WCID_ATTRIBUTE_BASE + (Wcid * HW_WCID_ATTRI_SIZE);

	{
		if (pEntry && pEntry->ValidAsMesh)
			WCIDAttri = (CipherAlg << 1) | PAIRWISEKEYTABLE;
		else
			WCIDAttri = (CipherAlg << 1) | SHAREDKEYTABLE;
	}

	RTMP_IO_WRITE32(pAd, offset, WCIDAttri);

	/* Update IV/EIV table */
	offset = MAC_IVEIV_TABLE_BASE + (Wcid * HW_IVEIV_ENTRY_SIZE);

	/* WPA mode */
	if ((CipherAlg == CIPHER_TKIP) || (CipherAlg == CIPHER_TKIP_NO_MIC)
	    || (CipherAlg == CIPHER_AES)) {
		/* Eiv bit on. keyid always is 0 for pairwise key */
		IVEIV = (KeyIdx << 6) | 0x20;
	} else {
		/* WEP KeyIdx is default tx key. */
		IVEIV = (KeyIdx << 6);
	}

	/* For key index and ext IV bit, so only need to update the position(offset+3). */
#ifdef RTMP_MAC_PCI
	RTMP_IO_WRITE8(pAd, offset + 3, IVEIV);
#endif /* RTMP_MAC_PCI // */
#ifdef RTMP_MAC_USB
	RTUSBMultiWrite_OneByte(pAd, offset + 3, &IVEIV);
#endif /* RTMP_MAC_USB // */

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPAddWcidAttributeEntry: WCID #%d, KeyIndex #%d, Alg=%s\n",
		  Wcid, KeyIdx, CipherName[CipherAlg]));
	DBGPRINT(RT_DEBUG_TRACE, ("	WCIDAttri = 0x%x \n", WCIDAttri));

}

/*
    ==========================================================================
    Description:
        Parse encryption type
Arguments:
    pAdapter                    Pointer to our adapter
    wrq                         Pointer to the ioctl argument

    Return Value:
        None

    Note:
    ==========================================================================
*/
char *GetEncryptType(char enc)
{
	if (enc == Ndis802_11WEPDisabled)
		return "NONE";
	if (enc == Ndis802_11WEPEnabled)
		return "WEP";
	if (enc == Ndis802_11Encryption2Enabled)
		return "TKIP";
	if (enc == Ndis802_11Encryption3Enabled)
		return "AES";
	if (enc == Ndis802_11Encryption4Enabled)
		return "TKIPAES";
	else
		return "UNKNOW";
}

char *GetAuthMode(char auth)
{
	if (auth == Ndis802_11AuthModeOpen)
		return "OPEN";
	if (auth == Ndis802_11AuthModeShared)
		return "SHARED";
	if (auth == Ndis802_11AuthModeAutoSwitch)
		return "AUTOWEP";
	if (auth == Ndis802_11AuthModeWPA)
		return "WPA";
	if (auth == Ndis802_11AuthModeWPAPSK)
		return "WPAPSK";
	if (auth == Ndis802_11AuthModeWPANone)
		return "WPANONE";
	if (auth == Ndis802_11AuthModeWPA2)
		return "WPA2";
	if (auth == Ndis802_11AuthModeWPA2PSK)
		return "WPA2PSK";
	if (auth == Ndis802_11AuthModeWPA1WPA2)
		return "WPA1WPA2";
	if (auth == Ndis802_11AuthModeWPA1PSKWPA2PSK)
		return "WPA1PSKWPA2PSK";

	return "UNKNOW";
}

int SetCommonHT(struct rt_rtmp_adapter *pAd)
{
	struct rt_oid_set_ht_phymode SetHT;

	if (pAd->CommonCfg.PhyMode < PHY_11ABGN_MIXED)
		return FALSE;

	SetHT.PhyMode = pAd->CommonCfg.PhyMode;
	SetHT.TransmitNo = ((u8)pAd->Antenna.field.TxPath);
	SetHT.HtMode = (u8)pAd->CommonCfg.RegTransmitSetting.field.HTMODE;
	SetHT.ExtOffset =
	    (u8)pAd->CommonCfg.RegTransmitSetting.field.EXTCHA;
	SetHT.MCS = MCS_AUTO;
	SetHT.BW = (u8)pAd->CommonCfg.RegTransmitSetting.field.BW;
	SetHT.STBC = (u8)pAd->CommonCfg.RegTransmitSetting.field.STBC;
	SetHT.SHORTGI = (u8)pAd->CommonCfg.RegTransmitSetting.field.ShortGI;

	RTMPSetHT(pAd, &SetHT);

	return TRUE;
}

char *RTMPGetRalinkEncryModeStr(u16 encryMode)
{
	switch (encryMode) {
	case Ndis802_11WEPDisabled:
		return "NONE";
	case Ndis802_11WEPEnabled:
		return "WEP";
	case Ndis802_11Encryption2Enabled:
		return "TKIP";
	case Ndis802_11Encryption3Enabled:
		return "AES";
	case Ndis802_11Encryption4Enabled:
		return "TKIPAES";
	default:
		return "UNKNOW";
	}
}
