// SPDX-License-Identifier: GPL-2.0-only
/*
 * Texas Instrument's System Control Interface (TI-SCI) reset driver
 *
 * Copyright (C) 2015-2017 Texas Instruments Incorporated - https://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/idr.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/soc/ti/ti_sci_protocol.h>

/**
 * struct ti_sci_reset_control - reset control structure
 * @dev_id: SoC-specific device identifier
 * @reset_mask: reset mask to use for toggling reset
 * @lock: synchronize reset_mask read-modify-writes
 */
struct ti_sci_reset_control {
	u32 dev_id;
	u32 reset_mask;
	struct mutex lock;
};

/**
 * struct ti_sci_reset_data - reset controller information structure
 * @rcdev: reset controller entity
 * @dev: reset controller device pointer
 * @sci: TI SCI handle used for communication with system controller
 * @idr: idr structure for mapping ids to reset control structures
 */
struct ti_sci_reset_data {
	struct reset_controller_dev rcdev;
	struct device *dev;
	const struct ti_sci_handle *sci;
	struct idr idr;
};

#define to_ti_sci_reset_data(p)	\
	container_of((p), struct ti_sci_reset_data, rcdev)

/**
 * ti_sci_reset_set() - program a device's reset
 * @rcdev: reset controller entity
 * @id: ID of the reset to toggle
 * @assert: boolean flag to indicate assert or deassert
 *
 * This is a common internal function used to assert or deassert a device's
 * reset using the TI SCI protocol. The device's reset is asserted if the
 * @assert argument is true, or deasserted if @assert argument is false.
 * The mechanism itself is a read-modify-write procedure, the current device
 * reset register is read using a TI SCI device operation, the new value is
 * set or un-set using the reset's mask, and the new reset value written by
 * using another TI SCI device operation.
 *
 * Return: 0 for successful request, else a corresponding error value
 */
static int ti_sci_reset_set(struct reset_controller_dev *rcdev,
			    unsigned long id, bool assert)
{
	struct ti_sci_reset_data *data = to_ti_sci_reset_data(rcdev);
	const struct ti_sci_handle *sci = data->sci;
	const struct ti_sci_dev_ops *dev_ops = &sci->ops.dev_ops;
	struct ti_sci_reset_control *control;
	u32 reset_state;
	int ret;

	control = idr_find(&data->idr, id);
	if (!control)
		return -EINVAL;

	mutex_lock(&control->lock);

	ret = dev_ops->get_device_resets(sci, control->dev_id, &reset_state);
	if (ret)
		goto out;

	if (assert)
		reset_state |= control->reset_mask;
	else
		reset_state &= ~control->reset_mask;

	ret = dev_ops->set_device_resets(sci, control->dev_id, reset_state);
out:
	mutex_unlock(&control->lock);

	return ret;
}

/**
 * ti_sci_reset_assert() - assert device reset
 * @rcdev: reset controller entity
 * @id: ID of the reset to be asserted
 *
 * This function implements the reset driver op to assert a device's reset
 * using the TI SCI protocol. This invokes the function ti_sci_reset_set()
 * with the corresponding parameters as passed in, but with the @assert
 * argument set to true for asserting the reset.
 *
 * Return: 0 for successful request, else a corresponding error value
 */
static int ti_sci_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return ti_sci_reset_set(rcdev, id, true);
}

/**
 * ti_sci_reset_deassert() - deassert device reset
 * @rcdev: reset controller entity
 * @id: ID of the reset to be deasserted
 *
 * This function implements the reset driver op to deassert a device's reset
 * using the TI SCI protocol. This invokes the function ti_sci_reset_set()
 * with the corresponding parameters as passed in, but with the @assert
 * argument set to false for deasserting the reset.
 *
 * Return: 0 for successful request, else a corresponding error value
 */
static int ti_sci_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return ti_sci_reset_set(rcdev, id, false);
}

