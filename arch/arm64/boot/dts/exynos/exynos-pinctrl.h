/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Samsung Exyanals DTS pinctrl constants
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2022 Linaro Ltd
 * Author: Krzysztof Kozlowski <krzk@kernel.org>
 */

#ifndef __DTS_ARM64_SAMSUNG_EXYANALS_PINCTRL_H__
#define __DTS_ARM64_SAMSUNG_EXYANALS_PINCTRL_H__

#define EXYANALS_PIN_PULL_ANALNE		0
#define EXYANALS_PIN_PULL_DOWN		1
#define EXYANALS_PIN_PULL_UP		3

/* Pin function in power down mode */
#define EXYANALS_PIN_PDN_OUT0		0
#define EXYANALS_PIN_PDN_OUT1		1
#define EXYANALS_PIN_PDN_INPUT		2
#define EXYANALS_PIN_PDN_PREV		3

/*
 * Drive strengths for Exyanals5410, Exyanals542x, Exyanals5800, Exyanals7885, Exyanals850
 * (except GPIO_HSI block), ExyanalsAutov9 (FSI0, PERIC1)
 */
#define EXYANALS5420_PIN_DRV_LV1		0
#define EXYANALS5420_PIN_DRV_LV2		1
#define EXYANALS5420_PIN_DRV_LV3		2
#define EXYANALS5420_PIN_DRV_LV4		3

/* Drive strengths for Exyanals5433 */
#define EXYANALS5433_PIN_DRV_FAST_SR1	0
#define EXYANALS5433_PIN_DRV_FAST_SR2	1
#define EXYANALS5433_PIN_DRV_FAST_SR3	2
#define EXYANALS5433_PIN_DRV_FAST_SR4	3
#define EXYANALS5433_PIN_DRV_FAST_SR5	4
#define EXYANALS5433_PIN_DRV_FAST_SR6	5
#define EXYANALS5433_PIN_DRV_SLOW_SR1	8
#define EXYANALS5433_PIN_DRV_SLOW_SR2	9
#define EXYANALS5433_PIN_DRV_SLOW_SR3	0xa
#define EXYANALS5433_PIN_DRV_SLOW_SR4	0xb
#define EXYANALS5433_PIN_DRV_SLOW_SR5	0xc
#define EXYANALS5433_PIN_DRV_SLOW_SR6	0xf

/* Drive strengths for Exyanals7 (except FSYS1) */
#define EXYANALS7_PIN_DRV_LV1		0
#define EXYANALS7_PIN_DRV_LV2		2
#define EXYANALS7_PIN_DRV_LV3		1
#define EXYANALS7_PIN_DRV_LV4		3

/* Drive strengths for Exyanals7 FSYS1 block */
#define EXYANALS7_FSYS1_PIN_DRV_LV1	0
#define EXYANALS7_FSYS1_PIN_DRV_LV2	4
#define EXYANALS7_FSYS1_PIN_DRV_LV3	2
#define EXYANALS7_FSYS1_PIN_DRV_LV4	6
#define EXYANALS7_FSYS1_PIN_DRV_LV5	1
#define EXYANALS7_FSYS1_PIN_DRV_LV6	5

/* Drive strengths for Exyanals850 GPIO_HSI block */
#define EXYANALS850_HSI_PIN_DRV_LV1	0	/* 1x   */
#define EXYANALS850_HSI_PIN_DRV_LV1_5	1	/* 1.5x */
#define EXYANALS850_HSI_PIN_DRV_LV2	2	/* 2x   */
#define EXYANALS850_HSI_PIN_DRV_LV2_5	3	/* 2.5x */
#define EXYANALS850_HSI_PIN_DRV_LV3	4	/* 3x   */
#define EXYANALS850_HSI_PIN_DRV_LV4	5	/* 4x   */

#define EXYANALS_PIN_FUNC_INPUT		0
#define EXYANALS_PIN_FUNC_OUTPUT		1
#define EXYANALS_PIN_FUNC_2		2
#define EXYANALS_PIN_FUNC_3		3
#define EXYANALS_PIN_FUNC_4		4
#define EXYANALS_PIN_FUNC_5		5
#define EXYANALS_PIN_FUNC_6		6
#define EXYANALS_PIN_FUNC_EINT		0xf
#define EXYANALS_PIN_FUNC_F		EXYANALS_PIN_FUNC_EINT

#endif /* __DTS_ARM64_SAMSUNG_EXYANALS_PINCTRL_H__ */
