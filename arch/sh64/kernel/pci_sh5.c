/*
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 * Copyright (C) 2003, 2004 Paul Mundt
 * Copyright (C) 2004 Richard Curnow
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Support functions for the SH5 PCI hardware.
 */

#include <linux/kernel.h>
#include <linux/rwsem.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <asm/pci.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/hardware.h>
#include "pci_sh5.h"

static unsigned long pcicr_virt;
unsigned long pciio_virt;

static void __init pci_fixup_ide_bases(struct pci_dev *d)
{
	int i;

	/*
	 * PCI IDE controllers use non-standard I/O port decoding, respect it.
	 */
	if ((d->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;
	printk("PCI: IDE base address fixup for %s\n", pci_name(d));
	for(i=0; i<4; i++) {
		struct resource *r = &d->resource[i];
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pci_fixup_ide_bases);

char * __devinit pcibios_setup(char *str)
{
	return str;
}

/* Rounds a number UP to the nearest power of two. Used for
 * sizing the PCI window.
 */
static u32 __init r2p2(u32 num)
{
	int i = 31;
	u32 tmp = num;

	if (num == 0)
		return 0;

	do {
		if (tmp & (1 << 31))
			break;
		i--;
		tmp <<= 1;
	} while (i >= 0);

	tmp = 1 << i;
	/* If the original number isn't a power of 2, round it up */
	if (tmp != num)
		tmp <<= 1;

	return tmp;
}

extern unsigned long long memory_start, memory_end;

int __init sh5pci_init(unsigned memStart, unsigned memSize)
{
	u32 lsr0;
	u32 uval;

	pcicr_virt = onchip_remap(SH5PCI_ICR_BASE, 1024, "PCICR");
	if (!pcicr_virt) {
		panic("Unable to remap PCICR\n");
	}

	pciio_virt = onchip_remap(SH5PCI_IO_BASE, 0x10000, "PCIIO");
	if (!pciio_virt) {
		panic("Unable to remap PCIIO\n");
	}

	pr_debug("Register base addres is 0x%08lx\n", pcicr_virt);

	/* Clear snoop registers */
        SH5PCI_WRITE(CSCR0, 0);
        SH5PCI_WRITE(CSCR1, 0);

	pr_debug("Wrote to reg\n");

        /* Switch off interrupts */
        SH5PCI_WRITE(INTM,  0);
        SH5PCI_WRITE(AINTM, 0);
        SH5PCI_WRITE(PINTM, 0);

        /* Set bus active, take it out of reset */
        uval = SH5PCI_READ(CR);

	/* Set command Register */
        SH5PCI_WRITE(CR, uval | CR_LOCK_MASK | CR_CFINT| CR_FTO | CR_PFE | CR_PFCS | CR_BMAM);

	uval=SH5PCI_READ(CR);
        pr_debug("CR is actually 0x%08x\n",uval);

        /* Allow it to be a master */
	/* NB - WE DISABLE I/O ACCESS to stop overlap */
        /* set WAIT bit to enable stepping, an attempt to improve stability */
	SH5PCI_WRITE_SHORT(CSR_CMD,
			    PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_WAIT);

        /*
        ** Set translation mapping memory in order to convert the address
        ** used for the main bus, to the PCI internal address.
        */
        SH5PCI_WRITE(MBR,0x40000000);

        /* Always set the max size 512M */
        SH5PCI_WRITE(MBMR, PCISH5_MEM_SIZCONV(512*1024*1024));

        /*
        ** I/O addresses are mapped at internal PCI specific address
        ** as is described into the configuration bridge table.
        ** These are changed to 0, to allow cards that have legacy
        ** io such as vga to function correctly. We set the SH5 IOBAR to
        ** 256K, which is a bit big as we can only have 64K of address space
        */

        SH5PCI_WRITE(IOBR,0x0);

	pr_debug("PCI:Writing 0x%08x to IOBR\n",0);

        /* Set up a 256K window. Totally pointless waste  of address space */
        SH5PCI_WRITE(IOBMR,0);
	pr_debug("PCI:Writing 0x%08x to IOBMR\n",0);

	/* The SH5 has a HUGE 256K I/O region, which breaks the PCI spec. Ideally,
         * we would want to map the I/O region somewhere, but it is so big this is not
         * that easy!
         */
	SH5PCI_WRITE(CSR_IBAR0,~0);
	/* Set memory size value */
        memSize = memory_end - memory_start;

        /* Now we set up the mbars so the PCI bus can see the memory of the machine */
        if (memSize < (1024 * 1024)) {
                printk(KERN_ERR "PCISH5: Ridiculous memory size of 0x%x?\n", memSize);
                return -EINVAL;
        }

        /* Set LSR 0 */
        lsr0 = (memSize > (512 * 1024 * 1024)) ? 0x1ff00001 : ((r2p2(memSize) - 0x100000) | 0x1);
        SH5PCI_WRITE(LSR0, lsr0);

	pr_debug("PCI:Writing 0x%08x to LSR0\n",lsr0);

        /* Set MBAR 0 */
        SH5PCI_WRITE(CSR_MBAR0, memory_start);
        SH5PCI_WRITE(LAR0, memory_start);

        SH5PCI_WRITE(CSR_MBAR1,0);
        SH5PCI_WRITE(LAR1,0);
        SH5PCI_WRITE(LSR1,0);

	pr_debug("PCI:Writing 0x%08llx to CSR_MBAR0\n",memory_start);
	pr_debug("PCI:Writing 0x%08llx to LAR0\n",memory_start);

        /* Enable the PCI interrupts on the device */
        SH5PCI_WRITE(INTM,  ~0);
        SH5PCI_WRITE(AINTM, ~0);
        SH5PCI_WRITE(PINTM, ~0);

	pr_debug("Switching on all error interrupts\n");

        return(0);
}

static int sh5pci_read(struct pci_bus *bus, unsigned int devfn, int where,
			int size, u32 *val)
{
	SH5PCI_WRITE(PAR, CONFIG_CMD(bus, devfn, where));

	switch (size) {
		case 1:
			*val = (u8)SH5PCI_READ_BYTE(PDR + (where & 3));
			break;
		case 2:
			*val = (u16)SH5PCI_READ_SHORT(PDR + (where & 2));
			break;
		case 4:
			*val = SH5PCI_READ(PDR);
			break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int sh5pci_write(struct pci_bus *bus, unsigned int devfn, int where,
			 int size, u32 val)
{
	SH5PCI_WRITE(PAR, CONFIG_CMD(bus, devfn, where));

	switch (size) {
		case 1:
			SH5PCI_WRITE_BYTE(PDR + (where & 3), (u8)val);
			break;
		case 2:
			SH5PCI_WRITE_SHORT(PDR + (where & 2), (u16)val);
			break;
		case 4:
			SH5PCI_WRITE(PDR, val);
			break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops pci_config_ops = {
	.read =		sh5pci_read,
	.write =	sh5pci_write,
};

/* Everything hangs off this */
static struct pci_bus *pci_root_bus;


static u8 __init no_swizzle(struct pci_dev *dev, u8 * pin)
{
	pr_debug("swizzle for dev %d on bus %d slot %d pin is %d\n",
	         dev->devfn,dev->bus->number, PCI_SLOT(dev->devfn),*pin);
	return PCI_SLOT(dev->devfn);
}

static inline u8 bridge_swizzle(u8 pin, u8 slot)
{
	return (((pin-1) + slot) % 4) + 1;
}

u8 __init common_swizzle(struct pci_dev *dev, u8 *pinp)
{
	if (dev->bus->number != 0) {
		u8 pin = *pinp;
		do {
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
			/* Move up the chain of bridges. */
			dev = dev->bus->self;
		} while (dev->bus->self);
		*pinp = pin;

		/* The slot is the slot of the last bridge. */
	}

	return PCI_SLOT(dev->devfn);
}

/* This needs to be shunted out of here into the board specific bit */

static int __init map_cayman_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	int result = -1;

	/* The complication here is that the PCI IRQ lines from the Cayman's 2
	   5V slots get into the CPU via a different path from the IRQ lines
	   from the 3 3.3V slots.  Thus, we have to detect whether the card's
	   interrupts go via the 5V or 3.3V path, i.e. the 'bridge swizzling'
	   at the point where we cross from 5V to 3.3V is not the normal case.

	   The added complication is that we don't know that the 5V slots are
	   always bus 2, because a card containing a PCI-PCI bridge may be
	   plugged into a 3.3V slot, and this changes the bus numbering.

	   Also, the Cayman has an intermediate PCI bus that goes a custom
	   expansion board header (and to the secondary bridge).  This bus has
	   never been used in practice.

	   The 1ary onboard PCI-PCI bridge is device 3 on bus 0
	   The 2ary onboard PCI-PCI bridge is device 0 on the 2ary bus of the 1ary bridge.
	   */

	struct slot_pin {
		int slot;
		int pin;
	} path[4];
	int i=0;

	while (dev->bus->number > 0) {

		slot = path[i].slot = PCI_SLOT(dev->devfn);
		pin = path[i].pin = bridge_swizzle(pin, slot);
		dev = dev->bus->self;
		i++;
		if (i > 3) panic("PCI path to root bus too long!\n");
	}

	slot = PCI_SLOT(dev->devfn);
	/* This is the slot on bus 0 through which the device is eventually
	   reachable. */

	/* Now work back up. */
	if ((slot < 3) || (i == 0)) {
		/* Bus 0 (incl. PCI-PCI bridge itself) : perform the final
		   swizzle now. */
		result = IRQ_INTA + bridge_swizzle(pin, slot) - 1;
	} else {
		i--;
		slot = path[i].slot;
		pin  = path[i].pin;
		if (slot > 0) {
			panic("PCI expansion bus device found - not handled!\n");
		} else {
			if (i > 0) {
				/* 5V slots */
				i--;
				slot = path[i].slot;
				pin  = path[i].pin;
				/* 'pin' was swizzled earlier wrt slot, don't do it again. */
				result = IRQ_P2INTA + (pin - 1);
			} else {
				/* IRQ for 2ary PCI-PCI bridge : unused */
				result = -1;
			}
		}
	}

	return result;
}

static irqreturn_t pcish5_err_irq(int irq, void *dev_id)
{
	struct pt_regs *regs = get_irq_regs();
	unsigned pci_int, pci_air, pci_cir, pci_aint;

	pci_int = SH5PCI_READ(INT);
	pci_cir = SH5PCI_READ(CIR);
	pci_air = SH5PCI_READ(AIR);

	if (pci_int) {
		printk("PCI INTERRUPT (at %08llx)!\n", regs->pc);
		printk("PCI INT -> 0x%x\n", pci_int & 0xffff);
		printk("PCI AIR -> 0x%x\n", pci_air);
		printk("PCI CIR -> 0x%x\n", pci_cir);
		SH5PCI_WRITE(INT, ~0);
	}

	pci_aint = SH5PCI_READ(AINT);
	if (pci_aint) {
		printk("PCI ARB INTERRUPT!\n");
		printk("PCI AINT -> 0x%x\n", pci_aint);
		printk("PCI AIR -> 0x%x\n", pci_air);
		printk("PCI CIR -> 0x%x\n", pci_cir);
		SH5PCI_WRITE(AINT, ~0);
	}

	return IRQ_HANDLED;
}

static irqreturn_t pcish5_serr_irq(int irq, void *dev_id)
{
	printk("SERR IRQ\n");

	return IRQ_NONE;
}

static void __init
pcibios_size_bridge(struct pci_bus *bus, struct resource *ior,
		    struct resource *memr)
{
	struct resource io_res, mem_res;
	struct pci_dev *dev;
	struct pci_dev *bridge = bus->self;
	struct list_head *ln;

	if (!bridge)
		return;	/* host bridge, nothing to do */

	/* set reasonable default locations for pcibios_align_resource */
	io_res.start = PCIBIOS_MIN_IO;
	mem_res.start = PCIBIOS_MIN_MEM;

	io_res.end = io_res.start;
	mem_res.end = mem_res.start;

	/* Collect information about how our direct children are layed out. */
	for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next) {
		int i;
		dev = pci_dev_b(ln);

		/* Skip bridges for now */
		if (dev->class >> 8 == PCI_CLASS_BRIDGE_PCI)
			continue;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource res;
			unsigned long size;

			memcpy(&res, &dev->resource[i], sizeof(res));
			size = res.end - res.start + 1;

			if (res.flags & IORESOURCE_IO) {
				res.start = io_res.end;
				pcibios_align_resource(dev, &res, size, 0);
				io_res.end = res.start + size;
			} else if (res.flags & IORESOURCE_MEM) {
				res.start = mem_res.end;
				pcibios_align_resource(dev, &res, size, 0);
				mem_res.end = res.start + size;
			}
		}
	}

	/* And for all of the subordinate busses. */
	for (ln=bus->children.next; ln != &bus->children; ln=ln->next)
		pcibios_size_bridge(pci_bus_b(ln), &io_res, &mem_res);

	/* turn the ending locations into sizes (subtract start) */
	io_res.end -= io_res.start;
	mem_res.end -= mem_res.start;

	/* Align the sizes up by bridge rules */
	io_res.end = ALIGN(io_res.end, 4*1024) - 1;
	mem_res.end = ALIGN(mem_res.end, 1*1024*1024) - 1;

	/* Adjust the bridge's allocation requirements */
	bridge->resource[0].end = bridge->resource[0].start + io_res.end;
	bridge->resource[1].end = bridge->resource[1].start + mem_res.end;

	bridge->resource[PCI_BRIDGE_RESOURCES].end =
	    bridge->resource[PCI_BRIDGE_RESOURCES].start + io_res.end;
	bridge->resource[PCI_BRIDGE_RESOURCES+1].end =
	    bridge->resource[PCI_BRIDGE_RESOURCES+1].start + mem_res.end;

	/* adjust parent's resource requirements */
	if (ior) {
		ior->end = ALIGN(ior->end, 4*1024);
		ior->end += io_res.end;
	}

	if (memr) {
		memr->end = ALIGN(memr->end, 1*1024*1024);
		memr->end += mem_res.end;
	}
}

static void __init pcibios_size_bridges(void)
{
	struct resource io_res, mem_res;

	memset(&io_res, 0, sizeof(io_res));
	memset(&mem_res, 0, sizeof(mem_res));

	pcibios_size_bridge(pci_root_bus, &io_res, &mem_res);
}

static int __init pcibios_init(void)
{
        if (request_irq(IRQ_ERR, pcish5_err_irq,
                        IRQF_DISABLED, "PCI Error",NULL) < 0) {
                printk(KERN_ERR "PCISH5: Cannot hook PCI_PERR interrupt\n");
                return -EINVAL;
        }

        if (request_irq(IRQ_SERR, pcish5_serr_irq,
                        IRQF_DISABLED, "PCI SERR interrupt", NULL) < 0) {
                printk(KERN_ERR "PCISH5: Cannot hook PCI_SERR interrupt\n");
                return -EINVAL;
        }

	/* The pci subsytem needs to know where memory is and how much
	 * of it there is. I've simply made these globals. A better mechanism
	 * is probably needed.
	 */
	sh5pci_init(__pa(memory_start),
		     __pa(memory_end) - __pa(memory_start));

	pci_root_bus = pci_scan_bus(0, &pci_config_ops, NULL);
	pcibios_size_bridges();
	pci_assign_unassigned_resources();
	pci_fixup_irqs(no_swizzle, map_cayman_irq);

	return 0;
}

subsys_initcall(pcibios_init);

void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev = bus->self;
	int i;

#if 1
	if(dev) {
		for(i=0; i<3; i++) {
			bus->resource[i] =
				&dev->resource[PCI_BRIDGE_RESOURCES+i];
			bus->resource[i]->name = bus->name;
		}
		bus->resource[0]->flags |= IORESOURCE_IO;
		bus->resource[1]->flags |= IORESOURCE_MEM;

		/* For now, propagate host limits to the bus;
		 * we'll adjust them later. */

#if 1
		bus->resource[0]->end = 64*1024 - 1 ;
		bus->resource[1]->end = PCIBIOS_MIN_MEM+(256*1024*1024)-1;
		bus->resource[0]->start = PCIBIOS_MIN_IO;
		bus->resource[1]->start = PCIBIOS_MIN_MEM;
#else
		bus->resource[0]->end = 0;
		bus->resource[1]->end = 0;
		bus->resource[0]->start =0;
		bus->resource[1]->start = 0;
#endif
		/* Turn off downstream PF memory address range by default */
		bus->resource[2]->start = 1024*1024;
		bus->resource[2]->end = bus->resource[2]->start - 1;
	}
#endif

}

