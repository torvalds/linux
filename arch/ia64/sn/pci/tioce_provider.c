/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003-2006 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/addrs.h>
#include <asm/sn/io.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/tioce_provider.h>

/*
 * 1/26/2006
 *
 * WAR for SGI PV 944642.  For revA TIOCE, need to use the following recipe
 * (taken from the above PV) before and after accessing tioce internal MMR's
 * to avoid tioce lockups.
 *
 * The recipe as taken from the PV:
 *
 *	if(mmr address < 0x45000) {
 *		if(mmr address == 0 or 0x80)
 *			mmr wrt or read address 0xc0
 *		else if(mmr address == 0x148 or 0x200)
 *			mmr wrt or read address 0x28
 *		else
 *			mmr wrt or read address 0x158
 *
 *		do desired mmr access (rd or wrt)
 *
 *		if(mmr address == 0x100)
 *			mmr wrt or read address 0x38
 *		mmr wrt or read address 0xb050
 *	} else
 *		do desired mmr access
 *
 * According to hw, we can use reads instead of writes to the above address
 *
 * Note this WAR can only to be used for accessing internal MMR's in the
 * TIOCE Coretalk Address Range 0x0 - 0x07ff_ffff.  This includes the
 * "Local CE Registers and Memories" and "PCI Compatible Config Space" address
 * spaces from table 2-1 of the "CE Programmer's Reference Overview" document.
 *
 * All registers defined in struct tioce will meet that criteria.
 */

static void inline
tioce_mmr_war_pre(struct tioce_kernel *kern, void __iomem *mmr_addr)
{
	u64 mmr_base;
	u64 mmr_offset;

	if (kern->ce_common->ce_rev != TIOCE_REV_A)
		return;

	mmr_base = kern->ce_common->ce_pcibus.bs_base;
	mmr_offset = (unsigned long)mmr_addr - mmr_base;

	if (mmr_offset < 0x45000) {
		u64 mmr_war_offset;

		if (mmr_offset == 0 || mmr_offset == 0x80)
			mmr_war_offset = 0xc0;
		else if (mmr_offset == 0x148 || mmr_offset == 0x200)
			mmr_war_offset = 0x28;
		else
			mmr_war_offset = 0x158;

		readq_relaxed((void __iomem *)(mmr_base + mmr_war_offset));
	}
}

static void inline
tioce_mmr_war_post(struct tioce_kernel *kern, void __iomem *mmr_addr)
{
	u64 mmr_base;
	u64 mmr_offset;

	if (kern->ce_common->ce_rev != TIOCE_REV_A)
		return;

	mmr_base = kern->ce_common->ce_pcibus.bs_base;
	mmr_offset = (unsigned long)mmr_addr - mmr_base;

	if (mmr_offset < 0x45000) {
		if (mmr_offset == 0x100)
			readq_relaxed((void __iomem *)(mmr_base + 0x38));
		readq_relaxed((void __iomem *)(mmr_base + 0xb050));
	}
}

/* load mmr contents into a variable */
#define tioce_mmr_load(kern, mmrp, varp) do {\
	tioce_mmr_war_pre(kern, mmrp); \
	*(varp) = readq_relaxed(mmrp); \
	tioce_mmr_war_post(kern, mmrp); \
} while (0)

/* store variable contents into mmr */
#define tioce_mmr_store(kern, mmrp, varp) do {\
	tioce_mmr_war_pre(kern, mmrp); \
	writeq(*varp, mmrp); \
	tioce_mmr_war_post(kern, mmrp); \
} while (0)

/* store immediate value into mmr */
#define tioce_mmr_storei(kern, mmrp, val) do {\
	tioce_mmr_war_pre(kern, mmrp); \
	writeq(val, mmrp); \
	tioce_mmr_war_post(kern, mmrp); \
} while (0)

/* set bits (immediate value) into mmr */
#define tioce_mmr_seti(kern, mmrp, bits) do {\
	u64 tmp; \
	tioce_mmr_load(kern, mmrp, &tmp); \
	tmp |= (bits); \
	tioce_mmr_store(kern, mmrp, &tmp); \
} while (0)

/* clear bits (immediate value) into mmr */
#define tioce_mmr_clri(kern, mmrp, bits) do { \
	u64 tmp; \
	tioce_mmr_load(kern, mmrp, &tmp); \
	tmp &= ~(bits); \
	tioce_mmr_store(kern, mmrp, &tmp); \
} while (0)

/**
 * Bus address ranges for the 5 flavors of TIOCE DMA
 */

#define TIOCE_D64_MIN	0x8000000000000000UL
#define TIOCE_D64_MAX	0xffffffffffffffffUL
#define TIOCE_D64_ADDR(a)	((a) >= TIOCE_D64_MIN)

#define TIOCE_D32_MIN	0x0000000080000000UL
#define TIOCE_D32_MAX	0x00000000ffffffffUL
#define TIOCE_D32_ADDR(a)	((a) >= TIOCE_D32_MIN && (a) <= TIOCE_D32_MAX)

