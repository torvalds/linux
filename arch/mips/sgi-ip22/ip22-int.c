/*
 * ip22-int.c: Routines for generic manipulation of the INT[23] ASIC
 *	       found on INDY and Indigo2 workstations.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1997, 1998 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Andrew R. Baker (andrewb@uab.edu)
 *		      - Indigo2 changes
 *		      - Interrupt handling fixes
 * Copyright (C) 2001, 2003 Ladislav Michl (ladis@linux-mips.org)
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/ftrace.h>

#include <asm/irq_cpu.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>

/* So far nothing hangs here */
#undef USE_LIO3_IRQ

struct sgint_regs *sgint;

static char lc0msk_to_irqnr[256];
static char lc1msk_to_irqnr[256];
static char lc2msk_to_irqnr[256];
static char lc3msk_to_irqnr[256];

extern int ip22_eisa_init(void);

static void enable_local0_irq(struct irq_data *d)
{
	/* don't allow mappable interrupt to be enabled from setup_irq,
	 * we have our own way to do so */
	if (d->irq != SGI_MAP_0_IRQ)
		sgint->imask0 |= (1 << (d->irq - SGINT_LOCAL0));
}

static void disable_local0_irq(struct irq_data *d)
{
	sgint->imask0 &= ~(1 << (d->irq - SGINT_LOCAL0));
}

static struct irq_chip ip22_local0_irq_type = {
	.name		= "IP22 local 0",
	.irq_mask	= disable_local0_irq,
	.irq_unmask	= enable_local0_irq,
};

static void enable_local1_irq(struct irq_data *d)
{
	/* don't allow mappable interrupt to be enabled from setup_irq,
	 * we have our own way to do so */
	if (d->irq != SGI_MAP_1_IRQ)
		sgint->imask1 |= (1 << (d->irq - SGINT_LOCAL1));
}

static void disable_local1_irq(struct irq_data *d)
{
	sgint->imask1 &= ~(1 << (d->irq - SGINT_LOCAL1));
}

static struct irq_chip ip22_local1_irq_type = {
	.name		= "IP22 local 1",
	.irq_mask	= disable_local1_irq,
	.irq_unmask	= enable_local1_irq,
};

static void enable_local2_irq(struct irq_data *d)
{
	sgint->imask0 |= (1 << (SGI_MAP_0_IRQ - SGINT_LOCAL0));
	sgint->cmeimask0 |= (1 << (d->irq - SGINT_LOCAL2));
}

static void disable_local2_irq(struct irq_data *d)
{
	sgint->cmeimask0 &= ~(1 << (d->irq - SGINT_LOCAL2));
	if (!sgint->cmeimask0)
		sgint->imask0 &= ~(1 << (SGI_MAP_0_IRQ - SGINT_LOCAL0));
}

static struct irq_chip ip22_local2_irq_type = {
	.name		= "IP22 local 2",
	.irq_mask	= disable_local2_irq,
	.irq_unmask	= enable_local2_irq,
};

static void enable_local3_irq(struct irq_data *d)
{
	sgint->imask1 |= (1 << (SGI_MAP_1_IRQ - SGINT_LOCAL1));
	sgint->cmeimask1 |= (1 << (d->irq - SGINT_LOCAL3));
}

static void disable_local3_irq(struct irq_data *d)
{
	sgint->cmeimask1 &= ~(1 << (d->irq - SGINT_LOCAL3));
	if (!sgint->cmeimask1)
		sgint->imask1 &= ~(1 << (SGI_MAP_1_IRQ - SGINT_LOCAL1));
}

static struct irq_chip ip22_local3_irq_type = {
	.name		= "IP22 local 3",
	.irq_mask	= disable_local3_irq,
	.irq_unmask	= enable_local3_irq,
};

static void indy_local0_irqdispatch(void)
{
	u8 mask = sgint->istat0 & sgint->imask0;
	u8 mask2;
	int irq;

	if (mask & SGINT_ISTAT0_LIO2) {
		mask2 = sgint->vmeistat & sgint->cmeimask0;
		irq = lc2msk_to_irqnr[mask2];
	} else
		irq = lc0msk_to_irqnr[mask];

	/* if irq == 0, then the interrupt has already been cleared */
	if (irq)
		do_IRQ(irq);
}

