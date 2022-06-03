// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2020 - 2021 Intel Corporation
 */

/**
 * DOC: MEI_PXP Client Driver
 *
 * The mei_pxp driver acts as a translation layer between PXP
 * protocol  implementer (I915) and ME FW by translating PXP
 * negotiation messages to ME FW command payloads and vice versa.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/mei_cl_bus.h>
#include <linux/component.h>
#include <drm/drm_connector.h>
#include <drm/i915_component.h>
#include <drm/i915_pxp_tee_interface.h>

#include "mei_pxp.h"

/**
 * mei_pxp_send_message() - Sends a PXP message to ME FW.
 * @dev: device corresponding to the mei_cl_device
 * @message: a message buffer to send
 * @size: size of the message
 * Return: 0 on Success, <0 on Failure
 */
static int
mei_pxp_send_message(struct device *dev, const void *message, size_t size)
{
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !message)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	/* temporary drop const qualifier till the API is fixed */
	byte = mei_cldev_send(cldev, (u8 *)message, size);
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	return 0;
}

/**
 * mei_pxp_receive_message() - Receives a PXP message from ME FW.
 * @dev: device corresponding to the mei_cl_device
 * @buffer: a message buffer to contain the received message
 * @size: size of the buffer
 * Return: bytes sent on Success, <0 on Failure
 */
static int
mei_pxp_receive_message(struct device *dev, void *buffer, size_t size)
{
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !buffer)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	byte = mei_cldev_recv(cldev, buffer, size);
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	return byte;
}

static const struct i915_pxp_component_ops mei_pxp_ops = {
	.owner = THIS_MODULE,
	.send = mei_pxp_send_message,
	.recv = mei_pxp_receive_message,
};

static int mei_component_master_bind(struct device *dev)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	struct i915_pxp_component *comp_master = mei_cldev_get_drvdata(cldev);
	int ret;

	comp_master->ops = &mei_pxp_ops;
	comp_master->tee_dev = dev;
	ret = component_bind_all(dev, comp_master);
	if (ret < 0)
		return ret;

	return 0;
}

static void mei_component_master_unbind(struct device *dev)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	struct i915_pxp_component *comp_master = mei_cldev_get_drvdata(cldev);

	component_unbind_all(dev, comp_master);
}

static const struct component_master_ops mei_component_master_ops = {
	.bind = mei_component_master_bind,
	.unbind = mei_component_master_unbind,
};

/**
 * mei_pxp_component_match - compare function for matching mei pxp.
 *
 *    The function checks if the driver is i915, the subcomponent is PXP
 *    and the grand parent of pxp and the parent of i915 are the same
 *    PCH device.
 *
 * @dev: master device
 * @subcomponent: subcomponent to match (I915_COMPONENT_PXP)
 * @data: compare data (mei pxp device)
 *
 * Return:
 * * 1 - if components match
 * * 0 - otherwise
 */
static int mei_pxp_component_match(struct device *dev, int subcomponent,
				   void *data)
{
	struct device *base = data;

	if (!dev->driver || strcmp(dev->driver->name, "i915") ||
	    subcomponent != I915_COMPONENT_PXP)
		return 0;

	base = base->parent;
	if (!base)
		return 0;

	base = base->parent;
	dev = dev->parent;

	return (base && dev && dev == base);
}

static int mei_pxp_probe(struct mei_cl_device *cldev,
			 const struct mei_cl_device_id *id)
{
	struct i915_pxp_component *comp_master;
	struct component_match *master_match;
	int ret;

	ret = mei_cldev_enable(cldev);
	if (ret < 0) {
		dev_err(&cldev->dev, "mei_cldev_enable Failed. %d\n", ret);
		goto enable_err_exit;
	}

	comp_master = kzalloc(sizeof(*comp_master), GFP_KERNEL);
	if (!comp_master) {
		ret = -ENOMEM;
		goto err_exit;
	}

	master_match = NULL;
	component_match_add_typed(&cldev->dev, &master_match,
				  mei_pxp_component_match, &cldev->dev);
	if (IS_ERR_OR_NULL(master_match)) {
		ret = -ENOMEM;
		goto err_exit;
	}

	mei_cldev_set_drvdata(cldev, comp_master);
	ret = component_master_add_with_match(&cldev->dev,
					      &mei_component_master_ops,
					      master_match);
	if (ret < 0) {
		dev_err(&cldev->dev, "Master comp add failed %d\n", ret);
		goto err_exit;
	}

	return 0;

err_exit:
	mei_cldev_set_drvdata(cldev, NULL);
	kfree(comp_master);
	mei_cldev_disable(cldev);
enable_err_exit:
	return ret;
}

static void mei_pxp_remove(struct mei_cl_device *cldev)
{
	struct i915_pxp_component *comp_master = mei_cldev_get_drvdata(cldev);
	int ret;

	component_master_del(&cldev->dev, &mei_component_master_ops);
	kfree(comp_master);
	mei_cldev_set_drvdata(cldev, NULL);

	ret = mei_cldev_disable(cldev);
	if (ret)
		dev_warn(&cldev->dev, "mei_cldev_disable() failed\n");
}

/* fbf6fcf1-96cf-4e2e-a6a6-1bab8cbe36b1 : PAVP GUID*/
#define MEI_GUID_PXP GUID_INIT(0xfbf6fcf1, 0x96cf, 0x4e2e, 0xA6, \
			       0xa6, 0x1b, 0xab, 0x8c, 0xbe, 0x36, 0xb1)

static struct mei_cl_device_id mei_pxp_tbl[] = {
	{ .uuid = MEI_GUID_PXP, .version = MEI_CL_VERSION_ANY },
	{ }
};
MODULE_DEVICE_TABLE(mei, mei_pxp_tbl);

static struct mei_cl_driver mei_pxp_driver = {
	.id_table = mei_pxp_tbl,
	.name = KBUILD_MODNAME,
	.probe = mei_pxp_probe,
	.remove	= mei_pxp_remove,
};

module_mei_cl_driver(mei_pxp_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MEI PXP");
