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
 * Device attributes common for all crypto queue devices.
 */

static ssize_t online_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct zcrypt_queue *zq = dev_get_drvdata(dev);
	struct ap_queue *aq = to_ap_queue(dev);
	int online = aq->config && zq->online ? 1 : 0;

	return scnprintf(buf, PAGE_SIZE, "%d\n", online);
}

static ssize_t online_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct zcrypt_queue *zq = dev_get_drvdata(dev);
	struct ap_queue *aq = to_ap_queue(dev);
	struct zcrypt_card *zc = zq->zcard;
	int online;

	if (sscanf(buf, "%d\n", &online) != 1 || online < 0 || online > 1)
		return -EINVAL;

	if (online && (!aq->config || !aq->card->config))
		return -ENODEV;
	if (online && !zc->online)
		return -EINVAL;
	zq->online = online;

	ZCRYPT_DBF_INFO("%s queue=%02x.%04x online=%d\n",
			__func__, AP_QID_CARD(zq->queue->qid),
			AP_QID_QUEUE(zq->queue->qid), online);

	ap_send_online_uevent(&aq->ap_dev, online);

	if (!online)
		ap_flush_queue(zq->queue);
	return count;
}

static DEVICE_ATTR_RW(online);

static ssize_t load_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct zcrypt_queue *zq = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&zq->load));
}

static DEVICE_ATTR_RO(load);

static struct attribute *zcrypt_queue_attrs[] = {
	&dev_attr_online.attr,
	&dev_attr_load.attr,
	NULL,
};

static const struct attribute_group zcrypt_queue_attr_group = {
	.attrs = zcrypt_queue_attrs,
};

bool zcrypt_queue_force_online(struct zcrypt_queue *zq, int online)
{
	if (!!zq->online != !!online) {
		zq->online = online;
		if (!online)
			ap_flush_queue(zq->queue);
		return true;
	}
	return false;
}

struct zcrypt_queue *zcrypt_queue_alloc(size_t reply_buf_size)
{
	struct zcrypt_queue *zq;

	zq = kzalloc(sizeof(*zq), GFP_KERNEL);
	if (!zq)
		return NULL;
	zq->reply.msg = kmalloc(reply_buf_size, GFP_KERNEL);
	if (!zq->reply.msg)
		goto out_free;
	zq->reply.bufsize = reply_buf_size;
	INIT_LIST_HEAD(&zq->list);
	kref_init(&zq->refcount);
	return zq;

out_free:
	kfree(zq);
	return NULL;
}
EXPORT_SYMBOL(zcrypt_queue_alloc);

void zcrypt_queue_free(struct zcrypt_queue *zq)
{
	kfree(zq->reply.msg);
	kfree(zq);
}
EXPORT_SYMBOL(zcrypt_queue_free);

static void zcrypt_queue_release(struct kref *kref)
{
	struct zcrypt_queue *zq =
		container_of(kref, struct zcrypt_queue, refcount);
	zcrypt_queue_free(zq);
}

void zcrypt_queue_get(struct zcrypt_queue *zq)
{
	kref_get(&zq->refcount);
}
EXPORT_SYMBOL(zcrypt_queue_get);

int zcrypt_queue_put(struct zcrypt_queue *zq)
{
	return kref_put(&zq->refcount, zcrypt_queue_release);
}
EXPORT_SYMBOL(zcrypt_queue_put);

/**
 * zcrypt_queue_register() - Register a crypto queue device.
 * @zq: Pointer to a crypto queue device
 *
 * Register a crypto queue device. Returns 0 if successful.
 */
int zcrypt_queue_register(struct zcrypt_queue *zq)
{
	struct zcrypt_card *zc;
	int rc;

	spin_lock(&zcrypt_list_lock);
	zc = dev_get_drvdata(&zq->queue->card->ap_dev.device);
	zcrypt_card_get(zc);
	zq->zcard = zc;
	zq->online = 1;	/* New devices are online by default. */

	ZCRYPT_DBF_INFO("%s queue=%02x.%04x register online=1\n",
			__func__, AP_QID_CARD(zq->queue->qid),
			AP_QID_QUEUE(zq->queue->qid));

	list_add_tail(&zq->list, &zc->zqueues);
	spin_unlock(&zcrypt_list_lock);

	rc = sysfs_create_group(&zq->queue->ap_dev.device.kobj,
				&zcrypt_queue_attr_group);
	if (rc)
		goto out;

	if (zq->ops->rng) {
		rc = zcrypt_rng_device_add();
		if (rc)
			goto out_unregister;
	}
	return 0;

out_unregister:
	sysfs_remove_group(&zq->queue->ap_dev.device.kobj,
			   &zcrypt_queue_attr_group);
out:
	spin_lock(&zcrypt_list_lock);
	list_del_init(&zq->list);
	spin_unlock(&zcrypt_list_lock);
	zcrypt_card_put(zc);
	return rc;
}
EXPORT_SYMBOL(zcrypt_queue_register);

/**
 * zcrypt_queue_unregister(): Unregister a crypto queue device.
 * @zq: Pointer to crypto queue device
 *
 * Unregister a crypto queue device.
 */
void zcrypt_queue_unregister(struct zcrypt_queue *zq)
{
	struct zcrypt_card *zc;

	ZCRYPT_DBF_INFO("%s queue=%02x.%04x unregister\n",
			__func__, AP_QID_CARD(zq->queue->qid),
			AP_QID_QUEUE(zq->queue->qid));

	zc = zq->zcard;
	spin_lock(&zcrypt_list_lock);
	list_del_init(&zq->list);
	spin_unlock(&zcrypt_list_lock);
	if (zq->ops->rng)
		zcrypt_rng_device_remove();
	sysfs_remove_group(&zq->queue->ap_dev.device.kobj,
			   &zcrypt_queue_attr_group);
	zcrypt_card_put(zc);
	zcrypt_queue_put(zq);
}
EXPORT_SYMBOL(zcrypt_queue_unregister);
