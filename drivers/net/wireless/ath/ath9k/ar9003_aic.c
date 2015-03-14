/*
 * Copyright (c) 2015 Qualcomm Atheros Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "hw.h"
#include "hw-ops.h"
#include "ar9003_mci.h"
#include "ar9003_aic.h"
#include "reg_aic.h"

static bool ar9003_hw_is_aic_enabled(struct ath_hw *ah)
{
	struct ath9k_hw_mci *mci_hw = &ah->btcoex_hw.mci;

	if (mci_hw->config & ATH_MCI_CONFIG_DISABLE_AIC)
		return false;

	return true;
}

static void ar9003_aic_gain_table(struct ath_hw *ah)
{
	u32 aic_atten_word[19], i;

	/* Config LNA gain difference */
	REG_WRITE(ah, AR_PHY_BT_COEX_4, 0x2c200a00);
	REG_WRITE(ah, AR_PHY_BT_COEX_5, 0x5c4e4438);

	/* Program gain table */
	aic_atten_word[0] = (0x1 & 0xf) << 14 | (0x1f & 0x1f) << 9 | (0x0 & 0xf) << 5 |
		(0x1f & 0x1f); /* -01 dB: 4'd1, 5'd31,  00 dB: 4'd0, 5'd31 */
	aic_atten_word[1] = (0x3 & 0xf) << 14 | (0x1f & 0x1f) << 9 | (0x2 & 0xf) << 5 |
		(0x1f & 0x1f); /* -03 dB: 4'd3, 5'd31, -02 dB: 4'd2, 5'd31 */
	aic_atten_word[2] = (0x5 & 0xf) << 14 | (0x1f & 0x1f) << 9 | (0x4 & 0xf) << 5 |
		(0x1f & 0x1f); /* -05 dB: 4'd5, 5'd31, -04 dB: 4'd4, 5'd31 */
	aic_atten_word[3] = (0x1 & 0xf) << 14 | (0x1e & 0x1f) << 9 | (0x0 & 0xf) << 5 |
		(0x1e & 0x1f); /* -07 dB: 4'd1, 5'd30, -06 dB: 4'd0, 5'd30 */
	aic_atten_word[4] = (0x3 & 0xf) << 14 | (0x1e & 0x1f) << 9 | (0x2 & 0xf) << 5 |
		(0x1e & 0x1f); /* -09 dB: 4'd3, 5'd30, -08 dB: 4'd2, 5'd30 */
	aic_atten_word[5] = (0x5 & 0xf) << 14 | (0x1e & 0x1f) << 9 | (0x4 & 0xf) << 5 |
		(0x1e & 0x1f); /* -11 dB: 4'd5, 5'd30, -10 dB: 4'd4, 5'd30 */
	aic_atten_word[6] = (0x1 & 0xf) << 14 | (0xf & 0x1f) << 9  | (0x0 & 0xf) << 5 |
		(0xf & 0x1f);  /* -13 dB: 4'd1, 5'd15, -12 dB: 4'd0, 5'd15 */
	aic_atten_word[7] = (0x3 & 0xf) << 14 | (0xf & 0x1f) << 9  | (0x2 & 0xf) << 5 |
		(0xf & 0x1f);  /* -15 dB: 4'd3, 5'd15, -14 dB: 4'd2, 5'd15 */
	aic_atten_word[8] = (0x5 & 0xf) << 14 | (0xf & 0x1f) << 9  | (0x4 & 0xf) << 5 |
		(0xf & 0x1f);  /* -17 dB: 4'd5, 5'd15, -16 dB: 4'd4, 5'd15 */
	aic_atten_word[9] = (0x1 & 0xf) << 14 | (0x7 & 0x1f) << 9  | (0x0 & 0xf) << 5 |
		(0x7 & 0x1f);  /* -19 dB: 4'd1, 5'd07, -18 dB: 4'd0, 5'd07 */
	aic_atten_word[10] = (0x3 & 0xf) << 14 | (0x7 & 0x1f) << 9  | (0x2 & 0xf) << 5 |
		(0x7 & 0x1f);  /* -21 dB: 4'd3, 5'd07, -20 dB: 4'd2, 5'd07 */
	aic_atten_word[11] = (0x5 & 0xf) << 14 | (0x7 & 0x1f) << 9  | (0x4 & 0xf) << 5 |
		(0x7 & 0x1f);  /* -23 dB: 4'd5, 5'd07, -22 dB: 4'd4, 5'd07 */
	aic_atten_word[12] = (0x7 & 0xf) << 14 | (0x7 & 0x1f) << 9  | (0x6 & 0xf) << 5 |
		(0x7 & 0x1f);  /* -25 dB: 4'd7, 5'd07, -24 dB: 4'd6, 5'd07 */
	aic_atten_word[13] = (0x3 & 0xf) << 14 | (0x3 & 0x1f) << 9  | (0x2 & 0xf) << 5 |
		(0x3 & 0x1f);  /* -27 dB: 4'd3, 5'd03, -26 dB: 4'd2, 5'd03 */
	aic_atten_word[14] = (0x5 & 0xf) << 14 | (0x3 & 0x1f) << 9  | (0x4 & 0xf) << 5 |
		(0x3 & 0x1f);  /* -29 dB: 4'd5, 5'd03, -28 dB: 4'd4, 5'd03 */
	aic_atten_word[15] = (0x1 & 0xf) << 14 | (0x1 & 0x1f) << 9  | (0x0 & 0xf) << 5 |
		(0x1 & 0x1f);  /* -31 dB: 4'd1, 5'd01, -30 dB: 4'd0, 5'd01 */
	aic_atten_word[16] = (0x3 & 0xf) << 14 | (0x1 & 0x1f) << 9  | (0x2 & 0xf) << 5 |
		(0x1 & 0x1f);  /* -33 dB: 4'd3, 5'd01, -32 dB: 4'd2, 5'd01 */
	aic_atten_word[17] = (0x5 & 0xf) << 14 | (0x1 & 0x1f) << 9  | (0x4 & 0xf) << 5 |
		(0x1 & 0x1f);  /* -35 dB: 4'd5, 5'd01, -34 dB: 4'd4, 5'd01 */
	aic_atten_word[18] = (0x7 & 0xf) << 14 | (0x1 & 0x1f) << 9  | (0x6 & 0xf) << 5 |
		(0x1 & 0x1f);  /* -37 dB: 4'd7, 5'd01, -36 dB: 4'd6, 5'd01 */

	/* Write to Gain table with auto increment enabled. */
	REG_WRITE(ah, (AR_PHY_AIC_SRAM_ADDR_B0 + 0x3000),
		  (ATH_AIC_SRAM_AUTO_INCREMENT |
		   ATH_AIC_SRAM_GAIN_TABLE_OFFSET));

	for (i = 0; i < 19; i++) {
		REG_WRITE(ah, (AR_PHY_AIC_SRAM_DATA_B0 + 0x3000),
			  aic_atten_word[i]);
	}
}

