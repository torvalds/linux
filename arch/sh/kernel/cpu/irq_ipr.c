/* $Id: irq_ipr.c,v 1.1.2.1 2002/11/17 10:53:43 mrbrown Exp $
 *
 * linux/arch/sh/kernel/irq_ipr.c
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2003 Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 *
 * Interrupt handling for IPR-based IRQ.
 *
 * Supported system:
 *	On-chip supporting modules (TMU, RTC, etc.).
 *	On-chip supporting modules for SH7709/SH7709A/SH7729/SH7300.
 *	Hitachi SolutionEngine external I/O:
 *		MS7709SE01, MS7709ASE01, and MS7750SE01
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/machvec.h>

struct ipr_data {
	unsigned int addr;	/* Address of Interrupt Priority Register */
	int shift;		/* Shifts of the 16-bit data */
	int priority;		/* The priority */
};
static struct ipr_data ipr_data[NR_IRQS];

static void enable_ipr_irq(unsigned int irq);
static void disable_ipr_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_ipr_irq disable_ipr_irq

static void mask_and_ack_ipr(unsigned int);
static void end_ipr_irq(unsigned int irq);

static unsigned int startup_ipr_irq(unsigned int irq)
{
	enable_ipr_irq(irq);
	return 0; /* never anything pending */
}

static struct hw_interrupt_type ipr_irq_type = {
	.typename = "IPR-IRQ",
	.startup = startup_ipr_irq,
	.shutdown = shutdown_ipr_irq,
	.enable = enable_ipr_irq,
	.disable = disable_ipr_irq,
	.ack = mask_and_ack_ipr,
	.end = end_ipr_irq
};

static void disable_ipr_irq(unsigned int irq)
{
	unsigned long val, flags;
	unsigned int addr = ipr_data[irq].addr;
	unsigned short mask = 0xffff ^ (0x0f << ipr_data[irq].shift);

	/* Set the priority in IPR to 0 */
	local_irq_save(flags);
	val = ctrl_inw(addr);
	val &= mask;
	ctrl_outw(val, addr);
	local_irq_restore(flags);
}

static void enable_ipr_irq(unsigned int irq)
{
	unsigned long val, flags;
	unsigned int addr = ipr_data[irq].addr;
	int priority = ipr_data[irq].priority;
	unsigned short value = (priority << ipr_data[irq].shift);

	/* Set priority in IPR back to original value */
	local_irq_save(flags);
	val = ctrl_inw(addr);
	val |= value;
	ctrl_outw(val, addr);
	local_irq_restore(flags);
}

static void mask_and_ack_ipr(unsigned int irq)
{
	disable_ipr_irq(irq);

#if defined(CONFIG_CPU_SUBTYPE_SH7707) || defined(CONFIG_CPU_SUBTYPE_SH7709) || \
    defined(CONFIG_CPU_SUBTYPE_SH7300) || defined(CONFIG_CPU_SUBTYPE_SH7705)
	/* This is needed when we use edge triggered setting */
	/* XXX: Is it really needed? */
	if (IRQ0_IRQ <= irq && irq <= IRQ5_IRQ) {
		/* Clear external interrupt request */
		int a = ctrl_inb(INTC_IRR0);
		a &= ~(1 << (irq - IRQ0_IRQ));
		ctrl_outb(a, INTC_IRR0);
	}
#endif
}

static void end_ipr_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_ipr_irq(irq);
}

void make_ipr_irq(unsigned int irq, unsigned int addr, int pos, int priority)
{
	disable_irq_nosync(irq);
	ipr_data[irq].addr = addr;
	ipr_data[irq].shift = pos*4; /* POSition (0-3) x 4 means shift */
	ipr_data[irq].priority = priority;

	irq_desc[irq].handler = &ipr_irq_type;
	disable_ipr_irq(irq);
}

#if defined(CONFIG_CPU_SUBTYPE_SH7705) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
static unsigned char pint_map[256];
static unsigned long portcr_mask = 0;

static void enable_pint_irq(unsigned int irq);
static void disable_pint_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_pint_irq disable_pint_irq

static void mask_and_ack_pint(unsigned int);
static void end_pint_irq(unsigned int irq);

static unsigned int startup_pint_irq(unsigned int irq)
{
	enable_pint_irq(irq);
	return 0; /* never anything pending */
}

static struct hw_interrupt_type pint_irq_type = {
	.typename = "PINT-IRQ",
	.startup = startup_pint_irq,
	.shutdown = shutdown_pint_irq,
	.enable = enable_pint_irq,
	.disable = disable_pint_irq,
	.ack = mask_and_ack_pint,
	.end = end_pint_irq
};

static void disable_pint_irq(unsigned int irq)
{
	unsigned long val, flags;

	local_irq_save(flags);
	val = ctrl_inw(INTC_INTER);
	val &= ~(1 << (irq - PINT_IRQ_BASE));
	ctrl_outw(val, INTC_INTER);	/* disable PINTn */
	portcr_mask &= ~(3 << (irq - PINT_IRQ_BASE)*2);
	local_irq_restore(flags);
}

