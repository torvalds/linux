/*
 * Renesas Solutions Highlander R7780MP Support.
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
	PSW,              /* Push Switch */
	EXT1,             /* EXT1n IRQ */
	EXT4,             /* EXT4n IRQ */
};

static struct intc_vect vectors[] __initdata = {
	INTC_IRQ(CF, IRQ_CF),
	INTC_IRQ(PSW, IRQ_PSW),
	INTC_IRQ(AX88796, IRQ_AX88796),
	INTC_IRQ(EXT1, IRQ_EXT1),
	INTC_IRQ(EXT4, IRQ_EXT4),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xa4000000, 0, 16, /* IRLMSK */
	  { 0, 0, 0, 0, CF, 0, 0, 0,
	    0, 0, 0, EXT4, 0, EXT1, PSW, AX88796 } },
};

static unsigned char irl2irq[HL_NR_IRL] __initdata = {
	0, IRQ_CF, 0, 0,
	0, 0, 0, 0,
	0, IRQ_EXT4, 0, IRQ_EXT1,
	0, IRQ_AX88796, IRQ_PSW,
};

static DECLARE_INTC_DESC(intc_desc, "r7780mp", vectors,
			 NULL, NULL, mask_registers, NULL, NULL);

unsigned char * __init highlander_init_irq_r7780mp(void)
{
	if ((ctrl_inw(0xa4000700) & 0xf000) == 0x2000) {
		printk(KERN_INFO "Using r7780mp interrupt controller.\n");
		register_intc_controller(&intc_desc);
		return irl2irq;
	}

	return NULL;
}
