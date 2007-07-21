/* 
 * Firmware replacement code.
 * 
 * Work around broken BIOSes that don't set an aperture or only set the
 * aperture in the AGP bridge. 
 * If all fails map the aperture over some low memory.  This is cheaper than 
 * doing bounce buffering. The memory is lost. This is done at early boot 
 * because only the bootmem allocator can allocate 32+MB. 
 * 
 * Copyright 2002 Andi Kleen, SuSE Labs.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/pci_ids.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/ioport.h>
#include <asm/e820.h>
#include <asm/io.h>
#include <asm/iommu.h>
#include <asm/pci-direct.h>
#include <asm/dma.h>
#include <asm/k8.h>

int iommu_aperture;
int iommu_aperture_disabled __initdata = 0;
int iommu_aperture_allowed __initdata = 0;

int fallback_aper_order __initdata = 1; /* 64MB */
int fallback_aper_force __initdata = 0; 

int fix_aperture __initdata = 1;

static struct resource gart_resource = {
	.name	= "GART",
	.flags	= IORESOURCE_MEM,
};

static void __init insert_aperture_resource(u32 aper_base, u32 aper_size)
{
	gart_resource.start = aper_base;
	gart_resource.end = aper_base + aper_size - 1;
	insert_resource(&iomem_resource, &gart_resource);
}

/* This code runs before the PCI subsystem is initialized, so just
   access the northbridge directly. */

static u32 __init allocate_aperture(void) 
{
	u32 aper_size;
	void *p; 

	if (fallback_aper_order > 7) 
		fallback_aper_order = 7; 
	aper_size = (32 * 1024 * 1024) << fallback_aper_order; 

	/* 
	 * Aperture has to be naturally aligned. This means an 2GB aperture won't
	 * have much chance of finding a place in the lower 4GB of memory.
	 * Unfortunately we cannot move it up because that would make the
	 * IOMMU useless.
	 */
	p = __alloc_bootmem_nopanic(aper_size, aper_size, 0);
	if (!p || __pa(p)+aper_size > 0xffffffff) {
		printk("Cannot allocate aperture memory hole (%p,%uK)\n",
		       p, aper_size>>10);
		if (p)
			free_bootmem(__pa(p), aper_size);
		return 0;
	}
	printk("Mapping aperture over %d KB of RAM @ %lx\n",
	       aper_size >> 10, __pa(p)); 
	insert_aperture_resource((u32)__pa(p), aper_size);
	return (u32)__pa(p); 
}

static int __init aperture_valid(u64 aper_base, u32 aper_size)
{ 
	if (!aper_base) 
		return 0;
	if (aper_size < 64*1024*1024) { 
		printk("Aperture too small (%d MB)\n", aper_size>>20);
		return 0;
	}
	if (aper_base + aper_size > 0x100000000UL) {
		printk("Aperture beyond 4GB. Ignoring.\n");
		return 0; 
	}
	if (e820_any_mapped(aper_base, aper_base + aper_size, E820_RAM)) {
		printk("Aperture pointing to e820 RAM. Ignoring.\n");
		return 0; 
	} 
	return 1;
} 

/* Find a PCI capability */
static __u32 __init find_cap(int num, int slot, int func, int cap) 
{ 
	u8 pos;
	int bytes;
	if (!(read_pci_config_16(num,slot,func,PCI_STATUS) & PCI_STATUS_CAP_LIST))
		return 0;
	pos = read_pci_config_byte(num,slot,func,PCI_CAPABILITY_LIST);
	for (bytes = 0; bytes < 48 && pos >= 0x40; bytes++) { 
		u8 id;
		pos &= ~3; 
		id = read_pci_config_byte(num,slot,func,pos+PCI_CAP_LIST_ID);
		if (id == 0xff)
			break;
		if (id == cap) 
			return pos; 
		pos = read_pci_config_byte(num,slot,func,pos+PCI_CAP_LIST_NEXT); 
	} 
	return 0;
} 

/* Read a standard AGPv3 bridge header */
static __u32 __init read_agp(int num, int slot, int func, int cap, u32 *order)
{ 
	u32 apsize;
	u32 apsizereg;
	int nbits;
	u32 aper_low, aper_hi;
	u64 aper;

	printk("AGP bridge at %02x:%02x:%02x\n", num, slot, func);
	apsizereg = read_pci_config_16(num,slot,func, cap + 0x14);
	if (apsizereg == 0xffffffff) {
		printk("APSIZE in AGP bridge unreadable\n");
		return 0;
	}

	apsize = apsizereg & 0xfff;
	/* Some BIOS use weird encodings not in the AGPv3 table. */
	if (apsize & 0xff) 
		apsize |= 0xf00; 
	nbits = hweight16(apsize);
	*order = 7 - nbits;
	if ((int)*order < 0) /* < 32MB */
		*order = 0;
	
	aper_low = read_pci_config(num,slot,func, 0x10);
	aper_hi = read_pci_config(num,slot,func,0x14);
	aper = (aper_low & ~((1<<22)-1)) | ((u64)aper_hi << 32);

	printk("Aperture from AGP @ %Lx size %u MB (APSIZE %x)\n", 
	       aper, 32 << *order, apsizereg);

	if (!aperture_valid(aper, (32*1024*1024) << *order))
	    return 0;
	return (u32)aper; 
} 

