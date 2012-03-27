/* arch/arm/mach-rk29/board-rk29.c
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
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>
#include <linux/mmc/host.h>
#include <linux/android_pmem.h>
#ifdef CONFIG_USB_ANDROID
#include <linux/usb/android_composite.h>
#endif
#include <linux/ion.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <mach/rk29_iomap.h>
#include <mach/board.h>
#include <mach/rk29_nand.h>
#include <mach/rk29_camera.h>                          /* ddl@rock-chips.com : camera support */
#include <media/soc_camera.h>                               /* ddl@rock-chips.com : camera support */
#include <mach/vpu_mem.h>
#include <mach/sram.h>
#include <mach/ddr.h>
#include <mach/cpufreq.h>
#include <mach/rk29_smc.h>

#include <linux/regulator/rk29-pwm-regulator.h>
#include <linux/regulator/machine.h>

#include <linux/regulator/act8891.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/i2c-gpio.h>
#include <linux/mpu.h>
#include "devices.h"
#include "../../../drivers/input/touchscreen/xpt2046_cbn_ts.h"

#ifdef CONFIG_BU92747GUW_CIR
#include "../../../drivers/cir/bu92747guw_cir.h"
#endif
#ifdef CONFIG_VIDEO_RK29
/*---------------- Camera Sensor Macro Define Begin  ------------------------*/
/*---------------- Camera Sensor Configuration Macro Begin ------------------------*/
#ifdef CONFIG_SOC_CAMERA_OV2655
#define CONFIG_SENSOR_0   RK29_CAM_SENSOR_OV2655                      /* back camera sensor *//* back camera sensor */
#define CONFIG_SENSOR_IIC_ADDR_0 	    0x60
#define CONFIG_SENSOR_IIC_ADAPTER_ID_0    1
#define CONFIG_SENSOR_ORIENTATION_0       90
#define CONFIG_SENSOR_POWER_PIN_0         INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_0          INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_0     RK29_PIN5_PD7//RK29_PIN6_PB7
#define CONFIG_SENSOR_FALSH_PIN_0          INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_0 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_0 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_0 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_0 RK29_CAM_FLASHACTIVE_L
#endif

#define CONFIG_SENSOR_QCIF_FPS_FIXED_0      12504
#define CONFIG_SENSOR_240X160_FPS_FIXED_0      12504
#define CONFIG_SENSOR_QVGA_FPS_FIXED_0      12504
#define CONFIG_SENSOR_CIF_FPS_FIXED_0       12504
#define CONFIG_SENSOR_VGA_FPS_FIXED_0       12504
#define CONFIG_SENSOR_480P_FPS_FIXED_0      12504
#define CONFIG_SENSOR_SVGA_FPS_FIXED_0      12504
#define CONFIG_SENSOR_720P_FPS_FIXED_0      12504
#ifdef CONFIG_SOC_CAMERA_OV9665
#define CONFIG_SENSOR_1 RK29_CAM_SENSOR_OV9665                      /* front camera sensor */
#define CONFIG_SENSOR_IIC_ADDR_1            0x60
#define CONFIG_SENSOR_IIC_ADAPTER_ID_1    1
#define CONFIG_SENSOR_ORIENTATION_1       0
#define CONFIG_SENSOR_POWER_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_1       RK29_PIN6_PB7//RK29_PIN5_PD7
#define CONFIG_SENSOR_FALSH_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_1 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_1 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_1 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_1 RK29_CAM_FLASHACTIVE_L
#endif
#define CONFIG_SENSOR_1   RK29_CAM_SENSOR_OV2655                       /* back camera sensor *//* back camera sensor */
#define CONFIG_SENSOR_IIC_ADDR_1 	    0x60
#define CONFIG_SENSOR_IIC_ADAPTER_ID_1    1
#define CONFIG_SENSOR_ORIENTATION_1       270
#define CONFIG_SENSOR_POWER_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_1          INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_1    RK29_PIN6_PB7//RK29_PIN6_PB7
#define CONFIG_SENSOR_FALSH_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_1 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_1 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_1 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_1 RK29_CAM_FLASHACTIVE_L

#define CONFIG_SENSOR_QCIF_FPS_FIXED_1      12504
#define CONFIG_SENSOR_240X160_FPS_FIXED_1      12504
#define CONFIG_SENSOR_QVGA_FPS_FIXED_1      12504
#define CONFIG_SENSOR_CIF_FPS_FIXED_1       12504
#define CONFIG_SENSOR_VGA_FPS_FIXED_1       12504
#define CONFIG_SENSOR_480P_FPS_FIXED_1      12504
#define CONFIG_SENSOR_SVGA_FPS_FIXED_1      12504
#define CONFIG_SENSOR_720P_FPS_FIXED_1      12504
#endif  //#ifdef CONFIG_VIDEO_RK29
/*---------------- Camera Sensor Configuration Macro End------------------------*/
#include "../../../drivers/media/video/rk29_camera.c"
/*---------------- Camera Sensor Macro Define End  ------------------------*/


/* Set memory size of pmem */
#ifdef CONFIG_RK29_MEM_SIZE_M
#define SDRAM_SIZE          (CONFIG_RK29_MEM_SIZE_M * SZ_1M)
#else
#define SDRAM_SIZE          SZ_512M
#endif
#define PMEM_GPU_SIZE       SZ_128M
#define PMEM_UI_SIZE        (68 * SZ_1M) /* 1280x800: 64M 1024x768: 48M ... */
#define PMEM_VPU_SIZE       SZ_64M
#define PMEM_SKYPE_SIZE     0
#define PMEM_CAM_SIZE       PMEM_CAM_NECESSARY
#ifdef CONFIG_VIDEO_RK29_WORK_IPP
#define MEM_CAMIPP_SIZE     PMEM_CAMIPP_NECESSARY
#else
#define MEM_CAMIPP_SIZE     0
#endif
#define MEM_FB_SIZE         (9*SZ_1M)
#ifdef CONFIG_FB_WORK_IPP
#ifdef CONFIG_FB_SCALING_OSD_1080P
#define MEM_FBIPP_SIZE      SZ_16M   //1920 x 1080 x 2 x 2  //RGB565 = x2;RGB888 = x4
#else
#define MEM_FBIPP_SIZE      SZ_8M   //1920 x 1080 x 2 x 2  //RGB565 = x2;RGB888 = x4
#endif
#else
#define MEM_FBIPP_SIZE      0
#endif
#if SDRAM_SIZE > SZ_512M
#define PMEM_GPU_BASE       (RK29_SDRAM_PHYS + SZ_512M - PMEM_GPU_SIZE)
#else
#define PMEM_GPU_BASE       (RK29_SDRAM_PHYS + SDRAM_SIZE - PMEM_GPU_SIZE)
#endif
#define PMEM_UI_BASE        (PMEM_GPU_BASE - PMEM_UI_SIZE)
#define PMEM_VPU_BASE       (PMEM_UI_BASE - PMEM_VPU_SIZE)
#define PMEM_CAM_BASE       (PMEM_VPU_BASE - PMEM_CAM_SIZE)
#define MEM_CAMIPP_BASE     (PMEM_CAM_BASE - MEM_CAMIPP_SIZE)
#define MEM_FB_BASE         (MEM_CAMIPP_BASE - MEM_FB_SIZE)
#define MEM_FBIPP_BASE      (MEM_FB_BASE - MEM_FBIPP_SIZE)
#define PMEM_SKYPE_BASE     (MEM_FBIPP_BASE - PMEM_SKYPE_SIZE)
#define LINUX_SIZE          (PMEM_SKYPE_BASE - RK29_SDRAM_PHYS)

#define PREALLOC_WLAN_SEC_NUM           4
#define PREALLOC_WLAN_BUF_NUM           160
#define PREALLOC_WLAN_SECTION_HEADER    24

#define WLAN_SECTION_SIZE_0     (PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_1     (PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_2     (PREALLOC_WLAN_BUF_NUM * 512)
#define WLAN_SECTION_SIZE_3     (PREALLOC_WLAN_BUF_NUM * 1024)

#define WLAN_SKB_BUF_NUM        16

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

struct wifi_mem_prealloc {
        void *mem_ptr;
        unsigned long size;
};

extern struct sys_timer rk29_timer;

static int rk29_nand_io_init(void)
{
    return 0;
}

struct rk29_nand_platform_data rk29_nand_data = {
    .width      = 1,     /* data bus width in bytes */
    .hw_ecc     = 1,     /* hw ecc 0: soft ecc */
    .num_flash    = 1,
    .io_init   = rk29_nand_io_init,
};

#define TOUCH_SCREEN_STANDBY_PIN          INVALID_GPIO
#define TOUCH_SCREEN_STANDBY_VALUE        GPIO_HIGH
#define TOUCH_SCREEN_DISPLAY_PIN          INVALID_GPIO
#define TOUCH_SCREEN_DISPLAY_VALUE        GPIO_HIGH

#ifdef CONFIG_FB_RK29
/*****************************************************************************************
 * lcd  devices
 * author: zyw@rock-chips.com
 *****************************************************************************************/
//#ifdef  CONFIG_LCD_TD043MGEA1
#define LCD_TXD_PIN          INVALID_GPIO
#define LCD_CLK_PIN          INVALID_GPIO
#define LCD_CS_PIN           INVALID_GPIO
/*****************************************************************************************
* frame buffe  devices
* author: zyw@rock-chips.com
*****************************************************************************************/
#define FB_ID                       0
#define FB_DISPLAY_ON_PIN           RK29_PIN6_PD1
#define FB_LCD_STANDBY_PIN          INVALID_GPIO
#define FB_LCD_CABC_EN_PIN          RK29_PIN6_PD2
#define FB_MCU_FMK_PIN              INVALID_GPIO

#define FB_DISPLAY_ON_VALUE         GPIO_HIGH
#define FB_LCD_STANDBY_VALUE        GPIO_HIGH

static int rk29_lcd_io_init(void)
{
    int ret = 0;
    return ret;
}

static int rk29_lcd_io_deinit(void)
{
    int ret = 0;
    return ret;
}

static struct rk29lcd_info rk29_lcd_info = {
    .txd_pin  = LCD_TXD_PIN,
    .clk_pin = LCD_CLK_PIN,
    .cs_pin = LCD_CS_PIN,
    .io_init   = rk29_lcd_io_init,
    .io_deinit = rk29_lcd_io_deinit,
};

int rk29_fb_io_enable(void)
{
    if(FB_DISPLAY_ON_PIN != INVALID_GPIO)
    {
        gpio_direction_output(FB_DISPLAY_ON_PIN, 0);
        gpio_set_value(FB_DISPLAY_ON_PIN, FB_DISPLAY_ON_VALUE);              
    }
    if(FB_LCD_STANDBY_PIN != INVALID_GPIO)
    {
        gpio_direction_output(FB_LCD_STANDBY_PIN, 0);
        gpio_set_value(FB_LCD_STANDBY_PIN, FB_LCD_STANDBY_VALUE);             
    }
    return 0;
}

int rk29_fb_io_disable(void)
{
    if(FB_DISPLAY_ON_PIN != INVALID_GPIO)
    {
        gpio_direction_output(FB_DISPLAY_ON_PIN, 0);
        gpio_set_value(FB_DISPLAY_ON_PIN, !FB_DISPLAY_ON_VALUE);              
    }
    if(FB_LCD_STANDBY_PIN != INVALID_GPIO)
    {
        gpio_direction_output(FB_LCD_STANDBY_PIN, 0);
        gpio_set_value(FB_LCD_STANDBY_PIN, !FB_LCD_STANDBY_VALUE);             
    }
    return 0;
}

static int rk29_fb_io_init(struct rk29_fb_setting_info *fb_setting)
{
    int ret = 0;
    if(fb_setting->mcu_fmk_en && (FB_MCU_FMK_PIN != INVALID_GPIO))
    {
        ret = gpio_request(FB_MCU_FMK_PIN, NULL);
        if(ret != 0)
        {
            gpio_free(FB_MCU_FMK_PIN);
            printk(">>>>>> FB_MCU_FMK_PIN gpio_request err \n ");
        }
        gpio_direction_input(FB_MCU_FMK_PIN);
    }
    if(fb_setting->disp_on_en)
    {
        if(FB_DISPLAY_ON_PIN != INVALID_GPIO)
        {
            ret = gpio_request(FB_DISPLAY_ON_PIN, NULL);
            if(ret != 0)
            {
                gpio_free(FB_DISPLAY_ON_PIN);
                printk(">>>>>> FB_DISPLAY_ON_PIN gpio_request err \n ");
            }
        }
        else if (TOUCH_SCREEN_DISPLAY_PIN != INVALID_GPIO)
        {
             ret = gpio_request(TOUCH_SCREEN_DISPLAY_PIN, NULL);
             if(ret != 0)
             {
                 gpio_free(TOUCH_SCREEN_DISPLAY_PIN);
                 printk(">>>>>> TOUCH_SCREEN_DISPLAY_PIN gpio_request err \n ");
             }
             gpio_direction_output(TOUCH_SCREEN_DISPLAY_PIN, 0);
             gpio_set_value(TOUCH_SCREEN_DISPLAY_PIN, TOUCH_SCREEN_DISPLAY_VALUE);
        }
    }

    if(fb_setting->disp_on_en)
    {
        if(FB_LCD_STANDBY_PIN != INVALID_GPIO)
        {
             ret = gpio_request(FB_LCD_STANDBY_PIN, NULL);
             if(ret != 0)
             {
                 gpio_free(FB_LCD_STANDBY_PIN);
                 printk(">>>>>> FB_LCD_STANDBY_PIN gpio_request err \n ");
             }
        }
        else if (TOUCH_SCREEN_STANDBY_PIN != INVALID_GPIO)
        {
             ret = gpio_request(TOUCH_SCREEN_STANDBY_PIN, NULL);
             if(ret != 0)
             {
                 gpio_free(TOUCH_SCREEN_STANDBY_PIN);
                 printk(">>>>>> TOUCH_SCREEN_STANDBY_PIN gpio_request err \n ");
             }
             gpio_direction_output(TOUCH_SCREEN_STANDBY_PIN, 0);
             gpio_set_value(TOUCH_SCREEN_STANDBY_PIN, TOUCH_SCREEN_STANDBY_VALUE);
         }
    }

    if(FB_LCD_CABC_EN_PIN != INVALID_GPIO)
    {
        ret = gpio_request(FB_LCD_CABC_EN_PIN, NULL);
        if(ret != 0)
        {
            gpio_free(FB_LCD_CABC_EN_PIN);
            printk(">>>>>> FB_LCD_CABC_EN_PIN gpio_request err \n ");
        }
        gpio_direction_output(FB_LCD_CABC_EN_PIN, 0);
        gpio_set_value(FB_LCD_CABC_EN_PIN, GPIO_HIGH);
    }
    
    rk29_fb_io_enable();   //enable it

    return ret;
}


