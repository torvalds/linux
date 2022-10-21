// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt/USB4 retimer support.
 *
 * Copyright (C) 2020, Intel Corporation
 * Authors: Kranthi Kuntala <kranthi.kuntala@intel.com>
 *	    Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/sched/signal.h>

#include "sb_regs.h"
#include "tb.h"

#define TB_MAX_RETIMER_INDEX	6

/**
 * tb_retimer_nvm_read() - Read contents of retimer NVM
 * @rt: Retimer device
 * @address: NVM address (in bytes) to start reading
 * @buf: Data read from NVM is stored here
 * @size: Number of bytes to read
 *
 * Reads retimer NVM and copies the contents to @buf. Returns %0 if the
 * read was successful and negative errno in case of failure.
 */
int tb_retimer_nvm_read(struct tb_retimer *rt, unsigned int address, void *buf,
			size_t size)
{
	return usb4_port_retimer_nvm_read(rt->port, rt->index, address, buf, size);
}

static int nvm_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct tb_nvm *nvm = priv;
	struct tb_retimer *rt = tb_to_retimer(nvm->dev);
	int ret;

	pm_runtime_get_sync(&rt->dev);

	if (!mutex_trylock(&rt->tb->lock)) {
		ret = restart_syscall();
		goto out;
	}

	ret = tb_retimer_nvm_read(rt, offset, val, bytes);
	mutex_unlock(&rt->tb->lock);

out:
	pm_runtime_mark_last_busy(&rt->dev);
	pm_runtime_put_autosuspend(&rt->dev);

	return ret;
}

static int nvm_write(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct tb_nvm *nvm = priv;
	struct tb_retimer *rt = tb_to_retimer(nvm->dev);
	int ret = 0;

	if (!mutex_trylock(&rt->tb->lock))
		return restart_syscall();

	ret = tb_nvm_write_buf(nvm, offset, val, bytes);
	mutex_unlock(&rt->tb->lock);

	return ret;
}

static int tb_retimer_nvm_add(struct tb_retimer *rt)
{
	struct tb_nvm *nvm;
	int ret;

	nvm = tb_nvm_alloc(&rt->dev);
	if (IS_ERR(nvm)) {
		ret = PTR_ERR(nvm) == -EOPNOTSUPP ? 0 : PTR_ERR(nvm);
		goto err_nvm;
	}

	ret = tb_nvm_read_version(nvm);
	if (ret)
		goto err_nvm;

	ret = tb_nvm_add_active(nvm, nvm_read);
	if (ret)
		goto err_nvm;

	ret = tb_nvm_add_non_active(nvm, nvm_write);
	if (ret)
		goto err_nvm;

	rt->nvm = nvm;
	return 0;

err_nvm:
	dev_dbg(&rt->dev, "NVM upgrade disabled\n");
	if (!IS_ERR(nvm))
		tb_nvm_free(nvm);

	return ret;
}

static int tb_retimer_nvm_validate_and_write(struct tb_retimer *rt)
{
	unsigned int image_size;
	const u8 *buf;
	int ret;

	ret = tb_nvm_validate(rt->nvm);
	if (ret)
		return ret;

	buf = rt->nvm->buf_data_start;
	image_size = rt->nvm->buf_data_size;

	ret = usb4_port_retimer_nvm_write(rt->port, rt->index, 0, buf,
					 image_size);
	if (ret)
		return ret;

	rt->nvm->flushed = true;
	return 0;
}

static int tb_retimer_nvm_authenticate(struct tb_retimer *rt, bool auth_only)
{
	u32 status;
	int ret;

	if (auth_only) {
		ret = usb4_port_retimer_nvm_set_offset(rt->port, rt->index, 0);
		if (ret)
			return ret;
	}

	ret = usb4_port_retimer_nvm_authenticate(rt->port, rt->index);
	if (ret)
		return ret;

	usleep_range(100, 150);

	/*
	 * Check the status now if we still can access the retimer. It
	 * is expected that the below fails.
	 */
	ret = usb4_port_retimer_nvm_authenticate_status(rt->port, rt->index,
							&status);
	if (!ret) {
		rt->auth_status = status;
		return status ? -EINVAL : 0;
	}

	return 0;
}

static ssize_t device_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct tb_retimer *rt = tb_to_retimer(dev);

	return sysfs_emit(buf, "%#x\n", rt->device);
}
static DEVICE_ATTR_RO(device);

static ssize_t nvm_authenticate_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tb_retimer *rt = tb_to_retimer(dev);
	int ret;

	if (!mutex_trylock(&rt->tb->lock))
		return restart_syscall();

	if (!rt->nvm)
		ret = -EAGAIN;
	else if (rt->no_nvm_upgrade)
		ret = -EOPNOTSUPP;
	else
		ret = sysfs_emit(buf, "%#x\n", rt->auth_status);

	mutex_unlock(&rt->tb->lock);

	return ret;
}

