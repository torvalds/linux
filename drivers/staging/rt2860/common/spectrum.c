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
	action.c

    Abstract:
    Handle association related requests either from WSTA or from local MLME

    Revision History:
    Who          When          What
    ---------    ----------    ----------------------------------------------
	Fonchi Wu    2008	  	   created for 802.11h
 */

#include "../rt_config.h"
#include "action.h"

/* The regulatory information in the USA (US) */
struct rt_dot11_regulatory_information USARegulatoryInfo[] = {
/*  "regulatory class"  "number of channels"  "Max Tx Pwr"  "channel list" */
	{0, {0, 0, {0}
	     }
	 }
	,			/* Invlid entry */
	{1, {4, 16, {36, 40, 44, 48}
	     }
	 }
	,
	{2, {4, 23, {52, 56, 60, 64}
	     }
	 }
	,
	{3, {4, 29, {149, 153, 157, 161}
	     }
	 }
	,
	{4, {11, 23, {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}
	     }
	 }
	,
	{5, {5, 30, {149, 153, 157, 161, 165}
	     }
	 }
	,
	{6, {10, 14, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
	     }
	 }
	,
	{7, {10, 27, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
	     }
	 }
	,
	{8, {5, 17, {11, 13, 15, 17, 19}
	     }
	 }
	,
	{9, {5, 30, {11, 13, 15, 17, 19}
	     }
	 }
	,
	{10, {2, 20, {21, 25}
	      }
	 }
	,
	{11, {2, 33, {21, 25}
	      }
	 }
	,
	{12, {11, 30, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}
	      }
	 }
};

#define USA_REGULATORY_INFO_SIZE (sizeof(USARegulatoryInfo) / sizeof(struct rt_dot11_regulatory_information))

/* The regulatory information in Europe */
struct rt_dot11_regulatory_information EuropeRegulatoryInfo[] = {
/*  "regulatory class"  "number of channels"  "Max Tx Pwr"  "channel list" */
	{0, {0, 0, {0}
	     }
	 }
	,			/* Invalid entry */
	{1, {4, 20, {36, 40, 44, 48}
	     }
	 }
	,
	{2, {4, 20, {52, 56, 60, 64}
	     }
	 }
	,
	{3, {11, 30, {100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}
	     }
	 }
	,
	{4, {13, 20, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}
	     }
	 }
};

#define EU_REGULATORY_INFO_SIZE (sizeof(EuropeRegulatoryInfo) / sizeof(struct rt_dot11_regulatory_information))

/* The regulatory information in Japan */
struct rt_dot11_regulatory_information JapanRegulatoryInfo[] = {
/*  "regulatory class"  "number of channels"  "Max Tx Pwr"  "channel list" */
	{0, {0, 0, {0}
	     }
	 }
	,			/* Invalid entry */
	{1, {4, 22, {34, 38, 42, 46}
	     }
	 }
	,
	{2, {3, 24, {8, 12, 16}
	     }
	 }
	,
	{3, {3, 24, {8, 12, 16}
	     }
	 }
	,
	{4, {3, 24, {8, 12, 16}
	     }
	 }
	,
	{5, {3, 24, {8, 12, 16}
	     }
	 }
	,
	{6, {3, 22, {8, 12, 16}
	     }
	 }
	,
	{7, {4, 24, {184, 188, 192, 196}
	     }
	 }
	,
	{8, {4, 24, {184, 188, 192, 196}
	     }
	 }
	,
	{9, {4, 24, {184, 188, 192, 196}
	     }
	 }
	,
	{10, {4, 24, {184, 188, 192, 196}
	      }
	 }
	,
	{11, {4, 22, {184, 188, 192, 196}
	      }
	 }
	,
	{12, {4, 24, {7, 8, 9, 11}
	      }
	 }
	,
	{13, {4, 24, {7, 8, 9, 11}
	      }
	 }
	,
	{14, {4, 24, {7, 8, 9, 11}
	      }
	 }
	,
	{15, {4, 24, {7, 8, 9, 11}
	      }
	 }
	,
	{16, {6, 24, {183, 184, 185, 187, 188, 189}
	      }
	 }
	,
	{17, {6, 24, {183, 184, 185, 187, 188, 189}
	      }
	 }
	,
	{18, {6, 24, {183, 184, 185, 187, 188, 189}
	      }
	 }
	,
	{19, {6, 24, {183, 184, 185, 187, 188, 189}
	      }
	 }
	,
	{20, {6, 17, {183, 184, 185, 187, 188, 189}
	      }
	 }
	,
	{21, {6, 24, {6, 7, 8, 9, 10, 11}
	      }
	 }
	,
	{22, {6, 24, {6, 7, 8, 9, 10, 11}
	      }
	 }
	,
	{23, {6, 24, {6, 7, 8, 9, 10, 11}
	      }
	 }
	,
	{24, {6, 24, {6, 7, 8, 9, 10, 11}
	      }
	 }
	,
	{25, {8, 24, {182, 183, 184, 185, 186, 187, 188, 189}
	      }
	 }
	,
	{26, {8, 24, {182, 183, 184, 185, 186, 187, 188, 189}
	      }
	 }
	,
	{27, {8, 24, {182, 183, 184, 185, 186, 187, 188, 189}
	      }
	 }
	,
	{28, {8, 24, {182, 183, 184, 185, 186, 187, 188, 189}
	      }
	 }
	,
	{29, {8, 17, {182, 183, 184, 185, 186, 187, 188, 189}
	      }
	 }
	,
	{30, {13, 23, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}
	      }
	 }
	,
	{31, {1, 23, {14}
	      }
	 }
	,
	{32, {4, 22, {52, 56, 60, 64}
	      }
	 }
};

#define JP_REGULATORY_INFO_SIZE (sizeof(JapanRegulatoryInfo) / sizeof(struct rt_dot11_regulatory_information))

