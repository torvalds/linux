/*
 * arch/arm/mach-lpc32xx/phy3250.c
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
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
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/spi/eeprom.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/amba/pl022.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include "common.h"

/*
 * Mapped GPIOLIB GPIOs
 */
#define SPI0_CS_GPIO	LPC32XX_GPIO(LPC32XX_GPIO_P3_GRP, 5)
#define LCD_POWER_GPIO	LPC32XX_GPIO(LPC32XX_GPO_P3_GRP, 0)
#define BKL_POWER_GPIO	LPC32XX_GPIO(LPC32XX_GPO_P3_GRP, 4)
#define LED_GPIO	LPC32XX_GPIO(LPC32XX_GPO_P3_GRP, 1)

/*
 * AMBA LCD controller
 */
static struct clcd_panel conn_lcd_panel = {
	.mode		= {
		.name		= "QVGA portrait",
		.refresh	= 60,
		.xres		= 240,
		.yres		= 320,
		.pixclock	= 191828,
		.left_margin	= 22,
		.right_margin	= 11,
		.upper_margin	= 2,
		.lower_margin	= 1,
		.hsync_len	= 5,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	.width		= -1,
	.height		= -1,
	.tim2		= (TIM2_IVS | TIM2_IHS),
	.cntl		= (CNTL_BGR | CNTL_LCDTFT | CNTL_LCDVCOMP(1) |
				CNTL_LCDBPP16_565),
	.bpp		= 16,
};
#define PANEL_SIZE (3 * SZ_64K)

static int lpc32xx_clcd_setup(struct clcd_fb *fb)
{
	dma_addr_t dma;

	fb->fb.screen_base = dma_alloc_writecombine(&fb->dev->dev,
		PANEL_SIZE, &dma, GFP_KERNEL);
	if (!fb->fb.screen_base) {
		printk(KERN_ERR "CLCD: unable to map framebuffer\n");
		return -ENOMEM;
	}

	fb->fb.fix.smem_start = dma;
	fb->fb.fix.smem_len = PANEL_SIZE;
	fb->panel = &conn_lcd_panel;

	if (gpio_request(LCD_POWER_GPIO, "LCD power"))
		printk(KERN_ERR "Error requesting gpio %u",
			LCD_POWER_GPIO);
	else if (gpio_direction_output(LCD_POWER_GPIO, 1))
		printk(KERN_ERR "Error setting gpio %u to output",
			LCD_POWER_GPIO);

	if (gpio_request(BKL_POWER_GPIO, "LCD backlight power"))
		printk(KERN_ERR "Error requesting gpio %u",
			BKL_POWER_GPIO);
	else if (gpio_direction_output(BKL_POWER_GPIO, 1))
		printk(KERN_ERR "Error setting gpio %u to output",
			BKL_POWER_GPIO);

	return 0;
}

static int lpc32xx_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_writecombine(&fb->dev->dev, vma,
		fb->fb.screen_base, fb->fb.fix.smem_start,
		fb->fb.fix.smem_len);
}

static void lpc32xx_clcd_remove(struct clcd_fb *fb)
{
	dma_free_writecombine(&fb->dev->dev, fb->fb.fix.smem_len,
		fb->fb.screen_base, fb->fb.fix.smem_start);
}

/*
 * On some early LCD modules (1307.0), the backlight logic is inverted.
 * For those board variants, swap the disable and enable states for
 * BKL_POWER_GPIO.
*/
static void clcd_disable(struct clcd_fb *fb)
{
	gpio_set_value(BKL_POWER_GPIO, 0);
	gpio_set_value(LCD_POWER_GPIO, 0);
}

static void clcd_enable(struct clcd_fb *fb)
{
	gpio_set_value(BKL_POWER_GPIO, 1);
	gpio_set_value(LCD_POWER_GPIO, 1);
}

static struct clcd_board lpc32xx_clcd_data = {
	.name		= "Phytec LCD",
	.check		= clcdfb_check,
	.decode		= clcdfb_decode,
	.disable	= clcd_disable,
	.enable		= clcd_enable,
	.setup		= lpc32xx_clcd_setup,
	.mmap		= lpc32xx_clcd_mmap,
	.remove		= lpc32xx_clcd_remove,
};

static struct amba_device lpc32xx_clcd_device = {
	.dev				= {
		.coherent_dma_mask	= ~0,
		.init_name		= "dev:clcd",
		.platform_data		= &lpc32xx_clcd_data,
	},
	.res				= {
		.start			= LPC32XX_LCD_BASE,
		.end			= (LPC32XX_LCD_BASE + SZ_4K - 1),
		.flags			= IORESOURCE_MEM,
	},
	.dma_mask			= ~0,
	.irq				= {IRQ_LPC32XX_LCD, NO_IRQ},
};

