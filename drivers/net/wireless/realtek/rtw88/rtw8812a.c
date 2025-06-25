// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2024  Realtek Corporation
 */

#include "main.h"
#include "coex.h"
#include "phy.h"
#include "reg.h"
#include "rtw88xxa.h"
#include "rtw8812a.h"
#include "rtw8812a_table.h"
#include "tx.h"

static void rtw8812a_power_off(struct rtw_dev *rtwdev)
{
	rtw88xxa_power_off(rtwdev, enter_lps_flow_8812a);
}

static s8 rtw8812a_cck_rx_pwr(u8 lna_idx, u8 vga_idx)
{
	s8 rx_pwr_all = 0;

	switch (lna_idx) {
	case 7:
		if (vga_idx <= 27)
			rx_pwr_all = -94 + 2 * (27 - vga_idx);
		else
			rx_pwr_all = -94;
		break;
	case 6:
		rx_pwr_all = -42 + 2 * (2 - vga_idx);
		break;
	case 5:
		rx_pwr_all = -36 + 2 * (7 - vga_idx);
		break;
	case 4:
		rx_pwr_all = -30 + 2 * (7 - vga_idx);
		break;
	case 3:
		rx_pwr_all = -18 + 2 * (7 - vga_idx);
		break;
	case 2:
		rx_pwr_all = 2 * (5 - vga_idx);
		break;
	case 1:
		rx_pwr_all = 14 - 2 * vga_idx;
		break;
	case 0:
		rx_pwr_all = 20 - 2 * vga_idx;
		break;
	default:
		break;
	}

	return rx_pwr_all;
}

static void rtw8812a_query_phy_status(struct rtw_dev *rtwdev, u8 *phy_status,
				      struct rtw_rx_pkt_stat *pkt_stat)
{
	rtw88xxa_query_phy_status(rtwdev, phy_status, pkt_stat,
				  rtw8812a_cck_rx_pwr);

	if (pkt_stat->rate >= DESC_RATE6M)
		return;

	if (rtwdev->hal.cck_high_power)
		return;

	if (pkt_stat->rssi >= 80)
		pkt_stat->rssi = ((pkt_stat->rssi - 80) << 1) +
				 ((pkt_stat->rssi - 80) >> 1) + 80;
	else if (pkt_stat->rssi <= 78 && pkt_stat->rssi >= 20)
		pkt_stat->rssi += 3;
}

static void rtw8812a_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
}

static void rtw8812a_do_lck(struct rtw_dev *rtwdev)
{
	u32 cont_tx, lc_cal, i;

	cont_tx = rtw_read32_mask(rtwdev, REG_SINGLE_TONE_CONT_TX, 0x70000);

	lc_cal = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);

	if (!cont_tx)
		rtw_write8(rtwdev, REG_TXPAUSE, 0xff);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_LCK, BIT(14), 1);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, 0x08000, 1);

	mdelay(150);

	for (i = 0; i < 5; i++) {
		if (rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, 0x08000) != 1)
			break;

		mdelay(10);
	}

	if (i == 5)
		rtw_dbg(rtwdev, RTW_DBG_RFK, "LCK timed out\n");

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, lc_cal);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_LCK, BIT(14), 0);

	if (!cont_tx)
		rtw_write8(rtwdev, REG_TXPAUSE, 0);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, lc_cal);
}

static void rtw8812a_iqk_backup_rf(struct rtw_dev *rtwdev, u32 *rfa_backup,
				   u32 *rfb_backup, const u32 *backup_rf_reg,
				   u32 rf_num)
{
	u32 i;

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	/* Save RF Parameters */
	for (i = 0; i < rf_num; i++) {
		rfa_backup[i] = rtw_read_rf(rtwdev, RF_PATH_A,
					    backup_rf_reg[i], MASKDWORD);
		rfb_backup[i] = rtw_read_rf(rtwdev, RF_PATH_B,
					    backup_rf_reg[i], MASKDWORD);
	}
}

static void rtw8812a_iqk_restore_rf(struct rtw_dev *rtwdev,
				    enum rtw_rf_path path,
				    const u32 *backup_rf_reg,
				    u32 *RF_backup, u32 rf_reg_num)
{
	u32 i;

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	for (i = 0; i < rf_reg_num; i++)
		rtw_write_rf(rtwdev, path, backup_rf_reg[i],
			     RFREG_MASK, RF_backup[i]);

	rtw_write_rf(rtwdev, path, RF_LUTWE, RFREG_MASK, 0);
}

static void rtw8812a_iqk_restore_afe(struct rtw_dev *rtwdev, u32 *afe_backup,
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
	rtw_write32_mask(rtwdev, REG_LSSI_WRITE_A, BIT(7), 1);
	rtw_write32_mask(rtwdev, REG_IQK_DPD_CFG, BIT(18), 1);
	rtw_write32_mask(rtwdev, REG_IQK_DPD_CFG, BIT(29), 1);
	rtw_write32_mask(rtwdev, REG_CFG_PMPD, BIT(29), 1);

	rtw_write32(rtwdev, REG_TXTONEB, 0x0);
	rtw_write32(rtwdev, REG_RXTONEB, 0x0);
	rtw_write32(rtwdev, REG_TXPITMB, 0x0);
	rtw_write32(rtwdev, REG_RXPITMB, 0x3c000000);
	rtw_write32_mask(rtwdev, REG_LSSI_WRITE_B, BIT(7), 1);
	rtw_write32_mask(rtwdev, REG_BPBDB, BIT(18), 1);
	rtw_write32_mask(rtwdev, REG_BPBDB, BIT(29), 1);
	rtw_write32_mask(rtwdev, REG_PHYTXONB, BIT(29), 1);
}

