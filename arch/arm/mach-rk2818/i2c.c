/* arch/arm/mach-rk2818/i2c.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <mach/i2c.h>
#include <mach/iomux.h>

#include <mach/irqs.h>
#include "devices.h"

/* i2c0 */
static struct rk2818_i2c_platform_data default_i2c0_data __initdata = { 
	.bus_num    = 0,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.clk_id  = "i2c0",
};
/* i2c1 */
static struct rk2818_i2c_platform_data default_i2c1_data __initdata = { 
	.bus_num    = 1,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.clk_id  = "i2c1",
};

static struct i2c_board_info __initdata board_i2c0_devices[] = {
#if defined (CONFIG_RK1000_CONTROL)
	{
		.type    		= "rk1000_control",
		.addr           = 0x40,
		.flags			= 0,
	},
#endif

#if defined (CONFIG_RK1000_TVOUT)
	{
		.type    		= "rk1000_tvout",
		.addr           = 0x42,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_SND_SOC_RK1000)
	{
		.type    		= "rk1000_i2c_codec",
		.addr           = 0x60,
		.flags			= 0,
	},
#endif
	{}
};
static struct i2c_board_info __initdata board_i2c1_devices[] = {
#if defined (CONFIG_RTC_HYM8563)
	{
		.type    		= "rtc_hym8563",
		.addr           = 0x51,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_FM_QN8006)
	{
		.type    		= "fm_qn8006",
		.addr           = 0x2b, 
		.flags			= 0,
	},
#endif
	{}
};

static void rk2818_i2c0_cfg_gpio(struct platform_device *dev)
{
	rk2818_mux_api_set(GPIOE_I2C0_SEL_NAME, IOMUXA_I2C0);
}

static void rk2818_i2c1_cfg_gpio(struct platform_device *dev)
{
	rk2818_mux_api_set(GPIOE_U1IR_I2C1_NAME, IOMUXA_I2C1);
}


static void __init rk2818_i2c0_set_platdata(struct rk2818_i2c_platform_data *pd)
{
	struct rk2818_i2c_platform_data *npd;

	if (!pd)
		pd = &default_i2c0_data;

	npd = kmemdup(pd, sizeof(struct rk2818_i2c_platform_data), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	else if (!npd->cfg_gpio)
		npd->cfg_gpio = rk2818_i2c0_cfg_gpio;

	rk2818_device_i2c0.dev.platform_data = npd;
}
static void __init rk2818_i2c1_set_platdata(struct rk2818_i2c_platform_data *pd)
{
	struct rk2818_i2c_platform_data *npd;

	if (!pd)
		pd = &default_i2c1_data;

	npd = kmemdup(pd, sizeof(struct rk2818_i2c_platform_data), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	else if (!npd->cfg_gpio)
		npd->cfg_gpio = rk2818_i2c1_cfg_gpio;

	rk2818_device_i2c1.dev.platform_data = npd;
}

void __init rk2818_i2c_board_init(void)
{
	rk2818_i2c0_set_platdata(NULL);
	rk2818_i2c1_set_platdata(NULL);
	i2c_register_board_info(0, board_i2c0_devices,
			ARRAY_SIZE(board_i2c0_devices));
	i2c_register_board_info(1, board_i2c1_devices,
			ARRAY_SIZE(board_i2c1_devices));
}
