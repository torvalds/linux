/*
 *	linux/arch/alpha/kernel/sys_sable.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999 Richard Henderson
 *
 * Code supporting the Sable, Sable-Gamma, and Lynx systems.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_t2.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"

DEFINE_SPINLOCK(sable_lynx_irq_lock);

typedef struct irq_swizzle_struct
{
	char irq_to_mask[64];
	char mask_to_irq[64];

	/* Note mask bit is true for DISABLED irqs.  */
	unsigned long shadow_mask;

	void (*update_irq_hw)(unsigned long bit, unsigned long mask);
	void (*ack_irq_hw)(unsigned long bit);

} irq_swizzle_t;

static irq_swizzle_t *sable_lynx_irq_swizzle;

static void sable_lynx_init_irq(int nr_irqs);

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SABLE)

/***********************************************************************/
/*
 *   For SABLE, which is really baroque, we manage 40 IRQ's, but the
 *   hardware really only supports 24, not via normal ISA PIC,
 *   but cascaded custom 8259's, etc.
 *	 0-7  (char at 536)
 *	 8-15 (char at 53a)
 *	16-23 (char at 53c)
 *
 * Summary Registers (536/53a/53c):
 *
 * Bit      Meaning               Kernel IRQ
 *------------------------------------------
 * 0        PCI slot 0			34
 * 1        NCR810 (builtin)		33
 * 2        TULIP (builtin)		32
 * 3        mouse			12
 * 4        PCI slot 1			35
 * 5        PCI slot 2			36
 * 6        keyboard			1
 * 7        floppy			6
 * 8        COM2			3
 * 9        parallel port		7
 *10        EISA irq 3			-
 *11        EISA irq 4			-
 *12        EISA irq 5			5
 *13        EISA irq 6			-
 *14        EISA irq 7			-
 *15        COM1			4
 *16        EISA irq 9			9
 *17        EISA irq 10			10
 *18        EISA irq 11			11
 *19        EISA irq 12			-
 *20        EISA irq 13			-
 *21        EISA irq 14			14
 *22        NC				15
 *23        IIC				-
 */

static void
sable_update_irq_hw(unsigned long bit, unsigned long mask)
{
	int port = 0x537;

	if (bit >= 16) {
		port = 0x53d;
		mask >>= 16;
	} else if (bit >= 8) {
		port = 0x53b;
		mask >>= 8;
	}

	outb(mask, port);
}

static void
sable_ack_irq_hw(unsigned long bit)
{
	int port, val1, val2;

	if (bit >= 16) {
		port = 0x53c;
		val1 = 0xE0 | (bit - 16);
		val2 = 0xE0 | 4;
	} else if (bit >= 8) {
		port = 0x53a;
		val1 = 0xE0 | (bit - 8);
		val2 = 0xE0 | 3;
	} else {
		port = 0x536;
		val1 = 0xE0 | (bit - 0);
		val2 = 0xE0 | 1;
	}

	outb(val1, port);	/* ack the slave */
	outb(val2, 0x534);	/* ack the master */
}

static irq_swizzle_t sable_irq_swizzle = {
	{
		-1,  6, -1,  8, 15, 12,  7,  9,	/* pseudo PIC  0-7  */
		-1, 16, 17, 18,  3, -1, 21, 22,	/* pseudo PIC  8-15 */
		-1, -1, -1, -1, -1, -1, -1, -1,	/* pseudo EISA 0-7  */
		-1, -1, -1, -1, -1, -1, -1, -1,	/* pseudo EISA 8-15  */
		 2,  1,  0,  4,  5, -1, -1, -1,	/* pseudo PCI */
		-1, -1, -1, -1, -1, -1, -1, -1,	/*  */
		-1, -1, -1, -1, -1, -1, -1, -1,	/*  */
		-1, -1, -1, -1, -1, -1, -1, -1 	/*  */
	},
	{
		34, 33, 32, 12, 35, 36,  1,  6,	/* mask 0-7  */
		 3,  7, -1, -1,  5, -1, -1,  4,	/* mask 8-15  */
		 9, 10, 11, -1, -1, 14, 15, -1,	/* mask 16-23  */
		-1, -1, -1, -1, -1, -1, -1, -1,	/*  */
		-1, -1, -1, -1, -1, -1, -1, -1,	/*  */
		-1, -1, -1, -1, -1, -1, -1, -1,	/*  */
		-1, -1, -1, -1, -1, -1, -1, -1,	/*  */
		-1, -1, -1, -1, -1, -1, -1, -1	/*  */
	},
	-1,
	sable_update_irq_hw,
	sable_ack_irq_hw
};

