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
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <asm/fiq.h>
#include <asm/div64.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
/* Amlogic Headers */
#include <linux/amlogic/amports/canvas.h>
#include <mach/am_regs.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/amports/timestamp.h>
#include <linux/amlogic/amports/tsync.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include <linux/amlogic/aml_common.h>
#include <mach/irqs.h>
#include <mach/mod_gate.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#include <mach/vpu.h>
#endif
/* Local Headers */
#include "../tvin_global.h"
#include "../tvin_format_table.h"
#include "../tvin_frontend.h"
#include "vdin_regs.h"
#include "vdin_drv.h"
#include "vdin_ctl.h"
#include "vdin_sm.h"
#include "vdin_vf.h"
#include "vdin_canvas.h"


#define VDIN_NAME		"vdin"
#define VDIN_DRV_NAME		"vdin"
#define VDIN_MOD_NAME		"vdin"
#define VDIN_DEV_NAME		"vdin"
#define VDIN_CLS_NAME		"vdin"
#define PROVIDER_NAME		"vdin"

#define SMOOTH_DEBUG

#define VDIN_PUT_INTERVAL		(HZ/100)   //10ms, #define HZ 100

#define INVALID_VDIN_INPUT		0xffffffff

static dev_t vdin_devno;
static struct class *vdin_clsp;

unsigned int vdin_addr_offset[VDIN_MAX_DEVS] = {0x00, 0x70};
struct vdin_dev_s *vdin_devp[VDIN_MAX_DEVS];
static int callmaster_status = 0;

/*
 * canvas_config_mode
 * 0: canvas_config in driver probe
 * 1: start cofig
 * 2: auto config
 */
static int canvas_config_mode = 2;
module_param(canvas_config_mode, int, 0664);
MODULE_PARM_DESC(canvas_config_mode, "canvas configure mode");

static bool work_mode_simple = false;
module_param(work_mode_simple, bool, 0664);
MODULE_PARM_DESC(work_mode_simple, "enable/disable simple work mode");

static char *first_field_type = NULL;
module_param(first_field_type, charp, 0664);
MODULE_PARM_DESC(first_field_type, "first field type in simple work mode");

static int max_ignore_frames = 0;
module_param(max_ignore_frames, int, 0664);
MODULE_PARM_DESC(max_ignore_frames, "ignore first <n> frames");

static int ignore_frames = 0;
module_param(ignore_frames, int, 0664);
MODULE_PARM_DESC(ignore_frames, "ignore first <n> frames");

static int start_provider_delay = 0;
module_param(start_provider_delay, int, 0664);
MODULE_PARM_DESC(start_provider_delay, "ignore first <n> frames");
static bool vdin_dbg_en = 0;
module_param(vdin_dbg_en,bool,0664);
MODULE_PARM_DESC(vdin_dbg_en,"enable/disable vdin debug information");

static bool invert_top_bot = false;
module_param(invert_top_bot,bool,0644);
MODULE_PARM_DESC(invert_top_bot,"invert field type top or bottom");

/*
*the check flag in vdin_isr
*bit0:bypass stop check,bit1:bypass cyc check
*bit2:bypass vsync check,bit3:bypass vga check
*/
static unsigned int isr_flag = 0;
module_param(isr_flag,uint,0664);
MODULE_PARM_DESC(isr_flag,"flag which affect the skip field");

static unsigned int irq_cnt = 0;
module_param(irq_cnt,uint,0664);
MODULE_PARM_DESC(irq_cnt,"counter of irq");

static int irq_max_count = 0;
static void vdin_backup_histgram(struct vframe_s *vf, struct vdin_dev_s *devp);

static u32 vdin_get_curr_field_type(struct vdin_dev_s *devp)
{
	u32 field_status;
	u32 type = VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD ;
	//struct tvin_parm_s *parm = &devp->parm;
	const struct tvin_format_s *fmt_info = devp->fmt_info_p;

	if (fmt_info->scan_mode == TVIN_SCAN_MODE_PROGRESSIVE){
		type |= VIDTYPE_PROGRESSIVE;
	}
	else{
		field_status = vdin_get_field_type(devp->addr_offset);
                if(invert_top_bot)
                        type |= field_status ? VIDTYPE_INTERLACE_TOP : VIDTYPE_INTERLACE_BOTTOM;
                else
		        type |= field_status ? VIDTYPE_INTERLACE_BOTTOM : VIDTYPE_INTERLACE_TOP;
	}
	if ((devp->format_convert == VDIN_FORMAT_CONVERT_YUV_YUV444) ||
			(devp->format_convert == VDIN_FORMAT_CONVERT_RGB_YUV444))
		type |= VIDTYPE_VIU_444;
	else if ((devp->format_convert == VDIN_FORMAT_CONVERT_YUV_YUV422) ||
			(devp->format_convert == VDIN_FORMAT_CONVERT_RGB_YUV422))
		type |= VIDTYPE_VIU_422;
	else if(devp->prop.dest_cfmt == TVIN_NV21)
	{
		type |= VIDTYPE_VIU_NV21;
		type &= (~VIDTYPE_VIU_SINGLE_PLANE);
	}
        else if(devp->prop.dest_cfmt == TVIN_NV12)
        {
		//type |= VIDTYPE_VIU_NV12;
		type &= (~VIDTYPE_VIU_SINGLE_PLANE);

        }
	return type;
}

void set_invert_top_bot(bool invert_flag)
{
    invert_top_bot = invert_flag;
}


void vdin_timer_func(unsigned long arg)
{
	struct vdin_dev_s *devp = (struct vdin_dev_s *)arg;

	/* state machine routine */
	tvin_smr(devp);
	/* add timer */
	devp->timer.expires = jiffies + VDIN_PUT_INTERVAL;
	add_timer(&devp->timer);
}

static const struct vframe_operations_s vdin_vf_ops =
{
	.peek = vdin_vf_peek,
	.get  = vdin_vf_get,
	.put  = vdin_vf_put,
	.vf_states = vdin_vf_states,
};


/*
 * 1. find the corresponding frontend according to the port & save it.
 * 2. set default register, including:
 *		a. set default write canvas address.
 *		b. mux null input.
 *		c. set clock auto.
 *		a&b will enable hw work.
 * 3. call the callback function of the frontend to open.
 * 4. regiseter provider.
 * 5. create timer for state machine.
 *
 * port: the port suported by frontend
 * index: the index of frontend
 * 0 success, otherwise failed
 */
int vdin_open_fe(enum tvin_port_e port, int index,  struct vdin_dev_s *devp)
{
	struct tvin_frontend_s *fe = tvin_get_frontend(port, index);
	int ret = 0;
	if (!fe) {
		pr_err("%s(%d): not supported port 0x%x \n", __func__, devp->index, port);
		return -1;
	}

	devp->frontend = fe;
	devp->parm.port        = port;
	devp->parm.info.fmt    = TVIN_SIG_FMT_NULL;
	devp->parm.info.status = TVIN_SIG_STATUS_NULL;
	devp->dec_enable = 1;  //enable decoder

	vdin_set_default_regmap(devp->addr_offset);

	if (devp->frontend->dec_ops && devp->frontend->dec_ops->open)
		ret = devp->frontend->dec_ops->open(devp->frontend, port);
	/* check open status */
	if (ret)
	    return 1;

	/* init vdin state machine */
	tvin_smr_init(devp->index);
	init_timer(&devp->timer);
	devp->timer.data = (ulong) devp;
	devp->timer.function = vdin_timer_func;
	devp->timer.expires = jiffies + VDIN_PUT_INTERVAL;
	add_timer(&devp->timer);

	pr_info("%s port:0x%x ok\n", __func__, port);
	return 0;
}

/*
 *
 * 1. disable hw work, including:
 *		a. mux null input.
 *		b. set clock off.
 * 2. delete timer for state machine.
 * 3. unregiseter provider & notify receiver.
 * 4. call the callback function of the frontend to close.
 *
 */
void vdin_close_fe(struct vdin_dev_s *devp)
{
	/* avoid null pointer oops */
	if (!devp || !devp->frontend)
		return;
	devp->dec_enable = 0;  //disable decoder

	vdin_hw_disable(devp->addr_offset);
	del_timer_sync(&devp->timer);
	if (devp->frontend && devp->frontend->dec_ops->close) {
		devp->frontend->dec_ops->close(devp->frontend);
		devp->frontend = NULL;
	}
	devp->parm.port = TVIN_PORT_NULL;
	devp->parm.info.fmt  = TVIN_SIG_FMT_NULL;
	devp->parm.info.status = TVIN_SIG_STATUS_NULL;

	pr_info("%s ok\n", __func__);
}

static inline void vdin_set_source_type(struct vdin_dev_s *devp, struct vframe_s *vf)
{
	switch (devp->parm.port)
	{
		case TVIN_PORT_CVBS0:
			vf->source_type= VFRAME_SOURCE_TYPE_TUNER;
			break;
		case TVIN_PORT_CVBS1:
		case TVIN_PORT_CVBS2:
		case TVIN_PORT_CVBS3:
		case TVIN_PORT_CVBS4:
		case TVIN_PORT_CVBS5:
		case TVIN_PORT_CVBS6:
		case TVIN_PORT_CVBS7:
			vf->source_type = VFRAME_SOURCE_TYPE_CVBS;
			break;
		case TVIN_PORT_COMP0:
		case TVIN_PORT_COMP1:
		case TVIN_PORT_COMP2:
		case TVIN_PORT_COMP3:
		case TVIN_PORT_COMP4:
		case TVIN_PORT_COMP5:
		case TVIN_PORT_COMP6:
		case TVIN_PORT_COMP7:
			vf->source_type = VFRAME_SOURCE_TYPE_COMP;
			break;
		case TVIN_PORT_HDMI0:
		case TVIN_PORT_HDMI1:
		case TVIN_PORT_HDMI2:
		case TVIN_PORT_HDMI3:
		case TVIN_PORT_HDMI4:
		case TVIN_PORT_HDMI5:
		case TVIN_PORT_HDMI6:
		case TVIN_PORT_HDMI7:
			vf->source_type = VFRAME_SOURCE_TYPE_HDMI;
			break;
		default:
			vf->source_type = VFRAME_SOURCE_TYPE_OTHERS;
			break;
	}
}



static inline void vdin_set_source_mode(struct vdin_dev_s *devp, struct vframe_s *vf)
{
	switch (devp->parm.info.fmt)
	{
		case TVIN_SIG_FMT_CVBS_NTSC_M:
		case TVIN_SIG_FMT_CVBS_NTSC_443:
			vf->source_mode = VFRAME_SOURCE_MODE_NTSC;
			break;
		case TVIN_SIG_FMT_CVBS_PAL_I:
		case TVIN_SIG_FMT_CVBS_PAL_M:
		case TVIN_SIG_FMT_CVBS_PAL_60:
		case TVIN_SIG_FMT_CVBS_PAL_CN:
			vf->source_mode = VFRAME_SOURCE_MODE_PAL;
			break;
		case TVIN_SIG_FMT_CVBS_SECAM:
			vf->source_mode = VFRAME_SOURCE_MODE_SECAM;
			break;
		default:
			vf->source_mode = VFRAME_SOURCE_MODE_OTHERS;
			break;
	}
}

/*
 * 480p/i = 9:8
 * 576p/i = 16:15
 * 720p and 1080p/i = 1:1
 * All VGA format = 1:1
 */
