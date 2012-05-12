/*
 * arch/arm/mach-ep93xx/core.c
 * Core routines for Cirrus EP93xx chips.
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 * Copyright (C) 2007 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * Thanks go to Michael Burian and Ray Lehtiniemi for their key
 * role in the ep93xx linux community.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#define pr_fmt(fmt) "ep93xx " KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/timex.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/termios.h>
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>
#include <linux/mtd/physmap.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/spi/spi.h>
#include <linux/export.h>

#include <mach/hardware.h>
#include <mach/fb.h>
#include <mach/ep93xx_keypad.h>
#include <mach/ep93xx_spi.h>
#include <mach/gpio-ep93xx.h>

#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <asm/hardware/vic.h>

#include "soc.h"

/*************************************************************************
 * Static I/O mappings that are needed for all EP93xx platforms
 *************************************************************************/
static struct map_desc ep93xx_io_desc[] __initdata = {
	{
		.virtual	= EP93XX_AHB_VIRT_BASE,
		.pfn		= __phys_to_pfn(EP93XX_AHB_PHYS_BASE),
		.length		= EP93XX_AHB_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= EP93XX_APB_VIRT_BASE,
		.pfn		= __phys_to_pfn(EP93XX_APB_PHYS_BASE),
		.length		= EP93XX_APB_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init ep93xx_map_io(void)
{
	iotable_init(ep93xx_io_desc, ARRAY_SIZE(ep93xx_io_desc));
}


/*************************************************************************
 * Timer handling for EP93xx
 *************************************************************************
 * The ep93xx has four internal timers.  Timers 1, 2 (both 16 bit) and
 * 3 (32 bit) count down at 508 kHz, are self-reloading, and can generate
 * an interrupt on underflow.  Timer 4 (40 bit) counts down at 983.04 kHz,
 * is free-running, and can't generate interrupts.
 *
 * The 508 kHz timers are ideal for use for the timer interrupt, as the
 * most common values of HZ divide 508 kHz nicely.  We pick one of the 16
 * bit timers (timer 1) since we don't need more than 16 bits of reload
 * value as long as HZ >= 8.
 *
 * The higher clock rate of timer 4 makes it a better choice than the
 * other timers for use in gettimeoffset(), while the fact that it can't
 * generate interrupts means we don't have to worry about not being able
 * to use this timer for something else.  We also use timer 4 for keeping
 * track of lost jiffies.
 */
#define EP93XX_TIMER_REG(x)		(EP93XX_TIMER_BASE + (x))
#define EP93XX_TIMER1_LOAD		EP93XX_TIMER_REG(0x00)
#define EP93XX_TIMER1_VALUE		EP93XX_TIMER_REG(0x04)
#define EP93XX_TIMER1_CONTROL		EP93XX_TIMER_REG(0x08)
#define EP93XX_TIMER123_CONTROL_ENABLE	(1 << 7)
#define EP93XX_TIMER123_CONTROL_MODE	(1 << 6)
#define EP93XX_TIMER123_CONTROL_CLKSEL	(1 << 3)
#define EP93XX_TIMER1_CLEAR		EP93XX_TIMER_REG(0x0c)
#define EP93XX_TIMER2_LOAD		EP93XX_TIMER_REG(0x20)
#define EP93XX_TIMER2_VALUE		EP93XX_TIMER_REG(0x24)
#define EP93XX_TIMER2_CONTROL		EP93XX_TIMER_REG(0x28)
#define EP93XX_TIMER2_CLEAR		EP93XX_TIMER_REG(0x2c)
#define EP93XX_TIMER4_VALUE_LOW		EP93XX_TIMER_REG(0x60)
#define EP93XX_TIMER4_VALUE_HIGH	EP93XX_TIMER_REG(0x64)
#define EP93XX_TIMER4_VALUE_HIGH_ENABLE	(1 << 8)
#define EP93XX_TIMER3_LOAD		EP93XX_TIMER_REG(0x80)
#define EP93XX_TIMER3_VALUE		EP93XX_TIMER_REG(0x84)
#define EP93XX_TIMER3_CONTROL		EP93XX_TIMER_REG(0x88)
#define EP93XX_TIMER3_CLEAR		EP93XX_TIMER_REG(0x8c)

#define EP93XX_TIMER123_CLOCK		508469
#define EP93XX_TIMER4_CLOCK		983040

#define TIMER1_RELOAD			((EP93XX_TIMER123_CLOCK / HZ) - 1)
#define TIMER4_TICKS_PER_JIFFY		DIV_ROUND_CLOSEST(CLOCK_TICK_RATE, HZ)

static unsigned int last_jiffy_time;

static irqreturn_t ep93xx_timer_interrupt(int irq, void *dev_id)
{
	/* Writing any value clears the timer interrupt */
	__raw_writel(1, EP93XX_TIMER1_CLEAR);

	/* Recover lost jiffies */
	while ((signed long)
		(__raw_readl(EP93XX_TIMER4_VALUE_LOW) - last_jiffy_time)
						>= TIMER4_TICKS_PER_JIFFY) {
		last_jiffy_time += TIMER4_TICKS_PER_JIFFY;
		timer_tick();
	}

	return IRQ_HANDLED;
}

static struct irqaction ep93xx_timer_irq = {
	.name		= "ep93xx timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= ep93xx_timer_interrupt,
};

static void __init ep93xx_timer_init(void)
{
	u32 tmode = EP93XX_TIMER123_CONTROL_MODE |
		    EP93XX_TIMER123_CONTROL_CLKSEL;

	/* Enable periodic HZ timer.  */
	__raw_writel(tmode, EP93XX_TIMER1_CONTROL);
	__raw_writel(TIMER1_RELOAD, EP93XX_TIMER1_LOAD);
	__raw_writel(tmode | EP93XX_TIMER123_CONTROL_ENABLE,
			EP93XX_TIMER1_CONTROL);

	/* Enable lost jiffy timer.  */
	__raw_writel(EP93XX_TIMER4_VALUE_HIGH_ENABLE,
			EP93XX_TIMER4_VALUE_HIGH);

	setup_irq(IRQ_EP93XX_TIMER1, &ep93xx_timer_irq);
}

static unsigned long ep93xx_gettimeoffset(void)
{
	int offset;

	offset = __raw_readl(EP93XX_TIMER4_VALUE_LOW) - last_jiffy_time;

	/* Calculate (1000000 / 983040) * offset.  */
	return offset + (53 * offset / 3072);
}

struct sys_timer ep93xx_timer = {
	.init		= ep93xx_timer_init,
	.offset		= ep93xx_gettimeoffset,
};


/*************************************************************************
 * EP93xx IRQ handling
 *************************************************************************/
void __init ep93xx_init_irq(void)
{
	vic_init(EP93XX_VIC1_BASE, 0, EP93XX_VIC1_VALID_IRQ_MASK, 0);
	vic_init(EP93XX_VIC2_BASE, 32, EP93XX_VIC2_VALID_IRQ_MASK, 0);
}


/*************************************************************************
 * EP93xx System Controller Software Locked register handling
 *************************************************************************/

/*
 * syscon_swlock prevents anything else from writing to the syscon
 * block while a software locked register is being written.
 */
static DEFINE_SPINLOCK(syscon_swlock);

void ep93xx_syscon_swlocked_write(unsigned int val, void __iomem *reg)
{
	unsigned long flags;

	spin_lock_irqsave(&syscon_swlock, flags);

	__raw_writel(0xaa, EP93XX_SYSCON_SWLOCK);
	__raw_writel(val, reg);

	spin_unlock_irqrestore(&syscon_swlock, flags);
}

void ep93xx_devcfg_set_clear(unsigned int set_bits, unsigned int clear_bits)
{
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&syscon_swlock, flags);

