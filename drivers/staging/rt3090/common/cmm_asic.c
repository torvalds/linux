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
	cmm_asic.c

	Abstract:
	Functions used to communicate with ASIC

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
*/

#include "../rt_config.h"


// Reset the RFIC setting to new series
RTMP_RF_REGS RF2850RegTable[] = {
//		ch	 R1		 R2		 R3(TX0~4=0) R4
		{1,  0x98402ecc, 0x984c0786, 0x9816b455, 0x9800510b},
		{2,  0x98402ecc, 0x984c0786, 0x98168a55, 0x9800519f},
		{3,  0x98402ecc, 0x984c078a, 0x98168a55, 0x9800518b},
		{4,  0x98402ecc, 0x984c078a, 0x98168a55, 0x9800519f},
		{5,  0x98402ecc, 0x984c078e, 0x98168a55, 0x9800518b},
		{6,  0x98402ecc, 0x984c078e, 0x98168a55, 0x9800519f},
		{7,  0x98402ecc, 0x984c0792, 0x98168a55, 0x9800518b},
		{8,  0x98402ecc, 0x984c0792, 0x98168a55, 0x9800519f},
		{9,  0x98402ecc, 0x984c0796, 0x98168a55, 0x9800518b},
		{10, 0x98402ecc, 0x984c0796, 0x98168a55, 0x9800519f},
		{11, 0x98402ecc, 0x984c079a, 0x98168a55, 0x9800518b},
		{12, 0x98402ecc, 0x984c079a, 0x98168a55, 0x9800519f},
		{13, 0x98402ecc, 0x984c079e, 0x98168a55, 0x9800518b},
		{14, 0x98402ecc, 0x984c07a2, 0x98168a55, 0x98005193},

		// 802.11 UNI / HyperLan 2
		{36, 0x98402ecc, 0x984c099a, 0x98158a55, 0x980ed1a3},
		{38, 0x98402ecc, 0x984c099e, 0x98158a55, 0x980ed193},
		{40, 0x98402ec8, 0x984c0682, 0x98158a55, 0x980ed183},
		{44, 0x98402ec8, 0x984c0682, 0x98158a55, 0x980ed1a3},
		{46, 0x98402ec8, 0x984c0686, 0x98158a55, 0x980ed18b},
		{48, 0x98402ec8, 0x984c0686, 0x98158a55, 0x980ed19b},
		{52, 0x98402ec8, 0x984c068a, 0x98158a55, 0x980ed193},
		{54, 0x98402ec8, 0x984c068a, 0x98158a55, 0x980ed1a3},
		{56, 0x98402ec8, 0x984c068e, 0x98158a55, 0x980ed18b},
		{60, 0x98402ec8, 0x984c0692, 0x98158a55, 0x980ed183},
		{62, 0x98402ec8, 0x984c0692, 0x98158a55, 0x980ed193},
		{64, 0x98402ec8, 0x984c0692, 0x98158a55, 0x980ed1a3}, // Plugfest#4, Day4, change RFR3 left4th 9->5.

		// 802.11 HyperLan 2
		{100, 0x98402ec8, 0x984c06b2, 0x98178a55, 0x980ed783},

		// 2008.04.30 modified
		// The system team has AN to improve the EVM value
		// for channel 102 to 108 for the RT2850/RT2750 dual band solution.
		{102, 0x98402ec8, 0x985c06b2, 0x98578a55, 0x980ed793},
		{104, 0x98402ec8, 0x985c06b2, 0x98578a55, 0x980ed1a3},
		{108, 0x98402ecc, 0x985c0a32, 0x98578a55, 0x980ed193},

		{110, 0x98402ecc, 0x984c0a36, 0x98178a55, 0x980ed183},
		{112, 0x98402ecc, 0x984c0a36, 0x98178a55, 0x980ed19b},
		{116, 0x98402ecc, 0x984c0a3a, 0x98178a55, 0x980ed1a3},
		{118, 0x98402ecc, 0x984c0a3e, 0x98178a55, 0x980ed193},
		{120, 0x98402ec4, 0x984c0382, 0x98178a55, 0x980ed183},
		{124, 0x98402ec4, 0x984c0382, 0x98178a55, 0x980ed193},
		{126, 0x98402ec4, 0x984c0382, 0x98178a55, 0x980ed15b}, // 0x980ed1bb->0x980ed15b required by Rory 20070927
		{128, 0x98402ec4, 0x984c0382, 0x98178a55, 0x980ed1a3},
		{132, 0x98402ec4, 0x984c0386, 0x98178a55, 0x980ed18b},
		{134, 0x98402ec4, 0x984c0386, 0x98178a55, 0x980ed193},
		{136, 0x98402ec4, 0x984c0386, 0x98178a55, 0x980ed19b},
		{140, 0x98402ec4, 0x984c038a, 0x98178a55, 0x980ed183},

		// 802.11 UNII
		{149, 0x98402ec4, 0x984c038a, 0x98178a55, 0x980ed1a7},
		{151, 0x98402ec4, 0x984c038e, 0x98178a55, 0x980ed187},
		{153, 0x98402ec4, 0x984c038e, 0x98178a55, 0x980ed18f},
		{157, 0x98402ec4, 0x984c038e, 0x98178a55, 0x980ed19f},
		{159, 0x98402ec4, 0x984c038e, 0x98178a55, 0x980ed1a7},
		{161, 0x98402ec4, 0x984c0392, 0x98178a55, 0x980ed187},
		{165, 0x98402ec4, 0x984c0392, 0x98178a55, 0x980ed197},
		{167, 0x98402ec4, 0x984c03d2, 0x98179855, 0x9815531f},
		{169, 0x98402ec4, 0x984c03d2, 0x98179855, 0x98155327},
		{171, 0x98402ec4, 0x984c03d6, 0x98179855, 0x98155307},
		{173, 0x98402ec4, 0x984c03d6, 0x98179855, 0x9815530f},

		// Japan
		{184, 0x95002ccc, 0x9500491e, 0x9509be55, 0x950c0a0b},
		{188, 0x95002ccc, 0x95004922, 0x9509be55, 0x950c0a13},
		{192, 0x95002ccc, 0x95004926, 0x9509be55, 0x950c0a1b},
		{196, 0x95002ccc, 0x9500492a, 0x9509be55, 0x950c0a23},
		{208, 0x95002ccc, 0x9500493a, 0x9509be55, 0x950c0a13},
		{212, 0x95002ccc, 0x9500493e, 0x9509be55, 0x950c0a1b},
		{216, 0x95002ccc, 0x95004982, 0x9509be55, 0x950c0a23},

		// still lack of MMAC(Japan) ch 34,38,42,46
};
UCHAR	NUM_OF_2850_CHNL = (sizeof(RF2850RegTable) / sizeof(RTMP_RF_REGS));

FREQUENCY_ITEM FreqItems3020[] =
{
	/**************************************************/
	// ISM : 2.4 to 2.483 GHz                         //
	/**************************************************/
	// 11g
	/**************************************************/
	//-CH---N-------R---K-----------
	{1,    241,  2,  2},
	{2,    241,	 2,  7},
	{3,    242,	 2,  2},
	{4,    242,	 2,  7},
	{5,    243,	 2,  2},
	{6,    243,	 2,  7},
	{7,    244,	 2,  2},
	{8,    244,	 2,  7},
	{9,    245,	 2,  2},
	{10,   245,	 2,  7},
	{11,   246,	 2,  2},
	{12,   246,	 2,  7},
	{13,   247,	 2,  2},
	{14,   248,	 2,  4},
};
UCHAR	NUM_OF_3020_CHNL = (sizeof(FreqItems3020) / sizeof(FREQUENCY_ITEM));


VOID AsicUpdateAutoFallBackTable(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pRateTable)
{
	UCHAR					i;
	HT_FBK_CFG0_STRUC		HtCfg0;
	HT_FBK_CFG1_STRUC		HtCfg1;
	LG_FBK_CFG0_STRUC		LgCfg0;
	LG_FBK_CFG1_STRUC		LgCfg1;
	PRTMP_TX_RATE_SWITCH	pCurrTxRate, pNextTxRate;

	// set to initial value
	HtCfg0.word = 0x65432100;
	HtCfg1.word = 0xedcba988;
	LgCfg0.word = 0xedcba988;
	LgCfg1.word = 0x00002100;

	pNextTxRate = (PRTMP_TX_RATE_SWITCH)pRateTable+1;
	for (i = 1; i < *((PUCHAR) pRateTable); i++)
	{
		pCurrTxRate = (PRTMP_TX_RATE_SWITCH)pRateTable+1+i;
		switch (pCurrTxRate->Mode)
		{
			case 0:		//CCK
				break;
			case 1:		//OFDM
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
			case 2:		//HT-MIX
			case 3:		//HT-GF
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
							default:
								DBGPRINT(RT_DEBUG_ERROR, ("AsicUpdateAutoFallBackTable: not support CurrMCS=%d\n", pCurrTxRate->CurrMCS));
						}
					}
				}
				break;
