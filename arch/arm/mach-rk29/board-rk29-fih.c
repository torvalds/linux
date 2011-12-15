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
#include <linux/spi/spi.h>
#include <linux/mmc/host.h>
#include <linux/android_pmem.h>
#include <linux/usb/android_composite.h>
#include <linux/i2c/tps65910.h>
#include <linux/mpu.h>
#include <linux/mpu3050.h>
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
#include <mach/rk29_lightsensor.h>
#include <mach/ddr.h>

#include <linux/regulator/rk29-pwm-regulator.h>
#include <linux/regulator/machine.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <linux/atmel_maxtouch.h>
#include <linux/leds-att1272.h>

#include "devices.h"
#include "../../../drivers/input/touchscreen/xpt2046_cbn_ts.h"

/*Below : /sys/class/timed_output */
#include "../../../drivers/staging/android/timed_gpio.h"

/* Set memory size of pmem */
#ifdef CONFIG_RK29_MEM_SIZE_M
#define SDRAM_SIZE          (CONFIG_RK29_MEM_SIZE_M * SZ_1M)
#else
#define SDRAM_SIZE          SZ_512M
#endif
#define PMEM_GPU_SIZE       SZ_64M
#define PMEM_UI_SIZE        SZ_32M
#define PMEM_VPU_SIZE       SZ_64M
#define PMEM_CAM_SIZE       0x01300000
#ifdef CONFIG_VIDEO_RK29_WORK_IPP
#define MEM_CAMIPP_SIZE     SZ_8M
#else
#define MEM_CAMIPP_SIZE     0
#endif
#define MEM_FB_SIZE         (3*SZ_2M)
#ifdef CONFIG_FB_WORK_IPP
#ifdef CONFIG_FB_SCALING_OSD_1080P
#define MEM_FBIPP_SIZE      SZ_16M   //1920 x 1080 x 2 x 2  //RGB565 = x2;RGB888 = x4
#else
#define MEM_FBIPP_SIZE      SZ_8M   //1920 x 1080 x 2 x 2  //RGB565 = x2;RGB888 = x4
#endif
#else
#define MEM_FBIPP_SIZE      0
#endif
#define PMEM_GPU_BASE       ((u32)RK29_SDRAM_PHYS + SDRAM_SIZE - PMEM_GPU_SIZE)
#define PMEM_UI_BASE        (PMEM_GPU_BASE - PMEM_UI_SIZE)
#define PMEM_VPU_BASE       (PMEM_UI_BASE - PMEM_VPU_SIZE)
#define PMEM_CAM_BASE       (PMEM_VPU_BASE - PMEM_CAM_SIZE)
#define MEM_CAMIPP_BASE     (PMEM_CAM_BASE - MEM_CAMIPP_SIZE)
#define MEM_FB_BASE         (MEM_CAMIPP_BASE - MEM_FB_SIZE)
#define MEM_FBIPP_BASE      (MEM_FB_BASE - MEM_FBIPP_SIZE)
#define LINUX_SIZE          (MEM_FBIPP_BASE - RK29_SDRAM_PHYS)

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

/*****************************************************************************************
* touch screen devices
* author: cf@rock-chips.com
*****************************************************************************************/
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
#define FB_DISPLAY_ON_PIN           INVALID_GPIO// RK29_PIN6_PD0
#define FB_LCD_STANDBY_PIN          RK29_PIN6_PD1
#define FB_LCD_CABC_EN_PIN          RK29_PIN6_PD2
#define FB_MCU_FMK_PIN              INVALID_GPIO

#define FB_DISPLAY_ON_VALUE         GPIO_HIGH
#define FB_LCD_STANDBY_VALUE        GPIO_HIGH

//#endif
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
        else
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
        else
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
        gpio_set_value(FB_LCD_CABC_EN_PIN, GPIO_LOW);
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

static struct android_pmem_platform_data android_pmem_pdata = {
	.name		= "pmem",
	.start		= PMEM_UI_BASE,
	.size		= PMEM_UI_SIZE,
	.no_allocator	= 0,
	.cached		= 1,
};

static struct platform_device android_pmem_device = {
	.name		= "android_pmem",
	.id		= 0,
	.dev		= {
		.platform_data = &android_pmem_pdata,
	},
};


static struct android_pmem_platform_data android_pmem_cam_pdata = {
	.name		= "pmem_cam",
	.start		= PMEM_CAM_BASE,
	.size		= PMEM_CAM_SIZE,
	.no_allocator	= 1,
	.cached		= 1,
};

