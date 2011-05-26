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
		.pddr				= (void __iomem *) MCFEPORT_EPDDR,
		.podr				= (void __iomem *) MCFEPORT_EPDR,
		.ppdr				= (void __iomem *) MCFEPORT_EPPDR,
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
			.base			= 8,
			.ngpio			= 8,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_FECH,
		.podr				= (void __iomem *) MCFGPIO_PODR_FECH,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_FECH,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_FECH,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_FECH,
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
			.base			= 16,
			.ngpio			= 8,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_FECL,
		.podr				= (void __iomem *) MCFGPIO_PODR_FECL,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_FECL,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_FECL,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_FECL,
	},
	{
		.gpio_chip			= {
			.label			= "SSI",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 24,
			.ngpio			= 5,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_SSI,
		.podr				= (void __iomem *) MCFGPIO_PODR_SSI,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_SSI,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_SSI,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_SSI,
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
			.base			= 32,
			.ngpio			= 4,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_BUSCTL,
		.podr				= (void __iomem *) MCFGPIO_PODR_BUSCTL,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_BUSCTL,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_BUSCTL,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_BUSCTL,
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
			.base			= 40,
			.ngpio			= 4,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_BE,
		.podr				= (void __iomem *) MCFGPIO_PODR_BE,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_BE,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_BE,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_BE,
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
			.base			= 49,
			.ngpio			= 5,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_CS,
		.podr				= (void __iomem *) MCFGPIO_PODR_CS,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_CS,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_CS,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_CS,
	},
	{
		.gpio_chip			= {
			.label			= "PWM",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 58,
			.ngpio			= 4,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_PWM,
		.podr				= (void __iomem *) MCFGPIO_PODR_PWM,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_PWM,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_PWM,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_PWM,
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
			.base			= 64,
			.ngpio			= 4,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_FECI2C,
		.podr				= (void __iomem *) MCFGPIO_PODR_FECI2C,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_FECI2C,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_FECI2C,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_FECI2C,
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
			.base			= 72,
			.ngpio			= 8,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_UART,
		.podr				= (void __iomem *) MCFGPIO_PODR_UART,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_UART,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_UART,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_UART,
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
			.base			= 80,
			.ngpio			= 6,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_QSPI,
		.podr				= (void __iomem *) MCFGPIO_PODR_QSPI,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_QSPI,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_QSPI,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_QSPI,
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
			.base			= 88,
			.ngpio			= 4,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_TIMER,
		.podr				= (void __iomem *) MCFGPIO_PODR_TIMER,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_TIMER,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_TIMER,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_TIMER,
	},
	{
		.gpio_chip			= {
			.label			= "LCDDATAH",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 96,
			.ngpio			= 2,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_LCDDATAH,
		.podr				= (void __iomem *) MCFGPIO_PODR_LCDDATAH,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_LCDDATAH,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_LCDDATAH,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_LCDDATAH,
	},
	{
		.gpio_chip			= {
			.label			= "LCDDATAM",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 104,
			.ngpio			= 8,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_LCDDATAM,
		.podr				= (void __iomem *) MCFGPIO_PODR_LCDDATAM,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_LCDDATAM,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_LCDDATAM,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_LCDDATAM,
	},
	{
		.gpio_chip			= {
			.label			= "LCDDATAL",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 112,
			.ngpio			= 8,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_LCDDATAL,
		.podr				= (void __iomem *) MCFGPIO_PODR_LCDDATAL,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_LCDDATAL,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_LCDDATAL,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_LCDDATAL,
	},
	{
		.gpio_chip			= {
			.label			= "LCDCTLH",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 120,
			.ngpio			= 1,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_LCDCTLH,
		.podr				= (void __iomem *) MCFGPIO_PODR_LCDCTLH,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_LCDCTLH,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_LCDCTLH,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_LCDCTLH,
	},
	{
		.gpio_chip			= {
			.label			= "LCDCTLL",
			.request		= mcf_gpio_request,
			.free			= mcf_gpio_free,
			.direction_input	= mcf_gpio_direction_input,
			.direction_output	= mcf_gpio_direction_output,
			.get			= mcf_gpio_get_value,
			.set			= mcf_gpio_set_value_fast,
			.base			= 128,
			.ngpio			= 8,
		},
		.pddr				= (void __iomem *) MCFGPIO_PDDR_LCDCTLL,
		.podr				= (void __iomem *) MCFGPIO_PODR_LCDCTLL,
		.ppdr				= (void __iomem *) MCFGPIO_PPDSDR_LCDCTLL,
		.setr				= (void __iomem *) MCFGPIO_PPDSDR_LCDCTLL,
		.clrr				= (void __iomem *) MCFGPIO_PCLRR_LCDCTLL,
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