static void rtw8812a_iqk_rx_fill(struct rtw_dev *rtwdev, enum rtw_rf_path path,
				 unsigned int rx_x, unsigned int rx_y)
{
	switch (path) {
	case RF_PATH_A:
		/* [31] = 0 --> Page C */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);
		if (rx_x >> 1 >= 0x112 ||
		    (rx_y >> 1 >= 0x12 && rx_y >> 1 <= 0x3ee)) {
			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_A,
					 0x000003ff, 0x100);
			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_A,
					 0x03ff0000, 0);
		} else {
			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_A,
					 0x000003ff, rx_x >> 1);
			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_A,
					 0x03ff0000, rx_y >> 1);
		}
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"rx_x = %x;;rx_y = %x ====>fill to IQC\n",
			rx_x >> 1 & 0x000003ff, rx_y >> 1 & 0x000003ff);
		rtw_dbg(rtwdev, RTW_DBG_RFK, "0xc10 = %x ====>fill to IQC\n",
			rtw_read32(rtwdev, REG_RX_IQC_AB_A));
		break;
	case RF_PATH_B:
		/* [31] = 0 --> Page C */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);
		if (rx_x >> 1 >= 0x112 ||
		    (rx_y >> 1 >= 0x12 && rx_y >> 1 <= 0x3ee)) {
			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_B,
					 0x000003ff, 0x100);
			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_B,
					 0x03ff0000, 0);
		} else {
			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_B,
					 0x000003ff, rx_x >> 1);
			rtw_write32_mask(rtwdev, REG_RX_IQC_AB_B,
					 0x03ff0000, rx_y >> 1);
		}
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"rx_x = %x;;rx_y = %x ====>fill to IQC\n",
			rx_x >> 1 & 0x000003ff, rx_y >> 1 & 0x000003ff);
		rtw_dbg(rtwdev, RTW_DBG_RFK, "0xe10 = %x====>fill to IQC\n",
			rtw_read32(rtwdev, REG_RX_IQC_AB_B));
		break;
	default:
		break;
	}
}

static void rtw8812a_iqk_tx_fill(struct rtw_dev *rtwdev, enum rtw_rf_path path,
				 unsigned int tx_x, unsigned int tx_y)
{
	switch (path) {
	case RF_PATH_A:
		/* [31] = 1 --> Page C1 */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);
		rtw_write32_mask(rtwdev, REG_PREDISTA, BIT(7), 0x1);
		rtw_write32_mask(rtwdev, REG_IQK_DPD_CFG, BIT(18), 0x1);
		rtw_write32_mask(rtwdev, REG_IQK_DPD_CFG, BIT(29), 0x1);
		rtw_write32_mask(rtwdev, REG_CFG_PMPD, BIT(29), 0x1);
		rtw_write32_mask(rtwdev, REG_IQC_Y, 0x000007ff, tx_y);
		rtw_write32_mask(rtwdev, REG_IQC_X, 0x000007ff, tx_x);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"tx_x = %x;;tx_y = %x =====> fill to IQC\n",
			tx_x & 0x000007ff, tx_y & 0x000007ff);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"0xcd4 = %x;;0xccc = %x ====>fill to IQC\n",
			rtw_read32_mask(rtwdev, REG_IQC_X, 0x000007ff),
			rtw_read32_mask(rtwdev, REG_IQC_Y, 0x000007ff));
		break;
	case RF_PATH_B:
		/* [31] = 1 --> Page C1 */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);
		rtw_write32_mask(rtwdev, REG_PREDISTB, BIT(7), 0x1);
		rtw_write32_mask(rtwdev, REG_BPBDB, BIT(18), 0x1);
		rtw_write32_mask(rtwdev, REG_BPBDB, BIT(29), 0x1);
		rtw_write32_mask(rtwdev, REG_PHYTXONB, BIT(29), 0x1);
		rtw_write32_mask(rtwdev, REG_IQKYB, 0x000007ff, tx_y);
		rtw_write32_mask(rtwdev, REG_IQKXB, 0x000007ff, tx_x);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"tx_x = %x;;tx_y = %x =====> fill to IQC\n",
			tx_x & 0x000007ff, tx_y & 0x000007ff);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"0xed4 = %x;;0xecc = %x ====>fill to IQC\n",
			rtw_read32_mask(rtwdev, REG_IQKXB, 0x000007ff),
			rtw_read32_mask(rtwdev, REG_IQKYB, 0x000007ff));
		break;
	default:
		break;
	}
}

