#include "rk_camera.h"
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/delay.h>
//*****************yzm***************
static struct rkcamera_platform_data *new_camera_head;	
   
#define PMEM_CAM_SIZE PMEM_CAM_NECESSARY 
/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/
#define CONFIG_SENSOR_POWER_IOCTL_USR	   1 //define this refer to your board layout
#define CONFIG_SENSOR_RESET_IOCTL_USR	   0
#define CONFIG_SENSOR_POWERDOWN_IOCTL_USR	   0
#define CONFIG_SENSOR_FLASH_IOCTL_USR	   0
#define CONFIG_SENSOR_AF_IOCTL_USR	   0

#if CONFIG_SENSOR_POWER_IOCTL_USR

static int sensor_power_usr_cb (struct rk29camera_gpio_res *res,int on)
{
	//#error "CONFIG_SENSOR_POWER_IOCTL_USR is 1, sensor_power_usr_cb function must be writed!!";
	return 0;
}
#endif

#if CONFIG_SENSOR_RESET_IOCTL_USR
static int sensor_reset_usr_cb (struct rk29camera_gpio_res *res,int on)
{

printk(KERN_EMERG "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	#error "CONFIG_SENSOR_RESET_IOCTL_USR is 1, sensor_reset_usr_cb function must be writed!!";
}
#endif

#if CONFIG_SENSOR_POWERDOWN_IOCTL_USR
static int sensor_powerdown_usr_cb (struct rk29camera_gpio_res *res,int on)
{

printk(KERN_EMERG "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	#error "CONFIG_SENSOR_POWERDOWN_IOCTL_USR is 1, sensor_powerdown_usr_cb function must be writed!!";
}
#endif

#if CONFIG_SENSOR_FLASH_IOCTL_USR
static int sensor_flash_usr_cb (struct rk29camera_gpio_res *res,int on)
{

printk(KERN_EMERG "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	#error "CONFIG_SENSOR_FLASH_IOCTL_USR is 1, sensor_flash_usr_cb function must be writed!!";
}
#endif

#if CONFIG_SENSOR_AF_IOCTL_USR
static int sensor_af_usr_cb (struct rk29camera_gpio_res *res,int on)
{

printk(KERN_EMERG "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()/n", __FILE__, __LINE__,__FUNCTION__);

	#error "CONFIG_SENSOR_AF_IOCTL_USR is 1, sensor_af_usr_cb function must be writed!!";
}

#endif

static struct rk29camera_platform_ioctl_cb	sensor_ioctl_cb = {
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

	#if CONFIG_SENSOR_AF_IOCTL_USR
	.sensor_af_cb = sensor_af_usr_cb,	
	#else
	.sensor_af_cb = NULL,
	#endif
};

#include "../../../drivers/media/video/rk30_camera.c"


