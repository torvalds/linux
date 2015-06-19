/*
 * Hisilicon Hi3620 clock driver
 *
 * Copyright (c) 2012-2013 Hisilicon Limited.
 * Copyright (c) 2012-2013 Linaro Limited.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
 *	   Xin Li <li.xin@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/hi3620-clock.h>

#include "clk.h"

/* clock parent list */
static const char *const timer0_mux_p[] __initconst = { "osc32k", "timerclk01", };
static const char *const timer1_mux_p[] __initconst = { "osc32k", "timerclk01", };
static const char *const timer2_mux_p[] __initconst = { "osc32k", "timerclk23", };
static const char *const timer3_mux_p[] __initconst = { "osc32k", "timerclk23", };
static const char *const timer4_mux_p[] __initconst = { "osc32k", "timerclk45", };
static const char *const timer5_mux_p[] __initconst = { "osc32k", "timerclk45", };
static const char *const timer6_mux_p[] __initconst = { "osc32k", "timerclk67", };
static const char *const timer7_mux_p[] __initconst = { "osc32k", "timerclk67", };
static const char *const timer8_mux_p[] __initconst = { "osc32k", "timerclk89", };
static const char *const timer9_mux_p[] __initconst = { "osc32k", "timerclk89", };
static const char *const uart0_mux_p[] __initconst = { "osc26m", "pclk", };
static const char *const uart1_mux_p[] __initconst = { "osc26m", "pclk", };
static const char *const uart2_mux_p[] __initconst = { "osc26m", "pclk", };
static const char *const uart3_mux_p[] __initconst = { "osc26m", "pclk", };
static const char *const uart4_mux_p[] __initconst = { "osc26m", "pclk", };
static const char *const spi0_mux_p[] __initconst = { "osc26m", "rclk_cfgaxi", };
static const char *const spi1_mux_p[] __initconst = { "osc26m", "rclk_cfgaxi", };
static const char *const spi2_mux_p[] __initconst = { "osc26m", "rclk_cfgaxi", };
/* share axi parent */
static const char *const saxi_mux_p[] __initconst = { "armpll3", "armpll2", };
static const char *const pwm0_mux_p[] __initconst = { "osc32k", "osc26m", };
static const char *const pwm1_mux_p[] __initconst = { "osc32k", "osc26m", };
static const char *const sd_mux_p[] __initconst = { "armpll2", "armpll3", };
static const char *const mmc1_mux_p[] __initconst = { "armpll2", "armpll3", };
static const char *const mmc1_mux2_p[] __initconst = { "osc26m", "mmc1_div", };
static const char *const g2d_mux_p[] __initconst = { "armpll2", "armpll3", };
static const char *const venc_mux_p[] __initconst = { "armpll2", "armpll3", };
static const char *const vdec_mux_p[] __initconst = { "armpll2", "armpll3", };
static const char *const vpp_mux_p[] __initconst = { "armpll2", "armpll3", };
static const char *const edc0_mux_p[] __initconst = { "armpll2", "armpll3", };
static const char *const ldi0_mux_p[] __initconst = { "armpll2", "armpll4",
					     "armpll3", "armpll5", };
static const char *const edc1_mux_p[] __initconst = { "armpll2", "armpll3", };
static const char *const ldi1_mux_p[] __initconst = { "armpll2", "armpll4",
					     "armpll3", "armpll5", };
static const char *const rclk_hsic_p[] __initconst = { "armpll3", "armpll2", };
static const char *const mmc2_mux_p[] __initconst = { "armpll2", "armpll3", };
static const char *const mmc3_mux_p[] __initconst = { "armpll2", "armpll3", };