static void __init
sable_init_irq(void)
{
	outb(-1, 0x537);	/* slave 0 */
	outb(-1, 0x53b);	/* slave 1 */
	outb(-1, 0x53d);	/* slave 2 */
	outb(0x44, 0x535);	/* enable cascades in master */

	sable_lynx_irq_swizzle = &sable_irq_swizzle;
	sable_lynx_init_irq(40);
}

/*
 * PCI Fixup configuration for ALPHA SABLE (2100).
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  0       TULIP
 *  1       SCSI
 *  2       PCI-EISA bridge
 *  3       none
 *  4       none
 *  5       none
 *  6       PCI on board slot 0
 *  7       PCI on board slot 1
 *  8       PCI on board slot 2
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */
/*
 * NOTE: the IRQ assignments below are arbitrary, but need to be consistent
 * with the values in the irq swizzling tables above.
 */

static int __init
sable_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static char irq_tab[9][5] __initdata = {
		/*INT    INTA   INTB   INTC   INTD */
		{ 32+0,  32+0,  32+0,  32+0,  32+0},  /* IdSel 0,  TULIP  */
		{ 32+1,  32+1,  32+1,  32+1,  32+1},  /* IdSel 1,  SCSI   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 2,  SIO   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 3,  none   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 4,  none   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 5,  none   */
		{ 32+2,  32+2,  32+2,  32+2,  32+2},  /* IdSel 6,  slot 0 */
		{ 32+3,  32+3,  32+3,  32+3,  32+3},  /* IdSel 7,  slot 1 */
		{ 32+4,  32+4,  32+4,  32+4,  32+4}   /* IdSel 8,  slot 2 */
	};
	long min_idsel = 0, max_idsel = 8, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}
#endif /* defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SABLE) */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_LYNX)

/***********************************************************************/
/* LYNX hardware specifics
 */
/*
 *   For LYNX, which is also baroque, we manage 64 IRQs, via a custom IC.
 *
 * Bit      Meaning               Kernel IRQ
 *------------------------------------------
 * 0        
 * 1        
 * 2        
 * 3        mouse			12
 * 4        
 * 5        
 * 6        keyboard			1
 * 7        floppy			6
 * 8        COM2			3
 * 9        parallel port		7
 *10        EISA irq 3			-
 *11        EISA irq 4			-
 *12        EISA irq 5			5
 *13        EISA irq 6			-
 *14        EISA irq 7			-
 *15        COM1			4
 *16        EISA irq 9			9
 *17        EISA irq 10			10
 *18        EISA irq 11			11
 *19        EISA irq 12			-
 *20        
 *21        EISA irq 14			14
 *22        EISA irq 15			15
 *23        IIC				-
 *24        VGA (builtin)               -
 *25
 *26
 *27
 *28        NCR810 (builtin)		28
 *29
 *30
 *31
 *32        PCI 0 slot 4 A primary bus  32
 *33        PCI 0 slot 4 B primary bus  33
 *34        PCI 0 slot 4 C primary bus  34
 *35        PCI 0 slot 4 D primary bus
 *36        PCI 0 slot 5 A primary bus
 *37        PCI 0 slot 5 B primary bus
 *38        PCI 0 slot 5 C primary bus
 *39        PCI 0 slot 5 D primary bus
 *40        PCI 0 slot 6 A primary bus
 *41        PCI 0 slot 6 B primary bus
 *42        PCI 0 slot 6 C primary bus
 *43        PCI 0 slot 6 D primary bus
 *44        PCI 0 slot 7 A primary bus
 *45        PCI 0 slot 7 B primary bus
 *46        PCI 0 slot 7 C primary bus
 *47        PCI 0 slot 7 D primary bus
 *48        PCI 0 slot 0 A secondary bus
 *49        PCI 0 slot 0 B secondary bus
 *50        PCI 0 slot 0 C secondary bus
 *51        PCI 0 slot 0 D secondary bus
 *52        PCI 0 slot 1 A secondary bus
 *53        PCI 0 slot 1 B secondary bus
 *54        PCI 0 slot 1 C secondary bus
 *55        PCI 0 slot 1 D secondary bus
 *56        PCI 0 slot 2 A secondary bus
 *57        PCI 0 slot 2 B secondary bus
 *58        PCI 0 slot 2 C secondary bus
 *59        PCI 0 slot 2 D secondary bus
 *60        PCI 0 slot 3 A secondary bus
 *61        PCI 0 slot 3 B secondary bus
 *62        PCI 0 slot 3 C secondary bus
 *63        PCI 0 slot 3 D secondary bus
 */

