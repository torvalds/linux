// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2024  Realtek Corporation
 */

#include "main.h"
#include "coex.h"
#include "phy.h"
#include "reg.h"
#include "rtw88xxa.h"
#include "rtw8821a.h"
#include "rtw8821a_table.h"
#include "tx.h"

static void rtw8821a_power_off(struct rtw_dev *rtwdev)
{
	rtw88xxa_power_off(rtwdev, enter_lps_flow_8821a);
}

static s8 rtw8821a_cck_rx_pwr(u8 lna_idx, u8 vga_idx)
{
	static const s8 lna_gain_table[] = {15, -1, -17, 0, -30, -38};
	s8 rx_pwr_all = 0;
	s8 lna_gain;

	switch (lna_idx) {
	case 5:
	case 4:
	case 2:
	case 1:
	case 0:
		lna_gain = lna_gain_table[lna_idx];
		rx_pwr_all = lna_gain - 2 * vga_idx;
		break;
	default:
		break;
	}

	return rx_pwr_all;
}

static void rtw8821a_query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
				      struct rtw_rx_pkt_stat *pkt_stat)
{
	rtw88xxa_query_phy_status(rtwdev, phy_status, pkt_stat,
				  rtw8821a_cck_rx_pwr);
}

static void rtw8821a_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
}

#define CAL_NUM_8821A 3
#define MACBB_REG_NUM_8821A 8
#define AFE_REG_NUM_8821A 4
#define RF_REG_NUM_8821A 3

static void rtw8821a_iqk_backup_rf(struct rtw_dev *rtwdev, u32 *rfa_backup,
				   const u32 *backup_rf_reg, u32 rf_num)
{
	u32 i;

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	/* Save RF Parameters */
	for (i = 0; i < rf_num; i++)
		rfa_backup[i] = rtw_read_rf(rtwdev, RF_PATH_A,
					    backup_rf_reg[i], MASKDWORD);
}

static void rtw8821a_iqk_restore_rf(struct rtw_dev *rtwdev,
				    const u32 *backup_rf_reg,
				    u32 *RF_backup, u32 rf_reg_num)
{
	u32 i;

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	for (i = 0; i < rf_reg_num; i++)
		rtw_write_rf(rtwdev, RF_PATH_A, backup_rf_reg[i],
			     RFREG_MASK, RF_backup[i]);
}

static void rtw8821a_iqk_restore_afe(struct rtw_dev *rtwdev, u32 *afe_backup,
				     const u32 *backup_afe_reg, u32 afe_num)
{
	u32 i;

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	/* Reload AFE Parameters */
	for (i = 0; i < afe_num; i++)
		rtw_write32(rtwdev, backup_afe_reg[i], afe_backup[i]);

	/* [31] = 1 --> Page C1 */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);

	rtw_write32(rtwdev, REG_OFDM0_XA_TX_IQ_IMBALANCE, 0x0);
	rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x0);
	rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE, 0x0);
	rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x3c000000);
	rtw_write32(rtwdev, REG_LSSI_WRITE_A, 0x00000080);
	rtw_write32(rtwdev, REG_TXAGCIDX, 0x00000000);
	rtw_write32(rtwdev, REG_IQK_DPD_CFG, 0x20040000);
	rtw_write32(rtwdev, REG_CFG_PMPD, 0x20000000);
	rtw_write32(rtwdev, REG_RFECTL_A, 0x0);
}

static void rtw8821a_iqk_rx_fill(struct rtw_dev *rtwdev,
				 unsigned int rx_x, unsigned int rx_y)
{
	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	rtw_write32_mask(rtwdev, REG_RX_IQC_AB_A,
			 0x000003ff, rx_x >> 1);
	rtw_write32_mask(rtwdev, REG_RX_IQC_AB_A,
			 0x03ff0000, (rx_y >> 1) & 0x3ff);
}

static void rtw8821a_iqk_tx_fill(struct rtw_dev *rtwdev,
				 unsigned int tx_x, unsigned int tx_y)
{
	/* [31] = 1 --> Page C1 */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);

	rtw_write32(rtwdev, REG_LSSI_WRITE_A, 0x00000080);
	rtw_write32(rtwdev, REG_IQK_DPD_CFG, 0x20040000);
	rtw_write32(rtwdev, REG_CFG_PMPD, 0x20000000);
	rtw_write32_mask(rtwdev, REG_IQC_Y, 0x000007ff, tx_y);
	rtw_write32_mask(rtwdev, REG_IQC_X, 0x000007ff, tx_x);
}

