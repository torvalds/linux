/* linux/arch/arm/plat-s5pc1xx/include/plat/gpio-cfg.h
 *
 * Copyright 2009 Samsung Electronic
 *
 * S5PC1XX Platform - GPIO pin configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* This file contains the necessary definitions to get the basic gpio
 * pin configuration done such as setting a pin to input or output or
 * changing the pull-{up,down} configurations.
 */

#ifndef __GPIO_CFG_S5PC1XX_H
#define __GPIO_CFG_S5PC1XX_H __FILE__

typedef unsigned int __bitwise__ s5p_gpio_drvstr_t;

#define S5P_GPIO_DRVSTR_LV1	0x00
#define S5P_GPIO_DRVSTR_LV2	0x01
#define S5P_GPIO_DRVSTR_LV3	0x10
#define S5P_GPIO_DRVSTR_LV4	0x11

extern s5p_gpio_drvstr_t s5p_gpio_get_drvstr(unsigned int pin, unsigned int off);

extern int s5p_gpio_set_drvstr(unsigned int pin, unsigned int off,
			s5p_gpio_drvstr_t drvstr);

#endif /* __GPIO_CFG_S5PC1XX_H */