static void indy_local1_irqdispatch(void)
{
	u8 mask = sgint->istat1 & sgint->imask1;
	u8 mask2;
	int irq;

	if (mask & SGINT_ISTAT1_LIO3) {
		mask2 = sgint->vmeistat & sgint->cmeimask1;
		irq = lc3msk_to_irqnr[mask2];
	} else
		irq = lc1msk_to_irqnr[mask];

	/* if irq == 0, then the interrupt has already been cleared */
	if (irq)
		do_IRQ(irq);
}

extern void ip22_be_interrupt(int irq);

static void __irq_entry indy_buserror_irq(void)
{
	int irq = SGI_BUSERR_IRQ;

	irq_enter();
	kstat_incr_irqs_this_cpu(irq, irq_to_desc(irq));
	ip22_be_interrupt(irq);
	irq_exit();
}

static struct irqaction local0_cascade = {
	.handler	= no_action,
	.flags		= IRQF_NO_THREAD,
	.name		= "local0 cascade",
};

static struct irqaction local1_cascade = {
	.handler	= no_action,
	.flags		= IRQF_NO_THREAD,
	.name		= "local1 cascade",
};

static struct irqaction buserr = {
	.handler	= no_action,
	.flags		= IRQF_NO_THREAD,
	.name		= "Bus Error",
};

static struct irqaction map0_cascade = {
	.handler	= no_action,
	.flags		= IRQF_NO_THREAD,
	.name		= "mapable0 cascade",
};

#ifdef USE_LIO3_IRQ
static struct irqaction map1_cascade = {
	.handler	= no_action,
	.flags		= IRQF_NO_THREAD,
	.name		= "mapable1 cascade",
};
#define SGI_INTERRUPTS	SGINT_END
#else
#define SGI_INTERRUPTS	SGINT_LOCAL3
#endif

extern void indy_8254timer_irq(void);

/*
 * IRQs on the INDY look basically (barring software IRQs which we don't use
 * at all) like:
 *
 *	MIPS IRQ	Source
 *	--------	------
 *	       0	Software (ignored)
 *	       1	Software (ignored)
 *	       2	Local IRQ level zero
 *	       3	Local IRQ level one
 *	       4	8254 Timer zero
 *	       5	8254 Timer one
 *	       6	Bus Error
 *	       7	R4k timer (what we use)
 *
 * We handle the IRQ according to _our_ priority which is:
 *
 * Highest ----	    R4k Timer
 *		    Local IRQ zero
 *		    Local IRQ one
 *		    Bus Error
 *		    8254 Timer zero
 * Lowest  ----	    8254 Timer one
 *
 * then we just return, if multiple IRQs are pending then we will just take
 * another exception, big deal.
 */

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause();

	/*
	 * First we check for r4k counter/timer IRQ.
	 */
	if (pending & CAUSEF_IP7)
		do_IRQ(SGI_TIMER_IRQ);
	else if (pending & CAUSEF_IP2)
		indy_local0_irqdispatch();
	else if (pending & CAUSEF_IP3)
		indy_local1_irqdispatch();
	else if (pending & CAUSEF_IP6)
		indy_buserror_irq();
	else if (pending & (CAUSEF_IP4 | CAUSEF_IP5))
		indy_8254timer_irq();
}

