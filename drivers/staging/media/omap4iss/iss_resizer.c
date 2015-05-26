/*
 * TI OMAP4 ISS V4L2 Driver - ISP RESIZER module
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/sched.h>

#include "iss.h"
#include "iss_regs.h"
#include "iss_resizer.h"

static const unsigned int resizer_fmts[] = {
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_YUYV8_1X16,
};

/*
 * resizer_print_status - Print current RESIZER Module register values.
 * @resizer: Pointer to ISS ISP RESIZER device.
 *
 * Also prints other debug information stored in the RESIZER module.
 */
#define RSZ_PRINT_REGISTER(iss, name)\
	dev_dbg(iss->dev, "###RSZ " #name "=0x%08x\n", \
		iss_reg_read(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_##name))

#define RZA_PRINT_REGISTER(iss, name)\
	dev_dbg(iss->dev, "###RZA " #name "=0x%08x\n", \
		iss_reg_read(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_##name))

static void resizer_print_status(struct iss_resizer_device *resizer)
{
	struct iss_device *iss = to_iss_device(resizer);

	dev_dbg(iss->dev, "-------------RESIZER Register dump-------------\n");

	RSZ_PRINT_REGISTER(iss, SYSCONFIG);
	RSZ_PRINT_REGISTER(iss, IN_FIFO_CTRL);
	RSZ_PRINT_REGISTER(iss, FRACDIV);
	RSZ_PRINT_REGISTER(iss, SRC_EN);
	RSZ_PRINT_REGISTER(iss, SRC_MODE);
	RSZ_PRINT_REGISTER(iss, SRC_FMT0);
	RSZ_PRINT_REGISTER(iss, SRC_FMT1);
	RSZ_PRINT_REGISTER(iss, SRC_VPS);
	RSZ_PRINT_REGISTER(iss, SRC_VSZ);
	RSZ_PRINT_REGISTER(iss, SRC_HPS);
	RSZ_PRINT_REGISTER(iss, SRC_HSZ);
	RSZ_PRINT_REGISTER(iss, DMA_RZA);
	RSZ_PRINT_REGISTER(iss, DMA_RZB);
	RSZ_PRINT_REGISTER(iss, DMA_STA);
	RSZ_PRINT_REGISTER(iss, GCK_MMR);
	RSZ_PRINT_REGISTER(iss, GCK_SDR);
	RSZ_PRINT_REGISTER(iss, IRQ_RZA);
	RSZ_PRINT_REGISTER(iss, IRQ_RZB);
	RSZ_PRINT_REGISTER(iss, YUV_Y_MIN);
	RSZ_PRINT_REGISTER(iss, YUV_Y_MAX);
	RSZ_PRINT_REGISTER(iss, YUV_C_MIN);
	RSZ_PRINT_REGISTER(iss, YUV_C_MAX);
	RSZ_PRINT_REGISTER(iss, SEQ);

	RZA_PRINT_REGISTER(iss, EN);
	RZA_PRINT_REGISTER(iss, MODE);
	RZA_PRINT_REGISTER(iss, 420);
	RZA_PRINT_REGISTER(iss, I_VPS);
	RZA_PRINT_REGISTER(iss, I_HPS);
	RZA_PRINT_REGISTER(iss, O_VSZ);
	RZA_PRINT_REGISTER(iss, O_HSZ);
	RZA_PRINT_REGISTER(iss, V_PHS_Y);
	RZA_PRINT_REGISTER(iss, V_PHS_C);
	RZA_PRINT_REGISTER(iss, V_DIF);
	RZA_PRINT_REGISTER(iss, V_TYP);
	RZA_PRINT_REGISTER(iss, V_LPF);
	RZA_PRINT_REGISTER(iss, H_PHS);
	RZA_PRINT_REGISTER(iss, H_DIF);
	RZA_PRINT_REGISTER(iss, H_TYP);
	RZA_PRINT_REGISTER(iss, H_LPF);
	RZA_PRINT_REGISTER(iss, DWN_EN);
	RZA_PRINT_REGISTER(iss, SDR_Y_BAD_H);
	RZA_PRINT_REGISTER(iss, SDR_Y_BAD_L);
	RZA_PRINT_REGISTER(iss, SDR_Y_SAD_H);
	RZA_PRINT_REGISTER(iss, SDR_Y_SAD_L);
	RZA_PRINT_REGISTER(iss, SDR_Y_OFT);
	RZA_PRINT_REGISTER(iss, SDR_Y_PTR_S);
	RZA_PRINT_REGISTER(iss, SDR_Y_PTR_E);
	RZA_PRINT_REGISTER(iss, SDR_C_BAD_H);
	RZA_PRINT_REGISTER(iss, SDR_C_BAD_L);
	RZA_PRINT_REGISTER(iss, SDR_C_SAD_H);
	RZA_PRINT_REGISTER(iss, SDR_C_SAD_L);
	RZA_PRINT_REGISTER(iss, SDR_C_OFT);
	RZA_PRINT_REGISTER(iss, SDR_C_PTR_S);
	RZA_PRINT_REGISTER(iss, SDR_C_PTR_E);

	dev_dbg(iss->dev, "-----------------------------------------------\n");
}

/*
 * resizer_enable - Enable/Disable RESIZER.
 * @enable: enable flag
 *
 */
static void resizer_enable(struct iss_resizer_device *resizer, u8 enable)
{
	struct iss_device *iss = to_iss_device(resizer);

	iss_reg_update(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SRC_EN,
		       RSZ_SRC_EN_SRC_EN, enable ? RSZ_SRC_EN_SRC_EN : 0);

	/* TODO: Enable RSZB */
	iss_reg_update(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_EN, RSZ_EN_EN,
		       enable ? RSZ_EN_EN : 0);
}

/* -----------------------------------------------------------------------------
 * Format- and pipeline-related configuration helpers
 */

/*
 * resizer_set_outaddr - Set memory address to save output image
 * @resizer: Pointer to ISP RESIZER device.
 * @addr: 32-bit memory address aligned on 32 byte boundary.
 *
 * Sets the memory address where the output will be saved.
 */
static void resizer_set_outaddr(struct iss_resizer_device *resizer, u32 addr)
{
	struct iss_device *iss = to_iss_device(resizer);
	struct v4l2_mbus_framefmt *informat, *outformat;

	informat = &resizer->formats[RESIZER_PAD_SINK];
	outformat = &resizer->formats[RESIZER_PAD_SOURCE_MEM];

	/* Save address splitted in Base Address H & L */
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_Y_BAD_H,
		      (addr >> 16) & 0xffff);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_Y_BAD_L,
		      addr & 0xffff);

	/* SAD = BAD */
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_Y_SAD_H,
		      (addr >> 16) & 0xffff);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_Y_SAD_L,
		      addr & 0xffff);

	/* Program UV buffer address... Hardcoded to be contiguous! */
	if ((informat->code == MEDIA_BUS_FMT_UYVY8_1X16) &&
	    (outformat->code == MEDIA_BUS_FMT_YUYV8_1_5X8)) {
		u32 c_addr = addr + (resizer->video_out.bpl_value *
				     (outformat->height - 1));

		/* Ensure Y_BAD_L[6:0] = C_BAD_L[6:0]*/
		if ((c_addr ^ addr) & 0x7f) {
			c_addr &= ~0x7f;
			c_addr += 0x80;
			c_addr |= addr & 0x7f;
		}

		/* Save address splitted in Base Address H & L */
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_C_BAD_H,
			      (c_addr >> 16) & 0xffff);
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_C_BAD_L,
			      c_addr & 0xffff);

		/* SAD = BAD */
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_C_SAD_H,
			      (c_addr >> 16) & 0xffff);
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_C_SAD_L,
			      c_addr & 0xffff);
	}
}