char RTMP_GetTxPwr(struct rt_rtmp_adapter *pAd, IN HTTRANSMIT_SETTING HTTxMode)
{
	struct tx_pwr_cfg {
		u8 Mode;
		u8 MCS;
		u16 req;
		u8 shift;
		u32 BitMask;
	};

	u32 Value;
	int Idx;
	u8 PhyMode;
	char CurTxPwr;
	u8 TxPwrRef = 0;
	char DaltaPwr;
	unsigned long TxPwr[5];

	struct tx_pwr_cfg TxPwrCfg[] = {
		{MODE_CCK, 0, 0, 4, 0x000000f0},
		{MODE_CCK, 1, 0, 0, 0x0000000f},
		{MODE_CCK, 2, 0, 12, 0x0000f000},
		{MODE_CCK, 3, 0, 8, 0x00000f00},

		{MODE_OFDM, 0, 0, 20, 0x00f00000},
		{MODE_OFDM, 1, 0, 16, 0x000f0000},
		{MODE_OFDM, 2, 0, 28, 0xf0000000},
		{MODE_OFDM, 3, 0, 24, 0x0f000000},
		{MODE_OFDM, 4, 1, 4, 0x000000f0},
		{MODE_OFDM, 5, 1, 0, 0x0000000f},
		{MODE_OFDM, 6, 1, 12, 0x0000f000},
		{MODE_OFDM, 7, 1, 8, 0x00000f00}
		, {MODE_HTMIX, 0, 1, 20, 0x00f00000},
		{MODE_HTMIX, 1, 1, 16, 0x000f0000},
		{MODE_HTMIX, 2, 1, 28, 0xf0000000},
		{MODE_HTMIX, 3, 1, 24, 0x0f000000},
		{MODE_HTMIX, 4, 2, 4, 0x000000f0},
		{MODE_HTMIX, 5, 2, 0, 0x0000000f},
		{MODE_HTMIX, 6, 2, 12, 0x0000f000},
		{MODE_HTMIX, 7, 2, 8, 0x00000f00},
		{MODE_HTMIX, 8, 2, 20, 0x00f00000},
		{MODE_HTMIX, 9, 2, 16, 0x000f0000},
		{MODE_HTMIX, 10, 2, 28, 0xf0000000},
		{MODE_HTMIX, 11, 2, 24, 0x0f000000},
		{MODE_HTMIX, 12, 3, 4, 0x000000f0},
		{MODE_HTMIX, 13, 3, 0, 0x0000000f},
		{MODE_HTMIX, 14, 3, 12, 0x0000f000},
		{MODE_HTMIX, 15, 3, 8, 0x00000f00}
	};
#define MAX_TXPWR_TAB_SIZE (sizeof(TxPwrCfg) / sizeof(struct tx_pwr_cfg))

	CurTxPwr = 19;

	/* check Tx Power setting from UI. */
	if (pAd->CommonCfg.TxPowerPercentage > 90) ;
	else if (pAd->CommonCfg.TxPowerPercentage > 60)	/* reduce Pwr for 1 dB. */
		CurTxPwr -= 1;
	else if (pAd->CommonCfg.TxPowerPercentage > 30)	/* reduce Pwr for 3 dB. */
		CurTxPwr -= 3;
	else if (pAd->CommonCfg.TxPowerPercentage > 15)	/* reduce Pwr for 6 dB. */
		CurTxPwr -= 6;
	else if (pAd->CommonCfg.TxPowerPercentage > 9)	/* reduce Pwr for 9 dB. */
		CurTxPwr -= 9;
	else			/* reduce Pwr for 12 dB. */
		CurTxPwr -= 12;

	if (pAd->CommonCfg.BBPCurrentBW == BW_40) {
		if (pAd->CommonCfg.CentralChannel > 14) {
			TxPwr[0] = pAd->Tx40MPwrCfgABand[0];
			TxPwr[1] = pAd->Tx40MPwrCfgABand[1];
			TxPwr[2] = pAd->Tx40MPwrCfgABand[2];
			TxPwr[3] = pAd->Tx40MPwrCfgABand[3];
			TxPwr[4] = pAd->Tx40MPwrCfgABand[4];
		} else {
			TxPwr[0] = pAd->Tx40MPwrCfgGBand[0];
			TxPwr[1] = pAd->Tx40MPwrCfgGBand[1];
			TxPwr[2] = pAd->Tx40MPwrCfgGBand[2];
			TxPwr[3] = pAd->Tx40MPwrCfgGBand[3];
			TxPwr[4] = pAd->Tx40MPwrCfgGBand[4];
		}
	} else {
		if (pAd->CommonCfg.Channel > 14) {
			TxPwr[0] = pAd->Tx20MPwrCfgABand[0];
			TxPwr[1] = pAd->Tx20MPwrCfgABand[1];
			TxPwr[2] = pAd->Tx20MPwrCfgABand[2];
			TxPwr[3] = pAd->Tx20MPwrCfgABand[3];
			TxPwr[4] = pAd->Tx20MPwrCfgABand[4];
		} else {
			TxPwr[0] = pAd->Tx20MPwrCfgGBand[0];
			TxPwr[1] = pAd->Tx20MPwrCfgGBand[1];
			TxPwr[2] = pAd->Tx20MPwrCfgGBand[2];
			TxPwr[3] = pAd->Tx20MPwrCfgGBand[3];
			TxPwr[4] = pAd->Tx20MPwrCfgGBand[4];
		}
	}

	switch (HTTxMode.field.MODE) {
	case MODE_CCK:
	case MODE_OFDM:
		Value = TxPwr[1];
		TxPwrRef = (Value & 0x00000f00) >> 8;

		break;

	case MODE_HTMIX:
	case MODE_HTGREENFIELD:
		if (pAd->CommonCfg.TxStream == 1) {
			Value = TxPwr[2];
			TxPwrRef = (Value & 0x00000f00) >> 8;
		} else if (pAd->CommonCfg.TxStream == 2) {
			Value = TxPwr[3];
			TxPwrRef = (Value & 0x00000f00) >> 8;
		}
		break;
	}

	PhyMode = (HTTxMode.field.MODE == MODE_HTGREENFIELD)
	    ? MODE_HTMIX : HTTxMode.field.MODE;

	for (Idx = 0; Idx < MAX_TXPWR_TAB_SIZE; Idx++) {
		if ((TxPwrCfg[Idx].Mode == PhyMode)
		    && (TxPwrCfg[Idx].MCS == HTTxMode.field.MCS)) {
			Value = TxPwr[TxPwrCfg[Idx].req];
			DaltaPwr =
			    TxPwrRef - (char)((Value & TxPwrCfg[Idx].BitMask)
					       >> TxPwrCfg[Idx].shift);
			CurTxPwr -= DaltaPwr;
			break;
		}
	}

	return CurTxPwr;
}

void MeasureReqTabInit(struct rt_rtmp_adapter *pAd)
{
	NdisAllocateSpinLock(&pAd->CommonCfg.MeasureReqTabLock);

	pAd->CommonCfg.pMeasureReqTab =
	    kmalloc(sizeof(struct rt_measure_req_tab), GFP_ATOMIC);
	if (pAd->CommonCfg.pMeasureReqTab)
		NdisZeroMemory(pAd->CommonCfg.pMeasureReqTab,
			       sizeof(struct rt_measure_req_tab));
	else
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s Fail to alloc memory for pAd->CommonCfg.pMeasureReqTab.\n",
			  __func__));

	return;
}

void MeasureReqTabExit(struct rt_rtmp_adapter *pAd)
{
	NdisFreeSpinLock(&pAd->CommonCfg.MeasureReqTabLock);

	kfree(pAd->CommonCfg.pMeasureReqTab);
	pAd->CommonCfg.pMeasureReqTab = NULL;

	return;
}

struct rt_measure_req_entry *MeasureReqLookUp(struct rt_rtmp_adapter *pAd, u8 DialogToken)
{
	u32 HashIdx;
	struct rt_measure_req_tab *pTab = pAd->CommonCfg.pMeasureReqTab;
	struct rt_measure_req_entry *pEntry = NULL;
	struct rt_measure_req_entry *pPrevEntry = NULL;

	if (pTab == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: pMeasureReqTab doesn't exist.\n", __func__));
		return NULL;
	}

	RTMP_SEM_LOCK(&pAd->CommonCfg.MeasureReqTabLock);

	HashIdx = MQ_DIALOGTOKEN_HASH_INDEX(DialogToken);
	pEntry = pTab->Hash[HashIdx];

	while (pEntry) {
		if (pEntry->DialogToken == DialogToken)
			break;
		else {
			pPrevEntry = pEntry;
			pEntry = pEntry->pNext;
		}
	}

	RTMP_SEM_UNLOCK(&pAd->CommonCfg.MeasureReqTabLock);

	return pEntry;
}

struct rt_measure_req_entry *MeasureReqInsert(struct rt_rtmp_adapter *pAd, u8 DialogToken)
{
	int i;
	unsigned long HashIdx;
	struct rt_measure_req_tab *pTab = pAd->CommonCfg.pMeasureReqTab;
	struct rt_measure_req_entry *pEntry = NULL, *pCurrEntry;
	unsigned long Now;

	if (pTab == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: pMeasureReqTab doesn't exist.\n", __func__));
		return NULL;
	}

	pEntry = MeasureReqLookUp(pAd, DialogToken);
	if (pEntry == NULL) {
		RTMP_SEM_LOCK(&pAd->CommonCfg.MeasureReqTabLock);
		for (i = 0; i < MAX_MEASURE_REQ_TAB_SIZE; i++) {
			NdisGetSystemUpTime(&Now);
			pEntry = &pTab->Content[i];

			if ((pEntry->Valid == TRUE)
			    && RTMP_TIME_AFTER((unsigned long)Now,
					       (unsigned long)(pEntry->
							       lastTime +
							       MQ_REQ_AGE_OUT)))
			{
				struct rt_measure_req_entry *pPrevEntry = NULL;
				unsigned long HashIdx =
				    MQ_DIALOGTOKEN_HASH_INDEX(pEntry->
							      DialogToken);
				struct rt_measure_req_entry *pProbeEntry =
				    pTab->Hash[HashIdx];

				/* update Hash list */
				do {
					if (pProbeEntry == pEntry) {
						if (pPrevEntry == NULL) {
							pTab->Hash[HashIdx] =
							    pEntry->pNext;
						} else {
							pPrevEntry->pNext =
							    pEntry->pNext;
						}
						break;
					}

					pPrevEntry = pProbeEntry;
					pProbeEntry = pProbeEntry->pNext;
				} while (pProbeEntry);

				NdisZeroMemory(pEntry,
					       sizeof(struct rt_measure_req_entry));
				pTab->Size--;

				break;
			}

			if (pEntry->Valid == FALSE)
				break;
		}

		if (i < MAX_MEASURE_REQ_TAB_SIZE) {
			NdisGetSystemUpTime(&Now);
			pEntry->lastTime = Now;
			pEntry->Valid = TRUE;
			pEntry->DialogToken = DialogToken;
			pTab->Size++;
		} else {
			pEntry = NULL;
			DBGPRINT(RT_DEBUG_ERROR,
				 ("%s: pMeasureReqTab tab full.\n", __func__));
		}

		/* add this Neighbor entry into HASH table */
		if (pEntry) {
			HashIdx = MQ_DIALOGTOKEN_HASH_INDEX(DialogToken);
			if (pTab->Hash[HashIdx] == NULL) {
				pTab->Hash[HashIdx] = pEntry;
			} else {
				pCurrEntry = pTab->Hash[HashIdx];
				while (pCurrEntry->pNext != NULL)
					pCurrEntry = pCurrEntry->pNext;
				pCurrEntry->pNext = pEntry;
			}
		}

		RTMP_SEM_UNLOCK(&pAd->CommonCfg.MeasureReqTabLock);
	}

	return pEntry;
}

