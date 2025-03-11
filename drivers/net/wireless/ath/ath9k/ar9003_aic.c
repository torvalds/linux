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
#include "ar9003_phy.h"
#include "reg_aic.h"

static const u8 com_att_db_table[ATH_AIC_MAX_COM_ATT_DB_TABLE] = {
	0, 3, 9, 15, 21, 27
};

static const u16 aic_lin_table[ATH_AIC_MAX_AIC_LIN_TABLE] = {
	8191, 7300, 6506, 5799, 5168, 4606, 4105, 3659,
	3261, 2906, 2590, 2309, 2057, 1834, 1634, 1457,
	1298, 1157, 1031, 919,	819,  730,  651,  580,
	517,  461,  411,  366,	326,  291,  259,  231,
	206,  183,  163,  146,	130,  116,  103,  92,
	82,   73,   65,	  58,	52,   46,   41,	  37,
	33,   29,   26,	  23,	21,   18,   16,	  15,
	13,   12,   10,	  9,	8,    7,    7,	  6,
	5,    5,    4,	  4,	3
};

static bool ar9003_hw_is_aic_enabled(struct ath_hw *ah)
{
	struct ath9k_hw_mci *mci_hw = &ah->btcoex_hw.mci;

	/*
	 * Disable AIC for now, until we have all the
	 * HW code and the driver-layer support ready.
	 */
	return false;

	if (mci_hw->config & ATH_MCI_CONFIG_DISABLE_AIC)
		return false;

	return true;
}

static int16_t ar9003_aic_find_valid(bool *cal_sram_valid,
				     bool dir, u8 index)
{
	int16_t i;

	if (dir) {
		for (i = index + 1; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
			if (cal_sram_valid[i])
				break;
		}
	} else {
		for (i = index - 1; i >= 0; i--) {
			if (cal_sram_valid[i])
				break;
		}
	}

	if ((i >= ATH_AIC_MAX_BT_CHANNEL) || (i < 0))
		i = -1;

	return i;
}

/*
 * type 0: aic_lin_table, 1: com_att_db_table
 */
static int16_t ar9003_aic_find_index(u8 type, int16_t value)
{
	int16_t i = -1;

	if (type == 0) {
		for (i = ATH_AIC_MAX_AIC_LIN_TABLE - 1; i >= 0; i--) {
			if (aic_lin_table[i] >= value)
				break;
		}
	} else if (type == 1) {
		for (i = 0; i < ATH_AIC_MAX_COM_ATT_DB_TABLE; i++) {
			if (com_att_db_table[i] > value) {
				i--;
				break;
			}
		}

		if (i >= ATH_AIC_MAX_COM_ATT_DB_TABLE)
			i = -1;
	}

	return i;
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

static u8 ar9003_aic_cal_start(struct ath_hw *ah, u8 min_valid_count)
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

	return aic->aic_cal_state;
}

