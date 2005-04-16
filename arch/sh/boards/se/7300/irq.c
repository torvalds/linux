/*
 * linux/arch/sh/boards/se/7300/irq.c
 *
 * Copyright (C) 2003 Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 *
 * SH-Mobile SolutionEngine 7300 Support.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mach/se7300.h>

/*
 * Initialize IRQ setting
 */
void __init
init_7300se_IRQ(void)
{
	ctrl_outw(0x0028, PA_EPLD_MODESET);	/* mode set IRQ0,1 active low. */
	ctrl_outw(0xa000, INTC_ICR1);	        /* IRQ mode; IRQ0,1 enable.    */
	ctrl_outw(0x0000, PORT_PFCR);	        /* use F for IRQ[3:0] and SIU. */

	/* PC_IRQ[0-3] -> IRQ0 (32) */
	make_ipr_irq(IRQ0_IRQ, IRQ0_IPR_ADDR, IRQ0_IPR_POS, 0x0f - IRQ0_IRQ);
	/* A_IRQ[0-3] -> IRQ1 (33) */
	make_ipr_irq(IRQ1_IRQ, IRQ1_IPR_ADDR, IRQ1_IPR_POS, 0x0f - IRQ1_IRQ);
	make_ipr_irq(SIOF0_IRQ, SIOF0_IPR_ADDR, SIOF0_IPR_POS, SIOF0_PRIORITY);
	make_ipr_irq(DMTE2_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY);
	make_ipr_irq(DMTE3_IRQ, DMA1_IPR_ADDR, DMA1_IPR_POS, DMA1_PRIORITY);
	make_ipr_irq(VIO_IRQ, VIO_IPR_ADDR, VIO_IPR_POS, VIO_PRIORITY);

	ctrl_outw(0x2000, PA_MRSHPC + 0x0c);	/* mrshpc irq enable */
}
