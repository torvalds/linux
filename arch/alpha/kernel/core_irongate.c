// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/core_irongate.c
 *
 * Based on code written by David A. Rusling (david.rusling@reo.mts.dec.com).
 *
 *	Copyright (C) 1999 Alpha Processor, Inc.,
 *		(David Daniel, Stig Telfer, Soohoon Lee)
 *
 * Code common to all IRONGATE core logic chips.
 */

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_irongate.h>
#undef __EXTERN_INLINE

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/memblock.h>

#include <asm/ptrace.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "pci_impl.h"

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif

igcsr32 *IronECC;

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address accordingly.  It is therefore not safe
 * to have concurrent invocations to configuration space access
 * routines, but there really shouldn't be any need for this.
 *
 *	addr[31:24]		reserved
 *	addr[23:16]		bus number (8 bits = 128 possible buses)
 *	addr[15:11]		Device number (5 bits)
 *	addr[10: 8]		function number
 *	addr[ 7: 2]		register number
 *
 * For IRONGATE:
 *    if (bus = addr[23:16]) == 0
 *    then
 *	  type 0 config cycle:
 *	      addr_on_pci[31:11] = id selection for device = addr[15:11]
 *	      addr_on_pci[10: 2] = addr[10: 2] ???
 *	      addr_on_pci[ 1: 0] = 00
 *    else
 *	  type 1 config cycle (pass on with no decoding):
 *	      addr_on_pci[31:24] = 0
 *	      addr_on_pci[23: 2] = addr[23: 2]
 *	      addr_on_pci[ 1: 0] = 01
 *    fi
 *
 * Notes:
 *	The function number selects which function of a multi-function device
 *	(e.g., SCSI and Ethernet).
 *
 *	The register selects a DWORD (32 bit) register offset.	Hence it
 *	doesn't get shifted by 2 bits as we want to "drop" the bottom two
 *	bits.
 */

static int
mk_conf_addr(struct pci_bus *pbus, unsigned int device_fn, int where,
	     unsigned long *pci_addr, unsigned char *type1)
{
	unsigned long addr;
	u8 bus = pbus->number;

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, "
		 "pci_addr=0x%p, type1=0x%p)\n",
		 bus, device_fn, where, pci_addr, type1));

	*type1 = (bus != 0);

	addr = (bus << 16) | (device_fn << 8) | where;
	addr |= IRONGATE_CONF;

	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static int
irongate_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 *value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, devfn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		*value = __kernel_ldbu(*(vucp)addr);
		break;
	case 2:
		*value = __kernel_ldwu(*(vusp)addr);
		break;
	case 4:
		*value = *(vuip)addr;
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int
irongate_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		      int size, u32 value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, devfn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		__kernel_stb(value, *(vucp)addr);
		mb();
		__kernel_ldbu(*(vucp)addr);
		break;
	case 2:
		__kernel_stw(value, *(vusp)addr);
		mb();
		__kernel_ldwu(*(vusp)addr);
		break;
	case 4:
		*(vuip)addr = value;
		mb();
		*(vuip)addr;
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops irongate_pci_ops =
{
	.read =		irongate_read_config,
	.write =	irongate_write_config,
};

int
irongate_pci_clr_err(void)
{
	unsigned int nmi_ctl=0;
	unsigned int IRONGATE_jd;

again:
	IRONGATE_jd = IRONGATE0->stat_cmd;
	printk("Iron stat_cmd %x\n", IRONGATE_jd);
	IRONGATE0->stat_cmd = IRONGATE_jd; /* write again clears error bits */
	mb();
	IRONGATE_jd = IRONGATE0->stat_cmd;  /* re-read to force write */

	IRONGATE_jd = *IronECC;
	printk("Iron ECC %x\n", IRONGATE_jd);
	*IronECC = IRONGATE_jd; /* write again clears error bits */
	mb();
	IRONGATE_jd = *IronECC;  /* re-read to force write */

	/* Clear ALI NMI */
        nmi_ctl = inb(0x61);
        nmi_ctl |= 0x0c;
        outb(nmi_ctl, 0x61);
        nmi_ctl &= ~0x0c;
        outb(nmi_ctl, 0x61);

	IRONGATE_jd = *IronECC;
	if (IRONGATE_jd & 0x300) goto again;

	return 0;
}

