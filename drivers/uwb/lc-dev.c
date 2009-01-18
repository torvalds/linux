/*
 * Ultra Wide Band
 * Life cycle of devices
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
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/random.h>
#include "uwb-internal.h"

/* We initialize addresses to 0xff (invalid, as it is bcast) */
static inline void uwb_dev_addr_init(struct uwb_dev_addr *addr)
{
	memset(&addr->data, 0xff, sizeof(addr->data));
}

static inline void uwb_mac_addr_init(struct uwb_mac_addr *addr)
{
	memset(&addr->data, 0xff, sizeof(addr->data));
}

/* @returns !0 if a device @addr is a broadcast address */
static inline int uwb_dev_addr_bcast(const struct uwb_dev_addr *addr)
{
	static const struct uwb_dev_addr bcast = { .data = { 0xff, 0xff } };
	return !uwb_dev_addr_cmp(addr, &bcast);
}

/*
 * Add callback @new to be called when an event occurs in @rc.
 */
int uwb_notifs_register(struct uwb_rc *rc, struct uwb_notifs_handler *new)
{
	if (mutex_lock_interruptible(&rc->notifs_chain.mutex))
		return -ERESTARTSYS;
	list_add(&new->list_node, &rc->notifs_chain.list);
	mutex_unlock(&rc->notifs_chain.mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(uwb_notifs_register);

/*
 * Remove event handler (callback)
 */
int uwb_notifs_deregister(struct uwb_rc *rc, struct uwb_notifs_handler *entry)
{
	if (mutex_lock_interruptible(&rc->notifs_chain.mutex))
		return -ERESTARTSYS;
	list_del(&entry->list_node);
	mutex_unlock(&rc->notifs_chain.mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(uwb_notifs_deregister);

/*
 * Notify all event handlers of a given event on @rc
 *
 * We are called with a valid reference to the device, or NULL if the
 * event is not for a particular event (e.g., a BG join event).
 */
void uwb_notify(struct uwb_rc *rc, struct uwb_dev *uwb_dev, enum uwb_notifs event)
{
	struct uwb_notifs_handler *handler;
	if (mutex_lock_interruptible(&rc->notifs_chain.mutex))
		return;
	if (!list_empty(&rc->notifs_chain.list)) {
		list_for_each_entry(handler, &rc->notifs_chain.list, list_node) {
			handler->cb(handler->data, uwb_dev, event);
		}
	}
	mutex_unlock(&rc->notifs_chain.mutex);
}

/*
 * Release the backing device of a uwb_dev that has been dynamically allocated.
 */
static void uwb_dev_sys_release(struct device *dev)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	uwb_bce_put(uwb_dev->bce);
	memset(uwb_dev, 0x69, sizeof(*uwb_dev));
	kfree(uwb_dev);
}

/*
 * Initialize a UWB device instance
 *
 * Alloc, zero and call this function.
 */
void uwb_dev_init(struct uwb_dev *uwb_dev)
{
	mutex_init(&uwb_dev->mutex);
	device_initialize(&uwb_dev->dev);
	uwb_dev->dev.release = uwb_dev_sys_release;
	uwb_dev_addr_init(&uwb_dev->dev_addr);
	uwb_mac_addr_init(&uwb_dev->mac_addr);
	bitmap_fill(uwb_dev->streams, UWB_NUM_GLOBAL_STREAMS);
}

static ssize_t uwb_dev_EUI_48_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	char addr[UWB_ADDR_STRSIZE];

	uwb_mac_addr_print(addr, sizeof(addr), &uwb_dev->mac_addr);
	return sprintf(buf, "%s\n", addr);
}
static DEVICE_ATTR(EUI_48, S_IRUGO, uwb_dev_EUI_48_show, NULL);

static ssize_t uwb_dev_DevAddr_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	char addr[UWB_ADDR_STRSIZE];

	uwb_dev_addr_print(addr, sizeof(addr), &uwb_dev->dev_addr);
	return sprintf(buf, "%s\n", addr);
}
static DEVICE_ATTR(DevAddr, S_IRUGO, uwb_dev_DevAddr_show, NULL);

/*
 * Show the BPST of this device.
 *
 * Calculated from the receive time of the device's beacon and it's
 * slot number.
 */
static ssize_t uwb_dev_BPST_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce;
	struct uwb_beacon_frame *bf;
	u16 bpst;

	bce = uwb_dev->bce;
	mutex_lock(&bce->mutex);
	bf = (struct uwb_beacon_frame *)bce->be->BeaconInfo;
	bpst = bce->be->wBPSTOffset
		- (u16)(bf->Beacon_Slot_Number * UWB_BEACON_SLOT_LENGTH_US);
	mutex_unlock(&bce->mutex);

	return sprintf(buf, "%d\n", bpst);
}
static DEVICE_ATTR(BPST, S_IRUGO, uwb_dev_BPST_show, NULL);

