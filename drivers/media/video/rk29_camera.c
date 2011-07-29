#include <mach/rk29_camera.h> 

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
#if !(PMEM_SENSOR_FULL_RESOLUTION_0)
#undef PMEM_SENSOR_FULL_RESOLUTION_0
#define PMEM_SENSOR_FULL_RESOLUTION_0  0x500000
#endif
#else
#define PMEM_SENSOR_FULL_RESOLUTION_0  0x00
#endif
 
#if (CONFIG_SENSOR_IIC_ADDR_1 != 0x00)
#define PMEM_SENSOR_FULL_RESOLUTION_1  CONS(CONFIG_SENSOR_1,_FULL_RESOLUTION)
#if !(PMEM_SENSOR_FULL_RESOLUTION_1)
#undef PMEM_SENSOR_FULL_RESOLUTION_1
#define PMEM_SENSOR_FULL_RESOLUTION_1  0x500000
#endif
#else
#define PMEM_SENSOR_FULL_RESOLUTION_1  0x00
#endif

#if (PMEM_SENSOR_FULL_RESOLUTION_0 > PMEM_SENSOR_FULL_RESOLUTION_1)
#define PMEM_CAM_FULL_RESOLUTION   PMEM_SENSOR_FULL_RESOLUTION_0
#else
#define PMEM_CAM_FULL_RESOLUTION   PMEM_SENSOR_FULL_RESOLUTION_1
#endif

