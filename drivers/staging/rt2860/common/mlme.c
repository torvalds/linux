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

	Module Name:
	mlme.c

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	John Chang	2004-08-25		Modify from RT2500 code base
	John Chang	2004-09-06		modified for RT2600
*/

#include "../rt_config.h"
#include <stdarg.h>

u8 CISCO_OUI[] = { 0x00, 0x40, 0x96 };

u8 WPA_OUI[] = { 0x00, 0x50, 0xf2, 0x01 };
u8 RSN_OUI[] = { 0x00, 0x0f, 0xac };
u8 WME_INFO_ELEM[] = { 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01 };
u8 WME_PARM_ELEM[] = { 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01 };
u8 Ccx2QosInfo[] = { 0x00, 0x40, 0x96, 0x04 };
u8 RALINK_OUI[] = { 0x00, 0x0c, 0x43 };
u8 BROADCOM_OUI[] = { 0x00, 0x90, 0x4c };
u8 WPS_OUI[] = { 0x00, 0x50, 0xf2, 0x04 };
u8 PRE_N_HT_OUI[] = { 0x00, 0x90, 0x4c };

u8 RateSwitchTable[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x11, 0x00, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x00, 0, 40, 101,
	0x01, 0x00, 1, 40, 50,
	0x02, 0x00, 2, 35, 45,
	0x03, 0x00, 3, 20, 45,
	0x04, 0x21, 0, 30, 50,
	0x05, 0x21, 1, 20, 50,
	0x06, 0x21, 2, 20, 50,
	0x07, 0x21, 3, 15, 50,
	0x08, 0x21, 4, 15, 30,
	0x09, 0x21, 5, 10, 25,
	0x0a, 0x21, 6, 8, 25,
	0x0b, 0x21, 7, 8, 25,
	0x0c, 0x20, 12, 15, 30,
	0x0d, 0x20, 13, 8, 20,
	0x0e, 0x20, 14, 8, 20,
	0x0f, 0x20, 15, 8, 25,
	0x10, 0x22, 15, 8, 25,
	0x11, 0x00, 0, 0, 0,
	0x12, 0x00, 0, 0, 0,
	0x13, 0x00, 0, 0, 0,
	0x14, 0x00, 0, 0, 0,
	0x15, 0x00, 0, 0, 0,
	0x16, 0x00, 0, 0, 0,
	0x17, 0x00, 0, 0, 0,
	0x18, 0x00, 0, 0, 0,
	0x19, 0x00, 0, 0, 0,
	0x1a, 0x00, 0, 0, 0,
	0x1b, 0x00, 0, 0, 0,
	0x1c, 0x00, 0, 0, 0,
	0x1d, 0x00, 0, 0, 0,
	0x1e, 0x00, 0, 0, 0,
	0x1f, 0x00, 0, 0, 0,
};

u8 RateSwitchTable11B[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x04, 0x03, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x00, 0, 40, 101,
	0x01, 0x00, 1, 40, 50,
	0x02, 0x00, 2, 35, 45,
	0x03, 0x00, 3, 20, 45,
};

u8 RateSwitchTable11BG[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0a, 0x00, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x00, 0, 40, 101,
	0x01, 0x00, 1, 40, 50,
	0x02, 0x00, 2, 35, 45,
	0x03, 0x00, 3, 20, 45,
	0x04, 0x10, 2, 20, 35,
	0x05, 0x10, 3, 16, 35,
	0x06, 0x10, 4, 10, 25,
	0x07, 0x10, 5, 16, 25,
	0x08, 0x10, 6, 10, 25,
	0x09, 0x10, 7, 10, 13,
};

u8 RateSwitchTable11G[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x08, 0x00, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x10, 0, 20, 101,
	0x01, 0x10, 1, 20, 35,
	0x02, 0x10, 2, 20, 35,
	0x03, 0x10, 3, 16, 35,
	0x04, 0x10, 4, 10, 25,
	0x05, 0x10, 5, 16, 25,
	0x06, 0x10, 6, 10, 25,
	0x07, 0x10, 7, 10, 13,
};

u8 RateSwitchTable11N1S[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0c, 0x0a, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x00, 0, 40, 101,
	0x01, 0x00, 1, 40, 50,
	0x02, 0x00, 2, 25, 45,
	0x03, 0x21, 0, 20, 35,
	0x04, 0x21, 1, 20, 35,
	0x05, 0x21, 2, 20, 35,
	0x06, 0x21, 3, 15, 35,
	0x07, 0x21, 4, 15, 30,
	0x08, 0x21, 5, 10, 25,
	0x09, 0x21, 6, 8, 14,
	0x0a, 0x21, 7, 8, 14,
	0x0b, 0x23, 7, 8, 14,
};

u8 RateSwitchTable11N2S[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0e, 0x0c, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x00, 0, 40, 101,
	0x01, 0x00, 1, 40, 50,
	0x02, 0x00, 2, 25, 45,
	0x03, 0x21, 0, 20, 35,
	0x04, 0x21, 1, 20, 35,
	0x05, 0x21, 2, 20, 35,
	0x06, 0x21, 3, 15, 35,
	0x07, 0x21, 4, 15, 30,
	0x08, 0x20, 11, 15, 30,
	0x09, 0x20, 12, 15, 30,
	0x0a, 0x20, 13, 8, 20,
	0x0b, 0x20, 14, 8, 20,
	0x0c, 0x20, 15, 8, 25,
	0x0d, 0x22, 15, 8, 15,
};

u8 RateSwitchTable11N3S[] = {
/* Item No.     Mode    Curr-MCS        TrainUp TrainDown       // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0b, 0x00, 0, 0, 0,	/* 0x0a, 0x00,  0,  0,  0,      // Initial used item after association */
	0x00, 0x21, 0, 30, 101,
	0x01, 0x21, 1, 20, 50,
	0x02, 0x21, 2, 20, 50,
	0x03, 0x21, 3, 15, 50,
	0x04, 0x21, 4, 15, 30,
	0x05, 0x20, 11, 15, 30,	/* Required by System-Alan @ 20080812 */
	0x06, 0x20, 12, 15, 30,	/* 0x05, 0x20, 12, 15, 30, */
	0x07, 0x20, 13, 8, 20,	/* 0x06, 0x20, 13,  8, 20, */
	0x08, 0x20, 14, 8, 20,	/* 0x07, 0x20, 14,  8, 20, */
	0x09, 0x20, 15, 8, 25,	/* 0x08, 0x20, 15,  8, 25, */
	0x0a, 0x22, 15, 8, 25,	/* 0x09, 0x22, 15,  8, 25, */
};

u8 RateSwitchTable11N2SForABand[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0b, 0x09, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x21, 0, 30, 101,
	0x01, 0x21, 1, 20, 50,
	0x02, 0x21, 2, 20, 50,
	0x03, 0x21, 3, 15, 50,
	0x04, 0x21, 4, 15, 30,
	0x05, 0x21, 5, 15, 30,
	0x06, 0x20, 12, 15, 30,
	0x07, 0x20, 13, 8, 20,
	0x08, 0x20, 14, 8, 20,
	0x09, 0x20, 15, 8, 25,
	0x0a, 0x22, 15, 8, 25,
};

u8 RateSwitchTable11N3SForABand[] = {	/* 3*3 */
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0b, 0x09, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x21, 0, 30, 101,
	0x01, 0x21, 1, 20, 50,
	0x02, 0x21, 2, 20, 50,
	0x03, 0x21, 3, 15, 50,
	0x04, 0x21, 4, 15, 30,
	0x05, 0x21, 5, 15, 30,
	0x06, 0x20, 12, 15, 30,
	0x07, 0x20, 13, 8, 20,
	0x08, 0x20, 14, 8, 20,
	0x09, 0x20, 15, 8, 25,
	0x0a, 0x22, 15, 8, 25,
};

u8 RateSwitchTable11BGN1S[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0c, 0x0a, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x00, 0, 40, 101,
	0x01, 0x00, 1, 40, 50,
	0x02, 0x00, 2, 25, 45,
	0x03, 0x21, 0, 20, 35,
	0x04, 0x21, 1, 20, 35,
	0x05, 0x21, 2, 20, 35,
	0x06, 0x21, 3, 15, 35,
	0x07, 0x21, 4, 15, 30,
	0x08, 0x21, 5, 10, 25,
	0x09, 0x21, 6, 8, 14,
	0x0a, 0x21, 7, 8, 14,
	0x0b, 0x23, 7, 8, 14,
};

u8 RateSwitchTable11BGN2S[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0e, 0x0c, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x00, 0, 40, 101,
	0x01, 0x00, 1, 40, 50,
	0x02, 0x00, 2, 25, 45,
	0x03, 0x21, 0, 20, 35,
	0x04, 0x21, 1, 20, 35,
	0x05, 0x21, 2, 20, 35,
	0x06, 0x21, 3, 15, 35,
	0x07, 0x21, 4, 15, 30,
	0x08, 0x20, 11, 15, 30,
	0x09, 0x20, 12, 15, 30,
	0x0a, 0x20, 13, 8, 20,
	0x0b, 0x20, 14, 8, 20,
	0x0c, 0x20, 15, 8, 25,
	0x0d, 0x22, 15, 8, 15,
};

u8 RateSwitchTable11BGN3S[] = {	/* 3*3 */
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0a, 0x00, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x21, 0, 30, 101,	/*50 */
	0x01, 0x21, 1, 20, 50,
	0x02, 0x21, 2, 20, 50,
	0x03, 0x21, 3, 20, 50,
	0x04, 0x21, 4, 15, 50,
	0x05, 0x20, 20, 15, 30,
	0x06, 0x20, 21, 8, 20,
	0x07, 0x20, 22, 8, 20,
	0x08, 0x20, 23, 8, 25,
	0x09, 0x22, 23, 8, 25,
};

u8 RateSwitchTable11BGN2SForABand[] = {
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0b, 0x09, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x21, 0, 30, 101,	/*50 */
	0x01, 0x21, 1, 20, 50,
	0x02, 0x21, 2, 20, 50,
	0x03, 0x21, 3, 15, 50,
	0x04, 0x21, 4, 15, 30,
	0x05, 0x21, 5, 15, 30,
	0x06, 0x20, 12, 15, 30,
	0x07, 0x20, 13, 8, 20,
	0x08, 0x20, 14, 8, 20,
	0x09, 0x20, 15, 8, 25,
	0x0a, 0x22, 15, 8, 25,
};

u8 RateSwitchTable11BGN3SForABand[] = {	/* 3*3 */
/* Item No.   Mode   Curr-MCS   TrainUp   TrainDown             // Mode- Bit0: STBC, Bit1: Short GI, Bit4,5: Mode(0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	0x0c, 0x09, 0, 0, 0,	/* Initial used item after association */
	0x00, 0x21, 0, 30, 101,	/*50 */
	0x01, 0x21, 1, 20, 50,
	0x02, 0x21, 2, 20, 50,
	0x03, 0x21, 3, 15, 50,
	0x04, 0x21, 4, 15, 30,
	0x05, 0x21, 5, 15, 30,
	0x06, 0x21, 12, 15, 30,
	0x07, 0x20, 20, 15, 30,
	0x08, 0x20, 21, 8, 20,
	0x09, 0x20, 22, 8, 20,
	0x0a, 0x20, 23, 8, 25,
	0x0b, 0x22, 23, 8, 25,
};

extern u8 OfdmRateToRxwiMCS[];
/* since RT61 has better RX sensibility, we have to limit TX ACK rate not to exceed our normal data TX rate. */
/* otherwise the WLAN peer may not be able to receive the ACK thus downgrade its data TX rate */
unsigned long BasicRateMask[12] =
    { 0xfffff001 /* 1-Mbps */ , 0xfffff003 /* 2 Mbps */ , 0xfffff007 /* 5.5 */ ,
0xfffff00f /* 11 */ ,
	0xfffff01f /* 6 */ , 0xfffff03f /* 9 */ , 0xfffff07f /* 12 */ ,
	    0xfffff0ff /* 18 */ ,
	0xfffff1ff /* 24 */ , 0xfffff3ff /* 36 */ , 0xfffff7ff /* 48 */ ,
	    0xffffffff /* 54 */
};

u8 BROADCAST_ADDR[MAC_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
u8 ZERO_MAC_ADDR[MAC_ADDR_LEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* e.g. RssiSafeLevelForTxRate[RATE_36]" means if the current RSSI is greater than */
/*              this value, then it's quaranteed capable of operating in 36 mbps TX rate in */
/*              clean environment. */
/*                                                                TxRate: 1   2   5.5   11       6        9    12       18       24   36   48   54       72  100 */
char RssiSafeLevelForTxRate[] =
    { -92, -91, -90, -87, -88, -86, -85, -83, -81, -78, -72, -71, -40, -40 };

u8 RateIdToMbps[] = { 1, 2, 5, 11, 6, 9, 12, 18, 24, 36, 48, 54, 72, 100 };
u16 RateIdTo500Kbps[] =
    { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108, 144, 200 };

u8 SsidIe = IE_SSID;
u8 SupRateIe = IE_SUPP_RATES;
u8 ExtRateIe = IE_EXT_SUPP_RATES;
u8 HtCapIe = IE_HT_CAP;
u8 AddHtInfoIe = IE_ADD_HT;
u8 NewExtChanIe = IE_SECONDARY_CH_OFFSET;
u8 ErpIe = IE_ERP;
u8 DsIe = IE_DS_PARM;
u8 TimIe = IE_TIM;
u8 WpaIe = IE_WPA;
u8 Wpa2Ie = IE_WPA2;
u8 IbssIe = IE_IBSS_PARM;

extern u8 WPA_OUI[];

u8 SES_OUI[] = { 0x00, 0x90, 0x4c };

u8 ZeroSsid[32] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	    0x00, 0x00, 0x00, 0x00
};

/*
	==========================================================================
	Description:
		initialize the MLME task and its data structure (queue, spinlock,
		timer, state machines).

	IRQL = PASSIVE_LEVEL

	Return:
		always return NDIS_STATUS_SUCCESS

	==========================================================================
*/
int MlmeInit(struct rt_rtmp_adapter *pAd)
{
	int Status = NDIS_STATUS_SUCCESS;

	DBGPRINT(RT_DEBUG_TRACE, ("--> MLME Initialize\n"));

	do {
		Status = MlmeQueueInit(&pAd->Mlme.Queue);
		if (Status != NDIS_STATUS_SUCCESS)
			break;

		pAd->Mlme.bRunning = FALSE;
		NdisAllocateSpinLock(&pAd->Mlme.TaskLock);

		{
			BssTableInit(&pAd->ScanTab);

			/* init STA state machines */
			AssocStateMachineInit(pAd, &pAd->Mlme.AssocMachine,
					      pAd->Mlme.AssocFunc);
			AuthStateMachineInit(pAd, &pAd->Mlme.AuthMachine,
					     pAd->Mlme.AuthFunc);
			AuthRspStateMachineInit(pAd, &pAd->Mlme.AuthRspMachine,
						pAd->Mlme.AuthRspFunc);
			SyncStateMachineInit(pAd, &pAd->Mlme.SyncMachine,
					     pAd->Mlme.SyncFunc);

			/* Since we are using switch/case to implement it, the init is different from the above */
			/* state machine init */
			MlmeCntlInit(pAd, &pAd->Mlme.CntlMachine, NULL);
		}

		WpaStateMachineInit(pAd, &pAd->Mlme.WpaMachine,
				    pAd->Mlme.WpaFunc);

		ActionStateMachineInit(pAd, &pAd->Mlme.ActMachine,
				       pAd->Mlme.ActFunc);

		/* Init mlme periodic timer */
		RTMPInitTimer(pAd, &pAd->Mlme.PeriodicTimer,
			      GET_TIMER_FUNCTION(MlmePeriodicExec), pAd, TRUE);

		/* Set mlme periodic timer */
		RTMPSetTimer(&pAd->Mlme.PeriodicTimer, MLME_TASK_EXEC_INTV);

		/* software-based RX Antenna diversity */
		RTMPInitTimer(pAd, &pAd->Mlme.RxAntEvalTimer,
			      GET_TIMER_FUNCTION(AsicRxAntEvalTimeout), pAd,
			      FALSE);

		{
#ifdef RTMP_PCI_SUPPORT
			if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)) {
				/* only PCIe cards need these two timers */
				RTMPInitTimer(pAd, &pAd->Mlme.PsPollTimer,
					      GET_TIMER_FUNCTION
					      (PsPollWakeExec), pAd, FALSE);
				RTMPInitTimer(pAd, &pAd->Mlme.RadioOnOffTimer,
					      GET_TIMER_FUNCTION(RadioOnExec),
					      pAd, FALSE);
			}
#endif /* RTMP_PCI_SUPPORT // */

			RTMPInitTimer(pAd, &pAd->Mlme.LinkDownTimer,
				      GET_TIMER_FUNCTION(LinkDownExec), pAd,
				      FALSE);

#ifdef RTMP_MAC_USB
			RTMPInitTimer(pAd, &pAd->Mlme.AutoWakeupTimer,
				      GET_TIMER_FUNCTION
				      (RtmpUsbStaAsicForceWakeupTimeout), pAd,
				      FALSE);
			pAd->Mlme.AutoWakeupTimerRunning = FALSE;
#endif /* RTMP_MAC_USB // */
		}

	} while (FALSE);

	DBGPRINT(RT_DEBUG_TRACE, ("<-- MLME Initialize\n"));

	return Status;
}

/*
	==========================================================================
	Description:
		main loop of the MLME
	Pre:
		Mlme has to be initialized, and there are something inside the queue
	Note:
		This function is invoked from MPSetInformation and MPReceive;
		This task guarantee only one MlmeHandler will run.

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
void MlmeHandler(struct rt_rtmp_adapter *pAd)
{
	struct rt_mlme_queue_elem *Elem = NULL;

	/* Only accept MLME and Frame from peer side, no other (control/data) frame should */
	/* get into this state machine */

	NdisAcquireSpinLock(&pAd->Mlme.TaskLock);
	if (pAd->Mlme.bRunning) {
		NdisReleaseSpinLock(&pAd->Mlme.TaskLock);
		return;
	} else {
		pAd->Mlme.bRunning = TRUE;
	}
	NdisReleaseSpinLock(&pAd->Mlme.TaskLock);

	while (!MlmeQueueEmpty(&pAd->Mlme.Queue)) {
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_MLME_RESET_IN_PROGRESS) ||
		    RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS) ||
		    RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Device Halted or Removed or MlmeRest, exit MlmeHandler! (queue num = %ld)\n",
				  pAd->Mlme.Queue.Num));
			break;
		}
		/*From message type, determine which state machine I should drive */
		if (MlmeDequeue(&pAd->Mlme.Queue, &Elem)) {
#ifdef RTMP_MAC_USB
			if (Elem->MsgType == MT2_RESET_CONF) {
				DBGPRINT_RAW(RT_DEBUG_TRACE,
					     ("reset MLME state machine!\n"));
				MlmeRestartStateMachine(pAd);
				Elem->Occupied = FALSE;
				Elem->MsgLen = 0;
				continue;
			}
#endif /* RTMP_MAC_USB // */

			/* if dequeue success */
			switch (Elem->Machine) {
				/* STA state machines */
			case ASSOC_STATE_MACHINE:
				StateMachinePerformAction(pAd,
							  &pAd->Mlme.
							  AssocMachine, Elem);
				break;
			case AUTH_STATE_MACHINE:
				StateMachinePerformAction(pAd,
							  &pAd->Mlme.
							  AuthMachine, Elem);
				break;
			case AUTH_RSP_STATE_MACHINE:
				StateMachinePerformAction(pAd,
							  &pAd->Mlme.
							  AuthRspMachine, Elem);
				break;
			case SYNC_STATE_MACHINE:
				StateMachinePerformAction(pAd,
							  &pAd->Mlme.
							  SyncMachine, Elem);
				break;
			case MLME_CNTL_STATE_MACHINE:
				MlmeCntlMachinePerformAction(pAd,
							     &pAd->Mlme.
							     CntlMachine, Elem);
				break;
			case WPA_PSK_STATE_MACHINE:
				StateMachinePerformAction(pAd,
							  &pAd->Mlme.
							  WpaPskMachine, Elem);
				break;

			case ACTION_STATE_MACHINE:
				StateMachinePerformAction(pAd,
							  &pAd->Mlme.ActMachine,
							  Elem);
				break;

			case WPA_STATE_MACHINE:
				StateMachinePerformAction(pAd,
							  &pAd->Mlme.WpaMachine,
							  Elem);
				break;

			default:
				DBGPRINT(RT_DEBUG_TRACE,
					 ("ERROR: Illegal machine %ld in MlmeHandler()\n",
					  Elem->Machine));
				break;
			}	/* end of switch */

			/* free MLME element */
			Elem->Occupied = FALSE;
			Elem->MsgLen = 0;

		} else {
			DBGPRINT_ERR(("MlmeHandler: MlmeQueue empty\n"));
		}
	}

	NdisAcquireSpinLock(&pAd->Mlme.TaskLock);
	pAd->Mlme.bRunning = FALSE;
	NdisReleaseSpinLock(&pAd->Mlme.TaskLock);
}

/*
	==========================================================================
	Description:
		Destructor of MLME (Destroy queue, state machine, spin lock and timer)
	Parameters:
		Adapter - NIC Adapter pointer
	Post:
		The MLME task will no longer work properly

	IRQL = PASSIVE_LEVEL

	==========================================================================
 */
void MlmeHalt(struct rt_rtmp_adapter *pAd)
{
	BOOLEAN Cancelled;

	DBGPRINT(RT_DEBUG_TRACE, ("==> MlmeHalt\n"));

	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) {
		/* disable BEACON generation and other BEACON related hardware timers */
		AsicDisableSync(pAd);
	}

	{
		/* Cancel pending timers */
		RTMPCancelTimer(&pAd->MlmeAux.AssocTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ReassocTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.DisassocTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.AuthTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &Cancelled);

#ifdef RTMP_MAC_PCI
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)
		    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)) {
			RTMPCancelTimer(&pAd->Mlme.PsPollTimer, &Cancelled);
			RTMPCancelTimer(&pAd->Mlme.RadioOnOffTimer, &Cancelled);
		}
#endif /* RTMP_MAC_PCI // */

		RTMPCancelTimer(&pAd->Mlme.LinkDownTimer, &Cancelled);

#ifdef RTMP_MAC_USB
		RTMPCancelTimer(&pAd->Mlme.AutoWakeupTimer, &Cancelled);
#endif /* RTMP_MAC_USB // */
	}

	RTMPCancelTimer(&pAd->Mlme.PeriodicTimer, &Cancelled);
	RTMPCancelTimer(&pAd->Mlme.RxAntEvalTimer, &Cancelled);

	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) {
		struct rt_rtmp_chip_op *pChipOps = &pAd->chipOps;

		/* Set LED */
		RTMPSetLED(pAd, LED_HALT);
		RTMPSetSignalLED(pAd, -100);	/* Force signal strength Led to be turned off, firmware is not done it. */
#ifdef RTMP_MAC_USB
		{
			LED_CFG_STRUC LedCfg;
			RTMP_IO_READ32(pAd, LED_CFG, &LedCfg.word);
			LedCfg.field.LedPolar = 0;
			LedCfg.field.RLedMode = 0;
			LedCfg.field.GLedMode = 0;
			LedCfg.field.YLedMode = 0;
			RTMP_IO_WRITE32(pAd, LED_CFG, LedCfg.word);
		}
#endif /* RTMP_MAC_USB // */

		if (pChipOps->AsicHaltAction)
			pChipOps->AsicHaltAction(pAd);
	}

	RTMPusecDelay(5000);	/*  5 msec to gurantee Ant Diversity timer canceled */

	MlmeQueueDestroy(&pAd->Mlme.Queue);
	NdisFreeSpinLock(&pAd->Mlme.TaskLock);

	DBGPRINT(RT_DEBUG_TRACE, ("<== MlmeHalt\n"));
}

void MlmeResetRalinkCounters(struct rt_rtmp_adapter *pAd)
{
	pAd->RalinkCounters.LastOneSecRxOkDataCnt =
	    pAd->RalinkCounters.OneSecRxOkDataCnt;
	/* clear all OneSecxxx counters. */
	pAd->RalinkCounters.OneSecBeaconSentCnt = 0;
	pAd->RalinkCounters.OneSecFalseCCACnt = 0;
	pAd->RalinkCounters.OneSecRxFcsErrCnt = 0;
	pAd->RalinkCounters.OneSecRxOkCnt = 0;
	pAd->RalinkCounters.OneSecTxFailCount = 0;
	pAd->RalinkCounters.OneSecTxNoRetryOkCount = 0;
	pAd->RalinkCounters.OneSecTxRetryOkCount = 0;
	pAd->RalinkCounters.OneSecRxOkDataCnt = 0;
	pAd->RalinkCounters.OneSecReceivedByteCount = 0;
	pAd->RalinkCounters.OneSecTransmittedByteCount = 0;

	/* TODO: for debug only. to be removed */
	pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BE] = 0;
	pAd->RalinkCounters.OneSecOsTxCount[QID_AC_BK] = 0;
	pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VI] = 0;
	pAd->RalinkCounters.OneSecOsTxCount[QID_AC_VO] = 0;
	pAd->RalinkCounters.OneSecDmaDoneCount[QID_AC_BE] = 0;
	pAd->RalinkCounters.OneSecDmaDoneCount[QID_AC_BK] = 0;
	pAd->RalinkCounters.OneSecDmaDoneCount[QID_AC_VI] = 0;
	pAd->RalinkCounters.OneSecDmaDoneCount[QID_AC_VO] = 0;
	pAd->RalinkCounters.OneSecTxDoneCount = 0;
	pAd->RalinkCounters.OneSecRxCount = 0;
	pAd->RalinkCounters.OneSecTxAggregationCount = 0;
	pAd->RalinkCounters.OneSecRxAggregationCount = 0;

	return;
}

/*
	==========================================================================
	Description:
		This routine is executed periodically to -
		1. Decide if it's a right time to turn on PwrMgmt bit of all
		   outgoiing frames
		2. Calculate ChannelQuality based on statistics of the last
		   period, so that TX rate won't toggling very frequently between a
		   successful TX and a failed TX.
		3. If the calculated ChannelQuality indicated current connection not
		   healthy, then a ROAMing attempt is tried here.

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
#define ADHOC_BEACON_LOST_TIME		(8*OS_HZ)	/* 8 sec */
void MlmePeriodicExec(void *SystemSpecific1,
		      void *FunctionContext,
		      void *SystemSpecific2, void *SystemSpecific3)
{
	unsigned long TxTotalCnt;
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;

#ifdef RTMP_MAC_PCI
	{
		/* If Hardware controlled Radio enabled, we have to check GPIO pin2 every 2 second. */
		/* Move code to here, because following code will return when radio is off */
		if ((pAd->Mlme.PeriodicRound % (MLME_TASK_EXEC_MULTIPLE * 2) ==
		     0) && (pAd->StaCfg.bHardwareRadio == TRUE)
		    && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))
		    && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))
		    /*&&(pAd->bPCIclkOff == FALSE) */
		    ) {
			u32 data = 0;

			/* Read GPIO pin2 as Hardware controlled radio state */
#ifndef RT3090
			RTMP_IO_READ32(pAd, GPIO_CTRL_CFG, &data);
#endif /* RT3090 // */
/*KH(PCIE PS):Added based on Jane<-- */
#ifdef RT3090
/* Read GPIO pin2 as Hardware controlled radio state */
/* We need to Read GPIO if HW said so no mater what advance power saving */
			if ((pAd->OpMode == OPMODE_STA) && (IDLE_ON(pAd))
			    &&
			    (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
			    && (pAd->StaCfg.PSControl.field.EnablePSinIdle ==
				TRUE)) {
				/* Want to make sure device goes to L0 state before reading register. */
				RTMPPCIeLinkCtrlValueRestore(pAd, 0);
				RTMP_IO_FORCE_READ32(pAd, GPIO_CTRL_CFG, &data);
				RTMPPCIeLinkCtrlSetting(pAd, 3);
			} else
				RTMP_IO_FORCE_READ32(pAd, GPIO_CTRL_CFG, &data);
#endif /* RT3090 // */
/*KH(PCIE PS):Added based on Jane--> */

			if (data & 0x04) {
				pAd->StaCfg.bHwRadio = TRUE;
			} else {
				pAd->StaCfg.bHwRadio = FALSE;
			}
			if (pAd->StaCfg.bRadio !=
			    (pAd->StaCfg.bHwRadio && pAd->StaCfg.bSwRadio)) {
				pAd->StaCfg.bRadio = (pAd->StaCfg.bHwRadio
						      && pAd->StaCfg.bSwRadio);
				if (pAd->StaCfg.bRadio == TRUE) {
					MlmeRadioOn(pAd);
					/* Update extra information */
					pAd->ExtraInfo = EXTRA_INFO_CLEAR;
				} else {
					MlmeRadioOff(pAd);
					/* Update extra information */
					pAd->ExtraInfo = HW_RADIO_OFF;
				}
			}
		}
	}
#endif /* RTMP_MAC_PCI // */

	/* Do nothing if the driver is starting halt state. */
	/* This might happen when timer already been fired before cancel timer with mlmehalt */
	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_HALT_IN_PROGRESS |
				  fRTMP_ADAPTER_RADIO_OFF |
				  fRTMP_ADAPTER_RADIO_MEASUREMENT |
				  fRTMP_ADAPTER_RESET_IN_PROGRESS))))
		return;

	RTMP_MLME_PRE_SANITY_CHECK(pAd);

	{
		/* Do nothing if monitor mode is on */
		if (MONITOR_ON(pAd))
			return;

		if (pAd->Mlme.PeriodicRound & 0x1) {
			/* This is the fix for wifi 11n extension channel overlapping test case.  for 2860D */
			if (((pAd->MACVersion & 0xffff) == 0x0101) &&
			    (STA_TGN_WIFI_ON(pAd)) &&
			    (pAd->CommonCfg.IOTestParm.bToggle == FALSE))
			{
				RTMP_IO_WRITE32(pAd, TXOP_CTRL_CFG, 0x24Bf);
				pAd->CommonCfg.IOTestParm.bToggle = TRUE;
			} else if ((STA_TGN_WIFI_ON(pAd)) &&
				   ((pAd->MACVersion & 0xffff) == 0x0101)) {
				RTMP_IO_WRITE32(pAd, TXOP_CTRL_CFG, 0x243f);
				pAd->CommonCfg.IOTestParm.bToggle = FALSE;
			}
		}
	}

	pAd->bUpdateBcnCntDone = FALSE;

