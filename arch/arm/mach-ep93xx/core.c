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
#include <linux/sys_soc.h>
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
#include <linux/irqchip/arm-vic.h>
#include <linux/reboot.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/random.h>

#include <mach/hardware.h>
#include <linux/platform_data/video-ep93xx.h>
#include <linux/platform_data/keypad-ep93xx.h>
#include <linux/platform_data/spi-ep93xx.h>
#include <mach/gpio-ep93xx.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

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
EXPORT_SYMBOL_GPL(ep93xx_chip_revision);

/*************************************************************************
 * EP93xx GPIO
 *************************************************************************/
static struct resource ep93xx_gpio_resource[] = {
	DEFINE_RES_MEM(EP93XX_GPIO_PHYS_BASE, 0xcc),
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
	{ IRQ_EP93XX_UART2 }, NULL);

static AMBA_APB_DEVICE(uart3, "apb:uart3", 0x00041010, EP93XX_UART3_PHYS_BASE,
	{ IRQ_EP93XX_UART3 }, &ep93xx_uart_data);

static struct resource ep93xx_rtc_resource[] = {
	DEFINE_RES_MEM(EP93XX_RTC_PHYS_BASE, 0x10c),
};

static struct platform_device ep93xx_rtc_device = {
	.name		= "ep93xx-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_rtc_resource),
	.resource	= ep93xx_rtc_resource,
};

/*************************************************************************
 * EP93xx OHCI USB Host
 *************************************************************************/

static struct clk *ep93xx_ohci_host_clock;

static int ep93xx_ohci_power_on(struct platform_device *pdev)
{
	if (!ep93xx_ohci_host_clock) {
		ep93xx_ohci_host_clock = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(ep93xx_ohci_host_clock))
			return PTR_ERR(ep93xx_ohci_host_clock);
	}

	return clk_enable(ep93xx_ohci_host_clock);
}

static void ep93xx_ohci_power_off(struct platform_device *pdev)
{
	clk_disable(ep93xx_ohci_host_clock);
}

static struct usb_ohci_pdata ep93xx_ohci_pdata = {
	.power_on	= ep93xx_ohci_power_on,
	.power_off	= ep93xx_ohci_power_off,
	.power_suspend	= ep93xx_ohci_power_off,
};

static struct resource ep93xx_ohci_resources[] = {
	DEFINE_RES_MEM(EP93XX_USB_PHYS_BASE, 0x1000),
	DEFINE_RES_IRQ(IRQ_EP93XX_USB),
};

static u64 ep93xx_ohci_dma_mask = DMA_BIT_MASK(32);

static struct platform_device ep93xx_ohci_device = {
	.name		= "ohci-platform",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_ohci_resources),
	.resource	= ep93xx_ohci_resources,
	.dev		= {
		.dma_mask		= &ep93xx_ohci_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &ep93xx_ohci_pdata,
	},
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
	DEFINE_RES_MEM(EP93XX_ETHERNET_PHYS_BASE, 0x10000),
	DEFINE_RES_IRQ(IRQ_EP93XX_ETHERNET),
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
	DEFINE_RES_MEM(EP93XX_SPI_PHYS_BASE, 0x18),
	DEFINE_RES_IRQ(IRQ_EP93XX_SSP),
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
static const struct gpio_led ep93xx_led_pins[] __initconst = {
	{
		.name	= "platform:grled",
		.gpio	= EP93XX_GPIO_LINE_GRLED,
	}, {
		.name	= "platform:rdled",
		.gpio	= EP93XX_GPIO_LINE_RDLED,
	},
};

static const struct gpio_led_platform_data ep93xx_led_data __initconst = {
	.num_leds	= ARRAY_SIZE(ep93xx_led_pins),
	.leds		= ep93xx_led_pins,
};

/*************************************************************************
 * EP93xx pwm peripheral handling
 *************************************************************************/
static struct resource ep93xx_pwm0_resource[] = {
	DEFINE_RES_MEM(EP93XX_PWM_PHYS_BASE, 0x10),
};

static struct platform_device ep93xx_pwm0_device = {
	.name		= "ep93xx-pwm",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(ep93xx_pwm0_resource),
	.resource	= ep93xx_pwm0_resource,
};

