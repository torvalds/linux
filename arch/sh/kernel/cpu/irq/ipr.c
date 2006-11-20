/*
 * Interrupt handling for IPR-based IRQ.
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2003  Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 * Copyright (C) 2006  Paul Mundt
 *
 * Supported system:
 *	On-chip supporting modules (TMU, RTC, etc.).
 *	On-chip supporting modules for SH7709/SH7709A/SH7729/SH7300.
 *	Hitachi SolutionEngine external I/O:
 *		MS7709SE01, MS7709ASE01, and MS7750SE01
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/interrupt.h>

static void disable_ipr_irq(unsigned int irq)
{
	struct ipr_data *p = get_irq_chip_data(irq);
	int shift = p->shift*4;
	/* Set the priority in IPR to 0 */
	ctrl_outw(ctrl_inw(p->addr) & (0xffff ^ (0xf << shift)), p->addr);
}

static void enable_ipr_irq(unsigned int irq)
{
	struct ipr_data *p = get_irq_chip_data(irq);
	int shift = p->shift*4;
	/* Set priority in IPR back to original value */
	ctrl_outw(ctrl_inw(p->addr) | (p->priority << shift), p->addr);
}

static struct irq_chip ipr_irq_chip = {
	.name		= "IPR",
	.mask		= disable_ipr_irq,
	.unmask		= enable_ipr_irq,
	.mask_ack	= disable_ipr_irq,
};

void make_ipr_irq(struct ipr_data *table, unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		unsigned int irq = table[i].irq;
		disable_irq_nosync(irq);
		set_irq_chip_and_handler_name(irq, &ipr_irq_chip,
				      handle_level_irq, "level");
		set_irq_chip_data(irq, &table[i]);
		enable_ipr_irq(irq);
	}
}
EXPORT_SYMBOL(make_ipr_irq);

/*
 * XXX: Move this garbage in to the drivers, and kill off the ridiculous CPU
 * subtype checks.
 */
static struct ipr_data sys_ipr_map[] = {
#ifndef CONFIG_CPU_SUBTYPE_SH7780
	{ TIMER_IRQ, TIMER_IPR_ADDR, TIMER_IPR_POS, TIMER_PRIORITY },
	{ TIMER1_IRQ, TIMER1_IPR_ADDR, TIMER1_IPR_POS, TIMER1_PRIORITY },
#ifdef RTC_IRQ
	{ RTC_IRQ, RTC_IPR_ADDR, RTC_IPR_POS, RTC_PRIORITY },
#endif
#ifdef SCI_ERI_IRQ
	{ SCI_ERI_IRQ, SCI_IPR_ADDR, SCI_IPR_POS, SCI_PRIORITY },
	{ SCI_RXI_IRQ, SCI_IPR_ADDR, SCI_IPR_POS, SCI_PRIORITY },
	{ SCI_TXI_IRQ, SCI_IPR_ADDR, SCI_IPR_POS, SCI_PRIORITY },
#endif
#ifdef SCIF1_ERI_IRQ
	{ SCIF1_ERI_IRQ, SCIF1_IPR_ADDR, SCIF1_IPR_POS, SCIF1_PRIORITY },
	{ SCIF1_RXI_IRQ, SCIF1_IPR_ADDR, SCIF1_IPR_POS, SCIF1_PRIORITY },
	{ SCIF1_BRI_IRQ, SCIF1_IPR_ADDR, SCIF1_IPR_POS, SCIF1_PRIORITY },
	{ SCIF1_TXI_IRQ, SCIF1_IPR_ADDR, SCIF1_IPR_POS, SCIF1_PRIORITY },
#endif
#ifdef SCIF2_ERI_IRQ
	{ SCIF2_ERI_IRQ, SCIF2_IPR_ADDR, SCIF2_IPR_POS, SCIF2_PRIORITY },
	{ SCIF2_RXI_IRQ, SCIF2_IPR_ADDR, SCIF2_IPR_POS, SCIF2_PRIORITY },
	{ SCIF2_BRI_IRQ, SCIF2_IPR_ADDR, SCIF2_IPR_POS, SCIF2_PRIORITY },
	{ SCIF2_TXI_IRQ, SCIF2_IPR_ADDR, SCIF2_IPR_POS, SCIF2_PRIORITY },
#endif
#ifdef SCIF3_ERI_IRQ
	{ SCIF3_ERI_IRQ, SCIF3_IPR_ADDR, SCIF3_IPR_POS, SCIF3_PRIORITY },
	{ SCIF3_RXI_IRQ, SCIF3_IPR_ADDR, SCIF3_IPR_POS, SCIF3_PRIORITY },
	{ SCIF3_BRI_IRQ, SCIF3_IPR_ADDR, SCIF3_IPR_POS, SCIF3_PRIORITY },
	{ SCIF3_TXI_IRQ, SCIF3_IPR_ADDR, SCIF3_IPR_POS, SCIF3_PRIORITY },
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7300)
	{ SCIF0_IRQ, SCIF0_IPR_ADDR, SCIF0_IPR_POS, SCIF0_PRIORITY },
	{ DMTE2_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY },
	{ DMTE3_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY },
	{ VIO_IRQ, VIO_IPR_ADDR, VIO_IPR_POS, VIO_PRIORITY },