static void rtw8821a_iqk_tx_vdf_true(struct rtw_dev *rtwdev, u32 cal,
				     bool *tx0iqkok,
				     int tx_x0[CAL_NUM_8821A],
				     int tx_y0[CAL_NUM_8821A])
{
	u32 cal_retry, delay_count, iqk_ready, tx_fail;
	int tx_dt[3], vdf_y[3], vdf_x[3];
	int k;

	for (k = 0; k < 3; k++) {
		switch (k) {
		case 0:
			/* TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
			rtw_write32(rtwdev, REG_OFDM0_XA_TX_IQ_IMBALANCE,
				    0x18008c38);
			/* RX_Tone_idx[9:0], RxK_Mask[29] */
			rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x38008c38);
			rtw_write32_mask(rtwdev, REG_INTPO_SETA, BIT(31), 0x0);
			break;
		case 1:
			rtw_write32_mask(rtwdev, REG_OFDM0_XA_TX_IQ_IMBALANCE,
					 BIT(28), 0x0);
			rtw_write32_mask(rtwdev, REG_OFDM0_A_TX_AFE,
					 BIT(28), 0x0);
			rtw_write32_mask(rtwdev, REG_INTPO_SETA, BIT(31), 0x0);
			break;
		case 2:
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"vdf_y[1] = %x vdf_y[0] = %x\n",
				vdf_y[1] >> 21 & 0x00007ff,
				vdf_y[0] >> 21 & 0x00007ff);

			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"vdf_x[1] = %x vdf_x[0] = %x\n",
				vdf_x[1] >> 21 & 0x00007ff,
				vdf_x[0] >> 21 & 0x00007ff);

			tx_dt[cal] = (vdf_y[1] >> 20) - (vdf_y[0] >> 20);
			tx_dt[cal] = (16 * tx_dt[cal]) * 10000 / 15708;
			tx_dt[cal] = (tx_dt[cal] >> 1) + (tx_dt[cal] & BIT(0));

			/* TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
			rtw_write32(rtwdev, REG_OFDM0_XA_TX_IQ_IMBALANCE,
				    0x18008c20);
			/* RX_Tone_idx[9:0], RxK_Mask[29] */
			rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x38008c20);
			rtw_write32_mask(rtwdev, REG_INTPO_SETA, BIT(31), 0x1);
			rtw_write32_mask(rtwdev, REG_INTPO_SETA, 0x3fff0000,
					 tx_dt[cal] & 0x00003fff);
			break;
		}

		rtw_write32(rtwdev, REG_RFECTL_A, 0x00100000);

		for (cal_retry = 0; cal_retry < 10; cal_retry++) {
			/* one shot */
			rtw_write32(rtwdev, REG_IQK_COM64, 0xfa000000);
			rtw_write32(rtwdev, REG_IQK_COM64, 0xf8000000);

			mdelay(10);

			rtw_write32(rtwdev, REG_RFECTL_A, 0x00000000);

			for (delay_count = 0; delay_count < 20; delay_count++) {
				iqk_ready = rtw_read32_mask(rtwdev,
							    REG_IQKA_END,
							    BIT(10));

				/* Originally: if (~iqk_ready || delay_count > 20)
				 * that looks like a typo so make it more explicit
				 */
				iqk_ready = true;

				if (iqk_ready)
					break;

				mdelay(1);
			}

			if (delay_count < 20) {
				/* ============TXIQK Check============== */
				tx_fail = rtw_read32_mask(rtwdev,
							  REG_IQKA_END,
							  BIT(12));

				/* Originally: if (~tx_fail) {
				 * It looks like a typo, so make it more explicit.
				 */
				tx_fail = false;

				if (!tx_fail) {
					rtw_write32(rtwdev, REG_RFECTL_A,
						    0x02000000);
					vdf_x[k] = rtw_read32_mask(rtwdev,
								   REG_IQKA_END,
								   0x07ff0000);
					vdf_x[k] <<= 21;

					rtw_write32(rtwdev, REG_RFECTL_A,
						    0x04000000);
					vdf_y[k] = rtw_read32_mask(rtwdev,
								   REG_IQKA_END,
								   0x07ff0000);
					vdf_y[k] <<= 21;

					*tx0iqkok = true;
					break;
				}

				rtw_write32_mask(rtwdev, REG_IQC_Y,
						 0x000007ff, 0x0);
				rtw_write32_mask(rtwdev, REG_IQC_X,
						 0x000007ff, 0x200);
			}

			*tx0iqkok = false;
		}
	}

	if (k == 3) {
		tx_x0[cal] = vdf_x[k - 1];
		tx_y0[cal] = vdf_y[k - 1];
	}
}

static void rtw8821a_iqk_tx_vdf_false(struct rtw_dev *rtwdev, u32 cal,
				      bool *tx0iqkok,
				      int tx_x0[CAL_NUM_8821A],
				      int tx_y0[CAL_NUM_8821A])
{
	u32 cal_retry, delay_count, iqk_ready, tx_fail;

	/* TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
	rtw_write32(rtwdev, REG_OFDM0_XA_TX_IQ_IMBALANCE, 0x18008c10);
	/* RX_Tone_idx[9:0], RxK_Mask[29] */
	rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x38008c10);
	rtw_write32(rtwdev, REG_RFECTL_A, 0x00100000);

	for (cal_retry = 0; cal_retry < 10; cal_retry++) {
		/* one shot */
		rtw_write32(rtwdev, REG_IQK_COM64, 0xfa000000);
		rtw_write32(rtwdev, REG_IQK_COM64, 0xf8000000);

		mdelay(10);
		rtw_write32(rtwdev, REG_RFECTL_A, 0x00000000);

		for (delay_count = 0; delay_count < 20; delay_count++) {
			iqk_ready = rtw_read32_mask(rtwdev, REG_IQKA_END, BIT(10));

			/* Originally: if (~iqk_ready || delay_count > 20)
			 * that looks like a typo so make it more explicit
			 */
			iqk_ready = true;

			if (iqk_ready)
				break;

			mdelay(1);
		}

		if (delay_count < 20) {
			/* ============TXIQK Check============== */
			tx_fail = rtw_read32_mask(rtwdev, REG_IQKA_END, BIT(12));

			/* Originally: if (~tx_fail) {
			 * It looks like a typo, so make it more explicit.
			 */
			tx_fail = false;

			if (!tx_fail) {
				rtw_write32(rtwdev, REG_RFECTL_A, 0x02000000);
				tx_x0[cal] = rtw_read32_mask(rtwdev, REG_IQKA_END,
							     0x07ff0000);
				tx_x0[cal] <<= 21;

				rtw_write32(rtwdev, REG_RFECTL_A, 0x04000000);
				tx_y0[cal] = rtw_read32_mask(rtwdev, REG_IQKA_END,
							     0x07ff0000);
				tx_y0[cal] <<= 21;

				*tx0iqkok = true;
				break;
			}

			rtw_write32_mask(rtwdev, REG_IQC_Y, 0x000007ff, 0x0);
			rtw_write32_mask(rtwdev, REG_IQC_X, 0x000007ff, 0x200);
		}

		*tx0iqkok = false;
	}
}

