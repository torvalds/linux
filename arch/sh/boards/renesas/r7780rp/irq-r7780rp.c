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
	int i;

	for (i = 0; i < 15; i++)
		make_r7780rp_irq(i);
}
