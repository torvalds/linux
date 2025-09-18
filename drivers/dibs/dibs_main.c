// SPDX-License-Identifier: GPL-2.0
/*
 *  DIBS - Direct Internal Buffer Sharing
 *
 *  Implementation of the DIBS class module
 *
 *  Copyright IBM Corp. 2025
 */
#define KMSG_COMPONENT "dibs"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/dibs.h>

#include "dibs_loopback.h"

MODULE_DESCRIPTION("Direct Internal Buffer Sharing class");
MODULE_LICENSE("GPL");

static struct class *dibs_class;

/* use an array rather a list for fast mapping: */
static struct dibs_client *clients[MAX_DIBS_CLIENTS];
static u8 max_client;
static DEFINE_MUTEX(clients_lock);
struct dibs_dev_list {
	struct list_head list;
	struct mutex mutex; /* protects dibs device list */
};

static struct dibs_dev_list dibs_dev_list = {
	.list = LIST_HEAD_INIT(dibs_dev_list.list),
	.mutex = __MUTEX_INITIALIZER(dibs_dev_list.mutex),
};

int dibs_register_client(struct dibs_client *client)
{
	struct dibs_dev *dibs;
	int i, rc = -ENOSPC;

	mutex_lock(&dibs_dev_list.mutex);
	mutex_lock(&clients_lock);
	for (i = 0; i < MAX_DIBS_CLIENTS; ++i) {
		if (!clients[i]) {
			clients[i] = client;
			client->id = i;
			if (i == max_client)
				max_client++;
			rc = 0;
			break;
		}
	}
	mutex_unlock(&clients_lock);

	if (i < MAX_DIBS_CLIENTS) {
		/* initialize with all devices that we got so far */
		list_for_each_entry(dibs, &dibs_dev_list.list, list) {
			dibs->priv[i] = NULL;
			client->ops->add_dev(dibs);
		}
	}
	mutex_unlock(&dibs_dev_list.mutex);

	return rc;
}
EXPORT_SYMBOL_GPL(dibs_register_client);

int dibs_unregister_client(struct dibs_client *client)
{
	struct dibs_dev *dibs;
	int rc = 0;

	mutex_lock(&dibs_dev_list.mutex);
	list_for_each_entry(dibs, &dibs_dev_list.list, list) {
		clients[client->id]->ops->del_dev(dibs);
		dibs->priv[client->id] = NULL;
	}

	mutex_lock(&clients_lock);
	clients[client->id] = NULL;
	if (client->id + 1 == max_client)
		max_client--;
	mutex_unlock(&clients_lock);

	mutex_unlock(&dibs_dev_list.mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(dibs_unregister_client);

static void dibs_dev_release(struct device *dev)
{
	struct dibs_dev *dibs;

	dibs = container_of(dev, struct dibs_dev, dev);

	kfree(dibs);
}

struct dibs_dev *dibs_dev_alloc(void)
{
	struct dibs_dev *dibs;

	dibs = kzalloc(sizeof(*dibs), GFP_KERNEL);
	if (!dibs)
		return dibs;
	dibs->dev.release = dibs_dev_release;
	dibs->dev.class = dibs_class;
	device_initialize(&dibs->dev);

	return dibs;
}
EXPORT_SYMBOL_GPL(dibs_dev_alloc);

static ssize_t gid_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct dibs_dev *dibs;

	dibs = container_of(dev, struct dibs_dev, dev);

	return sysfs_emit(buf, "%pUb\n", &dibs->gid);
}
static DEVICE_ATTR_RO(gid);

static ssize_t fabric_id_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct dibs_dev *dibs;
	u16 fabric_id;

	dibs = container_of(dev, struct dibs_dev, dev);
	fabric_id = dibs->ops->get_fabric_id(dibs);

	return sysfs_emit(buf, "0x%04x\n", fabric_id);
}
static DEVICE_ATTR_RO(fabric_id);

static struct attribute *dibs_dev_attrs[] = {
	&dev_attr_gid.attr,
	&dev_attr_fabric_id.attr,
	NULL,
};

static const struct attribute_group dibs_dev_attr_group = {
	.attrs = dibs_dev_attrs,
};

int dibs_dev_add(struct dibs_dev *dibs)
{
	int i, ret;

	ret = device_add(&dibs->dev);
	if (ret)
		return ret;

	ret = sysfs_create_group(&dibs->dev.kobj, &dibs_dev_attr_group);
	if (ret) {
		dev_err(&dibs->dev, "sysfs_create_group failed for dibs_dev\n");
		goto err_device_del;
	}
	mutex_lock(&dibs_dev_list.mutex);
	mutex_lock(&clients_lock);
	for (i = 0; i < max_client; ++i) {
		if (clients[i])
			clients[i]->ops->add_dev(dibs);
	}
	mutex_unlock(&clients_lock);
	list_add(&dibs->list, &dibs_dev_list.list);
	mutex_unlock(&dibs_dev_list.mutex);

	return 0;

err_device_del:
	device_del(&dibs->dev);
	return ret;

}
EXPORT_SYMBOL_GPL(dibs_dev_add);

void dibs_dev_del(struct dibs_dev *dibs)
{
	int i;

	mutex_lock(&dibs_dev_list.mutex);
	mutex_lock(&clients_lock);
	for (i = 0; i < max_client; ++i) {
		if (clients[i])
			clients[i]->ops->del_dev(dibs);
	}
	mutex_unlock(&clients_lock);
	list_del_init(&dibs->list);
	mutex_unlock(&dibs_dev_list.mutex);

	device_del(&dibs->dev);
}
EXPORT_SYMBOL_GPL(dibs_dev_del);

static int __init dibs_init(void)
{
	int rc;

	memset(clients, 0, sizeof(clients));
	max_client = 0;

	dibs_class = class_create("dibs");
	if (IS_ERR(&dibs_class))
		return PTR_ERR(&dibs_class);

	rc = dibs_loopback_init();
	if (rc)
		pr_err("%s fails with %d\n", __func__, rc);

	return rc;
}

static void __exit dibs_exit(void)
{
	dibs_loopback_exit();
	class_destroy(dibs_class);
}

module_init(dibs_init);
module_exit(dibs_exit);
