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


#include "rt_config.h"






#ifdef RTMP_INTERNAL_TX_ALC
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)

/* The Tx power tuning entry */

TX_POWER_TUNING_ENTRY_STRUCT TxPowerTuningTableOver5390[] = 
{
/*	idxTxPowerTable		Tx power control over RF		Tx power control over MAC */
/*	(zero-based array)		{ RF R49[5:0]: Tx0 ALC},		{MAC 0x1314~0x1324} */
/*     0       */				{0x00,					-15}, 
/*     1       */ 				{0x01,					-15}, 
/*     2       */ 				{0x00,					-14}, 
/*     3       */ 				{0x01,					-14}, 
/*     4       */ 				{0x00,					-13}, 
/*     5       */				{0x01,					-13}, 
/*     6       */ 				{0x00,					-12}, 
/*     7       */ 				{0x01,					-12}, 
/*     8       */ 				{0x00,					-11}, 
/*     9       */ 				{0x01,					-11}, 
/*     10     */ 				{0x00,					-10}, 
/*     11     */ 				{0x01,					-10}, 
/*     12     */ 				{0x00,					-9}, 
/*     13     */ 				{0x01,					-9}, 
/*     14     */ 				{0x00,					-8}, 
/*     15     */ 				{0x01,					-8}, 
/*     16     */ 				{0x00,					-7}, 
/*     17     */ 				{0x01,					-7}, 
/*     18     */ 				{0x00,					-6}, 
/*     19     */ 				{0x01,					-6}, 
/*     20     */ 				{0x00,					-5}, 
/*     21     */ 				{0x01,					-5}, 
/*     22     */ 				{0x00,					-4}, 
/*     23     */ 				{0x01,					-4}, 
/*     24     */ 				{0x00,					-3}, 
/*     25     */ 				{0x01,					-3}, 
/*     26     */ 				{0x00,					-2}, 
/*     27     */ 				{0x01,					-2}, 
/*     28     */ 				{0x00,					-1}, 
/*     29     */ 				{0x01,					-1}, 
/*     30     */ 				{0x00,					0}, 
/*     31     */ 				{0x01,					0}, 
/*     32     */ 				{0x02,					0}, 
/*     33     */ 				{0x03,					0}, 
/*     34     */ 				{0x04,					0}, 
/*     35     */ 				{0x05,					0}, 
/*     36     */ 				{0x06,					0}, 
/*     37     */ 				{0x07,					0}, 
/*     38     */ 				{0x08,					0}, 
/*     39     */ 				{0x09,					0}, 
/*     40     */ 				{0x0A,					0}, 
/*     41     */ 				{0x0B,					0}, 
/*     42     */ 				{0x0C,					0}, 
/*     43     */ 				{0x0D,					0}, 
/*     44     */ 				{0x0E,					0}, 
/*     45     */ 				{0x0F,					0}, 
/*     46     */ 				{0x0F,					0}, 
/*     47     */ 				{0x10,					0}, 
/*     48     */ 				{0x11,					0}, 
/*     49     */ 				{0x12,					0}, 
/*     50     */ 				{0x13,					0}, 
/*     51     */ 				{0x14,					0}, 
/*     52     */ 				{0x15,					0}, 
/*     53     */ 				{0x16,					0}, 
/*     54     */ 				{0x17,					0}, 
/*     55     */ 				{0x18,					0}, 
/*     56     */ 				{0x19,					0}, 
/*     57     */ 				{0x1A,					0}, 
/*     58     */ 				{0x1B,					0}, 
/*     59     */ 				{0x1C,					0}, 
/*     60     */ 				{0x1D,					0}, 
/*     61     */ 				{0x1E,					0}, 
/*     62     */ 				{0x1F,					0}, 
/*     63     */                                 	{0x20,                                       0}, 
/*     64     */                                 	{0x21,                                       0}, 
/*     65     */                                 	{0x22,                                       0}, 
/*     66     */                                 	{0x23,                                       0}, 
/*     67     */                                 	{0x24,                                       0}, 
/*     68     */                                 	{0x25,                                       0}, 
/*     69     */                                 	{0x26,                                       0}, 
/*     70     */                                 	{0x27,                                       0}, 
/*     71     */                                 	{0x27-1,                                   1}, 
/*     72     */                                 	{0x27,                                       1}, 
/*     73     */                                 	{0x27-1,                                   2}, 
/*     74     */                                 	{0x27,                                       2}, 
/*     75     */                                 	{0x27-1,                                   3}, 
/*     76     */                       		{0x27,                                       3}, 
/*     77     */                       		{0x27-1,                                   4}, 
/*     78     */                       		{0x27,                                       4}, 
/*     79     */                       		{0x27-1,                                   5}, 
/*     80     */                       		{0x27,                                       5}, 
/*     81     */                       		{0x27-1,                                   6}, 
/*     82     */                       		{0x27,                                       6}, 
/*     83     */                       		{0x27-1,                                   7}, 
/*     84     */                       		{0x27,                                       7}, 
/*     85     */                       		{0x27-1,                                   8}, 
/*     86     */                       		{0x27,                                       8}, 
/*     87     */                       		{0x27-1,                                   9}, 
/*     88     */                       		{0x27,                                       9}, 
/*     89     */                       		{0x27-1,                                   10}, 
/*     90     */                       		{0x27,                                       10}, 
/*     91     */                       		{0x27-1,                                   11}, 
/*     92     */                       		{0x27,                                       11}, 
/*     93     */                       		{0x27-1,                                   12}, 
/*     94     */                       		{0x27,                                       12}, 
/*     95     */                      		{0x27-1,                                   13}, 
/*     96     */                       		{0x27,                                       13}, 
/*     97     */                       		{0x27-1,                                   14}, 
/*     98     */                       		{0x27,                                       14}, 
/*     99     */                       		{0x27-1,                                   15}, 
/*     100   */                        		{0x27,                                       15}, 
};
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */


/* The Tx power tuning entry*/
TX_POWER_TUNING_ENTRY_STRUCT TxPowerTuningTableOrg[] = 
{
/*	idxTxPowerTable		Tx power control over RF		Tx power control over MAC*/
/*	(zero-based array)		{ RF R12[4:0]: Tx0 ALC},		{MAC 0x1314~0x1324}*/
/*	0	*/				{0x00, 							-15}, 
/*	1	*/				{0x01, 							-15}, 
/*	2	*/				{0x00, 							-14}, 
/*	3	*/				{0x01, 							-14}, 
/*	4	*/				{0x00, 							-13}, 
/*	5	*/				{0x01, 							-13}, 
/*	6	*/				{0x00, 							-12}, 
/*	7	*/				{0x01, 							-12}, 
/*	8	*/				{0x00, 							-11}, 
/*	9	*/				{0x01, 							-11}, 
/*	10	*/				{0x00, 							-10}, 
/*	11	*/				{0x01, 							-10}, 
/*	12	*/				{0x00, 							-9}, 
/*	13	*/				{0x01, 							-9}, 
/*	14	*/				{0x00, 							-8}, 
/*	15	*/				{0x01, 							-8}, 
/*	16	*/				{0x00, 							-7}, 
/*	17	*/				{0x01, 							-7}, 
/*	18	*/				{0x00, 							-6}, 
/*	19	*/				{0x01, 							-6}, 
/*	20	*/				{0x00, 							-5}, 
/*	21	*/				{0x01, 							-5}, 
/*	22	*/				{0x00, 							-4}, 
/*	23	*/				{0x01, 							-4}, 
/*	24	*/				{0x00, 							-3}, 
/*	25	*/				{0x01, 							-3}, 
/*	26	*/				{0x00,							-2}, 
/*	27	*/				{0x01, 							-2}, 
/*	28	*/				{0x00, 							-1}, 
/*	29	*/				{0x01, 							-1}, 
/*	30	*/				{0x00,							0}, 
/*	31	*/				{0x01,							0}, 
/*	32	*/				{0x02,							0}, 
/*	33	*/				{0x03,							0}, 
/*	34	*/				{0x04,							0}, 
/*	35	*/				{0x05,							0}, 
/*	36	*/				{0x06,							0}, 
/*	37	*/				{0x07,							0}, 
/*	38	*/				{0x08,							0}, 
/*	39	*/				{0x09,							0}, 
/*	40	*/				{0x0A,							0}, 
/*	41	*/				{0x0B,							0}, 
/*	42	*/				{0x0C,							0}, 
/*	43	*/				{0x0D,							0}, 
/*	44	*/				{0x0E,							0}, 
/*	45	*/				{0x0F,							0}, 
/*	46	*/				{0x0F-1,							1}, 
/*	47	*/				{0x0F,							1}, 
/*	48	*/				{0x0F-1,							2}, 
/*	49	*/				{0x0F,							2}, 
/*	50	*/				{0x0F-1,							3}, 
/*	51	*/				{0x0F,							3}, 
/*	52	*/				{0x0F-1,							4}, 
/*	53	*/				{0x0F,							4}, 
/*	54	*/				{0x0F-1,							5}, 
/*	55	*/				{0x0F,							5}, 
/*	56	*/				{0x0F-1,							6}, 
/*	57	*/				{0x0F,							6}, 
/*	58	*/				{0x0F-1,							7}, 
/*	59	*/				{0x0F,							7}, 
/*	60	*/				{0x0F-1,							8}, 
/*	61	*/				{0x0F,							8}, 
/*	62	*/				{0x0F-1,							9}, 
/*	63	*/				{0x0F,							9}, 
/*	64	*/				{0x0F-1,							10}, 
/*	65	*/				{0x0F,							10}, 
/*	66	*/				{0x0F-1,							11}, 
/*	67	*/				{0x0F,							11}, 
/*	68	*/				{0x0F-1,							12}, 
/*	69	*/				{0x0F,							12}, 
/*	70	*/				{0x0F-1,							13}, 
/*	71	*/				{0x0F,							13}, 
/*	72	*/				{0x0F-1,							14}, 
/*	73	*/				{0x0F,							14}, 
/*	74	*/				{0x0F-1,							15}, 
/*	75	*/				{0x0F,							15}, 
};

TX_POWER_TUNING_ENTRY_STRUCT *TxPowerTuningTable = TxPowerTuningTableOrg;
#endif /* RTMP_INTERNAL_TX_ALC */

#ifdef CONFIG_STA_SUPPORT
VOID AsicUpdateAutoFallBackTable(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pRateTable)
{
	UCHAR					i;
	HT_FBK_CFG0_STRUC		HtCfg0;
	HT_FBK_CFG1_STRUC		HtCfg1;
	LG_FBK_CFG0_STRUC		LgCfg0;
	LG_FBK_CFG1_STRUC		LgCfg1;
/*#ifdef DOT11N_SS3_SUPPORT*/
	PRTMP_TX_RATE_SWITCH	pCurrTxRate, pNextTxRate;

#ifdef AGS_SUPPORT
	PRTMP_TX_RATE_SWITCH_AGS	pCurrTxRate_AGS, pNextTxRate_AGS;	
	HT_FBK_3SS_CFG0_STRUC	Ht3SSCfg0;
	HT_FBK_3SS_CFG1_STRUC	Ht3SSCfg1;
	BOOLEAN					bUseAGS = FALSE;

	if (AGS_IS_USING(pAd, pRateTable))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("%s: Use AGS\n", __FUNCTION__));
		
		bUseAGS = TRUE;
	}

	Ht3SSCfg0.word = 0x1211100f;
	Ht3SSCfg1.word = 0x16151413;	
#endif /* AGS_SUPPORT */

	/* set to initial value*/
	HtCfg0.word = 0x65432100;
	HtCfg1.word = 0xedcba980;
	LgCfg0.word = 0xedcba988;
	LgCfg1.word = 0x00002100;
/*#ifdef DOT11N_SS3_SUPPORT*/

#ifdef AGS_SUPPORT
	if (bUseAGS)
	{
		pNextTxRate_AGS = (PRTMP_TX_RATE_SWITCH_AGS)pRateTable+1;
		pNextTxRate = (PRTMP_TX_RATE_SWITCH)pNextTxRate_AGS;
	}
	else
#endif /* AGS_SUPPORT */
	pNextTxRate = (PRTMP_TX_RATE_SWITCH)pRateTable+1;

	for (i = 1; i < *((PUCHAR) pRateTable); i++)
	{
#ifdef AGS_SUPPORT
		if (bUseAGS)
		{
			pCurrTxRate_AGS = (PRTMP_TX_RATE_SWITCH_AGS)pRateTable+1+i;
			pCurrTxRate = (PRTMP_TX_RATE_SWITCH)pCurrTxRate_AGS;
		}
		else
#endif /* AGS_SUPPORT */
		pCurrTxRate = (PRTMP_TX_RATE_SWITCH)pRateTable+1+i;

		switch (pCurrTxRate->Mode)
		{
			case 0:		/*CCK*/
				break;
			case 1:		/*OFDM*/
				{
					switch(pCurrTxRate->CurrMCS)
					{
						case 0:
							LgCfg0.field.OFDMMCS0FBK = (pNextTxRate->Mode == MODE_OFDM) ? (pNextTxRate->CurrMCS+8): pNextTxRate->CurrMCS;
							break;
						case 1:
							LgCfg0.field.OFDMMCS1FBK = (pNextTxRate->Mode == MODE_OFDM) ? (pNextTxRate->CurrMCS+8): pNextTxRate->CurrMCS;
							break;
						case 2:
							LgCfg0.field.OFDMMCS2FBK = (pNextTxRate->Mode == MODE_OFDM) ? (pNextTxRate->CurrMCS+8): pNextTxRate->CurrMCS;
							break;
						case 3:
							LgCfg0.field.OFDMMCS3FBK = (pNextTxRate->Mode == MODE_OFDM) ? (pNextTxRate->CurrMCS+8): pNextTxRate->CurrMCS;
							break;
						case 4:
							LgCfg0.field.OFDMMCS4FBK = (pNextTxRate->Mode == MODE_OFDM) ? (pNextTxRate->CurrMCS+8): pNextTxRate->CurrMCS;
							break;
						case 5:
							LgCfg0.field.OFDMMCS5FBK = (pNextTxRate->Mode == MODE_OFDM) ? (pNextTxRate->CurrMCS+8): pNextTxRate->CurrMCS;
							break;
						case 6:
							LgCfg0.field.OFDMMCS6FBK = (pNextTxRate->Mode == MODE_OFDM) ? (pNextTxRate->CurrMCS+8): pNextTxRate->CurrMCS;
							break;
						case 7:
							LgCfg0.field.OFDMMCS7FBK = (pNextTxRate->Mode == MODE_OFDM) ? (pNextTxRate->CurrMCS+8): pNextTxRate->CurrMCS;
							break;
					}
				}
				break;
#ifdef DOT11_N_SUPPORT
			case 2:		/*HT-MIX*/
			case 3:		/*HT-GF*/
				{
					if ((pNextTxRate->Mode >= MODE_HTMIX) && (pCurrTxRate->CurrMCS != pNextTxRate->CurrMCS))
					{
						switch(pCurrTxRate->CurrMCS)
						{
							case 0:
								HtCfg0.field.HTMCS0FBK = pNextTxRate->CurrMCS;
								break;
							case 1:
								HtCfg0.field.HTMCS1FBK = pNextTxRate->CurrMCS;
								break;
							case 2:
								HtCfg0.field.HTMCS2FBK = pNextTxRate->CurrMCS;
								break;
							case 3:
								HtCfg0.field.HTMCS3FBK = pNextTxRate->CurrMCS;
								break;
							case 4:
								HtCfg0.field.HTMCS4FBK = pNextTxRate->CurrMCS;
								break;
							case 5:
								HtCfg0.field.HTMCS5FBK = pNextTxRate->CurrMCS;
								break;
							case 6:
								HtCfg0.field.HTMCS6FBK = pNextTxRate->CurrMCS;
								break;
							case 7:
								HtCfg0.field.HTMCS7FBK = pNextTxRate->CurrMCS;
								break;
							case 8:
								HtCfg1.field.HTMCS8FBK = pNextTxRate->CurrMCS;
								break;
							case 9:
								HtCfg1.field.HTMCS9FBK = pNextTxRate->CurrMCS;
								break;
							case 10:
								HtCfg1.field.HTMCS10FBK = pNextTxRate->CurrMCS;
								break;
							case 11:
								HtCfg1.field.HTMCS11FBK = pNextTxRate->CurrMCS;
								break;
							case 12:
								HtCfg1.field.HTMCS12FBK = pNextTxRate->CurrMCS;
								break;
							case 13:
								HtCfg1.field.HTMCS13FBK = pNextTxRate->CurrMCS;
								break;
							case 14:
								HtCfg1.field.HTMCS14FBK = pNextTxRate->CurrMCS;
								break;
							case 15:
								HtCfg1.field.HTMCS15FBK = pNextTxRate->CurrMCS;
								break;
/*#ifdef DOT11N_SS3_SUPPORT*/
#ifdef AGS_SUPPORT
							case 16:
								Ht3SSCfg0.field.HTMCS16FBK = pNextTxRate->CurrMCS;
								break;
							case 17:
								Ht3SSCfg0.field.HTMCS17FBK = pNextTxRate->CurrMCS;
								break;
							case 18:
								Ht3SSCfg0.field.HTMCS18FBK = pNextTxRate->CurrMCS;
								break;
							case 19:
								Ht3SSCfg0.field.HTMCS19FBK = pNextTxRate->CurrMCS;
								break;
							case 20:
								Ht3SSCfg1.field.HTMCS20FBK = pNextTxRate->CurrMCS;
								break;
							case 21:
								Ht3SSCfg1.field.HTMCS21FBK = pNextTxRate->CurrMCS;
								break;
							case 22:
								Ht3SSCfg1.field.HTMCS22FBK = pNextTxRate->CurrMCS;
								break;
							case 23:
								Ht3SSCfg1.field.HTMCS23FBK = pNextTxRate->CurrMCS;
								break;
#endif /* AGS_SUPPORT */									
							default:
								DBGPRINT(RT_DEBUG_ERROR, ("AsicUpdateAutoFallBackTable: not support CurrMCS=%d\n", pCurrTxRate->CurrMCS));
						}
					}
				}
				break;
#endif /* DOT11_N_SUPPORT */
		}

		pNextTxRate = pCurrTxRate;
	}

