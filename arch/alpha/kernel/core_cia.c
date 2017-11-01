// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/core_cia.c
 *
 * Written by David A Rusling (david.rusling@reo.mts.dec.com).
 * December 1995.
 *
 *	Copyright (C) 1995  David A Rusling
 *	Copyright (C) 1997, 1998  Jay Estabrook
 *	Copyright (C) 1998, 1999, 2000  Richard Henderson
 *
 * Code common to all CIA core logic chips.
 */

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_cia.h>
#undef __EXTERN_INLINE

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/ptrace.h>
#include <asm/mce.h>

#include "proto.h"
#include "pci_impl.h"


/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

#define DEBUG_CONFIG 0
#if DEBUG_CONFIG
# define DBGC(args)	printk args
#else
# define DBGC(args)
#endif

#define vip	volatile int  *

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address.  It is therefore not safe to have
 * concurrent invocations to configuration space access routines, but
 * there really shouldn't be any need for this.
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
mk_conf_addr(struct pci_bus *bus_dev, unsigned int device_fn, int where,
	     unsigned long *pci_addr, unsigned char *type1)
{
	u8 bus = bus_dev->number;

	*type1 = (bus != 0);
	*pci_addr = (bus << 16) | (device_fn << 8) | where;

	DBGC(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x,"
	      " returning address 0x%p\n"
	      bus, device_fn, where, *pci_addr));

	return 0;
}

static unsigned int
conf_read(unsigned long addr, unsigned char type1)
{
	unsigned long flags;
	int stat0, value;
	int cia_cfg = 0;

	DBGC(("conf_read(addr=0x%lx, type1=%d) ", addr, type1));
	local_irq_save(flags);

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vip)CIA_IOC_CIA_ERR;
	*(vip)CIA_IOC_CIA_ERR = stat0;
	mb();
	*(vip)CIA_IOC_CIA_ERR; /* re-read to force write */

	/* If Type1 access, must set CIA CFG. */
	if (type1) {
		cia_cfg = *(vip)CIA_IOC_CFG;
		*(vip)CIA_IOC_CFG = (cia_cfg & ~3) | 1;
		mb();
		*(vip)CIA_IOC_CFG;
	}

	mb();
	draina();
	mcheck_expected(0) = 1;
	mcheck_taken(0) = 0;
	mb();

	/* Access configuration space.  */
	value = *(vip)addr;
	mb();
	mb();  /* magic */
	if (mcheck_taken(0)) {
		mcheck_taken(0) = 0;
		value = 0xffffffff;
		mb();
	}
	mcheck_expected(0) = 0;
	mb();

	/* If Type1 access, must reset IOC CFG so normal IO space ops work.  */
	if (type1) {
		*(vip)CIA_IOC_CFG = cia_cfg;
		mb();
		*(vip)CIA_IOC_CFG;
	}

	local_irq_restore(flags);
	DBGC(("done\n"));

	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1)
{
	unsigned long flags;
	int stat0, cia_cfg = 0;

	DBGC(("conf_write(addr=0x%lx, type1=%d) ", addr, type1));
	local_irq_save(flags);

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vip)CIA_IOC_CIA_ERR;
	*(vip)CIA_IOC_CIA_ERR = stat0;
	mb();
	*(vip)CIA_IOC_CIA_ERR; /* re-read to force write */

	/* If Type1 access, must set CIA CFG.  */
	if (type1) {
		cia_cfg = *(vip)CIA_IOC_CFG;
		*(vip)CIA_IOC_CFG = (cia_cfg & ~3) | 1;
		mb();
		*(vip)CIA_IOC_CFG;
	}

	mb();
	draina();
	mcheck_expected(0) = 1;
	mcheck_taken(0) = 0;
	mb();

	/* Access configuration space.  */
	*(vip)addr = value;
	mb();
	*(vip)addr; /* read back to force the write */

	mcheck_expected(0) = 0;
	mb();

	/* If Type1 access, must reset IOC CFG so normal IO space ops work.  */
	if (type1) {
		*(vip)CIA_IOC_CFG = cia_cfg;
		mb();
		*(vip)CIA_IOC_CFG;
	}

	local_irq_restore(flags);
	DBGC(("done\n"));
}

