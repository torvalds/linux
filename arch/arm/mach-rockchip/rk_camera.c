#include "rk_camera.h"
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/of_gpio.h>
//**********yzm***********//
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/module.h>
//**********yzm***********//

#define PMEM_CAM_NECESSARY	 0x00000000    //yzm

static int camio_version = KERNEL_VERSION(0,1,9);//yzm camio_version
module_param(camio_version, int, S_IRUGO);

static int camera_debug = 0;//yzm camera_debug
module_param(camera_debug, int, S_IRUGO|S_IWUSR);    

#undef  CAMMODULE_NAME
#define CAMMODULE_NAME   "rk_cam_io"

#define ddprintk(level, fmt, arg...) do {			\
	if (camera_debug >= level) 					\
	    printk(KERN_WARNING"%s(%d):" fmt"\n", CAMMODULE_NAME,__LINE__,## arg); } while (0)

#define dprintk(format, ...) ddprintk(1, format, ## __VA_ARGS__)  
#define eprintk(format, ...) printk(KERN_ERR "%s(%d):" format"\n",CAMMODULE_NAME,__LINE__,## __VA_ARGS__)  
#define debug_printk(format, ...) ddprintk(3, format, ## __VA_ARGS__)  

static int rk_sensor_io_init(void);
static int rk_sensor_io_deinit(int sensor);
static int rk_sensor_ioctrl(struct device *dev,enum rk29camera_ioctrl_cmd cmd, int on);
static int rk_sensor_power(struct device *dev, int on);
static int rk_sensor_register(void);
//static int rk_sensor_reset(struct device *dev);
static int rk_sensor_powerdown(struct device *dev, int on);

static struct rk29camera_platform_data rk_camera_platform_data = {
    .io_init = rk_sensor_io_init,
    .io_deinit = rk_sensor_io_deinit,
    .sensor_ioctrl = rk_sensor_ioctrl,
    .sensor_register = rk_sensor_register,

};


static u64 rockchip_device_camera_dmamask = 0xffffffffUL;
#if RK_SUPPORT_CIF0
static struct resource rk_camera_resource_host_0[2] = {};
#endif
#if RK_SUPPORT_CIF1
static struct resource rk_camera_resource_host_1[2] = {};
#endif

#if RK_SUPPORT_CIF0
 struct platform_device rk_device_camera_host_0 = {
	.name		  = RK29_CAM_DRV_NAME,
	.id 	  = RK_CAM_PLATFORM_DEV_ID_0,				// This is used to put cameras on this interface 
	.num_resources= 2,
	.resource	  = rk_camera_resource_host_0,//yzm
	.dev			= {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data	= &rk_camera_platform_data,
	}
};
#endif

#if RK_SUPPORT_CIF1
 struct platform_device rk_device_camera_host_1 = {
	.name		  = RK29_CAM_DRV_NAME,
	.id 	  = RK_CAM_PLATFORM_DEV_ID_1,				// This is used to put cameras on this interface 
	.num_resources	  = ARRAY_SIZE(rk_camera_resource_host_1),
	.resource	  = rk_camera_resource_host_1,//yzm
	.dev			= {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data	= &rk_camera_platform_data,
	}
};
#endif



static const struct of_device_id of_match_cif[] = {
    { .compatible = "rockchip,cif" },
	{},
};

MODULE_DEVICE_TABLE(of,of_match_cif);
static struct platform_driver rk_cif_driver =
{
    .driver 	= {
        .name	= RK3288_CIF_NAME,              
		.owner = THIS_MODULE,
        .of_match_table = of_match_ptr(of_match_cif),
    },
    .probe		= rk_dts_cif_probe,
    .remove		= rk_dts_cif_remove,
};

static const struct of_device_id of_match_sensor[] = {
    { .compatible = "rockchip,sensor" },
};
MODULE_DEVICE_TABLE(of,of_match_sensor);
static struct platform_driver rk_sensor_driver =
{
    .driver 	= {
        .name	= RK3288_SENSOR_NAME,              
		.owner  = THIS_MODULE,
        .of_match_table = of_match_ptr(of_match_sensor),
    },
    .probe		= rk_dts_sensor_probe,
    .remove		= rk_dts_sensor_remove,
};

//************yzm***************

static int rk_dts_sensor_remove(struct platform_device *pdev)
{
	return 0;
}
static int	rk_dts_sensor_probe(struct platform_device *pdev)
{
	struct device_node *np, *cp;
	int sensor_num = 0;
	struct device *dev = &pdev->dev;
	struct rkcamera_platform_data *new_camera_list;
	
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);
	np = dev->of_node;
	if (!np)
		return -ENODEV;
	
	for_each_child_of_node(np, cp) {
		u32 flash_attach,mir,i2c_rata,i2c_chl,i2c_add,cif_chl,mclk_rate,is_front;
		u32 resolution,pwdn_info,powerup_sequence;
		
		u32	powerdown = INVALID_GPIO,power = INVALID_GPIO,reset = INVALID_GPIO;
		u32 af = INVALID_GPIO,flash = INVALID_GPIO;
		struct rkcamera_platform_data *new_camera; 
		new_camera = kzalloc(sizeof(struct rkcamera_platform_data),GFP_KERNEL);
		if(!sensor_num)
		{			
			new_camera_head = new_camera;
			rk_camera_platform_data.register_dev_new = new_camera_head;
			new_camera_list = new_camera;
		}
		sensor_num ++;
		new_camera_list->next_camera = new_camera;
		new_camera_list = new_camera;
	
		if (of_property_read_u32(cp, "flash_attach", &flash_attach)) {
				printk("%s flash_attach %d \n", cp->name, flash_attach);
		}
		if (of_property_read_u32(cp, "mir", &mir)) {
				printk("%s mir %d \n", cp->name, mir);
		}
		if (of_property_read_u32(cp, "i2c_rata", &i2c_rata)) {
				printk("%s i2c_rata %d \n", cp->name, i2c_rata);
		}
		if (of_property_read_u32(cp, "i2c_chl", &i2c_chl)) {
				printk("%s i2c_chl %d \n", cp->name, i2c_chl);
		}
		if (of_property_read_u32(cp, "cif_chl", &cif_chl)) {
				printk("%s cif_chl %d \n", cp->name, cif_chl);
		}
		if (of_property_read_u32(cp, "mclk_rate", &mclk_rate)) {
				printk("%s mclk_rate %d \n", cp->name, mclk_rate);
		}
		if (of_property_read_u32(cp, "is_front", &is_front)) {
				printk("%s is_front %d \n", cp->name, is_front);
		}
		if (of_property_read_u32(cp, "rockchip,powerdown", &powerdown)) {
				printk("%s:Get %s rockchip,powerdown failed!\n",__func__, cp->name);				
		}
		if (of_property_read_u32(cp, "rockchip,power", &power)) {
				printk("%s:Get %s rockchip,power failed!\n",__func__, cp->name);				
		}
		if (of_property_read_u32(cp, "rockchip,reset", &reset)) {
				printk("%s:Get %s rockchip,reset failed!\n",__func__, cp->name);				
		}
		if (of_property_read_u32(cp, "rockchip,af", &af)) {
				printk("%s:Get %s rockchip,af failed!\n",__func__, cp->name);				
		}
		if (of_property_read_u32(cp, "rockchip,flash", &flash)) {
				printk("%s:Get %s rockchip,flash failed!\n",__func__, cp->name);				
		}
		if (of_property_read_u32(cp, "i2c_add", &i2c_add)) {
				printk("%s i2c_add %d \n", cp->name, i2c_add);
		}
		if (of_property_read_u32(cp, "resolution", &resolution)) {
				printk("%s resolution %d \n", cp->name, resolution);
		}
		if (of_property_read_u32(cp, "pwdn_info", &pwdn_info)) {
				printk("%s pwdn_info %d \n", cp->name, pwdn_info);
		}
		if (of_property_read_u32(cp, "powerup_sequence", &powerup_sequence)) {
				printk("%s powerup_sequence %d \n", cp->name, powerup_sequence);
		}

		strcpy(new_camera->dev.i2c_cam_info.type, cp->name);
		new_camera->dev.i2c_cam_info.addr = i2c_add>>1;
		new_camera->dev.desc_info.host_desc.bus_id = RK29_CAM_PLATFORM_DEV_ID+cif_chl;//yzm
		new_camera->dev.desc_info.host_desc.i2c_adapter_id = i2c_chl;//yzm
		new_camera->dev.desc_info.host_desc.module_name = cp->name;//const
		new_camera->dev.device_info.name = "soc-camera-pdrv";
		if(is_front)
			sprintf(new_camera->dev_name,"%s_%s",cp->name,"front");
		else
			sprintf(new_camera->dev_name,"%s_%s",cp->name,"back");
		new_camera->dev.device_info.dev.init_name =(const char*)&new_camera->dev_name[0];
		new_camera->io.gpio_reset = reset;
		new_camera->io.gpio_powerdown = powerdown;
		new_camera->io.gpio_power = power;
		new_camera->io.gpio_af = af;
		new_camera->io.gpio_flash = flash;
		new_camera->io.gpio_flag = ((INVALID_GPIO&0x01)<<RK29_CAM_POWERACTIVE_BITPOS)|((INVALID_GPIO&0x01)<<RK29_CAM_RESETACTIVE_BITPOS)|((pwdn_info&0x01)<<RK29_CAM_POWERDNACTIVE_BITPOS);
		new_camera->orientation = INVALID_GPIO;
		new_camera->resolution = resolution;
		new_camera->mirror = mir;
		new_camera->i2c_rate = i2c_rata;
		new_camera->flash = flash_attach;
		new_camera->pwdn_info = ((pwdn_info&0x10)|0x01);
		new_camera->powerup_sequence = powerup_sequence;
		new_camera->mclk_rate = mclk_rate;
		new_camera->of_node = cp;
			
		//	device = container_of(&(new_camera[sensor_num].dev.desc_info),rk_camera_device_register_info_t,desc_info);
		//	debug_printk( "sensor num %d ,desc_info point %p +++++++\n",sensor_num,&(new_camera.dev.desc_info));
		//	debug_printk( "sensor num %d ,dev %p +++++++\n",sensor_num,&(new_camera.dev));
		//	debug_printk( "sensor num %d ,device point %p +++++++\n",sensor_num,device);
		    debug_printk( "******************* /n power = %x\n", power);
			debug_printk( "******************* /n powerdown = %x\n", powerdown);
			debug_printk( "******************* /n i2c_add = %x\n", new_camera->dev.i2c_cam_info.addr << 1);
			debug_printk( "******************* /n i2c_chl = %d\n", new_camera->dev.desc_info.host_desc.i2c_adapter_id);
			debug_printk( "******************* /n init_name = %s\n", new_camera->dev.device_info.dev.init_name);
			debug_printk( "******************* /n dev_name = %s\n", new_camera->dev_name);
			debug_printk( "******************* /n module_name = %s\n", new_camera->dev.desc_info.host_desc.module_name);
	};
	new_camera_list->next_camera = NULL;
	return 0;
}
	
