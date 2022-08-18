/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Tesla FSD DTS pinctrl constants
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2022 Linaro Ltd
 * Author: Krzysztof Kozlowski <krzk@kernel.org>
 */

#ifndef __DTS_ARM64_TESLA_FSD_PINCTRL_H__
#define __DTS_ARM64_TESLA_FSD_PINCTRL_H__

#define FSD_PIN_PULL_NONE		0
#define FSD_PIN_PULL_DOWN		1
#define FSD_PIN_PULL_UP			3

#define FSD_PIN_DRV_LV1			0
#define FSD_PIN_DRV_LV2			2
#define FSD_PIN_DRV_LV3			1
#define FSD_PIN_DRV_LV4			3

#define FSD_PIN_FUNC_INPUT		0
#define FSD_PIN_FUNC_OUTPUT		1
#define FSD_PIN_FUNC_2			2
#define FSD_PIN_FUNC_3			3
#define FSD_PIN_FUNC_4			4
#define FSD_PIN_FUNC_5			5
#define FSD_PIN_FUNC_6			6
#define FSD_PIN_FUNC_EINT		0xf
#define FSD_PIN_FUNC_F			FSD_PIN_FUNC_EINT

#endif /* __DTS_ARM64_TESLA_FSD_PINCTRL_H__ */
