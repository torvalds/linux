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
#include <linux/amlogic/vout/vinfo.h>
#include <mach/am_regs.h>
#include <asm/uaccess.h>
#include <linux/amlogic/major.h>
#include "vout_serve.h"
#include "tvmode.h"
#include "vout_log.h"
#include <linux/amlogic/amlog.h>
#include <plat/io.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
//static struct early_suspend early_suspend;
//static int early_suspend_flag = 0;
#endif

MODULE_AMLOG(0, 0xff, LOG_LEVEL_DESC, LOG_MASK_DESC);
//class attribute

SET_VOUT2_CLASS_ATTR(enable,func_default_null)
SET_VOUT2_CLASS_ATTR(mode,set_vout_mode)
SET_VOUT2_CLASS_ATTR(axis,set_vout_window)


static  vout_info_t	vout_info;
static int s_venc_mux = 0;

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


#if 0	
#ifdef  CONFIG_PM
static int  meson_vout_suspend(struct platform_device *pdev, pm_message_t state);
static int  meson_vout_resume(struct platform_device *pdev);
#endif
#endif
static  void  set_vout_mode(char * name)
{
	vmode_t    mode;

	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_LOW,"tvmode2 set to %s\r\n",name);
	mode=validate_vmode2(name);
	if(VMODE_MAX==mode)
	{
		amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"no matched vout2 mode\n");
		return ; 
	}
	if(mode==get_current_vmode2())
	{
		amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_HIGH,"don't set the same mode as current.\r\n");	
		return ;
	}
	set_current_vmode2(mode);
	amlog_mask_level(LOG_MASK_PARA,LOG_LEVEL_LOW,"new mode2 %s set ok\r\n",name);
	vout2_notifier_call_chain(VOUT_EVENT_MODE_CHANGE,&mode) ;
}

void  set_vout2_mode_internal(char * name)
{
    set_vout_mode(name);
}    
EXPORT_SYMBOL(set_vout2_mode_internal);

char* get_vout2_mode_internal(void)
{
	return mode;
}
EXPORT_SYMBOL(get_vout2_mode_internal);

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
	vout2_notifier_call_chain(VOUT_EVENT_OSD_DISP_AXIS,&disp_rect[0]) ;
}

/*****************************************************************
**
**	sysfs  declare part 
**
******************************************************************/
static const char *venc_mux_help = {
	"venc_mux:\n"
	"    0. single display, viu1->panel, viu2->null\n"
	"    2. dual display, viu1->hdmi, viu2->panel\n"
	"    4. single display, viu1->null, viu2->hdmi\n"
	"    8. dual display, viu1->panel, viu2->hdmi\n"
};

static ssize_t venc_mux_show(struct class *class, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\ncurrent venc_mux: %d\r\n", venc_mux_help, s_venc_mux);
}

static ssize_t venc_mux_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int mux_type = 0;
	unsigned int mux = 0;

	mux = simple_strtoul(buf, NULL, 0);
	switch (mux) {
	case 0x0:
		mux_type = s_venc_mux;
		aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, mux_type, 0, 4);
		break;
	case 0x2:
		mux_type = mux |(s_venc_mux<<2);
		aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, mux_type, 0, 4);
		break;
	case 0x4:
		mux_type = (0x2<<2);
		aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, mux_type, 0, 4);
		break;
	case 0x8:
		mux_type = (0x2<<2) |s_venc_mux;
		aml_set_reg32_bits(P_VPU_VIU_VENC_MUX_CTRL, mux_type, 0, 4);
		break;
	default:
		printk("set venc_mux error\n");
		break;
	}

	printk("set venc_mux mux_type is %d\n", mux_type);
	return count;
}

static  struct  class_attribute   *vout_attr[]={
&class_vout2_attr_enable,
&class_vout2_attr_mode,	
&class_vout2_attr_axis ,
};
static CLASS_ATTR(venc_mux, S_IWUSR | S_IRUGO, venc_mux_show, venc_mux_store);

static int  create_vout_attr(void)
{
	//create base class for display
	int  i;

	vout_info.base_class=class_create(THIS_MODULE,VOUT_CLASS_NAME);
	if(IS_ERR(vout_info.base_class))
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create vout2 class fail\r\n");
		return  -1 ;
	}
	//create  class attr
	for(i=0;i<VOUT_ATTR_MAX;i++)
	{
		if ( class_create_file(vout_info.base_class,vout_attr[i]))
		{
			amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create disp2 attribute %s fail\r\n",vout_attr[i]->attr.name);
		}
	}
	if (class_create_file(vout_info.base_class, &class_attr_venc_mux))
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create disp2 attribute venc_mux fail\r\n");

	return   0;
}
#if 0
#ifdef  CONFIG_PM
static int  meson_vout_suspend(struct platform_device *pdev, pm_message_t state)
{	
#if 0 //def CONFIG_HAS_EARLYSUSPEND
    if (early_suspend_flag)
        return 0;
#endif
	vout2_suspend();
	return 0;
}

