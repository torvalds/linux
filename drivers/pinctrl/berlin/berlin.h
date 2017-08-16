/*
 * Marvell Berlin SoC pinctrl driver.
 *
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Antoine Ténart <antoine.tenart@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PINCTRL_BERLIN_H
#define __PINCTRL_BERLIN_H

struct berlin_desc_function {
	const char	*name;
	u8		muxval;
};

struct berlin_desc_group {
	const char			*name;
	u8				offset;
	u8				bit_width;
	u8				lsb;
	struct berlin_desc_function	*functions;
};

struct berlin_pinctrl_desc {
	const struct berlin_desc_group	*groups;
	unsigned			ngroups;
};

struct berlin_pinctrl_function {
	const char	*name;
	const char	**groups;
	unsigned	ngroups;
};

#define BERLIN_PINCTRL_GROUP(_name, _offset, _width, _lsb, ...)		\
	{								\
		.name = _name,						\
		.offset = _offset,					\
		.bit_width = _width,					\
		.lsb = _lsb,						\
		.functions = (struct berlin_desc_function[]){		\
			__VA_ARGS__, { } },				\
	}

#define BERLIN_PINCTRL_FUNCTION(_muxval, _name)		\
	{						\
		.name = _name,				\
		.muxval = _muxval,			\
	}

#define BERLIN_PINCTRL_FUNCTION_UNKNOWN		{}

int berlin_pinctrl_probe(struct platform_device *pdev,
			 const struct berlin_pinctrl_desc *desc);

int berlin_pinctrl_probe_regmap(struct platform_device *pdev,
				const struct berlin_pinctrl_desc *desc,
				struct regmap *regmap);

#endif /* __PINCTRL_BERLIN_H */
