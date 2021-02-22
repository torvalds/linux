// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Contains common pci routines for ALL ppc platform
 * (based on pci_32.c and pci_64.c)
 *
 * Port for PPC64 David Engebretsen, IBM Corp.
 * Contains common pci routines for ppc64 platform, pSeries and iSeries brands.
 *
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *   Rework, based on alpha PCI code.
 *
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/mm.h>
#include <linux/shmem_fs.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/vgaarb.h>
#include <linux/numa.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/byteorder.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>
#include <asm/eeh.h>

#include "../../../drivers/pci/pci.h"

/* hose_spinlock protects accesses to the the phb_bitmap. */
static DEFINE_SPINLOCK(hose_spinlock);
LIST_HEAD(hose_list);

/* For dynamic PHB numbering on get_phb_number(): max number of PHBs. */
#define MAX_PHBS 0x10000

/*
 * For dynamic PHB numbering: used/free PHBs tracking bitmap.
 * Accesses to this bitmap should be protected by hose_spinlock.
 */
static DECLARE_BITMAP(phb_bitmap, MAX_PHBS);

/* ISA Memory physical address */
resource_size_t isa_mem_base;
EXPORT_SYMBOL(isa_mem_base);


static const struct dma_map_ops *pci_dma_ops;

void set_pci_dma_ops(const struct dma_map_ops *dma_ops)
{
	pci_dma_ops = dma_ops;
}

/*
 * This function should run under locking protection, specifically
 * hose_spinlock.
 */
static int get_phb_number(struct device_node *dn)
{
	int ret, phb_id = -1;
	u32 prop_32;
	u64 prop;

	/*
	 * Try fixed PHB numbering first, by checking archs and reading
	 * the respective device-tree properties. Firstly, try powernv by
	 * reading "ibm,opal-phbid", only present in OPAL environment.
	 */
	ret = of_property_read_u64(dn, "ibm,opal-phbid", &prop);
	if (ret) {
		ret = of_property_read_u32_index(dn, "reg", 1, &prop_32);
		prop = prop_32;
	}

	if (!ret)
		phb_id = (int)(prop & (MAX_PHBS - 1));

	/* We need to be sure to not use the same PHB number twice. */
	if ((phb_id >= 0) && !test_and_set_bit(phb_id, phb_bitmap))
		return phb_id;

	/*
	 * If not pseries nor powernv, or if fixed PHB numbering tried to add
	 * the same PHB number twice, then fallback to dynamic PHB numbering.
	 */
	phb_id = find_first_zero_bit(phb_bitmap, MAX_PHBS);
	BUG_ON(phb_id >= MAX_PHBS);
	set_bit(phb_id, phb_bitmap);

	return phb_id;
}

struct pci_controller *pcibios_alloc_controller(struct device_node *dev)
{
	struct pci_controller *phb;

	phb = zalloc_maybe_bootmem(sizeof(struct pci_controller), GFP_KERNEL);
	if (phb == NULL)
		return NULL;
	spin_lock(&hose_spinlock);
	phb->global_number = get_phb_number(dev);
	list_add_tail(&phb->list_node, &hose_list);
	spin_unlock(&hose_spinlock);
	phb->dn = dev;
	phb->is_dynamic = slab_is_available();
#ifdef CONFIG_PPC64
	if (dev) {
		int nid = of_node_to_nid(dev);

		if (nid < 0 || !node_online(nid))
			nid = NUMA_NO_NODE;

		PHB_SET_NODE(phb, nid);
	}
#endif
	return phb;
}
EXPORT_SYMBOL_GPL(pcibios_alloc_controller);

void pcibios_free_controller(struct pci_controller *phb)
{
	spin_lock(&hose_spinlock);

	/* Clear bit of phb_bitmap to allow reuse of this PHB number. */
	if (phb->global_number < MAX_PHBS)
		clear_bit(phb->global_number, phb_bitmap);

	list_del(&phb->list_node);
	spin_unlock(&hose_spinlock);

	if (phb->is_dynamic)
		kfree(phb);
}
EXPORT_SYMBOL_GPL(pcibios_free_controller);

/*
 * This function is used to call pcibios_free_controller()
 * in a deferred manner: a callback from the PCI subsystem.
 *
 * _*DO NOT*_ call pcibios_free_controller() explicitly if
 * this is used (or it may access an invalid *phb pointer).
 *
 * The callback occurs when all references to the root bus
 * are dropped (e.g., child buses/devices and their users).
 *
 * It's called as .release_fn() of 'struct pci_host_bridge'
 * which is associated with the 'struct pci_controller.bus'
 * (root bus) - it expects .release_data to hold a pointer
 * to 'struct pci_controller'.
 *
 * In order to use it, register .release_fn()/release_data
 * like this:
 *
 * pci_set_host_bridge_release(bridge,
 *                             pcibios_free_controller_deferred
 *                             (void *) phb);
 *
 * e.g. in the pcibios_root_bridge_prepare() callback from
 * pci_create_root_bus().
 */
void pcibios_free_controller_deferred(struct pci_host_bridge *bridge)
{
	struct pci_controller *phb = (struct pci_controller *)
					 bridge->release_data;

	pr_debug("domain %d, dynamic %d\n", phb->global_number, phb->is_dynamic);

	pcibios_free_controller(phb);
}
EXPORT_SYMBOL_GPL(pcibios_free_controller_deferred);

/*
 * The function is used to return the minimal alignment
 * for memory or I/O windows of the associated P2P bridge.
 * By default, 4KiB alignment for I/O windows and 1MiB for
 * memory windows.
 */
resource_size_t pcibios_window_alignment(struct pci_bus *bus,
					 unsigned long type)
{
	struct pci_controller *phb = pci_bus_to_host(bus);

	if (phb->controller_ops.window_alignment)
		return phb->controller_ops.window_alignment(bus, type);

	/*
	 * PCI core will figure out the default
	 * alignment: 4KiB for I/O and 1MiB for
	 * memory window.
	 */
	return 1;
}

void pcibios_setup_bridge(struct pci_bus *bus, unsigned long type)
{
	struct pci_controller *hose = pci_bus_to_host(bus);

	if (hose->controller_ops.setup_bridge)
		hose->controller_ops.setup_bridge(bus, type);
}

void pcibios_reset_secondary_bus(struct pci_dev *dev)
{
	struct pci_controller *phb = pci_bus_to_host(dev->bus);

	if (phb->controller_ops.reset_secondary_bus) {
		phb->controller_ops.reset_secondary_bus(dev);
		return;
	}

	pci_reset_secondary_bus(dev);
}