static void rtw8821a_iqk_rx(struct rtw_dev *rtwdev, u32 cal, bool *rx0iqkok,
			    int rx_x0[CAL_NUM_8821A],
			    int rx_y0[CAL_NUM_8821A])
{
	u32 cal_retry, delay_count, iqk_ready, rx_fail;

	rtw_write32(rtwdev, REG_RFECTL_A, 0x00100000);

	for (cal_retry = 0; cal_retry < 10; cal_retry++) {
		/* one shot */
		rtw_write32(rtwdev, REG_IQK_COM64, 0xfa000000);
		rtw_write32(rtwdev, REG_IQK_COM64, 0xf8000000);

		mdelay(10);

		rtw_write32(rtwdev, REG_RFECTL_A, 0x00000000);

		for (delay_count = 0; delay_count < 20; delay_count++) {
			iqk_ready = rtw_read32_mask(rtwdev, REG_IQKA_END, BIT(10));

			/* Originally: if (~iqk_ready || delay_count > 20)
			 * that looks like a typo so make it more explicit
			 */
			iqk_ready = true;

			if (iqk_ready)
				break;

			mdelay(1);
		}

		if (delay_count < 20) {
			/* ============RXIQK Check============== */
			rx_fail = rtw_read32_mask(rtwdev, REG_IQKA_END, BIT(11));
			if (!rx_fail) {
				rtw_write32(rtwdev, REG_RFECTL_A, 0x06000000);
				rx_x0[cal] = rtw_read32_mask(rtwdev, REG_IQKA_END,
							     0x07ff0000);
				rx_x0[cal] <<= 21;

				rtw_write32(rtwdev, REG_RFECTL_A, 0x08000000);
				rx_y0[cal] = rtw_read32_mask(rtwdev, REG_IQKA_END,
							     0x07ff0000);
				rx_y0[cal] <<= 21;

				*rx0iqkok = true;
				break;
			}

			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_A,
					 0x000003ff, 0x200 >> 1);
			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_A,
					 0x03ff0000, 0x0 >> 1);
		}

		*rx0iqkok = false;
	}
}

static void rtw8821a_iqk(struct rtw_dev *rtwdev)
{
	int tx_average = 0, rx_average = 0, rx_iqk_loop = 0;
	const struct rtw_efuse *efuse = &rtwdev->efuse;
	int tx_x = 0, tx_y = 0, rx_x = 0, rx_y = 0;
	const struct rtw_hal *hal = &rtwdev->hal;
	bool tx0iqkok = false, rx0iqkok = false;
	int rx_x_temp = 0, rx_y_temp = 0;
	int rx_x0[2][CAL_NUM_8821A];
	int rx_y0[2][CAL_NUM_8821A];
	int tx_x0[CAL_NUM_8821A];
	int tx_y0[CAL_NUM_8821A];
	bool rx_finish1 = false;
	bool rx_finish2 = false;
	bool vdf_enable;
	u32 cal;
	int i;

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"band_width = %d, ext_pa = %d, ext_pa_5g = %d\n",
		hal->current_band_width, efuse->ext_pa_2g, efuse->ext_pa_5g);

	vdf_enable = hal->current_band_width == RTW_CHANNEL_WIDTH_80;

	for (cal = 0; cal < CAL_NUM_8821A; cal++) {
		/* path-A LOK */

		/* [31] = 0 --> Page C */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

		/* ========path-A AFE all on======== */
		/* Port 0 DAC/ADC on */
		rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x77777777);
		rtw_write32(rtwdev, REG_AFE_PWR2_A, 0x77777777);

		rtw_write32(rtwdev, REG_RX_WAIT_CCA_TX_CCK_RFON_A, 0x19791979);

		/* hardware 3-wire off */
		rtw_write32_mask(rtwdev, REG_3WIRE_SWA, 0xf, 0x4);

		/* LOK setting */

		/* 1. DAC/ADC sampling rate (160 MHz) */
		rtw_write32_mask(rtwdev, REG_CK_MONHA, GENMASK(26, 24), 0x7);

		/* 2. LoK RF setting (at BW = 20M) */
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x80002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, 0x00c00, 0x3);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK,
			     0x20000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK,
			     0x0003f);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK,
			     0xf3fc3);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_TXA_PREPAD, RFREG_MASK,
			     0x931d5);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RXBB2, RFREG_MASK, 0x8a001);
		rtw_write32(rtwdev, REG_DAC_RSTB, 0x00008000);
		rtw_write32_mask(rtwdev, REG_TXAGCIDX, BIT(0), 0x1);
		/* TX (X,Y) */
		rtw_write32(rtwdev, REG_IQK_COM00, 0x29002000);
		/* RX (X,Y) */
		rtw_write32(rtwdev, REG_IQK_COM32, 0xa9002000);
		/* [0]:AGC_en, [15]:idac_K_Mask */
		rtw_write32(rtwdev, REG_IQK_COM96, 0x00462910);

		/* [31] = 1 --> Page C1 */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);

		if (efuse->ext_pa_5g)
			rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE,
				    0x821403f7);
		else
			rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE,
				    0x821403f4);

		if (hal->current_band_type == RTW_BAND_5G)
			rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x68163e96);
		else
			rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x28163e96);

		/* TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
		rtw_write32(rtwdev, REG_OFDM0_XA_TX_IQ_IMBALANCE, 0x18008c10);
		/* RX_Tone_idx[9:0], RxK_Mask[29] */
		rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x38008c10);
		rtw_write32(rtwdev, REG_RFECTL_A, 0x00100000);
		rtw_write32(rtwdev, REG_IQK_COM64, 0xfa000000);
		rtw_write32(rtwdev, REG_IQK_COM64, 0xf8000000);

		mdelay(10);
		rtw_write32(rtwdev, REG_RFECTL_A, 0x00000000);

		/* [31] = 0 --> Page C */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_TXMOD, 0x7fe00,
			     rtw_read_rf(rtwdev, RF_PATH_A, RF_DTXLOK, 0xffc00));

		if (hal->current_band_width == RTW_CHANNEL_WIDTH_40)
			rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH,
				     RF18_BW_MASK, 0x1);
		else if (hal->current_band_width == RTW_CHANNEL_WIDTH_80)
			rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH,
				     RF18_BW_MASK, 0x0);

		/* [31] = 1 --> Page C1 */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);

		/* 3. TX RF setting */
		/* [31] = 0 --> Page C */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x80000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK,
			     0x20000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK,
			     0x0003f);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK,
			     0xf3fc3);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_TXA_PREPAD, RFREG_MASK, 0x931d5);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RXBB2, RFREG_MASK, 0x8a001);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x00000);
		rtw_write32(rtwdev, REG_DAC_RSTB, 0x00008000);
		rtw_write32_mask(rtwdev, REG_TXAGCIDX, BIT(0), 0x1);
		/* TX (X,Y) */
		rtw_write32(rtwdev, REG_IQK_COM00, 0x29002000);
		/* RX (X,Y) */
		rtw_write32(rtwdev, REG_IQK_COM32, 0xa9002000);
		/* [0]:AGC_en, [15]:idac_K_Mask */
		rtw_write32(rtwdev, REG_IQK_COM96, 0x0046a910);

		/* [31] = 1 --> Page C1 */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);

		if (efuse->ext_pa_5g)
			rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE,
				    0x821403f7);
		else
			rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE,
				    0x821403e3);

		if (hal->current_band_type == RTW_BAND_5G)
			rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x40163e96);
		else
			rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x00163e96);

		if (vdf_enable)
			rtw8821a_iqk_tx_vdf_true(rtwdev, cal, &tx0iqkok,
						 tx_x0, tx_y0);
		else
			rtw8821a_iqk_tx_vdf_false(rtwdev, cal, &tx0iqkok,
						  tx_x0, tx_y0);

		if (!tx0iqkok)
			break; /* TXK fail, Don't do RXK */

		/* ====== RX IQK ====== */
		/* [31] = 0 --> Page C */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);
		/* 1. RX RF setting */
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x80000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK,
			     0x30000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK,
			     0x0002f);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK,
			     0xfffbb);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RXBB2, RFREG_MASK, 0x88001);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_TXA_PREPAD, RFREG_MASK, 0x931d8);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x00000);

		rtw_write32_mask(rtwdev, REG_IQK_COM00, 0x03FF8000,
				 (tx_x0[cal] >> 21) & 0x000007ff);
		rtw_write32_mask(rtwdev, REG_IQK_COM00, 0x000007FF,
				 (tx_y0[cal] >> 21) & 0x000007ff);
		rtw_write32_mask(rtwdev, REG_IQK_COM00, BIT(31), 0x1);
		rtw_write32_mask(rtwdev, REG_IQK_COM00, BIT(31), 0x0);
		rtw_write32(rtwdev, REG_DAC_RSTB, 0x00008000);
		rtw_write32(rtwdev, REG_IQK_COM96, 0x0046a911);

		/* [31] = 1 --> Page C1 */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);

		/* TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
		rtw_write32(rtwdev, REG_OFDM0_XA_TX_IQ_IMBALANCE, 0x38008c10);
		/* RX_Tone_idx[9:0], RxK_Mask[29] */
		rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x18008c10);
		rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE, 0x02140119);

		if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_PCIE)
			rx_iqk_loop = 2; /* for 2% fail; */
		else
			rx_iqk_loop = 1;

		for (i = 0; i < rx_iqk_loop; i++) {
			if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_PCIE && i == 0)
				rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x28161100); /* Good */
			else
				rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x28160d00);

			rtw8821a_iqk_rx(rtwdev, cal, &rx0iqkok,
					rx_x0[i], rx_y0[i]);
		}

		if (tx0iqkok)
			tx_average++;
		if (rx0iqkok)
			rx_average++;
	}

	/* FillIQK Result */

	if (tx_average == 0)
		return;

	for (i = 0; i < tx_average; i++)
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"tx_x0[%d] = %x ;; tx_y0[%d] = %x\n",
			i, (tx_x0[i] >> 21) & 0x000007ff,
			i, (tx_y0[i] >> 21) & 0x000007ff);

	if (rtw88xxa_iqk_finish(tx_average, 3, tx_x0, tx_y0,
				&tx_x, &tx_y, true, true))
		rtw8821a_iqk_tx_fill(rtwdev, tx_x, tx_y);
	else
		rtw8821a_iqk_tx_fill(rtwdev, 0x200, 0x0);

	if (rx_average == 0)
		return;

	for (i = 0; i < rx_average; i++) {
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"rx_x0[0][%d] = %x ;; rx_y0[0][%d] = %x\n",
			i, (rx_x0[0][i] >> 21) & 0x000007ff,
			i, (rx_y0[0][i] >> 21) & 0x000007ff);

		if (rx_iqk_loop == 2)
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"rx_x0[1][%d] = %x ;; rx_y0[1][%d] = %x\n",
				i, (rx_x0[1][i] >> 21) & 0x000007ff,
				i, (rx_y0[1][i] >> 21) & 0x000007ff);
	}

	rx_finish1 = rtw88xxa_iqk_finish(rx_average, 4, rx_x0[0], rx_y0[0],
					 &rx_x_temp, &rx_y_temp, true, true);

	if (rx_finish1) {
		rx_x = rx_x_temp;
		rx_y = rx_y_temp;
	}

	if (rx_iqk_loop == 2) {
		rx_finish2 = rtw88xxa_iqk_finish(rx_average, 4,
						 rx_x0[1], rx_y0[1],
						 &rx_x, &rx_y, true, true);

		if (rx_finish1 && rx_finish2) {
			rx_x = (rx_x + rx_x_temp) / 2;
			rx_y = (rx_y + rx_y_temp) / 2;
		}
	}

	if (rx_finish1 || rx_finish2)
		rtw8821a_iqk_rx_fill(rtwdev, rx_x, rx_y);
	else
		rtw8821a_iqk_rx_fill(rtwdev, 0x200, 0x0);
}

