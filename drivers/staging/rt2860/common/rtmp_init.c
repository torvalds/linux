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
	rtmp_init.c

	Abstract:
	Miniport generic portion header file

	Revision History:
	Who         When          What
	--------    ----------    ----------------------------------------------
*/
#include "../rt_config.h"

u8 BIT8[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
char *CipherName[] =
    { "none", "wep64", "wep128", "TKIP", "AES", "CKIP64", "CKIP128" };

/* */
/* BBP register initialization set */
/* */
struct rt_reg_pair BBPRegTable[] = {
	{BBP_R65, 0x2C},	/* fix rssi issue */
	{BBP_R66, 0x38},	/* Also set this default value to pAd->BbpTuning.R66CurrentValue at initial */
	{BBP_R69, 0x12},
	{BBP_R70, 0xa},		/* BBP_R70 will change to 0x8 in ApStartUp and LinkUp for rt2860C, otherwise value is 0xa */
	{BBP_R73, 0x10},
	{BBP_R81, 0x37},
	{BBP_R82, 0x62},
	{BBP_R83, 0x6A},
	{BBP_R84, 0x99},	/* 0x19 is for rt2860E and after. This is for extension channel overlapping IOT. 0x99 is for rt2860D and before */
	{BBP_R86, 0x00},	/* middle range issue, Rory @2008-01-28 */
	{BBP_R91, 0x04},	/* middle range issue, Rory @2008-01-28 */
	{BBP_R92, 0x00},	/* middle range issue, Rory @2008-01-28 */
	{BBP_R103, 0x00},	/* near range high-power issue, requested from Gary @2008-0528 */
	{BBP_R105, 0x05},	/* 0x05 is for rt2860E to turn on FEQ control. It is safe for rt2860D and before, because Bit 7:2 are reserved in rt2860D and before. */
	{BBP_R106, 0x35},	/* for ShortGI throughput */
};

#define	NUM_BBP_REG_PARMS	(sizeof(BBPRegTable) / sizeof(struct rt_reg_pair))

/* */
/* ASIC register initialization sets */
/* */

struct rt_rtmp_reg_pair MACRegTable[] = {
#if defined(HW_BEACON_OFFSET) && (HW_BEACON_OFFSET == 0x200)
	{BCN_OFFSET0, 0xf8f0e8e0},	/* 0x3800(e0), 0x3A00(e8), 0x3C00(f0), 0x3E00(f8), 512B for each beacon */
	{BCN_OFFSET1, 0x6f77d0c8},	/* 0x3200(c8), 0x3400(d0), 0x1DC0(77), 0x1BC0(6f), 512B for each beacon */
#elif defined(HW_BEACON_OFFSET) && (HW_BEACON_OFFSET == 0x100)
	{BCN_OFFSET0, 0xece8e4e0},	/* 0x3800, 0x3A00, 0x3C00, 0x3E00, 512B for each beacon */
	{BCN_OFFSET1, 0xfcf8f4f0},	/* 0x3800, 0x3A00, 0x3C00, 0x3E00, 512B for each beacon */
#else
#error You must re-calculate new value for BCN_OFFSET0 & BCN_OFFSET1 in MACRegTable[]!
#endif /* HW_BEACON_OFFSET // */

	{LEGACY_BASIC_RATE, 0x0000013f},	/*  Basic rate set bitmap */
	{HT_BASIC_RATE, 0x00008003},	/* Basic HT rate set , 20M, MCS=3, MM. Format is the same as in TXWI. */
	{MAC_SYS_CTRL, 0x00},	/* 0x1004, , default Disable RX */
	{RX_FILTR_CFG, 0x17f97},	/*0x1400  , RX filter control, */
	{BKOFF_SLOT_CFG, 0x209},	/* default set short slot time, CC_DELAY_TIME should be 2 */
	/*{TX_SW_CFG0,          0x40a06}, // Gary,2006-08-23 */
	{TX_SW_CFG0, 0x0},	/* Gary,2008-05-21 for CWC test */
	{TX_SW_CFG1, 0x80606},	/* Gary,2006-08-23 */
	{TX_LINK_CFG, 0x1020},	/* Gary,2006-08-23 */
	/*{TX_TIMEOUT_CFG,      0x00182090},    // CCK has some problem. So increase timieout value. 2006-10-09// MArvek RT */
	{TX_TIMEOUT_CFG, 0x000a2090},	/* CCK has some problem. So increase timieout value. 2006-10-09// MArvek RT , Modify for 2860E ,2007-08-01 */
	{MAX_LEN_CFG, MAX_AGGREGATION_SIZE | 0x00001000},	/* 0x3018, MAX frame length. Max PSDU = 16kbytes. */
	{LED_CFG, 0x7f031e46},	/* Gary, 2006-08-23 */

	{PBF_MAX_PCNT, 0x1F3FBF9F},	/*0x1F3f7f9f},          //Jan, 2006/04/20 */

	{TX_RTY_CFG, 0x47d01f0f},	/* Jan, 2006/11/16, Set TxWI->ACK =0 in Probe Rsp Modify for 2860E ,2007-08-03 */

	{AUTO_RSP_CFG, 0x00000013},	/* Initial Auto_Responder, because QA will turn off Auto-Responder */
	{CCK_PROT_CFG, 0x05740003 /*0x01740003 */ },	/* Initial Auto_Responder, because QA will turn off Auto-Responder. And RTS threshold is enabled. */
	{OFDM_PROT_CFG, 0x05740003 /*0x01740003 */ },	/* Initial Auto_Responder, because QA will turn off Auto-Responder. And RTS threshold is enabled. */
#ifdef RTMP_MAC_USB
	{PBF_CFG, 0xf40006},	/* Only enable Queue 2 */
	{MM40_PROT_CFG, 0x3F44084},	/* Initial Auto_Responder, because QA will turn off Auto-Responder */
	{WPDMA_GLO_CFG, 0x00000030},
#endif /* RTMP_MAC_USB // */
	{GF20_PROT_CFG, 0x01744004},	/* set 19:18 --> Short NAV for MIMO PS */
	{GF40_PROT_CFG, 0x03F44084},
	{MM20_PROT_CFG, 0x01744004},
#ifdef RTMP_MAC_PCI
	{MM40_PROT_CFG, 0x03F54084},
#endif /* RTMP_MAC_PCI // */
	{TXOP_CTRL_CFG, 0x0000583f, /*0x0000243f *//*0x000024bf */ },	/*Extension channel backoff. */
	{TX_RTS_CFG, 0x00092b20},
	{EXP_ACK_TIME, 0x002400ca},	/* default value */

	{TXOP_HLDR_ET, 0x00000002},

	/* Jerry comments 2008/01/16: we use SIFS = 10us in CCK defaultly, but it seems that 10us
	   is too small for INTEL 2200bg card, so in MBSS mode, the delta time between beacon0
	   and beacon1 is SIFS (10us), so if INTEL 2200bg card connects to BSS0, the ping
	   will always lost. So we change the SIFS of CCK from 10us to 16us. */
	{XIFS_TIME_CFG, 0x33a41010},
	{PWR_PIN_CFG, 0x00000003},	/* patch for 2880-E */
};

struct rt_rtmp_reg_pair STAMACRegTable[] = {
	{WMM_AIFSN_CFG, 0x00002273},
	{WMM_CWMIN_CFG, 0x00002344},
	{WMM_CWMAX_CFG, 0x000034aa},
};

#define	NUM_MAC_REG_PARMS		(sizeof(MACRegTable) / sizeof(struct rt_rtmp_reg_pair))
#define	NUM_STA_MAC_REG_PARMS	(sizeof(STAMACRegTable) / sizeof(struct rt_rtmp_reg_pair))

/*
	========================================================================

	Routine Description:
		Allocate struct rt_rtmp_adapter data block and do some initialization

	Arguments:
		Adapter		Pointer to our adapter

	Return Value:
		NDIS_STATUS_SUCCESS
		NDIS_STATUS_FAILURE

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
int RTMPAllocAdapterBlock(void *handle,
				  struct rt_rtmp_adapter * * ppAdapter)
{
	struct rt_rtmp_adapter *pAd;
	int Status;
	int index;
	u8 *pBeaconBuf = NULL;

	DBGPRINT(RT_DEBUG_TRACE, ("--> RTMPAllocAdapterBlock\n"));

	*ppAdapter = NULL;

	do {
		/* Allocate struct rt_rtmp_adapter memory block */
		pBeaconBuf = kmalloc(MAX_BEACON_SIZE, MEM_ALLOC_FLAG);
		if (pBeaconBuf == NULL) {
			Status = NDIS_STATUS_FAILURE;
			DBGPRINT_ERR(("Failed to allocate memory - BeaconBuf!\n"));
			break;
		}
		NdisZeroMemory(pBeaconBuf, MAX_BEACON_SIZE);

		Status = AdapterBlockAllocateMemory(handle, (void **) & pAd);
		if (Status != NDIS_STATUS_SUCCESS) {
			DBGPRINT_ERR(("Failed to allocate memory - ADAPTER\n"));
			break;
		}
		pAd->BeaconBuf = pBeaconBuf;
		DBGPRINT(RT_DEBUG_OFF,
			 ("=== pAd = %p, size = %d ===\n", pAd,
			  (u32)sizeof(struct rt_rtmp_adapter)));

		/* Init spin locks */
		NdisAllocateSpinLock(&pAd->MgmtRingLock);
#ifdef RTMP_MAC_PCI
		NdisAllocateSpinLock(&pAd->RxRingLock);
#ifdef RT3090
		NdisAllocateSpinLock(&pAd->McuCmdLock);
#endif /* RT3090 // */
#endif /* RTMP_MAC_PCI // */

		for (index = 0; index < NUM_OF_TX_RING; index++) {
			NdisAllocateSpinLock(&pAd->TxSwQueueLock[index]);
			NdisAllocateSpinLock(&pAd->DeQueueLock[index]);
			pAd->DeQueueRunning[index] = FALSE;
		}

		NdisAllocateSpinLock(&pAd->irq_lock);

	} while (FALSE);

	if ((Status != NDIS_STATUS_SUCCESS) && (pBeaconBuf))
		kfree(pBeaconBuf);

	*ppAdapter = pAd;

	DBGPRINT_S(Status, ("<-- RTMPAllocAdapterBlock, Status=%x\n", Status));
	return Status;
}

/*
	========================================================================

	Routine Description:
		Read initial Tx power per MCS and BW from EEPROM

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
void RTMPReadTxPwrPerRate(struct rt_rtmp_adapter *pAd)
{
	unsigned long data, Adata, Gdata;
	u16 i, value, value2;
	int Apwrdelta, Gpwrdelta;
	u8 t1, t2, t3, t4;
	BOOLEAN bApwrdeltaMinus = TRUE, bGpwrdeltaMinus = TRUE;

	/* */
	/* Get power delta for 20MHz and 40MHz. */
	/* */
	DBGPRINT(RT_DEBUG_TRACE, ("Txpower per Rate\n"));
	RT28xx_EEPROM_READ16(pAd, EEPROM_TXPOWER_DELTA, value2);
	Apwrdelta = 0;
	Gpwrdelta = 0;

	if ((value2 & 0xff) != 0xff) {
		if ((value2 & 0x80))
			Gpwrdelta = (value2 & 0xf);

		if ((value2 & 0x40))
			bGpwrdeltaMinus = FALSE;
		else
			bGpwrdeltaMinus = TRUE;
	}
	if ((value2 & 0xff00) != 0xff00) {
		if ((value2 & 0x8000))
			Apwrdelta = ((value2 & 0xf00) >> 8);

		if ((value2 & 0x4000))
			bApwrdeltaMinus = FALSE;
		else
			bApwrdeltaMinus = TRUE;
	}
	DBGPRINT(RT_DEBUG_TRACE,
		 ("Gpwrdelta = %x, Apwrdelta = %x .\n", Gpwrdelta, Apwrdelta));

	/* */
	/* Get Txpower per MCS for 20MHz in 2.4G. */
	/* */
	for (i = 0; i < 5; i++) {
		RT28xx_EEPROM_READ16(pAd,
				     EEPROM_TXPOWER_BYRATE_20MHZ_2_4G + i * 4,
				     value);
		data = value;
		if (bApwrdeltaMinus == FALSE) {
			t1 = (value & 0xf) + (Apwrdelta);
			if (t1 > 0xf)
				t1 = 0xf;
			t2 = ((value & 0xf0) >> 4) + (Apwrdelta);
			if (t2 > 0xf)
				t2 = 0xf;
			t3 = ((value & 0xf00) >> 8) + (Apwrdelta);
			if (t3 > 0xf)
				t3 = 0xf;
			t4 = ((value & 0xf000) >> 12) + (Apwrdelta);
			if (t4 > 0xf)
				t4 = 0xf;
		} else {
			if ((value & 0xf) > Apwrdelta)
				t1 = (value & 0xf) - (Apwrdelta);
			else
				t1 = 0;
			if (((value & 0xf0) >> 4) > Apwrdelta)
				t2 = ((value & 0xf0) >> 4) - (Apwrdelta);
			else
				t2 = 0;
			if (((value & 0xf00) >> 8) > Apwrdelta)
				t3 = ((value & 0xf00) >> 8) - (Apwrdelta);
			else
				t3 = 0;
			if (((value & 0xf000) >> 12) > Apwrdelta)
				t4 = ((value & 0xf000) >> 12) - (Apwrdelta);
			else
				t4 = 0;
		}
		Adata = t1 + (t2 << 4) + (t3 << 8) + (t4 << 12);
		if (bGpwrdeltaMinus == FALSE) {
			t1 = (value & 0xf) + (Gpwrdelta);
			if (t1 > 0xf)
				t1 = 0xf;
			t2 = ((value & 0xf0) >> 4) + (Gpwrdelta);
			if (t2 > 0xf)
				t2 = 0xf;
			t3 = ((value & 0xf00) >> 8) + (Gpwrdelta);
			if (t3 > 0xf)
				t3 = 0xf;
			t4 = ((value & 0xf000) >> 12) + (Gpwrdelta);
			if (t4 > 0xf)
				t4 = 0xf;
		} else {
			if ((value & 0xf) > Gpwrdelta)
				t1 = (value & 0xf) - (Gpwrdelta);
			else
				t1 = 0;
			if (((value & 0xf0) >> 4) > Gpwrdelta)
				t2 = ((value & 0xf0) >> 4) - (Gpwrdelta);
			else
				t2 = 0;
			if (((value & 0xf00) >> 8) > Gpwrdelta)
				t3 = ((value & 0xf00) >> 8) - (Gpwrdelta);
			else
				t3 = 0;
			if (((value & 0xf000) >> 12) > Gpwrdelta)
				t4 = ((value & 0xf000) >> 12) - (Gpwrdelta);
			else
				t4 = 0;
		}
		Gdata = t1 + (t2 << 4) + (t3 << 8) + (t4 << 12);

		RT28xx_EEPROM_READ16(pAd,
				     EEPROM_TXPOWER_BYRATE_20MHZ_2_4G + i * 4 +
				     2, value);
		if (bApwrdeltaMinus == FALSE) {
			t1 = (value & 0xf) + (Apwrdelta);
			if (t1 > 0xf)
				t1 = 0xf;
			t2 = ((value & 0xf0) >> 4) + (Apwrdelta);
			if (t2 > 0xf)
				t2 = 0xf;
			t3 = ((value & 0xf00) >> 8) + (Apwrdelta);
			if (t3 > 0xf)
				t3 = 0xf;
			t4 = ((value & 0xf000) >> 12) + (Apwrdelta);
			if (t4 > 0xf)
				t4 = 0xf;
		} else {
			if ((value & 0xf) > Apwrdelta)
				t1 = (value & 0xf) - (Apwrdelta);
			else
				t1 = 0;
			if (((value & 0xf0) >> 4) > Apwrdelta)
				t2 = ((value & 0xf0) >> 4) - (Apwrdelta);
			else
				t2 = 0;
			if (((value & 0xf00) >> 8) > Apwrdelta)
				t3 = ((value & 0xf00) >> 8) - (Apwrdelta);
			else
				t3 = 0;
			if (((value & 0xf000) >> 12) > Apwrdelta)
				t4 = ((value & 0xf000) >> 12) - (Apwrdelta);
			else
				t4 = 0;
		}
		Adata |= ((t1 << 16) + (t2 << 20) + (t3 << 24) + (t4 << 28));
		if (bGpwrdeltaMinus == FALSE) {
			t1 = (value & 0xf) + (Gpwrdelta);
			if (t1 > 0xf)
				t1 = 0xf;
			t2 = ((value & 0xf0) >> 4) + (Gpwrdelta);
			if (t2 > 0xf)
				t2 = 0xf;
			t3 = ((value & 0xf00) >> 8) + (Gpwrdelta);
			if (t3 > 0xf)
				t3 = 0xf;
			t4 = ((value & 0xf000) >> 12) + (Gpwrdelta);
			if (t4 > 0xf)
				t4 = 0xf;
		} else {
			if ((value & 0xf) > Gpwrdelta)
				t1 = (value & 0xf) - (Gpwrdelta);
			else
				t1 = 0;
			if (((value & 0xf0) >> 4) > Gpwrdelta)
				t2 = ((value & 0xf0) >> 4) - (Gpwrdelta);
			else
				t2 = 0;
			if (((value & 0xf00) >> 8) > Gpwrdelta)
				t3 = ((value & 0xf00) >> 8) - (Gpwrdelta);
			else
				t3 = 0;
			if (((value & 0xf000) >> 12) > Gpwrdelta)
				t4 = ((value & 0xf000) >> 12) - (Gpwrdelta);
			else
				t4 = 0;
		}
		Gdata |= ((t1 << 16) + (t2 << 20) + (t3 << 24) + (t4 << 28));
		data |= (value << 16);

		/* For 20M/40M Power Delta issue */
		pAd->Tx20MPwrCfgABand[i] = data;
		pAd->Tx20MPwrCfgGBand[i] = data;
		pAd->Tx40MPwrCfgABand[i] = Adata;
		pAd->Tx40MPwrCfgGBand[i] = Gdata;

		if (data != 0xffffffff)
			RTMP_IO_WRITE32(pAd, TX_PWR_CFG_0 + i * 4, data);
		DBGPRINT_RAW(RT_DEBUG_TRACE,
			     ("20MHz BW, 2.4G band-%lx,  Adata = %lx,  Gdata = %lx \n",
			      data, Adata, Gdata));
	}
}

