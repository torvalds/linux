// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx 'Clocking Wizard' driver
 *
 *  Copyright (C) 2013 - 2021 Xilinx
 *
 *  Sören Brinkmann <soren.brinkmann@xilinx.com>
 *
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/iopoll.h>

#define WZRD_NUM_OUTPUTS	7
#define WZRD_ACLK_MAX_FREQ	250000000UL

#define WZRD_CLK_CFG_REG(n)	(0x200 + 4 * (n))

#define WZRD_CLKOUT0_FRAC_EN	BIT(18)
#define WZRD_CLKFBOUT_FRAC_EN	BIT(26)

#define WZRD_CLKFBOUT_MULT_SHIFT	8
#define WZRD_CLKFBOUT_MULT_MASK		(0xff << WZRD_CLKFBOUT_MULT_SHIFT)
#define WZRD_CLKFBOUT_FRAC_SHIFT	16
#define WZRD_CLKFBOUT_FRAC_MASK		(0x3ff << WZRD_CLKFBOUT_FRAC_SHIFT)
#define WZRD_DIVCLK_DIVIDE_SHIFT	0
#define WZRD_DIVCLK_DIVIDE_MASK		(0xff << WZRD_DIVCLK_DIVIDE_SHIFT)
#define WZRD_CLKOUT_DIVIDE_SHIFT	0
#define WZRD_CLKOUT_DIVIDE_WIDTH	8
#define WZRD_CLKOUT_DIVIDE_MASK		(0xff << WZRD_DIVCLK_DIVIDE_SHIFT)
#define WZRD_CLKOUT_FRAC_SHIFT		8
#define WZRD_CLKOUT_FRAC_MASK		0x3ff

#define WZRD_DR_MAX_INT_DIV_VALUE	255
#define WZRD_DR_STATUS_REG_OFFSET	0x04
#define WZRD_DR_LOCK_BIT_MASK		0x00000001
#define WZRD_DR_INIT_REG_OFFSET		0x25C
#define WZRD_DR_DIV_TO_PHASE_OFFSET	4
#define WZRD_DR_BEGIN_DYNA_RECONF	0x03
#define WZRD_DR_BEGIN_DYNA_RECONF_5_2	0x07
#define WZRD_DR_BEGIN_DYNA_RECONF1_5_2	0x02

#define WZRD_USEC_POLL		10
#define WZRD_TIMEOUT_POLL		1000
/* Get the mask from width */
#define div_mask(width)			((1 << (width)) - 1)

/* Extract divider instance from clock hardware instance */
#define to_clk_wzrd_divider(_hw) container_of(_hw, struct clk_wzrd_divider, hw)

enum clk_wzrd_int_clks {
	wzrd_clk_mul,
	wzrd_clk_mul_div,
	wzrd_clk_mul_frac,
	wzrd_clk_int_max
};

/**
 * struct clk_wzrd - Clock wizard private data structure
 *
 * @clk_data:		Clock data
 * @nb:			Notifier block
 * @base:		Memory base
 * @clk_in1:		Handle to input clock 'clk_in1'
 * @axi_clk:		Handle to input clock 's_axi_aclk'
 * @clks_internal:	Internal clocks
 * @clkout:		Output clocks
 * @speed_grade:	Speed grade of the device
 * @suspended:		Flag indicating power state of the device
 */
struct clk_wzrd {
	struct clk_onecell_data clk_data;
	struct notifier_block nb;
	void __iomem *base;
	struct clk *clk_in1;
	struct clk *axi_clk;
	struct clk *clks_internal[wzrd_clk_int_max];
	struct clk *clkout[WZRD_NUM_OUTPUTS];
	unsigned int speed_grade;
	bool suspended;
};

/**
 * struct clk_wzrd_divider - clock divider specific to clk_wzrd
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @base:	base address of register containing the divider
 * @offset:	offset address of register containing the divider
 * @shift:	shift to the divider bit field
 * @width:	width of the divider bit field
 * @flags:	clk_wzrd divider flags
 * @table:	array of value/divider pairs, last entry should have div = 0
 * @lock:	register lock
 */
struct clk_wzrd_divider {
	struct clk_hw hw;
	void __iomem *base;
	u16 offset;
	u8 shift;
	u8 width;
	u8 flags;
	const struct clk_div_table *table;
	spinlock_t *lock;  /* divider lock */
};

#define to_clk_wzrd(_nb) container_of(_nb, struct clk_wzrd, nb)

