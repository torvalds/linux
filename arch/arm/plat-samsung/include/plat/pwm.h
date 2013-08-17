/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * SAMSUNG PWM platform data definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __PLAT_SAMSUNG_PWM_H
#define __PLAT_SAMSUNG_PWM_H __FILE__

#include <plat/devs.h>

struct samsung_pwm_platdata {
	unsigned int	prescaler0;
	unsigned int	prescaler1;
};

extern void samsung_pwm_set_platdata(struct samsung_pwm_platdata *pd);

#endif /* __PLAT_SAMSUNG_PWM_H */
