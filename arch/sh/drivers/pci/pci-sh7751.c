/*
 *	Low-Level PCI Support for the SH7751
 *
 *  Dustin McIntire (dustin@sensoria.com)
 *	Derived from arch/i386/kernel/pci-*.c which bore the message:
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 *
 *  Ported to the new API by Paul Mundt <lethal@linux-sh.org>
 *  With cleanup by Paul van Gool <pvangool@mimotech.com>
 *
 *  May be copied or modified under the terms of the GNU General Public
 *  License.  See linux/COPYING for more information.
 *
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/delay.h>

#include <asm/machvec.h>
#include <asm/io.h>
#include "pci-sh7751.h"

static unsigned int pci_probe = PCI_PROBE_CONF1;
extern int pci_fixup_pcic(void);

void pcibios_fixup_irqs(void) __attribute__ ((weak));

/*
 * Direct access to PCI hardware...
 */

#define CONFIG_CMD(bus, devfn, where) (0x80000000 | (bus->number << 16) | (devfn << 8) | (where & ~3))

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */
static int sh7751_pci_read(struct pci_bus *bus, unsigned int devfn,
			   int where, int size, u32 *val)
{
	unsigned long flags;
	u32 data;

	/* 
	 * PCIPDR may only be accessed as 32 bit words, 
	 * so we must do byte alignment by hand 
	 */
	local_irq_save(flags);
	outl(CONFIG_CMD(bus,devfn,where), PCI_REG(SH7751_PCIPAR));
	data = inl(PCI_REG(SH7751_PCIPDR));
	local_irq_restore(flags);

	switch (size) {
	case 1:
		*val = (data >> ((where & 3) << 3)) & 0xff;
		break;
	case 2:
		*val = (data >> ((where & 2) << 3)) & 0xffff;
		break;
	case 4:
		*val = data;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	return PCIBIOS_SUCCESSFUL;
}

/* 
 * Since SH7751 only does 32bit access we'll have to do a read,
 * mask,write operation.
 * We'll allow an odd byte offset, though it should be illegal.
 */ 
static int sh7751_pci_write(struct pci_bus *bus, unsigned int devfn,
			    int where, int size, u32 val)
{
	unsigned long flags;
	int shift;
	u32 data;

	local_irq_save(flags);
	outl(CONFIG_CMD(bus,devfn,where), PCI_REG(SH7751_PCIPAR));
	data = inl(PCI_REG(SH7751_PCIPDR));
	local_irq_restore(flags);

	switch (size) {
	case 1:
		shift = (where & 3) << 3;
		data &= ~(0xff << shift);
		data |= ((val & 0xff) << shift);
		break;
	case 2:
		shift = (where & 2) << 3;
		data &= ~(0xffff << shift);
		data |= ((val & 0xffff) << shift);
		break;
	case 4:
		data = val;
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	outl(data, PCI_REG(SH7751_PCIPDR));

	return PCIBIOS_SUCCESSFUL;
}

#undef CONFIG_CMD

struct pci_ops sh7751_pci_ops = {
	.read 		= sh7751_pci_read,
	.write		= sh7751_pci_write,
};

static int __init pci_check_direct(void)
{
	unsigned int tmp, id;

	/* check for SH7751/SH7751R hardware */
	id = inl(SH7751_PCIREG_BASE+SH7751_PCICONF0);
	if (id != ((SH7751_DEVICE_ID << 16) | SH7751_VENDOR_ID) &&
	    id != ((SH7751R_DEVICE_ID << 16) | SH7751_VENDOR_ID)) {
		pr_debug("PCI: This is not an SH7751(R) (%x)\n", id);
		return -ENODEV;
	}

	/*
	 * Check if configuration works.
	 */
	if (pci_probe & PCI_PROBE_CONF1) {
		tmp = inl (PCI_REG(SH7751_PCIPAR));
		outl (0x80000000, PCI_REG(SH7751_PCIPAR));
		if (inl (PCI_REG(SH7751_PCIPAR)) == 0x80000000) {
			outl (tmp, PCI_REG(SH7751_PCIPAR));
			printk(KERN_INFO "PCI: Using configuration type 1\n");
			request_region(PCI_REG(SH7751_PCIPAR), 8, "PCI conf1");
			return 0;
		}
		outl (tmp, PCI_REG(SH7751_PCIPAR));
	}

	pr_debug("PCI: pci_check_direct failed\n");
	return -EINVAL;
}

/***************************************************************************************/

/*
 *  Handle bus scanning and fixups ....
 */

static void __init pci_fixup_ide_bases(struct pci_dev *d)
{
	int i;

	/*
	 * PCI IDE controllers use non-standard I/O port decoding, respect it.
	 */
	if ((d->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;
	pr_debug("PCI: IDE base address fixup for %s\n", pci_name(d));
	for(i=0; i<4; i++) {
		struct resource *r = &d->resource[i];
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}

DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pci_fixup_ide_bases);

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void __init pcibios_fixup_bus(struct pci_bus *b)
{
	pci_read_bridge_bases(b);
}

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space.
 * 
 * Note that the platform specific initialization (BSC registers, and memory
 * space mapping) will be called via the machine vectors (sh_mv.mv_pci_init()) if it
 * exitst and via the platform defined function pcibios_init_platform().  
 * See pci_bigsur.c for implementation;
 * 
 * The BIOS version of the pci functions is not yet implemented but it is left
 * in for completeness.  Currently an error will be genereated at compile time. 
 */

static int __init sh7751_pci_init(void)
{
	int ret;

	pr_debug("PCI: Starting intialization.\n");
	if ((ret = pci_check_direct()) != 0)
		return ret;

	return pcibios_init_platform();
}

subsys_initcall(sh7751_pci_init);

static int __init __area_sdram_check(unsigned int area)
{
	u32 word;

	word = inl(SH7751_BCR1);
	/* check BCR for SDRAM in area */
	if(((word >> area) & 1) == 0) {
		printk("PCI: Area %d is not configured for SDRAM. BCR1=0x%x\n",
		       area, word);
		return 0;
	}
	outl(word, PCI_REG(SH7751_PCIBCR1));

	word = (u16)inw(SH7751_BCR2);
	/* check BCR2 for 32bit SDRAM interface*/
	if(((word >> (area << 1)) & 0x3) != 0x3) {
		printk("PCI: Area %d is not 32 bit SDRAM. BCR2=0x%x\n",
		       area, word);
		return 0;
	}
	outl(word, PCI_REG(SH7751_PCIBCR2));

	return 1;
}

int __init sh7751_pcic_init(struct sh7751_pci_address_map *map)
{
	u32 reg;
	u32 word;

	/* Set the BCR's to enable PCI access */
	reg = inl(SH7751_BCR1);
	reg |= 0x80000;
	outl(reg, SH7751_BCR1);
	
	/* Turn the clocks back on (not done in reset)*/
	outl(0, PCI_REG(SH7751_PCICLKR));
	/* Clear Powerdown IRQ's (not done in reset) */
	word = SH7751_PCIPINT_D3 | SH7751_PCIPINT_D0;
	outl(word, PCI_REG(SH7751_PCIPINT));

	/*
	 * This code is unused for some boards as it is done in the
	 * bootloader and doing it here means the MAC addresses loaded
	 * by the bootloader get lost.
	 */
	if (!(map->flags & SH7751_PCIC_NO_RESET)) {
		/* toggle PCI reset pin */
		word = SH7751_PCICR_PREFIX | SH7751_PCICR_PRST;
		outl(word,PCI_REG(SH7751_PCICR));
		/* Wait for a long time... not 1 sec. but long enough */
		mdelay(100);
		word = SH7751_PCICR_PREFIX;
		outl(word,PCI_REG(SH7751_PCICR));
	}
	
	/* set the command/status bits to:
	 * Wait Cycle Control + Parity Enable + Bus Master +
	 * Mem space enable
	 */
	word = SH7751_PCICONF1_WCC | SH7751_PCICONF1_PER | 
	       SH7751_PCICONF1_BUM | SH7751_PCICONF1_MES;
	outl(word, PCI_REG(SH7751_PCICONF1));

	/* define this host as the host bridge */
	word = SH7751_PCI_HOST_BRIDGE << 24;
	outl(word, PCI_REG(SH7751_PCICONF2));

	/* Set IO and Mem windows to local address 
	 * Make PCI and local address the same for easy 1 to 1 mapping 
	 * Window0 = map->window0.size @ non-cached area base = SDRAM
	 * Window1 = map->window1.size @ cached area base = SDRAM 
	 */
	word = map->window0.size - 1;
	outl(word, PCI_REG(SH7751_PCILSR0));
	word = map->window1.size - 1;
	outl(word, PCI_REG(SH7751_PCILSR1));
	/* Set the values on window 0 PCI config registers */
	word = P2SEGADDR(map->window0.base);
	outl(word, PCI_REG(SH7751_PCILAR0));
	outl(word, PCI_REG(SH7751_PCICONF5));
	/* Set the values on window 1 PCI config registers */
	word =  PHYSADDR(map->window1.base);
	outl(word, PCI_REG(SH7751_PCILAR1));
	outl(word, PCI_REG(SH7751_PCICONF6));

	/* Set the local 16MB PCI memory space window to 
	 * the lowest PCI mapped address
	 */
	word = PCIBIOS_MIN_MEM & SH7751_PCIMBR_MASK;
	PCIDBG(2,"PCI: Setting upper bits of Memory window to 0x%x\n", word);
	outl(word , PCI_REG(SH7751_PCIMBR));

	/* Map IO space into PCI IO window
	 * The IO window is 64K-PCIBIOS_MIN_IO in size
	 * IO addresses will be translated to the 
	 * PCI IO window base address
	 */
	PCIDBG(3,"PCI: Mapping IO address 0x%x - 0x%x to base 0x%x\n", PCIBIOS_MIN_IO,
	    (64*1024), SH7751_PCI_IO_BASE+PCIBIOS_MIN_IO);

	/* 
	 * XXX: For now, leave this board-specific. In the event we have other
	 * boards that need to do similar work, this can be wrapped.
	 */
#ifdef CONFIG_SH_BIGSUR
	bigsur_port_map(PCIBIOS_MIN_IO, (64*1024), SH7751_PCI_IO_BASE+PCIBIOS_MIN_IO,0);
#endif

	/* Make sure the MSB's of IO window are set to access PCI space correctly */
	word = PCIBIOS_MIN_IO & SH7751_PCIIOBR_MASK;
	PCIDBG(2,"PCI: Setting upper bits of IO window to 0x%x\n", word);
	outl(word, PCI_REG(SH7751_PCIIOBR));
	
	/* Set PCI WCRx, BCRx's, copy from BSC locations */

	/* check BCR for SDRAM in specified area */
	switch (map->window0.base) {
	case SH7751_CS0_BASE_ADDR: word = __area_sdram_check(0); break;
	case SH7751_CS1_BASE_ADDR: word = __area_sdram_check(1); break;
	case SH7751_CS2_BASE_ADDR: word = __area_sdram_check(2); break;
	case SH7751_CS3_BASE_ADDR: word = __area_sdram_check(3); break;
	case SH7751_CS4_BASE_ADDR: word = __area_sdram_check(4); break;
	case SH7751_CS5_BASE_ADDR: word = __area_sdram_check(5); break;
	case SH7751_CS6_BASE_ADDR: word = __area_sdram_check(6); break;
	}
	
	if (!word)
		return 0;

	/* configure the wait control registers */
	word = inl(SH7751_WCR1);
	outl(word, PCI_REG(SH7751_PCIWCR1));
	word = inl(SH7751_WCR2);
	outl(word, PCI_REG(SH7751_PCIWCR2));
	word = inl(SH7751_WCR3);
	outl(word, PCI_REG(SH7751_PCIWCR3));
	word = inl(SH7751_MCR);
	outl(word, PCI_REG(SH7751_PCIMCR));

	/* NOTE: I'm ignoring the PCI error IRQs for now..
	 * TODO: add support for the internal error interrupts and
	 * DMA interrupts...
	 */

#ifdef CONFIG_SH_RTS7751R2D
	pci_fixup_pcic();
#endif

	/* SH7751 init done, set central function init complete */
	/* use round robin mode to stop a device starving/overruning */
	word = SH7751_PCICR_PREFIX | SH7751_PCICR_CFIN | SH7751_PCICR_ARBM;
	outl(word,PCI_REG(SH7751_PCICR)); 

	return 1;
}

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;
	}

	return str;
}

/* 
 * 	IRQ functions 
 */
static u8 __init sh7751_no_swizzle(struct pci_dev *dev, u8 *pin)
{
	/* no swizzling */
	return PCI_SLOT(dev->devfn);
}

static int sh7751_pci_lookup_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = -1;

	/* now lookup the actual IRQ on a platform specific basis (pci-'platform'.c) */
	irq = pcibios_map_platform_irq(slot,pin);
	if( irq < 0 ) {
		pr_debug("PCI: Error mapping IRQ on device %s\n", pci_name(dev));
		return irq;
	}

	pr_debug("Setting IRQ for slot %s to %d\n", pci_name(dev), irq);

	return irq;
}

void __init pcibios_fixup_irqs(void)
{
	pci_fixup_irqs(sh7751_no_swizzle, sh7751_pci_lookup_irq);
}