static void rtw8821a_do_iqk(struct rtw_dev *rtwdev)
{
	static const u32 backup_macbb_reg[MACBB_REG_NUM_8821A] = {
		0x520, 0x550, 0x808, 0xa04, 0x90c, 0xc00, 0x838, 0x82c
	};
	static const u32 backup_afe_reg[AFE_REG_NUM_8821A] = {
		0xc5c, 0xc60, 0xc64, 0xc68
	};
	static const u32 backup_rf_reg[RF_REG_NUM_8821A] = {
		0x65, 0x8f, 0x0
	};
	u32 macbb_backup[MACBB_REG_NUM_8821A];
	u32 afe_backup[AFE_REG_NUM_8821A];
	u32 rfa_backup[RF_REG_NUM_8821A];

	rtw88xxa_iqk_backup_mac_bb(rtwdev, macbb_backup,
				   backup_macbb_reg, MACBB_REG_NUM_8821A);
	rtw88xxa_iqk_backup_afe(rtwdev, afe_backup,
				backup_afe_reg, AFE_REG_NUM_8821A);
	rtw8821a_iqk_backup_rf(rtwdev, rfa_backup,
			       backup_rf_reg, RF_REG_NUM_8821A);

	rtw88xxa_iqk_configure_mac(rtwdev);

	rtw8821a_iqk(rtwdev);

	rtw8821a_iqk_restore_rf(rtwdev, backup_rf_reg,
				rfa_backup, RF_REG_NUM_8821A);
	rtw8821a_iqk_restore_afe(rtwdev, afe_backup,
				 backup_afe_reg, AFE_REG_NUM_8821A);
	rtw88xxa_iqk_restore_mac_bb(rtwdev, macbb_backup,
				    backup_macbb_reg, MACBB_REG_NUM_8821A);
}

