/*
 * arch/xtensa/pcibios.c
 *
 * PCI bios-type initialisation for PCI machines
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 2001-2005 Tensilica Inc.
 *
 * Based largely on work from Cort (ppc/kernel/pci.c)
 * IO functions copied from sparc.
 *
 * Chris Zankel <chris@zankel.net>
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/bootmem.h>

#include <asm/pci-bridge.h>
#include <asm/platform.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/* PCI Controller */


/*
 * pcibios_alloc_controller
 * pcibios_enable_device
 * pcibios_fixups
 * pcibios_align_resource
 * pcibios_fixup_bus
 * pcibios_setup
 * pci_bus_add_device
 * pci_mmap_page_range
 */

struct pci_controller* pci_ctrl_head;
struct pci_controller** pci_ctrl_tail = &pci_ctrl_head;

static int pci_bus_count;

/*
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 *
 * Why? Because some silly external IO cards only decode
 * the low 10 bits of the IO address. The 0x00-0xff region
 * is reserved for motherboard devices that decode all 16
 * bits, so it's ok to allocate at, say, 0x2800-0x28ff,
 * but we want to try to avoid allocating at 0x2900-0x2bff
 * which might have be mirrored at 0x0100-0x03ff..
 */
void
pcibios_align_resource(void *data, struct resource *res, unsigned long size,
    		       unsigned long align)
{
	struct pci_dev *dev = data;

	if (res->flags & IORESOURCE_IO) {
		unsigned long start = res->start;

		if (size > 0x100) {
			printk(KERN_ERR "PCI: I/O Region %s/%d too large"
			       " (%ld bytes)\n", pci_name(dev),
			       dev->resource - res, size);
		}

		if (start & 0x300) {
			start = (start + 0x3ff) & ~0x3ff;
			res->start = start;
		}
	}
}

int
pcibios_enable_resources(struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for(idx=0; idx<6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk (KERN_ERR "PCI: Device %s not available because "
				"of resource collisions\n", pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (dev->resource[PCI_ROM_RESOURCE].start)
		cmd |= PCI_COMMAND_MEMORY;
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n",
			pci_name(dev), old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

struct pci_controller * __init pcibios_alloc_controller(void)
{
	struct pci_controller *pci_ctrl;

	pci_ctrl = (struct pci_controller *)alloc_bootmem(sizeof(*pci_ctrl));
	memset(pci_ctrl, 0, sizeof(struct pci_controller));

	*pci_ctrl_tail = pci_ctrl;
	pci_ctrl_tail = &pci_ctrl->next;

	return pci_ctrl;
}

static int __init pcibios_init(void)
{
	struct pci_controller *pci_ctrl;
	struct pci_bus *bus;
	int next_busno = 0, i;

	printk("PCI: Probing PCI hardware\n");

	/* Scan all of the recorded PCI controllers.  */
	for (pci_ctrl = pci_ctrl_head; pci_ctrl; pci_ctrl = pci_ctrl->next) {
		pci_ctrl->last_busno = 0xff;
		bus = pci_scan_bus(pci_ctrl->first_busno, pci_ctrl->ops,
				   pci_ctrl);
		if (pci_ctrl->io_resource.flags) {
			unsigned long offs;

			offs = (unsigned long)pci_ctrl->io_space.base;
			pci_ctrl->io_resource.start += offs;
			pci_ctrl->io_resource.end += offs;
			bus->resource[0] = &pci_ctrl->io_resource;
		}
		for (i = 0; i < 3; ++i)
			if (pci_ctrl->mem_resources[i].flags)
				bus->resource[i+1] =&pci_ctrl->mem_resources[i];
		pci_ctrl->bus = bus;
		pci_ctrl->last_busno = bus->subordinate;
		if (next_busno <= pci_ctrl->last_busno)
			next_busno = pci_ctrl->last_busno+1;
	}
	pci_bus_count = next_busno;

	return platform_pcibios_fixup();
}

subsys_initcall(pcibios_init);

void __init pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_controller *pci_ctrl = bus->sysdata;
	struct resource *res;
	unsigned long io_offset;
	int i;

	io_offset = (unsigned long)pci_ctrl->io_space.base;
	if (bus->parent == NULL) {
		/* this is a host bridge - fill in its resources */
		pci_ctrl->bus = bus;

		bus->resource[0] = res = &pci_ctrl->io_resource;
		if (!res->flags) {
			if (io_offset)
				printk (KERN_ERR "I/O resource not set for host"
					" bridge %d\n", pci_ctrl->index);
			res->start = 0;
			res->end = IO_SPACE_LIMIT;
			res->flags = IORESOURCE_IO;
		}
		res->start += io_offset;
		res->end += io_offset;

		for (i = 0; i < 3; i++) {
			res = &pci_ctrl->mem_resources[i];
			if (!res->flags) {
				if (i > 0)
					continue;
				printk(KERN_ERR "Memory resource not set for "
				       "host bridge %d\n", pci_ctrl->index);
				res->start = 0;
				res->end = ~0U;
				res->flags = IORESOURCE_MEM;
			}
			bus->resource[i+1] = res;
		}
	} else {
		/* This is a subordinate bridge */
		pci_read_bridge_bases(bus);

		for (i = 0; i < 4; i++) {
			if ((res = bus->resource[i]) == NULL || !res->flags)
				continue;
			if (io_offset && (res->flags & IORESOURCE_IO)) {
				res->start += io_offset;
				res->end += io_offset;
			}
		}
	}
}

char __init *pcibios_setup(char *str)
{
	return str;
}

/* the next one is stolen from the alpha port... */

void __init
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx=0; idx<6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because "
			       "of resource collisions\n", pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n",
		       pci_name(dev), old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}

	return 0;
}

