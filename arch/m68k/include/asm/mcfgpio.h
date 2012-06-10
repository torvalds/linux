/*
 * Coldfire generic GPIO support.
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

#ifndef mcfgpio_h
#define mcfgpio_h

#include <linux/io.h>
#include <asm-generic/gpio.h>

struct mcf_gpio_chip {
	struct gpio_chip gpio_chip;
	void __iomem *pddr;
	void __iomem *podr;
	void __iomem *ppdr;
	void __iomem *setr;
	void __iomem *clrr;
	const u8 *gpio_to_pinmux;
};

extern struct mcf_gpio_chip mcf_gpio_chips[];
extern unsigned int mcf_gpio_chips_size;

int mcf_gpio_direction_input(struct gpio_chip *, unsigned);
int mcf_gpio_get_value(struct gpio_chip *, unsigned);
int mcf_gpio_direction_output(struct gpio_chip *, unsigned, int);
void mcf_gpio_set_value(struct gpio_chip *, unsigned, int);
void mcf_gpio_set_value_fast(struct gpio_chip *, unsigned, int);
int mcf_gpio_request(struct gpio_chip *, unsigned);
void mcf_gpio_free(struct gpio_chip *, unsigned);

/*
 *	Define macros to ease the pain of setting up the GPIO tables. There
 *	are two cases we need to deal with here, they cover all currently
 *	available ColdFire GPIO hardware. There are of course minor differences
 *	in the layout and number of bits in each ColdFire part, but the macros
 *	take all that in.
 *
 *	Firstly is the conventional GPIO registers where we toggle individual
 *	bits in a register, preserving the other bits in the register. For
 *	lack of a better term I have called this the slow method.
 */
#define	MCFGPS(mlabel, mbase, mngpio, mpddr, mpodr, mppdr)		    \
	{								    \
		.gpio_chip			= {			    \
			.label			= #mlabel,		    \
			.request		= mcf_gpio_request,	    \
			.free			= mcf_gpio_free,	    \
			.direction_input	= mcf_gpio_direction_input, \
			.direction_output	= mcf_gpio_direction_output,\
			.get			= mcf_gpio_get_value,	    \
			.set			= mcf_gpio_set_value,       \
			.base			= mbase,		    \
			.ngpio			= mngpio,		    \
		},							    \
		.pddr		= (void __iomem *) mpddr,		    \
		.podr		= (void __iomem *) mpodr,		    \
		.ppdr		= (void __iomem *) mppdr,		    \
	}

/*
 *	Secondly is the faster case, where we have set and clear registers
 *	that allow us to set or clear a bit with a single write, not having
 *	to worry about preserving other bits.
 */
#define	MCFGPF(mlabel, mbase, mngpio)					    \
	{								    \
		.gpio_chip			= {			    \
			.label			= #mlabel,		    \
			.request		= mcf_gpio_request,	    \
			.free			= mcf_gpio_free,	    \
			.direction_input	= mcf_gpio_direction_input, \
			.direction_output	= mcf_gpio_direction_output,\
			.get			= mcf_gpio_get_value,	    \
			.set			= mcf_gpio_set_value_fast,  \
			.base			= mbase,		    \
			.ngpio			= mngpio,		    \
		},							    \
		.pddr		= (void __iomem *) MCFGPIO_PDDR_##mlabel,   \
		.podr		= (void __iomem *) MCFGPIO_PODR_##mlabel,   \
		.ppdr		= (void __iomem *) MCFGPIO_PPDSDR_##mlabel, \
		.setr		= (void __iomem *) MCFGPIO_PPDSDR_##mlabel, \
		.clrr		= (void __iomem *) MCFGPIO_PCLRR_##mlabel,  \
	}

#endif