/*      RECBATimerTimeout(SystemSpecific1,FunctionContext,SystemSpecific2,SystemSpecific3); */
	pAd->Mlme.PeriodicRound++;

#ifdef RTMP_MAC_USB
	/* execute every 100ms, update the Tx FIFO Cnt for update Tx Rate. */
	NICUpdateFifoStaCounters(pAd);
#endif /* RTMP_MAC_USB // */

	/* execute every 500ms */
	if ((pAd->Mlme.PeriodicRound % 5 == 0)
	    && RTMPAutoRateSwitchCheck(pAd)
	    /*(OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED)) */ )
	{
		/* perform dynamic tx rate switching based on past TX history */
		{
			if ((OPSTATUS_TEST_FLAG
			     (pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
			    )
			    && (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE)))
				MlmeDynamicTxRateSwitching(pAd);
		}
	}
	/* Normal 1 second Mlme PeriodicExec. */
	if (pAd->Mlme.PeriodicRound % MLME_TASK_EXEC_MULTIPLE == 0) {
		pAd->Mlme.OneSecPeriodicRound++;

		/*ORIBATimerTimeout(pAd); */

		/* Media status changed, report to NDIS */
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_MEDIA_STATE_CHANGE)) {
			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_MEDIA_STATE_CHANGE);
			if (OPSTATUS_TEST_FLAG
			    (pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)) {
				pAd->IndicateMediaState =
				    NdisMediaStateConnected;
				RTMP_IndicateMediaState(pAd);

			} else {
				pAd->IndicateMediaState =
				    NdisMediaStateDisconnected;
				RTMP_IndicateMediaState(pAd);
			}
		}

		NdisGetSystemUpTime(&pAd->Mlme.Now32);

		/* add the most up-to-date h/w raw counters into software variable, so that */
		/* the dynamic tuning mechanism below are based on most up-to-date information */
		NICUpdateRawCounters(pAd);

#ifdef RTMP_MAC_USB
		RTUSBWatchDog(pAd);
#endif /* RTMP_MAC_USB // */

		/* Need statistics after read counter. So put after NICUpdateRawCounters */
		ORIBATimerTimeout(pAd);

		/* if MGMT RING is full more than twice within 1 second, we consider there's */
		/* a hardware problem stucking the TX path. In this case, try a hardware reset */
		/* to recover the system */
		/*      if (pAd->RalinkCounters.MgmtRingFullCount >= 2) */
		/*              RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HARDWARE_ERROR); */
		/*      else */
		/*              pAd->RalinkCounters.MgmtRingFullCount = 0; */

		/* The time period for checking antenna is according to traffic */
		{
			if (pAd->Mlme.bEnableAutoAntennaCheck) {
				TxTotalCnt =
				    pAd->RalinkCounters.OneSecTxNoRetryOkCount +
				    pAd->RalinkCounters.OneSecTxRetryOkCount +
				    pAd->RalinkCounters.OneSecTxFailCount;

				/* dynamic adjust antenna evaluation period according to the traffic */
				if (TxTotalCnt > 50) {
					if (pAd->Mlme.OneSecPeriodicRound %
					    10 == 0) {
						AsicEvaluateRxAnt(pAd);
					}
				} else {
					if (pAd->Mlme.OneSecPeriodicRound % 3 ==
					    0) {
						AsicEvaluateRxAnt(pAd);
					}
				}
			}
		}

		STAMlmePeriodicExec(pAd);

		MlmeResetRalinkCounters(pAd);

		{
#ifdef RTMP_MAC_PCI
			if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)
			    && (pAd->bPCIclkOff == FALSE))
#endif /* RTMP_MAC_PCI // */
			{
				/* When Adhoc beacon is enabled and RTS/CTS is enabled, there is a chance that hardware MAC FSM will run into a deadlock */
				/* and sending CTS-to-self over and over. */
				/* Software Patch Solution: */
				/* 1. Polling debug state register 0x10F4 every one second. */
				/* 2. If in 0x10F4 the ((bit29==1) && (bit7==1)) OR ((bit29==1) && (bit5==1)), it means the deadlock has occurred. */
				/* 3. If the deadlock occurred, reset MAC/BBP by setting 0x1004 to 0x0001 for a while then setting it back to 0x000C again. */

				u32 MacReg = 0;

				RTMP_IO_READ32(pAd, 0x10F4, &MacReg);
				if (((MacReg & 0x20000000) && (MacReg & 0x80))
				    || ((MacReg & 0x20000000)
					&& (MacReg & 0x20))) {
					RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x1);
					RTMPusecDelay(1);
					RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0xC);

					DBGPRINT(RT_DEBUG_WARN,
						 ("Warning, MAC specific condition occurs \n"));
				}
			}
		}

		RTMP_MLME_HANDLER(pAd);
	}

	pAd->bUpdateBcnCntDone = FALSE;
}

/*
	==========================================================================
	Validate SSID for connection try and rescan purpose
	Valid SSID will have visible chars only.
	The valid length is from 0 to 32.
	IRQL = DISPATCH_LEVEL
	==========================================================================
 */
BOOLEAN MlmeValidateSSID(u8 *pSsid, u8 SsidLen)
{
	int index;

	if (SsidLen > MAX_LEN_OF_SSID)
		return (FALSE);

	/* Check each character value */
	for (index = 0; index < SsidLen; index++) {
		if (pSsid[index] < 0x20)
			return (FALSE);
	}

	/* All checked */
	return (TRUE);
}

void MlmeSelectTxRateTable(struct rt_rtmp_adapter *pAd,
			   struct rt_mac_table_entry *pEntry,
			   u8 ** ppTable,
			   u8 *pTableSize, u8 *pInitTxRateIdx)
{
	do {
		/* decide the rate table for tuning */
		if (pAd->CommonCfg.TxRateTableSize > 0) {
			*ppTable = RateSwitchTable;
			*pTableSize = RateSwitchTable[0];
			*pInitTxRateIdx = RateSwitchTable[1];

			break;
		}

		if ((pAd->OpMode == OPMODE_STA) && ADHOC_ON(pAd)) {
			if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) && (pEntry->HTCapability.MCSSet[0] == 0xff) && ((pEntry->HTCapability.MCSSet[1] == 0x00) || (pAd->Antenna.field.TxPath == 1))) {	/* 11N 1S Adhoc */
				*ppTable = RateSwitchTable11N1S;
				*pTableSize = RateSwitchTable11N1S[0];
				*pInitTxRateIdx = RateSwitchTable11N1S[1];

			} else if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) && (pEntry->HTCapability.MCSSet[0] == 0xff) && (pEntry->HTCapability.MCSSet[1] == 0xff) && (pAd->Antenna.field.TxPath == 2)) {	/* 11N 2S Adhoc */
				if (pAd->LatchRfRegs.Channel <= 14) {
					*ppTable = RateSwitchTable11N2S;
					*pTableSize = RateSwitchTable11N2S[0];
					*pInitTxRateIdx =
					    RateSwitchTable11N2S[1];
				} else {
					*ppTable = RateSwitchTable11N2SForABand;
					*pTableSize =
					    RateSwitchTable11N2SForABand[0];
					*pInitTxRateIdx =
					    RateSwitchTable11N2SForABand[1];
				}

			} else if ((pEntry->RateLen == 4)
				   && (pEntry->HTCapability.MCSSet[0] == 0)
				   && (pEntry->HTCapability.MCSSet[1] == 0)
			    ) {
				*ppTable = RateSwitchTable11B;
				*pTableSize = RateSwitchTable11B[0];
				*pInitTxRateIdx = RateSwitchTable11B[1];

			} else if (pAd->LatchRfRegs.Channel <= 14) {
				*ppTable = RateSwitchTable11BG;
				*pTableSize = RateSwitchTable11BG[0];
				*pInitTxRateIdx = RateSwitchTable11BG[1];

			} else {
				*ppTable = RateSwitchTable11G;
				*pTableSize = RateSwitchTable11G[0];
				*pInitTxRateIdx = RateSwitchTable11G[1];

			}
			break;
		}
		/*if ((pAd->StaActive.SupRateLen + pAd->StaActive.ExtRateLen == 12) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0xff) && */
		/*      ((pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0x00) || (pAd->Antenna.field.TxPath == 1))) */
		if (((pEntry->RateLen == 12) || (pAd->OpMode == OPMODE_STA)) && (pEntry->HTCapability.MCSSet[0] == 0xff) && ((pEntry->HTCapability.MCSSet[1] == 0x00) || (pAd->CommonCfg.TxStream == 1))) {	/* 11BGN 1S AP */
			*ppTable = RateSwitchTable11BGN1S;
			*pTableSize = RateSwitchTable11BGN1S[0];
			*pInitTxRateIdx = RateSwitchTable11BGN1S[1];

			break;
		}
		/*else if ((pAd->StaActive.SupRateLen + pAd->StaActive.ExtRateLen == 12) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0xff) && */
		/*      (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0xff) && (pAd->Antenna.field.TxPath == 2)) */
		if (((pEntry->RateLen == 12) || (pAd->OpMode == OPMODE_STA)) && (pEntry->HTCapability.MCSSet[0] == 0xff) && (pEntry->HTCapability.MCSSet[1] == 0xff) && (pAd->CommonCfg.TxStream == 2)) {	/* 11BGN 2S AP */
			if (pAd->LatchRfRegs.Channel <= 14) {
				*ppTable = RateSwitchTable11BGN2S;
				*pTableSize = RateSwitchTable11BGN2S[0];
				*pInitTxRateIdx = RateSwitchTable11BGN2S[1];

			} else {
				*ppTable = RateSwitchTable11BGN2SForABand;
				*pTableSize = RateSwitchTable11BGN2SForABand[0];
				*pInitTxRateIdx =
				    RateSwitchTable11BGN2SForABand[1];

			}
			break;
		}
		/*else if ((pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0xff) && ((pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0x00) || (pAd->Antenna.field.TxPath == 1))) */
		if ((pEntry->HTCapability.MCSSet[0] == 0xff) && ((pEntry->HTCapability.MCSSet[1] == 0x00) || (pAd->CommonCfg.TxStream == 1))) {	/* 11N 1S AP */
			*ppTable = RateSwitchTable11N1S;
			*pTableSize = RateSwitchTable11N1S[0];
			*pInitTxRateIdx = RateSwitchTable11N1S[1];

			break;
		}
		/*else if ((pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0xff) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0xff) && (pAd->Antenna.field.TxPath == 2)) */
		if ((pEntry->HTCapability.MCSSet[0] == 0xff) && (pEntry->HTCapability.MCSSet[1] == 0xff) && (pAd->CommonCfg.TxStream == 2)) {	/* 11N 2S AP */
			if (pAd->LatchRfRegs.Channel <= 14) {
				*ppTable = RateSwitchTable11N2S;
				*pTableSize = RateSwitchTable11N2S[0];
				*pInitTxRateIdx = RateSwitchTable11N2S[1];
			} else {
				*ppTable = RateSwitchTable11N2SForABand;
				*pTableSize = RateSwitchTable11N2SForABand[0];
				*pInitTxRateIdx =
				    RateSwitchTable11N2SForABand[1];
			}

			break;
		}
		/*else if ((pAd->StaActive.SupRateLen == 4) && (pAd->StaActive.ExtRateLen == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0)) */
		if ((pEntry->RateLen == 4 || pAd->CommonCfg.PhyMode == PHY_11B)
		    /*Iverson mark for Adhoc b mode,sta will use rate 54  Mbps when connect with sta b/g/n mode */
		    /* && (pEntry->HTCapability.MCSSet[0] == 0) && (pEntry->HTCapability.MCSSet[1] == 0) */
		    ) {		/* B only AP */
			*ppTable = RateSwitchTable11B;
			*pTableSize = RateSwitchTable11B[0];
			*pInitTxRateIdx = RateSwitchTable11B[1];

			break;
		}
		/*else if ((pAd->StaActive.SupRateLen + pAd->StaActive.ExtRateLen > 8) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0)) */
		if ((pEntry->RateLen > 8)
		    && (pEntry->HTCapability.MCSSet[0] == 0)
		    && (pEntry->HTCapability.MCSSet[1] == 0)
		    ) {		/* B/G  mixed AP */
			*ppTable = RateSwitchTable11BG;
			*pTableSize = RateSwitchTable11BG[0];
			*pInitTxRateIdx = RateSwitchTable11BG[1];

			break;
		}
		/*else if ((pAd->StaActive.SupRateLen + pAd->StaActive.ExtRateLen == 8) && (pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0)) */
		if ((pEntry->RateLen == 8)
		    && (pEntry->HTCapability.MCSSet[0] == 0)
		    && (pEntry->HTCapability.MCSSet[1] == 0)
		    ) {		/* G only AP */
			*ppTable = RateSwitchTable11G;
			*pTableSize = RateSwitchTable11G[0];
			*pInitTxRateIdx = RateSwitchTable11G[1];

			break;
		}

		{
			/*else if ((pAd->StaActive.SupportedPhyInfo.MCSSet[0] == 0) && (pAd->StaActive.SupportedPhyInfo.MCSSet[1] == 0)) */
			if ((pEntry->HTCapability.MCSSet[0] == 0) && (pEntry->HTCapability.MCSSet[1] == 0)) {	/* Legacy mode */
				if (pAd->CommonCfg.MaxTxRate <= RATE_11) {
					*ppTable = RateSwitchTable11B;
					*pTableSize = RateSwitchTable11B[0];
					*pInitTxRateIdx = RateSwitchTable11B[1];
				} else if ((pAd->CommonCfg.MaxTxRate > RATE_11)
					   && (pAd->CommonCfg.MinTxRate >
					       RATE_11)) {
					*ppTable = RateSwitchTable11G;
					*pTableSize = RateSwitchTable11G[0];
					*pInitTxRateIdx = RateSwitchTable11G[1];

				} else {
					*ppTable = RateSwitchTable11BG;
					*pTableSize = RateSwitchTable11BG[0];
					*pInitTxRateIdx =
					    RateSwitchTable11BG[1];
				}
				break;
			}
			if (pAd->LatchRfRegs.Channel <= 14) {
				if (pAd->CommonCfg.TxStream == 1) {
					*ppTable = RateSwitchTable11N1S;
					*pTableSize = RateSwitchTable11N1S[0];
					*pInitTxRateIdx =
					    RateSwitchTable11N1S[1];
					DBGPRINT_RAW(RT_DEBUG_ERROR,
						     ("DRS: unkown mode,default use 11N 1S AP \n"));
				} else {
					*ppTable = RateSwitchTable11N2S;
					*pTableSize = RateSwitchTable11N2S[0];
					*pInitTxRateIdx =
					    RateSwitchTable11N2S[1];
					DBGPRINT_RAW(RT_DEBUG_ERROR,
						     ("DRS: unkown mode,default use 11N 2S AP \n"));
				}
			} else {
				if (pAd->CommonCfg.TxStream == 1) {
					*ppTable = RateSwitchTable11N1S;
					*pTableSize = RateSwitchTable11N1S[0];
					*pInitTxRateIdx =
					    RateSwitchTable11N1S[1];
					DBGPRINT_RAW(RT_DEBUG_ERROR,
						     ("DRS: unkown mode,default use 11N 1S AP \n"));
				} else {
					*ppTable = RateSwitchTable11N2SForABand;
					*pTableSize =
					    RateSwitchTable11N2SForABand[0];
					*pInitTxRateIdx =
					    RateSwitchTable11N2SForABand[1];
					DBGPRINT_RAW(RT_DEBUG_ERROR,
						     ("DRS: unkown mode,default use 11N 2S AP \n"));
				}
			}
			DBGPRINT_RAW(RT_DEBUG_ERROR,
				     ("DRS: unkown mode (SupRateLen=%d, ExtRateLen=%d, MCSSet[0]=0x%x, MCSSet[1]=0x%x)\n",
				      pAd->StaActive.SupRateLen,
				      pAd->StaActive.ExtRateLen,
				      pAd->StaActive.SupportedPhyInfo.MCSSet[0],
				      pAd->StaActive.SupportedPhyInfo.
				      MCSSet[1]));
		}
	} while (FALSE);
}

void STAMlmePeriodicExec(struct rt_rtmp_adapter *pAd)
{
	unsigned long TxTotalCnt;
	int i;

	/*
	   We return here in ATE mode, because the statistics
	   that ATE need are not collected via this routine.
	 */
#if defined(RT305x)||defined(RT3070)
	/* request by Gary, if Rssi0 > -42, BBP 82 need to be changed from 0x62 to 0x42, , bbp 67 need to be changed from 0x20 to 0x18 */
	if (!pAd->CommonCfg.HighPowerPatchDisabled) {
#ifdef RT3070
		if ((IS_RT3070(pAd) && ((pAd->MACVersion & 0xffff) < 0x0201)))
#endif /* RT3070 // */
		{
			if ((pAd->StaCfg.RssiSample.AvgRssi0 != 0)
			    && (pAd->StaCfg.RssiSample.AvgRssi0 >
				(pAd->BbpRssiToDbmDelta - 35))) {
				RT30xxWriteRFRegister(pAd, RF_R27, 0x20);
			} else {
				RT30xxWriteRFRegister(pAd, RF_R27, 0x23);
			}
		}
	}
#endif
#ifdef PCIE_PS_SUPPORT
/* don't perform idle-power-save mechanism within 3 min after driver initialization. */
/* This can make rebooter test more robust */
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)) {
		if ((pAd->OpMode == OPMODE_STA) && (IDLE_ON(pAd))
		    && (pAd->Mlme.SyncMachine.CurrState == SYNC_IDLE)
		    && (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE)
		    && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))) {
			if (IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd)) {
				if (pAd->StaCfg.PSControl.field.EnableNewPS ==
				    TRUE) {
					DBGPRINT(RT_DEBUG_TRACE,
						 ("%s\n", __func__));
					RT28xxPciAsicRadioOff(pAd,
							      GUI_IDLE_POWER_SAVE,
							      0);
				} else {
					AsicSendCommandToMcu(pAd, 0x30,
							     PowerSafeCID, 0xff,
							     0x2);
					/* Wait command success */
					AsicCheckCommanOk(pAd, PowerSafeCID);
					RTMP_SET_FLAG(pAd,
						      fRTMP_ADAPTER_IDLE_RADIO_OFF);
					DBGPRINT(RT_DEBUG_TRACE,
						 ("PSM - rt30xx Issue Sleep command)\n"));
				}
			} else if (pAd->Mlme.OneSecPeriodicRound > 180) {
				if (pAd->StaCfg.PSControl.field.EnableNewPS ==
				    TRUE) {
					DBGPRINT(RT_DEBUG_TRACE,
						 ("%s\n", __func__));
					RT28xxPciAsicRadioOff(pAd,
							      GUI_IDLE_POWER_SAVE,
							      0);
				} else {
					AsicSendCommandToMcu(pAd, 0x30,
							     PowerSafeCID, 0xff,
							     0x02);
					/* Wait command success */
					AsicCheckCommanOk(pAd, PowerSafeCID);
					RTMP_SET_FLAG(pAd,
						      fRTMP_ADAPTER_IDLE_RADIO_OFF);
					DBGPRINT(RT_DEBUG_TRACE,
						 ("PSM -  rt28xx Issue Sleep command)\n"));
				}
			}
		} else {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("STAMlmePeriodicExec MMCHK - CommonCfg.Ssid[%d]=%c%c%c%c... MlmeAux.Ssid[%d]=%c%c%c%c...\n",
				  pAd->CommonCfg.SsidLen,
				  pAd->CommonCfg.Ssid[0],
				  pAd->CommonCfg.Ssid[1],
				  pAd->CommonCfg.Ssid[2],
				  pAd->CommonCfg.Ssid[3], pAd->MlmeAux.SsidLen,
				  pAd->MlmeAux.Ssid[0], pAd->MlmeAux.Ssid[1],
				  pAd->MlmeAux.Ssid[2], pAd->MlmeAux.Ssid[3]));
		}
	}
#endif /* PCIE_PS_SUPPORT // */

	if (pAd->StaCfg.WpaSupplicantUP == WPA_SUPPLICANT_DISABLE) {
		/* WPA MIC error should block association attempt for 60 seconds */
		if (pAd->StaCfg.bBlockAssoc &&
		    RTMP_TIME_AFTER(pAd->Mlme.Now32,
				    pAd->StaCfg.LastMicErrorTime +
				    (60 * OS_HZ)))
			pAd->StaCfg.bBlockAssoc = FALSE;
	}

	if ((pAd->PreMediaState != pAd->IndicateMediaState)
	    && (pAd->CommonCfg.bWirelessEvent)) {
		if (pAd->IndicateMediaState == NdisMediaStateConnected) {
			RTMPSendWirelessEvent(pAd, IW_STA_LINKUP_EVENT_FLAG,
					      pAd->MacTab.Content[BSSID_WCID].
					      Addr, BSS0, 0);
		}
		pAd->PreMediaState = pAd->IndicateMediaState;
	}

	if (pAd->CommonCfg.PSPXlink && ADHOC_ON(pAd)) {
	} else {
		AsicStaBbpTuning(pAd);
	}

	TxTotalCnt = pAd->RalinkCounters.OneSecTxNoRetryOkCount +
	    pAd->RalinkCounters.OneSecTxRetryOkCount +
	    pAd->RalinkCounters.OneSecTxFailCount;

	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)) {
		/* update channel quality for Roaming and UI LinkQuality display */
		MlmeCalculateChannelQuality(pAd, NULL, pAd->Mlme.Now32);
	}
	/* must be AFTER MlmeDynamicTxRateSwitching() because it needs to know if */
	/* Radio is currently in noisy environment */
	if (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
		AsicAdjustTxPower(pAd);

	if (INFRA_ON(pAd)) {

		/* Is PSM bit consistent with user power management policy? */
		/* This is the only place that will set PSM bit ON. */
		if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
			MlmeCheckPsmChange(pAd, pAd->Mlme.Now32);

		pAd->RalinkCounters.LastOneSecTotalTxCount = TxTotalCnt;

		if ((RTMP_TIME_AFTER
		     (pAd->Mlme.Now32,
		      pAd->StaCfg.LastBeaconRxTime + (1 * OS_HZ)))
		    &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
		    &&
		    (((TxTotalCnt + pAd->RalinkCounters.OneSecRxOkCnt) <
		      600))) {
			RTMPSetAGCInitValue(pAd, BW_20);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("MMCHK - No BEACON. restore R66 to the low bound(%d) \n",
				  (0x2E + GET_LNA_GAIN(pAd))));
		}
		/*if ((pAd->RalinkCounters.OneSecTxNoRetryOkCount == 0) && */
		/*    (pAd->RalinkCounters.OneSecTxRetryOkCount == 0)) */
		{
			if (pAd->CommonCfg.bAPSDCapable
			    && pAd->CommonCfg.APEdcaParm.bAPSDCapable) {
				/* When APSD is enabled, the period changes as 20 sec */
				if ((pAd->Mlme.OneSecPeriodicRound % 20) == 8)
					RTMPSendNullFrame(pAd,
							  pAd->CommonCfg.TxRate,
							  TRUE);
			} else {
				/* Send out a NULL frame every 10 sec to inform AP that STA is still alive (Avoid being age out) */
				if ((pAd->Mlme.OneSecPeriodicRound % 10) == 8) {
					if (pAd->CommonCfg.bWmmCapable)
						RTMPSendNullFrame(pAd,
								  pAd->
								  CommonCfg.
								  TxRate, TRUE);
					else
						RTMPSendNullFrame(pAd,
								  pAd->
								  CommonCfg.
								  TxRate,
								  FALSE);
				}
			}
		}

		if (CQI_IS_DEAD(pAd->Mlme.ChannelQuality)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("MMCHK - No BEACON. Dead CQI. Auto Recovery attempt #%ld\n",
				  pAd->RalinkCounters.BadCQIAutoRecoveryCount));

			/* Lost AP, send disconnect & link down event */
			LinkDown(pAd, FALSE);

			RtmpOSWrielessEventSend(pAd, SIOCGIWAP, -1, NULL, NULL,
						0);

			/* RTMPPatchMacBbpBug(pAd); */
			MlmeAutoReconnectLastSSID(pAd);
		} else if (CQI_IS_BAD(pAd->Mlme.ChannelQuality)) {
			pAd->RalinkCounters.BadCQIAutoRecoveryCount++;
			DBGPRINT(RT_DEBUG_TRACE,
				 ("MMCHK - Bad CQI. Auto Recovery attempt #%ld\n",
				  pAd->RalinkCounters.BadCQIAutoRecoveryCount));
			MlmeAutoReconnectLastSSID(pAd);
		}

		if (pAd->StaCfg.bAutoRoaming) {
			BOOLEAN rv = FALSE;
			char dBmToRoam = pAd->StaCfg.dBmToRoam;
			char MaxRssi = RTMPMaxRssi(pAd,
						   pAd->StaCfg.RssiSample.
						   LastRssi0,
						   pAd->StaCfg.RssiSample.
						   LastRssi1,
						   pAd->StaCfg.RssiSample.
						   LastRssi2);

			/* Scanning, ignore Roaming */
			if (!RTMP_TEST_FLAG
			    (pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)
			    && (pAd->Mlme.SyncMachine.CurrState == SYNC_IDLE)
			    && (MaxRssi <= dBmToRoam)) {
				DBGPRINT(RT_DEBUG_TRACE,
					 ("Rssi=%d, dBmToRoam=%d\n", MaxRssi,
					  (char)dBmToRoam));

				/* Add auto seamless roaming */
				if (rv == FALSE)
					rv = MlmeCheckForFastRoaming(pAd);

				if (rv == FALSE) {
					if ((pAd->StaCfg.LastScanTime +
					     10 * OS_HZ) < pAd->Mlme.Now32) {
						DBGPRINT(RT_DEBUG_TRACE,
							 ("MMCHK - Roaming, No eligable entry, try new scan!\n"));
						pAd->StaCfg.ScanCnt = 2;
						pAd->StaCfg.LastScanTime =
						    pAd->Mlme.Now32;
						MlmeAutoScan(pAd);
					}
				}
			}
		}
	} else if (ADHOC_ON(pAd)) {
		/* If all peers leave, and this STA becomes the last one in this IBSS, then change MediaState */
		/* to DISCONNECTED. But still holding this IBSS (i.e. sending BEACON) so that other STAs can */
		/* join later. */
		if (RTMP_TIME_AFTER
		    (pAd->Mlme.Now32,
		     pAd->StaCfg.LastBeaconRxTime + ADHOC_BEACON_LOST_TIME)
		    && OPSTATUS_TEST_FLAG(pAd,
					  fOP_STATUS_MEDIA_STATE_CONNECTED)) {
			struct rt_mlme_start_req StartReq;

			DBGPRINT(RT_DEBUG_TRACE,
				 ("MMCHK - excessive BEACON lost, last STA in this IBSS, MediaState=Disconnected\n"));
			LinkDown(pAd, FALSE);

			StartParmFill(pAd, &StartReq,
				      (char *) pAd->MlmeAux.Ssid,
				      pAd->MlmeAux.SsidLen);
			MlmeEnqueue(pAd, SYNC_STATE_MACHINE, MT2_MLME_START_REQ,
				    sizeof(struct rt_mlme_start_req), &StartReq);
			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_START;
		}

		for (i = 1; i < MAX_LEN_OF_MAC_TABLE; i++) {
			struct rt_mac_table_entry *pEntry = &pAd->MacTab.Content[i];

			if (pEntry->ValidAsCLI == FALSE)
				continue;

			if (RTMP_TIME_AFTER
			    (pAd->Mlme.Now32,
			     pEntry->LastBeaconRxTime + ADHOC_BEACON_LOST_TIME))
				MacTableDeleteEntry(pAd, pEntry->Aid,
						    pEntry->Addr);
		}
	} else			/* no INFRA nor ADHOC connection */
	{

		if (pAd->StaCfg.bScanReqIsFromWebUI &&
		    RTMP_TIME_BEFORE(pAd->Mlme.Now32,
				     pAd->StaCfg.LastScanTime + (30 * OS_HZ)))
			goto SKIP_AUTO_SCAN_CONN;
		else
			pAd->StaCfg.bScanReqIsFromWebUI = FALSE;

		if ((pAd->StaCfg.bAutoReconnect == TRUE)
		    && RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_START_UP)
		    &&
		    (MlmeValidateSSID
		     (pAd->MlmeAux.AutoReconnectSsid,
		      pAd->MlmeAux.AutoReconnectSsidLen) == TRUE)) {
			if ((pAd->ScanTab.BssNr == 0)
			    && (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE)) {
				struct rt_mlme_scan_req ScanReq;

				if (RTMP_TIME_AFTER
				    (pAd->Mlme.Now32,
				     pAd->StaCfg.LastScanTime + (10 * OS_HZ))) {
					DBGPRINT(RT_DEBUG_TRACE,
						 ("STAMlmePeriodicExec():CNTL - ScanTab.BssNr==0, start a new ACTIVE scan SSID[%s]\n",
						  pAd->MlmeAux.
						  AutoReconnectSsid));
					ScanParmFill(pAd, &ScanReq,
						     (char *)pAd->MlmeAux.
						     AutoReconnectSsid,
						     pAd->MlmeAux.
						     AutoReconnectSsidLen,
						     BSS_ANY, SCAN_ACTIVE);
					MlmeEnqueue(pAd, SYNC_STATE_MACHINE,
						    MT2_MLME_SCAN_REQ,
						    sizeof
						    (struct rt_mlme_scan_req),
						    &ScanReq);
					pAd->Mlme.CntlMachine.CurrState =
					    CNTL_WAIT_OID_LIST_SCAN;
					/* Reset Missed scan number */
					pAd->StaCfg.LastScanTime =
					    pAd->Mlme.Now32;
				} else if (pAd->StaCfg.BssType == BSS_ADHOC)	/* Quit the forever scan when in a very clean room */
					MlmeAutoReconnectLastSSID(pAd);
			} else if (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE) {
				if ((pAd->Mlme.OneSecPeriodicRound % 7) == 0) {
					MlmeAutoScan(pAd);
					pAd->StaCfg.LastScanTime =
					    pAd->Mlme.Now32;
				} else {
					MlmeAutoReconnectLastSSID(pAd);
				}
			}
		}
	}

