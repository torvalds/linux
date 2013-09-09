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

#if defined(CONFIG_GPS_RK)
#include "../../../drivers/misc/gps/rk_gps/rk_gps.h"
#endif
#include "board-rk3026-86v-camera.c"
#include <plat/config.h>
#include <plat/board.h>
#include "../plat-rk/rk-fac-config.c"
#if 1
#define INIT_ERR(name)     do { printk("%s: %s init Failed: \n", __func__, (name)); } while(0)
#else
#define INIT_ERR(name)
#endif

/***********************************************************
*	board config
************************************************************/

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
////////////////////////////////////////////////////////////////////////////////////////
//key
////////////////////////////////////////////////////////////////////////////////////////
#include <plat/key.h>
static struct rk29_keys_button key_button[] = {	
	{		
		.desc	= "play",		
		.code	= KEY_POWER,		
		.wakeup	= 1,	
	},	
	{		
		.desc	= "vol-",	
		.code	= KEY_VOLUMEDOWN,	
	},	
	{		
		.desc	= "vol+",		
		.code	= KEY_VOLUMEUP,	
	},	
	{		
		.desc	= "menu",		
		.code	= EV_MENU,	
	},	
	{		
		.desc	= "esc",		
		.code	= KEY_BACK,	
	},	
	{		
		.desc	= "home",		
		.code	= KEY_HOME,	
	},	
};
struct rk29_keys_platform_data rk29_keys_pdata = {	
	.buttons	= key_button,	
	.chn	= -1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};
////////////////////////////////////////////////////////////////////////////////////////
//Backlight
////////////////////////////////////////////////////////////////////////////////////////
#ifdef CONFIG_BACKLIGHT_RK29_BL
static int rk29_backlight_io_init(void)
{
	int ret = 0;
	printk("rk29_backlight_io_init %d\n",bl_pwm_mode);
	iomux_set(bl_pwm_mode);
	msleep(50);	
	if(bl_en== -1)                
		return 0;  
	ret = port_output_init(bl_en, 1, "bl_en"); 
	if(ret < 0){                
	 	printk("%s: port output init faild\n", __func__);
		return ret;        
	}        
	port_output_on(bl_en);

	return ret;
	
}

static int rk29_backlight_io_deinit(void)
{
	int ret = 0, pwm_gpio;	
	
	if(bl_en != -1)                
		port_deinit(bl_en); 
	
	pwm_gpio = iomux_mode_to_gpio(bl_pwm_mode);
	gpio_request(pwm_gpio, NULL);
	gpio_direction_output(pwm_gpio, GPIO_LOW);
	return ret;
}

static int rk29_backlight_pwm_suspend(void)
{
	int ret = 0, pwm_gpio;

	pwm_gpio = iomux_mode_to_gpio(bl_pwm_mode);
	if (gpio_request(pwm_gpio, NULL)) {
		printk("func %s, line %d: request gpio fail\n", __FUNCTION__, __LINE__);
		return -1;
	}
	gpio_direction_output(pwm_gpio, GPIO_LOW);
 	port_output_off(bl_en);
	return ret;
}

static int rk29_backlight_pwm_resume(void)
{
	int pwm_gpio = iomux_mode_to_gpio(bl_pwm_mode);

	gpio_free(pwm_gpio);
	iomux_set(bl_pwm_mode);
	msleep(30);
	port_output_on(bl_en);
	return 0;
}

