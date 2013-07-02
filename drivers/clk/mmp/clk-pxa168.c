/*
 * pxa168 clock framework source file
 *
 * Copyright (C) 2012 Marvell
 * Chao Xie <xiechao.mail@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <mach/addr-map.h>

#include "clk.h"

#define APBC_RTC	0x28
#define APBC_TWSI0	0x2c
#define APBC_KPC	0x30
#define APBC_UART0	0x0
#define APBC_UART1	0x4
#define APBC_GPIO	0x8
#define APBC_PWM0	0xc
#define APBC_PWM1	0x10
#define APBC_PWM2	0x14
#define APBC_PWM3	0x18
#define APBC_SSP0	0x81c
#define APBC_SSP1	0x820
#define APBC_SSP2	0x84c
#define APBC_SSP3	0x858
#define APBC_SSP4	0x85c
#define APBC_TWSI1	0x6c
#define APBC_UART2	0x70
#define APMU_SDH0	0x54
#define APMU_SDH1	0x58
#define APMU_USB	0x5c
#define APMU_DISP0	0x4c
#define APMU_CCIC0	0x50
#define APMU_DFC	0x60
#define MPMU_UART_PLL	0x14

static DEFINE_SPINLOCK(clk_lock);

static struct clk_factor_masks uart_factor_masks = {
	.factor = 2,
	.num_mask = 0x1fff,
	.den_mask = 0x1fff,
	.num_shift = 16,
	.den_shift = 0,
};

static struct clk_factor_tbl uart_factor_tbl[] = {
	{.num = 8125, .den = 1536},	/*14.745MHZ */
};

static const char *uart_parent[] = {"pll1_3_16", "uart_pll"};
static const char *ssp_parent[] = {"pll1_96", "pll1_48", "pll1_24", "pll1_12"};
static const char *sdh_parent[] = {"pll1_12", "pll1_13"};
static const char *disp_parent[] = {"pll1_2", "pll1_12"};
static const char *ccic_parent[] = {"pll1_2", "pll1_12"};
static const char *ccic_phy_parent[] = {"pll1_6", "pll1_12"};

