/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * First generation of pinmux driver for Amlogic Meson SoCs
 *
 * Copyright (C) 2014 Beniamino Galvani <b.galvani@gmail.com>
 * Copyright (C) 2017 Jerome Brunet  <jbrunet@baylibre.com>
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
