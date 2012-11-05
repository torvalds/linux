/* arch/arm/mach-rk2928/board-rk2928.c
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
#include <linux/module.h>
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
#include <linux/fb.h>
#include <linux/regulator/machine.h>
#include <linux/rfkill-rk.h>
#include <linux/sensor-dev.h>
#include <linux/mfd/tps65910.h>
#include <linux/regulator/act8931.h>
#include <linux/regulator/rk29-pwm-regulator.h>

#include "board-rk2928-config.c" 
#include "board-phonepad.c" 

#if defined(CONFIG_HDMI_RK30)
	#include "../../../drivers/video/rockchip/hdmi/rk_hdmi.h"
#endif

#if defined(CONFIG_SPIM_RK29)
#include "../../../drivers/spi/rk29_spim.h"
#endif

#include "board-rk2928-camera.c" 
#include "board-rk2928-key.c"


#ifdef  CONFIG_THREE_FB_BUFFER
#define RK30_FB0_MEM_SIZE 12*SZ_1M
#else
#define RK30_FB0_MEM_SIZE 8*SZ_1M
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
        struct pwm_io_config *cfg = &pwm_cfg[bl_pwm];

        ret = gpio_request(cfg->gpio, "bl_pwm");
        if(ret < 0){
                printk("%s: request gpio(bl_pwm) failed\n", __func__);
                return ret;
        }
        rk30_mux_api_set(cfg->mux_name, cfg->pwm_mode);

        if(bl_en == -1)
                return 0;
        ret = port_output_init(bl_en, 0, "bl_en");
        if(ret < 0){
                printk("%s: port output init faild\n", __func__);
                return ret;
        }
        port_output_on(bl_en);

        return 0;
}
static int rk29_backlight_io_deinit(void)
{
        struct pwm_io_config *cfg = &pwm_cfg[bl_pwm];

        port_output_off(bl_en);
        if(bl_en != -1)
                port_deinit(bl_en);
        rk30_mux_api_set(cfg->mux_name, cfg->io_mode);
        gpio_free(cfg->gpio);

        return 0;
}

static int rk29_backlight_pwm_suspend(void)
{
        struct pwm_io_config *cfg = &pwm_cfg[bl_pwm];

        rk30_mux_api_set(cfg->mux_name, cfg->io_mode);

        if(bl_ref)
	        gpio_direction_output(cfg->gpio, GPIO_LOW);
        else
	        gpio_direction_output(cfg->gpio, GPIO_HIGH);

        port_output_off(bl_en);

	return 0;
}

static int rk29_backlight_pwm_resume(void)
{
        struct pwm_io_config *cfg = &pwm_cfg[bl_pwm];

        rk30_mux_api_set(cfg->mux_name, cfg->pwm_mode);
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

static int __init bl_board_init(void)
{
        int ret = check_bl_param();

        if(ret < 0)
                return ret;

        rk29_bl_info.pwm_id = bl_pwm;
        rk29_bl_info.bl_ref = bl_ref;
        rk29_bl_info.min_brightness = bl_min;

        return 0;
}

#else
static int __init bl_board_init(void)
{
        return 0;
}
#endif

#ifdef CONFIG_FB_ROCKCHIP
static int rk_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{
	int ret = 0;

        if(lcd_cabc != -1){
                ret = port_output_init(lcd_cabc, 0, "lcd_cabc");
                if(ret < 0)
                        printk("%s: port output init faild\n", __func__);
        }
        if(lcd_en == -1)
                return 0;
        ret = port_output_init(lcd_en, 1, "lcd_en");
        if(ret < 0)
                printk("%s: port output init faild\n", __func__);

	return ret;
}
static int rk_fb_io_disable(void)
{
        if(lcd_en != -1)
                port_output_off(lcd_en);

	return 0;
}
static int rk_fb_io_enable(void)
{
        if(lcd_en != -1)
                port_output_on(lcd_en);
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

static int __init lcd_board_init(void)
{
        return check_lcd_param();
}
#else
static int __init lcd_board_init(void)
{
        return 0;
}
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
static struct ion_platform_data rk30_ion_pdata = {
	.nr = 1,
	.heaps = {
		{
			.type = ION_HEAP_TYPE_CARVEOUT,
			.id = ION_NOR_HEAP_ID,
			.name = "norheap",
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
static int __init ion_board_init(void)
{
        int ret = check_ion_param();

        if(ret < 0)
                return ret;
        rk30_ion_pdata.heaps[0].size = ion_size;

        return 0;
}
#else
static int __init ion_board_init(void)
{
        return 0;
}
#endif

#if CONFIG_RK30_PWM_REGULATOR
static int pwm_voltage_map[] = {
	950000,975000,1000000, 1025000, 1050000, 1075000, 1100000, 1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000, 1325000, 1350000, 1375000, 1400000
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
		.pwm_voltage = 1200000,
		.suspend_voltage = 1050000,
		.min_uV = 950000,
		.max_uV	= 1400000,
		.coefficient = 455,	//45.5%
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

static int __init pwm_reg_board_init(void)
{
        struct pwm_io_config *cfg;
        int ret = check_reg_pwm_param();

        if(ret < 0)
                return ret;

        cfg = &pwm_cfg[reg_pwm];
        pwm_regulator_info[0].pwm_id = reg_pwm;
        pwm_regulator_info[0].pwm_iomux_name = cfg->mux_name;
        pwm_regulator_info[0].pwm_iomux_pwm = cfg->pwm_mode;
        pwm_regulator_info[0].pwm_iomux_gpio = cfg->io_mode;
        pwm_regulator_info[0].pwm_gpio = cfg->gpio;

        return 0;
}
#else
static int __init pwm_reg_board_init(void)
{
        return 0;
}
#endif
/***********************************************************
*	usb wifi
************************************************************/
#if defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) 
static void rkusb_wifi_power(int on) {

        pmic_ldo_set(wifi_ldo, on);
	udelay(100);
}
#endif

