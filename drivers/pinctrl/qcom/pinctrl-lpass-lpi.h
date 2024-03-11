/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 Linaro Ltd.
 */
#ifndef __PINCTRL_LPASS_LPI_H__
#define __PINCTRL_LPASS_LPI_H__

#include <linux/array_size.h>
#include <linux/bits.h>

#include "../core.h"

struct platform_device;

struct pinctrl_pin_desc;

#define LPI_SLEW_RATE_CTL_REG	0xa000
#define LPI_TLMM_REG_OFFSET		0x1000
#define LPI_SLEW_RATE_MAX		0x03
#define LPI_SLEW_BITS_SIZE		0x02
#define LPI_SLEW_RATE_MASK		GENMASK(1, 0)
#define LPI_GPIO_CFG_REG		0x00
#define LPI_GPIO_PULL_MASK		GENMASK(1, 0)
#define LPI_GPIO_FUNCTION_MASK		GENMASK(5, 2)
#define LPI_GPIO_OUT_STRENGTH_MASK	GENMASK(8, 6)
#define LPI_GPIO_OE_MASK		BIT(9)
#define LPI_GPIO_VALUE_REG		0x04
#define LPI_GPIO_VALUE_IN_MASK		BIT(0)
#define LPI_GPIO_VALUE_OUT_MASK		BIT(1)

#define LPI_GPIO_BIAS_DISABLE		0x0
#define LPI_GPIO_PULL_DOWN		0x1
#define LPI_GPIO_KEEPER			0x2
#define LPI_GPIO_PULL_UP		0x3
#define LPI_GPIO_DS_TO_VAL(v)		(v / 2 - 1)
#define LPI_NO_SLEW				-1

#define LPI_FUNCTION(fname)			                \
	[LPI_MUX_##fname] = {		                \
		.name = #fname,				\
		.groups = fname##_groups,               \
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define LPI_PINGROUP(id, soff, f1, f2, f3, f4)		\
	{						\
		.pin = id,				\
		.slew_offset = soff,			\
		.funcs = (int[]){			\
			LPI_MUX_gpio,			\
			LPI_MUX_##f1,			\
			LPI_MUX_##f2,			\
			LPI_MUX_##f3,			\
			LPI_MUX_##f4,			\
		},					\
		.nfuncs = 5,				\
	}

/*
 * Slew rate control is done in the same register as rest of the
 * pin configuration.
 */
#define LPI_FLAG_SLEW_RATE_SAME_REG			BIT(0)

struct lpi_pingroup {
	unsigned int pin;
	/* Bit offset in slew register for SoundWire pins only */
	int slew_offset;
	unsigned int *funcs;
	unsigned int nfuncs;
};

struct lpi_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
};

struct lpi_pinctrl_variant_data {
	const struct pinctrl_pin_desc *pins;
	int npins;
	const struct lpi_pingroup *groups;
	int ngroups;
	const struct lpi_function *functions;
	int nfunctions;
	unsigned int flags;
};

int lpi_pinctrl_probe(struct platform_device *pdev);
void lpi_pinctrl_remove(struct platform_device *pdev);

#endif /*__PINCTRL_LPASS_LPI_H__*/