/*
	========================================================================

	Routine Description:
		Read initial channel power parameters from EEPROM

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
void RTMPReadChannelPwr(struct rt_rtmp_adapter *pAd)
{
	u8 i, choffset;
	EEPROM_TX_PWR_STRUC Power;
	EEPROM_TX_PWR_STRUC Power2;

	/* Read Tx power value for all channels */
	/* Value from 1 - 0x7f. Default value is 24. */
	/* Power value : 2.4G 0x00 (0) ~ 0x1F (31) */
	/*             : 5.5G 0xF9 (-7) ~ 0x0F (15) */

	/* 0. 11b/g, ch1 - ch 14 */
	for (i = 0; i < 7; i++) {
		RT28xx_EEPROM_READ16(pAd, EEPROM_G_TX_PWR_OFFSET + i * 2,
				     Power.word);
		RT28xx_EEPROM_READ16(pAd, EEPROM_G_TX2_PWR_OFFSET + i * 2,
				     Power2.word);
		pAd->TxPower[i * 2].Channel = i * 2 + 1;
		pAd->TxPower[i * 2 + 1].Channel = i * 2 + 2;

		if ((Power.field.Byte0 > 31) || (Power.field.Byte0 < 0))
			pAd->TxPower[i * 2].Power = DEFAULT_RF_TX_POWER;
		else
			pAd->TxPower[i * 2].Power = Power.field.Byte0;

		if ((Power.field.Byte1 > 31) || (Power.field.Byte1 < 0))
			pAd->TxPower[i * 2 + 1].Power = DEFAULT_RF_TX_POWER;
		else
			pAd->TxPower[i * 2 + 1].Power = Power.field.Byte1;

		if ((Power2.field.Byte0 > 31) || (Power2.field.Byte0 < 0))
			pAd->TxPower[i * 2].Power2 = DEFAULT_RF_TX_POWER;
		else
			pAd->TxPower[i * 2].Power2 = Power2.field.Byte0;

		if ((Power2.field.Byte1 > 31) || (Power2.field.Byte1 < 0))
			pAd->TxPower[i * 2 + 1].Power2 = DEFAULT_RF_TX_POWER;
		else
			pAd->TxPower[i * 2 + 1].Power2 = Power2.field.Byte1;
	}

	/* 1. U-NII lower/middle band: 36, 38, 40; 44, 46, 48; 52, 54, 56; 60, 62, 64 (including central frequency in BW 40MHz) */
	/* 1.1 Fill up channel */
	choffset = 14;
	for (i = 0; i < 4; i++) {
		pAd->TxPower[3 * i + choffset + 0].Channel = 36 + i * 8 + 0;
		pAd->TxPower[3 * i + choffset + 0].Power = DEFAULT_RF_TX_POWER;
		pAd->TxPower[3 * i + choffset + 0].Power2 = DEFAULT_RF_TX_POWER;

		pAd->TxPower[3 * i + choffset + 1].Channel = 36 + i * 8 + 2;
		pAd->TxPower[3 * i + choffset + 1].Power = DEFAULT_RF_TX_POWER;
		pAd->TxPower[3 * i + choffset + 1].Power2 = DEFAULT_RF_TX_POWER;

		pAd->TxPower[3 * i + choffset + 2].Channel = 36 + i * 8 + 4;
		pAd->TxPower[3 * i + choffset + 2].Power = DEFAULT_RF_TX_POWER;
		pAd->TxPower[3 * i + choffset + 2].Power2 = DEFAULT_RF_TX_POWER;
	}

	/* 1.2 Fill up power */
	for (i = 0; i < 6; i++) {
		RT28xx_EEPROM_READ16(pAd, EEPROM_A_TX_PWR_OFFSET + i * 2,
				     Power.word);
		RT28xx_EEPROM_READ16(pAd, EEPROM_A_TX2_PWR_OFFSET + i * 2,
				     Power2.word);

		if ((Power.field.Byte0 < 16) && (Power.field.Byte0 >= -7))
			pAd->TxPower[i * 2 + choffset + 0].Power =
			    Power.field.Byte0;

		if ((Power.field.Byte1 < 16) && (Power.field.Byte1 >= -7))
			pAd->TxPower[i * 2 + choffset + 1].Power =
			    Power.field.Byte1;

		if ((Power2.field.Byte0 < 16) && (Power2.field.Byte0 >= -7))
			pAd->TxPower[i * 2 + choffset + 0].Power2 =
			    Power2.field.Byte0;

		if ((Power2.field.Byte1 < 16) && (Power2.field.Byte1 >= -7))
			pAd->TxPower[i * 2 + choffset + 1].Power2 =
			    Power2.field.Byte1;
	}

	/* 2. HipperLAN 2 100, 102 ,104; 108, 110, 112; 116, 118, 120; 124, 126, 128; 132, 134, 136; 140 (including central frequency in BW 40MHz) */
	/* 2.1 Fill up channel */
	choffset = 14 + 12;
	for (i = 0; i < 5; i++) {
		pAd->TxPower[3 * i + choffset + 0].Channel = 100 + i * 8 + 0;
		pAd->TxPower[3 * i + choffset + 0].Power = DEFAULT_RF_TX_POWER;
		pAd->TxPower[3 * i + choffset + 0].Power2 = DEFAULT_RF_TX_POWER;

		pAd->TxPower[3 * i + choffset + 1].Channel = 100 + i * 8 + 2;
		pAd->TxPower[3 * i + choffset + 1].Power = DEFAULT_RF_TX_POWER;
		pAd->TxPower[3 * i + choffset + 1].Power2 = DEFAULT_RF_TX_POWER;

		pAd->TxPower[3 * i + choffset + 2].Channel = 100 + i * 8 + 4;
		pAd->TxPower[3 * i + choffset + 2].Power = DEFAULT_RF_TX_POWER;
		pAd->TxPower[3 * i + choffset + 2].Power2 = DEFAULT_RF_TX_POWER;
	}
	pAd->TxPower[3 * 5 + choffset + 0].Channel = 140;
	pAd->TxPower[3 * 5 + choffset + 0].Power = DEFAULT_RF_TX_POWER;
	pAd->TxPower[3 * 5 + choffset + 0].Power2 = DEFAULT_RF_TX_POWER;

	/* 2.2 Fill up power */
	for (i = 0; i < 8; i++) {
		RT28xx_EEPROM_READ16(pAd,
				     EEPROM_A_TX_PWR_OFFSET + (choffset - 14) +
				     i * 2, Power.word);
		RT28xx_EEPROM_READ16(pAd,
				     EEPROM_A_TX2_PWR_OFFSET + (choffset - 14) +
				     i * 2, Power2.word);

		if ((Power.field.Byte0 < 16) && (Power.field.Byte0 >= -7))
			pAd->TxPower[i * 2 + choffset + 0].Power =
			    Power.field.Byte0;

		if ((Power.field.Byte1 < 16) && (Power.field.Byte1 >= -7))
			pAd->TxPower[i * 2 + choffset + 1].Power =
			    Power.field.Byte1;

		if ((Power2.field.Byte0 < 16) && (Power2.field.Byte0 >= -7))
			pAd->TxPower[i * 2 + choffset + 0].Power2 =
			    Power2.field.Byte0;

		if ((Power2.field.Byte1 < 16) && (Power2.field.Byte1 >= -7))
			pAd->TxPower[i * 2 + choffset + 1].Power2 =
			    Power2.field.Byte1;
	}

	/* 3. U-NII upper band: 149, 151, 153; 157, 159, 161; 165, 167, 169; 171, 173 (including central frequency in BW 40MHz) */
	/* 3.1 Fill up channel */
	choffset = 14 + 12 + 16;
	/*for (i = 0; i < 2; i++) */
	for (i = 0; i < 3; i++) {
		pAd->TxPower[3 * i + choffset + 0].Channel = 149 + i * 8 + 0;
		pAd->TxPower[3 * i + choffset + 0].Power = DEFAULT_RF_TX_POWER;
		pAd->TxPower[3 * i + choffset + 0].Power2 = DEFAULT_RF_TX_POWER;

		pAd->TxPower[3 * i + choffset + 1].Channel = 149 + i * 8 + 2;
		pAd->TxPower[3 * i + choffset + 1].Power = DEFAULT_RF_TX_POWER;
		pAd->TxPower[3 * i + choffset + 1].Power2 = DEFAULT_RF_TX_POWER;

		pAd->TxPower[3 * i + choffset + 2].Channel = 149 + i * 8 + 4;
		pAd->TxPower[3 * i + choffset + 2].Power = DEFAULT_RF_TX_POWER;
		pAd->TxPower[3 * i + choffset + 2].Power2 = DEFAULT_RF_TX_POWER;
	}
	pAd->TxPower[3 * 3 + choffset + 0].Channel = 171;
	pAd->TxPower[3 * 3 + choffset + 0].Power = DEFAULT_RF_TX_POWER;
	pAd->TxPower[3 * 3 + choffset + 0].Power2 = DEFAULT_RF_TX_POWER;

	pAd->TxPower[3 * 3 + choffset + 1].Channel = 173;
	pAd->TxPower[3 * 3 + choffset + 1].Power = DEFAULT_RF_TX_POWER;
	pAd->TxPower[3 * 3 + choffset + 1].Power2 = DEFAULT_RF_TX_POWER;

	/* 3.2 Fill up power */
	/*for (i = 0; i < 4; i++) */
	for (i = 0; i < 6; i++) {
		RT28xx_EEPROM_READ16(pAd,
				     EEPROM_A_TX_PWR_OFFSET + (choffset - 14) +
				     i * 2, Power.word);
		RT28xx_EEPROM_READ16(pAd,
				     EEPROM_A_TX2_PWR_OFFSET + (choffset - 14) +
				     i * 2, Power2.word);

		if ((Power.field.Byte0 < 16) && (Power.field.Byte0 >= -7))
			pAd->TxPower[i * 2 + choffset + 0].Power =
			    Power.field.Byte0;

		if ((Power.field.Byte1 < 16) && (Power.field.Byte1 >= -7))
			pAd->TxPower[i * 2 + choffset + 1].Power =
			    Power.field.Byte1;

		if ((Power2.field.Byte0 < 16) && (Power2.field.Byte0 >= -7))
			pAd->TxPower[i * 2 + choffset + 0].Power2 =
			    Power2.field.Byte0;

		if ((Power2.field.Byte1 < 16) && (Power2.field.Byte1 >= -7))
			pAd->TxPower[i * 2 + choffset + 1].Power2 =
			    Power2.field.Byte1;
	}

	/* 4. Print and Debug */
	/*choffset = 14 + 12 + 16 + 7; */
	choffset = 14 + 12 + 16 + 11;

}

/*
	========================================================================

	Routine Description:
		Read the following from the registry
		1. All the parameters
		2. NetworkAddres

	Arguments:
		Adapter						Pointer to our adapter
		WrapperConfigurationContext	For use by NdisOpenConfiguration

	Return Value:
		NDIS_STATUS_SUCCESS
		NDIS_STATUS_FAILURE
		NDIS_STATUS_RESOURCES

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
int NICReadRegParameters(struct rt_rtmp_adapter *pAd,
				 void *WrapperConfigurationContext)
{
	int Status = NDIS_STATUS_SUCCESS;
	DBGPRINT_S(Status, ("<-- NICReadRegParameters, Status=%x\n", Status));
	return Status;
}

/*
	========================================================================

	Routine Description:
		Read initial parameters from EEPROM

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
void NICReadEEPROMParameters(struct rt_rtmp_adapter *pAd, u8 *mac_addr)
{
	u32 data = 0;
	u16 i, value, value2;
	u8 TmpPhy;
	EEPROM_TX_PWR_STRUC Power;
	EEPROM_VERSION_STRUC Version;
	EEPROM_ANTENNA_STRUC Antenna;
	EEPROM_NIC_CONFIG2_STRUC NicConfig2;

	DBGPRINT(RT_DEBUG_TRACE, ("--> NICReadEEPROMParameters\n"));

	if (pAd->chipOps.eeinit)
		pAd->chipOps.eeinit(pAd);

	/* Init EEPROM Address Number, before access EEPROM; if 93c46, EEPROMAddressNum=6, else if 93c66, EEPROMAddressNum=8 */
	RTMP_IO_READ32(pAd, E2PROM_CSR, &data);
	DBGPRINT(RT_DEBUG_TRACE, ("--> E2PROM_CSR = 0x%x\n", data));

	if ((data & 0x30) == 0)
		pAd->EEPROMAddressNum = 6;	/* 93C46 */
	else if ((data & 0x30) == 0x10)
		pAd->EEPROMAddressNum = 8;	/* 93C66 */
	else
		pAd->EEPROMAddressNum = 8;	/* 93C86 */
	DBGPRINT(RT_DEBUG_TRACE,
		 ("--> EEPROMAddressNum = %d\n", pAd->EEPROMAddressNum));

	/* RT2860 MAC no longer auto load MAC address from E2PROM. Driver has to intialize */
	/* MAC address registers according to E2PROM setting */
	if (mac_addr == NULL ||
	    strlen((char *)mac_addr) != 17 ||
	    mac_addr[2] != ':' || mac_addr[5] != ':' || mac_addr[8] != ':' ||
	    mac_addr[11] != ':' || mac_addr[14] != ':') {
		u16 Addr01, Addr23, Addr45;

		RT28xx_EEPROM_READ16(pAd, 0x04, Addr01);
		RT28xx_EEPROM_READ16(pAd, 0x06, Addr23);
		RT28xx_EEPROM_READ16(pAd, 0x08, Addr45);

		pAd->PermanentAddress[0] = (u8)(Addr01 & 0xff);
		pAd->PermanentAddress[1] = (u8)(Addr01 >> 8);
		pAd->PermanentAddress[2] = (u8)(Addr23 & 0xff);
		pAd->PermanentAddress[3] = (u8)(Addr23 >> 8);
		pAd->PermanentAddress[4] = (u8)(Addr45 & 0xff);
		pAd->PermanentAddress[5] = (u8)(Addr45 >> 8);

		DBGPRINT(RT_DEBUG_TRACE,
			 ("Initialize MAC Address from E2PROM \n"));
	} else {
		int j;
		char *macptr;

		macptr = (char *)mac_addr;

		for (j = 0; j < MAC_ADDR_LEN; j++) {
			AtoH(macptr, &pAd->PermanentAddress[j], 1);
			macptr = macptr + 3;
		}

		DBGPRINT(RT_DEBUG_TRACE,
			 ("Initialize MAC Address from module parameter \n"));
	}

	{
		/*more conveninet to test mbssid, so ap's bssid &0xf1 */
		if (pAd->PermanentAddress[0] == 0xff)
			pAd->PermanentAddress[0] = RandomByte(pAd) & 0xf8;

		/*if (pAd->PermanentAddress[5] == 0xff) */
		/*      pAd->PermanentAddress[5] = RandomByte(pAd)&0xf8; */

		DBGPRINT_RAW(RT_DEBUG_TRACE,
			     ("E2PROM MAC: =%02x:%02x:%02x:%02x:%02x:%02x\n",
			      pAd->PermanentAddress[0],
			      pAd->PermanentAddress[1],
			      pAd->PermanentAddress[2],
			      pAd->PermanentAddress[3],
			      pAd->PermanentAddress[4],
			      pAd->PermanentAddress[5]));
		if (pAd->bLocalAdminMAC == FALSE) {
			MAC_DW0_STRUC csr2;
			MAC_DW1_STRUC csr3;
			COPY_MAC_ADDR(pAd->CurrentAddress,
				      pAd->PermanentAddress);
			csr2.field.Byte0 = pAd->CurrentAddress[0];
			csr2.field.Byte1 = pAd->CurrentAddress[1];
			csr2.field.Byte2 = pAd->CurrentAddress[2];
			csr2.field.Byte3 = pAd->CurrentAddress[3];
			RTMP_IO_WRITE32(pAd, MAC_ADDR_DW0, csr2.word);
			csr3.word = 0;
			csr3.field.Byte4 = pAd->CurrentAddress[4];
			csr3.field.Byte5 = pAd->CurrentAddress[5];
			csr3.field.U2MeMask = 0xff;
			RTMP_IO_WRITE32(pAd, MAC_ADDR_DW1, csr3.word);
			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("E2PROM MAC: =%02x:%02x:%02x:%02x:%02x:%02x\n",
				      PRINT_MAC(pAd->PermanentAddress)));
		}
	}

	/* if not return early. cause fail at emulation. */
	/* Init the channel number for TX channel power */
	RTMPReadChannelPwr(pAd);

	/* if E2PROM version mismatch with driver's expectation, then skip */
	/* all subsequent E2RPOM retieval and set a system error bit to notify GUI */
	RT28xx_EEPROM_READ16(pAd, EEPROM_VERSION_OFFSET, Version.word);
	pAd->EepromVersion =
	    Version.field.Version + Version.field.FaeReleaseNumber * 256;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("E2PROM: Version = %d, FAE release #%d\n",
		  Version.field.Version, Version.field.FaeReleaseNumber));

	if (Version.field.Version > VALID_EEPROM_VERSION) {
		DBGPRINT_ERR(("E2PROM: WRONG VERSION 0x%x, should be %d\n",
			      Version.field.Version, VALID_EEPROM_VERSION));
		/*pAd->SystemErrorBitmap |= 0x00000001;

		   // hard-code default value when no proper E2PROM installed
		   pAd->bAutoTxAgcA = FALSE;
		   pAd->bAutoTxAgcG = FALSE;

		   // Default the channel power
		   for (i = 0; i < MAX_NUM_OF_CHANNELS; i++)
		   pAd->TxPower[i].Power = DEFAULT_RF_TX_POWER;

		   // Default the channel power
		   for (i = 0; i < MAX_NUM_OF_11JCHANNELS; i++)
		   pAd->TxPower11J[i].Power = DEFAULT_RF_TX_POWER;

		   for(i = 0; i < NUM_EEPROM_BBP_PARMS; i++)
		   pAd->EEPROMDefaultValue[i] = 0xffff;
		   return;  */
	}
	/* Read BBP default value from EEPROM and store to array(EEPROMDefaultValue) in pAd */
	RT28xx_EEPROM_READ16(pAd, EEPROM_NIC1_OFFSET, value);
	pAd->EEPROMDefaultValue[0] = value;

	RT28xx_EEPROM_READ16(pAd, EEPROM_NIC2_OFFSET, value);
	pAd->EEPROMDefaultValue[1] = value;

	RT28xx_EEPROM_READ16(pAd, 0x38, value);	/* Country Region */
	pAd->EEPROMDefaultValue[2] = value;

	for (i = 0; i < 8; i++) {
		RT28xx_EEPROM_READ16(pAd, EEPROM_BBP_BASE_OFFSET + i * 2,
				     value);
		pAd->EEPROMDefaultValue[i + 3] = value;
	}

	/* We have to parse NIC configuration 0 at here. */
	/* If TSSI did not have preloaded value, it should reset the TxAutoAgc to false */
	/* Therefore, we have to read TxAutoAgc control beforehand. */
	/* Read Tx AGC control bit */
	Antenna.word = pAd->EEPROMDefaultValue[0];
	if (Antenna.word == 0xFFFF) {
#ifdef RT30xx
		if (IS_RT3090(pAd) || IS_RT3390(pAd)) {
			Antenna.word = 0;
			Antenna.field.RfIcType = RFIC_3020;
			Antenna.field.TxPath = 1;
			Antenna.field.RxPath = 1;
		} else
#endif /* RT30xx // */
		{

			Antenna.word = 0;
			Antenna.field.RfIcType = RFIC_2820;
			Antenna.field.TxPath = 1;
			Antenna.field.RxPath = 2;
			DBGPRINT(RT_DEBUG_WARN,
				 ("E2PROM error, hard code as 0x%04x\n",
				  Antenna.word));
		}
	}
	/* Choose the desired Tx&Rx stream. */
	if ((pAd->CommonCfg.TxStream == 0)
	    || (pAd->CommonCfg.TxStream > Antenna.field.TxPath))
		pAd->CommonCfg.TxStream = Antenna.field.TxPath;

	if ((pAd->CommonCfg.RxStream == 0)
	    || (pAd->CommonCfg.RxStream > Antenna.field.RxPath)) {
		pAd->CommonCfg.RxStream = Antenna.field.RxPath;

		if ((pAd->MACVersion < RALINK_2883_VERSION) &&
		    (pAd->CommonCfg.RxStream > 2)) {
			/* only 2 Rx streams for RT2860 series */
			pAd->CommonCfg.RxStream = 2;
		}
	}
	/* 3*3 */
	/* read value from EEPROM and set them to CSR174 ~ 177 in chain0 ~ chain2 */
	/* yet implement */
	for (i = 0; i < 3; i++) {
	}

	NicConfig2.word = pAd->EEPROMDefaultValue[1];

	{
		if ((NicConfig2.word & 0x00ff) == 0xff) {
			NicConfig2.word &= 0xff00;
		}

		if ((NicConfig2.word >> 8) == 0xff) {
			NicConfig2.word &= 0x00ff;
		}
	}

	if (NicConfig2.field.DynamicTxAgcControl == 1)
		pAd->bAutoTxAgcA = pAd->bAutoTxAgcG = TRUE;
	else
		pAd->bAutoTxAgcA = pAd->bAutoTxAgcG = FALSE;

	DBGPRINT_RAW(RT_DEBUG_TRACE,
		     ("NICReadEEPROMParameters: RxPath = %d, TxPath = %d\n",
		      Antenna.field.RxPath, Antenna.field.TxPath));

	/* Save the antenna for future use */
	pAd->Antenna.word = Antenna.word;

	/* Set the RfICType here, then we can initialize RFIC related operation callbacks */
	pAd->Mlme.RealRxPath = (u8)Antenna.field.RxPath;
	pAd->RfIcType = (u8)Antenna.field.RfIcType;

