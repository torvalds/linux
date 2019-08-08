// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#include <linux/delay.h>
#include "dm365_isif.h"
#include "vpfe_mc_capture.h"

#define MAX_WIDTH	4096
#define MAX_HEIGHT	4096

static const unsigned int isif_fmts[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_YUYV10_1X20,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8,
	MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8,
};

#define ISIF_COLPTN_R_Ye	0x0
#define ISIF_COLPTN_Gr_Cy	0x1
#define ISIF_COLPTN_Gb_G	0x2
#define ISIF_COLPTN_B_Mg	0x3

#define ISIF_CCOLP_CP01_0	0
#define ISIF_CCOLP_CP03_2	2
#define ISIF_CCOLP_CP05_4	4
#define ISIF_CCOLP_CP07_6	6
#define ISIF_CCOLP_CP11_0	8
#define ISIF_CCOLP_CP13_2	10
#define ISIF_CCOLP_CP15_4	12
#define ISIF_CCOLP_CP17_6	14

static const u32 isif_sgrbg_pattern =
	ISIF_COLPTN_Gr_Cy <<  ISIF_CCOLP_CP01_0 |
	ISIF_COLPTN_R_Ye  << ISIF_CCOLP_CP03_2 |
	ISIF_COLPTN_B_Mg  << ISIF_CCOLP_CP05_4 |
	ISIF_COLPTN_Gb_G  << ISIF_CCOLP_CP07_6 |
	ISIF_COLPTN_Gr_Cy << ISIF_CCOLP_CP11_0 |
	ISIF_COLPTN_R_Ye  << ISIF_CCOLP_CP13_2 |
	ISIF_COLPTN_B_Mg  << ISIF_CCOLP_CP15_4 |
	ISIF_COLPTN_Gb_G  << ISIF_CCOLP_CP17_6;

static const u32 isif_srggb_pattern =
	ISIF_COLPTN_R_Ye  << ISIF_CCOLP_CP01_0 |
	ISIF_COLPTN_Gr_Cy << ISIF_CCOLP_CP03_2 |
	ISIF_COLPTN_Gb_G  << ISIF_CCOLP_CP05_4 |
	ISIF_COLPTN_B_Mg  << ISIF_CCOLP_CP07_6 |
	ISIF_COLPTN_R_Ye  << ISIF_CCOLP_CP11_0 |
	ISIF_COLPTN_Gr_Cy << ISIF_CCOLP_CP13_2 |
	ISIF_COLPTN_Gb_G  << ISIF_CCOLP_CP15_4 |
	ISIF_COLPTN_B_Mg  << ISIF_CCOLP_CP17_6;

static inline u32 isif_read(void __iomem *base_addr, u32 offset)
{
	return readl(base_addr + offset);
}

static inline void isif_write(void __iomem *base_addr, u32 val, u32 offset)
{
	writel(val, base_addr + offset);
}

static inline u32 isif_merge(void __iomem *base_addr, u32 mask, u32 val,
			     u32 offset)
{
	u32 new_val = (isif_read(base_addr, offset) & ~mask) | (val & mask);

	isif_write(base_addr, new_val, offset);

	return new_val;
}

static void isif_enable_output_to_sdram(struct vpfe_isif_device *isif, int en)
{
	isif_merge(isif->isif_cfg.base_addr, ISIF_SYNCEN_WEN_MASK,
		   en << ISIF_SYNCEN_WEN_SHIFT, SYNCEN);
}

static inline void
isif_regw_lin_tbl(struct vpfe_isif_device *isif, u32 val, u32 offset, int i)
{
	if (!i)
		writel(val, isif->isif_cfg.linear_tbl0_addr + offset);
	else
		writel(val, isif->isif_cfg.linear_tbl1_addr + offset);
}

static void isif_disable_all_modules(struct vpfe_isif_device *isif)
{
	/* disable BC */
	isif_write(isif->isif_cfg.base_addr, 0, CLAMPCFG);
	/* disable vdfc */
	isif_write(isif->isif_cfg.base_addr, 0, DFCCTL);
	/* disable CSC */
	isif_write(isif->isif_cfg.base_addr, 0, CSCCTL);
	/* disable linearization */
	isif_write(isif->isif_cfg.base_addr, 0, LINCFG0);
}

static void isif_enable(struct vpfe_isif_device *isif, int en)
{
	if (!en)
		/* Before disable isif, disable all ISIF modules */
		isif_disable_all_modules(isif);

	/*
	 * wait for next VD. Assume lowest scan rate is 12 Hz. So
	 * 100 msec delay is good enough
	 */
	msleep(100);
	isif_merge(isif->isif_cfg.base_addr, ISIF_SYNCEN_VDHDEN_MASK,
		   en, SYNCEN);
}

/*
 * ISIF helper functions
 */

#define DM365_ISIF_MDFS_OFFSET		15
#define DM365_ISIF_MDFS_MASK		0x1

/* get field id in isif hardware */
enum v4l2_field vpfe_isif_get_fid(struct vpfe_device *vpfe_dev)
{
	struct vpfe_isif_device *isif = &vpfe_dev->vpfe_isif;
	u32 field_status;

	field_status = isif_read(isif->isif_cfg.base_addr, MODESET);
	return (field_status >> DM365_ISIF_MDFS_OFFSET) &
		DM365_ISIF_MDFS_MASK;
}

static int
isif_set_pixel_format(struct vpfe_isif_device *isif, unsigned int pixfmt)
{
	if (isif->formats[ISIF_PAD_SINK].code == MEDIA_BUS_FMT_SGRBG12_1X12) {
		if (pixfmt == V4L2_PIX_FMT_SBGGR16)
			isif->isif_cfg.data_pack = ISIF_PACK_16BIT;
		else if ((pixfmt == V4L2_PIX_FMT_SGRBG10DPCM8) ||
				(pixfmt == V4L2_PIX_FMT_SGRBG10ALAW8))
			isif->isif_cfg.data_pack = ISIF_PACK_8BIT;
		else
			return -EINVAL;

		isif->isif_cfg.bayer.pix_fmt = ISIF_PIXFMT_RAW;
		isif->isif_cfg.bayer.v4l2_pix_fmt = pixfmt;
	} else {
		if (pixfmt == V4L2_PIX_FMT_YUYV)
			isif->isif_cfg.ycbcr.pix_order = ISIF_PIXORDER_YCBYCR;
		else if (pixfmt == V4L2_PIX_FMT_UYVY)
			isif->isif_cfg.ycbcr.pix_order = ISIF_PIXORDER_CBYCRY;
		else
			return -EINVAL;

		isif->isif_cfg.data_pack = ISIF_PACK_8BIT;
		isif->isif_cfg.ycbcr.v4l2_pix_fmt = pixfmt;
	}

	return 0;
}

static int
isif_set_frame_format(struct vpfe_isif_device *isif,
		      enum isif_frmfmt frm_fmt)
{
	if (isif->formats[ISIF_PAD_SINK].code == MEDIA_BUS_FMT_SGRBG12_1X12)
		isif->isif_cfg.bayer.frm_fmt = frm_fmt;
	else
		isif->isif_cfg.ycbcr.frm_fmt = frm_fmt;

	return 0;
}

static int isif_set_image_window(struct vpfe_isif_device *isif)
{
	struct v4l2_rect *win = &isif->crop;

	if (isif->formats[ISIF_PAD_SINK].code == MEDIA_BUS_FMT_SGRBG12_1X12) {
		isif->isif_cfg.bayer.win.top = win->top;
		isif->isif_cfg.bayer.win.left = win->left;
		isif->isif_cfg.bayer.win.width = win->width;
		isif->isif_cfg.bayer.win.height = win->height;
		return 0;
	}
	isif->isif_cfg.ycbcr.win.top = win->top;
	isif->isif_cfg.ycbcr.win.left = win->left;
	isif->isif_cfg.ycbcr.win.width = win->width;
	isif->isif_cfg.ycbcr.win.height = win->height;

	return 0;
}

static int
isif_set_buftype(struct vpfe_isif_device *isif, enum isif_buftype buf_type)
{
	if (isif->formats[ISIF_PAD_SINK].code == MEDIA_BUS_FMT_SGRBG12_1X12)
		isif->isif_cfg.bayer.buf_type = buf_type;
	else
		isif->isif_cfg.ycbcr.buf_type = buf_type;

	return 0;
}

/* configure format in isif hardware */
static int
isif_config_format(struct vpfe_device *vpfe_dev, unsigned int pad)
{
	struct vpfe_isif_device *vpfe_isif = &vpfe_dev->vpfe_isif;
	enum isif_frmfmt frm_fmt = ISIF_FRMFMT_INTERLACED;
	struct v4l2_pix_format format;
	int ret = 0;

	v4l2_fill_pix_format(&format, &vpfe_dev->vpfe_isif.formats[pad]);
	mbus_to_pix(&vpfe_dev->vpfe_isif.formats[pad], &format);

	if (isif_set_pixel_format(vpfe_isif, format.pixelformat) < 0) {
		v4l2_err(&vpfe_dev->v4l2_dev,
			 "Failed to set pixel format in isif\n");
		return -EINVAL;
	}

	/* call for s_crop will override these values */
	vpfe_isif->crop.left = 0;
	vpfe_isif->crop.top = 0;
	vpfe_isif->crop.width = format.width;
	vpfe_isif->crop.height = format.height;

	/* configure the image window */
	isif_set_image_window(vpfe_isif);

	switch (vpfe_dev->vpfe_isif.formats[pad].field) {
	case V4L2_FIELD_INTERLACED:
		/* do nothing, since it is default */
		ret = isif_set_buftype(vpfe_isif, ISIF_BUFTYPE_FLD_INTERLEAVED);
		break;

	case V4L2_FIELD_NONE:
		frm_fmt = ISIF_FRMFMT_PROGRESSIVE;
		/* buffer type only applicable for interlaced scan */
		break;

	case V4L2_FIELD_SEQ_TB:
		ret = isif_set_buftype(vpfe_isif, ISIF_BUFTYPE_FLD_SEPARATED);
		break;

	default:
		return -EINVAL;
	}

	/* set the frame format */
	if (!ret)
		ret = isif_set_frame_format(vpfe_isif, frm_fmt);

	return ret;
}