/* fixed rate clocks */
static struct hisi_fixed_rate_clock hi3620_fixed_rate_clks[] __initdata = {
	{ HI3620_OSC32K,   "osc32k",   NULL, CLK_IS_ROOT, 32768, },
	{ HI3620_OSC26M,   "osc26m",   NULL, CLK_IS_ROOT, 26000000, },
	{ HI3620_PCLK,     "pclk",     NULL, CLK_IS_ROOT, 26000000, },
	{ HI3620_PLL_ARM0, "armpll0",  NULL, CLK_IS_ROOT, 1600000000, },
	{ HI3620_PLL_ARM1, "armpll1",  NULL, CLK_IS_ROOT, 1600000000, },
	{ HI3620_PLL_PERI, "armpll2",  NULL, CLK_IS_ROOT, 1440000000, },
	{ HI3620_PLL_USB,  "armpll3",  NULL, CLK_IS_ROOT, 1440000000, },
	{ HI3620_PLL_HDMI, "armpll4",  NULL, CLK_IS_ROOT, 1188000000, },
	{ HI3620_PLL_GPU,  "armpll5",  NULL, CLK_IS_ROOT, 1300000000, },
};

/* fixed factor clocks */
static struct hisi_fixed_factor_clock hi3620_fixed_factor_clks[] __initdata = {
	{ HI3620_RCLK_TCXO,   "rclk_tcxo",   "osc26m",   1, 4,  0, },
	{ HI3620_RCLK_CFGAXI, "rclk_cfgaxi", "armpll2",  1, 30, 0, },
	{ HI3620_RCLK_PICO,   "rclk_pico",   "hsic_div", 1, 40, 0, },
};

