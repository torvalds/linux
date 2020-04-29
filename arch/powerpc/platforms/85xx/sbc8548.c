// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Wind River SBC8548 setup and early boot code.
 *
 * Copyright 2007 Wind River Systems Inc.
 *
 * By Paul Gortmaker (see MAINTAINERS for contact information)
 *
 * Based largely on the MPC8548CDS support - Copyright 2005 Freescale Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/initrd.h>
#include <linux/interrupt.h>
#include <linux/fsl_devices.h>
#include <linux/of_platform.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/pci-bridge.h>
#include <asm/irq.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#include "mpc85xx.h"

static int sbc_rev;

static void __init sbc8548_pic_init(void)
{
	struct mpic *mpic = mpic_alloc(NULL, 0, MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	mpic_init(mpic);
}

/* Extract the HW Rev from the EPLD on the board */
static int __init sbc8548_hw_rev(void)
{
	struct device_node *np;
	struct resource res;
	unsigned int *rev;
	int board_rev = 0;

	np = of_find_compatible_node(NULL, NULL, "hw-rev");
	if (np == NULL) {
		printk("No HW-REV found in DTB.\n");
		return -ENODEV;
	}

	of_address_to_resource(np, 0, &res);
	of_node_put(np);

	rev = ioremap(res.start,sizeof(unsigned int));
	board_rev = (*rev) >> 28;
	iounmap(rev);

	return board_rev;
}

/*
 * Setup the architecture
 */
static void __init sbc8548_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("sbc8548_setup_arch()", 0);

	fsl_pci_assign_primary();

	sbc_rev = sbc8548_hw_rev();
}

static void sbc8548_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Wind River\n");
	seq_printf(m, "Machine\t\t: SBC8548 v%d\n", sbc_rev);
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));
}

machine_arch_initcall(sbc8548, mpc85xx_common_publish_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init sbc8548_probe(void)
{
	return of_machine_is_compatible("SBC8548");
}

define_machine(sbc8548) {
	.name		= "SBC8548",
	.probe		= sbc8548_probe,
	.setup_arch	= sbc8548_setup_arch,
	.init_IRQ	= sbc8548_pic_init,
	.show_cpuinfo	= sbc8548_show_cpuinfo,
	.get_irq	= mpic_get_irq,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
	.pcibios_fixup_phb      = fsl_pcibios_fixup_phb,
#endif
	.calibrate_decr = generic_calibrate_decr,
	.progress	= udbg_progress,
};
