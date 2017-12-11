// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/core_t2.c
 *
 * Written by Jay A Estabrook (jestabro@amt.tay1.dec.com).
 * December 1996.
 *
 * based on CIA code by David A Rusling (david.rusling@reo.mts.dec.com)
 *
 * Code common to all T2 core logic chips.
 */

#define __EXTERN_INLINE
#include <asm/io.h>
#include <asm/core_t2.h>
#undef __EXTERN_INLINE

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/delay.h>
#include <asm/mce.h>

#include "proto.h"
#include "pci_impl.h"

/* For dumping initial DMA window settings. */
#define DEBUG_PRINT_INITIAL_SETTINGS 0

/* For dumping final DMA window settings. */
#define DEBUG_PRINT_FINAL_SETTINGS 0

/*
 * By default, we direct-map starting at 2GB, in order to allow the
 * maximum size direct-map window (2GB) to match the maximum amount of
 * memory (2GB) that can be present on SABLEs. But that limits the
 * floppy to DMA only via the scatter/gather window set up for 8MB
 * ISA DMA, since the maximum ISA DMA address is 2GB-1.
 *
 * For now, this seems a reasonable trade-off: even though most SABLEs
 * have less than 1GB of memory, floppy usage/performance will not
 * really be affected by forcing it to go via scatter/gather...
 */
#define T2_DIRECTMAP_2G 1

#if T2_DIRECTMAP_2G
# define T2_DIRECTMAP_START	0x80000000UL
# define T2_DIRECTMAP_LENGTH	0x80000000UL
#else
# define T2_DIRECTMAP_START	0x40000000UL
# define T2_DIRECTMAP_LENGTH	0x40000000UL
#endif

/* The ISA scatter/gather window settings. */
#define T2_ISA_SG_START		0x00800000UL
#define T2_ISA_SG_LENGTH	0x00800000UL

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CONFIG 0

#if DEBUG_CONFIG
# define DBG(args)	printk args
#else
# define DBG(args)
#endif

static volatile unsigned int t2_mcheck_any_expected;
static volatile unsigned int t2_mcheck_last_taken;

/* Place to save the DMA Window registers as set up by SRM
   for restoration during shutdown. */
