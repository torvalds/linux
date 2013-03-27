/* arch/arm/mach-rk2928/board-rk2928-fpga.c
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
#include <mach/config.h>
#include <linux/fb.h>
#include <linux/regulator/machine.h>
#include <linux/rfkill-rk.h>
#include <linux/sensor-dev.h>
#include <linux/mfd/tps65910.h>
#include <linux/regulator/act8931.h>
#include <linux/regulator/rk29-pwm-regulator.h>
#if defined(CONFIG_MODEM_SOUND)
#include "../../../drivers/misc/modem_sound.h"
#endif
#if defined(CONFIG_HDMI_RK30)
	#include "../../../drivers/video/rockchip/hdmi/rk_hdmi.h"
#endif
#include "../../../drivers/headset_observe/rk_headset.h"

#if defined(CONFIG_SPIM_RK29)
#include "../../../drivers/spi/rk29_spim.h"
#endif
#ifdef CONFIG_SND_SOC_RK2928
#include "../../../sound/soc/codecs/rk2928_codec.h"
#endif
#ifdef CONFIG_TOUCHSCREEN_GT82X_IIC_760
#include <linux/goodix_touch_82x.h>
#endif

#if defined(CONFIG_SC6610)
#include <linux/sc6610.h>
#endif

#if defined(CONFIG_ANDROID_TIMED_GPIO)
#include "../../../drivers/staging/android/timed_gpio.h"
#endif
#if defined (CONFIG_BP_AUTO)
#include <linux/bp-auto.h>
#endif
#include "board-rk2928-phonepad-760-camera.c" 
#include "board-rk2928-phonepad-key.c"
int __sramdata g_pmic_type =  0;

#ifdef  CONFIG_THREE_FB_BUFFER
#define RK30_FB0_MEM_SIZE 12*SZ_1M
#else
#define RK30_FB0_MEM_SIZE 8*SZ_1M
#endif

/* Android Parameter */
static int ap_mdm = BP_ID_M50;
module_param(ap_mdm, int, 0644);
static int ap_has_alsa = 0;
module_param(ap_has_alsa, int, 0644);
static int ap_multi_card = 0;
module_param(ap_multi_card, int, 0644);
static int ap_data_only = 1;
module_param(ap_data_only, int, 0644);

static int ap_has_earphone = 1;
module_param(ap_has_earphone, int, 0644);

static struct spi_board_info board_spi_devices[] = {
};

/***********************************************************
*	rk30  backlight
************************************************************/
#ifdef CONFIG_BACKLIGHT_RK29_BL
#define PWM_ID            0
#define PWM_MUX_NAME      GPIO0D2_PWM_0_NAME
#define PWM_MUX_MODE      GPIO0D_PWM_0
#define PWM_MUX_MODE_GPIO GPIO0D_GPIO0D2
#define PWM_GPIO 	  RK2928_PIN0_PD2
#define PWM_EFFECT_VALUE  0

#define LCD_DISP_ON_PIN

#ifdef  LCD_DISP_ON_PIN
#define BL_EN_MUX_NAME     GPIO1A5_I2S_SDI_GPS_SIGN_NAME
#define BL_EN_MUX_MODE    GPIO1A_GPIO1A5 
#define BL_EN_PIN         RK2928_PIN1_PA5
#define BL_EN_VALUE       GPIO_HIGH
#endif
static int rk29_backlight_io_init(void)
{
	int ret = 0;
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE);
#ifdef  LCD_DISP_ON_PIN
	rk30_mux_api_set(BL_EN_MUX_NAME, BL_EN_MUX_MODE);

	ret = gpio_request(BL_EN_PIN, NULL);
	//if (ret != 0) {
	//	gpio_free(BL_EN_PIN);
	//}

	gpio_direction_output(BL_EN_PIN, 0);
	mdelay(100);
	gpio_set_value(BL_EN_PIN, BL_EN_VALUE);
#endif
	return ret;
}

static int rk29_backlight_io_deinit(void)
{
	int ret = 0;
#ifdef  LCD_DISP_ON_PIN
	gpio_set_value(BL_EN_PIN, !BL_EN_VALUE);
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
	#if defined(CONFIG_MFD_TPS65910)	
	if(pmic_is_tps65910())
	{
		gpio_direction_output(PWM_GPIO, GPIO_LOW);
	}
	#endif
	#if defined(CONFIG_REGULATOR_ACT8931)
	if(pmic_is_act8931())
	{
		gpio_direction_output(PWM_GPIO, GPIO_HIGH);
	}
	#endif
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
	.pwm_id = PWM_ID,
        .min_brightness = 15,
	.bl_ref = PWM_EFFECT_VALUE,
	.io_init = rk29_backlight_io_init,
	.io_deinit = rk29_backlight_io_deinit,
	.pwm_suspend = rk29_backlight_pwm_suspend,
	.pwm_resume = rk29_backlight_pwm_resume,
	.pre_div = 20000,
};

static struct platform_device rk29_device_backlight = {
	.name	= "rk29_backlight",
	.id 	= -1,
	.dev	= {
		.platform_data  = &rk29_bl_info,
	}
};

#endif
#ifdef  CONFIG_TOUCHSCREEN_GT82X_IIC_760
#define TOUCH_ENABLE_PIN        INVALID_GPIO
#define TOUCH_RESET_PIN  RK2928_PIN3_PD5
#define TOUCH_INT_PIN    RK2928_PIN3_PC7
int goodix_init_platform_hw(void)
{
        int ret;


    
  
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
        .xmax = 1024,
    .ymax = 600,
    .config_info_len =ARRAY_SIZE(ts82x_config_data),
    .config_info = ts82x_config_data,
        .init_platform_hw= goodix_init_platform_hw,
};
#endif
#ifdef CONFIG_FB_ROCKCHIP
#define LCD_STB
#ifdef LCD_STB
#define LCD_STB_MUX_NAME  GPIO1A3_I2S_LRCKTX_NAME
#define LCD_STB_GPIO_MODE GPIO1A_GPIO1A3