/**************************************************************************************************
 * SDMMC devices,  include the module of SD,MMC,and sdio.noted by xbw at 2012-03-05
**************************************************************************************************/
#ifdef CONFIG_SDMMC_RK29
#include "board-rk2928-sdk-sdmmc.c"

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
#define SDMMC0_WRITE_PROTECT_PIN	RK2928_PIN1_PA7	//According to your own project to set the value of write-protect-pin.
#endif

#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
#define SDMMC1_WRITE_PROTECT_PIN	RK2928_PIN0_PD5	//According to your own project to set the value of write-protect-pin.
#endif

#define RK29SDK_WIFI_SDIO_CARD_DETECT_N    RK2928_PIN0_PB2

#define RK29SDK_SD_CARD_DETECT_N        RK2928_PIN2_PA7  //According to your own project to set the value of card-detect-pin.
#define RK29SDK_SD_CARD_INSERT_LEVEL    GPIO_LOW         // set the voltage of insert-card. Please pay attention to the default setting.


#endif //endif ---#ifdef CONFIG_SDMMC_RK29

#ifdef CONFIG_SDMMC0_RK29
static int rk29_sdmmc0_cfg_gpio(void)
{
	rk29_sdmmc_set_iomux(0, 0xFFFF);

#if defined(CONFIG_SDMMC0_RK29_SDCARD_DET_FROM_GPIO)
    rk30_mux_api_set(GPIO1C1_MMC0_DETN_NAME, GPIO1C_GPIO1C1);
   // gpio_request(RK29SDK_SD_CARD_DETECT_N, "sd-detect");
   // gpio_direction_output(RK29SDK_SD_CARD_DETECT_N,GPIO_HIGH);//set mmc0-data1 to high.
#else
	rk30_mux_api_set(GPIO1C1_MMC0_DETN_NAME, GPIO1C_MMC0_DETN);
#endif	

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
	gpio_request(SDMMC0_WRITE_PROTECT_PIN, "sdmmc-wp");
	gpio_direction_input(SDMMC0_WRITE_PROTECT_PIN);
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

#if defined(CONFIG_SDMMC0_RK29_SDCARD_DET_FROM_GPIO)
    .detect_irq = RK29SDK_SD_CARD_DETECT_N,
    .insert_card_level = RK29SDK_SD_CARD_INSERT_LEVEL,
#else
	.detect_irq = INVALID_GPIO,
#endif

	.enable_sd_wakeup = 0,

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
	.write_prt = SDMMC0_WRITE_PROTECT_PIN,
#else
	.write_prt = INVALID_GPIO,
#endif
};
#endif // CONFIG_SDMMC0_RK29

