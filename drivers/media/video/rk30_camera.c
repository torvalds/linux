#include <plat/rk_camera.h> 
#include <mach/iomux.h>
#include <media/soc_camera.h>
#include <linux/android_pmem.h>
#ifndef PMEM_CAM_SIZE
#ifdef CONFIG_VIDEO_RK29 
/*---------------- Camera Sensor Fixed Macro Begin  ------------------------*/
// Below Macro is fixed, programer don't change it!!!!!!
#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

#if (CONFIG_SENSOR_IIC_ADDR_0 != 0x00)
#define PMEM_SENSOR_FULL_RESOLUTION_0  CONS(CONFIG_SENSOR_0,_FULL_RESOLUTION)
#define SENSOR_CIF_BUSID_0				CONS(RK_CAM_PLATFORM_DEV_ID_,CONFIG_SENSOR_CIF_INDEX_0)
#if !(PMEM_SENSOR_FULL_RESOLUTION_0)
#undef PMEM_SENSOR_FULL_RESOLUTION_0
#define PMEM_SENSOR_FULL_RESOLUTION_0  0x500000
#endif
#if(SENSOR_CIF_BUSID_0 == RK_CAM_PLATFORM_DEV_ID_0)
#define PMEM_SENSOR_FULL_RESOLUTION_CIF_0 PMEM_SENSOR_FULL_RESOLUTION_0
#define PMEM_SENSOR_FULL_RESOLUTION_CIF_1 0
#else
#define PMEM_SENSOR_FULL_RESOLUTION_CIF_1 PMEM_SENSOR_FULL_RESOLUTION_0
#define PMEM_SENSOR_FULL_RESOLUTION_CIF_0 0
#endif
#else
#define PMEM_SENSOR_FULL_RESOLUTION_CIF_0 0x00
#define PMEM_SENSOR_FULL_RESOLUTION_CIF_1 0x00
#endif
 
#if (CONFIG_SENSOR_IIC_ADDR_1 != 0x00)
#define PMEM_SENSOR_FULL_RESOLUTION_1  CONS(CONFIG_SENSOR_1,_FULL_RESOLUTION)
#define SENSOR_CIF_BUSID_1				CONS(RK_CAM_PLATFORM_DEV_ID_,CONFIG_SENSOR_CIF_INDEX_1)
#if !(PMEM_SENSOR_FULL_RESOLUTION_1)
#undef PMEM_SENSOR_FULL_RESOLUTION_1
#define PMEM_SENSOR_FULL_RESOLUTION_1  0x500000
#endif
#if (SENSOR_CIF_BUSID_1 == RK_CAM_PLATFORM_DEV_ID_0)
	   #if (PMEM_SENSOR_FULL_RESOLUTION_CIF_0 < PMEM_SENSOR_FULL_RESOLUTION_1)
	   #undef PMEM_SENSOR_FULL_RESOLUTION_CIF_0
	   #define PMEM_SENSOR_FULL_RESOLUTION_CIF_0 PMEM_SENSOR_FULL_RESOLUTION_1
	   #endif
#else
	   #if (PMEM_SENSOR_FULL_RESOLUTION_CIF_1 < PMEM_SENSOR_FULL_RESOLUTION_1)
	   #undef PMEM_SENSOR_FULL_RESOLUTION_CIF_1
	   #define PMEM_SENSOR_FULL_RESOLUTION_CIF_1 PMEM_SENSOR_FULL_RESOLUTION_1
	   #endif
#endif
#endif

//CIF 0
#if (PMEM_SENSOR_FULL_RESOLUTION_CIF_0 == 0x500000)
#define PMEM_CAM_NECESSARY   0x1400000       /* 1280*720*1.5*4(preview) + 7.5M(capture raw) + 4M(jpeg encode output) */
#define PMEM_CAMIPP_NECESSARY_CIF0    0x800000
#elif (PMEM_SENSOR_FULL_RESOLUTION_CIF_0 == 0x300000)
#define PMEM_CAM_NECESSARY   0xe00000        /* 1280*720*1.5*4(preview) + 4.5M(capture raw) + 3M(jpeg encode output) */
#define PMEM_CAMIPP_NECESSARY_CIF_0    0x500000
#elif (PMEM_SENSOR_FULL_RESOLUTION_CIF_0 == 0x200000) /* 1280*720*1.5*4(preview) + 3M(capture raw) + 3M(jpeg encode output) */
#define PMEM_CAM_NECESSARY   0xc00000
#define PMEM_CAMIPP_NECESSARY_CIF_0    0x400000
#elif ((PMEM_SENSOR_FULL_RESOLUTION_CIF_0 == 0x100000) || (PMEM_SENSOR_FULL_RESOLUTION_CIF_0 == 0x130000))
#define PMEM_CAM_NECESSARY   0x800000        /* 800*600*1.5*4(preview) + 2M(capture raw) + 2M(jpeg encode output) */
#define PMEM_CAMIPP_NECESSARY_CIF_0    0x400000
#elif (PMEM_SENSOR_FULL_RESOLUTION_CIF_0 == 0x30000)
#define PMEM_CAM_NECESSARY   0x400000        /* 640*480*1.5*4(preview) + 1M(capture raw) + 1M(jpeg encode output) */
#define PMEM_CAMIPP_NECESSARY_CIF_0    0x400000
#else
#define PMEM_CAM_NECESSARY   0x1200000
#define PMEM_CAMIPP_NECESSARY_CIF_0    0x800000
#endif

//CIF 1
#if (PMEM_SENSOR_FULL_RESOLUTION_CIF_1 == 0x500000)
#define PMEM_CAM_NECESSARY	 0x1400000		 /* 1280*720*1.5*4(preview) + 7.5M(capture raw) + 4M(jpeg encode output) */
#define PMEM_CAMIPP_NECESSARY_CIF_1	  0x800000
#elif (PMEM_SENSOR_FULL_RESOLUTION_CIF_1 == 0x300000)
#define PMEM_CAM_NECESSARY	 0xe00000		 /* 1280*720*1.5*4(preview) + 4.5M(capture raw) + 3M(jpeg encode output) */
#define PMEM_CAMIPP_NECESSARY_CIF_1    0x500000
#elif (PMEM_SENSOR_FULL_RESOLUTION_CIF_1== 0x200000) /* 1280*720*1.5*4(preview) + 3M(capture raw) + 3M(jpeg encode output) */
#define PMEM_CAM_NECESSARY	 0xc00000
#define PMEM_CAMIPP_NECESSARY_CIF_1    0x400000
#elif ((PMEM_SENSOR_FULL_RESOLUTION_CIF_1 == 0x100000) || (PMEM_SENSOR_FULL_RESOLUTION_CIF_1 == 0x130000))
#define PMEM_CAM_NECESSARY	 0x800000		 /* 800*600*1.5*4(preview) + 2M(capture raw) + 2M(jpeg encode output) */
#define PMEM_CAMIPP_NECESSARY_CIF_1    0x400000
#elif (PMEM_SENSOR_FULL_RESOLUTION_CIF_1 == 0x30000)
#define PMEM_CAM_NECESSARY	 0x400000		 /* 640*480*1.5*4(preview) + 1M(capture raw) + 1M(jpeg encode output) */
#define PMEM_CAMIPP_NECESSARY_CIF_1    0x400000
#else
#define PMEM_CAM_NECESSARY	 0x1200000
#define PMEM_CAMIPP_NECESSARY_CIF_1    0x800000
#endif
/*---------------- Camera Sensor Fixed Macro End  ------------------------*/
#else	//#ifdef CONFIG_VIDEO_RK 
#define PMEM_CAM_NECESSARY	 0x00000000
#endif
#else   // #ifdef PMEM_CAM_SIZE

/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_VIDEO_RK29 
static int camera_debug;
module_param(camera_debug, int, S_IRUGO|S_IWUSR);