static void rtw8812a_iqk(struct rtw_dev *rtwdev)
{
	int tx_x0_temp[10], tx_y0_temp[10], tx_x1_temp[10], tx_y1_temp[10];
	int rx_x0_temp[10], rx_y0_temp[10], rx_x1_temp[10], rx_y1_temp[10];
	bool iqk0_ready = false, tx0_finish = false, rx0_finish = false;
	bool iqk1_ready = false, tx1_finish = false, rx1_finish = false;
	u8 tx0_avg = 0, tx1_avg = 0, rx0_avg = 0, rx1_avg = 0;
	int tx_x0 = 0, tx_y0 = 0, tx_x1 = 0, tx_y1 = 0;
	int rx_x0 = 0, rx_y0 = 0, rx_x1 = 0, rx_y1 = 0;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	bool tx0_fail = true, rx0_fail = true;
	bool tx1_fail = true, rx1_fail = true;
	u8 cal0_retry, cal1_retry;
	u8 delay_count;

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

	/* ========path-A AFE all on======== */
	/* Port 0 DAC/ADC on */
	rtw_write32(rtwdev, REG_AFE_PWR1_A, 0x77777777);
	rtw_write32(rtwdev, REG_AFE_PWR2_A, 0x77777777);

	/* Port 1 DAC/ADC on */
	rtw_write32(rtwdev, REG_AFE_PWR1_B, 0x77777777);
	rtw_write32(rtwdev, REG_AFE_PWR2_B, 0x77777777);

	rtw_write32(rtwdev, REG_RX_WAIT_CCA_TX_CCK_RFON_A, 0x19791979);
	rtw_write32(rtwdev, REG_RX_WAIT_CCA_TX_CCK_RFON_B, 0x19791979);

	/* hardware 3-wire off */
	rtw_write32_mask(rtwdev, REG_3WIRE_SWA, 0xf, 0x4);
	rtw_write32_mask(rtwdev, REG_3WIRE_SWB, 0xf, 0x4);

	/* DAC/ADC sampling rate (160 MHz) */
	rtw_write32_mask(rtwdev, REG_CK_MONHA, GENMASK(26, 24), 0x7);
	rtw_write32_mask(rtwdev, REG_CK_MONHB, GENMASK(26, 24), 0x7);

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);
	/* ====== path A TX IQK RF setting ====== */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x80002);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK, 0x20000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK, 0x3fffd);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK, 0xfe83f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_TXA_PREPAD, RFREG_MASK, 0x931d5);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_RXBB2, RFREG_MASK, 0x8a001);

	/* ====== path B TX IQK RF setting ====== */
	rtw_write_rf(rtwdev, RF_PATH_B, RF_LUTWE, RFREG_MASK, 0x80002);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_MODE_TABLE_ADDR, RFREG_MASK, 0x20000);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_MODE_TABLE_DATA0, RFREG_MASK, 0x3fffd);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_MODE_TABLE_DATA1, RFREG_MASK, 0xfe83f);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_TXA_PREPAD, RFREG_MASK, 0x931d5);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_RXBB2, RFREG_MASK, 0x8a001);

	rtw_write32(rtwdev, REG_DAC_RSTB, 0x00008000);
	rtw_write32_mask(rtwdev, REG_TXAGCIDX, BIT(0), 0x1);
	rtw_write32_mask(rtwdev, REG_INIDLYB, BIT(0), 0x1);
	rtw_write32(rtwdev, REG_IQK_COM00, 0x29002000); /* TX (X,Y) */
	rtw_write32(rtwdev, REG_IQK_COM32, 0xa9002000); /* RX (X,Y) */
	rtw_write32(rtwdev, REG_IQK_COM96, 0x00462910); /* [0]:AGC_en, [15]:idac_K_Mask */
	/* [31] = 1 --> Page C1 */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);

	if (efuse->ext_pa_5g) {
		if (efuse->rfe_option == 1) {
			rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE, 0x821403e3);
			rtw_write32(rtwdev, REG_TXPITMB, 0x821403e3);
		} else {
			rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE, 0x821403f7);
			rtw_write32(rtwdev, REG_TXPITMB, 0x821403f7);
		}
	} else {
		rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE, 0x821403f1);
		rtw_write32(rtwdev, REG_TXPITMB, 0x821403f1);
	}

	if (rtwdev->hal.current_band_type == RTW_BAND_5G) {
		rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x68163e96);
		rtw_write32(rtwdev, REG_RXPITMB, 0x68163e96);
	} else {
		rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x28163e96);
		rtw_write32(rtwdev, REG_RXPITMB, 0x28163e96);

		if (efuse->rfe_option == 3) {
			if (efuse->ext_pa_2g)
				rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE,
					    0x821403e3);
			else
				rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE,
					    0x821403f7);
		}
	}

	/* TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
	rtw_write32(rtwdev, REG_OFDM0_XA_TX_IQ_IMBALANCE, 0x18008c10);
	/* RX_Tone_idx[9:0], RxK_Mask[29] */
	rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x38008c10);
	rtw_write32(rtwdev, REG_INTPO_SETA, 0x00000000);
	/* TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
	rtw_write32(rtwdev, REG_TXTONEB, 0x18008c10);
	/* RX_Tone_idx[9:0], RxK_Mask[29] */
	rtw_write32(rtwdev, REG_RXTONEB, 0x38008c10);
	rtw_write32(rtwdev, REG_INTPO_SETB, 0x00000000);

	cal0_retry = 0;
	cal1_retry = 0;
	while (1) {
		/* one shot */
		rtw_write32(rtwdev, REG_RFECTL_A, 0x00100000);
		rtw_write32(rtwdev, REG_RFECTL_B, 0x00100000);
		rtw_write32(rtwdev, REG_IQK_COM64, 0xfa000000);
		rtw_write32(rtwdev, REG_IQK_COM64, 0xf8000000);

		mdelay(10);

		rtw_write32(rtwdev, REG_RFECTL_A, 0x00000000);
		rtw_write32(rtwdev, REG_RFECTL_B, 0x00000000);

		for (delay_count = 0; delay_count < 20; delay_count++) {
			if (!tx0_finish)
				iqk0_ready = rtw_read32_mask(rtwdev,
							     REG_IQKA_END,
							     BIT(10));
			if (!tx1_finish)
				iqk1_ready = rtw_read32_mask(rtwdev,
							     REG_IQKB_END,
							     BIT(10));
			if (iqk0_ready && iqk1_ready)
				break;

			mdelay(1);
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "TX delay_count = %d\n",
			delay_count);

		if (delay_count < 20) { /* If 20ms No Result, then cal_retry++ */
			/* ============TXIQK Check============== */
			tx0_fail = rtw_read32_mask(rtwdev, REG_IQKA_END, BIT(12));
			tx1_fail = rtw_read32_mask(rtwdev, REG_IQKB_END, BIT(12));

			if (!(tx0_fail || tx0_finish)) {
				rtw_write32(rtwdev, REG_RFECTL_A, 0x02000000);
				tx_x0_temp[tx0_avg] = rtw_read32_mask(rtwdev,
								      REG_IQKA_END,
								      0x07ff0000);
				rtw_write32(rtwdev, REG_RFECTL_A, 0x04000000);
				tx_y0_temp[tx0_avg] = rtw_read32_mask(rtwdev,
								      REG_IQKA_END,
								      0x07ff0000);

				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"tx_x0[%d] = %x ;; tx_y0[%d] = %x\n",
					tx0_avg, tx_x0_temp[tx0_avg],
					tx0_avg, tx_y0_temp[tx0_avg]);

				tx_x0_temp[tx0_avg] <<= 21;
				tx_y0_temp[tx0_avg] <<= 21;

				tx0_avg++;
			} else {
				cal0_retry++;
				if (cal0_retry == 10)
					break;
			}

			if (!(tx1_fail || tx1_finish)) {
				rtw_write32(rtwdev, REG_RFECTL_B, 0x02000000);
				tx_x1_temp[tx1_avg] = rtw_read32_mask(rtwdev,
								      REG_IQKB_END,
								      0x07ff0000);
				rtw_write32(rtwdev, REG_RFECTL_B, 0x04000000);
				tx_y1_temp[tx1_avg] = rtw_read32_mask(rtwdev,
								      REG_IQKB_END,
								      0x07ff0000);

				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"tx_x1[%d] = %x ;; tx_y1[%d] = %x\n",
					tx1_avg, tx_x1_temp[tx1_avg],
					tx1_avg, tx_y1_temp[tx1_avg]);

				tx_x1_temp[tx1_avg] <<= 21;
				tx_y1_temp[tx1_avg] <<= 21;

				tx1_avg++;
			} else {
				cal1_retry++;
				if (cal1_retry == 10)
					break;
			}
		} else {
			cal0_retry++;
			cal1_retry++;

			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"delay 20ms TX IQK Not Ready!!!!!\n");

			if (cal0_retry == 10)
				break;
		}

		if (tx0_avg >= 2)
			tx0_finish = rtw88xxa_iqk_finish(tx0_avg, 4,
							 tx_x0_temp, tx_y0_temp, &tx_x0, &tx_y0,
							 false, false);

		if (tx1_avg >= 2)
			tx1_finish = rtw88xxa_iqk_finish(tx1_avg, 4,
							 tx_x1_temp, tx_y1_temp, &tx_x1, &tx_y1,
							 false, false);

		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"tx0_average = %d, tx1_average = %d\n",
			tx0_avg, tx1_avg);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"tx0_finish = %d, tx1_finish = %d\n",
			tx0_finish, tx1_finish);

		if (tx0_finish && tx1_finish)
			break;

		if ((cal0_retry + tx0_avg) >= 10 ||
		    (cal1_retry + tx1_avg) >= 10)
			break;
	}

	rtw_dbg(rtwdev, RTW_DBG_RFK, "TXA_cal_retry = %d\n", cal0_retry);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "TXB_cal_retry = %d\n", cal1_retry);

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);
	/* Load LOK */
	rtw_write_rf(rtwdev, RF_PATH_A, RF_TXMOD, 0x7fe00,
		     rtw_read_rf(rtwdev, RF_PATH_A, RF_DTXLOK, 0xffc00));
	rtw_write_rf(rtwdev, RF_PATH_B, RF_TXMOD, 0x7fe00,
		     rtw_read_rf(rtwdev, RF_PATH_B, RF_DTXLOK, 0xffc00));
	/* [31] = 1 --> Page C1 */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);

	/* [31] = 0 --> Page C */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);
	if (tx0_finish) {
		/* ====== path A RX IQK RF setting====== */
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x80000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_ADDR, RFREG_MASK,
			     0x30000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA0, RFREG_MASK,
			     0x3f7ff);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_MODE_TABLE_DATA1, RFREG_MASK,
			     0xfe7bf);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_RXBB2, RFREG_MASK, 0x88001);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_TXA_PREPAD, RFREG_MASK, 0x931d1);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE, RFREG_MASK, 0x00000);
	}
	if (tx1_finish) {
		/* ====== path B RX IQK RF setting====== */
		rtw_write_rf(rtwdev, RF_PATH_B, RF_LUTWE, RFREG_MASK, 0x80000);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_MODE_TABLE_ADDR, RFREG_MASK,
			     0x30000);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_MODE_TABLE_DATA0, RFREG_MASK,
			     0x3f7ff);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_MODE_TABLE_DATA1, RFREG_MASK,
			     0xfe7bf);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_RXBB2, RFREG_MASK, 0x88001);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_TXA_PREPAD, RFREG_MASK, 0x931d1);
		rtw_write_rf(rtwdev, RF_PATH_B, RF_LUTWE, RFREG_MASK, 0x00000);
	}

	rtw_write32_mask(rtwdev, REG_IQK_COM00, BIT(31), 0x1);
	rtw_write32_mask(rtwdev, REG_IQK_COM00, BIT(31), 0x0);
	rtw_write32(rtwdev, REG_DAC_RSTB, 0x00008000);

	if (rtwdev->hci.type == RTW_HCI_TYPE_PCIE)
		rtw_write32(rtwdev, REG_IQK_COM96, 0x0046a911);
	else
		rtw_write32(rtwdev, REG_IQK_COM96, 0x0046a890);

	if (efuse->rfe_option == 1) {
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77777717);
		rtw_write32(rtwdev, REG_RFE_INV_A, 0x00000077);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777717);
		rtw_write32(rtwdev, REG_RFE_INV_B, 0x00000077);
	} else {
		rtw_write32(rtwdev, REG_RFE_PINMUX_A, 0x77777717);
		rtw_write32(rtwdev, REG_RFE_INV_A, 0x02000077);
		rtw_write32(rtwdev, REG_RFE_PINMUX_B, 0x77777717);
		rtw_write32(rtwdev, REG_RFE_INV_B, 0x02000077);
	}

	/* [31] = 1 --> Page C1 */
	rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);

	if (tx0_finish) {
		/* TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
		rtw_write32(rtwdev, REG_OFDM0_XA_TX_IQ_IMBALANCE, 0x38008c10);
		/* RX_Tone_idx[9:0], RxK_Mask[29] */
		rtw_write32(rtwdev, REG_OFDM0_A_TX_AFE, 0x18008c10);
		rtw_write32(rtwdev, REG_OFDM0_XB_TX_IQ_IMBALANCE, 0x82140119);
	}
	if (tx1_finish) {
		/* TX_Tone_idx[9:0], TxK_Mask[29] TX_Tone = 16 */
		rtw_write32(rtwdev, REG_TXTONEB, 0x38008c10);
		/* RX_Tone_idx[9:0], RxK_Mask[29] */
		rtw_write32(rtwdev, REG_RXTONEB, 0x18008c10);
		rtw_write32(rtwdev, REG_TXPITMB, 0x82140119);
	}

	cal0_retry = 0;
	cal1_retry = 0;
	while (1) {
		/* one shot */
		/* [31] = 0 --> Page C */
		rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);

		if (tx0_finish) {
			rtw_write32_mask(rtwdev, REG_IQK_COM00, 0x03FF8000,
					 tx_x0 & 0x000007ff);
			rtw_write32_mask(rtwdev, REG_IQK_COM00, 0x000007FF,
					 tx_y0 & 0x000007ff);
			/* [31] = 1 --> Page C1 */
			rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);
			if (efuse->rfe_option == 1)
				rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x28161500);
			else
				rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x28160cc0);
			rtw_write32(rtwdev, REG_RFECTL_A, 0x00300000);
			rtw_write32(rtwdev, REG_RFECTL_A, 0x00100000);
			mdelay(5);
			rtw_write32(rtwdev, REG_TSSI_TRK_SW, 0x3c000000);
			rtw_write32(rtwdev, REG_RFECTL_A, 0x00000000);
		}

		if (tx1_finish) {
			/* [31] = 0 --> Page C */
			rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x0);
			rtw_write32_mask(rtwdev, REG_IQK_COM00, 0x03FF8000,
					 tx_x1 & 0x000007ff);
			rtw_write32_mask(rtwdev, REG_IQK_COM00, 0x000007FF,
					 tx_y1 & 0x000007ff);
			/* [31] = 1 --> Page C1 */
			rtw_write32_mask(rtwdev, REG_CCASEL, BIT(31), 0x1);
			if (efuse->rfe_option == 1)
				rtw_write32(rtwdev, REG_RXPITMB, 0x28161500);
			else
				rtw_write32(rtwdev, REG_RXPITMB, 0x28160ca0);
			rtw_write32(rtwdev, REG_RFECTL_B, 0x00300000);
			rtw_write32(rtwdev, REG_RFECTL_B, 0x00100000);
			mdelay(5);
			rtw_write32(rtwdev, REG_RXPITMB, 0x3c000000);
			rtw_write32(rtwdev, REG_RFECTL_B, 0x00000000);
		}

		for (delay_count = 0; delay_count < 20; delay_count++) {
			if (!rx0_finish && tx0_finish)
				iqk0_ready = rtw_read32_mask(rtwdev,
							     REG_IQKA_END,
							     BIT(10));
			if (!rx1_finish && tx1_finish)
				iqk1_ready = rtw_read32_mask(rtwdev,
							     REG_IQKB_END,
							     BIT(10));
			if (iqk0_ready && iqk1_ready)
				break;

			mdelay(1);
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "RX delay_count = %d\n",
			delay_count);

		if (delay_count < 20) { /* If 20ms No Result, then cal_retry++ */
			/* ============RXIQK Check============== */
			rx0_fail = rtw_read32_mask(rtwdev, REG_IQKA_END, BIT(11));
			rx1_fail = rtw_read32_mask(rtwdev, REG_IQKB_END, BIT(11));

			if (!(rx0_fail || rx0_finish) && tx0_finish) {
				rtw_write32(rtwdev, REG_RFECTL_A, 0x06000000);
				rx_x0_temp[rx0_avg] = rtw_read32_mask(rtwdev,
								      REG_IQKA_END,
								      0x07ff0000);
				rtw_write32(rtwdev, REG_RFECTL_A, 0x08000000);
				rx_y0_temp[rx0_avg] = rtw_read32_mask(rtwdev,
								      REG_IQKA_END,
								      0x07ff0000);

				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"rx_x0[%d] = %x ;; rx_y0[%d] = %x\n",
					rx0_avg, rx_x0_temp[rx0_avg],
					rx0_avg, rx_y0_temp[rx0_avg]);

				rx_x0_temp[rx0_avg] <<= 21;
				rx_y0_temp[rx0_avg] <<= 21;

				rx0_avg++;
			} else {
				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"1. RXA_cal_retry = %d\n", cal0_retry);

				cal0_retry++;
				if (cal0_retry == 10)
					break;
			}

			if (!(rx1_fail || rx1_finish) && tx1_finish) {
				rtw_write32(rtwdev, REG_RFECTL_B, 0x06000000);
				rx_x1_temp[rx1_avg] = rtw_read32_mask(rtwdev,
								      REG_IQKB_END,
								      0x07ff0000);
				rtw_write32(rtwdev, REG_RFECTL_B, 0x08000000);
				rx_y1_temp[rx1_avg] = rtw_read32_mask(rtwdev,
								      REG_IQKB_END,
								      0x07ff0000);

				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"rx_x1[%d] = %x ;; rx_y1[%d] = %x\n",
					rx1_avg, rx_x1_temp[rx1_avg],
					rx1_avg, rx_y1_temp[rx1_avg]);

				rx_x1_temp[rx1_avg] <<= 21;
				rx_y1_temp[rx1_avg] <<= 21;

				rx1_avg++;
			} else {
				cal1_retry++;
				if (cal1_retry == 10)
					break;
			}
		} else {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"2. RXA_cal_retry = %d\n", cal0_retry);

			cal0_retry++;
			cal1_retry++;

			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"delay 20ms RX IQK Not Ready!!!!!\n");

			if (cal0_retry == 10)
				break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "3. RXA_cal_retry = %d\n",
			cal0_retry);

		if (rx0_avg >= 2)
			rx0_finish = rtw88xxa_iqk_finish(rx0_avg, 4,
							 rx_x0_temp, rx_y0_temp,
							 &rx_x0, &rx_y0,
							 true, false);

		if (rx1_avg >= 2)
			rx1_finish = rtw88xxa_iqk_finish(rx1_avg, 4,
							 rx_x1_temp, rx_y1_temp,
							 &rx_x1, &rx_y1,
							 true, false);

		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"rx0_average = %d, rx1_average = %d\n",
			rx0_avg, rx1_avg);
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"rx0_finish = %d, rx1_finish = %d\n",
			rx0_finish, rx1_finish);

		if ((rx0_finish || !tx0_finish) && (rx1_finish || !tx1_finish))
			break;

		if ((cal0_retry + rx0_avg) >= 10 ||
		    (cal1_retry + rx1_avg) >= 10 ||
		    rx0_avg == 3 || rx1_avg == 3)
			break;
	}

	rtw_dbg(rtwdev, RTW_DBG_RFK, "RXA_cal_retry = %d\n", cal0_retry);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "RXB_cal_retry = %d\n", cal1_retry);

	/* FillIQK Result */
	rtw_dbg(rtwdev, RTW_DBG_RFK, "========Path_A =======\n");

	if (tx0_finish)
		rtw8812a_iqk_tx_fill(rtwdev, RF_PATH_A, tx_x0, tx_y0);
	else
		rtw8812a_iqk_tx_fill(rtwdev, RF_PATH_A, 0x200, 0x0);

	if (rx0_finish)
		rtw8812a_iqk_rx_fill(rtwdev, RF_PATH_A, rx_x0, rx_y0);
	else
		rtw8812a_iqk_rx_fill(rtwdev, RF_PATH_A, 0x200, 0x0);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "========Path_B =======\n");

	if (tx1_finish)
		rtw8812a_iqk_tx_fill(rtwdev, RF_PATH_B, tx_x1, tx_y1);
	else
		rtw8812a_iqk_tx_fill(rtwdev, RF_PATH_B, 0x200, 0x0);

	if (rx1_finish)
		rtw8812a_iqk_rx_fill(rtwdev, RF_PATH_B, rx_x1, rx_y1);
	else
		rtw8812a_iqk_rx_fill(rtwdev, RF_PATH_B, 0x200, 0x0);
}

