/*
 * linux/arch/sh/boards/se/7722/irq.c
 *
 * Copyright (C) 2007  Nobuhiro Iwamatsu
 *
 * Hitachi UL SolutionEngine 7722 Support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/se7722.h>

#define INTC_INTMSK0             0xFFD00044
#define INTC_INTMSKCLR0          0xFFD00064

static void disable_se7722_irq(unsigned int irq)
{
	struct ipr_data *p = get_irq_chip_data(irq);
	ctrl_outw( ctrl_inw( p->addr ) | p->priority , p->addr );
}

static void enable_se7722_irq(unsigned int irq)
{
	struct ipr_data *p = get_irq_chip_data(irq);
	ctrl_outw( ctrl_inw( p->addr ) & ~p->priority , p->addr );
}

static struct irq_chip se7722_irq_chip __read_mostly = {
	.name           = "SE7722",
	.mask           = disable_se7722_irq,
	.unmask         = enable_se7722_irq,
	.mask_ack       = disable_se7722_irq,
};

static struct ipr_data ipr_irq_table[] = {
	/* irq        ,idx,sft, priority     , addr   */
	{ MRSHPC_IRQ0 , 0 , 0 , MRSHPC_BIT0 , IRQ01_MASK } ,
	{ MRSHPC_IRQ1 , 0 , 0 , MRSHPC_BIT1 , IRQ01_MASK } ,
	{ MRSHPC_IRQ2 , 0 , 0 , MRSHPC_BIT2 , IRQ01_MASK } ,
	{ MRSHPC_IRQ3 , 0 , 0 , MRSHPC_BIT3 , IRQ01_MASK } ,
	{ SMC_IRQ     , 0 , 0 , SMC_BIT     , IRQ01_MASK } ,
	{ EXT_IRQ     , 0 , 0 , EXT_BIT     , IRQ01_MASK } ,
};

int se7722_irq_demux(int irq)
{

	if ((irq == IRQ0_IRQ)||(irq == IRQ1_IRQ)) {
		volatile unsigned short intv =
			*(volatile unsigned short *)IRQ01_STS;
		if (irq == IRQ0_IRQ){
			if(intv & SMC_BIT ) {
				return SMC_IRQ;
			} else if(intv & USB_BIT) {
				return USB_IRQ;
			} else {
				printk("intv =%04x\n", intv);
				return SMC_IRQ;
			}
		} else if(irq == IRQ1_IRQ){
			if(intv & MRSHPC_BIT0) {
				return MRSHPC_IRQ0;
			} else if(intv & MRSHPC_BIT1) {
				return MRSHPC_IRQ1;
			} else if(intv & MRSHPC_BIT2) {
				return MRSHPC_IRQ2;
			} else if(intv & MRSHPC_BIT3) {
				return MRSHPC_IRQ3;
			} else {
				printk("BIT_EXTENTION =%04x\n", intv);
				return EXT_IRQ;
			}
		}
	}
	return irq;

}
/*
 * Initialize IRQ setting
 */
void __init init_se7722_IRQ(void)
{
	int i = 0;
	ctrl_outw(0x2000, 0xb03fffec);  /* mrshpc irq enable */
	ctrl_outl((3 << ((7 - 0) * 4))|(3 << ((7 - 1) * 4)), INTC_INTPRI0);     /* irq0 pri=3,irq1,pri=3 */
	ctrl_outw((2 << ((7 - 0) * 2))|(2 << ((7 - 1) * 2)), INTC_ICR1);        /* irq0,1 low-level irq */

	for (i = 0; i < ARRAY_SIZE(ipr_irq_table); i++) {
		disable_irq_nosync(ipr_irq_table[i].irq);
		set_irq_chip_and_handler_name( ipr_irq_table[i].irq, &se7722_irq_chip,
			handle_level_irq, "level");
		set_irq_chip_data( ipr_irq_table[i].irq, &ipr_irq_table[i] );
		disable_se7722_irq(ipr_irq_table[i].irq);
	}
}