#ifdef RTMP_RF_RW_SUPPORT
	RtmpChipOpsRFHook(pAd);
#endif /* RTMP_RF_RW_SUPPORT // */

#ifdef RTMP_MAC_PCI
	sprintf((char *)pAd->nickname, "RT2860STA");
#endif /* RTMP_MAC_PCI // */

	/* */
	/* Reset PhyMode if we don't support 802.11a */
	/* Only RFIC_2850 & RFIC_2750 support 802.11a */
	/* */
	if ((Antenna.field.RfIcType != RFIC_2850)
	    && (Antenna.field.RfIcType != RFIC_2750)
	    && (Antenna.field.RfIcType != RFIC_3052)) {
		if ((pAd->CommonCfg.PhyMode == PHY_11ABG_MIXED) ||
		    (pAd->CommonCfg.PhyMode == PHY_11A))
			pAd->CommonCfg.PhyMode = PHY_11BG_MIXED;
		else if ((pAd->CommonCfg.PhyMode == PHY_11ABGN_MIXED) ||
			 (pAd->CommonCfg.PhyMode == PHY_11AN_MIXED) ||
			 (pAd->CommonCfg.PhyMode == PHY_11AGN_MIXED) ||
			 (pAd->CommonCfg.PhyMode == PHY_11N_5G))
			pAd->CommonCfg.PhyMode = PHY_11BGN_MIXED;
	}
	/* Read TSSI reference and TSSI boundary for temperature compensation. This is ugly */
	/* 0. 11b/g */
	{
		/* these are tempature reference value (0x00 ~ 0xFE)
		   ex: 0x00 0x15 0x25 0x45 0x88 0xA0 0xB5 0xD0 0xF0
		   TssiPlusBoundaryG [4] [3] [2] [1] [0] (smaller) +
		   TssiMinusBoundaryG[0] [1] [2] [3] [4] (larger) */
		RT28xx_EEPROM_READ16(pAd, 0x6E, Power.word);
		pAd->TssiMinusBoundaryG[4] = Power.field.Byte0;
		pAd->TssiMinusBoundaryG[3] = Power.field.Byte1;
		RT28xx_EEPROM_READ16(pAd, 0x70, Power.word);
		pAd->TssiMinusBoundaryG[2] = Power.field.Byte0;
		pAd->TssiMinusBoundaryG[1] = Power.field.Byte1;
		RT28xx_EEPROM_READ16(pAd, 0x72, Power.word);
		pAd->TssiRefG = Power.field.Byte0;	/* reference value [0] */
		pAd->TssiPlusBoundaryG[1] = Power.field.Byte1;
		RT28xx_EEPROM_READ16(pAd, 0x74, Power.word);
		pAd->TssiPlusBoundaryG[2] = Power.field.Byte0;
		pAd->TssiPlusBoundaryG[3] = Power.field.Byte1;
		RT28xx_EEPROM_READ16(pAd, 0x76, Power.word);
		pAd->TssiPlusBoundaryG[4] = Power.field.Byte0;
		pAd->TxAgcStepG = Power.field.Byte1;
		pAd->TxAgcCompensateG = 0;
		pAd->TssiMinusBoundaryG[0] = pAd->TssiRefG;
		pAd->TssiPlusBoundaryG[0] = pAd->TssiRefG;

		/* Disable TxAgc if the based value is not right */
		if (pAd->TssiRefG == 0xff)
			pAd->bAutoTxAgcG = FALSE;

		DBGPRINT(RT_DEBUG_TRACE,
			 ("E2PROM: G Tssi[-4 .. +4] = %d %d %d %d - %d -%d %d %d %d, step=%d, tuning=%d\n",
			  pAd->TssiMinusBoundaryG[4],
			  pAd->TssiMinusBoundaryG[3],
			  pAd->TssiMinusBoundaryG[2],
			  pAd->TssiMinusBoundaryG[1], pAd->TssiRefG,
			  pAd->TssiPlusBoundaryG[1], pAd->TssiPlusBoundaryG[2],
			  pAd->TssiPlusBoundaryG[3], pAd->TssiPlusBoundaryG[4],
			  pAd->TxAgcStepG, pAd->bAutoTxAgcG));
	}
	/* 1. 11a */
	{
		RT28xx_EEPROM_READ16(pAd, 0xD4, Power.word);
		pAd->TssiMinusBoundaryA[4] = Power.field.Byte0;
		pAd->TssiMinusBoundaryA[3] = Power.field.Byte1;
		RT28xx_EEPROM_READ16(pAd, 0xD6, Power.word);
		pAd->TssiMinusBoundaryA[2] = Power.field.Byte0;
		pAd->TssiMinusBoundaryA[1] = Power.field.Byte1;
		RT28xx_EEPROM_READ16(pAd, 0xD8, Power.word);
		pAd->TssiRefA = Power.field.Byte0;
		pAd->TssiPlusBoundaryA[1] = Power.field.Byte1;
		RT28xx_EEPROM_READ16(pAd, 0xDA, Power.word);
		pAd->TssiPlusBoundaryA[2] = Power.field.Byte0;
		pAd->TssiPlusBoundaryA[3] = Power.field.Byte1;
		RT28xx_EEPROM_READ16(pAd, 0xDC, Power.word);
		pAd->TssiPlusBoundaryA[4] = Power.field.Byte0;
		pAd->TxAgcStepA = Power.field.Byte1;
		pAd->TxAgcCompensateA = 0;
		pAd->TssiMinusBoundaryA[0] = pAd->TssiRefA;
		pAd->TssiPlusBoundaryA[0] = pAd->TssiRefA;

		/* Disable TxAgc if the based value is not right */
		if (pAd->TssiRefA == 0xff)
			pAd->bAutoTxAgcA = FALSE;

		DBGPRINT(RT_DEBUG_TRACE,
			 ("E2PROM: A Tssi[-4 .. +4] = %d %d %d %d - %d -%d %d %d %d, step=%d, tuning=%d\n",
			  pAd->TssiMinusBoundaryA[4],
			  pAd->TssiMinusBoundaryA[3],
			  pAd->TssiMinusBoundaryA[2],
			  pAd->TssiMinusBoundaryA[1], pAd->TssiRefA,
			  pAd->TssiPlusBoundaryA[1], pAd->TssiPlusBoundaryA[2],
			  pAd->TssiPlusBoundaryA[3], pAd->TssiPlusBoundaryA[4],
			  pAd->TxAgcStepA, pAd->bAutoTxAgcA));
	}
	pAd->BbpRssiToDbmDelta = 0x0;

	/* Read frequency offset setting for RF */
	RT28xx_EEPROM_READ16(pAd, EEPROM_FREQ_OFFSET, value);
	if ((value & 0x00FF) != 0x00FF)
		pAd->RfFreqOffset = (unsigned long)(value & 0x00FF);
	else
		pAd->RfFreqOffset = 0;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("E2PROM: RF FreqOffset=0x%lx \n", pAd->RfFreqOffset));

	/*CountryRegion byte offset (38h) */
	value = pAd->EEPROMDefaultValue[2] >> 8;	/* 2.4G band */
	value2 = pAd->EEPROMDefaultValue[2] & 0x00FF;	/* 5G band */

	if ((value <= REGION_MAXIMUM_BG_BAND)
	    && (value2 <= REGION_MAXIMUM_A_BAND)) {
		pAd->CommonCfg.CountryRegion = ((u8)value) | 0x80;
		pAd->CommonCfg.CountryRegionForABand = ((u8)value2) | 0x80;
		TmpPhy = pAd->CommonCfg.PhyMode;
		pAd->CommonCfg.PhyMode = 0xff;
		RTMPSetPhyMode(pAd, TmpPhy);
		SetCommonHT(pAd);
	}
	/* */
	/* Get RSSI Offset on EEPROM 0x9Ah & 0x9Ch. */
	/* The valid value are (-10 ~ 10) */
	/* */
	RT28xx_EEPROM_READ16(pAd, EEPROM_RSSI_BG_OFFSET, value);
	pAd->BGRssiOffset0 = value & 0x00ff;
	pAd->BGRssiOffset1 = (value >> 8);
	RT28xx_EEPROM_READ16(pAd, EEPROM_RSSI_BG_OFFSET + 2, value);
	pAd->BGRssiOffset2 = value & 0x00ff;
	pAd->ALNAGain1 = (value >> 8);
	RT28xx_EEPROM_READ16(pAd, EEPROM_LNA_OFFSET, value);
	pAd->BLNAGain = value & 0x00ff;
	pAd->ALNAGain0 = (value >> 8);

	/* Validate 11b/g RSSI_0 offset. */
	if ((pAd->BGRssiOffset0 < -10) || (pAd->BGRssiOffset0 > 10))
		pAd->BGRssiOffset0 = 0;

	/* Validate 11b/g RSSI_1 offset. */
	if ((pAd->BGRssiOffset1 < -10) || (pAd->BGRssiOffset1 > 10))
		pAd->BGRssiOffset1 = 0;

	/* Validate 11b/g RSSI_2 offset. */
	if ((pAd->BGRssiOffset2 < -10) || (pAd->BGRssiOffset2 > 10))
		pAd->BGRssiOffset2 = 0;

	RT28xx_EEPROM_READ16(pAd, EEPROM_RSSI_A_OFFSET, value);
	pAd->ARssiOffset0 = value & 0x00ff;
	pAd->ARssiOffset1 = (value >> 8);
	RT28xx_EEPROM_READ16(pAd, (EEPROM_RSSI_A_OFFSET + 2), value);
	pAd->ARssiOffset2 = value & 0x00ff;
	pAd->ALNAGain2 = (value >> 8);

	if (((u8)pAd->ALNAGain1 == 0xFF) || (pAd->ALNAGain1 == 0x00))
		pAd->ALNAGain1 = pAd->ALNAGain0;
	if (((u8)pAd->ALNAGain2 == 0xFF) || (pAd->ALNAGain2 == 0x00))
		pAd->ALNAGain2 = pAd->ALNAGain0;

	/* Validate 11a RSSI_0 offset. */
	if ((pAd->ARssiOffset0 < -10) || (pAd->ARssiOffset0 > 10))
		pAd->ARssiOffset0 = 0;

	/* Validate 11a RSSI_1 offset. */
	if ((pAd->ARssiOffset1 < -10) || (pAd->ARssiOffset1 > 10))
		pAd->ARssiOffset1 = 0;

	/*Validate 11a RSSI_2 offset. */
	if ((pAd->ARssiOffset2 < -10) || (pAd->ARssiOffset2 > 10))
		pAd->ARssiOffset2 = 0;

#ifdef RT30xx
	/* */
	/* Get TX mixer gain setting */
	/* 0xff are invalid value */
	/* Note: RT30xX default value is 0x00 and will program to RF_R17 only when this value is not zero. */
	/*       RT359X default value is 0x02 */
	/* */
	if (IS_RT30xx(pAd) || IS_RT3572(pAd)) {
		RT28xx_EEPROM_READ16(pAd, EEPROM_TXMIXER_GAIN_2_4G, value);
		pAd->TxMixerGain24G = 0;
		value &= 0x00ff;
		if (value != 0xff) {
			value &= 0x07;
			pAd->TxMixerGain24G = (u8)value;
		}
	}
#endif /* RT30xx // */

	/* */
	/* Get LED Setting. */
	/* */
	RT28xx_EEPROM_READ16(pAd, 0x3a, value);
	pAd->LedCntl.word = (value >> 8);
	RT28xx_EEPROM_READ16(pAd, EEPROM_LED1_OFFSET, value);
	pAd->Led1 = value;
	RT28xx_EEPROM_READ16(pAd, EEPROM_LED2_OFFSET, value);
	pAd->Led2 = value;
	RT28xx_EEPROM_READ16(pAd, EEPROM_LED3_OFFSET, value);
	pAd->Led3 = value;

	RTMPReadTxPwrPerRate(pAd);

#ifdef RT30xx
#ifdef RTMP_EFUSE_SUPPORT
	RtmpEfuseSupportCheck(pAd);
#endif /* RTMP_EFUSE_SUPPORT // */
#endif /* RT30xx // */

	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICReadEEPROMParameters\n"));
}

/*
	========================================================================

	Routine Description:
		Set default value from EEPROM

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
void NICInitAsicFromEEPROM(struct rt_rtmp_adapter *pAd)
{
	u32 data = 0;
	u8 BBPR1 = 0;
	u16 i;
/*      EEPROM_ANTENNA_STRUC    Antenna; */
	EEPROM_NIC_CONFIG2_STRUC NicConfig2;
	u8 BBPR3 = 0;

	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitAsicFromEEPROM\n"));
	for (i = 3; i < NUM_EEPROM_BBP_PARMS; i++) {
		u8 BbpRegIdx, BbpValue;

		if ((pAd->EEPROMDefaultValue[i] != 0xFFFF)
		    && (pAd->EEPROMDefaultValue[i] != 0)) {
			BbpRegIdx = (u8)(pAd->EEPROMDefaultValue[i] >> 8);
			BbpValue = (u8)(pAd->EEPROMDefaultValue[i] & 0xff);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BbpRegIdx, BbpValue);
		}
	}

	NicConfig2.word = pAd->EEPROMDefaultValue[1];

	{
		if ((NicConfig2.word & 0x00ff) == 0xff) {
			NicConfig2.word &= 0xff00;
		}

		if ((NicConfig2.word >> 8) == 0xff) {
			NicConfig2.word &= 0x00ff;
		}
	}

	/* Save the antenna for future use */
	pAd->NicConfig2.word = NicConfig2.word;

#ifdef RT30xx
	/* set default antenna as main */
	if (pAd->RfIcType == RFIC_3020)
		AsicSetRxAnt(pAd, pAd->RxAnt.Pair1PrimaryRxAnt);
