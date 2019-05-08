/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *	   Zhiyong Tao <zhiyong.tao@mediatek.com>
 *	   Hongzhou.Yang <hongzhou.yang@mediatek.com>
 */
#ifndef __PINCTRL_PARIS_H
#define __PINCTRL_PARIS_H

#include <linux/io.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"
#include "../pinmux.h"
#include "mtk-eint.h"
#include "pinctrl-mtk-common-v2.h"

#define MTK_RANGE(_a)		{ .range = (_a), .nranges = ARRAY_SIZE(_a), }

#define MTK_EINT_FUNCTION(_eintmux, _eintnum)				\
	{							\
		.eint_m = _eintmux,					\
		.eint_n = _eintnum,					\
	}

#define MTK_FUNCTION(_val, _name)				\
	{							\
		.muxval = _val,					\
		.name = _name,					\
	}

#define MTK_PIN(_number, _name, _eint, _drv_n, ...) {	\
		.number = _number,			\
		.name = _name,				\
		.eint = _eint,				\
		.drv_n = _drv_n,			\
		.funcs = (struct mtk_func_desc[]){	\
			__VA_ARGS__, { } },				\
	}

#define PINCTRL_PIN_GROUP(name, id)			\
	{						\
		name,					\
		id##_pins,				\
		ARRAY_SIZE(id##_pins),			\
		id##_funcs,				\
	}

int mtk_paris_pinctrl_probe(struct platform_device *pdev,
			    const struct mtk_pin_soc *soc);

extern const struct dev_pm_ops mtk_paris_pinctrl_pm_ops;

#endif /* __PINCTRL_PARIS_H */