static struct platform_device android_pmem_cam_device = {
	.name		= "android_pmem",
	.id		= 1,
	.dev		= {
		.platform_data = &android_pmem_cam_pdata,
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

static struct platform_device rk29_v4l2_output_devce = {
	.name		= "rk29_vout",
};

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

};
#endif

/*Atmel mxt224 touch*/
#if defined (CONFIG_ATMEL_MXT224)
#define TOUCH_RESET_PIN RK29_PIN6_PC3
#define TOUCH_INT_PIN   RK29_PIN0_PA2

int mxt224_init_platform_hw(void)
{
    if(gpio_request(TOUCH_RESET_PIN, "Touch_reset") != 0){
      gpio_free(TOUCH_RESET_PIN);
      printk("mxt224_init_platform_hw gpio_request error\n");
      return -EIO;
    }

    if(gpio_request(TOUCH_INT_PIN, "Touch_int") != 0){
      gpio_free(TOUCH_INT_PIN);
      printk("mxt224_init_platform_hw gpio_request error\n");
      return -EIO;
    }
    gpio_pull_updown(TOUCH_INT_PIN, 1);
    gpio_direction_output(TOUCH_RESET_PIN, 0);
    gpio_set_value(TOUCH_RESET_PIN,GPIO_LOW);
    msleep(10);
    gpio_set_value(TOUCH_RESET_PIN,GPIO_HIGH);
    msleep(500);
    return 0;
}

u8 mxt_read_chg(void)
{
	return 1;
}

struct mxt_platform_data mxt224_info = {
    .numtouch = 2,
    .init_platform_hw= mxt224_init_platform_hw,
    .read_chg = mxt_read_chg,
    .max_x = 4095,
    .max_y = 4095,
};
#endif

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
  .swap_xy = 0,
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
 
#define DC_CHECK_PIN RK29_PIN4_PA1
#define LI_LION_BAT_NUM 2
static int bq27510_init_dc_check_pin(void)
{ 
	if(gpio_request(DC_CHECK_PIN,"dc_check") != 0)
	{      
		gpio_free(DC_CHECK_PIN);      
		printk("bq27510 init dc check pin request error\n");      
    return -EIO;    
	} 
	gpio_direction_input(DC_CHECK_PIN); 
	return 0;
}
struct bq27510_platform_data bq27510_info = 
{ 
		.init_dc_check_pin = bq27510_init_dc_check_pin, 
    .dc_check_pin =  DC_CHECK_PIN, 
    .bat_num = LI_LION_BAT_NUM,
};
 
#endif


/*****************************************************************************************
* TI TPS65910 voltage regulator devices 
******************************************************************************************/
#if defined (CONFIG_TPS65910_CORE)
/* VDD1 */
static struct regulator_consumer_supply rk29_vdd1_supplies[] = {
	{
		.supply = "vcore",	//	set name vcore for all platform 
	},
};

/* VDD1 DCDC */
static struct regulator_init_data rk29_regulator_vdd1 = {
	.constraints = {
		.min_uV = 950000,
		.max_uV = 1400000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		.always_on = true,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vdd1_supplies),
	.consumer_supplies = rk29_vdd1_supplies,
};

/* VDD2 */
static struct regulator_consumer_supply rk29_vdd2_supplies[] = {
	{
		.supply = "vdd2",
	},
};

/* VDD2 DCDC */
static struct regulator_init_data rk29_regulator_vdd2 = {
	.constraints = {
		.min_uV = 1200000,
		.max_uV = 1200000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = true,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vdd2_supplies),
	.consumer_supplies = rk29_vdd2_supplies,
};

/* VIO */
static struct regulator_consumer_supply rk29_vio_supplies[] = {
	{
		.supply = "vio",
	},
};

/* VIO  LDO */
static struct regulator_init_data rk29_regulator_vio = {
	.constraints = {
		.min_uV = 3300000,
		.max_uV = 3300000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = true,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vio_supplies),
	.consumer_supplies = rk29_vio_supplies,
};

/* VAUX1 */
static struct regulator_consumer_supply rk29_vaux1_supplies[] = {
	{
		.supply = "vaux1",
	},
};

/* VAUX1  LDO */
static struct regulator_init_data rk29_regulator_vaux1 = {
	.constraints = {
		.min_uV = 2800000,
		.max_uV = 2800000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vaux1_supplies),
	.consumer_supplies = rk29_vaux1_supplies,
};

/* VAUX2 */
static struct regulator_consumer_supply rk29_vaux2_supplies[] = {
	{
		.supply = "vaux2",
	},
};

/* VAUX2  LDO */
static struct regulator_init_data rk29_regulator_vaux2 = {
	.constraints = {
		.min_uV = 2900000,
		.max_uV = 2900000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vaux2_supplies),
	.consumer_supplies = rk29_vaux2_supplies,
};

/* VDAC */
static struct regulator_consumer_supply rk29_vdac_supplies[] = {
	{
		.supply = "vdac",
	},
};

/* VDAC  LDO */
static struct regulator_init_data rk29_regulator_vdac = {
	.constraints = {
		.min_uV = 1800000,
		.max_uV = 1800000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vdac_supplies),
	.consumer_supplies = rk29_vdac_supplies,
};

/* VAUX33 */
static struct regulator_consumer_supply rk29_vaux33_supplies[] = {
	{
		.supply = "vaux33",
	},
};

/* VAUX33  LDO */
static struct regulator_init_data rk29_regulator_vaux33 = {
	.constraints = {
		.min_uV = 3300000,
		.max_uV = 3300000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vaux33_supplies),
	.consumer_supplies = rk29_vaux33_supplies,
};

/* VMMC */
static struct regulator_consumer_supply rk29_vmmc_supplies[] = {
	{
		.supply = "vmmc",
	},
};

/* VMMC  LDO */
static struct regulator_init_data rk29_regulator_vmmc = {
	.constraints = {
		.min_uV = 3000000,
		.max_uV = 3000000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vmmc_supplies),
	.consumer_supplies = rk29_vmmc_supplies,
};

/* VPLL */
static struct regulator_consumer_supply rk29_vpll_supplies[] = {
	{
		.supply = "vpll",
	},
};

/* VPLL  LDO */
static struct regulator_init_data rk29_regulator_vpll = {
	.constraints = {
		.min_uV = 2500000,
		.max_uV = 2500000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = true,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vpll_supplies),
	.consumer_supplies = rk29_vpll_supplies,
};

/* VDIG1 */
static struct regulator_consumer_supply rk29_vdig1_supplies[] = {
	{
		.supply = "vdig1",
	},
};

/* VDIG1  LDO */
static struct regulator_init_data rk29_regulator_vdig1 = {
	.constraints = {
		.min_uV = 2700000,
		.max_uV = 2700000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vdig1_supplies),
	.consumer_supplies = rk29_vdig1_supplies,
};

/* VDIG2 */
static struct regulator_consumer_supply rk29_vdig2_supplies[] = {
	{
		.supply = "vdig2",
	},
};

/* VDIG2  LDO */
static struct regulator_init_data rk29_regulator_vdig2 = {
	.constraints = {
		.min_uV = 1200000,
		.max_uV = 1200000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.always_on = true,
		.apply_uV = true,
	},
	.num_consumer_supplies = ARRAY_SIZE(rk29_vdig2_supplies),
	.consumer_supplies = rk29_vdig2_supplies,
};

static int rk29_tps65910_config(struct tps65910_platform_data *pdata)
{
	u8 val 	= 0;
	int i 	= 0;
	int err = -1;


	/* Configure TPS65910 for rk29 board needs */
	printk("rk29_tps65910_config: tps65910 init config.\n");
	
	err = tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_DEVCTRL2);
	if (err) {
		printk(KERN_ERR "Unable to read TPS65910_REG_DEVCTRL2 reg\n");
		return -EIO;
	}	
	/* Set sleep state active high and allow device turn-off after PWRON long press */
	val |= (TPS65910_DEV2_SLEEPSIG_POL | TPS65910_DEV2_PWON_LP_OFF);

	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val,
			TPS65910_REG_DEVCTRL2);
	if (err) {
		printk(KERN_ERR "Unable to write TPS65910_REG_DEVCTRL2 reg\n");
		return -EIO;
	}

	/* Set the maxinum load current */
	/* VDD1 */
	err = tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VDD1);
	if (err) {
		printk(KERN_ERR "Unable to read TPS65910_REG_VDD1 reg\n");
		return -EIO;
	}

	val |= (1<<5);
	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val, TPS65910_REG_VDD1);
	if (err) {
		printk(KERN_ERR "Unable to write TPS65910_REG_VDD1 reg\n");
		return -EIO;
	}

	/* VDD2 */
	err = tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VDD2);
	if (err) {
		printk(KERN_ERR "Unable to read TPS65910_REG_VDD2 reg\n");
		return -EIO;
	}

	val |= (1<<5);
	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val, TPS65910_REG_VDD2);
	if (err) {
		printk(KERN_ERR "Unable to write TPS65910_REG_VDD2 reg\n");
		return -EIO;
	}

	/* VIO */
	err = tps65910_i2c_read_u8(TPS65910_I2C_ID0, &val, TPS65910_REG_VIO);
	if (err) {
		printk(KERN_ERR "Unable to read TPS65910_REG_VIO reg\n");
		return -EIO;
	}

	val |= (1<<6);
	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val, TPS65910_REG_VIO);
	if (err) {
		printk(KERN_ERR "Unable to write TPS65910_REG_VIO reg\n");
		return -EIO;
	}

	/* Mask ALL interrupts */
	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, 0xFF,
			TPS65910_REG_INT_MSK);
	if (err) {
		printk(KERN_ERR "Unable to write TPS65910_REG_INT_MSK reg\n");
		return -EIO;
	}
	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, 0x03,
			TPS65910_REG_INT_MSK2);
	if (err) {
		printk(KERN_ERR "Unable to write TPS65910_REG_INT_MSK2 reg\n");
		return -EIO;
	}
	
	/* Set RTC Power, disable Smart Reflex in DEVCTRL_REG */
	val = 0;
	val |= (TPS65910_SR_CTL_I2C_SEL);
	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val,
			TPS65910_REG_DEVCTRL);
	if (err) {
		printk(KERN_ERR "Unable to write TPS65910_REG_DEVCTRL reg\n");
		return -EIO;
	}

	printk(KERN_INFO "TPS65910 Set default voltage.\n");