#define TIOCE_M32_MIN	0x0000000000000000UL
#define TIOCE_M32_MAX	0x000000007fffffffUL
#define TIOCE_M32_ADDR(a)	((a) >= TIOCE_M32_MIN && (a) <= TIOCE_M32_MAX)

#define TIOCE_M40_MIN	0x0000004000000000UL
#define TIOCE_M40_MAX	0x0000007fffffffffUL
#define TIOCE_M40_ADDR(a)	((a) >= TIOCE_M40_MIN && (a) <= TIOCE_M40_MAX)

#define TIOCE_M40S_MIN	0x0000008000000000UL
#define TIOCE_M40S_MAX	0x000000ffffffffffUL
#define TIOCE_M40S_ADDR(a)	((a) >= TIOCE_M40S_MIN && (a) <= TIOCE_M40S_MAX)

/*
 * ATE manipulation macros.
 */

#define ATE_PAGESHIFT(ps)	(__ffs(ps))
#define ATE_PAGEMASK(ps)	((ps)-1)

#define ATE_PAGE(x, ps) ((x) >> ATE_PAGESHIFT(ps))
#define ATE_NPAGES(start, len, pagesize) \
	(ATE_PAGE((start)+(len)-1, pagesize) - ATE_PAGE(start, pagesize) + 1)

#define ATE_VALID(ate)	((ate) & (1UL << 63))
#define ATE_MAKE(addr, ps, msi) \
	(((addr) & ~ATE_PAGEMASK(ps)) | (1UL << 63) | ((msi)?(1UL << 62):0))

/*
 * Flavors of ate-based mapping supported by tioce_alloc_map()
 */

#define TIOCE_ATE_M32	1
#define TIOCE_ATE_M40	2
#define TIOCE_ATE_M40S	3

#define KB(x)	((u64)(x) << 10)
#define MB(x)	((u64)(x) << 20)
#define GB(x)	((u64)(x) << 30)

/**
 * tioce_dma_d64 - create a DMA mapping using 64-bit direct mode
 * @ct_addr: system coretalk address
 *
 * Map @ct_addr into 64-bit CE bus space.  No device context is necessary
 * and no CE mapping are consumed.
 *
 * Bits 53:0 come from the coretalk address.  The remaining bits are set as
 * follows:
 *
 * 63    - must be 1 to indicate d64 mode to CE hardware
 * 62    - barrier bit ... controlled with tioce_dma_barrier()
 * 61    - msi bit ... specified through dma_flags
 * 60:54 - reserved, MBZ
 */
static u64
tioce_dma_d64(unsigned long ct_addr, int dma_flags)
{
	u64 bus_addr;

	bus_addr = ct_addr | (1UL << 63);
	if (dma_flags & SN_DMA_MSI)
		bus_addr |= (1UL << 61);

	return bus_addr;
}

/**
 * pcidev_to_tioce - return misc ce related pointers given a pci_dev
 * @pci_dev: pci device context
 * @base: ptr to store struct tioce_mmr * for the CE holding this device
 * @kernel: ptr to store struct tioce_kernel * for the CE holding this device
 * @port: ptr to store the CE port number that this device is on
 *
 * Return pointers to various CE-related structures for the CE upstream of
 * @pci_dev.
 */
static inline void
pcidev_to_tioce(struct pci_dev *pdev, struct tioce __iomem **base,
		struct tioce_kernel **kernel, int *port)
{
	struct pcidev_info *pcidev_info;
	struct tioce_common *ce_common;
	struct tioce_kernel *ce_kernel;

	pcidev_info = SN_PCIDEV_INFO(pdev);
	ce_common = (struct tioce_common *)pcidev_info->pdi_pcibus_info;
	ce_kernel = (struct tioce_kernel *)ce_common->ce_kernel_private;

	if (base)
		*base = (struct tioce __iomem *)ce_common->ce_pcibus.bs_base;
	if (kernel)
		*kernel = ce_kernel;

	/*
	 * we use port as a zero-based value internally, even though the
	 * documentation is 1-based.
	 */
	if (port)
		*port =
		    (pdev->bus->number < ce_kernel->ce_port1_secondary) ? 0 : 1;
}

/**
 * tioce_alloc_map - Given a coretalk address, map it to pcie bus address
 * space using one of the various ATE-based address modes.
 * @ce_kern: tioce context
 * @type: map mode to use
 * @port: 0-based port that the requesting device is downstream of
 * @ct_addr: the coretalk address to map
 * @len: number of bytes to map
 *
 * Given the addressing type, set up various parameters that define the
 * ATE pool to use.  Search for a contiguous block of entries to cover the
 * length, and if enough resources exist, fill in the ATEs and construct a
 * tioce_dmamap struct to track the mapping.
 */
