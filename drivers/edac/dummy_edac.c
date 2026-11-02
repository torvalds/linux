// SPDX-License-Identifier: GPL-2.0
/*
 * Dummy EDAC driver
 *
 * A minimal EDAC device driver that registers its own platform device
 * unconditionally, so it probes successfully on any system -- including
 * VMs and guests that have no real ECC-capable cache/memory hardware
 * exposed to them. Useful for exercising the EDAC core, sysfs interface,
 * and userspace tooling (edac-utils, mcelog, etc.) without needing the
 * physical hardware the "real" drivers depend on.
 *
 * Modeled after drivers/edac/a72_edac.c (Cortex A72 EDAC L1/L2 cache
 * error detection), but with all of the hardware-specific bits (system
 * register reads, SMP cross-calls, devicetree compatible matching)
 * replaced by a software-only error source that can be poked from
 * debugfs or left to generate nothing at all.
 */

#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "edac_module.h"

#define DRVNAME		"dummy-edac"
#define MESSAGE_SIZE	64

/*
 * Simulated "error syndrome". In real hardware drivers this would be
 * read out of a per-CPU system/MMIO register. Here userspace (via
 * debugfs) or nothing at all drives it, which is exactly the point:
 * the driver has no dependency on any physical error source.
 */
struct dummy_edac_priv {
	struct dentry *debugfs_dir;
	atomic_t inject_ce;
	atomic_t inject_ue;
};

static void dummy_edac_check(struct edac_device_ctl_info *edac_ctl)
{
	struct dummy_edac_priv *priv = edac_ctl->pvt_info;
	int cpu = raw_smp_processor_id();
	char msg[MESSAGE_SIZE];

	if (atomic_xchg(&priv->inject_ce, 0)) {
		snprintf(msg, MESSAGE_SIZE,
			 "simulated correctable error on CPU %d", cpu);
		edac_device_handle_ce(edac_ctl, cpu, 0, msg);
	}

	if (atomic_xchg(&priv->inject_ue, 0)) {
		snprintf(msg, MESSAGE_SIZE,
			 "simulated uncorrectable error on CPU %d", cpu);
		edac_device_handle_ue(edac_ctl, cpu, 0, msg);
	}
}

/*
 * debugfs knobs so you can drive the driver from userspace inside the
 * guest, e.g.:
 *   echo 1 > /sys/kernel/debug/dummy-edac/inject_ce
 *   echo 1 > /sys/kernel/debug/dummy-edac/inject_ue
 */
static int inject_ce_set(void *data, u64 val)
{
	struct dummy_edac_priv *priv = data;

	if (val)
		atomic_set(&priv->inject_ce, 1);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(inject_ce_fops, NULL, inject_ce_set, "%llu\n");

static int inject_ue_set(void *data, u64 val)
{
	struct dummy_edac_priv *priv = data;

	if (val)
		atomic_set(&priv->inject_ue, 1);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(inject_ue_fops, NULL, inject_ue_set, "%llu\n");

static int dummy_edac_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_ctl;
	struct dummy_edac_priv *priv;
	struct device *dev = &pdev->dev;
	int rc;

	edac_ctl = edac_device_alloc_ctl_info(sizeof(*priv), "cpu",
					      num_possible_cpus(), "L", 1, 1,
					      edac_device_alloc_index());
	if (!edac_ctl)
		return -ENOMEM;

	priv = edac_ctl->pvt_info;
	atomic_set(&priv->inject_ce, 0);
	atomic_set(&priv->inject_ue, 0);

	edac_ctl->edac_check = dummy_edac_check;
	edac_ctl->dev = dev;
	edac_ctl->mod_name = dev_name(dev);
	edac_ctl->dev_name = dev_name(dev);
	edac_ctl->ctl_name = DRVNAME;
	/* Poll fairly slowly; this is a software source, not real hardware. */
	edac_ctl->poll_msec = 1000;
	dev_set_drvdata(dev, edac_ctl);

	rc = edac_device_add_device(edac_ctl);
	if (rc)
		goto out_dev;

	priv->debugfs_dir = debugfs_create_dir(DRVNAME, NULL);
	debugfs_create_file_unsafe("inject_ce", 0200, priv->debugfs_dir,
				    priv, &inject_ce_fops);
	debugfs_create_file_unsafe("inject_ue", 0200, priv->debugfs_dir,
				    priv, &inject_ue_fops);

	return 0;

out_dev:
	edac_device_free_ctl_info(edac_ctl);

	return rc;
}

static void dummy_edac_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_ctl = dev_get_drvdata(&pdev->dev);
	struct dummy_edac_priv *priv = edac_ctl->pvt_info;

	debugfs_remove_recursive(priv->debugfs_dir);
	edac_device_del_device(edac_ctl->dev);
	edac_device_free_ctl_info(edac_ctl);
}

static struct platform_driver dummy_edac_driver = {
	.probe = dummy_edac_probe,
	.remove = dummy_edac_remove,
	.driver = {
		.name = DRVNAME,
	},
};

/*
 * No devicetree/ACPI match table, and no scan of CPU nodes for a "compatible"
 * + enable property: simply register a platform device unconditionally so
 * probe() always runs. There is nothing here that depends on the underlying
 * platform actually exposing the corresponding hardware, so it loads fine
 * under QEMU/KVM, containers-with-a-kernel, or any other guest environment.
 */
static struct platform_device *dummy_pdev;

static int __init dummy_edac_driver_init(void)
{
	int rc;

	dummy_pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
	if (IS_ERR(dummy_pdev)) {
		pr_err(DRVNAME ": failed to register dummy platform device\n");
		return PTR_ERR(dummy_pdev);
	}

	rc = platform_driver_register(&dummy_edac_driver);
	if (rc) {
		platform_device_unregister(dummy_pdev);
		return rc;
	}

	pr_info("Loading %s\n", DRVNAME);

	return 0;
}

static void __exit dummy_edac_driver_exit(void)
{
	platform_driver_unregister(&dummy_edac_driver);
	platform_device_unregister(dummy_pdev);

	pr_info("Removing %s\n", DRVNAME);
}

module_init(dummy_edac_driver_init);
module_exit(dummy_edac_driver_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Dummy EDAC driver for testing without real ECC hardware");
