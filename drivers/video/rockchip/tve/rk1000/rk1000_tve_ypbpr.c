/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/ctype.h>
#include <linux/string.h>
#include "rk1000_tve.h"

static const struct fb_videomode rk1000_ypbpr_mode[] = {
	{"YPBPR480P", 60, 720, 480, 27000000, 70, 6, 30, 9, 62, 6, 0, 0, 0},
	{"YPBPR576P", 50, 720, 576, 27000000, 74, 6, 39, 5, 64, 5, 0, 0, 0},
	{"YPBPR720P@50", 50, 1280, 720, 74250000, 660, 0, 20, 5, 40,
	 5, 0, 0, 0},
	{"YPbPR720P@60", 60, 1280, 720, 74250000, 330, 0, 20, 5, 40,
	 5, 0, 0, 0},
};

static struct rk1000_monspecs ypbpr_monspecs;

int rk1000_tv_ypbpr480_init(void)
{
	unsigned char tv_encoder_regs[] = {0x00, 0x00, 0x40, 0x08, 0x00,
					   0x02, 0x17, 0x0A, 0x0A};
	unsigned char tv_encoder_control_regs[] = {0x00};
	int i;
	int ret;

	for (i = 0; i < sizeof(tv_encoder_regs); i++) {
		ret = rk1000_tv_write_block(i, tv_encoder_regs + i, 1);
		if (ret < 0) {
			pr_err("rk1000_tv_write_block err!\n");
			return ret;
		}
	}
	for (i = 0; i < sizeof(tv_encoder_control_regs); i++) {
		ret = rk1000_control_write_block(i+3,
						 tv_encoder_control_regs+i,
						 1);
		if (ret < 0) {
			pr_err("rk1000_control_write_block err!\n");
			return ret;
		}
	}
	return 0;
}

int rk1000_tv_ypbpr576_init(void)
{
	unsigned char tv_encoder_regs[] = {0x06, 0x00, 0x40, 0x08, 0x00,
					   0x01, 0x17, 0x0A, 0x0A};
	unsigned char tv_encoder_control_regs[] = {0x00};
	int i;
	int ret;

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

int rk1000_tv_ypbpr720_50_init(void)
{
	unsigned char tv_encoder_regs[] = {0x06, 0x00, 0x40, 0x08,
					   0x00, 0x13, 0x17, 0x0A, 0x0A};
	unsigned char tv_encoder_control_regs[] = {0x00};
	int i;
	int ret;

	for (i = 0; i < sizeof(tv_encoder_regs); i++) {
		ret = rk1000_tv_write_block(i, tv_encoder_regs+i, 1);
		if (ret < 0) {
			pr_err("rk1000_tv_write_block err!\n");
			return ret;
		}
	}

	for (i = 0; i < sizeof(tv_encoder_control_regs); i++) {
		ret = rk1000_control_write_block(i+3,
						 tv_encoder_control_regs+i,
						 1);
		if (ret < 0) {
			pr_err("rk1000_control_write_block err!\n");
			return ret;
		}
	}
	return 0;
}

int rk1000_tv_ypbpr720_60_init(void)
{
	unsigned char tv_encoder_regs[] = {0x06, 0x00, 0x40, 0x08, 0x00,
					   0x17, 0x17, 0x0A, 0x0A};
	unsigned char tv_encoder_control_regs[] = {0x00};
	int i;
	int ret;

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

static int rk1000_ypbpr_set_enable(struct rk_display_device *device,
				   int enable)
{
	if (ypbpr_monspecs.suspend)
		return 0;
	if (ypbpr_monspecs.enable != enable ||
	    ypbpr_monspecs.mode_set != rk1000_tve.mode) {
		if (enable == 0 && ypbpr_monspecs.enable) {
			ypbpr_monspecs.enable = 0;
			rk1000_tv_standby(RK1000_TVOUT_YPBPR);
		} else if (enable == 1) {
			rk1000_switch_fb(ypbpr_monspecs.mode,
					 ypbpr_monspecs.mode_set);
			ypbpr_monspecs.enable = 1;
		}
	}
	return 0;
}

static int rk1000_ypbpr_get_enable(struct rk_display_device *device)
{
	return ypbpr_monspecs.enable;
}

static int rk1000_ypbpr_get_status(struct rk_display_device *device)
{
	if (rk1000_tve.mode > TVOUT_CVBS_PAL)
		return 1;
	else
		return 0;
}

static int rk1000_ypbpr_get_modelist(struct rk_display_device *device,
				     struct list_head **modelist)
{
	*modelist = &(ypbpr_monspecs.modelist);
	return 0;
}

static int rk1000_ypbpr_set_mode(struct rk_display_device *device,
				 struct fb_videomode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rk1000_ypbpr_mode); i++) {
		if (fb_mode_is_equal(&rk1000_ypbpr_mode[i], mode)) {
			if ((i + 3) != rk1000_tve.mode) {
				ypbpr_monspecs.mode_set = i + 3;
				ypbpr_monspecs.mode = (struct fb_videomode *)
							&rk1000_ypbpr_mode[i];
			}
			return 0;
		}
	}
	return -1;
}

static int rk1000_ypbpr_get_mode(struct rk_display_device *device,
				 struct fb_videomode *mode)
{
	*mode = *(ypbpr_monspecs.mode);
	return 0;
}

static struct rk_display_ops rk1000_ypbpr_display_ops = {
	.setenable = rk1000_ypbpr_set_enable,
	.getenable = rk1000_ypbpr_get_enable,
	.getstatus = rk1000_ypbpr_get_status,
	.getmodelist = rk1000_ypbpr_get_modelist,
	.setmode = rk1000_ypbpr_set_mode,
	.getmode = rk1000_ypbpr_get_mode,
};

static int rk1000_display_ypbpr_probe(struct rk_display_device *device,
				      void *devdata)
{
	device->owner = THIS_MODULE;
	strcpy(device->type, "YPbPr");
	device->name = "ypbpr";
	device->priority = DISPLAY_PRIORITY_YPBPR;
	device->property = rk1000_tve.property;
	device->priv_data = devdata;
	device->ops = &rk1000_ypbpr_display_ops;
	return 1;
}

static struct rk_display_driver display_rk1000_ypbpr = {
	.probe = rk1000_display_ypbpr_probe,
};

int rk1000_register_display_ypbpr(struct device *parent)
{
	int i;

	memset(&ypbpr_monspecs, 0, sizeof(struct rk1000_monspecs));
	INIT_LIST_HEAD(&ypbpr_monspecs.modelist);
	for (i = 0; i < ARRAY_SIZE(rk1000_ypbpr_mode); i++)
		display_add_videomode(&rk1000_ypbpr_mode[i],
				      &ypbpr_monspecs.modelist);
	if (rk1000_tve.mode > TVOUT_CVBS_PAL) {
		ypbpr_monspecs.mode = (struct fb_videomode *)
				     &(rk1000_ypbpr_mode[rk1000_tve.mode - 3]);
		ypbpr_monspecs.mode_set = rk1000_tve.mode;
	} else {
		ypbpr_monspecs.mode = (struct fb_videomode *)
					&(rk1000_ypbpr_mode[3]);
		ypbpr_monspecs.mode_set = TVOUT_YPBPR_1280X720P_60;
	}
	ypbpr_monspecs.ddev = rk_display_device_register(&display_rk1000_ypbpr,
							 parent, NULL);
	rk1000_tve.ypbpr = &ypbpr_monspecs;
	if (rk1000_tve.mode > TVOUT_CVBS_PAL)
		rk_display_device_enable(ypbpr_monspecs.ddev);
	return 0;
}