#ifdef AGS_SUPPORT
	if (bUseAGS == TRUE)
	{
		Ht3SSCfg0.field.HTMCS16FBK = 0x8; // MCS 16 -> MCS 8
		HtCfg1.field.HTMCS8FBK = 0x0; // MCS 8 -> MCS 0

		LgCfg0.field.OFDMMCS2FBK = 0x3; // OFDM 12 -> CCK 11
		LgCfg0.field.OFDMMCS1FBK = 0x2; // OFDM 9 -> CCK 5.5
		LgCfg0.field.OFDMMCS0FBK = 0x2; // OFDM 6 -> CCK 5.5
	}
#endif /* AGS_SUPPORT */

	RTMP_IO_WRITE32(pAd, HT_FBK_CFG0, HtCfg0.word);
	RTMP_IO_WRITE32(pAd, HT_FBK_CFG1, HtCfg1.word);
	RTMP_IO_WRITE32(pAd, LG_FBK_CFG0, LgCfg0.word);
	RTMP_IO_WRITE32(pAd, LG_FBK_CFG1, LgCfg1.word);

#ifdef AGS_SUPPORT
	if(bUseAGS == TRUE)
	{
		RTMP_IO_WRITE32(pAd, HT_FBK_3SS_CFG0, Ht3SSCfg0.word);
		RTMP_IO_WRITE32(pAd, HT_FBK_3SS_CFG1, Ht3SSCfg1.word);
		DBGPRINT(RT_DEBUG_TRACE, ("AsicUpdateAutoFallBackTable: Ht3SSCfg0=0x%x, Ht3SSCfg1=0x%x\n", Ht3SSCfg0.word, Ht3SSCfg1.word));
	}
#endif /* AGS_SUPPORT */

/*#ifdef DOT11N_SS3_SUPPORT*/
}
#endif /* CONFIG_STA_SUPPORT */

/*
	========================================================================

	Routine Description:
		Set MAC register value according operation mode.
		OperationMode AND bNonGFExist are for MM and GF Proteciton.
		If MM or GF mask is not set, those passing argument doesn't not take effect.
		
		Operation mode meaning:
		= 0 : Pure HT, no preotection.
		= 0x01; there may be non-HT devices in both the control and extension channel, protection is optional in BSS.
		= 0x10: No Transmission in 40M is protected.
		= 0x11: Transmission in both 40M and 20M shall be protected
		if (bNonGFExist)
			we should choose not to use GF. But still set correct ASIC registers.
	========================================================================
*/
VOID 	AsicUpdateProtect(
	IN		PRTMP_ADAPTER	pAd,
	IN 		USHORT			OperationMode,
	IN 		UCHAR			SetMask,
	IN		BOOLEAN			bDisableBGProtect,
	IN		BOOLEAN			bNonGFExist)	
{
	PROT_CFG_STRUC	ProtCfg, ProtCfg4;
	UINT32 Protect[6];
	USHORT			offset;
	UCHAR			i;
	UINT32 MacReg = 0;

#ifdef RALINK_ATE
	if (ATE_ON(pAd))
		return;
#endif /* RALINK_ATE */

#ifdef DOT11_N_SUPPORT
	if (!(pAd->CommonCfg.bHTProtect) && (OperationMode != 8))
	{
		return;
	}

	if (pAd->BATable.numDoneOriginator)
	{
		/* */
		/* enable the RTS/CTS to avoid channel collision*/
		/* */
		SetMask |= ALLN_SETPROTECT;
		OperationMode = 8;
	}
#endif /* DOT11_N_SUPPORT */

	/* Config ASIC RTS threshold register*/
	RTMP_IO_READ32(pAd, TX_RTS_CFG, &MacReg);
	MacReg &= 0xFF0000FF;
	/* If the user want disable RtsThreshold and enbale Amsdu/Ralink-Aggregation, set the RtsThreshold as 4096*/
        if ((
#ifdef DOT11_N_SUPPORT
			(pAd->CommonCfg.BACapability.field.AmsduEnable) || 
#endif /* DOT11_N_SUPPORT */
			(pAd->CommonCfg.bAggregationCapable == TRUE))
            && pAd->CommonCfg.RtsThreshold == MAX_RTS_THRESHOLD)
        {
			MacReg |= (0x1000 << 8);
        }
        else
        {
			MacReg |= (pAd->CommonCfg.RtsThreshold << 8);
        }

	RTMP_IO_WRITE32(pAd, TX_RTS_CFG, MacReg);

	/* Initial common protection settings*/
	RTMPZeroMemory(Protect, sizeof(Protect));
	ProtCfg4.word = 0;
	ProtCfg.word = 0;
	ProtCfg.field.TxopAllowGF40 = 1;
	ProtCfg.field.TxopAllowGF20 = 1;
	ProtCfg.field.TxopAllowMM40 = 1;
	ProtCfg.field.TxopAllowMM20 = 1;
	ProtCfg.field.TxopAllowOfdm = 1;
	ProtCfg.field.TxopAllowCck = 1;
	ProtCfg.field.RTSThEn = 1;
	ProtCfg.field.ProtectNav = ASIC_SHORTNAV;

	/* update PHY mode and rate*/
	if (pAd->OpMode == OPMODE_AP)
	{
		/* update PHY mode and rate*/
		if (pAd->CommonCfg.Channel > 14)
			ProtCfg.field.ProtectRate = 0x4000;
		ProtCfg.field.ProtectRate |= pAd->CommonCfg.RtsRate;	
	}
	else if (pAd->OpMode == OPMODE_STA)
	{
		// Decide Protect Rate for Legacy packet
		if (pAd->CommonCfg.Channel > 14)
		{
			ProtCfg.field.ProtectRate = 0x4000; // OFDM 6Mbps
		}
		else 
		{
			ProtCfg.field.ProtectRate = 0x0000; // CCK 1Mbps
			if (pAd->CommonCfg.MinTxRate > RATE_11)
				ProtCfg.field.ProtectRate |= 0x4000; // OFDM 6Mbps
		}
	}

	/* Handle legacy(B/G) protection*/
	if (bDisableBGProtect)
	{
		/*ProtCfg.field.ProtectRate = pAd->CommonCfg.RtsRate;*/
		ProtCfg.field.ProtectCtrl = 0;
		Protect[0] = ProtCfg.word;
		Protect[1] = ProtCfg.word;
		pAd->FlgCtsEnabled = 0; /* CTS-self is not used */
	}
	else
	{
		/*ProtCfg.field.ProtectRate = pAd->CommonCfg.RtsRate;*/
		ProtCfg.field.ProtectCtrl = 0;			/* CCK do not need to be protected*/
		Protect[0] = ProtCfg.word;
		ProtCfg.field.ProtectCtrl = ASIC_CTS;	/* OFDM needs using CCK to protect*/
		Protect[1] = ProtCfg.word;
		pAd->FlgCtsEnabled = 1; /* CTS-self is used */
	}

#ifdef DOT11_N_SUPPORT
	/* Decide HT frame protection.*/
	if ((SetMask & ALLN_SETPROTECT) != 0)
	{
		switch(OperationMode)
		{
			case 0x0:
				/* NO PROTECT */
				/* 1.All STAs in the BSS are 20/40 MHz HT*/
				/* 2. in ai 20/40MHz BSS*/
				/* 3. all STAs are 20MHz in a 20MHz BSS*/
				/* Pure HT. no protection.*/

				/* MM20_PROT_CFG*/
				/*	Reserved (31:27)*/
				/* 	PROT_TXOP(25:20) -- 010111*/
				/*	PROT_NAV(19:18)  -- 01 (Short NAV protection)*/
				/*  PROT_CTRL(17:16) -- 00 (None)*/
				/* 	PROT_RATE(15:0)  -- 0x4004 (OFDM 24M)*/
				Protect[2] = 0x01744004;	

				/* MM40_PROT_CFG*/
				/*	Reserved (31:27)*/
				/* 	PROT_TXOP(25:20) -- 111111*/
				/*	PROT_NAV(19:18)  -- 01 (Short NAV protection)*/
				/*  PROT_CTRL(17:16) -- 00 (None) */
				/* 	PROT_RATE(15:0)  -- 0x4084 (duplicate OFDM 24M)*/
				Protect[3] = 0x03f44084;

				/* CF20_PROT_CFG*/
				/*	Reserved (31:27)*/
				/* 	PROT_TXOP(25:20) -- 010111*/
				/*	PROT_NAV(19:18)  -- 01 (Short NAV protection)*/
				/*  PROT_CTRL(17:16) -- 00 (None)*/
				/* 	PROT_RATE(15:0)  -- 0x4004 (OFDM 24M)*/
				Protect[4] = 0x01744004;

				/* CF40_PROT_CFG*/
				/*	Reserved (31:27)*/
				/* 	PROT_TXOP(25:20) -- 111111*/
				/*	PROT_NAV(19:18)  -- 01 (Short NAV protection)*/
				/*  PROT_CTRL(17:16) -- 00 (None)*/
				/* 	PROT_RATE(15:0)  -- 0x4084 (duplicate OFDM 24M)*/
				Protect[5] = 0x03f44084;

				if (bNonGFExist)
				{
					/* PROT_NAV(19:18)  -- 01 (Short NAV protectiion)*/
					/* PROT_CTRL(17:16) -- 01 (RTS/CTS)*/
					Protect[4] = 0x01754004;
					Protect[5] = 0x03f54084;
				}
				pAd->CommonCfg.IOTestParm.bRTSLongProtOn = FALSE;
				break;
				
 			case 1:
				/* This is "HT non-member protection mode."*/
				/* If there may be non-HT STAs my BSS*/
				ProtCfg.word = 0x01744004;	/* PROT_CTRL(17:16) : 0 (None)*/
				ProtCfg4.word = 0x03f44084; /* duplicaet legacy 24M. BW set 1.*/
				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED))
				{
					ProtCfg.word = 0x01740003;	/*ERP use Protection bit is set, use protection rate at Clause 18..*/
					ProtCfg4.word = 0x03f40003; /* Don't duplicate RTS/CTS in CCK mode. 0x03f40083; */
				}
				/*Assign Protection method for 20&40 MHz packets*/
				ProtCfg.field.ProtectCtrl = ASIC_RTS;
				ProtCfg.field.ProtectNav = ASIC_SHORTNAV;
				ProtCfg4.field.ProtectCtrl = ASIC_RTS;
				ProtCfg4.field.ProtectNav = ASIC_SHORTNAV;
				Protect[2] = ProtCfg.word;
				Protect[3] = ProtCfg4.word;
				Protect[4] = ProtCfg.word;
				Protect[5] = ProtCfg4.word;
				pAd->CommonCfg.IOTestParm.bRTSLongProtOn = TRUE;
				break;
				
			case 2:
				/* If only HT STAs are in BSS. at least one is 20MHz. Only protect 40MHz packets*/
				ProtCfg.word = 0x01744004;  /* PROT_CTRL(17:16) : 0 (None)*/
				ProtCfg4.word = 0x03f44084; /* duplicaet legacy 24M. BW set 1.*/
				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED))
				{
					ProtCfg.word = 0x01740003;	/*ERP use Protection bit is set, use protection rate at Clause 18..*/
					ProtCfg4.word = 0x03f40003; /* Don't duplicate RTS/CTS in CCK mode. 0x03f40083; */
				} 
				/*Assign Protection method for 40MHz packets*/
				ProtCfg4.field.ProtectCtrl = ASIC_RTS;
				ProtCfg4.field.ProtectNav = ASIC_SHORTNAV;
				Protect[2] = ProtCfg.word;
				Protect[3] = ProtCfg4.word;
				if (bNonGFExist)
				{
					ProtCfg.field.ProtectCtrl = ASIC_RTS;
					ProtCfg.field.ProtectNav = ASIC_SHORTNAV;
				}
				Protect[4] = ProtCfg.word;
				Protect[5] = ProtCfg4.word;

				pAd->CommonCfg.IOTestParm.bRTSLongProtOn = FALSE;
				break;
				
			case 3:
				/* HT mixed mode.	 PROTECT ALL!*/
				/* Assign Rate*/
				ProtCfg.word = 0x01744004;	/*duplicaet legacy 24M. BW set 1.*/
				ProtCfg4.word = 0x03f44084;
				/* both 20MHz and 40MHz are protected. Whether use RTS or CTS-to-self depends on the*/
				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED))
				{
					ProtCfg.word = 0x01740003;	/*ERP use Protection bit is set, use protection rate at Clause 18..*/
					ProtCfg4.word = 0x03f40003; /* Don't duplicate RTS/CTS in CCK mode. 0x03f40083*/
				}
				/*Assign Protection method for 20&40 MHz packets*/
				ProtCfg.field.ProtectCtrl = ASIC_RTS;
				ProtCfg.field.ProtectNav = ASIC_SHORTNAV;
				ProtCfg4.field.ProtectCtrl = ASIC_RTS;
				ProtCfg4.field.ProtectNav = ASIC_SHORTNAV;
				Protect[2] = ProtCfg.word;
				Protect[3] = ProtCfg4.word;
				Protect[4] = ProtCfg.word;
				Protect[5] = ProtCfg4.word;
				pAd->CommonCfg.IOTestParm.bRTSLongProtOn = TRUE;
				break;	
				
			case 8:
				/* Special on for Atheros problem n chip.*/
				ProtCfg.word = 0x01754004;	/*duplicaet legacy 24M. BW set 1.*/
				ProtCfg4.word = 0x03f54084;
				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED))
				{
					ProtCfg.word = 0x01750003;	/*ERP use Protection bit is set, use protection rate at Clause 18..*/
					ProtCfg4.word = 0x03f50003; /* Don't duplicate RTS/CTS in CCK mode. 0x03f40083*/
				}
				
				Protect[2] = ProtCfg.word; 	/*0x01754004;*/
				Protect[3] = ProtCfg4.word; /*0x03f54084;*/
				Protect[4] = ProtCfg.word; 	/*0x01754004;*/
				Protect[5] = ProtCfg4.word; /*0x03f54084;*/
				pAd->CommonCfg.IOTestParm.bRTSLongProtOn = TRUE;
				break;		
		}
	}
#endif /* DOT11_N_SUPPORT */
	
	offset = CCK_PROT_CFG;
	for (i = 0;i < 6;i++)
	{
			if ((SetMask & (1<< i)))
		{
		RTMP_IO_WRITE32(pAd, offset + i*4, Protect[i]);
	}
}
}


VOID AsicBBPAdjust(RTMP_ADAPTER *pAd)
{
	RTMP_CHIP_ASIC_BBP_ADJUST(pAd);

}

	
/*
	==========================================================================
	Description:

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID AsicSwitchChannel(
					  IN PRTMP_ADAPTER pAd, 
	IN	UCHAR			Channel,
	IN	BOOLEAN			bScan) 
{
#ifdef DOT11N_SS3_SUPPORT

#endif /* DOT11N_SS3_SUPPORT */


#ifdef CONFIG_STA_SUPPORT
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	POS_COOKIE  pObj = (POS_COOKIE) pAd->OS_Cookie;
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND

		if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND))
		{
			if( (RTMP_Usb_AutoPM_Get_Interface(pObj->pUsb_Dev,pObj->intf)) == 1)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("AsicSwitchChannel: autopm_resume success\n"));
				RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_SUSPEND);
			}
			else if ((RTMP_Usb_AutoPM_Get_Interface(pObj->pUsb_Dev,pObj->intf)) == (-1))
			{
				DBGPRINT(RT_DEBUG_ERROR, ("AsicSwitchChannel autopm_resume fail ------\n"));
				RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND);
				return;
			}
			else
				DBGPRINT(RT_DEBUG_TRACE, ("AsicSwitchChannel: autopm_resume do nothing \n"));

		}
		else
		{
			DBGPRINT(RT_DEBUG_TRACE, ("AsicSwitchChannel: fRTMP_ADAPTER_CPU_SUSPEND\n"));
			return;
		}

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */
#endif /* CONFIG_STA_SUPPORT */



	RTMP_CHIP_ASIC_SWITCH_CHANNEL(pAd, Channel, bScan);

}

/*
	==========================================================================
	Description:
		This function is required for 2421 only, and should not be used during
		site survey. It's only required after NIC decided to stay at a channel
		for a longer period.
		When this function is called, it's always after AsicSwitchChannel().

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID AsicLockChannel(
	IN PRTMP_ADAPTER pAd, 
	IN UCHAR Channel) 
{
}

/*
	==========================================================================
	Description:

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */

#ifdef RTMP_INTERNAL_TX_ALC

/* Initialize the desired TSSI table*/

/* Parameters*/
/*	pAd: The adapter data structure*/

/* Return Value:*/
/*	None*/

VOID InitDesiredTSSITable(
	IN PRTMP_ADAPTER pAd)
{
	RTMP_CHIP_ASIC_TSSI_TABLE_INIT(pAd);
}
#endif /* RTMP_INTERNAL_TX_ALC */

