/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Axis ARTPEC-8 SoC device tree pinctrl constants
 *
 * Copyright (c) 2025 Samsung Electronics Co., Ltd.
 *             https://www.samsung.com
 * Copyright (c) 2025  Axis Communications AB.
 *             https://www.axis.com
 */

#ifndef __DTS_ARM64_SAMSUNG_EXYNOS_AXIS_ARTPEC_PINCTRL_H__
#define __DTS_ARM64_SAMSUNG_EXYNOS_AXIS_ARTPEC_PINCTRL_H__

#define ARTPEC_PIN_PULL_NONE		0
#define ARTPEC_PIN_PULL_DOWN		1
#define ARTPEC_PIN_PULL_UP		3

#define ARTPEC_PIN_FUNC_INPUT		0
#define ARTPEC_PIN_FUNC_OUTPUT		1
#define ARTPEC_PIN_FUNC_2		2
#define ARTPEC_PIN_FUNC_3		3
#define ARTPEC_PIN_FUNC_4		4
#define ARTPEC_PIN_FUNC_5		5
#define ARTPEC_PIN_FUNC_6		6
#define ARTPEC_PIN_FUNC_EINT		0xf
#define ARTPEC_PIN_FUNC_F		ARTPEC_PIN_FUNC_EINT

/* Drive strength for ARTPEC */
#define ARTPEC_PIN_DRV_SR1		0x8
#define ARTPEC_PIN_DRV_SR2		0x9
#define ARTPEC_PIN_DRV_SR3		0xa
#define ARTPEC_PIN_DRV_SR4		0xb
#define ARTPEC_PIN_DRV_SR5		0xc
#define ARTPEC_PIN_DRV_SR6		0xd

#endif /* __DTS_ARM64_SAMSUNG_EXYNOS_AXIS_ARTPEC_PINCTRL_H__ */
