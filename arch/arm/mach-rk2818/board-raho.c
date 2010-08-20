/* linux/arch/arm/mach-rk2818/board-phonesdk.c
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
#include <linux/circ_buf.h>
#include <linux/miscdevice.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/irqs.h>
#include <mach/board.h>
#include <mach/rk2818_iomap.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/spi_fpga.h>
#include <mach/rk2818_camera.h>                          /* ddl@rock-chips.com : camera support */
#include <mach/rk2818_nand.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/dm9000.h>
#include <linux/capella_cm3602.h>

#include <media/soc_camera.h>                               /* ddl@rock-chips.com : camera support */


#include "devices.h"

#include "../../../drivers/spi/rk2818_spim.h"
#include <linux/regulator/rk2818_lp8725.h>
#include "../../../drivers/input/touchscreen/xpt2046_ts.h"
#include "../../../drivers/staging/android/timed_gpio.h"
#include "../../../sound/soc/codecs/wm8994.h"
#include "../../../drivers/headset_observe/rk2818_headset.h"

/* --------------------------------------------------------------------
 *  声明了rk2818_gpioBank数组，并定义了GPIO寄存器组ID和寄存器基地址。
 * -------------------------------------------------------------------- */

static struct rk2818_gpio_bank rk2818_gpioBank[] = {
		{
		.id		= RK2818_ID_PIOA,
		.offset		= RK2818_GPIO0_BASE,
		.clock		= NULL,
	}, 
		{
		.id		= RK2818_ID_PIOB,
		.offset		= RK2818_GPIO0_BASE,
		.clock		= NULL,
	}, 
		{
		.id		= RK2818_ID_PIOC,
		.offset		= RK2818_GPIO0_BASE,
		.clock		= NULL,
	}, 
		{
		.id		= RK2818_ID_PIOD,
		.offset		= RK2818_GPIO0_BASE,
		.clock		= NULL,
	},
		{
		.id		= RK2818_ID_PIOE,
		.offset		= RK2818_GPIO1_BASE,
		.clock		= NULL,
	},
		{
		.id		= RK2818_ID_PIOF,
		.offset		= RK2818_GPIO1_BASE,
		.clock		= NULL,
	},
		{
		.id		= RK2818_ID_PIOG,
		.offset		= RK2818_GPIO1_BASE,
		.clock		= NULL,
	},
		{
		.id		= RK2818_ID_PIOH,
		.offset		= RK2818_GPIO1_BASE,
		.clock		= NULL,
	}
};

//IO映射方式描述 ，每个为一段线性连续映射
static struct map_desc rk2818_io_desc[] __initdata = {

	{
		.virtual	= RK2818_MCDMA_BASE,					//虚拟地址
		.pfn		= __phys_to_pfn(RK2818_MCDMA_PHYS),    //物理地址，须与页表对齐
		.length 	= RK2818_MCDMA_SIZE,							//长度
		.type		= MT_DEVICE							//映射方式
	},
	
	{
		.virtual	= RK2818_DWDMA_BASE,					
		.pfn		= __phys_to_pfn(RK2818_DWDMA_PHYS),    
		.length 	= RK2818_DWDMA_SIZE,						
		.type		= MT_DEVICE							
	},
	
	{
		.virtual	= RK2818_INTC_BASE,					
		.pfn		= __phys_to_pfn(RK2818_INTC_PHYS),   
		.length 	= RK2818_INTC_SIZE,					
		.type		= MT_DEVICE						
	},

	{
		.virtual	= RK2818_NANDC_BASE, 				
		.pfn		= __phys_to_pfn(RK2818_NANDC_PHYS),	 
		.length 	= RK2818_NANDC_SIZE, 				
		.type		= MT_DEVICE 					
	},

	{
		.virtual	= RK2818_SDRAMC_BASE,
		.pfn		= __phys_to_pfn(RK2818_SDRAMC_PHYS),
		.length 	= RK2818_SDRAMC_SIZE,
		.type		= MT_DEVICE
	},

	{
		.virtual	= RK2818_ARMDARBITER_BASE,					
		.pfn		= __phys_to_pfn(RK2818_ARMDARBITER_PHYS),    
		.length 	= RK2818_ARMDARBITER_SIZE,						
		.type		= MT_DEVICE							
	},
	
	{
		.virtual	= RK2818_APB_BASE,
		.pfn		= __phys_to_pfn(RK2818_APB_PHYS),
		.length 	= 0xa0000,                     
		.type		= MT_DEVICE
	},
	
	{
		.virtual	= RK2818_WDT_BASE,
		.pfn		= __phys_to_pfn(RK2818_WDT_PHYS),
		.length 	= 0xa0000,                      ///apb bus i2s i2c spi no map in this
		.type		= MT_DEVICE
	},
};

/*****************************************************************************************
 * sd/mmc devices
 * author: kfx@rock-chips.com
*****************************************************************************************/
static int rk2818_sdmmc0_io_init(void)
{
    rk2818_mux_api_set(GPIOH_MMC0D_SEL_NAME, IOMUXA_SDMMC0_DATA123);
	rk2818_mux_api_set(GPIOH_MMC0_SEL_NAME, IOMUXA_SDMMC0_CMD_DATA0_CLKOUT);

    return 0;
}

static int rk2818_sdmmc1_io_init(void)
{
	rk2818_mux_api_set(GPIOG_MMC1_SEL_NAME, IOMUXA_SDMMC1_CMD_DATA0_CLKOUT);
	rk2818_mux_api_set(GPIOG_MMC1D_SEL_NAME, IOMUXA_SDMMC1_DATA123);

    return 0;
}
#define CONFIG_SDMMC0_USE_DMA
#define CONFIG_SDMMC1_USE_DMA
struct rk2818_sdmmc_platform_data default_sdmmc0_data = {
	.host_ocr_avail = (MMC_VDD_27_28|MMC_VDD_28_29|MMC_VDD_29_30|
					   MMC_VDD_30_31|MMC_VDD_31_32|MMC_VDD_32_33| 
					   MMC_VDD_33_34|MMC_VDD_34_35| MMC_VDD_35_36),
	.host_caps 	= (MMC_CAP_4_BIT_DATA|MMC_CAP_MMC_HIGHSPEED|MMC_CAP_SD_HIGHSPEED),
	.io_init = rk2818_sdmmc0_io_init,
	.no_detect = 0,
	.dma_name = "sd_mmc",
#ifdef CONFIG_SDMMC0_USE_DMA
	.use_dma  = 1,
#else
	.use_dma = 0,
#endif
};
struct rk2818_sdmmc_platform_data default_sdmmc1_data = {
	.host_ocr_avail = (MMC_VDD_26_27|MMC_VDD_27_28|MMC_VDD_28_29|
					   MMC_VDD_29_30|MMC_VDD_30_31|MMC_VDD_31_32|
					   MMC_VDD_32_33|MMC_VDD_33_34),
	.host_caps 	= (MMC_CAP_4_BIT_DATA|MMC_CAP_SDIO_IRQ|
				   MMC_CAP_MMC_HIGHSPEED|MMC_CAP_SD_HIGHSPEED),
	.io_init = rk2818_sdmmc1_io_init,
	.no_detect = 1,
	.dma_name = "sdio",
#ifdef CONFIG_SDMMC1_USE_DMA
	.use_dma  = 1,
#else
	.use_dma = 0,
#endif
};

