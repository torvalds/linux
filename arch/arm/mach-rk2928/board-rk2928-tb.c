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
#include <linux/fb.h>
#include <linux/regulator/machine.h>
#include <linux/rfkill-rk.h>
#include <linux/sensor-dev.h>
#include <linux/mfd/tps65910.h>
#include <linux/regulator/rk29-pwm-regulator.h>
#if defined(CONFIG_HDMI_RK30)
	#include "../../../drivers/video/rockchip/hdmi/rk_hdmi.h"
#endif

#if defined(CONFIG_SPIM_RK29)
#include "../../../drivers/spi/rk29_spim.h"
#endif
#if defined(CONFIG_GPS_RK)
#include "../../../drivers/misc/gps/rk_gps/rk_gps.h"
#endif


#ifdef  CONFIG_THREE_FB_BUFFER
#define RK30_FB0_MEM_SIZE 12*SZ_1M
#else
#define RK30_FB0_MEM_SIZE 8*SZ_1M
#endif

int __sramdata g_pmic_type =  0;

#include "board-rk2928-tb-camera.c"
#include "board-rk2928-tb-key.c"

#if defined (CONFIG_EETI_EGALAX)
#if defined (CONFIG_MACH_RK2928_TB)
#define TOUCH_RESET_PIN  RK2928_PIN3_PC3
#define TOUCH_INT_PIN    RK2928_PIN3_PC7
#elif defined (CONFIG_MACH_RK2926_TB)
#define TOUCH_RESET_PIN  RK2928_PIN2_PB0
#define TOUCH_INT_PIN    RK2928_PIN1_PB0
#endif


static int EETI_EGALAX_init_platform_hw(void)
{
    if(gpio_request(TOUCH_RESET_PIN,NULL) != 0){
      gpio_free(TOUCH_RESET_PIN);
      printk("p1003_init_platform_hw gpio_request error\n");
      return -EIO;
    }

    if(gpio_request(TOUCH_INT_PIN,NULL) != 0){
      gpio_free(TOUCH_INT_PIN);
      printk("p1003_init_platform_hw gpio_request error\n");
      return -EIO;
    }
    gpio_pull_updown(TOUCH_INT_PIN, 1);
    gpio_direction_output(TOUCH_RESET_PIN, 0);
    msleep(500);
    gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
    msleep(500);
    gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);

    return 0;
}


static struct eeti_egalax_platform_data eeti_egalax_info = {
  .model= 1003,
  .init_platform_hw= EETI_EGALAX_init_platform_hw,
  .standby_pin = INVALID_GPIO,
  //.standby_value = GPIO_HIGH,
  .disp_on_pin = INVALID_GPIO,
  //.disp_on_value = GPIO_HIGH,
};
#endif

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
#define PWM_EFFECT_VALUE  1

#define LCD_DISP_ON_PIN

#ifdef  LCD_DISP_ON_PIN
#if defined(CONFIG_MACH_RK2928_TB)
#define BL_EN_PIN         RK2928_PIN3_PC4
#define BL_EN_VALUE       GPIO_HIGH
#elif defined(CONFIG_MACH_RK2926_TB)
#define BL_EN_PIN         RK2928_PIN2_PC1
#define BL_EN_VALUE       GPIO_HIGH
#endif

#endif
static int rk29_backlight_io_init(void)
{
	int ret = 0;
	rk30_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE);