static struct rk29fb_info rk29_fb_info = {
    .fb_id   = FB_ID,
    .mcu_fmk_pin = FB_MCU_FMK_PIN,
    .lcd_info = &rk29_lcd_info,
    .io_init   = rk29_fb_io_init,
    .io_enable = rk29_fb_io_enable,
    .io_disable = rk29_fb_io_disable,
};

/* rk29 fb resource */
static struct resource rk29_fb_resource[] = {
	[0] = {
        .name  = "lcdc reg",
		.start = RK29_LCDC_PHYS,
		.end   = RK29_LCDC_PHYS + RK29_LCDC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
	    .name  = "lcdc irq",
		.start = IRQ_LCDC,
		.end   = IRQ_LCDC,
		.flags = IORESOURCE_IRQ,
	},
	[2] = {
	    .name   = "win1 buf",
        .start  = MEM_FB_BASE,
        .end    = MEM_FB_BASE + MEM_FB_SIZE - 1,
        .flags  = IORESOURCE_MEM,
    },
    #ifdef CONFIG_FB_WORK_IPP
    [3] = {
	    .name   = "win1 ipp buf",
        .start  = MEM_FBIPP_BASE,
        .end    = MEM_FBIPP_BASE + MEM_FBIPP_SIZE - 1,
        .flags  = IORESOURCE_MEM,
    },
    #endif
};

/*platform_device*/
struct platform_device rk29_device_fb = {
	.name		  = "rk29-fb",
	.id		  = 4,
	.num_resources	  = ARRAY_SIZE(rk29_fb_resource),
	.resource	  = rk29_fb_resource,
	.dev            = {
		.platform_data  = &rk29_fb_info,
	}
};

struct platform_device rk29_device_dma_cpy = {
	.name		  = "dma_memcpy",
	.id		  = 4,

};

#endif

#if defined(CONFIG_RK29_GPIO_SUSPEND)
static void key_gpio_pullupdown_enable(void)
{
	gpio_pull_updown(RK29_PIN6_PA0, 0);
	gpio_pull_updown(RK29_PIN6_PA1, 0);
	gpio_pull_updown(RK29_PIN6_PA2, 0);
	gpio_pull_updown(RK29_PIN6_PA3, 0);
	gpio_pull_updown(RK29_PIN6_PA4, 0);
	gpio_pull_updown(RK29_PIN6_PA5, 0);
	gpio_pull_updown(RK29_PIN6_PA6, 0);
}

static void key_gpio_pullupdown_disable(void)
{
	gpio_pull_updown(RK29_PIN6_PA0, 1);
	gpio_pull_updown(RK29_PIN6_PA1, 1);
	gpio_pull_updown(RK29_PIN6_PA2, 1);
	gpio_pull_updown(RK29_PIN6_PA3, 1);
	gpio_pull_updown(RK29_PIN6_PA4, 1);
	gpio_pull_updown(RK29_PIN6_PA5, 1);
	gpio_pull_updown(RK29_PIN6_PA6, 1);
}

void rk29_setgpio_suspend_board(void)
{
	key_gpio_pullupdown_enable();
}

void rk29_setgpio_resume_board(void)
{
	key_gpio_pullupdown_disable();
}
#endif

#if defined(CONFIG_RK_IRDA) || defined(CONFIG_BU92747GUW_CIR)
#define BU92747GUW_RESET_PIN         RK29_PIN3_PD4// INVALID_GPIO //
#define BU92747GUW_RESET_MUX_NAME    GPIO3D4_HOSTWRN_NAME//NULL //
#define BU92747GUW_RESET_MUX_MODE    GPIO3H_GPIO3D4//NULL //

#define BU92747GUW_PWDN_PIN          RK29_PIN3_PD3//RK29_PIN5_PA7 //
#define BU92747GUW_PWDN_MUX_NAME     GPIO3D3_HOSTRDN_NAME//GPIO5A7_HSADCDATA2_NAME //
#define BU92747GUW_PWDN_MUX_MODE     GPIO3H_GPIO3D3//GPIO5L_GPIO5A7  //

static int bu92747guw_io_init(void)
{
	int ret;
	
	//reset pin
    if(BU92747GUW_RESET_MUX_NAME != NULL)
    {
        rk29_mux_api_set(BU92747GUW_RESET_MUX_NAME, BU92747GUW_RESET_MUX_MODE);
    }
	ret = gpio_request(BU92747GUW_RESET_PIN, NULL);
	if(ret != 0)
	{
		gpio_free(BU92747GUW_RESET_PIN);
		printk(">>>>>> BU92747GUW_RESET_PIN gpio_request err \n ");
	}
	gpio_direction_output(BU92747GUW_RESET_PIN, GPIO_HIGH);

	//power down pin
    if(BU92747GUW_PWDN_MUX_NAME != NULL)
    {
        rk29_mux_api_set(BU92747GUW_PWDN_MUX_NAME, BU92747GUW_PWDN_MUX_MODE);
    }
	ret = gpio_request(BU92747GUW_PWDN_PIN, NULL);
	if(ret != 0)
	{
		gpio_free(BU92747GUW_PWDN_PIN);
		printk(">>>>>> BU92747GUW_PWDN_PIN gpio_request err \n ");
	}

	//power down as default
	gpio_direction_output(BU92747GUW_PWDN_PIN, GPIO_LOW);
	
	return 0;
}


static int bu92747guw_io_deinit(void)
{
	gpio_free(BU92747GUW_PWDN_PIN);
	gpio_free(BU92747GUW_RESET_PIN);
	return 0;
}

//power ctl func is share with irda and remote
static int nPowerOnCount = 0;
static DEFINE_MUTEX(bu92747_power_mutex);

//1---power on;  0---power off
static int bu92747guw_power_ctl(int enable)
{
    printk("%s \n",__FUNCTION__);

	mutex_lock(&bu92747_power_mutex);
	if (enable) {
		nPowerOnCount++;
		if (nPowerOnCount == 1) {//power on first	
			//smc0_init(NULL);
   	 		gpio_set_value(BU92747GUW_PWDN_PIN, GPIO_HIGH);
			gpio_set_value(BU92747GUW_RESET_PIN, GPIO_LOW);
			mdelay(5);
			gpio_set_value(BU92747GUW_RESET_PIN, GPIO_HIGH);
			mdelay(5);
		}
	}
	else {
		nPowerOnCount--;
		if (nPowerOnCount <= 0) {//power down final
			nPowerOnCount = 0;
			//smc0_exit();
			gpio_set_value(BU92747GUW_PWDN_PIN, GPIO_LOW);
		}
	}
	mutex_unlock(&bu92747_power_mutex);
	return 0;
}
#endif

#ifdef CONFIG_RK_IRDA
#define IRDA_IRQ_PIN           RK29_PIN5_PB2
#define IRDA_IRQ_MUX_NAME      GPIO5B2_HSADCDATA5_NAME
#define IRDA_IRQ_MUX_MODE      GPIO5L_GPIO5B2

int irda_iomux_init(void)
{
    int ret = 0;

	//irda irq pin
    if(IRDA_IRQ_MUX_NAME != NULL)
    {
        rk29_mux_api_set(IRDA_IRQ_MUX_NAME, IRDA_IRQ_MUX_MODE);
    }
	ret = gpio_request(IRDA_IRQ_PIN, NULL);
	if(ret != 0)
	{
		gpio_free(IRDA_IRQ_PIN);
		printk(">>>>>> IRDA_IRQ_PIN gpio_request err \n ");
	}
	gpio_pull_updown(IRDA_IRQ_PIN, GPIO_HIGH);
	gpio_direction_input(IRDA_IRQ_PIN);

    return 0;
}

int irda_iomux_deinit(void)
{
	gpio_free(IRDA_IRQ_PIN);
	return 0;
}

static struct irda_info rk29_irda_info = {
    .intr_pin = IRDA_IRQ_PIN,
    .iomux_init = irda_iomux_init,
    .iomux_deinit = irda_iomux_deinit,
	.irda_pwr_ctl = bu92747guw_power_ctl,
};

static struct platform_device irda_device = {
#ifdef CONFIG_RK_IRDA_NET
			.name	= "rk_irda",
#else
			.name = "bu92747_irda",
#endif
    .id		  = -1,
	.dev            = {
		.platform_data  = &rk29_irda_info,
	}
};
#endif

#ifdef CONFIG_BU92747GUW_CIR
#define BU92747_CIR_IRQ_PIN RK29_PIN5_PB0
#define CIR_IRQ_PIN_IOMUX_NAME GPIO5B0_HSADCDATA3_NAME
#define CIR_IRQ_PIN_IOMUX_VALUE GPIO5L_GPIO5B0
static int cir_iomux_init(void)
{
	if (CIR_IRQ_PIN_IOMUX_NAME)
		rk29_mux_api_set(CIR_IRQ_PIN_IOMUX_NAME, CIR_IRQ_PIN_IOMUX_VALUE);
	rk29_mux_api_set(GPIO5A7_HSADCDATA2_NAME, GPIO5L_GPIO5A7);
	return 0;
}

static struct  bu92747guw_platform_data bu92747guw_pdata = {
	.intr_pin = BU92747_CIR_IRQ_PIN,
	.iomux_init = cir_iomux_init,
	.iomux_deinit = NULL,
	.cir_pwr_ctl = bu92747guw_power_ctl,
};  
#endif
#ifdef CONFIG_RK29_NEWTON
struct rk29_newton_data rk29_newton_info = {	
};
struct platform_device rk29_device_newton = {
	.name          = "rk29_newton",
	.id            = -1,		
	.dev		   = {
	.platform_data = &rk29_newton_info,	
		}    	    
	};
#endif
#if defined (CONFIG_TOUCHSCREEN_FT5406)
#define TOUCH_RESET_PIN RK29_PIN6_PC3
#define TOUCH_INT_PIN   RK29_PIN0_PA2
int ft5406_init_platform_hw(void)
{
	printk("ft5406_init_platform_hw\n");
    if(gpio_request(TOUCH_RESET_PIN,NULL) != 0){
      gpio_free(TOUCH_RESET_PIN);
      printk("ft5406_init_platform_hw gpio_request error\n");
      return -EIO;
    }

    if(gpio_request(TOUCH_INT_PIN,NULL) != 0){
      gpio_free(TOUCH_INT_PIN);
      printk("ift5406_init_platform_hw gpio_request error\n");
      return -EIO;
    }

	gpio_direction_output(TOUCH_RESET_PIN, 0);
	gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
	mdelay(10);
	gpio_direction_input(TOUCH_INT_PIN);
	mdelay(10);
	gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
	msleep(300);
    return 0;
}

void ft5406_exit_platform_hw(void)
{
	printk("ft5406_exit_platform_hw\n");
	gpio_free(TOUCH_RESET_PIN);
	gpio_free(TOUCH_INT_PIN);
}

int ft5406_platform_sleep(void)
{
	printk("ft5406_platform_sleep\n");
	gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
	return 0;
}

int ft5406_platform_wakeup(void)
{
	printk("ft5406_platform_wakeup\n");
	gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
	msleep(300);
	return 0;
}

struct ft5406_platform_data ft5406_info = {

  .init_platform_hw= ft5406_init_platform_hw,
  .exit_platform_hw= ft5406_exit_platform_hw,
  .platform_sleep  = ft5406_platform_sleep,
  .platform_wakeup = ft5406_platform_wakeup,

};
#endif

#if defined(CONFIG_TOUCHSCREEN_GT819)
#define TOUCH_RESET_PIN RK29_PIN6_PC3
#define TOUCH_INT_PIN   RK29_PIN0_PA2
int gt819_init_platform_hw(void)
{
	printk("gt819_init_platform_hw\n");
    if(gpio_request(TOUCH_RESET_PIN,NULL) != 0){
      gpio_free(TOUCH_RESET_PIN);
      printk("gt819_init_platform_hw gpio_request error\n");
      return -EIO;
    }

    if(gpio_request(TOUCH_INT_PIN,NULL) != 0){
      gpio_free(TOUCH_INT_PIN);
      printk("gt819_init_platform_hw gpio_request error\n");
      return -EIO;
    }
	gpio_direction_output(TOUCH_RESET_PIN, 0);
	gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
	mdelay(10);
//	gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
//	mdelay(10);
//	gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
	gpio_direction_input(TOUCH_INT_PIN);
//	mdelay(10);
	gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
	msleep(300);
    return 0;
}


void gt819_exit_platform_hw(void)
{
	printk("gt819_exit_platform_hw\n");
	gpio_free(TOUCH_RESET_PIN);
	gpio_free(TOUCH_INT_PIN);
}

int gt819_platform_sleep(void)
{
	printk("gt819_platform_sleep\n");
	gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
	return 0;
}

int gt819_platform_wakeup(void)
{
	printk("gt819_platform_wakeup\n");
	gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
	//msleep(5);
	//gpio_set_value(TOUCH_INT_PIN, GPIO_LOW); 
	//msleep(20);
	//gpio_set_value(TOUCH_INT_PIN, GPIO_HIGH);
	return 0;
}
struct goodix_platform_data goodix_info = {

  .init_platform_hw= gt819_init_platform_hw,
  .exit_platform_hw= gt819_exit_platform_hw,
  .platform_sleep  = gt819_platform_sleep,
  .platform_wakeup = gt819_platform_wakeup,

};
#endif

