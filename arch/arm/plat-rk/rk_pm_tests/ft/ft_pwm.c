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
#include "ft_pwm.h"
#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

#if defined(CONFIG_SOC_RK3168) || defined(CONFIG_SOC_RK3168M) || defined(CONFIG_ARCH_RK3188) || defined(CONFIG_ARCH_RK3026)
const static int pwm_voltage_map[] = {
	800000,825000,850000, 875000,900000, 925000 ,950000, 975000,1000000, 1025000, 1050000, 1075000, 1100000, 1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000, 1325000, 1350000,1375000
};
#else
const static int pwm_voltage_map[] = {
	950000, 975000,1000000, 1025000, 1050000, 1075000, 1100000, 1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000, 1325000, 1350000, 1375000, 1400000
};
#endif
#if 0
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
#endif
static struct ft_pwm_data ft_pwm[2] = {
	{
		.pwm_id = 0,
		.min_uV = 830000,
		.max_uV = 1380000,
		.coefficient = 550,     //(550 * 10)uV every 1% duty cycle
		.pwm_voltage_map = pwm_voltage_map,
	}, {
		.pwm_id = 1,
		.min_uV = 830000,
		.max_uV = 1380000,
		.coefficient = 550,     //(550 * 10)uV every 1% duty cycle
		.pwm_voltage_map = pwm_voltage_map,
	},
};

int ft_pwm_set_rate(struct ft_pwm_data* pwm, int nHz, u32 rate)
{
	u32 lrc, hrc;
	unsigned long clkrate;

	clkrate = clk_get_rate(clk_get(NULL, "pwm01"));

	printk("%s: out_rate=%d, get clkrate=%lu\n", __func__, nHz, clkrate);
	gpio_request(pwm->pwm_gpio, "ft pwm gpio");
	if (rate == 0) {
		// iomux pwm to gpio
		rk30_mux_api_set(pwm->gpio_name, pwm->pwm_iomux_gpio);
		//disable pull up or down
		gpio_pull_updown(pwm->pwm_gpio, PullDisable);
		// set gpio to low level
		gpio_direction_output(pwm->pwm_gpio, GPIO_LOW);

	} else if (rate < 100) {
		// iomux pwm to gpio
		lrc = clkrate / nHz;
		lrc = lrc >> (1 + (PWM_DIV >> 9));
		lrc = lrc ? lrc : 1;
		hrc = lrc * rate / 100;
		hrc = hrc ? hrc : 1;

		// iomux pwm
		rk30_mux_api_set(pwm->pwm_iomux_name, pwm->pwm_iomux_pwm);

		printk("%s: lrc=%d, hrc=%d\n", __func__, lrc, hrc);
		rk_pwm_setup(pwm->pwm_id, PWM_DIV, hrc, lrc);

	} else if (rate == 100) {
		// iomux pwm to gpio
		rk30_mux_api_set(pwm->gpio_name, pwm->pwm_iomux_gpio);
		//disable pull up or down
		gpio_pull_updown(pwm->pwm_gpio, PullDisable);
		// set gpio to low level
		gpio_direction_output(pwm->pwm_gpio, GPIO_HIGH);

	} else {
		printk("%s:rate error\n",__func__);
		return -1;
	}

	usleep_range(10 * 1000, 10 * 1000);

	return (0);
}

int ft_pwm_regulator_get_voltage(char* name)
{
	DBG("%s: get %s voltage\n", __func__, name);

	return 0;
}

int ft_pwm_set_voltage(char *name, int vol)
{
	int ret = 0;
	// VDD12 = 1.40 - 0.455*D , 其中D为PWM占空比,
	int pwm_value, pwm_id = 0;

	if (strcmp(name, "arm") == 0) {
		pwm_id = 0;

	} else if (strcmp(name, "logic") == 0) {
		pwm_id = 1;

	} else {
		printk("unknown name");
		return -EINVAL;
	}

	printk("%s: name=%s, pwm_id=%d, vol=%d\n", __func__, name, pwm_id, vol);
	/* pwm_value %, coefficient * 1000 */
	pwm_value = (ft_pwm[pwm_id].max_uV - vol) / ft_pwm[pwm_id].coefficient / 10;
	printk("%s: name=%s, pwm_id=%d, duty=%d%%, [min,max]=[%d, %d], coefficient=%d\n",
			__func__, name, pwm_id, pwm_value,
			ft_pwm[pwm_id].min_uV,
			ft_pwm[pwm_id].max_uV,
			ft_pwm[pwm_id].coefficient);
	ret = ft_pwm_set_rate(&ft_pwm[pwm_id], 1000 * 1000, pwm_value);

	if (ret != 0) {
		printk("%s: fail to set pwm rate, pwm_value = %d\n", __func__, pwm_value);
		return -1;
	}

	return 0;


}
static int pwm_mode[] = {PWM0, PWM1, PWM2};
void ft_pwm_init(void)
{
	int i = 0;
	for (i = 0; i < ARRAY_SIZE(ft_pwm); i++) {
		ft_pwm[i].pwm_gpio = iomux_mode_to_gpio(pwm_mode[ft_pwm[i].pwm_id]);
		ft_pwm[i].pwm_iomux_pwm = pwm_mode[ft_pwm[i].pwm_id];
		ft_pwm[i].pwm_iomux_gpio = iomux_switch_gpio_mode(pwm_mode[ft_pwm[i].pwm_id]);
	}
}