static bool ar9003_aic_cal_post_process(struct ath_hw *ah)
{
	struct ath9k_hw_aic *aic = &ah->btcoex_hw.aic;
	bool cal_sram_valid[ATH_AIC_MAX_BT_CHANNEL];
	struct ath_aic_out_info aic_sram[ATH_AIC_MAX_BT_CHANNEL];
	u32 dir_path_gain_idx, quad_path_gain_idx, value;
	u32 fixed_com_att_db;
	int8_t dir_path_sign, quad_path_sign;
	int16_t i;
	bool ret = true;

	memset(&cal_sram_valid, 0, sizeof(cal_sram_valid));
	memset(&aic_sram, 0, sizeof(aic_sram));

	for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
		struct ath_aic_sram_info sram;
		value = aic->aic_sram[i];

		cal_sram_valid[i] = sram.valid =
			MS(value, AR_PHY_AIC_SRAM_VALID);
		sram.rot_quad_att_db =
			MS(value, AR_PHY_AIC_SRAM_ROT_QUAD_ATT_DB);
		sram.vga_quad_sign =
			MS(value, AR_PHY_AIC_SRAM_VGA_QUAD_SIGN);
		sram.rot_dir_att_db =
			MS(value, AR_PHY_AIC_SRAM_ROT_DIR_ATT_DB);
		sram.vga_dir_sign =
			MS(value, AR_PHY_AIC_SRAM_VGA_DIR_SIGN);
		sram.com_att_6db =
			MS(value, AR_PHY_AIC_SRAM_COM_ATT_6DB);

		if (sram.valid) {
			dir_path_gain_idx = sram.rot_dir_att_db +
				com_att_db_table[sram.com_att_6db];
			quad_path_gain_idx = sram.rot_quad_att_db +
				com_att_db_table[sram.com_att_6db];

			dir_path_sign = (sram.vga_dir_sign) ? 1 : -1;
			quad_path_sign = (sram.vga_quad_sign) ? 1 : -1;

			aic_sram[i].dir_path_gain_lin = dir_path_sign *
				aic_lin_table[dir_path_gain_idx];
			aic_sram[i].quad_path_gain_lin = quad_path_sign *
				aic_lin_table[quad_path_gain_idx];
		}
	}

	for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
		int16_t start_idx, end_idx;

		if (cal_sram_valid[i])
			continue;

		start_idx = ar9003_aic_find_valid(cal_sram_valid, 0, i);
		end_idx = ar9003_aic_find_valid(cal_sram_valid, 1, i);

		if (start_idx < 0) {
			/* extrapolation */
			start_idx = end_idx;
			end_idx = ar9003_aic_find_valid(cal_sram_valid, 1, start_idx);

			if (end_idx < 0) {
				ret = false;
				break;
			}

			aic_sram[i].dir_path_gain_lin =
				((aic_sram[start_idx].dir_path_gain_lin -
				  aic_sram[end_idx].dir_path_gain_lin) *
				 (start_idx - i) + ((end_idx - i) >> 1)) /
				(end_idx - i) +
				aic_sram[start_idx].dir_path_gain_lin;
			aic_sram[i].quad_path_gain_lin =
				((aic_sram[start_idx].quad_path_gain_lin -
				  aic_sram[end_idx].quad_path_gain_lin) *
				 (start_idx - i) + ((end_idx - i) >> 1)) /
				(end_idx - i) +
				aic_sram[start_idx].quad_path_gain_lin;
		}

		if (end_idx < 0) {
			/* extrapolation */
			end_idx = ar9003_aic_find_valid(cal_sram_valid, 0, start_idx);

			if (end_idx < 0) {
				ret = false;
				break;
			}

			aic_sram[i].dir_path_gain_lin =
				((aic_sram[start_idx].dir_path_gain_lin -
				  aic_sram[end_idx].dir_path_gain_lin) *
				 (i - start_idx) + ((start_idx - end_idx) >> 1)) /
				(start_idx - end_idx) +
				aic_sram[start_idx].dir_path_gain_lin;
			aic_sram[i].quad_path_gain_lin =
				((aic_sram[start_idx].quad_path_gain_lin -
				  aic_sram[end_idx].quad_path_gain_lin) *
				 (i - start_idx) + ((start_idx - end_idx) >> 1)) /
				(start_idx - end_idx) +
				aic_sram[start_idx].quad_path_gain_lin;

		} else if (start_idx >= 0){
			/* interpolation */
			aic_sram[i].dir_path_gain_lin =
				(((end_idx - i) * aic_sram[start_idx].dir_path_gain_lin) +
				 ((i - start_idx) * aic_sram[end_idx].dir_path_gain_lin) +
				 ((end_idx - start_idx) >> 1)) /
				(end_idx - start_idx);
			aic_sram[i].quad_path_gain_lin =
				(((end_idx - i) * aic_sram[start_idx].quad_path_gain_lin) +
				 ((i - start_idx) * aic_sram[end_idx].quad_path_gain_lin) +
				 ((end_idx - start_idx) >> 1))/
				(end_idx - start_idx);
		}
	}

	/* From dir/quad_path_gain_lin to sram. */
	i = ar9003_aic_find_valid(cal_sram_valid, 1, 0);
	if (i < 0) {
		i = 0;
		ret = false;
	}
	fixed_com_att_db = com_att_db_table[MS(aic->aic_sram[i],
					    AR_PHY_AIC_SRAM_COM_ATT_6DB)];

	for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
		int16_t rot_dir_path_att_db, rot_quad_path_att_db;
		struct ath_aic_sram_info sram;

		sram.vga_dir_sign =
			(aic_sram[i].dir_path_gain_lin >= 0) ? 1 : 0;
		sram.vga_quad_sign =
			(aic_sram[i].quad_path_gain_lin >= 0) ? 1 : 0;

		rot_dir_path_att_db =
			ar9003_aic_find_index(0, abs(aic_sram[i].dir_path_gain_lin)) -
			fixed_com_att_db;
		rot_quad_path_att_db =
			ar9003_aic_find_index(0, abs(aic_sram[i].quad_path_gain_lin)) -
			fixed_com_att_db;

		sram.com_att_6db =
			ar9003_aic_find_index(1, fixed_com_att_db);

		sram.valid = true;

		sram.rot_dir_att_db =
			clamp(rot_dir_path_att_db, (int16_t)ATH_AIC_MIN_ROT_DIR_ATT_DB,
			      ATH_AIC_MAX_ROT_DIR_ATT_DB);
		sram.rot_quad_att_db =
			clamp(rot_quad_path_att_db, (int16_t)ATH_AIC_MIN_ROT_QUAD_ATT_DB,
			      ATH_AIC_MAX_ROT_QUAD_ATT_DB);

		aic->aic_sram[i] = (SM(sram.vga_dir_sign,
				       AR_PHY_AIC_SRAM_VGA_DIR_SIGN) |
				    SM(sram.vga_quad_sign,
				       AR_PHY_AIC_SRAM_VGA_QUAD_SIGN) |
				    SM(sram.com_att_6db,
				       AR_PHY_AIC_SRAM_COM_ATT_6DB) |
				    SM(sram.valid,
				       AR_PHY_AIC_SRAM_VALID) |
				    SM(sram.rot_dir_att_db,
				       AR_PHY_AIC_SRAM_ROT_DIR_ATT_DB) |
				    SM(sram.rot_quad_att_db,
				       AR_PHY_AIC_SRAM_ROT_QUAD_ATT_DB));
	}

	return ret;
}

