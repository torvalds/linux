// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, Linaro Limited
 */

#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "tsens.h"

/* ----- SROT ------ */
#define SROT_HW_VER_OFF	0x0000
#define SROT_CTRL_OFF		0x0004

/* ----- TM ------ */
#define TM_INT_EN_OFF				0x0000
#define TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF	0x0004
#define TM_Sn_STATUS_OFF			0x0044
#define TM_TRDY_OFF				0x0084
#define TM_HIGH_LOW_INT_STATUS_OFF		0x0088
#define TM_HIGH_LOW_Sn_INT_THRESHOLD_OFF	0x0090

struct tsens_legacy_calibration_format tsens_qcs404_nvmem = {
	.base_len = 8,
	.base_shift = 2,
	.sp_len = 6,
	.mode = { 4, 0 },
	.invalid = { 4, 2 },
	.base = { { 4, 3 }, { 4, 11 } },
	.sp = {
		{ { 0, 0 }, { 0, 6 } },
		{ { 0, 12 }, { 0, 18 } },
		{ { 0, 24 }, { 0, 30 } },
		{ { 1, 4 },  { 1, 10 } },
		{ { 1, 16 }, { 1, 22 } },
		{ { 2, 0 },  { 2, 6 } },
		{ { 2, 12 }, { 2, 18 } },
		{ { 2, 24 }, { 2, 30 } },
		{ { 3, 4 },  { 3, 10 } },
		{ { 3, 16 }, { 3, 22 } },
	},
};

struct tsens_legacy_calibration_format tsens_8976_nvmem = {
	.base_len = 8,
	.base_shift = 2,
	.sp_len = 6,
	.mode = { 4, 0 },
	.invalid = { 4, 2 },
	.base = { { 0, 0 }, { 2, 8 } },
	.sp = {
		{ { 0, 8 },  { 0, 14 } },
		{ { 0, 20 }, { 0, 26 } },
		{ { 1, 0 },  { 1, 6 } },
		{ { 1, 12 }, { 1, 18 } },
		{ { 2, 8 },  { 2, 14 } },
		{ { 2, 20 }, { 2, 26 } },
		{ { 3, 0 },  { 3, 6 } },
		{ { 3, 12 }, { 3, 18 } },
		{ { 4, 2 },  { 4, 9 } },
		{ { 4, 14 }, { 4, 21 } },
		{ { 4, 26 }, { 5, 1 } },
	},
};

static int calibrate_v1(struct tsens_priv *priv)
{
	u32 p1[10], p2[10];
	u32 *qfprom_cdata;
	int mode, ret;

	ret = tsens_calibrate_common(priv);
	if (!ret)
		return 0;

	qfprom_cdata = (u32 *)qfprom_read(priv->dev, "calib");
	if (IS_ERR(qfprom_cdata))
		return PTR_ERR(qfprom_cdata);

	mode = tsens_read_calibration_legacy(priv, &tsens_qcs404_nvmem,
					     p1, p2,
					     qfprom_cdata, NULL);

	compute_intercept_slope(priv, p1, p2, mode);
	kfree(qfprom_cdata);

	return 0;
}

/* v1.x: msm8956,8976,qcs404,405 */

static struct tsens_features tsens_v1_feat = {
	.ver_major	= VER_1_X,
	.crit_int	= 0,
	.combo_int	= 0,
	.adc		= 1,
	.srot_split	= 1,
	.max_sensors	= 11,
	.trip_min_temp	= -40000,
	.trip_max_temp	= 120000,
};

