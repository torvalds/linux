// SPDX-License-Identifier: GPL-2.0-only
/*
 * isst_tpmi.c: SST TPMI interface core
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
 *
 * This information will be useful to understand flows:
 * In the current generation of platforms, TPMI is supported via OOB
 * PCI device. This PCI device has one instance per CPU package.
 * There is a unique TPMI ID for SST. Each TPMI ID also has multiple
 * entries, representing per power domain information.
 *
 * There is one dev file for complete SST information and control same as the
 * prior generation of hardware. User spaces don't need to know how the
 * information is presented by the hardware. The TPMI core module implements
 * the hardware mapping.
 */

#include <linux/auxiliary_bus.h>
#include <linux/intel_tpmi.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <uapi/linux/isst_if.h>

#include "isst_tpmi_core.h"
#include "isst_if_common.h"

/**
 * struct tpmi_per_power_domain_info -	Store per power_domain SST info
 * @package_id:		Package id for this power_domain
 * @power_domain_id:	Power domain id, Each entry from the SST-TPMI instance is a power_domain.
 * @sst_base:		Mapped SST base IO memory
 * @auxdev:		Auxiliary device instance enumerated this instance
 *
 * This structure is used store complete SST information for a power_domain. This information
 * is used to read/write request for any SST IOCTL. Each physical CPU package can have multiple
 * power_domains. Each power domain describes its own SST information and has its own controls.
 */
struct tpmi_per_power_domain_info {
	int package_id;
	int power_domain_id;
	void __iomem *sst_base;
	struct auxiliary_device *auxdev;
};

/**
 * struct tpmi_sst_struct -	Store sst info for a package
 * @package_id:			Package id for this aux device instance
 * @number_of_power_domains:	Number of power_domains pointed by power_domain_info pointer
 * @power_domain_info:		Pointer to power domains information
 *
 * This structure is used store full SST information for a package.
 * Each package has a unique OOB PCI device, which enumerates TPMI.
 * Each Package will have multiple power_domains.
 */
struct tpmi_sst_struct {
	int package_id;
	int number_of_power_domains;
	struct tpmi_per_power_domain_info *power_domain_info;
};

/**
 * struct tpmi_sst_common_struct -	Store all SST instances
 * @max_index:		Maximum instances currently present
 * @sst_inst:		Pointer to per package instance
 *
 * Stores every SST Package instance.
 */
struct tpmi_sst_common_struct {
	int max_index;
	struct tpmi_sst_struct **sst_inst;
};

/*
 * Each IOCTL request is processed under this lock. Also used to protect
 * registration functions and common data structures.
 */
static DEFINE_MUTEX(isst_tpmi_dev_lock);

/* Usage count to track, number of TPMI SST instances registered to this core. */
static int isst_core_usage_count;

/* Stores complete SST information for every package and power_domain */
static struct tpmi_sst_common_struct isst_common;

static int isst_if_get_tpmi_instance_count(void __user *argp)
{
	struct isst_tpmi_instance_count tpmi_inst;
	struct tpmi_sst_struct *sst_inst;
	int i;

	if (copy_from_user(&tpmi_inst, argp, sizeof(tpmi_inst)))
		return -EFAULT;

	if (tpmi_inst.socket_id >= topology_max_packages())
		return -EINVAL;

	tpmi_inst.count = isst_common.sst_inst[tpmi_inst.socket_id]->number_of_power_domains;

	sst_inst = isst_common.sst_inst[tpmi_inst.socket_id];
	tpmi_inst.valid_mask = 0;
	for (i = 0; i < sst_inst->number_of_power_domains; ++i) {
		struct tpmi_per_power_domain_info *power_domain_info;

		power_domain_info = &sst_inst->power_domain_info[i];
		if (power_domain_info->sst_base)
			tpmi_inst.valid_mask |= BIT(i);
	}

	if (copy_to_user(argp, &tpmi_inst, sizeof(tpmi_inst)))
		return -EFAULT;

	return 0;
}

static long isst_if_def_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long ret = -ENOTTY;

	mutex_lock(&isst_tpmi_dev_lock);
	switch (cmd) {
	case ISST_IF_COUNT_TPMI_INSTANCES:
		ret = isst_if_get_tpmi_instance_count(argp);
		break;
	default:
		break;
	}
	mutex_unlock(&isst_tpmi_dev_lock);

	return ret;
}

