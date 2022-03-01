// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright IBM Corp. 2001, 2012
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *	       Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *				  Ralph Wuerthner <rwuerthn@de.ibm.com>
 *  MSGTYPE restruct:		  Holger Dengler <hd@linux.vnet.ibm.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/compat.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/hw_random.h>
#include <linux/debugfs.h>
#include <asm/debug.h>

#include "zcrypt_debug.h"
#include "zcrypt_api.h"

#include "zcrypt_msgtype6.h"
#include "zcrypt_msgtype50.h"

/*
 * Device attributes common for all crypto card devices.
 */

static ssize_t type_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct zcrypt_card *zc = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", zc->type_string);
}

static DEVICE_ATTR_RO(type);

static ssize_t online_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct zcrypt_card *zc = dev_get_drvdata(dev);
	struct ap_card *ac = to_ap_card(dev);
	int online = ac->config && zc->online ? 1 : 0;

	return scnprintf(buf, PAGE_SIZE, "%d\n", online);
}

static ssize_t online_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct zcrypt_card *zc = dev_get_drvdata(dev);
	struct ap_card *ac = to_ap_card(dev);
	struct zcrypt_queue *zq;
	int online, id, i = 0, maxzqs = 0;
	struct zcrypt_queue **zq_uelist = NULL;

	if (sscanf(buf, "%d\n", &online) != 1 || online < 0 || online > 1)
		return -EINVAL;

	if (online && !ac->config)
		return -ENODEV;

	zc->online = online;
	id = zc->card->id;

	ZCRYPT_DBF_INFO("%s card=%02x online=%d\n", __func__, id, online);

	ap_send_online_uevent(&ac->ap_dev, online);

	spin_lock(&zcrypt_list_lock);
	/*
	 * As we are in atomic context here, directly sending uevents
	 * does not work. So collect the zqueues in a dynamic array
	 * and process them after zcrypt_list_lock release. As we get/put
	 * the zqueue objects, we make sure they exist after lock release.
	 */
	list_for_each_entry(zq, &zc->zqueues, list)
		maxzqs++;
	if (maxzqs > 0)
		zq_uelist = kcalloc(maxzqs + 1, sizeof(zq), GFP_ATOMIC);
	list_for_each_entry(zq, &zc->zqueues, list)
		if (zcrypt_queue_force_online(zq, online))
			if (zq_uelist) {
				zcrypt_queue_get(zq);
				zq_uelist[i++] = zq;
			}
	spin_unlock(&zcrypt_list_lock);
	if (zq_uelist) {
		for (i = 0; zq_uelist[i]; i++) {
			zq = zq_uelist[i];
			ap_send_online_uevent(&zq->queue->ap_dev, online);
			zcrypt_queue_put(zq);
		}
		kfree(zq_uelist);
	}

	return count;
}

static DEVICE_ATTR_RW(online);

static ssize_t load_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct zcrypt_card *zc = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&zc->load));
}

static DEVICE_ATTR_RO(load);

static struct attribute *zcrypt_card_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_online.attr,
	&dev_attr_load.attr,
	NULL,
};

static const struct attribute_group zcrypt_card_attr_group = {
	.attrs = zcrypt_card_attrs,
};

struct zcrypt_card *zcrypt_card_alloc(void)
{
	struct zcrypt_card *zc;

	zc = kzalloc(sizeof(struct zcrypt_card), GFP_KERNEL);
	if (!zc)
		return NULL;
	INIT_LIST_HEAD(&zc->list);
	INIT_LIST_HEAD(&zc->zqueues);
	kref_init(&zc->refcount);
	return zc;
}
EXPORT_SYMBOL(zcrypt_card_alloc);

void zcrypt_card_free(struct zcrypt_card *zc)
{
	kfree(zc);
}
EXPORT_SYMBOL(zcrypt_card_free);

static void zcrypt_card_release(struct kref *kref)
{
	struct zcrypt_card *zdev =
		container_of(kref, struct zcrypt_card, refcount);
	zcrypt_card_free(zdev);
}

void zcrypt_card_get(struct zcrypt_card *zc)
{
	kref_get(&zc->refcount);
}
EXPORT_SYMBOL(zcrypt_card_get);

int zcrypt_card_put(struct zcrypt_card *zc)
{
	return kref_put(&zc->refcount, zcrypt_card_release);
}
EXPORT_SYMBOL(zcrypt_card_put);

/**
 * zcrypt_card_register() - Register a crypto card device.
 * @zc: Pointer to a crypto card device
 *
 * Register a crypto card device. Returns 0 if successful.
 */
int zcrypt_card_register(struct zcrypt_card *zc)
{
	int rc;

	spin_lock(&zcrypt_list_lock);
	list_add_tail(&zc->list, &zcrypt_card_list);
	spin_unlock(&zcrypt_list_lock);

	zc->online = 1;

	ZCRYPT_DBF_INFO("%s card=%02x register online=1\n",
			__func__, zc->card->id);

	rc = sysfs_create_group(&zc->card->ap_dev.device.kobj,
				&zcrypt_card_attr_group);
	if (rc) {
		spin_lock(&zcrypt_list_lock);
		list_del_init(&zc->list);
		spin_unlock(&zcrypt_list_lock);
	}

	return rc;
}
EXPORT_SYMBOL(zcrypt_card_register);

/**
 * zcrypt_card_unregister(): Unregister a crypto card device.
 * @zc: Pointer to crypto card device
 *
 * Unregister a crypto card device.
 */
void zcrypt_card_unregister(struct zcrypt_card *zc)
{
	ZCRYPT_DBF_INFO("%s card=%02x unregister\n",
			__func__, zc->card->id);

	spin_lock(&zcrypt_list_lock);
	list_del_init(&zc->list);
	spin_unlock(&zcrypt_list_lock);
	sysfs_remove_group(&zc->card->ap_dev.device.kobj,
			   &zcrypt_card_attr_group);
	zcrypt_card_put(zc);
}
EXPORT_SYMBOL(zcrypt_card_unregister);
