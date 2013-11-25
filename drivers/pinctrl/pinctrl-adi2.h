/*
 * Pinctrl Driver for ADI GPIO2 controller
 *
 * Copyright 2007-2013 Analog Devices Inc.
 *
 * Licensed under the GPLv2 or later
 */

#ifndef PINCTRL_PINCTRL_ADI2_H
#define PINCTRL_PINCTRL_ADI2_H

#include <linux/pinctrl/pinctrl.h>

 /**
 * struct adi_pin_group - describes a pin group
 * @name: the name of this pin group
 * @pins: an array of pins
 * @num: the number of pins in this array
 */
struct adi_pin_group {
	const char *name;
	const unsigned *pins;
	const unsigned num;
};

#define ADI_PIN_GROUP(n, p)  \
	{			\
		.name = n,	\
		.pins = p,	\
		.num = ARRAY_SIZE(p),	\
	}

 /**
 * struct adi_pmx_func - describes function mux setting of pin groups
 * @name: the name of this function mux setting
 * @groups: an array of pin groups
 * @num_groups: the number of pin groups in this array
 * @mux: the function mux setting array, end by zero
 */
struct adi_pmx_func {
	const char *name;
	const char * const *groups;
	const unsigned num_groups;
	const unsigned short *mux;
};

#define ADI_PMX_FUNCTION(n, g, m)		\
	{					\
		.name = n,			\
		.groups = g,			\
		.num_groups = ARRAY_SIZE(g),	\
		.mux = m,			\
	}

/**
 * struct adi_pinctrl_soc_data - ADI pin controller per-SoC configuration
 * @functions:  The functions supported on this SoC.
 * @nfunction:  The number of entries in @functions.
 * @groups:     An array describing all pin groups the pin SoC supports.
 * @ngroups:    The number of entries in @groups.
 * @pins:       An array describing all pins the pin controller affects.
 * @npins:      The number of entries in @pins.
 */
struct adi_pinctrl_soc_data {
	const struct adi_pmx_func *functions;
	int nfunctions;
	const struct adi_pin_group *groups;
	int ngroups;
	const struct pinctrl_pin_desc *pins;
	int npins;
};

void adi_pinctrl_soc_init(const struct adi_pinctrl_soc_data **soc);

#endif /* PINCTRL_PINCTRL_ADI2_H */
