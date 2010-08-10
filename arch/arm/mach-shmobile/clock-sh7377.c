/*
 * SH7377 clock framework support
 *
 * Copyright (C) 2010 Magnus Damm
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
#include <linux/sh_clk.h>
#include <mach/common.h>
#include <asm/clkdev.h>

/* SH7377 registers */
#define RTFRQCR    0xe6150000
#define SYFRQCR    0xe6150004
#define CMFRQCR    0xe61500E0
#define VCLKCR1    0xe6150008
#define VCLKCR2    0xe615000C
#define VCLKCR3    0xe615001C
#define FMSICKCR   0xe6150010
#define FMSOCKCR   0xe6150014
#define FSICKCR    0xe6150018
#define PLLC1CR    0xe6150028
#define PLLC2CR    0xe615002C
#define SUBUSBCKCR 0xe6150080
#define SPUCKCR    0xe6150084
#define MSUCKCR    0xe6150088
#define MVI3CKCR   0xe6150090
#define HDMICKCR   0xe6150094
#define MFCK1CR    0xe6150098
#define MFCK2CR    0xe615009C
#define DSITCKCR   0xe6150060
#define DSIPCKCR   0xe6150064
#define SMSTPCR0   0xe6150130
#define SMSTPCR1   0xe6150134
#define SMSTPCR2   0xe6150138
#define SMSTPCR3   0xe615013C
#define SMSTPCR4   0xe6150140

/* Fixed 32 KHz root clock from EXTALR pin */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * 26MHz default rate for the EXTALC1 root input clock.
 * If needed, reset this with clk_set_rate() from the platform code.
 */
struct clk sh7377_extalc1_clk = {
	.rate		= 26666666,
};

/*
 * 48MHz default rate for the EXTAL2 root input clock.
 * If needed, reset this with clk_set_rate() from the platform code.
 */
struct clk sh7377_extal2_clk = {
	.rate		= 48000000,
};

/* A fixed divide-by-2 block */
static unsigned long div2_recalc(struct clk *clk)
{
	return clk->parent->rate / 2;
}

static struct clk_ops div2_clk_ops = {
	.recalc		= div2_recalc,
};

/* Divide extalc1 by two */
static struct clk extalc1_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &sh7377_extalc1_clk,
};

/* Divide extal2 by two */
static struct clk extal2_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &sh7377_extal2_clk,
};

/* Divide extal2 by four */
static struct clk extal2_div4_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &extal2_div2_clk,
};

/* PLLC1 */
static unsigned long pllc1_recalc(struct clk *clk)
{
	unsigned long mult = 1;

	if (__raw_readl(PLLC1CR) & (1 << 14))
		mult = (((__raw_readl(RTFRQCR) >> 24) & 0x3f) + 1) * 2;

	return clk->parent->rate * mult;
}

static struct clk_ops pllc1_clk_ops = {
	.recalc		= pllc1_recalc,
};

static struct clk pllc1_clk = {
	.ops		= &pllc1_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &extalc1_div2_clk,
};

/* Divide PLLC1 by two */
static struct clk pllc1_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &pllc1_clk,
};

/* PLLC2 */
static unsigned long pllc2_recalc(struct clk *clk)
{
	unsigned long mult = 1;

	if (__raw_readl(PLLC2CR) & (1 << 31))
		mult = (((__raw_readl(PLLC2CR) >> 24) & 0x3f) + 1) * 2;

	return clk->parent->rate * mult;
}

static struct clk_ops pllc2_clk_ops = {
	.recalc		= pllc2_recalc,
};

static struct clk pllc2_clk = {
	.ops		= &pllc2_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &extalc1_div2_clk,
};

static struct clk *main_clks[] = {
	&r_clk,
	&sh7377_extalc1_clk,
	&sh7377_extal2_clk,
	&extalc1_div2_clk,
	&extal2_div2_clk,
	&extal2_div4_clk,
	&pllc1_clk,
	&pllc1_div2_clk,
	&pllc2_clk,
};

static void div4_kick(struct clk *clk)
{
	unsigned long value;

	/* set KICK bit in SYFRQCR to update hardware setting */
	value = __raw_readl(SYFRQCR);
	value |= (1 << 31);
	__raw_writel(value, SYFRQCR);
}

