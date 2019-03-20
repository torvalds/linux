// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Copyright (c) 2018, Linaro Limited
 */

#include <linux/bitops.h>
#include <linux/regmap.h>
#include "tsens.h"

/* ----- SROT ------ */
#define SROT_HW_VER_OFF	0x0000
#define SROT_CTRL_OFF		0x0004

/* ----- TM ------ */
#define TM_INT_EN_OFF			0x0004
#define TM_UPPER_LOWER_INT_STATUS_OFF	0x0008
#define TM_UPPER_LOWER_INT_CLEAR_OFF	0x000c
#define TM_UPPER_LOWER_INT_MASK_OFF	0x0010
#define TM_CRITICAL_INT_STATUS_OFF	0x0014
#define TM_CRITICAL_INT_CLEAR_OFF	0x0018
#define TM_CRITICAL_INT_MASK_OFF	0x001c
#define TM_Sn_UPPER_LOWER_THRESHOLD_OFF 0x0020
#define TM_Sn_CRITICAL_THRESHOLD_OFF	0x0060
#define TM_Sn_STATUS_OFF		0x00a0
#define TM_TRDY_OFF			0x00e4

static int get_temp_tsens_v2(struct tsens_priv *priv, int i, int *temp)
{
	struct tsens_sensor *s = &priv->sensor[i];
	u32 temp_idx = LAST_TEMP_0 + s->hw_id;
	u32 valid_idx = VALID_0 + s->hw_id;
	u32 last_temp = 0, valid, mask;
	int ret;

	ret = regmap_field_read(priv->rf[valid_idx], &valid);
	if (ret)
		return ret;
	while (!valid) {
		/* Valid bit is 0 for 6 AHB clock cycles.
		 * At 19.2MHz, 1 AHB clock is ~60ns.
		 * We should enter this loop very, very rarely.
		 */
		ndelay(400);
		ret = regmap_field_read(priv->rf[valid_idx], &valid);
		if (ret)
			return ret;
	}

	/* Valid bit is set, OK to read the temperature */
	ret = regmap_field_read(priv->rf[temp_idx], &last_temp);
	if (ret)
		return ret;

	mask = GENMASK(priv->fields[LAST_TEMP_0].msb,
		       priv->fields[LAST_TEMP_0].lsb);
	/* Convert temperature from deciCelsius to milliCelsius */
	*temp = sign_extend32(last_temp, fls(mask) - 1) * 100;

	return 0;
}

/* v2.x: 8996, 8998, sdm845 */

static const struct tsens_features tsens_v2_feat = {
	.ver_major	= VER_2_X,
	.crit_int	= 1,
	.adc		= 0,
	.srot_split	= 1,
	.max_sensors	= 16,
};

static const struct reg_field tsens_v2_regfields[MAX_REGFIELDS] = {
	/* ----- SROT ------ */
	/* VERSION */
	[VER_MAJOR] = REG_FIELD(SROT_HW_VER_OFF, 28, 31),
	[VER_MINOR] = REG_FIELD(SROT_HW_VER_OFF, 16, 27),
	[VER_STEP]  = REG_FIELD(SROT_HW_VER_OFF,  0, 15),
	/* CTRL_OFF */
	[TSENS_EN]     = REG_FIELD(SROT_CTRL_OFF,    0,  0),
	[TSENS_SW_RST] = REG_FIELD(SROT_CTRL_OFF,    1,  1),
	[SENSOR_EN]    = REG_FIELD(SROT_CTRL_OFF,    3, 18),

	/* ----- TM ------ */
	/* INTERRUPT ENABLE */
	/* v2 has separate enables for UPPER/LOWER/CRITICAL interrupts */
	[INT_EN]  = REG_FIELD(TM_INT_EN_OFF, 0, 2),

	/* Sn_STATUS */
	REG_FIELD_FOR_EACH_SENSOR16(LAST_TEMP,       TM_Sn_STATUS_OFF,  0,  11),
	REG_FIELD_FOR_EACH_SENSOR16(VALID,           TM_Sn_STATUS_OFF, 21,  21),
	REG_FIELD_FOR_EACH_SENSOR16(MIN_STATUS,      TM_Sn_STATUS_OFF, 16,  16),
	REG_FIELD_FOR_EACH_SENSOR16(LOWER_STATUS,    TM_Sn_STATUS_OFF, 17,  17),
	REG_FIELD_FOR_EACH_SENSOR16(UPPER_STATUS,    TM_Sn_STATUS_OFF, 18,  18),
	REG_FIELD_FOR_EACH_SENSOR16(CRITICAL_STATUS, TM_Sn_STATUS_OFF, 19,  19),
	REG_FIELD_FOR_EACH_SENSOR16(MAX_STATUS,      TM_Sn_STATUS_OFF, 20,  20),

	/* TRDY: 1=ready, 0=in progress */
	[TRDY] = REG_FIELD(TM_TRDY_OFF, 0, 0),
};

static const struct tsens_ops ops_generic_v2 = {
	.init		= init_common,
	.get_temp	= get_temp_tsens_v2,
};

const struct tsens_plat_data data_tsens_v2 = {
	.ops		= &ops_generic_v2,
	.feat		= &tsens_v2_feat,
	.fields	= tsens_v2_regfields,
};

/* Kept around for backward compatibility with old msm8996.dtsi */
const struct tsens_plat_data data_8996 = {
	.num_sensors	= 13,
	.ops		= &ops_generic_v2,
	.feat		= &tsens_v2_feat,
	.fields	= tsens_v2_regfields,
};
