// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2023 Rockchip Electronics Co., Ltd

#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include "cam-tb-setup.h"

static u32 rk_cam_w;
static u32 rk_cam_h;
static u32 rk_cam_hdr;
static u32 rk_cam_fps;

static int __init rk_cam_w_setup(char *str)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(str, 0, &val);
	if (!ret)
		rk_cam_w = (u32)val;
	else
		pr_err("get rk_cam_w fail\n");

	return 0;
}

u32 get_rk_cam_w(void)
{
	return rk_cam_w;
}
EXPORT_SYMBOL(get_rk_cam_w);

static int __init rk_cam_h_setup(char *str)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(str, 0, &val);
	if (!ret)
		rk_cam_h = (u32)val;
	else
		pr_err("get rk_cam_h fail\n");

	return 0;
}

u32 get_rk_cam_h(void)
{
	return rk_cam_h;
}
EXPORT_SYMBOL(get_rk_cam_h);

static int __init rk_cam_hdr_setup(char *str)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(str, 0, &val);
	if (!ret)
		rk_cam_hdr = (u32)val;
	else
		pr_err("get rk_cam_hdr fail\n");

	return 0;
}

u32 get_rk_cam_hdr(void)
{
	return rk_cam_hdr;
}
EXPORT_SYMBOL(get_rk_cam_hdr);

static int __init __maybe_unused rk_cam_fps_setup(char *str)
{
	int ret = 0;
	unsigned long val = 0;

	ret = kstrtoul(str, 0, &val);
	if (!ret)
		rk_cam_fps = (u32)val;
	else
		pr_err("get rk_cam_fps fail\n");

	return 0;
}

u32 get_rk_cam_fps(void)
{
	return rk_cam_fps;
}
EXPORT_SYMBOL(get_rk_cam_fps);

__setup("rk_cam_w=", rk_cam_w_setup);
__setup("rk_cam_h=", rk_cam_h_setup);
__setup("rk_cam_hdr=", rk_cam_hdr_setup);
__setup("rk_cam_fps=", rk_cam_fps_setup);
