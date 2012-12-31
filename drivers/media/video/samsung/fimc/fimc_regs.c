/* linux/drivers/media/video/samsung/fimc/fimc_regs.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Register interface file for Samsung Camera Interface (FIMC) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <linux/io.h>
#include <mach/map.h>
#include <plat/regs-fimc.h>
#include <plat/fimc.h>

#include "fimc.h"

/* struct fimc_limit: Limits for FIMC */
struct fimc_limit fimc40_limits[FIMC_DEVICES] = {
	{
		.pre_dst_w	= 3264,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 3264,
		.trg_h_rot	= 1280,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1280,
	}, {
		.pre_dst_w	= 1280,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 1280,
		.trg_h_rot	= 8192,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 768,
	}, {
		.pre_dst_w	= 1440,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 1440,
		.trg_h_rot	= 0,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 0,
	},
};

struct fimc_limit fimc43_limits[FIMC_DEVICES] = {
	{
		.pre_dst_w	= 4224,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 4224,
		.trg_h_rot	= 1920,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1920,
	}, {
		.pre_dst_w	= 4224,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 4224,
		.trg_h_rot	= 1920,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1920,
	}, {
		.pre_dst_w	= 1920,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 1920,
		.trg_h_rot	= 1280,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1280,
	},
};

struct fimc_limit fimc50_limits[FIMC_DEVICES] = {
	{
		.pre_dst_w	= 4224,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 4224,
		.trg_h_rot	= 1920,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1920,
	}, {
		.pre_dst_w	= 4224,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 4224,
		.trg_h_rot	= 1920,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1920,
	}, {
		.pre_dst_w	= 1920,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 1920,
		.trg_h_rot	= 1280,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1280,
	},
};

struct fimc_limit fimc51_limits[FIMC_DEVICES] = {
	{
		.pre_dst_w	= 4224,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 4224,
		.trg_h_rot	= 1920,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1920,
	}, {
		.pre_dst_w	= 4224,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 4224,
		.trg_h_rot	= 1920,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1920,
	}, {
		.pre_dst_w	= 4224,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 4224,
		.trg_h_rot	= 1920,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1920,
	}, {

		.pre_dst_w	= 1920,
		.bypass_w	= 8192,
		.trg_h_no_rot	= 1920,
		.trg_h_rot	= 1280,
		.real_w_no_rot	= 8192,
		.real_h_rot	= 1280,
	},
};

int fimc_hwset_camera_source(struct fimc_control *ctrl)
{
	struct s3c_platform_camera *cam = ctrl->cam;
	u32 cfg = 0;

	/* for now, we support only ITU601 8 bit mode */
	cfg |= S3C_CISRCFMT_ITU601_8BIT;
	cfg |= cam->order422;

	if (cam->type == CAM_TYPE_ITU)
		cfg |= cam->fmt;

	if (ctrl->is.sd) {
		cfg |= S3C_CISRCFMT_SOURCEHSIZE(ctrl->is.fmt.width);
		cfg |= S3C_CISRCFMT_SOURCEVSIZE(ctrl->is.fmt.height);
	} else {
		cfg |= S3C_CISRCFMT_SOURCEHSIZE(cam->width);
		cfg |= S3C_CISRCFMT_SOURCEVSIZE(cam->height);
	}

	writel(cfg, ctrl->regs + S3C_CISRCFMT);

	return 0;
}

int fimc_hwset_camera_change_source(struct fimc_control *ctrl)
{
	struct s3c_platform_camera *cam = ctrl->cam;
	u32 cfg = 0;

	/* for now, we support only ITU601 8 bit mode */
	cfg |= S3C_CISRCFMT_ITU601_8BIT;
	cfg |= cam->order422;

	if (cam->type == CAM_TYPE_ITU)
		cfg |= cam->fmt;

	if (ctrl->is.sd) {
		cfg |= S3C_CISRCFMT_SOURCEHSIZE(ctrl->is.zoom_in_width);
		cfg |= S3C_CISRCFMT_SOURCEVSIZE(ctrl->is.zoom_in_height);
	} else {
		cfg |= S3C_CISRCFMT_SOURCEHSIZE(cam->width);
		cfg |= S3C_CISRCFMT_SOURCEVSIZE(cam->height);
	}

	writel(cfg, ctrl->regs + S3C_CISRCFMT);

	return 0;
}

int fimc_hwset_enable_irq(struct fimc_control *ctrl, int overflow, int level)
{
	u32 cfg = readl(ctrl->regs + S3C_CIGCTRL);

	cfg &= ~(S3C_CIGCTRL_IRQ_OVFEN | S3C_CIGCTRL_IRQ_LEVEL);
	cfg |= S3C_CIGCTRL_IRQ_ENABLE;

	if (overflow)
		cfg |= S3C_CIGCTRL_IRQ_OVFEN;

	if (level)
		cfg |= S3C_CIGCTRL_IRQ_LEVEL;
	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	return 0;
}

int fimc_hwset_disable_irq(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIGCTRL);

	cfg &= ~(S3C_CIGCTRL_IRQ_OVFEN | S3C_CIGCTRL_IRQ_ENABLE);
	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	return 0;
}

int fimc_hwset_clear_irq(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIGCTRL);

	cfg |= S3C_CIGCTRL_IRQ_CLR;

	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	return 0;
}

int fimc_hwset_output_area_size(struct fimc_control *ctrl, u32 size)
{
	u32 cfg = 0;

	cfg = S3C_CITAREA_TARGET_AREA(size);

	writel(cfg, ctrl->regs + S3C_CITAREA);

	return 0;
}


int fimc_hwset_image_effect(struct fimc_control *ctrl)
{
	u32 cfg = 0;

	if (ctrl->fe.ie_on) {
		if (ctrl->fe.ie_after_sc)
			cfg |= S3C_CIIMGEFF_IE_SC_AFTER;

		cfg |= S3C_CIIMGEFF_FIN(ctrl->fe.fin);

		if (ctrl->fe.fin == FIMC_EFFECT_FIN_ARBITRARY_CBCR) {
			cfg |= S3C_CIIMGEFF_PAT_CB(ctrl->fe.pat_cb)
					| S3C_CIIMGEFF_PAT_CR(ctrl->fe.pat_cr);
		}

		cfg |= S3C_CIIMGEFF_IE_ENABLE;
	}

	writel(cfg, ctrl->regs + S3C_CIIMGEFF);

	return 0;
}

static void fimc_reset_cfg(struct fimc_control *ctrl)
{
	int i;
	u32 cfg[][2] = {
		{ 0x018, 0x00000000 }, { 0x01c, 0x00000000 },
		{ 0x020, 0x00000000 }, { 0x024, 0x00000000 },
		{ 0x028, 0x00000000 }, { 0x02c, 0x00000000 },
		{ 0x030, 0x00000000 }, { 0x034, 0x00000000 },
		{ 0x038, 0x00000000 }, { 0x03c, 0x00000000 },
		{ 0x040, 0x00000000 }, { 0x044, 0x00000000 },
		{ 0x048, 0x00000000 }, { 0x04c, 0x00000000 },
		{ 0x050, 0x00000000 }, { 0x054, 0x00000000 },
		{ 0x058, 0x18000000 }, { 0x05c, 0x00000000 },
		{ 0x064, 0x00000000 },
		{ 0x0c0, 0x00000000 }, { 0x0c4, 0xffffffff },
		{ 0x0d0, 0x00100080 }, { 0x0d4, 0x00000000 },
		{ 0x0d8, 0x00000000 }, { 0x0dc, 0x00000000 },
		{ 0x0f8, 0x00000000 }, { 0x0fc, 0x04000000 },
		{ 0x168, 0x00000000 }, { 0x16c, 0x00000000 },
		{ 0x170, 0x00000000 }, { 0x174, 0x00000000 },
		{ 0x178, 0x00000000 }, { 0x17c, 0x00000000 },
		{ 0x180, 0x00000000 }, { 0x184, 0x00000000 },
		{ 0x188, 0x00000000 }, { 0x18c, 0x00000000 },
		{ 0x194, 0x0000001e },
	};

	for (i = 0; i < sizeof(cfg) / 8; i++)
		writel(cfg[i][1], ctrl->regs + cfg[i][0]);
}

int fimc_hwset_reset(struct fimc_control *ctrl)
{
	u32 cfg = 0;

	cfg = readl(ctrl->regs + S3C_CISRCFMT);
	cfg |= S3C_CISRCFMT_ITU601_8BIT;
	writel(cfg, ctrl->regs + S3C_CISRCFMT);

	/* s/w reset */
	cfg = readl(ctrl->regs + S3C_CIGCTRL);
	cfg |= (S3C_CIGCTRL_SWRST);
	writel(cfg, ctrl->regs + S3C_CIGCTRL);
	mdelay(1);

	cfg = readl(ctrl->regs + S3C_CIGCTRL);
	cfg &= ~S3C_CIGCTRL_SWRST;
	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	/* in case of ITU656, CISRCFMT[31] should be 0 */
	if ((ctrl->cap != NULL) && (ctrl->cam->fmt == ITU_656_YCBCR422_8BIT)) {
		cfg = readl(ctrl->regs + S3C_CISRCFMT);
		cfg &= ~S3C_CISRCFMT_ITU601_8BIT;
		writel(cfg, ctrl->regs + S3C_CISRCFMT);
	}

	fimc_reset_cfg(ctrl);

	return 0;
}

