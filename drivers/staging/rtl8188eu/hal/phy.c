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
#include <rf.h>
#include <phy.h>

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

void phy_set_rf_reg(struct adapter *adapt, enum rf_radio_path rf_path,
		     u32 reg_addr, u32 bit_mask, u32 data)
{
	u32 original_value, bit_shift;

	/*  RF data is 12 bits only */
	if (bit_mask != bRFRegOffsetMask) {
		original_value = rf_serial_read(adapt, rf_path, reg_addr);
		bit_shift =  cal_bit_shift(bit_mask);
		data = ((original_value & (~bit_mask)) | (data << bit_shift));
	}

	rf_serial_write(adapt, rf_path, reg_addr, data);
}

static void get_tx_power_index(struct adapter *adapt, u8 channel, u8 *cck_pwr,
			       u8 *ofdm_pwr, u8 *bw20_pwr, u8 *bw40_pwr)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	u8 index = (channel - 1);
	u8 TxCount = 0, path_nums;

	if ((RF_1T2R == hal_data->rf_type) || (RF_1T1R == hal_data->rf_type))
		path_nums = 1;
	else
		path_nums = 2;

	for (TxCount = 0; TxCount < path_nums; TxCount++) {
		if (TxCount == RF_PATH_A) {
			cck_pwr[TxCount] = hal_data->Index24G_CCK_Base[TxCount][index];
			ofdm_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index]+
					    hal_data->OFDM_24G_Diff[TxCount][RF_PATH_A];

			bw20_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index]+
					    hal_data->BW20_24G_Diff[TxCount][RF_PATH_A];
			bw40_pwr[TxCount] = hal_data->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount == RF_PATH_B) {
			cck_pwr[TxCount] = hal_data->Index24G_CCK_Base[TxCount][index];
			ofdm_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[TxCount][index];

			bw20_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[TxCount][RF_PATH_A]+
			hal_data->BW20_24G_Diff[TxCount][index];
			bw40_pwr[TxCount] = hal_data->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount == RF_PATH_C) {
			cck_pwr[TxCount] = hal_data->Index24G_CCK_Base[TxCount][index];
			ofdm_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[RF_PATH_B][index]+
			hal_data->BW20_24G_Diff[TxCount][index];

			bw20_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[RF_PATH_B][index]+
			hal_data->BW20_24G_Diff[TxCount][index];
			bw40_pwr[TxCount] = hal_data->Index24G_BW40_Base[TxCount][index];
		} else if (TxCount == RF_PATH_D) {
			cck_pwr[TxCount] = hal_data->Index24G_CCK_Base[TxCount][index];
			ofdm_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[RF_PATH_B][index]+
			hal_data->BW20_24G_Diff[RF_PATH_C][index]+
			hal_data->BW20_24G_Diff[TxCount][index];

			bw20_pwr[TxCount] = hal_data->Index24G_BW40_Base[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[RF_PATH_A][index]+
			hal_data->BW20_24G_Diff[RF_PATH_B][index]+
			hal_data->BW20_24G_Diff[RF_PATH_C][index]+
			hal_data->BW20_24G_Diff[TxCount][index];
			bw40_pwr[TxCount] = hal_data->Index24G_BW40_Base[TxCount][index];
		}
	}
}

static void phy_power_index_check(struct adapter *adapt, u8 channel,
				  u8 *cck_pwr, u8 *ofdm_pwr, u8 *bw20_pwr,
				  u8 *bw40_pwr)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);

	hal_data->CurrentCckTxPwrIdx = cck_pwr[0];
	hal_data->CurrentOfdm24GTxPwrIdx = ofdm_pwr[0];
	hal_data->CurrentBW2024GTxPwrIdx = bw20_pwr[0];
	hal_data->CurrentBW4024GTxPwrIdx = bw40_pwr[0];
}

void phy_set_tx_power_level(struct adapter *adapt, u8 channel)
{
	u8 cck_pwr[MAX_TX_COUNT] = {0};
	u8 ofdm_pwr[MAX_TX_COUNT] = {0};/*  [0]:RF-A, [1]:RF-B */
	u8 bw20_pwr[MAX_TX_COUNT] = {0};
	u8 bw40_pwr[MAX_TX_COUNT] = {0};

	get_tx_power_index(adapt, channel, &cck_pwr[0], &ofdm_pwr[0],
			   &bw20_pwr[0], &bw40_pwr[0]);

	phy_power_index_check(adapt, channel, &cck_pwr[0], &ofdm_pwr[0],
			      &bw20_pwr[0], &bw40_pwr[0]);

	rtl88eu_phy_rf6052_set_cck_txpower(adapt, &cck_pwr[0]);
	rtl88eu_phy_rf6052_set_ofdm_txpower(adapt, &ofdm_pwr[0], &bw20_pwr[0],
					  &bw40_pwr[0], channel);
}