static struct hisi_mux_clock hi3620_mux_clks[] __initdata = {
	{ HI3620_TIMER0_MUX, "timer0_mux", timer0_mux_p, ARRAY_SIZE(timer0_mux_p), CLK_SET_RATE_PARENT, 0,     15, 2, 0,                   },
	{ HI3620_TIMER1_MUX, "timer1_mux", timer1_mux_p, ARRAY_SIZE(timer1_mux_p), CLK_SET_RATE_PARENT, 0,     17, 2, 0,                   },
	{ HI3620_TIMER2_MUX, "timer2_mux", timer2_mux_p, ARRAY_SIZE(timer2_mux_p), CLK_SET_RATE_PARENT, 0,     19, 2, 0,                   },
	{ HI3620_TIMER3_MUX, "timer3_mux", timer3_mux_p, ARRAY_SIZE(timer3_mux_p), CLK_SET_RATE_PARENT, 0,     21, 2, 0,                   },
	{ HI3620_TIMER4_MUX, "timer4_mux", timer4_mux_p, ARRAY_SIZE(timer4_mux_p), CLK_SET_RATE_PARENT, 0x18,  0,  2, 0,                   },
	{ HI3620_TIMER5_MUX, "timer5_mux", timer5_mux_p, ARRAY_SIZE(timer5_mux_p), CLK_SET_RATE_PARENT, 0x18,  2,  2, 0,                   },
	{ HI3620_TIMER6_MUX, "timer6_mux", timer6_mux_p, ARRAY_SIZE(timer6_mux_p), CLK_SET_RATE_PARENT, 0x18,  4,  2, 0,                   },
	{ HI3620_TIMER7_MUX, "timer7_mux", timer7_mux_p, ARRAY_SIZE(timer7_mux_p), CLK_SET_RATE_PARENT, 0x18,  6,  2, 0,                   },
	{ HI3620_TIMER8_MUX, "timer8_mux", timer8_mux_p, ARRAY_SIZE(timer8_mux_p), CLK_SET_RATE_PARENT, 0x18,  8,  2, 0,                   },
	{ HI3620_TIMER9_MUX, "timer9_mux", timer9_mux_p, ARRAY_SIZE(timer9_mux_p), CLK_SET_RATE_PARENT, 0x18,  10, 2, 0,                   },
	{ HI3620_UART0_MUX,  "uart0_mux",  uart0_mux_p,  ARRAY_SIZE(uart0_mux_p),  CLK_SET_RATE_PARENT, 0x100, 7,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_UART1_MUX,  "uart1_mux",  uart1_mux_p,  ARRAY_SIZE(uart1_mux_p),  CLK_SET_RATE_PARENT, 0x100, 8,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_UART2_MUX,  "uart2_mux",  uart2_mux_p,  ARRAY_SIZE(uart2_mux_p),  CLK_SET_RATE_PARENT, 0x100, 9,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_UART3_MUX,  "uart3_mux",  uart3_mux_p,  ARRAY_SIZE(uart3_mux_p),  CLK_SET_RATE_PARENT, 0x100, 10, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_UART4_MUX,  "uart4_mux",  uart4_mux_p,  ARRAY_SIZE(uart4_mux_p),  CLK_SET_RATE_PARENT, 0x100, 11, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_SPI0_MUX,   "spi0_mux",   spi0_mux_p,   ARRAY_SIZE(spi0_mux_p),   CLK_SET_RATE_PARENT, 0x100, 12, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_SPI1_MUX,   "spi1_mux",   spi1_mux_p,   ARRAY_SIZE(spi1_mux_p),   CLK_SET_RATE_PARENT, 0x100, 13, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_SPI2_MUX,   "spi2_mux",   spi2_mux_p,   ARRAY_SIZE(spi2_mux_p),   CLK_SET_RATE_PARENT, 0x100, 14, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_SAXI_MUX,   "saxi_mux",   saxi_mux_p,   ARRAY_SIZE(saxi_mux_p),   CLK_SET_RATE_PARENT, 0x100, 15, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_PWM0_MUX,   "pwm0_mux",   pwm0_mux_p,   ARRAY_SIZE(pwm0_mux_p),   CLK_SET_RATE_PARENT, 0x104, 10, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_PWM1_MUX,   "pwm1_mux",   pwm1_mux_p,   ARRAY_SIZE(pwm1_mux_p),   CLK_SET_RATE_PARENT, 0x104, 11, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_SD_MUX,     "sd_mux",     sd_mux_p,     ARRAY_SIZE(sd_mux_p),     CLK_SET_RATE_PARENT, 0x108, 4,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_MMC1_MUX,   "mmc1_mux",   mmc1_mux_p,   ARRAY_SIZE(mmc1_mux_p),   CLK_SET_RATE_PARENT, 0x108, 9,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_MMC1_MUX2,  "mmc1_mux2",  mmc1_mux2_p,  ARRAY_SIZE(mmc1_mux2_p),  CLK_SET_RATE_PARENT, 0x108, 10, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_G2D_MUX,    "g2d_mux",    g2d_mux_p,    ARRAY_SIZE(g2d_mux_p),    CLK_SET_RATE_PARENT, 0x10c, 5,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_VENC_MUX,   "venc_mux",   venc_mux_p,   ARRAY_SIZE(venc_mux_p),   CLK_SET_RATE_PARENT, 0x10c, 11, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_VDEC_MUX,   "vdec_mux",   vdec_mux_p,   ARRAY_SIZE(vdec_mux_p),   CLK_SET_RATE_PARENT, 0x110, 5,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_VPP_MUX,    "vpp_mux",    vpp_mux_p,    ARRAY_SIZE(vpp_mux_p),    CLK_SET_RATE_PARENT, 0x110, 11, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_EDC0_MUX,   "edc0_mux",   edc0_mux_p,   ARRAY_SIZE(edc0_mux_p),   CLK_SET_RATE_PARENT, 0x114, 6,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_LDI0_MUX,   "ldi0_mux",   ldi0_mux_p,   ARRAY_SIZE(ldi0_mux_p),   CLK_SET_RATE_PARENT, 0x114, 13, 2, CLK_MUX_HIWORD_MASK, },
	{ HI3620_EDC1_MUX,   "edc1_mux",   edc1_mux_p,   ARRAY_SIZE(edc1_mux_p),   CLK_SET_RATE_PARENT, 0x118, 6,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_LDI1_MUX,   "ldi1_mux",   ldi1_mux_p,   ARRAY_SIZE(ldi1_mux_p),   CLK_SET_RATE_PARENT, 0x118, 14, 2, CLK_MUX_HIWORD_MASK, },
	{ HI3620_RCLK_HSIC,  "rclk_hsic",  rclk_hsic_p,  ARRAY_SIZE(rclk_hsic_p),  CLK_SET_RATE_PARENT, 0x130, 2,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_MMC2_MUX,   "mmc2_mux",   mmc2_mux_p,   ARRAY_SIZE(mmc2_mux_p),   CLK_SET_RATE_PARENT, 0x140, 4,  1, CLK_MUX_HIWORD_MASK, },
	{ HI3620_MMC3_MUX,   "mmc3_mux",   mmc3_mux_p,   ARRAY_SIZE(mmc3_mux_p),   CLK_SET_RATE_PARENT, 0x140, 9,  1, CLK_MUX_HIWORD_MASK, },
};