int fimc_hwset_clksrc(struct fimc_control *ctrl, int src_clk)
{
	u32 cfg = readl(ctrl->regs + S3C_MISC_FIMC);
	cfg &= ~S3C_CLKSRC_HCLK_MASK;

	if (src_clk == FIMC_HCLK)
		cfg |= S3C_CLKSRC_HCLK;
	else if (src_clk == FIMC_SCLK)
		cfg |= S3C_CLKSRC_SCLK;

	writel(cfg, ctrl->regs + S3C_MISC_FIMC);
	return 0;
}

int fimc_hwget_overflow_state(struct fimc_control *ctrl)
{
	u32 cfg, status, flag;

	status = readl(ctrl->regs + S3C_CISTATUS);
	flag = S3C_CISTATUS_OVFIY | S3C_CISTATUS_OVFICB | S3C_CISTATUS_OVFICR;

	if (status & flag) {
		cfg = readl(ctrl->regs + S3C_CIWDOFST);
		cfg |= (S3C_CIWDOFST_CLROVFIY | S3C_CIWDOFST_CLROVFICB |
			S3C_CIWDOFST_CLROVFICR);
		writel(cfg, ctrl->regs + S3C_CIWDOFST);

		cfg = readl(ctrl->regs + S3C_CIWDOFST);
		cfg &= ~(S3C_CIWDOFST_CLROVFIY | S3C_CIWDOFST_CLROVFICB |
			S3C_CIWDOFST_CLROVFICR);
		writel(cfg, ctrl->regs + S3C_CIWDOFST);

		printk(KERN_INFO "FIMC%d overflow is occured status 0x%x\n",
				ctrl->id, status);
		return 1;
	}

	return 0;
}

int fimc_hwset_camera_offset(struct fimc_control *ctrl)
{
	struct s3c_platform_camera *cam = ctrl->cam;
	struct v4l2_rect *rect = &cam->window;
	u32 cfg, h1, h2, v1, v2;

	if (!cam) {
		fimc_err("%s: no active camera\n", __func__);
		return -ENODEV;
	}

	h1 = rect->left;
	h2 = cam->width - rect->width - rect->left;
	v1 = rect->top;
	v2 = cam->height - rect->height - rect->top;

	cfg = readl(ctrl->regs + S3C_CIWDOFST);
	cfg &= ~(S3C_CIWDOFST_WINHOROFST_MASK | S3C_CIWDOFST_WINVEROFST_MASK);
	cfg |= S3C_CIWDOFST_WINHOROFST(h1);
	cfg |= S3C_CIWDOFST_WINVEROFST(v1);
	cfg |= S3C_CIWDOFST_WINOFSEN;
	writel(cfg, ctrl->regs + S3C_CIWDOFST);

	cfg = 0;
	cfg |= S3C_CIWDOFST2_WINHOROFST2(h2);
	cfg |= S3C_CIWDOFST2_WINVEROFST2(v2);
	writel(cfg, ctrl->regs + S3C_CIWDOFST2);

	return 0;
}

int fimc_hwset_camera_polarity(struct fimc_control *ctrl)
{
	struct s3c_platform_camera *cam = ctrl->cam;
	u32 cfg;

	if (!cam) {
		fimc_err("%s: no active camera\n", __func__);
		return -ENODEV;
	}

	cfg = readl(ctrl->regs + S3C_CIGCTRL);

	cfg &= ~(S3C_CIGCTRL_INVPOLPCLK | S3C_CIGCTRL_INVPOLVSYNC |
		 S3C_CIGCTRL_INVPOLHREF | S3C_CIGCTRL_INVPOLHSYNC);

	if (cam->inv_pclk)
		cfg |= S3C_CIGCTRL_INVPOLPCLK;

	if (cam->inv_vsync)
		cfg |= S3C_CIGCTRL_INVPOLVSYNC;

	if (cam->inv_href)
		cfg |= S3C_CIGCTRL_INVPOLHREF;

	if (cam->inv_hsync)
		cfg |= S3C_CIGCTRL_INVPOLHSYNC;

	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	return 0;
}

int fimc40_hwset_camera_type(struct fimc_control *ctrl)
{
	struct s3c_platform_camera *cam = ctrl->cam;
	u32 cfg;

	if (!cam) {
		fimc_err("%s: no active camera\n", __func__);
		return -ENODEV;
	}

	cfg = readl(ctrl->regs + S3C_CIGCTRL);
	cfg &= ~(S3C_CIGCTRL_TESTPATTERN_MASK | S3C_CIGCTRL_SELCAM_ITU_MASK |
		S3C_CIGCTRL_SELCAM_FIMC_MASK);

	/* Interface selection */
	if (cam->type == CAM_TYPE_MIPI) {
		cfg |= S3C_CIGCTRL_SELCAM_FIMC_MIPI;
		writel(cam->fmt, ctrl->regs + S3C_CSIIMGFMT);
	} else if (cam->type == CAM_TYPE_ITU) {
		if (cam->id == CAMERA_PAR_A)
			cfg |= S3C_CIGCTRL_SELCAM_ITU_A;
		else
			cfg |= S3C_CIGCTRL_SELCAM_ITU_B;
		/* switch to ITU interface */
		cfg |= S3C_CIGCTRL_SELCAM_FIMC_ITU;
	} else {
		fimc_err("%s: invalid camera bus type selected\n",
			__func__);
		return -EINVAL;
	}

	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	return 0;
}

int fimc43_hwset_camera_type(struct fimc_control *ctrl)
{
	struct s3c_platform_camera *cam = ctrl->cam;
	u32 cfg;

	if (!cam) {
		fimc_err("%s: no active camera\n", __func__);
		return -ENODEV;
	}

	cfg = readl(ctrl->regs + S3C_CIGCTRL);
	cfg &= ~(S3C_CIGCTRL_TESTPATTERN_MASK | S3C_CIGCTRL_SELCAM_ITU_MASK |
		S3C_CIGCTRL_SELCAM_MIPI_MASK | S3C_CIGCTRL_SELCAM_FIMC_MASK |
		S3C_CIGCTRL_SELWB_CAMIF_MASK);

	/* Interface selection */
	if (cam->id == CAMERA_WB) {
		cfg |= S3C_CIGCTRL_SELWB_CAMIF_WRITEBACK;
	} else if (cam->type == CAM_TYPE_MIPI) {
		cfg |= S3C_CIGCTRL_SELCAM_FIMC_MIPI;

		/* C110/V210 Support only MIPI A support */
		cfg |= S3C_CIGCTRL_SELCAM_MIPI_A;

		/* FIXME: Temporary MIPI CSIS Data 32 bit aligned */
		if (ctrl->cap->fmt.pixelformat == V4L2_PIX_FMT_JPEG)
			writel((MIPI_USER_DEF_PACKET_1 | (0x1 << 8)),
					ctrl->regs + S3C_CSIIMGFMT);
		else
			writel(cam->fmt | (0x1 << 8), ctrl->regs + S3C_CSIIMGFMT);
	} else if (cam->type == CAM_TYPE_ITU) {
		if (cam->id == CAMERA_PAR_A)
			cfg |= S3C_CIGCTRL_SELCAM_ITU_A;
		else
			cfg |= S3C_CIGCTRL_SELCAM_ITU_B;
		/* switch to ITU interface */
		cfg |= S3C_CIGCTRL_SELCAM_FIMC_ITU;
	} else {
		fimc_err("%s: invalid camera bus type selected\n", __func__);
		return -EINVAL;
	}

	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	return 0;
}