SKIP_AUTO_SCAN_CONN:

	if ((pAd->MacTab.Content[BSSID_WCID].TXBAbitmap != 0)
	    && (pAd->MacTab.fAnyBASession == FALSE)) {
		pAd->MacTab.fAnyBASession = TRUE;
		AsicUpdateProtect(pAd, HT_FORCERTSCTS, ALLN_SETPROTECT, FALSE,
				  FALSE);
	} else if ((pAd->MacTab.Content[BSSID_WCID].TXBAbitmap == 0)
		   && (pAd->MacTab.fAnyBASession == TRUE)) {
		pAd->MacTab.fAnyBASession = FALSE;
		AsicUpdateProtect(pAd,
				  pAd->MlmeAux.AddHtInfo.AddHtInfo2.
				  OperaionMode, ALLN_SETPROTECT, FALSE, FALSE);
	}

	return;
}

/* Link down report */
void LinkDownExec(void *SystemSpecific1,
		  void *FunctionContext,
		  void *SystemSpecific2, void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;

	if (pAd != NULL) {
		struct rt_mlme_disassoc_req DisassocReq;

		if ((pAd->StaCfg.PortSecured == WPA_802_1X_PORT_NOT_SECURED) &&
		    (INFRA_ON(pAd))) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("LinkDownExec(): disassociate with current AP...\n"));
			DisassocParmFill(pAd, &DisassocReq,
					 pAd->CommonCfg.Bssid,
					 REASON_DISASSOC_STA_LEAVING);
			MlmeEnqueue(pAd, ASSOC_STATE_MACHINE,
				    MT2_MLME_DISASSOC_REQ,
				    sizeof(struct rt_mlme_disassoc_req),
				    &DisassocReq);
			pAd->Mlme.CntlMachine.CurrState = CNTL_WAIT_DISASSOC;

			pAd->IndicateMediaState = NdisMediaStateDisconnected;
			RTMP_IndicateMediaState(pAd);
			pAd->ExtraInfo = GENERAL_LINK_DOWN;
		}
	}
}

/* IRQL = DISPATCH_LEVEL */
void MlmeAutoScan(struct rt_rtmp_adapter *pAd)
{
	/* check CntlMachine.CurrState to avoid collision with NDIS SetOID request */
	if (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE) {
		DBGPRINT(RT_DEBUG_TRACE, ("MMCHK - Driver auto scan\n"));
		MlmeEnqueue(pAd,
			    MLME_CNTL_STATE_MACHINE,
			    OID_802_11_BSSID_LIST_SCAN,
			    pAd->MlmeAux.AutoReconnectSsidLen,
			    pAd->MlmeAux.AutoReconnectSsid);
		RTMP_MLME_HANDLER(pAd);
	}
}

/* IRQL = DISPATCH_LEVEL */
void MlmeAutoReconnectLastSSID(struct rt_rtmp_adapter *pAd)
{
	if (pAd->StaCfg.bAutoConnectByBssid) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Driver auto reconnect to last OID_802_11_BSSID setting - %02X:%02X:%02X:%02X:%02X:%02X\n",
			  pAd->MlmeAux.Bssid[0], pAd->MlmeAux.Bssid[1],
			  pAd->MlmeAux.Bssid[2], pAd->MlmeAux.Bssid[3],
			  pAd->MlmeAux.Bssid[4], pAd->MlmeAux.Bssid[5]));

		pAd->MlmeAux.Channel = pAd->CommonCfg.Channel;
		MlmeEnqueue(pAd,
			    MLME_CNTL_STATE_MACHINE,
			    OID_802_11_BSSID, MAC_ADDR_LEN, pAd->MlmeAux.Bssid);

		pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;

		RTMP_MLME_HANDLER(pAd);
	}
	/* check CntlMachine.CurrState to avoid collision with NDIS SetOID request */
	else if ((pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE) &&
		 (MlmeValidateSSID
		  (pAd->MlmeAux.AutoReconnectSsid,
		   pAd->MlmeAux.AutoReconnectSsidLen) == TRUE)) {
		struct rt_ndis_802_11_ssid OidSsid;
		OidSsid.SsidLength = pAd->MlmeAux.AutoReconnectSsidLen;
		NdisMoveMemory(OidSsid.Ssid, pAd->MlmeAux.AutoReconnectSsid,
			       pAd->MlmeAux.AutoReconnectSsidLen);

		DBGPRINT(RT_DEBUG_TRACE,
			 ("Driver auto reconnect to last OID_802_11_SSID setting - %s, len - %d\n",
			  pAd->MlmeAux.AutoReconnectSsid,
			  pAd->MlmeAux.AutoReconnectSsidLen));
		MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, OID_802_11_SSID,
			    sizeof(struct rt_ndis_802_11_ssid), &OidSsid);
		RTMP_MLME_HANDLER(pAd);
	}
}

/*
	==========================================================================
	Description:
		This routine checks if there're other APs out there capable for
		roaming. Caller should call this routine only when Link up in INFRA mode
		and channel quality is below CQI_GOOD_THRESHOLD.

	IRQL = DISPATCH_LEVEL

	Output:
	==========================================================================
 */
void MlmeCheckForRoaming(struct rt_rtmp_adapter *pAd, unsigned long Now32)
{
	u16 i;
	struct rt_bss_table *pRoamTab = &pAd->MlmeAux.RoamTab;
	struct rt_bss_entry *pBss;

	DBGPRINT(RT_DEBUG_TRACE, ("==> MlmeCheckForRoaming\n"));
	/* put all roaming candidates into RoamTab, and sort in RSSI order */
	BssTableInit(pRoamTab);
	for (i = 0; i < pAd->ScanTab.BssNr; i++) {
		pBss = &pAd->ScanTab.BssEntry[i];

		if ((pBss->LastBeaconRxTime + pAd->StaCfg.BeaconLostTime) <
		    Now32)
			continue;	/* AP disappear */
		if (pBss->Rssi <= RSSI_THRESHOLD_FOR_ROAMING)
			continue;	/* RSSI too weak. forget it. */
		if (MAC_ADDR_EQUAL(pBss->Bssid, pAd->CommonCfg.Bssid))
			continue;	/* skip current AP */
		if (pBss->Rssi <
		    (pAd->StaCfg.RssiSample.LastRssi0 + RSSI_DELTA))
			continue;	/* only AP with stronger RSSI is eligible for roaming */

		/* AP passing all above rules is put into roaming candidate table */
		NdisMoveMemory(&pRoamTab->BssEntry[pRoamTab->BssNr], pBss,
			       sizeof(struct rt_bss_entry));
		pRoamTab->BssNr += 1;
	}

	if (pRoamTab->BssNr > 0) {
		/* check CntlMachine.CurrState to avoid collision with NDIS SetOID request */
		if (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE) {
			pAd->RalinkCounters.PoorCQIRoamingCount++;
			DBGPRINT(RT_DEBUG_TRACE,
				 ("MMCHK - Roaming attempt #%ld\n",
				  pAd->RalinkCounters.PoorCQIRoamingCount));
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE,
				    MT2_MLME_ROAMING_REQ, 0, NULL);
			RTMP_MLME_HANDLER(pAd);
		}
	}
	DBGPRINT(RT_DEBUG_TRACE,
		 ("<== MlmeCheckForRoaming(# of candidate= %d)\n",
		  pRoamTab->BssNr));
}

/*
	==========================================================================
	Description:
		This routine checks if there're other APs out there capable for
		roaming. Caller should call this routine only when link up in INFRA mode
		and channel quality is below CQI_GOOD_THRESHOLD.

	IRQL = DISPATCH_LEVEL

	Output:
	==========================================================================
 */
BOOLEAN MlmeCheckForFastRoaming(struct rt_rtmp_adapter *pAd)
{
	u16 i;
	struct rt_bss_table *pRoamTab = &pAd->MlmeAux.RoamTab;
	struct rt_bss_entry *pBss;

	DBGPRINT(RT_DEBUG_TRACE, ("==> MlmeCheckForFastRoaming\n"));
	/* put all roaming candidates into RoamTab, and sort in RSSI order */
	BssTableInit(pRoamTab);
	for (i = 0; i < pAd->ScanTab.BssNr; i++) {
		pBss = &pAd->ScanTab.BssEntry[i];

		if ((pBss->Rssi <= -50)
		    && (pBss->Channel == pAd->CommonCfg.Channel))
			continue;	/* RSSI too weak. forget it. */
		if (MAC_ADDR_EQUAL(pBss->Bssid, pAd->CommonCfg.Bssid))
			continue;	/* skip current AP */
		if (!SSID_EQUAL
		    (pBss->Ssid, pBss->SsidLen, pAd->CommonCfg.Ssid,
		     pAd->CommonCfg.SsidLen))
			continue;	/* skip different SSID */
		if (pBss->Rssi <
		    (RTMPMaxRssi
		     (pAd, pAd->StaCfg.RssiSample.LastRssi0,
		      pAd->StaCfg.RssiSample.LastRssi1,
		      pAd->StaCfg.RssiSample.LastRssi2) + RSSI_DELTA))
			continue;	/* skip AP without better RSSI */

		DBGPRINT(RT_DEBUG_TRACE,
			 ("LastRssi0 = %d, pBss->Rssi = %d\n",
			  RTMPMaxRssi(pAd, pAd->StaCfg.RssiSample.LastRssi0,
				      pAd->StaCfg.RssiSample.LastRssi1,
				      pAd->StaCfg.RssiSample.LastRssi2),
			  pBss->Rssi));
		/* AP passing all above rules is put into roaming candidate table */
		NdisMoveMemory(&pRoamTab->BssEntry[pRoamTab->BssNr], pBss,
			       sizeof(struct rt_bss_entry));
		pRoamTab->BssNr += 1;
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 ("<== MlmeCheckForFastRoaming (BssNr=%d)\n", pRoamTab->BssNr));
	if (pRoamTab->BssNr > 0) {
		/* check CntlMachine.CurrState to avoid collision with NDIS SetOID request */
		if (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE) {
			pAd->RalinkCounters.PoorCQIRoamingCount++;
			DBGPRINT(RT_DEBUG_TRACE,
				 ("MMCHK - Roaming attempt #%ld\n",
				  pAd->RalinkCounters.PoorCQIRoamingCount));
			MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE,
				    MT2_MLME_ROAMING_REQ, 0, NULL);
			RTMP_MLME_HANDLER(pAd);
			return TRUE;
		}
	}

	return FALSE;
}

void MlmeSetTxRate(struct rt_rtmp_adapter *pAd,
		   struct rt_mac_table_entry *pEntry, struct rt_rtmp_tx_rate_switch * pTxRate)
{
	u8 MaxMode = MODE_OFDM;

	MaxMode = MODE_HTGREENFIELD;

	if (pTxRate->STBC && (pAd->StaCfg.MaxHTPhyMode.field.STBC)
	    && (pAd->Antenna.field.TxPath == 2))
		pAd->StaCfg.HTPhyMode.field.STBC = STBC_USE;
	else
		pAd->StaCfg.HTPhyMode.field.STBC = STBC_NONE;

	if (pTxRate->CurrMCS < MCS_AUTO)
		pAd->StaCfg.HTPhyMode.field.MCS = pTxRate->CurrMCS;

	if (pAd->StaCfg.HTPhyMode.field.MCS > 7)
		pAd->StaCfg.HTPhyMode.field.STBC = STBC_NONE;

	if (ADHOC_ON(pAd)) {
		/* If peer adhoc is b-only mode, we can't send 11g rate. */
		pAd->StaCfg.HTPhyMode.field.ShortGI = GI_800;
		pEntry->HTPhyMode.field.STBC = STBC_NONE;

		/* */
		/* For Adhoc MODE_CCK, driver will use AdhocBOnlyJoined flag to roll back to B only if necessary */
		/* */
		pEntry->HTPhyMode.field.MODE = pTxRate->Mode;
		pEntry->HTPhyMode.field.ShortGI =
		    pAd->StaCfg.HTPhyMode.field.ShortGI;
		pEntry->HTPhyMode.field.MCS = pAd->StaCfg.HTPhyMode.field.MCS;

		/* Patch speed error in status page */
		pAd->StaCfg.HTPhyMode.field.MODE = pEntry->HTPhyMode.field.MODE;
	} else {
		if (pTxRate->Mode <= MaxMode)
			pAd->StaCfg.HTPhyMode.field.MODE = pTxRate->Mode;

		if (pTxRate->ShortGI
		    && (pAd->StaCfg.MaxHTPhyMode.field.ShortGI))
			pAd->StaCfg.HTPhyMode.field.ShortGI = GI_400;
		else
			pAd->StaCfg.HTPhyMode.field.ShortGI = GI_800;

		/* Reexam each bandwidth's SGI support. */
		if (pAd->StaCfg.HTPhyMode.field.ShortGI == GI_400) {
			if ((pEntry->HTPhyMode.field.BW == BW_20)
			    &&
			    (!CLIENT_STATUS_TEST_FLAG
			     (pEntry, fCLIENT_STATUS_SGI20_CAPABLE)))
				pAd->StaCfg.HTPhyMode.field.ShortGI = GI_800;
			if ((pEntry->HTPhyMode.field.BW == BW_40)
			    &&
			    (!CLIENT_STATUS_TEST_FLAG
			     (pEntry, fCLIENT_STATUS_SGI40_CAPABLE)))
				pAd->StaCfg.HTPhyMode.field.ShortGI = GI_800;
		}
		/* Turn RTS/CTS rate to 6Mbps. */
		if ((pEntry->HTPhyMode.field.MCS == 0)
		    && (pAd->StaCfg.HTPhyMode.field.MCS != 0)) {
			pEntry->HTPhyMode.field.MCS =
			    pAd->StaCfg.HTPhyMode.field.MCS;
			if (pAd->MacTab.fAnyBASession) {
				AsicUpdateProtect(pAd, HT_FORCERTSCTS,
						  ALLN_SETPROTECT, TRUE,
						  (BOOLEAN) pAd->MlmeAux.
						  AddHtInfo.AddHtInfo2.
						  NonGfPresent);
			} else {
				AsicUpdateProtect(pAd,
						  pAd->MlmeAux.AddHtInfo.
						  AddHtInfo2.OperaionMode,
						  ALLN_SETPROTECT, TRUE,
						  (BOOLEAN) pAd->MlmeAux.
						  AddHtInfo.AddHtInfo2.
						  NonGfPresent);
			}
		} else if ((pEntry->HTPhyMode.field.MCS == 8)
			   && (pAd->StaCfg.HTPhyMode.field.MCS != 8)) {
			pEntry->HTPhyMode.field.MCS =
			    pAd->StaCfg.HTPhyMode.field.MCS;
			if (pAd->MacTab.fAnyBASession) {
				AsicUpdateProtect(pAd, HT_FORCERTSCTS,
						  ALLN_SETPROTECT, TRUE,
						  (BOOLEAN) pAd->MlmeAux.
						  AddHtInfo.AddHtInfo2.
						  NonGfPresent);
			} else {
				AsicUpdateProtect(pAd,
						  pAd->MlmeAux.AddHtInfo.
						  AddHtInfo2.OperaionMode,
						  ALLN_SETPROTECT, TRUE,
						  (BOOLEAN) pAd->MlmeAux.
						  AddHtInfo.AddHtInfo2.
						  NonGfPresent);
			}
		} else if ((pEntry->HTPhyMode.field.MCS != 0)
			   && (pAd->StaCfg.HTPhyMode.field.MCS == 0)) {
			AsicUpdateProtect(pAd, HT_RTSCTS_6M, ALLN_SETPROTECT,
					  TRUE,
					  (BOOLEAN) pAd->MlmeAux.AddHtInfo.
					  AddHtInfo2.NonGfPresent);

		} else if ((pEntry->HTPhyMode.field.MCS != 8)
			   && (pAd->StaCfg.HTPhyMode.field.MCS == 8)) {
			AsicUpdateProtect(pAd, HT_RTSCTS_6M, ALLN_SETPROTECT,
					  TRUE,
					  (BOOLEAN) pAd->MlmeAux.AddHtInfo.
					  AddHtInfo2.NonGfPresent);
		}

		pEntry->HTPhyMode.field.STBC = pAd->StaCfg.HTPhyMode.field.STBC;
		pEntry->HTPhyMode.field.ShortGI =
		    pAd->StaCfg.HTPhyMode.field.ShortGI;
		pEntry->HTPhyMode.field.MCS = pAd->StaCfg.HTPhyMode.field.MCS;
		pEntry->HTPhyMode.field.MODE = pAd->StaCfg.HTPhyMode.field.MODE;
		if ((pAd->StaCfg.MaxHTPhyMode.field.MODE == MODE_HTGREENFIELD)
		    && pAd->WIFItestbed.bGreenField)
			pEntry->HTPhyMode.field.MODE = MODE_HTGREENFIELD;
	}

	pAd->LastTxRate = (u16)(pEntry->HTPhyMode.word);
}

/*
	==========================================================================
	Description:
		This routine calculates the acumulated TxPER of eaxh TxRate. And
		according to the calculation result, change CommonCfg.TxRate which
		is the stable TX Rate we expect the Radio situation could sustained.

		CommonCfg.TxRate will change dynamically within {RATE_1/RATE_6, MaxTxRate}
	Output:
		CommonCfg.TxRate -

	IRQL = DISPATCH_LEVEL

	NOTE:
		call this routine every second
	==========================================================================
 */
void MlmeDynamicTxRateSwitching(struct rt_rtmp_adapter *pAd)
{
	u8 UpRateIdx = 0, DownRateIdx = 0, CurrRateIdx;
	unsigned long i, AccuTxTotalCnt = 0, TxTotalCnt;
	unsigned long TxErrorRatio = 0;
	BOOLEAN bTxRateChanged = FALSE, bUpgradeQuality = FALSE;
	struct rt_rtmp_tx_rate_switch *pCurrTxRate, *pNextTxRate = NULL;
	u8 *pTable;
	u8 TableSize = 0;
	u8 InitTxRateIdx = 0, TrainUp, TrainDown;
	char Rssi, RssiOffset = 0;
	TX_STA_CNT1_STRUC StaTx1;
	TX_STA_CNT0_STRUC TxStaCnt0;
	unsigned long TxRetransmit = 0, TxSuccess = 0, TxFailCount = 0;
	struct rt_mac_table_entry *pEntry;
	struct rt_rssi_sample *pRssi = &pAd->StaCfg.RssiSample;

	/* */
	/* walk through MAC table, see if need to change AP's TX rate toward each entry */
	/* */
	for (i = 1; i < MAX_LEN_OF_MAC_TABLE; i++) {
		pEntry = &pAd->MacTab.Content[i];

		/* check if this entry need to switch rate automatically */
		if (RTMPCheckEntryEnableAutoRateSwitch(pAd, pEntry) == FALSE)
			continue;

		if ((pAd->MacTab.Size == 1) || (pEntry->ValidAsDls)) {
			Rssi = RTMPMaxRssi(pAd,
					   pRssi->AvgRssi0,
					   pRssi->AvgRssi1, pRssi->AvgRssi2);

			/* Update statistic counter */
			RTMP_IO_READ32(pAd, TX_STA_CNT0, &TxStaCnt0.word);
			RTMP_IO_READ32(pAd, TX_STA_CNT1, &StaTx1.word);
			pAd->bUpdateBcnCntDone = TRUE;
			TxRetransmit = StaTx1.field.TxRetransmit;
			TxSuccess = StaTx1.field.TxSuccess;
			TxFailCount = TxStaCnt0.field.TxFailCount;
			TxTotalCnt = TxRetransmit + TxSuccess + TxFailCount;

			pAd->RalinkCounters.OneSecTxRetryOkCount +=
			    StaTx1.field.TxRetransmit;
			pAd->RalinkCounters.OneSecTxNoRetryOkCount +=
			    StaTx1.field.TxSuccess;
			pAd->RalinkCounters.OneSecTxFailCount +=
			    TxStaCnt0.field.TxFailCount;
			pAd->WlanCounters.TransmittedFragmentCount.u.LowPart +=
			    StaTx1.field.TxSuccess;
			pAd->WlanCounters.RetryCount.u.LowPart +=
			    StaTx1.field.TxRetransmit;
			pAd->WlanCounters.FailedCount.u.LowPart +=
			    TxStaCnt0.field.TxFailCount;

			/* if no traffic in the past 1-sec period, don't change TX rate, */
			/* but clear all bad history. because the bad history may affect the next */
			/* Chariot throughput test */
			AccuTxTotalCnt =
			    pAd->RalinkCounters.OneSecTxNoRetryOkCount +
			    pAd->RalinkCounters.OneSecTxRetryOkCount +
			    pAd->RalinkCounters.OneSecTxFailCount;

			if (TxTotalCnt)
				TxErrorRatio =
				    ((TxRetransmit +
				      TxFailCount) * 100) / TxTotalCnt;
		} else {
			if (INFRA_ON(pAd) && (i == 1))
				Rssi = RTMPMaxRssi(pAd,
						   pRssi->AvgRssi0,
						   pRssi->AvgRssi1,
						   pRssi->AvgRssi2);
			else
				Rssi = RTMPMaxRssi(pAd,
						   pEntry->RssiSample.AvgRssi0,
						   pEntry->RssiSample.AvgRssi1,
						   pEntry->RssiSample.AvgRssi2);

			TxTotalCnt = pEntry->OneSecTxNoRetryOkCount +
			    pEntry->OneSecTxRetryOkCount +
			    pEntry->OneSecTxFailCount;

			if (TxTotalCnt)
				TxErrorRatio =
				    ((pEntry->OneSecTxRetryOkCount +
				      pEntry->OneSecTxFailCount) * 100) /
				    TxTotalCnt;
		}

		if (TxTotalCnt) {
			/*
			   Three AdHoc connections can not work normally if one AdHoc connection is disappeared from a heavy traffic environment generated by ping tool
			   We force to set LongRtyLimit and ShortRtyLimit to 0 to stop retransmitting packet, after a while, resoring original settings
			 */
			if (TxErrorRatio == 100) {
				TX_RTY_CFG_STRUC TxRtyCfg, TxRtyCfgtmp;
				unsigned long Index;
				unsigned long MACValue;

				RTMP_IO_READ32(pAd, TX_RTY_CFG, &TxRtyCfg.word);
				TxRtyCfgtmp.word = TxRtyCfg.word;
				TxRtyCfg.field.LongRtyLimit = 0x0;
				TxRtyCfg.field.ShortRtyLimit = 0x0;
				RTMP_IO_WRITE32(pAd, TX_RTY_CFG, TxRtyCfg.word);

				RTMPusecDelay(1);

				Index = 0;
				MACValue = 0;
				do {
					RTMP_IO_READ32(pAd, TXRXQ_PCNT,
						       &MACValue);
					if ((MACValue & 0xffffff) == 0)
						break;
					Index++;
					RTMPusecDelay(1000);
				} while ((Index < 330)
					 &&
					 (!RTMP_TEST_FLAG
					  (pAd,
					   fRTMP_ADAPTER_HALT_IN_PROGRESS)));

				RTMP_IO_READ32(pAd, TX_RTY_CFG, &TxRtyCfg.word);
				TxRtyCfg.field.LongRtyLimit =
				    TxRtyCfgtmp.field.LongRtyLimit;
				TxRtyCfg.field.ShortRtyLimit =
				    TxRtyCfgtmp.field.ShortRtyLimit;
				RTMP_IO_WRITE32(pAd, TX_RTY_CFG, TxRtyCfg.word);
			}
		}

		CurrRateIdx = pEntry->CurrTxRateIndex;

		MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize,
				      &InitTxRateIdx);

		if (CurrRateIdx >= TableSize) {
			CurrRateIdx = TableSize - 1;
		}
		/* When switch from Fixed rate -> auto rate, the REAL TX rate might be different from pAd->CommonCfg.TxRateIndex. */
		/* So need to sync here. */
		pCurrTxRate =
		    (struct rt_rtmp_tx_rate_switch *) & pTable[(CurrRateIdx + 1) * 5];
		if ((pEntry->HTPhyMode.field.MCS != pCurrTxRate->CurrMCS)
		    /*&& (pAd->StaCfg.bAutoTxRateSwitch == TRUE) */
		    ) {

			/* Need to sync Real Tx rate and our record. */
			/* Then return for next DRS. */
			pCurrTxRate =
			    (struct rt_rtmp_tx_rate_switch *) & pTable[(InitTxRateIdx + 1)
							    * 5];
			pEntry->CurrTxRateIndex = InitTxRateIdx;
			MlmeSetTxRate(pAd, pEntry, pCurrTxRate);

			/* reset all OneSecTx counters */
			RESET_ONE_SEC_TX_CNT(pEntry);
			continue;
		}
		/* decide the next upgrade rate and downgrade rate, if any */
		if ((CurrRateIdx > 0) && (CurrRateIdx < (TableSize - 1))) {
			UpRateIdx = CurrRateIdx + 1;
			DownRateIdx = CurrRateIdx - 1;
		} else if (CurrRateIdx == 0) {
			UpRateIdx = CurrRateIdx + 1;
			DownRateIdx = CurrRateIdx;
		} else if (CurrRateIdx == (TableSize - 1)) {
			UpRateIdx = CurrRateIdx;
			DownRateIdx = CurrRateIdx - 1;
		}

		pCurrTxRate =
		    (struct rt_rtmp_tx_rate_switch *) & pTable[(CurrRateIdx + 1) * 5];

		if ((Rssi > -65) && (pCurrTxRate->Mode >= MODE_HTMIX)) {
			TrainUp =
			    (pCurrTxRate->TrainUp +
			     (pCurrTxRate->TrainUp >> 1));
			TrainDown =
			    (pCurrTxRate->TrainDown +
			     (pCurrTxRate->TrainDown >> 1));
		} else {
			TrainUp = pCurrTxRate->TrainUp;
			TrainDown = pCurrTxRate->TrainDown;
		}

		/*pAd->DrsCounters.LastTimeTxRateChangeAction = pAd->DrsCounters.LastSecTxRateChangeAction; */

		/* */
		/* Keep the last time TxRateChangeAction status. */
		/* */
		pEntry->LastTimeTxRateChangeAction =
		    pEntry->LastSecTxRateChangeAction;

		/* */
		/* CASE 1. when TX samples are fewer than 15, then decide TX rate solely on RSSI */
		/*         (criteria copied from RT2500 for Netopia case) */
		/* */
		if (TxTotalCnt <= 15) {
			char idx = 0;
			u8 TxRateIdx;
			u8 MCS0 = 0, MCS1 = 0, MCS2 = 0, MCS3 = 0, MCS4 =
			    0, MCS5 = 0, MCS6 = 0, MCS7 = 0;
			u8 MCS12 = 0, MCS13 = 0, MCS14 = 0, MCS15 = 0;
			u8 MCS20 = 0, MCS21 = 0, MCS22 = 0, MCS23 = 0;	/* 3*3 */

			/* check the existence and index of each needed MCS */
			while (idx < pTable[0]) {
				pCurrTxRate =
				    (struct rt_rtmp_tx_rate_switch *) & pTable[(idx + 1) *
								    5];

				if (pCurrTxRate->CurrMCS == MCS_0) {
					MCS0 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_1) {
					MCS1 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_2) {
					MCS2 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_3) {
					MCS3 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_4) {
					MCS4 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_5) {
					MCS5 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_6) {
					MCS6 = idx;
				}
				/*else if (pCurrTxRate->CurrMCS == MCS_7) */
				else if ((pCurrTxRate->CurrMCS == MCS_7) && (pCurrTxRate->ShortGI == GI_800))	/* prevent the highest MCS using short GI when 1T and low throughput */
				{
					MCS7 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_12) {
					MCS12 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_13) {
					MCS13 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_14) {
					MCS14 = idx;
				}
				else if ((pCurrTxRate->CurrMCS == MCS_15) && (pCurrTxRate->ShortGI == GI_800))	/*we hope to use ShortGI as initial rate, however Atheros's chip has bugs when short GI */
				{
					MCS15 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_20)	/* 3*3 */
				{
					MCS20 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_21) {
					MCS21 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_22) {
					MCS22 = idx;
				} else if (pCurrTxRate->CurrMCS == MCS_23) {
					MCS23 = idx;
				}
				idx++;
			}

			if (pAd->LatchRfRegs.Channel <= 14) {
				if (pAd->NicConfig2.field.ExternalLNAForG) {
					RssiOffset = 2;
				} else {
					RssiOffset = 5;
				}
			} else {
				if (pAd->NicConfig2.field.ExternalLNAForA) {
					RssiOffset = 5;
				} else {
					RssiOffset = 8;
				}
			}

			/*if (MCS15) */
			if ((pTable == RateSwitchTable11BGN3S) || (pTable == RateSwitchTable11N3S) || (pTable == RateSwitchTable)) {	/* N mode with 3 stream // 3*3 */
				if (MCS23 && (Rssi >= -70))
					TxRateIdx = MCS23;
				else if (MCS22 && (Rssi >= -72))
					TxRateIdx = MCS22;
				else if (MCS21 && (Rssi >= -76))
					TxRateIdx = MCS21;
				else if (MCS20 && (Rssi >= -78))
					TxRateIdx = MCS20;
				else if (MCS4 && (Rssi >= -82))
					TxRateIdx = MCS4;
				else if (MCS3 && (Rssi >= -84))
					TxRateIdx = MCS3;
				else if (MCS2 && (Rssi >= -86))
					TxRateIdx = MCS2;
				else if (MCS1 && (Rssi >= -88))
					TxRateIdx = MCS1;
				else
					TxRateIdx = MCS0;
			}
/*              else if ((pTable == RateSwitchTable11BGN2S) || (pTable == RateSwitchTable11BGN2SForABand) ||(pTable == RateSwitchTable11N2S) ||(pTable == RateSwitchTable11N2SForABand) || (pTable == RateSwitchTable)) */
			else if ((pTable == RateSwitchTable11BGN2S) || (pTable == RateSwitchTable11BGN2SForABand) || (pTable == RateSwitchTable11N2S) || (pTable == RateSwitchTable11N2SForABand))	/* 3*3 */
			{	/* N mode with 2 stream */
				if (MCS15 && (Rssi >= (-70 + RssiOffset)))
					TxRateIdx = MCS15;
				else if (MCS14 && (Rssi >= (-72 + RssiOffset)))
					TxRateIdx = MCS14;
				else if (MCS13 && (Rssi >= (-76 + RssiOffset)))
					TxRateIdx = MCS13;
				else if (MCS12 && (Rssi >= (-78 + RssiOffset)))
					TxRateIdx = MCS12;
				else if (MCS4 && (Rssi >= (-82 + RssiOffset)))
					TxRateIdx = MCS4;
				else if (MCS3 && (Rssi >= (-84 + RssiOffset)))
					TxRateIdx = MCS3;
				else if (MCS2 && (Rssi >= (-86 + RssiOffset)))
					TxRateIdx = MCS2;
				else if (MCS1 && (Rssi >= (-88 + RssiOffset)))
					TxRateIdx = MCS1;
				else
					TxRateIdx = MCS0;
			} else if ((pTable == RateSwitchTable11BGN1S) || (pTable == RateSwitchTable11N1S)) {	/* N mode with 1 stream */
				if (MCS7 && (Rssi > (-72 + RssiOffset)))
					TxRateIdx = MCS7;
				else if (MCS6 && (Rssi > (-74 + RssiOffset)))
					TxRateIdx = MCS6;
				else if (MCS5 && (Rssi > (-77 + RssiOffset)))
					TxRateIdx = MCS5;
				else if (MCS4 && (Rssi > (-79 + RssiOffset)))
					TxRateIdx = MCS4;
				else if (MCS3 && (Rssi > (-81 + RssiOffset)))
					TxRateIdx = MCS3;
				else if (MCS2 && (Rssi > (-83 + RssiOffset)))
					TxRateIdx = MCS2;
				else if (MCS1 && (Rssi > (-86 + RssiOffset)))
					TxRateIdx = MCS1;
				else
					TxRateIdx = MCS0;
			} else {	/* Legacy mode */
				if (MCS7 && (Rssi > -70))
					TxRateIdx = MCS7;
				else if (MCS6 && (Rssi > -74))
					TxRateIdx = MCS6;
				else if (MCS5 && (Rssi > -78))
					TxRateIdx = MCS5;
				else if (MCS4 && (Rssi > -82))
					TxRateIdx = MCS4;
				else if (MCS4 == 0)	/* for B-only mode */
					TxRateIdx = MCS3;
				else if (MCS3 && (Rssi > -85))
					TxRateIdx = MCS3;
				else if (MCS2 && (Rssi > -87))
					TxRateIdx = MCS2;
				else if (MCS1 && (Rssi > -90))
					TxRateIdx = MCS1;
				else
					TxRateIdx = MCS0;
			}

			/*              if (TxRateIdx != pAd->CommonCfg.TxRateIndex) */
			{
				pEntry->CurrTxRateIndex = TxRateIdx;
				pNextTxRate =
				    (struct rt_rtmp_tx_rate_switch *) &
				    pTable[(pEntry->CurrTxRateIndex + 1) * 5];
				MlmeSetTxRate(pAd, pEntry, pNextTxRate);
			}

			NdisZeroMemory(pEntry->TxQuality,
				       sizeof(u16)*
				       MAX_STEP_OF_TX_RATE_SWITCH);
			NdisZeroMemory(pEntry->PER,
				       sizeof(u8)*
				       MAX_STEP_OF_TX_RATE_SWITCH);
			pEntry->fLastSecAccordingRSSI = TRUE;
			/* reset all OneSecTx counters */
			RESET_ONE_SEC_TX_CNT(pEntry);

			continue;
		}

		if (pEntry->fLastSecAccordingRSSI == TRUE) {
			pEntry->fLastSecAccordingRSSI = FALSE;
			pEntry->LastSecTxRateChangeAction = 0;
			/* reset all OneSecTx counters */
			RESET_ONE_SEC_TX_CNT(pEntry);

			continue;
		}

		do {
			BOOLEAN bTrainUpDown = FALSE;

			pEntry->CurrTxRateStableTime++;

			/* downgrade TX quality if PER >= Rate-Down threshold */
			if (TxErrorRatio >= TrainDown) {
				bTrainUpDown = TRUE;
				pEntry->TxQuality[CurrRateIdx] =
				    DRS_TX_QUALITY_WORST_BOUND;
			}
			/* upgrade TX quality if PER <= Rate-Up threshold */
			else if (TxErrorRatio <= TrainUp) {
				bTrainUpDown = TRUE;
				bUpgradeQuality = TRUE;
				if (pEntry->TxQuality[CurrRateIdx])
					pEntry->TxQuality[CurrRateIdx]--;	/* quality very good in CurrRate */

				if (pEntry->TxRateUpPenalty)
					pEntry->TxRateUpPenalty--;
				else if (pEntry->TxQuality[UpRateIdx])
					pEntry->TxQuality[UpRateIdx]--;	/* may improve next UP rate's quality */
			}

			pEntry->PER[CurrRateIdx] = (u8)TxErrorRatio;

			if (bTrainUpDown) {
				/* perform DRS - consider TxRate Down first, then rate up. */
				if ((CurrRateIdx != DownRateIdx)
				    && (pEntry->TxQuality[CurrRateIdx] >=
					DRS_TX_QUALITY_WORST_BOUND)) {
					pEntry->CurrTxRateIndex = DownRateIdx;
				} else if ((CurrRateIdx != UpRateIdx)
					   && (pEntry->TxQuality[UpRateIdx] <=
					       0)) {
					pEntry->CurrTxRateIndex = UpRateIdx;
				}
			}
		} while (FALSE);

		/* if rate-up happen, clear all bad history of all TX rates */
		if (pEntry->CurrTxRateIndex > CurrRateIdx) {
			pEntry->CurrTxRateStableTime = 0;
			pEntry->TxRateUpPenalty = 0;
			pEntry->LastSecTxRateChangeAction = 1;	/* rate UP */
			NdisZeroMemory(pEntry->TxQuality,
				       sizeof(u16)*
				       MAX_STEP_OF_TX_RATE_SWITCH);
			NdisZeroMemory(pEntry->PER,
				       sizeof(u8)*
				       MAX_STEP_OF_TX_RATE_SWITCH);

			/* */
			/* For TxRate fast train up */
			/* */
			if (!pAd->StaCfg.StaQuickResponeForRateUpTimerRunning) {
				RTMPSetTimer(&pAd->StaCfg.
					     StaQuickResponeForRateUpTimer,
					     100);

				pAd->StaCfg.
				    StaQuickResponeForRateUpTimerRunning = TRUE;
			}
			bTxRateChanged = TRUE;
		}
		/* if rate-down happen, only clear DownRate's bad history */
		else if (pEntry->CurrTxRateIndex < CurrRateIdx) {
			pEntry->CurrTxRateStableTime = 0;
			pEntry->TxRateUpPenalty = 0;	/* no penalty */
			pEntry->LastSecTxRateChangeAction = 2;	/* rate DOWN */
			pEntry->TxQuality[pEntry->CurrTxRateIndex] = 0;
			pEntry->PER[pEntry->CurrTxRateIndex] = 0;

			/* */
			/* For TxRate fast train down */
			/* */
			if (!pAd->StaCfg.StaQuickResponeForRateUpTimerRunning) {
				RTMPSetTimer(&pAd->StaCfg.
					     StaQuickResponeForRateUpTimer,
					     100);

				pAd->StaCfg.
				    StaQuickResponeForRateUpTimerRunning = TRUE;
			}
			bTxRateChanged = TRUE;
		} else {
			pEntry->LastSecTxRateChangeAction = 0;	/* rate no change */
			bTxRateChanged = FALSE;
		}

		pEntry->LastTxOkCount = TxSuccess;
		{
			u8 tmpTxRate;

			/* to fix tcp ack issue */
			if (!bTxRateChanged
			    && (pAd->RalinkCounters.OneSecReceivedByteCount >
				(pAd->RalinkCounters.
				 OneSecTransmittedByteCount * 5))) {
				tmpTxRate = DownRateIdx;
				DBGPRINT_RAW(RT_DEBUG_TRACE,
					     ("DRS: Rx(%d) is 5 times larger than Tx(%d), use low rate (curr=%d, tmp=%d)\n",
					      pAd->RalinkCounters.
					      OneSecReceivedByteCount,
					      pAd->RalinkCounters.
					      OneSecTransmittedByteCount,
					      pEntry->CurrTxRateIndex,
					      tmpTxRate));
			} else {
				tmpTxRate = pEntry->CurrTxRateIndex;
			}

			pNextTxRate =
			    (struct rt_rtmp_tx_rate_switch *) & pTable[(tmpTxRate + 1) *
							    5];
		}
		if (bTxRateChanged && pNextTxRate) {
			MlmeSetTxRate(pAd, pEntry, pNextTxRate);
		}
		/* reset all OneSecTx counters */
		RESET_ONE_SEC_TX_CNT(pEntry);
	}
}

