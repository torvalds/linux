/*******************************************************************
 *
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2010/4/1   19:46
 *
 *******************************************************************/
#include <linux/amlogic/ge2d/ge2d_main.h>
#include "ge2d_log.h"
#include "ge2d_dev.h"
#include <linux/amlogic/amlog.h>
#include <mach/mod_gate.h>

MODULE_AMLOG(AMLOG_DEFAULT_LEVEL, 0x00, LOG_LEVEL_DESC, LOG_MASK_DESC);

/***********************************************************************
*
* file op section.
*
************************************************************************/
static  bool   command_valid(unsigned int cmd)
{
    return (cmd <= GE2D_STRETCHBLIT_NOALPHA_NOBLOCK && cmd >= GE2D_ANTIFLICKER_ENABLE );
}
static int 
ge2d_open(struct inode *inode, struct file *file) 
{
	 ge2d_context_t *context;
	 //we create one ge2d workqueue for this file handler.
	 
         //if (ge2d_device.open_count == 0)
         //       switch_mod_gate_by_name("ge2d", 1);
	 if(NULL==(context=create_ge2d_work_queue()))
	 {
	 	amlog_level(LOG_LEVEL_HIGH,"can't create work queue \r\n");
		return -1;		
	 }
	 //amlog_level(LOG_LEVEL_LOW,"open one ge2d device\n");
	 file->private_data=context;
	 ge2d_device.open_count++;
	 return 0;
}
static long
ge2d_ioctl(struct file *filp,unsigned int cmd, unsigned long args)
{

	ge2d_context_t *context=(ge2d_context_t *)filp->private_data;
	void  __user* argp =(void __user*)args;
	config_para_t     ge2d_config;	
	ge2d_para_t  para ;
    config_para_ex_t  ge2d_config_ex;
	int  ret=0;    	

	if(!command_valid(cmd))  return -1;
	switch (cmd)
   	{
		case  GE2D_CONFIG:
		case  GE2D_SRCCOLORKEY:	
		ret=copy_from_user(&ge2d_config,argp,sizeof(config_para_t));
		break;
		case  GE2D_CONFIG_EX:
		ret=copy_from_user(&ge2d_config_ex,argp,sizeof(config_para_ex_t));
		break;
		case  GE2D_SET_COEF:
		case  GE2D_ANTIFLICKER_ENABLE:	
		break;
		default :
		ret=copy_from_user(&para,argp,sizeof(ge2d_para_t));	
		break;
		
   	}
	switch(cmd)
	{
		case GE2D_CONFIG:
		ret=ge2d_context_config(context,&ge2d_config) ;
	  	break;
		case GE2D_CONFIG_EX:
		ret=ge2d_context_config_ex(context,&ge2d_config_ex) ;
	  	break;
		case GE2D_SET_COEF:
		ge2d_wq_set_scale_coef(context,args&0xff,args>>16);
		break;
		case GE2D_ANTIFLICKER_ENABLE:
		ge2d_antiflicker_enable(context,args);	
		break;
        case GE2D_SRCCOLORKEY:
		ge2dgen_src_key(context , ge2d_config.src_key.key_enable,ge2d_config.src_key.key_color, ge2d_config.src_key.key_mask,ge2d_config.src_key.key_mode);  //RGBA MODE		
		break;
		case GE2D_FILLRECTANGLE:
		amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"fill rect...,x=%d,y=%d,w=%d,h=%d,color=0x%x\r\n",
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.color);

            	fillrect(context,
                     para.src1_rect.x, para.src1_rect.y,
                     para.src1_rect.w, para.src1_rect.h,
                     para.color) ;	
		break;
		case GE2D_FILLRECTANGLE_NOBLOCK:
		amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"fill rect...,x=%d,y=%d,w=%d,h=%d,color=0x%x,noblk\r\n",
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.color);

            	fillrect_noblk(context,
                     para.src1_rect.x, para.src1_rect.y,
                     para.src1_rect.w, para.src1_rect.h,
                     para.color) ;	
		break;
		case GE2D_STRETCHBLIT:
		//stretch blit
            	amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"stretchblt...,x=%d,y=%d,w=%d,h=%d,dst.w=%d,dst.h=%d\r\n",
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.dst_rect.w, para.dst_rect.h);

            	stretchblt(context ,
                       para.src1_rect.x, para.src1_rect.y, para.src1_rect.w, para.src1_rect.h,
                       para.dst_rect.x,  para.dst_rect.y,  para.dst_rect.w,  para.dst_rect.h);	
		break;
		case GE2D_STRETCHBLIT_NOBLOCK:
		//stretch blit
            	amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"stretchblt...,x=%d,y=%d,w=%d,h=%d,dst.w=%d,dst.h=%d,noblk\r\n",
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.dst_rect.w, para.dst_rect.h);

            	stretchblt_noblk(context ,
                       para.src1_rect.x, para.src1_rect.y, para.src1_rect.w, para.src1_rect.h,
                       para.dst_rect.x,  para.dst_rect.y,  para.dst_rect.w,  para.dst_rect.h);	
		break;
		case GE2D_BLIT:
		//bitblt
            	amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"blit...\r\n");

            	bitblt(context ,
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.dst_rect.x, para.dst_rect.y);
           	break;
		case GE2D_BLIT_NOBLOCK:
		//bitblt
            	amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"blit...,noblk\r\n");

            	bitblt_noblk(context ,
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.dst_rect.x, para.dst_rect.y);
           	break;	
		case GE2D_BLEND:
		amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"blend ...\r\n");
		blend(context,
            		para.src1_rect.x, para.src1_rect.y,
            		para.src1_rect.w, para.src1_rect.h,
           		para.src2_rect.x, para.src2_rect.y,
           		para.src2_rect.w, para.src2_rect.h,
           		para.dst_rect.x, para.dst_rect.y,
           		para.dst_rect.w, para.dst_rect.h,
           		para.op) ;	
		break;
		case GE2D_BLEND_NOBLOCK:
		amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"blend ...,noblk\r\n");
		blend_noblk(context,
            		para.src1_rect.x, para.src1_rect.y,
            		para.src1_rect.w, para.src1_rect.h,
           		para.src2_rect.x, para.src2_rect.y,
           		para.src2_rect.w, para.src2_rect.h,
           		para.dst_rect.x, para.dst_rect.y,
           		para.dst_rect.w, para.dst_rect.h,
           		para.op) ;	
		break;
		case GE2D_BLIT_NOALPHA:
		//bitblt_noalpha
            	amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"blit_noalpha...\r\n");
            	bitblt_noalpha(context ,
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.dst_rect.x, para.dst_rect.y);	
		break;
		case GE2D_BLIT_NOALPHA_NOBLOCK:
		//bitblt_noalpha
            	amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"blit_noalpha...,noblk\r\n");
            	bitblt_noalpha_noblk(context ,
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.dst_rect.x, para.dst_rect.y);	
		break;
		case GE2D_STRETCHBLIT_NOALPHA:
		//stretch blit
            	amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"stretchblt_noalpha...,x=%d,y=%d,w=%d,h=%d,dst.w=%d,dst.h=%d\r\n",
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.dst_rect.w, para.dst_rect.h);

            	stretchblt_noalpha(context ,
                       para.src1_rect.x, para.src1_rect.y, para.src1_rect.w, para.src1_rect.h,
                       para.dst_rect.x,  para.dst_rect.y,  para.dst_rect.w,  para.dst_rect.h);	
		break;
		case GE2D_STRETCHBLIT_NOALPHA_NOBLOCK:
		//stretch blit
            	amlog_mask_level(LOG_MASK_IOCTL,LOG_LEVEL_LOW,"stretchblt_noalpha...,x=%d,y=%d,w=%d,h=%d,dst.w=%d,dst.h=%d,noblk\r\n",
                   para.src1_rect.x, para.src1_rect.y,
                   para.src1_rect.w, para.src1_rect.h,
                   para.dst_rect.w, para.dst_rect.h);

            	stretchblt_noalpha_noblk(context ,
                       para.src1_rect.x, para.src1_rect.y, para.src1_rect.w, para.src1_rect.h,
                       para.dst_rect.x,  para.dst_rect.y,  para.dst_rect.w,  para.dst_rect.h);	
		break;
	}
 	return ret;
}
static int 
ge2d_release(struct inode *inode, struct file *file)
{
	ge2d_context_t *context=(ge2d_context_t *)file->private_data;
	
	if(context && (0==destroy_ge2d_work_queue(context)))
	{
		ge2d_device.open_count--;
		//if (ge2d_device.open_count == 0)
		//	switch_mod_gate_by_name("ge2d", 0);
		return 0;
	}
	amlog_level(LOG_LEVEL_LOW,"release one ge2d device\n");
	return -1;
}


