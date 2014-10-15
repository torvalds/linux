/*
 * VDIN driver
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *		   Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


/* Standard Linux Headers */
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>

/* Amlogic Headers */
#include <mach/am_regs.h>
#include <mach/mod_gate.h>
#include <mach/cpu.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#include <mach/vpu.h>
#endif
/* Local Headers */
#include "../tvin_format_table.h"
#include "vdin_drv.h"
#include "vdin_ctl.h"
#include "vdin_regs.h"

static void parse_param(char *buf_orig,char **parm)
{
	char *ps, *token;
	unsigned int n=0;
	ps = buf_orig;
        while(1) {
                token = strsep(&ps, " \n");
                if (token == NULL)
                        break;
                if (*token == '\0')
                        continue;
                parm[n++] = token;
        }
}

static ssize_t sig_det_show(struct device *dev,struct device_attribute *attr,char *buf)
{
	int callmaster_status = 0;
	return sprintf(buf,"%d\n",callmaster_status);
}

static ssize_t sig_det_store(struct device *dev,struct device_attribute *attr,const char *buf,size_t len)
{
	enum tvin_port_e port = TVIN_PORT_NULL;
	tvin_frontend_t *frontend = NULL;
	int callmaster_status = 0;
	struct vdin_dev_s *devp = dev_get_drvdata(dev);
	if(!buf)
		return len;
	port = simple_strtol(buf,NULL,10);
	frontend = tvin_get_frontend(port,0);
	if(frontend&&frontend->dec_ops&&frontend->dec_ops->callmaster_det){
		/*call the frontend det function*/
		callmaster_status = frontend->dec_ops->callmaster_det(port,frontend);
		//pr_info("%d\n",callmaster_status);
	}
	printk("[vdin.%d]:%s callmaster_status=%d,port=[%s]\n",devp->index,__func__,callmaster_status,tvin_port_str(port));
	return len;
}
static DEVICE_ATTR(sig_det, 0664, sig_det_show, sig_det_store);

static ssize_t vdin_attr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        //struct vdin_dev_s *devp = dev_get_drvdata(dev);
        ssize_t len = 0;
        len += sprintf(buf+len,"\n0 HDMI0\t1 HDMI1\t2 HDMI2\t3 Component0\t4 Component1"
                               "\n5 CVBS0\t6 CVBS1\t7 Vga0\t8 CVBS2\n");
        len += sprintf(buf+len,"echo tvstart/v4l2start port fmt_id/resolution >/sys/class/vdin/vdinx/attr.\n");
	len += sprintf(buf+len,"echo v4l2start bt656/viuin/isp h_actve v_active frame_rate cfmt dfmt scan_fmt >/sys/class/vdin/vdinx/attr.\n");
	len += sprintf(buf+len,"cfmt/dfmt:\t0: RGB44\t1 YUV422\t2 YUV444\t7 NV12\t8 NV21\n");
	len += sprintf(buf+len,"scan_fmt:\t1: PROGRESSIVE\t2 INTERLACE\n");
        return len;
}
static void vdin_dump_mem(char *path, vdin_dev_t *devp)
{
        struct file *filp = NULL;
        loff_t pos = 0;
        void * buf = NULL;
        int i = 0;
        unsigned int canvas_real_size = devp->canvas_h * devp->canvas_w;
        mm_segment_t old_fs = get_fs();
        set_fs(KERNEL_DS);
        filp = filp_open(path,O_RDWR|O_CREAT,0666);

        if(IS_ERR(filp)){
                printk(KERN_ERR"create %s error.\n",path);
                return;
        }

        for(i=0; i < devp->canvas_max_num; i++){
                pos = canvas_real_size * i;
                buf = phys_to_virt(devp->mem_start + devp->canvas_max_size*i);
                vfs_write(filp,buf,canvas_real_size,&pos);
                pr_info("write buffer %2d of %2u  to %s.\n",i,devp->canvas_max_num,path);
        }
        vfs_fsync(filp,0);
        filp_close(filp,NULL);
        set_fs(old_fs);
}