static ssize_t nvm_authenticate_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tb_retimer *rt = tb_to_retimer(dev);
	int val, ret;

	pm_runtime_get_sync(&rt->dev);

	if (!mutex_trylock(&rt->tb->lock)) {
		ret = restart_syscall();
		goto exit_rpm;
	}

	if (!rt->nvm) {
		ret = -EAGAIN;
		goto exit_unlock;
	}

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		goto exit_unlock;

	/* Always clear status */
	rt->auth_status = 0;

	if (val) {
		if (val == AUTHENTICATE_ONLY) {
			ret = tb_retimer_nvm_authenticate(rt, true);
		} else {
			if (!rt->nvm->flushed) {
				if (!rt->nvm->buf) {
					ret = -EINVAL;
					goto exit_unlock;
				}

				ret = tb_retimer_nvm_validate_and_write(rt);
				if (ret || val == WRITE_ONLY)
					goto exit_unlock;
			}
			if (val == WRITE_AND_AUTHENTICATE)
				ret = tb_retimer_nvm_authenticate(rt, false);
		}
	}

exit_unlock:
	mutex_unlock(&rt->tb->lock);
exit_rpm:
	pm_runtime_mark_last_busy(&rt->dev);
	pm_runtime_put_autosuspend(&rt->dev);

	if (ret)
		return ret;
	return count;
}
static DEVICE_ATTR_RW(nvm_authenticate);

static ssize_t nvm_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tb_retimer *rt = tb_to_retimer(dev);
	int ret;

	if (!mutex_trylock(&rt->tb->lock))
		return restart_syscall();

	if (!rt->nvm)
		ret = -EAGAIN;
	else
		ret = sysfs_emit(buf, "%x.%x\n", rt->nvm->major, rt->nvm->minor);

	mutex_unlock(&rt->tb->lock);
	return ret;
}
static DEVICE_ATTR_RO(nvm_version);

static ssize_t vendor_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct tb_retimer *rt = tb_to_retimer(dev);

	return sysfs_emit(buf, "%#x\n", rt->vendor);
}
static DEVICE_ATTR_RO(vendor);

static struct attribute *retimer_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_nvm_authenticate.attr,
	&dev_attr_nvm_version.attr,
	&dev_attr_vendor.attr,
	NULL
};

static const struct attribute_group retimer_group = {
	.attrs = retimer_attrs,
};

static const struct attribute_group *retimer_groups[] = {
	&retimer_group,
	NULL
};

static void tb_retimer_release(struct device *dev)
{
	struct tb_retimer *rt = tb_to_retimer(dev);

	kfree(rt);
}

struct device_type tb_retimer_type = {
	.name = "thunderbolt_retimer",
	.groups = retimer_groups,
	.release = tb_retimer_release,
};

static int tb_retimer_add(struct tb_port *port, u8 index, u32 auth_status)
{
	struct tb_retimer *rt;
	u32 vendor, device;
	int ret;

	ret = usb4_port_retimer_read(port, index, USB4_SB_VENDOR_ID, &vendor,
				     sizeof(vendor));
	if (ret) {
		if (ret != -ENODEV)
			tb_port_warn(port, "failed read retimer VendorId: %d\n", ret);
		return ret;
	}

	ret = usb4_port_retimer_read(port, index, USB4_SB_PRODUCT_ID, &device,
				     sizeof(device));
	if (ret) {
		if (ret != -ENODEV)
			tb_port_warn(port, "failed read retimer ProductId: %d\n", ret);
		return ret;
	}

	if (vendor != PCI_VENDOR_ID_INTEL && vendor != 0x8087) {
		tb_port_info(port, "retimer NVM format of vendor %#x is not supported\n",
			     vendor);
		return -EOPNOTSUPP;
	}

	/*
	 * Check that it supports NVM operations. If not then don't add
	 * the device at all.
	 */
	ret = usb4_port_retimer_nvm_sector_size(port, index);
	if (ret < 0)
		return ret;

	rt = kzalloc(sizeof(*rt), GFP_KERNEL);
	if (!rt)
		return -ENOMEM;

	rt->index = index;
	rt->vendor = vendor;
	rt->device = device;
	rt->auth_status = auth_status;
	rt->port = port;
	rt->tb = port->sw->tb;

	rt->dev.parent = &port->usb4->dev;
	rt->dev.bus = &tb_bus_type;
	rt->dev.type = &tb_retimer_type;
	dev_set_name(&rt->dev, "%s:%u.%u", dev_name(&port->sw->dev),
		     port->port, index);

	ret = device_register(&rt->dev);
	if (ret) {
		dev_err(&rt->dev, "failed to register retimer: %d\n", ret);
		put_device(&rt->dev);
		return ret;
	}

	ret = tb_retimer_nvm_add(rt);
	if (ret) {
		dev_err(&rt->dev, "failed to add NVM devices: %d\n", ret);
		device_unregister(&rt->dev);
		return ret;
	}

	dev_info(&rt->dev, "new retimer found, vendor=%#x device=%#x\n",
		 rt->vendor, rt->device);

	pm_runtime_no_callbacks(&rt->dev);
	pm_runtime_set_active(&rt->dev);
	pm_runtime_enable(&rt->dev);
	pm_runtime_set_autosuspend_delay(&rt->dev, TB_AUTOSUSPEND_DELAY);
	pm_runtime_mark_last_busy(&rt->dev);
	pm_runtime_use_autosuspend(&rt->dev);

	return 0;
}

