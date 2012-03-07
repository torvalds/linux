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
#if defined(CONFIG_SPIM_RK29)
#include "../../../drivers/spi/rk29_spim.h"
#endif
#if defined(CONFIG_ANDROID_TIMED_GPIO)
#include "../../../drivers/staging/android/timed_gpio.h"
#endif

#define RK30_FB0_MEM_SIZE 8*SZ_1M


#if defined(CONFIG_TOUCHSCREEN_GT8XX)
#define TOUCH_RESET_PIN  RK30_PIN4_PD0
#define TOUCH_PWR_PIN    INVALID_GPIO
int goodix_init_platform_hw(void)
{
	int ret;
	printk("goodix_init_platform_hw\n");
	if(TOUCH_PWR_PIN != INVALID_GPIO)
	{
		ret = gpio_request(TOUCH_PWR_PIN, "goodix power pin");
		if(ret != 0){
			gpio_free(TOUCH_PWR_PIN);
			printk("goodix power error\n");
			return -EIO;
		}
		gpio_direction_output(TOUCH_PWR_PIN, 0);
		gpio_set_value(TOUCH_PWR_PIN,GPIO_LOW);
		msleep(100);
	}
	
	if(TOUCH_RESET_PIN != INVALID_GPIO)
	{
		ret = gpio_request(TOUCH_RESET_PIN, "goodix reset pin");
		if(ret != 0){
			gpio_free(TOUCH_RESET_PIN);
			printk("goodix gpio_request error\n");
			return -EIO;
		}
		gpio_direction_output(TOUCH_RESET_PIN, 0);
		gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
		msleep(10);
		gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
		msleep(500);
	}
	return 0;
}

struct goodix_platform_data goodix_info = {
	  .model= 8105,
	  .irq_pin = RK30_PIN4_PC2,
	  .rest_pin  = TOUCH_RESET_PIN,
	  .init_platform_hw = goodix_init_platform_hw,
};
#endif


/*****************************************************************************************
 * xpt2046 touch panel
 * author: hhb@rock-chips.com
 *****************************************************************************************/
#if defined(CONFIG_TOUCHSCREEN_XPT2046_NORMAL_SPI) || defined(CONFIG_TOUCHSCREEN_XPT2046_TSLIB_SPI)
#define XPT2046_GPIO_INT	RK30_PIN4_PC2 
#define DEBOUNCE_REPTIME  	3


static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 		= 1,
	.swap_xy		= 0,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,
	.pendown_iomux_name = GPIO4C2_SMCDATA2_TRACEDATA2_NAME,	
	.pendown_iomux_mode = GPIO4C_GPIO4C2,	
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
	.pendown_iomux_name = GPIO4C2_SMCDATA2_TRACEDATA2_NAME,	
	.pendown_iomux_mode = GPIO4C_GPIO4C2,	
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
#if defined(CONFIG_TOUCHSCREEN_XPT2046_SPI)
static struct rk29xx_spi_chip xpt2046_chip = {
	//.poll_mode = 1,
	.enable_dma = 1,
};
#endif
static struct spi_board_info board_spi_devices[] = {
#if defined(CONFIG_TOUCHSCREEN_XPT2046_SPI)
	{
		.modalias	= "xpt2046_ts",
		.chip_select	= 1,// 2,
		.max_speed_hz	= 1 * 1000 * 800,/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.irq 		= XPT2046_GPIO_INT,
		.platform_data = &xpt2046_info,
		.controller_data = &xpt2046_chip,
	},
#endif

};


/***********************************************************
*	rk30  backlight
************************************************************/
#ifdef CONFIG_BACKLIGHT_RK29_BL
#define PWM_ID            0
#define PWM_MUX_NAME      GPIO0A3_PWM0_NAME
#define PWM_MUX_MODE      GPIO0A_PWM0
#define PWM_MUX_MODE_GPIO GPIO0A_GPIO0A3
#define PWM_GPIO 	  RK30_PIN0_PA3
#define PWM_EFFECT_VALUE  1

#define LCD_DISP_ON_PIN

#ifdef  LCD_DISP_ON_PIN
//#define BL_EN_MUX_NAME    GPIOF34_UART3_SEL_NAME
//#define BL_EN_MUX_MODE    IOMUXB_GPIO1_B34

#define BL_EN_PIN         INVALID_GPIO //?
#define BL_EN_VALUE       GPIO_HIGH
#endif
static int rk29_backlight_io_init(void)
{
	int ret = 0;
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE);
#ifdef  LCD_DISP_ON_PIN
	// rk30_mux_api_set(BL_EN_MUX_NAME, BL_EN_MUX_MODE);

	ret = gpio_request(BL_EN_PIN, NULL);
	if(ret != 0)
	{
		gpio_free(BL_EN_PIN);
	}

	gpio_direction_output(BL_EN_PIN, 0);
	gpio_set_value(BL_EN_PIN, BL_EN_VALUE);
#endif
    return ret;
}

