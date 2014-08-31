/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8188E_PHYCFG_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_iol.h>
#include <rtl8188e_hal.h>

#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

#define MAX_DOZE_WAITING_TIMES_9x 64

static u32 cal_bit_shift(u32 bitmask)
{
	u32 i;

	for (i = 0; i <= 31; i++) {
		if (((bitmask >> i) & 0x1) == 1)
			break;
	}
	return i;
}

u32 phy_query_bb_reg(struct adapter *adapt, u32 regaddr, u32 bitmask)
{
	u32 return_value = 0, original_value, bit_shift;

	original_value = usb_read32(adapt, regaddr);
	bit_shift = cal_bit_shift(bitmask);
	return_value = (original_value & bitmask) >> bit_shift;
	return return_value;
}

void phy_set_bb_reg(struct adapter *adapt, u32 regaddr, u32 bitmask, u32 data)
{
	u32 original_value, bit_shift;

	if (bitmask != bMaskDWord) { /* if not "double word" write */
		original_value = usb_read32(adapt, regaddr);
		bit_shift = cal_bit_shift(bitmask);
		data = ((original_value & (~bitmask)) | (data << bit_shift));
	}

	usb_write32(adapt, regaddr, data);
}

static u32 rf_serial_read(struct adapter *adapt,
			enum rf_radio_path rfpath, u32 offset)
{
	u32 ret = 0;
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	struct bb_reg_def *phyreg = &hal_data->PHYRegDef[rfpath];
	u32 newoffset;
	u32 tmplong, tmplong2;
	u8 rfpi_enable = 0;

	offset &= 0xff;
	newoffset = offset;

