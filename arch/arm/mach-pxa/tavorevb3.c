/*
 *  linux/arch/arm/mach-pxa/tavorevb3.c
 *
 *  Support for the Marvell EVB3 Development Platform.
 *
 *  Copyright:  (C) Copyright 2008-2010 Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/mfd/88pm860x.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa930.h>

#include <plat/i2c.h>

#include "devices.h"
#include "generic.h"

#define TAVOREVB3_NR_IRQS	(IRQ_BOARD_START + 24)

static mfp_cfg_t evb3_mfp_cfg[] __initdata = {
	/* UART */
	GPIO53_UART1_TXD,
	GPIO54_UART1_RXD,

	/* PMIC */
	PMIC_INT_GPIO83,
};

#if defined(CONFIG_I2C_PXA) || defined(CONFIG_I2C_PXA_MODULE)
static struct pm860x_touch_pdata evb3_touch = {
	.gpadc_prebias	= 1,
	.slot_cycle	= 1,
	.tsi_prebias	= 6,
	.pen_prebias	= 16,
	.pen_prechg	= 2,
	.res_x		= 300,
};

static struct pm860x_backlight_pdata evb3_backlight[] = {
	{
		.id	= PM8606_ID_BACKLIGHT,
		.iset	= PM8606_WLED_CURRENT(24),
		.flags	= PM8606_BACKLIGHT1,
	},
	{},
};

static struct pm860x_led_pdata evb3_led[] = {
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

static struct pm860x_platform_data evb3_pm8607_info = {
	.touch				= &evb3_touch,
	.backlight			= &evb3_backlight[0],
	.led				= &evb3_led[0],
	.companion_addr			= 0x10,
	.irq_mode			= 0,
	.irq_base			= IRQ_BOARD_START,

	.i2c_port			= GI2C_PORT,
};

static struct i2c_board_info evb3_i2c_info[] = {
	{
		.type		= "88PM860x",
		.addr		= 0x34,
		.platform_data	= &evb3_pm8607_info,
		.irq		= gpio_to_irq(mfp_to_gpio(MFP_PIN_GPIO83)),
	},
};

static void __init evb3_init_i2c(void)
{
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(evb3_i2c_info));
}
#else
static inline void evb3_init_i2c(void) {}
#endif

static void __init evb3_init(void)
{
	/* initialize MFP configurations */
	pxa3xx_mfp_config(ARRAY_AND_SIZE(evb3_mfp_cfg));

	pxa_set_ffuart_info(NULL);

	evb3_init_i2c();
}

MACHINE_START(TAVOREVB3, "PXA950 Evaluation Board (aka TavorEVB3)")
	.boot_params	= 0xa0000100,
	.map_io         = pxa3xx_map_io,
	.nr_irqs	= TAVOREVB3_NR_IRQS,
	.init_irq       = pxa3xx_init_irq,
	.timer          = &pxa_timer,
	.init_machine   = evb3_init,
MACHINE_END