static struct rk29_bl_info rk29_bl_info = {
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
////////////////////////////////////////////////////////////////////////////////////////
//LCD
////////////////////////////////////////////////////////////////////////////////////////
#ifdef CONFIG_FB_ROCKCHIP
static int rk_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{
	int ret = 0;
	//printk("rk_fb_io_init %x,%x,%x\n",lcd_cs,lcd_en,lcd_std);
	if(lcd_cs != -1){                
		ret = port_output_init(lcd_cs, 1, "lcd_cs"); 
		if(ret < 0)                       
			printk("%s: port output init faild\n", __func__);
		port_output_on(lcd_cs);
	}   
	
	if(lcd_en != -1){	   
		ret = port_output_init(lcd_en, 1, "lcd_en");        
		if(ret < 0)                
			printk("%s: port output init faild\n", __func__);	
		port_output_on(lcd_en);	
	}
	
	return 0;
}
static int rk_fb_io_disable(void)
{
	if(lcd_cs != -1)                
		port_output_off(lcd_cs);	
	if(lcd_en != -1)                
		port_output_on(lcd_en);	
	return 0;
}
static int rk_fb_io_enable(void)
{
	if(lcd_en != -1)                
		port_output_on(lcd_en);	
	if(lcd_cs != -1)                
		port_output_on(lcd_cs);		
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

////////////////////////////////////////////////////////////////////////////////////////
//TP
////////////////////////////////////////////////////////////////////////////////////////
#if (defined(CONFIG_TOUCHSCREEN_GSLX680_RK3168)||defined (CONFIG_TOUCHSCREEN_GSLX680_RK3028))
static int gslx680_init_platform_hw()
	{
    return 0;
	}
	
static struct tp_platform_data gslx680_data = {

  .init_platform_hw = gslx680_init_platform_hw,
};
struct i2c_board_info __initdata gslx680_info = {
        .type = "gslX680",
        .flags = 0,
        .platform_data = &gslx680_data,
};
#endif


#if defined (CONFIG_TOUCHSCREEN_86V_GT811_IIC)
int gt811_init_platform_hw(int irq,int reset)
{
    int ret;
	if(tp_rst!=-1){
    	ret = port_output_init(tp_rst, 1, "tp_rst"); 
		if(ret<0)
			printk("%s: port output init faild\n", __func__);	
	}

	port_output_off(tp_rst);
    msleep(500);
    port_output_off(tp_rst);
    msleep(500);
    port_output_on(tp_rst);
    mdelay(100);
    return 0;
}


static struct tp_platform_data gt811_data = {
  .model= 811,
  .init_platform_hw= gt811_init_platform_hw,
};

struct i2c_board_info __initdata gt811_info = {
        .type = "gt811_ts",
        .flags = 0,
        .platform_data = &gt811_data,
};
#endif
#if defined(CONFIG_TOUCHSCREEN_GT8XX)
#define TOUCH_PWR_PIN    RK30_PIN0_PC5   // need to fly line by hardware engineer
static int goodix_init_platform_hw(void)
{
	int ret;
	
	if (TOUCH_PWR_PIN != INVALID_GPIO) {
		ret = gpio_request(TOUCH_PWR_PIN, "goodix power pin");
		if (ret != 0) {
			gpio_free(TOUCH_PWR_PIN);
			printk("goodix power error\n");
			return -EIO;
		}
		gpio_direction_output(TOUCH_PWR_PIN, 0);
		gpio_set_value(TOUCH_PWR_PIN, GPIO_LOW);
		msleep(100);
	}

	if(tp_rst!=-1){
    	ret = port_output_init(tp_rst, 1, "tp_rst"); 
		if(ret<0)
			printk("%s: port output init faild\n", __func__);	
	}
	port_output_on(tp_rst);
	msleep(100);
	return 0;
}

struct tp_platform_data goodix_data = {
	.model = 8105,
	.init_platform_hw = goodix_init_platform_hw,
};

struct i2c_board_info __initdata goodix_info = {
  .type = "Goodix-TS",
  .flags = 0,
  .platform_data = &goodix_data,
};
#endif

////////////////////////////////////////////////////////////////////////////////////////
//Gsensor
////////////////////////////////////////////////////////////////////////////////////////
#if defined (CONFIG_GS_LSM303D)
static int lms303d_init_platform_hw(void)
{
	return 0;
}
static struct sensor_platform_data lms303d_data = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
    .init_platform_hw = lms303d_init_platform_hw,
};
struct i2c_board_info __initdata lms303d_info = {
    .type                   = "gs_lsm303d",
    .flags                  = 0,
    .platform_data          =&lms303d_data, 
};
#endif

#if defined (CONFIG_GS_MMA8452)
static int mma8452_init_platform_hw(void)
{
	return 0;
}

static struct sensor_platform_data mma8452_data = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
    .init_platform_hw = mma8452_init_platform_hw,
};
struct i2c_board_info __initdata mma8452_info = {
    .type                   = "gs_mma8452",
    .flags                  = 0,
    .platform_data          =&mma8452_data, 
};
#endif

/*MMA7660 gsensor*/
#if defined (CONFIG_GS_MMA7660)
static int mma7660_init_platform_hw(void)
{
	//rk30_mux_api_set(GPIO1B2_SPI_RXD_UART1_SIN_NAME, GPIO1B_GPIO1B2);

	return 0;
}

static struct sensor_platform_data mma7660_data = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
    .init_platform_hw = mma7660_init_platform_hw,
};
struct i2c_board_info __initdata mma7660_info = {
        .type                   = "gs_mma7660",
        .flags                  = 0,
        .platform_data          =&mma7660_data, 
};
#endif

