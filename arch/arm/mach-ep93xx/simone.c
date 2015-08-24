/*
 * arch/arm/mach-ep93xx/simone.c
 * Simplemachines Sim.One support.
 *
 * Copyright (C) 2010 Ryan Mallon
 *
 * Based on the 2.6.24.7 support:
 *   Copyright (C) 2009 Simplemachines
 *   MMC support by Peter Ivanov <ivanovp@gmail.com>, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/mmc/host.h>
#include <linux/spi/spi.h>
#include <linux/spi/mmc_spi.h>
#include <linux/platform_data/video-ep93xx.h>
#include <linux/platform_data/spi-ep93xx.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <mach/gpio-ep93xx.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "soc.h"

static struct ep93xx_eth_data __initdata simone_eth_data = {
	.phy_id		= 1,
};

static struct ep93xxfb_mach_info __initdata simone_fb_info = {
	.num_modes	= EP93XXFB_USE_MODEDB,
	.bpp		= 16,
	.flags		= EP93XXFB_USE_SDCSN0 | EP93XXFB_PCLK_FALLING,
};

/*
 * GPIO lines used for MMC card detection.
 */
#define MMC_CARD_DETECT_GPIO EP93XX_GPIO_LINE_EGPIO0

/*
 * Up to v1.3, the Sim.One used SFRMOUT as SD card chip select, but this goes
 * low between multi-message command blocks. From v1.4, it uses a GPIO instead.
 * v1.3 parts will still work, since the signal on SFRMOUT is automatic.
 */
#define MMC_CHIP_SELECT_GPIO EP93XX_GPIO_LINE_EGPIO1

/*
 * MMC SPI chip select GPIO handling. If you are using SFRMOUT (SFRM1) signal,
 * you can leave these empty and pass NULL as .controller_data.
 */

static int simone_mmc_spi_setup(struct spi_device *spi)
{
	unsigned int gpio = MMC_CHIP_SELECT_GPIO;
	int err;

	err = gpio_request(gpio, spi->modalias);
	if (err)
		return err;

	err = gpio_direction_output(gpio, 1);
	if (err) {
		gpio_free(gpio);
		return err;
	}

	return 0;
}

static void simone_mmc_spi_cleanup(struct spi_device *spi)
{
	unsigned int gpio = MMC_CHIP_SELECT_GPIO;

	gpio_set_value(gpio, 1);
	gpio_direction_input(gpio);
	gpio_free(gpio);
}

static void simone_mmc_spi_cs_control(struct spi_device *spi, int value)
{
	gpio_set_value(MMC_CHIP_SELECT_GPIO, value);
}

static struct ep93xx_spi_chip_ops simone_mmc_spi_ops = {
	.setup		= simone_mmc_spi_setup,
	.cleanup	= simone_mmc_spi_cleanup,
	.cs_control	= simone_mmc_spi_cs_control,
};

/*
 * MMC card detection GPIO setup.
 */

static int simone_mmc_spi_init(struct device *dev,
	irqreturn_t (*irq_handler)(int, void *), void *mmc)
{
	unsigned int gpio = MMC_CARD_DETECT_GPIO;
	int irq, err;

	err = gpio_request(gpio, dev_name(dev));
	if (err)
		return err;

	err = gpio_direction_input(gpio);
	if (err)
		goto fail;

	irq = gpio_to_irq(gpio);
	if (irq < 0)
		goto fail;

	err = request_irq(irq, irq_handler, IRQF_TRIGGER_FALLING,
			  "MMC card detect", mmc);
	if (err)
		goto fail;

	printk(KERN_INFO "%s: using irq %d for MMC card detection\n",
	       dev_name(dev), irq);

	return 0;
fail:
	gpio_free(gpio);
	return err;
}

static void simone_mmc_spi_exit(struct device *dev, void *mmc)
{
	unsigned int gpio = MMC_CARD_DETECT_GPIO;

	free_irq(gpio_to_irq(gpio), mmc);
	gpio_free(gpio);
}

static struct mmc_spi_platform_data simone_mmc_spi_data = {
	.init		= simone_mmc_spi_init,
	.exit		= simone_mmc_spi_exit,
	.detect_delay	= 500,
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
};

static struct spi_board_info simone_spi_devices[] __initdata = {
	{
		.modalias		= "mmc_spi",
		.controller_data	= &simone_mmc_spi_ops,
		.platform_data		= &simone_mmc_spi_data,
		/*
		 * We use 10 MHz even though the maximum is 3.7 MHz. The driver
		 * will limit it automatically to max. frequency.
		 */
		.max_speed_hz		= 10 * 1000 * 1000,
		.bus_num		= 0,
		.chip_select		= 0,
		.mode			= SPI_MODE_3,
	},
};

static struct ep93xx_spi_info simone_spi_info __initdata = {
	.num_chipselect	= ARRAY_SIZE(simone_spi_devices),
};

static struct i2c_gpio_platform_data __initdata simone_i2c_gpio_data = {
	.sda_pin		= EP93XX_GPIO_LINE_EEDAT,
	.sda_is_open_drain	= 0,
	.scl_pin		= EP93XX_GPIO_LINE_EECLK,
	.scl_is_open_drain	= 0,
	.udelay			= 0,
	.timeout		= 0,
};

static struct i2c_board_info __initdata simone_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("ds1337", 0x68),
	},
};

static struct platform_device simone_audio_device = {
	.name		= "simone-audio",
	.id		= -1,
};

static void __init simone_register_audio(void)
{
	ep93xx_register_ac97();
	platform_device_register(&simone_audio_device);
}

static void __init simone_init_machine(void)
{
	ep93xx_init_devices();
	ep93xx_register_flash(2, EP93XX_CS6_PHYS_BASE, SZ_8M);
	ep93xx_register_eth(&simone_eth_data, 1);
	ep93xx_register_fb(&simone_fb_info);
	ep93xx_register_i2c(&simone_i2c_gpio_data, simone_i2c_board_info,
			    ARRAY_SIZE(simone_i2c_board_info));
	ep93xx_register_spi(&simone_spi_info, simone_spi_devices,
			    ARRAY_SIZE(simone_spi_devices));
	simone_register_audio();
}

MACHINE_START(SIM_ONE, "Simplemachines Sim.One Board")
	/* Maintainer: Ryan Mallon */
	.atag_offset	= 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= simone_init_machine,
	.init_late	= ep93xx_init_late,
	.restart	= ep93xx_restart,
MACHINE_END
