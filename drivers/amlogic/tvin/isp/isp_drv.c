/*
 * ISP driver
 *
 * Author: Kele Bai <kele.bai@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Standard Linux Headers */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
/* Amlogic Headers */
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include <mach/am_regs.h>
#include <mach/vpu.h>
/* Local Headers */
#include "../tvin_global.h"
#include "../tvin_frontend.h"
#include "../tvin_format_table.h"

#include "isp_drv.h"
#include "isp_tool.h"
#include "isp_sm.h"
#include "isp_regs.h"

#define DEVICE_NAME "isp"
#define MODULE_NAME "isp"

static struct isp_dev_s *isp_devp[ISP_NUM];
static unsigned int addr_offset[ISP_NUM];
static dev_t isp_devno;
static struct class *isp_clsp;
static unsigned int isp_debug = 0;
static unsigned int ae_enable = 1;
static unsigned int ae_adjust_enable = 1;
static unsigned int awb_enable = 1;
static unsigned int af_enable = 1;
static unsigned int af_pr = 0;
static unsigned int ioctl_debug = 0;
static unsigned int isr_debug = 0;
static unsigned int ae_flag = 0;
static bool rgb_mode = false;

extern struct isp_ae_to_sensor_s ae_sens;

static void parse_param(char *buf_orig,char **parm)
{
	char *ps, *token;
	unsigned int n=0;
	ps = buf_orig;

        if(isp_debug)
		pr_info("%s parm:%s",__func__,buf_orig);

        while(1) {
                token = strsep(&ps, " \n");
                if (token == NULL)
                        break;
                if (*token == '\0')
                        continue;
                parm[n++] = token;
        }
}

static ssize_t debug_store(struct device *dev,struct device_attribute *attr, const char* buf, size_t len)
{
	isp_dev_t *devp;
	unsigned int addr,data;
	char *parm[5]={NULL},*buf_orig;

	if(!buf)
		return len;
	buf_orig = kstrdup(buf, GFP_KERNEL);
	devp = dev_get_drvdata(dev);
	parse_param(buf_orig,(char **)&parm);
	if(!strcmp(parm[0],"r")){
		addr = simple_strtol(parm[1],NULL,16);
		data = isp_rd(addr);
		pr_info("r:0x%x = 0x%x.\n",addr,data);
	}else if(!strcmp(parm[0],"w")){
		addr = simple_strtol(parm[1],NULL,16);
		data = simple_strtol(parm[2],NULL,16);
		isp_wr(addr,data);
		pr_info("w:0x%x = 0x%x.\n",addr,data);
	}else if(!strcmp(parm[0],"reset")){
		isp_hw_reset();
	}else if(!strcmp(parm[0],"flag")){
		data = simple_strtol(parm[1],NULL,16);
		devp->flag = data;
		af_sm_init(devp);
	}else if(!strcmp(parm[0],"lenc-mode")){
		devp->debug.comb4_mode = simple_strtol(parm[1],NULL,10);
		devp->flag |= ISP_FLAG_SET_COMB4;
	}else if(!strcmp(parm[0],"test_pattern")){
		unsigned int width,height;
		width = simple_strtol(parm[1],NULL,10);
		height = simple_strtol(parm[2],NULL,10);
		isp_test_pattern(width,height,width+26,height+16,0);
	} else if(!strcmp(parm[0],"rgb")){
        unsigned int b,r,g;
        b = RD_BITS(ISP_GAIN_GRBG23, GAIN_GRBG2_BIT, GAIN_GRBG2_WID);
        r = RD_BITS(ISP_GAIN_GRBG01, GAIN_GRBG1_BIT, GAIN_GRBG1_WID);
        g = RD_BITS(ISP_GAIN_GRBG01, GAIN_GRBG0_BIT, GAIN_GRBG0_WID);
        pr_info("%s: r:%d, g:%d b:%d.\n",__func__,r,g,b);
	}else if(!strcmp(parm[0],"read_ae")){
	    int i;
		struct isp_ae_stat_s *ae = &devp->isp_ae;
		for(i=0;i<16;i++)
		pr_info("ae->luma_win[%d]=%d.\n",i,ae->luma_win[i]);
		for(i=0;i<3;i++)
		pr_info("ae->bayer_over_info[%d]=%d.\n",i,ae->bayer_over_info[i]);
	}else if(!strcmp(parm[0],"read_awb")){
	    int i;
		struct isp_awb_stat_s *awb = &devp->isp_awb;
		for(i=0;i<3;i++)
		pr_info("awb->rgb.rgb_sum[%d]=%d.\n",i,awb->rgb.rgb_sum[i]);
		pr_info("awb->rgb.rgb_count=%d.\n",awb->rgb.rgb_count);
		for(i=0;i<4;i++)
		pr_info("awb->yuv_low[%d].sum=%d,count=%d\n",i,awb->yuv_low[i].sum,awb->yuv_low[i].count);
		for(i=0;i<4;i++)
		pr_info("awb->yuv_mid[%d].sum=%d,count=%d\n",i,awb->yuv_mid[i].sum,awb->yuv_mid[i].count);
		for(i=0;i<4;i++)
		pr_info("awb->yuv_high[%d].sum=%d,count=%d\n",i,awb->yuv_high[i].sum,awb->yuv_high[i].count);
		//echo wb_test 1 >/sys/class/isp/isp0/debug --disable 3a,disable gamma correction,lensd
	}else if(!strcmp(parm[0],"wb_test")){
		unsigned int flag = simple_strtol(parm[1],NULL,10);
		if(flag)
			devp->flag |= ISP_FLAG_TEST_WB;
		else
			devp->flag &= (~ISP_FLAG_TEST_WB);
		pr_info("%s wb test.\n",flag?"start":"stop");
	//echo reconfigure h w bayer_fmt >/sys/class/isp/isp0/debug
	}else if(!strcmp(parm[0],"reconfigure")){
		unsigned int width,height,bayer;
		if(parm[1] && parm[2] && parm[3]){
			width = simple_strtol(parm[1],NULL,10);//width
			height = simple_strtol(parm[2],NULL,10);//height
			bayer = simple_strtol(parm[3],NULL,10);//bayer fmt 0:BGGR 1:RGGB 2:GBRG 3:GRBG
			devp->flag |= ISP_FLAG_RECONFIG;
			isp_load_def_setting(width,height,bayer);
			pr_info("default setting:%ux%u bayer=%u.\n",width,height,bayer);
		}else{
			devp->flag &= (~ISP_FLAG_RECONFIG);
			pr_info("config according to configure file.\n");
		}
	}else if(!strcmp(parm[0],"bypass_all")){
		isp_bypass_all();
		pr_info("isp bypass all for raw data.\n");
	}
	return len;
}

static ssize_t debug_show(struct device *dev,struct device_attribute *attr, char* buf)
{
	size_t len = 0;
	isp_dev_t *devp;

	devp = dev_get_drvdata(dev);
	len += sprintf(buf+len,"flag=0x%x.\n",devp->flag);
	return len;
}
static DEVICE_ATTR(debug, 0664, debug_show, debug_store);