#endif // DOT11_N_SUPPORT //
		}

		pNextTxRate = pCurrTxRate;
	}

	RTMP_IO_WRITE32(pAd, HT_FBK_CFG0, HtCfg0.word);
	RTMP_IO_WRITE32(pAd, HT_FBK_CFG1, HtCfg1.word);
	RTMP_IO_WRITE32(pAd, LG_FBK_CFG0, LgCfg0.word);
	RTMP_IO_WRITE32(pAd, LG_FBK_CFG1, LgCfg1.word);
}

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
VOID	AsicUpdateProtect(
	IN		PRTMP_ADAPTER	pAd,
	IN		USHORT			OperationMode,
	IN		UCHAR			SetMask,
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
#endif // RALINK_ATE //

#ifdef DOT11_N_SUPPORT
	if (!(pAd->CommonCfg.bHTProtect) && (OperationMode != 8))
	{
		return;
	}

	if (pAd->BATable.numDoneOriginator)
	{
		//
		// enable the RTS/CTS to avoid channel collision
		//
		SetMask = ALLN_SETPROTECT;
		OperationMode = 8;
	}
#endif // DOT11_N_SUPPORT //

	// Config ASIC RTS threshold register
	RTMP_IO_READ32(pAd, TX_RTS_CFG, &MacReg);
	MacReg &= 0xFF0000FF;
	// If the user want disable RtsThreshold and enbale Amsdu/Ralink-Aggregation, set the RtsThreshold as 4096
        if ((
#ifdef DOT11_N_SUPPORT
			(pAd->CommonCfg.BACapability.field.AmsduEnable) ||
#endif // DOT11_N_SUPPORT //
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

	// Initial common protection settings
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

	// update PHY mode and rate
	if (pAd->CommonCfg.Channel > 14)
		ProtCfg.field.ProtectRate = 0x4000;
	ProtCfg.field.ProtectRate |= pAd->CommonCfg.RtsRate;

	// Handle legacy(B/G) protection
	if (bDisableBGProtect)
	{
		//ProtCfg.field.ProtectRate = pAd->CommonCfg.RtsRate;
		ProtCfg.field.ProtectCtrl = 0;
		Protect[0] = ProtCfg.word;
		Protect[1] = ProtCfg.word;
		pAd->FlgCtsEnabled = 0; /* CTS-self is not used */
	}
	else
	{
		//ProtCfg.field.ProtectRate = pAd->CommonCfg.RtsRate;
		ProtCfg.field.ProtectCtrl = 0;			// CCK do not need to be protected
		Protect[0] = ProtCfg.word;
		ProtCfg.field.ProtectCtrl = ASIC_CTS;	// OFDM needs using CCK to protect
		Protect[1] = ProtCfg.word;
		pAd->FlgCtsEnabled = 1; /* CTS-self is used */
	}

#ifdef DOT11_N_SUPPORT
	// Decide HT frame protection.
	if ((SetMask & ALLN_SETPROTECT) != 0)
	{
		switch(OperationMode)
		{
			case 0x0:
				// NO PROTECT
				// 1.All STAs in the BSS are 20/40 MHz HT
				// 2. in ai 20/40MHz BSS
				// 3. all STAs are 20MHz in a 20MHz BSS
				// Pure HT. no protection.

				// MM20_PROT_CFG
				//	Reserved (31:27)
				//	PROT_TXOP(25:20) -- 010111
				//	PROT_NAV(19:18)  -- 01 (Short NAV protection)
				//  PROT_CTRL(17:16) -- 00 (None)
				//	PROT_RATE(15:0)  -- 0x4004 (OFDM 24M)
				Protect[2] = 0x01744004;

				// MM40_PROT_CFG
				//	Reserved (31:27)
				//	PROT_TXOP(25:20) -- 111111
				//	PROT_NAV(19:18)  -- 01 (Short NAV protection)
				//  PROT_CTRL(17:16) -- 00 (None)
				//	PROT_RATE(15:0)  -- 0x4084 (duplicate OFDM 24M)
				Protect[3] = 0x03f44084;

				// CF20_PROT_CFG
				//	Reserved (31:27)
				//	PROT_TXOP(25:20) -- 010111
				//	PROT_NAV(19:18)  -- 01 (Short NAV protection)
				//  PROT_CTRL(17:16) -- 00 (None)
				//	PROT_RATE(15:0)  -- 0x4004 (OFDM 24M)
				Protect[4] = 0x01744004;

				// CF40_PROT_CFG
				//	Reserved (31:27)
				//	PROT_TXOP(25:20) -- 111111
				//	PROT_NAV(19:18)  -- 01 (Short NAV protection)
				//  PROT_CTRL(17:16) -- 00 (None)
				//	PROT_RATE(15:0)  -- 0x4084 (duplicate OFDM 24M)
				Protect[5] = 0x03f44084;

				if (bNonGFExist)
				{
					// PROT_NAV(19:18)  -- 01 (Short NAV protectiion)
					// PROT_CTRL(17:16) -- 01 (RTS/CTS)
					Protect[4] = 0x01754004;
					Protect[5] = 0x03f54084;
				}
				pAd->CommonCfg.IOTestParm.bRTSLongProtOn = FALSE;
				break;

			case 1:
				// This is "HT non-member protection mode."
				// If there may be non-HT STAs my BSS
				ProtCfg.word = 0x01744004;	// PROT_CTRL(17:16) : 0 (None)
				ProtCfg4.word = 0x03f44084; // duplicaet legacy 24M. BW set 1.
				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED))
				{
					ProtCfg.word = 0x01740003;	//ERP use Protection bit is set, use protection rate at Clause 18..
					ProtCfg4.word = 0x03f40003; // Don't duplicate RTS/CTS in CCK mode. 0x03f40083;
				}
				//Assign Protection method for 20&40 MHz packets
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
				// If only HT STAs are in BSS. at least one is 20MHz. Only protect 40MHz packets
				ProtCfg.word = 0x01744004;  // PROT_CTRL(17:16) : 0 (None)
				ProtCfg4.word = 0x03f44084; // duplicaet legacy 24M. BW set 1.

				//Assign Protection method for 40MHz packets
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
				// HT mixed mode.	 PROTECT ALL!
				// Assign Rate
				ProtCfg.word = 0x01744004;	//duplicaet legacy 24M. BW set 1.
				ProtCfg4.word = 0x03f44084;
				// both 20MHz and 40MHz are protected. Whether use RTS or CTS-to-self depends on the
				if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_BG_PROTECTION_INUSED))
				{
					ProtCfg.word = 0x01740003;	//ERP use Protection bit is set, use protection rate at Clause 18..
					ProtCfg4.word = 0x03f40003; // Don't duplicate RTS/CTS in CCK mode. 0x03f40083
				}
				//Assign Protection method for 20&40 MHz packets
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
				// Special on for Atheros problem n chip.
				Protect[2] = 0x01754004;
				Protect[3] = 0x03f54084;
				Protect[4] = 0x01754004;
				Protect[5] = 0x03f54084;
				pAd->CommonCfg.IOTestParm.bRTSLongProtOn = TRUE;
				break;
		}
	}
