/*
 * Freescale STMP37XX development board support
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/stmp3xxx.h>
#include <mach/pins.h>
#include <mach/pinmux.h>
#include "stmp37xx.h"

/*
 * List of STMP37xx development board specific devices
 */
static struct platform_device *stmp37xx_devb_devices[] = {
	&stmp3xxx_dbguart,
	&stmp3xxx_appuart,
};

static struct pin_desc dbguart_pins_0[] = {
	{ PINID_PWM0, PIN_FUN3, },
	{ PINID_PWM1, PIN_FUN3, },
};

struct pin_desc appuart_pins_0[] = {
	{ PINID_UART2_CTS, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
	{ PINID_UART2_RTS, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
	{ PINID_UART2_RX, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
	{ PINID_UART2_TX, PIN_FUN1, PIN_4MA, PIN_1_8V, 0, },
};

static struct pin_group appuart_pins[] = {
	[0] = {
		.pins		= appuart_pins_0,
		.nr_pins	= ARRAY_SIZE(appuart_pins_0),
	},
	/* 37xx has the only app uart */
};

static struct pin_group dbguart_pins[] = {
	[0] = {
		.pins		= dbguart_pins_0,
		.nr_pins	= ARRAY_SIZE(dbguart_pins_0),
	},
};

static int dbguart_pins_control(int id, int request)
{
	int r = 0;

	if (request)
		r = stmp3xxx_request_pin_group(&dbguart_pins[id], "debug uart");
	else
		stmp3xxx_release_pin_group(&dbguart_pins[id], "debug uart");
	return r;
}


static void __init stmp37xx_devb_init(void)
{
	stmp3xxx_pinmux_init(NR_REAL_IRQS);

	/* Init STMP3xxx platform */
	stmp3xxx_init();

	stmp3xxx_dbguart.dev.platform_data = dbguart_pins_control;
	stmp3xxx_appuart.dev.platform_data = appuart_pins;

	/* Add STMP37xx development board devices */
	platform_add_devices(stmp37xx_devb_devices,
			ARRAY_SIZE(stmp37xx_devb_devices));
}

MACHINE_START(STMP37XX, "STMP37XX")
	.boot_params	= 0x40000100,
	.map_io		= stmp37xx_map_io,
	.init_irq	= stmp37xx_init_irq,
	.timer		= &stmp3xxx_timer,
	.init_machine	= stmp37xx_devb_init,
MACHINE_END
