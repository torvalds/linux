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
#include <mach/rk2818_camera.h>                          /* ddl@rock-chips.com : camera support */
#include <mach/rk2818_nand.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/dm9000.h>

#include <media/soc_camera.h>                               /* ddl@rock-chips.com : camera support */

#include "devices.h"

#include <linux/input/matrix_keypad.h>


#include "../../../drivers/spi/rk2818_spim.h"
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
 * SDMMC devices
 *author: kfx
*****************************************************************************************/

static int rk2818_sdmmc0_io_init(void)
{
	rk2818_mux_api_set(GPIOF3_APWM1_MMC0DETN_NAME, IOMUXA_SDMMC1_DETECT_N);
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

static int info_wifi_status(struct device *dev);
static int info_wifi_status_register(void (*callback)(int card_presend, void *dev_id), void *dev_id);
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
	.status = info_wifi_status,
        .register_status_notify = info_wifi_status_register,
};

static int info_wifi_cd;   /* wifi virtual 'card detect' status */
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

static int info_wifi_status(struct device *dev)
{
	return info_wifi_cd;
}

static int info_wifi_status_register(void (*callback)(int card_present, void *dev_id), void *dev_id) 
{
	if(wifi_status_cb)
 		return -EAGAIN;
        wifi_status_cb = callback;
        wifi_status_cb_devid = dev_id;
 	return 0;
}

#define INFO_WIFI_GPIO_POWER_N  TCA6424_P25
#define INFO_WIFI_GPIO_RESET_N  TCA6424_P27 

int info_wifi_power_state = 0;
int info_bt_power_state = 0;

static int info_wifi_power(int on)
{
	pr_info("%s: %d\n", __func__, on);
	if (on){
		gpio_set_value(INFO_WIFI_GPIO_POWER_N, on);
		mdelay(100);
		pr_info("wifi turn on power\n");
	}else{
		if (!info_bt_power_state){
			gpio_set_value(INFO_WIFI_GPIO_POWER_N, on);	
			mdelay(100);
			pr_info("wifi shut off power\n");
		}else
		{
			pr_info("wifi shouldn't shut off power, bt is using it!\n"); 
		}

	}

	info_wifi_power_state = on;
        return 0;
}

static int info_wifi_reset_state;
static int info_wifi_reset(int on)
{
	pr_info("%s: %d\n", __func__, on);
	gpio_set_value(INFO_WIFI_GPIO_RESET_N, on);
	mdelay(100);
	info_wifi_reset_state = on;
	return 0;
}

static int info_wifi_set_carddetect(int val)
{
	pr_info("%s:%d\n", __func__, val);	
	info_wifi_cd = val;
	if (wifi_status_cb){
		wifi_status_cb(val, wifi_status_cb_devid); 
	}else {
		pr_warning("%s, nobody to notify\n", __func__);	
	}
	return 0;
}

static struct wifi_platform_data info_wifi_control = {
	.set_power = info_wifi_power,
        .set_reset = info_wifi_reset,
        .set_carddetect = info_wifi_set_carddetect,
};
static struct platform_device info_wifi_device = {
	.name = "bcm4329_wlan",
        .id = 1,
	.dev = {
		.platform_data = &info_wifi_control,
         },
};

/* bluetooth rfkill device */
static struct platform_device info_rfkill = {
	.name = "info_rfkill",
	.id = -1,
};
 