void __init pxa168_clk_init(void)
{
	struct clk *clk;
	struct clk *uart_pll;
	void __iomem *mpmu_base;
	void __iomem *apmu_base;
	void __iomem *apbc_base;

	mpmu_base = ioremap(APB_PHYS_BASE + 0x50000, SZ_4K);
	if (mpmu_base == NULL) {
		pr_err("error to ioremap MPMU base\n");
		return;
	}

	apmu_base = ioremap(AXI_PHYS_BASE + 0x82800, SZ_4K);
	if (apmu_base == NULL) {
		pr_err("error to ioremap APMU base\n");
		return;
	}

	apbc_base = ioremap(APB_PHYS_BASE + 0x15000, SZ_4K);
	if (apbc_base == NULL) {
		pr_err("error to ioremap APBC base\n");
		return;
	}

	clk = clk_register_fixed_rate(NULL, "clk32", NULL, CLK_IS_ROOT, 3200);
	clk_register_clkdev(clk, "clk32", NULL);

	clk = clk_register_fixed_rate(NULL, "vctcxo", NULL, CLK_IS_ROOT,
				26000000);
	clk_register_clkdev(clk, "vctcxo", NULL);

	clk = clk_register_fixed_rate(NULL, "pll1", NULL, CLK_IS_ROOT,
				624000000);
	clk_register_clkdev(clk, "pll1", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_2", "pll1",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_2", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_4", "pll1_2",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_4", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_8", "pll1_4",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_8", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_16", "pll1_8",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_16", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_6", "pll1_2",
				CLK_SET_RATE_PARENT, 1, 3);
	clk_register_clkdev(clk, "pll1_6", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_12", "pll1_6",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_12", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_24", "pll1_12",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_24", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_48", "pll1_24",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_48", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_96", "pll1_48",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_96", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_13", "pll1",
				CLK_SET_RATE_PARENT, 1, 13);
	clk_register_clkdev(clk, "pll1_13", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_13_1_5", "pll1",
				CLK_SET_RATE_PARENT, 2, 3);
	clk_register_clkdev(clk, "pll1_13_1_5", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_2_1_5", "pll1",
				CLK_SET_RATE_PARENT, 2, 3);
	clk_register_clkdev(clk, "pll1_2_1_5", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_3_16", "pll1",
				CLK_SET_RATE_PARENT, 3, 16);
	clk_register_clkdev(clk, "pll1_3_16", NULL);

	uart_pll = mmp_clk_register_factor("uart_pll", "pll1_4", 0,
				mpmu_base + MPMU_UART_PLL,
				&uart_factor_masks, uart_factor_tbl,
				ARRAY_SIZE(uart_factor_tbl));
	clk_set_rate(uart_pll, 14745600);
	clk_register_clkdev(uart_pll, "uart_pll", NULL);

	clk = mmp_clk_register_apbc("twsi0", "pll1_13_1_5",
				apbc_base + APBC_TWSI0, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.0");

	clk = mmp_clk_register_apbc("twsi1", "pll1_13_1_5",
				apbc_base + APBC_TWSI1, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.1");

	clk = mmp_clk_register_apbc("gpio", "vctcxo",
				apbc_base + APBC_GPIO, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-gpio");

	clk = mmp_clk_register_apbc("kpc", "clk32",
				apbc_base + APBC_KPC, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa27x-keypad");

	clk = mmp_clk_register_apbc("rtc", "clk32",
				apbc_base + APBC_RTC, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "sa1100-rtc");

	clk = mmp_clk_register_apbc("pwm0", "pll1_48",
				apbc_base + APBC_PWM0, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa168-pwm.0");

	clk = mmp_clk_register_apbc("pwm1", "pll1_48",
				apbc_base + APBC_PWM1, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa168-pwm.1");

	clk = mmp_clk_register_apbc("pwm2", "pll1_48",
				apbc_base + APBC_PWM2, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa168-pwm.2");

	clk = mmp_clk_register_apbc("pwm3", "pll1_48",
				apbc_base + APBC_PWM3, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa168-pwm.3");

	clk = clk_register_mux(NULL, "uart0_mux", uart_parent,
				ARRAY_SIZE(uart_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_UART0, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, uart_pll);
	clk_register_clkdev(clk, "uart_mux.0", NULL);

	clk = mmp_clk_register_apbc("uart0", "uart0_mux",
				apbc_base + APBC_UART0, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.0");

	clk = clk_register_mux(NULL, "uart1_mux", uart_parent,
				ARRAY_SIZE(uart_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_UART1, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, uart_pll);
	clk_register_clkdev(clk, "uart_mux.1", NULL);

	clk = mmp_clk_register_apbc("uart1", "uart1_mux",
				apbc_base + APBC_UART1,	10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.1");

	clk = clk_register_mux(NULL, "uart2_mux", uart_parent,
				ARRAY_SIZE(uart_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_UART2, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, uart_pll);
	clk_register_clkdev(clk, "uart_mux.2", NULL);

	clk = mmp_clk_register_apbc("uart2", "uart2_mux",
				apbc_base + APBC_UART2,	10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.2");

	clk = clk_register_mux(NULL, "ssp0_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_SSP0, 4, 3, 0, &clk_lock);
	clk_register_clkdev(clk, "uart_mux.0", NULL);

	clk = mmp_clk_register_apbc("ssp0", "ssp0_mux", apbc_base + APBC_SSP0,
				10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-ssp.0");

	clk = clk_register_mux(NULL, "ssp1_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_SSP1, 4, 3, 0, &clk_lock);
	clk_register_clkdev(clk, "ssp_mux.1", NULL);

	clk = mmp_clk_register_apbc("ssp1", "ssp1_mux", apbc_base + APBC_SSP1,
				10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-ssp.1");

	clk = clk_register_mux(NULL, "ssp2_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_SSP2, 4, 3, 0, &clk_lock);
	clk_register_clkdev(clk, "ssp_mux.2", NULL);

	clk = mmp_clk_register_apbc("ssp2", "ssp1_mux", apbc_base + APBC_SSP2,
				10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-ssp.2");

	clk = clk_register_mux(NULL, "ssp3_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_SSP3, 4, 3, 0, &clk_lock);
	clk_register_clkdev(clk, "ssp_mux.3", NULL);

	clk = mmp_clk_register_apbc("ssp3", "ssp1_mux", apbc_base + APBC_SSP3,
				10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-ssp.3");

	clk = clk_register_mux(NULL, "ssp4_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_SSP4, 4, 3, 0, &clk_lock);
	clk_register_clkdev(clk, "ssp_mux.4", NULL);

	clk = mmp_clk_register_apbc("ssp4", "ssp1_mux", apbc_base + APBC_SSP4,
				10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-ssp.4");

	clk = mmp_clk_register_apmu("dfc", "pll1_4", apmu_base + APMU_DFC,
				0x19b, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa3xx-nand.0");

	clk = clk_register_mux(NULL, "sdh0_mux", sdh_parent,
				ARRAY_SIZE(sdh_parent), CLK_SET_RATE_PARENT,
				apmu_base + APMU_SDH0, 6, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "sdh0_mux", NULL);

	clk = mmp_clk_register_apmu("sdh0", "sdh_mux", apmu_base + APMU_SDH0,
				0x1b, &clk_lock);
	clk_register_clkdev(clk, NULL, "sdhci-pxa.0");

	clk = clk_register_mux(NULL, "sdh1_mux", sdh_parent,
				ARRAY_SIZE(sdh_parent), CLK_SET_RATE_PARENT,
				apmu_base + APMU_SDH1, 6, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "sdh1_mux", NULL);

	clk = mmp_clk_register_apmu("sdh1", "sdh1_mux", apmu_base + APMU_SDH1,
				0x1b, &clk_lock);
	clk_register_clkdev(clk, NULL, "sdhci-pxa.1");

	clk = mmp_clk_register_apmu("usb", "usb_pll", apmu_base + APMU_USB,
				0x9, &clk_lock);
	clk_register_clkdev(clk, "usb_clk", NULL);

	clk = mmp_clk_register_apmu("sph", "usb_pll", apmu_base + APMU_USB,
				0x12, &clk_lock);
	clk_register_clkdev(clk, "sph_clk", NULL);

	clk = clk_register_mux(NULL, "disp0_mux", disp_parent,
				ARRAY_SIZE(disp_parent), CLK_SET_RATE_PARENT,
				apmu_base + APMU_DISP0, 6, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "disp_mux.0", NULL);

	clk = mmp_clk_register_apmu("disp0", "disp0_mux",
				apmu_base + APMU_DISP0, 0x1b, &clk_lock);
	clk_register_clkdev(clk, "fnclk", "mmp-disp.0");

	clk = mmp_clk_register_apmu("disp0_hclk", "disp0_mux",
				apmu_base + APMU_DISP0, 0x24, &clk_lock);
	clk_register_clkdev(clk, "hclk", "mmp-disp.0");

	clk = clk_register_mux(NULL, "ccic0_mux", ccic_parent,
				ARRAY_SIZE(ccic_parent), CLK_SET_RATE_PARENT,
				apmu_base + APMU_CCIC0, 6, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "ccic_mux.0", NULL);

	clk = mmp_clk_register_apmu("ccic0", "ccic0_mux",
				apmu_base + APMU_CCIC0, 0x1b, &clk_lock);
	clk_register_clkdev(clk, "fnclk", "mmp-ccic.0");

	clk = clk_register_mux(NULL, "ccic0_phy_mux", ccic_phy_parent,
				ARRAY_SIZE(ccic_phy_parent),
				CLK_SET_RATE_PARENT, apmu_base + APMU_CCIC0,
				7, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "ccic_phy_mux.0", NULL);

	clk = mmp_clk_register_apmu("ccic0_phy", "ccic0_phy_mux",
				apmu_base + APMU_CCIC0, 0x24, &clk_lock);
	clk_register_clkdev(clk, "phyclk", "mmp-ccic.0");

	clk = clk_register_divider(NULL, "ccic0_sphy_div", "ccic0_mux",
				CLK_SET_RATE_PARENT, apmu_base + APMU_CCIC0,
				10, 5, 0, &clk_lock);
	clk_register_clkdev(clk, "sphyclk_div", NULL);

	clk = mmp_clk_register_apmu("ccic0_sphy", "ccic0_sphy_div",
				apmu_base + APMU_CCIC0, 0x300, &clk_lock);
	clk_register_clkdev(clk, "sphyclk", "mmp-ccic.0");
}
