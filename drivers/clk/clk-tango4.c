// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/init.h>
#include <linux/io.h>

#define CLK_COUNT 4 /* cpu_clk, sys_clk, usb_clk, sdio_clk */
static struct clk *clks[CLK_COUNT];
static struct clk_onecell_data clk_data = { clks, CLK_COUNT };

#define SYSCLK_DIV	0x20
#define CPUCLK_DIV	0x24
#define DIV_BYPASS	BIT(23)

/*** CLKGEN_PLL ***/
#define extract_pll_n(val)	((val >>  0) & ((1u << 7) - 1))
#define extract_pll_k(val)	((val >> 13) & ((1u << 3) - 1))
#define extract_pll_m(val)	((val >> 16) & ((1u << 3) - 1))
#define extract_pll_isel(val)	((val >> 24) & ((1u << 3) - 1))

static void __init make_pll(int idx, const char *parent, void __iomem *base)
{
	char name[8];
	u32 val, mul, div;

	sprintf(name, "pll%d", idx);
	val = readl(base + idx * 8);
	mul =  extract_pll_n(val) + 1;
	div = (extract_pll_m(val) + 1) << extract_pll_k(val);
	clk_register_fixed_factor(NULL, name, parent, 0, mul, div);
	if (extract_pll_isel(val) != 1)
		panic("%s: input not set to XTAL_IN\n", name);
}

static void __init make_cd(int idx, void __iomem *base)
{
	char name[8];
	u32 val, mul, div;

	sprintf(name, "cd%d", idx);
	val = readl(base + idx * 8);
	mul =  1 << 27;
	div = (2 << 27) + val;
	clk_register_fixed_factor(NULL, name, "pll2", 0, mul, div);
	if (val > 0xf0000000)
		panic("%s: unsupported divider %x\n", name, val);
}

static void __init tango4_clkgen_setup(struct device_node *np)
{
	struct clk **pp = clk_data.clks;
	void __iomem *base = of_iomap(np, 0);
	const char *parent = of_clk_get_parent_name(np, 0);

	if (!base)
		panic("%s: invalid address\n", np->name);

	if (readl(base + CPUCLK_DIV) & DIV_BYPASS)
		panic("%s: unsupported cpuclk setup\n", np->name);

	if (readl(base + SYSCLK_DIV) & DIV_BYPASS)
		panic("%s: unsupported sysclk setup\n", np->name);

	writel(0x100, base + CPUCLK_DIV); /* disable frequency ramping */

	make_pll(0, parent, base);
	make_pll(1, parent, base);
	make_pll(2, parent, base);
	make_cd(2, base + 0x80);
	make_cd(6, base + 0x80);

	pp[0] = clk_register_divider(NULL, "cpu_clk", "pll0", 0,
			base + CPUCLK_DIV, 8, 8, CLK_DIVIDER_ONE_BASED, NULL);
	pp[1] = clk_register_fixed_factor(NULL, "sys_clk", "pll1", 0, 1, 4);
	pp[2] = clk_register_fixed_factor(NULL,  "usb_clk", "cd2", 0, 1, 2);
	pp[3] = clk_register_fixed_factor(NULL, "sdio_clk", "cd6", 0, 1, 2);

	if (IS_ERR(pp[0]) || IS_ERR(pp[1]) || IS_ERR(pp[2]) || IS_ERR(pp[3]))
		panic("%s: clk registration failed\n", np->name);

	if (of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data))
		panic("%s: clk provider registration failed\n", np->name);
}
CLK_OF_DECLARE(tango4_clkgen, "sigma,tango4-clkgen", tango4_clkgen_setup);