static void dump_other_mem(char *path,unsigned int start,unsigned int offset)
{
	struct file *filp = NULL;
        loff_t pos = 0;
        void * buf = NULL;
        mm_segment_t old_fs = get_fs();
        set_fs(KERNEL_DS);
        filp = filp_open(path,O_RDWR|O_CREAT,0666);

        if(IS_ERR(filp)){
                printk(KERN_ERR"create %s error.\n",path);
                return;
       	}
        buf = phys_to_virt(start);
        vfs_write(filp,buf,offset,&pos);
        pr_info("write from 0x%x to 0x%x to %s.\n",start,start+offset,path);
        vfs_fsync(filp,0);
        filp_close(filp,NULL);
        set_fs(old_fs);
}
static void vdin_dump_state(vdin_dev_t *devp)
{
	struct vframe_s *vf = &devp->curr_wr_vfe->vf;
        struct tvin_parm_s *curparm = &devp->parm;
	pr_info("h_active = %d, v_active = %d\n", devp->h_active, devp->v_active);
	pr_info("signal format	= %s(0x%x)\n"
			"trans_fmt	= %s(%d)\n"
			"color_format	= %s(%d)\n"
			"format_convert = %s(%d)\n"
			"aspect_ratio	= %s(%d)\n"
			"decimation_ratio/dvi	= %u / %u\n",
			tvin_sig_fmt_str(devp->parm.info.fmt), devp->parm.info.fmt,
			tvin_trans_fmt_str(devp->prop.trans_fmt), devp->prop.trans_fmt,
			tvin_color_fmt_str(devp->prop.color_format), devp->prop.color_format,
			vdin_fmt_convert_str(devp->format_convert), devp->format_convert,
			tvin_aspect_ratio_str(devp->prop.aspect_ratio), devp->prop.aspect_ratio,
			devp->prop.decimation_ratio, devp->prop.dvi_info);
	vdin_dump_vf_state(devp->vfp);
	if(vf){
		pr_info("current vframe(%u):\n buf(w%u,h%u),type (0x%x,%u), duration(%d), ratio_control (0x%x).\n",
                          vf->index,vf->width,vf->height,vf->type,vf->type,vf->duration,vf->ratio_control);
		pr_info(" trans fmt %u,left_start_x %u,right_start_x %u,width_x %u\n"
	      		  " left_start_y %u,right_start_y %u,height_y %u\n",
	      		  vf->trans_fmt,vf->left_eye.start_x,vf->right_eye.start_x,vf->left_eye.width,
	      		  vf->left_eye.start_y,vf->right_eye.start_y,vf->left_eye.height);
                pr_info("current parameters:\n frontend of vdin index: %d, 3d flag: 0x%x, reserved 0x%x,"
                        " devp->flags:0x%x, max buffer num %u.\n",curparm->index,  curparm->flag,
                        curparm->reserved, devp->flags,devp->canvas_max_num);
        }

	pr_info("Vdin driver version: %s\n",VDIN_VER);
}
static void vdin_dump_histgram(vdin_dev_t *devp)
{
	uint i;
	printk("%s:\n",__func__);
	for(i=0;i<64;i++){
		printk("0x%-8x\t",devp->parm.histgram[i]);
		if((i+1)%8==0)
			printk("\n");
	}
}
static void vdin_write_mem(vdin_dev_t *devp,char *path)
{
	unsigned int real_size=0, size=0;
        struct file *filp = NULL;
        loff_t pos = 0;
        mm_segment_t old_fs;

        old_fs = get_fs();
	set_fs(KERNEL_DS);
	printk("bin file path =%s\n",path);
	filp = filp_open(path,O_RDONLY,0);
        if(IS_ERR(filp)){
                printk(KERN_ERR"read %s error.\n",path);
                return;
        }
        if(!devp->curr_wr_vfe){
	        devp->curr_wr_vfe = provider_vf_get(devp->vfp);
        }
        real_size = (devp->curr_wr_vfe->vf.width*devp->curr_wr_vfe->vf.height<<1);
        devp->curr_wr_vfe->vf.type = VIDTYPE_VIU_SINGLE_PLANE|VIDTYPE_VIU_FIELD|VIDTYPE_VIU_422;
        size = vfs_read(filp,phys_to_virt(canvas_get_addr(devp->curr_wr_vfe->vf.canvas0Addr)),real_size,&pos);
        if(size < real_size){
	        pr_info("%s read %u < %u error.\n",__func__,size,real_size);
                return;
        }
        vfs_fsync(filp,0);
        filp_close(filp,NULL);
        set_fs(old_fs);
	provider_vf_put(devp->curr_wr_vfe, devp->vfp);
        devp->curr_wr_vfe = NULL;
	vf_notify_receiver(devp->name,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
}
/*
* 1.show the current frame rate
* echo fps >/sys/class/vdin/vdinx/attr
* 2.dump the data from vdin memory
* echo capture dir >/sys/class/vdin/vdinx/attr
* 3.start the vdin hardware
* echo tvstart/v4l2start port fmt_id/resolution(width height frame_rate) >dir
* 4.freeze the vdin buffer
* echo freeze/unfreeze >/sys/class/vdin/vdinx/attr
* 5.enable vdin0-nr path or vdin0-mem
* echo output2nr >/sys/class/vdin/vdin0/attr
* echo output2mem >/sys/class/vdin/vdin0/attr
* 6.modify for vdin fmt & color fmt convertion
* echo convertion w h cfmt >/sys/class/vdin/vdin0/attr
*/
extern int start_tvin_service(int no ,vdin_parm_t *para);
extern int stop_tvin_service(int no);
static ssize_t vdin_attr_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t len)
{
        unsigned int fps=0;
        char ret=0,*buf_orig,*parm[8] = {NULL};
        struct vdin_dev_s *devp;
        if(!buf)
		return len;
        buf_orig = kstrdup(buf, GFP_KERNEL);
        //printk(KERN_INFO "input cmd : %s",buf_orig);
        devp = dev_get_drvdata(dev);
        parse_param(buf_orig,(char **)&parm);

	    if(!strncmp(parm[0], "fps", 3)){
		if(devp->cycle)
			fps = (VDIN_CRYSTAL + (devp->cycle>>3))/devp->cycle;
                pr_info("%u f/s\n",fps);
        }
        else if(!strcmp(parm[0],"capture")){
		if(parm[3] != NULL){
			unsigned int start,offset;
			start = simple_strtol(parm[2],NULL,16);
			offset = simple_strtol(parm[3],NULL,16);
			dump_other_mem(parm[1],start,offset);
		}else if(parm[1] != NULL){
                        vdin_dump_mem(parm[1],devp);
                }
        }
        else if(!strcmp(parm[0],"tvstart")){
                unsigned int port, fmt;
                port = simple_strtol(parm[1],NULL,10);
                switch(port){
                        case 0://HDMI0
                                port = TVIN_PORT_HDMI0;
                                break;
                        case 1://HDMI1
                                port = TVIN_PORT_HDMI1;
                                break;
                        case 2://HDMI2
                                port = TVIN_PORT_HDMI2;
                                break;
                        case 3://Component0
                                port = TVIN_PORT_COMP0;
                                break;
                        case 4://Component1
                                port = TVIN_PORT_COMP1;
                                break;
                        case 5://CVBS0
                                port = TVIN_PORT_CVBS0;
                                break;
                        case 6://CVBS1
                                port = TVIN_PORT_CVBS1;
                                break;
                        case 7://Vga0
                                port = TVIN_PORT_VGA0;
                                break;
                        case 8://CVBS2
                                port = TVIN_PORT_CVBS2;
                                break;
                        default:
                                port = TVIN_PORT_CVBS0;
                                break;
                }
                fmt = simple_strtol(parm[2],NULL,16);

                devp->flags |= VDIN_FLAG_FS_OPENED;
	        /* request irq */
	        snprintf(devp->irq_name, sizeof(devp->irq_name),  "vdin%d-irq", devp->index);
		pr_info("vdin work in normal mode\n");
		ret = request_irq(devp->irq, vdin_isr, IRQF_SHARED, devp->irq_name, (void *)devp);

                /*disable irq untill vdin is configured completely*/
                disable_irq_nosync(devp->irq);
	        /* remove the hardware limit to vertical [0-max]*/
	        WRITE_VCBUS_REG(VPP_PREBLEND_VD1_V_START_END, 0x00000fff);
	        pr_info("open device %s ok\n", dev_name(devp->dev));
                vdin_open_fe(port,0,devp);
                devp->parm.port = port;
                devp->parm.info.fmt = fmt;
                devp->fmt_info_p  = (struct tvin_format_s*)tvin_get_fmt_info(fmt);
				devp->flags |= VDIN_FLAG_DEC_STARTED;
                vdin_start_dec(devp);
        }
        else if(!strcmp(parm[0],"tvstop")){
                vdin_stop_dec(devp);
                vdin_close_fe(devp);
                devp->flags &= (~VDIN_FLAG_FS_OPENED);
		devp->flags &= (~VDIN_FLAG_DEC_STARTED);
	        /* free irq */
	        free_irq(devp->irq,(void *)devp);
	        /* reset the hardware limit to vertical [0-1079]  */
	        WRITE_VCBUS_REG(VPP_PREBLEND_VD1_V_START_END, 0x00000437);
        }
        else if(!strcmp(parm[0],"v4l2stop")){
                stop_tvin_service(devp->index);
        }
        else if(!strcmp(parm[0],"v4l2start")){
                struct vdin_parm_s param;
                if(!parm[4]){
                        pr_err("usage: echo v4l2start port width height fps cfmt >/sys/class/vdin/vdinx/attr."
                                     "\n port mybe bt656 or viuin,fps the frame rate of input.\n");
                        return len;
                }
                memset(&param,0,sizeof(vdin_parm_t));
                /*parse the port*/
                if(!strcmp(parm[1],"bt656"))
                        param.port = TVIN_PORT_CAMERA;
                else if(!strcmp(parm[1],"viuin"))
                        param.port = TVIN_PORT_VIU;
		if(!strcmp(parm[1],"isp"))
                        param.port = TVIN_PORT_ISP;
                /*parse the resolution*/
                param.h_active = simple_strtol(parm[2],NULL,10);
                param.v_active = simple_strtol(parm[3],NULL,10);
                param.frame_rate = simple_strtol(parm[4],NULL,10);
		if(!parm[5])
			param.cfmt = TVIN_YUV422;
		else
		        param.cfmt = simple_strtol(parm[5],NULL,10);
		if(!parm[6])
			param.dfmt = TVIN_YUV422;
		else
		        param.dfmt = simple_strtol(parm[6],NULL,10);
		if(!parm[7])
			param.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
		else
		        param.scan_mode = simple_strtol(parm[7],NULL,10);
                param.fmt = TVIN_SIG_FMT_MAX;
                //param.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;
                /*start the vdin hardware*/
                start_tvin_service(devp->index, &param);
        }
        else if(!strcmp(parm[0],"disablesm"))
                del_timer_sync(&devp->timer);
        else if(!strcmp(parm[0],"freeze")){
                if (!(devp->flags & VDIN_FLAG_DEC_STARTED))
                        return len;
                if(devp->fmt_info_p->scan_mode == TVIN_SCAN_MODE_PROGRESSIVE)
                        vdin_vf_freeze(devp->vfp, 1);
                else
                        vdin_vf_freeze(devp->vfp, 2);

        }
        else if(!strcmp(parm[0],"unfreeze")){
                if (!(devp->flags & VDIN_FLAG_DEC_STARTED))
                        return len;
                vdin_vf_unfreeze(devp->vfp);
        }
        else if(!strcmp(parm[0],"convertion")){
                if(parm[1] && parm[2] && parm[3]){
                        devp->debug.scaler4w  = simple_strtoul(parm[1],NULL,10);
                        devp->debug.scaler4h  = simple_strtoul(parm[2],NULL,10);
			devp->debug.dest_cfmt = simple_strtoul(parm[3],NULL,10);
			devp->flags |= VDIN_FLAG_MANUAL_CONVERTION;
			pr_info("enable manual convertion w=%u h=%u dest_cfmt=%s.\n",
				devp->debug.scaler4w,devp->debug.scaler4h,tvin_color_fmt_str(devp->debug.dest_cfmt));
                } else {
                	devp->flags &= (~VDIN_FLAG_MANUAL_CONVERTION);
                        pr_info("disable manual convertion w=%u h=%u dest_cfmt=%s.\n",
				devp->debug.scaler4w,devp->debug.scaler4h,tvin_color_fmt_str(devp->debug.dest_cfmt));
                }
        }
        else if(!strcmp(parm[0],"state")){
		vdin_dump_state(devp);
	}
	else if(!strcmp(parm[0],"histgram")){
		vdin_dump_histgram(devp);
	}
        else if(!strcmp(parm[0],"force_recycle")) {
                devp->flags |= VDIN_FLAG_FORCE_RECYCLE;
        }else if(!strcmp(parm[0],"read_pic")){
        	vdin_write_mem(devp,parm[1]);
}


        kfree(buf_orig);
        return len;
}
static DEVICE_ATTR(attr, 0664, vdin_attr_show, vdin_attr_store);
#ifdef VF_LOG_EN
static ssize_t vdin_vf_log_show(struct device * dev,
		struct device_attribute *attr, char * buf)
{
	int len = 0;
	struct vdin_dev_s *devp = dev_get_drvdata(dev);
	struct vf_log_s *log = &devp->vfp->log;

