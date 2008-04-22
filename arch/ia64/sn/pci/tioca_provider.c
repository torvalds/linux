/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003-2005 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/addrs.h>
#include <asm/sn/io.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/tioca_provider.h>

u32 tioca_gart_found;
EXPORT_SYMBOL(tioca_gart_found);	/* used by agp-sgi */

LIST_HEAD(tioca_list);
EXPORT_SYMBOL(tioca_list);	/* used by agp-sgi */

static int tioca_gart_init(struct tioca_kernel *);

/**
 * tioca_gart_init - Initialize SGI TIOCA GART
 * @tioca_common: ptr to common prom/kernel struct identifying the 
 *
 * If the indicated tioca has devices present, initialize its associated
 * GART MMR's and kernel memory.
 */
static int
tioca_gart_init(struct tioca_kernel *tioca_kern)
{
	u64 ap_reg;
	u64 offset;
	struct page *tmp;
	struct tioca_common *tioca_common;
	struct tioca __iomem *ca_base;

	tioca_common = tioca_kern->ca_common;
	ca_base = (struct tioca __iomem *)tioca_common->ca_common.bs_base;

	if (list_empty(tioca_kern->ca_devices))
		return 0;

	ap_reg = 0;

	/*
	 * Validate aperature size
	 */

	switch (CA_APERATURE_SIZE >> 20) {
	case 4:
		ap_reg |= (0x3ff << CA_GART_AP_SIZE_SHFT);	/* 4MB */
		break;
	case 8:
		ap_reg |= (0x3fe << CA_GART_AP_SIZE_SHFT);	/* 8MB */
		break;
	case 16:
		ap_reg |= (0x3fc << CA_GART_AP_SIZE_SHFT);	/* 16MB */
		break;
	case 32:
		ap_reg |= (0x3f8 << CA_GART_AP_SIZE_SHFT);	/* 32 MB */
		break;
	case 64:
		ap_reg |= (0x3f0 << CA_GART_AP_SIZE_SHFT);	/* 64 MB */
		break;
	case 128:
		ap_reg |= (0x3e0 << CA_GART_AP_SIZE_SHFT);	/* 128 MB */
		break;
	case 256:
		ap_reg |= (0x3c0 << CA_GART_AP_SIZE_SHFT);	/* 256 MB */
		break;
	case 512:
		ap_reg |= (0x380 << CA_GART_AP_SIZE_SHFT);	/* 512 MB */
		break;
	case 1024:
		ap_reg |= (0x300 << CA_GART_AP_SIZE_SHFT);	/* 1GB */
		break;
	case 2048:
		ap_reg |= (0x200 << CA_GART_AP_SIZE_SHFT);	/* 2GB */
		break;
	case 4096:
		ap_reg |= (0x000 << CA_GART_AP_SIZE_SHFT);	/* 4 GB */
		break;
	default:
		printk(KERN_ERR "%s:  Invalid CA_APERATURE_SIZE "
		       "0x%lx\n", __func__, (ulong) CA_APERATURE_SIZE);
		return -1;
	}

	/*
	 * Set up other aperature parameters
	 */

	if (PAGE_SIZE >= 16384) {
		tioca_kern->ca_ap_pagesize = 16384;
		ap_reg |= CA_GART_PAGE_SIZE;
	} else {
		tioca_kern->ca_ap_pagesize = 4096;
	}

	tioca_kern->ca_ap_size = CA_APERATURE_SIZE;
	tioca_kern->ca_ap_bus_base = CA_APERATURE_BASE;
	tioca_kern->ca_gart_entries =
	    tioca_kern->ca_ap_size / tioca_kern->ca_ap_pagesize;

	ap_reg |= (CA_GART_AP_ENB_AGP | CA_GART_AP_ENB_PCI);
	ap_reg |= tioca_kern->ca_ap_bus_base;

	/*
	 * Allocate and set up the GART
	 */

	tioca_kern->ca_gart_size = tioca_kern->ca_gart_entries * sizeof(u64);
	tmp =
	    alloc_pages_node(tioca_kern->ca_closest_node,
			     GFP_KERNEL | __GFP_ZERO,
			     get_order(tioca_kern->ca_gart_size));

	if (!tmp) {
		printk(KERN_ERR "%s:  Could not allocate "
		       "%lu bytes (order %d) for GART\n",
		       __func__,
		       tioca_kern->ca_gart_size,
		       get_order(tioca_kern->ca_gart_size));
		return -ENOMEM;
	}

	tioca_kern->ca_gart = page_address(tmp);
	tioca_kern->ca_gart_coretalk_addr =
	    PHYS_TO_TIODMA(virt_to_phys(tioca_kern->ca_gart));

	/*
	 * Compute PCI/AGP convenience fields 
	 */

	offset = CA_PCI32_MAPPED_BASE - CA_APERATURE_BASE;
	tioca_kern->ca_pciap_base = CA_PCI32_MAPPED_BASE;
	tioca_kern->ca_pciap_size = CA_PCI32_MAPPED_SIZE;
	tioca_kern->ca_pcigart_start = offset / tioca_kern->ca_ap_pagesize;
	tioca_kern->ca_pcigart_base =
	    tioca_kern->ca_gart_coretalk_addr + offset;
	tioca_kern->ca_pcigart =
	    &tioca_kern->ca_gart[tioca_kern->ca_pcigart_start];
	tioca_kern->ca_pcigart_entries =
	    tioca_kern->ca_pciap_size / tioca_kern->ca_ap_pagesize;
	tioca_kern->ca_pcigart_pagemap =
	    kzalloc(tioca_kern->ca_pcigart_entries / 8, GFP_KERNEL);
	if (!tioca_kern->ca_pcigart_pagemap) {
		free_pages((unsigned long)tioca_kern->ca_gart,
			   get_order(tioca_kern->ca_gart_size));
		return -1;
	}

	offset = CA_AGP_MAPPED_BASE - CA_APERATURE_BASE;
	tioca_kern->ca_gfxap_base = CA_AGP_MAPPED_BASE;
	tioca_kern->ca_gfxap_size = CA_AGP_MAPPED_SIZE;
	tioca_kern->ca_gfxgart_start = offset / tioca_kern->ca_ap_pagesize;
	tioca_kern->ca_gfxgart_base =
	    tioca_kern->ca_gart_coretalk_addr + offset;
	tioca_kern->ca_gfxgart =
	    &tioca_kern->ca_gart[tioca_kern->ca_gfxgart_start];
	tioca_kern->ca_gfxgart_entries =
	    tioca_kern->ca_gfxap_size / tioca_kern->ca_ap_pagesize;

	/*
	 * various control settings:
	 *      use agp op-combining
	 *      use GET semantics to fetch memory
	 *      participate in coherency domain
	 * 	DISABLE GART PREFETCHING due to hw bug tracked in SGI PV930029
	 */

	__sn_setq_relaxed(&ca_base->ca_control1,
			CA_AGPDMA_OP_ENB_COMBDELAY);	/* PV895469 ? */
	__sn_clrq_relaxed(&ca_base->ca_control2, CA_GART_MEM_PARAM);
	__sn_setq_relaxed(&ca_base->ca_control2,
			(0x2ull << CA_GART_MEM_PARAM_SHFT));
	tioca_kern->ca_gart_iscoherent = 1;
	__sn_clrq_relaxed(&ca_base->ca_control2,
	    		(CA_GART_WR_PREFETCH_ENB | CA_GART_RD_PREFETCH_ENB));

	/*
	 * Unmask GART fetch error interrupts.  Clear residual errors first.
	 */

	writeq(CA_GART_FETCH_ERR, &ca_base->ca_int_status_alias);
	writeq(CA_GART_FETCH_ERR, &ca_base->ca_mult_error_alias);
	__sn_clrq_relaxed(&ca_base->ca_int_mask, CA_GART_FETCH_ERR);

	/*
	 * Program the aperature and gart registers in TIOCA
	 */

	writeq(ap_reg, &ca_base->ca_gart_aperature);
	writeq(tioca_kern->ca_gart_coretalk_addr|1, &ca_base->ca_gart_ptr_table);

	return 0;
}