/* maximum frequencies for input/output clocks per speed grade */
static const unsigned long clk_wzrd_max_freq[] = {
	800000000UL,
	933000000UL,
	1066000000UL
};

/* spin lock variable for clk_wzrd */
static DEFINE_SPINLOCK(clkwzrd_lock);

static unsigned long clk_wzrd_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr = divider->base + divider->offset;
	unsigned int val;

	val = readl(div_addr) >> divider->shift;
	val &= div_mask(divider->width);

	return divider_recalc_rate(hw, parent_rate, val, divider->table,
			divider->flags, divider->width);
}

static int clk_wzrd_dynamic_reconfig(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	int err;
	u32 value;
	unsigned long flags = 0;
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr = divider->base + divider->offset;

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	value = DIV_ROUND_CLOSEST(parent_rate, rate);

	/* Cap the value to max */
	min_t(u32, value, WZRD_DR_MAX_INT_DIV_VALUE);

	/* Set divisor and clear phase offset */
	writel(value, div_addr);
	writel(0x00, div_addr + WZRD_DR_DIV_TO_PHASE_OFFSET);

	/* Check status register */
	err = readl_poll_timeout(divider->base + WZRD_DR_STATUS_REG_OFFSET,
				 value, value & WZRD_DR_LOCK_BIT_MASK,
				 WZRD_USEC_POLL, WZRD_TIMEOUT_POLL);
	if (err)
		goto err_reconfig;

	/* Initiate reconfiguration */
	writel(WZRD_DR_BEGIN_DYNA_RECONF_5_2,
	       divider->base + WZRD_DR_INIT_REG_OFFSET);
	writel(WZRD_DR_BEGIN_DYNA_RECONF1_5_2,
	       divider->base + WZRD_DR_INIT_REG_OFFSET);

	/* Check status register */
	err = readl_poll_timeout(divider->base + WZRD_DR_STATUS_REG_OFFSET,
				 value, value & WZRD_DR_LOCK_BIT_MASK,
				 WZRD_USEC_POLL, WZRD_TIMEOUT_POLL);
err_reconfig:
	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);
	else
		__release(divider->lock);
	return err;
}

static long clk_wzrd_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	u8 div;

	/*
	 * since we don't change parent rate we just round rate to closest
	 * achievable
	 */
	div = DIV_ROUND_CLOSEST(*prate, rate);

	return *prate / div;
}

static const struct clk_ops clk_wzrd_clk_divider_ops = {
	.round_rate = clk_wzrd_round_rate,
	.set_rate = clk_wzrd_dynamic_reconfig,
	.recalc_rate = clk_wzrd_recalc_rate,
};

static unsigned long clk_wzrd_recalc_ratef(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	unsigned int val;
	u32 div, frac;
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr = divider->base + divider->offset;

	val = readl(div_addr);
	div = val & div_mask(divider->width);
	frac = (val >> WZRD_CLKOUT_FRAC_SHIFT) & WZRD_CLKOUT_FRAC_MASK;

	return mult_frac(parent_rate, 1000, (div * 1000) + frac);
}

static int clk_wzrd_dynamic_reconfig_f(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	int err;
	u32 value, pre;
	unsigned long rate_div, f, clockout0_div;
	struct clk_wzrd_divider *divider = to_clk_wzrd_divider(hw);
	void __iomem *div_addr = divider->base + divider->offset;

	rate_div = DIV_ROUND_DOWN_ULL(parent_rate * 1000, rate);
	clockout0_div = rate_div / 1000;

	pre = DIV_ROUND_CLOSEST((parent_rate * 1000), rate);
	f = (u32)(pre - (clockout0_div * 1000));
	f = f & WZRD_CLKOUT_FRAC_MASK;
	f = f << WZRD_CLKOUT_DIVIDE_WIDTH;

	value = (f  | (clockout0_div & WZRD_CLKOUT_DIVIDE_MASK));

	/* Set divisor and clear phase offset */
	writel(value, div_addr);
	writel(0x0, div_addr + WZRD_DR_DIV_TO_PHASE_OFFSET);

	/* Check status register */
	err = readl_poll_timeout(divider->base + WZRD_DR_STATUS_REG_OFFSET, value,
				 value & WZRD_DR_LOCK_BIT_MASK,
				 WZRD_USEC_POLL, WZRD_TIMEOUT_POLL);
	if (err)
		return err;

	/* Initiate reconfiguration */
	writel(WZRD_DR_BEGIN_DYNA_RECONF_5_2,
	       divider->base + WZRD_DR_INIT_REG_OFFSET);
	writel(WZRD_DR_BEGIN_DYNA_RECONF1_5_2,
	       divider->base + WZRD_DR_INIT_REG_OFFSET);

	/* Check status register */
	return readl_poll_timeout(divider->base + WZRD_DR_STATUS_REG_OFFSET, value,
				value & WZRD_DR_LOCK_BIT_MASK,
				WZRD_USEC_POLL, WZRD_TIMEOUT_POLL);
}