static void resizer_configure(struct iss_resizer_device *resizer)
{
	struct iss_device *iss = to_iss_device(resizer);
	struct v4l2_mbus_framefmt *informat, *outformat;

	informat = &resizer->formats[RESIZER_PAD_SINK];
	outformat = &resizer->formats[RESIZER_PAD_SOURCE_MEM];

	/* Disable pass-through more. Despite its name, the BYPASS bit controls
	 * pass-through mode, not bypass mode.
	 */
	iss_reg_clr(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SRC_FMT0,
		    RSZ_SRC_FMT0_BYPASS);

	/* Select RSZ input */
	iss_reg_update(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SRC_FMT0,
		       RSZ_SRC_FMT0_SEL,
		       resizer->input == RESIZER_INPUT_IPIPEIF ?
		       RSZ_SRC_FMT0_SEL : 0);

	/* RSZ ignores WEN signal from IPIPE/IPIPEIF */
	iss_reg_clr(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SRC_MODE,
		    RSZ_SRC_MODE_WRT);

	/* Set Resizer in free-running mode */
	iss_reg_clr(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SRC_MODE,
		    RSZ_SRC_MODE_OST);

	/* Init Resizer A */
	iss_reg_clr(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_MODE,
		    RZA_MODE_ONE_SHOT);

	/* Set size related things now */
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SRC_VPS, 0);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SRC_HPS, 0);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SRC_VSZ,
		      informat->height - 2);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SRC_HSZ,
		      informat->width - 1);

	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_I_VPS, 0);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_I_HPS, 0);

	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_O_VSZ,
		      outformat->height - 2);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_O_HSZ,
		      outformat->width - 1);

	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_V_DIF, 0x100);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_H_DIF, 0x100);

	/* Buffer output settings */
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_Y_PTR_S, 0);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_Y_PTR_E,
		      outformat->height - 1);

	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_Y_OFT,
		      resizer->video_out.bpl_value);

	/* UYVY -> NV12 conversion */
	if ((informat->code == MEDIA_BUS_FMT_UYVY8_1X16) &&
	    (outformat->code == MEDIA_BUS_FMT_YUYV8_1_5X8)) {
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_420,
			      RSZ_420_CEN | RSZ_420_YEN);

		/* UV Buffer output settings */
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_C_PTR_S,
			      0);
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_C_PTR_E,
			      outformat->height - 1);

		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_SDR_C_OFT,
			      resizer->video_out.bpl_value);
	} else {
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_RESIZER, RZA_420, 0);
	}
}