static void vdin_set_pixel_aspect_ratio(struct vdin_dev_s *devp, struct vframe_s *vf)
{
	switch (devp->parm.info.fmt)
	{
		/* 480P */
		case TVIN_SIG_FMT_COMP_480P_60HZ_D000:
		case TVIN_SIG_FMT_HDMI_640X480P_60HZ:
		case TVIN_SIG_FMT_HDMI_720X480P_60HZ:
		case TVIN_SIG_FMT_HDMI_1440X480P_60HZ:
		case TVIN_SIG_FMT_HDMI_2880X480P_60HZ:
		case TVIN_SIG_FMT_HDMI_720X480P_120HZ:
		case TVIN_SIG_FMT_HDMI_720X480P_240HZ:
		case TVIN_SIG_FMT_HDMI_720X480P_60HZ_FRAME_PACKING:
		case TVIN_SIG_FMT_CAMERA_640X480P_30HZ:
			/* 480I */
		case TVIN_SIG_FMT_CVBS_NTSC_M:
		case TVIN_SIG_FMT_CVBS_NTSC_443:
		case TVIN_SIG_FMT_CVBS_PAL_M:
		case TVIN_SIG_FMT_CVBS_PAL_60:
		case TVIN_SIG_FMT_COMP_480I_59HZ_D940:
		case TVIN_SIG_FMT_HDMI_1440X480I_60HZ:
		case TVIN_SIG_FMT_HDMI_2880X480I_60HZ:
		case TVIN_SIG_FMT_HDMI_1440X480I_120HZ:
		case TVIN_SIG_FMT_HDMI_1440X480I_240HZ:
		case TVIN_SIG_FMT_BT656IN_480I_60HZ:
		case TVIN_SIG_FMT_BT601IN_480I_60HZ:
			vf->pixel_ratio = PIXEL_ASPECT_RATIO_8_9;
			break;
			/* 576P */
		case TVIN_SIG_FMT_COMP_576P_50HZ_D000:
		case TVIN_SIG_FMT_HDMI_720X576P_50HZ:
		case TVIN_SIG_FMT_HDMI_1440X576P_50HZ:
		case TVIN_SIG_FMT_HDMI_2880X576P_60HZ:
		case TVIN_SIG_FMT_HDMI_720X576P_100HZ:
		case TVIN_SIG_FMT_HDMI_720X576P_200HZ:
		case TVIN_SIG_FMT_HDMI_720X576P_50HZ_FRAME_PACKING:
			/* 576I */
		case TVIN_SIG_FMT_CVBS_PAL_I:
		case TVIN_SIG_FMT_CVBS_PAL_CN:
		case TVIN_SIG_FMT_CVBS_SECAM:
		case TVIN_SIG_FMT_COMP_576I_50HZ_D000:
		case TVIN_SIG_FMT_HDMI_1440X576I_50HZ:
		case TVIN_SIG_FMT_HDMI_2880X576I_50HZ:
		case TVIN_SIG_FMT_HDMI_1440X576I_100HZ:
		case TVIN_SIG_FMT_HDMI_1440X576I_200HZ:
		case TVIN_SIG_FMT_BT656IN_576I_50HZ:
		case TVIN_SIG_FMT_BT601IN_576I_50HZ:
			vf->pixel_ratio = PIXEL_ASPECT_RATIO_16_15;
			break;
		default:
			vf->pixel_ratio = PIXEL_ASPECT_RATIO_1_1;
			break;
	}
}
/*
 based on the bellow parameters:
 1.h_active
 2.v_active
*/
static void vdin_set_display_ratio(struct vframe_s *vf)
{
	unsigned int re = 0;
	if(vf->width == 0 || vf->height == 0)
		return;

	re = (vf->width * 36)/vf->height;
	if((36 < re && re <= 56)||(re == 90)||(re == 108)) {
		vf->ratio_control = 0xc0 << DISP_RATIO_ASPECT_RATIO_BIT;
	} else if(re > 56) {
		vf->ratio_control = 0x90 << DISP_RATIO_ASPECT_RATIO_BIT;
	} else {
		vf->ratio_control = 0x100 << DISP_RATIO_ASPECT_RATIO_BIT;
	}

}
/*
   based on the bellow parameters:
   1.h_active
   2.v_active
 */
static void vdin_vf_init(struct vdin_dev_s *devp)
{
	int i = 0;
        unsigned int chromaid, addr;
	struct vf_entry *master, *slave;
	struct vframe_s *vf;
	struct vf_pool *p = devp->vfp;
	//const struct tvin_format_s *fmt_info = tvin_get_fmt_info(fmt);
	if(vdin_dbg_en)
		pr_info("vdin.%d vframe initial infomation table: (%d of %d)\n",
	 		 devp->index, p->size, p->max_size);

	for (i = 0; i < p->size; ++i)
	{
		master = vf_get_master(p, i);
		master->flag |= VF_FLAG_NORMAL_FRAME;
		vf = &master->vf;
		memset(vf, 0, sizeof(struct vframe_s));
		vf->index = i;
		vf->width = devp->h_active;
		vf->height = devp->v_active;
		if ((devp->fmt_info_p->scan_mode == TVIN_SCAN_MODE_INTERLACED) &&
		    (!(devp->parm.flag & TVIN_PARM_FLAG_2D_TO_3D) &&
                      (devp->parm.info.fmt != TVIN_SIG_FMT_NULL))
		   )
			vf->height <<= 1;
#ifndef VDIN_DYNAMIC_DURATION
		vf->duration = devp->fmt_info_p->duration;
#endif
                /*if output fmt is nv21 or nv12 ,use the two continuous canvas for one field*/
                if((devp->prop.dest_cfmt == TVIN_NV12) ||
                   (devp->prop.dest_cfmt == TVIN_NV21))
                {
                        chromaid = (vdin_canvas_ids[devp->index][(vf->index<<1)+1])<<8;
                        addr = vdin_canvas_ids[devp->index][vf->index<<1] |chromaid;
                }
                else
                        addr = vdin_canvas_ids[devp->index][vf->index];

                vf->canvas0Addr = vf->canvas1Addr = addr;

		/* set source type & mode */
		vdin_set_source_type(devp, vf);
		vdin_set_source_mode(devp, vf);
		/* set source signal format */
		vf->sig_fmt = devp->parm.info.fmt;
		/* set pixel aspect ratio */
		vdin_set_pixel_aspect_ratio(devp, vf);
		/*set display ratio control */
		vdin_set_display_ratio(vf);
		/* init slave vframe */
		slave  = vf_get_slave(p, i);
		slave->flag = master->flag;
		memset(&slave->vf, 0, sizeof(struct vframe_s));
		slave->vf.index 	  = vf->index;
		slave->vf.width 	  = vf->width;
		slave->vf.height	  = vf->height;
		slave->vf.duration	  = vf->duration;
		slave->vf.ratio_control   = vf->ratio_control;
		slave->vf.canvas0Addr = vf->canvas0Addr;
		slave->vf.canvas1Addr = vf->canvas1Addr;
		/* set slave vf source type & mode */
		slave->vf.source_type = vf->source_type;
		slave->vf.source_mode = vf->source_mode;
		if(vdin_dbg_en)
			pr_info("\t%2d: 0x%2x %ux%u, duration = %u\n", vf->index,
				vf->canvas0Addr, vf->width, vf->height, vf->duration);
	}
}

/*
 * 1. config canvas for video frame.
 * 2. enable hw work, including:
 *		a. mux null input.
 *		b. set clock auto.
 * 3. set all registeres including:
 *		a. mux input.
 * 4. call the callback function of the frontend to start.
 * 5. enable irq .
 *
 */
void vdin_start_dec(struct vdin_dev_s *devp)
{
	struct tvin_state_machine_ops_s *sm_ops;
	/* avoid null pointer oops */
	if (!devp ||!devp->fmt_info_p){
                printk("[vdin..]%s null error.\n",__func__);
	        return;
	}
        if(devp->frontend && devp->frontend->sm_ops){
	        sm_ops = devp->frontend->sm_ops;
	        sm_ops->get_sig_propery(devp->frontend, &devp->prop);

		devp->parm.info.cfmt = devp->prop.color_format;
		if((devp->parm.dest_width!=0)||(devp->parm.dest_height!=0)){
			devp->prop.scaling4w = devp->parm.dest_width;
			devp->prop.scaling4h = devp->parm.dest_height;
		}
		if(devp->flags & VDIN_FLAG_MANUAL_CONVERTION){
			devp->prop.dest_cfmt = devp->debug.dest_cfmt;
			devp->prop.scaling4w = devp->debug.scaler4w;
			devp->prop.scaling4h = devp->debug.scaler4h;
		}
       }

	vdin_get_format_convert(devp);
	devp->curr_wr_vfe = NULL;
	/* h_active/v_active will be recalculated by bellow calling */
	vdin_set_decimation(devp);
	vdin_set_cutwin(devp);
	vdin_set_hvscale(devp);
#if defined(VDIN_V1)
    /*reverse / disable reverse write buffer*/
    vdin_wr_reverse(devp->addr_offset,devp->parm.h_reverse,devp->parm.v_reverse);
#endif

	/* h_active/v_active will be used by bellow calling */
	if (canvas_config_mode == 1) {
		vdin_canvas_start_config(devp);
	}
	else if (canvas_config_mode == 2){
		vdin_canvas_auto_config(devp);
	}
#if 0
	if((devp->prop.dest_cfmt == TVIN_NV12)||(devp->prop.dest_cfmt == TVIN_NV21)){
		devp->vfp->size = devp->canvas_max_num;
	} else {
		devp->vfp->size = devp->canvas_max_num;
	}
#endif

	devp->vfp->size = devp->canvas_max_num;
	vf_pool_init(devp->vfp, devp->vfp->size);
	vdin_vf_init(devp);

	devp->abnormal_cnt = 0;
	devp->last_wr_vfe = NULL;
	irq_max_count = 0;
	//devp->stamp_valid = false;
	devp->stamp = 0;
	devp->cycle = 0;
	devp->cycle_tag = 0;
	devp->hcnt64 = 0;
	devp->hcnt64_tag = 0;

	devp->vga_clr_cnt = devp->canvas_max_num;

	devp->vs_cnt_valid = 0;
	devp->vs_cnt_ignore = 0;

        devp->curr_field_type = vdin_get_curr_field_type(devp);
	//pr_info("start clean_counter is %d\n",clean_counter);
	/* configure regs and enable hw */
	#if ((defined CONFIG_ARCH_MESON8)||(defined CONFIG_ARCH_MESON8B))
	switch_vpu_mem_pd_vmod(devp->addr_offset?VPU_VIU_VDIN1:VPU_VIU_VDIN0,VPU_MEM_POWER_ON);
	#endif
	vdin_hw_enable(devp->addr_offset);
	vdin_set_all_regs(devp);

	if ((devp->parm.port >= TVIN_PORT_VGA0) && (devp->parm.port <= TVIN_PORT_VGA7))
		vdin_set_matrix_blank(devp);

	if (!(devp->parm.flag & TVIN_PARM_FLAG_CAP) &&
			devp->frontend->dec_ops && devp->frontend->dec_ops->start)
		devp->frontend->dec_ops->start(devp->frontend, devp->parm.info.fmt);

	/* register provider, so the receiver can get the valid vframe */
	udelay(start_provider_delay);
	vf_reg_provider(&devp->vprov);
	vf_notify_receiver(devp->name,VFRAME_EVENT_PROVIDER_START,NULL);
	#if defined(VDIN_V2)
	if(devp->parm.port != TVIN_PORT_VIU){
		/*enable irq */
		enable_irq(devp->irq);
		if(vdin_dbg_en)
			pr_info("****[%s]enable_irq ifdef VDIN_V2****\n",__func__);
	}
	#else
	enable_irq(devp->irq);
	if(vdin_dbg_en)
		pr_info("****[%s]enable_irq ifudef VDIN_V2****\n",__func__);
	#endif
	if(vdin_dbg_en)
		pr_info("****[%s]ok!****\n",__func__);
	#ifdef CONFIG_AM_TIMESYNC
	if(devp->parm.port != TVIN_PORT_VIU){
	/*disable audio&video sync used for libplayer*/
	tsync_set_enable(0);
	/* enable system_time */
	timestamp_pcrscr_enable(1);
	}
	#endif
}