static void rtw8821a_phy_calibration(struct rtw_dev *rtwdev)
{
	rtw8821a_do_iqk(rtwdev);
}

static void rtw8821a_pwr_track(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	if (!dm_info->pwr_trk_triggered) {
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER,
			     GENMASK(17, 16), 0x03);
		dm_info->pwr_trk_triggered = true;
		return;
	}

	rtw88xxa_phy_pwrtrack(rtwdev, NULL, rtw8821a_do_iqk);
	dm_info->pwr_trk_triggered = false;
}

static void rtw8821a_led_set(struct led_classdev *led,
			     enum led_brightness brightness)
{
	struct rtw_dev *rtwdev = container_of(led, struct rtw_dev, led_cdev);
	u32 gpio8_cfg;
	u8 ledcfg;

	if (brightness == LED_OFF) {
		gpio8_cfg = rtw_read32(rtwdev, REG_GPIO_PIN_CTRL_2);
		gpio8_cfg &= ~BIT(24);
		gpio8_cfg |= BIT(16) | BIT(8);
		rtw_write32(rtwdev, REG_GPIO_PIN_CTRL_2, gpio8_cfg);
	} else {
		ledcfg = rtw_read8(rtwdev, REG_LED_CFG + 2);
		gpio8_cfg = rtw_read32(rtwdev, REG_GPIO_PIN_CTRL_2);

		ledcfg &= BIT(7) | BIT(6);
		rtw_write8(rtwdev, REG_LED_CFG + 2, ledcfg);

		gpio8_cfg &= ~(BIT(24) | BIT(8));
		gpio8_cfg |= BIT(16);
		rtw_write32(rtwdev, REG_GPIO_PIN_CTRL_2, gpio8_cfg);
	}
}

static void rtw8821a_fill_txdesc_checksum(struct rtw_dev *rtwdev,
					  struct rtw_tx_pkt_info *pkt_info,
					  u8 *txdesc)
{
	fill_txdesc_checksum_common(txdesc, 16);
}

static void rtw8821a_coex_cfg_init(struct rtw_dev *rtwdev)
{
	u8 val8;

	/* BT report packet sample rate */
	rtw_write8_mask(rtwdev, REG_BT_TDMA_TIME, BIT_MASK_SAMPLE_RATE, 0x5);

	val8 = BIT_STATIS_BT_EN;
	if (rtwdev->efuse.share_ant)
		val8 |= BIT_R_GRANTALL_WLMASK;
	rtw_write8(rtwdev, REG_BT_COEX_ENH_INTR_CTRL, val8);

	/* enable BT counter statistics */
	rtw_write8(rtwdev, REG_BT_STAT_CTRL, 0x3);

	/* enable PTA */
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_BT_PTA_EN);
}

