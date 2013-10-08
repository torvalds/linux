/*
 *
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/rk29-pwm-regulator.h>
#include <mach/iomux.h>
#include <linux/gpio.h>
#include <mach/board.h>
#include <plat/pwm.h>

struct ft_pwm_data {
	int	pwm_id;
	char*	gpio_name;
	char*	pwm_iomux_name;
	unsigned int 	pwm_gpio;
	unsigned int 	pwm_iomux_pwm;
	unsigned int	pwm_iomux_gpio;
	int 	pwm_voltage;
	int	suspend_voltage;
	int	min_uV;
	int	max_uV;
	int	coefficient;
	const int	*pwm_voltage_map;
};

int ft_pwm_set_rate(struct ft_pwm_data* pwm, int nHz, u32 rate);
int ft_pwm_regulator_get_voltage(char* name);
int ft_pwm_set_voltage(char *name, int vol);
void ft_pwm_init(void);

