// SPDX-License-Identifier: GPL-2.0
/*
 * Firmware replacement code.
 *
 * Work around broken BIOSes that don't set an aperture, only set the
 * aperture in the AGP bridge, or set too small aperture.
 *
 * If all fails map the aperture over some low memory.  This is cheaper than
 * doing bounce buffering. The memory is lost. This is done at early boot
 * because only the bootmem allocator can allocate 32+MB.
 *
 * Copyright 2002 Andi Kleen, SuSE Labs.
 */
#define pr_fmt(fmt) "AGP: " fmt

#include <linux/kernel.h>
#include <linux/kcore.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/pci_ids.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/suspend.h>
#include <asm/e820/api.h>
#include <asm/io.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/pci-direct.h>
#include <asm/dma.h>
#include <asm/amd/nb.h>
#include <asm/x86_init.h>
#include <linux/crash_dump.h>

/*
 * Using 512M as goal, in case kexec will load kernel_big
 * that will do the on-position decompress, and could overlap with
 * the gart aperture that is used.
 * Sequence:
 * kernel_small
 * ==> kexec (with kdump trigger path or gart still enabled)
 * ==> kernel_small (gart area become e820_reserved)
 * ==> kexec (with kdump trigger path or gart still enabled)
 * ==> kerne_big (uncompressed size will be big than 64M or 128M)
 * So don't use 512M below as gart iommu, leave the space for kernel
 * code for safe.
 */
#define GART_MIN_ADDR	(512ULL << 20)
#define GART_MAX_ADDR	(1ULL   << 32)

int gart_iommu_aperture;
int gart_iommu_aperture_disabled __initdata;
int gart_iommu_aperture_allowed __initdata;

int fallback_aper_order __initdata = 1; /* 64MB */
int fallback_aper_force __initdata;

int fix_aperture __initdata = 1;

#if defined(CONFIG_PROC_VMCORE) || defined(CONFIG_PROC_KCORE)
/*
 * If the first kernel maps the aperture over e820 RAM, the kdump kernel will
 * use the same range because it will remain configured in the northbridge.
 * Trying to dump this area via /proc/vmcore may crash the machine, so exclude
 * it from vmcore.
 */
static unsigned long aperture_pfn_start, aperture_page_count;

static int gart_mem_pfn_is_ram(unsigned long pfn)
{
	return likely((pfn < aperture_pfn_start) ||
		      (pfn >= aperture_pfn_start + aperture_page_count));
}

#ifdef CONFIG_PROC_VMCORE
static bool gart_oldmem_pfn_is_ram(struct vmcore_cb *cb, unsigned long pfn)
{
	return !!gart_mem_pfn_is_ram(pfn);
}

static struct vmcore_cb gart_vmcore_cb = {
	.pfn_is_ram = gart_oldmem_pfn_is_ram,
};
#endif

static void __init exclude_from_core(u64 aper_base, u32 aper_order)
{
	aperture_pfn_start = aper_base >> PAGE_SHIFT;
	aperture_page_count = (32 * 1024 * 1024) << aper_order >> PAGE_SHIFT;
#ifdef CONFIG_PROC_VMCORE
	register_vmcore_cb(&gart_vmcore_cb);
#endif
#ifdef CONFIG_PROC_KCORE
	WARN_ON(register_mem_pfn_is_ram(&gart_mem_pfn_is_ram));
#endif
}
#else
static void exclude_from_core(u64 aper_base, u32 aper_order)
{
}
#endif

/* This code runs before the PCI subsystem is initialized, so just
   access the northbridge directly. */