static int rk29_backlight_io_deinit(void)
{
	int ret = 0;
#ifdef  LCD_DISP_ON_PIN
	gpio_free(BL_EN_PIN);
#endif
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE_GPIO);   
    return ret;
}

static int rk29_backlight_pwm_suspend(void)
{
	int ret = 0;
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE_GPIO);
	if (gpio_request(PWM_GPIO, NULL)) {
		printk("func %s, line %d: request gpio fail\n", __FUNCTION__, __LINE__);
		return -1;
	}
	gpio_direction_output(PWM_GPIO, GPIO_LOW);
#ifdef  LCD_DISP_ON_PIN
	gpio_direction_output(BL_EN_PIN, 0);
	gpio_set_value(BL_EN_PIN, !BL_EN_VALUE);
#endif
	return ret;
}

static int rk29_backlight_pwm_resume(void)
{
	gpio_free(PWM_GPIO);
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE);
#ifdef  LCD_DISP_ON_PIN
	msleep(30);
	gpio_direction_output(BL_EN_PIN, 1);
	gpio_set_value(BL_EN_PIN, BL_EN_VALUE);
#endif
	return 0;
}

static struct rk29_bl_info rk29_bl_info = {
    .pwm_id   = PWM_ID,
    .bl_ref   = PWM_EFFECT_VALUE,
    .io_init   = rk29_backlight_io_init,
    .io_deinit = rk29_backlight_io_deinit,
    .pwm_suspend = rk29_backlight_pwm_suspend,
    .pwm_resume = rk29_backlight_pwm_resume,
};


static struct platform_device rk29_device_backlight = {
	.name	= "rk29_backlight",
	.id 	= -1,
        .dev    = {
           .platform_data  = &rk29_bl_info,
        }
};

#endif

/*MMA8452 gsensor*/
#if defined (CONFIG_GS_MMA8452)
#define MMA8452_INT_PIN   RK30_PIN4_PC0

static int mma8452_init_platform_hw(void)
{
	rk30_mux_api_set(GPIO4C0_SMCDATA0_TRACEDATA0_NAME, GPIO4C_GPIO4C0);

	if(gpio_request(MMA8452_INT_PIN,NULL) != 0){
		gpio_free(MMA8452_INT_PIN);
		printk("mma8452_init_platform_hw gpio_request error\n");
		return -EIO;
	}
	gpio_pull_updown(MMA8452_INT_PIN, 1);
	return 0;
}


static struct mma8452_platform_data mma8452_info = {
	.model= 8452,
	.swap_xy = 0,
	.swap_xyz = 1,
	.init_platform_hw= mma8452_init_platform_hw,
	.orientation = { -1, 0, 0, 0, 0, 1, 0, -1, 0},
};
#endif
#if defined (CONFIG_COMPASS_AK8975)
static struct akm8975_platform_data akm8975_info =
{
	.m_layout = 
	{
		{
			{1, 0, 0 },
			{0, -1, 0 },
			{0,	0, -1 },
		},

		{
			{1, 0, 0 },
			{0, 1, 0 },
			{0,	0, 1 },
		},

		{
			{1, 0, 0 },
			{0, 1, 0 },
			{0,	0, 1 },
		},

		{
			{1, 0, 0 },
			{0, 1, 0 },
			{0,	0, 1 },
		},
	}

};

#endif

#if defined(CONFIG_GYRO_L3G4200D)

#include <linux/l3g4200d.h>
#define L3G4200D_INT_PIN  RK30_PIN4_PC3

static int l3g4200d_init_platform_hw(void)
{
	if (gpio_request(L3G4200D_INT_PIN, NULL) != 0) {
		gpio_free(L3G4200D_INT_PIN);
		printk("%s: request l3g4200d int pin error\n", __func__);
		return -EIO;
	}
	gpio_pull_updown(L3G4200D_INT_PIN, 1);
	return 0;
}

static struct l3g4200d_platform_data l3g4200d_info = {
	.fs_range = 1,

	.axis_map_x = 0,
	.axis_map_y = 1,
	.axis_map_z = 2,

	.negate_x = 1,
	.negate_y = 1,
	.negate_z = 0,

	.init = l3g4200d_init_platform_hw,
};

#endif

#ifdef CONFIG_LS_CM3217

#define CM3217_POWER_PIN 	INVALID_GPIO
#define CM3217_IRQ_PIN		INVALID_GPIO
static int cm3217_init_hw(void)
{
#if 0
	if (gpio_request(CM3217_POWER_PIN, NULL) != 0) {
	gpio_free(CM3217_POWER_PIN);
	printk("%s: request cm3217 power pin error\n", __func__);
	return -EIO;
	}
	gpio_pull_updown(CM3217_POWER_PIN, PullDisable);

	if (gpio_request(CM3217_IRQ_PIN, NULL) != 0) {
	gpio_free(CM3217_IRQ_PIN);
	printk("%s: request cm3217 int pin error\n", __func__);
	return -EIO;
	}
	gpio_pull_updown(CM3217_IRQ_PIN, PullDisable);
#endif
	return 0;
}