static struct hisi_divider_clock hi3620_div_clks[] __initdata = {
	{ HI3620_SHAREAXI_DIV, "saxi_div",   "saxi_mux",  0, 0x100, 0, 5, CLK_DIVIDER_HIWORD_MASK, NULL, },
	{ HI3620_CFGAXI_DIV,   "cfgaxi_div", "saxi_div",  0, 0x100, 5, 2, CLK_DIVIDER_HIWORD_MASK, NULL, },
	{ HI3620_SD_DIV,       "sd_div",     "sd_mux",	  0, 0x108, 0, 4, CLK_DIVIDER_HIWORD_MASK, NULL, },
	{ HI3620_MMC1_DIV,     "mmc1_div",   "mmc1_mux",  0, 0x108, 5, 4, CLK_DIVIDER_HIWORD_MASK, NULL, },
	{ HI3620_HSIC_DIV,     "hsic_div",   "rclk_hsic", 0, 0x130, 0, 2, CLK_DIVIDER_HIWORD_MASK, NULL, },
	{ HI3620_MMC2_DIV,     "mmc2_div",   "mmc2_mux",  0, 0x140, 0, 4, CLK_DIVIDER_HIWORD_MASK, NULL, },
	{ HI3620_MMC3_DIV,     "mmc3_div",   "mmc3_mux",  0, 0x140, 5, 4, CLK_DIVIDER_HIWORD_MASK, NULL, },
};