	val = __raw_readl(EP93XX_SYSCON_DEVCFG);
	val &= ~clear_bits;
	val |= set_bits;
	__raw_writel(0xaa, EP93XX_SYSCON_SWLOCK);
	__raw_writel(val, EP93XX_SYSCON_DEVCFG);

	spin_unlock_irqrestore(&syscon_swlock, flags);
}

/**
 * ep93xx_chip_revision() - returns the EP93xx chip revision
 *
 * See <mach/platform.h> for more information.
 */
unsigned int ep93xx_chip_revision(void)
{
	unsigned int v;

	v = __raw_readl(EP93XX_SYSCON_SYSCFG);
	v &= EP93XX_SYSCON_SYSCFG_REV_MASK;
	v >>= EP93XX_SYSCON_SYSCFG_REV_SHIFT;
	return v;
}

/*************************************************************************
 * EP93xx GPIO
 *************************************************************************/
static struct resource ep93xx_gpio_resource[] = {
	{
		.start		= EP93XX_GPIO_PHYS_BASE,
		.end		= EP93XX_GPIO_PHYS_BASE + 0xcc - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device ep93xx_gpio_device = {
	.name		= "gpio-ep93xx",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_gpio_resource),
	.resource	= ep93xx_gpio_resource,
};

/*************************************************************************
 * EP93xx peripheral handling
 *************************************************************************/
#define EP93XX_UART_MCR_OFFSET		(0x0100)

static void ep93xx_uart_set_mctrl(struct amba_device *dev,
				  void __iomem *base, unsigned int mctrl)
{
	unsigned int mcr;

	mcr = 0;
	if (mctrl & TIOCM_RTS)
		mcr |= 2;
	if (mctrl & TIOCM_DTR)
		mcr |= 1;

	__raw_writel(mcr, base + EP93XX_UART_MCR_OFFSET);
}

static struct amba_pl010_data ep93xx_uart_data = {
	.set_mctrl	= ep93xx_uart_set_mctrl,
};

static AMBA_APB_DEVICE(uart1, "apb:uart1", 0x00041010, EP93XX_UART1_PHYS_BASE,
	{ IRQ_EP93XX_UART1 }, &ep93xx_uart_data);

static AMBA_APB_DEVICE(uart2, "apb:uart2", 0x00041010, EP93XX_UART2_PHYS_BASE,
	{ IRQ_EP93XX_UART2 }, &ep93xx_uart_data);

static AMBA_APB_DEVICE(uart3, "apb:uart3", 0x00041010, EP93XX_UART3_PHYS_BASE,
	{ IRQ_EP93XX_UART3 }, &ep93xx_uart_data);

static struct resource ep93xx_rtc_resource[] = {
	{
		.start		= EP93XX_RTC_PHYS_BASE,
		.end		= EP93XX_RTC_PHYS_BASE + 0x10c - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device ep93xx_rtc_device = {
	.name		= "ep93xx-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_rtc_resource),
	.resource	= ep93xx_rtc_resource,
};


static struct resource ep93xx_ohci_resources[] = {
	[0] = {
		.start	= EP93XX_USB_PHYS_BASE,
		.end	= EP93XX_USB_PHYS_BASE + 0x0fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_EP93XX_USB,
		.end	= IRQ_EP93XX_USB,
		.flags	= IORESOURCE_IRQ,
	},
};


static struct platform_device ep93xx_ohci_device = {
	.name		= "ep93xx-ohci",
	.id		= -1,
	.dev		= {
		.dma_mask		= &ep93xx_ohci_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(ep93xx_ohci_resources),
	.resource	= ep93xx_ohci_resources,
};


/*************************************************************************
 * EP93xx physmap'ed flash
 *************************************************************************/
static struct physmap_flash_data ep93xx_flash_data;

static struct resource ep93xx_flash_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ep93xx_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &ep93xx_flash_data,
	},
	.num_resources	= 1,
	.resource	= &ep93xx_flash_resource,
};

/**
 * ep93xx_register_flash() - Register the external flash device.
 * @width:	bank width in octets
 * @start:	resource start address
 * @size:	resource size
 */
void __init ep93xx_register_flash(unsigned int width,
				  resource_size_t start, resource_size_t size)
{
	ep93xx_flash_data.width		= width;

	ep93xx_flash_resource.start	= start;
	ep93xx_flash_resource.end	= start + size - 1;

	platform_device_register(&ep93xx_flash);
}


/*************************************************************************
 * EP93xx ethernet peripheral handling
 *************************************************************************/
static struct ep93xx_eth_data ep93xx_eth_data;

static struct resource ep93xx_eth_resource[] = {
	{
		.start	= EP93XX_ETHERNET_PHYS_BASE,
		.end	= EP93XX_ETHERNET_PHYS_BASE + 0xffff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_EP93XX_ETHERNET,
		.end	= IRQ_EP93XX_ETHERNET,
		.flags	= IORESOURCE_IRQ,
	}
};

static u64 ep93xx_eth_dma_mask = DMA_BIT_MASK(32);

static struct platform_device ep93xx_eth_device = {
	.name		= "ep93xx-eth",
	.id		= -1,
	.dev		= {
		.platform_data		= &ep93xx_eth_data,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.dma_mask		= &ep93xx_eth_dma_mask,
	},
	.num_resources	= ARRAY_SIZE(ep93xx_eth_resource),
	.resource	= ep93xx_eth_resource,
};

/**
 * ep93xx_register_eth - Register the built-in ethernet platform device.
 * @data:	platform specific ethernet configuration (__initdata)
 * @copy_addr:	flag indicating that the MAC address should be copied
 *		from the IndAd registers (as programmed by the bootloader)
 */
void __init ep93xx_register_eth(struct ep93xx_eth_data *data, int copy_addr)
{
	if (copy_addr)
		memcpy_fromio(data->dev_addr, EP93XX_ETHERNET_BASE + 0x50, 6);

	ep93xx_eth_data = *data;
	platform_device_register(&ep93xx_eth_device);
}


/*************************************************************************
 * EP93xx i2c peripheral handling
 *************************************************************************/
static struct i2c_gpio_platform_data ep93xx_i2c_data;

static struct platform_device ep93xx_i2c_device = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev		= {
		.platform_data	= &ep93xx_i2c_data,
	},
};

/**
 * ep93xx_register_i2c - Register the i2c platform device.
 * @data:	platform specific i2c-gpio configuration (__initdata)
 * @devices:	platform specific i2c bus device information (__initdata)
 * @num:	the number of devices on the i2c bus
 */
void __init ep93xx_register_i2c(struct i2c_gpio_platform_data *data,
				struct i2c_board_info *devices, int num)
{
	/*
	 * Set the EEPROM interface pin drive type control.
	 * Defines the driver type for the EECLK and EEDAT pins as either
	 * open drain, which will require an external pull-up, or a normal
	 * CMOS driver.
	 */
	if (data->sda_is_open_drain && data->sda_pin != EP93XX_GPIO_LINE_EEDAT)
		pr_warning("sda != EEDAT, open drain has no effect\n");
	if (data->scl_is_open_drain && data->scl_pin != EP93XX_GPIO_LINE_EECLK)
		pr_warning("scl != EECLK, open drain has no effect\n");