/*****************************************************************************************
 * extern gpio devices
 *author: xxx
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
};
#endif

#if defined (CONFIG_IOEXTEND_TCA6424)
struct rk2818_gpio_expander_info  extgpio_tca6424_settinginfo[] = {

	{
		.gpio_num    		= TCA6424_P01,
		.pin_type           = GPIO_OUT,
		.pin_value		= GPIO_LOW,
	},

	{
		.gpio_num    		= TCA6424_P02,// tp3
		.pin_type           = GPIO_OUT,
		.pin_value			= GPIO_LOW,
	 },
	 {
		.gpio_num    		= TCA6424_P03,
		.pin_type           = GPIO_OUT,
		.pin_value			= GPIO_LOW,
	 },

	{
		.gpio_num    		= TCA6424_P04,// tp3
		.pin_type           = GPIO_OUT,
		.pin_value			= GPIO_LOW,
	 },
	 {
		.gpio_num    		= TCA6424_P05,
		.pin_type           = GPIO_OUT,
		.pin_value			= GPIO_LOW,
	 }, 
	 {
		.gpio_num    		= TCA6424_P11,
		.pin_type           = GPIO_OUT,
		.pin_value			= GPIO_HIGH,
	 }, 
	 {
		.gpio_num    		= TCA6424_P12,
		.pin_type           = GPIO_IN,
		//.pin_value			=GPIO_HIGH,
	 },

	{
		.gpio_num    		= TCA6424_P13,// tp3
		.pin_type           = GPIO_IN,
		//.pin_value			=GPIO_HIGH,
	 },
	 {
		.gpio_num    		= TCA6424_P14,
		.pin_type           = GPIO_IN,
		//.pin_value			=GPIO_HIGH,
	 },

	 {
		.gpio_num    		= TCA6424_P15,// tp3
		.pin_type           = GPIO_IN,
		//.pin_value			=GPIO_HIGH,
	 },
	 {
		.gpio_num    		= TCA6424_P17,// 3G PowerOn
		.pin_type           	= GPIO_OUT,
		.pin_value		=GPIO_HIGH,
	 },
         {
                .gpio_num               = TCA6424_P25,  //wifi reg on
                .pin_type               = GPIO_OUT,
                .pin_value              = GPIO_LOW,
	},
	{
                .gpio_num               = TCA6424_P27,  //wifi reset
                .pin_type               = GPIO_OUT,
                .pin_value              = GPIO_LOW,
	},
};

void tca6424_reset_itr(void)
{
		rk2818_mux_api_set(GPIOE_U1IR_I2C1_NAME, IOMUXA_GPIO1_A67);
		gpio_request(RK2818_PIN_PE6,NULL);
		gpio_request(RK2818_PIN_PE7,NULL);

		gpio_direction_output(RK2818_PIN_PE6,GPIO_HIGH);
		gpio_direction_output(RK2818_PIN_PE7,GPIO_LOW);
		udelay(3);
		gpio_set_value(RK2818_PIN_PE7,GPIO_HIGH);
		udelay(1);
		
		gpio_free(RK2818_PIN_PE6);
		gpio_free(RK2818_PIN_PE7);
		rk2818_mux_api_set(GPIOE_U1IR_I2C1_NAME, IOMUXA_I2C1);
}

struct tca6424_platform_data rk2818_tca6424_data={
	.gpio_base=GPIO_EXPANDER_BASE,
	.gpio_pin_num=CONFIG_EXPANDED_GPIO_NUM,
	.gpio_irq_start=NR_AIC_IRQS + 2*NUM_GROUP + CONFIG_SPI_FPGA_GPIO_IRQ_NUM,
	.irq_pin_num=CONFIG_EXPANDED_GPIO_IRQ_NUM,
	.tca6424_irq_pin=RK2818_PIN_PA1,
	.expand_port_group = 3,
	.expand_port_pinnum = 8,
	.rk_irq_mode = IRQF_TRIGGER_LOW,
	.rk_irq_gpio_pull_up_down = GPIOPullUp,
	.settinginfo=extgpio_tca6424_settinginfo,
	.settinginfolen=ARRAY_SIZE(extgpio_tca6424_settinginfo),
	.reseti2cpin = tca6424_reset_itr,
};
#endif

/*****************************************************************************************
 * gsensor devices
*****************************************************************************************/
#define GS_IRQ_PIN RK2818_PIN_PE0

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
static int rk2818_i2c0_io_init(void)
{
	rk2818_mux_api_set(GPIOE_I2C0_SEL_NAME, IOMUXA_I2C0);
	return 0;
}

