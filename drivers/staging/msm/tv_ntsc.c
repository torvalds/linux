/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/clk.h>

#include "msm_fb.h"
#include "tvenc.h"

#define NTSC_TV_DIMENSION_WIDTH      720
#define NTSC_TV_DIMENSION_HEIGHT     480

static int ntsc_off(struct platform_device *pdev);
static int ntsc_on(struct platform_device *pdev);

static int ntsc_on(struct platform_device *pdev)
{
	uint32 reg = 0;
	int ret = 0;
	struct msm_fb_data_type *mfd;

	mfd = platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	TV_OUT(TV_ENC_CTL, 0);	/* disable TV encoder */

	if (mfd->panel.id == NTSC_M) {
		/* Cr gain 11, Cb gain C6, y_gain 97 */
		TV_OUT(TV_GAIN, 0x0081B697);
	} else {
		/* Cr gain 11, Cb gain C6, y_gain 97 */
		TV_OUT(TV_GAIN, 0x008bc4a3);
		reg |= TVENC_CTL_NTSCJ_MODE;
	}

	TV_OUT(TV_CGMS, 0x0);
	/*  NTSC Timing */
	TV_OUT(TV_SYNC_1, 0x0020009e);
	TV_OUT(TV_SYNC_2, 0x011306B4);
	TV_OUT(TV_SYNC_3, 0x0006000C);
	TV_OUT(TV_SYNC_4, 0x0028020D);
	TV_OUT(TV_SYNC_5, 0x005E02FB);
	TV_OUT(TV_SYNC_6, 0x0006000C);
	TV_OUT(TV_SYNC_7, 0x00000012);
	TV_OUT(TV_BURST_V1, 0x0013020D);
	TV_OUT(TV_BURST_V2, 0x0014020C);
	TV_OUT(TV_BURST_V3, 0x0013020D);
	TV_OUT(TV_BURST_V4, 0x0014020C);
	TV_OUT(TV_BURST_H, 0x00AE00F2);
	TV_OUT(TV_SOL_REQ_ODD, 0x00280208);
	TV_OUT(TV_SOL_REQ_EVEN, 0x00290209);

	reg |= TVENC_CTL_TV_MODE_NTSC_M_PAL60;

	reg |= TVENC_CTL_Y_FILTER_EN |
	    TVENC_CTL_CR_FILTER_EN |
	    TVENC_CTL_CB_FILTER_EN | TVENC_CTL_SINX_FILTER_EN;
#ifdef CONFIG_FB_MSM_TVOUT_SVIDEO
	reg |= TVENC_CTL_S_VIDEO_EN;
#endif

	TV_OUT(TV_LEVEL, 0x00000000);	/* DC offset to 0. */
	TV_OUT(TV_OFFSET, 0x008080f0);

#ifdef CONFIG_FB_MSM_MDP31
	TV_OUT(TV_DAC_INTF, 0x29);
#endif
	TV_OUT(TV_ENC_CTL, reg);

	reg |= TVENC_CTL_ENC_EN;
	TV_OUT(TV_ENC_CTL, reg);

	return ret;
}

static int ntsc_off(struct platform_device *pdev)
{
	TV_OUT(TV_ENC_CTL, 0);	/* disable TV encoder */
	return 0;
}

static int __init ntsc_probe(struct platform_device *pdev)
{
	msm_fb_add_device(pdev);

	return 0;
}

static struct platform_driver this_driver = {
	.probe  = ntsc_probe,
	.driver = {
		.name   = "tv_ntsc",
	},
};

static struct msm_fb_panel_data ntsc_panel_data = {
	.panel_info.xres = NTSC_TV_DIMENSION_WIDTH,
	.panel_info.yres = NTSC_TV_DIMENSION_HEIGHT,
	.panel_info.type = TV_PANEL,
	.panel_info.pdest = DISPLAY_1,
	.panel_info.wait_cycle = 0,
	.panel_info.bpp = 16,
	.panel_info.fb_num = 2,
	.on = ntsc_on,
	.off = ntsc_off,
};

static struct platform_device this_device = {
	.name   = "tv_ntsc",
	.id	= 0,
	.dev	= {
		.platform_data = &ntsc_panel_data,
	}
};

static int __init ntsc_init(void)
{
	int ret;

	ret = platform_driver_register(&this_driver);
	if (!ret) {
		ret = platform_device_register(&this_device);
		if (ret)
			platform_driver_unregister(&this_driver);
	}

	return ret;
}

module_init(ntsc_init);