static int divisors[] = { 2, 3, 4, 6, 8, 12, 16, 18,
			  24, 32, 36, 48, 0, 72, 96, 0 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
	.kick = div4_kick,
};

enum { DIV4_I, DIV4_ZG, DIV4_B, DIV4_M1, DIV4_CSIR,
       DIV4_ZTR, DIV4_ZT, DIV4_Z, DIV4_HP,
       DIV4_ZS, DIV4_ZB, DIV4_ZB3, DIV4_CP, DIV4_NR };

#define DIV4(_reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(&pllc1_clk, _reg, _bit, _mask, _flags)

static struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4(RTFRQCR, 20, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_ZG] = DIV4(RTFRQCR, 16, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(RTFRQCR, 8, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_M1] = DIV4(RTFRQCR, 4, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_CSIR] = DIV4(RTFRQCR, 0, 0x6fff, 0),
	[DIV4_ZTR] = DIV4(SYFRQCR, 20, 0x6fff, 0),
	[DIV4_ZT] = DIV4(SYFRQCR, 16, 0x6fff, 0),
	[DIV4_Z] = DIV4(SYFRQCR, 12, 0x6fff, 0),
	[DIV4_HP] = DIV4(SYFRQCR, 4, 0x6fff, 0),
	[DIV4_ZS] = DIV4(CMFRQCR, 12, 0x6fff, 0),
	[DIV4_ZB] = DIV4(CMFRQCR, 8, 0x6fff, 0),
	[DIV4_ZB3] = DIV4(CMFRQCR, 4, 0x6fff, 0),
	[DIV4_CP] = DIV4(CMFRQCR, 0, 0x6fff, 0),
};

enum { DIV6_VCK1, DIV6_VCK2, DIV6_VCK3, DIV6_FMSI, DIV6_FMSO,
       DIV6_FSI, DIV6_SUB, DIV6_SPU, DIV6_MSU, DIV6_MVI3, DIV6_HDMI,
       DIV6_MF1, DIV6_MF2, DIV6_DSIT, DIV6_DSIP,
       DIV6_NR };

static struct clk div6_clks[] = {
	[DIV6_VCK1] = SH_CLK_DIV6(&pllc1_div2_clk, VCLKCR1, 0),
	[DIV6_VCK2] = SH_CLK_DIV6(&pllc1_div2_clk, VCLKCR2, 0),
	[DIV6_VCK3] = SH_CLK_DIV6(&pllc1_div2_clk, VCLKCR3, 0),
	[DIV6_FMSI] = SH_CLK_DIV6(&pllc1_div2_clk, FMSICKCR, 0),
	[DIV6_FMSO] = SH_CLK_DIV6(&pllc1_div2_clk, FMSOCKCR, 0),
	[DIV6_FSI] = SH_CLK_DIV6(&pllc1_div2_clk, FSICKCR, 0),
	[DIV6_SUB] = SH_CLK_DIV6(&sh7377_extal2_clk, SUBUSBCKCR, 0),
	[DIV6_SPU] = SH_CLK_DIV6(&pllc1_div2_clk, SPUCKCR, 0),
	[DIV6_MSU] = SH_CLK_DIV6(&pllc1_div2_clk, MSUCKCR, 0),
	[DIV6_MVI3] = SH_CLK_DIV6(&pllc1_div2_clk, MVI3CKCR, 0),
	[DIV6_HDMI] = SH_CLK_DIV6(&pllc1_div2_clk, HDMICKCR, 0),
	[DIV6_MF1] = SH_CLK_DIV6(&pllc1_div2_clk, MFCK1CR, 0),
	[DIV6_MF2] = SH_CLK_DIV6(&pllc1_div2_clk, MFCK2CR, 0),
	[DIV6_DSIT] = SH_CLK_DIV6(&pllc1_div2_clk, DSITCKCR, 0),
	[DIV6_DSIP] = SH_CLK_DIV6(&pllc1_div2_clk, DSIPCKCR, 0),
};