static ssize_t af_debug_store(struct device *dev,struct device_attribute *attr, const char* buf, size_t len)
{
	isp_dev_t *devp;
	int data[10]={0};
	char *parm[11]={NULL};
	char *buf_orig = kstrdup(buf, GFP_KERNEL);
	af_debug_t *af = NULL;
	if(IS_ERR_OR_NULL(buf)){
		pr_info("%s: cmd null error.\n",__func__);
		return len;
	}
	parse_param(buf_orig,(char **)&parm);
	devp = dev_get_drvdata(dev);
	if(!strcmp(parm[0],"jump")){
		data[0] = simple_strtol(parm[1],NULL,16);
		pr_info("%s to 0x%x.\n",parm[0],data[0]);
		devp->cam_param->cam_function.set_af_new_step(devp->cam_param->cam_function.priv_data,data[0]);
	//echo start control min max dir delay step >af_debug
	}else if(!strcmp(parm[0],"start")){
		af = kmalloc(sizeof(af_debug_t),GFP_KERNEL);
		if(IS_ERR_OR_NULL(af)){
			pr_info("%s kmalloc error.\n",__func__);
			return len;
		}
		memset(af,0,sizeof(af_debug_t));
		if(parm[1]&&parm[2]&&parm[3]&&parm[4]){
			//data[0] = simple_strtol(parm[1],NULL,16);//control
			data[1] = simple_strtol(parm[2],NULL,10);//min step
			data[2] = simple_strtol(parm[3],NULL,10);//max step
			data[3] = simple_strtol(parm[4],NULL,10);//dir
			data[4] = simple_strtol(parm[5],NULL,10);//delay
			data[5] = simple_strtol(parm[6],NULL,10);//pre step
			data[6] = simple_strtol(parm[7],NULL,10);//mid step
			data[7] = simple_strtol(parm[8],NULL,10);//post step
			data[8] = simple_strtol(parm[9],NULL,10);//pre threshold
			data[9] = simple_strtol(parm[10],NULL,10);//post threshold
		}
		//af->control = data[0];
		af->min_step = data[1];
		af->cur_step = af->min_step;
		af->max_step = data[2];
		af->dir = data[3]>0?true:false;
		af->delay = data[4];
		af->pre_step = data[5];
		af->mid_step = data[6];
		af->post_step = data[7];
		af->pre_threshold = data[8];
		af->post_threshold = data[9];

		af->state = 0;
		af->step = af->pre_step;
		if(devp->af_dbg)
			kfree(devp->af_dbg);
		devp->af_dbg = af;
		devp->flag |= ISP_FLAG_AF_DBG;
		pr_info("%s:full scan from %u-%u-%u-%u.\n",__func__,af->min_step,
				af->pre_threshold,af->post_threshold,af->max_step);
	}else if(!strcmp(parm[0],"print")){
		unsigned int i=0,cursor=0;
		af = devp->af_dbg;
		if(IS_ERR_OR_NULL(af))
			return len;
		devp->flag &=(~ISP_FLAG_AF_DBG);
		pr_info("ac[0]   ac[1]    ac[2]   ac[3]   dc[0]   dc[1]   dc[2]   dc[3]   af0_ac   af1_ac\n");
		if(af->dir){
			for (i=af->min_step; i <= af->max_step;i+=af->mid_step){
				cursor = i;
				while((af->data[cursor].ac[0]==0)&&(cursor!=0)){
					cursor--;
				}
				pr_info("step[%4u]:%u %u %u %u %u %u %u %u %u %u\n",
					i,af->data[cursor].ac[0],af->data[cursor].ac[1],
					af->data[cursor].ac[2],af->data[cursor].ac[3],
					af->data[cursor].dc[0],af->data[cursor].dc[1],
					af->data[cursor].dc[2],af->data[cursor].dc[3],
					af->data[cursor].af_ac[0],af->data[cursor].af_ac[1]);
				msleep(10);
			}
		}else{
			for (i=af->max_step; i <= af->min_step;i+=af->mid_step){
				cursor = i;
				while((af->data[cursor].ac[0]==0)&&(cursor!=0)){
					cursor--;
				}
				pr_info("step[%4u]:%u %u %u %u %u %u %u %u %u %u\n",
					i,af->data[cursor].ac[0],af->data[cursor].ac[1],
					af->data[cursor].ac[2],af->data[cursor].ac[3],
					af->data[cursor].dc[0],af->data[cursor].dc[1],
					af->data[cursor].dc[2],af->data[cursor].dc[3],
					af->data[cursor].af_ac[0],af->data[cursor].af_ac[1]);
				msleep(10);
			}
		}
		pr_info("%s:full scan end.\n",__func__);
		kfree(af);
		devp->af_dbg = NULL;
	}else if(!strcmp(parm[0],"blnr_en")){
		if(parm[1])
			devp->vs_cnt = simple_strtol(parm[1],NULL,10);
		else
			devp->vs_cnt = 4;
		devp->flag |= ISP_FLAG_BLNR;
	}else if(!strcmp(parm[0],"af_test")){
		devp->af_test.max = simple_strtol(parm[1],NULL,10);
		if(devp->af_test.af_win)
			kfree(devp->af_test.af_win);
		if(devp->af_test.af_bl)
			kfree(devp->af_test.af_bl);
		devp->af_test.af_win = kmalloc(sizeof(isp_af_stat_t)*devp->af_test.max,GFP_KERNEL);
		devp->af_test.af_bl = kmalloc(sizeof(isp_blnr_stat_t)*devp->af_test.max,GFP_KERNEL);
		devp->af_test.cnt = 0;
		devp->flag |= ISP_TEST_FOR_AF_WIN;
	}else if(!strcmp(parm[0],"af_print")){
		int i = 0;
		pr_info("af:f0_win0 f1_win0 f0_win1 f1_win1 f0_win2 f1_win2 f0_win3 f1_win3 f0_win4 f1_win4 f0_win5 f1_win5 f0_win6 f1_win6 f0_win7 f1_win7\n");
		for(i=0;i<devp->af_test.cnt;i++){
			pr_info("%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
				devp->af_test.af_win[i].luma_win[0],devp->af_test.af_win[i].luma_win[1],
				devp->af_test.af_win[i].luma_win[2],devp->af_test.af_win[i].luma_win[3],
				devp->af_test.af_win[i].luma_win[4],devp->af_test.af_win[i].luma_win[5],
				devp->af_test.af_win[i].luma_win[6],devp->af_test.af_win[i].luma_win[7],
				devp->af_test.af_win[i].luma_win[8],devp->af_test.af_win[i].luma_win[9],
				devp->af_test.af_win[i].luma_win[10],devp->af_test.af_win[i].luma_win[11],
				devp->af_test.af_win[i].luma_win[12],devp->af_test.af_win[i].luma_win[13],
				devp->af_test.af_win[i].luma_win[14],devp->af_test.af_win[i].luma_win[15]);
			msleep(1);
		}
		pr_info("blnr:ac0 ac1 ac2 ac3 dc0 dc1 dc2 dc3\n");
		for(i=0;i<devp->af_test.cnt;i++){
			pr_info("%u %u %u %u %u %u %u %u\n", devp->af_test.af_bl[i].ac[0],
				devp->af_test.af_bl[i].ac[1],devp->af_test.af_bl[i].ac[2],
				devp->af_test.af_bl[i].ac[3],devp->af_test.af_bl[i].dc[0],
				devp->af_test.af_bl[i].dc[1],devp->af_test.af_bl[i].dc[2],
				devp->af_test.af_bl[i].dc[3]);
		}
		kfree(devp->af_test.af_bl);
		kfree(devp->af_test.af_win);
		devp->af_test.af_bl = NULL;
		devp->af_test.af_win = NULL;
	}

	kfree(buf_orig);

	return len;
}

static ssize_t af_debug_show(struct device *dev,struct device_attribute *attr, char* buf)
{
	size_t len = 0;

	isp_dev_t *devp = dev_get_drvdata(dev);
	len += sprintf(buf+len,"0x%x 0x%x 0x%x 0x%x\n",devp->blnr_stat.dc[0],devp->blnr_stat.dc[1],devp->blnr_stat.dc[2],devp->blnr_stat.dc[3]);

        return len;
}