int fimc51_hwset_camera_type(struct fimc_control *ctrl)
{
	struct s3c_platform_camera *cam = ctrl->cam;
	u32 cfg;

	if (!cam) {
		fimc_err("%s: no active camera\n", __func__);
		return -ENODEV;
	}

	cfg = readl(ctrl->regs + S3C_CIGCTRL);
	cfg &= ~(S3C_CIGCTRL_TESTPATTERN_MASK | S3C_CIGCTRL_SELCAM_ITU_MASK |
		S3C_CIGCTRL_SELCAM_MIPI_MASK | S3C_CIGCTRL_SELCAM_FIMC_MASK |
		S3C_CIGCTRL_SELWB_CAMIF_MASK | S3C_CIGCTRL_SELWRITEBACK_MASK);

	/* Interface selection */
	if (cam->id == CAMERA_WB) {
		cfg |= S3C_CIGCTRL_SELWB_CAMIF_WRITEBACK;
		cfg |= S3C_CIGCTRL_SELWRITEBACK_A;
	} else if (cam->id == CAMERA_WB_B || cam->use_isp) {
		cfg |= S3C_CIGCTRL_SELWB_CAMIF_WRITEBACK;
		cfg |= S3C_CIGCTRL_SELWRITEBACK_B;
	} else if (cam->type == CAM_TYPE_MIPI) {
		cfg |= S3C_CIGCTRL_SELCAM_FIMC_MIPI;

		/* V310 Support MIPI A/B support */
		if (cam->id == CAMERA_CSI_C)
			cfg |= S3C_CIGCTRL_SELCAM_MIPI_A;
		else
			cfg |= S3C_CIGCTRL_SELCAM_MIPI_B;

		/* FIXME: Temporary MIPI CSIS Data 32 bit aligned */
		if (ctrl->cap->fmt.pixelformat == V4L2_PIX_FMT_JPEG)
			writel((MIPI_USER_DEF_PACKET_1 | (0x1 << 8)),
					ctrl->regs + S3C_CSIIMGFMT);
		else
			writel(cam->fmt | (0x1 << 8), ctrl->regs + S3C_CSIIMGFMT);
	} else if (cam->type == CAM_TYPE_ITU) {
		if (cam->id == CAMERA_PAR_A)
			cfg |= S3C_CIGCTRL_SELCAM_ITU_A;
		else
			cfg |= S3C_CIGCTRL_SELCAM_ITU_B;
		/* switch to ITU interface */
		cfg |= S3C_CIGCTRL_SELCAM_FIMC_ITU;
	} else {
		fimc_err("%s: invalid camera bus type selected\n", __func__);
		return -EINVAL;
	}

	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	return 0;
}
int fimc_hwset_camera_type(struct fimc_control *ctrl)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	switch (pdata->hw_ver) {
	case 0x40:
		fimc40_hwset_camera_type(ctrl);
		break;
	case 0x43:
	case 0x45:
		fimc43_hwset_camera_type(ctrl);
		break;
	case 0x51:
		fimc51_hwset_camera_type(ctrl);
		break;
	default:
		fimc43_hwset_camera_type(ctrl);
		break;
	}

	return 0;
}


int fimc_hwset_jpeg_mode(struct fimc_control *ctrl, bool enable)
{
	u32 cfg;
	cfg = readl(ctrl->regs + S3C_CIGCTRL);

	if (enable)
		cfg |= S3C_CIGCTRL_CAM_JPEG;
	else
		cfg &= ~S3C_CIGCTRL_CAM_JPEG;

	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	return 0;
}

int fimc_hwset_output_size(struct fimc_control *ctrl, int width, int height)
{
	u32 cfg = readl(ctrl->regs + S3C_CITRGFMT);

	cfg &= ~(S3C_CITRGFMT_TARGETH_MASK | S3C_CITRGFMT_TARGETV_MASK);

	cfg |= S3C_CITRGFMT_TARGETHSIZE(width);
	cfg |= S3C_CITRGFMT_TARGETVSIZE(height);

	writel(cfg, ctrl->regs + S3C_CITRGFMT);

	return 0;
}

int fimc_hwset_output_colorspace(struct fimc_control *ctrl, u32 pixelformat)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	u32 cfg;

	if (pdata->hw_ver != 0x40) {
		if (pixelformat == V4L2_PIX_FMT_YUV444) {
			cfg = readl(ctrl->regs + S3C_CIEXTEN);
			cfg |= S3C_CIEXTEN_YUV444_OUT;
			writel(cfg, ctrl->regs + S3C_CIEXTEN);

			return 0;
		} else {
			cfg = readl(ctrl->regs + S3C_CIEXTEN);
			cfg &= ~S3C_CIEXTEN_YUV444_OUT;
			writel(cfg, ctrl->regs + S3C_CIEXTEN);
		}
	}

	cfg = readl(ctrl->regs + S3C_CITRGFMT);
	cfg &= ~S3C_CITRGFMT_OUTFORMAT_MASK;

	switch (pixelformat) {
	case V4L2_PIX_FMT_JPEG:
		break;
	case V4L2_PIX_FMT_RGB565: /* fall through */
	case V4L2_PIX_FMT_RGB32:
		cfg |= S3C_CITRGFMT_OUTFORMAT_RGB;
		break;

	case V4L2_PIX_FMT_YUYV:		/* fall through */
	case V4L2_PIX_FMT_UYVY:		/* fall through */
	case V4L2_PIX_FMT_VYUY:		/* fall through */
	case V4L2_PIX_FMT_YVYU:
		cfg |= S3C_CITRGFMT_OUTFORMAT_YCBCR422_1PLANE;
		break;

	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_NV61:		/* fall through */
	case V4L2_PIX_FMT_YUV422P:
		cfg |= S3C_CITRGFMT_OUTFORMAT_YCBCR422;
		break;

	case V4L2_PIX_FMT_YUV420:	/* fall through */
	case V4L2_PIX_FMT_YVU420:	/* fall through */
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV12M:	/* fall through */
	case V4L2_PIX_FMT_NV12T:	/* fall through */
	case V4L2_PIX_FMT_NV21:
		cfg |= S3C_CITRGFMT_OUTFORMAT_YCBCR420;
		break;

	default:
		fimc_err("%s: invalid pixel format : %d\n",
				__func__, pixelformat);
		break;
	}

	writel(cfg, ctrl->regs + S3C_CITRGFMT);

	return 0;
}

int fimc_hwset_output_rot_flip(struct fimc_control *ctrl, u32 rot, u32 flip)
{
	u32 cfg, val;

	cfg = readl(ctrl->regs + S3C_CITRGFMT);
	cfg &= ~S3C_CITRGFMT_FLIP_MASK;
	cfg &= ~S3C_CITRGFMT_OUTROT90_CLOCKWISE;

	val = fimc_mapping_rot_flip(rot, flip);

	if (val & FIMC_ROT)
		cfg |= S3C_CITRGFMT_OUTROT90_CLOCKWISE;

	if (val & FIMC_XFLIP)
		cfg |= S3C_CITRGFMT_FLIP_X_MIRROR;

	if (val & FIMC_YFLIP)
		cfg |= S3C_CITRGFMT_FLIP_Y_MIRROR;

	writel(cfg, ctrl->regs + S3C_CITRGFMT);

	return 0;
}

int fimc_hwset_output_area(struct fimc_control *ctrl, u32 width, u32 height)
{
	u32 cfg = 0;

	cfg = S3C_CITAREA_TARGET_AREA(width * height);
	writel(cfg, ctrl->regs + S3C_CITAREA);

	return 0;
}

int fimc_hwset_enable_lastirq(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIOCTRL);

	cfg |= S3C_CIOCTRL_LASTIRQ_ENABLE;
	writel(cfg, ctrl->regs + S3C_CIOCTRL);

	return 0;
}

int fimc_hwset_disable_lastirq(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIOCTRL);

	cfg &= ~S3C_CIOCTRL_LASTIRQ_ENABLE;
	writel(cfg, ctrl->regs + S3C_CIOCTRL);

	return 0;
}

int fimc_hwset_enable_lastend(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIOCTRL);

	cfg |= S3C_CIOCTRL_LASTENDEN;
	writel(cfg, ctrl->regs + S3C_CIOCTRL);

	return 0;
}

int fimc_hwset_disable_lastend(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIOCTRL);

	cfg &= ~S3C_CIOCTRL_LASTENDEN;
	writel(cfg, ctrl->regs + S3C_CIOCTRL);

	return 0;
}

int fimc_hwset_prescaler(struct fimc_control *ctrl, struct fimc_scaler *sc)
{
	u32 cfg = 0, shfactor;

	shfactor = 10 - (sc->hfactor + sc->vfactor);

	cfg |= S3C_CISCPRERATIO_SHFACTOR(shfactor);
	cfg |= S3C_CISCPRERATIO_PREHORRATIO(sc->pre_hratio);
	cfg |= S3C_CISCPRERATIO_PREVERRATIO(sc->pre_vratio);

	writel(cfg, ctrl->regs + S3C_CISCPRERATIO);

	cfg = 0;
	cfg |= S3C_CISCPREDST_PREDSTWIDTH(sc->pre_dst_width);
	cfg |= S3C_CISCPREDST_PREDSTHEIGHT(sc->pre_dst_height);

	writel(cfg, ctrl->regs + S3C_CISCPREDST);

	return 0;
}

int fimc_hwset_output_address(struct fimc_control *ctrl,
			      struct fimc_buf_set *bs, int id)
{
	writel(bs->base[FIMC_ADDR_Y], ctrl->regs + S3C_CIOYSA(id));

	if (ctrl->cap && ctrl->cap->fmt.pixelformat == V4L2_PIX_FMT_YVU420) {
		writel(bs->base[FIMC_ADDR_CR], ctrl->regs + S3C_CIOCBSA(id));
		writel(bs->base[FIMC_ADDR_CB], ctrl->regs + S3C_CIOCRSA(id));
	} else {
		writel(bs->base[FIMC_ADDR_CB], ctrl->regs + S3C_CIOCBSA(id));
		writel(bs->base[FIMC_ADDR_CR], ctrl->regs + S3C_CIOCRSA(id));
	}

	return 0;
}

