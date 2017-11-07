// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/core_lca.c
 *
 * Written by David Mosberger (davidm@cs.arizona.edu) with some code
 * taken from Dave Rusling's (david.rusling@reo.mts.dec.com) 32-bit
 * bios code.
 *
 * Code common to all LCA core logic chips.
 */

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_lca.h>
#undef __EXTERN_INLINE

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/tty.h>

#include <asm/ptrace.h>
#include <asm/irq_regs.h>
#include <asm/smp.h>

#include "proto.h"
#include "pci_impl.h"


/*
 * BIOS32-style PCI interface:
 */

/*
 * Machine check reasons.  Defined according to PALcode sources
 * (osf.h and platform.h).
 */
#define MCHK_K_TPERR		0x0080
#define MCHK_K_TCPERR		0x0082
#define MCHK_K_HERR		0x0084
#define MCHK_K_ECC_C		0x0086
#define MCHK_K_ECC_NC		0x0088
#define MCHK_K_UNKNOWN		0x008A
#define MCHK_K_CACKSOFT		0x008C
#define MCHK_K_BUGCHECK		0x008E
#define MCHK_K_OS_BUGCHECK	0x0090
#define MCHK_K_DCPERR		0x0092
#define MCHK_K_ICPERR		0x0094


/*
 * Platform-specific machine-check reasons:
 */
#define MCHK_K_SIO_SERR		0x204	/* all platforms so far */
#define MCHK_K_SIO_IOCHK	0x206	/* all platforms so far */
#define MCHK_K_DCSR		0x208	/* all but Noname */


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the LCA_IOC_CONF register
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | | | | | | | | | | | | | | |F|F|F|R|R|R|R|R|R|0|0|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:11	Device select bit.
 * 	10:8	Function number
 * 	 7:2	Register number
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
	     unsigned long *pci_addr)
{
	unsigned long addr;
	u8 bus = pbus->number;

	if (bus == 0) {
		int device = device_fn >> 3;
		int func = device_fn & 0x7;

		/* Type 0 configuration cycle.  */

		if (device > 12) {
			return -1;
		}

		*(vulp)LCA_IOC_CONF = 0;
		addr = (1 << (11 + device)) | (func << 8) | where;
	} else {
		/* Type 1 configuration cycle.  */
		*(vulp)LCA_IOC_CONF = 1;
		addr = (bus << 16) | (device_fn << 8) | where;
	}
	*pci_addr = addr;
	return 0;
}

static unsigned int
conf_read(unsigned long addr)
{
	unsigned long flags, code, stat0;
	unsigned int value;

	local_irq_save(flags);

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vulp)LCA_IOC_STAT0;
	*(vulp)LCA_IOC_STAT0 = stat0;
	mb();

	/* Access configuration space.  */
	value = *(vuip)addr;
	draina();

	stat0 = *(vulp)LCA_IOC_STAT0;
	if (stat0 & LCA_IOC_STAT0_ERR) {
		code = ((stat0 >> LCA_IOC_STAT0_CODE_SHIFT)
			& LCA_IOC_STAT0_CODE_MASK);
		if (code != 1) {
			printk("lca.c:conf_read: got stat0=%lx\n", stat0);
		}

		/* Reset error status.  */
		*(vulp)LCA_IOC_STAT0 = stat0;
		mb();

		/* Reset machine check.  */
		wrmces(0x7);

		value = 0xffffffff;
	}
	local_irq_restore(flags);
	return value;
}

static void
conf_write(unsigned long addr, unsigned int value)
{
	unsigned long flags, code, stat0;

	local_irq_save(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vulp)LCA_IOC_STAT0;
	*(vulp)LCA_IOC_STAT0 = stat0;
	mb();

	/* Access configuration space.  */
	*(vuip)addr = value;
	draina();

	stat0 = *(vulp)LCA_IOC_STAT0;
	if (stat0 & LCA_IOC_STAT0_ERR) {
		code = ((stat0 >> LCA_IOC_STAT0_CODE_SHIFT)
			& LCA_IOC_STAT0_CODE_MASK);
		if (code != 1) {
			printk("lca.c:conf_write: got stat0=%lx\n", stat0);
		}

		/* Reset error status.  */
		*(vulp)LCA_IOC_STAT0 = stat0;
		mb();

		/* Reset machine check. */
		wrmces(0x7);
	}
	local_irq_restore(flags);
}