static long clk_wzrd_round_rate_f(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	return rate;
}

static const struct clk_ops clk_wzrd_clk_divider_ops_f = {
	.round_rate = clk_wzrd_round_rate_f,
	.set_rate = clk_wzrd_dynamic_reconfig_f,
	.recalc_rate = clk_wzrd_recalc_ratef,
};

static struct clk *clk_wzrd_register_divf(struct device *dev,
					  const char *name,
					  const char *parent_name,
					  unsigned long flags,
					  void __iomem *base, u16 offset,
					  u8 shift, u8 width,
					  u8 clk_divider_flags,
					  const struct clk_div_table *table,
					  spinlock_t *lock)
{
	struct clk_wzrd_divider *div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;

	init.ops = &clk_wzrd_clk_divider_ops_f;

	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	div->base = base;
	div->offset = offset;
	div->shift = shift;
	div->width = width;
	div->flags = clk_divider_flags;
	div->lock = lock;
	div->hw.init = &init;
	div->table = table;

	hw = &div->hw;
	ret =  devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);

	return hw->clk;
}

static struct clk *clk_wzrd_register_divider(struct device *dev,
					     const char *name,
					     const char *parent_name,
					     unsigned long flags,
					     void __iomem *base, u16 offset,
					     u8 shift, u8 width,
					     u8 clk_divider_flags,
					     const struct clk_div_table *table,
					     spinlock_t *lock)
{
	struct clk_wzrd_divider *div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_wzrd_clk_divider_ops;
	init.flags = flags;
	init.parent_names =  &parent_name;
	init.num_parents =  1;

	div->base = base;
	div->offset = offset;
	div->shift = shift;
	div->width = width;
	div->flags = clk_divider_flags;
	div->lock = lock;
	div->hw.init = &init;
	div->table = table;

	hw = &div->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		hw = ERR_PTR(ret);

	return hw->clk;
}

