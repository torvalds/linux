/*
 * Freescale STMP378X development board support
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
#include <linux/platform_device.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pins.h>
#include <mach/pinmux.h>
#include <mach/stmp3xxx.h>

#include "stmp378x.h"

static struct platform_device *devices[] = {
	&stmp3xxx_dbguart,
};

static struct pin_desc dbguart_pins_0[] = {
	{ PINID_PWM0, PIN_FUN3, },
	{ PINID_PWM1, PIN_FUN3, },
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

static void __init stmp378x_devb_init(void)
{
	stmp3xxx_pinmux_init(NR_REAL_IRQS);

	/* init stmp3xxx platform */
	stmp3xxx_init();

	stmp3xxx_dbguart.dev.platform_data = dbguart_pins_control;

	/* add board's devices */
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

MACHINE_START(STMP378X, "STMP378X")
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf0000000) >> 18) & 0xfffc,
	.boot_params	= 0x40000100,
	.map_io		= stmp378x_map_io,
	.init_irq	= stmp378x_init_irq,
	.timer		= &stmp3xxx_timer,
	.init_machine	= stmp378x_devb_init,
MACHINE_END