static void enable_pint_irq(unsigned int irq)
{
	unsigned long val, flags;

	local_irq_save(flags);
	val = ctrl_inw(INTC_INTER);
	val |= 1 << (irq - PINT_IRQ_BASE);
	ctrl_outw(val, INTC_INTER);	/* enable PINTn */
	portcr_mask |= 3 << (irq - PINT_IRQ_BASE)*2;
	local_irq_restore(flags);
}

static void mask_and_ack_pint(unsigned int irq)
{
	disable_pint_irq(irq);
}

static void end_pint_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_pint_irq(irq);
}

void make_pint_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &pint_irq_type;
	disable_pint_irq(irq);
}
#endif

void __init init_IRQ(void)
{
#if defined(CONFIG_CPU_SUBTYPE_SH7705) || \
    defined(CONFIG_CPU_SUBTYPE_SH7707) || \
    defined(CONFIG_CPU_SUBTYPE_SH7709)
	int i;
#endif

	make_ipr_irq(TIMER_IRQ, TIMER_IPR_ADDR, TIMER_IPR_POS, TIMER_PRIORITY);
	make_ipr_irq(TIMER1_IRQ, TIMER1_IPR_ADDR, TIMER1_IPR_POS, TIMER1_PRIORITY);
#if defined(CONFIG_SH_RTC)
	make_ipr_irq(RTC_IRQ, RTC_IPR_ADDR, RTC_IPR_POS, RTC_PRIORITY);
#endif

#ifdef SCI_ERI_IRQ
	make_ipr_irq(SCI_ERI_IRQ, SCI_IPR_ADDR, SCI_IPR_POS, SCI_PRIORITY);
	make_ipr_irq(SCI_RXI_IRQ, SCI_IPR_ADDR, SCI_IPR_POS, SCI_PRIORITY);
	make_ipr_irq(SCI_TXI_IRQ, SCI_IPR_ADDR, SCI_IPR_POS, SCI_PRIORITY);
#endif

#ifdef SCIF1_ERI_IRQ
	make_ipr_irq(SCIF1_ERI_IRQ, SCIF1_IPR_ADDR, SCIF1_IPR_POS, SCIF1_PRIORITY);
	make_ipr_irq(SCIF1_RXI_IRQ, SCIF1_IPR_ADDR, SCIF1_IPR_POS, SCIF1_PRIORITY);
	make_ipr_irq(SCIF1_BRI_IRQ, SCIF1_IPR_ADDR, SCIF1_IPR_POS, SCIF1_PRIORITY);
	make_ipr_irq(SCIF1_TXI_IRQ, SCIF1_IPR_ADDR, SCIF1_IPR_POS, SCIF1_PRIORITY);
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7300)
	make_ipr_irq(SCIF0_IRQ, SCIF0_IPR_ADDR, SCIF0_IPR_POS, SCIF0_PRIORITY);
	make_ipr_irq(DMTE2_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY);
	make_ipr_irq(DMTE3_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY);
	make_ipr_irq(VIO_IRQ, VIO_IPR_ADDR, VIO_IPR_POS, VIO_PRIORITY);
#endif

#ifdef SCIF_ERI_IRQ
	make_ipr_irq(SCIF_ERI_IRQ, SCIF_IPR_ADDR, SCIF_IPR_POS, SCIF_PRIORITY);
	make_ipr_irq(SCIF_RXI_IRQ, SCIF_IPR_ADDR, SCIF_IPR_POS, SCIF_PRIORITY);
	make_ipr_irq(SCIF_BRI_IRQ, SCIF_IPR_ADDR, SCIF_IPR_POS, SCIF_PRIORITY);
	make_ipr_irq(SCIF_TXI_IRQ, SCIF_IPR_ADDR, SCIF_IPR_POS, SCIF_PRIORITY);
#endif

#ifdef IRDA_ERI_IRQ
	make_ipr_irq(IRDA_ERI_IRQ, IRDA_IPR_ADDR, IRDA_IPR_POS, IRDA_PRIORITY);
	make_ipr_irq(IRDA_RXI_IRQ, IRDA_IPR_ADDR, IRDA_IPR_POS, IRDA_PRIORITY);
	make_ipr_irq(IRDA_BRI_IRQ, IRDA_IPR_ADDR, IRDA_IPR_POS, IRDA_PRIORITY);
	make_ipr_irq(IRDA_TXI_IRQ, IRDA_IPR_ADDR, IRDA_IPR_POS, IRDA_PRIORITY);
#endif