	len += sprintf(buf + len, "%d of %d\n", log->log_cur, VF_LOG_LEN);
	return len;
}

static ssize_t vdin_vf_log_store(struct device * dev,
		struct device_attribute *attr, const char * buf, size_t count)
{
	struct vdin_dev_s *devp = dev_get_drvdata(dev);

	if(!strncmp(buf, "start", 5)){
		vf_log_init(devp->vfp);
	}
	else if (!strncmp(buf, "print", 5)) {
		vf_log_print(devp->vfp);
	}
	else
	{
		pr_info("unknow command: %s\n"
				"Usage:\n"
				"a. show log messsage:\n"
				"echo print > /sys/class/vdin/vdin0/vf_log\n"
				"b. restart log message:\n"
				"echo start > /sys/class/vdin/vdin0/vf_log\n"
				"c. show log records\n"
				"cat > /sys/class/vdin/vdin0/vf_log\n" , buf);
	}
	return count;
}

/*
   1. show log length.
   cat /sys/class/vdin/vdin0/vf_log
   cat /sys/class/vdin/vdin1/vf_log
   2. clear log buffer and start log.
   echo start > /sys/class/vdin/vdin0/vf_log
   echo start > /sys/class/vdin/vdin1/vf_log
   3. print log
   echo print > /sys/class/vdin/vdin0/vf_log
   echo print > /sys/class/vdin/vdin1/vf_log
 */