/*
 * Show the IEs a device is beaconing
 *
 * We need to access the beacon cache, so we just lock it really
 * quick, print the IEs and unlock.
 *
 * We have a reference on the cache entry, so that should be
 * quite safe.
 */
static ssize_t uwb_dev_IEs_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	return uwb_bce_print_IEs(uwb_dev, uwb_dev->bce, buf, PAGE_SIZE);
}
static DEVICE_ATTR(IEs, S_IRUGO | S_IWUSR, uwb_dev_IEs_show, NULL);

static ssize_t uwb_dev_LQE_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce = uwb_dev->bce;
	size_t result;

	mutex_lock(&bce->mutex);
	result = stats_show(&uwb_dev->bce->lqe_stats, buf);
	mutex_unlock(&bce->mutex);
	return result;
}

static ssize_t uwb_dev_LQE_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce = uwb_dev->bce;
	ssize_t result;

	mutex_lock(&bce->mutex);
	result = stats_store(&uwb_dev->bce->lqe_stats, buf, size);
	mutex_unlock(&bce->mutex);
	return result;
}
static DEVICE_ATTR(LQE, S_IRUGO | S_IWUSR, uwb_dev_LQE_show, uwb_dev_LQE_store);

static ssize_t uwb_dev_RSSI_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce = uwb_dev->bce;
	size_t result;

	mutex_lock(&bce->mutex);
	result = stats_show(&uwb_dev->bce->rssi_stats, buf);
	mutex_unlock(&bce->mutex);
	return result;
}

static ssize_t uwb_dev_RSSI_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce = uwb_dev->bce;
	ssize_t result;

	mutex_lock(&bce->mutex);
	result = stats_store(&uwb_dev->bce->rssi_stats, buf, size);
	mutex_unlock(&bce->mutex);
	return result;
}
static DEVICE_ATTR(RSSI, S_IRUGO | S_IWUSR, uwb_dev_RSSI_show, uwb_dev_RSSI_store);


static struct attribute *dev_attrs[] = {
	&dev_attr_EUI_48.attr,
	&dev_attr_DevAddr.attr,
	&dev_attr_BPST.attr,
	&dev_attr_IEs.attr,
	&dev_attr_LQE.attr,
	&dev_attr_RSSI.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.attrs = dev_attrs,
};

static struct attribute_group *groups[] = {
	&dev_attr_group,
	NULL,
};

/**
 * Device SYSFS registration
 *
 *
 */
static int __uwb_dev_sys_add(struct uwb_dev *uwb_dev, struct device *parent_dev)
{
	struct device *dev;

	dev = &uwb_dev->dev;
	/* Device sysfs files are only useful for neighbor devices not
	   local radio controllers. */
	if (&uwb_dev->rc->uwb_dev != uwb_dev)
		dev->groups = groups;
	dev->parent = parent_dev;
	dev_set_drvdata(dev, uwb_dev);

	return device_add(dev);
}


static void __uwb_dev_sys_rm(struct uwb_dev *uwb_dev)
{
	dev_set_drvdata(&uwb_dev->dev, NULL);
	device_del(&uwb_dev->dev);
}


/**
 * Register and initialize a new UWB device
 *
 * Did you call uwb_dev_init() on it?
 *
 * @parent_rc: is the parent radio controller who has the link to the
 *             device. When registering the UWB device that is a UWB
 *             Radio Controller, we point back to it.
 *
 * If registering the device that is part of a radio, caller has set
 * rc->uwb_dev->dev. Otherwise it is to be left NULL--a new one will
 * be allocated.
 */
int uwb_dev_add(struct uwb_dev *uwb_dev, struct device *parent_dev,
		struct uwb_rc *parent_rc)
{
	int result;
	struct device *dev;

	BUG_ON(uwb_dev == NULL);
	BUG_ON(parent_dev == NULL);
	BUG_ON(parent_rc == NULL);

	mutex_lock(&uwb_dev->mutex);
	dev = &uwb_dev->dev;
	uwb_dev->rc = parent_rc;
	result = __uwb_dev_sys_add(uwb_dev, parent_dev);
	if (result < 0)
		printk(KERN_ERR "UWB: unable to register dev %s with sysfs: %d\n",
		       dev_name(dev), result);
	mutex_unlock(&uwb_dev->mutex);
	return result;
}


void uwb_dev_rm(struct uwb_dev *uwb_dev)
{
	mutex_lock(&uwb_dev->mutex);
	__uwb_dev_sys_rm(uwb_dev);
	mutex_unlock(&uwb_dev->mutex);
}


static
int __uwb_dev_try_get(struct device *dev, void *__target_uwb_dev)
{
	struct uwb_dev *target_uwb_dev = __target_uwb_dev;
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	if (uwb_dev == target_uwb_dev) {
		uwb_dev_get(uwb_dev);
		return 1;
	} else
		return 0;
}


/**
 * Given a UWB device descriptor, validate and refcount it
 *
 * @returns NULL if the device does not exist or is quiescing; the ptr to
 *               it otherwise.
 */
