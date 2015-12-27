/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2008 Martin Fuzzey, mfuzzey@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <dt-bindings/clock/imx21-clock.h>
#include <soc/imx/timer.h>
#include <asm/irq.h>

#include "clk.h"

#define MX21_CCM_BASE_ADDR	0x10027000
#define MX21_GPT1_BASE_ADDR	0x10003000
#define MX21_INT_GPT1		(NR_IRQS_LEGACY + 26)

static void __iomem *ccm __initdata;

/* Register offsets */
#define CCM_CSCR	(ccm + 0x00)
#define CCM_MPCTL0	(ccm + 0x04)
#define CCM_SPCTL0	(ccm + 0x0c)
#define CCM_PCDR0	(ccm + 0x18)
#define CCM_PCDR1	(ccm + 0x1c)
#define CCM_PCCR0	(ccm + 0x20)
#define CCM_PCCR1	(ccm + 0x24)

static const char *mpll_osc_sel_clks[] = { "ckih_gate", "ckih_div1p5", };
static const char *mpll_sel_clks[] = { "fpm_gate", "mpll_osc_sel", };
static const char *spll_sel_clks[] = { "fpm_gate", "mpll_osc_sel", };
static const char *ssi_sel_clks[] = { "spll_gate", "mpll_gate", };

static struct clk *clk[IMX21_CLK_MAX];
static struct clk_onecell_data clk_data;