/*goodix touch*/
#if defined(CONFIG_TOUCHSCREEN_GT8XX)
#define TOUCH_RESET_PIN  RK29_PIN6_PC3
#define TOUCH_PWR_PIN    RK29_PIN5_PA1
int goodix_init_platform_hw(void)
{
	int ret;
	printk("goodix_init_platform_hw\n");
	ret = gpio_request(TOUCH_PWR_PIN, "goodix power pin");
	if(ret != 0){
		gpio_free(TOUCH_PWR_PIN);
		printk("goodix power error\n");
		return -EIO;
	}
	gpio_direction_output(TOUCH_PWR_PIN, 0);
	gpio_set_value(TOUCH_PWR_PIN,GPIO_LOW);
	msleep(100);
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
	return 0;
}

struct goodix_platform_data goodix_info = {
	  .model= 8105,
	  .irq_pin = RK29_PIN0_PA2,
	  .rest_pin  = TOUCH_RESET_PIN,
	  .init_platform_hw = goodix_init_platform_hw,
};
#endif

#if defined (CONFIG_SND_SOC_CS42L52)

int cs42l52_init_platform_hw()
{
	printk("cs42l52_init_platform_hw\n");
    if(gpio_request(RK29_PIN6_PB6,NULL) != 0){
      gpio_free(RK29_PIN6_PB6);
      printk("cs42l52_init_platform_hw gpio_request error\n");
      return -EIO;
    }
    gpio_direction_output(RK29_PIN6_PB6, 0);
	gpio_set_value(RK29_PIN6_PB6,GPIO_HIGH);
	return 0;
}
struct cs42l52_platform_data cs42l52_info = {

  .init_platform_hw= cs42l52_init_platform_hw,

};
#endif
#if defined (CONFIG_BATTERY_BQ27541)
#define	DC_CHECK_PIN	RK29_PIN4_PA1
#define	LI_LION_BAT_NUM	1
#define CHG_OK RK29_PIN4_PA3
#define BAT_LOW	RK29_PIN4_PA2

static int bq27541_init_dc_check_pin(void){	
	if(gpio_request(DC_CHECK_PIN,"dc_check") != 0){      
		gpio_free(DC_CHECK_PIN);      
		printk("bq27541 init dc check pin request error\n");      
		return -EIO;    
	}	
	gpio_direction_input(DC_CHECK_PIN);	
	return 0;
}

struct bq27541_platform_data bq27541_info = {	
	.init_dc_check_pin = bq27541_init_dc_check_pin,	
	.dc_check_pin =  DC_CHECK_PIN,		
	.bat_num = LI_LION_BAT_NUM,
	.chgok_check_pin =  CHG_OK,
	.bat_check_pin =  BAT_LOW,
};
#endif
static struct android_pmem_platform_data android_pmem_pdata = {
	.name		= "pmem",
	.start		= PMEM_UI_BASE,
	.size		= PMEM_UI_SIZE,
	.no_allocator	= 1,
	.cached		= 1,
};

static struct platform_device android_pmem_device = {
	.name		= "android_pmem",
	.id		= 0,
	.dev		= {
		.platform_data = &android_pmem_pdata,
	},
};


static struct vpu_mem_platform_data vpu_mem_pdata = {
	.name		= "vpu_mem",
	.start		= PMEM_VPU_BASE,
	.size		= PMEM_VPU_SIZE,
	.cached		= 1,
};

static struct platform_device rk29_vpu_mem_device = {
	.name		= "vpu_mem",
	.id		    = 2,
	.dev		= {
	.platform_data = &vpu_mem_pdata,
	},
};

#if PMEM_SKYPE_SIZE > 0
static struct android_pmem_platform_data android_pmem_skype_pdata = {
	.name		= "pmem_skype",
	.start		= PMEM_SKYPE_BASE,
	.size		= PMEM_SKYPE_SIZE,
	.no_allocator	= 0,
	.cached		= 0,
};

static struct platform_device android_pmem_skype_device = {
	.name		= "android_pmem",
	.id		= 3,
	.dev		= {
		.platform_data = &android_pmem_skype_pdata,
	},
};
#endif

#ifdef CONFIG_ION
static struct ion_platform_data rk29_ion_pdata = {
	.nr = 1,
	.heaps = {
		{
			.type = ION_HEAP_TYPE_CARVEOUT,
			.id = 0,
			.name = "ui",
			.base = PMEM_UI_BASE,
			.size = PMEM_UI_SIZE,
		}
	},
};

static struct platform_device rk29_ion_device = {
	.name = "ion-rockchip",
	.id = 0,
	.dev = {
		.platform_data = &rk29_ion_pdata,
	},
};
#endif
#ifdef CONFIG_VIDEO_RK29XX_VOUT
static struct platform_device rk29_v4l2_output_devce = {
	.name		= "rk29_vout",
};
#endif
/*HANNSTAR_P1003 touch*/
#if defined (CONFIG_HANNSTAR_P1003)
#define TOUCH_RESET_PIN RK29_PIN6_PC3
#define TOUCH_INT_PIN   RK29_PIN0_PA2

int p1003_init_platform_hw(void)
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


struct p1003_platform_data p1003_info = {
  .model= 1003,
  .init_platform_hw= p1003_init_platform_hw,

};
#endif
#if defined (CONFIG_EETI_EGALAX)
#define TOUCH_RESET_PIN RK29_PIN6_PC3
#define TOUCH_INT_PIN   RK29_PIN0_PA2

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
  .standby_pin = TOUCH_SCREEN_STANDBY_PIN,
  .standby_value = TOUCH_SCREEN_STANDBY_VALUE,
  .disp_on_pin = TOUCH_SCREEN_DISPLAY_PIN,
  .disp_on_value = TOUCH_SCREEN_DISPLAY_VALUE,
};
#endif

#ifdef CONFIG_GS_KXTF9
#include <linux/kxtf9.h>
#define KXTF9_DEVICE_MAP 1
#define KXTF9_MAP_X (KXTF9_DEVICE_MAP-1)%2
#define KXTF9_MAP_Y KXTF9_DEVICE_MAP%2
#define KXTF9_NEG_X (KXTF9_DEVICE_MAP/2)%2
#define KXTF9_NEG_Y (KXTF9_DEVICE_MAP+1)/4
#define KXTF9_NEG_Z (KXTF9_DEVICE_MAP-1)/4
struct kxtf9_platform_data kxtf9_pdata = {
	.min_interval = 1,
	.poll_interval = 20,
	.g_range = KXTF9_G_2G,
	.axis_map_x = KXTF9_MAP_X,
	.axis_map_y = KXTF9_MAP_Y,
	.axis_map_z = 2,
	.negate_x = KXTF9_NEG_X,
	.negate_y = KXTF9_NEG_Y,
	.negate_z = KXTF9_NEG_Z,
	//.ctrl_regc_init = KXTF9_G_2G | ODR50F,
	//.ctrl_regb_init = ENABLE,
};
#endif /* CONFIG_GS_KXTF9 */


/*MMA8452 gsensor*/
#if defined (CONFIG_GS_MMA8452)
#define MMA8452_INT_PIN   RK29_PIN0_PA3
static int mma8452_init_platform_hw(void)
{

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
  .swap_xyz= 1,
  .swap_xy=0,
  .orientation ={1,0,0,0,0,-1,0,-1,0},
  .init_platform_hw= mma8452_init_platform_hw,

};
#endif
/*mpu3050*/
#if defined (CONFIG_MPU_SENSORS_MPU3050)
static struct mpu_platform_data mpu3050_data = {
	.int_config = 0x10,
	.orientation = { 1, 0, 0,0, 1, 0, 0, 0, 1 },
};
#endif

/* accel */
#if defined (CONFIG_MPU_SENSORS_KXTF9)
static struct ext_slave_platform_data inv_mpu_kxtf9_data = {
	.bus         = EXT_SLAVE_BUS_SECONDARY,
	.adapt_num = 0,
	.orientation = {1, 0, 0, 0, 1, 0, 0, 0, 1},
};
#endif

/* compass */
#if defined (CONFIG_MPU_SENSORS_AK8975)
static struct ext_slave_platform_data inv_mpu_ak8975_data = {
	.bus         = EXT_SLAVE_BUS_PRIMARY,
	.adapt_num = 0,
	.orientation = {0, 1, 0, -1, 0, 0, 0, 0, 1},
};
#endif

#if defined (CONFIG_BATTERY_BQ27510)
#define	DC_CHECK_PIN	RK29_PIN4_PA1
#define	LI_LION_BAT_NUM	2
static int bq27510_init_dc_check_pin(void){	
	if(gpio_request(DC_CHECK_PIN,"dc_check") != 0){      
		gpio_free(DC_CHECK_PIN);      
		printk("bq27510 init dc check pin request error\n");      
		return -EIO;    
	}	
	gpio_direction_input(DC_CHECK_PIN);	
	return 0;
}

struct bq27510_platform_data bq27510_info = {	
	.init_dc_check_pin = bq27510_init_dc_check_pin,	
	.dc_check_pin =  DC_CHECK_PIN,		
	.bat_num = LI_LION_BAT_NUM,
};
#endif

#ifdef CONFIG_BATTERY_RK29_ADC
static struct rk29_adc_battery_platform_data rk29_adc_battery_platdata = {
	.dc_det_pin      = RK29_PIN4_PA1,
	.batt_low_pin    = RK29_PIN4_PA2,
	.charge_set_pin  = INVALID_GPIO,
	.charge_ok_pin   = RK29_PIN4_PA3,
	
	.dc_det_level    = GPIO_LOW,
	.charge_ok_level = GPIO_HIGH,
};

static struct platform_device rk29_adc_device_battery = {
	.name   = "rk2918-battery",
	.id     = -1,
	.dev = {
		.platform_data = &rk29_adc_battery_platdata,
	},
};
#endif

/*************************************PMU ACT8891****************************************/

#if defined (CONFIG_REGULATOR_ACT8891) 
		/*dcdc mode*/
/*act8891 in REGULATOR_MODE_STANDBY mode is said DCDC is in PMF mode is can save power,when in REGULATOR_MODE_NORMAL 
mode is said DCDC is in PWM mode , General default is in REGULATOR_MODE_STANDBY mode*/
		/*ldo mode */
/*act8891 in REGULATOR_MODE_STANDBY mode is said LDO is in low power mode is can save power,when in REGULATOR_MODE_NORMAL 
mode is said DCDC is in nomal mode , General default is in REGULATOR_MODE_STANDBY mode*/
/*set dcdc and ldo voltage by regulator_set_voltage()*/
static struct act8891 *act8891;
int act8891_set_init(struct act8891 *act8891)
{
	int tmp = 0;
	struct regulator *act_ldo1,*act_ldo2,*act_ldo3,*act_ldo4;
	struct regulator *act_dcdc1,*act_dcdc2,*act_dcdc3;

	/*init ldo1*/
	act_ldo1 = regulator_get(NULL, "act_ldo1");
	regulator_enable(act_ldo1); 
	regulator_set_voltage(act_ldo1,1800000,1800000);
	tmp = regulator_get_voltage(act_ldo1);
	regulator_set_mode(act_ldo1,REGULATOR_MODE_STANDBY);
	//regulator_set_mode(act_ldo1,REGULATOR_MODE_NORMAL);
	printk("***regulator_set_init: ldo1 vcc =%d\n",tmp);
	regulator_put(act_ldo1);
	 
	/*init ldo2*/
	act_ldo2 = regulator_get(NULL, "act_ldo2");
	regulator_enable(act_ldo2);
	regulator_set_voltage(act_ldo2,1200000,1200000);
	tmp = regulator_get_voltage(act_ldo2);
	regulator_set_mode(act_ldo2,REGULATOR_MODE_STANDBY);
	//regulator_set_mode(act_ldo2,REGULATOR_MODE_NORMAL);
	printk("***regulator_set_init: ldo2 vcc =%d\n",tmp);
	regulator_put(act_ldo2);

	/*init ldo3*/
	act_ldo3 = regulator_get(NULL, "act_ldo3");
	regulator_enable(act_ldo3);
	regulator_set_voltage(act_ldo3,3300000,3300000);
	tmp = regulator_get_voltage(act_ldo3);
	regulator_set_mode(act_ldo3,REGULATOR_MODE_STANDBY);
	//regulator_set_mode(act_ldo3,REGULATOR_MODE_NORMAL);
	printk("***regulator_set_init: ldo3 vcc =%d\n",tmp);
	regulator_put(act_ldo3);

	/*init ldo4*/
	act_ldo4 = regulator_get(NULL, "act_ldo4");
	regulator_enable(act_ldo4);
	regulator_set_voltage(act_ldo4,2500000,2500000);
	tmp = regulator_get_voltage(act_ldo4);
	regulator_set_mode(act_ldo4,REGULATOR_MODE_STANDBY);
	//regulator_set_mode(act_ldo4,REGULATOR_MODE_NORMAL);
	printk("***regulator_set_init: ldo4 vcc =%d\n",tmp);
	regulator_put(act_ldo4);

	/*init dcdc1*/
	act_dcdc1 = regulator_get(NULL, "act_dcdc1");
	regulator_enable(act_dcdc1);
	regulator_set_voltage(act_dcdc1,3000000,3000000);
	tmp = regulator_get_voltage(act_dcdc1);
	regulator_set_mode(act_dcdc1,REGULATOR_MODE_STANDBY);
	//regulator_set_mode(act_dcdc1,REGULATOR_MODE_NORMAL);
	printk("***regulator_set_init: dcdc1 vcc =%d\n",tmp); 
	regulator_put(act_dcdc1);

	/*init dcdc2*/
	act_dcdc2 = regulator_get(NULL, "act_dcdc2");
	regulator_enable(act_dcdc2);
	regulator_set_voltage(act_dcdc2,1500000,1500000);
	tmp = regulator_get_voltage(act_dcdc2);
	regulator_set_mode(act_dcdc2,REGULATOR_MODE_STANDBY);
	//regulator_set_mode(act_dcdc2,REGULATOR_MODE_NORMAL);
	printk("***regulator_set_init: dcdc2 vcc =%d\n",tmp);
	regulator_put(act_dcdc2);

		/*init dcdc3*/
	act_dcdc3 = regulator_get(NULL, "act_dcdc3");
	regulator_enable(act_dcdc3);
	regulator_set_voltage(act_dcdc3,1200000,1200000);
	tmp = regulator_get_voltage(act_dcdc3);
	regulator_set_mode(act_dcdc3,REGULATOR_MODE_STANDBY);
	//regulator_set_mode(act_dcdc3,REGULATOR_MODE_NORMAL);
	printk("***regulator_set_init: dcdc3 vcc =%d\n",tmp);
	regulator_put(act_dcdc3);

	return(0);
}

