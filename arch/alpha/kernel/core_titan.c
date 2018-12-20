// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/core_titan.c
 *
 * Code common to all TITAN core logic chips.
 */

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_titan.h>
#undef __EXTERN_INLINE

#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/memblock.h>

#include <asm/ptrace.h>
#include <asm/smp.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/vga.h>

#include "proto.h"
#include "pci_impl.h"

/* Save Titan configuration data as the console had it set up.  */

struct
{
	unsigned long wsba[4];
	unsigned long wsm[4];
	unsigned long tba[4];
} saved_config[4] __attribute__((common));

/*
 * Is PChip 1 present? No need to query it more than once.
 */
static int titan_pchip1_present;

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif


/*
 * Routines to access TIG registers.
 */
static inline volatile unsigned long *
mk_tig_addr(int offset)
{
	return (volatile unsigned long *)(TITAN_TIG_SPACE + (offset << 6));
}

static inline u8 
titan_read_tig(int offset, u8 value)
{
	volatile unsigned long *tig_addr = mk_tig_addr(offset);
	return (u8)(*tig_addr & 0xff);
}

static inline void 
titan_write_tig(int offset, u8 value)
{
	volatile unsigned long *tig_addr = mk_tig_addr(offset);
	*tig_addr = (unsigned long)value;
}


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Note that all config space accesses use Type 1 address format.
 *
 * Note also that type 1 is determined by non-zero bus number.
 *
 * Type 1:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | |B|B|B|B|B|B|B|B|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|1|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:24	reserved
 *	23:16	bus number (8 bits = 128 possible buses)
 *	15:11	Device number (5 bits)
 *	10:8	function number
 *	 7:2	register number
 *  
 * Notes:
 *	The function number selects which function of a multi-function device 
 *	(e.g., SCSI and Ethernet).
 * 
 *	The register selects a DWORD (32 bit) register offset.  Hence it
 *	doesn't get shifted by 2 bits as we want to "drop" the bottom two
 *	bits.
 */

static int
mk_conf_addr(struct pci_bus *pbus, unsigned int device_fn, int where,
	     unsigned long *pci_addr, unsigned char *type1)
{
	struct pci_controller *hose = pbus->sysdata;
	unsigned long addr;
	u8 bus = pbus->number;

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, "
		 "pci_addr=0x%p, type1=0x%p)\n",
		 bus, device_fn, where, pci_addr, type1));

	if (!pbus->parent) /* No parent means peer PCI bus. */
		bus = 0;
        *type1 = (bus != 0);

        addr = (bus << 16) | (device_fn << 8) | where;
	addr |= hose->config_space_base;
		
	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static int