static int rk2818_i2c1_io_init(void)
{
	rk2818_mux_api_set(GPIOE_U1IR_I2C1_NAME, IOMUXA_I2C1);
	return 0;
}
struct rk2818_i2c_platform_data default_i2c0_data = { 
	.bus_num    = 0,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.mode 		= I2C_MODE_IRQ,
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
	.mode 		= I2C_MODE_POLL,
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
#if defined (CONFIG_SND_SOC_WM8994)
	{
		.type    		= "wm8994",
		.addr           = 0x1a,
		.flags			= 0,
		.platform_data  = &wm8994_data,
	},
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
#if defined (CONFIG_IOEXTEND_TCA6424)
	{
		.type    		= "extend_gpio_tca6424",
		.addr           = 0x23, 
		.flags			= 0,
		.platform_data=&rk2818_tca6424_data.gpio_base,
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
	

/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_VIDEO_RK2818
#define SENSOR_NAME_0 RK28_CAM_SENSOR_NAME_OV2655
#define SENSOR_IIC_ADDR_0 	    0x60
#define SENSOR_IIC_ADAPTER_ID_0    1
#define SENSOR_POWER_PIN_0         TCA6424_P16
#define SENSOR_RESET_PIN_0         INVALID_GPIO
#define SENSOR_POWERACTIVE_LEVEL_0 RK28_CAM_POWERACTIVE_L
#define SENSOR_RESETACTIVE_LEVEL_0 RK28_CAM_RESETACTIVE_L


#define SENSOR_NAME_1 NULL
#define SENSOR_IIC_ADDR_1 	    0x00
#define SENSOR_IIC_ADAPTER_ID_1    0xff
#define SENSOR_POWER_PIN_1         INVALID_GPIO
#define SENSOR_RESET_PIN_1         INVALID_GPIO
#define SENSOR_POWERACTIVE_LEVEL_1 RK28_CAM_POWERACTIVE_L
#define SENSOR_RESETACTIVE_LEVEL_1 RK28_CAM_RESETACTIVE_L

static int rk28_sensor_io_init(void);
static int rk28_sensor_io_deinit(void);

struct rk28camera_platform_data rk28_camera_platform_data = {
    .io_init = rk28_sensor_io_init,
    .io_deinit = rk28_sensor_io_deinit,
    .gpio_res = {
        {
            .gpio_reset = SENSOR_RESET_PIN_0,
            .gpio_power = SENSOR_POWER_PIN_0,
            .gpio_flag = (SENSOR_POWERACTIVE_LEVEL_0|SENSOR_RESETACTIVE_LEVEL_0),
            .dev_name = SENSOR_NAME_0,
        }, {
            .gpio_reset = SENSOR_RESET_PIN_1,
            .gpio_power = SENSOR_POWER_PIN_1,
            .gpio_flag = (SENSOR_POWERACTIVE_LEVEL_1|SENSOR_RESETACTIVE_LEVEL_1),
            .dev_name = SENSOR_NAME_1,
        }
    }
};

static int rk28_sensor_io_init(void)
{
    int ret = 0, i;
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;
	unsigned int camera_ioflag;
    //printk("\n%s....%d    ******** ddl *********\n",__FUNCTION__,__LINE__);

    for (i=0; i<2; i++) {
        camera_reset = rk28_camera_platform_data.gpio_res[i].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[i].gpio_power;
		camera_ioflag = rk28_camera_platform_data.gpio_res[i].gpio_flag;

        if (camera_power != INVALID_GPIO) {
            ret = gpio_request(camera_power, "camera power");
            if (ret)
                continue;

            gpio_set_value(camera_reset, (((~camera_ioflag)&RK28_CAM_POWERACTIVE_MASK)>>RK28_CAM_POWERACTIVE_BITPOS));
            gpio_direction_output(camera_power, (((~camera_ioflag)&RK28_CAM_POWERACTIVE_MASK)>>RK28_CAM_POWERACTIVE_BITPOS));

			//printk("\n%s....%d  %x   ******** ddl *********\n",__FUNCTION__,__LINE__,(((~camera_ioflag)&RK28_CAM_POWERACTIVE_MASK)>>RK28_CAM_POWERACTIVE_BITPOS));

        }

        if (camera_reset != INVALID_GPIO) {
            ret = gpio_request(camera_reset, "camera reset");
            if (ret) {
                if (camera_power != INVALID_GPIO)
                    gpio_free(camera_power);

                continue;
            }

            gpio_set_value(camera_reset, ((camera_ioflag&RK28_CAM_RESETACTIVE_MASK)>>RK28_CAM_RESETACTIVE_BITPOS));
            gpio_direction_output(camera_reset, ((camera_ioflag&RK28_CAM_RESETACTIVE_MASK)>>RK28_CAM_RESETACTIVE_BITPOS));

			//printk("\n%s....%d  %x   ******** ddl *********\n",__FUNCTION__,__LINE__,((camera_ioflag&RK28_CAM_RESETACTIVE_MASK)>>RK28_CAM_RESETACTIVE_BITPOS));

        }
    }

    return 0;
}

static int rk28_sensor_io_deinit(void)
{
    unsigned int i;
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;

    //printk("\n%s....%d    ******** ddl *********\n",__FUNCTION__,__LINE__);

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
	unsigned int camera_ioflag;

    if(rk28_camera_platform_data.gpio_res[0].dev_name &&  (strcmp(rk28_camera_platform_data.gpio_res[0].dev_name, dev_name(dev)) == 0)) {
        camera_reset = rk28_camera_platform_data.gpio_res[0].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[0].gpio_power;
		camera_ioflag = rk28_camera_platform_data.gpio_res[0].gpio_flag;
    } else if (rk28_camera_platform_data.gpio_res[1].dev_name && (strcmp(rk28_camera_platform_data.gpio_res[1].dev_name, dev_name(dev)) == 0)) {
        camera_reset = rk28_camera_platform_data.gpio_res[1].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[1].gpio_power;
		camera_ioflag = rk28_camera_platform_data.gpio_res[1].gpio_flag;
    }

    if (camera_reset != INVALID_GPIO) {
        gpio_set_value(camera_reset, ((camera_ioflag&RK28_CAM_RESETACTIVE_MASK)>>RK28_CAM_RESETACTIVE_BITPOS));
        //printk("\n%s..%s..ResetPin=%d ..PinLevel = %x \n",__FUNCTION__,dev_name(dev),camera_reset, ((camera_ioflag&RK28_CAM_RESETACTIVE_MASK)>>RK28_CAM_RESETACTIVE_BITPOS));
    }
    if (camera_power != INVALID_GPIO)  {
        if (on) {
        	gpio_set_value(camera_power, ((camera_ioflag&RK28_CAM_POWERACTIVE_MASK)>>RK28_CAM_POWERACTIVE_BITPOS));
			//printk("\n%s..%s..PowerPin=%d ..PinLevel = %x   \n",__FUNCTION__,dev_name(dev), camera_power, ((camera_ioflag&RK28_CAM_POWERACTIVE_MASK)>>RK28_CAM_POWERACTIVE_BITPOS));
		} else {
			gpio_set_value(camera_power, (((~camera_ioflag)&RK28_CAM_POWERACTIVE_MASK)>>RK28_CAM_POWERACTIVE_BITPOS));
			//printk("\n%s..%s..PowerPin=%d ..PinLevel = %x   \n",__FUNCTION__,dev_name(dev), camera_power, (((~camera_ioflag)&RK28_CAM_POWERACTIVE_MASK)>>RK28_CAM_POWERACTIVE_BITPOS));
		}
	}
    if (camera_reset != INVALID_GPIO) {
        msleep(3);          /* delay 3 ms */
        gpio_set_value(camera_reset,(((~camera_ioflag)&RK28_CAM_RESETACTIVE_MASK)>>RK28_CAM_RESETACTIVE_BITPOS));
        //printk("\n%s..%s..ResetPin= %d..PinLevel = %x   \n",__FUNCTION__,dev_name(dev), camera_reset, (((~camera_ioflag)&RK28_CAM_RESETACTIVE_MASK)>>RK28_CAM_RESETACTIVE_BITPOS));
    }
    return 0;
}

static struct i2c_board_info rk2818_i2c_cam_info[] = {
	{
		I2C_BOARD_INFO(SENSOR_NAME_0, SENSOR_IIC_ADDR_0>>1)
	},
};

struct soc_camera_link rk2818_iclink = {
	.bus_id		= RK28_CAM_PLATFORM_DEV_ID,
	.power		= rk28_sensor_power,
	.board_info	= &rk2818_i2c_cam_info[0],
	.i2c_adapter_id	= SENSOR_IIC_ADAPTER_ID_0,
	.module_name	= SENSOR_NAME_0,
};

/*platform_device : soc-camera need  */
struct platform_device rk2818_soc_camera_pdrv = {
	.name	= "soc-camera-pdrv",
	.id	= -1,
	.dev	= {
		.init_name = SENSOR_NAME_0,
		.platform_data = &rk2818_iclink,
	},
};
#endif

/*****************************************************************************************
 * battery  devices
 * author: lw@rock-chips.com
 *****************************************************************************************/
#define CHARGEOK_PIN	TCA6424_P07
struct rk2818_battery_platform_data rk2818_battery_platdata = {
	.charge_ok_pin = CHARGEOK_PIN,
	.charge_ok_level = 0,
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
#define SPI_CHIPSELECT_NUM 2
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
#define XPT2046_GPIO_INT           RK2818_PIN_PE3
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
		.max_speed_hz	= 8 * 1000 * 1000,
		.bus_num	= 0,
		.mode	= SPI_MODE_0,
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
		.chip_select	= 0,
		.max_speed_hz	= 125 * 1000 * 26,/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.irq = XPT2046_GPIO_INT,
		.platform_data = &xpt2046_info,
	},
#endif
}; 

/*****************************************************************************************
 * lcd  devices
 * author: zyw@rock-chips.com
 *****************************************************************************************/
//#ifdef  CONFIG_LCD_TD043MGEA1
#define LCD_TXD_PIN          RK2818_PIN_PE6
#define LCD_CLK_PIN          RK2818_PIN_PE7
#define LCD_CS_PIN           RK2818_PIN_PH6
#define LCD_TXD_MUX_NAME     GPIOE_U1IR_I2C1_NAME
#define LCD_CLK_MUX_NAME     NULL
#define LCD_CS_MUX_NAME      GPIOH6_IQ_SEL_NAME
#define LCD_TXD_MUX_MODE     0
#define LCD_CLK_MUX_MODE     0
#define LCD_CS_MUX_MODE      0
//#endif
static int rk2818_lcd_io_init(void)
{
    int ret = 0;
    
    rk2818_mux_api_set(LCD_CS_MUX_NAME, LCD_CS_MUX_MODE);
    if (LCD_CS_PIN != INVALID_GPIO) {
        ret = gpio_request(LCD_CS_PIN, NULL); 
        if(ret != 0)
        {
            goto err1;
            printk(">>>>>> lcd cs gpio_request err \n ");                    
        } 
    }
    
    rk2818_mux_api_set(LCD_CLK_MUX_NAME, LCD_CLK_MUX_MODE);
    if (LCD_CLK_PIN != INVALID_GPIO) {
        ret = gpio_request(LCD_CLK_PIN, NULL); 
        if(ret != 0)
        {
            goto err2;
            printk(">>>>>> lcd clk gpio_request err \n ");             
        }  
    }
    
    rk2818_mux_api_set(LCD_TXD_MUX_NAME, LCD_TXD_MUX_MODE); 
    if (LCD_TXD_PIN != INVALID_GPIO) {
        ret = gpio_request(LCD_TXD_PIN, NULL); 
        if(ret != 0)
        {
            goto err3;
            printk(">>>>>> lcd txd gpio_request err \n ");             
        } 
    }

    return 0;
    
err3:
    if (LCD_CLK_PIN != INVALID_GPIO) {
        gpio_free(LCD_CLK_PIN);
    }
err2:
    if (LCD_CS_PIN != INVALID_GPIO) {
        gpio_free(LCD_CS_PIN);
    }
err1:
    return ret;
}

static int rk2818_lcd_io_deinit(void)
{
    int ret = 0;

    gpio_free(LCD_CS_PIN); 
    rk2818_mux_api_mode_resume(LCD_CS_MUX_NAME);
    gpio_free(LCD_CLK_PIN);   
    gpio_free(LCD_TXD_PIN); 
    rk2818_mux_api_mode_resume(LCD_TXD_MUX_NAME);
    rk2818_mux_api_mode_resume(LCD_CLK_MUX_NAME);
    
    return ret;
}

struct rk2818lcd_info rk2818_lcd_info = {
    .txd_pin  = LCD_TXD_PIN,
    .clk_pin = LCD_CLK_PIN,
    .cs_pin = LCD_CS_PIN,
    .io_init   = rk2818_lcd_io_init,
    .io_deinit = rk2818_lcd_io_deinit, 
};


/*****************************************************************************************
 * frame buffe  devices
 * author: zyw@rock-chips.com
 *****************************************************************************************/
#define FB_ID                       0
#define FB_DISPLAY_ON_PIN           RK2818_PIN_PB1
#define FB_LCD_STANDBY_PIN          INVALID_GPIO
#define FB_MCU_FMK_PIN              INVALID_GPIO

#define FB_DISPLAY_ON_VALUE         GPIO_LOW
#define FB_LCD_STANDBY_VALUE        0

#define FB_DISPLAY_ON_MUX_NAME      GPIOB1_SMCS1_MMC0PCA_NAME
#define FB_DISPLAY_ON_MUX_MODE      IOMUXA_GPIO0_B1

#define FB_LCD_STANDBY_MUX_NAME     NULL
#define FB_LCD_STANDBY_MUX_MODE     1

#define FB_MCU_FMK_PIN_MUX_NAME     NULL
#define FB_MCU_FMK_MUX_MODE         0

#define FB_DATA0_16_MUX_NAME       GPIOC_LCDC16BIT_SEL_NAME
#define FB_DATA0_16_MUX_MODE        1

#define FB_DATA17_18_MUX_NAME      GPIOC_LCDC18BIT_SEL_NAME
#define FB_DATA17_18_MUX_MODE       1

#define FB_DATA19_24_MUX_NAME      GPIOC_LCDC24BIT_SEL_NAME
#define FB_DATA19_24_MUX_MODE       1

#define FB_DEN_MUX_NAME            CXGPIO_LCDDEN_SEL_NAME
#define FB_DEN_MUX_MODE             1

#define FB_VSYNC_MUX_NAME          CXGPIO_LCDVSYNC_SEL_NAME
#define FB_VSYNC_MUX_MODE           1

#define FB_MCU_FMK_MUX_NAME        NULL
#define FB_MCU_FMK_MUX_MODE         0

static int rk2818_fb_io_init(struct rk2818_fb_setting_info *fb_setting)
{
    int ret = 0;
    if(fb_setting->data_num <=16)
        rk2818_mux_api_set(FB_DATA0_16_MUX_NAME, FB_DATA0_16_MUX_MODE);
    if(fb_setting->data_num >16 && fb_setting->data_num<=18)
        rk2818_mux_api_set(FB_DATA17_18_MUX_NAME, FB_DATA17_18_MUX_MODE);
    if(fb_setting->data_num >18)
        rk2818_mux_api_set(FB_DATA19_24_MUX_NAME, FB_DATA19_24_MUX_MODE);
    
    if(fb_setting->vsync_en)
        rk2818_mux_api_set(FB_VSYNC_MUX_NAME, FB_VSYNC_MUX_MODE);
    
    if(fb_setting->den_en)
        rk2818_mux_api_set(FB_DEN_MUX_NAME, FB_DEN_MUX_MODE);
    
    if(fb_setting->mcu_fmk_en && FB_MCU_FMK_MUX_NAME && (FB_MCU_FMK_PIN != INVALID_GPIO))
    {
        rk2818_mux_api_set(FB_MCU_FMK_MUX_NAME, FB_MCU_FMK_MUX_MODE);
        ret = gpio_request(FB_MCU_FMK_PIN, NULL);         
        if(ret != 0)
        {
            gpio_free(FB_MCU_FMK_PIN);
            printk(">>>>>> FB_MCU_FMK_PIN gpio_request err \n ");             
        } 
        gpio_direction_input(FB_MCU_FMK_PIN);
    }

    if(fb_setting->disp_on_en && FB_DISPLAY_ON_MUX_NAME && (FB_DISPLAY_ON_PIN != INVALID_GPIO))
    {
        rk2818_mux_api_set(FB_DISPLAY_ON_MUX_NAME, FB_DISPLAY_ON_MUX_MODE);
        ret = gpio_request(FB_DISPLAY_ON_PIN, NULL);         
        if(ret != 0)
        {
            gpio_free(FB_DISPLAY_ON_PIN);
            printk(">>>>>> FB_DISPLAY_ON_PIN gpio_request err \n ");             
        }         
    }

    if(fb_setting->disp_on_en && FB_LCD_STANDBY_MUX_NAME && (FB_LCD_STANDBY_PIN != INVALID_GPIO))
    {
        rk2818_mux_api_set(FB_LCD_STANDBY_MUX_NAME, FB_LCD_STANDBY_MUX_MODE);
        ret = gpio_request(FB_LCD_STANDBY_PIN, NULL);         
        if(ret != 0)
        {
            gpio_free(FB_LCD_STANDBY_PIN);
            printk(">>>>>> FB_LCD_STANDBY_PIN gpio_request err \n ");             
        }
    }

    return ret;
}

struct rk2818fb_info rk2818_fb_info = {
    .fb_id   = FB_ID,  
    .disp_on_pin = FB_DISPLAY_ON_PIN,
    .disp_on_value = FB_DISPLAY_ON_VALUE,
    .standby_pin = FB_LCD_STANDBY_PIN,
    .standby_value = FB_LCD_STANDBY_VALUE,
    .mcu_fmk_pin = FB_MCU_FMK_PIN,  
    .lcd_info = &rk2818_lcd_info,
    .io_init   = rk2818_fb_io_init,
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

/********************************************************
*				dm9000 net work devices
*				author:lyx
********************************************************/
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
#define DM9000_NET_INT_PIN RK2818_PIN_PA3
#define DM9000_INT_IOMUX_NAME GPIOA23_UART2_SEL_NAME
#define DM9000_INT_IOMUX_MODE IOMUXB_GPIO0_A23
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

#ifdef CONFIG_KEYBOARD_MATRIX
/*
 * InfoPhone Matrix Keyboard Device
 */
#define KEYOUT0     TCA6424_P01
#define KEYOUT1		TCA6424_P02
#define KEYOUT2		TCA6424_P03
#define KEYOUT3		TCA6424_P04
#define KEYOUT4		TCA6424_P05

#define KEYIN0		TCA6424_P12
#define KEYIN1		TCA6424_P13
#define KEYIN2		TCA6424_P14
#define KEYIN3		TCA6424_P15

static const uint32_t rk2818matrix_keymap[] = {
	KEY(0, 0, KEY_1),
	KEY(1, 0, KEY_2),
	KEY(2, 0, KEY_3),
	KEY(3, 0, KEY_TAB),
	KEY(0, 1, KEY_4),
	KEY(1, 1, KEY_5),
	KEY(2, 1, KEY_6),
	KEY(3, 1, KEY_R),
	KEY(0, 2, KEY_7),
	KEY(1, 2, KEY_8),
	KEY(2, 2, KEY_9),
	KEY(3, 2, KEY_Q),
	KEY(0, 3, KEY_E),
	KEY(1, 3, KEY_0),
	KEY(2, 3, KEY_G),
	KEY(3, 3, KEY_LEFTCTRL),
	KEY(0, 4, KEY_W),
	KEY(1, 4, KEY_S),
	KEY(2, 4, KEY_F),
	KEY(3, 4, KEY_V),
};

static struct matrix_keymap_data rk2818matrix_keymap_data = {
	.keymap		= rk2818matrix_keymap,
	.keymap_size	= ARRAY_SIZE(rk2818matrix_keymap),
};

static const int rk2818matrix_row_gpios[] =
		{ KEYIN0, KEYIN1, KEYIN2, KEYIN3 };
static const int rk2818matrix_col_gpios[] =
		{ KEYOUT0, KEYOUT1, KEYOUT2, KEYOUT3, KEYOUT4 };

static struct matrix_keypad_platform_data rk2818matrixkey_pdata = {
	.keymap_data		= &rk2818matrix_keymap_data,
	.row_gpios		= rk2818matrix_row_gpios,
	.col_gpios		= rk2818matrix_col_gpios,
	.num_row_gpios		= ARRAY_SIZE(rk2818matrix_row_gpios),
	.num_col_gpios		= ARRAY_SIZE(rk2818matrix_col_gpios),
	.col_scan_delay_us	= 100,
	.debounce_ms		= 10,
	.wakeup			= 1,
};

static struct platform_device rk2818_device_matrixkey = {
	.name		= "matrix-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &rk2818matrixkey_pdata,
	},
};
#endif /* CONFIG_KEYBOARD_MATRIX */

#ifdef CONFIG_HEADSET_DET
struct rk2818_headset_data rk2818_headset_info = {
	.irq		= TCA6424_P23,
	.irq_type	= IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING,
	.headset_in_type= HEADSET_IN_HIGH,
};

struct platform_device rk28_device_headset = {
		.name	= "rk2818_headsetdet",
		.id 	= 0,
		.dev    = {
		.platform_data = &rk2818_headset_info,
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
#ifdef CONFIG_UART0_RK2818	
	&rk2818_device_uart0,
#endif
#ifdef CONFIG_UART1_RK2818	
	&rk2818_device_uart1,
#endif	
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
	&info_wifi_device,
#endif
	&info_rfkill,
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
#ifdef CONFIG_KEYBOARD_MATRIX	
	&rk2818_device_matrixkey,
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

};

extern struct sys_timer rk2818_timer;
#define POWER_PIN	RK2818_PIN_PH7
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
#define PLAY_ON_PIN RK2818_PIN_PE1
#define PLAY_ON_LEVEL 0
static  ADC_keyst gAdcValueTab[] = 
{
	{95,  AD2KEY1},///VOLUME_DOWN
	{249, AD2KEY2},///VOLUME_UP
	{408, AD2KEY3},///MENU
	{560, AD2KEY4},///HOME
	{725, AD2KEY5},///BACK
	{816, AD2KEY6},///CALL
	{0,0}
};

static unsigned char gInitKeyCode[] = 
{
	AD2KEY1,AD2KEY2,AD2KEY3,AD2KEY4,AD2KEY5,AD2KEY6,
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
    .adc_key_cnt    = 7,
};
struct rk2818_adckey_platform_data rk2818_adckey_platdata = {
	.adc_key = &rk2818_adc_key,
};
#if CONFIG_ANDROID_TIMED_GPIO
struct timed_gpio_platform_data rk28_vibrator_info = {
	.num_gpios = 0,
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
	platform_add_devices(devices, ARRAY_SIZE(devices));	
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
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
	.boot_params	= RK2818_SDRAM_PHYS + 0x88000,
	.map_io		= machine_rk2818_mapio,
	.init_irq	= machine_rk2818_init_irq,
	.init_machine	= machine_rk2818_board_init,
	.timer		= &rk2818_timer,
MACHINE_END