#if defined(CONFIG_SDMMC0_RK29_SDCARD_DET_FROM_GPIO)
static int __init sdmmc_board_init(void)
{
        struct port_config port;
        int ret = check_sdmmc_param();

        if(ret < 0)
                return ret;
        if(sd_det == -1)
                return 0;
        port = get_port_config(sd_det);

        default_sdmmc0_data.detect_irq = port.gpio;
        default_sdmmc0_data.insert_card_level = !port.io.active_low;
        return 0;
}
#else
static int __init sdmmc_board_init(void)
{
        return 0;
}
#endif

#ifdef CONFIG_SDMMC1_RK29
#define CONFIG_SDMMC1_USE_DMA
static int rk29_sdmmc1_cfg_gpio(void)
{
#if defined(CONFIG_SDMMC_RK29_OLD)
	rk30_mux_api_set(GPIO0B0_MMC1_CMD_NAME, GPIO0B_MMC1_CMD);
	rk30_mux_api_set(GPIO0B1_MMC1_CLKOUT_NAME, GPIO0B_MMC1_CLKOUT);
	rk30_mux_api_set(GPIO0B3_MMC1_D0_NAME, GPIO0B_MMC1_D0);
	rk30_mux_api_set(GPIO0B4_MMC1_D1_NAME, GPIO0B_MMC1_D1);
	rk30_mux_api_set(GPIO0B5_MMC1_D2_NAME, GPIO0B_MMC1_D2);
	rk30_mux_api_set(GPIO0B6_MMC1_D3_NAME, GPIO0B_MMC1_D3);
	//rk30_mux_api_set(GPIO0B2_MMC1_DETN_NAME, GPIO0B_MMC1_DETN);

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

#if !defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
#ifdef CONFIG_WIFI_CONTROL_FUNC
	.status = rk29sdk_wifi_status,
	.register_status_notify = rk29sdk_wifi_status_register,
#endif
#if 1
	.detect_irq = INVALID_GPIO,//RK29SDK_WIFI_SDIO_CARD_DETECT_N,
#endif

#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
	.write_prt = SDMMC1_WRITE_PROTECT_PIN,
#else
	.write_prt = INVALID_GPIO,
#endif

#else
	.detect_irq = INVALID_GPIO,
	.enable_sd_wakeup = 0,
#endif

};
#endif //endif--#ifdef CONFIG_SDMMC1_RK29

#ifdef CONFIG_SND_SOC_RK2928
static struct resource resources_acodec[] = {
	{
		.start 	= RK2928_ACODEC_PHYS,
		.end 	= RK2928_ACODEC_PHYS + RK2928_ACODEC_SIZE - 1,
		.flags 	= IORESOURCE_MEM,
	},
	{
		.flags	= IORESOURCE_IO,
	},
};

static struct platform_device device_acodec = {
	.name	= "rk2928-codec",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_acodec),
	.resource	= resources_acodec,
};

static int __init codec_board_init(void)
{
        int gpio;
        int ret = check_codec_param();

        if(ret < 0)
                return ret;
        gpio = get_port_config(spk_ctl).gpio;
        resources_acodec[1].start = gpio;
        resources_acodec[1].end = gpio;
        return 0;
}
#else
static int __init codec_board_init(void)
{
        return 0;
}
#endif
#ifdef CONFIG_BATTERY_RK30_ADC_FAC
#if defined(CONFIG_REGULATOR_ACT8931)
extern  int act8931_charge_det;
extern  int act8931_charge_ok;

