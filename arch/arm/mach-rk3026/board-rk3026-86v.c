/*
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
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/rk_fb.h>
#include <linux/regulator/machine.h>
#include <linux/rfkill-rk.h>
#include <linux/sensor-dev.h>
#include <linux/mfd/tps65910.h>
#include <linux/regulator/act8846.h>
#include <linux/regulator/rk29-pwm-regulator.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/dvfs.h>
#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#if defined(CONFIG_SPIM_RK29)
#include "../../../drivers/spi/rk29_spim.h"
#endif

#ifdef CONFIG_SND_SOC_RK3026
#include "../../../sound/soc/codecs/rk3026_codec.h"
#endif

#if defined(CONFIG_RK_HDMI)
        #include "../../../drivers/video/rockchip/hdmi/rk_hdmi.h"
#endif

#include "board-rk3026-86v-camera.c"

/***********************************************************
*	board config
************************************************************/
//system power on
#define POWER_ON_PIN		RK30_PIN1_PA2   //PWR_HOLD

//touchscreen
//#define TOUCH_RST_PIN		RK2928_PIN0_PD3
//#define TOUCH_RST_VALUE		GPIO_HIGH
//#define TOUCH_PWR_PIN		RK2928_PIN2_PB3
//#define TOUCH_PWR_VALUE		GPIO_LOW
#define TOUCH_INT_PIN		RK2928_PIN1_PB0

//backlight
#define LCD_DISP_ON_PIN
#define BL_PWM			0  // (0 ~ 2)
#define PWM_EFFECT_VALUE  	0
//#define PWM_MUX_NAME      	GPIO0D2_PWM_0_NAME
//#define PWM_MUX_MODE     	GPIO0D_PWM_0
//#define PWM_MUX_MODE_GPIO	GPIO0D_GPIO0D2
//#define PWM_GPIO 	 	RK2928_PIN0_PD2


#define BL_EN_PIN		RK2928_PIN3_PC1
#define BL_EN_VALUE		GPIO_HIGH
#define BL_EN_MUX_NAME  	GPIO3C1_OTG_DRVVBUS_NAME
#define BL_EN_MUX_MODE   	GPIO3C_GPIO3C1


//fb
#define LCD_EN_PIN		RK2928_PIN1_PB3
#define LCD_EN_VALUE		GPIO_LOW
#define LCD_CS_PIN		INVALID_GPIO
#define LCD_CS_VALUE		GPIO_HIGH

//gsensor
#define GS_INT_PIN		RK2928_PIN1_PB2

//sdmmc
//Reference to board-rk3026-tb-sdmmc-config.c

//keyboard
//#define RK31XX_MAINBOARD_V1      //if mainboard is RK31XX_MAINBOARD_V1.0
#define PLAY_ON_PIN		RK30_PIN1_PA4	//wakeup key		

//pwm regulator
#define REG_PWM			1  // (0 ~ 2)

//pmic
#define PMU_INT_PIN		RK30_PIN1_PB1
#define PMU_SLEEP_PIN		RK30_PIN1_PA1

//ion reserve memory
#define ION_RESERVE_SIZE        (80 * SZ_1M)

static int pwm_mode[] = {PWM0, PWM1, PWM2};
static inline int rk_gpio_request(int gpio, int direction, int value, const char *label)
{
	int ret = 0;
	unsigned long flags = 0;

	if(!gpio_is_valid(gpio))
		return 0;

	if(direction == GPIOF_DIR_IN)
		flags = GPIOF_IN;
	else if(value == GPIO_LOW)
		flags = GPIOF_OUT_INIT_LOW;
	else
		flags = GPIOF_OUT_INIT_HIGH;

	ret = gpio_request_one(gpio, flags, label);
	if(ret < 0)
		pr_err("Failed to request '%s'\n", label);

	return ret;
}

static struct spi_board_info board_spi_devices[] = {
};

/***********************************************************
*	touchscreen
************************************************************/
#if defined(CONFIG_TOUCHSCREEN_GSLX680_RK3028)
//#define TOUCH_RESET_PIN RK30_PIN0_PC1
//#define TOUCH_EN_PIN NULL
//#define TOUCH_INT_PIN RK30_PIN0_PB4
	
	int gslx680_init_platform_hw(void)
	{
	#if 0
	       if(gpio_request(TOUCH_RST_PIN,NULL) != 0){
			gpio_free(TOUCH_RST_PIN);
			printk("gslx680_init_platform_hw gpio_request error\n");
			return -EIO;
		}
	 #endif      
		if(gpio_request(TOUCH_INT_PIN,NULL) != 0){
			gpio_free(TOUCH_INT_PIN);
			printk("gslx680_init_platform_hw  gpio_request error\n");
			return -EIO;
		}
	#if 0	
		gpio_direction_output(TOUCH_RST_PIN, TOUCH_RST_VALUE);
		mdelay(10);
		gpio_set_value(TOUCH_RST_PIN,!TOUCH_RST_VALUE);
		mdelay(10);
		gpio_set_value(TOUCH_RST_PIN,TOUCH_RST_VALUE);
		msleep(300);
	#endif	
		return 0;
	
	}
	
	struct ts_hw_data     gslx680_info = {
		//.reset_gpio = TOUCH_RST_PIN,
		.touch_en_gpio = TOUCH_INT_PIN,
		.init_platform_hw = gslx680_init_platform_hw,
	};
#endif


#if defined(CONFIG_TOUCHSCREEN_GT8XX)
static int goodix_init_platform_hw(void)
{
	int ret  = 0;

	ret = rk_gpio_request(TOUCH_PWR_PIN, GPIOF_DIR_OUT, TOUCH_PWR_VALUE, "touch_pwr");
	if(ret < 0)
		return ret; 
	msleep(100);

	ret = rk_gpio_request(TOUCH_RST_PIN, GPIOF_DIR_OUT, TOUCH_RST_VALUE, "touch_rst");
	if(ret < 0)
		return ret; 
	msleep(100);

	return 0;
}