static struct hisi_gate_clock hi3620_seperated_gate_clks[] __initdata = {
	{ HI3620_TIMERCLK01,   "timerclk01",   "timer_rclk01", CLK_SET_RATE_PARENT, 0x20, 0, 0, },
	{ HI3620_TIMER_RCLK01, "timer_rclk01", "rclk_tcxo",    CLK_SET_RATE_PARENT, 0x20, 1, 0, },
	{ HI3620_TIMERCLK23,   "timerclk23",   "timer_rclk23", CLK_SET_RATE_PARENT, 0x20, 2, 0, },
	{ HI3620_TIMER_RCLK23, "timer_rclk23", "rclk_tcxo",    CLK_SET_RATE_PARENT, 0x20, 3, 0, },
	{ HI3620_RTCCLK,       "rtcclk",       "pclk",         CLK_SET_RATE_PARENT, 0x20, 5, 0, },
	{ HI3620_KPC_CLK,      "kpc_clk",      "pclk",         CLK_SET_RATE_PARENT, 0x20, 6, 0, },
	{ HI3620_GPIOCLK0,     "gpioclk0",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 8, 0, },
	{ HI3620_GPIOCLK1,     "gpioclk1",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 9, 0, },
	{ HI3620_GPIOCLK2,     "gpioclk2",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 10, 0, },
	{ HI3620_GPIOCLK3,     "gpioclk3",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 11, 0, },
	{ HI3620_GPIOCLK4,     "gpioclk4",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 12, 0, },
	{ HI3620_GPIOCLK5,     "gpioclk5",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 13, 0, },
	{ HI3620_GPIOCLK6,     "gpioclk6",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 14, 0, },
	{ HI3620_GPIOCLK7,     "gpioclk7",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 15, 0, },
	{ HI3620_GPIOCLK8,     "gpioclk8",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 16, 0, },
	{ HI3620_GPIOCLK9,     "gpioclk9",     "pclk",         CLK_SET_RATE_PARENT, 0x20, 17, 0, },
	{ HI3620_GPIOCLK10,    "gpioclk10",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 18, 0, },
	{ HI3620_GPIOCLK11,    "gpioclk11",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 19, 0, },
	{ HI3620_GPIOCLK12,    "gpioclk12",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 20, 0, },
	{ HI3620_GPIOCLK13,    "gpioclk13",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 21, 0, },
	{ HI3620_GPIOCLK14,    "gpioclk14",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 22, 0, },
	{ HI3620_GPIOCLK15,    "gpioclk15",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 23, 0, },
	{ HI3620_GPIOCLK16,    "gpioclk16",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 24, 0, },
	{ HI3620_GPIOCLK17,    "gpioclk17",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 25, 0, },
	{ HI3620_GPIOCLK18,    "gpioclk18",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 26, 0, },
	{ HI3620_GPIOCLK19,    "gpioclk19",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 27, 0, },
	{ HI3620_GPIOCLK20,    "gpioclk20",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 28, 0, },
	{ HI3620_GPIOCLK21,    "gpioclk21",    "pclk",         CLK_SET_RATE_PARENT, 0x20, 29, 0, },
	{ HI3620_DPHY0_CLK,    "dphy0_clk",    "osc26m",       CLK_SET_RATE_PARENT, 0x30, 15, 0, },
	{ HI3620_DPHY1_CLK,    "dphy1_clk",    "osc26m",       CLK_SET_RATE_PARENT, 0x30, 16, 0, },
	{ HI3620_DPHY2_CLK,    "dphy2_clk",    "osc26m",       CLK_SET_RATE_PARENT, 0x30, 17, 0, },
	{ HI3620_USBPHY_CLK,   "usbphy_clk",   "rclk_pico",    CLK_SET_RATE_PARENT, 0x30, 24, 0, },
	{ HI3620_ACP_CLK,      "acp_clk",      "rclk_cfgaxi",  CLK_SET_RATE_PARENT, 0x30, 28, 0, },
	{ HI3620_TIMERCLK45,   "timerclk45",   "rclk_tcxo",    CLK_SET_RATE_PARENT, 0x40, 3, 0, },
	{ HI3620_TIMERCLK67,   "timerclk67",   "rclk_tcxo",    CLK_SET_RATE_PARENT, 0x40, 4, 0, },
	{ HI3620_TIMERCLK89,   "timerclk89",   "rclk_tcxo",    CLK_SET_RATE_PARENT, 0x40, 5, 0, },
	{ HI3620_PWMCLK0,      "pwmclk0",      "pwm0_mux",     CLK_SET_RATE_PARENT, 0x40, 7, 0, },
	{ HI3620_PWMCLK1,      "pwmclk1",      "pwm1_mux",     CLK_SET_RATE_PARENT, 0x40, 8, 0, },
	{ HI3620_UARTCLK0,     "uartclk0",     "uart0_mux",    CLK_SET_RATE_PARENT, 0x40, 16, 0, },
	{ HI3620_UARTCLK1,     "uartclk1",     "uart1_mux",    CLK_SET_RATE_PARENT, 0x40, 17, 0, },
	{ HI3620_UARTCLK2,     "uartclk2",     "uart2_mux",    CLK_SET_RATE_PARENT, 0x40, 18, 0, },
	{ HI3620_UARTCLK3,     "uartclk3",     "uart3_mux",    CLK_SET_RATE_PARENT, 0x40, 19, 0, },
	{ HI3620_UARTCLK4,     "uartclk4",     "uart4_mux",    CLK_SET_RATE_PARENT, 0x40, 20, 0, },
	{ HI3620_SPICLK0,      "spiclk0",      "spi0_mux",     CLK_SET_RATE_PARENT, 0x40, 21, 0, },
	{ HI3620_SPICLK1,      "spiclk1",      "spi1_mux",     CLK_SET_RATE_PARENT, 0x40, 22, 0, },
	{ HI3620_SPICLK2,      "spiclk2",      "spi2_mux",     CLK_SET_RATE_PARENT, 0x40, 23, 0, },
	{ HI3620_I2CCLK0,      "i2cclk0",      "pclk",         CLK_SET_RATE_PARENT, 0x40, 24, 0, },
	{ HI3620_I2CCLK1,      "i2cclk1",      "pclk",         CLK_SET_RATE_PARENT, 0x40, 25, 0, },
	{ HI3620_SCI_CLK,      "sci_clk",      "osc26m",       CLK_SET_RATE_PARENT, 0x40, 26, 0, },
	{ HI3620_I2CCLK2,      "i2cclk2",      "pclk",         CLK_SET_RATE_PARENT, 0x40, 28, 0, },
	{ HI3620_I2CCLK3,      "i2cclk3",      "pclk",         CLK_SET_RATE_PARENT, 0x40, 29, 0, },
	{ HI3620_DDRC_PER_CLK, "ddrc_per_clk", "rclk_cfgaxi",  CLK_SET_RATE_PARENT, 0x50, 9, 0, },
	{ HI3620_DMAC_CLK,     "dmac_clk",     "rclk_cfgaxi",  CLK_SET_RATE_PARENT, 0x50, 10, 0, },
	{ HI3620_USB2DVC_CLK,  "usb2dvc_clk",  "rclk_cfgaxi",  CLK_SET_RATE_PARENT, 0x50, 17, 0, },
	{ HI3620_SD_CLK,       "sd_clk",       "sd_div",       CLK_SET_RATE_PARENT, 0x50, 20, 0, },
	{ HI3620_MMC_CLK1,     "mmc_clk1",     "mmc1_mux2",    CLK_SET_RATE_PARENT, 0x50, 21, 0, },
	{ HI3620_MMC_CLK2,     "mmc_clk2",     "mmc2_div",     CLK_SET_RATE_PARENT, 0x50, 22, 0, },
	{ HI3620_MMC_CLK3,     "mmc_clk3",     "mmc3_div",     CLK_SET_RATE_PARENT, 0x50, 23, 0, },
	{ HI3620_MCU_CLK,      "mcu_clk",      "acp_clk",      CLK_SET_RATE_PARENT, 0x50, 24, 0, },
};

