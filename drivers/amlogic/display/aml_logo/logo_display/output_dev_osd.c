//we will need ge2d device to transfer logo data ,
//so will also startup ge2d hardware.
#include "logo.h"
#include "logo_dev_osd.h"
#include "dev_ge2d.h"
#include <linux/wait.h>
#include	"amlogo_log.h" 
#include <linux/amlogic/amlog.h>
#include <linux/amlogic/vout/vinfo.h>
#include <linux/amlogic/vout/vout_notify.h>

static int osd0_init(logo_object_t *plogo);
static int osd1_init(logo_object_t *plogo);
static int osd_enable_set(int  enable);
static int osd_deinit(void);
static int osd_transfer(logo_object_t *plogo);

static  logo_output_dev_t   output_osd0={
	.idx=LOGO_DEV_OSD0,
	.hw_initialized=0,	
	.op={
		.init=osd0_init,
		.transfer=osd_transfer,
		.enable=osd_enable_set,
		.deinit=osd_deinit,
		},
};
static  logo_output_dev_t   output_osd1={
	.idx=LOGO_DEV_OSD1,
	.hw_initialized=0,		
	.op={
		.init=osd1_init,
		.transfer=osd_transfer,
		.enable=osd_enable_set,
		.deinit=osd_deinit,
		},
};

#ifdef CONFIG_AM_HDMI_ONLY
static  hdmi_only_info_t hdmi_only_info[PARA_HDMI_ONLY]={
	{"480i",VMODE_480I},
	{"480p",VMODE_480P},
	{"576i",VMODE_576I},
	{"576p",VMODE_576P},
	{"720p",VMODE_720P},
	{"800p",VMODE_800P},
	{"vga",VMODE_VGA},
	{"sxga",VMODE_SXGA},
	{"xga",VMODE_XGA},	
	{"1920x1200", VMODE_1920x1200},
	{"1080i",VMODE_1080I},
	{"1080p",VMODE_1080P},
	{"720p50hz",VMODE_720P_50HZ},
	{"1080i50hz",VMODE_1080I_50HZ},
	{"1080p50hz",VMODE_1080P_50HZ},
	{"1080p24hz", VMODE_1080P_24HZ},
	{"4k2k24hz",VMODE_4K2K_24HZ},
	{"4k2k25hz",VMODE_4K2K_25HZ},
	{"4k2k30hz",VMODE_4K2K_30HZ},
	{"4k2ksmpte",VMODE_4K2K_SMPTE},
};

static vmode_t hdmimode_hdmionly = VMODE_1080P;
#endif

static vmode_t cvbsmode_hdmionly = VMODE_480CVBS;