static u64
tioce_alloc_map(struct tioce_kernel *ce_kern, int type, int port,
		u64 ct_addr, int len, int dma_flags)
{
	int i;
	int j;
	int first;
	int last;
	int entries;
	int nates;
	u64 pagesize;
	int msi_capable, msi_wanted;
	u64 *ate_shadow;
	u64 __iomem *ate_reg;
	u64 addr;
	struct tioce __iomem *ce_mmr;
	u64 bus_base;
	struct tioce_dmamap *map;

	ce_mmr = (struct tioce __iomem *)ce_kern->ce_common->ce_pcibus.bs_base;

	switch (type) {
	case TIOCE_ATE_M32:
		/*
		 * The first 64 entries of the ate3240 pool are dedicated to
		 * super-page (TIOCE_ATE_M40S) mode.
		 */
		first = 64;
		entries = TIOCE_NUM_M3240_ATES - 64;
		ate_shadow = ce_kern->ce_ate3240_shadow;
		ate_reg = ce_mmr->ce_ure_ate3240;
		pagesize = ce_kern->ce_ate3240_pagesize;
		bus_base = TIOCE_M32_MIN;
		msi_capable = 1;
		break;
	case TIOCE_ATE_M40:
		first = 0;
		entries = TIOCE_NUM_M40_ATES;
		ate_shadow = ce_kern->ce_ate40_shadow;
		ate_reg = ce_mmr->ce_ure_ate40;
		pagesize = MB(64);
		bus_base = TIOCE_M40_MIN;
		msi_capable = 0;
		break;
	case TIOCE_ATE_M40S:
		/*
		 * ate3240 entries 0-31 are dedicated to port1 super-page
		 * mappings.  ate3240 entries 32-63 are dedicated to port2.
		 */
		first = port * 32;
		entries = 32;
		ate_shadow = ce_kern->ce_ate3240_shadow;
		ate_reg = ce_mmr->ce_ure_ate3240;
		pagesize = GB(16);
		bus_base = TIOCE_M40S_MIN;
		msi_capable = 0;
		break;
	default:
		return 0;
	}

	msi_wanted = dma_flags & SN_DMA_MSI;
	if (msi_wanted && !msi_capable)
		return 0;

	nates = ATE_NPAGES(ct_addr, len, pagesize);
	if (nates > entries)
		return 0;

	last = first + entries - nates;
	for (i = first; i <= last; i++) {
		if (ATE_VALID(ate_shadow[i]))
			continue;

		for (j = i; j < i + nates; j++)
			if (ATE_VALID(ate_shadow[j]))
				break;

		if (j >= i + nates)
			break;
	}

	if (i > last)
		return 0;

	map = kzalloc(sizeof(struct tioce_dmamap), GFP_ATOMIC);
	if (!map)
		return 0;

	addr = ct_addr;
	for (j = 0; j < nates; j++) {
		u64 ate;

		ate = ATE_MAKE(addr, pagesize, msi_wanted);
		ate_shadow[i + j] = ate;
		tioce_mmr_storei(ce_kern, &ate_reg[i + j], ate);
		addr += pagesize;
	}

	map->refcnt = 1;
	map->nbytes = nates * pagesize;
	map->ct_start = ct_addr & ~ATE_PAGEMASK(pagesize);
	map->pci_start = bus_base + (i * pagesize);
	map->ate_hw = &ate_reg[i];
	map->ate_shadow = &ate_shadow[i];
	map->ate_count = nates;

	list_add(&map->ce_dmamap_list, &ce_kern->ce_dmamap_list);

	return (map->pci_start + (ct_addr - map->ct_start));
}

/**
 * tioce_dma_d32 - create a DMA mapping using 32-bit direct mode
 * @pdev: linux pci_dev representing the function
 * @paddr: system physical address
 *
 * Map @paddr into 32-bit bus space of the CE associated with @pcidev_info.
 */
static u64
tioce_dma_d32(struct pci_dev *pdev, u64 ct_addr, int dma_flags)
{
	int dma_ok;
	int port;
	struct tioce __iomem *ce_mmr;
	struct tioce_kernel *ce_kern;
	u64 ct_upper;
	u64 ct_lower;
	dma_addr_t bus_addr;

	if (dma_flags & SN_DMA_MSI)
		return 0;

	ct_upper = ct_addr & ~0x3fffffffUL;
	ct_lower = ct_addr & 0x3fffffffUL;

	pcidev_to_tioce(pdev, &ce_mmr, &ce_kern, &port);

	if (ce_kern->ce_port[port].dirmap_refcnt == 0) {
		u64 tmp;

		ce_kern->ce_port[port].dirmap_shadow = ct_upper;
		tioce_mmr_storei(ce_kern, &ce_mmr->ce_ure_dir_map[port],
				 ct_upper);
		tmp = ce_mmr->ce_ure_dir_map[port];
		dma_ok = 1;
	} else
		dma_ok = (ce_kern->ce_port[port].dirmap_shadow == ct_upper);

	if (dma_ok) {
		ce_kern->ce_port[port].dirmap_refcnt++;
		bus_addr = TIOCE_D32_MIN + ct_lower;
	} else
		bus_addr = 0;

	return bus_addr;
}

