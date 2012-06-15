/*
 *  linux/arch/arm/mach-mmp/pxa168.c
 *
 *  Code specific to PXA168
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/platform_data/mv_usb.h>

#include <asm/mach/time.h>
#include <asm/system_misc.h>
#include <mach/addr-map.h>
#include <mach/cputype.h>
#include <mach/regs-apbc.h>
#include <mach/regs-apmu.h>
#include <mach/irqs.h>
#include <mach/dma.h>
#include <mach/devices.h>
#include <mach/mfp.h>
#include <linux/dma-mapping.h>
#include <mach/pxa168.h>
#include <mach/regs-usb.h>

#include "common.h"
#include "clock.h"

#define MFPR_VIRT_BASE	(APB_VIRT_BASE + 0x1e000)

static struct mfp_addr_map pxa168_mfp_addr_map[] __initdata =
{
	MFP_ADDR_X(GPIO0,   GPIO36,  0x04c),
	MFP_ADDR_X(GPIO37,  GPIO55,  0x000),
	MFP_ADDR_X(GPIO56,  GPIO123, 0x0e0),
	MFP_ADDR_X(GPIO124, GPIO127, 0x0f4),

	MFP_ADDR_END,
};

void __init pxa168_init_irq(void)
{
	icu_init_irq();
}

/* APB peripheral clocks */
static APBC_CLK(uart1, PXA168_UART1, 1, 14745600);
static APBC_CLK(uart2, PXA168_UART2, 1, 14745600);
static APBC_CLK(uart3, PXA168_UART3, 1, 14745600);
static APBC_CLK(twsi0, PXA168_TWSI0, 1, 33000000);
static APBC_CLK(twsi1, PXA168_TWSI1, 1, 33000000);
static APBC_CLK(pwm1, PXA168_PWM1, 1, 13000000);
static APBC_CLK(pwm2, PXA168_PWM2, 1, 13000000);
static APBC_CLK(pwm3, PXA168_PWM3, 1, 13000000);
static APBC_CLK(pwm4, PXA168_PWM4, 1, 13000000);
static APBC_CLK(ssp1, PXA168_SSP1, 4, 0);
static APBC_CLK(ssp2, PXA168_SSP2, 4, 0);
static APBC_CLK(ssp3, PXA168_SSP3, 4, 0);
static APBC_CLK(ssp4, PXA168_SSP4, 4, 0);
static APBC_CLK(ssp5, PXA168_SSP5, 4, 0);
static APBC_CLK(gpio, PXA168_GPIO, 0, 13000000);
static APBC_CLK(keypad, PXA168_KPC, 0, 32000);
static APBC_CLK(rtc, PXA168_RTC, 8, 32768);

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
	INIT_CLKREG(&clk_gpio, "pxa-gpio", NULL),
	INIT_CLKREG(&clk_keypad, "pxa27x-keypad", NULL),
	INIT_CLKREG(&clk_eth, "pxa168-eth", "MFUCLK"),
	INIT_CLKREG(&clk_usb, NULL, "PXA168-USBCLK"),
	INIT_CLKREG(&clk_rtc, "sa1100-rtc", NULL),
};

static int __init pxa168_init(void)
{
	if (cpu_is_pxa168()) {
		mfp_init_base(MFPR_VIRT_BASE);
		mfp_init_addr(pxa168_mfp_addr_map);
		pxa_init_dma(IRQ_PXA168_DMA_INT0, 32);
		clkdev_add_table(ARRAY_AND_SIZE(pxa168_clkregs));
	}

	return 0;
}
postcore_initcall(pxa168_init);

/* system timer - clock enabled, 3.25MHz */
#define TIMER_CLK_RST	(APBC_APBCLK | APBC_FNCLK | APBC_FNCLKSEL(3))

static void __init pxa168_timer_init(void)
{
	/* this is early, we have to initialize the CCU registers by
	 * ourselves instead of using clk_* API. Clock rate is defined
	 * by APBC_TIMERS_CLK_RST (3.25MHz) and enabled free-running
	 */
	__raw_writel(APBC_APBCLK | APBC_RST, APBC_PXA168_TIMERS);

	/* 3.25MHz, bus/functional clock enabled, release reset */
	__raw_writel(TIMER_CLK_RST, APBC_PXA168_TIMERS);

	timer_init(IRQ_PXA168_TIMER1);
}

struct sys_timer pxa168_timer = {
	.init	= pxa168_timer_init,
};

