/*
 * TI OMAP4 ISS V4L2 Driver - ISP IPIPEIF module
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
#include "iss_ipipeif.h"

static const unsigned int ipipeif_fmts[] = {
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_YUYV8_1X16,
};

/*
 * ipipeif_print_status - Print current IPIPEIF Module register values.
 * @ipipeif: Pointer to ISS ISP IPIPEIF device.
 *
 * Also prints other debug information stored in the IPIPEIF module.
 */
#define IPIPEIF_PRINT_REGISTER(iss, name)\
	dev_dbg(iss->dev, "###IPIPEIF " #name "=0x%08x\n", \
		iss_reg_read(iss, OMAP4_ISS_MEM_ISP_IPIPEIF, IPIPEIF_##name))

#define ISIF_PRINT_REGISTER(iss, name)\
	dev_dbg(iss->dev, "###ISIF " #name "=0x%08x\n", \
		iss_reg_read(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_##name))

#define ISP5_PRINT_REGISTER(iss, name)\
	dev_dbg(iss->dev, "###ISP5 " #name "=0x%08x\n", \
		iss_reg_read(iss, OMAP4_ISS_MEM_ISP_SYS1, ISP5_##name))

static void ipipeif_print_status(struct iss_ipipeif_device *ipipeif)
{
	struct iss_device *iss = to_iss_device(ipipeif);

	dev_dbg(iss->dev, "-------------IPIPEIF Register dump-------------\n");

	IPIPEIF_PRINT_REGISTER(iss, CFG1);
	IPIPEIF_PRINT_REGISTER(iss, CFG2);

	ISIF_PRINT_REGISTER(iss, SYNCEN);
	ISIF_PRINT_REGISTER(iss, CADU);
	ISIF_PRINT_REGISTER(iss, CADL);
	ISIF_PRINT_REGISTER(iss, MODESET);
	ISIF_PRINT_REGISTER(iss, CCOLP);
	ISIF_PRINT_REGISTER(iss, SPH);
	ISIF_PRINT_REGISTER(iss, LNH);
	ISIF_PRINT_REGISTER(iss, LNV);
	ISIF_PRINT_REGISTER(iss, VDINT(0));
	ISIF_PRINT_REGISTER(iss, HSIZE);

	ISP5_PRINT_REGISTER(iss, SYSCONFIG);
	ISP5_PRINT_REGISTER(iss, CTRL);
	ISP5_PRINT_REGISTER(iss, IRQSTATUS(0));
	ISP5_PRINT_REGISTER(iss, IRQENABLE_SET(0));
	ISP5_PRINT_REGISTER(iss, IRQENABLE_CLR(0));

	dev_dbg(iss->dev, "-----------------------------------------------\n");
}

static void ipipeif_write_enable(struct iss_ipipeif_device *ipipeif, u8 enable)
{
	struct iss_device *iss = to_iss_device(ipipeif);

	iss_reg_update(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_SYNCEN,
		       ISIF_SYNCEN_DWEN, enable ? ISIF_SYNCEN_DWEN : 0);
}

/*
 * ipipeif_enable - Enable/Disable IPIPEIF.
 * @enable: enable flag
 *
 */
static void ipipeif_enable(struct iss_ipipeif_device *ipipeif, u8 enable)
{
	struct iss_device *iss = to_iss_device(ipipeif);

	iss_reg_update(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_SYNCEN,
		       ISIF_SYNCEN_SYEN, enable ? ISIF_SYNCEN_SYEN : 0);
}

/* -----------------------------------------------------------------------------
 * Format- and pipeline-related configuration helpers
 */

/*
 * ipipeif_set_outaddr - Set memory address to save output image
 * @ipipeif: Pointer to ISP IPIPEIF device.
 * @addr: 32-bit memory address aligned on 32 byte boundary.
 *
 * Sets the memory address where the output will be saved.
 */
static void ipipeif_set_outaddr(struct iss_ipipeif_device *ipipeif, u32 addr)
{
	struct iss_device *iss = to_iss_device(ipipeif);

	/* Save address splitted in Base Address H & L */
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_CADU,
		      (addr >> (16 + 5)) & ISIF_CADU_MASK);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_CADL,
		      (addr >> 5) & ISIF_CADL_MASK);
}

static void ipipeif_configure(struct iss_ipipeif_device *ipipeif)
{
	struct iss_device *iss = to_iss_device(ipipeif);
	const struct iss_format_info *info;
	struct v4l2_mbus_framefmt *format;
	u32 isif_ccolp = 0;

	omap4iss_configure_bridge(iss, ipipeif->input);

	/* IPIPEIF_PAD_SINK */
	format = &ipipeif->formats[IPIPEIF_PAD_SINK];

	/* IPIPEIF with YUV422 input from ISIF */
	iss_reg_clr(iss, OMAP4_ISS_MEM_ISP_IPIPEIF, IPIPEIF_CFG1,
		    IPIPEIF_CFG1_INPSRC1_MASK | IPIPEIF_CFG1_INPSRC2_MASK);

	/* Select ISIF/IPIPEIF input format */
	switch (format->code) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
		iss_reg_update(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_MODESET,
			       ISIF_MODESET_CCDMD | ISIF_MODESET_INPMOD_MASK |
			       ISIF_MODESET_CCDW_MASK,
			       ISIF_MODESET_INPMOD_YCBCR16);

		iss_reg_update(iss, OMAP4_ISS_MEM_ISP_IPIPEIF, IPIPEIF_CFG2,
			       IPIPEIF_CFG2_YUV8, IPIPEIF_CFG2_YUV16);

		break;
	case MEDIA_BUS_FMT_SGRBG10_1X10:
		isif_ccolp = ISIF_CCOLP_CP0_F0_GR |
			ISIF_CCOLP_CP1_F0_R |
			ISIF_CCOLP_CP2_F0_B |
			ISIF_CCOLP_CP3_F0_GB;
		goto cont_raw;
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		isif_ccolp = ISIF_CCOLP_CP0_F0_R |
			ISIF_CCOLP_CP1_F0_GR |
			ISIF_CCOLP_CP2_F0_GB |
			ISIF_CCOLP_CP3_F0_B;
		goto cont_raw;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
		isif_ccolp = ISIF_CCOLP_CP0_F0_B |
			ISIF_CCOLP_CP1_F0_GB |
			ISIF_CCOLP_CP2_F0_GR |
			ISIF_CCOLP_CP3_F0_R;
		goto cont_raw;
	case MEDIA_BUS_FMT_SGBRG10_1X10:
		isif_ccolp = ISIF_CCOLP_CP0_F0_GB |
			ISIF_CCOLP_CP1_F0_B |
			ISIF_CCOLP_CP2_F0_R |
			ISIF_CCOLP_CP3_F0_GR;
cont_raw:
		iss_reg_clr(iss, OMAP4_ISS_MEM_ISP_IPIPEIF, IPIPEIF_CFG2,
			    IPIPEIF_CFG2_YUV16);

		iss_reg_update(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_MODESET,
			       ISIF_MODESET_CCDMD | ISIF_MODESET_INPMOD_MASK |
			       ISIF_MODESET_CCDW_MASK, ISIF_MODESET_INPMOD_RAW |
			       ISIF_MODESET_CCDW_2BIT);

		info = omap4iss_video_format_info(format->code);
		iss_reg_update(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_CGAMMAWD,
			       ISIF_CGAMMAWD_GWDI_MASK,
			       ISIF_CGAMMAWD_GWDI(info->bpp));

		/* Set RAW Bayer pattern */
		iss_reg_write(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_CCOLP,
			      isif_ccolp);
		break;
	}

	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_SPH, 0 & ISIF_SPH_MASK);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_LNH,
		      (format->width - 1) & ISIF_LNH_MASK);
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_LNV,
		      (format->height - 1) & ISIF_LNV_MASK);

	/* Generate ISIF0 on the last line of the image */
	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_VDINT(0),
		      format->height - 1);

	/* IPIPEIF_PAD_SOURCE_ISIF_SF */
	format = &ipipeif->formats[IPIPEIF_PAD_SOURCE_ISIF_SF];

	iss_reg_write(iss, OMAP4_ISS_MEM_ISP_ISIF, ISIF_HSIZE,
		      (ipipeif->video_out.bpl_value >> 5) &
		      ISIF_HSIZE_HSIZE_MASK);

	/* IPIPEIF_PAD_SOURCE_VP */
	/* Do nothing? */
}