resource_size_t pcibios_default_alignment(void)
{
	if (ppc_md.pcibios_default_alignment)
		return ppc_md.pcibios_default_alignment();

	return 0;
}

#ifdef CONFIG_PCI_IOV
resource_size_t pcibios_iov_resource_alignment(struct pci_dev *pdev, int resno)
{
	if (ppc_md.pcibios_iov_resource_alignment)
		return ppc_md.pcibios_iov_resource_alignment(pdev, resno);

	return pci_iov_resource_size(pdev, resno);
}

int pcibios_sriov_enable(struct pci_dev *pdev, u16 num_vfs)
{
	if (ppc_md.pcibios_sriov_enable)
		return ppc_md.pcibios_sriov_enable(pdev, num_vfs);

	return 0;
}

int pcibios_sriov_disable(struct pci_dev *pdev)
{
	if (ppc_md.pcibios_sriov_disable)
		return ppc_md.pcibios_sriov_disable(pdev);

	return 0;
}

#endif /* CONFIG_PCI_IOV */

static resource_size_t pcibios_io_size(const struct pci_controller *hose)
{
#ifdef CONFIG_PPC64
	return hose->pci_io_size;
#else
	return resource_size(&hose->io_resource);
#endif
}

int pcibios_vaddr_is_ioport(void __iomem *address)
{
	int ret = 0;
	struct pci_controller *hose;
	resource_size_t size;

	spin_lock(&hose_spinlock);
	list_for_each_entry(hose, &hose_list, list_node) {
		size = pcibios_io_size(hose);
		if (address >= hose->io_base_virt &&
		    address < (hose->io_base_virt + size)) {
			ret = 1;
			break;
		}
	}
	spin_unlock(&hose_spinlock);
	return ret;
}

unsigned long pci_address_to_pio(phys_addr_t address)
{
	struct pci_controller *hose;
	resource_size_t size;
	unsigned long ret = ~0;

	spin_lock(&hose_spinlock);
	list_for_each_entry(hose, &hose_list, list_node) {
		size = pcibios_io_size(hose);
		if (address >= hose->io_base_phys &&
		    address < (hose->io_base_phys + size)) {
			unsigned long base =
				(unsigned long)hose->io_base_virt - _IO_BASE;
			ret = base + (address - hose->io_base_phys);
			break;
		}
	}
	spin_unlock(&hose_spinlock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_address_to_pio);

/*
 * Return the domain number for this bus.
 */
int pci_domain_nr(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);

	return hose->global_number;
}
EXPORT_SYMBOL(pci_domain_nr);

/* This routine is meant to be used early during boot, when the
 * PCI bus numbers have not yet been assigned, and you need to
 * issue PCI config cycles to an OF device.
 * It could also be used to "fix" RTAS config cycles if you want
 * to set pci_assign_all_buses to 1 and still use RTAS for PCI
 * config cycles.
 */
struct pci_controller* pci_find_hose_for_OF_device(struct device_node* node)
{
	while(node) {
		struct pci_controller *hose, *tmp;
		list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
			if (hose->dn == node)
				return hose;
		node = node->parent;
	}
	return NULL;
}

struct pci_controller *pci_find_controller_for_domain(int domain_nr)
{
	struct pci_controller *hose;

	list_for_each_entry(hose, &hose_list, list_node)
		if (hose->global_number == domain_nr)
			return hose;

	return NULL;
}

struct pci_intx_virq {
	int virq;
	struct kref kref;
	struct list_head list_node;
};

static LIST_HEAD(intx_list);
static DEFINE_MUTEX(intx_mutex);

static void ppc_pci_intx_release(struct kref *kref)
{
	struct pci_intx_virq *vi = container_of(kref, struct pci_intx_virq, kref);

	list_del(&vi->list_node);
	irq_dispose_mapping(vi->virq);
	kfree(vi);
}

static int ppc_pci_unmap_irq_line(struct notifier_block *nb,
			       unsigned long action, void *data)
{
	struct pci_dev *pdev = to_pci_dev(data);

	if (action == BUS_NOTIFY_DEL_DEVICE) {
		struct pci_intx_virq *vi;

		mutex_lock(&intx_mutex);
		list_for_each_entry(vi, &intx_list, list_node) {
			if (vi->virq == pdev->irq) {
				kref_put(&vi->kref, ppc_pci_intx_release);
				break;
			}
		}
		mutex_unlock(&intx_mutex);
	}

	return NOTIFY_DONE;
}

static struct notifier_block ppc_pci_unmap_irq_notifier = {
	.notifier_call = ppc_pci_unmap_irq_line,
};

static int ppc_pci_register_irq_notifier(void)
{
	return bus_register_notifier(&pci_bus_type, &ppc_pci_unmap_irq_notifier);
}
arch_initcall(ppc_pci_register_irq_notifier);

/*
 * Reads the interrupt pin to determine if interrupt is use by card.
 * If the interrupt is used, then gets the interrupt line from the
 * openfirmware and sets it in the pci_dev and pci_config line.
 */
static int pci_read_irq_line(struct pci_dev *pci_dev)
{
	int virq;
	struct pci_intx_virq *vi, *vitmp;

	/* Preallocate vi as rewind is complex if this fails after mapping */
	vi = kzalloc(sizeof(struct pci_intx_virq), GFP_KERNEL);
	if (!vi)
		return -1;

	pr_debug("PCI: Try to map irq for %s...\n", pci_name(pci_dev));

	/* Try to get a mapping from the device-tree */
	virq = of_irq_parse_and_map_pci(pci_dev, 0, 0);
	if (virq <= 0) {
		u8 line, pin;

		/* If that fails, lets fallback to what is in the config
		 * space and map that through the default controller. We
		 * also set the type to level low since that's what PCI
		 * interrupts are. If your platform does differently, then
		 * either provide a proper interrupt tree or don't use this
		 * function.
		 */
		if (pci_read_config_byte(pci_dev, PCI_INTERRUPT_PIN, &pin))
			goto error_exit;
		if (pin == 0)
			goto error_exit;
		if (pci_read_config_byte(pci_dev, PCI_INTERRUPT_LINE, &line) ||
		    line == 0xff || line == 0) {
			goto error_exit;
		}
		pr_debug(" No map ! Using line %d (pin %d) from PCI config\n",
			 line, pin);

		virq = irq_create_mapping(NULL, line);
		if (virq)
			irq_set_irq_type(virq, IRQ_TYPE_LEVEL_LOW);
	}

	if (!virq) {
		pr_debug(" Failed to map !\n");
		goto error_exit;
	}

	pr_debug(" Mapped to linux irq %d\n", virq);

	pci_dev->irq = virq;

	mutex_lock(&intx_mutex);
	list_for_each_entry(vitmp, &intx_list, list_node) {
		if (vitmp->virq == virq) {
			kref_get(&vitmp->kref);
			kfree(vi);
			vi = NULL;
			break;
		}
	}
	if (vi) {
		vi->virq = virq;
		kref_init(&vi->kref);
		list_add_tail(&vi->list_node, &intx_list);
	}
	mutex_unlock(&intx_mutex);

	return 0;
error_exit:
	kfree(vi);
	return -1;
}

