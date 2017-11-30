/*
 * Copyright (C) 2015-2017 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PINCTRL_UNIPHIER_H__
#define __PINCTRL_UNIPHIER_H__

#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/kernel.h>
#include <linux/types.h>

struct platform_device;

/* input enable control register bit */
#define UNIPHIER_PIN_IECTRL_SHIFT	0
#define UNIPHIER_PIN_IECTRL_BITS	3
#define UNIPHIER_PIN_IECTRL_MASK	((1UL << (UNIPHIER_PIN_IECTRL_BITS)) \
					 - 1)

/* drive strength control register number */
#define UNIPHIER_PIN_DRVCTRL_SHIFT	((UNIPHIER_PIN_IECTRL_SHIFT) + \
					(UNIPHIER_PIN_IECTRL_BITS))
#define UNIPHIER_PIN_DRVCTRL_BITS	9
#define UNIPHIER_PIN_DRVCTRL_MASK	((1UL << (UNIPHIER_PIN_DRVCTRL_BITS)) \
					 - 1)

/* drive control type */
#define UNIPHIER_PIN_DRV_TYPE_SHIFT	((UNIPHIER_PIN_DRVCTRL_SHIFT) + \
					 (UNIPHIER_PIN_DRVCTRL_BITS))
#define UNIPHIER_PIN_DRV_TYPE_BITS	3
#define UNIPHIER_PIN_DRV_TYPE_MASK	((1UL << (UNIPHIER_PIN_DRV_TYPE_BITS)) \
					 - 1)

/* pull-up / pull-down register number */
#define UNIPHIER_PIN_PUPDCTRL_SHIFT	((UNIPHIER_PIN_DRV_TYPE_SHIFT) + \
					 (UNIPHIER_PIN_DRV_TYPE_BITS))
#define UNIPHIER_PIN_PUPDCTRL_BITS	9
#define UNIPHIER_PIN_PUPDCTRL_MASK	((1UL << (UNIPHIER_PIN_PUPDCTRL_BITS))\
					 - 1)

/* direction of pull register */
#define UNIPHIER_PIN_PULL_DIR_SHIFT	((UNIPHIER_PIN_PUPDCTRL_SHIFT) + \
					 (UNIPHIER_PIN_PUPDCTRL_BITS))
#define UNIPHIER_PIN_PULL_DIR_BITS	3
#define UNIPHIER_PIN_PULL_DIR_MASK	((1UL << (UNIPHIER_PIN_PULL_DIR_BITS))\
					 - 1)

#if UNIPHIER_PIN_PULL_DIR_SHIFT + UNIPHIER_PIN_PULL_DIR_BITS > BITS_PER_LONG
#error "unable to pack pin attributes."
#endif

#define UNIPHIER_PIN_IECTRL_NONE	(UNIPHIER_PIN_IECTRL_MASK)
#define UNIPHIER_PIN_IECTRL_EXIST	0

/* drive control type */
enum uniphier_pin_drv_type {
	UNIPHIER_PIN_DRV_1BIT,		/* 2 level control: 4/8 mA */
	UNIPHIER_PIN_DRV_2BIT,		/* 4 level control: 8/12/16/20 mA */
	UNIPHIER_PIN_DRV_3BIT,		/* 8 level control: 4/5/7/9/11/12/14/16 mA */
	UNIPHIER_PIN_DRV_FIXED4,	/* fixed to 4mA */
	UNIPHIER_PIN_DRV_FIXED5,	/* fixed to 5mA */
	UNIPHIER_PIN_DRV_FIXED8,	/* fixed to 8mA */
	UNIPHIER_PIN_DRV_NONE,		/* no support (input only pin) */
};

/* direction of pull register (no pin supports bi-directional pull biasing) */
enum uniphier_pin_pull_dir {
	UNIPHIER_PIN_PULL_UP,		/* pull-up or disabled */
	UNIPHIER_PIN_PULL_DOWN,		/* pull-down or disabled */
	UNIPHIER_PIN_PULL_UP_FIXED,	/* always pull-up */
	UNIPHIER_PIN_PULL_DOWN_FIXED,	/* always pull-down */
	UNIPHIER_PIN_PULL_NONE,		/* no pull register */
};

#define UNIPHIER_PIN_IECTRL(x) \
	(((x) & (UNIPHIER_PIN_IECTRL_MASK)) << (UNIPHIER_PIN_IECTRL_SHIFT))
#define UNIPHIER_PIN_DRVCTRL(x) \
	(((x) & (UNIPHIER_PIN_DRVCTRL_MASK)) << (UNIPHIER_PIN_DRVCTRL_SHIFT))
