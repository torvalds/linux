/*************************************************************
 * Amlogic 
 * vdac switch program
 *
 * Copyright (C) 2010 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:   jets.yan@amlogic
 *		   
 *		   
 **************************************************************/
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/vout/vdac_switch.h>
#include <linux/amlogic/vout/vout_notify.h>
#include "tvmode.h"
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/amlogic/aml_gpio_consumer.h>

#include <linux/amlogic/amlog.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend early_suspend;
static int early_suspend_flag = 0;
#endif

#define DEVICE_NAME     "vdacswitch"
#define char2lower(c) (((c>='A')&&(c<='Z')) ? ((c) + 'a' - 'A') : (c))

MODULE_AMLOG(0, 0xff, LOG_LEVEL_DESC, LOG_MASK_DESC);

#ifdef CONFIG_USE_OF
static struct aml_vdac_hw_switch *hw_switch = NULL;
#endif

static unsigned char switch_mode = VOUT_CVBS;
static struct aml_vdac_switch_platform_data *vdac_switch_platdata = NULL;
static inline int str2lower(char *dst, char *src, int length)
{
    int i = 0;

    if( (dst==NULL) || (src==NULL) )
        return -1;

    for( i=0; i<length; i++ )
    {
        if( (dst[i]=src[i]) == '\0')
            break;
    }

    return 0;
}

#ifdef  CONFIG_PM
static int  meson_vdac_switch_suspend(struct platform_device *pdev, pm_message_t state)
{	
#ifdef CONFIG_HAS_EARLYSUSPEND
    if (early_suspend_flag)
        return 0;
#endif

	return 0;
}

static int  meson_vdac_switch_resume(struct platform_device *pdev)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
    if (early_suspend_flag)
        return 0;
#endif

	return 0;
}
#endif 


#ifdef CONFIG_HAS_EARLYSUSPEND
static void meson_vdac_switch_early_suspend(struct early_suspend *h)
{
    if (early_suspend_flag)
        return;

	early_suspend_flag = 1;
}

static void meson_vdac_switch_late_resume(struct early_suspend *h)
{
    if (!early_suspend_flag)
        return;

	early_suspend_flag = 0;
}
#endif

static int vdac_switch_set_mode(unsigned char mode)
{
#ifdef CONFIG_USE_OF
    struct aml_vdac_hw_ctrl (*vdac)[3] = (struct aml_vdac_hw_ctrl (*)[3])hw_switch;
#endif
    printk("vdac_switch mode = %d\n", mode);
	if( (vdac_switch_platdata!=NULL)&&(vdac_switch_platdata->vdac_switch_change_type_func!=NULL) )
	{
		if( (mode>=VOUT_CVBS) && (mode<VOUT_MAX) )
		{
			vdac_switch_platdata->vdac_switch_change_type_func(mode);
		}
	}
#ifdef CONFIG_USE_OF
    amlogic_gpio_direction_output(vdac[mode]->pin1, vdac[mode]->val1, DEVICE_NAME);
    amlogic_gpio_direction_output(vdac[mode]->pin2, vdac[mode]->val2, DEVICE_NAME);
#endif
	return 0;
}

/*****************************************************************
**
**	vout driver interface  
**
******************************************************************/
#define VDACSWITCH_CLASS_NAME "vdac_hw_switch"

static ssize_t vdacswitch_mode_show(struct class *class, struct class_attribute *attr, char *buf)
{
    int pos = 0;
    const char *mode_str[] = {"cvbs","component","vga"};

    if( switch_mode < ARRAY_SIZE(mode_str) )
    {
        pos += snprintf(buf+pos, PAGE_SIZE, "%d:%s\n", switch_mode, mode_str[switch_mode]);
//        return sprintf(buf, "%d:%s\n", switch_mode, mode_str[switch_mode]);
    }

    return pos;
}

static ssize_t vdacswitch_mode_store(struct class *class, struct class_attribute *attr,
                                    const char *buf, size_t count)
{
    unsigned char mode = 0;
    char *endp;     //, mode_in_lower[16];

    mode = (unsigned char)simple_strtoul(buf, &endp, 0);
/*
    if( mode >= VOUT_MAX )
    {
        memset(mode_in_lower, 0x00, 16);
        str2lower(mode_in_lower, buf, 15);
        if( !strcmp(mode_in_lower,"cvbs") )
            mode = VOUT_CVBS;
        else if( !strcmp(mode_in_lower,"component") )
            mode = VOUT_COMPONENT;
        else if( !strcmp(mode_in_lower,"vga") )
            mode = VOUT_VGA;
    }
*/
    if( ((mode>=VOUT_CVBS)&&(mode<VOUT_MAX)) && mode != switch_mode )
    {
        switch_mode = mode;
        vdac_switch_set_mode(switch_mode);
    }

    return count;
}

static struct class_attribute vdacswitch_class_attrs[] =
{
    __ATTR(mode, S_IRUGO|S_IWUSR, vdacswitch_mode_show, vdacswitch_mode_store),
    __ATTR_NULL
};

static struct class vdacswitch_class = 
{
    .name = VDACSWITCH_CLASS_NAME,
    .class_attrs = vdacswitch_class_attrs,
};