/* Look for an AGP bridge. Windows only expects the aperture in the
   AGP bridge and some BIOS forget to initialize the Northbridge too.
   Work around this here. 

   Do an PCI bus scan by hand because we're running before the PCI
   subsystem. 

   All K8 AGP bridges are AGPv3 compliant, so we can do this scan
   generically. It's probably overkill to always scan all slots because
   the AGP bridges should be always an own bus on the HT hierarchy, 
   but do it here for future safety. */
static __u32 __init search_agp_bridge(u32 *order, int *valid_agp)
{
	int num, slot, func;

	/* Poor man's PCI discovery */
	for (num = 0; num < 256; num++) { 
		for (slot = 0; slot < 32; slot++) { 
			for (func = 0; func < 8; func++) { 
				u32 class, cap;
				u8 type;
				class = read_pci_config(num,slot,func,
							PCI_CLASS_REVISION);
				if (class == 0xffffffff)
					break; 
				
				switch (class >> 16) { 
				case PCI_CLASS_BRIDGE_HOST:
				case PCI_CLASS_BRIDGE_OTHER: /* needed? */
					/* AGP bridge? */
					cap = find_cap(num,slot,func,PCI_CAP_ID_AGP);
					if (!cap)
						break;
					*valid_agp = 1; 
					return read_agp(num,slot,func,cap,order);
				} 
				
				/* No multi-function device? */
				type = read_pci_config_byte(num,slot,func,
							       PCI_HEADER_TYPE);
				if (!(type & 0x80))
					break;
			} 
		} 
	}
	printk("No AGP bridge found\n"); 
	return 0;
}

void __init iommu_hole_init(void) 
{ 
	int fix, num; 
	u32 aper_size, aper_alloc = 0, aper_order = 0, last_aper_order = 0;
	u64 aper_base, last_aper_base = 0;
	int valid_agp = 0;

	if (iommu_aperture_disabled || !fix_aperture || !early_pci_allowed())
		return;

	printk("Checking aperture...\n"); 

	fix = 0;
	for (num = 24; num < 32; num++) {		
		if (!early_is_k8_nb(read_pci_config(0, num, 3, 0x00)))
			continue;

		iommu_detected = 1;
		iommu_aperture = 1; 

		aper_order = (read_pci_config(0, num, 3, 0x90) >> 1) & 7; 
		aper_size = (32 * 1024 * 1024) << aper_order; 
		aper_base = read_pci_config(0, num, 3, 0x94) & 0x7fff;
		aper_base <<= 25; 

		printk("CPU %d: aperture @ %Lx size %u MB\n", num-24, 
		       aper_base, aper_size>>20);
		
		if (!aperture_valid(aper_base, aper_size)) {
			fix = 1; 
			break; 
		}

		if ((last_aper_order && aper_order != last_aper_order) ||
		    (last_aper_base && aper_base != last_aper_base)) {
			fix = 1;
			break;
		}
		last_aper_order = aper_order;
		last_aper_base = aper_base;
	} 

	if (!fix && !fallback_aper_force) {
		if (last_aper_base) {
			unsigned long n = (32 * 1024 * 1024) << last_aper_order;
			insert_aperture_resource((u32)last_aper_base, n);
		}
		return; 
	}

	if (!fallback_aper_force)
		aper_alloc = search_agp_bridge(&aper_order, &valid_agp); 
		
	if (aper_alloc) { 
		/* Got the aperture from the AGP bridge */
	} else if (swiotlb && !valid_agp) {
		/* Do nothing */
	} else if ((!no_iommu && end_pfn > MAX_DMA32_PFN) ||
		   force_iommu ||
		   valid_agp ||
		   fallback_aper_force) { 
		printk("Your BIOS doesn't leave a aperture memory hole\n");
		printk("Please enable the IOMMU option in the BIOS setup\n");
		printk("This costs you %d MB of RAM\n",
		       32 << fallback_aper_order);

		aper_order = fallback_aper_order;
		aper_alloc = allocate_aperture();
		if (!aper_alloc) { 
			/* Could disable AGP and IOMMU here, but it's probably
			   not worth it. But the later users cannot deal with
			   bad apertures and turning on the aperture over memory
			   causes very strange problems, so it's better to 
			   panic early. */
			panic("Not enough memory for aperture");
		}
	} else { 
		return; 
	} 

	/* Fix up the north bridges */
	for (num = 24; num < 32; num++) { 		
		if (!early_is_k8_nb(read_pci_config(0, num, 3, 0x00)))
			continue;	

		/* Don't enable translation yet. That is done later. 
		   Assume this BIOS didn't initialise the GART so 
		   just overwrite all previous bits */ 
		write_pci_config(0, num, 3, 0x90, aper_order<<1); 
		write_pci_config(0, num, 3, 0x94, aper_alloc>>25); 
	} 
} 