/* -----------------------------------------------------------------------------
 * Interrupt handling
 */

static void ipipeif_isr_buffer(struct iss_ipipeif_device *ipipeif)
{
	struct iss_buffer *buffer;

	/* The ISIF generates VD0 interrupts even when writes are disabled.
	 * deal with it anyway). Disabling the ISIF when no buffer is available
	 * is thus not be enough, we need to handle the situation explicitly.
	 */
	if (list_empty(&ipipeif->video_out.dmaqueue))
		return;

	ipipeif_write_enable(ipipeif, 0);

	buffer = omap4iss_video_buffer_next(&ipipeif->video_out);
	if (buffer == NULL)
		return;

	ipipeif_set_outaddr(ipipeif, buffer->iss_addr);

	ipipeif_write_enable(ipipeif, 1);
}

/*
 * omap4iss_ipipeif_isr - Configure ipipeif during interframe time.
 * @ipipeif: Pointer to ISP IPIPEIF device.
 * @events: IPIPEIF events
 */
void omap4iss_ipipeif_isr(struct iss_ipipeif_device *ipipeif, u32 events)
{
	if (omap4iss_module_sync_is_stopping(&ipipeif->wait,
					     &ipipeif->stopping))
		return;

	if ((events & ISP5_IRQ_ISIF_INT(0)) &&
	    (ipipeif->output & IPIPEIF_OUTPUT_MEMORY))
		ipipeif_isr_buffer(ipipeif);
}