/*
 * AMBA SSP (SPI)
 */
static void phy3250_spi_cs_set(u32 control)
{
	gpio_set_value(SPI0_CS_GPIO, (int) control);
}

static struct pl022_config_chip spi0_chip_info = {
	.com_mode		= INTERRUPT_TRANSFER,
	.iface			= SSP_INTERFACE_MOTOROLA_SPI,
	.hierarchy		= SSP_MASTER,
	.slave_tx_disable	= 0,
	.rx_lev_trig		= SSP_RX_4_OR_MORE_ELEM,
	.tx_lev_trig		= SSP_TX_4_OR_MORE_EMPTY_LOC,
	.ctrl_len		= SSP_BITS_8,
	.wait_state		= SSP_MWIRE_WAIT_ZERO,
	.duplex			= SSP_MICROWIRE_CHANNEL_FULL_DUPLEX,
	.cs_control		= phy3250_spi_cs_set,
};

static struct pl022_ssp_controller lpc32xx_ssp0_data = {
	.bus_id			= 0,
	.num_chipselect		= 1,
	.enable_dma		= 0,
};

static struct amba_device lpc32xx_ssp0_device = {
	.dev				= {
		.coherent_dma_mask	= ~0,
		.init_name		= "dev:ssp0",
		.platform_data		= &lpc32xx_ssp0_data,
	},
	.res				= {
		.start			= LPC32XX_SSP0_BASE,
		.end			= (LPC32XX_SSP0_BASE + SZ_4K - 1),
		.flags			= IORESOURCE_MEM,
	},
	.dma_mask			= ~0,
	.irq				= {IRQ_LPC32XX_SSP0, NO_IRQ},
};

/* AT25 driver registration */
static int __init phy3250_spi_board_register(void)
{
#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
	static struct spi_board_info info[] = {
		{
			.modalias = "spidev",
			.max_speed_hz = 5000000,
			.bus_num = 0,
			.chip_select = 0,
			.controller_data = &spi0_chip_info,
		},
	};

#else
	static struct spi_eeprom eeprom = {
		.name = "at25256a",
		.byte_len = 0x8000,
		.page_size = 64,
		.flags = EE_ADDR2,
	};

	static struct spi_board_info info[] = {
		{
			.modalias = "at25",
			.max_speed_hz = 5000000,
			.bus_num = 0,
			.chip_select = 0,
			.mode = SPI_MODE_0,
			.platform_data = &eeprom,
			.controller_data = &spi0_chip_info,
		},
	};
#endif
	return spi_register_board_info(info, ARRAY_SIZE(info));
}
arch_initcall(phy3250_spi_board_register);

static struct i2c_board_info __initdata phy3250_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("pcf8563", 0x51),
	},
};

static struct gpio_led phy_leds[] = {
	{
		.name			= "led0",
		.gpio			= LED_GPIO,
		.active_low		= 1,
		.default_trigger	= "heartbeat",
	},
};

static struct gpio_led_platform_data led_data = {
	.leds = phy_leds,
	.num_leds = ARRAY_SIZE(phy_leds),
};

static struct platform_device lpc32xx_gpio_led_device = {
	.name			= "leds-gpio",
	.id			= -1,
	.dev.platform_data	= &led_data,
};

static struct platform_device *phy3250_devs[] __initdata = {
	&lpc32xx_i2c0_device,
	&lpc32xx_i2c1_device,
	&lpc32xx_i2c2_device,
	&lpc32xx_watchdog_device,
	&lpc32xx_gpio_led_device,
};

static struct amba_device *amba_devs[] __initdata = {
	&lpc32xx_clcd_device,
	&lpc32xx_ssp0_device,
};

/*
 * Board specific functions
 */
