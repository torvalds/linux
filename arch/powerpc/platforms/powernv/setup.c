/*
 * PowerNV setup code.
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/bug.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/xics.h>

#include "powernv.h"

static void __init pnv_setup_arch(void)
{
	/* Force console to hvc for now until we have sorted out the
	 * real console situation for the platform. This will make
	 * hvc_udbg work at least.
	 */
	add_preferred_console("hvc", 0, NULL);

	/* Initialize SMP */
	pnv_smp_init();

	/* XXX PCI */

	/* XXX NVRAM */

	/* Enable NAP mode */
	powersave_nap = 1;

	/* XXX PMCS */
}

static void __init pnv_init_early(void)
{
	/* XXX IOMMU */
}

static void __init pnv_init_IRQ(void)
{
	xics_init();

	WARN_ON(!ppc_md.get_irq);
}

static void pnv_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: PowerNV %s\n", model);
	if (firmware_has_feature(FW_FEATURE_OPALv2))
		seq_printf(m, "firmware\t: OPAL v2\n");
	else if (firmware_has_feature(FW_FEATURE_OPAL))
		seq_printf(m, "firmware\t: OPAL v1\n");
	else
		seq_printf(m, "firmware\t: BML\n");
	of_node_put(root);
}

static void pnv_restart(char *cmd)
{
	for (;;);
}

static void pnv_power_off(void)
{
	for (;;);
}

static void pnv_halt(void)
{
	for (;;);
}

static unsigned long __init pnv_get_boot_time(void)
{
	return 0;
}

static void pnv_get_rtc_time(struct rtc_time *rtc_tm)
{
}

static int pnv_set_rtc_time(struct rtc_time *tm)
{
	return 0;
}

static void pnv_progress(char *s, unsigned short hex)
{
}

#ifdef CONFIG_KEXEC
static void pnv_kexec_cpu_down(int crash_shutdown, int secondary)
{
	xics_kexec_teardown_cpu(secondary);
}
#endif /* CONFIG_KEXEC */

static int __init pnv_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "ibm,powernv"))
		return 0;

	hpte_init_native();

	pr_debug("PowerNV detected !\n");

	return 1;
}

define_machine(powernv) {
	.name			= "PowerNV",
	.probe			= pnv_probe,
	.setup_arch		= pnv_setup_arch,
	.init_early		= pnv_init_early,
	.init_IRQ		= pnv_init_IRQ,
	.show_cpuinfo		= pnv_show_cpuinfo,
	.restart		= pnv_restart,
	.power_off		= pnv_power_off,
	.halt			= pnv_halt,
	.get_boot_time		= pnv_get_boot_time,
	.get_rtc_time		= pnv_get_rtc_time,
	.set_rtc_time		= pnv_set_rtc_time,
	.progress		= pnv_progress,
	.power_save             = power7_idle,
	.calibrate_decr		= generic_calibrate_decr,
#ifdef CONFIG_KEXEC
	.kexec_cpu_down		= pnv_kexec_cpu_down,
#endif
};