static void
lynx_update_irq_hw(unsigned long bit, unsigned long mask)
{
	/*
	 * Write the AIR register on the T3/T4 with the
	 * address of the IC mask register (offset 0x40)
	 */
	*(vulp)T2_AIR = 0x40;
	mb();
	*(vulp)T2_AIR; /* re-read to force write */
	mb();
	*(vulp)T2_DIR = mask;    
	mb();
	mb();
}

static void
lynx_ack_irq_hw(unsigned long bit)
{
	*(vulp)T2_VAR = (u_long) bit;
	mb();
	mb();
}

static irq_swizzle_t lynx_irq_swizzle = {
	{ /* irq_to_mask */
		-1,  6, -1,  8, 15, 12,  7,  9,	/* pseudo PIC  0-7  */
		-1, 16, 17, 18,  3, -1, 21, 22,	/* pseudo PIC  8-15 */
		-1, -1, -1, -1, -1, -1, -1, -1,	/* pseudo */
		-1, -1, -1, -1, 28, -1, -1, -1,	/* pseudo */
		32, 33, 34, 35, 36, 37, 38, 39,	/* mask 32-39 */
		40, 41, 42, 43, 44, 45, 46, 47,	/* mask 40-47 */
		48, 49, 50, 51, 52, 53, 54, 55,	/* mask 48-55 */
		56, 57, 58, 59, 60, 61, 62, 63	/* mask 56-63 */
	},
	{ /* mask_to_irq */
		-1, -1, -1, 12, -1, -1,  1,  6,	/* mask 0-7   */
		 3,  7, -1, -1,  5, -1, -1,  4,	/* mask 8-15  */
		 9, 10, 11, -1, -1, 14, 15, -1,	/* mask 16-23 */
		-1, -1, -1, -1, 28, -1, -1, -1,	/* mask 24-31 */
		32, 33, 34, 35, 36, 37, 38, 39,	/* mask 32-39 */
		40, 41, 42, 43, 44, 45, 46, 47,	/* mask 40-47 */
		48, 49, 50, 51, 52, 53, 54, 55,	/* mask 48-55 */
		56, 57, 58, 59, 60, 61, 62, 63	/* mask 56-63 */
	},
	-1,
	lynx_update_irq_hw,
	lynx_ack_irq_hw
};

static void __init
lynx_init_irq(void)
{
	sable_lynx_irq_swizzle = &lynx_irq_swizzle;
	sable_lynx_init_irq(64);
}

/*
 * PCI Fixup configuration for ALPHA LYNX (2100A)
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  0       none
 *  1       none
 *  2       PCI-EISA bridge
 *  3       PCI-PCI bridge
 *  4       NCR 810 (Demi-Lynx only)
 *  5       none
 *  6       PCI on board slot 4
 *  7       PCI on board slot 5
 *  8       PCI on board slot 6
 *  9       PCI on board slot 7
 *
 * And behind the PPB we have:
 *
 * 11       PCI on board slot 0
 * 12       PCI on board slot 1
 * 13       PCI on board slot 2
 * 14       PCI on board slot 3
 */
/*
 * NOTE: the IRQ assignments below are arbitrary, but need to be consistent
 * with the values in the irq swizzling tables above.
 */