	__raw_writel((data->sda_is_open_drain << 1) |
		     (data->scl_is_open_drain << 0),
		     EP93XX_GPIO_EEDRIVE);

	ep93xx_i2c_data = *data;
	i2c_register_board_info(0, devices, num);
	platform_device_register(&ep93xx_i2c_device);
}

/*************************************************************************
 * EP93xx SPI peripheral handling
 *************************************************************************/
static struct ep93xx_spi_info ep93xx_spi_master_data;

static struct resource ep93xx_spi_resources[] = {
	{
		.start	= EP93XX_SPI_PHYS_BASE,
		.end	= EP93XX_SPI_PHYS_BASE + 0x18 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_EP93XX_SSP,
		.end	= IRQ_EP93XX_SSP,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 ep93xx_spi_dma_mask = DMA_BIT_MASK(32);

static struct platform_device ep93xx_spi_device = {
	.name		= "ep93xx-spi",
	.id		= 0,
	.dev		= {
		.platform_data		= &ep93xx_spi_master_data,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.dma_mask		= &ep93xx_spi_dma_mask,
	},
	.num_resources	= ARRAY_SIZE(ep93xx_spi_resources),
	.resource	= ep93xx_spi_resources,
};

/**
 * ep93xx_register_spi() - registers spi platform device
 * @info: ep93xx board specific spi master info (__initdata)
 * @devices: SPI devices to register (__initdata)
 * @num: number of SPI devices to register
 *
 * This function registers platform device for the EP93xx SPI controller and
 * also makes sure that SPI pins are muxed so that I2S is not using those pins.
 */
void __init ep93xx_register_spi(struct ep93xx_spi_info *info,
				struct spi_board_info *devices, int num)
{
	/*
	 * When SPI is used, we need to make sure that I2S is muxed off from
	 * SPI pins.
	 */
	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_I2SONSSP);

