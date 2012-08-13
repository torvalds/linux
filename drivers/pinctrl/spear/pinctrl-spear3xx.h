/*
 * Header file for the ST Microelectronics SPEAr3xx pinmux
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PINMUX_SPEAR3XX_H__
#define __PINMUX_SPEAR3XX_H__

#include "pinctrl-spear.h"

/* pad mux declarations */
#define PMX_FIRDA_MASK		(1 << 14)
#define PMX_I2C_MASK		(1 << 13)
#define PMX_SSP_CS_MASK		(1 << 12)
#define PMX_SSP_MASK		(1 << 11)
#define PMX_MII_MASK		(1 << 10)
#define PMX_GPIO_PIN0_MASK	(1 << 9)
#define PMX_GPIO_PIN1_MASK	(1 << 8)
#define PMX_GPIO_PIN2_MASK	(1 << 7)
#define PMX_GPIO_PIN3_MASK	(1 << 6)
#define PMX_GPIO_PIN4_MASK	(1 << 5)
#define PMX_GPIO_PIN5_MASK	(1 << 4)
#define PMX_UART0_MODEM_MASK	(1 << 3)
#define PMX_UART0_MASK		(1 << 2)
#define PMX_TIMER_2_3_MASK	(1 << 1)
#define PMX_TIMER_0_1_MASK	(1 << 0)

extern struct spear_pingroup spear3xx_firda_pingroup;
extern struct spear_pingroup spear3xx_gpio0_pin0_pingroup;
extern struct spear_pingroup spear3xx_gpio0_pin1_pingroup;
extern struct spear_pingroup spear3xx_gpio0_pin2_pingroup;
extern struct spear_pingroup spear3xx_gpio0_pin3_pingroup;
extern struct spear_pingroup spear3xx_gpio0_pin4_pingroup;
extern struct spear_pingroup spear3xx_gpio0_pin5_pingroup;
extern struct spear_pingroup spear3xx_i2c_pingroup;
extern struct spear_pingroup spear3xx_mii_pingroup;
extern struct spear_pingroup spear3xx_ssp_cs_pingroup;
extern struct spear_pingroup spear3xx_ssp_pingroup;
extern struct spear_pingroup spear3xx_timer_0_1_pingroup;
extern struct spear_pingroup spear3xx_timer_2_3_pingroup;
extern struct spear_pingroup spear3xx_uart0_ext_pingroup;
extern struct spear_pingroup spear3xx_uart0_pingroup;

#define SPEAR3XX_COMMON_PINGROUPS		\
	&spear3xx_firda_pingroup,		\
	&spear3xx_gpio0_pin0_pingroup,		\
	&spear3xx_gpio0_pin1_pingroup,		\
	&spear3xx_gpio0_pin2_pingroup,		\
	&spear3xx_gpio0_pin3_pingroup,		\
	&spear3xx_gpio0_pin4_pingroup,		\
	&spear3xx_gpio0_pin5_pingroup,		\
	&spear3xx_i2c_pingroup,			\
	&spear3xx_mii_pingroup,			\
	&spear3xx_ssp_cs_pingroup,		\
	&spear3xx_ssp_pingroup,			\
	&spear3xx_timer_0_1_pingroup,		\
	&spear3xx_timer_2_3_pingroup,		\
	&spear3xx_uart0_ext_pingroup,		\
	&spear3xx_uart0_pingroup

extern struct spear_function spear3xx_firda_function;
extern struct spear_function spear3xx_gpio0_function;
extern struct spear_function spear3xx_i2c_function;
extern struct spear_function spear3xx_mii_function;
extern struct spear_function spear3xx_ssp_cs_function;
extern struct spear_function spear3xx_ssp_function;
extern struct spear_function spear3xx_timer_0_1_function;
extern struct spear_function spear3xx_timer_2_3_function;
extern struct spear_function spear3xx_uart0_ext_function;
extern struct spear_function spear3xx_uart0_function;

#define SPEAR3XX_COMMON_FUNCTIONS		\
	&spear3xx_firda_function,		\
	&spear3xx_gpio0_function,		\
	&spear3xx_i2c_function,			\
	&spear3xx_mii_function,			\
	&spear3xx_ssp_cs_function,		\
	&spear3xx_ssp_function,			\
	&spear3xx_timer_0_1_function,		\
	&spear3xx_timer_2_3_function,		\
	&spear3xx_uart0_ext_function,		\
	&spear3xx_uart0_function

extern struct spear_pinctrl_machdata spear3xx_machdata;

#endif /* __PINMUX_SPEAR3XX_H__ */