#endif /* RT30xx // */

	/* */
	/* Send LED Setting to MCU. */
	/* */
	if (pAd->LedCntl.word == 0xFF) {
		pAd->LedCntl.word = 0x01;
		pAd->Led1 = 0x5555;
		pAd->Led2 = 0x2221;

#ifdef RTMP_MAC_PCI
		pAd->Led3 = 0xA9F8;
#endif /* RTMP_MAC_PCI // */
#ifdef RTMP_MAC_USB
		pAd->Led3 = 0x5627;
#endif /* RTMP_MAC_USB // */
	}

	AsicSendCommandToMcu(pAd, 0x52, 0xff, (u8)pAd->Led1,
			     (u8)(pAd->Led1 >> 8));
	AsicSendCommandToMcu(pAd, 0x53, 0xff, (u8)pAd->Led2,
			     (u8)(pAd->Led2 >> 8));
	AsicSendCommandToMcu(pAd, 0x54, 0xff, (u8)pAd->Led3,
			     (u8)(pAd->Led3 >> 8));
	AsicSendCommandToMcu(pAd, 0x51, 0xff, 0, pAd->LedCntl.field.Polarity);

	pAd->LedIndicatorStrength = 0xFF;
	RTMPSetSignalLED(pAd, -100);	/* Force signal strength Led to be turned off, before link up */

	{
		/* Read Hardware controlled Radio state enable bit */
		if (NicConfig2.field.HardwareRadioControl == 1) {
			pAd->StaCfg.bHardwareRadio = TRUE;

			/* Read GPIO pin2 as Hardware controlled radio state */
			RTMP_IO_READ32(pAd, GPIO_CTRL_CFG, &data);
			if ((data & 0x04) == 0) {
				pAd->StaCfg.bHwRadio = FALSE;
				pAd->StaCfg.bRadio = FALSE;
/*                              RTMP_IO_WRITE32(pAd, PWR_PIN_CFG, 0x00001818); */
				RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);
			}
		} else
			pAd->StaCfg.bHardwareRadio = FALSE;

		if (pAd->StaCfg.bRadio == FALSE) {
			RTMPSetLED(pAd, LED_RADIO_OFF);
		} else {
			RTMPSetLED(pAd, LED_RADIO_ON);
#ifdef RTMP_MAC_PCI
#ifdef RT3090
			AsicSendCommandToMcu(pAd, 0x30, PowerRadioOffCID, 0xff,
					     0x02);
			AsicCheckCommanOk(pAd, PowerRadioOffCID);
#endif /* RT3090 // */
#ifndef RT3090
			AsicSendCommandToMcu(pAd, 0x30, 0xff, 0xff, 0x02);
#endif /* RT3090 // */
			AsicSendCommandToMcu(pAd, 0x31, PowerWakeCID, 0x00,
					     0x00);
			/* 2-1. wait command ok. */
			AsicCheckCommanOk(pAd, PowerWakeCID);
#endif /* RTMP_MAC_PCI // */
		}
	}

#ifdef RTMP_MAC_PCI
#ifdef RT30xx
	if (IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd)) {
		struct rt_rtmp_chip_op *pChipOps = &pAd->chipOps;
		if (pChipOps->AsicReverseRfFromSleepMode)
			pChipOps->AsicReverseRfFromSleepMode(pAd);
	}
	/* 3090 MCU Wakeup command needs more time to be stable. */
	/* Before stable, don't issue other MCU command to prevent from firmware error. */

	if ((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
	    && IS_VERSION_AFTER_F(pAd)
	    && (pAd->StaCfg.PSControl.field.rt30xxPowerMode == 3)
	    && (pAd->StaCfg.PSControl.field.EnableNewPS == TRUE)) {
		DBGPRINT(RT_DEBUG_TRACE, ("%s, release Mcu Lock\n", __func__));
		RTMP_SEM_LOCK(&pAd->McuCmdLock);
		pAd->brt30xxBanMcuCmd = FALSE;
		RTMP_SEM_UNLOCK(&pAd->McuCmdLock);
	}
#endif /* RT30xx // */
#endif /* RTMP_MAC_PCI // */

	/* Turn off patching for cardbus controller */
	if (NicConfig2.field.CardbusAcceleration == 1) {
/*              pAd->bTest1 = TRUE; */
	}

	if (NicConfig2.field.DynamicTxAgcControl == 1)
		pAd->bAutoTxAgcA = pAd->bAutoTxAgcG = TRUE;
	else
		pAd->bAutoTxAgcA = pAd->bAutoTxAgcG = FALSE;
	/* */
	/* Since BBP has been progamed, to make sure BBP setting will be */
	/* upate inside of AsicAntennaSelect, so reset to UNKNOWN_BAND! */
	/* */
	pAd->CommonCfg.BandState = UNKNOWN_BAND;

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

	{
		/* Handle the difference when 1T */
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R1, &BBPR1);
		if (pAd->Antenna.field.TxPath == 1) {
			BBPR1 &= (~0x18);
		}
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R1, BBPR1);

		DBGPRINT(RT_DEBUG_TRACE,
			 ("Use Hw Radio Control Pin=%d; if used Pin=%d;\n",
			  pAd->CommonCfg.bHardwareRadio,
			  pAd->CommonCfg.bHardwareRadio));
	}

#ifdef RTMP_MAC_USB
#ifdef RT30xx
	/* update registers from EEPROM for RT3071 or later(3572/3592). */

	if (IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd)) {
		u8 RegIdx, RegValue;
		u16 value;

		/* after RT3071, write BBP from EEPROM 0xF0 to 0x102 */
		for (i = 0xF0; i <= 0x102; i = i + 2) {
			value = 0xFFFF;
			RT28xx_EEPROM_READ16(pAd, i, value);
			if ((value != 0xFFFF) && (value != 0)) {
				RegIdx = (u8)(value >> 8);
				RegValue = (u8)(value & 0xff);
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, RegIdx,
							     RegValue);
				DBGPRINT(RT_DEBUG_TRACE,
					 ("Update BBP Registers from EEPROM(0x%0x), BBP(0x%x) = 0x%x\n",
					  i, RegIdx, RegValue));
			}
		}

		/* after RT3071, write RF from EEPROM 0x104 to 0x116 */
		for (i = 0x104; i <= 0x116; i = i + 2) {
			value = 0xFFFF;
			RT28xx_EEPROM_READ16(pAd, i, value);
			if ((value != 0xFFFF) && (value != 0)) {
				RegIdx = (u8)(value >> 8);
				RegValue = (u8)(value & 0xff);
				RT30xxWriteRFRegister(pAd, RegIdx, RegValue);
				DBGPRINT(RT_DEBUG_TRACE,
					 ("Update RF Registers from EEPROM0x%x), BBP(0x%x) = 0x%x\n",
					  i, RegIdx, RegValue));
			}
		}
	}
#endif /* RT30xx // */
#endif /* RTMP_MAC_USB // */

	DBGPRINT(RT_DEBUG_TRACE,
		 ("TxPath = %d, RxPath = %d, RFIC=%d, Polar+LED mode=%x\n",
		  pAd->Antenna.field.TxPath, pAd->Antenna.field.RxPath,
		  pAd->RfIcType, pAd->LedCntl.word));
	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitAsicFromEEPROM\n"));
}

/*
	========================================================================

	Routine Description:
		Initialize NIC hardware

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
int NICInitializeAdapter(struct rt_rtmp_adapter *pAd, IN BOOLEAN bHardReset)
{
	int Status = NDIS_STATUS_SUCCESS;
	WPDMA_GLO_CFG_STRUC GloCfg;
#ifdef RTMP_MAC_PCI
	u32 Value;
	DELAY_INT_CFG_STRUC IntCfg;
#endif /* RTMP_MAC_PCI // */
/*      INT_MASK_CSR_STRUC              IntMask; */
	unsigned long i = 0, j = 0;
	AC_TXOP_CSR0_STRUC csr0;

	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitializeAdapter\n"));

	/* 3. Set DMA global configuration except TX_DMA_EN and RX_DMA_EN bits: */
retry:
	i = 0;
	do {
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0)
		    && (GloCfg.field.RxDMABusy == 0))
			break;

		RTMPusecDelay(1000);
		i++;
	} while (i < 100);
	DBGPRINT(RT_DEBUG_TRACE,
		 ("<== DMA offset 0x208 = 0x%x\n", GloCfg.word));
	GloCfg.word &= 0xff0;
	GloCfg.field.EnTXWriteBackDDONE = 1;
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);

	/* Record HW Beacon offset */
	pAd->BeaconOffset[0] = HW_BEACON_BASE0;
	pAd->BeaconOffset[1] = HW_BEACON_BASE1;
	pAd->BeaconOffset[2] = HW_BEACON_BASE2;
	pAd->BeaconOffset[3] = HW_BEACON_BASE3;
	pAd->BeaconOffset[4] = HW_BEACON_BASE4;
	pAd->BeaconOffset[5] = HW_BEACON_BASE5;
	pAd->BeaconOffset[6] = HW_BEACON_BASE6;
	pAd->BeaconOffset[7] = HW_BEACON_BASE7;

	/* */
	/* write all shared Ring's base address into ASIC */
	/* */

	/* asic simulation sequence put this ahead before loading firmware. */
	/* pbf hardware reset */
#ifdef RTMP_MAC_PCI
	RTMP_IO_WRITE32(pAd, WPDMA_RST_IDX, 0x1003f);	/* 0x10000 for reset rx, 0x3f resets all 6 tx rings. */
	RTMP_IO_WRITE32(pAd, PBF_SYS_CTRL, 0xe1f);
	RTMP_IO_WRITE32(pAd, PBF_SYS_CTRL, 0xe00);
#endif /* RTMP_MAC_PCI // */

	/* Initialze ASIC for TX & Rx operation */
	if (NICInitializeAsic(pAd, bHardReset) != NDIS_STATUS_SUCCESS) {
		if (j++ == 0) {
			NICLoadFirmware(pAd);
			goto retry;
		}
		return NDIS_STATUS_FAILURE;
	}

#ifdef RTMP_MAC_PCI
	/* Write AC_BK base address register */
	Value =
	    RTMP_GetPhysicalAddressLow(pAd->TxRing[QID_AC_BK].Cell[0].AllocPa);
	RTMP_IO_WRITE32(pAd, TX_BASE_PTR1, Value);
	DBGPRINT(RT_DEBUG_TRACE, ("--> TX_BASE_PTR1 : 0x%x\n", Value));

	/* Write AC_BE base address register */
	Value =
	    RTMP_GetPhysicalAddressLow(pAd->TxRing[QID_AC_BE].Cell[0].AllocPa);
	RTMP_IO_WRITE32(pAd, TX_BASE_PTR0, Value);
	DBGPRINT(RT_DEBUG_TRACE, ("--> TX_BASE_PTR0 : 0x%x\n", Value));

	/* Write AC_VI base address register */
	Value =
	    RTMP_GetPhysicalAddressLow(pAd->TxRing[QID_AC_VI].Cell[0].AllocPa);
	RTMP_IO_WRITE32(pAd, TX_BASE_PTR2, Value);
	DBGPRINT(RT_DEBUG_TRACE, ("--> TX_BASE_PTR2 : 0x%x\n", Value));

	/* Write AC_VO base address register */
	Value =
	    RTMP_GetPhysicalAddressLow(pAd->TxRing[QID_AC_VO].Cell[0].AllocPa);
	RTMP_IO_WRITE32(pAd, TX_BASE_PTR3, Value);
	DBGPRINT(RT_DEBUG_TRACE, ("--> TX_BASE_PTR3 : 0x%x\n", Value));

	/* Write MGMT_BASE_CSR register */
	Value = RTMP_GetPhysicalAddressLow(pAd->MgmtRing.Cell[0].AllocPa);
	RTMP_IO_WRITE32(pAd, TX_BASE_PTR5, Value);
	DBGPRINT(RT_DEBUG_TRACE, ("--> TX_BASE_PTR5 : 0x%x\n", Value));

	/* Write RX_BASE_CSR register */
	Value = RTMP_GetPhysicalAddressLow(pAd->RxRing.Cell[0].AllocPa);
	RTMP_IO_WRITE32(pAd, RX_BASE_PTR, Value);
	DBGPRINT(RT_DEBUG_TRACE, ("--> RX_BASE_PTR : 0x%x\n", Value));

	/* Init RX Ring index pointer */
	pAd->RxRing.RxSwReadIdx = 0;
	pAd->RxRing.RxCpuIdx = RX_RING_SIZE - 1;
	RTMP_IO_WRITE32(pAd, RX_CRX_IDX, pAd->RxRing.RxCpuIdx);

	/* Init TX rings index pointer */
	{
		for (i = 0; i < NUM_OF_TX_RING; i++) {
			pAd->TxRing[i].TxSwFreeIdx = 0;
			pAd->TxRing[i].TxCpuIdx = 0;
			RTMP_IO_WRITE32(pAd, (TX_CTX_IDX0 + i * 0x10),
					pAd->TxRing[i].TxCpuIdx);
		}
	}

	/* init MGMT ring index pointer */
	pAd->MgmtRing.TxSwFreeIdx = 0;
	pAd->MgmtRing.TxCpuIdx = 0;
	RTMP_IO_WRITE32(pAd, TX_MGMTCTX_IDX, pAd->MgmtRing.TxCpuIdx);

	/* */
	/* set each Ring's SIZE  into ASIC. Descriptor Size is fixed by design. */
	/* */

	/* Write TX_RING_CSR0 register */
	Value = TX_RING_SIZE;
	RTMP_IO_WRITE32(pAd, TX_MAX_CNT0, Value);
	RTMP_IO_WRITE32(pAd, TX_MAX_CNT1, Value);
	RTMP_IO_WRITE32(pAd, TX_MAX_CNT2, Value);
	RTMP_IO_WRITE32(pAd, TX_MAX_CNT3, Value);
	RTMP_IO_WRITE32(pAd, TX_MAX_CNT4, Value);
	Value = MGMT_RING_SIZE;
	RTMP_IO_WRITE32(pAd, TX_MGMTMAX_CNT, Value);

	/* Write RX_RING_CSR register */
	Value = RX_RING_SIZE;
	RTMP_IO_WRITE32(pAd, RX_MAX_CNT, Value);
#endif /* RTMP_MAC_PCI // */

	/* WMM parameter */
	csr0.word = 0;
	RTMP_IO_WRITE32(pAd, WMM_TXOP0_CFG, csr0.word);
	if (pAd->CommonCfg.PhyMode == PHY_11B) {
		csr0.field.Ac0Txop = 192;	/* AC_VI: 192*32us ~= 6ms */
		csr0.field.Ac1Txop = 96;	/* AC_VO: 96*32us  ~= 3ms */
	} else {
		csr0.field.Ac0Txop = 96;	/* AC_VI: 96*32us ~= 3ms */
		csr0.field.Ac1Txop = 48;	/* AC_VO: 48*32us ~= 1.5ms */
	}
	RTMP_IO_WRITE32(pAd, WMM_TXOP1_CFG, csr0.word);

#ifdef RTMP_MAC_PCI
	/* 3. Set DMA global configuration except TX_DMA_EN and RX_DMA_EN bits: */
	i = 0;
	do {
		RTMP_IO_READ32(pAd, WPDMA_GLO_CFG, &GloCfg.word);
		if ((GloCfg.field.TxDMABusy == 0)
		    && (GloCfg.field.RxDMABusy == 0))
			break;

		RTMPusecDelay(1000);
		i++;
	} while (i < 100);

	GloCfg.word &= 0xff0;
	GloCfg.field.EnTXWriteBackDDONE = 1;
	RTMP_IO_WRITE32(pAd, WPDMA_GLO_CFG, GloCfg.word);

	IntCfg.word = 0;
	RTMP_IO_WRITE32(pAd, DELAY_INT_CFG, IntCfg.word);
#endif /* RTMP_MAC_PCI // */

	/* reset action */
	/* Load firmware */
	/*  Status = NICLoadFirmware(pAd); */

	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitializeAdapter\n"));
	return Status;
}

/*
	========================================================================

	Routine Description:
		Initialize ASIC

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
int NICInitializeAsic(struct rt_rtmp_adapter *pAd, IN BOOLEAN bHardReset)
{
	unsigned long Index = 0;
	u8 R0 = 0xff;
	u32 MacCsr12 = 0, Counter = 0;
#ifdef RTMP_MAC_USB
	u32 MacCsr0 = 0;
	int Status;
	u8 Value = 0xff;
#endif /* RTMP_MAC_USB // */
#ifdef RT30xx
	u8 bbpreg = 0;
	u8 RFValue = 0;
#endif /* RT30xx // */
	u16 KeyIdx;
	int i, apidx;

	DBGPRINT(RT_DEBUG_TRACE, ("--> NICInitializeAsic\n"));

#ifdef RTMP_MAC_PCI
	RTMP_IO_WRITE32(pAd, PWR_PIN_CFG, 0x3);	/* To fix driver disable/enable hang issue when radio off */
	if (bHardReset == TRUE) {
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x3);
	} else
		RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x1);

	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x0);
	/* Initialize MAC register to default value */
	for (Index = 0; Index < NUM_MAC_REG_PARMS; Index++) {
		RTMP_IO_WRITE32(pAd, MACRegTable[Index].Register,
				MACRegTable[Index].Value);
	}

	{
		for (Index = 0; Index < NUM_STA_MAC_REG_PARMS; Index++) {
			RTMP_IO_WRITE32(pAd, STAMACRegTable[Index].Register,
					STAMACRegTable[Index].Value);
		}
	}
