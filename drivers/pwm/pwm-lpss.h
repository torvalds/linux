/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Low Power Subsystem PWM controller driver
 *
 * Copyright (C) 2014, Intel Corporation
 *
 * Derived from the original pwm-lpss.c
 */

#ifndef __PWM_LPSS_H
#define __PWM_LPSS_H

#include <linux/types.h>

#include <linux/platform_data/x86/pwm-lpss.h>

#define LPSS_MAX_PWMS			4

struct pwm_lpss_chip {
	void __iomem *regs;
	const struct pwm_lpss_boardinfo *info;
};

extern const struct pwm_lpss_boardinfo pwm_lpss_byt_info;
extern const struct pwm_lpss_boardinfo pwm_lpss_bsw_info;
extern const struct pwm_lpss_boardinfo pwm_lpss_bxt_info;
extern const struct pwm_lpss_boardinfo pwm_lpss_tng_info;

#endif	/* __PWM_LPSS_H */