/*
 * isif_try_format() - Try video format on a pad
 * @isif: VPFE isif device
 * @cfg: V4L2 subdev pad config
 * @fmt: pointer to v4l2 subdev format structure
 */
static void
isif_try_format(struct vpfe_isif_device *isif,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *fmt)
{
	unsigned int width = fmt->format.width;
	unsigned int height = fmt->format.height;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(isif_fmts); i++) {
		if (fmt->format.code == isif_fmts[i])
			break;
	}

	/* If not found, use YUYV8_2x8 as default */
	if (i >= ARRAY_SIZE(isif_fmts))
		fmt->format.code = MEDIA_BUS_FMT_YUYV8_2X8;

	/* Clamp the size. */
	fmt->format.width = clamp_t(u32, width, 32, MAX_WIDTH);
	fmt->format.height = clamp_t(u32, height, 32, MAX_HEIGHT);

	/* The data formatter truncates the number of horizontal output
	 * pixels to a multiple of 16. To avoid clipping data, allow
	 * callers to request an output size bigger than the input size
	 * up to the nearest multiple of 16.
	 */
	if (fmt->pad == ISIF_PAD_SOURCE)
		fmt->format.width &= ~15;
}

/*
 * vpfe_isif_buffer_isr() - isif module non-progressive buffer scheduling isr
 * @isif: Pointer to isif subdevice.
 */
void vpfe_isif_buffer_isr(struct vpfe_isif_device *isif)
{
	struct vpfe_device *vpfe_dev = to_vpfe_device(isif);
	struct vpfe_video_device *video = &isif->video_out;
	enum v4l2_field field;
	int fid;

	if (!video->started)
		return;

	field = video->fmt.fmt.pix.field;

	if (field == V4L2_FIELD_NONE) {
		/* handle progressive frame capture */
		if (video->cur_frm != video->next_frm)
			vpfe_video_process_buffer_complete(video);
		return;
	}

	/* interlaced or TB capture check which field we
	 * are in hardware
	 */
	fid = vpfe_isif_get_fid(vpfe_dev);

	/* switch the software maintained field id */
	video->field_id ^= 1;
	if (fid == video->field_id) {
		/* we are in-sync here,continue */
		if (fid == 0) {
			/*
			 * One frame is just being captured. If the
			 * next frame is available, release the current
			 * frame and move on
			 */
			if (video->cur_frm != video->next_frm)
				vpfe_video_process_buffer_complete(video);
			/*
			 * based on whether the two fields are stored
			 * interleavely or separately in memory,
			 * reconfigure the ISIF memory address
			 */
			if (field == V4L2_FIELD_SEQ_TB)
				vpfe_video_schedule_bottom_field(video);
			return;
		}
		/*
		 * if one field is just being captured configure
		 * the next frame get the next frame from the
		 * empty queue if no frame is available hold on
		 * to the current buffer
		 */
		spin_lock(&video->dma_queue_lock);
		if (!list_empty(&video->dma_queue) &&
		video->cur_frm == video->next_frm)
			vpfe_video_schedule_next_buffer(video);
		spin_unlock(&video->dma_queue_lock);
	} else if (fid == 0) {
		/*
		 * out of sync. Recover from any hardware out-of-sync.
		 * May loose one frame
		 */
		video->field_id = fid;
	}
}

/*
 * vpfe_isif_vidint1_isr() - ISIF module progressive buffer scheduling isr
 * @isif: Pointer to isif subdevice.
 */
void vpfe_isif_vidint1_isr(struct vpfe_isif_device *isif)
{
	struct vpfe_video_device *video = &isif->video_out;

	if (!video->started)
		return;

	spin_lock(&video->dma_queue_lock);
	if (video->fmt.fmt.pix.field == V4L2_FIELD_NONE &&
	    !list_empty(&video->dma_queue) && video->cur_frm == video->next_frm)
		vpfe_video_schedule_next_buffer(video);

	spin_unlock(&video->dma_queue_lock);
}

/*
 * VPFE video operations
 */

static int isif_video_queue(struct vpfe_device *vpfe_dev, unsigned long addr)
{
	struct vpfe_isif_device *isif = &vpfe_dev->vpfe_isif;

	isif_write(isif->isif_cfg.base_addr, (addr >> 21) &
		ISIF_CADU_BITS, CADU);
	isif_write(isif->isif_cfg.base_addr, (addr >> 5) &
		ISIF_CADL_BITS, CADL);

	return 0;
}

static const struct vpfe_video_operations isif_video_ops = {
	.queue = isif_video_queue,
};

/*
 * V4L2 subdev operations
 */

/* Parameter operations */
static int isif_get_params(struct v4l2_subdev *sd, void *params)
{
	struct vpfe_isif_device *isif = v4l2_get_subdevdata(sd);

	/* only raw module parameters can be set through the IOCTL */
	if (isif->formats[ISIF_PAD_SINK].code != MEDIA_BUS_FMT_SGRBG12_1X12)
		return -EINVAL;
	memcpy(params, &isif->isif_cfg.bayer.config_params,
			sizeof(isif->isif_cfg.bayer.config_params));
	return 0;
}

static int isif_validate_df_csc_params(const struct vpfe_isif_df_csc *df_csc)
{
	const struct vpfe_isif_color_space_conv *csc;
	int err = -EINVAL;
	int i;

	if (!df_csc->df_or_csc) {
		/* csc configuration */
		csc = &df_csc->csc;
		if (csc->en) {
			for (i = 0; i < VPFE_ISIF_CSC_NUM_COEFF; i++)
				if (csc->coeff[i].integer >
				    ISIF_CSC_COEF_INTEG_MASK ||
				    csc->coeff[i].decimal >
				    ISIF_CSC_COEF_DECIMAL_MASK) {
					pr_err("Invalid CSC coefficients\n");
					return err;
				}
		}
	}
	if (df_csc->start_pix > ISIF_DF_CSC_SPH_MASK) {
		pr_err("Invalid df_csc start pix value\n");
		return err;
	}

	if (df_csc->num_pixels > ISIF_DF_NUMPIX) {
		pr_err("Invalid df_csc num pixels value\n");
		return err;
	}

	if (df_csc->start_line > ISIF_DF_CSC_LNH_MASK) {
		pr_err("Invalid df_csc start_line value\n");
		return err;
	}

	if (df_csc->num_lines > ISIF_DF_NUMLINES) {
		pr_err("Invalid df_csc num_lines value\n");
		return err;
	}

	return 0;
}

#define DM365_ISIF_MAX_VDFLSFT		4
#define DM365_ISIF_MAX_VDFSLV		4095
#define DM365_ISIF_MAX_DFCMEM0		0x1fff
#define DM365_ISIF_MAX_DFCMEM1		0x1fff

static int isif_validate_dfc_params(const struct vpfe_isif_dfc *dfc)
{
	int err = -EINVAL;
	int i;

	if (!dfc->en)
		return 0;

	if (dfc->corr_whole_line > 1) {
		pr_err("Invalid corr_whole_line value\n");
		return err;
	}

	if (dfc->def_level_shift > DM365_ISIF_MAX_VDFLSFT) {
		pr_err("Invalid def_level_shift value\n");
		return err;
	}

	if (dfc->def_sat_level > DM365_ISIF_MAX_VDFSLV) {
		pr_err("Invalid def_sat_level value\n");
		return err;
	}

	if (!dfc->num_vdefects ||
	    dfc->num_vdefects > VPFE_ISIF_VDFC_TABLE_SIZE) {
		pr_err("Invalid num_vdefects value\n");
		return err;
	}

	for (i = 0; i < VPFE_ISIF_VDFC_TABLE_SIZE; i++) {
		if (dfc->table[i].pos_vert > DM365_ISIF_MAX_DFCMEM0) {
			pr_err("Invalid pos_vert value\n");
			return err;
		}
		if (dfc->table[i].pos_horz > DM365_ISIF_MAX_DFCMEM1) {
			pr_err("Invalid pos_horz value\n");
			return err;
		}
	}

	return 0;
}

#define DM365_ISIF_MAX_CLVRV			0xfff
#define DM365_ISIF_MAX_CLDC			0x1fff
#define DM365_ISIF_MAX_CLHSH			0x1fff
#define DM365_ISIF_MAX_CLHSV			0x1fff
#define DM365_ISIF_MAX_CLVSH			0x1fff
#define DM365_ISIF_MAX_CLVSV			0x1fff
#define DM365_ISIF_MAX_HEIGHT_BLACK_REGION	0x1fff

static int isif_validate_bclamp_params(const struct vpfe_isif_black_clamp *bclamp)
{
	int err = -EINVAL;

	if (bclamp->dc_offset > DM365_ISIF_MAX_CLDC) {
		pr_err("Invalid bclamp dc_offset value\n");
		return err;
	}
	if (!bclamp->en)
		return 0;
	if (bclamp->horz.clamp_pix_limit > 1) {
		pr_err("Invalid bclamp horz clamp_pix_limit value\n");
		return err;
	}
	if (bclamp->horz.win_count_calc < 1 ||
			bclamp->horz.win_count_calc > 32) {
		pr_err("Invalid bclamp horz win_count_calc value\n");
		return err;
	}
	if (bclamp->horz.win_start_h_calc > DM365_ISIF_MAX_CLHSH) {
		pr_err("Invalid bclamp win_start_v_calc value\n");
		return err;
	}

	if (bclamp->horz.win_start_v_calc > DM365_ISIF_MAX_CLHSV) {
		pr_err("Invalid bclamp win_start_v_calc value\n");
		return err;
	}
	if (bclamp->vert.reset_clamp_val > DM365_ISIF_MAX_CLVRV) {
		pr_err("Invalid bclamp reset_clamp_val value\n");
		return err;
	}
	if (bclamp->vert.ob_v_sz_calc > DM365_ISIF_MAX_HEIGHT_BLACK_REGION) {
		pr_err("Invalid bclamp ob_v_sz_calc value\n");
		return err;
	}
	if (bclamp->vert.ob_start_h > DM365_ISIF_MAX_CLVSH) {
		pr_err("Invalid bclamp ob_start_h value\n");
		return err;
	}
	if (bclamp->vert.ob_start_v > DM365_ISIF_MAX_CLVSV) {
		pr_err("Invalid bclamp ob_start_h value\n");
		return err;
	}
	return 0;
}