struct goodix_platform_data goodix_info = {
	.model = 8105,
	.irq_pin = TOUCH_INT_PIN,
	.rest_pin = TOUCH_RST_PIN,
	.init_platform_hw = goodix_init_platform_hw,
};
#endif

/***********************************************************
*	rk30  backlight
************************************************************/
#ifdef CONFIG_BACKLIGHT_RK29_BL
static int rk29_backlight_io_init(void)
{
	int ret = 0;

	iomux_set(pwm_mode[BL_PWM]);
	//ret = rk_gpio_request(RK30_PIN0_PD2, GPIOF_DIR_OUT, GPIO_LOW, "PWM");
#ifdef  LCD_DISP_ON_PIN
	ret = rk_gpio_request(BL_EN_PIN, GPIOF_DIR_OUT, BL_EN_VALUE, "bl_en");
	if(ret < 0)
		return ret;
#endif
	return 0;
}

static int rk29_backlight_io_deinit(void)
{
	int pwm_gpio;
#ifdef  LCD_DISP_ON_PIN
	gpio_free(BL_EN_PIN);
#endif
	pwm_gpio = iomux_mode_to_gpio(pwm_mode[BL_PWM]);
	return rk_gpio_request(BL_EN_PIN, GPIOF_DIR_OUT, GPIO_LOW, "BL_PWM");
}

static int rk29_backlight_pwm_suspend(void)
{
	int ret, pwm_gpio = iomux_mode_to_gpio(pwm_mode[BL_PWM]);

	ret = rk_gpio_request(pwm_gpio, GPIOF_DIR_OUT, GPIO_LOW, "BL_PWM");
	if(ret < 0)
		return ret;
#ifdef  LCD_DISP_ON_PIN
	gpio_direction_output(BL_EN_PIN, !BL_EN_VALUE);
#endif
	return ret;
}

static int rk29_backlight_pwm_resume(void)
{
	int pwm_gpio = iomux_mode_to_gpio(pwm_mode[BL_PWM]);

	gpio_free(pwm_gpio);
	iomux_set(pwm_mode[BL_PWM]);
#ifdef  LCD_DISP_ON_PIN
	msleep(30);
	gpio_direction_output(BL_EN_PIN, BL_EN_VALUE);
#endif
	return 0;
}

static struct rk29_bl_info rk29_bl_info = {
	.pwm_id = BL_PWM,
	.min_brightness=75,
	.max_brightness=255,
	.brightness_mode = BRIGHTNESS_MODE_CONIC,
	.bl_ref = PWM_EFFECT_VALUE,
	.io_init = rk29_backlight_io_init,
	.io_deinit = rk29_backlight_io_deinit,
	.pwm_suspend = rk29_backlight_pwm_suspend,
	.pwm_resume = rk29_backlight_pwm_resume,
};

static struct platform_device rk29_device_backlight = {
	.name	= "rk29_backlight",
	.id 	= -1,
	.dev	= {
		.platform_data  = &rk29_bl_info,
	}
};
#endif

/***********************************************************
*	fb
************************************************************/
#ifdef CONFIG_FB_ROCKCHIP
static int rk_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{
	int ret = 0;

	ret = rk_gpio_request(LCD_CS_PIN, GPIOF_DIR_OUT, LCD_CS_VALUE, "lcd_cs");
	if(ret < 0)
		return ret;

	return rk_gpio_request(LCD_EN_PIN, GPIOF_DIR_OUT, LCD_EN_VALUE, "lcd_en");
}

static int rk_fb_io_disable(void)
{
	gpio_set_value(LCD_CS_PIN, !LCD_CS_VALUE);
	gpio_set_value(LCD_EN_PIN, !LCD_EN_VALUE);

	return 0;
}

static int rk_fb_io_enable(void)
{
	gpio_set_value(LCD_CS_PIN, LCD_CS_VALUE);
	gpio_set_value(LCD_EN_PIN, LCD_EN_VALUE);

	return 0;
}

#if defined(CONFIG_LCDC0_RK3066B) || defined(CONFIG_LCDC0_RK3188)
struct rk29fb_info lcdc0_screen_info = {
#if defined(CONFIG_RK_HDMI) && defined(CONFIG_HDMI_SOURCE_LCDC0) && defined(CONFIG_DUAL_LCDC_DUAL_DISP_IN_KERNEL)
	.prop	   = EXTEND,	//extend display device
	.io_init    = NULL,
	.io_disable = NULL,
	.io_enable = NULL,
	.set_screen_info = hdmi_init_lcdc,
#else
	.prop	   = PRMRY,		//primary display device
	.io_init   = rk_fb_io_init,
	.io_disable = rk_fb_io_disable,
	.io_enable = rk_fb_io_enable,
	.set_screen_info = set_lcd_info,
#endif
};
#endif

#if defined(CONFIG_LCDC1_RK3066B) || defined(CONFIG_LCDC1_RK3188)
struct rk29fb_info lcdc1_screen_info = {
#if defined(CONFIG_RK_HDMI) && defined(CONFIG_HDMI_SOURCE_LCDC1) && defined(CONFIG_DUAL_LCDC_DUAL_DISP_IN_KERNEL)
	.prop	   = EXTEND,	//extend display device
	.io_init    = NULL,
	.io_disable = NULL,
	.io_enable = NULL,
	.set_screen_info = hdmi_init_lcdc,
#else
	.prop	   = PRMRY,		//primary display device
	.io_init   = rk_fb_io_init,
	.io_disable = rk_fb_io_disable,
	.io_enable = rk_fb_io_enable,
	.set_screen_info = set_lcd_info,
#endif
};
#endif

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
	.name		= "rk-fb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resource_fb),
	.resource	= resource_fb,
};
#endif