static void rtw8821a_coex_cfg_ant_switch(struct rtw_dev *rtwdev, u8 ctrl_type,
					 u8 pos_type)
{
	bool share_ant = rtwdev->efuse.share_ant;
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	u32 phase = coex_dm->cur_ant_pos_type;

	if (!rtwdev->efuse.btcoex)
		return;

	switch (phase) {
	case COEX_SET_ANT_POWERON:
	case COEX_SET_ANT_INIT:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_set(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		rtw_write8_set(rtwdev, REG_GNT_BT, BIT_PTA_SW_CTL);

		rtw_write8(rtwdev, REG_RFE_CTRL8,
			   share_ant ? PTA_CTRL_PIN : DPDT_CTRL_PIN);
		rtw_write32_mask(rtwdev, REG_RFE_CTRL8, 0x30000000, 0x1);
		break;
	case COEX_SET_ANT_WONLY:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_set(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		rtw_write8_clr(rtwdev, REG_GNT_BT, BIT_PTA_SW_CTL);

		rtw_write8(rtwdev, REG_RFE_CTRL8, DPDT_CTRL_PIN);
		rtw_write32_mask(rtwdev, REG_RFE_CTRL8, 0x30000000, 0x1);
		break;
	case COEX_SET_ANT_2G:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_set(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		rtw_write8_clr(rtwdev, REG_GNT_BT, BIT_PTA_SW_CTL);

		rtw_write8(rtwdev, REG_RFE_CTRL8,
			   share_ant ? PTA_CTRL_PIN : DPDT_CTRL_PIN);
		rtw_write32_mask(rtwdev, REG_RFE_CTRL8, 0x30000000, 0x1);
		break;
	case COEX_SET_ANT_5G:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_set(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		rtw_write8_set(rtwdev, REG_GNT_BT, BIT_PTA_SW_CTL);

		rtw_write8(rtwdev, REG_RFE_CTRL8, DPDT_CTRL_PIN);
		rtw_write32_mask(rtwdev, REG_RFE_CTRL8, 0x30000000,
				 share_ant ? 0x2 : 0x1);
		break;
	case COEX_SET_ANT_WOFF:
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_SEL_EN);
		rtw_write32_clr(rtwdev, REG_LED_CFG, BIT_DPDT_WL_SEL);
		rtw_write8_set(rtwdev, REG_GNT_BT, BIT_PTA_SW_CTL);

		rtw_write8(rtwdev, REG_RFE_CTRL8, DPDT_CTRL_PIN);
		rtw_write32_mask(rtwdev, REG_RFE_CTRL8, 0x30000000,
				 share_ant ? 0x2 : 0x1);
		break;
	default:
		rtw_warn(rtwdev, "%s: not handling phase %d\n",
			 __func__, phase);
		break;
	}
}

static void rtw8821a_coex_cfg_gnt_fix(struct rtw_dev *rtwdev)
{
}

static void rtw8821a_coex_cfg_gnt_debug(struct rtw_dev *rtwdev)
{
}

static void rtw8821a_coex_cfg_rfe_type(struct rtw_dev *rtwdev)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_rfe *coex_rfe = &coex->rfe;

	coex_rfe->ant_switch_exist = true;
}

static void rtw8821a_coex_cfg_wl_tx_power(struct rtw_dev *rtwdev, u8 wl_pwr)
{
	struct rtw_coex *coex = &rtwdev->coex;
	struct rtw_coex_dm *coex_dm = &coex->dm;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	bool share_ant = efuse->share_ant;

	if (share_ant)
		return;

	if (wl_pwr == coex_dm->cur_wl_pwr_lvl)
		return;

	coex_dm->cur_wl_pwr_lvl = wl_pwr;
}

static void rtw8821a_coex_cfg_wl_rx_gain(struct rtw_dev *rtwdev, bool low_gain)
{
}

static const struct rtw_chip_ops rtw8821a_ops = {
	.power_on		= rtw88xxa_power_on,
	.power_off		= rtw8821a_power_off,
	.phy_set_param		= NULL,
	.read_efuse		= rtw88xxa_read_efuse,
	.query_phy_status	= rtw8821a_query_phy_status,
	.set_channel		= rtw88xxa_set_channel,
	.mac_init		= NULL,
	.read_rf		= rtw88xxa_phy_read_rf,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_antenna		= NULL,
	.set_tx_power_index	= rtw88xxa_set_tx_power_index,
	.cfg_ldo25		= rtw8821a_cfg_ldo25,
	.efuse_grant		= rtw88xxa_efuse_grant,
	.false_alarm_statistics	= rtw88xxa_false_alarm_statistics,
	.phy_calibration	= rtw8821a_phy_calibration,
	.cck_pd_set		= rtw88xxa_phy_cck_pd_set,
	.pwr_track		= rtw8821a_pwr_track,
	.config_bfee		= NULL,
	.set_gid_table		= NULL,
	.cfg_csi_rate		= NULL,
	.led_set		= rtw8821a_led_set,
	.fill_txdesc_checksum	= rtw8821a_fill_txdesc_checksum,
	.coex_set_init		= rtw8821a_coex_cfg_init,
	.coex_set_ant_switch	= rtw8821a_coex_cfg_ant_switch,
	.coex_set_gnt_fix	= rtw8821a_coex_cfg_gnt_fix,
	.coex_set_gnt_debug	= rtw8821a_coex_cfg_gnt_debug,
	.coex_set_rfe_type	= rtw8821a_coex_cfg_rfe_type,
	.coex_set_wl_tx_power	= rtw8821a_coex_cfg_wl_tx_power,
	.coex_set_wl_rx_gain	= rtw8821a_coex_cfg_wl_rx_gain,
};

static const struct rtw_page_table page_table_8821a[] = {
	/* hq_num, nq_num, lq_num, exq_num, gapq_num */
	{0, 0, 0, 0, 0},	/* SDIO */
	{0, 0, 0, 0, 0},	/* PCI */
	{8, 0, 0, 0, 1},	/* 2 bulk out endpoints */
	{8, 0, 8, 0, 1},	/* 3 bulk out endpoints */
	{8, 0, 8, 4, 1},	/* 4 bulk out endpoints */
};

static const struct rtw_rqpn rqpn_table_8821a[] = {
	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},

	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},

	{RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH,
	 RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},

	{RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_HIGH, RTW_DMA_MAPPING_HIGH},

	{RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL,
	 RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_LOW,
	 RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH},
};

static const struct rtw_prioq_addrs prioq_addrs_8821a = {
	.prio[RTW_DMA_MAPPING_EXTRA] = {
		.rsvd = REG_RQPN_NPQ + 2, .avail = REG_RQPN_NPQ + 3,
	},
	.prio[RTW_DMA_MAPPING_LOW] = {
		.rsvd = REG_RQPN + 1, .avail = REG_FIFOPAGE_CTRL_2 + 1,
	},
	.prio[RTW_DMA_MAPPING_NORMAL] = {
		.rsvd = REG_RQPN_NPQ, .avail = REG_RQPN_NPQ + 1,
	},
	.prio[RTW_DMA_MAPPING_HIGH] = {
		.rsvd = REG_RQPN, .avail = REG_FIFOPAGE_CTRL_2,
	},
	.wsize = false,
};

static const struct rtw_hw_reg rtw8821a_dig[] = {
	[0] = { .addr = REG_RXIGI_A, .mask = 0x7f },
};

static const struct rtw_rfe_def rtw8821a_rfe_defs[] = {
	[0] = { .phy_pg_tbl	= &rtw8821a_bb_pg_tbl,
		.txpwr_lmt_tbl	= &rtw8821a_txpwr_lmt_tbl,
		.pwr_track_tbl	= &rtw8821a_rtw_pwr_track_tbl, },
};

/* TODO */
/* rssi in percentage % (dbm = % - 100) */
static const u8 wl_rssi_step_8821a[] = {101, 45, 101, 40};
static const u8 bt_rssi_step_8821a[] = {101, 101, 101, 101};

/* table_sant_8821a, table_nsant_8821a, tdma_sant_8821a, and tdma_nsant_8821a
 * are copied from rtw8821c.c because the 8821au driver's tables are not
 * compatible with the coex code in rtw88.
 *
 * tdma case 112 (A2DP) byte 0 had to be modified from 0x61 to 0x51,
 * otherwise the firmware gets confused after pausing the music:
 * rtw_8821au 1-2:1.2: [BTCoex], Bt_info[1], len=7, data=[81 00 0a 01 00 00]
 * - 81 means PAN (personal area network) when it should be 4x (A2DP)
 * The music is not smooth with the PAN algorithm.
 */