/**
 * tioca_fastwrite_enable - enable AGP FW for a tioca and its functions
 * @tioca_kernel: structure representing the CA
 *
 * Given a CA, scan all attached functions making sure they all support
 * FastWrite.  If so, enable FastWrite for all functions and the CA itself.
 */

void
tioca_fastwrite_enable(struct tioca_kernel *tioca_kern)
{
	int cap_ptr;
	u32 reg;
	struct tioca __iomem *tioca_base;
	struct pci_dev *pdev;
	struct tioca_common *common;

	common = tioca_kern->ca_common;

	/*
	 * Scan all vga controllers on this bus making sure they all
	 * support FW.  If not, return.
	 */

	list_for_each_entry(pdev, tioca_kern->ca_devices, bus_list) {
		if (pdev->class != (PCI_CLASS_DISPLAY_VGA << 8))
			continue;

		cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
		if (!cap_ptr)
			return;	/* no AGP CAP means no FW */

		pci_read_config_dword(pdev, cap_ptr + PCI_AGP_STATUS, &reg);
		if (!(reg & PCI_AGP_STATUS_FW))
			return;	/* function doesn't support FW */
	}

	/*
	 * Set fw for all vga fn's
	 */

	list_for_each_entry(pdev, tioca_kern->ca_devices, bus_list) {
		if (pdev->class != (PCI_CLASS_DISPLAY_VGA << 8))
			continue;

		cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
		pci_read_config_dword(pdev, cap_ptr + PCI_AGP_COMMAND, &reg);
		reg |= PCI_AGP_COMMAND_FW;
		pci_write_config_dword(pdev, cap_ptr + PCI_AGP_COMMAND, reg);
	}

	/*
	 * Set ca's fw to match
	 */

	tioca_base = (struct tioca __iomem*)common->ca_common.bs_base;
	__sn_setq_relaxed(&tioca_base->ca_control1, CA_AGP_FW_ENABLE);
}