#if defined (CONFIG_GS_MXC6225)
static int mxc6225_init_platform_hw(void)
{
//        rk30_mux_api_set(GPIO1B1_SPI_TXD_UART1_SOUT_NAME, GPIO1B_GPIO1B1);
        return 0;
}

static struct sensor_platform_data mxc6225_data = {
        .type = SENSOR_TYPE_ACCEL,
        .irq_enable = 0,
        .poll_delay_ms = 30,
        .init_platform_hw = mxc6225_init_platform_hw,    
};
struct i2c_board_info __initdata mxc6225_info = {
        .type                   = "gs_mxc6225",
        .flags                  = 0,
        .platform_data          =&mxc6225_data, 
};
#endif

#if defined (CONFIG_GS_DMT10)
static int dmt10_init_platform_hw(void)
{
        return 0;
}

static struct sensor_platform_data dmt10_data = {
        .type = SENSOR_TYPE_ACCEL,
        .irq_enable = 0,
        .poll_delay_ms = 30,
        .init_platform_hw = dmt10_init_platform_hw,    
};
struct i2c_board_info __initdata dmt10_info = {
        .type                   = "gs_dmard10",
        .flags                  = 0,
        .platform_data          =&dmt10_data, 
};
#endif


#if defined (CONFIG_GS_LIS3DH)
static int lis3dh_init_platform_hw(void)
{
        return 0;
}
static struct sensor_platform_data lis3dh_data = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
        .init_platform_hw = lis3dh_init_platform_hw,
};
struct i2c_board_info __initdata lis3dh_info = {
        .type                   = "gs_lis3dh",
        .flags                  = 0,
        .platform_data          =&lis3dh_data, 
};
#endif

////////////////////////////////////////////////////////////////////////////////////////
//battery
////////////////////////////////////////////////////////////////////////////////////////
#ifdef CONFIG_BATTERY_RK30_ADC_FAC
static struct rk30_adc_battery_platform_data rk30_adc_battery_platdata = {

};
 

static struct platform_device rk30_device_adc_battery = {
        .name   = "rk30-battery",
        .id     = -1,
        .dev = {
                .platform_data = &rk30_adc_battery_platdata,
        },
};
#endif

////////////////////////////////////////////////////////////////////////////////////////
//codec
////////////////////////////////////////////////////////////////////////////////////////
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

#if defined (CONFIG_SND_RK29_SOC_RT5631)
static struct codec_platform_data rt5631_data = {
};
struct i2c_board_info __initdata rt5631_info = {
        .type                   = "rt5631",
        .flags                  = 0,
        .platform_data          =&rt5631_data, 
};
#endif

#if defined (CONFIG_SND_RK29_SOC_ES8323)
static struct codec_platform_data es8323_data = {
};
struct i2c_board_info __initdata es8323_info = {
        .type                   = "es8323",
        .flags                  = 0,
        .platform_data          =&es8323_data, 
};
#endif

#if defined(CONFIG_MFD_RK616)
#define RK616_SCL_RATE			(100*1000)   //i2c scl rate
static int rk616_power_on_init(void)
{
	int ret;
		
	if(codec_power!=-1)
	{
		ret = port_output_init(codec_power, 1, "codec_power"); 
		if(ret < 0)                       
			printk("%s: port output init faild\n", __func__);
		port_output_on(codec_power);
	}
	
	if(codec_rst!=-1)
	{
		ret = port_output_init(codec_rst, 1, "codec_rst"); 
		if(ret < 0)                       
			printk("%s: port output init faild\n", __func__);
		port_output_on(codec_rst);
		msleep(100);
		port_output_off(codec_rst);
		msleep(100);
		port_output_on(codec_rst);
	}
	return 0;
	
}


static int rk616_power_deinit(void)
{
	if(codec_power!=-1)
	{
		port_output_off(codec_power);
		port_deinit(codec_power);
	}
	if(codec_rst!=-1)
	{
		port_output_off(codec_rst);
		port_deinit(codec_rst);
	}
	return 0;
}

static struct rk616_platform_data rk616_pdata = {
	.power_init = rk616_power_on_init,
	.power_deinit = rk616_power_deinit,
	.scl_rate   = RK616_SCL_RATE,
	.lcd0_func = INPUT,             //port lcd0 as input
	.lcd1_func = INPUT,             //port lcd1 as input
	.lvds_ch_nr = 1,		//the number of used lvds channel  
	//.hdmi_irq = RK30_PIN2_PD6,
	//.spk_ctl_gpio = RK30_PIN2_PD7,
	.hp_ctl_gpio = RK30_PIN2_PD7,
};

