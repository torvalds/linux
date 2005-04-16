/*
 *	linux/arch/alpha/kernel/sys_eb64p.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999 Richard Henderson
 *
 * Code supporting the EB64+ and EB66.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_apecs.h>
#include <asm/core_lca.h>
#include <asm/hwrpb.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"


/* Note mask bit is true for DISABLED irqs.  */
static unsigned int cached_irq_mask = -1;

static inline void
eb64p_update_irq_hw(unsigned int irq, unsigned long mask)
{
	outb(mask >> (irq >= 24 ? 24 : 16), (irq >= 24 ? 0x27 : 0x26));
}

static inline void
eb64p_enable_irq(unsigned int irq)
{
	eb64p_update_irq_hw(irq, cached_irq_mask &= ~(1 << irq));
}

static void
eb64p_disable_irq(unsigned int irq)
{
	eb64p_update_irq_hw(irq, cached_irq_mask |= 1 << irq);
}

static unsigned int
eb64p_startup_irq(unsigned int irq)
{
	eb64p_enable_irq(irq);
	return 0; /* never anything pending */
}

static void
eb64p_end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		eb64p_enable_irq(irq);
}

static struct hw_interrupt_type eb64p_irq_type = {
	.typename	= "EB64P",
	.startup	= eb64p_startup_irq,
	.shutdown	= eb64p_disable_irq,
	.enable		= eb64p_enable_irq,
	.disable	= eb64p_disable_irq,
	.ack		= eb64p_disable_irq,
	.end		= eb64p_end_irq,
};

static void 
eb64p_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary registers */
	pld = inb(0x26) | (inb(0x27) << 8);

	/*
	 * Now, for every possible bit set, work through
	 * them and call the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1;	/* clear least bit set */

		if (i == 5) {
			isa_device_interrupt(vector, regs);
		} else {
			handle_irq(16 + i, regs);
		}
	}
}

static void __init
eb64p_init_irq(void)
{
	long i;

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_CABRIOLET)
	/*
	 * CABRIO SRM may not set variation correctly, so here we test
	 * the high word of the interrupt summary register for the RAZ
	 * bits, and hope that a true EB64+ would read all ones...
	 */
	if (inw(0x806) != 0xffff) {
		extern struct alpha_machine_vector cabriolet_mv;

		printk("Detected Cabriolet: correcting HWRPB.\n");

		hwrpb->sys_variation |= 2L << 10;
		hwrpb_update_checksum(hwrpb);

		alpha_mv = cabriolet_mv;
		alpha_mv.init_irq();
		return;
	}
#endif /* GENERIC */

	outb(0xff, 0x26);
	outb(0xff, 0x27);

	init_i8259a_irqs();

	for (i = 16; i < 32; ++i) {
		irq_desc[i].status = IRQ_DISABLED | IRQ_LEVEL;
		irq_desc[i].handler = &eb64p_irq_type;
	}		

	common_init_isa_dma();
	setup_irq(16+5, &isa_cascade_irqaction);
}

/*
 * PCI Fixup configuration.
 *
 * There are two 8 bit external summary registers as follows:
 *
 * Summary @ 0x26:
 * Bit      Meaning
 * 0        Interrupt Line A from slot 0
 * 1        Interrupt Line A from slot 1
 * 2        Interrupt Line B from slot 0
 * 3        Interrupt Line B from slot 1
 * 4        Interrupt Line C from slot 0
 * 5        Interrupt line from the two ISA PICs
 * 6        Tulip
 * 7        NCR SCSI
 *
 * Summary @ 0x27
 * Bit      Meaning
 * 0        Interrupt Line C from slot 1
 * 1        Interrupt Line D from slot 0
 * 2        Interrupt Line D from slot 1
 * 3        RAZ
 * 4        RAZ
 * 5        RAZ
 * 6        RAZ
 * 7        RAZ
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  5       NCR SCSI controller
 *  6       PCI on board slot 0
 *  7       PCI on board slot 1
 *  8       Intel SIO PCI-ISA bridge chip
 *  9       Tulip - DECchip 21040 Ethernet controller
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */

static int __init
eb64p_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static char irq_tab[5][5] __initdata = {
		/*INT  INTA  INTB  INTC   INTD */
		{16+7, 16+7, 16+7, 16+7,  16+7},  /* IdSel 5,  slot ?, ?? */
		{16+0, 16+0, 16+2, 16+4,  16+9},  /* IdSel 6,  slot ?, ?? */
		{16+1, 16+1, 16+3, 16+8, 16+10},  /* IdSel 7,  slot ?, ?? */
		{  -1,   -1,   -1,   -1,    -1},  /* IdSel 8,  SIO */
		{16+6, 16+6, 16+6, 16+6,  16+6},  /* IdSel 9,  TULIP */
	};
	const long min_idsel = 5, max_idsel = 9, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}


/*
 * The System Vector
 */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_EB64P)
struct alpha_machine_vector eb64p_mv __initmv = {
	.vector_name		= "EB64+",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_APECS_IO,
	.machine_check		= apecs_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= APECS_AND_LCA_DEFAULT_MEM_BASE,

	.nr_irqs		= 32,
	.device_interrupt	= eb64p_device_interrupt,

	.init_arch		= apecs_init_arch,
	.init_irq		= eb64p_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= common_init_pci,
	.kill_arch		= NULL,
	.pci_map_irq		= eb64p_map_irq,
	.pci_swizzle		= common_swizzle,
};
ALIAS_MV(eb64p)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_EB66)
struct alpha_machine_vector eb66_mv __initmv = {
	.vector_name		= "EB66",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_LCA_IO,
	.machine_check		= lca_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= APECS_AND_LCA_DEFAULT_MEM_BASE,

	.nr_irqs		= 32,
	.device_interrupt	= eb64p_device_interrupt,

	.init_arch		= lca_init_arch,
	.init_irq		= eb64p_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= common_init_pci,
	.pci_map_irq		= eb64p_map_irq,
	.pci_swizzle		= common_swizzle,
};
ALIAS_MV(eb66)
#endif
