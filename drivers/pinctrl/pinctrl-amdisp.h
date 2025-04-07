/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ISP Pinctrl Driver
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 */

static const struct pinctrl_pin_desc amdisp_pins[] = {
	PINCTRL_PIN(0, "GPIO_0"), /* sensor0 control */
	PINCTRL_PIN(1, "GPIO_1"), /* sensor1 control */
	PINCTRL_PIN(2, "GPIO_2"), /* sensor2 control */
};

#define AMDISP_GPIO_PINS(pin) \
static const unsigned int gpio##pin##_pins[] = { pin }
AMDISP_GPIO_PINS(0);
AMDISP_GPIO_PINS(1);
AMDISP_GPIO_PINS(2);

static const unsigned int amdisp_range_pins[] = {
	0, 1, 2
};

static const char * const amdisp_range_pins_name[] = {
	"gpio0", "gpio1", "gpio2"
};

enum amdisp_functions {
	mux_gpio,
	mux_NA
};

static const char * const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2"
};

/**
 * struct amdisp_function - a pinmux function
 * @name:    Name of the pinmux function.
 * @groups:  List of pingroups for this function.
 * @ngroups: Number of entries in @groups.
 */
struct amdisp_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
};

#define FUNCTION(fname)					\
	[mux_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

static const struct amdisp_function amdisp_functions[] = {
	FUNCTION(gpio),
};

/**
 * struct amdisp_pingroup - a pinmux group
 * @name:  Name of the pinmux group.
 * @pins:  List of pins for this group.
 * @npins: Number of entries in @pins.
 * @funcs: List of functions belongs to this group.
 * @nfuncs: Number of entries in @funcs.
 * @offset: Group offset in amdisp pinmux groups.
 */
struct amdisp_pingroup {
	const char *name;
	const unsigned int *pins;
	unsigned int npins;
	unsigned int *funcs;
	unsigned int nfuncs;
	unsigned int offset;
};

#define PINGROUP(id, f0)					\
	{							\
		.name = "gpio" #id,				\
		.pins = gpio##id##_pins,			\
		.npins = ARRAY_SIZE(gpio##id##_pins),		\
		.funcs = (int[]){				\
			mux_##f0,				\
		},						\
		.nfuncs = 1,					\
		.offset = id,					\
	}

static const struct amdisp_pingroup amdisp_groups[] = {
	PINGROUP(0, gpio),
	PINGROUP(1, gpio),
	PINGROUP(2, gpio),
};