static void ar9003_aic_cal_done(struct ath_hw *ah)
{
	struct ath9k_hw_aic *aic = &ah->btcoex_hw.aic;

	/* Disable AIC reference signal in BT modem. */
	REG_WRITE(ah, ATH_AIC_BT_JUPITER_CTRL,
		  (REG_READ(ah, ATH_AIC_BT_JUPITER_CTRL) &
		   ~ATH_AIC_BT_AIC_ENABLE));

	if (ar9003_aic_cal_post_process(ah))
		aic->aic_cal_state = AIC_CAL_STATE_DONE;
	else
		aic->aic_cal_state = AIC_CAL_STATE_ERROR;
}

static u8 ar9003_aic_cal_continue(struct ath_hw *ah, bool cal_once)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_hw_mci *mci_hw = &ah->btcoex_hw.mci;
	struct ath9k_hw_aic *aic = &ah->btcoex_hw.aic;
	int i, num_chan;

	num_chan = MS(mci_hw->config, ATH_MCI_CONFIG_AIC_CAL_NUM_CHAN);

	if (!num_chan) {
		aic->aic_cal_state = AIC_CAL_STATE_ERROR;
		return aic->aic_cal_state;
	}

	if (cal_once) {
		for (i = 0; i < 10000; i++) {
			if ((REG_READ(ah, AR_PHY_AIC_CTRL_0_B1) &
			     AR_PHY_AIC_CAL_ENABLE) == 0)
				break;

			udelay(100);
		}
	}

	/*
	 * Use AR_PHY_AIC_CAL_ENABLE bit instead of AR_PHY_AIC_CAL_DONE.
	 * Sometimes CAL_DONE bit is not asserted.
	 */
	if ((REG_READ(ah, AR_PHY_AIC_CTRL_0_B1) &
	     AR_PHY_AIC_CAL_ENABLE) != 0) {
		ath_dbg(common, MCI, "AIC cal is not done after 40ms");
		goto exit;
	}

	REG_WRITE(ah, AR_PHY_AIC_SRAM_ADDR_B1,
		  (ATH_AIC_SRAM_CAL_OFFSET | ATH_AIC_SRAM_AUTO_INCREMENT));

	for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
		u32 value;

		value = REG_READ(ah, AR_PHY_AIC_SRAM_DATA_B1);

		if (value & 0x01) {
			if (aic->aic_sram[i] == 0)
				aic->aic_caled_chan++;

			aic->aic_sram[i] = value;

			if (!cal_once)
				break;
		}
	}

	if ((aic->aic_caled_chan >= num_chan) || cal_once) {
		ar9003_aic_cal_done(ah);
	} else {
		/* Start calibration */
		REG_CLR_BIT(ah, AR_PHY_AIC_CTRL_0_B1, AR_PHY_AIC_CAL_ENABLE);
		REG_SET_BIT(ah, AR_PHY_AIC_CTRL_0_B1,
			    AR_PHY_AIC_CAL_CH_VALID_RESET);
		REG_SET_BIT(ah, AR_PHY_AIC_CTRL_0_B1, AR_PHY_AIC_CAL_ENABLE);
	}
