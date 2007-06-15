/*
 * File:         arch/blackfin/mach-common/ints-priority-sc.c
 * Based on:
 * Author:
 *
 * Created:      ?
 * Description:  Set up the interrupt priorities
 *
 * Modified:
 *               1996 Roman Zippel
 *               1999 D. Jeff Dionne <jeff@uclinux.org>
 *               2000-2001 Lineo, Inc. D. Jefff Dionne <jeff@lineo.ca>
 *               2002 Arcturus Networks Inc. MaTed <mated@sympatico.ca>
 *               2003 Metrowerks/Motorola
 *               2003 Bas Vermeulen <bas@buyways.nl>
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <linux/irq.h>
#ifdef CONFIG_KGDB
#include <linux/kgdb.h>
#endif
#include <asm/traps.h>
#include <asm/blackfin.h>
#include <asm/gpio.h>
#include <asm/irq_handler.h>

#ifdef BF537_FAMILY
# define BF537_GENERIC_ERROR_INT_DEMUX
#else
# undef BF537_GENERIC_ERROR_INT_DEMUX
#endif

/*
 * NOTES:
 * - we have separated the physical Hardware interrupt from the
 * levels that the LINUX kernel sees (see the description in irq.h)
 * -
 */

unsigned long irq_flags = 0;

/* The number of spurious interrupts */
atomic_t num_spurious;

struct ivgx {
	/* irq number for request_irq, available in mach-bf533/irq.h */
	int irqno;
	/* corresponding bit in the SIC_ISR register */
	int isrflag;
} ivg_table[NR_PERI_INTS];

struct ivg_slice {
	/* position of first irq in ivg_table for given ivg */
	struct ivgx *ifirst;
	struct ivgx *istop;
} ivg7_13[IVG13 - IVG7 + 1];

static void search_IAR(void);

/*
 * Search SIC_IAR and fill tables with the irqvalues
 * and their positions in the SIC_ISR register.
 */
static void __init search_IAR(void)
{
	unsigned ivg, irq_pos = 0;
	for (ivg = 0; ivg <= IVG13 - IVG7; ivg++) {
		int irqn;

		ivg7_13[ivg].istop = ivg7_13[ivg].ifirst =
		    &ivg_table[irq_pos];

		for (irqn = 0; irqn < NR_PERI_INTS; irqn++) {
			int iar_shift = (irqn & 7) * 4;
			if (ivg ==
			    (0xf &
			     bfin_read32((unsigned long *) SIC_IAR0 +
					 (irqn >> 3)) >> iar_shift)) {
				ivg_table[irq_pos].irqno = IVG7 + irqn;
				ivg_table[irq_pos].isrflag = 1 << irqn;
				ivg7_13[ivg].istop++;
				irq_pos++;
			}
		}
	}
}

/*
 * This is for BF533 internal IRQs
 */

static void ack_noop(unsigned int irq)
{
	/* Dummy function.  */
}

static void bfin_core_mask_irq(unsigned int irq)
{
	irq_flags &= ~(1 << irq);
	if (!irqs_disabled())
		local_irq_enable();
}

static void bfin_core_unmask_irq(unsigned int irq)
{
	irq_flags |= 1 << irq;
	/*
	 * If interrupts are enabled, IMASK must contain the same value
	 * as irq_flags.  Make sure that invariant holds.  If interrupts
	 * are currently disabled we need not do anything; one of the
	 * callers will take care of setting IMASK to the proper value
	 * when reenabling interrupts.
	 * local_irq_enable just does "STI irq_flags", so it's exactly
	 * what we need.
	 */
	if (!irqs_disabled())
		local_irq_enable();
	return;
}

static void bfin_internal_mask_irq(unsigned int irq)
{
	bfin_write_SIC_IMASK(bfin_read_SIC_IMASK() &
			     ~(1 << (irq - (IRQ_CORETMR + 1))));
	SSYNC();
}

static void bfin_internal_unmask_irq(unsigned int irq)
{
	bfin_write_SIC_IMASK(bfin_read_SIC_IMASK() |
			     (1 << (irq - (IRQ_CORETMR + 1))));
	SSYNC();
}

static struct irq_chip bfin_core_irqchip = {
	.ack = ack_noop,
	.mask = bfin_core_mask_irq,
	.unmask = bfin_core_unmask_irq,
};

static struct irq_chip bfin_internal_irqchip = {
	.ack = ack_noop,
	.mask = bfin_internal_mask_irq,
	.unmask = bfin_internal_unmask_irq,
};

#ifdef BF537_GENERIC_ERROR_INT_DEMUX
static int error_int_mask;

static void bfin_generic_error_ack_irq(unsigned int irq)
{

}