void MeasureReqDelete(struct rt_rtmp_adapter *pAd, u8 DialogToken)
{
	struct rt_measure_req_tab *pTab = pAd->CommonCfg.pMeasureReqTab;
	struct rt_measure_req_entry *pEntry = NULL;

	if (pTab == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: pMeasureReqTab doesn't exist.\n", __func__));
		return;
	}
	/* if empty, return */
	if (pTab->Size == 0) {
		DBGPRINT(RT_DEBUG_ERROR, ("pMeasureReqTab empty.\n"));
		return;
	}

	pEntry = MeasureReqLookUp(pAd, DialogToken);
	if (pEntry != NULL) {
		struct rt_measure_req_entry *pPrevEntry = NULL;
		unsigned long HashIdx = MQ_DIALOGTOKEN_HASH_INDEX(pEntry->DialogToken);
		struct rt_measure_req_entry *pProbeEntry = pTab->Hash[HashIdx];

		RTMP_SEM_LOCK(&pAd->CommonCfg.MeasureReqTabLock);
		/* update Hash list */
		do {
			if (pProbeEntry == pEntry) {
				if (pPrevEntry == NULL) {
					pTab->Hash[HashIdx] = pEntry->pNext;
				} else {
					pPrevEntry->pNext = pEntry->pNext;
				}
				break;
			}

			pPrevEntry = pProbeEntry;
			pProbeEntry = pProbeEntry->pNext;
		} while (pProbeEntry);

		NdisZeroMemory(pEntry, sizeof(struct rt_measure_req_entry));
		pTab->Size--;

		RTMP_SEM_UNLOCK(&pAd->CommonCfg.MeasureReqTabLock);
	}

	return;
}

void TpcReqTabInit(struct rt_rtmp_adapter *pAd)
{
	NdisAllocateSpinLock(&pAd->CommonCfg.TpcReqTabLock);

	pAd->CommonCfg.pTpcReqTab = kmalloc(sizeof(struct rt_tpc_req_tab), GFP_ATOMIC);
	if (pAd->CommonCfg.pTpcReqTab)
		NdisZeroMemory(pAd->CommonCfg.pTpcReqTab, sizeof(struct rt_tpc_req_tab));
	else
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s Fail to alloc memory for pAd->CommonCfg.pTpcReqTab.\n",
			  __func__));

	return;
}

void TpcReqTabExit(struct rt_rtmp_adapter *pAd)
{
	NdisFreeSpinLock(&pAd->CommonCfg.TpcReqTabLock);

	kfree(pAd->CommonCfg.pTpcReqTab);
	pAd->CommonCfg.pTpcReqTab = NULL;

	return;
}

static struct rt_tpc_req_entry *TpcReqLookUp(struct rt_rtmp_adapter *pAd, u8 DialogToken)
{
	u32 HashIdx;
	struct rt_tpc_req_tab *pTab = pAd->CommonCfg.pTpcReqTab;
	struct rt_tpc_req_entry *pEntry = NULL;
	struct rt_tpc_req_entry *pPrevEntry = NULL;

	if (pTab == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: pTpcReqTab doesn't exist.\n", __func__));
		return NULL;
	}

	RTMP_SEM_LOCK(&pAd->CommonCfg.TpcReqTabLock);

	HashIdx = TPC_DIALOGTOKEN_HASH_INDEX(DialogToken);
	pEntry = pTab->Hash[HashIdx];

	while (pEntry) {
		if (pEntry->DialogToken == DialogToken)
			break;
		else {
			pPrevEntry = pEntry;
			pEntry = pEntry->pNext;
		}
	}

	RTMP_SEM_UNLOCK(&pAd->CommonCfg.TpcReqTabLock);

	return pEntry;
}

static struct rt_tpc_req_entry *TpcReqInsert(struct rt_rtmp_adapter *pAd, u8 DialogToken)
{
	int i;
	unsigned long HashIdx;
	struct rt_tpc_req_tab *pTab = pAd->CommonCfg.pTpcReqTab;
	struct rt_tpc_req_entry *pEntry = NULL, *pCurrEntry;
	unsigned long Now;

	if (pTab == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: pTpcReqTab doesn't exist.\n", __func__));
		return NULL;
	}

	pEntry = TpcReqLookUp(pAd, DialogToken);
	if (pEntry == NULL) {
		RTMP_SEM_LOCK(&pAd->CommonCfg.TpcReqTabLock);
		for (i = 0; i < MAX_TPC_REQ_TAB_SIZE; i++) {
			NdisGetSystemUpTime(&Now);
			pEntry = &pTab->Content[i];

			if ((pEntry->Valid == TRUE)
			    && RTMP_TIME_AFTER((unsigned long)Now,
					       (unsigned long)(pEntry->
							       lastTime +
							       TPC_REQ_AGE_OUT)))
			{
				struct rt_tpc_req_entry *pPrevEntry = NULL;
				unsigned long HashIdx =
				    TPC_DIALOGTOKEN_HASH_INDEX(pEntry->
							       DialogToken);
				struct rt_tpc_req_entry *pProbeEntry =
				    pTab->Hash[HashIdx];

				/* update Hash list */
				do {
					if (pProbeEntry == pEntry) {
						if (pPrevEntry == NULL) {
							pTab->Hash[HashIdx] =
							    pEntry->pNext;
						} else {
							pPrevEntry->pNext =
							    pEntry->pNext;
						}
						break;
					}

					pPrevEntry = pProbeEntry;
					pProbeEntry = pProbeEntry->pNext;
				} while (pProbeEntry);

				NdisZeroMemory(pEntry, sizeof(struct rt_tpc_req_entry));
				pTab->Size--;

				break;
			}

			if (pEntry->Valid == FALSE)
				break;
		}

		if (i < MAX_TPC_REQ_TAB_SIZE) {
			NdisGetSystemUpTime(&Now);
			pEntry->lastTime = Now;
			pEntry->Valid = TRUE;
			pEntry->DialogToken = DialogToken;
			pTab->Size++;
		} else {
			pEntry = NULL;
			DBGPRINT(RT_DEBUG_ERROR,
				 ("%s: pTpcReqTab tab full.\n", __func__));
		}

		/* add this Neighbor entry into HASH table */
		if (pEntry) {
			HashIdx = TPC_DIALOGTOKEN_HASH_INDEX(DialogToken);
			if (pTab->Hash[HashIdx] == NULL) {
				pTab->Hash[HashIdx] = pEntry;
			} else {
				pCurrEntry = pTab->Hash[HashIdx];
				while (pCurrEntry->pNext != NULL)
					pCurrEntry = pCurrEntry->pNext;
				pCurrEntry->pNext = pEntry;
			}
		}

		RTMP_SEM_UNLOCK(&pAd->CommonCfg.TpcReqTabLock);
	}

	return pEntry;
}

static void TpcReqDelete(struct rt_rtmp_adapter *pAd, u8 DialogToken)
{
	struct rt_tpc_req_tab *pTab = pAd->CommonCfg.pTpcReqTab;
	struct rt_tpc_req_entry *pEntry = NULL;

	if (pTab == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: pTpcReqTab doesn't exist.\n", __func__));
		return;
	}
	/* if empty, return */
	if (pTab->Size == 0) {
		DBGPRINT(RT_DEBUG_ERROR, ("pTpcReqTab empty.\n"));
		return;
	}

	pEntry = TpcReqLookUp(pAd, DialogToken);
	if (pEntry != NULL) {
		struct rt_tpc_req_entry *pPrevEntry = NULL;
		unsigned long HashIdx = TPC_DIALOGTOKEN_HASH_INDEX(pEntry->DialogToken);
		struct rt_tpc_req_entry *pProbeEntry = pTab->Hash[HashIdx];

		RTMP_SEM_LOCK(&pAd->CommonCfg.TpcReqTabLock);
		/* update Hash list */
		do {
			if (pProbeEntry == pEntry) {
				if (pPrevEntry == NULL) {
					pTab->Hash[HashIdx] = pEntry->pNext;
				} else {
					pPrevEntry->pNext = pEntry->pNext;
				}
				break;
			}

			pPrevEntry = pProbeEntry;
			pProbeEntry = pProbeEntry->pNext;
		} while (pProbeEntry);

		NdisZeroMemory(pEntry, sizeof(struct rt_tpc_req_entry));
		pTab->Size--;

		RTMP_SEM_UNLOCK(&pAd->CommonCfg.TpcReqTabLock);
	}

	return;
}

/*
	==========================================================================
	Description:
		Get Current TimeS tamp.

	Parametrs:

	Return	: Current Time Stamp.
	==========================================================================
 */
static u64 GetCurrentTimeStamp(struct rt_rtmp_adapter *pAd)
{
	/* get current time stamp. */
	return 0;
}

/*
	==========================================================================
	Description:
		Get Current Transmit Power.

	Parametrs:

	Return	: Current Time Stamp.
	==========================================================================
 */
static u8 GetCurTxPwr(struct rt_rtmp_adapter *pAd, u8 Wcid)
{
	return 16;		/* 16 dBm */
}

/*
	==========================================================================
	Description:
		Get Current Transmit Power.

	Parametrs:

	Return	: Current Time Stamp.
	==========================================================================
 */