#endif // DOT11_N_SUPPORT //

	offset = CCK_PROT_CFG;
	for (i = 0;i < 6;i++)
	{
			if ((SetMask & (1<< i)))
		{
		RTMP_IO_WRITE32(pAd, offset + i*4, Protect[i]);
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
VOID AsicSwitchChannel(
					  IN PRTMP_ADAPTER pAd,
	IN	UCHAR			Channel,
	IN	BOOLEAN			bScan)
{
	ULONG			R2 = 0, R3 = DEFAULT_RF_TX_POWER, R4 = 0;
	CHAR    TxPwer = 0, TxPwer2 = DEFAULT_RF_TX_POWER; //Bbp94 = BBPR94_DEFAULT, TxPwer2 = DEFAULT_RF_TX_POWER;
	UCHAR	index;
	UINT32	Value = 0; //BbpReg, Value;
	RTMP_RF_REGS *RFRegTable;
	UCHAR	RFValue;

	RFValue = 0;
	// Search Tx power value
	// We can't use ChannelList to search channel, since some central channl's txpowr doesn't list
	// in ChannelList, so use TxPower array instead.
	//
	for (index = 0; index < MAX_NUM_OF_CHANNELS; index++)
	{
		if (Channel == pAd->TxPower[index].Channel)
		{
			TxPwer = pAd->TxPower[index].Power;
			TxPwer2 = pAd->TxPower[index].Power2;
			break;
		}
	}

	if (index == MAX_NUM_OF_CHANNELS)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("AsicSwitchChannel: Can't find the Channel#%d \n", Channel));
	}

#ifdef RT30xx
	// The RF programming sequence is difference between 3xxx and 2xxx
	if ((IS_RT3070(pAd) || IS_RT3090(pAd)||IS_RT3390(pAd)) && ((pAd->RfIcType == RFIC_3020) || (pAd->RfIcType == RFIC_2020) ||
		(pAd->RfIcType == RFIC_3021) || (pAd->RfIcType == RFIC_3022)))
	{
		/* modify by WY for Read RF Reg. error */

		for (index = 0; index < NUM_OF_3020_CHNL; index++)
		{
			if (Channel == FreqItems3020[index].Channel)
			{
				// Programming channel parameters
				RT30xxWriteRFRegister(pAd, RF_R02, FreqItems3020[index].N);
				RT30xxWriteRFRegister(pAd, RF_R03, FreqItems3020[index].K);
				RT30xxReadRFRegister(pAd, RF_R06, &RFValue);
				RFValue = (RFValue & 0xFC) | FreqItems3020[index].R;
				RT30xxWriteRFRegister(pAd, RF_R06, RFValue);

				// Set Tx0 Power
				RT30xxReadRFRegister(pAd, RF_R12, &RFValue);
				RFValue = (RFValue & 0xE0) | TxPwer;
				RT30xxWriteRFRegister(pAd, RF_R12, RFValue);

				// Set Tx1 Power
				RT30xxReadRFRegister(pAd, RF_R13, &RFValue);
				RFValue = (RFValue & 0xE0) | TxPwer2;
				RT30xxWriteRFRegister(pAd, RF_R13, RFValue);

				// Tx/Rx Stream setting
				RT30xxReadRFRegister(pAd, RF_R01, &RFValue);
				//if (IS_RT3090(pAd))
				//	RFValue |= 0x01; // Enable RF block.
				RFValue &= 0x03;	//clear bit[7~2]
				if (pAd->Antenna.field.TxPath == 1)
					RFValue |= 0xA0;
				else if (pAd->Antenna.field.TxPath == 2)
					RFValue |= 0x80;
				if (pAd->Antenna.field.RxPath == 1)
					RFValue |= 0x50;
				else if (pAd->Antenna.field.RxPath == 2)
					RFValue |= 0x40;
				RT30xxWriteRFRegister(pAd, RF_R01, RFValue);

				// Set RF offset
				RT30xxReadRFRegister(pAd, RF_R23, &RFValue);
				RFValue = (RFValue & 0x80) | pAd->RfFreqOffset;
				RT30xxWriteRFRegister(pAd, RF_R23, RFValue);

				// Set BW
				if (!bScan && (pAd->CommonCfg.BBPCurrentBW == BW_40))
				{
					RFValue = pAd->Mlme.CaliBW40RfR24;
					//DISABLE_11N_CHECK(pAd);
				}
				else
				{
					RFValue = pAd->Mlme.CaliBW20RfR24;
				}
				RT30xxWriteRFRegister(pAd, RF_R24, RFValue);
				RT30xxWriteRFRegister(pAd, RF_R31, RFValue);

				// Enable RF tuning
				RT30xxReadRFRegister(pAd, RF_R07, &RFValue);
				RFValue = RFValue | 0x1;
				RT30xxWriteRFRegister(pAd, RF_R07, RFValue);

				// latch channel for future usage.
				pAd->LatchRfRegs.Channel = Channel;

		DBGPRINT(RT_DEBUG_TRACE, ("SwitchChannel#%d(RF=%d, Pwr0=%d, Pwr1=%d, %dT), N=0x%02X, K=0x%02X, R=0x%02X\n",
			Channel,
			pAd->RfIcType,
			TxPwer,
			TxPwer2,
			pAd->Antenna.field.TxPath,
			FreqItems3020[index].N,
			FreqItems3020[index].K,
			FreqItems3020[index].R));

				break;
			}
		}
	}
	else
#endif // RT30xx //
	{
		RFRegTable = RF2850RegTable;
		switch (pAd->RfIcType)
		{
			case RFIC_2820:
			case RFIC_2850:
			case RFIC_2720:
			case RFIC_2750:

				for (index = 0; index < NUM_OF_2850_CHNL; index++)
				{
					if (Channel == RFRegTable[index].Channel)
					{
						R2 = RFRegTable[index].R2;
						if (pAd->Antenna.field.TxPath == 1)
						{
							R2 |= 0x4000;	// If TXpath is 1, bit 14 = 1;
						}

						if (pAd->Antenna.field.RxPath == 2)
						{
							R2 |= 0x40;	// write 1 to off Rxpath.
						}
						else if (pAd->Antenna.field.RxPath == 1)
						{
							R2 |= 0x20040;	// write 1 to off RxPath
						}

						if (Channel > 14)
						{
							// initialize R3, R4
							R3 = (RFRegTable[index].R3 & 0xffffc1ff);
							R4 = (RFRegTable[index].R4 & (~0x001f87c0)) | (pAd->RfFreqOffset << 15);

							// 5G band power range: 0xF9~0X0F, TX0 Reg3 bit9/TX1 Reg4 bit6="0" means the TX power reduce 7dB
							// R3
							if ((TxPwer >= -7) && (TxPwer < 0))
							{
								TxPwer = (7+TxPwer);
								TxPwer = (TxPwer > 0xF) ? (0xF) : (TxPwer);
								R3 |= (TxPwer << 10);
								DBGPRINT(RT_DEBUG_ERROR, ("AsicSwitchChannel: TxPwer=%d \n", TxPwer));
							}
							else
							{
								TxPwer = (TxPwer > 0xF) ? (0xF) : (TxPwer);
								R3 |= (TxPwer << 10) | (1 << 9);
							}

							// R4
							if ((TxPwer2 >= -7) && (TxPwer2 < 0))
							{
								TxPwer2 = (7+TxPwer2);
								TxPwer2 = (TxPwer2 > 0xF) ? (0xF) : (TxPwer2);
								R4 |= (TxPwer2 << 7);
								DBGPRINT(RT_DEBUG_ERROR, ("AsicSwitchChannel: TxPwer2=%d \n", TxPwer2));
							}
							else
							{
								TxPwer2 = (TxPwer2 > 0xF) ? (0xF) : (TxPwer2);
								R4 |= (TxPwer2 << 7) | (1 << 6);
							}
						}
						else
						{
							R3 = (RFRegTable[index].R3 & 0xffffc1ff) | (TxPwer << 9); // set TX power0
						R4 = (RFRegTable[index].R4 & (~0x001f87c0)) | (pAd->RfFreqOffset << 15) | (TxPwer2 <<6);// Set freq Offset & TxPwr1
						}

						// Based on BBP current mode before changing RF channel.
						if (!bScan && (pAd->CommonCfg.BBPCurrentBW == BW_40))
						{
							R4 |=0x200000;
						}

						// Update variables
						pAd->LatchRfRegs.Channel = Channel;
						pAd->LatchRfRegs.R1 = RFRegTable[index].R1;
						pAd->LatchRfRegs.R2 = R2;
						pAd->LatchRfRegs.R3 = R3;
						pAd->LatchRfRegs.R4 = R4;

#ifdef DFS_DEBUG
#ifdef DFS_FCC_BW40_FIX
						if (pAd->infType == RTMP_DEV_INF_PCI) // RT2880 PCI
						{
							/* only for RT2880 */
							// FCC DFS test
							pAd->LatchRfRegs.R1 |= 0x100;
							pAd->LatchRfRegs.R4 |= 0x00400000;
						}
#endif // DFS_FCC_BW40_FIX //
#endif // DFS_DEBUG //

						// Set RF value 1's set R3[bit2] = [0]
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
						RTMP_RF_IO_WRITE32(pAd, (pAd->LatchRfRegs.R3 & (~0x04)));
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

						RTMPusecDelay(200);

						// Set RF value 2's set R3[bit2] = [1]
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
						RTMP_RF_IO_WRITE32(pAd, (pAd->LatchRfRegs.R3 | 0x04));
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

						RTMPusecDelay(200);

						// Set RF value 3's set R3[bit2] = [0]
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R1);
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R2);
						RTMP_RF_IO_WRITE32(pAd, (pAd->LatchRfRegs.R3 & (~0x04)));
						RTMP_RF_IO_WRITE32(pAd, pAd->LatchRfRegs.R4);

						break;
					}
				}
				break;

			default:
				break;
		}

		DBGPRINT(RT_DEBUG_TRACE, ("SwitchChannel#%d(RF=%d, Pwr0=%lu, Pwr1=%lu, %dT) to , R1=0x%08lx, R2=0x%08lx, R3=0x%08lx, R4=0x%08lx\n",
							  Channel,
							  pAd->RfIcType,
							  (R3 & 0x00003e00) >> 9,
							  (R4 & 0x000007c0) >> 6,
							  pAd->Antenna.field.TxPath,
							  pAd->LatchRfRegs.R1,
							  pAd->LatchRfRegs.R2,
							  pAd->LatchRfRegs.R3,
							  pAd->LatchRfRegs.R4));
	}

	// Change BBP setting during siwtch from a->g, g->a
	if (Channel <= 14)
	{
		ULONG	TxPinCfg = 0x00050F0A;//Gary 2007/08/09 0x050A0A

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0);//(0x44 - GET_LNA_GAIN(pAd)));	// According the Rory's suggestion to solve the middle range issue.
		//RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x62);

		// Rx High power VGA offset for LNA select
		if (pAd->NicConfig2.field.ExternalLNAForG)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x62);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x46);
		}
		else
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x84);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x50);
		}

		// 5G band selection PIN, bit1 and bit2 are complement
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x6);
		Value |= (0x04);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		// Turn off unused PA or LNA when only 1T or 1R
		if (pAd->Antenna.field.TxPath == 1)
		{
			TxPinCfg &= 0xFFFFFFF3;
		}
		if (pAd->Antenna.field.RxPath == 1)
		{
			TxPinCfg &= 0xFFFFF3FF;
		}


		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);

#if defined(RT3090) || defined(RT3390)
		// PCIe PHY Transmit attenuation adjustment
		if (IS_RT3090A(pAd) || IS_RT3390(pAd))
		{
			TX_ATTENUATION_CTRL_STRUC TxAttenuationCtrl = {0};

			RTMP_IO_READ32(pAd, PCIE_PHY_TX_ATTENUATION_CTRL, &TxAttenuationCtrl.word);

			if (Channel == 14) // Channel #14
			{
				TxAttenuationCtrl.field.PCIE_PHY_TX_ATTEN_EN = 1; // Enable PCIe PHY Tx attenuation
				TxAttenuationCtrl.field.PCIE_PHY_TX_ATTEN_VALUE = 4; // 9/16 full drive level
			}
			else // Channel #1~#13
			{
				TxAttenuationCtrl.field.PCIE_PHY_TX_ATTEN_EN = 0; // Disable PCIe PHY Tx attenuation
				TxAttenuationCtrl.field.PCIE_PHY_TX_ATTEN_VALUE = 0; // n/a
			}

			RTMP_IO_WRITE32(pAd, PCIE_PHY_TX_ATTENUATION_CTRL, TxAttenuationCtrl.word);
		}
