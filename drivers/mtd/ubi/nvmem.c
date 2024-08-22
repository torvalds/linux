// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023 Daniel Golle <daniel@makrotopia.org>
 */

/* UBI NVMEM provider */
#include "ubi.h"
#include <linux/nvmem-provider.h>

/* List of all NVMEM devices */
static LIST_HEAD(nvmem_devices);
static DEFINE_MUTEX(devices_mutex);

struct ubi_nvmem {
	struct nvmem_device *nvmem;
	int ubi_num;
	int vol_id;
	int usable_leb_size;
	struct list_head list;
};

static int ubi_nvmem_reg_read(void *priv, unsigned int from,
			      void *val, size_t bytes)
{
	size_t to_read, bytes_left = bytes;
	struct ubi_nvmem *unv = priv;
	struct ubi_volume_desc *desc;
	uint32_t offs;
	uint32_t lnum;
	int err = 0;

	desc = ubi_open_volume(unv->ubi_num, unv->vol_id, UBI_READONLY);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	offs = from % unv->usable_leb_size;
	lnum = from / unv->usable_leb_size;
	while (bytes_left) {
		to_read = unv->usable_leb_size - offs;

		if (to_read > bytes_left)
			to_read = bytes_left;

		err = ubi_read(desc, lnum, val, offs, to_read);
		if (err)
			break;

		lnum += 1;
		offs = 0;
		bytes_left -= to_read;
		val += to_read;
	}
	ubi_close_volume(desc);

	if (err)
		return err;

	return bytes_left == 0 ? 0 : -EIO;
}

static int ubi_nvmem_add(struct ubi_volume_info *vi)
{
	struct device_node *np = dev_of_node(vi->dev);
	struct nvmem_config config = {};
	struct ubi_nvmem *unv;
	int ret;

	if (!np)
		return 0;

	if (!of_get_child_by_name(np, "nvmem-layout"))
		return 0;

	if (WARN_ON_ONCE(vi->usable_leb_size <= 0) ||
	    WARN_ON_ONCE(vi->size <= 0))
		return -EINVAL;

	unv = kzalloc(sizeof(struct ubi_nvmem), GFP_KERNEL);
	if (!unv)
		return -ENOMEM;

	config.id = NVMEM_DEVID_NONE;
	config.dev = vi->dev;
	config.name = dev_name(vi->dev);
	config.owner = THIS_MODULE;
	config.priv = unv;
	config.reg_read = ubi_nvmem_reg_read;
	config.size = vi->usable_leb_size * vi->size;
	config.word_size = 1;
	config.stride = 1;
	config.read_only = true;
	config.root_only = true;
	config.ignore_wp = true;
	config.of_node = np;

	unv->ubi_num = vi->ubi_num;
	unv->vol_id = vi->vol_id;
	unv->usable_leb_size = vi->usable_leb_size;
	unv->nvmem = nvmem_register(&config);
	if (IS_ERR(unv->nvmem)) {
		ret = dev_err_probe(vi->dev, PTR_ERR(unv->nvmem),
				    "Failed to register NVMEM device\n");
		kfree(unv);
		return ret;
	}

	mutex_lock(&devices_mutex);
	list_add_tail(&unv->list, &nvmem_devices);
	mutex_unlock(&devices_mutex);

	return 0;
}

static void ubi_nvmem_remove(struct ubi_volume_info *vi)
{
	struct ubi_nvmem *unv_c, *unv = NULL;

	mutex_lock(&devices_mutex);
	list_for_each_entry(unv_c, &nvmem_devices, list)
		if (unv_c->ubi_num == vi->ubi_num && unv_c->vol_id == vi->vol_id) {
			unv = unv_c;
			break;
		}

	if (!unv) {
		mutex_unlock(&devices_mutex);
		return;
	}

	list_del(&unv->list);
	mutex_unlock(&devices_mutex);
	nvmem_unregister(unv->nvmem);
	kfree(unv);
}

/**
 * nvmem_notify - UBI notification handler.
 * @nb: registered notifier block
 * @l: notification type
 * @ns_ptr: pointer to the &struct ubi_notification object
 */
static int nvmem_notify(struct notifier_block *nb, unsigned long l,
			 void *ns_ptr)
{
	struct ubi_notification *nt = ns_ptr;

	switch (l) {
	case UBI_VOLUME_RESIZED:
		ubi_nvmem_remove(&nt->vi);
		fallthrough;
	case UBI_VOLUME_ADDED:
		ubi_nvmem_add(&nt->vi);
		break;
	case UBI_VOLUME_SHUTDOWN:
		ubi_nvmem_remove(&nt->vi);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block nvmem_notifier = {
	.notifier_call = nvmem_notify,
};

static int __init ubi_nvmem_init(void)
{
	return ubi_register_volume_notifier(&nvmem_notifier, 0);
}

static void __exit ubi_nvmem_exit(void)
{
	struct ubi_nvmem *unv, *tmp;

	mutex_lock(&devices_mutex);
	list_for_each_entry_safe(unv, tmp, &nvmem_devices, list) {
		nvmem_unregister(unv->nvmem);
		list_del(&unv->list);
		kfree(unv);
	}
	mutex_unlock(&devices_mutex);

	ubi_unregister_volume_notifier(&nvmem_notifier);
}

module_init(ubi_nvmem_init);
module_exit(ubi_nvmem_exit);
MODULE_DESCRIPTION("NVMEM layer over UBI volumes");
MODULE_AUTHOR("Daniel Golle");
MODULE_LICENSE("GPL");