struct i2c_board_info __initdata rk616_info = {
	.type	       = "rk616",
	.flags	       = 0,
	.platform_data = &rk616_pdata,
};
#endif


////////////////////////////////////////////////////////////////////////////////////////
//spi
////////////////////////////////////////////////////////////////////////////////////////
static struct spi_board_info board_spi_devices[] = {
};


////////////////////////////////////////////////////////////////////////////////////////
//compass
////////////////////////////////////////////////////////////////////////////////////////
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
*	usb wifi
************************************************************/
#if defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) 

static void rkusb_wifi_power(int on) {
	int ret=0;
	struct regulator *ldo = NULL;
	printk("hjc:%s[%d],on=%d\n",__func__,__LINE__,on);
#if defined(CONFIG_MFD_TPS65910)	
	if (pmic_is_tps65910())
		ldo = regulator_get(NULL, "vmmc");  //vccio_wl
#endif
#if defined(CONFIG_REGULATOR_ACT8931)
	if(pmic_is_act8931())
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
*	rfkill
************************************************************/
#ifdef CONFIG_RFKILL_RK
// bluetooth rfkill device, its driver in net/rfkill/rfkill-rk.c
static struct rfkill_rk_platform_data rfkill_rk_platdata = {
	.type               = RFKILL_TYPE_BLUETOOTH,

	.poweron_gpio       = { // BT_REG_ON
		.io             = INVALID_GPIO, //RK30_PIN3_PC7,
		.enable         = GPIO_HIGH,
		.iomux          = {
			.name       = "bt_poweron",
			//.fgpio      = GPIO3_C7,
		},
	},

	.reset_gpio         = { // BT_RST
		.io             = RK30_PIN1_PB3, // set io to INVALID_GPIO for disable it
		.enable         = GPIO_LOW,
		.iomux          = {
			.name       = "bt_reset",
			.fgpio      = GPIO1_B3,
		},
	}, 

	.wake_gpio          = { // BT_WAKE, use to control bt's sleep and wakeup
		.io             = RK30_PIN1_PB2, // set io to INVALID_GPIO for disable it
		.enable         = GPIO_HIGH,
		.iomux          = {
			.name       = "bt_wake",
			.fgpio      = GPIO1_B2,
		},
	},

	.wake_host_irq      = { // BT_HOST_WAKE, for bt wakeup host when it is in deep sleep
		.gpio           = {
			.io         = RK30_PIN0_PA4, // set io to INVALID_GPIO for disable it
			.enable     = GPIO_LOW,      // set GPIO_LOW for falling, set 0 for rising
			.iomux      = {
				.name   = "bt_wake_host",
				//.fgpio  = GPIO0_A4,  
			},
		},
	},

	.rts_gpio           = { // UART_RTS, enable or disable BT's data coming
		.io             = RK30_PIN1_PA3, // set io to INVALID_GPIO for disable it
		.enable         = GPIO_LOW,
		.iomux          = {
			.name       = "bt_rts",
			.fgpio      = GPIO1_A3,
			.fmux       = UART0_RTSN,
		},
	}
};

static struct platform_device device_rfkill_rk = {
    .name   = "rfkill_rk",
    .id     = -1,
    .dev    = {
        .platform_data = &rfkill_rk_platdata,
    },
};
#endif

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
		.max_uv         = 3300000,
	},
	
};
static  struct pmu_info  tps65910_ldo_info[] = {