static struct regulator_consumer_supply act8891_ldo1_consumers[] = {
	{
		.supply = "act_ldo1",
	}
};

static struct regulator_init_data act8891_ldo1_data = {
	.constraints = {
		.name = "ACT_LDO1",
		.min_uV = 600000,
		.max_uV = 3900000,
		.apply_uV = 1,		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(act8891_ldo1_consumers),
	.consumer_supplies = act8891_ldo1_consumers,
};

/**/
static struct regulator_consumer_supply act8891_ldo2_consumers[] = {
	{
		.supply = "act_ldo2",
	}
};

static struct regulator_init_data act8891_ldo2_data = {
	.constraints = {
		.name = "ACT_LDO2",
		.min_uV = 600000,
		.max_uV = 3900000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,	
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(act8891_ldo2_consumers),
	.consumer_supplies = act8891_ldo2_consumers,
};

/*ldo3 VCC_NAND WIFI/BT/FM_BCM4325*/
static struct regulator_consumer_supply act8891_ldo3_consumers[] = {
	{
		.supply = "act_ldo3",
	}
};

static struct regulator_init_data act8891_ldo3_data = {
	.constraints = {
		.name = "ACT_LDO3",
		.min_uV = 600000,
		.max_uV = 3900000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(act8891_ldo3_consumers),
	.consumer_supplies = act8891_ldo3_consumers,
};

/*ldo4 VCCA CODEC_WM8994*/
static struct regulator_consumer_supply act8891_ldo4_consumers[] = {
	{
		.supply = "act_ldo4",
	}
};

static struct regulator_init_data act8891_ldo4_data = {
	.constraints = {
		.name = "ACT_LDO4",
		.min_uV = 600000,
		.max_uV = 3900000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(act8891_ldo4_consumers),
	.consumer_supplies = act8891_ldo4_consumers,
};
/*buck1 vcc Core*/
static struct regulator_consumer_supply act8891_dcdc1_consumers[] = {
	{
		.supply = "act_dcdc1",
	}
};

static struct regulator_init_data act8891_dcdc1_data = {
	.constraints = {
		.name = "ACT_DCDC1",
		.min_uV = 600000,
		.max_uV = 3900000,
		.apply_uV = 1,
		//.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(act8891_dcdc1_consumers),
	.consumer_supplies = act8891_dcdc1_consumers
};

/*buck2 VDDDR MobileDDR VCC*/
static struct regulator_consumer_supply act8891_dcdc2_consumers[] = {
	{
		.supply = "act_dcdc2",
	}
};

static struct regulator_init_data act8891_dcdc2_data = {
	.constraints = {
		.name = "ACT_DCDC2",
		.min_uV = 600000,
		.max_uV = 3900000,
		.apply_uV = 1,
		//.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(act8891_dcdc2_consumers),
	.consumer_supplies = act8891_dcdc2_consumers
};

/*buck3 vdd Core*/
static struct regulator_consumer_supply act8891_dcdc3_consumers[] = {
	{
		.supply = "act_dcdc3",
	}
};

static struct regulator_init_data act8891_dcdc3_data = {
	.constraints = {
		.name = "ACT_DCDC3",
		.min_uV = 600000,
		.max_uV = 3900000,
		.apply_uV = 1,
		//.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_STANDBY | REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(act8891_dcdc3_consumers),
	.consumer_supplies = act8891_dcdc3_consumers
};

struct act8891_regulator_subdev act8891_regulator_subdev_id[] = {
	{
		.id=0,
		.initdata=&act8891_ldo1_data,		
	 },

	{
		.id=1,
		.initdata=&act8891_ldo2_data,		
	 },

	{
		.id=2,
		.initdata=&act8891_ldo3_data,		
	 },

	{
		.id=3,
		.initdata=&act8891_ldo4_data,		
	 },

	{
		.id=4,
		.initdata=&act8891_dcdc1_data,		
	 },

	{
		.id=5,
		.initdata=&act8891_dcdc2_data,		
	 },
	{
		.id=6,
		.initdata=&act8891_dcdc3_data,		
	 },

};

struct act8891_platform_data act8891_data={
	.set_init=act8891_set_init,
	.num_regulators=7,
	.regulators=act8891_regulator_subdev_id,
	
};
#endif

/*****************************************************************************************
 * i2c devices
 * author: kfx@rock-chips.com
*****************************************************************************************/
static int rk29_i2c0_io_init(void)
{
#ifdef CONFIG_RK29_I2C0_CONTROLLER
	rk29_mux_api_set(GPIO2B7_I2C0SCL_NAME, GPIO2L_I2C0_SCL);
	rk29_mux_api_set(GPIO2B6_I2C0SDA_NAME, GPIO2L_I2C0_SDA);
#else
	rk29_mux_api_set(GPIO2B7_I2C0SCL_NAME, GPIO2L_GPIO2B7);
	rk29_mux_api_set(GPIO2B6_I2C0SDA_NAME, GPIO2L_GPIO2B6);
#endif
	return 0;
}

static int rk29_i2c1_io_init(void)
{
#ifdef CONFIG_RK29_I2C1_CONTROLLER
	rk29_mux_api_set(GPIO1A7_I2C1SCL_NAME, GPIO1L_I2C1_SCL);
	rk29_mux_api_set(GPIO1A6_I2C1SDA_NAME, GPIO1L_I2C1_SDA);
#else
	rk29_mux_api_set(GPIO1A7_I2C1SCL_NAME, GPIO1L_GPIO1A7);
	rk29_mux_api_set(GPIO1A6_I2C1SDA_NAME, GPIO1L_GPIO1A6);
#endif
	return 0;
}
static int rk29_i2c2_io_init(void)
{
#ifdef CONFIG_RK29_I2C2_CONTROLLER
	rk29_mux_api_set(GPIO5D4_I2C2SCL_NAME, GPIO5H_I2C2_SCL);
	rk29_mux_api_set(GPIO5D3_I2C2SDA_NAME, GPIO5H_I2C2_SDA);
#else
	rk29_mux_api_set(GPIO5D4_I2C2SCL_NAME, GPIO5H_GPIO5D4);
	rk29_mux_api_set(GPIO5D3_I2C2SDA_NAME, GPIO5H_GPIO5D3);
#endif
	return 0;
}

static int rk29_i2c3_io_init(void)
{
#ifdef CONFIG_RK29_I2C3_CONTROLLER
	rk29_mux_api_set(GPIO2B5_UART3RTSN_I2C3SCL_NAME, GPIO2L_I2C3_SCL);
	rk29_mux_api_set(GPIO2B4_UART3CTSN_I2C3SDA_NAME, GPIO2L_I2C3_SDA);
#else
	rk29_mux_api_set(GPIO2B5_UART3RTSN_I2C3SCL_NAME, GPIO2L_GPIO2B5);
	rk29_mux_api_set(GPIO2B4_UART3CTSN_I2C3SDA_NAME, GPIO2L_GPIO2B4);
#endif
	return 0;
}
#ifdef CONFIG_RK29_I2C0_CONTROLLER
struct rk29_i2c_platform_data default_i2c0_data = {
	.bus_num    = 0,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode 		= I2C_MODE_IRQ,
	.io_init = rk29_i2c0_io_init,
};
#else
struct i2c_gpio_platform_data default_i2c0_data = {
       .sda_pin = RK29_PIN2_PB6,
       .scl_pin = RK29_PIN2_PB7,
       .udelay = 5, // clk = 500/udelay = 100Khz
       .timeout = 100,//msecs_to_jiffies(200),
       .bus_num    = 0,
       .io_init = rk29_i2c0_io_init,
};
#endif
#ifdef CONFIG_RK29_I2C1_CONTROLLER
struct rk29_i2c_platform_data default_i2c1_data = {
	.bus_num    = 1,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode 		= I2C_MODE_IRQ,
	.io_init = rk29_i2c1_io_init,
};
#else
struct i2c_gpio_platform_data default_i2c1_data = {
       .sda_pin = RK29_PIN1_PA6,
       .scl_pin = RK29_PIN1_PA7,
       .udelay = 5, // clk = 500/udelay = 100Khz
       .timeout = 100,//msecs_to_jiffies(200),
       .bus_num    = 1,
       .io_init = rk29_i2c1_io_init,
};
#endif
#ifdef CONFIG_RK29_I2C2_CONTROLLER
struct rk29_i2c_platform_data default_i2c2_data = {
	.bus_num    = 2,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode 		= I2C_MODE_IRQ,
	.io_init = rk29_i2c2_io_init,
};
#else
struct i2c_gpio_platform_data default_i2c2_data = {
       .sda_pin = RK29_PIN5_PD3,
       .scl_pin = RK29_PIN5_PD4,
       .udelay = 5, // clk = 500/udelay = 100Khz
       .timeout = 100,//msecs_to_jiffies(200),
       .bus_num    = 2,
       .io_init = rk29_i2c2_io_init,
};
#endif
#ifdef CONFIG_RK29_I2C3_CONTROLLER
struct rk29_i2c_platform_data default_i2c3_data = {
	.bus_num    = 3,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode 		= I2C_MODE_IRQ,
	.io_init = rk29_i2c3_io_init,
};
#else
struct i2c_gpio_platform_data default_i2c3_data = {
       .sda_pin = RK29_PIN5_PB5,
       .scl_pin = RK29_PIN5_PB4,
       .udelay = 5, // clk = 500/udelay = 100Khz
       .timeout = 100,//msecs_to_jiffies(200),
       .bus_num    = 3,
       .io_init = rk29_i2c3_io_init,
};
#endif
#ifdef CONFIG_I2C0_RK29
static struct i2c_board_info __initdata board_i2c0_devices[] = {
#if defined (CONFIG_RK1000_CONTROL)
	{
		.type    		= "rk1000_control",
		.addr           = 0x40,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_SND_SOC_RT5621)
        {
                .type                   = "rt5621",
                .addr                   = 0x1a,
                .flags                  = 0,
        },
#endif
#if defined (CONFIG_SND_SOC_RT5631)
        {
                .type                   = "rt5631",
                .addr                   = 0x1a,
                .flags                  = 0,
        },
#endif
#if defined (CONFIG_SND_SOC_RK1000)
	{
		.type    		= "rk1000_i2c_codec",
		.addr           = 0x60,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_SND_SOC_WM8988)
	{
		.type                   = "wm8988",
		.addr           = 0x1A,
		.flags                  = 0,
	},
#endif
#if defined (CONFIG_SND_SOC_WM8900)
	{
		.type    		= "wm8900",
		.addr           = 0x1A,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_BATTERY_STC3100)
	{
		.type    		= "stc3100",
		.addr           = 0x70,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_BATTERY_BQ27510)
	{
		.type    		= "bq27510",
		.addr           = 0x55,
		.flags			= 0,
		.platform_data  = &bq27510_info,
	},
#endif
#if defined (CONFIG_RTC_HYM8563)
	{
		.type    		= "rtc_hym8563",
		.addr           = 0x51,
		.flags			= 0,
		.irq            = RK29_PIN0_PA1,
	},
#endif
#if defined (CONFIG_GS_MMA8452)
    {
      .type           = "gs_mma8452",
      .addr           = 0x1c,
      .flags          = 0,
      .irq            = MMA8452_INT_PIN,
      .platform_data  = &mma8452_info,
    },
#endif
#if defined (CONFIG_COMPASS_AK8973)
	{
		.type    		= "ak8973",
		.addr           = 0x1d,
		.flags			= 0,
		.irq			= RK29_PIN0_PA4,
	},
#endif
#if defined (CONFIG_COMPASS_AK8975)
	{
		.type    		= "ak8975",
		.addr           = 0x0d,
		.flags			= 0,
		.irq			= RK29_PIN0_PA4,
	},
#endif
#if defined (CONFIG_MPU_SENSORS_MPU3050) 
	{
		.type 			= "mpu3050",
		.addr			= 0x68,
		.flags			= 0,
		.irq			= RK29_PIN4_PC4,
		.platform_data  = &mpu3050_data,
	},
#endif
#if defined (CONFIG_MPU_SENSORS_KXTF9)
	{
		.type    		= "kxtf9",
		.addr           = 0x0f,
		.flags			= 0,	
		//.irq 			= RK29_PIN6_PC4,
		.platform_data = &inv_mpu_kxtf9_data,
	},
#endif
#if defined (CONFIG_MPU_SENSORS_AK8975)
	{
		.type			= "ak8975",
		.addr			= 0x0d,
		.flags			= 0,	
		//.irq 			= RK29_PIN6_PC5,
		.platform_data = &inv_mpu_ak8975_data,
	},
#endif

#if defined (CONFIG_SND_SOC_CS42L52)
	{
		.type    		= "cs42l52",
		.addr           = 0x4A,
		.flags			= 0,
		.platform_data	= &cs42l52_info,
	},
#endif
#if defined (CONFIG_RTC_M41T66)
	{
		.type           = "rtc-M41T66",
		.addr           = 0x68,
		.flags          = 0,
		.irq            = RK29_PIN0_PA1,
	},
#endif
};
#endif
#if defined (CONFIG_ANX7150)
struct hdmi_platform_data anx7150_data = {
       //.io_init = anx7150_io_init,
};
#endif
#ifdef CONFIG_I2C1_RK29
static struct i2c_board_info __initdata board_i2c1_devices[] = {
#if defined (CONFIG_RK1000_CONTROL1)
	{
		.type			= "rk1000_control",
		.addr			= 0x40,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_ANX7150)
    {
		.type           = "anx7150",
        .addr           = 0x39,             //0x39, 0x3d
        .flags          = 0,
        .irq            = RK29_PIN1_PD7,
		.platform_data  = &anx7150_data,
    },
#endif
#ifdef CONFIG_BU92747GUW_CIR
    {
    	.type	="bu92747_cir",
    	.addr 	= 0x77,    
    	.flags      =0,
    	.irq		= BU92747_CIR_IRQ_PIN,
    	.platform_data = &bu92747guw_pdata,
    },
#endif

};
#endif

#ifdef CONFIG_I2C2_RK29
static struct i2c_board_info __initdata board_i2c2_devices[] = {
#if defined (CONFIG_HANNSTAR_P1003)
    {
      .type           = "p1003_touch",
      .addr           = 0x04,
      .flags          = 0, //I2C_M_NEED_DELAY
      .irq            = RK29_PIN0_PA2,
      .platform_data  = &p1003_info,
      //.udelay		  = 100
    },
#endif
#if defined (CONFIG_EETI_EGALAX)
    {
      .type           = "egalax_i2c",
      .addr           = 0x04,
      .flags          = 0,
      .irq            = RK29_PIN0_PA2,
      .platform_data  = &eeti_egalax_info,
    },
#endif
#if defined (CONFIG_TOUCHSCREEN_GT8XX)
    {
		.type	= "Goodix-TS",
		.addr 	= 0x55,
		.flags      =0,
		.irq		=RK29_PIN0_PA2,
		.platform_data = &goodix_info,
    },
#endif
#if defined (CONFIG_TOUCHSCREEN_FT5406)
    {
		.type	="ft5x0x_ts",
		.addr 	= 0x38,    //0x70,
		.flags      =0,
		.irq		=RK29_PIN0_PA2, // support goodix tp detect, 20110706
		.platform_data = &ft5406_info,
    },
#endif
};
#endif

#ifdef CONFIG_I2C3_RK29
static struct i2c_board_info __initdata board_i2c3_devices[] = {
#if defined (CONFIG_BATTERY_BQ27541)
	{
		.type    		= "bq27541",
		.addr           = 0x55,
		.flags			= 0,
		.platform_data  = &bq27541_info,
	},
#endif
#if defined (CONFIG_REGULATOR_ACT8891)
	{
		.type    		= "act8891",
		.addr           = 0x5b, 
		.flags			= 0,
		.platform_data=&act8891_data,
	},
#endif

};
#endif

/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_VIDEO_RK29
#define CONFIG_SENSOR_POWER_IOCTL_USR      0
#define CONFIG_SENSOR_RESET_IOCTL_USR      0
#define CONFIG_SENSOR_POWERDOWN_IOCTL_USR      0
#define CONFIG_SENSOR_FLASH_IOCTL_USR      0

#if CONFIG_SENSOR_POWER_IOCTL_USR
static int sensor_power_usr_cb (struct rk29camera_gpio_res *res,int on)
{
    #error "CONFIG_SENSOR_POWER_IOCTL_USR is 1, sensor_power_usr_cb function must be writed!!";
}
#endif

#if CONFIG_SENSOR_RESET_IOCTL_USR
static int sensor_reset_usr_cb (struct rk29camera_gpio_res *res,int on)
{
    #error "CONFIG_SENSOR_RESET_IOCTL_USR is 1, sensor_reset_usr_cb function must be writed!!";
}
#endif

#if CONFIG_SENSOR_POWERDOWN_IOCTL_USR
static int sensor_powerdown_usr_cb (struct rk29camera_gpio_res *res,int on)
{
    #error "CONFIG_SENSOR_POWERDOWN_IOCTL_USR is 1, sensor_powerdown_usr_cb function must be writed!!";
}
#endif

#if CONFIG_SENSOR_FLASH_IOCTL_USR
static int sensor_flash_usr_cb (struct rk29camera_gpio_res *res,int on)
{
    #error "CONFIG_SENSOR_FLASH_IOCTL_USR is 1, sensor_flash_usr_cb function must be writed!!";
}
#endif

static struct rk29camera_platform_ioctl_cb  sensor_ioctl_cb = {
    #if CONFIG_SENSOR_POWER_IOCTL_USR
    .sensor_power_cb = sensor_power_usr_cb,
    #else
    .sensor_power_cb = NULL,
    #endif

