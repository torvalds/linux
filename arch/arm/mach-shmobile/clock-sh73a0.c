/*
 * sh73a0 clock framework support
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

#define SMSTPCR0	0xe6150130
#define SMSTPCR1	0xe6150134
#define SMSTPCR2	0xe6150138
#define SMSTPCR3	0xe615013c
#define SMSTPCR4	0xe6150140
#define SMSTPCR5	0xe6150144

/* Fixed 32 KHz root clock from EXTALR pin */
static struct clk r_clk = {
	.rate           = 32768,
};

/* Temporarily fixed 48 MHz SUB clock */
static struct clk sub_clk = {
	.rate           = 48000000,
};

/* Temporarily fixed 104 MHz HP clock */
static struct clk hp_clk = {
	.rate           = 104000000,
};

static struct clk *main_clks[] = {
	&r_clk,
	&sub_clk,
	&hp_clk,
};

enum {	MSTP001,
	MSTP116,
	MSTP219, MSTP207, MSTP206, MSTP204, MSTP203, MSTP202, MSTP201, MSTP200,
	MSTP331, MSTP329, MSTP323,
	MSTP411, MSTP410, MSTP403,
	MSTP_NR };

#define MSTP(_parent, _reg, _bit, _flags) \
	SH_CLK_MSTP32(_parent, _reg, _bit, _flags)

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP001] = MSTP(&hp_clk, SMSTPCR0, 1, 0), /* I2C2 */
	[MSTP116] = MSTP(&hp_clk, SMSTPCR1, 16, 0), /* I2C0 */
	[MSTP219] = MSTP(&sub_clk, SMSTPCR2, 19, 0), /* SCIFA7 */
	[MSTP207] = MSTP(&sub_clk, SMSTPCR2, 7, 0), /* SCIFA5 */
	[MSTP206] = MSTP(&sub_clk, SMSTPCR2, 6, 0), /* SCIFB */
	[MSTP204] = MSTP(&sub_clk, SMSTPCR2, 4, 0), /* SCIFA0 */
	[MSTP203] = MSTP(&sub_clk, SMSTPCR2, 3, 0), /* SCIFA1 */
	[MSTP202] = MSTP(&sub_clk, SMSTPCR2, 2, 0), /* SCIFA2 */
	[MSTP201] = MSTP(&sub_clk, SMSTPCR2, 1, 0), /* SCIFA3 */
	[MSTP200] = MSTP(&sub_clk, SMSTPCR2, 0, 0), /* SCIFA4 */
	[MSTP331] = MSTP(&sub_clk, SMSTPCR3, 31, 0), /* SCIFA6 */
	[MSTP329] = MSTP(&r_clk, SMSTPCR3, 29, 0), /* CMT10 */
	[MSTP323] = MSTP(&hp_clk, SMSTPCR3, 23, 0), /* I2C1 */
	[MSTP411] = MSTP(&hp_clk, SMSTPCR4, 11, 0), /* I2C3 */
	[MSTP410] = MSTP(&hp_clk, SMSTPCR4, 10, 0), /* I2C4 */
	[MSTP403] = MSTP(&r_clk, SMSTPCR4, 0, 0), /* KEYSC0 */
};

#define CLKDEV_DEV_ID(_id, _clk) { .dev_id = _id, .clk = _clk }

static struct clk_lookup lookups[] = {
	/* MSTP32 clocks */
	CLKDEV_DEV_ID("i2c-sh_mobile.2", &mstp_clks[MSTP001]), /* I2C2 */
	CLKDEV_DEV_ID("i2c-sh_mobile.0", &mstp_clks[MSTP116]), /* I2C0 */
	CLKDEV_DEV_ID("sh-sci.7", &mstp_clks[MSTP219]), /* SCIFA7 */
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP207]), /* SCIFA5 */
	CLKDEV_DEV_ID("sh-sci.8", &mstp_clks[MSTP206]), /* SCIFB */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]), /* SCIFA0 */
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]), /* SCIFA1 */
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP202]), /* SCIFA2 */
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP201]), /* SCIFA3 */
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP200]), /* SCIFA4 */
	CLKDEV_DEV_ID("sh-sci.6", &mstp_clks[MSTP331]), /* SCIFA6 */
	CLKDEV_DEV_ID("sh_cmt.10", &mstp_clks[MSTP329]), /* CMT10 */
	CLKDEV_DEV_ID("i2c-sh_mobile.1", &mstp_clks[MSTP323]), /* I2C1 */
	CLKDEV_DEV_ID("i2c-sh_mobile.3", &mstp_clks[MSTP411]), /* I2C3 */
	CLKDEV_DEV_ID("i2c-sh_mobile.4", &mstp_clks[MSTP410]), /* I2C4 */
	CLKDEV_DEV_ID("sh_keysc.0", &mstp_clks[MSTP403]), /* KEYSC0 */
};

void __init sh73a0_clock_init(void)
{
	int k, ret = 0;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		clk_init();
	else
		panic("failed to setup sh73a0 clocks\n");
}
