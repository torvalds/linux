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

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <media/soc_camera.h>                               /* ddl@rock-chips.com : camera support */


#include "devices.h"

#include "../../../drivers/spi/rk2818_spim.h"

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
 void rk2818_sdmmc0_cfg_gpio(struct platform_device *dev)
{
#if 0
	rk2818_mux_api_set(GPIOF3_APWM1_MMC0DETN_NAME, IOMUXA_SDMMC1_DETECT_N);
	rk2818_mux_api_set(GPIOH_MMC0D_SEL_NAME, IOMUXA_SDMMC0_DATA123);
	rk2818_mux_api_set(GPIOH_MMC0_SEL_NAME, IOMUXA_SDMMC0_CMD_DATA0_CLKOUT);
#endif	
}

void rk2818_sdmmc1_cfg_gpio(struct platform_device *dev)
{
	rk2818_mux_api_set(GPIOG_MMC1_SEL_NAME, IOMUXA_SDMMC1_CMD_DATA0_CLKOUT);
	rk2818_mux_api_set(GPIOG_MMC1D_SEL_NAME, IOMUXA_SDMMC1_DATA123);
#if 0
	/* wifi power up (gpio control) */
	rk2818_mux_api_set(GPIOH7_HSADCCLK_SEL_NAME,IOMUXB_GPIO1_D7);
	rk2818_mux_api_set(GPIOF5_APWM3_DPWM3_NAME,IOMUXB_GPIO1_B5);
	gpio_request(RK2818_PIN_PH7, "sdio");
	gpio_direction_output(RK2818_PIN_PH7,GPIO_HIGH);
#endif

}
#define CONFIG_SDMMC0_USE_DMA
#define CONFIG_SDMMC1_USE_DMA
struct rk2818_sdmmc_platform_data default_sdmmc0_data = {
	.host_ocr_avail = (MMC_VDD_27_28|MMC_VDD_28_29|MMC_VDD_29_30|
					   MMC_VDD_30_31|MMC_VDD_31_32|MMC_VDD_32_33| 
					   MMC_VDD_33_34|MMC_VDD_34_35| MMC_VDD_35_36),
	.host_caps 	= (MMC_CAP_4_BIT_DATA|MMC_CAP_MMC_HIGHSPEED|MMC_CAP_SD_HIGHSPEED),
	.cfg_gpio = rk2818_sdmmc0_cfg_gpio,
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
	.cfg_gpio = rk2818_sdmmc1_cfg_gpio,
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
	.gpio_base=GPIOS_EXPANDER_BASE,
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
 * I2C devices
 *author: kfx
*****************************************************************************************/
 void rk2818_i2c0_cfg_gpio(struct platform_device *dev)
{
	rk2818_mux_api_set(GPIOE_I2C0_SEL_NAME, IOMUXA_I2C0);
}

void rk2818_i2c1_cfg_gpio(struct platform_device *dev)
{
	rk2818_mux_api_set(GPIOE_U1IR_I2C1_NAME, IOMUXA_I2C1);
}
struct rk2818_i2c_platform_data default_i2c0_data = { 
	.bus_num    = 0,
	.flags      = 0,
	.slave_addr = 0xff,
	.scl_rate  = 400*1000,
	.cfg_gpio = rk2818_i2c0_cfg_gpio,
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
	.cfg_gpio = rk2818_i2c1_cfg_gpio,
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
#if defined (CONFIG_PMIC_LP8725)
	{
		.type    		= "lp8725",
		.addr           = 0x79, 
		.flags			= 0,
	},
#endif
#if defined (CONFIG_GS_MMA7660)
    {
        .type           = "gs_mma7660",
        .addr           = 0x4c,
        .flags          = 0,
        .irq            = RK2818_PIN_PE3,
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
	},
#endif
};	

static void spi_xpt2046_cs_control(u32 command)
{
	if(command == 3)	
	    {
	    gpio_direction_output(RK2818_PIN_PF5, GPIO_LOW);
	    }
	if(command == 0)
	    {
	    gpio_direction_output(RK2818_PIN_PF5, GPIO_HIGH);
	    }
}

struct rk2818_spi_chip spi_xpt2046_info = {
	.cs_control = spi_xpt2046_cs_control,
};


/*****************************************************************************************
 * camera  devices
 *author: ddl@rock-chips.com
 *****************************************************************************************/
#if 1
 
#define RK2818_CAM_POWER_PIN    FPGA_PIN_PC5//SPI_GPIO_P1_05
#define RK2818_CAM_RESET_PIN    FPGA_PIN_PD6//SPI_GPIO_P1_14    

static int rk28_sensor_init(void);
static int rk28_sensor_deinit(void);

struct rk28camera_platform_data rk28_camera_platform_data = {
    .init = rk28_sensor_init,
    .deinit = rk28_sensor_deinit,
    .gpio_res = {
        {
            .gpio_reset = RK2818_CAM_RESET_PIN,
            .gpio_power = RK2818_CAM_POWER_PIN,
            .dev_name = "ov2655"
        }, {
            .gpio_reset = 0xFFFFFFFF,
            .gpio_power = 0xFFFFFFFF,
            .dev_name = NULL
        }
    }
};

static int rk28_sensor_init(void)
{
    int ret = 0, i;
    unsigned int camera_reset=0xffffffff,camera_power=0xffffffff;

    printk("\n%s....%d    ******** ddl *********\n",__FUNCTION__,__LINE__);
    
    for (i=0; i<2; i++) { 
        camera_reset = rk28_camera_platform_data.gpio_res[i].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[i].gpio_power; 
        
        if (camera_power != 0xffffffff) {            
            ret = gpio_request(camera_power, "camera power");
            if (ret)
                continue;
                
            gpio_set_value(camera_reset, 1);
            gpio_direction_output(camera_power, 0);   
        }
        
        if (camera_reset != 0xffffffff) {
            ret = gpio_request(camera_reset, "camera reset");
            if (ret) {
                if (camera_power != 0xffffffff) 
                    gpio_free(camera_power);    

                continue;
            }

            gpio_set_value(camera_reset, 0);
            gpio_direction_output(camera_reset, 0);            
        }
    }
    
    return 0;
}

static int rk28_sensor_deinit(void)
{
    unsigned int i;
    unsigned int camera_reset=0xffffffff,camera_power=0xffffffff;

    printk("\n%s....%d    ******** ddl *********\n",__FUNCTION__,__LINE__); 
   
    for (i=0; i<2; i++) { 
        camera_reset = rk28_camera_platform_data.gpio_res[i].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[i].gpio_power; 
        
        if (camera_power != 0xffffffff){
            gpio_direction_input(camera_power);
            gpio_free(camera_power);    
        }
        
        if (camera_reset != 0xffffffff)  {
            gpio_direction_input(camera_reset);
            gpio_free(camera_reset);    
        }
    }

    return 0;
}


static int rk28_sensor_power(struct device *dev, int on)
{
    unsigned int camera_reset=0xffffffff,camera_power=0xffffffff;
    
    if(rk28_camera_platform_data.gpio_res[0].dev_name &&  (strcmp(rk28_camera_platform_data.gpio_res[0].dev_name, dev_name(dev)) == 0)) {
        camera_reset = rk28_camera_platform_data.gpio_res[0].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[0].gpio_power;
    } else if (rk28_camera_platform_data.gpio_res[1].dev_name && (strcmp(rk28_camera_platform_data.gpio_res[1].dev_name, dev_name(dev)) == 0)) {
        camera_reset = rk28_camera_platform_data.gpio_res[1].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[1].gpio_power;
    }

    if (camera_reset != 0xffffffff) {
        gpio_set_value(camera_reset, !on);
        //printk("\n%s..%s..ResetPin=%d ..PinLevel = %x   ******** ddl *********\n",__FUNCTION__,dev_name(dev),camera_reset, on);
    }
    if (camera_power != 0xffffffff)  {       
        gpio_set_value(camera_power, !on);
        printk("\n%s..%s..PowerPin=%d ..PinLevel = %x   ******** ddl *********\n",__FUNCTION__,dev_name(dev), camera_power, !on);
    }
    if (camera_reset != 0xffffffff) {
        msleep(3);          /* delay 3 ms */
        gpio_set_value(camera_reset,on);
        printk("\n%s..%s..ResetPin= %d..PinLevel = %x   ******** ddl *********\n",__FUNCTION__,dev_name(dev), camera_reset, on);
    }      
    return 0;
}
#else