int rk30_battery_adc_io_init(void){
	int ret = 0;
		
        if(dc_det != -1){
        ret = port_input_init(dc_det, "dc_det");
    	        if (ret) {
                        printk("%s: port(%d) input init faild\n", __func__, dc_det);
    		        return ret ;
    	        }
        }
        if(chg_ok != -1){
        ret = port_input_init(chg_ok, "chg_ok");
    	        if (ret) {
                        printk("%s: port(%d) input init faild\n", __func__, chg_ok);
    		        return ret ;
    	        }
        }

	return 0;
}

int rk30_battery_adc_is_dc_charging(void)
{
        return  act8931_charge_det;  
}
int rk30_battery_adc_charging_ok(void)
{
       return act8931_charge_ok;
}
#endif

static struct rk30_adc_battery_platform_data rk30_adc_battery_platdata = {
        .dc_det_pin      = INVALID_GPIO,
        .batt_low_pin    = INVALID_GPIO,
        .charge_set_pin  = INVALID_GPIO,
        .charge_ok_pin   = INVALID_GPIO,
        
        //.io_init = rk30_battery_adc_io_init,
        #if defined(CONFIG_REGULATOR_ACT8931)
        .is_dc_charging  = rk30_battery_adc_is_dc_charging,
	.charging_ok     = rk30_battery_adc_charging_ok ,
        #endif
        
        .charging_sleep   = 0 ,
        .save_capacity   = 1 ,
};

static struct platform_device rk30_device_adc_battery = {
        .name   = "rk30-battery",
        .id     = -1,
        .dev = {
                .platform_data = &rk30_adc_battery_platdata,
        },
};
static int __init chg_board_init(void)
{
        int ret = check_chg_param();
        if(ret < 0)
                return ret;
        rk30_adc_battery_platdata.adc_channel = chg_adc;
        return 0;
}
#else
static int __init chg_board_init(void)
{
        return 0;
}
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
#ifdef CONFIG_BATTERY_RK30_ADC_FAC
	&rk30_device_adc_battery,
#endif
};
#if defined (CONFIG_MFD_TPS65910) && defined (CONFIG_REGULATOR_ACT8931)
#include "board-rk2928-sdk-tps65910.c"
#include "board-rk2928-sdk-act8931.c"
static struct i2c_board_info __initdata pmic_info = {
        .flags = 0,
};
static int __init pmic_board_init(void)
{
        int ret = 0, i;
        struct port_config port;

        ret = check_pmic_param();
        if(ret < 0)
                return ret;
        if(pmic_irq != -1){
                port = get_port_config(pmic_irq);
                pmic_info.irq = port.gpio;
        }
        if(pmic_is_tps65910()){
                strcpy(pmic_info.type, "tps65910");
                pmic_info.platform_data = &tps65910_data;

                tps65910_data.irq = port.gpio;
                for(i = 0; i < ARRAY_SIZE(tps65910_dcdc_info); i++){
                        tps65910_dcdc_info[i].min_uv = tps65910_dcdc[2*i];
                        tps65910_dcdc_info[i].max_uv = tps65910_dcdc[2*i + 1];
                }
                for(i = 0; i < ARRAY_SIZE(tps65910_ldo_info); i++){
                        tps65910_ldo_info[i].min_uv = tps65910_ldo[2*i];
                        tps65910_ldo_info[i].max_uv = tps65910_ldo[2*i + 1];
                }
        }
        if(pmic_is_act8931()){
                strcpy(pmic_info.type, "act8931");
                pmic_info.platform_data = &act8931_data;
                for(i = 0; i < ARRAY_SIZE(act8931_dcdc_info); i++){
                        act8931_dcdc_info[i].min_uv = act8931_dcdc[2*i];
                        act8931_dcdc_info[i].max_uv = act8931_dcdc[2*i + 1];
                }
                for(i = 0; i < ARRAY_SIZE(act8931_ldo_info); i++){
                        act8931_ldo_info[i].min_uv = act8931_ldo[2*i];
                        act8931_ldo_info[i].max_uv = act8931_ldo[2*i + 1];
                }
        }
        pmic_info.addr = pmic_addr;
	i2c_register_board_info(pmic_i2c, &pmic_info, 1);

        return 0;
}
#else
static int __init pmic_board_init(void)
{
        return 0;
}
#endif

