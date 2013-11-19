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

#define CPG_BASE 0xe6150000
#define CPG_LEN 0x1000

#define SMSTPCR1 0xe6150134
#define SMSTPCR2 0xe6150138
#define SMSTPCR3 0xe615013c
#define SMSTPCR5 0xe6150144
#define SMSTPCR7 0xe615014c
#define SMSTPCR8 0xe6150990
#define SMSTPCR9 0xe6150994

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
	MSTP931, MSTP930, MSTP929, MSTP928,
	MSTP917,
	MSTP813,
	MSTP726, MSTP725, MSTP724, MSTP723, MSTP722, MSTP721, MSTP720,
	MSTP717, MSTP716,
	MSTP704,
	MSTP522,
	MSTP315, MSTP314, MSTP313, MSTP312, MSTP311, MSTP305, MSTP304,
	MSTP216, MSTP207, MSTP206, MSTP204, MSTP203, MSTP202,
	MSTP124,
	MSTP_NR
};

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP931] = SH_CLK_MSTP32(&p_clk, SMSTPCR9, 31, 0), /* I2C0 */
	[MSTP930] = SH_CLK_MSTP32(&p_clk, SMSTPCR9, 30, 0), /* I2C1 */
	[MSTP929] = SH_CLK_MSTP32(&p_clk, SMSTPCR9, 29, 0), /* I2C2 */
	[MSTP928] = SH_CLK_MSTP32(&p_clk, SMSTPCR9, 28, 0), /* I2C3 */
	[MSTP917] = SH_CLK_MSTP32(&qspi_clk, SMSTPCR9, 17, 0), /* QSPI */
	[MSTP813] = SH_CLK_MSTP32(&p_clk, SMSTPCR8, 13, 0), /* Ether */
	[MSTP726] = SH_CLK_MSTP32(&zx_clk, SMSTPCR7, 26, 0), /* LVDS0 */
	[MSTP725] = SH_CLK_MSTP32(&zx_clk, SMSTPCR7, 25, 0), /* LVDS1 */
	[MSTP724] = SH_CLK_MSTP32(&zx_clk, SMSTPCR7, 24, 0), /* DU0 */
	[MSTP723] = SH_CLK_MSTP32(&zx_clk, SMSTPCR7, 23, 0), /* DU1 */
	[MSTP722] = SH_CLK_MSTP32(&zx_clk, SMSTPCR7, 22, 0), /* DU2 */
	[MSTP721] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 21, 0), /* SCIF0 */
	[MSTP720] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 20, 0), /* SCIF1 */
	[MSTP717] = SH_CLK_MSTP32(&zs_clk, SMSTPCR7, 17, 0), /* HSCIF0 */
	[MSTP716] = SH_CLK_MSTP32(&zs_clk, SMSTPCR7, 16, 0), /* HSCIF1 */
	[MSTP704] = SH_CLK_MSTP32(&mp_clk, SMSTPCR7, 4, 0), /* HSUSB */
	[MSTP522] = SH_CLK_MSTP32(&extal_clk, SMSTPCR5, 22, 0), /* Thermal */
	[MSTP315] = SH_CLK_MSTP32(&div6_clks[DIV6_MMC0], SMSTPCR3, 15, 0), /* MMC0 */
	[MSTP314] = SH_CLK_MSTP32(&div4_clks[DIV4_SD0], SMSTPCR3, 14, 0), /* SDHI0 */
	[MSTP313] = SH_CLK_MSTP32(&div4_clks[DIV4_SD1], SMSTPCR3, 13, 0), /* SDHI1 */
	[MSTP312] = SH_CLK_MSTP32(&div6_clks[DIV6_SD2], SMSTPCR3, 12, 0), /* SDHI2 */
	[MSTP311] = SH_CLK_MSTP32(&div6_clks[DIV6_SD3], SMSTPCR3, 11, 0), /* SDHI3 */
	[MSTP305] = SH_CLK_MSTP32(&div6_clks[DIV6_MMC1], SMSTPCR3, 5, 0), /* MMC1 */
	[MSTP304] = SH_CLK_MSTP32(&cp_clk, SMSTPCR3, 4, 0), /* TPU0 */
	[MSTP216] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 16, 0), /* SCIFB2 */
	[MSTP207] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 7, 0), /* SCIFB1 */
	[MSTP206] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 6, 0), /* SCIFB0 */
	[MSTP204] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 4, 0), /* SCIFA0 */
	[MSTP203] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 3, 0), /* SCIFA1 */
	[MSTP202] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 2, 0), /* SCIFA2 */
	[MSTP124] = SH_CLK_MSTP32(&rclk_clk, SMSTPCR1, 24, 0), /* CMT0 */
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
	CLKDEV_DEV_ID("e6508000.i2c", &mstp_clks[MSTP931]),
	CLKDEV_DEV_ID("e6518000.i2c", &mstp_clks[MSTP930]),
	CLKDEV_DEV_ID("e6530000.i2c", &mstp_clks[MSTP929]),
	CLKDEV_DEV_ID("e6540000.i2c", &mstp_clks[MSTP928]),
	CLKDEV_DEV_ID("r8a7790-ether", &mstp_clks[MSTP813]),
	CLKDEV_DEV_ID("e61f0000.thermal", &mstp_clks[MSTP522]),
	CLKDEV_DEV_ID("rcar_thermal", &mstp_clks[MSTP522]),
	CLKDEV_DEV_ID("ee200000.mmcif", &mstp_clks[MSTP315]),
	CLKDEV_DEV_ID("sh_mmcif.0", &mstp_clks[MSTP315]),
	CLKDEV_DEV_ID("ee100000.sdhi", &mstp_clks[MSTP314]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP314]),
	CLKDEV_DEV_ID("ee120000.sdhi", &mstp_clks[MSTP313]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP313]),
	CLKDEV_DEV_ID("ee140000.sdhi", &mstp_clks[MSTP312]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.2", &mstp_clks[MSTP312]),
	CLKDEV_DEV_ID("ee160000.sdhi", &mstp_clks[MSTP311]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.3", &mstp_clks[MSTP311]),
	CLKDEV_DEV_ID("ee220000.mmcif", &mstp_clks[MSTP305]),
	CLKDEV_DEV_ID("sh_mmcif.1", &mstp_clks[MSTP305]),
	CLKDEV_DEV_ID("sh_cmt.0", &mstp_clks[MSTP124]),
	CLKDEV_DEV_ID("qspi.0", &mstp_clks[MSTP917]),
	CLKDEV_DEV_ID("renesas_usbhs", &mstp_clks[MSTP704]),

	/* ICK */
	CLKDEV_ICK_ID("usbhs", "usb_phy_rcar_gen2", &mstp_clks[MSTP704]),
	CLKDEV_ICK_ID("lvds.0", "rcar-du-r8a7790", &mstp_clks[MSTP726]),
	CLKDEV_ICK_ID("lvds.1", "rcar-du-r8a7790", &mstp_clks[MSTP725]),
	CLKDEV_ICK_ID("du.0", "rcar-du-r8a7790", &mstp_clks[MSTP724]),
	CLKDEV_ICK_ID("du.1", "rcar-du-r8a7790", &mstp_clks[MSTP723]),
	CLKDEV_ICK_ID("du.2", "rcar-du-r8a7790", &mstp_clks[MSTP722]),

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
