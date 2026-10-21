// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023-2026 Intel Corporation */
#include <linux/bug.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sprintf.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uuid.h>

#include "issei_dev.h"
#include "fw_client.h"

/*
 * Specific attribute handlers for fw_clients kset.
 * Provide only show function as all fw_client attributes are read-only.
 */

struct issei_fw_cl_attr {
	struct attribute attr;
	ssize_t (*show)(struct issei_fw_client *fw_cl, const struct issei_fw_cl_attr *attr,
			char *buf);
};
#define to_issei_fw_cl_attr(x) container_of_const(x, struct issei_fw_cl_attr, attr)

static ssize_t fw_cl_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	const struct issei_fw_cl_attr *issei_attr;
	struct issei_fw_client *fw_cl;

	issei_attr = to_issei_fw_cl_attr(attr);
	fw_cl = to_issei_fw_client(kobj);

	if (!issei_attr->show)
		return -EIO;

	return issei_attr->show(fw_cl, issei_attr, buf);
}

static const struct sysfs_ops fw_cl_sysfs_ops = {
	.show = fw_cl_attr_show,
};

#define FW_CL_ATTR_RO(_name) \
	struct issei_fw_cl_attr fw_cl_attr_##_name = __ATTR_RO(_name)

/* fw_client attributes */

static ssize_t id_show(struct issei_fw_client *fw_cl,
		       const struct issei_fw_cl_attr *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", fw_cl->id);
}
static FW_CL_ATTR_RO(id);

static ssize_t ver_show(struct issei_fw_client *fw_cl,
			const struct issei_fw_cl_attr *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", fw_cl->ver);
}
static FW_CL_ATTR_RO(ver);

static ssize_t uuid_show(struct issei_fw_client *fw_cl,
			 const struct issei_fw_cl_attr *attr, char *buf)
{
	return sysfs_emit(buf, "%pUb\n", &fw_cl->uuid);
}
static FW_CL_ATTR_RO(uuid);

static ssize_t mtu_show(struct issei_fw_client *fw_cl,
			const struct issei_fw_cl_attr *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", fw_cl->mtu);
}
static FW_CL_ATTR_RO(mtu);

static const struct attribute *const fw_cl_attrs[] = {
	&fw_cl_attr_id.attr,
	&fw_cl_attr_ver.attr,
	&fw_cl_attr_uuid.attr,
	&fw_cl_attr_mtu.attr,
	NULL,
};

static const struct attribute_group fw_cl_group = {
	.attrs_const = fw_cl_attrs,
};
__ATTRIBUTE_GROUPS(fw_cl);

static void issei_fw_cl_init(struct issei_fw_client *fw_cl, u16 id, u8 ver, const uuid_t *uuid,
			     u32 mtu, u32 flags)
{
	INIT_LIST_HEAD(&fw_cl->list);
	fw_cl->id = id;
	fw_cl->ver = ver;
	fw_cl->uuid = *uuid;
	fw_cl->mtu = mtu;
	fw_cl->flags = flags;
}

static void fw_cl_release(struct kobject *kobj)
{
	struct issei_fw_client *fw_cl = to_issei_fw_client(kobj);

	kfree(fw_cl);
}

static const struct kobj_type fw_client_ktype = {
	.sysfs_ops = &fw_cl_sysfs_ops,
	.release = fw_cl_release,
	.default_groups = fw_cl_groups,
};

/**
 * issei_fw_cl_create - create firmware client object and add to list
 * @idev: issei device object
 * @id: firmware client id
 * @ver: firmware client version
 * @uuid: firmware client unique id
 * @mtu: firmware client maximum message size
 * @flags: firmware client flags
 *
 * Should be called under idev->client_lock
 *
 * Return: pointer to newly created object on success, ERR_PTR on failure
 */
struct issei_fw_client *issei_fw_cl_create(struct issei_device *idev, u16 id, u8 ver,
					   const uuid_t *uuid, u32 mtu, u32 flags)
{
	int ret;
	struct issei_fw_client *fw_cl = kzalloc_obj(*fw_cl);

	if (!fw_cl)
		return ERR_PTR(-ENOMEM);

	WARN_ON(!mutex_is_locked(&idev->client_lock));

	issei_fw_cl_init(fw_cl, id, ver, uuid, mtu, flags);
	fw_cl->kobj.kset = idev->fw_clients;

	ret = kobject_init_and_add(&fw_cl->kobj, &fw_client_ktype, NULL, "%u", id);
	if (ret) {
		kobject_put(&fw_cl->kobj);
		return ERR_PTR(ret);
	}

	list_add_tail(&fw_cl->list, &idev->fw_client_list);

	dev_dbg(&idev->dev, "FW client %pUb created\n", uuid);

	kobject_uevent(&fw_cl->kobj, KOBJ_ADD);

	return fw_cl;
}

static void __issei_fw_cl_remove(struct issei_device *idev, struct issei_fw_client *fw_cl)
{
	WARN(fw_cl->cl, "Removing connected client!\n");

	dev_dbg(&idev->dev, "FW client %pUb will be removed\n", &fw_cl->uuid);

	list_del(&fw_cl->list);
	kobject_put(&fw_cl->kobj);
}

/**
 * issei_fw_cl_remove_all - remove all firmware client objects
 * @idev: issei device object
 */
void issei_fw_cl_remove_all(struct issei_device *idev)
{
	struct issei_fw_client *fw_cl, *next;

	guard(mutex)(&idev->client_lock);

	list_for_each_entry_safe(fw_cl, next, &idev->fw_client_list, list)
		__issei_fw_cl_remove(idev, fw_cl);
}

/**
 * issei_fw_cl_find_by_uuid - find firmware client by uuid
 * @idev: issei device object
 * @uuid: uuid to search by it
 *
 * Should be called under idev->client_lock
 *
 * Return: pointer to firmware client object if found, NULL on failure
 */
struct issei_fw_client *issei_fw_cl_find_by_uuid(struct issei_device *idev, const uuid_t *uuid)
{
	struct issei_fw_client *fw_cl;

	WARN_ON(!mutex_is_locked(&idev->client_lock));

	list_for_each_entry(fw_cl, &idev->fw_client_list, list) {
		if (uuid_equal(&fw_cl->uuid, uuid)) {
			kobject_get(&fw_cl->kobj);
			return fw_cl;
		}
	}
	return NULL;
}

/**
 * issei_fw_cl_connect - connect firmware and host client
 * @fw_cl: firmware client
 * @cl: host client
 *
 * Should be called under idev->client_lock
 *
 * Return: 0 on success, -EBUSY if already connected
 */
int issei_fw_cl_connect(struct issei_fw_client *fw_cl, struct issei_host_client *cl)
{
	if (fw_cl->cl)
		return -EBUSY;

	kobject_get(&fw_cl->kobj);
	fw_cl->cl = cl;
	return 0;
}

/**
 * issei_fw_cl_disconnect - disconnect firmware and host client
 * @fw_cl: firmware client
 *
 * Should be called under idev->client_lock
 */
void issei_fw_cl_disconnect(struct issei_fw_client *fw_cl)
{
	WARN_ON(!fw_cl->cl);

	fw_cl->cl = NULL;
	kobject_put(&fw_cl->kobj);
}
