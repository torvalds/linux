/*
 * Celleb setup code
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
 *
 * This code is based on arch/powerpc/platforms/cell/setup.c:
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified by PPC64 Team, IBM Corp
 *  Modified by Cell Team, IBM Deutschland Entwicklung GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#undef DEBUG

#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/console.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/kexec.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/irq.h>
#include <asm/time.h>
#include <asm/spu_priv1.h>
#include <asm/firmware.h>
#include <asm/of_platform.h>
#include <asm/rtas.h>
#include <asm/cell-regs.h>

#include "interrupt.h"
#include "beat_wrapper.h"
#include "beat.h"
#include "pci.h"
#include "../cell/interrupt.h"
#include "../cell/pervasive.h"
#include "../cell/ras.h"

static char celleb_machine_type[128] = "Celleb";

static void celleb_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	/* using "CHRP" is to trick anaconda into installing FCx into Celleb */
	seq_printf(m, "machine\t\t: %s %s\n", celleb_machine_type, model);
	of_node_put(root);
}

static int __init celleb_machine_type_hack(char *ptr)
{
	strncpy(celleb_machine_type, ptr, sizeof(celleb_machine_type));
	celleb_machine_type[sizeof(celleb_machine_type)-1] = 0;
	return 0;
}

__setup("celleb_machine_type_hack=", celleb_machine_type_hack);

static void celleb_progress(char *s, unsigned short hex)
{
	printk("*** %04x : %s\n", hex, s ? s : "");
}

static void __init celleb_init_IRQ_native(void)
{
	iic_init_IRQ();
	spider_init_IRQ();
}

static void __init celleb_setup_arch_beat(void)
{
	ppc_md.restart		= beat_restart;
	ppc_md.power_off	= beat_power_off;
	ppc_md.halt		= beat_halt;
	ppc_md.get_rtc_time	= beat_get_rtc_time;
	ppc_md.set_rtc_time	= beat_set_rtc_time;
	ppc_md.power_save	= beat_power_save;
	ppc_md.nvram_size	= beat_nvram_get_size;
	ppc_md.nvram_read	= beat_nvram_read;
	ppc_md.nvram_write	= beat_nvram_write;
	ppc_md.set_dabr		= beat_set_xdabr;
	ppc_md.init_IRQ		= beatic_init_IRQ;
	ppc_md.get_irq		= beatic_get_irq;
#ifdef CONFIG_KEXEC
	ppc_md.kexec_cpu_down	= beat_kexec_cpu_down;
#endif

#ifdef CONFIG_SPU_BASE
	spu_priv1_ops		= &spu_priv1_beat_ops;
	spu_management_ops	= &spu_management_of_ops;
#endif

#ifdef CONFIG_SMP
	smp_init_celleb();
#endif
}

static void __init celleb_setup_arch_native(void)
{
	ppc_md.restart		= rtas_restart;
	ppc_md.power_off	= rtas_power_off;
	ppc_md.halt		= rtas_halt;
	ppc_md.get_boot_time	= rtas_get_boot_time;
	ppc_md.get_rtc_time	= rtas_get_rtc_time;
	ppc_md.set_rtc_time	= rtas_set_rtc_time;
	ppc_md.init_IRQ		= celleb_init_IRQ_native;

#ifdef CONFIG_SPU_BASE
	spu_priv1_ops		= &spu_priv1_mmio_ops;
	spu_management_ops	= &spu_management_of_ops;
#endif

	cbe_regs_init();

#ifdef CONFIG_CBE_RAS
	cbe_ras_init();
#endif

#ifdef CONFIG_SMP
	smp_init_cell();
#endif

	cbe_pervasive_init();
}

static void __init celleb_setup_arch(void)
{
	if (firmware_has_feature(FW_FEATURE_BEAT))
		celleb_setup_arch_beat();
	else
		celleb_setup_arch_native();

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif
}

static int __init celleb_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "Beat")) {
		powerpc_firmware_features |= FW_FEATURE_CELLEB_ALWAYS
			| FW_FEATURE_BEAT | FW_FEATURE_LPAR;
		hpte_init_beat_v3();
		return 1;
	}
	if (of_flat_dt_is_compatible(root, "TOSHIBA,Celleb")) {
		powerpc_firmware_features |= FW_FEATURE_CELLEB_ALWAYS;
		hpte_init_native();
		return 1;
	}

	return 0;
}

static struct of_device_id celleb_bus_ids[] __initdata = {
	{ .type = "scc", },
	{ .type = "ioif", },	/* old style */
	{},
};

static int __init celleb_publish_devices(void)
{
	if (!machine_is(celleb))
		return 0;

	/* Publish OF platform devices for southbridge IOs */
	of_platform_bus_probe(NULL, celleb_bus_ids, NULL);

	celleb_pci_workaround_init();

	return 0;
}
device_initcall(celleb_publish_devices);

define_machine(celleb) {
	.name			= "Cell Reference Set",
	.probe			= celleb_probe,
	.setup_arch		= celleb_setup_arch,
	.show_cpuinfo		= celleb_show_cpuinfo,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= celleb_progress,
	.pci_probe_mode 	= celleb_pci_probe_mode,
	.pci_setup_phb		= celleb_setup_phb,
#ifdef CONFIG_KEXEC
	.machine_kexec		= default_machine_kexec,
	.machine_kexec_prepare	= default_machine_kexec_prepare,
	.machine_crash_shutdown	= default_machine_crash_shutdown,
#endif
};
