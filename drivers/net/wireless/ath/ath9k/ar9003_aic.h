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

#ifndef AR9003_AIC_H
#define AR9003_AIC_H

#define ATH_AIC_MAX_COM_ATT_DB_TABLE    6
#define ATH_AIC_MAX_AIC_LIN_TABLE       69
#define ATH_AIC_MIN_ROT_DIR_ATT_DB      0
#define ATH_AIC_MIN_ROT_QUAD_ATT_DB     0
#define ATH_AIC_MAX_ROT_DIR_ATT_DB      37
#define ATH_AIC_MAX_ROT_QUAD_ATT_DB     37
#define ATH_AIC_SRAM_AUTO_INCREMENT     0x80000000
#define ATH_AIC_SRAM_GAIN_TABLE_OFFSET  0x280
#define ATH_AIC_SRAM_CAL_OFFSET         0x140
#define ATH_AIC_SRAM_OFFSET             0x00
#define ATH_AIC_MEAS_MAG_THRESH         20
#define ATH_AIC_BT_JUPITER_CTRL         0x66820
#define ATH_AIC_BT_AIC_ENABLE           0x02

enum aic_cal_state {
	AIC_CAL_STATE_IDLE = 0,
	AIC_CAL_STATE_STARTED,
	AIC_CAL_STATE_DONE,
	AIC_CAL_STATE_ERROR
};

struct ath_aic_sram_info {
	bool valid:1;
	bool vga_quad_sign:1;
	bool vga_dir_sign:1;
	u8 rot_quad_att_db;
	u8 rot_dir_att_db;
	u8 com_att_6db;
};

struct ath_aic_out_info {
	int16_t dir_path_gain_lin;
	int16_t quad_path_gain_lin;
	struct ath_aic_sram_info sram;
};

u8 ar9003_aic_calibration_single(struct ath_hw *ah);

#endif /* AR9003_AIC_H */
