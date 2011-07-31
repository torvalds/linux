/*
 * Ultra Wide Band
 * Life cycle of radio controllers
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 *
 * A UWB radio controller is also a UWB device, so it embeds one...
 *
 * List of RCs comes from the 'struct class uwb_rc_class'.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/random.h>
#include <linux/kdev_t.h>
#include <linux/etherdevice.h>
#include <linux/usb.h>
#include <linux/slab.h>

#include "uwb-internal.h"

static int uwb_rc_index_match(struct device *dev, void *data)
{
	int *index = data;
	struct uwb_rc *rc = dev_get_drvdata(dev);

	if (rc->index == *index)
		return 1;
	return 0;
}

static struct uwb_rc *uwb_rc_find_by_index(int index)
{
	struct device *dev;
	struct uwb_rc *rc = NULL;

	dev = class_find_device(&uwb_rc_class, NULL, &index, uwb_rc_index_match);
	if (dev)
		rc = dev_get_drvdata(dev);
	return rc;
}

static int uwb_rc_new_index(void)
{
	int index = 0;

	for (;;) {
		if (!uwb_rc_find_by_index(index))
			return index;
		if (++index < 0)
			index = 0;
	}
}

/**
 * Release the backing device of a uwb_rc that has been dynamically allocated.
 */
static void uwb_rc_sys_release(struct device *dev)
{
	struct uwb_dev *uwb_dev = container_of(dev, struct uwb_dev, dev);
	struct uwb_rc *rc = container_of(uwb_dev, struct uwb_rc, uwb_dev);

	uwb_rc_ie_release(rc);
	kfree(rc);
}


void uwb_rc_init(struct uwb_rc *rc)
{
	struct uwb_dev *uwb_dev = &rc->uwb_dev;

	uwb_dev_init(uwb_dev);
	rc->uwb_dev.dev.class = &uwb_rc_class;
	rc->uwb_dev.dev.release = uwb_rc_sys_release;
	uwb_rc_neh_create(rc);
	rc->beaconing = -1;
	rc->scan_type = UWB_SCAN_DISABLED;
	INIT_LIST_HEAD(&rc->notifs_chain.list);
	mutex_init(&rc->notifs_chain.mutex);
	INIT_LIST_HEAD(&rc->uwb_beca.list);
	mutex_init(&rc->uwb_beca.mutex);
	uwb_drp_avail_init(rc);
	uwb_rc_ie_init(rc);
	uwb_rsv_init(rc);
	uwb_rc_pal_init(rc);
}
EXPORT_SYMBOL_GPL(uwb_rc_init);


struct uwb_rc *uwb_rc_alloc(void)
{
	struct uwb_rc *rc;
	rc = kzalloc(sizeof(*rc), GFP_KERNEL);
	if (rc == NULL)
		return NULL;
	uwb_rc_init(rc);
	return rc;
}
EXPORT_SYMBOL_GPL(uwb_rc_alloc);

static struct attribute *rc_attrs[] = {
		&dev_attr_mac_address.attr,
		&dev_attr_scan.attr,
		&dev_attr_beacon.attr,
		NULL,
};

static struct attribute_group rc_attr_group = {
	.attrs = rc_attrs,
};

/*
 * Registration of sysfs specific stuff
 */
static int uwb_rc_sys_add(struct uwb_rc *rc)
{
	return sysfs_create_group(&rc->uwb_dev.dev.kobj, &rc_attr_group);
}


static void __uwb_rc_sys_rm(struct uwb_rc *rc)
{
	sysfs_remove_group(&rc->uwb_dev.dev.kobj, &rc_attr_group);
}

/**
 * uwb_rc_mac_addr_setup - get an RC's EUI-48 address or set it
 * @rc:  the radio controller.
 *
 * If the EUI-48 address is 00:00:00:00:00:00 or FF:FF:FF:FF:FF:FF
 * then a random locally administered EUI-48 is generated and set on
 * the device.  The probability of address collisions is sufficiently
 * unlikely (1/2^40 = 9.1e-13) that they're not checked for.
 */
static
int uwb_rc_mac_addr_setup(struct uwb_rc *rc)
{
	int result;
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_dev *uwb_dev = &rc->uwb_dev;
	char devname[UWB_ADDR_STRSIZE];
	struct uwb_mac_addr addr;

	result = uwb_rc_mac_addr_get(rc, &addr);
	if (result < 0) {
		dev_err(dev, "cannot retrieve UWB EUI-48 address: %d\n", result);
		return result;
	}

	if (uwb_mac_addr_unset(&addr) || uwb_mac_addr_bcast(&addr)) {
		addr.data[0] = 0x02; /* locally adminstered and unicast */
		get_random_bytes(&addr.data[1], sizeof(addr.data)-1);

		result = uwb_rc_mac_addr_set(rc, &addr);
		if (result < 0) {
			uwb_mac_addr_print(devname, sizeof(devname), &addr);
			dev_err(dev, "cannot set EUI-48 address %s: %d\n",
				devname, result);
			return result;
		}
	}
	uwb_dev->mac_addr = addr;
	return 0;
}