static void __init _mx21_clocks_init(unsigned long lref, unsigned long href)
{
	BUG_ON(!ccm);

	clk[IMX21_CLK_DUMMY] = imx_clk_fixed("dummy", 0);
	clk[IMX21_CLK_CKIL] = imx_obtain_fixed_clock("ckil", lref);
	clk[IMX21_CLK_CKIH] = imx_obtain_fixed_clock("ckih", href);
	clk[IMX21_CLK_FPM] = imx_clk_fixed_factor("fpm", "ckil", 512, 1);
	clk[IMX21_CLK_CKIH_DIV1P5] = imx_clk_fixed_factor("ckih_div1p5", "ckih_gate", 2, 3);

	clk[IMX21_CLK_MPLL_GATE] = imx_clk_gate("mpll_gate", "mpll", CCM_CSCR, 0);
	clk[IMX21_CLK_SPLL_GATE] = imx_clk_gate("spll_gate", "spll", CCM_CSCR, 1);
	clk[IMX21_CLK_FPM_GATE] = imx_clk_gate("fpm_gate", "fpm", CCM_CSCR, 2);
	clk[IMX21_CLK_CKIH_GATE] = imx_clk_gate_dis("ckih_gate", "ckih", CCM_CSCR, 3);
	clk[IMX21_CLK_MPLL_OSC_SEL] = imx_clk_mux("mpll_osc_sel", CCM_CSCR, 4, 1, mpll_osc_sel_clks, ARRAY_SIZE(mpll_osc_sel_clks));
	clk[IMX21_CLK_IPG] = imx_clk_divider("ipg", "hclk", CCM_CSCR, 9, 1);
	clk[IMX21_CLK_HCLK] = imx_clk_divider("hclk", "fclk", CCM_CSCR, 10, 4);
	clk[IMX21_CLK_MPLL_SEL] = imx_clk_mux("mpll_sel", CCM_CSCR, 16, 1, mpll_sel_clks, ARRAY_SIZE(mpll_sel_clks));
	clk[IMX21_CLK_SPLL_SEL] = imx_clk_mux("spll_sel", CCM_CSCR, 17, 1, spll_sel_clks, ARRAY_SIZE(spll_sel_clks));
	clk[IMX21_CLK_SSI1_SEL] = imx_clk_mux("ssi1_sel", CCM_CSCR, 19, 1, ssi_sel_clks, ARRAY_SIZE(ssi_sel_clks));
	clk[IMX21_CLK_SSI2_SEL] = imx_clk_mux("ssi2_sel", CCM_CSCR, 20, 1, ssi_sel_clks, ARRAY_SIZE(ssi_sel_clks));
	clk[IMX21_CLK_USB_DIV] = imx_clk_divider("usb_div", "spll_gate", CCM_CSCR, 26, 3);
	clk[IMX21_CLK_FCLK] = imx_clk_divider("fclk", "mpll_gate", CCM_CSCR, 29, 3);

	clk[IMX21_CLK_MPLL] = imx_clk_pllv1(IMX_PLLV1_IMX21, "mpll", "mpll_sel", CCM_MPCTL0);

	clk[IMX21_CLK_SPLL] = imx_clk_pllv1(IMX_PLLV1_IMX21, "spll", "spll_sel", CCM_SPCTL0);

	clk[IMX21_CLK_NFC_DIV] = imx_clk_divider("nfc_div", "fclk", CCM_PCDR0, 12, 4);
	clk[IMX21_CLK_SSI1_DIV] = imx_clk_divider("ssi1_div", "ssi1_sel", CCM_PCDR0, 16, 6);
	clk[IMX21_CLK_SSI2_DIV] = imx_clk_divider("ssi2_div", "ssi2_sel", CCM_PCDR0, 26, 6);

	clk[IMX21_CLK_PER1] = imx_clk_divider("per1", "mpll_gate", CCM_PCDR1, 0, 6);
	clk[IMX21_CLK_PER2] = imx_clk_divider("per2", "mpll_gate", CCM_PCDR1, 8, 6);
	clk[IMX21_CLK_PER3] = imx_clk_divider("per3", "mpll_gate", CCM_PCDR1, 16, 6);
	clk[IMX21_CLK_PER4] = imx_clk_divider("per4", "mpll_gate", CCM_PCDR1, 24, 6);

	clk[IMX21_CLK_UART1_IPG_GATE] = imx_clk_gate("uart1_ipg_gate", "ipg", CCM_PCCR0, 0);
	clk[IMX21_CLK_UART2_IPG_GATE] = imx_clk_gate("uart2_ipg_gate", "ipg", CCM_PCCR0, 1);
	clk[IMX21_CLK_UART3_IPG_GATE] = imx_clk_gate("uart3_ipg_gate", "ipg", CCM_PCCR0, 2);
	clk[IMX21_CLK_UART4_IPG_GATE] = imx_clk_gate("uart4_ipg_gate", "ipg", CCM_PCCR0, 3);
	clk[IMX21_CLK_CSPI1_IPG_GATE] = imx_clk_gate("cspi1_ipg_gate", "ipg", CCM_PCCR0, 4);
	clk[IMX21_CLK_CSPI2_IPG_GATE] = imx_clk_gate("cspi2_ipg_gate", "ipg", CCM_PCCR0, 5);
	clk[IMX21_CLK_SSI1_GATE] = imx_clk_gate("ssi1_gate", "ipg", CCM_PCCR0, 6);
	clk[IMX21_CLK_SSI2_GATE] = imx_clk_gate("ssi2_gate", "ipg", CCM_PCCR0, 7);
	clk[IMX21_CLK_SDHC1_IPG_GATE] = imx_clk_gate("sdhc1_ipg_gate", "ipg", CCM_PCCR0, 9);
	clk[IMX21_CLK_SDHC2_IPG_GATE] = imx_clk_gate("sdhc2_ipg_gate", "ipg", CCM_PCCR0, 10);
	clk[IMX21_CLK_GPIO_GATE] = imx_clk_gate("gpio_gate", "ipg", CCM_PCCR0, 11);
	clk[IMX21_CLK_I2C_GATE] = imx_clk_gate("i2c_gate", "ipg", CCM_PCCR0, 12);
	clk[IMX21_CLK_DMA_GATE] = imx_clk_gate("dma_gate", "ipg", CCM_PCCR0, 13);
	clk[IMX21_CLK_USB_GATE] = imx_clk_gate("usb_gate", "usb_div", CCM_PCCR0, 14);
	clk[IMX21_CLK_EMMA_GATE] = imx_clk_gate("emma_gate", "ipg", CCM_PCCR0, 15);
	clk[IMX21_CLK_SSI2_BAUD_GATE] = imx_clk_gate("ssi2_baud_gate", "ipg", CCM_PCCR0, 16);
	clk[IMX21_CLK_SSI1_BAUD_GATE] = imx_clk_gate("ssi1_baud_gate", "ipg", CCM_PCCR0, 17);
	clk[IMX21_CLK_LCDC_IPG_GATE] = imx_clk_gate("lcdc_ipg_gate", "ipg", CCM_PCCR0, 18);
	clk[IMX21_CLK_NFC_GATE] = imx_clk_gate("nfc_gate", "nfc_div", CCM_PCCR0, 19);
	clk[IMX21_CLK_SLCDC_HCLK_GATE] = imx_clk_gate("slcdc_hclk_gate", "hclk", CCM_PCCR0, 21);
	clk[IMX21_CLK_PER4_GATE] = imx_clk_gate("per4_gate", "per4", CCM_PCCR0, 22);
	clk[IMX21_CLK_BMI_GATE] = imx_clk_gate("bmi_gate", "hclk", CCM_PCCR0, 23);
	clk[IMX21_CLK_USB_HCLK_GATE] = imx_clk_gate("usb_hclk_gate", "hclk", CCM_PCCR0, 24);
	clk[IMX21_CLK_SLCDC_GATE] = imx_clk_gate("slcdc_gate", "hclk", CCM_PCCR0, 25);
	clk[IMX21_CLK_LCDC_HCLK_GATE] = imx_clk_gate("lcdc_hclk_gate", "hclk", CCM_PCCR0, 26);
	clk[IMX21_CLK_EMMA_HCLK_GATE] = imx_clk_gate("emma_hclk_gate", "hclk", CCM_PCCR0, 27);
	clk[IMX21_CLK_BROM_GATE] = imx_clk_gate("brom_gate", "hclk", CCM_PCCR0, 28);
	clk[IMX21_CLK_DMA_HCLK_GATE] = imx_clk_gate("dma_hclk_gate", "hclk", CCM_PCCR0, 30);
	clk[IMX21_CLK_CSI_HCLK_GATE] = imx_clk_gate("csi_hclk_gate", "hclk", CCM_PCCR0, 31);

	clk[IMX21_CLK_CSPI3_IPG_GATE] = imx_clk_gate("cspi3_ipg_gate", "ipg", CCM_PCCR1, 23);
	clk[IMX21_CLK_WDOG_GATE] = imx_clk_gate("wdog_gate", "ipg", CCM_PCCR1, 24);
	clk[IMX21_CLK_GPT1_IPG_GATE] = imx_clk_gate("gpt1_ipg_gate", "ipg", CCM_PCCR1, 25);
	clk[IMX21_CLK_GPT2_IPG_GATE] = imx_clk_gate("gpt2_ipg_gate", "ipg", CCM_PCCR1, 26);
	clk[IMX21_CLK_GPT3_IPG_GATE] = imx_clk_gate("gpt3_ipg_gate", "ipg", CCM_PCCR1, 27);
	clk[IMX21_CLK_PWM_IPG_GATE] = imx_clk_gate("pwm_ipg_gate", "ipg", CCM_PCCR1, 28);
	clk[IMX21_CLK_RTC_GATE] = imx_clk_gate("rtc_gate", "ipg", CCM_PCCR1, 29);
	clk[IMX21_CLK_KPP_GATE] = imx_clk_gate("kpp_gate", "ipg", CCM_PCCR1, 30);
	clk[IMX21_CLK_OWIRE_GATE] = imx_clk_gate("owire_gate", "ipg", CCM_PCCR1, 31);

	imx_check_clocks(clk, ARRAY_SIZE(clk));
}