#define MACBB_REG_NUM_8812A 9
#define AFE_REG_NUM_8812A 12
#define RF_REG_NUM_8812A 3

static void rtw8812a_do_iqk(struct rtw_dev *rtwdev)
{
	static const u32 backup_macbb_reg[MACBB_REG_NUM_8812A] = {
		0x520, 0x550, 0x808, 0xa04, 0x90c, 0xc00, 0xe00, 0x838, 0x82c
	};
	static const u32 backup_afe_reg[AFE_REG_NUM_8812A] = {
		0xc5c, 0xc60, 0xc64, 0xc68, 0xcb0, 0xcb4,
		0xe5c, 0xe60, 0xe64, 0xe68, 0xeb0, 0xeb4
	};
	static const u32 backup_rf_reg[RF_REG_NUM_8812A] = {
		0x65, 0x8f, 0x0
	};
	u32 macbb_backup[MACBB_REG_NUM_8812A] = {};
	u32 afe_backup[AFE_REG_NUM_8812A] = {};
	u32 rfa_backup[RF_REG_NUM_8812A] = {};
	u32 rfb_backup[RF_REG_NUM_8812A] = {};
	u32 reg_cb8, reg_eb8;

	rtw88xxa_iqk_backup_mac_bb(rtwdev, macbb_backup,
				   backup_macbb_reg, MACBB_REG_NUM_8812A);

	rtw_write32_set(rtwdev, REG_CCASEL, BIT(31));
	reg_cb8 = rtw_read32(rtwdev, REG_RFECTL_A);
	reg_eb8 = rtw_read32(rtwdev, REG_RFECTL_B);
	rtw_write32_clr(rtwdev, REG_CCASEL, BIT(31));

	rtw88xxa_iqk_backup_afe(rtwdev, afe_backup,
				backup_afe_reg, AFE_REG_NUM_8812A);
	rtw8812a_iqk_backup_rf(rtwdev, rfa_backup, rfb_backup,
			       backup_rf_reg, RF_REG_NUM_8812A);

	rtw88xxa_iqk_configure_mac(rtwdev);

	rtw8812a_iqk(rtwdev);

	rtw8812a_iqk_restore_rf(rtwdev, RF_PATH_A, backup_rf_reg,
				rfa_backup, RF_REG_NUM_8812A);
	rtw8812a_iqk_restore_rf(rtwdev, RF_PATH_B, backup_rf_reg,
				rfb_backup, RF_REG_NUM_8812A);

	rtw8812a_iqk_restore_afe(rtwdev, afe_backup,
				 backup_afe_reg, AFE_REG_NUM_8812A);

	rtw_write32_set(rtwdev, REG_CCASEL, BIT(31));
	rtw_write32(rtwdev, REG_RFECTL_A, reg_cb8);
	rtw_write32(rtwdev, REG_RFECTL_B, reg_eb8);
	rtw_write32_clr(rtwdev, REG_CCASEL, BIT(31));

	rtw88xxa_iqk_restore_mac_bb(rtwdev, macbb_backup,
				    backup_macbb_reg, MACBB_REG_NUM_8812A);
}