void InsertChannelRepIE(struct rt_rtmp_adapter *pAd,
			u8 *pFrameBuf,
			unsigned long *pFrameLen,
			char *pCountry, u8 RegulatoryClass)
{
	unsigned long TempLen;
	u8 Len;
	u8 IEId = IE_AP_CHANNEL_REPORT;
	u8 *pChListPtr = NULL;

	Len = 1;
	if (strncmp(pCountry, "US", 2) == 0) {
		if (RegulatoryClass >= USA_REGULATORY_INFO_SIZE) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("%s: USA Unknow Requlatory class (%d)\n",
				  __func__, RegulatoryClass));
			return;
		}

		Len +=
		    USARegulatoryInfo[RegulatoryClass].ChannelSet.
		    NumberOfChannels;
		pChListPtr =
		    USARegulatoryInfo[RegulatoryClass].ChannelSet.ChannelList;
	} else if (strncmp(pCountry, "JP", 2) == 0) {
		if (RegulatoryClass >= JP_REGULATORY_INFO_SIZE) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("%s: JP Unknow Requlatory class (%d)\n",
				  __func__, RegulatoryClass));
			return;
		}

		Len +=
		    JapanRegulatoryInfo[RegulatoryClass].ChannelSet.
		    NumberOfChannels;
		pChListPtr =
		    JapanRegulatoryInfo[RegulatoryClass].ChannelSet.ChannelList;
	} else {
		DBGPRINT(RT_DEBUG_ERROR, ("%s: Unknow Country (%s)\n",
					  __func__, pCountry));
		return;
	}

	MakeOutgoingFrame(pFrameBuf, &TempLen,
			  1, &IEId,
			  1, &Len,
			  1, &RegulatoryClass,
			  Len - 1, pChListPtr, END_OF_ARGS);

	*pFrameLen = *pFrameLen + TempLen;

	return;
}

/*
	==========================================================================
	Description:
		Insert Dialog Token into frame.

	Parametrs:
		1. frame buffer pointer.
		2. frame length.
		3. Dialog token.

	Return	: None.
	==========================================================================
 */
void InsertDialogToken(struct rt_rtmp_adapter *pAd,
		       u8 *pFrameBuf,
		       unsigned long *pFrameLen, u8 DialogToken)
{
	unsigned long TempLen;
	MakeOutgoingFrame(pFrameBuf, &TempLen, 1, &DialogToken, END_OF_ARGS);

	*pFrameLen = *pFrameLen + TempLen;

	return;
}

/*
	==========================================================================
	Description:
		Insert TPC Request IE into frame.

	Parametrs:
		1. frame buffer pointer.
		2. frame length.

	Return	: None.
	==========================================================================
 */
static void InsertTpcReqIE(struct rt_rtmp_adapter *pAd,
			   u8 *pFrameBuf, unsigned long *pFrameLen)
{
	unsigned long TempLen;
	unsigned long Len = 0;
	u8 ElementID = IE_TPC_REQUEST;

	MakeOutgoingFrame(pFrameBuf, &TempLen,
			  1, &ElementID, 1, &Len, END_OF_ARGS);

	*pFrameLen = *pFrameLen + TempLen;

	return;
}

/*
	==========================================================================
	Description:
		Insert TPC Report IE into frame.

	Parametrs:
		1. frame buffer pointer.
		2. frame length.
		3. Transmit Power.
		4. Link Margin.

	Return	: None.
	==========================================================================
 */
void InsertTpcReportIE(struct rt_rtmp_adapter *pAd,
		       u8 *pFrameBuf,
		       unsigned long *pFrameLen,
		       u8 TxPwr, u8 LinkMargin)
{
	unsigned long TempLen;
	unsigned long Len = sizeof(struct rt_tpc_report_info);
	u8 ElementID = IE_TPC_REPORT;
	struct rt_tpc_report_info TpcReportIE;

	TpcReportIE.TxPwr = TxPwr;
	TpcReportIE.LinkMargin = LinkMargin;

	MakeOutgoingFrame(pFrameBuf, &TempLen,
			  1, &ElementID,
			  1, &Len, Len, &TpcReportIE, END_OF_ARGS);

	*pFrameLen = *pFrameLen + TempLen;

	return;
}

/*
	==========================================================================
	Description:
		Insert Channel Switch Announcement IE into frame.

	Parametrs:
		1. frame buffer pointer.
		2. frame length.
		3. channel switch announcement mode.
		4. new selected channel.
		5. channel switch announcement count.

	Return	: None.
	==========================================================================
 */
static void InsertChSwAnnIE(struct rt_rtmp_adapter *pAd,
			    u8 *pFrameBuf,
			    unsigned long *pFrameLen,
			    u8 ChSwMode,
			    u8 NewChannel, u8 ChSwCnt)
{
	unsigned long TempLen;
	unsigned long Len = sizeof(struct rt_ch_sw_ann_info);
	u8 ElementID = IE_CHANNEL_SWITCH_ANNOUNCEMENT;
	struct rt_ch_sw_ann_info ChSwAnnIE;

	ChSwAnnIE.ChSwMode = ChSwMode;
	ChSwAnnIE.Channel = NewChannel;
	ChSwAnnIE.ChSwCnt = ChSwCnt;

	MakeOutgoingFrame(pFrameBuf, &TempLen,
			  1, &ElementID, 1, &Len, Len, &ChSwAnnIE, END_OF_ARGS);

	*pFrameLen = *pFrameLen + TempLen;

	return;
}

/*
	==========================================================================
	Description:
		Insert Measure Request IE into frame.

	Parametrs:
		1. frame buffer pointer.
		2. frame length.
		3. Measure Token.
		4. Measure Request Mode.
		5. Measure Request Type.
		6. Measure Channel.
		7. Measure Start time.
		8. Measure Duration.

	Return	: None.
	==========================================================================
 */
static void InsertMeasureReqIE(struct rt_rtmp_adapter *pAd,
			       u8 *pFrameBuf,
			       unsigned long *pFrameLen,
			       u8 Len, struct rt_measure_req_info * pMeasureReqIE)
{
	unsigned long TempLen;
	u8 ElementID = IE_MEASUREMENT_REQUEST;

	MakeOutgoingFrame(pFrameBuf, &TempLen,
			  1, &ElementID,
			  1, &Len,
			  sizeof(struct rt_measure_req_info), pMeasureReqIE, END_OF_ARGS);

	*pFrameLen = *pFrameLen + TempLen;

	return;
}

/*
	==========================================================================
	Description:
		Insert Measure Report IE into frame.

	Parametrs:
		1. frame buffer pointer.
		2. frame length.
		3. Measure Token.
		4. Measure Request Mode.
		5. Measure Request Type.
		6. Length of Report Information
		7. Pointer of Report Information Buffer.

	Return	: None.
	==========================================================================
 */
static void InsertMeasureReportIE(struct rt_rtmp_adapter *pAd,
				  u8 *pFrameBuf,
				  unsigned long *pFrameLen,
				  struct rt_measure_report_info * pMeasureReportIE,
				  u8 ReportLnfoLen, u8 *pReportInfo)
{
	unsigned long TempLen;
	unsigned long Len;
	u8 ElementID = IE_MEASUREMENT_REPORT;

	Len = sizeof(struct rt_measure_report_info) + ReportLnfoLen;

	MakeOutgoingFrame(pFrameBuf, &TempLen,
			  1, &ElementID,
			  1, &Len, Len, pMeasureReportIE, END_OF_ARGS);

	*pFrameLen = *pFrameLen + TempLen;

	if ((ReportLnfoLen > 0) && (pReportInfo != NULL)) {
		MakeOutgoingFrame(pFrameBuf + *pFrameLen, &TempLen,
				  ReportLnfoLen, pReportInfo, END_OF_ARGS);

		*pFrameLen = *pFrameLen + TempLen;
	}
	return;
}

/*
	==========================================================================
	Description:
		Prepare Measurement request action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.

	Return	: None.
	==========================================================================
 */
void MakeMeasurementReqFrame(struct rt_rtmp_adapter *pAd,
			     u8 *pOutBuffer,
			     unsigned long *pFrameLen,
			     u8 TotalLen,
			     u8 Category,
			     u8 Action,
			     u8 MeasureToken,
			     u8 MeasureReqMode,
			     u8 MeasureReqType, u8 NumOfRepetitions)
{
	unsigned long TempLen;
	struct rt_measure_req_info MeasureReqIE;

	InsertActField(pAd, (pOutBuffer + *pFrameLen), pFrameLen, Category,
		       Action);

	/* fill Dialog Token */
	InsertDialogToken(pAd, (pOutBuffer + *pFrameLen), pFrameLen,
			  MeasureToken);

	/* fill Number of repetitions. */
	if (Category == CATEGORY_RM) {
		MakeOutgoingFrame((pOutBuffer + *pFrameLen), &TempLen,
				  2, &NumOfRepetitions, END_OF_ARGS);

		*pFrameLen += TempLen;
	}
	/* prepare Measurement IE. */
	NdisZeroMemory(&MeasureReqIE, sizeof(struct rt_measure_req_info));
	MeasureReqIE.Token = MeasureToken;
	MeasureReqIE.ReqMode.word = MeasureReqMode;
	MeasureReqIE.ReqType = MeasureReqType;
	InsertMeasureReqIE(pAd, (pOutBuffer + *pFrameLen), pFrameLen,
			   TotalLen, &MeasureReqIE);

	return;
}