	ep93xx_spi_master_data = *info;
	spi_register_board_info(devices, num);
	platform_device_register(&ep93xx_spi_device);
}

/*************************************************************************
 * EP93xx LEDs
 *************************************************************************/
static struct gpio_led ep93xx_led_pins[] = {
	{
		.name	= "platform:grled",
		.gpio	= EP93XX_GPIO_LINE_GRLED,
	}, {
		.name	= "platform:rdled",
		.gpio	= EP93XX_GPIO_LINE_RDLED,
	},
};

static struct gpio_led_platform_data ep93xx_led_data = {
	.num_leds	= ARRAY_SIZE(ep93xx_led_pins),
	.leds		= ep93xx_led_pins,
};

static struct platform_device ep93xx_leds = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &ep93xx_led_data,
	},
};


/*************************************************************************
 * EP93xx pwm peripheral handling
 *************************************************************************/
static struct resource ep93xx_pwm0_resource[] = {
	{
		.start	= EP93XX_PWM_PHYS_BASE,
		.end	= EP93XX_PWM_PHYS_BASE + 0x10 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device ep93xx_pwm0_device = {
	.name		= "ep93xx-pwm",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(ep93xx_pwm0_resource),
	.resource	= ep93xx_pwm0_resource,
};

static struct resource ep93xx_pwm1_resource[] = {
	{
		.start	= EP93XX_PWM_PHYS_BASE + 0x20,
		.end	= EP93XX_PWM_PHYS_BASE + 0x30 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device ep93xx_pwm1_device = {
	.name		= "ep93xx-pwm",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(ep93xx_pwm1_resource),
	.resource	= ep93xx_pwm1_resource,
};

void __init ep93xx_register_pwm(int pwm0, int pwm1)
{
	if (pwm0)
		platform_device_register(&ep93xx_pwm0_device);

	/* NOTE: EP9307 does not have PWMOUT1 (pin EGPIO14) */
	if (pwm1)
		platform_device_register(&ep93xx_pwm1_device);
}

int ep93xx_pwm_acquire_gpio(struct platform_device *pdev)
{
	int err;

	if (pdev->id == 0) {
		err = 0;
	} else if (pdev->id == 1) {
		err = gpio_request(EP93XX_GPIO_LINE_EGPIO14,
				   dev_name(&pdev->dev));
		if (err)
			return err;
		err = gpio_direction_output(EP93XX_GPIO_LINE_EGPIO14, 0);
		if (err)
			goto fail;

		/* PWM 1 output on EGPIO[14] */
		ep93xx_devcfg_set_bits(EP93XX_SYSCON_DEVCFG_PONG);
	} else {
		err = -ENODEV;
	}

	return err;

fail:
	gpio_free(EP93XX_GPIO_LINE_EGPIO14);
	return err;
}
EXPORT_SYMBOL(ep93xx_pwm_acquire_gpio);

void ep93xx_pwm_release_gpio(struct platform_device *pdev)
{
	if (pdev->id == 1) {
		gpio_direction_input(EP93XX_GPIO_LINE_EGPIO14);
		gpio_free(EP93XX_GPIO_LINE_EGPIO14);

		/* EGPIO[14] used for GPIO */
		ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_PONG);
	}
}
EXPORT_SYMBOL(ep93xx_pwm_release_gpio);


/*************************************************************************
 * EP93xx video peripheral handling
 *************************************************************************/
static struct ep93xxfb_mach_info ep93xxfb_data;

static struct resource ep93xx_fb_resource[] = {
	{
		.start		= EP93XX_RASTER_PHYS_BASE,
		.end		= EP93XX_RASTER_PHYS_BASE + 0x800 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device ep93xx_fb_device = {
	.name			= "ep93xx-fb",
	.id			= -1,
	.dev			= {
		.platform_data		= &ep93xxfb_data,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.dma_mask		= &ep93xx_fb_device.dev.coherent_dma_mask,
	},
	.num_resources		= ARRAY_SIZE(ep93xx_fb_resource),
	.resource		= ep93xx_fb_resource,
};

/* The backlight use a single register in the framebuffer's register space */
#define EP93XX_RASTER_REG_BRIGHTNESS 0x20

static struct resource ep93xx_bl_resources[] = {
	DEFINE_RES_MEM(EP93XX_RASTER_PHYS_BASE +
		       EP93XX_RASTER_REG_BRIGHTNESS, 0x04),
};

static struct platform_device ep93xx_bl_device = {
	.name		= "ep93xx-bl",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_bl_resources),
	.resource	= ep93xx_bl_resources,
};

/**
 * ep93xx_register_fb - Register the framebuffer platform device.
 * @data:	platform specific framebuffer configuration (__initdata)
 */
void __init ep93xx_register_fb(struct ep93xxfb_mach_info *data)
{
	ep93xxfb_data = *data;
	platform_device_register(&ep93xx_fb_device);
	platform_device_register(&ep93xx_bl_device);
}


/*************************************************************************
 * EP93xx matrix keypad peripheral handling
 *************************************************************************/
static struct ep93xx_keypad_platform_data ep93xx_keypad_data;

static struct resource ep93xx_keypad_resource[] = {
	{
		.start	= EP93XX_KEY_MATRIX_PHYS_BASE,
		.end	= EP93XX_KEY_MATRIX_PHYS_BASE + 0x0c - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_EP93XX_KEY,
		.end	= IRQ_EP93XX_KEY,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ep93xx_keypad_device = {
	.name		= "ep93xx-keypad",
	.id		= -1,
	.dev		= {
		.platform_data	= &ep93xx_keypad_data,
	},
	.num_resources	= ARRAY_SIZE(ep93xx_keypad_resource),
	.resource	= ep93xx_keypad_resource,
};

/**
 * ep93xx_register_keypad - Register the keypad platform device.
 * @data:	platform specific keypad configuration (__initdata)
 */
void __init ep93xx_register_keypad(struct ep93xx_keypad_platform_data *data)
{
	ep93xx_keypad_data = *data;
	platform_device_register(&ep93xx_keypad_device);
}

int ep93xx_keypad_acquire_gpio(struct platform_device *pdev)
{
	int err;
	int i;

	for (i = 0; i < 8; i++) {
		err = gpio_request(EP93XX_GPIO_LINE_C(i), dev_name(&pdev->dev));
		if (err)
			goto fail_gpio_c;
		err = gpio_request(EP93XX_GPIO_LINE_D(i), dev_name(&pdev->dev));
		if (err)
			goto fail_gpio_d;
	}

	/* Enable the keypad controller; GPIO ports C and D used for keypad */
	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_KEYS |
				 EP93XX_SYSCON_DEVCFG_GONK);

