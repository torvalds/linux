// SPDX-License-Identifier: GPL-2.0+
/*
 * Core support for ATC260x PMICs
 *
 * Copyright (C) 2019 Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 * Copyright (C) 2020 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <linux/interrupt.h>
#include <linux/mfd/atc260x/core.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#define ATC260X_CHIP_REV_MAX	31

struct atc260x_init_regs {
	unsigned int cmu_devrst;
	unsigned int cmu_devrst_ints;
	unsigned int ints_msk;
	unsigned int pad_en;
	unsigned int pad_en_extirq;
};

static void regmap_lock_mutex(void *__mutex)
{
	struct mutex *mutex = __mutex;

	/*
	 * Using regmap within an atomic context (e.g. accessing a PMIC when
	 * powering system down) is normally allowed only if the regmap type
	 * is MMIO and the regcache type is either REGCACHE_NONE or
	 * REGCACHE_FLAT. For slow buses like I2C and SPI, the regmap is
	 * internally protected by a mutex which is acquired non-atomically.
	 *
	 * Let's improve this by using a customized locking scheme inspired
	 * from I2C atomic transfer. See i2c_in_atomic_xfer_mode() for a
	 * starting point.
	 */
	if (system_state > SYSTEM_RUNNING && irqs_disabled())
		mutex_trylock(mutex);
	else
		mutex_lock(mutex);
}

static void regmap_unlock_mutex(void *__mutex)
{
	struct mutex *mutex = __mutex;

	mutex_unlock(mutex);
}

static const struct regmap_config atc2603c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = ATC2603C_SADDR,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_config atc2609a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = ATC2609A_SADDR,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_irq atc2603c_regmap_irqs[] = {
	REGMAP_IRQ_REG(ATC2603C_IRQ_AUDIO,	0, ATC2603C_INTS_MSK_AUDIO),
	REGMAP_IRQ_REG(ATC2603C_IRQ_OV,		0, ATC2603C_INTS_MSK_OV),
	REGMAP_IRQ_REG(ATC2603C_IRQ_OC,		0, ATC2603C_INTS_MSK_OC),
	REGMAP_IRQ_REG(ATC2603C_IRQ_OT,		0, ATC2603C_INTS_MSK_OT),
	REGMAP_IRQ_REG(ATC2603C_IRQ_UV,		0, ATC2603C_INTS_MSK_UV),
	REGMAP_IRQ_REG(ATC2603C_IRQ_ALARM,	0, ATC2603C_INTS_MSK_ALARM),
	REGMAP_IRQ_REG(ATC2603C_IRQ_ONOFF,	0, ATC2603C_INTS_MSK_ONOFF),
	REGMAP_IRQ_REG(ATC2603C_IRQ_SGPIO,	0, ATC2603C_INTS_MSK_SGPIO),
	REGMAP_IRQ_REG(ATC2603C_IRQ_IR,		0, ATC2603C_INTS_MSK_IR),
	REGMAP_IRQ_REG(ATC2603C_IRQ_REMCON,	0, ATC2603C_INTS_MSK_REMCON),
	REGMAP_IRQ_REG(ATC2603C_IRQ_POWER_IN,	0, ATC2603C_INTS_MSK_POWERIN),
};

static const struct regmap_irq atc2609a_regmap_irqs[] = {
	REGMAP_IRQ_REG(ATC2609A_IRQ_AUDIO,	0, ATC2609A_INTS_MSK_AUDIO),
	REGMAP_IRQ_REG(ATC2609A_IRQ_OV,		0, ATC2609A_INTS_MSK_OV),
	REGMAP_IRQ_REG(ATC2609A_IRQ_OC,		0, ATC2609A_INTS_MSK_OC),
	REGMAP_IRQ_REG(ATC2609A_IRQ_OT,		0, ATC2609A_INTS_MSK_OT),
	REGMAP_IRQ_REG(ATC2609A_IRQ_UV,		0, ATC2609A_INTS_MSK_UV),
	REGMAP_IRQ_REG(ATC2609A_IRQ_ALARM,	0, ATC2609A_INTS_MSK_ALARM),
	REGMAP_IRQ_REG(ATC2609A_IRQ_ONOFF,	0, ATC2609A_INTS_MSK_ONOFF),
	REGMAP_IRQ_REG(ATC2609A_IRQ_WKUP,	0, ATC2609A_INTS_MSK_WKUP),
	REGMAP_IRQ_REG(ATC2609A_IRQ_IR,		0, ATC2609A_INTS_MSK_IR),
	REGMAP_IRQ_REG(ATC2609A_IRQ_REMCON,	0, ATC2609A_INTS_MSK_REMCON),
	REGMAP_IRQ_REG(ATC2609A_IRQ_POWER_IN,	0, ATC2609A_INTS_MSK_POWERIN),
};