static void __init hi3620_clk_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;

	clk_data = hisi_clk_init(np, HI3620_NR_CLKS);
	if (!clk_data)
		return;

	hisi_clk_register_fixed_rate(hi3620_fixed_rate_clks,
				     ARRAY_SIZE(hi3620_fixed_rate_clks),
				     clk_data);
	hisi_clk_register_fixed_factor(hi3620_fixed_factor_clks,
				       ARRAY_SIZE(hi3620_fixed_factor_clks),
				       clk_data);
	hisi_clk_register_mux(hi3620_mux_clks, ARRAY_SIZE(hi3620_mux_clks),
			      clk_data);
	hisi_clk_register_divider(hi3620_div_clks, ARRAY_SIZE(hi3620_div_clks),
				  clk_data);
	hisi_clk_register_gate_sep(hi3620_seperated_gate_clks,
				   ARRAY_SIZE(hi3620_seperated_gate_clks),
				   clk_data);
}
CLK_OF_DECLARE(hi3620_clk, "hisilicon,hi3620-clock", hi3620_clk_init);

struct hisi_mmc_clock {
	unsigned int		id;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	u32			clken_reg;
	u32			clken_bit;
	u32			div_reg;
	u32			div_off;
	u32			div_bits;
	u32			drv_reg;
	u32			drv_off;
	u32			drv_bits;
	u32			sam_reg;
	u32			sam_off;
	u32			sam_bits;
};

struct clk_mmc {
	struct clk_hw	hw;
	u32		id;
	void __iomem	*clken_reg;
	u32		clken_bit;
	void __iomem	*div_reg;
	u32		div_off;
	u32		div_bits;
	void __iomem	*drv_reg;
	u32		drv_off;
	u32		drv_bits;
	void __iomem	*sam_reg;
	u32		sam_off;
	u32		sam_bits;
};