EXPORT_SYMBOL(tioca_fastwrite_enable);	/* used by agp-sgi */

/**
 * tioca_dma_d64 - create a DMA mapping using 64-bit direct mode
 * @paddr: system physical address
 *
 * Map @paddr into 64-bit CA bus space.  No device context is necessary.
 * Bits 53:0 come from the coretalk address.  We just need to mask in the
 * following optional bits of the 64-bit pci address:
 *
 * 63:60 - Coretalk Packet Type -  0x1 for Mem Get/Put (coherent)
 *                                 0x2 for PIO (non-coherent)
 *                                 We will always use 0x1
 * 55:55 - Swap bytes		   Currently unused
 */
static u64
tioca_dma_d64(unsigned long paddr)
{
	dma_addr_t bus_addr;

	bus_addr = PHYS_TO_TIODMA(paddr);

	BUG_ON(!bus_addr);
	BUG_ON(bus_addr >> 54);

	/* Set upper nibble to Cache Coherent Memory op */
	bus_addr |= (1UL << 60);

	return bus_addr;
}

/**
 * tioca_dma_d48 - create a DMA mapping using 48-bit direct mode
 * @pdev: linux pci_dev representing the function
 * @paddr: system physical address
 *
 * Map @paddr into 64-bit bus space of the CA associated with @pcidev_info.
 *
 * The CA agp 48 bit direct address falls out as follows:
 *
 * When direct mapping AGP addresses, the 48 bit AGP address is
 * constructed as follows:
 *
 * [47:40] - Low 8 bits of the page Node ID extracted from coretalk
 *              address [47:40].  The upper 8 node bits are fixed
 *              and come from the xxx register bits [5:0]
 * [39:38] - Chiplet ID extracted from coretalk address [39:38]
 * [37:00] - node offset extracted from coretalk address [37:00]
 * 
 * Since the node id in general will be non-zero, and the chiplet id
 * will always be non-zero, it follows that the device must support
 * a dma mask of at least 0xffffffffff (40 bits) to target node 0
 * and in general should be 0xffffffffffff (48 bits) to target nodes
 * up to 255.  Nodes above 255 need the support of the xxx register,
 * and so a given CA can only directly target nodes in the range
 * xxx - xxx+255.
 */
