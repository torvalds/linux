/*
 * linux/arch/sh/boards/renesas/hs7751rvoip/pci.c
 *
 * Author:  Ian DaSilva (idasilva@mvista.com)
 *
 * Highly leveraged from pci-bigsur.c, written by Dustin McIntire.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * PCI initialization for the Renesas SH7751R HS7751RVoIP board
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/module.h>

#include <asm/io.h>
#include "../../../drivers/pci/pci-sh7751.h"
#include <asm/hs7751rvoip/hs7751rvoip.h>

#define PCIMCR_MRSET_OFF	0xBFFFFFFF
#define PCIMCR_RFSH_OFF		0xFFFFFFFB

/*
 * Only long word accesses of the PCIC's internal local registers and the
 * configuration registers from the CPU is supported.
 */
#define PCIC_WRITE(x,v) writel((v), PCI_REG(x))
#define PCIC_READ(x) readl(PCI_REG(x))

/*
 * Description:  This function sets up and initializes the pcic, sets
 * up the BARS, maps the DRAM into the address space etc, etc.
 */
int __init pcibios_init_platform(void)
{
	unsigned long bcr1, wcr1, wcr2, wcr3, mcr;
	unsigned short bcr2, bcr3;

	/*
	 * Initialize the slave bus controller on the pcic.  The values used
	 * here should not be hardcoded, but they should be taken from the bsc
	 * on the processor, to make this function as generic as possible.
	 * (i.e. Another sbc may usr different SDRAM timing settings -- in order
	 * for the pcic to work, its settings need to be exactly the same.)
	 */
	bcr1 = (*(volatile unsigned long *)(SH7751_BCR1));
	bcr2 = (*(volatile unsigned short *)(SH7751_BCR2));
	bcr3 = (*(volatile unsigned short *)(SH7751_BCR3));
	wcr1 = (*(volatile unsigned long *)(SH7751_WCR1));
	wcr2 = (*(volatile unsigned long *)(SH7751_WCR2));
	wcr3 = (*(volatile unsigned long *)(SH7751_WCR3));
	mcr = (*(volatile unsigned long *)(SH7751_MCR));

	bcr1 = bcr1 | 0x00080000;  /* Enable Bit 19, BREQEN */
	(*(volatile unsigned long *)(SH7751_BCR1)) = bcr1;

	bcr1 = bcr1 | 0x40080000;  /* Enable Bit 19 BREQEN, set PCIC to slave */
	PCIC_WRITE(SH7751_PCIBCR1, bcr1);	/* PCIC BCR1 */
	PCIC_WRITE(SH7751_PCIBCR2, bcr2);	/* PCIC BCR2 */
	PCIC_WRITE(SH7751_PCIBCR3, bcr3);	/* PCIC BCR3 */
	PCIC_WRITE(SH7751_PCIWCR1, wcr1);	/* PCIC WCR1 */
	PCIC_WRITE(SH7751_PCIWCR2, wcr2);	/* PCIC WCR2 */
	PCIC_WRITE(SH7751_PCIWCR3, wcr3);	/* PCIC WCR3 */
	mcr = (mcr & PCIMCR_MRSET_OFF) & PCIMCR_RFSH_OFF;
	PCIC_WRITE(SH7751_PCIMCR, mcr);		/* PCIC MCR */

	/* Enable all interrupts, so we know what to fix */
	PCIC_WRITE(SH7751_PCIINTM, 0x0000c3ff);
	PCIC_WRITE(SH7751_PCIAINTM, 0x0000380f);

	/* Set up standard PCI config registers */
	PCIC_WRITE(SH7751_PCICONF1, 0xFB900047); /* Bus Master, Mem & I/O access */
	PCIC_WRITE(SH7751_PCICONF2, 0x00000000); /* PCI Class code & Revision ID */
	PCIC_WRITE(SH7751_PCICONF4, 0xab000001); /* PCI I/O address (local regs) */
	PCIC_WRITE(SH7751_PCICONF5, 0x0c000000); /* PCI MEM address (local RAM)  */
	PCIC_WRITE(SH7751_PCICONF6, 0xd0000000); /* PCI MEM address (unused) */
	PCIC_WRITE(SH7751_PCICONF11, 0x35051054); /* PCI Subsystem ID & Vendor ID */
	PCIC_WRITE(SH7751_PCILSR0, 0x03f00000);	/* MEM (full 64M exposed) */
	PCIC_WRITE(SH7751_PCILSR1, 0x00000000); /* MEM (unused) */
	PCIC_WRITE(SH7751_PCILAR0, 0x0c000000); /* MEM (direct map from PCI) */
	PCIC_WRITE(SH7751_PCILAR1, 0x00000000); /* MEM (unused) */

	/* Now turn it on... */
	PCIC_WRITE(SH7751_PCICR, 0xa5000001);

	/*
	 * Set PCIMBR and PCIIOBR here, assuming a single window
	 * (16M MEM, 256K IO) is enough.  If a larger space is
	 * needed, the readx/writex and inx/outx functions will
	 * have to do more (e.g. setting registers for each call).
	 */

	/*
	 * Set the MBR so PCI address is one-to-one with window,
	 * meaning all calls go straight through... use ifdef to
	 * catch erroneous assumption.
	 */
	BUG_ON(PCIBIOS_MIN_MEM != SH7751_PCI_MEMORY_BASE);

	PCIC_WRITE(SH7751_PCIMBR, PCIBIOS_MIN_MEM);

	/* Set IOBR for window containing area specified in pci.h */
	PCIC_WRITE(SH7751_PCIIOBR, (PCIBIOS_MIN_IO & SH7751_PCIIOBR_MASK));

	/* All done, may as well say so... */
	printk("SH7751R PCI: Finished initialization of the PCI controller\n");

	return 1;
}

int __init pcibios_map_platform_irq(u8 slot, u8 pin)
{
        switch (slot) {
	case 0: return IRQ_PCISLOT;	/* PCI Extend slot */
	case 1: return IRQ_PCMCIA;	/* PCI Cardbus Bridge */
	case 2: return IRQ_PCIETH;	/* Realtek Ethernet controller */
	case 3: return IRQ_PCIHUB;	/* Realtek Ethernet Hub controller */
	default:
		printk("PCI: Bad IRQ mapping request for slot %d\n", slot);
		return -1;
	}
}

static struct resource sh7751_io_resource = {
	.name	= "SH7751_IO",
	.start	= 0x4000,
	.end	= 0x4000 + SH7751_PCI_IO_SIZE - 1,
	.flags	= IORESOURCE_IO
};

static struct resource sh7751_mem_resource = {
	.name	= "SH7751_mem",
	.start	= SH7751_PCI_MEMORY_BASE,
	.end	= SH7751_PCI_MEMORY_BASE + SH7751_PCI_MEM_SIZE - 1,
	.flags	= IORESOURCE_MEM
};

extern struct pci_ops sh7751_pci_ops;

struct pci_channel board_pci_channels[] = {
	{ &sh7751_pci_ops, &sh7751_io_resource, &sh7751_mem_resource, 0, 0xff },
	{ NULL, NULL, NULL, 0, 0 },
};
EXPORT_SYMBOL(board_pci_channels);