/*
	========================================================================
	Routine Description:
		Station side, Auto TxRate faster train up timer call back function.

	Arguments:
		SystemSpecific1			- Not used.
		FunctionContext			- Pointer to our Adapter context.
		SystemSpecific2			- Not used.
		SystemSpecific3			- Not used.

	Return Value:
		None

	========================================================================
*/
void StaQuickResponeForRateUpExec(void *SystemSpecific1,
				  void *FunctionContext,
				  void *SystemSpecific2,
				  void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;
	u8 UpRateIdx = 0, DownRateIdx = 0, CurrRateIdx = 0;
	unsigned long TxTotalCnt;
	unsigned long TxErrorRatio = 0;
	BOOLEAN bTxRateChanged;	/*, bUpgradeQuality = FALSE; */
	struct rt_rtmp_tx_rate_switch *pCurrTxRate, *pNextTxRate = NULL;
	u8 *pTable;
	u8 TableSize = 0;
	u8 InitTxRateIdx = 0, TrainUp, TrainDown;
	TX_STA_CNT1_STRUC StaTx1;
	TX_STA_CNT0_STRUC TxStaCnt0;
	char Rssi, ratio;
	unsigned long TxRetransmit = 0, TxSuccess = 0, TxFailCount = 0;
	struct rt_mac_table_entry *pEntry;
	unsigned long i;

	pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = FALSE;

	/* */
	/* walk through MAC table, see if need to change AP's TX rate toward each entry */
	/* */
	for (i = 1; i < MAX_LEN_OF_MAC_TABLE; i++) {
		pEntry = &pAd->MacTab.Content[i];

		/* check if this entry need to switch rate automatically */
		if (RTMPCheckEntryEnableAutoRateSwitch(pAd, pEntry) == FALSE)
			continue;

		if (INFRA_ON(pAd) && (i == 1))
			Rssi = RTMPMaxRssi(pAd,
					   pAd->StaCfg.RssiSample.AvgRssi0,
					   pAd->StaCfg.RssiSample.AvgRssi1,
					   pAd->StaCfg.RssiSample.AvgRssi2);
		else
			Rssi = RTMPMaxRssi(pAd,
					   pEntry->RssiSample.AvgRssi0,
					   pEntry->RssiSample.AvgRssi1,
					   pEntry->RssiSample.AvgRssi2);

		CurrRateIdx = pAd->CommonCfg.TxRateIndex;

		MlmeSelectTxRateTable(pAd, pEntry, &pTable, &TableSize,
				      &InitTxRateIdx);

		/* decide the next upgrade rate and downgrade rate, if any */
		if ((CurrRateIdx > 0) && (CurrRateIdx < (TableSize - 1))) {
			UpRateIdx = CurrRateIdx + 1;
			DownRateIdx = CurrRateIdx - 1;
		} else if (CurrRateIdx == 0) {
			UpRateIdx = CurrRateIdx + 1;
			DownRateIdx = CurrRateIdx;
		} else if (CurrRateIdx == (TableSize - 1)) {
			UpRateIdx = CurrRateIdx;
			DownRateIdx = CurrRateIdx - 1;
		}

		pCurrTxRate =
		    (struct rt_rtmp_tx_rate_switch *) & pTable[(CurrRateIdx + 1) * 5];

		if ((Rssi > -65) && (pCurrTxRate->Mode >= MODE_HTMIX)) {
			TrainUp =
			    (pCurrTxRate->TrainUp +
			     (pCurrTxRate->TrainUp >> 1));
			TrainDown =
			    (pCurrTxRate->TrainDown +
			     (pCurrTxRate->TrainDown >> 1));
		} else {
			TrainUp = pCurrTxRate->TrainUp;
			TrainDown = pCurrTxRate->TrainDown;
		}

		if (pAd->MacTab.Size == 1) {
			/* Update statistic counter */
			RTMP_IO_READ32(pAd, TX_STA_CNT0, &TxStaCnt0.word);
			RTMP_IO_READ32(pAd, TX_STA_CNT1, &StaTx1.word);

			TxRetransmit = StaTx1.field.TxRetransmit;
			TxSuccess = StaTx1.field.TxSuccess;
			TxFailCount = TxStaCnt0.field.TxFailCount;
			TxTotalCnt = TxRetransmit + TxSuccess + TxFailCount;

			pAd->RalinkCounters.OneSecTxRetryOkCount +=
			    StaTx1.field.TxRetransmit;
			pAd->RalinkCounters.OneSecTxNoRetryOkCount +=
			    StaTx1.field.TxSuccess;
			pAd->RalinkCounters.OneSecTxFailCount +=
			    TxStaCnt0.field.TxFailCount;
			pAd->WlanCounters.TransmittedFragmentCount.u.LowPart +=
			    StaTx1.field.TxSuccess;
			pAd->WlanCounters.RetryCount.u.LowPart +=
			    StaTx1.field.TxRetransmit;
			pAd->WlanCounters.FailedCount.u.LowPart +=
			    TxStaCnt0.field.TxFailCount;

			if (TxTotalCnt)
				TxErrorRatio =
				    ((TxRetransmit +
				      TxFailCount) * 100) / TxTotalCnt;
		} else {
			TxTotalCnt = pEntry->OneSecTxNoRetryOkCount +
			    pEntry->OneSecTxRetryOkCount +
			    pEntry->OneSecTxFailCount;

			if (TxTotalCnt)
				TxErrorRatio =
				    ((pEntry->OneSecTxRetryOkCount +
				      pEntry->OneSecTxFailCount) * 100) /
				    TxTotalCnt;
		}

		/* */
		/* CASE 1. when TX samples are fewer than 15, then decide TX rate solely on RSSI */
		/*         (criteria copied from RT2500 for Netopia case) */
		/* */
		if (TxTotalCnt <= 12) {
			NdisZeroMemory(pAd->DrsCounters.TxQuality,
				       sizeof(u16)*
				       MAX_STEP_OF_TX_RATE_SWITCH);
			NdisZeroMemory(pAd->DrsCounters.PER,
				       sizeof(u8)*
				       MAX_STEP_OF_TX_RATE_SWITCH);

			if ((pAd->DrsCounters.LastSecTxRateChangeAction == 1)
			    && (CurrRateIdx != DownRateIdx)) {
				pAd->CommonCfg.TxRateIndex = DownRateIdx;
				pAd->DrsCounters.TxQuality[CurrRateIdx] =
				    DRS_TX_QUALITY_WORST_BOUND;
			} else
			    if ((pAd->DrsCounters.LastSecTxRateChangeAction ==
				 2) && (CurrRateIdx != UpRateIdx)) {
				pAd->CommonCfg.TxRateIndex = UpRateIdx;
			}

			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("QuickDRS: TxTotalCnt <= 15, train back to original rate \n"));
			return;
		}

		do {
			unsigned long OneSecTxNoRetryOKRationCount;

			if (pAd->DrsCounters.LastTimeTxRateChangeAction == 0)
				ratio = 5;
			else
				ratio = 4;

			/* downgrade TX quality if PER >= Rate-Down threshold */
			if (TxErrorRatio >= TrainDown) {
				pAd->DrsCounters.TxQuality[CurrRateIdx] =
				    DRS_TX_QUALITY_WORST_BOUND;
			}

			pAd->DrsCounters.PER[CurrRateIdx] =
			    (u8)TxErrorRatio;

			OneSecTxNoRetryOKRationCount = (TxSuccess * ratio);

			/* perform DRS - consider TxRate Down first, then rate up. */
			if ((pAd->DrsCounters.LastSecTxRateChangeAction == 1)
			    && (CurrRateIdx != DownRateIdx)) {
				if ((pAd->DrsCounters.LastTxOkCount + 2) >=
				    OneSecTxNoRetryOKRationCount) {
					pAd->CommonCfg.TxRateIndex =
					    DownRateIdx;
					pAd->DrsCounters.
					    TxQuality[CurrRateIdx] =
					    DRS_TX_QUALITY_WORST_BOUND;

				}

			} else
			    if ((pAd->DrsCounters.LastSecTxRateChangeAction ==
				 2) && (CurrRateIdx != UpRateIdx)) {
				if ((TxErrorRatio >= 50)
				    || (TxErrorRatio >= TrainDown)) {

				} else if ((pAd->DrsCounters.LastTxOkCount + 2)
					   >= OneSecTxNoRetryOKRationCount) {
					pAd->CommonCfg.TxRateIndex = UpRateIdx;
				}
			}
		} while (FALSE);

		/* if rate-up happen, clear all bad history of all TX rates */
		if (pAd->CommonCfg.TxRateIndex > CurrRateIdx) {
			pAd->DrsCounters.TxRateUpPenalty = 0;
			NdisZeroMemory(pAd->DrsCounters.TxQuality,
				       sizeof(u16)*
				       MAX_STEP_OF_TX_RATE_SWITCH);
			NdisZeroMemory(pAd->DrsCounters.PER,
				       sizeof(u8)*
				       MAX_STEP_OF_TX_RATE_SWITCH);
			bTxRateChanged = TRUE;
		}
		/* if rate-down happen, only clear DownRate's bad history */
		else if (pAd->CommonCfg.TxRateIndex < CurrRateIdx) {
			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("QuickDRS: --TX rate from %d to %d \n",
				      CurrRateIdx, pAd->CommonCfg.TxRateIndex));

			pAd->DrsCounters.TxRateUpPenalty = 0;	/* no penalty */
			pAd->DrsCounters.TxQuality[pAd->CommonCfg.TxRateIndex] =
			    0;
			pAd->DrsCounters.PER[pAd->CommonCfg.TxRateIndex] = 0;
			bTxRateChanged = TRUE;
		} else {
			bTxRateChanged = FALSE;
		}

		pNextTxRate =
		    (struct rt_rtmp_tx_rate_switch *) &
		    pTable[(pAd->CommonCfg.TxRateIndex + 1) * 5];
		if (bTxRateChanged && pNextTxRate) {
			MlmeSetTxRate(pAd, pEntry, pNextTxRate);
		}
	}
}

/*
	==========================================================================
	Description:
		This routine is executed periodically inside MlmePeriodicExec() after
		association with an AP.
		It checks if StaCfg.Psm is consistent with user policy (recorded in
		StaCfg.WindowsPowerMode). If not, enforce user policy. However,
		there're some conditions to consider:
		1. we don't support power-saving in ADHOC mode, so Psm=PWR_ACTIVE all
		   the time when Mibss==TRUE
		2. When link up in INFRA mode, Psm should not be switch to PWR_SAVE
		   if outgoing traffic available in TxRing or MgmtRing.
	Output:
		1. change pAd->StaCfg.Psm to PWR_SAVE or leave it untouched

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
void MlmeCheckPsmChange(struct rt_rtmp_adapter *pAd, unsigned long Now32)
{
	unsigned long PowerMode;

	/* condition - */
	/* 1. Psm maybe ON only happen in INFRASTRUCTURE mode */
	/* 2. user wants either MAX_PSP or FAST_PSP */
	/* 3. but current psm is not in PWR_SAVE */
	/* 4. CNTL state machine is not doing SCANning */
	/* 5. no TX SUCCESS event for the past 1-sec period */
	PowerMode = pAd->StaCfg.WindowsPowerMode;

	if (INFRA_ON(pAd) &&
	    (PowerMode != Ndis802_11PowerModeCAM) &&
	    (pAd->StaCfg.Psm == PWR_ACTIVE) &&
/*              (! RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS)) */
	    (pAd->Mlme.CntlMachine.CurrState == CNTL_IDLE) &&
	    RTMP_TEST_PSFLAG(pAd, fRTMP_PS_CAN_GO_SLEEP)
	    /*&&
	       (pAd->RalinkCounters.OneSecTxNoRetryOkCount == 0) &&
	       (pAd->RalinkCounters.OneSecTxRetryOkCount == 0) */
	    ) {
		NdisGetSystemUpTime(&pAd->Mlme.LastSendNULLpsmTime);
		pAd->RalinkCounters.RxCountSinceLastNULL = 0;
		RTMP_SET_PSM_BIT(pAd, PWR_SAVE);
		if (!
		    (pAd->CommonCfg.bAPSDCapable
		     && pAd->CommonCfg.APEdcaParm.bAPSDCapable)) {
			RTMPSendNullFrame(pAd, pAd->CommonCfg.TxRate, FALSE);
		} else {
			RTMPSendNullFrame(pAd, pAd->CommonCfg.TxRate, TRUE);
		}
	}
}

/* IRQL = PASSIVE_LEVEL */
/* IRQL = DISPATCH_LEVEL */
void MlmeSetPsmBit(struct rt_rtmp_adapter *pAd, u16 psm)
{
	AUTO_RSP_CFG_STRUC csr4;

	pAd->StaCfg.Psm = psm;
	RTMP_IO_READ32(pAd, AUTO_RSP_CFG, &csr4.word);
	csr4.field.AckCtsPsmBit = (psm == PWR_SAVE) ? 1 : 0;
	RTMP_IO_WRITE32(pAd, AUTO_RSP_CFG, csr4.word);

	DBGPRINT(RT_DEBUG_TRACE, ("MlmeSetPsmBit = %d\n", psm));
}

/*
	==========================================================================
	Description:
		This routine calculates TxPER, RxPER of the past N-sec period. And
		according to the calculation result, ChannelQuality is calculated here
		to decide if current AP is still doing the job.

		If ChannelQuality is not good, a ROAMing attempt may be tried later.
	Output:
		StaCfg.ChannelQuality - 0..100

	IRQL = DISPATCH_LEVEL

	NOTE: This routine decide channle quality based on RX CRC error ratio.
		Caller should make sure a function call to NICUpdateRawCounters(pAd)
		is performed right before this routine, so that this routine can decide
		channel quality based on the most up-to-date information
	==========================================================================
 */
void MlmeCalculateChannelQuality(struct rt_rtmp_adapter *pAd,
				 struct rt_mac_table_entry *pMacEntry, unsigned long Now32)
{
	unsigned long TxOkCnt, TxCnt, TxPER, TxPRR;
	unsigned long RxCnt, RxPER;
	u8 NorRssi;
	char MaxRssi;
	struct rt_rssi_sample *pRssiSample = NULL;
	u32 OneSecTxNoRetryOkCount = 0;
	u32 OneSecTxRetryOkCount = 0;
	u32 OneSecTxFailCount = 0;
	u32 OneSecRxOkCnt = 0;
	u32 OneSecRxFcsErrCnt = 0;
	unsigned long ChannelQuality = 0;	/* 0..100, Channel Quality Indication for Roaming */
	unsigned long BeaconLostTime = pAd->StaCfg.BeaconLostTime;

	if (pAd->OpMode == OPMODE_STA) {
		pRssiSample = &pAd->StaCfg.RssiSample;
		OneSecTxNoRetryOkCount =
		    pAd->RalinkCounters.OneSecTxNoRetryOkCount;
		OneSecTxRetryOkCount = pAd->RalinkCounters.OneSecTxRetryOkCount;
		OneSecTxFailCount = pAd->RalinkCounters.OneSecTxFailCount;
		OneSecRxOkCnt = pAd->RalinkCounters.OneSecRxOkCnt;
		OneSecRxFcsErrCnt = pAd->RalinkCounters.OneSecRxFcsErrCnt;
	}

	MaxRssi = RTMPMaxRssi(pAd, pRssiSample->LastRssi0,
			      pRssiSample->LastRssi1, pRssiSample->LastRssi2);

	/* */
	/* calculate TX packet error ratio and TX retry ratio - if too few TX samples, skip TX related statistics */
	/* */
	TxOkCnt = OneSecTxNoRetryOkCount + OneSecTxRetryOkCount;
	TxCnt = TxOkCnt + OneSecTxFailCount;
	if (TxCnt < 5) {
		TxPER = 0;
		TxPRR = 0;
	} else {
		TxPER = (OneSecTxFailCount * 100) / TxCnt;
		TxPRR = ((TxCnt - OneSecTxNoRetryOkCount) * 100) / TxCnt;
	}

	/* */
	/* calculate RX PER - don't take RxPER into consideration if too few sample */
	/* */
	RxCnt = OneSecRxOkCnt + OneSecRxFcsErrCnt;
	if (RxCnt < 5)
		RxPER = 0;
	else
		RxPER = (OneSecRxFcsErrCnt * 100) / RxCnt;

	/* */
	/* decide ChannelQuality based on: 1)last BEACON received time, 2)last RSSI, 3)TxPER, and 4)RxPER */
	/* */
	if ((pAd->OpMode == OPMODE_STA) && INFRA_ON(pAd) && (OneSecTxNoRetryOkCount < 2) &&	/* no heavy traffic */
	    ((pAd->StaCfg.LastBeaconRxTime + BeaconLostTime) < Now32)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("BEACON lost > %ld msec with TxOkCnt=%ld -> CQI=0\n",
			  BeaconLostTime, TxOkCnt));
		ChannelQuality = 0;
	} else {
		/* Normalize Rssi */
		if (MaxRssi > -40)
			NorRssi = 100;
		else if (MaxRssi < -90)
			NorRssi = 0;
		else
			NorRssi = (MaxRssi + 90) * 2;

		/* ChannelQuality = W1*RSSI + W2*TxPRR + W3*RxPER        (RSSI 0..100), (TxPER 100..0), (RxPER 100..0) */
		ChannelQuality = (RSSI_WEIGHTING * NorRssi +
				  TX_WEIGHTING * (100 - TxPRR) +
				  RX_WEIGHTING * (100 - RxPER)) / 100;
	}

	if (pAd->OpMode == OPMODE_STA)
		pAd->Mlme.ChannelQuality =
		    (ChannelQuality > 100) ? 100 : ChannelQuality;

}

/* IRQL = DISPATCH_LEVEL */
void MlmeSetTxPreamble(struct rt_rtmp_adapter *pAd, u16 TxPreamble)
{
	AUTO_RSP_CFG_STRUC csr4;

	/* */
	/* Always use Long preamble before verifiation short preamble functionality works well. */
	/* Todo: remove the following line if short preamble functionality works */
	/* */
	/*TxPreamble = Rt802_11PreambleLong; */

	RTMP_IO_READ32(pAd, AUTO_RSP_CFG, &csr4.word);
	if (TxPreamble == Rt802_11PreambleLong) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("MlmeSetTxPreamble (= long PREAMBLE)\n"));
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);
		csr4.field.AutoResponderPreamble = 0;
	} else {
		/* NOTE: 1Mbps should always use long preamble */
		DBGPRINT(RT_DEBUG_TRACE,
			 ("MlmeSetTxPreamble (= short PREAMBLE)\n"));
		OPSTATUS_SET_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);
		csr4.field.AutoResponderPreamble = 1;
	}

	RTMP_IO_WRITE32(pAd, AUTO_RSP_CFG, csr4.word);
}

/*
    ==========================================================================
    Description:
        Update basic rate bitmap
    ==========================================================================
 */

void UpdateBasicRateBitmap(struct rt_rtmp_adapter *pAdapter)
{
	int i, j;
	/* 1  2  5.5, 11,  6,  9, 12, 18, 24, 36, 48,  54 */
	u8 rate[] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };
	u8 *sup_p = pAdapter->CommonCfg.SupRate;
	u8 *ext_p = pAdapter->CommonCfg.ExtRate;
	unsigned long bitmap = pAdapter->CommonCfg.BasicRateBitmap;

	/* if A mode, always use fix BasicRateBitMap */
	/*if (pAdapter->CommonCfg.Channel == PHY_11A) */
	if (pAdapter->CommonCfg.Channel > 14)
		pAdapter->CommonCfg.BasicRateBitmap = 0x150;	/* 6, 12, 24M */
	/* End of if */

	if (pAdapter->CommonCfg.BasicRateBitmap > 4095) {
		/* (2 ^ MAX_LEN_OF_SUPPORTED_RATES) -1 */
		return;
	}
	/* End of if */
	for (i = 0; i < MAX_LEN_OF_SUPPORTED_RATES; i++) {
		sup_p[i] &= 0x7f;
		ext_p[i] &= 0x7f;
	}			/* End of for */

	for (i = 0; i < MAX_LEN_OF_SUPPORTED_RATES; i++) {
		if (bitmap & (1 << i)) {
			for (j = 0; j < MAX_LEN_OF_SUPPORTED_RATES; j++) {
				if (sup_p[j] == rate[i])
					sup_p[j] |= 0x80;
				/* End of if */
			}	/* End of for */

			for (j = 0; j < MAX_LEN_OF_SUPPORTED_RATES; j++) {
				if (ext_p[j] == rate[i])
					ext_p[j] |= 0x80;
				/* End of if */
			}	/* End of for */
		}		/* End of if */
	}			/* End of for */
}				/* End of UpdateBasicRateBitmap */

