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
#include <mach/r8a7790.h>

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

#define CPG_BASE	0xe6150000
#define CPG_LEN		0x1000

#define SMSTPCR1	0xe6150134
#define SMSTPCR2	0xe6150138
#define SMSTPCR3	0xe615013c
#define SMSTPCR5	0xe6150144
#define SMSTPCR7	0xe615014c
#define SMSTPCR8	0xe6150990
#define SMSTPCR9	0xe6150994
#define SMSTPCR10	0xe6150998

#define MSTPSR1		IOMEM(0xe6150038)
#define MSTPSR2		IOMEM(0xe6150040)
#define MSTPSR3		IOMEM(0xe6150048)
#define MSTPSR5		IOMEM(0xe615003c)
#define MSTPSR7		IOMEM(0xe61501c4)
#define MSTPSR8		IOMEM(0xe61509a0)
#define MSTPSR9		IOMEM(0xe61509a4)
#define MSTPSR10	IOMEM(0xe61509a8)

#define SDCKCR		0xE6150074
#define SD2CKCR		0xE6150078
#define SD3CKCR		0xE615007C
#define MMC0CKCR	0xE6150240
#define MMC1CKCR	0xE6150244
#define SSPCKCR		0xE6150248
#define SSPRSCKCR	0xE615024C

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
	/* .parent will be set r8a7790_clock_init */
	.ops	= &followparent_clk_ops,
};

static struct clk audio_clk_a = {
};

static struct clk audio_clk_b = {
};