#endif
	}
	else
	{
		ULONG	TxPinCfg = 0x00050F05;//Gary 2007/8/9 0x050505

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0);//(0x44 - GET_LNA_GAIN(pAd)));   // According the Rory's suggestion to solve the middle range issue.
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0xF2);

		// Rx High power VGA offset for LNA select
		if (pAd->NicConfig2.field.ExternalLNAForA)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x46);
		}
		else
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x50);
		}

		// 5G band selection PIN, bit1 and bit2 are complement
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x6);
		Value |= (0x02);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		// Turn off unused PA or LNA when only 1T or 1R
		if (pAd->Antenna.field.TxPath == 1)
		{
			TxPinCfg &= 0xFFFFFFF3;
		}
		if (pAd->Antenna.field.RxPath == 1)
		{
			TxPinCfg &= 0xFFFFF3FF;
		}


		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);

	}

	// R66 should be set according to Channel and use 20MHz when scanning
	//RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, (0x2E + GET_LNA_GAIN(pAd)));
	if (bScan)
		RTMPSetAGCInitValue(pAd, BW_20);
	else
		RTMPSetAGCInitValue(pAd, pAd->CommonCfg.BBPCurrentBW);

	//
	// On 11A, We should delay and wait RF/BBP to be stable
	// and the appropriate time should be 1000 micro seconds
	// 2005/06/05 - On 11G, We also need this delay time. Otherwise it's difficult to pass the WHQL.
	//
	RTMPusecDelay(1000);
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
#ifdef ANT_DIVERSITY_SUPPORT
VOID	AsicAntennaSelect(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Channel)
{
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	if (pAd->Mlme.OneSecPeriodicRound % 2 == 1)
#endif // CONFIG_STA_SUPPORT //
	{
		// patch for AsicSetRxAnt failed
		pAd->RxAnt.EvaluatePeriod = 0;

		// check every 2 second. If rcv-beacon less than 5 in the past 2 second, then AvgRSSI is no longer a
		// valid indication of the distance between this AP and its clients.
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
		{
			SHORT	realavgrssi1;

			// if no traffic then reset average rssi to trigger evaluation
#ifdef CONFIG_STA_SUPPORT
			if (pAd->StaCfg.NumOfAvgRssiSample < 5)
			{
				pAd->RxAnt.Pair1LastAvgRssi = (-99);
				pAd->RxAnt.Pair2LastAvgRssi = (-99);
				DBGPRINT(RT_DEBUG_TRACE, ("MlmePeriodicExec: no traffic/beacon, reset RSSI\n"));
			}

			pAd->StaCfg.NumOfAvgRssiSample = 0;
			realavgrssi1 = (pAd->RxAnt.Pair1AvgRssi[pAd->RxAnt.Pair1PrimaryRxAnt] >> 3);
#endif // CONFIG_STA_SUPPORT //

			DBGPRINT(RT_DEBUG_TRACE,("Ant-realrssi0(%d), Lastrssi0(%d), EvaluateStableCnt=%d\n", realavgrssi1, pAd->RxAnt.Pair1LastAvgRssi, pAd->RxAnt.EvaluateStableCnt));

			// if the difference between two rssi is larger or less than 5, then evaluate the other antenna
			if ((pAd->RxAnt.EvaluateStableCnt < 2) || (realavgrssi1 > (pAd->RxAnt.Pair1LastAvgRssi + 5)) || (realavgrssi1 < (pAd->RxAnt.Pair1LastAvgRssi - 5)))
				AsicEvaluateRxAnt(pAd);

				pAd->RxAnt.Pair1LastAvgRssi = realavgrssi1;
		}
		else
		{
			// if not connected, always switch antenna to try to connect
			UCHAR	temp;

			temp = pAd->RxAnt.Pair1PrimaryRxAnt;
			pAd->RxAnt.Pair1PrimaryRxAnt = pAd->RxAnt.Pair1SecondaryRxAnt;
			pAd->RxAnt.Pair1SecondaryRxAnt = temp;

			DBGPRINT(RT_DEBUG_TRACE, ("MlmePeriodicExec: no connect, switch to another one to try connection\n"));

			AsicSetRxAnt(pAd, pAd->RxAnt.Pair1PrimaryRxAnt);
		}
	}
}
#endif // ANT_DIVERSITY_SUPPORT //

/*
	========================================================================

	Routine Description:
		Antenna miscellaneous setting.

	Arguments:
		pAd						Pointer to our adapter
		BandState				Indicate current Band State.

	Return Value:
		None

	IRQL <= DISPATCH_LEVEL

	Note:
		1.) Frame End type control
			only valid for G only (RF_2527 & RF_2529)
			0: means DPDT, set BBP R4 bit 5 to 1
			1: means SPDT, set BBP R4 bit 5 to 0


	========================================================================
*/
VOID	AsicAntennaSetting(
	IN	PRTMP_ADAPTER	pAd,
	IN	ABGBAND_STATE	BandState)
{
}

VOID AsicRfTuningExec(
	IN PVOID SystemSpecific1,
	IN PVOID FunctionContext,
	IN PVOID SystemSpecific2,
	IN PVOID SystemSpecific3)
{
}

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
VOID AsicAdjustTxPower(
	IN PRTMP_ADAPTER pAd)
{
	INT			i, j;
	CHAR		DeltaPwr = 0;
	BOOLEAN		bAutoTxAgc = FALSE;
	UCHAR		TssiRef, *pTssiMinusBoundary, *pTssiPlusBoundary, TxAgcStep;
	UCHAR		BbpR1 = 0, BbpR49 = 0, idx;
	PCHAR		pTxAgcCompensate;
	ULONG		TxPwr[5];
	CHAR		Value;
#ifdef CONFIG_STA_SUPPORT
	CHAR		Rssi = -127;
#endif // CONFIG_STA_SUPPORT //
#ifdef CARRIER_SENSE_NEW_ALGO
	unsigned long flags; //KH Add to Fix PCIe Power-Saving bug
#endif // CARRIER_SENSE_NEW_ALGO //


#ifdef CARRIER_SENSE_NEW_ALGO
	//KH Add to Fix PCIe Power-Saving bug<--
	RTMP_INT_LOCK(&pAd->irq_lock, flags);
	//KH Add to Fix PCIe Power-Saving bug-->
#endif // CARRIER_SENSE_NEW_ALGO //

#ifdef CONFIG_STA_SUPPORT
	if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE) ||
#ifdef RTMP_MAC_PCI
		(pAd->bPCIclkOff == TRUE) ||
#endif // RTMP_MAC_PCI //
		RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF) ||
		RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BSS_SCAN_IN_PROGRESS))
{

#ifdef CARRIER_SENSE_NEW_ALGO
	//KH Add to Fix PCIe Power-Saving bug<--
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
	//KH add to fix PCIe-Power Saving -->
#endif // CARRIER_SENSE_NEW_ALGO //
		return;
}
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		Rssi = RTMPMaxRssi(pAd,
						   pAd->StaCfg.RssiSample.AvgRssi0,
						   pAd->StaCfg.RssiSample.AvgRssi1,
						   pAd->StaCfg.RssiSample.AvgRssi2);
#endif // CONFIG_STA_SUPPORT //

	if (pAd->CommonCfg.BBPCurrentBW == BW_40)
	{
		if (pAd->CommonCfg.CentralChannel > 14)
		{
			TxPwr[0] = pAd->Tx40MPwrCfgABand[0];
			TxPwr[1] = pAd->Tx40MPwrCfgABand[1];
			TxPwr[2] = pAd->Tx40MPwrCfgABand[2];
			TxPwr[3] = pAd->Tx40MPwrCfgABand[3];
			TxPwr[4] = pAd->Tx40MPwrCfgABand[4];
		}
		else
		{
			TxPwr[0] = pAd->Tx40MPwrCfgGBand[0];
			TxPwr[1] = pAd->Tx40MPwrCfgGBand[1];
			TxPwr[2] = pAd->Tx40MPwrCfgGBand[2];
			TxPwr[3] = pAd->Tx40MPwrCfgGBand[3];
			TxPwr[4] = pAd->Tx40MPwrCfgGBand[4];
		}
	}
	else
	{
		if (pAd->CommonCfg.Channel > 14)
		{
			TxPwr[0] = pAd->Tx20MPwrCfgABand[0];
			TxPwr[1] = pAd->Tx20MPwrCfgABand[1];
			TxPwr[2] = pAd->Tx20MPwrCfgABand[2];
			TxPwr[3] = pAd->Tx20MPwrCfgABand[3];
			TxPwr[4] = pAd->Tx20MPwrCfgABand[4];
		}
		else
		{
			TxPwr[0] = pAd->Tx20MPwrCfgGBand[0];
			TxPwr[1] = pAd->Tx20MPwrCfgGBand[1];
			TxPwr[2] = pAd->Tx20MPwrCfgGBand[2];
			TxPwr[3] = pAd->Tx20MPwrCfgGBand[3];
			TxPwr[4] = pAd->Tx20MPwrCfgGBand[4];
		}
	}

	// TX power compensation for temperature variation based on TSSI. try every 4 second
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
			/* BbpR1 is unsigned char */
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49);

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

			if (BbpR49 > pTssiMinusBoundary[1])
			{
				// Reading is larger than the reference value
				// check for how large we need to decrease the Tx power
				for (idx = 1; idx < 5; idx++)
				{
					if (BbpR49 <= pTssiMinusBoundary[idx])  // Found the range
						break;
				}
				// The index is the step we should decrease, idx = 0 means there is nothing to compensate
//				if (R3 > (ULONG) (TxAgcStep * (idx-1)))
					*pTxAgcCompensate = -(TxAgcStep * (idx-1));
//				else
//					*pTxAgcCompensate = -((UCHAR)R3);

				DeltaPwr += (*pTxAgcCompensate);
				DBGPRINT(RT_DEBUG_TRACE, ("-- Tx Power, BBP R1=%x, TssiRef=%x, TxAgcStep=%x, step = -%d\n",
					BbpR49, TssiRef, TxAgcStep, idx-1));
			}
			else if (BbpR49 < pTssiPlusBoundary[1])
			{
				// Reading is smaller than the reference value
				// check for how large we need to increase the Tx power
				for (idx = 1; idx < 5; idx++)
				{
					if (BbpR49 >= pTssiPlusBoundary[idx])   // Found the range
						break;
				}
				// The index is the step we should increase, idx = 0 means there is nothing to compensate
				*pTxAgcCompensate = TxAgcStep * (idx-1);
				DeltaPwr += (*pTxAgcCompensate);
				DBGPRINT(RT_DEBUG_TRACE, ("++ Tx Power, BBP R1=%x, TssiRef=%x, TxAgcStep=%x, step = +%d\n",
					BbpR49, TssiRef, TxAgcStep, idx-1));
			}
			else
			{
				*pTxAgcCompensate = 0;
				DBGPRINT(RT_DEBUG_TRACE, ("   Tx Power, BBP R49=%x, TssiRef=%x, TxAgcStep=%x, step = +%d\n",
					BbpR49, TssiRef, TxAgcStep, 0));
			}
		}
	}
	else
	{
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

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R1, &BbpR1);
	BbpR1 &= 0xFC;