int tpmi_sst_dev_add(struct auxiliary_device *auxdev)
{
	struct intel_tpmi_plat_info *plat_info;
	struct tpmi_sst_struct *tpmi_sst;
	int i, pkg = 0, inst = 0;
	int num_resources;

	plat_info = tpmi_get_platform_data(auxdev);
	if (!plat_info) {
		dev_err(&auxdev->dev, "No platform info\n");
		return -EINVAL;
	}

	pkg = plat_info->package_id;
	if (pkg >= topology_max_packages()) {
		dev_err(&auxdev->dev, "Invalid package id :%x\n", pkg);
		return -EINVAL;
	}

	if (isst_common.sst_inst[pkg])
		return -EEXIST;

	num_resources = tpmi_get_resource_count(auxdev);

	if (!num_resources)
		return -EINVAL;

	tpmi_sst = devm_kzalloc(&auxdev->dev, sizeof(*tpmi_sst), GFP_KERNEL);
	if (!tpmi_sst)
		return -ENOMEM;

	tpmi_sst->power_domain_info = devm_kcalloc(&auxdev->dev, num_resources,
						   sizeof(*tpmi_sst->power_domain_info),
						   GFP_KERNEL);
	if (!tpmi_sst->power_domain_info)
		return -ENOMEM;

	tpmi_sst->number_of_power_domains = num_resources;

	for (i = 0; i < num_resources; ++i) {
		struct resource *res;

		res = tpmi_get_resource_at_index(auxdev, i);
		if (!res) {
			tpmi_sst->power_domain_info[i].sst_base = NULL;
			continue;
		}

		tpmi_sst->power_domain_info[i].package_id = pkg;
		tpmi_sst->power_domain_info[i].power_domain_id = i;
		tpmi_sst->power_domain_info[i].auxdev = auxdev;
		tpmi_sst->power_domain_info[i].sst_base = devm_ioremap_resource(&auxdev->dev, res);
		if (IS_ERR(tpmi_sst->power_domain_info[i].sst_base))
			return PTR_ERR(tpmi_sst->power_domain_info[i].sst_base);

		++inst;
	}

	if (!inst)
		return -ENODEV;

	tpmi_sst->package_id = pkg;
	auxiliary_set_drvdata(auxdev, tpmi_sst);

	mutex_lock(&isst_tpmi_dev_lock);
	if (isst_common.max_index < pkg)
		isst_common.max_index = pkg;
	isst_common.sst_inst[pkg] = tpmi_sst;
	mutex_unlock(&isst_tpmi_dev_lock);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_dev_add, INTEL_TPMI_SST);

void tpmi_sst_dev_remove(struct auxiliary_device *auxdev)
{
	struct tpmi_sst_struct *tpmi_sst = auxiliary_get_drvdata(auxdev);

	mutex_lock(&isst_tpmi_dev_lock);
	isst_common.sst_inst[tpmi_sst->package_id] = NULL;
	mutex_unlock(&isst_tpmi_dev_lock);
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_dev_remove, INTEL_TPMI_SST);

#define ISST_TPMI_API_VERSION	0x02

int tpmi_sst_init(void)
{
	struct isst_if_cmd_cb cb;
	int ret = 0;

	mutex_lock(&isst_tpmi_dev_lock);

	if (isst_core_usage_count) {
		++isst_core_usage_count;
		goto init_done;
	}

	isst_common.sst_inst = kcalloc(topology_max_packages(),
				       sizeof(*isst_common.sst_inst),
				       GFP_KERNEL);
	if (!isst_common.sst_inst)
		return -ENOMEM;

	memset(&cb, 0, sizeof(cb));
	cb.cmd_size = sizeof(struct isst_if_io_reg);
	cb.offset = offsetof(struct isst_if_io_regs, io_reg);
	cb.cmd_callback = NULL;
	cb.api_version = ISST_TPMI_API_VERSION;
	cb.def_ioctl = isst_if_def_ioctl;
	cb.owner = THIS_MODULE;
	ret = isst_if_cdev_register(ISST_IF_DEV_TPMI, &cb);
	if (ret)
		kfree(isst_common.sst_inst);
init_done:
	mutex_unlock(&isst_tpmi_dev_lock);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_init, INTEL_TPMI_SST);

void tpmi_sst_exit(void)
{
	mutex_lock(&isst_tpmi_dev_lock);
	if (isst_core_usage_count)
		--isst_core_usage_count;

	if (!isst_core_usage_count) {
		isst_if_cdev_unregister(ISST_IF_DEV_TPMI);
		kfree(isst_common.sst_inst);
	}
	mutex_unlock(&isst_tpmi_dev_lock);
}
EXPORT_SYMBOL_NS_GPL(tpmi_sst_exit, INTEL_TPMI_SST);

MODULE_IMPORT_NS(INTEL_TPMI);
MODULE_IMPORT_NS(INTEL_TPMI_POWER_DOMAIN);

MODULE_LICENSE("GPL");
