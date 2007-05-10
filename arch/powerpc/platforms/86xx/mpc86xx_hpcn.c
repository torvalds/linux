/*
 * MPC86xx HPCN board specific routines
 *
 * Recode: ZHANG WEI <wei.zhang@freescale.com>
 * Initial author: Xianghua Xiao <x.xiao@freescale.com>
 *
 * Copyright 2006 Freescale Semiconductor Inc.
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

#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mpc86xx.h>
#include <asm/prom.h>
#include <mm/mmu_decl.h>
#include <asm/udbg.h>
#include <asm/i8259.h>

#include <asm/mpic.h>

#include <sysdev/fsl_soc.h>

#include "mpc86xx.h"
#include "mpc8641_hpcn.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(fmt...) do { printk(KERN_ERR fmt); } while(0)
#else
#define DBG(fmt...) do { } while(0)
#endif

#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
unsigned long pci_dram_offset = 0;
#endif


#ifdef CONFIG_PCI
static void mpc86xx_8259_cascade(unsigned int irq, struct irq_desc *desc)
{
	unsigned int cascade_irq = i8259_irq();
	if (cascade_irq != NO_IRQ)
		generic_handle_irq(cascade_irq);
	desc->chip->eoi(irq);
}
#endif	/* CONFIG_PCI */

void __init
mpc86xx_hpcn_init_irq(void)
{
	struct mpic *mpic1;
	struct device_node *np;
	struct resource res;
#ifdef CONFIG_PCI
	struct device_node *cascade_node = NULL;
	int cascade_irq;
#endif

	/* Determine PIC address. */
	np = of_find_node_by_type(NULL, "open-pic");
	if (np == NULL)
		return;
	of_address_to_resource(np, 0, &res);

	/* Alloc mpic structure and per isu has 16 INT entries. */
	mpic1 = mpic_alloc(np, res.start,
			MPIC_PRIMARY | MPIC_WANTS_RESET | MPIC_BIG_ENDIAN,
			16, NR_IRQS - 4,
			" MPIC     ");
	BUG_ON(mpic1 == NULL);

	mpic_assign_isu(mpic1, 0, res.start + 0x10000);

	/* 48 Internal Interrupts */
	mpic_assign_isu(mpic1, 1, res.start + 0x10200);
	mpic_assign_isu(mpic1, 2, res.start + 0x10400);
	mpic_assign_isu(mpic1, 3, res.start + 0x10600);

	/* 16 External interrupts
	 * Moving them from [0 - 15] to [64 - 79]
	 */
	mpic_assign_isu(mpic1, 4, res.start + 0x10000);

	mpic_init(mpic1);

#ifdef CONFIG_PCI
	/* Initialize i8259 controller */
	for_each_node_by_type(np, "interrupt-controller")
		if (of_device_is_compatible(np, "chrp,iic")) {
			cascade_node = np;
			break;
		}
	if (cascade_node == NULL) {
		printk(KERN_DEBUG "mpc86xxhpcn: no ISA interrupt controller\n");
		return;
	}

	cascade_irq = irq_of_parse_and_map(cascade_node, 0);
	if (cascade_irq == NO_IRQ) {
		printk(KERN_ERR "mpc86xxhpcn: failed to map cascade interrupt");
		return;
	}
	DBG("mpc86xxhpcn: cascade mapped to irq %d\n", cascade_irq);

	i8259_init(cascade_node, 0);
	of_node_put(cascade_node);

	set_irq_chained_handler(cascade_irq, mpc86xx_8259_cascade);
#endif
}

#ifdef CONFIG_PCI

enum pirq{PIRQA = 8, PIRQB, PIRQC, PIRQD, PIRQE, PIRQF, PIRQG, PIRQH};
const unsigned char uli1575_irq_route_table[16] = {
	0, 	/* 0: Reserved */
	0x8, 	/* 1: 0b1000 */
	0, 	/* 2: Reserved */
	0x2,	/* 3: 0b0010 */
	0x4,	/* 4: 0b0100 */
	0x5, 	/* 5: 0b0101 */
	0x7,	/* 6: 0b0111 */
	0x6,	/* 7: 0b0110 */
	0, 	/* 8: Reserved */
	0x1,	/* 9: 0b0001 */
	0x3,	/* 10: 0b0011 */
	0x9,	/* 11: 0b1001 */
	0xb,	/* 12: 0b1011 */
	0, 	/* 13: Reserved */
	0xd,	/* 14, 0b1101 */
	0xf,	/* 15, 0b1111 */
};

