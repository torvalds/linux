/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2017 Baylibre SAS.
 * Author:  Jerome Brunet  <jbrunet@baylibre.com>
 *
 * Copyright (c) 2017 Amlogic, Inc. All rights reserved.
 * Author: Xingyu Chen <xingyu.chen@amlogic.com>
 *
 */

struct meson_pmx_bank {
	const char *name;
	unsigned int first;
	unsigned int last;
	unsigned int reg;
	unsigned int offset;
};

struct meson_axg_pmx_data {
	const struct meson_pmx_bank *pmx_banks;
	unsigned int num_pmx_banks;
};

#define BANK_PMX(n, f, l, r, o)				\
	{							\
		.name   = n,					\
		.first	= f,					\
		.last	= l,					\
		.reg	= r,					\
		.offset = o,					\
	}

struct meson_pmx_axg_data {
        unsigned int func;
};

#define PMX_DATA(f)							\
	{								\
		.func = f,						\
	}

#define GROUP(grp, f)							\
	{								\
		.name = #grp,						\
		.pins = grp ## _pins,                                   \
		.num_pins = ARRAY_SIZE(grp ## _pins),			\
		.data = (const struct meson_pmx_axg_data[]){		\
			PMX_DATA(f),					\
		},							\
	}

#define GPIO_GROUP(gpio)						\
	{								\
		.name = #gpio,						\
		.pins = (const unsigned int[]){ gpio },			\
		.num_pins = 1,						\
		.data = (const struct meson_pmx_axg_data[]){		\
			PMX_DATA(0),					\
		},							\
	}

extern const struct pinmux_ops meson_axg_pmx_ops;