#if defined(CONFIG_CPU_SUBTYPE_SH7707) || defined(CONFIG_CPU_SUBTYPE_SH7709) || \
    defined(CONFIG_CPU_SUBTYPE_SH7300) || defined(CONFIG_CPU_SUBTYPE_SH7705)
	/*
	 * Initialize the Interrupt Controller (INTC)
	 * registers to their power on values
	 */

	/*
	 * Enable external irq (INTC IRQ mode).
	 * You should set corresponding bits of PFC to "00"
	 * to enable these interrupts.
	 */
	make_ipr_irq(IRQ0_IRQ, IRQ0_IPR_ADDR, IRQ0_IPR_POS, IRQ0_PRIORITY);
	make_ipr_irq(IRQ1_IRQ, IRQ1_IPR_ADDR, IRQ1_IPR_POS, IRQ1_PRIORITY);
	make_ipr_irq(IRQ2_IRQ, IRQ2_IPR_ADDR, IRQ2_IPR_POS, IRQ2_PRIORITY);
	make_ipr_irq(IRQ3_IRQ, IRQ3_IPR_ADDR, IRQ3_IPR_POS, IRQ3_PRIORITY);
	make_ipr_irq(IRQ4_IRQ, IRQ4_IPR_ADDR, IRQ4_IPR_POS, IRQ4_PRIORITY);
	make_ipr_irq(IRQ5_IRQ, IRQ5_IPR_ADDR, IRQ5_IPR_POS, IRQ5_PRIORITY);
#if !defined(CONFIG_CPU_SUBTYPE_SH7300)
	make_ipr_irq(PINT0_IRQ, PINT0_IPR_ADDR, PINT0_IPR_POS, PINT0_PRIORITY);
	make_ipr_irq(PINT8_IRQ, PINT8_IPR_ADDR, PINT8_IPR_POS, PINT8_PRIORITY);
	enable_ipr_irq(PINT0_IRQ);
	enable_ipr_irq(PINT8_IRQ);

	for(i = 0; i < 16; i++)
		make_pint_irq(PINT_IRQ_BASE + i);
	for(i = 0; i < 256; i++)
	{
		if(i & 1) pint_map[i] = 0;
		else if(i & 2) pint_map[i] = 1;
		else if(i & 4) pint_map[i] = 2;
		else if(i & 8) pint_map[i] = 3;
		else if(i & 0x10) pint_map[i] = 4;
		else if(i & 0x20) pint_map[i] = 5;
		else if(i & 0x40) pint_map[i] = 6;
		else if(i & 0x80) pint_map[i] = 7;
	}
#endif /* !CONFIG_CPU_SUBTYPE_SH7300 */
#endif /* CONFIG_CPU_SUBTYPE_SH7707 || CONFIG_CPU_SUBTYPE_SH7709  || CONFIG_CPU_SUBTYPE_SH7300*/

#ifdef CONFIG_CPU_SUBTYPE_ST40
	init_IRQ_intc2();
#endif

	/* Perform the machine specific initialisation */
	if (sh_mv.mv_init_irq != NULL) {
		sh_mv.mv_init_irq();
	}
}
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || defined(CONFIG_CPU_SUBTYPE_SH7709) || \
    defined(CONFIG_CPU_SUBTYPE_SH7300) || defined(CONFIG_CPU_SUBTYPE_SH7705)
int ipr_irq_demux(int irq)
{
#if !defined(CONFIG_CPU_SUBTYPE_SH7300)
	unsigned long creg, dreg, d, sav;

	if(irq == PINT0_IRQ)
	{
#if defined(CONFIG_CPU_SUBTYPE_SH7707)
		creg = PORT_PACR;
		dreg = PORT_PADR;
#else
		creg = PORT_PCCR;
		dreg = PORT_PCDR;
#endif
		sav = ctrl_inw(creg);
		ctrl_outw(sav | portcr_mask, creg);
		d = (~ctrl_inb(dreg) ^ ctrl_inw(INTC_ICR2)) & ctrl_inw(INTC_INTER) & 0xff;
		ctrl_outw(sav, creg);
		if(d == 0) return irq;
		return PINT_IRQ_BASE + pint_map[d];
	}
	else if(irq == PINT8_IRQ)
	{
#if defined(CONFIG_CPU_SUBTYPE_SH7707)
		creg = PORT_PBCR;
		dreg = PORT_PBDR;
#else
		creg = PORT_PFCR;
		dreg = PORT_PFDR;
#endif
		sav = ctrl_inw(creg);
		ctrl_outw(sav | (portcr_mask >> 16), creg);
		d = (~ctrl_inb(dreg) ^ (ctrl_inw(INTC_ICR2) >> 8)) & (ctrl_inw(INTC_INTER) >> 8) & 0xff;
		ctrl_outw(sav, creg);
		if(d == 0) return irq;
		return PINT_IRQ_BASE + 8 + pint_map[d];
	}
#endif
	return irq;
}
#endif

EXPORT_SYMBOL(make_ipr_irq);