static DEVICE_ATTR(vf_log, 0664, vdin_vf_log_show, vdin_vf_log_store);
#endif //VF_LOG_EN

#if defined(VDIN_V2)
static ssize_t vdin_debug_for_isp_show(struct device * dev,
   struct device_attribute *attr, char * buf)
{
   int len = 0;

   return len;

}

static ssize_t vdin_debug_for_isp_store(struct device * dev,
   struct device_attribute *attr, const char * buf, size_t count)
{
        char *buf_orig,*parm[6] = {NULL};
        cam_parameter_t tmp_isp;
        struct vdin_dev_s *devp;

        if(!buf)
		return count;
        buf_orig = kstrdup(buf, GFP_KERNEL);
        devp = dev_get_drvdata(dev);
        parse_param(buf_orig,(char **)&parm);

	if(!strcmp(parm[0], "bypass_isp")){
        vdin_bypass_isp(devp->addr_offset);
		tmp_isp.cam_command = CMD_ISP_BYPASS;
		if(devp->frontend->dec_ops->ioctl)
			devp->frontend->dec_ops->ioctl(devp->frontend,(void *)&tmp_isp);
		pr_info("vdin bypass isp for raw data.\n");
        }
        return count;
}

static DEVICE_ATTR(debug_for_isp, 0664, vdin_debug_for_isp_show, vdin_debug_for_isp_store);

