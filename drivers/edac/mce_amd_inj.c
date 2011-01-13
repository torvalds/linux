/*
 * A simple MCE injection facility for testing the MCE decoding code. This
 * driver should be built as module so that it can be loaded on production
 * kernels for testing purposes.
 *
 * This file may be distributed under the terms of the GNU General Public
 * License version 2.
 *
 * Copyright (c) 2010:  Borislav Petkov <borislav.petkov@amd.com>
 *			Advanced Micro Devices Inc.
 */

#include <linux/kobject.h>
#include <linux/sysdev.h>
#include <linux/edac.h>
#include <asm/mce.h>

#include "mce_amd.h"

struct edac_mce_attr {
	struct attribute attr;
	ssize_t (*show) (struct kobject *kobj, struct edac_mce_attr *attr, char *buf);
	ssize_t (*store)(struct kobject *kobj, struct edac_mce_attr *attr,
			 const char *buf, size_t count);
};

#define EDAC_MCE_ATTR(_name, _mode, _show, _store)			\
static struct edac_mce_attr mce_attr_##_name = __ATTR(_name, _mode, _show, _store)

static struct kobject *mce_kobj;

/*
 * Collect all the MCi_XXX settings
 */
static struct mce i_mce;

#define MCE_INJECT_STORE(reg)						\
static ssize_t edac_inject_##reg##_store(struct kobject *kobj,		\
					 struct edac_mce_attr *attr,	\
					 const char *data, size_t count)\
{									\
	int ret = 0;							\
	unsigned long value;						\
									\
	ret = strict_strtoul(data, 16, &value);				\
	if (ret < 0)							\
		printk(KERN_ERR "Error writing MCE " #reg " field.\n");	\
									\
	i_mce.reg = value;						\
									\
	return count;							\
}

MCE_INJECT_STORE(status);
MCE_INJECT_STORE(misc);
MCE_INJECT_STORE(addr);

#define MCE_INJECT_SHOW(reg)						\
static ssize_t edac_inject_##reg##_show(struct kobject *kobj,		\
					struct edac_mce_attr *attr,	\
					char *buf)			\
{									\
	return sprintf(buf, "0x%016llx\n", i_mce.reg);			\
}

MCE_INJECT_SHOW(status);
MCE_INJECT_SHOW(misc);
MCE_INJECT_SHOW(addr);

EDAC_MCE_ATTR(status, 0644, edac_inject_status_show, edac_inject_status_store);
EDAC_MCE_ATTR(misc, 0644, edac_inject_misc_show, edac_inject_misc_store);
EDAC_MCE_ATTR(addr, 0644, edac_inject_addr_show, edac_inject_addr_store);

/*
 * This denotes into which bank we're injecting and triggers
 * the injection, at the same time.
 */
static ssize_t edac_inject_bank_store(struct kobject *kobj,
				      struct edac_mce_attr *attr,
				      const char *data, size_t count)
{
	int ret = 0;
	unsigned long value;

	ret = strict_strtoul(data, 10, &value);
	if (ret < 0) {
		printk(KERN_ERR "Invalid bank value!\n");
		return -EINVAL;
	}

	if (value > 5)
		if (boot_cpu_data.x86 != 0x15 || value > 6) {
			printk(KERN_ERR "Non-existant MCE bank: %lu\n", value);
			return -EINVAL;
		}

	i_mce.bank = value;

	amd_decode_mce(NULL, 0, &i_mce);

	return count;
}

static ssize_t edac_inject_bank_show(struct kobject *kobj,
				     struct edac_mce_attr *attr, char *buf)
{
	return sprintf(buf, "%d\n", i_mce.bank);
}

EDAC_MCE_ATTR(bank, 0644, edac_inject_bank_show, edac_inject_bank_store);

static struct edac_mce_attr *sysfs_attrs[] = { &mce_attr_status, &mce_attr_misc,
					       &mce_attr_addr, &mce_attr_bank
};

static int __init edac_init_mce_inject(void)
{
	struct sysdev_class *edac_class = NULL;
	int i, err = 0;

	edac_class = edac_get_sysfs_class();
	if (!edac_class)
		return -EINVAL;

	mce_kobj = kobject_create_and_add("mce", &edac_class->kset.kobj);
	if (!mce_kobj) {
		printk(KERN_ERR "Error creating a mce kset.\n");
		err = -ENOMEM;
		goto err_mce_kobj;
	}

	for (i = 0; i < ARRAY_SIZE(sysfs_attrs); i++) {
		err = sysfs_create_file(mce_kobj, &sysfs_attrs[i]->attr);
		if (err) {
			printk(KERN_ERR "Error creating %s in sysfs.\n",
					sysfs_attrs[i]->attr.name);
			goto err_sysfs_create;
		}
	}
	return 0;

err_sysfs_create:
	while (--i >= 0)
		sysfs_remove_file(mce_kobj, &sysfs_attrs[i]->attr);

	kobject_del(mce_kobj);

err_mce_kobj:
	edac_put_sysfs_class();

	return err;
}

static void __exit edac_exit_mce_inject(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sysfs_attrs); i++)
		sysfs_remove_file(mce_kobj, &sysfs_attrs[i]->attr);

	kobject_del(mce_kobj);

	edac_put_sysfs_class();
}

module_init(edac_init_mce_inject);
module_exit(edac_exit_mce_inject);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Borislav Petkov <borislav.petkov@amd.com>");
MODULE_AUTHOR("AMD Inc.");
MODULE_DESCRIPTION("MCE injection facility for testing MCE decoding");