/* -----------------------------------------------------------------------------
 * Interrupt handling
 */

static void resizer_isr_buffer(struct iss_resizer_device *resizer)
{
	struct iss_buffer *buffer;

	/* The whole resizer needs to be stopped. Disabling RZA only produces
	 * input FIFO overflows, most probably when the next frame is received.
	 */
	resizer_enable(resizer, 0);

	buffer = omap4iss_video_buffer_next(&resizer->video_out);
	if (buffer == NULL)
		return;

	resizer_set_outaddr(resizer, buffer->iss_addr);

	resizer_enable(resizer, 1);
}

/*
 * omap4iss_resizer_isr - Configure resizer during interframe time.
 * @resizer: Pointer to ISP RESIZER device.
 * @events: RESIZER events
 */
void omap4iss_resizer_isr(struct iss_resizer_device *resizer, u32 events)
{
	struct iss_device *iss = to_iss_device(resizer);
	struct iss_pipeline *pipe =
			     to_iss_pipeline(&resizer->subdev.entity);

	if (events & (ISP5_IRQ_RSZ_FIFO_IN_BLK_ERR |
		      ISP5_IRQ_RSZ_FIFO_OVF)) {
		dev_dbg(iss->dev, "RSZ Err: FIFO_IN_BLK:%d, FIFO_OVF:%d\n",
			events & ISP5_IRQ_RSZ_FIFO_IN_BLK_ERR ? 1 : 0,
			events & ISP5_IRQ_RSZ_FIFO_OVF ? 1 : 0);
		omap4iss_pipeline_cancel_stream(pipe);
	}

	if (omap4iss_module_sync_is_stopping(&resizer->wait,
					     &resizer->stopping))
		return;

	if (events & ISP5_IRQ_RSZ_INT_DMA)
		resizer_isr_buffer(resizer);
}