static void cm3217_exit_hw(void)
{
#if 0
	gpio_free(CM3217_POWER_PIN);
	gpio_free(CM3217_IRQ_PIN);
#endif
	return;
}

struct cm3217_platform_data cm3217_info = {
	.irq_pin = CM3217_IRQ_PIN,
	.power_pin = CM3217_POWER_PIN,
	.init_platform_hw = cm3217_init_hw,
	.exit_platform_hw = cm3217_exit_hw,
};
#endif



#ifdef CONFIG_FB_ROCKCHIP
static struct resource resource_fb[] = {
	[0] = {
		.name  = "fb0 buf",
		.start = 0,
		.end   = 0,//RK30_FB0_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.name  = "ipp buf",  //for rotate
		.start = 0,
		.end   = 0,//RK30_FB0_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.name  = "fb2 buf",
		.start = 0,
		.end   = 0,//RK30_FB0_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device device_fb = {
	.name		  = "rk-fb",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(resource_fb),
	.resource	  = resource_fb,
};
#endif

#if CONFIG_ANDROID_TIMED_GPIO
static struct timed_gpio timed_gpios[] = {
	{
		.name = "vibrator",
		.gpio = RK30_PIN0_PA4,
		.max_timeout = 1000,
		.active_low = 0,
		.adjust_time =20,      //adjust for diff product
	},
};

struct timed_gpio_platform_data rk29_vibrator_info = {
	.num_gpios = 1,
	.gpios = timed_gpios,
};

struct platform_device rk29_device_vibrator ={
	.name = "timed-gpio",
	.id = -1,
	.dev = {
		.platform_data = &rk29_vibrator_info,
		},

};
#endif 


static struct platform_device *devices[] __initdata = {
#ifdef CONFIG_BACKLIGHT_RK29_BL
	&rk29_device_backlight,
#endif	
#ifdef CONFIG_FB_ROCKCHIP
	&device_fb,
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
	&rk29_device_vibrator,
#endif

};

// i2c
#ifdef CONFIG_I2C0_RK30
static struct i2c_board_info __initdata i2c0_info[] = {
#if defined (CONFIG_GS_MMA8452)
	    {
	      .type	      = "gs_mma8452",
	      .addr	      = 0x1c,
	      .flags	      = 0,
	      .irq	      = MMA8452_INT_PIN,
	      .platform_data  = &mma8452_info,
	    },
#endif
#if defined (CONFIG_COMPASS_AK8975)
	{
		.type    	= "ak8975",
		.addr           = 0x0d,
		.flags		= 0,
		.irq		= RK30_PIN4_PC1,
		.platform_data  = &akm8975_info,
	},
#endif
#if defined (CONFIG_GYRO_L3G4200D)
	{
		.type           = "l3g4200d_gryo",
		.addr           = 0x69,
		.flags          = 0,
		.irq            = L3G4200D_INT_PIN,
		.platform_data  = &l3g4200d_info,
	},
#endif

#if defined (CONFIG_SND_SOC_RK1000)
	{
		.type    		= "rk1000_i2c_codec",
		.addr           = 0x60,
		.flags			= 0,
	},
	{
		.type			= "rk1000_control",
		.addr			= 0x40,
		.flags			= 0,
	},	
#endif
};
#endif

#ifdef CONFIG_I2C1_RK30
static struct i2c_board_info __initdata i2c1_info[] = {
};
#endif

#ifdef CONFIG_I2C2_RK30
static struct i2c_board_info __initdata i2c2_info[] = {
#if defined (CONFIG_TOUCHSCREEN_GT8XX)
		    {
				.type	= "Goodix-TS",
				.addr	= 0x55,
				.flags	    =0,
				.irq		=RK30_PIN4_PC2,
				.platform_data = &goodix_info,
		    },
#endif
#if defined (CONFIG_LS_CM3217)
	{
		.type           = "lightsensor",
		.addr           = 0x20,
		.flags          = 0,
		.irq            = CM3217_IRQ_PIN,
		.platform_data  = &cm3217_info,
	},
#endif

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
#ifdef CONFIG_FB_ROCKCHIP
	resource_fb[0].start = board_mem_reserve_add("fb0",RK30_FB0_MEM_SIZE);
	resource_fb[0].end = resource_fb[0].start + RK30_FB0_MEM_SIZE - 1;
	resource_fb[1].start = board_mem_reserve_add("ipp buf",RK30_FB0_MEM_SIZE);
	resource_fb[1].end = resource_fb[1].start + RK30_FB0_MEM_SIZE - 1;
	resource_fb[2].start = board_mem_reserve_add("fb2",RK30_FB0_MEM_SIZE);
	resource_fb[2].end = resource_fb[2].start + RK30_FB0_MEM_SIZE - 1;	
#endif
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