/*
 * Platform support for /proc/bus/pci/X/Y mmap()s.
 *  -- paulus.
 */
int pci_iobar_pfn(struct pci_dev *pdev, int bar, struct vm_area_struct *vma)
{
	struct pci_controller *hose = pci_bus_to_host(pdev->bus);
	resource_size_t ioaddr = pci_resource_start(pdev, bar);

	if (!hose)
		return -EINVAL;

	/* Convert to an offset within this PCI controller */
	ioaddr -= (unsigned long)hose->io_base_virt - _IO_BASE;

	vma->vm_pgoff += (ioaddr + hose->io_base_phys) >> PAGE_SHIFT;
	return 0;
}

/*
 * This one is used by /dev/mem and fbdev who have no clue about the
 * PCI device, it tries to find the PCI device first and calls the
 * above routine
 */
pgprot_t pci_phys_mem_access_prot(struct file *file,
				  unsigned long pfn,
				  unsigned long size,
				  pgprot_t prot)
{
	struct pci_dev *pdev = NULL;
	struct resource *found = NULL;
	resource_size_t offset = ((resource_size_t)pfn) << PAGE_SHIFT;
	int i;

	if (page_is_ram(pfn))
		return prot;

	prot = pgprot_noncached(prot);
	for_each_pci_dev(pdev) {
		for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
			struct resource *rp = &pdev->resource[i];
			int flags = rp->flags;

			/* Active and same type? */
			if ((flags & IORESOURCE_MEM) == 0)
				continue;
			/* In the range of this resource? */
			if (offset < (rp->start & PAGE_MASK) ||
			    offset > rp->end)
				continue;
			found = rp;
			break;
		}
		if (found)
			break;
	}
	if (found) {
		if (found->flags & IORESOURCE_PREFETCH)
			prot = pgprot_noncached_wc(prot);
		pci_dev_put(pdev);
	}

	pr_debug("PCI: Non-PCI map for %llx, prot: %lx\n",
		 (unsigned long long)offset, pgprot_val(prot));

	return prot;
}

/* This provides legacy IO read access on a bus */
int pci_legacy_read(struct pci_bus *bus, loff_t port, u32 *val, size_t size)
{
	unsigned long offset;
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct resource *rp = &hose->io_resource;
	void __iomem *addr;

	/* Check if port can be supported by that bus. We only check
	 * the ranges of the PHB though, not the bus itself as the rules
	 * for forwarding legacy cycles down bridges are not our problem
	 * here. So if the host bridge supports it, we do it.
	 */
	offset = (unsigned long)hose->io_base_virt - _IO_BASE;
	offset += port;

	if (!(rp->flags & IORESOURCE_IO))
		return -ENXIO;
	if (offset < rp->start || (offset + size) > rp->end)
		return -ENXIO;
	addr = hose->io_base_virt + port;

	switch(size) {
	case 1:
		*((u8 *)val) = in_8(addr);
		return 1;
	case 2:
		if (port & 1)
			return -EINVAL;
		*((u16 *)val) = in_le16(addr);
		return 2;
	case 4:
		if (port & 3)
			return -EINVAL;
		*((u32 *)val) = in_le32(addr);
		return 4;
	}
	return -EINVAL;
}

/* This provides legacy IO write access on a bus */
int pci_legacy_write(struct pci_bus *bus, loff_t port, u32 val, size_t size)
{
	unsigned long offset;
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct resource *rp = &hose->io_resource;
	void __iomem *addr;

	/* Check if port can be supported by that bus. We only check
	 * the ranges of the PHB though, not the bus itself as the rules
	 * for forwarding legacy cycles down bridges are not our problem
	 * here. So if the host bridge supports it, we do it.
	 */
	offset = (unsigned long)hose->io_base_virt - _IO_BASE;
	offset += port;

	if (!(rp->flags & IORESOURCE_IO))
		return -ENXIO;
	if (offset < rp->start || (offset + size) > rp->end)
		return -ENXIO;
	addr = hose->io_base_virt + port;

	/* WARNING: The generic code is idiotic. It gets passed a pointer
	 * to what can be a 1, 2 or 4 byte quantity and always reads that
	 * as a u32, which means that we have to correct the location of
	 * the data read within those 32 bits for size 1 and 2
	 */
	switch(size) {
	case 1:
		out_8(addr, val >> 24);
		return 1;
	case 2:
		if (port & 1)
			return -EINVAL;
		out_le16(addr, val >> 16);
		return 2;
	case 4:
		if (port & 3)
			return -EINVAL;
		out_le32(addr, val);
		return 4;
	}
	return -EINVAL;
}

/* This provides legacy IO or memory mmap access on a bus */
int pci_mmap_legacy_page_range(struct pci_bus *bus,
			       struct vm_area_struct *vma,
			       enum pci_mmap_state mmap_state)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	resource_size_t offset =
		((resource_size_t)vma->vm_pgoff) << PAGE_SHIFT;
	resource_size_t size = vma->vm_end - vma->vm_start;
	struct resource *rp;

	pr_debug("pci_mmap_legacy_page_range(%04x:%02x, %s @%llx..%llx)\n",
		 pci_domain_nr(bus), bus->number,
		 mmap_state == pci_mmap_mem ? "MEM" : "IO",
		 (unsigned long long)offset,
		 (unsigned long long)(offset + size - 1));

	if (mmap_state == pci_mmap_mem) {
		/* Hack alert !
		 *
		 * Because X is lame and can fail starting if it gets an error trying
		 * to mmap legacy_mem (instead of just moving on without legacy memory
		 * access) we fake it here by giving it anonymous memory, effectively
		 * behaving just like /dev/zero
		 */
		if ((offset + size) > hose->isa_mem_size) {
			printk(KERN_DEBUG
			       "Process %s (pid:%d) mapped non-existing PCI legacy memory for 0%04x:%02x\n",
			       current->comm, current->pid, pci_domain_nr(bus), bus->number);
			if (vma->vm_flags & VM_SHARED)
				return shmem_zero_setup(vma);
			return 0;
		}
		offset += hose->isa_mem_phys;
	} else {
		unsigned long io_offset = (unsigned long)hose->io_base_virt - _IO_BASE;
		unsigned long roffset = offset + io_offset;
		rp = &hose->io_resource;
		if (!(rp->flags & IORESOURCE_IO))
			return -ENXIO;
		if (roffset < rp->start || (roffset + size) > rp->end)
			return -ENXIO;
		offset += hose->io_base_phys;
	}
	pr_debug(" -> mapping phys %llx\n", (unsigned long long)offset);

	vma->vm_pgoff = offset >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