static int
isif_validate_raw_params(const struct vpfe_isif_raw_config *params)
{
	int ret;

	ret = isif_validate_df_csc_params(&params->df_csc);
	if (ret)
		return ret;
	ret = isif_validate_dfc_params(&params->dfc);
	if (ret)
		return ret;
	return isif_validate_bclamp_params(&params->bclamp);
}

static int isif_set_params(struct v4l2_subdev *sd, const struct vpfe_isif_raw_config *params)
{
	struct vpfe_isif_device *isif = v4l2_get_subdevdata(sd);
	int ret = -EINVAL;

	/* only raw module parameters can be set through the IOCTL */
	if (isif->formats[ISIF_PAD_SINK].code != MEDIA_BUS_FMT_SGRBG12_1X12)
		return ret;

	if (!isif_validate_raw_params(params)) {
		memcpy(&isif->isif_cfg.bayer.config_params, params,
			sizeof(*params));
		ret = 0;
	}
	return ret;
}
/*
 * isif_ioctl() - isif module private ioctl's
 * @sd: VPFE isif V4L2 subdevice
 * @cmd: ioctl command
 * @arg: ioctl argument
 *
 * Return 0 on success or a negative error code otherwise.
 */
static long isif_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case VIDIOC_VPFE_ISIF_S_RAW_PARAMS:
		return isif_set_params(sd, arg);

	case VIDIOC_VPFE_ISIF_G_RAW_PARAMS:
		return isif_get_params(sd, arg);

	default:
		return -ENOIOCTLCMD;
	}
}

static void isif_config_gain_offset(struct vpfe_isif_device *isif)
{
	struct vpfe_isif_gain_offsets_adj *gain_off_ptr =
		&isif->isif_cfg.bayer.config_params.gain_offset;
	void __iomem *base = isif->isif_cfg.base_addr;
	u32 val;

	val = ((gain_off_ptr->gain_sdram_en & 1) << GAIN_SDRAM_EN_SHIFT) |
	      ((gain_off_ptr->gain_ipipe_en & 1) << GAIN_IPIPE_EN_SHIFT) |
	      ((gain_off_ptr->gain_h3a_en & 1) << GAIN_H3A_EN_SHIFT) |
	      ((gain_off_ptr->offset_sdram_en & 1) << OFST_SDRAM_EN_SHIFT) |
	      ((gain_off_ptr->offset_ipipe_en & 1) << OFST_IPIPE_EN_SHIFT) |
	      ((gain_off_ptr->offset_h3a_en & 1) << OFST_H3A_EN_SHIFT);
	isif_merge(base, GAIN_OFFSET_EN_MASK, val, CGAMMAWD);

	isif_write(base, isif->isif_cfg.isif_gain_params.cr_gain, CRGAIN);
	isif_write(base, isif->isif_cfg.isif_gain_params.cgr_gain, CGRGAIN);
	isif_write(base, isif->isif_cfg.isif_gain_params.cgb_gain, CGBGAIN);
	isif_write(base, isif->isif_cfg.isif_gain_params.cb_gain, CBGAIN);
	isif_write(base, isif->isif_cfg.isif_gain_params.offset & OFFSET_MASK,
		   COFSTA);

}

static void isif_config_bclamp(struct vpfe_isif_device *isif,
		   struct vpfe_isif_black_clamp *bc)
{
	u32 val;

	/**
	 * DC Offset is always added to image data irrespective of bc enable
	 * status
	 */
	val = bc->dc_offset & ISIF_BC_DCOFFSET_MASK;
	isif_write(isif->isif_cfg.base_addr, val, CLDCOFST);

	if (!bc->en)
		return;

	val = (bc->bc_mode_color & ISIF_BC_MODE_COLOR_MASK) <<
		ISIF_BC_MODE_COLOR_SHIFT;

	/* Enable BC and horizontal clamp calculation parameters */
	val = val | 1 | ((bc->horz.mode & ISIF_HORZ_BC_MODE_MASK) <<
	      ISIF_HORZ_BC_MODE_SHIFT);

	isif_write(isif->isif_cfg.base_addr, val, CLAMPCFG);

	if (bc->horz.mode != VPFE_ISIF_HORZ_BC_DISABLE) {
		/*
		 * Window count for calculation
		 * Base window selection
		 * pixel limit
		 * Horizontal size of window
		 * vertical size of the window
		 * Horizontal start position of the window
		 * Vertical start position of the window
		 */
		val = (bc->horz.win_count_calc & ISIF_HORZ_BC_WIN_COUNT_MASK) |
		      ((bc->horz.base_win_sel_calc & 1) <<
		      ISIF_HORZ_BC_WIN_SEL_SHIFT) |
		      ((bc->horz.clamp_pix_limit & 1) <<
		      ISIF_HORZ_BC_PIX_LIMIT_SHIFT) |
		      ((bc->horz.win_h_sz_calc &
		      ISIF_HORZ_BC_WIN_H_SIZE_MASK) <<
		      ISIF_HORZ_BC_WIN_H_SIZE_SHIFT) |
		      ((bc->horz.win_v_sz_calc &
		      ISIF_HORZ_BC_WIN_V_SIZE_MASK) <<
		      ISIF_HORZ_BC_WIN_V_SIZE_SHIFT);

		isif_write(isif->isif_cfg.base_addr, val, CLHWIN0);

		val = bc->horz.win_start_h_calc & ISIF_HORZ_BC_WIN_START_H_MASK;
		isif_write(isif->isif_cfg.base_addr, val, CLHWIN1);

		val = bc->horz.win_start_v_calc & ISIF_HORZ_BC_WIN_START_V_MASK;
		isif_write(isif->isif_cfg.base_addr, val, CLHWIN2);
	}

	/* vertical clamp calculation parameters */
	/* OB H Valid */
	val = bc->vert.ob_h_sz_calc & ISIF_VERT_BC_OB_H_SZ_MASK;

	/* Reset clamp value sel for previous line */
	val |= (bc->vert.reset_val_sel & ISIF_VERT_BC_RST_VAL_SEL_MASK) <<
				ISIF_VERT_BC_RST_VAL_SEL_SHIFT;

	/* Line average coefficient */
	val |= bc->vert.line_ave_coef << ISIF_VERT_BC_LINE_AVE_COEF_SHIFT;
	isif_write(isif->isif_cfg.base_addr, val, CLVWIN0);

	/* Configured reset value */
	if (bc->vert.reset_val_sel == VPFE_ISIF_VERT_BC_USE_CONFIG_CLAMP_VAL) {
		val = bc->vert.reset_clamp_val & ISIF_VERT_BC_RST_VAL_MASK;
		isif_write(isif->isif_cfg.base_addr, val, CLVRV);
	}

	/* Optical Black horizontal start position */
	val = bc->vert.ob_start_h & ISIF_VERT_BC_OB_START_HORZ_MASK;
	isif_write(isif->isif_cfg.base_addr, val, CLVWIN1);

	/* Optical Black vertical start position */
	val = bc->vert.ob_start_v & ISIF_VERT_BC_OB_START_VERT_MASK;
	isif_write(isif->isif_cfg.base_addr, val, CLVWIN2);

	val = bc->vert.ob_v_sz_calc & ISIF_VERT_BC_OB_VERT_SZ_MASK;
	isif_write(isif->isif_cfg.base_addr, val, CLVWIN3);

	/* Vertical start position for BC subtraction */
	val = bc->vert_start_sub & ISIF_BC_VERT_START_SUB_V_MASK;
	isif_write(isif->isif_cfg.base_addr, val, CLSV);
}

/* This function will configure the window size to be capture in ISIF reg */
static void
isif_setwin(struct vpfe_isif_device *isif, struct v4l2_rect *image_win,
	    enum isif_frmfmt frm_fmt, int ppc, int mode)
{
	int horz_nr_pixels;
	int vert_nr_lines;
	int horz_start;
	int vert_start;
	int mid_img;

	/*
	 * ppc - per pixel count. indicates how many pixels per cell
	 * output to SDRAM. example, for ycbcr, it is one y and one c, so 2.
	 * raw capture this is 1
	 */
	horz_start = image_win->left << (ppc - 1);
	horz_nr_pixels = (image_win->width << (ppc - 1)) - 1;

	/* Writing the horizontal info into the registers */
	isif_write(isif->isif_cfg.base_addr,
		   horz_start & START_PX_HOR_MASK, SPH);
	isif_write(isif->isif_cfg.base_addr,
		   horz_nr_pixels & NUM_PX_HOR_MASK, LNH);
	vert_start = image_win->top;

	if (frm_fmt == ISIF_FRMFMT_INTERLACED) {
		vert_nr_lines = (image_win->height >> 1) - 1;
		vert_start >>= 1;
		/* To account for VD since line 0 doesn't have any data */
		vert_start += 1;
	} else {
		/* To account for VD since line 0 doesn't have any data */
		vert_start += 1;
		vert_nr_lines = image_win->height - 1;
		/* configure VDINT0 and VDINT1 */
		mid_img = vert_start + (image_win->height / 2);
		isif_write(isif->isif_cfg.base_addr, mid_img, VDINT1);
	}

