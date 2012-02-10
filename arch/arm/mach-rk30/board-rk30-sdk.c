/* arch/arm/mach-rk30/board-rk30-sdk.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
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
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>
#include <linux/mmc/host.h>
#include <linux/ion.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
/*set touchscreen different type header*/
#if defined(CONFIG_TOUCHSCREEN_XPT2046_NORMAL_SPI)
#include "../../../drivers/input/touchscreen/xpt2046_ts.h"
#elif defined(CONFIG_TOUCHSCREEN_XPT2046_TSLIB_SPI)
#include "../../../drivers/input/touchscreen/xpt2046_tslib_ts.h"
#elif defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI)
#include "../../../drivers/input/touchscreen/xpt2046_cbn_ts.h"
#endif


/*****************************************************************************************
 * xpt2046 touch panel
 * author: hhb@rock-chips.com
 *****************************************************************************************/
#if defined(CONFIG_TOUCHSCREEN_XPT2046_NORMAL_SPI) || defined(CONFIG_TOUCHSCREEN_XPT2046_TSLIB_SPI)
#define XPT2046_GPIO_INT	RK30_PIN6_PB7 
#define DEBOUNCE_REPTIME  	3


static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 		= 1,
	.swap_xy		= 0,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,
	.pendown_iomux_name = GPIO6B7_TESTCLOCKOUT_NAME,	
	.pendown_iomux_mode = GPIO6B_GPIO6B7,	
	.touch_virtualkey_length = 60,
	.penirq_recheck_delay_usecs = 1,
#if defined(CONFIG_TOUCHSCREEN_480X800)
	.x_min			= 0,
	.x_max			= 480,
	.y_min			= 0,
	.y_max			= 800,
	.touch_ad_top = 3940,
	.touch_ad_bottom = 310,
	.touch_ad_left = 3772,
	.touch_ad_right = 340,
#elif defined(CONFIG_TOUCHSCREEN_800X480)
	.x_min			= 0,
	.x_max			= 800,
	.y_min			= 0,
	.y_max			= 480,
	.touch_ad_top = 2447,
	.touch_ad_bottom = 207,
	.touch_ad_left = 5938,
	.touch_ad_right = 153,
#elif defined(CONFIG_TOUCHSCREEN_320X480)
	.x_min			= 0,
	.x_max			= 320,
	.y_min			= 0,
	.y_max			= 480,
	.touch_ad_top = 3166,
	.touch_ad_bottom = 256,
	.touch_ad_left = 3658,
	.touch_ad_right = 380,
#endif	
};
#elif defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI)
static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 	= 1,
	.swap_xy		= 0,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,
	.pendown_iomux_name = GPIO6B7_TESTCLOCKOUT_NAME,	
	.pendown_iomux_mode = GPIO6B_GPIO6B7,	
	.touch_virtualkey_length = 60,
	.penirq_recheck_delay_usecs = 1,
	
#if defined(CONFIG_TOUCHSCREEN_480X800)
	.x_min			= 0,
	.x_max			= 480,
	.y_min			= 0,
	.y_max			= 800,
	.screen_x = { 70,  410, 70, 410, 240},
	.screen_y = { 50, 50,  740, 740, 400},
	.uncali_x_default = {  3267,  831, 3139, 715, 1845 },
	.uncali_y_default = { 3638,  3664, 564,  591, 2087 },
#elif defined(CONFIG_TOUCHSCREEN_800X480)
	.x_min			= 0,
	.x_max			= 800,
	.y_min			= 0,
	.y_max			= 480,
	.screen_x[5] = { 50, 750,  50, 750, 400};
  	.screen_y[5] = { 40,  40, 440, 440, 240};
	.uncali_x_default[5] = { 438,  565, 3507,  3631, 2105 };
	.uncali_y_default[5] = {  3756,  489, 3792, 534, 2159 };
#elif defined(CONFIG_TOUCHSCREEN_320X480)
	.x_min			= 0,
	.x_max			= 320,
	.y_min			= 0,
	.y_max			= 480,
	.screen_x[5] = { 50, 270,  50, 270, 160}; 
	.screen_y[5] = { 40,  40, 440, 440, 240}; 
	.uncali_x_default[5] = { 812,  3341, 851,  3371, 2183 };
	.uncali_y_default[5] = {  442,  435, 3193, 3195, 2004 };
#endif	
};
#endif

static struct spi_board_info board_spi_devices[] = {
#if defined(CONFIG_TOUCHSCREEN_XPT2046_SPI)
	{
		.modalias	= "xpt2046_ts",
		.chip_select	= 1,// 2,
		.max_speed_hz	= 1 * 1000 * 1000,/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.irq 		= XPT2046_GPIO_INT,
		.platform_data = &xpt2046_info,
	},
#endif

};



static struct platform_device *devices[] __initdata = {
};

// i2c
#ifdef CONFIG_I2C0_RK30
static struct i2c_board_info __initdata i2c0_info[] = {
};
#endif

#ifdef CONFIG_I2C1_RK30
static struct i2c_board_info __initdata i2c1_info[] = {
};
#endif

#ifdef CONFIG_I2C2_RK30
static struct i2c_board_info __initdata i2c2_info[] = {
};
#endif

#ifdef CONFIG_I2C3_RK30
static struct i2c_board_info __initdata i2c3_info[] = {
};
#endif

#ifdef CONFIG_I2C4_RK30
static struct i2c_board_info __initdata i2c4_info[] = {
};
#endif

static void __init rk30_i2c_register_board_info(void)
{
#ifdef CONFIG_I2C0_RK30
	i2c_register_board_info(0, i2c0_info, ARRAY_SIZE(i2c0_info));
#endif
#ifdef CONFIG_I2C1_RK30
	i2c_register_board_info(1, i2c1_info, ARRAY_SIZE(i2c1_info));
#endif
#ifdef CONFIG_I2C2_RK30
	i2c_register_board_info(2, i2c2_info, ARRAY_SIZE(i2c2_info));
#endif
#ifdef CONFIG_I2C3_RK30
	i2c_register_board_info(3, i2c3_info, ARRAY_SIZE(i2c3_info));
#endif
#ifdef CONFIG_I2C4_RK30
	i2c_register_board_info(4, i2c4_info, ARRAY_SIZE(i2c4_info));
#endif
}
//end of i2c

static void __init machine_rk30_board_init(void)
{
	rk30_i2c_register_board_info();
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init rk30_reserve(void)
{
	board_mem_reserved();
}

MACHINE_START(RK30, "RK30board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= rk30_fixup,
	.reserve	= &rk30_reserve,
	.map_io		= rk30_map_io,
	.init_irq	= rk30_init_irq,
	.timer		= &rk30_timer,
	.init_machine	= machine_rk30_board_init,
MACHINE_END