void pci_resource_to_user(const struct pci_dev *dev, int bar,
			  const struct resource *rsrc,
			  resource_size_t *start, resource_size_t *end)
{
	struct pci_bus_region region;

	if (rsrc->flags & IORESOURCE_IO) {
		pcibios_resource_to_bus(dev->bus, &region,
					(struct resource *) rsrc);
		*start = region.start;
		*end = region.end;
		return;
	}

	/* We pass a CPU physical address to userland for MMIO instead of a
	 * BAR value because X is lame and expects to be able to use that
	 * to pass to /dev/mem!
	 *
	 * That means we may have 64-bit values where some apps only expect
	 * 32 (like X itself since it thinks only Sparc has 64-bit MMIO).
	 */
	*start = rsrc->start;
	*end = rsrc->end;
}

/**
 * pci_process_bridge_OF_ranges - Parse PCI bridge resources from device tree
 * @hose: newly allocated pci_controller to be setup
 * @dev: device node of the host bridge
 * @primary: set if primary bus (32 bits only, soon to be deprecated)
 *
 * This function will parse the "ranges" property of a PCI host bridge device
 * node and setup the resource mapping of a pci controller based on its
 * content.
 *
 * Life would be boring if it wasn't for a few issues that we have to deal
 * with here:
 *
 *   - We can only cope with one IO space range and up to 3 Memory space
 *     ranges. However, some machines (thanks Apple !) tend to split their
 *     space into lots of small contiguous ranges. So we have to coalesce.
 *
 *   - Some busses have IO space not starting at 0, which causes trouble with
 *     the way we do our IO resource renumbering. The code somewhat deals with
 *     it for 64 bits but I would expect problems on 32 bits.
 *
 *   - Some 32 bits platforms such as 4xx can have physical space larger than
 *     32 bits so we need to use 64 bits values for the parsing
 */
void pci_process_bridge_OF_ranges(struct pci_controller *hose,
				  struct device_node *dev, int primary)
{
	int memno = 0;
	struct resource *res;
	struct of_pci_range range;
	struct of_pci_range_parser parser;

	printk(KERN_INFO "PCI host bridge %pOF %s ranges:\n",
	       dev, primary ? "(primary)" : "");

	/* Check for ranges property */
	if (of_pci_range_parser_init(&parser, dev))
		return;

	/* Parse it */
	for_each_of_pci_range(&parser, &range) {
		/* If we failed translation or got a zero-sized region
		 * (some FW try to feed us with non sensical zero sized regions
		 * such as power3 which look like some kind of attempt at exposing
		 * the VGA memory hole)
		 */
		if (range.cpu_addr == OF_BAD_ADDR || range.size == 0)
			continue;

		/* Act based on address space type */
		res = NULL;
		switch (range.flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_IO:
			printk(KERN_INFO
			       "  IO 0x%016llx..0x%016llx -> 0x%016llx\n",
			       range.cpu_addr, range.cpu_addr + range.size - 1,
			       range.pci_addr);

			/* We support only one IO range */
			if (hose->pci_io_size) {
				printk(KERN_INFO
				       " \\--> Skipped (too many) !\n");
				continue;
			}
#ifdef CONFIG_PPC32
			/* On 32 bits, limit I/O space to 16MB */
			if (range.size > 0x01000000)
				range.size = 0x01000000;

			/* 32 bits needs to map IOs here */
			hose->io_base_virt = ioremap(range.cpu_addr,
						range.size);

			/* Expect trouble if pci_addr is not 0 */
			if (primary)
				isa_io_base =
					(unsigned long)hose->io_base_virt;
#endif /* CONFIG_PPC32 */
			/* pci_io_size and io_base_phys always represent IO
			 * space starting at 0 so we factor in pci_addr
			 */
			hose->pci_io_size = range.pci_addr + range.size;
			hose->io_base_phys = range.cpu_addr - range.pci_addr;

			/* Build resource */
			res = &hose->io_resource;
			range.cpu_addr = range.pci_addr;
			break;
		case IORESOURCE_MEM:
			printk(KERN_INFO
			       " MEM 0x%016llx..0x%016llx -> 0x%016llx %s\n",
			       range.cpu_addr, range.cpu_addr + range.size - 1,
			       range.pci_addr,
			       (range.flags & IORESOURCE_PREFETCH) ?
			       "Prefetch" : "");

			/* We support only 3 memory ranges */
			if (memno >= 3) {
				printk(KERN_INFO
				       " \\--> Skipped (too many) !\n");
				continue;
			}
			/* Handles ISA memory hole space here */
			if (range.pci_addr == 0) {
				if (primary || isa_mem_base == 0)
					isa_mem_base = range.cpu_addr;
				hose->isa_mem_phys = range.cpu_addr;
				hose->isa_mem_size = range.size;
			}

			/* Build resource */
			hose->mem_offset[memno] = range.cpu_addr -
							range.pci_addr;
			res = &hose->mem_resources[memno++];
			break;
		}
		if (res != NULL) {
			res->name = dev->full_name;
			res->flags = range.flags;
			res->start = range.cpu_addr;
			res->end = range.cpu_addr + range.size - 1;
			res->parent = res->child = res->sibling = NULL;
		}
	}
}

/* Decide whether to display the domain number in /proc */
int pci_proc_domain(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);

	if (!pci_has_flag(PCI_ENABLE_PROC_DOMAINS))
		return 0;
	if (pci_has_flag(PCI_COMPAT_DOMAIN_0))
		return hose->global_number != 0;
	return 1;
}

int pcibios_root_bridge_prepare(struct pci_host_bridge *bridge)
{
	if (ppc_md.pcibios_root_bridge_prepare)
		return ppc_md.pcibios_root_bridge_prepare(bridge);

	return 0;
}

/* This header fixup will do the resource fixup for all devices as they are
 * probed, but not for bridge ranges
 */