/**
 * tioce_dma_barrier - swizzle a TIOCE bus address to include or exclude
 * the barrier bit.
 * @bus_addr:  bus address to swizzle
 *
 * Given a TIOCE bus address, set the appropriate bit to indicate barrier
 * attributes.
 */
static u64
tioce_dma_barrier(u64 bus_addr, int on)
{
	u64 barrier_bit;

	/* barrier not supported in M40/M40S mode */
	if (TIOCE_M40_ADDR(bus_addr) || TIOCE_M40S_ADDR(bus_addr))
		return bus_addr;

	if (TIOCE_D64_ADDR(bus_addr))
		barrier_bit = (1UL << 62);
	else			/* must be m32 or d32 */
		barrier_bit = (1UL << 30);

	return (on) ? (bus_addr | barrier_bit) : (bus_addr & ~barrier_bit);
}

/**
 * tioce_dma_unmap - release CE mapping resources
 * @pdev: linux pci_dev representing the function
 * @bus_addr: bus address returned by an earlier tioce_dma_map
 * @dir: mapping direction (unused)
 *
 * Locate mapping resources associated with @bus_addr and release them.
 * For mappings created using the direct modes there are no resources
 * to release.
 */
void
tioce_dma_unmap(struct pci_dev *pdev, dma_addr_t bus_addr, int dir)
{
	int i;
	int port;
	struct tioce_kernel *ce_kern;
	struct tioce __iomem *ce_mmr;
	unsigned long flags;

	bus_addr = tioce_dma_barrier(bus_addr, 0);
	pcidev_to_tioce(pdev, &ce_mmr, &ce_kern, &port);

	/* nothing to do for D64 */

	if (TIOCE_D64_ADDR(bus_addr))
		return;

	spin_lock_irqsave(&ce_kern->ce_lock, flags);

	if (TIOCE_D32_ADDR(bus_addr)) {
		if (--ce_kern->ce_port[port].dirmap_refcnt == 0) {
			ce_kern->ce_port[port].dirmap_shadow = 0;
			tioce_mmr_storei(ce_kern, &ce_mmr->ce_ure_dir_map[port],
					 0);
		}
	} else {
		struct tioce_dmamap *map;

		list_for_each_entry(map, &ce_kern->ce_dmamap_list,
				    ce_dmamap_list) {
			u64 last;

			last = map->pci_start + map->nbytes - 1;
			if (bus_addr >= map->pci_start && bus_addr <= last)
				break;
		}

		if (&map->ce_dmamap_list == &ce_kern->ce_dmamap_list) {
			printk(KERN_WARNING
			       "%s:  %s - no map found for bus_addr 0x%lx\n",
			       __FUNCTION__, pci_name(pdev), bus_addr);
		} else if (--map->refcnt == 0) {
			for (i = 0; i < map->ate_count; i++) {
				map->ate_shadow[i] = 0;
				tioce_mmr_storei(ce_kern, &map->ate_hw[i], 0);
			}

			list_del(&map->ce_dmamap_list);
			kfree(map);
		}
	}

	spin_unlock_irqrestore(&ce_kern->ce_lock, flags);
}

/**
 * tioce_do_dma_map - map pages for PCI DMA
 * @pdev: linux pci_dev representing the function
 * @paddr: host physical address to map
 * @byte_count: bytes to map
 *
 * This is the main wrapper for mapping host physical pages to CE PCI space.
 * The mapping mode used is based on the device's dma_mask.
 */