	tmplong = phy_query_bb_reg(adapt, rFPGA0_XA_HSSIParameter2, bMaskDWord);
	if (rfpath == RF_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = phy_query_bb_reg(adapt, phyreg->rfHSSIPara2,
					    bMaskDWord);

	tmplong2 = (tmplong2 & (~bLSSIReadAddress)) |
		   (newoffset<<23) | bLSSIReadEdge;

	phy_set_bb_reg(adapt, rFPGA0_XA_HSSIParameter2, bMaskDWord,
		       tmplong&(~bLSSIReadEdge));
	udelay(10);

	phy_set_bb_reg(adapt, phyreg->rfHSSIPara2, bMaskDWord, tmplong2);
	udelay(100);

	udelay(10);

	if (rfpath == RF_PATH_A)
		rfpi_enable = (u8)phy_query_bb_reg(adapt, rFPGA0_XA_HSSIParameter1, BIT8);
	else if (rfpath == RF_PATH_B)
		rfpi_enable = (u8)phy_query_bb_reg(adapt, rFPGA0_XB_HSSIParameter1, BIT8);

	if (rfpi_enable)
		ret = phy_query_bb_reg(adapt, phyreg->rfLSSIReadBackPi,
				       bLSSIReadBackData);
	else
		ret = phy_query_bb_reg(adapt, phyreg->rfLSSIReadBack,
				       bLSSIReadBackData);
	return ret;
}

static void rf_serial_write(struct adapter *adapt,
			    enum rf_radio_path rfpath, u32 offset,
			    u32 data)
{
	u32 data_and_addr = 0;
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	struct bb_reg_def *phyreg = &hal_data->PHYRegDef[rfpath];
	u32 newoffset;

	newoffset = offset & 0xff;
	data_and_addr = ((newoffset<<20) | (data&0x000fffff)) & 0x0fffffff;
	phy_set_bb_reg(adapt, phyreg->rf3wireOffset, bMaskDWord, data_and_addr);
}

u32 phy_query_rf_reg(struct adapter *adapt, enum rf_radio_path rf_path,
		     u32 reg_addr, u32 bit_mask)
{
	u32 original_value, readback_value, bit_shift;

	original_value = rf_serial_read(adapt, rf_path, reg_addr);
	bit_shift =  cal_bit_shift(bit_mask);
	readback_value = (original_value & bit_mask) >> bit_shift;
	return readback_value;
}

/**
* Function:	PHY_SetRFReg
*
* OverView:	Write "Specific bits" to RF register (page 8~)
*
* Input:
*			struct adapter *Adapter,
*			enum rf_radio_path eRFPath,	Radio path of A/B/C/D
*			u32			RegAddr,	The target address to be modified
*			u32			BitMask		The target bit position in the target address
*									to be modified
*			u32			Data		The new register Data in the target bit position
*									of the target address
*
* Output:	None
* Return:		None
* Note:		This function is equal to "PutRFRegSetting" in PHY programming guide
*/
void
rtl8188e_PHY_SetRFReg(
		struct adapter *Adapter,
		enum rf_radio_path eRFPath,
		u32 RegAddr,
		u32 BitMask,
		u32 Data
	)
{
	u32 Original_Value, BitShift;

	/*  RF data is 12 bits only */
	if (BitMask != bRFRegOffsetMask) {
		Original_Value = rf_serial_read(Adapter, eRFPath, RegAddr);
		BitShift =  cal_bit_shift(BitMask);
		Data = ((Original_Value & (~BitMask)) | (Data << BitShift));
	}

	rf_serial_write(Adapter, eRFPath, RegAddr, Data);
}

static void getTxPowerIndex88E(struct adapter *Adapter, u8 channel, u8 *cckPowerLevel,
			       u8 *ofdmPowerLevel, u8 *BW20PowerLevel,
			       u8 *BW40PowerLevel)
{
	struct hal_data_8188e *pHalData = GET_HAL_DATA(Adapter);
	u8 index = (channel - 1);
	u8 TxCount = 0, path_nums;

	if ((RF_1T2R == pHalData->rf_type) || (RF_1T1R == pHalData->rf_type))
		path_nums = 1;
	else
		path_nums = 2;

	for (TxCount = 0; TxCount < path_nums; TxCount++) {
		if (TxCount == RF_PATH_A) {
			/*  1. CCK */
			cckPowerLevel[TxCount]	= pHalData->Index24G_CCK_Base[TxCount][index];
			/* 2. OFDM */
			ofdmPowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->OFDM_24G_Diff[TxCount][RF_PATH_A];
			/*  1. BW20 */
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
				pHalData->BW20_24G_Diff[TxCount][RF_PATH_A];
			/* 2. BW40 */
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount == RF_PATH_B) {
			/*  1. CCK */
			cckPowerLevel[TxCount]	= pHalData->Index24G_CCK_Base[TxCount][index];
			/* 2. OFDM */
			ofdmPowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[TxCount][index];
			/*  1. BW20 */
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[TxCount][RF_PATH_A]+
			pHalData->BW20_24G_Diff[TxCount][index];
			/* 2. BW40 */
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount == RF_PATH_C) {
			/*  1. CCK */
			cckPowerLevel[TxCount]	= pHalData->Index24G_CCK_Base[TxCount][index];
			/* 2. OFDM */
			ofdmPowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_B][index]+
			pHalData->BW20_24G_Diff[TxCount][index];
			/*  1. BW20 */
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_B][index]+
			pHalData->BW20_24G_Diff[TxCount][index];
			/* 2. BW40 */
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount == RF_PATH_D) {
			/*  1. CCK */
			cckPowerLevel[TxCount]	= pHalData->Index24G_CCK_Base[TxCount][index];
			/* 2. OFDM */
			ofdmPowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_B][index]+
			pHalData->BW20_24G_Diff[RF_PATH_C][index]+
			pHalData->BW20_24G_Diff[TxCount][index];

			/*  1. BW20 */
			BW20PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_A][index]+
			pHalData->BW20_24G_Diff[RF_PATH_B][index]+
			pHalData->BW20_24G_Diff[RF_PATH_C][index]+
			pHalData->BW20_24G_Diff[TxCount][index];

			/* 2. BW40 */
			BW40PowerLevel[TxCount]	= pHalData->Index24G_BW40_Base[TxCount][index];
		}
	}
}

static void phy_PowerIndexCheck88E(struct adapter *Adapter, u8 channel, u8 *cckPowerLevel,
				   u8 *ofdmPowerLevel, u8 *BW20PowerLevel, u8 *BW40PowerLevel)
{
	struct hal_data_8188e		*pHalData = GET_HAL_DATA(Adapter);

	pHalData->CurrentCckTxPwrIdx = cckPowerLevel[0];
	pHalData->CurrentOfdm24GTxPwrIdx = ofdmPowerLevel[0];
	pHalData->CurrentBW2024GTxPwrIdx = BW20PowerLevel[0];
	pHalData->CurrentBW4024GTxPwrIdx = BW40PowerLevel[0];
}