static void __init phy3250_board_init(void)
{
	u32 tmp;
	int i;

	lpc32xx_gpio_init();

	/* Register GPIOs used on this board */
	if (gpio_request(SPI0_CS_GPIO, "spi0 cs"))
		printk(KERN_ERR "Error requesting gpio %u",
			SPI0_CS_GPIO);
	else if (gpio_direction_output(SPI0_CS_GPIO, 1))
		printk(KERN_ERR "Error setting gpio %u to output",
			SPI0_CS_GPIO);

	/* Setup network interface for RMII mode */
	tmp = __raw_readl(LPC32XX_CLKPWR_MACCLK_CTRL);
	tmp &= ~LPC32XX_CLKPWR_MACCTRL_PINS_MSK;
	tmp |= LPC32XX_CLKPWR_MACCTRL_USE_RMII_PINS;
	__raw_writel(tmp, LPC32XX_CLKPWR_MACCLK_CTRL);

	/* Setup SLC NAND controller muxing */
	__raw_writel(LPC32XX_CLKPWR_NANDCLK_SEL_SLC,
		LPC32XX_CLKPWR_NAND_CLK_CTRL);

	/* Setup LCD muxing to RGB565 */
	tmp = __raw_readl(LPC32XX_CLKPWR_LCDCLK_CTRL) &
		~(LPC32XX_CLKPWR_LCDCTRL_LCDTYPE_MSK |
		LPC32XX_CLKPWR_LCDCTRL_PSCALE_MSK);
	tmp |= LPC32XX_CLKPWR_LCDCTRL_LCDTYPE_TFT16;
	__raw_writel(tmp, LPC32XX_CLKPWR_LCDCLK_CTRL);

	/* Set up I2C pull levels */
	tmp = __raw_readl(LPC32XX_CLKPWR_I2C_CLK_CTRL);
	tmp |= LPC32XX_CLKPWR_I2CCLK_USBI2CHI_DRIVE |
		LPC32XX_CLKPWR_I2CCLK_I2C2HI_DRIVE;
	__raw_writel(tmp, LPC32XX_CLKPWR_I2C_CLK_CTRL);

	/* Disable IrDA pulsing support on UART6 */
	tmp = __raw_readl(LPC32XX_UARTCTL_CTRL);
	tmp |= LPC32XX_UART_UART6_IRDAMOD_BYPASS;
	__raw_writel(tmp, LPC32XX_UARTCTL_CTRL);

	/* Enable DMA for I2S1 channel */
	tmp = __raw_readl(LPC32XX_CLKPWR_I2S_CLK_CTRL);
	tmp = LPC32XX_CLKPWR_I2SCTRL_I2S1_USE_DMA;
	__raw_writel(tmp, LPC32XX_CLKPWR_I2S_CLK_CTRL);

	lpc32xx_serial_init();

	/*
	 * AMBA peripheral clocks need to be enabled prior to AMBA device
	 * detection or a data fault will occur, so enable the clocks
	 * here. However, we don't want to enable them if the peripheral
	 * isn't included in the image
	 */
#ifdef CONFIG_FB_ARMCLCD
	tmp = __raw_readl(LPC32XX_CLKPWR_LCDCLK_CTRL);
	__raw_writel((tmp | LPC32XX_CLKPWR_LCDCTRL_CLK_EN),
		LPC32XX_CLKPWR_LCDCLK_CTRL);
#endif
#ifdef CONFIG_SPI_PL022
	tmp = __raw_readl(LPC32XX_CLKPWR_SSP_CLK_CTRL);
	__raw_writel((tmp | LPC32XX_CLKPWR_SSPCTRL_SSPCLK0_EN),
		LPC32XX_CLKPWR_SSP_CLK_CTRL);
#endif

	platform_add_devices(phy3250_devs, ARRAY_SIZE(phy3250_devs));
	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

	/* Test clock needed for UDA1380 initial init */
	__raw_writel(LPC32XX_CLKPWR_TESTCLK2_SEL_MOSC |
		LPC32XX_CLKPWR_TESTCLK_TESTCLK2_EN,
		LPC32XX_CLKPWR_TEST_CLK_SEL);

	i2c_register_board_info(0, phy3250_i2c_board_info,
		ARRAY_SIZE(phy3250_i2c_board_info));
}

static int __init lpc32xx_display_uid(void)
{
	u32 uid[4];

	lpc32xx_get_uid(uid);

	printk(KERN_INFO "LPC32XX unique ID: %08x%08x%08x%08x\n",
		uid[3], uid[2], uid[1], uid[0]);

	return 1;
}
arch_initcall(lpc32xx_display_uid);

MACHINE_START(PHY3250, "Phytec 3250 board with the LPC3250 Microcontroller")
	/* Maintainer: Kevin Wells, NXP Semiconductors */
	.boot_params	= 0x80000100,
	.map_io		= lpc32xx_map_io,
	.init_irq	= lpc32xx_init_irq,
	.timer		= &lpc32xx_timer,
	.init_machine	= phy3250_board_init,
MACHINE_END
