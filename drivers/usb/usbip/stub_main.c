/*
 * Copyright (C) 2003-2008 Takahiro Hirofuchi
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#include <linux/string.h>
#include <linux/module.h>
#include <linux/device.h>

#include "usbip_common.h"
#include "stub.h"

#define DRIVER_AUTHOR "Takahiro Hirofuchi"
#define DRIVER_DESC "USB/IP Host Driver"

struct kmem_cache *stub_priv_cache;

/*
 * busid_tables defines matching busids that usbip can grab. A user can change
 * dynamically what device is locally used and what device is exported to a
 * remote host.
 */
#define MAX_BUSID 16
static struct bus_id_priv busid_table[MAX_BUSID];
static spinlock_t busid_table_lock;

static void init_busid_table(void)
{
	int i;

	/*
	 * This also sets the bus_table[i].status to
	 * STUB_BUSID_OTHER, which is 0.
	 */
	memset(busid_table, 0, sizeof(busid_table));

	spin_lock_init(&busid_table_lock);

	for (i = 0; i < MAX_BUSID; i++)
		spin_lock_init(&busid_table[i].busid_lock);
}

/*
 * Find the index of the busid by name.
 * Must be called with busid_table_lock held.
 */
static int get_busid_idx(const char *busid)
{
	int i;
	int idx = -1;

	for (i = 0; i < MAX_BUSID; i++) {
		spin_lock(&busid_table[i].busid_lock);
		if (busid_table[i].name[0])
			if (!strncmp(busid_table[i].name, busid, BUSID_SIZE)) {
				idx = i;
				spin_unlock(&busid_table[i].busid_lock);
				break;
			}
		spin_unlock(&busid_table[i].busid_lock);
	}
	return idx;
}

/* Returns holding busid_lock. Should call put_busid_priv() to unlock */
struct bus_id_priv *get_busid_priv(const char *busid)
{
	int idx;
	struct bus_id_priv *bid = NULL;

	spin_lock(&busid_table_lock);
	idx = get_busid_idx(busid);
	if (idx >= 0) {
		bid = &(busid_table[idx]);
		/* get busid_lock before returning */
		spin_lock(&bid->busid_lock);
	}
	spin_unlock(&busid_table_lock);

	return bid;
}

void put_busid_priv(struct bus_id_priv *bid)
{
	if (bid)
		spin_unlock(&bid->busid_lock);
}

static int add_match_busid(char *busid)
{
	int i;
	int ret = -1;

	spin_lock(&busid_table_lock);
	/* already registered? */
	if (get_busid_idx(busid) >= 0) {
		ret = 0;
		goto out;
	}

	for (i = 0; i < MAX_BUSID; i++) {
		spin_lock(&busid_table[i].busid_lock);
		if (!busid_table[i].name[0]) {
			strlcpy(busid_table[i].name, busid, BUSID_SIZE);
			if ((busid_table[i].status != STUB_BUSID_ALLOC) &&
			    (busid_table[i].status != STUB_BUSID_REMOV))
				busid_table[i].status = STUB_BUSID_ADDED;
			ret = 0;
			spin_unlock(&busid_table[i].busid_lock);
			break;
		}
		spin_unlock(&busid_table[i].busid_lock);
	}

out:
	spin_unlock(&busid_table_lock);

	return ret;
}

int del_match_busid(char *busid)
{
	int idx;
	int ret = -1;

	spin_lock(&busid_table_lock);
	idx = get_busid_idx(busid);
	if (idx < 0)
		goto out;

	/* found */
	ret = 0;

	spin_lock(&busid_table[idx].busid_lock);

	if (busid_table[idx].status == STUB_BUSID_OTHER)
		memset(busid_table[idx].name, 0, BUSID_SIZE);

	if ((busid_table[idx].status != STUB_BUSID_OTHER) &&
	    (busid_table[idx].status != STUB_BUSID_ADDED))
		busid_table[idx].status = STUB_BUSID_REMOV;

	spin_unlock(&busid_table[idx].busid_lock);
out:
	spin_unlock(&busid_table_lock);

	return ret;
}

static ssize_t match_busid_show(struct device_driver *drv, char *buf)
{
	int i;
	char *out = buf;

	spin_lock(&busid_table_lock);
	for (i = 0; i < MAX_BUSID; i++) {
		spin_lock(&busid_table[i].busid_lock);
		if (busid_table[i].name[0])
			out += sprintf(out, "%s ", busid_table[i].name);
		spin_unlock(&busid_table[i].busid_lock);
	}
	spin_unlock(&busid_table_lock);
	out += sprintf(out, "\n");

	return out - buf;
}

static ssize_t match_busid_store(struct device_driver *dev, const char *buf,
				 size_t count)
{
	int len;
	char busid[BUSID_SIZE];

	if (count < 5)
		return -EINVAL;

	/* busid needs to include \0 termination */
	len = strlcpy(busid, buf + 4, BUSID_SIZE);
	if (sizeof(busid) <= len)
		return -EINVAL;

	if (!strncmp(buf, "add ", 4)) {
		if (add_match_busid(busid) < 0)
			return -ENOMEM;

		pr_debug("add busid %s\n", busid);
		return count;
	}

	if (!strncmp(buf, "del ", 4)) {
		if (del_match_busid(busid) < 0)
			return -ENODEV;

		pr_debug("del busid %s\n", busid);
		return count;
	}

	return -EINVAL;
}
static DRIVER_ATTR_RW(match_busid);

