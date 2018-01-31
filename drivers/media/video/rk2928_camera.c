/* SPDX-License-Identifier: GPL-2.0 */

#include <mach/iomux.h>
#include <media/soc_camera.h>
#include <linux/android_pmem.h>
#include <mach/rk2928_camera.h>
#ifndef PMEM_CAM_SIZE
#include "../../../arch/arm/plat-rk/rk_camera.c"
#else
/*****************************************************************************************
 * camera  devices
 * author: ddl@rock-chips.com
 *****************************************************************************************/
#ifdef CONFIG_VIDEO_RK29 

static int rk_sensor_iomux(int pin)
{    
    iomux_set_gpio_mode(pin);
    return 0;
}
#define PMEM_CAM_BASE 0 //just for compile ,no meaning
#include "../../../arch/arm/plat-rk/rk_camera.c"


static u64 rockchip_device_camera_dmamask = 0xffffffffUL;
#if RK_SUPPORT_CIF0
static struct resource rk_camera_resource_host_0[] = {
	[0] = {
		.start = RK2928_CIF_PHYS,
		.end   = RK2928_CIF_PHYS + RK2928_CIF_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_CIF,
		.end   = IRQ_CIF,
		.flags = IORESOURCE_IRQ,
	}
};
#endif
#if RK_SUPPORT_CIF1
static struct resource rk_camera_resource_host_1[] = {
	[0] = {
		.start = RK2928_CIF_PHYS,
		.end   = RK2928_CIF_PHYS+ RK2928_CIF_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_CIF,
		.end   = IRQ_CIF,
		.flags = IORESOURCE_IRQ,
	}
};
#endif

/*platform_device : */
#if RK_SUPPORT_CIF0
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
#endif

#if RK_SUPPORT_CIF1
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
#endif

static void rk_init_camera_plateform_data(void)
{
    int i,dev_idx;
    
    dev_idx = 0;
    for (i=0; i<RK_CAM_NUM; i++) {
        rk_camera_platform_data.sensor_init_data[i] = &rk_init_data_sensor[i];
        if (rk_camera_platform_data.register_dev[i].device_info.name) {            
            rk_camera_platform_data.register_dev[i].link_info.board_info = 
                &rk_camera_platform_data.register_dev[i].i2c_cam_info;
            rk_camera_platform_data.register_dev[i].device_info.id = dev_idx;
            rk_camera_platform_data.register_dev[i].device_info.dev.platform_data = 
                &rk_camera_platform_data.register_dev[i].link_info;
            dev_idx++;
        }
    }
}