VOID AsicGetAutoAgcOffset(
	IN PRTMP_ADAPTER pAd,
	IN PCHAR pDeltaPwr,
	IN PCHAR pTotalDeltaPwr,
	IN PCHAR pAgcCompensate,
	IN PUCHAR pBbpR49)
{
	CHAR            DeltaPwr = 0;
	BOOLEAN		bAutoTxAgc = FALSE;
	UCHAR		TssiRef, *pTssiMinusBoundary, *pTssiPlusBoundary, TxAgcStep;
/*	UCHAR		BbpR49 = 0, idx;*/
	UCHAR		idx;
	PCHAR		pTxAgcCompensate = NULL;
	BBP_R49_STRUC BbpR49;
	CHAR		TotalDeltaPower = 0; /* (non-positive number) including the transmit power controlled by the MAC and the BBP R1*/
#ifdef RTMP_INTERNAL_TX_ALC
/*	UCHAR desiredTSSI = 0, currentTSSI = 0; */
	PTX_POWER_TUNING_ENTRY_STRUCT pTxPowerTuningEntry = NULL;
	UCHAR RFValue = 0;
#endif /* RTMP_INTERNAL_TX_ALC */
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	PTX_POWER_TUNING_ENTRY_STRUCT pTxPowerTuningEntry2 = NULL;
	UCHAR BbpValue = 0;
	INT CurrentTemp = 0;
	INT LookupTableIndex = pAd->TxPowerCtrl.LookupTableIndex + TEMPERATURE_COMPENSATION_LOOKUP_TABLE_OFFSET;
	BOOLEAN bTempSuccess = FALSE;	
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */


	BbpR49.byte = 0;

#ifdef RTMP_INTERNAL_TX_ALC
	/* Locate the internal Tx ALC tuning entry*/
	if (pAd->TxPowerCtrl.bInternalTxALC == TRUE)
	{
		RTMP_CHIP_ASIC_AUTO_AGC_OFFSET_GET(
				pAd, &DeltaPwr, &TotalDeltaPower, pTxAgcCompensate, &BbpR49.byte);
	}
	/* no else; avoid pTxAgcCompensate == NULL */
#endif /* RTMP_INTERNAL_TX_ALC */

	/* TX power compensation for temperature variation based on TSSI. try every 4 second*/
	if (pAd->Mlme.OneSecPeriodicRound % 4 == 0)
	{
		if (pAd->CommonCfg.Channel <= 14)
		{
			/* bg channel */
			bAutoTxAgc         = pAd->bAutoTxAgcG;
			TssiRef            = pAd->TssiRefG;
			pTssiMinusBoundary = &pAd->TssiMinusBoundaryG[0];
			pTssiPlusBoundary  = &pAd->TssiPlusBoundaryG[0];
			TxAgcStep          = pAd->TxAgcStepG;
			pTxAgcCompensate   = &pAd->TxAgcCompensateG;
		}
		else
		{
			/* a channel */
			bAutoTxAgc         = pAd->bAutoTxAgcA;
			TssiRef            = pAd->TssiRefA;
			pTssiMinusBoundary = &pAd->TssiMinusBoundaryA[0];
			pTssiPlusBoundary  = &pAd->TssiPlusBoundaryA[0];
			TxAgcStep          = pAd->TxAgcStepA;
			pTxAgcCompensate   = &pAd->TxAgcCompensateA;
		}

		if (bAutoTxAgc)
		{
#ifdef RTMP_TEMPERATURE_COMPENSATION 
			if (IS_RT5392(pAd))
			{
				BbpR49.byte = 0;
				
				/* If temperature compensation is enabled */
				if (pAd->CommonCfg.TempComp != 0)
				{
					/*RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49); */
					/*DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] BBP_R49 = 0x%x\n", BbpR49)); */

					/* For method 1, the value is set before and /after reading temperature */
					if (pAd->CommonCfg.TempComp == 1)
					{
						/* Set [7:6] = 1 */
						RT30xxReadRFRegister(pAd, RF_R27, (PUCHAR)(&RFValue));
						RFValue = (RFValue & 0x7f);
						RFValue = (RFValue | 0x40);
						RT30xxWriteRFRegister(pAd, RF_R27, RFValue);

						/* Set BBP_R47[2] = 1 */
						RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpValue);
						BbpValue = (BbpValue | 0x04);
						RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpValue);

						RTMPusecDelay(1000);

						BbpValue = 0;
						RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpValue);

						/* if R47[2] == 0, it means reading temperature succeeds. */
						if ((BbpValue & 0x04) == 0)
						{
							bTempSuccess = TRUE;

							/* Current temperature */
							RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49.byte);
							CurrentTemp = (CHAR)BbpR49.byte;
							DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] BBP_R49 = %02x, current temp = %d\n", BbpR49.byte, CurrentTemp));
						}
						else
						{
							bTempSuccess = FALSE;

							/* set R47[2] = 0 */
							BbpValue = BbpValue & 0xfb;
							RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpValue);
						}

						/* Set [7:6] = 0 */
						RT30xxReadRFRegister(pAd, RF_R27, (PUCHAR)(&RFValue));
						RFValue = (RFValue & 0x3f);
						RT30xxWriteRFRegister(pAd, RF_R27, RFValue);
					}
					else if (pAd->CommonCfg.TempComp == 2)
					{
						bTempSuccess = TRUE;

						/* Current temperature */
						RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49.byte);
						CurrentTemp = (CHAR)BbpR49.byte;
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] BBP_R49 = %02x, current temp = %d\n", BbpR49.byte, CurrentTemp));
					}

					if (!bTempSuccess)
					{
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] Fail to read temperature.\n"));
						return;
					}
					else
					{
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] RefTempG = %d\n", pAd->TxPowerCtrl.RefTempG));

						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] index = %d\n", pAd->TxPowerCtrl.LookupTableIndex));
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] f(%d)= %d\n", pAd->TxPowerCtrl.LookupTableIndex - 1, pAd->TxPowerCtrl.LookupTable[LookupTableIndex - 1]));
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] f(%d)= %d\n", pAd->TxPowerCtrl.LookupTableIndex, pAd->TxPowerCtrl.LookupTable[LookupTableIndex]));
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] f(%d)= %d\n", pAd->TxPowerCtrl.LookupTableIndex + 1, pAd->TxPowerCtrl.LookupTable[LookupTableIndex + 1]));
						if (CurrentTemp > pAd->TxPowerCtrl.RefTempG + pAd->TxPowerCtrl.LookupTable[LookupTableIndex + 1] + ((pAd->TxPowerCtrl.LookupTable[LookupTableIndex + 1] - pAd->TxPowerCtrl.LookupTable[LookupTableIndex]) >> 2) &&
							LookupTableIndex < 32)
						{
							DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] ++\n"));
							LookupTableIndex++;
							pAd->TxPowerCtrl.LookupTableIndex++;
						}
						else if (CurrentTemp < pAd->TxPowerCtrl.RefTempG + pAd->TxPowerCtrl.LookupTable[LookupTableIndex] - ((pAd->TxPowerCtrl.LookupTable[LookupTableIndex] - pAd->TxPowerCtrl.LookupTable[LookupTableIndex - 1]) >> 2) &&
							LookupTableIndex > 0)
						{
							DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] --\n"));
							LookupTableIndex--;
							pAd->TxPowerCtrl.LookupTableIndex--;
						}
						else
						{
							DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] ==\n"));
						}


						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] idxTxPowerTable %d, idxTxPowerTable2 %d\n",
							pAd->TxPowerCtrl.idxTxPowerTable + pAd->TxPowerCtrl.LookupTableIndex,
							pAd->TxPowerCtrl.idxTxPowerTable2 + pAd->TxPowerCtrl.LookupTableIndex));

						pTxPowerTuningEntry = &TxPowerTuningTableOver5390[pAd->TxPowerCtrl.idxTxPowerTable + pAd->TxPowerCtrl.LookupTableIndex + TX_POWER_TUNING_ENTRY_OFFSET_OVER_5390];
						pTxPowerTuningEntry2 = &TxPowerTuningTableOver5390[pAd->TxPowerCtrl.idxTxPowerTable2 + pAd->TxPowerCtrl.LookupTableIndex + TX_POWER_TUNING_ENTRY_OFFSET_OVER_5390];
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] (tx0)RF_R12_Value = %x, MAC_PowerDelta = %d\n",
							pTxPowerTuningEntry->RF_R12_Value, pTxPowerTuningEntry->MAC_PowerDelta));
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] (tx1)RF_R12_Value = %x, MAC_PowerDelta = %d\n",
							pTxPowerTuningEntry2->RF_R12_Value, pTxPowerTuningEntry2->MAC_PowerDelta));

						/* Update RF_R49 [0:5] */
						RT30xxReadRFRegister(pAd, RF_R49, &RFValue);
						RFValue = ((RFValue & ~0x3F) | pTxPowerTuningEntry->RF_R12_Value);
						if ((RFValue & 0x3F) > 0x27) /* The valid range of the RF R49 (<5:0>tx0_alc<5:0>) is 0x00~0x27 */
						{
							RFValue = ((RFValue & ~0x3F) | 0x27);
						}
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] Update RF_R49[0:5] to 0x%x\n", pTxPowerTuningEntry->RF_R12_Value));
						RT30xxWriteRFRegister(pAd, RF_R49, RFValue);

						/* Update RF_R50 [0:5] */
						RT30xxReadRFRegister(pAd, RF_R50, &RFValue);
						RFValue = ((RFValue & ~0x3F) | pTxPowerTuningEntry2->RF_R12_Value);
						if ((RFValue & 0x3F) > 0x27) /* The valid range of the RF R49 (<5:0>tx0_alc<5:0>) is 0x00~0x27 */
						{
							RFValue = ((RFValue & ~0x3F) | 0x27);
						}
						DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] Update RF_R50[0:5] to 0x%x\n", pTxPowerTuningEntry2->RF_R12_Value));
						RT30xxWriteRFRegister(pAd, RF_R50, RFValue);
					}
				}
			}
			else
#endif /* RTMP_TEMPERATURE_COMPENSATION */
			{
			/* BbpR1 is unsigned char */
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49.byte);
			/* (p) TssiPlusBoundaryG[0] = 0 = (m) TssiMinusBoundaryG[0] */
			/* compensate: +4     +3   +2   +1    0   -1   -2   -3   -4 * steps */
			/* step value is defined in pAd->TxAgcStepG for tx power value */

			/* [4]+1+[4]   p4     p3   p2   p1   o1   m1   m2   m3   m4 */
			/* ex:         0x00 0x15 0x25 0x45 0x88 0xA0 0xB5 0xD0 0xF0
			   above value are examined in mass factory production */
			/*             [4]    [3]  [2]  [1]  [0]  [1]  [2]  [3]  [4] */

			/* plus (+) is 0x00 ~ 0x45, minus (-) is 0xa0 ~ 0xf0 */
			/* if value is between p1 ~ o1 or o1 ~ s1, no need to adjust tx power */
			/* if value is 0xa5, tx power will be -= TxAgcStep*(2-1) */

			if (BbpR49.byte > pTssiMinusBoundary[1])
			{
				/* Reading is larger than the reference value*/
				/* check for how large we need to decrease the Tx power*/
				for (idx = 1; idx < 5; idx++)
				{
					if (BbpR49.byte <= pTssiMinusBoundary[idx])  /* Found the range*/
						break;
				}
				/* The index is the step we should decrease, idx = 0 means there is nothing to compensate*/
/*				if (R3 > (ULONG) (TxAgcStep * (idx-1)))*/
					*pTxAgcCompensate = -(TxAgcStep * (idx-1));
/*				else*/
/*					*pTxAgcCompensate = -((UCHAR)R3);*/
				
				DeltaPwr += (*pTxAgcCompensate);
				DBGPRINT(RT_DEBUG_TRACE, ("-- Tx Power, BBP R49=%x, TssiRef=%x, TxAgcStep=%x, step = -%d\n",
					                BbpR49.byte, TssiRef, TxAgcStep, idx-1));                    
			}
			else if (BbpR49.byte < pTssiPlusBoundary[1])
			{
				/* Reading is smaller than the reference value*/
				/* check for how large we need to increase the Tx power*/
				for (idx = 1; idx < 5; idx++)
				{
					if (BbpR49.byte >= pTssiPlusBoundary[idx])   /* Found the range*/
						break;
				}
				/* The index is the step we should increase, idx = 0 means there is nothing to compensate*/
				*pTxAgcCompensate = TxAgcStep * (idx-1);
				DeltaPwr += (*pTxAgcCompensate);
				DBGPRINT(RT_DEBUG_TRACE, ("++ Tx Power, BBP R49=%x, TssiRef=%x, TxAgcStep=%x, step = +%d\n",
					                BbpR49.byte, TssiRef, TxAgcStep, idx-1));
			}
			else
			{
				*pTxAgcCompensate = 0;
				DBGPRINT(RT_DEBUG_TRACE, ("   Tx Power, BBP R49=%x, TssiRef=%x, TxAgcStep=%x, step = +%d\n",
					                BbpR49.byte, TssiRef, TxAgcStep, 0));
			}
		}
	}
	}
	else
	{
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
		if (IS_RT5392(pAd))
		{
			/* RT5392 and RT5372 only need to run suitable routine (Every 4 secs)*/
			/* In this case (1,2,3 secs), don't need to run */
		 	return;
		}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
		if (pAd->CommonCfg.Channel <= 14)
		{
			bAutoTxAgc         = pAd->bAutoTxAgcG;
			pTxAgcCompensate   = &pAd->TxAgcCompensateG;
		}
		else
		{
			bAutoTxAgc         = pAd->bAutoTxAgcA;
			pTxAgcCompensate   = &pAd->TxAgcCompensateA;
		}

		if (bAutoTxAgc)
			DeltaPwr += (*pTxAgcCompensate);
	}

	*pBbpR49 = BbpR49.byte;
	*pDeltaPwr = DeltaPwr;
	*pTotalDeltaPwr = TotalDeltaPower;
	*pAgcCompensate = *pTxAgcCompensate;
}

#ifdef SINGLE_SKU
/*
	==========================================================================
	Description:
		Gives CCK TX rate 2 more dB TX power.
		This routine works only in LINK UP in INFRASTRUCTURE mode.

		calculate desired Tx power in RF R3.Tx0~5,	should consider -
		0. if current radio is a noisy environment (pAd->DrsCounters.fNoisyEnvironment)
		1. TxPowerPercentage
		2. auto calibration based on TSSI feedback
		3. extra 2 db for CCK
		4. -10 db upon very-short distance (AvgRSSI >= -40db) to AP

	NOTE: Since this routine requires the value of (pAd->DrsCounters.fNoisyEnvironment),
		it should be called AFTER MlmeDynamicTxRatSwitching()
	==========================================================================
 */
VOID AsicAdjustSingleSkuTxPower(
	IN PRTMP_ADAPTER pAd) 
{
	INT			i, j;
	CHAR		DeltaPwr = 0;
	UCHAR		BbpR1 = 0, BbpR49 = 0, BbpR1Offset = 0;
	CHAR		TxAgcCompensate;
/*	ULONG		TxPwr[MAX_TXPOWER_ARRAY_SIZE];*/
	ULONG		TotalDeltaPwr[MAX_TXPOWER_ARRAY_SIZE];
	CHAR		Value, MinValue = 127;
	CHAR		TotalDeltaPower = 0; /* (non-positive number) including the transmit power controlled by the MAC and the BBP R1*/
	CONFIGURATION_OF_TX_POWER_CONTROL_OVER_MAC CfgOfTxPwrCtrlOverMAC = {0};

#ifdef CONFIG_STA_SUPPORT
	CHAR		Rssi = -127;
#endif /* CONFIG_STA_SUPPORT */
	

#ifdef CONFIG_STA_SUPPORT
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE) || 
		RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
{
		return;
}

	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		Rssi = RTMPMaxRssi(pAd, 
						   pAd->StaCfg.RssiSample.AvgRssi0, 
						   pAd->StaCfg.RssiSample.AvgRssi1, 
						   pAd->StaCfg.RssiSample.AvgRssi2);