static u64
tioce_do_dma_map(struct pci_dev *pdev, u64 paddr, size_t byte_count,
		 int barrier, int dma_flags)
{
	unsigned long flags;
	u64 ct_addr;
	u64 mapaddr = 0;
	struct tioce_kernel *ce_kern;
	struct tioce_dmamap *map;
	int port;
	u64 dma_mask;

	dma_mask = (barrier) ? pdev->dev.coherent_dma_mask : pdev->dma_mask;

	/* cards must be able to address at least 31 bits */
	if (dma_mask < 0x7fffffffUL)
		return 0;

	if (SN_DMA_ADDRTYPE(dma_flags) == SN_DMA_ADDR_PHYS)
		ct_addr = PHYS_TO_TIODMA(paddr);
	else
		ct_addr = paddr;

	/*
	 * If the device can generate 64 bit addresses, create a D64 map.
	 */
	if (dma_mask == ~0UL) {
		mapaddr = tioce_dma_d64(ct_addr, dma_flags);
		if (mapaddr)
			goto dma_map_done;
	}

	pcidev_to_tioce(pdev, NULL, &ce_kern, &port);

	spin_lock_irqsave(&ce_kern->ce_lock, flags);

	/*
	 * D64 didn't work ... See if we have an existing map that covers
	 * this address range.  Must account for devices dma_mask here since
	 * an existing map might have been done in a mode using more pci
	 * address bits than this device can support.
	 */
	list_for_each_entry(map, &ce_kern->ce_dmamap_list, ce_dmamap_list) {
		u64 last;

		last = map->ct_start + map->nbytes - 1;
		if (ct_addr >= map->ct_start &&
		    ct_addr + byte_count - 1 <= last &&
		    map->pci_start <= dma_mask) {
			map->refcnt++;
			mapaddr = map->pci_start + (ct_addr - map->ct_start);
			break;
		}
	}

	/*
	 * If we don't have a map yet, and the card can generate 40
	 * bit addresses, try the M40/M40S modes.  Note these modes do not
	 * support a barrier bit, so if we need a consistent map these
	 * won't work.
	 */
	if (!mapaddr && !barrier && dma_mask >= 0xffffffffffUL) {
		/*
		 * We have two options for 40-bit mappings:  16GB "super" ATEs
		 * and 64MB "regular" ATEs.  We'll try both if needed for a
		 * given mapping but which one we try first depends on the
		 * size.  For requests >64MB, prefer to use a super page with
		 * regular as the fallback. Otherwise, try in the reverse order.
		 */

		if (byte_count > MB(64)) {
			mapaddr = tioce_alloc_map(ce_kern, TIOCE_ATE_M40S,
						  port, ct_addr, byte_count,
						  dma_flags);
			if (!mapaddr)
				mapaddr =
				    tioce_alloc_map(ce_kern, TIOCE_ATE_M40, -1,
						    ct_addr, byte_count,
						    dma_flags);
		} else {
			mapaddr = tioce_alloc_map(ce_kern, TIOCE_ATE_M40, -1,
						  ct_addr, byte_count,
						  dma_flags);
			if (!mapaddr)
				mapaddr =
				    tioce_alloc_map(ce_kern, TIOCE_ATE_M40S,
						    port, ct_addr, byte_count,
						    dma_flags);
		}
	}

	/*
	 * 32-bit direct is the next mode to try
	 */
	if (!mapaddr && dma_mask >= 0xffffffffUL)
		mapaddr = tioce_dma_d32(pdev, ct_addr, dma_flags);

	/*
	 * Last resort, try 32-bit ATE-based map.
	 */
	if (!mapaddr)
		mapaddr =
		    tioce_alloc_map(ce_kern, TIOCE_ATE_M32, -1, ct_addr,
				    byte_count, dma_flags);

	spin_unlock_irqrestore(&ce_kern->ce_lock, flags);

dma_map_done:
	if (mapaddr && barrier)
		mapaddr = tioce_dma_barrier(mapaddr, 1);

	return mapaddr;
}

/**
 * tioce_dma - standard pci dma map interface
 * @pdev: pci device requesting the map
 * @paddr: system physical address to map into pci space
 * @byte_count: # bytes to map
 *
 * Simply call tioce_do_dma_map() to create a map with the barrier bit clear
 * in the address.
 */
static u64
tioce_dma(struct pci_dev *pdev, u64 paddr, size_t byte_count, int dma_flags)
{
	return tioce_do_dma_map(pdev, paddr, byte_count, 0, dma_flags);
}

/**
 * tioce_dma_consistent - consistent pci dma map interface
 * @pdev: pci device requesting the map
 * @paddr: system physical address to map into pci space
 * @byte_count: # bytes to map
 *
 * Simply call tioce_do_dma_map() to create a map with the barrier bit set
 * in the address.
 */ static u64
tioce_dma_consistent(struct pci_dev *pdev, u64 paddr, size_t byte_count, int dma_flags)
{
	return tioce_do_dma_map(pdev, paddr, byte_count, 1, dma_flags);
}

/**
 * tioce_error_intr_handler - SGI TIO CE error interrupt handler
 * @irq: unused
 * @arg: pointer to tioce_common struct for the given CE
 *
 * Handle a CE error interrupt.  Simply a wrapper around a SAL call which
 * defers processing to the SGI prom.
 */ static irqreturn_t
tioce_error_intr_handler(int irq, void *arg)
{
	struct tioce_common *soft = arg;
	struct ia64_sal_retval ret_stuff;
	ret_stuff.status = 0;
	ret_stuff.v0 = 0;

	SAL_CALL_NOLOCK(ret_stuff, (u64) SN_SAL_IOIF_ERROR_INTERRUPT,
			soft->ce_pcibus.bs_persist_segment,
			soft->ce_pcibus.bs_persist_busnum, 0, 0, 0, 0, 0);

	if (ret_stuff.v0)
		panic("tioce_error_intr_handler:  Fatal TIOCE error");

	return IRQ_HANDLED;
}

/**
 * tioce_reserve_m32 - reserve M32 ATEs for the indicated address range
 * @tioce_kernel: TIOCE context to reserve ATEs for
 * @base: starting bus address to reserve
 * @limit: last bus address to reserve
 *
 * If base/limit falls within the range of bus space mapped through the
 * M32 space, reserve the resources corresponding to the range.
 */