#ifdef  LCD_DISP_ON_PIN
	// rk30_mux_api_set(BL_EN_MUX_NAME, BL_EN_MUX_MODE);

	ret = gpio_request(BL_EN_PIN, NULL);
	if (ret != 0) {
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
	.pwm_id = PWM_ID,
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

/*MMA8452 gsensor*/
#if defined (CONFIG_GS_MMA8452)

static int mma8452_init_platform_hw(void)
{
	return 0;
}

static struct sensor_platform_data mma8452_info = {
	.type = SENSOR_TYPE_ACCEL,
	.irq_enable = 1,
	.poll_delay_ms = 30,
        .init_platform_hw = mma8452_init_platform_hw,
        .orientation = {-1, 0, 0, 0, 0, 1, 0, -1, 0},
};
#endif

#ifdef CONFIG_FB_ROCKCHIP

#if defined (CONFIG_MACH_RK2928_TB)
#define LCD_CABC_EN        RK2928_PIN2_PD1
#define LCD_CABC_EN_VALUE  GPIO_HIGH
#elif defined (CONFIG_MACH_RK2926_TB)
#define LCD_CABC_EN        RK2928_PIN2_PC3
#define LCD_CABC_EN_VALUE  GPIO_HIGH
#endif

static int rk_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{
	int ret = 0;

#if defined (CONFIG_MACH_RK2926_TB)
#endif

	ret = gpio_request(LCD_CABC_EN, NULL);
	if (ret != 0)
	{
		gpio_free(LCD_CABC_EN);
		printk(KERN_ERR "request lcd cabc en pin fail!\n");
		return -1;
	}
	else
	{
		gpio_direction_output(LCD_CABC_EN, !LCD_CABC_EN_VALUE); //disable
	}
	return 0;
}
static int rk_fb_io_disable(void)
{
	return 0;
}
static int rk_fb_io_enable(void)
{
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

#ifdef CONFIG_RK30_PWM_REGULATOR
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
#if defined (CONFIG_MACH_RK2928_TB)
		.pwm_id = 2,
		.pwm_gpio = RK2928_PIN0_PD4,
		.pwm_iomux_name = GPIO0D4_PWM_2_NAME,
		.pwm_iomux_pwm = GPIO0D_PWM_2, 
		.pwm_iomux_gpio = GPIO0D_GPIO0D4,
#elif defined (CONFIG_MACH_RK2926_TB)
                .pwm_id = 1,
		.pwm_gpio = RK2928_PIN0_PD3,
		.pwm_iomux_name = GPIO0D3_PWM_1_NAME,
		.pwm_iomux_pwm = GPIO0D_PWM_1, 
		.pwm_iomux_gpio = GPIO0D_GPIO0D3,
#endif
		.pwm_voltage = 1200000,
		.suspend_voltage = 1050000,
		.min_uV = 1000000,
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
#endif

/***********************************************************
*	usb wifi
************************************************************/
#if defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) 

static void rkusb_wifi_power(int on) {
#if 0
	struct regulator *ldo = NULL;
	
#if defined(CONFIG_MFD_TPS65910)	
	if (pmic_is_tps65910() )
		ldo = regulator_get(NULL, "vmmc");  //vccio_wl
#endif
#if defined(CONFIG_REGULATOR_ACT8931)
	if(pmic_is_act8931() )
		ldo = regulator_get(NULL, "act_ldo4");  //vccio_wl
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
#endif
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
	.detect_irq = RK2928_PIN1_PC1,  //INVALID_GPIO,
#endif
	.enable_sd_wakeup = 0,
#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
	.write_prt = SDMMC0_WRITE_PROTECT_PIN,
#else
	.write_prt = INVALID_GPIO,
#endif
};
#endif // CONFIG_SDMMC0_RK29

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
#if 0
	.detect_irq = RK29SDK_WIFI_SDIO_CARD_DETECT_N,
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

/**************************************************************************************************
 * the end of setting for SDMMC devices
**************************************************************************************************/



#ifdef CONFIG_RFKILL_RK
// bluetooth rfkill device, its driver in net/rfkill/rfkill-rk.c
static struct rfkill_rk_platform_data rfkill_rk_platdata = {
#if defined (CONFIG_MACH_RK2928_TB)
    .type               = RFKILL_TYPE_BLUETOOTH,

    .poweron_gpio       = { // BT_REG_ON
        .io             = INVALID_GPIO,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = NULL,
        },
    },

    .reset_gpio         = { // BT_RST
        .io             = RK2928_PIN3_PD5, // set io to INVALID_GPIO for disable it
        .enable         = GPIO_LOW,
        .iomux          = {
            .name       = NULL,
        },
    },

    .wake_gpio          = { // BT_WAKE, use to control bt's sleep and wakeup
        .io             = RK2928_PIN0_PC6, // set io to INVALID_GPIO for disable it
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = NULL,
        },
    },

    .wake_host_irq      = { // BT_HOST_WAKE, for bt wakeup host when it is in deep sleep
        .gpio           = {
            .io         = RK2928_PIN0_PC5, // set io to INVALID_GPIO for disable it
            .enable     = GPIO_LOW,      // set GPIO_LOW for falling, set 0 for rising
            .iomux      = {
                .name   = NULL,
            },
        },
    },

    .rts_gpio           = { // UART_RTS, enable or disable BT's data coming
        .io             = RK2928_PIN0_PC3, // set io to INVALID_GPIO for disable it
        .enable         = GPIO_LOW,
        .iomux          = {
            .name       = GPIO0C3_UART0_CTSN_NAME,
            .fgpio      = GPIO0C_GPIO0C3,
            .fmux       = GPIO0C_UART0_CTSN,
        },
    },
#endif
};

static struct platform_device device_rfkill_rk = {
    .name   = "rfkill_rk",
    .id     = -1,
    .dev    = {
        .platform_data = &rfkill_rk_platdata,
    },
};
#endif