 /*****************************************************************************************
 * camera  devices
 *author: ddl@rock-chips.com
 *****************************************************************************************/
#define RK2818_CAM_POWER_PIN    SPI_GPIO_P1_05
#define RK2818_CAM_RESET_PIN    SPI_GPIO_P1_14    

static int rk28_sensor_init(void);
static int rk28_sensor_deinit(void);

struct rk28camera_platform_data rk28_camera_platform_data = {
    .init = rk28_sensor_init,
    .deinit = rk28_sensor_deinit,
    .gpio_res = {
        {
            .gpio_reset = RK2818_CAM_RESET_PIN,
            .gpio_power = RK2818_CAM_POWER_PIN,
            .gpio_flag = 0x03,
            .dev_name = "ov2655"
        }, {
            .gpio_reset = 0xFFFFFFFF,
            .gpio_power = 0xFFFFFFFF,
            .gpio_flag = 0x00,
            .dev_name = NULL
        }
    }
};

static int rk28_sensor_init(void)
{
    int ret = 0, i;
    unsigned int camera_reset=0xffffffff,camera_power=0xffffffff, camera_ioflag;

    printk("\n%s....%d    ******** ddl *********\n",__FUNCTION__,__LINE__);
    
    for (i=0; i<2; i++) { 
        camera_reset = rk28_camera_platform_data.gpio_res[i].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[i].gpio_power; 
        camera_ioflag = rk28_camera_platform_data.gpio_res[i].gpio_flag;
        
        if (camera_power != 0xffffffff) {
            if (camera_ioflag & 0x02) {                 /* ddl@rock-chips.com : fpga io extern */
                spi_gpio_set_pinlevel(camera_power, SPI_GPIO_LOW);
                spi_gpio_set_pindirection(camera_power, SPI_GPIO_OUT);
            } else {
                ret = gpio_request(camera_power, "camera power");
                if (ret)
                    continue;
                    
                gpio_set_value(camera_reset, 1);
                gpio_direction_output(camera_power, 0);   
            }
        }
        
        if (camera_reset != 0xffffffff) {
            if (camera_ioflag & 0x01) {
                spi_gpio_set_pinlevel(camera_reset, SPI_GPIO_HIGH);
                spi_gpio_set_pindirection(camera_reset, SPI_GPIO_OUT);
            } else {
                ret = gpio_request(camera_reset, "camera reset");
                if (ret) {
                    if (camera_power != 0xffffffff) 
                        gpio_free(camera_power);    

                    continue;
                }
                gpio_direction_output(camera_reset, 0);
                gpio_set_value(camera_reset, 1);
            }
        }
    }
    
    return 0;
}

static int rk28_sensor_deinit(void)
{
    unsigned int i;
    unsigned int camera_reset=0xffffffff,camera_power=0xffffffff,camera_ioflag;

    printk("\n%s....%d    ******** ddl *********\n",__FUNCTION__,__LINE__); 
   
    for (i=0; i<2; i++) { 
        camera_reset = rk28_camera_platform_data.gpio_res[i].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[i].gpio_power; 
        camera_ioflag = rk28_camera_platform_data.gpio_res[i].gpio_flag;
        
        if (camera_power != 0xffffffff){
            if  ((camera_ioflag & 0x02) == 0) {
                gpio_direction_input(camera_power);
                gpio_free(camera_power);    
            } else {
                spi_gpio_set_pinlevel(camera_power, SPI_GPIO_HIGH);
                spi_gpio_set_pindirection(camera_power, SPI_GPIO_IN);
            }
        }
        
        if (camera_reset != 0xffffffff)  {
            if  ((camera_ioflag & 0x01) == 0) {
                gpio_direction_input(camera_reset);
                gpio_free(camera_reset);    
            } else {
                spi_gpio_set_pinlevel(camera_reset, SPI_GPIO_HIGH);
                spi_gpio_set_pindirection(camera_reset, SPI_GPIO_IN);
            }
        }
    }

    return 0;
}


static int rk28_sensor_power(struct device *dev, int on)
{
    unsigned int camera_reset=0xffffffff,camera_power=0xffffffff,camera_ioflag;
    
    if(rk28_camera_platform_data.gpio_res[0].dev_name &&  (strcmp(rk28_camera_platform_data.gpio_res[0].dev_name, dev_name(dev)) == 0)) {
        camera_reset = rk28_camera_platform_data.gpio_res[0].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[0].gpio_power;
        camera_ioflag = rk28_camera_platform_data.gpio_res[0].gpio_flag;           
    } else if (rk28_camera_platform_data.gpio_res[1].dev_name && (strcmp(rk28_camera_platform_data.gpio_res[1].dev_name, dev_name(dev)) == 0)) {
        camera_reset = rk28_camera_platform_data.gpio_res[1].gpio_reset;
        camera_power = rk28_camera_platform_data.gpio_res[1].gpio_power;
        camera_ioflag = rk28_camera_platform_data.gpio_res[1].gpio_flag;
    }

    if (camera_reset != 0xffffffff) {
        if (camera_ioflag & 0x01) {
            spi_gpio_set_pinlevel(camera_reset, on);
        } else {
            gpio_set_value(camera_reset, on);
        }        
        printk("\n%s..%s..ResetPin=%d ..PinLevel = %x   ******** ddl *********\n",__FUNCTION__,dev_name(dev),camera_reset, on);
    }
    if (camera_power != 0xffffffff)  {       
        if (camera_ioflag & 0x02) {
            spi_gpio_set_pinlevel(camera_power, !on);
        } else {
            gpio_set_value(camera_power, !on);
        }
        printk("\n%s..%s..PowerPin=%d ..PinLevel = %x   ******** ddl *********\n",__FUNCTION__,dev_name(dev), camera_power, !on);
    }
    if (camera_reset != 0xffffffff) {
        mdelay(3);
        if (camera_ioflag & 0x01) {
            spi_gpio_set_pinlevel(camera_reset,on);
        } else {
            gpio_set_value(camera_reset,on);
        }   
        printk("\n%s..%s..ResetPin= %d..PinLevel = %x   ******** ddl *********\n",__FUNCTION__,dev_name(dev), camera_reset, on);
    }        

    mdelay(3000);

    return 0;
}


#endif
 
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
 * SPI devices
 *author: lhh
 *****************************************************************************************/
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
		.chip_select	= 2,
		.max_speed_hz	= 125 * 1000 * 26,/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.controller_data = &spi_xpt2046_info,
		.irq = RK2818_PIN_PE1,
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

struct rk2818bl_info rk2818_bl_info = {
        .pwm_id   = 0,
        .pw_pin   = GPIO_HIGH | (RK2818_PIN_PF3<< 8) ,
        .bl_ref   = 0,
        .pw_iomux = GPIOF34_UART3_SEL_NAME,
};

static struct platform_device *devices[] __initdata = {
	&rk2818_device_uart1,
#ifdef CONFIG_DM9000
	&rk2818_device_dm9k,
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
         //rk2818_mux_api_set(GPIOH6_IQ_SEL_NAME, 1);
         rk2818_mux_api_mode_resume(GPIOH6_IQ_SEL_NAME);