#define IRONGATE_3GB 0xc0000000UL

/* On Albacore (aka UP1500) with 4Gb of RAM we have to reserve some
   memory for PCI. At this point we just reserve memory above 3Gb. Most
   of this memory will be freed after PCI setup is done. */
static void __init
albacore_init_arch(void)
{
	unsigned long memtop = max_low_pfn << PAGE_SHIFT;
	unsigned long pci_mem = (memtop + 0x1000000UL) & ~0xffffffUL;
	struct percpu_struct *cpu;
	int pal_rev, pal_var;

	cpu = (struct percpu_struct*)((char*)hwrpb + hwrpb->processor_offset);
	pal_rev = cpu->pal_revision & 0xffff;
	pal_var = (cpu->pal_revision >> 16) & 0xff;

	/* Consoles earlier than A5.6-18 (OSF PALcode v1.62-2) set up
	   the CPU incorrectly (leave speculative stores enabled),
	   which causes memory corruption under certain conditions.
	   Issue a warning for such consoles. */
	if (alpha_using_srm &&
	    (pal_rev < 0x13e ||	(pal_rev == 0x13e && pal_var < 2)))
		printk(KERN_WARNING "WARNING! Upgrade to SRM A5.6-19 "
				    "or later\n");

	if (pci_mem > IRONGATE_3GB)
		pci_mem = IRONGATE_3GB;
	IRONGATE0->pci_mem = pci_mem;
	alpha_mv.min_mem_address = pci_mem;
	if (memtop > pci_mem) {
#ifdef CONFIG_BLK_DEV_INITRD
		extern unsigned long initrd_start, initrd_end;
		extern void *move_initrd(unsigned long);

		/* Move the initrd out of the way. */
		if (initrd_end && __pa(initrd_end) > pci_mem) {
			unsigned long size;

			size = initrd_end - initrd_start;
			memblock_free((void *)initrd_start, PAGE_ALIGN(size));
			if (!move_initrd(pci_mem))
				printk("irongate_init_arch: initrd too big "
				       "(%ldK)\ndisabling initrd\n",
				       size / 1024);
		}
#endif
		memblock_reserve(pci_mem, memtop - pci_mem);
		printk("irongate_init_arch: temporarily reserving "
			"region %08lx-%08lx for PCI\n", pci_mem, memtop - 1);
	}
}

static void __init
irongate_setup_agp(void)
{
	/* Disable the GART window. AGPGART doesn't work due to yet
	   unresolved memory coherency issues... */
	IRONGATE0->agpva = IRONGATE0->agpva & ~0xf;
	alpha_agpgart_size = 0;
}

void __init
irongate_init_arch(void)
{
	struct pci_controller *hose;
	int amd761 = (IRONGATE0->dev_vendor >> 16) > 0x7006;	/* Albacore? */

	IronECC = amd761 ? &IRONGATE0->bacsr54_eccms761 : &IRONGATE0->dramms;

	irongate_pci_clr_err();

	if (amd761)
		albacore_init_arch();

	irongate_setup_agp();

	/*
	 * Create our single hose.
	 */

	pci_isa_hose = hose = alloc_pci_controller();
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->index = 0;

	/* This is for userland consumption.  For some reason, the 40-bit
	   PIO bias that we use in the kernel through KSEG didn't work for
	   the page table based user mappings.  So make sure we get the
	   43-bit PIO bias.  */
	hose->sparse_mem_base = 0;
	hose->sparse_io_base = 0;
	hose->dense_mem_base
	  = (IRONGATE_MEM & 0xffffffffffUL) | 0x80000000000UL;
	hose->dense_io_base
	  = (IRONGATE_IO & 0xffffffffffUL) | 0x80000000000UL;

	hose->sg_isa = hose->sg_pci = NULL;
	__direct_map_base = 0;
	__direct_map_size = 0xffffffff;
}

/*
 * IO map and AGP support
 */
#include <linux/vmalloc.h>
#include <linux/agp_backend.h>
#include <linux/agpgart.h>
#include <linux/export.h>