#define LCD_STB_EN        RK2928_PIN1_PA3
#define LCD_STB_EN_VALUE  GPIO_HIGH
#endif
#define LCD_MUX_NAME  GPIO0D4_PWM_2_NAME
#define LCD_GPIO_MODE GPIO0D_GPIO0D4

#define LCD_EN        RK2928_PIN0_PD4
#define LCD_EN_VALUE  GPIO_LOW
static int rk_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{
	int ret = 0;

   	 rk30_mux_api_set(LCD_MUX_NAME, LCD_GPIO_MODE);
	ret = gpio_request(LCD_EN, NULL);
	if (ret != 0)
	{
		gpio_free(LCD_EN);
		printk(KERN_ERR "request lcd en pin fail!\n");
		return -1;
	}
	else
	{
		gpio_direction_output(LCD_EN, LCD_EN_VALUE); //disable
	}
	         rk30_mux_api_set(LCD_STB_MUX_NAME, LCD_STB_GPIO_MODE);
        ret = gpio_request(LCD_STB_EN, NULL);
        if (ret != 0)
        {
                gpio_free(LCD_STB_EN);
                printk(KERN_ERR "request lcd en pin fail!\n");
                return -1;
        }
        else
        {
                gpio_direction_output(LCD_STB_EN, LCD_STB_EN_VALUE); //disable
        }

	return 0;
}
static int rk_fb_io_disable(void)
{
    	gpio_set_value(LCD_EN, !LCD_EN_VALUE);
	mdelay(50);
#ifdef LCD_STB
	gpio_set_value(LCD_STB_EN,GPIO_LOW);
#endif
	return 0;
}
static int rk_fb_io_enable(void)
{
#ifdef LCD_STB
	gpio_set_value(LCD_STB_EN,GPIO_HIGH);
#endif
	gpio_set_value(LCD_EN, LCD_EN_VALUE);
	mdelay(200);
	return 0;
}