#endif

#ifdef ISR_LOG_EN
static ssize_t vdin_isr_log_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 len = 0;
	struct vdin_dev_s *vdevp;
	vdevp = dev_get_drvdata(dev);
                len += sprintf(buf + len, "%d of %d\n", vdevp->vfp->isr_log.log_cur, ISR_LOG_LEN);
	return len;
}
/*
*1. show isr log length.
*cat /sys/class/vdin/vdin0/vf_log
*2. clear isr log buffer and start log.
*echo start > /sys/class/vdin/vdinx/isr_log
*3. print isr log
*echo print > /sys/class/vdin/vdinx/isr_log
*/
static ssize_t vdin_isr_log_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct vdin_dev_s *vdevp;
	vdevp = dev_get_drvdata(dev);
        if(!strncmp(buf,"start",5))
	        isr_log_init(vdevp->vfp);
        else if(!strncmp(buf,"print",5))
                isr_log_print(vdevp->vfp);
	return count;
}

static DEVICE_ATTR(isr_log,  0664, vdin_isr_log_show, vdin_isr_log_store);
#endif

static ssize_t vdin_crop_show(struct device * dev,
struct device_attribute *attr, char * buf)
{
        int len = 0;
        struct vdin_dev_s *devp = dev_get_drvdata(dev);
        tvin_cutwin_t *crop = &devp->debug.cutwin;

        len += sprintf(buf+len,"hs_offset %u,he_offset %u,vs_offset %u,ve_offset %u.\n",
				crop->hs,crop->he,crop->vs,crop->ve);
	return len;
}

static ssize_t vdin_crop_store(struct device * dev,
struct device_attribute *attr, const char * buf, size_t count)
{
	char *parm[4]={NULL},*buf_orig;
	struct vdin_dev_s *devp = dev_get_drvdata(dev);
	tvin_cutwin_t *crop = &devp->debug.cutwin;
	if(!buf)
		return count;
	buf_orig = kstrdup(buf, GFP_KERNEL);
	parse_param(buf_orig,parm);

	crop->hs = simple_strtol(parm[0],NULL,10);
	crop->he = simple_strtol(parm[1],NULL,10);
	crop->vs = simple_strtol(parm[2],NULL,10);
	crop->ve = simple_strtol(parm[3],NULL,10);

	pr_info("hs_offset %u,he_offset %u,vs_offset %u,ve_offset %u.\n",
                                crop->hs,crop->he,crop->vs,crop->ve);

	return count;
}

static DEVICE_ATTR(crop, 0664, vdin_crop_show, vdin_crop_store);

#if defined(VDIN_V2)
static ssize_t vdin_cm2_show(struct device *dev,
             struct device_attribute *attr,
			                     char *buf)
{
    struct vdin_dev_s *devp;
    unsigned int addr_port = VDIN_CHROMA_ADDR_PORT;
    unsigned int data_port = VDIN_CHROMA_DATA_PORT;

    devp = dev_get_drvdata(dev);
    if (devp->addr_offset != 0) {
        addr_port = VDIN_CHROMA_ADDR_PORT + devp->addr_offset;
	    data_port = VDIN_CHROMA_DATA_PORT + devp->addr_offset;
    }
    pr_info("addr_port: [0x%x] data_port: : [0x%x]\n",addr_port, data_port);

	pr_info("Usage:");
	pr_info("	echo wm addr data0 data1 data2 data3 data4 > /sys/class/vdin/vdin0/cm2 \n");
	pr_info("	echo rm addr > /sys/class/vdin/vdin0/cm2 \n");
	pr_info("	echo wm addr data0 data1 data2 data3 data4 > /sys/class/vdin/vdin1/cm2 \n");
	pr_info("	echo rm addr > /sys/class/vdin/vdin1/cm2 \n");
	return 0;
}