#define UNIPHIER_PIN_DRV_TYPE(x) \
	(((x) & (UNIPHIER_PIN_DRV_TYPE_MASK)) << (UNIPHIER_PIN_DRV_TYPE_SHIFT))
#define UNIPHIER_PIN_PUPDCTRL(x) \
	(((x) & (UNIPHIER_PIN_PUPDCTRL_MASK)) << (UNIPHIER_PIN_PUPDCTRL_SHIFT))
#define UNIPHIER_PIN_PULL_DIR(x) \
	(((x) & (UNIPHIER_PIN_PULL_DIR_MASK)) << (UNIPHIER_PIN_PULL_DIR_SHIFT))

#define UNIPHIER_PIN_ATTR_PACKED(iectrl, drvctrl, drv_type, pupdctrl, pull_dir)\
				(UNIPHIER_PIN_IECTRL(iectrl) |		\
				 UNIPHIER_PIN_DRVCTRL(drvctrl) |	\
				 UNIPHIER_PIN_DRV_TYPE(drv_type) |	\
				 UNIPHIER_PIN_PUPDCTRL(pupdctrl) |	\
				 UNIPHIER_PIN_PULL_DIR(pull_dir))

static inline unsigned int uniphier_pin_get_iectrl(void *drv_data)
{
	return ((unsigned long)drv_data >> UNIPHIER_PIN_IECTRL_SHIFT) &
						UNIPHIER_PIN_IECTRL_MASK;
}

static inline unsigned int uniphier_pin_get_drvctrl(void *drv_data)
{
	return ((unsigned long)drv_data >> UNIPHIER_PIN_DRVCTRL_SHIFT) &
						UNIPHIER_PIN_DRVCTRL_MASK;
}

static inline unsigned int uniphier_pin_get_drv_type(void *drv_data)
{
	return ((unsigned long)drv_data >> UNIPHIER_PIN_DRV_TYPE_SHIFT) &
						UNIPHIER_PIN_DRV_TYPE_MASK;
}

static inline unsigned int uniphier_pin_get_pupdctrl(void *drv_data)
{
	return ((unsigned long)drv_data >> UNIPHIER_PIN_PUPDCTRL_SHIFT) &
						UNIPHIER_PIN_PUPDCTRL_MASK;
}

static inline unsigned int uniphier_pin_get_pull_dir(void *drv_data)
{
	return ((unsigned long)drv_data >> UNIPHIER_PIN_PULL_DIR_SHIFT) &
						UNIPHIER_PIN_PULL_DIR_MASK;
}

struct uniphier_pinctrl_group {
	const char *name;
	const unsigned *pins;
	unsigned num_pins;
	const int *muxvals;
};

struct uniphier_pinmux_function {
	const char *name;
	const char * const *groups;
	unsigned num_groups;
};

struct uniphier_pinctrl_socdata {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct uniphier_pinctrl_group *groups;
	int groups_count;
	const struct uniphier_pinmux_function *functions;
	int functions_count;
	int (*get_gpio_muxval)(unsigned int pin, unsigned int gpio_offset);
	unsigned int caps;
#define UNIPHIER_PINCTRL_CAPS_PERPIN_IECTRL	BIT(1)
#define UNIPHIER_PINCTRL_CAPS_DBGMUX_SEPARATE	BIT(0)
};

#define UNIPHIER_PINCTRL_PIN(a, b, c, d, e, f, g)			\
{									\
	.number = a,							\
	.name = b,							\
	.drv_data = (void *)UNIPHIER_PIN_ATTR_PACKED(c, d, e, f, g),	\
}

#define __UNIPHIER_PINCTRL_GROUP(grp, mux)				\
	{								\
		.name = #grp,						\
		.pins = grp##_pins,					\
		.num_pins = ARRAY_SIZE(grp##_pins),			\
		.muxvals = mux,						\
	}

#define UNIPHIER_PINCTRL_GROUP(grp)					\
	__UNIPHIER_PINCTRL_GROUP(grp,					\
			grp##_muxvals +					\
			BUILD_BUG_ON_ZERO(ARRAY_SIZE(grp##_pins) !=	\
					  ARRAY_SIZE(grp##_muxvals)))

#define UNIPHIER_PINCTRL_GROUP_GPIO(grp)				\
	__UNIPHIER_PINCTRL_GROUP(grp, NULL)

#define UNIPHIER_PINMUX_FUNCTION(func)					\
	{								\
		.name = #func,						\
		.groups = func##_groups,				\
		.num_groups = ARRAY_SIZE(func##_groups),		\
	}

int uniphier_pinctrl_probe(struct platform_device *pdev,
			   struct uniphier_pinctrl_socdata *socdata);

extern const struct dev_pm_ops uniphier_pinctrl_pm_ops;

#endif /* __PINCTRL_UNIPHIER_H__ */