/* -----------------------------------------------------------------------------
 * ISP video operations
 */

static int ipipeif_video_queue(struct iss_video *video,
			       struct iss_buffer *buffer)
{
	struct iss_ipipeif_device *ipipeif = container_of(video,
				struct iss_ipipeif_device, video_out);

	if (!(ipipeif->output & IPIPEIF_OUTPUT_MEMORY))
		return -ENODEV;

	ipipeif_set_outaddr(ipipeif, buffer->iss_addr);

	/*
	 * If streaming was enabled before there was a buffer queued
	 * or underrun happened in the ISR, the hardware was not enabled
	 * and DMA queue flag ISS_VIDEO_DMAQUEUE_UNDERRUN is still set.
	 * Enable it now.
	 */
	if (video->dmaqueue_flags & ISS_VIDEO_DMAQUEUE_UNDERRUN) {
		if (ipipeif->output & IPIPEIF_OUTPUT_MEMORY)
			ipipeif_write_enable(ipipeif, 1);
		ipipeif_enable(ipipeif, 1);
		iss_video_dmaqueue_flags_clr(video);
	}

	return 0;
}

static const struct iss_video_operations ipipeif_video_ops = {
	.queue = ipipeif_video_queue,
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

#define IPIPEIF_DRV_SUBCLK_MASK	(OMAP4_ISS_ISP_SUBCLK_IPIPEIF |\
				 OMAP4_ISS_ISP_SUBCLK_ISIF)
/*
 * ipipeif_set_stream - Enable/Disable streaming on the IPIPEIF module
 * @sd: ISP IPIPEIF V4L2 subdevice
 * @enable: Enable/disable stream
 */