#endif /* RTMP_MAC_PCI // */
#ifdef RTMP_MAC_USB
	/* */
	/* Make sure MAC gets ready after NICLoadFirmware(). */
	/* */
	Index = 0;

	/*To avoid hang-on issue when interface up in kernel 2.4, */
	/*we use a local variable "MacCsr0" instead of using "pAd->MACVersion" directly. */
	do {
		RTMP_IO_READ32(pAd, MAC_CSR0, &MacCsr0);

		if ((MacCsr0 != 0x00) && (MacCsr0 != 0xFFFFFFFF))
			break;

		RTMPusecDelay(10);
	} while (Index++ < 100);

	pAd->MACVersion = MacCsr0;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("MAC_CSR0  [ Ver:Rev=0x%08x]\n", pAd->MACVersion));
	/* turn on bit13 (set to zero) after rt2860D. This is to solve high-current issue. */
	RTMP_IO_READ32(pAd, PBF_SYS_CTRL, &MacCsr12);
	MacCsr12 &= (~0x2000);
	RTMP_IO_WRITE32(pAd, PBF_SYS_CTRL, MacCsr12);

	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x3);
	RTMP_IO_WRITE32(pAd, USB_DMA_CFG, 0x0);
	Status = RTUSBVenderReset(pAd);

	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x0);

	/* Initialize MAC register to default value */
	for (Index = 0; Index < NUM_MAC_REG_PARMS; Index++) {
#ifdef RT30xx
		if ((MACRegTable[Index].Register == TX_SW_CFG0)
		    && (IS_RT3070(pAd) || IS_RT3071(pAd) || IS_RT3572(pAd)
			|| IS_RT3090(pAd) || IS_RT3390(pAd))) {
			MACRegTable[Index].Value = 0x00000400;
		}
#endif /* RT30xx // */
		RTMP_IO_WRITE32(pAd, (u16)MACRegTable[Index].Register,
				MACRegTable[Index].Value);
	}

	{
		for (Index = 0; Index < NUM_STA_MAC_REG_PARMS; Index++) {
			RTMP_IO_WRITE32(pAd,
					(u16)STAMACRegTable[Index].Register,
					STAMACRegTable[Index].Value);
		}
	}
#endif /* RTMP_MAC_USB // */

#ifdef RT30xx
	/* Initialize RT3070 serial MAC registers which is different from RT2870 serial */
	if (IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd)) {
		RTMP_IO_WRITE32(pAd, TX_SW_CFG1, 0);

		/* RT3071 version E has fixed this issue */
		if ((pAd->MACVersion & 0xffff) < 0x0211) {
			if (pAd->NicConfig2.field.DACTestBit == 1) {
				RTMP_IO_WRITE32(pAd, TX_SW_CFG2, 0x2C);	/* To fix throughput drop drastically */
			} else {
				RTMP_IO_WRITE32(pAd, TX_SW_CFG2, 0x0F);	/* To fix throughput drop drastically */
			}
		} else {
			RTMP_IO_WRITE32(pAd, TX_SW_CFG2, 0x0);
		}
	} else if (IS_RT3070(pAd)) {
		if (((pAd->MACVersion & 0xffff) < 0x0201)) {
			RTMP_IO_WRITE32(pAd, TX_SW_CFG1, 0);
			RTMP_IO_WRITE32(pAd, TX_SW_CFG2, 0x2C);	/* To fix throughput drop drastically */
		} else {
			RTMP_IO_WRITE32(pAd, TX_SW_CFG2, 0);
		}
	}
#endif /* RT30xx // */

	/* */
	/* Before program BBP, we need to wait BBP/RF get wake up. */
	/* */
	Index = 0;
	do {
		RTMP_IO_READ32(pAd, MAC_STATUS_CFG, &MacCsr12);

		if ((MacCsr12 & 0x03) == 0)	/* if BB.RF is stable */
			break;

		DBGPRINT(RT_DEBUG_TRACE,
			 ("Check MAC_STATUS_CFG  = Busy = %x\n", MacCsr12));
		RTMPusecDelay(1000);
	} while (Index++ < 100);

	/* The commands to firmware should be after these commands, these commands will init firmware */
	/* PCI and USB are not the same because PCI driver needs to wait for PCI bus ready */
	RTMP_IO_WRITE32(pAd, H2M_BBP_AGENT, 0);	/* initialize BBP R/W access agent */
	RTMP_IO_WRITE32(pAd, H2M_MAILBOX_CSR, 0);
#ifdef RT3090
	/*2008/11/28:KH add to fix the dead rf frequency offset bug<-- */
	AsicSendCommandToMcu(pAd, 0x72, 0, 0, 0);
	/*2008/11/28:KH add to fix the dead rf frequency offset bug--> */
#endif /* RT3090 // */
	RTMPusecDelay(1000);

	/* Read BBP register, make sure BBP is up and running before write new data */
	Index = 0;
	do {
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R0, &R0);
		DBGPRINT(RT_DEBUG_TRACE, ("BBP version = %x\n", R0));
	} while ((++Index < 20) && ((R0 == 0xff) || (R0 == 0x00)));
	/*ASSERT(Index < 20); //this will cause BSOD on Check-build driver */

	if ((R0 == 0xff) || (R0 == 0x00))
		return NDIS_STATUS_FAILURE;

	/* Initialize BBP register to default value */
	for (Index = 0; Index < NUM_BBP_REG_PARMS; Index++) {
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBPRegTable[Index].Register,
					     BBPRegTable[Index].Value);
	}

#ifdef RTMP_MAC_PCI
	/* TODO: shiang, check MACVersion, currently, rbus-based chip use this. */
	if (pAd->MACVersion == 0x28720200) {
		/*u8 value; */
		unsigned long value2;

		/*disable MLD by Bruce 20080704 */
		/*BBP_IO_READ8_BY_REG_ID(pAd, BBP_R105, &value); */
		/*BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R105, value | 4); */

		/*Maximum PSDU length from 16K to 32K bytes */
		RTMP_IO_READ32(pAd, MAX_LEN_CFG, &value2);
		value2 &= ~(0x3 << 12);
		value2 |= (0x2 << 12);
		RTMP_IO_WRITE32(pAd, MAX_LEN_CFG, value2);
	}
#endif /* RTMP_MAC_PCI // */

	/* for rt2860E and after, init BBP_R84 with 0x19. This is for extension channel overlapping IOT. */
	/* RT3090 should not program BBP R84 to 0x19, otherwise TX will block. */
	/*3070/71/72,3090,3090A( are included in RT30xx),3572,3390 */
	if (((pAd->MACVersion & 0xffff) != 0x0101)
	    && !(IS_RT30xx(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd)))
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R84, 0x19);

#ifdef RT30xx
/* add by johnli, RF power sequence setup */
	if (IS_RT30xx(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd)) {	/*update for RT3070/71/72/90/91/92,3572,3390. */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R79, 0x13);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R80, 0x05);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R81, 0x33);
	}

	if (IS_RT3090(pAd) || IS_RT3390(pAd))	/* RT309x, RT3071/72 */
	{
		/* enable DC filter */
		if ((pAd->MACVersion & 0xffff) >= 0x0211) {
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R103, 0xc0);
		}
		/* improve power consumption */
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R138, &bbpreg);
		if (pAd->Antenna.field.TxPath == 1) {
			/* turn off tx DAC_1 */
			bbpreg = (bbpreg | 0x20);
		}

		if (pAd->Antenna.field.RxPath == 1) {
			/* turn off tx ADC_1 */
			bbpreg &= (~0x2);
		}
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R138, bbpreg);

		/* improve power consumption in RT3071 Ver.E */
		if ((pAd->MACVersion & 0xffff) >= 0x0211) {
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R31, &bbpreg);
			bbpreg &= (~0x3);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R31, bbpreg);
		}
	} else if (IS_RT3070(pAd)) {
		if ((pAd->MACVersion & 0xffff) >= 0x0201) {
			/* enable DC filter */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R103, 0xc0);

			/* improve power consumption in RT3070 Ver.F */
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R31, &bbpreg);
			bbpreg &= (~0x3);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R31, bbpreg);
		}
		/* TX_LO1_en, RF R17 register Bit 3 to 0 */
		RT30xxReadRFRegister(pAd, RF_R17, &RFValue);
		RFValue &= (~0x08);
		/* to fix rx long range issue */
		if (pAd->NicConfig2.field.ExternalLNAForG == 0) {
			RFValue |= 0x20;
		}
		/* set RF_R17_bit[2:0] equal to EEPROM setting at 0x48h */
		if (pAd->TxMixerGain24G >= 1) {
			RFValue &= (~0x7);	/* clean bit [2:0] */
			RFValue |= pAd->TxMixerGain24G;
		}
		RT30xxWriteRFRegister(pAd, RF_R17, RFValue);
	}
/* end johnli */
#endif /* RT30xx // */

	if (pAd->MACVersion == 0x28600100) {
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x16);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x12);
	}

	if (pAd->MACVersion >= RALINK_2880E_VERSION && pAd->MACVersion < RALINK_3070_VERSION)	/* 3*3 */
	{
		/* enlarge MAX_LEN_CFG */
		u32 csr;
		RTMP_IO_READ32(pAd, MAX_LEN_CFG, &csr);
		csr &= 0xFFF;
		csr |= 0x2000;
		RTMP_IO_WRITE32(pAd, MAX_LEN_CFG, csr);
	}
#ifdef RTMP_MAC_USB
	{
		u8 MAC_Value[] =
		    { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0 };

		/*Initialize WCID table */
		Value = 0xff;
		for (Index = 0; Index < 254; Index++) {
			RTUSBMultiWrite(pAd,
					(u16)(MAC_WCID_BASE + Index * 8),
					MAC_Value, 8);
		}
	}
#endif /* RTMP_MAC_USB // */

	/* Add radio off control */
	{
		if (pAd->StaCfg.bRadio == FALSE) {
/*                      RTMP_IO_WRITE32(pAd, PWR_PIN_CFG, 0x00001818); */
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF);
			DBGPRINT(RT_DEBUG_TRACE, ("Set Radio Off\n"));
		}
	}

	/* Clear raw counters */
	RTMP_IO_READ32(pAd, RX_STA_CNT0, &Counter);
	RTMP_IO_READ32(pAd, RX_STA_CNT1, &Counter);
	RTMP_IO_READ32(pAd, RX_STA_CNT2, &Counter);
	RTMP_IO_READ32(pAd, TX_STA_CNT0, &Counter);
	RTMP_IO_READ32(pAd, TX_STA_CNT1, &Counter);
	RTMP_IO_READ32(pAd, TX_STA_CNT2, &Counter);

	/* ASIC will keep garbage value after boot */
	/* Clear all shared key table when initial */
	/* This routine can be ignored in radio-ON/OFF operation. */
	if (bHardReset) {
		for (KeyIdx = 0; KeyIdx < 4; KeyIdx++) {
			RTMP_IO_WRITE32(pAd, SHARED_KEY_MODE_BASE + 4 * KeyIdx,
					0);
		}

		/* Clear all pairwise key table when initial */
		for (KeyIdx = 0; KeyIdx < 256; KeyIdx++) {
			RTMP_IO_WRITE32(pAd,
					MAC_WCID_ATTRIBUTE_BASE +
					(KeyIdx * HW_WCID_ATTRI_SIZE), 1);
		}
	}
	/* assert HOST ready bit */
/*  RTMP_IO_WRITE32(pAd, MAC_CSR1, 0x0); // 2004-09-14 asked by Mark */
/*  RTMP_IO_WRITE32(pAd, MAC_CSR1, 0x4); */

	/* It isn't necessary to clear this space when not hard reset. */
	if (bHardReset == TRUE) {
		/* clear all on-chip BEACON frame space */
		for (apidx = 0; apidx < HW_BEACON_MAX_COUNT; apidx++) {
			for (i = 0; i < HW_BEACON_OFFSET >> 2; i += 4)
				RTMP_IO_WRITE32(pAd,
						pAd->BeaconOffset[apidx] + i,
						0x00);
		}
	}
#ifdef RTMP_MAC_USB
	AsicDisableSync(pAd);
	/* Clear raw counters */
	RTMP_IO_READ32(pAd, RX_STA_CNT0, &Counter);
	RTMP_IO_READ32(pAd, RX_STA_CNT1, &Counter);
	RTMP_IO_READ32(pAd, RX_STA_CNT2, &Counter);
	RTMP_IO_READ32(pAd, TX_STA_CNT0, &Counter);
	RTMP_IO_READ32(pAd, TX_STA_CNT1, &Counter);
	RTMP_IO_READ32(pAd, TX_STA_CNT2, &Counter);
	/* Default PCI clock cycle per ms is different as default setting, which is based on PCI. */
	RTMP_IO_READ32(pAd, USB_CYC_CFG, &Counter);
	Counter &= 0xffffff00;
	Counter |= 0x000001e;
	RTMP_IO_WRITE32(pAd, USB_CYC_CFG, Counter);
#endif /* RTMP_MAC_USB // */

	{
		/* for rt2860E and after, init TXOP_CTRL_CFG with 0x583f. This is for extension channel overlapping IOT. */
		if ((pAd->MACVersion & 0xffff) != 0x0101)
			RTMP_IO_WRITE32(pAd, TXOP_CTRL_CFG, 0x583f);
	}

	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICInitializeAsic\n"));
	return NDIS_STATUS_SUCCESS;
}

/*
	========================================================================

	Routine Description:
		Reset NIC Asics

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:
		Reset NIC to initial state AS IS system boot up time.

	========================================================================
*/
void NICIssueReset(struct rt_rtmp_adapter *pAd)
{
	u32 Value = 0;
	DBGPRINT(RT_DEBUG_TRACE, ("--> NICIssueReset\n"));

	/* Abort Tx, prevent ASIC from writing to Host memory */
	/*RTMP_IO_WRITE32(pAd, TX_CNTL_CSR, 0x001f0000); */

	/* Disable Rx, register value supposed will remain after reset */
	RTMP_IO_READ32(pAd, MAC_SYS_CTRL, &Value);
	Value &= (0xfffffff3);
	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, Value);

	/* Issue reset and clear from reset state */
	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x03);	/* 2004-09-17 change from 0x01 */
	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x00);

	DBGPRINT(RT_DEBUG_TRACE, ("<-- NICIssueReset\n"));
}

/*
	========================================================================

	Routine Description:
		Check ASIC registers and find any reason the system might hang

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	========================================================================
*/
BOOLEAN NICCheckForHang(struct rt_rtmp_adapter *pAd)
{
	return (FALSE);
}

void NICUpdateFifoStaCounters(struct rt_rtmp_adapter *pAd)
{
	TX_STA_FIFO_STRUC StaFifo;
	struct rt_mac_table_entry *pEntry;
	u8 i = 0;
	u8 pid = 0, wcid = 0;
	char reTry;
	u8 succMCS;

	do {
		RTMP_IO_READ32(pAd, TX_STA_FIFO, &StaFifo.word);

		if (StaFifo.field.bValid == 0)
			break;

		wcid = (u8)StaFifo.field.wcid;

		/* ignore NoACK and MGMT frame use 0xFF as WCID */
		if ((StaFifo.field.TxAckRequired == 0)
		    || (wcid >= MAX_LEN_OF_MAC_TABLE)) {
			i++;
			continue;
		}

		/* PID store Tx MCS Rate */
		pid = (u8)StaFifo.field.PidType;

		pEntry = &pAd->MacTab.Content[wcid];

		pEntry->DebugFIFOCount++;

		if (StaFifo.field.TxBF)	/* 3*3 */
			pEntry->TxBFCount++;

		if (!StaFifo.field.TxSuccess) {
			pEntry->FIFOCount++;
			pEntry->OneSecTxFailCount++;

			if (pEntry->FIFOCount >= 1) {
				DBGPRINT(RT_DEBUG_TRACE, ("#"));
				pEntry->NoBADataCountDown = 64;

				if (pEntry->PsMode == PWR_ACTIVE) {
					int tid;
					for (tid = 0; tid < NUM_OF_TID; tid++) {
						BAOriSessionTearDown(pAd,
								     pEntry->
								     Aid, tid,
								     FALSE,
								     FALSE);
					}

					/* Update the continuous transmission counter except PS mode */
					pEntry->ContinueTxFailCnt++;
				} else {
					/* Clear the FIFOCount when sta in Power Save mode. Basically we assume */
					/*     this tx error happened due to sta just go to sleep. */
					pEntry->FIFOCount = 0;
					pEntry->ContinueTxFailCnt = 0;
				}
				/*pEntry->FIFOCount = 0; */
			}
			/*pEntry->bSendBAR = TRUE; */
		} else {
			if ((pEntry->PsMode != PWR_SAVE)
			    && (pEntry->NoBADataCountDown > 0)) {
				pEntry->NoBADataCountDown--;
				if (pEntry->NoBADataCountDown == 0) {
					DBGPRINT(RT_DEBUG_TRACE, ("@\n"));
				}
			}

			pEntry->FIFOCount = 0;
			pEntry->OneSecTxNoRetryOkCount++;
			/* update NoDataIdleCount when sucessful send packet to STA. */
			pEntry->NoDataIdleCount = 0;
			pEntry->ContinueTxFailCnt = 0;
		}

		succMCS = StaFifo.field.SuccessRate & 0x7F;

		reTry = pid - succMCS;

		if (StaFifo.field.TxSuccess) {
			pEntry->TXMCSExpected[pid]++;
			if (pid == succMCS) {
				pEntry->TXMCSSuccessful[pid]++;
			} else {
				pEntry->TXMCSAutoFallBack[pid][succMCS]++;
			}
		} else {
			pEntry->TXMCSFailed[pid]++;
		}

		if (reTry > 0) {
			if ((pid >= 12) && succMCS <= 7) {
				reTry -= 4;
			}
			pEntry->OneSecTxRetryOkCount += reTry;
		}

		i++;
		/* ASIC store 16 stack */
	} while (i < (2 * TX_RING_SIZE));

}