#define to_mmc(_hw) container_of(_hw, struct clk_mmc, hw)

static struct hisi_mmc_clock hi3620_mmc_clks[] __initdata = {
	{ HI3620_SD_CIUCLK,	"sd_bclk1", "sd_clk", CLK_SET_RATE_PARENT, 0x1f8, 0, 0x1f8, 1, 3, 0x1f8, 4, 4, 0x1f8, 8, 4},
	{ HI3620_MMC_CIUCLK1,   "mmc_bclk1", "mmc_clk1", CLK_SET_RATE_PARENT, 0x1f8, 12, 0x1f8, 13, 3, 0x1f8, 16, 4, 0x1f8, 20, 4},
	{ HI3620_MMC_CIUCLK2,   "mmc_bclk2", "mmc_clk2", CLK_SET_RATE_PARENT, 0x1f8, 24, 0x1f8, 25, 3, 0x1f8, 28, 4, 0x1fc, 0, 4},
	{ HI3620_MMC_CIUCLK3,   "mmc_bclk3", "mmc_clk3", CLK_SET_RATE_PARENT, 0x1fc, 4, 0x1fc, 5, 3, 0x1fc, 8, 4, 0x1fc, 12, 4},
};

static unsigned long mmc_clk_recalc_rate(struct clk_hw *hw,
		       unsigned long parent_rate)
{
	switch (parent_rate) {
	case 26000000:
		return 13000000;
	case 180000000:
		return 25000000;
	case 360000000:
		return 50000000;
	case 720000000:
		return 100000000;
	case 1440000000:
		return 180000000;
	default:
		return parent_rate;
	}
}

static long mmc_clk_determine_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long min_rate,
			      unsigned long max_rate,
			      unsigned long *best_parent_rate,
			      struct clk_hw **best_parent_p)
{
	struct clk_mmc *mclk = to_mmc(hw);
	unsigned long best = 0;

	if ((rate <= 13000000) && (mclk->id == HI3620_MMC_CIUCLK1)) {
		rate = 13000000;
		best = 26000000;
	} else if (rate <= 26000000) {
		rate = 25000000;
		best = 180000000;
	} else if (rate <= 52000000) {
		rate = 50000000;
		best = 360000000;
	} else if (rate <= 100000000) {
		rate = 100000000;
		best = 720000000;
	} else {
		/* max is 180M */
		rate = 180000000;
		best = 1440000000;
	}
	*best_parent_rate = best;
	return rate;
}

static u32 mmc_clk_delay(u32 val, u32 para, u32 off, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++) {
		if (para % 2)
			val |= 1 << (off + i);
		else
			val &= ~(1 << (off + i));
		para = para >> 1;
	}

	return val;
}

static int mmc_clk_set_timing(struct clk_hw *hw, unsigned long rate)
{
	struct clk_mmc *mclk = to_mmc(hw);
	unsigned long flags;
	u32 sam, drv, div, val;
	static DEFINE_SPINLOCK(mmc_clk_lock);

	switch (rate) {
	case 13000000:
		sam = 3;
		drv = 1;
		div = 1;
		break;
	case 25000000:
		sam = 13;
		drv = 6;
		div = 6;
		break;
	case 50000000:
		sam = 3;
		drv = 6;
		div = 6;
		break;
	case 100000000:
		sam = 6;
		drv = 4;
		div = 6;
		break;
	case 180000000:
		sam = 6;
		drv = 4;
		div = 7;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&mmc_clk_lock, flags);

	val = readl_relaxed(mclk->clken_reg);
	val &= ~(1 << mclk->clken_bit);
	writel_relaxed(val, mclk->clken_reg);

	val = readl_relaxed(mclk->sam_reg);
	val = mmc_clk_delay(val, sam, mclk->sam_off, mclk->sam_bits);
	writel_relaxed(val, mclk->sam_reg);

	val = readl_relaxed(mclk->drv_reg);
	val = mmc_clk_delay(val, drv, mclk->drv_off, mclk->drv_bits);
	writel_relaxed(val, mclk->drv_reg);

	val = readl_relaxed(mclk->div_reg);
	val = mmc_clk_delay(val, div, mclk->div_off, mclk->div_bits);
	writel_relaxed(val, mclk->div_reg);

	val = readl_relaxed(mclk->clken_reg);
	val |= 1 << mclk->clken_bit;
	writel_relaxed(val, mclk->clken_reg);

	spin_unlock_irqrestore(&mmc_clk_lock, flags);

	return 0;
}