static int ipipeif_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct iss_ipipeif_device *ipipeif = v4l2_get_subdevdata(sd);
	struct iss_device *iss = to_iss_device(ipipeif);
	struct iss_video *video_out = &ipipeif->video_out;
	int ret = 0;

	if (ipipeif->state == ISS_PIPELINE_STREAM_STOPPED) {
		if (enable == ISS_PIPELINE_STREAM_STOPPED)
			return 0;

		omap4iss_isp_subclk_enable(iss, IPIPEIF_DRV_SUBCLK_MASK);
	}

	switch (enable) {
	case ISS_PIPELINE_STREAM_CONTINUOUS:

		ipipeif_configure(ipipeif);
		ipipeif_print_status(ipipeif);

		/*
		 * When outputting to memory with no buffer available, let the
		 * buffer queue handler start the hardware. A DMA queue flag
		 * ISS_VIDEO_DMAQUEUE_QUEUED will be set as soon as there is
		 * a buffer available.
		 */
		if (ipipeif->output & IPIPEIF_OUTPUT_MEMORY &&
		    !(video_out->dmaqueue_flags & ISS_VIDEO_DMAQUEUE_QUEUED))
			break;

		atomic_set(&ipipeif->stopping, 0);
		if (ipipeif->output & IPIPEIF_OUTPUT_MEMORY)
			ipipeif_write_enable(ipipeif, 1);
		ipipeif_enable(ipipeif, 1);
		iss_video_dmaqueue_flags_clr(video_out);
		break;

	case ISS_PIPELINE_STREAM_STOPPED:
		if (ipipeif->state == ISS_PIPELINE_STREAM_STOPPED)
			return 0;
		if (omap4iss_module_sync_idle(&sd->entity, &ipipeif->wait,
					      &ipipeif->stopping))
			ret = -ETIMEDOUT;

		if (ipipeif->output & IPIPEIF_OUTPUT_MEMORY)
			ipipeif_write_enable(ipipeif, 0);
		ipipeif_enable(ipipeif, 0);
		omap4iss_isp_subclk_disable(iss, IPIPEIF_DRV_SUBCLK_MASK);
		iss_video_dmaqueue_flags_clr(video_out);
		break;
	}

	ipipeif->state = enable;
	return ret;
}

static struct v4l2_mbus_framefmt *
__ipipeif_get_format(struct iss_ipipeif_device *ipipeif,
		     struct v4l2_subdev_pad_config *cfg, unsigned int pad,
		     enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&ipipeif->subdev, cfg, pad);
	return &ipipeif->formats[pad];
}

/*
 * ipipeif_try_format - Try video format on a pad
 * @ipipeif: ISS IPIPEIF device
 * @cfg: V4L2 subdev pad config
 * @pad: Pad number
 * @fmt: Format
 */
static void
ipipeif_try_format(struct iss_ipipeif_device *ipipeif,
		   struct v4l2_subdev_pad_config *cfg, unsigned int pad,
		   struct v4l2_mbus_framefmt *fmt,
		   enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt *format;
	unsigned int width = fmt->width;
	unsigned int height = fmt->height;
	unsigned int i;

	switch (pad) {
	case IPIPEIF_PAD_SINK:
		/* TODO: If the IPIPEIF output formatter pad is connected
		 * directly to the resizer, only YUV formats can be used.
		 */
		for (i = 0; i < ARRAY_SIZE(ipipeif_fmts); i++) {
			if (fmt->code == ipipeif_fmts[i])
				break;
		}

		/* If not found, use SGRBG10 as default */
		if (i >= ARRAY_SIZE(ipipeif_fmts))
			fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;

		/* Clamp the input size. */
		fmt->width = clamp_t(u32, width, 1, 8192);
		fmt->height = clamp_t(u32, height, 1, 8192);
		break;

	case IPIPEIF_PAD_SOURCE_ISIF_SF:
		format = __ipipeif_get_format(ipipeif, cfg, IPIPEIF_PAD_SINK,
					      which);
		memcpy(fmt, format, sizeof(*fmt));

		/* The data formatter truncates the number of horizontal output
		 * pixels to a multiple of 16. To avoid clipping data, allow
		 * callers to request an output size bigger than the input size
		 * up to the nearest multiple of 16.
		 */
		fmt->width = clamp_t(u32, width, 32, (fmt->width + 15) & ~15);
		fmt->width &= ~15;
		fmt->height = clamp_t(u32, height, 32, fmt->height);
		break;

	case IPIPEIF_PAD_SOURCE_VP:
		format = __ipipeif_get_format(ipipeif, cfg, IPIPEIF_PAD_SINK,
					      which);
		memcpy(fmt, format, sizeof(*fmt));

		fmt->width = clamp_t(u32, width, 32, fmt->width);
		fmt->height = clamp_t(u32, height, 32, fmt->height);
		break;
	}

	/* Data is written to memory unpacked, each 10-bit or 12-bit pixel is
	 * stored on 2 bytes.
	 */
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->field = V4L2_FIELD_NONE;
}