static ssize_t vdin_cm2_store(struct device *dev,
              struct device_attribute *attr,
		   const char *buffer, size_t count)
{
        struct vdin_dev_s *devp;
	int n = 0;
	char *buf_orig, *ps, *token;
	char *parm[7];
	u32 addr;
	int data[5] = {0};
	unsigned int addr_port = VDIN_CHROMA_ADDR_PORT;
	unsigned int data_port = VDIN_CHROMA_DATA_PORT;

        devp = dev_get_drvdata(dev);
        if (devp->addr_offset != 0) {
                addr_port = VDIN_CHROMA_ADDR_PORT + devp->addr_offset;
	        data_port = VDIN_CHROMA_DATA_PORT + devp->addr_offset;
        }
	buf_orig = kstrdup(buffer, GFP_KERNEL);
	ps = buf_orig;
	while (1) {
		token = strsep(&ps, " \n");
		if (token == NULL)
			break;
		if (*token == '\0')
			continue;
		parm[n++] = token;
	}

	if ((parm[0][0] == 'w') && parm[0][1] == 'm' ) {
		if (n != 7) {
			pr_info("read: invalid parameter\n");
			pr_info("please: cat /sys/class/vdin/vdin0/cm2 \n");
			kfree(buf_orig);
			return count;
		}
		addr = simple_strtol(parm[1], NULL, 16);
		addr = addr - addr%8;
		data[0] = simple_strtol(parm[2], NULL, 16);
		data[1] = simple_strtol(parm[3], NULL, 16);
		data[2] = simple_strtol(parm[4], NULL, 16);
		data[3] = simple_strtol(parm[5], NULL, 16);
		data[4] = simple_strtol(parm[6], NULL, 16);

		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr);
		aml_write_reg32(VCBUS_REG_ADDR(data_port), data[0]);
		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr + 1);
		aml_write_reg32(VCBUS_REG_ADDR(data_port), data[1]);
		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr + 2);
		aml_write_reg32(VCBUS_REG_ADDR(data_port), data[2]);
		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr + 3);
		aml_write_reg32(VCBUS_REG_ADDR(data_port), data[3]);
		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr + 4);
		aml_write_reg32(VCBUS_REG_ADDR(data_port), data[4]);

		pr_info("wm: [0x%x] <-- 0x0 \n",addr);
	}
	else if ((parm[0][0] == 'r') && parm[0][1] == 'm' ) {
		if (n != 2) {
			pr_info("read: invalid parameter\n");
			pr_info("please: cat /sys/class/vdin/vdin0/cm2 \n");
			kfree(buf_orig);
			return count;
		}
		addr = simple_strtol(parm[1], NULL, 16);
		addr = addr - addr%8;
		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr);
		data[0] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[0] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[0] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr+1);
		data[1] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[1] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[1] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr+2);
		data[2] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[2] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[2] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr+3);
		data[3] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[3] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[3] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		aml_write_reg32(VCBUS_REG_ADDR(addr_port), addr+4);
		data[4] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[4] = aml_read_reg32(VCBUS_REG_ADDR(data_port));
		data[4] = aml_read_reg32(VCBUS_REG_ADDR(data_port));

		pr_info("rm:[0x%x]-->[0x%x][0x%x][0x%x][0x%x][0x%x] \n",addr, data[0],data[1],data[2],data[3],data[4]);
	}else if (!strcmp(parm[0],"enable")){
		WRITE_VCBUS_REG_BITS(VDIN_CM_BRI_CON_CTRL+devp->addr_offset,1,CM_TOP_EN_BIT,CM_TOP_EN_WID);
	}else if (!strcmp(parm[0],"disable")){
		WRITE_VCBUS_REG_BITS(VDIN_CM_BRI_CON_CTRL+devp->addr_offset,0,CM_TOP_EN_BIT,CM_TOP_EN_WID);
	} else {
		pr_info("invalid command\n");
		pr_info("please: cat /sys/class/vdin/vdin0/bit");
	}
	kfree(buf_orig);
	return count;

}