static int 
cia_read_config(struct pci_bus *bus, unsigned int devfn, int where, int size,
		u32 *value)
{
	unsigned long addr, pci_addr;
	long mask;
	unsigned char type1;
	int shift;

	if (mk_conf_addr(bus, devfn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	mask = (size - 1) * 8;
	shift = (where & 3) * 8;
	addr = (pci_addr << 5) + mask + CIA_CONF;
	*value = conf_read(addr, type1) >> (shift);
	return PCIBIOS_SUCCESSFUL;
}

static int 
cia_write_config(struct pci_bus *bus, unsigned int devfn, int where, int size,
		 u32 value)
{
	unsigned long addr, pci_addr;
	long mask;
	unsigned char type1;

	if (mk_conf_addr(bus, devfn, where, &pci_addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	mask = (size - 1) * 8;
	addr = (pci_addr << 5) + mask + CIA_CONF;
	conf_write(addr, value << ((where & 3) * 8), type1);
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops cia_pci_ops = 
{
	.read = 	cia_read_config,
	.write =	cia_write_config,
};

/*
 * CIA Pass 1 and PYXIS Pass 1 and 2 have a broken scatter-gather tlb.
 * It cannot be invalidated.  Rather than hard code the pass numbers,
 * actually try the tbia to see if it works.
 */

void
cia_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	wmb();
	*(vip)CIA_IOC_PCI_TBIA = 3;	/* Flush all locked and unlocked.  */
	mb();
	*(vip)CIA_IOC_PCI_TBIA;
}

/*
 * On PYXIS, even if the tbia works, we cannot use it. It effectively locks
 * the chip (as well as direct write to the tag registers) if there is a
 * SG DMA operation in progress. This is true at least for PYXIS rev. 1,
 * so always use the method below.
 */
/*
 * This is the method NT and NetBSD use.
 *
 * Allocate mappings, and put the chip into DMA loopback mode to read a
 * garbage page.  This works by causing TLB misses, causing old entries to
 * be purged to make room for the new entries coming in for the garbage page.
 */

#define CIA_BROKEN_TBIA_BASE	0x30000000
#define CIA_BROKEN_TBIA_SIZE	1024

/* Always called with interrupts disabled */
void
cia_pci_tbi_try2(struct pci_controller *hose,
		 dma_addr_t start, dma_addr_t end)
{
	void __iomem *bus_addr;
	int ctrl;

	/* Put the chip into PCI loopback mode.  */
	mb();
	ctrl = *(vip)CIA_IOC_CIA_CTRL;
	*(vip)CIA_IOC_CIA_CTRL = ctrl | CIA_CTRL_PCI_LOOP_EN;
	mb();
	*(vip)CIA_IOC_CIA_CTRL;
	mb();

	/* Read from PCI dense memory space at TBI_ADDR, skipping 32k on
	   each read.  This forces SG TLB misses.  NetBSD claims that the
	   TLB entries are not quite LRU, meaning that we need to read more
	   times than there are actual tags.  The 2117x docs claim strict
	   round-robin.  Oh well, we've come this far...  */
	/* Even better - as seen on the PYXIS rev 1 the TLB tags 0-3 can
	   be filled by the TLB misses *only once* after being invalidated
	   (by tbia or direct write). Next misses won't update them even
	   though the lock bits are cleared. Tags 4-7 are "quite LRU" though,
	   so use them and read at window 3 base exactly 4 times. Reading
	   more sometimes makes the chip crazy.  -ink */

	bus_addr = cia_ioremap(CIA_BROKEN_TBIA_BASE, 32768 * 4);

	cia_readl(bus_addr + 0x00000);
	cia_readl(bus_addr + 0x08000);
	cia_readl(bus_addr + 0x10000);
	cia_readl(bus_addr + 0x18000);

	cia_iounmap(bus_addr);

	/* Restore normal PCI operation.  */
	mb();
	*(vip)CIA_IOC_CIA_CTRL = ctrl;
	mb();
	*(vip)CIA_IOC_CIA_CTRL;
	mb();
}

static inline void
cia_prepare_tbia_workaround(int window)
{
	unsigned long *ppte, pte;
	long i;

	/* Use minimal 1K map. */
	ppte = __alloc_bootmem(CIA_BROKEN_TBIA_SIZE, 32768, 0);
	pte = (virt_to_phys(ppte) >> (PAGE_SHIFT - 1)) | 1;

	for (i = 0; i < CIA_BROKEN_TBIA_SIZE / sizeof(unsigned long); ++i)
		ppte[i] = pte;

	*(vip)CIA_IOC_PCI_Wn_BASE(window) = CIA_BROKEN_TBIA_BASE | 3;
	*(vip)CIA_IOC_PCI_Wn_MASK(window)
	  = (CIA_BROKEN_TBIA_SIZE*1024 - 1) & 0xfff00000;
	*(vip)CIA_IOC_PCI_Tn_BASE(window) = virt_to_phys(ppte) >> 2;
}

static void __init
verify_tb_operation(void)
{
	static int page[PAGE_SIZE/4]
		__attribute__((aligned(PAGE_SIZE)))
		__initdata = { 0 };

	struct pci_iommu_arena *arena = pci_isa_hose->sg_isa;
	int ctrl, addr0, tag0, pte0, data0;
	int temp, use_tbia_try2 = 0;
	void __iomem *bus_addr;

	/* pyxis -- tbia is broken */
	if (pci_isa_hose->dense_io_base)
		use_tbia_try2 = 1;

	/* Put the chip into PCI loopback mode.  */
	mb();
	ctrl = *(vip)CIA_IOC_CIA_CTRL;
	*(vip)CIA_IOC_CIA_CTRL = ctrl | CIA_CTRL_PCI_LOOP_EN;
	mb();
	*(vip)CIA_IOC_CIA_CTRL;
	mb();

	/* Write a valid entry directly into the TLB registers.  */

	addr0 = arena->dma_base;
	tag0 = addr0 | 1;
	pte0 = (virt_to_phys(page) >> (PAGE_SHIFT - 1)) | 1;

	*(vip)CIA_IOC_TB_TAGn(0) = tag0;
	*(vip)CIA_IOC_TB_TAGn(1) = 0;
	*(vip)CIA_IOC_TB_TAGn(2) = 0;
	*(vip)CIA_IOC_TB_TAGn(3) = 0;
	*(vip)CIA_IOC_TB_TAGn(4) = 0;
	*(vip)CIA_IOC_TB_TAGn(5) = 0;
	*(vip)CIA_IOC_TB_TAGn(6) = 0;
	*(vip)CIA_IOC_TB_TAGn(7) = 0;
	*(vip)CIA_IOC_TBn_PAGEm(0,0) = pte0;
	*(vip)CIA_IOC_TBn_PAGEm(0,1) = 0;
	*(vip)CIA_IOC_TBn_PAGEm(0,2) = 0;
	*(vip)CIA_IOC_TBn_PAGEm(0,3) = 0;
	mb();

	/* Get a usable bus address */
	bus_addr = cia_ioremap(addr0, 8*PAGE_SIZE);

	/* First, verify we can read back what we've written.  If
	   this fails, we can't be sure of any of the other testing
	   we're going to do, so bail.  */
	/* ??? Actually, we could do the work with machine checks.
	   By passing this register update test, we pretty much
	   guarantee that cia_pci_tbi_try1 works.  If this test
	   fails, cia_pci_tbi_try2 might still work.  */

	temp = *(vip)CIA_IOC_TB_TAGn(0);
	if (temp != tag0) {
		printk("pci: failed tb register update test "
		       "(tag0 %#x != %#x)\n", temp, tag0);
		goto failed;
	}
	temp = *(vip)CIA_IOC_TB_TAGn(1);
	if (temp != 0) {
		printk("pci: failed tb register update test "
		       "(tag1 %#x != 0)\n", temp);
		goto failed;
	}
	temp = *(vip)CIA_IOC_TBn_PAGEm(0,0);
	if (temp != pte0) {
		printk("pci: failed tb register update test "
		       "(pte0 %#x != %#x)\n", temp, pte0);
		goto failed;
	}
	printk("pci: passed tb register update test\n");

	/* Second, verify we can actually do I/O through this entry.  */

	data0 = 0xdeadbeef;
	page[0] = data0;
	mcheck_expected(0) = 1;
	mcheck_taken(0) = 0;
	mb();
	temp = cia_readl(bus_addr);
	mb();
	mcheck_expected(0) = 0;
	mb();
	if (mcheck_taken(0)) {
		printk("pci: failed sg loopback i/o read test (mcheck)\n");
		goto failed;
	}
	if (temp != data0) {
		printk("pci: failed sg loopback i/o read test "
		       "(%#x != %#x)\n", temp, data0);
		goto failed;
	}
	printk("pci: passed sg loopback i/o read test\n");

	/* Third, try to invalidate the TLB.  */

	if (! use_tbia_try2) {
		cia_pci_tbi(arena->hose, 0, -1);
		temp = *(vip)CIA_IOC_TB_TAGn(0);
		if (temp & 1) {
			use_tbia_try2 = 1;
			printk("pci: failed tbia test; workaround available\n");
		} else {
			printk("pci: passed tbia test\n");
		}
	}

	/* Fourth, verify the TLB snoops the EV5's caches when
	   doing a tlb fill.  */

	data0 = 0x5adda15e;
	page[0] = data0;
	arena->ptes[4] = pte0;
	mcheck_expected(0) = 1;
	mcheck_taken(0) = 0;
	mb();
	temp = cia_readl(bus_addr + 4*PAGE_SIZE);
	mb();
	mcheck_expected(0) = 0;
	mb();
	if (mcheck_taken(0)) {
		printk("pci: failed pte write cache snoop test (mcheck)\n");
		goto failed;
	}
	if (temp != data0) {
		printk("pci: failed pte write cache snoop test "
		       "(%#x != %#x)\n", temp, data0);
		goto failed;
	}
	printk("pci: passed pte write cache snoop test\n");

	/* Fifth, verify that a previously invalid PTE entry gets
	   filled from the page table.  */

	data0 = 0xabcdef12;
	page[0] = data0;
	arena->ptes[5] = pte0;
	mcheck_expected(0) = 1;
	mcheck_taken(0) = 0;
	mb();
	temp = cia_readl(bus_addr + 5*PAGE_SIZE);
	mb();
	mcheck_expected(0) = 0;
	mb();
	if (mcheck_taken(0)) {
		printk("pci: failed valid tag invalid pte reload test "
		       "(mcheck; workaround available)\n");
		/* Work around this bug by aligning new allocations
		   on 4 page boundaries.  */
		arena->align_entry = 4;
	} else if (temp != data0) {
		printk("pci: failed valid tag invalid pte reload test "
		       "(%#x != %#x)\n", temp, data0);
		goto failed;
	} else {
		printk("pci: passed valid tag invalid pte reload test\n");
	}

	/* Sixth, verify machine checks are working.  Test invalid
	   pte under the same valid tag as we used above.  */

	mcheck_expected(0) = 1;
	mcheck_taken(0) = 0;
	mb();
	temp = cia_readl(bus_addr + 6*PAGE_SIZE);
	mb();
	mcheck_expected(0) = 0;
	mb();
	printk("pci: %s pci machine check test\n",
	       mcheck_taken(0) ? "passed" : "failed");

	/* Clean up after the tests.  */
	arena->ptes[4] = 0;
	arena->ptes[5] = 0;

	if (use_tbia_try2) {
		alpha_mv.mv_pci_tbi = cia_pci_tbi_try2;

		/* Tags 0-3 must be disabled if we use this workaraund. */
		wmb();
		*(vip)CIA_IOC_TB_TAGn(0) = 2;
		*(vip)CIA_IOC_TB_TAGn(1) = 2;
		*(vip)CIA_IOC_TB_TAGn(2) = 2;
		*(vip)CIA_IOC_TB_TAGn(3) = 2;

		printk("pci: tbia workaround enabled\n");
	}
	alpha_mv.mv_pci_tbi(arena->hose, 0, -1);

exit:
	/* unmap the bus addr */
	cia_iounmap(bus_addr);

	/* Restore normal PCI operation.  */
	mb();
	*(vip)CIA_IOC_CIA_CTRL = ctrl;
	mb();
	*(vip)CIA_IOC_CIA_CTRL;
	mb();
	return;

failed:
	printk("pci: disabling sg translation window\n");
	*(vip)CIA_IOC_PCI_W0_BASE = 0;
	*(vip)CIA_IOC_PCI_W1_BASE = 0;
	pci_isa_hose->sg_isa = NULL;
	alpha_mv.mv_pci_tbi = NULL;
	goto exit;
}

#if defined(ALPHA_RESTORE_SRM_SETUP)
/* Save CIA configuration data as the console had it set up.  */
struct 
{
    unsigned int hae_mem;
    unsigned int hae_io;
    unsigned int pci_dac_offset;
    unsigned int err_mask;
    unsigned int cia_ctrl;
    unsigned int cia_cnfg;
    struct {
	unsigned int w_base;
	unsigned int w_mask;
	unsigned int t_base;
    } window[4];
} saved_config __attribute((common));

void
cia_save_srm_settings(int is_pyxis)
{
	int i;

	/* Save some important registers. */
	saved_config.err_mask       = *(vip)CIA_IOC_ERR_MASK;
	saved_config.cia_ctrl       = *(vip)CIA_IOC_CIA_CTRL;
	saved_config.hae_mem        = *(vip)CIA_IOC_HAE_MEM;
	saved_config.hae_io         = *(vip)CIA_IOC_HAE_IO;
	saved_config.pci_dac_offset = *(vip)CIA_IOC_PCI_W_DAC;

	if (is_pyxis)
	    saved_config.cia_cnfg   = *(vip)CIA_IOC_CIA_CNFG;
	else
	    saved_config.cia_cnfg   = 0;

	/* Save DMA windows configuration. */
	for (i = 0; i < 4; i++) {
	    saved_config.window[i].w_base = *(vip)CIA_IOC_PCI_Wn_BASE(i);
	    saved_config.window[i].w_mask = *(vip)CIA_IOC_PCI_Wn_MASK(i);
	    saved_config.window[i].t_base = *(vip)CIA_IOC_PCI_Tn_BASE(i);
	}
	mb();
}

void
cia_restore_srm_settings(void)
{
	int i;

	for (i = 0; i < 4; i++) {
	    *(vip)CIA_IOC_PCI_Wn_BASE(i) = saved_config.window[i].w_base;
	    *(vip)CIA_IOC_PCI_Wn_MASK(i) = saved_config.window[i].w_mask;
	    *(vip)CIA_IOC_PCI_Tn_BASE(i) = saved_config.window[i].t_base;
	}

	*(vip)CIA_IOC_HAE_MEM   = saved_config.hae_mem;
	*(vip)CIA_IOC_HAE_IO    = saved_config.hae_io;
	*(vip)CIA_IOC_PCI_W_DAC = saved_config.pci_dac_offset;	
	*(vip)CIA_IOC_ERR_MASK  = saved_config.err_mask;
	*(vip)CIA_IOC_CIA_CTRL  = saved_config.cia_ctrl;

	if (saved_config.cia_cnfg) /* Must be pyxis. */
	    *(vip)CIA_IOC_CIA_CNFG  = saved_config.cia_cnfg;

	mb();
}
#else /* ALPHA_RESTORE_SRM_SETUP */
#define cia_save_srm_settings(p)	do {} while (0)
#define cia_restore_srm_settings()	do {} while (0)
#endif /* ALPHA_RESTORE_SRM_SETUP */


static void __init
do_init_arch(int is_pyxis)
{
	struct pci_controller *hose;
	int temp, cia_rev, tbia_window;

	cia_rev = *(vip)CIA_IOC_CIA_REV & CIA_REV_MASK;
	printk("pci: cia revision %d%s\n",
	       cia_rev, is_pyxis ? " (pyxis)" : "");

	if (alpha_using_srm)
		cia_save_srm_settings(is_pyxis);

	/* Set up error reporting.  */
	temp = *(vip)CIA_IOC_ERR_MASK;
	temp &= ~(CIA_ERR_CPU_PE | CIA_ERR_MEM_NEM | CIA_ERR_PA_PTE_INV
		  | CIA_ERR_RCVD_MAS_ABT | CIA_ERR_RCVD_TAR_ABT);
	*(vip)CIA_IOC_ERR_MASK = temp;

	/* Clear all currently pending errors.  */
	temp = *(vip)CIA_IOC_CIA_ERR;
	*(vip)CIA_IOC_CIA_ERR = temp;

	/* Turn on mchecks.  */
	temp = *(vip)CIA_IOC_CIA_CTRL;
	temp |= CIA_CTRL_FILL_ERR_EN | CIA_CTRL_MCHK_ERR_EN;
	*(vip)CIA_IOC_CIA_CTRL = temp;

	/* Clear the CFG register, which gets used for PCI config space
	   accesses.  That is the way we want to use it, and we do not
	   want to depend on what ARC or SRM might have left behind.  */
	*(vip)CIA_IOC_CFG = 0;
 
	/* Zero the HAEs.  */
	*(vip)CIA_IOC_HAE_MEM = 0;
	*(vip)CIA_IOC_HAE_IO = 0;

	/* For PYXIS, we always use BWX bus and i/o accesses.  To that end,
	   make sure they're enabled on the controller.  At the same time,
	   enable the monster window.  */
	if (is_pyxis) {
		temp = *(vip)CIA_IOC_CIA_CNFG;
		temp |= CIA_CNFG_IOA_BWEN | CIA_CNFG_PCI_MWEN;
		*(vip)CIA_IOC_CIA_CNFG = temp;
	}

	/* Synchronize with all previous changes.  */
	mb();
	*(vip)CIA_IOC_CIA_REV;

	/*
	 * Create our single hose.
	 */

	pci_isa_hose = hose = alloc_pci_controller();
	hose->io_space = &ioport_resource;
	hose->mem_space = &iomem_resource;
	hose->index = 0;

	if (! is_pyxis) {
		struct resource *hae_mem = alloc_resource();
		hose->mem_space = hae_mem;

		hae_mem->start = 0;
		hae_mem->end = CIA_MEM_R1_MASK;
		hae_mem->name = pci_hae0_name;
		hae_mem->flags = IORESOURCE_MEM;

		if (request_resource(&iomem_resource, hae_mem) < 0)
			printk(KERN_ERR "Failed to request HAE_MEM\n");

		hose->sparse_mem_base = CIA_SPARSE_MEM - IDENT_ADDR;
		hose->dense_mem_base = CIA_DENSE_MEM - IDENT_ADDR;
		hose->sparse_io_base = CIA_IO - IDENT_ADDR;
		hose->dense_io_base = 0;
	} else {
		hose->sparse_mem_base = 0;
		hose->dense_mem_base = CIA_BW_MEM - IDENT_ADDR;
		hose->sparse_io_base = 0;
		hose->dense_io_base = CIA_BW_IO - IDENT_ADDR;
	}

	/*
	 * Set up the PCI to main memory translation windows.
	 *
	 * Window 0 is S/G 8MB at 8MB (for isa)
	 * Window 1 is S/G 1MB at 768MB (for tbia) (unused for CIA rev 1)
	 * Window 2 is direct access 2GB at 2GB
	 * Window 3 is DAC access 4GB at 8GB (or S/G for tbia if CIA rev 1)
	 *
	 * ??? NetBSD hints that page tables must be aligned to 32K,
	 * possibly due to a hardware bug.  This is over-aligned
	 * from the 8K alignment one would expect for an 8MB window. 
	 * No description of what revisions affected.
	 */

	hose->sg_pci = NULL;
	hose->sg_isa = iommu_arena_new(hose, 0x00800000, 0x00800000, 32768);

	__direct_map_base = 0x80000000;
	__direct_map_size = 0x80000000;

	*(vip)CIA_IOC_PCI_W0_BASE = hose->sg_isa->dma_base | 3;
	*(vip)CIA_IOC_PCI_W0_MASK = (hose->sg_isa->size - 1) & 0xfff00000;
	*(vip)CIA_IOC_PCI_T0_BASE = virt_to_phys(hose->sg_isa->ptes) >> 2;

	*(vip)CIA_IOC_PCI_W2_BASE = __direct_map_base | 1;
	*(vip)CIA_IOC_PCI_W2_MASK = (__direct_map_size - 1) & 0xfff00000;
	*(vip)CIA_IOC_PCI_T2_BASE = 0 >> 2;

	/* On PYXIS we have the monster window, selected by bit 40, so
	   there is no need for window3 to be enabled.

	   On CIA, we don't have true arbitrary addressing -- bits <39:32>
	   are compared against W_DAC.  We can, however, directly map 4GB,
	   which is better than before.  However, due to assumptions made
	   elsewhere, we should not claim that we support DAC unless that
	   4GB covers all of physical memory.

	   On CIA rev 1, apparently W1 and W2 can't be used for SG. 
	   At least, there are reports that it doesn't work for Alcor. 
	   In that case, we have no choice but to use W3 for the TBIA 
	   workaround, which means we can't use DAC at all. */ 

	tbia_window = 1;
	if (is_pyxis) {
		*(vip)CIA_IOC_PCI_W3_BASE = 0;
	} else if (cia_rev == 1) {
		*(vip)CIA_IOC_PCI_W1_BASE = 0;
		tbia_window = 3;
	} else if (max_low_pfn > (0x100000000UL >> PAGE_SHIFT)) {
		*(vip)CIA_IOC_PCI_W3_BASE = 0;
	} else {
		*(vip)CIA_IOC_PCI_W3_BASE = 0x00000000 | 1 | 8;
		*(vip)CIA_IOC_PCI_W3_MASK = 0xfff00000;
		*(vip)CIA_IOC_PCI_T3_BASE = 0 >> 2;

		alpha_mv.pci_dac_offset = 0x200000000UL;
		*(vip)CIA_IOC_PCI_W_DAC = alpha_mv.pci_dac_offset >> 32;
	}

	/* Prepare workaround for apparently broken tbia. */
	cia_prepare_tbia_workaround(tbia_window);
}

void __init
cia_init_arch(void)
{
	do_init_arch(0);
}

void __init
pyxis_init_arch(void)
{
	/* On pyxis machines we can precisely calculate the
	   CPU clock frequency using pyxis real time counter.
	   It's especially useful for SX164 with broken RTC.

	   Both CPU and chipset are driven by the single 16.666M
	   or 16.667M crystal oscillator. PYXIS_RT_COUNT clock is
	   66.66 MHz. -ink */

	unsigned int cc0, cc1;
	unsigned long pyxis_cc;

	__asm__ __volatile__ ("rpcc %0" : "=r"(cc0));
	pyxis_cc = *(vulp)PYXIS_RT_COUNT;
	do { } while(*(vulp)PYXIS_RT_COUNT - pyxis_cc < 4096);
	__asm__ __volatile__ ("rpcc %0" : "=r"(cc1));
	cc1 -= cc0;
	hwrpb->cycle_freq = ((cc1 >> 11) * 100000000UL) / 3;
	hwrpb_update_checksum(hwrpb);

	do_init_arch(1);
}

void
cia_kill_arch(int mode)
{
	if (alpha_using_srm)
		cia_restore_srm_settings();
}

void __init
cia_init_pci(void)
{
	/* Must delay this from init_arch, as we need machine checks.  */
	verify_tb_operation();
	common_init_pci();
}

static inline void
cia_pci_clr_err(void)
{
	int jd;

	jd = *(vip)CIA_IOC_CIA_ERR;
	*(vip)CIA_IOC_CIA_ERR = jd;
	mb();
	*(vip)CIA_IOC_CIA_ERR;		/* re-read to force write.  */
}

#ifdef CONFIG_VERBOSE_MCHECK
static void
cia_decode_pci_error(struct el_CIA_sysdata_mcheck *cia, const char *msg)
{
	static const char * const pci_cmd_desc[16] = {
		"Interrupt Acknowledge", "Special Cycle", "I/O Read",
		"I/O Write", "Reserved 0x4", "Reserved 0x5", "Memory Read",
		"Memory Write", "Reserved 0x8", "Reserved 0x9",
		"Configuration Read", "Configuration Write",
		"Memory Read Multiple", "Dual Address Cycle",
		"Memory Read Line", "Memory Write and Invalidate"
	};

	if (cia->cia_err & (CIA_ERR_COR_ERR
			    | CIA_ERR_UN_COR_ERR
			    | CIA_ERR_MEM_NEM
			    | CIA_ERR_PA_PTE_INV)) {
		static const char * const window_desc[6] = {
			"No window active", "Window 0 hit", "Window 1 hit",
			"Window 2 hit", "Window 3 hit", "Monster window hit"
		};

		const char *window;
		const char *cmd;
		unsigned long addr, tmp;
		int lock, dac;
	
		cmd = pci_cmd_desc[cia->pci_err0 & 0x7];
		lock = (cia->pci_err0 >> 4) & 1;
		dac = (cia->pci_err0 >> 5) & 1;

		tmp = (cia->pci_err0 >> 8) & 0x1F;
		tmp = ffs(tmp);
		window = window_desc[tmp];

		addr = cia->pci_err1;
		if (dac) {
			tmp = *(vip)CIA_IOC_PCI_W_DAC & 0xFFUL;
			addr |= tmp << 32;
		}

		printk(KERN_CRIT "CIA machine check: %s\n", msg);
		printk(KERN_CRIT "  DMA command: %s\n", cmd);
		printk(KERN_CRIT "  PCI address: %#010lx\n", addr);
		printk(KERN_CRIT "  %s, Lock: %d, DAC: %d\n",
		       window, lock, dac);
	} else if (cia->cia_err & (CIA_ERR_PERR
				   | CIA_ERR_PCI_ADDR_PE
				   | CIA_ERR_RCVD_MAS_ABT
				   | CIA_ERR_RCVD_TAR_ABT
				   | CIA_ERR_IOA_TIMEOUT)) {
		static const char * const master_st_desc[16] = {
			"Idle", "Drive bus", "Address step cycle",
			"Address cycle", "Data cycle", "Last read data cycle",
			"Last write data cycle", "Read stop cycle",
			"Write stop cycle", "Read turnaround cycle",
			"Write turnaround cycle", "Reserved 0xB",
			"Reserved 0xC", "Reserved 0xD", "Reserved 0xE",
			"Unknown state"
		};
		static const char * const target_st_desc[16] = {
			"Idle", "Busy", "Read data cycle", "Write data cycle",
			"Read stop cycle", "Write stop cycle",
			"Read turnaround cycle", "Write turnaround cycle",
			"Read wait cycle", "Write wait cycle",
			"Reserved 0xA", "Reserved 0xB", "Reserved 0xC",
			"Reserved 0xD", "Reserved 0xE", "Unknown state"
		};

		const char *cmd;
		const char *master, *target;
		unsigned long addr, tmp;
		int dac;

		master = master_st_desc[(cia->pci_err0 >> 16) & 0xF];
		target = target_st_desc[(cia->pci_err0 >> 20) & 0xF];
		cmd = pci_cmd_desc[(cia->pci_err0 >> 24) & 0xF];
		dac = (cia->pci_err0 >> 28) & 1;

		addr = cia->pci_err2;
		if (dac) {
			tmp = *(volatile int *)CIA_IOC_PCI_W_DAC & 0xFFUL;
			addr |= tmp << 32;
		}

		printk(KERN_CRIT "CIA machine check: %s\n", msg);
		printk(KERN_CRIT "  PCI command: %s\n", cmd);
		printk(KERN_CRIT "  Master state: %s, Target state: %s\n",
		       master, target);
		printk(KERN_CRIT "  PCI address: %#010lx, DAC: %d\n",
		       addr, dac);
	} else {
		printk(KERN_CRIT "CIA machine check: %s\n", msg);
		printk(KERN_CRIT "  Unknown PCI error\n");
		printk(KERN_CRIT "  PCI_ERR0 = %#08lx", cia->pci_err0);
		printk(KERN_CRIT "  PCI_ERR1 = %#08lx", cia->pci_err1);
		printk(KERN_CRIT "  PCI_ERR2 = %#08lx", cia->pci_err2);
	}
}

static void
cia_decode_mem_error(struct el_CIA_sysdata_mcheck *cia, const char *msg)
{
	unsigned long mem_port_addr;
	unsigned long mem_port_mask;
	const char *mem_port_cmd;
	const char *seq_state;
	const char *set_select;
	unsigned long tmp;

	/* If this is a DMA command, also decode the PCI bits.  */
	if ((cia->mem_err1 >> 20) & 1)
		cia_decode_pci_error(cia, msg);
	else
		printk(KERN_CRIT "CIA machine check: %s\n", msg);

	mem_port_addr = cia->mem_err0 & 0xfffffff0;
	mem_port_addr |= (cia->mem_err1 & 0x83UL) << 32;

	mem_port_mask = (cia->mem_err1 >> 12) & 0xF;

	tmp = (cia->mem_err1 >> 8) & 0xF;
	tmp |= ((cia->mem_err1 >> 20) & 1) << 4;
	if ((tmp & 0x1E) == 0x06)
		mem_port_cmd = "WRITE BLOCK or WRITE BLOCK LOCK";
	else if ((tmp & 0x1C) == 0x08)
		mem_port_cmd = "READ MISS or READ MISS MODIFY";
	else if (tmp == 0x1C)
		mem_port_cmd = "BC VICTIM";
	else if ((tmp & 0x1E) == 0x0E)
		mem_port_cmd = "READ MISS MODIFY";
	else if ((tmp & 0x1C) == 0x18)
		mem_port_cmd = "DMA READ or DMA READ MODIFY";
	else if ((tmp & 0x1E) == 0x12)
		mem_port_cmd = "DMA WRITE";
	else
		mem_port_cmd = "Unknown";

	tmp = (cia->mem_err1 >> 16) & 0xF;
	switch (tmp) {
	case 0x0:
		seq_state = "Idle";
		break;
	case 0x1:
		seq_state = "DMA READ or DMA WRITE";
		break;
	case 0x2: case 0x3:
		seq_state = "READ MISS (or READ MISS MODIFY) with victim";
		break;
	case 0x4: case 0x5: case 0x6:
		seq_state = "READ MISS (or READ MISS MODIFY) with no victim";
		break;
	case 0x8: case 0x9: case 0xB:
		seq_state = "Refresh";
		break;
	case 0xC:
		seq_state = "Idle, waiting for DMA pending read";
		break;
	case 0xE: case 0xF:
		seq_state = "Idle, ras precharge";
		break;
	default:
		seq_state = "Unknown";
		break;
	}

	tmp = (cia->mem_err1 >> 24) & 0x1F;
	switch (tmp) {
	case 0x00: set_select = "Set 0 selected"; break;
	case 0x01: set_select = "Set 1 selected"; break;
	case 0x02: set_select = "Set 2 selected"; break;
	case 0x03: set_select = "Set 3 selected"; break;
	case 0x04: set_select = "Set 4 selected"; break;
	case 0x05: set_select = "Set 5 selected"; break;
	case 0x06: set_select = "Set 6 selected"; break;
	case 0x07: set_select = "Set 7 selected"; break;
	case 0x08: set_select = "Set 8 selected"; break;
	case 0x09: set_select = "Set 9 selected"; break;
	case 0x0A: set_select = "Set A selected"; break;
	case 0x0B: set_select = "Set B selected"; break;
	case 0x0C: set_select = "Set C selected"; break;
	case 0x0D: set_select = "Set D selected"; break;
	case 0x0E: set_select = "Set E selected"; break;
	case 0x0F: set_select = "Set F selected"; break;
	case 0x10: set_select = "No set selected"; break;
	case 0x1F: set_select = "Refresh cycle"; break;
	default:   set_select = "Unknown"; break;
	}

	printk(KERN_CRIT "  Memory port command: %s\n", mem_port_cmd);
	printk(KERN_CRIT "  Memory port address: %#010lx, mask: %#lx\n",
	       mem_port_addr, mem_port_mask);
	printk(KERN_CRIT "  Memory sequencer state: %s\n", seq_state);
	printk(KERN_CRIT "  Memory set: %s\n", set_select);
}

static void
cia_decode_ecc_error(struct el_CIA_sysdata_mcheck *cia, const char *msg)
{
	long syn;
	long i;
	const char *fmt;

	cia_decode_mem_error(cia, msg);

	syn = cia->cia_syn & 0xff;
	if (syn == (syn & -syn)) {
		fmt = KERN_CRIT "  ECC syndrome %#x -- check bit %d\n";
		i = ffs(syn) - 1;
	} else {
		static unsigned char const data_bit[64] = {
			0xCE, 0xCB, 0xD3, 0xD5,
			0xD6, 0xD9, 0xDA, 0xDC,
			0x23, 0x25, 0x26, 0x29,
			0x2A, 0x2C, 0x31, 0x34,
			0x0E, 0x0B, 0x13, 0x15,
			0x16, 0x19, 0x1A, 0x1C,
			0xE3, 0xE5, 0xE6, 0xE9,
			0xEA, 0xEC, 0xF1, 0xF4,
			0x4F, 0x4A, 0x52, 0x54,
			0x57, 0x58, 0x5B, 0x5D,
			0xA2, 0xA4, 0xA7, 0xA8,
			0xAB, 0xAD, 0xB0, 0xB5,
			0x8F, 0x8A, 0x92, 0x94,
			0x97, 0x98, 0x9B, 0x9D,
			0x62, 0x64, 0x67, 0x68,
			0x6B, 0x6D, 0x70, 0x75
		};

		for (i = 0; i < 64; ++i)
			if (data_bit[i] == syn)
				break;

		if (i < 64)
			fmt = KERN_CRIT "  ECC syndrome %#x -- data bit %d\n";
		else
			fmt = KERN_CRIT "  ECC syndrome %#x -- unknown bit\n";
	}

	printk (fmt, syn, i);
}

static void
cia_decode_parity_error(struct el_CIA_sysdata_mcheck *cia)
{
	static const char * const cmd_desc[16] = {
		"NOP", "LOCK", "FETCH", "FETCH_M", "MEMORY BARRIER",
		"SET DIRTY", "WRITE BLOCK", "WRITE BLOCK LOCK",
		"READ MISS0", "READ MISS1", "READ MISS MOD0",
		"READ MISS MOD1", "BCACHE VICTIM", "Spare",
		"READ MISS MOD STC0", "READ MISS MOD STC1"
	};

	unsigned long addr;
	unsigned long mask;
	const char *cmd;
	int par;

	addr = cia->cpu_err0 & 0xfffffff0;
	addr |= (cia->cpu_err1 & 0x83UL) << 32;
	cmd = cmd_desc[(cia->cpu_err1 >> 8) & 0xF];
	mask = (cia->cpu_err1 >> 12) & 0xF;
	par = (cia->cpu_err1 >> 21) & 1;

	printk(KERN_CRIT "CIA machine check: System bus parity error\n");
	printk(KERN_CRIT "  Command: %s, Parity bit: %d\n", cmd, par);
	printk(KERN_CRIT "  Address: %#010lx, Mask: %#lx\n", addr, mask);
}
#endif /* CONFIG_VERBOSE_MCHECK */


static int
cia_decode_mchk(unsigned long la_ptr)
{
	struct el_common *com;
	struct el_CIA_sysdata_mcheck *cia;

	com = (void *)la_ptr;
	cia = (void *)(la_ptr + com->sys_offset);

	if ((cia->cia_err & CIA_ERR_VALID) == 0)
		return 0;

#ifdef CONFIG_VERBOSE_MCHECK
	if (!alpha_verbose_mcheck)
		return 1;

	switch (ffs(cia->cia_err & 0xfff) - 1) {
	case 0: /* CIA_ERR_COR_ERR */
		cia_decode_ecc_error(cia, "Corrected ECC error");
		break;
	case 1: /* CIA_ERR_UN_COR_ERR */
		cia_decode_ecc_error(cia, "Uncorrected ECC error");
		break;
	case 2: /* CIA_ERR_CPU_PE */
		cia_decode_parity_error(cia);
		break;
	case 3: /* CIA_ERR_MEM_NEM */
		cia_decode_mem_error(cia, "Access to nonexistent memory");
		break;
	case 4: /* CIA_ERR_PCI_SERR */
		cia_decode_pci_error(cia, "PCI bus system error");
		break;
	case 5: /* CIA_ERR_PERR */
		cia_decode_pci_error(cia, "PCI data parity error");
		break;
	case 6: /* CIA_ERR_PCI_ADDR_PE */
		cia_decode_pci_error(cia, "PCI address parity error");
		break;
	case 7: /* CIA_ERR_RCVD_MAS_ABT */
		cia_decode_pci_error(cia, "PCI master abort");
		break;
	case 8: /* CIA_ERR_RCVD_TAR_ABT */
		cia_decode_pci_error(cia, "PCI target abort");
		break;
	case 9: /* CIA_ERR_PA_PTE_INV */
		cia_decode_pci_error(cia, "PCI invalid PTE");
		break;
	case 10: /* CIA_ERR_FROM_WRT_ERR */
		cia_decode_mem_error(cia, "Write to flash ROM attempted");
		break;
	case 11: /* CIA_ERR_IOA_TIMEOUT */
		cia_decode_pci_error(cia, "I/O timeout");
		break;
	}

	if (cia->cia_err & CIA_ERR_LOST_CORR_ERR)
		printk(KERN_CRIT "CIA lost machine check: "
		       "Correctable ECC error\n");
	if (cia->cia_err & CIA_ERR_LOST_UN_CORR_ERR)
		printk(KERN_CRIT "CIA lost machine check: "
		       "Uncorrectable ECC error\n");
	if (cia->cia_err & CIA_ERR_LOST_CPU_PE)
		printk(KERN_CRIT "CIA lost machine check: "
		       "System bus parity error\n");
	if (cia->cia_err & CIA_ERR_LOST_MEM_NEM)
		printk(KERN_CRIT "CIA lost machine check: "
		       "Access to nonexistent memory\n");
	if (cia->cia_err & CIA_ERR_LOST_PERR)
		printk(KERN_CRIT "CIA lost machine check: "
		       "PCI data parity error\n");
	if (cia->cia_err & CIA_ERR_LOST_PCI_ADDR_PE)
		printk(KERN_CRIT "CIA lost machine check: "
		       "PCI address parity error\n");
	if (cia->cia_err & CIA_ERR_LOST_RCVD_MAS_ABT)
		printk(KERN_CRIT "CIA lost machine check: "
		       "PCI master abort\n");
	if (cia->cia_err & CIA_ERR_LOST_RCVD_TAR_ABT)
		printk(KERN_CRIT "CIA lost machine check: "
		       "PCI target abort\n");
	if (cia->cia_err & CIA_ERR_LOST_PA_PTE_INV)
		printk(KERN_CRIT "CIA lost machine check: "
		       "PCI invalid PTE\n");
	if (cia->cia_err & CIA_ERR_LOST_FROM_WRT_ERR)
		printk(KERN_CRIT "CIA lost machine check: "
		       "Write to flash ROM attempted\n");
	if (cia->cia_err & CIA_ERR_LOST_IOA_TIMEOUT)
		printk(KERN_CRIT "CIA lost machine check: "
		       "I/O timeout\n");
#endif /* CONFIG_VERBOSE_MCHECK */

	return 1;
}

void
cia_machine_check(unsigned long vector, unsigned long la_ptr)
{
	int expected;

	/* Clear the error before any reporting.  */
	mb();
	mb();  /* magic */
	draina();
	cia_pci_clr_err();
	wrmces(rdmces());	/* reset machine check pending flag.  */
	mb();

	expected = mcheck_expected(0);
	if (!expected && vector == 0x660)
		expected = cia_decode_mchk(la_ptr);
	process_mcheck_info(vector, la_ptr, "CIA", expected);
}
