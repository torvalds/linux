#include "rk_ext_fshled_ctl.h"
#include "../camsys_gpio.h"
#include "flashlight.h"
#include "leds-rt8547.h"

typedef struct ext_fsh_info_s{
    struct      platform_device pdev;
    char*       dev_model;    
    struct      list_head         list;
}ext_fsh_info_t;

struct ext_fsh_dev_list_s{
    struct list_head         dev_list;
};

static struct ext_fsh_dev_list_s g_ext_fsh_devs;

int camsys_init_ext_fsh_module(void)
{
    camsys_trace(1,"init external flash module");
    INIT_LIST_HEAD(&g_ext_fsh_devs.dev_list);
    return 0;
}

int camsys_deinit_ext_fsh_module(void)
{
    ext_fsh_info_t* cur_fsh_info = NULL;
    camsys_trace(1,"deinit external flash module");
    if (!list_empty(&g_ext_fsh_devs.dev_list)) {
        list_for_each_entry(cur_fsh_info, &g_ext_fsh_devs.dev_list, list) {
            if (cur_fsh_info) {
            	platform_device_unregister(&cur_fsh_info->pdev);
    	        list_del_init(&cur_fsh_info->list);
    	        /* free after unregister device ?*/
    	        kfree(cur_fsh_info->pdev.dev.platform_data);
    	        kfree(cur_fsh_info);
    	        cur_fsh_info = NULL;
            }
        }
    }
    
    INIT_LIST_HEAD(&g_ext_fsh_devs.dev_list);
    return 0;
}

static void camsys_ext_fsh_release(struct device *dev)
{
}

void* camsys_register_ext_fsh_dev(camsys_flash_info_t *fsh_info)
{
    ext_fsh_info_t* new_dev = NULL;
    if(strcmp(fsh_info->fl_drv_name,"rt8547") == 0){
        struct rt8547_platform_data* new_rt_dev = NULL;
        new_dev = kzalloc(sizeof(ext_fsh_info_t),GFP_KERNEL);
        if(!new_dev){
            camsys_err("register new ext flash dev erro !");
            goto fail0;
        }
        
	new_rt_dev = kzalloc(sizeof(*new_rt_dev), GFP_KERNEL);
        if(!new_rt_dev){
            camsys_err("register new ext flash dev erro !");
            goto fail1;
        }

        new_dev->pdev.id = -1;
        new_dev->pdev.name = fsh_info->fl_drv_name;
	new_dev->pdev.dev.release = camsys_ext_fsh_release;
        new_dev->pdev.dev.platform_data = (void*)new_rt_dev;
        new_dev->dev_model = "rt-flash-led";

        new_rt_dev->flen_gpio = camsys_gpio_get(fsh_info->fl_en.name);
        new_rt_dev->flen_active = fsh_info->fl_en.active;
        camsys_trace(1,"flen name :%s,gpio %d,active %d \n",fsh_info->fl_en.name,new_rt_dev->flen_gpio,new_rt_dev->flen_active);
        new_rt_dev->flset_gpio = camsys_gpio_get(fsh_info->fl.name );
        new_rt_dev->flset_active = fsh_info->fl.active;
        camsys_trace(1,"flset name :%s, gpio %d, active %d \n",fsh_info->fl.name,new_rt_dev->flset_gpio,new_rt_dev->flset_active);
        new_rt_dev->ctl_gpio   = -1;
        new_rt_dev->def_lvp = RT8547_LVP_3V;
	new_rt_dev->def_tol = RT8547_TOL_100mA;

    //    new_rt_dev->def_lvp = RT8547_LVP_MAX;
	//    new_rt_dev->def_tol = RT8547_TOL_MAX;

    	if(platform_device_register(&new_dev->pdev) < 0){
    		camsys_err("register rtfled fail\n");
    		kfree(new_rt_dev);
    		goto fail1;
    	}

        list_add_tail(&new_dev->list, &g_ext_fsh_devs.dev_list);
        camsys_trace(1,"register new rt led dev success !");
    }
    
    return (void*)new_dev;
fail1:
    if(new_dev)
        kfree(new_dev);
fail0:
    return NULL;
}

int camsys_deregister_ext_fsh_dev(void* dev)
{
    ext_fsh_info_t* cur_fsh_info = NULL;
    if (!list_empty(&g_ext_fsh_devs.dev_list)) {
        list_for_each_entry(cur_fsh_info, &g_ext_fsh_devs.dev_list, list) {
            if (dev == cur_fsh_info) {
                camsys_trace(1,"unregister  ext flsh dev !");
            	platform_device_unregister(&cur_fsh_info->pdev);
    	        list_del_init(&cur_fsh_info->list);
    	        /* free after unregister device ?*/
    	        kfree(cur_fsh_info->pdev.dev.platform_data);
    	        kfree(cur_fsh_info);
		return 0;
            }
        }
    }
    return 0;
}

/*******************************
mode:
    0:  CAM_ENGINE_FLASH_OFF = 0x00,
    1:  CAM_ENGINE_FLASH_AUTO = 0x01,
    2:  CAM_ENGINE_FLASH_ON = 0x02,
    3:  CAM_ENGINE_FLASH_RED_EYE = 0x03,
    5:  CAM_ENGINE_FLASH_TORCH = 0x05
********************************/
int camsys_ext_fsh_ctrl(void* dev,int mode,unsigned int on)
{
    ext_fsh_info_t* cur_fsh_info = NULL;
    struct flashlight_device *fled_dev = NULL;
    if (!list_empty(&g_ext_fsh_devs.dev_list)) {
        list_for_each_entry(cur_fsh_info, &g_ext_fsh_devs.dev_list, list) {
            if (dev == cur_fsh_info) {
            break;
            }
        }
    }
    if(cur_fsh_info == NULL){
	camsys_err("this flash dev have not been registered !");
	return -1;
    }

    fled_dev = find_flashlight_by_name(cur_fsh_info->dev_model);
	if(fled_dev == NULL){
		camsys_err("--find_flashlight_by_name return NULL!--");
		return -1;
	}
    switch(mode){
        case 0: /* off */
           /* set flashlight mode to Off */
            flashlight_set_mode(fled_dev, FLASHLIGHT_MODE_OFF);
            break;
        case 2: /* flash on */
           /* set strobe timeout to 256ms */
           //flashlight_set_strobe_timeout(fled_dev, 256, 256);
           flashlight_set_strobe_timeout(fled_dev, 512, 512);
           /* set strobe brightness to to index 18 (1A), refer to the datasheet for the others */
           flashlight_set_strobe_brightness(fled_dev, 18);
           /* set flashlight mode to Strobe */
           flashlight_set_mode(fled_dev, FLASHLIGHT_MODE_FLASH);
           flashlight_strobe(fled_dev);
	break;
        case 5: /* torch */
            /* set the torch brightness index 2 (75mA), refer to the datasheet for index current value. */
            flashlight_set_torch_brightness(fled_dev, 2);
            /* set flashlight mode to Torch */
            flashlight_set_mode(fled_dev, FLASHLIGHT_MODE_TORCH);
            break;
        default:
    		camsys_err("not support this mode %d !",mode);
    }

    return 0;
}