/*-----------------------------------------------------------------------------
 * Function:    SetTxPowerLevel8190()
 *
 * Overview:    This function is export to "HalCommon" moudule
 *			We must consider RF path later!!!!!!!
 *
 * Input:       struct adapter *Adapter
 *			u8		channel
 *
 * Output:      NONE
 *
 * Return:      NONE
 *	2008/11/04	MHC		We remove EEPROM_93C56.
 *						We need to move CCX relative code to independet file.
 *	2009/01/21	MHC		Support new EEPROM format from SD3 requirement.
 *
 *---------------------------------------------------------------------------*/
void
PHY_SetTxPowerLevel8188E(
		struct adapter *Adapter,
		u8 channel
	)
{
	u8 cckPowerLevel[MAX_TX_COUNT] = {0};
	u8 ofdmPowerLevel[MAX_TX_COUNT] = {0};/*  [0]:RF-A, [1]:RF-B */
	u8 BW20PowerLevel[MAX_TX_COUNT] = {0};
	u8 BW40PowerLevel[MAX_TX_COUNT] = {0};

	getTxPowerIndex88E(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0], &BW20PowerLevel[0], &BW40PowerLevel[0]);

	phy_PowerIndexCheck88E(Adapter, channel, &cckPowerLevel[0], &ofdmPowerLevel[0], &BW20PowerLevel[0], &BW40PowerLevel[0]);

	rtl8188e_PHY_RF6052SetCckTxPower(Adapter, &cckPowerLevel[0]);
	rtl8188e_PHY_RF6052SetOFDMTxPower(Adapter, &ofdmPowerLevel[0], &BW20PowerLevel[0], &BW40PowerLevel[0], channel);
}

/*-----------------------------------------------------------------------------
 * Function:    PHY_SetBWModeCallback8192C()
 *
 * Overview:    Timer callback function for SetSetBWMode
 *
 * Input:		PRT_TIMER		pTimer
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		(1) We do not take j mode into consideration now
 *			(2) Will two workitem of "switch channel" and "switch channel bandwidth" run
 *			     concurrently?
 *---------------------------------------------------------------------------*/
