// SPDX-License-Identifier: GPL-2.0
//
// Register map access API - AC'97 support
//
// Copyright 2013 Linaro Ltd.  All rights reserved.

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

struct regmap *__regmap_init_ac97(struct snd_ac97 *ac97,
				  const struct regmap_config *config,
				  struct lock_class_key *lock_key,
				  const char *lock_name)
{
	return __regmap_init(&ac97->dev, &ac97_regmap_bus, ac97, config,
			     lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__regmap_init_ac97);

struct regmap *__devm_regmap_init_ac97(struct snd_ac97 *ac97,
				       const struct regmap_config *config,
				       struct lock_class_key *lock_key,
				       const char *lock_name)
{
	return __devm_regmap_init(&ac97->dev, &ac97_regmap_bus, ac97, config,
				  lock_key, lock_name);
}
EXPORT_SYMBOL_GPL(__devm_regmap_init_ac97);

MODULE_LICENSE("GPL v2");