#if (PMEM_CAM_FULL_RESOLUTION == 0x500000)
#define PMEM_CAM_NECESSARY   0x1200000       /* 1280*720*1.5*4(preview) + 7.5M(capture raw) + 4M(jpeg encode output) */
#elif (PMEM_CAM_FULL_RESOLUTION == 0x300000)
#define PMEM_CAM_NECESSARY   0xe00000        /* 1280*720*1.5*4(preview) + 4.5M(capture raw) + 3M(jpeg encode output) */
#elif (PMEM_CAM_FULL_RESOLUTION == 0x200000) /* 1280*720*1.5*4(preview) + 3M(capture raw) + 3M(jpeg encode output) */
#define PMEM_CAM_NECESSARY   0xc00000
#elif ((PMEM_CAM_FULL_RESOLUTION == 0x100000) || (PMEM_CAM_FULL_RESOLUTION == 0x130000))
#define PMEM_CAM_NECESSARY   0x800000        /* 800*600*1.5*4(preview) + 2M(capture raw) + 2M(jpeg encode output) */
#elif (PMEM_CAM_FULL_RESOLUTION == 0x30000)
#define PMEM_CAM_NECESSARY   0x400000        /* 640*480*1.5*4(preview) + 1M(capture raw) + 1M(jpeg encode output) */
#else
#define PMEM_CAM_NECESSARY   0x1200000
#endif
/*---------------- Camera Sensor Fixed Macro End  ------------------------*/
#else   //#ifdef CONFIG_VIDEO_RK29 
#define PMEM_CAM_NECESSARY   0x00000000
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
	    printk(KERN_WARNING"rk29_cam_io: " fmt , ## arg); } while (0)

#define dprintk(format, ...) ddprintk(1, format, ## __VA_ARGS__)    

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
    			dprintk("\n%s..%s..PowerPin=%d ..PinLevel = %x   \n",__FUNCTION__,res->dev_name, camera_power, ((camera_ioflag&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
    			msleep(10);
    		} else {
    			gpio_set_value(camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
    			dprintk("\n%s..%s..PowerPin=%d ..PinLevel = %x   \n",__FUNCTION__,res->dev_name, camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
    		}
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			printk("\n%s..%s..PowerPin=%d request failed!\n",__FUNCTION__,res->dev_name,camera_power);
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
	        	dprintk("\n%s..%s..ResetPin=%d ..PinLevel = %x \n",__FUNCTION__,res->dev_name,camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
			} else {
				gpio_set_value(camera_reset,(((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
        		dprintk("\n%s..%s..ResetPin= %d..PinLevel = %x   \n",__FUNCTION__,res->dev_name, camera_reset, (((~camera_ioflag)&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
	        }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			printk("\n%s..%s..ResetPin=%d request failed!\n",__FUNCTION__,res->dev_name,camera_reset);
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
	        	dprintk("\n%s..%s..PowerDownPin=%d ..PinLevel = %x \n",__FUNCTION__,res->dev_name,camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
			} else {
				gpio_set_value(camera_powerdown,(((~camera_ioflag)&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
        		dprintk("\n%s..%s..PowerDownPin= %d..PinLevel = %x   \n",__FUNCTION__,res->dev_name, camera_powerdown, (((~camera_ioflag)&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
	        }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			dprintk("\n%s..%s..PowerDownPin=%d request failed!\n",__FUNCTION__,res->dev_name,camera_powerdown);
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
	        	    dprintk("\n%s..%s..FlashPin=%d ..PinLevel = %x \n",__FUNCTION__,res->dev_name,camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    break;
                }

                case Flash_Torch:
                {
                    gpio_set_value(camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    dprintk("\n%s..%s..FlashPin=%d ..PinLevel = %x \n",__FUNCTION__,res->dev_name,camera_flash, ((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));
	        	    break;
                }

                default:
                {
                    printk("\n%s..%s..Flash command(%d) is invalidate \n",__FUNCTION__,res->dev_name,on);
                    break;
                }
            }
		} else {
			ret = RK29_CAM_EIO_REQUESTFAIL;
			printk("\n%s..%s..FlashPin=%d request failed!\n",__FUNCTION__,res->dev_name,camera_flash);
		}
    } else {
		ret = RK29_CAM_EIO_INVALID;
    }
    return ret;
}


static int rk29_sensor_io_init(void)
{
    int ret = 0, i;
    unsigned int camera_reset = INVALID_GPIO, camera_power = INVALID_GPIO;
	unsigned int camera_powerdown = INVALID_GPIO, camera_flash = INVALID_GPIO;
	unsigned int camera_ioflag;

    if (sensor_ioctl_cb.sensor_power_cb == NULL)
        sensor_ioctl_cb.sensor_power_cb = sensor_power_default_cb;
    if (sensor_ioctl_cb.sensor_reset_cb == NULL)
        sensor_ioctl_cb.sensor_reset_cb = sensor_reset_default_cb;
    if (sensor_ioctl_cb.sensor_powerdown_cb == NULL)
        sensor_ioctl_cb.sensor_powerdown_cb = sensor_powerdown_default_cb;
    if (sensor_ioctl_cb.sensor_flash_cb == NULL)
        sensor_ioctl_cb.sensor_flash_cb = sensor_flash_default_cb;
    
    for (i=0; i<2; i++) {
        camera_reset = rk29_camera_platform_data.gpio_res[i].gpio_reset;
        camera_power = rk29_camera_platform_data.gpio_res[i].gpio_power;
		camera_powerdown = rk29_camera_platform_data.gpio_res[i].gpio_powerdown;
        camera_flash = rk29_camera_platform_data.gpio_res[i].gpio_flash;
		camera_ioflag = rk29_camera_platform_data.gpio_res[i].gpio_flag;
		rk29_camera_platform_data.gpio_res[i].gpio_init = 0;

        if (camera_power != INVALID_GPIO) {
            ret = gpio_request(camera_power, "camera power");
            if (ret) {
                if (i == 0) {
				    goto sensor_io_int_loop_end;
                } else {
                    if (camera_power != rk29_camera_platform_data.gpio_res[0].gpio_power)
                        goto sensor_io_int_loop_end;
                }
            }

			rk29_camera_platform_data.gpio_res[i].gpio_init |= RK29_CAM_POWERACTIVE_MASK;
            gpio_set_value(camera_reset, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));
            gpio_direction_output(camera_power, (((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));

			printk("\n%s....power pin(%d) init success(0x%x)  \n",__FUNCTION__,camera_power,(((~camera_ioflag)&RK29_CAM_POWERACTIVE_MASK)>>RK29_CAM_POWERACTIVE_BITPOS));

        }

        if (camera_reset != INVALID_GPIO) {
            ret = gpio_request(camera_reset, "camera reset");
            if (ret)
				goto sensor_io_int_loop_end;
			rk29_camera_platform_data.gpio_res[i].gpio_init |= RK29_CAM_RESETACTIVE_MASK;
            gpio_set_value(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));
            gpio_direction_output(camera_reset, ((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));

			dprintk("\n%s....reset pin(%d) init success(0x%x)\n",__FUNCTION__,camera_reset,((camera_ioflag&RK29_CAM_RESETACTIVE_MASK)>>RK29_CAM_RESETACTIVE_BITPOS));

        }

		if (camera_powerdown != INVALID_GPIO) {
            ret = gpio_request(camera_powerdown, "camera powerdown");
            if (ret)
				goto sensor_io_int_loop_end;
			rk29_camera_platform_data.gpio_res[i].gpio_init |= RK29_CAM_POWERDNACTIVE_MASK;
            gpio_set_value(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));
            gpio_direction_output(camera_powerdown, ((camera_ioflag&RK29_CAM_POWERDNACTIVE_MASK)>>RK29_CAM_POWERDNACTIVE_BITPOS));

			printk("\n%s....powerdown pin(%d) init success(0x%x) \n",__FUNCTION__,camera_powerdown,((camera_ioflag&RK29_CAM_POWERDNACTIVE_BITPOS)>>RK29_CAM_POWERDNACTIVE_BITPOS));

        }

		if (camera_flash != INVALID_GPIO) {
            ret = gpio_request(camera_flash, "camera flash");
            if (ret)
				goto sensor_io_int_loop_end;
			rk29_camera_platform_data.gpio_res[i].gpio_init |= RK29_CAM_FLASHACTIVE_MASK;
            gpio_set_value(camera_flash, ((~camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));    /* falsh off */
            gpio_direction_output(camera_flash, ((~camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));

			dprintk("\n%s....flash pin(%d) init success(0x%x) \n",__FUNCTION__,camera_flash,((camera_ioflag&RK29_CAM_FLASHACTIVE_MASK)>>RK29_CAM_FLASHACTIVE_BITPOS));

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
    struct rk29camera_gpio_res *res = NULL;    
	int ret = RK29_CAM_IO_SUCCESS;

    if(rk29_camera_platform_data.gpio_res[0].dev_name &&  (strcmp(rk29_camera_platform_data.gpio_res[0].dev_name, dev_name(dev)) == 0)) {
		res = (struct rk29camera_gpio_res *)&rk29_camera_platform_data.gpio_res[0];
    } else if (rk29_camera_platform_data.gpio_res[1].dev_name && (strcmp(rk29_camera_platform_data.gpio_res[1].dev_name, dev_name(dev)) == 0)) {
    	res = (struct rk29camera_gpio_res *)&rk29_camera_platform_data.gpio_res[1];
    } else {
        printk(KERN_ERR "%s is not regisiterd in rk29_camera_platform_data!!\n",dev_name(dev));
        ret = RK29_CAM_EIO_INVALID;
        goto rk29_sensor_ioctrl_end;
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
rk29_sensor_ioctrl_end:
    return ret;
}
static int rk29_sensor_power(struct device *dev, int on)
{
	rk29_sensor_ioctrl(dev,Cam_Power,on);
    return 0;
}
#if 0
static int rk29_sensor_reset(struct device *dev)
{
	rk29_sensor_ioctrl(dev,Cam_Reset,1);
	msleep(2);
	rk29_sensor_ioctrl(dev,Cam_Reset,0);
	return 0;
}
#endif
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

#endif

#endif //#ifdef CONFIG_VIDEO_RK29