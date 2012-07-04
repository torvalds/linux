/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2008 Martin Fuzzey, mfuzzey@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/clkdev.h>
#include <linux/err.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include "clk.h"

#define IO_ADDR_CCM(off)	(MX21_IO_ADDRESS(MX21_CCM_BASE_ADDR + (off)))

/* Register offsets */
#define CCM_CSCR		IO_ADDR_CCM(0x0)
#define CCM_MPCTL0		IO_ADDR_CCM(0x4)
#define CCM_MPCTL1		IO_ADDR_CCM(0x8)
#define CCM_SPCTL0		IO_ADDR_CCM(0xc)
#define CCM_SPCTL1		IO_ADDR_CCM(0x10)
#define CCM_OSC26MCTL		IO_ADDR_CCM(0x14)
#define CCM_PCDR0		IO_ADDR_CCM(0x18)
#define CCM_PCDR1		IO_ADDR_CCM(0x1c)
#define CCM_PCCR0		IO_ADDR_CCM(0x20)
#define CCM_PCCR1		IO_ADDR_CCM(0x24)
#define CCM_CCSR		IO_ADDR_CCM(0x28)
#define CCM_PMCTL		IO_ADDR_CCM(0x2c)
#define CCM_PMCOUNT		IO_ADDR_CCM(0x30)
#define CCM_WKGDCTL		IO_ADDR_CCM(0x34)

static const char *mpll_sel_clks[] = { "fpm", "ckih", };
static const char *spll_sel_clks[] = { "fpm", "ckih", };

enum imx21_clks {
	ckil, ckih, fpm, mpll_sel, spll_sel, mpll, spll, fclk, hclk, ipg, per1,
	per2, per3, per4, uart1_ipg_gate, uart2_ipg_gate, uart3_ipg_gate,
	uart4_ipg_gate, gpt1_ipg_gate, gpt2_ipg_gate, gpt3_ipg_gate,
	pwm_ipg_gate, sdhc1_ipg_gate, sdhc2_ipg_gate, lcdc_ipg_gate,
	lcdc_hclk_gate, cspi3_ipg_gate, cspi2_ipg_gate, cspi1_ipg_gate,
	per4_gate, csi_hclk_gate, usb_div, usb_gate, usb_hclk_gate, ssi1_gate,
	ssi2_gate, nfc_div, nfc_gate, dma_gate, dma_hclk_gate, brom_gate,
	emma_gate, emma_hclk_gate, slcdc_gate, slcdc_hclk_gate, wdog_gate,
	gpio_gate, i2c_gate, kpp_gate, owire_gate, rtc_gate, clk_max
};

static struct clk *clk[clk_max];

/*
 * must be called very early to get information about the
 * available clock rate when the timer framework starts
 */
