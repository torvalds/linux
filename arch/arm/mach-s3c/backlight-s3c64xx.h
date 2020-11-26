/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 */

#ifndef __ASM_PLAT_BACKLIGHT_S3C64XX_H
#define __ASM_PLAT_BACKLIGHT_S3C64XX_H __FILE__

/* samsung_bl_gpio_info - GPIO info for PWM Backlight control
 * @no:		GPIO number for PWM timer out
 * @func:	Special function of GPIO line for PWM timer
 */
struct samsung_bl_gpio_info {
	int no;
	int func;
};

extern void __init samsung_bl_set(struct samsung_bl_gpio_info *gpio_info,
	struct platform_pwm_backlight_data *bl_data);

#endif /* __ASM_PLAT_BACKLIGHT_S3C64XX_H */
