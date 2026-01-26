// SPDX-License-Identifier: GPL-2.0
/*
 * MMIO register bitfield-controlled multiplexer driver
 *
 * Copyright (C) 2017 Pengutronix, Philipp Zabel <kernel@pengutronix.de>
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mux/driver.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

struct mux_mmio {
	struct regmap_field **fields;
	unsigned int *hardware_states;
};

static int mux_mmio_get(struct mux_control *mux, int *state)
{
	struct mux_mmio *mux_mmio = mux_chip_priv(mux->chip);
	unsigned int index = mux_control_get_index(mux);

	return regmap_field_read(mux_mmio->fields[index], state);
}

static int mux_mmio_set(struct mux_control *mux, int state)
{
	struct mux_mmio *mux_mmio = mux_chip_priv(mux->chip);
	unsigned int index = mux_control_get_index(mux);

	return regmap_field_write(mux_mmio->fields[index], state);
}

static const struct mux_control_ops mux_mmio_ops = {
	.set = mux_mmio_set,
};

static const struct of_device_id mux_mmio_dt_ids[] = {
	{ .compatible = "mmio-mux", },
	{ .compatible = "reg-mux", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mux_mmio_dt_ids);

static const struct regmap_config mux_mmio_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int mux_mmio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct mux_chip *mux_chip;
	struct mux_mmio *mux_mmio;
	struct regmap *regmap;
	void __iomem *base;
	int num_fields;
	int ret;
	int i;

	if (of_device_is_compatible(np, "mmio-mux")) {
		regmap = syscon_node_to_regmap(np->parent);
	} else {
		base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(base))
			regmap = ERR_PTR(-ENODEV);
		else
			regmap = regmap_init_mmio(dev, base, &mux_mmio_regmap_cfg);
		/* Fallback to checking the parent node on "real" errors. */
		if (IS_ERR(regmap) && regmap != ERR_PTR(-EPROBE_DEFER)) {
			regmap = dev_get_regmap(dev->parent, NULL);
			if (!regmap)
				regmap = ERR_PTR(-ENODEV);
		}
	}
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to get regmap\n");

	ret = of_property_count_u32_elems(np, "mux-reg-masks");
	if (ret == 0 || ret % 2)
		ret = -EINVAL;
	if (ret < 0) {
		dev_err(dev, "mux-reg-masks property missing or invalid: %d\n",
			ret);
		return ret;
	}
	num_fields = ret / 2;

	mux_chip = devm_mux_chip_alloc(dev, num_fields, sizeof(struct mux_mmio));
	if (IS_ERR(mux_chip))
		return PTR_ERR(mux_chip);

	mux_mmio = mux_chip_priv(mux_chip);

	mux_mmio->fields = devm_kmalloc(dev, num_fields * sizeof(*mux_mmio->fields), GFP_KERNEL);
	if (!mux_mmio->fields)
		return -ENOMEM;

	mux_mmio->hardware_states = devm_kmalloc(dev, num_fields *
						 sizeof(*mux_mmio->hardware_states), GFP_KERNEL);
	if (!mux_mmio->hardware_states)
		return -ENOMEM;

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

		mux_mmio->fields[i] = devm_regmap_field_alloc(dev, regmap, field);
		if (IS_ERR(mux_mmio->fields[i])) {
			ret = PTR_ERR(mux_mmio->fields[i]);
			dev_err(dev, "bitfield %d: failed to allocate: %d\n",
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

	dev_set_drvdata(dev, mux_chip);

	return devm_mux_chip_register(dev, mux_chip);
}

static int mux_mmio_suspend_noirq(struct device *dev)
{
	struct mux_chip *mux_chip = dev_get_drvdata(dev);
	struct mux_mmio *mux_mmio = mux_chip_priv(mux_chip);
	unsigned int state;
	int ret, i;

	for (i = 0; i < mux_chip->controllers; i++) {
		ret = mux_mmio_get(&mux_chip->mux[i], &state);
		if (ret) {
			dev_err(dev, "control %u: error saving mux: %d\n", i, ret);
			return ret;
		}

		mux_mmio->hardware_states[i] = state;
	}

	return 0;
}

static int mux_mmio_resume_noirq(struct device *dev)
{
	struct mux_chip *mux_chip = dev_get_drvdata(dev);
	struct mux_mmio *mux_mmio = mux_chip_priv(mux_chip);
	int ret, i;

	for (i = 0; i < mux_chip->controllers; i++) {
		ret = mux_mmio_set(&mux_chip->mux[i], mux_mmio->hardware_states[i]);
		if (ret) {
			dev_err(dev, "control %u: error restoring mux: %d\n", i, ret);
			return ret;
		}
	}

	return 0;
}

static DEFINE_NOIRQ_DEV_PM_OPS(mux_mmio_pm_ops, mux_mmio_suspend_noirq, mux_mmio_resume_noirq);

static struct platform_driver mux_mmio_driver = {
	.driver = {
		.name = "mmio-mux",
		.of_match_table	= mux_mmio_dt_ids,
		.pm = pm_sleep_ptr(&mux_mmio_pm_ops),
	},
	.probe = mux_mmio_probe,
};
module_platform_driver(mux_mmio_driver);

MODULE_DESCRIPTION("MMIO register bitfield-controlled multiplexer driver");
MODULE_AUTHOR("Philipp Zabel <p.zabel@pengutronix.de>");
MODULE_LICENSE("GPL v2");