static int __init
lynx_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static char irq_tab[19][5] __initdata = {
		/*INT    INTA   INTB   INTC   INTD */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 13,  PCEB   */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 14,  PPB    */
		{   28,    28,    28,    28,    28},  /* IdSel 15,  NCR demi */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 16,  none   */
		{   32,    32,    33,    34,    35},  /* IdSel 17,  slot 4 */
		{   36,    36,    37,    38,    39},  /* IdSel 18,  slot 5 */
		{   40,    40,    41,    42,    43},  /* IdSel 19,  slot 6 */
		{   44,    44,    45,    46,    47},  /* IdSel 20,  slot 7 */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 22,  none   */
		/* The following are actually behind the PPB. */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 16   none */
		{   28,    28,    28,    28,    28},  /* IdSel 17   NCR lynx */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 18   none */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 19   none */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 20   none */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 21   none */
		{   48,    48,    49,    50,    51},  /* IdSel 22   slot 0 */
		{   52,    52,    53,    54,    55},  /* IdSel 23   slot 1 */
		{   56,    56,    57,    58,    59},  /* IdSel 24   slot 2 */
		{   60,    60,    61,    62,    63}   /* IdSel 25   slot 3 */
	};
	const long min_idsel = 2, max_idsel = 20, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static u8 __init
lynx_swizzle(struct pci_dev *dev, u8 *pinp)
{
	int slot, pin = *pinp;

	if (dev->bus->number == 0) {
		slot = PCI_SLOT(dev->devfn);
	}
	/* Check for the built-in bridge */
	else if (PCI_SLOT(dev->bus->self->devfn) == 3) {
		slot = PCI_SLOT(dev->devfn) + 11;
	}
	else
	{
		/* Must be a card-based bridge.  */
		do {
			if (PCI_SLOT(dev->bus->self->devfn) == 3) {
				slot = PCI_SLOT(dev->devfn) + 11;
				break;
			}
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn)) ;

			/* Move up the chain of bridges.  */
			dev = dev->bus->self;
			/* Slot of the next bridge.  */
			slot = PCI_SLOT(dev->devfn);
		} while (dev->bus->self);
	}
	*pinp = pin;
	return slot;
}

#endif /* defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_LYNX) */

/***********************************************************************/
/* GENERIC irq routines */

static inline void
sable_lynx_enable_irq(unsigned int irq)
{
	unsigned long bit, mask;

	bit = sable_lynx_irq_swizzle->irq_to_mask[irq];
	spin_lock(&sable_lynx_irq_lock);
	mask = sable_lynx_irq_swizzle->shadow_mask &= ~(1UL << bit);
	sable_lynx_irq_swizzle->update_irq_hw(bit, mask);
	spin_unlock(&sable_lynx_irq_lock);
#if 0
	printk("%s: mask 0x%lx bit 0x%x irq 0x%x\n",
	       __func__, mask, bit, irq);
#endif
}

static void
sable_lynx_disable_irq(unsigned int irq)
{
	unsigned long bit, mask;

	bit = sable_lynx_irq_swizzle->irq_to_mask[irq];
	spin_lock(&sable_lynx_irq_lock);
	mask = sable_lynx_irq_swizzle->shadow_mask |= 1UL << bit;
	sable_lynx_irq_swizzle->update_irq_hw(bit, mask);
	spin_unlock(&sable_lynx_irq_lock);
#if 0
	printk("%s: mask 0x%lx bit 0x%x irq 0x%x\n",
	       __func__, mask, bit, irq);
#endif
}

static unsigned int
sable_lynx_startup_irq(unsigned int irq)
{
	sable_lynx_enable_irq(irq);
	return 0;
}

static void
sable_lynx_end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		sable_lynx_enable_irq(irq);
}

static void
sable_lynx_mask_and_ack_irq(unsigned int irq)
{
	unsigned long bit, mask;

	bit = sable_lynx_irq_swizzle->irq_to_mask[irq];
	spin_lock(&sable_lynx_irq_lock);
	mask = sable_lynx_irq_swizzle->shadow_mask |= 1UL << bit;
	sable_lynx_irq_swizzle->update_irq_hw(bit, mask);
	sable_lynx_irq_swizzle->ack_irq_hw(bit);
	spin_unlock(&sable_lynx_irq_lock);
}

static struct hw_interrupt_type sable_lynx_irq_type = {
	.typename	= "SABLE/LYNX",
	.startup	= sable_lynx_startup_irq,
	.shutdown	= sable_lynx_disable_irq,
	.enable		= sable_lynx_enable_irq,
	.disable	= sable_lynx_disable_irq,
	.ack		= sable_lynx_mask_and_ack_irq,
	.end		= sable_lynx_end_irq,
};