/* IRQL = PASSIVE_LEVEL */
/* IRQL = DISPATCH_LEVEL */
/* bLinkUp is to identify the inital link speed. */
/* TRUE indicates the rate update at linkup, we should not try to set the rate at 54Mbps. */
void MlmeUpdateTxRates(struct rt_rtmp_adapter *pAd, IN BOOLEAN bLinkUp, u8 apidx)
{
	int i, num;
	u8 Rate = RATE_6, MaxDesire = RATE_1, MaxSupport = RATE_1;
	u8 MinSupport = RATE_54;
	unsigned long BasicRateBitmap = 0;
	u8 CurrBasicRate = RATE_1;
	u8 *pSupRate, SupRateLen, *pExtRate, ExtRateLen;
	PHTTRANSMIT_SETTING pHtPhy = NULL;
	PHTTRANSMIT_SETTING pMaxHtPhy = NULL;
	PHTTRANSMIT_SETTING pMinHtPhy = NULL;
	BOOLEAN *auto_rate_cur_p;
	u8 HtMcs = MCS_AUTO;

	/* find max desired rate */
	UpdateBasicRateBitmap(pAd);

	num = 0;
	auto_rate_cur_p = NULL;
	for (i = 0; i < MAX_LEN_OF_SUPPORTED_RATES; i++) {
		switch (pAd->CommonCfg.DesireRate[i] & 0x7f) {
		case 2:
			Rate = RATE_1;
			num++;
			break;
		case 4:
			Rate = RATE_2;
			num++;
			break;
		case 11:
			Rate = RATE_5_5;
			num++;
			break;
		case 22:
			Rate = RATE_11;
			num++;
			break;
		case 12:
			Rate = RATE_6;
			num++;
			break;
		case 18:
			Rate = RATE_9;
			num++;
			break;
		case 24:
			Rate = RATE_12;
			num++;
			break;
		case 36:
			Rate = RATE_18;
			num++;
			break;
		case 48:
			Rate = RATE_24;
			num++;
			break;
		case 72:
			Rate = RATE_36;
			num++;
			break;
		case 96:
			Rate = RATE_48;
			num++;
			break;
		case 108:
			Rate = RATE_54;
			num++;
			break;
			/*default: Rate = RATE_1;   break; */
		}
		if (MaxDesire < Rate)
			MaxDesire = Rate;
	}

/*=========================================================================== */
/*=========================================================================== */
	{
		pHtPhy = &pAd->StaCfg.HTPhyMode;
		pMaxHtPhy = &pAd->StaCfg.MaxHTPhyMode;
		pMinHtPhy = &pAd->StaCfg.MinHTPhyMode;

		auto_rate_cur_p = &pAd->StaCfg.bAutoTxRateSwitch;
		HtMcs = pAd->StaCfg.DesiredTransmitSetting.field.MCS;

		if ((pAd->StaCfg.BssType == BSS_ADHOC) &&
		    (pAd->CommonCfg.PhyMode == PHY_11B) &&
		    (MaxDesire > RATE_11)) {
			MaxDesire = RATE_11;
		}
	}

	pAd->CommonCfg.MaxDesiredRate = MaxDesire;
	pMinHtPhy->word = 0;
	pMaxHtPhy->word = 0;
	pHtPhy->word = 0;

	/* Auto rate switching is enabled only if more than one DESIRED RATES are */
	/* specified; otherwise disabled */
	if (num <= 1) {
		/*OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED); */
		/*pAd->CommonCfg.bAutoTxRateSwitch      = FALSE; */
		*auto_rate_cur_p = FALSE;
	} else {
		/*OPSTATUS_SET_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED); */
		/*pAd->CommonCfg.bAutoTxRateSwitch      = TRUE; */
		*auto_rate_cur_p = TRUE;
	}

	if (HtMcs != MCS_AUTO) {
		/*OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED); */
		/*pAd->CommonCfg.bAutoTxRateSwitch      = FALSE; */
		*auto_rate_cur_p = FALSE;
	} else {
		/*OPSTATUS_SET_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED); */
		/*pAd->CommonCfg.bAutoTxRateSwitch      = TRUE; */
		*auto_rate_cur_p = TRUE;
	}

	if ((ADHOC_ON(pAd) || INFRA_ON(pAd)) && (pAd->OpMode == OPMODE_STA)) {
		pSupRate = &pAd->StaActive.SupRate[0];
		pExtRate = &pAd->StaActive.ExtRate[0];
		SupRateLen = pAd->StaActive.SupRateLen;
		ExtRateLen = pAd->StaActive.ExtRateLen;
	} else {
		pSupRate = &pAd->CommonCfg.SupRate[0];
		pExtRate = &pAd->CommonCfg.ExtRate[0];
		SupRateLen = pAd->CommonCfg.SupRateLen;
		ExtRateLen = pAd->CommonCfg.ExtRateLen;
	}

	/* find max supported rate */
	for (i = 0; i < SupRateLen; i++) {
		switch (pSupRate[i] & 0x7f) {
		case 2:
			Rate = RATE_1;
			if (pSupRate[i] & 0x80)
				BasicRateBitmap |= 0x0001;
			break;
		case 4:
			Rate = RATE_2;
			if (pSupRate[i] & 0x80)
				BasicRateBitmap |= 0x0002;
			break;
		case 11:
			Rate = RATE_5_5;
			if (pSupRate[i] & 0x80)
				BasicRateBitmap |= 0x0004;
			break;
		case 22:
			Rate = RATE_11;
			if (pSupRate[i] & 0x80)
				BasicRateBitmap |= 0x0008;
			break;
		case 12:
			Rate = RATE_6;	/*if (pSupRate[i] & 0x80) */
			BasicRateBitmap |= 0x0010;
			break;
		case 18:
			Rate = RATE_9;
			if (pSupRate[i] & 0x80)
				BasicRateBitmap |= 0x0020;
			break;
		case 24:
			Rate = RATE_12;	/*if (pSupRate[i] & 0x80) */
			BasicRateBitmap |= 0x0040;
			break;
		case 36:
			Rate = RATE_18;
			if (pSupRate[i] & 0x80)
				BasicRateBitmap |= 0x0080;
			break;
		case 48:
			Rate = RATE_24;	/*if (pSupRate[i] & 0x80) */
			BasicRateBitmap |= 0x0100;
			break;
		case 72:
			Rate = RATE_36;
			if (pSupRate[i] & 0x80)
				BasicRateBitmap |= 0x0200;
			break;
		case 96:
			Rate = RATE_48;
			if (pSupRate[i] & 0x80)
				BasicRateBitmap |= 0x0400;
			break;
		case 108:
			Rate = RATE_54;
			if (pSupRate[i] & 0x80)
				BasicRateBitmap |= 0x0800;
			break;
		default:
			Rate = RATE_1;
			break;
		}
		if (MaxSupport < Rate)
			MaxSupport = Rate;

		if (MinSupport > Rate)
			MinSupport = Rate;
	}

	for (i = 0; i < ExtRateLen; i++) {
		switch (pExtRate[i] & 0x7f) {
		case 2:
			Rate = RATE_1;
			if (pExtRate[i] & 0x80)
				BasicRateBitmap |= 0x0001;
			break;
		case 4:
			Rate = RATE_2;
			if (pExtRate[i] & 0x80)
				BasicRateBitmap |= 0x0002;
			break;
		case 11:
			Rate = RATE_5_5;
			if (pExtRate[i] & 0x80)
				BasicRateBitmap |= 0x0004;
			break;
		case 22:
			Rate = RATE_11;
			if (pExtRate[i] & 0x80)
				BasicRateBitmap |= 0x0008;
			break;
		case 12:
			Rate = RATE_6;	/*if (pExtRate[i] & 0x80) */
			BasicRateBitmap |= 0x0010;
			break;
		case 18:
			Rate = RATE_9;
			if (pExtRate[i] & 0x80)
				BasicRateBitmap |= 0x0020;
			break;
		case 24:
			Rate = RATE_12;	/*if (pExtRate[i] & 0x80) */
			BasicRateBitmap |= 0x0040;
			break;
		case 36:
			Rate = RATE_18;
			if (pExtRate[i] & 0x80)
				BasicRateBitmap |= 0x0080;
			break;
		case 48:
			Rate = RATE_24;	/*if (pExtRate[i] & 0x80) */
			BasicRateBitmap |= 0x0100;
			break;
		case 72:
			Rate = RATE_36;
			if (pExtRate[i] & 0x80)
				BasicRateBitmap |= 0x0200;
			break;
		case 96:
			Rate = RATE_48;
			if (pExtRate[i] & 0x80)
				BasicRateBitmap |= 0x0400;
			break;
		case 108:
			Rate = RATE_54;
			if (pExtRate[i] & 0x80)
				BasicRateBitmap |= 0x0800;
			break;
		default:
			Rate = RATE_1;
			break;
		}
		if (MaxSupport < Rate)
			MaxSupport = Rate;

		if (MinSupport > Rate)
			MinSupport = Rate;
	}

	RTMP_IO_WRITE32(pAd, LEGACY_BASIC_RATE, BasicRateBitmap);

	/* bug fix */
	/* pAd->CommonCfg.BasicRateBitmap = BasicRateBitmap; */

	/* calculate the exptected ACK rate for each TX rate. This info is used to caculate */
	/* the DURATION field of outgoing uniicast DATA/MGMT frame */
	for (i = 0; i < MAX_LEN_OF_SUPPORTED_RATES; i++) {
		if (BasicRateBitmap & (0x01 << i))
			CurrBasicRate = (u8)i;
		pAd->CommonCfg.ExpectedACKRate[i] = CurrBasicRate;
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 ("MlmeUpdateTxRates[MaxSupport = %d] = MaxDesire %d Mbps\n",
		  RateIdToMbps[MaxSupport], RateIdToMbps[MaxDesire]));
	/* max tx rate = min {max desire rate, max supported rate} */
	if (MaxSupport < MaxDesire)
		pAd->CommonCfg.MaxTxRate = MaxSupport;
	else
		pAd->CommonCfg.MaxTxRate = MaxDesire;

	pAd->CommonCfg.MinTxRate = MinSupport;
	/* 2003-07-31 john - 2500 doesn't have good sensitivity at high OFDM rates. to increase the success */
	/* ratio of initial DHCP packet exchange, TX rate starts from a lower rate depending */
	/* on average RSSI */
	/*       1. RSSI >= -70db, start at 54 Mbps (short distance) */
	/*       2. -70 > RSSI >= -75, start at 24 Mbps (mid distance) */
	/*       3. -75 > RSSI, start at 11 Mbps (long distance) */
	if (*auto_rate_cur_p) {
		short dbm = 0;

		dbm = pAd->StaCfg.RssiSample.AvgRssi0 - pAd->BbpRssiToDbmDelta;

		if (bLinkUp == TRUE)
			pAd->CommonCfg.TxRate = RATE_24;
		else
			pAd->CommonCfg.TxRate = pAd->CommonCfg.MaxTxRate;

		if (dbm < -75)
			pAd->CommonCfg.TxRate = RATE_11;
		else if (dbm < -70)
			pAd->CommonCfg.TxRate = RATE_24;

		/* should never exceed MaxTxRate (consider 11B-only mode) */
		if (pAd->CommonCfg.TxRate > pAd->CommonCfg.MaxTxRate)
			pAd->CommonCfg.TxRate = pAd->CommonCfg.MaxTxRate;

		pAd->CommonCfg.TxRateIndex = 0;
	} else {
		pAd->CommonCfg.TxRate = pAd->CommonCfg.MaxTxRate;
		pHtPhy->field.MCS =
		    (pAd->CommonCfg.MaxTxRate >
		     3) ? (pAd->CommonCfg.MaxTxRate -
			   4) : pAd->CommonCfg.MaxTxRate;
		pHtPhy->field.MODE =
		    (pAd->CommonCfg.MaxTxRate > 3) ? MODE_OFDM : MODE_CCK;

		pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.STBC =
		    pHtPhy->field.STBC;
		pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.ShortGI =
		    pHtPhy->field.ShortGI;
		pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.MCS =
		    pHtPhy->field.MCS;
		pAd->MacTab.Content[BSSID_WCID].HTPhyMode.field.MODE =
		    pHtPhy->field.MODE;
	}

	if (pAd->CommonCfg.TxRate <= RATE_11) {
		pMaxHtPhy->field.MODE = MODE_CCK;
		pMaxHtPhy->field.MCS = pAd->CommonCfg.TxRate;
		pMinHtPhy->field.MCS = pAd->CommonCfg.MinTxRate;
	} else {
		pMaxHtPhy->field.MODE = MODE_OFDM;
		pMaxHtPhy->field.MCS = OfdmRateToRxwiMCS[pAd->CommonCfg.TxRate];
		if (pAd->CommonCfg.MinTxRate >= RATE_6
		    && (pAd->CommonCfg.MinTxRate <= RATE_54)) {
			pMinHtPhy->field.MCS =
			    OfdmRateToRxwiMCS[pAd->CommonCfg.MinTxRate];
		} else {
			pMinHtPhy->field.MCS = pAd->CommonCfg.MinTxRate;
		}
	}

	pHtPhy->word = (pMaxHtPhy->word);
	if (bLinkUp && (pAd->OpMode == OPMODE_STA)) {
		pAd->MacTab.Content[BSSID_WCID].HTPhyMode.word = pHtPhy->word;
		pAd->MacTab.Content[BSSID_WCID].MaxHTPhyMode.word =
		    pMaxHtPhy->word;
		pAd->MacTab.Content[BSSID_WCID].MinHTPhyMode.word =
		    pMinHtPhy->word;
	} else {
		switch (pAd->CommonCfg.PhyMode) {
		case PHY_11BG_MIXED:
		case PHY_11B:
		case PHY_11BGN_MIXED:
			pAd->CommonCfg.MlmeRate = RATE_1;
			pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_CCK;
			pAd->CommonCfg.MlmeTransmit.field.MCS = RATE_1;

/*#ifdef        WIFI_TEST */
			pAd->CommonCfg.RtsRate = RATE_11;
/*#else */
/*                              pAd->CommonCfg.RtsRate = RATE_1; */
/*#endif */
			break;
		case PHY_11G:
		case PHY_11A:
		case PHY_11AGN_MIXED:
		case PHY_11GN_MIXED:
		case PHY_11N_2_4G:
		case PHY_11AN_MIXED:
		case PHY_11N_5G:
			pAd->CommonCfg.MlmeRate = RATE_6;
			pAd->CommonCfg.RtsRate = RATE_6;
			pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_OFDM;
			pAd->CommonCfg.MlmeTransmit.field.MCS =
			    OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
			break;
		case PHY_11ABG_MIXED:
		case PHY_11ABGN_MIXED:
			if (pAd->CommonCfg.Channel <= 14) {
				pAd->CommonCfg.MlmeRate = RATE_1;
				pAd->CommonCfg.RtsRate = RATE_1;
				pAd->CommonCfg.MlmeTransmit.field.MODE =
				    MODE_CCK;
				pAd->CommonCfg.MlmeTransmit.field.MCS = RATE_1;
			} else {
				pAd->CommonCfg.MlmeRate = RATE_6;
				pAd->CommonCfg.RtsRate = RATE_6;
				pAd->CommonCfg.MlmeTransmit.field.MODE =
				    MODE_OFDM;
				pAd->CommonCfg.MlmeTransmit.field.MCS =
				    OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
			}
			break;
		default:	/* error */
			pAd->CommonCfg.MlmeRate = RATE_6;
			pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_OFDM;
			pAd->CommonCfg.MlmeTransmit.field.MCS =
			    OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
			pAd->CommonCfg.RtsRate = RATE_1;
			break;
		}
		/* */
		/* Keep Basic Mlme Rate. */
		/* */
		pAd->MacTab.Content[MCAST_WCID].HTPhyMode.word =
		    pAd->CommonCfg.MlmeTransmit.word;
		if (pAd->CommonCfg.MlmeTransmit.field.MODE == MODE_OFDM)
			pAd->MacTab.Content[MCAST_WCID].HTPhyMode.field.MCS =
			    OfdmRateToRxwiMCS[RATE_24];
		else
			pAd->MacTab.Content[MCAST_WCID].HTPhyMode.field.MCS =
			    RATE_1;
		pAd->CommonCfg.BasicMlmeRate = pAd->CommonCfg.MlmeRate;
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 (" MlmeUpdateTxRates (MaxDesire=%d, MaxSupport=%d, MaxTxRate=%d, MinRate=%d, Rate Switching =%d)\n",
		  RateIdToMbps[MaxDesire], RateIdToMbps[MaxSupport],
		  RateIdToMbps[pAd->CommonCfg.MaxTxRate],
		  RateIdToMbps[pAd->CommonCfg.MinTxRate],
		  /*OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED) */
		  *auto_rate_cur_p));
	DBGPRINT(RT_DEBUG_TRACE,
		 (" MlmeUpdateTxRates (TxRate=%d, RtsRate=%d, BasicRateBitmap=0x%04lx)\n",
		  RateIdToMbps[pAd->CommonCfg.TxRate],
		  RateIdToMbps[pAd->CommonCfg.RtsRate], BasicRateBitmap));
	DBGPRINT(RT_DEBUG_TRACE,
		 ("MlmeUpdateTxRates (MlmeTransmit=0x%x, MinHTPhyMode=%x, MaxHTPhyMode=0x%x, HTPhyMode=0x%x)\n",
		  pAd->CommonCfg.MlmeTransmit.word,
		  pAd->MacTab.Content[BSSID_WCID].MinHTPhyMode.word,
		  pAd->MacTab.Content[BSSID_WCID].MaxHTPhyMode.word,
		  pAd->MacTab.Content[BSSID_WCID].HTPhyMode.word));
}

/*
	==========================================================================
	Description:
		This function update HT Rate setting.
		Input Wcid value is valid for 2 case :
		1. it's used for Station in infra mode that copy AP rate to Mactable.
		2. OR Station	in adhoc mode to copy peer's HT rate to Mactable.

 IRQL = DISPATCH_LEVEL

	==========================================================================
 */
void MlmeUpdateHtTxRates(struct rt_rtmp_adapter *pAd, u8 apidx)
{
	u8 StbcMcs;		/*j, StbcMcs, bitmask; */
	char i;			/* 3*3 */
	struct rt_ht_capability *pRtHtCap = NULL;
	struct rt_ht_phy_info *pActiveHtPhy = NULL;
	unsigned long BasicMCS;
	u8 j, bitmask;
	struct rt_ht_phy_info *pDesireHtPhy = NULL;
	PHTTRANSMIT_SETTING pHtPhy = NULL;
	PHTTRANSMIT_SETTING pMaxHtPhy = NULL;
	PHTTRANSMIT_SETTING pMinHtPhy = NULL;
	BOOLEAN *auto_rate_cur_p;

	DBGPRINT(RT_DEBUG_TRACE, ("MlmeUpdateHtTxRates===> \n"));

	auto_rate_cur_p = NULL;

	{
		pDesireHtPhy = &pAd->StaCfg.DesiredHtPhyInfo;
		pActiveHtPhy = &pAd->StaCfg.DesiredHtPhyInfo;
		pHtPhy = &pAd->StaCfg.HTPhyMode;
		pMaxHtPhy = &pAd->StaCfg.MaxHTPhyMode;
		pMinHtPhy = &pAd->StaCfg.MinHTPhyMode;

		auto_rate_cur_p = &pAd->StaCfg.bAutoTxRateSwitch;
	}

	if ((ADHOC_ON(pAd) || INFRA_ON(pAd)) && (pAd->OpMode == OPMODE_STA)) {
		if (pAd->StaActive.SupportedPhyInfo.bHtEnable == FALSE)
			return;

		pRtHtCap = &pAd->StaActive.SupportedHtPhy;
		pActiveHtPhy = &pAd->StaActive.SupportedPhyInfo;
		StbcMcs = (u8)pAd->MlmeAux.AddHtInfo.AddHtInfo3.StbcMcs;
		BasicMCS =
		    pAd->MlmeAux.AddHtInfo.MCSSet[0] +
		    (pAd->MlmeAux.AddHtInfo.MCSSet[1] << 8) + (StbcMcs << 16);
		if ((pAd->CommonCfg.DesiredHtPhy.TxSTBC) && (pRtHtCap->RxSTBC)
		    && (pAd->Antenna.field.TxPath == 2))
			pMaxHtPhy->field.STBC = STBC_USE;
		else
			pMaxHtPhy->field.STBC = STBC_NONE;
	} else {
		if (pDesireHtPhy->bHtEnable == FALSE)
			return;

		pRtHtCap = &pAd->CommonCfg.DesiredHtPhy;
		StbcMcs = (u8)pAd->CommonCfg.AddHTInfo.AddHtInfo3.StbcMcs;
		BasicMCS =
		    pAd->CommonCfg.AddHTInfo.MCSSet[0] +
		    (pAd->CommonCfg.AddHTInfo.MCSSet[1] << 8) + (StbcMcs << 16);
		if ((pAd->CommonCfg.DesiredHtPhy.TxSTBC) && (pRtHtCap->RxSTBC)
		    && (pAd->Antenna.field.TxPath == 2))
			pMaxHtPhy->field.STBC = STBC_USE;
		else
			pMaxHtPhy->field.STBC = STBC_NONE;
	}

	/* Decide MAX ht rate. */
	if ((pRtHtCap->GF) && (pAd->CommonCfg.DesiredHtPhy.GF))
		pMaxHtPhy->field.MODE = MODE_HTGREENFIELD;
	else
		pMaxHtPhy->field.MODE = MODE_HTMIX;

	if ((pAd->CommonCfg.DesiredHtPhy.ChannelWidth)
	    && (pRtHtCap->ChannelWidth))
		pMaxHtPhy->field.BW = BW_40;
	else
		pMaxHtPhy->field.BW = BW_20;

	if (pMaxHtPhy->field.BW == BW_20)
		pMaxHtPhy->field.ShortGI =
		    (pAd->CommonCfg.DesiredHtPhy.ShortGIfor20 & pRtHtCap->
		     ShortGIfor20);
	else
		pMaxHtPhy->field.ShortGI =
		    (pAd->CommonCfg.DesiredHtPhy.ShortGIfor40 & pRtHtCap->
		     ShortGIfor40);

	if (pDesireHtPhy->MCSSet[4] != 0) {
		pMaxHtPhy->field.MCS = 32;
	}

	for (i = 23; i >= 0; i--)	/* 3*3 */
	{
		j = i / 8;
		bitmask = (1 << (i - (j * 8)));

		if ((pActiveHtPhy->MCSSet[j] & bitmask)
		    && (pDesireHtPhy->MCSSet[j] & bitmask)) {
			pMaxHtPhy->field.MCS = i;
			break;
		}

		if (i == 0)
			break;
	}

	/* Copy MIN ht rate.  rt2860??? */
	pMinHtPhy->field.BW = BW_20;
	pMinHtPhy->field.MCS = 0;
	pMinHtPhy->field.STBC = 0;
	pMinHtPhy->field.ShortGI = 0;
	/*If STA assigns fixed rate. update to fixed here. */
	if ((pAd->OpMode == OPMODE_STA) && (pDesireHtPhy->MCSSet[0] != 0xff)) {
		if (pDesireHtPhy->MCSSet[4] != 0) {
			pMaxHtPhy->field.MCS = 32;
			pMinHtPhy->field.MCS = 32;
			DBGPRINT(RT_DEBUG_TRACE,
				 ("MlmeUpdateHtTxRates<=== Use Fixed MCS = %d\n",
				  pMinHtPhy->field.MCS));
		}

		for (i = 23; (char)i >= 0; i--)	/* 3*3 */
		{
			j = i / 8;
			bitmask = (1 << (i - (j * 8)));
			if ((pDesireHtPhy->MCSSet[j] & bitmask)
			    && (pActiveHtPhy->MCSSet[j] & bitmask)) {
				pMaxHtPhy->field.MCS = i;
				pMinHtPhy->field.MCS = i;
				break;
			}
			if (i == 0)
				break;
		}
	}

	/* Decide ht rate */
	pHtPhy->field.STBC = pMaxHtPhy->field.STBC;
	pHtPhy->field.BW = pMaxHtPhy->field.BW;
	pHtPhy->field.MODE = pMaxHtPhy->field.MODE;
	pHtPhy->field.MCS = pMaxHtPhy->field.MCS;
	pHtPhy->field.ShortGI = pMaxHtPhy->field.ShortGI;

	/* use default now. rt2860 */
	if (pDesireHtPhy->MCSSet[0] != 0xff)
		*auto_rate_cur_p = FALSE;
	else
		*auto_rate_cur_p = TRUE;

	DBGPRINT(RT_DEBUG_TRACE,
		 (" MlmeUpdateHtTxRates<---.AMsduSize = %d  \n",
		  pAd->CommonCfg.DesiredHtPhy.AmsduSize));
	DBGPRINT(RT_DEBUG_TRACE,
		 ("TX: MCS[0] = %x (choose %d), BW = %d, ShortGI = %d, MODE = %d,  \n",
		  pActiveHtPhy->MCSSet[0], pHtPhy->field.MCS, pHtPhy->field.BW,
		  pHtPhy->field.ShortGI, pHtPhy->field.MODE));
	DBGPRINT(RT_DEBUG_TRACE, ("MlmeUpdateHtTxRates<=== \n"));
}

void BATableInit(struct rt_rtmp_adapter *pAd, struct rt_ba_table *Tab)
{
	int i;

	Tab->numAsOriginator = 0;
	Tab->numAsRecipient = 0;
	Tab->numDoneOriginator = 0;
	NdisAllocateSpinLock(&pAd->BATabLock);
	for (i = 0; i < MAX_LEN_OF_BA_REC_TABLE; i++) {
		Tab->BARecEntry[i].REC_BA_Status = Recipient_NONE;
		NdisAllocateSpinLock(&(Tab->BARecEntry[i].RxReRingLock));
	}
	for (i = 0; i < MAX_LEN_OF_BA_ORI_TABLE; i++) {
		Tab->BAOriEntry[i].ORI_BA_Status = Originator_NONE;
	}
}

/* IRQL = DISPATCH_LEVEL */
void MlmeRadioOff(struct rt_rtmp_adapter *pAd)
{
	RTMP_MLME_RADIO_OFF(pAd);
}

/* IRQL = DISPATCH_LEVEL */
void MlmeRadioOn(struct rt_rtmp_adapter *pAd)
{
	RTMP_MLME_RADIO_ON(pAd);
}

/* =========================================================================================== */
/* bss_table.c */
/* =========================================================================================== */

/*! \brief initialize BSS table
 *	\param p_tab pointer to the table
 *	\return none
 *	\pre
 *	\post

 IRQL = PASSIVE_LEVEL
 IRQL = DISPATCH_LEVEL

 */
void BssTableInit(struct rt_bss_table *Tab)
{
	int i;

	Tab->BssNr = 0;
	Tab->BssOverlapNr = 0;
	for (i = 0; i < MAX_LEN_OF_BSS_TABLE; i++) {
		NdisZeroMemory(&Tab->BssEntry[i], sizeof(struct rt_bss_entry));
		Tab->BssEntry[i].Rssi = -127;	/* initial the rssi as a minimum value */
	}
}

/*! \brief search the BSS table by SSID
 *	\param p_tab pointer to the bss table
 *	\param ssid SSID string
 *	\return index of the table, BSS_NOT_FOUND if not in the table
 *	\pre
 *	\post
 *	\note search by sequential search

 IRQL = DISPATCH_LEVEL

 */
unsigned long BssTableSearch(struct rt_bss_table *Tab, u8 *pBssid, u8 Channel)
{
	u8 i;

	for (i = 0; i < Tab->BssNr; i++) {
		/* */
		/* Some AP that support A/B/G mode that may used the same BSSID on 11A and 11B/G. */
		/* We should distinguish this case. */
		/* */
		if ((((Tab->BssEntry[i].Channel <= 14) && (Channel <= 14)) ||
		     ((Tab->BssEntry[i].Channel > 14) && (Channel > 14))) &&
		    MAC_ADDR_EQUAL(Tab->BssEntry[i].Bssid, pBssid)) {
			return i;
		}
	}
	return (unsigned long)BSS_NOT_FOUND;
}

unsigned long BssSsidTableSearch(struct rt_bss_table *Tab,
			 u8 *pBssid,
			 u8 *pSsid, u8 SsidLen, u8 Channel)
{
	u8 i;

	for (i = 0; i < Tab->BssNr; i++) {
		/* */
		/* Some AP that support A/B/G mode that may used the same BSSID on 11A and 11B/G. */
		/* We should distinguish this case. */
		/* */
		if ((((Tab->BssEntry[i].Channel <= 14) && (Channel <= 14)) ||
		     ((Tab->BssEntry[i].Channel > 14) && (Channel > 14))) &&
		    MAC_ADDR_EQUAL(Tab->BssEntry[i].Bssid, pBssid) &&
		    SSID_EQUAL(pSsid, SsidLen, Tab->BssEntry[i].Ssid,
			       Tab->BssEntry[i].SsidLen)) {
			return i;
		}
	}
	return (unsigned long)BSS_NOT_FOUND;
}

unsigned long BssTableSearchWithSSID(struct rt_bss_table *Tab,
			     u8 *Bssid,
			     u8 *pSsid,
			     u8 SsidLen, u8 Channel)
{
	u8 i;

	for (i = 0; i < Tab->BssNr; i++) {
		if ((((Tab->BssEntry[i].Channel <= 14) && (Channel <= 14)) ||
		     ((Tab->BssEntry[i].Channel > 14) && (Channel > 14))) &&
		    MAC_ADDR_EQUAL(&(Tab->BssEntry[i].Bssid), Bssid) &&
		    (SSID_EQUAL
		     (pSsid, SsidLen, Tab->BssEntry[i].Ssid,
		      Tab->BssEntry[i].SsidLen)
		     || (NdisEqualMemory(pSsid, ZeroSsid, SsidLen))
		     ||
		     (NdisEqualMemory
		      (Tab->BssEntry[i].Ssid, ZeroSsid,
		       Tab->BssEntry[i].SsidLen)))) {
			return i;
		}
	}
	return (unsigned long)BSS_NOT_FOUND;
}

unsigned long BssSsidTableSearchBySSID(struct rt_bss_table *Tab,
			       u8 *pSsid, u8 SsidLen)
{
	u8 i;

	for (i = 0; i < Tab->BssNr; i++) {
		if (SSID_EQUAL
		    (pSsid, SsidLen, Tab->BssEntry[i].Ssid,
		     Tab->BssEntry[i].SsidLen)) {
			return i;
		}
	}
	return (unsigned long)BSS_NOT_FOUND;
}

