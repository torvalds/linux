/*
 * Wireless USB Host Controller
 * sysfs glue, wusbcore module support and life cycle management
 *
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
 * Creation/destruction of wusbhc is split in two parts; that that
 * doesn't require the HCD to be added (wusbhc_{create,destroy}) and
 * the one that requires (phase B, wusbhc_b_{create,destroy}).
 *
 * This is so because usb_add_hcd() will start the HC, and thus, all
 * the HC specific stuff has to be already initialized (like sysfs
 * thingies).
 */
#include <linux/device.h>
#include <linux/module.h>
#include "wusbhc.h"

/**
 * Extract the wusbhc that corresponds to a USB Host Controller class device
 *
 * WARNING! Apply only if @dev is that of a
 *          wusbhc.usb_hcd.self->class_dev; otherwise, you loose.
 */
static struct wusbhc *usbhc_dev_to_wusbhc(struct device *dev)
{
	struct usb_bus *usb_bus = dev_get_drvdata(dev);
	struct usb_hcd *usb_hcd = bus_to_hcd(usb_bus);
	return usb_hcd_to_wusbhc(usb_hcd);
}

/*
 * Show & store the current WUSB trust timeout
 *
 * We don't do locking--it is an 'atomic' value.
 *
 * The units that we store/show are always MILLISECONDS. However, the
 * value of trust_timeout is jiffies.
 */
static ssize_t wusb_trust_timeout_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct wusbhc *wusbhc = usbhc_dev_to_wusbhc(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", wusbhc->trust_timeout);
}

static ssize_t wusb_trust_timeout_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct wusbhc *wusbhc = usbhc_dev_to_wusbhc(dev);
	ssize_t result = -ENOSYS;
	unsigned trust_timeout;

	result = sscanf(buf, "%u", &trust_timeout);
	if (result != 1) {
		result = -EINVAL;
		goto out;
	}
	/* FIXME: maybe we should check for range validity? */
	wusbhc->trust_timeout = trust_timeout;
	cancel_delayed_work(&wusbhc->keep_alive_timer);
	flush_workqueue(wusbd);
	queue_delayed_work(wusbd, &wusbhc->keep_alive_timer,
			   (trust_timeout * CONFIG_HZ)/1000/2);
out:
	return result < 0 ? result : size;
}
static DEVICE_ATTR(wusb_trust_timeout, 0644, wusb_trust_timeout_show,
					     wusb_trust_timeout_store);

/*
 * Show the current WUSB CHID.
 */
static ssize_t wusb_chid_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct wusbhc *wusbhc = usbhc_dev_to_wusbhc(dev);
	const struct wusb_ckhdid *chid;
	ssize_t result = 0;

	if (wusbhc->wuie_host_info != NULL)
		chid = &wusbhc->wuie_host_info->CHID;
	else
		chid = &wusb_ckhdid_zero;

	result += ckhdid_printf(buf, PAGE_SIZE, chid);
	result += sprintf(buf + result, "\n");

	return result;
}

/*
 * Store a new CHID.
 *
 * - Write an all zeros CHID and it will stop the controller
 * - Write a non-zero CHID and it will start it.
 *
 * See wusbhc_chid_set() for more info.
 */
static ssize_t wusb_chid_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	struct wusbhc *wusbhc = usbhc_dev_to_wusbhc(dev);
	struct wusb_ckhdid chid;
	ssize_t result;

	result = sscanf(buf,
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx\n",
			&chid.data[0] , &chid.data[1] ,
			&chid.data[2] , &chid.data[3] ,
			&chid.data[4] , &chid.data[5] ,
			&chid.data[6] , &chid.data[7] ,
			&chid.data[8] , &chid.data[9] ,
			&chid.data[10], &chid.data[11],
			&chid.data[12], &chid.data[13],
			&chid.data[14], &chid.data[15]);
	if (result != 16) {
		dev_err(dev, "Unrecognized CHID (need 16 8-bit hex digits): "
			"%d\n", (int)result);
		return -EINVAL;
	}
	result = wusbhc_chid_set(wusbhc, &chid);
	return result < 0 ? result : size;
}
static DEVICE_ATTR(wusb_chid, 0644, wusb_chid_show, wusb_chid_store);


static ssize_t wusb_phy_rate_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct wusbhc *wusbhc = usbhc_dev_to_wusbhc(dev);

	return sprintf(buf, "%d\n", wusbhc->phy_rate);
}

static ssize_t wusb_phy_rate_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct wusbhc *wusbhc = usbhc_dev_to_wusbhc(dev);
	uint8_t phy_rate;
	ssize_t result;

	result = sscanf(buf, "%hhu", &phy_rate);
	if (result != 1)
		return -EINVAL;
	if (phy_rate >= UWB_PHY_RATE_INVALID)
		return -EINVAL;

	wusbhc->phy_rate = phy_rate;
	return size;
}
static DEVICE_ATTR(wusb_phy_rate, 0644, wusb_phy_rate_show, wusb_phy_rate_store);

