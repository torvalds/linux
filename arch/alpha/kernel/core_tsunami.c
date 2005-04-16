/*
 *	linux/arch/alpha/kernel/core_tsunami.c
 *
 * Based on code written by David A. Rusling (david.rusling@reo.mts.dec.com).
 *
 * Code common to all TSUNAMI core logic chips.
 */

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_tsunami.h>
#undef __EXTERN_INLINE

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/ptrace.h>
#include <asm/smp.h>

#include "proto.h"
#include "pci_impl.h"

/* Save Tsunami configuration data as the console had it set up.  */

struct 
{
	unsigned long wsba[4];
	unsigned long wsm[4];
	unsigned long tba[4];
} saved_config[2] __attribute__((common));

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the I/O controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

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
tsunami_read_config(struct pci_bus *bus, unsigned int devfn, int where,
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
tsunami_write_config(struct pci_bus *bus, unsigned int devfn, int where,
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

struct pci_ops tsunami_pci_ops = 
{
	.read =		tsunami_read_config,
	.write = 	tsunami_write_config,
};

void
tsunami_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	tsunami_pchip *pchip = hose->index ? TSUNAMI_pchip1 : TSUNAMI_pchip0;
	volatile unsigned long *csr;
	unsigned long value;

	/* We can invalidate up to 8 tlb entries in a go.  The flush
	   matches against <31:16> in the pci address.  */
	csr = &pchip->tlbia.csr;
	if (((start ^ end) & 0xffff0000) == 0)
		csr = &pchip->tlbiv.csr;

	/* For TBIA, it doesn't matter what value we write.  For TBI, 
	   it's the shifted tag bits.  */
	value = (start & 0xffff0000) >> 12;

	*csr = value;
	mb();
	*csr;
}

#ifdef NXM_MACHINE_CHECKS_ON_TSUNAMI
static long __init
tsunami_probe_read(volatile unsigned long *vaddr)
{
	long dont_care, probe_result;
	int cpu = smp_processor_id();
	int s = swpipl(IPL_MCHECK - 1);

	mcheck_taken(cpu) = 0;
	mcheck_expected(cpu) = 1;
	mb();
	dont_care = *vaddr;
	draina();
	mcheck_expected(cpu) = 0;
	probe_result = !mcheck_taken(cpu);
	mcheck_taken(cpu) = 0;
	setipl(s);

	printk("dont_care == 0x%lx\n", dont_care);

	return probe_result;
}

static long __init
tsunami_probe_write(volatile unsigned long *vaddr)
{
	long true_contents, probe_result = 1;

	TSUNAMI_cchip->misc.csr |= (1L << 28); /* clear NXM... */
	true_contents = *vaddr;
	*vaddr = 0;
	draina();
	if (TSUNAMI_cchip->misc.csr & (1L << 28)) {
		int source = (TSUNAMI_cchip->misc.csr >> 29) & 7;
		TSUNAMI_cchip->misc.csr |= (1L << 28); /* ...and unlock NXS. */
		probe_result = 0;
		printk("tsunami_probe_write: unit %d at 0x%016lx\n", source,
		       (unsigned long)vaddr);
	}
	if (probe_result)
		*vaddr = true_contents;
	return probe_result;
}
#else
#define tsunami_probe_read(ADDR) 1
#endif /* NXM_MACHINE_CHECKS_ON_TSUNAMI */

#define FN __FUNCTION__

static void __init
tsunami_init_one_pchip(tsunami_pchip *pchip, int index)
{
	struct pci_controller *hose;

	if (tsunami_probe_read(&pchip->pctl.csr) == 0)
		return;

	hose = alloc_pci_controller();
	if (index == 0)
		pci_isa_hose = hose;
	hose->io_space = alloc_resource();
	hose->mem_space = alloc_resource();

	/* This is for userland consumption.  For some reason, the 40-bit
	   PIO bias that we use in the kernel through KSEG didn't work for
	   the page table based user mappings.  So make sure we get the
	   43-bit PIO bias.  */
	hose->sparse_mem_base = 0;
	hose->sparse_io_base = 0;
	hose->dense_mem_base
	  = (TSUNAMI_MEM(index) & 0xffffffffffL) | 0x80000000000L;
	hose->dense_io_base
	  = (TSUNAMI_IO(index) & 0xffffffffffL) | 0x80000000000L;

	hose->config_space_base = TSUNAMI_CONF(index);
	hose->index = index;

	hose->io_space->start = TSUNAMI_IO(index) - TSUNAMI_IO_BIAS;
	hose->io_space->end = hose->io_space->start + TSUNAMI_IO_SPACE - 1;
	hose->io_space->name = pci_io_names[index];
	hose->io_space->flags = IORESOURCE_IO;

	hose->mem_space->start = TSUNAMI_MEM(index) - TSUNAMI_MEM_BIAS;
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

	saved_config[index].wsba[0] = pchip->wsba[0].csr;
	saved_config[index].wsm[0] = pchip->wsm[0].csr;
	saved_config[index].tba[0] = pchip->tba[0].csr;

	saved_config[index].wsba[1] = pchip->wsba[1].csr;
	saved_config[index].wsm[1] = pchip->wsm[1].csr;
	saved_config[index].tba[1] = pchip->tba[1].csr;

	saved_config[index].wsba[2] = pchip->wsba[2].csr;
	saved_config[index].wsm[2] = pchip->wsm[2].csr;
	saved_config[index].tba[2] = pchip->tba[2].csr;

	saved_config[index].wsba[3] = pchip->wsba[3].csr;
	saved_config[index].wsm[3] = pchip->wsm[3].csr;
	saved_config[index].tba[3] = pchip->tba[3].csr;

	/*
	 * Set up the PCI to main memory translation windows.
	 *
	 * Note: Window 3 is scatter-gather only
	 * 
	 * Window 0 is scatter-gather 8MB at 8MB (for isa)
	 * Window 1 is scatter-gather (up to) 1GB at 1GB
	 * Window 2 is direct access 2GB at 2GB
	 *
	 * NOTE: we need the align_entry settings for Acer devices on ES40,
	 * specifically floppy and IDE when memory is larger than 2GB.
	 */
	hose->sg_isa = iommu_arena_new(hose, 0x00800000, 0x00800000, 0);
	/* Initially set for 4 PTEs, but will be overridden to 64K for ISA. */
        hose->sg_isa->align_entry = 4;

	hose->sg_pci = iommu_arena_new(hose, 0x40000000,
				       size_for_memory(0x40000000), 0);
        hose->sg_pci->align_entry = 4; /* Tsunami caches 4 PTEs at a time */

	__direct_map_base = 0x80000000;
	__direct_map_size = 0x80000000;

	pchip->wsba[0].csr = hose->sg_isa->dma_base | 3;
	pchip->wsm[0].csr  = (hose->sg_isa->size - 1) & 0xfff00000;
	pchip->tba[0].csr  = virt_to_phys(hose->sg_isa->ptes);

	pchip->wsba[1].csr = hose->sg_pci->dma_base | 3;
	pchip->wsm[1].csr  = (hose->sg_pci->size - 1) & 0xfff00000;
	pchip->tba[1].csr  = virt_to_phys(hose->sg_pci->ptes);

	pchip->wsba[2].csr = 0x80000000 | 1;
	pchip->wsm[2].csr  = (0x80000000 - 1) & 0xfff00000;
	pchip->tba[2].csr  = 0;

	pchip->wsba[3].csr = 0;

	/* Enable the Monster Window to make DAC pci64 possible. */
	pchip->pctl.csr |= pctl_m_mwin;

	tsunami_pci_tbi(hose, 0, -1);
}

void __init
tsunami_init_arch(void)
{
#ifdef NXM_MACHINE_CHECKS_ON_TSUNAMI
	unsigned long tmp;
	
	/* Ho hum.. init_arch is called before init_IRQ, but we need to be
	   able to handle machine checks.  So install the handler now.  */
	wrent(entInt, 0);

	/* NXMs just don't matter to Tsunami--unless they make it
	   choke completely. */
	tmp = (unsigned long)(TSUNAMI_cchip - 1);
	printk("%s: probing bogus address:  0x%016lx\n", FN, bogus_addr);
	printk("\tprobe %s\n",
	       tsunami_probe_write((unsigned long *)bogus_addr)
	       ? "succeeded" : "failed");
#endif /* NXM_MACHINE_CHECKS_ON_TSUNAMI */

#if 0
	printk("%s: CChip registers:\n", FN);
	printk("%s: CSR_CSC 0x%lx\n", FN, TSUNAMI_cchip->csc.csr);
	printk("%s: CSR_MTR 0x%lx\n", FN, TSUNAMI_cchip.mtr.csr);
	printk("%s: CSR_MISC 0x%lx\n", FN, TSUNAMI_cchip->misc.csr);
	printk("%s: CSR_DIM0 0x%lx\n", FN, TSUNAMI_cchip->dim0.csr);
	printk("%s: CSR_DIM1 0x%lx\n", FN, TSUNAMI_cchip->dim1.csr);
	printk("%s: CSR_DIR0 0x%lx\n", FN, TSUNAMI_cchip->dir0.csr);
	printk("%s: CSR_DIR1 0x%lx\n", FN, TSUNAMI_cchip->dir1.csr);
	printk("%s: CSR_DRIR 0x%lx\n", FN, TSUNAMI_cchip->drir.csr);

	printk("%s: DChip registers:\n");
	printk("%s: CSR_DSC 0x%lx\n", FN, TSUNAMI_dchip->dsc.csr);
	printk("%s: CSR_STR 0x%lx\n", FN, TSUNAMI_dchip->str.csr);
	printk("%s: CSR_DREV 0x%lx\n", FN, TSUNAMI_dchip->drev.csr);
#endif
	/* With multiple PCI busses, we play with I/O as physical addrs.  */
	ioport_resource.end = ~0UL;

	/* Find how many hoses we have, and initialize them.  TSUNAMI
	   and TYPHOON can have 2, but might only have 1 (DS10).  */

	tsunami_init_one_pchip(TSUNAMI_pchip0, 0);
	if (TSUNAMI_cchip->csc.csr & 1L<<14)
		tsunami_init_one_pchip(TSUNAMI_pchip1, 1);
}

static void
tsunami_kill_one_pchip(tsunami_pchip *pchip, int index)
{
	pchip->wsba[0].csr = saved_config[index].wsba[0];
	pchip->wsm[0].csr = saved_config[index].wsm[0];
	pchip->tba[0].csr = saved_config[index].tba[0];

	pchip->wsba[1].csr = saved_config[index].wsba[1];
	pchip->wsm[1].csr = saved_config[index].wsm[1];
	pchip->tba[1].csr = saved_config[index].tba[1];

	pchip->wsba[2].csr = saved_config[index].wsba[2];
	pchip->wsm[2].csr = saved_config[index].wsm[2];
	pchip->tba[2].csr = saved_config[index].tba[2];

	pchip->wsba[3].csr = saved_config[index].wsba[3];
	pchip->wsm[3].csr = saved_config[index].wsm[3];
	pchip->tba[3].csr = saved_config[index].tba[3];
}

void
tsunami_kill_arch(int mode)
{
	tsunami_kill_one_pchip(TSUNAMI_pchip0, 0);
	if (TSUNAMI_cchip->csc.csr & 1L<<14)
		tsunami_kill_one_pchip(TSUNAMI_pchip1, 1);
}

static inline void
tsunami_pci_clr_err_1(tsunami_pchip *pchip)
{
	pchip->perror.csr;
	pchip->perror.csr = 0x040;
	mb();
	pchip->perror.csr;
}

static inline void
tsunami_pci_clr_err(void)
{
	tsunami_pci_clr_err_1(TSUNAMI_pchip0);

	/* TSUNAMI and TYPHOON can have 2, but might only have 1 (DS10) */
	if (TSUNAMI_cchip->csc.csr & 1L<<14)
		tsunami_pci_clr_err_1(TSUNAMI_pchip1);
}

void
tsunami_machine_check(unsigned long vector, unsigned long la_ptr,
		      struct pt_regs * regs)
{
	/* Clear error before any reporting.  */
	mb();
	mb();  /* magic */
	draina();
	tsunami_pci_clr_err();
	wrmces(0x7);
	mb();

	process_mcheck_info(vector, la_ptr, regs, "TSUNAMI",
			    mcheck_expected(smp_processor_id()));
}
