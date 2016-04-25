#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/init.h>
#include <linux/io.h>

static struct clk *out[2];
static struct clk_onecell_data clk_data = { out, 2 };

#define SYSCLK_CTRL	0x20
#define CPUCLK_CTRL	0x24
#define LEGACY_DIV	0x3c

#define PLL_N(val)	(((val) >>  0) & 0x7f)
#define PLL_K(val)	(((val) >> 13) & 0x7)
#define PLL_M(val)	(((val) >> 16) & 0x7)
#define DIV_INDEX(val)	(((val) >>  8) & 0xf)

static void __init make_pll(int idx, const char *parent, void __iomem *base)
{
	char name[8];
	u32 val, mul, div;

	sprintf(name, "pll%d", idx);
	val = readl_relaxed(base + idx*8);
	mul =  PLL_N(val) + 1;
	div = (PLL_M(val) + 1) << PLL_K(val);
	clk_register_fixed_factor(NULL, name, parent, 0, mul, div);
}

static int __init get_div(void __iomem *base)
{
	u8 sysclk_tab[16] = { 2, 4, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4 };
	int idx = DIV_INDEX(readl_relaxed(base + LEGACY_DIV));

	return sysclk_tab[idx];
}

static void __init tango4_clkgen_setup(struct device_node *np)
{
	int div, ret;
	void __iomem *base = of_iomap(np, 0);
	const char *parent = of_clk_get_parent_name(np, 0);

	if (!base)
		panic("%s: invalid address\n", np->full_name);

	make_pll(0, parent, base);
	make_pll(1, parent, base);

	out[0] = clk_register_divider(NULL, "cpuclk", "pll0", 0,
			base + CPUCLK_CTRL, 8, 8, CLK_DIVIDER_ONE_BASED, NULL);

	div = readl_relaxed(base + SYSCLK_CTRL) & BIT(23) ? get_div(base) : 4;
	out[1] = clk_register_fixed_factor(NULL, "sysclk", "pll1", 0, 1, div);

	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
	if (IS_ERR(out[0]) || IS_ERR(out[1]) || ret < 0)
		panic("%s: clk registration failed\n", np->full_name);
}
CLK_OF_DECLARE(tango4_clkgen, "sigma,tango4-clkgen", tango4_clkgen_setup);