static void rk30_camera_request_reserve_mem(void)
{
    int i,max_resolution;
    int cam_ipp_mem=PMEM_CAMIPP_NECESSARY, cam_pmem=PMEM_CAM_NECESSARY;
    
    i =0;
    max_resolution = 0x00;
    while (strstr(new_camera[i].dev.device_info.dev.init_name,"end")==NULL) {
        if (new_camera[i].resolution > max_resolution)
            max_resolution = new_camera[i].resolution;
        i++;
    }

    if (max_resolution < PMEM_SENSOR_FULL_RESOLUTION_CIF_1)
        max_resolution = PMEM_SENSOR_FULL_RESOLUTION_CIF_1;
    if (max_resolution < PMEM_SENSOR_FULL_RESOLUTION_CIF_0)
        max_resolution = PMEM_SENSOR_FULL_RESOLUTION_CIF_0;

    switch (max_resolution)
    {
        case 0x800000:
        default:
        {
            cam_ipp_mem = 0x800000;
            cam_pmem = 0x1900000;
            break;
        }

        case 0x500000:
        {
            cam_ipp_mem = 0x800000;
            cam_pmem = 0x1400000;
            break;
        }

        case 0x300000:
        {
            cam_ipp_mem = 0x600000;
            cam_pmem = 0xf00000;
            break;
        }

        case 0x200000:
        {
            cam_ipp_mem = 0x600000;
            cam_pmem = 0xc00000;
            break;
        }

        case 0x100000:
        {
            cam_ipp_mem = 0x600000;
            cam_pmem = 0xa00000;
            break;
        }

        case 0x30000:
        {
            cam_ipp_mem = 0x600000;
            cam_pmem = 0x600000;
            break;
        }
    }

 
    rk_camera_platform_data.meminfo.vbase = rk_camera_platform_data.meminfo_cif1.vbase = NULL;
#if defined(CONFIG_VIDEO_RKCIF_WORK_SIMUL_OFF) || ((RK_SUPPORT_CIF0 && RK_SUPPORT_CIF1) == 0)
    rk_camera_platform_data.meminfo.name = "camera_ipp_mem";
    rk_camera_platform_data.meminfo.start = board_mem_reserve_add("camera_ipp_mem",cam_ipp_mem);
    rk_camera_platform_data.meminfo.size= cam_ipp_mem;

    memcpy(&rk_camera_platform_data.meminfo_cif1,&rk_camera_platform_data.meminfo,sizeof(struct rk29camera_mem_res));
#else
    rk_camera_platform_data.meminfo.name = "camera_ipp_mem_0";
    rk_camera_platform_data.meminfo.start = board_mem_reserve_add("camera_ipp_mem_0",PMEM_CAMIPP_NECESSARY_CIF_0);
    rk_camera_platform_data.meminfo.size= PMEM_CAMIPP_NECESSARY_CIF_0;

    rk_camera_platform_data.meminfo_cif1.name = "camera_ipp_mem_1";
    rk_camera_platform_data.meminfo_cif1.start =board_mem_reserve_add("camera_ipp_mem_1",PMEM_CAMIPP_NECESSARY_CIF_1);
    rk_camera_platform_data.meminfo_cif1.size= PMEM_CAMIPP_NECESSARY_CIF_1;
#endif

 #if PMEM_CAM_NECESSARY
        android_pmem_cam_pdata.start = board_mem_reserve_add((char*)(android_pmem_cam_pdata.name),cam_pmem);
        android_pmem_cam_pdata.size= cam_pmem;
 #endif

}
static int rk_register_camera_devices(void)
{
    int i;
    int host_registered_0,host_registered_1;
    struct rkcamera_platform_data *new_camera;
    
	rk_init_camera_plateform_data();

    host_registered_0 = 0;
    host_registered_1 = 0;
    for (i=0; i<RK_CAM_NUM; i++) {
        if (rk_camera_platform_data.register_dev[i].device_info.name) {
            if (rk_camera_platform_data.register_dev[i].link_info.bus_id == RK_CAM_PLATFORM_DEV_ID_0) {
            #if RK_SUPPORT_CIF0                
                host_registered_0 = 1;
            #else
                printk(KERN_ERR "%s(%d) : This chip isn't support CIF0, Please user check ...\n",__FUNCTION__,__LINE__);
            #endif
            } 

            if (rk_camera_platform_data.register_dev[i].link_info.bus_id == RK_CAM_PLATFORM_DEV_ID_1) {
            #if RK_SUPPORT_CIF1
                host_registered_1 = 1;
            #else
                printk(KERN_ERR "%s(%d) : This chip isn't support CIF1, Please user check ...\n",__FUNCTION__,__LINE__);
            #endif
            } 
        }
    }

    
    i=0;
    new_camera = rk_camera_platform_data.register_dev_new;
    if (new_camera != NULL) {
        while (strstr(new_camera->dev.device_info.dev.init_name,"end")==NULL) {
            if (new_camera->dev.link_info.bus_id == RK_CAM_PLATFORM_DEV_ID_1) {
                host_registered_1 = 1;
            } else if (new_camera->dev.link_info.bus_id == RK_CAM_PLATFORM_DEV_ID_0) {
                host_registered_0 = 1;
            }
            new_camera++;
        }
    }
    #if RK_SUPPORT_CIF0
    if (host_registered_0) {
        platform_device_register(&rk_device_camera_host_0);
    }
    #endif
    #if RK_SUPPORT_CIF1
    if (host_registered_1) {
        platform_device_register(&rk_device_camera_host_1);
    }  
    #endif

    for (i=0; i<RK_CAM_NUM; i++) {
        if (rk_camera_platform_data.register_dev[i].device_info.name) {
            platform_device_register(&rk_camera_platform_data.register_dev[i].device_info);
        }
    }

    if (rk_camera_platform_data.sensor_register)
       (rk_camera_platform_data.sensor_register)(); 
    
 #if PMEM_CAM_NECESSARY
    platform_device_register(&android_pmem_cam_device);
 #endif
    
	return 0;
}

module_init(rk_register_camera_devices);
#endif

#endif //#ifdef CONFIG_VIDEO_RK