	return 0;

fail_gpio_d:
	gpio_free(EP93XX_GPIO_LINE_C(i));
fail_gpio_c:
	for (--i; i >= 0; --i) {
		gpio_free(EP93XX_GPIO_LINE_C(i));
		gpio_free(EP93XX_GPIO_LINE_D(i));
	}
	return err;
}
EXPORT_SYMBOL(ep93xx_keypad_acquire_gpio);

void ep93xx_keypad_release_gpio(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < 8; i++) {
		gpio_free(EP93XX_GPIO_LINE_C(i));
		gpio_free(EP93XX_GPIO_LINE_D(i));
	}

	/* Disable the keypad controller; GPIO ports C and D used for GPIO */
	ep93xx_devcfg_set_bits(EP93XX_SYSCON_DEVCFG_KEYS |
			       EP93XX_SYSCON_DEVCFG_GONK);
}
EXPORT_SYMBOL(ep93xx_keypad_release_gpio);

/*************************************************************************
 * EP93xx I2S audio peripheral handling
 *************************************************************************/
static struct resource ep93xx_i2s_resource[] = {
	{
		.start	= EP93XX_I2S_PHYS_BASE,
		.end	= EP93XX_I2S_PHYS_BASE + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device ep93xx_i2s_device = {
	.name		= "ep93xx-i2s",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_i2s_resource),
	.resource	= ep93xx_i2s_resource,
};

static struct platform_device ep93xx_pcm_device = {
	.name		= "ep93xx-pcm-audio",
	.id		= -1,
};

void __init ep93xx_register_i2s(void)
{
	platform_device_register(&ep93xx_i2s_device);
	platform_device_register(&ep93xx_pcm_device);
}

#define EP93XX_SYSCON_DEVCFG_I2S_MASK	(EP93XX_SYSCON_DEVCFG_I2SONSSP | \
					 EP93XX_SYSCON_DEVCFG_I2SONAC97)

#define EP93XX_I2SCLKDIV_MASK		(EP93XX_SYSCON_I2SCLKDIV_ORIDE | \
					 EP93XX_SYSCON_I2SCLKDIV_SPOL)

int ep93xx_i2s_acquire(void)
{
	unsigned val;

	ep93xx_devcfg_set_clear(EP93XX_SYSCON_DEVCFG_I2SONAC97,
			EP93XX_SYSCON_DEVCFG_I2S_MASK);

	/*
	 * This is potentially racy with the clock api for i2s_mclk, sclk and 
	 * lrclk. Since the i2s driver is the only user of those clocks we
	 * rely on it to prevent parallel use of this function and the 
	 * clock api for the i2s clocks.
	 */
	val = __raw_readl(EP93XX_SYSCON_I2SCLKDIV);
	val &= ~EP93XX_I2SCLKDIV_MASK;
	val |= EP93XX_SYSCON_I2SCLKDIV_ORIDE | EP93XX_SYSCON_I2SCLKDIV_SPOL;
	ep93xx_syscon_swlocked_write(val, EP93XX_SYSCON_I2SCLKDIV);

	return 0;
}
EXPORT_SYMBOL(ep93xx_i2s_acquire);

void ep93xx_i2s_release(void)
{
	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_I2S_MASK);
}
EXPORT_SYMBOL(ep93xx_i2s_release);

/*************************************************************************
 * EP93xx AC97 audio peripheral handling
 *************************************************************************/
static struct resource ep93xx_ac97_resources[] = {
	{
		.start	= EP93XX_AAC_PHYS_BASE,
		.end	= EP93XX_AAC_PHYS_BASE + 0xac - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_EP93XX_AACINTR,
		.end	= IRQ_EP93XX_AACINTR,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ep93xx_ac97_device = {
	.name		= "ep93xx-ac97",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_ac97_resources),
	.resource	= ep93xx_ac97_resources,
};

void __init ep93xx_register_ac97(void)
{
	/*
	 * Make sure that the AC97 pins are not used by I2S.
	 */
	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_I2SONAC97);