static int __devinit
get_pci_irq_from_of(struct pci_controller *hose, int slot, int pin)
{
	struct of_irq oirq;
	u32 laddr[3];
	struct device_node *hosenode = hose ? hose->arch_data : NULL;

	if (!hosenode) return -EINVAL;

	laddr[0] = (hose->first_busno << 16) | (PCI_DEVFN(slot, 0) << 8);
	laddr[1] = laddr[2] = 0;
	of_irq_map_raw(hosenode, &pin, 1, laddr, &oirq);
	DBG("mpc86xx_hpcn: pci irq addr %x, slot %d, pin %d, irq %d\n",
			laddr[0], slot, pin, oirq.specifier[0]);
	return oirq.specifier[0];
}

static void __devinit quirk_uli1575(struct pci_dev *dev)
{
	unsigned short temp;
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	unsigned char irq2pin[16], c;
	unsigned long pirq_map_word = 0;
	u32 irq;
	int i;

	/*
	 * ULI1575 interrupts route setup
	 */
	memset(irq2pin, 0, 16); /* Initialize default value 0 */

	/*
	 * PIRQA -> PIRQD mapping read from OF-tree
	 *
	 * interrupts for PCI slot0 -- PIRQA / PIRQB / PIRQC / PIRQD
	 *                PCI slot1 -- PIRQB / PIRQC / PIRQD / PIRQA
	 */
	for (i = 0; i < 4; i++){
		irq = get_pci_irq_from_of(hose, 17, i + 1);
		if (irq > 0 && irq < 16)
			irq2pin[irq] = PIRQA + i;
		else
			printk(KERN_WARNING "ULI1575 device"
			    "(slot %d, pin %d) irq %d is invalid.\n",
			    17, i, irq);
	}

	/*
	 * PIRQE -> PIRQF mapping set manually
	 *
	 * IRQ pin   IRQ#
	 * PIRQE ---- 9
	 * PIRQF ---- 10
	 * PIRQG ---- 11
	 * PIRQH ---- 12
	 */
	for (i = 0; i < 4; i++) irq2pin[i + 9] = PIRQE + i;

	/* Set IRQ-PIRQ Mapping to ULI1575 */
	for (i = 0; i < 16; i++)
		if (irq2pin[i])
			pirq_map_word |= (uli1575_irq_route_table[i] & 0xf)
				<< ((irq2pin[i] - PIRQA) * 4);

	/* ULI1575 IRQ mapping conf register default value is 0xb9317542 */
	DBG("Setup ULI1575 IRQ mapping configuration register value = 0x%x\n",
			pirq_map_word);
	pci_write_config_dword(dev, 0x48, pirq_map_word);

#define ULI1575_SET_DEV_IRQ(slot, pin, reg) 				\
	do { 								\
		int irq; 						\
		irq = get_pci_irq_from_of(hose, slot, pin); 		\
		if (irq > 0 && irq < 16) 				\
			pci_write_config_byte(dev, reg, irq2pin[irq]); 	\
		else							\
			printk(KERN_WARNING "ULI1575 device"		\
			    "(slot %d, pin %d) irq %d is invalid.\n",	\
			    slot, pin, irq);				\
	} while(0)

	/* USB 1.1 OHCI controller 1, slot 28, pin 1 */
	ULI1575_SET_DEV_IRQ(28, 1, 0x86);

	/* USB 1.1 OHCI controller 2, slot 28, pin 2 */
	ULI1575_SET_DEV_IRQ(28, 2, 0x87);

	/* USB 1.1 OHCI controller 3, slot 28, pin 3 */
	ULI1575_SET_DEV_IRQ(28, 3, 0x88);

	/* USB 2.0 controller, slot 28, pin 4 */
	irq = get_pci_irq_from_of(hose, 28, 4);
	if (irq >= 0 && irq <=15)
		pci_write_config_dword(dev, 0x74, uli1575_irq_route_table[irq]);

	/* Audio controller, slot 29, pin 1 */
	ULI1575_SET_DEV_IRQ(29, 1, 0x8a);

	/* Modem controller, slot 29, pin 2 */
	ULI1575_SET_DEV_IRQ(29, 2, 0x8b);

	/* HD audio controller, slot 29, pin 3 */
	ULI1575_SET_DEV_IRQ(29, 3, 0x8c);

	/* SMB interrupt: slot 30, pin 1 */
	ULI1575_SET_DEV_IRQ(30, 1, 0x8e);

	/* PMU ACPI SCI interrupt: slot 30, pin 2 */
	ULI1575_SET_DEV_IRQ(30, 2, 0x8f);

	/* Serial ATA interrupt: slot 31, pin 1 */
	ULI1575_SET_DEV_IRQ(31, 1, 0x8d);

	/* Primary PATA IDE IRQ: 14
	 * Secondary PATA IDE IRQ: 15
	 */
	pci_write_config_byte(dev, 0x44, 0x30 | uli1575_irq_route_table[14]);
	pci_write_config_byte(dev, 0x75, uli1575_irq_route_table[15]);

	/* Set IRQ14 and IRQ15 to legacy IRQs */
	pci_read_config_word(dev, 0x46, &temp);
	temp |= 0xc000;
	pci_write_config_word(dev, 0x46, temp);

	/* Set i8259 interrupt trigger
	 * IRQ 3:  Level
	 * IRQ 4:  Level
	 * IRQ 5:  Level
	 * IRQ 6:  Level
	 * IRQ 7:  Level
	 * IRQ 9:  Level
	 * IRQ 10: Level
	 * IRQ 11: Level
	 * IRQ 12: Level
	 * IRQ 14: Edge
	 * IRQ 15: Edge
	 */
	outb(0xfa, 0x4d0);
	outb(0x1e, 0x4d1);

#undef ULI1575_SET_DEV_IRQ

	/* Disable the HD interface and enable the AC97 interface. */
	pci_read_config_byte(dev, 0xb8, &c);
	c &= 0x7f;
	pci_write_config_byte(dev, 0xb8, c);
}