static struct
{
	struct {
		unsigned long wbase;
		unsigned long wmask;
		unsigned long tbase;
	} window[2];
	unsigned long hae_1;
  	unsigned long hae_2;
	unsigned long hae_3;
	unsigned long hae_4;
	unsigned long hbase;
} t2_saved_config __attribute((common));

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the T2_HAXR2 register
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | |D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|0|
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
	     unsigned long *pci_addr, unsigned char *type1)
{
	unsigned long addr;
	u8 bus = pbus->number;

	DBG(("mk_conf_addr(bus=%d, dfn=0x%x, where=0x%x,"
	     " addr=0x%lx, type1=0x%x)\n",
	     bus, device_fn, where, pci_addr, type1));

	if (bus == 0) {
		int device = device_fn >> 3;

		/* Type 0 configuration cycle.  */

		if (device > 8) {
			DBG(("mk_conf_addr: device (%d)>20, returning -1\n",
			     device));
			return -1;
		}

		*type1 = 0;
		addr = (0x0800L << device) | ((device_fn & 7) << 8) | (where);
	} else {
		/* Type 1 configuration cycle.  */
		*type1 = 1;
		addr = (bus << 16) | (device_fn << 8) | (where);
	}
	*pci_addr = addr;
	DBG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

/*
 * NOTE: both conf_read() and conf_write() may set HAE_3 when needing
 *       to do type1 access. This is protected by the use of spinlock IRQ
 *       primitives in the wrapper functions pci_{read,write}_config_*()
 *       defined in drivers/pci/pci.c.
 */
static unsigned int
conf_read(unsigned long addr, unsigned char type1)
{
	unsigned int value, cpu, taken;
	unsigned long t2_cfg = 0;

	cpu = smp_processor_id();

	DBG(("conf_read(addr=0x%lx, type1=%d)\n", addr, type1));

	/* If Type1 access, must set T2 CFG.  */
	if (type1) {
		t2_cfg = *(vulp)T2_HAE_3 & ~0xc0000000UL;
		*(vulp)T2_HAE_3 = 0x40000000UL | t2_cfg;
		mb();
	}
	mb();
	draina();

	mcheck_expected(cpu) = 1;
	mcheck_taken(cpu) = 0;
	t2_mcheck_any_expected |= (1 << cpu);
	mb();

	/* Access configuration space. */
	value = *(vuip)addr;
	mb();
	mb();  /* magic */

	/* Wait for possible mcheck. Also, this lets other CPUs clear
	   their mchecks as well, as they can reliably tell when
	   another CPU is in the midst of handling a real mcheck via
	   the "taken" function. */
	udelay(100);

	if ((taken = mcheck_taken(cpu))) {
		mcheck_taken(cpu) = 0;
		t2_mcheck_last_taken |= (1 << cpu);
		value = 0xffffffffU;
		mb();
	}
	mcheck_expected(cpu) = 0;
	t2_mcheck_any_expected = 0;
	mb();

	/* If Type1 access, must reset T2 CFG so normal IO space ops work.  */
	if (type1) {
		*(vulp)T2_HAE_3 = t2_cfg;
		mb();
	}

	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned int cpu, taken;
	unsigned long t2_cfg = 0;

	cpu = smp_processor_id();

	/* If Type1 access, must set T2 CFG.  */
	if (type1) {
		t2_cfg = *(vulp)T2_HAE_3 & ~0xc0000000UL;
		*(vulp)T2_HAE_3 = t2_cfg | 0x40000000UL;
		mb();
	}
	mb();
	draina();

	mcheck_expected(cpu) = 1;
	mcheck_taken(cpu) = 0;
	t2_mcheck_any_expected |= (1 << cpu);
	mb();

	/* Access configuration space.  */
	*(vuip)addr = value;
	mb();
	mb();  /* magic */

	/* Wait for possible mcheck. Also, this lets other CPUs clear
	   their mchecks as well, as they can reliably tell when
	   this CPU is in the midst of handling a real mcheck via
	   the "taken" function. */
	udelay(100);

	if ((taken = mcheck_taken(cpu))) {
		mcheck_taken(cpu) = 0;
		t2_mcheck_last_taken |= (1 << cpu);
		mb();
	}
	mcheck_expected(cpu) = 0;
	t2_mcheck_any_expected = 0;
	mb();

	/* If Type1 access, must reset T2 CFG so normal IO space ops work.  */
	if (type1) {
		*(vulp)T2_HAE_3 = t2_cfg;
		mb();
	}
}

static int
t2_read_config(struct pci_bus *bus, unsigned int devfn, int where,
	       int size, u32 *value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;
	int shift;
	long mask;

	if (mk_conf_addr(bus, devfn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	mask = (size - 1) * 8;
	shift = (where & 3) * 8;
	addr = (pci_addr << 5) + mask + T2_CONF;
	*value = conf_read(addr, type1) >> (shift);
	return PCIBIOS_SUCCESSFUL;
}

static int 
t2_write_config(struct pci_bus *bus, unsigned int devfn, int where, int size,
		u32 value)
{
	unsigned long addr, pci_addr;
	unsigned char type1;
	long mask;

	if (mk_conf_addr(bus, devfn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	mask = (size - 1) * 8;
	addr = (pci_addr << 5) + mask + T2_CONF;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops t2_pci_ops = 
{
	.read =		t2_read_config,
	.write =	t2_write_config,
};

static void __init
t2_direct_map_window1(unsigned long base, unsigned long length)
{
	unsigned long temp;

	__direct_map_base = base;
	__direct_map_size = length;

	temp = (base & 0xfff00000UL) | ((base + length - 1) >> 20);
	*(vulp)T2_WBASE1 = temp | 0x80000UL; /* OR in ENABLE bit */
	temp = (length - 1) & 0xfff00000UL;
	*(vulp)T2_WMASK1 = temp;
	*(vulp)T2_TBASE1 = 0;

#if DEBUG_PRINT_FINAL_SETTINGS
	printk("%s: setting WBASE1=0x%lx WMASK1=0x%lx TBASE1=0x%lx\n",
	       __func__, *(vulp)T2_WBASE1, *(vulp)T2_WMASK1, *(vulp)T2_TBASE1);
#endif
}

static void __init
t2_sg_map_window2(struct pci_controller *hose,
		  unsigned long base,
		  unsigned long length)
{
	unsigned long temp;

	/* Note we can only do 1 SG window, as the other is for direct, so
	   do an ISA SG area, especially for the floppy. */
	hose->sg_isa = iommu_arena_new(hose, base, length, 0);
	hose->sg_pci = NULL;

	temp = (base & 0xfff00000UL) | ((base + length - 1) >> 20);
	*(vulp)T2_WBASE2 = temp | 0xc0000UL; /* OR in ENABLE/SG bits */
	temp = (length - 1) & 0xfff00000UL;
	*(vulp)T2_WMASK2 = temp;
	*(vulp)T2_TBASE2 = virt_to_phys(hose->sg_isa->ptes) >> 1;
	mb();

	t2_pci_tbi(hose, 0, -1); /* flush TLB all */

#if DEBUG_PRINT_FINAL_SETTINGS
	printk("%s: setting WBASE2=0x%lx WMASK2=0x%lx TBASE2=0x%lx\n",
	       __func__, *(vulp)T2_WBASE2, *(vulp)T2_WMASK2, *(vulp)T2_TBASE2);
#endif
}

static void __init
t2_save_configuration(void)
{
#if DEBUG_PRINT_INITIAL_SETTINGS
	printk("%s: HAE_1 was 0x%lx\n", __func__, srm_hae); /* HW is 0 */
	printk("%s: HAE_2 was 0x%lx\n", __func__, *(vulp)T2_HAE_2);
	printk("%s: HAE_3 was 0x%lx\n", __func__, *(vulp)T2_HAE_3);
	printk("%s: HAE_4 was 0x%lx\n", __func__, *(vulp)T2_HAE_4);
	printk("%s: HBASE was 0x%lx\n", __func__, *(vulp)T2_HBASE);

	printk("%s: WBASE1=0x%lx WMASK1=0x%lx TBASE1=0x%lx\n", __func__,
	       *(vulp)T2_WBASE1, *(vulp)T2_WMASK1, *(vulp)T2_TBASE1);
	printk("%s: WBASE2=0x%lx WMASK2=0x%lx TBASE2=0x%lx\n", __func__,
	       *(vulp)T2_WBASE2, *(vulp)T2_WMASK2, *(vulp)T2_TBASE2);
#endif

	/*
	 * Save the DMA Window registers.
	 */
	t2_saved_config.window[0].wbase = *(vulp)T2_WBASE1;
	t2_saved_config.window[0].wmask = *(vulp)T2_WMASK1;
	t2_saved_config.window[0].tbase = *(vulp)T2_TBASE1;
	t2_saved_config.window[1].wbase = *(vulp)T2_WBASE2;
	t2_saved_config.window[1].wmask = *(vulp)T2_WMASK2;
	t2_saved_config.window[1].tbase = *(vulp)T2_TBASE2;

	t2_saved_config.hae_1 = srm_hae; /* HW is already set to 0 */
	t2_saved_config.hae_2 = *(vulp)T2_HAE_2;
	t2_saved_config.hae_3 = *(vulp)T2_HAE_3;
	t2_saved_config.hae_4 = *(vulp)T2_HAE_4;
	t2_saved_config.hbase = *(vulp)T2_HBASE;
}

void __init
t2_init_arch(void)
{
	struct pci_controller *hose;
	struct resource *hae_mem;
	unsigned long temp;
	unsigned int i;

	for (i = 0; i < NR_CPUS; i++) {
		mcheck_expected(i) = 0;
		mcheck_taken(i) = 0;
	}
	t2_mcheck_any_expected = 0;
	t2_mcheck_last_taken = 0;

	/* Enable scatter/gather TLB use.  */
	temp = *(vulp)T2_IOCSR;
	if (!(temp & (0x1UL << 26))) {
		printk("t2_init_arch: enabling SG TLB, IOCSR was 0x%lx\n",
		       temp);
		*(vulp)T2_IOCSR = temp | (0x1UL << 26);
		mb();	
		*(vulp)T2_IOCSR; /* read it back to make sure */
	}

	t2_save_configuration();

	/*
	 * Create our single hose.
	 */
	pci_isa_hose = hose = alloc_pci_controller();
	hose->io_space = &ioport_resource;
	hae_mem = alloc_resource();
	hae_mem->start = 0;
	hae_mem->end = T2_MEM_R1_MASK;
	hae_mem->name = pci_hae0_name;
	if (request_resource(&iomem_resource, hae_mem) < 0)
		printk(KERN_ERR "Failed to request HAE_MEM\n");
	hose->mem_space = hae_mem;
	hose->index = 0;

	hose->sparse_mem_base = T2_SPARSE_MEM - IDENT_ADDR;
	hose->dense_mem_base = T2_DENSE_MEM - IDENT_ADDR;
	hose->sparse_io_base = T2_IO - IDENT_ADDR;
	hose->dense_io_base = 0;

	/*
	 * Set up the PCI->physical memory translation windows.
	 *
	 * Window 1 is direct mapped.
	 * Window 2 is scatter/gather (for ISA).
	 */

	t2_direct_map_window1(T2_DIRECTMAP_START, T2_DIRECTMAP_LENGTH);

	/* Always make an ISA DMA window. */
	t2_sg_map_window2(hose, T2_ISA_SG_START, T2_ISA_SG_LENGTH);

	*(vulp)T2_HBASE = 0x0; /* Disable HOLES. */

	/* Zero HAE.  */
	*(vulp)T2_HAE_1 = 0; mb(); /* Sparse MEM HAE */
	*(vulp)T2_HAE_2 = 0; mb(); /* Sparse I/O HAE */
	*(vulp)T2_HAE_3 = 0; mb(); /* Config Space HAE */

	/*
	 * We also now zero out HAE_4, the dense memory HAE, so that
	 * we need not account for its "offset" when accessing dense
	 * memory resources which we allocated in our normal way. This
	 * HAE would need to stay untouched were we to keep the SRM
	 * resource settings.
	 *
	 * Thus we can now run standard X servers on SABLE/LYNX. :-)
	 */
	*(vulp)T2_HAE_4 = 0; mb();
}

void
t2_kill_arch(int mode)
{
	/*
	 * Restore the DMA Window registers.
	 */
	*(vulp)T2_WBASE1 = t2_saved_config.window[0].wbase;
	*(vulp)T2_WMASK1 = t2_saved_config.window[0].wmask;
	*(vulp)T2_TBASE1 = t2_saved_config.window[0].tbase;
	*(vulp)T2_WBASE2 = t2_saved_config.window[1].wbase;
	*(vulp)T2_WMASK2 = t2_saved_config.window[1].wmask;
	*(vulp)T2_TBASE2 = t2_saved_config.window[1].tbase;
	mb();

	*(vulp)T2_HAE_1 = srm_hae;
	*(vulp)T2_HAE_2 = t2_saved_config.hae_2;
	*(vulp)T2_HAE_3 = t2_saved_config.hae_3;
	*(vulp)T2_HAE_4 = t2_saved_config.hae_4;
	*(vulp)T2_HBASE = t2_saved_config.hbase;
	mb();
	*(vulp)T2_HBASE; /* READ it back to ensure WRITE occurred. */
}

void
t2_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	unsigned long t2_iocsr;

	t2_iocsr = *(vulp)T2_IOCSR;

	/* set the TLB Clear bit */
	*(vulp)T2_IOCSR = t2_iocsr | (0x1UL << 28);
	mb();
	*(vulp)T2_IOCSR; /* read it back to make sure */

	/* clear the TLB Clear bit */
	*(vulp)T2_IOCSR = t2_iocsr & ~(0x1UL << 28);
	mb();
	*(vulp)T2_IOCSR; /* read it back to make sure */
}

#define SIC_SEIC (1UL << 33)    /* System Event Clear */

static void
t2_clear_errors(int cpu)
{
	struct sable_cpu_csr *cpu_regs;

	cpu_regs = (struct sable_cpu_csr *)T2_CPUn_BASE(cpu);
		
	cpu_regs->sic &= ~SIC_SEIC;

	/* Clear CPU errors.  */
	cpu_regs->bcce |= cpu_regs->bcce;
	cpu_regs->cbe  |= cpu_regs->cbe;
	cpu_regs->bcue |= cpu_regs->bcue;
	cpu_regs->dter |= cpu_regs->dter;

	*(vulp)T2_CERR1 |= *(vulp)T2_CERR1;
	*(vulp)T2_PERR1 |= *(vulp)T2_PERR1;

	mb();
	mb();  /* magic */
}

/*
 * SABLE seems to have a "broadcast" style machine check, in that all
 * CPUs receive it. And, the issuing CPU, in the case of PCI Config
 * space read/write faults, will also receive a second mcheck, upon
 * lowering IPL during completion processing in pci_read_config_byte()
 * et al.
 *
 * Hence all the taken/expected/any_expected/last_taken stuff...
 */
void
t2_machine_check(unsigned long vector, unsigned long la_ptr)
{
	int cpu = smp_processor_id();
#ifdef CONFIG_VERBOSE_MCHECK
	struct el_common *mchk_header = (struct el_common *)la_ptr;
#endif

	/* Clear the error before any reporting.  */
	mb();
	mb();  /* magic */
	draina();
	t2_clear_errors(cpu);

	/* This should not actually be done until the logout frame is
	   examined, but, since we don't do that, go on and do this... */
	wrmces(0x7);
	mb();

	/* Now, do testing for the anomalous conditions. */
	if (!mcheck_expected(cpu) && t2_mcheck_any_expected) {
		/*
		 * FUNKY: Received mcheck on a CPU and not
		 * expecting it, but another CPU is expecting one.
		 *
		 * Just dismiss it for now on this CPU...
		 */
#ifdef CONFIG_VERBOSE_MCHECK
		if (alpha_verbose_mcheck > 1) {
			printk("t2_machine_check(cpu%d): any_expected 0x%x -"
			       " (assumed) spurious -"
			       " code 0x%x\n", cpu, t2_mcheck_any_expected,
			       (unsigned int)mchk_header->code);
		}
#endif
		return;
	}

	if (!mcheck_expected(cpu) && !t2_mcheck_any_expected) {
		if (t2_mcheck_last_taken & (1 << cpu)) {
#ifdef CONFIG_VERBOSE_MCHECK
		    if (alpha_verbose_mcheck > 1) {
			printk("t2_machine_check(cpu%d): last_taken 0x%x - "
			       "unexpected mcheck - code 0x%x\n",
			       cpu, t2_mcheck_last_taken,
			       (unsigned int)mchk_header->code);
		    }
#endif
		    t2_mcheck_last_taken = 0;
		    mb();
		    return;
		} else {
			t2_mcheck_last_taken = 0;
			mb();
		}
	}

#ifdef CONFIG_VERBOSE_MCHECK
	if (alpha_verbose_mcheck > 1) {
		printk("%s t2_mcheck(cpu%d): last_taken 0x%x - "
		       "any_expected 0x%x - code 0x%x\n",
		       (mcheck_expected(cpu) ? "EX" : "UN"), cpu,
		       t2_mcheck_last_taken, t2_mcheck_any_expected,
		       (unsigned int)mchk_header->code);
	}
#endif

	process_mcheck_info(vector, la_ptr, "T2", mcheck_expected(cpu));
}