static DEVICE_ATTR(cm2, S_IWUSR | S_IRUGO, vdin_cm2_show, vdin_cm2_store);
#endif
int vdin_create_device_files(struct device *dev)
{
	int ret = 0;
	ret = device_create_file(dev,&dev_attr_sig_det);
	/* create sysfs attribute files */
        #ifdef VF_LOG_EN
        ret = device_create_file(dev,&dev_attr_vf_log);
        #endif
        #ifdef ISR_LOG_EN
        ret = device_create_file(dev,&dev_attr_isr_log);
        #endif
        ret = device_create_file(dev,&dev_attr_attr);
#if defined(VDIN_V2)
    ret = device_create_file(dev,&dev_attr_cm2);
	ret = device_create_file(dev,&dev_attr_debug_for_isp);
#endif
	ret = device_create_file(dev,&dev_attr_crop);
	return ret;
}
void vdin_remove_device_files(struct device *dev)
{
	#ifdef VF_LOG_EN
        device_remove_file(dev,&dev_attr_vf_log);
        #endif
        #ifdef ISR_LOG_EN
        device_remove_file(dev,&dev_attr_isr_log);
        #endif
        device_remove_file(dev,&dev_attr_attr);
#if defined(VDIN_V2)
	device_remove_file(dev,&dev_attr_cm2);
	device_remove_file(dev,&dev_attr_debug_for_isp);
#endif
    device_remove_file(dev,&dev_attr_crop);
	device_remove_file(dev,&dev_attr_sig_det);
}
static int memp = MEMP_DCDR_WITHOUT_3D;

static char * memp_str(int profile)
{
	switch (profile) {
	case MEMP_VDIN_WITHOUT_3D:
		return "vdin without 3d";
	case MEMP_VDIN_WITH_3D:
		return "vdin with 3d";
	case MEMP_DCDR_WITHOUT_3D:
		return "decoder without 3d";
	case MEMP_DCDR_WITH_3D:
		return "decoder with 3d";
	case MEMP_ATV_WITHOUT_3D:
		return "atv without 3d";
	case MEMP_ATV_WITH_3D:
		return "atv with 3d";
	default:
		return "unkown";
	}
}

/*
 * cat /sys/class/vdin/memp
 */
static ssize_t memp_show(struct class *class,
	struct class_attribute *attr, char *buf)
{
	int len = 0;
	len += sprintf(buf+len, "%d: %s\n", memp, memp_str(memp));
	return len;
}

/*
 * echo 0|1|2|3|4|5 > /sys/class/vdin/memp
 */