#if 1
	/* VDIG1 Set the default voltage from 1800mV to 2700 mV for camera io */
	val = 0x01;
	val |= (0x03 << 2);
	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val, TPS65910_REG_VDIG1);
	if (err) {
		printk(KERN_ERR "Unable to read TPS65910 Reg at offset 0x%x= \
				\n", TPS65910_REG_VDIG1);
		return -EIO;
	}
#endif

#if 0
	/* VDD1 whitch suplies for core Set the default voltage: 1150 mV(47)*/
	val = 47;	
	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val, TPS65910_REG_VDD1_OP);
	if (err) {
		printk(KERN_ERR "Unable to read TPS65910 Reg at offset 0x%x= \
				\n", TPS65910_REG_VDD1_OP);
		return -EIO;
	}
#endif

#if 1
	/* VDD2 whitch suplies for ddr3 Set the default voltage: 1087 * 1.25mV(41)*/
	val = 42;	
	err = tps65910_i2c_write_u8(TPS65910_I2C_ID0, val, TPS65910_REG_VDD2_OP);
	if (err) {
		printk(KERN_ERR "Unable to read TPS65910 Reg at offset 0x%x= \
				\n", TPS65910_REG_VDD2_OP);
		return -EIO;
	}
#endif

	/* initilize all ISR work as NULL, specific driver will
	 * assign function(s) later.
	 */
	for (i = 0; i < TPS65910_MAX_IRQS; i++)
		pdata->handlers[i] = NULL;

	return 0;
}

struct tps65910_platform_data rk29_tps65910_data = {
	.irq_num 	= (unsigned)TPS65910_HOST_IRQ,
	.gpio  		= NULL,
	.vio   		= &rk29_regulator_vio,
	.vdd1  		= &rk29_regulator_vdd1,
	.vdd2  		= &rk29_regulator_vdd2,
	.vdd3  		= NULL,
	.vdig1		= &rk29_regulator_vdig1,
	.vdig2		= &rk29_regulator_vdig2,
	.vaux33		= &rk29_regulator_vaux33,
	.vmmc		= &rk29_regulator_vmmc,
	.vaux1		= &rk29_regulator_vaux1,
	.vaux2		= &rk29_regulator_vaux2,
	.vdac		= &rk29_regulator_vdac,
	.vpll		= &rk29_regulator_vpll,
	.board_tps65910_config = rk29_tps65910_config,
};
#endif /* CONFIG_TPS65910_CORE */

#if defined (CONFIG_LEDS_ATT1272) 
struct att1272_led_platform_data att1272_data = {
    .name = "att1272",
    .en_gpio = RK29_PIN1_PB0,
    .flen_gpio = RK29_PIN1_PB1,
};
#endif

/*****************************************************************************************
 * i2c devices
 * author: kfx@rock-chips.com
*****************************************************************************************/
static int rk29_i2c0_io_init(void)
{
	rk29_mux_api_set(GPIO2B7_I2C0SCL_NAME, GPIO2L_I2C0_SCL);
	rk29_mux_api_set(GPIO2B6_I2C0SDA_NAME, GPIO2L_I2C0_SDA);
	return 0;
}

static int rk29_i2c1_io_init(void)
{
	rk29_mux_api_set(GPIO1A7_I2C1SCL_NAME, GPIO1L_I2C1_SCL);
	rk29_mux_api_set(GPIO1A6_I2C1SDA_NAME, GPIO1L_I2C1_SDA);
	return 0;
}
static int rk29_i2c2_io_init(void)
{
	rk29_mux_api_set(GPIO5D4_I2C2SCL_NAME, GPIO5H_I2C2_SCL);
	rk29_mux_api_set(GPIO5D3_I2C2SDA_NAME, GPIO5H_I2C2_SDA);
	return 0;
}

static int rk29_i2c3_io_init(void)
{
	rk29_mux_api_set(GPIO2B5_UART3RTSN_I2C3SCL_NAME, GPIO2L_I2C3_SCL);
	rk29_mux_api_set(GPIO2B4_UART3CTSN_I2C3SDA_NAME, GPIO2L_I2C3_SDA);
	return 0;
}

struct rk29_i2c_platform_data default_i2c0_data = {
	.bus_num    = 0,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode 		= I2C_MODE_IRQ,
	.io_init = rk29_i2c0_io_init,
};

struct rk29_i2c_platform_data default_i2c1_data = {
	.bus_num    = 1,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode 		= I2C_MODE_IRQ,
	.io_init = rk29_i2c1_io_init,
};

struct rk29_i2c_platform_data default_i2c2_data = {
	.bus_num    = 2,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode 		= I2C_MODE_IRQ,
	.io_init = rk29_i2c2_io_init,
};

struct rk29_i2c_platform_data default_i2c3_data = {
	.bus_num    = 3,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode 		= I2C_MODE_POLL,
	.io_init = rk29_i2c3_io_init,
};