/* Group all the WUSBHC attributes */
static struct attribute *wusbhc_attrs[] = {
		&dev_attr_wusb_trust_timeout.attr,
		&dev_attr_wusb_chid.attr,
		&dev_attr_wusb_phy_rate.attr,
		NULL,
};

static struct attribute_group wusbhc_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = wusbhc_attrs,
};

/*
 * Create a wusbhc instance
 *
 * NOTEs:
 *
 *  - assumes *wusbhc has been zeroed and wusbhc->usb_hcd has been
 *    initialized but not added.
 *
 *  - fill out ports_max, mmcies_max and mmcie_{add,rm} before calling.
 *
 *  - fill out wusbhc->uwb_rc and refcount it before calling
 *  - fill out the wusbhc->sec_modes array
 */
int wusbhc_create(struct wusbhc *wusbhc)
{
	int result = 0;

	wusbhc->trust_timeout = WUSB_TRUST_TIMEOUT_MS;
	wusbhc->phy_rate = UWB_PHY_RATE_INVALID - 1;

	mutex_init(&wusbhc->mutex);
	result = wusbhc_mmcie_create(wusbhc);
	if (result < 0)
		goto error_mmcie_create;
	result = wusbhc_devconnect_create(wusbhc);
	if (result < 0)
		goto error_devconnect_create;
	result = wusbhc_rh_create(wusbhc);
	if (result < 0)
		goto error_rh_create;
	result = wusbhc_sec_create(wusbhc);
	if (result < 0)
		goto error_sec_create;
	return 0;

error_sec_create:
	wusbhc_rh_destroy(wusbhc);
error_rh_create:
	wusbhc_devconnect_destroy(wusbhc);
error_devconnect_create:
	wusbhc_mmcie_destroy(wusbhc);
error_mmcie_create:
	return result;
}
EXPORT_SYMBOL_GPL(wusbhc_create);

static inline struct kobject *wusbhc_kobj(struct wusbhc *wusbhc)
{
	return &wusbhc->usb_hcd.self.controller->kobj;
}

/*
 * Phase B of a wusbhc instance creation
 *
 * Creates fields that depend on wusbhc->usb_hcd having been
 * added. This is where we create the sysfs files in
 * /sys/class/usb_host/usb_hostX/.
 *
 * NOTE: Assumes wusbhc->usb_hcd has been already added by the upper
 *       layer (hwahc or whci)
 */
int wusbhc_b_create(struct wusbhc *wusbhc)
{
	int result = 0;
	struct device *dev = wusbhc->usb_hcd.self.controller;

	result = sysfs_create_group(wusbhc_kobj(wusbhc), &wusbhc_attr_group);
	if (result < 0) {
		dev_err(dev, "Cannot register WUSBHC attributes: %d\n", result);
		goto error_create_attr_group;
	}

	result = wusbhc_pal_register(wusbhc);
	if (result < 0)
		goto error_pal_register;
	return 0;

error_pal_register:
	sysfs_remove_group(wusbhc_kobj(wusbhc), &wusbhc_attr_group);
error_create_attr_group:
	return result;
}
EXPORT_SYMBOL_GPL(wusbhc_b_create);

void wusbhc_b_destroy(struct wusbhc *wusbhc)
{
	wusbhc_pal_unregister(wusbhc);
	sysfs_remove_group(wusbhc_kobj(wusbhc), &wusbhc_attr_group);
}
EXPORT_SYMBOL_GPL(wusbhc_b_destroy);

void wusbhc_destroy(struct wusbhc *wusbhc)
{
	wusbhc_sec_destroy(wusbhc);
	wusbhc_rh_destroy(wusbhc);
	wusbhc_devconnect_destroy(wusbhc);
	wusbhc_mmcie_destroy(wusbhc);
}
EXPORT_SYMBOL_GPL(wusbhc_destroy);

struct workqueue_struct *wusbd;
EXPORT_SYMBOL_GPL(wusbd);

/*
 * WUSB Cluster ID allocation map
 *
 * Each WUSB bus in a channel is identified with a Cluster Id in the
 * unauth address pace (WUSB1.0[4.3]). We take the range 0xe0 to 0xff
 * (that's space for 31 WUSB controllers, as 0xff can't be taken). We
 * start taking from 0xff, 0xfe, 0xfd... (hence the += or -= 0xff).
 *
 * For each one we taken, we pin it in the bitap
 */
#define CLUSTER_IDS 32
static DECLARE_BITMAP(wusb_cluster_id_table, CLUSTER_IDS);
static DEFINE_SPINLOCK(wusb_cluster_ids_lock);

/*
 * Get a WUSB Cluster ID
 *
 * Need to release with wusb_cluster_id_put() when done w/ it.
 */