static int mmc_clk_prepare(struct clk_hw *hw)
{
	struct clk_mmc *mclk = to_mmc(hw);
	unsigned long rate;

	if (mclk->id == HI3620_MMC_CIUCLK1)
		rate = 13000000;
	else
		rate = 25000000;

	return mmc_clk_set_timing(hw, rate);
}

static int mmc_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	return mmc_clk_set_timing(hw, rate);
}

static struct clk_ops clk_mmc_ops = {
	.prepare = mmc_clk_prepare,
	.determine_rate = mmc_clk_determine_rate,
	.set_rate = mmc_clk_set_rate,
	.recalc_rate = mmc_clk_recalc_rate,
};

static struct clk *hisi_register_clk_mmc(struct hisi_mmc_clock *mmc_clk,
			void __iomem *base, struct device_node *np)
{
	struct clk_mmc *mclk;
	struct clk *clk;
	struct clk_init_data init;

	mclk = kzalloc(sizeof(*mclk), GFP_KERNEL);
	if (!mclk) {
		pr_err("%s: fail to allocate mmc clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = mmc_clk->name;
	init.ops = &clk_mmc_ops;
	init.flags = mmc_clk->flags | CLK_IS_BASIC;
	init.parent_names = (mmc_clk->parent_name ? &mmc_clk->parent_name : NULL);
	init.num_parents = (mmc_clk->parent_name ? 1 : 0);
	mclk->hw.init = &init;

	mclk->id = mmc_clk->id;
	mclk->clken_reg = base + mmc_clk->clken_reg;
	mclk->clken_bit = mmc_clk->clken_bit;
	mclk->div_reg = base + mmc_clk->div_reg;
	mclk->div_off = mmc_clk->div_off;
	mclk->div_bits = mmc_clk->div_bits;
	mclk->drv_reg = base + mmc_clk->drv_reg;
	mclk->drv_off = mmc_clk->drv_off;
	mclk->drv_bits = mmc_clk->drv_bits;
	mclk->sam_reg = base + mmc_clk->sam_reg;
	mclk->sam_off = mmc_clk->sam_off;
	mclk->sam_bits = mmc_clk->sam_bits;

	clk = clk_register(NULL, &mclk->hw);
	if (WARN_ON(IS_ERR(clk)))
		kfree(mclk);
	return clk;
}

static void __init hi3620_mmc_clk_init(struct device_node *node)
{
	void __iomem *base;
	int i, num = ARRAY_SIZE(hi3620_mmc_clks);
	struct clk_onecell_data *clk_data;

	if (!node) {
		pr_err("failed to find pctrl node in DTS\n");
		return;
	}

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("failed to map pctrl\n");
		return;
	}

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (WARN_ON(!clk_data))
		return;

	clk_data->clks = kzalloc(sizeof(struct clk *) * num, GFP_KERNEL);
	if (!clk_data->clks) {
		pr_err("%s: fail to allocate mmc clk\n", __func__);
		return;
	}

	for (i = 0; i < num; i++) {
		struct hisi_mmc_clock *mmc_clk = &hi3620_mmc_clks[i];
		clk_data->clks[mmc_clk->id] =
			hisi_register_clk_mmc(mmc_clk, base, node);
	}

	clk_data->clk_num = num;
	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
}

CLK_OF_DECLARE(hi3620_mmc_clk, "hisilicon,hi3620-mmc-clock", hi3620_mmc_clk_init);