#ifdef SINGLE_SKU
	// Handle regulatory max tx power constrain
	do
	{
		UCHAR    TxPwrInEEPROM = 0xFF, CountryTxPwr = 0xFF, criterion;
		UCHAR    AdjustMaxTxPwr[40];

		if (pAd->CommonCfg.Channel > 14) // 5G band
			TxPwrInEEPROM = ((pAd->CommonCfg.DefineMaxTxPwr & 0xFF00) >> 8);
		else // 2.4G band
			TxPwrInEEPROM = (pAd->CommonCfg.DefineMaxTxPwr & 0x00FF);
		CountryTxPwr = GetCuntryMaxTxPwr(pAd, pAd->CommonCfg.Channel);

		// error handling, range check
		if ((TxPwrInEEPROM > 0x50) || (CountryTxPwr > 0x50))
		{
			DBGPRINT(RT_DEBUG_ERROR,("AsicAdjustTxPower - Invalid max tx power (=0x%02x), CountryTxPwr=%d\n", TxPwrInEEPROM, CountryTxPwr));
			break;
		}

		criterion = *((PUCHAR)TxPwr + 2) & 0xF;        // FAE use OFDM 6M as criterion

		DBGPRINT_RAW(RT_DEBUG_TRACE,("AsicAdjustTxPower (criterion=%d, TxPwrInEEPROM=%d, CountryTxPwr=%d)\n", criterion, TxPwrInEEPROM, CountryTxPwr));

		// Adjust max tx power according to the relationship of tx power in E2PROM
		for (i=0; i<5; i++)
		{
			// CCK will have 4dBm larger than OFDM
			// Therefore, we should separate to parse the tx power field
			if (i == 0)
			{
				for (j=0; j<8; j++)
				{
					Value = (CHAR)((TxPwr[i] >> j*4) & 0x0F);

					if (j < 4)
					{
						// CCK will have 4dBm larger than OFDM
						AdjustMaxTxPwr[i*8+j] = TxPwrInEEPROM + (Value - criterion) + 4;
					}
					else
					{
						AdjustMaxTxPwr[i*8+j] = TxPwrInEEPROM + (Value - criterion);
					}
					DBGPRINT_RAW(RT_DEBUG_TRACE,("AsicAdjustTxPower (i/j=%d/%d, Value=%d, %d)\n", i, j, Value, AdjustMaxTxPwr[i*8+j]));
				}
			}
			else
			{
				for (j=0; j<8; j++)
				{
					Value = (CHAR)((TxPwr[i] >> j*4) & 0x0F);

					AdjustMaxTxPwr[i*8+j] = TxPwrInEEPROM + (Value - criterion);
					DBGPRINT_RAW(RT_DEBUG_TRACE,("AsicAdjustTxPower (i/j=%d/%d, Value=%d, %d)\n", i, j, Value, AdjustMaxTxPwr[i*8+j]));
				}
			}
		}

		// Adjust tx power according to the relationship
		for (i=0; i<5; i++)
		{
			if (TxPwr[i] != 0xffffffff)
			{
				for (j=0; j<8; j++)
				{
					Value = (CHAR)((TxPwr[i] >> j*4) & 0x0F);

					// The system tx power is larger than the regulatory, the power should be restrain
					if (AdjustMaxTxPwr[i*8+j] > CountryTxPwr)
					{
						// decrease to zero and don't need to take care BBPR1
						if ((Value - (AdjustMaxTxPwr[i*8+j] - CountryTxPwr)) > 0)
							Value -= (AdjustMaxTxPwr[i*8+j] - CountryTxPwr);
						else
							Value = 0;

						DBGPRINT_RAW(RT_DEBUG_TRACE,("AsicAdjustTxPower (i/j=%d/%d, Value=%d, %d)\n", i, j, Value, AdjustMaxTxPwr[i*8+j]));
					}
					else
						DBGPRINT_RAW(RT_DEBUG_TRACE,("AsicAdjustTxPower (i/j=%d/%d, Value=%d, %d, no change)\n", i, j, Value, AdjustMaxTxPwr[i*8+j]));

						TxPwr[i] = (TxPwr[i] & ~(0x0000000F << j*4)) | (Value << j*4);
				}
			}
		}
	} while (FALSE);
#endif // SINGLE_SKU //

	/* calculate delta power based on the percentage specified from UI */
	// E2PROM setting is calibrated for maximum TX power (i.e. 100%)
	// We lower TX power here according to the percentage specified from UI
	if (pAd->CommonCfg.TxPowerPercentage == 0xffffffff)       // AUTO TX POWER control
	{
#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			// to patch high power issue with some APs, like Belkin N1.
			if (Rssi > -35)
			{
				BbpR1 |= 0x02;		// DeltaPwr -= 12;
			}
			else if (Rssi > -40)
			{
				BbpR1 |= 0x01;		// DeltaPwr -= 6;
			}
			else
		;
		}
#endif // CONFIG_STA_SUPPORT //
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 90)  // 91 ~ 100% & AUTO, treat as 100% in terms of mW
		;
	else if (pAd->CommonCfg.TxPowerPercentage > 60)  // 61 ~ 90%, treat as 75% in terms of mW		// DeltaPwr -= 1;
	{
		DeltaPwr -= 1;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 30)  // 31 ~ 60%, treat as 50% in terms of mW		// DeltaPwr -= 3;
	{
		DeltaPwr -= 3;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 15)  // 16 ~ 30%, treat as 25% in terms of mW		// DeltaPwr -= 6;
	{
		BbpR1 |= 0x01;
	}
	else if (pAd->CommonCfg.TxPowerPercentage > 9)   // 10 ~ 15%, treat as 12.5% in terms of mW		// DeltaPwr -= 9;
	{
		BbpR1 |= 0x01;
		DeltaPwr -= 3;
	}
	else                                           // 0 ~ 9 %, treat as MIN(~3%) in terms of mW		// DeltaPwr -= 12;
	{
		BbpR1 |= 0x02;
	}

	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BbpR1);

	/* reset different new tx power for different TX rate */
	for(i=0; i<5; i++)
	{
		if (TxPwr[i] != 0xffffffff)
		{
			for (j=0; j<8; j++)
			{
				Value = (CHAR)((TxPwr[i] >> j*4) & 0x0F); /* 0 ~ 15 */

				if ((Value + DeltaPwr) < 0)
				{
					Value = 0; /* min */
				}
				else if ((Value + DeltaPwr) > 0xF)
				{
					Value = 0xF; /* max */
				}
				else
				{
					Value += DeltaPwr; /* temperature compensation */
				}

				/* fill new value to CSR offset */
				TxPwr[i] = (TxPwr[i] & ~(0x0000000F << j*4)) | (Value << j*4);
			}

			/* write tx power value to CSR */
			/* TX_PWR_CFG_0 (8 tx rate) for	TX power for OFDM 12M/18M
											TX power for OFDM 6M/9M
											TX power for CCK5.5M/11M
											TX power for CCK1M/2M */
			/* TX_PWR_CFG_1 ~ TX_PWR_CFG_4 */
			RTMP_IO_WRITE32(pAd, TX_PWR_CFG_0 + i*4, TxPwr[i]);
		}
	}

#ifdef CARRIER_SENSE_NEW_ALGO
	//KH Add to Fix PCIe Power-Saving bug<--
	RTMP_INT_UNLOCK(&pAd->irq_lock, flags);
	//KH add to fix PCIe-Power Saving -->
#endif // CARRIER_SENSE_NEW_ALGO //

}


#ifdef CONFIG_STA_SUPPORT
VOID AsicResetBBPAgent(
IN PRTMP_ADAPTER pAd)
{
	BBP_CSR_CFG_STRUC	BbpCsr;
	DBGPRINT(RT_DEBUG_ERROR, ("Reset BBP Agent busy bit.!! \n"));
	// Still need to find why BBP agent keeps busy, but in fact, hardware still function ok. Now clear busy first.
	RTMP_IO_READ32(pAd, H2M_BBP_AGENT, &BbpCsr.word);
	BbpCsr.field.Busy = 0;
	RTMP_IO_WRITE32(pAd, H2M_BBP_AGENT, BbpCsr.word);
}
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
#endif // CONFIG_STA_SUPPORT //


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
	// always one BSSID in STA mode
	Addr4 = (ULONG)(pBssid[4]) | (ULONG)(pBssid[5] << 8);

	RTMP_IO_WRITE32(pAd, MAC_BSSID_DW1, Addr4);
}

VOID AsicSetMcastWC(
	IN PRTMP_ADAPTER pAd)
{
	MAC_TABLE_ENTRY *pEntry = &pAd->MacTab.Content[MCAST_WCID];
	USHORT		offset;

	pEntry->Sst        = SST_ASSOC;
	pEntry->Aid        = MCAST_WCID;	// Softap supports 1 BSSID and use WCID=0 as multicast Wcid index
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
	ULONG		offset;

	DBGPRINT(RT_DEBUG_TRACE, ("AsicDelWcidTab==>Wcid = 0x%x\n",Wcid));
	offset = MAC_WCID_BASE + Wcid * HW_WCID_ENTRY_SIZE;
	RTMP_IO_WRITE32(pAd, offset, Addr0);
	offset += 4;
	RTMP_IO_WRITE32(pAd, offset, Addr1);
}

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

	//OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_AGGREGATION_INUSED);
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
	//Data  |= 0x20;
#ifndef WIFI_TEST
	//if ( pAd->CommonCfg.bEnableTxBurst )
	//	Data |= 0x60; // for performance issue not set the TXOP to 0
#endif
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_DYNAMIC_BE_TXOP_ACTIVE)
#ifdef DOT11_N_SUPPORT
		&& (pAd->MacTab.fAnyStationMIMOPSDynamic == FALSE)