static void rtw8812a_phy_calibration(struct rtw_dev *rtwdev)
{
	u8 channel = rtwdev->hal.current_channel;

	rtw8812a_do_iqk(rtwdev);

	/* The official driver wants to do this after connecting
	 * but before first writing a new igi (phydm_get_new_igi).
	 * Here seems close enough.
	 */
	if (channel >= 36 && channel <= 64)
		rtw_load_table(rtwdev, &rtw8812a_agc_diff_lb_tbl);
	else if (channel >= 100)
		rtw_load_table(rtwdev, &rtw8812a_agc_diff_hb_tbl);
}

static void rtw8812a_pwr_track(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;

	if (!dm_info->pwr_trk_triggered) {
		rtw_write_rf(rtwdev, RF_PATH_A, RF_T_METER,
			     GENMASK(17, 16), 0x03);
		dm_info->pwr_trk_triggered = true;
		return;
	}

	rtw88xxa_phy_pwrtrack(rtwdev, rtw8812a_do_lck, rtw8812a_do_iqk);
	dm_info->pwr_trk_triggered = false;
}

static void rtw8812a_led_set(struct led_classdev *led,
			     enum led_brightness brightness)
{
	struct rtw_dev *rtwdev = container_of(led, struct rtw_dev, led_cdev);
	u8 ledcfg;