	if (!mode)
		isif_write(isif->isif_cfg.base_addr, 0, VDINT0);
	else
		isif_write(isif->isif_cfg.base_addr, vert_nr_lines, VDINT0);
	isif_write(isif->isif_cfg.base_addr,
		   vert_start & START_VER_ONE_MASK, SLV0);
	isif_write(isif->isif_cfg.base_addr,
		   vert_start & START_VER_TWO_MASK, SLV1);
	isif_write(isif->isif_cfg.base_addr,
		   vert_nr_lines & NUM_LINES_VER, LNV);
}

#define DM365_ISIF_DFCMWR_MEMORY_WRITE		1
#define DM365_ISIF_DFCMRD_MEMORY_READ		0x2

static void
isif_config_dfc(struct vpfe_isif_device *isif, struct vpfe_isif_dfc *vdfc)
{
#define DFC_WRITE_WAIT_COUNT	1000
	u32 count = DFC_WRITE_WAIT_COUNT;
	u32 val;
	int i;

	if (!vdfc->en)
		return;

	/* Correction mode */
	val = (vdfc->corr_mode & ISIF_VDFC_CORR_MOD_MASK) <<
	       ISIF_VDFC_CORR_MOD_SHIFT;

	/* Correct whole line or partial */
	if (vdfc->corr_whole_line)
		val |= 1 << ISIF_VDFC_CORR_WHOLE_LN_SHIFT;

	/* level shift value */
	val |= (vdfc->def_level_shift & ISIF_VDFC_LEVEL_SHFT_MASK) <<
		ISIF_VDFC_LEVEL_SHFT_SHIFT;

	isif_write(isif->isif_cfg.base_addr, val, DFCCTL);

	/* Defect saturation level */
	val = vdfc->def_sat_level & ISIF_VDFC_SAT_LEVEL_MASK;
	isif_write(isif->isif_cfg.base_addr, val, VDFSATLV);

	isif_write(isif->isif_cfg.base_addr, vdfc->table[0].pos_vert &
		   ISIF_VDFC_POS_MASK, DFCMEM0);
	isif_write(isif->isif_cfg.base_addr, vdfc->table[0].pos_horz &
		   ISIF_VDFC_POS_MASK, DFCMEM1);
	if (vdfc->corr_mode == VPFE_ISIF_VDFC_NORMAL ||
	    vdfc->corr_mode == VPFE_ISIF_VDFC_HORZ_INTERPOL_IF_SAT) {
		isif_write(isif->isif_cfg.base_addr,
			   vdfc->table[0].level_at_pos, DFCMEM2);
		isif_write(isif->isif_cfg.base_addr,
			   vdfc->table[0].level_up_pixels, DFCMEM3);
		isif_write(isif->isif_cfg.base_addr,
			   vdfc->table[0].level_low_pixels, DFCMEM4);
	}

	val = isif_read(isif->isif_cfg.base_addr, DFCMEMCTL);
	/* set DFCMARST and set DFCMWR */
	val |= 1 << ISIF_DFCMEMCTL_DFCMARST_SHIFT;
	val |= 1;
	isif_write(isif->isif_cfg.base_addr, val, DFCMEMCTL);

	while (count && (isif_read(isif->isif_cfg.base_addr, DFCMEMCTL) & 0x01))
		count--;

	val = isif_read(isif->isif_cfg.base_addr, DFCMEMCTL);
	if (!count) {
		pr_debug("defect table write timeout !!\n");
		return;
	}

	for (i = 1; i < vdfc->num_vdefects; i++) {
		isif_write(isif->isif_cfg.base_addr, vdfc->table[i].pos_vert &
			ISIF_VDFC_POS_MASK, DFCMEM0);

		isif_write(isif->isif_cfg.base_addr, vdfc->table[i].pos_horz &
			ISIF_VDFC_POS_MASK, DFCMEM1);

		if (vdfc->corr_mode == VPFE_ISIF_VDFC_NORMAL ||
		    vdfc->corr_mode == VPFE_ISIF_VDFC_HORZ_INTERPOL_IF_SAT) {
			isif_write(isif->isif_cfg.base_addr,
				   vdfc->table[i].level_at_pos, DFCMEM2);
			isif_write(isif->isif_cfg.base_addr,
				   vdfc->table[i].level_up_pixels, DFCMEM3);
			isif_write(isif->isif_cfg.base_addr,
				   vdfc->table[i].level_low_pixels, DFCMEM4);
		}
		val = isif_read(isif->isif_cfg.base_addr, DFCMEMCTL);
		/* clear DFCMARST and set DFCMWR */
		val &= ~(1 << ISIF_DFCMEMCTL_DFCMARST_SHIFT);
		val |= 1;
		isif_write(isif->isif_cfg.base_addr, val, DFCMEMCTL);

		count = DFC_WRITE_WAIT_COUNT;
		while (count && (isif_read(isif->isif_cfg.base_addr,
			DFCMEMCTL) & 0x01))
			count--;

		val = isif_read(isif->isif_cfg.base_addr, DFCMEMCTL);
		if (!count) {
			pr_debug("defect table write timeout !!\n");
			return;
		}
	}
	if (vdfc->num_vdefects < VPFE_ISIF_VDFC_TABLE_SIZE) {
		/* Extra cycle needed */
		isif_write(isif->isif_cfg.base_addr, 0, DFCMEM0);
		isif_write(isif->isif_cfg.base_addr,
			   DM365_ISIF_MAX_DFCMEM1, DFCMEM1);
		isif_write(isif->isif_cfg.base_addr,
			   DM365_ISIF_DFCMWR_MEMORY_WRITE, DFCMEMCTL);
	}
	/* enable VDFC */
	isif_merge(isif->isif_cfg.base_addr, (1 << ISIF_VDFC_EN_SHIFT),
		   (1 << ISIF_VDFC_EN_SHIFT), DFCCTL);

	isif_merge(isif->isif_cfg.base_addr, (1 << ISIF_VDFC_EN_SHIFT),
		   (0 << ISIF_VDFC_EN_SHIFT), DFCCTL);

	isif_write(isif->isif_cfg.base_addr, 0x6, DFCMEMCTL);
	for (i = 0; i < vdfc->num_vdefects; i++) {
		count = DFC_WRITE_WAIT_COUNT;
		while (count &&
			(isif_read(isif->isif_cfg.base_addr, DFCMEMCTL) & 0x2))
			count--;
		val = isif_read(isif->isif_cfg.base_addr, DFCMEMCTL);
		if (!count) {
			pr_debug("defect table write timeout !!\n");
			return;
		}
		isif_write(isif->isif_cfg.base_addr,
			   DM365_ISIF_DFCMRD_MEMORY_READ, DFCMEMCTL);
	}
}

static void
isif_config_csc(struct vpfe_isif_device *isif, struct vpfe_isif_df_csc *df_csc)
{
	u32 val1;
	u32 val2;
	u32 i;

	if (!df_csc->csc.en) {
		isif_write(isif->isif_cfg.base_addr, 0, CSCCTL);
		return;
	}
	/* initialize all bits to 0 */
	val1 = 0;
	for (i = 0; i < VPFE_ISIF_CSC_NUM_COEFF; i++) {
		if ((i % 2) == 0) {
			/* CSCM - LSB */
			val1 = ((df_csc->csc.coeff[i].integer &
				ISIF_CSC_COEF_INTEG_MASK) <<
				ISIF_CSC_COEF_INTEG_SHIFT) |
				((df_csc->csc.coeff[i].decimal &
				ISIF_CSC_COEF_DECIMAL_MASK));
		} else {

			/* CSCM - MSB */
			val2 = ((df_csc->csc.coeff[i].integer &
				ISIF_CSC_COEF_INTEG_MASK) <<
				ISIF_CSC_COEF_INTEG_SHIFT) |
				((df_csc->csc.coeff[i].decimal &
				ISIF_CSC_COEF_DECIMAL_MASK));
			val2 <<= ISIF_CSCM_MSB_SHIFT;
			val2 |= val1;
			isif_write(isif->isif_cfg.base_addr, val2,
				   (CSCM0 + ((i-1) << 1)));
		}
	}
	/* program the active area */
	isif_write(isif->isif_cfg.base_addr, df_csc->start_pix &
		ISIF_DF_CSC_SPH_MASK, FMTSPH);
	/*
	 * one extra pixel as required for CSC. Actually number of
	 * pixel - 1 should be configured in this register. So we
	 * need to subtract 1 before writing to FMTSPH, but we will
	 * not do this since csc requires one extra pixel
	 */
	isif_write(isif->isif_cfg.base_addr, df_csc->num_pixels &
		ISIF_DF_CSC_SPH_MASK, FMTLNH);
	isif_write(isif->isif_cfg.base_addr, df_csc->start_line &
		ISIF_DF_CSC_SPH_MASK, FMTSLV);
	/*
	 * one extra line as required for CSC. See reason documented for
	 * num_pixels
	 */
	isif_write(isif->isif_cfg.base_addr, df_csc->num_lines &
		ISIF_DF_CSC_SPH_MASK, FMTLNV);
	/* Enable CSC */
	isif_write(isif->isif_cfg.base_addr, 1, CSCCTL);
}

static void
isif_config_linearization(struct vpfe_isif_device *isif,
			  struct vpfe_isif_linearize *linearize)
{
	u32 val;
	u32 i;

	if (!linearize->en) {
		isif_write(isif->isif_cfg.base_addr, 0, LINCFG0);
		return;
	}
	/* shift value for correction */
	val = (linearize->corr_shft & ISIF_LIN_CORRSFT_MASK) <<
	      ISIF_LIN_CORRSFT_SHIFT;
	/* enable */
	val |= 1;
	isif_write(isif->isif_cfg.base_addr, val, LINCFG0);
	/* Scale factor */
	val = (linearize->scale_fact.integer & 1) <<
	      ISIF_LIN_SCALE_FACT_INTEG_SHIFT;
	val |= linearize->scale_fact.decimal & ISIF_LIN_SCALE_FACT_DECIMAL_MASK;
	isif_write(isif->isif_cfg.base_addr, val, LINCFG1);