static int do_rebind(char *busid, struct bus_id_priv *busid_priv)
{
	int ret;

	/* device_attach() callers should hold parent lock for USB */
	if (busid_priv->udev->dev.parent)
		device_lock(busid_priv->udev->dev.parent);
	ret = device_attach(&busid_priv->udev->dev);
	if (busid_priv->udev->dev.parent)
		device_unlock(busid_priv->udev->dev.parent);
	if (ret < 0) {
		dev_err(&busid_priv->udev->dev, "rebind failed\n");
		return ret;
	}
	return 0;
}

static void stub_device_rebind(void)
{
#if IS_MODULE(CONFIG_USBIP_HOST)
	struct bus_id_priv *busid_priv;
	int i;

	/* update status to STUB_BUSID_OTHER so probe ignores the device */
	spin_lock(&busid_table_lock);
	for (i = 0; i < MAX_BUSID; i++) {
		if (busid_table[i].name[0] &&
		    busid_table[i].shutdown_busid) {
			busid_priv = &(busid_table[i]);
			busid_priv->status = STUB_BUSID_OTHER;
		}
	}
	spin_unlock(&busid_table_lock);

	/* now run rebind - no need to hold locks. driver files are removed */
	for (i = 0; i < MAX_BUSID; i++) {
		if (busid_table[i].name[0] &&
		    busid_table[i].shutdown_busid) {
			busid_priv = &(busid_table[i]);
			do_rebind(busid_table[i].name, busid_priv);
		}
	}
#endif
}

static ssize_t rebind_store(struct device_driver *dev, const char *buf,
				 size_t count)
{
	int ret;
	int len;
	struct bus_id_priv *bid;

	/* buf length should be less that BUSID_SIZE */
	len = strnlen(buf, BUSID_SIZE);

	if (!(len < BUSID_SIZE))
		return -EINVAL;

	bid = get_busid_priv(buf);
	if (!bid)
		return -ENODEV;

	/* mark the device for deletion so probe ignores it during rescan */
	bid->status = STUB_BUSID_OTHER;
	/* release the busid lock */
	put_busid_priv(bid);

	ret = do_rebind((char *) buf, bid);
	if (ret < 0)
		return ret;

	/* delete device from busid_table */
	del_match_busid((char *) buf);

	return count;
}

static DRIVER_ATTR_WO(rebind);

static struct stub_priv *stub_priv_pop_from_listhead(struct list_head *listhead)
{
	struct stub_priv *priv, *tmp;

	list_for_each_entry_safe(priv, tmp, listhead, list) {
		list_del(&priv->list);
		return priv;
	}

	return NULL;
}

static struct stub_priv *stub_priv_pop(struct stub_device *sdev)
{
	unsigned long flags;
	struct stub_priv *priv;

	spin_lock_irqsave(&sdev->priv_lock, flags);

	priv = stub_priv_pop_from_listhead(&sdev->priv_init);
	if (priv)
		goto done;

	priv = stub_priv_pop_from_listhead(&sdev->priv_tx);
	if (priv)
		goto done;

	priv = stub_priv_pop_from_listhead(&sdev->priv_free);

done:
	spin_unlock_irqrestore(&sdev->priv_lock, flags);

	return priv;
}

void stub_device_cleanup_urbs(struct stub_device *sdev)
{
	struct stub_priv *priv;
	struct urb *urb;

	dev_dbg(&sdev->udev->dev, "Stub device cleaning up urbs\n");

	while ((priv = stub_priv_pop(sdev))) {
		urb = priv->urb;
		dev_dbg(&sdev->udev->dev, "free urb seqnum %lu\n",
			priv->seqnum);
		usb_kill_urb(urb);

		kmem_cache_free(stub_priv_cache, priv);

		kfree(urb->transfer_buffer);
		urb->transfer_buffer = NULL;

		kfree(urb->setup_packet);
		urb->setup_packet = NULL;

		usb_free_urb(urb);
	}
}

static int __init usbip_host_init(void)
{
	int ret;

	init_busid_table();

	stub_priv_cache = KMEM_CACHE(stub_priv, SLAB_HWCACHE_ALIGN);
	if (!stub_priv_cache) {
		pr_err("kmem_cache_create failed\n");
		return -ENOMEM;
	}

	ret = usb_register_device_driver(&stub_driver, THIS_MODULE);
	if (ret) {
		pr_err("usb_register failed %d\n", ret);
		goto err_usb_register;
	}

	ret = driver_create_file(&stub_driver.drvwrap.driver,
				 &driver_attr_match_busid);
	if (ret) {
		pr_err("driver_create_file failed\n");
		goto err_create_file;
	}

	ret = driver_create_file(&stub_driver.drvwrap.driver,
				 &driver_attr_rebind);
	if (ret) {
		pr_err("driver_create_file failed\n");
		goto err_create_file;
	}

	return ret;

err_create_file:
	usb_deregister_device_driver(&stub_driver);
err_usb_register:
	kmem_cache_destroy(stub_priv_cache);
	return ret;
}

static void __exit usbip_host_exit(void)
{
	driver_remove_file(&stub_driver.drvwrap.driver,
			   &driver_attr_match_busid);

	driver_remove_file(&stub_driver.drvwrap.driver,
			   &driver_attr_rebind);

	/*
	 * deregister() calls stub_disconnect() for all devices. Device
	 * specific data is cleared in stub_disconnect().
	 */
	usb_deregister_device_driver(&stub_driver);

	/* initiate scan to attach devices */
	stub_device_rebind();

	kmem_cache_destroy(stub_priv_cache);
}

module_init(usbip_host_init);
module_exit(usbip_host_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
