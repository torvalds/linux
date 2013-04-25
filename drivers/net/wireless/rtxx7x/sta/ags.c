/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#define MODULE_AGS
#include "rt_config.h"
    
#ifdef AGS_SUPPORT
    

/* */
/* AGS: 1x1 HT-capable rate table */
/* */
    UCHAR AGS1x1HTRateTable[] = 
 {
	
	    /* */
	    /* [Item no.] [Mode]* [CurrMCS] [TrainUp] [TrainDown] [downMCS  ] [upMCS3] [upMCS2] [upMCS1] */
	    /* */
	    /* [Mode]*: */
	    /* bit0: STBC */
	    /* bit1: Short GI */
	    /* bit2: BW */
	    /* bit4~bit5: Mode (0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	    /* */
	    0x09, 0x08, 0, 0, 0, 0, 0, 0, 0,	/* Initial used item after association: the number of rate indexes, the initial mcs */
	    0x00, 0x21, 0, 30, 101, 0, 16, 8, 1,	/* MCS 0 */
	    0x01, 0x21, 1, 20, 50, 0, 16, 9, 2,	/* MCS 1 */
	    0x02, 0x21, 2, 20, 50, 1, 17, 9, 3,	/* MCS 2 */
	    0x03, 0x21, 3, 15, 50, 2, 17, 10, 4,	/* MCS 3 */
	    0x04, 0x21, 4, 15, 30, 3, 18, 11, 5,	/* MCS 4 */
	    0x05, 0x21, 5, 10, 25, 4, 18, 12, 6,	/* MCS 5 */
	    0x06, 0x21, 6, 8, 14, 5, 19, 12, 7,	/* MCS 6 */
	    0x07, 0x21, 7, 8, 14, 6, 19, 12, 8,	/* MCS 7 */
	    0x08, 0x23, 7, 8, 14, 7, 19, 12, 8,	/* MCS 7 + Short GI */
};



/* */
/* AGS: 2x2 HT-capable rate table */
/* */
    UCHAR AGS2x2HTRateTable[] = 
 {
	
	    /* */
	    /* [Item no.] [Mode]* [CurrMCS] [TrainUp] [TrainDown] [downMCS  ] [upMCS3] [upMCS2] [upMCS1] */
	    /* */
	    /* [Mode]*: */
	    /* bit0: STBC */
	    /* bit1: Short GI */
	    /* bit2: BW */
	    /* bit4~bit5: Mode (0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	    /* */
	    0x11, 0x10, 0, 0, 0, 0, 0, 0, 0,	/* Initial used item after association: the number of rate indexes, the initial mcs */
	    0x00, 0x21, 0, 30, 101, 0, 16, 8, 1,	/* MCS 0 */
	    0x01, 0x21, 1, 20, 50, 0, 16, 9, 2,	/* MCS 1 */
	    0x02, 0x21, 2, 20, 50, 1, 17, 9, 3,	/* MCS 2 */
	    0x03, 0x21, 3, 15, 50, 2, 17, 10, 4,	/* MCS 3 */
	    0x04, 0x21, 4, 15, 30, 3, 18, 11, 5,	/* MCS 4 */
	    0x05, 0x21, 5, 10, 25, 4, 18, 12, 6,	/* MCS 5 */
	    0x06, 0x21, 6, 8, 14, 5, 19, 12, 7,	/* MCS 6 */
	    0x07, 0x21, 7, 8, 14, 6, 19, 12, 7,	/* MCS 7 */
	    0x08, 0x20, 8, 30, 50, 0, 16, 9, 2,	/* MCS 8 */
	    0x09, 0x20, 9, 20, 50, 8, 17, 10, 4,	/* MCS 9 */
	    0x0A, 0x20, 10, 20, 50, 9, 18, 11, 5,	/* MCS 10 */
	    0x0B, 0x20, 11, 15, 30, 10, 18, 12, 6,	/* MCS 11 */
	    0x0C, 0x20, 12, 15, 30, 11, 20, 13, 12,	/* MCS 12 */
	    0x0D, 0x20, 13, 8, 20, 12, 20, 14, 13,	/* MCS 13 */
	    0x0E, 0x20, 14, 8, 18, 13, 21, 15, 14,	/* MCS 14 */
	    0x0F, 0x20, 15, 8, 25, 14, 21, 16, 15,	/* MCS 15 */
	    0x10, 0x22, 15, 8, 25, 15, 21, 16, 16,	/* MCS 15 + Short GI */
};



/* */
/* AGS: 3x3 HT-capable rate table */
/* */
    UCHAR AGS3x3HTRateTable[] = 
 {
	
	    /* */
	    /* [Item no.] [Mode]* [CurrMCS] [TrainUp] [TrainDown] [downMCS  ] [upMCS3] [upMCS2] [upMCS1] */
	    /* */
	    /* [Mode]*: */
	    /* bit0: STBC */
	    /* bit1: Short GI */
	    /* bit2: BW */
	    /* bit4~bit5: Mode (0:CCK, 1:OFDM, 2:HT Mix, 3:HT GF) */
	    /* */
	    0x19, 0x18, 0, 0, 0, 0, 0, 0, 0,	/* Initial used item after association: the number of rate indexes, the initial mcs */
	    0x00, 0x21, 0, 30, 101, 0, 16, 8, 1,	/* MCS 0 */
	    0x01, 0x21, 1, 20, 50, 0, 16, 9, 2,	/* MCS 1 */
	    0x02, 0x21, 2, 20, 50, 1, 17, 9, 3,	/* MCS 2 */
	    0x03, 0x21, 3, 15, 50, 2, 17, 10, 4,	/* MCS 3 */
	    0x04, 0x21, 4, 15, 30, 3, 18, 11, 5,	/* MCS 4 */
	    0x05, 0x21, 5, 10, 25, 4, 18, 12, 6,	/* MCS 5 */
	    0x06, 0x21, 6, 8, 14, 5, 19, 12, 7,	/* MCS 6 */
	    0x07, 0x21, 7, 8, 14, 6, 19, 12, 7,	/* MCS 7 */
	    0x08, 0x20, 8, 30, 50, 0, 16, 9, 2,	/* MCS 8 */
	    0x09, 0x20, 9, 20, 50, 8, 17, 10, 4,	/* MCS 9 */
	    0x0A, 0x20, 10, 20, 50, 9, 18, 11, 5,	/* MCS 10 */
	    0x0B, 0x20, 11, 15, 30, 10, 18, 12, 6,	/* MCS 11 */
	    0x0C, 0x20, 12, 15, 30, 11, 20, 13, 12,	/* MCS 12 */
	    0x0D, 0x20, 13, 8, 20, 12, 20, 14, 13,	/* MCS 13 */
	    0x0E, 0x20, 14, 8, 18, 13, 21, 15, 14,	/* MCS 14 */
	    0x0F, 0x20, 15, 8, 14, 14, 21, 15, 15,	/* MCS 15 */
	    0x10, 0x20, 16, 30, 50, 8, 17, 9, 3,	/* MCS 16 */
	    0x11, 0x20, 17, 20, 50, 16, 18, 11, 5,	/* MCS 17 */
	    0x12, 0x20, 18, 20, 50, 17, 19, 12, 7,	/* MCS 18 */
	    0x13, 0x20, 19, 15, 30, 18, 20, 13, 19,	/* MCS 19 */
	    0x14, 0x20, 20, 15, 30, 19, 21, 15, 20,	/* MCS 20 */
	    0x15, 0x20, 21, 8, 20, 20, 22, 21, 21,	/* MCS 21 */
	    0x16, 0x20, 22, 8, 20, 21, 23, 22, 22,	/* MCS 22 */
	    0x17, 0x20, 23, 6, 18, 22, 24, 23, 23,	/* MCS 23 */
	    0x18, 0x22, 23, 6, 14, 23, 24, 24, 24,	/* MCS 23 + Short GI */
};