/*
 * 1. disable irq.
 * 2. disable hw work, including:
 *		a. mux null input.
 *		b. set clock off.
 * 3. call the callback function of the frontend to stop.
 *
 */
void vdin_stop_dec(struct vdin_dev_s *devp)
{
	/* avoid null pointer oops */
	if (!devp || !devp->frontend)
		return;

	vf_unreg_provider(&devp->vprov);
	if (!(devp->parm.flag & TVIN_PARM_FLAG_CAP) &&
			devp->frontend->dec_ops && devp->frontend->dec_ops->stop)
		devp->frontend->dec_ops->stop(devp->frontend, devp->parm.port);
	vdin_set_default_regmap(devp->addr_offset);
	vdin_hw_disable(devp->addr_offset);
	disable_irq_nosync(devp->irq);
	/* reset default canvas  */
	vdin_set_def_wr_canvas(devp);
	#if ((defined CONFIG_ARCH_MESON8)||(defined CONFIG_ARCH_MESON8B))
	switch_vpu_mem_pd_vmod(devp->addr_offset?VPU_VIU_VDIN1:VPU_VIU_VDIN0,VPU_MEM_POWER_DOWN);
	#endif
	memset(&devp->prop, 0, sizeof(struct tvin_sig_property_s));
	ignore_frames = 0;
	devp->cycle = 0;
	if(vdin_dbg_en)
		pr_info("%s ok\n", __func__);
}
//@todo

int start_tvin_service(int no ,vdin_parm_t *para)
{
	struct tvin_frontend_s *fe;
	int ret = 0;
	struct vdin_dev_s *devp = vdin_devp[no];
	if(IS_ERR(devp)){
		pr_err(KERN_ERR "[vdin..]%s vdin%d has't registered,please register.\n",__func__,no);
		return -1;
	}
	if(vdin_dbg_en)
		pr_info("****[%s]para:cfmt:%d;dfmt:%d;dest_hactive:%d;dest_vactive:%d;frame_rate:%d;h_active:%d,v_active:%d;scan_mode:%d****\n",__func__,
	para->cfmt,para->dfmt,para->dest_hactive,para->dest_vactive,
	para->frame_rate,para->h_active,para->v_active,para->scan_mode);
	devp->start_time = jiffies_to_msecs(jiffies);
	if (devp->flags & VDIN_FLAG_DEC_STARTED) {
	        pr_err("%s: port 0x%x, decode started already.\n",__func__,para->port);
		ret = -EBUSY;
		return ret;
	}
#if defined(VDIN_V2)
	if(para->port != TVIN_PORT_VIU){
		ret = request_irq(devp->irq, vdin_v4l2_isr, IRQF_SHARED, devp->irq_name, (void *)devp);
		/*disable vsync irq until vdin configured completely*/
		disable_irq_nosync(devp->irq);
	}
#else
	ret = request_irq(devp->irq, vdin_v4l2_isr, IRQF_SHARED, devp->irq_name, (void *)devp);
	/*disable vsync irq until vdin configured completely*/
	disable_irq_nosync(devp->irq);
#endif
	/*config the vdin use default value*/
	vdin_set_default_regmap(devp->addr_offset);

	devp->parm.port = para->port;
	devp->parm.info.fmt     = para->fmt;
	//add for camera random resolution
	if(para->fmt >= TVIN_SIG_FMT_MAX){
                devp->fmt_info_p = kmalloc(sizeof(tvin_format_t),GFP_KERNEL);
                if(!devp->fmt_info_p){
                        pr_err("[vdin..]%s kmalloc error.\n",__func__);
                        return -ENOMEM;
                }
                devp->fmt_info_p->hs_bp     = para->hs_bp;
                devp->fmt_info_p->vs_bp     = para->vs_bp;
                devp->fmt_info_p->hs_pol    = para->hsync_phase;
                devp->fmt_info_p->vs_pol    = para->vsync_phase;
	        devp->fmt_info_p->h_active  = para->h_active;
	        devp->fmt_info_p->v_active  = para->v_active;
                devp->fmt_info_p->scan_mode = para->scan_mode;
                devp->fmt_info_p->duration  = 96000/para->frame_rate;
                devp->fmt_info_p->pixel_clk = para->h_active*para->v_active*para->frame_rate;
                devp->fmt_info_p->pixel_clk /=10000;
	}else{
                devp->fmt_info_p = (struct tvin_format_s*)tvin_get_fmt_info(devp->parm.info.fmt);
				//devp->fmt_info_p = tvin_get_fmt_info(devp->parm.info.fmt);
        }
        if(!devp->fmt_info_p) {
		pr_err("%s(%d): error, fmt is null!!!\n", __func__, no);
		return -1;
        }
        fe = tvin_get_frontend(para->port, 0);
        if(fe){
                fe->private_data = para;
                fe->port         = para->port;
	        devp->frontend   = fe;
                if(fe->dec_ops->open)
                        fe->dec_ops->open(fe,fe->port);
        }else{
		pr_err("%s(%d): not supported port 0x%x \n", __func__, no, para->port);
		return -1;
	}
        #ifdef CONFIG_ARCH_MESON6
        switch_mod_gate_by_name("vdin", 1);
        #endif
	vdin_start_dec(devp);
	devp->flags |= VDIN_FLAG_DEC_OPENED;
	devp->flags |= VDIN_FLAG_DEC_STARTED;
	irq_cnt = 0;
	return 0;
}

EXPORT_SYMBOL(start_tvin_service);
//@todo
int stop_tvin_service(int no)
{
	struct vdin_dev_s *devp;
	unsigned int end_time;
	devp = vdin_devp[no];

	if(!(devp->flags&VDIN_FLAG_DEC_STARTED)){
		pr_err("%s:decode hasn't started.\n",__func__);
		return -EBUSY;
	}
/* remove for m6&m8 camera function, may cause hardware disable bug on kernel 3.10 */
//#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
//	devp->flags |= VDIN_FLAG_DEC_STOP_ISR;
//#endif
	vdin_stop_dec(devp);
        /*close fe*/
        if(devp->frontend->dec_ops->close)
                devp->frontend->dec_ops->close(devp->frontend);
        /*free the memory allocated in start tvin service*/
	if(devp->parm.info.fmt >= TVIN_SIG_FMT_MAX)
                kfree(devp->fmt_info_p);
#ifdef CONFIG_ARCH_MESON6
        switch_mod_gate_by_name("vdin", 0);
#endif
	devp->flags &= (~VDIN_FLAG_DEC_OPENED);
	devp->flags &= (~VDIN_FLAG_DEC_STARTED);
#if defined(VDIN_V2)
	if(devp->parm.port!= TVIN_PORT_VIU){
		/* free irq */
		free_irq(devp->irq,(void *)devp);
	}
#else
	free_irq(devp->irq,(void *)devp);
#endif
	end_time = jiffies_to_msecs(jiffies);
	if(vdin_dbg_en)
		pr_info("[vdin]:vdin start time:%ums,stop time:%ums,run time:%u.\n",devp->start_time,end_time,end_time-devp->start_time);
	return 0;
}
EXPORT_SYMBOL(stop_tvin_service);

void get_tvin_canvas_info(int* start , int* num)
{
        *start = vdin_canvas_ids[0][0];
	*num = vdin_devp[0]->canvas_max_num;
}
EXPORT_SYMBOL(get_tvin_canvas_info);

static int vdin_ioctl_fe(int no, fe_arg_t *parm)
{
	struct vdin_dev_s *devp = vdin_devp[no];
	int ret = 0;
        if(IS_ERR(devp)){
                pr_err("[vdin..]%s vdin%d has't registered,please register.\n",__func__,no);
                return -1;
        }
	if(devp->frontend && devp->frontend->dec_ops && devp->frontend->dec_ops->ioctl)
		ret = devp->frontend->dec_ops->ioctl(devp->frontend, parm->arg);
	else if(!devp->frontend) {
		devp->frontend = tvin_get_frontend(parm->port,parm->index);
		if(devp->frontend && devp->frontend->dec_ops && devp->frontend->dec_ops->ioctl)
		        ret = devp->frontend->dec_ops->ioctl(devp->frontend, parm->arg);
	}
	return ret;
}
static void vdin_rdma_isr(struct vdin_dev_s *devp)
{
	if(devp->parm.port==TVIN_PORT_VIU)
		vdin_v4l2_isr(devp->irq,devp);
}
static int vdin_func(int no, vdin_arg_t *arg)
{
	struct vdin_dev_s *devp = vdin_devp[no];
	int ret = 0;
	struct vdin_arg_s *parm = NULL;
	struct vframe_s *vf = NULL;
	parm = arg;
        if(IS_ERR_OR_NULL(devp)){
		if(vdin_dbg_en)
		        pr_err("[vdin..]%s vdin%d has't registered,please register.\n",__func__,no);
                return -1;
        }else if(!(devp->flags&VDIN_FLAG_DEC_STARTED)&&(parm->cmd != VDIN_CMD_MPEGIN_START)){
        	if(vdin_dbg_en)
			;//pr_err("[vdin..]%s vdin%d has't started.\n",__func__,no);
		return -1;
	}
	if(vdin_dbg_en)
		pr_info("[vdin_drv]%s vdin%d:parm->cmd : %d \n",__func__,no,parm->cmd);
	switch(parm->cmd){
                /*ajust vdin1 matrix1 & matrix2 for isp to get histogram information*/
		case VDIN_CMD_SET_CSC:
			vdin_set_matrixs(devp,parm->matrix_id,parm->color_convert);
			break;
		case VDIN_CMD_SET_CM2:
#if defined(VDIN_V2)
			vdin_set_cm2(devp->addr_offset,devp->h_active,devp->v_active,parm->cm2);
#endif
			break;
		case VDIN_CMD_ISR:
			vdin_rdma_isr(devp);
			break;
		case VDIN_CMD_MPEGIN_START:
			devp->h_active = parm->h_active;
			devp->v_active = parm->v_active;
			vdin_set_mpegin(devp);
			if(!(devp->flags & VDIN_FLAG_DEC_STARTED)){
				devp->curr_wr_vfe = kmalloc(sizeof(vf_entry_t),GFP_KERNEL);
				devp->flags |= VDIN_FLAG_DEC_STARTED;
			}
			break;
		case VDIN_CMD_GET_HISTGRAM:
			if (!devp->curr_wr_vfe) {
				if(vdin_dbg_en)
					pr_info("[warning!!!]VDIN_CMD_GET_HISTGRAM:devp->curr_wr_vfe is NULL\n");
				break;
			}
			vdin_set_vframe_prop_info(&devp->curr_wr_vfe->vf, devp);
			vdin_backup_histgram(&devp->curr_wr_vfe->vf, devp);
			vf = parm->private;
			if(vf && devp->curr_wr_vfe)
				memcpy(&vf->prop,&devp->curr_wr_vfe->vf.prop,sizeof(vframe_prop_t));
			break;
		case VDIN_CMD_MPEGIN_STOP:
			if(devp->flags & VDIN_FLAG_DEC_STARTED){
				vdin_hw_disable(devp->addr_offset);
				kfree(devp->curr_wr_vfe);
				devp->curr_wr_vfe = NULL;
				devp->flags &= (~VDIN_FLAG_DEC_STARTED);
			}
			break;
		case VDIN_CMD_FORCE_GO_FIELD:
			vdin_force_gofiled(devp);
			break;
		default:
			break;
	}
	return ret;
}
static vdin_v4l2_ops_t vdin_4v4l2_ops = {
        .start_tvin_service   = start_tvin_service,
        .stop_tvin_service    = stop_tvin_service,
        .get_tvin_canvas_info = get_tvin_canvas_info,
        .set_tvin_canvas_info = NULL,
        .tvin_fe_func         = vdin_ioctl_fe,
        .tvin_vdin_func	      = vdin_func,
};

