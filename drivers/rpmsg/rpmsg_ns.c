// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2020 - All Rights Reserved
 */
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/rpmsg/ns.h>
#include <linux/slab.h>

#include "rpmsg_internal.h"

/**
 * rpmsg_ns_register_device() - register name service device based on rpdev
 * @rpdev: prepared rpdev to be used for creating endpoints
 *
 * This function wraps rpmsg_register_device() preparing the rpdev for use as
 * basis for the rpmsg name service device.
 */
int rpmsg_ns_register_device(struct rpmsg_device *rpdev)
{
	rpdev->src = RPMSG_NS_ADDR;
	rpdev->dst = RPMSG_NS_ADDR;

	return rpmsg_register_device_override(rpdev, "rpmsg_ns");
}
EXPORT_SYMBOL(rpmsg_ns_register_device);

/* invoked when a name service announcement arrives */
static int rpmsg_ns_cb(struct rpmsg_device *rpdev, void *data, int len,
		       void *priv, u32 src)
{
	struct rpmsg_ns_msg *msg = data;
	struct rpmsg_device *newch;
	struct rpmsg_channel_info chinfo;
	struct device *dev = rpdev->dev.parent;
	int ret;

#if defined(CONFIG_DYNAMIC_DEBUG)
	dynamic_hex_dump("NS announcement: ", DUMP_PREFIX_NONE, 16, 1,
			 data, len, true);
#endif

	if (len != sizeof(*msg)) {
		dev_err(dev, "malformed ns msg (%d)\n", len);
		return -EINVAL;
	}

	/* don't trust the remote processor for null terminating the name */
	msg->name[RPMSG_NAME_SIZE - 1] = '\0';

	strncpy(chinfo.name, msg->name, sizeof(chinfo.name));
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpmsg32_to_cpu(rpdev, msg->addr);

	dev_info(dev, "%sing channel %s addr 0x%x\n",
		 rpmsg32_to_cpu(rpdev, msg->flags) & RPMSG_NS_DESTROY ?
		 "destroy" : "creat", msg->name, chinfo.dst);

	if (rpmsg32_to_cpu(rpdev, msg->flags) & RPMSG_NS_DESTROY) {
		ret = rpmsg_release_channel(rpdev, &chinfo);
		if (ret)
			dev_err(dev, "rpmsg_destroy_channel failed: %d\n", ret);
	} else {
		newch = rpmsg_create_channel(rpdev, &chinfo);
		if (!newch)
			dev_err(dev, "rpmsg_create_channel failed\n");
	}

	return 0;
}

static int rpmsg_ns_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_endpoint *ns_ept;
	struct rpmsg_channel_info ns_chinfo = {
		.src = RPMSG_NS_ADDR,
		.dst = RPMSG_NS_ADDR,
		.name = "name_service",
	};

	/*
	 * Create the NS announcement service endpoint associated to the RPMsg
	 * device. The endpoint will be automatically destroyed when the RPMsg
	 * device will be deleted.
	 */
	ns_ept = rpmsg_create_ept(rpdev, rpmsg_ns_cb, NULL, ns_chinfo);
	if (!ns_ept) {
		dev_err(&rpdev->dev, "failed to create the ns ept\n");
		return -ENOMEM;
	}
	rpdev->ept = ns_ept;

	return 0;
}

static struct rpmsg_driver rpmsg_ns_driver = {
	.drv.name = KBUILD_MODNAME,
	.probe = rpmsg_ns_probe,
};

static int rpmsg_ns_init(void)
{
	int ret;

	ret = register_rpmsg_driver(&rpmsg_ns_driver);
	if (ret < 0)
		pr_err("%s: Failed to register rpmsg driver\n", __func__);

	return ret;
}
postcore_initcall(rpmsg_ns_init);

static void rpmsg_ns_exit(void)
{
	unregister_rpmsg_driver(&rpmsg_ns_driver);
}
module_exit(rpmsg_ns_exit);

MODULE_DESCRIPTION("Name service announcement rpmsg driver");
MODULE_AUTHOR("Arnaud Pouliquen <arnaud.pouliquen@st.com>");
MODULE_ALIAS("rpmsg:" KBUILD_MODNAME);
MODULE_LICENSE("GPL v2");
