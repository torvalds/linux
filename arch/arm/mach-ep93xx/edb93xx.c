// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/arm/mach-ep93xx/edb93xx.c
 * Cirrus Logic EDB93xx Development Board support.
 *
 * EDB93XX, EDB9301, EDB9307A
 * Copyright (C) 2008-2009 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * EDB9302
 * Copyright (C) 2006 George Kashperko <george@chas.com.ua>
 *
 * EDB9302A, EDB9315, EDB9315A
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * EDB9307
 * Copyright (C) 2007 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * EDB9312
 * Copyright (C) 2006 Infosys Technologies Limited
 *                    Toufeeq Hussain <toufeeq_hussain@infosys.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/gpio/machine.h>

#include <sound/cs4271.h>

#include "hardware.h"
#include <linux/platform_data/video-ep93xx.h>
#include <linux/platform_data/spi-ep93xx.h>
#include "gpio-ep93xx.h"

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "soc.h"

static void __init edb93xx_register_flash(void)
{
	if (machine_is_edb9307() || machine_is_edb9312() ||
	    machine_is_edb9315()) {
		ep93xx_register_flash(4, EP93XX_CS6_PHYS_BASE, SZ_32M);
	} else {
		ep93xx_register_flash(2, EP93XX_CS6_PHYS_BASE, SZ_16M);
	}
}

static struct ep93xx_eth_data __initdata edb93xx_eth_data = {
	.phy_id		= 1,
};


/*************************************************************************
 * EDB93xx i2c peripheral handling
 *************************************************************************/

static struct i2c_board_info __initdata edb93xxa_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("isl1208", 0x6f),
	},
};

static struct i2c_board_info __initdata edb93xx_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("ds1337", 0x68),
	},
};

static void __init edb93xx_register_i2c(void)
{
	if (machine_is_edb9302a() || machine_is_edb9307a() ||
	    machine_is_edb9315a()) {
		ep93xx_register_i2c(edb93xxa_i2c_board_info,
				    ARRAY_SIZE(edb93xxa_i2c_board_info));
	} else if (machine_is_edb9302() || machine_is_edb9307()
		|| machine_is_edb9312() || machine_is_edb9315()) {
		ep93xx_register_i2c(edb93xx_i2c_board_info,
				    ARRAY_SIZE(edb93xx_i2c_board_info));
	}
}


/*************************************************************************
 * EDB93xx SPI peripheral handling
 *************************************************************************/
static struct cs4271_platform_data edb93xx_cs4271_data = {
	/* Intentionally left blank */
};

static struct spi_board_info edb93xx_spi_board_info[] __initdata = {
	{
		.modalias		= "cs4271",
		.platform_data		= &edb93xx_cs4271_data,
		.max_speed_hz		= 6000000,
		.bus_num		= 0,
		.chip_select		= 0,
		.mode			= SPI_MODE_3,
	},
};