static u64
tioca_dma_d48(struct pci_dev *pdev, u64 paddr)
{
	struct tioca_common *tioca_common;
	struct tioca __iomem *ca_base;
	u64 ct_addr;
	dma_addr_t bus_addr;
	u32 node_upper;
	u64 agp_dma_extn;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(pdev);

	tioca_common = (struct tioca_common *)pcidev_info->pdi_pcibus_info;
	ca_base = (struct tioca __iomem *)tioca_common->ca_common.bs_base;

	ct_addr = PHYS_TO_TIODMA(paddr);
	if (!ct_addr)
		return 0;

	bus_addr = (dma_addr_t) (ct_addr & 0xffffffffffffUL);
	node_upper = ct_addr >> 48;

	if (node_upper > 64) {
		printk(KERN_ERR "%s:  coretalk addr 0x%p node id out "
		       "of range\n", __func__, (void *)ct_addr);
		return 0;
	}

	agp_dma_extn = __sn_readq_relaxed(&ca_base->ca_agp_dma_addr_extn);
	if (node_upper != (agp_dma_extn >> CA_AGP_DMA_NODE_ID_SHFT)) {
		printk(KERN_ERR "%s:  coretalk upper node (%u) "
		       "mismatch with ca_agp_dma_addr_extn (%lu)\n",
		       __func__,
		       node_upper, (agp_dma_extn >> CA_AGP_DMA_NODE_ID_SHFT));
		return 0;
	}

	return bus_addr;
}

/**
 * tioca_dma_mapped - create a DMA mapping using a CA GART 
 * @pdev: linux pci_dev representing the function
 * @paddr: host physical address to map
 * @req_size: len (bytes) to map
 *
 * Map @paddr into CA address space using the GART mechanism.  The mapped
 * dma_addr_t is guaranteed to be contiguous in CA bus space.
 */
static dma_addr_t
tioca_dma_mapped(struct pci_dev *pdev, u64 paddr, size_t req_size)
{
	int i, ps, ps_shift, entry, entries, mapsize, last_entry;
	u64 xio_addr, end_xio_addr;
	struct tioca_common *tioca_common;
	struct tioca_kernel *tioca_kern;
	dma_addr_t bus_addr = 0;
	struct tioca_dmamap *ca_dmamap;
	void *map;
	unsigned long flags;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(pdev);

	tioca_common = (struct tioca_common *)pcidev_info->pdi_pcibus_info;
	tioca_kern = (struct tioca_kernel *)tioca_common->ca_kernel_private;

	xio_addr = PHYS_TO_TIODMA(paddr);
	if (!xio_addr)
		return 0;

	spin_lock_irqsave(&tioca_kern->ca_lock, flags);

	/*
	 * allocate a map struct
	 */

	ca_dmamap = kzalloc(sizeof(struct tioca_dmamap), GFP_ATOMIC);
	if (!ca_dmamap)
		goto map_return;

	/*
	 * Locate free entries that can hold req_size.  Account for
	 * unaligned start/length when allocating.
	 */

	ps = tioca_kern->ca_ap_pagesize;	/* will be power of 2 */
	ps_shift = ffs(ps) - 1;
	end_xio_addr = xio_addr + req_size - 1;

	entries = (end_xio_addr >> ps_shift) - (xio_addr >> ps_shift) + 1;

	map = tioca_kern->ca_pcigart_pagemap;
	mapsize = tioca_kern->ca_pcigart_entries;

	entry = find_first_zero_bit(map, mapsize);
	while (entry < mapsize) {
		last_entry = find_next_bit(map, mapsize, entry);

		if (last_entry - entry >= entries)
			break;

		entry = find_next_zero_bit(map, mapsize, last_entry);
	}

	if (entry > mapsize)
		goto map_return;

	for (i = 0; i < entries; i++)
		set_bit(entry + i, map);

	bus_addr = tioca_kern->ca_pciap_base + (entry * ps);

	ca_dmamap->cad_dma_addr = bus_addr;
	ca_dmamap->cad_gart_size = entries;
	ca_dmamap->cad_gart_entry = entry;
	list_add(&ca_dmamap->cad_list, &tioca_kern->ca_dmamaps);

	if (xio_addr % ps) {
		tioca_kern->ca_pcigart[entry] = tioca_paddr_to_gart(xio_addr);
		bus_addr += xio_addr & (ps - 1);
		xio_addr &= ~(ps - 1);
		xio_addr += ps;
		entry++;
	}

	while (xio_addr < end_xio_addr) {
		tioca_kern->ca_pcigart[entry] = tioca_paddr_to_gart(xio_addr);
		xio_addr += ps;
		entry++;
	}

	tioca_tlbflush(tioca_kern);

map_return:
	spin_unlock_irqrestore(&tioca_kern->ca_lock, flags);
	return bus_addr;
}