static void pcibios_fixup_resources(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	int i;

	if (!hose) {
		printk(KERN_ERR "No host bridge for PCI dev %s !\n",
		       pci_name(dev));
		return;
	}

	if (dev->is_virtfn)
		return;

	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		struct resource *res = dev->resource + i;
		struct pci_bus_region reg;
		if (!res->flags)
			continue;

		/* If we're going to re-assign everything, we mark all resources
		 * as unset (and 0-base them). In addition, we mark BARs starting
		 * at 0 as unset as well, except if PCI_PROBE_ONLY is also set
		 * since in that case, we don't want to re-assign anything
		 */
		pcibios_resource_to_bus(dev->bus, &reg, res);
		if (pci_has_flag(PCI_REASSIGN_ALL_RSRC) ||
		    (reg.start == 0 && !pci_has_flag(PCI_PROBE_ONLY))) {
			/* Only print message if not re-assigning */
			if (!pci_has_flag(PCI_REASSIGN_ALL_RSRC))
				pr_debug("PCI:%s Resource %d %pR is unassigned\n",
					 pci_name(dev), i, res);
			res->end -= res->start;
			res->start = 0;
			res->flags |= IORESOURCE_UNSET;
			continue;
		}

		pr_debug("PCI:%s Resource %d %pR\n", pci_name(dev), i, res);
	}

	/* Call machine specific resource fixup */
	if (ppc_md.pcibios_fixup_resources)
		ppc_md.pcibios_fixup_resources(dev);
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pcibios_fixup_resources);

/* This function tries to figure out if a bridge resource has been initialized
 * by the firmware or not. It doesn't have to be absolutely bullet proof, but
 * things go more smoothly when it gets it right. It should covers cases such
 * as Apple "closed" bridge resources and bare-metal pSeries unassigned bridges
 */
static int pcibios_uninitialized_bridge_resource(struct pci_bus *bus,
						 struct resource *res)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pci_dev *dev = bus->self;
	resource_size_t offset;
	struct pci_bus_region region;
	u16 command;
	int i;

	/* We don't do anything if PCI_PROBE_ONLY is set */
	if (pci_has_flag(PCI_PROBE_ONLY))
		return 0;

	/* Job is a bit different between memory and IO */
	if (res->flags & IORESOURCE_MEM) {
		pcibios_resource_to_bus(dev->bus, &region, res);

		/* If the BAR is non-0 then it's probably been initialized */
		if (region.start != 0)
			return 0;

		/* The BAR is 0, let's check if memory decoding is enabled on
		 * the bridge. If not, we consider it unassigned
		 */
		pci_read_config_word(dev, PCI_COMMAND, &command);
		if ((command & PCI_COMMAND_MEMORY) == 0)
			return 1;

		/* Memory decoding is enabled and the BAR is 0. If any of the bridge
		 * resources covers that starting address (0 then it's good enough for
		 * us for memory space)
		 */
		for (i = 0; i < 3; i++) {
			if ((hose->mem_resources[i].flags & IORESOURCE_MEM) &&
			    hose->mem_resources[i].start == hose->mem_offset[i])
				return 0;
		}

		/* Well, it starts at 0 and we know it will collide so we may as
		 * well consider it as unassigned. That covers the Apple case.
		 */
		return 1;
	} else {
		/* If the BAR is non-0, then we consider it assigned */
		offset = (unsigned long)hose->io_base_virt - _IO_BASE;
		if (((res->start - offset) & 0xfffffffful) != 0)
			return 0;

		/* Here, we are a bit different than memory as typically IO space
		 * starting at low addresses -is- valid. What we do instead if that
		 * we consider as unassigned anything that doesn't have IO enabled
		 * in the PCI command register, and that's it.
		 */
		pci_read_config_word(dev, PCI_COMMAND, &command);
		if (command & PCI_COMMAND_IO)
			return 0;

		/* It's starting at 0 and IO is disabled in the bridge, consider
		 * it unassigned
		 */
		return 1;
	}
}

/* Fixup resources of a PCI<->PCI bridge */
static void pcibios_fixup_bridge(struct pci_bus *bus)
{
	struct resource *res;
	int i;

	struct pci_dev *dev = bus->self;

	pci_bus_for_each_resource(bus, res, i) {
		if (!res || !res->flags)
			continue;
		if (i >= 3 && bus->self->transparent)
			continue;

		/* If we're going to reassign everything, we can
		 * shrink the P2P resource to have size as being
		 * of 0 in order to save space.
		 */
		if (pci_has_flag(PCI_REASSIGN_ALL_RSRC)) {
			res->flags |= IORESOURCE_UNSET;
			res->start = 0;
			res->end = -1;
			continue;
		}

		pr_debug("PCI:%s Bus rsrc %d %pR\n", pci_name(dev), i, res);

		/* Try to detect uninitialized P2P bridge resources,
		 * and clear them out so they get re-assigned later
		 */
		if (pcibios_uninitialized_bridge_resource(bus, res)) {
			res->flags = 0;
			pr_debug("PCI:%s            (unassigned)\n", pci_name(dev));
		}
	}
}

void pcibios_setup_bus_self(struct pci_bus *bus)
{
	struct pci_controller *phb;

	/* Fix up the bus resources for P2P bridges */
	if (bus->self != NULL)
		pcibios_fixup_bridge(bus);

	/* Platform specific bus fixups. This is currently only used
	 * by fsl_pci and I'm hoping to get rid of it at some point
	 */
	if (ppc_md.pcibios_fixup_bus)
		ppc_md.pcibios_fixup_bus(bus);

	/* Setup bus DMA mappings */
	phb = pci_bus_to_host(bus);
	if (phb->controller_ops.dma_bus_setup)
		phb->controller_ops.dma_bus_setup(bus);
}

void pcibios_bus_add_device(struct pci_dev *dev)
{
	struct pci_controller *phb;
	/* Fixup NUMA node as it may not be setup yet by the generic
	 * code and is needed by the DMA init
	 */
	set_dev_node(&dev->dev, pcibus_to_node(dev->bus));

	/* Hook up default DMA ops */
	set_dma_ops(&dev->dev, pci_dma_ops);
	dev->dev.archdata.dma_offset = PCI_DRAM_OFFSET;

	/* Additional platform DMA/iommu setup */
	phb = pci_bus_to_host(dev->bus);
	if (phb->controller_ops.dma_dev_setup)
		phb->controller_ops.dma_dev_setup(dev);

	/* Read default IRQs and fixup if necessary */
	pci_read_irq_line(dev);
	if (ppc_md.pci_irq_fixup)
		ppc_md.pci_irq_fixup(dev);

	if (ppc_md.pcibios_bus_add_device)
		ppc_md.pcibios_bus_add_device(dev);
}

int pcibios_add_device(struct pci_dev *dev)
{
#ifdef CONFIG_PCI_IOV
	if (ppc_md.pcibios_fixup_sriov)
		ppc_md.pcibios_fixup_sriov(dev);
#endif /* CONFIG_PCI_IOV */

	return 0;
}

void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

