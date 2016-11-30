/*
 * TI SYSCON regmap reset driver
 *
 * Copyright (C) 2015-2016 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *	Suman Anna <afd@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/reset/ti-syscon.h>

/**
 * struct ti_syscon_reset_control - reset control structure
 * @assert_offset: reset assert control register offset from syscon base
 * @assert_bit: reset assert bit in the reset assert control register
 * @deassert_offset: reset deassert control register offset from syscon base
 * @deassert_bit: reset deassert bit in the reset deassert control register
 * @status_offset: reset status register offset from syscon base
 * @status_bit: reset status bit in the reset status register
 * @flags: reset flag indicating how the (de)assert and status are handled
 */
struct ti_syscon_reset_control {
	unsigned int assert_offset;
	unsigned int assert_bit;
	unsigned int deassert_offset;
	unsigned int deassert_bit;
	unsigned int status_offset;
	unsigned int status_bit;
	u32 flags;
};

/**
 * struct ti_syscon_reset_data - reset controller information structure
 * @rcdev: reset controller entity
 * @regmap: regmap handle containing the memory-mapped reset registers
 * @controls: array of reset controls
 * @nr_controls: number of controls in control array
 */
struct ti_syscon_reset_data {
	struct reset_controller_dev rcdev;
	struct regmap *regmap;
	struct ti_syscon_reset_control *controls;
	unsigned int nr_controls;
};

#define to_ti_syscon_reset_data(rcdev)	\
	container_of(rcdev, struct ti_syscon_reset_data, rcdev)

/**
 * ti_syscon_reset_assert() - assert device reset
 * @rcdev: reset controller entity
 * @id: ID of the reset to be asserted
 *
 * This function implements the reset driver op to assert a device's reset.
 * This asserts the reset in a manner prescribed by the reset flags.
 *
 * Return: 0 for successful request, else a corresponding error value
 */
static int ti_syscon_reset_assert(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	struct ti_syscon_reset_data *data = to_ti_syscon_reset_data(rcdev);
	struct ti_syscon_reset_control *control;
	unsigned int mask, value;

	if (id >= data->nr_controls)
		return -EINVAL;

	control = &data->controls[id];

	if (control->flags & ASSERT_NONE)
		return -ENOTSUPP; /* assert not supported for this reset */

	mask = BIT(control->assert_bit);
	value = (control->flags & ASSERT_SET) ? mask : 0x0;

	return regmap_update_bits(data->regmap, control->assert_offset, mask, value);
}

/**
 * ti_syscon_reset_deassert() - deassert device reset
 * @rcdev: reset controller entity
 * @id: ID of reset to be deasserted
 *
 * This function implements the reset driver op to deassert a device's reset.
 * This deasserts the reset in a manner prescribed by the reset flags.
 *
 * Return: 0 for successful request, else a corresponding error value
 */
static int ti_syscon_reset_deassert(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct ti_syscon_reset_data *data = to_ti_syscon_reset_data(rcdev);
	struct ti_syscon_reset_control *control;
	unsigned int mask, value;

	if (id >= data->nr_controls)
		return -EINVAL;

	control = &data->controls[id];

	if (control->flags & DEASSERT_NONE)
		return -ENOTSUPP; /* deassert not supported for this reset */

	mask = BIT(control->deassert_bit);
	value = (control->flags & DEASSERT_SET) ? mask : 0x0;

	return regmap_update_bits(data->regmap, control->deassert_offset, mask, value);
}

/**
 * ti_syscon_reset_status() - check device reset status
 * @rcdev: reset controller entity
 * @id: ID of the reset for which the status is being requested
 *
 * This function implements the reset driver op to return the status of a
 * device's reset.
 *
 * Return: 0 if reset is deasserted, true if reset is asserted, else a
 * corresponding error value
 */
static int ti_syscon_reset_status(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	struct ti_syscon_reset_data *data = to_ti_syscon_reset_data(rcdev);
	struct ti_syscon_reset_control *control;
	unsigned int reset_state;
	int ret;

	if (id >= data->nr_controls)
		return -EINVAL;

	control = &data->controls[id];

	if (control->flags & STATUS_NONE)
		return -ENOTSUPP; /* status not supported for this reset */

	ret = regmap_read(data->regmap, control->status_offset, &reset_state);
	if (ret)
		return ret;

	return !(reset_state & BIT(control->status_bit)) ==
		!(control->flags & STATUS_SET);
}

static struct reset_control_ops ti_syscon_reset_ops = {
	.assert		= ti_syscon_reset_assert,
	.deassert	= ti_syscon_reset_deassert,
	.status		= ti_syscon_reset_status,
};

static int ti_syscon_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ti_syscon_reset_data *data;
	struct regmap *regmap;
	const __be32 *list;
	struct ti_syscon_reset_control *controls;
	int size, nr_controls, i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	list = of_get_property(np, "ti,reset-bits", &size);
	if (!list || (size / sizeof(*list)) % 7 != 0) {
		dev_err(dev, "invalid DT reset description\n");
		return -EINVAL;
	}

	nr_controls = (size / sizeof(*list)) / 7;
	controls = devm_kzalloc(dev, nr_controls * sizeof(*controls), GFP_KERNEL);
	if (!controls)
		return -ENOMEM;

	for (i = 0; i < nr_controls; i++) {
		controls[i].assert_offset = be32_to_cpup(list++);
		controls[i].assert_bit = be32_to_cpup(list++);
		controls[i].deassert_offset = be32_to_cpup(list++);
		controls[i].deassert_bit = be32_to_cpup(list++);
		controls[i].status_offset = be32_to_cpup(list++);
		controls[i].status_bit = be32_to_cpup(list++);
		controls[i].flags = be32_to_cpup(list++);
	}

	data->rcdev.ops = &ti_syscon_reset_ops;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.of_node = np;
	data->rcdev.nr_resets = nr_controls;
	data->regmap = regmap;
	data->controls = controls;
	data->nr_controls = nr_controls;

	platform_set_drvdata(pdev, data);

	return devm_reset_controller_register(dev, &data->rcdev);
}

static const struct of_device_id ti_syscon_reset_of_match[] = {
	{ .compatible = "ti,syscon-reset", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ti_syscon_reset_of_match);

static struct platform_driver ti_syscon_reset_driver = {
	.probe = ti_syscon_reset_probe,
	.driver = {
		.name = "ti-syscon-reset",
		.of_match_table = ti_syscon_reset_of_match,
	},
};
module_platform_driver(ti_syscon_reset_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_AUTHOR("Suman Anna <s-anna@ti.com>");
MODULE_DESCRIPTION("TI SYSCON Regmap Reset Driver");
MODULE_LICENSE("GPL v2");
