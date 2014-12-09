/*
 * Register map access API - AC'97 support
 *
 * Copyright 2013 Linaro Ltd.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <sound/ac97_codec.h>

bool regmap_ac97_default_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AC97_RESET:
	case AC97_POWERDOWN:
	case AC97_INT_PAGING:
	case AC97_EXTENDED_ID:
	case AC97_EXTENDED_STATUS:
	case AC97_EXTENDED_MID:
	case AC97_EXTENDED_MSTATUS:
	case AC97_GPIO_STATUS:
	case AC97_MISC_AFE:
	case AC97_VENDOR_ID1:
	case AC97_VENDOR_ID2:
	case AC97_CODEC_CLASS_REV:
	case AC97_PCI_SVID:
	case AC97_PCI_SID:
	case AC97_FUNC_SELECT:
	case AC97_FUNC_INFO:
	case AC97_SENSE_INFO:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(regmap_ac97_default_volatile);

static int regmap_ac97_reg_read(void *context, unsigned int reg,
	unsigned int *val)
{
	struct snd_ac97 *ac97 = context;

	*val = ac97->bus->ops->read(ac97, reg);

	return 0;
}

static int regmap_ac97_reg_write(void *context, unsigned int reg,
	unsigned int val)
{
	struct snd_ac97 *ac97 = context;

	ac97->bus->ops->write(ac97, reg, val);

	return 0;
}

static const struct regmap_bus ac97_regmap_bus = {
		.reg_write = regmap_ac97_reg_write,
		.reg_read = regmap_ac97_reg_read,
};

/**
 * regmap_init_ac97(): Initialise AC'97 register map
 *
 * @ac97: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
struct regmap *regmap_init_ac97(struct snd_ac97 *ac97,
				const struct regmap_config *config)
{
	return regmap_init(&ac97->dev, &ac97_regmap_bus, ac97, config);
}
EXPORT_SYMBOL_GPL(regmap_init_ac97);

/**
 * devm_regmap_init_ac97(): Initialise AC'97 register map
 *
 * @ac97: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
struct regmap *devm_regmap_init_ac97(struct snd_ac97 *ac97,
				     const struct regmap_config *config)
{
	return devm_regmap_init(&ac97->dev, &ac97_regmap_bus, ac97, config);
}
EXPORT_SYMBOL_GPL(devm_regmap_init_ac97);

MODULE_LICENSE("GPL v2");
