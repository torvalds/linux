#include "logo.h"
#include <linux/wait.h>
#include "logo_dev_vid.h"
 #include	"amlogo_log.h"
#include <linux/amlogic/amlog.h>
static  logo_output_dev_t   output_vid={
	.idx=LOGO_DEV_VID,
	.hw_initialized=0,	
	.op={
		.init=vid_init,
		.transfer=vid_transfer,
		.enable=vid_enable,
		.deinit=vid_deinit,
		},
};

static int vid_init(logo_object_t *plogo)
{
	if(plogo->para.output_dev_type==output_vid.idx)
	{
		set_current_vmode(plogo->para.vout_mode);
		output_vid.vinfo=get_current_vinfo();
		plogo->dev=&output_vid;
		plogo->dev->window.x=0;
		plogo->dev->window.y=0;
		plogo->dev->window.w=plogo->dev->vinfo->width;
		plogo->dev->window.h=plogo->dev->vinfo->height;
		plogo->dev->output_dev.vid.mem_start=plogo->platform_res[LOGO_DEV_VID].mem_start;
		plogo->dev->output_dev.vid.mem_end=plogo->platform_res[LOGO_DEV_VID].mem_end;
		amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"display on video layer\n");
		return OUTPUT_DEV_FOUND;
	}
	return OUTPUT_DEV_UNFOUND;
}
static  int  vid_enable(int  enable)
{
	return SUCCESS;
}
static int vid_deinit(void)
{
	return SUCCESS;
}
static  int  vid_transfer(logo_object_t *plogo)
{
	amlog_mask_level(LOG_MASK_DEVICE,LOG_LEVEL_LOW,"start video transfer\n");;
	SET_MPEG_REG_MASK(VPP_MISC, VPP_VD1_PREBLEND | VPP_PREBLEND_EN | VPP_VD1_POSTBLEND|VPP_POSTBLEND_EN); 
	CLEAR_MPEG_REG_MASK(VPP_MISC, VPP_OSD1_POSTBLEND | VPP_OSD2_POSTBLEND);
	return SUCCESS;
}
int dev_vid_setup(void)
{
	register_logo_output_dev(&output_vid);
	return SUCCESS;
}