	for (i = 0; i < VPFE_ISIF_LINEAR_TAB_SIZE; i++) {
		val = linearize->table[i] & ISIF_LIN_ENTRY_MASK;
		if (i%2)
			isif_regw_lin_tbl(isif, val, ((i >> 1) << 2), 1);
		else
			isif_regw_lin_tbl(isif, val, ((i >> 1) << 2), 0);
	}
}

static void
isif_config_culling(struct vpfe_isif_device *isif, struct vpfe_isif_cul *cul)
{
	u32 val;

	/* Horizontal pattern */
	val = cul->hcpat_even << CULL_PAT_EVEN_LINE_SHIFT;
	val |= cul->hcpat_odd;
	isif_write(isif->isif_cfg.base_addr, val, CULH);
	/* vertical pattern */
	isif_write(isif->isif_cfg.base_addr, cul->vcpat, CULV);
	/* LPF */
	isif_merge(isif->isif_cfg.base_addr, ISIF_LPF_MASK << ISIF_LPF_SHIFT,
		   cul->en_lpf << ISIF_LPF_SHIFT, MODESET);
}

static int isif_get_pix_fmt(u32 mbus_code)
{
	switch (mbus_code) {
	case MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		return ISIF_PIXFMT_RAW;

	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_YUYV10_2X10:
	case MEDIA_BUS_FMT_Y8_1X8:
		return ISIF_PIXFMT_YCBCR_8BIT;

	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUYV10_1X20:
		return ISIF_PIXFMT_YCBCR_16BIT;

	default:
		break;
	}
	return -EINVAL;
}

#define ISIF_INTERLACE_INVERSE_MODE		0x4b6d
#define ISIF_INTERLACE_NON_INVERSE_MODE		0x0b6d
#define ISIF_PROGRESSIVE_INVERSE_MODE		0x4000
#define ISIF_PROGRESSIVE_NON_INVERSE_MODE	0x0000

static int isif_config_raw(struct v4l2_subdev *sd, int mode)
{
	struct vpfe_isif_device *isif = v4l2_get_subdevdata(sd);
	struct isif_params_raw *params = &isif->isif_cfg.bayer;
	struct vpfe_isif_raw_config *module_params =
				&isif->isif_cfg.bayer.config_params;
	struct v4l2_mbus_framefmt *format;
	int pix_fmt;
	u32 val;

	format = &isif->formats[ISIF_PAD_SINK];

	/* In case of user has set BT656IF earlier, it should be reset
	 * when configuring for raw input.
	 */
	isif_write(isif->isif_cfg.base_addr, 0, REC656IF);
	/* Configure CCDCFG register
	 * Set CCD Not to swap input since input is RAW data
	 * Set FID detection function to Latch at V-Sync
	 * Set WENLOG - isif valid area
	 * Set TRGSEL
	 * Set EXTRG
	 * Packed to 8 or 16 bits
	 */
	val = ISIF_YCINSWP_RAW | ISIF_CCDCFG_FIDMD_LATCH_VSYNC |
	      ISIF_CCDCFG_WENLOG_AND | ISIF_CCDCFG_TRGSEL_WEN |
	      ISIF_CCDCFG_EXTRG_DISABLE | (isif->isif_cfg.data_pack &
	      ISIF_DATA_PACK_MASK);
	isif_write(isif->isif_cfg.base_addr, val, CCDCFG);

	pix_fmt = isif_get_pix_fmt(format->code);
	if (pix_fmt < 0) {
		pr_debug("Invalid pix_fmt(input mode)\n");
		return -EINVAL;
	}
	/*
	 * Configure the vertical sync polarity(MODESET.VDPOL)
	 * Configure the horizontal sync polarity (MODESET.HDPOL)
	 * Configure frame id polarity (MODESET.FLDPOL)
	 * Configure data polarity
	 * Configure External WEN Selection
	 * Configure frame format(progressive or interlace)
	 * Configure pixel format (Input mode)
	 * Configure the data shift
	 */
	val = ISIF_VDHDOUT_INPUT | ((params->vd_pol & ISIF_VD_POL_MASK) <<
	      ISIF_VD_POL_SHIFT) | ((params->hd_pol & ISIF_HD_POL_MASK) <<
	      ISIF_HD_POL_SHIFT) | ((params->fid_pol & ISIF_FID_POL_MASK) <<
	      ISIF_FID_POL_SHIFT) | ((ISIF_DATAPOL_NORMAL &
	      ISIF_DATAPOL_MASK) << ISIF_DATAPOL_SHIFT) | ((ISIF_EXWEN_DISABLE &
	      ISIF_EXWEN_MASK) << ISIF_EXWEN_SHIFT) | ((params->frm_fmt &
	      ISIF_FRM_FMT_MASK) << ISIF_FRM_FMT_SHIFT) | ((pix_fmt &
	      ISIF_INPUT_MASK) << ISIF_INPUT_SHIFT);

	/* currently only MEDIA_BUS_FMT_SGRBG12_1X12 is
	 * supported. shift appropriately depending on
	 * different MBUS fmt's added
	 */
	if (format->code == MEDIA_BUS_FMT_SGRBG12_1X12)
		val |= ((VPFE_ISIF_NO_SHIFT &
			ISIF_DATASFT_MASK) << ISIF_DATASFT_SHIFT);

	isif_write(isif->isif_cfg.base_addr, val, MODESET);
	/*
	 * Configure GAMMAWD register
	 * CFA pattern setting
	 */
	val = (params->cfa_pat & ISIF_GAMMAWD_CFA_MASK) <<
		ISIF_GAMMAWD_CFA_SHIFT;
	/* Gamma msb */
	if (params->v4l2_pix_fmt == V4L2_PIX_FMT_SGRBG10ALAW8)
		val = val | ISIF_ALAW_ENABLE;

	val = val | ((params->data_msb & ISIF_ALAW_GAMA_WD_MASK) <<
			ISIF_ALAW_GAMA_WD_SHIFT);

	isif_write(isif->isif_cfg.base_addr, val, CGAMMAWD);
	/* Configure DPCM compression settings */
	if (params->v4l2_pix_fmt == V4L2_PIX_FMT_SGRBG10DPCM8) {
		val =  1 << ISIF_DPCM_EN_SHIFT;
		val |= (params->dpcm_predictor &
			ISIF_DPCM_PREDICTOR_MASK) << ISIF_DPCM_PREDICTOR_SHIFT;
	}
	isif_write(isif->isif_cfg.base_addr, val, MISC);
	/* Configure Gain & Offset */
	isif_config_gain_offset(isif);
	/* Configure Color pattern */
	if (format->code == MEDIA_BUS_FMT_SGRBG12_1X12)
		val = isif_sgrbg_pattern;
	else
		/* default set to rggb */
		val = isif_srggb_pattern;

	isif_write(isif->isif_cfg.base_addr, val, CCOLP);

	/* Configure HSIZE register  */
	val = (params->horz_flip_en & ISIF_HSIZE_FLIP_MASK) <<
	      ISIF_HSIZE_FLIP_SHIFT;

	/* calculate line offset in 32 bytes based on pack value */
	if (isif->isif_cfg.data_pack == ISIF_PACK_8BIT)
		val |= ((params->win.width + 31) >> 5) & ISIF_LINEOFST_MASK;
	else if (isif->isif_cfg.data_pack == ISIF_PACK_12BIT)
		val |= ((((params->win.width + (params->win.width >> 2)) +
			31) >> 5) & ISIF_LINEOFST_MASK);
	else
		val |= (((params->win.width * 2) + 31) >> 5) &
			ISIF_LINEOFST_MASK;
	isif_write(isif->isif_cfg.base_addr, val, HSIZE);
	/* Configure SDOFST register  */
	if (params->frm_fmt == ISIF_FRMFMT_INTERLACED) {
		if (params->image_invert_en)
			/* For interlace inverse mode */
			isif_write(isif->isif_cfg.base_addr,
				   ISIF_INTERLACE_INVERSE_MODE, SDOFST);
		else
			/* For interlace non inverse mode */
			isif_write(isif->isif_cfg.base_addr,
				   ISIF_INTERLACE_NON_INVERSE_MODE, SDOFST);
	} else if (params->frm_fmt == ISIF_FRMFMT_PROGRESSIVE) {
		if (params->image_invert_en)
			isif_write(isif->isif_cfg.base_addr,
				   ISIF_PROGRESSIVE_INVERSE_MODE, SDOFST);
		else
			/* For progessive non inverse mode */
			isif_write(isif->isif_cfg.base_addr,
				   ISIF_PROGRESSIVE_NON_INVERSE_MODE, SDOFST);
	}
	/* Configure video window */
	isif_setwin(isif, &params->win, params->frm_fmt, 1, mode);
	/* Configure Black Clamp */
	isif_config_bclamp(isif, &module_params->bclamp);
	/* Configure Vertical Defection Pixel Correction */
	isif_config_dfc(isif, &module_params->dfc);
	if (!module_params->df_csc.df_or_csc)
		/* Configure Color Space Conversion */
		isif_config_csc(isif, &module_params->df_csc);

	isif_config_linearization(isif, &module_params->linearize);
	/* Configure Culling */
	isif_config_culling(isif, &module_params->culling);
	/* Configure Horizontal and vertical offsets(DFC,LSC,Gain) */
	val = module_params->horz_offset & ISIF_DATA_H_OFFSET_MASK;
	isif_write(isif->isif_cfg.base_addr, val, DATAHOFST);

	val = module_params->vert_offset & ISIF_DATA_V_OFFSET_MASK;
	isif_write(isif->isif_cfg.base_addr, val, DATAVOFST);

	return 0;
}

