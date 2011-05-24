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

#include <linux/slab.h>

#include "usbip_common.h"
#include "stub.h"

/* Version Information */
#define DRIVER_VERSION "1.0"
#define DRIVER_AUTHOR "Takahiro Hirofuchi"
#define DRIVER_DESC "Stub Driver for USB/IP"

/* stub_priv is allocated from stub_priv_cache */
struct kmem_cache *stub_priv_cache;

/*-------------------------------------------------------------------------*/

/* Define sysfs entries for the usbip driver */


/*
 * busid_tables defines matching busids that usbip can grab. A user can change
 * dynamically what device is locally used and what device is exported to a
 * remote host.
 */
#define MAX_BUSID 16
static struct bus_id_priv busid_table[MAX_BUSID];
static spinlock_t busid_table_lock;


int match_busid(const char *busid)
{
	int i;

	spin_lock(&busid_table_lock);

	for (i = 0; i < MAX_BUSID; i++)
		if (busid_table[i].name[0])
			if (!strncmp(busid_table[i].name, busid, BUSID_SIZE)) {
				/* already registerd */
				spin_unlock(&busid_table_lock);
				return 0;
			}

	spin_unlock(&busid_table_lock);

	return 1;
}

struct bus_id_priv *get_busid_priv(const char *busid)
{
	int i;

	spin_lock(&busid_table_lock);

	for (i = 0; i < MAX_BUSID; i++)
		if (busid_table[i].name[0])
			if (!strncmp(busid_table[i].name, busid, BUSID_SIZE)) {
				/* already registerd */
				spin_unlock(&busid_table_lock);
				return &(busid_table[i]);
			}

	spin_unlock(&busid_table_lock);

	return NULL;
}

static ssize_t show_match_busid(struct device_driver *drv, char *buf)
{
	int i;
	char *out = buf;

	spin_lock(&busid_table_lock);

	for (i = 0; i < MAX_BUSID; i++)
		if (busid_table[i].name[0])
			out += sprintf(out, "%s ", busid_table[i].name);

	spin_unlock(&busid_table_lock);

	out += sprintf(out, "\n");

	return out - buf;
}

static int add_match_busid(char *busid)
{
	int i;

	if (!match_busid(busid))
		return 0;

	spin_lock(&busid_table_lock);

	for (i = 0; i < MAX_BUSID; i++)
		if (!busid_table[i].name[0]) {
			strncpy(busid_table[i].name, busid, BUSID_SIZE);
			if ((busid_table[i].status != STUB_BUSID_ALLOC) &&
			    (busid_table[i].status != STUB_BUSID_REMOV))
				busid_table[i].status = STUB_BUSID_ADDED;
			spin_unlock(&busid_table_lock);
			return 0;
		}

	spin_unlock(&busid_table_lock);

	return -1;
}

int del_match_busid(char *busid)
{
	int i;

	spin_lock(&busid_table_lock);

	for (i = 0; i < MAX_BUSID; i++)
		if (!strncmp(busid_table[i].name, busid, BUSID_SIZE)) {
			/* found */
			if (busid_table[i].status == STUB_BUSID_OTHER)
				memset(busid_table[i].name, 0, BUSID_SIZE);
			if ((busid_table[i].status != STUB_BUSID_OTHER) &&
			    (busid_table[i].status != STUB_BUSID_ADDED)) {
				busid_table[i].status = STUB_BUSID_REMOV;
			}
			spin_unlock(&busid_table_lock);
			return 0;
		}

	spin_unlock(&busid_table_lock);

	return -1;
}
static void init_busid_table(void)
{
	int i;


	for (i = 0; i < MAX_BUSID; i++) {
		memset(busid_table[i].name, 0, BUSID_SIZE);
		busid_table[i].status = STUB_BUSID_OTHER;
		busid_table[i].interf_count = 0;
		busid_table[i].sdev = NULL;
		busid_table[i].shutdown_busid = 0;
	}
	spin_lock_init(&busid_table_lock);
}

