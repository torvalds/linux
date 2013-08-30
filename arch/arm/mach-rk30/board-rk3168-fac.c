/*
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
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <mach/dvfs.h>

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
#include <linux/rk_fb.h>
#include <linux/regulator/machine.h>
#include <linux/rfkill-rk.h>
#include <linux/sensor-dev.h>
#include <linux/mfd/tps65910.h>
#include <linux/regulator/act8846.h>
#include <linux/regulator/rk29-pwm-regulator.h>
#if defined(CONFIG_CT36X_TS)
#include <linux/ct36x.h>
#endif
#include <linux/regulator/act8931.h>
#include <plat/config.h>
 


#if defined(CONFIG_MFD_RK610)
#include <linux/mfd/rk610_core.h>
#endif
#if defined(CONFIG_MFD_RK616)
#include <linux/mfd/rk616.h>
#endif

#ifdef CONFIG_TOUCHSCREEN_GT82X_IIC
#include <linux/goodix_touch_82x.h>
#endif

#if defined(CONFIG_RK_HDMI)
	#include "../../../drivers/video/rockchip/hdmi/rk_hdmi.h"
#endif

#if defined(CONFIG_SPIM_RK29)
#include "../../../drivers/spi/rk29_spim.h"
#endif
#if defined(CONFIG_GPS_RK)
#include "../../../drivers/misc/gps/rk_gps/rk_gps.h"
#endif

#if defined(CONFIG_MT6620)
#include <linux/gps.h>
#endif

#if 1
#define INIT_ERR(name)     do { printk("%s: %s init Failed: \n", __func__, (name)); } while(0)
#else
#define INIT_ERR(name)
#endif

#include "board-rk3168-86v-camera.c"
#include "../plat-rk/rk-fac-config.c"
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

#if defined (CONFIG_TOUCHSCREEN_GSLX680_RK3168)
int gslx680_init_platform_hw()
{
	int ret;
	if(tp_rst!=-1){
    	ret = port_output_init(tp_rst, 1, "tp_rst"); 
		if(ret<0)
			printk("%s: port output init faild\n", __func__);	
	}
    
    port_output_on(tp_rst);
    mdelay(10);
    port_output_off(tp_rst);
    mdelay(10);
    port_output_on(tp_rst);
    msleep(300);
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


/***********************************************************
*	rk30  codec
************************************************************/
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
#ifdef  CONFIG_TOUCHSCREEN_GT82X_IIC
#define TOUCH_ENABLE_PIN	INVALID_GPIO
#define TOUCH_RESET_PIN  RK30_PIN0_PB6//RK30_PIN4_PD0
#define TOUCH_INT_PIN    RK30_PIN1_PB7//RK30_PIN4_PC2
int goodix_init_platform_hw(void)
{
	int ret;
	
	//rk30_mux_api_set(GPIO4D0_SMCDATA8_TRACEDATA8_NAME, GPIO4D_GPIO4D0);  //hjc
	//rk30_mux_api_set(GPIO4C2_SMCDATA2_TRACEDATA2_NAME, GPIO4C_GPIO4C2);   //hjc
	//printk("%s:0x%x,0x%x\n",__func__,rk30_mux_api_get(GPIO4D0_SMCDATA8_TRACEDATA8_NAME),rk30_mux_api_get(GPIO4C2_SMCDATA2_TRACEDATA2_NAME));
	if (TOUCH_ENABLE_PIN != INVALID_GPIO) {
		ret = gpio_request(TOUCH_ENABLE_PIN, "goodix power pin");
		if (ret != 0) {
			gpio_free(TOUCH_ENABLE_PIN);
			printk("goodix power error\n");
			return -EIO;
		}
		gpio_direction_output(TOUCH_ENABLE_PIN, 0);
		gpio_set_value(TOUCH_ENABLE_PIN, GPIO_LOW);
		msleep(100);
	}

	if (TOUCH_RESET_PIN != INVALID_GPIO) {
		ret = gpio_request(TOUCH_RESET_PIN, "goodix reset pin");
		if (ret != 0) {
			gpio_free(TOUCH_RESET_PIN);
			printk("goodix gpio_request error\n");
			return -EIO;
		}
		gpio_direction_output(TOUCH_RESET_PIN, 0);
		gpio_set_value(TOUCH_RESET_PIN, GPIO_LOW);
		msleep(10);
		gpio_set_value(TOUCH_RESET_PIN, GPIO_HIGH);
		msleep(500);
	}
	return 0;
}
u8 ts82x_config_data[] = {
	0x65,0x00,0x04,0x00,0x03,0x00,0x0A,0x0D,0x1E,0xE7,
	0x32,0x03,0x08,0x10,0x48,0x42,0x42,0x20,0x00,0x01,
	0x60,0x60,0x4B,0x6E,0x0E,0x0D,0x0C,0x0B,0x0A,0x09,
	0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x00,0x1D,
	0x1C,0x1B,0x1A,0x19,0x18,0x17,0x16,0x15,0x14,0x13,
	0x12,0x11,0x10,0x0F,0x50,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2B,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00
};
static struct goodix_i2c_rmi_platform_data ts82x_pdata = {
    .gpio_shutdown = TOUCH_ENABLE_PIN,
    .gpio_irq = TOUCH_INT_PIN,
    .gpio_reset = TOUCH_RESET_PIN,
    .irq_edge = 1, /* 0:rising edge, 1:falling edge */

    .ypol = 1,
	.swap_xy = 1,
	.xpol = 0,
	.xmax = 800,
    .ymax = 600,
    .config_info_len =ARRAY_SIZE(ts82x_config_data),
    .config_info = ts82x_config_data,
	.init_platform_hw= goodix_init_platform_hw,
};
#endif

