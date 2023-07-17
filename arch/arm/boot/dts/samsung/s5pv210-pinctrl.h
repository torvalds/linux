/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Samsung S5PV210 DTS pinctrl constants
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2022 Linaro Ltd
 * Author: Krzysztof Kozlowski <krzk@kernel.org>
 */

#ifndef __DTS_ARM_SAMSUNG_S5PV210_PINCTRL_H__
#define __DTS_ARM_SAMSUNG_S5PV210_PINCTRL_H__

#define S5PV210_PIN_PULL_NONE		0
#define S5PV210_PIN_PULL_DOWN		1
#define S5PV210_PIN_PULL_UP		2

/* Pin function in power down mode */
#define S5PV210_PIN_PDN_OUT0		0
#define S5PV210_PIN_PDN_OUT1		1
#define S5PV210_PIN_PDN_INPUT		2
#define S5PV210_PIN_PDN_PREV		3

#define S5PV210_PIN_DRV_LV1		0
#define S5PV210_PIN_DRV_LV2		2
#define S5PV210_PIN_DRV_LV3		1
#define S5PV210_PIN_DRV_LV4		3

#define S5PV210_PIN_FUNC_INPUT		0
#define S5PV210_PIN_FUNC_OUTPUT		1
#define S5PV210_PIN_FUNC_2		2
#define S5PV210_PIN_FUNC_3		3
#define S5PV210_PIN_FUNC_4		4
#define S5PV210_PIN_FUNC_5		5
#define S5PV210_PIN_FUNC_6		6
#define S5PV210_PIN_FUNC_EINT		0xf
#define S5PV210_PIN_FUNC_F		S5PV210_PIN_FUNC_EINT

#endif /* __DTS_ARM_SAMSUNG_S5PV210_PINCTRL_H__ */
