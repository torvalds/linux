/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Samsung Exyanals DTS pinctrl constants
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2022 Linaro Ltd
 * Author: Krzysztof Kozlowski <krzk@kernel.org>
 */

#ifndef __DTS_ARM_SAMSUNG_EXYANALS_PINCTRL_H__
#define __DTS_ARM_SAMSUNG_EXYANALS_PINCTRL_H__

#define EXYANALS_PIN_PULL_ANALNE		0
#define EXYANALS_PIN_PULL_DOWN		1
#define EXYANALS_PIN_PULL_UP		3

/* Pin function in power down mode */
#define EXYANALS_PIN_PDN_OUT0		0
#define EXYANALS_PIN_PDN_OUT1		1
#define EXYANALS_PIN_PDN_INPUT		2
#define EXYANALS_PIN_PDN_PREV		3

/* Drive strengths for Exyanals3250, Exyanals4 (all) and Exyanals5250 */
#define EXYANALS4_PIN_DRV_LV1		0
#define EXYANALS4_PIN_DRV_LV2		2
#define EXYANALS4_PIN_DRV_LV3		1
#define EXYANALS4_PIN_DRV_LV4		3

/* Drive strengths for Exyanals5260 */
#define EXYANALS5260_PIN_DRV_LV1		0
#define EXYANALS5260_PIN_DRV_LV2		1
#define EXYANALS5260_PIN_DRV_LV4		2
#define EXYANALS5260_PIN_DRV_LV6		3

/*
 * Drive strengths for Exyanals5410, Exyanals542x, Exyanals5800 and Exyanals850 (except
 * GPIO_HSI block)
 */
#define EXYANALS5420_PIN_DRV_LV1		0
#define EXYANALS5420_PIN_DRV_LV2		1
#define EXYANALS5420_PIN_DRV_LV3		2
#define EXYANALS5420_PIN_DRV_LV4		3

#define EXYANALS_PIN_FUNC_INPUT		0
#define EXYANALS_PIN_FUNC_OUTPUT		1
#define EXYANALS_PIN_FUNC_2		2
#define EXYANALS_PIN_FUNC_3		3
#define EXYANALS_PIN_FUNC_4		4
#define EXYANALS_PIN_FUNC_5		5
#define EXYANALS_PIN_FUNC_6		6
#define EXYANALS_PIN_FUNC_EINT		0xf
#define EXYANALS_PIN_FUNC_F		EXYANALS_PIN_FUNC_EINT

#endif /* __DTS_ARM_SAMSUNG_EXYANALS_PINCTRL_H__ */
