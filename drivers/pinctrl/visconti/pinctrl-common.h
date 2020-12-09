/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 TOSHIBA CORPORATION
 * Copyright (c) 2020 Toshiba Electronic Devices & Storage Corporation
 * Copyright (c) 2020 Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */

#ifndef __VISCONTI_PINCTRL_COMMON_H__
#define __VISCONTI_PINCTRL_COMMON_H__

struct pinctrl_pin_desc;

/* PIN */
#define VISCONTI_PINS(pins_name, ...)  \
	static const unsigned int pins_name ## _pins[] = { __VA_ARGS__ }

struct visconti_desc_pin {
	struct pinctrl_pin_desc pin;
	unsigned int dsel_offset;
	unsigned int dsel_shift;
	unsigned int pude_offset;
	unsigned int pudsel_offset;
	unsigned int pud_shift;
};

#define VISCONTI_PIN(_pin, dsel, d_sh, pude, pudsel, p_sh)	\
{								\
	.pin = _pin,						\
	.dsel_offset = dsel,					\
	.dsel_shift = d_sh,					\
	.pude_offset = pude,					\
	.pudsel_offset = pudsel,				\
	.pud_shift = p_sh,					\
}

/* Group */
#define VISCONTI_GROUPS(groups_name, ...)	\
	static const char * const groups_name ## _grps[] = { __VA_ARGS__ }

struct visconti_mux {
	unsigned int offset;
	unsigned int mask;
	unsigned int val;
};

struct visconti_pin_group {
	const char *name;
	const unsigned int *pins;
	unsigned int nr_pins;
	struct visconti_mux mux;
};

#define VISCONTI_PIN_GROUP(group_name, off, msk, v)	\
{							\
	.name = __stringify(group_name) "_grp",		\
	.pins = group_name ## _pins,			\
	.nr_pins = ARRAY_SIZE(group_name ## _pins),	\
	.mux = {					\
		.offset = off,				\
		.mask = msk,				\
		.val = v,				\
	}						\
}

/* MUX */
struct visconti_pin_function {
	const char *name;
	const char * const *groups;
	unsigned int nr_groups;
};

#define VISCONTI_PIN_FUNCTION(func)		\
{						\
	.name = #func,				\
	.groups = func ## _grps,		\
	.nr_groups = ARRAY_SIZE(func ## _grps),	\
}

/* chip dependent data */
struct visconti_pinctrl_devdata {
	const struct visconti_desc_pin *pins;
	unsigned int nr_pins;
	const struct visconti_pin_group *groups;
	unsigned int nr_groups;
	const struct visconti_pin_function *functions;
	unsigned int nr_functions;

	const struct visconti_mux *gpio_mux;

	void (*unlock)(void __iomem *base);
};

int visconti_pinctrl_probe(struct platform_device *pdev,
			   const struct visconti_pinctrl_devdata *devdata);

#endif /* __VISCONTI_PINCTRL_COMMON_H__ */