#ifdef CONFIG_PROC_FS

/*
 * Return the index of the PCI controller for device pdev.
 */

int
pci_controller_num(struct pci_dev *dev)
{
	struct pci_controller *pci_ctrl = (struct pci_controller*) dev->sysdata;
	return pci_ctrl->index;
}

#endif /* CONFIG_PROC_FS */

/*
 * Platform support for /proc/bus/pci/X/Y mmap()s,
 * modelled on the sparc64 implementation by Dave Miller.
 *  -- paulus.
 */

/*
 * Adjust vm_pgoff of VMA such that it is the physical page offset
 * corresponding to the 32-bit pci bus offset for DEV requested by the user.
 *
 * Basically, the user finds the base address for his device which he wishes
 * to mmap.  They read the 32-bit value from the config space base register,
 * add whatever PAGE_SIZE multiple offset they wish, and feed this into the
 * offset parameter of mmap on /proc/bus/pci/XXX for that device.
 *
 * Returns negative error code on failure, zero on success.
 */
static __inline__ int
__pci_mmap_make_offset(struct pci_dev *dev, struct vm_area_struct *vma,
		       enum pci_mmap_state mmap_state)
{
	struct pci_controller *pci_ctrl = (struct pci_controller*) dev->sysdata;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long io_offset = 0;
	int i, res_bit;

	if (pci_ctrl == 0)
		return -EINVAL;		/* should never happen */

	/* If memory, add on the PCI bridge address offset */
	if (mmap_state == pci_mmap_mem) {
		res_bit = IORESOURCE_MEM;
	} else {
		io_offset = (unsigned long)pci_ctrl->io_space.base;
		offset += io_offset;
		res_bit = IORESOURCE_IO;
	}

	/*
	 * Check that the offset requested corresponds to one of the
	 * resources of the device.
	 */
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		struct resource *rp = &dev->resource[i];
		int flags = rp->flags;

		/* treat ROM as memory (should be already) */
		if (i == PCI_ROM_RESOURCE)
			flags |= IORESOURCE_MEM;

		/* Active and same type? */
		if ((flags & res_bit) == 0)
			continue;

		/* In the range of this resource? */
		if (offset < (rp->start & PAGE_MASK) || offset > rp->end)
			continue;

		/* found it! construct the final physical address */
		if (mmap_state == pci_mmap_io)
			offset += pci_ctrl->io_space.start - io_offset;
		vma->vm_pgoff = offset >> PAGE_SHIFT;
		return 0;
	}

	return -EINVAL;
}

/*
 * Set vm_page_prot of VMA, as appropriate for this architecture, for a pci
 * device mapping.
 */
static __inline__ void
__pci_mmap_set_pgprot(struct pci_dev *dev, struct vm_area_struct *vma,
		      enum pci_mmap_state mmap_state, int write_combine)
{
	int prot = pgprot_val(vma->vm_page_prot);

	/* Set to write-through */
	prot &= ~_PAGE_NO_CACHE;
#if 0
	if (!write_combine)
		prot |= _PAGE_WRITETHRU;
#endif
	vma->vm_page_prot = __pgprot(prot);
}

/*
 * Perform the actual remap of the pages for a PCI device mapping, as
 * appropriate for this architecture.  The region in the process to map
 * is described by vm_start and vm_end members of VMA, the base physical
 * address is found in vm_pgoff.
 * The pci device structure is provided so that architectures may make mapping
 * decisions on a per-device or per-bus basis.
 *
 * Returns a negative error code on failure, zero on success.
 */
int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state,
			int write_combine)
{
	int ret;

	ret = __pci_mmap_make_offset(dev, vma, mmap_state);
	if (ret < 0)
		return ret;

	__pci_mmap_set_pgprot(dev, vma, mmap_state, write_combine);

	ret = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			         vma->vm_end - vma->vm_start,vma->vm_page_prot);

	return ret;
}

/*
 * This probably belongs here rather than ioport.c because
 * we do not want this crud linked into SBus kernels.
 * Also, think for a moment about likes of floppy.c that
 * include architecture specific parts. They may want to redefine ins/outs.
 *
 * We do not use horroble macroses here because we want to
 * advance pointer by sizeof(size).
 */
void outsb(unsigned long addr, const void *src, unsigned long count) {
        while (count) {
                count -= 1;
                writeb(*(const char *)src, addr);
                src += 1;
                addr += 1;
        }
}

void outsw(unsigned long addr, const void *src, unsigned long count) {
        while (count) {
                count -= 2;
                writew(*(const short *)src, addr);
                src += 2;
                addr += 2;
        }
}

void outsl(unsigned long addr, const void *src, unsigned long count) {
        while (count) {
                count -= 4;
                writel(*(const long *)src, addr);
                src += 4;
                addr += 4;
        }
}

void insb(unsigned long addr, void *dst, unsigned long count) {
        while (count) {
                count -= 1;
                *(unsigned char *)dst = readb(addr);
                dst += 1;
                addr += 1;
        }
}

void insw(unsigned long addr, void *dst, unsigned long count) {
        while (count) {
                count -= 2;
                *(unsigned short *)dst = readw(addr);
                dst += 2;
                addr += 2;
        }
}

void insl(unsigned long addr, void *dst, unsigned long count) {
        while (count) {
                count -= 4;
                /*
                 * XXX I am sure we are in for an unaligned trap here.
                 */
                *(unsigned long *)dst = readl(addr);
                dst += 4;
                addr += 4;
        }
}