static int uwb_rc_setup(struct uwb_rc *rc)
{
	int result;
	struct device *dev = &rc->uwb_dev.dev;

	result = uwb_radio_setup(rc);
	if (result < 0) {
		dev_err(dev, "cannot setup UWB radio: %d\n", result);
		goto error;
	}
	result = uwb_rc_mac_addr_setup(rc);
	if (result < 0) {
		dev_err(dev, "cannot setup UWB MAC address: %d\n", result);
		goto error;
	}
	result = uwb_rc_dev_addr_assign(rc);
	if (result < 0) {
		dev_err(dev, "cannot assign UWB DevAddr: %d\n", result);
		goto error;
	}
	result = uwb_rc_ie_setup(rc);
	if (result < 0) {
		dev_err(dev, "cannot setup IE subsystem: %d\n", result);
		goto error_ie_setup;
	}
	result = uwb_rsv_setup(rc);
	if (result < 0) {
		dev_err(dev, "cannot setup reservation subsystem: %d\n", result);
		goto error_rsv_setup;
	}
	uwb_dbg_add_rc(rc);
	return 0;

error_rsv_setup:
	uwb_rc_ie_release(rc);
error_ie_setup:
error:
	return result;
}


/**
 * Register a new UWB radio controller
 *
 * Did you call uwb_rc_init() on your rc?
 *
 * We assume that this is being called with a > 0 refcount on
 * it [through ops->{get|put}_device(). We'll take our own, though.
 *
 * @parent_dev is our real device, the one that provides the actual UWB device
 */
int uwb_rc_add(struct uwb_rc *rc, struct device *parent_dev, void *priv)
{
	int result;
	struct device *dev;
	char macbuf[UWB_ADDR_STRSIZE], devbuf[UWB_ADDR_STRSIZE];

	rc->index = uwb_rc_new_index();

	dev = &rc->uwb_dev.dev;
	dev_set_name(dev, "uwb%d", rc->index);

	rc->priv = priv;

	init_waitqueue_head(&rc->uwbd.wq);
	INIT_LIST_HEAD(&rc->uwbd.event_list);
	spin_lock_init(&rc->uwbd.event_list_lock);

	uwbd_start(rc);

	result = rc->start(rc);
	if (result < 0)
		goto error_rc_start;

	result = uwb_rc_setup(rc);
	if (result < 0) {
		dev_err(dev, "cannot setup UWB radio controller: %d\n", result);
		goto error_rc_setup;
	}

	result = uwb_dev_add(&rc->uwb_dev, parent_dev, rc);
	if (result < 0 && result != -EADDRNOTAVAIL)
		goto error_dev_add;

	result = uwb_rc_sys_add(rc);
	if (result < 0) {
		dev_err(parent_dev, "cannot register UWB radio controller "
			"dev attributes: %d\n", result);
		goto error_sys_add;
	}

	uwb_mac_addr_print(macbuf, sizeof(macbuf), &rc->uwb_dev.mac_addr);
	uwb_dev_addr_print(devbuf, sizeof(devbuf), &rc->uwb_dev.dev_addr);
	dev_info(dev,
		 "new uwb radio controller (mac %s dev %s) on %s %s\n",
		 macbuf, devbuf, parent_dev->bus->name, dev_name(parent_dev));
	rc->ready = 1;
	return 0;

error_sys_add:
	uwb_dev_rm(&rc->uwb_dev);
error_dev_add:
error_rc_setup:
	rc->stop(rc);
error_rc_start:
	uwbd_stop(rc);
	return result;
}
EXPORT_SYMBOL_GPL(uwb_rc_add);


static int uwb_dev_offair_helper(struct device *dev, void *priv)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	return __uwb_dev_offair(uwb_dev, uwb_dev->rc);
}

/*
 * Remove a Radio Controller; stop beaconing/scanning, disconnect all children
 */
