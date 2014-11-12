/*************************************************************
 * Amlogic 
 * vout  serve program
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
 * Author:   jianfeng_wang@amlogic
 *		   
 *		   
 **************************************************************/
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/of.h>

#include <linux/amlogic/vout/vinfo.h>
#include <mach/am_regs.h>
#include <asm/uaccess.h>
#include <linux/major.h>
#include "vout_serve.h"
#include "tvmode.h"
#include "vout_log.h"
#include <linux/amlogic/amlog.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend early_suspend;
static int early_suspend_flag = 0;
#endif
#ifdef CONFIG_SCREEN_ON_EARLY
static int early_resume_flag = 0;
#endif

MODULE_AMLOG(0, 0xff, LOG_LEVEL_DESC, LOG_MASK_DESC);
//class attribute

SET_VOUT_CLASS_ATTR(enable,func_default_null)
SET_VOUT_CLASS_ATTR(mode,set_vout_mode)
SET_VOUT_CLASS_ATTR(axis,set_vout_window)
SET_VOUT_CLASS_ATTR(wr_reg,write_reg)
SET_VOUT_CLASS_ATTR(rd_reg,read_reg)


static  vout_info_t	vout_info;
int power_level=0;

int get_power_level()
{
    return power_level;
}
EXPORT_SYMBOL(get_power_level);


/*****************************************************************
**
**	sysfs impletement part  
**
******************************************************************/
static  void   func_default_null(char  *str)
{
	return ;
}
static   int* parse_para(char *para,char   *para_num)
{
	 static unsigned   int  buffer[MAX_NUMBER_PARA] ; 
	 char  *endp ;
	 int *pt=NULL;
	 int len=0,count=0;

	if(!para) return NULL;
	memset(buffer,0,sizeof(int)*MAX_NUMBER_PARA);
	pt=&buffer[0];
	len=strlen(para);
	endp=(char*)buffer;
	do
	{
		//filter space out 
		while(para && ( isspace(*para) || !isalnum(*para)) && len)
		{
			para++;
			len --; 
		}
		if(len==0) break;
		*pt++=simple_strtoul(para,&endp,0);
		
		para=endp;
		len=strlen(para);
	}while(endp && ++count<*para_num&&count<MAX_NUMBER_PARA) ;
	*para_num=count;
	
	return  buffer;
}

static  void  read_reg(char *para)
{
	printk("please use new interface[ /sys/class/amlogic/debug]\n");
	return ;
}

static  void  write_reg(char *para)
{
	printk("please use new interface[ /sys/class/amlogic/debug]\n");
	return ;
}


	
#ifdef  CONFIG_PM
static int  meson_vout_suspend(struct platform_device *pdev, pm_message_t state);
static int  meson_vout_resume(struct platform_device *pdev);
#endif

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
vmode_t mode_by_user = VMODE_INIT_NULL;
#endif

static  void  set_vout_mode(char * name)
{
	vmode_t    mode;

	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"tvmode set to %s\r\n",name);
	mode=validate_vmode(name);
	if(VMODE_MAX==mode)
	{
		amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"no matched vout mode\n");
		return ; 
	}

	mode_by_user = mode;

	if(mode==get_current_vmode())
	{
		amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"don't set the same mode as current.\r\n");	
		return ;
	}

	set_current_vmode(mode);
	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"new mode %s set ok\r\n",name);
	vout_notifier_call_chain(VOUT_EVENT_MODE_CHANGE,&mode) ;
	printk("%s[%d]\n", __func__, __LINE__);
}

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
void update_vmode_status(char* name)
{
	snprintf(mode, 40, "%s\n", name);
}

EXPORT_SYMBOL(update_vmode_status);

void set_vout_mode_fr_auto(char* name)
{
	vmode_t    vmode;

	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"tvmode set to %s\n",name);

	vmode=validate_vmode(name);
	if(VMODE_MAX==vmode)
	{
		amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"no matched vout mode\n");
		return ; 
	}
	if(vmode==get_current_vmode())
	{
		amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"don't set the same mode as current.\n");
		return ;
	}
	
	update_vmode_status(name);

	set_current_vmode(vmode);
	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"new mode %s set ok\n",name);
	vout_notifier_call_chain(VOUT_EVENT_MODE_CHANGE,&vmode) ;
	printk("%s[%d]\n", __func__, __LINE__);
}
EXPORT_SYMBOL(set_vout_mode_fr_auto);

#endif

char* get_vout_mode_internal(void)
{
	return mode;
}
EXPORT_SYMBOL(get_vout_mode_internal);

//axis type : 0x12  0x100 0x120 0x130
static void  set_vout_window(char *para) 
{
#define   OSD_COUNT   2
	static  disp_rect_t  disp_rect[OSD_COUNT];
	char  count=OSD_COUNT*4;	
	int   *pt=&disp_rect[0].x;
	

	//parse window para .
	memcpy(pt,parse_para(para,&count),sizeof(disp_rect_t)*OSD_COUNT);
	
	if(count >=4 && count <8 )
	{
		disp_rect[1]=disp_rect[0] ;
	}
	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_LOW,"osd0=>x:%d ,y:%d,w:%d,h:%d\r\n osd1=> x:%d,y:%d,w:%d,h:%d \r\n", \
			*pt,*(pt+1),*(pt+2),*(pt+3),*(pt+4),*(pt+5),*(pt+6),*(pt+7));
	vout_notifier_call_chain(VOUT_EVENT_OSD_DISP_AXIS,&disp_rect[0]) ;
}

