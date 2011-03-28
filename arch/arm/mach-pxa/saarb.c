/*
 *  linux/arch/arm/mach-pxa/saarb.c
 *
 *  Support for the Marvell Handheld Platform (aka SAARB)
 *
 *  Copyright (C) 2007-2010 Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/i2c/pxa-i2c.h>
#include <linux/mfd/88pm860x.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/mfp.h>
#include <mach/mfp-pxa930.h>
#include <mach/gpio.h>

#include "generic.h"

#define SAARB_NR_IRQS	(IRQ_BOARD_START + 40)

static struct pm860x_touch_pdata saarb_touch = {
	.gpadc_prebias	= 1,
	.slot_cycle	= 1,
	.tsi_prebias	= 6,
	.pen_prebias	= 16,
	.pen_prechg	= 2,
	.res_x		= 300,
};

static struct pm860x_backlight_pdata saarb_backlight[] = {
	{
		.id	= PM8606_ID_BACKLIGHT,
		.iset	= PM8606_WLED_CURRENT(24),
		.flags	= PM8606_BACKLIGHT1,
	},
	{},
};

static struct pm860x_led_pdata saarb_led[] = {
	{
		.id	= PM8606_ID_LED,
		.iset	= PM8606_LED_CURRENT(12),
		.flags	= PM8606_LED1_RED,
	}, {
		.id	= PM8606_ID_LED,
		.iset	= PM8606_LED_CURRENT(12),
		.flags	= PM8606_LED1_GREEN,
	}, {
		.id	= PM8606_ID_LED,
		.iset	= PM8606_LED_CURRENT(12),
		.flags	= PM8606_LED1_BLUE,
	}, {
		.id	= PM8606_ID_LED,
		.iset	= PM8606_LED_CURRENT(12),
		.flags	= PM8606_LED2_RED,
	}, {
		.id	= PM8606_ID_LED,
		.iset	= PM8606_LED_CURRENT(12),
		.flags	= PM8606_LED2_GREEN,
	}, {
		.id	= PM8606_ID_LED,
		.iset	= PM8606_LED_CURRENT(12),
		.flags	= PM8606_LED2_BLUE,
	},
};

static struct pm860x_platform_data saarb_pm8607_info = {
	.touch		= &saarb_touch,
	.backlight	= &saarb_backlight[0],
	.led		= &saarb_led[0],
	.companion_addr	= 0x10,
	.irq_mode	= 0,
	.irq_base	= IRQ_BOARD_START,

	.i2c_port	= GI2C_PORT,
};

static struct i2c_board_info saarb_i2c_info[] = {
	{
		.type		= "88PM860x",
		.addr		= 0x34,
		.platform_data	= &saarb_pm8607_info,
		.irq		= gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO83)),
	},
};

static void __init saarb_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(saarb_i2c_info));
}

MACHINE_START(SAARB, "PXA955 Handheld Platform (aka SAARB)")
	.boot_params    = 0xa0000100,
	.map_io         = pxa_map_io,
	.nr_irqs	= SAARB_NR_IRQS,
	.init_irq       = pxa95x_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = saarb_init,
MACHINE_END

