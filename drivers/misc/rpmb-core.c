// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2015 - 2019 Intel Corporation. All rights reserved.
 * Copyright(c) 2021 - 2024 Linaro Ltd.
 */
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rpmb.h>
#include <linux/slab.h>

static DEFINE_IDA(rpmb_ida);
static DEFINE_MUTEX(rpmb_mutex);

/**
 * rpmb_dev_get() - increase rpmb device ref counter
 * @rdev: rpmb device
 */
struct rpmb_dev *rpmb_dev_get(struct rpmb_dev *rdev)
{
	if (rdev)
		get_device(&rdev->dev);
	return rdev;
}
EXPORT_SYMBOL_GPL(rpmb_dev_get);

/**
 * rpmb_dev_put() - decrease rpmb device ref counter
 * @rdev: rpmb device
 */
void rpmb_dev_put(struct rpmb_dev *rdev)
{
	if (rdev)
		put_device(&rdev->dev);
}
EXPORT_SYMBOL_GPL(rpmb_dev_put);

/**
 * rpmb_route_frames() - route rpmb frames to rpmb device
 * @rdev:	rpmb device
 * @req:	rpmb request frames
 * @req_len:	length of rpmb request frames in bytes
 * @rsp:	rpmb response frames
 * @rsp_len:	length of rpmb response frames in bytes
 *
 * Returns: < 0 on failure
 */
int rpmb_route_frames(struct rpmb_dev *rdev, u8 *req,
		      unsigned int req_len, u8 *rsp, unsigned int rsp_len)
{
	if (!req || !req_len || !rsp || !rsp_len)
		return -EINVAL;

	return rdev->descr.route_frames(rdev->dev.parent, req, req_len,
					rsp, rsp_len);
}
EXPORT_SYMBOL_GPL(rpmb_route_frames);

static void rpmb_dev_release(struct device *dev)
{
	struct rpmb_dev *rdev = to_rpmb_dev(dev);

	mutex_lock(&rpmb_mutex);
	ida_simple_remove(&rpmb_ida, rdev->id);
	mutex_unlock(&rpmb_mutex);
	kfree(rdev->descr.dev_id);
	kfree(rdev);
}

static struct class rpmb_class = {
	.name = "rpmb",
	.dev_release = rpmb_dev_release,
};

/**
 * rpmb_dev_find_device() - return first matching rpmb device
 * @start: rpmb device to begin with
 * @data: data for the match function
 * @match: the matching function
 *
 * Iterate over registered RPMB devices, and call @match() for each passing
 * it the RPMB device and @data.
 *
 * The return value of @match() is checked for each call. If it returns
 * anything other 0, break and return the found RPMB device.
 *
 * It's the callers responsibility to call rpmb_dev_put() on the returned
 * device, when it's done with it.
 *
 * Returns: a matching rpmb device or NULL on failure
 */
struct rpmb_dev *rpmb_dev_find_device(const void *data,
				      const struct rpmb_dev *start,
				      int (*match)(struct device *dev,
						   const void *data))
{
	struct device *dev;
	const struct device *start_dev = NULL;

	if (start)
		start_dev = &start->dev;
	dev = class_find_device(&rpmb_class, start_dev, data, match);

	return dev ? to_rpmb_dev(dev) : NULL;
}
EXPORT_SYMBOL_GPL(rpmb_dev_find_device);

int rpmb_interface_register(struct class_interface *intf)
{
	intf->class = &rpmb_class;

	return class_interface_register(intf);
}
EXPORT_SYMBOL_GPL(rpmb_interface_register);

void rpmb_interface_unregister(struct class_interface *intf)
{
	class_interface_unregister(intf);
}
EXPORT_SYMBOL_GPL(rpmb_interface_unregister);

/**
 * rpmb_dev_unregister() - unregister RPMB partition from the RPMB subsystem
 * @rdev: the rpmb device to unregister
 *
 * This function should be called from the release function of the
 * underlying device used when the RPMB device was registered.
 *
 * Returns: < 0 on failure
 */
int rpmb_dev_unregister(struct rpmb_dev *rdev)
{
	if (!rdev)
		return -EINVAL;

	device_del(&rdev->dev);

	rpmb_dev_put(rdev);

	return 0;
}
EXPORT_SYMBOL_GPL(rpmb_dev_unregister);

/**
 * rpmb_dev_register - register RPMB partition with the RPMB subsystem
 * @dev: storage device of the rpmb device
 * @descr: RPMB device description
 *
 * While registering the RPMB partition extract needed device information
 * while needed resources are available.
 *
 * Returns: a pointer to a 'struct rpmb_dev' or an ERR_PTR on failure
 */
struct rpmb_dev *rpmb_dev_register(struct device *dev,
				   struct rpmb_descr *descr)
{
	struct rpmb_dev *rdev;
	int ret;

	if (!dev || !descr || !descr->route_frames || !descr->dev_id ||
	    !descr->dev_id_len)
		return ERR_PTR(-EINVAL);

	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (!rdev)
		return ERR_PTR(-ENOMEM);
	rdev->descr = *descr;
	rdev->descr.dev_id = kmemdup(descr->dev_id, descr->dev_id_len,
				     GFP_KERNEL);
	if (!rdev->descr.dev_id) {
		ret = -ENOMEM;
		goto err_free_rdev;
	}

	mutex_lock(&rpmb_mutex);
	ret = ida_simple_get(&rpmb_ida, 0, 0, GFP_KERNEL);
	mutex_unlock(&rpmb_mutex);
	if (ret < 0)
		goto err_free_dev_id;
	rdev->id = ret;

	dev_set_name(&rdev->dev, "rpmb%d", rdev->id);
	rdev->dev.class = &rpmb_class;
	rdev->dev.parent = dev;

	ret = device_register(&rdev->dev);
	if (ret) {
		put_device(&rdev->dev);
		return ERR_PTR(ret);
	}

	dev_dbg(&rdev->dev, "registered device\n");

	return rdev;

err_free_dev_id:
	kfree(rdev->descr.dev_id);
err_free_rdev:
	kfree(rdev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(rpmb_dev_register);

static int __init rpmb_init(void)
{
	int ret;

	ret = class_register(&rpmb_class);
	if (ret) {
		pr_err("couldn't create class\n");
		return ret;
	}
	ida_init(&rpmb_ida);
	return 0;
}

static void __exit rpmb_exit(void)
{
	ida_destroy(&rpmb_ida);
	class_unregister(&rpmb_class);
}

subsys_initcall(rpmb_init);
module_exit(rpmb_exit);

MODULE_AUTHOR("Jens Wiklander <jens.wiklander@linaro.org>");
MODULE_DESCRIPTION("RPMB class");
MODULE_LICENSE("GPL");