/*
	========================================================================

	Routine Description:
		Read statistical counters from hardware registers and record them
		in software variables for later on query

	Arguments:
		pAd					Pointer to our adapter

	Return Value:
		None

	IRQL = DISPATCH_LEVEL

	========================================================================
*/
void NICUpdateRawCounters(struct rt_rtmp_adapter *pAd)
{
	u32 OldValue;	/*, Value2; */
	/*unsigned long PageSum, OneSecTransmitCount; */
	/*unsigned long TxErrorRatio, Retry, Fail; */
	RX_STA_CNT0_STRUC RxStaCnt0;
	RX_STA_CNT1_STRUC RxStaCnt1;
	RX_STA_CNT2_STRUC RxStaCnt2;
	TX_STA_CNT0_STRUC TxStaCnt0;
	TX_STA_CNT1_STRUC StaTx1;
	TX_STA_CNT2_STRUC StaTx2;
	TX_AGG_CNT_STRUC TxAggCnt;
	TX_AGG_CNT0_STRUC TxAggCnt0;
	TX_AGG_CNT1_STRUC TxAggCnt1;
	TX_AGG_CNT2_STRUC TxAggCnt2;
	TX_AGG_CNT3_STRUC TxAggCnt3;
	TX_AGG_CNT4_STRUC TxAggCnt4;
	TX_AGG_CNT5_STRUC TxAggCnt5;
	TX_AGG_CNT6_STRUC TxAggCnt6;
	TX_AGG_CNT7_STRUC TxAggCnt7;
	struct rt_counter_ralink *pRalinkCounters;

	pRalinkCounters = &pAd->RalinkCounters;

	RTMP_IO_READ32(pAd, RX_STA_CNT0, &RxStaCnt0.word);
	RTMP_IO_READ32(pAd, RX_STA_CNT2, &RxStaCnt2.word);

	{
		RTMP_IO_READ32(pAd, RX_STA_CNT1, &RxStaCnt1.word);
		/* Update RX PLCP error counter */
		pAd->PrivateInfo.PhyRxErrCnt += RxStaCnt1.field.PlcpErr;
		/* Update False CCA counter */
		pAd->RalinkCounters.OneSecFalseCCACnt +=
		    RxStaCnt1.field.FalseCca;
	}

	/* Update FCS counters */
	OldValue = pAd->WlanCounters.FCSErrorCount.u.LowPart;
	pAd->WlanCounters.FCSErrorCount.u.LowPart += (RxStaCnt0.field.CrcErr);	/* >> 7); */
	if (pAd->WlanCounters.FCSErrorCount.u.LowPart < OldValue)
		pAd->WlanCounters.FCSErrorCount.u.HighPart++;

	/* Add FCS error count to private counters */
	pRalinkCounters->OneSecRxFcsErrCnt += RxStaCnt0.field.CrcErr;
	OldValue = pRalinkCounters->RealFcsErrCount.u.LowPart;
	pRalinkCounters->RealFcsErrCount.u.LowPart += RxStaCnt0.field.CrcErr;
	if (pRalinkCounters->RealFcsErrCount.u.LowPart < OldValue)
		pRalinkCounters->RealFcsErrCount.u.HighPart++;

	/* Update Duplicate Rcv check */
	pRalinkCounters->DuplicateRcv += RxStaCnt2.field.RxDupliCount;
	pAd->WlanCounters.FrameDuplicateCount.u.LowPart +=
	    RxStaCnt2.field.RxDupliCount;
	/* Update RX Overflow counter */
	pAd->Counters8023.RxNoBuffer += (RxStaCnt2.field.RxFifoOverflowCount);

	/*pAd->RalinkCounters.RxCount = 0; */
#ifdef RTMP_MAC_USB
	if (pRalinkCounters->RxCount != pAd->watchDogRxCnt) {
		pAd->watchDogRxCnt = pRalinkCounters->RxCount;
		pAd->watchDogRxOverFlowCnt = 0;
	} else {
		if (RxStaCnt2.field.RxFifoOverflowCount)
			pAd->watchDogRxOverFlowCnt++;
		else
			pAd->watchDogRxOverFlowCnt = 0;
	}
#endif /* RTMP_MAC_USB // */

	/*if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED) || */
	/*      (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_TX_RATE_SWITCH_ENABLED) && (pAd->MacTab.Size != 1))) */
	if (!pAd->bUpdateBcnCntDone) {
		/* Update BEACON sent count */
		RTMP_IO_READ32(pAd, TX_STA_CNT0, &TxStaCnt0.word);
		RTMP_IO_READ32(pAd, TX_STA_CNT1, &StaTx1.word);
		RTMP_IO_READ32(pAd, TX_STA_CNT2, &StaTx2.word);
		pRalinkCounters->OneSecBeaconSentCnt +=
		    TxStaCnt0.field.TxBeaconCount;
		pRalinkCounters->OneSecTxRetryOkCount +=
		    StaTx1.field.TxRetransmit;
		pRalinkCounters->OneSecTxNoRetryOkCount +=
		    StaTx1.field.TxSuccess;
		pRalinkCounters->OneSecTxFailCount +=
		    TxStaCnt0.field.TxFailCount;
		pAd->WlanCounters.TransmittedFragmentCount.u.LowPart +=
		    StaTx1.field.TxSuccess;
		pAd->WlanCounters.RetryCount.u.LowPart +=
		    StaTx1.field.TxRetransmit;
		pAd->WlanCounters.FailedCount.u.LowPart +=
		    TxStaCnt0.field.TxFailCount;
	}

	/*if (pAd->bStaFifoTest == TRUE) */
	{
		RTMP_IO_READ32(pAd, TX_AGG_CNT, &TxAggCnt.word);
		RTMP_IO_READ32(pAd, TX_AGG_CNT0, &TxAggCnt0.word);
		RTMP_IO_READ32(pAd, TX_AGG_CNT1, &TxAggCnt1.word);
		RTMP_IO_READ32(pAd, TX_AGG_CNT2, &TxAggCnt2.word);
		RTMP_IO_READ32(pAd, TX_AGG_CNT3, &TxAggCnt3.word);
		RTMP_IO_READ32(pAd, TX_AGG_CNT4, &TxAggCnt4.word);
		RTMP_IO_READ32(pAd, TX_AGG_CNT5, &TxAggCnt5.word);
		RTMP_IO_READ32(pAd, TX_AGG_CNT6, &TxAggCnt6.word);
		RTMP_IO_READ32(pAd, TX_AGG_CNT7, &TxAggCnt7.word);
		pRalinkCounters->TxAggCount += TxAggCnt.field.AggTxCount;
		pRalinkCounters->TxNonAggCount += TxAggCnt.field.NonAggTxCount;
		pRalinkCounters->TxAgg1MPDUCount +=
		    TxAggCnt0.field.AggSize1Count;
		pRalinkCounters->TxAgg2MPDUCount +=
		    TxAggCnt0.field.AggSize2Count;

		pRalinkCounters->TxAgg3MPDUCount +=
		    TxAggCnt1.field.AggSize3Count;
		pRalinkCounters->TxAgg4MPDUCount +=
		    TxAggCnt1.field.AggSize4Count;
		pRalinkCounters->TxAgg5MPDUCount +=
		    TxAggCnt2.field.AggSize5Count;
		pRalinkCounters->TxAgg6MPDUCount +=
		    TxAggCnt2.field.AggSize6Count;

		pRalinkCounters->TxAgg7MPDUCount +=
		    TxAggCnt3.field.AggSize7Count;
		pRalinkCounters->TxAgg8MPDUCount +=
		    TxAggCnt3.field.AggSize8Count;
		pRalinkCounters->TxAgg9MPDUCount +=
		    TxAggCnt4.field.AggSize9Count;
		pRalinkCounters->TxAgg10MPDUCount +=
		    TxAggCnt4.field.AggSize10Count;

		pRalinkCounters->TxAgg11MPDUCount +=
		    TxAggCnt5.field.AggSize11Count;
		pRalinkCounters->TxAgg12MPDUCount +=
		    TxAggCnt5.field.AggSize12Count;
		pRalinkCounters->TxAgg13MPDUCount +=
		    TxAggCnt6.field.AggSize13Count;
		pRalinkCounters->TxAgg14MPDUCount +=
		    TxAggCnt6.field.AggSize14Count;

		pRalinkCounters->TxAgg15MPDUCount +=
		    TxAggCnt7.field.AggSize15Count;
		pRalinkCounters->TxAgg16MPDUCount +=
		    TxAggCnt7.field.AggSize16Count;

		/* Calculate the transmitted A-MPDU count */
		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    TxAggCnt0.field.AggSize1Count;
		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt0.field.AggSize2Count / 2);

		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt1.field.AggSize3Count / 3);
		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt1.field.AggSize4Count / 4);

		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt2.field.AggSize5Count / 5);
		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt2.field.AggSize6Count / 6);

		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt3.field.AggSize7Count / 7);
		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt3.field.AggSize8Count / 8);

		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt4.field.AggSize9Count / 9);
		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt4.field.AggSize10Count / 10);

		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt5.field.AggSize11Count / 11);
		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt5.field.AggSize12Count / 12);

		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt6.field.AggSize13Count / 13);
		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt6.field.AggSize14Count / 14);

		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt7.field.AggSize15Count / 15);
		pRalinkCounters->TransmittedAMPDUCount.u.LowPart +=
		    (TxAggCnt7.field.AggSize16Count / 16);
	}

}

/*
	========================================================================

	Routine Description:
		Reset NIC from error

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:
		Reset NIC from error state

	========================================================================
*/
void NICResetFromError(struct rt_rtmp_adapter *pAd)
{
	/* Reset BBP (according to alex, reset ASIC will force reset BBP */
	/* Therefore, skip the reset BBP */
	/* RTMP_IO_WRITE32(pAd, MAC_CSR1, 0x2); */

	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x1);
	/* Remove ASIC from reset state */
	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0x0);

	NICInitializeAdapter(pAd, FALSE);
	NICInitAsicFromEEPROM(pAd);

	/* Switch to current channel, since during reset process, the connection should remains on. */
	AsicSwitchChannel(pAd, pAd->CommonCfg.CentralChannel, FALSE);
	AsicLockChannel(pAd, pAd->CommonCfg.CentralChannel);
}

int NICLoadFirmware(struct rt_rtmp_adapter *pAd)
{
	int status = NDIS_STATUS_SUCCESS;
	if (pAd->chipOps.loadFirmware)
		status = pAd->chipOps.loadFirmware(pAd);

	return status;
}

/*
	========================================================================

	Routine Description:
		erase 8051 firmware image in MAC ASIC

	Arguments:
		Adapter						Pointer to our adapter

	IRQL = PASSIVE_LEVEL

	========================================================================
*/
void NICEraseFirmware(struct rt_rtmp_adapter *pAd)
{
	if (pAd->chipOps.eraseFirmware)
		pAd->chipOps.eraseFirmware(pAd);

}				/* End of NICEraseFirmware */

/*
	========================================================================

	Routine Description:
		Load Tx rate switching parameters

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		NDIS_STATUS_SUCCESS         firmware image load ok
		NDIS_STATUS_FAILURE         image not found

	IRQL = PASSIVE_LEVEL

	Rate Table Format:
		1. (B0: Valid Item number) (B1:Initial item from zero)
		2. Item Number(Dec)      Mode(Hex)     Current MCS(Dec)    TrainUp(Dec)    TrainDown(Dec)

	========================================================================
*/
int NICLoadRateSwitchingParams(struct rt_rtmp_adapter *pAd)
{
	return NDIS_STATUS_SUCCESS;
}

/*
	========================================================================

	Routine Description:
		Compare two memory block

	Arguments:
		pSrc1		Pointer to first memory address
		pSrc2		Pointer to second memory address

	Return Value:
		0:			memory is equal
		1:			pSrc1 memory is larger
		2:			pSrc2 memory is larger

	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
unsigned long RTMPCompareMemory(void *pSrc1, void *pSrc2, unsigned long Length)
{
	u8 *pMem1;
	u8 *pMem2;
	unsigned long Index = 0;

	pMem1 = (u8 *)pSrc1;
	pMem2 = (u8 *)pSrc2;

	for (Index = 0; Index < Length; Index++) {
		if (pMem1[Index] > pMem2[Index])
			return (1);
		else if (pMem1[Index] < pMem2[Index])
			return (2);
	}

	/* Equal */
	return (0);
}

/*
	========================================================================

	Routine Description:
		Zero out memory block

	Arguments:
		pSrc1		Pointer to memory address
		Length		Size

	Return Value:
		None

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void RTMPZeroMemory(void *pSrc, unsigned long Length)
{
	u8 *pMem;
	unsigned long Index = 0;

	pMem = (u8 *)pSrc;

	for (Index = 0; Index < Length; Index++) {
		pMem[Index] = 0x00;
	}
}

/*
	========================================================================

	Routine Description:
		Copy data from memory block 1 to memory block 2

	Arguments:
		pDest		Pointer to destination memory address
		pSrc		Pointer to source memory address
		Length		Copy size

	Return Value:
		None

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void RTMPMoveMemory(void *pDest, void *pSrc, unsigned long Length)
{
	u8 *pMem1;
	u8 *pMem2;
	u32 Index;

	ASSERT((Length == 0) || (pDest && pSrc));

	pMem1 = (u8 *)pDest;
	pMem2 = (u8 *)pSrc;

	for (Index = 0; Index < Length; Index++) {
		pMem1[Index] = pMem2[Index];
	}
}

/*
	========================================================================

	Routine Description:
		Initialize port configuration structure

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:

	========================================================================
*/
void UserCfgInit(struct rt_rtmp_adapter *pAd)
{
	u32 key_index, bss_index;

	DBGPRINT(RT_DEBUG_TRACE, ("--> UserCfgInit\n"));

	/* */
	/*  part I. intialize common configuration */
	/* */
#ifdef RTMP_MAC_USB
	pAd->BulkOutReq = 0;

	pAd->BulkOutComplete = 0;
	pAd->BulkOutCompleteOther = 0;
	pAd->BulkOutCompleteCancel = 0;
	pAd->BulkInReq = 0;
	pAd->BulkInComplete = 0;
	pAd->BulkInCompleteFail = 0;

	/*pAd->QuickTimerP = 100; */
	/*pAd->TurnAggrBulkInCount = 0; */
	pAd->bUsbTxBulkAggre = 0;

	/* init as unsed value to ensure driver will set to MCU once. */
	pAd->LedIndicatorStrength = 0xFF;

	pAd->CommonCfg.MaxPktOneTxBulk = 2;
	pAd->CommonCfg.TxBulkFactor = 1;
	pAd->CommonCfg.RxBulkFactor = 1;

	pAd->CommonCfg.TxPower = 100;	/*mW */

	NdisZeroMemory(&pAd->CommonCfg.IOTestParm,
		       sizeof(pAd->CommonCfg.IOTestParm));
#endif /* RTMP_MAC_USB // */

	for (key_index = 0; key_index < SHARE_KEY_NUM; key_index++) {
		for (bss_index = 0; bss_index < MAX_MBSSID_NUM; bss_index++) {
			pAd->SharedKey[bss_index][key_index].KeyLen = 0;
			pAd->SharedKey[bss_index][key_index].CipherAlg =
			    CIPHER_NONE;
		}
	}

	pAd->EepromAccess = FALSE;

	pAd->Antenna.word = 0;
	pAd->CommonCfg.BBPCurrentBW = BW_20;

	pAd->LedCntl.word = 0;
#ifdef RTMP_MAC_PCI
	pAd->LedIndicatorStrength = 0;
	pAd->RLnkCtrlOffset = 0;
	pAd->HostLnkCtrlOffset = 0;
	pAd->StaCfg.PSControl.field.EnableNewPS = TRUE;
	pAd->CheckDmaBusyCount = 0;
#endif /* RTMP_MAC_PCI // */

	pAd->bAutoTxAgcA = FALSE;	/* Default is OFF */
	pAd->bAutoTxAgcG = FALSE;	/* Default is OFF */
	pAd->RfIcType = RFIC_2820;

	/* Init timer for reset complete event */
	pAd->CommonCfg.CentralChannel = 1;
	pAd->bForcePrintTX = FALSE;
	pAd->bForcePrintRX = FALSE;
	pAd->bStaFifoTest = FALSE;
	pAd->bProtectionTest = FALSE;
	pAd->CommonCfg.Dsifs = 10;	/* in units of usec */
	pAd->CommonCfg.TxPower = 100;	/*mW */
	pAd->CommonCfg.TxPowerPercentage = 0xffffffff;	/* AUTO */
	pAd->CommonCfg.TxPowerDefault = 0xffffffff;	/* AUTO */
	pAd->CommonCfg.TxPreamble = Rt802_11PreambleAuto;	/* use Long preamble on TX by defaut */
	pAd->CommonCfg.bUseZeroToDisableFragment = FALSE;
	pAd->CommonCfg.RtsThreshold = 2347;
	pAd->CommonCfg.FragmentThreshold = 2346;
	pAd->CommonCfg.UseBGProtection = 0;	/* 0: AUTO */
	pAd->CommonCfg.bEnableTxBurst = TRUE;	/*0; */
	pAd->CommonCfg.PhyMode = 0xff;	/* unknown */
	pAd->CommonCfg.BandState = UNKNOWN_BAND;
	pAd->CommonCfg.RadarDetect.CSPeriod = 10;
	pAd->CommonCfg.RadarDetect.CSCount = 0;
	pAd->CommonCfg.RadarDetect.RDMode = RD_NORMAL_MODE;

	pAd->CommonCfg.RadarDetect.ChMovingTime = 65;
	pAd->CommonCfg.RadarDetect.LongPulseRadarTh = 3;
	pAd->CommonCfg.bAPSDCapable = FALSE;
	pAd->CommonCfg.bNeedSendTriggerFrame = FALSE;
	pAd->CommonCfg.TriggerTimerCount = 0;
	pAd->CommonCfg.bAPSDForcePowerSave = FALSE;
	pAd->CommonCfg.bCountryFlag = FALSE;
	pAd->CommonCfg.TxStream = 0;
	pAd->CommonCfg.RxStream = 0;

	NdisZeroMemory(&pAd->BeaconTxWI, sizeof(pAd->BeaconTxWI));

	NdisZeroMemory(&pAd->CommonCfg.HtCapability,
		       sizeof(pAd->CommonCfg.HtCapability));
	pAd->HTCEnable = FALSE;
	pAd->bBroadComHT = FALSE;
	pAd->CommonCfg.bRdg = FALSE;

	NdisZeroMemory(&pAd->CommonCfg.AddHTInfo,
		       sizeof(pAd->CommonCfg.AddHTInfo));
	pAd->CommonCfg.BACapability.field.MMPSmode = MMPS_ENABLE;
	pAd->CommonCfg.BACapability.field.MpduDensity = 0;
	pAd->CommonCfg.BACapability.field.Policy = IMMED_BA;
	pAd->CommonCfg.BACapability.field.RxBAWinLimit = 64;	/*32; */
	pAd->CommonCfg.BACapability.field.TxBAWinLimit = 64;	/*32; */
	DBGPRINT(RT_DEBUG_TRACE,
		 ("--> UserCfgInit. BACapability = 0x%x\n",
		  pAd->CommonCfg.BACapability.word));

	pAd->CommonCfg.BACapability.field.AutoBA = FALSE;
	BATableInit(pAd, &pAd->BATable);

	pAd->CommonCfg.bExtChannelSwitchAnnouncement = 1;
	pAd->CommonCfg.bHTProtect = 1;
	pAd->CommonCfg.bMIMOPSEnable = TRUE;
	/*2008/11/05:KH add to support Antenna power-saving of AP<-- */
	pAd->CommonCfg.bGreenAPEnable = FALSE;
	/*2008/11/05:KH add to support Antenna power-saving of AP--> */
	pAd->CommonCfg.bBADecline = FALSE;
	pAd->CommonCfg.bDisableReordering = FALSE;

	if (pAd->MACVersion == 0x28720200) {
		pAd->CommonCfg.TxBASize = 13;	/*by Jerry recommend */
	} else {
		pAd->CommonCfg.TxBASize = 7;
	}

	pAd->CommonCfg.REGBACapability.word = pAd->CommonCfg.BACapability.word;

	/*pAd->CommonCfg.HTPhyMode.field.BW = BW_20; */
	/*pAd->CommonCfg.HTPhyMode.field.MCS = MCS_AUTO; */
	/*pAd->CommonCfg.HTPhyMode.field.ShortGI = GI_800; */
	/*pAd->CommonCfg.HTPhyMode.field.STBC = STBC_NONE; */
	pAd->CommonCfg.TxRate = RATE_6;

	pAd->CommonCfg.MlmeTransmit.field.MCS = MCS_RATE_6;
	pAd->CommonCfg.MlmeTransmit.field.BW = BW_20;
	pAd->CommonCfg.MlmeTransmit.field.MODE = MODE_OFDM;

	pAd->CommonCfg.BeaconPeriod = 100;	/* in mSec */

	/* */
	/* part II. intialize STA specific configuration */
	/* */
	{
		RX_FILTER_SET_FLAG(pAd, fRX_FILTER_ACCEPT_DIRECT);
		RX_FILTER_CLEAR_FLAG(pAd, fRX_FILTER_ACCEPT_MULTICAST);
		RX_FILTER_SET_FLAG(pAd, fRX_FILTER_ACCEPT_BROADCAST);
		RX_FILTER_SET_FLAG(pAd, fRX_FILTER_ACCEPT_ALL_MULTICAST);

		pAd->StaCfg.Psm = PWR_ACTIVE;

		pAd->StaCfg.OrigWepStatus = Ndis802_11EncryptionDisabled;
		pAd->StaCfg.PairCipher = Ndis802_11EncryptionDisabled;
		pAd->StaCfg.GroupCipher = Ndis802_11EncryptionDisabled;
		pAd->StaCfg.bMixCipher = FALSE;
		pAd->StaCfg.DefaultKeyId = 0;

		/* 802.1x port control */
		pAd->StaCfg.PrivacyFilter = Ndis802_11PrivFilter8021xWEP;
		pAd->StaCfg.PortSecured = WPA_802_1X_PORT_NOT_SECURED;
		pAd->StaCfg.LastMicErrorTime = 0;
		pAd->StaCfg.MicErrCnt = 0;
		pAd->StaCfg.bBlockAssoc = FALSE;
		pAd->StaCfg.WpaState = SS_NOTUSE;

		pAd->CommonCfg.NdisRadioStateOff = FALSE;	/* New to support microsoft disable radio with OID command */

		pAd->StaCfg.RssiTrigger = 0;
		NdisZeroMemory(&pAd->StaCfg.RssiSample, sizeof(struct rt_rssi_sample));
		pAd->StaCfg.RssiTriggerMode =
		    RSSI_TRIGGERED_UPON_BELOW_THRESHOLD;
		pAd->StaCfg.AtimWin = 0;
		pAd->StaCfg.DefaultListenCount = 3;	/*default listen count; */
		pAd->StaCfg.BssType = BSS_INFRA;	/* BSS_INFRA or BSS_ADHOC or BSS_MONITOR */
		pAd->StaCfg.bScanReqIsFromWebUI = FALSE;
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_WAKEUP_NOW);

		pAd->StaCfg.bAutoTxRateSwitch = TRUE;
		pAd->StaCfg.DesiredTransmitSetting.field.MCS = MCS_AUTO;
	}

