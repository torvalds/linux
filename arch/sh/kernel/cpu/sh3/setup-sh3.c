/*
 * Shared SH3 Setup code
 *
 *  Copyright (C) 2008  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>

/* All SH3 devices are equipped with IRQ0->5 (except sh7708) */

enum {
	UNUSED = 0,

	/* interrupt sources */
	IRQ0, IRQ1, IRQ2, IRQ3, IRQ4, IRQ5,
};

static struct intc_vect vectors_irq0123[] __initdata = {
	INTC_VECT(IRQ0, 0x600), INTC_VECT(IRQ1, 0x620),
	INTC_VECT(IRQ2, 0x640), INTC_VECT(IRQ3, 0x660),
};

static struct intc_vect vectors_irq45[] __initdata = {
	INTC_VECT(IRQ4, 0x680), INTC_VECT(IRQ5, 0x6a0),
};

static struct intc_prio_reg prio_registers[] __initdata = {
	{ 0xa4000016, 0, 16, 4, /* IPRC */ { IRQ3, IRQ2, IRQ1, IRQ0 } },
	{ 0xa4000018, 0, 16, 4, /* IPRD */ { 0, 0, IRQ5, IRQ4 } },
};

static struct intc_mask_reg ack_registers[] __initdata = {
	{ 0xa4000004, 0, 8, /* IRR0 */
	  { 0, 0, IRQ5, IRQ4, IRQ3, IRQ2, IRQ1, IRQ0 } },
};

static struct intc_sense_reg sense_registers[] __initdata = {
	{ 0xa4000010, 16, 2, { 0, 0, IRQ5, IRQ4, IRQ3, IRQ2, IRQ1, IRQ0 } },
};

static DECLARE_INTC_DESC_ACK(intc_desc_irq0123, "sh3-irq0123",
			     vectors_irq0123, NULL, NULL,
			     prio_registers, sense_registers, ack_registers);

static DECLARE_INTC_DESC_ACK(intc_desc_irq45, "sh3-irq45",
			     vectors_irq45, NULL, NULL,
			     prio_registers, sense_registers, ack_registers);

#define INTC_ICR1		0xa4000010UL
#define INTC_ICR1_IRQLVL	(1<<14)

void __init plat_irq_setup_pins(int mode)
{
	if (mode == IRQ_MODE_IRQ) {
		__raw_writew(__raw_readw(INTC_ICR1) & ~INTC_ICR1_IRQLVL, INTC_ICR1);
		register_intc_controller(&intc_desc_irq0123);
		return;
	}
	BUG();
}

void __init plat_irq_setup_sh3(void)
{
	register_intc_controller(&intc_desc_irq45);
}
