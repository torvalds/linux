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

#include <linux/device.h>
#include <linux/pwm.h>

#define MAX_PWMS			4

struct pwm_lpss_chip {
	struct pwm_chip chip;
	void __iomem *regs;
	const struct pwm_lpss_boardinfo *info;
};

struct pwm_lpss_boardinfo {
	unsigned long clk_rate;
	unsigned int npwm;
	unsigned long base_unit_bits;
	bool bypass;
	/*
	 * On some devices the _PS0/_PS3 AML code of the GPU (GFX0) device
	 * messes with the PWM0 controllers state,
	 */
	bool other_devices_aml_touches_pwm_regs;
};

struct pwm_lpss_chip *pwm_lpss_probe(struct device *dev, struct resource *r,
				     const struct pwm_lpss_boardinfo *info);

#endif	/* __PWM_LPSS_H */