exit:
	return aic->aic_cal_state;

}

u8 ar9003_aic_calibration(struct ath_hw *ah)
{
	struct ath9k_hw_aic *aic = &ah->btcoex_hw.aic;
	u8 cal_ret = AIC_CAL_STATE_ERROR;

	switch (aic->aic_cal_state) {
	case AIC_CAL_STATE_IDLE:
		cal_ret = ar9003_aic_cal_start(ah, 1);
		break;
	case AIC_CAL_STATE_STARTED:
		cal_ret = ar9003_aic_cal_continue(ah, false);
		break;
	case AIC_CAL_STATE_DONE:
		cal_ret = AIC_CAL_STATE_DONE;
		break;
	default:
		break;
	}

	return cal_ret;
}

u8 ar9003_aic_start_normal(struct ath_hw *ah)
{
	struct ath9k_hw_aic *aic = &ah->btcoex_hw.aic;
	int16_t i;

	if (aic->aic_cal_state != AIC_CAL_STATE_DONE)
		return 1;

	ar9003_aic_gain_table(ah);

	REG_WRITE(ah, AR_PHY_AIC_SRAM_ADDR_B1, ATH_AIC_SRAM_AUTO_INCREMENT);

	for (i = 0; i < ATH_AIC_MAX_BT_CHANNEL; i++) {
		REG_WRITE(ah, AR_PHY_AIC_SRAM_DATA_B1, aic->aic_sram[i]);
	}

	/* FIXME: Replace these with proper register names */
	REG_WRITE(ah, 0xa6b0, 0x80);
	REG_WRITE(ah, 0xa6b4, 0x5b2df0);
	REG_WRITE(ah, 0xa6b8, 0x10762cc8);
	REG_WRITE(ah, 0xa6bc, 0x1219a4b);
	REG_WRITE(ah, 0xa6c0, 0x1e01);
	REG_WRITE(ah, 0xb6b4, 0xf0);
	REG_WRITE(ah, 0xb6c0, 0x1e01);
	REG_WRITE(ah, 0xb6b0, 0x81);
	REG_WRITE(ah, AR_PHY_65NM_CH1_RXTX4, 0x40000000);

	aic->aic_enabled = true;

	return 0;
}

u8 ar9003_aic_cal_reset(struct ath_hw *ah)
{
	struct ath9k_hw_aic *aic = &ah->btcoex_hw.aic;

	aic->aic_cal_state = AIC_CAL_STATE_IDLE;
	return aic->aic_cal_state;
}

u8 ar9003_aic_calibration_single(struct ath_hw *ah)
{
	struct ath9k_hw_mci *mci_hw = &ah->btcoex_hw.mci;
	u8 cal_ret;
	int num_chan;

	num_chan = MS(mci_hw->config, ATH_MCI_CONFIG_AIC_CAL_NUM_CHAN);

	(void) ar9003_aic_cal_start(ah, num_chan);
	cal_ret = ar9003_aic_cal_continue(ah, true);

	return cal_ret;
}

void ar9003_hw_attach_aic_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);

	priv_ops->is_aic_enabled = ar9003_hw_is_aic_enabled;
}