static const struct reg_field tsens_v1_regfields[MAX_REGFIELDS] = {
	/* ----- SROT ------ */
	/* VERSION */
	[VER_MAJOR] = REG_FIELD(SROT_HW_VER_OFF, 28, 31),
	[VER_MINOR] = REG_FIELD(SROT_HW_VER_OFF, 16, 27),
	[VER_STEP]  = REG_FIELD(SROT_HW_VER_OFF,  0, 15),
	/* CTRL_OFFSET */
	[TSENS_EN]     = REG_FIELD(SROT_CTRL_OFF, 0,  0),
	[TSENS_SW_RST] = REG_FIELD(SROT_CTRL_OFF, 1,  1),
	[SENSOR_EN]    = REG_FIELD(SROT_CTRL_OFF, 3, 13),

	/* ----- TM ------ */
	/* INTERRUPT ENABLE */
	[INT_EN]     = REG_FIELD(TM_INT_EN_OFF, 0, 0),

	/* UPPER/LOWER TEMPERATURE THRESHOLDS */
	REG_FIELD_FOR_EACH_SENSOR11(LOW_THRESH,    TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF,  0,  9),
	REG_FIELD_FOR_EACH_SENSOR11(UP_THRESH,     TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF, 10, 19),

	/* UPPER/LOWER INTERRUPTS [CLEAR/STATUS] */
	REG_FIELD_FOR_EACH_SENSOR11(LOW_INT_CLEAR, TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF, 20, 20),
	REG_FIELD_FOR_EACH_SENSOR11(UP_INT_CLEAR,  TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF, 21, 21),
	[LOW_INT_STATUS_0] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  0,  0),
	[LOW_INT_STATUS_1] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  1,  1),
	[LOW_INT_STATUS_2] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  2,  2),
	[LOW_INT_STATUS_3] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  3,  3),
	[LOW_INT_STATUS_4] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  4,  4),
	[LOW_INT_STATUS_5] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  5,  5),
	[LOW_INT_STATUS_6] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  6,  6),
	[LOW_INT_STATUS_7] = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  7,  7),
	[UP_INT_STATUS_0]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  8,  8),
	[UP_INT_STATUS_1]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF,  9,  9),
	[UP_INT_STATUS_2]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 10, 10),
	[UP_INT_STATUS_3]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 11, 11),
	[UP_INT_STATUS_4]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 12, 12),
	[UP_INT_STATUS_5]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 13, 13),
	[UP_INT_STATUS_6]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 14, 14),
	[UP_INT_STATUS_7]  = REG_FIELD(TM_HIGH_LOW_INT_STATUS_OFF, 15, 15),

	/* NO CRITICAL INTERRUPT SUPPORT on v1 */

	/* Sn_STATUS */
	REG_FIELD_FOR_EACH_SENSOR11(LAST_TEMP,    TM_Sn_STATUS_OFF,  0,  9),
	REG_FIELD_FOR_EACH_SENSOR11(VALID,        TM_Sn_STATUS_OFF, 14, 14),
	/* xxx_STATUS bits: 1 == threshold violated */
	REG_FIELD_FOR_EACH_SENSOR11(MIN_STATUS,   TM_Sn_STATUS_OFF, 10, 10),
	REG_FIELD_FOR_EACH_SENSOR11(LOWER_STATUS, TM_Sn_STATUS_OFF, 11, 11),
	REG_FIELD_FOR_EACH_SENSOR11(UPPER_STATUS, TM_Sn_STATUS_OFF, 12, 12),
	/* No CRITICAL field on v1.x */
	REG_FIELD_FOR_EACH_SENSOR11(MAX_STATUS,   TM_Sn_STATUS_OFF, 13, 13),

	/* TRDY: 1=ready, 0=in progress */
	[TRDY] = REG_FIELD(TM_TRDY_OFF, 0, 0),
};

static int __init init_8956(struct tsens_priv *priv) {
	priv->sensor[0].slope = 3313;
	priv->sensor[1].slope = 3275;
	priv->sensor[2].slope = 3320;
	priv->sensor[3].slope = 3246;
	priv->sensor[4].slope = 3279;
	priv->sensor[5].slope = 3257;
	priv->sensor[6].slope = 3234;
	priv->sensor[7].slope = 3269;
	priv->sensor[8].slope = 3255;
	priv->sensor[9].slope = 3239;
	priv->sensor[10].slope = 3286;

	return init_common(priv);
}

static const struct tsens_ops ops_generic_v1 = {
	.init		= init_common,
	.calibrate	= calibrate_v1,
	.get_temp	= get_temp_tsens_valid,
};

struct tsens_plat_data data_tsens_v1 = {
	.ops		= &ops_generic_v1,
	.feat		= &tsens_v1_feat,
	.fields	= tsens_v1_regfields,
};

static const struct tsens_ops ops_8956 = {
	.init		= init_8956,
	.calibrate	= tsens_calibrate_common,
	.get_temp	= get_temp_tsens_valid,
};

struct tsens_plat_data data_8956 = {
	.num_sensors	= 11,
	.ops		= &ops_8956,
	.feat		= &tsens_v1_feat,
	.fields		= tsens_v1_regfields,
};

static const struct tsens_ops ops_8976 = {
	.init		= init_common,
	.calibrate	= tsens_calibrate_common,
	.get_temp	= get_temp_tsens_valid,
};

struct tsens_plat_data data_8976 = {
	.num_sensors	= 11,
	.ops		= &ops_8976,
	.feat		= &tsens_v1_feat,
	.fields		= tsens_v1_regfields,
};