static void vdin_pause_dec(struct vdin_dev_s *devp)
{
	vdin_hw_disable(devp->addr_offset);
}

static void vdin_resume_dec(struct vdin_dev_s *devp)
{
	vdin_hw_enable(devp->addr_offset);
}

static void vdin_vf_reg(struct vdin_dev_s *devp)
{
	vf_reg_provider(&devp->vprov);
	vf_notify_receiver(devp->name,VFRAME_EVENT_PROVIDER_START,NULL);
}

static void vdin_vf_unreg(struct vdin_dev_s *devp)
{
	vf_unreg_provider(&devp->vprov);
}

#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
static inline void vdin_set_view(struct vdin_dev_s *devp, struct vframe_s *vf)
{
	struct vframe_view_s *left_eye, *right_eye;
	enum tvin_sig_fmt_e fmt = devp->parm.info.fmt;
	const struct tvin_format_s *fmt_info = tvin_get_fmt_info(fmt);

        if(!fmt_info) {
                pr_err("[tvafe..] %s: error,fmt is null!!! \n",__func__);
                return;
        }

	left_eye  = &vf->left_eye;
	right_eye = &vf->right_eye;

	switch (devp->parm.info.trans_fmt)
	{
		case TVIN_TFMT_3D_LRH_OLOR:
		case TVIN_TFMT_3D_LRH_OLER:
			left_eye->start_x	 = 0;
			left_eye->start_y	 = 0;
			left_eye->width 	 = devp->h_active >> 1;
			left_eye->height	 = devp->v_active;
			right_eye->start_x	 = devp->h_active >> 1;
			right_eye->start_y	 = 0;
			right_eye->width	 = devp->h_active >> 1;
			right_eye->height	 = devp->v_active;
			break;
		case TVIN_TFMT_3D_TB:
			left_eye->start_x	 = 0;
			left_eye->start_y	 = 0;
			left_eye->width 	 = devp->h_active;
			left_eye->height	 = devp->v_active >> 1;
			right_eye->start_x	 = 0;
			right_eye->start_y	 = devp->v_active >> 1;
			right_eye->width	 = devp->h_active;
			right_eye->height	 = devp->v_active >> 1;
			break;
		case TVIN_TFMT_3D_FP: {
			unsigned int vactive = 0;
			unsigned int vspace = 0;
			struct vf_entry *slave = NULL;
			vspace  = fmt_info->vs_front + fmt_info->vs_width + fmt_info->vs_bp;
			if ((devp->parm.info.fmt == TVIN_SIG_FMT_HDMI_1920X1080I_50HZ_FRAME_PACKING) ||(devp->parm.info.fmt == TVIN_SIG_FMT_HDMI_1920X1080I_60HZ_FRAME_PACKING))
			{
				vactive = (fmt_info->v_active - vspace - vspace - vspace + 1) >> 2;
				slave = vf_get_slave(devp->vfp, vf->index);
				slave->vf.left_eye.start_x  = 0;
				slave->vf.left_eye.start_y  = vactive + vspace + vactive + vspace - 1;
				slave->vf.left_eye.width    = devp->h_active;
				slave->vf.left_eye.height   = vactive;
				slave->vf.right_eye.start_x = 0;
				slave->vf.right_eye.start_y = vactive + vspace + vactive + vspace + vactive + vspace - 1;
				slave->vf.right_eye.width   = devp->h_active;
				slave->vf.right_eye.height  = vactive;
			}
			else
				vactive = (fmt_info->v_active - vspace) >> 1;
				left_eye->start_x	 = 0;
				left_eye->start_y	 = 0;
				left_eye->width 	 = devp->h_active;
				left_eye->height	 = vactive;
				right_eye->start_x	 = 0;
				right_eye->start_y	 = vactive + vspace;
				right_eye->width	 = devp->h_active;
				right_eye->height	 = vactive;
				break;
			}
                case TVIN_TFMT_3D_FA: {
                        unsigned int vactive = 0;
                        unsigned int vspace = 0;

                        vspace  = fmt_info->vs_front + fmt_info->vs_width + fmt_info->vs_bp;

                        vactive = (fmt_info->v_active - vspace + 1) >> 1;

                        left_eye->start_x    = 0;
                        left_eye->start_y    = 0;
                        left_eye->width      = devp->h_active;
                        left_eye->height     = vactive;
                        right_eye->start_x   = 0;
                        right_eye->start_y   = vactive + vspace;
                        right_eye->width     = devp->h_active;
                        right_eye->height    = vactive;
                        break;
                }
                case TVIN_TFMT_3D_LA: {
                        left_eye->start_x    = 0;
                        left_eye->start_y    = 0;
                        left_eye->width      = devp->h_active;
                        left_eye->height     = (devp->v_active) >> 1;
                        right_eye->start_x   = 0;
                        right_eye->start_y   = 0;
                        right_eye->width     = devp->h_active;
                        right_eye->height    = (devp->v_active) >> 1;
                        break;
                }
		default:
			left_eye->start_x	 = 0;
			left_eye->start_y	 = 0;
			left_eye->width 	 = 0;
			left_eye->height	 = 0;
			right_eye->start_x	 = 0;
			right_eye->start_y	 = 0;
			right_eye->width	 = 0;
			right_eye->height	 = 0;
			break;
	}
}
#endif
irqreturn_t vdin_isr_simple(int irq, void *dev_id)
{
	struct vdin_dev_s *devp = (struct vdin_dev_s *)dev_id;
	struct tvin_decoder_ops_s *decops;
	unsigned int last_field_type;

	if (irq_max_count >= devp->canvas_max_num) {
		vdin_hw_disable(devp->addr_offset);
		return IRQ_HANDLED;
	}

	last_field_type = devp->curr_field_type;
	devp->curr_field_type = vdin_get_curr_field_type(devp);

	decops =devp->frontend->dec_ops;
	if (decops->decode_isr(devp->frontend, vdin_get_meas_hcnt64(devp->addr_offset)) == -1)
		return IRQ_HANDLED;
	/* set canvas address */

	vdin_set_canvas_id(devp->addr_offset, vdin_canvas_ids[devp->index][irq_max_count]);
	if(vdin_dbg_en)
		pr_info("%2d: canvas id %d, field type %s\n", irq_max_count,
			vdin_canvas_ids[devp->index][irq_max_count],
			((last_field_type & VIDTYPE_TYPEMASK)== VIDTYPE_INTERLACE_TOP ? "top":"buttom"));

	irq_max_count++;
	return IRQ_HANDLED;
}

static void vdin_backup_histgram(struct vframe_s *vf, struct vdin_dev_s *devp)
{
	unsigned int i = 0;
        devp->parm.hist_pow = vf->prop.hist.hist_pow;
	devp->parm.luma_sum = vf->prop.hist.luma_sum;
	devp->parm.pixel_sum = vf->prop.hist.pixel_sum;
	for (i = 0; i < 64; i++){
		devp->parm.histgram[i] = vf->prop.hist.gamma[i];
	}
}
/*as use the spin_lock,
 *1--there is no sleep,
 *2--it is better to shorter the time,
 *3--it is better to shorter the time,
 */
#define VDIN_MEAS_24M_1MS 24000