#endif // DOT11_N_SUPPORT //
	)
	{
		// For CWC test, change txop from 0x30 to 0x20 in TxBurst mode
		if (pAd->CommonCfg.bEnableTxBurst)
		Data |= 0x20;
	}
	RTMP_IO_WRITE32(pAd, EDCA_AC0_CFG, Data);
}

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

	// 2003-12-20 disable TSF and TBTT while NIC in power-saving have side effect
	//			  that NIC will never wakes up because TSF stops and no more
	//			  TBTT interrupts
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
//	RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, 0x00000000);
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		csr.field.BeaconInterval = pAd->CommonCfg.BeaconPeriod << 4; // ASIC register in units of 1/16 TU
		csr.field.bTsfTicking = 1;
		csr.field.TsfSyncMode = 1; // sync TSF in INFRASTRUCTURE mode
		csr.field.bBeaconGen  = 0; // do NOT generate BEACON
		csr.field.bTBTTEnable = 1;
	}
#endif // CONFIG_STA_SUPPORT //
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

	DBGPRINT(RT_DEBUG_TRACE, ("--->AsicEnableIbssSync(ADHOC mode. MPDUtotalByteCount = %d)\n", pAd->BeaconTxWI.MPDUtotalByteCount));

	RTMP_IO_READ32(pAd, BCN_TIME_CFG, &csr9.word);
	csr9.field.bBeaconGen = 0;
	csr9.field.bTBTTEnable = 0;
	csr9.field.bTsfTicking = 0;
	RTMP_IO_WRITE32(pAd, BCN_TIME_CFG, csr9.word);

#ifdef RTMP_MAC_PCI
	// move BEACON TXD and frame content to on-chip memory
	ptr = (PUCHAR)&pAd->BeaconTxWI;
	for (i=0; i<TXWI_SIZE; i+=4)  // 16-byte TXWI field
	{
		UINT32 longptr =  *ptr + (*(ptr+1)<<8) + (*(ptr+2)<<16) + (*(ptr+3)<<24);
		RTMP_IO_WRITE32(pAd, HW_BEACON_BASE0 + i, longptr);
		ptr += 4;
	}

	// start right after the 16-byte TXWI field
	ptr = pAd->BeaconBuf;
	for (i=0; i< pAd->BeaconTxWI.MPDUtotalByteCount; i+=4)
	{
		UINT32 longptr =  *ptr + (*(ptr+1)<<8) + (*(ptr+2)<<16) + (*(ptr+3)<<24);
		RTMP_IO_WRITE32(pAd, HW_BEACON_BASE0 + TXWI_SIZE + i, longptr);
		ptr +=4;
	}
#endif // RTMP_MAC_PCI //


	//
	// For Wi-Fi faily generated beacons between participating stations.
	// Set TBTT phase adaptive adjustment step to 8us (default 16us)
	// don't change settings 2006-5- by Jerry
	//RTMP_IO_WRITE32(pAd, TBTT_SYNC_CFG, 0x00001010);

	// start sending BEACON
	csr9.field.BeaconInterval = pAd->CommonCfg.BeaconPeriod << 4; // ASIC register in units of 1/16 TU
	csr9.field.bTsfTicking = 1;
	csr9.field.TsfSyncMode = 2; // sync TSF in IBSS mode
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
			if (pAd->MacTab.Content[i].ValidAsCLI || pAd->MacTab.Content[i].ValidAsApCli)
				CLIENT_STATUS_CLEAR_FLAG(&pAd->MacTab.Content[i], fCLIENT_STATUS_WMM_CAPABLE);
		}

		//========================================================
		//      MAC Register has a copy .
		//========================================================
//#ifndef WIFI_TEST
		if( pAd->CommonCfg.bEnableTxBurst )
		{
			// For CWC test, change txop from 0x30 to 0x20 in TxBurst mode
			Ac0Cfg.field.AcTxop = 0x20; // Suggest by John for TxBurst in HT Mode
		}
		else
			Ac0Cfg.field.AcTxop = 0;	// QID_AC_BE
//#else
//		Ac0Cfg.field.AcTxop = 0;	// QID_AC_BE
//#endif
		Ac0Cfg.field.Cwmin = CW_MIN_IN_BITS;
		Ac0Cfg.field.Cwmax = CW_MAX_IN_BITS;
		Ac0Cfg.field.Aifsn = 2;
		RTMP_IO_WRITE32(pAd, EDCA_AC0_CFG, Ac0Cfg.word);

		Ac1Cfg.field.AcTxop = 0;	// QID_AC_BK
		Ac1Cfg.field.Cwmin = CW_MIN_IN_BITS;
		Ac1Cfg.field.Cwmax = CW_MAX_IN_BITS;
		Ac1Cfg.field.Aifsn = 2;
		RTMP_IO_WRITE32(pAd, EDCA_AC1_CFG, Ac1Cfg.word);

		if (pAd->CommonCfg.PhyMode == PHY_11B)
		{
			Ac2Cfg.field.AcTxop = 192;	// AC_VI: 192*32us ~= 6ms
			Ac3Cfg.field.AcTxop = 96;	// AC_VO: 96*32us  ~= 3ms
		}
		else
		{
			Ac2Cfg.field.AcTxop = 96;	// AC_VI: 96*32us ~= 3ms
			Ac3Cfg.field.AcTxop = 48;	// AC_VO: 48*32us ~= 1.5ms
		}
		Ac2Cfg.field.Cwmin = CW_MIN_IN_BITS;
		Ac2Cfg.field.Cwmax = CW_MAX_IN_BITS;
		Ac2Cfg.field.Aifsn = 2;
		RTMP_IO_WRITE32(pAd, EDCA_AC2_CFG, Ac2Cfg.word);
		Ac3Cfg.field.Cwmin = CW_MIN_IN_BITS;
		Ac3Cfg.field.Cwmax = CW_MAX_IN_BITS;
		Ac3Cfg.field.Aifsn = 2;
		RTMP_IO_WRITE32(pAd, EDCA_AC3_CFG, Ac3Cfg.word);

		//========================================================
		//      DMA Register has a copy too.
		//========================================================
		csr0.field.Ac0Txop = 0;		// QID_AC_BE
		csr0.field.Ac1Txop = 0;		// QID_AC_BK
		RTMP_IO_WRITE32(pAd, WMM_TXOP0_CFG, csr0.word);
		if (pAd->CommonCfg.PhyMode == PHY_11B)
		{
			csr1.field.Ac2Txop = 192;		// AC_VI: 192*32us ~= 6ms
			csr1.field.Ac3Txop = 96;		// AC_VO: 96*32us  ~= 3ms
		}
		else
		{
			csr1.field.Ac2Txop = 96;		// AC_VI: 96*32us ~= 3ms
			csr1.field.Ac3Txop = 48;		// AC_VO: 48*32us ~= 1.5ms
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
		//========================================================
		//      MAC Register has a copy.
		//========================================================
		//
		// Modify Cwmin/Cwmax/Txop on queue[QID_AC_VI], Recommend by Jerry 2005/07/27
		// To degrade our VIDO Queue's throughput for WiFi WMM S3T07 Issue.
		//
		//pEdcaParm->Txop[QID_AC_VI] = pEdcaParm->Txop[QID_AC_VI] * 7 / 10; // rt2860c need this

		Ac0Cfg.field.AcTxop =  pEdcaParm->Txop[QID_AC_BE];
		Ac0Cfg.field.Cwmin= pEdcaParm->Cwmin[QID_AC_BE];
		Ac0Cfg.field.Cwmax = pEdcaParm->Cwmax[QID_AC_BE];
		Ac0Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_BE]; //+1;

		Ac1Cfg.field.AcTxop =  pEdcaParm->Txop[QID_AC_BK];
		Ac1Cfg.field.Cwmin = pEdcaParm->Cwmin[QID_AC_BK]; //+2;
		Ac1Cfg.field.Cwmax = pEdcaParm->Cwmax[QID_AC_BK];
		Ac1Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_BK]; //+1;

		Ac2Cfg.field.AcTxop = (pEdcaParm->Txop[QID_AC_VI] * 6) / 10;
		if(pAd->Antenna.field.TxPath == 1)
		{
			Ac2Cfg.field.Cwmin = pEdcaParm->Cwmin[QID_AC_VI] + 1;
			Ac2Cfg.field.Cwmax = pEdcaParm->Cwmax[QID_AC_VI] + 1;
		}
		else
		{
		Ac2Cfg.field.Cwmin = pEdcaParm->Cwmin[QID_AC_VI];
		Ac2Cfg.field.Cwmax = pEdcaParm->Cwmax[QID_AC_VI];
		}
		Ac2Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_VI] + 1;
#ifdef CONFIG_STA_SUPPORT
#endif // CONFIG_STA_SUPPORT //

#ifdef INF_AMAZON_SE
#endif // INF_AMAZON_SE //

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			// Tuning for Wi-Fi WMM S06
			if (pAd->CommonCfg.bWiFiTest &&
				pEdcaParm->Aifsn[QID_AC_VI] == 10)
				Ac2Cfg.field.Aifsn -= 1;

			// Tuning for TGn Wi-Fi 5.2.32
			// STA TestBed changes in this item: conexant legacy sta ==> broadcom 11n sta
			if (STA_TGN_WIFI_ON(pAd) &&
				pEdcaParm->Aifsn[QID_AC_VI] == 10)
			{
				Ac0Cfg.field.Aifsn = 3;
				Ac2Cfg.field.AcTxop = 5;
			}

#ifdef RT30xx
			if (pAd->RfIcType == RFIC_3020 || pAd->RfIcType == RFIC_2020)
			{
				// Tuning for WiFi WMM S3-T07: connexant legacy sta ==> broadcom 11n sta.
				Ac2Cfg.field.Aifsn = 5;
			}
#endif // RT30xx //
		}
#endif // CONFIG_STA_SUPPORT //

		Ac3Cfg.field.AcTxop = pEdcaParm->Txop[QID_AC_VO];
		Ac3Cfg.field.Cwmin = pEdcaParm->Cwmin[QID_AC_VO];
		Ac3Cfg.field.Cwmax = pEdcaParm->Cwmax[QID_AC_VO];
		Ac3Cfg.field.Aifsn = pEdcaParm->Aifsn[QID_AC_VO];