/* Shared-Antenna Coex Table */
static const struct coex_table_para table_sant_8821a[] = {
	{0x55555555, 0x55555555}, /* case-0 */
	{0x55555555, 0x55555555},
	{0x66555555, 0x66555555},
	{0xaaaaaaaa, 0xaaaaaaaa},
	{0x5a5a5a5a, 0x5a5a5a5a},
	{0xfafafafa, 0xfafafafa}, /* case-5 */
	{0x6a5a5555, 0xaaaaaaaa},
	{0x6a5a56aa, 0x6a5a56aa},
	{0x6a5a5a5a, 0x6a5a5a5a},
	{0x66555555, 0x5a5a5a5a},
	{0x66555555, 0x6a5a5a5a}, /* case-10 */
	{0x66555555, 0xaaaaaaaa},
	{0x66555555, 0x6a5a5aaa},
	{0x66555555, 0x6aaa6aaa},
	{0x66555555, 0x6a5a5aaa},
	{0x66555555, 0xaaaaaaaa}, /* case-15 */
	{0xffff55ff, 0xfafafafa},
	{0xffff55ff, 0x6afa5afa},
	{0xaaffffaa, 0xfafafafa},
	{0xaa5555aa, 0x5a5a5a5a},
	{0xaa5555aa, 0x6a5a5a5a}, /* case-20 */
	{0xaa5555aa, 0xaaaaaaaa},
	{0xffffffff, 0x55555555},
	{0xffffffff, 0x5a5a5a5a},
	{0xffffffff, 0x5a5a5a5a},
	{0xffffffff, 0x5a5a5aaa}, /* case-25 */
	{0x55555555, 0x5a5a5a5a},
	{0x55555555, 0xaaaaaaaa},
	{0x66555555, 0x6a5a6a5a},
	{0x66556655, 0x66556655},
	{0x66556aaa, 0x6a5a6aaa}, /* case-30 */
	{0xffffffff, 0x5aaa5aaa},
	{0x56555555, 0x5a5a5aaa}
};

/* Non-Shared-Antenna Coex Table */
static const struct coex_table_para table_nsant_8821a[] = {
	{0xffffffff, 0xffffffff}, /* case-100 */
	{0xffff55ff, 0xfafafafa},
	{0x66555555, 0x66555555},
	{0xaaaaaaaa, 0xaaaaaaaa},
	{0x5a5a5a5a, 0x5a5a5a5a},
	{0xffffffff, 0xffffffff}, /* case-105 */
	{0x5afa5afa, 0x5afa5afa},
	{0x55555555, 0xfafafafa},
	{0x66555555, 0xfafafafa},
	{0x66555555, 0x5a5a5a5a},
	{0x66555555, 0x6a5a5a5a}, /* case-110 */
	{0x66555555, 0xaaaaaaaa},
	{0xffff55ff, 0xfafafafa},
	{0xffff55ff, 0x5afa5afa},
	{0xffff55ff, 0xaaaaaaaa},
	{0xffff55ff, 0xffff55ff}, /* case-115 */
	{0xaaffffaa, 0x5afa5afa},
	{0xaaffffaa, 0xaaaaaaaa},
	{0xffffffff, 0xfafafafa},
	{0xffff55ff, 0xfafafafa},
	{0xffffffff, 0xaaaaaaaa}, /* case-120 */
	{0xffff55ff, 0x5afa5afa},
	{0xffff55ff, 0x5afa5afa},
	{0x55ff55ff, 0x55ff55ff}
};

/* Shared-Antenna TDMA */
static const struct coex_tdma_para tdma_sant_8821a[] = {
	{ {0x00, 0x00, 0x00, 0x00, 0x00} }, /* case-0 */
	{ {0x61, 0x45, 0x03, 0x11, 0x11} }, /* case-1 */
	{ {0x61, 0x3a, 0x03, 0x11, 0x11} },
	{ {0x61, 0x35, 0x03, 0x11, 0x11} },
	{ {0x61, 0x20, 0x03, 0x11, 0x11} },
	{ {0x61, 0x3a, 0x03, 0x11, 0x11} }, /* case-5 */
	{ {0x61, 0x45, 0x03, 0x11, 0x10} },
	{ {0x61, 0x35, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x20, 0x03, 0x11, 0x10} },
	{ {0x61, 0x10, 0x03, 0x11, 0x10} }, /* case-10 */
	{ {0x61, 0x08, 0x03, 0x11, 0x15} },
	{ {0x61, 0x08, 0x03, 0x10, 0x14} },
	{ {0x51, 0x08, 0x03, 0x10, 0x54} },
	{ {0x51, 0x08, 0x03, 0x10, 0x55} },
	{ {0x51, 0x08, 0x07, 0x10, 0x54} }, /* case-15 */
	{ {0x51, 0x45, 0x03, 0x10, 0x50} },
	{ {0x51, 0x3a, 0x03, 0x11, 0x50} },
	{ {0x51, 0x30, 0x03, 0x10, 0x50} },
	{ {0x51, 0x21, 0x03, 0x10, 0x50} },
	{ {0x51, 0x10, 0x03, 0x10, 0x50} }, /* case-20 */
	{ {0x51, 0x4a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x08, 0x03, 0x30, 0x54} },
	{ {0x55, 0x08, 0x03, 0x10, 0x54} },
	{ {0x65, 0x10, 0x03, 0x11, 0x10} },
	{ {0x51, 0x10, 0x03, 0x10, 0x51} }, /* case-25 */
	{ {0x51, 0x21, 0x03, 0x10, 0x50} },
	{ {0x61, 0x08, 0x03, 0x11, 0x11} }
};

/* Non-Shared-Antenna TDMA */
static const struct coex_tdma_para tdma_nsant_8821a[] = {
	{ {0x00, 0x00, 0x00, 0x40, 0x00} }, /* case-100 */
	{ {0x61, 0x45, 0x03, 0x11, 0x11} },
	{ {0x61, 0x25, 0x03, 0x11, 0x11} },
	{ {0x61, 0x35, 0x03, 0x11, 0x11} },
	{ {0x61, 0x20, 0x03, 0x11, 0x11} },
	{ {0x61, 0x10, 0x03, 0x11, 0x11} }, /* case-105 */
	{ {0x61, 0x45, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x30, 0x03, 0x11, 0x10} },
	{ {0x61, 0x20, 0x03, 0x11, 0x10} },
	{ {0x61, 0x10, 0x03, 0x11, 0x10} }, /* case-110 */
	{ {0x61, 0x10, 0x03, 0x11, 0x11} },
	{ {0x51, 0x08, 0x03, 0x10, 0x14} }, /* a2dp high rssi */
	{ {0x51, 0x08, 0x03, 0x10, 0x54} }, /* a2dp not high rssi */
	{ {0x51, 0x08, 0x03, 0x10, 0x55} },
	{ {0x51, 0x08, 0x07, 0x10, 0x54} }, /* case-115 */
	{ {0x51, 0x45, 0x03, 0x10, 0x50} },
	{ {0x51, 0x3a, 0x03, 0x10, 0x50} },
	{ {0x51, 0x30, 0x03, 0x10, 0x50} },
	{ {0x51, 0x21, 0x03, 0x10, 0x50} },
	{ {0x51, 0x21, 0x03, 0x10, 0x50} }, /* case-120 */
	{ {0x51, 0x10, 0x03, 0x10, 0x50} }
};

