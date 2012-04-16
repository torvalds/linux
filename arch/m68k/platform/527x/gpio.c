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
#if defined(CONFIG_M5271)
	MCFGPS(PIRQ, 1, 7, MCFEPORT_EPDDR, MCFEPORT_EPDR, MCFEPORT_EPPDR),
	MCFGPF(ADDR, 13, 3),
	MCFGPF(DATAH, 16, 8),
	MCFGPF(DATAL, 24, 8),
	MCFGPF(BUSCTL, 32, 8),
	MCFGPF(BS, 40, 4),
	MCFGPF(CS, 49, 7),
	MCFGPF(SDRAM, 56, 6),
	MCFGPF(FECI2C, 64, 4),
	MCFGPF(UARTH, 72, 2),
	MCFGPF(UARTL, 80, 8),
	MCFGPF(QSPI, 88, 5),
	MCFGPF(TIMER, 96, 8),
#elif defined(CONFIG_M5275)
	MCFGPS(PIRQ, 1, 7, MCFEPORT_EPDDR, MCFEPORT_EPDR, MCFEPORT_EPPDR),
	MCFGPF(BUSCTL, 8, 8),
	MCFGPF(ADDR, 21, 3),
	MCFGPF(CS, 25, 7),
	MCFGPF(FEC0H, 32, 8),
	MCFGPF(FEC0L, 40, 8),
	MCFGPF(FECI2C, 48, 6),
	MCFGPF(QSPI, 56, 7),
	MCFGPF(SDRAM, 64, 8),
	MCFGPF(TIMERH, 72, 4),
	MCFGPF(TIMERL, 80, 4),
	MCFGPF(UARTL, 88, 8),
	MCFGPF(FEC1H, 96, 8),
	MCFGPF(FEC1L, 104, 8),
	MCFGPF(BS, 114, 2),
	MCFGPF(IRQ, 121, 7),
	MCFGPF(USBH, 128, 1),
	MCFGPF(USBL, 136, 8),
	MCFGPF(UARTH, 144, 4),
#endif
};

static int __init mcf_gpio_init(void)
{
	unsigned i = 0;
	while (i < ARRAY_SIZE(mcf_gpio_chips))
		(void)gpiochip_add((struct gpio_chip *)&mcf_gpio_chips[i++]);
	return 0;
}

core_initcall(mcf_gpio_init);
