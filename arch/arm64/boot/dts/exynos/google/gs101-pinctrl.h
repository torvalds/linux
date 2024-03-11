/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Pinctrl binding constants for GS101
 *
 * Copyright 2020-2023 Google LLC
 */

#ifndef __DTS_ARM64_SAMSUNG_EXYNOS_GOOGLE_PINCTRL_GS101_H__
#define __DTS_ARM64_SAMSUNG_EXYNOS_GOOGLE_PINCTRL_GS101_H__

#define GS101_PIN_PULL_NONE		0
#define GS101_PIN_PULL_DOWN		1
#define GS101_PIN_PULL_UP		3

/* Pin function in power down mode */
#define GS101_PIN_PDN_OUT0		0
#define GS101_PIN_PDN_OUT1		1
#define GS101_PIN_PDN_INPUT		2
#define GS101_PIN_PDN_PREV		3

/* GS101 drive strengths */
#define GS101_PIN_DRV_2_5_MA		0
#define GS101_PIN_DRV_5_MA		1
#define GS101_PIN_DRV_7_5_MA		2
#define GS101_PIN_DRV_10_MA		3

#define GS101_PIN_FUNC_INPUT		0
#define GS101_PIN_FUNC_OUTPUT		1
#define GS101_PIN_FUNC_2		2
#define GS101_PIN_FUNC_3		3
#define GS101_PIN_FUNC_EINT		0xf

#endif /* __DTS_ARM64_SAMSUNG_EXYNOS_GOOGLE_PINCTRL_GS101_H__ */