void uwb_rc_rm(struct uwb_rc *rc)
{
	rc->ready = 0;

	uwb_dbg_del_rc(rc);
	uwb_rsv_remove_all(rc);
	uwb_radio_shutdown(rc);

	rc->stop(rc);

	uwbd_stop(rc);
	uwb_rc_neh_destroy(rc);

	uwb_dev_lock(&rc->uwb_dev);
	rc->priv = NULL;
	rc->cmd = NULL;
	uwb_dev_unlock(&rc->uwb_dev);
	mutex_lock(&rc->uwb_beca.mutex);
	uwb_dev_for_each(rc, uwb_dev_offair_helper, NULL);
	__uwb_rc_sys_rm(rc);
	mutex_unlock(&rc->uwb_beca.mutex);
	uwb_rsv_cleanup(rc);
 	uwb_beca_release(rc);
	uwb_dev_rm(&rc->uwb_dev);
}
EXPORT_SYMBOL_GPL(uwb_rc_rm);

static int find_rc_try_get(struct device *dev, void *data)
{
	struct uwb_rc *target_rc = data;
	struct uwb_rc *rc = dev_get_drvdata(dev);

	if (rc == NULL) {
		WARN_ON(1);
		return 0;
	}
	if (rc == target_rc) {
		if (rc->ready == 0)
			return 0;
		else
			return 1;
	}
	return 0;
}

/**
 * Given a radio controller descriptor, validate and refcount it
 *
 * @returns NULL if the rc does not exist or is quiescing; the ptr to
 *               it otherwise.
 */
struct uwb_rc *__uwb_rc_try_get(struct uwb_rc *target_rc)
{
	struct device *dev;
	struct uwb_rc *rc = NULL;

	dev = class_find_device(&uwb_rc_class, NULL, target_rc,
				find_rc_try_get);
	if (dev) {
		rc = dev_get_drvdata(dev);
		__uwb_rc_get(rc);
	}
	return rc;
}
EXPORT_SYMBOL_GPL(__uwb_rc_try_get);

/*
 * RC get for external refcount acquirers...
 *
 * Increments the refcount of the device and it's backend modules
 */
static inline struct uwb_rc *uwb_rc_get(struct uwb_rc *rc)
{
	if (rc->ready == 0)
		return NULL;
	uwb_dev_get(&rc->uwb_dev);
	return rc;
}

static int find_rc_grandpa(struct device *dev, void *data)
{
	struct device *grandpa_dev = data;
	struct uwb_rc *rc = dev_get_drvdata(dev);

	if (rc->uwb_dev.dev.parent->parent == grandpa_dev) {
		rc = uwb_rc_get(rc);
		return 1;
	}
	return 0;
}

/**
 * Locate and refcount a radio controller given a common grand-parent
 *
 * @grandpa_dev  Pointer to the 'grandparent' device structure.
 * @returns NULL If the rc does not exist or is quiescing; the ptr to
 *               it otherwise, properly referenced.
 *
 * The Radio Control interface (or the UWB Radio Controller) is always
 * an interface of a device. The parent is the interface, the
 * grandparent is the device that encapsulates the interface.
 *
 * There is no need to lock around as the "grandpa" would be
 * refcounted by the target, and to remove the referemes, the
 * uwb_rc_class->sem would have to be taken--we hold it, ergo we
 * should be safe.
 */
struct uwb_rc *uwb_rc_get_by_grandpa(const struct device *grandpa_dev)
{
	struct device *dev;
	struct uwb_rc *rc = NULL;

	dev = class_find_device(&uwb_rc_class, NULL, (void *)grandpa_dev,
				find_rc_grandpa);
	if (dev)
		rc = dev_get_drvdata(dev);
	return rc;
}
EXPORT_SYMBOL_GPL(uwb_rc_get_by_grandpa);

/**
 * Find a radio controller by device address
 *
 * @returns the pointer to the radio controller, properly referenced
 */
static int find_rc_dev(struct device *dev, void *data)
{
	struct uwb_dev_addr *addr = data;
	struct uwb_rc *rc = dev_get_drvdata(dev);

	if (rc == NULL) {
		WARN_ON(1);
		return 0;
	}
	if (!uwb_dev_addr_cmp(&rc->uwb_dev.dev_addr, addr)) {
		rc = uwb_rc_get(rc);
		return 1;
	}
	return 0;
}

struct uwb_rc *uwb_rc_get_by_dev(const struct uwb_dev_addr *addr)
{
	struct device *dev;
	struct uwb_rc *rc = NULL;

	dev = class_find_device(&uwb_rc_class, NULL, (void *)addr,
				find_rc_dev);
	if (dev)
		rc = dev_get_drvdata(dev);

	return rc;
}
EXPORT_SYMBOL_GPL(uwb_rc_get_by_dev);

/**
 * Drop a reference on a radio controller
 *
 * This is the version that should be done by entities external to the
 * UWB Radio Control stack (ie: clients of the API).
 */
void uwb_rc_put(struct uwb_rc *rc)
{
	__uwb_rc_put(rc);
}
EXPORT_SYMBOL_GPL(uwb_rc_put);