#define GPIO_SWPORTA_DR   0x0000
#define GPIO_SWPORTA_DDR  0x0004
#define PWM_MUX_REG       (GRF_GPIO0D_IOMUX)
#define PWM_DDR_REG       (RK2928_GPIO0_BASE + GPIO_SWPORTA_DDR)
#define PWM_DR_REG        (RK2928_GPIO0_BASE + GPIO_SWPORTA_DR)

#define mux_set_gpio_mode(id)  do { writel_relaxed( 1 << (20 + (id) * 2), PWM_MUX_REG); dsb(); } while (0)
#define mux_set_pwm_mode(id)  do { writel_relaxed( (1 << (20 + (id) * 2)) | (1 << (4 + (id) * 2)), PWM_MUX_REG); dsb(); } while (0)

#define pwm_output_high(id) do {\
                                writel_relaxed(readl_relaxed(PWM_DDR_REG) | (1 << (26 + (id))), PWM_DDR_REG); \
                                writel_relaxed(readl_relaxed(PWM_DR_REG) | (1 << (26 + (id))), PWM_DR_REG); \
                                dsb(); \
                                } while (0)

void __sramfunc rk30_pwm_logic_suspend_voltage(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR

	sram_udelay(10000);
        mux_set_gpio_mode(reg_pwm);
        pwm_output_high(reg_pwm);
#endif 
}
void __sramfunc rk30_pwm_logic_resume_voltage(void)
{
#ifdef CONFIG_RK30_PWM_REGULATOR
        mux_set_pwm_mode(reg_pwm);
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

//i2c
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

/**************** gsensor ****************/
// kxtik
#if defined (CONFIG_GS_KXTIK)
static int kxtik_init_platform_hw(void)
{
	return 0;
}

static struct sensor_platform_data kxtik_data = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
	.init_platform_hw = kxtik_init_platform_hw,
};
struct i2c_board_info __initdata kxtik_info = {
        .type = "gs_kxtik",
        .flags = 0,
        .platform_data = &kxtik_data,
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
        .type = "gs_mma8452",
        .flags = 0,
        .platform_data = &mma8452_data,
};
#endif
#if defined (CONFIG_GS_MMA7660)
static int mma7660_init_platform_hw(void)
{
	return 0;
}

static struct sensor_platform_data mma7660_data = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
        .init_platform_hw = mma7660_init_platform_hw,
};
struct i2c_board_info __initdata mma7660_info = {
        .type = "gs_mma7660",
        .flags = 0,
        .platform_data = &mma7660_data,
};
#endif
static int __init gs_board_init(void)
{
        int i;
        struct port_config port;
        int ret = check_gs_param();

        if(ret < 0)
                return ret;
        port = get_port_config(gs_irq);
        //kxtik
#if defined (CONFIG_GS_KXTIK)
        if(gs_type == GS_TYPE_KXTIK){
                kxtik_info.irq = port.gpio;
                kxtik_info.addr = gs_addr;
                for(i = 0; i < 9; i++)
                        kxtik_data.orientation[i] = gs_orig[i];
	        i2c_register_board_info(gs_i2c, &kxtik_info, 1);
        }
#endif
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
        
        return 0;
}
#ifdef CONFIG_LS_AP321XX
static struct sensor_platform_data ls_ap321xx_data = {
	.type = SENSOR_TYPE_LIGHT,
	.irq_enable = 1,
	.poll_delay_ms = 500,
};
struct i2c_board_info __initdata ls_ap321xx_info = {
        .type = "ls_ap321xx",
        .flags = 0,
        .platform_data = &ls_ap321xx_data,
};
#endif
static int __init ls_board_init(void)
{
        struct port_config port;
        int ret = check_ls_param();

        if(ret < 0)
                return ret;
        port = get_port_config(ls_irq);
        //ap321xx
#if defined(CONFIG_LS_AP321XX)
        if(ls_type == LS_TYPE_AP321XX){
                ls_ap321xx_info.irq = port.gpio;
                ls_ap321xx_info.addr = ls_addr;
	        i2c_register_board_info(ls_i2c, &ls_ap321xx_info, 1);
        }
#endif
        return 0;
}