/* IRQL = DISPATCH_LEVEL */
void BssTableDeleteEntry(struct rt_bss_table *Tab,
			 u8 *pBssid, u8 Channel)
{
	u8 i, j;

	for (i = 0; i < Tab->BssNr; i++) {
		if ((Tab->BssEntry[i].Channel == Channel) &&
		    (MAC_ADDR_EQUAL(Tab->BssEntry[i].Bssid, pBssid))) {
			for (j = i; j < Tab->BssNr - 1; j++) {
				NdisMoveMemory(&(Tab->BssEntry[j]),
					       &(Tab->BssEntry[j + 1]),
					       sizeof(struct rt_bss_entry));
			}
			NdisZeroMemory(&(Tab->BssEntry[Tab->BssNr - 1]),
				       sizeof(struct rt_bss_entry));
			Tab->BssNr -= 1;
			return;
		}
	}
}

/*
	========================================================================
	Routine Description:
		Delete the Originator Entry in BAtable. Or decrease numAs Originator by 1 if needed.

	Arguments:
	// IRQL = DISPATCH_LEVEL
	========================================================================
*/
void BATableDeleteORIEntry(struct rt_rtmp_adapter *pAd,
			   struct rt_ba_ori_entry *pBAORIEntry)
{

	if (pBAORIEntry->ORI_BA_Status != Originator_NONE) {
		NdisAcquireSpinLock(&pAd->BATabLock);
		if (pBAORIEntry->ORI_BA_Status == Originator_Done) {
			pAd->BATable.numAsOriginator -= 1;
			DBGPRINT(RT_DEBUG_TRACE,
				 ("BATableDeleteORIEntry numAsOriginator= %ld\n",
				  pAd->BATable.numAsRecipient));
			/* Erase Bitmap flag. */
		}
		pAd->MacTab.Content[pBAORIEntry->Wcid].TXBAbitmap &= (~(1 << (pBAORIEntry->TID)));	/* If STA mode,  erase flag here */
		pAd->MacTab.Content[pBAORIEntry->Wcid].BAOriWcidArray[pBAORIEntry->TID] = 0;	/* If STA mode,  erase flag here */
		pBAORIEntry->ORI_BA_Status = Originator_NONE;
		pBAORIEntry->Token = 1;
		/* Not clear Sequence here. */
		NdisReleaseSpinLock(&pAd->BATabLock);
	}
}

/*! \brief
 *	\param
 *	\return
 *	\pre
 *	\post

 IRQL = DISPATCH_LEVEL

 */
void BssEntrySet(struct rt_rtmp_adapter *pAd, struct rt_bss_entry *pBss, u8 *pBssid, char Ssid[], u8 SsidLen, u8 BssType, u16 BeaconPeriod, struct rt_cf_parm * pCfParm, u16 AtimWin, u16 CapabilityInfo, u8 SupRate[], u8 SupRateLen, u8 ExtRate[], u8 ExtRateLen, struct rt_ht_capability_ie * pHtCapability, struct rt_add_ht_info_ie * pAddHtInfo,	/* AP might use this additional ht info IE */
		 u8 HtCapabilityLen,
		 u8 AddHtInfoLen,
		 u8 NewExtChanOffset,
		 u8 Channel,
		 char Rssi,
		 IN LARGE_INTEGER TimeStamp,
		 u8 CkipFlag,
		 struct rt_edca_parm *pEdcaParm,
		 struct rt_qos_capability_parm *pQosCapability,
		 struct rt_qbss_load_parm *pQbssLoad,
		 u16 LengthVIE, struct rt_ndis_802_11_variable_ies *pVIE)
{
	COPY_MAC_ADDR(pBss->Bssid, pBssid);
	/* Default Hidden SSID to be TRUE, it will be turned to FALSE after coping SSID */
	pBss->Hidden = 1;
	if (SsidLen > 0) {
		/* For hidden SSID AP, it might send beacon with SSID len equal to 0 */
		/* Or send beacon /probe response with SSID len matching real SSID length, */
		/* but SSID is all zero. such as "00-00-00-00" with length 4. */
		/* We have to prevent this case overwrite correct table */
		if (NdisEqualMemory(Ssid, ZeroSsid, SsidLen) == 0) {
			NdisZeroMemory(pBss->Ssid, MAX_LEN_OF_SSID);
			NdisMoveMemory(pBss->Ssid, Ssid, SsidLen);
			pBss->SsidLen = SsidLen;
			pBss->Hidden = 0;
		}
	} else
		pBss->SsidLen = 0;
	pBss->BssType = BssType;
	pBss->BeaconPeriod = BeaconPeriod;
	if (BssType == BSS_INFRA) {
		if (pCfParm->bValid) {
			pBss->CfpCount = pCfParm->CfpCount;
			pBss->CfpPeriod = pCfParm->CfpPeriod;
			pBss->CfpMaxDuration = pCfParm->CfpMaxDuration;
			pBss->CfpDurRemaining = pCfParm->CfpDurRemaining;
		}
	} else {
		pBss->AtimWin = AtimWin;
	}

	pBss->CapabilityInfo = CapabilityInfo;
	/* The privacy bit indicate security is ON, it maight be WEP, TKIP or AES */
	/* Combine with AuthMode, they will decide the connection methods. */
	pBss->Privacy = CAP_IS_PRIVACY_ON(pBss->CapabilityInfo);
	ASSERT(SupRateLen <= MAX_LEN_OF_SUPPORTED_RATES);
	if (SupRateLen <= MAX_LEN_OF_SUPPORTED_RATES)
		NdisMoveMemory(pBss->SupRate, SupRate, SupRateLen);
	else
		NdisMoveMemory(pBss->SupRate, SupRate,
			       MAX_LEN_OF_SUPPORTED_RATES);
	pBss->SupRateLen = SupRateLen;
	ASSERT(ExtRateLen <= MAX_LEN_OF_SUPPORTED_RATES);
	NdisMoveMemory(pBss->ExtRate, ExtRate, ExtRateLen);
	pBss->NewExtChanOffset = NewExtChanOffset;
	pBss->ExtRateLen = ExtRateLen;
	pBss->Channel = Channel;
	pBss->CentralChannel = Channel;
	pBss->Rssi = Rssi;
	/* Update CkipFlag. if not exists, the value is 0x0 */
	pBss->CkipFlag = CkipFlag;

	/* New for microsoft Fixed IEs */
	NdisMoveMemory(pBss->FixIEs.Timestamp, &TimeStamp, 8);
	pBss->FixIEs.BeaconInterval = BeaconPeriod;
	pBss->FixIEs.Capabilities = CapabilityInfo;

	/* New for microsoft Variable IEs */
	if (LengthVIE != 0) {
		pBss->VarIELen = LengthVIE;
		NdisMoveMemory(pBss->VarIEs, pVIE, pBss->VarIELen);
	} else {
		pBss->VarIELen = 0;
	}

	pBss->AddHtInfoLen = 0;
	pBss->HtCapabilityLen = 0;
	if (HtCapabilityLen > 0) {
		pBss->HtCapabilityLen = HtCapabilityLen;
		NdisMoveMemory(&pBss->HtCapability, pHtCapability,
			       HtCapabilityLen);
		if (AddHtInfoLen > 0) {
			pBss->AddHtInfoLen = AddHtInfoLen;
			NdisMoveMemory(&pBss->AddHtInfo, pAddHtInfo,
				       AddHtInfoLen);

			if ((pAddHtInfo->ControlChan > 2)
			    && (pAddHtInfo->AddHtInfo.ExtChanOffset ==
				EXTCHA_BELOW)
			    && (pHtCapability->HtCapInfo.ChannelWidth ==
				BW_40)) {
				pBss->CentralChannel =
				    pAddHtInfo->ControlChan - 2;
			} else
			    if ((pAddHtInfo->AddHtInfo.ExtChanOffset ==
				 EXTCHA_ABOVE)
				&& (pHtCapability->HtCapInfo.ChannelWidth ==
				    BW_40)) {
				pBss->CentralChannel =
				    pAddHtInfo->ControlChan + 2;
			}
		}
	}

	BssCipherParse(pBss);

	/* new for QOS */
	if (pEdcaParm)
		NdisMoveMemory(&pBss->EdcaParm, pEdcaParm, sizeof(struct rt_edca_parm));
	else
		pBss->EdcaParm.bValid = FALSE;
	if (pQosCapability)
		NdisMoveMemory(&pBss->QosCapability, pQosCapability,
			       sizeof(struct rt_qos_capability_parm));
	else
		pBss->QosCapability.bValid = FALSE;
	if (pQbssLoad)
		NdisMoveMemory(&pBss->QbssLoad, pQbssLoad,
			       sizeof(struct rt_qbss_load_parm));
	else
		pBss->QbssLoad.bValid = FALSE;

	{
		struct rt_eid * pEid;
		u16 Length = 0;

		NdisZeroMemory(&pBss->WpaIE.IE[0], MAX_CUSTOM_LEN);
		NdisZeroMemory(&pBss->RsnIE.IE[0], MAX_CUSTOM_LEN);
		pEid = (struct rt_eid *) pVIE;
		while ((Length + 2 + (u16)pEid->Len) <= LengthVIE) {
			switch (pEid->Eid) {
			case IE_WPA:
				if (NdisEqualMemory(pEid->Octet, WPA_OUI, 4)) {
					if ((pEid->Len + 2) > MAX_CUSTOM_LEN) {
						pBss->WpaIE.IELen = 0;
						break;
					}
					pBss->WpaIE.IELen = pEid->Len + 2;
					NdisMoveMemory(pBss->WpaIE.IE, pEid,
						       pBss->WpaIE.IELen);
				}
				break;
			case IE_RSN:
				if (NdisEqualMemory
				    (pEid->Octet + 2, RSN_OUI, 3)) {
					if ((pEid->Len + 2) > MAX_CUSTOM_LEN) {
						pBss->RsnIE.IELen = 0;
						break;
					}
					pBss->RsnIE.IELen = pEid->Len + 2;
					NdisMoveMemory(pBss->RsnIE.IE, pEid,
						       pBss->RsnIE.IELen);
				}
				break;
			}
			Length = Length + 2 + (u16)pEid->Len;	/* Eid[1] + Len[1]+ content[Len] */
			pEid = (struct rt_eid *) ((u8 *) pEid + 2 + pEid->Len);
		}
	}
}

/*!
 *	\brief insert an entry into the bss table
 *	\param p_tab The BSS table
 *	\param Bssid BSSID
 *	\param ssid SSID
 *	\param ssid_len Length of SSID
 *	\param bss_type
 *	\param beacon_period
 *	\param timestamp
 *	\param p_cf
 *	\param atim_win
 *	\param cap
 *	\param rates
 *	\param rates_len
 *	\param channel_idx
 *	\return none
 *	\pre
 *	\post
 *	\note If SSID is identical, the old entry will be replaced by the new one

 IRQL = DISPATCH_LEVEL

 */
unsigned long BssTableSetEntry(struct rt_rtmp_adapter *pAd, struct rt_bss_table *Tab, u8 *pBssid, char Ssid[], u8 SsidLen, u8 BssType, u16 BeaconPeriod, struct rt_cf_parm * CfParm, u16 AtimWin, u16 CapabilityInfo, u8 SupRate[], u8 SupRateLen, u8 ExtRate[], u8 ExtRateLen, struct rt_ht_capability_ie * pHtCapability, struct rt_add_ht_info_ie * pAddHtInfo,	/* AP might use this additional ht info IE */
		       u8 HtCapabilityLen,
		       u8 AddHtInfoLen,
		       u8 NewExtChanOffset,
		       u8 ChannelNo,
		       char Rssi,
		       IN LARGE_INTEGER TimeStamp,
		       u8 CkipFlag,
		       struct rt_edca_parm *pEdcaParm,
		       struct rt_qos_capability_parm *pQosCapability,
		       struct rt_qbss_load_parm *pQbssLoad,
		       u16 LengthVIE, struct rt_ndis_802_11_variable_ies *pVIE)
{
	unsigned long Idx;

	Idx =
	    BssTableSearchWithSSID(Tab, pBssid, (u8 *) Ssid, SsidLen,
				   ChannelNo);
	if (Idx == BSS_NOT_FOUND) {
		if (Tab->BssNr >= MAX_LEN_OF_BSS_TABLE) {
			/* */
			/* It may happen when BSS Table was full. */
			/* The desired AP will not be added into BSS Table */
			/* In this case, if we found the desired AP then overwrite BSS Table. */
			/* */
			if (!OPSTATUS_TEST_FLAG
			    (pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)) {
				if (MAC_ADDR_EQUAL(pAd->MlmeAux.Bssid, pBssid)
				    || SSID_EQUAL(pAd->MlmeAux.Ssid,
						  pAd->MlmeAux.SsidLen, Ssid,
						  SsidLen)) {
					Idx = Tab->BssOverlapNr;
					BssEntrySet(pAd, &Tab->BssEntry[Idx],
						    pBssid, Ssid, SsidLen,
						    BssType, BeaconPeriod,
						    CfParm, AtimWin,
						    CapabilityInfo, SupRate,
						    SupRateLen, ExtRate,
						    ExtRateLen, pHtCapability,
						    pAddHtInfo, HtCapabilityLen,
						    AddHtInfoLen,
						    NewExtChanOffset, ChannelNo,
						    Rssi, TimeStamp, CkipFlag,
						    pEdcaParm, pQosCapability,
						    pQbssLoad, LengthVIE, pVIE);
					Tab->BssOverlapNr =
					    (Tab->BssOverlapNr++) %
					    MAX_LEN_OF_BSS_TABLE;
				}
				return Idx;
			} else {
				return BSS_NOT_FOUND;
			}
		}
		Idx = Tab->BssNr;
		BssEntrySet(pAd, &Tab->BssEntry[Idx], pBssid, Ssid, SsidLen,
			    BssType, BeaconPeriod, CfParm, AtimWin,
			    CapabilityInfo, SupRate, SupRateLen, ExtRate,
			    ExtRateLen, pHtCapability, pAddHtInfo,
			    HtCapabilityLen, AddHtInfoLen, NewExtChanOffset,
			    ChannelNo, Rssi, TimeStamp, CkipFlag, pEdcaParm,
			    pQosCapability, pQbssLoad, LengthVIE, pVIE);
		Tab->BssNr++;
	} else {
		/* avoid  Hidden SSID form beacon to overwirite correct SSID from probe response */
		if ((SSID_EQUAL
		     (Ssid, SsidLen, Tab->BssEntry[Idx].Ssid,
		      Tab->BssEntry[Idx].SsidLen))
		    ||
		    (NdisEqualMemory
		     (Tab->BssEntry[Idx].Ssid, ZeroSsid,
		      Tab->BssEntry[Idx].SsidLen))) {
			BssEntrySet(pAd, &Tab->BssEntry[Idx], pBssid, Ssid,
				    SsidLen, BssType, BeaconPeriod, CfParm,
				    AtimWin, CapabilityInfo, SupRate,
				    SupRateLen, ExtRate, ExtRateLen,
				    pHtCapability, pAddHtInfo, HtCapabilityLen,
				    AddHtInfoLen, NewExtChanOffset, ChannelNo,
				    Rssi, TimeStamp, CkipFlag, pEdcaParm,
				    pQosCapability, pQbssLoad, LengthVIE, pVIE);
		}
	}

	return Idx;
}

/* IRQL = DISPATCH_LEVEL */
void BssTableSsidSort(struct rt_rtmp_adapter *pAd,
		      struct rt_bss_table *OutTab, char Ssid[], u8 SsidLen)
{
	int i;
	BssTableInit(OutTab);

	for (i = 0; i < pAd->ScanTab.BssNr; i++) {
		struct rt_bss_entry *pInBss = &pAd->ScanTab.BssEntry[i];
		BOOLEAN bIsHiddenApIncluded = FALSE;

		if (((pAd->CommonCfg.bIEEE80211H == 1) &&
		     (pAd->MlmeAux.Channel > 14) &&
		     RadarChannelCheck(pAd, pInBss->Channel))
		    ) {
			if (pInBss->Hidden)
				bIsHiddenApIncluded = TRUE;
		}

		if ((pInBss->BssType == pAd->StaCfg.BssType) &&
		    (SSID_EQUAL(Ssid, SsidLen, pInBss->Ssid, pInBss->SsidLen)
		     || bIsHiddenApIncluded)) {
			struct rt_bss_entry *pOutBss = &OutTab->BssEntry[OutTab->BssNr];

			/* 2.4G/5G N only mode */
			if ((pInBss->HtCapabilityLen == 0) &&
			    ((pAd->CommonCfg.PhyMode == PHY_11N_2_4G)
			     || (pAd->CommonCfg.PhyMode == PHY_11N_5G))) {
				DBGPRINT(RT_DEBUG_TRACE,
					 ("STA is in N-only Mode, this AP don't have Ht capability in Beacon.\n"));
				continue;
			}
			/* New for WPA2 */
			/* Check the Authmode first */
			if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA) {
				/* Check AuthMode and AuthModeAux for matching, in case AP support dual-mode */
				if ((pAd->StaCfg.AuthMode != pInBss->AuthMode)
				    && (pAd->StaCfg.AuthMode !=
					pInBss->AuthModeAux))
					/* None matched */
					continue;

				/* Check cipher suite, AP must have more secured cipher than station setting */
				if ((pAd->StaCfg.AuthMode ==
				     Ndis802_11AuthModeWPA)
				    || (pAd->StaCfg.AuthMode ==
					Ndis802_11AuthModeWPAPSK)) {
					/* If it's not mixed mode, we should only let BSS pass with the same encryption */
					if (pInBss->WPA.bMixMode == FALSE)
						if (pAd->StaCfg.WepStatus !=
						    pInBss->WPA.GroupCipher)
							continue;

					/* check group cipher */
					if ((pAd->StaCfg.WepStatus <
					     pInBss->WPA.GroupCipher)
					    && (pInBss->WPA.GroupCipher !=
						Ndis802_11GroupWEP40Enabled)
					    && (pInBss->WPA.GroupCipher !=
						Ndis802_11GroupWEP104Enabled))
						continue;

					/* check pairwise cipher, skip if none matched */
					/* If profile set to AES, let it pass without question. */
					/* If profile set to TKIP, we must find one mateched */
					if ((pAd->StaCfg.WepStatus ==
					     Ndis802_11Encryption2Enabled)
					    && (pAd->StaCfg.WepStatus !=
						pInBss->WPA.PairCipher)
					    && (pAd->StaCfg.WepStatus !=
						pInBss->WPA.PairCipherAux))
						continue;
				} else
				    if ((pAd->StaCfg.AuthMode ==
					 Ndis802_11AuthModeWPA2)
					|| (pAd->StaCfg.AuthMode ==
					    Ndis802_11AuthModeWPA2PSK)) {
					/* If it's not mixed mode, we should only let BSS pass with the same encryption */
					if (pInBss->WPA2.bMixMode == FALSE)
						if (pAd->StaCfg.WepStatus !=
						    pInBss->WPA2.GroupCipher)
							continue;

					/* check group cipher */
					if ((pAd->StaCfg.WepStatus <
					     pInBss->WPA.GroupCipher)
					    && (pInBss->WPA2.GroupCipher !=
						Ndis802_11GroupWEP40Enabled)
					    && (pInBss->WPA2.GroupCipher !=
						Ndis802_11GroupWEP104Enabled))
						continue;

					/* check pairwise cipher, skip if none matched */
					/* If profile set to AES, let it pass without question. */
					/* If profile set to TKIP, we must find one mateched */
					if ((pAd->StaCfg.WepStatus ==
					     Ndis802_11Encryption2Enabled)
					    && (pAd->StaCfg.WepStatus !=
						pInBss->WPA2.PairCipher)
					    && (pAd->StaCfg.WepStatus !=
						pInBss->WPA2.PairCipherAux))
						continue;
				}
			}
			/* Bss Type matched, SSID matched. */
			/* We will check wepstatus for qualification Bss */
			else if (pAd->StaCfg.WepStatus != pInBss->WepStatus) {
				DBGPRINT(RT_DEBUG_TRACE,
					 ("StaCfg.WepStatus=%d, while pInBss->WepStatus=%d\n",
					  pAd->StaCfg.WepStatus,
					  pInBss->WepStatus));
				/* */
				/* For the SESv2 case, we will not qualify WepStatus. */
				/* */
				if (!pInBss->bSES)
					continue;
			}
			/* Since the AP is using hidden SSID, and we are trying to connect to ANY */
			/* It definitely will fail. So, skip it. */
			/* CCX also require not even try to connect it! */
			if (SsidLen == 0)
				continue;

			/* If both station and AP use 40MHz, still need to check if the 40MHZ band's legality in my country region */
			/* If this 40MHz wideband is not allowed in my country list, use bandwidth 20MHZ instead, */
			if ((pInBss->CentralChannel != pInBss->Channel) &&
			    (pAd->CommonCfg.RegTransmitSetting.field.BW ==
			     BW_40)) {
				if (RTMPCheckChannel
				    (pAd, pInBss->CentralChannel,
				     pInBss->Channel) == FALSE) {
					pAd->CommonCfg.RegTransmitSetting.field.
					    BW = BW_20;
					SetCommonHT(pAd);
					pAd->CommonCfg.RegTransmitSetting.field.
					    BW = BW_40;
				} else {
					if (pAd->CommonCfg.DesiredHtPhy.
					    ChannelWidth == BAND_WIDTH_20) {
						SetCommonHT(pAd);
					}
				}
			}
			/* copy matching BSS from InTab to OutTab */
			NdisMoveMemory(pOutBss, pInBss, sizeof(struct rt_bss_entry));

			OutTab->BssNr++;
		} else if ((pInBss->BssType == pAd->StaCfg.BssType)
			   && (SsidLen == 0)) {
			struct rt_bss_entry *pOutBss = &OutTab->BssEntry[OutTab->BssNr];

			/* 2.4G/5G N only mode */
			if ((pInBss->HtCapabilityLen == 0) &&
			    ((pAd->CommonCfg.PhyMode == PHY_11N_2_4G)
			     || (pAd->CommonCfg.PhyMode == PHY_11N_5G))) {
				DBGPRINT(RT_DEBUG_TRACE,
					 ("STA is in N-only Mode, this AP don't have Ht capability in Beacon.\n"));
				continue;
			}
			/* New for WPA2 */
			/* Check the Authmode first */
			if (pAd->StaCfg.AuthMode >= Ndis802_11AuthModeWPA) {
				/* Check AuthMode and AuthModeAux for matching, in case AP support dual-mode */
				if ((pAd->StaCfg.AuthMode != pInBss->AuthMode)
				    && (pAd->StaCfg.AuthMode !=
					pInBss->AuthModeAux))
					/* None matched */
					continue;

				/* Check cipher suite, AP must have more secured cipher than station setting */
				if ((pAd->StaCfg.AuthMode ==
				     Ndis802_11AuthModeWPA)
				    || (pAd->StaCfg.AuthMode ==
					Ndis802_11AuthModeWPAPSK)) {
					/* If it's not mixed mode, we should only let BSS pass with the same encryption */
					if (pInBss->WPA.bMixMode == FALSE)
						if (pAd->StaCfg.WepStatus !=
						    pInBss->WPA.GroupCipher)
							continue;

					/* check group cipher */
					if (pAd->StaCfg.WepStatus <
					    pInBss->WPA.GroupCipher)
						continue;

					/* check pairwise cipher, skip if none matched */
					/* If profile set to AES, let it pass without question. */
					/* If profile set to TKIP, we must find one mateched */
					if ((pAd->StaCfg.WepStatus ==
					     Ndis802_11Encryption2Enabled)
					    && (pAd->StaCfg.WepStatus !=
						pInBss->WPA.PairCipher)
					    && (pAd->StaCfg.WepStatus !=
						pInBss->WPA.PairCipherAux))
						continue;
				} else
				    if ((pAd->StaCfg.AuthMode ==
					 Ndis802_11AuthModeWPA2)
					|| (pAd->StaCfg.AuthMode ==
					    Ndis802_11AuthModeWPA2PSK)) {
					/* If it's not mixed mode, we should only let BSS pass with the same encryption */
					if (pInBss->WPA2.bMixMode == FALSE)
						if (pAd->StaCfg.WepStatus !=
						    pInBss->WPA2.GroupCipher)
							continue;

					/* check group cipher */
					if (pAd->StaCfg.WepStatus <
					    pInBss->WPA2.GroupCipher)
						continue;

					/* check pairwise cipher, skip if none matched */
					/* If profile set to AES, let it pass without question. */
					/* If profile set to TKIP, we must find one mateched */
					if ((pAd->StaCfg.WepStatus ==
					     Ndis802_11Encryption2Enabled)
					    && (pAd->StaCfg.WepStatus !=
						pInBss->WPA2.PairCipher)
					    && (pAd->StaCfg.WepStatus !=
						pInBss->WPA2.PairCipherAux))
						continue;
				}
			}
			/* Bss Type matched, SSID matched. */
			/* We will check wepstatus for qualification Bss */
			else if (pAd->StaCfg.WepStatus != pInBss->WepStatus)
				continue;

			/* If both station and AP use 40MHz, still need to check if the 40MHZ band's legality in my country region */
			/* If this 40MHz wideband is not allowed in my country list, use bandwidth 20MHZ instead, */
			if ((pInBss->CentralChannel != pInBss->Channel) &&
			    (pAd->CommonCfg.RegTransmitSetting.field.BW ==
			     BW_40)) {
				if (RTMPCheckChannel
				    (pAd, pInBss->CentralChannel,
				     pInBss->Channel) == FALSE) {
					pAd->CommonCfg.RegTransmitSetting.field.
					    BW = BW_20;
					SetCommonHT(pAd);
					pAd->CommonCfg.RegTransmitSetting.field.
					    BW = BW_40;
				}
			}
			/* copy matching BSS from InTab to OutTab */
			NdisMoveMemory(pOutBss, pInBss, sizeof(struct rt_bss_entry));

			OutTab->BssNr++;
		}

		if (OutTab->BssNr >= MAX_LEN_OF_BSS_TABLE)
			break;
	}

	BssTableSortByRssi(OutTab);
}

/* IRQL = DISPATCH_LEVEL */
void BssTableSortByRssi(struct rt_bss_table *OutTab)
{
	int i, j;
	struct rt_bss_entry TmpBss;

	for (i = 0; i < OutTab->BssNr - 1; i++) {
		for (j = i + 1; j < OutTab->BssNr; j++) {
			if (OutTab->BssEntry[j].Rssi > OutTab->BssEntry[i].Rssi) {
				NdisMoveMemory(&TmpBss, &OutTab->BssEntry[j],
					       sizeof(struct rt_bss_entry));
				NdisMoveMemory(&OutTab->BssEntry[j],
					       &OutTab->BssEntry[i],
					       sizeof(struct rt_bss_entry));
				NdisMoveMemory(&OutTab->BssEntry[i], &TmpBss,
					       sizeof(struct rt_bss_entry));
			}
		}
	}
}