int fimc_hwset_output_yuv(struct fimc_control *ctrl, u32 pixelformat)
{
	u32 cfg;

	cfg = readl(ctrl->regs + S3C_CIOCTRL);
	cfg &= ~(S3C_CIOCTRL_ORDER2P_MASK | S3C_CIOCTRL_ORDER422_MASK |
		S3C_CIOCTRL_YCBCR_PLANE_MASK);

	switch (pixelformat) {
	/* 1 plane formats */
	case V4L2_PIX_FMT_YUYV:
		cfg |= S3C_CIOCTRL_ORDER422_YCBYCR;
		break;

	case V4L2_PIX_FMT_UYVY:
		cfg |= S3C_CIOCTRL_ORDER422_CBYCRY;
		break;

	case V4L2_PIX_FMT_VYUY:
		cfg |= S3C_CIOCTRL_ORDER422_CRYCBY;
		break;

	case V4L2_PIX_FMT_YVYU:
		cfg |= S3C_CIOCTRL_ORDER422_YCRYCB;
		break;

	/* 2 plane formats */
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV12M:	/* fall through */
	case V4L2_PIX_FMT_NV12T:	/* fall through */
	case V4L2_PIX_FMT_NV16:
		cfg |= S3C_CIOCTRL_ORDER2P_LSB_CBCR;
		cfg |= S3C_CIOCTRL_YCBCR_2PLANE;
		break;

	case V4L2_PIX_FMT_NV21:		/* fall through */
	case V4L2_PIX_FMT_NV61:
		cfg |= S3C_CIOCTRL_ORDER2P_LSB_CRCB;
		cfg |= S3C_CIOCTRL_YCBCR_2PLANE;
		break;

	/* 3 plane formats */
	case V4L2_PIX_FMT_YUV422P:	/* fall through */
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		cfg |= S3C_CIOCTRL_YCBCR_3PLANE;
		break;
	}

	writel(cfg, ctrl->regs + S3C_CIOCTRL);

	return 0;
}

int fimc_hwset_output_scan(struct fimc_control *ctrl,
			   struct v4l2_pix_format *fmt)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	u32 cfg;

	/* nothing to do: FIMC40 not supported interlaced and weave output */
	if (pdata->hw_ver == 0x40)
		return 0;

	cfg = readl(ctrl->regs + S3C_CISCCTRL);
	cfg &= ~S3C_CISCCTRL_SCAN_MASK;

	if (fmt->field == V4L2_FIELD_INTERLACED ||
		fmt->field == V4L2_FIELD_INTERLACED_TB)
		cfg |= S3C_CISCCTRL_INTERLACE;
	else
		cfg |= S3C_CISCCTRL_PROGRESSIVE;

	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	cfg = readl(ctrl->regs + S3C_CIOCTRL);
	cfg &= ~S3C_CIOCTRL_WEAVE_MASK;

	if ((ctrl->cap) && (fmt->field == V4L2_FIELD_INTERLACED_TB))
		cfg |= S3C_CIOCTRL_WEAVE_OUT;

	writel(cfg, ctrl->regs + S3C_CIOCTRL);

	return 0;
}

int fimc_hwset_input_rot(struct fimc_control *ctrl, u32 rot, u32 flip)
{
	u32 cfg, val;

	cfg = readl(ctrl->regs + S3C_CITRGFMT);
	cfg &= ~S3C_CITRGFMT_INROT90_CLOCKWISE;

	val = fimc_mapping_rot_flip(rot, flip);

	if (val & FIMC_ROT)
		cfg |= S3C_CITRGFMT_INROT90_CLOCKWISE;

	writel(cfg, ctrl->regs + S3C_CITRGFMT);

	return 0;
}

int fimc40_hwset_scaler(struct fimc_control *ctrl, struct fimc_scaler *sc)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);

	cfg &= ~(S3C_CISCCTRL_SCALERBYPASS |
		S3C_CISCCTRL_SCALEUP_H | S3C_CISCCTRL_SCALEUP_V |
		S3C_CISCCTRL_MAIN_V_RATIO_MASK |
		S3C_CISCCTRL_MAIN_H_RATIO_MASK |
		S3C_CISCCTRL_CSCR2Y_WIDE |
		S3C_CISCCTRL_CSCY2R_WIDE);

	if (ctrl->range == FIMC_RANGE_WIDE)
		cfg |= (S3C_CISCCTRL_CSCR2Y_WIDE | S3C_CISCCTRL_CSCY2R_WIDE);

	if (sc->bypass)
		cfg |= S3C_CISCCTRL_SCALERBYPASS;

	if (sc->scaleup_h)
		cfg |= S3C_CISCCTRL_SCALEUP_H;

	if (sc->scaleup_v)
		cfg |= S3C_CISCCTRL_SCALEUP_V;

	cfg |= S3C_CISCCTRL_MAINHORRATIO(sc->main_hratio);
	cfg |= S3C_CISCCTRL_MAINVERRATIO(sc->main_vratio);

	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	return 0;
}

int fimc43_hwset_scaler(struct fimc_control *ctrl, struct fimc_scaler *sc)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);
	u32 cfg_ext = readl(ctrl->regs + S3C_CIEXTEN);

	cfg &= ~(S3C_CISCCTRL_SCALERBYPASS |
		S3C_CISCCTRL_SCALEUP_H | S3C_CISCCTRL_SCALEUP_V |
		S3C_CISCCTRL_MAIN_V_RATIO_MASK |
		S3C_CISCCTRL_MAIN_H_RATIO_MASK |
		S3C_CISCCTRL_CSCR2Y_WIDE |
		S3C_CISCCTRL_CSCY2R_WIDE);

	if (ctrl->range == FIMC_RANGE_WIDE)
		cfg |= (S3C_CISCCTRL_CSCR2Y_WIDE | S3C_CISCCTRL_CSCY2R_WIDE);

	if (sc->bypass)
		cfg |= S3C_CISCCTRL_SCALERBYPASS;

	if (sc->scaleup_h)
		cfg |= S3C_CISCCTRL_SCALEUP_H;

	if (sc->scaleup_v)
		cfg |= S3C_CISCCTRL_SCALEUP_V;

	cfg |= S3C_CISCCTRL_MAINHORRATIO(sc->main_hratio);
	cfg |= S3C_CISCCTRL_MAINVERRATIO(sc->main_vratio);

	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	cfg_ext &= ~S3C_CIEXTEN_MAINHORRATIO_EXT_MASK;
	cfg_ext &= ~S3C_CIEXTEN_MAINVERRATIO_EXT_MASK;

	cfg_ext |= S3C_CIEXTEN_MAINHORRATIO_EXT(sc->main_hratio);
	cfg_ext |= S3C_CIEXTEN_MAINVERRATIO_EXT(sc->main_vratio);

	writel(cfg_ext, ctrl->regs + S3C_CIEXTEN);

	return 0;
}

int fimc50_hwset_scaler(struct fimc_control *ctrl, struct fimc_scaler *sc)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);
	u32 cfg_ext = readl(ctrl->regs + S3C_CIEXTEN);

	cfg &= ~(S3C_CISCCTRL_SCALERBYPASS |
		S3C_CISCCTRL_SCALEUP_H | S3C_CISCCTRL_SCALEUP_V |
		S3C_CISCCTRL_MAIN_V_RATIO_MASK |
		S3C_CISCCTRL_MAIN_H_RATIO_MASK |
		S3C_CISCCTRL_CSCR2Y_WIDE |
		S3C_CISCCTRL_CSCY2R_WIDE);

	if (ctrl->range == FIMC_RANGE_WIDE)
		cfg |= (S3C_CISCCTRL_CSCR2Y_WIDE | S3C_CISCCTRL_CSCY2R_WIDE);

	if (sc->bypass)
		cfg |= S3C_CISCCTRL_SCALERBYPASS;

	if (sc->scaleup_h)
		cfg |= S3C_CISCCTRL_SCALEUP_H;

	if (sc->scaleup_v)
		cfg |= S3C_CISCCTRL_SCALEUP_V;

	cfg |= S3C_CISCCTRL_MAINHORRATIO((sc->main_hratio >> 6));
	cfg |= S3C_CISCCTRL_MAINVERRATIO((sc->main_vratio >> 6));

	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	cfg_ext &= ~S3C_CIEXTEN_MAINHORRATIO_EXT_MASK;
	cfg_ext &= ~S3C_CIEXTEN_MAINVERRATIO_EXT_MASK;

	cfg_ext |= S3C_CIEXTEN_MAINHORRATIO_EXT(sc->main_hratio);
	cfg_ext |= S3C_CIEXTEN_MAINVERRATIO_EXT(sc->main_vratio);

	writel(cfg_ext, ctrl->regs + S3C_CIEXTEN);

	return 0;
}

int fimc_hwset_scaler(struct fimc_control *ctrl, struct fimc_scaler *sc)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	switch (pdata->hw_ver) {
	case 0x40:
		fimc40_hwset_scaler(ctrl, sc);
		break;
	case 0x43:
	case 0x45:
		fimc43_hwset_scaler(ctrl, sc);
		break;
	case 0x50:
	case 0x51:
		fimc50_hwset_scaler(ctrl, sc);
		break;
	default:
		fimc43_hwset_scaler(ctrl, sc);
		break;
	}

	return 0;
}


int fimc_hwset_scaler_bypass(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);

	cfg |= S3C_CISCCTRL_SCALERBYPASS;

	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	return 0;
}

int fimc_hwset_enable_lcdfifo(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);

	cfg |= S3C_CISCCTRL_LCDPATHEN_FIFO;
	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	return 0;
}

int fimc_hwset_disable_lcdfifo(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);

	cfg &= ~S3C_CISCCTRL_LCDPATHEN_FIFO;
	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	return 0;
}