static u32 __init allocate_aperture(void)
{
	u32 aper_size;
	unsigned long addr;

	/* aper_size should <= 1G */
	if (fallback_aper_order > 5)
		fallback_aper_order = 5;
	aper_size = (32 * 1024 * 1024) << fallback_aper_order;

	/*
	 * Aperture has to be naturally aligned. This means a 2GB aperture
	 * won't have much chance of finding a place in the lower 4GB of
	 * memory. Unfortunately we cannot move it up because that would
	 * make the IOMMU useless.
	 */
	addr = memblock_phys_alloc_range(aper_size, aper_size,
					 GART_MIN_ADDR, GART_MAX_ADDR);
	if (!addr) {
		pr_err("Cannot allocate aperture memory hole [mem %#010lx-%#010lx] (%uKB)\n",
		       addr, addr + aper_size - 1, aper_size >> 10);
		return 0;
	}
	pr_info("Mapping aperture over RAM [mem %#010lx-%#010lx] (%uKB)\n",
		addr, addr + aper_size - 1, aper_size >> 10);
	register_nosave_region(addr >> PAGE_SHIFT,
			       (addr+aper_size) >> PAGE_SHIFT);

	return (u32)addr;
}


/* Find a PCI capability */
static u32 __init find_cap(int bus, int slot, int func, int cap)
{
	int bytes;
	u8 pos;

	if (!(read_pci_config_16(bus, slot, func, PCI_STATUS) &
						PCI_STATUS_CAP_LIST))
		return 0;

	pos = read_pci_config_byte(bus, slot, func, PCI_CAPABILITY_LIST);
	for (bytes = 0; bytes < 48 && pos >= 0x40; bytes++) {
		u8 id;

		pos &= ~3;
		id = read_pci_config_byte(bus, slot, func, pos+PCI_CAP_LIST_ID);
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pos = read_pci_config_byte(bus, slot, func,
						pos+PCI_CAP_LIST_NEXT);
	}
	return 0;
}

/* Read a standard AGPv3 bridge header */
static u32 __init read_agp(int bus, int slot, int func, int cap, u32 *order)
{
	u32 apsize;
	u32 apsizereg;
	int nbits;
	u32 aper_low, aper_hi;
	u64 aper;
	u32 old_order;

	pr_info("pci 0000:%02x:%02x:%02x: AGP bridge\n", bus, slot, func);
	apsizereg = read_pci_config_16(bus, slot, func, cap + 0x14);
	if (apsizereg == 0xffffffff) {
		pr_err("pci 0000:%02x:%02x.%d: APSIZE unreadable\n",
		       bus, slot, func);
		return 0;
	}

	/* old_order could be the value from NB gart setting */
	old_order = *order;

	apsize = apsizereg & 0xfff;
	/* Some BIOS use weird encodings not in the AGPv3 table. */
	if (apsize & 0xff)
		apsize |= 0xf00;
	nbits = hweight16(apsize);
	*order = 7 - nbits;
	if ((int)*order < 0) /* < 32MB */
		*order = 0;

	aper_low = read_pci_config(bus, slot, func, 0x10);
	aper_hi = read_pci_config(bus, slot, func, 0x14);
	aper = (aper_low & ~((1<<22)-1)) | ((u64)aper_hi << 32);

	/*
	 * On some sick chips, APSIZE is 0. It means it wants 4G
	 * so let double check that order, and lets trust AMD NB settings:
	 */
	pr_info("pci 0000:%02x:%02x.%d: AGP aperture [bus addr %#010Lx-%#010Lx] (old size %uMB)\n",
		bus, slot, func, aper, aper + (32ULL << (old_order + 20)) - 1,
		32 << old_order);
	if (aper + (32ULL<<(20 + *order)) > 0x100000000ULL) {
		pr_info("pci 0000:%02x:%02x.%d: AGP aperture size %uMB (APSIZE %#x) is not right, using settings from NB\n",
			bus, slot, func, 32 << *order, apsizereg);
		*order = old_order;
	}

	pr_info("pci 0000:%02x:%02x.%d: AGP aperture [bus addr %#010Lx-%#010Lx] (%uMB, APSIZE %#x)\n",
		bus, slot, func, aper, aper + (32ULL << (*order + 20)) - 1,
		32 << *order, apsizereg);

	if (!aperture_valid(aper, (32*1024*1024) << *order, 32<<20))
		return 0;
	return (u32)aper;
}