/**
 * tioca_dma_unmap - release CA mapping resources
 * @pdev: linux pci_dev representing the function
 * @bus_addr: bus address returned by an earlier tioca_dma_map
 * @dir: mapping direction (unused)
 *
 * Locate mapping resources associated with @bus_addr and release them.
 * For mappings created using the direct modes (64 or 48) there are no
 * resources to release.
 */
static void
tioca_dma_unmap(struct pci_dev *pdev, dma_addr_t bus_addr, int dir)
{
	int i, entry;
	struct tioca_common *tioca_common;
	struct tioca_kernel *tioca_kern;
	struct tioca_dmamap *map;
	struct pcidev_info *pcidev_info = SN_PCIDEV_INFO(pdev);
	unsigned long flags;

	tioca_common = (struct tioca_common *)pcidev_info->pdi_pcibus_info;
	tioca_kern = (struct tioca_kernel *)tioca_common->ca_kernel_private;

	/* return straight away if this isn't be a mapped address */

	if (bus_addr < tioca_kern->ca_pciap_base ||
	    bus_addr >= (tioca_kern->ca_pciap_base + tioca_kern->ca_pciap_size))
		return;

	spin_lock_irqsave(&tioca_kern->ca_lock, flags);

	list_for_each_entry(map, &tioca_kern->ca_dmamaps, cad_list)
	    if (map->cad_dma_addr == bus_addr)
		break;

	BUG_ON(map == NULL);

	entry = map->cad_gart_entry;

	for (i = 0; i < map->cad_gart_size; i++, entry++) {
		clear_bit(entry, tioca_kern->ca_pcigart_pagemap);
		tioca_kern->ca_pcigart[entry] = 0;
	}
	tioca_tlbflush(tioca_kern);

	list_del(&map->cad_list);
	spin_unlock_irqrestore(&tioca_kern->ca_lock, flags);
	kfree(map);
}

/**
 * tioca_dma_map - map pages for PCI DMA
 * @pdev: linux pci_dev representing the function
 * @paddr: host physical address to map
 * @byte_count: bytes to map
 *
 * This is the main wrapper for mapping host physical pages to CA PCI space.
 * The mapping mode used is based on the devices dma_mask.  As a last resort
 * use the GART mapped mode.
 */
static u64
tioca_dma_map(struct pci_dev *pdev, u64 paddr, size_t byte_count, int dma_flags)
{
	u64 mapaddr;

	/*
	 * Not supported for now ...
	 */
	if (dma_flags & SN_DMA_MSI)
		return 0;

	/*
	 * If card is 64 or 48 bit addressable, use a direct mapping.  32
	 * bit direct is so restrictive w.r.t. where the memory resides that
	 * we don't use it even though CA has some support.
	 */

	if (pdev->dma_mask == ~0UL)
		mapaddr = tioca_dma_d64(paddr);
	else if (pdev->dma_mask == 0xffffffffffffUL)
		mapaddr = tioca_dma_d48(pdev, paddr);
	else
		mapaddr = 0;

	/* Last resort ... use PCI portion of CA GART */

	if (mapaddr == 0)
		mapaddr = tioca_dma_mapped(pdev, paddr, byte_count);

	return mapaddr;
}

/**
 * tioca_error_intr_handler - SGI TIO CA error interrupt handler
 * @irq: unused
 * @arg: pointer to tioca_common struct for the given CA
 *
 * Handle a CA error interrupt.  Simply a wrapper around a SAL call which
 * defers processing to the SGI prom.
 */
