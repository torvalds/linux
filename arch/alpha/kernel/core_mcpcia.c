// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/core_mcpcia.c
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
 *
 * Code common to all MCbus-PCI Adaptor core logic chipsets
 */

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_mcpcia.h>
#undef __EXTERN_INLINE

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/ptrace.h>

#include "proto.h"
#include "pci_impl.h"

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CFG 0

#if DEBUG_CFG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif

/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the MCPCIA_HAXR2 register
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

static unsigned int
conf_read(unsigned long addr, unsigned char type1,
	  struct pci_controller *hose)
{
	unsigned long flags;
	unsigned long mid = MCPCIA_HOSE2MID(hose->index);
	unsigned int stat0, value, cpu;

	cpu = smp_processor_id();

	local_irq_save(flags);

	DBG_CFG(("conf_read(addr=0x%lx, type1=%d, hose=%d)\n",
		 addr, type1, mid));

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)MCPCIA_CAP_ERR(mid);
	*(vuip)MCPCIA_CAP_ERR(mid) = stat0;
	mb();
	*(vuip)MCPCIA_CAP_ERR(mid);
	DBG_CFG(("conf_read: MCPCIA_CAP_ERR(%d) was 0x%x\n", mid, stat0));

	mb();
	draina();
	mcheck_expected(cpu) = 1;
	mcheck_taken(cpu) = 0;
	mcheck_extra(cpu) = mid;
	mb();

	/* Access configuration space.  */
	value = *((vuip)addr);
	mb();
	mb();  /* magic */

	if (mcheck_taken(cpu)) {
		mcheck_taken(cpu) = 0;
		value = 0xffffffffU;
		mb();
	}
	mcheck_expected(cpu) = 0;
	mb();

	DBG_CFG(("conf_read(): finished\n"));

	local_irq_restore(flags);
	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1,
	   struct pci_controller *hose)
{
	unsigned long flags;
	unsigned long mid = MCPCIA_HOSE2MID(hose->index);
	unsigned int stat0, cpu;

	cpu = smp_processor_id();

	local_irq_save(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)MCPCIA_CAP_ERR(mid);
	*(vuip)MCPCIA_CAP_ERR(mid) = stat0; mb();
	*(vuip)MCPCIA_CAP_ERR(mid);
	DBG_CFG(("conf_write: MCPCIA CAP_ERR(%d) was 0x%x\n", mid, stat0));

	draina();
	mcheck_expected(cpu) = 1;
	mcheck_extra(cpu) = mid;
	mb();

	/* Access configuration space.  */
	*((vuip)addr) = value;
	mb();
	mb();  /* magic */
	*(vuip)MCPCIA_CAP_ERR(mid); /* read to force the write */
	mcheck_expected(cpu) = 0;
	mb();

	DBG_CFG(("conf_write(): finished\n"));
	local_irq_restore(flags);
}