static void ar9003_aic_cal_start(struct ath_hw *ah, u8 min_valid_count)
{
	struct ath9k_hw_aic *aic = &ah->btcoex_hw.aic;
	int i;

	/* Write to Gain table with auto increment enabled. */
	REG_WRITE(ah, (AR_PHY_AIC_SRAM_ADDR_B0 + 0x3000),
		  (ATH_AIC_SRAM_AUTO_INCREMENT |
		   ATH_AIC_SRAM_CAL_OFFSET));

	for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
		REG_WRITE(ah, (AR_PHY_AIC_SRAM_DATA_B0 + 0x3000), 0);
		aic->aic_sram[i] = 0;
	}

	REG_WRITE(ah, AR_PHY_AIC_CTRL_0_B0,
		  (SM(0, AR_PHY_AIC_MON_ENABLE) |
		   SM(127, AR_PHY_AIC_CAL_MAX_HOP_COUNT) |
		   SM(min_valid_count, AR_PHY_AIC_CAL_MIN_VALID_COUNT) |
		   SM(37, AR_PHY_AIC_F_WLAN) |
		   SM(1, AR_PHY_AIC_CAL_CH_VALID_RESET) |
		   SM(0, AR_PHY_AIC_CAL_ENABLE) |
		   SM(0x40, AR_PHY_AIC_BTTX_PWR_THR) |
		   SM(0, AR_PHY_AIC_ENABLE)));

	REG_WRITE(ah, AR_PHY_AIC_CTRL_0_B1,
		  (SM(0, AR_PHY_AIC_MON_ENABLE) |
		   SM(1, AR_PHY_AIC_CAL_CH_VALID_RESET) |
		   SM(0, AR_PHY_AIC_CAL_ENABLE) |
		   SM(0x40, AR_PHY_AIC_BTTX_PWR_THR) |
		   SM(0, AR_PHY_AIC_ENABLE)));

	REG_WRITE(ah, AR_PHY_AIC_CTRL_1_B0,
		  (SM(8, AR_PHY_AIC_CAL_BT_REF_DELAY) |
		   SM(0, AR_PHY_AIC_BT_IDLE_CFG) |
		   SM(1, AR_PHY_AIC_STDBY_COND) |
		   SM(37, AR_PHY_AIC_STDBY_ROT_ATT_DB) |
		   SM(5, AR_PHY_AIC_STDBY_COM_ATT_DB) |
		   SM(15, AR_PHY_AIC_RSSI_MAX) |
		   SM(0, AR_PHY_AIC_RSSI_MIN)));

	REG_WRITE(ah, AR_PHY_AIC_CTRL_1_B1,
		  (SM(15, AR_PHY_AIC_RSSI_MAX) |
		   SM(0, AR_PHY_AIC_RSSI_MIN)));

	REG_WRITE(ah, AR_PHY_AIC_CTRL_2_B0,
		  (SM(44, AR_PHY_AIC_RADIO_DELAY) |
		   SM(8, AR_PHY_AIC_CAL_STEP_SIZE_CORR) |
		   SM(12, AR_PHY_AIC_CAL_ROT_IDX_CORR) |
		   SM(2, AR_PHY_AIC_CAL_CONV_CHECK_FACTOR) |
		   SM(5, AR_PHY_AIC_ROT_IDX_COUNT_MAX) |
		   SM(0, AR_PHY_AIC_CAL_SYNTH_TOGGLE) |
		   SM(0, AR_PHY_AIC_CAL_SYNTH_AFTER_BTRX) |
		   SM(200, AR_PHY_AIC_CAL_SYNTH_SETTLING)));

	REG_WRITE(ah, AR_PHY_AIC_CTRL_3_B0,
		  (SM(2, AR_PHY_AIC_MON_MAX_HOP_COUNT) |
		   SM(1, AR_PHY_AIC_MON_MIN_STALE_COUNT) |
		   SM(1, AR_PHY_AIC_MON_PWR_EST_LONG) |
		   SM(2, AR_PHY_AIC_MON_PD_TALLY_SCALING) |
		   SM(10, AR_PHY_AIC_MON_PERF_THR) |
		   SM(2, AR_PHY_AIC_CAL_TARGET_MAG_SETTING) |
		   SM(1, AR_PHY_AIC_CAL_PERF_CHECK_FACTOR) |
		   SM(1, AR_PHY_AIC_CAL_PWR_EST_LONG)));

	REG_WRITE(ah, AR_PHY_AIC_CTRL_4_B0,
		  (SM(2, AR_PHY_AIC_CAL_ROT_ATT_DB_EST_ISO) |
		   SM(3, AR_PHY_AIC_CAL_COM_ATT_DB_EST_ISO) |
		   SM(0, AR_PHY_AIC_CAL_ISO_EST_INIT_SETTING) |
		   SM(2, AR_PHY_AIC_CAL_COM_ATT_DB_BACKOFF) |
		   SM(1, AR_PHY_AIC_CAL_COM_ATT_DB_FIXED)));

	REG_WRITE(ah, AR_PHY_AIC_CTRL_4_B1,
		  (SM(2, AR_PHY_AIC_CAL_ROT_ATT_DB_EST_ISO) |
		   SM(3, AR_PHY_AIC_CAL_COM_ATT_DB_EST_ISO) |
		   SM(0, AR_PHY_AIC_CAL_ISO_EST_INIT_SETTING) |
		   SM(2, AR_PHY_AIC_CAL_COM_ATT_DB_BACKOFF) |
		   SM(1, AR_PHY_AIC_CAL_COM_ATT_DB_FIXED)));

	ar9003_aic_gain_table(ah);

	/* Need to enable AIC reference signal in BT modem. */
	REG_WRITE(ah, ATH_AIC_BT_JUPITER_CTRL,
		  (REG_READ(ah, ATH_AIC_BT_JUPITER_CTRL) |
		   ATH_AIC_BT_AIC_ENABLE));

	aic->aic_cal_start_time = REG_READ(ah, AR_TSF_L32);

	/* Start calibration */
	REG_CLR_BIT(ah, AR_PHY_AIC_CTRL_0_B1, AR_PHY_AIC_CAL_ENABLE);
	REG_SET_BIT(ah, AR_PHY_AIC_CTRL_0_B1, AR_PHY_AIC_CAL_CH_VALID_RESET);
	REG_SET_BIT(ah, AR_PHY_AIC_CTRL_0_B1, AR_PHY_AIC_CAL_ENABLE);

	aic->aic_caled_chan = 0;
	aic->aic_cal_state = AIC_CAL_STATE_STARTED;
}

u8 ar9003_aic_calibration_single(struct ath_hw *ah)
{
	struct ath9k_hw_mci *mci_hw = &ah->btcoex_hw.mci;
	u8 cal_ret = 0;
	int num_chan;

	num_chan = MS(mci_hw->config, ATH_MCI_CONFIG_AIC_CAL_NUM_CHAN);

	ar9003_aic_cal_start(ah, num_chan);

	return cal_ret;
}

void ar9003_hw_attach_aic_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);

	priv_ops->is_aic_enabled = ar9003_hw_is_aic_enabled;
}