#define GET_PAGE_DIR_OFF(addr) (addr >> 22)
#define GET_PAGE_DIR_IDX(addr) (GET_PAGE_DIR_OFF(addr))

#define GET_GATT_OFF(addr) ((addr & 0x003ff000) >> 12) 
#define GET_GATT(addr) (gatt_pages[GET_PAGE_DIR_IDX(addr)])

void __iomem *
irongate_ioremap(unsigned long addr, unsigned long size)
{
	struct vm_struct *area;
	unsigned long vaddr;
	unsigned long baddr, last;
	u32 *mmio_regs, *gatt_pages, *cur_gatt, pte;
	unsigned long gart_bus_addr;

	if (!alpha_agpgart_size)
		return (void __iomem *)(addr + IRONGATE_MEM);

	gart_bus_addr = (unsigned long)IRONGATE0->bar0 &
			PCI_BASE_ADDRESS_MEM_MASK; 

	/* 
	 * Check for within the AGP aperture...
	 */
	do {
		/*
		 * Check the AGP area
		 */
		if (addr >= gart_bus_addr && addr + size - 1 < 
		    gart_bus_addr + alpha_agpgart_size)
			break;

		/*
		 * Not found - assume legacy ioremap
		 */
		return (void __iomem *)(addr + IRONGATE_MEM);
	} while(0);

	mmio_regs = (u32 *)(((unsigned long)IRONGATE0->bar1 &
			PCI_BASE_ADDRESS_MEM_MASK) + IRONGATE_MEM);

	gatt_pages = (u32 *)(phys_to_virt(mmio_regs[1])); /* FIXME */

	/*
	 * Adjust the limits (mappings must be page aligned)
	 */
	if (addr & ~PAGE_MASK) {
		printk("AGP ioremap failed... addr not page aligned (0x%lx)\n",
		       addr);
		return (void __iomem *)(addr + IRONGATE_MEM);
	}
	last = addr + size - 1;
	size = PAGE_ALIGN(last) - addr;

#if 0
	printk("irongate_ioremap(0x%lx, 0x%lx)\n", addr, size);
	printk("irongate_ioremap:  gart_bus_addr  0x%lx\n", gart_bus_addr);
	printk("irongate_ioremap:  gart_aper_size 0x%lx\n", gart_aper_size);
	printk("irongate_ioremap:  mmio_regs      %p\n", mmio_regs);
	printk("irongate_ioremap:  gatt_pages     %p\n", gatt_pages);
	
	for(baddr = addr; baddr <= last; baddr += PAGE_SIZE)
	{
		cur_gatt = phys_to_virt(GET_GATT(baddr) & ~1);
		pte = cur_gatt[GET_GATT_OFF(baddr)] & ~1;
		printk("irongate_ioremap:  cur_gatt %p pte 0x%x\n",
		       cur_gatt, pte);
	}
#endif

	/*
	 * Map it
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area) return NULL;

	for(baddr = addr, vaddr = (unsigned long)area->addr; 
	    baddr <= last; 
	    baddr += PAGE_SIZE, vaddr += PAGE_SIZE)
	{
		cur_gatt = phys_to_virt(GET_GATT(baddr) & ~1);
		pte = cur_gatt[GET_GATT_OFF(baddr)] & ~1;

		if (__alpha_remap_area_pages(vaddr,
					     pte, PAGE_SIZE, 0)) {
			printk("AGP ioremap: FAILED to map...\n");
			vfree(area->addr);
			return NULL;
		}
	}

	flush_tlb_all();

	vaddr = (unsigned long)area->addr + (addr & ~PAGE_MASK);
#if 0
	printk("irongate_ioremap(0x%lx, 0x%lx) returning 0x%lx\n",
	       addr, size, vaddr);
#endif
	return (void __iomem *)vaddr;
}
EXPORT_SYMBOL(irongate_ioremap);

void
irongate_iounmap(volatile void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	if (((long)addr >> 41) == -2)
		return;	/* kseg map, nothing to do */
	if (addr)
		return vfree((void *)(PAGE_MASK & addr)); 
}
EXPORT_SYMBOL(irongate_iounmap);