static int  meson_vout_resume(struct platform_device *pdev)
{
#if 0 //def CONFIG_HAS_EARLYSUSPEND
    if (early_suspend_flag)
        return 0;
#endif
	vout2_resume();
	return 0;
}
#endif 
#endif
#if 0 //def CONFIG_HAS_EARLYSUSPEND
static void meson_vout_early_suspend(struct early_suspend *h)
{
    if (early_suspend_flag)
        return 0;
    meson_vout_suspend((struct platform_device *)h->param, PMSG_SUSPEND);
    early_suspend_flag = 1;
}

static void meson_vout_late_resume(struct early_suspend *h)
{
    if (!early_suspend_flag)
        return 0;
    early_suspend_flag = 0;
    meson_vout_resume((struct platform_device *)h->param);
}
#endif

/*****************************************************************
**
**	vout driver interface  
**
******************************************************************/
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define VPP_OFIFO_SIZE_WID          13
#else
#define VPP_OFIFO_SIZE_WID          12
#endif // MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define VPP_OFIFO_SIZE_MASK         0xfff
#define VPP_OFIFO_SIZE_BIT          0

static int 
 meson_vout_probe(struct platform_device *pdev)
{
	int ret =-1;
	
	vout_info.base_class=NULL;
	amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"start init vout2 module \r\n");
#if 0 //def CONFIG_HAS_EARLYSUSPEND
    early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
    early_suspend.suspend = meson_vout_early_suspend;
    early_suspend.resume = meson_vout_late_resume;
    early_suspend.param = pdev;
	register_early_suspend(&early_suspend);
#endif

	ret =create_vout_attr();
	s_venc_mux = aml_read_reg32(P_VPU_VIU_VENC_MUX_CTRL) & 0x3;
	if(ret==0)
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create  vout2 attribute ok \r\n");
	}
	else
	{
		amlog_mask_level(LOG_MASK_INIT,LOG_LEVEL_HIGH,"create  vout2 attribute fail \r\n");
	}

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    aml_set_reg32_bits(P_VPP2_OFIFO_SIZE, 0x800,
                        VPP_OFIFO_SIZE_BIT, VPP_OFIFO_SIZE_WID);
#else
    aml_set_reg32_bits(P_VPP2_OFIFO_SIZE, 0x780,
                        VPP_OFIFO_SIZE_BIT, VPP_OFIFO_SIZE_WID);
#endif                        

	return ret;
}
static int
 meson_vout_remove(struct platform_device *pdev)
{
   	int i;
	if(vout_info.base_class==NULL) return -1;
#if 0 //def CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&early_suspend);
#endif
	
	for(i=0;i<VOUT_ATTR_MAX;i++)
	{
		class_remove_file(vout_info.base_class,vout_attr[i]) ;
	}
		
	class_destroy(vout_info.base_class);
	
	return 0;
}


static const struct of_device_id meson_vout2_dt_match[]={
	{	.compatible 	= "amlogic,mesonvout2",
	},
	{},
};

static struct platform_driver
vout2_driver = {
    .probe      = meson_vout_probe,
    .remove     = meson_vout_remove,
#if 0 //def  CONFIG_PM      
    .suspend  =meson_vout_suspend,
    .resume    =meson_vout_resume,
#endif    
    .driver     = {
        .name   = "mesonvout2",
        .of_match_table=meson_vout2_dt_match,
    }
};
static int __init vout2_init_module(void)
{
	int ret =0;
  printk("%s enter\n", __func__);
	if (platform_driver_register(&vout2_driver)) 
	{
    printk("%s fail\n", __func__);
       		amlog_level(LOG_LEVEL_HIGH,"failed to register vout2 driver\n");
        	ret= -ENODEV;
    	}
	
	return ret;

}
static __exit void vout2_exit_module(void)
{
	
	amlog_level(LOG_LEVEL_HIGH,"vout2_remove_module.\n");

    	platform_driver_unregister(&vout2_driver);
}
module_init(vout2_init_module);
module_exit(vout2_exit_module);

MODULE_DESCRIPTION("vout2 serve  module");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jianfeng_wang <jianfeng.wang@amlogic.com>");