static void tb_retimer_remove(struct tb_retimer *rt)
{
	dev_info(&rt->dev, "retimer disconnected\n");
	tb_nvm_free(rt->nvm);
	device_unregister(&rt->dev);
}

struct tb_retimer_lookup {
	const struct tb_port *port;
	u8 index;
};

static int retimer_match(struct device *dev, void *data)
{
	const struct tb_retimer_lookup *lookup = data;
	struct tb_retimer *rt = tb_to_retimer(dev);

	return rt && rt->port == lookup->port && rt->index == lookup->index;
}

static struct tb_retimer *tb_port_find_retimer(struct tb_port *port, u8 index)
{
	struct tb_retimer_lookup lookup = { .port = port, .index = index };
	struct device *dev;

	dev = device_find_child(&port->usb4->dev, &lookup, retimer_match);
	if (dev)
		return tb_to_retimer(dev);

	return NULL;
}

/**
 * tb_retimer_scan() - Scan for on-board retimers under port
 * @port: USB4 port to scan
 * @add: If true also registers found retimers
 *
 * Brings the sideband into a state where retimers can be accessed.
 * Then Tries to enumerate on-board retimers connected to @port. Found
 * retimers are registered as children of @port if @add is set.  Does
 * not scan for cable retimers for now.
 */
int tb_retimer_scan(struct tb_port *port, bool add)
{
	u32 status[TB_MAX_RETIMER_INDEX + 1] = {};
	int ret, i, last_idx = 0;
	struct usb4_port *usb4;

	usb4 = port->usb4;
	if (!usb4)
		return 0;

	pm_runtime_get_sync(&usb4->dev);

	/*
	 * Send broadcast RT to make sure retimer indices facing this
	 * port are set.
	 */
	ret = usb4_port_enumerate_retimers(port);
	if (ret)
		goto out;

	/*
	 * Enable sideband channel for each retimer. We can do this
	 * regardless whether there is device connected or not.
	 */
	for (i = 1; i <= TB_MAX_RETIMER_INDEX; i++)
		usb4_port_retimer_set_inbound_sbtx(port, i);

	/*
	 * Before doing anything else, read the authentication status.
	 * If the retimer has it set, store it for the new retimer
	 * device instance.
	 */
	for (i = 1; i <= TB_MAX_RETIMER_INDEX; i++)
		usb4_port_retimer_nvm_authenticate_status(port, i, &status[i]);

	for (i = 1; i <= TB_MAX_RETIMER_INDEX; i++) {
		/*
		 * Last retimer is true only for the last on-board
		 * retimer (the one connected directly to the Type-C
		 * port).
		 */
		ret = usb4_port_retimer_is_last(port, i);
		if (ret > 0)
			last_idx = i;
		else if (ret < 0)
			break;
	}

	if (!last_idx) {
		ret = 0;
		goto out;
	}

	/* Add on-board retimers if they do not exist already */
	for (i = 1; i <= last_idx; i++) {
		struct tb_retimer *rt;

		rt = tb_port_find_retimer(port, i);
		if (rt) {
			put_device(&rt->dev);
		} else if (add) {
			ret = tb_retimer_add(port, i, status[i]);
			if (ret && ret != -EOPNOTSUPP)
				break;
		}
	}

out:
	pm_runtime_mark_last_busy(&usb4->dev);
	pm_runtime_put_autosuspend(&usb4->dev);

	return ret;
}

static int remove_retimer(struct device *dev, void *data)
{
	struct tb_retimer *rt = tb_to_retimer(dev);
	struct tb_port *port = data;

	if (rt && rt->port == port)
		tb_retimer_remove(rt);
	return 0;
}

/**
 * tb_retimer_remove_all() - Remove all retimers under port
 * @port: USB4 port whose retimers to remove
 *
 * This removes all previously added retimers under @port.
 */
void tb_retimer_remove_all(struct tb_port *port)
{
	struct usb4_port *usb4;

	usb4 = port->usb4;
	if (usb4)
		device_for_each_child_reverse(&usb4->dev, port,
					      remove_retimer);
}