static int clk_wzrd_clk_notifier(struct notifier_block *nb, unsigned long event,
				 void *data)
{
	unsigned long max;
	struct clk_notifier_data *ndata = data;
	struct clk_wzrd *clk_wzrd = to_clk_wzrd(nb);

	if (clk_wzrd->suspended)
		return NOTIFY_OK;

	if (ndata->clk == clk_wzrd->clk_in1)
		max = clk_wzrd_max_freq[clk_wzrd->speed_grade - 1];
	else if (ndata->clk == clk_wzrd->axi_clk)
		max = WZRD_ACLK_MAX_FREQ;
	else
		return NOTIFY_DONE;	/* should never happen */

	switch (event) {
	case PRE_RATE_CHANGE:
		if (ndata->new_rate > max)
			return NOTIFY_BAD;
		return NOTIFY_OK;
	case POST_RATE_CHANGE:
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

static int __maybe_unused clk_wzrd_suspend(struct device *dev)
{
	struct clk_wzrd *clk_wzrd = dev_get_drvdata(dev);

	clk_disable_unprepare(clk_wzrd->axi_clk);
	clk_wzrd->suspended = true;

	return 0;
}

static int __maybe_unused clk_wzrd_resume(struct device *dev)
{
	int ret;
	struct clk_wzrd *clk_wzrd = dev_get_drvdata(dev);

	ret = clk_prepare_enable(clk_wzrd->axi_clk);
	if (ret) {
		dev_err(dev, "unable to enable s_axi_aclk\n");
		return ret;
	}

	clk_wzrd->suspended = false;

	return 0;
}

static SIMPLE_DEV_PM_OPS(clk_wzrd_dev_pm_ops, clk_wzrd_suspend,
			 clk_wzrd_resume);

static int clk_wzrd_probe(struct platform_device *pdev)
{
	int i, ret;
	u32 reg, reg_f, mult;
	unsigned long rate;
	const char *clk_name;
	void __iomem *ctrl_reg;
	struct clk_wzrd *clk_wzrd;
	struct device_node *np = pdev->dev.of_node;
	int nr_outputs;
	unsigned long flags = 0;

	clk_wzrd = devm_kzalloc(&pdev->dev, sizeof(*clk_wzrd), GFP_KERNEL);
	if (!clk_wzrd)
		return -ENOMEM;
	platform_set_drvdata(pdev, clk_wzrd);

	clk_wzrd->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(clk_wzrd->base))
		return PTR_ERR(clk_wzrd->base);

	ret = of_property_read_u32(np, "xlnx,speed-grade", &clk_wzrd->speed_grade);
	if (!ret) {
		if (clk_wzrd->speed_grade < 1 || clk_wzrd->speed_grade > 3) {
			dev_warn(&pdev->dev, "invalid speed grade '%d'\n",
				 clk_wzrd->speed_grade);
			clk_wzrd->speed_grade = 0;
		}
	}

	clk_wzrd->clk_in1 = devm_clk_get(&pdev->dev, "clk_in1");
	if (IS_ERR(clk_wzrd->clk_in1))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk_wzrd->clk_in1),
				     "clk_in1 not found\n");

	clk_wzrd->axi_clk = devm_clk_get(&pdev->dev, "s_axi_aclk");
	if (IS_ERR(clk_wzrd->axi_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk_wzrd->axi_clk),
				     "s_axi_aclk not found\n");
	ret = clk_prepare_enable(clk_wzrd->axi_clk);
	if (ret) {
		dev_err(&pdev->dev, "enabling s_axi_aclk failed\n");
		return ret;
	}
	rate = clk_get_rate(clk_wzrd->axi_clk);
	if (rate > WZRD_ACLK_MAX_FREQ) {
		dev_err(&pdev->dev, "s_axi_aclk frequency (%lu) too high\n",
			rate);
		ret = -EINVAL;
		goto err_disable_clk;
	}

	reg = readl(clk_wzrd->base + WZRD_CLK_CFG_REG(0));
	reg_f = reg & WZRD_CLKFBOUT_FRAC_MASK;
	reg_f =  reg_f >> WZRD_CLKFBOUT_FRAC_SHIFT;

	reg = reg & WZRD_CLKFBOUT_MULT_MASK;
	reg =  reg >> WZRD_CLKFBOUT_MULT_SHIFT;
	mult = (reg * 1000) + reg_f;
	clk_name = kasprintf(GFP_KERNEL, "%s_mul", dev_name(&pdev->dev));
	if (!clk_name) {
		ret = -ENOMEM;
		goto err_disable_clk;
	}

	ret = of_property_read_u32(np, "xlnx,nr-outputs", &nr_outputs);
	if (ret || nr_outputs > WZRD_NUM_OUTPUTS) {
		ret = -EINVAL;
		goto err_disable_clk;
	}
	if (nr_outputs == 1)
		flags = CLK_SET_RATE_PARENT;

	clk_wzrd->clks_internal[wzrd_clk_mul] = clk_register_fixed_factor
			(&pdev->dev, clk_name,
			 __clk_get_name(clk_wzrd->clk_in1),
			0, mult, 1000);
	if (IS_ERR(clk_wzrd->clks_internal[wzrd_clk_mul])) {
		dev_err(&pdev->dev, "unable to register fixed-factor clock\n");
		ret = PTR_ERR(clk_wzrd->clks_internal[wzrd_clk_mul]);
		goto err_disable_clk;
	}

	clk_name = kasprintf(GFP_KERNEL, "%s_mul_div", dev_name(&pdev->dev));
	if (!clk_name) {
		ret = -ENOMEM;
		goto err_rm_int_clk;
	}

	ctrl_reg = clk_wzrd->base + WZRD_CLK_CFG_REG(0);
	/* register div */
	clk_wzrd->clks_internal[wzrd_clk_mul_div] = clk_register_divider
			(&pdev->dev, clk_name,
			 __clk_get_name(clk_wzrd->clks_internal[wzrd_clk_mul]),
			flags, ctrl_reg, 0, 8, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &clkwzrd_lock);
	if (IS_ERR(clk_wzrd->clks_internal[wzrd_clk_mul_div])) {
		dev_err(&pdev->dev, "unable to register divider clock\n");
		ret = PTR_ERR(clk_wzrd->clks_internal[wzrd_clk_mul_div]);
		goto err_rm_int_clk;
	}

	/* register div per output */
	for (i = nr_outputs - 1; i >= 0 ; i--) {
		const char *clkout_name;

		clkout_name = kasprintf(GFP_KERNEL, "%s_out%d", dev_name(&pdev->dev), i);
		if (!clkout_name) {
			ret = -ENOMEM;
			goto err_rm_int_clk;
		}

		if (!i)
			clk_wzrd->clkout[i] = clk_wzrd_register_divf
				(&pdev->dev, clkout_name,
				clk_name, flags,
				clk_wzrd->base, (WZRD_CLK_CFG_REG(2) + i * 12),
				WZRD_CLKOUT_DIVIDE_SHIFT,
				WZRD_CLKOUT_DIVIDE_WIDTH,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
				NULL, &clkwzrd_lock);
		else
			clk_wzrd->clkout[i] = clk_wzrd_register_divider
				(&pdev->dev, clkout_name,
				clk_name, 0,
				clk_wzrd->base, (WZRD_CLK_CFG_REG(2) + i * 12),
				WZRD_CLKOUT_DIVIDE_SHIFT,
				WZRD_CLKOUT_DIVIDE_WIDTH,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
				NULL, &clkwzrd_lock);
		if (IS_ERR(clk_wzrd->clkout[i])) {
			int j;

			for (j = i + 1; j < nr_outputs; j++)
				clk_unregister(clk_wzrd->clkout[j]);
			dev_err(&pdev->dev,
				"unable to register divider clock\n");
			ret = PTR_ERR(clk_wzrd->clkout[i]);
			goto err_rm_int_clks;
		}
	}

	kfree(clk_name);

	clk_wzrd->clk_data.clks = clk_wzrd->clkout;
	clk_wzrd->clk_data.clk_num = ARRAY_SIZE(clk_wzrd->clkout);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_wzrd->clk_data);

	if (clk_wzrd->speed_grade) {
		clk_wzrd->nb.notifier_call = clk_wzrd_clk_notifier;

		ret = clk_notifier_register(clk_wzrd->clk_in1,
					    &clk_wzrd->nb);
		if (ret)
			dev_warn(&pdev->dev,
				 "unable to register clock notifier\n");

		ret = clk_notifier_register(clk_wzrd->axi_clk, &clk_wzrd->nb);
		if (ret)
			dev_warn(&pdev->dev,
				 "unable to register clock notifier\n");
	}

	return 0;