/***********************************************************************
*
* module  section    (init&exit)
*
************************************************************************/
static int  
init_ge2d_device(void)
{
	int  ret=0;
	
	strcpy(ge2d_device.name,"ge2d");
	ret=register_chrdev(0,ge2d_device.name,&ge2d_fops);
	if(ret <=0) 
	{
		amlog_level(LOG_LEVEL_HIGH,"register ge2d device error\r\n");
		return  ret ;
	}
	ge2d_device.major=ret;
	ge2d_device.dbg_enable=0;
	amlog_level(LOG_LEVEL_LOW,"ge2d_dev major:%d\r\n",ret);
	ret = class_register(&ge2d_class);
	if(ret<0 )
	{
		amlog_level(LOG_LEVEL_HIGH,"error create ge2d class\r\n");
		return ret;
	}
	ge2d_device.cla=&ge2d_class ;
	ge2d_device.dev=device_create(ge2d_device.cla,NULL,MKDEV(ge2d_device.major,0),NULL,ge2d_device.name);
	if (IS_ERR(ge2d_device.dev)) {
		amlog_level(LOG_LEVEL_HIGH,"create ge2d device error\n");
		class_unregister(ge2d_device.cla);
		return -1 ;
	}
	return ge2d_setup();
	
}
static int remove_ge2d_device(void)
{
	if(ge2d_device.cla)
	{
		if(ge2d_device.dev)
		device_destroy(ge2d_device.cla, MKDEV(ge2d_device.major, 0));
	    	class_unregister(ge2d_device.cla);
	}
	
	unregister_chrdev(ge2d_device.major, ge2d_device.name);
	ge2d_deinit();
	return  0;
}

static int __init
ge2d_init_module(void)
{
   	amlog_level(LOG_LEVEL_HIGH,"ge2d_init\n");
    	return init_ge2d_device();
    	
}

static void __exit
ge2d_remove_module(void)
{
	remove_ge2d_device();
    	amlog_level(LOG_LEVEL_HIGH,"ge2d module removed.\n");
    
}

module_init(ge2d_init_module);
module_exit(ge2d_remove_module);

MODULE_DESCRIPTION("AMLOGIC  ge2d driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jianfeng <jianfeng.wang@amlogic.com>");


