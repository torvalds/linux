// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include "tsens.h"

/* ----- SROT ------ */
#define SROT_CTRL_OFF 0x0000

/* ----- TM ------ */
#define TM_INT_EN_OFF				0x0000
#define TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF	0x0004
#define TM_Sn_STATUS_OFF			0x0030
#define TM_TRDY_OFF				0x005c

/* extra data for 8974 */
#define BKP_SEL			0x3
#define BKP_REDUN_SEL		0xe0000000

#define BIT_APPEND		0x3

struct tsens_legacy_calibration_format tsens_8916_nvmem = {
	.base_len = 7,
	.base_shift = 3,
	.sp_len = 5,
	.mode = { 0, 29, 1 },
	.invalid = { 0, 31, 1 },
	.base = { { 0, 0 }, { 1, 25 } },
	.sp = {
		{ { 0, 7 },  { 0, 12 } },
		{ { 0, 17 }, { 0, 22 } },
		{ { 0, 27 }, { 1, 0 } },
		{ { 1, 5 },  { 1, 10 } },
		{ { 1, 15 }, { 1, 20 } },
	},
};

struct tsens_legacy_calibration_format tsens_8974_nvmem = {
	.base_len = 8,
	.base_shift = 2,
	.sp_len = 6,
	.mode = { 1, 30 },
	.invalid = { 3, 30 },
	.base = { { 0, 0 }, { 2, 12 } },
	.sp = {
		{ { 0, 8 },  { 2, 20 } },
		{ { 0, 14 }, { 2, 26 } },
		{ { 0, 20 }, { 3, 0 } },
		{ { 0, 26 }, { 3, 6 } },
		{ { 1, 0 },  { 3, 12 } },
		{ { 1, 6 },  { 3, 18 } },
		{ { 1, 12 }, { 3, 24 } },
		{ { 1, 18 }, { 4, 0 } },
		{ { 1, 24 }, { 4, 6 } },
		{ { 2, 0 },  { 4, 12 } },
		{ { 2, 6 },  { 4, 18 } },
	},
};

struct tsens_legacy_calibration_format tsens_8974_backup_nvmem = {
	.base_len = 8,
	.base_shift = 2,
	.sp_len = 6,
	.mode = { 4, 30, 1 },
	.invalid = { 5, 30, 1 },
	.base = { { 0, 0 }, { 2, 18 } },
	.sp = {
		{ { 0, 8 },  { 2, 26 } },
		{ { 0, 14 }, { 3, 0 } },
		{ { 0, 20 }, { 3, 6 } },
		{ { 0, 26 }, { 3, 12 } },
		{ { 1, 0 },  { 3, 18 } },
		{ { 1, 6 },  { 3, 24, 1 } },
		{ { 1, 12 }, { 4, 0, 1 } },
		{ { 1, 18 }, { 4, 6, 1 } },
		{ { 2, 0 },  { 4, 12, 1 } },
		{ { 2, 6 },  { 4, 18, 1 } },
		{ { 2, 12 }, { 4, 24, 1 } },
	},
};

static int calibrate_8916(struct tsens_priv *priv)
{
	u32 p1[5], p2[5];
	u32 *qfprom_cdata, *qfprom_csel;
	int mode, ret;

	ret = tsens_calibrate_nvmem(priv, 3);
	if (!ret)
		return 0;

	qfprom_cdata = (u32 *)qfprom_read(priv->dev, "calib");
	if (IS_ERR(qfprom_cdata))
		return PTR_ERR(qfprom_cdata);

	qfprom_csel = (u32 *)qfprom_read(priv->dev, "calib_sel");
	if (IS_ERR(qfprom_csel)) {
		kfree(qfprom_cdata);
		return PTR_ERR(qfprom_csel);
	}

	mode = tsens_read_calibration_legacy(priv, &tsens_8916_nvmem,
					     p1, p2,
					     qfprom_cdata, qfprom_csel);

	compute_intercept_slope(priv, p1, p2, mode);
	kfree(qfprom_cdata);
	kfree(qfprom_csel);

	return 0;
}