#endif /* CONFIG_STA_SUPPORT */

        NdisZeroMemory(TotalDeltaPwr, sizeof(TotalDeltaPwr));
        /* Get Tx Rate Offset Table which from eeprom 0xDEh ~ 0xEFh */
        AsicGetTxPowerOffset(pAd, &CfgOfTxPwrCtrlOverMAC);
        /* Get temperature compensation Delta Power Value */
        AsicGetAutoAgcOffset(pAd, &DeltaPwr, &TotalDeltaPower, &TxAgcCompensate, &BbpR49);

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R1, &BbpR1);
	BbpR1 &= 0xFC;

	/* Handle regulatory max tx power constrain*/
	do
	{
		UCHAR    TxPwrInEEPROM = 0xFF, CountryTxPwr = 0xFF, criterion;
/*		UCHAR    AdjustMaxTxPwr[MAX_TXPOWER_ARRAY_SIZE * 8]; */
		UCHAR    AdjustMaxTxPwr[(MAX_TX_PWR_CONTROL_OVER_MAC_REGISTERS * 8)]; 

		{
		if (pAd->CommonCfg.Channel > 14) /* 5G band*/
			TxPwrInEEPROM = ((pAd->CommonCfg.DefineMaxTxPwr & 0xFF00) >> 8);
		else /* 2.4G band*/
			TxPwrInEEPROM = (pAd->CommonCfg.DefineMaxTxPwr & 0x00FF);
		}

		CountryTxPwr = GetCuntryMaxTxPwr(pAd, pAd->CommonCfg.Channel); 

		
		/* FAE uses OFDM 6M as criterion*/
		
/*		criterion = (TxPwr[0] >> 16) & 0xF;         FAE use OFDM 6M as criterion*/
		criterion = (UCHAR)((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[0].RegisterValue & 0x000F0000) >> 16);

		DBGPRINT_RAW(RT_DEBUG_INFO, ("AsicAdjustSingleSkuTxPower (criterion=%d, TxPwrInEEPROM=%d, CountryTxPwr=%d)\n", criterion, TxPwrInEEPROM, CountryTxPwr));
 
		/* Adjust max tx power according to the relationship of tx power in E2PROM*/
/*		for (i=0; i<MAX_TXPOWER_ARRAY_SIZE; i++)*/
		for (i=0; i<CfgOfTxPwrCtrlOverMAC.NumOfEntries; i++)
		{
			/* CCK will have 4dBm larger than OFDM*/
			/* Therefore, we should separate to parse the tx power field*/
			if (i == 0)
			{
				for (j=0; j<8; j++)
				{
/*					Value = (CHAR)((TxPwr[i] >> j*4) & 0x0F);*/
					Value = (CHAR)((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue >> j*4) & 0x0F);
 
					if (j < 4)
					{
						/* CCK will have 4dBm larger than OFDM*/
						AdjustMaxTxPwr[i*8+j] = TxPwrInEEPROM + (Value - criterion) + 4;
					}
					else
					{
						AdjustMaxTxPwr[i*8+j] = TxPwrInEEPROM + (Value - criterion);
					}
/*					DBGPRINT_RAW(RT_DEBUG_INFO, ("AsicAdjustSingleSkuTxPower (i/j=%d/%d, Value=%d, %d)\n", i, j, Value, AdjustMaxTxPwr[i*8+j]));*/

					DBGPRINT_RAW(RT_DEBUG_INFO, ("AsicAdjustSingleSkuTxPower (offset = 0x%04X, i/j=%d/%d, Value=%d, %d)\n", 
						i, 
						j, 
						Value, 
						AdjustMaxTxPwr[i*8+j], 
						CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].MACRegisterOffset));
				}
			}
			else
			{
				for (j=0; j<8; j++)
				{
					Value = (CHAR)((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue >> j*4) & 0x0F);
 
					AdjustMaxTxPwr[i*8+j] = TxPwrInEEPROM + (Value - criterion);
/*					DBGPRINT_RAW(RT_DEBUG_INFO, ("AsicAdjustSingleSkuTxPower (i/j=%d/%d, Value=%d, %d)\n", i, j, Value, AdjustMaxTxPwr[i*8+j]));*/

					DBGPRINT_RAW(RT_DEBUG_INFO, ("AsicAdjustSingleSkuTxPower (offset = 0x%04X, i/j=%d/%d, Value=%d, %d)\n", 
						CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].MACRegisterOffset, 
						i, 
						j, 
						Value, 
						AdjustMaxTxPwr[i*8+j]));
				}
			}
		}
 
		/* Adjust tx power according to the relationship*/
/*		for (i=0; i<MAX_TXPOWER_ARRAY_SIZE; i++)*/
		for (i=0; i<CfgOfTxPwrCtrlOverMAC.NumOfEntries; i++)
		{
/*			if (TxPwr[i] != 0xffffffff)*/
			if (CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue != 0xffffffff)
			{
				for (j=0; j<8; j++)
				{
/*					Value = (CHAR)((TxPwr[i] >> j*4) & 0x0F);*/
					Value = (CHAR)((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue >> j*4) & 0x0F);

					/* The system tx power is larger than the regulatory, the power should be restrain*/
					if (AdjustMaxTxPwr[i*8+j] > CountryTxPwr)
					{
						Value = (AdjustMaxTxPwr[i*8+j] - CountryTxPwr);
						if (Value > 0xF)
						{
						    /* If print the Error msg. It means the output power larger than Country Regulatory over 15dBm,
						      * the origianl design has overflow case.
						      */
						    DBGPRINT_RAW(RT_DEBUG_ERROR,("AsicAdjustSingleSkuTxPower: Value overflow - %d\n", Value));
						}
						TotalDeltaPwr[i] = (TotalDeltaPwr[i] & ~(0x0000000F << j*4)) | (Value << j*4);

/*						DBGPRINT_RAW(RT_DEBUG_INFO, ("AsicAdjustSingleSkuTxPower (i/j=%d/%d, Value=%d, %d)\n", i, j, Value, AdjustMaxTxPwr[i*8+j]));*/
						DBGPRINT_RAW(RT_DEBUG_INFO,("AsicAdjustSingleSkuTxPower (offset = 0x%04X, i/j=%d/%d, Value=%d, %d)\n", 
							CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].MACRegisterOffset, 
							i, 
							j, 
							Value, 
							AdjustMaxTxPwr[i*8+j]));
					}
					else
					{
/*						DBGPRINT_RAW(RT_DEBUG_INFO, ("AsicAdjustSingleSkuTxPower (i/j=%d/%d, Value=%d, %d, no change)\n", i, j, Value, AdjustMaxTxPwr[i*8+j]));*/
						DBGPRINT_RAW(RT_DEBUG_INFO,("AsicAdjustSingleSkuTxPower (offset = 0x%04X, i/j=%d/%d, Value=%d, %d, no change)\n", 
							CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].MACRegisterOffset, 
							i, 
							j, 
							Value, 
							AdjustMaxTxPwr[i*8+j]));
					}
				}
			}
		}
	} while (FALSE);

/*	TotalDeltaPower += DeltaPowerByBbpR1;  the transmit power controlled by the BBP R1*/
/*	TotalDeltaPower += DeltaPwr;  the transmit power controlled by the MAC	*/
	DeltaPwr += TotalDeltaPower;

	/* calculate delta power based on the percentage specified from UI */
	/* E2PROM setting is calibrated for maximum TX power (i.e. 100%)*/
	/* We lower TX power here according to the percentage specified from UI*/
	if (pAd->CommonCfg.TxPowerPercentage == 0xffffffff)       /* AUTO TX POWER control*/
	{
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			/* to patch high power issue with some APs, like Belkin N1.*/
			if (Rssi > -35)
			{
				DeltaPwr -= 12;
			}
			else if (Rssi > -40)
			{
				DeltaPwr -= 6;
			}
			else
		;
		}
#endif /* CONFIG_STA_SUPPORT */
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 90)  /* 91 ~ 100% & AUTO, treat as 100% in terms of mW*/
		;
	else if (pAd->CommonCfg.TxPowerPercentage > 60)  /* 61 ~ 90%, treat as 75% in terms of mW		 DeltaPwr -= 1;*/
	{
		DeltaPwr -= 1;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 30)  /* 31 ~ 60%, treat as 50% in terms of mW		 DeltaPwr -= 3;*/
	{
		DeltaPwr -= 3;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 15)  /* 16 ~ 30%, treat as 25% in terms of mW		 DeltaPwr -= 6;*/
	{
		DeltaPwr -= 6;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 9)   /* 10 ~ 15%, treat as 12.5% in terms of mW		 DeltaPwr -= 9;*/
	{
		DeltaPwr -= 9;
	}
	else                                           /* 0 ~ 9 %, treat as MIN(~3%) in terms of mW		 DeltaPwr -= 12;*/
	{
		DeltaPwr -= 12;
	}

	/* reset different new tx power for different TX rate */

	/* Calcuate the minimum transmit power */
/*	for(i=0; i<MAX_TXPOWER_ARRAY_SIZE; i++)*/
	for(i=0; i<CfgOfTxPwrCtrlOverMAC.NumOfEntries; i++)
	{
/*		if (TxPwr[i] != 0xffffffff)*/
		if (CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue != 0xffffffff)
		{
			for (j=0; j<8; j++)
			{
				/* After Single SKU, each data rate offset power value is saved in TotalDeltaPwr[].
				   PwrChange will add DeltaPwr and TotalDeltaPwr[] for each data rate to calculate
				   the final adjust output power value which is saved in MAC Reg. and BBP_R1 */
				CHAR PwrChange;

				/* Value / TxPwr[] is get from eeprom 0xDEh ~ 0xEFh and increase or decrease the  
				   20/40 Bandwidth Delta Value in eeprom 0x50h. */
				Value = (CHAR)((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue >> j*4) & 0x0F); /* 0 ~ 15 */

				/* Fix the corner case of Single SKU read eeprom offset 0xF0h ~ 0xFEh which for BBP Instruction configuration */
				if (Value == 0xF)
					continue;

				/* Value_offset is current Pwr comapre with Country Regulation and need adjust delta value */
				PwrChange = (CHAR)((TotalDeltaPwr[i] >> j*4) & 0x0F); /* 0 ~ 15 */
				PwrChange -= DeltaPwr;

				Value -= PwrChange;
				
				if(MinValue > Value)
					MinValue = Value;				
			}
		}
	}
	
	/* Depend on the minimum transmit power to adjust static Tx power control 
	   Prevent the value of MAC_TX_PWR_CFG less than 0. */
		
	if((MinValue < 0)&&(MinValue >= -6))
	{
		BbpR1 |= 0x01;
		BbpR1Offset = 6;
	}
	else if ((MinValue < -6)&&(MinValue >= -12))
	{
		BbpR1 |= 0x02;
		BbpR1Offset = 12;
	}
	else if (MinValue < -12)
	{
		DBGPRINT(RT_DEBUG_WARN, ("AsicAdjustTxPower: ASIC limit..\n"));
		BbpR1 |= 0x02;
		BbpR1Offset = 12;
	}

	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BbpR1);


/*	for(i=0; i<MAX_TXPOWER_ARRAY_SIZE; i++)*/
	for (i=0; i < CfgOfTxPwrCtrlOverMAC.NumOfEntries; i++)
	{
/*		if (TxPwr[i] != 0xffffffff)*/
		if (CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue != 0xffffffff)
		{
			for (j=0; j<8; j++)
			{
				/* After Single SKU, each data rate offset power value is saved in TotalDeltaPwr[].
				   PwrChange will add DeltaPwr and TotalDeltaPwr[] for each data rate to calculate
				   the final adjust output power value which is saved in MAC Reg. and BBP_R1 */
				CHAR PwrChange;

				/* Value / TxPwr[] is get from eeprom 0xDEh ~ 0xEFh and increase or decrease the  
				   20/40 Bandwidth Delta Value in eeprom 0x50h. */
				Value = (CHAR)((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue >> j*4) & 0x0F); /* 0 ~ 15 */

				/* Value_offset is current Pwr comapre with Country Regulation and need adjust delta value */
				PwrChange = (CHAR)((TotalDeltaPwr[i] >> j*4) & 0x0F); /* 0 ~ 15 */
				PwrChange -= DeltaPwr;

				Value -= (PwrChange - BbpR1Offset);

				if (Value < 0)
					Value = 0; /* min */

				if (Value > 0xF)
					Value = 0xF; /* max */

				/* fill new value to CSR offset */
/*				TxPwr[i] = (TxPwr[i] & ~(0x0000000F << j*4)) | (Value << j*4);*/
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue = (CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue & ~(0x0000000F << j*4)) | (Value << j*4);
			}

			/* write tx power value to CSR */
			/* TX_PWR_CFG_0 (8 tx rate) for	TX power for OFDM 12M/18M
											TX power for OFDM 6M/9M
											TX power for CCK5.5M/11M
											TX power for CCK1M/2M */
			/* TX_PWR_CFG_1 ~ TX_PWR_CFG_4 */
				RTMP_IO_WRITE32(pAd, CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].MACRegisterOffset, CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue);

		}
	}



}
#endif /* SINGLE_SKU */

/*
	==========================================================================
	Description:
		Gives CCK TX rate 2 more dB TX power.
		This routine works only in LINK UP in INFRASTRUCTURE mode.

		calculate desired Tx power in RF R3.Tx0~5,	should consider -
		0. if current radio is a noisy environment (pAd->DrsCounters.fNoisyEnvironment)
		1. TxPowerPercentage
		2. auto calibration based on TSSI feedback
		3. extra 2 db for CCK
		4. -10 db upon very-short distance (AvgRSSI >= -40db) to AP

	NOTE: Since this routine requires the value of (pAd->DrsCounters.fNoisyEnvironment),
		it should be called AFTER MlmeDynamicTxRatSwitching()
	==========================================================================
 */
#define MDSM_NORMAL_TX_POWER							0x00
#define MDSM_DROP_TX_POWER_BY_6dBm					0x01
#define MDSM_DROP_TX_POWER_BY_12dBm					0x02
#define MDSM_ADD_TX_POWER_BY_6dBm						0x03
#define MDSM_BBP_R1_STATIC_TX_POWER_CONTROL_MASK	0x03
VOID AsicAdjustTxPower(
	IN PRTMP_ADAPTER pAd) 
{
	INT			i, j;
	CHAR		DeltaPwr = 0;
	UCHAR		BbpR1 = 0, BbpR49 = 0;
	CHAR		TxAgcCompensate;
/*	ULONG		TxPwr[MAX_TXPOWER_ARRAY_SIZE];*/
	CHAR		Value;
	CHAR		DeltaPowerByBbpR1 = 0; /* non-positive number*/
	CHAR		TotalDeltaPower = 0; /* (non-positive number) including the transmit power controlled by the MAC and the BBP R1	*/
	CONFIGURATION_OF_TX_POWER_CONTROL_OVER_MAC CfgOfTxPwrCtrlOverMAC = {0};

#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	ULONG		TxPwrCfg7Over5390 = 0, TxPwrCfg9Over5390 = 0;
	CHAR 		desiredTssi = 0, currentTssi = 0;

	ULONG TxPwrCfg8Over5392 = 0;
	BOOLEAN		bAutoTxAgc = FALSE;

	PTX_PWR_CFG_STRUC pFinalTxPwr = NULL;


#ifdef RTMP_INTERNAL_TX_ALC		
	PTX_POWER_TUNING_ENTRY_STRUCT pTxPowerTuningEntry = NULL;
#endif /* RTMP_INTERNAL_TX_ALC */
	UCHAR RFValue = 0;
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */

#ifdef CONFIG_STA_SUPPORT
	CHAR		Rssi = -127;
#endif /* CONFIG_STA_SUPPORT */
	
#ifdef CONFIG_STA_SUPPORT

	if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF))
		return;



	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE) || 
		RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
	{
		return;
	}

	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		Rssi = RTMPMaxRssi(pAd, 
						   pAd->StaCfg.RssiSample.AvgRssi0, 
						   pAd->StaCfg.RssiSample.AvgRssi1, 
						   pAd->StaCfg.RssiSample.AvgRssi2);
#endif /* CONFIG_STA_SUPPORT */

#ifdef SINGLE_SKU
	if (pAd->CommonCfg.bSKUMode == TRUE)
	{
		AsicAdjustSingleSkuTxPower(pAd);
		return;
	}
#endif /* SINGLE_SKU */

	/* Get Tx Rate Offset Table which from eeprom 0xDEh ~ 0xEFh */
/*	AsicGetTxPowerOffset(pAd, (PULONG) &TxPwr);*/
	AsicGetTxPowerOffset(pAd, (PULONG) &CfgOfTxPwrCtrlOverMAC);
	/* Get temperature compensation Delta Power Value */
	AsicGetAutoAgcOffset(pAd, &DeltaPwr, &TotalDeltaPower, &TxAgcCompensate, &BbpR49);

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R1, &BbpR1);
	BbpR1 &= 0xFC;

	/* calculate delta power based on the percentage specified from UI */
	/* E2PROM setting is calibrated for maximum TX power (i.e. 100%)*/
	/* We lower TX power here according to the percentage specified from UI*/
	if (pAd->CommonCfg.TxPowerPercentage == 0xffffffff)       /* AUTO TX POWER control*/
	{
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			/* to patch high power issue with some APs, like Belkin N1.*/
			if (Rssi > -35)
			{
				BbpR1 |= 0x02;		/* DeltaPwr -= 12;*/
			}
			else if (Rssi > -40)
			{
				BbpR1 |= 0x01;		/* DeltaPwr -= 6;*/
			}
			else
		;
		}
#endif /* CONFIG_STA_SUPPORT */
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 90)  /* 91 ~ 100% & AUTO, treat as 100% in terms of mW*/
		;
	else if (pAd->CommonCfg.TxPowerPercentage > 60)  /* 61 ~ 90%, treat as 75% in terms of mW		 DeltaPwr -= 1;*/
	{
		DeltaPwr -= 1;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 30)  /* 31 ~ 60%, treat as 50% in terms of mW		 DeltaPwr -= 3;*/
	{
		DeltaPwr -= 3;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 15)  /* 16 ~ 30%, treat as 25% in terms of mW		 DeltaPwr -= 6;*/
	{
		DeltaPowerByBbpR1 -= 6; /* -6 dBm*/
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 9)   /* 10 ~ 15%, treat as 12.5% in terms of mW		 DeltaPwr -= 9;*/
	{
		DeltaPowerByBbpR1 -= 6; /* -6 dBm*/
		DeltaPwr -= 3;
	}
	else                                           /* 0 ~ 9 %, treat as MIN(~3%) in terms of mW		 DeltaPwr -= 12;*/
	{
		DeltaPowerByBbpR1 -= 12; /* -12 dBm*/
	}
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BbpR1);


	TotalDeltaPower += DeltaPowerByBbpR1; /* the transmit power controlled by the BBP R1*/
	TotalDeltaPower += DeltaPwr; /* the transmit power controlled by the MAC	*/