//#ifdef WIFI_TEST
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
//#endif // WIFI_TEST //

		RTMP_IO_WRITE32(pAd, EDCA_AC0_CFG, Ac0Cfg.word);
		RTMP_IO_WRITE32(pAd, EDCA_AC1_CFG, Ac1Cfg.word);
		RTMP_IO_WRITE32(pAd, EDCA_AC2_CFG, Ac2Cfg.word);
		RTMP_IO_WRITE32(pAd, EDCA_AC3_CFG, Ac3Cfg.word);


		//========================================================
		//      DMA Register has a copy too.
		//========================================================
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
			CwminCsr.field.Cwmin3 = pEdcaParm->Cwmin[QID_AC_VO] - 1; //for TGn wifi test
#endif // CONFIG_STA_SUPPORT //
		RTMP_IO_WRITE32(pAd, WMM_CWMIN_CFG, CwminCsr.word);

		CwmaxCsr.word = 0;
		CwmaxCsr.field.Cwmax0 = pEdcaParm->Cwmax[QID_AC_BE];
		CwmaxCsr.field.Cwmax1 = pEdcaParm->Cwmax[QID_AC_BK];
		CwmaxCsr.field.Cwmax2 = pEdcaParm->Cwmax[QID_AC_VI];
		CwmaxCsr.field.Cwmax3 = pEdcaParm->Cwmax[QID_AC_VO];
		RTMP_IO_WRITE32(pAd, WMM_CWMAX_CFG, CwmaxCsr.word);

		AifsnCsr.word = 0;
		AifsnCsr.field.Aifsn0 = Ac0Cfg.field.Aifsn; //pEdcaParm->Aifsn[QID_AC_BE];
		AifsnCsr.field.Aifsn1 = Ac1Cfg.field.Aifsn; //pEdcaParm->Aifsn[QID_AC_BK];
		AifsnCsr.field.Aifsn2 = Ac2Cfg.field.Aifsn; //pEdcaParm->Aifsn[QID_AC_VI];
#ifdef INF_AMAZON_SE
#endif // INF_AMAZON_SE //

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			// Tuning for Wi-Fi WMM S06
			if (pAd->CommonCfg.bWiFiTest &&
				pEdcaParm->Aifsn[QID_AC_VI] == 10)
				AifsnCsr.field.Aifsn2 = Ac2Cfg.field.Aifsn - 4;

			// Tuning for TGn Wi-Fi 5.2.32
			// STA TestBed changes in this item: connexant legacy sta ==> broadcom 11n sta
			if (STA_TGN_WIFI_ON(pAd) &&
				pEdcaParm->Aifsn[QID_AC_VI] == 10)
			{
				AifsnCsr.field.Aifsn0 = 3;
				AifsnCsr.field.Aifsn2 = 7;
			}

			if (INFRA_ON(pAd))
				CLIENT_STATUS_SET_FLAG(&pAd->MacTab.Content[BSSID_WCID], fCLIENT_STATUS_WMM_CAPABLE);
		}
#endif // CONFIG_STA_SUPPORT //

#ifdef CONFIG_STA_SUPPORT
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
			AifsnCsr.field.Aifsn3 = Ac3Cfg.field.Aifsn - 1; //pEdcaParm->Aifsn[QID_AC_VO]; //for TGn wifi test
#ifdef RT30xx
			// TODO: Shiang, this modification also suitable for RT3052/RT3050 ???
			if (pAd->RfIcType == RFIC_3020 || pAd->RfIcType == RFIC_2020)
			{
				IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
					AifsnCsr.field.Aifsn2 = 0x2; //pEdcaParm->Aifsn[QID_AC_VI]; //for WiFi WMM S4-T04.
			}
#endif // RT30xx //
		}
#endif // CONFIG_STA_SUPPORT //
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
VOID	AsicSetSlotTime(
	IN PRTMP_ADAPTER pAd,
	IN BOOLEAN bUseShortSlotTime)
{
	ULONG	SlotTime;
	UINT32	RegValue = 0;

#ifdef CONFIG_STA_SUPPORT
	if (pAd->CommonCfg.Channel > 14)
		bUseShortSlotTime = TRUE;
#endif // CONFIG_STA_SUPPORT //

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
		// force using short SLOT time for FAE to demo performance when TxBurst is ON
		if (((pAd->StaActive.SupportedPhyInfo.bHtEnable == FALSE) && (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_WMM_INUSED)))
#ifdef DOT11_N_SUPPORT
			|| ((pAd->StaActive.SupportedPhyInfo.bHtEnable == TRUE) && (pAd->CommonCfg.BACapability.field.Policy == BA_NOTUSE))
#endif // DOT11_N_SUPPORT //
			)
		{
			// In this case, we will think it is doing Wi-Fi test
			// And we will not set to short slot when bEnableTxBurst is TRUE.
		}
		else if (pAd->CommonCfg.bEnableTxBurst)
		{
			OPSTATUS_SET_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED);
			SlotTime = 9;
		}
	}
#endif // CONFIG_STA_SUPPORT //

	//
	// For some reasons, always set it to short slot time.
	//
	// ToDo: Should consider capability with 11B
	//
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if (pAd->StaCfg.BssType == BSS_ADHOC)
		{
			OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_SLOT_INUSED);
			SlotTime = 20;
		}
	}