/*
 * Look for an AGP bridge. Windows only expects the aperture in the
 * AGP bridge and some BIOS forget to initialize the Northbridge too.
 * Work around this here.
 *
 * Do an PCI bus scan by hand because we're running before the PCI
 * subsystem.
 *
 * All AMD AGP bridges are AGPv3 compliant, so we can do this scan
 * generically. It's probably overkill to always scan all slots because
 * the AGP bridges should be always an own bus on the HT hierarchy,
 * but do it here for future safety.
 */
static u32 __init search_agp_bridge(u32 *order, int *valid_agp)
{
	int bus, slot, func;

	/* Poor man's PCI discovery */
	for (bus = 0; bus < 256; bus++) {
		for (slot = 0; slot < 32; slot++) {
			for (func = 0; func < 8; func++) {
				u32 class, cap;
				u8 type;
				class = read_pci_config(bus, slot, func,
							PCI_CLASS_REVISION);
				if (class == 0xffffffff)
					break;

				switch (class >> 16) {
				case PCI_CLASS_BRIDGE_HOST:
				case PCI_CLASS_BRIDGE_OTHER: /* needed? */
					/* AGP bridge? */
					cap = find_cap(bus, slot, func,
							PCI_CAP_ID_AGP);
					if (!cap)
						break;
					*valid_agp = 1;
					return read_agp(bus, slot, func, cap,
							order);
				}

				type = read_pci_config_byte(bus, slot, func,
							       PCI_HEADER_TYPE);
				if (!(type & PCI_HEADER_TYPE_MFD))
					break;
			}
		}
	}
	pr_info("No AGP bridge found\n");

	return 0;
}

static bool gart_fix_e820 __initdata = true;

static int __init parse_gart_mem(char *p)
{
	return kstrtobool(p, &gart_fix_e820);
}
early_param("gart_fix_e820", parse_gart_mem);

/*
 * With kexec/kdump, if the first kernel doesn't shut down the GART and the
 * second kernel allocates a different GART region, there might be two
 * overlapping GART regions present:
 *
 * - the first still used by the GART initialized in the first kernel.
 * - (sub-)set of it used as normal RAM by the second kernel.
 *
 * which leads to memory corruptions and a kernel panic eventually.
 *
 * This can also happen if the BIOS has forgotten to mark the GART region
 * as reserved.
 *
 * Try to update the e820 map to mark that new region as reserved.
 */