#ifdef RTMP_INTERNAL_TX_ALC	
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	
	/* Locate the internal Tx ALC tuning entry */
	
	if ((pAd->TxPowerCtrl.bInternalTxALC == TRUE) && (IS_RT5390(pAd)))
	{
		if ((pAd->Mlme.OneSecPeriodicRound % 4 == 0) && (DeltaPowerByBbpR1 == 0))
		{
			if (GetDesiredTssiAndCurrentTssi(pAd, &desiredTssi, &currentTssi) == FALSE)
			{
				DBGPRINT(RT_DEBUG_ERROR, ("%s: Incorrect desired TSSI or current TSSI\n", __FUNCTION__));
				
				/* Tx power adjustment over RF */
				

					RT30xxReadRFRegister(pAd, RF_R49, (PUCHAR)(&RFValue));
					RFValue = ((RFValue & ~0x3F) | pAd->TxPowerCtrl.RF_R12_Value);
					if ((RFValue & 0x3F) > 0x27) /* The valid range of the RF R49 (<5:0>tx0_alc<5:0>) is 0x00~0x27 */
					{
						RFValue = ((RFValue & ~0x3F) | 0x27);
					}
					RT30xxWriteRFRegister(pAd, RF_R49, (UCHAR)(RFValue));

				
				/* Tx power adjustment over MAC */
				
				TotalDeltaPower += pAd->TxPowerCtrl.MAC_PowerDelta;


			}
			else
			{
				if (desiredTssi > currentTssi)
				{
					pAd->TxPowerCtrl.idxTxPowerTable++;
				}

				if (desiredTssi < currentTssi)
				{
					pAd->TxPowerCtrl.idxTxPowerTable--;
				}

				if (pAd->TxPowerCtrl.idxTxPowerTable < LOWERBOUND_TX_POWER_TUNING_ENTRY_OVER_5390)
				{
					pAd->TxPowerCtrl.idxTxPowerTable = LOWERBOUND_TX_POWER_TUNING_ENTRY_OVER_5390;
				}

				if (pAd->TxPowerCtrl.idxTxPowerTable >= UPPERBOUND_TX_POWER_TUNING_ENTRY_OVER_5390)
				{
					pAd->TxPowerCtrl.idxTxPowerTable = UPPERBOUND_TX_POWER_TUNING_ENTRY_OVER_5390;
				}

				
				/* Valide pAd->TxPowerCtrl.idxTxPowerTable: -30 ~ 70 */
				

				pTxPowerTuningEntry = &TxPowerTuningTableOver5390[pAd->TxPowerCtrl.idxTxPowerTable + TX_POWER_TUNING_ENTRY_OFFSET_OVER_5390]; // zero-based array
				pAd->TxPowerCtrl.RF_R12_Value = pTxPowerTuningEntry->RF_R12_Value;
				pAd->TxPowerCtrl.MAC_PowerDelta = pTxPowerTuningEntry->MAC_PowerDelta;

				
				/* Tx power adjustment over RF */
				
				RT30xxReadRFRegister(pAd, RF_R49, (PUCHAR)(&RFValue));
				RFValue = ((RFValue & ~0x3F) | pAd->TxPowerCtrl.RF_R12_Value);
				if ((RFValue & 0x3F) > 0x27) /* The valid range of the RF R49 (<5:0>tx0_alc<5:0>) is 0x00~0x1F */
				{
					RFValue = ((RFValue & ~0x3F) | 0x27);
				}
				RT30xxWriteRFRegister(pAd, RF_R49, (UCHAR)(RFValue));

				
				/* Tx power adjustment over MAC */
				
				TotalDeltaPower += pAd->TxPowerCtrl.MAC_PowerDelta;

				DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSI = %d, currentTSSI = %d, idxTxPowerTable = %d, {RF_R12_Value = 0x%X, MAC_PowerDelta = %d}\n", 
					__FUNCTION__, 
					desiredTssi, 
					currentTssi, 
					pAd->TxPowerCtrl.idxTxPowerTable, 
					pTxPowerTuningEntry->RF_R12_Value, 
					pTxPowerTuningEntry->MAC_PowerDelta));
			}
		}
		else
		{
			
			/* Tx power adjustment over RF */
			
			RT30xxReadRFRegister(pAd, RF_R49, (PUCHAR)(&RFValue));
			RFValue = ((RFValue & ~0x3F) | pAd->TxPowerCtrl.RF_R12_Value);
			if ((RFValue & 0x3F) > 0x27) /* The valid range of the RF R49 (<5:0>tx0_alc<5:0>) is 0x00~0x1F */
			{
				RFValue = ((RFValue & ~0x3F) | 0x27);
			}
			RT30xxWriteRFRegister(pAd, RF_R49, (UCHAR)(RFValue));

			
			/* Tx power adjustment over MAC */
			
			TotalDeltaPower += pAd->TxPowerCtrl.MAC_PowerDelta;
		}
	}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
#endif /* RTMP_INTERNAL_TX_ALC */


	/* The BBP R1 controls the transmit power for all rates*/
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R1, &BbpR1);
	BbpR1 &= ~MDSM_BBP_R1_STATIC_TX_POWER_CONTROL_MASK;

	if (TotalDeltaPower <= -12)
	{
		TotalDeltaPower += 12;
		BbpR1 |= MDSM_DROP_TX_POWER_BY_12dBm;

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BbpR1);

		DBGPRINT(RT_DEBUG_INFO, ("TPC: %s: Drop the transmit power by 12 dBm (BBP R1)\n", __FUNCTION__));
	}
	else if ((TotalDeltaPower <= -6) && (TotalDeltaPower > -12))
	{
		TotalDeltaPower += 6;
		BbpR1 |= MDSM_DROP_TX_POWER_BY_6dBm;		

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BbpR1);

		DBGPRINT(RT_DEBUG_INFO, ("TPC: %s: Drop the transmit power by 6 dBm (BBP R1)\n", __FUNCTION__));
	}
	else
	{
		/* Control the the transmit power by using the MAC only*/
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BbpR1);
	}
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	
	/* For rt5392, power will be updated each 4 sec */

	if ((IS_RT5392(pAd) && pAd->Mlme.OneSecPeriodicRound % 4 == 0) ||
		!IS_RT5392(pAd))
	{
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */	
	/* reset different new tx power for different TX rate */
/*	for(i=0; i<MAX_TXPOWER_ARRAY_SIZE; i++)*/
	for (i=0; i < CfgOfTxPwrCtrlOverMAC.NumOfEntries; i++)
	{
		if (CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue != 0xffffffff)
		{
			for (j=0; j<8; j++)
			{
				Value = (CHAR)((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue >> j*4) & 0x0F);

#ifdef RTMP_INTERNAL_TX_ALC
				/* The upper bounds of the MAC 0x1314~0x1324 are variable when the STA uses the internal Tx ALC.*/
				if (pAd->TxPowerCtrl.bInternalTxALC == TRUE)
				{
					switch (TX_PWR_CFG_0 + (i * 4))
					{
						case TX_PWR_CFG_0: 
						{
							if ((Value + TotalDeltaPower) < 0)
							{
								Value = 0;
							}
							else if ((Value + TotalDeltaPower) > 0xE)
							{
								Value = 0xE;
							}
							else
							{
								Value += TotalDeltaPower;
							}
						}
						break;

						case TX_PWR_CFG_1: 
						{
							if ((j >= 0) && (j <= 3))
							{
								if ((Value + TotalDeltaPower) < 0)
								{
									Value = 0;
								}
								else if ((Value + TotalDeltaPower) > 0xC)
								{
									Value = 0xC;
								}
								else
								{
									Value += TotalDeltaPower;
								}
							}
							else
							{
								if ((Value + TotalDeltaPower) < 0)
								{
									Value = 0;
								}
								else if ((Value + TotalDeltaPower) > 0xE)
								{
									Value = 0xE;
								}
								else
								{
									Value += TotalDeltaPower;
								}
							}
						}
						break;

						case TX_PWR_CFG_2: 
						{
							if ((j == 0) || (j == 2) || (j == 3))
							{
								if ((Value + TotalDeltaPower) < 0)
								{
									Value = 0;
								}
								else if ((Value + TotalDeltaPower) > 0xC)
								{
									Value = 0xC;
								}
								else
								{
									Value += TotalDeltaPower;
								}
							}
							else
							{
								if ((Value + TotalDeltaPower) < 0)
								{
									Value = 0;
								}
								else if ((Value + TotalDeltaPower) > 0xE)
								{
									Value = 0xE;
								}
								else
								{
									Value += TotalDeltaPower;
								}
							}
						}
						break;

						case TX_PWR_CFG_3: 
						{
							if ((j == 0) || (j == 2) || (j == 3) || 
							    ((j >= 4) && (j <= 7)))
							{
								if ((Value + TotalDeltaPower) < 0)
								{
									Value = 0;
								}
								else if ((Value + TotalDeltaPower) > 0xC)
								{
									Value = 0xC;
								}
								else
								{
									Value += TotalDeltaPower;
								}
							}
							else
							{
								if ((Value + TotalDeltaPower) < 0)
								{
									Value = 0;
								}
								else if ((Value + TotalDeltaPower) > 0xE)
								{
									Value = 0xE;
								}
								else
								{
									Value += TotalDeltaPower;
								}
							}
						}
						break;

						case TX_PWR_CFG_4: 
						{
							if ((Value + TotalDeltaPower) < 0)
							{
								Value = 0;
							}
							else if ((Value + TotalDeltaPower) > 0xC)
							{
								Value = 0xC;
							}
							else
							{
								Value += TotalDeltaPower;
							}
						}
						break;

						default: 
						{							
							/* do nothing*/
							DBGPRINT(RT_DEBUG_ERROR, ("%s: unknown register = 0x%X\n", 
								__FUNCTION__, 
								(TX_PWR_CFG_0 + (i * 4))));
						}
						break;
					}
				}
				else
#endif /* RTMP_INTERNAL_TX_ALC */
				{
					if ((Value + TotalDeltaPower) < 0)
					{
						Value = 0; /* min */
					}
					else if ((Value + TotalDeltaPower) > 0xC)
					{
						Value = 0xC; /* max */
					}
					else
					{
						Value += TotalDeltaPower; /* temperature compensation */
					}
				}
				/* fill new value to CSR offset */
				CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue = (CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue & ~(0x0000000F << j*4)) | (Value << j*4);
			}
#ifdef RTMP_TEMPERATURE_COMPENSATION
				/* If temperature compensation enabled... */
				if (IS_RT5392(pAd) && bAutoTxAgc && pAd->CommonCfg.TempComp != 0)
				{
					pFinalTxPwr = (PTX_PWR_CFG_STRUC)&(CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue);
					DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] AsicAdjustTxPower(%x), before - %x\n", 
						(UINT)CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].MACRegisterOffset, 
						(UINT)CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue));
					pFinalTxPwr->field.Byte0 += pTxPowerTuningEntry->MAC_PowerDelta;
					pFinalTxPwr->field.Byte1 += pTxPowerTuningEntry->MAC_PowerDelta;
					pFinalTxPwr->field.Byte2 += pTxPowerTuningEntry->MAC_PowerDelta;
					pFinalTxPwr->field.Byte3 += pTxPowerTuningEntry->MAC_PowerDelta;
					DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] AsicAdjustTxPower(%x), after - %x\n", 
						(UINT)CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].MACRegisterOffset, 
						(UINT)CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue));
				}
				else
				{
					DBGPRINT(RT_DEBUG_TRACE, ("AsicAdjustTxPower %x -> %x.\n", 
						(UINT)CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].MACRegisterOffset, 
						(UINT)CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue));
				}
#endif /* RTMP_TEMPERATURE_COMPENSATION */				

			/* write tx power value to CSR */
			/* TX_PWR_CFG_0 (8 tx rate) for	TX power for OFDM 12M/18M
											TX power for OFDM 6M/9M
											TX power for CCK5.5M/11M
											TX power for CCK1M/2M */
			/* TX_PWR_CFG_1 ~ TX_PWR_CFG_4 */
			{
/*				RTMP_IO_WRITE32(pAd, TX_PWR_CFG_0 + i*4, TxPwr[i]);*/
				RTMP_IO_WRITE32(pAd, CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].MACRegisterOffset, CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue);
			}
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
			
			/* 5390 has different MAC registers for OFDM 54, HT MCS 7 and STBC MCS 7. */
			
			if (IS_RT5390(pAd))
			{
				if ((TX_PWR_CFG_0 + i * 4) == TX_PWR_CFG_1) /* Get Tx power for OFDM 54 */
				{
					TxPwrCfg7Over5390 |= ((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue & 0x0000FF00) >> 8);
				}

				if ((TX_PWR_CFG_0 + i * 4) == TX_PWR_CFG_2) /* Get Tx power for HT MCS 7 */
				{
					TxPwrCfg7Over5390 |= ((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue & 0x0000FF00) << 8);
				}

				if (IS_RT5392(pAd))
				{
					if ((TX_PWR_CFG_0 + i * 4) == TX_PWR_CFG_3) /* Get Tx power for HT MCS 15 */
					{
						TxPwrCfg8Over5392 |= ((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue & 0x0000FF00) >> 8);
					}
				}

				if ((TX_PWR_CFG_0 + i * 4) == TX_PWR_CFG_4) /* Get Tx power for STBC MCS 7 */
				{
					TxPwrCfg9Over5390 |= ((CfgOfTxPwrCtrlOverMAC.TxPwrCtrlOverMAC[i].RegisterValue & 0x0000FF00) >> 8);
				}
			}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */		

		}
	}
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	
		/* 5390/5392 has different MAC registers for OFDM 54, HT MCS 7 and STBC MCS 7. */
	
#ifdef RTMP_TEMPERATURE_COMPENSATION
		if (IS_RT5392(pAd))
		{
			pFinalTxPwr = (PTX_PWR_CFG_STRUC)&TxPwrCfg7Over5390;
			if (bAutoTxAgc && pAd->CommonCfg.TempComp != 0)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] AsicAdjustTxPower(%x), before %x\n", TX_PWR_CFG_7, (UINT)TxPwrCfg7Over5390));
				pFinalTxPwr->field.Byte0 += pTxPowerTuningEntry->MAC_PowerDelta;
				/* pFinalTxPwr->field.Byte1 += pTxPowerTuningEntry->MAC_PowerDelta; */
				pFinalTxPwr->field.Byte2 += pTxPowerTuningEntry->MAC_PowerDelta;
				/* pFinalTxPwr->field.Byte3 += pTxPowerTuningEntry->MAC_PowerDelta; */
				DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] AsicAdjustTxPower(%x), after %x\n", TX_PWR_CFG_7, (UINT)TxPwrCfg7Over5390));
			}
			else
			{
				DBGPRINT(RT_DEBUG_TRACE, ("AsicAdjustTxPower %x -> %x.\n", TX_PWR_CFG_7, (UINT)TxPwrCfg7Over5390));
			}
			RTMP_IO_WRITE32(pAd, TX_PWR_CFG_7, TxPwrCfg7Over5390);

			pFinalTxPwr = (PTX_PWR_CFG_STRUC)&TxPwrCfg8Over5392;
			if (bAutoTxAgc && pAd->CommonCfg.TempComp != 0)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] AsicAdjustTxPower(%x), before %x\n", TX_PWR_CFG_8, (UINT)TxPwrCfg8Over5392));
				pFinalTxPwr->field.Byte0 += pTxPowerTuningEntry->MAC_PowerDelta;
				/* pFinalTxPwr->field.Byte1 += pTxPowerTuningEntry->MAC_PowerDelta; */
				/* pFinalTxPwr->field.Byte2 += pTxPowerTuningEntry->MAC_PowerDelta; */
				/* pFinalTxPwr->field.Byte3 += pTxPowerTuningEntry->MAC_PowerDelta; */
				DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] AsicAdjustTxPower(%x), after %x\n", TX_PWR_CFG_8, (UINT)TxPwrCfg8Over5392));
			}
			else
			{
				DBGPRINT(RT_DEBUG_TRACE, ("AsicAdjustTxPower %x -> %x.\n", TX_PWR_CFG_8, (UINT)TxPwrCfg8Over5392));
			}
			RTMP_IO_WRITE32(pAd, TX_PWR_CFG_8, TxPwrCfg8Over5392);

			pFinalTxPwr = (PTX_PWR_CFG_STRUC)&TxPwrCfg9Over5390;
			if (bAutoTxAgc && pAd->CommonCfg.TempComp != 0)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] AsicAdjustTxPower(%x), before %x\n", TX_PWR_CFG_9, (UINT)TxPwrCfg9Over5390));
				pFinalTxPwr->field.Byte0 += pTxPowerTuningEntry->MAC_PowerDelta;
				/* pFinalTxPwr->field.Byte1 += pTxPowerTuningEntry->MAC_PowerDelta; */
				/* pFinalTxPwr->field.Byte2 += pTxPowerTuningEntry->MAC_PowerDelta; */
				/* pFinalTxPwr->field.Byte3 += pTxPowerTuningEntry->MAC_PowerDelta; */
				DBGPRINT(RT_DEBUG_TRACE, ("[temp. compensation] AsicAdjustTxPower(%x), after %x\n", TX_PWR_CFG_9, (UINT)TxPwrCfg9Over5390));
		}
			else
			{
				DBGPRINT(RT_DEBUG_TRACE, ("AsicAdjustTxPower %x -> %x.\n", TX_PWR_CFG_9, (UINT)TxPwrCfg9Over5390));
			}
			RTMP_IO_WRITE32(pAd, TX_PWR_CFG_9, TxPwrCfg9Over5390);
		}
		else 
#endif /* RTMP_TEMPERATURE_COMPENSATION */
		if (IS_RT5390(pAd))
		{
			RTMP_IO_WRITE32(pAd, TX_PWR_CFG_7, TxPwrCfg7Over5390);
			RTMP_IO_WRITE32(pAd, TX_PWR_CFG_9, TxPwrCfg9Over5390);

		DBGPRINT(RT_DEBUG_INFO, ("AsicAdjustTxPower: offset = 0x%X, TxPwr = 0x%08X, offset = 0x%X, TxPwr = 0x%08X\n", 
			TX_PWR_CFG_7, 
			(UINT)TxPwrCfg7Over5390, 
			TX_PWR_CFG_9, 
			(UINT)TxPwrCfg9Over5390));
		}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)		
       }
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */


}