irqreturn_t vdin_isr(int irq, void *dev_id)
{
	ulong flags;
	struct vdin_dev_s *devp = (struct vdin_dev_s *)dev_id;
	enum tvin_sm_status_e state;

	struct vf_entry *next_wr_vfe = NULL;
	struct vf_entry *curr_wr_vfe = NULL;
	struct vframe_s *curr_wr_vf = NULL;
	unsigned int last_field_type;
	signed short step = 0;
	struct tvin_decoder_ops_s *decops;
	unsigned int stamp = 0;
	struct tvin_state_machine_ops_s *sm_ops;
	//unsigned long long total_time;
	//unsigned long long total_tmp;
	struct tvafe_vga_parm_s vga_parm = {0};
	bool is_vga = false;

	if (!devp) return IRQ_HANDLED;

        isr_log(devp->vfp);
	/* debug interrupt interval time
	 *
	 * this code about system time must be outside of spinlock.
	 * because the spinlock may affect the system time.
	 */

	spin_lock_irqsave(&devp->isr_lock, flags);

	/* avoid null pointer oops */
	if (!devp || !devp->frontend) {
		goto irq_handled;
	}
	//vdin_write_done_check(devp->addr_offset, devp);

	if ((devp->flags & VDIN_FLAG_DEC_STOP_ISR)&&
                        (!(isr_flag & VDIN_BYPASS_STOP_CHECK)))
	{
		vdin_hw_disable(devp->addr_offset);
		devp->flags &= ~VDIN_FLAG_DEC_STOP_ISR;
		goto irq_handled;
	}
	stamp  = vdin_get_meas_vstamp(devp->addr_offset);
	if (!devp->curr_wr_vfe) {
		devp->curr_wr_vfe = provider_vf_get(devp->vfp);
		/*save the first field stamp*/
		devp->stamp = stamp;
		goto irq_handled;
	}
	/*check vs is valid base on the time during continuous vs*/
	if(vdin_check_cycle(devp) && (!(isr_flag & VDIN_BYPASS_CYC_CHECK)))
		goto irq_handled;

	devp->hcnt64 = vdin_get_meas_hcnt64(devp->addr_offset);

	/* ignore invalid vs base on the continuous fields different cnt to void screen flicker */
	if (vdin_check_vs(devp)&&(!(isr_flag & VDIN_BYPASS_VSYNC_CHECK)))
	{
		goto irq_handled;
	}
	sm_ops = devp->frontend->sm_ops;

	if ((devp->parm.port >= TVIN_PORT_VGA0) &&
			(devp->parm.port <= TVIN_PORT_VGA7) &&
			(devp->dec_enable))
	{
                if(!(isr_flag && VDIN_BYPASS_VGA_CHECK))
		        is_vga = true;
	}
	else{
		is_vga = false;
	}
	last_field_type = devp->curr_field_type;
	devp->curr_field_type = vdin_get_curr_field_type(devp);

	/* ignore the unstable signal */
	state = tvin_get_sm_status(devp->index);
	if (devp->parm.info.status != TVIN_SIG_STATUS_STABLE ||
			state != TVIN_SM_STATUS_STABLE) {
		goto irq_handled;
	}

	/* for 3D mode & interlaced format, give up bottom field to avoid odd/even phase different*/
	if (((devp->parm.flag & TVIN_PARM_FLAG_2D_TO_3D) ||
	    (devp->parm.info.trans_fmt&&(devp->parm.info.trans_fmt!=TVIN_TFMT_3D_FP))) &&
	    ((last_field_type & VIDTYPE_INTERLACE_BOTTOM) == VIDTYPE_INTERLACE_BOTTOM)
	   )
	{
		goto irq_handled;
	}
	curr_wr_vfe = devp->curr_wr_vfe;
	curr_wr_vf  = &curr_wr_vfe->vf;

	decops = devp->frontend->dec_ops;
	if (decops->decode_isr(devp->frontend, devp->hcnt64) == TVIN_BUF_SKIP) {
		goto irq_handled;
	}
	if(devp->parm.port >= TVIN_PORT_CVBS0 && devp->parm.port <= TVIN_PORT_CVBS7)
		curr_wr_vf->phase = sm_ops->get_secam_phase(devp->frontend)? VFRAME_PHASE_DB : VFRAME_PHASE_DR;
	if (is_vga)
		sm_ops->vga_get_param(&vga_parm, devp->frontend);
	/* 4.csc=default wr=zoomed */
	if ((curr_wr_vf->vga_parm.vga_in_clean == 1) && is_vga)
	{
		curr_wr_vf->vga_parm.vga_in_clean--;
		vdin_set_matrix(devp);
		//sm_ops->vga_set_param(&curr_wr_vf->vga_parm, devp->frontend);
		sm_ops->vga_set_param((struct tvafe_vga_parm_s*)VDIN_FLAG_BLACK_SCREEN_OFF, devp->frontend);
		step = curr_wr_vf->vga_parm.vpos_step;
		if (step > 0)
			vdin_delay_line(1, devp->addr_offset);
		else
			vdin_delay_line(0, devp->addr_offset);

		goto irq_handled;
	}
	/* 3.csc=blank wr=zoomed */
	if ((curr_wr_vf->vga_parm.vga_in_clean == 2) && is_vga)
	{
		curr_wr_vf->vga_parm.vga_in_clean--;
		curr_wr_vf->vga_parm.clk_step = vga_parm.clk_step;
		curr_wr_vf->vga_parm.phase = vga_parm.phase;
		curr_wr_vf->vga_parm.hpos_step = vga_parm.hpos_step;
		curr_wr_vf->vga_parm.vpos_step = vga_parm.vpos_step;
		set_wr_ctrl(curr_wr_vf->vga_parm.hpos_step, curr_wr_vf->vga_parm.vpos_step,devp);

		goto irq_handled;
	}
	/* 2.csc=blank wr=default */
	if ((curr_wr_vf->vga_parm.vga_in_clean == 3) && is_vga)
	{
		curr_wr_vf->vga_parm.vga_in_clean--;

		goto irq_handled;
	}


	if (ignore_frames < max_ignore_frames ) {
		ignore_frames++;

		goto irq_handled;
	}

	if (sm_ops->check_frame_skip && sm_ops->check_frame_skip(devp->frontend)) {
		goto irq_handled;
	}

	next_wr_vfe = provider_vf_peek(devp->vfp);
	if (!next_wr_vfe) {
		goto irq_handled;
	}
	/*if vdin-nr,di must get vdin current field type which di pre will read*/
        if(vf_notify_receiver(devp->name,VFRAME_EVENT_PROVIDER_QUREY_VDIN2NR,NULL))
		curr_wr_vf->type = devp->curr_field_type;
	else
		curr_wr_vf->type = last_field_type;

	/* for 2D->3D mode or hdmi 3d mode& interlaced format, fill-in as progressive format */
	if (((devp->parm.flag & TVIN_PARM_FLAG_2D_TO_3D)||(curr_wr_vf->trans_fmt)) &&
	    (last_field_type & VIDTYPE_INTERLACE)
	   )
	{
		curr_wr_vf->type &= ~VIDTYPE_INTERLACE_TOP;
		curr_wr_vf->type |=  VIDTYPE_PROGRESSIVE;
		curr_wr_vf->type |=  VIDTYPE_PRE_INTERLACE;
	}

	vdin_set_vframe_prop_info(curr_wr_vf, devp);
	vdin_backup_histgram(curr_wr_vf, devp);

#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
	curr_wr_vf->trans_fmt = devp->parm.info.trans_fmt;

	vdin_set_view(devp, curr_wr_vf);
#endif
	vdin_calculate_duration(devp);
	/* put for receiver

	   ppmgr had handled master and slave vf by itself,vdin do not to declare them respectively
	   ppmgr put the vf that included master vf and slave vf
	 */
        /*config height according to 3d mode dynamically*/
	if ((devp->fmt_info_p->scan_mode == TVIN_SCAN_MODE_INTERLACED) &&
	    (!(devp->parm.flag & TVIN_PARM_FLAG_2D_TO_3D) &&
            (devp->parm.info.fmt != TVIN_SIG_FMT_NULL))
	   )
		curr_wr_vf->height = devp->v_active<<1;
	else
		curr_wr_vf->height = devp->v_active;
	curr_wr_vfe->flag |= VF_FLAG_NORMAL_FRAME;
	provider_vf_put(curr_wr_vfe, devp->vfp);

	/* prepare for next input data */
	next_wr_vfe = provider_vf_get(devp->vfp);
	vdin_set_canvas_id(devp->addr_offset, (next_wr_vfe->vf.canvas0Addr&0xff));
        /* prepare for chroma canvas*/
        if((devp->prop.dest_cfmt == TVIN_NV12)||(devp->prop.dest_cfmt == TVIN_NV21))
                vdin_set_chma_canvas_id(devp->addr_offset,(next_wr_vfe->vf.canvas0Addr>>8)&0xff);

        devp->curr_wr_vfe = next_wr_vfe;
	vf_notify_receiver(devp->name,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);

	/* 1.csc=blank wr=default*/
	if (is_vga &&
			((vga_parm.hpos_step != next_wr_vfe->vf.vga_parm.hpos_step) ||
			 (vga_parm.vpos_step != next_wr_vfe->vf.vga_parm.vpos_step) ||
			 (vga_parm.clk_step  != next_wr_vfe->vf.vga_parm.clk_step ) ||
			 (vga_parm.phase	 != next_wr_vfe->vf.vga_parm.phase) ||
			 (devp->vga_clr_cnt > 0 				  )
			)
	   )
	{
		set_wr_ctrl(0, 0, devp);
		vdin_set_matrix_blank(devp);
		vga_parm.hpos_step = 0;
		vga_parm.vpos_step = 0;
		vga_parm.phase	   = next_wr_vfe->vf.vga_parm.phase;
		//sm_ops->vga_set_param(&vga_parm, devp->frontend);
		sm_ops->vga_set_param((struct tvafe_vga_parm_s*)VDIN_FLAG_BLACK_SCREEN_ON, devp->frontend);

		if (next_wr_vfe->vf.vga_parm.vga_in_clean == 0)
			next_wr_vfe->vf.vga_parm.vga_in_clean = 3;
		else
			next_wr_vfe->vf.vga_parm.vga_in_clean--;
		if (devp->vga_clr_cnt > 0)
			devp->vga_clr_cnt--;
	}
irq_handled:
	spin_unlock_irqrestore(&devp->isr_lock, flags);

        isr_log(devp->vfp);
	return IRQ_HANDLED;
}
/*
* there are too much logic in vdin_isr which is useless in camera&viu
*so vdin_v4l2_isr use to the sample v4l2 application such as camera,viu
*/
static unsigned short skip_ratio = 1;
module_param(skip_ratio,ushort,0664);
MODULE_PARM_DESC(skip_ratio,"\n vdin skip frame ratio 1/ratio will reserved.\n");