	platform_device_register(&ep93xx_ac97_device);
	platform_device_register(&ep93xx_pcm_device);
}

/*************************************************************************
 * EP93xx Watchdog
 *************************************************************************/
static struct resource ep93xx_wdt_resources[] = {
	DEFINE_RES_MEM(EP93XX_WATCHDOG_PHYS_BASE, 0x08),
};

static struct platform_device ep93xx_wdt_device = {
	.name		= "ep93xx-wdt",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_wdt_resources),
	.resource	= ep93xx_wdt_resources,
};

void __init ep93xx_init_devices(void)
{
	/* Disallow access to MaverickCrunch initially */
	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_CPENA);

	/* Default all ports to GPIO */
	ep93xx_devcfg_set_bits(EP93XX_SYSCON_DEVCFG_KEYS |
			       EP93XX_SYSCON_DEVCFG_GONK |
			       EP93XX_SYSCON_DEVCFG_EONIDE |
			       EP93XX_SYSCON_DEVCFG_GONIDE |
			       EP93XX_SYSCON_DEVCFG_HONIDE);

	/* Get the GPIO working early, other devices need it */
	platform_device_register(&ep93xx_gpio_device);

	amba_device_register(&uart1_device, &iomem_resource);
	amba_device_register(&uart2_device, &iomem_resource);
	amba_device_register(&uart3_device, &iomem_resource);

	platform_device_register(&ep93xx_rtc_device);
	platform_device_register(&ep93xx_ohci_device);
	platform_device_register(&ep93xx_leds);
	platform_device_register(&ep93xx_wdt_device);
}

void ep93xx_restart(char mode, const char *cmd)
{
	/*
	 * Set then clear the SWRST bit to initiate a software reset
	 */
	ep93xx_devcfg_set_bits(EP93XX_SYSCON_DEVCFG_SWRST);
	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_SWRST);

	while (1)
		;
}