void __init early_gart_iommu_check(void)
{
	u32 agp_aper_order = 0;
	int i, fix, slot, valid_agp = 0;
	u32 ctl;
	u32 aper_size = 0, aper_order = 0, last_aper_order = 0;
	u64 aper_base = 0, last_aper_base = 0;
	int aper_enabled = 0, last_aper_enabled = 0, last_valid = 0;

	if (!amd_gart_present())
		return;

	if (!early_pci_allowed())
		return;

	/* This is mostly duplicate of iommu_hole_init */
	search_agp_bridge(&agp_aper_order, &valid_agp);

	fix = 0;
	for (i = 0; amd_nb_bus_dev_ranges[i].dev_limit; i++) {
		int bus;
		int dev_base, dev_limit;

		bus = amd_nb_bus_dev_ranges[i].bus;
		dev_base = amd_nb_bus_dev_ranges[i].dev_base;
		dev_limit = amd_nb_bus_dev_ranges[i].dev_limit;

		for (slot = dev_base; slot < dev_limit; slot++) {
			if (!early_is_amd_nb(read_pci_config(bus, slot, 3, 0x00)))
				continue;

			ctl = read_pci_config(bus, slot, 3, AMD64_GARTAPERTURECTL);
			aper_enabled = ctl & GARTEN;
			aper_order = (ctl >> 1) & 7;
			aper_size = (32 * 1024 * 1024) << aper_order;
			aper_base = read_pci_config(bus, slot, 3, AMD64_GARTAPERTUREBASE) & 0x7fff;
			aper_base <<= 25;

			if (last_valid) {
				if ((aper_order != last_aper_order) ||
				    (aper_base != last_aper_base) ||
				    (aper_enabled != last_aper_enabled)) {
					fix = 1;
					break;
				}
			}

			last_aper_order = aper_order;
			last_aper_base = aper_base;
			last_aper_enabled = aper_enabled;
			last_valid = 1;
		}
	}

	if (!fix && !aper_enabled)
		return;

	if (!aper_base || !aper_size || aper_base + aper_size > 0x100000000UL)
		fix = 1;

	if (gart_fix_e820 && !fix && aper_enabled) {
		if (e820__mapped_any(aper_base, aper_base + aper_size,
				    E820_TYPE_RAM)) {
			/* reserve it, so we can reuse it in second kernel */
			pr_info("e820: reserve [mem %#010Lx-%#010Lx] for GART\n",
				aper_base, aper_base + aper_size - 1);
			e820__range_add(aper_base, aper_size, E820_TYPE_RESERVED);
			e820__update_table_print();
		}
	}

	if (valid_agp)
		return;

	/* disable them all at first */
	for (i = 0; i < amd_nb_bus_dev_ranges[i].dev_limit; i++) {
		int bus;
		int dev_base, dev_limit;

		bus = amd_nb_bus_dev_ranges[i].bus;
		dev_base = amd_nb_bus_dev_ranges[i].dev_base;
		dev_limit = amd_nb_bus_dev_ranges[i].dev_limit;

		for (slot = dev_base; slot < dev_limit; slot++) {
			if (!early_is_amd_nb(read_pci_config(bus, slot, 3, 0x00)))
				continue;

			ctl = read_pci_config(bus, slot, 3, AMD64_GARTAPERTURECTL);
			ctl &= ~GARTEN;
			write_pci_config(bus, slot, 3, AMD64_GARTAPERTURECTL, ctl);
		}
	}

}

static int __initdata printed_gart_size_msg;

