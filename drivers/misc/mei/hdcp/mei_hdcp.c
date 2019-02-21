// SPDX-License-Identifier: (GPL-2.0)
/*
 * Copyright © 2019 Intel Corporation
 *
 * Mei_hdcp.c: HDCP client driver for mei bus
 *
 * Author:
 * Ramalingam C <ramalingam.c@intel.com>
 */

/**
 * DOC: MEI_HDCP Client Driver
 *
 * This is a client driver to the mei_bus to make the HDCP2.2 services of
 * ME FW available for the interested consumers like I915.
 *
 * This module will act as a translation layer between HDCP protocol
 * implementor(I915) and ME FW by translating HDCP2.2 authentication
 * messages to ME FW command payloads and vice versa.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/mei_cl_bus.h>

static int mei_hdcp_probe(struct mei_cl_device *cldev,
			  const struct mei_cl_device_id *id)
{
	int ret;

	ret = mei_cldev_enable(cldev);
	if (ret < 0)
		dev_err(&cldev->dev, "mei_cldev_enable Failed. %d\n", ret);

	return ret;
}

static int mei_hdcp_remove(struct mei_cl_device *cldev)
{
	return mei_cldev_disable(cldev);
}

#define MEI_UUID_HDCP GUID_INIT(0xB638AB7E, 0x94E2, 0x4EA2, 0xA5, \
				0x52, 0xD1, 0xC5, 0x4B, 0x62, 0x7F, 0x04)

static struct mei_cl_device_id mei_hdcp_tbl[] = {
	{ .uuid = MEI_UUID_HDCP, .version = MEI_CL_VERSION_ANY },
	{ }
};
MODULE_DEVICE_TABLE(mei, mei_hdcp_tbl);

static struct mei_cl_driver mei_hdcp_driver = {
	.id_table = mei_hdcp_tbl,
	.name = KBUILD_MODNAME,
	.probe = mei_hdcp_probe,
	.remove	= mei_hdcp_remove,
};

module_mei_cl_driver(mei_hdcp_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MEI HDCP");