static void
tioce_reserve_m32(struct tioce_kernel *ce_kern, u64 base, u64 limit)
{
	int ate_index, last_ate, ps;
	struct tioce __iomem *ce_mmr;

	ce_mmr = (struct tioce __iomem *)ce_kern->ce_common->ce_pcibus.bs_base;
	ps = ce_kern->ce_ate3240_pagesize;
	ate_index = ATE_PAGE(base, ps);
	last_ate = ate_index + ATE_NPAGES(base, limit-base+1, ps) - 1;

	if (ate_index < 64)
		ate_index = 64;

	if (last_ate >= TIOCE_NUM_M3240_ATES)
		last_ate = TIOCE_NUM_M3240_ATES - 1;

	while (ate_index <= last_ate) {
		u64 ate;

		ate = ATE_MAKE(0xdeadbeef, ps, 0);
		ce_kern->ce_ate3240_shadow[ate_index] = ate;
		tioce_mmr_storei(ce_kern, &ce_mmr->ce_ure_ate3240[ate_index],
				 ate);
		ate_index++;
	}
}

/**
 * tioce_kern_init - init kernel structures related to a given TIOCE
 * @tioce_common: ptr to a cached tioce_common struct that originated in prom
 */
static struct tioce_kernel *
tioce_kern_init(struct tioce_common *tioce_common)
{
	int i;
	int ps;
	int dev;
	u32 tmp;
	unsigned int seg, bus;
	struct tioce __iomem *tioce_mmr;
	struct tioce_kernel *tioce_kern;

	tioce_kern = kzalloc(sizeof(struct tioce_kernel), GFP_KERNEL);
	if (!tioce_kern) {
		return NULL;
	}

	tioce_kern->ce_common = tioce_common;
	spin_lock_init(&tioce_kern->ce_lock);
	INIT_LIST_HEAD(&tioce_kern->ce_dmamap_list);
	tioce_common->ce_kernel_private = (u64) tioce_kern;

	/*
	 * Determine the secondary bus number of the port2 logical PPB.
	 * This is used to decide whether a given pci device resides on
	 * port1 or port2.  Note:  We don't have enough plumbing set up
	 * here to use pci_read_config_xxx() so use raw_pci_read().
	 */

	seg = tioce_common->ce_pcibus.bs_persist_segment;
	bus = tioce_common->ce_pcibus.bs_persist_busnum;

	raw_pci_read(seg, bus, PCI_DEVFN(2, 0), PCI_SECONDARY_BUS, 1,&tmp);
	tioce_kern->ce_port1_secondary = (u8) tmp;

	/*
	 * Set PMU pagesize to the largest size available, and zero out
	 * the ATEs.
	 */

	tioce_mmr = (struct tioce __iomem *)tioce_common->ce_pcibus.bs_base;
	tioce_mmr_clri(tioce_kern, &tioce_mmr->ce_ure_page_map,
		       CE_URE_PAGESIZE_MASK);
	tioce_mmr_seti(tioce_kern, &tioce_mmr->ce_ure_page_map,
		       CE_URE_256K_PAGESIZE);
	ps = tioce_kern->ce_ate3240_pagesize = KB(256);

	for (i = 0; i < TIOCE_NUM_M40_ATES; i++) {
		tioce_kern->ce_ate40_shadow[i] = 0;
		tioce_mmr_storei(tioce_kern, &tioce_mmr->ce_ure_ate40[i], 0);
	}

	for (i = 0; i < TIOCE_NUM_M3240_ATES; i++) {
		tioce_kern->ce_ate3240_shadow[i] = 0;
		tioce_mmr_storei(tioce_kern, &tioce_mmr->ce_ure_ate3240[i], 0);
	}

	/*
	 * Reserve ATEs corresponding to reserved address ranges.  These
	 * include:
	 *
	 *	Memory space covered by each PPB mem base/limit register
	 * 	Memory space covered by each PPB prefetch base/limit register
	 *
	 * These bus ranges are for pio (downstream) traffic only, and so
	 * cannot be used for DMA.
	 */

	for (dev = 1; dev <= 2; dev++) {
		u64 base, limit;

		/* mem base/limit */

		raw_pci_read(seg, bus, PCI_DEVFN(dev, 0),
				  PCI_MEMORY_BASE, 2, &tmp);
		base = (u64)tmp << 16;

		raw_pci_read(seg, bus, PCI_DEVFN(dev, 0),
				  PCI_MEMORY_LIMIT, 2, &tmp);
		limit = (u64)tmp << 16;
		limit |= 0xfffffUL;

		if (base < limit)
			tioce_reserve_m32(tioce_kern, base, limit);

		/*
		 * prefetch mem base/limit.  The tioce ppb's have 64-bit
		 * decoders, so read the upper portions w/o checking the
		 * attributes.
		 */

		raw_pci_read(seg, bus, PCI_DEVFN(dev, 0),
				  PCI_PREF_MEMORY_BASE, 2, &tmp);
		base = ((u64)tmp & PCI_PREF_RANGE_MASK) << 16;

		raw_pci_read(seg, bus, PCI_DEVFN(dev, 0),
				  PCI_PREF_BASE_UPPER32, 4, &tmp);
		base |= (u64)tmp << 32;

		raw_pci_read(seg, bus, PCI_DEVFN(dev, 0),
				  PCI_PREF_MEMORY_LIMIT, 2, &tmp);

		limit = ((u64)tmp & PCI_PREF_RANGE_MASK) << 16;
		limit |= 0xfffffUL;

		raw_pci_read(seg, bus, PCI_DEVFN(dev, 0),
				  PCI_PREF_LIMIT_UPPER32, 4, &tmp);
		limit |= (u64)tmp << 32;

		if ((base < limit) && TIOCE_M32_ADDR(base))
			tioce_reserve_m32(tioce_kern, base, limit);
	}

	return tioce_kern;
}