#ifdef CONFIG_SND_SOC_RK2928
static struct resource resources_acodec[] = {
	{
		.start 	= RK2928_ACODEC_PHYS,
		.end 	= RK2928_ACODEC_PHYS + RK2928_ACODEC_SIZE - 1,
		.flags 	= IORESOURCE_MEM,
	},
	{
                #if defined (CONFIG_MACH_RK2928_TB)
		.start	= RK2928_PIN3_PD4,
		.end	= RK2928_PIN3_PD4,
                #elif defined (CONFIG_MACH_RK2926_TB)
                .start	= RK2928_PIN1_PA0,
		.end	= RK2928_PIN1_PA0,
                #endif
		.flags	= IORESOURCE_IO,
	},
};

static struct platform_device device_acodec = {
	.name	= "rk2928-codec",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_acodec),
	.resource	= resources_acodec,
};
#endif

#if defined(CONFIG_GPS_RK)
int rk_gps_io_init(void)
{
	printk("%s \n", __FUNCTION__);
	rk30_mux_api_set(GPIO1B1_SPI_TXD_UART1_SOUT_NAME, GPIO1B_GPIO1B1);//VCC_EN
	gpio_request(RK2928_PIN1_PB1, NULL);
	gpio_direction_output(RK2928_PIN1_PB1, GPIO_LOW);

	rk30_mux_api_set(GPIO1A2_I2S_LRCKRX_GPS_CLK_NAME, GPIO1A_GPS_CLK);//GPS_CLK
	rk30_mux_api_set(GPIO1A4_I2S_SDO_GPS_MAG_NAME, GPIO1A_GPS_MAG);//GPS_MAG
	rk30_mux_api_set(GPIO1A5_I2S_SDI_GPS_SIGN_NAME, GPIO1A_GPS_SIGN);//GPS_SIGN

	rk30_mux_api_set(GPIO1B0_SPI_CLK_UART1_CTSN_NAME, GPIO1B_GPIO1B0);//SPI_CLK
	gpio_request(RK2928_PIN1_PB0, NULL);
	gpio_direction_output(RK2928_PIN1_PB0, GPIO_LOW);

	rk30_mux_api_set(GPIO1B2_SPI_RXD_UART1_SIN_NAME, GPIO1B_GPIO1B2);//SPI_MOSI
	gpio_request(RK2928_PIN1_PB2, NULL);
	gpio_direction_output(RK2928_PIN1_PB2, GPIO_LOW);	

	rk30_mux_api_set(GPIO1B3_SPI_CSN0_UART1_RTSN_NAME, GPIO1B_GPIO1B3);//SPI_CS
	gpio_request(RK2928_PIN1_PB3, NULL);
	gpio_direction_output(RK2928_PIN1_PB3, GPIO_LOW);		
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
	clk_enable(clk_get(NULL, "aclk_gps"));
	return 0;
}
int rk_disable_hclk_gps(void)
{
	printk("%s \n", __FUNCTION__);
	clk_disable(clk_get(NULL, "aclk_gps"));
	return 0;
}
struct rk_gps_data rk_gps_info = {
	.io_init = rk_gps_io_init,
	.power_up = rk_gps_power_up,
	.power_down = rk_gps_power_down,
	.reset = rk_gps_reset_set,
	.enable_hclk_gps = rk_enable_hclk_gps,
	.disable_hclk_gps = rk_disable_hclk_gps,
	.GpsSign = RK2928_PIN1_PA5,
	.GpsMag = RK2928_PIN1_PA4,        //GPIO index
	.GpsClk = RK2928_PIN1_PA2,        //GPIO index
	.GpsVCCEn = RK2928_PIN1_PB1,     //GPIO index
	.GpsSpi_CSO = RK2928_PIN1_PB3,    //GPIO index
	.GpsSpiClk = RK2928_PIN1_PB0,     //GPIO index
	.GpsSpiMOSI = RK2928_PIN1_PB2,	  //GPIO index	
	.GpsIrq = IRQ_GPS,
	.GpsSpiEn = 1,
	.GpsAdcCh = 2,
	.u32GpsPhyAddr = RK2928_GPS_PHYS,
	.u32GpsPhySize = RK2928_GPS_SIZE,
};