	ledcfg = rtw_read8(rtwdev, REG_LED_CFG);
	ledcfg &= BIT(6) | BIT(4);
	ledcfg |= BIT(5);

	if (brightness == LED_OFF)
		ledcfg |= BIT(3);

	rtw_write8(rtwdev, REG_LED_CFG, ledcfg);
}

static void rtw8812a_fill_txdesc_checksum(struct rtw_dev *rtwdev,
					  struct rtw_tx_pkt_info *pkt_info,
					  u8 *txdesc)
{
	fill_txdesc_checksum_common(txdesc, 16);
}

static void rtw8812a_coex_cfg_init(struct rtw_dev *rtwdev)
{
}

static void rtw8812a_coex_cfg_gnt_fix(struct rtw_dev *rtwdev)
{
}

static void rtw8821a_coex_cfg_rfe_type(struct rtw_dev *rtwdev)
{
}

static void rtw8821a_coex_cfg_wl_tx_power(struct rtw_dev *rtwdev, u8 wl_pwr)
{
}

static void rtw8821a_coex_cfg_wl_rx_gain(struct rtw_dev *rtwdev, bool low_gain)
{
}

static const struct rtw_chip_ops rtw8812a_ops = {
	.power_on		= rtw88xxa_power_on,
	.power_off		= rtw8812a_power_off,
	.phy_set_param		= NULL,
	.read_efuse		= rtw88xxa_read_efuse,
	.query_phy_status	= rtw8812a_query_phy_status,
	.set_channel		= rtw88xxa_set_channel,
	.mac_init		= NULL,
	.read_rf		= rtw88xxa_phy_read_rf,
	.write_rf		= rtw_phy_write_rf_reg_sipi,
	.set_antenna		= NULL,
	.set_tx_power_index	= rtw88xxa_set_tx_power_index,
	.cfg_ldo25		= rtw8812a_cfg_ldo25,
	.efuse_grant		= rtw88xxa_efuse_grant,
	.set_ampdu_factor	= NULL,
	.false_alarm_statistics	= rtw88xxa_false_alarm_statistics,
	.phy_calibration	= rtw8812a_phy_calibration,
	.cck_pd_set		= rtw88xxa_phy_cck_pd_set,
	.pwr_track		= rtw8812a_pwr_track,
	.config_bfee		= NULL,
	.set_gid_table		= NULL,
	.cfg_csi_rate		= NULL,
	.led_set		= rtw8812a_led_set,
	.fill_txdesc_checksum	= rtw8812a_fill_txdesc_checksum,
	.coex_set_init		= rtw8812a_coex_cfg_init,
	.coex_set_ant_switch	= NULL,
	.coex_set_gnt_fix	= rtw8812a_coex_cfg_gnt_fix,
	.coex_set_gnt_debug	= NULL,
	.coex_set_rfe_type	= rtw8821a_coex_cfg_rfe_type,
	.coex_set_wl_tx_power	= rtw8821a_coex_cfg_wl_tx_power,
	.coex_set_wl_rx_gain	= rtw8821a_coex_cfg_wl_rx_gain,
};