static int
mk_conf_addr(struct pci_bus *pbus, unsigned int devfn, int where,
	     struct pci_controller *hose, unsigned long *pci_addr,
	     unsigned char *type1)
{
	u8 bus = pbus->number;
	unsigned long addr;

	DBG_CFG(("mk_conf_addr(bus=%d,devfn=0x%x,hose=%d,where=0x%x,"
		 " pci_addr=0x%p, type1=0x%p)\n",
		 bus, devfn, hose->index, where, pci_addr, type1));

	/* Type 1 configuration cycle for *ALL* busses.  */
	*type1 = 1;

	if (!pbus->parent) /* No parent means peer PCI bus. */
		bus = 0;
	addr = (bus << 16) | (devfn << 8) | (where);
	addr <<= 5; /* swizzle for SPARSE */
	addr |= hose->config_space_base;

	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static int
mcpcia_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		   int size, u32 *value)
{
	struct pci_controller *hose = bus->sysdata;
	unsigned long addr, w;
	unsigned char type1;

	if (mk_conf_addr(bus, devfn, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (size - 1) * 8;
	w = conf_read(addr, type1, hose);
	switch (size) {
	case 1:
		*value = __kernel_extbl(w, where & 3);
		break;
	case 2:
		*value = __kernel_extwl(w, where & 3);
		break;
	case 4:
		*value = w;
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int
mcpcia_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 value)
{
	struct pci_controller *hose = bus->sysdata;
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, devfn, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= (size - 1) * 8;
	value = __kernel_insql(value, where & 3);
	conf_write(addr, value, type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops mcpcia_pci_ops = 
{
	.read =		mcpcia_read_config,
	.write =	mcpcia_write_config,
};

void
mcpcia_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	wmb();
	*(vuip)MCPCIA_SG_TBIA(MCPCIA_HOSE2MID(hose->index)) = 0;
	mb();
}

static int __init
mcpcia_probe_hose(int h)
{
	int cpu = smp_processor_id();
	int mid = MCPCIA_HOSE2MID(h);
	unsigned int pci_rev;

	/* Gotta be REAL careful.  If hose is absent, we get an mcheck.  */

	mb();
	mb();
	draina();
	wrmces(7);

	mcheck_expected(cpu) = 2;	/* indicates probing */
	mcheck_taken(cpu) = 0;
	mcheck_extra(cpu) = mid;
	mb();

	/* Access the bus revision word. */
	pci_rev = *(vuip)MCPCIA_REV(mid);

	mb();
	mb();  /* magic */
	if (mcheck_taken(cpu)) {
		mcheck_taken(cpu) = 0;
		pci_rev = 0xffffffff;
		mb();
	}
	mcheck_expected(cpu) = 0;
	mb();

	return (pci_rev >> 16) == PCI_CLASS_BRIDGE_HOST;
}

static void __init
mcpcia_new_hose(int h)
{
	struct pci_controller *hose;
	struct resource *io, *mem, *hae_mem;
	int mid = MCPCIA_HOSE2MID(h);

	hose = alloc_pci_controller();
	if (h == 0)
		pci_isa_hose = hose;
	io = alloc_resource();
	mem = alloc_resource();
	hae_mem = alloc_resource();
			
	hose->io_space = io;
	hose->mem_space = hae_mem;
	hose->sparse_mem_base = MCPCIA_SPARSE(mid) - IDENT_ADDR;
	hose->dense_mem_base = MCPCIA_DENSE(mid) - IDENT_ADDR;
	hose->sparse_io_base = MCPCIA_IO(mid) - IDENT_ADDR;
	hose->dense_io_base = 0;
	hose->config_space_base = MCPCIA_CONF(mid);
	hose->index = h;

	io->start = MCPCIA_IO(mid) - MCPCIA_IO_BIAS;
	io->end = io->start + 0xffff;
	io->name = pci_io_names[h];
	io->flags = IORESOURCE_IO;

	mem->start = MCPCIA_DENSE(mid) - MCPCIA_MEM_BIAS;
	mem->end = mem->start + 0xffffffff;
	mem->name = pci_mem_names[h];
	mem->flags = IORESOURCE_MEM;

	hae_mem->start = mem->start;
	hae_mem->end = mem->start + MCPCIA_MEM_MASK;
	hae_mem->name = pci_hae0_name;
	hae_mem->flags = IORESOURCE_MEM;

	if (request_resource(&ioport_resource, io) < 0)
		printk(KERN_ERR "Failed to request IO on hose %d\n", h);
	if (request_resource(&iomem_resource, mem) < 0)
		printk(KERN_ERR "Failed to request MEM on hose %d\n", h);
	if (request_resource(mem, hae_mem) < 0)
		printk(KERN_ERR "Failed to request HAE_MEM on hose %d\n", h);
}

static void
mcpcia_pci_clr_err(int mid)
{
	*(vuip)MCPCIA_CAP_ERR(mid);
	*(vuip)MCPCIA_CAP_ERR(mid) = 0xffffffff;   /* Clear them all.  */
	mb();
	*(vuip)MCPCIA_CAP_ERR(mid);  /* Re-read for force write.  */
}

static void __init
mcpcia_startup_hose(struct pci_controller *hose)
{
	int mid = MCPCIA_HOSE2MID(hose->index);
	unsigned int tmp;

	mcpcia_pci_clr_err(mid);

	/* 
	 * Set up error reporting.
	 */
	tmp = *(vuip)MCPCIA_CAP_ERR(mid);
	tmp |= 0x0006;		/* master/target abort */
	*(vuip)MCPCIA_CAP_ERR(mid) = tmp;
	mb();
	tmp = *(vuip)MCPCIA_CAP_ERR(mid);

	/*
	 * Set up the PCI->physical memory translation windows.
	 *
	 * Window 0 is scatter-gather 8MB at 8MB (for isa)
	 * Window 1 is scatter-gather (up to) 1GB at 1GB (for pci)
	 * Window 2 is direct access 2GB at 2GB
	 */
	hose->sg_isa = iommu_arena_new(hose, 0x00800000, 0x00800000,
				       SMP_CACHE_BYTES);
	hose->sg_pci = iommu_arena_new(hose, 0x40000000,
				       size_for_memory(0x40000000),
				       SMP_CACHE_BYTES);

	__direct_map_base = 0x80000000;
	__direct_map_size = 0x80000000;

	*(vuip)MCPCIA_W0_BASE(mid) = hose->sg_isa->dma_base | 3;
	*(vuip)MCPCIA_W0_MASK(mid) = (hose->sg_isa->size - 1) & 0xfff00000;
	*(vuip)MCPCIA_T0_BASE(mid) = virt_to_phys(hose->sg_isa->ptes) >> 8;

	*(vuip)MCPCIA_W1_BASE(mid) = hose->sg_pci->dma_base | 3;
	*(vuip)MCPCIA_W1_MASK(mid) = (hose->sg_pci->size - 1) & 0xfff00000;
	*(vuip)MCPCIA_T1_BASE(mid) = virt_to_phys(hose->sg_pci->ptes) >> 8;

	*(vuip)MCPCIA_W2_BASE(mid) = __direct_map_base | 1;
	*(vuip)MCPCIA_W2_MASK(mid) = (__direct_map_size - 1) & 0xfff00000;
	*(vuip)MCPCIA_T2_BASE(mid) = 0;

	*(vuip)MCPCIA_W3_BASE(mid) = 0x0;

	mcpcia_pci_tbi(hose, 0, -1);

	*(vuip)MCPCIA_HBASE(mid) = 0x0;
	mb();

	*(vuip)MCPCIA_HAE_MEM(mid) = 0U;
	mb();
	*(vuip)MCPCIA_HAE_MEM(mid); /* read it back. */
	*(vuip)MCPCIA_HAE_IO(mid) = 0;
	mb();
	*(vuip)MCPCIA_HAE_IO(mid);  /* read it back. */
}

void __init
mcpcia_init_arch(void)
{
	/* With multiple PCI busses, we play with I/O as physical addrs.  */
	ioport_resource.end = ~0UL;

	/* Allocate hose 0.  That's the one that all the ISA junk hangs
	   off of, from which we'll be registering stuff here in a bit.
	   Other hose detection is done in mcpcia_init_hoses, which is
	   called from init_IRQ.  */

	mcpcia_new_hose(0);
}

/* This is called from init_IRQ, since we cannot take interrupts
   before then.  Which means we cannot do this in init_arch.  */

void __init
mcpcia_init_hoses(void)
{
	struct pci_controller *hose;
	int hose_count;
	int h;

	/* First, find how many hoses we have.  */
	hose_count = 0;
	for (h = 0; h < MCPCIA_MAX_HOSES; ++h) {
		if (mcpcia_probe_hose(h)) {
			if (h != 0)
				mcpcia_new_hose(h);
			hose_count++;
		}
	}

	printk("mcpcia_init_hoses: found %d hoses\n", hose_count);

	/* Now do init for each hose.  */
	for (hose = hose_head; hose; hose = hose->next)
		mcpcia_startup_hose(hose);
}

static void
mcpcia_print_uncorrectable(struct el_MCPCIA_uncorrected_frame_mcheck *logout)
{
	struct el_common_EV5_uncorrectable_mcheck *frame;
	int i;

	frame = &logout->procdata;

	/* Print PAL fields */
	for (i = 0; i < 24; i += 2) {
		printk("  paltmp[%d-%d] = %16lx %16lx\n",
		       i, i+1, frame->paltemp[i], frame->paltemp[i+1]);
	}
	for (i = 0; i < 8; i += 2) {
		printk("  shadow[%d-%d] = %16lx %16lx\n",
		       i, i+1, frame->shadow[i], 
		       frame->shadow[i+1]);
	}
	printk("  Addr of excepting instruction  = %16lx\n",
	       frame->exc_addr);
	printk("  Summary of arithmetic traps    = %16lx\n",
	       frame->exc_sum);
	printk("  Exception mask                 = %16lx\n",
	       frame->exc_mask);
	printk("  Base address for PALcode       = %16lx\n",
	       frame->pal_base);
	printk("  Interrupt Status Reg           = %16lx\n",
	       frame->isr);
	printk("  CURRENT SETUP OF EV5 IBOX      = %16lx\n",
	       frame->icsr);
	printk("  I-CACHE Reg %s parity error   = %16lx\n",
	       (frame->ic_perr_stat & 0x800L) ? 
	       "Data" : "Tag", 
	       frame->ic_perr_stat); 
	printk("  D-CACHE error Reg              = %16lx\n",
	       frame->dc_perr_stat);
	if (frame->dc_perr_stat & 0x2) {
		switch (frame->dc_perr_stat & 0x03c) {
		case 8:
			printk("    Data error in bank 1\n");
			break;
		case 4:
			printk("    Data error in bank 0\n");
			break;
		case 20:
			printk("    Tag error in bank 1\n");
			break;
		case 10:
			printk("    Tag error in bank 0\n");
			break;
		}
	}
	printk("  Effective VA                   = %16lx\n",
	       frame->va);
	printk("  Reason for D-stream            = %16lx\n",
	       frame->mm_stat);
	printk("  EV5 SCache address             = %16lx\n",
	       frame->sc_addr);
	printk("  EV5 SCache TAG/Data parity     = %16lx\n",
	       frame->sc_stat);
	printk("  EV5 BC_TAG_ADDR                = %16lx\n",
	       frame->bc_tag_addr);
	printk("  EV5 EI_ADDR: Phys addr of Xfer = %16lx\n",
	       frame->ei_addr);
	printk("  Fill Syndrome                  = %16lx\n",
	       frame->fill_syndrome);
	printk("  EI_STAT reg                    = %16lx\n",
	       frame->ei_stat);
	printk("  LD_LOCK                        = %16lx\n",
	       frame->ld_lock);
}

static void
mcpcia_print_system_area(unsigned long la_ptr)
{
	struct el_common *frame;
	struct pci_controller *hose;

	struct IOD_subpacket {
	  unsigned long base;
	  unsigned int whoami;
	  unsigned int rsvd1;
	  unsigned int pci_rev;
	  unsigned int cap_ctrl;
	  unsigned int hae_mem;
	  unsigned int hae_io;
	  unsigned int int_ctl;
	  unsigned int int_reg;
	  unsigned int int_mask0;
	  unsigned int int_mask1;
	  unsigned int mc_err0;
	  unsigned int mc_err1;
	  unsigned int cap_err;
	  unsigned int rsvd2;
	  unsigned int pci_err1;
	  unsigned int mdpa_stat;
	  unsigned int mdpa_syn;
	  unsigned int mdpb_stat;
	  unsigned int mdpb_syn;
	  unsigned int rsvd3;
	  unsigned int rsvd4;
	  unsigned int rsvd5;
	} *iodpp;

	frame = (struct el_common *)la_ptr;
	iodpp = (struct IOD_subpacket *) (la_ptr + frame->sys_offset);

	for (hose = hose_head; hose; hose = hose->next, iodpp++) {

	  printk("IOD %d Register Subpacket - Bridge Base Address %16lx\n",
		 hose->index, iodpp->base);
	  printk("  WHOAMI      = %8x\n", iodpp->whoami);
	  printk("  PCI_REV     = %8x\n", iodpp->pci_rev);
	  printk("  CAP_CTRL    = %8x\n", iodpp->cap_ctrl);
	  printk("  HAE_MEM     = %8x\n", iodpp->hae_mem);
	  printk("  HAE_IO      = %8x\n", iodpp->hae_io);
	  printk("  INT_CTL     = %8x\n", iodpp->int_ctl);
	  printk("  INT_REG     = %8x\n", iodpp->int_reg);
	  printk("  INT_MASK0   = %8x\n", iodpp->int_mask0);
	  printk("  INT_MASK1   = %8x\n", iodpp->int_mask1);
	  printk("  MC_ERR0     = %8x\n", iodpp->mc_err0);
	  printk("  MC_ERR1     = %8x\n", iodpp->mc_err1);
	  printk("  CAP_ERR     = %8x\n", iodpp->cap_err);
	  printk("  PCI_ERR1    = %8x\n", iodpp->pci_err1);
	  printk("  MDPA_STAT   = %8x\n", iodpp->mdpa_stat);
	  printk("  MDPA_SYN    = %8x\n", iodpp->mdpa_syn);
	  printk("  MDPB_STAT   = %8x\n", iodpp->mdpb_stat);
	  printk("  MDPB_SYN    = %8x\n", iodpp->mdpb_syn);
	}
}

void
mcpcia_machine_check(unsigned long vector, unsigned long la_ptr)
{
	struct el_MCPCIA_uncorrected_frame_mcheck *mchk_logout;
	unsigned int cpu = smp_processor_id();
	int expected;

	mchk_logout = (struct el_MCPCIA_uncorrected_frame_mcheck *)la_ptr;
	expected = mcheck_expected(cpu);

	mb();
	mb();  /* magic */
	draina();

	switch (expected) {
	case 0:
	    {
		/* FIXME: how do we figure out which hose the
		   error was on?  */	
		struct pci_controller *hose;
		for (hose = hose_head; hose; hose = hose->next)
			mcpcia_pci_clr_err(MCPCIA_HOSE2MID(hose->index));
		break;
	    }
	case 1:
		mcpcia_pci_clr_err(mcheck_extra(cpu));
		break;
	default:
		/* Otherwise, we're being called from mcpcia_probe_hose
		   and there's no hose clear an error from.  */
		break;
	}

	wrmces(0x7);
	mb();

	process_mcheck_info(vector, la_ptr, "MCPCIA", expected != 0);
	if (!expected && vector != 0x620 && vector != 0x630) {
		mcpcia_print_uncorrectable(mchk_logout);
		mcpcia_print_system_area(la_ptr);
	}
}
