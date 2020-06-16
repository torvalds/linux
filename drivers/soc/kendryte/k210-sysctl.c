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

#define K210_SYSCTL_CLK0_FREQ		26000000UL

/* Registers base address */
#define K210_SYSCTL_SYSCTL_BASE_ADDR	0x50440000ULL

/* Registers */
#define K210_SYSCTL_PLL0		0x08
#define K210_SYSCTL_PLL1		0x0c
/* clkr: 4bits, clkf1: 6bits, clkod: 4bits, bwadj: 4bits */
#define   PLL_RESET		(1 << 20)
#define   PLL_PWR		(1 << 21)
#define   PLL_INTFB		(1 << 22)
#define   PLL_BYPASS		(1 << 23)
#define   PLL_TEST		(1 << 24)
#define   PLL_OUT_EN		(1 << 25)
#define   PLL_TEST_EN		(1 << 26)
#define K210_SYSCTL_PLL_LOCK		0x18
#define   PLL0_LOCK1		(1 << 0)
#define   PLL0_LOCK2		(1 << 1)
#define   PLL0_SLIP_CLEAR	(1 << 2)
#define   PLL0_TEST_CLK_OUT	(1 << 3)
#define   PLL1_LOCK1		(1 << 8)
#define   PLL1_LOCK2		(1 << 9)
#define   PLL1_SLIP_CLEAR	(1 << 10)
#define   PLL1_TEST_CLK_OUT	(1 << 11)
#define   PLL2_LOCK1		(1 << 16)
#define   PLL2_LOCK2		(1 << 16)
#define   PLL2_SLIP_CLEAR	(1 << 18)
#define   PLL2_TEST_CLK_OUT	(1 << 19)
#define K210_SYSCTL_CLKSEL0	0x20
#define   CLKSEL_ACLK		(1 << 0)
#define K210_SYSCTL_CLKEN_CENT		0x28
#define   CLKEN_CPU		(1 << 0)
#define   CLKEN_SRAM0		(1 << 1)
#define   CLKEN_SRAM1		(1 << 2)
#define   CLKEN_APB0		(1 << 3)
#define   CLKEN_APB1		(1 << 4)
#define   CLKEN_APB2		(1 << 5)
#define K210_SYSCTL_CLKEN_PERI		0x2c
#define   CLKEN_ROM		(1 << 0)
#define   CLKEN_DMA		(1 << 1)
#define   CLKEN_AI		(1 << 2)
#define   CLKEN_DVP		(1 << 3)
#define   CLKEN_FFT		(1 << 4)
#define   CLKEN_GPIO		(1 << 5)
#define   CLKEN_SPI0		(1 << 6)
#define   CLKEN_SPI1		(1 << 7)
#define   CLKEN_SPI2		(1 << 8)
#define   CLKEN_SPI3		(1 << 9)
#define   CLKEN_I2S0		(1 << 10)
#define   CLKEN_I2S1		(1 << 11)
#define   CLKEN_I2S2		(1 << 12)
#define   CLKEN_I2C0		(1 << 13)
#define   CLKEN_I2C1		(1 << 14)
#define   CLKEN_I2C2		(1 << 15)
#define   CLKEN_UART1		(1 << 16)
#define   CLKEN_UART2		(1 << 17)
#define   CLKEN_UART3		(1 << 18)
#define   CLKEN_AES		(1 << 19)
#define   CLKEN_FPIO		(1 << 20)
#define   CLKEN_TIMER0		(1 << 21)
#define   CLKEN_TIMER1		(1 << 22)
#define   CLKEN_TIMER2		(1 << 23)
#define   CLKEN_WDT0		(1 << 24)
#define   CLKEN_WDT1		(1 << 25)
#define   CLKEN_SHA		(1 << 26)
#define   CLKEN_OTP		(1 << 27)
#define   CLKEN_RTC		(1 << 29)

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
	clksel0 = readl(s->regs + K210_SYSCTL_CLKSEL0);
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
		      regs + K210_SYSCTL_CLKEN_CENT);
	k210_set_bits(CLKEN_ROM | CLKEN_TIMER0 | CLKEN_RTC,
		      regs + K210_SYSCTL_CLKEN_PERI);

	k210_set_bits(CLKSEL_ACLK, regs + K210_SYSCTL_CLKSEL0);

	iounmap(regs);
}
SOC_EARLY_INIT_DECLARE(generic_k210, "kendryte,k210", k210_soc_early_init);

#ifdef CONFIG_SOC_KENDRYTE_K210_DTB_BUILTIN
/*
 * Generic entry for the default k210.dtb embedded DTB for boards with:
 *   - Vendor ID: 0x4B5
 *   - Arch ID: 0xE59889E6A5A04149 (= "Canaan AI" in UTF-8 encoded Chinese)
 *   - Impl ID:	0x4D41495832303030 (= "MAIX2000")
 * These values are reported by the SiPEED MAXDUINO, SiPEED MAIX GO and
 * SiPEED Dan dock boards.
 */
SOC_BUILTIN_DTB_DECLARE(k210, 0x4B5, 0xE59889E6A5A04149, 0x4D41495832303030);
#endif
