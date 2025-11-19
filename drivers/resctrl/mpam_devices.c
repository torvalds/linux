// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Arm Ltd.

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/acpi.h>
#include <linux/arm_mpam.h>
#include <linux/cacheinfo.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/srcu.h>
#include <linux/types.h>

#include "mpam_internal.h"

/*
 * mpam_list_lock protects the SRCU lists when writing. Once the
 * mpam_enabled key is enabled these lists are read-only,
 * unless the error interrupt disables the driver.
 */
static DEFINE_MUTEX(mpam_list_lock);
static LIST_HEAD(mpam_all_msc);

struct srcu_struct mpam_srcu;

/*
 * Number of MSCs that have been probed. Once all MSCs have been probed MPAM
 * can be enabled.
 */
static atomic_t mpam_num_msc;

/*
 * An MSC can control traffic from a set of CPUs, but may only be accessible
 * from a (hopefully wider) set of CPUs. The common reason for this is power
 * management. If all the CPUs in a cluster are in PSCI:CPU_SUSPEND, the
 * corresponding cache may also be powered off. By making accesses from
 * one of those CPUs, we ensure we don't access a cache that's powered off.
 */
static void update_msc_accessibility(struct mpam_msc *msc)
{
	u32 affinity_id;
	int err;

	err = device_property_read_u32(&msc->pdev->dev, "cpu_affinity",
				       &affinity_id);
	if (err)
		cpumask_copy(&msc->accessibility, cpu_possible_mask);
	else
		acpi_pptt_get_cpus_from_container(affinity_id, &msc->accessibility);
}

static void mpam_msc_destroy(struct mpam_msc *msc)
{
	struct platform_device *pdev = msc->pdev;

	lockdep_assert_held(&mpam_list_lock);

	list_del_rcu(&msc->all_msc_list);
	platform_set_drvdata(pdev, NULL);
}

static void mpam_msc_drv_remove(struct platform_device *pdev)
{
	struct mpam_msc *msc = platform_get_drvdata(pdev);

	mutex_lock(&mpam_list_lock);
	mpam_msc_destroy(msc);
	mutex_unlock(&mpam_list_lock);

	synchronize_srcu(&mpam_srcu);
}

static struct mpam_msc *do_mpam_msc_drv_probe(struct platform_device *pdev)
{
	int err;
	u32 tmp;
	struct mpam_msc *msc;
	struct resource *msc_res;
	struct device *dev = &pdev->dev;

	lockdep_assert_held(&mpam_list_lock);

	msc = devm_kzalloc(&pdev->dev, sizeof(*msc), GFP_KERNEL);
	if (!msc)
		return ERR_PTR(-ENOMEM);

	err = devm_mutex_init(dev, &msc->probe_lock);
	if (err)
		return ERR_PTR(err);

	err = devm_mutex_init(dev, &msc->part_sel_lock);
	if (err)
		return ERR_PTR(err);

	msc->id = pdev->id;
	msc->pdev = pdev;
	INIT_LIST_HEAD_RCU(&msc->all_msc_list);
	INIT_LIST_HEAD_RCU(&msc->ris);

	update_msc_accessibility(msc);
	if (cpumask_empty(&msc->accessibility)) {
		dev_err_once(dev, "MSC is not accessible from any CPU!");
		return ERR_PTR(-EINVAL);
	}

	if (device_property_read_u32(&pdev->dev, "pcc-channel", &tmp))
		msc->iface = MPAM_IFACE_MMIO;
	else
		msc->iface = MPAM_IFACE_PCC;

	if (msc->iface == MPAM_IFACE_MMIO) {
		void __iomem *io;

		io = devm_platform_get_and_ioremap_resource(pdev, 0,
							    &msc_res);
		if (IS_ERR(io)) {
			dev_err_once(dev, "Failed to map MSC base address\n");
			return ERR_CAST(io);
		}
		msc->mapped_hwpage_sz = msc_res->end - msc_res->start;
		msc->mapped_hwpage = io;
	} else {
		return ERR_PTR(-EINVAL);
	}

	list_add_rcu(&msc->all_msc_list, &mpam_all_msc);
	platform_set_drvdata(pdev, msc);

	return msc;
}

static int fw_num_msc;

static int mpam_msc_drv_probe(struct platform_device *pdev)
{
	int err;
	struct mpam_msc *msc = NULL;
	void *plat_data = pdev->dev.platform_data;

	mutex_lock(&mpam_list_lock);
	msc = do_mpam_msc_drv_probe(pdev);
	mutex_unlock(&mpam_list_lock);

	if (IS_ERR(msc))
		return PTR_ERR(msc);

	/* Create RIS entries described by firmware */
	err = acpi_mpam_parse_resources(msc, plat_data);
	if (err) {
		mpam_msc_drv_remove(pdev);
		return err;
	}

	if (atomic_add_return(1, &mpam_num_msc) == fw_num_msc)
		pr_info("Discovered all MSCs\n");

	return 0;
}

static struct platform_driver mpam_msc_driver = {
	.driver = {
		.name = "mpam_msc",
	},
	.probe = mpam_msc_drv_probe,
	.remove = mpam_msc_drv_remove,
};

static int __init mpam_msc_driver_init(void)
{
	if (!system_supports_mpam())
		return -EOPNOTSUPP;

	init_srcu_struct(&mpam_srcu);

	fw_num_msc = acpi_mpam_count_msc();
	if (fw_num_msc <= 0) {
		pr_err("No MSC devices found in firmware\n");
		return -EINVAL;
	}

	return platform_driver_register(&mpam_msc_driver);
}
subsys_initcall(mpam_msc_driver_init);
