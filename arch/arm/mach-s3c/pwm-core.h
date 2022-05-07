/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2013 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * Samsung PWM controller platform data helpers.
 */

#ifndef __ASM_ARCH_PWM_CORE_H
#define __ASM_ARCH_PWM_CORE_H __FILE__

#include <clocksource/samsung_pwm.h>

#ifdef CONFIG_SAMSUNG_DEV_PWM
extern void samsung_pwm_set_platdata(struct samsung_pwm_variant *pd);
#else
static inline void samsung_pwm_set_platdata(struct samsung_pwm_variant *pd) { }
#endif

#endif /* __ASM_ARCH_PWM_CORE_H */
