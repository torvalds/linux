/*
 * mmp2 clock framework source file
 *
 * Copyright (C) 2012 Marvell
 * Chao Xie <xiechao.mail@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk/mmp.h>

#include "clk.h"

#define APBC_RTC	0x0
#define APBC_TWSI0	0x4
#define APBC_TWSI1	0x8
#define APBC_TWSI2	0xc
#define APBC_TWSI3	0x10
#define APBC_TWSI4	0x7c
#define APBC_TWSI5	0x80
#define APBC_KPC	0x18
#define APBC_UART0	0x2c
#define APBC_UART1	0x30
#define APBC_UART2	0x34
#define APBC_UART3	0x88
#define APBC_GPIO	0x38
#define APBC_PWM0	0x3c
#define APBC_PWM1	0x40
#define APBC_PWM2	0x44
#define APBC_PWM3	0x48
#define APBC_SSP0	0x50
#define APBC_SSP1	0x54
#define APBC_SSP2	0x58
#define APBC_SSP3	0x5c
#define APMU_SDH0	0x54
#define APMU_SDH1	0x58
#define APMU_SDH2	0xe8
#define APMU_SDH3	0xec
#define APMU_USB	0x5c
#define APMU_DISP0	0x4c
#define APMU_DISP1	0x110
#define APMU_CCIC0	0x50
#define APMU_CCIC1	0xf4
#define MPMU_UART_PLL	0x14

static DEFINE_SPINLOCK(clk_lock);

static struct mmp_clk_factor_masks uart_factor_masks = {
	.factor = 2,
	.num_mask = 0x1fff,
	.den_mask = 0x1fff,
	.num_shift = 16,
	.den_shift = 0,
};

static struct mmp_clk_factor_tbl uart_factor_tbl[] = {
	{.num = 8125, .den = 1536},	/*14.745MHZ */
	{.num = 3521, .den = 689},	/*19.23MHZ */
};

static const char *uart_parent[] = {"uart_pll", "vctcxo"};
static const char *ssp_parent[] = {"vctcxo_4", "vctcxo_2", "vctcxo", "pll1_16"};
static const char *sdh_parent[] = {"pll1_4", "pll2", "usb_pll", "pll1"};
static const char *disp_parent[] = {"pll1", "pll1_16", "pll2", "vctcxo"};
static const char *ccic_parent[] = {"pll1_2", "pll1_16", "vctcxo"};