static void af_stat(struct af_debug_s *af,cam_function_t *ops)
{
	if (af->state == 0) {
		if(ops&&ops->set_af_new_step)
			ops->set_af_new_step(ops->priv_data,af->cur_step);
		af->state = 1;
		if(af_pr)
			pr_info("set step %u.\n",af->cur_step);
	}else if(af->state == af->delay) {
		af->state = 0;
		if(af->cur_step >= af->post_threshold)
			af->step = af->post_step;
		else if(af->cur_step >= af->pre_threshold)
			af->step = af->mid_step;
		else
			af->step = af->post_step;

		if(af->dir){
			af->cur_step += af->step;
			if (af->cur_step > af->max_step){
				af->cur_step = 0;
				/*stop*/
				af->state = 0xffffffff;
				ops->set_af_new_step(ops->priv_data,0);
				pr_info("%s get statics ok.\n",__func__);
			}
		}else{
			af->cur_step -= af->step;
			if (af->cur_step <= af->max_step){
				af->cur_step = 0;
				/*stop*/
				af->state = 0xffffffff;
				ops->set_af_new_step(ops->priv_data,0);
				pr_info("%s get statics ok.\n",__func__);
			}
		}
	}
        return;

}
static DEVICE_ATTR(af_debug, 0664, af_debug_show, af_debug_store);


static ssize_t ae_param_store(struct device *dev,struct device_attribute *attr, const char* buf, size_t len)
{

        char *parm[18]={NULL};
	isp_dev_t *devp;
	char *buf_orig = kstrdup(buf, GFP_KERNEL);
	parse_param(buf_orig,(char **)&parm);
	devp = dev_get_drvdata(dev);
	if(!devp->isp_ae_parm||!buf){
		pr_err("[%s..]%s %s error.isp device has't started.\n",DEVICE_NAME,__func__,buf);
		return len;
	}
	set_ae_parm(devp->isp_ae_parm,(char **)&parm);
	kfree(buf_orig);
	return len;
}

static ssize_t ae_param_show(struct device *dev,struct device_attribute *attr, char* buf)
{
	size_t len = 0;
	isp_dev_t *devp;
	char *buff="show";

	devp = dev_get_drvdata(dev);
	if(!devp->isp_ae_parm){
		pr_err("[%s..]%s %s error,isp device has't started.\n",DEVICE_NAME,__func__,buf);
		return len;
	}
	set_ae_parm(devp->isp_ae_parm,&buff);
	return len;
}
static DEVICE_ATTR(ae_param, 0664, ae_param_show, ae_param_store);

static ssize_t awb_param_store(struct device *dev,struct device_attribute *attr, const char* buf, size_t len)
{
        char *parm[21]={NULL};
	isp_dev_t *devp;
	char *buf_orig = kstrdup(buf, GFP_KERNEL);
	parse_param(buf_orig,(char **)&parm);
	devp = dev_get_drvdata(dev);
	if(!devp->isp_awb_parm||!buf){
		pr_err("[%s..]%s %s error.isp device has't started.\n",DEVICE_NAME,__func__,buf);
		return len;
	}
	set_awb_parm(devp->isp_awb_parm,(char **)&parm);
	kfree(buf_orig);
	return len;
}

static ssize_t awb_param_show(struct device *dev,struct device_attribute *attr, char* buf)
{
	size_t len = 0;
	char *buff ="show";
	isp_dev_t *devp;

	devp = dev_get_drvdata(dev);
	if(!devp->isp_awb_parm){
		len += sprintf(buf+len,"[%s..]%s isp device has't started.\n",DEVICE_NAME,__func__);
		return len;
	}
	set_awb_parm(devp->isp_awb_parm,&buff);
	return len;
}

static DEVICE_ATTR(awb_param, 0664, awb_param_show, awb_param_store);

static ssize_t af_param_store(struct device *dev,struct device_attribute *attr, const char* buf, size_t len)
{
        char *parm[3]={NULL};
	isp_dev_t *devp;
	char *buf_orig = kstrdup(buf, GFP_KERNEL);
	parse_param(buf_orig,(char **)&parm);
	devp = dev_get_drvdata(dev);
	if(!devp->isp_af_parm||!buf){
		pr_err("[%s..]%s %s error.isp device has't started.\n",DEVICE_NAME,__func__,buf);
		return len;
	}
	set_af_parm(devp->isp_af_parm,(char **)&parm);
	kfree(buf_orig);
	return len;
}

static ssize_t af_param_show(struct device *dev,struct device_attribute *attr, char* buf)
{
	size_t len = 0;
	char *buff="show";
	isp_dev_t *devp;

	devp = dev_get_drvdata(dev);
	if(!devp->isp_af_parm){
		len += sprintf(buf+len,"[%s..]%s isp device has't started.\n",DEVICE_NAME,__func__);
		return len;
	}
	set_af_parm(devp->isp_af_parm,&buff);
	return len;
}

static DEVICE_ATTR(af_param, 0664, af_param_show, af_param_store);

static ssize_t capture_param_store(struct device *dev,struct device_attribute *attr, const char* buf, size_t len)
{
        char *parm[3];
	isp_dev_t *devp;
	char *buf_orig = kstrdup(buf, GFP_KERNEL);
	parse_param(buf_orig,(char **)&parm);
	devp = dev_get_drvdata(dev);
	if(!devp->capture_parm||!buf){
		pr_err("[%s..]%s %s error.isp device has't started.\n",DEVICE_NAME,__func__,buf);
		return len;
	}
	set_cap_parm(devp->capture_parm,(char **)&parm);
	kfree(buf_orig);
	return len;
}

static ssize_t capture_param_show(struct device *dev,struct device_attribute *attr, char* buf)
{
	size_t len = 0;
	char *buff="show";
		isp_dev_t *devp;

	devp = dev_get_drvdata(dev);
	if(!devp->capture_parm){
		len += sprintf(buf+len,"[%s..]%s isp device has't started.\n",DEVICE_NAME,__func__);
		return len;
	}
	set_cap_parm(devp->capture_parm,&buff);
	return len;
}

static DEVICE_ATTR(cap_param, 0664, capture_param_show, capture_param_store);

static ssize_t wave_param_store(struct device *dev,struct device_attribute *attr, const char* buf, size_t len)
{
        char *parm[3];
	isp_dev_t *devp;
	char *buf_orig = kstrdup(buf, GFP_KERNEL);
	parse_param(buf_orig,(char **)&parm);
	devp = dev_get_drvdata(dev);
	if(!devp->wave||!buf){
		pr_err("[%s..]%s %s error.isp device has't started.\n",DEVICE_NAME,__func__,buf);
		return len;
	}
	if(!strcmp(parm[0],"torch")){
		unsigned int level = simple_strtol(parm[1],NULL,10);
		pr_info("%s:set torch level to %u.\n",__func__,level);
		torch_level(devp->flash.mode_pol_inv,devp->flash.led1_pol_inv,devp->flash.pin_mux_inv,devp->flash.torch_pol_inv,devp->wave,level);
	}else{
		set_wave_parm(devp->wave,(char **)&parm);
	}
	kfree(buf_orig);
	return len;
}

static ssize_t wave_param_show(struct device *dev,struct device_attribute *attr, char* buf)
{
	size_t len = 0;
	char *buff="show";
		isp_dev_t *devp;

	devp = dev_get_drvdata(dev);
	if(!devp->wave){
		len += sprintf(buf+len,"[%s..]%s isp device has't started.\n",DEVICE_NAME,__func__);
		return len;
	}
	set_wave_parm(devp->wave,&buff);
	return len;
}
static DEVICE_ATTR(wave_param, 0664, wave_param_show, wave_param_store);

