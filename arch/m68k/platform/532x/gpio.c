/*
 * Coldfire generic GPIO support
 *
 * (C) Copyright 2009, Steven King <sfking@fdwdc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfgpio.h>

static struct mcf_gpio_chip mcf_gpio_chips[] = {
	MCFGPS(PIRQ, 0, 8, MCFEPORT_EPDDR, MCFEPORT_EPDR, MCFEPORT_EPPDR),
	MCFGPF(FECH, 8, 8),
	MCFGPF(FECL, 16, 8),
	MCFGPF(SSI, 24, 5),
	MCFGPF(BUSCTL, 32, 4),
	MCFGPF(BE, 40, 4),
	MCFGPF(CS, 49, 5),
	MCFGPF(PWM, 58, 4),
	MCFGPF(FECI2C, 64, 4),
	MCFGPF(UART, 72, 8),
	MCFGPF(QSPI, 80, 6),
	MCFGPF(TIMER, 88, 4),
	MCFGPF(LCDDATAH, 96, 2),
	MCFGPF(LCDDATAM, 104, 8),
	MCFGPF(LCDDATAL, 112, 8),
	MCFGPF(LCDCTLH, 120, 1),
	MCFGPF(LCDCTLL, 128, 8),
};

static int __init mcf_gpio_init(void)
{
	unsigned i = 0;
	while (i < ARRAY_SIZE(mcf_gpio_chips))
		(void)gpiochip_add((struct gpio_chip *)&mcf_gpio_chips[i++]);
	return 0;
}

core_initcall(mcf_gpio_init);