#if defined(CONFIG_LCDC_RK2928)
struct rk29fb_info lcdc_screen_info = {
	.prop	   = PRMRY,		//primary display device
	.io_init   = rk_fb_io_init,
	.io_disable = rk_fb_io_disable,
	.io_enable = rk_fb_io_enable,
	.set_screen_info = set_lcd_info,
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

//LCDC
#ifdef CONFIG_LCDC_RK2928
static struct resource resource_lcdc[] = {
	[0] = {
		.name  = "lcdc reg",
		.start = RK2928_LCDC_PHYS,
		.end   = RK2928_LCDC_PHYS + RK2928_LCDC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	
	[1] = {
		.name  = "lcdc irq",
		.start = IRQ_LCDC,
		.end   = IRQ_LCDC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device device_lcdc = {
	.name		  = "rk2928-lcdc",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(resource_lcdc),
	.resource	  = resource_lcdc,
	.dev 		= {
		.platform_data = &lcdc_screen_info,
	},
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
#if CONFIG_ANDROID_TIMED_GPIO
static struct timed_gpio timed_gpios[] = {
        {
                .name = "vibrator",
                .gpio = RK2928_PIN3_PD6,
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


#if defined (CONFIG_TP_760_TS)

#define TOUCH_RESET_PIN RK2928_PIN3_PD5
#define TOUCH_INT_PIN   RK2928_PIN3_PC7
int ft5306_init_platform_hw(void)
{
	
	if(gpio_request(TOUCH_RESET_PIN,NULL) != 0)
	{
		gpio_free(TOUCH_RESET_PIN);
		printk("ft5306_init_platform_hw TOUCH_RESET_PIN error\n");
		return -EIO;
	}

	if(gpio_request(TOUCH_INT_PIN,NULL) != 0)
	{
		gpio_free(TOUCH_INT_PIN);
		printk("ift5306_init_platform_hw TOUCH_INT_PIN error\n");
		return -EIO;
	}
	gpio_direction_input(TOUCH_INT_PIN);
	gpio_direction_output(TOUCH_RESET_PIN, 1);
	gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
	msleep(50);
	msleep(300);
	return 0;
	
}

void ft5306_exit_platform_hw(void)
{
	gpio_free(TOUCH_RESET_PIN);
	gpio_free(TOUCH_INT_PIN);
}

int ft5306_platform_sleep(void)
{
	gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
	return 0;
}

int ft5306_platform_wakeup(void)
{
	//gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
	//msleep(10);
	gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
	msleep(300);
	return 0;
}

struct ft5306_platform_data ft5306_info = {
  .irq_pin = TOUCH_INT_PIN,
  .rest_pin = TOUCH_RESET_PIN,
  .init_platform_hw= ft5306_init_platform_hw,
  .exit_platform_hw= ft5306_exit_platform_hw,
  .platform_sleep  = ft5306_platform_sleep,
  .platform_wakeup = ft5306_platform_wakeup,

};
#endif

#if defined(CONFIG_TOUCHSCREEN_BYD693X)

#define TOUCH_RESET_PIN RK2928_PIN3_PD5
#define TOUCH_INT_PIN   RK2928_PIN3_PC7
struct byd_platform_data byd693x_info = {
	.int_pin = TOUCH_INT_PIN,
	.rst_pin = TOUCH_RESET_PIN,
	.screen_max_x = 800,
	.screen_max_y = 480,
	.xpol = -1,
};
#endif

/*MMA7660 gsensor*/
#if defined (CONFIG_GS_MMA7660)
#define MMA7660_INT_PIN   RK2928_PIN3_PD1
static int mma7660_init_platform_hw(void)
{
	//rk30_mux_api_set(GPIO1B1_SPI_TXD_UART1_SOUT_NAME, GPIO1B_GPIO1B1);

	return 0;
}

static struct sensor_platform_data mma7660_info = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
        .init_platform_hw = mma7660_init_platform_hw,
        .orientation = {1, 0, 0, 0, -1,0, 0, 0, -1},
};
#endif


#if defined (CONFIG_GS_KXTIK)
#define KXTIK_INT_PIN         RK2928_PIN3_PD1

static struct sensor_platform_data kxtik_pdata = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 60,
	.orientation = {-1, 0, 0, 0, 0, -1, 0, 1, 0},
};

#endif /* CONFIG_GS_KXTIK*/

#if defined (CONFIG_GS_MM3A310)
#define MM3A310_INT_PIN         RK2928_PIN3_PD1

static struct sensor_platform_data mm3a310_pdata = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
	.orientation = {-1, 0, 0, 0, 0, -1, 0, 1, 0},
};

#endif /* CONFIG_GS_MM3A310*/


#ifdef CONFIG_LS_AP321XX
#define LS_AP321XX_INT_PIN         RK2928_PIN0_PC6

static struct sensor_platform_data ls_ap321xx_info = {
	.type = SENSOR_TYPE_LIGHT,
	.irq_enable = 1,
	.poll_delay_ms = 500,
};
#endif
#ifdef CONFIG_PS_AP321XX
#define PS_AP321XX_INT_PIN         RK2928_PIN0_PC6

static struct sensor_platform_data ps_ap321xx_info = {
	.type = SENSOR_TYPE_PROXIMITY,
	.irq_enable = 1,
	.poll_delay_ms = 500,
};
#endif

#if defined(CONFIG_BATTERY_RK30_ADC)||defined(CONFIG_BATTERY_RK30_ADC_FAC)
#define   CHARGE_OK_PIN  RK2928_PIN3_PD2
#define   DC_DET_PIN     RK2928_PIN3_PD3
#define   DC_CUR_SET_PIN RK2928_PIN1_PA0
static int ac_current = -1;
#define CHARING_CURRENT_500MA 0
#define CHARING_CURRENT_1000MA 1
int rk30_battery_adc_io_init(void){
	int ret = 0;
		
	//dc charge detect pin
	ret = gpio_request(DC_DET_PIN, NULL);
	if (ret) {
		printk("failed to request dc_det gpio\n");
		return ret ;
	}

	gpio_pull_updown(DC_DET_PIN, 1);//important
	ret = gpio_direction_input(DC_DET_PIN);
	if (ret) {
		printk("failed to set gpio dc_det input\n");
		return ret ;
	}
	
	//charge ok pin
	ret = gpio_request(CHARGE_OK_PIN, NULL);
	if (ret) {
		printk("failed to request charge_ok gpio\n");
		return ret ;
	}

	gpio_pull_updown(CHARGE_OK_PIN, 1);//important
	ret = gpio_direction_input(CHARGE_OK_PIN);
	if (ret) {
		printk("failed to set gpio charge_ok input\n");
		return ret ;
	}

	//charge current set pin
	ret = gpio_request(DC_CUR_SET_PIN, NULL);
	if (ret) {
		printk("failed to request DC_CUR_SET_PIN gpio\n");
		return ret ;
	}

	ret = gpio_direction_output(DC_CUR_SET_PIN, GPIO_LOW);//500ma
	if (ret) {
		printk("failed to set gpio DC_CUR_SET_PIN output\n");
		return ret ;
	}
	printk("charging: set charging current 500ma\n");
	ac_current = CHARING_CURRENT_500MA;

	gpio_request(BL_EN_PIN, NULL);
	
	return 0;

}

static int set_ac_charging_current(void)
{
	if (gpio_get_value(BL_EN_PIN) && (ac_current==CHARING_CURRENT_1000MA)) {
		printk("charging: set charging current 500ma\n");
		gpio_set_value(DC_CUR_SET_PIN, GPIO_LOW);
		ac_current = CHARING_CURRENT_500MA;
	}
	else if (!gpio_get_value(BL_EN_PIN) && (ac_current==CHARING_CURRENT_500MA)) {
		printk("charging: set charging current 1000ma\n");
		gpio_set_value(DC_CUR_SET_PIN, GPIO_HIGH);
		ac_current = CHARING_CURRENT_1000MA;
	}
}

static struct rk30_adc_battery_platform_data rk30_adc_battery_platdata = {
        .dc_det_pin      = DC_DET_PIN,
        .batt_low_pin    = INVALID_GPIO,
        .charge_set_pin  = INVALID_GPIO,
        .charge_ok_pin   = CHARGE_OK_PIN,
        .dc_det_level    = GPIO_LOW,  //
        .charge_ok_level = GPIO_HIGH,
		//.control_ac_charging_current = set_ac_charging_current,
		.save_capacity = 1,
	    .is_reboot_charging = 1,
        .io_init = rk30_battery_adc_io_init,
};

static struct platform_device rk30_device_adc_battery = {
        .name   = "rk30-battery",
        .id     = -1,
        .dev = {
                .platform_data = &rk30_adc_battery_platdata,
        },
};
#endif


#if CONFIG_RK30_PWM_REGULATOR
const static int pwm_voltage_map[] = {
	1000000, 1025000, 1050000, 1075000, 1100000, 1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000, 1325000, 1350000, 1375000, 1400000
};

static struct regulator_consumer_supply pwm_dcdc1_consumers[] = {
	{
		.supply = "vdd_core",
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
		.pwm_id = 2,
		.pwm_gpio = RK2928_PIN0_PD4,
		.pwm_iomux_name = GPIO0D4_PWM_2_NAME,
		.pwm_iomux_pwm = GPIO0D_PWM_2, 
		.pwm_iomux_gpio = GPIO0D_GPIO0D4,
		.pwm_voltage = 1200000,
		.suspend_voltage = 1050000,
		.min_uV = 1000000,
		.max_uV	= 1400000,
		.coefficient = 504,	//50.4%
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

/***********************************************************
*	usb wifi
************************************************************/
#if defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) || defined(CONFIG_RT5370)
#define WIFI_POWER_EN_MUX_NAME     GPIO0B0_MMC1_CMD_NAME
#define WIFI_POWER_EN_MUX_MODE    GPIO0B_GPIO0B0
#define WIFI_POWER_EN_PIN         RK2928_PIN0_PB0
#define WIFI_POWER_EN_VALUE       GPIO_LOW
static int rkusb_wifi_init = 0;
static void rkusb_wifi_power_io_init(void)
{
	int ret = 0;
	
	if(rkusb_wifi_init)
		return;
        rk30_mux_api_set(WIFI_POWER_EN_MUX_NAME, WIFI_POWER_EN_MUX_MODE);

        ret = gpio_request(WIFI_POWER_EN_PIN, NULL);
        if (ret != 0) {
		printk("rkusb_wifi_power_io_init fail!!!!!\n");
                gpio_free(WIFI_POWER_EN_PIN);
        }

        gpio_direction_output(WIFI_POWER_EN_PIN, 0);
        gpio_set_value(WIFI_POWER_EN_PIN, GPIO_HIGH);
	rkusb_wifi_init = 1;
}
static void rkusb_wifi_power(int on) {
#if 0
	struct regulator *ldo = NULL;
	
#if defined(CONFIG_MFD_TPS65910)	
	if(pmic_is_tps65910()) {
		ldo = regulator_get(NULL, "vmmc");  //vccio_wl
	}
#endif
#if defined(CONFIG_REGULATOR_ACT8931)
	if(pmic_is_act8931()) {
		ldo = regulator_get(NULL, "act_ldo4");  //vccio_wl
	}
#endif	
	
	if(on) {
		regulator_enable(ldo);
		printk("%s: vccio_wl enable\n", __func__);
	} else {
		printk("%s: vccio_wl disable\n", __func__);
		regulator_disable(ldo);
	}
	
	regulator_put(ldo);
	udelay(100);
#else
	rkusb_wifi_power_io_init();
     	if(on)
      	{
		 printk("usb wifi power on!!\n");
        	 gpio_set_value(WIFI_POWER_EN_PIN,GPIO_LOW);
      	}
      	else
	{	
		printk("usb wifi power down!!\n");
		gpio_set_value(WIFI_POWER_EN_PIN,GPIO_HIGH);
	}
#endif
}

#endif
#if defined(CONFIG_MODEM_SOUND)
struct modem_sound_data modem_sound_info = {
	.spkctl_io = RK2928_PIN3_PD4,
	.spkctl_active = GPIO_HIGH,
};

struct platform_device modem_sound_device = {
	.name = "modem_sound",
	.id = -1,
	.dev		= {
	.platform_data = &modem_sound_info,
		}
	};
#endif

int rk2928_sd_vcc_reset(){
      struct regulator *vcc;

      vcc = regulator_get(NULL,"act_ldo4");
      if (vcc == NULL || IS_ERR(vcc) ){
            printk("%s get cif vaux33 ldo failed!\n",__func__);
            return -1 ;
      }

       printk("hj---->rk29_sdmmc_hw_init get vmmc regulator successfully \n\n\n");
       regulator_disable(vcc);
       mdelay(2000);
       regulator_enable(vcc);

}
/**************************************************************************************************
 * SDMMC devices,  include the module of SD,MMC,and SDIO.noted by xbw at 2012-03-05
**************************************************************************************************/

#ifdef CONFIG_RFKILL_RK
// bluetooth rfkill device, its driver in net/rfkill/rfkill-rk.c
static struct rfkill_rk_platform_data rfkill_rk_platdata = {
    .type           = RFKILL_TYPE_BLUETOOTH,
    .poweron_gpio   = { // BT_REG_ON
        .io             = RK2928_PIN1_PA3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = NULL,
            },
        },

    .reset_gpio     = { // BT_RST
        .io             = RK2928_PIN3_PD5, // set io to INVALID_GPIO for disable it
        .enable         = GPIO_LOW,
        .iomux          = {
            .name           = NULL,
            },
        },

    .wake_gpio      = { // BT_WAKE, use to control bt's sleep and wakeup
        .io             = RK2928_PIN0_PC6, // set io to INVALID_GPIO for disable it
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name           = NULL,
            },
        },

    .wake_host_irq  = { // BT_HOST_WAKE, for bt wakeup host when it is in deep sleep
        .gpio           = {
            .io         = RK2928_PIN0_PC5, // set io to INVALID_GPIO for disable it
            .enable     = GPIO_LOW,      // set GPIO_LOW for falling, set 0 for rising
            .iomux      = {
                .name       = NULL,
            },
        },
    },

    .rts_gpio       = { // UART_RTS, enable or disable BT's data coming
        .io         = RK2928_PIN0_PC3, // set io to INVALID_GPIO for disable it
        .enable      = GPIO_LOW,
        .iomux      = {
           .name        = GPIO0C3_UART0_CTSN_NAME,
           .fgpio       = GPIO0C_GPIO0C3,
           .fmux        = GPIO0C_UART0_RTSN,//GPIO0C_UART0_CTSN,
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
#ifdef CONFIG_SDMMC_RK29
#include "board-rk2928-phonepad-sdmmc.c"
#endif

#ifdef CONFIG_SDMMC0_RK29
static int rk29_sdmmc0_cfg_gpio(void)
{
#ifdef CONFIG_SDMMC_RK29_OLD
	rk30_mux_api_set(GPIO3B1_SDMMC0CMD_NAME, GPIO3B_SDMMC0_CMD);
	rk30_mux_api_set(GPIO3B0_SDMMC0CLKOUT_NAME, GPIO3B_SDMMC0_CLKOUT);
	rk30_mux_api_set(GPIO3B2_SDMMC0DATA0_NAME, GPIO3B_SDMMC0_DATA0);
	rk30_mux_api_set(GPIO3B3_SDMMC0DATA1_NAME, GPIO3B_SDMMC0_DATA1);
	rk30_mux_api_set(GPIO3B4_SDMMC0DATA2_NAME, GPIO3B_SDMMC0_DATA2);
	rk30_mux_api_set(GPIO3B5_SDMMC0DATA3_NAME, GPIO3B_SDMMC0_DATA3);

	rk30_mux_api_set(GPIO3B6_SDMMC0DETECTN_NAME, GPIO3B_GPIO3B6);

	rk30_mux_api_set(GPIO3A7_SDMMC0PWREN_NAME, GPIO3A_GPIO3A7);
	gpio_request(RK30_PIN3_PA7, "sdmmc-power");
	gpio_direction_output(RK30_PIN3_PA7, GPIO_LOW);

#else
	    rk29_sdmmc_set_iomux(0, 0xFFFF);

    #if defined(CONFIG_SDMMC0_RK29_SDCARD_DET_FROM_GPIO)
        rk30_mux_api_set(RK29SDK_SD_CARD_DETECT_PIN_NAME, RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO);
    #else
	    rk30_mux_api_set(RK29SDK_SD_CARD_DETECT_PIN_NAME, RK29SDK_SD_CARD_DETECT_IOMUX_FMUX);
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

    //.setpower = rk29_sdmmc_board_setpower,
};
#endif // CONFIG_SDMMC0_RK29

#ifdef CONFIG_SDMMC1_RK29
#define CONFIG_SDMMC1_USE_DMA
static int rk29_sdmmc1_cfg_gpio(void)
{
#if defined(CONFIG_SDMMC_RK29_OLD)
	rk30_mux_api_set(GPIO3C0_SMMC1CMD_NAME, GPIO3C_SMMC1_CMD);
	rk30_mux_api_set(GPIO3C5_SDMMC1CLKOUT_NAME, GPIO3C_SDMMC1_CLKOUT);
	rk30_mux_api_set(GPIO3C1_SDMMC1DATA0_NAME, GPIO3C_SDMMC1_DATA0);
	rk30_mux_api_set(GPIO3C2_SDMMC1DATA1_NAME, GPIO3C_SDMMC1_DATA1);
	rk30_mux_api_set(GPIO3C3_SDMMC1DATA2_NAME, GPIO3C_SDMMC1_DATA2);
	rk30_mux_api_set(GPIO3C4_SDMMC1DATA3_NAME, GPIO3C_SDMMC1_DATA3);
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
    #endif

    .det_pin_info = {    
#if !defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)    
     #if defined(RK29SDK_SD_CARD_DETECT_N) || (INVALID_GPIO != RK29SDK_SD_CARD_DETECT_N)  
        .io             = RK29SDK_SD_CARD_DETECT_N,
     #else
         .io             = INVALID_GPIO,
     #endif   
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
    }, 
    
	.enable_sd_wakeup = 0,
};
#endif //endif--#ifdef CONFIG_SDMMC1_RK29

/**************************************************************************************************
 * the end of setting for SDMMC devices
**************************************************************************************************/
#if defined(CONFIG_MT5931_MT6622)
static struct mt6622_platform_data mt6622_platdata = {
    .power_gpio         = { // BT_REG_ON
        .io             = RK2928_PIN3_PC2, // set io to INVALID_GPIO for disable it
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = NULL,
        },
    },

    .reset_gpio         = { // BT_RST
        .io             = RK2928_PIN0_PC6,
        .enable         = GPIO_LOW,
        .iomux          = {
            .name       = NULL,
        },
    },

    .irq_gpio           = {
        .io             = RK2928_PIN3_PD3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = NULL,
        },
    }
};

static struct platform_device device_mt6622 = {
    .name   = "mt6622",
    .id     = -1,
    .dev    = {
        .platform_data = &mt6622_platdata,
    },
};
#endif

#if defined(CONFIG_SC6610)
static int sc6610_io_init(void)
{
        
        return 0;
}

static int sc6610_io_deinit(void)
{
        

        return 0;
}

struct rk29_sc6610_data rk29_sc6610_info = {
        .io_init = sc6610_io_init,
        .io_deinit = sc6610_io_deinit,
        .bp_power = RK2928_PIN3_PC2,//RK29_PIN0_PB4,
        .bp_reset = INVALID_GPIO,//RK29_PIN0_PB3,
        .bp_wakeup_ap = RK2928_PIN3_PC3,//RK29_PIN0_PC2,
        .ap_wakeup_bp = RK2928_PIN3_PC4,//RK29_PIN0_PB0, 
        .modem_assert = RK2928_PIN3_PC5,
};
struct platform_device rk29_device_sc6610 = {
        .name = "SC6610",
        .id = -1,
        .dev            = {
                .platform_data = &rk29_sc6610_info,
        }
    };
#endif

#if defined(CONFIG_BP_AUTO)

static int bp_io_deinit(void)
{
	
	return 0;
}
 
static int bp_io_init(void)
{
	rk30_mux_api_set(GPIO0D6_MMC1_PWREN_NAME, GPIO0D_GPIO0D6);//AP_STATUS
	rk30_mux_api_set(GPIO0B6_MMC1_D3_NAME, GPIO0B_GPIO0B6);//mdm_rst
	rk30_mux_api_set(GPIO0D0_UART2_RTSN_NAME, GPIO0D_GPIO0D0);//mdm_ready
         return 0;
}

static int bp_id_get(void)
{
	return ap_mdm;   //internally 3G modem ID, defined in  include\linux\Bp-auto.h
}
struct bp_platform_data bp_auto_info = {	
	.init_platform_hw 	= bp_io_init,	
	.exit_platform_hw 	= bp_io_deinit,	
	.get_bp_id              = bp_id_get,
	.bp_power		= RK2928_PIN3_PC2, 	// 3g_power
	.bp_en			= BP_UNKNOW_DATA,	// 3g_en
	.bp_reset			= BP_UNKNOW_DATA,
	.ap_ready		= BP_UNKNOW_DATA,	//
	.bp_ready		= BP_UNKNOW_DATA,
	.ap_wakeup_bp	= RK2928_PIN3_PC4,
	.bp_wakeup_ap	= RK2928_PIN3_PC3,	//
	.bp_uart_en		= BP_UNKNOW_DATA, 	//EINT9
	.bp_usb_en		= BP_UNKNOW_DATA, 	//W_disable
	.bp_assert		= RK2928_PIN3_PC5,	
	.gpio_valid 		= 0,		//if 1:gpio is define in bp_auto_info,if 0:is not use gpio in bp_auto_info

};

struct platform_device device_bp_auto = {	
        .name = "bp-auto",	
    	.id = -1,	
	.dev		= {
		.platform_data = &bp_auto_info,
	}    	
    };
#endif
#if defined (CONFIG_RK_HEADSET_DET) || defined (CONFIG_RK_HEADSET_IRQ_HOOK_ADC_DET)
static int rk_headset_io_init(int gpio, char *iomux_name, int iomux_mode)
{
	int ret;
	ret = gpio_request(gpio, "headset_io");
	if(ret) 
		return ret;

	rk30_mux_api_set(iomux_name, iomux_mode);
	gpio_pull_updown(gpio, PullDisable);
	gpio_direction_input(gpio);
	mdelay(50);
	return 0;
};

static int rk_hook_io_init(int gpio, char *iomux_name, int iomux_mode)
{
	int ret;
	ret = gpio_request(gpio, "hook_io");
	if(ret) 
		return ret;

	rk30_mux_api_set(iomux_name, iomux_mode);
	gpio_pull_updown(gpio, PullDisable);
	gpio_direction_input(gpio);
	mdelay(50);
	return 0;
};

struct rk_headset_pdata rk_headset_info = {
		.Headset_gpio		= RK2928_PIN1_PB4,
		.Hook_gpio  = RK2928_PIN0_PD1,
		.Hook_down_type = HOOK_DOWN_HIGH,
		.headset_in_type = HEADSET_IN_HIGH,
		.hook_key_code = KEY_MEDIA,
		.headset_gpio_info = {GPIO1B4_SPI_CSN1_NAME, GPIO1B_GPIO1B4},
		.headset_io_init = rk_headset_io_init,
		.hook_gpio_info = {GPIO0D1_UART2_CTSN_NAME, GPIO0D_GPIO0D1},
		.hook_io_init = rk_hook_io_init,
};
struct platform_device rk_device_headset = {
		.name	= "rk_headsetdet",
		.id 	= 0,
		.dev    = {
			    .platform_data = &rk_headset_info,
		}
};
#endif
#ifdef CONFIG_SND_SOC_RK2928
#define HP_CTL_MUX_NAME  GPIO1B6_MMC0_PWREN_NAME
#define HP_CTL_MUX_MODE  GPIO1B_GPIO1B6
#define HP_CTL_IO_NAME   RK2928_PIN1_PB6
static int hpctl_io_init(void)
{
	int ret=0;

	rk30_mux_api_set(HP_CTL_MUX_NAME, HP_CTL_MUX_MODE);
	ret = gpio_request(HP_CTL_IO_NAME, NULL);
        if (ret != 0) {
        	gpio_free(HP_CTL_IO_NAME);
		printk("HP_CTL_IO requeset fail\n");
        }
        else
        {
		gpio_direction_output(HP_CTL_IO_NAME, GPIO_LOW);
	}
	return ret;
}
struct rk2928_codec_pdata rk2928_codec_pdata_info={
	.hpctl = HP_CTL_IO_NAME,
	.hpctl_io_init = hpctl_io_init, 
};
static struct resource resources_acodec[] = {
	{
		.start 	= RK2928_ACODEC_PHYS,
		.end 	= RK2928_ACODEC_PHYS + RK2928_ACODEC_SIZE - 1,
		.flags 	= IORESOURCE_MEM,
	},
	{
		.start	= RK2928_PIN3_PD4,
		.end	= RK2928_PIN3_PD4,
		.flags	= IORESOURCE_IO,
	},
};

static struct platform_device device_acodec = {
	.name	= "rk2928-codec",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_acodec),
	.resource	= resources_acodec,
	.dev    = {
		    .platform_data = &rk2928_codec_pdata_info,
	}	
};
#endif

static struct platform_device *devices[] __initdata = {
#ifdef CONFIG_FB_ROCKCHIP
	&device_fb,
#endif
#ifdef CONFIG_LCDC_RK2928
	&device_lcdc,
#endif
#ifdef CONFIG_BACKLIGHT_RK29_BL
	&rk29_device_backlight,
#endif
#ifdef CONFIG_ION
	&device_ion,
#endif
#ifdef CONFIG_SND_SOC_RK2928
	&device_acodec,
#endif

#if defined(CONFIG_BATTERY_RK30_ADC)||defined(CONFIG_BATTERY_RK30_ADC_FAC)
 	&rk30_device_adc_battery,
#endif
#if defined(CONFIG_SC6610)
        &rk29_device_sc6610,

#endif
#if defined(CONFIG_BP_AUTO)
	&device_bp_auto,
#endif
#if defined (CONFIG_RK_HEADSET_DET) ||  defined (CONFIG_RK_HEADSET_IRQ_HOOK_ADC_DET)
	&rk_device_headset,
#endif
#if defined (CONFIG_MODEM_SOUND)
 &modem_sound_device,
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
        &rk29_device_vibrator,
#endif
#ifdef CONFIG_WIFI_CONTROL_FUNC
	&rk29sdk_wifi_device,
#endif
#ifdef CONFIG_MT5931_MT6622
	&device_mt6622,
#endif
#ifdef CONFIG_RFKILL_RK
	&device_rfkill_rk,
#endif

};
//i2c
#ifdef CONFIG_I2C0_RK30
#ifdef CONFIG_MFD_TPS65910
#define TPS65910_HOST_IRQ        RK2928_PIN3_PC6
#include "board-rk2928-phonepad-tps65910.c"
#endif
#ifdef CONFIG_REGULATOR_ACT8931
#define ACT8931_HOST_IRQ		RK2928_PIN1_PB2
#include "board-rk2928-sdk-act8931.c"
#endif

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
#ifdef CONFIG_MACH_RK2926_M713
int __sramdata gpio0d3_iomux,gpio0d3_do,gpio0d3_dir;
#else
int __sramdata gpio0d4_iomux,gpio0d4_do,gpio0d4_dir;
#endif

#define gpio0_readl(offset)	readl_relaxed(RK2928_GPIO0_BASE + offset)
#define gpio0_writel(v, offset)	do { writel_relaxed(v, RK2928_GPIO0_BASE + offset); dsb(); } while (0)

void __sramfunc rk30_pwm_logic_suspend_voltage(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR

#ifdef CONFIG_MACH_RK2926_M713
	sram_udelay(10000);
	gpio0d3_iomux = readl_relaxed(GRF_GPIO0D_IOMUX);
	gpio0d3_do = gpio0_readl(GPIO_SWPORTA_DR);
	gpio0d3_dir = gpio0_readl(GPIO_SWPORTA_DDR);

	writel_relaxed((gpio0d3_iomux |(1<<22)) & (~(1<<6)), GRF_GPIO0D_IOMUX);
	gpio0_writel(gpio0d3_dir |(1<<27), GPIO_SWPORTA_DDR);
	gpio0_writel(gpio0d3_do |(1<<27), GPIO_SWPORTA_DR);
#else
//	int gpio0d7_iomux,gpio0d7_do,gpio0d7_dir,gpio0d7_en;
	sram_udelay(10000);
	gpio0d4_iomux = readl_relaxed(GRF_GPIO0D_IOMUX);
	gpio0d4_do = gpio0_readl(GPIO_SWPORTA_DR);
	gpio0d4_dir = gpio0_readl(GPIO_SWPORTA_DDR);

	writel_relaxed((gpio0d4_iomux |(1<<24)) & (~(1<<8)), GRF_GPIO0D_IOMUX);
	gpio0_writel(gpio0d4_dir |(1<<28), GPIO_SWPORTA_DDR);
	gpio0_writel(gpio0d4_do |(1<<28), GPIO_SWPORTA_DR);
#endif
	
#endif 
}
void __sramfunc rk30_pwm_logic_resume_voltage(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR
#ifdef CONFIG_MACH_RK2926_M713
	writel_relaxed((1<<22)|gpio0d3_iomux, GRF_GPIO0D_IOMUX);
	gpio0_writel(gpio0d3_dir, GPIO_SWPORTA_DDR);
	gpio0_writel(gpio0d3_do, GPIO_SWPORTA_DR);
	sram_udelay(10000);
#else
	writel_relaxed((1<<24)|gpio0d4_iomux, GRF_GPIO0D_IOMUX);
	gpio0_writel(gpio0d4_dir, GPIO_SWPORTA_DDR);
	gpio0_writel(gpio0d4_do, GPIO_SWPORTA_DR);
	sram_udelay(10000);
        sram_udelay(10000);
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

#ifdef CONFIG_I2C1_RK30
static struct i2c_board_info __initdata i2c1_info[] = {
#if defined (CONFIG_GS_MMA7660)
		{
			.type		= "gs_mma7660",
			.addr		= 0x4c,
			.flags		= 0,
			.irq		= MMA7660_INT_PIN,
			.platform_data = &mma7660_info,
		},
#endif

#if defined (CONFIG_GS_KXTIK)
		{
				.type		= "gs_kxtik",
				.addr		= 0x0F,
				.flags		= 0,
				.platform_data = &kxtik_pdata,
				.irq = KXTIK_INT_PIN, // Replace with appropriate GPIO setup
		},
#endif

#if defined (CONFIG_GS_MM3A310)
		{
				.type		= "gs_mm3a310",
				.addr		= 0x27,
				.flags		= 0,
				.platform_data = &mm3a310_pdata,
				.irq = MM3A310_INT_PIN, // Replace with appropriate GPIO setup
		},
#endif

#ifdef CONFIG_LS_AP321XX
        {
                .type                   = "ls_ap321xx",
                .addr                   = 0x1E,
                .flags                  = 0,
                .irq                     = LS_AP321XX_INT_PIN,
                .platform_data = &ls_ap321xx_info
        },
#endif

#ifdef CONFIG_PS_AP321XX
        {
                .type                   = "ps_ap321xx",
                .addr                   = 0x1E,
                .flags                  = 0,
                .irq                     = PS_AP321XX_INT_PIN,
                .platform_data = &ps_ap321xx_info
        },
#endif

#ifdef CONFIG_RDA5990
#define RDA_WIFI_CORE_ADDR (0x13)
#define RDA_WIFI_RF_ADDR (0x14) //correct add is 0x14
#define RDA_BT_CORE_ADDR (0x15)
#define RDA_BT_RF_ADDR (0x16)

#define RDA_WIFI_RF_I2C_DEVNAME "rda_wifi_rf_i2c"
#define RDA_WIFI_CORE_I2C_DEVNAME "rda_wifi_core_i2c"
#define RDA_BT_RF_I2C_DEVNAME "rda_bt_rf_i2c"
#define RDA_BT_CORE_I2C_DEVNAME "rda_bt_core_i2c"
		{
			.type          = RDA_WIFI_CORE_I2C_DEVNAME,
			.addr          = RDA_WIFI_CORE_ADDR,
                	.flags         = 0,

		},

		{
			.type          = RDA_WIFI_RF_I2C_DEVNAME,
			.addr          = RDA_WIFI_RF_ADDR,
                	.flags         = 0,

		},
		{
			.type          = RDA_BT_CORE_I2C_DEVNAME,
			.addr          = RDA_BT_CORE_ADDR,
                	.flags         = 0,

		},
		{
			.type          = RDA_BT_RF_I2C_DEVNAME,
			.addr          = RDA_BT_RF_ADDR,
                	.flags         = 0,

		},
#endif

};
#endif
#ifdef CONFIG_I2C2_RK30
static struct i2c_board_info __initdata i2c2_info[] = {
#if defined (CONFIG_TP_760_TS)
	{
		.type          = "ft5x0x_ts",
		.addr          = 0x38,
		.flags         = 0,
		.irq           = TOUCH_INT_PIN,
		.platform_data = &ft5306_info,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_BYD693X)
	{
		.type          = "byd693x-ts",
		.addr          = 0x52,
		.flags         = 0,
		.irq           = TOUCH_INT_PIN,
		.platform_data = &byd693x_info,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_GT82X_IIC_760)
        {
                .type          = "Goodix-TS-82X",
                .addr          = 0x5D,
                .flags         = 0,
                .irq           = RK2928_PIN3_PC7,
                .platform_data = &ts82x_pdata,
        },
#endif

};
#endif
#ifdef CONFIG_I2C3_RK30
static struct i2c_board_info __initdata i2c3_info[] = {
};
#endif
#ifdef CONFIG_I2C_GPIO_RK30
#define I2C_SDA_PIN     INVALID_GPIO   //set sda_pin here
#define I2C_SCL_PIN     INVALID_GPIO   //set scl_pin here
static int rk30_i2c_io_init(void)
{
        //set iomux (gpio) here

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
//end of i2c

#define POWER_ON_PIN RK2928_PIN1_PA2   //power_hold
#if defined(CONFIG_REGULATOR_ACT8931)
extern  int act8931_charge_det ;
#endif
static void rk2928_pm_power_off(void)
{
	printk(KERN_ERR "rk2928_pm_power_off start...\n");
        #if defined(CONFIG_REGULATOR_ACT8931)
        if(pmic_is_act8931())
        {
                 #ifdef CONFIG_BATTERY_RK30_ADC_FAC
              if (gpio_get_value (rk30_adc_battery_platdata.dc_det_pin) == rk30_adc_battery_platdata.dc_det_level)//if(act8931_charge_det)
		arm_pm_restart(0, NULL);
				 #endif
               act8931_device_shutdown();
        }
        #endif
	
	#if defined(CONFIG_MFD_TPS65910)
	if(pmic_is_tps65910())
	{
		tps65910_device_shutdown();//tps65910 shutdown
	}
	#endif
	gpio_direction_output(POWER_ON_PIN, GPIO_LOW);
	
};

static void __init rk2928_board_init(void)
{
	gpio_request(POWER_ON_PIN, "poweronpin");
	gpio_direction_output(POWER_ON_PIN, GPIO_HIGH);
	
	pm_power_off = rk2928_pm_power_off;
	
	rk30_i2c_register_board_info();
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
	platform_add_devices(devices, ARRAY_SIZE(devices));

#ifdef CONFIG_WIFI_CONTROL_FUNC
	rk29sdk_wifi_bt_gpio_control_init();
#endif
}

static void __init rk2928_reserve(void)
{
#ifdef CONFIG_ION
	rk30_ion_pdata.heaps[0].base = board_mem_reserve_add("ion", ION_RESERVE_SIZE);
#endif
#ifdef CONFIG_FB_ROCKCHIP
	resource_fb[0].start = board_mem_reserve_add("fb0", RK30_FB0_MEM_SIZE);
	resource_fb[0].end = resource_fb[0].start + RK30_FB0_MEM_SIZE - 1;
#endif
#ifdef CONFIG_VIDEO_RK29
	rk30_camera_request_reserve_mem();
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
static struct dvfs_arm_table dvfs_cpu_logic_table[] = {
	{.frequency = 216 * 1000,	.cpu_volt =  1200 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 312 * 1000,	.cpu_volt =  1200 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 408 * 1000,	.cpu_volt =  1200 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 504 * 1000,	.cpu_volt = 1200 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 600 * 1000,	.cpu_volt = 1200 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 696 * 1000,	.cpu_volt = 1400 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 816 * 1000,	.cpu_volt = 1400 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 912 * 1000,	.cpu_volt = 1450 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 1008 * 1000,	.cpu_volt = 1500 * 1000,	.logic_volt = 1200 * 1000},
#if 0
        {.frequency = 1104 * 1000,      .cpu_volt = 1400 * 1000,        .logic_volt = 1200 * 1000},
        {.frequency = 1200 * 1000,      .cpu_volt = 1400 * 1000,        .logic_volt = 1200 * 1000},
        {.frequency = 1104 * 1000,      .cpu_volt = 1400 * 1000,        .logic_volt = 1200 * 1000},
        {.frequency = 1248 * 1000,      .cpu_volt = 1400 * 1000,        .logic_volt = 1200 * 1000},
#endif
        {.frequency = CPUFREQ_TABLE_END},

};

static struct cpufreq_frequency_table dvfs_gpu_table[] = {
	{.frequency = 266 * 1000,	.index = 1200 * 1000},
	{.frequency = 400 * 1000,	.index = 1275 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dvfs_ddr_table[] = {
	{.frequency = 300 * 1000,	.index = 1200 * 1000},
	{.frequency = 400 * 1000,	.index = 1200 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

#define DVFS_CPU_TABLE_SIZE	(ARRAY_SIZE(dvfs_cpu_logic_table))
static struct cpufreq_frequency_table cpu_dvfs_table[DVFS_CPU_TABLE_SIZE];
static struct cpufreq_frequency_table dep_cpu2core_table[DVFS_CPU_TABLE_SIZE];

void __init board_clock_init(void)
{
	rk2928_clock_data_init(periph_pll_default, codec_pll_default, RK30_CLOCKS_DEFAULT_FLAGS);
	dvfs_set_arm_logic_volt(dvfs_cpu_logic_table, cpu_dvfs_table, dep_cpu2core_table);
	dvfs_set_freq_volt_table(clk_get(NULL, "gpu"), dvfs_gpu_table);
	dvfs_set_freq_volt_table(clk_get(NULL, "ddr"), dvfs_ddr_table);
	printk("%s end\n", __func__);
}


MACHINE_START(RK2928, "RK2928board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= rk2928_fixup,
	.reserve	= &rk2928_reserve,
	.map_io		= rk2928_map_io,
	.init_irq	= rk2928_init_irq,
	.timer		= &rk2928_timer,
	.init_machine	= rk2928_board_init,
MACHINE_END