#define ddprintk(level, fmt, arg...) do {			\
	if (camera_debug >= level) 					\
	    printk(KERN_WARNING"rk_cam_io: " fmt , ## arg); } while (0)

#define dprintk(format, ...) ddprintk(1, format, ## __VA_ARGS__)    

#define SENSOR_NAME_0 STR(CONFIG_SENSOR_0)			/* back camera sensor */
#define SENSOR_NAME_1 STR(CONFIG_SENSOR_1)			/* front camera sensor */
#define SENSOR_DEVICE_NAME_0  STR(CONS(CONFIG_SENSOR_0, _back))
#define SENSOR_DEVICE_NAME_1  STR(CONS(CONFIG_SENSOR_1, _front))

static int rk_sensor_io_init(void);
static int rk_sensor_io_deinit(int sensor);
static int rk_sensor_ioctrl(struct device *dev,enum rk29camera_ioctrl_cmd cmd, int on);
static int rk_sensor_power(struct device *dev, int on);
static int rk_sensor_reset(struct device *dev);
static int rk_sensor_powerdown(struct device *dev, int on);
static struct rk29camera_platform_data rk_camera_platform_data = {
    .io_init = rk_sensor_io_init,
    .io_deinit = rk_sensor_io_deinit,
    .sensor_ioctrl = rk_sensor_ioctrl,
    
    .gpio_res[0] = {
		.gpio_reset = INVALID_GPIO,
		.gpio_power = INVALID_GPIO,
		.gpio_powerdown = INVALID_GPIO,
		.gpio_flash = INVALID_GPIO,
		.gpio_flag = 0,
		.gpio_init = 0, 		   
		.dev_name = NULL,
    },
	.gpio_res[1] = {
		.gpio_reset = INVALID_GPIO,
		.gpio_power = INVALID_GPIO,
		.gpio_powerdown = INVALID_GPIO,
		.gpio_flash = INVALID_GPIO,
		.gpio_flag = 0,
		.gpio_init = 0, 		   
		.dev_name = NULL,
	},
    .info[0] = {
            .dev_name = NULL,
            .orientation = 0, 
	},
    .info[1] = {
		.dev_name = NULL,
		.orientation = 0, 
	},
    .sensor_init_data[0] = NULL,
    .sensor_init_data[1] = NULL,
};

#if (CONFIG_SENSOR_IIC_ADDR_0 != 0x00)
static struct i2c_board_info rk_i2c_cam_info_0[] = {
	{
		I2C_BOARD_INFO(SENSOR_NAME_0, CONFIG_SENSOR_IIC_ADDR_0>>1)
	},
};

static struct soc_camera_link rk_iclink_0 = {
	.bus_id= SENSOR_CIF_BUSID_0,
	.power		= rk_sensor_power,
#if (CONFIG_SENSOR_RESET_PIN_0 != INVALID_GPIO)
	.reset		= rk_sensor_reset,
#endif	  
	.powerdown	= rk_sensor_powerdown,
	.board_info = &rk_i2c_cam_info_0[0],
	
	.i2c_adapter_id = CONFIG_SENSOR_IIC_ADAPTER_ID_0,
	.module_name	= SENSOR_NAME_0,
};

/*platform_device: soc-camera need  */
 struct platform_device rk_soc_camera_pdrv_0 = {
	.name	= "soc-camera-pdrv",
	.id = 0,
	.dev	= {
		.init_name = SENSOR_DEVICE_NAME_0,
		.platform_data = &rk_iclink_0,
	},
};
#else
 struct platform_device rk_soc_camera_pdrv_0 = {
	.name	= NULL,
};
#endif
#if (CONFIG_SENSOR_IIC_ADDR_1 != 0x00)
static struct i2c_board_info rk_i2c_cam_info_1[] = {
	{
		I2C_BOARD_INFO(SENSOR_NAME_1, CONFIG_SENSOR_IIC_ADDR_1>>1)
	},
};

static struct soc_camera_link rk_iclink_1 = {
	.bus_id 	= SENSOR_CIF_BUSID_1,
	.power		= rk_sensor_power,
#if (CONFIG_SENSOR_RESET_PIN_1 != INVALID_GPIO)
	.reset		= rk_sensor_reset,
#endif		
	.powerdown	= rk_sensor_powerdown,
	.board_info = &rk_i2c_cam_info_1[0],
	.i2c_adapter_id = CONFIG_SENSOR_IIC_ADAPTER_ID_1,
	.module_name	= SENSOR_NAME_1,
};

/*platform_device : soc-camera need  */
 struct platform_device rk_soc_camera_pdrv_1 = {
	.name	= "soc-camera-pdrv",
	.id = 1,
	.dev	= {
		.init_name = SENSOR_DEVICE_NAME_1,
		.platform_data = &rk_iclink_1,
	},
};
#else
 struct platform_device rk_soc_camera_pdrv_1 = {
	.name	= NULL,
};
#endif

static u64 rockchip_device_camera_dmamask = 0xffffffffUL;
static struct resource rk_camera_resource_host_0[] = {
	[0] = {
		.start = RK30_CIF0_PHYS,
		.end   = RK30_CIF0_PHYS + RK30_CIF0_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_CIF0,
		.end   = IRQ_CIF0,
		.flags = IORESOURCE_IRQ,
	}
};
static struct resource rk_camera_resource_host_1[] = {
	[0] = {
		.start = RK30_CIF1_PHYS,
		.end   = RK30_CIF1_PHYS + RK30_CIF1_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_CIF1,
		.end   = IRQ_CIF1,
		.flags = IORESOURCE_IRQ,
	}
};
/*platform_device : */
 struct platform_device rk_device_camera_host_0 = {
	.name		  = RK29_CAM_DRV_NAME,
	.id 	  = RK_CAM_PLATFORM_DEV_ID_0,				/* This is used to put cameras on this interface */
	.num_resources	  = ARRAY_SIZE(rk_camera_resource_host_0),
	.resource	  = rk_camera_resource_host_0,
	.dev			= {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data	= &rk_camera_platform_data,
	}
};
/*platform_device : */
 struct platform_device rk_device_camera_host_1 = {
	.name		  = RK29_CAM_DRV_NAME,
	.id 	  = RK_CAM_PLATFORM_DEV_ID_1,				/* This is used to put cameras on this interface */
	.num_resources	  = ARRAY_SIZE(rk_camera_resource_host_1),
	.resource	  = rk_camera_resource_host_1,
	.dev			= {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data	= &rk_camera_platform_data,
	}
};

static void rk_init_camera_plateform_data(void)
{
	struct rk29camera_platform_data* tmp_host_plateform_data = &rk_camera_platform_data;
	int cam_index_in = 0; 
#if (CONFIG_SENSOR_IIC_ADDR_0 != 0x00)
	if(cam_index_in < RK_CAM_NUM){
		tmp_host_plateform_data->sensor_init_data[cam_index_in] = rk_init_data_sensor_0_p;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_reset= CONFIG_SENSOR_RESET_PIN_0;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_power= CONFIG_SENSOR_POWER_PIN_0;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_powerdown= CONFIG_SENSOR_POWERDN_PIN_0;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_flash= CONFIG_SENSOR_FALSH_PIN_0;
		tmp_host_plateform_data->gpio_res[cam_index_in].dev_name= SENSOR_DEVICE_NAME_0;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_flag= (CONFIG_SENSOR_POWERACTIVE_LEVEL_0|CONFIG_SENSOR_RESETACTIVE_LEVEL_0|CONFIG_SENSOR_POWERDNACTIVE_LEVEL_0|CONFIG_SENSOR_FLASHACTIVE_LEVEL_0);
		tmp_host_plateform_data->gpio_res[cam_index_in].dev_name= SENSOR_DEVICE_NAME_0;
		tmp_host_plateform_data->info[cam_index_in].dev_name= SENSOR_DEVICE_NAME_0;
		tmp_host_plateform_data->info[cam_index_in].orientation= CONFIG_SENSOR_ORIENTATION_0;
	}
	cam_index_in++;
#endif
#if (CONFIG_SENSOR_IIC_ADDR_1 != 0x00)
	if(cam_index_in < RK_CAM_NUM){
		tmp_host_plateform_data->sensor_init_data[cam_index_in] = rk_init_data_sensor_1_p;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_reset= CONFIG_SENSOR_RESET_PIN_1;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_power= CONFIG_SENSOR_POWER_PIN_1;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_powerdown= CONFIG_SENSOR_POWERDN_PIN_1;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_flash= CONFIG_SENSOR_FALSH_PIN_1;
		tmp_host_plateform_data->gpio_res[cam_index_in].dev_name= SENSOR_DEVICE_NAME_1;
		tmp_host_plateform_data->gpio_res[cam_index_in].gpio_flag= (CONFIG_SENSOR_POWERACTIVE_LEVEL_1|CONFIG_SENSOR_RESETACTIVE_LEVEL_1|CONFIG_SENSOR_POWERDNACTIVE_LEVEL_1|CONFIG_SENSOR_FLASHACTIVE_LEVEL_1);
		tmp_host_plateform_data->gpio_res[cam_index_in].dev_name= SENSOR_DEVICE_NAME_1;
		tmp_host_plateform_data->info[cam_index_in].dev_name= SENSOR_DEVICE_NAME_1;
		tmp_host_plateform_data->info[cam_index_in].orientation= CONFIG_SENSOR_ORIENTATION_1;
	}
	cam_index_in++;
#endif
}

static int rk_sensor_iomux(int pin)
{    
    switch (pin)
    {
        case RK30_PIN0_PA0: 
		{
			 rk30_mux_api_set(GPIO0A0_HDMIHOTPLUGIN_NAME,0);
			break;	
		}
        case RK30_PIN0_PA1: 
		{
			 rk30_mux_api_set(GPIO0A1_HDMII2CSCL_NAME,0);
			break;	
		}
        case RK30_PIN0_PA2:
		{
			 rk30_mux_api_set(GPIO0A2_HDMII2CSDA_NAME,0);
			break;	
		}
        case RK30_PIN0_PA3:
		{
			 rk30_mux_api_set(GPIO0A3_PWM0_NAME,0);
			break;	
		}
        case RK30_PIN0_PA4:
		{
			 rk30_mux_api_set(GPIO0A4_PWM1_NAME,0);
			break;	
		}
        case RK30_PIN0_PA5:
		{
			 rk30_mux_api_set(GPIO0A5_OTGDRVVBUS_NAME,0);
			break;	
		}
        case RK30_PIN0_PA6:
        {
             rk30_mux_api_set(GPIO0A6_HOSTDRVVBUS_NAME,0);
            break;	
        }
        case RK30_PIN0_PA7:
        {
             rk30_mux_api_set(GPIO0A7_I2S8CHSDI_NAME,0);
            break;	
        }
        case RK30_PIN0_PB0:
        {
             rk30_mux_api_set(GPIO0B0_I2S8CHCLK_NAME,0);
            break;	
        }
        case RK30_PIN0_PB1:
        {
             rk30_mux_api_set(GPIO0B1_I2S8CHSCLK_NAME,0);
            break;	
        }
        case RK30_PIN0_PB2:
        {
             rk30_mux_api_set(GPIO0B2_I2S8CHLRCKRX_NAME,0);
            break;	
        }
        case RK30_PIN0_PB3:
        {
             rk30_mux_api_set(GPIO0B3_I2S8CHLRCKTX_NAME,0);
            break;	
        }
        case RK30_PIN0_PB4:
        {
             rk30_mux_api_set(GPIO0B4_I2S8CHSDO0_NAME,0);
            break;	
        }
        case RK30_PIN0_PB5:
        {
             rk30_mux_api_set(GPIO0B5_I2S8CHSDO1_NAME,0);
            break;	
        }
        case RK30_PIN0_PB6:
        {
             rk30_mux_api_set(GPIO0B6_I2S8CHSDO2_NAME,0);
            break;	
        }
        case RK30_PIN0_PB7:
        {
             rk30_mux_api_set(GPIO0B7_I2S8CHSDO3_NAME,0);
            break;	
        }
        case RK30_PIN0_PC0:
        {
             rk30_mux_api_set(GPIO0C0_I2S12CHCLK_NAME,0);
            break;	
        }
        case RK30_PIN0_PC1:
        {
             rk30_mux_api_set(GPIO0C1_I2S12CHSCLK_NAME,0);
            break;	
        }
        case RK30_PIN0_PC2:
        {
             rk30_mux_api_set(GPIO0C2_I2S12CHLRCKRX_NAME,0);
            break;	
        }
        case RK30_PIN0_PC3:
        {
             rk30_mux_api_set(GPIO0C3_I2S12CHLRCKTX_NAME,0);
            break;	
        }
        case RK30_PIN0_PC4:
        {
             rk30_mux_api_set(GPIO0C4_I2S12CHSDI_NAME,0);
            break;	
        }
        case RK30_PIN0_PC5:
        {
             rk30_mux_api_set(GPIO0C5_I2S12CHSDO_NAME,0);
            break;	
        }
        case RK30_PIN0_PC6:
        {
             rk30_mux_api_set(GPIO0C6_TRACECLK_SMCADDR2_NAME,0);
            break;	
        }
        case RK30_PIN0_PC7:
        {
             rk30_mux_api_set(GPIO0C7_TRACECTL_SMCADDR3_NAME,0);
            break;	
        }
        case RK30_PIN0_PD0:
        {
             rk30_mux_api_set(GPIO0D0_I2S22CHCLK_SMCCSN0_NAME,0);
            break;	
        }
        case RK30_PIN0_PD1:
        {
             rk30_mux_api_set(GPIO0D1_I2S22CHSCLK_SMCWEN_NAME,0);
            break;	
        }
        case RK30_PIN0_PD2:
        {
             rk30_mux_api_set(GPIO0D2_I2S22CHLRCKRX_SMCOEN_NAME,0);
            break;	
        }
        case RK30_PIN0_PD3:
        {
             rk30_mux_api_set(GPIO0D3_I2S22CHLRCKTX_SMCADVN_NAME,0);
            break;	
        }
        case RK30_PIN0_PD4:
        {
             rk30_mux_api_set(GPIO0D4_I2S22CHSDI_SMCADDR0_NAME,0);
            break;	
        }
        case RK30_PIN0_PD5:
        {
             rk30_mux_api_set(GPIO0D5_I2S22CHSDO_SMCADDR1_NAME,0);
            break;	
        }
        case RK30_PIN0_PD6:
        {
             rk30_mux_api_set(GPIO0D6_PWM2_NAME,0);
            break;	
        }
        case RK30_PIN0_PD7:
        {
             rk30_mux_api_set(GPIO0D7_PWM3_NAME,0);
            break;	
        }
        case RK30_PIN1_PA0:
        {
             rk30_mux_api_set(GPIO1A0_UART0SIN_NAME,0);
            break;	
        }
        case RK30_PIN1_PA1:
        {
             rk30_mux_api_set(GPIO1A1_UART0SOUT_NAME,0);
            break;	
        }
        case RK30_PIN1_PA2:
        {
             rk30_mux_api_set(GPIO1A2_UART0CTSN_NAME,0);
            break;	
        }
        case RK30_PIN1_PA3:
        {
             rk30_mux_api_set(GPIO1A3_UART0RTSN_NAME,0);
            break;	
        }
        case RK30_PIN1_PA4:
        {
             rk30_mux_api_set(GPIO1A4_UART1SIN_SPI0CSN0_NAME,0);
            break;	
        }
        case RK30_PIN1_PA5:
        {
             rk30_mux_api_set(GPIO1A5_UART1SOUT_SPI0CLK_NAME,0);
            break;	
        }
        case RK30_PIN1_PA6:
        {
             rk30_mux_api_set(GPIO1A6_UART1CTSN_SPI0RXD_NAME,0);
            break;	
        }
        case RK30_PIN1_PA7:
        {
             rk30_mux_api_set(GPIO1A7_UART1RTSN_SPI0TXD_NAME,0);
            break;	
        }
        case RK30_PIN1_PB0:
        {
             rk30_mux_api_set(GPIO1B0_UART2SIN_NAME,0);
            break;	
        }
        case RK30_PIN1_PB1:
        {
             rk30_mux_api_set(GPIO1B1_UART2SOUT_NAME,0);
            break;	
        }
        case RK30_PIN1_PB2:
        {
             rk30_mux_api_set(GPIO1B2_SPDIFTX_NAME,0);
            break;	
        }
        case RK30_PIN1_PB3:
        {
             rk30_mux_api_set(GPIO1B3_CIF0CLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN1_PB4:
        {
             rk30_mux_api_set(GPIO1B4_CIF0DATA0_NAME,0);
            break;	
        }
        case RK30_PIN1_PB5:
        {
             rk30_mux_api_set(GPIO1B5_CIF0DATA1_NAME,0);
            break;	
        }
        case RK30_PIN1_PB6:
        {
             rk30_mux_api_set(GPIO1B6_CIFDATA10_NAME,0);
            break;	
        }
        case RK30_PIN1_PB7:
        {
             rk30_mux_api_set(GPIO1B7_CIFDATA11_NAME,0);
            break;	
        }
        case RK30_PIN1_PC0:
        {
             rk30_mux_api_set(GPIO1C0_CIF1DATA2_RMIICLKOUT_RMIICLKIN_NAME,0);
            break;	
        }
        case RK30_PIN1_PC1:
        {
             rk30_mux_api_set(GPIO1C1_CIFDATA3_RMIITXEN_NAME,0);
            break;	
        }
        case RK30_PIN1_PC2:
        {
             rk30_mux_api_set(GPIO1C2_CIF1DATA4_RMIITXD1_NAME,0);
            break;	
        }
        case RK30_PIN1_PC3:
        {
             rk30_mux_api_set(GPIO1C3_CIFDATA5_RMIITXD0_NAME,0);
            break;	
        }
        case RK30_PIN1_PC4:
        {
             rk30_mux_api_set(GPIO1C4_CIFDATA6_RMIIRXERR_NAME,0);
            break;	
        }
        case RK30_PIN1_PC5:
        {
             rk29_mux_api_set(GPIO1C5_CIFDATA7_RMIICRSDVALID_NAME,0);
            break;	
        }
        case RK30_PIN1_PC6:
        {
             rk30_mux_api_set(GPIO1C6_CIFDATA8_RMIIRXD1_NAME,0);
            break;	
        }
        case RK30_PIN1_PC7:
        {
             rk30_mux_api_set(GPIO1C7_CIFDATA9_RMIIRXD0_NAME,0);
            break;	
        }
        case RK30_PIN1_PD0:
        {
             rk30_mux_api_set(GPIO1D0_CIF1VSYNC_MIIMD_NAME,0);
            break;	
        }
        case RK30_PIN1_PD1:
        {
             rk30_mux_api_set(GPIO1D1_CIF1HREF_MIIMDCLK_NAME,0);
            break;	
        }
        case RK30_PIN1_PD2:
        {
             rk30_mux_api_set(GPIO1D2_CIF1CLKIN_NAME,0);
            break;	
        }
        case RK30_PIN1_PD3:
        {
             rk30_mux_api_set(GPIO1D3_CIF1DATA0_NAME,0);
            break;	
        }
        case RK30_PIN1_PD4:
        {
             rk30_mux_api_set(GPIO1D4_CIF1DATA1_NAME,0);
            break;	
        }
        case RK30_PIN1_PD5:
        {
             rk30_mux_api_set(GPIO1D5_CIF1DATA10_NAME,0);
            break;	
        }
        case RK30_PIN1_PD6:
        {
             rk30_mux_api_set(GPIO1D6_CIF1DATA11_NAME,0);
            break;	
        }
        case RK30_PIN1_PD7:
        {
             rk30_mux_api_set(GPIO1D7_CIF1CLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN2_PA0:
        {
             rk30_mux_api_set(GPIO2A0_LCDC1DATA0_SMCADDR4_NAME,0);
            break;	
        }
        case RK30_PIN2_PA1:
        {
             rk30_mux_api_set(GPIO2A1_LCDC1DATA1_SMCADDR5_NAME,0);
            break;	
        }
        case RK30_PIN2_PA2:
        {
             rk30_mux_api_set(GPIO2A2_LCDCDATA2_SMCADDR6_NAME,0);
            break;	
        }
        case RK30_PIN2_PA3:
        {
             rk30_mux_api_set(GPIO2A3_LCDCDATA3_SMCADDR7_NAME,0);
            break;	
        }
        case RK30_PIN2_PA4:
        {
             rk30_mux_api_set(GPIO2A4_LCDC1DATA4_SMCADDR8_NAME,0);
            break;	
        }
        case RK30_PIN2_PA5:
        {
             rk30_mux_api_set(GPIO2A5_LCDC1DATA5_SMCADDR9_NAME,0);
            break;	
        }
        case RK30_PIN2_PA6:
        {
             rk30_mux_api_set(GPIO2A6_LCDC1DATA6_SMCADDR10_NAME,0);
            break;	
        }
        case RK30_PIN2_PA7:
        {
             rk30_mux_api_set(GPIO2A7_LCDC1DATA7_SMCADDR11_NAME,0);
            break;	
        }
        case RK30_PIN2_PB0:
        {
             rk30_mux_api_set(GPIO2B0_LCDC1DATA8_SMCADDR12_NAME,0);
            break;	
        }
        case RK30_PIN2_PB1:
        {
             rk30_mux_api_set(GPIO2B1_LCDC1DATA9_SMCADDR13_NAME,0);
            break;	
        }
        case RK30_PIN2_PB2:
        {
             rk30_mux_api_set(GPIO2B2_LCDC1DATA10_SMCADDR14_NAME,0);
            break;	
        }
        case RK30_PIN2_PB3:
        {
             rk30_mux_api_set(GPIO2B3_LCDC1DATA11_SMCADDR15_NAME,0);
            break;	
        }
        case RK30_PIN2_PB4:
        {
             rk30_mux_api_set(GPIO2B4_LCDC1DATA12_SMCADDR16_HSADCDATA9_NAME,0);
            break;	
        }
        case RK30_PIN2_PB5:
        {
             rk30_mux_api_set(GPIO2B5_LCDC1DATA13_SMCADDR17_HSADCDATA8_NAME,0);
            break;	
        }
        case RK30_PIN2_PB6:
        {
             rk30_mux_api_set(GPIO2B6_LCDC1DATA14_SMCADDR18_TSSYNC_NAME,0);
            break;	
        }
        case RK30_PIN2_PB7:
        {
             rk30_mux_api_set(GPIO2B7_LCDC1DATA15_SMCADDR19_HSADCDATA7_NAME,0);
            break;	
        }
        case RK30_PIN2_PC0:
        {
             rk30_mux_api_set(GPIO2C0_LCDCDATA16_GPSCLK_HSADCCLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN2_PC1:
        {
             rk30_mux_api_set(GPIO2C1_LCDC1DATA17_SMCBLSN0_HSADCDATA6_NAME,0);
            break;	
        }
        case RK30_PIN2_PC2:
        {
             rk30_mux_api_set(GPIO2C2_LCDC1DATA18_SMCBLSN1_HSADCDATA5_NAME,0);
            break;	
        }
        case RK30_PIN2_PC3:
        {
             rk29_mux_api_set(GPIO2C3_LCDC1DATA19_SPI1CLK_HSADCDATA0_NAME,0);
            break;	
        }
        case RK30_PIN2_PC4:
        {
             rk30_mux_api_set(GPIO2C4_LCDC1DATA20_SPI1CSN0_HSADCDATA1_NAME,0);
            break;	
        }
        case RK30_PIN2_PC5:
        {
             rk30_mux_api_set(GPIO2C5_LCDC1DATA21_SPI1TXD_HSADCDATA2_NAME,0);
            break;	
        }
        case RK30_PIN2_PC6:
        {
             rk30_mux_api_set(GPIO2C6_LCDC1DATA22_SPI1RXD_HSADCDATA3_NAME,0);
            break;	
        }
        case RK30_PIN2_PC7:
        {
             rk30_mux_api_set(GPIO2C7_LCDC1DATA23_SPI1CSN1_HSADCDATA4_NAME,0);
            break;	
        }
        case RK30_PIN2_PD0:
        {
             rk30_mux_api_set(GPIO2D0_LCDC1DCLK_NAME,0);
            break;	
        }
        case RK30_PIN2_PD1:
        {
             rk30_mux_api_set(GPIO2D1_LCDC1DEN_SMCCSN1_NAME,0);
            break;	
        }
        case RK30_PIN2_PD2:
        {
             rk30_mux_api_set(GPIO2D2_LCDC1HSYNC_NAME,0);
            break;	
        }
        case RK30_PIN2_PD3:
        {
             rk30_mux_api_set(GPIO2D3_LCDC1VSYNC_NAME,0);
            break;	
        }
        case RK30_PIN2_PD4:
        {
             rk30_mux_api_set(GPIO2D4_I2C0SDA_NAME,0);
            break;	
        }
        case RK30_PIN2_PD5:
        {
             rk30_mux_api_set(GPIO2D5_I2C0SCL_NAME,0);
            break;	
        }
        case RK30_PIN2_PD6:
        {
             rk30_mux_api_set(GPIO2D6_I2C1SDA_NAME,0);
            break;	
        }
        case RK30_PIN2_PD7:
        {
             rk30_mux_api_set(GPIO2D7_I2C1SCL_NAME,0);
            break;	
        }
        case RK30_PIN3_PA0:
        {
             rk30_mux_api_set(GPIO3A0_I2C2SDA_NAME,0);
            break;	
        }
        case RK30_PIN3_PA1:
        {
             rk30_mux_api_set(GPIO3A1_I2C2SCL_NAME,0);
            break;	
        }
        case RK30_PIN3_PA2:
        {
             rk30_mux_api_set(GPIO3A2_I2C3SDA_NAME,0);
            break;	
        }
        case RK30_PIN3_PA3:
        {
             rk30_mux_api_set(GPIO3A3_I2C3SCL_NAME,0);
            break;	
        }
        case RK30_PIN3_PA4:
        {
             rk30_mux_api_set(GPIO3A4_I2C4SDA_NAME,0);
            break;	
        }
        case RK30_PIN3_PA5:
        {
             rk30_mux_api_set(GPIO3A5_I2C4SCL_NAME,0);
            break;	
        }
        case RK30_PIN3_PA6:
        {
             rk30_mux_api_set(GPIO3A6_SDMMC0RSTNOUT_NAME,0);
            break;	
        }
        case RK30_PIN3_PA7:
        {
             rk30_mux_api_set(GPIO3A7_SDMMC0WRITEPRT_NAME,0);
            break;	
        }
        case RK30_PIN3_PB0:
        {
             rk30_mux_api_set(GPIO3B0_SDMMC0CLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN3_PB1:
        {
             rk30_mux_api_set(GPIO3B1_SDMMC0CMD_NAME,0);
            break;	
        }
        case RK30_PIN3_PB2:
        {
             rk30_mux_api_set(GPIO3B2_SDMMC0DATA0_NAME,0);
            break;	
        }
        case RK30_PIN3_PB3:
        {
             rk30_mux_api_set(GPIO3B3_SDMMC0DATA1_NAME,0);
            break;	
        }
        case RK30_PIN3_PB4:
        {
             rk30_mux_api_set(GPIO3B4_SDMMC0DATA2_NAME,0);
            break;	
        }
        case RK30_PIN3_PB5:
        {
             rk30_mux_api_set(GPIO3B5_SDMMC0DATA3_NAME,0);
            break;	
        }
        case RK30_PIN3_PB6:
        {
             rk30_mux_api_set(GPIO3B6_SDMMC0DETECTN_NAME,0);
            break;	
        }
        case RK30_PIN3_PB7:
        {
             rk30_mux_api_set(GPIO3B7_SDMMC0WRITEPRT_NAME,0);
            break;	
        }
        case RK30_PIN3_PC0:
        {
             rk30_mux_api_set(GPIO3C0_SMMC1CMD_NAME,0);
            break;	
        }
        case RK30_PIN3_PC1:
        {
             rk30_mux_api_set(GPIO3C1_SDMMC1DATA0_NAME,0);
            break;	
        }
        case RK30_PIN3_PC2:
        {
             rk30_mux_api_set(GPIO3C2_SDMMC1DATA1_NAME,0);
            break;	
        }
        case RK30_PIN3_PC3:
        {
             rk30_mux_api_set(GPIO3C3_SDMMC1DATA2_NAME,0);
            break;	
        }
        case RK30_PIN3_PC4:
        {
             rk30_mux_api_set(GPIO3C4_SDMMC1DATA3_NAME,0);
            break;	
        }
        case RK30_PIN3_PC5:
        {
             rk30_mux_api_set(GPIO3C5_SDMMC1CLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN3_PC6:
        {
             rk30_mux_api_set(GPIO3C6_SDMMC1DETECTN_NAME,0);
            break;	
        }
        case RK30_PIN3_PC7:
        {
             rk30_mux_api_set(GPIO3C7_SDMMC1WRITEPRT_NAME,0);
            break;	
        }
        case RK30_PIN3_PD0:
        {
             rk30_mux_api_set(GPIO3D0_SDMMC1PWREN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD1:
        {
             rk30_mux_api_set(GPIO3D1_SDMMC1BACKENDPWR_NAME,0);
            break;	
        }
        case RK30_PIN3_PD2:
        {
             rk30_mux_api_set(GPIO3D2_SDMMC1INTN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD3:
        {
             rk30_mux_api_set(GPIO3D3_UART3SIN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD4:
        {
             rk30_mux_api_set(GPIO3D4_UART3SOUT_NAME,0);
            break;	
        }
        case RK30_PIN3_PD5:
        {
             rk30_mux_api_set(GPIO3D5_UART3CTSN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD6:
        {
             rk30_mux_api_set(GPIO3D6_UART3RTSN_NAME,0);
            break;	
        }
        case RK30_PIN3_PD7:
        {
             rk30_mux_api_set(GPIO3D7_FLASHDQS_EMMCCLKOUT_NAME,0);
            break;	
        }
        case RK30_PIN4_PA0:
	{
		 rk30_mux_api_set(GPIO4A0_FLASHDATA8_NAME,0);
		break;	
	}
        case RK30_PIN4_PA1:
	{
		 rk30_mux_api_set(GPIO4A1_FLASHDATA9_NAME,0);
		break;	
	}
        case RK30_PIN4_PA2:
	{
		 rk30_mux_api_set(GPIO4A2_FLASHDATA10_NAME,0);
		break;	
	}
			
        case RK30_PIN4_PA3:
	{
		 rk30_mux_api_set(GPIO4A3_FLASHDATA11_NAME,0);
		break;	
	}
        case RK30_PIN4_PA4:
	{
		 rk30_mux_api_set(GPIO4A4_FLASHDATA12_NAME,0);
		break;	
	}
        case RK30_PIN4_PA5:
        {
             rk30_mux_api_set(GPIO4A5_FLASHDATA13_NAME,0);
            break;	
        }
        case RK30_PIN4_PA6:
        {
             rk30_mux_api_set(GPIO4A6_FLASHDATA14_NAME,0);
            break;	
        }
        case RK30_PIN4_PA7:
        {
             rk30_mux_api_set(GPIO4A7_FLASHDATA15_NAME,0);
            break;	
        }
        case RK30_PIN4_PB0:
        {
             rk30_mux_api_set(GPIO4B0_FLASHCSN1_NAME,0);
            break;	
        }
        case RK30_PIN4_PB1:
        {
             rk30_mux_api_set(GPIO4B1_FLASHCSN2_EMMCCMD_NAME,0);
            break;	
        }
        case RK30_PIN4_PB2:
        {
             rk30_mux_api_set(GPIO4B2_FLASHCSN3_EMMCRSTNOUT_NAME,0);
            break;	
        }
        case RK30_PIN4_PB3:
        {
             rk30_mux_api_set(GPIO4B3_FLASHCSN4_NAME,0);
            break;	
        }
        case RK30_PIN4_PB4:
        {
             rk30_mux_api_set(GPIO4B4_FLASHCSN5_NAME,0);
            break;	
        }
        case RK30_PIN4_PB5:
        {
             rk30_mux_api_set(GPIO4B5_FLASHCSN6_NAME,0);
            break;	
        }
        case RK30_PIN4_PB6:
        {
             rk30_mux_api_set(GPIO4B6_FLASHCSN7_NAME ,0);
            break;	
        }
        case RK30_PIN4_PB7:
        {
             rk30_mux_api_set(GPIO4B7_SPI0CSN1_NAME,0);
            break;	
        }
        case RK30_PIN4_PC0:
        {
             rk30_mux_api_set(GPIO4C0_SMCDATA0_TRACEDATA0_NAME,0);
            break;	
        }
        case RK30_PIN4_PC1:
        {
             rk30_mux_api_set(GPIO4C1_SMCDATA1_TRACEDATA1_NAME,0);
            break;	
        }
        case RK30_PIN4_PC2:
        {
             rk30_mux_api_set(GPIO4C2_SMCDATA2_TRACEDATA2_NAME,0);
            break;	
        }
        case RK30_PIN4_PC3:
        {
             rk30_mux_api_set(GPIO4C3_SMCDATA3_TRACEDATA3_NAME,0);
            break;	
        }
        case RK30_PIN4_PC4:
        {
             rk30_mux_api_set(GPIO4C4_SMCDATA4_TRACEDATA4_NAME,0);
            break;	
        }
        case RK30_PIN4_PC5:
        {
             rk30_mux_api_set(GPIO4C5_SMCDATA5_TRACEDATA5_NAME,0);
            break;	
        }
        case RK30_PIN4_PC6:
        {
             rk30_mux_api_set(GPIO4C6_SMCDATA6_TRACEDATA6_NAME,0);
            break;	
        }


        case RK30_PIN4_PC7:
        {
             rk30_mux_api_set(GPIO4C7_SMCDATA7_TRACEDATA7_NAME,0);
            break;	
        }
        case RK30_PIN4_PD0:
	{
		 rk30_mux_api_set(GPIO4D0_SMCDATA8_TRACEDATA8_NAME,0);			   
		break;	
	}
        case RK30_PIN4_PD1:
        {
             rk30_mux_api_set(GPIO4D1_SMCDATA9_TRACEDATA9_NAME,0);             
            break;	
        }
        case RK30_PIN4_PD2:
	{
		 rk30_mux_api_set(GPIO4D2_SMCDATA10_TRACEDATA10_NAME,0);			   
		break;	
	}
        case RK30_PIN4_PD3:
        {
             rk30_mux_api_set(GPIO4D3_SMCDATA11_TRACEDATA11_NAME,0);           
            break;	
        }
        case RK30_PIN4_PD4:
        {
             rk30_mux_api_set(GPIO4D4_SMCDATA12_TRACEDATA12_NAME,0);
            break;	
        }
        case RK30_PIN4_PD5:
        {
             rk30_mux_api_set(GPIO4D5_SMCDATA13_TRACEDATA13_NAME,0);
            break;	
        }
        case RK30_PIN4_PD6:
        {
             rk30_mux_api_set(GPIO4D6_SMCDATA14_TRACEDATA14_NAME,0);
            break;	
        }
        case RK30_PIN4_PD7:
        {
             rk30_mux_api_set(GPIO4D7_SMCDATA15_TRACEDATA15_NAME,0);
            break;	
        } 
        case RK30_PIN6_PA0:
        case RK30_PIN6_PA1:
        case RK30_PIN6_PA2:
        case RK30_PIN6_PA3:
        case RK30_PIN6_PA4:
        case RK30_PIN6_PA5:
        case RK30_PIN6_PA6:
        case RK30_PIN6_PA7:
        case RK30_PIN6_PB0:
        case RK30_PIN6_PB1:
        case RK30_PIN6_PB2:
        case RK30_PIN6_PB3:
        case RK30_PIN6_PB4:
        case RK30_PIN6_PB5:
        case RK30_PIN6_PB6:
			break;
        case RK30_PIN6_PB7:
		{
			 rk30_mux_api_set(GPIO6B7_TESTCLOCKOUT_NAME,0);
			break;	
		} 
        default:
        {
            printk("Pin=%d isn't RK GPIO, Please init it's iomux yourself!",pin);
            break;
        }
    }
    return 0;
}

static int sensor_power_default_cb (struct rk29camera_gpio_res *res, int on)
{
    int camera_power = res->gpio_power;
    int camera_ioflag = res->gpio_flag;
    int camera_io_init = res->gpio_init;
    int ret = 0;
    
    if (camera_power != INVALID_GPIO)  {
		     if (camera_io_init & RK29_CAM_POWERACTIVE_MASK) {
            if (on) {
            	gpio_set_value(camera_power, ((camera_ioflag&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
    			dprintk("%s..%s..PowerPin=%d ..PinLevel = %x   \n",__FUNCTION__,res->dev_name, camera_power, ((camera_ioflag&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
    			msleep(10);
    		} else {
    			gpio_set_value(camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
    			dprintk("%s..%s..PowerPin=%d ..PinLevel = %x   \n",__FUNCTION__,res->dev_name, camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
    		}
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			printk("%s..%s..PowerPin=%d request failed!\n",__FUNCTION__,res->dev_name,camera_power);
	    }        
    } else {
		ret = RK29_CAM_EIO_INVALID;
    } 

    return ret;
}

static int sensor_reset_default_cb (struct rk29camera_gpio_res *res, int on)
{
    int camera_reset = res->gpio_reset;
    int camera_ioflag = res->gpio_flag;
    int camera_io_init = res->gpio_init;  
    int ret = 0;
    
    if (camera_reset != INVALID_GPIO) {
		if (camera_io_init & RK29_CAM_RESETACTIVE_MASK) {
			if (on) {
	        	gpio_set_value(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
	        	dprintk("%s..%s..ResetPin=%d ..PinLevel = %x \n",__FUNCTION__,res->dev_name,camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
			} else {
				gpio_set_value(camera_reset,(((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
        		dprintk("%s..%s..ResetPin= %d..PinLevel = %x   \n",__FUNCTION__,res->dev_name, camera_reset, (((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
	        }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			printk("%s..%s..ResetPin=%d request failed!\n",__FUNCTION__,res->dev_name,camera_reset);
		}
    } else {
		ret = RK29_CAM_EIO_INVALID;
    }

    return ret;
}

static int sensor_powerdown_default_cb (struct rk29camera_gpio_res *res, int on)
{
    int camera_powerdown = res->gpio_powerdown;
    int camera_ioflag = res->gpio_flag;
    int camera_io_init = res->gpio_init;  
    int ret = 0;    

    if (camera_powerdown != INVALID_GPIO) {
		if (camera_io_init & RK29_CAM_POWERDNACTIVE_MASK) {
			if (on) {
	        	gpio_set_value(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
	        	dprintk("%s..%s..PowerDownPin=%d ..PinLevel = %x \n",__FUNCTION__,res->dev_name,camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
			} else {
				gpio_set_value(camera_powerdown,(((~camera_ioflag)&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
        		dprintk("%s..%s..PowerDownPin= %d..PinLevel = %x   \n",__FUNCTION__,res->dev_name, camera_powerdown, (((~camera_ioflag)&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
	        }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			dprintk("%s..%s..PowerDownPin=%d request failed!\n",__FUNCTION__,res->dev_name,camera_powerdown);
		}
    } else {
		ret = RK29_CAM_EIO_INVALID;
    }
    return ret;
}


static int sensor_flash_default_cb (struct rk29camera_gpio_res *res, int on)
{
    int camera_flash = res->gpio_flash;
    int camera_ioflag = res->gpio_flag;
    int camera_io_init = res->gpio_init;  
    int ret = 0;    

    if (camera_flash != INVALID_GPIO) {
		if (camera_io_init & RK29_CAM_FLASHACTIVE_MASK) {
            switch (on)
            {
                case Flash_Off:
                {
                    gpio_set_value(camera_flash,(((~camera_ioflag)&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
        		    dprintk("\n%s..%s..FlashPin= %d..PinLevel = %x   \n",__FUNCTION__,res->dev_name, camera_flash, (((~camera_ioflag)&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS)); 
        		    break;
                }

                case Flash_On:
                {
                    gpio_set_value(camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    dprintk("%s..%s..FlashPin=%d ..PinLevel = %x \n",__FUNCTION__,res->dev_name,camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    break;
                }

                case Flash_Torch:
                {
                    gpio_set_value(camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    dprintk("%s..%s..FlashPin=%d ..PinLevel = %x \n",__FUNCTION__,res->dev_name,camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    break;
                }

                default:
                {
                    printk("%s..%s..Flash command(%d) is invalidate \n",__FUNCTION__,res->dev_name,on);
                    break;
                }
            }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			printk("%s..%s..FlashPin=%d request failed!\n",__FUNCTION__,res->dev_name,camera_flash);
		}
    } else {
		ret = RK29_CAM_EIO_INVALID;
    }
    return ret;
}


static int rk_sensor_io_init(void)
{
    int ret = 0, i,j;
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;
	unsigned int camera_powerdown = INVALID_GPIO, camera_flash = INVALID_GPIO;
	unsigned int camera_ioflag;
	static bool is_init = false;
	struct rk29camera_platform_data* plat_data = &rk_camera_platform_data;
	if(is_init){
		//printk("sensor io has been initialized \n");
		return 0;
		}
	else
		is_init = true;
    if (sensor_ioctl_cb.sensor_power_cb == NULL)
        sensor_ioctl_cb.sensor_power_cb = sensor_power_default_cb;
    if (sensor_ioctl_cb.sensor_reset_cb == NULL)
        sensor_ioctl_cb.sensor_reset_cb = sensor_reset_default_cb;
    if (sensor_ioctl_cb.sensor_powerdown_cb == NULL)
        sensor_ioctl_cb.sensor_powerdown_cb = sensor_powerdown_default_cb;
    if (sensor_ioctl_cb.sensor_flash_cb == NULL)
        sensor_ioctl_cb.sensor_flash_cb = sensor_flash_default_cb;
	for(i = 0;i < RK_CAM_NUM; i++){
		camera_reset = plat_data->gpio_res[i].gpio_reset;
		camera_power = plat_data->gpio_res[i].gpio_power;
		camera_powerdown = plat_data->gpio_res[i].gpio_powerdown;
		camera_flash = plat_data->gpio_res[i].gpio_flash;
		camera_ioflag = plat_data->gpio_res[i].gpio_flag;
		plat_data->gpio_res[i].gpio_init = 0;

        if (camera_power != INVALID_GPIO) {
            ret = gpio_request(camera_power, "camera power");
            if (ret) {
                if (i == 0) {
                    printk("%s..%s..power pin(%d) init failed\n",__FUNCTION__,plat_data->gpio_res[i].dev_name,camera_power);
				    goto sensor_io_init_erro;
                } else {
                    if (camera_power != plat_data->gpio_res[i].gpio_power) {
                        printk("%s..%s..power pin(%d) init failed\n",__FUNCTION__,plat_data->gpio_res[i].dev_name,camera_power);
                        goto sensor_io_init_erro;
                    }
                }
            }

            if (rk_sensor_iomux(camera_power) < 0) {
                printk(KERN_ERR "%s..%s..power pin(%d) iomux init failed\n",__FUNCTION__,plat_data->gpio_res[i].dev_name,camera_power);
                goto sensor_io_init_erro;
            }
            
			plat_data->gpio_res[i].gpio_init |= RK29_CAM_POWERACTIVE_MASK;
            gpio_set_value(camera_reset, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
            gpio_direction_output(camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));

			dprintk("%s....power pin(%d) init success(0x%x)  \n",__FUNCTION__,camera_power,(((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));

        }

        if (camera_reset != INVALID_GPIO) {
            ret = gpio_request(camera_reset, "camera reset");
            if (ret) {
                printk("%s..%s..reset pin(%d) init failed\n",__FUNCTION__,plat_data->gpio_res[i].dev_name,camera_reset);
                goto sensor_io_init_erro;
            }

            if (rk_sensor_iomux(camera_reset) < 0) {
                printk(KERN_ERR "%s..%s..reset pin(%d) iomux init failed\n",__FUNCTION__,plat_data->gpio_res[i].dev_name,camera_reset);
                goto sensor_io_init_erro;
            }
            
			plat_data->gpio_res[i].gpio_init |= RK29_CAM_RESETACTIVE_MASK;
            gpio_set_value(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
            gpio_direction_output(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));

			dprintk("%s....reset pin(%d) init success(0x%x)\n",__FUNCTION__,camera_reset,((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));

        }

		if (camera_powerdown != INVALID_GPIO) {
            ret = gpio_request(camera_powerdown, "camera powerdown");
            if (ret) {
                printk("%s..%s..powerdown pin(%d) init failed\n",__FUNCTION__,plat_data->gpio_res[i].dev_name,camera_powerdown);
                goto sensor_io_init_erro;
            }

            if (rk_sensor_iomux(camera_powerdown) < 0) {
                printk(KERN_ERR "%s..%s..powerdown pin(%d) iomux init failed\n",__FUNCTION__,plat_data->gpio_res[i].dev_name,camera_powerdown);
                goto sensor_io_init_erro;
            }
            
			plat_data->gpio_res[i].gpio_init |= RK29_CAM_POWERDNACTIVE_MASK;
            gpio_set_value(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
            gpio_direction_output(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));

			dprintk("%s....powerdown pin(%d) init success(0x%x) \n",__FUNCTION__,camera_powerdown,((camera_ioflag&RK29_CAM_POWERDNACTIVE_BITPOS)>>RK29_CAM_POWERDNACTIVE_BITPOS));

        }

		if (camera_flash != INVALID_GPIO) {
            ret = gpio_request(camera_flash, "camera flash");
            if (ret) {
                printk("%s..%s..flash pin(%d) init failed\n",__FUNCTION__,plat_data->gpio_res[i].dev_name,camera_flash);
				goto sensor_io_init_erro;
            }

            if (rk_sensor_iomux(camera_flash) < 0) {
                printk(KERN_ERR "%s..%s..flash pin(%d) iomux init failed\n",__FUNCTION__,plat_data->gpio_res[i].dev_name,camera_flash);                
            }
            
			plat_data->gpio_res[i].gpio_init |= RK29_CAM_FLASHACTIVE_MASK;
            gpio_set_value(camera_flash, ((~camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));    /* falsh off */
            gpio_direction_output(camera_flash, ((~camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));

			dprintk("%s....flash pin(%d) init success(0x%x) \n",__FUNCTION__,camera_flash,((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));

        }  

        
        for (j=0; j<10; j++) {
            memset(&plat_data->info[i].fival[j],0x00,sizeof(struct v4l2_frmivalenum));
        }
        j=0;
        if (plat_data->info[i].dev_name && strstr(plat_data->info[i].dev_name,"_back")) {
            
            #if CONFIG_SENSOR_QCIF_FPS_FIXED_0
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_QCIF_FPS_FIXED_0;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 176;
            plat_data->info[i].fival[j].height = 144;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_QVGA_FPS_FIXED_0
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_QVGA_FPS_FIXED_0;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 320;
            plat_data->info[i].fival[j].height = 240;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_CIF_FPS_FIXED_0
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_CIF_FPS_FIXED_0;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 352;
            plat_data->info[i].fival[j].height = 288;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_VGA_FPS_FIXED_0
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_VGA_FPS_FIXED_0;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 640;
            plat_data->info[i].fival[j].height = 480;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_480P_FPS_FIXED_0
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_480P_FPS_FIXED_0;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 720;
            plat_data->info[i].fival[j].height = 480;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif            

            #if CONFIG_SENSOR_SVGA_FPS_FIXED_0
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_SVGA_FPS_FIXED_0;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 800;
            plat_data->info[i].fival[j].height = 600;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_720P_FPS_FIXED_0
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_720P_FPS_FIXED_0;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 1280;
            plat_data->info[i].fival[j].height = 720;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

        } else {
            #if CONFIG_SENSOR_QCIF_FPS_FIXED_1
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_QCIF_FPS_FIXED_1;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 176;
            plat_data->info[i].fival[j].height = 144;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_QVGA_FPS_FIXED_1
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_QVGA_FPS_FIXED_1;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 320;
            plat_data->info[i].fival[j].height = 240;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_CIF_FPS_FIXED_1
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_CIF_FPS_FIXED_1;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 352;
            plat_data->info[i].fival[j].height = 288;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_VGA_FPS_FIXED_1
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_VGA_FPS_FIXED_1;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 640;
            plat_data->info[i].fival[j].height = 480;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_480P_FPS_FIXED_1
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_480P_FPS_FIXED_1;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 720;
            plat_data->info[i].fival[j].height = 480;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif 

            #if CONFIG_SENSOR_SVGA_FPS_FIXED_1
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_SVGA_FPS_FIXED_1;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 800;
            plat_data->info[i].fival[j].height = 600;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif

            #if CONFIG_SENSOR_720P_FPS_FIXED_1
            plat_data->info[i].fival[j].discrete.denominator = CONFIG_SENSOR_720P_FPS_FIXED_1;
            plat_data->info[i].fival[j].discrete.numerator= 1;
            plat_data->info[i].fival[j].index = 0;
            plat_data->info[i].fival[j].pixel_format = V4L2_PIX_FMT_NV12;
            plat_data->info[i].fival[j].width = 1280;
            plat_data->info[i].fival[j].height = 720;
            plat_data->info[i].fival[j].type = V4L2_FRMIVAL_TYPE_DISCRETE;
            j++;
            #endif
        }
	continue;
	sensor_io_init_erro:
		rk_sensor_io_deinit(i);
	}
	return 0;
}

static int rk_sensor_io_deinit(int sensor)
{
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;
	unsigned int camera_powerdown = INVALID_GPIO, camera_flash = INVALID_GPIO;
	struct rk29camera_platform_data* plat_data = &rk_camera_platform_data;
    camera_reset = plat_data->gpio_res[sensor].gpio_reset;
    camera_power = plat_data->gpio_res[sensor].gpio_power;
	camera_powerdown = plat_data->gpio_res[sensor].gpio_powerdown;
    camera_flash = plat_data->gpio_res[sensor].gpio_flash;

    printk("%s..%s enter..\n",__FUNCTION__,plat_data->gpio_res[sensor].dev_name);

	if (plat_data->gpio_res[sensor].gpio_init & RK29_CAM_POWERACTIVE_MASK) {
	    if (camera_power != INVALID_GPIO) {
	        gpio_direction_input(camera_power);
	        gpio_free(camera_power);
	    }
	}

	if (plat_data->gpio_res[sensor].gpio_init & RK29_CAM_RESETACTIVE_MASK) {
	    if (camera_reset != INVALID_GPIO)  {
	        gpio_direction_input(camera_reset);
	        gpio_free(camera_reset);
	    }
	}

	if (plat_data->gpio_res[sensor].gpio_init & RK29_CAM_POWERDNACTIVE_MASK) {
	    if (camera_powerdown != INVALID_GPIO)  {
	        gpio_direction_input(camera_powerdown);
	        gpio_free(camera_powerdown);
	    }
	}

	if (plat_data->gpio_res[sensor].gpio_init & RK29_CAM_FLASHACTIVE_MASK) {
	    if (camera_flash != INVALID_GPIO)  {
	        gpio_direction_input(camera_flash);
	        gpio_free(camera_flash);
	    }
	}
	plat_data->gpio_res[sensor].gpio_init = 0;
	
    return 0;
}
static int rk_sensor_ioctrl(struct device *dev,enum rk29camera_ioctrl_cmd cmd, int on)
{
    struct rk29camera_gpio_res *res = NULL;    
	int ret = RK29_CAM_IO_SUCCESS,i = 0;

	struct rk29camera_platform_data* plat_data = &rk_camera_platform_data;
	//for test reg
	for(i = 0;i < RK_CAM_NUM;i++){
		if(plat_data->gpio_res[i].dev_name &&  (strcmp(plat_data->gpio_res[i].dev_name, dev_name(dev)) == 0)) {
				res = (struct rk29camera_gpio_res *)&plat_data->gpio_res[i];
				break;
		    } 
		} 
     if(i == RK_CAM_NUM){
		ret = RK29_CAM_EIO_INVALID;
		goto rk_sensor_ioctrl_end;
     	}
	
	switch (cmd)
 	{
 		case Cam_Power:
		{
			if (sensor_ioctl_cb.sensor_power_cb) {
                ret = sensor_ioctl_cb.sensor_power_cb(res, on);
			} else {
                printk(KERN_ERR "sensor_ioctl_cb.sensor_power_cb is NULL");
                WARN_ON(1);
			}
			break;
		}
		case Cam_Reset:
		{
			if (sensor_ioctl_cb.sensor_reset_cb) {
                ret = sensor_ioctl_cb.sensor_reset_cb(res, on);
			} else {
                printk(KERN_ERR "sensor_ioctl_cb.sensor_reset_cb is NULL");
                WARN_ON(1);
			}
			break;
		}

		case Cam_PowerDown:
		{
			if (sensor_ioctl_cb.sensor_powerdown_cb) {
                ret = sensor_ioctl_cb.sensor_powerdown_cb(res, on);
			} else {
                printk(KERN_ERR "sensor_ioctl_cb.sensor_powerdown_cb is NULL");
                WARN_ON(1);
			}
			break;
		}

		case Cam_Flash:
		{
			if (sensor_ioctl_cb.sensor_flash_cb) {
                ret = sensor_ioctl_cb.sensor_flash_cb(res, on);
			} else {
                printk(KERN_ERR "sensor_ioctl_cb.sensor_flash_cb is NULL!");
                WARN_ON(1);
			}
			break;
		}
		default:
		{
			printk("%s cmd(0x%x) is unknown!\n",__FUNCTION__, cmd);
			break;
		}
 	}
rk_sensor_ioctrl_end:
    return ret;
}
static int rk_sensor_power(struct device *dev, int on)
{
	rk_sensor_ioctrl(dev,Cam_Power,on);
    return 0;
}
#if (CONFIG_SENSOR_RESET_PIN_0 != INVALID_GPIO) || (CONFIG_SENSOR_RESET_PIN_1 != INVALID_GPIO)
static int rk_sensor_reset(struct device *dev)
{
	rk_sensor_ioctrl(dev,Cam_Reset,1);
	msleep(2);
	rk_sensor_ioctrl(dev,Cam_Reset,0);
	return 0;
}
#endif
static int rk_sensor_powerdown(struct device *dev, int on)
{
	return rk_sensor_ioctrl(dev,Cam_PowerDown,on);
}

static struct android_pmem_platform_data android_pmem_cam_pdata = {
	.name		= "pmem_cam",
	//.start		= PMEM_CAM_BASE,
	.size		= 0,
	.no_allocator	= 1,
	.cached		= 1,
};

 struct platform_device android_pmem_cam_device = {
	.name		= "android_pmem",
	.id		= 1,
	.dev		= {
		.platform_data = &android_pmem_cam_pdata,
	},
};
 static void rk30_camera_request_reserve_mem(void)
{
#if (MEM_CAMIPP_SIZE_CIF_0 != 0)
	#if CONFIG_USE_CIF_0
		 rk_camera_platform_data.meminfo.name = "camera_ipp_mem_0";
		 rk_camera_platform_data.meminfo.start = board_mem_reserve_add("camera_ipp_mem_0",MEM_CAMIPP_SIZE_CIF_0);
		 rk_camera_platform_data.meminfo.size= MEM_CAMIPP_SIZE_CIF_0;
	#endif
#endif

#if (MEM_CAMIPP_SIZE_CIF_1 != 0)
      #if CONFIG_USE_CIF_1
	 rk_camera_platform_data.meminfo_cif1.name = "camera_ipp_mem_1";
	 rk_camera_platform_data.meminfo_cif1.start =board_mem_reserve_add("camera_ipp_mem_1",MEM_CAMIPP_SIZE_CIF_1);
	 rk_camera_platform_data.meminfo_cif1.size= MEM_CAMIPP_SIZE_CIF_1;
      #endif
#endif

#if (PMEM_CAM_SIZE != 0)
		 android_pmem_cam_pdata.start = board_mem_reserve_add("camera_pmem",PMEM_CAM_SIZE);
		 android_pmem_cam_pdata.size = PMEM_CAM_SIZE;
#endif
}
static int rk_register_camera_devices(void)
{
	rk_init_camera_plateform_data();
#if CONFIG_USE_CIF_0
	platform_device_register(&rk_device_camera_host_0);
#endif
#if CONFIG_USE_CIF_1
	platform_device_register(&rk_device_camera_host_1);
#endif
	if(rk_soc_camera_pdrv_0.name)
		platform_device_register(&rk_soc_camera_pdrv_0);
	if(rk_soc_camera_pdrv_1.name)
		platform_device_register(&rk_soc_camera_pdrv_1);
	if(((struct android_pmem_platform_data*)(android_pmem_cam_device.dev.platform_data))->size)
		platform_device_register(&android_pmem_cam_device);
	return 0;
}

module_init(rk_register_camera_devices);
#endif

#endif //#ifdef CONFIG_VIDEO_RK
