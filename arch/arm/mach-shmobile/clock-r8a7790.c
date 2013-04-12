/*
 * r8a7790 clock framework support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <mach/clock.h>
#include <mach/common.h>

/*
 *   MD		EXTAL		PLL0	PLL1	PLL3
 * 14 13 19	(MHz)		*1	*1
 *---------------------------------------------------
 * 0  0  0	15 x 1		x172/2	x208/2	x106
 * 0  0  1	15 x 1		x172/2	x208/2	x88
 * 0  1  0	20 x 1		x130/2	x156/2	x80
 * 0  1  1	20 x 1		x130/2	x156/2	x66
 * 1  0  0	26 / 2		x200/2	x240/2	x122
 * 1  0  1	26 / 2		x200/2	x240/2	x102
 * 1  1  0	30 / 2		x172/2	x208/2	x106
 * 1  1  1	30 / 2		x172/2	x208/2	x88
 *
 * *1 :	Table 7.6 indicates VCO ouput (PLLx = VCO/2)
 *	see "p1 / 2" on R8A7790_CLOCK_ROOT() below
 */

#define MD(nr)	(1 << nr)

#define CPG_BASE 0xe6150000
#define CPG_LEN 0x1000

#define SMSTPCR2 0xe6150138
#define SMSTPCR7 0xe615014c

#define MODEMR		0xE6160060

static struct clk_mapping cpg_mapping = {
	.phys   = CPG_BASE,
	.len    = CPG_LEN,
};

static struct clk extal_clk = {
	/* .rate will be updated on r8a7790_clock_init() */
	.mapping	= &cpg_mapping,
};

static struct sh_clk_ops followparent_clk_ops = {
	.recalc	= followparent_recalc,
};

static struct clk main_clk = {
	/* .parent will be set r8a73a4_clock_init */
	.ops	= &followparent_clk_ops,
};

/*
 * clock ratio of these clock will be updated
 * on r8a7790_clock_init()
 */
SH_FIXED_RATIO_CLK_SET(pll1_clk,		main_clk,	1, 1);
SH_FIXED_RATIO_CLK_SET(pll3_clk,		main_clk,	1, 1);
SH_FIXED_RATIO_CLK_SET(lb_clk,			pll1_clk,	1, 1);
SH_FIXED_RATIO_CLK_SET(qspi_clk,		pll1_clk,	1, 1);

/* fixed ratio clock */
SH_FIXED_RATIO_CLK_SET(extal_div2_clk,		extal_clk,	1, 2);
SH_FIXED_RATIO_CLK_SET(cp_clk,			extal_clk,	1, 2);

SH_FIXED_RATIO_CLK_SET(pll1_div2_clk,		pll1_clk,	1, 2);
SH_FIXED_RATIO_CLK_SET(zg_clk,			pll1_clk,	1, 3);
SH_FIXED_RATIO_CLK_SET(zx_clk,			pll1_clk,	1, 3);
SH_FIXED_RATIO_CLK_SET(zs_clk,			pll1_clk,	1, 6);
SH_FIXED_RATIO_CLK_SET(hp_clk,			pll1_clk,	1, 12);
SH_FIXED_RATIO_CLK_SET(i_clk,			pll1_clk,	1, 2);
SH_FIXED_RATIO_CLK_SET(b_clk,			pll1_clk,	1, 12);
SH_FIXED_RATIO_CLK_SET(p_clk,			pll1_clk,	1, 24);
SH_FIXED_RATIO_CLK_SET(cl_clk,			pll1_clk,	1, 48);
SH_FIXED_RATIO_CLK_SET(m2_clk,			pll1_clk,	1, 8);
SH_FIXED_RATIO_CLK_SET(imp_clk,			pll1_clk,	1, 4);
SH_FIXED_RATIO_CLK_SET(rclk_clk,		pll1_clk,	1, (48 * 1024));
SH_FIXED_RATIO_CLK_SET(oscclk_clk,		pll1_clk,	1, (12 * 1024));

SH_FIXED_RATIO_CLK_SET(zb3_clk,			pll3_clk,	1, 4);
SH_FIXED_RATIO_CLK_SET(zb3d2_clk,		pll3_clk,	1, 8);
SH_FIXED_RATIO_CLK_SET(ddr_clk,			pll3_clk,	1, 8);
SH_FIXED_RATIO_CLK_SET(mp_clk,			pll1_div2_clk,	1, 15);

static struct clk *main_clks[] = {
	&extal_clk,
	&extal_div2_clk,
	&main_clk,
	&pll1_clk,
	&pll1_div2_clk,
	&pll3_clk,
	&lb_clk,
	&qspi_clk,
	&zg_clk,
	&zx_clk,
	&zs_clk,
	&hp_clk,
	&i_clk,
	&b_clk,
	&p_clk,
	&cl_clk,
	&m2_clk,
	&imp_clk,
	&rclk_clk,
	&oscclk_clk,
	&zb3_clk,
	&zb3d2_clk,
	&ddr_clk,
	&mp_clk,
	&cp_clk,
};