INT Show_AGS_Proc(
    IN  PRTMP_ADAPTER	pAd, 
    IN  PSTRING			arg)
{
	MAC_TABLE_ENTRY *pEntry = &pAd->MacTab.Content[1];
	UINT32 IdQuality;


	printk("MCS Group\t\tMCS Index\n");
	printk("%d\t\t\t%d\n\n", pEntry->AGSCtrl.MCSGroup, pEntry->CurrTxRateIndex);

	printk("MCS Quality:\n");
	for(IdQuality=0; IdQuality<=23; IdQuality++)
		printk("%02d\t\t%d\n", IdQuality, pEntry->TxQuality[IdQuality]);

	return TRUE;
}


/* */
/* The dynamic Tx rate switching for AGS (Adaptive Group Switching) */
/* */
/* Parameters */
/*	pAd: The adapter data structure */
/*	pEntry: Pointer to a caller-supplied variable in which points to a MAC table entry */
/*	pTable: Pointer to a caller-supplied variable in wich points to a Tx rate switching table */
/*	TableSize: The size, in bytes, of the specified Tx rate switching table */
/*	pAGSStatisticsInfo: Pointer to a caller-supplied variable in which points to the statistics information */
/* */
/* Return Value: */
/*	None */
/* */
VOID MlmeDynamicTxRateSwitchingAGS(
	IN PRTMP_ADAPTER pAd, 
	IN PMAC_TABLE_ENTRY pEntry, 
	IN PUCHAR pTable, 
	IN UCHAR TableSize, 
	IN PAGS_STATISTICS_INFO pAGSStatisticsInfo,
	IN UCHAR InitTxRateIdx)
{
	UCHAR UpRateIdx = 0, DownRateIdx = 0, CurrRateIdx = 0;
	BOOLEAN bTxRateChanged = TRUE, bUpgradeQuality = FALSE;
	PRTMP_TX_RATE_SWITCH_AGS pCurrTxRate = NULL;
	PRTMP_TX_RATE_SWITCH	pNextTxRate = NULL;
	UCHAR TrainUp = 0, TrainDown = 0;
	CHAR RssiOffset = 0;

	DBGPRINT_RAW(RT_DEBUG_TRACE, ("\n\nAGS: ---> %s\n", __FUNCTION__));

	DBGPRINT(RT_DEBUG_TRACE, ("%s: QuickAGS: AccuTxTotalCnt = %lu, TxSuccess = %lu, TxRetransmit = %lu, TxFailCount = %lu, TxErrorRatio = %lu\n",
		__FUNCTION__, 
		pAGSStatisticsInfo->AccuTxTotalCnt, 
		pAGSStatisticsInfo->TxSuccess, 
		pAGSStatisticsInfo->TxRetransmit, 
		pAGSStatisticsInfo->TxFailCount, 
		pAGSStatisticsInfo->TxErrorRatio));

	/* for 3*3, 1st time, pEntry->CurrTxRateIndex = 24 in StaAddMacTableEntry() */
	CurrRateIdx = pEntry->CurrTxRateIndex;	

	if (CurrRateIdx >= TableSize)
	{
		CurrRateIdx = TableSize - 1;
	}

	pCurrTxRate = (PRTMP_TX_RATE_SWITCH_AGS)(&pTable[(CurrRateIdx + 1) *
												SIZE_OF_AGS_RATE_TABLE_ENTRY]);

	/* */
	/* Select the next upgrade rate and the next downgrade rate, if any */
	/* */
	do 
	{
		if (InitTxRateIdx == AGS3x3HTRateTable[1])
		{
			/* 3*3 table */
			DBGPRINT_RAW(RT_DEBUG_TRACE,
				("%s: AGS: pEntry->AGSCtrl.MCSGroup = %d, TxQuality2[%d] = %d,  "
				"TxQuality1[%d] = %d, TxQuality0[%d] = %d, pCurrTxRate->upMcs1 = %d, "
				"pCurrTxRate->ItemNo = %d\n",
				__FUNCTION__, 
				pEntry->AGSCtrl.MCSGroup, 
				pCurrTxRate->upMcs3, 
				pEntry->TxQuality[pCurrTxRate->upMcs3], 
				pCurrTxRate->upMcs2, 
				pEntry->TxQuality[pCurrTxRate->upMcs2], 
				pCurrTxRate->upMcs1, 
				pEntry->TxQuality[pCurrTxRate->upMcs1], 
				pCurrTxRate->upMcs1, 
				pCurrTxRate->ItemNo));

			/* */
			/* 3x3 peer device (Adhoc, DLS or AP) */
			/* */
			/* for 3*3, pEntry->AGSCtrl.MCSGroup = 0, 3, 3, 3, ... */
			switch (pEntry->AGSCtrl.MCSGroup)
			{
				case 0: /* MCS selection in round robin policy (different MCS group) */
				{
					UpRateIdx = pCurrTxRate->upMcs3;
					DBGPRINT_RAW(RT_DEBUG_TRACE,
							("ags> MCSGroup = 0; UpRateIdx = %d in group 3 is better.\n",
							UpRateIdx));

					/* MCS group #2 has better Tx quality */
					if ((pEntry->TxQuality[UpRateIdx] >
								pEntry->TxQuality[pCurrTxRate->upMcs2]) && 
					     (pCurrTxRate->upMcs2 != pCurrTxRate->ItemNo))
					{
						/* quality for group 2 is better */
						UpRateIdx = pCurrTxRate->upMcs2;

						DBGPRINT_RAW(RT_DEBUG_TRACE,
							("ags> MCSGroup = 0; UpRateIdx = %d in group 2 is better.\n",
							UpRateIdx));
					}

					/* MCS group #1 has better Tx quality */
					if ((pEntry->TxQuality[UpRateIdx] >
								pEntry->TxQuality[pCurrTxRate->upMcs1]) && 
					     (pCurrTxRate->upMcs1 != pCurrTxRate->ItemNo))
					{
						/* quality for group 1 is better */
						UpRateIdx = pCurrTxRate->upMcs1;

						DBGPRINT_RAW(RT_DEBUG_TRACE,
							("ags> MCSGroup = 0; UpRateIdx = %d in group 1 is better.\n",
							UpRateIdx));
					}
				}
				break;
				
				case 3:
				{
					UpRateIdx = pCurrTxRate->upMcs3;

					DBGPRINT_RAW(RT_DEBUG_TRACE,
							("ags> MCSGroup = 3; UpRateIdx = %d\n",
							UpRateIdx));
				}
				break;
				
				case 2:
				{
					UpRateIdx = pCurrTxRate->upMcs2;

					DBGPRINT_RAW(RT_DEBUG_TRACE,
							("ags> MCSGroup = 2; UpRateIdx = %d\n",
							UpRateIdx));
				}
				break;
				
				case 1:
				{
					UpRateIdx = pCurrTxRate->upMcs1;

					DBGPRINT_RAW(RT_DEBUG_TRACE,
							("ags> MCSGroup = 1; UpRateIdx = %d\n",
							UpRateIdx));
				}
				break;
				
				default:
				{
					DBGPRINT_RAW(RT_DEBUG_ERROR,
						("%s: AGS: [3x3 peer device (Adhoc, DLS or AP)], "
						"Incorrect MCS group, pEntry->AGSCtrl.MCSGroup = %d\n", 
						__FUNCTION__, 
						pEntry->AGSCtrl.MCSGroup));
				}
				break;
			}			

			if ((pEntry->AGSCtrl.MCSGroup == 0) && 
			     (((pEntry->TxQuality[pCurrTxRate->upMcs3] >
						pEntry->TxQuality[pCurrTxRate->upMcs2]) &&
					(pCurrTxRate->upMcs2 != pCurrTxRate->ItemNo)) || 
			     ((pEntry->TxQuality[pCurrTxRate->upMcs3] >
						pEntry->TxQuality[pCurrTxRate->upMcs1]) &&
					(pCurrTxRate->upMcs1 != pCurrTxRate->ItemNo))))
			{
				/* quality of group 2 or 1 is better than group 3 */
				/* just show debug information here */
				DBGPRINT_RAW(RT_DEBUG_TRACE,
					("%s: ###################################################"
					"#######################\n", 
					__FUNCTION__));

				DBGPRINT_RAW(RT_DEBUG_TRACE,
					("%s: AGS: [3x3 peer device (Adhoc, DLS or AP)], Before - "
					"pEntry->AGSCtrl.MCSGroup = %d, TxQuality2[%d] = %d,  "
					"TxQuality1[%d] = %d, TxQuality0[%d] = %d\n",
					__FUNCTION__, 
					pEntry->AGSCtrl.MCSGroup, 
					pCurrTxRate->upMcs3, 
					pEntry->TxQuality[pCurrTxRate->upMcs3], 
					pCurrTxRate->upMcs2, 
					pEntry->TxQuality[pCurrTxRate->upMcs2], 
					pCurrTxRate->upMcs1, 
					pEntry->TxQuality[pCurrTxRate->upMcs1]));
			}
		}
		else if (InitTxRateIdx == AGS2x2HTRateTable[1])
		{
			/* 2*2 table */

			DBGPRINT_RAW(RT_DEBUG_TRACE,
				("%s: AGS: pEntry->AGSCtrl.MCSGroup = %d, TxQuality1[%d] = %d, "
				"TxQuality0[%d] = %d, pCurrTxRate->upMcs1 = %d, "
				"pCurrTxRate->ItemNo = %d\n",
				__FUNCTION__, 
				pEntry->AGSCtrl.MCSGroup, 
				pCurrTxRate->upMcs2, 
				pEntry->TxQuality[pCurrTxRate->upMcs2], 
				pCurrTxRate->upMcs1, 
				pEntry->TxQuality[pCurrTxRate->upMcs1], 
				pCurrTxRate->upMcs1, 
				pCurrTxRate->ItemNo));

			/* */
			/* 2x2 peer device (Adhoc, DLS or AP) */
			/* */
			switch (pEntry->AGSCtrl.MCSGroup)
			{
				case 0: /* MCS selection in round robin policy */
				{
					UpRateIdx = pCurrTxRate->upMcs2;

					/* MCS group #1 has better Tx quality */
					if ((pEntry->TxQuality[UpRateIdx] >
								pEntry->TxQuality[pCurrTxRate->upMcs1]) && 
					     (pCurrTxRate->upMcs1 != pCurrTxRate->ItemNo))
					{
						UpRateIdx = pCurrTxRate->upMcs1;
					}
				}
				break;
				
				case 2:
				{
					UpRateIdx = pCurrTxRate->upMcs2;
				}
				break;
				
				case 1:
				{
					UpRateIdx = pCurrTxRate->upMcs1;
				}
				break;
				
				default:
				{
					DBGPRINT_RAW(RT_DEBUG_ERROR,
						("%s: AGS: [2x2 peer device (Adhoc, DLS or AP)], "
						"Incorrect MCS group, pEntry->AGSCtrl.MCSGroup = %d\n", 
						__FUNCTION__, 
						pEntry->AGSCtrl.MCSGroup));
				}
				break;
			}	

			if ((pEntry->AGSCtrl.MCSGroup == 0) && 
			     ((pEntry->TxQuality[pCurrTxRate->upMcs2] >
					pEntry->TxQuality[pCurrTxRate->upMcs1]) &&
					(pCurrTxRate->upMcs1 != pCurrTxRate->ItemNo)))
			{
				DBGPRINT_RAW(RT_DEBUG_TRACE,
					("%s: ###################################################"
					"#######################\n", 
					__FUNCTION__));

				DBGPRINT_RAW(RT_DEBUG_TRACE,
					("%s: AGS: [2x2 peer device (Adhoc, DLS or AP)], Before - "
					"pEntry->AGSCtrl.MCSGroup = %d, TxQuality1[%d] = %d, "
					"TxQuality0[%d] = %d\n",
					__FUNCTION__, 
					pEntry->AGSCtrl.MCSGroup, 
					pCurrTxRate->upMcs2, 
					pEntry->TxQuality[pCurrTxRate->upMcs2], 
					pCurrTxRate->upMcs1, 
					pEntry->TxQuality[pCurrTxRate->upMcs1]));
			}
		} 
		else 
		{
			/* */
			/* 1x1 peer device (Adhoc, DLS or AP) */
			/* */
			switch (pEntry->AGSCtrl.MCSGroup)
			{
				case 1:
				case 0:
				{
					UpRateIdx = pCurrTxRate->upMcs1;
				}
				break;
				
				default:
				{
					DBGPRINT_RAW(RT_DEBUG_ERROR,
						("%s: AGS: [1x1 peer device (Adhoc, DLS or AP)], "
						"Incorrect MCS group, pEntry->AGSCtrl.MCSGroup = %d\n", 
						__FUNCTION__, 
						pEntry->AGSCtrl.MCSGroup));
				}
				break;
			}	
		}


		/* */
		/* The STA uses the best Tx rate at this moment. */
		/* */
		if (UpRateIdx == pEntry->CurrTxRateIndex)
		{
			/* current rate is the best one */
			pEntry->AGSCtrl.MCSGroup = 0; /* Try to escape the local optima */

			DBGPRINT_RAW(RT_DEBUG_TRACE,
						("ags> Current rate is the best one!\n"));
			break;
		}
		
		if ((pEntry->TxQuality[UpRateIdx] > 0) &&
			(pEntry->AGSCtrl.MCSGroup > 0))
		{
			/*
				Quality of up rate is bad try to use lower group.
				So continue to get the up rate index.
			*/
			pEntry->AGSCtrl.MCSGroup--; /* Try to use the MCS of the lower MCS group */
		}
		else
		{
			break;
		}
	} while (1);


	DownRateIdx = pCurrTxRate->downMcs;

	DBGPRINT_RAW(RT_DEBUG_TRACE,
				("ags> UpRateIdx = %d, DownRateIdx = %d\n",
				UpRateIdx, DownRateIdx));

#ifdef DOT11_N_SUPPORT
	if ((pAGSStatisticsInfo->RSSI > -65) &&
		(pCurrTxRate->Mode >= MODE_HTMIX))
	{
		TrainUp = (pCurrTxRate->TrainUp + (pCurrTxRate->TrainUp >> 1));
		TrainDown	= (pCurrTxRate->TrainDown + (pCurrTxRate->TrainDown >> 1));
	}
	else
#endif /* DOT11_N_SUPPORT */
	{
		TrainUp = pCurrTxRate->TrainUp;
		TrainDown	= pCurrTxRate->TrainDown;
	}

	/* */
	/* Keep the TxRateChangeAction status */
	/* */
	pEntry->LastTimeTxRateChangeAction = pEntry->LastSecTxRateChangeAction;



	/* */
	/* MCS selection based on the RSSI information when the Tx samples are fewer than 15. */
	/* */
	if (pAGSStatisticsInfo->AccuTxTotalCnt <= 15)
	{
		CHAR idx = 0;
		UCHAR TxRateIdx;
		UCHAR MCS0 = 0, MCS1 = 0, MCS2 = 0, MCS3 = 0, MCS4 = 0,  MCS5 =0, MCS6 = 0, MCS7 = 0;	
		UCHAR MCS8 = 0, MCS9 = 0, MCS10 = 0, MCS11 = 0, MCS12 = 0, MCS13 = 0, MCS14 = 0, MCS15 = 0;
		UCHAR MCS16 = 0, MCS17 = 0, MCS18 = 0, MCS19 = 0, MCS20 = 0, MCS21 = 0, MCS22 = 0, MCS23 = 0;

		/* */
		/* Check the existence and index of each needed MCS */
		/* */
		/* for 3*3, maximum is 0x19 columns */
		while (idx < pTable[0])
		{
			pCurrTxRate = (PRTMP_TX_RATE_SWITCH_AGS)(&pTable[(idx + 1) * SIZE_OF_AGS_RATE_TABLE_ENTRY]);

			if (pCurrTxRate->CurrMCS == MCS_0)
			{
				MCS0 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_1)
			{
				MCS1 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_2)
			{
				MCS2 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_3)
			{
				MCS3 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_4)
			{
				MCS4 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_5)
			{
			 	MCS5 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_6)
			{
			    MCS6 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_7)
			{
				MCS7 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_8)
			{
				MCS8 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_9)
			{
				MCS9 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_10)
			{
				MCS10 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_11)
			{
				MCS11 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_12)
			{
				MCS12 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_13)
			{
				MCS13 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_14)
			{
				MCS14 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_15)
			{
				MCS15 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_16)
			{
				MCS16 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_17)
			{
				MCS17 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_18)
			{
				MCS18 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_19)
			{
				MCS19 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_20)
			{
				MCS20 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_21)
			{
				MCS21 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_22)
			{
				MCS22 = idx;
			}
			else if (pCurrTxRate->CurrMCS == MCS_23)
			{
				MCS23 = idx;
			}
			
			idx++;
		}

		RssiOffset = 0;
		
		if (InitTxRateIdx == AGS3x3HTRateTable[1])
		{
			/* */
			/* 3x3 peer device (Adhoc, DLS or AP) */
			/* */
			if (MCS23 && (pAGSStatisticsInfo->RSSI > (-67 + RssiOffset)))
			{
				TxRateIdx = MCS23;
			}
			else if (MCS22 && (pAGSStatisticsInfo->RSSI > (-69 + RssiOffset)))
			{
				TxRateIdx = MCS22;
			}
			else if (MCS21 && (pAGSStatisticsInfo->RSSI > (-72 + RssiOffset)))
			{
				TxRateIdx = MCS21;
			}
			else if (MCS20 && (pAGSStatisticsInfo->RSSI > (-74 + RssiOffset)))
			{
				TxRateIdx = MCS20;
			}
			else if (MCS19 && (pAGSStatisticsInfo->RSSI > (-78 + RssiOffset)))
			{
				TxRateIdx = MCS19;
			}
			else if (MCS18 && (pAGSStatisticsInfo->RSSI > (-80 + RssiOffset)))
			{
				TxRateIdx = MCS18;
			}
			else if (MCS17 && (pAGSStatisticsInfo->RSSI > (-85 + RssiOffset)))
			{
				TxRateIdx = MCS17;
			}
			else
			{
				TxRateIdx = MCS16;
			}

			pEntry->AGSCtrl.MCSGroup = 3;

			DBGPRINT_RAW(RT_DEBUG_TRACE,
					("ags> Group3 RSSI = %d, TxRateIdx = %d\n",
					pAGSStatisticsInfo->RSSI, TxRateIdx));
		}
		else if (InitTxRateIdx == AGS2x2HTRateTable[1])
		{
			/* */
			/* 2x2 peer device (Adhoc, DLS or AP) */
			/* */
			if (MCS15 && (pAGSStatisticsInfo->RSSI > (-69 + RssiOffset)))
			{
				TxRateIdx = MCS15;
			}
			else if (MCS14 && (pAGSStatisticsInfo->RSSI > (-71 + RssiOffset)))
			{
				TxRateIdx = MCS14;
			}
			else if (MCS13 && (pAGSStatisticsInfo->RSSI > (-74 + RssiOffset)))
			{
				TxRateIdx = MCS13;
			}
			else if (MCS12 && (pAGSStatisticsInfo->RSSI > (-76 + RssiOffset)))
			{
				TxRateIdx = MCS12;
			}
			else if (MCS11 && (pAGSStatisticsInfo->RSSI > (-80 + RssiOffset)))
			{
				TxRateIdx = MCS11;
			}
			else if (MCS10 && (pAGSStatisticsInfo->RSSI > (-82 + RssiOffset)))
			{
				TxRateIdx = MCS10;
			}
			else if (MCS9 && (pAGSStatisticsInfo->RSSI > (-87 + RssiOffset)))
			{
				TxRateIdx = MCS9;
			}
			else
			{
				TxRateIdx = MCS8;
			}
			
			pEntry->AGSCtrl.MCSGroup = 2;
		} 
		else 
		{
			/* */
			/* 1x1 peer device (Adhoc, DLS or AP) */
			/* */
			if (MCS7 && (pAGSStatisticsInfo->RSSI > (-71 + RssiOffset)))
			{
				TxRateIdx = MCS7;
			}
			else if (MCS6 && (pAGSStatisticsInfo->RSSI > (-73 + RssiOffset)))
			{
				TxRateIdx = MCS6;
			}
			else if (MCS5 && (pAGSStatisticsInfo->RSSI > (-76 + RssiOffset)))
			{
				TxRateIdx = MCS5;
			}
			else if (MCS4 && (pAGSStatisticsInfo->RSSI > (-78 + RssiOffset)))
			{
				TxRateIdx = MCS4;
			}
			else if (MCS3 && (pAGSStatisticsInfo->RSSI > (-82 + RssiOffset)))
			{
				TxRateIdx = MCS3;
			}
			else if (MCS2 && (pAGSStatisticsInfo->RSSI > (-84 + RssiOffset)))
			{
				TxRateIdx = MCS2;
			}
			else if (MCS1 && (pAGSStatisticsInfo->RSSI > (-89 + RssiOffset)))
			{
				TxRateIdx = MCS1;
			}
			else
			{
				TxRateIdx = MCS0;
			}
			
			pEntry->AGSCtrl.MCSGroup = 1;
		}

		pEntry->AGSCtrl.lastRateIdx = pEntry->CurrTxRateIndex;
		pEntry->CurrTxRateIndex = TxRateIdx;

		pNextTxRate = (PRTMP_TX_RATE_SWITCH)(&pTable[(pEntry->CurrTxRateIndex + 1) * SIZE_OF_AGS_RATE_TABLE_ENTRY]);
		MlmeSetTxRate(pAd, pEntry, pNextTxRate);

		RTMPZeroMemory(pEntry->TxQuality, (sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH));
		RTMPZeroMemory(pEntry->PER, (sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH));

		pEntry->fLastSecAccordingRSSI = TRUE;			
		/* reset all OneSecTx counters */
		RESET_ONE_SEC_TX_CNT(pEntry);
		return;
	}


	/* */
	/* The MCS selection is based on the RSSI and skips the rate tuning this time. */
	/* */
	if (pEntry->fLastSecAccordingRSSI == TRUE)
	{
		pEntry->fLastSecAccordingRSSI = FALSE;
		pEntry->LastSecTxRateChangeAction = 0;
		/* reset all OneSecTx counters */
		RESET_ONE_SEC_TX_CNT(pEntry);
		
		DBGPRINT_RAW(RT_DEBUG_TRACE,
			("%s: AGS: The MCS selection is based on the RSSI, and skips "
			"the rate tuning this time.\n", 
			__FUNCTION__));

		return;
	}


	DBGPRINT_RAW(RT_DEBUG_TRACE,
				("%s: AGS: TrainUp:%d, TrainDown:%d\n",
				__FUNCTION__, TrainUp, TrainDown));

	do
	{
		BOOLEAN	bTrainUpDown = FALSE;

		DBGPRINT_RAW(RT_DEBUG_TRACE,
					("%s: AGS: TxQuality[CurrRateIdx(%d)] = %d, UpPenalty:%d\n",
					__FUNCTION__, CurrRateIdx,
					pEntry->TxQuality[CurrRateIdx], pEntry->TxRateUpPenalty));

		if (pAGSStatisticsInfo->TxErrorRatio >= TrainDown) /* Poor quality */
		{
			/* error ratio too high, do rate down */
			DBGPRINT_RAW(RT_DEBUG_TRACE,
						("%s: AGS: (DOWN) TxErrorRatio >= TrainDown\n",__FUNCTION__));
			bTrainUpDown = TRUE;
			pEntry->TxQuality[CurrRateIdx] = AGS_TX_QUALITY_WORST_BOUND;
		}
		else if (pAGSStatisticsInfo->TxErrorRatio <= TrainUp) /* Good quality */
		{
			/* error ratio low, maybe rate up */
			bTrainUpDown = TRUE;
			bUpgradeQuality = TRUE;

			DBGPRINT_RAW(RT_DEBUG_TRACE,
				("%s: AGS: (UP) pEntry->TxQuality[CurrRateIdx] = %d, "
				"pEntry->TxRateUpPenalty = %d\n",
				__FUNCTION__, 
				pEntry->TxQuality[CurrRateIdx], 
				pEntry->TxRateUpPenalty));

			if (pEntry->TxQuality[CurrRateIdx])
			{
				/* Good quality in the current Tx rate */
				pEntry->TxQuality[CurrRateIdx]--;
			}

			if (pEntry->TxRateUpPenalty) /* no use for the parameter */
			{
				pEntry->TxRateUpPenalty--;
			}
			else
			{
				if (pEntry->TxQuality[pCurrTxRate->upMcs3] &&
					(pCurrTxRate->upMcs3 != CurrRateIdx))
				{
					/* hope do rate up next time for the MCS */
					pEntry->TxQuality[pCurrTxRate->upMcs3]--;
				}
				
				if (pEntry->TxQuality[pCurrTxRate->upMcs2] &&
					(pCurrTxRate->upMcs2 != CurrRateIdx))
				{
					/* hope do rate up next time for the MCS */
					pEntry->TxQuality[pCurrTxRate->upMcs2]--;
				}
				
				if (pEntry->TxQuality[pCurrTxRate->upMcs1] &&
					(pCurrTxRate->upMcs1 != CurrRateIdx))
				{
					/* hope do rate up next time for the MCS */
					pEntry->TxQuality[pCurrTxRate->upMcs1]--;
				}
			}
		}
		else if (pEntry->AGSCtrl.MCSGroup > 0) /*even if TxErrorRatio > TrainUp */
		{
			/* not bad and not good */
			if (UpRateIdx != 0)
			{
				bTrainUpDown = TRUE;
				
				if (pEntry->TxQuality[CurrRateIdx])
				{
					/* Good quality in the current Tx rate */
					pEntry->TxQuality[CurrRateIdx]--;
				}

				if (pEntry->TxQuality[UpRateIdx])
				{
					/* It may improve next train-up Tx rate's quality */
					pEntry->TxQuality[UpRateIdx]--;
				}

				DBGPRINT_RAW(RT_DEBUG_TRACE, ("ags> not bad and not good\n"));
			}
		}

		/* update error ratio for current MCS */
		pEntry->PER[CurrRateIdx] = (UCHAR)(pAGSStatisticsInfo->TxErrorRatio);

		/* */
		/* Update the current Tx rate */
		/* */
		if (bTrainUpDown)
		{
			/* need to rate up or down */
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: AGS: bTrainUpDown = %d, CurrRateIdx = %d, DownRateIdx = %d, UpRateIdx = %d, pEntry->TxQuality[CurrRateIdx] = %d, pEntry->TxQuality[UpRateIdx] = %d\n",
				__FUNCTION__, 
				bTrainUpDown, 
				CurrRateIdx, 
				DownRateIdx, 
				UpRateIdx, 
				pEntry->TxQuality[CurrRateIdx], 
				pEntry->TxQuality[UpRateIdx]));
			
			/* Downgrade Tx rate */
			if ((CurrRateIdx != DownRateIdx) && 
			     (pEntry->TxQuality[CurrRateIdx] >= AGS_TX_QUALITY_WORST_BOUND))
			{
				pEntry->CurrTxRateIndex = DownRateIdx;
				pEntry->LastSecTxRateChangeAction = 2; /* Tx rate down */
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("ags> rate down!\n"));
			}
			else if ((CurrRateIdx != UpRateIdx) && 
					(pEntry->TxQuality[UpRateIdx] <= 0)) /* Upgrade Tx rate */
			{
				pEntry->CurrTxRateIndex = UpRateIdx;
				pEntry->LastSecTxRateChangeAction = 1; /* Tx rate up */
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("ags> rate up!\n"));
			}
		}
	} while (FALSE);


	DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: AGS: pEntry->CurrTxRateIndex = %d, CurrRateIdx = %d, pEntry->LastSecTxRateChangeAction = %d\n", 
		__FUNCTION__, 
		pEntry->CurrTxRateIndex, 
		CurrRateIdx, 
		pEntry->LastSecTxRateChangeAction));

	/* rate up/down post handle */
	/* Tx rate up */
	if ((pEntry->CurrTxRateIndex != CurrRateIdx) && 
	     (pEntry->LastSecTxRateChangeAction == 1))
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: AGS: ++TX rate from %d to %d\n", 
			__FUNCTION__, 
			CurrRateIdx, 
			pEntry->CurrTxRateIndex));
		
		pEntry->TxRateUpPenalty = 0;
		pEntry->LastSecTxRateChangeAction = 1; /* Tx rate up */
		RTMPZeroMemory(pEntry->PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);
		pEntry->AGSCtrl.lastRateIdx = CurrRateIdx;

		/* */
		/* Tx rate fast train up */
		/* */
		if (!pAd->StaCfg.StaQuickResponeForRateUpTimerRunning)
		{
			RTMPSetTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, 100);

			pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = TRUE;
		}

		bTxRateChanged = TRUE;
	}
	else if ((pEntry->CurrTxRateIndex != CurrRateIdx) && 
	             (pEntry->LastSecTxRateChangeAction == 2)) /* Tx rate down */
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: AGS: --TX rate from %d to %d\n", 
			__FUNCTION__, 
			CurrRateIdx, 
			pEntry->CurrTxRateIndex));
		
		pEntry->TxRateUpPenalty = 0; /* No penalty */
		pEntry->LastSecTxRateChangeAction = 2; /* Tx rate down */
		pEntry->TxQuality[pEntry->CurrTxRateIndex] = 0;
		pEntry->PER[pEntry->CurrTxRateIndex] = 0;
		pEntry->AGSCtrl.lastRateIdx = CurrRateIdx;

		/* */
		/* Tx rate fast train down */
		/* */
		if (!pAd->StaCfg.StaQuickResponeForRateUpTimerRunning)
		{
			RTMPSetTimer(&pAd->StaCfg.StaQuickResponeForRateUpTimer, 100);
		
			pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = TRUE;
		}

		bTxRateChanged = TRUE;
	}
	else /* Tx rate remains unchanged. */
	{
		pEntry->LastSecTxRateChangeAction = 0; /* Tx rate remains unchanged. */
		bTxRateChanged = FALSE;
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("ags> no rate up/down!\n"));
	}

	pEntry->LastTxOkCount = pAGSStatisticsInfo->TxSuccess;

	/* set new tx rate */
	pNextTxRate = (PRTMP_TX_RATE_SWITCH)\
									(&pTable[(pEntry->CurrTxRateIndex + 1) *
									SIZE_OF_AGS_RATE_TABLE_ENTRY]);

	if ((bTxRateChanged == TRUE) && (pNextTxRate != NULL))
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE,
					("ags> set new rate MCS = %d!\n", pEntry->CurrTxRateIndex));
		MlmeSetTxRate(pAd, pEntry, pNextTxRate);
	}

	/* */
	/* RDG threshold control for the infrastructure mode only */
	/* */