irqreturn_t vdin_v4l2_isr(int irq, void *dev_id)
{
	ulong flags;
	struct vdin_dev_s *devp = (struct vdin_dev_s *)dev_id;

	struct vf_entry *next_wr_vfe = NULL,*curr_wr_vfe = NULL;
	struct vframe_s *curr_wr_vf = NULL;
	unsigned int last_field_type, stamp;
	struct tvin_decoder_ops_s *decops;
        struct tvin_state_machine_ops_s *sm_ops;
	int ret = 0;
	if (!devp)
                return IRQ_HANDLED;
	isr_log(devp->vfp);
        irq_cnt++;
	spin_lock_irqsave(&devp->isr_lock, flags);
        if(devp)
	        /* avoid null pointer oops */
	        stamp  = vdin_get_meas_vstamp(devp->addr_offset);
	if (!devp->curr_wr_vfe) {
		devp->curr_wr_vfe = provider_vf_get(devp->vfp);
		/*save the first field stamp*/
		devp->stamp = stamp;
		goto irq_handled;
	}
	if(devp->parm.port == TVIN_PORT_VIU){
	        if(!vdin_write_done_check(devp->addr_offset, devp)){
			if(vdin_dbg_en)
				pr_info("[vdin.%u] write undone skiped.\n",devp->index);
	                goto irq_handled;
	        }
	}


	if(devp->last_wr_vfe){
		struct timeval ts;
		do_gettimeofday(&ts);
		devp->last_wr_vfe->vf.pts_us64 = ts.tv_sec;
		devp->last_wr_vfe->vf.pts = ts.tv_usec;
		provider_vf_put(devp->last_wr_vfe, devp->vfp);
		devp->last_wr_vfe = NULL;
		vf_notify_receiver(devp->name,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
	}
	/*check vs is valid base on the time during continuous vs*/
	vdin_check_cycle(devp);

/* remove for m6&m8 camera function, may cause hardware disable bug on kernel 3.10 */
//#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
//	if (devp->flags & VDIN_FLAG_DEC_STOP_ISR){
//		vdin_hw_disable(devp->addr_offset);
//		devp->flags &= ~VDIN_FLAG_DEC_STOP_ISR;
//		goto irq_handled;
//	}
//#endif
	/* ignore invalid vs base on the continuous fields different cnt to void screen flicker */

	last_field_type = devp->curr_field_type;
	devp->curr_field_type = vdin_get_curr_field_type(devp);

	curr_wr_vfe = devp->curr_wr_vfe;
	curr_wr_vf  = &curr_wr_vfe->vf;

	curr_wr_vf->type = last_field_type;

	vdin_set_vframe_prop_info(curr_wr_vf, devp);
	vdin_backup_histgram(curr_wr_vf, devp);

	if(devp->frontend && devp->frontend->dec_ops){
		decops = devp->frontend->dec_ops;
                /*pass the histogram information to viuin frontend*/
		devp->frontend->private_data = &curr_wr_vf->prop;
          	ret = decops->decode_isr(devp->frontend, devp->hcnt64);
		if (ret == TVIN_BUF_SKIP) {
			if(vdin_dbg_en)
				pr_info("%s bufffer(%u) skiped.\n",__func__,curr_wr_vf->index);
			goto irq_handled;
		/*if the current buffer is reserved,recycle tmp list,put current buffer to tmp list*/
		} else if(ret == TVIN_BUF_TMP) {
			recycle_tmp_vfs(devp->vfp);
			tmp_vf_put(curr_wr_vfe,devp->vfp);
			curr_wr_vfe = NULL;
                /*function of capture end,the reserved is the best*/
		} else if(ret == TVIN_BUF_RECYCLE_TMP) {
			tmp_to_rd(devp->vfp);
			goto irq_handled;
		}
	}
	/*check the skip frame*/
	if(devp->frontend && devp->frontend->sm_ops){
		sm_ops = devp->frontend->sm_ops;
		if(sm_ops->check_frame_skip &&
			sm_ops->check_frame_skip(devp->frontend)) {
			goto irq_handled;
		}
	}
	next_wr_vfe = provider_vf_peek(devp->vfp);

	if (!next_wr_vfe) {
                /*add for force vdin buffer recycle*/
                if(devp->flags & VDIN_FLAG_FORCE_RECYCLE) {
                        next_wr_vfe = receiver_vf_get(devp->vfp);
                        if(next_wr_vfe)
                                receiver_vf_put(&next_wr_vfe->vf,devp->vfp);
                        else
                                pr_err("[vdin.%d] force recycle error, no buffer in ready list.",devp->index);
                }
                else {
		        goto irq_handled;
                }
	}
	if(curr_wr_vfe){
		curr_wr_vfe->flag |= VF_FLAG_NORMAL_FRAME;
		//provider_vf_put(curr_wr_vfe, devp->vfp);
		devp->last_wr_vfe = curr_wr_vfe;
	}

	/* prepare for next input data */
	next_wr_vfe = provider_vf_get(devp->vfp);
	if(devp->parm.port == TVIN_PORT_VIU){
		VSYNC_WR_MPEG_REG_BITS(VDIN_WR_CTRL+devp->addr_offset, (next_wr_vfe->vf.canvas0Addr&0xff), WR_CANVAS_BIT, WR_CANVAS_WID);
       /* prepare for chroma canvas*/
		if((devp->prop.dest_cfmt == TVIN_NV12)||(devp->prop.dest_cfmt == TVIN_NV21))
			VSYNC_WR_MPEG_REG_BITS(VDIN_WR_CTRL2+devp->addr_offset, (next_wr_vfe->vf.canvas0Addr>>8)&0xff, WRITE_CHROMA_CANVAS_ADDR_BIT,WRITE_CHROMA_CANVAS_ADDR_WID);
	}else{
		vdin_set_canvas_id(devp->addr_offset, (next_wr_vfe->vf.canvas0Addr&0xff));
		if((devp->prop.dest_cfmt == TVIN_NV12)||(devp->prop.dest_cfmt == TVIN_NV21))
			vdin_set_chma_canvas_id(devp->addr_offset, (next_wr_vfe->vf.canvas0Addr>>8)&0xff);
	}
	devp->curr_wr_vfe = next_wr_vfe;
	vf_notify_receiver(devp->name,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);


irq_handled:
	spin_unlock_irqrestore(&devp->isr_lock, flags);
	isr_log(devp->vfp);
	return IRQ_HANDLED;
}


static int vdin_open(struct inode *inode, struct file *file)
{
	vdin_dev_t *devp;
        int ret = 0;
	/* Get the per-device structure that contains this cdev */
	devp = container_of(inode->i_cdev, vdin_dev_t, cdev);
	file->private_data = devp;

	if (devp->index >= VDIN_MAX_DEVS)
		return -ENXIO;

	if (devp->flags &= VDIN_FLAG_FS_OPENED) {
		if(vdin_dbg_en)
			pr_info("%s, device %s opened already\n", __func__, dev_name(devp->dev));
		return 0;
	}

	devp->flags |= VDIN_FLAG_FS_OPENED;

	/* request irq */
	if (work_mode_simple) {
		if(vdin_dbg_en)
			pr_info("vdin.%d work in simple mode.\n",devp->index);
		ret = request_irq(devp->irq, vdin_isr_simple, IRQF_SHARED, devp->irq_name, (void *)devp);
	}
	else {
		if(vdin_dbg_en)
			pr_info("vdin.%d work in normal mode.\n",devp->index);
		ret = request_irq(devp->irq, vdin_isr, IRQF_SHARED, devp->irq_name, (void *)devp);
	}
        /*disable irq untill vdin is configured completely*/
        disable_irq_nosync(devp->irq);

	/* remove the hardware limit to vertical [0-max]*/
        WRITE_VCBUS_REG(VPP_PREBLEND_VD1_V_START_END, 0x00000fff);
	if(vdin_dbg_en)
		pr_info("open device %s ok\n", dev_name(devp->dev));
	return ret;
}

static int vdin_release(struct inode *inode, struct file *file)
{
	vdin_dev_t *devp = file->private_data;

	if (!(devp->flags &= VDIN_FLAG_FS_OPENED)) {
		if(vdin_dbg_en)
			pr_info("%s, device %s not opened\n", __func__, dev_name(devp->dev));
		return 0;
	}

	devp->flags &= (~VDIN_FLAG_FS_OPENED);

	/* free irq */
	free_irq(devp->irq,(void *)devp);

	file->private_data = NULL;

	/* reset the hardware limit to vertical [0-1079]  */
        WRITE_VCBUS_REG(VPP_PREBLEND_VD1_V_START_END, 0x00000437);
	if(vdin_dbg_en)
		pr_info("close device %s ok\n", dev_name(devp->dev));
	return 0;
}

static long vdin_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	unsigned int delay_cnt = 0;
	vdin_dev_t *devp = NULL;
	void __user *argp = (void __user *)arg;

	if (_IOC_TYPE(cmd) != TVIN_IOC_MAGIC) {
		pr_err("%s invalid command: %u\n", __func__, cmd);
		return -ENOSYS;
	}


	/* Get the per-device structure that contains this cdev */
	devp = file->private_data;

	switch (cmd)
	{
		case TVIN_IOC_OPEN: {
			struct tvin_parm_s parm = {0};
			mutex_lock(&devp->fe_lock);
			if (copy_from_user(&parm, argp, sizeof(struct tvin_parm_s)))
			{
				pr_err("TVIN_IOC_OPEN(%d) invalid parameter\n", devp->index);
				ret = -EFAULT;
				mutex_unlock(&devp->fe_lock);
				break;
			}
			if (devp->flags & VDIN_FLAG_DEC_OPENED) {
				pr_err("TVIN_IOC_OPEN(%d) port %s opend already\n",
						parm.index, tvin_port_str(parm.port));
				ret = -EBUSY;
				mutex_unlock(&devp->fe_lock);
				break;
			}
			devp->parm.index = parm.index;
			devp->parm.port  = parm.port;
			devp->unstable_flag = false;
			ret = vdin_open_fe(devp->parm.port, devp->parm.index, devp);
			if (ret) {
				pr_err("TVIN_IOC_OPEN(%d) failed to open port 0x%x\n",
						devp->index, parm.port);
				ret = -EFAULT;
				mutex_unlock(&devp->fe_lock);
				break;
			}

			devp->flags |= VDIN_FLAG_DEC_OPENED;
			if(vdin_dbg_en)
				pr_info("TVIN_IOC_OPEN(%d) port %s opened ok\n\n", parm.index,
					tvin_port_str(devp->parm.port));
			mutex_unlock(&devp->fe_lock);
			break;
		}
		case TVIN_IOC_START_DEC: {
			struct tvin_parm_s parm = {0};
			mutex_lock(&devp->fe_lock);
			if (devp->flags & VDIN_FLAG_DEC_STARTED) {
				pr_err("TVIN_IOC_START_DEC(%d) port 0x%x, decode started already\n", parm.index, parm.port);
				ret = -EBUSY;
				mutex_unlock(&devp->fe_lock);
				break;
			}
			if ((devp->parm.info.status != TVIN_SIG_STATUS_STABLE) ||
					(devp->parm.info.fmt == TVIN_SIG_FMT_NULL))
			{
				pr_err("TVIN_IOC_START_DEC: port %s start invalid\n",
						tvin_port_str(devp->parm.port));
				pr_err("	status: %s, fmt: %s\n",
						tvin_sig_status_str(devp->parm.info.status),
						tvin_sig_fmt_str(devp->parm.info.fmt));
				ret = -EPERM;
					mutex_unlock(&devp->fe_lock);
					break;
			}

			if (copy_from_user(&parm, argp, sizeof(struct tvin_parm_s)))
			{
				pr_err("TVIN_IOC_START_DEC(%d) invalid parameter\n", devp->index);
				ret = -EFAULT;
				mutex_unlock(&devp->fe_lock);
				break;
			}
                        devp->parm.info.fmt = parm.info.fmt;
                        devp->fmt_info_p  = (struct tvin_format_s*)tvin_get_fmt_info(devp->parm.info.fmt);
						//devp->fmt_info_p  = tvin_get_fmt_info(devp->parm.info.fmt);
                        if(!devp->fmt_info_p) {
				pr_err("TVIN_IOC_START_DEC(%d) error, fmt is null \n", devp->index);
				ret = -EFAULT;
				mutex_unlock(&devp->fe_lock);
				break;
                        }
			vdin_start_dec(devp);
			devp->flags |= VDIN_FLAG_DEC_STARTED;
			if(vdin_dbg_en)
				pr_info("TVIN_IOC_START_DEC port %s, decode started ok\n\n",
					tvin_port_str(devp->parm.port));
			mutex_unlock(&devp->fe_lock);
			break;
		}
		case TVIN_IOC_STOP_DEC:	{
			struct tvin_parm_s *parm = &devp->parm;
			mutex_lock(&devp->fe_lock);
			if(!(devp->flags & VDIN_FLAG_DEC_STARTED)) {
				pr_err("TVIN_IOC_STOP_DEC(%d) decode havn't started\n", devp->index);
				ret = -EPERM;
				mutex_unlock(&devp->fe_lock);
				break;
			}
			devp->flags |= VDIN_FLAG_DEC_STOP_ISR;
			delay_cnt = 7;
			while ((devp->flags & VDIN_FLAG_DEC_STOP_ISR) && delay_cnt)
			{
				mdelay(10);
				delay_cnt--;
			}
			vdin_stop_dec(devp);
                        /*
			if (devp->flags & VDIN_FLAG_FORCE_UNSTABLE)
			{
				set_foreign_affairs(FOREIGN_AFFAIRS_00);
				pr_info("video reset vpp.\n");
			}
			delay_cnt = 7;
			while (get_foreign_affairs(FOREIGN_AFFAIRS_00) && delay_cnt)
			{
				mdelay(10);
				delay_cnt--;
			}
                        */
			/* init flag */
			devp->flags &= ~VDIN_FLAG_DEC_STOP_ISR;
			devp->flags &= ~VDIN_FLAG_FORCE_UNSTABLE;
			/* clear the flag of decode started */
			devp->flags &= (~VDIN_FLAG_DEC_STARTED);
			if(vdin_dbg_en)
				pr_info("TVIN_IOC_STOP_DEC(%d) port %s, decode stop ok\n\n",
					parm->index, tvin_port_str(parm->port));
			mutex_unlock(&devp->fe_lock);
			break;
		}
		case TVIN_IOC_VF_REG:
			if ((devp->flags & VDIN_FLAG_DEC_REGED) == VDIN_FLAG_DEC_REGED) {
				pr_err("TVIN_IOC_VF_REG(%d) decoder is registered already\n", devp->index);
				ret = -EINVAL;
				break;
			}
			devp->flags |= VDIN_FLAG_DEC_REGED;
			vdin_vf_reg(devp);
			if(vdin_dbg_en)
				pr_info("TVIN_IOC_VF_REG(%d) ok\n\n", devp->index);
			break;
		case TVIN_IOC_VF_UNREG:
			if ((devp->flags & VDIN_FLAG_DEC_REGED) != VDIN_FLAG_DEC_REGED) {
				pr_err("TVIN_IOC_VF_UNREG(%d) decoder isn't registered\n", devp->index);
				ret = -EINVAL;
				break;
			}
			devp->flags &= (~VDIN_FLAG_DEC_REGED);
			vdin_vf_unreg(devp);
			if(vdin_dbg_en)
				pr_info("TVIN_IOC_VF_REG(%d) ok\n\n", devp->index);
			break;
		case TVIN_IOC_CLOSE: {
			struct tvin_parm_s *parm = &devp->parm;
			enum tvin_port_e port = parm->port;
			mutex_lock(&devp->fe_lock);
			if (!(devp->flags & VDIN_FLAG_DEC_OPENED)) {
				pr_err("TVIN_IOC_CLOSE(%d) you have not opened port\n", devp->index);
				ret = -EPERM;
				mutex_unlock(&devp->fe_lock);
				break;
			}
			vdin_close_fe(devp);
			devp->flags &= (~VDIN_FLAG_DEC_OPENED);
			if(vdin_dbg_en)
				pr_info("TVIN_IOC_CLOSE(%d) port %s closed ok\n\n", parm->index,
					tvin_port_str(port));
			mutex_unlock(&devp->fe_lock);
			break;
		}
		case TVIN_IOC_S_PARM: {
			struct tvin_parm_s parm = {0};
			if (copy_from_user(&parm, argp, sizeof(struct tvin_parm_s))) {
				ret = -EFAULT;
				break;
			}
			devp->parm.flag = parm.flag;
			devp->parm.reserved = parm.reserved;
			break;
		}
		case TVIN_IOC_G_PARM: {
			struct tvin_parm_s parm;
			memcpy(&parm, &devp->parm, sizeof(tvin_parm_t));
			if (devp->flags & VDIN_FLAG_FORCE_UNSTABLE)
				parm.info.status = TVIN_SIG_STATUS_UNSTABLE;
			if (copy_to_user(argp, &parm, sizeof(tvin_parm_t)))
				ret = -EFAULT;
			break;
		}
		case TVIN_IOC_G_SIG_INFO: {
			struct tvin_info_s info;
			memset(&info, 0, sizeof(tvin_info_t));
			mutex_lock(&devp->fe_lock);
			/* if port is not opened, ignore this command */
			if (!(devp->flags & VDIN_FLAG_DEC_OPENED)) {
				ret = -EPERM;
				mutex_unlock(&devp->fe_lock);
				break;
			}
			memcpy(&info, &devp->parm.info, sizeof(tvin_info_t));
			if (devp->flags & VDIN_FLAG_FORCE_UNSTABLE)
				info.status = TVIN_SIG_STATUS_UNSTABLE;
			/*meas the frame ratio for dvi save use parm.reserved high 8 bit*/
			if ((devp->parm.port >= TVIN_PORT_HDMI0) &&
				(devp->parm.port <= TVIN_PORT_HDMI7) )
			{
				if(devp->cycle)	{
					info.fps = (VDIN_CRYSTAL + (devp->cycle>>3))/devp->cycle;
				}
				if(vdin_dbg_en)
					pr_info("current dvi frame ratio is %u.cycle is %u.\n",info.fps,devp->cycle);
			}
			if (copy_to_user(argp, &info, sizeof(tvin_info_t))) {
				ret = -EFAULT;
				mutex_unlock(&devp->fe_lock);
			}
			mutex_unlock(&devp->fe_lock);
			break;
		}
		case TVIN_IOC_G_BUF_INFO: {
			struct tvin_buf_info_s buf_info;
			buf_info.buf_count	= devp->canvas_max_num;
			buf_info.buf_width	= devp->canvas_w;
			buf_info.buf_height = devp->canvas_h;
			buf_info.buf_size	= devp->canvas_max_size;
			buf_info.wr_list_size = devp->vfp->wr_list_size;
			if (copy_to_user(argp, &buf_info, sizeof(struct tvin_buf_info_s)))
				ret = -EFAULT;
			break;
		}
		case TVIN_IOC_START_GET_BUF:
			devp->vfp->wr_next = devp->vfp->wr_list.next;
			break;
		case TVIN_IOC_GET_BUF: {
			struct tvin_video_buf_s tvbuf;
			struct vf_entry *vfe;
			vfe = list_entry(devp->vfp->wr_next, struct vf_entry, list);
			devp->vfp->wr_next = devp->vfp->wr_next->next;
			if (devp->vfp->wr_next != &devp->vfp->wr_list) {
				tvbuf.index = vfe->vf.index;
			}
			else {
				tvbuf.index = -1;
			}
			if (copy_to_user(argp, &tvbuf, sizeof(struct tvin_video_buf_s)))
				ret = -EFAULT;
			break;
		}
		case TVIN_IOC_PAUSE_DEC:
			vdin_pause_dec(devp);
			break;
		case TVIN_IOC_RESUME_DEC:
			vdin_resume_dec(devp);
			break;
                case TVIN_IOC_FREEZE_VF: {
                        mutex_lock(&devp->fe_lock);
		        if(!(devp->flags & VDIN_FLAG_DEC_STARTED)) {
		                pr_err("TVIN_IOC_FREEZE_BUF(%d) decode havn't started\n", devp->index);
			        ret = -EPERM;
			        mutex_unlock(&devp->fe_lock);
			        break;
                        }

                        if(devp->fmt_info_p->scan_mode == TVIN_SCAN_MODE_PROGRESSIVE)
                                vdin_vf_freeze(devp->vfp, 1);
                        else
                                vdin_vf_freeze(devp->vfp, 2);
                        mutex_lock(&devp->fe_lock);
                        break;
                }
                case TVIN_IOC_UNFREEZE_VF: {
                        mutex_lock(&devp->fe_lock);
		        if(!(devp->flags & VDIN_FLAG_DEC_STARTED)) {
		                pr_err("TVIN_IOC_FREEZE_BUF(%d) decode havn't started\n", devp->index);
			        ret = -EPERM;
			        mutex_unlock(&devp->fe_lock);
			        break;
		        }
                        vdin_vf_unfreeze(devp->vfp);
                        mutex_lock(&devp->fe_lock);
                        break;
                }
		case TVIN_IOC_CALLMASTER_SET: {
		        enum tvin_port_e port_call = TVIN_PORT_NULL;
			tvin_frontend_t *frontend = NULL;
			//struct vdin_dev_s *devp = dev_get_drvdata(dev);
			if (copy_from_user(&port_call, argp, sizeof(enum tvin_port_e))) {
				ret = -EFAULT;
			        break;
			}
			frontend = tvin_get_frontend(port_call,0);
			if(frontend&&frontend->dec_ops&&frontend->dec_ops->callmaster_det){
				/*call the frontend det function*/
				callmaster_status = frontend->dec_ops->callmaster_det(port_call,frontend);
			}
			if(vdin_dbg_en)
				pr_info("[vdin.%d]:%s callmaster_status=%d,port_call=[%s]\n",devp->index,__func__,callmaster_status,tvin_port_str(port_call));
			break;
		}
		case TVIN_IOC_CALLMASTER_GET: {
			if (copy_to_user(argp, &callmaster_status, sizeof(int))){
				ret = -EFAULT;
				break;
			}
			callmaster_status=0;
			break;
		}
		default:
			ret = -ENOIOCTLCMD;
			//pr_info("%s %d is not supported command\n", __func__, cmd);
			break;
	}
	return ret;
}