int fimc_hwget_frame_count(struct fimc_control *ctrl)
{
	return S3C_CISTATUS_GET_FRAME_COUNT(readl(ctrl->regs + S3C_CISTATUS));
}

int fimc_hwget_frame_end(struct fimc_control *ctrl)
{
	unsigned long timeo = jiffies;
	u32 cfg;

	timeo += 20;	/* waiting for 100ms */

	cfg = readl(ctrl->regs + S3C_CISTATUS);
	cfg &= ~S3C_CISTATUS_FRAMEEND;
	writel(cfg, ctrl->regs + S3C_CISTATUS);
	while (time_before(jiffies, timeo)) {
		cfg = readl(ctrl->regs + S3C_CISTATUS);
		if (S3C_CISTATUS_GET_FRAME_END(cfg)) {
			cfg &= ~S3C_CISTATUS_FRAMEEND;
			writel(cfg, ctrl->regs + S3C_CISTATUS);
			break;
		}
		cond_resched();
	}

	return 0;
}

int fimc_hwget_last_frame_end(struct fimc_control *ctrl)
{
	unsigned long timeo = jiffies;
	u32 cfg;

	timeo += 20;	/* waiting for 100ms */
	while (time_before(jiffies, timeo)) {
		cfg = readl(ctrl->regs + S3C_CISTATUS);

		if (S3C_CISTATUS_GET_LAST_CAPTURE_END(cfg)) {
			cfg &= ~S3C_CISTATUS_LASTCAPTUREEND;
			writel(cfg, ctrl->regs + S3C_CISTATUS);
			break;
		}
		cond_resched();
	}

	return 0;
}

int fimc_hwset_start_scaler(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);

	cfg |= S3C_CISCCTRL_SCALERSTART;
	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	return 0;
}

int fimc_hwset_stop_scaler(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);

	cfg &= ~S3C_CISCCTRL_SCALERSTART;
	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	return 0;
}

int fimc_hwset_input_rgb(struct fimc_control *ctrl, u32 pixelformat)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);
	cfg &= ~S3C_CISCCTRL_INRGB_FMT_RGB_MASK;

	if (pixelformat == V4L2_PIX_FMT_RGB32)
		cfg |= S3C_CISCCTRL_INRGB_FMT_RGB888;
	else if (pixelformat == V4L2_PIX_FMT_RGB565)
		cfg |= S3C_CISCCTRL_INRGB_FMT_RGB565;

	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	return 0;
}

int fimc_hwset_intput_field(struct fimc_control *ctrl, enum v4l2_field field)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	u32 cfg;

	if (pdata->hw_ver == 0x40)
		return 0;

	cfg = readl(ctrl->regs + S3C_MSCTRL);
	cfg &= ~S3C_MSCTRL_FIELD_MASK;

	if (field == V4L2_FIELD_NONE)
		cfg |= S3C_MSCTRL_FIELD_NORMAL;
	else if (field == V4L2_FIELD_INTERLACED_TB)
		cfg |= S3C_MSCTRL_FIELD_WEAVE;

	writel(cfg, ctrl->regs + S3C_MSCTRL);

	return 0;
}

int fimc_hwset_output_rgb(struct fimc_control *ctrl, u32 pixelformat)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);
	cfg &= ~S3C_CISCCTRL_OUTRGB_FMT_RGB_MASK;

	if (pixelformat == V4L2_PIX_FMT_RGB32)
		cfg |= S3C_CISCCTRL_OUTRGB_FMT_RGB888;
	else if (pixelformat == V4L2_PIX_FMT_RGB565)
		cfg |= S3C_CISCCTRL_OUTRGB_FMT_RGB565;

	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	return 0;
}

int fimc_hwset_ext_rgb(struct fimc_control *ctrl, int enable)
{
	u32 cfg = readl(ctrl->regs + S3C_CISCCTRL);
	cfg &= ~S3C_CISCCTRL_EXTRGB_EXTENSION;

	if (enable)
		cfg |= S3C_CISCCTRL_EXTRGB_EXTENSION;

	writel(cfg, ctrl->regs + S3C_CISCCTRL);

	return 0;
}

int fimc_hwset_enable_capture(struct fimc_control *ctrl, u32 bypass)
{
	u32 cfg = readl(ctrl->regs + S3C_CIIMGCPT);
	cfg &= ~S3C_CIIMGCPT_IMGCPTEN_SC;
	cfg |= S3C_CIIMGCPT_IMGCPTEN;

	if (!bypass)
		cfg |= S3C_CIIMGCPT_IMGCPTEN_SC;

	writel(cfg, ctrl->regs + S3C_CIIMGCPT);

	return 0;
}

int fimc_hwset_disable_capture(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIIMGCPT);

	cfg &= ~(S3C_CIIMGCPT_IMGCPTEN_SC | S3C_CIIMGCPT_IMGCPTEN);

	writel(cfg, ctrl->regs + S3C_CIIMGCPT);

	return 0;
}

void fimc_wait_disable_capture(struct fimc_control *ctrl)
{
	unsigned long timeo = jiffies + 40; /* timeout of 200 ms */
	u32 cfg;
	if (!ctrl || !ctrl->cap)
		return;
	while (time_before(jiffies, timeo)) {
		cfg = readl(ctrl->regs + S3C_CISTATUS);

		if (0 == (cfg & S3C_CISTATUS_IMGCPTEN)	\
			&& 0 == (cfg & S3C_CISTATUS_IMGCPTENSC)	\
			&& 0 == (cfg & S3C_CISTATUS_SCALERSTART))
			break;
		msleep(5);
	}
	fimc_info2("IMGCPTEN: Wait time = %d ms\n"	\
		, jiffies_to_msecs(jiffies - timeo + 20));
	return;
}

int fimc_hwset_input_address(struct fimc_control *ctrl, dma_addr_t *base)
{
	writel(base[FIMC_ADDR_Y], ctrl->regs + S3C_CIIYSA0);
	writel(base[FIMC_ADDR_CB], ctrl->regs + S3C_CIICBSA0);
	writel(base[FIMC_ADDR_CR], ctrl->regs + S3C_CIICRSA0);

	return 0;
}

int fimc_hwset_enable_autoload(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIREAL_ISIZE);

	cfg |= S3C_CIREAL_ISIZE_AUTOLOAD_ENABLE;

	writel(cfg, ctrl->regs + S3C_CIREAL_ISIZE);

	return 0;
}

int fimc_hwset_disable_autoload(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIREAL_ISIZE);

	cfg &= ~S3C_CIREAL_ISIZE_AUTOLOAD_ENABLE;

	writel(cfg, ctrl->regs + S3C_CIREAL_ISIZE);

	return 0;
}

int fimc_hwset_real_input_size(struct fimc_control *ctrl, u32 width, u32 height)
{
	u32 cfg = readl(ctrl->regs + S3C_CIREAL_ISIZE);
	cfg &= ~(S3C_CIREAL_ISIZE_HEIGHT_MASK | S3C_CIREAL_ISIZE_WIDTH_MASK);

	cfg |= S3C_CIREAL_ISIZE_WIDTH(width);
	cfg |= S3C_CIREAL_ISIZE_HEIGHT(height);

	writel(cfg, ctrl->regs + S3C_CIREAL_ISIZE);

	return 0;
}

int fimc_hwset_addr_change_enable(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIREAL_ISIZE);

	cfg &= ~S3C_CIREAL_ISIZE_ADDR_CH_DISABLE;

	writel(cfg, ctrl->regs + S3C_CIREAL_ISIZE);

	return 0;
}

int fimc_hwset_addr_change_disable(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIREAL_ISIZE);

	cfg |= S3C_CIREAL_ISIZE_ADDR_CH_DISABLE;

	writel(cfg, ctrl->regs + S3C_CIREAL_ISIZE);

	return 0;
}

int fimc_hwset_input_burst_cnt(struct fimc_control *ctrl, u32 cnt)
{
	u32 cfg = readl(ctrl->regs + S3C_MSCTRL);
	cfg &= ~S3C_MSCTRL_BURST_CNT_MASK;

	if (cnt > 4)
		cnt = 4;
	else if (cnt == 0)
		cnt = 4;

	cfg |= S3C_MSCTRL_SUCCESSIVE_COUNT(cnt);
	writel(cfg, ctrl->regs + S3C_MSCTRL);

	return 0;
}

int fimc_hwset_input_colorspace(struct fimc_control *ctrl, u32 pixelformat)
{
	u32 cfg = readl(ctrl->regs + S3C_MSCTRL);
	cfg &= ~S3C_MSCTRL_INFORMAT_RGB;

	/* Color format setting */
	switch (pixelformat) {
	case V4L2_PIX_FMT_YUV420:	/* fall through */
	case V4L2_PIX_FMT_YVU420:	/* fall through */
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV21:		/* fall through */
	case V4L2_PIX_FMT_NV12T:
		cfg |= S3C_MSCTRL_INFORMAT_YCBCR420;
		break;
	case V4L2_PIX_FMT_YUYV:		/* fall through */
	case V4L2_PIX_FMT_UYVY: 	/* fall through */
	case V4L2_PIX_FMT_YVYU:		/* fall through */
	case V4L2_PIX_FMT_VYUY:
		cfg |= S3C_MSCTRL_INFORMAT_YCBCR422_1PLANE;
		break;
	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_NV61:
		cfg |= S3C_MSCTRL_INFORMAT_YCBCR422;
		break;
	case V4L2_PIX_FMT_RGB565:	/* fall through */
	case V4L2_PIX_FMT_RGB32:
		cfg |= S3C_MSCTRL_INFORMAT_RGB;
		break;
	default:
		fimc_err("%s: Invalid pixelformt : %d\n",
				__func__, pixelformat);
		return -EINVAL;
	}

	writel(cfg, ctrl->regs + S3C_MSCTRL);

	return 0;
}

