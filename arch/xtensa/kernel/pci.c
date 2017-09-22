/*
 * arch/xtensa/kernel/pci.c
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
resource_size_t
pcibios_align_resource(void *data, const struct resource *res,
		       resource_size_t size, resource_size_t align)
{
	struct pci_dev *dev = data;
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		if (size > 0x100) {
			pr_err("PCI: I/O Region %s/%d too large (%u bytes)\n",
					pci_name(dev), dev->resource - res,
					size);
		}

		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	}

	return start;
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

static void __init pci_controller_apertures(struct pci_controller *pci_ctrl,
					    struct list_head *resources)
{
	struct resource *res;
	unsigned long io_offset;
	int i;

	io_offset = (unsigned long)pci_ctrl->io_space.base;
	res = &pci_ctrl->io_resource;
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
	pci_add_resource_offset(resources, res, io_offset);

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
		pci_add_resource(resources, res);
	}
}

static int __init pcibios_init(void)
{
	struct pci_controller *pci_ctrl;
	struct list_head resources;
	struct pci_bus *bus;
	int next_busno = 0, ret;

	printk("PCI: Probing PCI hardware\n");

	/* Scan all of the recorded PCI controllers.  */
	for (pci_ctrl = pci_ctrl_head; pci_ctrl; pci_ctrl = pci_ctrl->next) {
		pci_ctrl->last_busno = 0xff;
		INIT_LIST_HEAD(&resources);
		pci_controller_apertures(pci_ctrl, &resources);
		bus = pci_scan_root_bus(NULL, pci_ctrl->first_busno,
					pci_ctrl->ops, pci_ctrl, &resources);
		if (!bus)
			continue;

		pci_ctrl->bus = bus;
		pci_ctrl->last_busno = bus->busn_res.end;
		if (next_busno <= pci_ctrl->last_busno)
			next_busno = pci_ctrl->last_busno+1;
	}
	pci_bus_count = next_busno;
	ret = platform_pcibios_fixup();
	if (ret)
		return ret;

	for (pci_ctrl = pci_ctrl_head; pci_ctrl; pci_ctrl = pci_ctrl->next) {
		if (pci_ctrl->bus)
			pci_bus_add_devices(pci_ctrl->bus);
	}

	return 0;
}

subsys_initcall(pcibios_init);

void pcibios_fixup_bus(struct pci_bus *bus)
{
	if (bus->parent) {
		/* This is a subordinate bridge */
		pci_read_bridge_bases(bus);
	}
}

void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
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
 * Perform the actual remap of the pages for a PCI device mapping, as
 * appropriate for this architecture.  The region in the process to map
 * is described by vm_start and vm_end members of VMA, the base physical
 * address is found in vm_pgoff.
 * The pci device structure is provided so that architectures may make mapping
 * decisions on a per-device or per-bus basis.
 *
 * Returns a negative error code on failure, zero on success.
 */
int pci_mmap_page_range(struct pci_dev *dev, int bar,
			struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state,
			int write_combine)
{
	int ret;

	ret = __pci_mmap_make_offset(dev, vma, mmap_state);
	if (ret < 0)
		return ret;

	vma->vm_page_prot = pgprot_device(vma->vm_page_prot);

	ret = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			         vma->vm_end - vma->vm_start,vma->vm_page_prot);

	return ret;
}