VOID AsicResetBBPAgent(
IN PRTMP_ADAPTER pAd)
{
	BBP_CSR_CFG_STRUC	BbpCsr;

	/* Still need to find why BBP agent keeps busy, but in fact, hardware still function ok. Now clear busy first. */
	/* IF chipOps.AsicResetBbpAgent == NULL, run "else" part */
	RTMP_CHIP_ASIC_RESET_BBP_AGENT(pAd);
	else
	{
		DBGPRINT(RT_DEBUG_INFO, ("Reset BBP Agent busy bit.!! \n"));
		RTMP_IO_READ32(pAd, H2M_BBP_AGENT, &BbpCsr.word);
		BbpCsr.field.Busy = 0;
		RTMP_IO_WRITE32(pAd, H2M_BBP_AGENT, BbpCsr.word);
		}

}
#ifdef CONFIG_STA_SUPPORT
/*
	==========================================================================
	Description:
		put PHY to sleep here, and set next wakeup timer. PHY doesn't not wakeup 
		automatically. Instead, MCU will issue a TwakeUpInterrupt to host after
		the wakeup timer timeout. Driver has to issue a separate command to wake
		PHY up.

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID AsicSleepThenAutoWakeup(
	IN PRTMP_ADAPTER pAd, 
	IN USHORT TbttNumToNextWakeUp) 
{
	RTMP_STA_SLEEP_THEN_AUTO_WAKEUP(pAd, TbttNumToNextWakeUp);
}

/*
	==========================================================================
	Description:
		AsicForceWakeup() is used whenever manual wakeup is required
		AsicForceSleep() should only be used when not in INFRA BSS. When
		in INFRA BSS, we should use AsicSleepThenAutoWakeup() instead.
	==========================================================================
 */
VOID AsicForceSleep(
	IN PRTMP_ADAPTER pAd)
{

}

/*
	==========================================================================
	Description:
		AsicForceWakeup() is used whenever Twakeup timer (set via AsicSleepThenAutoWakeup)
		expired.

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	==========================================================================
 */
VOID AsicForceWakeup(
	IN PRTMP_ADAPTER pAd,
	IN BOOLEAN    bFromTx)
{
    DBGPRINT(RT_DEBUG_INFO, ("--> AsicForceWakeup \n"));
    RTMP_STA_FORCE_WAKEUP(pAd, bFromTx);	
}
#endif /* CONFIG_STA_SUPPORT */


/*
	==========================================================================
	Description:
		Set My BSSID

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID AsicSetBssid(
	IN PRTMP_ADAPTER pAd, 
	IN PUCHAR pBssid) 
{
	ULONG		  Addr4;

	DBGPRINT(RT_DEBUG_TRACE, ("==============> AsicSetBssid %x:%x:%x:%x:%x:%x\n",
		pBssid[0],pBssid[1],pBssid[2],pBssid[3], pBssid[4],pBssid[5]));
	
	Addr4 = (ULONG)(pBssid[0])		 | 
			(ULONG)(pBssid[1] << 8)  | 
			(ULONG)(pBssid[2] << 16) |
			(ULONG)(pBssid[3] << 24);
	RTMP_IO_WRITE32(pAd, MAC_BSSID_DW0, Addr4);

	Addr4 = 0;
	/* always one BSSID in STA mode*/
	Addr4 = (ULONG)(pBssid[4]) | (ULONG)(pBssid[5] << 8);

	RTMP_IO_WRITE32(pAd, MAC_BSSID_DW1, Addr4);
}

VOID AsicSetMcastWC(
	IN PRTMP_ADAPTER pAd) 
{
	MAC_TABLE_ENTRY *pEntry = &pAd->MacTab.Content[MCAST_WCID];
	USHORT		offset;
	
	pEntry->Sst        = SST_ASSOC;
	pEntry->Aid        = MCAST_WCID;	/* Softap supports 1 BSSID and use WCID=0 as multicast Wcid index*/
	pEntry->PsMode     = PWR_ACTIVE;
	pEntry->CurrTxRate = pAd->CommonCfg.MlmeRate; 
	offset = MAC_WCID_BASE + BSS0Mcast_WCID * HW_WCID_ENTRY_SIZE;
}

/*
	==========================================================================
	Description:   

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
VOID AsicDelWcidTab(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR	Wcid) 
{
	ULONG		  Addr0 = 0x0, Addr1 = 0x0;
	ULONG 		offset;

	DBGPRINT(RT_DEBUG_TRACE, ("AsicDelWcidTab==>Wcid = 0x%x\n",Wcid));
	offset = MAC_WCID_BASE + Wcid * HW_WCID_ENTRY_SIZE;
	RTMP_IO_WRITE32(pAd, offset, Addr0);
	offset += 4;
	RTMP_IO_WRITE32(pAd, offset, Addr1);
}

#ifdef DOT11_N_SUPPORT
/*
	==========================================================================
	Description:

	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID AsicEnableRDG(
	IN PRTMP_ADAPTER pAd) 
{
	TX_LINK_CFG_STRUC	TxLinkCfg;
	UINT32				Data = 0;

	RTMP_IO_READ32(pAd, TX_LINK_CFG, &TxLinkCfg.word);
	TxLinkCfg.field.TxRDGEn = 1;
	RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);

	RTMP_IO_READ32(pAd, EDCA_AC0_CFG, &Data);
	Data  &= 0xFFFFFF00;
	Data  |= 0x80;
	RTMP_IO_WRITE32(pAd, EDCA_AC0_CFG, Data);

	/*OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_AGGREGATION_INUSED);*/
}

/*
	==========================================================================
	Description:

	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID AsicDisableRDG(
	IN PRTMP_ADAPTER pAd) 
{
	TX_LINK_CFG_STRUC	TxLinkCfg;
	UINT32				Data = 0;



	RTMP_IO_READ32(pAd, TX_LINK_CFG, &TxLinkCfg.word);
	TxLinkCfg.field.TxRDGEn = 0;
	RTMP_IO_WRITE32(pAd, TX_LINK_CFG, TxLinkCfg.word);

	RTMP_IO_READ32(pAd, EDCA_AC0_CFG, &Data);
	
	Data  &= 0xFFFFFF00;
	/*Data  |= 0x20;*/
#ifndef WIFI_TEST
	/*if ( pAd->CommonCfg.bEnableTxBurst )	*/
	/*	Data |= 0x60;  for performance issue not set the TXOP to 0*/
#endif
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_DYNAMIC_BE_TXOP_ACTIVE) 
#ifdef DOT11_N_SUPPORT
		&& (pAd->MacTab.fAnyStationMIMOPSDynamic == FALSE)
#endif /* DOT11_N_SUPPORT */
	)
	{
		/* For CWC test, change txop from 0x30 to 0x20 in TxBurst mode*/
		if (pAd->CommonCfg.bEnableTxBurst)
		Data |= 0x20;
	}
	RTMP_IO_WRITE32(pAd, EDCA_AC0_CFG, Data);

}
#endif /* DOT11_N_SUPPORT */

/*
	==========================================================================
	Description:

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID AsicDisableSync(
	IN PRTMP_ADAPTER pAd) 
{
	BCN_TIME_CFG_STRUC csr;
	
	DBGPRINT(RT_DEBUG_TRACE, ("--->Disable TSF synchronization\n"));

	/* 2003-12-20 disable TSF and TBTT while NIC in power-saving have side effect*/
	/*			  that NIC will never wakes up because TSF stops and no more */
	/*			  TBTT interrupts*/
	pAd->TbttTickCount = 0;
	RTMP_IO_READ32(pAd, BCN_TIME_CFG, &csr.word);
	csr.field.bBeaconGen = 0;
	csr.field.bTBTTEnable = 0;
	csr.field.TsfSyncMode = 0;
	csr.field.bTsfTicking = 0;
	RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, csr.word);

}

/*
	==========================================================================
	Description:

	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID AsicEnableBssSync(
	IN PRTMP_ADAPTER pAd) 
{
	BCN_TIME_CFG_STRUC csr;

	DBGPRINT(RT_DEBUG_TRACE, ("--->AsicEnableBssSync(INFRA mode)\n"));

	RTMP_IO_READ32(pAd, BCN_TIME_CFG, &csr.word);
/*	RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, 0x00000000);*/
#ifdef CONFIG_STA_SUPPORT	
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		csr.field.BeaconInterval = pAd->CommonCfg.BeaconPeriod << 4; /* ASIC register in units of 1/16 TU*/
		csr.field.bTsfTicking = 1;
		csr.field.TsfSyncMode = 1; /* sync TSF in INFRASTRUCTURE mode*/
		csr.field.bBeaconGen  = 0; /* do NOT generate BEACON*/
		csr.field.bTBTTEnable = 1;
	}
#endif /* CONFIG_STA_SUPPORT */	
	RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, csr.word);
}

/*
	==========================================================================
	Description:
	Note: 
		BEACON frame in shared memory should be built ok before this routine
		can be called. Otherwise, a garbage frame maybe transmitted out every
		Beacon period.

	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID AsicEnableIbssSync(
	IN PRTMP_ADAPTER pAd)
{
	BCN_TIME_CFG_STRUC csr9;
	PUCHAR			ptr;
	UINT i;
	ULONG beaconBaseLocation = 0;
	USHORT			beaconLen = pAd->BeaconTxWI.MPDUtotalByteCount;
#ifdef SPECIFIC_BCN_BUF_SUPPORT	
	unsigned long irqFlag = 0;
#endif /* SPECIFIC_BCN_BUF_SUPPORT */

#ifdef RT_BIG_ENDIAN
	TXWI_STRUC		localTxWI;
	
	NdisMoveMemory((PUCHAR)&localTxWI, (PUCHAR)&pAd->BeaconTxWI, TXWI_SIZE);
	RTMPWIEndianChange((PUCHAR)&localTxWI, TYPE_TXWI);
	beaconLen = localTxWI.MPDUtotalByteCount;
#endif /* RT_BIG_ENDIAN */

	DBGPRINT(RT_DEBUG_TRACE, ("--->AsicEnableIbssSync(MPDUtotalByteCount=%d, beaconLen=%d)\n", pAd->BeaconTxWI.MPDUtotalByteCount, beaconLen));


	DBGPRINT(RT_DEBUG_TRACE, ("--->AsicEnableIbssSync(ADHOC mode. MPDUtotalByteCount = %d)\n", pAd->BeaconTxWI.MPDUtotalByteCount));

	RTMP_IO_READ32(pAd, BCN_TIME_CFG, &csr9.word);
	csr9.field.bBeaconGen = 0;
	csr9.field.bTBTTEnable = 0;
	csr9.field.bTsfTicking = 0;
	RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, csr9.word);

	{
		beaconBaseLocation = HW_BEACON_BASE0(pAd);
	}

#ifdef SPECIFIC_BCN_BUF_SUPPORT
	RTMP_MAC_SHR_MSEL_LOCK(pAd, HIGHER_SHRMEM, irqFlag);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */


#ifdef RTMP_MAC_USB
	/* move BEACON TXD and frame content to on-chip memory*/
	ptr = (PUCHAR)&pAd->BeaconTxWI;
	for (i=0; i<TXWI_SIZE; i+=2)  /* 16-byte TXWI field*/
	{
		/*UINT32 longptr =  *ptr + (*(ptr+1)<<8) + (*(ptr+2)<<16) + (*(ptr+3)<<24);*/
		/*RTMP_IO_WRITE32(pAd, HW_BEACON_BASE0 + i, longptr);*/
		RTUSBMultiWrite(pAd, HW_BEACON_BASE0(pAd) + i, ptr, 2);
		ptr += 2;
	}

	/* start right after the 16-byte TXWI field*/
	ptr = pAd->BeaconBuf;
	/*for (i=0; i< pAd->BeaconTxWI.MPDUtotalByteCount; i+=2)*/
	for (i=0; i< beaconLen; i+=2)
	{
		/*UINT32 longptr =  *ptr + (*(ptr+1)<<8) + (*(ptr+2)<<16) + (*(ptr+3)<<24);*/
		/*RTMP_IO_WRITE32(pAd, HW_BEACON_BASE0 + TXWI_SIZE + i, longptr);*/
		RTUSBMultiWrite(pAd, HW_BEACON_BASE0(pAd) + TXWI_SIZE + i, ptr, 2);
		ptr +=2;
	}
#endif /* RTMP_MAC_USB */

	{
		/* do nothing*/
	}

#ifdef SPECIFIC_BCN_BUF_SUPPORT
	RTMP_MAC_SHR_MSEL_UNLOCK(pAd, LOWER_SHRMEM, irqFlag);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */

	
	/* For Wi-Fi faily generated beacons between participating stations. */
	/* Set TBTT phase adaptive adjustment step to 8us (default 16us)*/
	/* don't change settings 2006-5- by Jerry*/
	/*RTMP_IO_WRITE32(pAd, TBTT_SYNC_CFG, 0x00001010);*/
	
	/* start sending BEACON*/
	csr9.field.BeaconInterval = pAd->CommonCfg.BeaconPeriod << 4; /* ASIC register in units of 1/16 TU*/
	csr9.field.bTsfTicking = 1;
	csr9.field.TsfSyncMode = 2; /* sync TSF in IBSS mode*/
	csr9.field.bTBTTEnable = 1;
	csr9.field.bBeaconGen = 1;
	RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, csr9.word);
}

/*
	==========================================================================
	Description:

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID AsicSetEdcaParm(
	IN PRTMP_ADAPTER pAd,
	IN PEDCA_PARM	 pEdcaParm)
{
	EDCA_AC_CFG_STRUC   Ac0Cfg, Ac1Cfg, Ac2Cfg, Ac3Cfg;
	AC_TXOP_CSR0_STRUC csr0;
	AC_TXOP_CSR1_STRUC csr1;
	AIFSN_CSR_STRUC    AifsnCsr;
	CWMIN_CSR_STRUC    CwminCsr;
	CWMAX_CSR_STRUC    CwmaxCsr;
	int i;

	Ac0Cfg.word = 0;
	Ac1Cfg.word = 0;
	Ac2Cfg.word = 0;
	Ac3Cfg.word = 0;
	if ((pEdcaParm == NULL) || (pEdcaParm->bValid == FALSE))
	{
		DBGPRINT(RT_DEBUG_TRACE,("AsicSetEdcaParm\n"));
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_WMM_INUSED);
		for (i=0; i<MAX_LEN_OF_MAC_TABLE; i++)
		{
			if (IS_ENTRY_CLIENT(&pAd->MacTab.Content[i]) || IS_ENTRY_APCLI(&pAd->MacTab.Content[i]))
				CLIENT_STATUS_CLEAR_FLAG(&pAd->MacTab.Content[i], fCLIENT_STATUS_WMM_CAPABLE);
		}

		/*========================================================*/
		/*      MAC Register has a copy .*/
		/*========================================================*/
/*#ifndef WIFI_TEST*/
		if( pAd->CommonCfg.bEnableTxBurst )		
		{
			/* For CWC test, change txop from 0x30 to 0x20 in TxBurst mode*/
			Ac0Cfg.field.AcTxop = 0x20; /* Suggest by John for TxBurst in HT Mode*/
		}
		else
			Ac0Cfg.field.AcTxop = 0;	/* QID_AC_BE*/
