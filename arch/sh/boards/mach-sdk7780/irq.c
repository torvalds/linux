// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/sh/boards/renesas/sdk7780/irq.c
 *
 * Renesas Technology Europe SDK7780 Support.
 *
 * Copyright (C) 2008  Nicholas Beck <nbeck@mpc-data.co.uk>
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <mach/sdk7780.h>

enum {
	UNUSED = 0,
	/* board specific interrupt sources */
	SMC91C111,	/* Ethernet controller */
};

static struct intc_vect fpga_vectors[] __initdata = {
	INTC_IRQ(SMC91C111, IRQ_ETHERNET),
};

static struct intc_mask_reg fpga_mask_registers[] __initdata = {
	{ 0, FPGA_IRQ0MR, 16,
	  { 0, 0, 0, 0, 0, 0, 0, 0,
	    0, 0, 0, SMC91C111, 0, 0, 0, 0 } },
};

static DECLARE_INTC_DESC(fpga_intc_desc, "sdk7780-irq", fpga_vectors,
			 NULL, fpga_mask_registers, NULL, NULL);

void __init init_sdk7780_IRQ(void)
{
	printk(KERN_INFO "Using SDK7780 interrupt controller.\n");

	__raw_writew(0xFFFF, FPGA_IRQ0MR);
	/* Setup IRL 0-3 */
	__raw_writew(0x0003, FPGA_IMSR);
	plat_irq_setup_pins(IRQ_MODE_IRL3210);

	register_intc_controller(&fpga_intc_desc);
}