    #if CONFIG_SENSOR_RESET_IOCTL_USR
    .sensor_reset_cb = sensor_reset_usr_cb,
    #else
    .sensor_reset_cb = NULL,
    #endif

    #if CONFIG_SENSOR_POWERDOWN_IOCTL_USR
    .sensor_powerdown_cb = sensor_powerdown_usr_cb,
    #else
    .sensor_powerdown_cb = NULL,
    #endif

    #if CONFIG_SENSOR_FLASH_IOCTL_USR
    .sensor_flash_cb = sensor_flash_usr_cb,
    #else
    .sensor_flash_cb = NULL,
    #endif
};
#include "../../../drivers/media/video/rk29_camera.c"
#endif
/*****************************************************************************************
 * backlight  devices
 * author: nzy@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_BACKLIGHT_RK29_BL
 /*
 GPIO1B5_PWM0_NAME,       GPIO1L_PWM0
 GPIO5D2_PWM1_UART1SIRIN_NAME,  GPIO5H_PWM1
 GPIO2A3_SDMMC0WRITEPRT_PWM2_NAME,   GPIO2L_PWM2
 GPIO1A5_EMMCPWREN_PWM3_NAME,     GPIO1L_PWM3
 */

#define PWM_ID            0
#define PWM_MUX_NAME      GPIO1B5_PWM0_NAME
#define PWM_MUX_MODE      GPIO1L_PWM0
#define PWM_MUX_MODE_GPIO GPIO1L_GPIO1B5
#define PWM_GPIO RK29_PIN1_PB5
#define PWM_EFFECT_VALUE  0

#define LCD_DISP_ON_PIN

#ifdef  LCD_DISP_ON_PIN
//#define BL_EN_MUX_NAME    GPIOF34_UART3_SEL_NAME
//#define BL_EN_MUX_MODE    IOMUXB_GPIO1_B34

#define BL_EN_PIN         RK29_PIN6_PD0
#define BL_EN_VALUE       GPIO_HIGH
#endif
static int rk29_backlight_io_init(void)
{
    int ret = 0;

    rk29_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE);
	#ifdef  LCD_DISP_ON_PIN
   // rk29_mux_api_set(BL_EN_MUX_NAME, BL_EN_MUX_MODE);

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
    rk29_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE_GPIO);
    gpio_free(PWM_GPIO);
    if (ret = gpio_request(PWM_GPIO, NULL)) {
	  printk("func %s, line %d: request gpio fail\n", __FUNCTION__, __LINE__);
	  return -1;
    }    
    gpio_direction_output(PWM_GPIO, GPIO_HIGH);
    gpio_set_value(PWM_GPIO, GPIO_HIGH);
    return ret;
}

static int rk29_backlight_pwm_suspend(void)
{
	int ret = 0;
	rk29_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE_GPIO);
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
	rk29_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE);