static void fixup_8974_points(int mode, u32 *p1, u32 *p2)
{
	int i;

	if (mode == NO_PT_CALIB) {
		p1[0] += 2;
		p1[1] += 9;
		p1[2] += 3;
		p1[3] += 9;
		p1[4] += 5;
		p1[5] += 9;
		p1[6] += 7;
		p1[7] += 10;
		p1[8] += 8;
		p1[9] += 9;
		p1[10] += 8;
	} else {
		for (i = 0; i < 11; i++) {
			/*
			 * ONE_PT_CALIB requires using addition here instead of
			 * using OR operation.
			 */
			p1[i] += BIT_APPEND;
			p2[i] += BIT_APPEND;
		}
	}

}

static int calibrate_8974_nvmem(struct tsens_priv *priv)
{
	u32 p1[11], p2[11];
	u32 backup;
	int ret, mode;

	ret = nvmem_cell_read_variable_le_u32(priv->dev, "use_backup", &backup);
	if (ret == -ENOENT)
		dev_warn(priv->dev, "Please migrate to separate nvmem cells for calibration data\n");
	if (ret < 0)
		return ret;

	mode = tsens_read_calibration(priv, 2, p1, p2, backup == BKP_SEL);
	if (mode < 0)
		return mode;

	fixup_8974_points(mode, p1, p2);

	compute_intercept_slope(priv, p1, p2, mode);

	return 0;
}

static int calibrate_8974(struct tsens_priv *priv)
{
	u32 p1[11], p2[11];
	u32 *calib, *bkp;
	u32 calib_redun_sel;
	int mode, ret;

	ret = calibrate_8974_nvmem(priv);
	if (ret == 0)
		return 0;

	calib = (u32 *)qfprom_read(priv->dev, "calib");
	if (IS_ERR(calib))
		return PTR_ERR(calib);

	bkp = (u32 *)qfprom_read(priv->dev, "calib_backup");
	if (IS_ERR(bkp)) {
		kfree(calib);
		return PTR_ERR(bkp);
	}

	calib_redun_sel = FIELD_GET(BKP_REDUN_SEL, bkp[1]);

	if (calib_redun_sel == BKP_SEL)
		mode = tsens_read_calibration_legacy(priv, &tsens_8974_backup_nvmem,
						     p1, p2,
						     bkp, calib);
	else
		mode = tsens_read_calibration_legacy(priv, &tsens_8974_nvmem,
						     p1, p2,
						     calib, NULL);

	fixup_8974_points(mode, p1, p2);

	compute_intercept_slope(priv, p1, p2, mode);
	kfree(calib);
	kfree(bkp);

	return 0;
}

static int __init init_8226(struct tsens_priv *priv)
{
	priv->sensor[0].slope = 2901;
	priv->sensor[1].slope = 2846;
	priv->sensor[2].slope = 3038;
	priv->sensor[3].slope = 2955;
	priv->sensor[4].slope = 2901;
	priv->sensor[5].slope = 2846;

	return init_common(priv);
}

static int __init init_8939(struct tsens_priv *priv) {
	priv->sensor[0].slope = 2911;
	priv->sensor[1].slope = 2789;
	priv->sensor[2].slope = 2906;
	priv->sensor[3].slope = 2763;
	priv->sensor[4].slope = 2922;
	priv->sensor[5].slope = 2867;
	priv->sensor[6].slope = 2833;
	priv->sensor[7].slope = 2838;
	priv->sensor[8].slope = 2840;
	/* priv->sensor[9].slope = 2852; */

	return init_common(priv);
}

static int __init init_9607(struct tsens_priv *priv)
{
	int i;

	for (i = 0; i < priv->num_sensors; ++i)
		priv->sensor[i].slope = 3000;

	return init_common(priv);
}

/* v0.1: 8226, 8916, 8939, 8974, 9607 */

static struct tsens_features tsens_v0_1_feat = {
	.ver_major	= VER_0_1,
	.crit_int	= 0,
	.combo_int	= 0,
	.adc		= 1,
	.srot_split	= 1,
	.max_sensors	= 11,
	.trip_min_temp	= -40000,
	.trip_max_temp	= 120000,
};