static struct gpiod_lookup_table edb93xx_spi_cs_gpio_table = {
	.dev_id = "spi0",
	.table = {
		GPIO_LOOKUP("A", 6, "cs", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct ep93xx_spi_info edb93xx_spi_info __initdata = {
	/* Intentionally left blank */
};

static struct gpiod_lookup_table edb93xx_cs4272_edb9301_gpio_table = {
	.dev_id = "spi0.0", /* CS0 on SPI0 */
	.table = {
		GPIO_LOOKUP("A", 1, "reset", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct gpiod_lookup_table edb93xx_cs4272_edb9302_gpio_table = {
	.dev_id = "spi0.0", /* CS0 on SPI0 */
	.table = {
		GPIO_LOOKUP("H", 2, "reset", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct gpiod_lookup_table edb93xx_cs4272_edb9315_gpio_table = {
	.dev_id = "spi0.0", /* CS0 on SPI0 */
	.table = {
		GPIO_LOOKUP("B", 6, "reset", GPIO_ACTIVE_LOW),
		{ },
	},
};

static void __init edb93xx_register_spi(void)
{
	if (machine_is_edb9301() || machine_is_edb9302())
		gpiod_add_lookup_table(&edb93xx_cs4272_edb9301_gpio_table);
	else if (machine_is_edb9302a() || machine_is_edb9307a())
		gpiod_add_lookup_table(&edb93xx_cs4272_edb9302_gpio_table);
	else if (machine_is_edb9315a())
		gpiod_add_lookup_table(&edb93xx_cs4272_edb9315_gpio_table);

	gpiod_add_lookup_table(&edb93xx_spi_cs_gpio_table);
	ep93xx_register_spi(&edb93xx_spi_info, edb93xx_spi_board_info,
			    ARRAY_SIZE(edb93xx_spi_board_info));
}


/*************************************************************************
 * EDB93xx I2S
 *************************************************************************/
static struct platform_device edb93xx_audio_device = {
	.name		= "edb93xx-audio",
	.id		= -1,
};

static int __init edb93xx_has_audio(void)
{
	return (machine_is_edb9301() || machine_is_edb9302() ||
		machine_is_edb9302a() || machine_is_edb9307a() ||
		machine_is_edb9315a());
}

static void __init edb93xx_register_i2s(void)
{
	if (edb93xx_has_audio()) {
		ep93xx_register_i2s();
		platform_device_register(&edb93xx_audio_device);
	}
}


/*************************************************************************
 * EDB93xx pwm
 *************************************************************************/
static void __init edb93xx_register_pwm(void)
{
	if (machine_is_edb9301() ||
	    machine_is_edb9302() || machine_is_edb9302a()) {
		/* EP9301 and EP9302 only have pwm.1 (EGPIO14) */
		ep93xx_register_pwm(0, 1);
	} else if (machine_is_edb9307() || machine_is_edb9307a()) {
		/* EP9307 only has pwm.0 (PWMOUT) */
		ep93xx_register_pwm(1, 0);
	} else {
		/* EP9312 and EP9315 have both */
		ep93xx_register_pwm(1, 1);
	}
}


/*************************************************************************
 * EDB93xx framebuffer
 *************************************************************************/
static struct ep93xxfb_mach_info __initdata edb93xxfb_info = {
	.flags		= 0,
};

static int __init edb93xx_has_fb(void)
{
	/* These platforms have an ep93xx with video capability */
	return machine_is_edb9307() || machine_is_edb9307a() ||
	       machine_is_edb9312() || machine_is_edb9315() ||
	       machine_is_edb9315a();
}

static void __init edb93xx_register_fb(void)
{
	if (!edb93xx_has_fb())
		return;

	if (machine_is_edb9307a() || machine_is_edb9315a())
		edb93xxfb_info.flags |= EP93XXFB_USE_SDCSN0;
	else
		edb93xxfb_info.flags |= EP93XXFB_USE_SDCSN3;

	ep93xx_register_fb(&edb93xxfb_info);
}


/*************************************************************************
 * EDB93xx IDE
 *************************************************************************/
static int __init edb93xx_has_ide(void)
{
	/*
	 * Although EDB9312 and EDB9315 do have IDE capability, they have
	 * INTRQ line wired as pull-up, which makes using IDE interface
	 * problematic.
	 */
	return machine_is_edb9312() || machine_is_edb9315() ||
	       machine_is_edb9315a();
}

static void __init edb93xx_register_ide(void)
{
	if (!edb93xx_has_ide())
		return;

	ep93xx_register_ide();
}


static void __init edb93xx_init_machine(void)
{
	ep93xx_init_devices();
	edb93xx_register_flash();
	ep93xx_register_eth(&edb93xx_eth_data, 1);
	edb93xx_register_i2c();
	edb93xx_register_spi();
	edb93xx_register_i2s();
	edb93xx_register_pwm();
	edb93xx_register_fb();
	edb93xx_register_ide();
	ep93xx_register_adc();
}


#ifdef CONFIG_MACH_EDB9301
MACHINE_START(EDB9301, "Cirrus Logic EDB9301 Evaluation Board")
	/* Maintainer: H Hartley Sweeten <hsweeten@visionengravers.com> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= edb93xx_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9302
MACHINE_START(EDB9302, "Cirrus Logic EDB9302 Evaluation Board")
	/* Maintainer: George Kashperko <george@chas.com.ua> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= edb93xx_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9302A
MACHINE_START(EDB9302A, "Cirrus Logic EDB9302A Evaluation Board")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= edb93xx_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9307
MACHINE_START(EDB9307, "Cirrus Logic EDB9307 Evaluation Board")
	/* Maintainer: Herbert Valerio Riedel <hvr@gnu.org> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= edb93xx_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9307A
MACHINE_START(EDB9307A, "Cirrus Logic EDB9307A Evaluation Board")
	/* Maintainer: H Hartley Sweeten <hsweeten@visionengravers.com> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= edb93xx_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9312
MACHINE_START(EDB9312, "Cirrus Logic EDB9312 Evaluation Board")
	/* Maintainer: Toufeeq Hussain <toufeeq_hussain@infosys.com> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= edb93xx_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9315
MACHINE_START(EDB9315, "Cirrus Logic EDB9315 Evaluation Board")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= edb93xx_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9315A
MACHINE_START(EDB9315A, "Cirrus Logic EDB9315A Evaluation Board")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_EP93XX_IRQS,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.init_time	= ep93xx_timer_init,
	.init_machine	= edb93xx_init_machine,
	.restart	= ep93xx_restart,
MACHINE_END
#endif