int fimc_hwset_input_yuv(struct fimc_control *ctrl, u32 pixelformat)
{
	u32 cfg = readl(ctrl->regs + S3C_MSCTRL);
	cfg &= ~(S3C_MSCTRL_ORDER2P_SHIFT_MASK | S3C_MSCTRL_C_INT_IN_2PLANE |
						S3C_MSCTRL_ORDER422_YCBYCR);

	switch (pixelformat) {
	case V4L2_PIX_FMT_YUV420:	/* fall through */
	case V4L2_PIX_FMT_YVU420:
		cfg |= S3C_MSCTRL_C_INT_IN_3PLANE;
		break;
	case V4L2_PIX_FMT_YUYV:		/* fall through */
		cfg |= S3C_MSCTRL_ORDER422_YCBYCR;
		break;
	case V4L2_PIX_FMT_UYVY:
		cfg |= S3C_MSCTRL_ORDER422_CBYCRY;
		break;
	case V4L2_PIX_FMT_YVYU:
		cfg |= S3C_MSCTRL_ORDER422_YCRYCB;
		break;
	case V4L2_PIX_FMT_VYUY:
		cfg |= S3C_MSCTRL_ORDER422_CRYCBY;
		break;
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV12T:
	case V4L2_PIX_FMT_NV16:
		cfg |= S3C_MSCTRL_ORDER2P_LSB_CBCR;
		cfg |= S3C_MSCTRL_C_INT_IN_2PLANE;
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV61:
		cfg |= S3C_MSCTRL_ORDER2P_LSB_CRCB;
		cfg |= S3C_MSCTRL_C_INT_IN_2PLANE;
		break;
	case V4L2_PIX_FMT_RGB565:	/* fall through */
	case V4L2_PIX_FMT_RGB32:
		break;
	default:
		fimc_err("%s: Invalid pixelformt : %d\n",
				__func__, pixelformat);
	}

	writel(cfg, ctrl->regs + S3C_MSCTRL);

	return 0;
}

int fimc_hwset_input_flip(struct fimc_control *ctrl, u32 rot, u32 flip)
{
	u32 cfg, val;

	cfg = readl(ctrl->regs + S3C_MSCTRL);
	cfg &= ~(S3C_MSCTRL_FLIP_X_MIRROR | S3C_MSCTRL_FLIP_Y_MIRROR);
	val = fimc_mapping_rot_flip(rot, flip);

	if (val & FIMC_XFLIP)
		cfg |= S3C_MSCTRL_FLIP_X_MIRROR;

	if (val & FIMC_YFLIP)
		cfg |= S3C_MSCTRL_FLIP_Y_MIRROR;

	writel(cfg, ctrl->regs + S3C_MSCTRL);

	return 0;
}

int fimc_hwset_input_source(struct fimc_control *ctrl, enum fimc_input path)
{
	u32 cfg = readl(ctrl->regs + S3C_MSCTRL);
	cfg &= ~S3C_MSCTRL_INPUT_MASK;

	if (path == FIMC_SRC_MSDMA)
		cfg |= S3C_MSCTRL_INPUT_MEMORY;
	else if (path == FIMC_SRC_CAM)
		cfg |= S3C_MSCTRL_INPUT_EXTCAM;

	writel(cfg, ctrl->regs + S3C_MSCTRL);

	return 0;

}

int fimc_hwset_start_input_dma(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_MSCTRL);
	cfg |= S3C_MSCTRL_ENVID;

	writel(cfg, ctrl->regs + S3C_MSCTRL);

	return 0;
}

int fimc_hwset_stop_input_dma(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_MSCTRL);
	cfg &= ~S3C_MSCTRL_ENVID;

	writel(cfg, ctrl->regs + S3C_MSCTRL);

	return 0;
}

void fimc_wait_stop_processing(struct fimc_control *ctrl)
{
	fimc_hwget_frame_end(ctrl);
	fimc_hwget_last_frame_end(ctrl);
}

void fimc_hwset_stop_processing(struct fimc_control *ctrl)
{
	fimc_wait_stop_processing(ctrl);

	fimc_hwset_stop_scaler(ctrl);
	fimc_hwset_disable_capture(ctrl);
	fimc_hwset_stop_input_dma(ctrl);

	/* We need to wait for sometime after processing is stopped.
	 * This is required for obtaining clean buffer for DMA processing. */
	fimc_wait_stop_processing(ctrl);
}

int fimc40_hwset_output_offset(struct fimc_control *ctrl, u32 pixelformat,
			       struct v4l2_rect *bounds,
			       struct v4l2_rect *crop)
{
	u32 cfg_y = 0, cfg_cb = 0, cfg_cr = 0;

	if (!crop->left && !crop->top && (bounds->width == crop->width) &&
		(bounds->height == crop->height))
		return -EINVAL;

	fimc_dbg("%s: left: %d, top: %d, width: %d, height: %d\n",
		__func__, crop->left, crop->top, crop->width, crop->height);

	switch (pixelformat) {
	/* 1 plane, 32 bits per pixel */
	case V4L2_PIX_FMT_RGB32:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left * 4);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		break;

	/* 1 plane, 16 bits per pixel */
	case V4L2_PIX_FMT_YUYV:	/* fall through */
	case V4L2_PIX_FMT_UYVY:	/* fall through */
	case V4L2_PIX_FMT_VYUY:	/* fall through */
	case V4L2_PIX_FMT_YVYU:	/* fall through */
	case V4L2_PIX_FMT_RGB565:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left * 2);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		break;

	/* 2 planes, 16 bits per pixel */
	case V4L2_PIX_FMT_NV16: /* fall through */
	case V4L2_PIX_FMT_NV61:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		cfg_cb |= S3C_CIOCBOFF_HORIZONTAL(crop->left / 2);
		cfg_cb |= S3C_CIOCBOFF_VERTICAL(crop->top / 2);
		break;

	/* 2 planes, 12 bits per pixel */
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV12T:	/* fall through */
	case V4L2_PIX_FMT_NV21:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		cfg_cb |= S3C_CIOCBOFF_HORIZONTAL(crop->left / 4);
		cfg_cb |= S3C_CIOCBOFF_VERTICAL(crop->top / 4);
		break;

	/* 3 planes, 16 bits per pixel */
	case V4L2_PIX_FMT_YUV422P:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		cfg_cb |= S3C_CIOCBOFF_HORIZONTAL(crop->left / 2);
		cfg_cb |= S3C_CIOCBOFF_VERTICAL(crop->top / 2);
		cfg_cr |= S3C_CIOCROFF_HORIZONTAL(crop->left / 2);
		cfg_cr |= S3C_CIOCROFF_VERTICAL(crop->top / 2);
		break;

	/* 3 planes, 12 bits per pixel */
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		cfg_cb |= S3C_CIOCBOFF_HORIZONTAL(crop->left / 4);
		cfg_cb |= S3C_CIOCBOFF_VERTICAL(crop->top / 4);
		cfg_cr |= S3C_CIOCROFF_HORIZONTAL(crop->left / 4);
		cfg_cr |= S3C_CIOCROFF_VERTICAL(crop->top / 4);
		break;

	default:
		break;
	}

	writel(cfg_y, ctrl->regs + S3C_CIOYOFF);
	writel(cfg_cb, ctrl->regs + S3C_CIOCBOFF);
	writel(cfg_cr, ctrl->regs + S3C_CIOCROFF);

	return 0;
}

int fimc50_hwset_output_offset(struct fimc_control *ctrl, u32 pixelformat,
			       struct v4l2_rect *bounds,
			       struct v4l2_rect *crop)
{
	u32 cfg_y = 0, cfg_cb = 0, cfg_cr = 0;

	if (!crop->left && !crop->top && (bounds->width == crop->width) &&
		(bounds->height == crop->height))
		return -EINVAL;

	fimc_dbg("%s: left: %d, top: %d, width: %d, height: %d\n",
		__func__, crop->left, crop->top, crop->width, crop->height);