#ifdef PCIE_PS_SUPPORT
	pAd->brt30xxBanMcuCmd = FALSE;
	pAd->b3090ESpecialChip = FALSE;
/*KH Debug:the following must be removed */
	pAd->StaCfg.PSControl.field.rt30xxPowerMode = 3;
	pAd->StaCfg.PSControl.field.rt30xxForceASPMTest = 0;
	pAd->StaCfg.PSControl.field.rt30xxFollowHostASPM = 1;
#endif /* PCIE_PS_SUPPORT // */

	/* global variables mXXXX used in MAC protocol state machines */
	OPSTATUS_SET_FLAG(pAd, fOP_STATUS_RECEIVE_DTIM);
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_ADHOC_ON);
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_INFRA_ON);

	/* PHY specification */
	pAd->CommonCfg.PhyMode = PHY_11BG_MIXED;	/* default PHY mode */
	OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_SHORT_PREAMBLE_INUSED);	/* CCK use long preamble */

	{
		/* user desired power mode */
		pAd->StaCfg.WindowsPowerMode = Ndis802_11PowerModeCAM;
		pAd->StaCfg.WindowsBatteryPowerMode = Ndis802_11PowerModeCAM;
		pAd->StaCfg.bWindowsACCAMEnable = FALSE;

		RTMPInitTimer(pAd, &pAd->StaCfg.StaQuickResponeForRateUpTimer,
			      GET_TIMER_FUNCTION(StaQuickResponeForRateUpExec),
			      pAd, FALSE);
		pAd->StaCfg.StaQuickResponeForRateUpTimerRunning = FALSE;

		/* Patch for Ndtest */
		pAd->StaCfg.ScanCnt = 0;

		pAd->StaCfg.bHwRadio = TRUE;	/* Default Hardware Radio status is On */
		pAd->StaCfg.bSwRadio = TRUE;	/* Default Software Radio status is On */
		pAd->StaCfg.bRadio = TRUE;	/* bHwRadio && bSwRadio */
		pAd->StaCfg.bHardwareRadio = FALSE;	/* Default is OFF */
		pAd->StaCfg.bShowHiddenSSID = FALSE;	/* Default no show */

		/* Nitro mode control */
		pAd->StaCfg.bAutoReconnect = TRUE;

		/* Save the init time as last scan time, the system should do scan after 2 seconds. */
		/* This patch is for driver wake up from standby mode, system will do scan right away. */
		NdisGetSystemUpTime(&pAd->StaCfg.LastScanTime);
		if (pAd->StaCfg.LastScanTime > 10 * OS_HZ)
			pAd->StaCfg.LastScanTime -= (10 * OS_HZ);

		NdisZeroMemory(pAd->nickname, IW_ESSID_MAX_SIZE + 1);
#ifdef RTMP_MAC_PCI
		sprintf((char *)pAd->nickname, "RT2860STA");
#endif /* RTMP_MAC_PCI // */
#ifdef RTMP_MAC_USB
		sprintf((char *)pAd->nickname, "RT2870STA");
#endif /* RTMP_MAC_USB // */
		RTMPInitTimer(pAd, &pAd->StaCfg.WpaDisassocAndBlockAssocTimer,
			      GET_TIMER_FUNCTION(WpaDisassocApAndBlockAssoc),
			      pAd, FALSE);
		pAd->StaCfg.IEEE8021X = FALSE;
		pAd->StaCfg.IEEE8021x_required_keys = FALSE;
		pAd->StaCfg.WpaSupplicantUP = WPA_SUPPLICANT_DISABLE;
		pAd->StaCfg.bRSN_IE_FromWpaSupplicant = FALSE;
		pAd->StaCfg.WpaSupplicantUP = WPA_SUPPLICANT_ENABLE;

		NdisZeroMemory(pAd->StaCfg.ReplayCounter, 8);

		pAd->StaCfg.bAutoConnectByBssid = FALSE;
		pAd->StaCfg.BeaconLostTime = BEACON_LOST_TIME;
		NdisZeroMemory(pAd->StaCfg.WpaPassPhrase, 64);
		pAd->StaCfg.WpaPassPhraseLen = 0;
		pAd->StaCfg.bAutoRoaming = FALSE;
		pAd->StaCfg.bForceTxBurst = FALSE;
	}

	/* Default for extra information is not valid */
	pAd->ExtraInfo = EXTRA_INFO_CLEAR;

	/* Default Config change flag */
	pAd->bConfigChanged = FALSE;

	/* */
	/* part III. AP configurations */
	/* */

	/* */
	/* part IV. others */
	/* */
	/* dynamic BBP R66:sensibity tuning to overcome background noise */
	pAd->BbpTuning.bEnable = TRUE;
	pAd->BbpTuning.FalseCcaLowerThreshold = 100;
	pAd->BbpTuning.FalseCcaUpperThreshold = 512;
	pAd->BbpTuning.R66Delta = 4;
	pAd->Mlme.bEnableAutoAntennaCheck = TRUE;

	/* */
	/* Also initial R66CurrentValue, RTUSBResumeMsduTransmission might use this value. */
	/* if not initial this value, the default value will be 0. */
	/* */
	pAd->BbpTuning.R66CurrentValue = 0x38;

	pAd->Bbp94 = BBPR94_DEFAULT;
	pAd->BbpForCCK = FALSE;

	/* Default is FALSE for test bit 1 */
	/*pAd->bTest1 = FALSE; */

	/* initialize MAC table and allocate spin lock */
	NdisZeroMemory(&pAd->MacTab, sizeof(struct rt_mac_table));
	InitializeQueueHeader(&pAd->MacTab.McastPsQueue);
	NdisAllocateSpinLock(&pAd->MacTabLock);

	/*RTMPInitTimer(pAd, &pAd->RECBATimer, RECBATimerTimeout, pAd, TRUE); */
	/*RTMPSetTimer(&pAd->RECBATimer, REORDER_EXEC_INTV); */

	pAd->CommonCfg.bWiFiTest = FALSE;
#ifdef RTMP_MAC_PCI
	pAd->bPCIclkOff = FALSE;
#endif /* RTMP_MAC_PCI // */

	RTMP_SET_PSFLAG(pAd, fRTMP_PS_CAN_GO_SLEEP);
	DBGPRINT(RT_DEBUG_TRACE, ("<-- UserCfgInit\n"));
}

/* IRQL = PASSIVE_LEVEL */
u8 BtoH(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');	/* Handle numerals */
	if (ch >= 'A' && ch <= 'F')
		return (ch - 'A' + 0xA);	/* Handle capitol hex digits */
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a' + 0xA);	/* Handle small hex digits */
	return (255);
}

/* */
/*  FUNCTION: AtoH(char *, u8 *, int) */
/* */
/*  PURPOSE:  Converts ascii string to network order hex */
/* */
/*  PARAMETERS: */
/*    src    - pointer to input ascii string */
/*    dest   - pointer to output hex */
/*    destlen - size of dest */
/* */
/*  COMMENTS: */
/* */
/*    2 ascii bytes make a hex byte so must put 1st ascii byte of pair */
/*    into upper nibble and 2nd ascii byte of pair into lower nibble. */
/* */
/* IRQL = PASSIVE_LEVEL */

void AtoH(char *src, u8 *dest, int destlen)
{
	char *srcptr;
	u8 *destTemp;

	srcptr = src;
	destTemp = (u8 *)dest;

	while (destlen--) {
		*destTemp = BtoH(*srcptr++) << 4;	/* Put 1st ascii byte in upper nibble. */
		*destTemp += BtoH(*srcptr++);	/* Add 2nd ascii byte to above. */
		destTemp++;
	}
}

/*+++Mark by shiang, not use now, need to remove after confirm */
/*---Mark by shiang, not use now, need to remove after confirm */

/*
	========================================================================

	Routine Description:
		Init timer objects

	Arguments:
		pAd			Pointer to our adapter
		pTimer				Timer structure
		pTimerFunc			Function to execute when timer expired
		Repeat				Ture for period timer

	Return Value:
		None

	Note:

	========================================================================
*/
void RTMPInitTimer(struct rt_rtmp_adapter *pAd,
		   struct rt_ralink_timer *pTimer,
		   void *pTimerFunc, void *pData, IN BOOLEAN Repeat)
{
	/* */
	/* Set Valid to TRUE for later used. */
	/* It will crash if we cancel a timer or set a timer */
	/* that we haven't initialize before. */
	/* */
	pTimer->Valid = TRUE;

	pTimer->PeriodicType = Repeat;
	pTimer->State = FALSE;
	pTimer->cookie = (unsigned long)pData;

#ifdef RTMP_TIMER_TASK_SUPPORT
	pTimer->pAd = pAd;
#endif /* RTMP_TIMER_TASK_SUPPORT // */

	RTMP_OS_Init_Timer(pAd, &pTimer->TimerObj, pTimerFunc, (void *)pTimer);
}

/*
	========================================================================

	Routine Description:
		Init timer objects

	Arguments:
		pTimer				Timer structure
		Value				Timer value in milliseconds

	Return Value:
		None

	Note:
		To use this routine, must call RTMPInitTimer before.

	========================================================================
*/
void RTMPSetTimer(struct rt_ralink_timer *pTimer, unsigned long Value)
{
	if (pTimer->Valid) {
		pTimer->TimerValue = Value;
		pTimer->State = FALSE;
		if (pTimer->PeriodicType == TRUE) {
			pTimer->Repeat = TRUE;
			RTMP_SetPeriodicTimer(&pTimer->TimerObj, Value);
		} else {
			pTimer->Repeat = FALSE;
			RTMP_OS_Add_Timer(&pTimer->TimerObj, Value);
		}
	} else {
		DBGPRINT_ERR(("RTMPSetTimer failed, Timer hasn't been initialize!\n"));
	}
}

/*
	========================================================================

	Routine Description:
		Init timer objects

	Arguments:
		pTimer				Timer structure
		Value				Timer value in milliseconds

	Return Value:
		None

	Note:
		To use this routine, must call RTMPInitTimer before.

	========================================================================
*/
void RTMPModTimer(struct rt_ralink_timer *pTimer, unsigned long Value)
{
	BOOLEAN Cancel;

	if (pTimer->Valid) {
		pTimer->TimerValue = Value;
		pTimer->State = FALSE;
		if (pTimer->PeriodicType == TRUE) {
			RTMPCancelTimer(pTimer, &Cancel);
			RTMPSetTimer(pTimer, Value);
		} else {
			RTMP_OS_Mod_Timer(&pTimer->TimerObj, Value);
		}
	} else {
		DBGPRINT_ERR(("RTMPModTimer failed, Timer hasn't been initialize!\n"));
	}
}

/*
	========================================================================

	Routine Description:
		Cancel timer objects

	Arguments:
		Adapter						Pointer to our adapter

	Return Value:
		None

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	Note:
		1.) To use this routine, must call RTMPInitTimer before.
		2.) Reset NIC to initial state AS IS system boot up time.

	========================================================================
*/
void RTMPCancelTimer(struct rt_ralink_timer *pTimer, OUT BOOLEAN * pCancelled)
{
	if (pTimer->Valid) {
		if (pTimer->State == FALSE)
			pTimer->Repeat = FALSE;

		RTMP_OS_Del_Timer(&pTimer->TimerObj, pCancelled);

		if (*pCancelled == TRUE)
			pTimer->State = TRUE;

#ifdef RTMP_TIMER_TASK_SUPPORT
		/* We need to go-through the TimerQ to findout this timer handler and remove it if */
		/*              it's still waiting for execution. */
		RtmpTimerQRemove(pTimer->pAd, pTimer);
#endif /* RTMP_TIMER_TASK_SUPPORT // */
	} else {
		DBGPRINT_ERR(("RTMPCancelTimer failed, Timer hasn't been initialize!\n"));
	}
}

/*
	========================================================================

	Routine Description:
		Set LED Status

	Arguments:
		pAd						Pointer to our adapter
		Status					LED Status

	Return Value:
		None

	IRQL = PASSIVE_LEVEL
	IRQL = DISPATCH_LEVEL

	Note:

	========================================================================
*/
void RTMPSetLED(struct rt_rtmp_adapter *pAd, u8 Status)
{
	/*unsigned long                 data; */
	u8 HighByte = 0;
	u8 LowByte;

	LowByte = pAd->LedCntl.field.LedMode & 0x7f;
	switch (Status) {
	case LED_LINK_DOWN:
		HighByte = 0x20;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		pAd->LedIndicatorStrength = 0;
		break;
	case LED_LINK_UP:
		if (pAd->CommonCfg.Channel > 14)
			HighByte = 0xa0;
		else
			HighByte = 0x60;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		break;
	case LED_RADIO_ON:
		HighByte = 0x20;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		break;
	case LED_HALT:
		LowByte = 0;	/* Driver sets MAC register and MAC controls LED */
	case LED_RADIO_OFF:
		HighByte = 0;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		break;
	case LED_WPS:
		HighByte = 0x10;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		break;
	case LED_ON_SITE_SURVEY:
		HighByte = 0x08;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		break;
	case LED_POWER_UP:
		HighByte = 0x04;
		AsicSendCommandToMcu(pAd, 0x50, 0xff, LowByte, HighByte);
		break;
	default:
		DBGPRINT(RT_DEBUG_WARN,
			 ("RTMPSetLED::Unknown Status %d\n", Status));
		break;
	}

	/* */
	/* Keep LED status for LED SiteSurvey mode. */
	/* After SiteSurvey, we will set the LED mode to previous status. */
	/* */
	if ((Status != LED_ON_SITE_SURVEY) && (Status != LED_POWER_UP))
		pAd->LedStatus = Status;

	DBGPRINT(RT_DEBUG_TRACE,
		 ("RTMPSetLED::Mode=%d,HighByte=0x%02x,LowByte=0x%02x\n",
		  pAd->LedCntl.field.LedMode, HighByte, LowByte));
}