static int rk_dts_cif_remove(struct platform_device *pdev)
{
	 return 0;
}
	
static int rk_dts_cif_probe(struct platform_device *pdev) //yzm
{
	int irq,err;
	struct device *dev = &pdev->dev;
		
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);
	
	rk_camera_platform_data.cif_dev = &pdev->dev;
	
	err = of_address_to_resource(dev->of_node, 0, &rk_camera_resource_host_0[0]);
	if (err < 0){
		printk(KERN_EMERG "Get register resource from %s platform device failed!",pdev->name);
		return -ENODEV;
	}
	rk_camera_resource_host_0[0].flags = IORESOURCE_MEM;
	//map irqs
	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (irq < 0) {
		printk(KERN_EMERG "Get irq resource from %s platform device failed!",pdev->name);
		return -ENODEV;;
	}
	rk_camera_resource_host_0[1].start = irq;
	rk_camera_resource_host_0[1].end   = irq;
	rk_camera_resource_host_0[1].flags = IORESOURCE_IRQ;
	//debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n res = [%x--%x] \n",rk_camera_resource_host_0[0].start , rk_camera_resource_host_0[0].end);
	//debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n irq_num = %d\n",irq);
	return 0;
}
	
static int rk_cif_sensor_init(void)
{
	
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);
	platform_driver_register(&rk_cif_driver);	
		
	platform_driver_register(&rk_sensor_driver);	

	return 0;
}
	