static irqreturn_t
tioca_error_intr_handler(int irq, void *arg)
{
	struct tioca_common *soft = arg;
	struct ia64_sal_retval ret_stuff;
	u64 segment;
	u64 busnum;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	segment = soft->ca_common.bs_persist_segment;
	busnum = soft->ca_common.bs_persist_busnum;

	SAL_CALL_NOLOCK(ret_stuff,
			(u64) SN_SAL_IOIF_ERROR_INTERRUPT,
			segment, busnum, 0, 0, 0, 0, 0);

	return IRQ_HANDLED;
}

/**
 * tioca_bus_fixup - perform final PCI fixup for a TIO CA bus
 * @prom_bussoft: Common prom/kernel struct representing the bus
 *
 * Replicates the tioca_common pointed to by @prom_bussoft in kernel
 * space.  Allocates and initializes a kernel-only area for a given CA,
 * and sets up an irq for handling CA error interrupts.
 *
 * On successful setup, returns the kernel version of tioca_common back to
 * the caller.
 */
static void *
tioca_bus_fixup(struct pcibus_bussoft *prom_bussoft, struct pci_controller *controller)
{
	struct tioca_common *tioca_common;
	struct tioca_kernel *tioca_kern;
	struct pci_bus *bus;

	/* sanity check prom rev */

	if (is_shub1() && sn_sal_rev() < 0x0406) {
		printk
		    (KERN_ERR "%s:  SGI prom rev 4.06 or greater required "
		     "for tioca support\n", __func__);
		return NULL;
	}

	/*
	 * Allocate kernel bus soft and copy from prom.
	 */

	tioca_common = kzalloc(sizeof(struct tioca_common), GFP_KERNEL);
	if (!tioca_common)
		return NULL;

	memcpy(tioca_common, prom_bussoft, sizeof(struct tioca_common));
	tioca_common->ca_common.bs_base = (unsigned long)
		ioremap(REGION_OFFSET(tioca_common->ca_common.bs_base),
			sizeof(struct tioca_common));

	/* init kernel-private area */

	tioca_kern = kzalloc(sizeof(struct tioca_kernel), GFP_KERNEL);
	if (!tioca_kern) {
		kfree(tioca_common);
		return NULL;
	}

	tioca_kern->ca_common = tioca_common;
	spin_lock_init(&tioca_kern->ca_lock);
	INIT_LIST_HEAD(&tioca_kern->ca_dmamaps);
	tioca_kern->ca_closest_node =
	    nasid_to_cnodeid(tioca_common->ca_closest_nasid);
	tioca_common->ca_kernel_private = (u64) tioca_kern;

	bus = pci_find_bus(tioca_common->ca_common.bs_persist_segment,
		tioca_common->ca_common.bs_persist_busnum);
	BUG_ON(!bus);
	tioca_kern->ca_devices = &bus->devices;

	/* init GART */

	if (tioca_gart_init(tioca_kern) < 0) {
		kfree(tioca_kern);
		kfree(tioca_common);
		return NULL;
	}

	tioca_gart_found++;
	list_add(&tioca_kern->ca_list, &tioca_list);

	if (request_irq(SGI_TIOCA_ERROR,
			tioca_error_intr_handler,
			IRQF_SHARED, "TIOCA error", (void *)tioca_common))
		printk(KERN_WARNING
		       "%s:  Unable to get irq %d.  "
		       "Error interrupts won't be routed for TIOCA bus %d\n",
		       __func__, SGI_TIOCA_ERROR,
		       (int)tioca_common->ca_common.bs_persist_busnum);

	sn_set_err_irq_affinity(SGI_TIOCA_ERROR);

	/* Setup locality information */
	controller->node = tioca_kern->ca_closest_node;
	return tioca_common;
}

static struct sn_pcibus_provider tioca_pci_interfaces = {
	.dma_map = tioca_dma_map,
	.dma_map_consistent = tioca_dma_map,
	.dma_unmap = tioca_dma_unmap,
	.bus_fixup = tioca_bus_fixup,
	.force_interrupt = NULL,
	.target_interrupt = NULL
};

/**
 * tioca_init_provider - init SN PCI provider ops for TIO CA
 */
int
tioca_init_provider(void)
{
	sn_pci_provider[PCIIO_ASIC_TYPE_TIOCA] = &tioca_pci_interfaces;
	return 0;
}