         gpio_free(RK2818_PIN_PE4);   
         gpio_free(RK2818_PIN_PE5); 
         //rk2818_mux_api_set(GPIOE_I2C0_SEL_NAME, 0);
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

//	adc	 ---> key	
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
    .pin_playon     = RK2818_PIN_PA3,
    .playon_level   = 1,
    .adc_empty      = 900,
    .adc_invalid    = 20,
    .adc_drift      = 50,
    .adc_chn        = 1,
    .adc_key_table  = gAdcValueTab,
    .initKeyCode    = gInitKeyCode,
    .adc_key_cnt    = 10,
};

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
#ifdef CONFIG_SPI_I2C
	i2c_register_board_info(default_i2c2_data.bus_num, board_i2c2_devices,
			ARRAY_SIZE(board_i2c2_devices));
	i2c_register_board_info(default_i2c3_data.bus_num, board_i2c3_devices,
			ARRAY_SIZE(board_i2c3_devices));
#endif
	platform_add_devices(devices, ARRAY_SIZE(devices));	
	spi_register_board_info(board_spi_devices, ARRAY_SIZE(board_spi_devices));
	rk2818_mux_api_set(GPIOB4_SPI0CS0_MMC0D4_NAME,IOMUXA_GPIO0_B4); //IOMUXA_SPI0_CSN0);//use for gpio SPI CS0
	rk2818_mux_api_set(GPIOB0_SPI0CSN1_MMC1PCA_NAME,IOMUXA_GPIO0_B0); //IOMUXA_SPI0_CSN1);//use for gpio SPI CS1
	rk2818_mux_api_set(GPIOB_SPI0_MMC0_NAME,IOMUXA_SPI0);//use for SPI CLK SDI SDO

	rk2818_mux_api_set(GPIOF5_APWM3_DPWM3_NAME,IOMUXB_GPIO1_B5);
	if(0 != gpio_request(RK2818_PIN_PF5, NULL))
    {
        gpio_free(RK2818_PIN_PF5);
        printk(">>>>>> RK2818_PIN_PF5 gpio_request err \n "); 
    } 
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