/*
	==========================================================================
	Description:
		Prepare Measurement report action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.

	Return	: None.
	==========================================================================
 */
void EnqueueMeasurementRep(struct rt_rtmp_adapter *pAd,
			   u8 *pDA,
			   u8 DialogToken,
			   u8 MeasureToken,
			   u8 MeasureReqMode,
			   u8 MeasureReqType,
			   u8 ReportInfoLen, u8 *pReportInfo)
{
	u8 *pOutBuffer = NULL;
	int NStatus;
	unsigned long FrameLen;
	struct rt_header_802_11 ActHdr;
	struct rt_measure_report_info MeasureRepIE;

	/* build action frame header. */
	MgtMacHeaderInit(pAd, &ActHdr, SUBTYPE_ACTION, 0, pDA,
			 pAd->CurrentAddress);

	NStatus = MlmeAllocateMemory(pAd, (void *)& pOutBuffer);	/*Get an unused nonpaged memory */
	if (NStatus != NDIS_STATUS_SUCCESS) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s() allocate memory failed \n", __func__));
		return;
	}
	NdisMoveMemory(pOutBuffer, (char *)& ActHdr, sizeof(struct rt_header_802_11));
	FrameLen = sizeof(struct rt_header_802_11);

	InsertActField(pAd, (pOutBuffer + FrameLen), &FrameLen,
		       CATEGORY_SPECTRUM, SPEC_MRP);

	/* fill Dialog Token */
	InsertDialogToken(pAd, (pOutBuffer + FrameLen), &FrameLen, DialogToken);

	/* prepare Measurement IE. */
	NdisZeroMemory(&MeasureRepIE, sizeof(struct rt_measure_report_info));
	MeasureRepIE.Token = MeasureToken;
	MeasureRepIE.ReportMode = MeasureReqMode;
	MeasureRepIE.ReportType = MeasureReqType;
	InsertMeasureReportIE(pAd, (pOutBuffer + FrameLen), &FrameLen,
			      &MeasureRepIE, ReportInfoLen, pReportInfo);

	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);

	return;
}

/*
	==========================================================================
	Description:
		Prepare TPC Request action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.

	Return	: None.
	==========================================================================
 */
void EnqueueTPCReq(struct rt_rtmp_adapter *pAd, u8 *pDA, u8 DialogToken)
{
	u8 *pOutBuffer = NULL;
	int NStatus;
	unsigned long FrameLen;

	struct rt_header_802_11 ActHdr;

	/* build action frame header. */
	MgtMacHeaderInit(pAd, &ActHdr, SUBTYPE_ACTION, 0, pDA,
			 pAd->CurrentAddress);

	NStatus = MlmeAllocateMemory(pAd, (void *)& pOutBuffer);	/*Get an unused nonpaged memory */
	if (NStatus != NDIS_STATUS_SUCCESS) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s() allocate memory failed \n", __func__));
		return;
	}
	NdisMoveMemory(pOutBuffer, (char *)& ActHdr, sizeof(struct rt_header_802_11));
	FrameLen = sizeof(struct rt_header_802_11);

	InsertActField(pAd, (pOutBuffer + FrameLen), &FrameLen,
		       CATEGORY_SPECTRUM, SPEC_TPCRQ);

	/* fill Dialog Token */
	InsertDialogToken(pAd, (pOutBuffer + FrameLen), &FrameLen, DialogToken);

	/* Insert TPC Request IE. */
	InsertTpcReqIE(pAd, (pOutBuffer + FrameLen), &FrameLen);

	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);

	return;
}

/*
	==========================================================================
	Description:
		Prepare TPC Report action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.

	Return	: None.
	==========================================================================
 */
void EnqueueTPCRep(struct rt_rtmp_adapter *pAd,
		   u8 *pDA,
		   u8 DialogToken, u8 TxPwr, u8 LinkMargin)
{
	u8 *pOutBuffer = NULL;
	int NStatus;
	unsigned long FrameLen;

	struct rt_header_802_11 ActHdr;

	/* build action frame header. */
	MgtMacHeaderInit(pAd, &ActHdr, SUBTYPE_ACTION, 0, pDA,
			 pAd->CurrentAddress);

	NStatus = MlmeAllocateMemory(pAd, (void *)& pOutBuffer);	/*Get an unused nonpaged memory */
	if (NStatus != NDIS_STATUS_SUCCESS) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s() allocate memory failed \n", __func__));
		return;
	}
	NdisMoveMemory(pOutBuffer, (char *)& ActHdr, sizeof(struct rt_header_802_11));
	FrameLen = sizeof(struct rt_header_802_11);

	InsertActField(pAd, (pOutBuffer + FrameLen), &FrameLen,
		       CATEGORY_SPECTRUM, SPEC_TPCRP);

	/* fill Dialog Token */
	InsertDialogToken(pAd, (pOutBuffer + FrameLen), &FrameLen, DialogToken);

	/* Insert TPC Request IE. */
	InsertTpcReportIE(pAd, (pOutBuffer + FrameLen), &FrameLen, TxPwr,
			  LinkMargin);

	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);

	return;
}

/*
	==========================================================================
	Description:
		Prepare Channel Switch Announcement action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.
		2. Channel switch announcement mode.
		2. a New selected channel.

	Return	: None.
	==========================================================================
 */
void EnqueueChSwAnn(struct rt_rtmp_adapter *pAd,
		    u8 *pDA, u8 ChSwMode, u8 NewCh)
{
	u8 *pOutBuffer = NULL;
	int NStatus;
	unsigned long FrameLen;

	struct rt_header_802_11 ActHdr;

	/* build action frame header. */
	MgtMacHeaderInit(pAd, &ActHdr, SUBTYPE_ACTION, 0, pDA,
			 pAd->CurrentAddress);

	NStatus = MlmeAllocateMemory(pAd, (void *)& pOutBuffer);	/*Get an unused nonpaged memory */
	if (NStatus != NDIS_STATUS_SUCCESS) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s() allocate memory failed \n", __func__));
		return;
	}
	NdisMoveMemory(pOutBuffer, (char *)& ActHdr, sizeof(struct rt_header_802_11));
	FrameLen = sizeof(struct rt_header_802_11);

	InsertActField(pAd, (pOutBuffer + FrameLen), &FrameLen,
		       CATEGORY_SPECTRUM, SPEC_CHANNEL_SWITCH);

	InsertChSwAnnIE(pAd, (pOutBuffer + FrameLen), &FrameLen, ChSwMode,
			NewCh, 0);

	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);

	return;
}

static BOOLEAN DfsRequirementCheck(struct rt_rtmp_adapter *pAd, u8 Channel)
{
	BOOLEAN Result = FALSE;
	int i;

	do {
		/* check DFS procedure is running. */
		/* make sure DFS procedure won't start twice. */
		if (pAd->CommonCfg.RadarDetect.RDMode != RD_NORMAL_MODE) {
			Result = FALSE;
			break;
		}
		/* check the new channel carried from Channel Switch Announcemnet is valid. */
		for (i = 0; i < pAd->ChannelListNum; i++) {
			if ((Channel == pAd->ChannelList[i].Channel)
			    && (pAd->ChannelList[i].RemainingTimeForUse == 0)) {
				/* found radar signal in the channel. the channel can't use at least for 30 minutes. */
				pAd->ChannelList[i].RemainingTimeForUse = 1800;	/*30 min = 1800 sec */
				Result = TRUE;
				break;
			}
		}
	} while (FALSE);

	return Result;
}

void NotifyChSwAnnToPeerAPs(struct rt_rtmp_adapter *pAd,
			    u8 *pRA,
			    u8 *pTA, u8 ChSwMode, u8 Channel)
{
}

static void StartDFSProcedure(struct rt_rtmp_adapter *pAd,
			      u8 Channel, u8 ChSwMode)
{
	/* start DFS procedure */
	pAd->CommonCfg.Channel = Channel;

	N_ChannelCheck(pAd);

	pAd->CommonCfg.RadarDetect.RDMode = RD_SWITCHING_MODE;
	pAd->CommonCfg.RadarDetect.CSCount = 0;
}

/*
	==========================================================================
	Description:
		Channel Switch Announcement action frame sanity check.

	Parametrs:
		1. MLME message containing the received frame
		2. message length.
		3. Channel switch announcement information buffer.

	Return	: None.
	==========================================================================
 */

/*
  Channel Switch Announcement IE.
  +----+-----+-----------+------------+-----------+
  | ID | Len |Ch Sw Mode | New Ch Num | Ch Sw Cnt |
  +----+-----+-----------+------------+-----------+
    1    1        1           1            1
*/
static BOOLEAN PeerChSwAnnSanity(struct rt_rtmp_adapter *pAd,
				 void * pMsg,
				 unsigned long MsgLen,
				 struct rt_ch_sw_ann_info * pChSwAnnInfo)
{
	struct rt_frame_802_11 * Fr = (struct rt_frame_802_11 *) pMsg;
	u8 *pFramePtr = Fr->Octet;
	BOOLEAN result = FALSE;
	struct rt_eid * eid_ptr;