void pcibios_fixup_bus(struct pci_bus *bus)
{
	/* When called from the generic PCI probe, read PCI<->PCI bridge
	 * bases. This is -not- called when generating the PCI tree from
	 * the OF device-tree.
	 */
	pci_read_bridge_bases(bus);

	/* Now fixup the bus bus */
	pcibios_setup_bus_self(bus);
}
EXPORT_SYMBOL(pcibios_fixup_bus);

static int skip_isa_ioresource_align(struct pci_dev *dev)
{
	if (pci_has_flag(PCI_CAN_SKIP_ISA_ALIGN) &&
	    !(dev->bus->bridge_ctl & PCI_BRIDGE_CTL_ISA))
		return 1;
	return 0;
}

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
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	struct pci_dev *dev = data;
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		if (skip_isa_ioresource_align(dev))
			return start;
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	}

	return start;
}
EXPORT_SYMBOL(pcibios_align_resource);

/*
 * Reparent resource children of pr that conflict with res
 * under res, and make res replace those children.
 */
static int reparent_resources(struct resource *parent,
				     struct resource *res)
{
	struct resource *p, **pp;
	struct resource **firstpp = NULL;

	for (pp = &parent->child; (p = *pp) != NULL; pp = &p->sibling) {
		if (p->end < res->start)
			continue;
		if (res->end < p->start)
			break;
		if (p->start < res->start || p->end > res->end)
			return -1;	/* not completely contained */
		if (firstpp == NULL)
			firstpp = pp;
	}
	if (firstpp == NULL)
		return -1;	/* didn't find any conflicting entries? */
	res->parent = parent;
	res->child = *firstpp;
	res->sibling = *pp;
	*firstpp = res;
	*pp = NULL;
	for (p = res->child; p != NULL; p = p->sibling) {
		p->parent = res;
		pr_debug("PCI: Reparented %s %pR under %s\n",
			 p->name, p, res->name);
	}
	return 0;
}

/*
 *  Handle resources of PCI devices.  If the world were perfect, we could
 *  just allocate all the resource regions and do nothing more.  It isn't.
 *  On the other hand, we cannot just re-allocate all devices, as it would
 *  require us to know lots of host bridge internals.  So we attempt to
 *  keep as much of the original configuration as possible, but tweak it
 *  when it's found to be wrong.
 *
 *  Known BIOS problems we have to work around:
 *	- I/O or memory regions not configured
 *	- regions configured, but not enabled in the command register
 *	- bogus I/O addresses above 64K used
 *	- expansion ROMs left enabled (this may sound harmless, but given
 *	  the fact the PCI specs explicitly allow address decoders to be
 *	  shared between expansion ROMs and other resource regions, it's
 *	  at least dangerous)
 *
 *  Our solution:
 *	(1) Allocate resources for all buses behind PCI-to-PCI bridges.
 *	    This gives us fixed barriers on where we can allocate.
 *	(2) Allocate resources for all enabled devices.  If there is
 *	    a collision, just mark the resource as unallocated. Also
 *	    disable expansion ROMs during this step.
 *	(3) Try to allocate resources for disabled devices.  If the
 *	    resources were assigned correctly, everything goes well,
 *	    if they weren't, they won't disturb allocation of other
 *	    resources.
 *	(4) Assign new addresses to resources which were either
 *	    not configured at all or misconfigured.  If explicitly
 *	    requested by the user, configure expansion ROM address
 *	    as well.
 */

static void pcibios_allocate_bus_resources(struct pci_bus *bus)
{
	struct pci_bus *b;
	int i;
	struct resource *res, *pr;

	pr_debug("PCI: Allocating bus resources for %04x:%02x...\n",
		 pci_domain_nr(bus), bus->number);

	pci_bus_for_each_resource(bus, res, i) {
		if (!res || !res->flags || res->start > res->end || res->parent)
			continue;

		/* If the resource was left unset at this point, we clear it */
		if (res->flags & IORESOURCE_UNSET)
			goto clear_resource;

		if (bus->parent == NULL)
			pr = (res->flags & IORESOURCE_IO) ?
				&ioport_resource : &iomem_resource;
		else {
			pr = pci_find_parent_resource(bus->self, res);
			if (pr == res) {
				/* this happens when the generic PCI
				 * code (wrongly) decides that this
				 * bridge is transparent  -- paulus
				 */
				continue;
			}
		}

		pr_debug("PCI: %s (bus %d) bridge rsrc %d: %pR, parent %p (%s)\n",
			 bus->self ? pci_name(bus->self) : "PHB", bus->number,
			 i, res, pr, (pr && pr->name) ? pr->name : "nil");

		if (pr && !(pr->flags & IORESOURCE_UNSET)) {
			struct pci_dev *dev = bus->self;

			if (request_resource(pr, res) == 0)
				continue;
			/*
			 * Must be a conflict with an existing entry.
			 * Move that entry (or entries) under the
			 * bridge resource and try again.
			 */
			if (reparent_resources(pr, res) == 0)
				continue;

			if (dev && i < PCI_BRIDGE_RESOURCE_NUM &&
			    pci_claim_bridge_resource(dev,
						i + PCI_BRIDGE_RESOURCES) == 0)
				continue;
		}
		pr_warn("PCI: Cannot allocate resource region %d of PCI bridge %d, will remap\n",
			i, bus->number);
	clear_resource:
		/* The resource might be figured out when doing
		 * reassignment based on the resources required
		 * by the downstream PCI devices. Here we set
		 * the size of the resource to be 0 in order to
		 * save more space.
		 */
		res->start = 0;
		res->end = -1;
		res->flags = 0;
	}

	list_for_each_entry(b, &bus->children, node)
		pcibios_allocate_bus_resources(b);
}

static inline void alloc_resource(struct pci_dev *dev, int idx)
{
	struct resource *pr, *r = &dev->resource[idx];

	pr_debug("PCI: Allocating %s: Resource %d: %pR\n",
		 pci_name(dev), idx, r);

	pr = pci_find_parent_resource(dev, r);
	if (!pr || (pr->flags & IORESOURCE_UNSET) ||
	    request_resource(pr, r) < 0) {
		printk(KERN_WARNING "PCI: Cannot allocate resource region %d"
		       " of device %s, will remap\n", idx, pci_name(dev));
		if (pr)
			pr_debug("PCI:  parent is %p: %pR\n", pr, pr);
		/* We'll assign a new address later */
		r->flags |= IORESOURCE_UNSET;
		r->end -= r->start;
		r->start = 0;
	}
}