static void memp_set(int type)
{
	switch (type) {
	case MEMP_VDIN_WITHOUT_3D:
	case MEMP_VDIN_WITH_3D:

#if defined(VDIN_V1)
#if (MESON_CPU_TYPE != MESON_CPU_TYPE_MESON8B)
		aml_set_reg32_mask(P_MMC_QOS7_CTRL0, 1<<25);	// set audio to urgent
                aml_write_reg32(P_MMC_CHAN_CTRL0, 0xf);		// set ch1-7 arbiter weight to 0
		aml_clr_reg32_mask(P_MMC_CHAN_CTRL1, 0xf<<20);	// set ch8 arbiter weight to 0
                // echo 0 > /sys/module/di/parameters/pre_urgent     //           disable urgent for DI pre
                // echo 0 > /sys/module/di/parameters/input2pre      //           disable input2pre
                aml_set_reg32_mask(P_MMC_PARB_CTRL, 1<<16);          //           enable A9 urgent for better CPU performance
#endif
                aml_set_reg32_mask(P_VPU_VD1_MMC_CTRL, 1<<12);       //           arb0
                aml_set_reg32_mask(P_VPU_VD2_MMC_CTRL, 1<<12);       //           arb0
                aml_set_reg32_mask(P_VPU_DI_IF1_MMC_CTRL, 1<<12);    //           arb0
                aml_clr_reg32_mask(P_VPU_DI_MEM_MMC_CTRL, 1<<12);    //           arb1
                aml_clr_reg32_mask(P_VPU_DI_INP_MMC_CTRL, 1<<12);    //           arb1
                aml_set_reg32_mask(P_VPU_DI_MTNRD_MMC_CTRL, 1<<12);  //           arb0
                aml_clr_reg32_mask(P_VPU_DI_CHAN2_MMC_CTRL, 1<<12);  //           arb1
                aml_clr_reg32_mask(P_VPU_DI_MTNWR_MMC_CTRL, 1<<12);  //           arb1
                aml_clr_reg32_mask(P_VPU_DI_NRWR_MMC_CTRL, 1<<12);   //           arb1
                aml_clr_reg32_mask(P_VPU_DI_DIWR_MMC_CTRL, 1<<12);   //           arb1
#endif
		break;
	case MEMP_DCDR_WITHOUT_3D:
	case MEMP_DCDR_WITH_3D:
#if defined(VDIN_V1)
#if (MESON_CPU_TYPE != MESON_CPU_TYPE_MESON8B)
		aml_set_reg32_mask(P_MMC_QOS7_CTRL0, 1<<25);		// set audio to urgent
                aml_write_reg32(P_MMC_CHAN_CTRL0, 0xf);			// set ch1-7 arbiter weight to 0
		aml_clr_reg32_mask(P_MMC_CHAN_CTRL1, 0xf<<20);		// set ch8 arbiter weight to 0
                // echo 0 > /sys/module/di/parameters/pre_urgent     //           disable urgent for DI pre
                aml_clr_reg32_mask(P_MMC_PARB_CTRL, 1<<16);          //           enable A9 urgent for better CPU performance
#endif
                aml_set_reg32_mask(P_VPU_VD1_MMC_CTRL, 1<<12);       //           arb0
                aml_set_reg32_mask(P_VPU_VD2_MMC_CTRL, 1<<12);       //           arb0
                aml_set_reg32_mask(P_VPU_DI_IF1_MMC_CTRL, 1<<12);    //           arb0
                aml_clr_reg32_mask(P_VPU_DI_MEM_MMC_CTRL, 1<<12);    //           arb1
                aml_clr_reg32_mask(P_VPU_DI_INP_MMC_CTRL, 1<<12);    //           arb1
                aml_set_reg32_mask(P_VPU_DI_MTNRD_MMC_CTRL, 1<<12);  //           arb0
                aml_clr_reg32_mask(P_VPU_DI_CHAN2_MMC_CTRL, 1<<12);  //           arb1
                aml_clr_reg32_mask(P_VPU_DI_MTNWR_MMC_CTRL, 1<<12);  //           arb1
                aml_clr_reg32_mask(P_VPU_DI_NRWR_MMC_CTRL, 1<<12);   //           arb1
                aml_clr_reg32_mask(P_VPU_DI_DIWR_MMC_CTRL, 1<<12);   //           arb1
#endif
		memp = type;
		break;
	case MEMP_ATV_WITHOUT_3D:
	case MEMP_ATV_WITH_3D:
#if defined(VDIN_V1)
#if (MESON_CPU_TYPE != MESON_CPU_TYPE_MESON8B)
		aml_set_reg32_mask(P_MMC_QOS7_CTRL0, 1<<25);		// set audio to urgent
                aml_write_reg32(P_MMC_CHAN_CTRL0, 0xf);			// set ch1-7 arbiter weight to 0
		aml_clr_reg32_mask(P_MMC_CHAN_CTRL1, 0xf<<20);		// set ch8 arbiter weight to 0
                // echo 0 > /sys/module/di/parameters/pre_urgent     //           disable urgent for DI pre
                // echo 0 > /sys/module/di/parameters/input2pre      //           disable input2pre
                aml_clr_reg32_mask(P_MMC_PARB_CTRL, 1<<16);          //           enable A9 urgent for better CPU performance
#endif
                aml_set_reg32_mask(P_VPU_VD1_MMC_CTRL, 1<<12);       //           arb0
                aml_set_reg32_mask(P_VPU_VD2_MMC_CTRL, 1<<12);       //           arb0
                aml_set_reg32_mask(P_VPU_DI_IF1_MMC_CTRL, 1<<12);    //           arb0
                aml_clr_reg32_mask(P_VPU_DI_MEM_MMC_CTRL, 1<<12);    //           arb1
                aml_clr_reg32_mask(P_VPU_DI_INP_MMC_CTRL, 1<<12);    //           arb1
                aml_set_reg32_mask(P_VPU_DI_MTNRD_MMC_CTRL, 1<<12);  //           arb0
                aml_clr_reg32_mask(P_VPU_DI_CHAN2_MMC_CTRL, 1<<12);  //           arb1
                aml_clr_reg32_mask(P_VPU_DI_MTNWR_MMC_CTRL, 1<<12);  //           arb1
                aml_clr_reg32_mask(P_VPU_DI_NRWR_MMC_CTRL, 1<<12);   //           arb1
                aml_clr_reg32_mask(P_VPU_DI_DIWR_MMC_CTRL, 1<<12);   //           arb1

                aml_set_reg32_mask(P_VPU_TVD3D_MMC_CTRL, 1<<14);     //           arb2
                aml_set_reg32_mask(P_VPU_TVD3D_MMC_CTRL, 1<<15);     //           urgent
                aml_set_reg32_mask(P_VPU_TVDVBI_MMC_CTRL, 1<<14);    //           arb2
                aml_set_reg32_mask(P_VPU_TVD3D_MMC_CTRL, 1<<15);     //           urgent
#endif
		memp = type;
		break;
	default:
		/* @todo */
		break;
	}
}

static ssize_t memp_store(struct class *class,
	struct class_attribute *attr, const char *buf, size_t count)
{
	int type = simple_strtol(buf, NULL, 10);
	memp_set(type);
	return count;
}

static CLASS_ATTR(memp, 0644, memp_show, memp_store);

int vdin_create_class_files(struct class *vdin_clsp)
{
	int ret = 0;
	ret = class_create_file(vdin_clsp, &class_attr_memp);
	return ret;
}
void vdin_remove_class_files(struct class *vdin_clsp)
{
	class_remove_file(vdin_clsp, &class_attr_memp);
}

