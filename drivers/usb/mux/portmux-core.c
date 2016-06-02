/**
 * portmux-core.c - USB Port Mux support
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/err.h>
#include <linux/usb/portmux.h>

static int usb_mux_change_state(struct portmux_dev *pdev,
				enum portmux_role role)
{
	struct device *dev = &pdev->dev;
	int ret = -EINVAL;

	dev_WARN_ONCE(dev,
		      !mutex_is_locked(&pdev->mux_mutex),
		      "mutex is unlocked\n");

	switch (role) {
	case PORTMUX_HOST:
		if (pdev->desc->ops->set_host_cb)
			ret = pdev->desc->ops->set_host_cb(pdev->dev.parent);
		break;
	case PORTMUX_DEVICE:
		if (pdev->desc->ops->set_device_cb)
			ret = pdev->desc->ops->set_device_cb(pdev->dev.parent);
		break;
	default:
		break;
	}

	if (!ret)
		pdev->mux_state = role;

	return ret;
}

static const char * const role_name[] = {
	"unknown",	/* PORTMUX_UNKNOWN */
	"host",		/* PORTMUX_HOST */
	"peripheral"	/* PORTMUX_DEVICE */
};

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct portmux_dev *pdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", role_name[pdev->mux_state]);
}

static ssize_t state_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct portmux_dev *pdev = dev_get_drvdata(dev);
	enum portmux_role role;

	if (sysfs_streq(buf, "peripheral"))
		role = PORTMUX_DEVICE;
	else if (sysfs_streq(buf, "host"))
		role = PORTMUX_HOST;
	else
		return -EINVAL;

	mutex_lock(&pdev->mux_mutex);
	usb_mux_change_state(pdev, role);
	mutex_unlock(&pdev->mux_mutex);

	return count;
}
static DEVICE_ATTR_RW(state);

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct portmux_dev *pdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", pdev->desc->name);
}
static DEVICE_ATTR_RO(name);

static struct attribute *portmux_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_name.attr,
	NULL,
};

static struct attribute_group portmux_attr_grp = {
	.attrs = portmux_attrs,
};

static const struct attribute_group *portmux_group[] = {
	&portmux_attr_grp,
	NULL,
};

static void portmux_dev_release(struct device *dev)
{
	dev_vdbg(dev, "%s\n", __func__);
}

/**
 * portmux_register - register a port mux
 * @dev: device the mux belongs to
 * @desc: the descriptor of this port mux
 *
 * Called by port mux drivers to register a mux. Returns a valid
 * pointer to struct portmux_dev on success or an ERR_PTR() on
 * error.
 */
struct portmux_dev *portmux_register(struct portmux_desc *desc)
{
	static atomic_t portmux_no = ATOMIC_INIT(-1);
	struct portmux_dev *pdev;
	int ret;

	/* parameter sanity check */
	if (!desc || !desc->name || !desc->ops || !desc->dev)
		return ERR_PTR(-EINVAL);

	pdev = kzalloc(sizeof(*pdev), GFP_KERNEL);
	if (!pdev)
		return ERR_PTR(-ENOMEM);

	mutex_init(&pdev->mux_mutex);
	pdev->desc = desc;
	pdev->dev.parent = desc->dev;
	pdev->dev.release = portmux_dev_release;
	dev_set_name(&pdev->dev, "portmux.%lu",
		     (unsigned long)atomic_inc_return(&portmux_no));
	pdev->dev.groups = portmux_group;
	ret = device_register(&pdev->dev);
	if (ret) {
		kfree(pdev);
		return ERR_PTR(ret);
	}

	dev_set_drvdata(&pdev->dev, pdev);

	return pdev;
}
EXPORT_SYMBOL_GPL(portmux_register);

/**
 * portmux_unregister - unregister a port mux
 * @pdev: the port mux device
 *
 * Called by port mux drivers to release a mux.
 */
void portmux_unregister(struct portmux_dev *pdev)
{
	device_unregister(&pdev->dev);
	kfree(pdev);
}
EXPORT_SYMBOL_GPL(portmux_unregister);

/**
 * portmux_switch - switch the port role
 * @pdev: the port mux device
 * @role: the target role
 *
 * Called by other components to switch the port role.
 */
int portmux_switch(struct portmux_dev *pdev, enum portmux_role role)
{
	int ret;

	mutex_lock(&pdev->mux_mutex);
	ret = usb_mux_change_state(pdev, role);
	mutex_unlock(&pdev->mux_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(portmux_switch);

#ifdef CONFIG_PM_SLEEP
/**
 * portmux_complete - refresh port state during system resumes back
 * @pdev: the port mux device
 *
 * Called by port mux drivers to refresh port state during system
 * resumes back.
 */
void portmux_complete(struct portmux_dev *pdev)
{
	mutex_lock(&pdev->mux_mutex);
	usb_mux_change_state(pdev, pdev->mux_state);
	mutex_unlock(&pdev->mux_mutex);
}
EXPORT_SYMBOL_GPL(portmux_complete);
#endif