	/* skip 802.11 header. */
	MsgLen -= sizeof(struct rt_header_802_11);

	/* skip category and action code. */
	pFramePtr += 2;
	MsgLen -= 2;

	if (pChSwAnnInfo == NULL)
		return result;

	eid_ptr = (struct rt_eid *) pFramePtr;
	while (((u8 *) eid_ptr + eid_ptr->Len + 1) <
	       ((u8 *)pFramePtr + MsgLen)) {
		switch (eid_ptr->Eid) {
		case IE_CHANNEL_SWITCH_ANNOUNCEMENT:
			NdisMoveMemory(&pChSwAnnInfo->ChSwMode, eid_ptr->Octet,
				       1);
			NdisMoveMemory(&pChSwAnnInfo->Channel,
				       eid_ptr->Octet + 1, 1);
			NdisMoveMemory(&pChSwAnnInfo->ChSwCnt,
				       eid_ptr->Octet + 2, 1);

			result = TRUE;
			break;

		default:
			break;
		}
		eid_ptr = (struct rt_eid *) ((u8 *) eid_ptr + 2 + eid_ptr->Len);
	}

	return result;
}

/*
	==========================================================================
	Description:
		Measurement request action frame sanity check.

	Parametrs:
		1. MLME message containing the received frame
		2. message length.
		3. Measurement request information buffer.

	Return	: None.
	==========================================================================
 */
static BOOLEAN PeerMeasureReqSanity(struct rt_rtmp_adapter *pAd,
				    void * pMsg,
				    unsigned long MsgLen,
				    u8 *pDialogToken,
				    struct rt_measure_req_info * pMeasureReqInfo,
				    struct rt_measure_req * pMeasureReq)
{
	struct rt_frame_802_11 * Fr = (struct rt_frame_802_11 *) pMsg;
	u8 *pFramePtr = Fr->Octet;
	BOOLEAN result = FALSE;
	struct rt_eid * eid_ptr;
	u8 *ptr;
	u64 MeasureStartTime;
	u16 MeasureDuration;

	/* skip 802.11 header. */
	MsgLen -= sizeof(struct rt_header_802_11);

	/* skip category and action code. */
	pFramePtr += 2;
	MsgLen -= 2;

	if (pMeasureReqInfo == NULL)
		return result;

	NdisMoveMemory(pDialogToken, pFramePtr, 1);
	pFramePtr += 1;
	MsgLen -= 1;

	eid_ptr = (struct rt_eid *) pFramePtr;
	while (((u8 *) eid_ptr + eid_ptr->Len + 1) <
	       ((u8 *)pFramePtr + MsgLen)) {
		switch (eid_ptr->Eid) {
		case IE_MEASUREMENT_REQUEST:
			NdisMoveMemory(&pMeasureReqInfo->Token, eid_ptr->Octet,
				       1);
			NdisMoveMemory(&pMeasureReqInfo->ReqMode.word,
				       eid_ptr->Octet + 1, 1);
			NdisMoveMemory(&pMeasureReqInfo->ReqType,
				       eid_ptr->Octet + 2, 1);
			ptr = (u8 *)(eid_ptr->Octet + 3);
			NdisMoveMemory(&pMeasureReq->ChNum, ptr, 1);
			NdisMoveMemory(&MeasureStartTime, ptr + 1, 8);
			pMeasureReq->MeasureStartTime =
			    SWAP64(MeasureStartTime);
			NdisMoveMemory(&MeasureDuration, ptr + 9, 2);
			pMeasureReq->MeasureDuration = SWAP16(MeasureDuration);

			result = TRUE;
			break;

		default:
			break;
		}
		eid_ptr = (struct rt_eid *) ((u8 *) eid_ptr + 2 + eid_ptr->Len);
	}

	return result;
}

/*
	==========================================================================
	Description:
		Measurement report action frame sanity check.

	Parametrs:
		1. MLME message containing the received frame
		2. message length.
		3. Measurement report information buffer.
		4. basic report information buffer.

	Return	: None.
	==========================================================================
 */

/*
  Measurement Report IE.
  +----+-----+-------+-------------+--------------+----------------+
  | ID | Len | Token | Report Mode | Measure Type | Measure Report |
  +----+-----+-------+-------------+--------------+----------------+
    1     1      1          1             1            variable

  Basic Report.
  +--------+------------+----------+-----+
  | Ch Num | Start Time | Duration | Map |
  +--------+------------+----------+-----+
      1          8           2        1

  Map Field Bit Format.
  +-----+---------------+---------------------+-------+------------+----------+
  | Bss | OFDM Preamble | Unidentified signal | Radar | Unmeasured | Reserved |
  +-----+---------------+---------------------+-------+------------+----------+
     0          1                  2              3         4          5-7
*/
static BOOLEAN PeerMeasureReportSanity(struct rt_rtmp_adapter *pAd,
				       void * pMsg,
				       unsigned long MsgLen,
				       u8 *pDialogToken,
				       struct rt_measure_report_info *
				       pMeasureReportInfo,
				       u8 *pReportBuf)
{
	struct rt_frame_802_11 * Fr = (struct rt_frame_802_11 *) pMsg;
	u8 *pFramePtr = Fr->Octet;
	BOOLEAN result = FALSE;
	struct rt_eid * eid_ptr;
	u8 *ptr;

	/* skip 802.11 header. */
	MsgLen -= sizeof(struct rt_header_802_11);

	/* skip category and action code. */
	pFramePtr += 2;
	MsgLen -= 2;

	if (pMeasureReportInfo == NULL)
		return result;

	NdisMoveMemory(pDialogToken, pFramePtr, 1);
	pFramePtr += 1;
	MsgLen -= 1;

	eid_ptr = (struct rt_eid *) pFramePtr;
	while (((u8 *) eid_ptr + eid_ptr->Len + 1) <
	       ((u8 *)pFramePtr + MsgLen)) {
		switch (eid_ptr->Eid) {
		case IE_MEASUREMENT_REPORT:
			NdisMoveMemory(&pMeasureReportInfo->Token,
				       eid_ptr->Octet, 1);
			NdisMoveMemory(&pMeasureReportInfo->ReportMode,
				       eid_ptr->Octet + 1, 1);
			NdisMoveMemory(&pMeasureReportInfo->ReportType,
				       eid_ptr->Octet + 2, 1);
			if (pMeasureReportInfo->ReportType == RM_BASIC) {
				struct rt_measure_basic_report * pReport =
				    (struct rt_measure_basic_report *) pReportBuf;
				ptr = (u8 *)(eid_ptr->Octet + 3);
				NdisMoveMemory(&pReport->ChNum, ptr, 1);
				NdisMoveMemory(&pReport->MeasureStartTime,
					       ptr + 1, 8);
				NdisMoveMemory(&pReport->MeasureDuration,
					       ptr + 9, 2);
				NdisMoveMemory(&pReport->Map, ptr + 11, 1);

			} else if (pMeasureReportInfo->ReportType == RM_CCA) {
				struct rt_measure_cca_report * pReport =
				    (struct rt_measure_cca_report *) pReportBuf;
				ptr = (u8 *)(eid_ptr->Octet + 3);
				NdisMoveMemory(&pReport->ChNum, ptr, 1);
				NdisMoveMemory(&pReport->MeasureStartTime,
					       ptr + 1, 8);
				NdisMoveMemory(&pReport->MeasureDuration,
					       ptr + 9, 2);
				NdisMoveMemory(&pReport->CCA_Busy_Fraction,
					       ptr + 11, 1);

			} else if (pMeasureReportInfo->ReportType ==
				   RM_RPI_HISTOGRAM) {
				struct rt_measure_rpi_report * pReport =
				    (struct rt_measure_rpi_report *) pReportBuf;
				ptr = (u8 *)(eid_ptr->Octet + 3);
				NdisMoveMemory(&pReport->ChNum, ptr, 1);
				NdisMoveMemory(&pReport->MeasureStartTime,
					       ptr + 1, 8);
				NdisMoveMemory(&pReport->MeasureDuration,
					       ptr + 9, 2);
				NdisMoveMemory(&pReport->RPI_Density, ptr + 11,
					       8);
			}
			result = TRUE;
			break;

		default:
			break;
		}
		eid_ptr = (struct rt_eid *) ((u8 *) eid_ptr + 2 + eid_ptr->Len);
	}

	return result;
}

/*
	==========================================================================
	Description:
		TPC Request action frame sanity check.

	Parametrs:
		1. MLME message containing the received frame
		2. message length.
		3. Dialog Token.

	Return	: None.
	==========================================================================
 */