static ssize_t gamma_debug_store(struct device *dev,struct device_attribute *attr, const char* buf, size_t len)
{
	unsigned int curve_ratio,r,g,b;
	if(buf){
		curve_ratio = simple_strtol(buf,NULL,16);
    	        r = (curve_ratio >> 8) & 15;
		g = (curve_ratio >> 4) & 15;
		b = (curve_ratio >> 0) & 15;
		pr_info("curve ratio r:%u,g:%u,b:%u.\n",r,g,b);
		if(!set_gamma_table_with_curve_ratio(r,g,b))
			pr_info("%s:set gamma error.\n",__func__);
	}else{
		pr_info("%s:null pointer error.\n",__func__);
	}
	return len;
}
static ssize_t gamma_debug_show(struct device *dev,struct device_attribute *attr,char *buf)
{
	unsigned short *gammaR, *gammaG, *gammaB, i;
	gammaR = kmalloc(257 * sizeof(unsigned short), GFP_KERNEL);
	gammaG = kmalloc(257 * sizeof(unsigned short), GFP_KERNEL);
	gammaB = kmalloc(257 * sizeof(unsigned short), GFP_KERNEL);
	get_isp_gamma_table(gammaR,GAMMA_R);
	get_isp_gamma_table(gammaG,GAMMA_G);
	get_isp_gamma_table(gammaB,GAMMA_B);
	pr_info("  r        g         b.\n");
	for(i=0;i<257;i++){
		pr_info("0x%3x    0x%3x    0x%3x\n",
                        gammaR[i],gammaG[i],gammaB[i]);
		msleep(1);
	}
	kfree(gammaR);
	kfree(gammaG);
	kfree(gammaB);
	return 0;
}

static DEVICE_ATTR(gamma_debug, 0664, gamma_debug_show, gamma_debug_store);

static ssize_t gamma_show(struct device *dev,struct device_attribute *attr,char *buf)
{
	pr_info("Usage:");
	pr_info("	echo sgr|sgg|sgb xxx...xx > /sys/class/register/gamma\n");
	pr_info("Notes:");
	pr_info("	if the string xxx......xx is less than 257*3,");
	pr_info("	then the remaining will be set value 0\n");
	pr_info("	if the string xxx......xx is more than 257*3, ");
	pr_info("	then the remaining will be ignored\n");
	return 0;
}

static ssize_t gamma_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buffer, size_t count)
{

	char *buf_orig, *parm[4];
	unsigned short *gammaR, *gammaG, *gammaB;
	unsigned int gamma_count;
	char gamma[4];
	int i = 0;

	/* to avoid the bellow warning message while compiling:
	 * warning: the frame size of 1576 bytes is larger than 1024 bytes
	 */
	gammaR = kmalloc(257 * sizeof(unsigned short), GFP_KERNEL);
	gammaG = kmalloc(257 * sizeof(unsigned short), GFP_KERNEL);
	gammaB = kmalloc(257 * sizeof(unsigned short), GFP_KERNEL);

	buf_orig = kstrdup(buffer, GFP_KERNEL);
	parse_param(buf_orig,(char **)&parm);

	if ((parm[0][0] == 's') && (parm[0][1] == 'g')) {
		memset(gammaR, 0, 257 * sizeof(unsigned short));
		gamma_count = (strlen(parm[1]) + 2) / 3;
		if (gamma_count > 257)
			gamma_count = 257;

		for (i = 0; i < gamma_count; ++i) {
			gamma[0] = parm[1][3 * i + 0];
			gamma[1] = parm[1][3 * i + 1];
			gamma[2] = parm[1][3 * i + 2];
			gamma[3] = '\0';
			gammaR[i] = simple_strtol(gamma, NULL, 16);
		}

		switch (parm[0][2]) {
		case 'r':
			set_isp_gamma_table(gammaR, GAMMA_R);
			break;

		case 'g':
			set_isp_gamma_table(gammaR, GAMMA_G);
			break;

		case 'b':
			set_isp_gamma_table(gammaR, GAMMA_B);
			break;
		default:
			break;
		}
	} else {
		pr_info("invalid command\n");
		pr_info("please: cat /sys/class/isp/isp0/gamma");

	}
	kfree(buf_orig);
	kfree(gammaR);
	kfree(gammaG);
	kfree(gammaB);
	return count;
}

static DEVICE_ATTR(gamma, 0664, gamma_show, gamma_store);
#if 0//for no use
static ssize_t ls_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        ssize_t len = 0;

        len += sprintf(buf+len," command format:\n");
        len += sprintf(buf+len," echo psize_v2h ocenter_c2l ocenter_c2t gain_0db curvature_gr curvature_r curvature_b curvature_gb force_enable > ... \n");

        len += sprintf(buf+len," Example:\n");
        len += sprintf(buf+len," echo enable 100 50 50 0 120 120 120 120 1 > /sys/class/isp/isp0/lens \n");

        return len;
}

static ssize_t ls_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t len)
{
	char *buf_orig, *parm[10]={NULL};
	unsigned int psize_v2h=0,ocenter_c2l=0,ocenter_c2t=0,gain_0db=0,curvature_gr=0,curvature_r=0;
        unsigned int curvature_b=0,curvature_gb=0;
        isp_dev_t *devp;
	isp_info_t *info = &devp->info;
        bool force_enable=false;
	devp = dev_get_drvdata(dev);


	/* to avoid the bellow warning message while compiling:
	 * warning: the frame size of 1576 bytes is larger than 1024 bytes
	 */
        if(!buf)
                return len;

	buf_orig = kstrdup(buf, GFP_KERNEL);
	parse_param(buf_orig,(char **)&parm);
	if(!strcmp(parm[0],"enable")) {
	        psize_v2h = simple_strtoul(     parm[1],NULL,10);
	        ocenter_c2l = simple_strtoul(   parm[2],NULL,10);
	        ocenter_c2t = simple_strtoul(   parm[3],NULL,10);
	        gain_0db = simple_strtoul(      parm[4],NULL,10);
	        curvature_gr = simple_strtoul(  parm[5],NULL,10);
	        curvature_r = simple_strtoul(   parm[6],NULL,10);
                curvature_b = simple_strtoul(   parm[7],NULL,10);
                curvature_gb = simple_strtoul(  parm[8],NULL,10);
                force_enable = simple_strtoul(  parm[9],NULL,10);

                pr_info("psize_v2h:%u hactive:%u vactive:%u ocenter_c2l:%u ocenter_c2t:%u gain_0db:%u curvature_gr:%u curvature_r:%u curvature_b:%u curvature_gb:%u force_enable:%u \n", \
                psize_v2h,info->h_active,info->v_active,ocenter_c2l,ocenter_c2t,gain_0db, \
                curvature_gr,curvature_r, curvature_b,curvature_gb,force_enable);

                isp_ls_curve(psize_v2h,info->h_active,info->v_active,ocenter_c2l,ocenter_c2t,gain_0db, \
                curvature_gr,curvature_r, curvature_b,curvature_gb,force_enable);
        }
	kfree(buf_orig);

	return len;
}

static DEVICE_ATTR(lens, 0664, ls_show, ls_store);
#endif
/*
*get aet current state
*cat /sys/class/isp/isp0/aet
*set aet new step
*echo x >/sys/class/isp/isp0/aet
*/
static ssize_t aet_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        ssize_t len = 0;
	struct cam_function_s *cam_func;
	isp_dev_t *devp = dev_get_drvdata(dev);

        if(!buf)
                return len;
	if(devp->cam_param)
		cam_func = &(devp->cam_param->cam_function);
	else
		return len;
        len += sprintf(buf+len,"aet_current_step: %u.\n",cam_func->get_aet_current_step(cam_func->priv_data));
        len += sprintf(buf+len,"aet_current_gain:%u.\n",cam_func->get_aet_current_gain(cam_func->priv_data));
        len += sprintf(buf+len,"aet_min_gain:%u.\n",cam_func->get_aet_min_gain(cam_func->priv_data));
        len += sprintf(buf+len,"aet_max_gain:%u.\n",cam_func->get_aet_max_gain(cam_func->priv_data));
        len += sprintf(buf+len,"aet_max_step:%u.\n",cam_func->get_aet_max_step(cam_func->priv_data));

        return len;
}