enum { MSTP001,
       MSTP131, MSTP130, MSTP129, MSTP128, MSTP116, MSTP106, MSTP101,
       MSTP223, MSTP207, MSTP206, MSTP204, MSTP203, MSTP202, MSTP201, MSTP200,
       MSTP331, MSTP329, MSTP325, MSTP323, MSTP322,
       MSTP315, MSTP314, MSTP313,
       MSTP403,
       MSTP_NR };

#define MSTP(_parent, _reg, _bit, _flags) \
  SH_CLK_MSTP32(_parent, _reg, _bit, _flags)

static struct clk mstp_clks[] = {
	[MSTP001] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR0, 1, 0), /* IIC2 */
	[MSTP131] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 31, 0), /* VEU3 */
	[MSTP130] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 30, 0), /* VEU2 */
	[MSTP129] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 29, 0), /* VEU1 */
	[MSTP128] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 28, 0), /* VEU0 */
	[MSTP116] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR1, 16, 0), /* IIC0 */
	[MSTP106] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 6, 0), /* JPU */
	[MSTP101] = MSTP(&div4_clks[DIV4_M1], SMSTPCR1, 1, 0), /* VPU */
	[MSTP223] = MSTP(&div6_clks[DIV6_SPU], SMSTPCR2, 23, 0), /* SPU2 */
	[MSTP207] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 7, 0), /* SCIFA5 */
	[MSTP206] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 6, 0), /* SCIFB */
	[MSTP204] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 4, 0), /* SCIFA0 */
	[MSTP203] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 3, 0), /* SCIFA1 */
	[MSTP202] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 2, 0), /* SCIFA2 */
	[MSTP201] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 1, 0), /* SCIFA3 */
	[MSTP200] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 0, 0), /* SCIFA4 */
	[MSTP331] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR3, 31, 0), /* SCIFA6 */
	[MSTP329] = MSTP(&r_clk, SMSTPCR3, 29, 0), /* CMT10 */
	[MSTP325] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR3, 25, 0), /* IRDA */
	[MSTP323] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR3, 23, 0), /* IIC1 */
	[MSTP322] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR3, 22, 0), /* USB0 */
	[MSTP315] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 15, 0), /* FLCTL */
	[MSTP314] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 14, 0), /* SDHI0 */
	[MSTP313] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 13, 0), /* SDHI1 */
	[MSTP403] = MSTP(&r_clk, SMSTPCR4, 3, 0), /* KEYSC */
};

