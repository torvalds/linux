/* Copyright (c) 2012, 2017-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DPU_IO_UTIL_H__
#define __DPU_IO_UTIL_H__

#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define DEV_DBG(fmt, args...)   pr_debug(fmt, ##args)
#define DEV_INFO(fmt, args...)  pr_info(fmt, ##args)
#define DEV_WARN(fmt, args...)  pr_warn(fmt, ##args)
#define DEV_ERR(fmt, args...)   pr_err(fmt, ##args)

struct dss_gpio {
	unsigned int gpio;
	unsigned int value;
	char gpio_name[32];
};

enum dss_clk_type {
	DSS_CLK_AHB, /* no set rate. rate controlled through rpm */
	DSS_CLK_PCLK,
};

struct dss_clk {
	struct clk *clk; /* clk handle */
	char clk_name[32];
	enum dss_clk_type type;
	unsigned long rate;
	unsigned long max_rate;
};

struct dss_module_power {
	unsigned int num_gpio;
	struct dss_gpio *gpio_config;
	unsigned int num_clk;
	struct dss_clk *clk_config;
};

int msm_dss_get_clk(struct device *dev, struct dss_clk *clk_arry, int num_clk);
void msm_dss_put_clk(struct dss_clk *clk_arry, int num_clk);
int msm_dss_clk_set_rate(struct dss_clk *clk_arry, int num_clk);
int msm_dss_enable_clk(struct dss_clk *clk_arry, int num_clk, int enable);
int msm_dss_parse_clock(struct platform_device *pdev,
		struct dss_module_power *mp);
#endif /* __DPU_IO_UTIL_H__ */