void BssCipherParse(struct rt_bss_entry *pBss)
{
	struct rt_eid * pEid;
	u8 *pTmp;
	struct rt_rsn_ie_header * pRsnHeader;
	struct rt_cipher_suite_struct * pCipher;
	struct rt_akm_suite * pAKM;
	u16 Count;
	int Length;
	NDIS_802_11_ENCRYPTION_STATUS TmpCipher;

	/* */
	/* WepStatus will be reset later, if AP announce TKIP or AES on the beacon frame. */
	/* */
	if (pBss->Privacy) {
		pBss->WepStatus = Ndis802_11WEPEnabled;
	} else {
		pBss->WepStatus = Ndis802_11WEPDisabled;
	}
	/* Set default to disable & open authentication before parsing variable IE */
	pBss->AuthMode = Ndis802_11AuthModeOpen;
	pBss->AuthModeAux = Ndis802_11AuthModeOpen;

	/* Init WPA setting */
	pBss->WPA.PairCipher = Ndis802_11WEPDisabled;
	pBss->WPA.PairCipherAux = Ndis802_11WEPDisabled;
	pBss->WPA.GroupCipher = Ndis802_11WEPDisabled;
	pBss->WPA.RsnCapability = 0;
	pBss->WPA.bMixMode = FALSE;

	/* Init WPA2 setting */
	pBss->WPA2.PairCipher = Ndis802_11WEPDisabled;
	pBss->WPA2.PairCipherAux = Ndis802_11WEPDisabled;
	pBss->WPA2.GroupCipher = Ndis802_11WEPDisabled;
	pBss->WPA2.RsnCapability = 0;
	pBss->WPA2.bMixMode = FALSE;

	Length = (int)pBss->VarIELen;

	while (Length > 0) {
		/* Parse cipher suite base on WPA1 & WPA2, they should be parsed differently */
		pTmp = ((u8 *)pBss->VarIEs) + pBss->VarIELen - Length;
		pEid = (struct rt_eid *) pTmp;
		switch (pEid->Eid) {
		case IE_WPA:
			if (NdisEqualMemory(pEid->Octet, SES_OUI, 3)
			    && (pEid->Len == 7)) {
				pBss->bSES = TRUE;
				break;
			} else if (NdisEqualMemory(pEid->Octet, WPA_OUI, 4) !=
				   1) {
				/* if unsupported vendor specific IE */
				break;
			}
			/* Skip OUI, version, and multicast suite */
			/* This part should be improved in the future when AP supported multiple cipher suite. */
			/* For now, it's OK since almost all APs have fixed cipher suite supported. */
			/* pTmp = (u8 *)pEid->Octet; */
			pTmp += 11;

			/* Cipher Suite Selectors from Spec P802.11i/D3.2 P26. */
			/*      Value      Meaning */
			/*      0                       None */
			/*      1                       WEP-40 */
			/*      2                       Tkip */
			/*      3                       WRAP */
			/*      4                       AES */
			/*      5                       WEP-104 */
			/* Parse group cipher */
			switch (*pTmp) {
			case 1:
				pBss->WPA.GroupCipher =
				    Ndis802_11GroupWEP40Enabled;
				break;
			case 5:
				pBss->WPA.GroupCipher =
				    Ndis802_11GroupWEP104Enabled;
				break;
			case 2:
				pBss->WPA.GroupCipher =
				    Ndis802_11Encryption2Enabled;
				break;
			case 4:
				pBss->WPA.GroupCipher =
				    Ndis802_11Encryption3Enabled;
				break;
			default:
				break;
			}
			/* number of unicast suite */
			pTmp += 1;

			/* skip all unicast cipher suites */
			/*Count = *(u16 *)pTmp; */
			Count = (pTmp[1] << 8) + pTmp[0];
			pTmp += sizeof(u16);

			/* Parsing all unicast cipher suite */
			while (Count > 0) {
				/* Skip OUI */
				pTmp += 3;
				TmpCipher = Ndis802_11WEPDisabled;
				switch (*pTmp) {
				case 1:
				case 5:	/* Although WEP is not allowed in WPA related auth mode, we parse it anyway */
					TmpCipher =
					    Ndis802_11Encryption1Enabled;
					break;
				case 2:
					TmpCipher =
					    Ndis802_11Encryption2Enabled;
					break;
				case 4:
					TmpCipher =
					    Ndis802_11Encryption3Enabled;
					break;
				default:
					break;
				}
				if (TmpCipher > pBss->WPA.PairCipher) {
					/* Move the lower cipher suite to PairCipherAux */
					pBss->WPA.PairCipherAux =
					    pBss->WPA.PairCipher;
					pBss->WPA.PairCipher = TmpCipher;
				} else {
					pBss->WPA.PairCipherAux = TmpCipher;
				}
				pTmp++;
				Count--;
			}

			/* 4. get AKM suite counts */
			/*Count = *(u16 *)pTmp; */
			Count = (pTmp[1] << 8) + pTmp[0];
			pTmp += sizeof(u16);
			pTmp += 3;

			switch (*pTmp) {
			case 1:
				/* Set AP support WPA-enterprise mode */
				if (pBss->AuthMode == Ndis802_11AuthModeOpen)
					pBss->AuthMode = Ndis802_11AuthModeWPA;
				else
					pBss->AuthModeAux =
					    Ndis802_11AuthModeWPA;
				break;
			case 2:
				/* Set AP support WPA-PSK mode */
				if (pBss->AuthMode == Ndis802_11AuthModeOpen)
					pBss->AuthMode =
					    Ndis802_11AuthModeWPAPSK;
				else
					pBss->AuthModeAux =
					    Ndis802_11AuthModeWPAPSK;
				break;
			default:
				break;
			}
			pTmp += 1;

			/* Fixed for WPA-None */
			if (pBss->BssType == BSS_ADHOC) {
				pBss->AuthMode = Ndis802_11AuthModeWPANone;
				pBss->AuthModeAux = Ndis802_11AuthModeWPANone;
				pBss->WepStatus = pBss->WPA.GroupCipher;
				/* Patched bugs for old driver */
				if (pBss->WPA.PairCipherAux ==
				    Ndis802_11WEPDisabled)
					pBss->WPA.PairCipherAux =
					    pBss->WPA.GroupCipher;
			} else
				pBss->WepStatus = pBss->WPA.PairCipher;

			/* Check the Pair & Group, if different, turn on mixed mode flag */
			if (pBss->WPA.GroupCipher != pBss->WPA.PairCipher)
				pBss->WPA.bMixMode = TRUE;

			break;

		case IE_RSN:
			pRsnHeader = (struct rt_rsn_ie_header *) pTmp;

			/* 0. Version must be 1 */
			if (le2cpu16(pRsnHeader->Version) != 1)
				break;
			pTmp += sizeof(struct rt_rsn_ie_header);

			/* 1. Check group cipher */
			pCipher = (struct rt_cipher_suite_struct *) pTmp;
			if (!RTMPEqualMemory(pTmp, RSN_OUI, 3))
				break;

			/* Parse group cipher */
			switch (pCipher->Type) {
			case 1:
				pBss->WPA2.GroupCipher =
				    Ndis802_11GroupWEP40Enabled;
				break;
			case 5:
				pBss->WPA2.GroupCipher =
				    Ndis802_11GroupWEP104Enabled;
				break;
			case 2:
				pBss->WPA2.GroupCipher =
				    Ndis802_11Encryption2Enabled;
				break;
			case 4:
				pBss->WPA2.GroupCipher =
				    Ndis802_11Encryption3Enabled;
				break;
			default:
				break;
			}
			/* set to correct offset for next parsing */
			pTmp += sizeof(struct rt_cipher_suite_struct);

			/* 2. Get pairwise cipher counts */
			/*Count = *(u16 *)pTmp; */
			Count = (pTmp[1] << 8) + pTmp[0];
			pTmp += sizeof(u16);

			/* 3. Get pairwise cipher */
			/* Parsing all unicast cipher suite */
			while (Count > 0) {
				/* Skip OUI */
				pCipher = (struct rt_cipher_suite_struct *) pTmp;
				TmpCipher = Ndis802_11WEPDisabled;
				switch (pCipher->Type) {
				case 1:
				case 5:	/* Although WEP is not allowed in WPA related auth mode, we parse it anyway */
					TmpCipher =
					    Ndis802_11Encryption1Enabled;
					break;
				case 2:
					TmpCipher =
					    Ndis802_11Encryption2Enabled;
					break;
				case 4:
					TmpCipher =
					    Ndis802_11Encryption3Enabled;
					break;
				default:
					break;
				}
				if (TmpCipher > pBss->WPA2.PairCipher) {
					/* Move the lower cipher suite to PairCipherAux */
					pBss->WPA2.PairCipherAux =
					    pBss->WPA2.PairCipher;
					pBss->WPA2.PairCipher = TmpCipher;
				} else {
					pBss->WPA2.PairCipherAux = TmpCipher;
				}
				pTmp += sizeof(struct rt_cipher_suite_struct);
				Count--;
			}

			/* 4. get AKM suite counts */
			/*Count = *(u16 *)pTmp; */
			Count = (pTmp[1] << 8) + pTmp[0];
			pTmp += sizeof(u16);

			/* 5. Get AKM ciphers */
			/* Parsing all AKM ciphers */
			while (Count > 0) {
				pAKM = (struct rt_akm_suite *) pTmp;
				if (!RTMPEqualMemory(pTmp, RSN_OUI, 3))
					break;

				switch (pAKM->Type) {
				case 1:
					/* Set AP support WPA-enterprise mode */
					if (pBss->AuthMode ==
					    Ndis802_11AuthModeOpen)
						pBss->AuthMode =
						    Ndis802_11AuthModeWPA2;
					else
						pBss->AuthModeAux =
						    Ndis802_11AuthModeWPA2;
					break;
				case 2:
					/* Set AP support WPA-PSK mode */
					if (pBss->AuthMode ==
					    Ndis802_11AuthModeOpen)
						pBss->AuthMode =
						    Ndis802_11AuthModeWPA2PSK;
					else
						pBss->AuthModeAux =
						    Ndis802_11AuthModeWPA2PSK;
					break;
				default:
					if (pBss->AuthMode ==
					    Ndis802_11AuthModeOpen)
						pBss->AuthMode =
						    Ndis802_11AuthModeMax;
					else
						pBss->AuthModeAux =
						    Ndis802_11AuthModeMax;
					break;
				}
				pTmp += (Count * sizeof(struct rt_akm_suite));
				Count--;
			}

			/* Fixed for WPA-None */
			if (pBss->BssType == BSS_ADHOC) {
				pBss->AuthMode = Ndis802_11AuthModeWPANone;
				pBss->AuthModeAux = Ndis802_11AuthModeWPANone;
				pBss->WPA.PairCipherAux =
				    pBss->WPA2.PairCipherAux;
				pBss->WPA.GroupCipher = pBss->WPA2.GroupCipher;
				pBss->WepStatus = pBss->WPA.GroupCipher;
				/* Patched bugs for old driver */
				if (pBss->WPA.PairCipherAux ==
				    Ndis802_11WEPDisabled)
					pBss->WPA.PairCipherAux =
					    pBss->WPA.GroupCipher;
			}
			pBss->WepStatus = pBss->WPA2.PairCipher;

			/* 6. Get RSN capability */
			/*pBss->WPA2.RsnCapability = *(u16 *)pTmp; */
			pBss->WPA2.RsnCapability = (pTmp[1] << 8) + pTmp[0];
			pTmp += sizeof(u16);

			/* Check the Pair & Group, if different, turn on mixed mode flag */
			if (pBss->WPA2.GroupCipher != pBss->WPA2.PairCipher)
				pBss->WPA2.bMixMode = TRUE;

			break;
		default:
			break;
		}
		Length -= (pEid->Len + 2);
	}
}

/* =========================================================================================== */
/* mac_table.c */
/* =========================================================================================== */

/*! \brief generates a random mac address value for IBSS BSSID
 *	\param Addr the bssid location
 *	\return none
 *	\pre
 *	\post
 */
void MacAddrRandomBssid(struct rt_rtmp_adapter *pAd, u8 *pAddr)
{
	int i;

	for (i = 0; i < MAC_ADDR_LEN; i++) {
		pAddr[i] = RandomByte(pAd);
	}

	pAddr[0] = (pAddr[0] & 0xfe) | 0x02;	/* the first 2 bits must be 01xxxxxxxx */
}

/*! \brief init the management mac frame header
 *	\param p_hdr mac header
 *	\param subtype subtype of the frame
 *	\param p_ds destination address, don't care if it is a broadcast address
 *	\return none
 *	\pre the station has the following information in the pAd->StaCfg
 *	 - bssid
 *	 - station address
 *	\post
 *	\note this function initializes the following field

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

 */
void MgtMacHeaderInit(struct rt_rtmp_adapter *pAd,
		      struct rt_header_802_11 * pHdr80211,
		      u8 SubType,
		      u8 ToDs, u8 *pDA, u8 *pBssid)
{
	NdisZeroMemory(pHdr80211, sizeof(struct rt_header_802_11));

	pHdr80211->FC.Type = BTYPE_MGMT;
	pHdr80211->FC.SubType = SubType;
/*      if (SubType == SUBTYPE_ACK)     // sample, no use, it will conflict with ACTION frame sub type */
/*              pHdr80211->FC.Type = BTYPE_CNTL; */
	pHdr80211->FC.ToDs = ToDs;
	COPY_MAC_ADDR(pHdr80211->Addr1, pDA);
	COPY_MAC_ADDR(pHdr80211->Addr2, pAd->CurrentAddress);
	COPY_MAC_ADDR(pHdr80211->Addr3, pBssid);
}

/* =========================================================================================== */
/* mem_mgmt.c */
/* =========================================================================================== */

/*!***************************************************************************
 * This routine build an outgoing frame, and fill all information specified
 * in argument list to the frame body. The actual frame size is the summation
 * of all arguments.
 * input params:
 *		Buffer - pointer to a pre-allocated memory segment
 *		args - a list of <int arg_size, arg> pairs.
 *		NOTE NOTE NOTE! the last argument must be NULL, otherwise this
 *						   function will FAIL!
 * return:
 *		Size of the buffer
 * usage:
 *		MakeOutgoingFrame(Buffer, output_length, 2, &fc, 2, &dur, 6, p_addr1, 6,p_addr2, END_OF_ARGS);

 IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

 ****************************************************************************/
unsigned long MakeOutgoingFrame(u8 * Buffer, unsigned long * FrameLen, ...)
{
	u8 *p;
	int leng;
	unsigned long TotLeng;
	va_list Args;

	/* calculates the total length */
	TotLeng = 0;
	va_start(Args, FrameLen);
	do {
		leng = va_arg(Args, int);
		if (leng == END_OF_ARGS) {
			break;
		}
		p = va_arg(Args, void *);
		NdisMoveMemory(&Buffer[TotLeng], p, leng);
		TotLeng = TotLeng + leng;
	} while (TRUE);

	va_end(Args);		/* clean up */
	*FrameLen = TotLeng;
	return TotLeng;
}

/* =========================================================================================== */
/* mlme_queue.c */
/* =========================================================================================== */

/*! \brief	Initialize The MLME Queue, used by MLME Functions
 *	\param	*Queue	   The MLME Queue
 *	\return Always	   Return NDIS_STATE_SUCCESS in this implementation
 *	\pre
 *	\post
 *	\note	Because this is done only once (at the init stage), no need to be locked

 IRQL = PASSIVE_LEVEL

 */
int MlmeQueueInit(struct rt_mlme_queue *Queue)
{
	int i;

	NdisAllocateSpinLock(&Queue->Lock);

	Queue->Num = 0;
	Queue->Head = 0;
	Queue->Tail = 0;

	for (i = 0; i < MAX_LEN_OF_MLME_QUEUE; i++) {
		Queue->Entry[i].Occupied = FALSE;
		Queue->Entry[i].MsgLen = 0;
		NdisZeroMemory(Queue->Entry[i].Msg, MGMT_DMA_BUFFER_SIZE);
	}

	return NDIS_STATUS_SUCCESS;
}

/*! \brief	 Enqueue a message for other threads, if they want to send messages to MLME thread
 *	\param	*Queue	  The MLME Queue
 *	\param	 Machine  The State Machine Id
 *	\param	 MsgType  The Message Type
 *	\param	 MsgLen   The Message length
 *	\param	*Msg	  The message pointer
 *	\return  TRUE if enqueue is successful, FALSE if the queue is full
 *	\pre
 *	\post
 *	\note	 The message has to be initialized

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

 */
BOOLEAN MlmeEnqueue(struct rt_rtmp_adapter *pAd,
		    unsigned long Machine,
		    unsigned long MsgType, unsigned long MsgLen, void * Msg)
{
	int Tail;
	struct rt_mlme_queue *Queue = (struct rt_mlme_queue *)& pAd->Mlme.Queue;

	/* Do nothing if the driver is starting halt state. */
	/* This might happen when timer already been fired before cancel timer with mlmehalt */
	if (RTMP_TEST_FLAG
	    (pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return FALSE;

	/* First check the size, it MUST not exceed the mlme queue size */
	if (MsgLen > MGMT_DMA_BUFFER_SIZE) {
		DBGPRINT_ERR(("MlmeEnqueue: msg too large, size = %ld \n",
			      MsgLen));
		return FALSE;
	}

	if (MlmeQueueFull(Queue)) {
		return FALSE;
	}

	NdisAcquireSpinLock(&(Queue->Lock));
	Tail = Queue->Tail;
	Queue->Tail++;
	Queue->Num++;
	if (Queue->Tail == MAX_LEN_OF_MLME_QUEUE) {
		Queue->Tail = 0;
	}

	Queue->Entry[Tail].Wcid = RESERVED_WCID;
	Queue->Entry[Tail].Occupied = TRUE;
	Queue->Entry[Tail].Machine = Machine;
	Queue->Entry[Tail].MsgType = MsgType;
	Queue->Entry[Tail].MsgLen = MsgLen;

	if (Msg != NULL) {
		NdisMoveMemory(Queue->Entry[Tail].Msg, Msg, MsgLen);
	}

	NdisReleaseSpinLock(&(Queue->Lock));
	return TRUE;
}

/*! \brief	 This function is used when Recv gets a MLME message
 *	\param	*Queue			 The MLME Queue
 *	\param	 TimeStampHigh	 The upper 32 bit of timestamp
 *	\param	 TimeStampLow	 The lower 32 bit of timestamp
 *	\param	 Rssi			 The receiving RSSI strength
 *	\param	 MsgLen			 The length of the message
 *	\param	*Msg			 The message pointer
 *	\return  TRUE if everything ok, FALSE otherwise (like Queue Full)
 *	\pre
 *	\post

 IRQL = DISPATCH_LEVEL

 */
BOOLEAN MlmeEnqueueForRecv(struct rt_rtmp_adapter *pAd,
			   unsigned long Wcid,
			   unsigned long TimeStampHigh,
			   unsigned long TimeStampLow,
			   u8 Rssi0,
			   u8 Rssi1,
			   u8 Rssi2,
			   unsigned long MsgLen, void * Msg, u8 Signal)
{
	int Tail, Machine;
	struct rt_frame_802_11 * pFrame = (struct rt_frame_802_11 *) Msg;
	int MsgType;
	struct rt_mlme_queue *Queue = (struct rt_mlme_queue *)& pAd->Mlme.Queue;

	/* Do nothing if the driver is starting halt state. */
	/* This might happen when timer already been fired before cancel timer with mlmehalt */
	if (RTMP_TEST_FLAG
	    (pAd,
	     fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST)) {
		DBGPRINT_ERR(("MlmeEnqueueForRecv: fRTMP_ADAPTER_HALT_IN_PROGRESS\n"));
		return FALSE;
	}
	/* First check the size, it MUST not exceed the mlme queue size */
	if (MsgLen > MGMT_DMA_BUFFER_SIZE) {
		DBGPRINT_ERR(("MlmeEnqueueForRecv: frame too large, size = %ld \n", MsgLen));
		return FALSE;
	}

	if (MlmeQueueFull(Queue)) {
		return FALSE;
	}

	{
		if (!MsgTypeSubst(pAd, pFrame, &Machine, &MsgType)) {
			DBGPRINT_ERR(("MlmeEnqueueForRecv: un-recongnized mgmt->subtype=%d\n", pFrame->Hdr.FC.SubType));
			return FALSE;
		}
	}

	/* OK, we got all the informations, it is time to put things into queue */
	NdisAcquireSpinLock(&(Queue->Lock));
	Tail = Queue->Tail;
	Queue->Tail++;
	Queue->Num++;
	if (Queue->Tail == MAX_LEN_OF_MLME_QUEUE) {
		Queue->Tail = 0;
	}
	Queue->Entry[Tail].Occupied = TRUE;
	Queue->Entry[Tail].Machine = Machine;
	Queue->Entry[Tail].MsgType = MsgType;
	Queue->Entry[Tail].MsgLen = MsgLen;
	Queue->Entry[Tail].TimeStamp.u.LowPart = TimeStampLow;
	Queue->Entry[Tail].TimeStamp.u.HighPart = TimeStampHigh;
	Queue->Entry[Tail].Rssi0 = Rssi0;
	Queue->Entry[Tail].Rssi1 = Rssi1;
	Queue->Entry[Tail].Rssi2 = Rssi2;
	Queue->Entry[Tail].Signal = Signal;
	Queue->Entry[Tail].Wcid = (u8)Wcid;

	Queue->Entry[Tail].Channel = pAd->LatchRfRegs.Channel;

	if (Msg != NULL) {
		NdisMoveMemory(Queue->Entry[Tail].Msg, Msg, MsgLen);
	}

	NdisReleaseSpinLock(&(Queue->Lock));

	RTMP_MLME_HANDLER(pAd);

	return TRUE;
}

/*! \brief	 Dequeue a message from the MLME Queue
 *	\param	*Queue	  The MLME Queue
 *	\param	*Elem	  The message dequeued from MLME Queue
 *	\return  TRUE if the Elem contains something, FALSE otherwise
 *	\pre
 *	\post

 IRQL = DISPATCH_LEVEL

 */
BOOLEAN MlmeDequeue(struct rt_mlme_queue *Queue, struct rt_mlme_queue_elem ** Elem)
{
	NdisAcquireSpinLock(&(Queue->Lock));
	*Elem = &(Queue->Entry[Queue->Head]);
	Queue->Num--;
	Queue->Head++;
	if (Queue->Head == MAX_LEN_OF_MLME_QUEUE) {
		Queue->Head = 0;
	}
	NdisReleaseSpinLock(&(Queue->Lock));
	return TRUE;
}

/* IRQL = DISPATCH_LEVEL */
void MlmeRestartStateMachine(struct rt_rtmp_adapter *pAd)
{
#ifdef RTMP_MAC_PCI
	struct rt_mlme_queue_elem *Elem = NULL;
#endif /* RTMP_MAC_PCI // */
	BOOLEAN Cancelled;

	DBGPRINT(RT_DEBUG_TRACE, ("MlmeRestartStateMachine \n"));

#ifdef RTMP_MAC_PCI
	NdisAcquireSpinLock(&pAd->Mlme.TaskLock);
	if (pAd->Mlme.bRunning) {
		NdisReleaseSpinLock(&pAd->Mlme.TaskLock);
		return;
	} else {
		pAd->Mlme.bRunning = TRUE;
	}
	NdisReleaseSpinLock(&pAd->Mlme.TaskLock);

	/* Remove all Mlme queues elements */
	while (!MlmeQueueEmpty(&pAd->Mlme.Queue)) {
		/*From message type, determine which state machine I should drive */
		if (MlmeDequeue(&pAd->Mlme.Queue, &Elem)) {
			/* free MLME element */
			Elem->Occupied = FALSE;
			Elem->MsgLen = 0;

		} else {
			DBGPRINT_ERR(("MlmeRestartStateMachine: MlmeQueue empty\n"));
		}
	}
#endif /* RTMP_MAC_PCI // */

	{
		/* Cancel all timer events */
		/* Be careful to cancel new added timer */
		RTMPCancelTimer(&pAd->MlmeAux.AssocTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ReassocTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.DisassocTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.AuthTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.BeaconTimer, &Cancelled);
		RTMPCancelTimer(&pAd->MlmeAux.ScanTimer, &Cancelled);

	}

	/* Change back to original channel in case of doing scan */
	AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
	AsicLockChannel(pAd, pAd->CommonCfg.Channel);

	/* Resume MSDU which is turned off durning scan */
	RTMPResumeMsduTransmission(pAd);

	{
		/* Set all state machines back IDLE */
		pAd->Mlme.CntlMachine.CurrState = CNTL_IDLE;
		pAd->Mlme.AssocMachine.CurrState = ASSOC_IDLE;
		pAd->Mlme.AuthMachine.CurrState = AUTH_REQ_IDLE;
		pAd->Mlme.AuthRspMachine.CurrState = AUTH_RSP_IDLE;
		pAd->Mlme.SyncMachine.CurrState = SYNC_IDLE;
		pAd->Mlme.ActMachine.CurrState = ACT_IDLE;
	}

#ifdef RTMP_MAC_PCI
	/* Remove running state */
	NdisAcquireSpinLock(&pAd->Mlme.TaskLock);
	pAd->Mlme.bRunning = FALSE;
	NdisReleaseSpinLock(&pAd->Mlme.TaskLock);
#endif /* RTMP_MAC_PCI // */
}

/*! \brief	test if the MLME Queue is empty
 *	\param	*Queue	  The MLME Queue
 *	\return TRUE if the Queue is empty, FALSE otherwise
 *	\pre
 *	\post

 IRQL = DISPATCH_LEVEL

 */
BOOLEAN MlmeQueueEmpty(struct rt_mlme_queue *Queue)
{
	BOOLEAN Ans;

	NdisAcquireSpinLock(&(Queue->Lock));
	Ans = (Queue->Num == 0);
	NdisReleaseSpinLock(&(Queue->Lock));

	return Ans;
}

/*! \brief	 test if the MLME Queue is full
 *	\param	 *Queue		 The MLME Queue
 *	\return  TRUE if the Queue is empty, FALSE otherwise
 *	\pre
 *	\post

 IRQL = PASSIVE_LEVEL
 IRQL = DISPATCH_LEVEL

 */
BOOLEAN MlmeQueueFull(struct rt_mlme_queue *Queue)
{
	BOOLEAN Ans;

	NdisAcquireSpinLock(&(Queue->Lock));
	Ans = (Queue->Num == MAX_LEN_OF_MLME_QUEUE
	       || Queue->Entry[Queue->Tail].Occupied);
	NdisReleaseSpinLock(&(Queue->Lock));

	return Ans;
}

/*! \brief	 The destructor of MLME Queue
 *	\param
 *	\return
 *	\pre
 *	\post
 *	\note	Clear Mlme Queue, Set Queue->Num to Zero.

 IRQL = PASSIVE_LEVEL

 */
void MlmeQueueDestroy(struct rt_mlme_queue *pQueue)
{
	NdisAcquireSpinLock(&(pQueue->Lock));
	pQueue->Num = 0;
	pQueue->Head = 0;
	pQueue->Tail = 0;
	NdisReleaseSpinLock(&(pQueue->Lock));
	NdisFreeSpinLock(&(pQueue->Lock));
}

/*! \brief	 To substitute the message type if the message is coming from external
 *	\param	pFrame		   The frame received
 *	\param	*Machine	   The state machine
 *	\param	*MsgType	   the message type for the state machine
 *	\return TRUE if the substitution is successful, FALSE otherwise
 *	\pre
 *	\post

 IRQL = DISPATCH_LEVEL

 */
BOOLEAN MsgTypeSubst(struct rt_rtmp_adapter *pAd,
		     struct rt_frame_802_11 * pFrame,
		     int * Machine, int * MsgType)
{
	u16 Seq, Alg;
	u8 EAPType;
	u8 *pData;

	/* Pointer to start of data frames including SNAP header */
	pData = (u8 *)pFrame + LENGTH_802_11;

	/* The only data type will pass to this function is EAPOL frame */
	if (pFrame->Hdr.FC.Type == BTYPE_DATA) {
		{
			*Machine = WPA_STATE_MACHINE;
			EAPType =
			    *((u8 *) pFrame + LENGTH_802_11 +
			      LENGTH_802_1_H + 1);
			return (WpaMsgTypeSubst(EAPType, (int *) MsgType));
		}
	}

	switch (pFrame->Hdr.FC.SubType) {
	case SUBTYPE_ASSOC_REQ:
		*Machine = ASSOC_STATE_MACHINE;
		*MsgType = MT2_PEER_ASSOC_REQ;
		break;
	case SUBTYPE_ASSOC_RSP:
		*Machine = ASSOC_STATE_MACHINE;
		*MsgType = MT2_PEER_ASSOC_RSP;
		break;
	case SUBTYPE_REASSOC_REQ:
		*Machine = ASSOC_STATE_MACHINE;
		*MsgType = MT2_PEER_REASSOC_REQ;
		break;
	case SUBTYPE_REASSOC_RSP:
		*Machine = ASSOC_STATE_MACHINE;
		*MsgType = MT2_PEER_REASSOC_RSP;
		break;
	case SUBTYPE_PROBE_REQ:
		*Machine = SYNC_STATE_MACHINE;
		*MsgType = MT2_PEER_PROBE_REQ;
		break;
	case SUBTYPE_PROBE_RSP:
		*Machine = SYNC_STATE_MACHINE;
		*MsgType = MT2_PEER_PROBE_RSP;
		break;
	case SUBTYPE_BEACON:
		*Machine = SYNC_STATE_MACHINE;
		*MsgType = MT2_PEER_BEACON;
		break;
	case SUBTYPE_ATIM:
		*Machine = SYNC_STATE_MACHINE;
		*MsgType = MT2_PEER_ATIM;
		break;
	case SUBTYPE_DISASSOC:
		*Machine = ASSOC_STATE_MACHINE;
		*MsgType = MT2_PEER_DISASSOC_REQ;
		break;
	case SUBTYPE_AUTH:
		/* get the sequence number from payload 24 Mac Header + 2 bytes algorithm */
		NdisMoveMemory(&Seq, &pFrame->Octet[2], sizeof(u16));
		NdisMoveMemory(&Alg, &pFrame->Octet[0], sizeof(u16));
		if (Seq == 1 || Seq == 3) {
			*Machine = AUTH_RSP_STATE_MACHINE;
			*MsgType = MT2_PEER_AUTH_ODD;
		} else if (Seq == 2 || Seq == 4) {
			if (Alg == AUTH_MODE_OPEN || Alg == AUTH_MODE_KEY) {
				*Machine = AUTH_STATE_MACHINE;
				*MsgType = MT2_PEER_AUTH_EVEN;
			}
		} else {
			return FALSE;
		}
		break;
	case SUBTYPE_DEAUTH:
		*Machine = AUTH_RSP_STATE_MACHINE;
		*MsgType = MT2_PEER_DEAUTH;
		break;
	case SUBTYPE_ACTION:
		*Machine = ACTION_STATE_MACHINE;
		/*  Sometimes Sta will return with category bytes with MSB = 1, if they receive catogory out of their support */
		if ((pFrame->Octet[0] & 0x7F) > MAX_PEER_CATE_MSG) {
			*MsgType = MT2_ACT_INVALID;
		} else {
			*MsgType = (pFrame->Octet[0] & 0x7F);
		}
		break;
	default:
		return FALSE;
		break;
	}

	return TRUE;
}

/* =========================================================================================== */
/* state_machine.c */
/* =========================================================================================== */

/*! \brief Initialize the state machine.
 *	\param *S			pointer to the state machine
 *	\param	Trans		State machine transition function
 *	\param	StNr		number of states
 *	\param	MsgNr		number of messages
 *	\param	DefFunc		default function, when there is invalid state/message combination
 *	\param	InitState	initial state of the state machine
 *	\param	Base		StateMachine base, internal use only
 *	\pre p_sm should be a legal pointer
 *	\post

 IRQL = PASSIVE_LEVEL

 */
void StateMachineInit(struct rt_state_machine *S,
		      IN STATE_MACHINE_FUNC Trans[],
		      unsigned long StNr,
		      unsigned long MsgNr,
		      IN STATE_MACHINE_FUNC DefFunc,
		      unsigned long InitState, unsigned long Base)
{
	unsigned long i, j;

	/* set number of states and messages */
	S->NrState = StNr;
	S->NrMsg = MsgNr;
	S->Base = Base;

	S->TransFunc = Trans;

	/* init all state transition to default function */
	for (i = 0; i < StNr; i++) {
		for (j = 0; j < MsgNr; j++) {
			S->TransFunc[i * MsgNr + j] = DefFunc;
		}
	}

	/* set the starting state */
	S->CurrState = InitState;
}

/*! \brief This function fills in the function pointer into the cell in the state machine
 *	\param *S	pointer to the state machine
 *	\param St	state
 *	\param Msg	incoming message
 *	\param f	the function to be executed when (state, message) combination occurs at the state machine
 *	\pre *S should be a legal pointer to the state machine, st, msg, should be all within the range, Base should be set in the initial state
 *	\post

 IRQL = PASSIVE_LEVEL

 */
void StateMachineSetAction(struct rt_state_machine *S,
			   unsigned long St,
			   unsigned long Msg, IN STATE_MACHINE_FUNC Func)
{
	unsigned long MsgIdx;

	MsgIdx = Msg - S->Base;

	if (St < S->NrState && MsgIdx < S->NrMsg) {
		/* boundary checking before setting the action */
		S->TransFunc[St * S->NrMsg + MsgIdx] = Func;
	}
}

/*! \brief	 This function does the state transition
 *	\param	 *Adapter the NIC adapter pointer
 *	\param	 *S	  the state machine
 *	\param	 *Elem	  the message to be executed
 *	\return   None

 IRQL = DISPATCH_LEVEL

 */
void StateMachinePerformAction(struct rt_rtmp_adapter *pAd,
			       struct rt_state_machine *S, struct rt_mlme_queue_elem *Elem)
{
	(*(S->TransFunc[S->CurrState * S->NrMsg + Elem->MsgType - S->Base]))
	    (pAd, Elem);
}

/*
	==========================================================================
	Description:
		The drop function, when machine executes this, the message is simply
		ignored. This function does nothing, the message is freed in
		StateMachinePerformAction()
	==========================================================================
 */
void Drop(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
}

/* =========================================================================================== */
/* lfsr.c */
/* =========================================================================================== */

/*
	==========================================================================
	Description:

	IRQL = PASSIVE_LEVEL

	==========================================================================
 */
void LfsrInit(struct rt_rtmp_adapter *pAd, unsigned long Seed)
{
	if (Seed == 0)
		pAd->Mlme.ShiftReg = 1;
	else
		pAd->Mlme.ShiftReg = Seed;
}

/*
	==========================================================================
	Description:
	==========================================================================
 */
u8 RandomByte(struct rt_rtmp_adapter *pAd)
{
	unsigned long i;
	u8 R, Result;

	R = 0;

	if (pAd->Mlme.ShiftReg == 0)
		NdisGetSystemUpTime((unsigned long *) & pAd->Mlme.ShiftReg);

	for (i = 0; i < 8; i++) {
		if (pAd->Mlme.ShiftReg & 0x00000001) {
			pAd->Mlme.ShiftReg =
			    ((pAd->Mlme.
			      ShiftReg ^ LFSR_MASK) >> 1) | 0x80000000;
			Result = 1;
		} else {
			pAd->Mlme.ShiftReg = pAd->Mlme.ShiftReg >> 1;
			Result = 0;
		}
		R = (R << 1) | Result;
	}

	return R;
}

/*
	========================================================================

	Routine Description:
		Verify the support rate for different PHY type

	Arguments:
		pAd 				Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
void RTMPCheckRates(struct rt_rtmp_adapter *pAd,
		    IN u8 SupRate[], IN u8 * SupRateLen)
{
	u8 RateIdx, i, j;
	u8 NewRate[12], NewRateLen;

	NewRateLen = 0;

	if (pAd->CommonCfg.PhyMode == PHY_11B)
		RateIdx = 4;
	else
		RateIdx = 12;

	/* Check for support rates exclude basic rate bit */
	for (i = 0; i < *SupRateLen; i++)
		for (j = 0; j < RateIdx; j++)
			if ((SupRate[i] & 0x7f) == RateIdTo500Kbps[j])
				NewRate[NewRateLen++] = SupRate[i];

	*SupRateLen = NewRateLen;
	NdisMoveMemory(SupRate, NewRate, NewRateLen);
}

BOOLEAN RTMPCheckChannel(struct rt_rtmp_adapter *pAd,
			 u8 CentralChannel, u8 Channel)
{
	u8 k;
	u8 UpperChannel = 0, LowerChannel = 0;
	u8 NoEffectChannelinList = 0;

	/* Find upper and lower channel according to 40MHz current operation. */
	if (CentralChannel < Channel) {
		UpperChannel = Channel;
		if (CentralChannel > 2)
			LowerChannel = CentralChannel - 2;
		else
			return FALSE;
	} else if (CentralChannel > Channel) {
		UpperChannel = CentralChannel + 2;
		LowerChannel = Channel;
	}

	for (k = 0; k < pAd->ChannelListNum; k++) {
		if (pAd->ChannelList[k].Channel == UpperChannel) {
			NoEffectChannelinList++;
		}
		if (pAd->ChannelList[k].Channel == LowerChannel) {
			NoEffectChannelinList++;
		}
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 ("Total Channel in Channel List = [%d]\n",
		  NoEffectChannelinList));
	if (NoEffectChannelinList == 2)
		return TRUE;
	else
		return FALSE;
}

/*
	========================================================================

	Routine Description:
		Verify the support rate for HT phy type

	Arguments:
		pAd 				Pointer to our adapter

	Return Value:
		FALSE if pAd->CommonCfg.SupportedHtPhy doesn't accept the pHtCapability.  (AP Mode)

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
BOOLEAN RTMPCheckHt(struct rt_rtmp_adapter *pAd,
		    u8 Wcid,
		    struct rt_ht_capability_ie * pHtCapability,
		    struct rt_add_ht_info_ie * pAddHtInfo)
{
	if (Wcid >= MAX_LEN_OF_MAC_TABLE)
		return FALSE;

	/* If use AMSDU, set flag. */
	if (pAd->CommonCfg.DesiredHtPhy.AmsduEnable)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid],
				       fCLIENT_STATUS_AMSDU_INUSED);
	/* Save Peer Capability */
	if (pHtCapability->HtCapInfo.ShortGIfor20)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid],
				       fCLIENT_STATUS_SGI20_CAPABLE);
	if (pHtCapability->HtCapInfo.ShortGIfor40)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid],
				       fCLIENT_STATUS_SGI40_CAPABLE);
	if (pHtCapability->HtCapInfo.TxSTBC)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid],
				       fCLIENT_STATUS_TxSTBC_CAPABLE);
	if (pHtCapability->HtCapInfo.RxSTBC)
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid],
				       fCLIENT_STATUS_RxSTBC_CAPABLE);
	if (pAd->CommonCfg.bRdg && pHtCapability->ExtHtCapInfo.RDGSupport) {
		CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[Wcid],
				       fCLIENT_STATUS_RDG_CAPABLE);
	}

	if (Wcid < MAX_LEN_OF_MAC_TABLE) {
		pAd->MacTab.Content[Wcid].MpduDensity =
		    pHtCapability->HtCapParm.MpduDensity;
	}
	/* Will check ChannelWidth for MCSSet[4] below */
	pAd->MlmeAux.HtCapability.MCSSet[4] = 0x1;
	switch (pAd->CommonCfg.RxStream) {
	case 1:
		pAd->MlmeAux.HtCapability.MCSSet[0] = 0xff;
		pAd->MlmeAux.HtCapability.MCSSet[1] = 0x00;
		pAd->MlmeAux.HtCapability.MCSSet[2] = 0x00;
		pAd->MlmeAux.HtCapability.MCSSet[3] = 0x00;
		break;
	case 2:
		pAd->MlmeAux.HtCapability.MCSSet[0] = 0xff;
		pAd->MlmeAux.HtCapability.MCSSet[1] = 0xff;
		pAd->MlmeAux.HtCapability.MCSSet[2] = 0x00;
		pAd->MlmeAux.HtCapability.MCSSet[3] = 0x00;
		break;
	case 3:
		pAd->MlmeAux.HtCapability.MCSSet[0] = 0xff;
		pAd->MlmeAux.HtCapability.MCSSet[1] = 0xff;
		pAd->MlmeAux.HtCapability.MCSSet[2] = 0xff;
		pAd->MlmeAux.HtCapability.MCSSet[3] = 0x00;
		break;
	}

	pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth =
	    pAddHtInfo->AddHtInfo.RecomWidth & pAd->CommonCfg.DesiredHtPhy.
	    ChannelWidth;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPCheckHt:: HtCapInfo.ChannelWidth=%d, RecomWidth=%d, DesiredHtPhy.ChannelWidth=%d, BW40MAvailForA/G=%d/%d, PhyMode=%d \n",
		  pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth,
		  pAddHtInfo->AddHtInfo.RecomWidth,
		  pAd->CommonCfg.DesiredHtPhy.ChannelWidth,
		  pAd->NicConfig2.field.BW40MAvailForA,
		  pAd->NicConfig2.field.BW40MAvailForG,
		  pAd->CommonCfg.PhyMode));

	pAd->MlmeAux.HtCapability.HtCapInfo.GF =
	    pHtCapability->HtCapInfo.GF & pAd->CommonCfg.DesiredHtPhy.GF;

	/* Send Assoc Req with my HT capability. */
	pAd->MlmeAux.HtCapability.HtCapInfo.AMsduSize =
	    pAd->CommonCfg.DesiredHtPhy.AmsduSize;
	pAd->MlmeAux.HtCapability.HtCapInfo.MimoPs =
	    pAd->CommonCfg.DesiredHtPhy.MimoPs;
	pAd->MlmeAux.HtCapability.HtCapInfo.ShortGIfor20 =
	    (pAd->CommonCfg.DesiredHtPhy.ShortGIfor20) & (pHtCapability->
							  HtCapInfo.
							  ShortGIfor20);
	pAd->MlmeAux.HtCapability.HtCapInfo.ShortGIfor40 =
	    (pAd->CommonCfg.DesiredHtPhy.ShortGIfor40) & (pHtCapability->
							  HtCapInfo.
							  ShortGIfor40);
	pAd->MlmeAux.HtCapability.HtCapInfo.TxSTBC =
	    (pAd->CommonCfg.DesiredHtPhy.TxSTBC) & (pHtCapability->HtCapInfo.
						    RxSTBC);
	pAd->MlmeAux.HtCapability.HtCapInfo.RxSTBC =
	    (pAd->CommonCfg.DesiredHtPhy.RxSTBC) & (pHtCapability->HtCapInfo.
						    TxSTBC);
	pAd->MlmeAux.HtCapability.HtCapParm.MaxRAmpduFactor =
	    pAd->CommonCfg.DesiredHtPhy.MaxRAmpduFactor;
	pAd->MlmeAux.HtCapability.HtCapParm.MpduDensity =
	    pAd->CommonCfg.HtCapability.HtCapParm.MpduDensity;
	pAd->MlmeAux.HtCapability.ExtHtCapInfo.PlusHTC =
	    pHtCapability->ExtHtCapInfo.PlusHTC;
	pAd->MacTab.Content[Wcid].HTCapability.ExtHtCapInfo.PlusHTC =
	    pHtCapability->ExtHtCapInfo.PlusHTC;
	if (pAd->CommonCfg.bRdg) {
		pAd->MlmeAux.HtCapability.ExtHtCapInfo.RDGSupport =
		    pHtCapability->ExtHtCapInfo.RDGSupport;
		pAd->MlmeAux.HtCapability.ExtHtCapInfo.PlusHTC = 1;
	}

	if (pAd->MlmeAux.HtCapability.HtCapInfo.ChannelWidth == BW_20)
		pAd->MlmeAux.HtCapability.MCSSet[4] = 0x0;	/* BW20 can't transmit MCS32 */

	COPY_AP_HTSETTINGS_FROM_BEACON(pAd, pHtCapability);
	return TRUE;
}