    #ifdef  LCD_DISP_ON_PIN
    msleep(30);
    gpio_direction_output(BL_EN_PIN, 1);
    gpio_set_value(BL_EN_PIN, BL_EN_VALUE);
    #endif
	return 0;
}

struct rk29_bl_info rk29_bl_info = {
    .pwm_id   = PWM_ID,
    .bl_ref   = PWM_EFFECT_VALUE,
    .io_init   = rk29_backlight_io_init,
    .io_deinit = rk29_backlight_io_deinit,
    .pwm_suspend = rk29_backlight_pwm_suspend,
    .pwm_resume = rk29_backlight_pwm_resume,
    .min_brightness = 30,
};
#endif
/*****************************************************************************************
* pwm voltage regulator devices
******************************************************************************************/
#if defined (CONFIG_RK29_PWM_REGULATOR)

#define REGULATOR_PWM_ID					2
#define REGULATOR_PWM_MUX_NAME      		GPIO2A3_SDMMC0WRITEPRT_PWM2_NAME
#define REGULATOR_PWM_MUX_MODE      					GPIO2L_PWM2
#define REGULATOR_PWM_MUX_MODE_GPIO 				GPIO2L_GPIO2A3
#define REGULATOR_PWM_GPIO				RK29_PIN2_PA3

static struct regulator_consumer_supply pwm_consumers[] = {
	{
		.supply = "vcore",
	}
};

static struct regulator_init_data rk29_pwm_regulator_data = {
	.constraints = {
		.name = "PWM2",
		.min_uV =  950000,
		.max_uV = 1400000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(pwm_consumers),
	.consumer_supplies = pwm_consumers,
};

static struct pwm_platform_data rk29_regulator_pwm_platform_data = {
	.pwm_id = REGULATOR_PWM_ID,
	.pwm_gpio = REGULATOR_PWM_GPIO,
	//.pwm_iomux_name[] = REGULATOR_PWM_MUX_NAME;
	.pwm_iomux_name = REGULATOR_PWM_MUX_NAME,
	.pwm_iomux_pwm = REGULATOR_PWM_MUX_MODE,
	.pwm_iomux_gpio = REGULATOR_PWM_MUX_MODE_GPIO,
	.init_data  = &rk29_pwm_regulator_data,
};

static struct platform_device rk29_device_pwm_regulator = {
	.name = "pwm-voltage-regulator",
	.id   = -1,
	.dev  = {
		.platform_data = &rk29_regulator_pwm_platform_data,
	},
};

#endif

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
#define SDMMC0_WRITE_PROTECT_PIN		RK29_PIN6_PB0 //According to your own project to set the value of write-protect-pin.
#endif


#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
#define SDMMC1_WRITE_PROTECT_PIN		RK29_PIN6_PB0 //According to your own project to set the value of write-protect-pin.
#endif

/*****************************************************************************************
 * SDMMC devices
*****************************************************************************************/
#if !defined(CONFIG_SDMMC_RK29_OLD)	
static void rk29_sdmmc_gpio_open(int device_id, int on)
{
    switch(device_id)
    {
        case 0://mmc0
        {
            #ifdef CONFIG_SDMMC0_RK29
            if(on)
            {
                gpio_direction_output(RK29_PIN1_PD0,GPIO_HIGH);//set mmc0-clk to high
                gpio_direction_output(RK29_PIN1_PD1,GPIO_HIGH);//set mmc0-cmd to high.
                gpio_direction_output(RK29_PIN1_PD2,GPIO_HIGH);//set mmc0-data0 to high.
                gpio_direction_output(RK29_PIN1_PD3,GPIO_HIGH);//set mmc0-data1 to high.
                gpio_direction_output(RK29_PIN1_PD4,GPIO_HIGH);//set mmc0-data2 to high.
                gpio_direction_output(RK29_PIN1_PD5,GPIO_HIGH);//set mmc0-data3 to high.

                mdelay(30);
            }
            else
            {
                rk29_mux_api_set(GPIO1D0_SDMMC0CLKOUT_NAME, GPIO1H_GPIO1_D0);
                gpio_request(RK29_PIN1_PD0, "mmc0-clk");
                gpio_direction_output(RK29_PIN1_PD0,GPIO_LOW);//set mmc0-clk to low.

                rk29_mux_api_set(GPIO1D1_SDMMC0CMD_NAME, GPIO1H_GPIO1_D1);
                gpio_request(RK29_PIN1_PD1, "mmc0-cmd");
                gpio_direction_output(RK29_PIN1_PD1,GPIO_LOW);//set mmc0-cmd to low.

                rk29_mux_api_set(GPIO1D2_SDMMC0DATA0_NAME, GPIO1H_GPIO1D2);
                gpio_request(RK29_PIN1_PD2, "mmc0-data0");
                gpio_direction_output(RK29_PIN1_PD2,GPIO_LOW);//set mmc0-data0 to low.

                rk29_mux_api_set(GPIO1D3_SDMMC0DATA1_NAME, GPIO1H_GPIO1D3);
                gpio_request(RK29_PIN1_PD3, "mmc0-data1");
                gpio_direction_output(RK29_PIN1_PD3,GPIO_LOW);//set mmc0-data1 to low.

                rk29_mux_api_set(GPIO1D4_SDMMC0DATA2_NAME, GPIO1H_GPIO1D4);
                gpio_request(RK29_PIN1_PD4, "mmc0-data2");
                gpio_direction_output(RK29_PIN1_PD4,GPIO_LOW);//set mmc0-data2 to low.

                rk29_mux_api_set(GPIO1D5_SDMMC0DATA3_NAME, GPIO1H_GPIO1D5);
                gpio_request(RK29_PIN1_PD5, "mmc0-data3");
                gpio_direction_output(RK29_PIN1_PD5,GPIO_LOW);//set mmc0-data3 to low.

                mdelay(30);
            }
            #endif
        }
        break;
        
        case 1://mmc1
        {
            #ifdef CONFIG_SDMMC1_RK29
            if(on)
            {
                gpio_direction_output(RK29_PIN1_PC7,GPIO_HIGH);//set mmc1-clk to high
                gpio_direction_output(RK29_PIN1_PC2,GPIO_HIGH);//set mmc1-cmd to high.
                gpio_direction_output(RK29_PIN1_PC3,GPIO_HIGH);//set mmc1-data0 to high.
                gpio_direction_output(RK29_PIN1_PC4,GPIO_HIGH);//set mmc1-data1 to high.
                gpio_direction_output(RK29_PIN1_PC5,GPIO_HIGH);//set mmc1-data2 to high.
                gpio_direction_output(RK29_PIN1_PC6,GPIO_HIGH);//set mmc1-data3 to high.
                mdelay(100);
            }
            else
            {
                rk29_mux_api_set(GPIO1C7_SDMMC1CLKOUT_NAME, GPIO1H_GPIO1C7);
                gpio_request(RK29_PIN1_PC7, "mmc1-clk");
                gpio_direction_output(RK29_PIN1_PC7,GPIO_LOW);//set mmc1-clk to low.

                rk29_mux_api_set(GPIO1C2_SDMMC1CMD_NAME, GPIO1H_GPIO1C2);
                gpio_request(RK29_PIN1_PC2, "mmc1-cmd");
                gpio_direction_output(RK29_PIN1_PC2,GPIO_LOW);//set mmc1-cmd to low.

                rk29_mux_api_set(GPIO1C3_SDMMC1DATA0_NAME, GPIO1H_GPIO1C3);
                gpio_request(RK29_PIN1_PC3, "mmc1-data0");
                gpio_direction_output(RK29_PIN1_PC3,GPIO_LOW);//set mmc1-data0 to low.

                mdelay(100);
            }
            #endif
        }
        break; 
        
        case 2: //mmc2
        break;
        
        default:
        break;
    }
}

static void rk29_sdmmc_set_iomux_mmc0(unsigned int bus_width)
{
    switch (bus_width)
    {
        
    	case 1://SDMMC_CTYPE_4BIT:
    	{
        	rk29_mux_api_set(GPIO1D3_SDMMC0DATA1_NAME, GPIO1H_SDMMC0_DATA1);
        	rk29_mux_api_set(GPIO1D4_SDMMC0DATA2_NAME, GPIO1H_SDMMC0_DATA2);
        	rk29_mux_api_set(GPIO1D5_SDMMC0DATA3_NAME, GPIO1H_SDMMC0_DATA3);
    	}
    	break;

    	case 0x10000://SDMMC_CTYPE_8BIT:
    	    break;
    	case 0xFFFF: //gpio_reset
    	{
            rk29_mux_api_set(GPIO5D5_SDMMC0PWREN_NAME, GPIO5H_GPIO5D5);   
            gpio_request(RK29_PIN5_PD5,"sdmmc-power");
            gpio_direction_output(RK29_PIN5_PD5,GPIO_HIGH); //power-off

            rk29_sdmmc_gpio_open(0, 0);

            gpio_direction_output(RK29_PIN5_PD5,GPIO_LOW); //power-on

            rk29_sdmmc_gpio_open(0, 1);
    	}
    	break;

    	default: //case 0://SDMMC_CTYPE_1BIT:
        {
        	rk29_mux_api_set(GPIO1D1_SDMMC0CMD_NAME, GPIO1H_SDMMC0_CMD);
        	rk29_mux_api_set(GPIO1D0_SDMMC0CLKOUT_NAME, GPIO1H_SDMMC0_CLKOUT);
        	rk29_mux_api_set(GPIO1D2_SDMMC0DATA0_NAME, GPIO1H_SDMMC0_DATA0);

        	rk29_mux_api_set(GPIO1D3_SDMMC0DATA1_NAME, GPIO1H_GPIO1D3);
        	gpio_request(RK29_PIN1_PD3, "mmc0-data1");
        	gpio_direction_output(RK29_PIN1_PD3,GPIO_HIGH);

        	rk29_mux_api_set(GPIO1D4_SDMMC0DATA2_NAME, GPIO1H_GPIO1D4);
        	gpio_request(RK29_PIN1_PD4, "mmc0-data2");
        	gpio_direction_output(RK29_PIN1_PD4,GPIO_HIGH);
        	
            rk29_mux_api_set(GPIO1D5_SDMMC0DATA3_NAME, GPIO1H_GPIO1D5);
            gpio_request(RK29_PIN1_PD5, "mmc0-data3");
        	gpio_direction_output(RK29_PIN1_PD5,GPIO_HIGH);
    	}
    	break;
	}
}

static void rk29_sdmmc_set_iomux_mmc1(unsigned int bus_width)
{
#if 0
    switch (bus_width)
    {
        
    	case 1://SDMMC_CTYPE_4BIT:
    	{
            rk29_mux_api_set(GPIO1C2_SDMMC1CMD_NAME, GPIO1H_SDMMC1_CMD);
            rk29_mux_api_set(GPIO1C7_SDMMC1CLKOUT_NAME, GPIO1H_SDMMC1_CLKOUT);
            rk29_mux_api_set(GPIO1C3_SDMMC1DATA0_NAME, GPIO1H_SDMMC1_DATA0);
            rk29_mux_api_set(GPIO1C4_SDMMC1DATA1_NAME, GPIO1H_SDMMC1_DATA1);
            rk29_mux_api_set(GPIO1C5_SDMMC1DATA2_NAME, GPIO1H_SDMMC1_DATA2);
            rk29_mux_api_set(GPIO1C6_SDMMC1DATA3_NAME, GPIO1H_SDMMC1_DATA3);
    	}
    	break;

    	case 0x10000://SDMMC_CTYPE_8BIT:
    	    break;
    	case 0xFFFF:
    	{
    	   rk29_sdmmc_gpio_open(1, 0); 
    	   rk29_sdmmc_gpio_open(1, 1);
    	}
    	break;

    	default: //case 0://SDMMC_CTYPE_1BIT:
        {
            rk29_mux_api_set(GPIO1C2_SDMMC1CMD_NAME, GPIO1H_SDMMC1_CMD);
        	rk29_mux_api_set(GPIO1C7_SDMMC1CLKOUT_NAME, GPIO1H_SDMMC1_CLKOUT);
        	rk29_mux_api_set(GPIO1C3_SDMMC1DATA0_NAME, GPIO1H_SDMMC1_DATA0);

            rk29_mux_api_set(GPIO1C4_SDMMC1DATA1_NAME, GPIO1H_GPIO1C4);
            gpio_request(RK29_PIN1_PC4, "mmc1-data1");
        	gpio_direction_output(RK29_PIN1_PC4,GPIO_HIGH);
        	
            rk29_mux_api_set(GPIO1C5_SDMMC1DATA2_NAME, GPIO1H_GPIO1C5);
            gpio_request(RK29_PIN1_PC5, "mmc1-data2");
        	gpio_direction_output(RK29_PIN1_PC5,GPIO_HIGH);

            rk29_mux_api_set(GPIO1C6_SDMMC1DATA3_NAME, GPIO1H_GPIO1C6);
            gpio_request(RK29_PIN1_PC6, "mmc1-data3");
        	gpio_direction_output(RK29_PIN1_PC6,GPIO_HIGH);

    	}
    	break;
	}
#else
    rk29_mux_api_set(GPIO1C2_SDMMC1CMD_NAME, GPIO1H_SDMMC1_CMD);
    rk29_mux_api_set(GPIO1C7_SDMMC1CLKOUT_NAME, GPIO1H_SDMMC1_CLKOUT);
    rk29_mux_api_set(GPIO1C3_SDMMC1DATA0_NAME, GPIO1H_SDMMC1_DATA0);
    rk29_mux_api_set(GPIO1C4_SDMMC1DATA1_NAME, GPIO1H_SDMMC1_DATA1);
    rk29_mux_api_set(GPIO1C5_SDMMC1DATA2_NAME, GPIO1H_SDMMC1_DATA2);
    rk29_mux_api_set(GPIO1C6_SDMMC1DATA3_NAME, GPIO1H_SDMMC1_DATA3);

#endif
}

static void rk29_sdmmc_set_iomux_mmc2(unsigned int bus_width)
{
    ;//
}


static void rk29_sdmmc_set_iomux(int device_id, unsigned int bus_width)
{
    switch(device_id)
    {
        case 0:
            #ifdef CONFIG_SDMMC0_RK29
            rk29_sdmmc_set_iomux_mmc0(bus_width);
            #endif
            break;
        case 1:
            #ifdef CONFIG_SDMMC1_RK29
            rk29_sdmmc_set_iomux_mmc1(bus_width);
            #endif
            break;
        case 2:
            rk29_sdmmc_set_iomux_mmc2(bus_width);
            break;
        default:
            break;
    }    
}

#endif

#ifdef CONFIG_WIFI_CONTROL_FUNC
static int rk29sdk_wifi_status(struct device *dev);
static int rk29sdk_wifi_status_register(void (*callback)(int card_presend, void *dev_id), void *dev_id);
#endif

#ifdef CONFIG_SDMMC0_RK29
static int rk29_sdmmc0_cfg_gpio(void)
{
#ifdef CONFIG_SDMMC_RK29_OLD	
    rk29_mux_api_set(GPIO1D1_SDMMC0CMD_NAME, GPIO1H_SDMMC0_CMD);
	rk29_mux_api_set(GPIO1D0_SDMMC0CLKOUT_NAME, GPIO1H_SDMMC0_CLKOUT);
	rk29_mux_api_set(GPIO1D2_SDMMC0DATA0_NAME, GPIO1H_SDMMC0_DATA0);
	rk29_mux_api_set(GPIO1D3_SDMMC0DATA1_NAME, GPIO1H_SDMMC0_DATA1);
	rk29_mux_api_set(GPIO1D4_SDMMC0DATA2_NAME, GPIO1H_SDMMC0_DATA2);
	rk29_mux_api_set(GPIO1D5_SDMMC0DATA3_NAME, GPIO1H_SDMMC0_DATA3);
	
	rk29_mux_api_set(GPIO2A2_SDMMC0DETECTN_NAME, GPIO2L_GPIO2A2);

    rk29_mux_api_set(GPIO5D5_SDMMC0PWREN_NAME, GPIO5H_GPIO5D5);   ///GPIO5H_SDMMC0_PWR_EN);  ///GPIO5H_GPIO5D5);
	gpio_request(RK29_PIN5_PD5,"sdmmc");
#if 0
	gpio_set_value(RK29_PIN5_PD5,GPIO_HIGH);
	mdelay(100);
	gpio_set_value(RK29_PIN5_PD5,GPIO_LOW);
#else
	gpio_direction_output(RK29_PIN5_PD5,GPIO_LOW);
#endif

#else
    rk29_sdmmc_set_iomux(0, 0xFFFF);
    
	rk29_mux_api_set(GPIO2A2_SDMMC0DETECTN_NAME, GPIO2L_SDMMC0_DETECT_N);//Modifyed by xbw.

	#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
    gpio_request(SDMMC0_WRITE_PROTECT_PIN,"sdmmc-wp");
    gpio_direction_input(SDMMC0_WRITE_PROTECT_PIN);	    
    #endif

#endif

	return 0;
}

#define CONFIG_SDMMC0_USE_DMA
struct rk29_sdmmc_platform_data default_sdmmc0_data = {
	.host_ocr_avail = (MMC_VDD_25_26|MMC_VDD_26_27|MMC_VDD_27_28|MMC_VDD_28_29|MMC_VDD_29_30|
					   MMC_VDD_30_31|MMC_VDD_31_32|MMC_VDD_32_33|
					   MMC_VDD_33_34|MMC_VDD_34_35| MMC_VDD_35_36),
	.host_caps 	= (MMC_CAP_4_BIT_DATA|MMC_CAP_MMC_HIGHSPEED|MMC_CAP_SD_HIGHSPEED),
	.io_init = rk29_sdmmc0_cfg_gpio,
	
#if !defined(CONFIG_SDMMC_RK29_OLD)		
	.set_iomux = rk29_sdmmc_set_iomux,
#endif

	.dma_name = "sd_mmc",
#ifdef CONFIG_SDMMC0_USE_DMA
	.use_dma  = 1,
#else
	.use_dma = 0,
#endif
	.detect_irq = RK29_PIN2_PA2, // INVALID_GPIO
	.enable_sd_wakeup = 0,

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
    .write_prt = SDMMC0_WRITE_PROTECT_PIN,
#else
    .write_prt = INVALID_GPIO,
#endif
};
#endif
#ifdef CONFIG_SDMMC1_RK29
#define CONFIG_SDMMC1_USE_DMA
static int rk29_sdmmc1_cfg_gpio(void)
{
#if defined(CONFIG_SDMMC_RK29_OLD)
	rk29_mux_api_set(GPIO1C2_SDMMC1CMD_NAME, GPIO1H_SDMMC1_CMD);
	rk29_mux_api_set(GPIO1C7_SDMMC1CLKOUT_NAME, GPIO1H_SDMMC1_CLKOUT);
	rk29_mux_api_set(GPIO1C3_SDMMC1DATA0_NAME, GPIO1H_SDMMC1_DATA0);
	rk29_mux_api_set(GPIO1C4_SDMMC1DATA1_NAME, GPIO1H_SDMMC1_DATA1);
	rk29_mux_api_set(GPIO1C5_SDMMC1DATA2_NAME, GPIO1H_SDMMC1_DATA2);
	rk29_mux_api_set(GPIO1C6_SDMMC1DATA3_NAME, GPIO1H_SDMMC1_DATA3);
	//rk29_mux_api_set(GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME, GPIO1H_SDMMC1_DETECT_N);

#else

#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
    gpio_request(SDMMC1_WRITE_PROTECT_PIN,"sdio-wp");
    gpio_direction_input(SDMMC1_WRITE_PROTECT_PIN);	    
#endif

#endif

	return 0;
}



#define RK29SDK_WIFI_SDIO_CARD_DETECT_N    RK29_PIN1_PD6

struct rk29_sdmmc_platform_data default_sdmmc1_data = {
	.host_ocr_avail = (MMC_VDD_25_26|MMC_VDD_26_27|MMC_VDD_27_28|MMC_VDD_28_29|
					   MMC_VDD_29_30|MMC_VDD_30_31|MMC_VDD_31_32|
					   MMC_VDD_32_33|MMC_VDD_33_34),

#if !defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)					   
	.host_caps 	= (MMC_CAP_4_BIT_DATA|MMC_CAP_SDIO_IRQ|
				   MMC_CAP_MMC_HIGHSPEED|MMC_CAP_SD_HIGHSPEED),
#else
    .host_caps 	= (MMC_CAP_4_BIT_DATA|MMC_CAP_MMC_HIGHSPEED|MMC_CAP_SD_HIGHSPEED),
#endif

	.io_init = rk29_sdmmc1_cfg_gpio,
	
#if !defined(CONFIG_SDMMC_RK29_OLD)		
	.set_iomux = rk29_sdmmc_set_iomux,
#endif	

