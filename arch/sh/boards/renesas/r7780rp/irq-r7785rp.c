/*
 * Renesas Solutions Highlander R7785RP Support.
 *
 * Copyright (C) 2002  Atom Create Engineering Co., Ltd.
 * Copyright (C) 2006  Paul Mundt
 * Copyright (C) 2007  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/r7780rp.h>

enum {
	UNUSED = 0,

	/* board specific interrupt sources */
	AX88796,          /* Ethernet controller */
	CF,               /* Compact Flash */
};

static struct intc_vect vectors[] __initdata = {
	INTC_IRQ(CF, IRQ_CF),
	INTC_IRQ(AX88796, IRQ_AX88796),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xa4000010, 0, 16, /* IRLMCR1 */
	  { 0, 0, 0, 0, CF, AX88796, 0, 0,
	    0, 0, 0, 0, 0, 0, 0, 0 } },
};

static unsigned char irl2irq[HL_NR_IRL] __initdata = {
	0, IRQ_CF, 0, 0,
	0, 0, 0, 0,
	0, 0, IRQ_AX88796, 0,
	0, 0, 0,
};

static DECLARE_INTC_DESC(intc_desc, "r7785rp", vectors,
			 NULL, NULL, mask_registers, NULL, NULL);

unsigned char * __init highlander_init_irq_r7785rp(void)
{
	if ((ctrl_inw(0xa4000158) & 0xf000) != 0x1000)
		return NULL;

	printk(KERN_INFO "Using r7785rp interrupt controller.\n");

	ctrl_outw(0x0000, PA_IRLSSR1);	/* FPGA IRLSSR1(CF_CD clear) */

	/* Setup the FPGA IRL */
	ctrl_outw(0x0000, PA_IRLPRA);	/* FPGA IRLA */
	ctrl_outw(0xe598, PA_IRLPRB);	/* FPGA IRLB */
	ctrl_outw(0x7060, PA_IRLPRC);	/* FPGA IRLC */
	ctrl_outw(0x0000, PA_IRLPRD);	/* FPGA IRLD */
	ctrl_outw(0x4321, PA_IRLPRE);	/* FPGA IRLE */
	ctrl_outw(0x0000, PA_IRLPRF);	/* FPGA IRLF */

	register_intc_controller(&intc_desc);
	return irl2irq;
}