static ssize_t store_match_busid(struct device_driver *dev, const char *buf,
		size_t count)
{
	int len;
	char busid[BUSID_SIZE];

	if (count < 5)
		return -EINVAL;

	/* strnlen() does not include \0 */
	len = strnlen(buf + 4, BUSID_SIZE);

	/* busid needs to include \0 termination */
	if (!(len < BUSID_SIZE))
		return -EINVAL;

	strncpy(busid, buf + 4, BUSID_SIZE);


	if (!strncmp(buf, "add ", 4)) {
		if (add_match_busid(busid) < 0)
			return -ENOMEM;
		else {
			usbip_udbg("add busid %s\n", busid);
			return count;
		}
	} else if (!strncmp(buf, "del ", 4)) {
		if (del_match_busid(busid) < 0)
			return -ENODEV;
		else {
			usbip_udbg("del busid %s\n", busid);
			return count;
		}
	} else
		return -EINVAL;
}

static DRIVER_ATTR(match_busid, S_IRUSR|S_IWUSR, show_match_busid,
							store_match_busid);



/*-------------------------------------------------------------------------*/

/* Cleanup functions used to free private data */

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
	if (priv) {
		spin_unlock_irqrestore(&sdev->priv_lock, flags);
		return priv;
	}

	priv = stub_priv_pop_from_listhead(&sdev->priv_tx);
	if (priv) {
		spin_unlock_irqrestore(&sdev->priv_lock, flags);
		return priv;
	}

	priv = stub_priv_pop_from_listhead(&sdev->priv_free);
	if (priv) {
		spin_unlock_irqrestore(&sdev->priv_lock, flags);
		return priv;
	}

	spin_unlock_irqrestore(&sdev->priv_lock, flags);
	return NULL;
}

void stub_device_cleanup_urbs(struct stub_device *sdev)
{
	struct stub_priv *priv;

	usbip_udbg("free sdev %p\n", sdev);

	while ((priv = stub_priv_pop(sdev))) {
		struct urb *urb = priv->urb;

		usbip_udbg("   free urb %p\n", urb);
		usb_kill_urb(urb);

		kmem_cache_free(stub_priv_cache, priv);

		kfree(urb->transfer_buffer);

		kfree(urb->setup_packet);

		usb_free_urb(urb);
	}
}


/*-------------------------------------------------------------------------*/

static int __init usb_stub_init(void)
{
	int ret;

	stub_priv_cache = kmem_cache_create("stub_priv",
					    sizeof(struct stub_priv), 0,
					    SLAB_HWCACHE_ALIGN, NULL);

	if (!stub_priv_cache) {
		printk(KERN_ERR KBUILD_MODNAME
		       ": create stub_priv_cache error\n");
		return -ENOMEM;
	}

	ret = usb_register(&stub_driver);
	if (ret) {
		printk(KERN_ERR KBUILD_MODNAME ": usb_register failed %d\n",
		       ret);
		goto error_usb_register;
	}

	printk(KERN_INFO KBUILD_MODNAME ":"
	       DRIVER_DESC ":" DRIVER_VERSION "\n");

	init_busid_table();

	ret = driver_create_file(&stub_driver.drvwrap.driver,
				 &driver_attr_match_busid);

	if (ret) {
		printk(KERN_ERR KBUILD_MODNAME ": create driver sysfs\n");
		goto error_create_file;
	}

	return ret;
error_create_file:
	usb_deregister(&stub_driver);
error_usb_register:
	kmem_cache_destroy(stub_priv_cache);
	return ret;
}

static void __exit usb_stub_exit(void)
{
	driver_remove_file(&stub_driver.drvwrap.driver,
			   &driver_attr_match_busid);

	/*
	 * deregister() calls stub_disconnect() for all devices. Device
	 * specific data is cleared in stub_disconnect().
	 */
	usb_deregister(&stub_driver);

	kmem_cache_destroy(stub_priv_cache);
}

module_init(usb_stub_init);
module_exit(usb_stub_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