	switch (pixelformat) {
	/* 1 plane, 32 bits per pixel */
	case V4L2_PIX_FMT_RGB32:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		break;

	/* 1 plane, 16 bits per pixel */
	case V4L2_PIX_FMT_YUYV:	/* fall through */
	case V4L2_PIX_FMT_UYVY:	/* fall through */
	case V4L2_PIX_FMT_VYUY:	/* fall through */
	case V4L2_PIX_FMT_YVYU:	/* fall through */
	case V4L2_PIX_FMT_RGB565:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		break;

	/* 2 planes, 16 bits per pixel */
	case V4L2_PIX_FMT_NV16: /* fall through */
	case V4L2_PIX_FMT_NV61:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		cfg_cb |= S3C_CIOCBOFF_HORIZONTAL(crop->left);
		cfg_cb |= S3C_CIOCBOFF_VERTICAL(crop->top);
		break;

	/* 2 planes, 12 bits per pixel */
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV12M:	/* fall through */
	case V4L2_PIX_FMT_NV12T:	/* fall through */
	case V4L2_PIX_FMT_NV21:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		cfg_cb |= S3C_CIOCBOFF_HORIZONTAL(crop->left);
		cfg_cb |= S3C_CIOCBOFF_VERTICAL(crop->top);
		break;

	/* 3 planes, 16 bits per pixel */
	case V4L2_PIX_FMT_YUV422P:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		cfg_cb |= S3C_CIOCBOFF_HORIZONTAL(crop->left);
		cfg_cb |= S3C_CIOCBOFF_VERTICAL(crop->top);
		cfg_cr |= S3C_CIOCROFF_HORIZONTAL(crop->left);
		cfg_cr |= S3C_CIOCROFF_VERTICAL(crop->top);
		break;

	/* 3 planes, 12 bits per pixel */
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		cfg_y |= S3C_CIOYOFF_HORIZONTAL(crop->left);
		cfg_y |= S3C_CIOYOFF_VERTICAL(crop->top);
		cfg_cb |= S3C_CIOCBOFF_HORIZONTAL(crop->left);
		cfg_cb |= S3C_CIOCBOFF_VERTICAL(crop->top);
		cfg_cr |= S3C_CIOCROFF_HORIZONTAL(crop->left);
		cfg_cr |= S3C_CIOCROFF_VERTICAL(crop->top);
		break;

	default:
		break;
	}

	writel(cfg_y, ctrl->regs + S3C_CIOYOFF);
	writel(cfg_cb, ctrl->regs + S3C_CIOCBOFF);
	writel(cfg_cr, ctrl->regs + S3C_CIOCROFF);

	return 0;
}

int fimc_hwset_output_offset(struct fimc_control *ctrl, u32 pixelformat,
			     struct v4l2_rect *bounds,
			     struct v4l2_rect *crop)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	if (pdata->hw_ver >= 0x50)
		fimc50_hwset_output_offset(ctrl, pixelformat, bounds, crop);
	else
		fimc40_hwset_output_offset(ctrl, pixelformat, bounds, crop);

	return 0;
}

int fimc40_hwset_input_offset(struct fimc_control *ctrl, u32 pixelformat,
			      struct v4l2_rect *bounds,
			      struct v4l2_rect *crop)
{
	u32 cfg_y = 0, cfg_cb = 0;

	if (crop->left || crop->top ||
		(bounds->width != crop->width) ||
		(bounds->height != crop->height)) {
		switch (pixelformat) {
		case V4L2_PIX_FMT_YUYV:		/* fall through */
		case V4L2_PIX_FMT_RGB565:
			cfg_y |= S3C_CIIYOFF_HORIZONTAL(crop->left * 2);
			cfg_y |= S3C_CIIYOFF_VERTICAL(crop->top);
			break;
		case V4L2_PIX_FMT_RGB32:
			cfg_y |= S3C_CIIYOFF_HORIZONTAL(crop->left * 4);
			cfg_y |= S3C_CIIYOFF_VERTICAL(crop->top);
			break;
		case V4L2_PIX_FMT_NV12:		/* fall through */
		case V4L2_PIX_FMT_NV21:		/* fall through */
		case V4L2_PIX_FMT_NV12T:
			cfg_y |= S3C_CIIYOFF_HORIZONTAL(crop->left);
			cfg_y |= S3C_CIIYOFF_VERTICAL(crop->top);
			cfg_cb |= S3C_CIICBOFF_HORIZONTAL(crop->left);
			cfg_cb |= S3C_CIICBOFF_VERTICAL(crop->top / 2);

			break;
		default:
			fimc_err("%s: Invalid pixelformt : %d\n",
					__func__, pixelformat);
		}
	}

	writel(cfg_y, ctrl->regs + S3C_CIIYOFF);
	writel(cfg_cb, ctrl->regs + S3C_CIICBOFF);

	return 0;
}

int fimc50_hwset_input_offset(struct fimc_control *ctrl, u32 pixelformat,
			      struct v4l2_rect *bounds,
			      struct v4l2_rect *crop)
{
	u32 cfg_y = 0, cfg_cb = 0, cfg_cr = 0;

	if (crop->left || crop->top ||
		(bounds->width != crop->width) ||
		(bounds->height != crop->height)) {
		switch (pixelformat) {
		case V4L2_PIX_FMT_YUYV:		/* fall through */
		case V4L2_PIX_FMT_UYVY:		/* fall through */
		case V4L2_PIX_FMT_YVYU:		/* fall through */
		case V4L2_PIX_FMT_VYUY:		/* fall through */
		case V4L2_PIX_FMT_RGB565:
			cfg_y |= S3C_CIIYOFF_HORIZONTAL(crop->left);
			cfg_y |= S3C_CIIYOFF_VERTICAL(crop->top);
			break;
		case V4L2_PIX_FMT_RGB32:
			cfg_y |= S3C_CIIYOFF_HORIZONTAL(crop->left);
			cfg_y |= S3C_CIIYOFF_VERTICAL(crop->top);
			break;
		case V4L2_PIX_FMT_NV12: /* fall through*/
		case V4L2_PIX_FMT_NV21: /* fall through*/
		case V4L2_PIX_FMT_NV12T:
			cfg_y |= S3C_CIIYOFF_HORIZONTAL(crop->left);
			cfg_y |= S3C_CIIYOFF_VERTICAL(crop->top);
			cfg_cb |= S3C_CIICBOFF_HORIZONTAL(crop->left);
			cfg_cb |= S3C_CIICBOFF_VERTICAL(crop->top);
			break;
		case V4L2_PIX_FMT_NV16:		/* fall through */
		case V4L2_PIX_FMT_NV61:
			cfg_y |= S3C_CIIYOFF_HORIZONTAL(crop->left);
			cfg_y |= S3C_CIIYOFF_VERTICAL(crop->top);
			cfg_cb |= S3C_CIICBOFF_HORIZONTAL(crop->left);
			cfg_cb |= S3C_CIICBOFF_VERTICAL(crop->top);
			break;
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YVU420:
			cfg_y |= S3C_CIIYOFF_HORIZONTAL(crop->left);
			cfg_y |= S3C_CIIYOFF_VERTICAL(crop->top);
			cfg_cb |= S3C_CIICBOFF_HORIZONTAL(crop->left);
			cfg_cb |= S3C_CIICBOFF_VERTICAL(crop->top);
			cfg_cr |= S3C_CIICROFF_HORIZONTAL(crop->left);
			cfg_cr |= S3C_CIICROFF_VERTICAL(crop->top);
			break;
		default:
			fimc_err("%s: Invalid pixelformt : %d\n",
					__func__, pixelformat);
		}
	}

	writel(cfg_y, ctrl->regs + S3C_CIIYOFF);
	writel(cfg_cb, ctrl->regs + S3C_CIICBOFF);
	writel(cfg_cr, ctrl->regs + S3C_CIICROFF);

	return 0;
}

int fimc_hwset_input_offset(struct fimc_control *ctrl, u32 pixelformat,
			    struct v4l2_rect *bounds,
			    struct v4l2_rect *crop)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	if (pdata->hw_ver >= 0x50)
		fimc50_hwset_input_offset(ctrl, pixelformat, bounds, crop);
	else
		fimc40_hwset_input_offset(ctrl, pixelformat, bounds, crop);

	return 0;
}

int fimc_hwset_org_input_size(struct fimc_control *ctrl, u32 width, u32 height)
{
	u32 cfg = 0;

	cfg |= S3C_ORGISIZE_HORIZONTAL(width);
	cfg |= S3C_ORGISIZE_VERTICAL(height);

	writel(cfg, ctrl->regs + S3C_ORGISIZE);

	return 0;
}

int fimc_hwset_org_output_size(struct fimc_control *ctrl, u32 width, u32 height)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	u32 cfg = 0;

	cfg |= S3C_ORGOSIZE_HORIZONTAL(width);
	cfg |= S3C_ORGOSIZE_VERTICAL(height);

	writel(cfg, ctrl->regs + S3C_ORGOSIZE);

	if (pdata->hw_ver != 0x40) {
		cfg = readl(ctrl->regs + S3C_CIGCTRL);
		cfg &= ~S3C_CIGCTRL_CSC_MASK;

		if (width >= FIMC_HD_WIDTH)
			cfg |= S3C_CIGCTRL_CSC_ITU709;
		else
			cfg |= S3C_CIGCTRL_CSC_ITU601;

		writel(cfg, ctrl->regs + S3C_CIGCTRL);
	}

	return 0;
}

int fimc_hwset_ext_output_size(struct fimc_control *ctrl, u32 width, u32 height)
{
	u32 cfg = readl(ctrl->regs + S3C_CIEXTEN);

	cfg &= ~S3C_CIEXTEN_TARGETH_EXT_MASK;
	cfg &= ~S3C_CIEXTEN_TARGETV_EXT_MASK;
	cfg |= S3C_CIEXTEN_TARGETH_EXT(width);
	cfg |= S3C_CIEXTEN_TARGETV_EXT(height);

	writel(cfg, ctrl->regs + S3C_CIEXTEN);

	return 0;
}