void __init arch_init_irq(void)
{
	int i;

	/* Init local mask --> irq tables. */
	for (i = 0; i < 256; i++) {
		if (i & 0x80) {
			lc0msk_to_irqnr[i] = SGINT_LOCAL0 + 7;
			lc1msk_to_irqnr[i] = SGINT_LOCAL1 + 7;
			lc2msk_to_irqnr[i] = SGINT_LOCAL2 + 7;
			lc3msk_to_irqnr[i] = SGINT_LOCAL3 + 7;
		} else if (i & 0x40) {
			lc0msk_to_irqnr[i] = SGINT_LOCAL0 + 6;
			lc1msk_to_irqnr[i] = SGINT_LOCAL1 + 6;
			lc2msk_to_irqnr[i] = SGINT_LOCAL2 + 6;
			lc3msk_to_irqnr[i] = SGINT_LOCAL3 + 6;
		} else if (i & 0x20) {
			lc0msk_to_irqnr[i] = SGINT_LOCAL0 + 5;
			lc1msk_to_irqnr[i] = SGINT_LOCAL1 + 5;
			lc2msk_to_irqnr[i] = SGINT_LOCAL2 + 5;
			lc3msk_to_irqnr[i] = SGINT_LOCAL3 + 5;
		} else if (i & 0x10) {
			lc0msk_to_irqnr[i] = SGINT_LOCAL0 + 4;
			lc1msk_to_irqnr[i] = SGINT_LOCAL1 + 4;
			lc2msk_to_irqnr[i] = SGINT_LOCAL2 + 4;
			lc3msk_to_irqnr[i] = SGINT_LOCAL3 + 4;
		} else if (i & 0x08) {
			lc0msk_to_irqnr[i] = SGINT_LOCAL0 + 3;
			lc1msk_to_irqnr[i] = SGINT_LOCAL1 + 3;
			lc2msk_to_irqnr[i] = SGINT_LOCAL2 + 3;
			lc3msk_to_irqnr[i] = SGINT_LOCAL3 + 3;
		} else if (i & 0x04) {
			lc0msk_to_irqnr[i] = SGINT_LOCAL0 + 2;
			lc1msk_to_irqnr[i] = SGINT_LOCAL1 + 2;
			lc2msk_to_irqnr[i] = SGINT_LOCAL2 + 2;
			lc3msk_to_irqnr[i] = SGINT_LOCAL3 + 2;
		} else if (i & 0x02) {
			lc0msk_to_irqnr[i] = SGINT_LOCAL0 + 1;
			lc1msk_to_irqnr[i] = SGINT_LOCAL1 + 1;
			lc2msk_to_irqnr[i] = SGINT_LOCAL2 + 1;
			lc3msk_to_irqnr[i] = SGINT_LOCAL3 + 1;
		} else if (i & 0x01) {
			lc0msk_to_irqnr[i] = SGINT_LOCAL0 + 0;
			lc1msk_to_irqnr[i] = SGINT_LOCAL1 + 0;
			lc2msk_to_irqnr[i] = SGINT_LOCAL2 + 0;
			lc3msk_to_irqnr[i] = SGINT_LOCAL3 + 0;
		} else {
			lc0msk_to_irqnr[i] = 0;
			lc1msk_to_irqnr[i] = 0;
			lc2msk_to_irqnr[i] = 0;
			lc3msk_to_irqnr[i] = 0;
		}
	}

	/* Mask out all interrupts. */
	sgint->imask0 = 0;
	sgint->imask1 = 0;
	sgint->cmeimask0 = 0;
	sgint->cmeimask1 = 0;

	/* init CPU irqs */
	mips_cpu_irq_init();

	for (i = SGINT_LOCAL0; i < SGI_INTERRUPTS; i++) {
		struct irq_chip *handler;

		if (i < SGINT_LOCAL1)
			handler		= &ip22_local0_irq_type;
		else if (i < SGINT_LOCAL2)
			handler		= &ip22_local1_irq_type;
		else if (i < SGINT_LOCAL3)
			handler		= &ip22_local2_irq_type;
		else
			handler		= &ip22_local3_irq_type;

		irq_set_chip_and_handler(i, handler, handle_level_irq);
	}

	/* vector handler. this register the IRQ as non-sharable */
	setup_irq(SGI_LOCAL_0_IRQ, &local0_cascade);
	setup_irq(SGI_LOCAL_1_IRQ, &local1_cascade);
	setup_irq(SGI_BUSERR_IRQ, &buserr);

	/* cascade in cascade. i love Indy ;-) */
	setup_irq(SGI_MAP_0_IRQ, &map0_cascade);
#ifdef USE_LIO3_IRQ
	setup_irq(SGI_MAP_1_IRQ, &map1_cascade);
#endif

#ifdef CONFIG_EISA
	if (ip22_is_fullhouse())	/* Only Indigo-2 has EISA stuff */
		ip22_eisa_init();
#endif
}
