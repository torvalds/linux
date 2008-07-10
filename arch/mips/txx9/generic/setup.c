/*
 * linux/arch/mips/txx9/generic/setup.c
 *
 * Based on linux/arch/mips/txx9/rbtx4938/setup.c,
 *	    and RBTX49xx patch from CELF patch archive.
 *
 * 2003-2005 (c) MontaVista Software, Inc.
 * (C) Copyright TOSHIBA CORPORATION 2000-2001, 2004-2007
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/txx9/generic.h>

/* EBUSC settings of TX4927, etc. */
struct resource txx9_ce_res[8];
static char txx9_ce_res_name[8][4];	/* "CEn" */

/* pcode, internal register */
char txx9_pcode_str[8];
static struct resource txx9_reg_res = {
	.name = txx9_pcode_str,
	.flags = IORESOURCE_MEM,
};
void __init
txx9_reg_res_init(unsigned int pcode, unsigned long base, unsigned long size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(txx9_ce_res); i++) {
		sprintf(txx9_ce_res_name[i], "CE%d", i);
		txx9_ce_res[i].flags = IORESOURCE_MEM;
		txx9_ce_res[i].name = txx9_ce_res_name[i];
	}

	sprintf(txx9_pcode_str, "TX%x", pcode);
	if (base) {
		txx9_reg_res.start = base & 0xfffffffffULL;
		txx9_reg_res.end = (base & 0xfffffffffULL) + (size - 1);
		request_resource(&iomem_resource, &txx9_reg_res);
	}
}

/* clocks */
unsigned int txx9_master_clock;
unsigned int txx9_cpu_clock;
unsigned int txx9_gbus_clock;