/* TODO */
static const struct coex_rf_para rf_para_tx_8821a[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 20, false, 7}, /* for WL-CPT */
	{8, 17, true, 4},
	{7, 18, true, 4},
	{6, 19, true, 4},
	{5, 20, true, 4}
};

static const struct coex_rf_para rf_para_rx_8821a[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 20, false, 7}, /* for WL-CPT */
	{3, 24, true, 5},
	{2, 26, true, 5},
	{1, 27, true, 5},
	{0, 28, true, 5}
};

static_assert(ARRAY_SIZE(rf_para_tx_8821a) == ARRAY_SIZE(rf_para_rx_8821a));

static const struct coex_5g_afh_map afh_5g_8821a[] = { {0, 0, 0} };

static const struct rtw_reg_domain coex_info_hw_regs_8821a[] = {
	{0xCB0, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0xCB4, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0xCBA, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x430, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x434, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x42a, MASKLWORD, RTW_REG_DOMAIN_MAC16},
	{0x426, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x45e, BIT(3), RTW_REG_DOMAIN_MAC8},
	{0x454, MASKLWORD, RTW_REG_DOMAIN_MAC16},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x4c, BIT(24) | BIT(23), RTW_REG_DOMAIN_MAC32},
	{0x64, BIT(0), RTW_REG_DOMAIN_MAC8},
	{0x4c6, BIT(4), RTW_REG_DOMAIN_MAC8},
	{0x40, BIT(5), RTW_REG_DOMAIN_MAC8},
	{0x1, RFREG_MASK, RTW_REG_DOMAIN_RF_A},
	{0, 0, RTW_REG_DOMAIN_NL},
	{0x550, MASKDWORD, RTW_REG_DOMAIN_MAC32},
	{0x522, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x953, BIT(1), RTW_REG_DOMAIN_MAC8},
	{0xc50,  MASKBYTE0, RTW_REG_DOMAIN_MAC8},
	{0x60A, MASKBYTE0, RTW_REG_DOMAIN_MAC8},
};

const struct rtw_chip_info rtw8821a_hw_spec = {
	.ops = &rtw8821a_ops,
	.id = RTW_CHIP_TYPE_8821A,
	.fw_name = "rtw88/rtw8821a_fw.bin",
	.wlan_cpu = RTW_WCPU_11N,
	.tx_pkt_desc_sz = 40,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 512,
	.log_efuse_size = 512,
	.ptct_efuse_size = 0,
	.txff_size = 65536,
	.rxff_size = 16128,
	.rsvd_drv_pg_num = 8,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.max_power_index = 0x3f,
	.csi_buf_pg_num = 0,
	.band = RTW_BAND_2G | RTW_BAND_5G,
	.page_size = 256,
	.dig_min = 0x20,
	.ht_supported = true,
	.vht_supported = true,
	.lps_deep_mode_supported = 0,
	.sys_func_en = 0xFD,
	.pwr_on_seq = card_enable_flow_8821a,
	.pwr_off_seq = card_disable_flow_8821a,
	.page_table = page_table_8821a,
	.rqpn_table = rqpn_table_8821a,
	.prioq_addrs = &prioq_addrs_8821a,
	.intf_table = NULL,
	.dig = rtw8821a_dig,
	.rf_sipi_addr = {REG_LSSI_WRITE_A, REG_LSSI_WRITE_B},
	.ltecoex_addr = NULL,
	.mac_tbl = &rtw8821a_mac_tbl,
	.agc_tbl = &rtw8821a_agc_tbl,
	.bb_tbl = &rtw8821a_bb_tbl,
	.rf_tbl = {&rtw8821a_rf_a_tbl},
	.rfe_defs = rtw8821a_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8821a_rfe_defs),
	.rx_ldpc = false,
	.hw_feature_report = false,
	.c2h_ra_report_size = 4,
	.old_datarate_fb_limit = true,
	.usb_tx_agg_desc_num = 6,
	.iqk_threshold = 8,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.max_scan_ie_len = IEEE80211_MAX_DATA_LEN,

	.coex_para_ver = 20190509, /* glcoex_ver_date_8821a_1ant */
	.bt_desired_ver = 0x62, /* But for 2 ant it's 0x5c */
	.scbd_support = false,
	.new_scbd10_def = false,
	.ble_hid_profile_support = false,
	.wl_mimo_ps_support = false,
	.pstdma_type = COEX_PSTDMA_FORCE_LPSOFF,
	.bt_rssi_type = COEX_BTRSSI_RATIO,
	.ant_isolation = 10,
	.rssi_tolerance = 2,
	.wl_rssi_step = wl_rssi_step_8821a,
	.bt_rssi_step = bt_rssi_step_8821a,
	.table_sant_num = ARRAY_SIZE(table_sant_8821a),
	.table_sant = table_sant_8821a,
	.table_nsant_num = ARRAY_SIZE(table_nsant_8821a),
	.table_nsant = table_nsant_8821a,
	.tdma_sant_num = ARRAY_SIZE(tdma_sant_8821a),
	.tdma_sant = tdma_sant_8821a,
	.tdma_nsant_num = ARRAY_SIZE(tdma_nsant_8821a),
	.tdma_nsant = tdma_nsant_8821a,
	.wl_rf_para_num = ARRAY_SIZE(rf_para_tx_8821a),
	.wl_rf_para_tx = rf_para_tx_8821a,
	.wl_rf_para_rx = rf_para_rx_8821a,
	.bt_afh_span_bw20 = 0x20,
	.bt_afh_span_bw40 = 0x30,
	.afh_5g_num = ARRAY_SIZE(afh_5g_8821a),
	.afh_5g = afh_5g_8821a,

	.coex_info_hw_regs_num = ARRAY_SIZE(coex_info_hw_regs_8821a),
	.coex_info_hw_regs = coex_info_hw_regs_8821a,
};
EXPORT_SYMBOL(rtw8821a_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8821a_fw.bin");

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8821a/8811a driver");
MODULE_LICENSE("Dual BSD/GPL");