static BOOLEAN PeerTpcReqSanity(struct rt_rtmp_adapter *pAd,
				void * pMsg,
				unsigned long MsgLen, u8 *pDialogToken)
{
	struct rt_frame_802_11 * Fr = (struct rt_frame_802_11 *) pMsg;
	u8 *pFramePtr = Fr->Octet;
	BOOLEAN result = FALSE;
	struct rt_eid * eid_ptr;

	MsgLen -= sizeof(struct rt_header_802_11);

	/* skip category and action code. */
	pFramePtr += 2;
	MsgLen -= 2;

	if (pDialogToken == NULL)
		return result;

	NdisMoveMemory(pDialogToken, pFramePtr, 1);
	pFramePtr += 1;
	MsgLen -= 1;

	eid_ptr = (struct rt_eid *) pFramePtr;
	while (((u8 *) eid_ptr + eid_ptr->Len + 1) <
	       ((u8 *)pFramePtr + MsgLen)) {
		switch (eid_ptr->Eid) {
		case IE_TPC_REQUEST:
			result = TRUE;
			break;

		default:
			break;
		}
		eid_ptr = (struct rt_eid *) ((u8 *) eid_ptr + 2 + eid_ptr->Len);
	}

	return result;
}

/*
	==========================================================================
	Description:
		TPC Report action frame sanity check.

	Parametrs:
		1. MLME message containing the received frame
		2. message length.
		3. Dialog Token.
		4. TPC Report IE.

	Return	: None.
	==========================================================================
 */
static BOOLEAN PeerTpcRepSanity(struct rt_rtmp_adapter *pAd,
				void * pMsg,
				unsigned long MsgLen,
				u8 *pDialogToken,
				struct rt_tpc_report_info * pTpcRepInfo)
{
	struct rt_frame_802_11 * Fr = (struct rt_frame_802_11 *) pMsg;
	u8 *pFramePtr = Fr->Octet;
	BOOLEAN result = FALSE;
	struct rt_eid * eid_ptr;

	MsgLen -= sizeof(struct rt_header_802_11);

	/* skip category and action code. */
	pFramePtr += 2;
	MsgLen -= 2;

	if (pDialogToken == NULL)
		return result;

	NdisMoveMemory(pDialogToken, pFramePtr, 1);
	pFramePtr += 1;
	MsgLen -= 1;

	eid_ptr = (struct rt_eid *) pFramePtr;
	while (((u8 *) eid_ptr + eid_ptr->Len + 1) <
	       ((u8 *)pFramePtr + MsgLen)) {
		switch (eid_ptr->Eid) {
		case IE_TPC_REPORT:
			NdisMoveMemory(&pTpcRepInfo->TxPwr, eid_ptr->Octet, 1);
			NdisMoveMemory(&pTpcRepInfo->LinkMargin,
				       eid_ptr->Octet + 1, 1);
			result = TRUE;
			break;

		default:
			break;
		}
		eid_ptr = (struct rt_eid *) ((u8 *) eid_ptr + 2 + eid_ptr->Len);
	}

	return result;
}

/*
	==========================================================================
	Description:
		Channel Switch Announcement action frame handler.

	Parametrs:
		Elme - MLME message containing the received frame

	Return	: None.
	==========================================================================
 */
static void PeerChSwAnnAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	struct rt_ch_sw_ann_info ChSwAnnInfo;
	struct rt_frame_802_11 * pFr = (struct rt_frame_802_11 *) Elem->Msg;
	u8 index = 0, Channel = 0, NewChannel = 0;
	unsigned long Bssidx = 0;

	NdisZeroMemory(&ChSwAnnInfo, sizeof(struct rt_ch_sw_ann_info));
	if (!PeerChSwAnnSanity(pAd, Elem->Msg, Elem->MsgLen, &ChSwAnnInfo)) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Invalid Channel Switch Action Frame.\n"));
		return;
	}

	if (pAd->OpMode == OPMODE_STA) {
		Bssidx =
		    BssTableSearch(&pAd->ScanTab, pFr->Hdr.Addr3,
				   pAd->CommonCfg.Channel);
		if (Bssidx == BSS_NOT_FOUND) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("PeerChSwAnnAction - Bssidx is not found\n"));
			return;
		}

		DBGPRINT(RT_DEBUG_TRACE,
			 ("\n****Bssidx is %d, Channel = %d\n", index,
			  pAd->ScanTab.BssEntry[Bssidx].Channel));
		hex_dump("SSID", pAd->ScanTab.BssEntry[Bssidx].Bssid, 6);

		Channel = pAd->CommonCfg.Channel;
		NewChannel = ChSwAnnInfo.Channel;

		if ((pAd->CommonCfg.bIEEE80211H == 1) && (NewChannel != 0)
		    && (Channel != NewChannel)) {
			/* Switching to channel 1 can prevent from rescanning the current channel immediately (by auto reconnection). */
			/* In addition, clear the MLME queue and the scan table to discard the RX packets and previous scanning results. */
			AsicSwitchChannel(pAd, 1, FALSE);
			AsicLockChannel(pAd, 1);
			LinkDown(pAd, FALSE);
			MlmeQueueInit(&pAd->Mlme.Queue);
			BssTableInit(&pAd->ScanTab);
			RTMPusecDelay(1000000);	/* use delay to prevent STA do reassoc */

			/* channel sanity check */
			for (index = 0; index < pAd->ChannelListNum; index++) {
				if (pAd->ChannelList[index].Channel ==
				    NewChannel) {
					pAd->ScanTab.BssEntry[Bssidx].Channel =
					    NewChannel;
					pAd->CommonCfg.Channel = NewChannel;
					AsicSwitchChannel(pAd,
							  pAd->CommonCfg.
							  Channel, FALSE);
					AsicLockChannel(pAd,
							pAd->CommonCfg.Channel);
					DBGPRINT(RT_DEBUG_TRACE,
						 ("&&&&&&&&&&&&&&&&PeerChSwAnnAction - STA receive channel switch announcement IE (New Channel =%d)\n",
						  NewChannel));
					break;
				}
			}

			if (index >= pAd->ChannelListNum) {
				DBGPRINT_ERR("&&&&&&&&&&&&&&&&&&&&&&&&&&PeerChSwAnnAction(can not find New Channel=%d in ChannelList[%d]\n", pAd->CommonCfg.Channel, pAd->ChannelListNum);
			}
		}
	}

	return;
}

/*
	==========================================================================
	Description:
		Measurement Request action frame handler.

	Parametrs:
		Elme - MLME message containing the received frame

	Return	: None.
	==========================================================================
 */
static void PeerMeasureReqAction(struct rt_rtmp_adapter *pAd,
				 struct rt_mlme_queue_elem *Elem)
{
	struct rt_frame_802_11 * pFr = (struct rt_frame_802_11 *) Elem->Msg;
	u8 DialogToken;
	struct rt_measure_req_info MeasureReqInfo;
	struct rt_measure_req MeasureReq;
	MEASURE_REPORT_MODE ReportMode;

	if (PeerMeasureReqSanity
	    (pAd, Elem->Msg, Elem->MsgLen, &DialogToken, &MeasureReqInfo,
	     &MeasureReq)) {
		ReportMode.word = 0;
		ReportMode.field.Incapable = 1;
		EnqueueMeasurementRep(pAd, pFr->Hdr.Addr2, DialogToken,
				      MeasureReqInfo.Token, ReportMode.word,
				      MeasureReqInfo.ReqType, 0, NULL);
	}

	return;
}

/*
	==========================================================================
	Description:
		Measurement Report action frame handler.

	Parametrs:
		Elme - MLME message containing the received frame

	Return	: None.
	==========================================================================
 */
static void PeerMeasureReportAction(struct rt_rtmp_adapter *pAd,
				    struct rt_mlme_queue_elem *Elem)
{
	struct rt_measure_report_info MeasureReportInfo;
	struct rt_frame_802_11 * pFr = (struct rt_frame_802_11 *) Elem->Msg;
	u8 DialogToken;
	u8 *pMeasureReportInfo;

/*      if (pAd->CommonCfg.bIEEE80211H != TRUE) */
/*              return; */

	pMeasureReportInfo = kmalloc(sizeof(struct rt_measure_rpi_report), GFP_ATOMIC);
	if (pMeasureReportInfo == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s unable to alloc memory for measure report buffer (size=%zu).\n",
			  __func__, sizeof(struct rt_measure_rpi_report)));
		return;
	}

	NdisZeroMemory(&MeasureReportInfo, sizeof(struct rt_measure_report_info));
	NdisZeroMemory(pMeasureReportInfo, sizeof(struct rt_measure_rpi_report));
	if (PeerMeasureReportSanity
	    (pAd, Elem->Msg, Elem->MsgLen, &DialogToken, &MeasureReportInfo,
	     pMeasureReportInfo)) {
		do {
			struct rt_measure_req_entry *pEntry = NULL;

			/* Not a autonomous measure report. */
			/* check the dialog token field. drop it if the dialog token doesn't match. */
			if ((DialogToken != 0)
			    && ((pEntry = MeasureReqLookUp(pAd, DialogToken)) ==
				NULL))
				break;

			if (pEntry != NULL)
				MeasureReqDelete(pAd, pEntry->DialogToken);

			if (MeasureReportInfo.ReportType == RM_BASIC) {
				struct rt_measure_basic_report * pBasicReport =
				    (struct rt_measure_basic_report *) pMeasureReportInfo;
				if ((pBasicReport->Map.field.Radar)
				    &&
				    (DfsRequirementCheck
				     (pAd, pBasicReport->ChNum) == TRUE)) {
					NotifyChSwAnnToPeerAPs(pAd,
							       pFr->Hdr.Addr1,
							       pFr->Hdr.Addr2,
							       1,
							       pBasicReport->
							       ChNum);
					StartDFSProcedure(pAd,
							  pBasicReport->ChNum,
							  1);
				}
			}
		} while (FALSE);
	} else
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Invalid Measurement Report Frame.\n"));

	kfree(pMeasureReportInfo);

	return;
}