enum { MSTP721, MSTP720,
	MSTP216, MSTP207, MSTP206, MSTP204, MSTP203, MSTP202, MSTP_NR };
static struct clk mstp_clks[MSTP_NR] = {
	[MSTP721] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 21, 0), /* SCIF0 */
	[MSTP720] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 20, 0), /* SCIF1 */
	[MSTP216] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 16, 0), /* SCIFB2 */
	[MSTP207] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 7, 0), /* SCIFB1 */
	[MSTP206] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 6, 0), /* SCIFB0 */
	[MSTP204] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 4, 0), /* SCIFA0 */
	[MSTP203] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 3, 0), /* SCIFA1 */
	[MSTP202] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 2, 0), /* SCIFA2 */
};

static struct clk_lookup lookups[] = {

	/* main clocks */
	CLKDEV_CON_ID("extal",		&extal_clk),
	CLKDEV_CON_ID("extal_div2",	&extal_div2_clk),
	CLKDEV_CON_ID("main",		&main_clk),
	CLKDEV_CON_ID("pll1",		&pll1_clk),
	CLKDEV_CON_ID("pll1_div2",	&pll1_div2_clk),
	CLKDEV_CON_ID("pll3",		&pll3_clk),
	CLKDEV_CON_ID("zg",		&zg_clk),
	CLKDEV_CON_ID("zx",		&zx_clk),
	CLKDEV_CON_ID("zs",		&zs_clk),
	CLKDEV_CON_ID("hp",		&hp_clk),
	CLKDEV_CON_ID("i",		&i_clk),
	CLKDEV_CON_ID("b",		&b_clk),
	CLKDEV_CON_ID("lb",		&lb_clk),
	CLKDEV_CON_ID("p",		&p_clk),
	CLKDEV_CON_ID("cl",		&cl_clk),
	CLKDEV_CON_ID("m2",		&m2_clk),
	CLKDEV_CON_ID("imp",		&imp_clk),
	CLKDEV_CON_ID("rclk",		&rclk_clk),
	CLKDEV_CON_ID("oscclk",		&oscclk_clk),
	CLKDEV_CON_ID("zb3",		&zb3_clk),
	CLKDEV_CON_ID("zb3d2",		&zb3d2_clk),
	CLKDEV_CON_ID("ddr",		&ddr_clk),
	CLKDEV_CON_ID("mp",		&mp_clk),
	CLKDEV_CON_ID("qspi",		&qspi_clk),
	CLKDEV_CON_ID("cp",		&cp_clk),

	/* MSTP */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]),
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]),
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP206]),
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP207]),
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP216]),
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP202]),
	CLKDEV_DEV_ID("sh-sci.6", &mstp_clks[MSTP721]),
	CLKDEV_DEV_ID("sh-sci.7", &mstp_clks[MSTP720]),
};

#define R8A7790_CLOCK_ROOT(e, m, p0, p1, p30, p31)		\
	extal_clk.rate	= e * 1000 * 1000;			\
	main_clk.parent	= m;					\
	SH_CLK_SET_RATIO(&pll1_clk_ratio, p1 / 2, 1);		\
	if (mode & MD(19))					\
		SH_CLK_SET_RATIO(&pll3_clk_ratio, p31, 1);	\
	else							\
		SH_CLK_SET_RATIO(&pll3_clk_ratio, p30, 1)


void __init r8a7790_clock_init(void)
{
	void __iomem *modemr = ioremap_nocache(MODEMR, PAGE_SIZE);
	u32 mode;
	int k, ret = 0;

	BUG_ON(!modemr);
	mode = ioread32(modemr);
	iounmap(modemr);

	switch (mode & (MD(14) | MD(13))) {
	case 0:
		R8A7790_CLOCK_ROOT(15, &extal_clk, 172, 208, 106, 88);
		break;
	case MD(13):
		R8A7790_CLOCK_ROOT(20, &extal_clk, 130, 156, 80, 66);
		break;
	case MD(14):
		R8A7790_CLOCK_ROOT(26, &extal_div2_clk, 200, 240, 122, 102);
		break;
	case MD(13) | MD(14):
		R8A7790_CLOCK_ROOT(30, &extal_div2_clk, 172, 208, 106, 88);
		break;
	}

	if (mode & (MD(18)))
		SH_CLK_SET_RATIO(&lb_clk_ratio, 1, 36);
	else
		SH_CLK_SET_RATIO(&lb_clk_ratio, 1, 24);

	if ((mode & (MD(3) | MD(2) | MD(1))) == MD(2))
		SH_CLK_SET_RATIO(&qspi_clk_ratio, 1, 16);
	else
		SH_CLK_SET_RATIO(&qspi_clk_ratio, 1, 20);

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup r8a7790 clocks\n");
}