#ifdef CONFIG_PS_AP321XX
static struct sensor_platform_data ps_ap321xx_data = {
	.type = SENSOR_TYPE_PROXIMITY,
	.irq_enable = 1,
	.poll_delay_ms = 500,
};
struct i2c_board_info __initdata ps_ap321xx_info = {
        .type = "ps_ap321xx",
        .flags = 0,
        .platform_data = &ps_ap321xx_data,
};
#endif
static int __init ps_board_init(void)
{
        struct port_config port;
        int ret = check_ps_param();

        if(ret < 0)
                return ret;
        port = get_port_config(ps_irq);
        //ap321xx
#if defined(CONFIG_PS_AP321XX)
        if(ps_type == PS_TYPE_AP321XX){
                ps_ap321xx_info.irq = port.gpio;
                ps_ap321xx_info.addr = ps_addr;
	        i2c_register_board_info(ps_i2c, &ps_ap321xx_info, 1);
        }
#endif
        return 0;
}


#if defined (CONFIG_RTC_HYM8563)
struct i2c_board_info __initdata rtc_info = {
        .type = "rtc_hym8563",
        .flags = 0,
};
static int __init rtc_board_init(void)
{
        struct port_config port;
        int ret;

        if(pmic_is_tps65910())
                return 0;
                
        ret = check_rtc_param();
        if(ret < 0)
                return ret;
        port = get_port_config(rtc_irq);
        rtc_info.irq = port.gpio;
        rtc_info.addr = rtc_addr;
	i2c_register_board_info(rtc_i2c, &rtc_info, 1);
        
        return 0;
}
#else
static int __init rtc_board_init(void)
{
        return 0;
}
#endif

static int __init rk2928_config_init(void)
{
        int ret = 0;

        ret = key_board_init();
        if(ret < 0)
                return ret;
        ret = bl_board_init();
        if(ret < 0)
                return ret;
        ret = lcd_board_init();
        if(ret < 0)
                return ret;
        ret = ion_board_init();
        if(ret < 0)
                return ret;
        ret = pwm_reg_board_init();
        if(ret < 0)
                return ret;
        ret = gs_board_init();
        if(ret < 0)
                return ret;
        ret = ls_board_init();
        if(ret < 0)
                return ret;
        ret = cam_board_init();
        if(ret < 0)
                return ret;
        ret = ps_board_init();
        if(ret < 0)
                return ret;
        ret = rtc_board_init();
        if(ret < 0)
                return ret;
        ret = pmic_board_init();
        if(ret < 0)
                return ret;
        ret = codec_board_init();
        if(ret < 0)
                return ret;
        ret = sdmmc_board_init();
        if(ret < 0)
                return ret;
        ret = chg_board_init();
        if(ret < 0)
                return ret;
        return 0;
}
#if defined(CONFIG_REGULATOR_ACT8931)
extern  int act8931_charge_det;
#endif
static void rk2928_pm_power_off(void)
{
	printk(KERN_ERR "rk2928_pm_power_off start...\n");
        #if defined(CONFIG_REGULATOR_ACT8931)
        if(pmic_is_act8931())
        {
              if(act8931_charge_det)
                   arm_pm_restart(0, NULL);
        }
        #endif
	
	#if defined(CONFIG_MFD_TPS65910)	
	if(pmic_is_tps65910())
	{
		tps65910_device_shutdown();//tps65910 shutdown
	}
	#endif
        rk2928_power_off();
	
};

