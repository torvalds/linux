// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-ep93xx/vision_ep9307.c
 * Vision Engraving Systems EP9307 SoM support.
 *
 * Copyright (C) 2008-2011 Vision Engraving Systems
 * H Hartley Sweeten <hsweeten@visionengravers.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/mtd/partitions.h>
#include <linux/i2c.h>
#include <linux/platform_data/pca953x.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/spi/mmc_spi.h>
#include <linux/mmc/host.h>

#include <sound/cs4271.h>

#include "hardware.h"
#include <linux/platform_data/video-ep93xx.h>
#include <linux/platform_data/spi-ep93xx.h>
#include "gpio-ep93xx.h"

#include <asm/mach-types.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>

#include "soc.h"

/*************************************************************************
 * Static I/O mappings for the FPGA
 *************************************************************************/
#define VISION_PHYS_BASE	EP93XX_CS7_PHYS_BASE
#define VISION_VIRT_BASE	0xfebff000

static struct map_desc vision_io_desc[] __initdata = {
	{
		.virtual	= VISION_VIRT_BASE,
		.pfn		= __phys_to_pfn(VISION_PHYS_BASE),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static void __init vision_map_io(void)
{
	ep93xx_map_io();

	iotable_init(vision_io_desc, ARRAY_SIZE(vision_io_desc));
}

/*************************************************************************
 * Ethernet
 *************************************************************************/
static struct ep93xx_eth_data vision_eth_data __initdata = {
	.phy_id		= 1,
};

/*************************************************************************
 * Framebuffer
 *************************************************************************/
#define VISION_LCD_ENABLE	EP93XX_GPIO_LINE_EGPIO1

static int vision_lcd_setup(struct platform_device *pdev)
{
	int err;

	err = gpio_request_one(VISION_LCD_ENABLE, GPIOF_INIT_HIGH,
				dev_name(&pdev->dev));
	if (err)
		return err;

	ep93xx_devcfg_clear_bits(EP93XX_SYSCON_DEVCFG_RAS |
				 EP93XX_SYSCON_DEVCFG_RASONP3 |
				 EP93XX_SYSCON_DEVCFG_EXVC);

	return 0;
}

static void vision_lcd_teardown(struct platform_device *pdev)
{
	gpio_free(VISION_LCD_ENABLE);
}

static void vision_lcd_blank(int blank_mode, struct fb_info *info)
{
	if (blank_mode)
		gpio_set_value(VISION_LCD_ENABLE, 0);
	else
		gpio_set_value(VISION_LCD_ENABLE, 1);
}

static struct ep93xxfb_mach_info ep93xxfb_info __initdata = {
	.flags		= EP93XXFB_USE_SDCSN0 | EP93XXFB_PCLK_FALLING,
	.setup		= vision_lcd_setup,
	.teardown	= vision_lcd_teardown,
	.blank		= vision_lcd_blank,
};


/*************************************************************************
 * GPIO Expanders
 *************************************************************************/
#define PCA9539_74_GPIO_BASE	(EP93XX_GPIO_LINE_MAX + 1)
#define PCA9539_75_GPIO_BASE	(PCA9539_74_GPIO_BASE + 16)
#define PCA9539_76_GPIO_BASE	(PCA9539_75_GPIO_BASE + 16)
#define PCA9539_77_GPIO_BASE	(PCA9539_76_GPIO_BASE + 16)

static struct pca953x_platform_data pca953x_74_gpio_data = {
	.gpio_base	= PCA9539_74_GPIO_BASE,
	.irq_base	= EP93XX_BOARD_IRQ(0),
};

static struct pca953x_platform_data pca953x_75_gpio_data = {
	.gpio_base	= PCA9539_75_GPIO_BASE,
	.irq_base	= -1,
};

static struct pca953x_platform_data pca953x_76_gpio_data = {
	.gpio_base	= PCA9539_76_GPIO_BASE,
	.irq_base	= -1,
};

static struct pca953x_platform_data pca953x_77_gpio_data = {
	.gpio_base	= PCA9539_77_GPIO_BASE,
	.irq_base	= -1,
};

/*************************************************************************
 * I2C Bus
 *************************************************************************/

static struct i2c_board_info vision_i2c_info[] __initdata = {
	{
		I2C_BOARD_INFO("isl1208", 0x6f),
		.irq		= IRQ_EP93XX_EXT1,
	}, {
		I2C_BOARD_INFO("pca9539", 0x74),
		.platform_data	= &pca953x_74_gpio_data,
	}, {
		I2C_BOARD_INFO("pca9539", 0x75),
		.platform_data	= &pca953x_75_gpio_data,
	}, {
		I2C_BOARD_INFO("pca9539", 0x76),
		.platform_data	= &pca953x_76_gpio_data,
	}, {
		I2C_BOARD_INFO("pca9539", 0x77),
		.platform_data	= &pca953x_77_gpio_data,
	},
};

/*************************************************************************
 * SPI CS4271 Audio Codec
 *************************************************************************/
static struct cs4271_platform_data vision_cs4271_data = {
	/* Intentionally left blank */
};

/*************************************************************************
 * SPI Flash
 *************************************************************************/
static struct mtd_partition vision_spi_flash_partitions[] = {
	{
		.name	= "SPI bootstrap",
		.offset	= 0,
		.size	= SZ_4K,
	}, {
		.name	= "Bootstrap config",
		.offset	= MTDPART_OFS_APPEND,
		.size	= SZ_4K,
	}, {
		.name	= "System config",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct flash_platform_data vision_spi_flash_data = {
	.name		= "SPI Flash",
	.parts		= vision_spi_flash_partitions,
	.nr_parts	= ARRAY_SIZE(vision_spi_flash_partitions),
};

/*************************************************************************
 * SPI SD/MMC host
 *************************************************************************/
static struct mmc_spi_platform_data vision_spi_mmc_data = {
	.detect_delay	= 100,
	.powerup_msecs	= 100,
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.caps2		= MMC_CAP2_RO_ACTIVE_HIGH,
};

static struct gpiod_lookup_table vision_spi_mmc_gpio_table = {
	.dev_id = "mmc_spi.2", /* "mmc_spi @ CS2 */
	.table = {
		/* Card detect */
		GPIO_LOOKUP_IDX("B", 7, NULL, 0, GPIO_ACTIVE_LOW),
		/* Write protect */
		GPIO_LOOKUP_IDX("F", 0, NULL, 1, GPIO_ACTIVE_HIGH),
		{ },
	},
};

/*************************************************************************
 * SPI Bus
 *************************************************************************/
static struct spi_board_info vision_spi_board_info[] __initdata = {
	{
		.modalias		= "cs4271",
		.platform_data		= &vision_cs4271_data,
		.max_speed_hz		= 6000000,
		.bus_num		= 0,
		.chip_select		= 0,
		.mode			= SPI_MODE_3,
	}, {
		.modalias		= "sst25l",
		.platform_data		= &vision_spi_flash_data,
		.max_speed_hz		= 20000000,
		.bus_num		= 0,
		.chip_select		= 1,
		.mode			= SPI_MODE_3,
	}, {
		.modalias		= "mmc_spi",
		.platform_data		= &vision_spi_mmc_data,
		.max_speed_hz		= 20000000,
		.bus_num		= 0,
		.chip_select		= 2,
		.mode			= SPI_MODE_3,
	},
};

static struct gpiod_lookup_table vision_spi_cs4271_gpio_table = {
	.dev_id = "spi0.0", /* cs4271 @ CS0 */
	.table = {
		/* RESET */
		GPIO_LOOKUP_IDX("H", 2, NULL, 0, GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct gpiod_lookup_table vision_spi_cs_gpio_table = {
	.dev_id = "spi0",
	.table = {
		GPIO_LOOKUP_IDX("A", 6, "cs", 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("A", 7, "cs", 1, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("G", 2, "cs", 2, GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct ep93xx_spi_info vision_spi_master __initdata = {
	.use_dma	= 1,
};

/*************************************************************************
 * I2S Audio
 *************************************************************************/
static struct platform_device vision_audio_device = {
	.name		= "edb93xx-audio",
	.id		= -1,
};

static void __init vision_register_i2s(void)
{
	ep93xx_register_i2s();
	platform_device_register(&vision_audio_device);
}

/*************************************************************************
 * Machine Initialization
 *************************************************************************/
static void __init vision_init_machine(void)
{
	ep93xx_init_devices();
	ep93xx_register_flash(2, EP93XX_CS6_PHYS_BASE, SZ_64M);
	ep93xx_register_eth(&vision_eth_data, 1);
	ep93xx_register_fb(&ep93xxfb_info);
	ep93xx_register_pwm(1, 0);

	/*
	 * Request the gpio expander's interrupt gpio line now to prevent
	 * the kernel from doing a WARN in gpiolib:gpio_ensure_requested().
	 */
	if (gpio_request_one(EP93XX_GPIO_LINE_F(7), GPIOF_DIR_IN,
				"pca9539:74"))
		pr_warn("cannot request interrupt gpio for pca9539:74\n");

	vision_i2c_info[1].irq = gpio_to_irq(EP93XX_GPIO_LINE_F(7));

	ep93xx_register_i2c(vision_i2c_info,
				ARRAY_SIZE(vision_i2c_info));
	gpiod_add_lookup_table(&vision_spi_cs4271_gpio_table);
	gpiod_add_lookup_table(&vision_spi_mmc_gpio_table);
	gpiod_add_lookup_table(&vision_spi_cs_gpio_table);
	ep93xx_register_spi(&vision_spi_master, vision_spi_board_info,
				ARRAY_SIZE(vision_spi_board_info));
	vision_register_i2s();
}

MACHINE_START(VISION_EP9307, "Vision Engraving Systems EP9307")
	/* Maintainer: H Hartley Sweeten <hsweeten@visionengravers.com> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS + EP93XX_BOARD_IRQS,
	.map_io		= vision_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= vision_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
