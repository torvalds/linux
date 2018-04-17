/*
 * First generation of pinmux driver for Amlogic Meson SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 * Copyright (C) 2017 Jerome Brunet  <jbrunet@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

struct meson8_pmx_data {
	bool is_gpio;
	unsigned int reg;
	unsigned int bit;
};

#define PMX_DATA(r, b, g)						\
	{								\
		.reg = r,						\
		.bit = b,						\
		.is_gpio = g,						\
	}

#define GROUP(grp, r, b)						\
	{								\
		.name = #grp,						\
		.pins = grp ## _pins,					\
		.num_pins = ARRAY_SIZE(grp ## _pins),			\
		.data = (const struct meson8_pmx_data[]){		\
			PMX_DATA(r, b, false),				\
		},							\
	 }

#define GPIO_GROUP(gpio)						\
	{								\
		.name = #gpio,						\
		.pins = (const unsigned int[]){ gpio },			\
		.num_pins = 1,						\
		.data = (const struct meson8_pmx_data[]){		\
			PMX_DATA(0, 0, true),				\
		},							\
	}

extern const struct pinmux_ops meson8_pmx_ops;