#define DM365_ISIF_HSIZE_MASK		0xffffffe0
#define DM365_ISIF_SDOFST_2_LINES	0x00000249

/* This function will configure ISIF for YCbCr parameters. */
static int isif_config_ycbcr(struct v4l2_subdev *sd, int mode)
{
	struct vpfe_isif_device *isif = v4l2_get_subdevdata(sd);
	struct isif_ycbcr_config *params = &isif->isif_cfg.ycbcr;
	struct v4l2_mbus_framefmt *format;
	int pix_fmt;
	u32 modeset;
	u32 ccdcfg;

	format = &isif->formats[ISIF_PAD_SINK];
	/*
	 * first reset the ISIF
	 * all registers have default values after reset
	 * This is important since we assume default values to be set in
	 * a lot of registers that we didn't touch
	 */
	/* start with all bits zero */
	ccdcfg = 0;
	modeset = 0;
	pix_fmt = isif_get_pix_fmt(format->code);
	if (pix_fmt < 0) {
		pr_debug("Invalid pix_fmt(input mode)\n");
		return -EINVAL;
	}
	/* configure pixel format or input mode */
	modeset = modeset | ((pix_fmt & ISIF_INPUT_MASK) <<
		  ISIF_INPUT_SHIFT) | ((params->frm_fmt & ISIF_FRM_FMT_MASK) <<
		  ISIF_FRM_FMT_SHIFT) | (((params->fid_pol &
		  ISIF_FID_POL_MASK) << ISIF_FID_POL_SHIFT)) |
		  (((params->hd_pol & ISIF_HD_POL_MASK) << ISIF_HD_POL_SHIFT)) |
		  (((params->vd_pol & ISIF_VD_POL_MASK) << ISIF_VD_POL_SHIFT));
	/* pack the data to 8-bit CCDCCFG */
	switch (format->code) {
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
		if (pix_fmt != ISIF_PIXFMT_YCBCR_8BIT) {
			pr_debug("Invalid pix_fmt(input mode)\n");
			return -EINVAL;
		}
		modeset |= ((VPFE_PINPOL_NEGATIVE & ISIF_VD_POL_MASK) <<
				ISIF_VD_POL_SHIFT);
		isif_write(isif->isif_cfg.base_addr, 3, REC656IF);
		ccdcfg = ccdcfg | ISIF_PACK_8BIT | ISIF_YCINSWP_YCBCR;
		break;

	case MEDIA_BUS_FMT_YUYV10_2X10:
		if (pix_fmt != ISIF_PIXFMT_YCBCR_8BIT) {
			pr_debug("Invalid pix_fmt(input mode)\n");
			return -EINVAL;
		}
		/* setup BT.656, embedded sync  */
		isif_write(isif->isif_cfg.base_addr, 3, REC656IF);
		/* enable 10 bit mode in ccdcfg */
		ccdcfg = ccdcfg | ISIF_PACK_8BIT | ISIF_YCINSWP_YCBCR |
			ISIF_BW656_ENABLE;
		break;

	case MEDIA_BUS_FMT_YUYV10_1X20:
		if (pix_fmt != ISIF_PIXFMT_YCBCR_16BIT) {
			pr_debug("Invalid pix_fmt(input mode)\n");
			return -EINVAL;
		}
		isif_write(isif->isif_cfg.base_addr, 3, REC656IF);
		break;

	case MEDIA_BUS_FMT_Y8_1X8:
		ccdcfg |= ISIF_PACK_8BIT;
		ccdcfg |= ISIF_YCINSWP_YCBCR;
		if (pix_fmt != ISIF_PIXFMT_YCBCR_8BIT) {
			pr_debug("Invalid pix_fmt(input mode)\n");
			return -EINVAL;
		}
		break;

	case MEDIA_BUS_FMT_YUYV8_1X16:
		if (pix_fmt != ISIF_PIXFMT_YCBCR_16BIT) {
			pr_debug("Invalid pix_fmt(input mode)\n");
			return -EINVAL;
		}
		break;

	default:
		/* should never come here */
		pr_debug("Invalid interface type\n");
		return -EINVAL;
	}
	isif_write(isif->isif_cfg.base_addr, modeset, MODESET);
	/* Set up pix order */
	ccdcfg |= (params->pix_order & ISIF_PIX_ORDER_MASK) <<
		ISIF_PIX_ORDER_SHIFT;
	isif_write(isif->isif_cfg.base_addr, ccdcfg, CCDCFG);
	/* configure video window */
	if (format->code == MEDIA_BUS_FMT_YUYV10_1X20 ||
			format->code == MEDIA_BUS_FMT_YUYV8_1X16)
		isif_setwin(isif, &params->win, params->frm_fmt, 1, mode);
	else
		isif_setwin(isif, &params->win, params->frm_fmt, 2, mode);

	/*
	 * configure the horizontal line offset
	 * this is done by rounding up width to a multiple of 16 pixels
	 * and multiply by two to account for y:cb:cr 4:2:2 data
	 */
	isif_write(isif->isif_cfg.base_addr,
		   ((((params->win.width * 2) + 31) &
		   DM365_ISIF_HSIZE_MASK) >> 5), HSIZE);

	/* configure the memory line offset */
	if (params->frm_fmt == ISIF_FRMFMT_INTERLACED &&
	    params->buf_type == ISIF_BUFTYPE_FLD_INTERLEAVED)
		/* two fields are interleaved in memory */
		isif_write(isif->isif_cfg.base_addr,
			   DM365_ISIF_SDOFST_2_LINES, SDOFST);
	return 0;
}

static int isif_configure(struct v4l2_subdev *sd, int mode)
{
	struct vpfe_isif_device *isif = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = &isif->formats[ISIF_PAD_SINK];

	switch (format->code) {
	case MEDIA_BUS_FMT_SGRBG10_ALAW8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		return isif_config_raw(sd, mode);

	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_YUYV10_2X10:
	case MEDIA_BUS_FMT_Y8_1X8:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUYV10_1X20:
		return isif_config_ycbcr(sd, mode);

	default:
		break;
	}
	return -EINVAL;
}

/*
 * isif_set_stream() - Enable/Disable streaming on the ISIF module
 * @sd: VPFE ISIF V4L2 subdevice
 * @enable: Enable/disable stream
 */
static int isif_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct vpfe_isif_device *isif = v4l2_get_subdevdata(sd);
	int ret;

	if (enable) {
		ret = isif_configure(sd,
			(isif->output == ISIF_OUTPUT_MEMORY) ? 0 : 1);
		if (ret)
			return ret;
		if (isif->output == ISIF_OUTPUT_MEMORY)
			isif_enable_output_to_sdram(isif, 1);
		isif_enable(isif, 1);
	} else {
		isif_enable(isif, 0);
		isif_enable_output_to_sdram(isif, 0);
	}

	return 0;
}

/*
 * __isif_get_format() - helper function for getting isif format
 * @isif: pointer to isif private structure.
 * @pad: pad number.
 * @cfg: V4L2 subdev pad config
 * @which: wanted subdev format.
 */
static struct v4l2_mbus_framefmt *
__isif_get_format(struct vpfe_isif_device *isif,
		  struct v4l2_subdev_pad_config *cfg, unsigned int pad,
		  enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&isif->subdev, cfg, pad);

	return &isif->formats[pad];
}

/*
 * isif_set_format() - set format on pad
 * @sd    : VPFE ISIF device
 * @cfg   : V4L2 subdev pad config
 * @fmt   : pointer to v4l2 subdev format structure
 *
 * Return 0 on success or -EINVAL if format or pad is invalid
 */