static struct spi_board_info board_spi_devices[] = {
};
/***********************************************************
*	rk30  backlight
************************************************************/
#ifdef CONFIG_BACKLIGHT_RK29_BL
static int rk29_backlight_io_init(void)
{
	int ret = 0;
	iomux_set(bl_pwm_mode);		
	msleep(100);

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


static int rk29_backlight_io_deinit(void){        
  	int pwm_gpio;	
	
	if(bl_en != -1)                
		port_deinit(bl_en); 
	
	pwm_gpio = iomux_mode_to_gpio(bl_pwm_mode);
	gpio_request(pwm_gpio, NULL);
	gpio_direction_output(pwm_gpio, GPIO_LOW);
	return 0;
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
	msleep(150);
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


#ifdef CONFIG_FB_ROCKCHIP
static int rk_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{	
	int ret = 0; 
	printk("rk_fb_io_init %x,%x,%x\n",lcd_cs,lcd_en,lcd_std);
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

	if(lcd_std != -1){	   
		ret = port_output_init(lcd_std, 1, "lcd_std");        
		if(ret < 0)                
			printk("%s: port output init faild\n", __func__);	
		port_output_on(lcd_std);	
	}
	
	return ret;
}

static int rk_fb_io_disable(void){ 
	if(lcd_cs != -1)                
		port_output_off(lcd_cs);	
	if(lcd_en != -1)                
		port_output_on(lcd_en);	
	return 0;
}

static int rk_fb_io_enable(void){  
	if(lcd_en != -1)                
		port_output_on(lcd_en);	
	if(lcd_cs != -1)                
		port_output_on(lcd_cs);		
	return 0;
}
       
#if defined(CONFIG_LCDC0_RK3066B)
struct rk29fb_info lcdc0_screen_info = {
#if defined(CONFIG_RK_HDMI) && defined(CONFIG_HDMI_SOURCE_LCDC0)&& defined(CONFIG_DUAL_LCDC_DUAL_DISP_IN_KERNEL)
	.prop	   = EXTEND,		//primary display device
	.io_init   = NULL,
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

#if defined(CONFIG_LCDC1_RK3066B)
struct rk29fb_info lcdc1_screen_info = {
#if defined(CONFIG_RK_HDMI) && defined(CONFIG_HDMI_SOURCE_LCDC1) && defined(CONFIG_DUAL_LCDC_DUAL_DISP_IN_KERNEL)
	.prop	   = EXTEND,		//primary display device
	.io_init   = NULL,
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

#if defined(CONFIG_LCDC0_RK3066B)
static struct resource resource_lcdc0[] = {
	[0] = {
		.name  = "lcdc0 reg",
		.start = RK30_LCDC0_PHYS,
		.end   = RK30_LCDC0_PHYS + RK30_LCDC0_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	
	[1] = {
		.name  = "lcdc0 irq",
		.start = IRQ_LCDC0,
		.end   = IRQ_LCDC0,
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
#if defined(CONFIG_LCDC1_RK3066B) 
static struct resource resource_lcdc1[] = {
	[0] = {
		.name  = "lcdc1 reg",
		.start = RK30_LCDC1_PHYS,
		.end   = RK30_LCDC1_PHYS + RK30_LCDC1_SIZE - 1,
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

#if defined(CONFIG_MFD_RK610)
#define RK610_RST_PIN 			RK30_PIN3_PB2
static int rk610_power_on_init(void)
{
	int ret;
	if(RK610_RST_PIN != INVALID_GPIO)
	{
		ret = gpio_request(RK610_RST_PIN, "rk610 reset");
		if (ret)
		{
			printk(KERN_ERR "rk610_control_probe request gpio fail\n");
		}
		else 
		{
			gpio_direction_output(RK610_RST_PIN, GPIO_HIGH);
			msleep(100);
			gpio_direction_output(RK610_RST_PIN, GPIO_LOW);
			msleep(100);
	    		gpio_set_value(RK610_RST_PIN, GPIO_HIGH);
		}
	}

	return 0;
	
}


static struct rk610_ctl_platform_data rk610_ctl_pdata = {
	.rk610_power_on_init = rk610_power_on_init,
};
#endif

#ifdef CONFIG_SND_SOC_RK610
static int rk610_codec_io_init(void)
{
//if need iomux.
//Must not gpio_request
	return 0;
}

static struct rk610_codec_platform_data rk610_codec_pdata = {
	.spk_ctl_io = RK30_PIN2_PD7,
	.io_init = rk610_codec_io_init,
	.boot_depop = 1,
};
#endif

#if defined(CONFIG_MFD_RK616)
//#define RK616_RST_PIN 			RK30_PIN3_PB2
//#define RK616_PWREN_PIN			RK30_PIN0_PA3
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


static struct rk616_platform_data rk616_pdata = {
	.power_init = rk616_power_on_init,
	.scl_rate   = RK616_SCL_RATE,
	.lcd0_func = INPUT,             //port lcd0 as input
	.lcd1_func = OUTPUT,             //port lcd1 as input
	.lvds_ch_nr = 1,		//the number of used lvds channel  
	//.hdmi_irq = RK30_PIN2_PD6,
	//.spk_ctl_gpio = RK30_PIN2_PD7,
};

struct i2c_board_info __initdata rk616_info = {
	.type	       = "rk616",
	.flags	       = 0,
	.platform_data = &rk616_pdata,
};

#endif

#ifdef CONFIG_ION
#define ION_RESERVE_SIZE        (80 * SZ_1M)
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

/**************************************************************************************************
 * SDMMC devices,  include the module of SD,MMC,and sdio.noted by xbw at 2012-03-05
**************************************************************************************************/
#ifdef CONFIG_SDMMC_RK29
#include "board-rk3168-86v-sdmmc-config.c"
#include "../plat-rk/rk-sdmmc-ops.c"
#include "../plat-rk/rk-sdmmc-wifi.c"
#endif //endif ---#ifdef CONFIG_SDMMC_RK29

#ifdef CONFIG_SDMMC0_RK29
static int rk29_sdmmc0_cfg_gpio(void)
{
#ifdef CONFIG_SDMMC_RK29_OLD
	iomux_set(MMC0_CMD);
	iomux_set(MMC0_CLKOUT);
	iomux_set(MMC0_D0);
	iomux_set(MMC0_D1);
	iomux_set(MMC0_D2);
	iomux_set(MMC0_D3);

	iomux_set_gpio_mode(iomux_mode_to_gpio(MMC0_DETN));

	gpio_request(RK30_PIN3_PA7, "sdmmc-power");
	gpio_direction_output(RK30_PIN3_PA7, GPIO_LOW);

#else
	rk29_sdmmc_set_iomux(0, 0xFFFF);

    #if defined(CONFIG_SDMMC0_RK29_SDCARD_DET_FROM_GPIO)
        #if SDMMC_USE_NEW_IOMUX_API
        iomux_set_gpio_mode(iomux_gpio_to_mode(RK29SDK_SD_CARD_DETECT_N));
        #else
        rk30_mux_api_set(RK29SDK_SD_CARD_DETECT_PIN_NAME, RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO);
        #endif
    #else
        #if SDMMC_USE_NEW_IOMUX_API       
        iomux_set(MMC0_DETN);
        #else
        rk30_mux_api_set(RK29SDK_SD_CARD_DETECT_PIN_NAME, RK29SDK_SD_CARD_DETECT_IOMUX_FMUX);
        #endif
    #endif	

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
	gpio_request(SDMMC0_WRITE_PROTECT_PIN, "sdmmc-wp");
	gpio_direction_input(SDMMC0_WRITE_PROTECT_PIN);
#endif

#endif

	return 0;
}

#define CONFIG_SDMMC0_USE_DMA
struct rk29_sdmmc_platform_data default_sdmmc0_data = {
	.host_ocr_avail =
	    (MMC_VDD_25_26 | MMC_VDD_26_27 | MMC_VDD_27_28 | MMC_VDD_28_29 |
	     MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33 |
	     MMC_VDD_33_34 | MMC_VDD_34_35 | MMC_VDD_35_36),
	.host_caps =
	    (MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED),
	.io_init = rk29_sdmmc0_cfg_gpio,

#if !defined(CONFIG_SDMMC_RK29_OLD)
	.set_iomux = rk29_sdmmc_set_iomux,
#endif

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
        #ifdef RK29SDK_SD_CARD_DETECT_PIN_NAME
        .iomux          = {
            .name       = RK29SDK_SD_CARD_DETECT_PIN_NAME,
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO
            .fgpio      = RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO,
            #endif
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FMUX
            .fmux       = RK29SDK_SD_CARD_DETECT_IOMUX_FMUX,
            #endif
        },
        #endif
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
#if defined(CONFIG_SDMMC_RK29_OLD)
	iomux_set(MMC1_CMD);
	iomux_set(MMC1_CLKOUT);
	iomux_set(MMC1_D0);
	iomux_set(MMC1_D1);
	iomux_set(MMC1_D2);
	iomux_set(MMC1_D3);
#else

#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
	gpio_request(SDMMC1_WRITE_PROTECT_PIN, "sdio-wp");
	gpio_direction_input(SDMMC1_WRITE_PROTECT_PIN);
#endif

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

#if !defined(CONFIG_SDMMC_RK29_OLD)
	.set_iomux = rk29_sdmmc_set_iomux,
#endif

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
	    #ifdef USE_SDIO_INT_LEVEL
        .sdio_INT_level = RK30SDK_WIFI_GPIO_WIFI_INT_B_ENABLE_VALUE,
        #endif
    #endif

    .det_pin_info = {    
#if defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
     #if defined(RK29SDK_SD_CARD_DETECT_N) || (INVALID_GPIO != RK29SDK_SD_CARD_DETECT_N)  
        .io             = RK29SDK_SD_CARD_DETECT_N,
     #else
         .io             = INVALID_GPIO,
     #endif   

        .enable         = RK29SDK_SD_CARD_INSERT_LEVEL,
        #ifdef RK29SDK_SD_CARD_DETECT_PIN_NAME
        .iomux          = {
            .name       = RK29SDK_SD_CARD_DETECT_PIN_NAME,
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO
            .fgpio      = RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO,
            #endif
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FMUX
            .fmux       = RK29SDK_SD_CARD_DETECT_IOMUX_FMUX,
            #endif
        },
        #endif
 #else
        .io             = INVALID_GPIO,
        .enable         = GPIO_LOW,
#endif
    },
   
	.enable_sd_wakeup = 0,
};
#endif //endif--#ifdef CONFIG_SDMMC1_RK29

/**************************************************************************************************
 * the end of setting for SDMMC devices
**************************************************************************************************/

#ifdef CONFIG_BATTERY_RK30_ADC_FAC
static struct rk30_adc_battery_platform_data rk30_adc_battery_platdata = {
/*
        .dc_det_pin      = RK30_PIN0_PB2,
        .batt_low_pin    = INVALID_GPIO, 
        .charge_set_pin  = INVALID_GPIO,
        .charge_ok_pin   = RK30_PIN0_PA6,
	 .usb_det_pin = INVALID_GPIO,
        .dc_det_level    = GPIO_LOW,
        .charge_ok_level = GPIO_HIGH,

	.reference_voltage = 1800, // the rK2928 is 3300;RK3066 and rk29 are 2500;rk3066B is 1800;
       .pull_up_res = 200,     //divider resistance ,  pull-up resistor
       .pull_down_res = 120, //divider resistance , pull-down resistor

	.is_reboot_charging = 1,
        .save_capacity   = 1 ,
        .low_voltage_protection = 3600,   
    */
};
 

static struct platform_device rk30_device_adc_battery = {
        .name   = "rk30-battery",
        .id     = -1,
        .dev = {
                .platform_data = &rk30_adc_battery_platdata,
        },
};
#endif
#ifdef CONFIG_RK30_PWM_REGULATOR
static int pwm_voltage_map[] = {
	800000,825000,850000, 875000,900000, 925000 ,950000, 975000,1000000, 1025000, 1050000, 1075000, 1100000, 1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000, 1325000, 1350000,1375000
};

static struct regulator_consumer_supply pwm_dcdc1_consumers[] = {
	{
		.supply = "vdd_cpu",
	}
};

struct regulator_init_data pwm_regulator_init_dcdc[1] =
{
	{
		.constraints = {
			.name = "PWM_DCDC1",
			.min_uV = 600000,
			.max_uV = 1800000,	//0.6-1.8V
			.apply_uV = true,
			.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
		},
		.num_consumer_supplies = ARRAY_SIZE(pwm_dcdc1_consumers),
		.consumer_supplies = pwm_dcdc1_consumers,
	},
};

static struct pwm_platform_data pwm_regulator_info[1] = {
	{
		.pwm_id = 1,
		.pwm_gpio = RK30_PIN3_PD4,
		.pwm_iomux_pwm = PWM1,
		.pwm_iomux_gpio = GPIO3_D4,
		.pwm_voltage = 1100000,
		.suspend_voltage = 1000000,
		.min_uV = 800000,
		.max_uV	= 1375000,
		.coefficient = 575,	//55.0%
		.pwm_voltage_map = pwm_voltage_map,
		.init_data	= &pwm_regulator_init_dcdc[0],
	},
};

struct platform_device pwm_regulator_device[1] = {
	{
		.name = "pwm-voltage-regulator",
		.id = 0,
		.dev		= {
			.platform_data = &pwm_regulator_info[0],
		}
	},
};
#endif

#ifdef CONFIG_RFKILL_RK
// bluetooth rfkill device, its driver in net/rfkill/rfkill-rk.c
static struct rfkill_rk_platform_data rfkill_rk_platdata = {
    .type               = RFKILL_TYPE_BLUETOOTH,

    .poweron_gpio       = { // BT_REG_ON
        .io             = INVALID_GPIO, //RK30_PIN3_PC7,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "bt_poweron",
            .fgpio      = GPIO3_C7,
        },
    },

    .reset_gpio         = { // BT_RST
        .io             = RK30_PIN3_PD1, // set io to INVALID_GPIO for disable it
        .enable         = GPIO_LOW,
        .iomux          = {
            .name       = "bt_reset",
            .fgpio      = GPIO3_D1,
       },
   }, 

    .wake_gpio          = { // BT_WAKE, use to control bt's sleep and wakeup
        .io             = RK30_PIN3_PC6, // set io to INVALID_GPIO for disable it
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "bt_wake",
            .fgpio      = GPIO3_C6,
        },
    },

    .wake_host_irq      = { // BT_HOST_WAKE, for bt wakeup host when it is in deep sleep
        .gpio           = {
            .io         = RK30_PIN0_PA5, // set io to INVALID_GPIO for disable it
            .enable     = GPIO_LOW,      // set GPIO_LOW for falling, set 0 for rising
            .iomux      = {
                .name   = NULL,
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
    },
};

static struct platform_device device_rfkill_rk = {
    .name   = "rfkill_rk",
    .id     = -1,
    .dev    = {
        .platform_data = &rfkill_rk_platdata,
    },
};
#endif

#if defined(CONFIG_GPS_RK)
int rk_gps_io_init(void)
{
	printk("%s \n", __FUNCTION__);
	
	rk30_mux_api_set(GPIO1B5_UART3RTSN_NAME, GPIO1B_GPIO1B5);//VCC_EN
	gpio_request(RK30_PIN1_PB5, NULL);
	gpio_direction_output(RK30_PIN1_PB5, GPIO_LOW);

	rk30_mux_api_set(GPIO1B4_UART3CTSN_GPSRFCLK_NAME, GPIO1B_GPSRFCLK);//GPS_CLK
	rk30_mux_api_set(GPIO1B2_UART3SIN_GPSMAG_NAME, GPIO1B_GPSMAG);//GPS_MAG
	rk30_mux_api_set(GPIO1B3_UART3SOUT_GPSSIG_NAME, GPIO1B_GPSSIG);//GPS_SIGN

	rk30_mux_api_set(GPIO1A6_UART1CTSN_SPI0CLK_NAME, GPIO1A_GPIO1A6);//SPI_CLK
	gpio_request(RK30_PIN1_PA6, NULL);
	gpio_direction_output(RK30_PIN1_PA6, GPIO_LOW);

	rk30_mux_api_set(GPIO1A5_UART1SOUT_SPI0TXD_NAME, GPIO1A_GPIO1A5);//SPI_MOSI
	gpio_request(RK30_PIN1_PA5, NULL);
	gpio_direction_output(RK30_PIN1_PA5, GPIO_LOW);	

	rk30_mux_api_set(GPIO1A7_UART1RTSN_SPI0CSN0_NAME, GPIO1A_GPIO1A7);//SPI_CS
	gpio_request(RK30_PIN1_PA7, NULL);
	gpio_direction_output(RK30_PIN1_PA7, GPIO_LOW);		
	return 0;
}
int rk_gps_power_up(void)
{
	printk("%s \n", __FUNCTION__);

	return 0;
}

int rk_gps_power_down(void)
{
	printk("%s \n", __FUNCTION__);

	return 0;
}

int rk_gps_reset_set(int level)
{
	return 0;
}
int rk_enable_hclk_gps(void)
{
	printk("%s \n", __FUNCTION__);
	clk_enable(clk_get(NULL, "hclk_gps"));
	return 0;
}
int rk_disable_hclk_gps(void)
{
	printk("%s \n", __FUNCTION__);
	clk_disable(clk_get(NULL, "hclk_gps"));
	return 0;
}
struct rk_gps_data rk_gps_info = {
	.io_init = rk_gps_io_init,
	.power_up = rk_gps_power_up,
	.power_down = rk_gps_power_down,
	.reset = rk_gps_reset_set,
	.enable_hclk_gps = rk_enable_hclk_gps,
	.disable_hclk_gps = rk_disable_hclk_gps,
	.GpsSign = RK30_PIN1_PB3,
	.GpsMag = RK30_PIN1_PB2,        //GPIO index
	.GpsClk = RK30_PIN1_PB4,        //GPIO index
	.GpsVCCEn = RK30_PIN1_PB5,     //GPIO index
	.GpsSpi_CSO = RK30_PIN1_PA4,    //GPIO index
	.GpsSpiClk = RK30_PIN1_PA5,     //GPIO index
	.GpsSpiMOSI = RK30_PIN1_PA7,	  //GPIO index
	.GpsIrq = IRQ_GPS,
	.GpsSpiEn = 0,
	.GpsAdcCh = 2,
	.u32GpsPhyAddr = RK30_GPS_PHYS,
	.u32GpsPhySize = RK30_GPS_SIZE,
};

struct platform_device rk_device_gps = {
	.name = "gps_hv5820b",
	.id = -1,
	.dev		= {
	.platform_data = &rk_gps_info,
		}
	};
#endif

#if defined(CONFIG_MT5931_MT6622)
static struct mt6622_platform_data mt6622_platdata = {
		    .power_gpio         = { // BT_REG_ON
		    	.io             = RK30_PIN3_PC7, // set io to INVALID_GPIO for disable it
			    .enable         = GPIO_HIGH,
			    .iomux          = {
				    .name       = NULL,
				},
		    },

		    .reset_gpio         = { // BT_RST
		        .io             = RK30_PIN3_PD1,
		        .enable         = GPIO_HIGH,
		        .iomux          = {
		            .name       = NULL,
		        },
		    },

		    .irq_gpio           = {
			    .io             = RK30_PIN0_PA5,
			    .enable         = GPIO_LOW,
			    .iomux          = {
				    .name       = NULL,
				},
    },

    .rts_gpio           = { // UART_RTS
        .io             = RK30_PIN1_PA3,
        .enable         = GPIO_LOW,
        .iomux          = {
            .name       = "bt_rts",
            .fgpio      = GPIO1_A3,
            .fmux       = UART0_RTSN,
        },
    },
};

static struct platform_device device_mt6622 = {
		    .name   = "mt6622",
			.id     = -1,
			.dev    = {
			       .platform_data = &mt6622_platdata,
			},
};	
#endif
#if defined CONFIG_TCC_BT_DEV
static struct tcc_bt_platform_data tcc_bt_platdata = {

    .power_gpio   = { // ldoon
        .io             =  RK30_PIN3_PC0,//difined depend on your harware
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = NULL,
            },
        },

    .wake_host_gpio  = { // BT_HOST_WAKE, for bt wakeup host when it is in deep sleep
        .io         = RK30_PIN0_PC5, // set io to INVALID_GPIO for disable it,it's depend on your hardware
        .enable     = IRQF_TRIGGER_RISING,// set IRQF_TRIGGER_FALLING for falling, set IRQF_TRIGGER_RISING for rising
        .iomux      = {
            .name       = NULL,
        },
    },
};

static struct platform_device device_tcc_bt = {
    .name   = "tcc_bt_dev",
    .id     = -1,
    .dev    = {
        .platform_data = &tcc_bt_platdata,
        },
};
#endif

static struct platform_device *devices[] __initdata = {

#ifdef CONFIG_ION
	&device_ion,
#endif
#ifdef CONFIG_WIFI_CONTROL_FUNC
	&rk29sdk_wifi_device,
#endif

#if defined(CONFIG_MT6620)
	    &mt3326_device_gps,
#endif

#ifdef CONFIG_BATTERY_RK30_ADC_FAC
 	&rk30_device_adc_battery,
#endif
#ifdef CONFIG_RFKILL_RK
	&device_rfkill_rk,
#endif
#ifdef CONFIG_GPS_RK
	&rk_device_gps,
#endif
#ifdef CONFIG_MT5931_MT6622
	&device_mt6622,
#endif
#ifdef CONFIG_TCC_BT_DEV
        &device_tcc_bt,
#endif
};


static int rk_platform_add_display_devices(void)
{
	struct platform_device *fb = NULL;  //fb
	struct platform_device *lcdc0 = NULL; //lcdc0
	struct platform_device *lcdc1 = NULL; //lcdc1
	struct platform_device *bl = NULL; //backlight
#ifdef CONFIG_FB_ROCKCHIP
	fb = &device_fb;
#endif

#if defined(CONFIG_LCDC0_RK3066B)
	lcdc0 = &device_lcdc0,
#endif

#if defined(CONFIG_LCDC1_RK3066B)
	lcdc1 = &device_lcdc1,
#endif

#ifdef CONFIG_BACKLIGHT_RK29_BL
	bl = &rk29_device_backlight,
#endif
	__rk_platform_add_display_devices(fb,lcdc0,lcdc1,bl);

	return 0;
	
}

// i2c
#ifdef CONFIG_I2C0_RK30
static struct i2c_board_info __initdata i2c0_info[] = {
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
#if defined (CONFIG_SND_SOC_RK1000)
	{
		.type          = "rk1000_i2c_codec",
		.addr          = 0x60,
		.flags         = 0,
	},
	{
		.type          = "rk1000_control",
		.addr          = 0x40,
		.flags         = 0,
	},
#endif
#if defined (CONFIG_SND_SOC_RT5616)
        {
                .type                   = "rt5616",
                .addr                   = 0x1b,
                .flags                  = 0,
        },
#endif
};
#endif

int __sramdata g_pmic_type =  0;
#ifdef CONFIG_I2C1_RK30
#ifdef CONFIG_REGULATOR_ACT8846
#define PMU_POWER_SLEEP RK30_PIN0_PA1
#define ACT8846_HOST_IRQ                RK30_PIN0_PB3

static struct pmu_info  act8846_dcdc_info[] = {
	{
		.name          = "act_dcdc1",   //ddr
		.min_uv          = 1200000,
		.max_uv         = 1200000,
		#ifdef CONFIG_ACT8846_SUPPORT_RESET
		.suspend_vol  =   1200000,
		#else
		.suspend_vol  =   900000,
		#endif
	},
	{
		.name          = "vdd_core",    //logic
		.min_uv          = 1000000,
		.max_uv         = 1000000,
		#ifdef CONFIG_ACT8846_SUPPORT_RESET
		.suspend_vol  =  1200000,
		#else
		.suspend_vol  =  900000,
		#endif
	},
	{
		.name          = "vdd_cpu",   //arm
		.min_uv          = 1000000,
		.max_uv         = 1000000,
		#ifdef CONFIG_ACT8846_SUPPORT_RESET
		.suspend_vol  =  1200000,
		#else
		.suspend_vol  =  900000,
		#endif
	},
	{
		.name          = "act_dcdc4",   //vccio
		.min_uv          = 3000000,
		.max_uv         = 3000000,
		#ifdef CONFIG_ACT8846_SUPPORT_RESET
		.suspend_vol  =  3000000,
		#else
		.suspend_vol  =  2800000,
		#endif
	},
	
};
static  struct pmu_info  act8846_ldo_info[] = {
	{
		.name          = "act_ldo1",   //vdd10
		.min_uv          = 1000000,
		.max_uv         = 1000000,
	},
	{
		.name          = "act_ldo2",    //vdd12
		.min_uv          = 1200000,
		.max_uv         = 1200000,
	},
	{
		.name          = "act_ldo3",   //vcc18_cif
		.min_uv          = 1800000,
		.max_uv         = 1800000,
	},
	{
		.name          = "act_ldo4",   //vcca33
		.min_uv          = 3300000,
		.max_uv         = 3300000,
	},
	{
		.name          = "act_ldo5",   //vcctp
		.min_uv          = 3300000,
		.max_uv         = 3300000,
	},
	{
		.name          = "act_ldo6",   //vcc_jetta
		.min_uv          = 3300000,
		.max_uv         = 3300000,
	},
	{
		.name          = "act_ldo7",   //vcc18
		.min_uv          = 1800000,
		.max_uv         = 1800000,
	},
	{
		.name          = "act_ldo8",   //vcc28_cif
		.min_uv          = 2800000,
		.max_uv         = 2800000,
	},
 };

#include "board-pmu-act8846.c"
#endif

#ifdef CONFIG_MFD_WM831X_I2C
#define PMU_POWER_SLEEP 		RK30_PIN0_PA1 

static struct pmu_info  wm8326_dcdc_info[] = {
	{
		.name          = "vdd_core",   //logic
		.min_uv          = 1000000,
		.max_uv         = 1000000,
		.suspend_vol  =  950000,
	},
	{
		.name          = "vdd_cpu",    //arm
		.min_uv          = 1000000,
		.max_uv         = 1000000,
		.suspend_vol  =  950000,
	},
	{
		.name          = "dcdc3",   //ddr
		.min_uv          = 1150000,
		.max_uv         = 1150000,
		.suspend_vol  =  1150000,
	},
	#ifdef CONFIG_MACH_RK3066_SDK
	{
		.name          = "dcdc4",   //vcc_io
		.min_uv          = 3300000,
		.max_uv         = 3300000,
		.suspend_vol  =  3000000,
	},
	#else
	{
		.name          = "dcdc4",   //vcc_io
		.min_uv          = 3000000,
		.max_uv         = 3000000,
		.suspend_vol  =  2800000,
	},
	#endif
};

static struct pmu_info  wm8326_ldo_info[] = {
	{
		.name          = "ldo1",   //vcc18_cif
		.min_uv          = 1800000,
		.max_uv         = 1800000,
		.suspend_vol  =  1800000,
	},
	{
		.name          = "ldo2",    //vccio_wl
		.min_uv          = 1800000,
		.max_uv         = 1800000,
		.suspend_vol  =  1800000,
	},
	{
		.name          = "ldo3",   //
		.min_uv          = 1100000,
		.max_uv         = 1100000,
		.suspend_vol  =  1100000,
	},
	{
		.name          = "ldo4",   //vdd11
		.min_uv          = 1000000,
		.max_uv         = 1000000,
		.suspend_vol  =  1000000,
	},
	{
		.name          = "ldo5",   //vcc25
		.min_uv          = 1800000,
		.max_uv         = 1800000,
		.suspend_vol  =  1800000,
	},
	{
		.name          = "ldo6",   //vcc33
		.min_uv          = 3300000,
		.max_uv         = 3300000,
		.suspend_vol  =  3300000,
	},
	{
		.name          = "ldo7",   //vcc28_cif
		.min_uv          = 2800000,
		.max_uv         = 2800000,
		.suspend_vol  =  2800000,
	},
	{
		.name          = "ldo8",   //vcca33
		.min_uv          = 3300000,
		.max_uv         = 3300000,
		.suspend_vol  =  3300000,
	},
	{
		.name          = "ldo9",   //vcc_tp
		.min_uv          = 3300000,
		.max_uv         = 3300000,
		.suspend_vol  =  3300000,
	},
	{
		.name          = "ldo10",   //flash_io
		.min_uv          = 1800000,
		.max_uv         = 1800000,
		.suspend_vol  =  1800000,
	},
};

#include "board-pmu-wm8326.c"
#endif

#ifdef CONFIG_MFD_TPS65910
#ifdef CONFIG_ARCH_RK3066B
#define TPS65910_HOST_IRQ        RK30_PIN0_PB3
#else
#define TPS65910_HOST_IRQ        RK30_PIN6_PA4
#endif

#define PMU_POWER_SLEEP RK30_PIN0_PA1

static struct pmu_info  tps65910_dcdc_info[] = {
	{
		.name          = "vdd_core",   //logic
		.min_uv          = 1200000,
		.max_uv         = 1200000,
	},
	{
		.name          = "vdd_cpu",    //arm
		.min_uv          = 1200000,
		.max_uv         = 1200000,
	},
	{
		.name          = "vio",   //vcc_io
		.min_uv          = 3000000,
		.max_uv         = 3000000,
	},
	
};
static  struct pmu_info  tps65910_ldo_info[] = {
/*	{
		.name          = "vpll",   //vdd10
		.min_uv          = 1000000,
		.max_uv         = 1000000,
	},*/
	{
		.name          = "vdig1",    //vcc18_cif
		.min_uv          = 1800000,
		.max_uv         = 1800000,
	},
#ifdef CONFIG_MFD_RK616
	{
		.name          = "vdig2",   //vdd11//1.0->1.2 for rk616 vdd_core lch
		.min_uv          = 1200000,
		.max_uv         = 1200000,
	},
#else
	{
		.name          = "vdig2",   //vdd11
		.min_uv          = 1000000,
		.max_uv         = 1000000,
	},
#endif	
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
	{
		.name          = "vmmc",   //vcca30
		.min_uv          = 3000000,
		.max_uv         = 3000000,
	},
	{
		.name          = "vdac",   //vcc18
		.min_uv          = 1800000,
		.max_uv         = 1800000,
	},
 };

#include "board-pmu-tps65910.c"
#endif
#ifdef CONFIG_REGULATOR_ACT8931
#define ACT8931_HOST_IRQ		RK30_PIN0_PB5//depend on your hardware


#define ACT8931_CHGSEL_PIN RK30_PIN0_PD0 //depend on your hardware


static struct pmu_info  act8931_dcdc_info[] = {
	{
		.name          = "vdd_core",   //vdd_logic
		.min_uv          = 1200000,
		.max_uv         = 1200000,
	},
	{
		.name          = "act_dcdc2",    //ddr
		.min_uv          = 1500000,
		.max_uv         = 1500000,
	},
	{
		.name          = "vdd_cpu",   //vdd_arm
		.min_uv          = 1200000,
		.max_uv         = 1200000,
	},
	
};
static  struct pmu_info  act8931_ldo_info[] = {
	{
		.name          = "act_ldo1",   //vcc28_cif
		.min_uv          = 2800000,
		.max_uv         = 2800000,
	},
	{
		.name          = "act_ldo2",    //vcc18_cif
		.min_uv          = 1800000,
		.max_uv         = 1800000,
	},
	{
		.name          = "act_ldo3",    //vcca30
		.min_uv          = 3000000,
		.max_uv         = 3000000,
	},
	{
		.name          = "act_ldo4",    //vcc_wl
		.min_uv          = 3300000,
		.max_uv         = 3300000,
	},
};
#include "board-rk30-sdk-act8931.c"
#endif

static struct i2c_board_info __initdata i2c1_info[] = {
#if defined (CONFIG_REGULATOR_ACT8846)
	{
		.type    		= "act8846",
		.addr           = 0x5a, 
		.flags			= 0,
		.irq            = ACT8846_HOST_IRQ,
		.platform_data=&act8846_data,
	},
#endif
#if defined (CONFIG_RTC_HYM8563)
	{
		.type                   = "rtc_hym8563",
		.addr           = 0x51,
		.flags                  = 0,
		.irq            = RK30_PIN0_PB5,
	},
#endif
#if defined (CONFIG_MFD_WM831X_I2C)
	{
		.type          = "wm8326",
		.addr          = 0x34,
		.flags         = 0,
		.irq           = RK30_PIN0_PB3,
		.platform_data = &wm831x_platdata,
	},
#endif
#if defined (CONFIG_MFD_TPS65910)
	{
        .type           = "tps65910",
        .addr           = TPS65910_I2C_ID0,
        .flags          = 0,
        .irq            = TPS65910_HOST_IRQ,
    	.platform_data = &tps65910_data,
	},
#endif
#if defined (CONFIG_REGULATOR_ACT8931)
	{
		.type    		= "act8931",
		.addr           = 0x5b, 
		.flags			= 0,
		.irq            = ACT8931_HOST_IRQ,
		.platform_data=&act8931_data,
	},
#endif
};
#endif

void __sramfunc board_pmu_suspend(void)
{      
        #if defined (CONFIG_REGULATOR_ACT8846)
       if(pmic_is_act8846())
       board_pmu_act8846_suspend(); 
       #endif
	#if defined (CONFIG_MFD_WM831X_I2C)
       if(pmic_is_wm8326())
       board_pmu_wm8326_suspend();
	#endif
	#if defined (CONFIG_MFD_TPS65910)
       if(pmic_is_tps65910())
       board_pmu_tps65910_suspend(); 
    #endif   
}

void __sramfunc board_pmu_resume(void)
{      
        #if defined (CONFIG_REGULATOR_ACT8846)
       if(pmic_is_act8846())
       board_pmu_act8846_resume(); 
       #endif
	#if defined (CONFIG_MFD_WM831X_I2C)
       if(pmic_is_wm8326())
       board_pmu_wm8326_resume();
	#endif
	#if defined (CONFIG_MFD_TPS65910)
       if(pmic_is_tps65910())
       board_pmu_tps65910_resume(); 
	#endif
}

 int __sramdata gpio3d6_iomux,gpio3d6_do,gpio3d6_dir,gpio3d6_en;

#define grf_readl(offset)	readl_relaxed(RK30_GRF_BASE + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK30_GRF_BASE + offset); dsb(); } while (0)
 
void __sramfunc rk30_pwm_logic_suspend_voltage(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR

//	int gpio0d7_iomux,gpio0d7_do,gpio0d7_dir,gpio0d7_en;
	sram_udelay(10000);
	gpio3d6_iomux = grf_readl(GRF_GPIO3D_IOMUX);
	gpio3d6_do = grf_readl(GRF_GPIO3H_DO);
	gpio3d6_dir = grf_readl(GRF_GPIO3H_DIR);
	gpio3d6_en = grf_readl(GRF_GPIO3H_EN);

	grf_writel((1<<28), GRF_GPIO3D_IOMUX);
	grf_writel((1<<30)|(1<<14), GRF_GPIO3H_DIR);
	grf_writel((1<<30)|(1<<14), GRF_GPIO3H_DO);
	grf_writel((1<<30)|(1<<14), GRF_GPIO3H_EN);
#endif 
}
void __sramfunc rk30_pwm_logic_resume_voltage(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR
	grf_writel((1<<28)|gpio3d6_iomux, GRF_GPIO3D_IOMUX);
	grf_writel((1<<30)|gpio3d6_en, GRF_GPIO3H_EN);
	grf_writel((1<<30)|gpio3d6_dir, GRF_GPIO3H_DIR);
	grf_writel((1<<30)|gpio3d6_do, GRF_GPIO3H_DO);
	sram_udelay(10000);

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


#ifdef CONFIG_I2C2_RK30
static struct i2c_board_info __initdata i2c2_info[] = {
#if defined (CONFIG_CT36X_TS)
	{
		.type	       = CT36X_NAME,
		.addr          = 0x01,
		.flags         = 0,
		.platform_data = &ct36x_info,
	},
#endif
#if defined (CONFIG_LS_CM3217)
	{
		.type          = "lightsensor",
		.addr          = 0x10,
		.flags         = 0,
		.platform_data = &cm3217_info,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_GT82X_IIC)
	{
		.type          = "Goodix-TS-82X",
		.addr          = 0x5D,
		.flags         = 0,
		.irq           = RK30_PIN1_PB7,
		.platform_data = &ts82x_pdata,
	},
#endif
#if defined(CONFIG_HDMI_CAT66121)
        {
                .type           = "cat66121_hdmi",
                .addr           = 0x4c,
                .flags          = 0,
                .irq            = RK30_PIN2_PD6,
                .platform_data  = &rk_hdmi_pdata,
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
#ifdef CONFIG_MFD_RK610
		{
			.type			= "rk610_ctl",
			.addr			= 0x40,
			.flags			= 0,
			.platform_data		= &rk610_ctl_pdata,
		},
#ifdef CONFIG_RK610_TVOUT
		{
			.type			= "rk610_tvout",
			.addr			= 0x42,
			.flags			= 0,
		},
#endif
#ifdef CONFIG_HDMI_RK610
		{
			.type			= "rk610_hdmi",
			.addr			= 0x46,
			.flags			= 0,
			.irq			= INVALID_GPIO,
		},
#endif
#ifdef CONFIG_SND_SOC_RK610
		{//RK610_CODEC addr  from 0x60 to 0x80 (0x60~0x80)
			.type			= "rk610_i2c_codec",
			.addr			= 0x60,
			.flags			= 0,
			.platform_data		= &rk610_codec_pdata,			
		},
#endif
#endif

};

#if defined (CONFIG_SND_SOC_RT5616)
        {
                .type                   = "rt5616",
                .addr                   = 0x1b,
                .flags                  = 0,
        },
#endif
#endif

#ifdef CONFIG_I2C_GPIO_RK30
#define I2C_SDA_PIN     INVALID_GPIO// RK30_PIN2_PD6   //set sda_pin here
#define I2C_SCL_PIN     INVALID_GPIO//RK30_PIN2_PD7   //set scl_pin here
static int rk30_i2c_io_init(void)
{
        //set iomux (gpio) here
        //rk30_mux_api_set(GPIO2D7_I2C1SCL_NAME, GPIO2D_GPIO2D7);
        //rk30_mux_api_set(GPIO2D6_I2C1SDA_NAME, GPIO2D_GPIO2D6);

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
#ifdef CONFIG_I2C4_RK30
	i2c_register_board_info(4, i2c4_info, ARRAY_SIZE(i2c4_info));
#endif
#ifdef CONFIG_I2C_GPIO_RK30
	i2c_register_board_info(5, i2c_gpio_info, ARRAY_SIZE(i2c_gpio_info));
#endif
}
//end of i2c
#define USB_INSERT_FAKE_SHUTDOWN
#if defined(USB_INSERT_FAKE_SHUTDOWN)
extern int rk30_pm_enter(suspend_state_t state);
int __sramdata charge_power_off = 0;

static void rk30_charge_deal(void)
{	
	struct regulator *ldo = NULL;
	int ret;
	charge_power_off = 1;
		
	//ldo = regulator_get(NULL, "ldo9");	// shutdown tp power		
	//regulator_disable(ldo); 	
	//regulator_put(ldo); 		
	//gpio_set_value(TOUCH_RESET_PIN,  GPIO_LOW);
	rk29_backlight_pwm_suspend();//shutdown backlight
	rk_fb_io_disable();  //shutdown lcd
	
	local_irq_disable();
	local_fiq_disable();

	rk30_pm_enter(PM_SUSPEND_MEM);

	if(charge_power_off == 1)
	{
		arm_pm_restart(0, NULL);
	}
}
#else
static void rk30_charge_deal(void){
}
#endif 


#define POWER_ON_PIN RK30_PIN0_PA0   //power_hold
static void rk30_pm_power_off(void)
{
	printk(KERN_ERR "rk30_pm_power_off start...\n");
	gpio_direction_output(get_port_config(pwr_on).gpio, GPIO_LOW);
	
       if (pmic_is_tps65910()) {
               printk("enter dcdet pmic_is_tps65910===========\n");
               if(gpio_get_value (get_port_config(dc_det).gpio) == GPIO_LOW)
               {
                       printk("enter restart===========\n");
                       arm_pm_restart(0, "charge");
               }else if(gpio_get_value (RK30_PIN0_PA7) == GPIO_LOW){//usb in
			printk("debug: detect dc_det LOW, charging!\n");
			rk30_charge_deal();
		  }
               tps65910_device_shutdown();
       }else if(pmic_is_act8846()){
		 printk("enter dcdet pmic_is_act8846===========\n");
               if(gpio_get_value (get_port_config(dc_det).gpio) == GPIO_LOW)
               {
                       printk("enter restart===========\n");
                       arm_pm_restart(0, "charge");
               }
              act8846_device_shutdown();
	}else if(pmic_is_wm8326()){
		 printk("enter dcdet pmic_is_wm8326===========\n");
               if(gpio_get_value (get_port_config(dc_det).gpio) == GPIO_LOW)
               {
                       printk("enter restart===========\n");
                       arm_pm_restart(0, "charge");
               }
		wm831x_set_bits(Wm831x,WM831X_GPIO_LEVEL,0x0001,0x0000);  //set sys_pwr 0
		wm831x_device_shutdown(Wm831x);//wm8326 shutdown
	}else if(pmic_is_act8931()){
		 printk("enter dcdet pmic_is_act8931===========\n");
               if(gpio_get_value (get_port_config(dc_det).gpio) == GPIO_LOW)
               {
                       printk("enter restart===========\n");
                       arm_pm_restart(0, "charge");
               }
              act8931_device_shutdown();
	}
	while (1);
}

static int __init tp_board_init(void)
{
	struct port_config irq_port;
	struct port_config rst_port;
	int ret = check_tp_param();

	if(ret < 0)
	    return ret;

  	 irq_port = get_port_config(tp_irq);
	 rst_port = get_port_config(tp_rst);
#if defined (CONFIG_TOUCHSCREEN_GSLX680_RK3168)
	if(tp_type == TP_TYPE_GSLX680){		 
		gslx680_data.irq_pin = irq_port.gpio;
		gslx680_info.addr = tp_addr; 
		gslx680_data.reset_pin= rst_port.gpio;
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
    return 0;
}


static int __init codec_board_init(void)
{
	struct port_config spk_port;
	struct port_config hp_port;
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
#if defined (CONFIG_GS_MMA8452)        
	if(gs_type == GS_TYPE_MMA8452){                
		mma8452_info.irq = port.gpio;                
		mma8452_info.addr = gs_addr;                
		for(i = 0; i < 9; i++)                        
			mma8452_data.orientation[i] = gs_orig[i];	        
		i2c_register_board_info(gs_i2c, &mma8452_info, 1);        
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
		case 3:
			bl_pwm_mode = PWM3;
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

static void __init rk_board_init(void)
{
	avs_init();
	rk_power_on();
	rk_config_init();
	
	//pm_power_off = rk30_pm_power_off;

	rk30_i2c_register_board_info();
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
	platform_add_devices(devices, ARRAY_SIZE(devices));
	rk_platform_add_display_devices();
	board_usb_detect_init(RK30_PIN0_PA7);
	  
#ifdef CONFIG_WIFI_CONTROL_FUNC
	rk29sdk_wifi_bt_gpio_control_init();
#endif

#if defined(CONFIG_MT6620)
	    clk_set_rate(clk_get_sys("rk_serial.1", "uart"), 48*1000000);
#endif

#if defined(CONFIG_MT5931_MT6622)
		clk_set_rate(clk_get_sys("rk_serial.0", "uart"), 24*1000000);
#endif	
		
}

static void __init rk30_reserve(void)
{
#ifdef CONFIG_ION	
		rk30_ion_pdata.heaps[0].base = board_mem_reserve_add("ion", ION_RESERVE_SIZE);	
#endif

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


#ifdef CONFIG_VIDEO_RK29
	rk30_camera_request_reserve_mem();
#endif
	
#ifdef CONFIG_GPS_RK
	//it must be more than 8MB
	rk_gps_info.u32MemoryPhyAddr = board_mem_reserve_add("gps", SZ_8M);
#endif
	board_mem_reserved();
	
}

/**
 * dvfs_cpu_logic_table: table for arm and logic dvfs 
 * @frequency	: arm frequency
 * @cpu_volt	: arm voltage depend on frequency
 * @logic_volt	: logic voltage arm requests depend on frequency
 * comments	: min arm/logic voltage
 */
#ifdef CONFIG_DVFS_WITH_UOC
//chenxing uoc
static struct cpufreq_frequency_table dvfs_arm_table[] = {
	{.frequency = 312 * 1000,       .index = 950 * 1000},
	{.frequency = 504 * 1000,       .index = 1000 * 1000},
	{.frequency = 816 * 1000,       .index = 1050 * 1000},
	{.frequency = 1008 * 1000,      .index = 1125 * 1000},
	{.frequency = 1200 * 1000,      .index = 1200 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dvfs_gpu_table[] = {
	{.frequency = 100 * 1000,	.index = 1000 * 1000},
	{.frequency = 200 * 1000,	.index = 1000 * 1000},
	{.frequency = 266 * 1000,	.index = 1050 * 1000},
	//{.frequency = 300 * 1000,	.index = 1050 * 1000},
	{.frequency = 400 * 1000,	.index = 1125 * 1000},
        {.frequency = 600 * 1000,       .index = 1250 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dvfs_ddr_table[] = {
	{.frequency = 200 * 1000 + DDR_FREQ_SUSPEND,    .index = 1000 * 1000},
	{.frequency = 300 * 1000 + DDR_FREQ_VIDEO,      .index = 1050 * 1000},
	{.frequency = 400 * 1000 + DDR_FREQ_NORMAL,     .index = 1100 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};
#else 
//chenliang
static struct cpufreq_frequency_table dvfs_arm_table[] = {
	{.frequency = 312 * 1000,       .index = 1025 * 1000},
	{.frequency = 504 * 1000,       .index = 1025 * 1000},
	{.frequency = 816 * 1000,       .index = 1050 * 1000},
	{.frequency = 1008 * 1000,      .index = 1125 * 1000},
	{.frequency = 1200 * 1000,      .index = 1200 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dvfs_gpu_table[] = {
	{.frequency = 100 * 1000,	.index = 1000 * 1000},
	{.frequency = 200 * 1000,	.index = 1000 * 1000},
	{.frequency = 266 * 1000,	.index = 1050 * 1000},
	{.frequency = 300 * 1000,	.index = 1050 * 1000},
	{.frequency = 400 * 1000,	.index = 1125 * 1000},
        {.frequency = 600 * 1000,       .index = 1250 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dvfs_ddr_table[] = {
	{.frequency = 200 * 1000 + DDR_FREQ_SUSPEND,    .index = 1000 * 1000},
	{.frequency = 240 * 1000 + DDR_FREQ_VIDEO,      .index = 1050 * 1000},
	{.frequency = 300 * 1000 + DDR_FREQ_NORMAL,     .index = 1075 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};
#endif
//#define DVFS_CPU_TABLE_SIZE	(ARRAY_SIZE(dvfs_cpu_logic_table))
//static struct cpufreq_frequency_table cpu_dvfs_table[DVFS_CPU_TABLE_SIZE];
//static struct cpufreq_frequency_table dep_cpu2core_table[DVFS_CPU_TABLE_SIZE];
int get_max_freq(struct cpufreq_frequency_table *table)
{
	int i,temp=0;
	
	for(i=0;table[i].frequency!= CPUFREQ_TABLE_END;i++)
	{
		if(temp<table[i].frequency)
			temp=table[i].frequency;
	}	
	printk("get_max_freq=%d\n",temp);
	return temp;
}

void __init board_clock_init(void)
{
	u32 flags=RK30_CLOCKS_DEFAULT_FLAGS;
#if !defined(CONFIG_ARCH_RK3188)
	if(get_max_freq(dvfs_gpu_table)<=(400*1000))
	{	
		flags=RK30_CLOCKS_DEFAULT_FLAGS|CLK_GPU_GPLL;
	}
	else
		flags=RK30_CLOCKS_DEFAULT_FLAGS|CLK_GPU_CPLL;
#endif
	rk30_clock_data_init(periph_pll_default, codec_pll_default, flags);
	//dvfs_set_arm_logic_volt(dvfs_cpu_logic_table, cpu_dvfs_table, dep_cpu2core_table);
	dvfs_set_freq_volt_table(clk_get(NULL, "cpu"), dvfs_arm_table);
	dvfs_set_freq_volt_table(clk_get(NULL, "gpu"), dvfs_gpu_table);
	dvfs_set_freq_volt_table(clk_get(NULL, "ddr"), dvfs_ddr_table);
}

MACHINE_START(RK30, "RK30board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= rk30_fixup,
	.reserve	= &rk30_reserve,
	.map_io		= rk30_map_io,
	.init_irq	= rk30_init_irq,
	.timer		= &rk30_timer,
	.init_machine	= rk_board_init,
MACHINE_END
