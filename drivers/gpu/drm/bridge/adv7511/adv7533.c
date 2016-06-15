/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "adv7511.h"

static const struct reg_sequence adv7533_fixed_registers[] = {
	{ 0x16, 0x20 },
	{ 0x9a, 0xe0 },
	{ 0xba, 0x70 },
	{ 0xde, 0x82 },
	{ 0xe4, 0x40 },
	{ 0xe5, 0x80 },
};

static const struct reg_sequence adv7533_cec_fixed_registers[] = {
	{ 0x15, 0xd0 },
	{ 0x17, 0xd0 },
	{ 0x24, 0x20 },
	{ 0x57, 0x11 },
};

static const struct regmap_config adv7533_cec_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = 0xff,
	.cache_type = REGCACHE_RBTREE,
};

void adv7533_dsi_power_on(struct adv7511 *adv)
{
	/* set number of dsi lanes (hardcoded to 4 for now) */
	regmap_write(adv->regmap_cec, 0x1c, 4 << 4);
	/* disable internal timing generator */
	regmap_write(adv->regmap_cec, 0x27, 0x0b);
	/* enable hdmi */
	regmap_write(adv->regmap_cec, 0x03, 0x89);
	/* disable test mode */
	regmap_write(adv->regmap_cec, 0x55, 0x00);

	regmap_register_patch(adv->regmap_cec, adv7533_cec_fixed_registers,
			      ARRAY_SIZE(adv7533_cec_fixed_registers));
}

void adv7533_dsi_power_off(struct adv7511 *adv)
{
	/* disable hdmi */
	regmap_write(adv->regmap_cec, 0x03, 0x0b);
}

int adv7533_patch_registers(struct adv7511 *adv)
{
	return regmap_register_patch(adv->regmap,
				     adv7533_fixed_registers,
				     ARRAY_SIZE(adv7533_fixed_registers));
}

void adv7533_uninit_cec(struct adv7511 *adv)
{
	i2c_unregister_device(adv->i2c_cec);
}

static const int cec_i2c_addr = 0x78;

int adv7533_init_cec(struct adv7511 *adv)
{
	int ret;

	adv->i2c_cec = i2c_new_dummy(adv->i2c_main->adapter, cec_i2c_addr >> 1);
	if (!adv->i2c_cec)
		return -ENOMEM;

	adv->regmap_cec = devm_regmap_init_i2c(adv->i2c_cec,
					&adv7533_cec_regmap_config);
	if (IS_ERR(adv->regmap_cec)) {
		ret = PTR_ERR(adv->regmap_cec);
		goto err;
	}

	ret = regmap_register_patch(adv->regmap_cec,
				    adv7533_cec_fixed_registers,
				    ARRAY_SIZE(adv7533_cec_fixed_registers));
	if (ret)
		goto err;

	return 0;
err:
	adv7533_uninit_cec(adv);
	return ret;
}
