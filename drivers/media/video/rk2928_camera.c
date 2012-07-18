
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
    return 0;
}
#define PMEM_CAM_BASE 0 //just for compile ,no meaning
#include "../../../arch/arm/plat-rk/rk_camera.c"


static u64 rockchip_device_camera_dmamask = 0xffffffffUL;
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
#ifdef CONFIG_VIDEO_RK29_WORK_IPP
    #ifdef VIDEO_RKCIF_WORK_SIMUL_OFF
        rk_camera_platform_data.meminfo.name = "camera_ipp_mem";
        rk_camera_platform_data.meminfo.start = board_mem_reserve_add("camera_ipp_mem",PMEM_CAMIPP_NECESSARY);
        rk_camera_platform_data.meminfo.size= PMEM_CAMIPP_NECESSARY;

        memcpy(&rk_camera_platform_data.meminfo_cif1,&rk_camera_platform_data.meminfo,sizeof(struct rk29camera_mem_res));
    #else
        rk_camera_platform_data.meminfo.name = "camera_ipp_mem_0";
        rk_camera_platform_data.meminfo.start = board_mem_reserve_add("camera_ipp_mem_0",PMEM_CAMIPP_NECESSARY_CIF_0);
        rk_camera_platform_data.meminfo.size= PMEM_CAMIPP_NECESSARY_CIF_0;
        
        rk_camera_platform_data.meminfo_cif1.name = "camera_ipp_mem_1";
        rk_camera_platform_data.meminfo_cif1.start =board_mem_reserve_add("camera_ipp_mem_1",PMEM_CAMIPP_NECESSARY_CIF_1);
        rk_camera_platform_data.meminfo_cif1.size= PMEM_CAMIPP_NECESSARY_CIF_1;
    #endif
 #endif
 #if PMEM_CAM_NECESSARY
        android_pmem_cam_pdata.start = board_mem_reserve_add((char*)(android_pmem_cam_pdata.name),PMEM_CAM_NECESSARY);
        android_pmem_cam_pdata.size= PMEM_CAM_NECESSARY;
 #endif

}
static int rk_register_camera_devices(void)
{
    int i;
    int host_registered_0,host_registered_1;
    
	rk_init_camera_plateform_data();

    host_registered_0 = 0;
    host_registered_1 = 0;
    for (i=0; i<RK_CAM_NUM; i++) {
        if (rk_camera_platform_data.register_dev[i].device_info.name) {
            if (rk_camera_platform_data.register_dev[i].link_info.bus_id == RK_CAM_PLATFORM_DEV_ID_0) {
                if (!host_registered_0) {
                    platform_device_register(&rk_device_camera_host_0);
                    host_registered_0 = 1;
                }
            } else if (rk_camera_platform_data.register_dev[i].link_info.bus_id == RK_CAM_PLATFORM_DEV_ID_1) {
                if (!host_registered_1) {
                    platform_device_register(&rk_device_camera_host_1);
                    host_registered_1 = 1;
                }
            } 
        }
    }

    for (i=0; i<RK_CAM_NUM; i++) {
        if (rk_camera_platform_data.register_dev[i].device_info.name) {
            platform_device_register(&rk_camera_platform_data.register_dev[i].device_info);
        }
    }
 #if PMEM_CAM_NECESSARY
            platform_device_register(&android_pmem_cam_device);
 #endif
    
	return 0;
}

module_init(rk_register_camera_devices);
#endif

#endif //#ifdef CONFIG_VIDEO_RK