static int sensor_power_default_cb (struct rk29camera_gpio_res *res, int on)
{
    int camera_power = res->gpio_power;
    int camera_ioflag = res->gpio_flag;
    int camera_io_init = res->gpio_init;
    int ret = 0;

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    
    if (camera_power != INVALID_GPIO)  {
		if (camera_io_init & RK29_CAM_POWERACTIVE_MASK) {
            if (on) {
            	//gpio_set_value(camera_power, ((camera_ioflag&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
				gpio_direction_output(camera_power,1);
				dprintk("%s PowerPin=%d ..PinLevel = %x",res->dev_name, camera_power, ((camera_ioflag&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
    			msleep(10);
    		} else {
    			//gpio_set_value(camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
				gpio_direction_output(camera_power,0);
				dprintk("%s PowerPin=%d ..PinLevel = %x",res->dev_name, camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
    		}
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			eprintk("%s PowerPin=%d request failed!", res->dev_name,camera_power);
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

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    
    if (camera_reset != INVALID_GPIO) {
		if (camera_io_init & RK29_CAM_RESETACTIVE_MASK) {
			if (on) {
	        	gpio_set_value(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
	        	dprintk("%s ResetPin=%d ..PinLevel = %x",res->dev_name,camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
			} else {
				gpio_set_value(camera_reset,(((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
        		dprintk("%s ResetPin= %d..PinLevel = %x",res->dev_name, camera_reset, (((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
	        }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			eprintk("%s ResetPin=%d request failed!", res->dev_name,camera_reset);
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

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


    if (camera_powerdown != INVALID_GPIO) {
		if (camera_io_init & RK29_CAM_POWERDNACTIVE_MASK) {
			if (on) {
	        	gpio_set_value(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
	        	dprintk("%s PowerDownPin=%d ..PinLevel = %x" ,res->dev_name,camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
			} else {
				gpio_set_value(camera_powerdown,(((~camera_ioflag)&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
        		dprintk("%s PowerDownPin= %d..PinLevel = %x" ,res->dev_name, camera_powerdown, (((~camera_ioflag)&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
	        }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			dprintk("%s PowerDownPin=%d request failed!", res->dev_name,camera_powerdown);
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

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


    if (camera_flash != INVALID_GPIO) {
		if (camera_io_init & RK29_CAM_FLASHACTIVE_MASK) {
            switch (on)
            {
                case Flash_Off:
                {
                    gpio_set_value(camera_flash,(((~camera_ioflag)&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
        		    dprintk("%s FlashPin= %d..PinLevel = %x", res->dev_name, camera_flash, (((~camera_ioflag)&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS)); 
        		    break;
                }

                case Flash_On:
                {
                    gpio_set_value(camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    dprintk("%s FlashPin=%d ..PinLevel = %x", res->dev_name,camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    break;
                }

                case Flash_Torch:
                {
                    gpio_set_value(camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    dprintk("%s FlashPin=%d ..PinLevel = %x", res->dev_name,camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    break;
                }

                default:
                {
                    eprintk("%s Flash command(%d) is invalidate", res->dev_name,on);
                    break;
                }
            }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			eprintk("%s FlashPin=%d request failed!", res->dev_name,camera_flash);
		}
    } else {
		ret = RK29_CAM_EIO_INVALID;
    }
    return ret;
}

static int sensor_afpower_default_cb (struct rk29camera_gpio_res *res, int on)
{
	int ret = 0;   
	int camera_af = res->gpio_af;
	
debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	
	if (camera_af != INVALID_GPIO) {
		gpio_set_value(camera_af, on);
	}

	return ret;
}

/*
static void rk29_sensor_fps_get(int idx, unsigned int *val, int w, int h)
{

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);


    switch (idx)
    {
        #ifdef CONFIG_SENSOR_0
        case 0:
        {
            if ((w==176) && (h==144)) {
                *val = CONFIG_SENSOR_QCIF_FPS_FIXED_0;
            #ifdef CONFIG_SENSOR_240X160_FPS_FIXED_0
            } else if ((w==240) && (h==160)) {
                *val = CONFIG_SENSOR_240X160_FPS_FIXED_0;
            #endif
            } else if ((w==320) && (h==240)) {
                *val = CONFIG_SENSOR_QVGA_FPS_FIXED_0;
            } else if ((w==352) && (h==288)) {
                *val = CONFIG_SENSOR_CIF_FPS_FIXED_0;
            } else if ((w==640) && (h==480)) {
                *val = CONFIG_SENSOR_VGA_FPS_FIXED_0;
            } else if ((w==720) && (h==480)) {
                *val = CONFIG_SENSOR_480P_FPS_FIXED_0;
            } else if ((w==800) && (h==600)) {
                *val = CONFIG_SENSOR_SVGA_FPS_FIXED_0;
            } else if ((w==1280) && (h==720)) {
                *val = CONFIG_SENSOR_720P_FPS_FIXED_0;
            }
            break;
        }
        #endif
        #ifdef CONFIG_SENSOR_1
        case 1:
        {
            if ((w==176) && (h==144)) {
                *val = CONFIG_SENSOR_QCIF_FPS_FIXED_1;
            #ifdef CONFIG_SENSOR_240X160_FPS_FIXED_1
            } else if ((w==240) && (h==160)) {
                *val = CONFIG_SENSOR_240X160_FPS_FIXED_1;
            #endif
            } else if ((w==320) && (h==240)) {
                *val = CONFIG_SENSOR_QVGA_FPS_FIXED_1;
            } else if ((w==352) && (h==288)) {
                *val = CONFIG_SENSOR_CIF_FPS_FIXED_1;
            } else if ((w==640) && (h==480)) {
                *val = CONFIG_SENSOR_VGA_FPS_FIXED_1;
            } else if ((w==720) && (h==480)) {
                *val = CONFIG_SENSOR_480P_FPS_FIXED_1;
            } else if ((w==800) && (h==600)) {
                *val = CONFIG_SENSOR_SVGA_FPS_FIXED_1;
            } else if ((w==1280) && (h==720)) {
                *val = CONFIG_SENSOR_720P_FPS_FIXED_1;
            }
            break;
        }
        #endif
        #ifdef CONFIG_SENSOR_01
        case 2:
        {
            if ((w==176) && (h==144)) {
                *val = CONFIG_SENSOR_QCIF_FPS_FIXED_01;
            #ifdef CONFIG_SENSOR_240X160_FPS_FIXED_01
            } else if ((w==240) && (h==160)) {
                *val = CONFIG_SENSOR_240X160_FPS_FIXED_01;
            #endif
            } else if ((w==320) && (h==240)) {
                *val = CONFIG_SENSOR_QVGA_FPS_FIXED_01;
            } else if ((w==352) && (h==288)) {
                *val = CONFIG_SENSOR_CIF_FPS_FIXED_01;
            } else if ((w==640) && (h==480)) {
                *val = CONFIG_SENSOR_VGA_FPS_FIXED_01;
            } else if ((w==720) && (h==480)) {
                *val = CONFIG_SENSOR_480P_FPS_FIXED_01;
            } else if ((w==800) && (h==600)) {
                *val = CONFIG_SENSOR_SVGA_FPS_FIXED_01;
            } else if ((w==1280) && (h==720)) {
                *val = CONFIG_SENSOR_720P_FPS_FIXED_01;
            }
            break;
        }
        #endif
        #ifdef CONFIG_SENSOR_02
        case 3:
        {
            if ((w==176) && (h==144)) {
                *val = CONFIG_SENSOR_QCIF_FPS_FIXED_02;
            #ifdef CONFIG_SENSOR_240X160_FPS_FIXED_02
            } else if ((w==240) && (h==160)) {
                *val = CONFIG_SENSOR_240X160_FPS_FIXED_02;
            #endif
            } else if ((w==320) && (h==240)) {
                *val = CONFIG_SENSOR_QVGA_FPS_FIXED_02;
            } else if ((w==352) && (h==288)) {
                *val = CONFIG_SENSOR_CIF_FPS_FIXED_02;
            } else if ((w==640) && (h==480)) {
                *val = CONFIG_SENSOR_VGA_FPS_FIXED_02;
            } else if ((w==720) && (h==480)) {
                *val = CONFIG_SENSOR_480P_FPS_FIXED_02;
            } else if ((w==800) && (h==600)) {
                *val = CONFIG_SENSOR_SVGA_FPS_FIXED_02;
            } else if ((w==1280) && (h==720)) {
                *val = CONFIG_SENSOR_720P_FPS_FIXED_02;
            }
            break;
        }
        #endif
        
        #ifdef CONFIG_SENSOR_11
        case 4:
        {
            if ((w==176) && (h==144)) {
                *val = CONFIG_SENSOR_QCIF_FPS_FIXED_11;
            #ifdef CONFIG_SENSOR_240X160_FPS_FIXED_11
            } else if ((w==240) && (h==160)) {
                *val = CONFIG_SENSOR_240X160_FPS_FIXED_11;
            #endif
            } else if ((w==320) && (h==240)) {
                *val = CONFIG_SENSOR_QVGA_FPS_FIXED_11;
            } else if ((w==352) && (h==288)) {
                *val = CONFIG_SENSOR_CIF_FPS_FIXED_11;
            } else if ((w==640) && (h==480)) {
                *val = CONFIG_SENSOR_VGA_FPS_FIXED_11;
            } else if ((w==720) && (h==480)) {
                *val = CONFIG_SENSOR_480P_FPS_FIXED_11;
            } else if ((w==800) && (h==600)) {
                *val = CONFIG_SENSOR_SVGA_FPS_FIXED_11;
            } else if ((w==1280) && (h==720)) {
                *val = CONFIG_SENSOR_720P_FPS_FIXED_11;
            }
            break;
        }
        #endif
        #ifdef CONFIG_SENSOR_12
        case 5:
        {
            if ((w==176) && (h==144)) {
                *val = CONFIG_SENSOR_QCIF_FPS_FIXED_12;
            #ifdef CONFIG_SENSOR_240X160_FPS_FIXED_12
            } else if ((w==240) && (h==160)) {
                *val = CONFIG_SENSOR_240X160_FPS_FIXED_12;
            #endif
            } else if ((w==320) && (h==240)) {
                *val = CONFIG_SENSOR_QVGA_FPS_FIXED_12;
            } else if ((w==352) && (h==288)) {
                *val = CONFIG_SENSOR_CIF_FPS_FIXED_12;
            } else if ((w==640) && (h==480)) {
                *val = CONFIG_SENSOR_VGA_FPS_FIXED_12;
            } else if ((w==720) && (h==480)) {
                *val = CONFIG_SENSOR_480P_FPS_FIXED_12;
            } else if ((w==800) && (h==600)) {
                *val = CONFIG_SENSOR_SVGA_FPS_FIXED_12;
            } else if ((w==1280) && (h==720)) {
                *val = CONFIG_SENSOR_720P_FPS_FIXED_12;
            }
            break;
        }
        #endif
        default:
            eprintk(" sensor-%d have not been define in board file!",idx);
    }
}
*/
static int _rk_sensor_io_init_(struct rk29camera_gpio_res *gpio_res,struct device_node *of_node)
{
    int ret = 0;
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;
	unsigned int camera_powerdown = INVALID_GPIO, camera_flash = INVALID_GPIO;
	unsigned int camera_af = INVALID_GPIO,camera_ioflag;
    struct rk29camera_gpio_res *io_res;
    bool io_requested_in_camera;
	enum of_gpio_flags flags;
	
	struct rkcamera_platform_data *new_camera;//yzm
	//struct device_node *parent_node;
	debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);


    camera_reset = gpio_res->gpio_reset;
	camera_power = gpio_res->gpio_power;
	camera_powerdown = gpio_res->gpio_powerdown;
	camera_flash = gpio_res->gpio_flash;
	camera_af = gpio_res->gpio_af;	
	camera_ioflag = gpio_res->gpio_flag;
	gpio_res->gpio_init = 0;

    if (camera_power != INVALID_GPIO) {
		debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$/ camera_power  = %x\n", camera_power );

		camera_power = of_get_named_gpio_flags(of_node,"rockchip,power",0,&flags);//yzm
		gpio_res->gpio_power = camera_power;//yzm,将io的完整信息传回去。

		
		//dev->of_node = parent_node;//将dev->of_node还原
		debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$/ camera_power  = %x\n", camera_power );  

		ret = gpio_request(camera_power, "camera power");    //申请名为"camera power"的io管脚
        if (ret) {
			
            io_requested_in_camera = false;

            if (io_requested_in_camera==false) {

				new_camera = new_camera_head;
                while (new_camera != NULL) {
                    io_res = &new_camera->io;
                    if (io_res->gpio_init & RK29_CAM_POWERACTIVE_MASK) {
                        if (io_res->gpio_power == camera_power)
                            io_requested_in_camera = true;    
                    }
                    new_camera = new_camera->next_camera;
                }

            }
            
            if (io_requested_in_camera==false) {
                printk( "%s power pin(%d) init failed\n", gpio_res->dev_name,camera_power);
                goto _rk_sensor_io_init_end_;
            } else {
                ret =0;
            }
        }
      
		gpio_res->gpio_init |= RK29_CAM_POWERACTIVE_MASK;
        gpio_set_value(camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
        gpio_direction_output(camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));

		dprintk("%s power pin(%d) init success(0x%x)" ,gpio_res->dev_name,camera_power,(((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));

    }
/*
    if (camera_reset != INVALID_GPIO) {
        ret = gpio_request(camera_reset, "camera reset");
        if (ret) {
            io_requested_in_camera = false;
            for (i=0; i<RK_CAM_NUM; i++) {
                io_res = &rk_camera_platform_data.gpio_res[i];
                if (io_res->gpio_init & RK29_CAM_RESETACTIVE_MASK) {
                    if (io_res->gpio_reset == camera_reset)
                        io_requested_in_camera = true;    
                }
            }

            if (io_requested_in_camera==false) {
                i=0;
                while (strstr(new_camera[i].dev_name,"end")==NULL) {
                    io_res = &new_camera[i].io;
                    if (io_res->gpio_init & RK29_CAM_RESETACTIVE_MASK) {
                        if (io_res->gpio_reset == camera_reset)
                            io_requested_in_camera = true;    
                    }
                    i++;
                }
            }
            
            if (io_requested_in_camera==false) {
                eprintk("%s reset pin(%d) init failed" ,gpio_res->dev_name,camera_reset);
                goto _rk_sensor_io_init_end_;
            } else {
                ret =0;
            }
        }

        if (rk_camera_platform_data.iomux(camera_reset,dev) < 0) {
            ret = -1;
            eprintk("%s reset pin(%d) iomux init failed", gpio_res->dev_name,camera_reset);
            goto _rk_sensor_io_init_end_;
        }
        
		gpio_res->gpio_init |= RK29_CAM_RESETACTIVE_MASK;
        gpio_set_value(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
        gpio_direction_output(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));

		dprintk("%s reset pin(%d) init success(0x%x)" ,gpio_res->dev_name,camera_reset,((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));

    }
*/
	if (camera_powerdown != INVALID_GPIO) {
		debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$/ camera_powerdown  = %x\n", camera_powerdown );

		camera_powerdown = of_get_named_gpio_flags(of_node,"rockchip,powerdown",0,&flags);//yzm
		gpio_res->gpio_powerdown = camera_powerdown;//yzm,将io的完整信息传回去。

		debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$/ camera_powerdown  = %x\n", camera_powerdown );  
		ret = gpio_request(camera_powerdown, "camera powerdown");
        if (ret) {
            io_requested_in_camera = false;

            if (io_requested_in_camera==false) {
				
                new_camera = new_camera_head;
                while (new_camera != NULL) {
                    io_res = &new_camera->io;
                    if (io_res->gpio_init & RK29_CAM_POWERDNACTIVE_MASK) {
                        if (io_res->gpio_powerdown == camera_powerdown)
                            io_requested_in_camera = true;    
                    }
                    new_camera = new_camera->next_camera;
                }
            }
            
            if (io_requested_in_camera==false) {
                eprintk("%s powerdown pin(%d) init failed",gpio_res->dev_name,camera_powerdown);
                goto _rk_sensor_io_init_end_;
            } else {
                ret =0;
            }
        }
		debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s(),iomux is ok\n", __FILE__, __LINE__,__FUNCTION__);
        
		gpio_res->gpio_init |= RK29_CAM_POWERDNACTIVE_MASK;
        gpio_set_value(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
        gpio_direction_output(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));

		dprintk("%s powerdown pin(%d) init success(0x%x)" ,gpio_res->dev_name,camera_powerdown,((camera_ioflag&RK29_CAM_POWERDNACTIVE_BITPOS)>>RK29_CAM_POWERDNACTIVE_BITPOS));

    }
/*
	if (camera_flash != INVALID_GPIO) {
        ret = gpio_request(camera_flash, "camera flash");
        if (ret) {
            io_requested_in_camera = false;
            for (i=0; i<RK_CAM_NUM; i++) {
                io_res = &rk_camera_platform_data.gpio_res[i];
                if (io_res->gpio_init & RK29_CAM_POWERDNACTIVE_MASK) {
                    if (io_res->gpio_powerdown == camera_powerdown)
                        io_requested_in_camera = true;    
                }
            }

            if (io_requested_in_camera==false) {
                i=0;
                while (strstr(new_camera[i].dev_name,"end")==NULL) {
                    io_res = &new_camera[i].io;
                    if (io_res->gpio_init & RK29_CAM_POWERDNACTIVE_MASK) {
                        if (io_res->gpio_powerdown == camera_powerdown)
                            io_requested_in_camera = true;    
                    }
                    i++;
                }
            }
            
            ret = 0;        //ddl@rock-chips.com : flash is only a function, sensor is also run;
            if (io_requested_in_camera==false) {
                eprintk("%s flash pin(%d) init failed",gpio_res->dev_name,camera_flash);
                goto _rk_sensor_io_init_end_;
            }
        }


        if (rk_camera_platform_data.iomux(camera_flash,dev) < 0) {
            printk("%s flash pin(%d) iomux init failed\n",gpio_res->dev_name,camera_flash);                            
        }
        
		gpio_res->gpio_init |= RK29_CAM_FLASHACTIVE_MASK;
        gpio_set_value(camera_flash, ((~camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));   //  falsh off 
        gpio_direction_output(camera_flash, ((~camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));

		dprintk("%s flash pin(%d) init success(0x%x)",gpio_res->dev_name, camera_flash,((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));

    }  


	if (camera_af != INVALID_GPIO) {
		ret = gpio_request(camera_af, "camera af");
		if (ret) {
			io_requested_in_camera = false;
			for (i=0; i<RK_CAM_NUM; i++) {
				io_res = &rk_camera_platform_data.gpio_res[i];
				if (io_res->gpio_init & RK29_CAM_AFACTIVE_MASK) {
					if (io_res->gpio_af == camera_af)
						io_requested_in_camera = true;	  
				}
			}

			if (io_requested_in_camera==false) {
				i=0;
				while (strstr(new_camera[i].dev_name,"end")==NULL) {
					io_res = &new_camera[i].io;
					if (io_res->gpio_init & RK29_CAM_AFACTIVE_MASK) {
						if (io_res->gpio_af == camera_af)
							io_requested_in_camera = true;	  
					}
					i++;
				}
			}
			
			if (io_requested_in_camera==false) {
				eprintk("%s af pin(%d) init failed",gpio_res->dev_name,camera_af);
				goto _rk_sensor_io_init_end_;
			} else {
                ret =0;
            }
			
		}


		if (rk_camera_platform_data.iomux(camera_af,dev) < 0) {
			 ret = -1;
			eprintk("%s af pin(%d) iomux init failed\n",gpio_res->dev_name,camera_af); 	
            goto _rk_sensor_io_init_end_;			
		}
		
		gpio_res->gpio_init |= RK29_CAM_AFACTIVE_MASK;
		//gpio_direction_output(camera_af, ((camera_ioflag&RK29_CAM_AFACTIVE_MASK)>>RK29_CAM_AFACTIVE_BITPOS));
		dprintk("%s af pin(%d) init success",gpio_res->dev_name, camera_af);

	}
*/

	
_rk_sensor_io_init_end_:
    return ret;

}

static int _rk_sensor_io_deinit_(struct rk29camera_gpio_res *gpio_res)
{
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;
	unsigned int camera_powerdown = INVALID_GPIO, camera_flash = INVALID_GPIO,camera_af = INVALID_GPIO;

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

    
    camera_reset = gpio_res->gpio_reset;
    camera_power = gpio_res->gpio_power;
	camera_powerdown = gpio_res->gpio_powerdown;
    camera_flash = gpio_res->gpio_flash;
    camera_af = gpio_res->gpio_af;

	if (gpio_res->gpio_init & RK29_CAM_POWERACTIVE_MASK) {
	    if (camera_power != INVALID_GPIO) {
	        gpio_direction_input(camera_power);
	        gpio_free(camera_power);
	    }
	}

	if (gpio_res->gpio_init & RK29_CAM_RESETACTIVE_MASK) {
	    if (camera_reset != INVALID_GPIO)  {
	        gpio_direction_input(camera_reset);
	        gpio_free(camera_reset);
	    }
	}

	if (gpio_res->gpio_init & RK29_CAM_POWERDNACTIVE_MASK) {
	    if (camera_powerdown != INVALID_GPIO)  {
	        gpio_direction_input(camera_powerdown);
	        gpio_free(camera_powerdown);
	    }
	}

	if (gpio_res->gpio_init & RK29_CAM_FLASHACTIVE_MASK) {
	    if (camera_flash != INVALID_GPIO)  {
	        gpio_direction_input(camera_flash);
	        gpio_free(camera_flash);
	    }
	}
	if (gpio_res->gpio_init & RK29_CAM_AFACTIVE_MASK) {
	    if (camera_af != INVALID_GPIO)  {
	       // gpio_direction_input(camera_af);
	        gpio_free(camera_af);
	    }
	}	
	gpio_res->gpio_init = 0;
	
    return 0;
}

static int rk_sensor_io_init(void)
{
	static bool is_init = false;
	
	struct rkcamera_platform_data *new_camera;
debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    if(is_init) {		
		return 0;
	} else {
		is_init = true;
	}
    
    if (sensor_ioctl_cb.sensor_power_cb == NULL)
        sensor_ioctl_cb.sensor_power_cb = sensor_power_default_cb;
    if (sensor_ioctl_cb.sensor_reset_cb == NULL)
        sensor_ioctl_cb.sensor_reset_cb = sensor_reset_default_cb;
    if (sensor_ioctl_cb.sensor_powerdown_cb == NULL)
        sensor_ioctl_cb.sensor_powerdown_cb = sensor_powerdown_default_cb;
    if (sensor_ioctl_cb.sensor_flash_cb == NULL)
        sensor_ioctl_cb.sensor_flash_cb = sensor_flash_default_cb;
    if (sensor_ioctl_cb.sensor_af_cb == NULL)
        sensor_ioctl_cb.sensor_af_cb = sensor_afpower_default_cb;	

	/**********yzm*********/
	new_camera = new_camera_head;
	while(new_camera != NULL)
	{
		if (_rk_sensor_io_init_(&new_camera->io,new_camera->of_node)<0)
            _rk_sensor_io_deinit_(&new_camera->io);
		new_camera = new_camera->next_camera;
	}
	return 0;
}

static int rk_sensor_io_deinit(int sensor)
{
	struct rkcamera_platform_data *new_camera;

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	new_camera = new_camera_head;
	while(new_camera != NULL)
	{
		_rk_sensor_io_deinit_(&new_camera->io);
		new_camera = new_camera->next_camera;
	}

    return 0;
}
static int rk_sensor_ioctrl(struct device *dev,enum rk29camera_ioctrl_cmd cmd, int on)
{
    struct rk29camera_gpio_res *res = NULL;
    struct rkcamera_platform_data *new_cam_dev = NULL;
	struct rk29camera_platform_data* plat_data = &rk_camera_platform_data;
    int ret = RK29_CAM_IO_SUCCESS,i = 0;
    //struct soc_camera_link *dev_icl = NULL;//yzm
	struct soc_camera_desc *dev_icl = NULL;//yzm
	struct rkcamera_platform_data *new_camera;
debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

    if (res == NULL) {
		new_camera = new_camera_head;
		while(new_camera != NULL)
		{
            if (strcmp(new_camera->dev_name, dev_name(dev)) == 0) {
                res = (struct rk29camera_gpio_res *)&new_camera->io; 
                new_cam_dev = &new_camera[i];
                //dev_icl = &new_camera[i].dev.link_info;
				 dev_icl = &new_camera->dev.desc_info;//yzm
                break;
            }
            new_camera = new_camera->next_camera;;
        }		
    }
    
    if (res == NULL) {
        eprintk("%s is not regisiterd in rk29_camera_platform_data!!",dev_name(dev));
        ret = RK29_CAM_EIO_INVALID;
        goto rk_sensor_ioctrl_end;
    }
	
	switch (cmd)
 	{
 		case Cam_Power:
		{
			if (sensor_ioctl_cb.sensor_power_cb) {
                ret = sensor_ioctl_cb.sensor_power_cb(res, on);   
                ret = (ret != RK29_CAM_EIO_INVALID)?ret:0;     /* ddl@rock-chips.com: v0.1.1 */ 
			} else {
                eprintk("sensor_ioctl_cb.sensor_power_cb is NULL");
                WARN_ON(1);
			}

			printk("ret: %d\n",ret);
			break;
		}
		case Cam_Reset:
		{
			if (sensor_ioctl_cb.sensor_reset_cb) {
                ret = sensor_ioctl_cb.sensor_reset_cb(res, on);

                ret = (ret != RK29_CAM_EIO_INVALID)?ret:0;
			} else {
                eprintk( "sensor_ioctl_cb.sensor_reset_cb is NULL");
                WARN_ON(1);
			}
			break;
		}

		case Cam_PowerDown:
		{
			if (sensor_ioctl_cb.sensor_powerdown_cb) {
                ret = sensor_ioctl_cb.sensor_powerdown_cb(res, on);
			} else {
                eprintk( "sensor_ioctl_cb.sensor_powerdown_cb is NULL");
                WARN_ON(1);
			}
			break;
		}

		case Cam_Flash:
		{
			if (sensor_ioctl_cb.sensor_flash_cb) {
                ret = sensor_ioctl_cb.sensor_flash_cb(res, on);
			} else {
                eprintk( "sensor_ioctl_cb.sensor_flash_cb is NULL!");
                WARN_ON(1);
			}
			break;
		}
		
		case Cam_Af:
		{
			if (sensor_ioctl_cb.sensor_af_cb) {
                ret = sensor_ioctl_cb.sensor_af_cb(res, on);
			} else {
                eprintk( "sensor_ioctl_cb.sensor_af_cb is NULL!");
                WARN_ON(1);
			}
			break;
		}

        case Cam_Mclk:
        {
            if (plat_data->sensor_mclk && dev_icl) {
                //plat_data->sensor_mclk(dev_icl->bus_id,(on!=0)?1:0,on);
				plat_data->sensor_mclk(dev_icl->host_desc.bus_id,(on!=0)?1:0,on);//yzm
            } else { 
                eprintk( "%s(%d): sensor_mclk(%p) or dev_icl(%p) is NULL",
                    __FUNCTION__,__LINE__,plat_data->sensor_mclk,dev_icl);
            }
            break;
        }
        
		default:
		{
			eprintk("%s cmd(0x%x) is unknown!",__FUNCTION__, cmd);
			break;
		}
 	}
rk_sensor_ioctrl_end:
    return ret;
}

static int rk_sensor_pwrseq(struct device *dev,int powerup_sequence, int on, int mclk_rate)
{
    int ret =0;
    int i,powerup_type;

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    
    for (i=0; i<8; i++) {

        if (on == 1)
            powerup_type = SENSOR_PWRSEQ_GET(powerup_sequence,i);
        else
            powerup_type = SENSOR_PWRSEQ_GET(powerup_sequence,(7-i));
        
        switch (powerup_type)
        {
            case SENSOR_PWRSEQ_AVDD:
            case SENSOR_PWRSEQ_DOVDD:
            case SENSOR_PWRSEQ_DVDD:
            case SENSOR_PWRSEQ_PWR:
            {  
                ret = rk_sensor_ioctrl(dev,Cam_Power, on);
                if (ret<0) {
                    eprintk("SENSOR_PWRSEQ_PWR failed");
                } else { 
                    msleep(10);
                    dprintk("SensorPwrSeq-power: %d",on);
                }
                break;
            }

            case SENSOR_PWRSEQ_HWRST:
            {
                if(!on){
                    rk_sensor_ioctrl(dev,Cam_Reset, 1);
                }else{
                    ret = rk_sensor_ioctrl(dev,Cam_Reset, 1);
                    msleep(2);
                    ret |= rk_sensor_ioctrl(dev,Cam_Reset, 0); 
                }
                if (ret<0) {
                    eprintk("SENSOR_PWRSEQ_HWRST failed");
                } else {
                    dprintk("SensorPwrSeq-reset: %d",on);
                }
                break;
            }

            case SENSOR_PWRSEQ_PWRDN:
            {     
                ret = rk_sensor_ioctrl(dev,Cam_PowerDown, !on);
                if (ret<0) {
                    eprintk("SENSOR_PWRSEQ_PWRDN failed");
                } else {
                    dprintk("SensorPwrSeq-power down: %d",!on);
                }
                break;
            }

            case SENSOR_PWRSEQ_CLKIN:
            {
                ret = rk_sensor_ioctrl(dev,Cam_Mclk, (on?mclk_rate:on));
                if (ret<0) {
                    eprintk("SENSOR_PWRSEQ_CLKIN failed");
                } else {
                    dprintk("SensorPwrSeq-clock: %d",on);
                }
                break;
            }

            default:
                break;
        }
        
    } 

    return ret;
}

static int rk_sensor_power(struct device *dev, int on)   //icd->pdev
{
    int powerup_sequence,mclk_rate;
    
    struct rk29camera_platform_data* plat_data = &rk_camera_platform_data;
    struct rk29camera_gpio_res *dev_io = NULL;
    struct rkcamera_platform_data *new_camera=NULL, *new_device=NULL;
    bool real_pwroff = true;
    int ret = 0;

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

    new_camera = plat_data->register_dev_new;    //new_camera[]
    
	while (new_camera != NULL) {//yzm
    //while (strstr(new_camera->dev_name,"end")==NULL) {

        if (new_camera->io.gpio_powerdown != INVALID_GPIO) {		//true
            gpio_direction_output(new_camera->io.gpio_powerdown,
                ((new_camera->io.gpio_flag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));            
        }

		debug_printk( "new_camera->dev_name= %s \n", new_camera->dev_name);	//yzm
		debug_printk( "dev_name(dev)= %s \n", dev_name(dev));	 //yzm
		
        if (strcmp(new_camera->dev_name,dev_name(dev))) {		//当不是打开的sensor时为TRUE
			debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i\n", __FILE__, __LINE__);//yzm
            if (sensor_ioctl_cb.sensor_powerdown_cb && on)
            	{
            		debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i\n", __FILE__, __LINE__);//yzm
                	sensor_ioctl_cb.sensor_powerdown_cb(&new_camera->io,1);
            	}
        } else {
            new_device = new_camera;
            dev_io = &new_camera->io;
            debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i\n", __FILE__, __LINE__);//yzm
            if (!Sensor_Support_DirectResume(new_camera->pwdn_info))
                real_pwroff = true;			
            else
                real_pwroff = false;
        }
        //new_camera++;
        new_camera = new_camera->next_camera;//yzm
    }

    if (new_device != NULL) {
        powerup_sequence = new_device->powerup_sequence;
        if ((new_device->mclk_rate == 24) || (new_device->mclk_rate == 48))
            mclk_rate = new_device->mclk_rate*1000000;
        else 
            mclk_rate = 24000000;
    } else {
        powerup_sequence = sensor_PWRSEQ_DEFAULT;
        mclk_rate = 24000000;
    }
        
    if (on) {
		debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i\n", __FILE__, __LINE__);//yzm
        rk_sensor_pwrseq(dev, powerup_sequence, on,mclk_rate);  
    } else {
        if (real_pwroff) {
            if (rk_sensor_pwrseq(dev, powerup_sequence, on,mclk_rate)<0)    /* ddl@rock-chips.com: v0.1.5 */
                goto PowerDown;
            
            /*ddl@rock-chips.com: all power down switch to Hi-Z after power off*/  //高阻态
            new_camera = plat_data->register_dev_new;
			while (new_camera != NULL) {//yzm
            //while (strstr(new_camera->dev_name,"end")==NULL) {
                if (new_camera->io.gpio_powerdown != INVALID_GPIO) {
                    gpio_direction_input(new_camera->io.gpio_powerdown);            
                }
                new_camera->pwdn_info |= 0x01;
                //new_camera++;
                new_camera = new_camera->next_camera;//yzm
            }
        } else {  
PowerDown:
            rk_sensor_ioctrl(dev,Cam_PowerDown, !on);

            rk_sensor_ioctrl(dev,Cam_Mclk, 0);
        }

        mdelay(10);/* ddl@rock-chips.com: v0.1.3 */
    }
    return ret;
}
#if 0
static int rk_sensor_reset(struct device *dev)
{
#if 0
	rk_sensor_ioctrl(dev,Cam_Reset,1);
	msleep(2);
	rk_sensor_ioctrl(dev,Cam_Reset,0);
#else
    /*
    *ddl@rock-chips.com : the rest function invalidate, because this operate is put together in rk_sensor_power;
    */
#endif
	return 0;
}
#endif
static int rk_sensor_powerdown(struct device *dev, int on)
{

debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	return rk_sensor_ioctrl(dev,Cam_PowerDown,on);
}

int rk_sensor_register(void)
{
    int i;    
	struct rkcamera_platform_data *new_camera;	
	
    i = 0;
debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

	new_camera = new_camera_head;
	
	while (new_camera != NULL) {//yzm	
        if (new_camera->dev.i2c_cam_info.addr == INVALID_VALUE) {
            WARN(1, 
                KERN_ERR "%s(%d): new_camera[%d] i2c addr is invalidate!",
                __FUNCTION__,__LINE__,i);
            continue;
        }
        sprintf(new_camera->dev_name,"%s_%d",new_camera->dev.device_info.dev.init_name,i+3);
        new_camera->dev.device_info.dev.init_name =(const char*)&new_camera->dev_name[0];//转换成指针
        new_camera->io.dev_name =(const char*)&new_camera->dev_name[0];
        if (new_camera->orientation == INVALID_VALUE) {        //关于前后方向
            if (strstr(new_camera->dev_name,"back")) {		           
                new_camera->orientation = 90;
            } else {
                new_camera->orientation = 270;
            }
        }
        /* ddl@rock-chips.com: v0.1.3 */
        if ((new_camera->fov_h <= 0) || (new_camera->fov_h>360))
            new_camera->fov_h = 100;
        
        if ((new_camera->fov_v <= 0) || (new_camera->fov_v>360))
            new_camera->fov_v = 100;        

		new_camera->dev.desc_info.subdev_desc.power = rk_sensor_power;//yzm
		new_camera->dev.desc_info.subdev_desc.powerdown = rk_sensor_powerdown;//yzm
		new_camera->dev.desc_info.host_desc.board_info =&new_camera->dev.i2c_cam_info; //yzm

        new_camera->dev.device_info.id = i+6;//?? platform_device.id
		new_camera->dev.device_info.dev.platform_data = &new_camera->dev.desc_info;//yzm
		debug_printk("platform_data(desc_info) %p +++++++++++++\n",new_camera->dev.device_info.dev.platform_data);
		new_camera->dev.desc_info.subdev_desc.drv_priv = &rk_camera_platform_data;//yzm

        platform_device_register(&(new_camera->dev.device_info));
debug_printk( "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);  
debug_printk("new_camera = %p +++++++++++++\n",new_camera);
debug_printk("new_camera->next_camera = %p +++++++++++++\n",new_camera->next_camera);

        new_camera = new_camera->next_camera;
    }
	
		return 0;
}