static struct resource ep93xx_pwm1_resource[] = {
	DEFINE_RES_MEM(EP93XX_PWM_PHYS_BASE + 0x20, 0x10),
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
	DEFINE_RES_MEM(EP93XX_RASTER_PHYS_BASE, 0x800),
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
	DEFINE_RES_MEM(EP93XX_KEY_MATRIX_PHYS_BASE, 0x0c),
	DEFINE_RES_IRQ(IRQ_EP93XX_KEY),
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
	DEFINE_RES_MEM(EP93XX_I2S_PHYS_BASE, 0x100),
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
	DEFINE_RES_MEM(EP93XX_AAC_PHYS_BASE, 0xac),
	DEFINE_RES_IRQ(IRQ_EP93XX_AACINTR),
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

/*************************************************************************
 * EP93xx IDE
 *************************************************************************/
static struct resource ep93xx_ide_resources[] = {
	DEFINE_RES_MEM(EP93XX_IDE_PHYS_BASE, 0x38),
	DEFINE_RES_IRQ(IRQ_EP93XX_EXT3),
};

static struct platform_device ep93xx_ide_device = {
	.name		= "ep93xx-ide",
	.id		= -1,
	.dev		= {
		.dma_mask		= &ep93xx_ide_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(ep93xx_ide_resources),
	.resource	= ep93xx_ide_resources,
};

void __init ep93xx_register_ide(void)
{
	platform_device_register(&ep93xx_ide_device);
}

int ep93xx_ide_acquire_gpio(struct platform_device *pdev)
{
	int err;
	int i;

	err = gpio_request(EP93XX_GPIO_LINE_EGPIO2, dev_name(&pdev->dev));
	if (err)
		return err;
	err = gpio_request(EP93XX_GPIO_LINE_EGPIO15, dev_name(&pdev->dev));
	if (err)
		goto fail_egpio15;
	for (i = 2; i < 8; i++) {
		err = gpio_request(EP93XX_GPIO_LINE_E(i), dev_name(&pdev->dev));
		if (err)
			goto fail_gpio_e;
	}
	for (i = 4; i < 8; i++) {
		err = gpio_request(EP93XX_GPIO_LINE_G(i), dev_name(&pdev->dev));
		if (err)
			goto fail_gpio_g;
	}
	for (i = 0; i < 8; i++) {
		err = gpio_request(EP93XX_GPIO_LINE_H(i), dev_name(&pdev->dev));
		if (err)
			goto fail_gpio_h;
	}

	/* GPIO ports E[7:2], G[7:4] and H used by IDE */
	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_EONIDE |
				 EP93XX_SYSCON_DEVCFG_GONIDE |
				 EP93XX_SYSCON_DEVCFG_HONIDE);
	return 0;

fail_gpio_h:
	for (--i; i >= 0; --i)
		gpio_free(EP93XX_GPIO_LINE_H(i));
	i = 8;
fail_gpio_g:
	for (--i; i >= 4; --i)
		gpio_free(EP93XX_GPIO_LINE_G(i));
	i = 8;
fail_gpio_e:
	for (--i; i >= 2; --i)
		gpio_free(EP93XX_GPIO_LINE_E(i));
	gpio_free(EP93XX_GPIO_LINE_EGPIO15);
fail_egpio15:
	gpio_free(EP93XX_GPIO_LINE_EGPIO2);
	return err;
}
EXPORT_SYMBOL(ep93xx_ide_acquire_gpio);

void ep93xx_ide_release_gpio(struct platform_device *pdev)
{
	int i;

	for (i = 2; i < 8; i++)
		gpio_free(EP93XX_GPIO_LINE_E(i));
	for (i = 4; i < 8; i++)
		gpio_free(EP93XX_GPIO_LINE_G(i));
	for (i = 0; i < 8; i++)
		gpio_free(EP93XX_GPIO_LINE_H(i));
	gpio_free(EP93XX_GPIO_LINE_EGPIO15);
	gpio_free(EP93XX_GPIO_LINE_EGPIO2);


	/* GPIO ports E[7:2], G[7:4] and H used by GPIO */
	ep93xx_devcfg_set_bits(EP93XX_SYSCON_DEVCFG_EONIDE |
			       EP93XX_SYSCON_DEVCFG_GONIDE |
			       EP93XX_SYSCON_DEVCFG_HONIDE);
}
EXPORT_SYMBOL(ep93xx_ide_release_gpio);

/*************************************************************************
 * EP93xx ADC
 *************************************************************************/
static struct resource ep93xx_adc_resources[] = {
	DEFINE_RES_MEM(EP93XX_ADC_PHYS_BASE, 0x28),
	DEFINE_RES_IRQ(IRQ_EP93XX_TOUCH),
};

static struct platform_device ep93xx_adc_device = {
	.name		= "ep93xx-adc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ep93xx_adc_resources),
	.resource	= ep93xx_adc_resources,
};

void __init ep93xx_register_adc(void)
{
	/* Power up ADC, deactivate Touch Screen Controller */
	ep93xx_devcfg_set_clear(EP93XX_SYSCON_DEVCFG_TIN,
				EP93XX_SYSCON_DEVCFG_ADCPD);

	platform_device_register(&ep93xx_adc_device);
}

/*************************************************************************
 * EP93xx Security peripheral
 *************************************************************************/

/*
 * The Maverick Key is 256 bits of micro fuses blown at the factory during
 * manufacturing to uniquely identify a part.
 *
 * See: http://arm.cirrus.com/forum/viewtopic.php?t=486&highlight=maverick+key
 */
#define EP93XX_SECURITY_REG(x)		(EP93XX_SECURITY_BASE + (x))
#define EP93XX_SECURITY_SECFLG		EP93XX_SECURITY_REG(0x2400)
#define EP93XX_SECURITY_FUSEFLG		EP93XX_SECURITY_REG(0x2410)
#define EP93XX_SECURITY_UNIQID		EP93XX_SECURITY_REG(0x2440)
#define EP93XX_SECURITY_UNIQCHK		EP93XX_SECURITY_REG(0x2450)
#define EP93XX_SECURITY_UNIQVAL		EP93XX_SECURITY_REG(0x2460)
#define EP93XX_SECURITY_SECID1		EP93XX_SECURITY_REG(0x2500)
#define EP93XX_SECURITY_SECID2		EP93XX_SECURITY_REG(0x2504)
#define EP93XX_SECURITY_SECCHK1		EP93XX_SECURITY_REG(0x2520)
#define EP93XX_SECURITY_SECCHK2		EP93XX_SECURITY_REG(0x2524)
#define EP93XX_SECURITY_UNIQID2		EP93XX_SECURITY_REG(0x2700)
#define EP93XX_SECURITY_UNIQID3		EP93XX_SECURITY_REG(0x2704)
#define EP93XX_SECURITY_UNIQID4		EP93XX_SECURITY_REG(0x2708)
#define EP93XX_SECURITY_UNIQID5		EP93XX_SECURITY_REG(0x270c)

static char ep93xx_soc_id[33];

static const char __init *ep93xx_get_soc_id(void)
{
	unsigned int id, id2, id3, id4, id5;

	if (__raw_readl(EP93XX_SECURITY_UNIQVAL) != 1)
		return "bad Hamming code";

	id = __raw_readl(EP93XX_SECURITY_UNIQID);
	id2 = __raw_readl(EP93XX_SECURITY_UNIQID2);
	id3 = __raw_readl(EP93XX_SECURITY_UNIQID3);
	id4 = __raw_readl(EP93XX_SECURITY_UNIQID4);
	id5 = __raw_readl(EP93XX_SECURITY_UNIQID5);

	if (id != id2)
		return "invalid";

	/* Toss the unique ID into the entropy pool */
	add_device_randomness(&id2, 4);
	add_device_randomness(&id3, 4);
	add_device_randomness(&id4, 4);
	add_device_randomness(&id5, 4);

	snprintf(ep93xx_soc_id, sizeof(ep93xx_soc_id),
		 "%08x%08x%08x%08x", id2, id3, id4, id5);

	return ep93xx_soc_id;
}

static const char __init *ep93xx_get_soc_rev(void)
{
	int rev = ep93xx_chip_revision();

	switch (rev) {
	case EP93XX_CHIP_REV_D0:
		return "D0";
	case EP93XX_CHIP_REV_D1:
		return "D1";
	case EP93XX_CHIP_REV_E0:
		return "E0";
	case EP93XX_CHIP_REV_E1:
		return "E1";
	case EP93XX_CHIP_REV_E2:
		return "E2";
	default:
		return "unknown";
	}
}

static const char __init *ep93xx_get_machine_name(void)
{
	return kasprintf(GFP_KERNEL,"%s", machine_desc->name);
}

static struct device __init *ep93xx_init_soc(void)
{
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return NULL;