static  inline void  setup_color_mode(const color_bit_define_t *color,u32  reg)
{
	u32  data32;

	data32 = aml_read_reg32(reg)&(~(0xf<<8));
	data32 |=  color->hw_blkmode<< 8; /* osd_blk_mode */
	aml_write_reg32(reg, data32);
}
static inline u32 get_curr_color_depth(u32 reg)
{
	u32  data32=0;

	data32 =(aml_read_reg32(reg)>>8)&0xf;
	switch(data32)
	{
		case 4:
		data32=16;
		break;
		case 7:
		data32=24;
		break;
		case 5:
		data32=32;
		break;
	}
	return data32;
}
static int osd_hw_setup(logo_object_t *plogo)
{
	struct osd_ctl_s  osd_ctl;
	const color_bit_define_t  *color;

	osd_ctl.addr=plogo->dev->output_dev.osd.mem_start;
	osd_ctl.index=plogo->dev->idx;
	plogo->dev->output_dev.osd.color_depth=plogo->parser->logo_pic_info.color_info;
	color=&default_color_format_array[plogo->dev->output_dev.osd.color_depth];
	
	osd_ctl.xres=plogo->dev->vinfo->width ;					//logo pic.	
	osd_ctl.yres=plogo->dev->vinfo->height;
	osd_ctl.xres_virtual=plogo->dev->vinfo->width ;
	osd_ctl.yres_virtual=plogo->dev->vinfo->height<<1;
	osd_ctl.disp_start_x=0;
	osd_ctl.disp_end_x=osd_ctl.xres -1;
	osd_ctl.disp_start_y=0;
	osd_ctl.disp_end_y=osd_ctl.yres-1;
	osd_init_hw(0);
	setup_color_mode(color,osd_ctl.index==0?P_VIU_OSD1_BLK0_CFG_W0:P_VIU_OSD2_BLK0_CFG_W0);
	if(!plogo->para.loaded)
	{
	    if(plogo->dev->idx == LOGO_DEV_OSD0){
	        aml_set_reg32_mask(P_VPP_MISC,VPP_OSD1_POSTBLEND);
	    }else if(plogo->dev->idx == LOGO_DEV_OSD1){
	        aml_set_reg32_mask(P_VPP_MISC,VPP_OSD2_POSTBLEND);
	    }
	}
	osd_setup(&osd_ctl, \
					0, \
					0, \
					osd_ctl.xres, \
					osd_ctl.yres, \
					osd_ctl.xres_virtual, \
					osd_ctl.yres_virtual, \
					osd_ctl.disp_start_x, \
					osd_ctl.disp_start_y, \
					osd_ctl.disp_end_x, \
					osd_ctl.disp_end_y, \
					osd_ctl.addr, \
					color, \
					osd_ctl.index) ;

	return SUCCESS;
	
}

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
void set_osd_freescaler(int index, logo_object_t *plogo, vmode_t new_mode) {
    vmode_t old_mode = plogo->para.vout_mode & VMODE_MODE_BIT_MASK;
    printk("aml_logo: outputmode changed(%d->%d), reset osd%d scaler.\n", old_mode, new_mode, index);
    osd_free_scale_mode_hw(index, 1);
    osd_free_scale_enable_hw(index, 0);
    osd_set_color_mode(index, &default_color_format_array[plogo->dev->output_dev.osd.color_depth]);
    switch(old_mode) {
        case VMODE_480I:
        case VMODE_480CVBS:
        case VMODE_480P:
        case VMODE_576I:
        case VMODE_576CVBS:
        case VMODE_576P:
        case VMODE_720P:
        case VMODE_720P_50HZ:
            osd_set_free_scale_axis_hw(index, 0, 0, 1279, 719);
            osddev_update_disp_axis_hw(0, 1279, 0, 719, 0, 0, 0, index);
            break;
        case VMODE_1080I:
        case VMODE_1080I_50HZ:
        case VMODE_1080P:
        case VMODE_1080P_50HZ:
        case VMODE_1080P_24HZ:
        case VMODE_4K2K_24HZ:
        case VMODE_4K2K_25HZ:
        case VMODE_4K2K_30HZ:
        case VMODE_4K2K_SMPTE:
            osd_set_free_scale_axis_hw(index, 0, 0, 1919, 1079);
            osddev_update_disp_axis_hw(0, 1919, 0, 1079, 0, 0, 0, index);
            break;
		default:
			break;
   }
   switch(new_mode) {
        case VMODE_480I:
        case VMODE_480CVBS:
        case VMODE_480P:
            osd_set_window_axis_hw(index, 0, 0, 719, 479);
            break;
        case VMODE_576I:
        case VMODE_576CVBS:
        case VMODE_576P:
            osd_set_window_axis_hw(index, 0, 0, 719, 575);
            break;
        case VMODE_720P:
        case VMODE_720P_50HZ:
            osd_set_window_axis_hw(index, 0, 0, 1279, 719);
             break;
        case VMODE_1080I:
        case VMODE_1080I_50HZ:
        case VMODE_1080P:
        case VMODE_1080P_50HZ:
        case VMODE_1080P_24HZ:
            osd_set_window_axis_hw(index, 0, 0, 1919, 1079);
            break;
        case VMODE_4K2K_24HZ:
        case VMODE_4K2K_25HZ:
        case VMODE_4K2K_30HZ:
            osd_set_window_axis_hw(index, 0, 0, 3839, 2159);
            break;
        case VMODE_4K2K_SMPTE:
            osd_set_window_axis_hw(index, 0, 0, 4095, 2159);
            break;
		default:
			break;
   }
   osd_free_scale_enable_hw(index, 0x10001);
   osd_enable_hw(1, index);
}
#endif

