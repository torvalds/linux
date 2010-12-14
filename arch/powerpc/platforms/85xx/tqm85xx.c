/*
 * Based on MPC8560 ADS and arch/ppc tqm85xx ports
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * Copyright 2008 Freescale Semiconductor Inc.
 *
 * Copyright (c) 2005-2006 DENX Software Engineering
 * Stefan Roese <sr@denx.de>
 *
 * Based on original work by
 * 	Kumar Gala <kumar.gala@freescale.com>
 *      Copyright 2004 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/of_platform.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpic.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

#ifdef CONFIG_CPM2
#include <asm/cpm2.h>
#include <sysdev/cpm2_pic.h>

static void cpm2_cascade(unsigned int irq, struct irq_desc *desc)
{
	int cascade_irq;

	while ((cascade_irq = cpm2_get_irq()) >= 0)
		generic_handle_irq(cascade_irq);

	desc->chip->eoi(irq);
}
#endif /* CONFIG_CPM2 */

static void __init tqm85xx_pic_init(void)
{
	struct mpic *mpic;
	struct resource r;
	struct device_node *np;
#ifdef CONFIG_CPM2
	int irq;
#endif

	np = of_find_node_by_type(NULL, "open-pic");
	if (!np) {
		printk(KERN_ERR "Could not find open-pic node\n");
		return;
	}

	if (of_address_to_resource(np, 0, &r)) {
		printk(KERN_ERR "Could not map mpic register space\n");
		of_node_put(np);
		return;
	}

	mpic = mpic_alloc(np, r.start,
			MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	of_node_put(np);

	mpic_init(mpic);

#ifdef CONFIG_CPM2
	/* Setup CPM2 PIC */
	np = of_find_compatible_node(NULL, NULL, "fsl,cpm2-pic");
	if (np == NULL) {
		printk(KERN_ERR "PIC init: can not find fsl,cpm2-pic node\n");
		return;
	}
	irq = irq_of_parse_and_map(np, 0);

	if (irq == NO_IRQ) {
		of_node_put(np);
		printk(KERN_ERR "PIC init: got no IRQ for cpm cascade\n");
		return;
	}

	cpm2_pic_init(np);
	of_node_put(np);
	set_irq_chained_handler(irq, cpm2_cascade);
#endif
}

/*
 * Setup the architecture
 */
static void __init tqm85xx_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("tqm85xx_setup_arch()", 0);

#ifdef CONFIG_CPM2
	cpm2_reset();
#endif

#ifdef CONFIG_PCI
	for_each_node_by_type(np, "pci") {
		if (of_device_is_compatible(np, "fsl,mpc8540-pci") ||
		    of_device_is_compatible(np, "fsl,mpc8548-pcie")) {
			struct resource rsrc;
			if (!of_address_to_resource(np, 0, &rsrc)) {
				if ((rsrc.start & 0xfffff) == 0x8000)
					fsl_add_bridge(np, 1);
				else
					fsl_add_bridge(np, 0);
			}
		}
	}
#endif
}

static void tqm85xx_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: TQ Components\n");
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));
}

static void __init tqm85xx_ti1520_fixup(struct pci_dev *pdev)
{
	unsigned int val;

	/* Do not do the fixup on other platforms! */
	if (!machine_is(tqm85xx))
		return;

	dev_info(&pdev->dev, "Using TI 1520 fixup on TQM85xx\n");

	/*
	 * Enable P2CCLK bit in system control register
	 * to enable CLOCK output to power chip
	 */
	pci_read_config_dword(pdev, 0x80, &val);
	pci_write_config_dword(pdev, 0x80, val | (1 << 27));

}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_1520,
		tqm85xx_ti1520_fixup);

static struct of_device_id __initdata of_bus_ids[] = {
	{ .compatible = "simple-bus", },
	{ .compatible = "gianfar", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);

	return 0;
}
machine_device_initcall(tqm85xx, declare_of_platform_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init tqm85xx_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if ((of_flat_dt_is_compatible(root, "tqc,tqm8540")) ||
	    (of_flat_dt_is_compatible(root, "tqc,tqm8541")) ||
	    (of_flat_dt_is_compatible(root, "tqc,tqm8548")) ||
	    (of_flat_dt_is_compatible(root, "tqc,tqm8555")) ||
	    (of_flat_dt_is_compatible(root, "tqc,tqm8560")))
		return 1;

	return 0;
}

define_machine(tqm85xx) {
	.name			= "TQM85xx",
	.probe			= tqm85xx_probe,
	.setup_arch		= tqm85xx_setup_arch,
	.init_IRQ		= tqm85xx_pic_init,
	.show_cpuinfo		= tqm85xx_show_cpuinfo,
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