static const struct regmap_irq_chip atc2603c_regmap_irq_chip = {
	.name = "atc2603c",
	.irqs = atc2603c_regmap_irqs,
	.num_irqs = ARRAY_SIZE(atc2603c_regmap_irqs),
	.num_regs = 1,
	.status_base = ATC2603C_INTS_PD,
	.mask_base = ATC2603C_INTS_MSK,
	.mask_invert = true,
};

static const struct regmap_irq_chip atc2609a_regmap_irq_chip = {
	.name = "atc2609a",
	.irqs = atc2609a_regmap_irqs,
	.num_irqs = ARRAY_SIZE(atc2609a_regmap_irqs),
	.num_regs = 1,
	.status_base = ATC2609A_INTS_PD,
	.mask_base = ATC2609A_INTS_MSK,
	.mask_invert = true,
};

static const struct resource atc2603c_onkey_resources[] = {
	DEFINE_RES_IRQ(ATC2603C_IRQ_ONOFF),
};

static const struct resource atc2609a_onkey_resources[] = {
	DEFINE_RES_IRQ(ATC2609A_IRQ_ONOFF),
};

static const struct mfd_cell atc2603c_mfd_cells[] = {
	{ .name = "atc260x-regulator" },
	{ .name = "atc260x-pwrc" },
	{
		.name = "atc260x-onkey",
		.num_resources = ARRAY_SIZE(atc2603c_onkey_resources),
		.resources = atc2603c_onkey_resources,
	},
};

static const struct mfd_cell atc2609a_mfd_cells[] = {
	{ .name = "atc260x-regulator" },
	{ .name = "atc260x-pwrc" },
	{
		.name = "atc260x-onkey",
		.num_resources = ARRAY_SIZE(atc2609a_onkey_resources),
		.resources = atc2609a_onkey_resources,
	},
};

static const struct atc260x_init_regs atc2603c_init_regs = {
	.cmu_devrst = ATC2603C_CMU_DEVRST,
	.cmu_devrst_ints = ATC2603C_CMU_DEVRST_INTS,
	.ints_msk = ATC2603C_INTS_MSK,
	.pad_en = ATC2603C_PAD_EN,
	.pad_en_extirq = ATC2603C_PAD_EN_EXTIRQ,
};

static const struct atc260x_init_regs atc2609a_init_regs = {
	.cmu_devrst = ATC2609A_CMU_DEVRST,
	.cmu_devrst_ints = ATC2609A_CMU_DEVRST_INTS,
	.ints_msk = ATC2609A_INTS_MSK,
	.pad_en = ATC2609A_PAD_EN,
	.pad_en_extirq = ATC2609A_PAD_EN_EXTIRQ,
};

static void atc260x_cmu_reset(struct atc260x *atc260x)
{
	const struct atc260x_init_regs *regs = atc260x->init_regs;

	/* Assert reset */
	regmap_update_bits(atc260x->regmap, regs->cmu_devrst,
			   regs->cmu_devrst_ints, ~regs->cmu_devrst_ints);

	/* De-assert reset */
	regmap_update_bits(atc260x->regmap, regs->cmu_devrst,
			   regs->cmu_devrst_ints, regs->cmu_devrst_ints);
}

static void atc260x_dev_init(struct atc260x *atc260x)
{
	const struct atc260x_init_regs *regs = atc260x->init_regs;

	/* Initialize interrupt block */
	atc260x_cmu_reset(atc260x);

	/* Disable all interrupt sources */
	regmap_write(atc260x->regmap, regs->ints_msk, 0);

	/* Enable EXTIRQ pad */
	regmap_update_bits(atc260x->regmap, regs->pad_en,
			   regs->pad_en_extirq, regs->pad_en_extirq);
}

/**
 * atc260x_match_device(): Setup ATC260x variant related fields
 *
 * @atc260x: ATC260x device to setup (.dev field must be set)
 * @regmap_cfg: regmap config associated with this ATC260x device
 *
 * This lets the ATC260x core configure the MFD cells and register maps
 * for later use.
 */