static const struct reg_field tsens_v0_1_regfields[MAX_REGFIELDS] = {
	/* ----- SROT ------ */
	/* No VERSION information */

	/* CTRL_OFFSET */
	[TSENS_EN]     = REG_FIELD(SROT_CTRL_OFF, 0,  0),
	[TSENS_SW_RST] = REG_FIELD(SROT_CTRL_OFF, 1,  1),

	/* ----- TM ------ */
	/* INTERRUPT ENABLE */
	[INT_EN] = REG_FIELD(TM_INT_EN_OFF, 0, 0),

	/* UPPER/LOWER TEMPERATURE THRESHOLDS */
	REG_FIELD_FOR_EACH_SENSOR11(LOW_THRESH,    TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF,  0,  9),
	REG_FIELD_FOR_EACH_SENSOR11(UP_THRESH,     TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF, 10, 19),

	/* UPPER/LOWER INTERRUPTS [CLEAR/STATUS] */
	REG_FIELD_FOR_EACH_SENSOR11(LOW_INT_CLEAR, TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF, 20, 20),
	REG_FIELD_FOR_EACH_SENSOR11(UP_INT_CLEAR,  TM_Sn_UPPER_LOWER_STATUS_CTRL_OFF, 21, 21),

	/* NO CRITICAL INTERRUPT SUPPORT on v0.1 */

	/* Sn_STATUS */
	REG_FIELD_FOR_EACH_SENSOR11(LAST_TEMP,    TM_Sn_STATUS_OFF,  0,  9),
	/* No VALID field on v0.1 */
	/* xxx_STATUS bits: 1 == threshold violated */
	REG_FIELD_FOR_EACH_SENSOR11(MIN_STATUS,   TM_Sn_STATUS_OFF, 10, 10),
	REG_FIELD_FOR_EACH_SENSOR11(LOWER_STATUS, TM_Sn_STATUS_OFF, 11, 11),
	REG_FIELD_FOR_EACH_SENSOR11(UPPER_STATUS, TM_Sn_STATUS_OFF, 12, 12),
	/* No CRITICAL field on v0.1 */
	REG_FIELD_FOR_EACH_SENSOR11(MAX_STATUS,   TM_Sn_STATUS_OFF, 13, 13),

	/* TRDY: 1=ready, 0=in progress */
	[TRDY] = REG_FIELD(TM_TRDY_OFF, 0, 0),
};

static const struct tsens_ops ops_v0_1 = {
	.init		= init_common,
	.calibrate	= tsens_calibrate_common,
	.get_temp	= get_temp_common,
};

static const struct tsens_ops ops_8226 = {
	.init		= init_8226,
	.calibrate	= tsens_calibrate_common,
	.get_temp	= get_temp_common,
};

struct tsens_plat_data data_8226 = {
	.num_sensors	= 6,
	.ops		= &ops_8226,
	.feat		= &tsens_v0_1_feat,
	.fields	= tsens_v0_1_regfields,
};

static const struct tsens_ops ops_8916 = {
	.init		= init_common,
	.calibrate	= calibrate_8916,
	.get_temp	= get_temp_common,
};

struct tsens_plat_data data_8916 = {
	.num_sensors	= 5,
	.ops		= &ops_8916,
	.hw_ids		= (unsigned int []){0, 1, 2, 4, 5 },

	.feat		= &tsens_v0_1_feat,
	.fields	= tsens_v0_1_regfields,
};

static const struct tsens_ops ops_8939 = {
	.init		= init_8939,
	.calibrate	= tsens_calibrate_common,
	.get_temp	= get_temp_common,
};

struct tsens_plat_data data_8939 = {
	.num_sensors	= 9,
	.ops		= &ops_8939,
	.hw_ids		= (unsigned int []){ 0, 1, 2, 3, 5, 6, 7, 8, 9, /* 10 */ },

	.feat		= &tsens_v0_1_feat,
	.fields	= tsens_v0_1_regfields,
};

static const struct tsens_ops ops_8974 = {
	.init		= init_common,
	.calibrate	= calibrate_8974,
	.get_temp	= get_temp_common,
};

struct tsens_plat_data data_8974 = {
	.num_sensors	= 11,
	.ops		= &ops_8974,
	.feat		= &tsens_v0_1_feat,
	.fields	= tsens_v0_1_regfields,
};

static const struct tsens_ops ops_9607 = {
	.init		= init_9607,
	.calibrate	= tsens_calibrate_common,
	.get_temp	= get_temp_common,
};

struct tsens_plat_data data_9607 = {
	.num_sensors	= 5,
	.ops		= &ops_9607,
	.feat		= &tsens_v0_1_feat,
	.fields	= tsens_v0_1_regfields,
};
