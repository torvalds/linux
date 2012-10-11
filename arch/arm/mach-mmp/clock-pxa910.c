#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/clk.h>

#include <mach/addr-map.h>

#include "common.h"
#include "clock.h"

/*
 * APB Clock register offsets for PXA910
 */
#define APBC_UART0	APBC_REG(0x000)
#define APBC_UART1	APBC_REG(0x004)
#define APBC_GPIO	APBC_REG(0x008)
#define APBC_PWM1	APBC_REG(0x00c)
#define APBC_PWM2	APBC_REG(0x010)
#define APBC_PWM3	APBC_REG(0x014)
#define APBC_PWM4	APBC_REG(0x018)
#define APBC_SSP1	APBC_REG(0x01c)
#define APBC_SSP2	APBC_REG(0x020)
#define APBC_RTC	APBC_REG(0x028)
#define APBC_TWSI0	APBC_REG(0x02c)
#define APBC_KPC	APBC_REG(0x030)
#define APBC_SSP3	APBC_REG(0x04c)
#define APBC_TWSI1	APBC_REG(0x06c)

#define APMU_NAND	APMU_REG(0x060)
#define APMU_USB	APMU_REG(0x05c)

static APBC_CLK(uart1, UART0, 1, 14745600);
static APBC_CLK(uart2, UART1, 1, 14745600);
static APBC_CLK(twsi0, TWSI0, 1, 33000000);
static APBC_CLK(twsi1, TWSI1, 1, 33000000);
static APBC_CLK(pwm1, PWM1, 1, 13000000);
static APBC_CLK(pwm2, PWM2, 1, 13000000);
static APBC_CLK(pwm3, PWM3, 1, 13000000);
static APBC_CLK(pwm4, PWM4, 1, 13000000);
static APBC_CLK(gpio, GPIO, 0, 13000000);
static APBC_CLK(rtc, RTC, 8, 32768);

static APMU_CLK(nand, NAND, 0x19b, 156000000);
static APMU_CLK(u2o, USB, 0x1b, 480000000);

/* device and clock bindings */
static struct clk_lookup pxa910_clkregs[] = {
	INIT_CLKREG(&clk_uart1, "pxa2xx-uart.0", NULL),
	INIT_CLKREG(&clk_uart2, "pxa2xx-uart.1", NULL),
	INIT_CLKREG(&clk_twsi0, "pxa2xx-i2c.0", NULL),
	INIT_CLKREG(&clk_twsi1, "pxa2xx-i2c.1", NULL),
	INIT_CLKREG(&clk_pwm1, "pxa910-pwm.0", NULL),
	INIT_CLKREG(&clk_pwm2, "pxa910-pwm.1", NULL),
	INIT_CLKREG(&clk_pwm3, "pxa910-pwm.2", NULL),
	INIT_CLKREG(&clk_pwm4, "pxa910-pwm.3", NULL),
	INIT_CLKREG(&clk_nand, "pxa3xx-nand", NULL),
	INIT_CLKREG(&clk_gpio, "pxa-gpio", NULL),
	INIT_CLKREG(&clk_u2o, NULL, "U2OCLK"),
	INIT_CLKREG(&clk_rtc, "sa1100-rtc", NULL),
};

void __init pxa910_clk_init(void)
{
	clkdev_add_table(ARRAY_AND_SIZE(pxa910_clkregs));
}
