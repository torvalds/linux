// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2019 Christoph Hellwig.
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 */
#include <linux/types.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/bitfield.h>
#include <asm/soc.h>

#include <soc/canaan/k210-sysctl.h>

#define K210_SYSCTL_CLK0_FREQ		26000000UL

/* Registers base address */
#define K210_SYSCTL_SYSCTL_BASE_ADDR	0x50440000ULL

/* Register bits */
/* K210_SYSCTL_PLL1: clkr: 4bits, clkf1: 6bits, clkod: 4bits, bwadj: 4bits */
#define PLL_RESET		(1 << 20)
#define PLL_PWR			(1 << 21)
#define PLL_BYPASS		(1 << 23)
#define PLL_OUT_EN		(1 << 25)
/* K210_SYSCTL_PLL_LOCK */
#define PLL1_LOCK1		(1 << 8)
#define PLL1_LOCK2		(1 << 9)
#define PLL1_SLIP_CLEAR		(1 << 10)
/* K210_SYSCTL_SEL0 */
#define CLKSEL_ACLK		(1 << 0)
/* K210_SYSCTL_CLKEN_CENT */
#define CLKEN_CPU		(1 << 0)
#define CLKEN_SRAM0		(1 << 1)
#define CLKEN_SRAM1		(1 << 2)
/* K210_SYSCTL_EN_PERI */
#define CLKEN_ROM		(1 << 0)
#define CLKEN_TIMER0		(1 << 21)
#define CLKEN_RTC		(1 << 29)

struct k210_sysctl {
	void __iomem		*regs;
	struct clk_hw		hw;
};

static void k210_set_bits(u32 val, void __iomem *reg)
{
	writel(readl(reg) | val, reg);
}

static void k210_clear_bits(u32 val, void __iomem *reg)
{
	writel(readl(reg) & ~val, reg);
}

static void k210_pll1_enable(void __iomem *regs)
{
	u32 val;

	val = readl(regs + K210_SYSCTL_PLL1);
	val &= ~GENMASK(19, 0);				/* clkr1 = 0 */
	val |= FIELD_PREP(GENMASK(9, 4), 0x3B);		/* clkf1 = 59 */
	val |= FIELD_PREP(GENMASK(13, 10), 0x3);	/* clkod1 = 3 */
	val |= FIELD_PREP(GENMASK(19, 14), 0x3B);	/* bwadj1 = 59 */
	writel(val, regs + K210_SYSCTL_PLL1);

	k210_clear_bits(PLL_BYPASS, regs + K210_SYSCTL_PLL1);
	k210_set_bits(PLL_PWR, regs + K210_SYSCTL_PLL1);

	/*
	 * Reset the pll. The magic NOPs come from the Kendryte reference SDK.
	 */
	k210_clear_bits(PLL_RESET, regs + K210_SYSCTL_PLL1);
	k210_set_bits(PLL_RESET, regs + K210_SYSCTL_PLL1);
	nop();
	nop();
	k210_clear_bits(PLL_RESET, regs + K210_SYSCTL_PLL1);

	for (;;) {
		val = readl(regs + K210_SYSCTL_PLL_LOCK);
		if (val & PLL1_LOCK2)
			break;
		writel(val | PLL1_SLIP_CLEAR, regs + K210_SYSCTL_PLL_LOCK);
	}

	k210_set_bits(PLL_OUT_EN, regs + K210_SYSCTL_PLL1);
}

static unsigned long k210_sysctl_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct k210_sysctl *s = container_of(hw, struct k210_sysctl, hw);
	u32 clksel0, pll0;
	u64 pll0_freq, clkr0, clkf0, clkod0;

	/*
	 * If the clock selector is not set, use the base frequency.
	 * Otherwise, use PLL0 frequency with a frequency divisor.
	 */
	clksel0 = readl(s->regs + K210_SYSCTL_SEL0);
	if (!(clksel0 & CLKSEL_ACLK))
		return K210_SYSCTL_CLK0_FREQ;

	/*
	 * Get PLL0 frequency:
	 * freq = base frequency * clkf0 / (clkr0 * clkod0)
	 */
	pll0 = readl(s->regs + K210_SYSCTL_PLL0);
	clkr0 = 1 + FIELD_GET(GENMASK(3, 0), pll0);
	clkf0 = 1 + FIELD_GET(GENMASK(9, 4), pll0);
	clkod0 = 1 + FIELD_GET(GENMASK(13, 10), pll0);
	pll0_freq = clkf0 * K210_SYSCTL_CLK0_FREQ / (clkr0 * clkod0);

	/* Get the frequency divisor from the clock selector */
	return pll0_freq / (2ULL << FIELD_GET(0x00000006, clksel0));
}

static const struct clk_ops k210_sysctl_clk_ops = {
	.recalc_rate	= k210_sysctl_clk_recalc_rate,
};

static const struct clk_init_data k210_clk_init_data = {
	.name		= "k210-sysctl-pll1",
	.ops		= &k210_sysctl_clk_ops,
};

static int k210_sysctl_probe(struct platform_device *pdev)
{
	struct k210_sysctl *s;
	int error;

	pr_info("Kendryte K210 SoC sysctl\n");

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->regs = devm_ioremap_resource(&pdev->dev,
			platform_get_resource(pdev, IORESOURCE_MEM, 0));
	if (IS_ERR(s->regs))
		return PTR_ERR(s->regs);

	s->hw.init = &k210_clk_init_data;
	error = devm_clk_hw_register(&pdev->dev, &s->hw);
	if (error) {
		dev_err(&pdev->dev, "failed to register clk");
		return error;
	}

	error = devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_simple_get,
					    &s->hw);
	if (error) {
		dev_err(&pdev->dev, "adding clk provider failed\n");
		return error;
	}

	return 0;
}

static const struct of_device_id k210_sysctl_of_match[] = {
	{ .compatible = "kendryte,k210-sysctl", },
	{}
};

static struct platform_driver k210_sysctl_driver = {
	.driver	= {
		.name		= "k210-sysctl",
		.of_match_table	= k210_sysctl_of_match,
	},
	.probe			= k210_sysctl_probe,
};

static int __init k210_sysctl_init(void)
{
	return platform_driver_register(&k210_sysctl_driver);
}
core_initcall(k210_sysctl_init);

/*
 * This needs to be called very early during initialization, given that
 * PLL1 needs to be enabled to be able to use all SRAM.
 */
static void __init k210_soc_early_init(const void *fdt)
{
	void __iomem *regs;

	regs = ioremap(K210_SYSCTL_SYSCTL_BASE_ADDR, 0x1000);
	if (!regs)
		panic("K210 sysctl ioremap");

	/* Enable PLL1 to make the KPU SRAM useable */
	k210_pll1_enable(regs);

	k210_set_bits(PLL_OUT_EN, regs + K210_SYSCTL_PLL0);

	k210_set_bits(CLKEN_CPU | CLKEN_SRAM0 | CLKEN_SRAM1,
		      regs + K210_SYSCTL_EN_CENT);
	k210_set_bits(CLKEN_ROM | CLKEN_TIMER0 | CLKEN_RTC,
		      regs + K210_SYSCTL_EN_PERI);

	k210_set_bits(CLKSEL_ACLK, regs + K210_SYSCTL_SEL0);

	iounmap(regs);
}
SOC_EARLY_INIT_DECLARE(generic_k210, "kendryte,k210", k210_soc_early_init);
