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
 ******************************************************************************/

#include <osdep_service.h>
#include <drv_types.h>
#include <phy.h>
#include <rf.h>
#include <rtl8188e_hal.h>

void rtl88eu_phy_rf6052_set_bandwidth(struct adapter *adapt,
				      enum ht_channel_width bandwidth)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);

	switch (bandwidth) {
	case HT_CHANNEL_WIDTH_20:
		hal_data->RfRegChnlVal[0] = ((hal_data->RfRegChnlVal[0] &
					      0xfffff3ff) | BIT(10) | BIT(11));
		phy_set_rf_reg(adapt, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask,
			       hal_data->RfRegChnlVal[0]);
		break;
	case HT_CHANNEL_WIDTH_40:
		hal_data->RfRegChnlVal[0] = ((hal_data->RfRegChnlVal[0] &
					      0xfffff3ff) | BIT(10));
		phy_set_rf_reg(adapt, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask,
			       hal_data->RfRegChnlVal[0]);
		break;
	default:
		break;
	}
}

void rtl88eu_phy_rf6052_set_cck_txpower(struct adapter *adapt, u8 *powerlevel)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	struct dm_priv *pdmpriv = &hal_data->dmpriv;
	struct mlme_ext_priv *pmlmeext = &adapt->mlmeextpriv;
	u32 tx_agc[2] = {0, 0}, tmpval = 0, pwrtrac_value;
	u8 idx1, idx2;
	u8 *ptr;
	u8 direction;


	if (pmlmeext->sitesurvey_res.state == SCAN_PROCESS) {
		tx_agc[RF_PATH_A] = 0x3f3f3f3f;
		tx_agc[RF_PATH_B] = 0x3f3f3f3f;
		for (idx1 = RF_PATH_A; idx1 <= RF_PATH_B; idx1++) {
			tx_agc[idx1] = powerlevel[idx1] |
				      (powerlevel[idx1]<<8) |
				      (powerlevel[idx1]<<16) |
				      (powerlevel[idx1]<<24);
			if (tx_agc[idx1] > 0x20 && hal_data->ExternalPA)
				tx_agc[idx1] = 0x20;
		}
	} else {
		if (pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1) {
			tx_agc[RF_PATH_A] = 0x10101010;
			tx_agc[RF_PATH_B] = 0x10101010;
		} else if (pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level2) {
			tx_agc[RF_PATH_A] = 0x00000000;
			tx_agc[RF_PATH_B] = 0x00000000;
		} else {
			for (idx1 = RF_PATH_A; idx1 <= RF_PATH_B; idx1++) {
				tx_agc[idx1] = powerlevel[idx1] |
					       (powerlevel[idx1]<<8) |
					       (powerlevel[idx1]<<16) |
					       (powerlevel[idx1]<<24);
			}
			if (hal_data->EEPROMRegulatory == 0) {
				tmpval = hal_data->MCSTxPowerLevelOriginalOffset[0][6] +
					 (hal_data->MCSTxPowerLevelOriginalOffset[0][7]<<8);
				tx_agc[RF_PATH_A] += tmpval;

				tmpval = hal_data->MCSTxPowerLevelOriginalOffset[0][14] +
					 (hal_data->MCSTxPowerLevelOriginalOffset[0][15]<<24);
				tx_agc[RF_PATH_B] += tmpval;
			}
		}
	}
	for (idx1 = RF_PATH_A; idx1 <= RF_PATH_B; idx1++) {
		ptr = (u8 *)(&(tx_agc[idx1]));
		for (idx2 = 0; idx2 < 4; idx2++) {
			if (*ptr > RF6052_MAX_TX_PWR)
				*ptr = RF6052_MAX_TX_PWR;
			ptr++;
		}
	}
	rtl88eu_dm_txpower_track_adjust(&hal_data->odmpriv, 1, &direction,
					&pwrtrac_value);

	if (direction == 1) {
		/*  Increase TX power */
		tx_agc[0] += pwrtrac_value;
		tx_agc[1] += pwrtrac_value;
	} else if (direction == 2) {
		/*  Decrease TX power */
		tx_agc[0] -=  pwrtrac_value;
		tx_agc[1] -=  pwrtrac_value;
	}

	/*  rf-A cck tx power */
	tmpval = tx_agc[RF_PATH_A]&0xff;
	phy_set_bb_reg(adapt, rTxAGC_A_CCK1_Mcs32, bMaskByte1, tmpval);
	tmpval = tx_agc[RF_PATH_A]>>8;
	phy_set_bb_reg(adapt, rTxAGC_B_CCK11_A_CCK2_11, 0xffffff00, tmpval);

	/*  rf-B cck tx power */
	tmpval = tx_agc[RF_PATH_B]>>24;
	phy_set_bb_reg(adapt, rTxAGC_B_CCK11_A_CCK2_11, bMaskByte0, tmpval);
	tmpval = tx_agc[RF_PATH_B]&0x00ffffff;
	phy_set_bb_reg(adapt, rTxAGC_B_CCK1_55_Mcs32, 0xffffff00, tmpval);
}