	.dma_name = "sdio",
#ifdef CONFIG_SDMMC1_USE_DMA
	.use_dma  = 1,
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
//for wifi develop board
    .detect_irq = INVALID_GPIO,
    .enable_sd_wakeup = 0,
#endif

};
#endif ////endif--#ifdef CONFIG_SDMMC1_RK29


int rk29sdk_wifi_power_state = 0;
int rk29sdk_bt_power_state = 0;

#ifdef CONFIG_WIFI_CONTROL_FUNC
#define RK29SDK_WIFI_BT_GPIO_POWER_N       RK29_PIN5_PD6
#define RK29SDK_WIFI_GPIO_RESET_N          RK29_PIN6_PC0
#define RK29SDK_BT_GPIO_RESET_N            RK29_PIN6_PC4

static int rk29sdk_wifi_cd = 0;   /* wifi virtual 'card detect' status */
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

static int rk29sdk_wifi_status(struct device *dev)
{
        return rk29sdk_wifi_cd;
}

static int rk29sdk_wifi_status_register(void (*callback)(int card_present, void *dev_id), void *dev_id)
{
        if(wifi_status_cb)
                return -EAGAIN;
        wifi_status_cb = callback;
        wifi_status_cb_devid = dev_id;
        return 0;
}

static int rk29sdk_wifi_bt_gpio_control_init(void)
{
    if (gpio_request(RK29SDK_WIFI_BT_GPIO_POWER_N, "wifi_bt_power")) {
           pr_info("%s: request wifi_bt power gpio failed\n", __func__);
           return -1;
    }

    if (gpio_request(RK29SDK_WIFI_GPIO_RESET_N, "wifi reset")) {
           pr_info("%s: request wifi reset gpio failed\n", __func__);
           gpio_free(RK29SDK_WIFI_BT_GPIO_POWER_N);
           return -1;
    }

    if (gpio_request(RK29SDK_BT_GPIO_RESET_N, "bt reset")) {
          pr_info("%s: request bt reset gpio failed\n", __func__);
          gpio_free(RK29SDK_WIFI_GPIO_RESET_N);
          return -1;
    }

    gpio_direction_output(RK29SDK_WIFI_BT_GPIO_POWER_N, GPIO_LOW);
    gpio_direction_output(RK29SDK_WIFI_GPIO_RESET_N,    GPIO_LOW);
    gpio_direction_output(RK29SDK_BT_GPIO_RESET_N,      GPIO_LOW);

    #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)
    
    rk29_mux_api_set(GPIO1C4_SDMMC1DATA1_NAME, GPIO1H_GPIO1C4);
    gpio_request(RK29_PIN1_PC4, "mmc1-data1");
    gpio_direction_output(RK29_PIN1_PC4,GPIO_LOW);//set mmc1-data1 to low.

    rk29_mux_api_set(GPIO1C5_SDMMC1DATA2_NAME, GPIO1H_GPIO1C5);
    gpio_request(RK29_PIN1_PC5, "mmc1-data2");
    gpio_direction_output(RK29_PIN1_PC5,GPIO_LOW);//set mmc1-data2 to low.

    rk29_mux_api_set(GPIO1C6_SDMMC1DATA3_NAME, GPIO1H_GPIO1C6);
    gpio_request(RK29_PIN1_PC6, "mmc1-data3");
    gpio_direction_output(RK29_PIN1_PC6,GPIO_LOW);//set mmc1-data3 to low.
    
    rk29_sdmmc_gpio_open(1, 0); //added by xbw at 2011-10-13
    #endif    
    pr_info("%s: init finished\n",__func__);

    return 0;
}

static int rk29sdk_wifi_power(int on)
{
        pr_info("%s: %d\n", __func__, on);
        if (on){
                gpio_set_value(RK29SDK_WIFI_BT_GPIO_POWER_N, GPIO_HIGH);

                #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)	
                rk29_sdmmc_gpio_open(1, 1); //added by xbw at 2011-10-13
                #endif

                gpio_set_value(RK29SDK_WIFI_GPIO_RESET_N, GPIO_HIGH);
                mdelay(100);
                pr_info("wifi turn on power\n");
        }else{
                if (!rk29sdk_bt_power_state){
                        gpio_set_value(RK29SDK_WIFI_BT_GPIO_POWER_N, GPIO_LOW);

                        #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)	
                        rk29_sdmmc_gpio_open(1, 0); //added by xbw at 2011-10-13
                        #endif
                        
                        mdelay(100);
                        pr_info("wifi shut off power\n");
                }else
                {
                        pr_info("wifi shouldn't shut off power, bt is using it!\n");
                }
                gpio_set_value(RK29SDK_WIFI_GPIO_RESET_N, GPIO_LOW);

        }

        rk29sdk_wifi_power_state = on;
        return 0;
}

static int rk29sdk_wifi_reset_state;
static int rk29sdk_wifi_reset(int on)
{
        pr_info("%s: %d\n", __func__, on);
        gpio_set_value(RK29SDK_WIFI_GPIO_RESET_N, on);
        mdelay(100);
        rk29sdk_wifi_reset_state = on;
        return 0;
}

int rk29sdk_wifi_set_carddetect(int val)
{
        pr_info("%s:%d\n", __func__, val);
        rk29sdk_wifi_cd = val;
        if (wifi_status_cb){
                wifi_status_cb(val, wifi_status_cb_devid);
        }else {
                pr_warning("%s, nobody to notify\n", __func__);
        }
        return 0;
}
EXPORT_SYMBOL(rk29sdk_wifi_set_carddetect);

static struct wifi_mem_prealloc wifi_mem_array[PREALLOC_WLAN_SEC_NUM] = {
        {NULL, (WLAN_SECTION_SIZE_0 + PREALLOC_WLAN_SECTION_HEADER)},
        {NULL, (WLAN_SECTION_SIZE_1 + PREALLOC_WLAN_SECTION_HEADER)},
        {NULL, (WLAN_SECTION_SIZE_2 + PREALLOC_WLAN_SECTION_HEADER)},
        {NULL, (WLAN_SECTION_SIZE_3 + PREALLOC_WLAN_SECTION_HEADER)}
};

static void *rk29sdk_mem_prealloc(int section, unsigned long size)
{
        if (section == PREALLOC_WLAN_SEC_NUM)
                return wlan_static_skb;

        if ((section < 0) || (section > PREALLOC_WLAN_SEC_NUM))
                return NULL;

        if (wifi_mem_array[section].size < size)
                return NULL;

        return wifi_mem_array[section].mem_ptr;
}

int __init rk29sdk_init_wifi_mem(void)
{
        int i;
        int j;

        for (i = 0 ; i < WLAN_SKB_BUF_NUM ; i++) {
                wlan_static_skb[i] = dev_alloc_skb(
                                ((i < (WLAN_SKB_BUF_NUM / 2)) ? 4096 : 8192));

                if (!wlan_static_skb[i])
                        goto err_skb_alloc;
        }

        for (i = 0 ; i < PREALLOC_WLAN_SEC_NUM ; i++) {
                wifi_mem_array[i].mem_ptr =
                                kmalloc(wifi_mem_array[i].size, GFP_KERNEL);

                if (!wifi_mem_array[i].mem_ptr)
                        goto err_mem_alloc;
        }
        return 0;

err_mem_alloc:
        pr_err("Failed to mem_alloc for WLAN\n");
        for (j = 0 ; j < i ; j++)
               kfree(wifi_mem_array[j].mem_ptr);

        i = WLAN_SKB_BUF_NUM;

err_skb_alloc:
        pr_err("Failed to skb_alloc for WLAN\n");
        for (j = 0 ; j < i ; j++)
                dev_kfree_skb(wlan_static_skb[j]);

        return -ENOMEM;
}

static struct wifi_platform_data rk29sdk_wifi_control = {
        .set_power = rk29sdk_wifi_power,
        .set_reset = rk29sdk_wifi_reset,
        .set_carddetect = rk29sdk_wifi_set_carddetect,
        .mem_prealloc   = rk29sdk_mem_prealloc,
};
static struct platform_device rk29sdk_wifi_device = {
        .name = "bcm4329_wlan",
        .id = 1,
        .dev = {
                .platform_data = &rk29sdk_wifi_control,
         },
};
#endif


/* bluetooth rfkill device */
static struct platform_device rk29sdk_rfkill = {
        .name = "rk29sdk_rfkill",
        .id = -1,
};


//#ifdef CONFIG_VIVANTE
#define GPU_HIGH_CLOCK        552
#define GPU_LOW_CLOCK         (periph_pll_default / 1000000) /* same as general pll clock rate below */
static struct resource resources_gpu[] = {
    [0] = {
		.name 	= "gpu_irq",
        .start 	= IRQ_GPU,
        .end    = IRQ_GPU,
        .flags  = IORESOURCE_IRQ,
    },
    [1] = {
		.name   = "gpu_base",
        .start  = RK29_GPU_PHYS,
        .end    = RK29_GPU_PHYS + RK29_GPU_SIZE - 1,
        .flags  = IORESOURCE_MEM,
    },
    [2] = {
		.name   = "gpu_mem",
        .start  = PMEM_GPU_BASE,
        .end    = PMEM_GPU_BASE + PMEM_GPU_SIZE - 1,
        .flags  = IORESOURCE_MEM,
    },
    [3] = {
		.name 	= "gpu_clk",
        .start 	= GPU_LOW_CLOCK,
        .end    = GPU_HIGH_CLOCK,
        .flags  = IORESOURCE_IO,
    },
};
static struct platform_device rk29_device_gpu = {
    .name             = "galcore",
    .id               = 0,
    .num_resources    = ARRAY_SIZE(resources_gpu),
    .resource         = resources_gpu,
};
//#endif

#ifdef CONFIG_KEYS_RK29
extern struct rk29_keys_platform_data rk29_keys_pdata;
static struct platform_device rk29_device_keys = {
	.name		= "rk29-keypad",
	.id		= -1,
	.dev		= {
		.platform_data	= &rk29_keys_pdata,
	},
};
#endif

#ifdef CONFIG_LEDS_GPIO_PLATFORM
struct gpio_led rk29_leds[] = {
		{
			.name = "rk29_red_led",
			.gpio = RK29_PIN4_PB2,
			.default_trigger = "timer",
			.active_low = 0,
			.retain_state_suspended = 1,
			.default_state = LEDS_GPIO_DEFSTATE_OFF,
		},
		{
			.name = "rk29_green_led",
			.gpio = RK29_PIN4_PB1,
			.default_trigger = "timer",
			.active_low = 0,
			.retain_state_suspended = 1,
			.default_state = LEDS_GPIO_DEFSTATE_OFF,
		},
		{
			.name = "rk29_blue_led",
			.gpio = RK29_PIN4_PB0,
			.default_trigger = "timer",
			.active_low = 0,
			.retain_state_suspended = 1,
			.default_state = LEDS_GPIO_DEFSTATE_OFF,
		},
};

struct gpio_led_platform_data rk29_leds_pdata = {
	.leds = &rk29_leds,
	.num_leds	= ARRAY_SIZE(rk29_leds),
};

struct platform_device rk29_device_gpio_leds = {
	.name	= "leds-gpio",
	.id 	= -1,
	.dev	= {
	   .platform_data  = &rk29_leds_pdata,
	},
};
#endif

#ifdef CONFIG_LEDS_NEWTON_PWM
static struct led_newton_pwm rk29_pwm_leds[] = {
		{
			.name = "power_led",
			.pwm_id = 1,
			.pwm_gpio = RK29_PIN5_PD2,
			.pwm_iomux_name = GPIO5D2_PWM1_UART1SIRIN_NAME,
			.pwm_iomux_pwm = GPIO5H_PWM1,
			.pwm_iomux_gpio = GPIO5H_GPIO5D2,
			.freq = 1000,
			.period = 255,
		},
};

static struct led_newton_pwm_platform_data rk29_pwm_leds_pdata = {
	.leds = &rk29_pwm_leds,
	.num_leds	= ARRAY_SIZE(rk29_pwm_leds),
};

static struct platform_device rk29_device_pwm_leds = {
	.name	= "leds_newton_pwm",
	.id 	= -1,
	.dev	= {
	   .platform_data  = &rk29_pwm_leds_pdata,
	},
};

#endif
static void __init rk29_board_iomux_init(void)
{
	#ifdef CONFIG_RK29_PWM_REGULATOR
	rk29_mux_api_set(REGULATOR_PWM_MUX_NAME,REGULATOR_PWM_MUX_MODE);
	#endif
}

static struct platform_device *devices[] __initdata = {

#ifdef CONFIG_RK29_WATCHDOG
	&rk29_device_wdt,
#endif

#ifdef CONFIG_UART1_RK29
	&rk29_device_uart1,
#endif
#ifdef CONFIG_UART0_RK29
	&rk29_device_uart0,
#endif
#ifdef CONFIG_UART2_RK29
	&rk29_device_uart2,
#endif
#ifdef CONFIG_UART3_RK29
	&rk29_device_uart3,
#endif

#ifdef CONFIG_RK29_PWM_REGULATOR
	&rk29_device_pwm_regulator,
#endif
#ifdef CONFIG_SPIM0_RK29
    &rk29xx_device_spi0m,
#endif
#ifdef CONFIG_SPIM1_RK29
    &rk29xx_device_spi1m,
#endif
#ifdef CONFIG_ADC_RK29
	&rk29_device_adc,
#endif
#ifdef CONFIG_BATTERY_RK29_ADC
    &rk29_adc_device_battery,
#endif
#ifdef CONFIG_I2C0_RK29
	&rk29_device_i2c0,
#endif
#ifdef CONFIG_I2C1_RK29
	&rk29_device_i2c1,
#endif
#ifdef CONFIG_I2C2_RK29
	&rk29_device_i2c2,
#endif
#ifdef CONFIG_I2C3_RK29
	&rk29_device_i2c3,
#endif

#ifdef CONFIG_SND_RK29_SOC_I2S_2CH
        &rk29_device_iis_2ch,
#endif
#ifdef CONFIG_SND_RK29_SOC_I2S_8CH
        &rk29_device_iis_8ch,
#endif

#ifdef CONFIG_KEYS_RK29
	&rk29_device_keys,
#endif
#ifdef CONFIG_KEYS_RK29_NEWTON
	&rk29_device_keys,
#endif
#ifdef CONFIG_SDMMC0_RK29
	&rk29_device_sdmmc0,
#endif
#ifdef CONFIG_SDMMC1_RK29
	&rk29_device_sdmmc1,
#endif

#ifdef CONFIG_MTD_NAND_RK29XX
	&rk29xx_device_nand,
#endif

#ifdef CONFIG_WIFI_CONTROL_FUNC
        &rk29sdk_wifi_device,
#endif

#ifdef CONFIG_BT
        &rk29sdk_rfkill,
#endif

#ifdef CONFIG_MTD_NAND_RK29
	&rk29_device_nand,
#endif

#ifdef CONFIG_FB_RK29
	&rk29_device_fb,
	&rk29_device_dma_cpy,
#endif
#ifdef CONFIG_BACKLIGHT_RK29_BL
	&rk29_device_backlight,
#endif
#ifdef CONFIG_BACKLIGHT_RK29_NEWTON_BL
	&rk29_device_backlight,
#endif
#ifdef CONFIG_RK29_VMAC
	&rk29_device_vmac,
#endif
//#ifdef CONFIG_VIVANTE
	&rk29_device_gpu,
//#endif
#ifdef CONFIG_VIDEO_RK29
 	&rk29_device_camera,      /* ddl@rock-chips.com : camera support  */
 	#if (CONFIG_SENSOR_IIC_ADDR_0 != 0x00)
 	&rk29_soc_camera_pdrv_0,
 	#endif
    #if (CONFIG_SENSOR_IIC_ADDR_1 != 0x00)
 	&rk29_soc_camera_pdrv_1,
 	#endif   