#ifdef CONFIG_USE_OF
static void vdac_hw_type_match(struct device_node *np, const char *str, int idx, const char *vdac_string)
{
    int ret = 0;
    struct aml_vdac_hw_ctrl (*vdac_array)[3] = (struct aml_vdac_hw_ctrl (*)[3])hw_switch;
    ret = of_property_read_string_index(np, vdac_string, 0, &str);
    if(!ret) {
        vdac_array[idx]->pin1 = amlogic_gpio_name_map_num(str);
        amlogic_gpio_request(vdac_array[idx]->pin1, DEVICE_NAME);
        ret = of_property_read_string_index(np, vdac_string, 1, &str);
        if(!ret) {
            vdac_array[idx]->val1 = (strcmp(str, "H") == 0);
            ret = of_property_read_string_index(np, vdac_string, 2, &str);
            if(!ret) {
                vdac_array[idx]->pin2 = amlogic_gpio_name_map_num(str);
                amlogic_gpio_request(vdac_array[idx]->pin2, DEVICE_NAME);
                ret = of_property_read_string_index(np, vdac_string, 3, &str);
                if(!ret) {
                    vdac_array[idx]->val2 = (strcmp(str, "H") == 0);
                }
            }
        }
    }
}

static void get_dt_vdac_hw_data(struct device_node *np)
{
    int ret = 0;
    int idx = 0;
    const char *str = NULL;
    const char *vdac_hw_string[] = {"vout_cvbs", "vout_ypbr", "vout_vga", NULL};    //match with dts file
    while(vdac_hw_string[idx]) {
        ret = of_property_read_string_index(np, vdac_hw_string[idx], 0, &str);
        if(!ret) {
            vdac_hw_type_match(np, str, idx, vdac_hw_string[idx]);
        }
        idx ++;
    }
#if 1
    {
        struct aml_vdac_hw_ctrl (*vdac_array)[3] = (struct aml_vdac_hw_ctrl (*)[3])hw_switch;
        printk("vdac config test\n");
        printk("pin1 val1 pin2 val2\n");
        for(idx = 0; idx < 3; idx ++) {
            printk("%d %d %d %d\n", vdac_array[idx]->pin1, vdac_array[idx]->val1, vdac_array[idx]->pin2, vdac_array[idx]->val2);
        }
    }
#endif
}
#endif

static int meson_vdac_switch_probe(struct platform_device *pdev)
{
#ifdef CONFIG_USE_OF
	int ret = 0;
    int val = 0;
    struct device_node *init_data;
#endif
	amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"start init vdac switch module \r\n");
#ifdef CONFIG_HAS_EARLYSUSPEND
    early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
    early_suspend.suspend = meson_vdac_switch_early_suspend;
    early_suspend.resume = meson_vdac_switch_late_resume;
	register_early_suspend(&early_suspend);
#endif

#ifdef CONFIG_USE_OF
    hw_switch = kzalloc(sizeof(struct aml_vdac_hw_switch), GFP_KERNEL);
    if(!hw_switch) {
        printk("not alloc vdac switch mem\n");
    }
    if(pdev->dev.of_node) {
        ret = of_property_read_u32(pdev->dev.of_node,"hw-switch",&val);
        if(ret) {
            printk("not find vdac hw switch config\n");
        }
        else {
            init_data = of_find_node_by_phandle(val);
            if(!init_data) {
                printk("not find vdac switch detail\n");
            }
            else {
                get_dt_vdac_hw_data(init_data);
            }
        }
    }
#else
	vdac_switch_platdata = (struct aml_vdac_switch_platform_data*)pdev->dev.platform_data;
#endif

    vdac_switch_set_mode(switch_mode);

    ret = class_register(&vdacswitch_class);
    if(ret)
    {
        printk("vdac switch driver create class failed!\n");
    }

	return ret;
}
static int meson_vdac_switch_remove(struct platform_device *pdev)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&early_suspend);
#endif

	vdac_switch_platdata = NULL;

	return 0;
}

static struct platform_driver vdac_switch_driver = {
    .probe      = meson_vdac_switch_probe,
    .remove     = meson_vdac_switch_remove,
#ifdef  CONFIG_PM      
    .suspend  =meson_vdac_switch_suspend,
    .resume    =meson_vdac_switch_resume,
#endif    
    .driver     = {
        .name   = DEVICE_NAME,
    }
};

static int __init vdac_switch_init_module(void)
{
	int ret =0;
    
    printk("%s\n", __func__);
	if (platform_driver_register(&vdac_switch_driver)) 
	{
   		amlog_level(LOG_LEVEL_HIGH,"failed to register vdac switch driver\n");
    	ret= -ENODEV;
	}
	
	return ret;
}

static __exit void vdac_switch_exit_module(void)
{
	amlog_level(LOG_LEVEL_HIGH,"vdac_switch remove module.\n");

    platform_driver_unregister(&vdac_switch_driver);
}

arch_initcall(vdac_switch_init_module);
module_exit(vdac_switch_exit_module);

static int __init vdac_hw_switch_bootargs_setup(char *line)
{
    unsigned char mode[16];

    memset(mode, 0x00, 16);
    str2lower(mode, line, 15);

    if( !strcmp(mode, "cvbs") )
        switch_mode = VOUT_CVBS;
    else if( !strcmp(mode, "component") )
        switch_mode = VOUT_COMPONENT;
    else if( !strcmp(mode, "vga") )
        switch_mode = VOUT_VGA;
    else
        switch_mode = VOUT_CVBS;

    return 1;
}

__setup("vdachwswitch=", vdac_hw_switch_bootargs_setup);

MODULE_DESCRIPTION("vdac switch module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jets.yan <jets.yan@amlogic.com>");
