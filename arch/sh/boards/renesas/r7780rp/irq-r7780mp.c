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
	CF,		/* Compact Flash */
	TP,		/* Touch panel */
	SCIF1,		/* FPGA SCIF1 */
	SCIF0,		/* FPGA SCIF0 */
	SMBUS,		/* SMBUS */
	RTC,		/* RTC Alarm */
	AX88796,	/* Ethernet controller */
	PSW,		/* Push Switch */

	/* external bus connector */
	EXT1, EXT2, EXT4, EXT5, EXT6,
};

static struct intc_vect vectors[] __initdata = {
	INTC_IRQ(CF, IRQ_CF),
	INTC_IRQ(TP, IRQ_TP),
	INTC_IRQ(SCIF1, IRQ_SCIF1),
	INTC_IRQ(SCIF0, IRQ_SCIF0),
	INTC_IRQ(SMBUS, IRQ_SMBUS),
	INTC_IRQ(RTC, IRQ_RTC),
	INTC_IRQ(AX88796, IRQ_AX88796),
	INTC_IRQ(PSW, IRQ_PSW),

	INTC_IRQ(EXT1, IRQ_EXT1), INTC_IRQ(EXT2, IRQ_EXT2),
	INTC_IRQ(EXT4, IRQ_EXT4), INTC_IRQ(EXT5, IRQ_EXT5),
	INTC_IRQ(EXT6, IRQ_EXT6),
};

static struct intc_mask_reg mask_registers[] __initdata = {
	{ 0xa4000000, 0, 16, /* IRLMSK */
	  { SCIF0, SCIF1, RTC, 0, CF, 0, TP, SMBUS,
	    0, EXT6, EXT5, EXT4, EXT2, EXT1, PSW, AX88796 } },
};

static unsigned char irl2irq[HL_NR_IRL] __initdata = {
	0, IRQ_CF, IRQ_TP, IRQ_SCIF1,
	IRQ_SCIF0, IRQ_SMBUS, IRQ_RTC, IRQ_EXT6,
	IRQ_EXT5, IRQ_EXT4, IRQ_EXT2, IRQ_EXT1,
	0, IRQ_AX88796, IRQ_PSW,
};

static DECLARE_INTC_DESC(intc_desc, "r7780mp", vectors,
			 NULL, mask_registers, NULL, NULL);

unsigned char * __init highlander_init_irq_r7780mp(void)
{
	if ((ctrl_inw(0xa4000700) & 0xf000) == 0x2000) {
		printk(KERN_INFO "Using r7780mp interrupt controller.\n");
		register_intc_controller(&intc_desc);
		return irl2irq;
	}

	return NULL;
}
