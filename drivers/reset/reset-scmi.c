// SPDX-License-Identifier: GPL-2.0
/*
 * ARM System Control and Management Interface (ARM SCMI) reset driver
 *
 * Copyright (C) 2019 ARM Ltd.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/reset-controller.h>
#include <linux/scmi_protocol.h>

/**
 * struct scmi_reset_data - reset controller information structure
 * @rcdev: reset controller entity
 * @handle: ARM SCMI handle used for communication with system controller
 */
struct scmi_reset_data {
	struct reset_controller_dev rcdev;
	const struct scmi_handle *handle;
};

#define to_scmi_reset_data(p)	container_of((p), struct scmi_reset_data, rcdev)
#define to_scmi_handle(p)	(to_scmi_reset_data(p)->handle)

/**
 * scmi_reset_assert() - assert device reset
 * @rcdev: reset controller entity
 * @id: ID of the reset to be asserted
 *
 * This function implements the reset driver op to assert a device's reset
 * using the ARM SCMI protocol.
 *
 * Return: 0 for successful request, else a corresponding error value
 */
static int
scmi_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	const struct scmi_handle *handle = to_scmi_handle(rcdev);

	return handle->reset_ops->assert(handle, id);
}

/**
 * scmi_reset_deassert() - deassert device reset
 * @rcdev: reset controller entity
 * @id: ID of the reset to be deasserted
 *
 * This function implements the reset driver op to deassert a device's reset
 * using the ARM SCMI protocol.
 *
 * Return: 0 for successful request, else a corresponding error value
 */
static int
scmi_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	const struct scmi_handle *handle = to_scmi_handle(rcdev);

	return handle->reset_ops->deassert(handle, id);
}

/**
 * scmi_reset_reset() - reset the device
 * @rcdev: reset controller entity
 * @id: ID of the reset signal to be reset(assert + deassert)
 *
 * This function implements the reset driver op to trigger a device's
 * reset signal using the ARM SCMI protocol.
 *
 * Return: 0 for successful request, else a corresponding error value
 */
static int
scmi_reset_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	const struct scmi_handle *handle = to_scmi_handle(rcdev);

	return handle->reset_ops->reset(handle, id);
}

static const struct reset_control_ops scmi_reset_ops = {
	.assert		= scmi_reset_assert,
	.deassert	= scmi_reset_deassert,
	.reset		= scmi_reset_reset,
};

static int scmi_reset_probe(struct scmi_device *sdev)
{
	struct scmi_reset_data *data;
	struct device *dev = &sdev->dev;
	struct device_node *np = dev->of_node;
	const struct scmi_handle *handle = sdev->handle;

	if (!handle || !handle->reset_ops)
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->rcdev.ops = &scmi_reset_ops;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.of_node = np;
	data->rcdev.nr_resets = handle->reset_ops->num_domains_get(handle);
	data->handle = handle;

	return devm_reset_controller_register(dev, &data->rcdev);
}

static const struct scmi_device_id scmi_id_table[] = {
	{ SCMI_PROTOCOL_RESET },
	{ },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_reset_driver = {
	.name = "scmi-reset",
	.probe = scmi_reset_probe,
	.id_table = scmi_id_table,
};
module_scmi_driver(scmi_reset_driver);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCMI reset controller driver");
MODULE_LICENSE("GPL v2");