static struct clk audio_clk_c = {
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
	&audio_clk_a,
	&audio_clk_b,
	&audio_clk_c,
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

/* SDHI (DIV4) clock */
static int divisors[] = { 2, 3, 4, 6, 8, 12, 16, 18, 24, 0, 36, 48, 10 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum {
	DIV4_SDH, DIV4_SD0, DIV4_SD1, DIV4_NR
};

static struct clk div4_clks[DIV4_NR] = {
	[DIV4_SDH] = SH_CLK_DIV4(&pll1_clk, SDCKCR, 8, 0x0dff, CLK_ENABLE_ON_INIT),
	[DIV4_SD0] = SH_CLK_DIV4(&pll1_clk, SDCKCR, 4, 0x1de0, CLK_ENABLE_ON_INIT),
	[DIV4_SD1] = SH_CLK_DIV4(&pll1_clk, SDCKCR, 0, 0x1de0, CLK_ENABLE_ON_INIT),
};

/* DIV6 clocks */
enum {
	DIV6_SD2, DIV6_SD3,
	DIV6_MMC0, DIV6_MMC1,
	DIV6_SSP, DIV6_SSPRS,
	DIV6_NR
};

static struct clk div6_clks[DIV6_NR] = {
	[DIV6_SD2]	= SH_CLK_DIV6(&pll1_div2_clk, SD2CKCR, 0),
	[DIV6_SD3]	= SH_CLK_DIV6(&pll1_div2_clk, SD3CKCR, 0),
	[DIV6_MMC0]	= SH_CLK_DIV6(&pll1_div2_clk, MMC0CKCR, 0),
	[DIV6_MMC1]	= SH_CLK_DIV6(&pll1_div2_clk, MMC1CKCR, 0),
	[DIV6_SSP]	= SH_CLK_DIV6(&pll1_div2_clk, SSPCKCR, 0),
	[DIV6_SSPRS]	= SH_CLK_DIV6(&pll1_div2_clk, SSPRSCKCR, 0),
};

/* MSTP */
enum {
	MSTP1017, /* parent of SCU */

	MSTP1031, MSTP1030,
	MSTP1029, MSTP1028, MSTP1027, MSTP1026, MSTP1025, MSTP1024, MSTP1023, MSTP1022,
	MSTP1015, MSTP1014, MSTP1013, MSTP1012, MSTP1011, MSTP1010,
	MSTP1009, MSTP1008, MSTP1007, MSTP1006, MSTP1005,
	MSTP931, MSTP930, MSTP929, MSTP928,
	MSTP917,
	MSTP815, MSTP814,
	MSTP813,
	MSTP811, MSTP810, MSTP809, MSTP808,
	MSTP726, MSTP725, MSTP724, MSTP723, MSTP722, MSTP721, MSTP720,
	MSTP717, MSTP716,
	MSTP704, MSTP703,
	MSTP522,
	MSTP502, MSTP501,
	MSTP315, MSTP314, MSTP313, MSTP312, MSTP311, MSTP305, MSTP304,
	MSTP216, MSTP207, MSTP206, MSTP204, MSTP203, MSTP202,
	MSTP124,
	MSTP_NR
};

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP1031] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 31, MSTPSR10, 0), /* SCU0 */
	[MSTP1030] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 30, MSTPSR10, 0), /* SCU1 */
	[MSTP1029] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 29, MSTPSR10, 0), /* SCU2 */
	[MSTP1028] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 28, MSTPSR10, 0), /* SCU3 */
	[MSTP1027] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 27, MSTPSR10, 0), /* SCU4 */
	[MSTP1026] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 26, MSTPSR10, 0), /* SCU5 */
	[MSTP1025] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 25, MSTPSR10, 0), /* SCU6 */
	[MSTP1024] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 24, MSTPSR10, 0), /* SCU7 */
	[MSTP1023] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 23, MSTPSR10, 0), /* SCU8 */
	[MSTP1022] = SH_CLK_MSTP32_STS(&mstp_clks[MSTP1017], SMSTPCR10, 22, MSTPSR10, 0), /* SCU9 */
	[MSTP1017] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 17, MSTPSR10, 0), /* SCU */
	[MSTP1015] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 15, MSTPSR10, 0), /* SSI0 */
	[MSTP1014] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 14, MSTPSR10, 0), /* SSI1 */
	[MSTP1013] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 13, MSTPSR10, 0), /* SSI2 */
	[MSTP1012] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 12, MSTPSR10, 0), /* SSI3 */
	[MSTP1011] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 11, MSTPSR10, 0), /* SSI4 */
	[MSTP1010] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 10, MSTPSR10, 0), /* SSI5 */
	[MSTP1009] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 9, MSTPSR10, 0), /* SSI6 */
	[MSTP1008] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 8, MSTPSR10, 0), /* SSI7 */
	[MSTP1007] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 7, MSTPSR10, 0), /* SSI8 */
	[MSTP1006] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 6, MSTPSR10, 0), /* SSI9 */
	[MSTP1005] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR10, 5, MSTPSR10, 0), /* SSI ALL */
	[MSTP931] = SH_CLK_MSTP32_STS(&hp_clk, SMSTPCR9, 31, MSTPSR9, 0), /* I2C0 */
	[MSTP930] = SH_CLK_MSTP32_STS(&hp_clk, SMSTPCR9, 30, MSTPSR9, 0), /* I2C1 */
	[MSTP929] = SH_CLK_MSTP32_STS(&hp_clk, SMSTPCR9, 29, MSTPSR9, 0), /* I2C2 */
	[MSTP928] = SH_CLK_MSTP32_STS(&hp_clk, SMSTPCR9, 28, MSTPSR9, 0), /* I2C3 */
	[MSTP917] = SH_CLK_MSTP32_STS(&qspi_clk, SMSTPCR9, 17, MSTPSR9, 0), /* QSPI */
	[MSTP815] = SH_CLK_MSTP32_STS(&zs_clk, SMSTPCR8, 15, MSTPSR8, 0), /* SATA0 */
	[MSTP814] = SH_CLK_MSTP32_STS(&zs_clk, SMSTPCR8, 14, MSTPSR8, 0), /* SATA1 */
	[MSTP813] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR8, 13, MSTPSR8, 0), /* Ether */
	[MSTP811] = SH_CLK_MSTP32_STS(&zg_clk, SMSTPCR8, 11, MSTPSR8, 0), /* VIN0 */
	[MSTP810] = SH_CLK_MSTP32_STS(&zg_clk, SMSTPCR8, 10, MSTPSR8, 0), /* VIN1 */
	[MSTP809] = SH_CLK_MSTP32_STS(&zg_clk, SMSTPCR8,  9, MSTPSR8, 0), /* VIN2 */
	[MSTP808] = SH_CLK_MSTP32_STS(&zg_clk, SMSTPCR8,  8, MSTPSR8, 0), /* VIN3 */
	[MSTP726] = SH_CLK_MSTP32_STS(&zx_clk, SMSTPCR7, 26, MSTPSR7, 0), /* LVDS0 */
	[MSTP725] = SH_CLK_MSTP32_STS(&zx_clk, SMSTPCR7, 25, MSTPSR7, 0), /* LVDS1 */
	[MSTP724] = SH_CLK_MSTP32_STS(&zx_clk, SMSTPCR7, 24, MSTPSR7, 0), /* DU0 */
	[MSTP723] = SH_CLK_MSTP32_STS(&zx_clk, SMSTPCR7, 23, MSTPSR7, 0), /* DU1 */
	[MSTP722] = SH_CLK_MSTP32_STS(&zx_clk, SMSTPCR7, 22, MSTPSR7, 0), /* DU2 */
	[MSTP721] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR7, 21, MSTPSR7, 0), /* SCIF0 */
	[MSTP720] = SH_CLK_MSTP32_STS(&p_clk, SMSTPCR7, 20, MSTPSR7, 0), /* SCIF1 */
	[MSTP717] = SH_CLK_MSTP32_STS(&zs_clk, SMSTPCR7, 17, MSTPSR7, 0), /* HSCIF0 */
	[MSTP716] = SH_CLK_MSTP32_STS(&zs_clk, SMSTPCR7, 16, MSTPSR7, 0), /* HSCIF1 */
	[MSTP704] = SH_CLK_MSTP32_STS(&mp_clk, SMSTPCR7, 4, MSTPSR7, 0), /* HSUSB */
	[MSTP703] = SH_CLK_MSTP32_STS(&mp_clk, SMSTPCR7, 3, MSTPSR7, 0), /* EHCI */
	[MSTP522] = SH_CLK_MSTP32_STS(&extal_clk, SMSTPCR5, 22, MSTPSR5, 0), /* Thermal */
	[MSTP502] = SH_CLK_MSTP32_STS(&zs_clk, SMSTPCR5, 2, MSTPSR5, 0), /* Audio-DMAC low */
	[MSTP501] = SH_CLK_MSTP32_STS(&zs_clk, SMSTPCR5, 1, MSTPSR5, 0), /* Audio-DMAC hi */
	[MSTP315] = SH_CLK_MSTP32_STS(&div6_clks[DIV6_MMC0], SMSTPCR3, 15, MSTPSR3, 0), /* MMC0 */
	[MSTP314] = SH_CLK_MSTP32_STS(&div4_clks[DIV4_SD0], SMSTPCR3, 14, MSTPSR3, 0), /* SDHI0 */
	[MSTP313] = SH_CLK_MSTP32_STS(&div4_clks[DIV4_SD1], SMSTPCR3, 13, MSTPSR3, 0), /* SDHI1 */
	[MSTP312] = SH_CLK_MSTP32_STS(&div6_clks[DIV6_SD2], SMSTPCR3, 12, MSTPSR3, 0), /* SDHI2 */
	[MSTP311] = SH_CLK_MSTP32_STS(&div6_clks[DIV6_SD3], SMSTPCR3, 11, MSTPSR3, 0), /* SDHI3 */
	[MSTP305] = SH_CLK_MSTP32_STS(&div6_clks[DIV6_MMC1], SMSTPCR3, 5, MSTPSR3, 0), /* MMC1 */
	[MSTP304] = SH_CLK_MSTP32_STS(&cp_clk, SMSTPCR3, 4, MSTPSR3, 0), /* TPU0 */
	[MSTP216] = SH_CLK_MSTP32_STS(&mp_clk, SMSTPCR2, 16, MSTPSR2, 0), /* SCIFB2 */
	[MSTP207] = SH_CLK_MSTP32_STS(&mp_clk, SMSTPCR2, 7, MSTPSR2, 0), /* SCIFB1 */
	[MSTP206] = SH_CLK_MSTP32_STS(&mp_clk, SMSTPCR2, 6, MSTPSR2, 0), /* SCIFB0 */
	[MSTP204] = SH_CLK_MSTP32_STS(&mp_clk, SMSTPCR2, 4, MSTPSR2, 0), /* SCIFA0 */
	[MSTP203] = SH_CLK_MSTP32_STS(&mp_clk, SMSTPCR2, 3, MSTPSR2, 0), /* SCIFA1 */
	[MSTP202] = SH_CLK_MSTP32_STS(&mp_clk, SMSTPCR2, 2, MSTPSR2, 0), /* SCIFA2 */
	[MSTP124] = SH_CLK_MSTP32_STS(&rclk_clk, SMSTPCR1, 24, MSTPSR1, 0), /* CMT0 */
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

	/* DIV4 */
	CLKDEV_CON_ID("sdh",		&div4_clks[DIV4_SDH]),

	/* DIV6 */
	CLKDEV_CON_ID("ssp",		&div6_clks[DIV6_SSP]),
	CLKDEV_CON_ID("ssprs",		&div6_clks[DIV6_SSPRS]),

	/* MSTP */
	CLKDEV_DEV_ID("rcar_sound", &mstp_clks[MSTP1005]),
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]),
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]),
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP206]),
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP207]),
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP216]),
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP202]),
	CLKDEV_DEV_ID("sh-sci.6", &mstp_clks[MSTP721]),
	CLKDEV_DEV_ID("sh-sci.7", &mstp_clks[MSTP720]),
	CLKDEV_DEV_ID("sh-sci.8", &mstp_clks[MSTP717]),
	CLKDEV_DEV_ID("sh-sci.9", &mstp_clks[MSTP716]),
	CLKDEV_DEV_ID("i2c-rcar_gen2.0", &mstp_clks[MSTP931]),
	CLKDEV_DEV_ID("i2c-rcar_gen2.1", &mstp_clks[MSTP930]),
	CLKDEV_DEV_ID("i2c-rcar_gen2.2", &mstp_clks[MSTP929]),
	CLKDEV_DEV_ID("i2c-rcar_gen2.3", &mstp_clks[MSTP928]),
	CLKDEV_DEV_ID("r8a7790-ether", &mstp_clks[MSTP813]),
	CLKDEV_DEV_ID("r8a7790-vin.0", &mstp_clks[MSTP811]),
	CLKDEV_DEV_ID("r8a7790-vin.1", &mstp_clks[MSTP810]),
	CLKDEV_DEV_ID("r8a7790-vin.2", &mstp_clks[MSTP809]),
	CLKDEV_DEV_ID("r8a7790-vin.3", &mstp_clks[MSTP808]),
	CLKDEV_DEV_ID("rcar_thermal", &mstp_clks[MSTP522]),
	CLKDEV_DEV_ID("sh-dma-engine.0", &mstp_clks[MSTP502]),
	CLKDEV_DEV_ID("sh-dma-engine.1", &mstp_clks[MSTP501]),
	CLKDEV_DEV_ID("sh_mmcif.0", &mstp_clks[MSTP315]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP314]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP313]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.2", &mstp_clks[MSTP312]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.3", &mstp_clks[MSTP311]),
	CLKDEV_DEV_ID("sh_mmcif.1", &mstp_clks[MSTP305]),
	CLKDEV_DEV_ID("qspi.0", &mstp_clks[MSTP917]),
	CLKDEV_DEV_ID("renesas_usbhs", &mstp_clks[MSTP704]),
	CLKDEV_DEV_ID("pci-rcar-gen2.0", &mstp_clks[MSTP703]),
	CLKDEV_DEV_ID("pci-rcar-gen2.1", &mstp_clks[MSTP703]),
	CLKDEV_DEV_ID("pci-rcar-gen2.2", &mstp_clks[MSTP703]),
	CLKDEV_DEV_ID("sata-r8a7790.0", &mstp_clks[MSTP815]),
	CLKDEV_DEV_ID("sata-r8a7790.1", &mstp_clks[MSTP814]),

	/* ICK */
	CLKDEV_ICK_ID("fck", "sh-cmt-48-gen2.0", &mstp_clks[MSTP124]),
	CLKDEV_ICK_ID("usbhs", "usb_phy_rcar_gen2", &mstp_clks[MSTP704]),
	CLKDEV_ICK_ID("lvds.0", "rcar-du-r8a7790", &mstp_clks[MSTP726]),
	CLKDEV_ICK_ID("lvds.1", "rcar-du-r8a7790", &mstp_clks[MSTP725]),
	CLKDEV_ICK_ID("du.0", "rcar-du-r8a7790", &mstp_clks[MSTP724]),
	CLKDEV_ICK_ID("du.1", "rcar-du-r8a7790", &mstp_clks[MSTP723]),
	CLKDEV_ICK_ID("du.2", "rcar-du-r8a7790", &mstp_clks[MSTP722]),
	CLKDEV_ICK_ID("clk_a", "rcar_sound", &audio_clk_a),
	CLKDEV_ICK_ID("clk_b", "rcar_sound", &audio_clk_b),
	CLKDEV_ICK_ID("clk_c", "rcar_sound", &audio_clk_c),
	CLKDEV_ICK_ID("clk_i", "rcar_sound", &m2_clk),
	CLKDEV_ICK_ID("src.0", "rcar_sound", &mstp_clks[MSTP1031]),
	CLKDEV_ICK_ID("src.1", "rcar_sound", &mstp_clks[MSTP1030]),
	CLKDEV_ICK_ID("src.2", "rcar_sound", &mstp_clks[MSTP1029]),
	CLKDEV_ICK_ID("src.3", "rcar_sound", &mstp_clks[MSTP1028]),
	CLKDEV_ICK_ID("src.4", "rcar_sound", &mstp_clks[MSTP1027]),
	CLKDEV_ICK_ID("src.5", "rcar_sound", &mstp_clks[MSTP1026]),
	CLKDEV_ICK_ID("src.6", "rcar_sound", &mstp_clks[MSTP1025]),
	CLKDEV_ICK_ID("src.7", "rcar_sound", &mstp_clks[MSTP1024]),
	CLKDEV_ICK_ID("src.8", "rcar_sound", &mstp_clks[MSTP1023]),
	CLKDEV_ICK_ID("src.9", "rcar_sound", &mstp_clks[MSTP1022]),
	CLKDEV_ICK_ID("ssi.0", "rcar_sound", &mstp_clks[MSTP1015]),
	CLKDEV_ICK_ID("ssi.1", "rcar_sound", &mstp_clks[MSTP1014]),
	CLKDEV_ICK_ID("ssi.2", "rcar_sound", &mstp_clks[MSTP1013]),
	CLKDEV_ICK_ID("ssi.3", "rcar_sound", &mstp_clks[MSTP1012]),
	CLKDEV_ICK_ID("ssi.4", "rcar_sound", &mstp_clks[MSTP1011]),
	CLKDEV_ICK_ID("ssi.5", "rcar_sound", &mstp_clks[MSTP1010]),
	CLKDEV_ICK_ID("ssi.6", "rcar_sound", &mstp_clks[MSTP1009]),
	CLKDEV_ICK_ID("ssi.7", "rcar_sound", &mstp_clks[MSTP1008]),
	CLKDEV_ICK_ID("ssi.8", "rcar_sound", &mstp_clks[MSTP1007]),
	CLKDEV_ICK_ID("ssi.9", "rcar_sound", &mstp_clks[MSTP1006]),

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
	u32 mode = rcar_gen2_read_mode_pins();
	int k, ret = 0;

	switch (mode & (MD(14) | MD(13))) {
	case 0:
		R8A7790_CLOCK_ROOT(15, &extal_clk, 172, 208, 106, 88);
		break;
	case MD(13):
		R8A7790_CLOCK_ROOT(20, &extal_clk, 130, 156, 80, 66);
		break;
	case MD(14):
		R8A7790_CLOCK_ROOT(26 / 2, &extal_div2_clk, 200, 240, 122, 102);
		break;
	case MD(13) | MD(14):
		R8A7790_CLOCK_ROOT(30 / 2, &extal_div2_clk, 172, 208, 106, 88);
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
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup r8a7790 clocks\n");
}