/* -----------------------------------------------------------------------------
 * ISS video operations
 */

static int resizer_video_queue(struct iss_video *video,
			       struct iss_buffer *buffer)
{
	struct iss_resizer_device *resizer = container_of(video,
				struct iss_resizer_device, video_out);

	if (!(resizer->output & RESIZER_OUTPUT_MEMORY))
		return -ENODEV;

	resizer_set_outaddr(resizer, buffer->iss_addr);

	/*
	 * If streaming was enabled before there was a buffer queued
	 * or underrun happened in the ISR, the hardware was not enabled
	 * and DMA queue flag ISS_VIDEO_DMAQUEUE_UNDERRUN is still set.
	 * Enable it now.
	 */
	if (video->dmaqueue_flags & ISS_VIDEO_DMAQUEUE_UNDERRUN) {
		resizer_enable(resizer, 1);
		iss_video_dmaqueue_flags_clr(video);
	}

	return 0;
}

static const struct iss_video_operations resizer_video_ops = {
	.queue = resizer_video_queue,
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

/*
 * resizer_set_stream - Enable/Disable streaming on the RESIZER module
 * @sd: ISP RESIZER V4L2 subdevice
 * @enable: Enable/disable stream
 */
static int resizer_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct iss_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct iss_device *iss = to_iss_device(resizer);
	struct iss_video *video_out = &resizer->video_out;
	int ret = 0;

	if (resizer->state == ISS_PIPELINE_STREAM_STOPPED) {
		if (enable == ISS_PIPELINE_STREAM_STOPPED)
			return 0;

		omap4iss_isp_subclk_enable(iss, OMAP4_ISS_ISP_SUBCLK_RSZ);

		iss_reg_set(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_GCK_MMR,
			    RSZ_GCK_MMR_MMR);
		iss_reg_set(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_GCK_SDR,
			    RSZ_GCK_SDR_CORE);

		/* FIXME: Enable RSZB also */
		iss_reg_set(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SYSCONFIG,
			    RSZ_SYSCONFIG_RSZA_CLK_EN);
	}

	switch (enable) {
	case ISS_PIPELINE_STREAM_CONTINUOUS:

		resizer_configure(resizer);
		resizer_print_status(resizer);

		/*
		 * When outputting to memory with no buffer available, let the
		 * buffer queue handler start the hardware. A DMA queue flag
		 * ISS_VIDEO_DMAQUEUE_QUEUED will be set as soon as there is
		 * a buffer available.
		 */
		if (resizer->output & RESIZER_OUTPUT_MEMORY &&
		    !(video_out->dmaqueue_flags & ISS_VIDEO_DMAQUEUE_QUEUED))
			break;

		atomic_set(&resizer->stopping, 0);
		resizer_enable(resizer, 1);
		iss_video_dmaqueue_flags_clr(video_out);
		break;

	case ISS_PIPELINE_STREAM_STOPPED:
		if (resizer->state == ISS_PIPELINE_STREAM_STOPPED)
			return 0;
		if (omap4iss_module_sync_idle(&sd->entity, &resizer->wait,
					      &resizer->stopping))
			ret = -ETIMEDOUT;

		resizer_enable(resizer, 0);
		iss_reg_clr(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_SYSCONFIG,
			    RSZ_SYSCONFIG_RSZA_CLK_EN);
		iss_reg_clr(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_GCK_SDR,
			    RSZ_GCK_SDR_CORE);
		iss_reg_clr(iss, OMAP4_ISS_MEM_ISP_RESIZER, RSZ_GCK_MMR,
			    RSZ_GCK_MMR_MMR);
		omap4iss_isp_subclk_disable(iss, OMAP4_ISS_ISP_SUBCLK_RSZ);
		iss_video_dmaqueue_flags_clr(video_out);
		break;
	}

	resizer->state = enable;
	return ret;
}