/*	if (INFRA_ON(pAd) && (pAd->OpMode == OPMODE_STA) && (!DLS_ON(pAd)) && (!TDLS_ON(pAd))) */
	if (INFRA_ON(pAd))
	{
		if ((pAd->CommonCfg.bRdg == TRUE) &&
			CLIENT_STATUS_TEST_FLAG(&pAd->MacTab.Content[BSSID_WCID],
									fCLIENT_STATUS_RDG_CAPABLE)) /* RDG capable */
		{
			TXOP_THRESHOLD_CFG_STRUC TxopThCfg = {{0}};
			TX_LINK_CFG_STRUC TxLinkCfg = {{0}};
			
			if ((pAd->RalinkCounters.OneSecReceivedByteCount > (pAd->RalinkCounters.OneSecTransmittedByteCount * 5)) && 
			     (pNextTxRate->CurrMCS != MCS_23) && 
			     ((pAd->RalinkCounters.OneSecReceivedByteCount + pAd->RalinkCounters.OneSecTransmittedByteCount) >= (50 * 1024)))
			{
				RTMP_IO_READ32(pAd, TX_LINK_CFG, &TxLinkCfg.word);
				TxLinkCfg.field.TxRDGEn = 0;
				RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);

				RTMP_IO_READ32(pAd, TXOP_THRES_CFG, &TxopThCfg.word);
				TxopThCfg.field.RDG_IN_THRES = 0xFF; /* Similar to diable Rx RDG */
				TxopThCfg.field.RDG_OUT_THRES = 0x00;
				RTMP_IO_WRITE32(pAd, TXOP_THRES_CFG, TxopThCfg.word);

				DBGPRINT_RAW(RT_DEBUG_TRACE, ("AGS: %s: RDG_IN_THRES = 0xFF\n", __FUNCTION__));
			}
			else
			{
				RTMP_IO_READ32(pAd, TX_LINK_CFG, &TxLinkCfg.word);
				TxLinkCfg.field.TxRDGEn = 1;
				RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);

				RTMP_IO_READ32(pAd, TXOP_THRES_CFG, &TxopThCfg.word);
				TxopThCfg.field.RDG_IN_THRES = 0x00;
				TxopThCfg.field.RDG_OUT_THRES = 0x00;
				RTMP_IO_WRITE32(pAd, TXOP_THRES_CFG, TxopThCfg.word);

				DBGPRINT_RAW(RT_DEBUG_TRACE, ("AGS: %s: RDG_IN_THRES = 0x00\n", __FUNCTION__));
			}
		}
	}

	/* reset all OneSecTx counters */
	RESET_ONE_SEC_TX_CNT(pEntry);

	DBGPRINT_RAW(RT_DEBUG_TRACE, ("AGS: <--- %s\n", __FUNCTION__));
}