static ssize_t aet_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t len)
{
	char *buf_orig,*parm[4]={NULL};
	struct cam_function_s *cam_func;
        isp_dev_t *devp=dev_get_drvdata(dev);

        if(!buf)
                return len;
	buf_orig = kstrdup(buf, GFP_KERNEL);
	parse_param(buf_orig,(char **)&parm);
	if(devp->cam_param)
		cam_func = &(devp->cam_param->cam_function);
	else
		return len;

	if(parm[0]){
		unsigned int step = simple_strtol(parm[0],NULL,10);
		cam_func->set_aet_new_step(cam_func->priv_data,step,1,1);
	}

	kfree(buf_orig);

	return len;
}

static DEVICE_ATTR(aet, 0664, aet_show, aet_store);

#ifndef USE_WORK_QUEUE
static int isp_thread(isp_dev_t *devp) {
	unsigned newstep = 0;
	struct cam_function_s *func = &devp->cam_param->cam_function;
	printk("isp_thread is run! \n");
	while (1) {
	        if(ae_flag&0x2) {
		        ae_flag &= (~0x2);
		        newstep = isp_tune_exposure(devp);
		        printk("set new step2 %d \n",newstep);
		        if(func&&func->set_aet_new_step)
		                func->set_aet_new_step(func->priv_data,newstep,true,true);
	        }
	        if(atomic_read(&devp->ae_info.writeable)&&func&&func->set_aet_new_step){
		        if(isp_debug)
			        printk("[isp] set new step:%d \n",ae_sens.new_step);
		        if(ae_adjust_enable)
			        func->set_aet_new_step(func->priv_data,ae_sens.new_step,ae_sens.shutter,ae_sens.gain);
		        atomic_set(&devp->ae_info.writeable,0);
	        }
	        if(devp->flag&ISP_FLAG_AF_DBG){
		        af_stat(devp->af_dbg,func);
	        }
	        if(devp->flag & ISP_AF_SM_MASK) {
		        if(atomic_read(&devp->af_info.writeable)&&func&&func->set_af_new_step){
			        func->set_af_new_step(func->priv_data,devp->af_info.cur_step);
			        atomic_set(&devp->af_info.writeable,0);
		        }
	        }
                if(kthread_should_stop())
                        break;
	}
}

static int start_isp_thread(isp_dev_t *devp) {
	if(!devp->kthread) {
		devp->kthread = kthread_run(isp_thread, devp, "isp");
		if(IS_ERR(devp->kthread)) {
			pr_err("[%s..]%s thread creating error.\n",DEVICE_NAME,__func__);
			return -1;
		}
		wake_up_process(devp->kthread);
	}
	return 0;
}

static void stop_isp_thread(isp_dev_t *devp) {
    if(devp->kthread){
        send_sig(SIGTERM, devp->kthread, 1);
        kthread_stop(devp->kthread);
        devp->kthread = NULL;
    }
}
#endif

static int isp_support(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
        if(port == TVIN_PORT_ISP)
                return 0;
        else
                return -1;
}

static int isp_fe_open(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
	isp_dev_t *devp = container_of(fe,isp_dev_t,frontend);
	vdin_parm_t *parm = (vdin_parm_t*)fe->private_data;
	isp_info_t *info = &devp->info;

	info->fe_port = parm->isp_fe_port;
	info->bayer_fmt = parm->cfmt;
	info->cfmt = TVIN_YUV422;
	info->dfmt = parm->dfmt;
	info->h_active = parm->h_active;
	info->v_active = parm->v_active;
	info->dest_hactive = parm->dest_hactive;
	info->dest_vactive = parm->dest_vactive;
	info->frame_rate = parm->frame_rate;
	info->skip_cnt = 0;
	devp->isp_fe = tvin_get_frontend(info->fe_port, 0);
	if(devp->isp_fe && devp->isp_fe->dec_ops) {
		devp->isp_fe->private_data = fe->private_data;
		devp->isp_fe->dec_ops->open(devp->isp_fe, info->fe_port);
		pr_info("[%s..]%s: open %s ok.\n",DEVICE_NAME,__func__,tvin_port_str(info->fe_port));
	} else {
		pr_info("[%s..]%s:get %s frontend error.\n",DEVICE_NAME,__func__,tvin_port_str(info->fe_port));
	}
	/*open the isp to vdin path,power on the isp hw module*/
	switch_vpu_mem_pd_vmod(VPU_ISP,VPU_MEM_POWER_ON);
        devp->cam_param = (cam_parameter_t*)parm->reserved;
	if(IS_ERR_OR_NULL(devp->cam_param)){
		pr_err("[%s..] camera parameter error use default config.\n",__func__);
		isp_load_def_setting(info->h_active,info->v_active,0);
	}  else {
		unsigned int i = 0;
		devp->isp_ae_parm = &devp->cam_param->xml_scenes->ae;
		devp->isp_awb_parm = &devp->cam_param->xml_scenes->awb;
		devp->isp_af_parm = &devp->cam_param->xml_scenes->af;
		devp->isp_af_parm->valid_step_cnt = 0;
		for(i = 0;devp->isp_af_parm->step[i] != 0;i++){
			devp->isp_af_parm->valid_step_cnt++;
		}
		devp->capture_parm = devp->cam_param->xml_capture;
		devp->wave = devp->cam_param->xml_wave;
		isp_hw_enable(false);
		isp_set_def_config(devp->cam_param->xml_regs_map,info->fe_port,info->bayer_fmt,info->h_active,info->v_active);
		isp_set_manual_wb(devp->cam_param->xml_wb_manual);
		if (rgb_mode){
			isp_bypass_for_rgb();
			info->cfmt = TVIN_RGB444;
		}
		/*test for wb test disable gamma & lens*/
		if(devp->flag & ISP_FLAG_TEST_WB)
			disable_gc_lns_pk(false);
		/*enable isp hw*/
		isp_hw_enable(true);
		devp->af_info.x0 = info->h_active/devp->isp_af_parm->win_ratio;
		devp->af_info.y0 = info->v_active/devp->isp_af_parm->win_ratio;
		devp->af_info.x1 = info->h_active - devp->af_info.x0;
		devp->af_info.y1 = info->v_active - devp->af_info.y0;
		devp->af_info.radius = info->h_active/devp->isp_af_parm->radius_ratio;
		devp->af_info.af_detect = kmalloc(sizeof(isp_blnr_stat_t)*devp->isp_af_parm->detect_step_cnt,GFP_KERNEL);
		devp->af_info.v_dc = kmalloc(sizeof(unsigned long long)*devp->isp_af_parm->detect_step_cnt,GFP_KERNEL);
	}
        return 0;
}

static void isp_fe_close(struct tvin_frontend_s *fe)
{
        isp_dev_t *devp = container_of(fe,isp_dev_t,frontend);
		if(devp->af_info.af_detect)
			kfree(devp->af_info.af_detect);
		if(devp->af_info.v_dc)
			kfree(devp->af_info.v_dc);
	if(devp->isp_fe)
		devp->isp_fe->dec_ops->close(devp->isp_fe);
        memset(&devp->info,0,sizeof(isp_info_t));
	/*power down isp hw*/
	isp_hw_enable(false);
	switch_vpu_mem_pd_vmod(VPU_ISP,VPU_MEM_POWER_DOWN);

}