#define CLKDEV_CON_ID(_id, _clk) { .con_id = _id, .clk = _clk }
#define CLKDEV_DEV_ID(_id, _clk) { .dev_id = _id, .clk = _clk }

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("r_clk", &r_clk),
	CLKDEV_CON_ID("extalc1", &sh7377_extalc1_clk),
	CLKDEV_CON_ID("extal2", &sh7377_extal2_clk),
	CLKDEV_CON_ID("extalc1_div2_clk", &extalc1_div2_clk),
	CLKDEV_CON_ID("extal2_div2_clk", &extal2_div2_clk),
	CLKDEV_CON_ID("extal2_div4_clk", &extal2_div4_clk),
	CLKDEV_CON_ID("pllc1_clk", &pllc1_clk),
	CLKDEV_CON_ID("pllc1_div2_clk", &pllc1_div2_clk),
	CLKDEV_CON_ID("pllc2_clk", &pllc2_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("i_clk", &div4_clks[DIV4_I]),
	CLKDEV_CON_ID("zg_clk", &div4_clks[DIV4_ZG]),
	CLKDEV_CON_ID("b_clk", &div4_clks[DIV4_B]),
	CLKDEV_CON_ID("m1_clk", &div4_clks[DIV4_M1]),
	CLKDEV_CON_ID("csir_clk", &div4_clks[DIV4_CSIR]),
	CLKDEV_CON_ID("ztr_clk", &div4_clks[DIV4_ZTR]),
	CLKDEV_CON_ID("zt_clk", &div4_clks[DIV4_ZT]),
	CLKDEV_CON_ID("z_clk", &div4_clks[DIV4_Z]),
	CLKDEV_CON_ID("hp_clk", &div4_clks[DIV4_HP]),
	CLKDEV_CON_ID("zs_clk", &div4_clks[DIV4_ZS]),
	CLKDEV_CON_ID("zb_clk", &div4_clks[DIV4_ZB]),
	CLKDEV_CON_ID("zb3_clk", &div4_clks[DIV4_ZB3]),
	CLKDEV_CON_ID("cp_clk", &div4_clks[DIV4_CP]),

	/* DIV6 clocks */
	CLKDEV_CON_ID("vck1_clk", &div6_clks[DIV6_VCK1]),
	CLKDEV_CON_ID("vck2_clk", &div6_clks[DIV6_VCK2]),
	CLKDEV_CON_ID("vck3_clk", &div6_clks[DIV6_VCK3]),
	CLKDEV_CON_ID("fmsi_clk", &div6_clks[DIV6_FMSI]),
	CLKDEV_CON_ID("fmso_clk", &div6_clks[DIV6_FMSO]),
	CLKDEV_CON_ID("fsi_clk", &div6_clks[DIV6_FSI]),
	CLKDEV_CON_ID("sub_clk", &div6_clks[DIV6_SUB]),
	CLKDEV_CON_ID("spu_clk", &div6_clks[DIV6_SPU]),
	CLKDEV_CON_ID("msu_clk", &div6_clks[DIV6_MSU]),
	CLKDEV_CON_ID("mvi3_clk", &div6_clks[DIV6_MVI3]),
	CLKDEV_CON_ID("hdmi_clk", &div6_clks[DIV6_HDMI]),
	CLKDEV_CON_ID("mf1_clk", &div6_clks[DIV6_MF1]),
	CLKDEV_CON_ID("mf2_clk", &div6_clks[DIV6_MF2]),
	CLKDEV_CON_ID("dsit_clk", &div6_clks[DIV6_DSIT]),
	CLKDEV_CON_ID("dsip_clk", &div6_clks[DIV6_DSIP]),

	/* MSTP32 clocks */
	CLKDEV_DEV_ID("i2c-sh_mobile.2", &mstp_clks[MSTP001]), /* IIC2 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.4", &mstp_clks[MSTP131]), /* VEU3 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.3", &mstp_clks[MSTP130]), /* VEU2 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.2", &mstp_clks[MSTP129]), /* VEU1 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.1", &mstp_clks[MSTP128]), /* VEU0 */
	CLKDEV_DEV_ID("i2c-sh_mobile.0", &mstp_clks[MSTP116]), /* IIC0 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.5", &mstp_clks[MSTP106]), /* JPU */
	CLKDEV_DEV_ID("uio_pdrv_genirq.0", &mstp_clks[MSTP101]), /* VPU */
	CLKDEV_DEV_ID("uio_pdrv_genirq.6", &mstp_clks[MSTP223]), /* SPU2DSP0 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.7", &mstp_clks[MSTP223]), /* SPU2DSP1 */
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP207]), /* SCIFA5 */
	CLKDEV_DEV_ID("sh-sci.7", &mstp_clks[MSTP206]), /* SCIFB */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]), /* SCIFA0 */
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]), /* SCIFA1 */
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP202]), /* SCIFA2 */
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP201]), /* SCIFA3 */
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP200]), /* SCIFA4 */
	CLKDEV_DEV_ID("sh-sci.6", &mstp_clks[MSTP331]), /* SCIFA6 */
	CLKDEV_CON_ID("cmt1", &mstp_clks[MSTP329]), /* CMT10 */
	CLKDEV_DEV_ID("sh_irda", &mstp_clks[MSTP325]), /* IRDA */
	CLKDEV_DEV_ID("i2c-sh_mobile.1", &mstp_clks[MSTP323]), /* IIC1 */
	CLKDEV_DEV_ID("r8a66597_hcd.0", &mstp_clks[MSTP322]), /* USBHS */
	CLKDEV_DEV_ID("r8a66597_udc.0", &mstp_clks[MSTP322]), /* USBHS */
	CLKDEV_DEV_ID("sh_flctl", &mstp_clks[MSTP315]), /* FLCTL */
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP314]), /* SDHI0 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP313]), /* SDHI1 */
	CLKDEV_DEV_ID("sh_keysc.0", &mstp_clks[MSTP403]), /* KEYSC */
};

void __init sh7377_clock_init(void)
{
	int k, ret = 0;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		clk_init();
	else
		panic("failed to setup sh7377 clocks\n");
}
