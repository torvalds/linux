#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/delay.h>
#include "rk1000_tve.h"



static const struct fb_videomode rk1000_cvbs_mode[] = {
	{"NTSC", 60, 720, 480, 27000000, 116, 16, 25, 14, 6, 6, 0, 1, 0},
	{"PAL", 50, 720, 576, 27000000, 126, 12, 37, 6, 6, 6, 0, 1, 0},
};

static struct rk1000_monspecs cvbs_monspecs;
extern int cvbsformat;
static int changeflag;

int rk1000_tv_ntsc_init(void)
{
	unsigned char tv_encoder_regs[] = {0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
	unsigned char tv_encoder_control_regs[] = {0x43, 0x01};
	int i;
	int ret;

	if (cvbsformat >= 0)
		return 0;

	for (i = 0; i < sizeof(tv_encoder_regs); i++) {
		ret = rk1000_tv_write_block(i, tv_encoder_regs + i, 1);
		if (ret < 0) {
			pr_err("rk1000_tv_write_block err!\n");
			return ret;
		}
	}

	for (i = 0; i < sizeof(tv_encoder_control_regs); i++) {
		ret = rk1000_control_write_block(i + 3,
						 tv_encoder_control_regs + i,
						 1);
		if (ret < 0) {
			pr_err("rk1000_control_write_block err!\n");
			return ret;
		}
	}

	return 0;
}

int rk1000_tv_pal_init(void)
{
	unsigned char tv_encoder_regs[] = {0x06, 0x00, 0x00, 0x03, 0x00, 0x00};
	unsigned char tv_encoder_control_regs[] = {0x41, 0x01};
	int i;
	int ret;

	if (cvbsformat >= 0)
		return 0;

	for (i = 0; i < sizeof(tv_encoder_regs); i++) {
		ret = rk1000_tv_write_block(i, tv_encoder_regs+i, 1);
		if (ret < 0) {
			pr_err("rk1000_tv_write_block err!\n");
			return ret;
		}
	}

	for (i = 0; i < sizeof(tv_encoder_control_regs); i++) {
		ret = rk1000_control_write_block(i + 3,
						 tv_encoder_control_regs + i,
						 1);
		if (ret < 0) {
			pr_err("rk1000_control_write_block err!\n");
			return ret;
		}
	}
	return 0;
}

static int rk1000_cvbs_set_enable(struct rk_display_device *device, int enable)
{
	unsigned char val;

	if (cvbs_monspecs.suspend)
		return 0;
	if ((cvbs_monspecs.enable != enable) ||
	    (cvbs_monspecs.mode_set != rk1000_tve.mode)) {
		if ((enable == 0) && cvbs_monspecs.enable) {
			cvbs_monspecs.enable = 0;
			rk1000_tv_standby(RK1000_TVOUT_CVBS);
		} else if (enable == 1) {
			if (cvbsformat >= 0) {
				rk1000_switch_fb(cvbs_monspecs.mode,
						 cvbs_monspecs.mode_set);
				cvbsformat = -1;
			} else{
				val = 0x07;
				rk1000_tv_write_block(0x03, &val, 1);
				rk1000_switch_fb(cvbs_monspecs.mode,
						 cvbs_monspecs.mode_set);
				if (changeflag == 1)
					msleep(600);
				val = 0x03;
				rk1000_tv_write_block(0x03, &val, 1);
			}
			cvbs_monspecs.enable = 1;
			changeflag = 0;
		}
	}
	return 0;
}

static int rk1000_cvbs_get_enable(struct rk_display_device *device)
{
	return cvbs_monspecs.enable;
}

static int rk1000_cvbs_get_status(struct rk_display_device *device)
{
	if (rk1000_tve.mode < TVOUT_YPBPR_720X480P_60)
		return 1;
	else
		return 0;
}

static int rk1000_cvbs_get_modelist(struct rk_display_device *device,
				    struct list_head **modelist)
{
	*modelist = &(cvbs_monspecs.modelist);
	return 0;
}

static int rk1000_cvbs_set_mode(struct rk_display_device *device,
				struct fb_videomode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rk1000_cvbs_mode); i++) {
		if (fb_mode_is_equal(&rk1000_cvbs_mode[i], mode)) {
			if (((i + 1) != rk1000_tve.mode)) {
				cvbs_monspecs.mode_set = i + 1;
				cvbs_monspecs.mode = (struct fb_videomode *)
							&rk1000_cvbs_mode[i];
				changeflag = 1;
			}
			return 0;
		}
	}
	return -1;
}

static int rk1000_cvbs_get_mode(struct rk_display_device *device,
				struct fb_videomode *mode)
{
	*mode = *(cvbs_monspecs.mode);
	return 0;
}

static struct rk_display_ops rk1000_cvbs_display_ops = {
	.setenable = rk1000_cvbs_set_enable,
	.getenable = rk1000_cvbs_get_enable,
	.getstatus = rk1000_cvbs_get_status,
	.getmodelist = rk1000_cvbs_get_modelist,
	.setmode = rk1000_cvbs_set_mode,
	.getmode = rk1000_cvbs_get_mode,
};

static int rk1000_display_cvbs_probe(struct rk_display_device *device,
				     void *devdata)
{
	device->owner = THIS_MODULE;
	strcpy(device->type, "TV");
	device->name = "cvbs";
	device->priority = DISPLAY_PRIORITY_TV;
	device->property = rk1000_tve.property;
	device->priv_data = devdata;
	device->ops = &rk1000_cvbs_display_ops;
	return 1;
}

static struct rk_display_driver display_rk1000_cvbs = {
	.probe = rk1000_display_cvbs_probe,
};

int rk1000_register_display_cvbs(struct device *parent)
{
	int i;

	memset(&cvbs_monspecs, 0, sizeof(struct rk1000_monspecs));
	INIT_LIST_HEAD(&cvbs_monspecs.modelist);
	for (i = 0; i < ARRAY_SIZE(rk1000_cvbs_mode); i++)
		display_add_videomode(&rk1000_cvbs_mode[i],
				      &cvbs_monspecs.modelist);
	if (rk1000_tve.mode < TVOUT_YPBPR_720X480P_60) {
		cvbs_monspecs.mode = (struct fb_videomode *)
				      &(rk1000_cvbs_mode[rk1000_tve.mode - 1]);
		cvbs_monspecs.mode_set = rk1000_tve.mode;
	} else {
		cvbs_monspecs.mode = (struct fb_videomode *)
					&(rk1000_cvbs_mode[0]);
		cvbs_monspecs.mode_set = TVOUT_CVBS_NTSC;
	}
	cvbs_monspecs.ddev = rk_display_device_register(&display_rk1000_cvbs,
							parent, NULL);
	rk1000_tve.cvbs = &cvbs_monspecs;
	if (rk1000_tve.mode < TVOUT_YPBPR_720X480P_60)
		rk_display_device_enable(cvbs_monspecs.ddev);
	return 0;
}