static void isp_fe_start(struct tvin_frontend_s *fe, enum tvin_sig_fmt_e fmt)
{
        isp_dev_t *devp = container_of(fe,isp_dev_t,frontend);

	pr_info("[%s..]%s:isp start.\n",DEVICE_NAME,__func__);

	if(devp->isp_fe)
	        devp->isp_fe->dec_ops->start(devp->isp_fe,fmt);

	/*configuration the hw,load reg table*/

	if(!IS_ERR_OR_NULL(devp->cam_param)) {
	        if(devp->cam_param->cam_mode == CAMERA_CAPTURE){
		        devp->flag = ISP_FLAG_CAPTURE;
			capture_sm_init(devp);
	        }else if(devp->cam_param->cam_mode == CAMERA_RECORD){
		        devp->flag = ISP_FLAG_RECORD;
#ifndef USE_WORK_QUEUE
			start_isp_thread(devp);
#endif
	        }else{
		        devp->flag &= (~ISP_WORK_MODE_MASK);
#ifndef USE_WORK_QUEUE
			start_isp_thread(devp);
#endif
	        }
        }
#ifndef USE_WORK_QUEUE
    tasklet_enable(&devp->isp_task);
#endif
        devp->flag |= ISP_FLAG_START;
	return;
}
static void isp_fe_stop(struct tvin_frontend_s *fe, enum tvin_port_e port)
{
        isp_dev_t *devp = container_of(fe,isp_dev_t,frontend);
	if(devp->isp_fe)
	        devp->isp_fe->dec_ops->stop(devp->isp_fe,devp->info.fe_port);
#ifndef USE_WORK_QUEUE
	tasklet_disable_nosync(&devp->isp_task);
#else
	cancel_work_sync(&devp->isp_wq);
	printk("[isp]:cancel work queue\n");
#endif
	if(devp->cam_param->cam_mode != CAMERA_CAPTURE){
#ifndef USE_WORK_QUEUE
		stop_isp_thread(devp);
#endif
	}
	devp->flag &= (~ISP_FLAG_AF);
	devp->flag &= (~ISP_FLAG_TOUCH_AF);
	isp_sm_uninit(devp);
	/*disable hw*/
        devp->flag &= (~ISP_FLAG_START);
}
static int isp_fe_ioctl(struct tvin_frontend_s *fe, void *arg)
{
	unsigned int x0,y0,x1,y1;
	xml_wb_manual_t *wb;
	isp_dev_t *devp = container_of(fe,isp_dev_t,frontend);
	cam_parameter_t *param = (cam_parameter_t *)arg;
	enum cam_command_e cmd;
	cam_cmd_state_t ret = CAM_STATE_SUCCESS;
	if(IS_ERR_OR_NULL(param)||rgb_mode) {
		if(ioctl_debug)
                        pr_err("[%s..]camera parameter can't be null.\n",DEVICE_NAME);
                return -1;
        }
	cmd = param->cam_command;
	devp->cam_param = param;
	if(ioctl_debug)
	 	pr_info("[%s..]%s:cmd: %s run mode %u.\n",DEVICE_NAME,__func__,cam_cmd_to_str(cmd),devp->cam_param->cam_mode);
	switch(cmd) {
                case CAM_COMMAND_INIT:
		        break;
		case CAM_COMMAND_GET_STATE:
			ret = devp->cmd_state;
			break;
                case CAM_COMMAND_SCENES:
		        devp->isp_ae_parm = &param->xml_scenes->ae;
		        devp->isp_awb_parm = &param->xml_scenes->awb;
		        devp->isp_af_parm = &param->xml_scenes->af;
		        devp->capture_parm = param->xml_capture;
		        devp->flag |= ISP_FLAG_SET_SCENES;
		        break;
                case CAM_COMMAND_EFFECT:
	                devp->flag |= ISP_FLAG_SET_EFFECT;
		        break;
                case CAM_COMMAND_AWB:
			if(!(devp->flag & ISP_FLAG_CAPTURE)){
				wb = devp->cam_param->xml_wb_manual;
				isp_set_manual_wb(wb);
	               	        devp->flag |= ISP_FLAG_AWB;
			}
		        break;
		case CAM_COMMAND_MWB:
			devp->flag &= (~ISP_FLAG_AWB);
	                devp->flag |= ISP_FLAG_MWB;
		        break;
		case CAM_COMMAND_SET_WORK_MODE:
			if(devp->cam_param->cam_mode == CAMERA_CAPTURE)
		                devp->flag = ISP_FLAG_CAPTURE;
	                else if(devp->cam_param->cam_mode == CAMERA_RECORD)
		                devp->flag = ISP_FLAG_RECORD;
	                else
		                devp->flag &= (~ISP_WORK_MODE_MASK);
			break;
                // ae related
                case CAM_COMMAND_AE_ON:
			if(!(devp->flag & ISP_FLAG_CAPTURE)){
		        	isp_sm_init(devp);
		        	devp->flag |= ISP_FLAG_AE;
			}
		        break;
                case CAM_COMMAND_AE_OFF:
		        devp->flag &= (~ISP_FLAG_AE);
		        break;
		case CAM_COMMAND_SET_AE_LEVEL:
			devp->ae_info.manul_level = devp->cam_param->exposure_level;
			isp_set_manual_exposure(devp);
			break;
                // af related
                case CAM_COMMAND_AF:
			devp->flag |= ISP_FLAG_AF;
			af_sm_init(devp);
		        break;
                case CAM_COMMAND_FULLSCAN:
			isp_set_af_scan_stat(devp->af_info.x0,devp->af_info.y0,devp->af_info.x1,devp->af_info.y1);
			devp->flag |= ISP_FLAG_TOUCH_AF;
			devp->cmd_state = CAM_STATE_DOING;
			af_sm_init(devp);
		        break;
                case CAM_COMMAND_TOUCH_FOCUS:
			if(!(devp->flag & ISP_FLAG_CAPTURE)){
				devp->flag |= ISP_FLAG_TOUCH_AF;
			        //devp->isp_af_parm = &param->xml_scenes->af;
			        devp->isp_af_parm->x = param->xml_scenes->af.x;
			        devp->isp_af_parm->y = param->xml_scenes->af.y;
			        x0 = devp->isp_af_parm->x>devp->af_info.radius?devp->isp_af_parm->x-devp->af_info.radius:0;
			        y0 = devp->isp_af_parm->y>devp->af_info.radius?devp->isp_af_parm->y-devp->af_info.radius:0;
			        x1 = devp->isp_af_parm->x + devp->af_info.radius;
			        y1 = devp->isp_af_parm->y + devp->af_info.radius;
			        if(x1 >= devp->info.h_active)
				        x1 = devp->info.h_active - 1;
			        if(y1 >= devp->info.v_active)
				        y1 = devp->info.v_active - 1;
			        if(ioctl_debug)
				        pr_info("focus win: center(%u,%u) left(%u %u) right(%u,%u).\n",devp->isp_af_parm->x,
					devp->isp_af_parm->y,x0,y0,x1,y1);
			        isp_set_af_scan_stat(x0,y0,x1,y1);
			        af_sm_init(devp);
			}
		        break;
                case CAM_COMMAND_CONTINUOUS_FOCUS_ON:
			if(!(devp->flag & ISP_FLAG_CAPTURE)){
			        devp->flag |= ISP_FLAG_AF;
				af_sm_init(devp);
			}
		        break;
                case CAM_COMMAND_CONTINUOUS_FOCUS_OFF:
			devp->flag &= (~ISP_FLAG_AF);
		        break;
                case CAM_COMMAND_BACKGROUND_FOCUS_ON:
		        break;
                case CAM_COMMAND_BACKGROUND_FOCUS_OFF:
		        break;
                // flash related
                case CAM_COMMAND_SET_FLASH_MODE:
			isp_set_flash_mode(devp);
		        break;
                // torch related
                case CAM_COMMAND_TORCH:
		        devp->wave = param->xml_wave;
		        torch_level(devp->flash.mode_pol_inv,devp->flash.led1_pol_inv,devp->flash.pin_mux_inv,devp->flash.torch_pol_inv,devp->wave,param->level);
		        break;
		case CMD_ISP_BYPASS:
			isp_bypass_all();
			break;
	        default:
		        break;
	}
	return ret;
}
static int isp_fe_isr(struct tvin_frontend_s *fe, unsigned int hcnt64)
{
	xml_csc_t *csc;
	xml_wb_manual_t *wb;
	af_debug_t *af;
        isp_dev_t *devp = container_of(fe,isp_dev_t,frontend);
	struct isp_af_info_s *af_info = &devp->af_info;
	int ret = 0;
	if(IS_ERR_OR_NULL(devp->cam_param)){
		pr_info("%s:null pointer error.\n",__func__);
	}
	if(devp->flag & ISP_FLAG_RECONFIG)
		return ret;

	if(awb_enable){
                isp_get_awb_stat(&devp->isp_awb);
	}
	if((devp->flag&ISP_FLAG_AE)&&(ae_enable)){
	        isp_get_ae_stat(&devp->isp_ae);
	}
	if((devp->flag&ISP_AF_SM_MASK)&&(af_enable)){
	        isp_get_blnr_stat(&af_info->isr_af_data);
		isp_get_af_scan_stat(&af_info->isr_af_data);
	}
	if(devp->flag & ISP_FLAG_SET_EFFECT){
		csc = &(devp->cam_param->xml_effect_manual->csc);
		isp_set_matrix(csc,devp->info.v_active);
		devp->flag &= (~ISP_FLAG_SET_EFFECT);
	}
	if(devp->flag&ISP_FLAG_CAPTURE){
		isp_get_blnr_stat(&devp->blnr_stat);
		isp_get_af_scan_stat(&devp->blnr_stat);
	}
	if(devp->flag&ISP_FLAG_BLNR){
		isp_get_blnr_stat(&devp->blnr_stat);
		isp_get_af_scan_stat(&devp->blnr_stat);
		if(devp->vs_cnt-- == 0)
			devp->flag &= (~ISP_FLAG_BLNR);
	}
	if(devp->flag & ISP_FLAG_AF_DBG){
		af = devp->af_dbg;
		if((af->state >= 1)&&(af->state <= af->delay)){
			isp_get_blnr_stat(&af->data[af->cur_step]);
			isp_get_af_scan_stat(&af->data[af->cur_step]);
			af->state++;
		}
	}
	if(devp->flag & ISP_TEST_FOR_AF_WIN){
		isp_get_blnr_stat(&devp->af_test.af_bl[devp->af_test.cnt]);
		//isp_get_af_scan_stat(&devp->af_test.af_bl[devp->af_test.cnt]);
		isp_get_af_stat(&devp->af_test.af_win[devp->af_test.cnt]);
		//isp_get_ae_stat(&devp->af_test.ae_win[devp->af_test.cnt]);
		devp->af_test.cnt+=1;
		if(devp->af_test.cnt >= devp->af_test.max){
			devp->flag &=(~ISP_TEST_FOR_AF_WIN);
			pr_info("get af win,ae win&blnr info end.\n");
		}
	}
	if(devp->flag&ISP_FLAG_MWB){
		wb = devp->cam_param->xml_wb_manual;
		isp_set_manual_wb(wb);
		devp->flag &=(~ISP_FLAG_MWB);
	}
	if(devp->flag&ISP_FLAG_SET_COMB4){
		isp_set_lnsd_mode(devp->debug.comb4_mode);
		devp->flag &= (~ISP_FLAG_SET_COMB4);
	}
	if(devp->isp_fe)
		ret = devp->isp_fe->dec_ops->decode_isr(devp->isp_fe,0);

	if(devp->flag & ISP_FLAG_CAPTURE)
		ret = max(isp_capture_sm(devp),ret);
	if(isr_debug&&ret)
		pr_info("%s isp %d buf.\n",__func__,ret);


#ifndef USE_WORK_QUEUE
	if(!(devp->flag & ISP_FLAG_TEST_WB))
	    tasklet_schedule(&devp->isp_task);
#else
        schedule_work(&devp->isp_wq);
#endif
        return ret;
}