/*
	========================================================================

	Routine Description:
		Verify the support rate for different PHY type

	Arguments:
		pAd 				Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
void RTMPUpdateMlmeRate(struct rt_rtmp_adapter *pAd)
{
	u8 MinimumRate;
	u8 ProperMlmeRate;	/*= RATE_54; */
	u8 i, j, RateIdx = 12;	/*1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54 */
	BOOLEAN bMatch = FALSE;

	switch (pAd->CommonCfg.PhyMode) {
	case PHY_11B:
		ProperMlmeRate = RATE_11;
		MinimumRate = RATE_1;
		break;
	case PHY_11BG_MIXED:
	case PHY_11ABGN_MIXED:
	case PHY_11BGN_MIXED:
		if ((pAd->MlmeAux.SupRateLen == 4) &&
		    (pAd->MlmeAux.ExtRateLen == 0))
			/* B only AP */
			ProperMlmeRate = RATE_11;
		else
			ProperMlmeRate = RATE_24;

		if (pAd->MlmeAux.Channel <= 14)
			MinimumRate = RATE_1;
		else
			MinimumRate = RATE_6;
		break;
	case PHY_11A:
	case PHY_11N_2_4G:	/* rt2860 need to check mlmerate for 802.11n */
	case PHY_11GN_MIXED:
	case PHY_11AGN_MIXED:
	case PHY_11AN_MIXED:
	case PHY_11N_5G:
		ProperMlmeRate = RATE_24;
		MinimumRate = RATE_6;
		break;
	case PHY_11ABG_MIXED:
		ProperMlmeRate = RATE_24;
		if (pAd->MlmeAux.Channel <= 14)
			MinimumRate = RATE_1;
		else
			MinimumRate = RATE_6;
		break;
	default:		/* error */
		ProperMlmeRate = RATE_1;
		MinimumRate = RATE_1;
		break;
	}

	for (i = 0; i < pAd->MlmeAux.SupRateLen; i++) {
		for (j = 0; j < RateIdx; j++) {
			if ((pAd->MlmeAux.SupRate[i] & 0x7f) ==
			    RateIdTo500Kbps[j]) {
				if (j == ProperMlmeRate) {
					bMatch = TRUE;
					break;
				}
			}
		}

		if (bMatch)
			break;
	}

	if (bMatch == FALSE) {
		for (i = 0; i < pAd->MlmeAux.ExtRateLen; i++) {
			for (j = 0; j < RateIdx; j++) {
				if ((pAd->MlmeAux.ExtRate[i] & 0x7f) ==
				    RateIdTo500Kbps[j]) {
					if (j == ProperMlmeRate) {
						bMatch = TRUE;
						break;
					}
				}
			}

			if (bMatch)
				break;
		}
	}

	if (bMatch == FALSE) {
		ProperMlmeRate = MinimumRate;
	}

	pAd->CommonCfg.MlmeRate = MinimumRate;
	pAd->CommonCfg.RtsRate = ProperMlmeRate;
	if (pAd->CommonCfg.MlmeRate >= RATE_6) {
		pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_OFDM;
		pAd->CommonCfg.MlmeTransmit.field.MCS =
		    OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
		pAd->MacTab.Content[BSS0Mcast_WCID].HTPhyMode.field.MODE =
		    MODE_OFDM;
		pAd->MacTab.Content[BSS0Mcast_WCID].HTPhyMode.field.MCS =
		    OfdmRateToRxwiMCS[pAd->CommonCfg.MlmeRate];
	} else {
		pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_CCK;
		pAd->CommonCfg.MlmeTransmit.field.MCS = pAd->CommonCfg.MlmeRate;
		pAd->MacTab.Content[BSS0Mcast_WCID].HTPhyMode.field.MODE =
		    MODE_CCK;
		pAd->MacTab.Content[BSS0Mcast_WCID].HTPhyMode.field.MCS =
		    pAd->CommonCfg.MlmeRate;
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPUpdateMlmeRate ==>   MlmeTransmit = 0x%x  \n",
		  pAd->CommonCfg.MlmeTransmit.word));
}

char RTMPMaxRssi(struct rt_rtmp_adapter *pAd,
		 char Rssi0, char Rssi1, char Rssi2)
{
	char larger = -127;

	if ((pAd->Antenna.field.RxPath == 1) && (Rssi0 != 0)) {
		larger = Rssi0;
	}

	if ((pAd->Antenna.field.RxPath >= 2) && (Rssi1 != 0)) {
		larger = max(Rssi0, Rssi1);
	}

	if ((pAd->Antenna.field.RxPath == 3) && (Rssi2 != 0)) {
		larger = max(larger, Rssi2);
	}

	if (larger == -127)
		larger = 0;

	return larger;
}

/*
    ========================================================================
    Routine Description:
        Periodic evaluate antenna link status

    Arguments:
        pAd         - Adapter pointer

    Return Value:
        None

    ========================================================================
*/
void AsicEvaluateRxAnt(struct rt_rtmp_adapter *pAd)
{
	u8 BBPR3 = 0;

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS |
			   fRTMP_ADAPTER_HALT_IN_PROGRESS |
			   fRTMP_ADAPTER_RADIO_OFF |
			   fRTMP_ADAPTER_NIC_NOT_EXIST |
			   fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS) ||
	    OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE)
#ifdef RT30xx
	    || (pAd->EepromAccess)
#endif /* RT30xx // */
#ifdef RT3090
	    || (pAd->bPCIclkOff == TRUE)
#endif /* RT3090 // */
	    )
		return;

	{
		/*if (pAd->StaCfg.Psm == PWR_SAVE) */
		/*      return; */

		{

			if (pAd->StaCfg.Psm == PWR_SAVE)
				return;

			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BBPR3);
			BBPR3 &= (~0x18);
			if (pAd->Antenna.field.RxPath == 3) {
				BBPR3 |= (0x10);
			} else if (pAd->Antenna.field.RxPath == 2) {
				BBPR3 |= (0x8);
			} else if (pAd->Antenna.field.RxPath == 1) {
				BBPR3 |= (0x0);
			}
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BBPR3);
#ifdef RTMP_MAC_PCI
			pAd->StaCfg.BBPR3 = BBPR3;
#endif /* RTMP_MAC_PCI // */
			if (OPSTATUS_TEST_FLAG
			    (pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
			    ) {
				unsigned long TxTotalCnt =
				    pAd->RalinkCounters.OneSecTxNoRetryOkCount +
				    pAd->RalinkCounters.OneSecTxRetryOkCount +
				    pAd->RalinkCounters.OneSecTxFailCount;

				/* dynamic adjust antenna evaluation period according to the traffic */
				if (TxTotalCnt > 50) {
					RTMPSetTimer(&pAd->Mlme.RxAntEvalTimer,
						     20);
					pAd->Mlme.bLowThroughput = FALSE;
				} else {
					RTMPSetTimer(&pAd->Mlme.RxAntEvalTimer,
						     300);
					pAd->Mlme.bLowThroughput = TRUE;
				}
			}
		}

	}

}

/*
    ========================================================================
    Routine Description:
        After evaluation, check antenna link status

    Arguments:
        pAd         - Adapter pointer

    Return Value:
        None

    ========================================================================
*/
void AsicRxAntEvalTimeout(void *SystemSpecific1,
			  void *FunctionContext,
			  void *SystemSpecific2, void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;
	u8 BBPR3 = 0;
	char larger = -127, rssi0, rssi1, rssi2;

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS |
			   fRTMP_ADAPTER_HALT_IN_PROGRESS |
			   fRTMP_ADAPTER_RADIO_OFF |
			   fRTMP_ADAPTER_NIC_NOT_EXIST) ||
	    OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE)
#ifdef RT30xx
	    || (pAd->EepromAccess)
#endif /* RT30xx // */
#ifdef RT3090
	    || (pAd->bPCIclkOff == TRUE)
#endif /* RT3090 // */
	    )
		return;

	{
		/*if (pAd->StaCfg.Psm == PWR_SAVE) */
		/*      return; */
		{
			if (pAd->StaCfg.Psm == PWR_SAVE)
				return;

			/* if the traffic is low, use average rssi as the criteria */
			if (pAd->Mlme.bLowThroughput == TRUE) {
				rssi0 = pAd->StaCfg.RssiSample.LastRssi0;
				rssi1 = pAd->StaCfg.RssiSample.LastRssi1;
				rssi2 = pAd->StaCfg.RssiSample.LastRssi2;
			} else {
				rssi0 = pAd->StaCfg.RssiSample.AvgRssi0;
				rssi1 = pAd->StaCfg.RssiSample.AvgRssi1;
				rssi2 = pAd->StaCfg.RssiSample.AvgRssi2;
			}

			if (pAd->Antenna.field.RxPath == 3) {
				larger = max(rssi0, rssi1);

				if (larger > (rssi2 + 20))
					pAd->Mlme.RealRxPath = 2;
				else
					pAd->Mlme.RealRxPath = 3;
			} else if (pAd->Antenna.field.RxPath == 2) {
				if (rssi0 > (rssi1 + 20))
					pAd->Mlme.RealRxPath = 1;
				else
					pAd->Mlme.RealRxPath = 2;
			}

			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &BBPR3);
			BBPR3 &= (~0x18);
			if (pAd->Mlme.RealRxPath == 3) {
				BBPR3 |= (0x10);
			} else if (pAd->Mlme.RealRxPath == 2) {
				BBPR3 |= (0x8);
			} else if (pAd->Mlme.RealRxPath == 1) {
				BBPR3 |= (0x0);
			}
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, BBPR3);
#ifdef RTMP_MAC_PCI
			pAd->StaCfg.BBPR3 = BBPR3;
#endif /* RTMP_MAC_PCI // */
		}
	}

}

void APSDPeriodicExec(void *SystemSpecific1,
		      void *FunctionContext,
		      void *SystemSpecific2, void *SystemSpecific3)
{
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)FunctionContext;

	if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
		return;

	pAd->CommonCfg.TriggerTimerCount++;

/* Driver should not send trigger frame, it should be send by application layer */
/*
	if (pAd->CommonCfg.bAPSDCapable && pAd->CommonCfg.APEdcaParm.bAPSDCapable
		&& (pAd->CommonCfg.bNeedSendTriggerFrame ||
		(((pAd->CommonCfg.TriggerTimerCount%20) == 19) && (!pAd->CommonCfg.bAPSDAC_BE || !pAd->CommonCfg.bAPSDAC_BK || !pAd->CommonCfg.bAPSDAC_VI || !pAd->CommonCfg.bAPSDAC_VO))))
	{
		DBGPRINT(RT_DEBUG_TRACE,("Sending trigger frame and enter service period when support APSD\n"));
		RTMPSendNullFrame(pAd, pAd->CommonCfg.TxRate, TRUE);
		pAd->CommonCfg.bNeedSendTriggerFrame = FALSE;
		pAd->CommonCfg.TriggerTimerCount = 0;
		pAd->CommonCfg.bInServicePeriod = TRUE;
	}*/
}

/*
    ========================================================================
    Routine Description:
        Set/reset MAC registers according to bPiggyBack parameter

    Arguments:
        pAd         - Adapter pointer
        bPiggyBack  - Enable / Disable Piggy-Back

    Return Value:
        None

    ========================================================================
*/
void RTMPSetPiggyBack(struct rt_rtmp_adapter *pAd, IN BOOLEAN bPiggyBack)
{
	TX_LINK_CFG_STRUC TxLinkCfg;

	RTMP_IO_READ32(pAd, TX_LINK_CFG, &TxLinkCfg.word);

	TxLinkCfg.field.TxCFAckEn = bPiggyBack;
	RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);
}

/*
    ========================================================================
    Routine Description:
        check if this entry need to switch rate automatically

    Arguments:
        pAd
        pEntry

    Return Value:
        TURE
        FALSE

    ========================================================================
*/
BOOLEAN RTMPCheckEntryEnableAutoRateSwitch(struct rt_rtmp_adapter *pAd,
					   struct rt_mac_table_entry *pEntry)
{
	BOOLEAN result = TRUE;

	{
		/* only associated STA counts */
		if (pEntry && (pEntry->ValidAsCLI)
		    && (pEntry->Sst == SST_ASSOC)) {
			result = pAd->StaCfg.bAutoTxRateSwitch;
		} else
			result = FALSE;
	}

	return result;
}

BOOLEAN RTMPAutoRateSwitchCheck(struct rt_rtmp_adapter *pAd)
{
	{
		if (pAd->StaCfg.bAutoTxRateSwitch)
			return TRUE;
	}
	return FALSE;
}

/*
    ========================================================================
    Routine Description:
        check if this entry need to fix tx legacy rate

    Arguments:
        pAd
        pEntry

    Return Value:
        TURE
        FALSE

    ========================================================================
*/
u8 RTMPStaFixedTxMode(struct rt_rtmp_adapter *pAd, struct rt_mac_table_entry *pEntry)
{
	u8 tx_mode = FIXED_TXMODE_HT;

	{
		tx_mode =
		    (u8)pAd->StaCfg.DesiredTransmitSetting.field.
		    FixedTxMode;
	}

	return tx_mode;
}

/*
    ========================================================================
    Routine Description:
        Overwrite HT Tx Mode by Fixed Legency Tx Mode, if specified.

    Arguments:
        pAd
        pEntry

    Return Value:
        TURE
        FALSE

    ========================================================================
*/
void RTMPUpdateLegacyTxSetting(u8 fixed_tx_mode, struct rt_mac_table_entry *pEntry)
{
	HTTRANSMIT_SETTING TransmitSetting;

	if (fixed_tx_mode == FIXED_TXMODE_HT)
		return;

	TransmitSetting.word = 0;

	TransmitSetting.field.MODE = pEntry->HTPhyMode.field.MODE;
	TransmitSetting.field.MCS = pEntry->HTPhyMode.field.MCS;

	if (fixed_tx_mode == FIXED_TXMODE_CCK) {
		TransmitSetting.field.MODE = MODE_CCK;
		/* CCK mode allow MCS 0~3 */
		if (TransmitSetting.field.MCS > MCS_3)
			TransmitSetting.field.MCS = MCS_3;
	} else {
		TransmitSetting.field.MODE = MODE_OFDM;
		/* OFDM mode allow MCS 0~7 */
		if (TransmitSetting.field.MCS > MCS_7)
			TransmitSetting.field.MCS = MCS_7;
	}

	if (pEntry->HTPhyMode.field.MODE >= TransmitSetting.field.MODE) {
		pEntry->HTPhyMode.word = TransmitSetting.word;
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RTMPUpdateLegacyTxSetting : wcid-%d, MODE=%s, MCS=%d \n",
			  pEntry->Aid, GetPhyMode(pEntry->HTPhyMode.field.MODE),
			  pEntry->HTPhyMode.field.MCS));
	}
}

/*
	==========================================================================
	Description:
		dynamic tune BBP R66 to find a balance between sensibility and
		noise isolation

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
void AsicStaBbpTuning(struct rt_rtmp_adapter *pAd)
{
	u8 OrigR66Value = 0, R66;	/*, R66UpperBound = 0x30, R66LowerBound = 0x30; */
	char Rssi;

	/* 2860C did not support Fase CCA, therefore can't tune */
	if (pAd->MACVersion == 0x28600100)
		return;

	/* */
	/* work as a STA */
	/* */
	if (pAd->Mlme.CntlMachine.CurrState != CNTL_IDLE)	/* no R66 tuning when SCANNING */
		return;

	if ((pAd->OpMode == OPMODE_STA)
	    && (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
	    )
	    && !(OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
#ifdef RTMP_MAC_PCI
	    && (pAd->bPCIclkOff == FALSE)
#endif /* RTMP_MAC_PCI // */
	    ) {
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R66, &OrigR66Value);
		R66 = OrigR66Value;

		if (pAd->Antenna.field.RxPath > 1)
			Rssi =
			    (pAd->StaCfg.RssiSample.AvgRssi0 +
			     pAd->StaCfg.RssiSample.AvgRssi1) >> 1;
		else
			Rssi = pAd->StaCfg.RssiSample.AvgRssi0;

		if (pAd->LatchRfRegs.Channel <= 14) {	/*BG band */
#ifdef RT30xx
			/* RT3070 is a no LNA solution, it should have different control regarding to AGC gain control */
			/* Otherwise, it will have some throughput side effect when low RSSI */

			if (IS_RT3070(pAd) || IS_RT3090(pAd) || IS_RT3572(pAd)
			    || IS_RT3390(pAd)) {
				if (Rssi > RSSI_FOR_MID_LOW_SENSIBILITY) {
					R66 =
					    0x1C + 2 * GET_LNA_GAIN(pAd) + 0x20;
					if (OrigR66Value != R66) {
						RTMP_BBP_IO_WRITE8_BY_REG_ID
						    (pAd, BBP_R66, R66);
					}
				} else {
					R66 = 0x1C + 2 * GET_LNA_GAIN(pAd);
					if (OrigR66Value != R66) {
						RTMP_BBP_IO_WRITE8_BY_REG_ID
						    (pAd, BBP_R66, R66);
					}
				}
			} else
#endif /* RT30xx // */
			{
				if (Rssi > RSSI_FOR_MID_LOW_SENSIBILITY) {
					R66 = (0x2E + GET_LNA_GAIN(pAd)) + 0x10;
					if (OrigR66Value != R66) {
						RTMP_BBP_IO_WRITE8_BY_REG_ID
						    (pAd, BBP_R66, R66);
					}
				} else {
					R66 = 0x2E + GET_LNA_GAIN(pAd);
					if (OrigR66Value != R66) {
						RTMP_BBP_IO_WRITE8_BY_REG_ID
						    (pAd, BBP_R66, R66);
					}
				}
			}
		} else {	/*A band */
			if (pAd->CommonCfg.BBPCurrentBW == BW_20) {
				if (Rssi > RSSI_FOR_MID_LOW_SENSIBILITY) {
					R66 =
					    0x32 + (GET_LNA_GAIN(pAd) * 5) / 3 +
					    0x10;
					if (OrigR66Value != R66) {
						RTMP_BBP_IO_WRITE8_BY_REG_ID
						    (pAd, BBP_R66, R66);
					}
				} else {
					R66 =
					    0x32 + (GET_LNA_GAIN(pAd) * 5) / 3;
					if (OrigR66Value != R66) {
						RTMP_BBP_IO_WRITE8_BY_REG_ID
						    (pAd, BBP_R66, R66);
					}
				}
			} else {
				if (Rssi > RSSI_FOR_MID_LOW_SENSIBILITY) {
					R66 =
					    0x3A + (GET_LNA_GAIN(pAd) * 5) / 3 +
					    0x10;
					if (OrigR66Value != R66) {
						RTMP_BBP_IO_WRITE8_BY_REG_ID
						    (pAd, BBP_R66, R66);
					}
				} else {
					R66 =
					    0x3A + (GET_LNA_GAIN(pAd) * 5) / 3;
					if (OrigR66Value != R66) {
						RTMP_BBP_IO_WRITE8_BY_REG_ID
						    (pAd, BBP_R66, R66);
					}
				}
			}
		}

	}
}

void RTMPSetAGCInitValue(struct rt_rtmp_adapter *pAd, u8 BandWidth)
{
	u8 R66 = 0x30;

	if (pAd->LatchRfRegs.Channel <= 14) {	/* BG band */
#ifdef RT30xx
		/* Gary was verified Amazon AP and find that RT307x has BBP_R66 invalid default value */

		if (IS_RT3070(pAd) || IS_RT3090(pAd) || IS_RT3572(pAd)
		    || IS_RT3390(pAd)) {
			R66 = 0x1C + 2 * GET_LNA_GAIN(pAd);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
		} else
#endif /* RT30xx // */
		{
			R66 = 0x2E + GET_LNA_GAIN(pAd);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
		}
	} else {		/*A band */
		{
			if (BandWidth == BW_20) {
				R66 =
				    (u8)(0x32 +
					     (GET_LNA_GAIN(pAd) * 5) / 3);
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
			} else {
				R66 =
				    (u8)(0x3A +
					     (GET_LNA_GAIN(pAd) * 5) / 3);
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
			}
		}
	}

}