int atc260x_match_device(struct atc260x *atc260x, struct regmap_config *regmap_cfg)
{
	struct device *dev = atc260x->dev;
	const void *of_data;

	of_data = of_device_get_match_data(dev);
	if (!of_data)
		return -ENODEV;

	atc260x->ic_type = (unsigned long)of_data;

	switch (atc260x->ic_type) {
	case ATC2603C:
		*regmap_cfg = atc2603c_regmap_config;
		atc260x->regmap_irq_chip = &atc2603c_regmap_irq_chip;
		atc260x->cells = atc2603c_mfd_cells;
		atc260x->nr_cells = ARRAY_SIZE(atc2603c_mfd_cells);
		atc260x->type_name = "atc2603c";
		atc260x->rev_reg = ATC2603C_CHIP_VER;
		atc260x->init_regs = &atc2603c_init_regs;
		break;
	case ATC2609A:
		*regmap_cfg = atc2609a_regmap_config;
		atc260x->regmap_irq_chip = &atc2609a_regmap_irq_chip;
		atc260x->cells = atc2609a_mfd_cells;
		atc260x->nr_cells = ARRAY_SIZE(atc2609a_mfd_cells);
		atc260x->type_name = "atc2609a";
		atc260x->rev_reg = ATC2609A_CHIP_VER;
		atc260x->init_regs = &atc2609a_init_regs;
		break;
	default:
		dev_err(dev, "Unsupported ATC260x device type: %u\n",
			atc260x->ic_type);
		return -EINVAL;
	}

	atc260x->regmap_mutex = devm_kzalloc(dev, sizeof(*atc260x->regmap_mutex),
					     GFP_KERNEL);
	if (!atc260x->regmap_mutex)
		return -ENOMEM;

	mutex_init(atc260x->regmap_mutex);

	regmap_cfg->lock = regmap_lock_mutex,
	regmap_cfg->unlock = regmap_unlock_mutex,
	regmap_cfg->lock_arg = atc260x->regmap_mutex;

	return 0;
}
EXPORT_SYMBOL_GPL(atc260x_match_device);

/**
 * atc260x_device_probe(): Probe a configured ATC260x device
 *
 * @atc260x: ATC260x device to probe (must be configured)
 *
 * This function lets the ATC260x core register the ATC260x MFD devices
 * and IRQCHIP. The ATC260x device passed in must be fully configured
 * with atc260x_match_device, its IRQ set, and regmap created.
 */
int atc260x_device_probe(struct atc260x *atc260x)
{
	struct device *dev = atc260x->dev;
	unsigned int chip_rev;
	int ret;

	if (!atc260x->irq) {
		dev_err(dev, "No interrupt support\n");
		return -EINVAL;
	}

	/* Initialize the hardware */
	atc260x_dev_init(atc260x);

	ret = regmap_read(atc260x->regmap, atc260x->rev_reg, &chip_rev);
	if (ret) {
		dev_err(dev, "Failed to get chip revision\n");
		return ret;
	}

	if (chip_rev > ATC260X_CHIP_REV_MAX) {
		dev_err(dev, "Unknown chip revision: %u\n", chip_rev);
		return -EINVAL;
	}

	atc260x->ic_ver = __ffs(chip_rev + 1U);

	dev_info(dev, "Detected chip type %s rev.%c\n",
		 atc260x->type_name, 'A' + atc260x->ic_ver);

	ret = devm_regmap_add_irq_chip(dev, atc260x->regmap, atc260x->irq, IRQF_ONESHOT,
				       -1, atc260x->regmap_irq_chip, &atc260x->irq_data);
	if (ret) {
		dev_err(dev, "Failed to add IRQ chip: %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				   atc260x->cells, atc260x->nr_cells, NULL, 0,
				   regmap_irq_get_domain(atc260x->irq_data));
	if (ret) {
		dev_err(dev, "Failed to add child devices: %d\n", ret);
		regmap_del_irq_chip(atc260x->irq, atc260x->irq_data);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(atc260x_device_probe);

MODULE_DESCRIPTION("ATC260x PMICs Core support");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_AUTHOR("Cristian Ciocaltea <cristian.ciocaltea@gmail.com>");
MODULE_LICENSE("GPL");
