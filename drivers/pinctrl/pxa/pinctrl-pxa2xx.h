/*
 * Marvell PXA2xx family pin control
 *
 * Copyright (C) 2015 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 */

#ifndef __PINCTRL_PXA_H
#define __PINCTRL_PXA_H

#define PXA_FUNCTION(_dir, _af, _name)				\
	{							\
		.name = _name,					\
		.muxval = (_dir | (_af << 1)),			\
	}

#define PXA_PIN(_pin, funcs...)					\
	{							\
		.pin = _pin,					\
		.functions = (struct pxa_desc_function[]){	\
			funcs, { } },				\
	}

#define PXA_GPIO_PIN(_pin, funcs...)				\
	{							\
		.pin = _pin,					\
		.functions = (struct pxa_desc_function[]){	\
			PXA_FUNCTION(0, 0, "gpio_in"),		\
			PXA_FUNCTION(1, 0, "gpio_out"),		\
			funcs, { } },				\
	}

#define PXA_GPIO_ONLY_PIN(_pin)					\
	{							\
		.pin = _pin,					\
		.functions = (struct pxa_desc_function[]){	\
			PXA_FUNCTION(0, 0, "gpio_in"),		\
			PXA_FUNCTION(1, 0, "gpio_out"),		\
			{ } },					\
	}

#define PXA_PINCTRL_PIN(pin)		\
	PINCTRL_PIN(pin, "P" #pin)

struct pxa_desc_function {
	const char	*name;
	u8		muxval;
};

struct pxa_desc_pin {
	struct pinctrl_pin_desc		pin;
	struct pxa_desc_function	*functions;
};

struct pxa_pinctrl_group {
	const char	*name;
	unsigned	pin;
};

struct pxa_pinctrl_function {
	const char	*name;
	const char	**groups;
	unsigned	ngroups;
};

struct pxa_pinctrl {
	spinlock_t			lock;
	void __iomem			**base_gafr;
	void __iomem			**base_gpdr;
	void __iomem			**base_pgsr;
	struct device			*dev;
	struct pinctrl_desc		desc;
	struct pinctrl_dev		*pctl_dev;
	unsigned			npins;
	const struct pxa_desc_pin	*ppins;
	unsigned			ngroups;
	struct pxa_pinctrl_group	*groups;
	unsigned			nfuncs;
	struct pxa_pinctrl_function	*functions;
	char				*name;
};

int pxa2xx_pinctrl_init(struct platform_device *pdev,
			const struct pxa_desc_pin *ppins, int npins,
			void __iomem *base_gafr[], void __iomem *base_gpdr[],
			void __iomem *base_gpsr[]);

#endif /* __PINCTRL_PXA_H */