#endif // CONFIG_STA_SUPPORT //

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
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		 BssIndex,
	IN UCHAR		 KeyIdx,
	IN UCHAR		 CipherAlg,
	IN PUCHAR		 pKey,
	IN PUCHAR		 pTxMic,
	IN PUCHAR		 pRxMic)
{
	ULONG offset; //, csr0;
	SHAREDKEY_MODE_STRUC csr1;
#ifdef RTMP_MAC_PCI
	INT   i;
#endif // RTMP_MAC_PCI //

	DBGPRINT(RT_DEBUG_TRACE, ("AsicAddSharedKeyEntry BssIndex=%d, KeyIdx=%d\n", BssIndex,KeyIdx));
//============================================================================================

	DBGPRINT(RT_DEBUG_TRACE,("AsicAddSharedKeyEntry: %s key #%d\n", CipherName[CipherAlg], BssIndex*4 + KeyIdx));
	DBGPRINT_RAW(RT_DEBUG_TRACE, ("		Key = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		pKey[0],pKey[1],pKey[2],pKey[3],pKey[4],pKey[5],pKey[6],pKey[7],pKey[8],pKey[9],pKey[10],pKey[11],pKey[12],pKey[13],pKey[14],pKey[15]));
	if (pRxMic)
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("		Rx MIC Key = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			pRxMic[0],pRxMic[1],pRxMic[2],pRxMic[3],pRxMic[4],pRxMic[5],pRxMic[6],pRxMic[7]));
	}
	if (pTxMic)
	{
		DBGPRINT_RAW(RT_DEBUG_TRACE, ("		Tx MIC Key = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			pTxMic[0],pTxMic[1],pTxMic[2],pTxMic[3],pTxMic[4],pTxMic[5],pTxMic[6],pTxMic[7]));
	}
//============================================================================================
	//
	// fill key material - key + TX MIC + RX MIC
	//
#ifdef RTMP_MAC_PCI
	offset = SHARED_KEY_TABLE_BASE + (4*BssIndex + KeyIdx)*HW_KEY_ENTRY_SIZE;
	for (i=0; i<MAX_LEN_OF_SHARE_KEY; i++)
	{
		RTMP_IO_WRITE8(pAd, offset + i, pKey[i]);
	}

	offset += MAX_LEN_OF_SHARE_KEY;
	if (pTxMic)
	{
		for (i=0; i<8; i++)
		{
			RTMP_IO_WRITE8(pAd, offset + i, pTxMic[i]);
		}
	}

	offset += 8;
	if (pRxMic)
	{
		for (i=0; i<8; i++)
		{
			RTMP_IO_WRITE8(pAd, offset + i, pRxMic[i]);
		}
	}
#endif // RTMP_MAC_PCI //


	//
	// Update cipher algorithm. WSTA always use BSS0
	//
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

//	IRQL = DISPATCH_LEVEL
VOID AsicRemoveSharedKeyEntry(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		 BssIndex,
	IN UCHAR		 KeyIdx)
{
	//ULONG SecCsr0;
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


VOID AsicUpdateWCIDAttribute(
	IN PRTMP_ADAPTER pAd,
	IN USHORT		WCID,
	IN UCHAR		BssIndex,
	IN UCHAR        CipherAlg,
	IN BOOLEAN		bUsePairewiseKeyTable)
{
	ULONG   WCIDAttri = 0, offset;

	//
	// Update WCID attribute.
	// Only TxKey could update WCID attribute.
	//
	offset = MAC_WCID_ATTRIBUTE_BASE + (WCID * HW_WCID_ATTRI_SIZE);
	WCIDAttri = (BssIndex << 4) | (CipherAlg << 1) | (bUsePairewiseKeyTable);
	RTMP_IO_WRITE32(pAd, offset, WCIDAttri);
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

    Routine Description:
        Set Cipher Key, Cipher algorithm, IV/EIV to Asic

    Arguments:
        pAd                     Pointer to our adapter
        WCID                    WCID Entry number.
        BssIndex                BSSID index, station or none multiple BSSID support
                                this value should be 0.
        KeyIdx                  This KeyIdx will set to IV's KeyID if bTxKey enabled
        pCipherKey              Pointer to Cipher Key.
        bUsePairewiseKeyTable   TRUE means saved the key in SharedKey table,
                                otherwise PairewiseKey table
        bTxKey                  This is the transmit key if enabled.

    Return Value:
        None

    Note:
        This routine will set the relative key stuff to Asic including WCID attribute,
        Cipher Key, Cipher algorithm and IV/EIV.

        IV/EIV will be update if this CipherKey is the transmission key because
        ASIC will base on IV's KeyID value to select Cipher Key.

        If bTxKey sets to FALSE, this is not the TX key, but it could be
        RX key

	For AP mode bTxKey must be always set to TRUE.
    ========================================================================
*/
VOID AsicAddKeyEntry(
	IN PRTMP_ADAPTER pAd,
	IN USHORT		WCID,
	IN UCHAR		BssIndex,
	IN UCHAR		KeyIdx,
	IN PCIPHER_KEY	pCipherKey,
	IN BOOLEAN		bUsePairewiseKeyTable,
	IN BOOLEAN		bTxKey)
{
	ULONG	offset;
//	ULONG   WCIDAttri = 0;
	UCHAR	IV4 = 0;
	PUCHAR		pKey = pCipherKey->Key;
//	ULONG		KeyLen = pCipherKey->KeyLen;
	PUCHAR		pTxMic = pCipherKey->TxMic;
	PUCHAR		pRxMic = pCipherKey->RxMic;
	PUCHAR		pTxtsc = pCipherKey->TxTsc;
	UCHAR		CipherAlg = pCipherKey->CipherAlg;
	SHAREDKEY_MODE_STRUC csr1;
#ifdef RTMP_MAC_PCI
	UCHAR		i;
#endif // RTMP_MAC_PCI //

//	ASSERT(KeyLen <= MAX_LEN_OF_PEER_KEY);

	DBGPRINT(RT_DEBUG_TRACE, ("==> AsicAddKeyEntry\n"));
	//
	// 1.) decide key table offset
	//
	if (bUsePairewiseKeyTable)
		offset = PAIRWISE_KEY_TABLE_BASE + (WCID * HW_KEY_ENTRY_SIZE);
	else
		offset = SHARED_KEY_TABLE_BASE + (4 * BssIndex + KeyIdx) * HW_KEY_ENTRY_SIZE;

	//
	// 2.) Set Key to Asic
	//
	//for (i = 0; i < KeyLen; i++)
#ifdef RTMP_MAC_PCI
	for (i = 0; i < MAX_LEN_OF_PEER_KEY; i++)
	{
		RTMP_IO_WRITE8(pAd, offset + i, pKey[i]);
	}
	offset += MAX_LEN_OF_PEER_KEY;

	//
	// 3.) Set MIC key if available
	//
	if (pTxMic)
	{
		for (i = 0; i < 8; i++)
		{
			RTMP_IO_WRITE8(pAd, offset + i, pTxMic[i]);
		}
	}
	offset += LEN_TKIP_TXMICK;

	if (pRxMic)
	{
		for (i = 0; i < 8; i++)
		{
			RTMP_IO_WRITE8(pAd, offset + i, pRxMic[i]);
		}
	}
#endif // RTMP_MAC_PCI //


	//
	// 4.) Modify IV/EIV if needs
	//     This will force Asic to use this key ID by setting IV.
	//
	if (bTxKey)
	{
#ifdef RTMP_MAC_PCI
		offset = MAC_IVEIV_TABLE_BASE + (WCID * HW_IVEIV_ENTRY_SIZE);
		//
		// Write IV
		//
		RTMP_IO_WRITE8(pAd, offset, pTxtsc[1]);
		RTMP_IO_WRITE8(pAd, offset + 1, ((pTxtsc[1] | 0x20) & 0x7f));
		RTMP_IO_WRITE8(pAd, offset + 2, pTxtsc[0]);

		IV4 = (KeyIdx << 6);
		if ((CipherAlg == CIPHER_TKIP) || (CipherAlg == CIPHER_TKIP_NO_MIC) ||(CipherAlg == CIPHER_AES))
			IV4 |= 0x20;  // turn on extension bit means EIV existence

		RTMP_IO_WRITE8(pAd, offset + 3, IV4);

		//
		// Write EIV
		//
		offset += 4;
		for (i = 0; i < 4; i++)
		{
			RTMP_IO_WRITE8(pAd, offset + i, pTxtsc[i + 2]);
		}
#endif // RTMP_MAC_PCI //

		AsicUpdateWCIDAttribute(pAd, WCID, BssIndex, CipherAlg, bUsePairewiseKeyTable);
	}

	if (!bUsePairewiseKeyTable)
	{
		//
		// Only update the shared key security mode
		//
		RTMP_IO_READ32(pAd, SHARED_KEY_MODE_BASE + 4 * (BssIndex / 2), &csr1.word);
		if ((BssIndex % 2) == 0)
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
		RTMP_IO_WRITE32(pAd, SHARED_KEY_MODE_BASE + 4 * (BssIndex / 2), csr1.word);
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<== AsicAddKeyEntry\n"));
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
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR        pAddr,
	IN UCHAR		WCID,
	IN CIPHER_KEY		 *pCipherKey)
{
	INT i;
	ULONG		offset;
	PUCHAR		 pKey = pCipherKey->Key;
	PUCHAR		 pTxMic = pCipherKey->TxMic;
	PUCHAR		 pRxMic = pCipherKey->RxMic;
#ifdef DBG
	UCHAR		CipherAlg = pCipherKey->CipherAlg;
#endif // DBG //

	// EKEY
	offset = PAIRWISE_KEY_TABLE_BASE + (WCID * HW_KEY_ENTRY_SIZE);
#ifdef RTMP_MAC_PCI
	for (i=0; i<MAX_LEN_OF_PEER_KEY; i++)
	{
		RTMP_IO_WRITE8(pAd, offset + i, pKey[i]);
	}
#endif // RTMP_MAC_PCI //
	for (i=0; i<MAX_LEN_OF_PEER_KEY; i+=4)
	{
		UINT32 Value;
		RTMP_IO_READ32(pAd, offset + i, &Value);
	}

	offset += MAX_LEN_OF_PEER_KEY;

	//  MIC KEY
	if (pTxMic)
	{
#ifdef RTMP_MAC_PCI
		for (i=0; i<8; i++)
		{
			RTMP_IO_WRITE8(pAd, offset+i, pTxMic[i]);
		}
#endif // RTMP_MAC_PCI //
	}
	offset += 8;
	if (pRxMic)
	{
#ifdef RTMP_MAC_PCI
		for (i=0; i<8; i++)
		{
			RTMP_IO_WRITE8(pAd, offset+i, pRxMic[i]);
		}
#endif // RTMP_MAC_PCI //
	}

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
	IN UCHAR		 BssIdx,
	IN UCHAR		 Wcid)
{
	ULONG		WCIDAttri;
	USHORT		offset;

	// re-set the entry's WCID attribute as OPEN-NONE.
	offset = MAC_WCID_ATTRIBUTE_BASE + (Wcid * HW_WCID_ATTRI_SIZE);
	WCIDAttri = (BssIdx<<4) | PAIRWISEKEYTABLE;
	RTMP_IO_WRITE32(pAd, offset, WCIDAttri);
}

BOOLEAN AsicSendCommandToMcu(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR		 Command,
	IN UCHAR		 Token,
	IN UCHAR		 Arg0,
	IN UCHAR		 Arg1)
{


	if (pAd->chipOps.sendCommandToMcu)
		pAd->chipOps.sendCommandToMcu(pAd, Command, Token, Arg0, Arg1);

	return TRUE;
}


VOID AsicSetRxAnt(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			Ant)
{
#ifdef RT33xx
	RT33xxSetRxAnt(pAd, Ant);
#else
#ifdef RT30xx
	/* RT3572 ATE need not to do this. */
	RT30xxSetRxAnt(pAd, Ant);
#endif // RT30xx //
#endif // RT33xx //
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
		// RF R2 bit 18 = 0
		UINT32			R1 = 0, R2 = 0, R3 = 0;
		UCHAR			index;
		RTMP_RF_REGS	*RFRegTable;

		RFRegTable = RF2850RegTable;

		switch (pAd->RfIcType)
		{
			case RFIC_2820:
			case RFIC_2850:
			case RFIC_2720:
			case RFIC_2750:

				for (index = 0; index < NUM_OF_2850_CHNL; index++)
				{
					if (Channel == RFRegTable[index].Channel)
					{
						R1 = RFRegTable[index].R1 & 0xffffdfff;
						R2 = RFRegTable[index].R2 & 0xfffbffff;
						R3 = RFRegTable[index].R3 & 0xfff3ffff;

						RTMP_RF_IO_WRITE32(pAd, R1);
						RTMP_RF_IO_WRITE32(pAd, R2);

						// Program R1b13 to 1, R3/b18,19 to 0, R2b18 to 0.
						// Set RF R2 bit18=0, R3 bit[18:19]=0
						//if (pAd->StaCfg.bRadio == FALSE)
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

			default:
				break;
		}
	}
}


VOID AsicTurnOnRFClk(
	IN PRTMP_ADAPTER pAd,
	IN	UCHAR			Channel)
{
	// RF R2 bit 18 = 0
	UINT32			R1 = 0, R2 = 0, R3 = 0;
	UCHAR			index;
	RTMP_RF_REGS	*RFRegTable;

#ifdef PCIE_PS_SUPPORT
	// The RF programming sequence is difference between 3xxx and 2xxx
	if ((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd)))
	{
		return;
	}
#endif // PCIE_PS_SUPPORT //

	RFRegTable = RF2850RegTable;

	switch (pAd->RfIcType)
	{
		case RFIC_2820:
		case RFIC_2850:
		case RFIC_2720:
		case RFIC_2750:

			for (index = 0; index < NUM_OF_2850_CHNL; index++)
			{
				if (Channel == RFRegTable[index].Channel)
				{
					R3 = pAd->LatchRfRegs.R3;
					R3 &= 0xfff3ffff;
					R3 |= 0x00080000;
					RTMP_RF_IO_WRITE32(pAd, R3);

					R1 = RFRegTable[index].R1;
					RTMP_RF_IO_WRITE32(pAd, R1);

					R2 = RFRegTable[index].R2;
					if (pAd->Antenna.field.TxPath == 1)
					{
						R2 |= 0x4000;	// If TXpath is 1, bit 14 = 1;
					}

					if (pAd->Antenna.field.RxPath == 2)
					{
						R2 |= 0x40;	// write 1 to off Rxpath.
					}
					else if (pAd->Antenna.field.RxPath == 1)
					{
						R2 |= 0x20040;	// write 1 to off RxPath
					}
					RTMP_RF_IO_WRITE32(pAd, R2);

					break;
				}
			}
			break;

		default:
			break;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("AsicTurnOnRFClk#%d(RF=%d, ) , R2=0x%08x\n",
		Channel,
		pAd->RfIcType,
		R2));
}