static void __init pcibios_allocate_resources(int pass)
{
	struct pci_dev *dev = NULL;
	int idx, disabled;
	u16 command;
	struct resource *r;

	for_each_pci_dev(dev) {
		pci_read_config_word(dev, PCI_COMMAND, &command);
		for (idx = 0; idx <= PCI_ROM_RESOURCE; idx++) {
			r = &dev->resource[idx];
			if (r->parent)		/* Already allocated */
				continue;
			if (!r->flags || (r->flags & IORESOURCE_UNSET))
				continue;	/* Not assigned at all */
			/* We only allocate ROMs on pass 1 just in case they
			 * have been screwed up by firmware
			 */
			if (idx == PCI_ROM_RESOURCE )
				disabled = 1;
			if (r->flags & IORESOURCE_IO)
				disabled = !(command & PCI_COMMAND_IO);
			else
				disabled = !(command & PCI_COMMAND_MEMORY);
			if (pass == disabled)
				alloc_resource(dev, idx);
		}
		if (pass)
			continue;
		r = &dev->resource[PCI_ROM_RESOURCE];
		if (r->flags) {
			/* Turn the ROM off, leave the resource region,
			 * but keep it unregistered.
			 */
			u32 reg;
			pci_read_config_dword(dev, dev->rom_base_reg, &reg);
			if (reg & PCI_ROM_ADDRESS_ENABLE) {
				pr_debug("PCI: Switching off ROM of %s\n",
					 pci_name(dev));
				r->flags &= ~IORESOURCE_ROM_ENABLE;
				pci_write_config_dword(dev, dev->rom_base_reg,
						       reg & ~PCI_ROM_ADDRESS_ENABLE);
			}
		}
	}
}

static void __init pcibios_reserve_legacy_regions(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	resource_size_t	offset;
	struct resource *res, *pres;
	int i;

	pr_debug("Reserving legacy ranges for domain %04x\n", pci_domain_nr(bus));

	/* Check for IO */
	if (!(hose->io_resource.flags & IORESOURCE_IO))
		goto no_io;
	offset = (unsigned long)hose->io_base_virt - _IO_BASE;
	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	BUG_ON(res == NULL);
	res->name = "Legacy IO";
	res->flags = IORESOURCE_IO;
	res->start = offset;
	res->end = (offset + 0xfff) & 0xfffffffful;
	pr_debug("Candidate legacy IO: %pR\n", res);
	if (request_resource(&hose->io_resource, res)) {
		printk(KERN_DEBUG
		       "PCI %04x:%02x Cannot reserve Legacy IO %pR\n",
		       pci_domain_nr(bus), bus->number, res);
		kfree(res);
	}

 no_io:
	/* Check for memory */
	for (i = 0; i < 3; i++) {
		pres = &hose->mem_resources[i];
		offset = hose->mem_offset[i];
		if (!(pres->flags & IORESOURCE_MEM))
			continue;
		pr_debug("hose mem res: %pR\n", pres);
		if ((pres->start - offset) <= 0xa0000 &&
		    (pres->end - offset) >= 0xbffff)
			break;
	}
	if (i >= 3)
		return;
	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	BUG_ON(res == NULL);
	res->name = "Legacy VGA memory";
	res->flags = IORESOURCE_MEM;
	res->start = 0xa0000 + offset;
	res->end = 0xbffff + offset;
	pr_debug("Candidate VGA memory: %pR\n", res);
	if (request_resource(pres, res)) {
		printk(KERN_DEBUG
		       "PCI %04x:%02x Cannot reserve VGA memory %pR\n",
		       pci_domain_nr(bus), bus->number, res);
		kfree(res);
	}
}

void __init pcibios_resource_survey(void)
{
	struct pci_bus *b;

	/* Allocate and assign resources */
	list_for_each_entry(b, &pci_root_buses, node)
		pcibios_allocate_bus_resources(b);
	if (!pci_has_flag(PCI_REASSIGN_ALL_RSRC)) {
		pcibios_allocate_resources(0);
		pcibios_allocate_resources(1);
	}

	/* Before we start assigning unassigned resource, we try to reserve
	 * the low IO area and the VGA memory area if they intersect the
	 * bus available resources to avoid allocating things on top of them
	 */
	if (!pci_has_flag(PCI_PROBE_ONLY)) {
		list_for_each_entry(b, &pci_root_buses, node)
			pcibios_reserve_legacy_regions(b);
	}

	/* Now, if the platform didn't decide to blindly trust the firmware,
	 * we proceed to assigning things that were left unassigned
	 */
	if (!pci_has_flag(PCI_PROBE_ONLY)) {
		pr_debug("PCI: Assigning unassigned resources...\n");
		pci_assign_unassigned_resources();
	}
}

/* This is used by the PCI hotplug driver to allocate resource
 * of newly plugged busses. We can try to consolidate with the
 * rest of the code later, for now, keep it as-is as our main
 * resource allocation function doesn't deal with sub-trees yet.
 */
void pcibios_claim_one_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct pci_bus *child_bus;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];

			if (r->parent || !r->start || !r->flags)
				continue;

			pr_debug("PCI: Claiming %s: Resource %d: %pR\n",
				 pci_name(dev), i, r);

			if (pci_claim_resource(dev, i) == 0)
				continue;

			pci_claim_bridge_resource(dev, i);
		}
	}

	list_for_each_entry(child_bus, &bus->children, node)
		pcibios_claim_one_bus(child_bus);
}
EXPORT_SYMBOL_GPL(pcibios_claim_one_bus);


/* pcibios_finish_adding_to_bus
 *
 * This is to be called by the hotplug code after devices have been
 * added to a bus, this include calling it for a PHB that is just
 * being added
 */
void pcibios_finish_adding_to_bus(struct pci_bus *bus)
{
	pr_debug("PCI: Finishing adding to hotplug bus %04x:%02x\n",
		 pci_domain_nr(bus), bus->number);

	/* Allocate bus and devices resources */
	pcibios_allocate_bus_resources(bus);
	pcibios_claim_one_bus(bus);
	if (!pci_has_flag(PCI_PROBE_ONLY)) {
		if (bus->self)
			pci_assign_unassigned_bridge_resources(bus->self);
		else
			pci_assign_unassigned_bus_resources(bus);
	}

	/* Add new devices to global lists.  Register in proc, sysfs. */
	pci_bus_add_devices(bus);
}
EXPORT_SYMBOL_GPL(pcibios_finish_adding_to_bus);

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	struct pci_controller *phb = pci_bus_to_host(dev->bus);

	if (phb->controller_ops.enable_device_hook)
		if (!phb->controller_ops.enable_device_hook(dev))
			return -EINVAL;

	return pci_enable_resources(dev, mask);
}

void pcibios_disable_device(struct pci_dev *dev)
{
	struct pci_controller *phb = pci_bus_to_host(dev->bus);

	if (phb->controller_ops.disable_device)
		phb->controller_ops.disable_device(dev);
}