static void bfin_generic_error_mask_irq(unsigned int irq)
{
	error_int_mask &= ~(1L << (irq - IRQ_PPI_ERROR));

	if (!error_int_mask) {
		local_irq_disable();
		bfin_write_SIC_IMASK(bfin_read_SIC_IMASK() &
				     ~(1 <<
				       (IRQ_GENERIC_ERROR -
					(IRQ_CORETMR + 1))));
		SSYNC();
		local_irq_enable();
	}
}

static void bfin_generic_error_unmask_irq(unsigned int irq)
{
	local_irq_disable();
	bfin_write_SIC_IMASK(bfin_read_SIC_IMASK() | 1 <<
			     (IRQ_GENERIC_ERROR - (IRQ_CORETMR + 1)));
	SSYNC();
	local_irq_enable();

	error_int_mask |= 1L << (irq - IRQ_PPI_ERROR);
}

static struct irq_chip bfin_generic_error_irqchip = {
	.ack = bfin_generic_error_ack_irq,
	.mask = bfin_generic_error_mask_irq,
	.unmask = bfin_generic_error_unmask_irq,
};

static void bfin_demux_error_irq(unsigned int int_err_irq,
				  struct irq_desc *intb_desc)
{
	int irq = 0;

	SSYNC();

#if (defined(CONFIG_BF537) || defined(CONFIG_BF536))
	if (bfin_read_EMAC_SYSTAT() & EMAC_ERR_MASK)
		irq = IRQ_MAC_ERROR;
	else
#endif
	if (bfin_read_SPORT0_STAT() & SPORT_ERR_MASK)
		irq = IRQ_SPORT0_ERROR;
	else if (bfin_read_SPORT1_STAT() & SPORT_ERR_MASK)
		irq = IRQ_SPORT1_ERROR;
	else if (bfin_read_PPI_STATUS() & PPI_ERR_MASK)
		irq = IRQ_PPI_ERROR;
	else if (bfin_read_CAN_GIF() & CAN_ERR_MASK)
		irq = IRQ_CAN_ERROR;
	else if (bfin_read_SPI_STAT() & SPI_ERR_MASK)
		irq = IRQ_SPI_ERROR;
	else if ((bfin_read_UART0_IIR() & UART_ERR_MASK_STAT1) &&
		 (bfin_read_UART0_IIR() & UART_ERR_MASK_STAT0))
		irq = IRQ_UART0_ERROR;
	else if ((bfin_read_UART1_IIR() & UART_ERR_MASK_STAT1) &&
		 (bfin_read_UART1_IIR() & UART_ERR_MASK_STAT0))
		irq = IRQ_UART1_ERROR;

	if (irq) {
		if (error_int_mask & (1L << (irq - IRQ_PPI_ERROR))) {
			struct irq_desc *desc = irq_desc + irq;
			desc->handle_irq(irq, desc);
		} else {

			switch (irq) {
			case IRQ_PPI_ERROR:
				bfin_write_PPI_STATUS(PPI_ERR_MASK);
				break;
#if (defined(CONFIG_BF537) || defined(CONFIG_BF536))
			case IRQ_MAC_ERROR:
				bfin_write_EMAC_SYSTAT(EMAC_ERR_MASK);
				break;
#endif
			case IRQ_SPORT0_ERROR:
				bfin_write_SPORT0_STAT(SPORT_ERR_MASK);
				break;

			case IRQ_SPORT1_ERROR:
				bfin_write_SPORT1_STAT(SPORT_ERR_MASK);
				break;

			case IRQ_CAN_ERROR:
				bfin_write_CAN_GIS(CAN_ERR_MASK);
				break;

			case IRQ_SPI_ERROR:
				bfin_write_SPI_STAT(SPI_ERR_MASK);
				break;

			default:
				break;
			}

			pr_debug("IRQ %d:"
				" MASKED PERIPHERAL ERROR INTERRUPT ASSERTED\n",
				irq);
		}
	} else
		printk(KERN_ERR
		       "%s : %s : LINE %d :\nIRQ ?: PERIPHERAL ERROR"
		       " INTERRUPT ASSERTED BUT NO SOURCE FOUND\n",
		       __FUNCTION__, __FILE__, __LINE__);


}
#endif				/* BF537_GENERIC_ERROR_INT_DEMUX */

#ifdef CONFIG_IRQCHIP_DEMUX_GPIO

static unsigned short gpio_enabled[gpio_bank(MAX_BLACKFIN_GPIOS)];
static unsigned short gpio_edge_triggered[gpio_bank(MAX_BLACKFIN_GPIOS)];

static void bfin_gpio_ack_irq(unsigned int irq)
{
	u16 gpionr = irq - IRQ_PF0;

	if (gpio_edge_triggered[gpio_bank(gpionr)] & gpio_bit(gpionr)) {
		set_gpio_data(gpionr, 0);
		SSYNC();
	}
}

