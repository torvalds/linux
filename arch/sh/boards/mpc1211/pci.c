/*
 *	Low-Level PCI Support for the MPC-1211(CTP/PCI/MPC-SH02)
 *
 *  (c) 2002-2003 Saito.K & Jeanne
 *
 *  Dustin McIntire (dustin@sensoria.com)
 *	Derived from arch/i386/kernel/pci-*.c which bore the message:
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 *	
 *  May be copied or modified under the terms of the GNU General Public
 *  License.  See linux/COPYING for more information.
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/mpc1211/pci.h>

static struct resource mpcpci_io_resource = {
	"MPCPCI IO",
	0x00000000,
	0xffffffff,
	IORESOURCE_IO
};

static struct resource mpcpci_mem_resource = {
	"MPCPCI mem",
	0x00000000,
	0xffffffff,
	IORESOURCE_MEM
};

static struct pci_ops pci_direct_conf1;
struct pci_channel board_pci_channels[] = {
	{&pci_direct_conf1, &mpcpci_io_resource, &mpcpci_mem_resource, 0, 256},
	{NULL, NULL, NULL, 0, 0},
};

/*
 * Direct access to PCI hardware...
 */


#define CONFIG_CMD(bus, devfn, where) (0x80000000 | (bus->number << 16) | (devfn << 8) | (where & ~3))

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */
static int pci_conf1_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *value)
{
	u32 word;
	unsigned long flags;

	/* 
	 * PCIPDR may only be accessed as 32 bit words, 
	 * so we must do byte alignment by hand 
	 */
	local_irq_save(flags);
	writel(CONFIG_CMD(bus,devfn,where), PCIPAR);
	word = readl(PCIPDR);
	local_irq_restore(flags);

	switch (size) {
	case 1:
		switch (where & 0x3) {
		case 3:
			*value = (u8)(word >> 24);
			break;
		case 2:
			*value = (u8)(word >> 16);
			break;
		case 1:
			*value = (u8)(word >> 8);
			break;
		default:
			*value = (u8)word;
			break;
		}
		break;
	case 2:
		switch (where & 0x3) {
		case 3:
			*value = (u16)(word >> 24);
			local_irq_save(flags);
			writel(CONFIG_CMD(bus,devfn,(where+1)), PCIPAR);
			word = readl(PCIPDR);
			local_irq_restore(flags);
			*value |= ((word & 0xff) << 8);
			break;
		case 2:
			*value = (u16)(word >> 16);
			break;
		case 1:
			*value = (u16)(word >> 8);
			break;
		default:
			*value = (u16)word;
			break;
		}
		break;
	case 4:
		*value = word;
		break;
	}
	PCIDBG(4,"pci_conf1_read@0x%08x=0x%x\n", CONFIG_CMD(bus,devfn,where),*value); 
	return PCIBIOS_SUCCESSFUL;    
}

/* 
 * Since MPC-1211 only does 32bit access we'll have to do a read,mask,write operation.  
 * We'll allow an odd byte offset, though it should be illegal.
 */ 
static int pci_conf1_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 value)
{
	u32 word,mask = 0;
	unsigned long flags;
	u32 shift = (where & 3) * 8;

	if(size == 1) {
		mask = ((1 << 8) - 1) << shift;  // create the byte mask
	} else if(size == 2){
		if(shift == 24)
			return PCIBIOS_BAD_REGISTER_NUMBER;           
		mask = ((1 << 16) - 1) << shift;  // create the word mask
	}
	local_irq_save(flags);
	writel(CONFIG_CMD(bus,devfn,where), PCIPAR);
	if(size == 4){
		writel(value, PCIPDR);
		local_irq_restore(flags);
		PCIDBG(4,"pci_conf1_write@0x%08x=0x%x\n", CONFIG_CMD(bus,devfn,where),value);
		return PCIBIOS_SUCCESSFUL;
	}
	word = readl(PCIPDR);
	word &= ~mask;
	word |= ((value << shift) & mask);
	writel(word, PCIPDR);
	local_irq_restore(flags);
	PCIDBG(4,"pci_conf1_write@0x%08x=0x%x\n", CONFIG_CMD(bus,devfn,where),word);
	return PCIBIOS_SUCCESSFUL;
}

