/*
 * Renesas Technology SH7710 VoIP Gateway
 *
 * Copyright (C) 2006  Ranjit Deshpande
 * Kenati Technologies Inc.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */
#include <linux/init.h>
#include <asm/machvec.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/irq.h>

static struct ipr_data sh7710voipgw_ipr_map[] = {
	{ TIMER2_IRQ, TIMER2_IPR_ADDR, TIMER2_IPR_POS, TIMER2_PRIORITY },
	{ WDT_IRQ, WDT_IPR_ADDR, WDT_IPR_POS, WDT_PRIORITY },

	/* SCIF0 */
	{ SCIF0_ERI_IRQ, SCIF0_IPR_ADDR, SCIF0_IPR_POS, SCIF0_PRIORITY },
	{ SCIF0_RXI_IRQ, SCIF0_IPR_ADDR, SCIF0_IPR_POS, SCIF0_PRIORITY },
	{ SCIF0_BRI_IRQ, SCIF0_IPR_ADDR, SCIF0_IPR_POS, SCIF0_PRIORITY },
	{ SCIF0_TXI_IRQ, SCIF0_IPR_ADDR, SCIF0_IPR_POS, SCIF0_PRIORITY },

	/* DMAC-1 */
	{ DMTE0_IRQ, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY },
	{ DMTE1_IRQ, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY },
	{ DMTE2_IRQ, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY },
	{ DMTE3_IRQ, DMA_IPR_ADDR, DMA_IPR_POS, DMA_PRIORITY },

	/* DMAC-2 */
	{ DMTE4_IRQ, DMA2_IPR_ADDR, DMA2_IPR_POS, DMA2_PRIORITY },
	{ DMTE4_IRQ, DMA2_IPR_ADDR, DMA2_IPR_POS, DMA2_PRIORITY },

	/* IPSEC */
	{ IPSEC_IRQ, IPSEC_IPR_ADDR, IPSEC_IPR_POS, IPSEC_PRIORITY },

	/* EDMAC */
	{ EDMAC0_IRQ, EDMAC0_IPR_ADDR, EDMAC0_IPR_POS, EDMAC0_PRIORITY },
	{ EDMAC1_IRQ, EDMAC1_IPR_ADDR, EDMAC1_IPR_POS, EDMAC1_PRIORITY },
	{ EDMAC2_IRQ, EDMAC2_IPR_ADDR, EDMAC2_IPR_POS, EDMAC2_PRIORITY },

	/* SIOF0 */
	{ SIOF0_ERI_IRQ, SIOF0_IPR_ADDR, SIOF0_IPR_POS, SIOF0_PRIORITY },
	{ SIOF0_TXI_IRQ, SIOF0_IPR_ADDR, SIOF0_IPR_POS, SIOF0_PRIORITY },
	{ SIOF0_RXI_IRQ, SIOF0_IPR_ADDR, SIOF0_IPR_POS, SIOF0_PRIORITY },
	{ SIOF0_CCI_IRQ, SIOF0_IPR_ADDR, SIOF0_IPR_POS, SIOF0_PRIORITY },

	/* SIOF1 */
	{ SIOF1_ERI_IRQ, SIOF1_IPR_ADDR, SIOF1_IPR_POS, SIOF1_PRIORITY },
	{ SIOF1_TXI_IRQ, SIOF1_IPR_ADDR, SIOF1_IPR_POS, SIOF1_PRIORITY },
	{ SIOF1_RXI_IRQ, SIOF1_IPR_ADDR, SIOF1_IPR_POS, SIOF1_PRIORITY },
	{ SIOF1_CCI_IRQ, SIOF1_IPR_ADDR, SIOF1_IPR_POS, SIOF1_PRIORITY },

	/* SLIC IRQ's */
	{ IRQ1_IRQ, IRQ1_IPR_ADDR, IRQ1_IPR_POS, IRQ1_PRIORITY },
	{ IRQ2_IRQ, IRQ2_IPR_ADDR, IRQ2_IPR_POS, IRQ2_PRIORITY },
};

/*
 * Initialize IRQ setting
 */
static void __init sh7710voipgw_init_irq(void)
{
	/* Disable all interrupts in IPR registers */
	ctrl_outw(0x0, INTC_IPRA);
	ctrl_outw(0x0, INTC_IPRB);
	ctrl_outw(0x0, INTC_IPRC);
	ctrl_outw(0x0, INTC_IPRD);
	ctrl_outw(0x0, INTC_IPRE);
	ctrl_outw(0x0, INTC_IPRF);
	ctrl_outw(0x0, INTC_IPRG);
	ctrl_outw(0x0, INTC_IPRH);
	ctrl_outw(0x0, INTC_IPRI);

	/* Ack all interrupt sources in the IRR0 register */
	ctrl_outb(0x3f, INTC_IRR0);

	/* Use IRQ0 - IRQ3 as active low interrupt lines i.e. disable
	 * IRL mode.
	 */
	ctrl_outw(0x2aa, INTC_ICR1);

	make_ipr_irq(sh7710voipgw_ipr_map, ARRAY_SIZE(sh7710voipgw_ipr_map));
}

/*
 * The Machine Vector
 */
static struct sh_machine_vector mv_sh7710voipgw __initmv = {
	.mv_name		= "SH7710 VoIP Gateway",
	.mv_nr_irqs		= 104,
	.mv_init_irq		= sh7710voipgw_init_irq,
};
