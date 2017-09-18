/*
 * MMIO register bitfield-controlled multiplexer driver
 *
 * Copyright (C) 2017 Pengutronix, Philipp Zabel <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mux/driver.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

static int mux_mmio_set(struct mux_control *mux, int state)
{
	struct regmap_field **fields = mux_chip_priv(mux->chip);

	return regmap_field_write(fields[mux_control_get_index(mux)], state);
}

static const struct mux_control_ops mux_mmio_ops = {
	.set = mux_mmio_set,
};

static const struct of_device_id mux_mmio_dt_ids[] = {
	{ .compatible = "mmio-mux", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mux_mmio_dt_ids);

static int mux_mmio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regmap_field **fields;
	struct mux_chip *mux_chip;
	struct regmap *regmap;
	int num_fields;
	int ret;
	int i;

	regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(dev, "failed to get regmap: %d\n", ret);
		return ret;
	}

	ret = of_property_count_u32_elems(np, "mux-reg-masks");
	if (ret == 0 || ret % 2)
		ret = -EINVAL;
	if (ret < 0) {
		dev_err(dev, "mux-reg-masks property missing or invalid: %d\n",
			ret);
		return ret;
	}
	num_fields = ret / 2;

	mux_chip = devm_mux_chip_alloc(dev, num_fields, num_fields *
				       sizeof(*fields));
	if (IS_ERR(mux_chip))
		return PTR_ERR(mux_chip);

	fields = mux_chip_priv(mux_chip);

	for (i = 0; i < num_fields; i++) {
		struct mux_control *mux = &mux_chip->mux[i];
		struct reg_field field;
		s32 idle_state = MUX_IDLE_AS_IS;
		u32 reg, mask;
		int bits;

		ret = of_property_read_u32_index(np, "mux-reg-masks",
						 2 * i, &reg);
		if (!ret)
			ret = of_property_read_u32_index(np, "mux-reg-masks",
							 2 * i + 1, &mask);
		if (ret < 0) {
			dev_err(dev, "bitfield %d: failed to read mux-reg-masks property: %d\n",
				i, ret);
			return ret;
		}

		field.reg = reg;
		field.msb = fls(mask) - 1;
		field.lsb = ffs(mask) - 1;

		if (mask != GENMASK(field.msb, field.lsb)) {
			dev_err(dev, "bitfield %d: invalid mask 0x%x\n",
				i, mask);
			return -EINVAL;
		}

		fields[i] = devm_regmap_field_alloc(dev, regmap, field);
		if (IS_ERR(fields[i])) {
			ret = PTR_ERR(fields[i]);
			dev_err(dev, "bitfield %d: failed allocate: %d\n",
				i, ret);
			return ret;
		}

		bits = 1 + field.msb - field.lsb;
		mux->states = 1 << bits;

		of_property_read_u32_index(np, "idle-states", i,
					   (u32 *)&idle_state);
		if (idle_state != MUX_IDLE_AS_IS) {
			if (idle_state < 0 || idle_state >= mux->states) {
				dev_err(dev, "bitfield: %d: out of range idle state %d\n",
					i, idle_state);
				return -EINVAL;
			}

			mux->idle_state = idle_state;
		}
	}

	mux_chip->ops = &mux_mmio_ops;

	return devm_mux_chip_register(dev, mux_chip);
}

static struct platform_driver mux_mmio_driver = {
	.driver = {
		.name = "mmio-mux",
		.of_match_table	= of_match_ptr(mux_mmio_dt_ids),
	},
	.probe = mux_mmio_probe,
};
module_platform_driver(mux_mmio_driver);

MODULE_DESCRIPTION("MMIO register bitfield-controlled multiplexer driver");
MODULE_AUTHOR("Philipp Zabel <p.zabel@pengutronix.de>");
MODULE_LICENSE("GPL v2");