struct uwb_dev *uwb_dev_try_get(struct uwb_rc *rc, struct uwb_dev *uwb_dev)
{
	if (uwb_dev_for_each(rc, __uwb_dev_try_get, uwb_dev))
		return uwb_dev;
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(uwb_dev_try_get);


/**
 * Remove a device from the system [grunt for other functions]
 */
int __uwb_dev_offair(struct uwb_dev *uwb_dev, struct uwb_rc *rc)
{
	struct device *dev = &uwb_dev->dev;
	char macbuf[UWB_ADDR_STRSIZE], devbuf[UWB_ADDR_STRSIZE];

	uwb_mac_addr_print(macbuf, sizeof(macbuf), &uwb_dev->mac_addr);
	uwb_dev_addr_print(devbuf, sizeof(devbuf), &uwb_dev->dev_addr);
	dev_info(dev, "uwb device (mac %s dev %s) disconnected from %s %s\n",
		 macbuf, devbuf,
		 rc ? rc->uwb_dev.dev.parent->bus->name : "n/a",
		 rc ? dev_name(rc->uwb_dev.dev.parent) : "");
	uwb_dev_rm(uwb_dev);
	list_del(&uwb_dev->bce->node);
	uwb_bce_put(uwb_dev->bce);
	uwb_dev_put(uwb_dev);	/* for the creation in _onair() */

	return 0;
}


/**
 * A device went off the air, clean up after it!
 *
 * This is called by the UWB Daemon (through the beacon purge function
 * uwb_bcn_cache_purge) when it is detected that a device has been in
 * radio silence for a while.
 *
 * If this device is actually a local radio controller we don't need
 * to go through the offair process, as it is not registered as that.
 *
 * NOTE: uwb_bcn_cache.mutex is held!
 */
void uwbd_dev_offair(struct uwb_beca_e *bce)
{
	struct uwb_dev *uwb_dev;

	uwb_dev = bce->uwb_dev;
	if (uwb_dev) {
		uwb_notify(uwb_dev->rc, uwb_dev, UWB_NOTIF_OFFAIR);
		__uwb_dev_offair(uwb_dev, uwb_dev->rc);
	}
}


/**
 * A device went on the air, start it up!
 *
 * This is called by the UWB Daemon when it is detected that a device
 * has popped up in the radio range of the radio controller.
 *
 * It will just create the freaking device, register the beacon and
 * stuff and yatla, done.
 *
 *
 * NOTE: uwb_beca.mutex is held, bce->mutex is held
 */
void uwbd_dev_onair(struct uwb_rc *rc, struct uwb_beca_e *bce)
{
	int result;
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_dev *uwb_dev;
	char macbuf[UWB_ADDR_STRSIZE], devbuf[UWB_ADDR_STRSIZE];

	uwb_mac_addr_print(macbuf, sizeof(macbuf), bce->mac_addr);
	uwb_dev_addr_print(devbuf, sizeof(devbuf), &bce->dev_addr);
	uwb_dev = kzalloc(sizeof(struct uwb_dev), GFP_KERNEL);
	if (uwb_dev == NULL) {
		dev_err(dev, "new device %s: Cannot allocate memory\n",
			macbuf);
		return;
	}
	uwb_dev_init(uwb_dev);		/* This sets refcnt to one, we own it */
	uwb_dev->mac_addr = *bce->mac_addr;
	uwb_dev->dev_addr = bce->dev_addr;
	dev_set_name(&uwb_dev->dev, macbuf);
	result = uwb_dev_add(uwb_dev, &rc->uwb_dev.dev, rc);
	if (result < 0) {
		dev_err(dev, "new device %s: cannot instantiate device\n",
			macbuf);
		goto error_dev_add;
	}
	/* plug the beacon cache */
	bce->uwb_dev = uwb_dev;
	uwb_dev->bce = bce;
	uwb_bce_get(bce);		/* released in uwb_dev_sys_release() */
	dev_info(dev, "uwb device (mac %s dev %s) connected to %s %s\n",
		 macbuf, devbuf, rc->uwb_dev.dev.parent->bus->name,
		 dev_name(rc->uwb_dev.dev.parent));
	uwb_notify(rc, uwb_dev, UWB_NOTIF_ONAIR);
	return;

error_dev_add:
	kfree(uwb_dev);
	return;
}

/**
 * Iterate over the list of UWB devices, calling a @function on each
 *
 * See docs for bus_for_each()....
 *
 * @rc:       radio controller for the devices.
 * @function: function to call.
 * @priv:     data to pass to @function.
 * @returns:  0 if no invocation of function() returned a value
 *            different to zero. That value otherwise.
 */
int uwb_dev_for_each(struct uwb_rc *rc, uwb_dev_for_each_f function, void *priv)
{
	return device_for_each_child(&rc->uwb_dev.dev, priv, function);
}
EXPORT_SYMBOL_GPL(uwb_dev_for_each);
