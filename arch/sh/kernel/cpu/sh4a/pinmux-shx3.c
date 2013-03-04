/*
 * SH-X3 prototype CPU pinmux
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <cpu/pfc.h>

static int __init shx3_pinmux_setup(void)
{
	return sh_pfc_register("pfc-shx3", NULL, 0);
}
arch_initcall(shx3_pinmux_setup);
