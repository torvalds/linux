#include <media/soc_camera.h>
#include <media/camsys_head.h>
#include <linux/android_pmem.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include "../../../arch/arm/mach-rockchip/rk30_camera.h"/*yzm*/
#include "../../../arch/arm/mach-rockchip/rk_camera.h"/*yzm*/
//**********yzm***********//
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/module.h>

static int rk_register_camera_devices(void)
{
    int i;
    int host_registered_0,host_registered_1;
    struct rkcamera_platform_data *new_camera;    

	//printk(KERN_EMERG "/$$$$$$$$$$$$$$$$$$$$$$//n Here I am: %s:%i-------%s()\n", __FILE__, __LINE__,__FUNCTION__);

	rk_cif_sensor_init();

    host_registered_0 = 0;
    host_registered_1 = 0;
    
    i=0;
    new_camera = rk_camera_platform_data.register_dev_new;
	//new_camera = new_camera_head;

    if (new_camera != NULL) {
        while (new_camera != NULL) {
			if (new_camera->dev.desc_info.host_desc.bus_id == RK_CAM_PLATFORM_DEV_ID_1) {/*yzm*/
                host_registered_1 = 1;
			} else if (new_camera->dev.desc_info.host_desc.bus_id == RK_CAM_PLATFORM_DEV_ID_0) {/*yzm*/
                host_registered_0 = 1;
            }
			
            new_camera = new_camera->next_camera;
        }
    }

    #if RK_SUPPORT_CIF0
    if (host_registered_0) {
        platform_device_register(&rk_device_camera_host_0);//host_0 has sensor
    }   //host_device_register
    #endif
	
    #if RK_SUPPORT_CIF1
    if (host_registered_1) {
        platform_device_register(&rk_device_camera_host_1);//host_1 has sensor
    }  //host_device_register
    #endif


    if (rk_camera_platform_data.sensor_register)      
       (rk_camera_platform_data.sensor_register)();   //call rk_sensor_register()

	return 0;
}


module_init(rk_register_camera_devices);/*yzm*/