err_rm_int_clks:
	clk_unregister(clk_wzrd->clks_internal[1]);
err_rm_int_clk:
	kfree(clk_name);
	clk_unregister(clk_wzrd->clks_internal[0]);
err_disable_clk:
	clk_disable_unprepare(clk_wzrd->axi_clk);

	return ret;
}

static int clk_wzrd_remove(struct platform_device *pdev)
{
	int i;
	struct clk_wzrd *clk_wzrd = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);

	for (i = 0; i < WZRD_NUM_OUTPUTS; i++)
		clk_unregister(clk_wzrd->clkout[i]);
	for (i = 0; i < wzrd_clk_int_max; i++)
		clk_unregister(clk_wzrd->clks_internal[i]);

	if (clk_wzrd->speed_grade) {
		clk_notifier_unregister(clk_wzrd->axi_clk, &clk_wzrd->nb);
		clk_notifier_unregister(clk_wzrd->clk_in1, &clk_wzrd->nb);
	}

	clk_disable_unprepare(clk_wzrd->axi_clk);

	return 0;
}

static const struct of_device_id clk_wzrd_ids[] = {
	{ .compatible = "xlnx,clocking-wizard" },
	{ .compatible = "xlnx,clocking-wizard-v5.2" },
	{ .compatible = "xlnx,clocking-wizard-v6.0" },
	{ },
};
MODULE_DEVICE_TABLE(of, clk_wzrd_ids);

static struct platform_driver clk_wzrd_driver = {
	.driver = {
		.name = "clk-wizard",
		.of_match_table = clk_wzrd_ids,
		.pm = &clk_wzrd_dev_pm_ops,
	},
	.probe = clk_wzrd_probe,
	.remove = clk_wzrd_remove,
};
module_platform_driver(clk_wzrd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Brinkmann <soren.brinkmann@xilinx.com");
MODULE_DESCRIPTION("Driver for the Xilinx Clocking Wizard IP core");