    #if (PMEM_CAM_SIZE > 0)
 	&android_pmem_cam_device,
 	#endif
#endif
#if PMEM_SKYPE_SIZE > 0
	&android_pmem_skype_device,
#endif
#ifdef CONFIG_ION
	&rk29_ion_device,
#endif
	&android_pmem_device,
	&rk29_vpu_mem_device,
#ifdef CONFIG_USB20_OTG
	&rk29_device_usb20_otg,
#endif
#ifdef CONFIG_USB20_HOST
	&rk29_device_usb20_host,
#endif
#ifdef CONFIG_USB11_HOST
	&rk29_device_usb11_host,
#endif
#ifdef CONFIG_USB_ANDROID
	&android_usb_device,
	&usb_mass_storage_device,
#endif
#ifdef CONFIG_USB_ANDROID_RNDIS
    &rk29_device_rndis,
#endif
#ifdef CONFIG_RK29_IPP
	&rk29_device_ipp,
#endif
#ifdef CONFIG_VIDEO_RK29XX_VOUT
	&rk29_v4l2_output_devce,
#endif
#ifdef CONFIG_RK29_NEWTON
	&rk29_device_newton,
#endif
#ifdef CONFIG_RK_IRDA
    &irda_device,
#endif
#ifdef CONFIG_LEDS_GPIO_PLATFORM
	&rk29_device_gpio_leds,
#endif
#ifdef CONFIG_LEDS_NEWTON_PWM
	&rk29_device_pwm_leds,
#endif
#ifdef CONFIG_SND_RK29_SOC_CS42L52
	&rk29_cs42l52_device,
#endif
};

/*****************************************************************************************
 * spi devices
 * author: cmc@rock-chips.com
 *****************************************************************************************/
static int rk29_vmac_register_set(void)
{
	//config rk29 vmac as rmii, 100MHz
	u32 value= readl(RK29_GRF_BASE + 0xbc);
	value = (value & 0xfff7ff) | (0x400);
	writel(value, RK29_GRF_BASE + 0xbc);
	return 0;
}

static int rk29_rmii_io_init(void)
{
	int err;

	//phy power gpio
	err = gpio_request(RK29_PIN6_PB0, "phy_power_en");
	if (err) {
		gpio_free(RK29_PIN6_PB0);
		printk("-------request RK29_PIN6_PB0 fail--------\n");
		return -1;
	}
	//phy power down
	gpio_direction_output(RK29_PIN6_PB0, GPIO_LOW);
	gpio_set_value(RK29_PIN6_PB0, GPIO_LOW);

	return 0;
}

static int rk29_rmii_io_deinit(void)
{
	//phy power down
	gpio_direction_output(RK29_PIN6_PB0, GPIO_LOW);
	gpio_set_value(RK29_PIN6_PB0, GPIO_LOW);
	//free
	gpio_free(RK29_PIN6_PB0);
	return 0;
}

static int rk29_rmii_power_control(int enable)
{
	if (enable) {
		//enable phy power
		gpio_direction_output(RK29_PIN6_PB0, GPIO_HIGH);
		gpio_set_value(RK29_PIN6_PB0, GPIO_HIGH);
	}
	else {
		gpio_direction_output(RK29_PIN6_PB0, GPIO_LOW);
		gpio_set_value(RK29_PIN6_PB0, GPIO_LOW);
	}
	return 0;
}

struct rk29_vmac_platform_data rk29_vmac_pdata = {
	.vmac_register_set = rk29_vmac_register_set,
	.rmii_io_init = rk29_rmii_io_init,
	.rmii_io_deinit = rk29_rmii_io_deinit,
	.rmii_power_control = rk29_rmii_power_control,
};

/*****************************************************************************************
 * spi devices
 * author: cmc@rock-chips.com
 *****************************************************************************************/
#define SPI_CHIPSELECT_NUM 2
static struct spi_cs_gpio rk29xx_spi0_cs_gpios[SPI_CHIPSELECT_NUM] = {
    {
		.name = "spi0 cs0",
		.cs_gpio = RK29_PIN2_PC1,
		.cs_iomux_name = GPIO2C1_SPI0CSN0_NAME,
		.cs_iomux_mode = GPIO2H_SPI0_CSN0,
	},
	{
		.name = "spi0 cs1",
		.cs_gpio = RK29_PIN1_PA4,
		.cs_iomux_name = GPIO1A4_EMMCWRITEPRT_SPI0CS1_NAME,//if no iomux,set it NULL
		.cs_iomux_mode = GPIO1L_SPI0_CSN1,
	}
};

static struct spi_cs_gpio rk29xx_spi1_cs_gpios[SPI_CHIPSELECT_NUM] = {
    {
		.name = "spi1 cs0",
		.cs_gpio = RK29_PIN2_PC5,
		.cs_iomux_name = GPIO2C5_SPI1CSN0_NAME,
		.cs_iomux_mode = GPIO2H_SPI1_CSN0,
	},
	{
		.name = "spi1 cs1",
		.cs_gpio = RK29_PIN1_PA3,
		.cs_iomux_name = GPIO1A3_EMMCDETECTN_SPI1CS1_NAME,//if no iomux,set it NULL
		.cs_iomux_mode = GPIO1L_SPI1_CSN1,
	}
};

static int spi_io_init(struct spi_cs_gpio *cs_gpios, int cs_num)
{
#if 1
	int i;
	if (cs_gpios) {
		for (i=0; i<cs_num; i++) {
			rk29_mux_api_set(cs_gpios[i].cs_iomux_name, cs_gpios[i].cs_iomux_mode);
		}
	}
#endif
	return 0;
}

static int spi_io_deinit(struct spi_cs_gpio *cs_gpios, int cs_num)
{
	return 0;
}

static int spi_io_fix_leakage_bug(void)
{
#if 0
	gpio_direction_output(RK29_PIN2_PC1, GPIO_LOW);
#endif
	return 0;
}

static int spi_io_resume_leakage_bug(void)
{
#if 0
	gpio_direction_output(RK29_PIN2_PC1, GPIO_HIGH);
#endif
	return 0;
}

struct rk29xx_spi_platform_data rk29xx_spi0_platdata = {
	.num_chipselect = SPI_CHIPSELECT_NUM,
	.chipselect_gpios = rk29xx_spi0_cs_gpios,
	.io_init = spi_io_init,
	.io_deinit = spi_io_deinit,
	.io_fix_leakage_bug = spi_io_fix_leakage_bug,
	.io_resume_leakage_bug = spi_io_resume_leakage_bug,
};

struct rk29xx_spi_platform_data rk29xx_spi1_platdata = {
	.num_chipselect = SPI_CHIPSELECT_NUM,
	.chipselect_gpios = rk29xx_spi1_cs_gpios,
	.io_init = spi_io_init,
	.io_deinit = spi_io_deinit,
	.io_fix_leakage_bug = spi_io_fix_leakage_bug,
	.io_resume_leakage_bug = spi_io_resume_leakage_bug,
};

/*****************************************************************************************
 * xpt2046 touch panel
 * author: cmc@rock-chips.com
 *****************************************************************************************/
#define XPT2046_GPIO_INT           RK29_PIN0_PA3
#define DEBOUNCE_REPTIME  3

#if defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_SPI)
static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 	= 1,
	.swap_xy		= 0,
	.x_min			= 0,
	.x_max			= 320,
	.y_min			= 0,
	.y_max			= 480,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,
	.penirq_recheck_delay_usecs = 1,
};
#elif defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_CBN_SPI)
static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 	= 1,
	.swap_xy		= 0,
	.x_min			= 0,
	.x_max			= 320,
	.y_min			= 0,
	.y_max			= 480,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,
	.penirq_recheck_delay_usecs = 1,
};
#elif defined(CONFIG_TOUCHSCREEN_XPT2046_SPI)
static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 	= 1,
	.swap_xy		= 1,
	.x_min			= 0,
	.x_max			= 800,
	.y_min			= 0,
	.y_max			= 480,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,

	.penirq_recheck_delay_usecs = 1,
};
#elif defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI)
static struct xpt2046_platform_data xpt2046_info = {
	.model			= 2046,
	.keep_vref_on 	= 1,
	.swap_xy		= 1,
	.x_min			= 0,
	.x_max			= 800,
	.y_min			= 0,
	.y_max			= 480,
	.debounce_max		= 7,
	.debounce_rep		= DEBOUNCE_REPTIME,
	.debounce_tol		= 20,
	.gpio_pendown		= XPT2046_GPIO_INT,

	.penirq_recheck_delay_usecs = 1,
};
#endif

static struct spi_board_info board_spi_devices[] = {
#if defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_SPI) || defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_CBN_SPI)\
    ||defined(CONFIG_TOUCHSCREEN_XPT2046_SPI) || defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI)
	{
		.modalias	= "xpt2046_ts",
		.chip_select	= 0,
		.max_speed_hz	= 125 * 1000 * 26,/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.irq = XPT2046_GPIO_INT,
		.platform_data = &xpt2046_info,
	},
#endif
};


static void __init rk29_gic_init_irq(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
	gic_init(0, 32, (void __iomem *)RK29_GICPERI_BASE, (void __iomem *)RK29_GICCPU_BASE);
#else
	gic_dist_init(0, (void __iomem *)RK29_GICPERI_BASE, 32);
	gic_cpu_init(0, (void __iomem *)RK29_GICCPU_BASE);
#endif
}

static void __init machine_rk29_init_irq(void)
{
	rk29_gic_init_irq();
	rk29_gpio_init();
}

static struct cpufreq_frequency_table freq_table[] = {
	{ .index = 1200000, .frequency =  408000 },
	{ .index = 1250000, .frequency =  816000 },
	{ .index = 1350000, .frequency = 1008000 },
	{ .frequency = CPUFREQ_TABLE_END },
};

static void __init machine_rk29_board_init(void)
{
	rk29_board_iomux_init();

	board_power_init();
	board_update_cpufreq_table(freq_table);

		platform_add_devices(devices, ARRAY_SIZE(devices));
#ifdef CONFIG_I2C0_RK29
	i2c_register_board_info(default_i2c0_data.bus_num, board_i2c0_devices,
			ARRAY_SIZE(board_i2c0_devices));
#endif
#ifdef CONFIG_I2C1_RK29
	i2c_register_board_info(default_i2c1_data.bus_num, board_i2c1_devices,
			ARRAY_SIZE(board_i2c1_devices));
#endif
#ifdef CONFIG_I2C2_RK29
	i2c_register_board_info(default_i2c2_data.bus_num, board_i2c2_devices,
			ARRAY_SIZE(board_i2c2_devices));
#endif
#ifdef CONFIG_I2C3_RK29
	i2c_register_board_info(default_i2c3_data.bus_num, board_i2c3_devices,
			ARRAY_SIZE(board_i2c3_devices));
#endif

	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
        
#ifdef CONFIG_WIFI_CONTROL_FUNC
	rk29sdk_wifi_bt_gpio_control_init();
	rk29sdk_init_wifi_mem();
#endif

	board_usb_detect_init(RK29_PIN0_PA0);
#if defined(CONFIG_RK_IRDA) || defined(CONFIG_BU92747GUW_CIR)
	smc0_init(NULL);
	bu92747guw_io_init();
#endif

}

static void __init machine_rk29_fixup(struct machine_desc *desc, struct tag *tags,
					char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = RK29_SDRAM_PHYS;
	mi->bank[0].size = LINUX_SIZE;
#if SDRAM_SIZE > SZ_512M
	mi->nr_banks = 2;
	mi->bank[1].start = RK29_SDRAM_PHYS + SZ_512M;
	mi->bank[1].size = SDRAM_SIZE - SZ_512M;
#endif
}

static void __init machine_rk29_mapio(void)
{
	rk29_map_common_io();
	rk29_setup_early_printk();
	rk29_sram_init();
	rk29_clock_init(periph_pll_default);
	rk29_iomux_init();
	ddr_init(DDR_TYPE, DDR_FREQ);
}

MACHINE_START(RK29, "RK29board")
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37))
	/* UART for LL DEBUG */
	.phys_io	= RK29_UART1_PHYS & 0xfff00000,
	.io_pg_offst	= ((RK29_UART1_BASE) >> 18) & 0xfffc,
#endif
	.boot_params	= RK29_SDRAM_PHYS + 0x88000,
	.fixup		= machine_rk29_fixup,
	.map_io		= machine_rk29_mapio,
	.init_irq	= machine_rk29_init_irq,
	.init_machine	= machine_rk29_board_init,
	.timer		= &rk29_timer,
MACHINE_END