/**
 * tioce_force_interrupt - implement altix force_interrupt() backend for CE
 * @sn_irq_info: sn asic irq that we need an interrupt generated for
 *
 * Given an sn_irq_info struct, set the proper bit in ce_adm_force_int to
 * force a secondary interrupt to be generated.  This is to work around an
 * asic issue where there is a small window of opportunity for a legacy device
 * interrupt to be lost.
 */
static void
tioce_force_interrupt(struct sn_irq_info *sn_irq_info)
{
	struct pcidev_info *pcidev_info;
	struct tioce_common *ce_common;
	struct tioce_kernel *ce_kern;
	struct tioce __iomem *ce_mmr;
	u64 force_int_val;

	if (!sn_irq_info->irq_bridge)
		return;

	if (sn_irq_info->irq_bridge_type != PCIIO_ASIC_TYPE_TIOCE)
		return;

	pcidev_info = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	if (!pcidev_info)
		return;

	ce_common = (struct tioce_common *)pcidev_info->pdi_pcibus_info;
	ce_mmr = (struct tioce __iomem *)ce_common->ce_pcibus.bs_base;
	ce_kern = (struct tioce_kernel *)ce_common->ce_kernel_private;

	/*
	 * TIOCE Rev A workaround (PV 945826), force an interrupt by writing
	 * the TIO_INTx register directly (1/26/2006)
	 */
	if (ce_common->ce_rev == TIOCE_REV_A) {
		u64 int_bit_mask = (1ULL << sn_irq_info->irq_int_bit);
		u64 status;

		tioce_mmr_load(ce_kern, &ce_mmr->ce_adm_int_status, &status);
		if (status & int_bit_mask) {
			u64 force_irq = (1 << 8) | sn_irq_info->irq_irq;
			u64 ctalk = sn_irq_info->irq_xtalkaddr;
			u64 nasid, offset;

			nasid = (ctalk & CTALK_NASID_MASK) >> CTALK_NASID_SHFT;
			offset = (ctalk & CTALK_NODE_OFFSET);
			HUB_S(TIO_IOSPACE_ADDR(nasid, offset), force_irq);
		}

		return;
	}

	/*
	 * irq_int_bit is originally set up by prom, and holds the interrupt
	 * bit shift (not mask) as defined by the bit definitions in the
	 * ce_adm_int mmr.  These shifts are not the same for the
	 * ce_adm_force_int register, so do an explicit mapping here to make
	 * things clearer.
	 */

	switch (sn_irq_info->irq_int_bit) {
	case CE_ADM_INT_PCIE_PORT1_DEV_A_SHFT:
		force_int_val = 1UL << CE_ADM_FORCE_INT_PCIE_PORT1_DEV_A_SHFT;
		break;
	case CE_ADM_INT_PCIE_PORT1_DEV_B_SHFT:
		force_int_val = 1UL << CE_ADM_FORCE_INT_PCIE_PORT1_DEV_B_SHFT;
		break;
	case CE_ADM_INT_PCIE_PORT1_DEV_C_SHFT:
		force_int_val = 1UL << CE_ADM_FORCE_INT_PCIE_PORT1_DEV_C_SHFT;
		break;
	case CE_ADM_INT_PCIE_PORT1_DEV_D_SHFT:
		force_int_val = 1UL << CE_ADM_FORCE_INT_PCIE_PORT1_DEV_D_SHFT;
		break;
	case CE_ADM_INT_PCIE_PORT2_DEV_A_SHFT:
		force_int_val = 1UL << CE_ADM_FORCE_INT_PCIE_PORT2_DEV_A_SHFT;
		break;
	case CE_ADM_INT_PCIE_PORT2_DEV_B_SHFT:
		force_int_val = 1UL << CE_ADM_FORCE_INT_PCIE_PORT2_DEV_B_SHFT;
		break;
	case CE_ADM_INT_PCIE_PORT2_DEV_C_SHFT:
		force_int_val = 1UL << CE_ADM_FORCE_INT_PCIE_PORT2_DEV_C_SHFT;
		break;
	case CE_ADM_INT_PCIE_PORT2_DEV_D_SHFT:
		force_int_val = 1UL << CE_ADM_FORCE_INT_PCIE_PORT2_DEV_D_SHFT;
		break;
	default:
		return;
	}
	tioce_mmr_storei(ce_kern, &ce_mmr->ce_adm_force_int, force_int_val);
}