/**
 * ti_sci_reset_status() - check device reset status
 * @rcdev: reset controller entity
 * @id: ID of reset to be checked
 *
 * This function implements the reset driver op to return the status of a
 * device's reset using the TI SCI protocol. The reset register value is read
 * by invoking the TI SCI device operation .get_device_resets(), and the
 * status of the specific reset is extracted and returned using this reset's
 * reset mask.
 *
 * Return: 0 if reset is deasserted, or a non-zero value if reset is asserted
 */
static int ti_sci_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct ti_sci_reset_data *data = to_ti_sci_reset_data(rcdev);
	const struct ti_sci_handle *sci = data->sci;
	const struct ti_sci_dev_ops *dev_ops = &sci->ops.dev_ops;
	struct ti_sci_reset_control *control;
	u32 reset_state;
	int ret;

	control = idr_find(&data->idr, id);
	if (!control)
		return -EINVAL;

	ret = dev_ops->get_device_resets(sci, control->dev_id, &reset_state);
	if (ret)
		return ret;

	return reset_state & control->reset_mask;
}

static const struct reset_control_ops ti_sci_reset_ops = {
	.assert		= ti_sci_reset_assert,
	.deassert	= ti_sci_reset_deassert,
	.status		= ti_sci_reset_status,
};

/**
 * ti_sci_reset_of_xlate() - translate a set of OF arguments to a reset ID
 * @rcdev: reset controller entity
 * @reset_spec: OF reset argument specifier
 *
 * This function performs the translation of the reset argument specifier
 * values defined in a reset consumer device node. The function allocates a
 * reset control structure for that device reset, and will be used by the
 * driver for performing any reset functions on that reset. An idr structure
 * is allocated and used to map to the reset control structure. This idr
 * is used by the driver to do reset lookups.
 *
 * Return: 0 for successful request, else a corresponding error value
 */
static int ti_sci_reset_of_xlate(struct reset_controller_dev *rcdev,
				 const struct of_phandle_args *reset_spec)
{
	struct ti_sci_reset_data *data = to_ti_sci_reset_data(rcdev);
	struct ti_sci_reset_control *control;

	if (WARN_ON(reset_spec->args_count != rcdev->of_reset_n_cells))
		return -EINVAL;

	control = devm_kzalloc(data->dev, sizeof(*control), GFP_KERNEL);
	if (!control)
		return -ENOMEM;

	control->dev_id = reset_spec->args[0];
	control->reset_mask = reset_spec->args[1];
	mutex_init(&control->lock);

	return idr_alloc(&data->idr, control, 0, 0, GFP_KERNEL);
}

static const struct of_device_id ti_sci_reset_of_match[] = {
	{ .compatible = "ti,sci-reset", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ti_sci_reset_of_match);

static int ti_sci_reset_probe(struct platform_device *pdev)
{
	struct ti_sci_reset_data *data;

	if (!pdev->dev.of_node)
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->sci = devm_ti_sci_get_handle(&pdev->dev);
	if (IS_ERR(data->sci))
		return PTR_ERR(data->sci);

	data->rcdev.ops = &ti_sci_reset_ops;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.of_node = pdev->dev.of_node;
	data->rcdev.of_reset_n_cells = 2;
	data->rcdev.of_xlate = ti_sci_reset_of_xlate;
	data->dev = &pdev->dev;
	idr_init(&data->idr);

	platform_set_drvdata(pdev, data);

	return reset_controller_register(&data->rcdev);
}

static int ti_sci_reset_remove(struct platform_device *pdev)
{
	struct ti_sci_reset_data *data = platform_get_drvdata(pdev);

	reset_controller_unregister(&data->rcdev);

	idr_destroy(&data->idr);

	return 0;
}

static struct platform_driver ti_sci_reset_driver = {
	.probe = ti_sci_reset_probe,
	.remove = ti_sci_reset_remove,
	.driver = {
		.name = "ti-sci-reset",
		.of_match_table = ti_sci_reset_of_match,
	},
};
module_platform_driver(ti_sci_reset_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("TI System Control Interface (TI SCI) Reset driver");
MODULE_LICENSE("GPL v2");