#ifdef CONFIG_I2C0_RK29
static struct i2c_board_info __initdata board_i2c0_devices[] = {
#if defined (CONFIG_RK1000_CONTROL)
	{
		.type    		= "rk1000_control",
		.addr           = 0x40,
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
#if defined (CONFIG_SENSORS_AK8973)
	{
		.type    		= "ak8973",
		.addr           = 0x1d,
		.flags			= 0,
		.irq			= RK29_PIN0_PA4,
	},
#endif
#if defined (CONFIG_MPU_SENSORS_MPU3050) 
	{
		.type			= "mpu3050",
		.addr			= 0x68,
		.flags			= 0,
		.irq			= RK29_PIN4_PC4,
		.platform_data	= &mpu3050_data,
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
#if defined (CONFIG_ANX7150) || defined (CONFIG_ANX7150_NEW)
    {
		.type           = "anx7150",
        .addr           = 0x39,             //0x39, 0x3d
        .flags          = 0,
        .irq            = RK29_PIN1_PD7,
    },
#endif
#if defined (CONFIG_ATMEL_MXT224)
    {
      .type           = "maXTouch",
      .addr           = 0x4B,
      .flags          = 0,
      .irq            = RK29_PIN0_PA2,
      .platform_data  = &mxt224_info,
    },
#endif

#if defined (CONFIG_LEDS_ATT1272)
    {
      .type           = "att1272",
      .addr           = 0x37,
      .flags          = 0,
      .irq            = 0,
      .platform_data  = &att1272_data,
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
        .flags          = 0,
        .irq            = RK29_PIN0_PA2,
        .platform_data  = &p1003_info,
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
#if defined (CONFIG_TPS65910_CORE)
	{
        .type           = "tps659102",
        .addr           = TPS65910_I2C_ID0,
        .flags          = 0,
        .irq            = TPS65910_HOST_IRQ,
        .platform_data  = &rk29_tps65910_data,
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
};
#endif

#ifdef CONFIG_I2C3_RK29
static struct i2c_board_info __initdata board_i2c3_devices[] = {
};
#endif

/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_VIDEO_RK29
/*---------------- Camera Sensor Configuration Begin ------------------------*/
#define CONFIG_SENSOR_0 RK29_CAM_SENSOR_MT9P111                      /* back camera sensor */
#define CONFIG_SENSOR_IIC_ADDR_0 	    0x78
#define CONFIG_SENSOR_IIC_ADAPTER_ID_0    1
#define CONFIG_SENSOR_POWER_PIN_0         RK29_PIN5_PD7
#define CONFIG_SENSOR_RESET_PIN_0         INVALID_GPIO
#define CONFIG_SENSOR_POWERDN_PIN_0       RK29_PIN1_PB2
#define CONFIG_SENSOR_FALSH_PIN_0         RK29_PIN1_PB1
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_0 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_0 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_0 RK29_CAM_POWERDNACTIVE_H
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_0 RK29_CAM_FLASHACTIVE_H

#define CONFIG_SENSOR_1 RK29_CAM_SENSOR_S5K6AA                      /* front camera sensor */
#define CONFIG_SENSOR_IIC_ADDR_1 	    0x78
#define CONFIG_SENSOR_IIC_ADAPTER_ID_1    1
#define CONFIG_SENSOR_POWER_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_RESET_PIN_1         RK29_PIN1_PB3
#define CONFIG_SENSOR_POWERDN_PIN_1       RK29_PIN6_PB7
#define CONFIG_SENSOR_FALSH_PIN_1         INVALID_GPIO
#define CONFIG_SENSOR_POWERACTIVE_LEVEL_1 RK29_CAM_POWERACTIVE_L
#define CONFIG_SENSOR_RESETACTIVE_LEVEL_1 RK29_CAM_RESETACTIVE_L
#define CONFIG_SENSOR_POWERDNACTIVE_LEVEL_1 RK29_CAM_POWERDNACTIVE_L
#define CONFIG_SENSOR_FLASHACTIVE_LEVEL_1 RK29_CAM_FLASHACTIVE_L
/*---------------- Camera Sensor Configuration End------------------------*/

#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

#define SENSOR_NAME_0 STR(CONFIG_SENSOR_0)			/* back camera sensor */
#define SENSOR_NAME_1 STR(CONFIG_SENSOR_1)			/* front camera sensor */
#define SENSOR_DEVICE_NAME_0  STR(CONS(CONFIG_SENSOR_0, _back))
#define SENSOR_DEVICE_NAME_1  STR(CONS(CONFIG_SENSOR_1, _front))

static int rk29_sensor_io_init(void);
static int rk29_sensor_io_deinit(int sensor);
static int rk29_sensor_ioctrl(struct device *dev,enum rk29camera_ioctrl_cmd cmd,int on);

static struct rk29camera_platform_data rk29_camera_platform_data = {
    .io_init = rk29_sensor_io_init,
    .io_deinit = rk29_sensor_io_deinit,
    .sensor_ioctrl = rk29_sensor_ioctrl,
    .gpio_res = {
        {
            .gpio_reset = CONFIG_SENSOR_RESET_PIN_0,
            .gpio_power = CONFIG_SENSOR_POWER_PIN_0,
            .gpio_powerdown = CONFIG_SENSOR_POWERDN_PIN_0,
            .gpio_flash = CONFIG_SENSOR_FALSH_PIN_0,
            .gpio_flag = (CONFIG_SENSOR_POWERACTIVE_LEVEL_0|CONFIG_SENSOR_RESETACTIVE_LEVEL_0|CONFIG_SENSOR_POWERDNACTIVE_LEVEL_0|CONFIG_SENSOR_FLASHACTIVE_LEVEL_0),
            .gpio_init = 0,
            .dev_name = SENSOR_DEVICE_NAME_0,
        }, {
            .gpio_reset = CONFIG_SENSOR_RESET_PIN_1,
            .gpio_power = CONFIG_SENSOR_POWER_PIN_1,
            .gpio_powerdown = CONFIG_SENSOR_POWERDN_PIN_1,
            .gpio_flash = CONFIG_SENSOR_FALSH_PIN_1,
            .gpio_flag = (CONFIG_SENSOR_POWERACTIVE_LEVEL_1|CONFIG_SENSOR_RESETACTIVE_LEVEL_1|CONFIG_SENSOR_POWERDNACTIVE_LEVEL_1|CONFIG_SENSOR_FLASHACTIVE_LEVEL_1),
            .gpio_init = 0,
            .dev_name = SENSOR_DEVICE_NAME_1,
        }
    },
	#ifdef CONFIG_VIDEO_RK29_WORK_IPP
	.meminfo = {
	    .name  = "camera_ipp_mem",
		.start = MEM_CAMIPP_BASE,
		.size   = MEM_CAMIPP_SIZE,
	}
	#endif
};

static int rk29_sensor_io_init(void)
{
    int ret = 0, i;
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;
	unsigned int camera_powerdown = INVALID_GPIO, camera_flash = INVALID_GPIO;
	unsigned int camera_ioflag;

    for (i=0; i<2; i++) {
        camera_reset = rk29_camera_platform_data.gpio_res[i].gpio_reset;
        camera_power = rk29_camera_platform_data.gpio_res[i].gpio_power;
		camera_powerdown = rk29_camera_platform_data.gpio_res[i].gpio_powerdown;
        camera_flash = rk29_camera_platform_data.gpio_res[i].gpio_flash;
		camera_ioflag = rk29_camera_platform_data.gpio_res[i].gpio_flag;
		rk29_camera_platform_data.gpio_res[i].gpio_init = 0;

        if (camera_power != INVALID_GPIO) {
            ret = gpio_request(camera_power, "camera power");
            if (ret)
				goto sensor_io_int_loop_end;
			rk29_camera_platform_data.gpio_res[i].gpio_init |= RK29_CAM_POWERACTIVE_MASK;
            gpio_set_value(camera_reset, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
            gpio_direction_output(camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));

			//printk("\n%s....power pin(%d) init success(0x%x)  \n",__FUNCTION__,camera_power,(((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));

        }

        if (camera_reset != INVALID_GPIO) {
            ret = gpio_request(camera_reset, "camera reset");
            if (ret)
				goto sensor_io_int_loop_end;
			rk29_camera_platform_data.gpio_res[i].gpio_init |= RK29_CAM_RESETACTIVE_MASK;
            gpio_set_value(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
            gpio_direction_output(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));

			//printk("\n%s....reset pin(%d) init success(0x%x)\n",__FUNCTION__,camera_reset,((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));

        }

		if (camera_powerdown != INVALID_GPIO) {
            ret = gpio_request(camera_powerdown, "camera powerdown");
            if (ret)
				goto sensor_io_int_loop_end;
			rk29_camera_platform_data.gpio_res[i].gpio_init |= RK29_CAM_POWERDNACTIVE_MASK;
            gpio_set_value(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
            gpio_direction_output(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));

			//printk("\n%s....powerdown pin(%d) init success(0x%x) \n",__FUNCTION__,camera_powerdown,((camera_ioflag&RK29_CAM_POWERDNACTIVE_BITPOS)>>RK29_CAM_POWERDNACTIVE_BITPOS));

        }

		if (camera_flash != INVALID_GPIO) {
            ret = gpio_request(camera_flash, "camera flash");
            if (ret)
				goto sensor_io_int_loop_end;
			rk29_camera_platform_data.gpio_res[i].gpio_init |= RK29_CAM_FLASHACTIVE_MASK;
            /*lzg@rock-chips.com; to forbid to  flash while powing on ;*/
#if 0
            gpio_set_value(camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
            gpio_direction_output(camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
#endif
			//printk("\n%s....flash pin(%d) init success(0x%x) \n",__FUNCTION__,camera_flash,((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));

        }
		continue;
sensor_io_int_loop_end:
		rk29_sensor_io_deinit(i);
		continue;
    }

    return 0;
}

static int rk29_sensor_io_deinit(int sensor)
{
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;
	unsigned int camera_powerdown = INVALID_GPIO, camera_flash = INVALID_GPIO;

    camera_reset = rk29_camera_platform_data.gpio_res[sensor].gpio_reset;
    camera_power = rk29_camera_platform_data.gpio_res[sensor].gpio_power;
	camera_powerdown = rk29_camera_platform_data.gpio_res[sensor].gpio_powerdown;
    camera_flash = rk29_camera_platform_data.gpio_res[sensor].gpio_flash;

	if (rk29_camera_platform_data.gpio_res[sensor].gpio_init & RK29_CAM_POWERACTIVE_MASK) {
	    if (camera_power != INVALID_GPIO) {
	        gpio_direction_input(camera_power);
	        gpio_free(camera_power);
	    }
	}

	if (rk29_camera_platform_data.gpio_res[sensor].gpio_init & RK29_CAM_RESETACTIVE_MASK) {
	    if (camera_reset != INVALID_GPIO)  {
	        gpio_direction_input(camera_reset);
	        gpio_free(camera_reset);
	    }
	}

	if (rk29_camera_platform_data.gpio_res[sensor].gpio_init & RK29_CAM_POWERDNACTIVE_MASK) {
	    if (camera_powerdown != INVALID_GPIO)  {
	        gpio_direction_input(camera_powerdown);
	        gpio_free(camera_powerdown);
	    }
	}

	if (rk29_camera_platform_data.gpio_res[sensor].gpio_init & RK29_CAM_FLASHACTIVE_MASK) {
	    if (camera_flash != INVALID_GPIO)  {
	        gpio_direction_input(camera_flash);
	        gpio_free(camera_flash);
	    }
	}

	rk29_camera_platform_data.gpio_res[sensor].gpio_init = 0;
    return 0;
}
static int rk29_sensor_ioctrl(struct device *dev,enum rk29camera_ioctrl_cmd cmd, int on)
{
    unsigned int camera_power=INVALID_GPIO,camera_reset=INVALID_GPIO, camera_powerdown=INVALID_GPIO,camera_flash = INVALID_GPIO;
	unsigned int camera_ioflag,camera_io_init;
	int ret = RK29_CAM_IO_SUCCESS;

    if(rk29_camera_platform_data.gpio_res[0].dev_name &&  (strcmp(rk29_camera_platform_data.gpio_res[0].dev_name, dev_name(dev)) == 0)) {
		camera_power = rk29_camera_platform_data.gpio_res[0].gpio_power;
		camera_reset = rk29_camera_platform_data.gpio_res[0].gpio_reset;
        camera_powerdown = rk29_camera_platform_data.gpio_res[0].gpio_powerdown;
		camera_flash = rk29_camera_platform_data.gpio_res[0].gpio_flash;
		camera_ioflag = rk29_camera_platform_data.gpio_res[0].gpio_flag;
		camera_io_init = rk29_camera_platform_data.gpio_res[0].gpio_init;
    } else if (rk29_camera_platform_data.gpio_res[1].dev_name && (strcmp(rk29_camera_platform_data.gpio_res[1].dev_name, dev_name(dev)) == 0)) {
    	camera_power = rk29_camera_platform_data.gpio_res[1].gpio_power;
        camera_reset = rk29_camera_platform_data.gpio_res[1].gpio_reset;
        camera_powerdown = rk29_camera_platform_data.gpio_res[1].gpio_powerdown;
		camera_flash = rk29_camera_platform_data.gpio_res[1].gpio_flash;
		camera_ioflag = rk29_camera_platform_data.gpio_res[1].gpio_flag;
		camera_io_init = rk29_camera_platform_data.gpio_res[1].gpio_init;
    }

 	switch (cmd)
 	{
 		case Cam_Power:
		{
			if (camera_power != INVALID_GPIO)  {
				if (camera_io_init & RK29_CAM_POWERACTIVE_MASK) {
			        if (on) {
			        	gpio_set_value(camera_power, ((camera_ioflag&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
						//printk("\n%s..%s..PowerPin=%d ..PinLevel = %x   \n",__FUNCTION__,dev_name(dev), camera_power, ((camera_ioflag&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
						msleep(10);
					} else {
						gpio_set_value(camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
						//printk("\n%s..%s..PowerPin=%d ..PinLevel = %x   \n",__FUNCTION__,dev_name(dev), camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
					}
				} else {
					ret = RK29_CAM_EIO_REQUESTFAIL;
					printk("\n%s..%s..PowerPin=%d request failed!\n",__FUNCTION__,dev_name(dev),camera_reset);
				}
		    } else {
				ret = RK29_CAM_EIO_INVALID;
		    }
			break;
		}
		case Cam_Reset:
		{
			if (camera_reset != INVALID_GPIO) {
				if (camera_io_init & RK29_CAM_RESETACTIVE_MASK) {
					if (on) {
			        	gpio_set_value(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
			        	//printk("\n%s..%s..ResetPin=%d ..PinLevel = %x \n",__FUNCTION__,dev_name(dev),camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
					} else {
						gpio_set_value(camera_reset,(((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
		        		//printk("\n%s..%s..ResetPin= %d..PinLevel = %x   \n",__FUNCTION__,dev_name(dev), camera_reset, (((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
			        }
				} else {
					ret = RK29_CAM_EIO_REQUESTFAIL;
					printk("\n%s..%s..ResetPin=%d request failed!\n",__FUNCTION__,dev_name(dev),camera_reset);
				}
		    } else {
				ret = RK29_CAM_EIO_INVALID;
		    }
			break;
		}

		case Cam_PowerDown:
		{
			if (camera_powerdown != INVALID_GPIO) {
				if (camera_io_init & RK29_CAM_POWERDNACTIVE_MASK) {
					if (on) {
			        	gpio_set_value(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
			        	//printk("\n%s..%s..PowerDownPin=%d ..PinLevel = %x \n",__FUNCTION__,dev_name(dev),camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
					} else {
						gpio_set_value(camera_powerdown,(((~camera_ioflag)&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
		        		//printk("\n%s..%s..PowerDownPin= %d..PinLevel = %x   \n",__FUNCTION__,dev_name(dev), camera_powerdown, (((~camera_ioflag)&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
			        }
				} else {
					ret = RK29_CAM_EIO_REQUESTFAIL;
					printk("\n%s..%s..PowerDownPin=%d request failed!\n",__FUNCTION__,dev_name(dev),camera_powerdown);
				}
		    } else {
				ret = RK29_CAM_EIO_INVALID;
		    }
			break;
		}

		case Cam_Flash:
		{
			if (camera_flash != INVALID_GPIO) {
				if (camera_io_init & RK29_CAM_FLASHACTIVE_MASK) {
                    switch (on)
                    {
                        case Flash_Off:
                        {
                            gpio_set_value(camera_flash,(((~camera_ioflag)&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
		        		    printk("\n%s..%s..FlashPin= %d..PinLevel = %x   \n",__FUNCTION__,dev_name(dev), camera_flash, (((~camera_ioflag)&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS)); 
		        		    break;
                        }

                        case Flash_On:
                        {
                            gpio_set_value(camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
			        	    printk("\n%s..%s..FlashPin=%d ..PinLevel = %x \n",__FUNCTION__,dev_name(dev),camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
			        	    break;
                        }

                        case Flash_Torch:
                        {
                            gpio_set_value(camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
			        	    printk("\n%s..%s..FlashPin=%d ..PinLevel = %x \n",__FUNCTION__,dev_name(dev),camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
			        	    break;
                        }

                        default:
                        {
                            printk("\n%s..%s..Flash command(%d) is invalidate \n",__FUNCTION__,dev_name(dev),on);
                            break;
                        }
                    }
				} else {
					ret = RK29_CAM_EIO_REQUESTFAIL;
					printk("\n%s..%s..FlashPin=%d request failed!\n",__FUNCTION__,dev_name(dev),camera_flash);
				}
		    } else {
				ret = RK29_CAM_EIO_INVALID;
		    }
			break;
		}

		default:
		{
			printk("%s cmd(0x%x) is unknown!\n",__FUNCTION__, cmd);
			break;
		}
 	}
    return ret;
}
static int rk29_sensor_power(struct device *dev, int on)
{
	rk29_sensor_ioctrl(dev,Cam_Power,on);
    return 0;
}
static int rk29_sensor_reset(struct device *dev)
{
	rk29_sensor_ioctrl(dev,Cam_Reset,1);
	msleep(2);
	rk29_sensor_ioctrl(dev,Cam_Reset,0);
	return 0;
}
static int rk29_sensor_powerdown(struct device *dev, int on)
{
	return rk29_sensor_ioctrl(dev,Cam_PowerDown,on);
}
#if (CONFIG_SENSOR_IIC_ADDR_0 != 0x00)
static struct i2c_board_info rk29_i2c_cam_info_0[] = {
	{
		I2C_BOARD_INFO(SENSOR_NAME_0, CONFIG_SENSOR_IIC_ADDR_0>>1)
	},
};

static struct soc_camera_link rk29_iclink_0 = {
	.bus_id		= RK29_CAM_PLATFORM_DEV_ID,
	.power		= rk29_sensor_power,
	.powerdown  = rk29_sensor_powerdown,
	.board_info	= &rk29_i2c_cam_info_0[0],
	.i2c_adapter_id	= CONFIG_SENSOR_IIC_ADAPTER_ID_0,
	.module_name	= SENSOR_NAME_0,
};

/*platform_device : soc-camera need  */
static struct platform_device rk29_soc_camera_pdrv_0 = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.init_name = SENSOR_DEVICE_NAME_0,
		.platform_data = &rk29_iclink_0,
	},
};
#endif
#if (CONFIG_SENSOR_IIC_ADDR_1 != 0x00)
static struct i2c_board_info rk29_i2c_cam_info_1[] = {
	{
		I2C_BOARD_INFO(SENSOR_NAME_1, CONFIG_SENSOR_IIC_ADDR_1>>1)
	},
};

static struct soc_camera_link rk29_iclink_1 = {
	.bus_id		= RK29_CAM_PLATFORM_DEV_ID,
	.power		= rk29_sensor_power,
	.powerdown  = rk29_sensor_powerdown,
	.board_info	= &rk29_i2c_cam_info_1[0],
	.i2c_adapter_id	= CONFIG_SENSOR_IIC_ADAPTER_ID_1,
	.module_name	= SENSOR_NAME_1,
};

/*platform_device : soc-camera need  */
static struct platform_device rk29_soc_camera_pdrv_1 = {
	.name	= "soc-camera-pdrv",
	.id	= 1,
	.dev	= {
		.init_name = SENSOR_DEVICE_NAME_1,
		.platform_data = &rk29_iclink_1,
	},
};
#endif

static u64 rockchip_device_camera_dmamask = 0xffffffffUL;
static struct resource rk29_camera_resource[] = {
	[0] = {
		.start = RK29_VIP_PHYS,
		.end   = RK29_VIP_PHYS + RK29_VIP_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_VIP,
		.end   = IRQ_VIP,
		.flags = IORESOURCE_IRQ,
	}
};

/*platform_device : */
static struct platform_device rk29_device_camera = {
	.name		  = RK29_CAM_DRV_NAME,
	.id		  = RK29_CAM_PLATFORM_DEV_ID,               /* This is used to put cameras on this interface */
	.num_resources	  = ARRAY_SIZE(rk29_camera_resource),
	.resource	  = rk29_camera_resource,
	.dev            = {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data  = &rk29_camera_platform_data,
	}
};
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
    return ret;
}

static int rk29_backlight_pwm_suspend(void)
{
	int ret = 0;
	rk29_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE_GPIO);
	if (ret = gpio_request(PWM_GPIO, NULL)) {
		printk("func %s, line %d: request gpio fail\n", __FUNCTION__, __LINE__);
		return -1;
	}
	gpio_direction_output(PWM_GPIO, GPIO_HIGH);
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
};
#endif

#ifdef CONFIG_CM3202 
/**********************************
 * cm3202 lightsensor *
**********************************/
#define CM3202_SD 	RK29_PIN5_PA2
static int cm3202_init_hw(void)
{
	int ret;
	ret = gpio_request(CM3202_SD, "cm3202_sd");
	if (ret) {
		printk( "failed to request cm3202 SD GPIO%d\n",CM3202_SD);
	}
	gpio_pull_updown(CM3202_SD,GPIOPullDown);
	return ret;
}

static void cm3202_exit_hw(void)
{
	gpio_free(CM3202_SD);
}

struct cm3202_platform_data cm3202_info = {
	.CM3202_SD_IOPIN = CM3202_SD,
	.DATA_ADC_CHN = 2,
	.init_platform_hw = cm3202_init_hw,
	.exit_platform_hw = cm3202_exit_hw,
};

struct platform_device cm3202_device = {
	.name = "lightsensor",
	.id = -1,
	.dev = {
		.platform_data = &cm3202_info,
	}
};
#endif

#ifdef CONFIG_FIH_TOUCHKEY_LED
#define FIH_TOUCHKEY_LED_PWM_ID 1
#define FIH_TOUCHKEY_LED_PWM_MUX_NAME      	GPIO5D2_PWM1_UART1SIRIN_NAME 
#define FIH_TOUCHKEY_LED_PWM_MUX_MODE      	GPIO5H_PWM1 
#define FIH_TOUCHKEY_LED_PWM_MUX_MODE_GPIO 	GPIO5H_GPIO5D2 
#define FIH_TOUCHKEY_LED_PWM_GPIO 			RK29_PIN5_PD2
#define FIH_TOUCHKEY_LED_PWM_EFFECT_VALUE  	1

static int fih_touchkey_led_io_init(void)
{
    rk29_mux_api_set(FIH_TOUCHKEY_LED_PWM_MUX_NAME, FIH_TOUCHKEY_LED_PWM_MUX_MODE);
}
static int fih_touchkey_led_io_deinit(void)
{
    rk29_mux_api_set(FIH_TOUCHKEY_LED_PWM_MUX_NAME, FIH_TOUCHKEY_LED_PWM_MUX_MODE_GPIO);
}

static int fih_touchkey_led_pwm_suspend(void)
{
	int ret = 0;
	rk29_mux_api_set(FIH_TOUCHKEY_LED_PWM_MUX_NAME, FIH_TOUCHKEY_LED_PWM_MUX_MODE_GPIO);
	if (ret = gpio_request(FIH_TOUCHKEY_LED_PWM_GPIO, NULL)) {
		printk("func %s, line %d: request gpio fail\n", __FUNCTION__, __LINE__);
		return -1;
	}
	gpio_direction_output(FIH_TOUCHKEY_LED_PWM_GPIO, GPIO_LOW);
	return ret;
}

static int fih_touchkey_led_pwm_resume(void)
{
	gpio_free(FIH_TOUCHKEY_LED_PWM_GPIO);
	rk29_mux_api_set(FIH_TOUCHKEY_LED_PWM_MUX_NAME, FIH_TOUCHKEY_LED_PWM_MUX_MODE);
	return 0;
}

struct rk29_bl_info fih_touchkey_led_info = {
    .pwm_id   = FIH_TOUCHKEY_LED_PWM_ID,
    .bl_ref   = FIH_TOUCHKEY_LED_PWM_EFFECT_VALUE,
    .io_init   = fih_touchkey_led_io_init,
    .io_deinit = fih_touchkey_led_io_deinit,
    .pwm_suspend = fih_touchkey_led_pwm_suspend,
    .pwm_resume = fih_touchkey_led_pwm_resume,
};
#endif
#ifdef CONFIG_FIH_TOUCHKEY_LED
struct platform_device fih_touchkey_led = {
		.name	= "fih_touchkey_led",
		.id 	= -1,
        .dev    = {
           .platform_data  = &fih_touchkey_led_info,
        }
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


/*****************************************************************************************
 * SDMMC devices
*****************************************************************************************/
#ifdef CONFIG_SDMMC0_RK29
static int rk29_sdmmc0_cfg_gpio(void)
{
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
	return 0;
}

#define CONFIG_SDMMC0_USE_DMA
struct rk29_sdmmc_platform_data default_sdmmc0_data = {
	.host_ocr_avail = (MMC_VDD_25_26|MMC_VDD_26_27|MMC_VDD_27_28|MMC_VDD_28_29|MMC_VDD_29_30|
					   MMC_VDD_30_31|MMC_VDD_31_32|MMC_VDD_32_33|
					   MMC_VDD_33_34|MMC_VDD_34_35| MMC_VDD_35_36),
	.host_caps 	= (MMC_CAP_4_BIT_DATA|MMC_CAP_MMC_HIGHSPEED|MMC_CAP_SD_HIGHSPEED),
	.io_init = rk29_sdmmc0_cfg_gpio,
	.dma_name = "sd_mmc",
#ifdef CONFIG_SDMMC0_USE_DMA
	.use_dma  = 1,
#else
	.use_dma = 0,
#endif
	.detect_irq = RK29_PIN2_PA2, // INVALID_GPIO
	.enable_sd_wakeup = 0,
};
#endif
#ifdef CONFIG_SDMMC1_RK29
#define CONFIG_SDMMC1_USE_DMA
static int rk29_sdmmc1_cfg_gpio(void)
{
	rk29_mux_api_set(GPIO1C2_SDMMC1CMD_NAME, GPIO1H_SDMMC1_CMD);
	rk29_mux_api_set(GPIO1C7_SDMMC1CLKOUT_NAME, GPIO1H_SDMMC1_CLKOUT);
	rk29_mux_api_set(GPIO1C3_SDMMC1DATA0_NAME, GPIO1H_SDMMC1_DATA0);
	rk29_mux_api_set(GPIO1C4_SDMMC1DATA1_NAME, GPIO1H_SDMMC1_DATA1);
	rk29_mux_api_set(GPIO1C5_SDMMC1DATA2_NAME, GPIO1H_SDMMC1_DATA2);
	rk29_mux_api_set(GPIO1C6_SDMMC1DATA3_NAME, GPIO1H_SDMMC1_DATA3);
	//rk29_mux_api_set(GPIO1C0_UART0CTSN_SDMMC1DETECTN_NAME, GPIO1H_SDMMC1_DETECT_N);
	return 0;
}

#ifdef CONFIG_WIFI_CONTROL_FUNC
static int rk29sdk_wifi_status(struct device *dev);
static int rk29sdk_wifi_status_register(void (*callback)(int card_presend, void *dev_id), void *dev_id);
#endif

#define RK29SDK_WIFI_SDIO_CARD_DETECT_N    RK29_PIN1_PD6

struct rk29_sdmmc_platform_data default_sdmmc1_data = {
	.host_ocr_avail = (MMC_VDD_25_26|MMC_VDD_26_27|MMC_VDD_27_28|MMC_VDD_28_29|
					   MMC_VDD_29_30|MMC_VDD_30_31|MMC_VDD_31_32|
					   MMC_VDD_32_33|MMC_VDD_33_34),
	.host_caps 	= (MMC_CAP_4_BIT_DATA|MMC_CAP_SDIO_IRQ|
				   MMC_CAP_MMC_HIGHSPEED|MMC_CAP_SD_HIGHSPEED),
	.io_init = rk29_sdmmc1_cfg_gpio,
	.dma_name = "sdio",
#ifdef CONFIG_SDMMC1_USE_DMA
	.use_dma  = 1,
#else
	.use_dma = 0,
#endif
#ifdef CONFIG_WIFI_CONTROL_FUNC
        .status = rk29sdk_wifi_status,
        .register_status_notify = rk29sdk_wifi_status_register,
#endif
#if 0
        .detect_irq = RK29SDK_WIFI_SDIO_CARD_DETECT_N,
#endif
};
#endif

#ifdef CONFIG_WIFI_CONTROL_FUNC
#define RK29SDK_WIFI_BT_GPIO_POWER_N       RK29_PIN5_PD6
#define RK29SDK_WIFI_GPIO_RESET_N          RK29_PIN6_PC0
#define RK29SDK_BT_GPIO_RESET_N            RK29_PIN6_PC4

static int rk29sdk_wifi_cd = 0;   /* wifi virtual 'card detect' status */
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;
int rk29sdk_wifi_power_state = 0;
int rk29sdk_bt_power_state = 0;

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

    pr_info("%s: init finished\n",__func__);

    return 0;
}

static int rk29sdk_wifi_power(int on)
{
        pr_info("%s: %d\n", __func__, on);
        if (on){
                gpio_set_value(RK29SDK_WIFI_BT_GPIO_POWER_N, GPIO_HIGH);
                gpio_set_value(RK29SDK_WIFI_GPIO_RESET_N, GPIO_HIGH);
                mdelay(100);
                pr_info("wifi turn on power\n");
        }else{
                if (!rk29sdk_bt_power_state){
                        gpio_set_value(RK29SDK_WIFI_BT_GPIO_POWER_N, GPIO_LOW);
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


#ifdef CONFIG_VIVANTE
static struct resource resources_gpu[] = {
    [0] = {
		.name 	= "gpu_irq",
        .start 	= IRQ_GPU,
        .end    = IRQ_GPU,
        .flags  = IORESOURCE_IRQ,
    },
    [1] = {
		.name = "gpu_base",
        .start  = RK29_GPU_PHYS,
        .end    = RK29_GPU_PHYS + RK29_GPU_SIZE,
        .flags  = IORESOURCE_MEM,
    },
    [2] = {
		.name = "gpu_mem",
        .start  = PMEM_GPU_BASE,
        .end    = PMEM_GPU_BASE + PMEM_GPU_SIZE,
        .flags  = IORESOURCE_MEM,
    },
};
static struct platform_device rk29_device_gpu = {
    .name             = "galcore",
    .id               = 0,
    .num_resources    = ARRAY_SIZE(resources_gpu),
    .resource         = resources_gpu,
};
#endif
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
#if CONFIG_ANDROID_TIMED_GPIO
static struct timed_gpio timed_gpios[] = {
	{
		.name = "vibrator",
		.gpio = RK29_PIN6_PD3,
		.max_timeout = 1000,
		.active_low = 0,
		.adjust_time =5,      //adjust for diff product
	},
	{
		.name = "vibratorR",
		.gpio =	RK29_PIN4_PD5,
		.max_timeout = 1000,
		.active_low = 0,
		.adjust_time =5,      //adjust for diff product

	},
};
struct timed_gpio_platform_data rk29_vibrator_info = {
	.num_gpios = 2,
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
static void __init rk29_board_iomux_init(void)
{
	#ifdef CONFIG_RK29_PWM_REGULATOR
	rk29_mux_api_set(REGULATOR_PWM_MUX_NAME,REGULATOR_PWM_MUX_MODE);
	#endif
	#if defined (CONFIG_TPS65910_CORE)
    rk29_mux_api_set(GPIO4D32_CPUTRACEDATA32_NAME, GPIO4H_GPIO4D32);
	#endif
}

static struct platform_device *devices[] __initdata = {
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
#ifdef CONFIG_SDMMC0_RK29
	&rk29_device_sdmmc0,
#endif
#ifdef CONFIG_SDMMC1_RK29
	&rk29_device_sdmmc1,
#endif

#ifdef CONFIG_CM3202
	&cm3202_device,
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
#ifdef CONFIG_RK29_VMAC
	&rk29_device_vmac,
#endif
#ifdef CONFIG_VIVANTE
	&rk29_device_gpu,
#endif
#ifdef CONFIG_VIDEO_RK29
 	&rk29_device_camera,      /* ddl@rock-chips.com : camera support  */
 	#if (CONFIG_SENSOR_IIC_ADDR_0 != 0x00)
 	&rk29_soc_camera_pdrv_0,
 	#endif
	#if (CONFIG_SENSOR_IIC_ADDR_1 != 0x00)
 	&rk29_soc_camera_pdrv_1,
 	#endif
 	&android_pmem_cam_device,
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
#ifdef CONFIG_RK29_IPP
	&rk29_device_ipp,
#endif
#ifdef CONFIG_VIDEO_RK29XX_VOUT
	&rk29_v4l2_output_devce,
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
	&rk29_device_vibrator,
#endif
#ifdef CONFIG_FIH_TOUCHKEY_LED
	&fih_touchkey_led,
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
	gic_dist_init(0, (void __iomem *)RK29_GICPERI_BASE, 32);
	gic_cpu_init(0, (void __iomem *)RK29_GICCPU_BASE);
}

static void __init machine_rk29_init_irq(void)
{
	rk29_gic_init_irq();
	rk29_gpio_init();
}

static void __init machine_rk29_board_init(void)
{
	rk29_board_iomux_init();

	board_power_init();

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
}

static void __init machine_rk29_fixup(struct machine_desc *desc, struct tag *tags,
					char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = RK29_SDRAM_PHYS;
	mi->bank[0].node = PHYS_TO_NID(RK29_SDRAM_PHYS);
	mi->bank[0].size = LINUX_SIZE;
}

static void __init machine_rk29_mapio(void)
{
	rk29_map_common_io();
	rk29_setup_early_printk();
	rk29_sram_init();
	rk29_clock_init(periph_pll_288mhz);
	rk29_iomux_init();
	ddr_init(DDR_TYPE,DDR_FREQ);  // DDR3_1333H, 400
}

MACHINE_START(RK29, "RK29board")
	/* UART for LL DEBUG */
	.phys_io	= RK29_UART1_PHYS,
	.io_pg_offst	= ((RK29_UART1_BASE) >> 18) & 0xfffc,
	.boot_params	= RK29_SDRAM_PHYS + 0x88000,
	.fixup		= machine_rk29_fixup,
	.map_io		= machine_rk29_mapio,
	.init_irq	= machine_rk29_init_irq,
	.init_machine	= machine_rk29_board_init,
	.timer		= &rk29_timer,
MACHINE_END