/* FIXME: coordinate with the choose_addres() from the USB stack */
/* we want to leave the top of the 128 range for cluster addresses and
 * the bottom for device addresses (as we map them one on one with
 * ports). */
u8 wusb_cluster_id_get(void)
{
	u8 id;
	spin_lock(&wusb_cluster_ids_lock);
	id = find_first_zero_bit(wusb_cluster_id_table, CLUSTER_IDS);
	if (id >= CLUSTER_IDS) {
		id = 0;
		goto out;
	}
	set_bit(id, wusb_cluster_id_table);
	id = (u8) 0xff - id;
out:
	spin_unlock(&wusb_cluster_ids_lock);
	return id;

}
EXPORT_SYMBOL_GPL(wusb_cluster_id_get);

/*
 * Release a WUSB Cluster ID
 *
 * Obtained it with wusb_cluster_id_get()
 */
void wusb_cluster_id_put(u8 id)
{
	id = 0xff - id;
	BUG_ON(id >= CLUSTER_IDS);
	spin_lock(&wusb_cluster_ids_lock);
	WARN_ON(!test_bit(id, wusb_cluster_id_table));
	clear_bit(id, wusb_cluster_id_table);
	spin_unlock(&wusb_cluster_ids_lock);
}
EXPORT_SYMBOL_GPL(wusb_cluster_id_put);

/**
 * wusbhc_giveback_urb - return an URB to the USB core
 * @wusbhc: the host controller the URB is from.
 * @urb:    the URB.
 * @status: the URB's status.
 *
 * Return an URB to the USB core doing some additional WUSB specific
 * processing.
 *
 *  - After a successful transfer, update the trust timeout timestamp
 *    for the WUSB device.
 *
 *  - [WUSB] sections 4.13 and 7.5.1 specifies the stop retrasmittion
 *    condition for the WCONNECTACK_IE is that the host has observed
 *    the associated device responding to a control transfer.
 */
void wusbhc_giveback_urb(struct wusbhc *wusbhc, struct urb *urb, int status)
{
	struct wusb_dev *wusb_dev = __wusb_dev_get_by_usb_dev(wusbhc, urb->dev);

	if (status == 0 && wusb_dev) {
		wusb_dev->entry_ts = jiffies;

		/* wusbhc_devconnect_acked() can't be called from
		   atomic context so defer it to a work queue. */
		if (!list_empty(&wusb_dev->cack_node))
			queue_work(wusbd, &wusb_dev->devconnect_acked_work);
		else
			wusb_dev_put(wusb_dev);
	}

	usb_hcd_giveback_urb(&wusbhc->usb_hcd, urb, status);
}
EXPORT_SYMBOL_GPL(wusbhc_giveback_urb);

/**
 * wusbhc_reset_all - reset the HC hardware
 * @wusbhc: the host controller to reset.
 *
 * Request a full hardware reset of the chip.  This will also reset
 * the radio controller and any other PALs.
 */
void wusbhc_reset_all(struct wusbhc *wusbhc)
{
	uwb_rc_reset_all(wusbhc->uwb_rc);
}
EXPORT_SYMBOL_GPL(wusbhc_reset_all);

static struct notifier_block wusb_usb_notifier = {
	.notifier_call = wusb_usb_ncb,
	.priority = INT_MAX	/* Need to be called first of all */
};

static int __init wusbcore_init(void)
{
	int result;
	result = wusb_crypto_init();
	if (result < 0)
		goto error_crypto_init;
	/* WQ is singlethread because we need to serialize notifications */
	wusbd = create_singlethread_workqueue("wusbd");
	if (wusbd == NULL) {
		result = -ENOMEM;
		printk(KERN_ERR "WUSB-core: Cannot create wusbd workqueue\n");
		goto error_wusbd_create;
	}
	usb_register_notify(&wusb_usb_notifier);
	bitmap_zero(wusb_cluster_id_table, CLUSTER_IDS);
	set_bit(0, wusb_cluster_id_table);	/* reserve Cluster ID 0xff */
	return 0;

error_wusbd_create:
	wusb_crypto_exit();
error_crypto_init:
	return result;

}
module_init(wusbcore_init);

static void __exit wusbcore_exit(void)
{
	clear_bit(0, wusb_cluster_id_table);
	if (!bitmap_empty(wusb_cluster_id_table, CLUSTER_IDS)) {
		char buf[256];
		bitmap_scnprintf(buf, sizeof(buf), wusb_cluster_id_table,
				 CLUSTER_IDS);
		printk(KERN_ERR "BUG: WUSB Cluster IDs not released "
		       "on exit: %s\n", buf);
		WARN_ON(1);
	}
	usb_unregister_notify(&wusb_usb_notifier);
	destroy_workqueue(wusbd);
	wusb_crypto_exit();
}
module_exit(wusbcore_exit);

MODULE_AUTHOR("Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>");
MODULE_DESCRIPTION("Wireless USB core");
MODULE_LICENSE("GPL");