static int osd0_init(logo_object_t *plogo)
{
#if defined(CONFIG_AM_HDMI_ONLY)
	int hpd_state = 0;
#endif
#if defined(CONFIG_AM_HDMI_ONLY) || (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
	vmode_t cur_mode = plogo->para.vout_mode;
#endif

	if(plogo->para.output_dev_type==output_osd0.idx)
	{
		DisableVideoLayer();
		if((plogo->platform_res[output_osd0.idx].mem_end - plogo->platform_res[output_osd0.idx].mem_start) ==0 ) 
		{
			return OUTPUT_DEV_UNFOUND;
		}
		if(plogo->para.loaded)
		{
			osd_init_hw(plogo->para.loaded);
			if(plogo->para.vout_mode > VMODE_4K2K_SMPTE){
				plogo->para.vout_mode|=VMODE_LOGO_BIT_MASK;
			}
		}
#ifdef CONFIG_AM_HDMI_ONLY
		if(plogo->para.vout_mode > VMODE_4K2K_SMPTE) {
			set_current_vmode(plogo->para.vout_mode);
		}else{
			extern int read_hpd_gpio(void);
			hpd_state = read_hpd_gpio();
    		
			if (hpd_state == 0){
			    cur_mode = cvbsmode_hdmionly;
			}
			else{
			    cur_mode = hdmimode_hdmionly;
			}
			set_current_vmode(cur_mode);
		}
#else
		set_current_vmode(plogo->para.vout_mode);
#endif

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
		osd_init_scan_mode();
#endif
		output_osd0.vinfo=get_current_vinfo();
		plogo->dev=&output_osd0;
		plogo->dev->window.x=0;
		plogo->dev->window.y=0;
		plogo->dev->window.w=plogo->dev->vinfo->width;
		plogo->dev->window.h=plogo->dev->vinfo->height;
		plogo->dev->output_dev.osd.mem_start=plogo->platform_res[LOGO_DEV_OSD0].mem_start;
		plogo->dev->output_dev.osd.mem_end=plogo->platform_res[LOGO_DEV_OSD0].mem_end;
		plogo->dev->output_dev.osd.color_depth=get_curr_color_depth(P_VIU_OSD1_BLK0_CFG_W0);//setup by uboot
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
		if((cur_mode != (plogo->para.vout_mode & VMODE_MODE_BIT_MASK)) && (cur_mode <= VMODE_4K2K_SMPTE)) {
		    set_osd_freescaler(LOGO_DEV_OSD0, plogo, cur_mode);
		}
#endif
		return OUTPUT_DEV_FOUND;
	}
	return OUTPUT_DEV_UNFOUND;
}
static int osd1_init(logo_object_t *plogo)
{
#if defined(CONFIG_AM_HDMI_ONLY)
	int hpd_state = 0;
#endif
#if defined(CONFIG_AM_HDMI_ONLY) || (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
	vmode_t cur_mode = plogo->para.vout_mode;
#endif

	if(plogo->para.output_dev_type==output_osd1.idx)
	{
		DisableVideoLayer();
		if((plogo->platform_res[output_osd1.idx].mem_end - plogo->platform_res[output_osd1.idx].mem_start) ==0)
		{
			return OUTPUT_DEV_UNFOUND;
		}
		if(plogo->para.loaded)
		{
			osd_init_hw(plogo->para.loaded);
			if(plogo->para.vout_mode > VMODE_4K2K_SMPTE){
				plogo->para.vout_mode|=VMODE_LOGO_BIT_MASK;
			}
		}
#ifdef CONFIG_AM_HDMI_ONLY
		if(plogo->para.vout_mode > VMODE_4K2K_SMPTE) {
			set_current_vmode(plogo->para.vout_mode);
		}else{
			extern int read_hpd_gpio(void);
			hpd_state = read_hpd_gpio();
    		
			if (hpd_state == 0){
			    cur_mode = cvbsmode_hdmionly;
			}
			else{
			    cur_mode = hdmimode_hdmionly;
			}
			set_current_vmode(cur_mode);
		}
#else
		set_current_vmode(plogo->para.vout_mode);
#endif

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
		osd_init_scan_mode();
#endif
		output_osd1.vinfo=get_current_vinfo();
		plogo->dev=&output_osd1;
		plogo->dev->window.x=0;
		plogo->dev->window.y=0;
		plogo->dev->window.w=plogo->dev->vinfo->width;
		plogo->dev->window.h=plogo->dev->vinfo->height;
		plogo->dev->output_dev.osd.mem_start=plogo->platform_res[LOGO_DEV_OSD1].mem_start;
		plogo->dev->output_dev.osd.mem_end=plogo->platform_res[LOGO_DEV_OSD1].mem_end;
		plogo->dev->output_dev.osd.color_depth=get_curr_color_depth(P_VIU_OSD2_BLK0_CFG_W0);//setup by uboot
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
		if((cur_mode != (plogo->para.vout_mode & VMODE_MODE_BIT_MASK)) && (cur_mode <= VMODE_4K2K_SMPTE)) {
		    set_osd_freescaler(LOGO_DEV_OSD1, plogo, cur_mode);
		}
#endif
		return OUTPUT_DEV_FOUND;
	}
	return OUTPUT_DEV_UNFOUND;
}