/*  powerbase0 for OFDM rates */
/*  powerbase1 for HT MCS rates */
static void getpowerbase88e(struct adapter *adapt, u8 *pwr_level_ofdm,
			    u8 *pwr_level_bw20, u8 *pwr_level_bw40,
			    u8 channel, u32 *ofdmbase, u32 *mcs_base)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	u32 powerbase0, powerbase1;
	u8 i, powerlevel[2];

	for (i = 0; i < 2; i++) {
		powerbase0 = pwr_level_ofdm[i];

		powerbase0 = (powerbase0<<24) | (powerbase0<<16) |
			     (powerbase0<<8) | powerbase0;
		*(ofdmbase+i) = powerbase0;
	}
	for (i = 0; i < hal_data->NumTotalRFPath; i++) {
		/* Check HT20 to HT40 diff */
		if (hal_data->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
			powerlevel[i] = pwr_level_bw20[i];
		else
			powerlevel[i] = pwr_level_bw40[i];
		powerbase1 = powerlevel[i];
		powerbase1 = (powerbase1<<24) | (powerbase1<<16) |
			     (powerbase1<<8) | powerbase1;
		*(mcs_base+i) = powerbase1;
	}
}
static void get_rx_power_val_by_reg(struct adapter *adapt, u8 channel,
				    u8 index, u32 *powerbase0, u32 *powerbase1,
				    u32 *out_val)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	struct dm_priv	*pdmpriv = &hal_data->dmpriv;
	u8 i, chnlGroup = 0, pwr_diff_limit[4], customer_pwr_limit;
	s8 pwr_diff = 0;
	u32 write_val, customer_limit, rf;
	u8 regulatory = hal_data->EEPROMRegulatory;

	/*  Index 0 & 1= legacy OFDM, 2-5=HT_MCS rate */

	for (rf = 0; rf < 2; rf++) {
		u8 j = index + (rf ? 8 : 0);

		switch (regulatory) {
		case 0:
			chnlGroup = 0;
			write_val = hal_data->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf ? 8 : 0)] +
				((index < 2) ? powerbase0[rf] : powerbase1[rf]);
			break;
		case 1: /*  Realtek regulatory */
			/*  increase power diff defined by Realtek for regulatory */
			if (hal_data->pwrGroupCnt == 1)
				chnlGroup = 0;
			if (hal_data->pwrGroupCnt >= hal_data->PGMaxGroup) {
				if (channel < 3)
					chnlGroup = 0;
				else if (channel < 6)
					chnlGroup = 1;
				else if (channel < 9)
					chnlGroup = 2;
				else if (channel < 12)
					chnlGroup = 3;
				else if (channel < 14)
					chnlGroup = 4;
				else if (channel == 14)
					chnlGroup = 5;
			}
			write_val = hal_data->MCSTxPowerLevelOriginalOffset[chnlGroup][index+(rf ? 8 : 0)] +
					((index < 2) ? powerbase0[rf] : powerbase1[rf]);
			break;
		case 2:	/*  Better regulatory */
				/*  don't increase any power diff */
			write_val = (index < 2) ? powerbase0[rf] : powerbase1[rf];
			break;
		case 3:	/*  Customer defined power diff. */
				/*  increase power diff defined by customer. */
			chnlGroup = 0;

			if (index < 2)
				pwr_diff = hal_data->TxPwrLegacyHtDiff[rf][channel-1];
			else if (hal_data->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
				pwr_diff = hal_data->TxPwrHt20Diff[rf][channel-1];

			if (hal_data->CurrentChannelBW == HT_CHANNEL_WIDTH_40)
				customer_pwr_limit = hal_data->PwrGroupHT40[rf][channel-1];
			else
				customer_pwr_limit = hal_data->PwrGroupHT20[rf][channel-1];

			if (pwr_diff >= customer_pwr_limit)
				pwr_diff = 0;
			else
				pwr_diff = customer_pwr_limit - pwr_diff;

			for (i = 0; i < 4; i++) {
				pwr_diff_limit[i] = (u8)((hal_data->MCSTxPowerLevelOriginalOffset[chnlGroup][j] &
							 (0x7f << (i * 8))) >> (i * 8));

				if (pwr_diff_limit[i] > pwr_diff)
					pwr_diff_limit[i] = pwr_diff;
			}
			customer_limit = (pwr_diff_limit[3]<<24) |
					 (pwr_diff_limit[2]<<16) |
					 (pwr_diff_limit[1]<<8) |
					 (pwr_diff_limit[0]);
			write_val = customer_limit + ((index < 2) ? powerbase0[rf] : powerbase1[rf]);
			break;
		default:
			chnlGroup = 0;
			write_val = hal_data->MCSTxPowerLevelOriginalOffset[chnlGroup][j] +
					((index < 2) ? powerbase0[rf] : powerbase1[rf]);
			break;
		}
