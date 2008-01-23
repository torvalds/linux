/*
 * linux/arch/sh/boards/renesas/sdk7780/irq.c
 *
 * Renesas Technology Europe SDK7780 Support.
 *
 * Copyright (C) 2008  Nicholas Beck <nbeck@mpc-data.co.uk>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/sdk7780.h>

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

	ctrl_outw(0xFFFF, FPGA_IRQ0MR);
	/* Setup IRL 0-3 */
	ctrl_outw(0x0003, FPGA_IMSR);
	plat_irq_setup_pins(IRQ_MODE_IRL3210);

	register_intc_controller(&fpga_intc_desc);
}