/*
 * ipipeif_enum_mbus_code - Handle pixel format enumeration
 * @sd     : pointer to v4l2 subdev structure
 * @cfg    : V4L2 subdev pad config
 * @code   : pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int ipipeif_enum_mbus_code(struct v4l2_subdev *sd,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_mbus_code_enum *code)
{
	struct iss_ipipeif_device *ipipeif = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	switch (code->pad) {
	case IPIPEIF_PAD_SINK:
		if (code->index >= ARRAY_SIZE(ipipeif_fmts))
			return -EINVAL;

		code->code = ipipeif_fmts[code->index];
		break;

	case IPIPEIF_PAD_SOURCE_ISIF_SF:
	case IPIPEIF_PAD_SOURCE_VP:
		/* No format conversion inside IPIPEIF */
		if (code->index != 0)
			return -EINVAL;

		format = __ipipeif_get_format(ipipeif, cfg, IPIPEIF_PAD_SINK,
					      code->which);

		code->code = format->code;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int ipipeif_enum_frame_size(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_size_enum *fse)
{
	struct iss_ipipeif_device *ipipeif = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	ipipeif_try_format(ipipeif, cfg, fse->pad, &format, fse->which);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	ipipeif_try_format(ipipeif, cfg, fse->pad, &format, fse->which);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * ipipeif_get_format - Retrieve the video format on a pad
 * @sd : ISP IPIPEIF V4L2 subdevice
 * @cfg: V4L2 subdev pad config
 * @fmt: Format
 *
 * Return 0 on success or -EINVAL if the pad is invalid or doesn't correspond
 * to the format type.
 */
static int ipipeif_get_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct iss_ipipeif_device *ipipeif = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __ipipeif_get_format(ipipeif, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

/*
 * ipipeif_set_format - Set the video format on a pad
 * @sd : ISP IPIPEIF V4L2 subdevice
 * @cfg: V4L2 subdev pad config
 * @fmt: Format
 *
 * Return 0 on success or -EINVAL if the pad is invalid or doesn't correspond
 * to the format type.
 */
static int ipipeif_set_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt)
{
	struct iss_ipipeif_device *ipipeif = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __ipipeif_get_format(ipipeif, cfg, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	ipipeif_try_format(ipipeif, cfg, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	/* Propagate the format from sink to source */
	if (fmt->pad == IPIPEIF_PAD_SINK) {
		format = __ipipeif_get_format(ipipeif, cfg,
					      IPIPEIF_PAD_SOURCE_ISIF_SF,
					      fmt->which);
		*format = fmt->format;
		ipipeif_try_format(ipipeif, cfg, IPIPEIF_PAD_SOURCE_ISIF_SF,
				   format, fmt->which);

		format = __ipipeif_get_format(ipipeif, cfg,
					      IPIPEIF_PAD_SOURCE_VP,
					      fmt->which);
		*format = fmt->format;
		ipipeif_try_format(ipipeif, cfg, IPIPEIF_PAD_SOURCE_VP, format,
				fmt->which);
	}

	return 0;
}

static int ipipeif_link_validate(struct v4l2_subdev *sd,
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
 * ipipeif_init_formats - Initialize formats on all pads
 * @sd: ISP IPIPEIF V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static int ipipeif_init_formats(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));
	format.pad = IPIPEIF_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = MEDIA_BUS_FMT_SGRBG10_1X10;
	format.format.width = 4096;
	format.format.height = 4096;
	ipipeif_set_format(sd, fh ? fh->pad : NULL, &format);

	return 0;
}

/* V4L2 subdev video operations */
static const struct v4l2_subdev_video_ops ipipeif_v4l2_video_ops = {
	.s_stream = ipipeif_set_stream,
};

/* V4L2 subdev pad operations */
static const struct v4l2_subdev_pad_ops ipipeif_v4l2_pad_ops = {
	.enum_mbus_code = ipipeif_enum_mbus_code,
	.enum_frame_size = ipipeif_enum_frame_size,
	.get_fmt = ipipeif_get_format,
	.set_fmt = ipipeif_set_format,
	.link_validate = ipipeif_link_validate,
};

/* V4L2 subdev operations */
static const struct v4l2_subdev_ops ipipeif_v4l2_ops = {
	.video = &ipipeif_v4l2_video_ops,
	.pad = &ipipeif_v4l2_pad_ops,
};

/* V4L2 subdev internal operations */
static const struct v4l2_subdev_internal_ops ipipeif_v4l2_internal_ops = {
	.open = ipipeif_init_formats,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

/*
 * ipipeif_link_setup - Setup IPIPEIF connections
 * @entity: IPIPEIF media entity
 * @local: Pad at the local end of the link
 * @remote: Pad at the remote end of the link
 * @flags: Link flags
 *
 * return -EINVAL or zero on success
 */
static int ipipeif_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct iss_ipipeif_device *ipipeif = v4l2_get_subdevdata(sd);
	struct iss_device *iss = to_iss_device(ipipeif);

	switch (local->index | media_entity_type(remote->entity)) {
	case IPIPEIF_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		/* Read from the sensor CSI2a or CSI2b. */
		if (!(flags & MEDIA_LNK_FL_ENABLED)) {
			ipipeif->input = IPIPEIF_INPUT_NONE;
			break;
		}

		if (ipipeif->input != IPIPEIF_INPUT_NONE)
			return -EBUSY;

		if (remote->entity == &iss->csi2a.subdev.entity)
			ipipeif->input = IPIPEIF_INPUT_CSI2A;
		else if (remote->entity == &iss->csi2b.subdev.entity)
			ipipeif->input = IPIPEIF_INPUT_CSI2B;

		break;

	case IPIPEIF_PAD_SOURCE_ISIF_SF | MEDIA_ENT_T_DEVNODE:
		/* Write to memory */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (ipipeif->output & ~IPIPEIF_OUTPUT_MEMORY)
				return -EBUSY;
			ipipeif->output |= IPIPEIF_OUTPUT_MEMORY;
		} else {
			ipipeif->output &= ~IPIPEIF_OUTPUT_MEMORY;
		}
		break;

	case IPIPEIF_PAD_SOURCE_VP | MEDIA_ENT_T_V4L2_SUBDEV:
		/* Send to IPIPE/RESIZER */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (ipipeif->output & ~IPIPEIF_OUTPUT_VP)
				return -EBUSY;
			ipipeif->output |= IPIPEIF_OUTPUT_VP;
		} else {
			ipipeif->output &= ~IPIPEIF_OUTPUT_VP;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* media operations */
static const struct media_entity_operations ipipeif_media_ops = {
	.link_setup = ipipeif_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

/*
 * ipipeif_init_entities - Initialize V4L2 subdev and media entity
 * @ipipeif: ISS ISP IPIPEIF module
 *
 * Return 0 on success and a negative error code on failure.
 */
static int ipipeif_init_entities(struct iss_ipipeif_device *ipipeif)
{
	struct v4l2_subdev *sd = &ipipeif->subdev;
	struct media_pad *pads = ipipeif->pads;
	struct media_entity *me = &sd->entity;
	int ret;

	ipipeif->input = IPIPEIF_INPUT_NONE;

	v4l2_subdev_init(sd, &ipipeif_v4l2_ops);
	sd->internal_ops = &ipipeif_v4l2_internal_ops;
	strlcpy(sd->name, "OMAP4 ISS ISP IPIPEIF", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for iss subdevs */
	v4l2_set_subdevdata(sd, ipipeif);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[IPIPEIF_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[IPIPEIF_PAD_SOURCE_ISIF_SF].flags = MEDIA_PAD_FL_SOURCE;
	pads[IPIPEIF_PAD_SOURCE_VP].flags = MEDIA_PAD_FL_SOURCE;

	me->ops = &ipipeif_media_ops;
	ret = media_entity_init(me, IPIPEIF_PADS_NUM, pads, 0);
	if (ret < 0)
		return ret;

	ipipeif_init_formats(sd, NULL);

	ipipeif->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ipipeif->video_out.ops = &ipipeif_video_ops;
	ipipeif->video_out.iss = to_iss_device(ipipeif);
	ipipeif->video_out.capture_mem = PAGE_ALIGN(4096 * 4096) * 3;
	ipipeif->video_out.bpl_alignment = 32;
	ipipeif->video_out.bpl_zero_padding = 1;
	ipipeif->video_out.bpl_max = 0x1ffe0;

	ret = omap4iss_video_init(&ipipeif->video_out, "ISP IPIPEIF");
	if (ret < 0)
		return ret;

	/* Connect the IPIPEIF subdev to the video node. */
	ret = media_entity_create_link(&ipipeif->subdev.entity,
				       IPIPEIF_PAD_SOURCE_ISIF_SF,
				       &ipipeif->video_out.video.entity, 0, 0);
	if (ret < 0)
		return ret;

	return 0;
}

void omap4iss_ipipeif_unregister_entities(struct iss_ipipeif_device *ipipeif)
{
	v4l2_device_unregister_subdev(&ipipeif->subdev);
	omap4iss_video_unregister(&ipipeif->video_out);
}

int omap4iss_ipipeif_register_entities(struct iss_ipipeif_device *ipipeif,
	struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev and video node. */
	ret = v4l2_device_register_subdev(vdev, &ipipeif->subdev);
	if (ret < 0)
		goto error;

	ret = omap4iss_video_register(&ipipeif->video_out, vdev);
	if (ret < 0)
		goto error;

	return 0;

error:
	omap4iss_ipipeif_unregister_entities(ipipeif);
	return ret;
}

/* -----------------------------------------------------------------------------
 * ISP IPIPEIF initialisation and cleanup
 */

/*
 * omap4iss_ipipeif_init - IPIPEIF module initialization.
 * @iss: Device pointer specific to the OMAP4 ISS.
 *
 * TODO: Get the initialisation values from platform data.
 *
 * Return 0 on success or a negative error code otherwise.
 */
int omap4iss_ipipeif_init(struct iss_device *iss)
{
	struct iss_ipipeif_device *ipipeif = &iss->ipipeif;

	ipipeif->state = ISS_PIPELINE_STREAM_STOPPED;
	init_waitqueue_head(&ipipeif->wait);

	return ipipeif_init_entities(ipipeif);
}

/*
 * omap4iss_ipipeif_cleanup - IPIPEIF module cleanup.
 * @iss: Device pointer specific to the OMAP4 ISS.
 */
void omap4iss_ipipeif_cleanup(struct iss_device *iss)
{
	struct iss_ipipeif_device *ipipeif = &iss->ipipeif;

	media_entity_cleanup(&ipipeif->subdev.entity);
}