/**
 * dvfs_cpu_logic_table: table for arm and logic dvfs
 * @frequency	: arm frequency
 * @cpu_volt	: arm voltage depend on frequency
 * @logic_volt	: logic voltage arm requests depend on frequency
 * comments	: min arm/logic voltage
 */
#define DVFS_CPU_TABLE_SIZE	(ARRAY_SIZE(dvfs_cpu_logic_table))
static struct cpufreq_frequency_table cpu_dvfs_table[DVFS_CPU_TABLE_SIZE];
static struct cpufreq_frequency_table dep_cpu2core_table[DVFS_CPU_TABLE_SIZE];

static noinline __init void board_init_dvfs(void)
{
	unsigned i, j;

	for (i = 0, j = 0; (i + 2) < dvfs_cpu_logic_num && j < (ARRAY_SIZE(dvfs_cpu_logic_table) - 1); i += 3, j++) {
		dvfs_cpu_logic_table[j].frequency  = dvfs_cpu_logic[i + 0] * 1000;
		dvfs_cpu_logic_table[j].cpu_volt   = dvfs_cpu_logic[i + 1] * 1000;
		dvfs_cpu_logic_table[j].logic_volt = dvfs_cpu_logic[i + 2] * 1000;
	}
	if (j > 0) {
		dvfs_cpu_logic_table[j].frequency = CPUFREQ_TABLE_END;
	}
	dvfs_set_arm_logic_volt(dvfs_cpu_logic_table, cpu_dvfs_table, dep_cpu2core_table);

	for (i = 0, j = 0; (i + 1) < dvfs_gpu_num && j < (ARRAY_SIZE(dvfs_gpu_table) - 1); i += 2, j++) {
		dvfs_gpu_table[j].frequency = dvfs_gpu[i + 0] * 1000;
		dvfs_gpu_table[j].index     = dvfs_gpu[i + 1] * 1000;
	}
	if (j > 0) {
		dvfs_gpu_table[j].frequency = CPUFREQ_TABLE_END;
	}
	dvfs_set_freq_volt_table(clk_get(NULL, "gpu"), dvfs_gpu_table);

#if 0
	for (i = 0, j = 0; (i + 1) < dvfs_ddr_num && j < (ARRAY_SIZE(dvfs_ddr_table) - 1); i += 2, j++) {
		dvfs_ddr_table[j].frequency = dvfs_ddr[i + 0] * 1000;
		dvfs_ddr_table[j].index     = dvfs_ddr[i + 1] * 1000;
	}
	if (j > 0) {
		dvfs_ddr_table[j].frequency = CPUFREQ_TABLE_END;
	}
	dvfs_set_freq_volt_table(clk_get(NULL, "ddr"), dvfs_ddr_table);
#endif
}

static void __init rk2928_board_init(void)
{
        rk2928_power_on();
        rk2928_config_init();
 
	pm_power_off = rk2928_pm_power_off;

	board_init_dvfs();

	rk30_i2c_register_board_info();
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
	platform_add_devices(devices, ARRAY_SIZE(devices));
        if(is_phonepad)
                phonepad_board_init();
}
static void __init rk2928_reserve(void)
{
#ifdef CONFIG_ION
	rk30_ion_pdata.heaps[0].base = board_mem_reserve_add("ion", ion_size);
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

void __init board_clock_init(void)
{
	rk2928_clock_data_init(periph_pll_default, codec_pll_default, RK30_CLOCKS_DEFAULT_FLAGS);
}

MACHINE_START(RK2928, "RK2928board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= rk2928_fixup,
	.reserve	= rk2928_reserve,
	.map_io		= rk2928_map_io,
	.init_irq	= rk2928_init_irq,
	.timer		= &rk2928_timer,
	.init_machine	= rk2928_board_init,
MACHINE_END