int __init mx21_clocks_init(unsigned long lref, unsigned long href)
{
	ccm = ioremap(MX21_CCM_BASE_ADDR, SZ_2K);

	_mx21_clocks_init(lref, href);

	clk_register_clkdev(clk[IMX21_CLK_PER1], "per", "imx21-uart.0");
	clk_register_clkdev(clk[IMX21_CLK_UART1_IPG_GATE], "ipg", "imx21-uart.0");
	clk_register_clkdev(clk[IMX21_CLK_PER1], "per", "imx21-uart.1");
	clk_register_clkdev(clk[IMX21_CLK_UART2_IPG_GATE], "ipg", "imx21-uart.1");
	clk_register_clkdev(clk[IMX21_CLK_PER1], "per", "imx21-uart.2");
	clk_register_clkdev(clk[IMX21_CLK_UART3_IPG_GATE], "ipg", "imx21-uart.2");
	clk_register_clkdev(clk[IMX21_CLK_PER1], "per", "imx21-uart.3");
	clk_register_clkdev(clk[IMX21_CLK_UART4_IPG_GATE], "ipg", "imx21-uart.3");
	clk_register_clkdev(clk[IMX21_CLK_GPT1_IPG_GATE], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[IMX21_CLK_PER1], "per", "imx-gpt.0");
	clk_register_clkdev(clk[IMX21_CLK_PER2], "per", "imx21-cspi.0");
	clk_register_clkdev(clk[IMX21_CLK_CSPI1_IPG_GATE], "ipg", "imx21-cspi.0");
	clk_register_clkdev(clk[IMX21_CLK_PER2], "per", "imx21-cspi.1");
	clk_register_clkdev(clk[IMX21_CLK_CSPI2_IPG_GATE], "ipg", "imx21-cspi.1");
	clk_register_clkdev(clk[IMX21_CLK_PER2], "per", "imx21-cspi.2");
	clk_register_clkdev(clk[IMX21_CLK_CSPI3_IPG_GATE], "ipg", "imx21-cspi.2");
	clk_register_clkdev(clk[IMX21_CLK_PER3], "per", "imx21-fb.0");
	clk_register_clkdev(clk[IMX21_CLK_LCDC_IPG_GATE], "ipg", "imx21-fb.0");
	clk_register_clkdev(clk[IMX21_CLK_LCDC_HCLK_GATE], "ahb", "imx21-fb.0");
	clk_register_clkdev(clk[IMX21_CLK_USB_GATE], "per", "imx21-hcd.0");
	clk_register_clkdev(clk[IMX21_CLK_USB_HCLK_GATE], "ahb", "imx21-hcd.0");
	clk_register_clkdev(clk[IMX21_CLK_NFC_GATE], NULL, "imx21-nand.0");
	clk_register_clkdev(clk[IMX21_CLK_DMA_HCLK_GATE], "ahb", "imx21-dma");
	clk_register_clkdev(clk[IMX21_CLK_DMA_GATE], "ipg", "imx21-dma");
	clk_register_clkdev(clk[IMX21_CLK_WDOG_GATE], NULL, "imx2-wdt.0");
	clk_register_clkdev(clk[IMX21_CLK_I2C_GATE], NULL, "imx21-i2c.0");
	clk_register_clkdev(clk[IMX21_CLK_OWIRE_GATE], NULL, "mxc_w1.0");

	mxc_timer_init(MX21_GPT1_BASE_ADDR, MX21_INT_GPT1, GPT_TYPE_IMX21);

	return 0;
}

static void __init mx21_clocks_init_dt(struct device_node *np)
{
	ccm = of_iomap(np, 0);

	_mx21_clocks_init(32768, 26000000);

	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}
CLK_OF_DECLARE(imx27_ccm, "fsl,imx21-ccm", mx21_clocks_init_dt);
