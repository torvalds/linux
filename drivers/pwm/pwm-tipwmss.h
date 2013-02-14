/*
 * TI PWM Subsystem driver
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef __TIPWMSS_H
#define __TIPWMSS_H

#ifdef CONFIG_PWM_TIPWMSS
/* PWM substem clock gating */
#define PWMSS_ECAPCLK_EN	BIT(0)
#define PWMSS_ECAPCLK_STOP_REQ	BIT(1)
#define PWMSS_EPWMCLK_EN	BIT(8)
#define PWMSS_EPWMCLK_STOP_REQ	BIT(9)

#define PWMSS_ECAPCLK_EN_ACK	BIT(0)
#define PWMSS_EPWMCLK_EN_ACK	BIT(8)

extern u16 pwmss_submodule_state_change(struct device *dev, int set);
#else
static inline u16 pwmss_submodule_state_change(struct device *dev, int set)
{
	/* return success status value */
	return 0xFFFF;
}
#endif
#endif	/* __TIPWMSS_H */