/*****************************************************************************************
 * extern gpio devices
 * author: xxx@rock-chips.com
 *****************************************************************************************/
#if defined (CONFIG_GPIO_PCA9554)
struct rk2818_gpio_expander_info  extern_gpio_settinginfo[] = {
	{
		.gpio_num    		=RK2818_PIN_PI0,
		.pin_type           = GPIO_IN,
		//.pin_value			=GPIO_HIGH,
	 },

	{
		.gpio_num    		=RK2818_PIN_PI4,// tp3
		.pin_type           = GPIO_IN,
		//.pin_value			=GPIO_HIGH,
	 },
	 
	 {
		.gpio_num    		=RK2818_PIN_PI5,//tp4
		.pin_type           = GPIO_IN,
		//.pin_value			=GPIO_HIGH,
	 },
	 {
		.gpio_num    		=RK2818_PIN_PI6,//tp2
		.pin_type           = GPIO_OUT,
		//.pin_value			=GPIO_HIGH,
	 },
	 {
		.gpio_num    		=RK2818_PIN_PI7,//tp1
		.pin_type           = GPIO_OUT,
		.pin_value			=GPIO_HIGH,
	 },


		
};

struct pca9554_platform_data rk2818_pca9554_data={
	.gpio_base=GPIO_EXPANDER_BASE,
	.gpio_pin_num=CONFIG_EXPANDED_GPIO_NUM,
	.gpio_irq_start=NR_AIC_IRQS + 2*NUM_GROUP,
	.irq_pin_num=CONFIG_EXPANDED_GPIO_IRQ_NUM,
	.pca9954_irq_pin=RK2818_PIN_PE2,
	.settinginfo=extern_gpio_settinginfo,
	.settinginfolen=ARRAY_SIZE(extern_gpio_settinginfo),
	.names="pca9554",
};
#endif

/*****************************************************************************************
 *regulator devices  drivers/regulator/rk2818_lp8725.c  linux/regulator/rk2818_lp8725.h
 *author: cym
*****************************************************************************************/
#if defined (CONFIG_RK2818_REGULATOR_LP8725)
/*ldo1 2V8OUT USB2.5V LCD_VCC*/
static struct regulator_consumer_supply ldo1_consumers[] = {
	{
		.supply = "ldo1",
	}
};

static struct regulator_init_data rk2818_lp8725_ldo1_data = {
	.constraints = {
		.name = "LDO1",
		.min_uV = 1200000,
		.max_uV = 3300000,
		.apply_uV = 1,		
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,		
	},
	.num_consumer_supplies = ARRAY_SIZE(ldo1_consumers),
	.consumer_supplies = ldo1_consumers,
};

/*ldo2 CAMERA_1V8 SD_CARD*/
static struct regulator_consumer_supply ldo2_consumers[] = {
	{
		.supply = "ldo2",
	}
};

static struct regulator_init_data rk2818_lp8725_ldo2_data = {
	.constraints = {
		.name = "LDO2",
		.min_uV = 1200000,
		.max_uV = 3300000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,		
	},
	.num_consumer_supplies = ARRAY_SIZE(ldo2_consumers),
	.consumer_supplies = ldo2_consumers,
};

/*ldo3 VCC_NAND WIFI/BT/FM_BCM4325*/
static struct regulator_consumer_supply ldo3_consumers[] = {
	{
		.supply = "ldo3",
	}
};

static struct regulator_init_data rk2818_lp8725_ldo3_data = {
	.constraints = {
		.name = "LDO3",
		.min_uV = 1200000,
		.max_uV = 3300000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(ldo3_consumers),
	.consumer_supplies = ldo3_consumers,
};

/*ldo4 VCCA CODEC_WM8994*/
static struct regulator_consumer_supply ldo4_consumers[] = {
	{
		.supply = "ldo4",
	}
};

static struct regulator_init_data rk2818_lp8725_ldo4_data = {
	.constraints = {
		.name = "LDO4",
		.min_uV = 1200000,
		.max_uV = 3300000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(ldo4_consumers),
	.consumer_supplies = ldo4_consumers,
};

/*ldo5 AVDD18 CODEC_WM8994*/
static struct regulator_consumer_supply ldo5_consumers[] = {
	{
		.supply = "ldo5",
	}
};

static struct regulator_init_data rk2818_lp8725_ldo5_data = {
	.constraints = {
		.name = "LDO5",
		.min_uV = 1200000,
		.max_uV = 3300000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(ldo5_consumers),
	.consumer_supplies = ldo5_consumers,
};

/*lilo1 VCCIO Sensor（3M）*/
static struct regulator_consumer_supply lilo1_consumers[] = {
	{
		.supply = "lilo1",
	}
};

static struct regulator_init_data rk2818_lp8725_lilo1_data = {
	.constraints = {
		.name = "LILO1",
		.min_uV = 800000,
		.max_uV = 3300000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(lilo1_consumers),
	.consumer_supplies = lilo1_consumers
};

/*lilo2 VCC33_SD Sensor（3M）*/
static struct regulator_consumer_supply lilo2_consumers[] = {
	{
		.supply = "lilo2",
	}
};

static struct regulator_init_data rk2818_lp8725_lilo2_data = {
	.constraints = {
		.name = "LILO2",
		.min_uV = 800000,
		.max_uV = 3300000,
		.apply_uV = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(lilo2_consumers),
	.consumer_supplies = lilo2_consumers
};

/*buck1 VDD12 Core*/
static struct regulator_consumer_supply buck1_consumers[] = {
	{
		.supply = "vdd12",
	}
};

static struct regulator_init_data rk2818_lp8725_buck1_data = {
	.constraints = {
		.name = "VDD12",
		.min_uV = 800000,
		.max_uV = 1500000,
		.apply_uV = 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_IDLE | REGULATOR_MODE_NORMAL,
	},
	.num_consumer_supplies = ARRAY_SIZE(buck1_consumers),
	.consumer_supplies = buck1_consumers
};

/*buck2 VDDDR MobileDDR VCC*/
static struct regulator_consumer_supply buck2_consumers[] = {
	{
		.supply = "vccdr",
	}
};

static struct regulator_init_data rk2818_lp8725_buck2_data = {
	.constraints = {
		.name = "VCCDR",
		.min_uV = 1800000,
		.max_uV = 1800000,
		.apply_uV = 1,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(buck2_consumers),
	.consumer_supplies = buck2_consumers
};

/*buck1_v2 VDD12 Core*/
static struct regulator_consumer_supply buck1_v2_consumers[] = {
	{
		.supply = "vdd12_v2",
	}
};

static struct regulator_init_data rk2818_lp8725_buck1_v2_data = {
	.constraints = {
		.name = "VDD12_V2",
		.min_uV = 800000,
		.max_uV = 1500000,
		.apply_uV = 1,
		//.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_MODE,
	},
	.num_consumer_supplies = ARRAY_SIZE(buck1_v2_consumers),
	.consumer_supplies = buck1_v2_consumers
};

/*buck2_v2 VDDDR MobileDDR VCC*/
static struct regulator_consumer_supply buck2_v2_consumers[] = {
	{
		.supply = "vccdr_v2",
	}
};

static struct regulator_init_data rk2818_lp8725_buck2_v2_data = {
	.constraints = {
		.name = "VCCDR_V2",
		.min_uV = 1800000,
		.max_uV = 1800000,
		.apply_uV = 1,
		//.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = ARRAY_SIZE(buck2_v2_consumers),
	.consumer_supplies = buck2_v2_consumers
};

struct lp8725_regulator_subdev rk2818_lp8725_regulator_subdev[] = {
	{
		.id=LP8725_LDO1,
		.initdata=&rk2818_lp8725_ldo1_data,		
	 },