#undef CONFIG_CMD

static struct pci_ops pci_direct_conf1 = {
	.read =		pci_conf1_read,
	.write = 	pci_conf1_write,
};

static void __devinit quirk_ali_ide_ports(struct pci_dev *dev)
{
        dev->resource[0].start = 0x1f0;
	dev->resource[0].end   = 0x1f7;
	dev->resource[0].flags = IORESOURCE_IO;
        dev->resource[1].start = 0x3f6;
	dev->resource[1].end   = 0x3f6;
	dev->resource[1].flags = IORESOURCE_IO;
        dev->resource[2].start = 0x170;
	dev->resource[2].end   = 0x177;
	dev->resource[2].flags = IORESOURCE_IO;
        dev->resource[3].start = 0x376;
	dev->resource[3].end   = 0x376;
	dev->resource[3].flags = IORESOURCE_IO;
        dev->resource[4].start = 0xf000;
	dev->resource[4].end   = 0xf00f;
	dev->resource[4].flags = IORESOURCE_IO;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M5229, quirk_ali_ide_ports);

char * __devinit pcibios_setup(char *str)
{
	return str;
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void __init pcibios_fixup_bus(struct pci_bus *b)
{
	pci_read_bridge_bases(b);
}

/* 
 * 	IRQ functions 
 */
static inline u8 bridge_swizzle(u8 pin, u8 slot)
{
        return (((pin-1) + slot) % 4) + 1;
}

static inline u8 bridge_swizzle_pci_1(u8 pin, u8 slot)
{
        return (((pin-1) - slot) & 3) + 1;
}

static u8 __init mpc1211_swizzle(struct pci_dev *dev, u8 *pinp)
{
	unsigned long flags;
        u8 pin = *pinp;
	u32 word;

	for ( ; dev->bus->self; dev = dev->bus->self) {
		if (!pin)
			continue;

		if (dev->bus->number == 1) {
			local_irq_save(flags);
			writel(0x80000000 | 0x2c, PCIPAR);
			word = readl(PCIPDR);
			local_irq_restore(flags);
			word >>= 16;

			if (word == 0x0001)
				pin = bridge_swizzle_pci_1(pin, PCI_SLOT(dev->devfn));
			else
				pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
		} else
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
	}

	*pinp = pin;

	return PCI_SLOT(dev->devfn);
}

static int __init map_mpc1211_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = -1;

	/* now lookup the actual IRQ on a platform specific basis (pci-'platform'.c) */
	if (dev->bus->number == 0) {
		switch (slot) {
		case 13:   irq =  9; break;   /* USB */
		case 22:   irq = 10; break;   /* LAN */
		default:   irq =  0; break;
	  	}
	} else {
		switch (pin) {
		case 0:   irq =  0; break;
		case 1:   irq =  7; break;
		case 2:   irq =  9; break;
		case 3:   irq = 10; break;
		case 4:   irq = 11; break;
		}
	}

	if( irq < 0 ) {
		PCIDBG(3, "PCI: Error mapping IRQ on device %s\n", pci_name(dev));
		return irq;
	}
	
	PCIDBG(2, "Setting IRQ for slot %s to %d\n", pci_name(dev), irq);

	return irq;
}

void __init pcibios_fixup_irqs(void)
{
	pci_fixup_irqs(mpc1211_swizzle, map_mpc1211_irq);
}

void pcibios_align_resource(void *data, struct resource *res,
			    resource_size_t size, resource_size_t align)
{
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		if (start >= 0x10000UL) {
			if ((start & 0xffffUL) < 0x4000UL) {
				start = (start & 0xffff0000UL) + 0x4000UL;
			} else if ((start & 0xffffUL) >= 0xf000UL) {
				start = (start & 0xffff0000UL) + 0x10000UL;
			}
			res->start = start;
		} else {
			if (start & 0x300) {
				start = (start + 0x3ff) & ~0x3ff;
				res->start = start;
			}
		}
	}
}

