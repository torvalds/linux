/*
 * r8a73a4 clock framework support
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
#include <mach/common.h>

#define CPG_BASE 0xe6150000
#define CPG_LEN 0x270

#define MPCKCR 0xe6150080
#define SMSTPCR2 0xe6150138
#define SMSTPCR5 0xe6150144

static struct clk_mapping cpg_mapping = {
	.phys   = CPG_BASE,
	.len    = CPG_LEN,
};

static struct clk extalr_clk = {
	.rate	= 32768,
	.mapping	= &cpg_mapping,
};

static struct clk extal1_clk = {
	.rate	= 26000000,
	.mapping	= &cpg_mapping,
};

static struct clk extal2_clk = {
	.rate	= 48000000,
	.mapping	= &cpg_mapping,
};

static struct clk *main_clks[] = {
	&extalr_clk,
	&extal1_clk,
	&extal2_clk,
};

enum {
	MSTP217, MSTP216, MSTP207, MSTP206, MSTP204, MSTP203,
	MSTP522,
	MSTP_NR
};

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP204] = SH_CLK_MSTP32(&extal2_clk, SMSTPCR2, 4, 0), /* SCIFA0 */
	[MSTP203] = SH_CLK_MSTP32(&extal2_clk, SMSTPCR2, 3, 0), /* SCIFA1 */
	[MSTP206] = SH_CLK_MSTP32(&extal2_clk, SMSTPCR2, 6, 0), /* SCIFB0 */
	[MSTP207] = SH_CLK_MSTP32(&extal2_clk, SMSTPCR2, 7, 0), /* SCIFB1 */
	[MSTP216] = SH_CLK_MSTP32(&extal2_clk, SMSTPCR2, 16, 0), /* SCIFB2 */
	[MSTP217] = SH_CLK_MSTP32(&extal2_clk, SMSTPCR2, 17, 0), /* SCIFB3 */
	[MSTP522] = SH_CLK_MSTP32(&extal2_clk, SMSTPCR5, 22, 0), /* Thermal */
};

static struct clk_lookup lookups[] = {
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]),
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]),
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP206]),
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP207]),
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP216]),
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP217]),
	CLKDEV_DEV_ID("rcar_thermal", &mstp_clks[MSTP522]),

	/* for DT */
	CLKDEV_DEV_ID("e61f0000.thermal", &mstp_clks[MSTP522]),
};

void __init r8a73a4_clock_init(void)
{
	void __iomem *cpg_base, *reg;
	int k, ret = 0;

	/* fix MPCLK to EXTAL2 for now.
	 * this is needed until more detailed clock topology is supported
	 */
	cpg_base = ioremap_nocache(CPG_BASE, CPG_LEN);
	BUG_ON(!cpg_base);
	reg = cpg_base + (MPCKCR - CPG_BASE);
	iowrite32(ioread32(reg) | 1 << 7 | 0x0c, reg); /* set CKSEL */
	iounmap(cpg_base);

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup r8a73a4 clocks\n");
}