#if defined(CONFIG_LCDC0_RK3066B) || defined(CONFIG_LCDC0_RK3188)
static struct resource resource_lcdc0[] = {
	[0] = {
		.name  = "lcdc0 reg",
		.start = RK3026_LCDC0_PHYS,
		.end   = RK3026_LCDC0_PHYS + RK3026_LCDC0_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	
	[1] = {
		.name  = "lcdc0 irq",
		.start = IRQ_LCDC,
		.end   = IRQ_LCDC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device device_lcdc0 = {
	.name		  = "rk30-lcdc",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(resource_lcdc0),
	.resource	  = resource_lcdc0,
	.dev 		= {
		.platform_data = &lcdc0_screen_info,
	},
};
#endif

#if defined(CONFIG_LCDC1_RK3066B) || defined(CONFIG_LCDC1_RK3188)
static struct resource resource_lcdc1[] = {
	[0] = {
		.name  = "lcdc1 reg",
		.start = RK3026_LCDC1_PHYS,
		.end   = RK3026_LCDC1_PHYS + RK3026_LCDC1_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.name  = "lcdc1 irq",
		.start = IRQ_LCDC1,
		.end   = IRQ_LCDC1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device device_lcdc1 = {
	.name		  = "rk30-lcdc",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(resource_lcdc1),
	.resource	  = resource_lcdc1,
	.dev 		= {
		.platform_data = &lcdc1_screen_info,
	},
};
#endif

static int rk_platform_add_display_devices(void)
{
	struct platform_device *fb = NULL;  //fb
	struct platform_device *lcdc0 = NULL; //lcdc0
	struct platform_device *lcdc1 = NULL; //lcdc1
	struct platform_device *bl = NULL; //backlight
#ifdef CONFIG_FB_ROCKCHIP
	fb = &device_fb;
#endif

#if defined(CONFIG_LCDC0_RK3066B) || defined(CONFIG_LCDC0_RK3188)
	lcdc0 = &device_lcdc0,
#endif

#if defined(CONFIG_LCDC1_RK3066B) || defined(CONFIG_LCDC1_RK3188)
	lcdc1 = &device_lcdc1,
#endif

#ifdef CONFIG_BACKLIGHT_RK29_BL
	bl = &rk29_device_backlight,
#endif
	__rk_platform_add_display_devices(fb,lcdc0,lcdc1,bl);

	return 0;
}


/***********************************************************
*	gsensor
************************************************************/
// mma 8452 gsensor
#if defined (CONFIG_GS_MMA8452)
#define MMA8452_INT_PIN GS_INT_PIN
static int mma8452_init_platform_hw(void)
{
	return 0;
}

static struct sensor_platform_data mma8452_info = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
        .init_platform_hw = mma8452_init_platform_hw,
        .orientation = {-1, 0, 0, 0, -1, 0, 0, 0, 1},
};
#endif

// lsm303d gsensor
#if defined (CONFIG_GS_LSM303D)
#define LSM303D_INT_PIN GS_INT_PIN
static int lms303d_init_platform_hw(void)
{
	return 0;
}

static struct sensor_platform_data lms303d_info = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
        .init_platform_hw = lms303d_init_platform_hw,
        .orientation = {-1, 0, 0, 0, -1, 0, 0, 0, 1},
};
#endif


/*MMA7660 gsensor*/
#if defined (CONFIG_GS_MMA7660)
#define MMA7660_INT_PIN   GS_INT_PIN

static int mma7660_init_platform_hw(void)
{
	//rk30_mux_api_set(GPIO1B2_SPI_RXD_UART1_SIN_NAME, GPIO1B_GPIO1B2);

	return 0;
}

static struct sensor_platform_data mma7660_info = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
    .init_platform_hw = mma7660_init_platform_hw,
#ifndef CONFIG_MFD_RK616
	#ifdef CONFIG_TOUCHSCREEN_GSLX680_RK3168
	.orientation = {-1, 0, 0, 0, -1, 0, 0, 0, 1},
	#else
    .orientation = {0, -1, 0, -1, 0, 0, 0, 0, -1},
    #endif  
#else
	.orientation = {1, 0, 0, 0, -1, 0, 0, 0, -1},
#endif	
};
#endif


#if defined (CONFIG_GS_MXC6225)
#define MXC6225_INT_PIN   GS_INT_PIN

static int mxc6225_init_platform_hw(void)
{
        return 0;
}

static struct sensor_platform_data mxc6225_info = {
        .type = SENSOR_TYPE_ACCEL,
        .irq_enable = 0,
        .poll_delay_ms = 30,
        .init_platform_hw = mxc6225_init_platform_hw,
        .orientation = { 0, -1, 0, 1, 0, 0, 0, 0, 0},
};
#endif
#if defined (CONFIG_GS_LIS3DH)
#define LIS3DH_INT_PIN   GS_INT_PIN
	
	static int lis3dh_init_platform_hw(void)
	{
	
		return 0;
	}
	
	static struct sensor_platform_data lis3dh_info = {
		.type = SENSOR_TYPE_ACCEL,
		.irq_enable = 1,
		.poll_delay_ms = 30,
		.init_platform_hw = lis3dh_init_platform_hw,
		.orientation = {1, 0, 0, 0, 1, 0, 0, 0, 1},
	};
#endif

#if defined (CONFIG_COMPASS_AK8975)
static struct sensor_platform_data akm8975_info =
{
	.type = SENSOR_TYPE_COMPASS,
	.irq_enable = 1,
	.poll_delay_ms = 30,
	.m_layout = 
	{
		{
			{1, 0, 0},
			{0, 1, 0},
			{0, 0, 1},
		},

		{
			{1, 0, 0},
			{0, 1, 0},
			{0, 0, 1},
		},

		{
			{1, 0, 0},
			{0, 1, 0},
			{0, 0, 1},
		},

		{
			{1, 0, 0},
			{0, 1, 0},
			{0, 0, 1},
		},
	}
};

#endif

#if defined (CONFIG_COMPASS_AK8963)
static struct sensor_platform_data akm8963_info =
{
       .type = SENSOR_TYPE_COMPASS,
       .irq_enable = 1,
       .poll_delay_ms = 30,
       .m_layout = 
       {
               {
                       {0, 1, 0},
                       {1, 0, 0},
                       {0, 0, -1},
               },

               {
                       {1, 0, 0},
                       {0, 1, 0},
                       {0, 0, 1},
               },

               {
                       {0, -1, 0},
                       {-1, 0, 0},
                       {0, 0, -1},
               },

               {
                       {1, 0, 0},
                       {0, 1, 0},
                       {0, 0, 1},
               },
       }
};

#endif


#if defined(CONFIG_GYRO_L3G4200D)

#include <linux/l3g4200d.h>
#define L3G4200D_INT_PIN  RK30_PIN0_PB4

static int l3g4200d_init_platform_hw(void)
{
	return 0;
}

static struct sensor_platform_data l3g4200d_info = {
	.type = SENSOR_TYPE_GYROSCOPE,
	.irq_enable = 1,
	.poll_delay_ms = 30,
	.orientation = {0, 1, 0, -1, 0, 0, 0, 0, 1},
	.init_platform_hw = l3g4200d_init_platform_hw,
	.x_min = 40,//x_min,y_min,z_min = (0-100) according to hardware
	.y_min = 40,
	.z_min = 20,
};

#endif

#ifdef CONFIG_LS_CM3217
static struct sensor_platform_data cm3217_info = {
	.type = SENSOR_TYPE_LIGHT,
	.irq_enable = 0,
	.poll_delay_ms = 500,
};

#endif

/***********************************************************
*	keyboard
************************************************************/
#include <plat/key.h>

static struct rk29_keys_button key_button[] = {
        {
                .desc   = "play",
                .code   = KEY_POWER,
                .gpio   = PLAY_ON_PIN,
                .active_low = PRESS_LEV_LOW,
                .wakeup = 1,
        },
/* disable adc keyboard,
 * because rk280a adc reference voltage is 3.3V, but
 * rk30xx mainbord key's supply voltage is 2.5V and
 * rk31xx mainbord key's supply voltage is 1.8V.
 */
#if 0 
#ifdef RK31XX_MAINBOARD_V1
        {
                .desc   = "vol-",
                .code   = KEY_VOLUMEDOWN,
		.adc_value      = 744,
                .gpio   = INVALID_GPIO,
                .active_low = PRESS_LEV_LOW,
        },
        {
                .desc   = "play",
                .code   = KEY_POWER,
                .gpio   = PLAY_ON_PIN,
                .active_low = PRESS_LEV_LOW,
                .wakeup = 1,
        },
        {
                .desc   = "vol+",
                .code   = KEY_VOLUMEUP,
                .adc_value      = 558,
                .gpio = INVALID_GPIO,
                .active_low = PRESS_LEV_LOW,
        },
	{
                .desc   = "menu",
                .code   = EV_MENU,
                .adc_value      = 1,
                .gpio = INVALID_GPIO,
                .active_low = PRESS_LEV_LOW,
        },
        {
                .desc   = "home",
                .code   = KEY_HOME,
                .adc_value      = 354,
                .gpio = INVALID_GPIO,
                .active_low = PRESS_LEV_LOW,
        },
        {
                .desc   = "esc",
                .code   = KEY_BACK,
                .adc_value      = 169,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
#else
        {
                .desc   = "vol-",
                .code   = KEY_VOLUMEDOWN,
		.adc_value      = 900,
                .gpio   = INVALID_GPIO,
                .active_low = PRESS_LEV_LOW,
        },
        {
                .desc   = "play",
                .code   = KEY_POWER,
                .gpio   = PLAY_ON_PIN,
                .active_low = PRESS_LEV_LOW,
                .wakeup = 1,
        },
        {
                .desc   = "vol+",
                .code   = KEY_VOLUMEUP,
                .adc_value      = 1,
                .gpio = INVALID_GPIO,
                .active_low = PRESS_LEV_LOW,
        },
	{
                .desc   = "menu",
                .code   = EV_MENU,
                .adc_value      = 133,
                .gpio = INVALID_GPIO,
                .active_low = PRESS_LEV_LOW,
        },
        {
                .desc   = "home",
                .code   = KEY_HOME,
                .adc_value      = 550,
                .gpio = INVALID_GPIO,
                .active_low = PRESS_LEV_LOW,
        },
        {
                .desc   = "esc",
                .code   = KEY_BACK,
                .adc_value      = 333,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "camera",
		.code	= KEY_CAMERA,
		.adc_value	= 742,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
#endif
#endif
};

struct rk29_keys_platform_data rk29_keys_pdata = {
	.buttons	= key_button,
	.nbuttons	= ARRAY_SIZE(key_button),
	.chn	= 1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};
/***********************************************************
*	usb wifi
************************************************************/
#if defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) 

static void rkusb_wifi_power(int on) {
	int ret=0;
	struct regulator *ldo = NULL;
	printk("hjc:%s[%d],on=%d\n",__func__,__LINE__,on);
#if defined(CONFIG_MFD_TPS65910)	
	if (pmic_is_tps65910() )
		ldo = regulator_get(NULL, "vmmc");  //vccio_wl
#endif
#if defined(CONFIG_REGULATOR_ACT8931)
	if(pmic_is_act8931() )
		ldo = regulator_get(NULL, "act_ldo4");  //vccio_wl
#endif	
	
	if(!on) {
		
		regulator_set_voltage(ldo, 3000000, 3000000);
		ret = regulator_enable(ldo);
		if(ret != 0){
			printk("faild to enable vmmc\n");
		}
		printk("%s: vccio_wl enable\n", __func__);
	} else {
		printk("%s: vccio_wl disable\n", __func__);
		regulator_disable(ldo);
		if(ret != 0){
			printk("faild to disable vmmc\n");
		}
	}	
	regulator_put(ldo);
	udelay(100);
}

#endif



/***********************************************************
*	sdmmc
************************************************************/
#ifdef CONFIG_SDMMC_RK29
#include "board-rk3026-86v-sdmmc-config.c"
#include "../plat-rk/rk-sdmmc-ops.c"
#include "../plat-rk/rk-sdmmc-wifi.c"
#endif //endif ---#ifdef CONFIG_SDMMC_RK29

#ifdef CONFIG_SDMMC0_RK29
#define CONFIG_SDMMC0_USE_DMA
static int rk29_sdmmc0_cfg_gpio(void)
{
	rk29_sdmmc_set_iomux(0, 0xFFFF);
	#if defined(CONFIG_SDMMC0_RK29_SDCARD_DET_FROM_GPIO)
        iomux_set_gpio_mode(iomux_gpio_to_mode(RK29SDK_SD_CARD_DETECT_N));
    	#else
        iomux_set(MMC0_DETN);
    	#endif	

	#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
	gpio_request(SDMMC0_WRITE_PROTECT_PIN, "sdmmc-wp");
	gpio_direction_input(SDMMC0_WRITE_PROTECT_PIN);
	#endif
	return 0;
}

struct rk29_sdmmc_platform_data default_sdmmc0_data = {
	.host_ocr_avail =
	    (MMC_VDD_25_26 | MMC_VDD_26_27 | MMC_VDD_27_28 | MMC_VDD_28_29 |
	     MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33 |
	     MMC_VDD_33_34 | MMC_VDD_34_35 | MMC_VDD_35_36),
	.host_caps =
	    (MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
	.io_init = rk29_sdmmc0_cfg_gpio,

	.set_iomux = rk29_sdmmc_set_iomux,

	.dma_name = "sd_mmc",
#ifdef CONFIG_SDMMC0_USE_DMA
	.use_dma = 1,
#else
	.use_dma = 0,
#endif

#if defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC) && defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
	.status = rk29sdk_wifi_mmc0_status,
	.register_status_notify = rk29sdk_wifi_mmc0_status_register,
#endif

#if defined(RK29SDK_SD_CARD_PWR_EN) || (INVALID_GPIO != RK29SDK_SD_CARD_PWR_EN)
	.power_en = RK29SDK_SD_CARD_PWR_EN,
	.power_en_level = RK29SDK_SD_CARD_PWR_EN_LEVEL,
#else
	.power_en = INVALID_GPIO,
	.power_en_level = GPIO_LOW,
#endif    
	.enable_sd_wakeup = 0,

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
	.write_prt = SDMMC0_WRITE_PROTECT_PIN,
	.write_prt_enalbe_level = SDMMC0_WRITE_PROTECT_ENABLE_VALUE;
#else
	.write_prt = INVALID_GPIO,
#endif

	.det_pin_info = {    
    		#if defined(RK29SDK_SD_CARD_DETECT_N) || (INVALID_GPIO != RK29SDK_SD_CARD_DETECT_N)  
        	.io             = RK29SDK_SD_CARD_DETECT_N, //INVALID_GPIO,
        	.enable         = RK29SDK_SD_CARD_INSERT_LEVEL,
    		#else
        	.io             = INVALID_GPIO,
        	.enable         = GPIO_LOW,
    		#endif    
    	}, 

};
#endif // CONFIG_SDMMC0_RK29

#ifdef CONFIG_SDMMC1_RK29
#define CONFIG_SDMMC1_USE_DMA
static int rk29_sdmmc1_cfg_gpio(void)
{
#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
	gpio_request(SDMMC1_WRITE_PROTECT_PIN, "sdio-wp");
	gpio_direction_input(SDMMC1_WRITE_PROTECT_PIN);
#endif
	return 0;
}

struct rk29_sdmmc_platform_data default_sdmmc1_data = {
	.host_ocr_avail =
	    (MMC_VDD_25_26 | MMC_VDD_26_27 | MMC_VDD_27_28 | MMC_VDD_28_29 |
	     MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33 |
	     MMC_VDD_33_34),

#if !defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
	.host_caps = (MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ |
		      MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
#else
	.host_caps =
	    (MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
#endif

	.io_init = rk29_sdmmc1_cfg_gpio,

	.set_iomux = rk29_sdmmc_set_iomux,

	.dma_name = "sdio",
#ifdef CONFIG_SDMMC1_USE_DMA
	.use_dma = 1,
#else
	.use_dma = 0,
#endif

#if defined(CONFIG_WIFI_CONTROL_FUNC) || defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC)
	.status = rk29sdk_wifi_status,
	.register_status_notify = rk29sdk_wifi_status_register,
#endif

#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
	.write_prt = SDMMC1_WRITE_PROTECT_PIN,
	.write_prt_enalbe_level = SDMMC1_WRITE_PROTECT_ENABLE_VALUE;
#else
	.write_prt = INVALID_GPIO,
#endif

    #if defined(CONFIG_RK29_SDIO_IRQ_FROM_GPIO)
	.sdio_INT_gpio = RK29SDK_WIFI_SDIO_CARD_INT,
    #endif

	.det_pin_info = {    
	#if defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
		#if defined(RK29SDK_SD_CARD_DETECT_N) || (INVALID_GPIO != RK29SDK_SD_CARD_DETECT_N)  
        	.io             = RK29SDK_SD_CARD_DETECT_N,
     		#else
         	.io             = INVALID_GPIO,
		#endif   

        	.enable         = RK29SDK_SD_CARD_INSERT_LEVEL,
	#else
        	.io             = INVALID_GPIO,
        	.enable         = GPIO_LOW,
	#endif
	},
	.enable_sd_wakeup = 0,
};
#endif //endif--#ifdef CONFIG_SDMMC1_RK29



/***********************************************************
*	ion
************************************************************/
#ifdef CONFIG_ION
static struct ion_platform_data rk30_ion_pdata = {
	.nr = 1,
	.heaps = {
		{
			.type = ION_HEAP_TYPE_CARVEOUT,
			.id = ION_NOR_HEAP_ID,
			.name = "norheap",
			.size = ION_RESERVE_SIZE,
		}
	},
};

static struct platform_device device_ion = {
	.name = "ion-rockchip",
	.id = 0,
	.dev = {
		.platform_data = &rk30_ion_pdata,
	},
};
#endif

/***********************************************************
*	pwm regulator
************************************************************/
#ifdef CONFIG_RK30_PWM_REGULATOR
static int pwm_voltage_map[] = {
	800000,  825000,  850000,  875000,  900000,  925000 ,
	950000,  975000,  1000000, 1025000, 1050000, 1075000, 
	1100000, 1125000, 1150000, 1175000, 1200000, 1225000, 
	1250000, 1275000, 1300000, 1325000, 1350000, 1375000
};

static struct regulator_consumer_supply pwm_dcdc1_consumers[] = {
	{
		.supply = "vdd_core",
	}
};

struct regulator_init_data pwm_regulator_init_dcdc[1] = {
	{
		.constraints = {
			.name = "PWM_DCDC1",
			.min_uV = 600000,
			.max_uV = 1800000,      //0.6-1.8V
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(pwm_dcdc1_consumers),
		.consumer_supplies = pwm_dcdc1_consumers,
	},
};

static struct pwm_platform_data pwm_regulator_info[1] = {
	{
		.pwm_id = REG_PWM,
		.pwm_voltage = 1200000,
		.suspend_voltage = 1050000,
		.min_uV = 950000,
		.max_uV = 1400000,
		.coefficient = 504,     //50.4%
		.pwm_voltage_map = pwm_voltage_map,
		.init_data      = &pwm_regulator_init_dcdc[0],
	},
};
struct platform_device pwm_regulator_device[1] = {
	{
		.name = "pwm-voltage-regulator",
		.id = 0,
		.dev            = {
			.platform_data = &pwm_regulator_info[0],
		}
	},
};

static void pwm_regulator_init(void)
{
	pwm_regulator_info[0].pwm_gpio = iomux_mode_to_gpio(pwm_mode[REG_PWM]);
	pwm_regulator_info[0].pwm_iomux_pwm = pwm_mode[REG_PWM];
	pwm_regulator_info[0].pwm_iomux_gpio = iomux_switch_gpio_mode(pwm_mode[REG_PWM]);
}
#endif

int __sramdata pwm_iomux, pwm_do, pwm_dir, pwm_en;
#define grf_readl(offset)       readl_relaxed(RK30_GRF_BASE + offset)
#define grf_writel(v, offset)   do { writel_relaxed(v, RK30_GRF_BASE + offset); dsb(); } while (0)

#define GPIO0_D2_OFFSET		10
void __sramfunc rk30_pwm_logic_suspend_voltage(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR
	#if 0
	/* pwm0: GPIO0_D2, pwm1: GPIO0_D3, pwm2: GPIO0_D4 */
	int off = GPIO0_D2_OFFSET + REG_PWM;

	sram_udelay(10000);
	pwm_iomux = grf_readl(GRF_GPIO0D_IOMUX);
	pwm_dir = grf_readl(GRF_GPIO0H_DIR);
	pwm_do = grf_readl(GRF_GPIO0H_DO);
	pwm__en = grf_readl(GRF_GPIO0H_EN);

	grf_writel((1<<(2 * off), GRF_GPIO0D_IOMUX);
	grf_writel((1<<(16 + off))|(1<<off), GRF_GPIO0H_DIR);
	grf_writel((1<<(16 + off))|(1<<off), GRF_GPIO0H_DO);
	#endif
#endif
}

void __sramfunc rk30_pwm_logic_resume_voltage(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR
	#if 0
	/* pwm0: GPIO0_D2, pwm1: GPIO0_D3, pwm2: GPIO0_D4 */
	int off = GPIO0_D2_OFFSET + REG_PWM;

	grf_writel((1<<(2 * off))|pwm_iomux, GRF_GPIO0D_IOMUX);
	grf_writel(((1<<(16 + off))|pwm_dir), GRF_GPIO0L_DIR);
	grf_writel(((1<<(16 + off))|pwm_do), GRF_GPIO0L_DO);
	grf_writel(((1<<(16 + off))|pwm_en), GRF_GPIO0L_EN);
	sram_udelay(10000);
	#endif
#endif
}
extern void pwm_suspend_voltage(void);
extern void pwm_resume_voltage(void);

void  rk30_pwm_suspend_voltage_set(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR
	pwm_suspend_voltage();
#endif
}

void  rk30_pwm_resume_voltage_set(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR
	pwm_resume_voltage();
#endif
}

/***********************************************************
*	pmic
************************************************************/
int __sramdata g_pmic_type =  0;

#ifdef CONFIG_MFD_TPS65910
#define TPS65910_HOST_IRQ 	PMU_INT_PIN
#define PMU_POWER_SLEEP		PMU_SLEEP_PIN
static struct pmu_info  tps65910_dcdc_info[] = {
	{
		.name = "vdd_cpu",
		.min_uv = 1200000,
		.max_uv = 1200000,
	},
	{
		.name = "vdd_core",
		.min_uv = 1200000,
		.max_uv = 1200000,
	},
	{
		.name = "vio",
		.min_uv = 3300000,
		.max_uv = 3300000,
	},
};

static struct pmu_info tps65910_ldo_info[] = {
	{
		.name          = "vpll",   //vcc25
		.min_uv          = 1000000,
		.max_uv         = 2500000,
	},
	{
		.name          = "vdig1",    //vcc18_cif
		.min_uv          = 1800000,
		.max_uv         = 1800000,
	},

	{
		.name          = "vdig2",   //vdd11
		.min_uv          = 1100000,
		.max_uv         = 1100000,
	},
	{
		.name          = "vaux1",   //vcc28_cif
		.min_uv          = 2800000,
		.max_uv         = 2800000,
	},
	{
		.name          = "vaux2",   //vcca33
		.min_uv          = 3300000,
		.max_uv         = 3300000,
	},
	{
		.name          = "vaux33",   //vcc_tp
		.min_uv          = 3300000,
		.max_uv         = 3300000,
	},
	{
		.name          = "vmmc",   //vcca30
		.min_uv          = 3000000,
		.max_uv         = 3000000,
	},
	{
		.name          = "vdac",   //
		.min_uv          = 1800000,
		.max_uv         = 1800000,
	},
};
#include "../mach-rk30/board-pmu-tps65910.c"
#endif

void __sramfunc board_pmu_suspend(void)
{
	#if defined (CONFIG_MFD_TPS65910)
	if(pmic_is_tps65910())
		board_pmu_tps65910_suspend();
	#endif
}

void __sramfunc board_pmu_resume(void)
{
	#if defined (CONFIG_MFD_TPS65910)
	if(pmic_is_tps65910())
		board_pmu_tps65910_resume();
	#endif
}

#ifdef CONFIG_SND_SOC_RK3026
struct rk3026_codec_pdata rk3026_codec_pdata_info={
    .spk_ctl_gpio = INVALID_GPIO,
    .hp_ctl_gpio = RK2928_PIN1_PA0,
};

static struct resource resources_acodec[] = {
	{
		.start 	= RK2928_ACODEC_PHYS,
		.end 	= RK2928_ACODEC_PHYS + RK2928_ACODEC_SIZE - 1,
		.flags 	= IORESOURCE_MEM,
	},
};

static struct platform_device rk3026_codec = {
	.name	= "rk3026-codec",
	.id		= -1,
	.resource = resources_acodec,
    	.dev = {
        	.platform_data = &rk3026_codec_pdata_info,
    }
};
#endif
/***********************************************************
*	i2c
************************************************************/
#ifdef CONFIG_I2C0_RK30
static struct i2c_board_info __initdata i2c0_info[] = {
#if defined (CONFIG_MFD_TPS65910)
	{
		.type           = "tps65910",
		.addr           = TPS65910_I2C_ID0,
		.flags          = 0,
		.irq            = TPS65910_HOST_IRQ,
		.platform_data = &tps65910_data,
	},
#endif

};
#endif

#ifdef CONFIG_I2C1_RK30
static struct i2c_board_info __initdata i2c1_info[] = {
#if defined (CONFIG_GS_MXC6225)
        {
                .type           = "gs_mxc6225",
                .addr           = 0x15,
                .flags          = 0,
                .irq            = MXC6225_INT_PIN,
                .platform_data  = &mxc6225_info,
        },
#endif
#if defined (CONFIG_GS_MMA7660)
	{
		.type	        = "gs_mma7660",//gs_mma7660
		.addr	        = 0x4c,
		.flags	        = 0,
		.irq	        = MMA7660_INT_PIN,
		.platform_data = &mma7660_info,
	},
#endif
#if defined (CONFIG_GS_MMA8452)
	{
		.type	        = "gs_mma8452",
		.addr	        = 0x1d,
		.flags	        = 0,
		.irq	        = MMA8452_INT_PIN,
		.platform_data = &mma8452_info,
	},
#endif
#if defined (CONFIG_GS_LIS3DH)
	{
		.type	        = "gs_lis3dh",
		.addr	        = 0x19,   //0x19(SA0-->VCC), 0x18(SA0-->GND)
		.flags	        = 0,
		.irq	        = LIS3DH_INT_PIN,
		.platform_data = &lis3dh_info,
	},
#endif
#if defined (CONFIG_GS_LSM303D)
        {
            .type           = "gs_lsm303d",
            .addr           = 0x1d,   //0x19(SA0-->VCC), 0x18(SA0-->GND)
            .flags          = 0,
            .irq            = LSM303D_INT_PIN,         
	    .platform_data  = &lms303d_info,           
        },
#endif
#if defined (CONFIG_COMPASS_AK8975)
	{
		.type          = "ak8975",
		.addr          = 0x0d,
		.flags         = 0,
		.irq           = RK30_PIN3_PD7,	
		.platform_data = &akm8975_info,
		.irq           = RK30_PIN3_PD7,	
		.platform_data = &akm8975_info,
	},
#endif
#if defined (CONFIG_COMPASS_AK8963)
	{
		.type          = "ak8963",
		.addr          = 0x0d,
		.flags         = 0,
		.irq           = RK30_PIN3_PD7,	
		.platform_data = &akm8963_info,
	},
#endif
#if defined (CONFIG_GYRO_L3G4200D)
	{
		.type          = "l3g4200d_gryo",
		.addr          = 0x69,
		.flags         = 0,
		.irq           = L3G4200D_INT_PIN,
		.platform_data = &l3g4200d_info,
	},
#endif

};
#endif

#ifdef CONFIG_I2C2_RK30
static struct i2c_board_info __initdata i2c2_info[] = {
#if defined (CONFIG_TOUCHSCREEN_GT8XX)
	{
		.type          = "Goodix-TS",
		.addr          = 0x55,
		.flags         = 0,
		.irq           = TOUCH_INT_PIN,
		.platform_data = &goodix_info,
	},
#endif
#if defined (CONFIG_TOUCHSCREEN_GSLX680_RK3028)
    {
        .type           = "gslX680",
        .addr           = 0x40,
        .flags          = 0,
        .platform_data =&gslx680_info,
    },
#endif

};
#endif

#ifdef CONFIG_I2C3_RK30
static struct i2c_board_info __initdata i2c3_info[] = {
};
#endif

#ifdef CONFIG_I2C_GPIO_RK30
#define I2C_SDA_PIN     INVALID_GPIO// RK2928_PIN2_PD6   //set sda_pin here
#define I2C_SCL_PIN     INVALID_GPIO//RK2928_PIN2_PD7   //set scl_pin here
static int rk30_i2c_io_init(void)
{
        return 0;
}

struct i2c_gpio_platform_data default_i2c_gpio_data = {
       .sda_pin = I2C_SDA_PIN,
       .scl_pin = I2C_SCL_PIN,
       .udelay = 5, // clk = 500/udelay = 100Khz
       .timeout = 100,//msecs_to_jiffies(100),
       .bus_num    = 5,
       .io_init = rk30_i2c_io_init,
};

static struct i2c_board_info __initdata i2c_gpio_info[] = {
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
#ifdef CONFIG_I2C_GPIO_RK30
	i2c_register_board_info(4, i2c_gpio_info, ARRAY_SIZE(i2c_gpio_info));
#endif
}

/***********************************************************
*	board init
************************************************************/
static struct platform_device *devices[] __initdata = {
#ifdef CONFIG_ION
	&device_ion,
#endif
#ifdef CONFIG_WIFI_CONTROL_FUNC
	&rk29sdk_wifi_device,
#endif
#ifdef CONFIG_SND_SOC_RK3026
 	&rk3026_codec,
#endif
};

static void rk30_pm_power_off(void)
{
#if defined(CONFIG_MFD_TPS65910)
	tps65910_device_shutdown();//tps65910 shutdown
#endif
	gpio_direction_output(POWER_ON_PIN, GPIO_LOW);
	while(1);
}

static void __init machine_rk30_board_init(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR
	pwm_regulator_init();
#endif
	avs_init();
	pm_power_off = rk30_pm_power_off;
	rk_gpio_request(POWER_ON_PIN, GPIOF_DIR_OUT, GPIO_HIGH, "system power on");
	rk30_i2c_register_board_info();
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
	platform_add_devices(devices, ARRAY_SIZE(devices));
	rk_platform_add_display_devices();	
#if defined(CONFIG_WIFI_CONTROL_FUNC)
	rk29sdk_wifi_bt_gpio_control_init();
#elif defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC)
    rk29sdk_wifi_combo_module_gpio_init();
#endif
}

static void __init rk30_reserve(void)
{
	//fb reserve
#ifdef CONFIG_FB_ROCKCHIP
	resource_fb[0].start = board_mem_reserve_add("fb0 buf", get_fb_size());
	resource_fb[0].end = resource_fb[0].start + get_fb_size()- 1;
	#if 0
	resource_fb[1].start = board_mem_reserve_add("ipp buf", RK30_FB0_MEM_SIZE);
	resource_fb[1].end = resource_fb[1].start + RK30_FB0_MEM_SIZE - 1;
	#endif

	#if defined(CONFIG_FB_ROTATE) || !defined(CONFIG_THREE_FB_BUFFER)
	resource_fb[2].start = board_mem_reserve_add("fb2 buf",get_fb_size());
	resource_fb[2].end = resource_fb[2].start + get_fb_size() - 1;
	#endif
#endif
	//ion reserve
#ifdef CONFIG_ION
	rk30_ion_pdata.heaps[0].base = board_mem_reserve_add("ion", ION_RESERVE_SIZE);
#endif
#ifdef CONFIG_VIDEO_RK29
	rk30_camera_request_reserve_mem();
#endif
	board_mem_reserved();
}

/***********************************************************
*	clock
************************************************************/
static struct cpufreq_frequency_table dvfs_arm_table[] = {
	//{.frequency = 312 * 1000,       .index = 1200 * 1000},
	//{.frequency = 504 * 1000,       .index = 1200 * 1000},
	{.frequency = 816 * 1000,       .index = 1250 * 1000},
	//{.frequency = 1008 * 1000,      .index = 1200 * 1000},
	//{.frequency = 1200 * 1000,      .index = 1200 * 1000},
	//{.frequency = 1416 * 1000,      .index = 1200 * 1000},
	//{.frequency = 1608 * 1000,      .index = 1200 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dvfs_gpu_table[] = {
	{.frequency = 100 * 1000,       .index = 1200 * 1000},
	{.frequency = 200 * 1000,       .index = 1200 * 1000},
	{.frequency = 266 * 1000,       .index = 1200 * 1000},
	{.frequency = 300 * 1000,       .index = 1200 * 1000},
	{.frequency = 400 * 1000,       .index = 1200 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dvfs_ddr_table[] = {
	{.frequency = 200 * 1000 + DDR_FREQ_SUSPEND,    .index = 1200 * 1000},
	{.frequency = 300 * 1000 + DDR_FREQ_VIDEO,      .index = 1200 * 1000},
	{.frequency = 400 * 1000 + DDR_FREQ_NORMAL,     .index = 1200 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

void __init board_clock_init(void)
{
	rk2928_clock_data_init(periph_pll_default, codec_pll_default, RK30_CLOCKS_DEFAULT_FLAGS);
	//dvfs_set_arm_logic_volt(dvfs_cpu_logic_table, cpu_dvfs_table, dep_cpu2core_table);	
	dvfs_set_freq_volt_table(clk_get(NULL, "cpu"), dvfs_arm_table);
	dvfs_set_freq_volt_table(clk_get(NULL, "gpu"), dvfs_gpu_table);
	dvfs_set_freq_volt_table(clk_get(NULL, "ddr"), dvfs_ddr_table);
}

/************************ end *****************************/
MACHINE_START(RK30, "RK30board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= rk2928_fixup,
	.reserve	= &rk30_reserve,
	.map_io		= rk2928_map_io,
	.init_irq	= rk2928_init_irq,
	.timer		= &rk2928_timer,
	.init_machine	= machine_rk30_board_init,
MACHINE_END
