/*
 * MPC8610 HPCD board specific routines
 *
 * Initial author: Xianghua Xiao <x.xiao@freescale.com>
 * Recode: Jason Jin <jason.jin@freescale.com>
 *
 * Rewrite the interrupt routing. remove the 8259PIC support,
 * All the integrated device in ULI use sideband interrupt.
 *
 * Copyright 2007 Freescale Semiconductor Inc.
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
#include <linux/of.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpc86xx.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>

#include <asm/mpic.h>

#include <sysdev/fsl_pci.h>
#include <sysdev/fsl_soc.h>

void __init
mpc86xx_hpcd_init_irq(void)
{
	struct mpic *mpic1;
	struct device_node *np;
	struct resource res;

	/* Determine PIC address. */
	np = of_find_node_by_type(NULL, "open-pic");
	if (np == NULL)
		return;
	of_address_to_resource(np, 0, &res);

	/* Alloc mpic structure and per isu has 16 INT entries. */
	mpic1 = mpic_alloc(np, res.start,
			MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			0, 256, " MPIC     ");
	BUG_ON(mpic1 == NULL);

	mpic_init(mpic1);
}

#ifdef CONFIG_PCI
static void __devinit quirk_uli1575(struct pci_dev *dev)
{
	u32 temp32;

	/* Disable INTx */
	pci_read_config_dword(dev, 0x48, &temp32);
	pci_write_config_dword(dev, 0x48, (temp32 | 1<<26));

	/* Enable sideband interrupt */
	pci_read_config_dword(dev, 0x90, &temp32);
	pci_write_config_dword(dev, 0x90, (temp32 | 1<<22));
}

static void __devinit quirk_uli5288(struct pci_dev *dev)
{
	unsigned char c;
	unsigned short temp;

	/* Interrupt Disable, Needed when SATA disabled */
	pci_read_config_word(dev, PCI_COMMAND, &temp);
	temp |= 1<<10;
	pci_write_config_word(dev, PCI_COMMAND, temp);

	pci_read_config_byte(dev, 0x83, &c);
	c |= 0x80;
	pci_write_config_byte(dev, 0x83, c);

	pci_write_config_byte(dev, PCI_CLASS_PROG, 0x01);
	pci_write_config_byte(dev, PCI_CLASS_DEVICE, 0x06);

	pci_read_config_byte(dev, 0x83, &c);
	c &= 0x7f;
	pci_write_config_byte(dev, 0x83, c);
}

/*
 * Since 8259PIC was disabled on the board, the IDE device can not
 * use the legacy IRQ, we need to let the IDE device work under
 * native mode and use the interrupt line like other PCI devices.
 * IRQ14 is a sideband interrupt from IDE device to CPU and we use this
 * as the interrupt for IDE device.
 */
static void __devinit quirk_uli5229(struct pci_dev *dev)
{
	unsigned char c;

	pci_read_config_byte(dev, 0x4b, &c);
	c |= 0x10;
	pci_write_config_byte(dev, 0x4b, c);
}

/*
 * SATA interrupt pin bug fix
 * There's a chip bug for 5288, The interrupt pin should be 2,
 * not the read only value 1, So it use INTB#, not INTA# which
 * actually used by the IDE device 5229.
 * As of this bug, during the PCI initialization, 5288 read the
 * irq of IDE device from the device tree, this function fix this
 * bug by re-assigning a correct irq to 5288.
 *
 */
static void __devinit final_uli5288(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct device_node *hosenode = hose ? hose->arch_data : NULL;
	struct of_irq oirq;
	int virq, pin = 2;
	u32 laddr[3];

	if (!hosenode)
		return;

	laddr[0] = (hose->first_busno << 16) | (PCI_DEVFN(31, 0) << 8);
	laddr[1] = laddr[2] = 0;
	of_irq_map_raw(hosenode, &pin, 1, laddr, &oirq);
	virq = irq_create_of_mapping(oirq.controller, oirq.specifier,
				     oirq.size);
	dev->irq = virq;
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x1575, quirk_uli1575);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x5288, quirk_uli5288);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x5229, quirk_uli5229);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_AL, 0x5288, final_uli5288);
#endif /* CONFIG_PCI */

static void __init
mpc86xx_hpcd_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif
	if (ppc_md.progress)
		ppc_md.progress("mpc86xx_hpcd_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_node_by_type(np, "pci") {
		if (of_device_is_compatible(np, "fsl,mpc8610-pci")
		    || of_device_is_compatible(np, "fsl,mpc8641-pcie")) {
			struct resource rsrc;
			of_address_to_resource(np, 0, &rsrc);
			if ((rsrc.start & 0xfffff) == 0xa000)
				fsl_add_bridge(np, 1);
			else
				fsl_add_bridge(np, 0);
		}
        }
#endif

	printk("MPC86xx HPCD board from Freescale Semiconductor\n");
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc86xx_hpcd_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "fsl,MPC8610HPCD"))
		return 1;	/* Looks good */

	return 0;
}

long __init
mpc86xx_time_init(void)
{
	unsigned int temp;

	/* Set the time base to zero */
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, 0);

	temp = mfspr(SPRN_HID0);
	temp |= HID0_TBEN;
	mtspr(SPRN_HID0, temp);
	asm volatile("isync");

	return 0;
}

define_machine(mpc86xx_hpcd) {
	.name			= "MPC86xx HPCD",
	.probe			= mpc86xx_hpcd_probe,
	.setup_arch		= mpc86xx_hpcd_setup_arch,
	.init_IRQ		= mpc86xx_hpcd_init_irq,
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.time_init		= mpc86xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
};
