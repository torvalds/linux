/*
 * Clock definitions for u8540 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/platform_data/clk-ux500.h>
#include "clk.h"

static const struct of_device_id u8540_clk_of_match[] = {
	{ .compatible = "stericsson,u8540-clks", },
	{ }
};

/* CLKRST4 is missing making it hard to index things */
enum clkrst_index {
	CLKRST1_INDEX = 0,
	CLKRST2_INDEX,
	CLKRST3_INDEX,
	CLKRST5_INDEX,
	CLKRST6_INDEX,
	CLKRST_MAX,
};

void u8540_clk_init(void)
{
	struct clk *clk;
	struct device_node *np = NULL;
	u32 bases[CLKRST_MAX];
	int i;

	if (of_have_populated_dt())
		np = of_find_matching_node(NULL, u8540_clk_of_match);
	if (!np) {
		pr_err("Either DT or U8540 Clock node not found\n");
		return;
	}
	for (i = 0; i < ARRAY_SIZE(bases); i++) {
		struct resource r;

		if (of_address_to_resource(np, i, &r))
			/* Not much choice but to continue */
			pr_err("failed to get CLKRST %d base address\n",
			       i + 1);
		bases[i] = r.start;
	}

	/* Clock sources. */
	/* Fixed ClockGen */
	clk = clk_reg_prcmu_gate("soc0_pll", NULL, PRCMU_PLLSOC0,
				CLK_IGNORE_UNUSED);
	clk_register_clkdev(clk, "soc0_pll", NULL);

	clk = clk_reg_prcmu_gate("soc1_pll", NULL, PRCMU_PLLSOC1,
				CLK_IGNORE_UNUSED);
	clk_register_clkdev(clk, "soc1_pll", NULL);

	clk = clk_reg_prcmu_gate("ddr_pll", NULL, PRCMU_PLLDDR,
				CLK_IGNORE_UNUSED);
	clk_register_clkdev(clk, "ddr_pll", NULL);

	clk = clk_register_fixed_rate(NULL, "rtc32k", NULL,
				CLK_IGNORE_UNUSED,
				32768);
	clk_register_clkdev(clk, "clk32k", NULL);
	clk_register_clkdev(clk, "apb_pclk", "rtc-pl031");

	clk = clk_register_fixed_rate(NULL, "ulp38m4", NULL,
				CLK_IGNORE_UNUSED,
				38400000);

	clk = clk_reg_prcmu_gate("uartclk", NULL, PRCMU_UARTCLK, 0);
	clk_register_clkdev(clk, NULL, "UART");

	/* msp02clk needs a abx500 clk as parent. Handle by abx500 clk driver */
	clk = clk_reg_prcmu_gate("msp02clk", "ab9540_sysclk12_b1",
			PRCMU_MSP02CLK, 0);
	clk_register_clkdev(clk, NULL, "MSP02");

	clk = clk_reg_prcmu_gate("msp1clk", NULL, PRCMU_MSP1CLK, 0);
	clk_register_clkdev(clk, NULL, "MSP1");

	clk = clk_reg_prcmu_gate("i2cclk", NULL, PRCMU_I2CCLK, 0);
	clk_register_clkdev(clk, NULL, "I2C");

	clk = clk_reg_prcmu_gate("slimclk", NULL, PRCMU_SLIMCLK, 0);
	clk_register_clkdev(clk, NULL, "slim");

	clk = clk_reg_prcmu_gate("per1clk", NULL, PRCMU_PER1CLK, 0);
	clk_register_clkdev(clk, NULL, "PERIPH1");

	clk = clk_reg_prcmu_gate("per2clk", NULL, PRCMU_PER2CLK, 0);
	clk_register_clkdev(clk, NULL, "PERIPH2");

	clk = clk_reg_prcmu_gate("per3clk", NULL, PRCMU_PER3CLK, 0);
	clk_register_clkdev(clk, NULL, "PERIPH3");

	clk = clk_reg_prcmu_gate("per5clk", NULL, PRCMU_PER5CLK, 0);
	clk_register_clkdev(clk, NULL, "PERIPH5");

	clk = clk_reg_prcmu_gate("per6clk", NULL, PRCMU_PER6CLK, 0);
	clk_register_clkdev(clk, NULL, "PERIPH6");

	clk = clk_reg_prcmu_gate("per7clk", NULL, PRCMU_PER7CLK, 0);
	clk_register_clkdev(clk, NULL, "PERIPH7");

	clk = clk_reg_prcmu_scalable("lcdclk", NULL, PRCMU_LCDCLK, 0,
				CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "lcd");
	clk_register_clkdev(clk, "lcd", "mcde");

	clk = clk_reg_prcmu_opp_gate("bmlclk", NULL, PRCMU_BMLCLK, 0);
	clk_register_clkdev(clk, NULL, "bml");

	clk = clk_reg_prcmu_scalable("hsitxclk", NULL, PRCMU_HSITXCLK, 0,
				     CLK_SET_RATE_GATE);

	clk = clk_reg_prcmu_scalable("hsirxclk", NULL, PRCMU_HSIRXCLK, 0,
				     CLK_SET_RATE_GATE);

	clk = clk_reg_prcmu_scalable("hdmiclk", NULL, PRCMU_HDMICLK, 0,
				     CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "hdmi");
	clk_register_clkdev(clk, "hdmi", "mcde");

	clk = clk_reg_prcmu_gate("apeatclk", NULL, PRCMU_APEATCLK, 0);
	clk_register_clkdev(clk, NULL, "apeat");

	clk = clk_reg_prcmu_gate("apetraceclk", NULL, PRCMU_APETRACECLK, 0);
	clk_register_clkdev(clk, NULL, "apetrace");

	clk = clk_reg_prcmu_gate("mcdeclk", NULL, PRCMU_MCDECLK, 0);
	clk_register_clkdev(clk, NULL, "mcde");
	clk_register_clkdev(clk, "mcde", "mcde");
	clk_register_clkdev(clk, NULL, "dsilink.0");
	clk_register_clkdev(clk, NULL, "dsilink.1");
	clk_register_clkdev(clk, NULL, "dsilink.2");

	clk = clk_reg_prcmu_opp_gate("ipi2cclk", NULL, PRCMU_IPI2CCLK, 0);
	clk_register_clkdev(clk, NULL, "ipi2");

	clk = clk_reg_prcmu_gate("dsialtclk", NULL, PRCMU_DSIALTCLK, 0);
	clk_register_clkdev(clk, NULL, "dsialt");

	clk = clk_reg_prcmu_gate("dmaclk", NULL, PRCMU_DMACLK, 0);
	clk_register_clkdev(clk, NULL, "dma40.0");

	clk = clk_reg_prcmu_gate("b2r2clk", NULL, PRCMU_B2R2CLK, 0);
	clk_register_clkdev(clk, NULL, "b2r2");
	clk_register_clkdev(clk, NULL, "b2r2_core");
	clk_register_clkdev(clk, NULL, "U8500-B2R2.0");
	clk_register_clkdev(clk, NULL, "b2r2_1_core");

	clk = clk_reg_prcmu_scalable("tvclk", NULL, PRCMU_TVCLK, 0,
				     CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "tv");
	clk_register_clkdev(clk, "tv", "mcde");

	clk = clk_reg_prcmu_gate("sspclk", NULL, PRCMU_SSPCLK, 0);
	clk_register_clkdev(clk, NULL, "SSP");

	clk = clk_reg_prcmu_gate("rngclk", NULL, PRCMU_RNGCLK, 0);
	clk_register_clkdev(clk, NULL, "rngclk");

	clk = clk_reg_prcmu_gate("uiccclk", NULL, PRCMU_UICCCLK, 0);
	clk_register_clkdev(clk, NULL, "uicc");

	clk = clk_reg_prcmu_gate("timclk", NULL, PRCMU_TIMCLK, 0);
	clk_register_clkdev(clk, NULL, "mtu0");
	clk_register_clkdev(clk, NULL, "mtu1");

	clk = clk_reg_prcmu_opp_volt_scalable("sdmmcclk", NULL,
					PRCMU_SDMMCCLK, 100000000,
					CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdmmc");

	clk = clk_reg_prcmu_opp_volt_scalable("sdmmchclk", NULL,
					PRCMU_SDMMCHCLK, 400000000,
					CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdmmchclk");

	clk = clk_reg_prcmu_gate("hvaclk", NULL, PRCMU_HVACLK, 0);
	clk_register_clkdev(clk, NULL, "hva");

	clk = clk_reg_prcmu_gate("g1clk", NULL, PRCMU_G1CLK, 0);
	clk_register_clkdev(clk, NULL, "g1");

	clk = clk_reg_prcmu_scalable("spare1clk", NULL, PRCMU_SPARE1CLK, 0,
				     CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsilcd", "mcde");

	clk = clk_reg_prcmu_scalable("dsi_pll", "hdmiclk",
				PRCMU_PLLDSI, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsihs2", "mcde");
	clk_register_clkdev(clk, "hs_clk", "dsilink.2");

	clk = clk_reg_prcmu_scalable("dsilcd_pll", "spare1clk",
				PRCMU_PLLDSI_LCD, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsilcd_pll", "mcde");

	clk = clk_reg_prcmu_scalable("dsi0clk", "dsi_pll",
				PRCMU_DSI0CLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsihs0", "mcde");

	clk = clk_reg_prcmu_scalable("dsi0lcdclk", "dsilcd_pll",
				PRCMU_DSI0CLK_LCD, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsihs0", "mcde");
	clk_register_clkdev(clk, "hs_clk", "dsilink.0");

	clk = clk_reg_prcmu_scalable("dsi1clk", "dsi_pll",
				PRCMU_DSI1CLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsihs1", "mcde");

	clk = clk_reg_prcmu_scalable("dsi1lcdclk", "dsilcd_pll",
				PRCMU_DSI1CLK_LCD, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsihs1", "mcde");
	clk_register_clkdev(clk, "hs_clk", "dsilink.1");

	clk = clk_reg_prcmu_scalable("dsi0escclk", "tvclk",
				PRCMU_DSI0ESCCLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "lp_clk", "dsilink.0");
	clk_register_clkdev(clk, "dsilp0", "mcde");

	clk = clk_reg_prcmu_scalable("dsi1escclk", "tvclk",
				PRCMU_DSI1ESCCLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "lp_clk", "dsilink.1");
	clk_register_clkdev(clk, "dsilp1", "mcde");

	clk = clk_reg_prcmu_scalable("dsi2escclk", "tvclk",
				PRCMU_DSI2ESCCLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "lp_clk", "dsilink.2");
	clk_register_clkdev(clk, "dsilp2", "mcde");

	clk = clk_reg_prcmu_scalable_rate("armss", NULL,
				PRCMU_ARMSS, 0, CLK_IGNORE_UNUSED);
	clk_register_clkdev(clk, "armss", NULL);

	clk = clk_register_fixed_factor(NULL, "smp_twd", "armss",
				CLK_IGNORE_UNUSED, 1, 2);
	clk_register_clkdev(clk, NULL, "smp_twd");

	/* PRCC P-clocks */
	/* Peripheral 1 : PRCC P-clocks */
	clk = clk_reg_prcc_pclk("p1_pclk0", "per1clk", bases[CLKRST1_INDEX],
				BIT(0), 0);
	clk_register_clkdev(clk, "apb_pclk", "uart0");

	clk = clk_reg_prcc_pclk("p1_pclk1", "per1clk", bases[CLKRST1_INDEX],
				BIT(1), 0);
	clk_register_clkdev(clk, "apb_pclk", "uart1");

	clk = clk_reg_prcc_pclk("p1_pclk2", "per1clk", bases[CLKRST1_INDEX],
				BIT(2), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.1");

	clk = clk_reg_prcc_pclk("p1_pclk3", "per1clk", bases[CLKRST1_INDEX],
				BIT(3), 0);
	clk_register_clkdev(clk, "apb_pclk", "msp0");
	clk_register_clkdev(clk, "apb_pclk", "dbx5x0-msp-i2s.0");

	clk = clk_reg_prcc_pclk("p1_pclk4", "per1clk", bases[CLKRST1_INDEX],
				BIT(4), 0);
	clk_register_clkdev(clk, "apb_pclk", "msp1");
	clk_register_clkdev(clk, "apb_pclk", "dbx5x0-msp-i2s.1");

	clk = clk_reg_prcc_pclk("p1_pclk5", "per1clk", bases[CLKRST1_INDEX],
				BIT(5), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi0");

	clk = clk_reg_prcc_pclk("p1_pclk6", "per1clk", bases[CLKRST1_INDEX],
				BIT(6), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.2");

	clk = clk_reg_prcc_pclk("p1_pclk7", "per1clk", bases[CLKRST1_INDEX],
				BIT(7), 0);
	clk_register_clkdev(clk, NULL, "spi3");

	clk = clk_reg_prcc_pclk("p1_pclk8", "per1clk", bases[CLKRST1_INDEX],
				BIT(8), 0);
	clk_register_clkdev(clk, "apb_pclk", "slimbus0");

	clk = clk_reg_prcc_pclk("p1_pclk9", "per1clk", bases[CLKRST1_INDEX],
				BIT(9), 0);
	clk_register_clkdev(clk, NULL, "gpio.0");
	clk_register_clkdev(clk, NULL, "gpio.1");
	clk_register_clkdev(clk, NULL, "gpioblock0");
	clk_register_clkdev(clk, "apb_pclk", "ab85xx-codec.0");

	clk = clk_reg_prcc_pclk("p1_pclk10", "per1clk", bases[CLKRST1_INDEX],
				BIT(10), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.4");

	clk = clk_reg_prcc_pclk("p1_pclk11", "per1clk", bases[CLKRST1_INDEX],
				BIT(11), 0);
	clk_register_clkdev(clk, "apb_pclk", "msp3");
	clk_register_clkdev(clk, "apb_pclk", "dbx5x0-msp-i2s.3");

	/* Peripheral 2 : PRCC P-clocks */
	clk = clk_reg_prcc_pclk("p2_pclk0", "per2clk", bases[CLKRST2_INDEX],
				BIT(0), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.3");

	clk = clk_reg_prcc_pclk("p2_pclk1", "per2clk", bases[CLKRST2_INDEX],
				BIT(1), 0);
	clk_register_clkdev(clk, NULL, "spi2");

	clk = clk_reg_prcc_pclk("p2_pclk2", "per2clk", bases[CLKRST2_INDEX],
				BIT(2), 0);
	clk_register_clkdev(clk, NULL, "spi1");

	clk = clk_reg_prcc_pclk("p2_pclk3", "per2clk", bases[CLKRST2_INDEX],
				BIT(3), 0);
	clk_register_clkdev(clk, NULL, "pwl");

	clk = clk_reg_prcc_pclk("p2_pclk4", "per2clk", bases[CLKRST2_INDEX],
				BIT(4), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi4");

	clk = clk_reg_prcc_pclk("p2_pclk5", "per2clk", bases[CLKRST2_INDEX],
				BIT(5), 0);
	clk_register_clkdev(clk, "apb_pclk", "msp2");
	clk_register_clkdev(clk, "apb_pclk", "dbx5x0-msp-i2s.2");

	clk = clk_reg_prcc_pclk("p2_pclk6", "per2clk", bases[CLKRST2_INDEX],
				BIT(6), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi1");

	clk = clk_reg_prcc_pclk("p2_pclk7", "per2clk", bases[CLKRST2_INDEX],
				BIT(7), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi3");

	clk = clk_reg_prcc_pclk("p2_pclk8", "per2clk", bases[CLKRST2_INDEX],
				BIT(8), 0);
	clk_register_clkdev(clk, NULL, "spi0");

	clk = clk_reg_prcc_pclk("p2_pclk9", "per2clk", bases[CLKRST2_INDEX],
				BIT(9), 0);
	clk_register_clkdev(clk, "hsir_hclk", "ste_hsi.0");

	clk = clk_reg_prcc_pclk("p2_pclk10", "per2clk", bases[CLKRST2_INDEX],
				BIT(10), 0);
	clk_register_clkdev(clk, "hsit_hclk", "ste_hsi.0");

	clk = clk_reg_prcc_pclk("p2_pclk11", "per2clk", bases[CLKRST2_INDEX],
				BIT(11), 0);
	clk_register_clkdev(clk, NULL, "gpio.6");
	clk_register_clkdev(clk, NULL, "gpio.7");
	clk_register_clkdev(clk, NULL, "gpioblock1");

	clk = clk_reg_prcc_pclk("p2_pclk12", "per2clk", bases[CLKRST2_INDEX],
				BIT(12), 0);
	clk_register_clkdev(clk, "msp4-pclk", "ab85xx-codec.0");

	/* Peripheral 3 : PRCC P-clocks */
	clk = clk_reg_prcc_pclk("p3_pclk0", "per3clk", bases[CLKRST3_INDEX],
				BIT(0), 0);
	clk_register_clkdev(clk, NULL, "fsmc");

	clk = clk_reg_prcc_pclk("p3_pclk1", "per3clk", bases[CLKRST3_INDEX],
				BIT(1), 0);
	clk_register_clkdev(clk, "apb_pclk", "ssp0");

	clk = clk_reg_prcc_pclk("p3_pclk2", "per3clk", bases[CLKRST3_INDEX],
				BIT(2), 0);
	clk_register_clkdev(clk, "apb_pclk", "ssp1");

	clk = clk_reg_prcc_pclk("p3_pclk3", "per3clk", bases[CLKRST3_INDEX],
				BIT(3), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.0");

	clk = clk_reg_prcc_pclk("p3_pclk4", "per3clk", bases[CLKRST3_INDEX],
				BIT(4), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi2");

	clk = clk_reg_prcc_pclk("p3_pclk5", "per3clk", bases[CLKRST3_INDEX],
				BIT(5), 0);
	clk_register_clkdev(clk, "apb_pclk", "ske");
	clk_register_clkdev(clk, "apb_pclk", "nmk-ske-keypad");

	clk = clk_reg_prcc_pclk("p3_pclk6", "per3clk", bases[CLKRST3_INDEX],
				BIT(6), 0);
	clk_register_clkdev(clk, "apb_pclk", "uart2");

	clk = clk_reg_prcc_pclk("p3_pclk7", "per3clk", bases[CLKRST3_INDEX],
				BIT(7), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi5");

	clk = clk_reg_prcc_pclk("p3_pclk8", "per3clk", bases[CLKRST3_INDEX],
				BIT(8), 0);
	clk_register_clkdev(clk, NULL, "gpio.2");
	clk_register_clkdev(clk, NULL, "gpio.3");
	clk_register_clkdev(clk, NULL, "gpio.4");
	clk_register_clkdev(clk, NULL, "gpio.5");
	clk_register_clkdev(clk, NULL, "gpioblock2");

	clk = clk_reg_prcc_pclk("p3_pclk9", "per3clk", bases[CLKRST3_INDEX],
				BIT(9), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.5");

	clk = clk_reg_prcc_pclk("p3_pclk10", "per3clk", bases[CLKRST3_INDEX],
				BIT(10), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.6");

	clk = clk_reg_prcc_pclk("p3_pclk11", "per3clk", bases[CLKRST3_INDEX],
				BIT(11), 0);
	clk_register_clkdev(clk, "apb_pclk", "uart3");

	clk = clk_reg_prcc_pclk("p3_pclk12", "per3clk", bases[CLKRST3_INDEX],
				BIT(12), 0);
	clk_register_clkdev(clk, "apb_pclk", "uart4");

	/* Peripheral 5 : PRCC P-clocks */
	clk = clk_reg_prcc_pclk("p5_pclk0", "per5clk", bases[CLKRST5_INDEX],
				BIT(0), 0);
	clk_register_clkdev(clk, "usb", "musb-ux500.0");
	clk_register_clkdev(clk, "usbclk", "ab-iddet.0");

	clk = clk_reg_prcc_pclk("p5_pclk1", "per5clk", bases[CLKRST5_INDEX],
				BIT(1), 0);
	clk_register_clkdev(clk, NULL, "gpio.8");
	clk_register_clkdev(clk, NULL, "gpioblock3");

	/* Peripheral 6 : PRCC P-clocks */
	clk = clk_reg_prcc_pclk("p6_pclk0", "per6clk", bases[CLKRST6_INDEX],
				BIT(0), 0);
	clk_register_clkdev(clk, "apb_pclk", "rng");

	clk = clk_reg_prcc_pclk("p6_pclk1", "per6clk", bases[CLKRST6_INDEX],
				BIT(1), 0);
	clk_register_clkdev(clk, NULL, "cryp0");
	clk_register_clkdev(clk, NULL, "cryp1");

	clk = clk_reg_prcc_pclk("p6_pclk2", "per6clk", bases[CLKRST6_INDEX],
				BIT(2), 0);
	clk_register_clkdev(clk, NULL, "hash0");

	clk = clk_reg_prcc_pclk("p6_pclk3", "per6clk", bases[CLKRST6_INDEX],
				BIT(3), 0);
	clk_register_clkdev(clk, NULL, "pka");

	clk = clk_reg_prcc_pclk("p6_pclk4", "per6clk", bases[CLKRST6_INDEX],
				BIT(4), 0);
	clk_register_clkdev(clk, NULL, "db8540-hash1");

	clk = clk_reg_prcc_pclk("p6_pclk5", "per6clk", bases[CLKRST6_INDEX],
				BIT(5), 0);
	clk_register_clkdev(clk, NULL, "cfgreg");

	clk = clk_reg_prcc_pclk("p6_pclk6", "per6clk", bases[CLKRST6_INDEX],
				BIT(6), 0);
	clk_register_clkdev(clk, "apb_pclk", "mtu0");

	clk = clk_reg_prcc_pclk("p6_pclk7", "per6clk", bases[CLKRST6_INDEX],
				BIT(7), 0);
	clk_register_clkdev(clk, "apb_pclk", "mtu1");

	/*
	 * PRCC K-clocks  ==> see table PRCC_PCKEN/PRCC_KCKEN
	 * This differs from the internal implementation:
	 * We don't use the PERPIH[n| clock as parent, since those _should_
	 * only be used as parents for the P-clocks.
	 * TODO: "parentjoin" with corresponding P-clocks for all K-clocks.
	 */

	/* Peripheral 1 : PRCC K-clocks */
	clk = clk_reg_prcc_kclk("p1_uart0_kclk", "uartclk",
			bases[CLKRST1_INDEX], BIT(0), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "uart0");

	clk = clk_reg_prcc_kclk("p1_uart1_kclk", "uartclk",
			bases[CLKRST1_INDEX], BIT(1), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "uart1");

	clk = clk_reg_prcc_kclk("p1_i2c1_kclk", "i2cclk",
			bases[CLKRST1_INDEX], BIT(2), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.1");

	clk = clk_reg_prcc_kclk("p1_msp0_kclk", "msp02clk",
			bases[CLKRST1_INDEX], BIT(3), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "msp0");
	clk_register_clkdev(clk, NULL, "dbx5x0-msp-i2s.0");

	clk = clk_reg_prcc_kclk("p1_msp1_kclk", "msp1clk",
			bases[CLKRST1_INDEX], BIT(4), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "msp1");
	clk_register_clkdev(clk, NULL, "dbx5x0-msp-i2s.1");

	clk = clk_reg_prcc_kclk("p1_sdi0_kclk", "sdmmchclk",
			bases[CLKRST1_INDEX], BIT(5), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi0");

	clk = clk_reg_prcc_kclk("p1_i2c2_kclk", "i2cclk",
			bases[CLKRST1_INDEX], BIT(6), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.2");

	clk = clk_reg_prcc_kclk("p1_slimbus0_kclk", "slimclk",
			bases[CLKRST1_INDEX], BIT(8), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "slimbus0");

	clk = clk_reg_prcc_kclk("p1_i2c4_kclk", "i2cclk",
			bases[CLKRST1_INDEX], BIT(9), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.4");

	clk = clk_reg_prcc_kclk("p1_msp3_kclk", "msp1clk",
			bases[CLKRST1_INDEX], BIT(10), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "msp3");
	clk_register_clkdev(clk, NULL, "dbx5x0-msp-i2s.3");

	/* Peripheral 2 : PRCC K-clocks */
	clk = clk_reg_prcc_kclk("p2_i2c3_kclk", "i2cclk",
			bases[CLKRST2_INDEX], BIT(0), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.3");

	clk = clk_reg_prcc_kclk("p2_pwl_kclk", "rtc32k",
			bases[CLKRST2_INDEX], BIT(1), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "pwl");

	clk = clk_reg_prcc_kclk("p2_sdi4_kclk", "sdmmchclk",
			bases[CLKRST2_INDEX], BIT(2), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi4");

	clk = clk_reg_prcc_kclk("p2_msp2_kclk", "msp02clk",
			bases[CLKRST2_INDEX], BIT(3), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "msp2");
	clk_register_clkdev(clk, NULL, "dbx5x0-msp-i2s.2");

	clk = clk_reg_prcc_kclk("p2_sdi1_kclk", "sdmmchclk",
			bases[CLKRST2_INDEX], BIT(4), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi1");

	clk = clk_reg_prcc_kclk("p2_sdi3_kclk", "sdmmcclk",
			bases[CLKRST2_INDEX], BIT(5), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi3");

	clk = clk_reg_prcc_kclk("p2_ssirx_kclk", "hsirxclk",
			bases[CLKRST2_INDEX], BIT(6),
			CLK_SET_RATE_GATE|CLK_SET_RATE_PARENT);
	clk_register_clkdev(clk, "hsir_hsirxclk", "ste_hsi.0");

	clk = clk_reg_prcc_kclk("p2_ssitx_kclk", "hsitxclk",
			bases[CLKRST2_INDEX], BIT(7),
			CLK_SET_RATE_GATE|CLK_SET_RATE_PARENT);
	clk_register_clkdev(clk, "hsit_hsitxclk", "ste_hsi.0");

	/* Should only be 9540, but might be added for 85xx as well */
	clk = clk_reg_prcc_kclk("p2_msp4_kclk", "msp02clk",
			bases[CLKRST2_INDEX], BIT(9), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "msp4");
	clk_register_clkdev(clk, "msp4", "ab85xx-codec.0");

	/* Peripheral 3 : PRCC K-clocks */
	clk = clk_reg_prcc_kclk("p3_ssp0_kclk", "sspclk",
			bases[CLKRST3_INDEX], BIT(1), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "ssp0");

	clk = clk_reg_prcc_kclk("p3_ssp1_kclk", "sspclk",
			bases[CLKRST3_INDEX], BIT(2), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "ssp1");

	clk = clk_reg_prcc_kclk("p3_i2c0_kclk", "i2cclk",
			bases[CLKRST3_INDEX], BIT(3), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.0");

	clk = clk_reg_prcc_kclk("p3_sdi2_kclk", "sdmmchclk",
			bases[CLKRST3_INDEX], BIT(4), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi2");

	clk = clk_reg_prcc_kclk("p3_ske_kclk", "rtc32k",
			bases[CLKRST3_INDEX], BIT(5), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "ske");
	clk_register_clkdev(clk, NULL, "nmk-ske-keypad");

	clk = clk_reg_prcc_kclk("p3_uart2_kclk", "uartclk",
			bases[CLKRST3_INDEX], BIT(6), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "uart2");

	clk = clk_reg_prcc_kclk("p3_sdi5_kclk", "sdmmcclk",
			bases[CLKRST3_INDEX], BIT(7), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi5");

	clk = clk_reg_prcc_kclk("p3_i2c5_kclk", "i2cclk",
			bases[CLKRST3_INDEX], BIT(8), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.5");

	clk = clk_reg_prcc_kclk("p3_i2c6_kclk", "i2cclk",
			bases[CLKRST3_INDEX], BIT(9), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.6");

	clk = clk_reg_prcc_kclk("p3_uart3_kclk", "uartclk",
			bases[CLKRST3_INDEX], BIT(10), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "uart3");

	clk = clk_reg_prcc_kclk("p3_uart4_kclk", "uartclk",
			bases[CLKRST3_INDEX], BIT(11), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "uart4");

	/* Peripheral 6 : PRCC K-clocks */
	clk = clk_reg_prcc_kclk("p6_rng_kclk", "rngclk",
			bases[CLKRST6_INDEX], BIT(0), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "rng");
}
