/*
 * Clock definitions for u8500 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/platform_data/clk-ux500.h>
#include "clk.h"

void u8500_clk_init(u32 clkrst1_base, u32 clkrst2_base, u32 clkrst3_base,
		    u32 clkrst5_base, u32 clkrst6_base)
{
	struct prcmu_fw_version *fw_version;
	const char *sgaclk_parent = NULL;
	struct clk *clk;

	/* Clock sources */
	clk = clk_reg_prcmu_gate("soc0_pll", NULL, PRCMU_PLLSOC0,
				CLK_IS_ROOT|CLK_IGNORE_UNUSED);
	clk_register_clkdev(clk, "soc0_pll", NULL);

	clk = clk_reg_prcmu_gate("soc1_pll", NULL, PRCMU_PLLSOC1,
				CLK_IS_ROOT|CLK_IGNORE_UNUSED);
	clk_register_clkdev(clk, "soc1_pll", NULL);

	clk = clk_reg_prcmu_gate("ddr_pll", NULL, PRCMU_PLLDDR,
				CLK_IS_ROOT|CLK_IGNORE_UNUSED);
	clk_register_clkdev(clk, "ddr_pll", NULL);

	/* FIXME: Add sys, ulp and int clocks here. */

	clk = clk_register_fixed_rate(NULL, "rtc32k", "NULL",
				CLK_IS_ROOT|CLK_IGNORE_UNUSED,
				32768);
	clk_register_clkdev(clk, "clk32k", NULL);
	clk_register_clkdev(clk, "apb_pclk", "rtc-pl031");

	/* PRCMU clocks */
	fw_version = prcmu_get_fw_version();
	if (fw_version != NULL) {
		switch (fw_version->project) {
		case PRCMU_FW_PROJECT_U8500_C2:
		case PRCMU_FW_PROJECT_U8520:
		case PRCMU_FW_PROJECT_U8420:
			sgaclk_parent = "soc0_pll";
			break;
		default:
			break;
		}
	}

	if (sgaclk_parent)
		clk = clk_reg_prcmu_gate("sgclk", sgaclk_parent,
					PRCMU_SGACLK, 0);
	else
		clk = clk_reg_prcmu_gate("sgclk", NULL,
					PRCMU_SGACLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "mali");

	clk = clk_reg_prcmu_gate("uartclk", NULL, PRCMU_UARTCLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "UART");

	clk = clk_reg_prcmu_gate("msp02clk", NULL, PRCMU_MSP02CLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "MSP02");

	clk = clk_reg_prcmu_gate("msp1clk", NULL, PRCMU_MSP1CLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "MSP1");

	clk = clk_reg_prcmu_gate("i2cclk", NULL, PRCMU_I2CCLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "I2C");

	clk = clk_reg_prcmu_gate("slimclk", NULL, PRCMU_SLIMCLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "slim");

	clk = clk_reg_prcmu_gate("per1clk", NULL, PRCMU_PER1CLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "PERIPH1");

	clk = clk_reg_prcmu_gate("per2clk", NULL, PRCMU_PER2CLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "PERIPH2");

	clk = clk_reg_prcmu_gate("per3clk", NULL, PRCMU_PER3CLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "PERIPH3");

	clk = clk_reg_prcmu_gate("per5clk", NULL, PRCMU_PER5CLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "PERIPH5");

	clk = clk_reg_prcmu_gate("per6clk", NULL, PRCMU_PER6CLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "PERIPH6");

	clk = clk_reg_prcmu_gate("per7clk", NULL, PRCMU_PER7CLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "PERIPH7");

	clk = clk_reg_prcmu_scalable("lcdclk", NULL, PRCMU_LCDCLK, 0,
				CLK_IS_ROOT|CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "lcd");
	clk_register_clkdev(clk, "lcd", "mcde");

	clk = clk_reg_prcmu_opp_gate("bmlclk", NULL, PRCMU_BMLCLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "bml");

	clk = clk_reg_prcmu_scalable("hsitxclk", NULL, PRCMU_HSITXCLK, 0,
				CLK_IS_ROOT|CLK_SET_RATE_GATE);

	clk = clk_reg_prcmu_scalable("hsirxclk", NULL, PRCMU_HSIRXCLK, 0,
				CLK_IS_ROOT|CLK_SET_RATE_GATE);

	clk = clk_reg_prcmu_scalable("hdmiclk", NULL, PRCMU_HDMICLK, 0,
				CLK_IS_ROOT|CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "hdmi");
	clk_register_clkdev(clk, "hdmi", "mcde");

	clk = clk_reg_prcmu_scalable("apeatclk", NULL, PRCMU_APEATCLK, 0,
				     CLK_IS_ROOT|CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "apeat");

	clk = clk_reg_prcmu_scalable("apetraceclk", NULL, PRCMU_APETRACECLK, 0,
				CLK_IS_ROOT|CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "apetrace");

	clk = clk_reg_prcmu_gate("mcdeclk", NULL, PRCMU_MCDECLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "mcde");
	clk_register_clkdev(clk, "mcde", "mcde");
	clk_register_clkdev(clk, "dsisys", "dsilink.0");
	clk_register_clkdev(clk, "dsisys", "dsilink.1");
	clk_register_clkdev(clk, "dsisys", "dsilink.2");

	clk = clk_reg_prcmu_opp_gate("ipi2cclk", NULL, PRCMU_IPI2CCLK,
				CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "ipi2");

	clk = clk_reg_prcmu_gate("dsialtclk", NULL, PRCMU_DSIALTCLK,
				CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "dsialt");

	clk = clk_reg_prcmu_gate("dmaclk", NULL, PRCMU_DMACLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "dma40.0");

	clk = clk_reg_prcmu_gate("b2r2clk", NULL, PRCMU_B2R2CLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "b2r2");
	clk_register_clkdev(clk, NULL, "b2r2_core");
	clk_register_clkdev(clk, NULL, "U8500-B2R2.0");

	clk = clk_reg_prcmu_scalable("tvclk", NULL, PRCMU_TVCLK, 0,
				CLK_IS_ROOT|CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "tv");
	clk_register_clkdev(clk, "tv", "mcde");

	clk = clk_reg_prcmu_gate("sspclk", NULL, PRCMU_SSPCLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "SSP");

	clk = clk_reg_prcmu_gate("rngclk", NULL, PRCMU_RNGCLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "rngclk");

	clk = clk_reg_prcmu_gate("uiccclk", NULL, PRCMU_UICCCLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "uicc");

	clk = clk_reg_prcmu_gate("timclk", NULL, PRCMU_TIMCLK, CLK_IS_ROOT);
	clk_register_clkdev(clk, NULL, "mtu0");
	clk_register_clkdev(clk, NULL, "mtu1");

	clk = clk_reg_prcmu_opp_volt_scalable("sdmmcclk", NULL, PRCMU_SDMMCCLK,
					100000000,
					CLK_IS_ROOT|CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdmmc");

	clk = clk_reg_prcmu_scalable("dsi_pll", "hdmiclk",
				PRCMU_PLLDSI, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsihs2", "mcde");
	clk_register_clkdev(clk, "dsihs2", "dsilink.2");


	clk = clk_reg_prcmu_scalable("dsi0clk", "dsi_pll",
				PRCMU_DSI0CLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsihs0", "mcde");
	clk_register_clkdev(clk, "dsihs0", "dsilink.0");

	clk = clk_reg_prcmu_scalable("dsi1clk", "dsi_pll",
				PRCMU_DSI1CLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsihs1", "mcde");
	clk_register_clkdev(clk, "dsihs1", "dsilink.1");

	clk = clk_reg_prcmu_scalable("dsi0escclk", "tvclk",
				PRCMU_DSI0ESCCLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsilp0", "dsilink.0");
	clk_register_clkdev(clk, "dsilp0", "mcde");

	clk = clk_reg_prcmu_scalable("dsi1escclk", "tvclk",
				PRCMU_DSI1ESCCLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsilp1", "dsilink.1");
	clk_register_clkdev(clk, "dsilp1", "mcde");

	clk = clk_reg_prcmu_scalable("dsi2escclk", "tvclk",
				PRCMU_DSI2ESCCLK, 0, CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, "dsilp2", "dsilink.2");
	clk_register_clkdev(clk, "dsilp2", "mcde");

	clk = clk_reg_prcmu_scalable_rate("armss", NULL,
				PRCMU_ARMSS, 0, CLK_IS_ROOT|CLK_IGNORE_UNUSED);
	clk_register_clkdev(clk, "armss", NULL);

	clk = clk_register_fixed_factor(NULL, "smp_twd", "armss",
				CLK_IGNORE_UNUSED, 1, 2);
	clk_register_clkdev(clk, NULL, "smp_twd");

	/*
	 * FIXME: Add special handled PRCMU clocks here:
	 * 1. clkout0yuv, use PRCMU as parent + need regulator + pinctrl.
	 * 2. ab9540_clkout1yuv, see clkout0yuv
	 */

	/* PRCC P-clocks */
	clk = clk_reg_prcc_pclk("p1_pclk0", "per1clk", clkrst1_base,
				BIT(0), 0);
	clk_register_clkdev(clk, "apb_pclk", "uart0");

	clk = clk_reg_prcc_pclk("p1_pclk1", "per1clk", clkrst1_base,
				BIT(1), 0);
	clk_register_clkdev(clk, "apb_pclk", "uart1");

	clk = clk_reg_prcc_pclk("p1_pclk2", "per1clk", clkrst1_base,
				BIT(2), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.1");

	clk = clk_reg_prcc_pclk("p1_pclk3", "per1clk", clkrst1_base,
				BIT(3), 0);
	clk_register_clkdev(clk, "apb_pclk", "msp0");
	clk_register_clkdev(clk, "apb_pclk", "ux500-msp-i2s.0");

	clk = clk_reg_prcc_pclk("p1_pclk4", "per1clk", clkrst1_base,
				BIT(4), 0);
	clk_register_clkdev(clk, "apb_pclk", "msp1");
	clk_register_clkdev(clk, "apb_pclk", "ux500-msp-i2s.1");

	clk = clk_reg_prcc_pclk("p1_pclk5", "per1clk", clkrst1_base,
				BIT(5), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi0");

	clk = clk_reg_prcc_pclk("p1_pclk6", "per1clk", clkrst1_base,
				BIT(6), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.2");

	clk = clk_reg_prcc_pclk("p1_pclk7", "per1clk", clkrst1_base,
				BIT(7), 0);
	clk_register_clkdev(clk, NULL, "spi3");

	clk = clk_reg_prcc_pclk("p1_pclk8", "per1clk", clkrst1_base,
				BIT(8), 0);
	clk_register_clkdev(clk, "apb_pclk", "slimbus0");

	clk = clk_reg_prcc_pclk("p1_pclk9", "per1clk", clkrst1_base,
				BIT(9), 0);
	clk_register_clkdev(clk, NULL, "gpio.0");
	clk_register_clkdev(clk, NULL, "gpio.1");
	clk_register_clkdev(clk, NULL, "gpioblock0");

	clk = clk_reg_prcc_pclk("p1_pclk10", "per1clk", clkrst1_base,
				BIT(10), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.4");

	clk = clk_reg_prcc_pclk("p1_pclk11", "per1clk", clkrst1_base,
				BIT(11), 0);
	clk_register_clkdev(clk, "apb_pclk", "msp3");
	clk_register_clkdev(clk, "apb_pclk", "ux500-msp-i2s.3");

	clk = clk_reg_prcc_pclk("p2_pclk0", "per2clk", clkrst2_base,
				BIT(0), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.3");

	clk = clk_reg_prcc_pclk("p2_pclk1", "per2clk", clkrst2_base,
				BIT(1), 0);
	clk_register_clkdev(clk, NULL, "spi2");

	clk = clk_reg_prcc_pclk("p2_pclk2", "per2clk", clkrst2_base,
				BIT(2), 0);
	clk_register_clkdev(clk, NULL, "spi1");

	clk = clk_reg_prcc_pclk("p2_pclk3", "per2clk", clkrst2_base,
				BIT(3), 0);
	clk_register_clkdev(clk, NULL, "pwl");

	clk = clk_reg_prcc_pclk("p2_pclk4", "per2clk", clkrst2_base,
				BIT(4), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi4");

	clk = clk_reg_prcc_pclk("p2_pclk5", "per2clk", clkrst2_base,
				BIT(5), 0);
	clk_register_clkdev(clk, "apb_pclk", "msp2");
	clk_register_clkdev(clk, "apb_pclk", "ux500-msp-i2s.2");

	clk = clk_reg_prcc_pclk("p2_pclk6", "per2clk", clkrst2_base,
				BIT(6), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi1");

	clk = clk_reg_prcc_pclk("p2_pclk7", "per2clk", clkrst2_base,
				BIT(7), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi3");

	clk = clk_reg_prcc_pclk("p2_pclk8", "per2clk", clkrst2_base,
				BIT(8), 0);
	clk_register_clkdev(clk, NULL, "spi0");

	clk = clk_reg_prcc_pclk("p2_pclk9", "per2clk", clkrst2_base,
				BIT(9), 0);
	clk_register_clkdev(clk, "hsir_hclk", "ste_hsi.0");

	clk = clk_reg_prcc_pclk("p2_pclk10", "per2clk", clkrst2_base,
				BIT(10), 0);
	clk_register_clkdev(clk, "hsit_hclk", "ste_hsi.0");

	clk = clk_reg_prcc_pclk("p2_pclk11", "per2clk", clkrst2_base,
				BIT(11), 0);
	clk_register_clkdev(clk, NULL, "gpio.6");
	clk_register_clkdev(clk, NULL, "gpio.7");
	clk_register_clkdev(clk, NULL, "gpioblock1");

	clk = clk_reg_prcc_pclk("p2_pclk12", "per2clk", clkrst2_base,
				BIT(12), 0);

	clk = clk_reg_prcc_pclk("p3_pclk0", "per3clk", clkrst3_base,
				BIT(0), 0);
	clk_register_clkdev(clk, "fsmc", NULL);
	clk_register_clkdev(clk, NULL, "smsc911x.0");

	clk = clk_reg_prcc_pclk("p3_pclk1", "per3clk", clkrst3_base,
				BIT(1), 0);
	clk_register_clkdev(clk, "apb_pclk", "ssp0");

	clk = clk_reg_prcc_pclk("p3_pclk2", "per3clk", clkrst3_base,
				BIT(2), 0);
	clk_register_clkdev(clk, "apb_pclk", "ssp1");

	clk = clk_reg_prcc_pclk("p3_pclk3", "per3clk", clkrst3_base,
				BIT(3), 0);
	clk_register_clkdev(clk, "apb_pclk", "nmk-i2c.0");

	clk = clk_reg_prcc_pclk("p3_pclk4", "per3clk", clkrst3_base,
				BIT(4), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi2");

	clk = clk_reg_prcc_pclk("p3_pclk5", "per3clk", clkrst3_base,
				BIT(5), 0);
	clk_register_clkdev(clk, "apb_pclk", "ske");
	clk_register_clkdev(clk, "apb_pclk", "nmk-ske-keypad");

	clk = clk_reg_prcc_pclk("p3_pclk6", "per3clk", clkrst3_base,
				BIT(6), 0);
	clk_register_clkdev(clk, "apb_pclk", "uart2");

	clk = clk_reg_prcc_pclk("p3_pclk7", "per3clk", clkrst3_base,
				BIT(7), 0);
	clk_register_clkdev(clk, "apb_pclk", "sdi5");

	clk = clk_reg_prcc_pclk("p3_pclk8", "per3clk", clkrst3_base,
				BIT(8), 0);
	clk_register_clkdev(clk, NULL, "gpio.2");
	clk_register_clkdev(clk, NULL, "gpio.3");
	clk_register_clkdev(clk, NULL, "gpio.4");
	clk_register_clkdev(clk, NULL, "gpio.5");
	clk_register_clkdev(clk, NULL, "gpioblock2");

	clk = clk_reg_prcc_pclk("p5_pclk0", "per5clk", clkrst5_base,
				BIT(0), 0);
	clk_register_clkdev(clk, "usb", "musb-ux500.0");

	clk = clk_reg_prcc_pclk("p5_pclk1", "per5clk", clkrst5_base,
				BIT(1), 0);
	clk_register_clkdev(clk, NULL, "gpio.8");
	clk_register_clkdev(clk, NULL, "gpioblock3");

	clk = clk_reg_prcc_pclk("p6_pclk0", "per6clk", clkrst6_base,
				BIT(0), 0);
	clk_register_clkdev(clk, "apb_pclk", "rng");

	clk = clk_reg_prcc_pclk("p6_pclk1", "per6clk", clkrst6_base,
				BIT(1), 0);
	clk_register_clkdev(clk, NULL, "cryp0");
	clk_register_clkdev(clk, NULL, "cryp1");

	clk = clk_reg_prcc_pclk("p6_pclk2", "per6clk", clkrst6_base,
				BIT(2), 0);
	clk_register_clkdev(clk, NULL, "hash0");

	clk = clk_reg_prcc_pclk("p6_pclk3", "per6clk", clkrst6_base,
				BIT(3), 0);
	clk_register_clkdev(clk, NULL, "pka");

	clk = clk_reg_prcc_pclk("p6_pclk4", "per6clk", clkrst6_base,
				BIT(4), 0);
	clk_register_clkdev(clk, NULL, "hash1");

	clk = clk_reg_prcc_pclk("p6_pclk5", "per6clk", clkrst6_base,
				BIT(5), 0);
	clk_register_clkdev(clk, NULL, "cfgreg");

	clk = clk_reg_prcc_pclk("p6_pclk6", "per6clk", clkrst6_base,
				BIT(6), 0);
	clk_register_clkdev(clk, "apb_pclk", "mtu0");

	clk = clk_reg_prcc_pclk("p6_pclk7", "per6clk", clkrst6_base,
				BIT(7), 0);
	clk_register_clkdev(clk, "apb_pclk", "mtu1");

	/* PRCC K-clocks
	 *
	 * FIXME: Some drivers requires PERPIH[n| to be automatically enabled
	 * by enabling just the K-clock, even if it is not a valid parent to
	 * the K-clock. Until drivers get fixed we might need some kind of
	 * "parent muxed join".
	 */

	/* Periph1 */
	clk = clk_reg_prcc_kclk("p1_uart0_kclk", "uartclk",
			clkrst1_base, BIT(0), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "uart0");

	clk = clk_reg_prcc_kclk("p1_uart1_kclk", "uartclk",
			clkrst1_base, BIT(1), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "uart1");

	clk = clk_reg_prcc_kclk("p1_i2c1_kclk", "i2cclk",
			clkrst1_base, BIT(2), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.1");

	clk = clk_reg_prcc_kclk("p1_msp0_kclk", "msp02clk",
			clkrst1_base, BIT(3), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "msp0");
	clk_register_clkdev(clk, NULL, "ux500-msp-i2s.0");

	clk = clk_reg_prcc_kclk("p1_msp1_kclk", "msp1clk",
			clkrst1_base, BIT(4), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "msp1");
	clk_register_clkdev(clk, NULL, "ux500-msp-i2s.1");

	clk = clk_reg_prcc_kclk("p1_sdi0_kclk", "sdmmcclk",
			clkrst1_base, BIT(5), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi0");

	clk = clk_reg_prcc_kclk("p1_i2c2_kclk", "i2cclk",
			clkrst1_base, BIT(6), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.2");

	clk = clk_reg_prcc_kclk("p1_slimbus0_kclk", "slimclk",
			clkrst1_base, BIT(8), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "slimbus0");

	clk = clk_reg_prcc_kclk("p1_i2c4_kclk", "i2cclk",
			clkrst1_base, BIT(9), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.4");

	clk = clk_reg_prcc_kclk("p1_msp3_kclk", "msp1clk",
			clkrst1_base, BIT(10), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "msp3");
	clk_register_clkdev(clk, NULL, "ux500-msp-i2s.3");

	/* Periph2 */
	clk = clk_reg_prcc_kclk("p2_i2c3_kclk", "i2cclk",
			clkrst2_base, BIT(0), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.3");

	clk = clk_reg_prcc_kclk("p2_sdi4_kclk", "sdmmcclk",
			clkrst2_base, BIT(2), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi4");

	clk = clk_reg_prcc_kclk("p2_msp2_kclk", "msp02clk",
			clkrst2_base, BIT(3), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "msp2");
	clk_register_clkdev(clk, NULL, "ux500-msp-i2s.2");

	clk = clk_reg_prcc_kclk("p2_sdi1_kclk", "sdmmcclk",
			clkrst2_base, BIT(4), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi1");

	clk = clk_reg_prcc_kclk("p2_sdi3_kclk", "sdmmcclk",
			clkrst2_base, BIT(5), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi3");

	/* Note that rate is received from parent. */
	clk = clk_reg_prcc_kclk("p2_ssirx_kclk", "hsirxclk",
			clkrst2_base, BIT(6),
			CLK_SET_RATE_GATE|CLK_SET_RATE_PARENT);
	clk = clk_reg_prcc_kclk("p2_ssitx_kclk", "hsitxclk",
			clkrst2_base, BIT(7),
			CLK_SET_RATE_GATE|CLK_SET_RATE_PARENT);

	/* Periph3 */
	clk = clk_reg_prcc_kclk("p3_ssp0_kclk", "sspclk",
			clkrst3_base, BIT(1), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "ssp0");

	clk = clk_reg_prcc_kclk("p3_ssp1_kclk", "sspclk",
			clkrst3_base, BIT(2), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "ssp1");

	clk = clk_reg_prcc_kclk("p3_i2c0_kclk", "i2cclk",
			clkrst3_base, BIT(3), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "nmk-i2c.0");

	clk = clk_reg_prcc_kclk("p3_sdi2_kclk", "sdmmcclk",
			clkrst3_base, BIT(4), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi2");

	clk = clk_reg_prcc_kclk("p3_ske_kclk", "rtc32k",
			clkrst3_base, BIT(5), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "ske");
	clk_register_clkdev(clk, NULL, "nmk-ske-keypad");

	clk = clk_reg_prcc_kclk("p3_uart2_kclk", "uartclk",
			clkrst3_base, BIT(6), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "uart2");

	clk = clk_reg_prcc_kclk("p3_sdi5_kclk", "sdmmcclk",
			clkrst3_base, BIT(7), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "sdi5");

	/* Periph6 */
	clk = clk_reg_prcc_kclk("p3_rng_kclk", "rngclk",
			clkrst6_base, BIT(0), CLK_SET_RATE_GATE);
	clk_register_clkdev(clk, NULL, "rng");
}
