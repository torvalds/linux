/*
 * SH7264 Pinmux
 *
 *  Copyright (C) 2012  Renesas Electronics Europe Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <cpu/pfc.h>

static int __init plat_pinmux_setup(void)
{
	return sh_pfc_register("pfc-sh7264", NULL, 0);
}
arch_initcall(plat_pinmux_setup);