titan_read_config(struct pci_bus *bus, unsigned int devfn, int where,
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
titan_write_config(struct pci_bus *bus, unsigned int devfn, int where,
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

struct pci_ops titan_pci_ops = 
{
	.read =		titan_read_config,
	.write =	titan_write_config,
};


void
titan_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	titan_pachip *pachip = 
	  (hose->index & 1) ? TITAN_pachip1 : TITAN_pachip0;
	titan_pachip_port *port;
	volatile unsigned long *csr;
	unsigned long value;

	/* Get the right hose.  */
	port = &pachip->g_port;
	if (hose->index & 2) 
		port = &pachip->a_port;

	/* We can invalidate up to 8 tlb entries in a go.  The flush
	   matches against <31:16> in the pci address.  
	   Note that gtlbi* and atlbi* are in the same place in the g_port
	   and a_port, respectively, so the g_port offset can be used
	   even if hose is an a_port */
	csr = &port->port_specific.g.gtlbia.csr;
	if (((start ^ end) & 0xffff0000) == 0)
		csr = &port->port_specific.g.gtlbiv.csr;

	/* For TBIA, it doesn't matter what value we write.  For TBI, 
	   it's the shifted tag bits.  */
	value = (start & 0xffff0000) >> 12;

	wmb();
	*csr = value;
	mb();
	*csr;
}

static int
titan_query_agp(titan_pachip_port *port)
{
	union TPAchipPCTL pctl;

	/* set up APCTL */
	pctl.pctl_q_whole = port->pctl.csr;

	return pctl.pctl_r_bits.apctl_v_agp_present;

}

static void __init
titan_init_one_pachip_port(titan_pachip_port *port, int index)
{
	struct pci_controller *hose;

	hose = alloc_pci_controller();
	if (index == 0)
		pci_isa_hose = hose;
	hose->io_space = alloc_resource();
	hose->mem_space = alloc_resource();

	/*
	 * This is for userland consumption.  The 40-bit PIO bias that we 
	 * use in the kernel through KSEG doesn't work in the page table 
	 * based user mappings. (43-bit KSEG sign extends the physical
	 * address from bit 40 to hit the I/O bit - mapped addresses don't).
	 * So make sure we get the 43-bit PIO bias.  
	 */
	hose->sparse_mem_base = 0;
	hose->sparse_io_base = 0;
	hose->dense_mem_base
	  = (TITAN_MEM(index) & 0xffffffffffUL) | 0x80000000000UL;
	hose->dense_io_base
	  = (TITAN_IO(index) & 0xffffffffffUL) | 0x80000000000UL;

	hose->config_space_base = TITAN_CONF(index);
	hose->index = index;

	hose->io_space->start = TITAN_IO(index) - TITAN_IO_BIAS;
	hose->io_space->end = hose->io_space->start + TITAN_IO_SPACE - 1;
	hose->io_space->name = pci_io_names[index];
	hose->io_space->flags = IORESOURCE_IO;

	hose->mem_space->start = TITAN_MEM(index) - TITAN_MEM_BIAS;
	hose->mem_space->end = hose->mem_space->start + 0xffffffff;
	hose->mem_space->name = pci_mem_names[index];
	hose->mem_space->flags = IORESOURCE_MEM;

	if (request_resource(&ioport_resource, hose->io_space) < 0)
		printk(KERN_ERR "Failed to request IO on hose %d\n", index);
	if (request_resource(&iomem_resource, hose->mem_space) < 0)
		printk(KERN_ERR "Failed to request MEM on hose %d\n", index);

	/*
	 * Save the existing PCI window translations.  SRM will 
	 * need them when we go to reboot.
	 */
	saved_config[index].wsba[0] = port->wsba[0].csr;
	saved_config[index].wsm[0]  = port->wsm[0].csr;
	saved_config[index].tba[0]  = port->tba[0].csr;

	saved_config[index].wsba[1] = port->wsba[1].csr;
	saved_config[index].wsm[1]  = port->wsm[1].csr;
	saved_config[index].tba[1]  = port->tba[1].csr;

	saved_config[index].wsba[2] = port->wsba[2].csr;
	saved_config[index].wsm[2]  = port->wsm[2].csr;
	saved_config[index].tba[2]  = port->tba[2].csr;

	saved_config[index].wsba[3] = port->wsba[3].csr;
	saved_config[index].wsm[3]  = port->wsm[3].csr;
	saved_config[index].tba[3]  = port->tba[3].csr;

	/*
	 * Set up the PCI to main memory translation windows.
	 *
	 * Note: Window 3 on Titan is Scatter-Gather ONLY.
	 *
	 * Window 0 is scatter-gather 8MB at 8MB (for isa)
	 * Window 1 is direct access 1GB at 2GB
	 * Window 2 is scatter-gather 1GB at 3GB
	 */
	hose->sg_isa = iommu_arena_new(hose, 0x00800000, 0x00800000,
				       SMP_CACHE_BYTES);
	hose->sg_isa->align_entry = 8; /* 64KB for ISA */

	hose->sg_pci = iommu_arena_new(hose, 0xc0000000, 0x40000000,
				       SMP_CACHE_BYTES);
	hose->sg_pci->align_entry = 4; /* Titan caches 4 PTEs at a time */

	port->wsba[0].csr = hose->sg_isa->dma_base | 3;
	port->wsm[0].csr  = (hose->sg_isa->size - 1) & 0xfff00000;
	port->tba[0].csr  = virt_to_phys(hose->sg_isa->ptes);

	port->wsba[1].csr = __direct_map_base | 1;
	port->wsm[1].csr  = (__direct_map_size - 1) & 0xfff00000;
	port->tba[1].csr  = 0;

	port->wsba[2].csr = hose->sg_pci->dma_base | 3;
	port->wsm[2].csr  = (hose->sg_pci->size - 1) & 0xfff00000;
	port->tba[2].csr  = virt_to_phys(hose->sg_pci->ptes);

	port->wsba[3].csr = 0;

	/* Enable the Monster Window to make DAC pci64 possible.  */
	port->pctl.csr |= pctl_m_mwin;

	/*
	 * If it's an AGP port, initialize agplastwr.
	 */
	if (titan_query_agp(port)) 
		port->port_specific.a.agplastwr.csr = __direct_map_base;

	titan_pci_tbi(hose, 0, -1);
}

static void __init
titan_init_pachips(titan_pachip *pachip0, titan_pachip *pachip1)
{
	titan_pchip1_present = TITAN_cchip->csc.csr & 1L<<14;

	/* Init the ports in hose order... */
	titan_init_one_pachip_port(&pachip0->g_port, 0);	/* hose 0 */
	if (titan_pchip1_present)
		titan_init_one_pachip_port(&pachip1->g_port, 1);/* hose 1 */
	titan_init_one_pachip_port(&pachip0->a_port, 2);	/* hose 2 */
	if (titan_pchip1_present)
		titan_init_one_pachip_port(&pachip1->a_port, 3);/* hose 3 */
}

void __init
titan_init_arch(void)
{
#if 0
	printk("%s: titan_init_arch()\n", __func__);
	printk("%s: CChip registers:\n", __func__);
	printk("%s: CSR_CSC 0x%lx\n", __func__, TITAN_cchip->csc.csr);
	printk("%s: CSR_MTR 0x%lx\n", __func__, TITAN_cchip->mtr.csr);
	printk("%s: CSR_MISC 0x%lx\n", __func__, TITAN_cchip->misc.csr);
	printk("%s: CSR_DIM0 0x%lx\n", __func__, TITAN_cchip->dim0.csr);
	printk("%s: CSR_DIM1 0x%lx\n", __func__, TITAN_cchip->dim1.csr);
	printk("%s: CSR_DIR0 0x%lx\n", __func__, TITAN_cchip->dir0.csr);
	printk("%s: CSR_DIR1 0x%lx\n", __func__, TITAN_cchip->dir1.csr);
	printk("%s: CSR_DRIR 0x%lx\n", __func__, TITAN_cchip->drir.csr);

	printk("%s: DChip registers:\n", __func__);
	printk("%s: CSR_DSC 0x%lx\n", __func__, TITAN_dchip->dsc.csr);
	printk("%s: CSR_STR 0x%lx\n", __func__, TITAN_dchip->str.csr);
	printk("%s: CSR_DREV 0x%lx\n", __func__, TITAN_dchip->drev.csr);
#endif

	boot_cpuid = __hard_smp_processor_id();

	/* With multiple PCI busses, we play with I/O as physical addrs.  */
	ioport_resource.end = ~0UL;
	iomem_resource.end = ~0UL;

	/* PCI DMA Direct Mapping is 1GB at 2GB.  */
	__direct_map_base = 0x80000000;
	__direct_map_size = 0x40000000;

	/* Init the PA chip(s).  */
	titan_init_pachips(TITAN_pachip0, TITAN_pachip1);

	/* Check for graphic console location (if any).  */
	find_console_vga_hose();
}

static void
titan_kill_one_pachip_port(titan_pachip_port *port, int index)
{
	port->wsba[0].csr = saved_config[index].wsba[0];
	port->wsm[0].csr  = saved_config[index].wsm[0];
	port->tba[0].csr  = saved_config[index].tba[0];

	port->wsba[1].csr = saved_config[index].wsba[1];
	port->wsm[1].csr  = saved_config[index].wsm[1];
	port->tba[1].csr  = saved_config[index].tba[1];

	port->wsba[2].csr = saved_config[index].wsba[2];
	port->wsm[2].csr  = saved_config[index].wsm[2];
	port->tba[2].csr  = saved_config[index].tba[2];

	port->wsba[3].csr = saved_config[index].wsba[3];
	port->wsm[3].csr  = saved_config[index].wsm[3];
	port->tba[3].csr  = saved_config[index].tba[3];
}

static void
titan_kill_pachips(titan_pachip *pachip0, titan_pachip *pachip1)
{
	if (titan_pchip1_present) {
		titan_kill_one_pachip_port(&pachip1->g_port, 1);
		titan_kill_one_pachip_port(&pachip1->a_port, 3);
	}
	titan_kill_one_pachip_port(&pachip0->g_port, 0);
	titan_kill_one_pachip_port(&pachip0->a_port, 2);
}

void
titan_kill_arch(int mode)
{
	titan_kill_pachips(TITAN_pachip0, TITAN_pachip1);
}


/*
 * IO map support.
 */

void __iomem *
titan_ioportmap(unsigned long addr)
{
	FIXUP_IOADDR_VGA(addr);
	return (void __iomem *)(addr + TITAN_IO_BIAS);
}


void __iomem *
titan_ioremap(unsigned long addr, unsigned long size)
{
	int h = (addr & TITAN_HOSE_MASK) >> TITAN_HOSE_SHIFT;
	unsigned long baddr = addr & ~TITAN_HOSE_MASK;
	unsigned long last = baddr + size - 1;
	struct pci_controller *hose;	
	struct vm_struct *area;
	unsigned long vaddr;
	unsigned long *ptes;
	unsigned long pfn;

#ifdef CONFIG_VGA_HOSE
	/*
	 * Adjust the address and hose, if necessary.
	 */ 
	if (pci_vga_hose && __is_mem_vga(addr)) {
		h = pci_vga_hose->index;
		addr += pci_vga_hose->mem_space->start;
	}
#endif

	/*
	 * Find the hose.
	 */
	for (hose = hose_head; hose; hose = hose->next)
		if (hose->index == h)
			break;
	if (!hose)
		return NULL;

	/*
	 * Is it direct-mapped?
	 */
	if ((baddr >= __direct_map_base) && 
	    ((baddr + size - 1) < __direct_map_base + __direct_map_size)) {
		vaddr = addr - __direct_map_base + TITAN_MEM_BIAS;
		return (void __iomem *) vaddr;
	}

	/* 
	 * Check the scatter-gather arena.
	 */
	if (hose->sg_pci &&
	    baddr >= (unsigned long)hose->sg_pci->dma_base &&
	    last < (unsigned long)hose->sg_pci->dma_base + hose->sg_pci->size){

		/*
		 * Adjust the limits (mappings must be page aligned)
		 */
		baddr -= hose->sg_pci->dma_base;
		last -= hose->sg_pci->dma_base;
		baddr &= PAGE_MASK;
		size = PAGE_ALIGN(last) - baddr;

		/*
		 * Map it
		 */
		area = get_vm_area(size, VM_IOREMAP);
		if (!area) {
			printk("ioremap failed... no vm_area...\n");
			return NULL;
		}

		ptes = hose->sg_pci->ptes;
		for (vaddr = (unsigned long)area->addr; 
		    baddr <= last; 
		    baddr += PAGE_SIZE, vaddr += PAGE_SIZE) {
			pfn = ptes[baddr >> PAGE_SHIFT];
			if (!(pfn & 1)) {
				printk("ioremap failed... pte not valid...\n");
				vfree(area->addr);
				return NULL;
			}
			pfn >>= 1;	/* make it a true pfn */
			
			if (__alpha_remap_area_pages(vaddr,
						     pfn << PAGE_SHIFT, 
						     PAGE_SIZE, 0)) {
				printk("FAILED to remap_area_pages...\n");
				vfree(area->addr);
				return NULL;
			}
		}

		flush_tlb_all();

		vaddr = (unsigned long)area->addr + (addr & ~PAGE_MASK);
		return (void __iomem *) vaddr;
	}

	/* Assume a legacy (read: VGA) address, and return appropriately. */
	return (void __iomem *)(addr + TITAN_MEM_BIAS);
}

void
titan_iounmap(volatile void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;
	if (addr >= VMALLOC_START)
		vfree((void *)(PAGE_MASK & addr)); 
}

int
titan_is_mmio(const volatile void __iomem *xaddr)
{
	unsigned long addr = (unsigned long) xaddr;

	if (addr >= VMALLOC_START)
		return 1;
	else
		return (addr & 0x100000000UL) == 0;
}

#ifndef CONFIG_ALPHA_GENERIC
EXPORT_SYMBOL(titan_ioportmap);
EXPORT_SYMBOL(titan_ioremap);
EXPORT_SYMBOL(titan_iounmap);
EXPORT_SYMBOL(titan_is_mmio);
#endif

/*
 * AGP GART Support.
 */
#include <linux/agp_backend.h>
#include <asm/agp_backend.h>
#include <linux/slab.h>
#include <linux/delay.h>

struct titan_agp_aperture {
	struct pci_iommu_arena *arena;
	long pg_start;
	long pg_count;
};

static int
titan_agp_setup(alpha_agp_info *agp)
{
	struct titan_agp_aperture *aper;

	if (!alpha_agpgart_size)
		return -ENOMEM;

	aper = kmalloc(sizeof(struct titan_agp_aperture), GFP_KERNEL);
	if (aper == NULL)
		return -ENOMEM;

	aper->arena = agp->hose->sg_pci;
	aper->pg_count = alpha_agpgart_size / PAGE_SIZE;
	aper->pg_start = iommu_reserve(aper->arena, aper->pg_count,
				       aper->pg_count - 1);
	if (aper->pg_start < 0) {
		printk(KERN_ERR "Failed to reserve AGP memory\n");
		kfree(aper);
		return -ENOMEM;
	}

	agp->aperture.bus_base = 
		aper->arena->dma_base + aper->pg_start * PAGE_SIZE;
	agp->aperture.size = aper->pg_count * PAGE_SIZE;
	agp->aperture.sysdata = aper;

	return 0;
}

static void
titan_agp_cleanup(alpha_agp_info *agp)
{
	struct titan_agp_aperture *aper = agp->aperture.sysdata;
	int status;

	status = iommu_release(aper->arena, aper->pg_start, aper->pg_count);
	if (status == -EBUSY) {
		printk(KERN_WARNING 
		       "Attempted to release bound AGP memory - unbinding\n");
		iommu_unbind(aper->arena, aper->pg_start, aper->pg_count);
		status = iommu_release(aper->arena, aper->pg_start, 
				       aper->pg_count);
	}
	if (status < 0)
		printk(KERN_ERR "Failed to release AGP memory\n");

	kfree(aper);
	kfree(agp);
}

static int
titan_agp_configure(alpha_agp_info *agp)
{
	union TPAchipPCTL pctl;
	titan_pachip_port *port = agp->private;
	pctl.pctl_q_whole = port->pctl.csr;

	/* Side-Band Addressing? */
	pctl.pctl_r_bits.apctl_v_agp_sba_en = agp->mode.bits.sba;

	/* AGP Rate? */
	pctl.pctl_r_bits.apctl_v_agp_rate = 0;		/* 1x */
	if (agp->mode.bits.rate & 2) 
		pctl.pctl_r_bits.apctl_v_agp_rate = 1;	/* 2x */
#if 0
	if (agp->mode.bits.rate & 4) 
		pctl.pctl_r_bits.apctl_v_agp_rate = 2;	/* 4x */
#endif
	
	/* RQ Depth? */
	pctl.pctl_r_bits.apctl_v_agp_hp_rd = 2;
	pctl.pctl_r_bits.apctl_v_agp_lp_rd = 7;

	/*
	 * AGP Enable.
	 */
	pctl.pctl_r_bits.apctl_v_agp_en = agp->mode.bits.enable;

	/* Tell the user.  */
	printk("Enabling AGP: %dX%s\n", 
	       1 << pctl.pctl_r_bits.apctl_v_agp_rate,
	       pctl.pctl_r_bits.apctl_v_agp_sba_en ? " - SBA" : "");
	       
	/* Write it.  */
	port->pctl.csr = pctl.pctl_q_whole;
	
	/* And wait at least 5000 66MHz cycles (per Titan spec).  */
	udelay(100);

	return 0;
}

static int 
titan_agp_bind_memory(alpha_agp_info *agp, off_t pg_start, struct agp_memory *mem)
{
	struct titan_agp_aperture *aper = agp->aperture.sysdata;
	return iommu_bind(aper->arena, aper->pg_start + pg_start, 
			  mem->page_count, mem->pages);
}

static int 
titan_agp_unbind_memory(alpha_agp_info *agp, off_t pg_start, struct agp_memory *mem)
{
	struct titan_agp_aperture *aper = agp->aperture.sysdata;
	return iommu_unbind(aper->arena, aper->pg_start + pg_start,
			    mem->page_count);
}

static unsigned long
titan_agp_translate(alpha_agp_info *agp, dma_addr_t addr)
{
	struct titan_agp_aperture *aper = agp->aperture.sysdata;
	unsigned long baddr = addr - aper->arena->dma_base;
	unsigned long pte;

	if (addr < agp->aperture.bus_base ||
	    addr >= agp->aperture.bus_base + agp->aperture.size) {
		printk("%s: addr out of range\n", __func__);
		return -EINVAL;
	}

	pte = aper->arena->ptes[baddr >> PAGE_SHIFT];
	if (!(pte & 1)) {
		printk("%s: pte not valid\n", __func__);
		return -EINVAL;
	}

	return (pte >> 1) << PAGE_SHIFT;
}

struct alpha_agp_ops titan_agp_ops =
{
	.setup		= titan_agp_setup,
	.cleanup	= titan_agp_cleanup,
	.configure	= titan_agp_configure,
	.bind		= titan_agp_bind_memory,
	.unbind		= titan_agp_unbind_memory,
	.translate	= titan_agp_translate
};

alpha_agp_info *
titan_agp_info(void)
{
	alpha_agp_info *agp;
	struct pci_controller *hose;
	titan_pachip_port *port;
	int hosenum = -1;
	union TPAchipPCTL pctl;

	/*
	 * Find the AGP port.
	 */
	port = &TITAN_pachip0->a_port;
	if (titan_query_agp(port))
		hosenum = 2;
	if (hosenum < 0 && 
	    titan_pchip1_present &&
	    titan_query_agp(port = &TITAN_pachip1->a_port)) 
		hosenum = 3;
	
	/*
	 * Find the hose the port is on.
	 */
	for (hose = hose_head; hose; hose = hose->next)
		if (hose->index == hosenum)
			break;

	if (!hose || !hose->sg_pci)
		return NULL;

	/*
	 * Allocate the info structure.
	 */
	agp = kmalloc(sizeof(*agp), GFP_KERNEL);
	if (!agp)
		return NULL;

	/*
	 * Fill it in.
	 */
	agp->hose = hose;
	agp->private = port;
	agp->ops = &titan_agp_ops;

	/*
	 * Aperture - not configured until ops.setup().
	 *
	 * FIXME - should we go ahead and allocate it here?
	 */
	agp->aperture.bus_base = 0;
	agp->aperture.size = 0;
	agp->aperture.sysdata = NULL;

	/*
	 * Capabilities.
	 */
	agp->capability.lw = 0;
	agp->capability.bits.rate = 3; 	/* 2x, 1x */
	agp->capability.bits.sba = 1;
	agp->capability.bits.rq = 7;	/* 8 - 1 */

	/*
	 * Mode.
	 */
	pctl.pctl_q_whole = port->pctl.csr;
	agp->mode.lw = 0;
	agp->mode.bits.rate = 1 << pctl.pctl_r_bits.apctl_v_agp_rate;
	agp->mode.bits.sba = pctl.pctl_r_bits.apctl_v_agp_sba_en;
	agp->mode.bits.rq = 7;	/* RQ Depth? */
	agp->mode.bits.enable = pctl.pctl_r_bits.apctl_v_agp_en;

	return agp;
}