#ifndef USE_WORK_QUEUE
static void isp_tasklet(unsigned long arg)
{
	isp_dev_t *devp = (isp_dev_t *)arg;
        if((devp->flag & ISP_FLAG_AE)&&(ae_enable)){
	    	isp_ae_sm(devp);
        }
	if((devp->flag&ISP_FLAG_AWB)&&(awb_enable)){
    		isp_awb_sm(devp);
	}
	if((devp->flag&ISP_AF_SM_MASK)&&(af_enable)){
		isp_af_detect(devp);
	}
}

#else

static void  isp_do_work(struct work_struct *work)
{

	isp_dev_t *devp = container_of(work, isp_dev_t, isp_wq);

	unsigned newstep = 0;
	struct cam_function_s *func = &devp->cam_param->cam_function;
	if(!(devp->flag & ISP_FLAG_START)){
		printk("[isp]%s:isp has stop\n",__func__);
		return;
	}
	if(!(devp->flag & ISP_FLAG_TEST_WB)){
		if((devp->flag & ISP_FLAG_AE)&&(ae_enable)){
			isp_ae_sm(devp);
		}
		if((devp->flag&ISP_FLAG_AWB)&&(awb_enable)){
			isp_awb_sm(devp);
		}
		if((devp->flag&ISP_AF_SM_MASK)&&(af_enable)){
			isp_af_detect(devp);
		}
	}

	if(ae_flag&0x2)	{
		ae_flag &= (~0x2);
		newstep = isp_tune_exposure(devp);
		printk("wq:set new step2 %d \n",newstep);
		if(func&&func->set_aet_new_step)
			func->set_aet_new_step(func->priv_data,newstep,true,true);
	}
	if(atomic_read(&devp->ae_info.writeable)&&func&&func->set_aet_new_step)	{
		if(isp_debug)
		        printk("[isp] wq:set new step:%d \n",ae_sens.new_step);
		if(ae_adjust_enable)
			func->set_aet_new_step(func->priv_data,ae_sens.new_step,ae_sens.shutter,ae_sens.gain);
		atomic_set(&devp->ae_info.writeable,0);
	}
	if(devp->flag&ISP_FLAG_AF_DBG){
		af_stat(devp->af_dbg,func);
	}
	if(devp->flag & ISP_AF_SM_MASK) {
		if(atomic_read(&devp->af_info.writeable)&&func&&func->set_af_new_step){
			func->set_af_new_step(func->priv_data,devp->af_info.cur_step);
			atomic_set(&devp->af_info.writeable,0);
		}
	}

}
#endif

static struct tvin_decoder_ops_s isp_dec_ops ={
        .support            = isp_support,
	.open               = isp_fe_open,
	.start              = isp_fe_start,
	.stop               = isp_fe_stop,
	.close              = isp_fe_close,
	.ioctl              = isp_fe_ioctl,
	.decode_isr         = isp_fe_isr,
};
static void isp_sig_propery(struct tvin_frontend_s *fe, struct tvin_sig_property_s *prop)
{
	isp_dev_t *devp = container_of(fe,isp_dev_t,frontend);

        prop->color_format = devp->info.cfmt;
	prop->dest_cfmt = devp->info.dfmt;

        prop->scaling4w = devp->info.dest_hactive;
	prop->scaling4h = devp->info.dest_vactive;

	prop->vs = 0;
	prop->ve = 0;
	prop->hs = 0;
	prop->he = 0;
        prop->decimation_ratio = 0;
}
static bool isp_frame_skip(struct tvin_frontend_s *fe)
{
	isp_dev_t *devp = container_of(fe,isp_dev_t,frontend);
	if(devp->isp_fe && devp->isp_fe->sm_ops){
		if(devp->isp_fe->sm_ops->check_frame_skip)
			return devp->isp_fe->sm_ops->check_frame_skip(devp->isp_fe);
	}
	return 0;
}