void __init gart_iommu_hole_init(void)
{
	u32 agp_aper_base = 0, agp_aper_order = 0;
	u32 aper_size, aper_alloc = 0, aper_order = 0, last_aper_order = 0;
	u64 aper_base, last_aper_base = 0;
	int fix, slot, valid_agp = 0;
	int i, node;

	if (!amd_gart_present())
		return;

	if (gart_iommu_aperture_disabled || !fix_aperture ||
	    !early_pci_allowed())
		return;

	pr_info("Checking aperture...\n");

	if (!fallback_aper_force)
		agp_aper_base = search_agp_bridge(&agp_aper_order, &valid_agp);

	fix = 0;
	node = 0;
	for (i = 0; i < amd_nb_bus_dev_ranges[i].dev_limit; i++) {
		int bus;
		int dev_base, dev_limit;
		u32 ctl;

		bus = amd_nb_bus_dev_ranges[i].bus;
		dev_base = amd_nb_bus_dev_ranges[i].dev_base;
		dev_limit = amd_nb_bus_dev_ranges[i].dev_limit;

		for (slot = dev_base; slot < dev_limit; slot++) {
			if (!early_is_amd_nb(read_pci_config(bus, slot, 3, 0x00)))
				continue;

			iommu_detected = 1;
			gart_iommu_aperture = 1;
			x86_init.iommu.iommu_init = gart_iommu_init;

			ctl = read_pci_config(bus, slot, 3,
					      AMD64_GARTAPERTURECTL);

			/*
			 * Before we do anything else disable the GART. It may
			 * still be enabled if we boot into a crash-kernel here.
			 * Reconfiguring the GART while it is enabled could have
			 * unknown side-effects.
			 */
			ctl &= ~GARTEN;
			write_pci_config(bus, slot, 3, AMD64_GARTAPERTURECTL, ctl);

			aper_order = (ctl >> 1) & 7;
			aper_size = (32 * 1024 * 1024) << aper_order;
			aper_base = read_pci_config(bus, slot, 3, AMD64_GARTAPERTUREBASE) & 0x7fff;
			aper_base <<= 25;

			pr_info("Node %d: aperture [bus addr %#010Lx-%#010Lx] (%uMB)\n",
				node, aper_base, aper_base + aper_size - 1,
				aper_size >> 20);
			node++;

			if (!aperture_valid(aper_base, aper_size, 64<<20)) {
				if (valid_agp && agp_aper_base &&
				    agp_aper_base == aper_base &&
				    agp_aper_order == aper_order) {
					/* the same between two setting from NB and agp */
					if (!no_iommu &&
					    max_pfn > MAX_DMA32_PFN &&
					    !printed_gart_size_msg) {
						pr_err("you are using iommu with agp, but GART size is less than 64MB\n");
						pr_err("please increase GART size in your BIOS setup\n");
						pr_err("if BIOS doesn't have that option, contact your HW vendor!\n");
						printed_gart_size_msg = 1;
					}
				} else {
					fix = 1;
					goto out;
				}
			}

			if ((last_aper_order && aper_order != last_aper_order) ||
			    (last_aper_base && aper_base != last_aper_base)) {
				fix = 1;
				goto out;
			}
			last_aper_order = aper_order;
			last_aper_base = aper_base;
		}
	}

out:
	if (!fix && !fallback_aper_force) {
		if (last_aper_base) {
			/*
			 * If this is the kdump kernel, the first kernel
			 * may have allocated the range over its e820 RAM
			 * and fixed up the northbridge
			 */
			exclude_from_core(last_aper_base, last_aper_order);
		}
		return;
	}

	if (!fallback_aper_force) {
		aper_alloc = agp_aper_base;
		aper_order = agp_aper_order;
	}

	if (aper_alloc) {
		/* Got the aperture from the AGP bridge */
	} else if ((!no_iommu && max_pfn > MAX_DMA32_PFN) ||
		   force_iommu ||
		   valid_agp ||
		   fallback_aper_force) {
		pr_info("Your BIOS doesn't leave an aperture memory hole\n");
		pr_info("Please enable the IOMMU option in the BIOS setup\n");
		pr_info("This costs you %dMB of RAM\n",
			32 << fallback_aper_order);

		aper_order = fallback_aper_order;
		aper_alloc = allocate_aperture();
		if (!aper_alloc) {
			/*
			 * Could disable AGP and IOMMU here, but it's
			 * probably not worth it. But the later users
			 * cannot deal with bad apertures and turning
			 * on the aperture over memory causes very
			 * strange problems, so it's better to panic
			 * early.
			 */
			panic("Not enough memory for aperture");
		}
	} else {
		return;
	}

	/*
	 * If this is the kdump kernel _and_ the first kernel did not
	 * configure the aperture in the northbridge, this range may
	 * overlap with the first kernel's memory. We can't access the
	 * range through vmcore even though it should be part of the dump.
	 */
	exclude_from_core(aper_alloc, aper_order);

	/* Fix up the north bridges */
	for (i = 0; i < amd_nb_bus_dev_ranges[i].dev_limit; i++) {
		int bus, dev_base, dev_limit;

		/*
		 * Don't enable translation yet but enable GART IO and CPU
		 * accesses and set DISTLBWALKPRB since GART table memory is UC.
		 */
		u32 ctl = aper_order << 1;

		bus = amd_nb_bus_dev_ranges[i].bus;
		dev_base = amd_nb_bus_dev_ranges[i].dev_base;
		dev_limit = amd_nb_bus_dev_ranges[i].dev_limit;
		for (slot = dev_base; slot < dev_limit; slot++) {
			if (!early_is_amd_nb(read_pci_config(bus, slot, 3, 0x00)))
				continue;

			write_pci_config(bus, slot, 3, AMD64_GARTAPERTURECTL, ctl);
			write_pci_config(bus, slot, 3, AMD64_GARTAPERTUREBASE, aper_alloc >> 25);
		}
	}

	set_up_gart_resume(aper_order, aper_alloc);
}