static int
isif_set_format(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *fmt)
{
	struct vpfe_isif_device *isif = v4l2_get_subdevdata(sd);
	struct vpfe_device *vpfe_dev = to_vpfe_device(isif);
	struct v4l2_mbus_framefmt *format;

	format = __isif_get_format(isif, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	isif_try_format(isif, cfg, fmt);
	memcpy(format, &fmt->format, sizeof(*format));

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	if (fmt->pad == ISIF_PAD_SOURCE)
		return isif_config_format(vpfe_dev, fmt->pad);

	return 0;
}

/*
 * isif_get_format() - Retrieve the video format on a pad
 * @sd: VPFE ISIF V4L2 subdevice
 * @cfg: V4L2 subdev pad config
 * @fmt: pointer to v4l2 subdev format structure
 *
 * Return 0 on success or -EINVAL if the pad is invalid or doesn't correspond
 * to the format type.
 */
static int
isif_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *fmt)
{
	struct vpfe_isif_device *vpfe_isif = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __isif_get_format(vpfe_isif, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	memcpy(&fmt->format, format, sizeof(fmt->format));

	return 0;
}

/*
 * isif_enum_frame_size() - enum frame sizes on pads
 * @sd: VPFE isif V4L2 subdevice
 * @cfg: V4L2 subdev pad config
 * @code: pointer to v4l2_subdev_frame_size_enum structure
 */
static int
isif_enum_frame_size(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		     struct v4l2_subdev_frame_size_enum *fse)
{
	struct vpfe_isif_device *isif = v4l2_get_subdevdata(sd);
	struct v4l2_subdev_format format;

	if (fse->index != 0)
		return -EINVAL;

	format.pad = fse->pad;
	format.format.code = fse->code;
	format.format.width = 1;
	format.format.height = 1;
	format.which = fse->which;
	isif_try_format(isif, cfg, &format);
	fse->min_width = format.format.width;
	fse->min_height = format.format.height;

	if (format.format.code != fse->code)
		return -EINVAL;

	format.pad = fse->pad;
	format.format.code = fse->code;
	format.format.width = -1;
	format.format.height = -1;
	format.which = fse->which;
	isif_try_format(isif, cfg, &format);
	fse->max_width = format.format.width;
	fse->max_height = format.format.height;

	return 0;
}

/*
 * isif_enum_mbus_code() - enum mbus codes for pads
 * @sd: VPFE isif V4L2 subdevice
 * @cfg: V4L2 subdev pad config
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 */
static int
isif_enum_mbus_code(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
		    struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->pad) {
	case ISIF_PAD_SINK:
	case ISIF_PAD_SOURCE:
		if (code->index >= ARRAY_SIZE(isif_fmts))
			return -EINVAL;
		code->code = isif_fmts[code->index];
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * isif_pad_set_selection() - set crop rectangle on pad
 * @sd: VPFE isif V4L2 subdevice
 * @cfg: V4L2 subdev pad config
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 *
 * Return 0 on success, -EINVAL if pad is invalid
 */
static int
isif_pad_set_selection(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_selection *sel)
{
	struct vpfe_isif_device *vpfe_isif = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	/* check whether it's a valid pad and target */
	if (sel->pad != ISIF_PAD_SINK || sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	format = __isif_get_format(vpfe_isif, cfg, sel->pad, sel->which);
	if (format == NULL)
		return -EINVAL;

	/* check wether crop rect is within limits */
	if (sel->r.top < 0 || sel->r.left < 0 ||
		(sel->r.left + sel->r.width >
		vpfe_isif->formats[ISIF_PAD_SINK].width) ||
		(sel->r.top + sel->r.height >
			vpfe_isif->formats[ISIF_PAD_SINK].height)) {
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = format->width;
		sel->r.height = format->height;
	}
	/* adjust the width to 16 pixel boundary */
	sel->r.width = (sel->r.width + 15) & ~0xf;
	vpfe_isif->crop = sel->r;
	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		isif_set_image_window(vpfe_isif);
	} else {
		struct v4l2_rect *rect;

		rect = v4l2_subdev_get_try_crop(sd, cfg, ISIF_PAD_SINK);
		memcpy(rect, &vpfe_isif->crop, sizeof(*rect));
	}
	return 0;
}

/*
 * isif_pad_get_selection() - get crop rectangle on pad
 * @sd: VPFE isif V4L2 subdevice
 * @cfg: V4L2 subdev pad config
 * @code: pointer to v4l2_subdev_mbus_code_enum structure
 *
 * Return 0 on success, -EINVAL if pad is invalid
 */
static int
isif_pad_get_selection(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_selection *sel)
{
	struct vpfe_isif_device *vpfe_isif = v4l2_get_subdevdata(sd);

	/* check whether it's a valid pad and target */
	if (sel->pad != ISIF_PAD_SINK || sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		struct v4l2_rect *rect;

		rect = v4l2_subdev_get_try_crop(sd, cfg, ISIF_PAD_SINK);
		memcpy(&sel->r, rect, sizeof(*rect));
	} else {
		sel->r = vpfe_isif->crop;
	}

	return 0;
}

/*
 * isif_init_formats() - Initialize formats on all pads
 * @sd: VPFE isif V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. Try formats are initialized
 * on the file handle.
 */
static int
isif_init_formats(struct v4l2_subdev *sd,
		  struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;
	struct v4l2_subdev_selection sel;

	memset(&format, 0, sizeof(format));
	format.pad = ISIF_PAD_SINK;
	format.which = V4L2_SUBDEV_FORMAT_TRY;
	format.format.code = MEDIA_BUS_FMT_SGRBG12_1X12;
	format.format.width = MAX_WIDTH;
	format.format.height = MAX_HEIGHT;
	isif_set_format(sd, fh->pad, &format);

	memset(&format, 0, sizeof(format));
	format.pad = ISIF_PAD_SOURCE;
	format.which = V4L2_SUBDEV_FORMAT_TRY;
	format.format.code = MEDIA_BUS_FMT_SGRBG12_1X12;
	format.format.width = MAX_WIDTH;
	format.format.height = MAX_HEIGHT;
	isif_set_format(sd, fh->pad, &format);

	memset(&sel, 0, sizeof(sel));
	sel.pad = ISIF_PAD_SINK;
	sel.which = V4L2_SUBDEV_FORMAT_TRY;
	sel.target = V4L2_SEL_TGT_CROP;
	sel.r.width = MAX_WIDTH;
	sel.r.height = MAX_HEIGHT;
	isif_pad_set_selection(sd, fh->pad, &sel);

	return 0;
}

/* subdev core operations */
static const struct v4l2_subdev_core_ops isif_v4l2_core_ops = {
	.ioctl = isif_ioctl,
};

/* subdev file operations */
static const struct v4l2_subdev_internal_ops isif_v4l2_internal_ops = {
	.open = isif_init_formats,
};

/* subdev video operations */
static const struct v4l2_subdev_video_ops isif_v4l2_video_ops = {
	.s_stream = isif_set_stream,
};

/* subdev pad operations */
static const struct v4l2_subdev_pad_ops isif_v4l2_pad_ops = {
	.enum_mbus_code = isif_enum_mbus_code,
	.enum_frame_size = isif_enum_frame_size,
	.get_fmt = isif_get_format,
	.set_fmt = isif_set_format,
	.set_selection = isif_pad_set_selection,
	.get_selection = isif_pad_get_selection,
};

/* subdev operations */
static const struct v4l2_subdev_ops isif_v4l2_ops = {
	.core = &isif_v4l2_core_ops,
	.video = &isif_v4l2_video_ops,
	.pad = &isif_v4l2_pad_ops,
};

/*
 * Media entity operations
 */

/*
 * isif_link_setup() - Setup isif connections
 * @entity: isif media entity
 * @local: Pad at the local end of the link
 * @remote: Pad at the remote end of the link
 * @flags: Link flags
 *
 * return -EINVAL or zero on success
 */
static int
isif_link_setup(struct media_entity *entity, const struct media_pad *local,
		const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct vpfe_isif_device *isif = v4l2_get_subdevdata(sd);
	unsigned int index = local->index;

	/* FIXME: this is actually a hack! */
	if (is_media_entity_v4l2_subdev(remote->entity))
		index |= 2 << 16;

	switch (index) {
	case ISIF_PAD_SINK | 2 << 16:
		/* read from decoder/sensor */
		if (!(flags & MEDIA_LNK_FL_ENABLED)) {
			isif->input = ISIF_INPUT_NONE;
			break;
		}
		if (isif->input != ISIF_INPUT_NONE)
			return -EBUSY;
		isif->input = ISIF_INPUT_PARALLEL;
		break;

	case ISIF_PAD_SOURCE:
		/* write to memory */
		if (flags & MEDIA_LNK_FL_ENABLED)
			isif->output = ISIF_OUTPUT_MEMORY;
		else
			isif->output = ISIF_OUTPUT_NONE;
		break;

	case ISIF_PAD_SOURCE | 2 << 16:
		if (flags & MEDIA_LNK_FL_ENABLED)
			isif->output = ISIF_OUTPUT_IPIPEIF;
		else
			isif->output = ISIF_OUTPUT_NONE;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}
static const struct media_entity_operations isif_media_ops = {
	.link_setup = isif_link_setup,
};

/*
 * vpfe_isif_unregister_entities() - isif unregister entity
 * @isif - pointer to isif subdevice structure.
 */
void vpfe_isif_unregister_entities(struct vpfe_isif_device *isif)
{
	vpfe_video_unregister(&isif->video_out);
	/* unregister subdev */
	v4l2_device_unregister_subdev(&isif->subdev);
	/* cleanup entity */
	media_entity_cleanup(&isif->subdev.entity);
}

static void isif_restore_defaults(struct vpfe_isif_device *isif)
{
	enum vpss_ccdc_source_sel source = VPSS_CCDCIN;
	int i;

	memset(&isif->isif_cfg.bayer.config_params, 0,
	       sizeof(struct vpfe_isif_raw_config));

	isif->isif_cfg.bayer.config_params.linearize.corr_shft =
					VPFE_ISIF_NO_SHIFT;
	isif->isif_cfg.bayer.config_params.linearize.scale_fact.integer = 1;
	isif->isif_cfg.bayer.config_params.culling.hcpat_odd =
			ISIF_CULLING_HCAPT_ODD;
	isif->isif_cfg.bayer.config_params.culling.hcpat_even =
			ISIF_CULLING_HCAPT_EVEN;
	isif->isif_cfg.bayer.config_params.culling.vcpat = ISIF_CULLING_VCAPT;
	/* Enable clock to ISIF, IPIPEIF and BL */
	vpss_enable_clock(VPSS_CCDC_CLOCK, 1);
	vpss_enable_clock(VPSS_IPIPEIF_CLOCK, 1);
	vpss_enable_clock(VPSS_BL_CLOCK, 1);

	/* set all registers to default value */
	for (i = 0; i <= 0x1f8; i += 4)
		isif_write(isif->isif_cfg.base_addr, 0, i);
	/* no culling support */
	isif_write(isif->isif_cfg.base_addr, 0xffff, CULH);
	isif_write(isif->isif_cfg.base_addr, 0xff, CULV);

	/* Set default offset and gain */
	isif_config_gain_offset(isif);
	vpss_select_ccdc_source(source);
}

/*
 * vpfe_isif_register_entities() - isif register entity
 * @isif - pointer to isif subdevice structure.
 * @vdev: pointer to v4l2 device structure.
 */
int vpfe_isif_register_entities(struct vpfe_isif_device *isif,
			    struct v4l2_device *vdev)
{
	struct vpfe_device *vpfe_dev = to_vpfe_device(isif);
	unsigned int flags;
	int ret;

	/* Register the subdev */
	ret = v4l2_device_register_subdev(vdev, &isif->subdev);
	if (ret < 0)
		return ret;

	isif_restore_defaults(isif);
	ret = vpfe_video_register(&isif->video_out, vdev);
	if (ret) {
		pr_err("Failed to register isif video out device\n");
		goto out_video_register;
	}
	isif->video_out.vpfe_dev = vpfe_dev;
	flags = 0;
	/* connect isif to video node */
	ret = media_create_pad_link(&isif->subdev.entity, 1,
				       &isif->video_out.video_dev.entity,
				       0, flags);
	if (ret < 0)
		goto out_create_link;
	return 0;
out_create_link:
	vpfe_video_unregister(&isif->video_out);
out_video_register:
	v4l2_device_unregister_subdev(&isif->subdev);
	return ret;
}

/* -------------------------------------------------------------------
 * V4L2 subdev control operations
 */

static int vpfe_isif_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpfe_isif_device *isif =
	     container_of(ctrl->handler, struct vpfe_isif_device, ctrls);
	struct isif_oper_config *config = &isif->isif_cfg;

	switch (ctrl->id) {
	case VPFE_CID_DPCM_PREDICTOR:
		config->bayer.dpcm_predictor = ctrl->val;
		break;

	case VPFE_ISIF_CID_CRGAIN:
		config->isif_gain_params.cr_gain = ctrl->val;
		break;

	case VPFE_ISIF_CID_CGRGAIN:
		config->isif_gain_params.cgr_gain = ctrl->val;
		break;

	case VPFE_ISIF_CID_CGBGAIN:
		config->isif_gain_params.cgb_gain = ctrl->val;
		break;

	case VPFE_ISIF_CID_CBGAIN:
		config->isif_gain_params.cb_gain = ctrl->val;
		break;

	case VPFE_ISIF_CID_GAIN_OFFSET:
		config->isif_gain_params.offset = ctrl->val;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops vpfe_isif_ctrl_ops = {
	.s_ctrl = vpfe_isif_s_ctrl,
};

static const struct v4l2_ctrl_config vpfe_isif_dpcm_pred = {
	.ops = &vpfe_isif_ctrl_ops,
	.id = VPFE_CID_DPCM_PREDICTOR,
	.name = "DPCM Predictor",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config vpfe_isif_crgain = {
	.ops = &vpfe_isif_ctrl_ops,
	.id = VPFE_ISIF_CID_CRGAIN,
	.name = "CRGAIN",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 12) - 1,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config vpfe_isif_cgrgain = {
	.ops = &vpfe_isif_ctrl_ops,
	.id = VPFE_ISIF_CID_CGRGAIN,
	.name = "CGRGAIN",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 12) - 1,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config vpfe_isif_cgbgain = {
	.ops = &vpfe_isif_ctrl_ops,
	.id = VPFE_ISIF_CID_CGBGAIN,
	.name = "CGBGAIN",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 12) - 1,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config vpfe_isif_cbgain = {
	.ops = &vpfe_isif_ctrl_ops,
	.id = VPFE_ISIF_CID_CBGAIN,
	.name = "CBGAIN",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 12) - 1,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config vpfe_isif_gain_offset = {
	.ops = &vpfe_isif_ctrl_ops,
	.id = VPFE_ISIF_CID_GAIN_OFFSET,
	.name = "Gain Offset",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = (1 << 12) - 1,
	.step = 1,
	.def = 0,
};

static void isif_remove(struct vpfe_isif_device *isif,
			struct platform_device *pdev)
{
	struct resource *res;
	int i = 0;

	iounmap(isif->isif_cfg.base_addr);
	iounmap(isif->isif_cfg.linear_tbl0_addr);
	iounmap(isif->isif_cfg.linear_tbl1_addr);

	while (i < 3) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (res)
			release_mem_region(res->start,
					   resource_size(res));
		i++;
	}
}

static void isif_config_defaults(struct vpfe_isif_device *isif)
{
	isif->isif_cfg.ycbcr.v4l2_pix_fmt = V4L2_PIX_FMT_UYVY;
	isif->isif_cfg.ycbcr.pix_fmt = ISIF_PIXFMT_YCBCR_8BIT;
	isif->isif_cfg.ycbcr.frm_fmt = ISIF_FRMFMT_INTERLACED;
	isif->isif_cfg.ycbcr.fid_pol = VPFE_PINPOL_POSITIVE;
	isif->isif_cfg.ycbcr.vd_pol = VPFE_PINPOL_POSITIVE;
	isif->isif_cfg.ycbcr.hd_pol = VPFE_PINPOL_POSITIVE;
	isif->isif_cfg.ycbcr.pix_order = ISIF_PIXORDER_CBYCRY;
	isif->isif_cfg.ycbcr.buf_type = ISIF_BUFTYPE_FLD_INTERLEAVED;

	isif->isif_cfg.bayer.v4l2_pix_fmt = V4L2_PIX_FMT_SGRBG10ALAW8;
	isif->isif_cfg.bayer.pix_fmt = ISIF_PIXFMT_RAW;
	isif->isif_cfg.bayer.frm_fmt = ISIF_FRMFMT_PROGRESSIVE;
	isif->isif_cfg.bayer.fid_pol = VPFE_PINPOL_POSITIVE;
	isif->isif_cfg.bayer.vd_pol = VPFE_PINPOL_POSITIVE;
	isif->isif_cfg.bayer.hd_pol = VPFE_PINPOL_POSITIVE;
	isif->isif_cfg.bayer.cfa_pat = ISIF_CFA_PAT_MOSAIC;
	isif->isif_cfg.bayer.data_msb = ISIF_BIT_MSB_11;
	isif->isif_cfg.data_pack = ISIF_PACK_8BIT;
}
/*
 * vpfe_isif_init() - Initialize V4L2 subdev and media entity
 * @isif: VPFE isif module
 * @pdev: Pointer to platform device structure.
 * Return 0 on success and a negative error code on failure.
 */
int vpfe_isif_init(struct vpfe_isif_device *isif, struct platform_device *pdev)
{
	struct v4l2_subdev *sd = &isif->subdev;
	struct media_pad *pads = &isif->pads[0];
	struct media_entity *me = &sd->entity;
	static resource_size_t res_len;
	struct resource *res;
	void __iomem *addr;
	int status;
	int i = 0;

	/* Get the ISIF base address, linearization table0 and table1 addr. */
	while (i < 3) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			status = -ENOENT;
			goto fail_nobase_res;
		}
		res_len = resource_size(res);
		res = request_mem_region(res->start, res_len, res->name);
		if (!res) {
			status = -EBUSY;
			goto fail_nobase_res;
		}
		addr = ioremap_nocache(res->start, res_len);
		if (!addr) {
			status = -EBUSY;
			goto fail_base_iomap;
		}
		switch (i) {
		case 0:
			/* ISIF base address */
			isif->isif_cfg.base_addr = addr;
			break;
		case 1:
			/* ISIF linear tbl0 address */
			isif->isif_cfg.linear_tbl0_addr = addr;
			break;
		default:
			/* ISIF linear tbl0 address */
			isif->isif_cfg.linear_tbl1_addr = addr;
			break;
		}
		i++;
	}
	davinci_cfg_reg(DM365_VIN_CAM_WEN);
	davinci_cfg_reg(DM365_VIN_CAM_VD);
	davinci_cfg_reg(DM365_VIN_CAM_HD);
	davinci_cfg_reg(DM365_VIN_YIN4_7_EN);
	davinci_cfg_reg(DM365_VIN_YIN0_3_EN);

	/* queue ops */
	isif->video_out.ops = &isif_video_ops;
	v4l2_subdev_init(sd, &isif_v4l2_ops);
	sd->internal_ops = &isif_v4l2_internal_ops;
	strscpy(sd->name, "DAVINCI ISIF", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for davinci subdevs */
	v4l2_set_subdevdata(sd, isif);
	sd->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	pads[ISIF_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[ISIF_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	isif->input = ISIF_INPUT_NONE;
	isif->output = ISIF_OUTPUT_NONE;
	me->ops = &isif_media_ops;
	status = media_entity_pads_init(me, ISIF_PADS_NUM, pads);
	if (status)
		goto isif_fail;
	isif->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	status = vpfe_video_init(&isif->video_out, "ISIF");
	if (status) {
		pr_err("Failed to init isif-out video device\n");
		goto isif_fail;
	}
	v4l2_ctrl_handler_init(&isif->ctrls, 6);
	v4l2_ctrl_new_custom(&isif->ctrls, &vpfe_isif_crgain, NULL);
	v4l2_ctrl_new_custom(&isif->ctrls, &vpfe_isif_cgrgain, NULL);
	v4l2_ctrl_new_custom(&isif->ctrls, &vpfe_isif_cgbgain, NULL);
	v4l2_ctrl_new_custom(&isif->ctrls, &vpfe_isif_cbgain, NULL);
	v4l2_ctrl_new_custom(&isif->ctrls, &vpfe_isif_gain_offset, NULL);
	v4l2_ctrl_new_custom(&isif->ctrls, &vpfe_isif_dpcm_pred, NULL);

	v4l2_ctrl_handler_setup(&isif->ctrls);
	sd->ctrl_handler = &isif->ctrls;
	isif_config_defaults(isif);
	return 0;
fail_base_iomap:
	release_mem_region(res->start, res_len);
	i--;
fail_nobase_res:
	if (isif->isif_cfg.base_addr)
		iounmap(isif->isif_cfg.base_addr);
	if (isif->isif_cfg.linear_tbl0_addr)
		iounmap(isif->isif_cfg.linear_tbl0_addr);

	while (i >= 0) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		release_mem_region(res->start, res_len);
		i--;
	}
	return status;
isif_fail:
	v4l2_ctrl_handler_free(&isif->ctrls);
	isif_remove(isif, pdev);
	return status;
}

/*
 * vpfe_isif_cleanup - isif module cleanup
 * @isif: pointer to isif subdevice
 * @dev: pointer to platform device structure
 */
void
vpfe_isif_cleanup(struct vpfe_isif_device *isif, struct platform_device *pdev)
{
	isif_remove(isif, pdev);
}