resource_size_t pcibios_io_space_offset(struct pci_controller *hose)
{
	return (unsigned long) hose->io_base_virt - _IO_BASE;
}

static void pcibios_setup_phb_resources(struct pci_controller *hose,
					struct list_head *resources)
{
	struct resource *res;
	resource_size_t offset;
	int i;

	/* Hookup PHB IO resource */
	res = &hose->io_resource;

	if (!res->flags) {
		pr_debug("PCI: I/O resource not set for host"
			 " bridge %pOF (domain %d)\n",
			 hose->dn, hose->global_number);
	} else {
		offset = pcibios_io_space_offset(hose);

		pr_debug("PCI: PHB IO resource    = %pR off 0x%08llx\n",
			 res, (unsigned long long)offset);
		pci_add_resource_offset(resources, res, offset);
	}

	/* Hookup PHB Memory resources */
	for (i = 0; i < 3; ++i) {
		res = &hose->mem_resources[i];
		if (!res->flags)
			continue;

		offset = hose->mem_offset[i];
		pr_debug("PCI: PHB MEM resource %d = %pR off 0x%08llx\n", i,
			 res, (unsigned long long)offset);

		pci_add_resource_offset(resources, res, offset);
	}
}

/*
 * Null PCI config access functions, for the case when we can't
 * find a hose.
 */
#define NULL_PCI_OP(rw, size, type)					\
static int								\
null_##rw##_config_##size(struct pci_dev *dev, int offset, type val)	\
{									\
	return PCIBIOS_DEVICE_NOT_FOUND;    				\
}

static int
null_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		 int len, u32 *val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int
null_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		  int len, u32 val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static struct pci_ops null_pci_ops =
{
	.read = null_read_config,
	.write = null_write_config,
};

/*
 * These functions are used early on before PCI scanning is done
 * and all of the pci_dev and pci_bus structures have been created.
 */
static struct pci_bus *
fake_pci_bus(struct pci_controller *hose, int busnr)
{
	static struct pci_bus bus;

	if (hose == NULL) {
		printk(KERN_ERR "Can't find hose for PCI bus %d!\n", busnr);
	}
	bus.number = busnr;
	bus.sysdata = hose;
	bus.ops = hose? hose->ops: &null_pci_ops;
	return &bus;
}

#define EARLY_PCI_OP(rw, size, type)					\
int early_##rw##_config_##size(struct pci_controller *hose, int bus,	\
			       int devfn, int offset, type value)	\
{									\
	return pci_bus_##rw##_config_##size(fake_pci_bus(hose, bus),	\
					    devfn, offset, value);	\
}

EARLY_PCI_OP(read, byte, u8 *)
EARLY_PCI_OP(read, word, u16 *)
EARLY_PCI_OP(read, dword, u32 *)
EARLY_PCI_OP(write, byte, u8)
EARLY_PCI_OP(write, word, u16)
EARLY_PCI_OP(write, dword, u32)

int early_find_capability(struct pci_controller *hose, int bus, int devfn,
			  int cap)
{
	return pci_bus_find_capability(fake_pci_bus(hose, bus), devfn, cap);
}

struct device_node *pcibios_get_phb_of_node(struct pci_bus *bus)
{
	struct pci_controller *hose = bus->sysdata;

	return of_node_get(hose->dn);
}

/**
 * pci_scan_phb - Given a pci_controller, setup and scan the PCI bus
 * @hose: Pointer to the PCI host controller instance structure
 */
void pcibios_scan_phb(struct pci_controller *hose)
{
	LIST_HEAD(resources);
	struct pci_bus *bus;
	struct device_node *node = hose->dn;
	int mode;

	pr_debug("PCI: Scanning PHB %pOF\n", node);

	/* Get some IO space for the new PHB */
	pcibios_setup_phb_io_space(hose);

	/* Wire up PHB bus resources */
	pcibios_setup_phb_resources(hose, &resources);

	hose->busn.start = hose->first_busno;
	hose->busn.end	 = hose->last_busno;
	hose->busn.flags = IORESOURCE_BUS;
	pci_add_resource(&resources, &hose->busn);

	/* Create an empty bus for the toplevel */
	bus = pci_create_root_bus(hose->parent, hose->first_busno,
				  hose->ops, hose, &resources);
	if (bus == NULL) {
		pr_err("Failed to create bus for PCI domain %04x\n",
			hose->global_number);
		pci_free_resource_list(&resources);
		return;
	}
	hose->bus = bus;

	/* Get probe mode and perform scan */
	mode = PCI_PROBE_NORMAL;
	if (node && hose->controller_ops.probe_mode)
		mode = hose->controller_ops.probe_mode(bus);
	pr_debug("    probe mode: %d\n", mode);
	if (mode == PCI_PROBE_DEVTREE)
		of_scan_bus(node, bus);

	if (mode == PCI_PROBE_NORMAL) {
		pci_bus_update_busn_res_end(bus, 255);
		hose->last_busno = pci_scan_child_bus(bus);
		pci_bus_update_busn_res_end(bus, hose->last_busno);
	}

	/* Platform gets a chance to do some global fixups before
	 * we proceed to resource allocation
	 */
	if (ppc_md.pcibios_fixup_phb)
		ppc_md.pcibios_fixup_phb(hose);

	/* Configure PCI Express settings */
	if (bus && !pci_has_flag(PCI_PROBE_ONLY)) {
		struct pci_bus *child;
		list_for_each_entry(child, &bus->children, node)
			pcie_bus_configure_settings(child);
	}
}
EXPORT_SYMBOL_GPL(pcibios_scan_phb);

static void fixup_hide_host_resource_fsl(struct pci_dev *dev)
{
	int i, class = dev->class >> 8;
	/* When configured as agent, programing interface = 1 */
	int prog_if = dev->class & 0xf;

	if ((class == PCI_CLASS_PROCESSOR_POWERPC ||
	     class == PCI_CLASS_BRIDGE_OTHER) &&
		(dev->hdr_type == PCI_HEADER_TYPE_NORMAL) &&
		(prog_if == 0) &&
		(dev->bus->parent == NULL)) {
		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			dev->resource[i].start = 0;
			dev->resource[i].end = 0;
			dev->resource[i].flags = 0;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MOTOROLA, PCI_ANY_ID, fixup_hide_host_resource_fsl);
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_FREESCALE, PCI_ANY_ID, fixup_hide_host_resource_fsl);


static int __init discover_phbs(void)
{
	if (ppc_md.discover_phbs)
		ppc_md.discover_phbs();

	return 0;
}
core_initcall(discover_phbs);