static  int  osd_enable_set(int  enable)
{
	return SUCCESS;
}
static int osd_deinit(void)
{
	return SUCCESS;
}
//just an examble;
static  int  thread_progress(void *para)
{
#define	OFFSET_PROGRESS_X 15
#define  START_PROGRESS_X	70
#define  START_PROGRESS_Y	70
#define  PROGRESS_HEIGHT	25 
#define  PORGRESS_BORDER	3
	unsigned int progress= 0 ;
	unsigned int step =1;
	src_dst_info_t  op_info;
	logo_object_t *plogo=(logo_object_t*)para;
	ge2d_context_t  *context=plogo->dev->ge2d_context;
	wait_queue_head_t  wait_head;

	init_waitqueue_head(&wait_head);
	op_info.color=0x555555ff;
	op_info.dst_rect.x=START_PROGRESS_X+OFFSET_PROGRESS_X;
	op_info.dst_rect.y=plogo->dev->vinfo->height-START_PROGRESS_Y;
	op_info.dst_rect.w=(plogo->dev->vinfo->width -START_PROGRESS_X*2);
	op_info.dst_rect.h=PROGRESS_HEIGHT;
	dev_ge2d_cmd(context,CMD_FILLRECT,&op_info);

	op_info.dst_rect.x+=PORGRESS_BORDER;
	op_info.dst_rect.y+=PORGRESS_BORDER;
	op_info.dst_rect.w=(plogo->dev->vinfo->width -START_PROGRESS_X*2-PORGRESS_BORDER*2)*step/100;
	op_info.dst_rect.h=PROGRESS_HEIGHT -PORGRESS_BORDER*2;
	op_info.color=0x00ffff;
	while(progress < 100)
	{
		dev_ge2d_cmd(context,CMD_FILLRECT,&op_info);
		wait_event_interruptible_timeout(wait_head,0,7);
		progress+=step;
		op_info.dst_rect.x+=op_info.dst_rect.w;
		op_info.color-=(0xff*step/100)<<8; //color change smoothly.
	}
	op_info.dst_rect.w=(plogo->dev->vinfo->width -START_PROGRESS_X-PORGRESS_BORDER+ OFFSET_PROGRESS_X) - op_info.dst_rect.x ;
	dev_ge2d_cmd(context,CMD_FILLRECT,&op_info);
	return 0;
}
static  int  osd_transfer(logo_object_t *plogo)
{
	src_dst_info_t  op_info;
	ge2d_context_t  *context;
	config_para_t	ge2d_config;
	int  screen_mem_start;
	int  screen_size ;
	u32  	canvas_index;
	canvas_t	canvas;		
	
	if(!plogo->dev->hw_initialized) // hardware need initialized first .
	{
		if(osd_hw_setup(plogo))
		return  -OUTPUT_DEV_SETUP_FAIL;
		amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"osd hardware initiate success\n");
	}
	if (plogo->need_transfer==FALSE) return -EDEV_NO_TRANSFER_NEED;

	ge2d_config.src_dst_type=plogo->dev->idx?OSD1_OSD1:OSD0_OSD0;
	ge2d_config.alu_const_color=0x000000ff;
	context=dev_ge2d_setup(&ge2d_config);
	//we use ge2d to strechblit pic.
	if(NULL==context) return -OUTPUT_DEV_SETUP_FAIL;
	amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"logo setup ge2d device OK\n");
	plogo->dev->ge2d_context=context;
	//clear dst rect
	op_info.color=0x000000ff;
	op_info.dst_rect.x=0;
	op_info.dst_rect.y=0;
	op_info.dst_rect.w=plogo->dev->vinfo->width;
	op_info.dst_rect.h=plogo->dev->vinfo->height;
	dev_ge2d_cmd(context,CMD_FILLRECT,&op_info);
	amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"fill==dst:%d-%d-%d-%d\n",op_info.dst_rect.x,op_info.dst_rect.y,op_info.dst_rect.w,op_info.dst_rect.h);	

	op_info.src_rect.x=0;  //setup origin src rect 
	op_info.src_rect.y=0;
	op_info.src_rect.w=plogo->parser->logo_pic_info.width;
	op_info.src_rect.h=plogo->parser->logo_pic_info.height;

	switch (plogo->para.dis_mode)
	{
		case DISP_MODE_ORIGIN:
		op_info.dst_rect.x=op_info.src_rect.x;
		op_info.dst_rect.y=op_info.src_rect.y;
		op_info.dst_rect.w=op_info.src_rect.w;
		op_info.dst_rect.h=op_info.src_rect.h;
		break;
		case DISP_MODE_CENTER: //maybe offset is useful
		op_info.dst_rect.x=	(plogo->dev->vinfo->width - plogo->parser->logo_pic_info.width)>>1;
		op_info.dst_rect.y=(plogo->dev->vinfo->height - plogo->parser->logo_pic_info.height)>>1;
		op_info.dst_rect.w=op_info.src_rect.w;
		op_info.dst_rect.h=op_info.src_rect.h;
		break;
		case DISP_MODE_FULL_SCREEN:
		op_info.dst_rect.x=0;
		op_info.dst_rect.y=0;
		op_info.dst_rect.w=plogo->dev->vinfo->width;
		op_info.dst_rect.h=plogo->dev->vinfo->height;
	
		break;	
	}
	if(strcmp(plogo->parser->name,"bmp")==0)
	{
		screen_mem_start = plogo->platform_res[plogo->para.output_dev_type].mem_start;
		screen_size=plogo->dev->vinfo->width*plogo->dev->vinfo->height*(plogo->parser->decoder.bmp.color_depth>>3);
		ge2d_config.src_dst_type=plogo->dev->idx?ALLOC_OSD1:ALLOC_OSD0;
		ge2d_config.alu_const_color=0x000000ff;
		ge2d_config.src_format=GE2D_FORMAT_S24_RGB;
		ge2d_config.src_planes[0].addr = screen_mem_start+screen_size;
		ge2d_config.src_planes[0].w = plogo->parser->logo_pic_info.width;
		ge2d_config.src_planes[0].h = plogo->parser->logo_pic_info.height;
		context=dev_ge2d_setup(&ge2d_config);
		if(NULL==context) return -OUTPUT_DEV_SETUP_FAIL;
		amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"logo setup ge2d device OK\n");
		plogo->dev->ge2d_context=context;
	}
	else if(strcmp(plogo->parser->name,"jpg")==0)
	{// transfer from video layer to osd layer.
		amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"transfer from video layer to osd layer\n");
		ge2d_config.src_dst_type=plogo->dev->idx?ALLOC_OSD1:ALLOC_OSD0;
		ge2d_config.alu_const_color=0x000000ff;
		canvas_index=plogo->parser->decoder.jpg.out_canvas_index;
		if(plogo->parser->logo_pic_info.color_info==24)//we only support this format
		{
			ge2d_config.src_format=GE2D_FORMAT_M24_YUV420;
			ge2d_config.dst_format=GE2D_FORMAT_S24_RGB;
			canvas_read(canvas_index&0xff,&canvas);
			if(canvas.addr==0) return FAIL;
			ge2d_config.src_planes[0].addr = canvas.addr;
			ge2d_config.src_planes[0].w = canvas.width;
			ge2d_config.src_planes[0].h = canvas.height;
			amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"Y:[0x%x][%d*%d]\n",(u32)canvas.addr,canvas.width,canvas.height);
			canvas_read(canvas_index>>8&0xff,&canvas);
			if(canvas.addr==0) return FAIL;
			ge2d_config.src_planes[1].addr = canvas.addr;
			ge2d_config.src_planes[1].w = canvas.width;
			ge2d_config.src_planes[1].h = canvas.height;
			amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"U:[0x%x][%d*%d]\n",(u32)canvas.addr,canvas.width,canvas.height);	
			canvas_read(canvas_index>>16&0xff,&canvas);
			if(canvas.addr==0) return FAIL;
			ge2d_config.src_planes[2].addr =  canvas.addr;
			ge2d_config.src_planes[2].w = canvas.width;
			ge2d_config.src_planes[2].h = canvas.height;
			amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"V:[0x%x][%d*%d]\n",(u32)canvas.addr,canvas.width,canvas.height);
			context=dev_ge2d_setup(&ge2d_config);		
		}else{
			amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"can't transfer unsupported jpg format\n");	
			return FAIL;
		}
	}else
	{
		amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"unsupported logo picture format format\n");	
		return FAIL ;
	}
	amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"blit==src:%d-%d-%d-%d\t",op_info.src_rect.x,op_info.src_rect.y,op_info.src_rect.w,op_info.src_rect.h);
	amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"dst:%d-%d-%d-%d\n",op_info.dst_rect.x,op_info.dst_rect.y,op_info.dst_rect.w,op_info.dst_rect.h);	
	amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"move logo pic completed\n");
	
	dev_ge2d_cmd(context,CMD_STRETCH_BLIT,&op_info);

	if(plogo->para.progress) //need progress.
	{
		kernel_thread(thread_progress, plogo, 0);
	}
	return SUCCESS;	
}
int dev_osd_setup(void)
{
	register_logo_output_dev(&output_osd0);
	register_logo_output_dev(&output_osd1);
	return SUCCESS;
}

