/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Samsung Exynos DTS pinctrl constants
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2022 Linaro Ltd
 * Author: Krzysztof Kozlowski <krzk@kernel.org>
 */

#ifndef __DTS_ARM64_SAMSUNG_EXYNOS_PINCTRL_H__
#define __DTS_ARM64_SAMSUNG_EXYNOS_PINCTRL_H__

#define EXYNOS_PIN_PULL_NONE		0
#define EXYNOS_PIN_PULL_DOWN		1
#define EXYNOS_PIN_PULL_UP		3

/* Pin function in power down mode */
#define EXYNOS_PIN_PDN_OUT0		0
#define EXYNOS_PIN_PDN_OUT1		1
#define EXYNOS_PIN_PDN_INPUT		2
#define EXYNOS_PIN_PDN_PREV		3

/*
 * Drive strengths for Exynos5410, Exynos542x, Exynos5800, Exynos7885, Exynos850
 * (except GPIO_HSI block), ExynosAutov9 (FSI0, PERIC1)
 */
#define EXYNOS5420_PIN_DRV_LV1		0
#define EXYNOS5420_PIN_DRV_LV2		1
#define EXYNOS5420_PIN_DRV_LV3		2
#define EXYNOS5420_PIN_DRV_LV4		3

/* Drive strengths for Exynos5433 */
#define EXYNOS5433_PIN_DRV_FAST_SR1	0
#define EXYNOS5433_PIN_DRV_FAST_SR2	1
#define EXYNOS5433_PIN_DRV_FAST_SR3	2
#define EXYNOS5433_PIN_DRV_FAST_SR4	3
#define EXYNOS5433_PIN_DRV_FAST_SR5	4
#define EXYNOS5433_PIN_DRV_FAST_SR6	5
#define EXYNOS5433_PIN_DRV_SLOW_SR1	8
#define EXYNOS5433_PIN_DRV_SLOW_SR2	9
#define EXYNOS5433_PIN_DRV_SLOW_SR3	0xa
#define EXYNOS5433_PIN_DRV_SLOW_SR4	0xb
#define EXYNOS5433_PIN_DRV_SLOW_SR5	0xc
#define EXYNOS5433_PIN_DRV_SLOW_SR6	0xf

/* Drive strengths for Exynos7 (except FSYS1) */
#define EXYNOS7_PIN_DRV_LV1		0
#define EXYNOS7_PIN_DRV_LV2		2
#define EXYNOS7_PIN_DRV_LV3		1
#define EXYNOS7_PIN_DRV_LV4		3

/* Drive strengths for Exynos7 FSYS1 block */
#define EXYNOS7_FSYS1_PIN_DRV_LV1	0
#define EXYNOS7_FSYS1_PIN_DRV_LV2	4
#define EXYNOS7_FSYS1_PIN_DRV_LV3	2
#define EXYNOS7_FSYS1_PIN_DRV_LV4	6
#define EXYNOS7_FSYS1_PIN_DRV_LV5	1
#define EXYNOS7_FSYS1_PIN_DRV_LV6	5

/* Drive strengths for Exynos850 GPIO_HSI block */
#define EXYNOS850_HSI_PIN_DRV_LV1	0	/* 1x   */
#define EXYNOS850_HSI_PIN_DRV_LV1_5	1	/* 1.5x */
#define EXYNOS850_HSI_PIN_DRV_LV2	2	/* 2x   */
#define EXYNOS850_HSI_PIN_DRV_LV2_5	3	/* 2.5x */
#define EXYNOS850_HSI_PIN_DRV_LV3	4	/* 3x   */
#define EXYNOS850_HSI_PIN_DRV_LV4	5	/* 4x   */

#define EXYNOS_PIN_FUNC_INPUT		0
#define EXYNOS_PIN_FUNC_OUTPUT		1
#define EXYNOS_PIN_FUNC_2		2
#define EXYNOS_PIN_FUNC_3		3
#define EXYNOS_PIN_FUNC_4		4
#define EXYNOS_PIN_FUNC_5		5
#define EXYNOS_PIN_FUNC_6		6
#define EXYNOS_PIN_FUNC_EINT		0xf
#define EXYNOS_PIN_FUNC_F		EXYNOS_PIN_FUNC_EINT

#endif /* __DTS_ARM64_SAMSUNG_EXYNOS_PINCTRL_H__ */
