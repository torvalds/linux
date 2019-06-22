// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARTPEC-6 clock initialization
 *
 * Copyright 2015-2016 Axis Comunications AB.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <dt-bindings/clock/axis,artpec6-clkctrl.h>

#define NUM_I2S_CLOCKS 2

struct artpec6_clkctrl_drvdata {
	struct clk *clk_table[ARTPEC6_CLK_NUMCLOCKS];
	void __iomem *syscon_base;
	struct clk_onecell_data clk_data;
	spinlock_t i2scfg_lock;
};

static struct artpec6_clkctrl_drvdata *clkdata;

static const char *const i2s_clk_names[NUM_I2S_CLOCKS] = {
	"i2s0",
	"i2s1",
};

static const int i2s_clk_indexes[NUM_I2S_CLOCKS] = {
	ARTPEC6_CLK_I2S0_CLK,
	ARTPEC6_CLK_I2S1_CLK,
};

static void of_artpec6_clkctrl_setup(struct device_node *np)
{
	int i;
	const char *sys_refclk_name;
	u32 pll_mode, pll_m, pll_n;
	struct clk **clks;

	/* Mandatory parent clock. */
	i = of_property_match_string(np, "clock-names", "sys_refclk");
	if (i < 0)
		return;

	sys_refclk_name = of_clk_get_parent_name(np, i);

	clkdata = kzalloc(sizeof(*clkdata), GFP_KERNEL);
	if (!clkdata)
		return;

	clks = clkdata->clk_table;

	for (i = 0; i < ARTPEC6_CLK_NUMCLOCKS; ++i)
		clks[i] = ERR_PTR(-EPROBE_DEFER);

	clkdata->syscon_base = of_iomap(np, 0);
	BUG_ON(clkdata->syscon_base == NULL);

	/* Read PLL1 factors configured by boot strap pins. */
	pll_mode = (readl(clkdata->syscon_base) >> 6) & 3;
	switch (pll_mode) {
	case 0:		/* DDR3-2133 mode */
		pll_m = 4;
		pll_n = 85;
		break;
	case 1:		/* DDR3-1866 mode */
		pll_m = 6;
		pll_n = 112;
		break;
	case 2:		/* DDR3-1600 mode */
		pll_m = 4;
		pll_n = 64;
		break;
	case 3:		/* DDR3-1333 mode */
		pll_m = 8;
		pll_n = 106;
		break;
	}

	clks[ARTPEC6_CLK_CPU] =
	    clk_register_fixed_factor(NULL, "cpu", sys_refclk_name, 0, pll_n,
				      pll_m);
	clks[ARTPEC6_CLK_CPU_PERIPH] =
	    clk_register_fixed_factor(NULL, "cpu_periph", "cpu", 0, 1, 2);

	/* EPROBE_DEFER on the apb_clock is not handled in amba devices. */
	clks[ARTPEC6_CLK_UART_PCLK] =
	    clk_register_fixed_factor(NULL, "uart_pclk", "cpu", 0, 1, 8);
	clks[ARTPEC6_CLK_UART_REFCLK] =
	    clk_register_fixed_rate(NULL, "uart_ref", sys_refclk_name, 0,
				    50000000);

	clks[ARTPEC6_CLK_SPI_PCLK] =
	    clk_register_fixed_factor(NULL, "spi_pclk", "cpu", 0, 1, 8);
	clks[ARTPEC6_CLK_SPI_SSPCLK] =
	    clk_register_fixed_rate(NULL, "spi_sspclk", sys_refclk_name, 0,
				    50000000);

	clks[ARTPEC6_CLK_DBG_PCLK] =
	    clk_register_fixed_factor(NULL, "dbg_pclk", "cpu", 0, 1, 8);

	clkdata->clk_data.clks = clkdata->clk_table;
	clkdata->clk_data.clk_num = ARTPEC6_CLK_NUMCLOCKS;

	of_clk_add_provider(np, of_clk_src_onecell_get, &clkdata->clk_data);
}

CLK_OF_DECLARE_DRIVER(artpec6_clkctrl, "axis,artpec6-clkctrl",
		      of_artpec6_clkctrl_setup);