/*****************************************************************
**
**	sysfs  declare part 
**
******************************************************************/

static  struct  class_attribute   *vout_attr[]={
&class_vout_attr_enable,
&class_vout_attr_mode,	
&class_vout_attr_axis ,
&class_vout_attr_wr_reg,
&class_vout_attr_rd_reg,
};

static int  create_vout_attr(void)
{
	//create base class for display
	int  i;
    extern const vinfo_t *get_current_vinfo(void);
    vinfo_t * init_mode;

	vout_info.base_class=class_create(THIS_MODULE,VOUT_CLASS_NAME);
	if(IS_ERR(vout_info.base_class))
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create vout class fail\r\n");
		return  -1 ;
	}
	//create  class attr
	for(i=0;i<VOUT_ATTR_MAX;i++)
	{
		if ( class_create_file(vout_info.base_class,vout_attr[i]))
		{
			amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create disp attribute %s fail\r\n",vout_attr[i]->attr.name);
		}
	}

    // Init /sys/class/display/mode value
    init_mode = (vinfo_t *)get_current_vinfo();
    if(init_mode)
        strcpy(mode, init_mode->name);

	return   0;
}

#ifdef  CONFIG_PM
static int  meson_vout_suspend(struct platform_device *pdev, pm_message_t state)
{	
#ifdef CONFIG_HAS_EARLYSUSPEND
    if (early_suspend_flag)
        return 0;
#endif
	vout_suspend();
	return 0;
}

static int  meson_vout_resume(struct platform_device *pdev)
{
#ifdef CONFIG_SCREEN_ON_EARLY
    if (early_resume_flag) {
    	early_resume_flag = 0;
    	return 0;
    }
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
    if (early_suspend_flag)
        return 0;
#endif
	vout_resume();
	return 0;
}
#endif 

#ifdef CONFIG_SCREEN_ON_EARLY
void resume_vout_early(void)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
   early_suspend_flag = 0;
   early_resume_flag = 1;
   vout_resume();
#endif
    return;
}
EXPORT_SYMBOL(resume_vout_early);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void meson_vout_early_suspend(struct early_suspend *h)
{
    if (early_suspend_flag)
        return;
    //meson_vout_suspend((struct platform_device *)h->param, PMSG_SUSPEND);
    vout_suspend();
    early_suspend_flag = 1;
}

static void meson_vout_late_resume(struct early_suspend *h)
{
    if (!early_suspend_flag)
        return;
    early_suspend_flag = 0;
    //meson_vout_resume((struct platform_device *)h->param);
    vout_resume();
}
#endif

/*****************************************************************
**
**	vout driver interface  
**
******************************************************************/
static int 
 meson_vout_probe(struct platform_device *pdev)
{
	int ret =-1;
	
	vout_info.base_class=NULL;
	amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"start init vout module \r\n");
#ifdef CONFIG_HAS_EARLYSUSPEND
    early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
    early_suspend.suspend = meson_vout_early_suspend;
    early_suspend.resume = meson_vout_late_resume;
	register_early_suspend(&early_suspend);
#endif

	if(pdev->dev.of_node != NULL) {
	    ret = of_property_read_u32(pdev->dev.of_node,"power_level",&power_level);
	}

	ret =create_vout_attr();
	if(ret==0)
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create  vout attribute ok \r\n");
	}
	else
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create  vout attribute fail \r\n");
	}

	return ret;
}
static int
 meson_vout_remove(struct platform_device *pdev)
{
   	int i;
	if(vout_info.base_class==NULL) return -1;
#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&early_suspend);
#endif
	
	for(i=0;i<VOUT_ATTR_MAX;i++)
	{
		class_remove_file(vout_info.base_class,vout_attr[i]) ;
	}
		
	class_destroy(vout_info.base_class);
	
	return 0;
}


static const struct of_device_id meson_vout_dt_match[]={
	{	.compatible 	= "amlogic,mesonvout",
	},
	{},
};

static struct platform_driver
vout_driver = {
    .probe      = meson_vout_probe,
    .remove     = meson_vout_remove,
#ifdef  CONFIG_PM      
    .suspend  =meson_vout_suspend,
    .resume    =meson_vout_resume,
#endif    
    .driver     = {
        .name   = "mesonvout",
        .of_match_table=meson_vout_dt_match,
    }
};
static int __init vout_init_module(void)
{
	int ret =0;
    
    printk("%s\n", __func__);
	if (platform_driver_register(&vout_driver)) 
	{
       		amlog_level(LOG_LEVEL_HIGH,"failed to register osd driver\n");
        	ret= -ENODEV;
    	}
	
	return ret;

}
static __exit void vout_exit_module(void)
{
	
	amlog_level(LOG_LEVEL_HIGH,"osd_remove_module.\n");

    	platform_driver_unregister(&vout_driver);
}
module_init(vout_init_module);
module_exit(vout_exit_module);

MODULE_DESCRIPTION("vout serve  module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jianfeng_wang <jianfeng.wang@amlogic.com>");