static int
lca_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		int size, u32 *value)
{
	unsigned long addr, pci_addr;
	long mask;
	int shift;

	if (mk_conf_addr(bus, devfn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	shift = (where & 3) * 8;
	mask = (size - 1) * 8;
	addr = (pci_addr << 5) + mask + LCA_CONF;
	*value = conf_read(addr) >> (shift);
	return PCIBIOS_SUCCESSFUL;
}

static int 
lca_write_config(struct pci_bus *bus, unsigned int devfn, int where, int size,
		 u32 value)
{
	unsigned long addr, pci_addr;
	long mask;

	if (mk_conf_addr(bus, devfn, where, &pci_addr))
		return PCIBIOS_DEVICE_NOT_FOUND;

	mask = (size - 1) * 8;
	addr = (pci_addr << 5) + mask + LCA_CONF;
	conf_write(addr, value << ((where & 3) * 8));
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops lca_pci_ops = 
{
	.read =		lca_read_config,
	.write =	lca_write_config,
};

void
lca_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	wmb();
	*(vulp)LCA_IOC_TBIA = 0;
	mb();
}

void __init
lca_init_arch(void)
{
	struct pci_controller *hose;

	/*
	 * Create our single hose.
	 */

	pci_isa_hose = hose = alloc_pci_controller();
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->index = 0;

	hose->sparse_mem_base = LCA_SPARSE_MEM - IDENT_ADDR;
	hose->dense_mem_base = LCA_DENSE_MEM - IDENT_ADDR;
	hose->sparse_io_base = LCA_IO - IDENT_ADDR;
	hose->dense_io_base = 0;

	/*
	 * Set up the PCI to main memory translation windows.
	 *
	 * Mimic the SRM settings for the direct-map window.
	 *   Window 0 is scatter-gather 8MB at 8MB (for isa).
	 *   Window 1 is direct access 1GB at 1GB.
	 *
	 * Note that we do not try to save any of the DMA window CSRs
	 * before setting them, since we cannot read those CSRs on LCA.
	 */
	hose->sg_isa = iommu_arena_new(hose, 0x00800000, 0x00800000, 0);
	hose->sg_pci = NULL;
	__direct_map_base = 0x40000000;
	__direct_map_size = 0x40000000;

	*(vulp)LCA_IOC_W_BASE0 = hose->sg_isa->dma_base | (3UL << 32);
	*(vulp)LCA_IOC_W_MASK0 = (hose->sg_isa->size - 1) & 0xfff00000;
	*(vulp)LCA_IOC_T_BASE0 = virt_to_phys(hose->sg_isa->ptes);

	*(vulp)LCA_IOC_W_BASE1 = __direct_map_base | (2UL << 32);
	*(vulp)LCA_IOC_W_MASK1 = (__direct_map_size - 1) & 0xfff00000;
	*(vulp)LCA_IOC_T_BASE1 = 0;

	*(vulp)LCA_IOC_TB_ENA = 0x80;

	lca_pci_tbi(hose, 0, -1);

	/*
	 * Disable PCI parity for now.  The NCR53c810 chip has
	 * troubles meeting the PCI spec which results in
	 * data parity errors.
	 */
	*(vulp)LCA_IOC_PAR_DIS = 1UL<<5;

	/*
	 * Finally, set up for restoring the correct HAE if using SRM.
	 * Again, since we cannot read many of the CSRs on the LCA,
	 * one of which happens to be the HAE, we save the value that
	 * the SRM will expect...
	 */
	if (alpha_using_srm)
		srm_hae = 0x80000000UL;
}

/*
 * Constants used during machine-check handling.  I suppose these
 * could be moved into lca.h but I don't see much reason why anybody
 * else would want to use them.
 */

#define ESR_EAV		(1UL<< 0)	/* error address valid */
#define ESR_CEE		(1UL<< 1)	/* correctable error */
#define ESR_UEE		(1UL<< 2)	/* uncorrectable error */
#define ESR_WRE		(1UL<< 3)	/* write-error */
#define ESR_SOR		(1UL<< 4)	/* error source */
#define ESR_CTE		(1UL<< 7)	/* cache-tag error */
#define ESR_MSE		(1UL<< 9)	/* multiple soft errors */
#define ESR_MHE		(1UL<<10)	/* multiple hard errors */
#define ESR_NXM		(1UL<<12)	/* non-existent memory */

#define IOC_ERR		(  1<<4)	/* ioc logs an error */
#define IOC_CMD_SHIFT	0
#define IOC_CMD		(0xf<<IOC_CMD_SHIFT)
#define IOC_CODE_SHIFT	8
#define IOC_CODE	(0xf<<IOC_CODE_SHIFT)
#define IOC_LOST	(  1<<5)
#define IOC_P_NBR	((__u32) ~((1<<13) - 1))

static void
mem_error(unsigned long esr, unsigned long ear)
{
	printk("    %s %s error to %s occurred at address %x\n",
	       ((esr & ESR_CEE) ? "Correctable" :
		(esr & ESR_UEE) ? "Uncorrectable" : "A"),
	       (esr & ESR_WRE) ? "write" : "read",
	       (esr & ESR_SOR) ? "memory" : "b-cache",
	       (unsigned) (ear & 0x1ffffff8));
	if (esr & ESR_CTE) {
		printk("    A b-cache tag parity error was detected.\n");
	}
	if (esr & ESR_MSE) {
		printk("    Several other correctable errors occurred.\n");
	}
	if (esr & ESR_MHE) {
		printk("    Several other uncorrectable errors occurred.\n");
	}
	if (esr & ESR_NXM) {
		printk("    Attempted to access non-existent memory.\n");
	}
}

static void
ioc_error(__u32 stat0, __u32 stat1)
{
	static const char * const pci_cmd[] = {
		"Interrupt Acknowledge", "Special", "I/O Read", "I/O Write",
		"Rsvd 1", "Rsvd 2", "Memory Read", "Memory Write", "Rsvd3",
		"Rsvd4", "Configuration Read", "Configuration Write",
		"Memory Read Multiple", "Dual Address", "Memory Read Line",
		"Memory Write and Invalidate"
	};
	static const char * const err_name[] = {
		"exceeded retry limit", "no device", "bad data parity",
		"target abort", "bad address parity", "page table read error",
		"invalid page", "data error"
	};
	unsigned code = (stat0 & IOC_CODE) >> IOC_CODE_SHIFT;
	unsigned cmd  = (stat0 & IOC_CMD)  >> IOC_CMD_SHIFT;

	printk("    %s initiated PCI %s cycle to address %x"
	       " failed due to %s.\n",
	       code > 3 ? "PCI" : "CPU", pci_cmd[cmd], stat1, err_name[code]);

	if (code == 5 || code == 6) {
		printk("    (Error occurred at PCI memory address %x.)\n",
		       (stat0 & ~IOC_P_NBR));
	}
	if (stat0 & IOC_LOST) {
		printk("    Other PCI errors occurred simultaneously.\n");
	}
}

void
lca_machine_check(unsigned long vector, unsigned long la_ptr)
{
	const char * reason;
	union el_lca el;

	el.c = (struct el_common *) la_ptr;

	wrmces(rdmces());	/* reset machine check pending flag */

	printk(KERN_CRIT "LCA machine check: vector=%#lx pc=%#lx code=%#x\n",
	       vector, get_irq_regs()->pc, (unsigned int) el.c->code);

	/*
	 * The first quadword after the common header always seems to
	 * be the machine check reason---don't know why this isn't
	 * part of the common header instead.  In the case of a long
	 * logout frame, the upper 32 bits is the machine check
	 * revision level, which we ignore for now.
	 */
	switch ((unsigned int) el.c->code) {
	case MCHK_K_TPERR:	reason = "tag parity error"; break;
	case MCHK_K_TCPERR:	reason = "tag control parity error"; break;
	case MCHK_K_HERR:	reason = "access to non-existent memory"; break;
	case MCHK_K_ECC_C:	reason = "correctable ECC error"; break;
	case MCHK_K_ECC_NC:	reason = "non-correctable ECC error"; break;
	case MCHK_K_CACKSOFT:	reason = "MCHK_K_CACKSOFT"; break;
	case MCHK_K_BUGCHECK:	reason = "illegal exception in PAL mode"; break;
	case MCHK_K_OS_BUGCHECK: reason = "callsys in kernel mode"; break;
	case MCHK_K_DCPERR:	reason = "d-cache parity error"; break;
	case MCHK_K_ICPERR:	reason = "i-cache parity error"; break;
	case MCHK_K_SIO_SERR:	reason = "SIO SERR occurred on PCI bus"; break;
	case MCHK_K_SIO_IOCHK:	reason = "SIO IOCHK occurred on ISA bus"; break;
	case MCHK_K_DCSR:	reason = "MCHK_K_DCSR"; break;
	case MCHK_K_UNKNOWN:
	default:		reason = "unknown"; break;
	}

	switch (el.c->size) {
	case sizeof(struct el_lca_mcheck_short):
		printk(KERN_CRIT
		       "  Reason: %s (short frame%s, dc_stat=%#lx):\n",
		       reason, el.c->retry ? ", retryable" : "",
		       el.s->dc_stat);
		if (el.s->esr & ESR_EAV) {
			mem_error(el.s->esr, el.s->ear);
		}
		if (el.s->ioc_stat0 & IOC_ERR) {
			ioc_error(el.s->ioc_stat0, el.s->ioc_stat1);
		}
		break;

	case sizeof(struct el_lca_mcheck_long):
		printk(KERN_CRIT "  Reason: %s (long frame%s):\n",
		       reason, el.c->retry ? ", retryable" : "");
		printk(KERN_CRIT
		       "    reason: %#lx  exc_addr: %#lx  dc_stat: %#lx\n", 
		       el.l->pt[0], el.l->exc_addr, el.l->dc_stat);
		printk(KERN_CRIT "    car: %#lx\n", el.l->car);
		if (el.l->esr & ESR_EAV) {
			mem_error(el.l->esr, el.l->ear);
		}
		if (el.l->ioc_stat0 & IOC_ERR) {
			ioc_error(el.l->ioc_stat0, el.l->ioc_stat1);
		}
		break;

	default:
		printk(KERN_CRIT "  Unknown errorlog size %d\n", el.c->size);
	}

	/* Dump the logout area to give all info.  */
#ifdef CONFIG_VERBOSE_MCHECK
	if (alpha_verbose_mcheck > 1) {
		unsigned long * ptr = (unsigned long *) la_ptr;
		long i;
		for (i = 0; i < el.c->size / sizeof(long); i += 2) {
			printk(KERN_CRIT " +%8lx %016lx %016lx\n",
			       i*sizeof(long), ptr[i], ptr[i+1]);
		}
	}
#endif /* CONFIG_VERBOSE_MCHECK */
}

/*
 * The following routines are needed to support the SPEED changing
 * necessary to successfully manage the thermal problem on the AlphaBook1.
 */

void
lca_clock_print(void)
{
        long    pmr_reg;

        pmr_reg = LCA_READ_PMR;

        printk("Status of clock control:\n");
        printk("\tPrimary clock divisor\t0x%lx\n", LCA_GET_PRIMARY(pmr_reg));
        printk("\tOverride clock divisor\t0x%lx\n", LCA_GET_OVERRIDE(pmr_reg));
        printk("\tInterrupt override is %s\n",
	       (pmr_reg & LCA_PMR_INTO) ? "on" : "off"); 
        printk("\tDMA override is %s\n",
	       (pmr_reg & LCA_PMR_DMAO) ? "on" : "off"); 

}

int
lca_get_clock(void)
{
        long    pmr_reg;

        pmr_reg = LCA_READ_PMR;
        return(LCA_GET_PRIMARY(pmr_reg));

}

void
lca_clock_fiddle(int divisor)
{
        long    pmr_reg;

        pmr_reg = LCA_READ_PMR;
        LCA_SET_PRIMARY_CLOCK(pmr_reg, divisor);
	/* lca_norm_clock = divisor; */
        LCA_WRITE_PMR(pmr_reg);
        mb();
}