/**
 * tioce_target_interrupt - implement set_irq_affinity for tioce resident
 * functions.  Note:  only applies to line interrupts, not MSI's.
 *
 * @sn_irq_info: SN IRQ context
 *
 * Given an sn_irq_info, set the associated CE device's interrupt destination
 * register.  Since the interrupt destination registers are on a per-ce-slot
 * basis, this will retarget line interrupts for all functions downstream of
 * the slot.
 */
static void
tioce_target_interrupt(struct sn_irq_info *sn_irq_info)
{
	struct pcidev_info *pcidev_info;
	struct tioce_common *ce_common;
	struct tioce_kernel *ce_kern;
	struct tioce __iomem *ce_mmr;
	int bit;
	u64 vector;

	pcidev_info = (struct pcidev_info *)sn_irq_info->irq_pciioinfo;
	if (!pcidev_info)
		return;

	ce_common = (struct tioce_common *)pcidev_info->pdi_pcibus_info;
	ce_mmr = (struct tioce __iomem *)ce_common->ce_pcibus.bs_base;
	ce_kern = (struct tioce_kernel *)ce_common->ce_kernel_private;

	bit = sn_irq_info->irq_int_bit;

	tioce_mmr_seti(ce_kern, &ce_mmr->ce_adm_int_mask, (1UL << bit));
	vector = (u64)sn_irq_info->irq_irq << INTR_VECTOR_SHFT;
	vector |= sn_irq_info->irq_xtalkaddr;
	tioce_mmr_storei(ce_kern, &ce_mmr->ce_adm_int_dest[bit], vector);
	tioce_mmr_clri(ce_kern, &ce_mmr->ce_adm_int_mask, (1UL << bit));

	tioce_force_interrupt(sn_irq_info);
}

/**
 * tioce_bus_fixup - perform final PCI fixup for a TIO CE bus
 * @prom_bussoft: Common prom/kernel struct representing the bus
 *
 * Replicates the tioce_common pointed to by @prom_bussoft in kernel
 * space.  Allocates and initializes a kernel-only area for a given CE,
 * and sets up an irq for handling CE error interrupts.
 *
 * On successful setup, returns the kernel version of tioce_common back to
 * the caller.
 */
static void *
tioce_bus_fixup(struct pcibus_bussoft *prom_bussoft, struct pci_controller *controller)
{
	struct tioce_common *tioce_common;
	struct tioce_kernel *tioce_kern;
	struct tioce __iomem *tioce_mmr;

	/*
	 * Allocate kernel bus soft and copy from prom.
	 */

	tioce_common = kzalloc(sizeof(struct tioce_common), GFP_KERNEL);
	if (!tioce_common)
		return NULL;

	memcpy(tioce_common, prom_bussoft, sizeof(struct tioce_common));
	tioce_common->ce_pcibus.bs_base = (unsigned long)
		ioremap(REGION_OFFSET(tioce_common->ce_pcibus.bs_base),
			sizeof(struct tioce_common));

	tioce_kern = tioce_kern_init(tioce_common);
	if (tioce_kern == NULL) {
		kfree(tioce_common);
		return NULL;
	}

	/*
	 * Clear out any transient errors before registering the error
	 * interrupt handler.
	 */

	tioce_mmr = (struct tioce __iomem *)tioce_common->ce_pcibus.bs_base;
	tioce_mmr_seti(tioce_kern, &tioce_mmr->ce_adm_int_status_alias, ~0ULL);
	tioce_mmr_seti(tioce_kern, &tioce_mmr->ce_adm_error_summary_alias,
		       ~0ULL);
	tioce_mmr_seti(tioce_kern, &tioce_mmr->ce_dre_comp_err_addr, 0ULL);

	if (request_irq(SGI_PCIASIC_ERROR,
			tioce_error_intr_handler,
			IRQF_SHARED, "TIOCE error", (void *)tioce_common))
		printk(KERN_WARNING
		       "%s:  Unable to get irq %d.  "
		       "Error interrupts won't be routed for "
		       "TIOCE bus %04x:%02x\n",
		       __FUNCTION__, SGI_PCIASIC_ERROR,
		       tioce_common->ce_pcibus.bs_persist_segment,
		       tioce_common->ce_pcibus.bs_persist_busnum);

	sn_set_err_irq_affinity(SGI_PCIASIC_ERROR);
	return tioce_common;
}

static struct sn_pcibus_provider tioce_pci_interfaces = {
	.dma_map = tioce_dma,
	.dma_map_consistent = tioce_dma_consistent,
	.dma_unmap = tioce_dma_unmap,
	.bus_fixup = tioce_bus_fixup,
	.force_interrupt = tioce_force_interrupt,
	.target_interrupt = tioce_target_interrupt
};

/**
 * tioce_init_provider - init SN PCI provider ops for TIO CE
 */
int
tioce_init_provider(void)
{
	sn_pci_provider[PCIIO_ASIC_TYPE_TIOCE] = &tioce_pci_interfaces;
	return 0;
}