	{
		.name          = "vpll",   //vcc25
		.min_uv          = 2500000,
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
		.name          = "vaux2",   //vcc33
		.min_uv          = 3300000,
		.max_uv         = 3300000,
	},
	{
		.name          = "vaux33",   //vcc_tp
		.min_uv          = 3300000,
		.max_uv         = 3300000,
	},
/*	{
		.name          = "vmmc",   //vcca30
		.min_uv          = 3000000,
		.max_uv         = 3000000,
	},
*/
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

static int gpio_118,
     gpio_11c,
     gpio_120,
     gpio_124,
     gpio_128,
     gpio_12c,
     gpio_130,
     gpio_134,
     lcd_gpio;

void board_gpio_suspend(void)
{

	
	gpio_118 = grf_readl(0x0118);
	gpio_11c = grf_readl(0x011c);
	gpio_120 = grf_readl(0x0120);
	gpio_124 = grf_readl(0x0124);
	gpio_128 = grf_readl(0x0128);
	gpio_12c = grf_readl(0x012c);
	gpio_130 = grf_readl(0x0130);
	gpio_134 = grf_readl(0x0134);
//	lcd_gpio = grf_readl(0x0cc);
	grf_writel(0xffffffff, 0x0118);
	grf_writel(0xffffffff, 0x011c);
	grf_writel(0xffffffff, 0x0120);
	grf_writel(0xffffffff, 0x0124);
	grf_writel(0xff7fff7f, 0x0128);
	grf_writel(0xffffffff, 0x012c);
	grf_writel(0xfefffeff, 0x0130);
	grf_writel(0xffffffff, 0x0134);



	
	sram_printch('9');

}
 void board_gpio_resume(void) 
 {
	grf_writel(0xffff0000|gpio_118, 0x0118);
	grf_writel(0xffff0000|gpio_11c, 0x011c);
	grf_writel(0xffff0000|gpio_120, 0x0120);
	grf_writel(0xffff0000|gpio_124, 0x0124);
	grf_writel(0xffff0000|gpio_128, 0x0128);
	grf_writel(0xffff0000|gpio_12c, 0x012c);	
	grf_writel(0xffff0000|gpio_130, 0x0130);
	grf_writel(0xffff0000|gpio_134, 0x0134);
 }

#if defined(CONFIG_GPS_RK)
#define GPS_OSCEN_PIN 	RK2928_PIN1_PB0
#define GPS_RXEN_PIN 	RK2928_PIN1_PB0

static int rk_gps_io_init(void)
{
	printk("%s \n", __FUNCTION__);
	
	gpio_request(GPS_OSCEN_PIN, NULL);
	gpio_direction_output(GPS_OSCEN_PIN, GPIO_LOW);	
	
	iomux_set(GPS_CLK);//GPS_CLK
	iomux_set(GPS_MAG);//GPS_MAG
	iomux_set(GPS_SIGN);//GPS_SIGN
#if 0
	gpio_request(RK30_PIN1_PA6, NULL);
	gpio_direction_output(RK30_PIN1_PA6, GPIO_LOW);

	gpio_request(RK30_PIN1_PA5, NULL);
	gpio_direction_output(RK30_PIN1_PA5, GPIO_LOW);	

	gpio_request(RK30_PIN1_PA7, NULL);
	gpio_direction_output(RK30_PIN1_PA7, GPIO_LOW);		
#endif	
	return 0;
}
static int rk_gps_power_up(void)
{
	printk("%s \n", __FUNCTION__);

	return 0;
}

static int rk_gps_power_down(void)
{
	printk("%s \n", __FUNCTION__);

	return 0;
}

static int rk_gps_reset_set(int level)
{
	return 0;
}
static int rk_enable_hclk_gps(void)
{
	struct clk *gps_aclk = NULL;
	gps_aclk = clk_get(NULL, "aclk_gps");
	if(gps_aclk) {
		clk_enable(gps_aclk);
		clk_put(gps_aclk);
		printk("%s \n", __FUNCTION__);
	}
	else
		printk("get gps aclk fail\n");
	return 0;
}
static int rk_disable_hclk_gps(void)
{
	struct clk *gps_aclk = NULL;
	gps_aclk = clk_get(NULL, "aclk_gps");
	if(gps_aclk) {
		//TO wait long enough until GPS ISR is finished.
		msleep(5);
		clk_disable(gps_aclk);
		clk_put(gps_aclk);
		printk("%s \n", __FUNCTION__);
	}	
	else
		printk("get gps aclk fail\n");
	return 0;
}
static struct rk_gps_data rk_gps_info = {
	.io_init = rk_gps_io_init,
	.power_up = rk_gps_power_up,
	.power_down = rk_gps_power_down,
	.reset = rk_gps_reset_set,
	.enable_hclk_gps = rk_enable_hclk_gps,
	.disable_hclk_gps = rk_disable_hclk_gps,
	.GpsSign = RK2928_PIN1_PA5,
	.GpsMag = RK2928_PIN1_PA4,        //GPIO index
	.GpsClk = RK2928_PIN1_PA2,        //GPIO index
	.GpsVCCEn = GPS_OSCEN_PIN,     //GPIO index
#if 0	
	.GpsSpi_CSO = RK30_PIN1_PA4,    //GPIO index
	.GpsSpiClk = RK30_PIN1_PA5,     //GPIO index
	.GpsSpiMOSI = RK30_PIN1_PA7,	  //GPIO index
#endif	
	.GpsIrq = IRQ_GPS,
	.GpsSpiEn = 0,
	.GpsAdcCh = 2,
	.u32GpsPhyAddr = RK2928_GPS_PHYS,
	.u32GpsPhySize = RK2928_GPS_SIZE,
};

static struct platform_device rk_device_gps = {
	.name = "gps_hv5820b",
	.id = -1,
	.dev		= {
	.platform_data = &rk_gps_info,
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
};
#endif

#ifdef CONFIG_I2C3_RK30
static struct i2c_board_info __initdata i2c3_info[] = {
};
#endif

#ifdef CONFIG_I2C_GPIO_RK30
#define I2C_SDA_PIN     INVALID_GPIO// RK30_PIN2_PD6   //set sda_pin here
#define I2C_SCL_PIN     INVALID_GPIO//RK30_PIN2_PD7   //set scl_pin here
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
#ifdef CONFIG_RFKILL_RK
	&device_rfkill_rk,
#endif
#ifdef CONFIG_BATTERY_RK30_ADC_FAC
 	&rk30_device_adc_battery,
#endif
#ifdef CONFIG_SND_SOC_RK3026
	&rk3026_codec,
#endif
#ifdef CONFIG_GPS_RK
	&rk_device_gps,
#endif
};

static void rk30_pm_power_off(void)
{
#if defined(CONFIG_MFD_TPS65910)
	tps65910_device_shutdown();//tps65910 shutdown
#endif
	port_output_off(pwr_on);
	while(1);
}
static int __init tp_board_init(void)
{
	int i;
	struct port_config irq_port;
	struct port_config rst_port;
	int ret = check_tp_param();

	if(ret < 0)
	    return ret;

   irq_port = get_port_config(tp_irq);
	 rst_port = get_port_config(tp_rst);
#if (defined (CONFIG_TOUCHSCREEN_GSLX680_RK3168)||defined (CONFIG_TOUCHSCREEN_GSLX680_RK3028))
	if(tp_type == TP_TYPE_GSLX680){		 
		gslx680_data.irq_pin = irq_port.gpio;
		gslx680_info.addr = tp_addr; 
		//gslx680_data.reset_pin= rst_port.gpio;
		gslx680_data.x_max=tp_xmax;
		gslx680_data.y_max=tp_ymax;
		gslx680_data.firmVer=tp_firmVer;
		i2c_register_board_info(tp_i2c, &gslx680_info, 1);
	}
#endif   

#if defined (CONFIG_TOUCHSCREEN_86V_GT811_IIC)
	if(tp_type == TP_TYPE_GT811_86V){
		gt811_data.irq_pin = irq_port.gpio;
		gt811_info.addr = tp_addr; 
		gt811_data.reset_pin= rst_port.gpio;
		gt811_data.x_max=tp_xmax;
		gt811_data.y_max=tp_ymax;
		i2c_register_board_info(tp_i2c, &gt811_info, 1);
	}
#endif

#if defined(CONFIG_TOUCHSCREEN_GT8XX)
	if(tp_type == TP_TYPE_GT8XX){		
		goodix_data.irq_pin = irq_port.gpio;
		goodix_info.addr = tp_addr; 
		goodix_data.reset_pin= rst_port.gpio;
		goodix_data.x_max=tp_xmax;
		goodix_data.y_max=tp_ymax;
		i2c_register_board_info(tp_i2c, &goodix_info, 1);
	}
#endif
    return 0;
}


static int __init codec_board_init(void)
{
	struct port_config spk_port;
	struct port_config hp_port;
	struct port_config hdmi_irq_port;
	int ret = check_codec_param();

	if(ret < 0)
	    return ret;

	spk_port = get_port_config(spk_ctl);
	hp_port = get_port_config(hp_det);

#if defined (CONFIG_SND_RK29_SOC_RT5631)
	if(codec_type == CODEC_TYPE_RT5631){
		rt5631_data.spk_pin = spk_port.gpio;
		rt5631_info.addr = codec_addr; 
		rt5631_data.hp_pin= hp_port.gpio;
		i2c_register_board_info(codec_i2c, &rt5631_info, 1);
	}
#endif	

#if defined (CONFIG_SND_RK29_SOC_ES8323)
	if(codec_type == CODEC_TYPE_ES8323){
		es8323_data.spk_pin = spk_port.gpio;
		es8323_info.addr = codec_addr; 
		es8323_data.hp_pin= hp_port.gpio;
		i2c_register_board_info(codec_i2c, &es8323_info, 1);
	}
#endif	

#if defined (CONFIG_MFD_RK616)
if(codec_type == CODEC_TYPE_RK616){
		rk616_pdata.spk_ctl_gpio = spk_port.gpio;
		rk616_info.addr = codec_addr; 
		rk616_pdata.hdmi_irq= get_port_config(codec_hdmi_irq).gpio;
		i2c_register_board_info(codec_i2c, &rk616_info, 1);
	}
#endif 
	return 0;
}

static int __init chg_board_init(void)
{   
	int i;
	int ret = check_chg_param();        
	if(ret < 0)                
		return ret;  
#ifdef CONFIG_BATTERY_RK30_ADC_FAC      
	//rk30_adc_battery_platdata.adc_channel = chg_adc;        
	if(dc_det != -1){                
		rk30_adc_battery_platdata.dc_det_pin = get_port_config(dc_det).gpio;
		printk("rk30_adc_battery_platdata.dc_det_pin %d %d",rk30_adc_battery_platdata.dc_det_pin,RK30_PIN0_PB2);
		rk30_adc_battery_platdata.dc_det_level = !get_port_config(dc_det).io.active_low;
		} 
	else{
			rk30_adc_battery_platdata.dc_det_pin=INVALID_GPIO;
		}
	if(bat_low != -1){                
		rk30_adc_battery_platdata.batt_low_pin = get_port_config(bat_low).gpio;
		rk30_adc_battery_platdata.batt_low_level = !get_port_config(bat_low).io.active_low;
		}
	else{
			rk30_adc_battery_platdata.batt_low_pin=INVALID_GPIO;
		}
	if(chg_ok != -1){                
		rk30_adc_battery_platdata.charge_ok_pin = get_port_config(chg_ok).gpio;
		rk30_adc_battery_platdata.charge_ok_level = !get_port_config(chg_ok).io.active_low;
		}
	else{
		rk30_adc_battery_platdata.charge_ok_pin=INVALID_GPIO;
		}	
	if(chg_set != -1){                
		rk30_adc_battery_platdata.charge_set_pin = get_port_config(chg_set).gpio; 
		rk30_adc_battery_platdata.charge_set_level = !get_port_config(chg_set).io.active_low;
		} 
	else{
		rk30_adc_battery_platdata.charge_set_pin=INVALID_GPIO;
		}
	if(usb_det!= -1){
			rk30_adc_battery_platdata.usb_det_pin = get_port_config(chg_set).gpio; 
			rk30_adc_battery_platdata.usb_det_level = !get_port_config(chg_set).io.active_low;
		}
	else{
		rk30_adc_battery_platdata.usb_det_pin=INVALID_GPIO;			
		}

	if(ref_vol!= -1){
		rk30_adc_battery_platdata.reference_voltage=ref_vol;
		}
	
	if(up_res!= -1){
		rk30_adc_battery_platdata.pull_up_res=up_res;
		}
	if(down_res!= -1){
		rk30_adc_battery_platdata.pull_down_res=down_res;
		}
	if(root_chg!= -1){
		rk30_adc_battery_platdata.is_reboot_charging=root_chg;
		}
	if(save_cap!= -1){
		rk30_adc_battery_platdata.save_capacity=save_cap;
		}
	if(low_vol!= -1){
		rk30_adc_battery_platdata.low_voltage_protection=low_vol;
		}

	for(i=0;i<11;i++)
	{
		rk30_adc_battery_platdata.chargeArray[i]=bat_charge[i];
		rk30_adc_battery_platdata.dischargeArray[i]=bat_discharge[i];
	}
#endif		
	return 0;
}

static int __init gs_board_init(void)
{        
	int i;        
	struct port_config port;        
	int ret = check_gs_param();        
	if(ret < 0)                
		return ret;        
	 port = get_port_config(gs_irq);       
//mma7660
#if defined (CONFIG_GS_MMA7660)        
	if(gs_type == GS_TYPE_MMA7660){
		
		mma7660_info.irq = port.gpio;                
		mma7660_info.addr = gs_addr;               
		for(i = 0; i < 9; i++)                        
			mma7660_data.orientation[i] = gs_orig[i];	        
		i2c_register_board_info(gs_i2c, &mma7660_info, 1);        
	}
#endif

#if defined (CONFIG_GS_LIS3DH)
	if(gs_type == GS_TYPE_LIS3DH){                
		lis3dh_info.irq = port.gpio;                
		lis3dh_info.addr = gs_addr;                
		for(i = 0; i < 9; i++)                        
			lis3dh_data.orientation[i] = gs_orig[i];	        
		i2c_register_board_info(gs_i2c, &lis3dh_info, 1); 
	}
#endif

#if defined (CONFIG_GS_MXC6225)
	if(gs_type == GS_TYPE_MXC6225){ 
		mxc6225_info.irq = port.gpio;                
		mxc6225_info.addr = gs_addr;                
		for(i = 0; i < 9; i++)                        
			mxc6225_data.orientation[i] = gs_orig[i];	        
		i2c_register_board_info(gs_i2c, &mxc6225_info, 1); 
	}
#endif

#if defined (CONFIG_GS_DMT10)
	if(gs_type == GS_TYPE_DMARAD10){ 
		dmt10_info.irq = port.gpio;                
		dmt10_info.addr = gs_addr;                
		for(i = 0; i < 9; i++)                        
			dmt10_data.orientation[i] = gs_orig[i];	        
		i2c_register_board_info(gs_i2c, &dmt10_info, 1); 
	}
#endif

#if defined (CONFIG_GS_MMA8452)
	if(gs_type == GS_TYPE_MMA8452){ 
		mma8452_info.irq = port.gpio;                
		mma8452_info.addr = gs_addr;                
		for(i = 0; i < 9; i++)                        
			mma8452_data.orientation[i] = gs_orig[i];	        
		i2c_register_board_info(gs_i2c, &mma8452_info, 1); 
	}
#endif

#if defined (CONFIG_GS_LSM303D)
	if(gs_type == GS_TYPE_LSM303D){ 
		lms303d_info.irq = port.gpio;                
		lms303d_info.addr = gs_addr;                
		for(i = 0; i < 9; i++)                        
			lms303d_data.orientation[i] = gs_orig[i];	        
		i2c_register_board_info(gs_i2c, &lms303d_info, 1); 
	}
#endif
	return 0;
}

static int __init bl_board_init(void){       
	int ret = check_bl_param();        
	if(ret < 0)                
		return ret; 

	switch(bl_pwmid)
    {
    	case 0:
			bl_pwm_mode = PWM0;
			break;
		case 1:
			bl_pwm_mode = PWM1;
			break;
		case 2:
			bl_pwm_mode = PWM2;
			break;
    }
	
	rk29_bl_info.pwm_id = bl_pwmid;
	rk29_bl_info.brightness_mode=bl_mode;
	rk29_bl_info.pre_div=bl_div;
	rk29_bl_info.bl_ref = bl_ref;
	rk29_bl_info.min_brightness=bl_min;
	rk29_bl_info.max_brightness=bl_max;


	return 0;
}

static int __init lcd_board_init(void)
{        
	return check_lcd_param();
}

static int __init key_board_init(void){        
	int i;        
	struct port_config port;        
	for(i = 0; i < key_val_size; i++){ 
		if(key_val[i] & (1<<31)){                        
			key_button[i].adc_value = key_val[i] & 0xffff;                        
			key_button[i].gpio = INVALID_GPIO;                
		}else{
			port = get_port_config(key_val[i]);
			key_button[i].gpio = port.gpio;                        
			key_button[i].active_low = port.io.active_low;                
		}        
	}        
	rk29_keys_pdata.nbuttons = key_val_size;        
	rk29_keys_pdata.chn = key_adc;        
	return 0;
}

static int __init wifi_board_init(void)
{        
	return check_wifi_param();
}


static int __init rk_config_init(void)
{
	int ret = 0;
	ret = lcd_board_init();       
	if(ret < 0)                
		INIT_ERR("lcd");

	ret = bl_board_init();       
	if(ret < 0)                
		INIT_ERR("backlight");
	
	ret = tp_board_init();
	if(ret < 0)
		INIT_ERR("tp");  

	ret = gs_board_init();       
	if(ret < 0)                
		INIT_ERR("gsensor");
	
	ret = codec_board_init();
	if(ret < 0)
		INIT_ERR("codec"); 

	ret = key_board_init();        
	if(ret < 0)
		INIT_ERR("key"); 

	ret = chg_board_init();
	if(ret < 0)
		INIT_ERR("charge"); 

	ret = wifi_board_init();
	if(ret < 0)
		INIT_ERR("wifi"); 
	
	return 0;	
}

static void __init machine_rk30_board_init(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR
	pwm_regulator_init();
#endif
	avs_init();
	pm_power_off = rk30_pm_power_off;
	rk_power_on();
	rk_config_init();
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
#ifdef CONFIG_GPS_RK
	//it must be more than 8MB
	rk_gps_info.u32MemoryPhyAddr = board_mem_reserve_add("gps", SZ_8M);
#endif
	board_mem_reserved();
}

/***********************************************************
*	clock
************************************************************/
static struct cpufreq_frequency_table dvfs_arm_table[] = {
	{.frequency = 312 * 1000,       .index = 1200 * 1000},
	{.frequency = 504 * 1000,       .index = 1200 * 1000},
	{.frequency = 816 * 1000,       .index = 1250 * 1000},
	{.frequency = 1008 * 1000,      .index = 1350 * 1000},
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