/* */
/* Auto Tx rate faster train up/down for AGS (Adaptive Group Switching) */
/* */
/* Parameters */
/*	pAd: The adapter data structure */
/*	pEntry: Pointer to a caller-supplied variable in which points to a MAC table entry */
/*	pTable: Pointer to a caller-supplied variable in wich points to a Tx rate switching table */
/*	TableSize: The size, in bytes, of the specified Tx rate switching table */
/*	pAGSStatisticsInfo: Pointer to a caller-supplied variable in which points to the statistics information */
/* */
/* Return Value: */
/*	None */
/* */
VOID StaQuickResponeForRateUpExecAGS(
	IN PRTMP_ADAPTER pAd, 
	IN PMAC_TABLE_ENTRY pEntry, 
	IN PUCHAR pTable, 
	IN UCHAR TableSize, 
	IN PAGS_STATISTICS_INFO pAGSStatisticsInfo,
	IN UCHAR InitTxRateIdx)
{
	UCHAR UpRateIdx = 0, DownRateIdx = 0, CurrRateIdx = 0;
	BOOLEAN bTxRateChanged = TRUE;
	PRTMP_TX_RATE_SWITCH_AGS pCurrTxRate = NULL;
	PRTMP_TX_RATE_SWITCH	 pNextTxRate = NULL;
	UCHAR TrainDown = 0, TrainUp = 0;
	CHAR ratio = 0;
	ULONG OneSecTxNoRetryOKRationCount = 0;


	DBGPRINT_RAW(RT_DEBUG_TRACE, ("QuickAGS: ---> %s\n", __FUNCTION__));

	pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = FALSE;

	DBGPRINT(RT_DEBUG_TRACE,
		("%s: QuickAGS: AccuTxTotalCnt = %lu, TxSuccess = %lu, "
		"TxRetransmit = %lu, TxFailCount = %lu, TxErrorRatio = %lu\n",
		__FUNCTION__, 
		pAGSStatisticsInfo->AccuTxTotalCnt, 
		pAGSStatisticsInfo->TxSuccess, 
		pAGSStatisticsInfo->TxRetransmit, 
		pAGSStatisticsInfo->TxFailCount, 
		pAGSStatisticsInfo->TxErrorRatio));

	CurrRateIdx = pEntry->CurrTxRateIndex;	

	if (CurrRateIdx >= TableSize)
	{
		CurrRateIdx = TableSize - 1;
	}

	UpRateIdx = DownRateIdx = pEntry->AGSCtrl.lastRateIdx;

	pCurrTxRate = (PRTMP_TX_RATE_SWITCH_AGS)(&pTable[(CurrRateIdx + 1) *
												SIZE_OF_AGS_RATE_TABLE_ENTRY]);

	if ((pAGSStatisticsInfo->RSSI > -65) && (pCurrTxRate->Mode >= MODE_HTMIX))
	{
		TrainUp = (pCurrTxRate->TrainUp + (pCurrTxRate->TrainUp >> 1));
		TrainDown	= (pCurrTxRate->TrainDown + (pCurrTxRate->TrainDown >> 1));
	}
	else
	{
		TrainUp = pCurrTxRate->TrainUp;
		TrainDown	= pCurrTxRate->TrainDown;
	}
		
	/* */
	/* MCS selection based on the RSSI information when the Tx samples are fewer than 15. */
	/* */
	if (pAGSStatisticsInfo->AccuTxTotalCnt <= 15)
	{
		RTMPZeroMemory(pEntry->TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);
		RTMPZeroMemory(pEntry->PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);

		if ((pEntry->LastSecTxRateChangeAction == 1) && (CurrRateIdx != DownRateIdx))
		{
			pEntry->CurrTxRateIndex = DownRateIdx;
			pEntry->TxQuality[CurrRateIdx] = AGS_TX_QUALITY_WORST_BOUND;
		}
		else if ((pEntry->LastSecTxRateChangeAction == 2) && (CurrRateIdx != UpRateIdx))
		{
			pEntry->CurrTxRateIndex = UpRateIdx;
		}

		DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: AccuTxTotalCnt <= 15, train back to original rate\n", 
			__FUNCTION__));
		
		return;
	}

	do
	{
		if (pEntry->LastTimeTxRateChangeAction == 0)
		{
			ratio = 5;
		}
		else
		{
			ratio = 4;
		}

		if (pAGSStatisticsInfo->TxErrorRatio >= TrainDown) /* Poor quality */
		{
			pEntry->TxQuality[CurrRateIdx] = AGS_TX_QUALITY_WORST_BOUND;
		}

		pEntry->PER[CurrRateIdx] = (UCHAR)(pAGSStatisticsInfo->TxErrorRatio);

		OneSecTxNoRetryOKRationCount = (pAGSStatisticsInfo->TxSuccess * ratio);
		
		/* Tx rate down */
		if ((pEntry->LastSecTxRateChangeAction == 1) &&
			(CurrRateIdx != DownRateIdx))
		{
			if ((pEntry->LastTxOkCount + 2) >= OneSecTxNoRetryOKRationCount) /* Poor quality */
			{
				pEntry->CurrTxRateIndex = DownRateIdx;
				pEntry->TxQuality[CurrRateIdx] = AGS_TX_QUALITY_WORST_BOUND;

				DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: (UP) bad Tx ok count (L:%lu, C:%lu)\n", 
					__FUNCTION__, 
					pEntry->LastTxOkCount, 
					OneSecTxNoRetryOKRationCount));
			}
			else /* Good quality */
			{
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: (UP) keep rate-up (L:%lu, C:%lu)\n", 
					__FUNCTION__, 
					pEntry->LastTxOkCount, 
					OneSecTxNoRetryOKRationCount));

				RTMPZeroMemory(pEntry->TxQuality, sizeof(USHORT) * MAX_STEP_OF_TX_RATE_SWITCH);

				if (pEntry->AGSCtrl.MCSGroup == 0)
				{
					if (InitTxRateIdx == AGS3x3HTRateTable[1])
					{
						/* */
						/* 3x3 peer device (Adhoc, DLS or AP) */
						/* */
						pEntry->AGSCtrl.MCSGroup = 3;
					}
					else if (InitTxRateIdx == AGS2x2HTRateTable[1])
					{
						/* */
						/* 2x2 peer device (Adhoc, DLS or AP) */
						/* */
						pEntry->AGSCtrl.MCSGroup = 2;
					}
					else
					{
						pEntry->AGSCtrl.MCSGroup = 1;
					}
				}
			}
		}
		else if ((pEntry->LastSecTxRateChangeAction == 2) &&
				(CurrRateIdx != UpRateIdx)) /* Tx rate up */
		{
			if ((pAGSStatisticsInfo->TxErrorRatio >= 50) ||
				(pAGSStatisticsInfo->TxErrorRatio >= TrainDown)) /* Poor quality */
			{
				if (InitTxRateIdx == AGS3x3HTRateTable[1])
				{
					/* */
					/* 3x3 peer device (Adhoc, DLS or AP) */
					/* */
					pEntry->AGSCtrl.MCSGroup = 3;
				}
				else if (InitTxRateIdx == AGS2x2HTRateTable[1])
				{
					/* */
					/* 2x2 peer device (Adhoc, DLS or AP) */
					/* */
					pEntry->AGSCtrl.MCSGroup = 2;
				}
				else
				{
					pEntry->AGSCtrl.MCSGroup = 1;
				}

				DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: (DOWN) direct train down (TxErrorRatio[%lu] >= TrainDown[%d])\n", 
					__FUNCTION__, 
					pAGSStatisticsInfo->TxErrorRatio, 
					TrainDown));
			}
			else if ((pEntry->LastTxOkCount + 2) >= OneSecTxNoRetryOKRationCount)
			{
				pEntry->CurrTxRateIndex = UpRateIdx;
				
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: (DOWN) bad tx ok count (L:%lu, C:%lu)\n", 
					__FUNCTION__, 
					pEntry->LastTxOkCount, 
					OneSecTxNoRetryOKRationCount));
			}
			else
			{
				if (InitTxRateIdx == AGS3x3HTRateTable[1])
				{
					/* */
					/* 3x3 peer device (Adhoc, DLS or AP) */
					/* */
					pEntry->AGSCtrl.MCSGroup = 3;
				}
				else if (InitTxRateIdx == AGS2x2HTRateTable[1])
				{
					/* */
					/* 2x2 peer device (Adhoc, DLS or AP) */
					/* */
					pEntry->AGSCtrl.MCSGroup = 2;
				}
				else
				{
					pEntry->AGSCtrl.MCSGroup = 1;
				}

				DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: (Down) keep rate-down (L:%lu, C:%lu)\n", 
					__FUNCTION__, 
					pEntry->LastTxOkCount, 
					OneSecTxNoRetryOKRationCount));
			}
		}
	}while (FALSE);

	DBGPRINT_RAW(RT_DEBUG_TRACE,
				("ags> new group = %d\n", pEntry->AGSCtrl.MCSGroup));

	/* */
	/* Last action is rate-up */
	/* */
	if (pEntry->LastSecTxRateChangeAction == 1) 
	{
		/* looking for the next group with valid MCS */
		if ((pEntry->CurrTxRateIndex != CurrRateIdx) && (pEntry->AGSCtrl.MCSGroup > 0))
		{
			pEntry->AGSCtrl.MCSGroup--; /* Try to use the MCS of the lower MCS group */
			pCurrTxRate = (PRTMP_TX_RATE_SWITCH_AGS)(&pTable[(DownRateIdx + 1) * SIZE_OF_AGS_RATE_TABLE_ENTRY]);
		}
		
		/* UpRateIdx is for temp use in this section */
		switch (pEntry->AGSCtrl.MCSGroup)
		{
			case 3: 
			{
				UpRateIdx = pCurrTxRate->upMcs3;
			}
			break;
			
			case 2: 
			{
				UpRateIdx = pCurrTxRate->upMcs2;
			}
			break;
			
			case 1: 
			{
				UpRateIdx = pCurrTxRate->upMcs1;
			}
			break;
			
			case 0: 
			{
				UpRateIdx = CurrRateIdx;
			}
			break;
			
			default: 
			{
				DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: Incorrect MCS group, pEntry->AGSCtrl.MCSGroup = %d\n", 
					__FUNCTION__, 
					pEntry->AGSCtrl.MCSGroup));
			}
			break;
		}

		if (UpRateIdx == pEntry->CurrTxRateIndex)
		{
			pEntry->AGSCtrl.MCSGroup = 0; /* Try to escape the local optima */
		}
		
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: next MCS group,  pEntry->AGSCtrl.MCSGroup = %d\n", 
			__FUNCTION__, 
			pEntry->AGSCtrl.MCSGroup));
		
	}

	if ((pEntry->CurrTxRateIndex != CurrRateIdx) && 
	     (pEntry->LastSecTxRateChangeAction == 2)) /* Tx rate up */
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: ++TX rate from %d to %d\n", 
			__FUNCTION__, 
			CurrRateIdx, 
			pEntry->CurrTxRateIndex));	
		
		pEntry->TxRateUpPenalty = 0;
		pEntry->TxQuality[pEntry->CurrTxRateIndex] = 0; /*restore the TxQuality from max to 0 */
		RTMPZeroMemory(pEntry->PER, sizeof(UCHAR) * MAX_STEP_OF_TX_RATE_SWITCH);
	}
	else if ((pEntry->CurrTxRateIndex != CurrRateIdx) && 
	            (pEntry->LastSecTxRateChangeAction == 1)) /* Tx rate down */
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: --TX rate from %d to %d\n", 
			__FUNCTION__, 
			CurrRateIdx, 
			pEntry->CurrTxRateIndex));
		
		pEntry->TxRateUpPenalty = 0; /* No penalty */
		pEntry->TxQuality[pEntry->CurrTxRateIndex] = 0;
		pEntry->PER[pEntry->CurrTxRateIndex] = 0;
	}
	else
	{
		bTxRateChanged = FALSE;
		
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("%s: QuickAGS: rate is not changed\n", 
			__FUNCTION__));
	}

	pNextTxRate = (PRTMP_TX_RATE_SWITCH)(&pTable[(pEntry->CurrTxRateIndex + 1) * SIZE_OF_AGS_RATE_TABLE_ENTRY]);
	if ((bTxRateChanged == TRUE) && (pNextTxRate != NULL))
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE,
					("ags> confirm current rate MCS = %d!\n", pEntry->CurrTxRateIndex));

		MlmeSetTxRate(pAd, pEntry, pNextTxRate);
	}

	/* reset all OneSecTx counters */
	RESET_ONE_SEC_TX_CNT(pEntry);

	DBGPRINT_RAW(RT_DEBUG_TRACE, ("QuickAGS: <--- %s\n", __FUNCTION__));
}



#endif	/* AGS_SUPPORT */
    
/* End of ags.c */ 
