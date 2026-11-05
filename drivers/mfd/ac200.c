// SPDX-License-Identifier: GPL-2.0-only
/*
 * X-Powers AC200 Core driver
 *
 * Copyright (C) 2019 Jernej Skrabec <jernej.skrabec@gmail.com>
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define AC200_SYS_CONTROL_REG			0x0002
#define AC200_SYS_CONTROL_CHIP_RESET_DEASSERT	BIT(0)
#define AC200_SYS_IRQ_ENABLE_REG		0x0004
#define AC200_SYS_IRQ_INTB_ENABLE		BIT(15)
#define AC200_SYS_IRQ_INTB_ACTIVE_HIGH		BIT(14)
#define AC200_SYS_IRQ_RTC			BIT(12)
#define AC200_SYS_IRQ_EPHY			BIT(8)
#define AC200_SYS_IRQ_TVE			BIT(4)

/* Interface register accessible from every register page. */
#define AC200_TWI_REG_ADDR_H	0x00fe
#define AC200_MAX_REG		0xa1f2

static const struct regmap_range_cfg ac200_range_cfg[] = {
	{
		.range_max = AC200_MAX_REG,
		.selector_reg = AC200_TWI_REG_ADDR_H,
		.selector_mask = 0xff,
		.window_len = 256,
	},
};

/*
 * Each AC200 sub-block can reset independently, invalidating its register
 * contents without regmap's knowledge. Cache only the common page selector;
 * this avoids a selector read-modify-write for every access on the same page
 * without ever returning stale functional-register values.
 */
static bool ac200_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg != AC200_TWI_REG_ADDR_H;
}

static const struct regmap_config ac200_regmap_config = {
	.name = "ac200",
	.reg_bits = 8,
	.reg_stride = 2,
	.val_bits = 16,
	.ranges = ac200_range_cfg,
	.num_ranges = ARRAY_SIZE(ac200_range_cfg),
	.max_register = AC200_MAX_REG,
	.volatile_reg = ac200_volatile_reg,
	.cache_type = REGCACHE_MAPLE,
};

static const char * const ac200_codec_supplies[] = {
	"ac-ldoin",
};

static const char * const ac200_tve_supplies[] = {
	"tv-vcc",
};

static const struct resource ac200_tve_resources[] = {
	DEFINE_RES_IRQ_NAMED(0, "intb"),
};

#define AC200_CELL(_name, _supplies, _resources)		\
	{							\
		.name = (_name),				\
		.parent_supplies = (_supplies),			\
		.num_parent_supplies = ARRAY_SIZE(_supplies),	\
		.resources = (_resources),			\
		.num_resources = MFD_RES_SIZE(_resources),	\
	}

static const struct mfd_cell ac200_cells[] = {
	AC200_CELL("ac200-codec", ac200_codec_supplies, NULL),
	AC200_CELL("ac200-tve", ac200_tve_supplies, ac200_tve_resources),
};

static const struct mfd_cell ac200_noirq_cells[] = {
	AC200_CELL("ac200-codec", ac200_codec_supplies, NULL),
	AC200_CELL("ac200-tve", ac200_tve_supplies, NULL),
};

static void ac200_disable_intb(void *data)
{
	struct regmap *regmap = data;
	int ret;

	ret = regmap_clear_bits(regmap, AC200_SYS_IRQ_ENABLE_REG, AC200_SYS_IRQ_INTB_ENABLE);
	if (ret)
		dev_err(regmap_get_device(regmap), "failed to disable INTB: %d\n", ret);
}

static int ac200_init_irq(struct device *dev, struct regmap *regmap, int irq)
{
	unsigned int trigger;
	u16 value = 0;
	int ret;

	trigger = irq_get_trigger_type(irq);
	switch (trigger) {
	case IRQ_TYPE_LEVEL_HIGH:
		value |= AC200_SYS_IRQ_INTB_ACTIVE_HIGH;
		break;
	case IRQ_TYPE_NONE:
	case IRQ_TYPE_LEVEL_LOW:
		break;
	default:
		return dev_err_probe(dev, -EINVAL,
				     "INTB is level triggered, not type %u\n", trigger);
	}

	/* Mask every source before enabling the shared output. */
	ret = regmap_update_bits(regmap, AC200_SYS_IRQ_ENABLE_REG,
				 AC200_SYS_IRQ_INTB_ENABLE | AC200_SYS_IRQ_INTB_ACTIVE_HIGH |
				 AC200_SYS_IRQ_RTC | AC200_SYS_IRQ_EPHY | AC200_SYS_IRQ_TVE, value);
	if (ret)
		return ret;

	/* Children are removed before INTB is disabled and the regmap released. */
	ret = devm_add_action_or_reset(dev, ac200_disable_intb, regmap);
	if (ret)
		return ret;

	if (trigger == IRQ_TYPE_NONE) {
		ret = irq_set_irq_type(irq, IRQ_TYPE_LEVEL_LOW);
		if (ret)
			return dev_err_probe(dev, ret, "failed to set INTB trigger\n");
	}

	/*
	 * Function drivers request INTB with IRQF_SHARED | IRQF_ONESHOT before
	 * enabling their own source. They check and clear their own status and
	 * mask their source before freeing the IRQ. No IRQ domain is needed.
	 */
	return regmap_set_bits(regmap, AC200_SYS_IRQ_ENABLE_REG, AC200_SYS_IRQ_INTB_ENABLE);
}

static int ac200_probe(struct i2c_client *client)
{
	const struct mfd_cell *cells = ac200_noirq_cells;
	unsigned int num_cells = ARRAY_SIZE(ac200_noirq_cells);
	struct device *dev = &client->dev;
	struct regmap *regmap;
	struct clk *clk;
	int ret;

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to enable input clock\n");

	ret = devm_clk_rate_exclusive_get(dev, clk);
	if (ret)
		return dev_err_probe(dev, ret, "failed to lock clock rate\n");

	/*
	 * No minimum delay is documented. Retain the vendor resume path's 40 ms
	 * wait after enabling the input clock and before register access.
	 */
	msleep(40);

	regmap = devm_regmap_init_i2c(client, &ac200_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "failed to initialize regmap\n");

	ret = regmap_set_bits(regmap, AC200_SYS_CONTROL_REG, AC200_SYS_CONTROL_CHIP_RESET_DEASSERT);
	if (ret)
		return ret;

	if (client->irq > 0) {
		ret = ac200_init_irq(dev, regmap, client->irq);
		if (ret)
			return ret;
		cells = ac200_cells;
		num_cells = ARRAY_SIZE(ac200_cells);
	}

	/* Function drivers read their firmware properties from the parent. */
	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, cells, num_cells,
				   NULL, client->irq, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add function devices\n");

	return 0;
}

static const struct of_device_id ac200_of_match[] = {
	{ .compatible = "x-powers,ac200" },
	{ }
};
MODULE_DEVICE_TABLE(of, ac200_of_match);

static const struct i2c_device_id ac200_i2c_ids[] = {
	{ .name = "ac200" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ac200_i2c_ids);

static struct i2c_driver ac200_driver = {
	.driver = {
		.name = "ac200",
		.of_match_table = ac200_of_match,
	},
	.probe = ac200_probe,
	.id_table = ac200_i2c_ids,
};
module_i2c_driver(ac200_driver);

MODULE_AUTHOR("Jernej Skrabec <jernej.skrabec@gmail.com>");
MODULE_AUTHOR("James Hilliard <james.hilliard1@gmail.com>");
MODULE_DESCRIPTION("X-Powers AC200 Core driver");
MODULE_LICENSE("GPL");
