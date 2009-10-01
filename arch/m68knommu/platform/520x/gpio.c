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
	{
		.gpio_chip			= {
			.label			= "PIRQ",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value,
			.ngpio			= 8,
		},
		.pddr				= MCFEPORT_EPDDR,
		.podr				= MCFEPORT_EPDR,
		.ppdr				= MCFEPORT_EPPDR,
	},
	{
		.gpio_chip			= {
			.label			= "BUSCTL",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 8,
			.ngpio			= 4,
		},
		.pddr				= MCFGPIO_PDDR_BUSCTL,
		.podr				= MCFGPIO_PODR_BUSCTL,
		.ppdr				= MCFGPIO_PPDSDR_BUSCTL,
		.setr				= MCFGPIO_PPDSDR_BUSCTL,
		.clrr				= MCFGPIO_PCLRR_BUSCTL,
	},
	{
		.gpio_chip			= {
			.label			= "BE",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 16,
			.ngpio			= 4,
		},
		.pddr				= MCFGPIO_PDDR_BE,
		.podr				= MCFGPIO_PODR_BE,
		.ppdr				= MCFGPIO_PPDSDR_BE,
		.setr				= MCFGPIO_PPDSDR_BE,
		.clrr				= MCFGPIO_PCLRR_BE,
	},
	{
		.gpio_chip			= {
			.label			= "CS",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 25,
			.ngpio			= 3,
		},
		.pddr				= MCFGPIO_PDDR_CS,
		.podr				= MCFGPIO_PODR_CS,
		.ppdr				= MCFGPIO_PPDSDR_CS,
		.setr				= MCFGPIO_PPDSDR_CS,
		.clrr				= MCFGPIO_PCLRR_CS,
	},
	{
		.gpio_chip			= {
			.label			= "FECI2C",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 32,
			.ngpio			= 4,
		},
		.pddr				= MCFGPIO_PDDR_FECI2C,
		.podr				= MCFGPIO_PODR_FECI2C,
		.ppdr				= MCFGPIO_PPDSDR_FECI2C,
		.setr				= MCFGPIO_PPDSDR_FECI2C,
		.clrr				= MCFGPIO_PCLRR_FECI2C,
	},
	{
		.gpio_chip			= {
			.label			= "QSPI",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 40,
			.ngpio			= 4,
		},
		.pddr				= MCFGPIO_PDDR_QSPI,
		.podr				= MCFGPIO_PODR_QSPI,
		.ppdr				= MCFGPIO_PPDSDR_QSPI,
		.setr				= MCFGPIO_PPDSDR_QSPI,
		.clrr				= MCFGPIO_PCLRR_QSPI,
	},
	{
		.gpio_chip			= {
			.label			= "TIMER",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 48,
			.ngpio			= 4,
		},
		.pddr				= MCFGPIO_PDDR_TIMER,
		.podr				= MCFGPIO_PODR_TIMER,
		.ppdr				= MCFGPIO_PPDSDR_TIMER,
		.setr				= MCFGPIO_PPDSDR_TIMER,
		.clrr				= MCFGPIO_PCLRR_TIMER,
	},
	{
		.gpio_chip			= {
			.label			= "UART",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 56,
			.ngpio			= 8,
		},
		.pddr				= MCFGPIO_PDDR_UART,
		.podr				= MCFGPIO_PODR_UART,
		.ppdr				= MCFGPIO_PPDSDR_UART,
		.setr				= MCFGPIO_PPDSDR_UART,
		.clrr				= MCFGPIO_PCLRR_UART,
	},
	{
		.gpio_chip			= {
			.label			= "FECH",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 64,
			.ngpio			= 8,
		},
		.pddr				= MCFGPIO_PDDR_FECH,
		.podr				= MCFGPIO_PODR_FECH,
		.ppdr				= MCFGPIO_PPDSDR_FECH,
		.setr				= MCFGPIO_PPDSDR_FECH,
		.clrr				= MCFGPIO_PCLRR_FECH,
	},
	{
		.gpio_chip			= {
			.label			= "FECL",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 72,
			.ngpio			= 8,
		},
		.pddr				= MCFGPIO_PDDR_FECL,
		.podr				= MCFGPIO_PODR_FECL,
		.ppdr				= MCFGPIO_PPDSDR_FECL,
		.setr				= MCFGPIO_PPDSDR_FECL,
		.clrr				= MCFGPIO_PCLRR_FECL,
	},
};

static int __init mcf_gpio_init(void)
{
	unsigned i = 0;
	while (i < ARRAY_SIZE(mcf_gpio_chips))
		(void)gpiochip_add((struct gpio_chip *)&mcf_gpio_chips[i++]);
	return 0;
}

core_initcall(mcf_gpio_init);