	{
		.id=LP8725_LDO2,
		.initdata=&rk2818_lp8725_ldo2_data,		
	 },

	{
		.id=LP8725_LDO3,
		.initdata=&rk2818_lp8725_ldo3_data,		
	 },

	{
		.id=LP8725_LDO4,
		.initdata=&rk2818_lp8725_ldo4_data,		
	 },

	{
		.id=LP8725_LDO5,
		.initdata=&rk2818_lp8725_ldo5_data,		
	 },

	{
		.id=LP8725_LILO1,
		.initdata=&rk2818_lp8725_lilo1_data,		
	 },

	{
		.id=LP8725_LILO2,
		.initdata=&rk2818_lp8725_lilo2_data,		
	 },

	{
		.id=LP8725_DCDC1,
		.initdata=&rk2818_lp8725_buck1_data,		
	 },

	{
		.id=LP8725_DCDC2,
		.initdata=&rk2818_lp8725_buck2_data,		
	 },
	{
		.id=LP8725_DCDC1_V2,
		.initdata=&rk2818_lp8725_buck1_v2_data,		
	 },

	{
		.id=LP8725_DCDC2_V2,
		.initdata=&rk2818_lp8725_buck2_v2_data,		
	 },
};

struct lp8725_platform_data rk2818_lp8725_data={
	.num_regulators=LP8725_NUM_REGULATORS,
	.regulators=rk2818_lp8725_regulator_subdev,
};
#endif

/*****************************************************************************************
 * gsensor devices
*****************************************************************************************/
#define GS_IRQ_PIN RK2818_PIN_PE3

struct rk2818_gs_platform_data rk2818_gs_platdata = {
	.gsensor_irq_pin = GS_IRQ_PIN,
};

/*****************************************************************************************
 * wm8994  codec
 * author: cjq@rock-chips.com
 *****************************************************************************************/
static struct wm8994_platform_data wm8994_data = {
    .mic_input = 0,
    .micBase_vcc = 0,
    .bb_input = 0, 
    .bb_output = 0,
    .frequence = 0,
    .enable_pin = 0,
    .headset_pin = 0,
    .headset_call_vol = 0,
    .speaker_call_vol = 0,
    .earpiece_call_vol = 0,
    .bt_call_vol = 0,
};// must initialize 

/*****************************************************************************************
 * i2c devices
 * author: kfx@rock-chips.com
*****************************************************************************************/
static void rk2818_i2c0_io_init(void)
{
	rk2818_mux_api_set(GPIOE_I2C0_SEL_NAME, IOMUXA_I2C0);
}

static void rk2818_i2c1_io_init(void)
{
	rk2818_mux_api_set(GPIOE_U1IR_I2C1_NAME, IOMUXA_I2C1);
}

struct rk2818_i2c_platform_data default_i2c0_data = { 
	.bus_num    = 0,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode       = I2C_MODE_IRQ, //I2C_MODE_POLL
	.io_init = rk2818_i2c0_io_init,
};
struct rk2818_i2c_platform_data default_i2c1_data = { 
#ifdef CONFIG_I2C0_RK2818
	.bus_num    = 1,
#else
	.bus_num	= 0,
#endif
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode       = I2C_MODE_IRQ, //I2C_MODE_POLL
	.io_init = rk2818_i2c1_io_init,
};

struct rk2818_i2c_spi_data default_i2c2_data = { 
	.bus_num    = 2,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	
};
struct rk2818_i2c_spi_data default_i2c3_data = { 