static void bfin_gpio_mask_ack_irq(unsigned int irq)
{
	u16 gpionr = irq - IRQ_PF0;

	if (gpio_edge_triggered[gpio_bank(gpionr)] & gpio_bit(gpionr)) {
		set_gpio_data(gpionr, 0);
		SSYNC();
	}

	set_gpio_maska(gpionr, 0);
	SSYNC();
}

static void bfin_gpio_mask_irq(unsigned int irq)
{
	set_gpio_maska(irq - IRQ_PF0, 0);
	SSYNC();
}

static void bfin_gpio_unmask_irq(unsigned int irq)
{
	set_gpio_maska(irq - IRQ_PF0, 1);
	SSYNC();
}

static unsigned int bfin_gpio_irq_startup(unsigned int irq)
{
	unsigned int ret;
	u16 gpionr = irq - IRQ_PF0;

	if (!(gpio_enabled[gpio_bank(gpionr)] & gpio_bit(gpionr))) {
		ret = gpio_request(gpionr, NULL);
		if (ret)
			return ret;
	}

	gpio_enabled[gpio_bank(gpionr)] |= gpio_bit(gpionr);
	bfin_gpio_unmask_irq(irq);

	return ret;
}

static void bfin_gpio_irq_shutdown(unsigned int irq)
{
	bfin_gpio_mask_irq(irq);
	gpio_free(irq - IRQ_PF0);
	gpio_enabled[gpio_bank(irq - IRQ_PF0)] &= ~gpio_bit(irq - IRQ_PF0);
}

static int bfin_gpio_irq_type(unsigned int irq, unsigned int type)
{

	unsigned int ret;
	u16 gpionr = irq - IRQ_PF0;

	if (type == IRQ_TYPE_PROBE) {
		/* only probe unenabled GPIO interrupt lines */
		if (gpio_enabled[gpio_bank(gpionr)] & gpio_bit(gpionr))
			return 0;
		type = IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING;
	}

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING |
	            IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
	{
		if (!(gpio_enabled[gpio_bank(gpionr)] & gpio_bit(gpionr))) {
			ret = gpio_request(gpionr, NULL);
			if (ret)
				return ret;
		}

		gpio_enabled[gpio_bank(gpionr)] |= gpio_bit(gpionr);
	} else {
		gpio_enabled[gpio_bank(gpionr)] &= ~gpio_bit(gpionr);
		return 0;
	}

	set_gpio_dir(gpionr, 0);
	set_gpio_inen(gpionr, 1);

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) {
		gpio_edge_triggered[gpio_bank(gpionr)] |= gpio_bit(gpionr);
		set_gpio_edge(gpionr, 1);
	} else {
		set_gpio_edge(gpionr, 0);
		gpio_edge_triggered[gpio_bank(gpionr)] &= ~gpio_bit(gpionr);
	}

	if ((type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING))
	    == (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING))
		set_gpio_both(gpionr, 1);
	else
		set_gpio_both(gpionr, 0);

	if ((type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_LEVEL_LOW)))
		set_gpio_polar(gpionr, 1);	/* low or falling edge denoted by one */
	else
		set_gpio_polar(gpionr, 0);	/* high or rising edge denoted by zero */

	SSYNC();

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING))
		set_irq_handler(irq, handle_edge_irq);
	else
		set_irq_handler(irq, handle_level_irq);

	return 0;
}


static struct irq_chip bfin_gpio_irqchip = {
	.ack = bfin_gpio_ack_irq,
	.mask = bfin_gpio_mask_irq,
	.mask_ack = bfin_gpio_mask_ack_irq,
	.unmask = bfin_gpio_unmask_irq,
	.set_type = bfin_gpio_irq_type,
	.startup = bfin_gpio_irq_startup,
	.shutdown = bfin_gpio_irq_shutdown
};

static void bfin_demux_gpio_irq(unsigned int intb_irq,
				 struct irq_desc *intb_desc)
{
	u16 i;

	for (i = 0; i < MAX_BLACKFIN_GPIOS; i+=16) {
		int irq = IRQ_PF0 + i;
		int flag_d = get_gpiop_data(i);
		int mask =
			flag_d & (gpio_enabled[gpio_bank(i)] &
			      get_gpiop_maska(i));

		while (mask) {
			if (mask & 1) {
				struct irq_desc *desc = irq_desc + irq;
				desc->handle_irq(irq, desc);
			}
			irq++;
			mask >>= 1;
		}
	}
}

#endif				/* CONFIG_IRQCHIP_DEMUX_GPIO */

/*
 * This function should be called during kernel startup to initialize
 * the BFin IRQ handling routines.
 */