/*
	==========================================================================
	Description:
		TPC Request action frame handler.

	Parametrs:
		Elme - MLME message containing the received frame

	Return	: None.
	==========================================================================
 */
static void PeerTpcReqAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	struct rt_frame_802_11 * pFr = (struct rt_frame_802_11 *) Elem->Msg;
	u8 *pFramePtr = pFr->Octet;
	u8 DialogToken;
	u8 TxPwr = GetCurTxPwr(pAd, Elem->Wcid);
	u8 LinkMargin = 0;
	char RealRssi;

	/* link margin: Ratio of the received signal power to the minimum desired by the station (STA). The */
	/*                              STA may incorporate rate information and channel conditions, including interference, into its computation */
	/*                              of link margin. */

	RealRssi = RTMPMaxRssi(pAd, ConvertToRssi(pAd, Elem->Rssi0, RSSI_0),
			       ConvertToRssi(pAd, Elem->Rssi1, RSSI_1),
			       ConvertToRssi(pAd, Elem->Rssi2, RSSI_2));

	/* skip Category and action code. */
	pFramePtr += 2;

	/* Dialog token. */
	NdisMoveMemory(&DialogToken, pFramePtr, 1);

	LinkMargin = (RealRssi / MIN_RCV_PWR);
	if (PeerTpcReqSanity(pAd, Elem->Msg, Elem->MsgLen, &DialogToken))
		EnqueueTPCRep(pAd, pFr->Hdr.Addr2, DialogToken, TxPwr,
			      LinkMargin);

	return;
}

/*
	==========================================================================
	Description:
		TPC Report action frame handler.

	Parametrs:
		Elme - MLME message containing the received frame

	Return	: None.
	==========================================================================
 */
static void PeerTpcRepAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{
	u8 DialogToken;
	struct rt_tpc_report_info TpcRepInfo;
	struct rt_tpc_req_entry *pEntry = NULL;

	NdisZeroMemory(&TpcRepInfo, sizeof(struct rt_tpc_report_info));
	if (PeerTpcRepSanity
	    (pAd, Elem->Msg, Elem->MsgLen, &DialogToken, &TpcRepInfo)) {
		pEntry = TpcReqLookUp(pAd, DialogToken);
		if (pEntry != NULL) {
			TpcReqDelete(pAd, pEntry->DialogToken);
			DBGPRINT(RT_DEBUG_TRACE,
				 ("%s: DialogToken=%x, TxPwr=%d, LinkMargin=%d\n",
				  __func__, DialogToken, TpcRepInfo.TxPwr,
				  TpcRepInfo.LinkMargin));
		}
	}

	return;
}

/*
	==========================================================================
	Description:
		Spectrun action frames Handler such as channel switch annoucement,
		measurement report, measurement request actions frames.

	Parametrs:
		Elme - MLME message containing the received frame

	Return	: None.
	==========================================================================
 */
void PeerSpectrumAction(struct rt_rtmp_adapter *pAd, struct rt_mlme_queue_elem *Elem)
{

	u8 Action = Elem->Msg[LENGTH_802_11 + 1];

	if (pAd->CommonCfg.bIEEE80211H != TRUE)
		return;

	switch (Action) {
	case SPEC_MRQ:
		/* current rt2860 unable do such measure specified in Measurement Request. */
		/* reject all measurement request. */
		PeerMeasureReqAction(pAd, Elem);
		break;

	case SPEC_MRP:
		PeerMeasureReportAction(pAd, Elem);
		break;

	case SPEC_TPCRQ:
		PeerTpcReqAction(pAd, Elem);
		break;

	case SPEC_TPCRP:
		PeerTpcRepAction(pAd, Elem);
		break;

	case SPEC_CHANNEL_SWITCH:

		PeerChSwAnnAction(pAd, Elem);
		break;
	}

	return;
}

/*
	==========================================================================
	Description:

	Parametrs:

	Return	: None.
	==========================================================================
 */
int Set_MeasureReq_Proc(struct rt_rtmp_adapter *pAd, char *arg)
{
	u32 Aid = 1;
	u32 ArgIdx;
	char *thisChar;

	MEASURE_REQ_MODE MeasureReqMode;
	u8 MeasureReqToken = RandomByte(pAd);
	u8 MeasureReqType = RM_BASIC;
	u8 MeasureCh = 1;
	u64 MeasureStartTime = GetCurrentTimeStamp(pAd);
	struct rt_measure_req MeasureReq;
	u8 TotalLen;

	struct rt_header_802_11 ActHdr;
	u8 *pOutBuffer = NULL;
	int NStatus;
	unsigned long FrameLen;

	NStatus = MlmeAllocateMemory(pAd, (void *)& pOutBuffer);	/*Get an unused nonpaged memory */
	if (NStatus != NDIS_STATUS_SUCCESS) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("%s() allocate memory failed \n", __func__));
		goto END_OF_MEASURE_REQ;
	}

	ArgIdx = 1;
	while ((thisChar = strsep((char **)&arg, "-")) != NULL) {
		switch (ArgIdx) {
		case 1:	/* Aid. */
			Aid = (u8)simple_strtol(thisChar, 0, 16);
			break;

		case 2:	/* Measurement Request Type. */
			MeasureReqType = simple_strtol(thisChar, 0, 16);
			if (MeasureReqType > 3) {
				DBGPRINT(RT_DEBUG_ERROR,
					 ("%s: unknow MeasureReqType(%d)\n",
					  __func__, MeasureReqType));
				goto END_OF_MEASURE_REQ;
			}
			break;

		case 3:	/* Measurement channel. */
			MeasureCh = (u8)simple_strtol(thisChar, 0, 16);
			break;
		}
		ArgIdx++;
	}

	DBGPRINT(RT_DEBUG_TRACE,
		 ("%s::Aid = %d, MeasureReqType=%d MeasureCh=%d\n", __func__,
		  Aid, MeasureReqType, MeasureCh));
	if (!VALID_WCID(Aid)) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: unknow sta of Aid(%d)\n", __func__, Aid));
		goto END_OF_MEASURE_REQ;
	}

	MeasureReqMode.word = 0;
	MeasureReqMode.field.Enable = 1;

	MeasureReqInsert(pAd, MeasureReqToken);

	/* build action frame header. */
	MgtMacHeaderInit(pAd, &ActHdr, SUBTYPE_ACTION, 0,
			 pAd->MacTab.Content[Aid].Addr, pAd->CurrentAddress);

	NdisMoveMemory(pOutBuffer, (char *)& ActHdr, sizeof(struct rt_header_802_11));
	FrameLen = sizeof(struct rt_header_802_11);

	TotalLen = sizeof(struct rt_measure_req_info) + sizeof(struct rt_measure_req);

	MakeMeasurementReqFrame(pAd, pOutBuffer, &FrameLen,
				sizeof(struct rt_measure_req_info), CATEGORY_RM, RM_BASIC,
				MeasureReqToken, MeasureReqMode.word,
				MeasureReqType, 0);

	MeasureReq.ChNum = MeasureCh;
	MeasureReq.MeasureStartTime = cpu2le64(MeasureStartTime);
	MeasureReq.MeasureDuration = cpu2le16(2000);

	{
		unsigned long TempLen;
		MakeOutgoingFrame(pOutBuffer + FrameLen, &TempLen,
				  sizeof(struct rt_measure_req), &MeasureReq,
				  END_OF_ARGS);
		FrameLen += TempLen;
	}

	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, (u32)FrameLen);

END_OF_MEASURE_REQ:
	MlmeFreeMemory(pAd, pOutBuffer);

	return TRUE;
}

int Set_TpcReq_Proc(struct rt_rtmp_adapter *pAd, char *arg)
{
	u32 Aid;

	u8 TpcReqToken = RandomByte(pAd);

	Aid = (u32)simple_strtol(arg, 0, 16);

	DBGPRINT(RT_DEBUG_TRACE, ("%s::Aid = %d\n", __func__, Aid));
	if (!VALID_WCID(Aid)) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("%s: unknow sta of Aid(%d)\n", __func__, Aid));
		return TRUE;
	}

	TpcReqInsert(pAd, TpcReqToken);

	EnqueueTPCReq(pAd, pAd->MacTab.Content[Aid].Addr, TpcReqToken);

	return TRUE;
}
