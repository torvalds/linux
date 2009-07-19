/*
 * arch/sh/kernel/cpu/sh4a/hwblk-sh7722.c
 *
 * SH7722 hardware block support
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <asm/suspend.h>
#include <asm/hwblk.h>
#include <cpu/sh7722.h>

/* SH7722 registers */
#define MSTPCR0		0xa4150030
#define MSTPCR1		0xa4150034
#define MSTPCR2		0xa4150038

/* SH7722 Power Domains */
enum { CORE_AREA, SUB_AREA, CORE_AREA_BM };
static struct hwblk_area sh7722_hwblk_area[] = {
	[CORE_AREA] = HWBLK_AREA(0, 0),
	[CORE_AREA_BM] = HWBLK_AREA(HWBLK_AREA_FLAG_PARENT, CORE_AREA),
	[SUB_AREA] = HWBLK_AREA(0, 0),
};

/* Table mapping HWBLK to Module Stop Bit and Power Domain */
static struct hwblk sh7722_hwblk[HWBLK_NR] = {
	[HWBLK_TLB] = HWBLK(MSTPCR0, 31, CORE_AREA),
	[HWBLK_IC] = HWBLK(MSTPCR0, 30, CORE_AREA),
	[HWBLK_OC] = HWBLK(MSTPCR0, 29, CORE_AREA),
	[HWBLK_URAM] = HWBLK(MSTPCR0, 28, CORE_AREA),
	[HWBLK_XYMEM] = HWBLK(MSTPCR0, 26, CORE_AREA),
	[HWBLK_INTC] = HWBLK(MSTPCR0, 22, CORE_AREA),
	[HWBLK_DMAC] = HWBLK(MSTPCR0, 21, CORE_AREA_BM),
	[HWBLK_SHYWAY] = HWBLK(MSTPCR0, 20, CORE_AREA),
	[HWBLK_HUDI] = HWBLK(MSTPCR0, 19, CORE_AREA),
	[HWBLK_UBC] = HWBLK(MSTPCR0, 17, CORE_AREA),
	[HWBLK_TMU] = HWBLK(MSTPCR0, 15, CORE_AREA),
	[HWBLK_CMT] = HWBLK(MSTPCR0, 14, SUB_AREA),
	[HWBLK_RWDT] = HWBLK(MSTPCR0, 13, SUB_AREA),
	[HWBLK_FLCTL] = HWBLK(MSTPCR0, 10, CORE_AREA),
	[HWBLK_SCIF0] = HWBLK(MSTPCR0, 7, CORE_AREA),
	[HWBLK_SCIF1] = HWBLK(MSTPCR0, 6, CORE_AREA),
	[HWBLK_SCIF2] = HWBLK(MSTPCR0, 5, CORE_AREA),
	[HWBLK_SIO] = HWBLK(MSTPCR0, 3, CORE_AREA),
	[HWBLK_SIOF0] = HWBLK(MSTPCR0, 2, CORE_AREA),
	[HWBLK_SIOF1] = HWBLK(MSTPCR0, 1, CORE_AREA),

	[HWBLK_IIC] = HWBLK(MSTPCR1, 9, CORE_AREA),
	[HWBLK_RTC] = HWBLK(MSTPCR1, 8, SUB_AREA),

	[HWBLK_TPU] = HWBLK(MSTPCR2, 25, CORE_AREA),
	[HWBLK_IRDA] = HWBLK(MSTPCR2, 24, CORE_AREA),
	[HWBLK_SDHI] = HWBLK(MSTPCR2, 18, CORE_AREA),
	[HWBLK_SIM] = HWBLK(MSTPCR2, 16, CORE_AREA),
	[HWBLK_KEYSC] = HWBLK(MSTPCR2, 14, SUB_AREA),
	[HWBLK_TSIF] = HWBLK(MSTPCR2, 13, SUB_AREA),
	[HWBLK_USBF] = HWBLK(MSTPCR2, 11, CORE_AREA),
	[HWBLK_2DG] = HWBLK(MSTPCR2, 9, CORE_AREA_BM),
	[HWBLK_SIU] = HWBLK(MSTPCR2, 8, CORE_AREA),
	[HWBLK_JPU] = HWBLK(MSTPCR2, 6, CORE_AREA_BM),
	[HWBLK_VOU] = HWBLK(MSTPCR2, 5, CORE_AREA_BM),
	[HWBLK_BEU] = HWBLK(MSTPCR2, 4, CORE_AREA_BM),
	[HWBLK_CEU] = HWBLK(MSTPCR2, 3, CORE_AREA_BM),
	[HWBLK_VEU] = HWBLK(MSTPCR2, 2, CORE_AREA_BM),
	[HWBLK_VPU] = HWBLK(MSTPCR2, 1, CORE_AREA_BM),
	[HWBLK_LCDC] = HWBLK(MSTPCR2, 0, CORE_AREA_BM),
};

static struct hwblk_info sh7722_hwblk_info = {
	.areas = sh7722_hwblk_area,
	.nr_areas = ARRAY_SIZE(sh7722_hwblk_area),
	.hwblks = sh7722_hwblk,
	.nr_hwblks = ARRAY_SIZE(sh7722_hwblk),
};

int arch_hwblk_sleep_mode(void)
{
	if (!sh7722_hwblk_area[CORE_AREA].cnt[HWBLK_CNT_USAGE])
		return SUSP_SH_STANDBY | SUSP_SH_SF;

	if (!sh7722_hwblk_area[CORE_AREA_BM].cnt[HWBLK_CNT_USAGE])
		return SUSP_SH_SLEEP | SUSP_SH_SF;

	return SUSP_SH_SLEEP;
}

int __init arch_hwblk_init(void)
{
	return hwblk_register(&sh7722_hwblk_info);
}