int __init init_arch_irq(void)
{
	int irq;
	unsigned long ilat = 0;
	/*  Disable all the peripheral intrs  - page 4-29 HW Ref manual */
	bfin_write_SIC_IMASK(SIC_UNMASK_ALL);
	SSYNC();

	local_irq_disable();

#ifndef CONFIG_KGDB
	bfin_write_EVT0(evt_emulation);
#endif
	bfin_write_EVT2(evt_evt2);
	bfin_write_EVT3(trap);
	bfin_write_EVT5(evt_ivhw);
	bfin_write_EVT6(evt_timer);
	bfin_write_EVT7(evt_evt7);
	bfin_write_EVT8(evt_evt8);
	bfin_write_EVT9(evt_evt9);
	bfin_write_EVT10(evt_evt10);
	bfin_write_EVT11(evt_evt11);
	bfin_write_EVT12(evt_evt12);
	bfin_write_EVT13(evt_evt13);
	bfin_write_EVT14(evt14_softirq);
	bfin_write_EVT15(evt_system_call);
	CSYNC();

	for (irq = 0; irq < SYS_IRQS; irq++) {
		if (irq <= IRQ_CORETMR)
			set_irq_chip(irq, &bfin_core_irqchip);
		else
			set_irq_chip(irq, &bfin_internal_irqchip);
#ifdef BF537_GENERIC_ERROR_INT_DEMUX
		if (irq != IRQ_GENERIC_ERROR) {
#endif

#ifdef CONFIG_IRQCHIP_DEMUX_GPIO
			if ((irq != IRQ_PROG_INTA) /*PORT F & G MASK_A Interrupt*/
# if defined(BF537_FAMILY) && !(defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE))
				&& (irq != IRQ_MAC_RX) /*PORT H MASK_A Interrupt*/
# endif
			    ) {
#endif
				set_irq_handler(irq, handle_simple_irq);
#ifdef CONFIG_IRQCHIP_DEMUX_GPIO
			} else {
				set_irq_chained_handler(irq,
							bfin_demux_gpio_irq);
			}
#endif

#ifdef BF537_GENERIC_ERROR_INT_DEMUX
		} else {
			set_irq_handler(irq, bfin_demux_error_irq);
		}
#endif
	}
#ifdef BF537_GENERIC_ERROR_INT_DEMUX
	for (irq = IRQ_PPI_ERROR; irq <= IRQ_UART1_ERROR; irq++) {
		set_irq_chip(irq, &bfin_generic_error_irqchip);
		set_irq_handler(irq, handle_level_irq);
	}
#endif

#ifdef CONFIG_IRQCHIP_DEMUX_GPIO
	for (irq = IRQ_PF0; irq < NR_IRQS; irq++) {
		set_irq_chip(irq, &bfin_gpio_irqchip);
		/* if configured as edge, then will be changed to do_edge_IRQ */
		set_irq_handler(irq, handle_level_irq);
	}
#endif
	bfin_write_IMASK(0);
	CSYNC();
	ilat = bfin_read_ILAT();
	CSYNC();
	bfin_write_ILAT(ilat);
	CSYNC();

	printk(KERN_INFO
	       "Configuring Blackfin Priority Driven Interrupts\n");
	/* IMASK=xxx is equivalent to STI xx or irq_flags=xx,
	 * local_irq_enable()
	 */
	program_IAR();
	/* Therefore it's better to setup IARs before interrupts enabled */
	search_IAR();

	/* Enable interrupts IVG7-15 */
	irq_flags = irq_flags | IMASK_IVG15 |
	    IMASK_IVG14 | IMASK_IVG13 | IMASK_IVG12 | IMASK_IVG11 |
	    IMASK_IVG10 | IMASK_IVG9 | IMASK_IVG8 | IMASK_IVG7 |
	    IMASK_IVGHW;

	return 0;
}

#ifdef CONFIG_DO_IRQ_L1
void do_irq(int vec, struct pt_regs *fp)__attribute__((l1_text));
#endif

void do_irq(int vec, struct pt_regs *fp)
{
	if (vec == EVT_IVTMR_P) {
		vec = IRQ_CORETMR;
	} else {
		struct ivgx *ivg = ivg7_13[vec - IVG7].ifirst;
		struct ivgx *ivg_stop = ivg7_13[vec - IVG7].istop;
		unsigned long sic_status;

		SSYNC();
		sic_status = bfin_read_SIC_IMASK() & bfin_read_SIC_ISR();

		for (;; ivg++) {
			if (ivg >= ivg_stop) {
				atomic_inc(&num_spurious);
				return;
			} else if (sic_status & ivg->isrflag)
				break;
		}
		vec = ivg->irqno;
	}
	asm_do_IRQ(vec, fp);

#ifdef CONFIG_KGDB
	kgdb_process_breakpoint();
#endif
}