int fimc_hwset_input_addr_style(struct fimc_control *ctrl, u32 pixelformat)
{
	u32 cfg = readl(ctrl->regs + S3C_CIDMAPARAM);
	cfg &= ~S3C_CIDMAPARAM_R_MODE_MASK;

	if (pixelformat == V4L2_PIX_FMT_NV12T)
		cfg |= S3C_CIDMAPARAM_R_MODE_64X32;
	else
		cfg |= S3C_CIDMAPARAM_R_MODE_LINEAR;

	writel(cfg, ctrl->regs + S3C_CIDMAPARAM);

	return 0;
}

int fimc_hwset_output_addr_style(struct fimc_control *ctrl, u32 pixelformat)
{
	u32 cfg = readl(ctrl->regs + S3C_CIDMAPARAM);
	cfg &= ~S3C_CIDMAPARAM_W_MODE_MASK;

	if (pixelformat == V4L2_PIX_FMT_NV12T)
		cfg |= S3C_CIDMAPARAM_W_MODE_64X32;
	else
		cfg |= S3C_CIDMAPARAM_W_MODE_LINEAR;

	writel(cfg, ctrl->regs + S3C_CIDMAPARAM);

	return 0;
}

int fimc_hw_wait_winoff(struct fimc_control *ctrl)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	u32 cfg = readl(ctrl->regs + S3C_CISTATUS);
	u32 status = S3C_CISTATUS_GET_LCD_STATUS(cfg);
	int i = FIMC_FIFOOFF_CNT;

	if (pdata->hw_ver == 0x40)
		return 0;

	while (status && i--) {
		cfg = readl(ctrl->regs + S3C_CISTATUS);
		status = S3C_CISTATUS_GET_LCD_STATUS(cfg);
	}

	if (i < 1) {
		fimc_err("Fail : %s\n", __func__);
		return -EBUSY;
	} else
		return 0;
}

int fimc_hw_wait_stop_input_dma(struct fimc_control *ctrl)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	u32 cfg = readl(ctrl->regs + S3C_MSCTRL);
	u32 status = S3C_MSCTRL_GET_INDMA_STATUS(cfg);
	int i = FIMC_FIFOOFF_CNT, j = FIMC_FIFOOFF_CNT;

	if (pdata->hw_ver == 0x40)
		return 0;

	while (status && i--) {
		cfg = readl(ctrl->regs + S3C_MSCTRL);
		status = S3C_MSCTRL_GET_INDMA_STATUS(cfg);
	}

	cfg = readl(ctrl->regs + S3C_CISTATUS);
	status = S3C_CISTATUS_GET_ENVID_STATUS(cfg);
	while (status && j--) {
		cfg = readl(ctrl->regs + S3C_CISTATUS);
		status = S3C_CISTATUS_GET_ENVID_STATUS(cfg);
	}

	if ((i < 1) || (j < 1)) {
		fimc_err("Fail : %s\n", __func__);
		return -EBUSY;
	} else {
		return 0;
	}
}

int fimc_hwset_input_lineskip(struct fimc_control *ctrl)
{
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	u32 cfg = 0;

	if (pdata->hw_ver == 0x40)
		return 0;

	cfg = S3C_CIILINESKIP(ctrl->sc.skipline);

	writel(cfg, ctrl->regs + S3C_CIILINESKIP_Y);
	writel(cfg, ctrl->regs + S3C_CIILINESKIP_CB);
	writel(cfg, ctrl->regs + S3C_CIILINESKIP_CR);

	return 0;
}

int fimc_hw_reset_camera(struct fimc_control *ctrl)
{
	u32 cfg = 0;
	cfg = readl(ctrl->regs + S3C_CIGCTRL);
	cfg &= ~S3C_CIGCTRL_CAMRST_A;

	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	cfg = readl(ctrl->regs + S3C_CIGCTRL);
	cfg |= S3C_CIGCTRL_CAMRST_A;

	writel(cfg, ctrl->regs + S3C_CIGCTRL);

	return 0;
}

/* Above FIMC v5.1 */
int fimc_hwset_output_buf_sequence(struct fimc_control *ctrl, u32 shift, u32 enable)
{
	u32 cfg = readl(ctrl->regs + S3C_CIFCNTSEQ);
	u32 mask = 0x00000001 << shift;

	cfg &= (~mask);
	cfg |= (enable << shift);
	writel(cfg, ctrl->regs + S3C_CIFCNTSEQ);
	return 0;
}

int fimc_hwget_output_buf_sequence(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIFCNTSEQ);
	return cfg;
}
/* Above FIMC v5.1 */
int fimc_hw_reset_output_buf_sequence(struct fimc_control *ctrl)
{
	writel(0x0, ctrl->regs + S3C_CIFCNTSEQ);
	return 0;
}

void fimc_hwset_output_buf_sequence_all(struct fimc_control *ctrl, u32 framecnt_seq)
{
	writel(framecnt_seq, ctrl->regs + S3C_CIFCNTSEQ);
}

/* Above FIMC v5.1 */
int fimc_hwget_before_frame_count(struct fimc_control *ctrl)
{
	u32 before = readl(ctrl->regs + S3C_CISTATUS2);
	before &= 0x00001f80; /* [12:7] FrameCnt_before */
	return before >> 7;
}

/* Above FIMC v5.1 */
int fimc_hwget_present_frame_count(struct fimc_control *ctrl)
{
	u32 present = readl(ctrl->regs + S3C_CISTATUS2);
	present &= 0x0000003f; /* [5:0] FrameCnt_present */
	return present >> 0;
}

int fimc_hwget_check_framecount_sequence(struct fimc_control *ctrl, u32 frame)
{
	u32 framecnt_seq = readl(ctrl->regs + S3C_CIFCNTSEQ);
	frame -= 1;
	frame = 0x1 << frame;

	if (framecnt_seq & frame)
		return FIMC_FRAMECNT_SEQ_ENABLE;
	else
		return FIMC_FRAMECNT_SEQ_DISABLE;
}

int fimc_hwset_sysreg_camblk_fimd0_wb(struct fimc_control *ctrl)
{
	u32 camblk_cfg = readl(SYSREG_CAMERA_BLK);

	if (soc_is_exynos4210()) {
		camblk_cfg &= (~(0x3 << 14));
		camblk_cfg |= ctrl->id << 14;
	} else {
		camblk_cfg &= (~(0x3 << 23));
		camblk_cfg |= ctrl->id << 23;
	}

	writel(camblk_cfg, SYSREG_CAMERA_BLK);

	return 0;
}

int fimc_hwset_sysreg_camblk_fimd1_wb(struct fimc_control *ctrl)
{
	u32 camblk_cfg = readl(SYSREG_CAMERA_BLK);

	camblk_cfg &= (~(0x3 << 10));
	camblk_cfg |= ctrl->id << 10;

	writel(camblk_cfg, SYSREG_CAMERA_BLK);

	return 0;
}

int fimc_hwset_sysreg_camblk_isp_wb(struct fimc_control *ctrl)
{
	u32 camblk_cfg = readl(SYSREG_CAMERA_BLK);
	u32 ispblk_cfg = readl(SYSREG_ISP_BLK);
	camblk_cfg = camblk_cfg & (~(0x7 << 20));
	if (ctrl->id == 0)
		camblk_cfg = camblk_cfg | (0x1 << 20);
	else if (ctrl->id == 1)
		camblk_cfg = camblk_cfg | (0x2 << 20);
	else if (ctrl->id == 2)
		camblk_cfg = camblk_cfg | (0x4 << 20);
	else if (ctrl->id == 3)
		camblk_cfg = camblk_cfg | (0x7 << 20); /* FIXME*/
	else
		fimc_err("%s: not supported id : %d\n", __func__, ctrl->id);

	camblk_cfg = camblk_cfg & (~(0x1 << 15));
	writel(camblk_cfg, SYSREG_CAMERA_BLK);
	udelay(1000);
	camblk_cfg = camblk_cfg | (0x1 << 15);
	writel(camblk_cfg, SYSREG_CAMERA_BLK);

	ispblk_cfg = ispblk_cfg & (~(0x1 << 7));
	writel(ispblk_cfg, SYSREG_ISP_BLK);
	udelay(1000);
	ispblk_cfg = ispblk_cfg | (0x1 << 7);
	writel(ispblk_cfg, SYSREG_ISP_BLK);

	return 0;
}

void fimc_hwset_enable_frame_end_irq(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIGCTRL);
	cfg |= S3C_CIGCTRL_IRQ_END_DISABLE;
	writel(cfg, ctrl->regs + S3C_CIGCTRL);
}

void fimc_hwset_disable_frame_end_irq(struct fimc_control *ctrl)
{
	u32 cfg = readl(ctrl->regs + S3C_CIGCTRL);
	cfg &= ~S3C_CIGCTRL_IRQ_END_DISABLE;
	writel(cfg, ctrl->regs + S3C_CIGCTRL);
}

void fimc_reset_status_reg(struct fimc_control *ctrl)
{
	writel(0x0, ctrl->regs + S3C_CISTATUS);
}
