/*
 * Copyright (C) 2014 Free Electrons
 *
 * License Terms: GNU General Public License v2
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * Allwinner A31 AR100 clock driver
 *
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define SUN6I_AR100_MAX_PARENTS		4
#define SUN6I_AR100_SHIFT_MASK		0x3
#define SUN6I_AR100_SHIFT_MAX		SUN6I_AR100_SHIFT_MASK
#define SUN6I_AR100_SHIFT_SHIFT		4
#define SUN6I_AR100_DIV_MASK		0x1f
#define SUN6I_AR100_DIV_MAX		(SUN6I_AR100_DIV_MASK + 1)
#define SUN6I_AR100_DIV_SHIFT		8
#define SUN6I_AR100_MUX_MASK		0x3
#define SUN6I_AR100_MUX_SHIFT		16

struct ar100_clk {
	struct clk_hw hw;
	void __iomem *reg;
};

static inline struct ar100_clk *to_ar100_clk(struct clk_hw *hw)
{
	return container_of(hw, struct ar100_clk, hw);
}

static unsigned long ar100_recalc_rate(struct clk_hw *hw,
				       unsigned long parent_rate)
{
	struct ar100_clk *clk = to_ar100_clk(hw);
	u32 val = readl(clk->reg);
	int shift = (val >> SUN6I_AR100_SHIFT_SHIFT) & SUN6I_AR100_SHIFT_MASK;
	int div = (val >> SUN6I_AR100_DIV_SHIFT) & SUN6I_AR100_DIV_MASK;

	return (parent_rate >> shift) / (div + 1);
}

static int ar100_determine_rate(struct clk_hw *hw,
				struct clk_rate_request *req)
{
	int nparents = clk_hw_get_num_parents(hw);
	long best_rate = -EINVAL;
	int i;

	req->best_parent_hw = NULL;

	for (i = 0; i < nparents; i++) {
		unsigned long parent_rate;
		unsigned long tmp_rate;
		struct clk *parent;
		unsigned long div;
		int shift;

		parent = clk_get_parent_by_index(hw->clk, i);
		parent_rate = __clk_get_rate(parent);
		div = DIV_ROUND_UP(parent_rate, req->rate);

		/*
		 * The AR100 clk contains 2 divisors:
		 * - one power of 2 divisor
		 * - one regular divisor
		 *
		 * First check if we can safely shift (or divide by a power
		 * of 2) without losing precision on the requested rate.
		 */
		shift = ffs(div) - 1;
		if (shift > SUN6I_AR100_SHIFT_MAX)
			shift = SUN6I_AR100_SHIFT_MAX;

		div >>= shift;

		/*
		 * Then if the divisor is still bigger than what the HW
		 * actually supports, use a bigger shift (or power of 2
		 * divider) value and accept to lose some precision.
		 */
		while (div > SUN6I_AR100_DIV_MAX) {
			shift++;
			div >>= 1;
			if (shift > SUN6I_AR100_SHIFT_MAX)
				break;
		}

		/*
		 * If the shift value (or power of 2 divider) is bigger
		 * than what the HW actually support, skip this parent.
		 */
		if (shift > SUN6I_AR100_SHIFT_MAX)
			continue;

		tmp_rate = (parent_rate >> shift) / div;
		if (!req->best_parent_hw || tmp_rate > best_rate) {
			req->best_parent_hw = __clk_get_hw(parent);
			req->best_parent_rate = parent_rate;
			best_rate = tmp_rate;
		}
	}

	if (best_rate < 0)
		return best_rate;

	req->rate = best_rate;

	return 0;
}

static int ar100_set_parent(struct clk_hw *hw, u8 index)
{
	struct ar100_clk *clk = to_ar100_clk(hw);
	u32 val = readl(clk->reg);

	if (index >= SUN6I_AR100_MAX_PARENTS)
		return -EINVAL;

	val &= ~(SUN6I_AR100_MUX_MASK << SUN6I_AR100_MUX_SHIFT);
	val |= (index << SUN6I_AR100_MUX_SHIFT);
	writel(val, clk->reg);

	return 0;
}

static u8 ar100_get_parent(struct clk_hw *hw)
{
	struct ar100_clk *clk = to_ar100_clk(hw);
	return (readl(clk->reg) >> SUN6I_AR100_MUX_SHIFT) &
	       SUN6I_AR100_MUX_MASK;
}

static int ar100_set_rate(struct clk_hw *hw, unsigned long rate,
			  unsigned long parent_rate)
{
	unsigned long div = parent_rate / rate;
	struct ar100_clk *clk = to_ar100_clk(hw);
	u32 val = readl(clk->reg);
	int shift;

	if (parent_rate % rate)
		return -EINVAL;

	shift = ffs(div) - 1;
	if (shift > SUN6I_AR100_SHIFT_MAX)
		shift = SUN6I_AR100_SHIFT_MAX;

	div >>= shift;

	if (div > SUN6I_AR100_DIV_MAX)
		return -EINVAL;

	val &= ~((SUN6I_AR100_SHIFT_MASK << SUN6I_AR100_SHIFT_SHIFT) |
		 (SUN6I_AR100_DIV_MASK << SUN6I_AR100_DIV_SHIFT));
	val |= (shift << SUN6I_AR100_SHIFT_SHIFT) |
	       (div << SUN6I_AR100_DIV_SHIFT);
	writel(val, clk->reg);

	return 0;
}

static struct clk_ops ar100_ops = {
	.recalc_rate = ar100_recalc_rate,
	.determine_rate = ar100_determine_rate,
	.set_parent = ar100_set_parent,
	.get_parent = ar100_get_parent,
	.set_rate = ar100_set_rate,
};

static int sun6i_a31_ar100_clk_probe(struct platform_device *pdev)
{
	const char *parents[SUN6I_AR100_MAX_PARENTS];
	struct device_node *np = pdev->dev.of_node;
	const char *clk_name = np->name;
	struct clk_init_data init;
	struct ar100_clk *ar100;
	struct resource *r;
	struct clk *clk;
	int nparents;

	ar100 = devm_kzalloc(&pdev->dev, sizeof(*ar100), GFP_KERNEL);
	if (!ar100)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ar100->reg = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(ar100->reg))
		return PTR_ERR(ar100->reg);

	nparents = of_clk_get_parent_count(np);
	if (nparents > SUN6I_AR100_MAX_PARENTS)
		nparents = SUN6I_AR100_MAX_PARENTS;

	of_clk_parent_fill(np, parents, nparents);

	of_property_read_string(np, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = &ar100_ops;
	init.parent_names = parents;
	init.num_parents = nparents;
	init.flags = 0;

	ar100->hw.init = &init;

	clk = clk_register(&pdev->dev, &ar100->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	return of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static const struct of_device_id sun6i_a31_ar100_clk_dt_ids[] = {
	{ .compatible = "allwinner,sun6i-a31-ar100-clk" },
	{ /* sentinel */ }
};

static struct platform_driver sun6i_a31_ar100_clk_driver = {
	.driver = {
		.name = "sun6i-a31-ar100-clk",
		.of_match_table = sun6i_a31_ar100_clk_dt_ids,
	},
	.probe = sun6i_a31_ar100_clk_probe,
};
module_platform_driver(sun6i_a31_ar100_clk_driver);

MODULE_AUTHOR("Boris BREZILLON <boris.brezillon@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A31 AR100 clock Driver");
MODULE_LICENSE("GPL v2");