struct platform_device rk_device_gps = {
	.name = "gps_hv5820b",
	.id = -1,
	.dev		= {
	.platform_data = &rk_gps_info,
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
#ifdef CONFIG_WIFI_CONTROL_FUNC
	&rk29sdk_wifi_device,
#endif
#ifdef CONFIG_RFKILL_RK
	&device_rfkill_rk,
#endif
#ifdef CONFIG_SND_SOC_RK2928
	&device_acodec,
#endif
#ifdef CONFIG_GPS_RK
	&rk_device_gps,
#endif

};
//i2c
#ifdef CONFIG_I2C0_RK30
static struct i2c_board_info __initdata i2c0_info[] = {
#if defined (CONFIG_GS_MMA8452)
	{
		.type	        = "gs_mma8452",
		.addr	        = 0x1d,
		.flags	        = 0,
                #if defined(CONFIG_MACH_RK2928_TB)
		.irq	        = RK2928_PIN3_PD1,
                #elif defined(CONFIG_MACH_RK2926_TB)
		.irq	        = RK2928_PIN1_PB2,
                #endif
		.platform_data = &mma8452_info,
	},
#endif
};
#endif
#ifdef CONFIG_I2C1_RK30
#ifdef CONFIG_MFD_TPS65910
#if defined(CONFIG_MACH_RK2928_TB)
#define TPS65910_HOST_IRQ        RK2928_PIN3_PC6
#elif defined(CONFIG_MACH_RK2926_TB)
#define TPS65910_HOST_IRQ        RK2928_PIN1_PB1
#endif
#include "board-rk2928-sdk-tps65910.c"
#endif
static struct i2c_board_info __initdata i2c1_info[] = {

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
#ifdef CONFIG_I2C2_RK30
static struct i2c_board_info __initdata i2c2_info[] = {
#if defined (CONFIG_EETI_EGALAX)
        {
                .type           = "egalax_i2c",
                .addr           = 0x04,
                .flags          = 0,
                .irq            = TOUCH_INT_PIN,
                .platform_data  = &eeti_egalax_info,
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
#if defined (CONFIG_MACH_RK2928_TB)
#define POWER_ON_PIN RK2928_PIN3_PC5   //power_hold
#elif defined (CONFIG_MACH_RK2926_TB)
#define POWER_ON_PIN RK2928_PIN1_PA2   //power_hold
#endif
static void rk2928_pm_power_off(void)
{
	printk(KERN_ERR "rk2928_pm_power_off start...\n");
	
	#if defined(CONFIG_MFD_TPS65910)
		tps65910_device_shutdown();//tps65910 shutdown
	#endif
	gpio_direction_output(POWER_ON_PIN, GPIO_LOW);
	
};

static void __init rk2928_board_init(void)
{
	gpio_request(POWER_ON_PIN, "poweronpin");
	gpio_direction_output(POWER_ON_PIN, GPIO_HIGH);
        gpio_free(POWER_ON_PIN);
	
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
static struct dvfs_arm_table dvfs_cpu_logic_table[] = {
        {.frequency = 216 * 1000,       .cpu_volt = 1200 * 1000,        .logic_volt = 1200 * 1000},
        {.frequency = 312 * 1000,       .cpu_volt = 1200 * 1000,        .logic_volt = 1200 * 1000},
        {.frequency = 408 * 1000,       .cpu_volt = 1200 * 1000,        .logic_volt = 1200 * 1000},
        {.frequency = 504 * 1000,       .cpu_volt = 1200 * 1000,        .logic_volt = 1200 * 1000},
        {.frequency = 600 * 1000,       .cpu_volt = 1200 * 1000,        .logic_volt = 1200 * 1000},
        {.frequency = 696 * 1000,       .cpu_volt = 1400 * 1000,        .logic_volt = 1200 * 1000},
        {.frequency = 816 * 1000,       .cpu_volt = 1400 * 1000,        .logic_volt = 1200 * 1000},
        //{.frequency = 912 * 1000,       .cpu_volt = 1450 * 1000,        .logic_volt = 1200 * 1000},
        //{.frequency = 1008 * 1000,      .cpu_volt = 1500 * 1000,        .logic_volt = 1200 * 1000},
#if 0
	{.frequency = 1104 * 1000,	.cpu_volt = 1400 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 1200 * 1000,	.cpu_volt = 1400 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 1104 * 1000,	.cpu_volt = 1400 * 1000,	.logic_volt = 1200 * 1000},
	{.frequency = 1248 * 1000,	.cpu_volt = 1400 * 1000,	.logic_volt = 1200 * 1000},
#endif
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dvfs_gpu_table[] = {
	{.frequency = 266 * 1000,	.index = 1050 * 1000},
	{.frequency = 400 * 1000,	.index = 1275 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};

static struct cpufreq_frequency_table dvfs_ddr_table[] = {
	{.frequency = 300 * 1000,	.index = 1050 * 1000},
	{.frequency = 400 * 1000,	.index = 1125 * 1000},
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
	//dvfs_set_freq_volt_table(clk_get(NULL, "ddr"), dvfs_ddr_table);
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
