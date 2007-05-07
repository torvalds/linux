/*
 * Renesas Solutions Highlander R7780RP-1 Support.
 *
 * Copyright (C) 2002  Atom Create Engineering Co., Ltd.
 * Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <asm/io.h>
#include <asm/r7780rp.h>

void __init highlander_init_irq(void)
{
	ctrl_outw(0x0000, PA_IRLSSR1);	/* FPGA IRLSSR1(CF_CD clear) */

	/* Setup the FPGA IRL */
	ctrl_outw(0x0000, PA_IRLPRA);	/* FPGA IRLA */
	ctrl_outw(0xe598, PA_IRLPRB);	/* FPGA IRLB */
	ctrl_outw(0x7060, PA_IRLPRC);	/* FPGA IRLC */
	ctrl_outw(0x0000, PA_IRLPRD);	/* FPGA IRLD */
	ctrl_outw(0x4321, PA_IRLPRE);	/* FPGA IRLE */
	ctrl_outw(0x0000, PA_IRLPRF);	/* FPGA IRLF */

	make_r7780rp_irq(1);	/* CF card */
	make_r7780rp_irq(10);	/* On-board ethernet */
}