	soc_dev_attr->machine = ep93xx_get_machine_name();
	soc_dev_attr->family = "Cirrus Logic EP93xx";
	soc_dev_attr->revision = ep93xx_get_soc_rev();
	soc_dev_attr->soc_id = ep93xx_get_soc_id();

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->machine);
		kfree(soc_dev_attr);
		return NULL;
	}

	return soc_device_to_device(soc_dev);
}

struct device __init *ep93xx_init_devices(void)
{
	struct device *parent;

	/* Disallow access to MaverickCrunch initially */
	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_CPENA);

	/* Default all ports to GPIO */
	ep93xx_devcfg_set_bits(EP93XX_SYSCON_DEVCFG_KEYS |
			       EP93XX_SYSCON_DEVCFG_GONK |
			       EP93XX_SYSCON_DEVCFG_EONIDE |
			       EP93XX_SYSCON_DEVCFG_GONIDE |
			       EP93XX_SYSCON_DEVCFG_HONIDE);

	parent = ep93xx_init_soc();

	/* Get the GPIO working early, other devices need it */
	platform_device_register(&ep93xx_gpio_device);

	amba_device_register(&uart1_device, &iomem_resource);
	amba_device_register(&uart2_device, &iomem_resource);
	amba_device_register(&uart3_device, &iomem_resource);

	platform_device_register(&ep93xx_rtc_device);
	platform_device_register(&ep93xx_ohci_device);
	platform_device_register(&ep93xx_wdt_device);

	gpio_led_register_device(-1, &ep93xx_led_data);

	return parent;
}

void ep93xx_restart(enum reboot_mode mode, const char *cmd)
{
	/*
	 * Set then clear the SWRST bit to initiate a software reset
	 */
	ep93xx_devcfg_set_bits(EP93XX_SYSCON_DEVCFG_SWRST);
	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_SWRST);

	while (1)
		;
}

void __init ep93xx_init_late(void)
{
	crunch_init();
}