static void __devinit quirk_uli5288(struct pci_dev *dev)
{
	unsigned char c;

	pci_read_config_byte(dev,0x83,&c);
	c |= 0x80;
	pci_write_config_byte(dev, 0x83, c);

	pci_write_config_byte(dev, 0x09, 0x01);
	pci_write_config_byte(dev, 0x0a, 0x06);

	pci_read_config_byte(dev,0x83,&c);
	c &= 0x7f;
	pci_write_config_byte(dev, 0x83, c);

	pci_read_config_byte(dev,0x84,&c);
	c |= 0x01;
	pci_write_config_byte(dev, 0x84, c);
}

static void __devinit quirk_uli5229(struct pci_dev *dev)
{
	unsigned short temp;
	pci_write_config_word(dev, 0x04, 0x0405);
	pci_read_config_word(dev, 0x4a, &temp);
	temp |= 0x1000;
	pci_write_config_word(dev, 0x4a, temp);
}

static void __devinit early_uli5249(struct pci_dev *dev)
{
	unsigned char temp;
	pci_write_config_word(dev, 0x04, 0x0007);
	pci_read_config_byte(dev, 0x7c, &temp);
	pci_write_config_byte(dev, 0x7c, 0x80);
	pci_write_config_byte(dev, 0x09, 0x01);
	pci_write_config_byte(dev, 0x7c, temp);
	dev->class |= 0x1;
}

DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x1575, quirk_uli1575);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x5288, quirk_uli5288);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, 0x5229, quirk_uli5229);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_AL, 0x5249, early_uli5249);
#endif /* CONFIG_PCI */


static void __init
mpc86xx_hpcn_setup_arch(void)
{
	struct device_node *np;

	if (ppc_md.progress)
		ppc_md.progress("mpc86xx_hpcn_setup_arch()", 0);

	np = of_find_node_by_type(NULL, "cpu");
	if (np != 0) {
		const unsigned int *fp;

		fp = of_get_property(np, "clock-frequency", NULL);
		if (fp != 0)
			loops_per_jiffy = *fp / HZ;
		else
			loops_per_jiffy = 50000000 / HZ;
		of_node_put(np);
	}

#ifdef CONFIG_PCI
	for (np = NULL; (np = of_find_node_by_type(np, "pci")) != NULL;)
		add_bridge(np);

	ppc_md.pci_exclude_device = mpc86xx_exclude_device;
#endif

	printk("MPC86xx HPCN board from Freescale Semiconductor\n");

#ifdef CONFIG_SMP
	mpc86xx_smp_init();
#endif
}


void
mpc86xx_hpcn_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	uint memsize = total_memory;
	const char *model = "";
	uint svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: Freescale Semiconductor\n");

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "Machine\t\t: %s\n", model);
	of_node_put(root);

	seq_printf(m, "SVR\t\t: 0x%x\n", svid);
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));
}


/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc86xx_hpcn_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (of_flat_dt_is_compatible(root, "mpc86xx"))
		return 1;	/* Looks good */

	return 0;
}


void
mpc86xx_restart(char *cmd)
{
	void __iomem *rstcr;

	rstcr = ioremap(get_immrbase() + MPC86XX_RSTCR_OFFSET, 0x100);

	local_irq_disable();

	/* Assert reset request to Reset Control Register */
	out_be32(rstcr, 0x2);

	/* not reached */
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


define_machine(mpc86xx_hpcn) {
	.name			= "MPC86xx HPCN",
	.probe			= mpc86xx_hpcn_probe,
	.setup_arch		= mpc86xx_hpcn_setup_arch,
	.init_IRQ		= mpc86xx_hpcn_init_irq,
	.show_cpuinfo		= mpc86xx_hpcn_show_cpuinfo,
	.get_irq		= mpic_get_irq,
	.restart		= mpc86xx_restart,
	.time_init		= mpc86xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