	.bus_num    = 3,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	
};
static struct i2c_board_info __initdata board_i2c0_devices[] = {
#if defined (CONFIG_RK1000_CONTROL)
	{
		.type    		= "rk1000_control",
		.addr           = 0x40,
		.flags			= 0,
	},
#endif

#if defined (CONFIG_RK1000_TVOUT)
	{
		.type    		= "rk1000_tvout",
		.addr           = 0x42,
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
#if defined (CONFIG_SND_SOC_WM8988)
	{
		.type    		= "wm8988",
		.addr           = 0x1a,
		.flags			= 0,
	}
#endif	
};
static struct i2c_board_info __initdata board_i2c1_devices[] = {
#if defined (CONFIG_RTC_HYM8563)
	{
		.type    		= "rtc_hym8563",
		.addr           = 0x51,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_RTC_DRV_S35392A)
	{
		.type    		= "rtc-s35392a",
		.addr           = 0x30,
		.flags			= 0,
	},
#endif
#if defined (CONFIG_FM_QN8006)
	{
		.type    		= "fm_qn8006",
		.addr           = 0x2b, 
		.flags			= 0,
	},
#endif
#if defined (CONFIG_GPIO_PCA9554)
	{
		.type    		= "extend_gpio_pca9554",
		.addr           = 0x3c, 
		.flags			= 0,
		.platform_data=&rk2818_pca9554_data.gpio_base,
	},
#endif
#if defined (CONFIG_RK2818_REGULATOR_LP8725)
	{
		.type    		= "lp8725",
		.addr           = 0x79, 
		.flags			= 0,
		.platform_data=&rk2818_lp8725_data,
	},
#endif
#if defined (CONFIG_GS_MMA7660)
    {
        .type           = "gs_mma7660",
        .addr           = 0x4c,
        .flags          = 0,
        .irq            = GS_IRQ_PIN,
		.platform_data = &rk2818_gs_platdata,
    },
#endif
	{},
};

static struct i2c_board_info __initdata board_i2c2_devices[] = {

};

static struct i2c_board_info __initdata board_i2c3_devices[] = {
#if defined (CONFIG_SND_SOC_WM8994)
	{
		.type    		= "wm8994",
		.addr           = 0x1a,
		.flags			= 0,
		.platform_data  = &wm8994_data,
	},
#endif
};	

/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/ 
#define RK2818_CAM_POWER_PIN    FPGA_PIO1_05//SPI_GPIO_P1_05
#define RK2818_CAM_RESET_PIN    FPGA_PIO1_14//SPI_GPIO_P1_14    

static int rk28_sensor_io_init(void);
static int rk28_sensor_io_deinit(void);

struct rk28camera_platform_data rk28_camera_platform_data = {
    .io_init = rk28_sensor_io_init,
    .io_deinit = rk28_sensor_io_deinit,
    .gpio_res = {
        {
            .gpio_reset = RK2818_CAM_RESET_PIN,
            .gpio_power = RK2818_CAM_POWER_PIN,
            .dev_name = "ov2655"
        }, {
            .gpio_reset = INVALID_GPIO,
            .gpio_power = INVALID_GPIO,
            .dev_name = NULL
        }
    }
};

static int rk28_sensor_io_init(void)
{
    int ret = 0, i;
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;

    printk("\n%s....%d    ******** ddl *********\n",__FUNCTION__,__LINE__);
    
    for (i=0; i<2; i++) { 
        camera_reset = rk28_camera_platform_data.gpio_res[i].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[i].gpio_power; 
        
        if (camera_power != INVALID_GPIO) {            
            ret = gpio_request(camera_power, "camera power");
            if (ret)
                continue;
                
            gpio_set_value(camera_reset, 1);
            gpio_direction_output(camera_power, 0);   
        }
        
        if (camera_reset != INVALID_GPIO) {
            ret = gpio_request(camera_reset, "camera reset");
            if (ret) {
                if (camera_power != INVALID_GPIO) 
                    gpio_free(camera_power);    

                continue;
            }

            gpio_set_value(camera_reset, 0);
            gpio_direction_output(camera_reset, 0);            
        }
    }
    
    return 0;
}

static int rk28_sensor_io_deinit(void)
{
    unsigned int i;
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;

    printk("\n%s....%d    ******** ddl *********\n",__FUNCTION__,__LINE__); 
   
    for (i=0; i<2; i++) { 
        camera_reset = rk28_camera_platform_data.gpio_res[i].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[i].gpio_power; 
        
        if (camera_power != INVALID_GPIO){
            gpio_direction_input(camera_power);
            gpio_free(camera_power);    
        }
        
        if (camera_reset != INVALID_GPIO)  {
            gpio_direction_input(camera_reset);
            gpio_free(camera_reset);    
        }
    }

    return 0;
}


static int rk28_sensor_power(struct device *dev, int on)
{
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;
    
    if(rk28_camera_platform_data.gpio_res[0].dev_name &&  (strcmp(rk28_camera_platform_data.gpio_res[0].dev_name, dev_name(dev)) == 0)) {
        camera_reset = rk28_camera_platform_data.gpio_res[0].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[0].gpio_power;
    } else if (rk28_camera_platform_data.gpio_res[1].dev_name && (strcmp(rk28_camera_platform_data.gpio_res[1].dev_name, dev_name(dev)) == 0)) {
        camera_reset = rk28_camera_platform_data.gpio_res[1].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[1].gpio_power;
    }

    if (camera_reset != INVALID_GPIO) {
        gpio_set_value(camera_reset, !on);
        //printk("\n%s..%s..ResetPin=%d ..PinLevel = %x   ******** ddl *********\n",__FUNCTION__,dev_name(dev),camera_reset, on);
    }
    if (camera_power != INVALID_GPIO)  {       
        gpio_set_value(camera_power, !on);
        printk("\n%s..%s..PowerPin=%d ..PinLevel = %x   ******** ddl *********\n",__FUNCTION__,dev_name(dev), camera_power, !on);
    }
    if (camera_reset != INVALID_GPIO) {
        msleep(3);          /* delay 3 ms */
        gpio_set_value(camera_reset,on);
        printk("\n%s..%s..ResetPin= %d..PinLevel = %x   ******** ddl *********\n",__FUNCTION__,dev_name(dev), camera_reset, on);
    }      
    return 0;
}

 
#define OV2655_IIC_ADDR 	    0x60
static struct i2c_board_info rk2818_i2c_cam_info[] = {
#ifdef CONFIG_SOC_CAMERA_OV2655
	{
		I2C_BOARD_INFO("ov2655", OV2655_IIC_ADDR>>1)
	},
#endif	
};

struct soc_camera_link rk2818_iclink = {
	.bus_id		= RK28_CAM_PLATFORM_DEV_ID,
	.power		= rk28_sensor_power,
	.board_info	= &rk2818_i2c_cam_info[0],
	.i2c_adapter_id	= 2,
#ifdef CONFIG_SOC_CAMERA_OV2655	
	.module_name	= "ov2655",
#endif	
};

/*****************************************************************************************
 * battery  devices
 * author: lw@rock-chips.com
 *****************************************************************************************/
#define CHARGEOK_PIN	SPI_GPIO_P6_06//RK2818_PIN_PB1
struct rk2818_battery_platform_data rk2818_battery_platdata = {
	.charge_ok_pin = CHARGEOK_PIN,
};


/*****************************************************************************************
 * serial devices
 * author: lhh@rock-chips.com
 *****************************************************************************************/
static int serial_io_init(void)
{
	int ret;
#if 1   
	//cz@rock-chips.com
	//20100808 
	//UART0的四个管脚先IOMUX成GPIO
	//然后分别设置输入输出/拉高拉低处理
	//最后再IOMUX成UART
	//防止直接IOMUX成UART后四个管脚的状态不对时
	//操作UART导致UART_USR_BUSY始终为1造成如下死循环
	//while(rk2818_uart_read(port,UART_USR)&UART_USR_BUSY)
	//UART四个管脚在未传输时正常状态应该为：
	//RX/TX：HIGH
	//CTS/RTS：LOW
	//注意：CTS/RTS为低有效，硬件上不应该强行做上拉
		rk2818_mux_api_set(GPIOG1_UART0_MMC1WPT_NAME, IOMUXA_GPIO1_C1 /*IOMUXA_UART0_SOUT*/);  
		rk2818_mux_api_set(GPIOG0_UART0_MMC1DET_NAME, IOMUXA_GPIO1_C0 /*IOMUXA_UART0_SIN*/);
		
		ret = gpio_request(RK2818_PIN_PG0, NULL); 
		if(ret != 0)
		{
		  gpio_free(RK2818_PIN_PG0);
		}
		gpio_direction_output(RK2818_PIN_PG0,GPIO_HIGH); 
	
		
		ret = gpio_request(RK2818_PIN_PG1, NULL); 
		if(ret != 0)
		{
		  gpio_free(RK2818_PIN_PG1);
		}
		gpio_direction_output(RK2818_PIN_PG1,GPIO_HIGH); 
	
		gpio_pull_updown(RK2818_PIN_PG1,GPIOPullUp);
		gpio_pull_updown(RK2818_PIN_PG0,GPIOPullUp);
	
		rk2818_mux_api_set(GPIOG1_UART0_MMC1WPT_NAME, IOMUXA_UART0_SOUT);  
		rk2818_mux_api_set(GPIOG0_UART0_MMC1DET_NAME, IOMUXA_UART0_SIN);
	
		rk2818_mux_api_set(GPIOB2_U0CTSN_SEL_NAME, IOMUXB_GPIO0_B2/*IOMUXB_UART0_CTS_N*/);
		rk2818_mux_api_set(GPIOB3_U0RTSN_SEL_NAME, IOMUXB_GPIO0_B3/*IOMUXB_UART0_RTS_N*/);
	
		ret = gpio_request(RK2818_PIN_PB2, NULL); 
		if(ret != 0)
		{
		  gpio_free(RK2818_PIN_PB2);
		}
		gpio_direction_input(RK2818_PIN_PB2); 
	//	  gpio_direction_output(RK2818_PIN_PB2,GPIO_LOW); 
		
		ret = gpio_request(RK2818_PIN_PB3, NULL); 
		if(ret != 0)
		{
		  gpio_free(RK2818_PIN_PB3);
		}
		gpio_direction_output(RK2818_PIN_PB3,GPIO_LOW); 
#endif

	rk2818_mux_api_set(GPIOB2_U0CTSN_SEL_NAME, IOMUXB_UART0_CTS_N);
	rk2818_mux_api_set(GPIOB3_U0RTSN_SEL_NAME, IOMUXB_UART0_RTS_N);

	return 0;
}

struct rk2818_serial_platform_data rk2818_serial0_platdata = {
	.io_init = serial_io_init,
};

/*****************************************************************************************
 * i2s devices
 * author: lhhrock-chips.com
 *****************************************************************************************/
static int i2s_io_init(void)
{
    /* Configure the I2S pins in correct mode */
    rk2818_mux_api_set(CXGPIO_I2S_SEL_NAME,IOMUXB_I2S_INTERFACE);
	return 0;
}

struct rk2818_i2s_platform_data rk2818_i2s_platdata = {
	.io_init = i2s_io_init,
};


/*****************************************************************************************
 * spi devices
 * author: lhhrock-chips.com
 *****************************************************************************************/
#define SPI_CHIPSELECT_NUM 3
struct spi_cs_gpio rk2818_spi_cs_gpios[SPI_CHIPSELECT_NUM] = {
	{
		.name = "spi cs0",
		.cs_gpio = RK2818_PIN_PB4,
		.cs_iomux_name = GPIOB4_SPI0CS0_MMC0D4_NAME,//if no iomux,set it NULL
		.cs_iomux_mode = IOMUXA_GPIO0_B4,
	},
	{
		.name = "spi cs1",
		.cs_gpio = RK2818_PIN_PB0,
		.cs_iomux_name = GPIOB0_SPI0CSN1_MMC1PCA_NAME,
		.cs_iomux_mode = IOMUXA_GPIO0_B0,
	},
	{
		.name = "spi cs2",
		.cs_gpio = RK2818_PIN_PF5,
		.cs_iomux_name = GPIOF5_APWM3_DPWM3_NAME,
		.cs_iomux_mode = IOMUXB_GPIO1_B5,
	}
};

static int spi_io_init(struct spi_cs_gpio *cs_gpios, int cs_num)
{	
	int i,j,ret;
	//clk
	rk2818_mux_api_set(GPIOB_SPI0_MMC0_NAME, IOMUXA_SPI0);
	//cs
	if (cs_gpios) {
		for (i=0; i<cs_num; i++) {
			rk2818_mux_api_set(cs_gpios[i].cs_iomux_name, cs_gpios[i].cs_iomux_mode);
			ret = gpio_request(cs_gpios[i].cs_gpio, cs_gpios[i].name);
			if (ret) {
				for (j=0;j<i;j++) {
					gpio_free(cs_gpios[j].cs_gpio);
					rk2818_mux_api_mode_resume(cs_gpios[j].cs_iomux_name);
				}
				printk("[fun:%s, line:%d], gpio request err\n", __func__, __LINE__);
				return -1;
			}
		}
	}
	return 0;
}

static int spi_io_deinit(struct spi_cs_gpio *cs_gpios, int cs_num)
{
	int i;
	rk2818_mux_api_mode_resume(GPIOB_SPI0_MMC0_NAME);	
	
	if (cs_gpios) {
		for (i=0; i<cs_num; i++) {
			gpio_free(cs_gpios[i].cs_gpio);
			rk2818_mux_api_mode_resume(cs_gpios[i].cs_iomux_name);
		}
	}
	
	return 0;
}

struct rk2818_spi_platform_data rk2818_spi_platdata = {
	.num_chipselect = SPI_CHIPSELECT_NUM,//raho 大板需要支持3个片选 dxj
	.chipselect_gpios = rk2818_spi_cs_gpios,
	.io_init = spi_io_init,
	.io_deinit = spi_io_deinit,
};


/*****************************************************************************************
 * xpt2046 touch panel
 * author: dxjrock-chips.com
 *****************************************************************************************/
#define XPT2046_GPIO_INT           RK2818_PIN_PE1
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
#if defined(CONFIG_SPI_FPGA)
	{	/* fpga ice65l08xx */
		.modalias	= "spi_fpga",
		.chip_select	= 1,
		.max_speed_hz	= 18 * 1000 * 1000,
		.bus_num	= 0,
		.mode	= SPI_MODE_0,
		//.platform_data = &rk2818_spi_platdata,
	},
#endif
#if defined(CONFIG_ENC28J60)	
	{	/* net chip */
		.modalias	= "enc28j60",
		.chip_select	= 1,
		.max_speed_hz	= 12 * 1000 * 1000,
		.bus_num	= 0,
		.mode	= SPI_MODE_0,
	},
#endif	
#if defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_SPI) || defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_CBN_SPI)\
    ||defined(CONFIG_TOUCHSCREEN_XPT2046_SPI) || defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI)
	{
		.modalias	= "xpt2046_ts",
		.chip_select	= 2,
		.max_speed_hz	= 125 * 1000 * 26,/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.irq = XPT2046_GPIO_INT,
		.platform_data = &xpt2046_info,
	},
#endif
}; 

/*rk2818_fb gpio information*/
static struct rk2818_fb_gpio rk2818_fb_gpio_info = {
    .display_on = (GPIO_LOW<<16)|RK2818_PIN_PA2,
    .lcd_standby = 0,
    .mcu_fmk_pin = 0,
};

/*rk2818_fb iomux information*/
static struct rk2818_fb_iomux rk2818_fb_iomux_info = {
    .data16     = GPIOC_LCDC16BIT_SEL_NAME,
    .data18     = GPIOC_LCDC18BIT_SEL_NAME,
    .data24     = GPIOC_LCDC24BIT_SEL_NAME,
    .den        = CXGPIO_LCDDEN_SEL_NAME,
    .vsync      = CXGPIO_LCDVSYNC_SEL_NAME,
    .mcu_fmk    = 0,
};
/*rk2818_fb*/
struct rk2818_fb_mach_info rk2818_fb_mach_info = {
    .gpio = &rk2818_fb_gpio_info,
    .iomux = &rk2818_fb_iomux_info,
};

void lcd_set_iomux(u8 enable)
{
    int ret=-1;

    if(enable)
    {
        rk2818_mux_api_set(GPIOH6_IQ_SEL_NAME, 0);
        ret = gpio_request(RK2818_PIN_PH6, NULL); 
        if(ret != 0)
        {
            gpio_free(RK2818_PIN_PH6);
            printk(">>>>>> lcd cs gpio_request err \n ");           
            goto pin_err;
        }  
        rk2818_mux_api_set(GPIOE_I2C0_SEL_NAME, 1);    

        ret = gpio_request(RK2818_PIN_PE4, NULL); 
        if(ret != 0)
        {
            gpio_free(RK2818_PIN_PE4);
            printk(">>>>>> lcd clk gpio_request err \n "); 
            goto pin_err;
        }  
        ret = gpio_request(RK2818_PIN_PE5, NULL); 
        if(ret != 0)
        {
            gpio_free(RK2818_PIN_PE5);
            printk(">>>>>> lcd txd gpio_request err \n "); 
            goto pin_err;
        }        
    }
    else
    {
         gpio_free(RK2818_PIN_PH6);
         rk2818_mux_api_mode_resume(GPIOH6_IQ_SEL_NAME);

         gpio_free(RK2818_PIN_PE4);   
         gpio_free(RK2818_PIN_PE5); 
         rk2818_mux_api_mode_resume(GPIOE_I2C0_SEL_NAME);
    }
    return ;
pin_err:
    return ;

}

struct lcd_td043mgea1_data lcd_td043mgea1 = {
    .pin_txd    = RK2818_PIN_PE4,
    .pin_clk    = RK2818_PIN_PE5,
    .pin_cs     = RK2818_PIN_PH6,
    .screen_set_iomux = lcd_set_iomux,
};

/*****************************************************************************************
 * backlight  devices
 * author: nzy@rock-chips.com
 *****************************************************************************************/
 /*
 GPIOF2_APWM0_SEL_NAME,       IOMUXB_PWM0
 GPIOF3_APWM1_MMC0DETN_NAME,  IOMUXA_PWM1
 GPIOF4_APWM2_MMC0WPT_NAME,   IOMUXA_PWM2
 GPIOF5_APWM3_DPWM3_NAME,     IOMUXB_PWM3
 */
 
#define PWM_ID            0  
#define PWM_MUX_NAME      GPIOF2_APWM0_SEL_NAME
#define PWM_MUX_MODE      IOMUXB_PWM0
#define PWM_EFFECT_VALUE  0


#define BL_EN_MUX_NAME    GPIOF34_UART3_SEL_NAME
#define BL_EN_MUX_MODE    IOMUXB_GPIO1_B34

#define BL_EN_PIN         RK2818_PIN_PF3
#define BL_EN_VALUE       GPIO_HIGH



static int rk2818_backlight_io_init(void)
{
    int ret = 0;
    
    rk2818_mux_api_set(PWM_MUX_NAME, PWM_MUX_MODE);

    rk2818_mux_api_set(BL_EN_MUX_NAME, BL_EN_MUX_MODE); 

    ret = gpio_request(BL_EN_PIN, NULL); 
    if(ret != 0)
    {
        gpio_free(BL_EN_PIN);
        printk(KERN_ERR ">>>>>> lcd_cs gpio_request err \n ");        
    }
    
    gpio_direction_output(BL_EN_PIN, 0);
    gpio_set_value(BL_EN_PIN, BL_EN_VALUE);

    return ret;
}

static int rk2818_backlight_io_deinit(void)
{
    int ret = 0;
    
    gpio_free(BL_EN_PIN);
    
    rk2818_mux_api_mode_resume(PWM_MUX_NAME);

    rk2818_mux_api_mode_resume(BL_EN_MUX_NAME);

    return ret;
}

struct rk2818_bl_info rk2818_bl_info = {
    .pwm_id   = PWM_ID,
    .bl_ref   = PWM_EFFECT_VALUE,
    .io_init   = rk2818_backlight_io_init,
    .io_deinit = rk2818_backlight_io_deinit, 
};


/*****************************************************************************************
 * netcard  devices
 * author: lyx@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_DM9000
/*
GPIOA5_FLASHCS1_SEL_NAME     IOMUXB_FLASH_CS1
GPIOA6_FLASHCS2_SEL_NAME     IOMUXB_FLASH_CS2
GPIOA7_FLASHCS3_SEL_NAME     IOMUXB_FLASH_CS3
GPIOE_SPI1_FLASH_SEL1_NAME   IOMUXA_FLASH_CS45
GPIOE_SPI1_FLASH_SEL_NAME    IOMUXA_FLASH_CS67
*/
#define DM9000_USE_NAND_CS 1     //cs can be 1,2,3,4,5,6 or 7
#define DM9000_CS_IOMUX_NAME GPIOA5_FLASHCS1_SEL_NAME
#define DM9000_CS_IOMUX_MODE IOMUXB_FLASH_CS1
#define DM9000_NET_INT_PIN RK2818_PIN_PA1
#define DM9000_INT_IOMUX_NAME GPIOA1_HOSTDATA17_SEL_NAME
#define DM9000_INT_IOMUX_MODE IOMUXB_GPIO0_A1
#define DM9000_INT_INIT_VALUE GPIOPullDown
#define DM9000_IRQ IRQF_TRIGGER_HIGH
#define DM9000_IO_ADDR (RK2818_NANDC_PHYS + 0x800 + DM9000_USE_NAND_CS*0x100 + 0x8)
#define DM9000_DATA_ADDR (RK2818_NANDC_PHYS + 0x800 + DM9000_USE_NAND_CS*0x100 + 0x4)

static int dm9k_gpio_set(void)
{
	//cs
	rk2818_mux_api_set(DM9000_CS_IOMUX_NAME, DM9000_CS_IOMUX_MODE);
	//int
	rk2818_mux_api_set(DM9000_INT_IOMUX_NAME, DM9000_INT_IOMUX_MODE);
		
	return 0;
}
static int dm9k_gpio_free(void)
{
	rk2818_mux_api_mode_resume(DM9000_INT_IOMUX_NAME);
	rk2818_mux_api_mode_resume(DM9000_CS_IOMUX_NAME);
	return 0;
}

static struct resource dm9k_resource[] = {
	[0] = {
		.start = DM9000_IO_ADDR,    
		.end   = DM9000_IO_ADDR + 3,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DM9000_DATA_ADDR,	
		.end   = DM9000_DATA_ADDR + 3,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = DM9000_NET_INT_PIN,
		.end   = DM9000_NET_INT_PIN,
		.flags = IORESOURCE_IRQ | DM9000_IRQ,
	}

};

/* for the moment we limit ourselves to 8bit IO until some
 * better IO routines can be written and tested
*/
struct dm9000_plat_data dm9k_platdata = {	
	.flags = DM9000_PLATF_8BITONLY,
	.irq_pin = DM9000_NET_INT_PIN,
	.irq_pin_value = DM9000_INT_INIT_VALUE,
	.io_init = dm9k_gpio_set,
	.io_deinit = dm9k_gpio_free,
};

struct platform_device rk2818_device_dm9k = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm9k_resource),
	.resource	= dm9k_resource,
	.dev		= {
		.platform_data = &dm9k_platdata,
	}
};
#endif

#ifdef CONFIG_HEADSET_DET
struct rk2818_headset_data rk2818_headset_info = {
    .irq = FPGA_PIO0_00,
};

struct platform_device rk28_device_headset = {
		.name	= "rk2818_headsetdet",
		.id 	= 0,
		.dev    = {
		    .platform_data = &rk2818_headset_info,
		}
};
#endif

#ifdef CONFIG_INPUT_LPSENSOR_CM3602 
static int capella_cm3602_power(int on)
{	/* TODO eolsen Add Voltage reg control */	
    if (on) {		
    //		gpio_direction_output(MAHIMAHI_GPIO_PROXIMITY_EN, 0);
    }
    else {		
    //		gpio_direction_output(MAHIMAHI_GPIO_PROXIMITY_EN, 1);
    }	
    return 0;
}

static struct capella_cm3602_platform_data capella_cm3602_pdata = {	
	.power = capella_cm3602_power,
	//.p_out = MAHIMAHI_GPIO_PROXIMITY_INT_N
	};

struct platform_device rk2818_device_cm3605 = {	
	    .name = CAPELLA_CM3602,
		.id = -1,
		.dev = {		
		.platform_data = &capella_cm3602_pdata	
			}
	};
#endif

/*****************************************************************************************
 * nand flash devices
 * author: hxy@rock-chips.com
 *****************************************************************************************/
/*
GPIOA5_FLASHCS1_SEL_NAME,   IOMUXB_FLASH_CS1
GPIOA6_FLASHCS2_SEL_NAME,   IOMUXB_FLASH_CS2
GPIOA7_FLASHCS3_SEL_NAME,   IOMUXB_FLASH_CS3
GPIOE_SPI1_FLASH_SEL1_NAME, IOMUXA_FLASH_CS45  
GPIOE_SPI1_FLASH_SEL_NAME,  IOMUXA_FLASH_CS67  
*/

#define NAND_CS_MAX_NUM     1  /*form 0 to 8, it is 0 when no nand flash */

int rk2818_nand_io_init(void)
{
#if (NAND_CS_MAX_NUM == 2)
    rk2818_mux_api_set(GPIOA5_FLASHCS1_SEL_NAME, IOMUXB_FLASH_CS1);
#elif (NAND_CS_MAX_NUM == 3)
    rk2818_mux_api_set(GPIOA5_FLASHCS1_SEL_NAME, IOMUXB_FLASH_CS1);
    rk2818_mux_api_set(GPIOA6_FLASHCS2_SEL_NAME, IOMUXB_FLASH_CS2);
#elif (NAND_CS_MAX_NUM == 4)
    rk2818_mux_api_set(GPIOA5_FLASHCS1_SEL_NAME, IOMUXB_FLASH_CS1);
    rk2818_mux_api_set(GPIOA6_FLASHCS2_SEL_NAME, IOMUXB_FLASH_CS2);
    rk2818_mux_api_set(GPIOA7_FLASHCS3_SEL_NAME, IOMUXB_FLASH_CS3);
#elif ((NAND_CS_MAX_NUM == 5) || (NAND_CS_MAX_NUM == 6))
    rk2818_mux_api_set(GPIOA5_FLASHCS1_SEL_NAME, IOMUXB_FLASH_CS1);
    rk2818_mux_api_set(GPIOA6_FLASHCS2_SEL_NAME, IOMUXB_FLASH_CS2);
    rk2818_mux_api_set(GPIOA7_FLASHCS3_SEL_NAME, IOMUXB_FLASH_CS3);
    rk2818_mux_api_set(GPIOE_SPI1_FLASH_SEL1_NAME, IOMUXA_FLASH_CS45);  
#elif ((NAND_CS_MAX_NUM == 7) || (NAND_CS_MAX_NUM == 8))
    rk2818_mux_api_set(GPIOA5_FLASHCS1_SEL_NAME, IOMUXB_FLASH_CS1);
    rk2818_mux_api_set(GPIOA6_FLASHCS2_SEL_NAME, IOMUXB_FLASH_CS2);
    rk2818_mux_api_set(GPIOA7_FLASHCS3_SEL_NAME, IOMUXB_FLASH_CS3);
    rk2818_mux_api_set(GPIOE_SPI1_FLASH_SEL1_NAME, IOMUXA_FLASH_CS45);  
    rk2818_mux_api_set(GPIOE_SPI1_FLASH_SEL_NAME, IOMUXA_FLASH_CS67);  
#endif
    return 0;
}

struct rk2818_nand_platform_data rk2818_nand_data = {
    .width      = 1,     /* data bus width in bytes */
    .hw_ecc     = 1,     /* hw ecc 0: soft ecc */
    .num_flash    = 1,
    .io_init   = rk2818_nand_io_init,
};


/*****************************************/

static struct platform_device *devices[] __initdata = {
#ifdef CONFIG_BT
    &rk2818_device_rfkill,
#endif
	&rk2818_device_uart0,
	&rk2818_device_uart1,
#ifdef CONFIG_I2C0_RK2818
	&rk2818_device_i2c0,
#endif
#ifdef CONFIG_I2C1_RK2818
	&rk2818_device_i2c1,
#endif
#ifdef CONFIG_SDMMC0_RK2818	
	&rk2818_device_sdmmc0,
#endif
#ifdef CONFIG_SDMMC1_RK2818
	&rk2818_device_sdmmc1,
#endif
	&rk2818_device_spim,
	&rk2818_device_i2s,
#if defined(CONFIG_ANDROID_PMEM)
	&rk2818_device_pmem,
	&rk2818_device_pmem_dsp,
#endif
	&rk2818_device_adc,
	&rk2818_device_adckey,
	&rk2818_device_battery,
    &rk2818_device_fb,    
    &rk2818_device_backlight,
	&rk2818_device_dsp,

#ifdef CONFIG_VIDEO_RK2818
 	&rk2818_device_camera,      /* ddl@rock-chips.com : camera support  */
 	&rk2818_soc_camera_pdrv,
 #endif
 
#ifdef CONFIG_MTD_NAND_RK2818
	&rk2818_nand_device,
#endif
#ifdef CONFIG_DM9000
	&rk2818_device_dm9k,
#endif
#ifdef CONFIG_INPUT_LPSENSOR_CM3602 
    &rk2818_device_cm3605,
#endif
#ifdef CONFIG_HEADSET_DET
    &rk28_device_headset,
#endif
#ifdef CONFIG_DWC_OTG
	&rk2818_device_dwc_otg,
#endif
#ifdef CONFIG_RK2818_HOST11
	&rk2818_device_host11,
#endif
#ifdef CONFIG_USB_ANDROID
	&android_usb_device,
	&usb_mass_storage_device,
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
	&rk28_device_vibrator,
#endif
};

extern struct sys_timer rk2818_timer;
#define POWER_PIN	RK2818_PIN_PB1
static void rk2818_power_on(void)
{
	int ret;
	ret = gpio_request(POWER_PIN, NULL);
	if (ret) {
		printk("failed to request power_off gpio\n");
		goto err_free_gpio;
	}

	gpio_pull_updown(POWER_PIN, GPIOPullUp);
	ret = gpio_direction_output(POWER_PIN, GPIO_HIGH);
	if (ret) {
		printk("failed to set power_off gpio output\n");
		goto err_free_gpio;
	}

	gpio_set_value(POWER_PIN, 1);/*power on*/
	
err_free_gpio:
	gpio_free(POWER_PIN);
}

static void rk2818_power_off(void)
{
	printk("shut down system now ...\n");
	gpio_set_value(POWER_PIN, 0);/*power down*/
}

//	adc	 ---> key	
#define PLAY_ON_PIN RK2818_PIN_PA3
#define PLAY_ON_LEVEL 1
static  ADC_keyst gAdcValueTab[] = 
{
	{0x5c,  AD2KEY1},///VOLUME_DOWN
	{0xbf,  AD2KEY2},///VOLUME_UP
	{0x115, AD2KEY3},///MENU
	{0x177, AD2KEY4},///HOME
	{0x1d3, AD2KEY5},///BACK
	{0x290, AD2KEY6},///CALL
	{0x230, AD2KEY7},///SEARCH
	{0,     0}///table end
};

static unsigned char gInitKeyCode[] = 
{
	AD2KEY1,AD2KEY2,AD2KEY3,AD2KEY4,AD2KEY5,AD2KEY6,AD2KEY7,
	ENDCALL,KEYSTART,KEY_WAKEUP,
};

struct adc_key_data rk2818_adc_key = {
    .pin_playon     = PLAY_ON_PIN,
    .playon_level   = PLAY_ON_LEVEL,
    .adc_empty      = 900,
    .adc_invalid    = 20,
    .adc_drift      = 50,
    .adc_chn        = 1,
    .adc_key_table  = gAdcValueTab,
    .initKeyCode    = gInitKeyCode,
    .adc_key_cnt    = 10,
};

struct rk2818_adckey_platform_data rk2818_adckey_platdata = {
	.adc_key = &rk2818_adc_key,
};

#if CONFIG_ANDROID_TIMED_GPIO
static struct timed_gpio timed_gpios[] = {
	{
		.name = "vibrator",
		.gpio = SPI_GPIO_P1_12,
		.max_timeout = 1000,
		.active_low = 1,
	},
};

struct timed_gpio_platform_data rk28_vibrator_info = {
	.num_gpios = 1,
	.gpios = timed_gpios,
};
#endif

static void __init machine_rk2818_init_irq(void)
{
	rk2818_init_irq();
	rk2818_gpio_init(rk2818_gpioBank, 8);
	rk2818_gpio_irq_setup();
}

static void __init machine_rk2818_board_init(void)
{	
	rk2818_power_on();
	pm_power_off = rk2818_power_off;
#ifdef CONFIG_I2C0_RK2818
	i2c_register_board_info(default_i2c0_data.bus_num, board_i2c0_devices,
			ARRAY_SIZE(board_i2c0_devices));
#endif
#ifdef CONFIG_I2C1_RK2818
	i2c_register_board_info(default_i2c1_data.bus_num, board_i2c1_devices,
			ARRAY_SIZE(board_i2c1_devices));
#endif
#ifdef CONFIG_SPI_FPGA_I2C
	i2c_register_board_info(default_i2c2_data.bus_num, board_i2c2_devices,
			ARRAY_SIZE(board_i2c2_devices));
	i2c_register_board_info(default_i2c3_data.bus_num, board_i2c3_devices,
			ARRAY_SIZE(board_i2c3_devices));
#endif
	platform_add_devices(devices, ARRAY_SIZE(devices));	
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
	//rk2818_mux_api_set(GPIOB4_SPI0CS0_MMC0D4_NAME,IOMUXA_GPIO0_B4); //IOMUXA_SPI0_CSN0);//use for gpio SPI CS0
	//rk2818_mux_api_set(GPIOB0_SPI0CSN1_MMC1PCA_NAME,IOMUXA_GPIO0_B0); //IOMUXA_SPI0_CSN1);//use for gpio SPI CS1
	//rk2818_mux_api_set(GPIOB_SPI0_MMC0_NAME,IOMUXA_SPI0);//use for SPI CLK SDI SDO

	//rk2818_mux_api_set(GPIOF5_APWM3_DPWM3_NAME,IOMUXB_GPIO1_B5);
	//if(0 != gpio_request(RK2818_PIN_PF5, NULL))
    //{
    //    gpio_free(RK2818_PIN_PF5);
    //    printk(">>>>>> RK2818_PIN_PF5 gpio_request err \n "); 
    //} 
}

static void __init machine_rk2818_mapio(void)
{
	iotable_init(rk2818_io_desc, ARRAY_SIZE(rk2818_io_desc));
	rk2818_clock_init();
	rk2818_iomux_init();	
}

MACHINE_START(RK2818, "RK28board")

/* UART for LL DEBUG */
	.phys_io	= 0x18002000,
	.io_pg_offst	= ((0xFF100000) >> 18) & 0xfffc,
	.boot_params	= RK2818_SDRAM_PHYS + 0xf8000,
	.map_io		= machine_rk2818_mapio,
	.init_irq	= machine_rk2818_init_irq,
	.init_machine	= machine_rk2818_board_init,
	.timer		= &rk2818_timer,
MACHINE_END