static int artpec6_clkctrl_probe(struct platform_device *pdev)
{
	int propidx;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct clk **clks = clkdata->clk_table;
	const char *sys_refclk_name;
	const char *i2s_refclk_name = NULL;
	const char *frac_clk_name[2] = { NULL, NULL };
	const char *i2s_mux_parents[2];
	u32 muxreg;
	int i;
	int err = 0;

	/* Mandatory parent clock. */
	propidx = of_property_match_string(np, "clock-names", "sys_refclk");
	if (propidx < 0)
		return -EINVAL;

	sys_refclk_name = of_clk_get_parent_name(np, propidx);

	/* Find clock names of optional parent clocks. */
	propidx = of_property_match_string(np, "clock-names", "i2s_refclk");
	if (propidx >= 0)
		i2s_refclk_name = of_clk_get_parent_name(np, propidx);

	propidx = of_property_match_string(np, "clock-names", "frac_clk0");
	if (propidx >= 0)
		frac_clk_name[0] = of_clk_get_parent_name(np, propidx);
	propidx = of_property_match_string(np, "clock-names", "frac_clk1");
	if (propidx >= 0)
		frac_clk_name[1] = of_clk_get_parent_name(np, propidx);

	spin_lock_init(&clkdata->i2scfg_lock);

	clks[ARTPEC6_CLK_NAND_CLKA] =
	    clk_register_fixed_factor(dev, "nand_clka", "cpu", 0, 1, 8);
	clks[ARTPEC6_CLK_NAND_CLKB] =
	    clk_register_fixed_rate(dev, "nand_clkb", sys_refclk_name, 0,
				    100000000);
	clks[ARTPEC6_CLK_ETH_ACLK] =
	    clk_register_fixed_factor(dev, "eth_aclk", "cpu", 0, 1, 4);
	clks[ARTPEC6_CLK_DMA_ACLK] =
	    clk_register_fixed_factor(dev, "dma_aclk", "cpu", 0, 1, 4);
	clks[ARTPEC6_CLK_PTP_REF] =
	    clk_register_fixed_rate(dev, "ptp_ref", sys_refclk_name, 0,
				    100000000);
	clks[ARTPEC6_CLK_SD_PCLK] =
	    clk_register_fixed_rate(dev, "sd_pclk", sys_refclk_name, 0,
				    100000000);
	clks[ARTPEC6_CLK_SD_IMCLK] =
	    clk_register_fixed_rate(dev, "sd_imclk", sys_refclk_name, 0,
				    100000000);
	clks[ARTPEC6_CLK_I2S_HST] =
	    clk_register_fixed_factor(dev, "i2s_hst", "cpu", 0, 1, 8);

	for (i = 0; i < NUM_I2S_CLOCKS; ++i) {
		if (i2s_refclk_name && frac_clk_name[i]) {
			i2s_mux_parents[0] = frac_clk_name[i];
			i2s_mux_parents[1] = i2s_refclk_name;

			clks[i2s_clk_indexes[i]] =
			    clk_register_mux(dev, i2s_clk_names[i],
					     i2s_mux_parents, 2,
					     CLK_SET_RATE_NO_REPARENT |
					     CLK_SET_RATE_PARENT,
					     clkdata->syscon_base + 0x14, i, 1,
					     0, &clkdata->i2scfg_lock);
		} else if (frac_clk_name[i]) {
			/* Lock the mux for internal clock reference. */
			muxreg = readl(clkdata->syscon_base + 0x14);
			muxreg &= ~BIT(i);
			writel(muxreg, clkdata->syscon_base + 0x14);
			clks[i2s_clk_indexes[i]] =
			    clk_register_fixed_factor(dev, i2s_clk_names[i],
						      frac_clk_name[i], 0, 1,
						      1);
		} else if (i2s_refclk_name) {
			/* Lock the mux for external clock reference. */
			muxreg = readl(clkdata->syscon_base + 0x14);
			muxreg |= BIT(i);
			writel(muxreg, clkdata->syscon_base + 0x14);
			clks[i2s_clk_indexes[i]] =
			    clk_register_fixed_factor(dev, i2s_clk_names[i],
						      i2s_refclk_name, 0, 1, 1);
		}
	}

	clks[ARTPEC6_CLK_I2C] =
	    clk_register_fixed_rate(dev, "i2c", sys_refclk_name, 0, 100000000);

	clks[ARTPEC6_CLK_SYS_TIMER] =
	    clk_register_fixed_rate(dev, "timer", sys_refclk_name, 0,
				    100000000);
	clks[ARTPEC6_CLK_FRACDIV_IN] =
	    clk_register_fixed_rate(dev, "fracdiv_in", sys_refclk_name, 0,
				    600000000);

	for (i = 0; i < ARTPEC6_CLK_NUMCLOCKS; ++i) {
		if (IS_ERR(clks[i]) && PTR_ERR(clks[i]) != -EPROBE_DEFER) {
			dev_err(dev,
				"Failed to register clock at index %d err=%ld\n",
				i, PTR_ERR(clks[i]));
			err = PTR_ERR(clks[i]);
		}
	}

	return err;
}

static const struct of_device_id artpec_clkctrl_of_match[] = {
	{ .compatible = "axis,artpec6-clkctrl" },
	{}
};

static struct platform_driver artpec6_clkctrl_driver = {
	.probe = artpec6_clkctrl_probe,
	.driver = {
		   .name = "artpec6_clkctrl",
		   .of_match_table = artpec_clkctrl_of_match,
	},
};

builtin_platform_driver(artpec6_clkctrl_driver);
