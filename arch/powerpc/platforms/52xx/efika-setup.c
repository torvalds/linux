/*
 *
 * Efika 5K2 platform setup
 * Some code really inspired from the lite5200b platform.
 * 
 * Copyright (C) 2006 bplan GmbH
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/utsrelease.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>
#include <linux/timer.h>
#include <linux/pci.h>

#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/of_device.h>
#include <asm/of_platform.h>
#include <asm/mpc52xx.h>

#include "efika.h"

static void efika_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *revision = NULL;
	const char *codegendescription = NULL;
	const char *codegenvendor = NULL;

	root = of_find_node_by_path("/");
	if (root) {
		revision = get_property(root, "revision", NULL);
		codegendescription =
		    get_property(root, "CODEGEN,description", NULL);
		codegenvendor = get_property(root, "CODEGEN,vendor", NULL);

		of_node_put(root);
	}

	if (codegendescription)
		seq_printf(m, "machine\t\t: %s\n", codegendescription);
	else
		seq_printf(m, "machine\t\t: Efika\n");

	if (revision)
		seq_printf(m, "revision\t: %s\n", revision);

	if (codegenvendor)
		seq_printf(m, "vendor\t\t: %s\n", codegenvendor);

	of_node_put(root);
}

static void __init efika_setup_arch(void)
{
	rtas_initialize();

#ifdef CONFIG_BLK_DEV_INITRD
	initrd_below_start_ok = 1;

	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
		ROOT_DEV = Root_SDA2;	/* sda2 (sda1 is for the kernel) */

	efika_pcisetup();

	if (ppc_md.progress)
		ppc_md.progress("Linux/PPC " UTS_RELEASE " runnung on Efika ;-)\n", 0x0);
}

static void __init efika_init(void)
{
	struct device_node *np;
	struct device_node *cnp = NULL;
	const u32 *base;

	/* Find every child of the SOC node and add it to of_platform */
	np = of_find_node_by_name(NULL, "builtin");
	if (np) {
		char name[BUS_ID_SIZE];
		while ((cnp = of_get_next_child(np, cnp))) {
			strcpy(name, cnp->name);

			base = get_property(cnp, "reg", NULL);
			if (base == NULL)
				continue;

			snprintf(name+strlen(name), BUS_ID_SIZE, "@%x", *base);
			of_platform_device_create(cnp, name, NULL);

			printk(KERN_INFO EFIKA_PLATFORM_NAME" : Added %s (type '%s' at '%s') to the known devices\n", name, cnp->type, cnp->full_name);
		}
	}

	if (ppc_md.progress)
		ppc_md.progress("  Have fun with your Efika!    ", 0x7777);
}

static int __init efika_probe(void)
{
	char *model = of_get_flat_dt_prop(of_get_flat_dt_root(),
					  "model", NULL);

	if (model == NULL)
		return 0;
	if (strcmp(model, "EFIKA5K2"))
		return 0;

	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	return 1;
}

define_machine(efika)
{
	.name = EFIKA_PLATFORM_NAME,
	.probe = efika_probe,
	.setup_arch = efika_setup_arch,
	.init = efika_init,
	.show_cpuinfo = efika_show_cpuinfo,
	.init_IRQ = mpc52xx_init_irq,
	.get_irq = mpc52xx_get_irq,
	.restart = rtas_restart,
	.power_off = rtas_power_off,
	.halt = rtas_halt,
	.set_rtc_time = rtas_set_rtc_time,
	.get_rtc_time = rtas_get_rtc_time,
	.progress = rtas_progress,
	.get_boot_time = rtas_get_boot_time,
	.calibrate_decr = generic_calibrate_decr,
	.phys_mem_access_prot = pci_phys_mem_access_prot,
};
