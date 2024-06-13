// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 *
 * sysfs support for blk-crypto.  This file contains the code which exports the
 * crypto capabilities of devices via /sys/block/$disk/queue/crypto/.
 */

#include <linux/blk-crypto-profile.h>

#include "blk-crypto-internal.h"

struct blk_crypto_kobj {
	struct kobject kobj;
	struct blk_crypto_profile *profile;
};

struct blk_crypto_attr {
	struct attribute attr;
	ssize_t (*show)(struct blk_crypto_profile *profile,
			struct blk_crypto_attr *attr, char *page);
};

static struct blk_crypto_profile *kobj_to_crypto_profile(struct kobject *kobj)
{
	return container_of(kobj, struct blk_crypto_kobj, kobj)->profile;
}

static struct blk_crypto_attr *attr_to_crypto_attr(struct attribute *attr)
{
	return container_of(attr, struct blk_crypto_attr, attr);
}

static ssize_t max_dun_bits_show(struct blk_crypto_profile *profile,
				 struct blk_crypto_attr *attr, char *page)
{
	return sysfs_emit(page, "%u\n", 8 * profile->max_dun_bytes_supported);
}

static ssize_t num_keyslots_show(struct blk_crypto_profile *profile,
				 struct blk_crypto_attr *attr, char *page)
{
	return sysfs_emit(page, "%u\n", profile->num_slots);
}

#define BLK_CRYPTO_RO_ATTR(_name) \
	static struct blk_crypto_attr _name##_attr = __ATTR_RO(_name)

BLK_CRYPTO_RO_ATTR(max_dun_bits);
BLK_CRYPTO_RO_ATTR(num_keyslots);

static struct attribute *blk_crypto_attrs[] = {
	&max_dun_bits_attr.attr,
	&num_keyslots_attr.attr,
	NULL,
};

static const struct attribute_group blk_crypto_attr_group = {
	.attrs = blk_crypto_attrs,
};

/*
 * The encryption mode attributes.  To avoid hard-coding the list of encryption
 * modes, these are initialized at boot time by blk_crypto_sysfs_init().
 */
static struct blk_crypto_attr __blk_crypto_mode_attrs[BLK_ENCRYPTION_MODE_MAX];
static struct attribute *blk_crypto_mode_attrs[BLK_ENCRYPTION_MODE_MAX + 1];

static umode_t blk_crypto_mode_is_visible(struct kobject *kobj,
					  struct attribute *attr, int n)
{
	struct blk_crypto_profile *profile = kobj_to_crypto_profile(kobj);
	struct blk_crypto_attr *a = attr_to_crypto_attr(attr);
	int mode_num = a - __blk_crypto_mode_attrs;

	if (profile->modes_supported[mode_num])
		return 0444;
	return 0;
}

static ssize_t blk_crypto_mode_show(struct blk_crypto_profile *profile,
				    struct blk_crypto_attr *attr, char *page)
{
	int mode_num = attr - __blk_crypto_mode_attrs;

	return sysfs_emit(page, "0x%x\n", profile->modes_supported[mode_num]);
}

static const struct attribute_group blk_crypto_modes_attr_group = {
	.name = "modes",
	.attrs = blk_crypto_mode_attrs,
	.is_visible = blk_crypto_mode_is_visible,
};

static const struct attribute_group *blk_crypto_attr_groups[] = {
	&blk_crypto_attr_group,
	&blk_crypto_modes_attr_group,
	NULL,
};

static ssize_t blk_crypto_attr_show(struct kobject *kobj,
				    struct attribute *attr, char *page)
{
	struct blk_crypto_profile *profile = kobj_to_crypto_profile(kobj);
	struct blk_crypto_attr *a = attr_to_crypto_attr(attr);

	return a->show(profile, a, page);
}

static const struct sysfs_ops blk_crypto_attr_ops = {
	.show = blk_crypto_attr_show,
};

static void blk_crypto_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct blk_crypto_kobj, kobj));
}

static struct kobj_type blk_crypto_ktype = {
	.default_groups = blk_crypto_attr_groups,
	.sysfs_ops	= &blk_crypto_attr_ops,
	.release	= blk_crypto_release,
};

/*
 * If the request_queue has a blk_crypto_profile, create the "crypto"
 * subdirectory in sysfs (/sys/block/$disk/queue/crypto/).
 */
int blk_crypto_sysfs_register(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct blk_crypto_kobj *obj;
	int err;

	if (!q->crypto_profile)
		return 0;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;
	obj->profile = q->crypto_profile;

	err = kobject_init_and_add(&obj->kobj, &blk_crypto_ktype, &q->kobj,
				   "crypto");
	if (err) {
		kobject_put(&obj->kobj);
		return err;
	}
	q->crypto_kobject = &obj->kobj;
	return 0;
}

void blk_crypto_sysfs_unregister(struct gendisk *disk)
{
	kobject_put(disk->queue->crypto_kobject);
}

static int __init blk_crypto_sysfs_init(void)
{
	int i;

	BUILD_BUG_ON(BLK_ENCRYPTION_MODE_INVALID != 0);
	for (i = 1; i < BLK_ENCRYPTION_MODE_MAX; i++) {
		struct blk_crypto_attr *attr = &__blk_crypto_mode_attrs[i];

		attr->attr.name = blk_crypto_modes[i].name;
		attr->attr.mode = 0444;
		attr->show = blk_crypto_mode_show;
		blk_crypto_mode_attrs[i - 1] = &attr->attr;
	}
	return 0;
}
subsys_initcall(blk_crypto_sysfs_init);