vmode_t get_current_cvbs_vmode(void)
{
	return cvbsmode_hdmionly;
}

#ifdef CONFIG_AM_HDMI_ONLY
vmode_t get_current_hdmi_vmode(void)
{
	return hdmimode_hdmionly;
}
#endif

static int __init get_cvbs_mode(char *str)
{
    if(strncmp("480", str, 3) == 0){
        cvbsmode_hdmionly = VMODE_480CVBS;
    }else if(strncmp("576", str, 3) == 0){
        cvbsmode_hdmionly = VMODE_576CVBS;
    }else{
	cvbsmode_hdmionly = VMODE_480CVBS;
    }
    printk("kernel get cvbsmode form uboot is %s\n", str);
    return 1;
}
__setup("cvbsmode=", get_cvbs_mode);

#ifdef CONFIG_AM_HDMI_ONLY
static int __init get_hdmi_mode(char *str)
{
    u32 i;
    for(i = 0; i<PARA_HDMI_ONLY; i++){
	if (strcmp(hdmi_only_info[i].name, str) == 0){
		hdmimode_hdmionly = hdmi_only_info[i].info;
		break;
	}
   }

   if(i == PARA_HDMI_ONLY){
	hdmimode_hdmionly = VMODE_1080P;
   }
   printk("kernel get hdmimode form uboot is %s\n", str);
   return 1;
}
__setup("hdmimode=", get_hdmi_mode);
#endif