static const struct rtw_page_table page_table_8812a[] = {
	/* hq_num, nq_num, lq_num, exq_num, gapq_num */
	{0, 0, 0, 0, 0},	/* SDIO */
	{0, 0, 0, 0, 0},	/* PCI */
	{16, 0, 0, 0, 1},	/* 2 bulk out endpoints */
	{16, 0, 16, 0, 1},	/* 3 bulk out endpoints */
	{16, 0, 16, 0, 1},	/* 4 bulk out endpoints */
};

static const struct rtw_rqpn rqpn_table_8812a[] = {
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

static const struct rtw_prioq_addrs prioq_addrs_8812a = {
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

static const struct rtw_hw_reg rtw8812a_dig[] = {
	[0] = { .addr = REG_RXIGI_A, .mask = 0x7f },
	[1] = { .addr = REG_RXIGI_B, .mask = 0x7f },
};

static const struct rtw_rfe_def rtw8812a_rfe_defs[] = {
	[0] = { .phy_pg_tbl	= &rtw8812a_bb_pg_tbl,
		.txpwr_lmt_tbl	= &rtw8812a_txpwr_lmt_tbl,
		.pwr_track_tbl	= &rtw8812a_rtw_pwr_track_tbl, },
	[1] = { .phy_pg_tbl	= &rtw8812a_bb_pg_tbl,
		.txpwr_lmt_tbl	= &rtw8812a_txpwr_lmt_tbl,
		.pwr_track_tbl	= &rtw8812a_rtw_pwr_track_tbl, },
	[2] = { .phy_pg_tbl	= &rtw8812a_bb_pg_tbl,
		.txpwr_lmt_tbl	= &rtw8812a_txpwr_lmt_tbl,
		.pwr_track_tbl	= &rtw8812a_rtw_pwr_track_tbl, },
	[3] = { .phy_pg_tbl	= &rtw8812a_bb_pg_rfe3_tbl,
		.txpwr_lmt_tbl	= &rtw8812a_txpwr_lmt_tbl,
		.pwr_track_tbl	= &rtw8812a_rtw_pwr_track_rfe3_tbl, },
};

static const u8 wl_rssi_step_8812a[] = {101, 45, 101, 40};
static const u8 bt_rssi_step_8812a[] = {101, 101, 101, 101};

static const struct coex_rf_para rf_para_tx_8812a[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 20, false, 7}, /* for WL-CPT */
	{8, 17, true, 4},
	{7, 18, true, 4},
	{6, 19, true, 4},
	{5, 20, true, 4}
};

static const struct coex_rf_para rf_para_rx_8812a[] = {
	{0, 0, false, 7},  /* for normal */
	{0, 20, false, 7}, /* for WL-CPT */
	{3, 24, true, 5},
	{2, 26, true, 5},
	{1, 27, true, 5},
	{0, 28, true, 5}
};

static_assert(ARRAY_SIZE(rf_para_tx_8812a) == ARRAY_SIZE(rf_para_rx_8812a));

const struct rtw_chip_info rtw8812a_hw_spec = {
	.ops = &rtw8812a_ops,
	.id = RTW_CHIP_TYPE_8812A,
	.fw_name = "rtw88/rtw8812a_fw.bin",
	.wlan_cpu = RTW_WCPU_8051,
	.tx_pkt_desc_sz = 40,
	.tx_buf_desc_sz = 16,
	.rx_pkt_desc_sz = 24,
	.rx_buf_desc_sz = 8,
	.phy_efuse_size = 512,
	.log_efuse_size = 512,
	.ptct_efuse_size = 0,
	.txff_size = 131072,
	.rxff_size = 16128,
	.rsvd_drv_pg_num = 9,
	.txgi_factor = 1,
	.is_pwr_by_rate_dec = true,
	.max_power_index = 0x3f,
	.csi_buf_pg_num = 0,
	.band = RTW_BAND_2G | RTW_BAND_5G,
	.page_size = 512,
	.dig_min = 0x20,
	.ht_supported = true,
	.vht_supported = true,
	.lps_deep_mode_supported = 0,
	.sys_func_en = 0xFD,
	.pwr_on_seq = card_enable_flow_8812a,
	.pwr_off_seq = card_disable_flow_8812a,
	.page_table = page_table_8812a,
	.rqpn_table = rqpn_table_8812a,
	.prioq_addrs = &prioq_addrs_8812a,
	.intf_table = NULL,
	.dig = rtw8812a_dig,
	.rf_sipi_addr = {REG_LSSI_WRITE_A, REG_LSSI_WRITE_B},
	.ltecoex_addr = NULL,
	.mac_tbl = &rtw8812a_mac_tbl,
	.agc_tbl = &rtw8812a_agc_tbl,
	.bb_tbl = &rtw8812a_bb_tbl,
	.rf_tbl = {&rtw8812a_rf_a_tbl, &rtw8812a_rf_b_tbl},
	.rfe_defs = rtw8812a_rfe_defs,
	.rfe_defs_size = ARRAY_SIZE(rtw8812a_rfe_defs),
	.rx_ldpc = false,
	.amsdu_in_ampdu = true,
	.hw_feature_report = false,
	.c2h_ra_report_size = 4,
	.old_datarate_fb_limit = true,
	.usb_tx_agg_desc_num = 1,
	.iqk_threshold = 8,
	.ampdu_density = IEEE80211_HT_MPDU_DENSITY_16,
	.max_scan_ie_len = IEEE80211_MAX_DATA_LEN,

	.coex_para_ver = 0, /* no coex code in 8812au driver */
	.bt_desired_ver = 0,
	.scbd_support = false,
	.new_scbd10_def = false,
	.ble_hid_profile_support = false,
	.wl_mimo_ps_support = false,
	.pstdma_type = COEX_PSTDMA_FORCE_LPSOFF,
	.bt_rssi_type = COEX_BTRSSI_RATIO,
	.ant_isolation = 15,
	.rssi_tolerance = 2,
	.wl_rssi_step = wl_rssi_step_8812a,
	.bt_rssi_step = bt_rssi_step_8812a,
	.table_sant_num = 0,
	.table_sant = NULL,
	.table_nsant_num = 0,
	.table_nsant = NULL,
	.tdma_sant_num = 0,
	.tdma_sant = NULL,
	.tdma_nsant_num = 0,
	.tdma_nsant = NULL,
	.wl_rf_para_num = ARRAY_SIZE(rf_para_tx_8812a),
	.wl_rf_para_tx = rf_para_tx_8812a,
	.wl_rf_para_rx = rf_para_rx_8812a,
	.bt_afh_span_bw20 = 0x20,
	.bt_afh_span_bw40 = 0x30,
	.afh_5g_num = 0,
	.afh_5g = NULL,
	.coex_info_hw_regs_num = 0,
	.coex_info_hw_regs = NULL,
};
EXPORT_SYMBOL(rtw8812a_hw_spec);

MODULE_FIRMWARE("rtw88/rtw8812a_fw.bin");

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ac wireless 8812a driver");
MODULE_LICENSE("Dual BSD/GPL");