/*#else*/
/*		Ac0Cfg.field.AcTxop = 0;	 QID_AC_BE*/
/*#endif					*/
		Ac0Cfg.field.Cwmin = CW_MIN_IN_BITS;
		Ac0Cfg.field.Cwmax = CW_MAX_IN_BITS;
		Ac0Cfg.field.Aifsn = 2;
		RTMP_IO_WRITE32(pAd, EDCA_AC0_CFG, Ac0Cfg.word);

		Ac1Cfg.field.AcTxop = 0;	/* QID_AC_BK*/
		Ac1Cfg.field.Cwmin = CW_MIN_IN_BITS;
		Ac1Cfg.field.Cwmax = CW_MAX_IN_BITS;
		Ac1Cfg.field.Aifsn = 2;
		RTMP_IO_WRITE32(pAd, EDCA_AC1_CFG, Ac1Cfg.word);

		if (pAd->CommonCfg.PhyMode == PHY_11B)
		{
			Ac2Cfg.field.AcTxop = 192;	/* AC_VI: 192*32us ~= 6ms*/
			Ac3Cfg.field.AcTxop = 96;	/* AC_VO: 96*32us  ~= 3ms*/
		}
		else
		{
			Ac2Cfg.field.AcTxop = 96;	/* AC_VI: 96*32us ~= 3ms*/
			Ac3Cfg.field.AcTxop = 48;	/* AC_VO: 48*32us ~= 1.5ms*/
		}
		Ac2Cfg.field.Cwmin = CW_MIN_IN_BITS;
		Ac2Cfg.field.Cwmax = CW_MAX_IN_BITS;
		Ac2Cfg.field.Aifsn = 2;
		RTMP_IO_WRITE32(pAd, EDCA_AC2_CFG, Ac2Cfg.word);
		Ac3Cfg.field.Cwmin = CW_MIN_IN_BITS;
		Ac3Cfg.field.Cwmax = CW_MAX_IN_BITS;
		Ac3Cfg.field.Aifsn = 2;
		RTMP_IO_WRITE32(pAd, EDCA_AC3_CFG, Ac3Cfg.word);

		/*========================================================*/
		/*      DMA Register has a copy too.*/
		/*========================================================*/
		csr0.field.Ac0Txop = 0;		/* QID_AC_BE*/
		csr0.field.Ac1Txop = 0;		/* QID_AC_BK*/
		RTMP_IO_WRITE32(pAd, WMM_TXOP0_CFG, csr0.word);
		if (pAd->CommonCfg.PhyMode == PHY_11B)
		{
			csr1.field.Ac2Txop = 192;		/* AC_VI: 192*32us ~= 6ms*/
			csr1.field.Ac3Txop = 96;		/* AC_VO: 96*32us  ~= 3ms*/
		}
		else
		{
			csr1.field.Ac2Txop = 96;		/* AC_VI: 96*32us ~= 3ms*/
			csr1.field.Ac3Txop = 48;		/* AC_VO: 48*32us ~= 1.5ms*/
		}
		RTMP_IO_WRITE32(pAd, WMM_TXOP1_CFG, csr1.word);

		CwminCsr.word = 0;
		CwminCsr.field.Cwmin0 = CW_MIN_IN_BITS;
		CwminCsr.field.Cwmin1 = CW_MIN_IN_BITS;
		CwminCsr.field.Cwmin2 = CW_MIN_IN_BITS;
		CwminCsr.field.Cwmin3 = CW_MIN_IN_BITS;
		RTMP_IO_WRITE32(pAd, WMM_CWMIN_CFG, CwminCsr.word);

		CwmaxCsr.word = 0;
		CwmaxCsr.field.Cwmax0 = CW_MAX_IN_BITS;
		CwmaxCsr.field.Cwmax1 = CW_MAX_IN_BITS;
		CwmaxCsr.field.Cwmax2 = CW_MAX_IN_BITS;
		CwmaxCsr.field.Cwmax3 = CW_MAX_IN_BITS;
		RTMP_IO_WRITE32(pAd, WMM_CWMAX_CFG, CwmaxCsr.word);

		RTMP_IO_WRITE32(pAd, WMM_AIFSN_CFG, 0x00002222);

		NdisZeroMemory(&pAd->CommonCfg.APEdcaParm, sizeof(EDCA_PARM));

	}
	else
	{
		OPSTATUS_SET_FLAG(pAd, fOP_STATUS_WMM_INUSED);
		/*========================================================*/
		/*      MAC Register has a copy.*/
		/*========================================================*/
		
		/* Modify Cwmin/Cwmax/Txop on queue[QID_AC_VI], Recommend by Jerry 2005/07/27*/
		/* To degrade our VIDO Queue's throughput for WiFi WMM S3T07 Issue.*/
		
		/*pEdcaParm->Txop[QID_AC_VI] = pEdcaParm->Txop[QID_AC_VI] * 7 / 10;  rt2860c need this		*/

		Ac0Cfg.field.AcTxop =  pEdcaParm->Txop[QID_AC_BE];
		Ac0Cfg.field.Cwmin= pEdcaParm->Cwmin[QID_AC_BE];
		Ac0Cfg.field.Cwmax = pEdcaParm->Cwmax[QID_AC_BE];
		Ac0Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_BE]; /*+1;*/

		Ac1Cfg.field.AcTxop =  pEdcaParm->Txop[QID_AC_BK];
		Ac1Cfg.field.Cwmin = pEdcaParm->Cwmin[QID_AC_BK]; /*+2; */
		Ac1Cfg.field.Cwmax = pEdcaParm->Cwmax[QID_AC_BK];
		Ac1Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_BK]; /*+1;*/


		Ac2Cfg.field.AcTxop = (pEdcaParm->Txop[QID_AC_VI] * 6) / 10;
		{
			Ac2Cfg.field.Cwmin = pEdcaParm->Cwmin[QID_AC_VI];
			Ac2Cfg.field.Cwmax = pEdcaParm->Cwmax[QID_AC_VI];
		}
		Ac2Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_VI] + 1;
#ifdef CONFIG_STA_SUPPORT
#ifdef RTMP_MAC_USB
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			if(pAd->Antenna.field.TxPath == 1)
				Ac2Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_VI] + 1;/* 5.2.27 T6 Pass Tx VI+BE, but will impack 5.2.27/28 T7. Tx VI*/
			else
				Ac2Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_VI] + 3;
		}	
#endif /* RTMP_MAC_USB */
#endif /* CONFIG_STA_SUPPORT */
		
#ifdef INF_AMAZON_SE
#endif /* INF_AMAZON_SE */

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			/* Tuning for Wi-Fi WMM S06*/
			if (pAd->CommonCfg.bWiFiTest && 
				pEdcaParm->Aifsn[QID_AC_VI] == 10)
				Ac2Cfg.field.Aifsn -= 1; 

			/* Tuning for TGn Wi-Fi 5.2.32*/
			/* STA TestBed changes in this item: conexant legacy sta ==> broadcom 11n sta*/
			if (STA_TGN_WIFI_ON(pAd) && 
				pEdcaParm->Aifsn[QID_AC_VI] == 10)
			{
				Ac0Cfg.field.Aifsn = 3;
				Ac2Cfg.field.AcTxop = 5;
			}
			
#ifdef RT30xx
			if (pAd->RfIcType == RFIC_3020 || pAd->RfIcType == RFIC_2020)
			{
				/* Tuning for WiFi WMM S3-T07: connexant legacy sta ==> broadcom 11n sta.*/
				Ac2Cfg.field.Aifsn = 5;
			}
#endif /* RT30xx */
		}
#endif /* CONFIG_STA_SUPPORT */

		Ac3Cfg.field.AcTxop = pEdcaParm->Txop[QID_AC_VO];
		Ac3Cfg.field.Cwmin = pEdcaParm->Cwmin[QID_AC_VO];
		Ac3Cfg.field.Cwmax = pEdcaParm->Cwmax[QID_AC_VO];
		Ac3Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_VO];

/*#ifdef WIFI_TEST*/
		if (pAd->CommonCfg.bWiFiTest)
		{
			if (Ac3Cfg.field.AcTxop == 102)
			{
			Ac0Cfg.field.AcTxop = pEdcaParm->Txop[QID_AC_BE] ? pEdcaParm->Txop[QID_AC_BE] : 10;
				Ac0Cfg.field.Aifsn  = pEdcaParm->Aifsn[QID_AC_BE]-1; /* AIFSN must >= 1 */
			Ac1Cfg.field.AcTxop = pEdcaParm->Txop[QID_AC_BK];
				Ac1Cfg.field.Aifsn  = pEdcaParm->Aifsn[QID_AC_BK];
			Ac2Cfg.field.AcTxop = pEdcaParm->Txop[QID_AC_VI];
			} /* End of if */
		}
/*#endif  WIFI_TEST */

		RTMP_IO_WRITE32(pAd, EDCA_AC0_CFG, Ac0Cfg.word);
		RTMP_IO_WRITE32(pAd, EDCA_AC1_CFG, Ac1Cfg.word);
		RTMP_IO_WRITE32(pAd, EDCA_AC2_CFG, Ac2Cfg.word);
		RTMP_IO_WRITE32(pAd, EDCA_AC3_CFG, Ac3Cfg.word);


		/*========================================================*/
		/*      DMA Register has a copy too.*/
		/*========================================================*/
		csr0.field.Ac0Txop = Ac0Cfg.field.AcTxop;
		csr0.field.Ac1Txop = Ac1Cfg.field.AcTxop;
		RTMP_IO_WRITE32(pAd, WMM_TXOP0_CFG, csr0.word);

		csr1.field.Ac2Txop = Ac2Cfg.field.AcTxop;
		csr1.field.Ac3Txop = Ac3Cfg.field.AcTxop;
		RTMP_IO_WRITE32(pAd, WMM_TXOP1_CFG, csr1.word);

		CwminCsr.word = 0;
		CwminCsr.field.Cwmin0 = pEdcaParm->Cwmin[QID_AC_BE];
		CwminCsr.field.Cwmin1 = pEdcaParm->Cwmin[QID_AC_BK];
		CwminCsr.field.Cwmin2 = pEdcaParm->Cwmin[QID_AC_VI];
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			CwminCsr.field.Cwmin3 = pEdcaParm->Cwmin[QID_AC_VO] - 1; /*for TGn wifi test*/
#endif /* CONFIG_STA_SUPPORT */
		RTMP_IO_WRITE32(pAd, WMM_CWMIN_CFG, CwminCsr.word);

		CwmaxCsr.word = 0;
		CwmaxCsr.field.Cwmax0 = pEdcaParm->Cwmax[QID_AC_BE];
		CwmaxCsr.field.Cwmax1 = pEdcaParm->Cwmax[QID_AC_BK];
		CwmaxCsr.field.Cwmax2 = pEdcaParm->Cwmax[QID_AC_VI];
		CwmaxCsr.field.Cwmax3 = pEdcaParm->Cwmax[QID_AC_VO];
		RTMP_IO_WRITE32(pAd, WMM_CWMAX_CFG, CwmaxCsr.word);

		AifsnCsr.word = 0;
		AifsnCsr.field.Aifsn0 = Ac0Cfg.field.Aifsn; /*pEdcaParm->Aifsn[QID_AC_BE];*/
		AifsnCsr.field.Aifsn1 = Ac1Cfg.field.Aifsn; /*pEdcaParm->Aifsn[QID_AC_BK];*/
#ifdef CONFIG_STA_SUPPORT
#ifdef RTMP_MAC_USB
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			if(pAd->Antenna.field.TxPath == 1)
				AifsnCsr.field.Aifsn1 = Ac1Cfg.field.Aifsn + 2; 	/*5.2.27 T7 Pass*/
		}
#endif /* RTMP_MAC_USB */
#endif /* CONFIG_STA_SUPPORT */
		AifsnCsr.field.Aifsn2 = Ac2Cfg.field.Aifsn; /*pEdcaParm->Aifsn[QID_AC_VI];*/
#ifdef INF_AMAZON_SE
#endif /* INF_AMAZON_SE */

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			/* Tuning for Wi-Fi WMM S06*/
			if (pAd->CommonCfg.bWiFiTest &&
				pEdcaParm->Aifsn[QID_AC_VI] == 10)
				AifsnCsr.field.Aifsn2 = Ac2Cfg.field.Aifsn - 4;

			/* Tuning for TGn Wi-Fi 5.2.32*/
			/* STA TestBed changes in this item: connexant legacy sta ==> broadcom 11n sta*/
			if (STA_TGN_WIFI_ON(pAd) && 
				pEdcaParm->Aifsn[QID_AC_VI] == 10)
			{
				AifsnCsr.field.Aifsn0 = 3;
				AifsnCsr.field.Aifsn2 = 7;
			}

			if (INFRA_ON(pAd))
				CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[BSSID_WCID], fCLIENT_STATUS_WMM_CAPABLE);
		}
#endif /* CONFIG_STA_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			AifsnCsr.field.Aifsn3 = Ac3Cfg.field.Aifsn - 1; /*pEdcaParm->Aifsn[QID_AC_VO]; for TGn wifi test*/
#ifdef RT30xx
			/* TODO: Shiang, this modification also suitable for RT3052/RT3050 ???*/
			if (pAd->RfIcType == RFIC_3020 || pAd->RfIcType == RFIC_2020
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
				|| IS_RT5390(pAd)
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
			)
			{
				IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
					AifsnCsr.field.Aifsn2 = 0x2; /*pEdcaParm->Aifsn[QID_AC_VI]; for WiFi WMM S4-T04.*/
			}
#endif /* RT30xx */
		}
#endif /* CONFIG_STA_SUPPORT */
		RTMP_IO_WRITE32(pAd, WMM_AIFSN_CFG, AifsnCsr.word);

		NdisMoveMemory(&pAd->CommonCfg.APEdcaParm, pEdcaParm, sizeof(EDCA_PARM));
		if (!ADHOC_ON(pAd))
		{
			DBGPRINT(RT_DEBUG_TRACE,("EDCA [#%d]: AIFSN CWmin CWmax  TXOP(us)  ACM\n", pEdcaParm->EdcaUpdateCount));
			DBGPRINT(RT_DEBUG_TRACE,("     AC_BE      %2d     %2d     %2d      %4d     %d\n",
									 pEdcaParm->Aifsn[0],
									 pEdcaParm->Cwmin[0],
									 pEdcaParm->Cwmax[0],
									 pEdcaParm->Txop[0]<<5,
									 pEdcaParm->bACM[0]));
			DBGPRINT(RT_DEBUG_TRACE,("     AC_BK      %2d     %2d     %2d      %4d     %d\n",
									 pEdcaParm->Aifsn[1],
									 pEdcaParm->Cwmin[1],
									 pEdcaParm->Cwmax[1],
									 pEdcaParm->Txop[1]<<5,
									 pEdcaParm->bACM[1]));
			DBGPRINT(RT_DEBUG_TRACE,("     AC_VI      %2d     %2d     %2d      %4d     %d\n",
									 pEdcaParm->Aifsn[2],
									 pEdcaParm->Cwmin[2],
									 pEdcaParm->Cwmax[2],
									 pEdcaParm->Txop[2]<<5,
									 pEdcaParm->bACM[2]));
			DBGPRINT(RT_DEBUG_TRACE,("     AC_VO      %2d     %2d     %2d      %4d     %d\n",
									 pEdcaParm->Aifsn[3],
									 pEdcaParm->Cwmin[3],
									 pEdcaParm->Cwmax[3],
									 pEdcaParm->Txop[3]<<5,
									 pEdcaParm->bACM[3]));
		}

	}
}

/*
	==========================================================================
	Description:

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL
	
	==========================================================================
 */
VOID 	AsicSetSlotTime(
	IN PRTMP_ADAPTER pAd,
	IN BOOLEAN bUseShortSlotTime) 
{
	ULONG	SlotTime;
	UINT32	RegValue = 0;

#ifdef CONFIG_STA_SUPPORT
	if (pAd->CommonCfg.Channel > 14)
		bUseShortSlotTime = TRUE;
#endif /* CONFIG_STA_SUPPORT */

	if (bUseShortSlotTime && OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED))
		return;
	else if ((!bUseShortSlotTime) && (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED)))
		return;

	if (bUseShortSlotTime)
		OPSTATUS_SET_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED);
	else
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED);

	SlotTime = (bUseShortSlotTime)? 9 : 20;

#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		/* force using short SLOT time for FAE to demo performance when TxBurst is ON*/
		if (((pAd->StaActive.SupportedPhyInfo.bHtEnable == FALSE) && (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED)))
#ifdef DOT11_N_SUPPORT
			|| ((pAd->StaActive.SupportedPhyInfo.bHtEnable == TRUE) && (pAd->CommonCfg.BACapability.field.Policy == BA_NOTUSE))
#endif /* DOT11_N_SUPPORT */
			)
		{
			/* In this case, we will think it is doing Wi-Fi test*/
			/* And we will not set to short slot when bEnableTxBurst is TRUE.*/
		}
		else if (pAd->CommonCfg.bEnableTxBurst)
		{
			OPSTATUS_SET_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED);
			SlotTime = 9;
		}
	}
#endif /* CONFIG_STA_SUPPORT */

	
	/* For some reasons, always set it to short slot time.*/
	/* */
	/* ToDo: Should consider capability with 11B*/
	
#ifdef CONFIG_STA_SUPPORT 
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if (pAd->StaCfg.BssType == BSS_ADHOC)	
		{
			OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED);
			SlotTime = 20;
		}
	}
#endif /* CONFIG_STA_SUPPORT */

	RTMP_IO_READ32(pAd, BKOFF_SLOT_CFG, &RegValue);
	RegValue = RegValue & 0xFFFFFF00;

	RegValue |= SlotTime;

	RTMP_IO_WRITE32(pAd, BKOFF_SLOT_CFG, RegValue);
}