int __init mx21_clocks_init(unsigned long lref, unsigned long href)
{
	int i;

	clk[ckil] = imx_clk_fixed("ckil", lref);
	clk[ckih] = imx_clk_fixed("ckih", href);
	clk[fpm] = imx_clk_fixed_factor("fpm", "ckil", 512, 1);
	clk[mpll_sel] = imx_clk_mux("mpll_sel", CCM_CSCR, 16, 1, mpll_sel_clks,
			ARRAY_SIZE(mpll_sel_clks));
	clk[spll_sel] = imx_clk_mux("spll_sel", CCM_CSCR, 17, 1, spll_sel_clks,
			ARRAY_SIZE(spll_sel_clks));
	clk[mpll] = imx_clk_pllv1("mpll", "mpll_sel", CCM_MPCTL0);
	clk[spll] = imx_clk_pllv1("spll", "spll_sel", CCM_SPCTL0);
	clk[fclk] = imx_clk_divider("fclk", "mpll", CCM_CSCR, 29, 3);
	clk[hclk] = imx_clk_divider("hclk", "fclk", CCM_CSCR, 10, 4);
	clk[ipg] = imx_clk_divider("ipg", "hclk", CCM_CSCR, 9, 1);
	clk[per1] = imx_clk_divider("per1", "mpll", CCM_PCDR1, 0, 6);
	clk[per2] = imx_clk_divider("per2", "mpll", CCM_PCDR1, 8, 6);
	clk[per3] = imx_clk_divider("per3", "mpll", CCM_PCDR1, 16, 6);
	clk[per4] = imx_clk_divider("per4", "mpll", CCM_PCDR1, 24, 6);
	clk[uart1_ipg_gate] = imx_clk_gate("uart1_ipg_gate", "ipg", CCM_PCCR0, 0);
	clk[uart2_ipg_gate] = imx_clk_gate("uart2_ipg_gate", "ipg", CCM_PCCR0, 1);
	clk[uart3_ipg_gate] = imx_clk_gate("uart3_ipg_gate", "ipg", CCM_PCCR0, 2);
	clk[uart4_ipg_gate] = imx_clk_gate("uart4_ipg_gate", "ipg", CCM_PCCR0, 3);
	clk[gpt1_ipg_gate] = imx_clk_gate("gpt1_ipg_gate", "ipg", CCM_PCCR1, 25);
	clk[gpt2_ipg_gate] = imx_clk_gate("gpt2_ipg_gate", "ipg", CCM_PCCR1, 26);
	clk[gpt3_ipg_gate] = imx_clk_gate("gpt3_ipg_gate", "ipg", CCM_PCCR1, 27);
	clk[pwm_ipg_gate] = imx_clk_gate("pwm_ipg_gate", "ipg", CCM_PCCR1, 28);
	clk[sdhc1_ipg_gate] = imx_clk_gate("sdhc1_ipg_gate", "ipg", CCM_PCCR0, 9);
	clk[sdhc2_ipg_gate] = imx_clk_gate("sdhc2_ipg_gate", "ipg", CCM_PCCR0, 10);
	clk[lcdc_ipg_gate] = imx_clk_gate("lcdc_ipg_gate", "ipg", CCM_PCCR0, 18);
	clk[lcdc_hclk_gate] = imx_clk_gate("lcdc_hclk_gate", "hclk", CCM_PCCR0, 26);
	clk[cspi3_ipg_gate] = imx_clk_gate("cspi3_ipg_gate", "ipg", CCM_PCCR1, 23);
	clk[cspi2_ipg_gate] = imx_clk_gate("cspi2_ipg_gate", "ipg", CCM_PCCR0, 5);
	clk[cspi1_ipg_gate] = imx_clk_gate("cspi1_ipg_gate", "ipg", CCM_PCCR0, 4);
	clk[per4_gate] = imx_clk_gate("per4_gate", "per4", CCM_PCCR0, 22);
	clk[csi_hclk_gate] = imx_clk_gate("csi_hclk_gate", "hclk", CCM_PCCR0, 31);
	clk[usb_div] = imx_clk_divider("usb_div", "spll", CCM_CSCR, 26, 3);
	clk[usb_gate] = imx_clk_gate("usb_gate", "usb_div", CCM_PCCR0, 14);
	clk[usb_hclk_gate] = imx_clk_gate("usb_hclk_gate", "hclk", CCM_PCCR0, 24);
	clk[ssi1_gate] = imx_clk_gate("ssi1_gate", "ipg", CCM_PCCR0, 6);
	clk[ssi2_gate] = imx_clk_gate("ssi2_gate", "ipg", CCM_PCCR0, 7);
	clk[nfc_div] = imx_clk_divider("nfc_div", "ipg", CCM_PCDR0, 12, 4);
	clk[nfc_gate] = imx_clk_gate("nfc_gate", "nfc_div", CCM_PCCR0, 19);
	clk[dma_gate] = imx_clk_gate("dma_gate", "ipg", CCM_PCCR0, 13);
	clk[dma_hclk_gate] = imx_clk_gate("dma_hclk_gate", "hclk", CCM_PCCR0, 30);
	clk[brom_gate] = imx_clk_gate("brom_gate", "hclk", CCM_PCCR0, 28);
	clk[emma_gate] = imx_clk_gate("emma_gate", "ipg", CCM_PCCR0, 15);
	clk[emma_hclk_gate] = imx_clk_gate("emma_hclk_gate", "hclk", CCM_PCCR0, 27);
	clk[slcdc_gate] = imx_clk_gate("slcdc_gate", "ipg", CCM_PCCR0, 25);
	clk[slcdc_hclk_gate] = imx_clk_gate("slcdc_hclk_gate", "hclk", CCM_PCCR0, 21);
	clk[wdog_gate] = imx_clk_gate("wdog_gate", "ipg", CCM_PCCR1, 24);
	clk[gpio_gate] = imx_clk_gate("gpio_gate", "ipg", CCM_PCCR0, 11);
	clk[i2c_gate] = imx_clk_gate("i2c_gate", "ipg", CCM_PCCR0, 12);
	clk[kpp_gate] = imx_clk_gate("kpp_gate", "ipg", CCM_PCCR1, 30);
	clk[owire_gate] = imx_clk_gate("owire_gate", "ipg", CCM_PCCR1, 31);
	clk[rtc_gate] = imx_clk_gate("rtc_gate", "ipg", CCM_PCCR1, 29);

	for (i = 0; i < ARRAY_SIZE(clk); i++)
		if (IS_ERR(clk[i]))
			pr_err("i.MX21 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));

	clk_register_clkdev(clk[per1], "per1", NULL);
	clk_register_clkdev(clk[per2], "per2", NULL);
	clk_register_clkdev(clk[per3], "per3", NULL);
	clk_register_clkdev(clk[per4], "per4", NULL);
	clk_register_clkdev(clk[per1], "per", "imx21-uart.0");
	clk_register_clkdev(clk[uart1_ipg_gate], "ipg", "imx21-uart.0");
	clk_register_clkdev(clk[per1], "per", "imx21-uart.1");
	clk_register_clkdev(clk[uart2_ipg_gate], "ipg", "imx21-uart.1");
	clk_register_clkdev(clk[per1], "per", "imx21-uart.2");
	clk_register_clkdev(clk[uart3_ipg_gate], "ipg", "imx21-uart.2");
	clk_register_clkdev(clk[per1], "per", "imx21-uart.3");
	clk_register_clkdev(clk[uart4_ipg_gate], "ipg", "imx21-uart.3");
	clk_register_clkdev(clk[gpt1_ipg_gate], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[per1], "per", "imx-gpt.0");
	clk_register_clkdev(clk[gpt2_ipg_gate], "ipg", "imx-gpt.1");
	clk_register_clkdev(clk[per1], "per", "imx-gpt.1");
	clk_register_clkdev(clk[gpt3_ipg_gate], "ipg", "imx-gpt.2");
	clk_register_clkdev(clk[per1], "per", "imx-gpt.2");
	clk_register_clkdev(clk[pwm_ipg_gate], "pwm", "mxc_pwm.0");
	clk_register_clkdev(clk[per2], "per", "imx21-cspi.0");
	clk_register_clkdev(clk[cspi1_ipg_gate], "ipg", "imx21-cspi.0");
	clk_register_clkdev(clk[per2], "per", "imx21-cspi.1");
	clk_register_clkdev(clk[cspi2_ipg_gate], "ipg", "imx21-cspi.1");
	clk_register_clkdev(clk[per2], "per", "imx21-cspi.2");
	clk_register_clkdev(clk[cspi3_ipg_gate], "ipg", "imx21-cspi.2");
	clk_register_clkdev(clk[per3], "per", "imx-fb.0");
	clk_register_clkdev(clk[lcdc_ipg_gate], "ipg", "imx-fb.0");
	clk_register_clkdev(clk[lcdc_hclk_gate], "ahb", "imx-fb.0");
	clk_register_clkdev(clk[usb_gate], "per", "imx21-hcd.0");
	clk_register_clkdev(clk[usb_hclk_gate], "ahb", "imx21-hcd.0");
	clk_register_clkdev(clk[nfc_gate], NULL, "mxc_nand.0");
	clk_register_clkdev(clk[dma_hclk_gate], "ahb", "imx-dma");
	clk_register_clkdev(clk[dma_gate], "ipg", "imx-dma");
	clk_register_clkdev(clk[wdog_gate], NULL, "imx2-wdt.0");
	clk_register_clkdev(clk[i2c_gate], NULL, "imx-i2c.0");
	clk_register_clkdev(clk[kpp_gate], NULL, "mxc-keypad");
	clk_register_clkdev(clk[owire_gate], NULL, "mxc_w1.0");
	clk_register_clkdev(clk[brom_gate], "brom", NULL);
	clk_register_clkdev(clk[emma_gate], "emma", NULL);
	clk_register_clkdev(clk[slcdc_gate], "slcdc", NULL);
	clk_register_clkdev(clk[gpio_gate], "gpio", NULL);
	clk_register_clkdev(clk[rtc_gate], "rtc", NULL);
	clk_register_clkdev(clk[csi_hclk_gate], "csi", NULL);
	clk_register_clkdev(clk[ssi1_gate], "ssi1", NULL);
	clk_register_clkdev(clk[ssi2_gate], "ssi2", NULL);
	clk_register_clkdev(clk[sdhc1_ipg_gate], "sdhc1", NULL);
	clk_register_clkdev(clk[sdhc2_ipg_gate], "sdhc2", NULL);

	mxc_timer_init(MX21_IO_ADDRESS(MX21_GPT1_BASE_ADDR), MX21_INT_GPT1);

	return 0;
}