static void phy_set_bw_mode_callback(struct adapter *adapt)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	u8 reg_bw_opmode;
	u8 reg_prsr_rsc;

	if (hal_data->rf_chip == RF_PSEUDO_11N)
		return;

	/*  There is no 40MHz mode in RF_8225. */
	if (hal_data->rf_chip == RF_8225)
		return;

	if (adapt->bDriverStopped)
		return;

	/* Set MAC register */

	reg_bw_opmode = usb_read8(adapt, REG_BWOPMODE);
	reg_prsr_rsc = usb_read8(adapt, REG_RRSR+2);

	switch (hal_data->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		reg_bw_opmode |= BW_OPMODE_20MHZ;
		usb_write8(adapt, REG_BWOPMODE, reg_bw_opmode);
		break;
	case HT_CHANNEL_WIDTH_40:
		reg_bw_opmode &= ~BW_OPMODE_20MHZ;
		usb_write8(adapt, REG_BWOPMODE, reg_bw_opmode);
		reg_prsr_rsc = (reg_prsr_rsc&0x90) |
			       (hal_data->nCur40MhzPrimeSC<<5);
		usb_write8(adapt, REG_RRSR+2, reg_prsr_rsc);
		break;
	default:
		break;
	}

	/* Set PHY related register */
	switch (hal_data->CurrentChannelBW) {
	case HT_CHANNEL_WIDTH_20:
		phy_set_bb_reg(adapt, rFPGA0_RFMOD, bRFMOD, 0x0);
		phy_set_bb_reg(adapt, rFPGA1_RFMOD, bRFMOD, 0x0);
		break;
	case HT_CHANNEL_WIDTH_40:
		phy_set_bb_reg(adapt, rFPGA0_RFMOD, bRFMOD, 0x1);
		phy_set_bb_reg(adapt, rFPGA1_RFMOD, bRFMOD, 0x1);
		/* Set Control channel to upper or lower.
		 * These settings are required only for 40MHz
		 */
		phy_set_bb_reg(adapt, rCCK0_System, bCCKSideBand,
		    (hal_data->nCur40MhzPrimeSC>>1));
		phy_set_bb_reg(adapt, rOFDM1_LSTF, 0xC00,
			       hal_data->nCur40MhzPrimeSC);
		phy_set_bb_reg(adapt, 0x818, (BIT26 | BIT27),
		   (hal_data->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) ? 2 : 1);
		break;
	default:
		break;
	}

	/* Set RF related register */
	switch (hal_data->rf_chip) {
	case RF_8225:
		break;
	case RF_8256:
		break;
	case RF_8258:
		break;
	case RF_PSEUDO_11N:
		break;
	case RF_6052:
		rtl88eu_phy_rf6052_set_bandwidth(adapt, hal_data->CurrentChannelBW);
		break;
	default:
		break;
	}
}

void phy_set_bw_mode(struct adapter *adapt, enum ht_channel_width bandwidth,
		     unsigned char offset)
{
	struct hal_data_8188e	*hal_data = GET_HAL_DATA(adapt);
	enum ht_channel_width tmp_bw = hal_data->CurrentChannelBW;

	hal_data->CurrentChannelBW = bandwidth;
	hal_data->nCur40MhzPrimeSC = offset;

	if ((!adapt->bDriverStopped) && (!adapt->bSurpriseRemoved))
		phy_set_bw_mode_callback(adapt);
	else
		hal_data->CurrentChannelBW = tmp_bw;
}

static void phy_sw_chnl_callback(struct adapter *adapt, u8 channel)
{
	u8 rf_path;
	u32 param1, param2;
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);

	if (adapt->bNotifyChannelChange)
		DBG_88E("[%s] ch = %d\n", __func__, channel);

	phy_set_tx_power_level(adapt, channel);

	param1 = RF_CHNLBW;
	param2 = channel;
	for (rf_path = 0; rf_path < hal_data->NumTotalRFPath; rf_path++) {
		hal_data->RfRegChnlVal[rf_path] = (hal_data->RfRegChnlVal[rf_path] &
						  0xfffffc00) | param2;
		phy_set_rf_reg(adapt, (enum rf_radio_path)rf_path, param1,
			       bRFRegOffsetMask, hal_data->RfRegChnlVal[rf_path]);
	}
}

void phy_sw_chnl(struct adapter *adapt, u8 channel)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	u8 tmpchannel = hal_data->CurrentChannel;
	bool  result = true;

	if (hal_data->rf_chip == RF_PSEUDO_11N)
		return;

	if (channel == 0)
		channel = 1;

	hal_data->CurrentChannel = channel;

	if ((!adapt->bDriverStopped) && (!adapt->bSurpriseRemoved)) {
		phy_sw_chnl_callback(adapt, channel);

		if (!result)
			hal_data->CurrentChannel = tmpchannel;

	} else {
		hal_data->CurrentChannel = tmpchannel;
	}
}
