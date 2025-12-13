// SPDX-License-Identifier: GPL-2.0
/*
 * Cortex A72 EDAC L1 and L2 cache error detection
 *
 * Copyright (c) 2020 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
 * Copyright (c) 2025 Microsoft Corporation, <vijayb@linux.microsoft.com>
 *
 * Based on Code from:
 * Copyright (c) 2018, NXP Semiconductor
 * Author: York Sun <york.sun@nxp.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/bitfield.h>
#include <asm/smp_plat.h>

#include "edac_module.h"

#define DRVNAME			"a72-edac"

#define SYS_CPUMERRSR_EL1	sys_reg(3, 1, 15, 2, 2)
#define SYS_L2MERRSR_EL1	sys_reg(3, 1, 15, 2, 3)

#define CPUMERRSR_EL1_RAMID	GENMASK(30, 24)
#define L2MERRSR_EL1_CPUID_WAY	GENMASK(21, 18)

#define CPUMERRSR_EL1_VALID	BIT(31)
#define CPUMERRSR_EL1_FATAL	BIT(63)
#define L2MERRSR_EL1_VALID	BIT(31)
#define L2MERRSR_EL1_FATAL	BIT(63)

#define L1_I_TAG_RAM		0x00
#define L1_I_DATA_RAM		0x01
#define L1_D_TAG_RAM		0x08
#define L1_D_DATA_RAM		0x09
#define TLB_RAM			0x18

#define MESSAGE_SIZE		64

struct mem_err_synd_reg {
	u64 cpu_mesr;
	u64 l2_mesr;
};

static struct cpumask compat_mask;

static void report_errors(struct edac_device_ctl_info *edac_ctl, int cpu,
			  struct mem_err_synd_reg *mesr)
{
	u64 cpu_mesr = mesr->cpu_mesr;
	u64 l2_mesr = mesr->l2_mesr;
	char msg[MESSAGE_SIZE];

	if (cpu_mesr & CPUMERRSR_EL1_VALID) {
		const char *str;
		bool fatal = cpu_mesr & CPUMERRSR_EL1_FATAL;

		switch (FIELD_GET(CPUMERRSR_EL1_RAMID, cpu_mesr)) {
		case L1_I_TAG_RAM:
			str = "L1-I Tag RAM";
			break;
		case L1_I_DATA_RAM:
			str = "L1-I Data RAM";
			break;
		case L1_D_TAG_RAM:
			str = "L1-D Tag RAM";
			break;
		case L1_D_DATA_RAM:
			str = "L1-D Data RAM";
			break;
		case TLB_RAM:
			str = "TLB RAM";
			break;
		default:
			str = "Unspecified";
			break;
		}

		snprintf(msg, MESSAGE_SIZE, "%s %s error(s) on CPU %d",
			 str, fatal ? "fatal" : "correctable", cpu);

		if (fatal)
			edac_device_handle_ue(edac_ctl, cpu, 0, msg);
		else
			edac_device_handle_ce(edac_ctl, cpu, 0, msg);
	}

	if (l2_mesr & L2MERRSR_EL1_VALID) {
		bool fatal = l2_mesr & L2MERRSR_EL1_FATAL;

		snprintf(msg, MESSAGE_SIZE, "L2 %s error(s) on CPU %d CPUID/WAY 0x%lx",
			 fatal ? "fatal" : "correctable", cpu,
			 FIELD_GET(L2MERRSR_EL1_CPUID_WAY, l2_mesr));
		if (fatal)
			edac_device_handle_ue(edac_ctl, cpu, 1, msg);
		else
			edac_device_handle_ce(edac_ctl, cpu, 1, msg);
	}
}

static void read_errors(void *data)
{
	struct mem_err_synd_reg *mesr = data;

	mesr->cpu_mesr = read_sysreg_s(SYS_CPUMERRSR_EL1);
	if (mesr->cpu_mesr & CPUMERRSR_EL1_VALID) {
		write_sysreg_s(0, SYS_CPUMERRSR_EL1);
		isb();
	}
	mesr->l2_mesr = read_sysreg_s(SYS_L2MERRSR_EL1);
	if (mesr->l2_mesr & L2MERRSR_EL1_VALID) {
		write_sysreg_s(0, SYS_L2MERRSR_EL1);
		isb();
	}
}

static void a72_edac_check(struct edac_device_ctl_info *edac_ctl)
{
	struct mem_err_synd_reg mesr;
	int cpu;

	cpus_read_lock();
	for_each_cpu_and(cpu, cpu_online_mask, &compat_mask) {
		smp_call_function_single(cpu, read_errors, &mesr, true);
		report_errors(edac_ctl, cpu, &mesr);
	}
	cpus_read_unlock();
}

static int a72_edac_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_ctl;
	struct device *dev = &pdev->dev;
	int rc;

	edac_ctl = edac_device_alloc_ctl_info(0, "cpu",
					      num_possible_cpus(), "L", 2, 1,
					      edac_device_alloc_index());
	if (!edac_ctl)
		return -ENOMEM;

	edac_ctl->edac_check = a72_edac_check;
	edac_ctl->dev = dev;
	edac_ctl->mod_name = dev_name(dev);
	edac_ctl->dev_name = dev_name(dev);
	edac_ctl->ctl_name = DRVNAME;
	dev_set_drvdata(dev, edac_ctl);

	rc = edac_device_add_device(edac_ctl);
	if (rc)
		goto out_dev;

	return 0;

out_dev:
	edac_device_free_ctl_info(edac_ctl);

	return rc;
}

static void a72_edac_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_ctl = dev_get_drvdata(&pdev->dev);

	edac_device_del_device(edac_ctl->dev);
	edac_device_free_ctl_info(edac_ctl);
}

static const struct of_device_id cortex_arm64_edac_of_match[] = {
	{ .compatible = "arm,cortex-a72" },
	{}
};
MODULE_DEVICE_TABLE(of, cortex_arm64_edac_of_match);

static struct platform_driver a72_edac_driver = {
	.probe = a72_edac_probe,
	.remove = a72_edac_remove,
	.driver = {
		.name = DRVNAME,
	},
};

static struct platform_device *a72_pdev;

static int __init a72_edac_driver_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct device_node *np __free(device_node) = of_cpu_device_node_get(cpu);
		if (np) {
			if (of_match_node(cortex_arm64_edac_of_match, np) &&
			    of_property_read_bool(np, "edac-enabled")) {
				cpumask_set_cpu(cpu, &compat_mask);
			}
		} else {
			pr_warn("failed to find device node for CPU %d\n", cpu);
		}
	}

	if (cpumask_empty(&compat_mask))
		return 0;

	a72_pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
	if (IS_ERR(a72_pdev)) {
		pr_err("failed to register A72 EDAC device\n");
		return PTR_ERR(a72_pdev);
	}

	return platform_driver_register(&a72_edac_driver);
}

static void __exit a72_edac_driver_exit(void)
{
	platform_device_unregister(a72_pdev);
	platform_driver_unregister(&a72_edac_driver);
}

module_init(a72_edac_driver_init);
module_exit(a72_edac_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("Cortex A72 L1 and L2 cache EDAC driver");