/*  20100427 Joseph: Driver dynamic Tx power shall not affect Tx power. It shall be determined by power training mechanism. */
/*  Currently, we cannot fully disable driver dynamic tx power mechanism because it is referenced by BT coexist mechanism. */
/*  In the future, two mechanism shall be separated from each other and maintained independently. Thanks for Lanhsin's reminder. */
		/* 92d do not need this */
		if (pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1)
			write_val = 0x14141414;
		else if (pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level2)
			write_val = 0x00000000;

		*(out_val+rf) = write_val;
	}
}

static void write_ofdm_pwr_reg(struct adapter *adapt, u8 index, u32 *pvalue)
{
	u16 regoffset_a[6] = { rTxAGC_A_Rate18_06, rTxAGC_A_Rate54_24,
			       rTxAGC_A_Mcs03_Mcs00, rTxAGC_A_Mcs07_Mcs04,
			       rTxAGC_A_Mcs11_Mcs08, rTxAGC_A_Mcs15_Mcs12 };
	u16 regoffset_b[6] = { rTxAGC_B_Rate18_06, rTxAGC_B_Rate54_24,
			       rTxAGC_B_Mcs03_Mcs00, rTxAGC_B_Mcs07_Mcs04,
			       rTxAGC_B_Mcs11_Mcs08, rTxAGC_B_Mcs15_Mcs12 };
	u8 i, rf, pwr_val[4];
	u32 write_val;
	u16 regoffset;

	for (rf = 0; rf < 2; rf++) {
		write_val = pvalue[rf];
		for (i = 0; i < 4; i++) {
			pwr_val[i] = (u8)((write_val & (0x7f<<(i*8)))>>(i*8));
			if (pwr_val[i]  > RF6052_MAX_TX_PWR)
				pwr_val[i]  = RF6052_MAX_TX_PWR;
		}
		write_val = (pwr_val[3]<<24) | (pwr_val[2]<<16) |
			    (pwr_val[1]<<8) | pwr_val[0];

		if (rf == 0)
			regoffset = regoffset_a[index];
		else
			regoffset = regoffset_b[index];

		phy_set_bb_reg(adapt, regoffset, bMaskDWord, write_val);
	}
}

void rtl88eu_phy_rf6052_set_ofdm_txpower(struct adapter *adapt,
					 u8 *pwr_level_ofdm,
					 u8 *pwr_level_bw20,
					 u8 *pwr_level_bw40, u8 channel)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(adapt);
	u32 write_val[2], powerbase0[2], powerbase1[2], pwrtrac_value;
	u8 direction;
	u8 index = 0;

	getpowerbase88e(adapt, pwr_level_ofdm, pwr_level_bw20, pwr_level_bw40,
			channel, &powerbase0[0], &powerbase1[0]);

	rtl88eu_dm_txpower_track_adjust(&hal_data->odmpriv, 0, &direction,
					&pwrtrac_value);

	for (index = 0; index < 6; index++) {
		get_rx_power_val_by_reg(adapt, channel, index,
					&powerbase0[0], &powerbase1[0],
					&write_val[0]);

		if (direction == 1) {
			write_val[0] += pwrtrac_value;
			write_val[1] += pwrtrac_value;
		} else if (direction == 2) {
			write_val[0] -= pwrtrac_value;
			write_val[1] -= pwrtrac_value;
		}
		write_ofdm_pwr_reg(adapt, index, &write_val[0]);
	}
}
