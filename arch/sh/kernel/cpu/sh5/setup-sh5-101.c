/*
 * SH5-101 Setup
 *
 *  Copyright (C) 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>

void __init plat_irq_setup(void)
{
	/* do nothing - all IRL interrupts are handled by the board code */
}
