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

struct mcf_gpio_chip mcf_gpio_chips[] = {
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
	MCFGPF(ETPU, 104, 3),
};

unsigned int mcf_gpio_chips_size = ARRAY_SIZE(mcf_gpio_chips);