static int vdin_mmap(struct file *file, struct vm_area_struct * vma)
{
	vdin_dev_t *devp = file->private_data;
	unsigned long start, len, off;
	unsigned long pfn, size;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT)) {
		return -EINVAL;
	}

	start = devp->mem_start & PAGE_MASK;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + devp->mem_size);

	off = vma->vm_pgoff << PAGE_SHIFT;

	if ((vma->vm_end - vma->vm_start + off) > len) {
		return -EINVAL;
	}

	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;

	size = vma->vm_end - vma->vm_start;
	pfn  = off >> PAGE_SHIFT;

	if (io_remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static struct file_operations vdin_fops = {
	.owner	         = THIS_MODULE,
	.open	         = vdin_open,
	.release         = vdin_release,
	.unlocked_ioctl  = vdin_ioctl,
	.mmap	         = vdin_mmap,
};

static int vdin_add_cdev(struct cdev *cdevp, struct file_operations *fops,
		int minor)
{
	int ret;
	dev_t devno = MKDEV(MAJOR(vdin_devno), minor);
	cdev_init(cdevp, fops);
	cdevp->owner = THIS_MODULE;
	ret = cdev_add(cdevp, devno, 1);
	return ret;
}

static struct device * vdin_create_device(struct device *parent, int minor)
{
	dev_t devno = MKDEV(MAJOR(vdin_devno), minor);
	return device_create(vdin_clsp, parent, devno, NULL, "%s%d",
			VDIN_DEV_NAME, minor);
}

static void vdin_delete_device(int minor)
{
	dev_t devno = MKDEV(MAJOR(vdin_devno), minor);
	device_destroy(vdin_clsp, devno);
}

static struct resource memobj;
static int vdin_drv_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct vdin_dev_s *vdevp;
	struct resource *res;
	const void *name;
	int offset,size;
	struct device_node *of_node = pdev->dev.of_node;

	/* malloc vdev */
	vdevp = kmalloc(sizeof(struct vdin_dev_s), GFP_KERNEL);
	if (!vdevp) {
		pr_err("%s: failed to allocate memory.\n", __func__);
		goto fail_kmalloc_vdev;
	}
	memset(vdevp, 0, sizeof(struct vdin_dev_s));
	#ifdef CONFIG_USE_OF
	if (pdev->dev.of_node) {
	        ret = of_property_read_u32(pdev->dev.of_node,"vdin_id",&(vdevp->index));
		if(ret) {
			pr_err("don't find  vdin id.\n");
			goto fail_get_resource_irq;
		}
	}
	#else
	vdevp->index = pdev->id;
        #endif
	vdin_devp[vdevp->index] = vdevp;
	/* create cdev and reigser with sysfs */
	ret = vdin_add_cdev(&vdevp->cdev, &vdin_fops, vdevp->index);
	if (ret) {
		pr_err("%s: failed to add cdev.\n", __func__);
		goto fail_add_cdev;
	}
	vdevp->dev = vdin_create_device(&pdev->dev, vdevp->index);
	if (IS_ERR(vdevp->dev)) {
		pr_err("%s: failed to create device.\n", __func__);
		ret = PTR_ERR(vdevp->dev);
		goto fail_create_device;
	}
	ret = vdin_create_device_files(vdevp->dev);
	if(ret < 0) {
		pr_err("%s: fail to create vdin attribute files.\n", __func__);
		goto fail_create_dev_file;
	}
	/* get memory address from resource */
#if 0
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
#else
	res = &memobj;
	ret = find_reserve_block(pdev->dev.of_node->name,0);
	if(ret < 0) {
	        name = of_get_property(of_node, "share-memory-name", NULL);
	        if(!name) {
	                pr_err("\nvdin memory resource undefined1.\n");
	                ret = -EFAULT;
	                goto fail_get_resource_mem;
	        }
	        else {
	                ret= find_reserve_block_by_name(name);
	                if(ret<0) {
	                        pr_err("\nvdin memory resource undefined2.\n");
	                        ret = -EFAULT;
	                        goto fail_get_resource_mem;
	                }
	                name = of_get_property(of_node, "share-memory-offset", NULL);
	                if(name)
	                        offset = of_read_ulong(name,1);
	                else {
	                        pr_err("\nvdin memory resource undefined3.\n");
	                        ret = -EFAULT;
	                        goto fail_get_resource_mem;
	                }
	                        name = of_get_property(of_node, "share-memory-size", NULL);
	                if(name)
	                        size = of_read_ulong(name,1);
	                else {
	                        pr_err("\nvdin memory resource undefined4.\n");
	                        ret = -EFAULT;
	                        goto fail_get_resource_mem;
	                }

	                res->start = (phys_addr_t)get_reserve_block_addr(ret)+offset;
	                res->end = res->start+ size-1;

	        }
	}
	else {
	        res->start = (phys_addr_t)get_reserve_block_addr(ret);
	        res->end = res->start+ (phys_addr_t)get_reserve_block_size(ret)-1;
        }
#endif
	if (!res) {
		pr_err("%s: can't get mem resource\n", __func__);
		ret = -ENXIO;
		goto fail_get_resource_mem;
	}

	vdevp->mem_start = res->start;
	vdevp->mem_size  = res->end - res->start + 1;
	pr_info("vdin%d mem_start = 0x%x, mem_size = 0x%x\n", vdevp->index,
			vdevp->mem_start,vdevp->mem_size);


	/* get irq from resource */
	#ifdef CONFIG_USE_OF
	if (pdev->dev.of_node) {
	        ret = of_property_read_u32(pdev->dev.of_node,"irq",&(res->start));
		if(ret) {
			pr_err("don't find  match irq\n");
			goto fail_get_resource_irq;
		}
		res->end = res->start;
		res->flags = IORESOURCE_IRQ;
	}
	#else
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		pr_err("%s: can't get irq resource\n", __func__);
		ret = -ENXIO;
		goto fail_get_resource_irq;
	}
	#endif
	vdevp->irq = res->start;
        snprintf(vdevp->irq_name, sizeof(vdevp->irq_name),  "vdin%d-irq", vdevp->index);
	/* init vdin parameters */
	vdevp->flags = VDIN_FLAG_NULL;
	vdevp->flags &= (~VDIN_FLAG_FS_OPENED);
	mutex_init(&vdevp->mm_lock);
	mutex_init(&vdevp->fe_lock);
	spin_lock_init(&vdevp->isr_lock);
	vdevp->frontend = NULL;

	/* @todo vdin_addr_offset */
	vdevp->addr_offset = vdin_addr_offset[vdevp->index];
        /*disable vdin hardware*/
        vdin_enable_module(vdevp->addr_offset, false);
	vdevp->flags = 0;

	/* create vf pool */
	vdevp->vfp = vf_pool_alloc(VDIN_CANVAS_MAX_CNT);

	/* init vframe provider */
	/* @todo provider name */
	sprintf(vdevp->name, "%s%d", PROVIDER_NAME, vdevp->index);
	vf_provider_init(&vdevp->vprov, vdevp->name, &vdin_vf_ops, vdevp->vfp);
	/* @todo canvas_config_mode */
	if (canvas_config_mode == 0 || canvas_config_mode == 1) {
		vdin_canvas_init(vdevp);
	}

	/* set drvdata */
	dev_set_drvdata(vdevp->dev, vdevp);
	platform_set_drvdata(pdev, vdevp);

	pr_info("%s: driver initialized ok\n", __func__);
	return 0;

