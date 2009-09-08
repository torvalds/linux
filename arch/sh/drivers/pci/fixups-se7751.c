#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/io.h>
#include "pci-sh4.h"

int __init pcibios_map_platform_irq(u8 slot, u8 pin)
{
        switch (slot) {
        case 0: return 13;
        case 1: return 13;	/* AMD Ethernet controller */
        case 2: return -1;
        case 3: return -1;
        case 4: return -1;
        default:
                printk("PCI: Bad IRQ mapping request for slot %d\n", slot);
                return -1;
        }
}

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
int pci_fixup_pcic(struct pci_channel *chan)
{
	unsigned long bcr1, wcr1, wcr2, wcr3, mcr;
	unsigned short bcr2;

	/*
	* Initialize the slave bus controller on the pcic.  The values used
	* here should not be hardcoded, but they should be taken from the bsc
	* on the processor, to make this function as generic as possible.
	* (i.e. Another sbc may usr different SDRAM timing settings -- in order
	* for the pcic to work, its settings need to be exactly the same.)
	*/
	bcr1 = (*(volatile unsigned long*)(SH7751_BCR1));
	bcr2 = (*(volatile unsigned short*)(SH7751_BCR2));
	wcr1 = (*(volatile unsigned long*)(SH7751_WCR1));
	wcr2 = (*(volatile unsigned long*)(SH7751_WCR2));
	wcr3 = (*(volatile unsigned long*)(SH7751_WCR3));
	mcr = (*(volatile unsigned long*)(SH7751_MCR));

	bcr1 = bcr1 | 0x00080000;  /* Enable Bit 19, BREQEN */
	(*(volatile unsigned long*)(SH7751_BCR1)) = bcr1;

	bcr1 = bcr1 | 0x40080000;  /* Enable Bit 19 BREQEN, set PCIC to slave */
	PCIC_WRITE(SH7751_PCIBCR1, bcr1);	 /* PCIC BCR1 */
	PCIC_WRITE(SH7751_PCIBCR2, bcr2);     /* PCIC BCR2 */
	PCIC_WRITE(SH7751_PCIWCR1, wcr1);     /* PCIC WCR1 */
	PCIC_WRITE(SH7751_PCIWCR2, wcr2);     /* PCIC WCR2 */
	PCIC_WRITE(SH7751_PCIWCR3, wcr3);     /* PCIC WCR3 */
	mcr = (mcr & PCIMCR_MRSET_OFF) & PCIMCR_RFSH_OFF;
	PCIC_WRITE(SH7751_PCIMCR, mcr);      /* PCIC MCR */


	/* Enable all interrupts, so we know what to fix */
	PCIC_WRITE(SH7751_PCIINTM, 0x0000c3ff);
	PCIC_WRITE(SH7751_PCIAINTM, 0x0000380f);

	/* Set up standard PCI config registers */
	PCIC_WRITE(SH7751_PCICONF1,	0xF39000C7); /* Bus Master, Mem & I/O access */
	PCIC_WRITE(SH7751_PCICONF2,	0x00000000); /* PCI Class code & Revision ID */
	PCIC_WRITE(SH7751_PCICONF4,	0xab000001); /* PCI I/O address (local regs) */
	PCIC_WRITE(SH7751_PCICONF5,	0x0c000000); /* PCI MEM address (local RAM)  */
	PCIC_WRITE(SH7751_PCICONF6,	0xd0000000); /* PCI MEM address (unused)     */
	PCIC_WRITE(SH7751_PCICONF11, 0x35051054); /* PCI Subsystem ID & Vendor ID */
	PCIC_WRITE(SH7751_PCILSR0, 0x03f00000);   /* MEM (full 64M exposed)       */
	PCIC_WRITE(SH7751_PCILSR1, 0x00000000);   /* MEM (unused)                 */
	PCIC_WRITE(SH7751_PCILAR0, 0x0c000000);   /* MEM (direct map from PCI)    */
	PCIC_WRITE(SH7751_PCILAR1, 0x00000000);   /* MEM (unused)                 */

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
	* meaning all calls go straight through... use BUG_ON to
	* catch erroneous assumption.
	*/
	BUG_ON(chan->mem_resource->start != SH7751_PCI_MEMORY_BASE);

	PCIC_WRITE(SH7751_PCIMBR, chan->mem_resource->start);

	/* Set IOBR for window containing area specified in pci.h */
	PCIC_WRITE(SH7751_PCIIOBR, (chan->io_resource->start & SH7751_PCIIOBR_MASK));

	/* All done, may as well say so... */
	printk("SH7751 PCI: Finished initialization of the PCI controller\n");

	return 1;
}