void pxa168_clear_keypad_wakeup(void)
{
	uint32_t val;
	uint32_t mask = APMU_PXA168_KP_WAKE_CLR;

	/* wake event clear is needed in order to clear keypad interrupt */
	val = __raw_readl(APMU_WAKE_CLR);
	__raw_writel(val |  mask, APMU_WAKE_CLR);
}

/* on-chip devices */
PXA168_DEVICE(uart1, "pxa2xx-uart", 0, UART1, 0xd4017000, 0x30, 21, 22);
PXA168_DEVICE(uart2, "pxa2xx-uart", 1, UART2, 0xd4018000, 0x30, 23, 24);
PXA168_DEVICE(uart3, "pxa2xx-uart", 2, UART3, 0xd4026000, 0x30, 23, 24);
PXA168_DEVICE(twsi0, "pxa2xx-i2c", 0, TWSI0, 0xd4011000, 0x28);
PXA168_DEVICE(twsi1, "pxa2xx-i2c", 1, TWSI1, 0xd4025000, 0x28);
PXA168_DEVICE(pwm1, "pxa168-pwm", 0, NONE, 0xd401a000, 0x10);
PXA168_DEVICE(pwm2, "pxa168-pwm", 1, NONE, 0xd401a400, 0x10);
PXA168_DEVICE(pwm3, "pxa168-pwm", 2, NONE, 0xd401a800, 0x10);
PXA168_DEVICE(pwm4, "pxa168-pwm", 3, NONE, 0xd401ac00, 0x10);
PXA168_DEVICE(nand, "pxa3xx-nand", -1, NAND, 0xd4283000, 0x80, 97, 99);
PXA168_DEVICE(ssp1, "pxa168-ssp", 0, SSP1, 0xd401b000, 0x40, 52, 53);
PXA168_DEVICE(ssp2, "pxa168-ssp", 1, SSP2, 0xd401c000, 0x40, 54, 55);
PXA168_DEVICE(ssp3, "pxa168-ssp", 2, SSP3, 0xd401f000, 0x40, 56, 57);
PXA168_DEVICE(ssp4, "pxa168-ssp", 3, SSP4, 0xd4020000, 0x40, 58, 59);
PXA168_DEVICE(ssp5, "pxa168-ssp", 4, SSP5, 0xd4021000, 0x40, 60, 61);
PXA168_DEVICE(fb, "pxa168-fb", -1, LCD, 0xd420b000, 0x1c8);
PXA168_DEVICE(keypad, "pxa27x-keypad", -1, KEYPAD, 0xd4012000, 0x4c);
PXA168_DEVICE(eth, "pxa168-eth", -1, MFU, 0xc0800000, 0x0fff);

struct resource pxa168_resource_gpio[] = {
	{
		.start	= 0xd4019000,
		.end	= 0xd4019fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_PXA168_GPIOX,
		.end	= IRQ_PXA168_GPIOX,
		.name	= "gpio_mux",
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device pxa168_device_gpio = {
	.name		= "pxa-gpio",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pxa168_resource_gpio),
	.resource	= pxa168_resource_gpio,
};

struct resource pxa168_usb_host_resources[] = {
	/* USB Host conroller register base */
	[0] = {
		.start	= PXA168_U2H_REGBASE + U2x_CAPREGS_OFFSET,
		.end	= PXA168_U2H_REGBASE + USB_REG_RANGE,
		.flags	= IORESOURCE_MEM,
		.name	= "capregs",
	},
	/* USB PHY register base */
	[1] = {
		.start	= PXA168_U2H_PHYBASE,
		.end	= PXA168_U2H_PHYBASE + USB_PHY_RANGE,
		.flags	= IORESOURCE_MEM,
		.name	= "phyregs",
	},
	[2] = {
		.start	= IRQ_PXA168_USB2,
		.end	= IRQ_PXA168_USB2,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 pxa168_usb_host_dmamask = DMA_BIT_MASK(32);
struct platform_device pxa168_device_usb_host = {
	.name = "pxa-sph",
	.id   = -1,
	.dev  = {
		.dma_mask = &pxa168_usb_host_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},

	.num_resources = ARRAY_SIZE(pxa168_usb_host_resources),
	.resource      = pxa168_usb_host_resources,
};

int __init pxa168_add_usb_host(struct mv_usb_platform_data *pdata)
{
	pxa168_device_usb_host.dev.platform_data = pdata;
	return platform_device_register(&pxa168_device_usb_host);
}

void pxa168_restart(char mode, const char *cmd)
{
	soft_restart(0xffff0000);
}