static void 
sable_lynx_srm_device_interrupt(unsigned long vector)
{
	/* Note that the vector reported by the SRM PALcode corresponds
	   to the interrupt mask bits, but we have to manage via the
	   so-called legacy IRQs for many common devices.  */

	int bit, irq;

	bit = (vector - 0x800) >> 4;
	irq = sable_lynx_irq_swizzle->mask_to_irq[bit];
#if 0
	printk("%s: vector 0x%lx bit 0x%x irq 0x%x\n",
	       __func__, vector, bit, irq);
#endif
	handle_irq(irq);
}

static void __init
sable_lynx_init_irq(int nr_irqs)
{
	long i;

	for (i = 0; i < nr_irqs; ++i) {
		irq_desc[i].status = IRQ_DISABLED | IRQ_LEVEL;
		irq_desc[i].chip = &sable_lynx_irq_type;
	}

	common_init_isa_dma();
}

static void __init
sable_lynx_init_pci(void)
{
	common_init_pci();
}

/*****************************************************************/
/*
 * The System Vectors
 *
 * In order that T2_HAE_ADDRESS should be a constant, we play
 * these games with GAMMA_BIAS.
 */

#if defined(CONFIG_ALPHA_GENERIC) || \
    (defined(CONFIG_ALPHA_SABLE) && !defined(CONFIG_ALPHA_GAMMA))
#undef GAMMA_BIAS
#define GAMMA_BIAS 0
struct alpha_machine_vector sable_mv __initmv = {
	.vector_name		= "Sable",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_T2_IO,
	.machine_check		= t2_machine_check,
	.max_isa_dma_address	= ALPHA_SABLE_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= EISA_DEFAULT_IO_BASE,
	.min_mem_address	= T2_DEFAULT_MEM_BASE,

	.nr_irqs		= 40,
	.device_interrupt	= sable_lynx_srm_device_interrupt,

	.init_arch		= t2_init_arch,
	.init_irq		= sable_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= sable_lynx_init_pci,
	.kill_arch		= t2_kill_arch,
	.pci_map_irq		= sable_map_irq,
	.pci_swizzle		= common_swizzle,

	.sys = { .t2 = {
	    .gamma_bias		= 0
	} }
};
ALIAS_MV(sable)
#endif /* GENERIC || (SABLE && !GAMMA) */

#if defined(CONFIG_ALPHA_GENERIC) || \
    (defined(CONFIG_ALPHA_SABLE) && defined(CONFIG_ALPHA_GAMMA))
#undef GAMMA_BIAS
#define GAMMA_BIAS _GAMMA_BIAS
struct alpha_machine_vector sable_gamma_mv __initmv = {
	.vector_name		= "Sable-Gamma",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_T2_IO,
	.machine_check		= t2_machine_check,
	.max_isa_dma_address	= ALPHA_SABLE_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= EISA_DEFAULT_IO_BASE,
	.min_mem_address	= T2_DEFAULT_MEM_BASE,

	.nr_irqs		= 40,
	.device_interrupt	= sable_lynx_srm_device_interrupt,

	.init_arch		= t2_init_arch,
	.init_irq		= sable_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= sable_lynx_init_pci,
	.kill_arch		= t2_kill_arch,
	.pci_map_irq		= sable_map_irq,
	.pci_swizzle		= common_swizzle,

	.sys = { .t2 = {
	    .gamma_bias		= _GAMMA_BIAS
	} }
};
ALIAS_MV(sable_gamma)
#endif /* GENERIC || (SABLE && GAMMA) */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_LYNX)
#undef GAMMA_BIAS
#define GAMMA_BIAS _GAMMA_BIAS
struct alpha_machine_vector lynx_mv __initmv = {
	.vector_name		= "Lynx",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_T2_IO,
	.machine_check		= t2_machine_check,
	.max_isa_dma_address	= ALPHA_SABLE_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= EISA_DEFAULT_IO_BASE,
	.min_mem_address	= T2_DEFAULT_MEM_BASE,

	.nr_irqs		= 64,
	.device_interrupt	= sable_lynx_srm_device_interrupt,

	.init_arch		= t2_init_arch,
	.init_irq		= lynx_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= sable_lynx_init_pci,
	.kill_arch		= t2_kill_arch,
	.pci_map_irq		= lynx_map_irq,
	.pci_swizzle		= lynx_swizzle,

	.sys = { .t2 = {
	    .gamma_bias		= _GAMMA_BIAS
	} }
};
ALIAS_MV(lynx)
#endif /* GENERIC || LYNX */