/*
	========================================================================
	Description:
		Add Shared key information into ASIC. 
		Update shared key, TxMic and RxMic to Asic Shared key table
		Update its cipherAlg to Asic Shared key Mode.
		
    Return:
	========================================================================
*/
VOID AsicAddSharedKeyEntry(
	IN PRTMP_ADAPTER 	pAd,
	IN UCHAR		 	BssIndex,
	IN UCHAR		 	KeyIdx,
	IN PCIPHER_KEY		pCipherKey)
{
	ULONG offset; /*, csr0;*/
	SHAREDKEY_MODE_STRUC csr1;

	PUCHAR		pKey = pCipherKey->Key;
	PUCHAR		pTxMic = pCipherKey->TxMic;
	PUCHAR		pRxMic = pCipherKey->RxMic;
	UCHAR		CipherAlg = pCipherKey->CipherAlg;

	DBGPRINT(RT_DEBUG_TRACE, ("AsicAddSharedKeyEntry BssIndex=%d, KeyIdx=%d\n", BssIndex,KeyIdx));
/*============================================================================================*/

	DBGPRINT(RT_DEBUG_TRACE,("AsicAddSharedKeyEntry: %s key #%d\n", CipherName[CipherAlg], BssIndex*4 + KeyIdx));
	DBGPRINT_RAW(RT_DEBUG_TRACE, (" 	Key = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		pKey[0],pKey[1],pKey[2],pKey[3],pKey[4],pKey[5],pKey[6],pKey[7],pKey[8],pKey[9],pKey[10],pKey[11],pKey[12],pKey[13],pKey[14],pKey[15]));
	if (pRxMic)
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE, (" 	Rx MIC Key = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			pRxMic[0],pRxMic[1],pRxMic[2],pRxMic[3],pRxMic[4],pRxMic[5],pRxMic[6],pRxMic[7]));
	}
	if (pTxMic)
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE, (" 	Tx MIC Key = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			pTxMic[0],pTxMic[1],pTxMic[2],pTxMic[3],pTxMic[4],pTxMic[5],pTxMic[6],pTxMic[7]));
	}
/*============================================================================================*/
	
	/* fill key material - key + TX MIC + RX MIC*/
	

#ifdef RTMP_MAC_USB
{
	offset = SHARED_KEY_TABLE_BASE + (4*BssIndex + KeyIdx)*HW_KEY_ENTRY_SIZE;
	RTUSBMultiWrite(pAd, offset, pKey, MAX_LEN_OF_SHARE_KEY);

	offset += MAX_LEN_OF_SHARE_KEY;
	if (pTxMic)
	{
		RTUSBMultiWrite(pAd, offset, pTxMic, 8);
	}

	offset += 8;
	if (pRxMic)
	{
		RTUSBMultiWrite(pAd, offset, pRxMic, 8);
	}
}
#endif /* RTMP_MAC_USB */

	
	/* Update cipher algorithm. WSTA always use BSS0*/
	
	RTMP_IO_READ32(pAd, SHARED_KEY_MODE_BASE+4*(BssIndex/2), &csr1.word);
	DBGPRINT(RT_DEBUG_TRACE,("Read: SHARED_KEY_MODE_BASE at this Bss[%d] KeyIdx[%d]= 0x%x \n", BssIndex,KeyIdx, csr1.word));
	if ((BssIndex%2) == 0)
	{
		if (KeyIdx == 0)
			csr1.field.Bss0Key0CipherAlg = CipherAlg;
		else if (KeyIdx == 1)
			csr1.field.Bss0Key1CipherAlg = CipherAlg;
		else if (KeyIdx == 2)
			csr1.field.Bss0Key2CipherAlg = CipherAlg;
		else
			csr1.field.Bss0Key3CipherAlg = CipherAlg;
	}
	else
	{
		if (KeyIdx == 0)
			csr1.field.Bss1Key0CipherAlg = CipherAlg;
		else if (KeyIdx == 1)
			csr1.field.Bss1Key1CipherAlg = CipherAlg;
		else if (KeyIdx == 2)
			csr1.field.Bss1Key2CipherAlg = CipherAlg;
		else
			csr1.field.Bss1Key3CipherAlg = CipherAlg;
	}
	DBGPRINT(RT_DEBUG_TRACE,("Write: SHARED_KEY_MODE_BASE at this Bss[%d] = 0x%x \n", BssIndex, csr1.word));
	RTMP_IO_WRITE32(pAd, SHARED_KEY_MODE_BASE+4*(BssIndex/2), csr1.word);
		
}

/*	IRQL = DISPATCH_LEVEL*/
VOID AsicRemoveSharedKeyEntry(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		 BssIndex,
	IN UCHAR		 KeyIdx)
{
	/*ULONG SecCsr0;*/
	SHAREDKEY_MODE_STRUC csr1;

	DBGPRINT(RT_DEBUG_TRACE,("AsicRemoveSharedKeyEntry: #%d \n", BssIndex*4 + KeyIdx));

	RTMP_IO_READ32(pAd, SHARED_KEY_MODE_BASE+4*(BssIndex/2), &csr1.word);
	if ((BssIndex%2) == 0)
	{
		if (KeyIdx == 0)
			csr1.field.Bss0Key0CipherAlg = 0;
		else if (KeyIdx == 1)
			csr1.field.Bss0Key1CipherAlg = 0;
		else if (KeyIdx == 2)
			csr1.field.Bss0Key2CipherAlg = 0;
		else
			csr1.field.Bss0Key3CipherAlg = 0;
	}
	else
	{
		if (KeyIdx == 0)
			csr1.field.Bss1Key0CipherAlg = 0;
		else if (KeyIdx == 1)
			csr1.field.Bss1Key1CipherAlg = 0;
		else if (KeyIdx == 2)
			csr1.field.Bss1Key2CipherAlg = 0;
		else
			csr1.field.Bss1Key3CipherAlg = 0;
	}
	DBGPRINT(RT_DEBUG_TRACE,("Write: SHARED_KEY_MODE_BASE at this Bss[%d] = 0x%x \n", BssIndex, csr1.word));
	RTMP_IO_WRITE32(pAd, SHARED_KEY_MODE_BASE+4*(BssIndex/2), csr1.word);
	ASSERT(BssIndex < 4);
	ASSERT(KeyIdx < 4);

}

VOID AsicUpdateWCIDIVEIV(
	IN PRTMP_ADAPTER pAd,
	IN USHORT		WCID,
	IN ULONG        uIV,
	IN ULONG        uEIV)
{
	ULONG	offset;

	offset = MAC_IVEIV_TABLE_BASE + (WCID * HW_IVEIV_ENTRY_SIZE);

	RTMP_IO_WRITE32(pAd, offset, uIV);
	RTMP_IO_WRITE32(pAd, offset + 4, uEIV);

	DBGPRINT(RT_DEBUG_TRACE, ("%s: wcid(%d) 0x%08lx, 0x%08lx \n", 
									__FUNCTION__, WCID, uIV, uEIV));	
}

VOID AsicUpdateRxWCIDTable(
	IN PRTMP_ADAPTER pAd,
	IN USHORT		WCID,
	IN PUCHAR        pAddr)
{
	ULONG offset;
	ULONG Addr;
	
	offset = MAC_WCID_BASE + (WCID * HW_WCID_ENTRY_SIZE);	
	Addr = pAddr[0] + (pAddr[1] << 8) +(pAddr[2] << 16) +(pAddr[3] << 24);
	RTMP_IO_WRITE32(pAd, offset, Addr);
	Addr = pAddr[4] + (pAddr[5] << 8);
	RTMP_IO_WRITE32(pAd, offset + 4, Addr);	
}
	
/*
	========================================================================
	Description:
		Add Client security information into ASIC WCID table and IVEIV table.
    Return:

    Note :
		The key table selection rule :
    	1.	Wds-links and Mesh-links always use Pair-wise key table. 	
		2. 	When the CipherAlg is TKIP, AES, SMS4 or the dynamic WEP is enabled, 
			it needs to set key into Pair-wise Key Table.
		3.	The pair-wise key security mode is set NONE, it means as no security.
		4.	In STA Adhoc mode, it always use shared key table.
		5.	Otherwise, use shared key table

	========================================================================
*/
VOID	AsicUpdateWcidAttributeEntry(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			BssIdx,
	IN 	UCHAR		 	KeyIdx,
	IN 	UCHAR		 	CipherAlg,
	IN	UINT8			Wcid,
	IN	UINT8			KeyTabFlag)
{
	WCID_ATTRIBUTE_STRUC WCIDAttri;	
	USHORT		offset;

	/* Initialize the content of WCID Attribue  */
	WCIDAttri.word = 0;

	/* The limitation of HW WCID table */
	if (/*Wcid < 1 ||*/ Wcid > 254)
	{		
		DBGPRINT(RT_DEBUG_WARN, ("%s: Wcid is invalid (%d). \n", 
										__FUNCTION__, Wcid));	
		return;
	}

	/* Update the pairwise key security mode.
	   Use bit10 and bit3~1 to indicate the pairwise cipher mode */	
	WCIDAttri.field.PairKeyModeExt = ((CipherAlg & 0x08) >> 3);
	WCIDAttri.field.PairKeyMode = (CipherAlg & 0x07);

	/* Update the MBSS index.
	   Use bit11 and bit6~4 to indicate the BSS index */	
	WCIDAttri.field.BSSIdxExt = ((BssIdx & 0x08) >> 3);
	WCIDAttri.field.BSSIdx = (BssIdx & 0x07);

	
	/* Assign Key Table selection */		
	WCIDAttri.field.KeyTab = KeyTabFlag;

	/* Update related information to ASIC */
	offset = MAC_WCID_ATTRIBUTE_BASE + (Wcid * HW_WCID_ATTRI_SIZE);
	RTMP_IO_WRITE32(pAd, offset, WCIDAttri.word);

	DBGPRINT(RT_DEBUG_TRACE, ("%s : WCID #%d, KeyIndex #%d, Alg=%s\n", __FUNCTION__, Wcid, KeyIdx, CipherName[CipherAlg]));
	DBGPRINT(RT_DEBUG_TRACE, ("		WCIDAttri = 0x%x \n", WCIDAttri.word));	
	
}
	

/*
	========================================================================
	Description:
		Add Pair-wise key material into ASIC. 
		Update pairwise key, TxMic and RxMic to Asic Pair-wise key table
				
    Return:
	========================================================================
*/
VOID AsicAddPairwiseKeyEntry(
	IN PRTMP_ADAPTER 	pAd,
	IN UCHAR			WCID,
	IN PCIPHER_KEY		pCipherKey)
{
	INT i;
	ULONG 		offset;
	PUCHAR		 pKey = pCipherKey->Key;
	PUCHAR		 pTxMic = pCipherKey->TxMic;
	PUCHAR		 pRxMic = pCipherKey->RxMic;
	UCHAR		CipherAlg = pCipherKey->CipherAlg;
#ifdef SPECIFIC_BCN_BUF_SUPPORT
	unsigned long irqFlag = 0;
#endif /* SPECIFIC_BCN_BUF_SUPPORT */

#ifdef SPECIFIC_BCN_BUF_SUPPORT
	RTMP_MAC_SHR_MSEL_LOCK(pAd, LOWER_SHRMEM, irqFlag);
#endif /* SPECIFIC_BCN_BUF_SUPPORT */
	
	/* EKEY*/
	offset = PAIRWISE_KEY_TABLE_BASE + (WCID * HW_KEY_ENTRY_SIZE);
#ifdef RTMP_MAC_USB
	RTUSBMultiWrite(pAd, offset, &pCipherKey->Key[0], MAX_LEN_OF_PEER_KEY);
#endif /* RTMP_MAC_USB */
	for (i=0; i<MAX_LEN_OF_PEER_KEY; i+=4)
	{
		UINT32 Value;
		RTMP_IO_READ32(pAd, offset + i, &Value);
	}

	offset += MAX_LEN_OF_PEER_KEY;
	
	/*  MIC KEY*/
	if (pTxMic)
	{
#ifdef RTMP_MAC_USB
		RTUSBMultiWrite(pAd, offset, &pCipherKey->TxMic[0], 8);
#endif /* RTMP_MAC_USB */
	}
	offset += 8;
	if (pRxMic)
	{
#ifdef RTMP_MAC_USB
		RTUSBMultiWrite(pAd, offset, &pCipherKey->RxMic[0], 8);
#endif /* RTMP_MAC_USB */
	}

#ifdef SPECIFIC_BCN_BUF_SUPPORT
	RTMP_MAC_SHR_MSEL_UNLOCK(pAd, LOWER_SHRMEM, irqFlag);
#endif /* SPECIFIC_BCN_BUF_SUPPORT*/

	DBGPRINT(RT_DEBUG_TRACE,("AsicAddPairwiseKeyEntry: WCID #%d Alg=%s\n",WCID, CipherName[CipherAlg]));
	DBGPRINT(RT_DEBUG_TRACE,("	Key = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		pKey[0],pKey[1],pKey[2],pKey[3],pKey[4],pKey[5],pKey[6],pKey[7],pKey[8],pKey[9],pKey[10],pKey[11],pKey[12],pKey[13],pKey[14],pKey[15]));
	if (pRxMic)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("	Rx MIC Key = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			pRxMic[0],pRxMic[1],pRxMic[2],pRxMic[3],pRxMic[4],pRxMic[5],pRxMic[6],pRxMic[7]));
	}
	if (pTxMic)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("	Tx MIC Key = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			pTxMic[0],pTxMic[1],pTxMic[2],pTxMic[3],pTxMic[4],pTxMic[5],pTxMic[6],pTxMic[7]));
	}
}
/*
	========================================================================
	Description:
		Remove Pair-wise key material from ASIC. 

    Return:
	========================================================================
*/	
VOID AsicRemovePairwiseKeyEntry(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		 Wcid)
{
	/* Set the specific WCID attribute entry as OPEN-NONE */
	AsicUpdateWcidAttributeEntry(pAd, 
							  BSS0,
							  0,
							  CIPHER_NONE, 
							  Wcid,
							  PAIRWISEKEYTABLE);

	DBGPRINT(RT_DEBUG_TRACE, ("%s : Wcid #%d \n", __FUNCTION__, Wcid));
}

BOOLEAN AsicSendCommandToMcu(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		 Command,
	IN UCHAR		 Token,
	IN UCHAR		 Arg0,
	IN UCHAR		 Arg1)
{
	if (pAd->chipOps.sendCommandToMcu)
		return pAd->chipOps.sendCommandToMcu(pAd, Command, Token, Arg0, Arg1, TRUE);
	else
		return FALSE;
}


BOOLEAN AsicSendCommandToMcuBBP(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		 Command,
	IN UCHAR		 Token,
	IN UCHAR		 Arg0,
	IN UCHAR		 Arg1,
	IN BOOLEAN		FlgIsNeedLocked)
{
	if (pAd->chipOps.sendCommandToMcu)
		return pAd->chipOps.sendCommandToMcu(pAd, Command, Token, Arg0, Arg1, FlgIsNeedLocked);
	else
		return FALSE;
}

/*
	========================================================================
	Description:
		For 1x1 chipset : 2070 / 3070 / 3090 / 3370 / 3390 / 5370 / 5390 
		Usage :	1. Set Default Antenna as initialize
				2. Antenna Diversity switching used
				3. iwpriv command switch Antenna

    Return:
	========================================================================
 */
VOID AsicSetRxAnt(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			Ant)
{
	if (pAd->chipOps.SetRxAnt)
		pAd->chipOps.SetRxAnt(pAd, Ant);

}


VOID AsicTurnOffRFClk(
	IN PRTMP_ADAPTER pAd, 
	IN	UCHAR		Channel) 
{
	if (pAd->chipOps.AsicRfTurnOff)
	{
		pAd->chipOps.AsicRfTurnOff(pAd);
	}
	else
	{
#if defined(RT28xx) || defined(RT2880) || defined(RT2883)
		/* RF R2 bit 18 = 0*/
		UINT32			R1 = 0, R2 = 0, R3 = 0;
		UCHAR			index;
		RTMP_RF_REGS	*RFRegTable;
	
		RFRegTable = RF2850RegTable;
#endif /* defined(RT28xx) || defined(RT2880) || defined(RT2883) */

		switch (pAd->RfIcType)
		{
#if defined(RT28xx) || defined(RT2880) || defined(RT2883)
#if defined(RT28xx) || defined(RT2880)
			case RFIC_2820:
			case RFIC_2850:
			case RFIC_2720:
			case RFIC_2750:
#endif /* defined(RT28xx) || defined(RT2880) */
				for (index = 0; index < NUM_OF_2850_CHNL; index++)
				{
					if (Channel == RFRegTable[index].Channel)
					{
						R1 = RFRegTable[index].R1 & 0xffffdfff;
						R2 = RFRegTable[index].R2 & 0xfffbffff;
						R3 = RFRegTable[index].R3 & 0xfff3ffff;

						RTMP_RF_IO_WRITE32(pAd, R1);
						RTMP_RF_IO_WRITE32(pAd, R2);

						/* Program R1b13 to 1, R3/b18,19 to 0, R2b18 to 0. */
						/* Set RF R2 bit18=0, R3 bit[18:19]=0*/
						/*if (pAd->StaCfg.bRadio == FALSE)*/
						if (1)
						{
							RTMP_RF_IO_WRITE32(pAd, R3);

							DBGPRINT(RT_DEBUG_TRACE, ("AsicTurnOffRFClk#%d(RF=%d, ) , R2=0x%08x,  R3 = 0x%08x \n",
								Channel, pAd->RfIcType, R2, R3));
						}
						else
							DBGPRINT(RT_DEBUG_TRACE, ("AsicTurnOffRFClk#%d(RF=%d, ) , R2=0x%08x \n",
								Channel, pAd->RfIcType, R2));
						break;
					}
				}
				break;
#endif /* defined(RT28xx) || defined(RT2880) || defined(RT2883) */
			default:
				DBGPRINT(RT_DEBUG_TRACE, ("AsicTurnOffRFClk#%d : Unkonwn RFIC=%d\n",
											Channel, pAd->RfIcType));
				break;
		}
	}
}





#ifdef VCORECAL_SUPPORT
VOID AsicVCORecalibration(
	IN PRTMP_ADAPTER pAd)
{
	UCHAR BbpR49 = 0, Tssi = 0;
#if defined (RT5350)
	UCHAR BbpR47 = 0;
#endif
#ifdef RALINK_ATE
	if (ATE_ON(pAd))
		return;
#endif

	if (pAd->chipCap.FlgIsVcoReCalSup == FALSE)
		return;

#if defined (RT5350)
	if (pAd->TxPowerCtrl.bInternalTxALC == TRUE)
	{
	    //TSSI_REPORT_SEL = 0
	    RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpR47);
	    BbpR47 &= ~0x3;
	    RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpR47 );
	}
#endif

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49);
	Tssi = BbpR49 >> 1; /* bit 0 is used for update flag*/
	
	/*DBGPRINT(RT_DEBUG_TRACE, ("AsicVCORecalibration: BbpR49=%x TSSI difference=%d\n", BbpR49, abs((pAd->LatchTssi) - Tssi)));*/
	if (abs((pAd->LatchTssi) - Tssi) >= pAd->CommonCfg.VCORecalibrationThreshold)
	{
		UCHAR RFValue = 0;
	
		DBGPRINT(RT_DEBUG_TRACE, ("AsicVCORecalibration: vcocal_en=1, TSSI difference=%d\n", abs((pAd->LatchTssi) - Tssi)));

		RT30xxReadRFRegister(pAd, RF_R03, (PUCHAR)&RFValue);
		RFValue = RFValue | 0x80; /* bit 7=vcocal_en*/
		RT30xxWriteRFRegister(pAd, RF_R03, (UCHAR)RFValue);
		RTMPusecDelay(2000);

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R49, BbpR49 & 0xfe); /* clear update flag*/
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49);
		pAd->RefreshTssi = 1;
	}
}
#endif /* VCORECAL_SUPPORT */