static void
_PHY_SetBWMode92C(
		struct adapter *Adapter
)
{
	struct hal_data_8188e *pHalData = GET_HAL_DATA(Adapter);
	u8 regBwOpMode;
	u8 regRRSR_RSC;

	if (pHalData->rf_chip == RF_PSEUDO_11N)
		return;

	/*  There is no 40MHz mode in RF_8225. */
	if (pHalData->rf_chip == RF_8225)
		return;

	if (Adapter->bDriverStopped)
		return;

	/* 3 */
	/* 3<1>Set MAC register */
	/* 3 */

	regBwOpMode = usb_read8(Adapter, REG_BWOPMODE);
	regRRSR_RSC = usb_read8(Adapter, REG_RRSR+2);

	switch (pHalData->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		regBwOpMode |= BW_OPMODE_20MHZ;
		/*  2007/02/07 Mark by Emily because we have not verify whether this register works */
		usb_write8(Adapter, REG_BWOPMODE, regBwOpMode);
		break;
	case HT_CHANNEL_WIDTH_40:
		regBwOpMode &= ~BW_OPMODE_20MHZ;
		/*  2007/02/07 Mark by Emily because we have not verify whether this register works */
		usb_write8(Adapter, REG_BWOPMODE, regBwOpMode);
		regRRSR_RSC = (regRRSR_RSC&0x90) | (pHalData->nCur40MhzPrimeSC<<5);
		usb_write8(Adapter, REG_RRSR+2, regRRSR_RSC);
		break;
	default:
		break;
	}

	/* 3  */
	/* 3 <2>Set PHY related register */
	/* 3 */
	switch (pHalData->CurrentChannelBW) {
	/* 20 MHz channel*/
	case HT_CHANNEL_WIDTH_20:
		phy_set_bb_reg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x0);
		phy_set_bb_reg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x0);
		break;
	/* 40 MHz channel*/
	case HT_CHANNEL_WIDTH_40:
		phy_set_bb_reg(Adapter, rFPGA0_RFMOD, bRFMOD, 0x1);
		phy_set_bb_reg(Adapter, rFPGA1_RFMOD, bRFMOD, 0x1);
		/*  Set Control channel to upper or lower. These settings are required only for 40MHz */
		phy_set_bb_reg(Adapter, rCCK0_System, bCCKSideBand, (pHalData->nCur40MhzPrimeSC>>1));
		phy_set_bb_reg(Adapter, rOFDM1_LSTF, 0xC00, pHalData->nCur40MhzPrimeSC);
		phy_set_bb_reg(Adapter, 0x818, (BIT26 | BIT27),
			     (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		break;
	}
	/* Skip over setting of J-mode in BB register here. Default value is "None J mode". Emily 20070315 */

	/* 3<3>Set RF related register */
	switch (pHalData->rf_chip) {
	case RF_8225:
		break;
	case RF_8256:
		/*  Please implement this function in Hal8190PciPhy8256.c */
		break;
	case RF_8258:
		/*  Please implement this function in Hal8190PciPhy8258.c */
		break;
	case RF_PSEUDO_11N:
		break;
	case RF_6052:
		rtl8188e_PHY_RF6052SetBandwidth(Adapter, pHalData->CurrentChannelBW);
		break;
	default:
		break;
	}
}

 /*-----------------------------------------------------------------------------
 * Function:   SetBWMode8190Pci()
 *
 * Overview:  This function is export to "HalCommon" moudule
 *
 * Input:		struct adapter *Adapter
 *			enum ht_channel_width Bandwidth	20M or 40M
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Note:		We do not take j mode into consideration now
 *---------------------------------------------------------------------------*/
void PHY_SetBWMode8188E(struct adapter *Adapter, enum ht_channel_width Bandwidth,	/*  20M or 40M */
			unsigned char	Offset)		/*  Upper, Lower, or Don't care */
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	enum ht_channel_width tmpBW = pHalData->CurrentChannelBW;

	pHalData->CurrentChannelBW = Bandwidth;

	pHalData->nCur40MhzPrimeSC = Offset;

	if ((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved))
		_PHY_SetBWMode92C(Adapter);
	else
		pHalData->CurrentChannelBW = tmpBW;
}

static void _PHY_SwChnl8192C(struct adapter *Adapter, u8 channel)
{
	u8 eRFPath;
	u32 param1, param2;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);

	if (Adapter->bNotifyChannelChange)
		DBG_88E("[%s] ch = %d\n", __func__, channel);

	/* s1. pre common command - CmdID_SetTxPowerLevel */
	PHY_SetTxPowerLevel8188E(Adapter, channel);

	/* s2. RF dependent command - CmdID_RF_WriteReg, param1=RF_CHNLBW, param2=channel */
	param1 = RF_CHNLBW;
	param2 = channel;
	for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++) {
		pHalData->RfRegChnlVal[eRFPath] = ((pHalData->RfRegChnlVal[eRFPath] & 0xfffffc00) | param2);
		PHY_SetRFReg(Adapter, (enum rf_radio_path)eRFPath, param1, bRFRegOffsetMask, pHalData->RfRegChnlVal[eRFPath]);
	}
}

void PHY_SwChnl8188E(struct adapter *Adapter, u8 channel)
{
	/*  Call after initialization */
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	u8 tmpchannel = pHalData->CurrentChannel;
	bool  bResult = true;

	if (pHalData->rf_chip == RF_PSEUDO_11N)
		return;		/* return immediately if it is peudo-phy */

	if (channel == 0)
		channel = 1;

	pHalData->CurrentChannel = channel;

	if ((!Adapter->bDriverStopped) && (!Adapter->bSurpriseRemoved)) {
		_PHY_SwChnl8192C(Adapter, channel);

		if (bResult)
			;
		else
			pHalData->CurrentChannel = tmpchannel;

	} else {
		pHalData->CurrentChannel = tmpchannel;
	}
}