/*
	========================================================================

	Routine Description:
		Set LED Signal Stregth

	Arguments:
		pAd						Pointer to our adapter
		Dbm						Signal Stregth

	Return Value:
		None

	IRQL = PASSIVE_LEVEL

	Note:
		Can be run on any IRQL level.

		According to Microsoft Zero Config Wireless Signal Stregth definition as belows.
		<= -90  No Signal
		<= -81  Very Low
		<= -71  Low
		<= -67  Good
		<= -57  Very Good
		 > -57  Excellent
	========================================================================
*/
void RTMPSetSignalLED(struct rt_rtmp_adapter *pAd, IN NDIS_802_11_RSSI Dbm)
{
	u8 nLed = 0;

	if (pAd->LedCntl.field.LedMode == LED_MODE_SIGNAL_STREGTH) {
		if (Dbm <= -90)
			nLed = 0;
		else if (Dbm <= -81)
			nLed = 1;
		else if (Dbm <= -71)
			nLed = 3;
		else if (Dbm <= -67)
			nLed = 7;
		else if (Dbm <= -57)
			nLed = 15;
		else
			nLed = 31;

		/* */
		/* Update Signal Stregth to firmware if changed. */
		/* */
		if (pAd->LedIndicatorStrength != nLed) {
			AsicSendCommandToMcu(pAd, 0x51, 0xff, nLed,
					     pAd->LedCntl.field.Polarity);
			pAd->LedIndicatorStrength = nLed;
		}
	}
}

/*
	========================================================================

	Routine Description:
		Enable RX

	Arguments:
		pAd						Pointer to our adapter

	Return Value:
		None

	IRQL <= DISPATCH_LEVEL

	Note:
		Before Enable RX, make sure you have enabled Interrupt.
	========================================================================
*/
void RTMPEnableRxTx(struct rt_rtmp_adapter *pAd)
{
/*      WPDMA_GLO_CFG_STRUC     GloCfg; */
/*      unsigned long   i = 0; */
	u32 rx_filter_flag;

	DBGPRINT(RT_DEBUG_TRACE, ("==> RTMPEnableRxTx\n"));

	/* Enable Rx DMA. */
	RT28XXDMAEnable(pAd);

	/* enable RX of MAC block */
	if (pAd->OpMode == OPMODE_AP) {
		rx_filter_flag = APNORMAL;

		RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, rx_filter_flag);	/* enable RX of DMA block */
	} else {
		if (pAd->CommonCfg.PSPXlink)
			rx_filter_flag = PSPXLINK;
		else
			rx_filter_flag = STANORMAL;	/* Staion not drop control frame will fail WiFi Certification. */
		RTMP_IO_WRITE32(pAd, RX_FILTR_CFG, rx_filter_flag);
	}

	RTMP_IO_WRITE32(pAd, MAC_SYS_CTRL, 0xc);
	DBGPRINT(RT_DEBUG_TRACE, ("<== RTMPEnableRxTx\n"));
}

/*+++Add by shiang, move from os/linux/rt_main_dev.c */
void CfgInitHook(struct rt_rtmp_adapter *pAd)
{
	pAd->bBroadComHT = TRUE;
}

int rt28xx_init(struct rt_rtmp_adapter *pAd,
		char *pDefaultMac, char *pHostName)
{
	u32 index;
	u8 TmpPhy;
	int Status;
	u32 MacCsr0 = 0;

#ifdef RTMP_MAC_PCI
	{
		/* If dirver doesn't wake up firmware here, */
		/* NICLoadFirmware will hang forever when interface is up again. */
		/* RT2860 PCI */
		if (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE) &&
		    OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)) {
			AUTO_WAKEUP_STRUC AutoWakeupCfg;
			AsicForceWakeup(pAd, TRUE);
			AutoWakeupCfg.word = 0;
			RTMP_IO_WRITE32(pAd, AUTO_WAKEUP_CFG,
					AutoWakeupCfg.word);
			OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_DOZE);
		}
	}
#endif /* RTMP_MAC_PCI // */

	/* reset Adapter flags */
	RTMP_CLEAR_FLAGS(pAd);

	/* Init BssTab & ChannelInfo tabbles for auto channel select. */

	/* Allocate BA Reordering memory */
	ba_reordering_resource_init(pAd, MAX_REORDERING_MPDU_NUM);

	/* Make sure MAC gets ready. */
	index = 0;
	do {
		RTMP_IO_READ32(pAd, MAC_CSR0, &MacCsr0);
		pAd->MACVersion = MacCsr0;

		if ((pAd->MACVersion != 0x00)
		    && (pAd->MACVersion != 0xFFFFFFFF))
			break;

		RTMPusecDelay(10);
	} while (index++ < 100);
	DBGPRINT(RT_DEBUG_TRACE,
		 ("MAC_CSR0  [ Ver:Rev=0x%08x]\n", pAd->MACVersion));

#ifdef RTMP_MAC_PCI
#ifdef PCIE_PS_SUPPORT
	/*Iverson patch PCIE L1 issue to make sure that driver can be read,write ,BBP and RF register  at pcie L.1 level */
	if ((IS_RT3090(pAd) || IS_RT3572(pAd) || IS_RT3390(pAd))
	    && OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_PCIE_DEVICE)) {
		RTMP_IO_READ32(pAd, AUX_CTRL, &MacCsr0);
		MacCsr0 |= 0x402;
		RTMP_IO_WRITE32(pAd, AUX_CTRL, MacCsr0);
		DBGPRINT(RT_DEBUG_TRACE, ("AUX_CTRL = 0x%x\n", MacCsr0));
	}
#endif /* PCIE_PS_SUPPORT // */

	/* To fix driver disable/enable hang issue when radio off */
	RTMP_IO_WRITE32(pAd, PWR_PIN_CFG, 0x2);
#endif /* RTMP_MAC_PCI // */

	/* Disable DMA */
	RT28XXDMADisable(pAd);

	/* Load 8051 firmware */
	Status = NICLoadFirmware(pAd);
	if (Status != NDIS_STATUS_SUCCESS) {
		DBGPRINT_ERR(("NICLoadFirmware failed, Status[=0x%08x]\n",
			      Status));
		goto err1;
	}

	NICLoadRateSwitchingParams(pAd);

	/* Disable interrupts here which is as soon as possible */
	/* This statement should never be true. We might consider to remove it later */
#ifdef RTMP_MAC_PCI
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_ACTIVE)) {
		RTMP_ASIC_INTERRUPT_DISABLE(pAd);
	}
#endif /* RTMP_MAC_PCI // */

	Status = RTMPAllocTxRxRingMemory(pAd);
	if (Status != NDIS_STATUS_SUCCESS) {
		DBGPRINT_ERR(("RTMPAllocDMAMemory failed, Status[=0x%08x]\n",
			      Status));
		goto err1;
	}

	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);

	/* initialize MLME */
	/* */

	Status = RtmpMgmtTaskInit(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
		goto err2;

	Status = MlmeInit(pAd);
	if (Status != NDIS_STATUS_SUCCESS) {
		DBGPRINT_ERR(("MlmeInit failed, Status[=0x%08x]\n", Status));
		goto err2;
	}
	/* Initialize pAd->StaCfg, pAd->ApCfg, pAd->CommonCfg to manufacture default */
	/* */
	UserCfgInit(pAd);
	Status = RtmpNetTaskInit(pAd);
	if (Status != NDIS_STATUS_SUCCESS)
		goto err3;

/*      COPY_MAC_ADDR(pAd->ApCfg.MBSSID[apidx].Bssid, netif->hwaddr); */
/*      pAd->bForcePrintTX = TRUE; */

	CfgInitHook(pAd);

	NdisAllocateSpinLock(&pAd->MacTabLock);

	MeasureReqTabInit(pAd);
	TpcReqTabInit(pAd);

	/* */
	/* Init the hardware, we need to init asic before read registry, otherwise mac register will be reset */
	/* */
	Status = NICInitializeAdapter(pAd, TRUE);
	if (Status != NDIS_STATUS_SUCCESS) {
		DBGPRINT_ERR(("NICInitializeAdapter failed, Status[=0x%08x]\n",
			      Status));
		if (Status != NDIS_STATUS_SUCCESS)
			goto err3;
	}

	DBGPRINT(RT_DEBUG_OFF, ("1. Phy Mode = %d\n", pAd->CommonCfg.PhyMode));

#ifdef RTMP_MAC_USB
	pAd->CommonCfg.bMultipleIRP = FALSE;

	if (pAd->CommonCfg.bMultipleIRP)
		pAd->CommonCfg.NumOfBulkInIRP = RX_RING_SIZE;
	else
		pAd->CommonCfg.NumOfBulkInIRP = 1;
#endif /* RTMP_MAC_USB // */

	/*Init Ba Capability parameters. */
/*      RT28XX_BA_INIT(pAd); */
	pAd->CommonCfg.DesiredHtPhy.MpduDensity =
	    (u8)pAd->CommonCfg.BACapability.field.MpduDensity;
	pAd->CommonCfg.DesiredHtPhy.AmsduEnable =
	    (u16)pAd->CommonCfg.BACapability.field.AmsduEnable;
	pAd->CommonCfg.DesiredHtPhy.AmsduSize =
	    (u16)pAd->CommonCfg.BACapability.field.AmsduSize;
	pAd->CommonCfg.DesiredHtPhy.MimoPs =
	    (u16)pAd->CommonCfg.BACapability.field.MMPSmode;
	/* UPdata to HT IE */
	pAd->CommonCfg.HtCapability.HtCapInfo.MimoPs =
	    (u16)pAd->CommonCfg.BACapability.field.MMPSmode;
	pAd->CommonCfg.HtCapability.HtCapInfo.AMsduSize =
	    (u16)pAd->CommonCfg.BACapability.field.AmsduSize;
	pAd->CommonCfg.HtCapability.HtCapParm.MpduDensity =
	    (u8)pAd->CommonCfg.BACapability.field.MpduDensity;

	/* after reading Registry, we now know if in AP mode or STA mode */

	/* Load 8051 firmware; crash when FW image not existent */
	/* Status = NICLoadFirmware(pAd); */
	/* if (Status != NDIS_STATUS_SUCCESS) */
	/*    break; */

	DBGPRINT(RT_DEBUG_OFF, ("2. Phy Mode = %d\n", pAd->CommonCfg.PhyMode));

	/* We should read EEPROM for all cases.  rt2860b */
	NICReadEEPROMParameters(pAd, (u8 *)pDefaultMac);

	DBGPRINT(RT_DEBUG_OFF, ("3. Phy Mode = %d\n", pAd->CommonCfg.PhyMode));

	NICInitAsicFromEEPROM(pAd);	/*rt2860b */

	/* Set PHY to appropriate mode */
	TmpPhy = pAd->CommonCfg.PhyMode;
	pAd->CommonCfg.PhyMode = 0xff;
	RTMPSetPhyMode(pAd, TmpPhy);
	SetCommonHT(pAd);

	/* No valid channels. */
	if (pAd->ChannelListNum == 0) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("Wrong configuration. No valid channel found. Check \"ContryCode\" and \"ChannelGeography\" setting.\n"));
		goto err4;
	}

	DBGPRINT(RT_DEBUG_OFF,
		 ("MCS Set = %02x %02x %02x %02x %02x\n",
		  pAd->CommonCfg.HtCapability.MCSSet[0],
		  pAd->CommonCfg.HtCapability.MCSSet[1],
		  pAd->CommonCfg.HtCapability.MCSSet[2],
		  pAd->CommonCfg.HtCapability.MCSSet[3],
		  pAd->CommonCfg.HtCapability.MCSSet[4]));

#ifdef RTMP_RF_RW_SUPPORT
	/*Init RT30xx RFRegisters after read RFIC type from EEPROM */
	NICInitRFRegisters(pAd);
#endif /* RTMP_RF_RW_SUPPORT // */

/*              APInitialize(pAd); */

	/* */
	/* Initialize RF register to default value */
	/* */
	AsicSwitchChannel(pAd, pAd->CommonCfg.Channel, FALSE);
	AsicLockChannel(pAd, pAd->CommonCfg.Channel);

	/* 8051 firmware require the signal during booting time. */
	/*2008/11/28:KH marked the following codes to patch Frequency offset bug */
	/*AsicSendCommandToMcu(pAd, 0x72, 0xFF, 0x00, 0x00); */

	if (pAd && (Status != NDIS_STATUS_SUCCESS)) {
		/* */
		/* Undo everything if it failed */
		/* */
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE)) {
/*                      NdisMDeregisterInterrupt(&pAd->Interrupt); */
			RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_INTERRUPT_IN_USE);
		}
/*              RTMPFreeAdapter(pAd); // we will free it in disconnect() */
	} else if (pAd) {
		/* Microsoft HCT require driver send a disconnect event after driver initialization. */
		OPSTATUS_CLEAR_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED);
/*              pAd->IndicateMediaState = NdisMediaStateDisconnected; */
		RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_MEDIA_STATE_CHANGE);

		DBGPRINT(RT_DEBUG_TRACE,
			 ("NDIS_STATUS_MEDIA_DISCONNECT Event B!\n"));

#ifdef RTMP_MAC_USB
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS);
		RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_REMOVE_IN_PROGRESS);

		/* */
		/* Support multiple BulkIn IRP, */
		/* the value on pAd->CommonCfg.NumOfBulkInIRP may be large than 1. */
		/* */
		for (index = 0; index < pAd->CommonCfg.NumOfBulkInIRP; index++) {
			RTUSBBulkReceive(pAd);
			DBGPRINT(RT_DEBUG_TRACE, ("RTUSBBulkReceive!\n"));
		}
#endif /* RTMP_MAC_USB // */
	}			/* end of else */

	/* Set up the Mac address */
	RtmpOSNetDevAddrSet(pAd->net_dev, &pAd->CurrentAddress[0]);

	DBGPRINT_S(Status, ("<==== rt28xx_init, Status=%x\n", Status));

	return TRUE;

err4:
err3:
	MlmeHalt(pAd);
err2:
	RTMPFreeTxRxRingMemory(pAd);
err1:

	os_free_mem(pAd, pAd->mpdu_blk_pool.mem);	/* free BA pool */

	/* shall not set priv to NULL here because the priv didn't been free yet. */
	/*net_dev->ml_priv = 0; */
#ifdef ST
err0:
#endif /* ST // */

	DBGPRINT(RT_DEBUG_ERROR, ("rt28xx Initialized fail!\n"));
	return FALSE;
}

/*---Add by shiang, move from os/linux/rt_main_dev.c */

static int RtmpChipOpsRegister(struct rt_rtmp_adapter *pAd, int infType)
{
	struct rt_rtmp_chip_op *pChipOps = &pAd->chipOps;
	int status;

	memset(pChipOps, 0, sizeof(struct rt_rtmp_chip_op));

	/* set eeprom related hook functions */
	status = RtmpChipOpsEepromHook(pAd, infType);

	/* set mcu related hook functions */
	switch (infType) {
#ifdef RTMP_PCI_SUPPORT
	case RTMP_DEV_INF_PCI:
		pChipOps->loadFirmware = RtmpAsicLoadFirmware;
		pChipOps->eraseFirmware = RtmpAsicEraseFirmware;
		pChipOps->sendCommandToMcu = RtmpAsicSendCommandToMcu;
		break;
#endif /* RTMP_PCI_SUPPORT // */
#ifdef RTMP_USB_SUPPORT
	case RTMP_DEV_INF_USB:
		pChipOps->loadFirmware = RtmpAsicLoadFirmware;
		pChipOps->sendCommandToMcu = RtmpAsicSendCommandToMcu;
		break;
#endif /* RTMP_USB_SUPPORT // */
	default:
		break;
	}

	return status;
}

int RtmpRaDevCtrlInit(struct rt_rtmp_adapter *pAd, IN RTMP_INF_TYPE infType)
{
	/*void  *handle; */

	/* Assign the interface type. We need use it when do register/EEPROM access. */
	pAd->infType = infType;

	pAd->OpMode = OPMODE_STA;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("STA Driver version-%s\n", STA_DRIVER_VERSION));

#ifdef RTMP_MAC_USB
	init_MUTEX(&(pAd->UsbVendorReq_semaphore));
	os_alloc_mem(pAd, (u8 **) & pAd->UsbVendorReqBuf,
		     MAX_PARAM_BUFFER_SIZE - 1);
	if (pAd->UsbVendorReqBuf == NULL) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("Allocate vendor request temp buffer failed!\n"));
		return FALSE;
	}
#endif /* RTMP_MAC_USB // */

	RtmpChipOpsRegister(pAd, infType);

	return 0;
}

BOOLEAN RtmpRaDevCtrlExit(struct rt_rtmp_adapter *pAd)
{

	RTMPFreeAdapter(pAd);

	return TRUE;
}

/* not yet support MBSS */
struct net_device *get_netdev_from_bssid(struct rt_rtmp_adapter *pAd, u8 FromWhichBSSID)
{
	struct net_device *dev_p = NULL;

	{
		dev_p = pAd->net_dev;
	}

	ASSERT(dev_p);
	return dev_p;		/* return one of MBSS */
}