static struct tvin_state_machine_ops_s isp_sm_ops ={
       .get_sig_propery  = isp_sig_propery,
       .check_frame_skip = isp_frame_skip,
};

static int isp_open(struct inode *inode, struct file *file)
{
	isp_dev_t *devp;
	/* Get the per-device structure that contains this cdev */
	devp = container_of(inode->i_cdev, isp_dev_t, cdev);
	file->private_data = devp;
	return 0;
}

static int isp_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}
static struct file_operations isp_fops = {
	.owner	         = THIS_MODULE,
	.open	         = isp_open,
	.release         = isp_release,
};
static int isp_add_cdev(struct cdev *cdevp, struct file_operations *fops,
		int minor)
{
	int ret;
	dev_t devno = MKDEV(MAJOR(isp_devno), minor);
	cdev_init(cdevp, fops);
	cdevp->owner = THIS_MODULE;
	ret = cdev_add(cdevp, devno, 1);
	return ret;
}

static struct device * isp_create_device(struct device *parent, int minor)
{
	dev_t devno = MKDEV(MAJOR(isp_devno), minor);
	return device_create(isp_clsp, parent, devno, NULL, "%s%d",
			DEVICE_NAME, minor);
}

static void isp_delete_device(int minor)
{
	dev_t devno = MKDEV(MAJOR(isp_devno), minor);
	device_destroy(isp_clsp, devno);
}

static int isp_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct isp_dev_s *devp = NULL;
	devp = kmalloc(sizeof(isp_dev_t),GFP_KERNEL);
	memset(devp,0,sizeof(isp_dev_t));
	if(!devp){
		pr_err("[%s..]%s kmalloc error.\n",DEVICE_NAME,__func__);
		return -ENOMEM;
	}
	devp->index = pdev->id;
	devp->offset = addr_offset[devp->index];
	isp_devp[devp->index] = devp;
	/* create cdev and reigser with sysfs */
	ret = isp_add_cdev(&devp->cdev,&isp_fops,devp->index);

	devp->dev = isp_create_device(&pdev->dev,devp->index);

	ret = device_create_file(devp->dev,&dev_attr_debug);
	ret = device_create_file(devp->dev,&dev_attr_ae_param);
	ret = device_create_file(devp->dev,&dev_attr_awb_param);
	ret = device_create_file(devp->dev,&dev_attr_af_param);
	ret = device_create_file(devp->dev,&dev_attr_af_debug);
	ret = device_create_file(devp->dev,&dev_attr_cap_param);
	ret = device_create_file(devp->dev,&dev_attr_wave_param);
	ret = device_create_file(devp->dev,&dev_attr_gamma_debug);
	ret = device_create_file(devp->dev,&dev_attr_gamma);
	//ret = device_create_file(devp->dev,&dev_attr_lens);
	ret = device_create_file(devp->dev,&dev_attr_aet);
	if(ret < 0)
		goto err;

	sprintf(devp->frontend.name, "%s%d", DEVICE_NAME, devp->index);

	if(!tvin_frontend_init(&devp->frontend,&isp_dec_ops,&isp_sm_ops,devp->index)) {
		if(tvin_reg_frontend(&devp->frontend))
			pr_err("[%s..]%s register isp frontend error.\n",DEVICE_NAME,__func__);
	}
#ifdef USE_WORK_QUEUE
    INIT_WORK(&devp->isp_wq,(void (*)(struct work_struct *))isp_do_work);
#else
    tasklet_init(&devp->isp_task,isp_tasklet,(unsigned long)devp);
    tasklet_disable(&devp->isp_task);
#endif
	platform_set_drvdata(pdev,(void *)devp);
	dev_set_drvdata(devp->dev,(void *)devp);
	pr_info("[%s..]%s isp probe ok.\n",DEVICE_NAME,__func__);
	return 0;
err:
	isp_delete_device(devp->index);
	return 0;

}
static int isp_remove(struct platform_device *pdev)
{
	struct isp_dev_s *devp;
	devp = platform_get_drvdata(pdev);
	device_remove_file(devp->dev,&dev_attr_debug);
	device_remove_file(devp->dev,&dev_attr_ae_param);
	device_remove_file(devp->dev,&dev_attr_awb_param);
	device_remove_file(devp->dev,&dev_attr_af_param);
	device_remove_file(devp->dev,&dev_attr_cap_param);
	device_remove_file(devp->dev,&dev_attr_wave_param);
	//device_remove_file(devp->dev,&dev_attr_lens);

	isp_delete_device(devp->index);
        tvin_unreg_frontend(&devp->frontend);
#ifndef USE_WORK_QUEUE
	tasklet_kill(&devp->isp_task);
#endif
	kfree(devp);
	return 0;
}

static int isp_suspend(struct platform_device *pdev, pm_message_t state)
{
	wave_power_manage(false);
	return 0;
}

static int isp_resume(struct platform_device *pdev)
{
	wave_power_manage(true);
	return 0;
}
static struct platform_driver isp_driver = {
	.probe 	 = isp_probe,
	.remove  = isp_remove,
	.suspend = isp_suspend,
	.resume  = isp_resume,
	.driver = {
		.name = DEVICE_NAME,
	}
};

static struct platform_device *isp_dev[ISP_NUM];
static int __init isp_init_module(void)
{
	int i = 0;
	isp_clsp = class_create(THIS_MODULE, MODULE_NAME);
	if(IS_ERR(isp_clsp)) {
		pr_err();
	}
	for(i=0;i<ISP_NUM;i++) {
		isp_dev[i] = platform_device_alloc(DEVICE_NAME,i);
		platform_device_add(isp_dev[i]);
	}
	platform_driver_register(&isp_driver);

	return 0;
}


static void __exit isp_exit_module(void)
{
	int i = 0;
	for(i=0;i<ISP_NUM;i++){
		platform_device_del(isp_dev[i]);
	}
	platform_driver_unregister(&isp_driver);
	class_destroy(isp_clsp);
	return;
}
module_param(isp_debug,uint,0664);
MODULE_PARM_DESC(isp_debug,"\n debug flag for isp.\n");

module_param(ae_enable,uint,0664);
MODULE_PARM_DESC(ae_enable,"\n ae_enable.\n");

module_param(ae_adjust_enable,uint,0664);
MODULE_PARM_DESC(ae_adjust_enable,"\n ae_adjust_enable.\n");

module_param(awb_enable,uint,0664);
MODULE_PARM_DESC(awb_enable,"\n awb_enable.\n");

module_param(af_enable,uint,0664);
MODULE_PARM_DESC(af_enable,"\n af enable flag.\n");

module_param(ae_flag,uint,0664);
MODULE_PARM_DESC(ae_flag,"\n debug flag for ae_flag.\n");

module_param(af_pr,uint,0664);
MODULE_PARM_DESC(af_pr,"\n debug flag for af print.\n");

module_param(ioctl_debug,uint,0664);
MODULE_PARM_DESC(ioctl_debug,"\n debug ioctl function.\n");

module_param(isr_debug,uint,0664);
MODULE_PARM_DESC(isr_debug,"\n debug isr function.\n");

module_param(rgb_mode,bool,0664);
MODULE_PARM_DESC(rgb_mode,"\n debug for rgb output.\n");

MODULE_VERSION(ISP_VER);
module_init(isp_init_module);
module_exit(isp_exit_module);
MODULE_DESCRIPTION("AMLOGIC isp input driver");
MODULE_LICENSE("GPL");
