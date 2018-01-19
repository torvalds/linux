// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk/mmp.h>

#include "addr-map.h"

#include "common.h"
#include "clock.h"

/*
 * APB clock register offsets for PXA168
 */
#define APBC_UART1	APBC_REG(0x000)
#define APBC_UART2	APBC_REG(0x004)
#define APBC_GPIO	APBC_REG(0x008)
#define APBC_PWM1	APBC_REG(0x00c)
#define APBC_PWM2	APBC_REG(0x010)
#define APBC_PWM3	APBC_REG(0x014)
#define APBC_PWM4	APBC_REG(0x018)
#define APBC_RTC	APBC_REG(0x028)
#define APBC_TWSI0	APBC_REG(0x02c)
#define APBC_KPC	APBC_REG(0x030)
#define APBC_TWSI1	APBC_REG(0x06c)
#define APBC_UART3	APBC_REG(0x070)
#define APBC_SSP1	APBC_REG(0x81c)
#define APBC_SSP2	APBC_REG(0x820)
#define APBC_SSP3	APBC_REG(0x84c)
#define APBC_SSP4	APBC_REG(0x858)
#define APBC_SSP5	APBC_REG(0x85c)

#define APMU_NAND	APMU_REG(0x060)
#define APMU_LCD	APMU_REG(0x04c)
#define APMU_ETH	APMU_REG(0x0fc)
#define APMU_USB	APMU_REG(0x05c)

/* APB peripheral clocks */
static APBC_CLK(uart1, UART1, 1, 14745600);
static APBC_CLK(uart2, UART2, 1, 14745600);
static APBC_CLK(uart3, UART3, 1, 14745600);
static APBC_CLK(twsi0, TWSI0, 1, 33000000);
static APBC_CLK(twsi1, TWSI1, 1, 33000000);
static APBC_CLK(pwm1, PWM1, 1, 13000000);
static APBC_CLK(pwm2, PWM2, 1, 13000000);
static APBC_CLK(pwm3, PWM3, 1, 13000000);
static APBC_CLK(pwm4, PWM4, 1, 13000000);
static APBC_CLK(ssp1, SSP1, 4, 0);
static APBC_CLK(ssp2, SSP2, 4, 0);
static APBC_CLK(ssp3, SSP3, 4, 0);
static APBC_CLK(ssp4, SSP4, 4, 0);
static APBC_CLK(ssp5, SSP5, 4, 0);
static APBC_CLK(gpio, GPIO, 0, 13000000);
static APBC_CLK(keypad, KPC, 0, 32000);
static APBC_CLK(rtc, RTC, 8, 32768);

static APMU_CLK(nand, NAND, 0x19b, 156000000);
static APMU_CLK(lcd, LCD, 0x7f, 312000000);
static APMU_CLK(eth, ETH, 0x09, 0);
static APMU_CLK(usb, USB, 0x12, 0);

/* device and clock bindings */
static struct clk_lookup pxa168_clkregs[] = {
	INIT_CLKREG(&clk_uart1, "pxa2xx-uart.0", NULL),
	INIT_CLKREG(&clk_uart2, "pxa2xx-uart.1", NULL),
	INIT_CLKREG(&clk_uart3, "pxa2xx-uart.2", NULL),
	INIT_CLKREG(&clk_twsi0, "pxa2xx-i2c.0", NULL),
	INIT_CLKREG(&clk_twsi1, "pxa2xx-i2c.1", NULL),
	INIT_CLKREG(&clk_pwm1, "pxa168-pwm.0", NULL),
	INIT_CLKREG(&clk_pwm2, "pxa168-pwm.1", NULL),
	INIT_CLKREG(&clk_pwm3, "pxa168-pwm.2", NULL),
	INIT_CLKREG(&clk_pwm4, "pxa168-pwm.3", NULL),
	INIT_CLKREG(&clk_ssp1, "pxa168-ssp.0", NULL),
	INIT_CLKREG(&clk_ssp2, "pxa168-ssp.1", NULL),
	INIT_CLKREG(&clk_ssp3, "pxa168-ssp.2", NULL),
	INIT_CLKREG(&clk_ssp4, "pxa168-ssp.3", NULL),
	INIT_CLKREG(&clk_ssp5, "pxa168-ssp.4", NULL),
	INIT_CLKREG(&clk_nand, "pxa3xx-nand", NULL),
	INIT_CLKREG(&clk_lcd, "pxa168-fb", NULL),
	INIT_CLKREG(&clk_gpio, "mmp-gpio", NULL),
	INIT_CLKREG(&clk_keypad, "pxa27x-keypad", NULL),
	INIT_CLKREG(&clk_eth, "pxa168-eth", "MFUCLK"),
	INIT_CLKREG(&clk_usb, NULL, "PXA168-USBCLK"),
	INIT_CLKREG(&clk_rtc, "sa1100-rtc", NULL),
};

void __init pxa168_clk_init(phys_addr_t mpmu_phys, phys_addr_t apmu_phys,
			    phys_addr_t apbc_phys)
{
	clkdev_add_table(ARRAY_AND_SIZE(pxa168_clkregs));
}