#endif
#ifdef SCIF_ERI_IRQ
	{ SCIF_ERI_IRQ, SCIF_IPR_ADDR, SCIF_IPR_POS, SCIF_PRIORITY },
	{ SCIF_RXI_IRQ, SCIF_IPR_ADDR, SCIF_IPR_POS, SCIF_PRIORITY },
	{ SCIF_BRI_IRQ, SCIF_IPR_ADDR, SCIF_IPR_POS, SCIF_PRIORITY },
	{ SCIF_TXI_IRQ, SCIF_IPR_ADDR, SCIF_IPR_POS, SCIF_PRIORITY },
#endif
#ifdef IRDA_ERI_IRQ
	{ IRDA_ERI_IRQ, IRDA_IPR_ADDR, IRDA_IPR_POS, IRDA_PRIORITY },
	{ IRDA_RXI_IRQ, IRDA_IPR_ADDR, IRDA_IPR_POS, IRDA_PRIORITY },
	{ IRDA_BRI_IRQ, IRDA_IPR_ADDR, IRDA_IPR_POS, IRDA_PRIORITY },
	{ IRDA_TXI_IRQ, IRDA_IPR_ADDR, IRDA_IPR_POS, IRDA_PRIORITY },
#endif
#if defined(CONFIG_CPU_SUBTYPE_SH7707) || defined(CONFIG_CPU_SUBTYPE_SH7709) || \
    defined(CONFIG_CPU_SUBTYPE_SH7706) || \
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
	{ IRQ0_IRQ, IRQ0_IPR_ADDR, IRQ0_IPR_POS, IRQ0_PRIORITY },
	{ IRQ1_IRQ, IRQ1_IPR_ADDR, IRQ1_IPR_POS, IRQ1_PRIORITY },
	{ IRQ2_IRQ, IRQ2_IPR_ADDR, IRQ2_IPR_POS, IRQ2_PRIORITY },
	{ IRQ3_IRQ, IRQ3_IPR_ADDR, IRQ3_IPR_POS, IRQ3_PRIORITY },
	{ IRQ4_IRQ, IRQ4_IPR_ADDR, IRQ4_IPR_POS, IRQ4_PRIORITY },
	{ IRQ5_IRQ, IRQ5_IPR_ADDR, IRQ5_IPR_POS, IRQ5_PRIORITY },
#endif
#endif
};

void __init init_IRQ(void)
{
	make_ipr_irq(sys_ipr_map, ARRAY_SIZE(sys_ipr_map));

#ifdef CONFIG_CPU_HAS_PINT_IRQ
	init_IRQ_pint();
#endif

#ifdef CONFIG_CPU_HAS_INTC2_IRQ
	init_IRQ_intc2();
#endif
	/* Perform the machine specific initialisation */
	if (sh_mv.mv_init_irq != NULL)
		sh_mv.mv_init_irq();

	irq_ctx_init(smp_processor_id());
}

#if !defined(CONFIG_CPU_HAS_PINT_IRQ)
int ipr_irq_demux(int irq)
{
	return irq;
}
#endif