static struct v4l2_mbus_framefmt *
__resizer_get_format(struct iss_resizer_device *resizer,
		     struct v4l2_subdev_pad_config *cfg, unsigned int pad,
		     enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&resizer->subdev, cfg, pad);
	return &resizer->formats[pad];
}

/*
 * resizer_try_format - Try video format on a pad
 * @resizer: ISS RESIZER device
 * @cfg: V4L2 subdev pad config
 * @pad: Pad number
 * @fmt: Format
 */
static void
resizer_try_format(struct iss_resizer_device *resizer,
		   struct v4l2_subdev_pad_config *cfg, unsigned int pad,
		   struct v4l2_mbus_framefmt *fmt,
		   enum v4l2_subdev_format_whence which)
{
	u32 pixelcode;
	struct v4l2_mbus_framefmt *format;
	unsigned int width = fmt->width;
	unsigned int height = fmt->height;
	unsigned int i;

	switch (pad) {
	case RESIZER_PAD_SINK:
		for (i = 0; i < ARRAY_SIZE(resizer_fmts); i++) {
			if (fmt->code == resizer_fmts[i])
				break;
		}

		/* If not found, use UYVY as default */
		if (i >= ARRAY_SIZE(resizer_fmts))
			fmt->code = MEDIA_BUS_FMT_UYVY8_1X16;

		/* Clamp the input size. */
		fmt->width = clamp_t(u32, width, 1, 8192);
		fmt->height = clamp_t(u32, height, 1, 8192);
		break;

	case RESIZER_PAD_SOURCE_MEM:
		pixelcode = fmt->code;
		format = __resizer_get_format(resizer, cfg, RESIZER_PAD_SINK,
					      which);
		memcpy(fmt, format, sizeof(*fmt));

		if ((pixelcode == MEDIA_BUS_FMT_YUYV8_1_5X8) &&
		    (fmt->code == MEDIA_BUS_FMT_UYVY8_1X16))
			fmt->code = pixelcode;

		/* The data formatter truncates the number of horizontal output
		 * pixels to a multiple of 16. To avoid clipping data, allow
		 * callers to request an output size bigger than the input size
		 * up to the nearest multiple of 16.
		 */
		fmt->width = clamp_t(u32, width, 32, (fmt->width + 15) & ~15);
		fmt->width &= ~15;
		fmt->height = clamp_t(u32, height, 32, fmt->height);
		break;

	}

	fmt->colorspace = V4L2_COLORSPACE_JPEG;
	fmt->field = V4L2_FIELD_NONE;
}

/*
 * resizer_enum_mbus_code - Handle pixel format enumeration
 * @sd     : pointer to v4l2 subdev structure
 * @cfg: V4L2 subdev pad config
 * @code   : pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int resizer_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	struct iss_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	switch (code->pad) {
	case RESIZER_PAD_SINK:
		if (code->index >= ARRAY_SIZE(resizer_fmts))
			return -EINVAL;

		code->code = resizer_fmts[code->index];
		break;

	case RESIZER_PAD_SOURCE_MEM:
		format = __resizer_get_format(resizer, cfg, RESIZER_PAD_SINK,
					      code->which);

		if (code->index == 0) {
			code->code = format->code;
			break;
		}

		switch (format->code) {
		case MEDIA_BUS_FMT_UYVY8_1X16:
			if (code->index == 1)
				code->code = MEDIA_BUS_FMT_YUYV8_1_5X8;
			else
				return -EINVAL;
			break;
		default:
			if (code->index != 0)
				return -EINVAL;
		}

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int resizer_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct iss_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	resizer_try_format(resizer, cfg, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	resizer_try_format(resizer, cfg, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * resizer_get_format - Retrieve the video format on a pad
 * @sd : ISP RESIZER V4L2 subdevice
 * @cfg: V4L2 subdev pad config
 * @fmt: Format
 *
 * Return 0 on success or -EINVAL if the pad is invalid or doesn't correspond
 * to the format type.
 */