fail_get_resource_irq:
fail_get_resource_mem:
fail_create_dev_file:
	vdin_delete_device(vdevp->index);
fail_create_device:
	cdev_del(&vdevp->cdev);
fail_add_cdev:
	kfree(vdevp);
fail_kmalloc_vdev:
	return ret;
}

static int vdin_drv_remove(struct platform_device *pdev)
{
	struct vdin_dev_s *vdevp;
	vdevp = platform_get_drvdata(pdev);

	mutex_destroy(&vdevp->mm_lock);
	mutex_destroy(&vdevp->fe_lock);

	vf_pool_free(vdevp->vfp);
	vdin_remove_device_files(vdevp->dev);
	vdin_delete_device(vdevp->index);
	cdev_del(&vdevp->cdev);
	vdin_devp[vdevp->index] = NULL;
	kfree(vdevp);

	/* free drvdata */
	dev_set_drvdata(vdevp->dev, NULL);
	platform_set_drvdata(pdev, NULL);

	pr_info("%s: driver removed ok\n", __func__);
	return 0;
}

#ifdef CONFIG_PM
static int vdin_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct vdin_dev_s *vdevp;
	vdevp = platform_get_drvdata(pdev);
        vdin_enable_module(vdevp->addr_offset, false);
        pr_info("%s ok.\n",__func__);
	return 0;
}

static int vdin_drv_resume(struct platform_device *pdev)
{
	struct vdin_dev_s *vdevp;
	vdevp = platform_get_drvdata(pdev);
        vdin_enable_module(vdevp->addr_offset, true);
        pr_info("%s ok.\n",__func__);
	return 0;
}
#endif
#ifdef CONFIG_OF
static const struct of_device_id vdin_dt_match[]={
        {       .compatible = "amlogic,vdin",   },
        {},
};
#else
#define vdin_dt_match NULL
#endif
static struct platform_driver vdin_driver = {
	.probe		= vdin_drv_probe,
	.remove		= vdin_drv_remove,
#ifdef CONFIG_PM
	.suspend	= vdin_drv_suspend,
	.resume		= vdin_drv_resume,
#endif
	.driver 	= {
		.name	        = VDIN_DRV_NAME,
                .of_match_table = vdin_dt_match,
}
};

extern int vdin_reg_v4l2(vdin_v4l2_ops_t *v4l2_ops);
extern void vdin_unreg_v4l2(void);

static int __init vdin_drv_init(void)
{
	int ret = 0;

	ret = alloc_chrdev_region(&vdin_devno, 0, VDIN_MAX_DEVS, VDIN_DEV_NAME);
	if (ret < 0) {
		pr_err("%s: failed to allocate major number\n", __func__);
		goto fail_alloc_cdev_region;
	}

	pr_info("%s: major %d\n", __func__, MAJOR(vdin_devno));

	vdin_clsp = class_create(THIS_MODULE, VDIN_CLS_NAME);
	if (IS_ERR(vdin_clsp)) {
		ret = PTR_ERR(vdin_clsp);
		pr_err("%s: failed to create class\n", __func__);
		goto fail_class_create;
	}

	ret = vdin_create_class_files(vdin_clsp);

	ret = platform_driver_register(&vdin_driver);

	if (ret != 0) {
		pr_err("%s: failed to register driver\n", __func__);
		goto fail_pdrv_register;
	}
	/*register vdin for v4l2 interface*/
        if(vdin_reg_v4l2(&vdin_4v4l2_ops))
                pr_err("[vdin..] %s: register vdin v4l2 error.\n",__func__);
	return 0;

fail_pdrv_register:
	class_destroy(vdin_clsp);
fail_class_create:
	unregister_chrdev_region(vdin_devno, VDIN_MAX_DEVS);
fail_alloc_cdev_region:
	return ret;
}

static void __exit vdin_drv_exit(void)
{
	vdin_unreg_v4l2();
	vdin_remove_class_files(vdin_clsp);
	class_destroy(vdin_clsp);
	unregister_chrdev_region(vdin_devno, VDIN_MAX_DEVS);
	platform_driver_unregister(&vdin_driver);
}

module_init(vdin_drv_init);
module_exit(vdin_drv_exit);


MODULE_VERSION(VDIN_VER);

MODULE_DESCRIPTION("AMLOGIC VDIN Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xu Lin <lin.xu@amlogic.com>");

#ifdef CONFIG_TVIN_VDIN_CTRL
int vdin_ctrl_open_fe(int no , int port)
{
	struct tvin_frontend_s *fe;
        int ret = 0;
	struct vdin_dev_s *devp = vdin_devp[no];
        if(IS_ERR(devp)){
                printk(KERN_ERR "[vdin..]%s vdin%d has't registered,please register.\n",__func__,no);
                return -1;
        }
        if (devp->flags & VDIN_FLAG_DEC_STARTED) {
		pr_err("%s: port 0x%x, decode started already.\n",__func__, port);
		ret = -EBUSY;
	}

        devp->flags |= VDIN_FLAG_FS_OPENED;
	/* request irq */
	snprintf(devp->irq_name, sizeof(devp->irq_name),  "vdin%d-irq", devp->index);
	if (work_mode_simple) {
	        pr_info("vdin work in simple mode\n");
	        ret = request_irq(devp->irq, vdin_isr_simple, IRQF_SHARED, devp->irq_name, (void *)devp);
	} else {
	        pr_info("vdin work in normal mode\n");
	        ret = request_irq(devp->irq, vdin_isr, IRQF_SHARED, devp->irq_name, (void *)devp);
	}

        /*disable irq untill vdin is configured completely*/
        disable_irq_nosync(devp->irq);
	/* remove the hardware limit to vertical [0-max]*/
	WRITE_CBUS_REG(VPP_PREBLEND_VD1_V_START_END, 0x00000fff);
	pr_info("open device %s ok\n", dev_name(devp->dev));
        vdin_open_fe(port,0,devp);
        devp->parm.port = port;
        devp->parm.info.fmt = TVIN_SIG_FMT_NULL;
	return 0;
}

int vdin_ctrl_close_fe(int no)
{
	struct vdin_dev_s *devp;
	devp = vdin_devp[no];

	del_timer_sync(&devp->timer);

        /*close fe*/
        if(devp->frontend->dec_ops->close)
                devp->frontend->dec_ops->close(devp->frontend);
        /*free the memory allocated in start tvin service*/
	if(devp->parm.info.fmt >= TVIN_SIG_FMT_MAX)
                kfree(devp->fmt_info_p);
#ifdef CONFIG_ARCH_MESON6
        switch_mod_gate_by_name("vdin", 0);
#endif
	devp->flags &= (~VDIN_FLAG_DEC_OPENED);
	devp->flags &= (~VDIN_FLAG_DEC_STARTED);
	/* free irq */
	free_irq(devp->irq,(void *)devp);
	return 0;
}


int vdin_ctrl_start_fe(int no ,vdin_parm_t *para)
{
	struct tvin_frontend_s *fe;
        int ret = 0;
	struct vdin_dev_s *devp = vdin_devp[no];
        if(IS_ERR(devp)){
                printk(KERN_ERR "[vdin..]%s vdin%d has't registered,please register.\n",__func__,no);
                return -1;
        }
        if (devp->flags & VDIN_FLAG_DEC_STARTED) {
		pr_err("%s: port 0x%x, decode started already.\n",__func__,para->port);
		ret = -EBUSY;
	}

	devp->parm.port         = para->port;
	devp->parm.info.fmt     = para->fmt;
	//add for camera random resolution
	if(para->fmt >= TVIN_SIG_FMT_MAX){
                devp->fmt_info_p = kmalloc(sizeof(tvin_format_t),GFP_KERNEL);
                if(!devp->fmt_info_p){
                        printk("[vdin..]%s kmalloc error.\n",__func__);
                        return -ENOMEM;
                }
                devp->fmt_info_p->hs_bp     = para->hs_bp;
                devp->fmt_info_p->vs_bp     = para->vs_bp;
                devp->fmt_info_p->hs_pol    = para->hsync_phase;
                devp->fmt_info_p->vs_pol    = para->vsync_phase;
	        devp->fmt_info_p->h_active  = para->h_active;
	        devp->fmt_info_p->v_active  = para->v_active;
                devp->fmt_info_p->scan_mode = para->scan_mode;
                devp->fmt_info_p->duration  = 96000/para->frame_rate;
                devp->fmt_info_p->pixel_clk = para->h_active*para->v_active*para->frame_rate;
                devp->fmt_info_p->pixel_clk /=10000;
	}else{
                devp->fmt_info_p = tvin_get_fmt_info(devp->parm.info.fmt);
        }
        if(!devp->fmt_info_p) {
		pr_err("%s(%d): error, fmt is null!!!\n", __func__, no);
		return -1;
        }
        fe = tvin_get_frontend(para->port, 0);
        if(fe){
                fe->private_data = para;
                fe->port         = para->port;
	        devp->frontend   = fe;

        }else{
		pr_err("%s(%d): not supported port 0x%x \n", __func__, no, para->port);
		return -1;
	}
	//disable cut window?
	devp->parm.cutwin.he = 0;
	devp->parm.cutwin.hs = 0;
	devp->parm.cutwin.ve = 0;
	devp->parm.cutwin.vs = 0;
        /*add for viu loop back scaler down*/
	if(TVIN_PORT_VIU == para->port){
		devp->scaler4w = para->reserved & 0xffff;
		devp->scaler4h = para->reserved >> 16;
        }
        #ifdef CONFIG_ARCH_MESON6
        switch_mod_gate_by_name("vdin", 1);
        #endif
	vdin_start_dec(devp);
	devp->flags = VDIN_FLAG_DEC_OPENED;
	devp->flags |= VDIN_FLAG_DEC_STARTED;
	return 0;
}

int vdin_ctrl_stop_fe(int no)
{
	struct vdin_dev_s *devp;
	devp = vdin_devp[no];
	if(!(devp->flags&VDIN_FLAG_DEC_STARTED)){
		pr_err("%s:decode hasn't started.\n",__func__);
		return -EBUSY;
	}
	devp->flags |= VDIN_FLAG_DEC_STOP_ISR;
	vdin_stop_dec(devp);

	devp->flags &= (~VDIN_FLAG_DEC_STARTED);

	return 0;
}

enum tvin_sig_fmt_e  vdin_ctrl_get_fmt(int no)
{
	struct vdin_dev_s *devp;
	devp = vdin_devp[no];
        if(devp->frontend && devp->frontend->sm_ops){
	    return  devp->frontend->sm_ops->get_fmt(devp->frontend);
	}
	return TVIN_SIG_FMT_NULL;

}
#endif