void __init mmp2_clk_init(phys_addr_t mpmu_phys, phys_addr_t apmu_phys,
			  phys_addr_t apbc_phys)
{
	struct clk *clk;
	struct clk *vctcxo;
	void __iomem *mpmu_base;
	void __iomem *apmu_base;
	void __iomem *apbc_base;

	mpmu_base = ioremap(mpmu_phys, SZ_4K);
	if (mpmu_base == NULL) {
		pr_err("error to ioremap MPMU base\n");
		return;
	}

	apmu_base = ioremap(apmu_phys, SZ_4K);
	if (apmu_base == NULL) {
		pr_err("error to ioremap APMU base\n");
		return;
	}

	apbc_base = ioremap(apbc_phys, SZ_4K);
	if (apbc_base == NULL) {
		pr_err("error to ioremap APBC base\n");
		return;
	}

	clk = clk_register_fixed_rate(NULL, "clk32", NULL, 0, 3200);
	clk_register_clkdev(clk, "clk32", NULL);

	vctcxo = clk_register_fixed_rate(NULL, "vctcxo", NULL, 0, 26000000);
	clk_register_clkdev(vctcxo, "vctcxo", NULL);

	clk = clk_register_fixed_rate(NULL, "pll1", NULL, 0, 800000000);
	clk_register_clkdev(clk, "pll1", NULL);

	clk = clk_register_fixed_rate(NULL, "usb_pll", NULL, 0, 480000000);
	clk_register_clkdev(clk, "usb_pll", NULL);

	clk = clk_register_fixed_rate(NULL, "pll2", NULL, 0, 960000000);
	clk_register_clkdev(clk, "pll2", NULL);

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

	clk = clk_register_fixed_factor(NULL, "pll1_20", "pll1_4",
				CLK_SET_RATE_PARENT, 1, 5);
	clk_register_clkdev(clk, "pll1_20", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_3", "pll1",
				CLK_SET_RATE_PARENT, 1, 3);
	clk_register_clkdev(clk, "pll1_3", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_6", "pll1_3",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_6", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_12", "pll1_6",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll1_12", NULL);

	clk = clk_register_fixed_factor(NULL, "pll2_2", "pll2",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll2_2", NULL);

	clk = clk_register_fixed_factor(NULL, "pll2_4", "pll2_2",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll2_4", NULL);

	clk = clk_register_fixed_factor(NULL, "pll2_8", "pll2_4",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll2_8", NULL);

	clk = clk_register_fixed_factor(NULL, "pll2_16", "pll2_8",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll2_16", NULL);

	clk = clk_register_fixed_factor(NULL, "pll2_3", "pll2",
				CLK_SET_RATE_PARENT, 1, 3);
	clk_register_clkdev(clk, "pll2_3", NULL);

	clk = clk_register_fixed_factor(NULL, "pll2_6", "pll2_3",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll2_6", NULL);

	clk = clk_register_fixed_factor(NULL, "pll2_12", "pll2_6",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll2_12", NULL);

	clk = clk_register_fixed_factor(NULL, "vctcxo_2", "vctcxo",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "vctcxo_2", NULL);

	clk = clk_register_fixed_factor(NULL, "vctcxo_4", "vctcxo_2",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "vctcxo_4", NULL);

	clk = mmp_clk_register_factor("uart_pll", "pll1_4", 0,
				mpmu_base + MPMU_UART_PLL,
				&uart_factor_masks, uart_factor_tbl,
				ARRAY_SIZE(uart_factor_tbl), &clk_lock);
	clk_set_rate(clk, 14745600);
	clk_register_clkdev(clk, "uart_pll", NULL);

	clk = mmp_clk_register_apbc("twsi0", "vctcxo",
				apbc_base + APBC_TWSI0, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.0");

	clk = mmp_clk_register_apbc("twsi1", "vctcxo",
				apbc_base + APBC_TWSI1, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.1");

	clk = mmp_clk_register_apbc("twsi2", "vctcxo",
				apbc_base + APBC_TWSI2, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.2");

	clk = mmp_clk_register_apbc("twsi3", "vctcxo",
				apbc_base + APBC_TWSI3, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.3");

	clk = mmp_clk_register_apbc("twsi4", "vctcxo",
				apbc_base + APBC_TWSI4, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.4");

	clk = mmp_clk_register_apbc("twsi5", "vctcxo",
				apbc_base + APBC_TWSI5, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.5");

	clk = mmp_clk_register_apbc("gpio", "vctcxo",
				apbc_base + APBC_GPIO, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp2-gpio");

	clk = mmp_clk_register_apbc("kpc", "clk32",
				apbc_base + APBC_KPC, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa27x-keypad");

	clk = mmp_clk_register_apbc("rtc", "clk32",
				apbc_base + APBC_RTC, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-rtc");

	clk = mmp_clk_register_apbc("pwm0", "vctcxo",
				apbc_base + APBC_PWM0, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp2-pwm.0");

	clk = mmp_clk_register_apbc("pwm1", "vctcxo",
				apbc_base + APBC_PWM1, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp2-pwm.1");

	clk = mmp_clk_register_apbc("pwm2", "vctcxo",
				apbc_base + APBC_PWM2, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp2-pwm.2");

	clk = mmp_clk_register_apbc("pwm3", "vctcxo",
				apbc_base + APBC_PWM3, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp2-pwm.3");

	clk = clk_register_mux(NULL, "uart0_mux", uart_parent,
				ARRAY_SIZE(uart_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apbc_base + APBC_UART0, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, vctcxo);
	clk_register_clkdev(clk, "uart_mux.0", NULL);

	clk = mmp_clk_register_apbc("uart0", "uart0_mux",
				apbc_base + APBC_UART0, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.0");

	clk = clk_register_mux(NULL, "uart1_mux", uart_parent,
				ARRAY_SIZE(uart_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apbc_base + APBC_UART1, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, vctcxo);
	clk_register_clkdev(clk, "uart_mux.1", NULL);

	clk = mmp_clk_register_apbc("uart1", "uart1_mux",
				apbc_base + APBC_UART1, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.1");

	clk = clk_register_mux(NULL, "uart2_mux", uart_parent,
				ARRAY_SIZE(uart_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apbc_base + APBC_UART2, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, vctcxo);
	clk_register_clkdev(clk, "uart_mux.2", NULL);

	clk = mmp_clk_register_apbc("uart2", "uart2_mux",
				apbc_base + APBC_UART2, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.2");

	clk = clk_register_mux(NULL, "uart3_mux", uart_parent,
				ARRAY_SIZE(uart_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apbc_base + APBC_UART3, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, vctcxo);
	clk_register_clkdev(clk, "uart_mux.3", NULL);

	clk = mmp_clk_register_apbc("uart3", "uart3_mux",
				apbc_base + APBC_UART3, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.3");

	clk = clk_register_mux(NULL, "ssp0_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apbc_base + APBC_SSP0, 4, 3, 0, &clk_lock);
	clk_register_clkdev(clk, "uart_mux.0", NULL);

	clk = mmp_clk_register_apbc("ssp0", "ssp0_mux",
				apbc_base + APBC_SSP0, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-ssp.0");

	clk = clk_register_mux(NULL, "ssp1_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apbc_base + APBC_SSP1, 4, 3, 0, &clk_lock);
	clk_register_clkdev(clk, "ssp_mux.1", NULL);

	clk = mmp_clk_register_apbc("ssp1", "ssp1_mux",
				apbc_base + APBC_SSP1, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-ssp.1");

	clk = clk_register_mux(NULL, "ssp2_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apbc_base + APBC_SSP2, 4, 3, 0, &clk_lock);
	clk_register_clkdev(clk, "ssp_mux.2", NULL);

	clk = mmp_clk_register_apbc("ssp2", "ssp2_mux",
				apbc_base + APBC_SSP2, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-ssp.2");

	clk = clk_register_mux(NULL, "ssp3_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apbc_base + APBC_SSP3, 4, 3, 0, &clk_lock);
	clk_register_clkdev(clk, "ssp_mux.3", NULL);

	clk = mmp_clk_register_apbc("ssp3", "ssp3_mux",
				apbc_base + APBC_SSP3, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-ssp.3");

	clk = clk_register_mux(NULL, "sdh_mux", sdh_parent,
				ARRAY_SIZE(sdh_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apmu_base + APMU_SDH0, 8, 2, 0, &clk_lock);
	clk_register_clkdev(clk, "sdh_mux", NULL);

	clk = clk_register_divider(NULL, "sdh_div", "sdh_mux",
				CLK_SET_RATE_PARENT, apmu_base + APMU_SDH0,
				10, 4, CLK_DIVIDER_ONE_BASED, &clk_lock);
	clk_register_clkdev(clk, "sdh_div", NULL);

	clk = mmp_clk_register_apmu("sdh0", "sdh_div", apmu_base + APMU_SDH0,
				0x1b, &clk_lock);
	clk_register_clkdev(clk, NULL, "sdhci-pxav3.0");

	clk = mmp_clk_register_apmu("sdh1", "sdh_div", apmu_base + APMU_SDH1,
				0x1b, &clk_lock);
	clk_register_clkdev(clk, NULL, "sdhci-pxav3.1");

	clk = mmp_clk_register_apmu("sdh2", "sdh_div", apmu_base + APMU_SDH2,
				0x1b, &clk_lock);
	clk_register_clkdev(clk, NULL, "sdhci-pxav3.2");

	clk = mmp_clk_register_apmu("sdh3", "sdh_div", apmu_base + APMU_SDH3,
				0x1b, &clk_lock);
	clk_register_clkdev(clk, NULL, "sdhci-pxav3.3");

	clk = mmp_clk_register_apmu("usb", "usb_pll", apmu_base + APMU_USB,
				0x9, &clk_lock);
	clk_register_clkdev(clk, "usb_clk", NULL);

	clk = clk_register_mux(NULL, "disp0_mux", disp_parent,
				ARRAY_SIZE(disp_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apmu_base + APMU_DISP0, 6, 2, 0, &clk_lock);
	clk_register_clkdev(clk, "disp_mux.0", NULL);

	clk = clk_register_divider(NULL, "disp0_div", "disp0_mux",
				CLK_SET_RATE_PARENT, apmu_base + APMU_DISP0,
				8, 4, CLK_DIVIDER_ONE_BASED, &clk_lock);
	clk_register_clkdev(clk, "disp_div.0", NULL);

	clk = mmp_clk_register_apmu("disp0", "disp0_div",
				apmu_base + APMU_DISP0, 0x1b, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-disp.0");

	clk = clk_register_divider(NULL, "disp0_sphy_div", "disp0_mux", 0,
				apmu_base + APMU_DISP0, 15, 5, 0, &clk_lock);
	clk_register_clkdev(clk, "disp_sphy_div.0", NULL);

	clk = mmp_clk_register_apmu("disp0_sphy", "disp0_sphy_div",
				apmu_base + APMU_DISP0, 0x1024, &clk_lock);
	clk_register_clkdev(clk, "disp_sphy.0", NULL);

	clk = clk_register_mux(NULL, "disp1_mux", disp_parent,
				ARRAY_SIZE(disp_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apmu_base + APMU_DISP1, 6, 2, 0, &clk_lock);
	clk_register_clkdev(clk, "disp_mux.1", NULL);

	clk = clk_register_divider(NULL, "disp1_div", "disp1_mux",
				CLK_SET_RATE_PARENT, apmu_base + APMU_DISP1,
				8, 4, CLK_DIVIDER_ONE_BASED, &clk_lock);
	clk_register_clkdev(clk, "disp_div.1", NULL);

	clk = mmp_clk_register_apmu("disp1", "disp1_div",
				apmu_base + APMU_DISP1, 0x1b, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-disp.1");

	clk = mmp_clk_register_apmu("ccic_arbiter", "vctcxo",
				apmu_base + APMU_CCIC0, 0x1800, &clk_lock);
	clk_register_clkdev(clk, "ccic_arbiter", NULL);

	clk = clk_register_mux(NULL, "ccic0_mux", ccic_parent,
				ARRAY_SIZE(ccic_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apmu_base + APMU_CCIC0, 6, 2, 0, &clk_lock);
	clk_register_clkdev(clk, "ccic_mux.0", NULL);

	clk = clk_register_divider(NULL, "ccic0_div", "ccic0_mux",
				CLK_SET_RATE_PARENT, apmu_base + APMU_CCIC0,
				17, 4, CLK_DIVIDER_ONE_BASED, &clk_lock);
	clk_register_clkdev(clk, "ccic_div.0", NULL);

	clk = mmp_clk_register_apmu("ccic0", "ccic0_div",
				apmu_base + APMU_CCIC0, 0x1b, &clk_lock);
	clk_register_clkdev(clk, "fnclk", "mmp-ccic.0");

	clk = mmp_clk_register_apmu("ccic0_phy", "ccic0_div",
				apmu_base + APMU_CCIC0, 0x24, &clk_lock);
	clk_register_clkdev(clk, "phyclk", "mmp-ccic.0");

	clk = clk_register_divider(NULL, "ccic0_sphy_div", "ccic0_div",
				CLK_SET_RATE_PARENT, apmu_base + APMU_CCIC0,
				10, 5, 0, &clk_lock);
	clk_register_clkdev(clk, "sphyclk_div", "mmp-ccic.0");

	clk = mmp_clk_register_apmu("ccic0_sphy", "ccic0_sphy_div",
				apmu_base + APMU_CCIC0, 0x300, &clk_lock);
	clk_register_clkdev(clk, "sphyclk", "mmp-ccic.0");

	clk = clk_register_mux(NULL, "ccic1_mux", ccic_parent,
				ARRAY_SIZE(ccic_parent),
				CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
				apmu_base + APMU_CCIC1, 6, 2, 0, &clk_lock);
	clk_register_clkdev(clk, "ccic_mux.1", NULL);

	clk = clk_register_divider(NULL, "ccic1_div", "ccic1_mux",
				CLK_SET_RATE_PARENT, apmu_base + APMU_CCIC1,
				16, 4, CLK_DIVIDER_ONE_BASED, &clk_lock);
	clk_register_clkdev(clk, "ccic_div.1", NULL);

	clk = mmp_clk_register_apmu("ccic1", "ccic1_div",
				apmu_base + APMU_CCIC1, 0x1b, &clk_lock);
	clk_register_clkdev(clk, "fnclk", "mmp-ccic.1");

	clk = mmp_clk_register_apmu("ccic1_phy", "ccic1_div",
				apmu_base + APMU_CCIC1, 0x24, &clk_lock);
	clk_register_clkdev(clk, "phyclk", "mmp-ccic.1");

	clk = clk_register_divider(NULL, "ccic1_sphy_div", "ccic1_div",
				CLK_SET_RATE_PARENT, apmu_base + APMU_CCIC1,
				10, 5, 0, &clk_lock);
	clk_register_clkdev(clk, "sphyclk_div", "mmp-ccic.1");

	clk = mmp_clk_register_apmu("ccic1_sphy", "ccic1_sphy_div",
				apmu_base + APMU_CCIC1, 0x300, &clk_lock);
	clk_register_clkdev(clk, "sphyclk", "mmp-ccic.1");
}