static int resizer_get_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct iss_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __resizer_get_format(resizer, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

/*
 * resizer_set_format - Set the video format on a pad
 * @sd : ISP RESIZER V4L2 subdevice
 * @cfg: V4L2 subdev pad config
 * @fmt: Format
 *
 * Return 0 on success or -EINVAL if the pad is invalid or doesn't correspond
 * to the format type.
 */
static int resizer_set_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct iss_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __resizer_get_format(resizer, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	resizer_try_format(resizer, cfg, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	/* Propagate the format from sink to source */
	if (fmt->pad == RESIZER_PAD_SINK) {
		format = __resizer_get_format(resizer, cfg,
					      RESIZER_PAD_SOURCE_MEM,
					      fmt->which);
		*format = fmt->format;
		resizer_try_format(resizer, cfg, RESIZER_PAD_SOURCE_MEM, format,
				fmt->which);
	}

	return 0;
}

static int resizer_link_validate(struct v4l2_subdev *sd,
				 struct media_link *link,
				 struct v4l2_subdev_format *source_fmt,
				 struct v4l2_subdev_format *sink_fmt)
{
	/* Check if the two ends match */
	if (source_fmt->format.width != sink_fmt->format.width ||
	    source_fmt->format.height != sink_fmt->format.height)
		return -EPIPE;

	if (source_fmt->format.code != sink_fmt->format.code)
		return -EPIPE;

	return 0;
}

/*
 * resizer_init_formats - Initialize formats on all pads
 * @sd: ISP RESIZER V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static int resizer_init_formats(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));
	format.pad = RESIZER_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = MEDIA_BUS_FMT_UYVY8_1X16;
	format.format.width = 4096;
	format.format.height = 4096;
	resizer_set_format(sd, fh ? fh->pad : NULL, &format);

	return 0;
}

/* V4L2 subdev video operations */
static const struct v4l2_subdev_video_ops resizer_v4l2_video_ops = {
	.s_stream = resizer_set_stream,
};

/* V4L2 subdev pad operations */
static const struct v4l2_subdev_pad_ops resizer_v4l2_pad_ops = {
	.enum_mbus_code = resizer_enum_mbus_code,
	.enum_frame_size = resizer_enum_frame_size,
	.get_fmt = resizer_get_format,
	.set_fmt = resizer_set_format,
	.link_validate = resizer_link_validate,
};

/* V4L2 subdev operations */
static const struct v4l2_subdev_ops resizer_v4l2_ops = {
	.video = &resizer_v4l2_video_ops,
	.pad = &resizer_v4l2_pad_ops,
};

/* V4L2 subdev internal operations */
static const struct v4l2_subdev_internal_ops resizer_v4l2_internal_ops = {
	.open = resizer_init_formats,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

/*
 * resizer_link_setup - Setup RESIZER connections
 * @entity: RESIZER media entity
 * @local: Pad at the local end of the link
 * @remote: Pad at the remote end of the link
 * @flags: Link flags
 *
 * return -EINVAL or zero on success
 */
static int resizer_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct iss_resizer_device *resizer = v4l2_get_subdevdata(sd);
	struct iss_device *iss = to_iss_device(resizer);

	switch (local->index | media_entity_type(remote->entity)) {
	case RESIZER_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		/* Read from IPIPE or IPIPEIF. */
		if (!(flags & MEDIA_LNK_FL_ENABLED)) {
			resizer->input = RESIZER_INPUT_NONE;
			break;
		}

		if (resizer->input != RESIZER_INPUT_NONE)
			return -EBUSY;

		if (remote->entity == &iss->ipipeif.subdev.entity)
			resizer->input = RESIZER_INPUT_IPIPEIF;
		else if (remote->entity == &iss->ipipe.subdev.entity)
			resizer->input = RESIZER_INPUT_IPIPE;


		break;

	case RESIZER_PAD_SOURCE_MEM | MEDIA_ENT_T_DEVNODE:
		/* Write to memory */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (resizer->output & ~RESIZER_OUTPUT_MEMORY)
				return -EBUSY;
			resizer->output |= RESIZER_OUTPUT_MEMORY;
		} else {
			resizer->output &= ~RESIZER_OUTPUT_MEMORY;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* media operations */
static const struct media_entity_operations resizer_media_ops = {
	.link_setup = resizer_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * resizer_init_entities - Initialize V4L2 subdev and media entity
 * @resizer: ISS ISP RESIZER module
 *
 * Return 0 on success and a negative error code on failure.
 */
static int resizer_init_entities(struct iss_resizer_device *resizer)
{
	struct v4l2_subdev *sd = &resizer->subdev;
	struct media_pad *pads = resizer->pads;
	struct media_entity *me = &sd->entity;
	int ret;

	resizer->input = RESIZER_INPUT_NONE;

	v4l2_subdev_init(sd, &resizer_v4l2_ops);
	sd->internal_ops = &resizer_v4l2_internal_ops;
	strlcpy(sd->name, "OMAP4 ISS ISP resizer", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for iss subdevs */
	v4l2_set_subdevdata(sd, resizer);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[RESIZER_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[RESIZER_PAD_SOURCE_MEM].flags = MEDIA_PAD_FL_SOURCE;

	me->ops = &resizer_media_ops;
	ret = media_entity_init(me, RESIZER_PADS_NUM, pads, 0);
	if (ret < 0)
		return ret;

	resizer_init_formats(sd, NULL);

	resizer->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	resizer->video_out.ops = &resizer_video_ops;
	resizer->video_out.iss = to_iss_device(resizer);
	resizer->video_out.capture_mem = PAGE_ALIGN(4096 * 4096) * 3;
	resizer->video_out.bpl_alignment = 32;
	resizer->video_out.bpl_zero_padding = 1;
	resizer->video_out.bpl_max = 0x1ffe0;

	ret = omap4iss_video_init(&resizer->video_out, "ISP resizer a");
	if (ret < 0)
		return ret;

	/* Connect the RESIZER subdev to the video node. */
	ret = media_entity_create_link(&resizer->subdev.entity,
				       RESIZER_PAD_SOURCE_MEM,
				       &resizer->video_out.video.entity, 0, 0);
	if (ret < 0)
		return ret;

	return 0;
}

void omap4iss_resizer_unregister_entities(struct iss_resizer_device *resizer)
{
	v4l2_device_unregister_subdev(&resizer->subdev);
	omap4iss_video_unregister(&resizer->video_out);
}

int omap4iss_resizer_register_entities(struct iss_resizer_device *resizer,
	struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev and video node. */
	ret = v4l2_device_register_subdev(vdev, &resizer->subdev);
	if (ret < 0)
		goto error;

	ret = omap4iss_video_register(&resizer->video_out, vdev);
	if (ret < 0)
		goto error;

	return 0;

error:
	omap4iss_resizer_unregister_entities(resizer);
	return ret;
}

/* -----------------------------------------------------------------------------
 * ISP RESIZER initialisation and cleanup
 */

/*
 * omap4iss_resizer_init - RESIZER module initialization.
 * @iss: Device pointer specific to the OMAP4 ISS.
 *
 * TODO: Get the initialisation values from platform data.
 *
 * Return 0 on success or a negative error code otherwise.
 */
int omap4iss_resizer_init(struct iss_device *iss)
{
	struct iss_resizer_device *resizer = &iss->resizer;

	resizer->state = ISS_PIPELINE_STREAM_STOPPED;
	init_waitqueue_head(&resizer->wait);

	return resizer_init_entities(resizer);
}

/*
 * omap4iss_resizer_cleanup - RESIZER module cleanup.
 * @iss: Device pointer specific to the OMAP4 ISS.
 */
void omap4iss_resizer_cleanup(struct iss_device *iss)
{
	struct iss_resizer_device *resizer = &iss->resizer;

	media_entity_cleanup(&resizer->subdev.entity);
}
