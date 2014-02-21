/*
 * ispresizer.c
 *
 * TI OMAP3 ISP - Resizer module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/module.h>

#include "isp.h"
#include "ispreg.h"
#include "ispresizer.h"

/*
 * Resizer Constants
 */
#define MIN_RESIZE_VALUE		64
#define MID_RESIZE_VALUE		512
#define MAX_RESIZE_VALUE		1024

#define MIN_IN_WIDTH			32
#define MIN_IN_HEIGHT			32
#define MAX_IN_WIDTH_MEMORY_MODE	4095
#define MAX_IN_WIDTH_ONTHEFLY_MODE_ES1	1280
#define MAX_IN_WIDTH_ONTHEFLY_MODE_ES2	4095
#define MAX_IN_HEIGHT			4095

#define MIN_OUT_WIDTH			16
#define MIN_OUT_HEIGHT			2
#define MAX_OUT_HEIGHT			4095

/*
 * Resizer Use Constraints
 * "TRM ES3.1, table 12-46"
 */
#define MAX_4TAP_OUT_WIDTH_ES1		1280
#define MAX_7TAP_OUT_WIDTH_ES1		640
#define MAX_4TAP_OUT_WIDTH_ES2		3312
#define MAX_7TAP_OUT_WIDTH_ES2		1650
#define MAX_4TAP_OUT_WIDTH_3630		4096
#define MAX_7TAP_OUT_WIDTH_3630		2048

/*
 * Constants for ratio calculation
 */
#define RESIZE_DIVISOR			256
#define DEFAULT_PHASE			1

/*
 * Default (and only) configuration of filter coefficients.
 * 7-tap mode is for scale factors 0.25x to 0.5x.
 * 4-tap mode is for scale factors 0.5x to 4.0x.
 * There shouldn't be any reason to recalculate these, EVER.
 */
static const struct isprsz_coef filter_coefs = {
	/* For 8-phase 4-tap horizontal filter: */
	{
		0x0000, 0x0100, 0x0000, 0x0000,
		0x03FA, 0x00F6, 0x0010, 0x0000,
		0x03F9, 0x00DB, 0x002C, 0x0000,
		0x03FB, 0x00B3, 0x0053, 0x03FF,
		0x03FD, 0x0082, 0x0084, 0x03FD,
		0x03FF, 0x0053, 0x00B3, 0x03FB,
		0x0000, 0x002C, 0x00DB, 0x03F9,
		0x0000, 0x0010, 0x00F6, 0x03FA
	},
	/* For 8-phase 4-tap vertical filter: */
	{
		0x0000, 0x0100, 0x0000, 0x0000,
		0x03FA, 0x00F6, 0x0010, 0x0000,
		0x03F9, 0x00DB, 0x002C, 0x0000,
		0x03FB, 0x00B3, 0x0053, 0x03FF,
		0x03FD, 0x0082, 0x0084, 0x03FD,
		0x03FF, 0x0053, 0x00B3, 0x03FB,
		0x0000, 0x002C, 0x00DB, 0x03F9,
		0x0000, 0x0010, 0x00F6, 0x03FA
	},
	/* For 4-phase 7-tap horizontal filter: */
	#define DUMMY 0
	{
		0x0004, 0x0023, 0x005A, 0x0058, 0x0023, 0x0004, 0x0000, DUMMY,
		0x0002, 0x0018, 0x004d, 0x0060, 0x0031, 0x0008, 0x0000, DUMMY,
		0x0001, 0x000f, 0x003f, 0x0062, 0x003f, 0x000f, 0x0001, DUMMY,
		0x0000, 0x0008, 0x0031, 0x0060, 0x004d, 0x0018, 0x0002, DUMMY
	},
	/* For 4-phase 7-tap vertical filter: */
	{
		0x0004, 0x0023, 0x005A, 0x0058, 0x0023, 0x0004, 0x0000, DUMMY,
		0x0002, 0x0018, 0x004d, 0x0060, 0x0031, 0x0008, 0x0000, DUMMY,
		0x0001, 0x000f, 0x003f, 0x0062, 0x003f, 0x000f, 0x0001, DUMMY,
		0x0000, 0x0008, 0x0031, 0x0060, 0x004d, 0x0018, 0x0002, DUMMY
	}
	/*
	 * The dummy padding is required in 7-tap mode because of how the
	 * registers are arranged physically.
	 */
	#undef DUMMY
};

/*
 * __resizer_get_format - helper function for getting resizer format
 * @res   : pointer to resizer private structure
 * @pad   : pad number
 * @fh    : V4L2 subdev file handle
 * @which : wanted subdev format
 * return zero
 */
static struct v4l2_mbus_framefmt *
__resizer_get_format(struct isp_res_device *res, struct v4l2_subdev_fh *fh,
		     unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(fh, pad);
	else
		return &res->formats[pad];
}

/*
 * __resizer_get_crop - helper function for getting resizer crop rectangle
 * @res   : pointer to resizer private structure
 * @fh    : V4L2 subdev file handle
 * @which : wanted subdev crop rectangle
 */
static struct v4l2_rect *
__resizer_get_crop(struct isp_res_device *res, struct v4l2_subdev_fh *fh,
		   enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(fh, RESZ_PAD_SINK);
	else
		return &res->crop.request;
}

/*
 * resizer_set_filters - Set resizer filters
 * @res: Device context.
 * @h_coeff: horizontal coefficient
 * @v_coeff: vertical coefficient
 * Return none
 */
static void resizer_set_filters(struct isp_res_device *res, const u16 *h_coeff,
				const u16 *v_coeff)
{
	struct isp_device *isp = to_isp_device(res);
	u32 startaddr_h, startaddr_v, tmp_h, tmp_v;
	int i;

	startaddr_h = ISPRSZ_HFILT10;
	startaddr_v = ISPRSZ_VFILT10;

	for (i = 0; i < COEFF_CNT; i += 2) {
		tmp_h = h_coeff[i] |
			(h_coeff[i + 1] << ISPRSZ_HFILT_COEF1_SHIFT);
		tmp_v = v_coeff[i] |
			(v_coeff[i + 1] << ISPRSZ_VFILT_COEF1_SHIFT);
		isp_reg_writel(isp, tmp_h, OMAP3_ISP_IOMEM_RESZ, startaddr_h);
		isp_reg_writel(isp, tmp_v, OMAP3_ISP_IOMEM_RESZ, startaddr_v);
		startaddr_h += 4;
		startaddr_v += 4;
	}
}

/*
 * resizer_set_bilinear - Chrominance horizontal algorithm select
 * @res: Device context.
 * @type: Filtering interpolation type.
 *
 * Filtering that is same as luminance processing is
 * intended only for downsampling, and bilinear interpolation
 * is intended only for upsampling.
 */
static void resizer_set_bilinear(struct isp_res_device *res,
				 enum resizer_chroma_algo type)
{
	struct isp_device *isp = to_isp_device(res);

	if (type == RSZ_BILINEAR)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
			    ISPRSZ_CNT_CBILIN);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
			    ISPRSZ_CNT_CBILIN);
}

/*
 * resizer_set_ycpos - Luminance and chrominance order
 * @res: Device context.
 * @pixelcode: pixel code.
 */
static void resizer_set_ycpos(struct isp_res_device *res,
			      enum v4l2_mbus_pixelcode pixelcode)
{
	struct isp_device *isp = to_isp_device(res);

	switch (pixelcode) {
	case V4L2_MBUS_FMT_YUYV8_1X16:
		isp_reg_set(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
			    ISPRSZ_CNT_YCPOS);
		break;
	case V4L2_MBUS_FMT_UYVY8_1X16:
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
			    ISPRSZ_CNT_YCPOS);
		break;
	default:
		return;
	}
}

/*
 * resizer_set_phase - Setup horizontal and vertical starting phase
 * @res: Device context.
 * @h_phase: horizontal phase parameters.
 * @v_phase: vertical phase parameters.
 *
 * Horizontal and vertical phase range is 0 to 7
 */
static void resizer_set_phase(struct isp_res_device *res, u32 h_phase,
			      u32 v_phase)
{
	struct isp_device *isp = to_isp_device(res);
	u32 rgval = 0;

	rgval = isp_reg_readl(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT) &
	      ~(ISPRSZ_CNT_HSTPH_MASK | ISPRSZ_CNT_VSTPH_MASK);
	rgval |= (h_phase << ISPRSZ_CNT_HSTPH_SHIFT) & ISPRSZ_CNT_HSTPH_MASK;
	rgval |= (v_phase << ISPRSZ_CNT_VSTPH_SHIFT) & ISPRSZ_CNT_VSTPH_MASK;

	isp_reg_writel(isp, rgval, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT);
}

/*
 * resizer_set_luma - Setup luminance enhancer parameters
 * @res: Device context.
 * @luma: Structure for luminance enhancer parameters.
 *
 * Algorithm select:
 *  0x0: Disable
 *  0x1: [-1  2 -1]/2 high-pass filter
 *  0x2: [-1 -2  6 -2 -1]/4 high-pass filter
 *
 * Maximum gain:
 *  The data is coded in U4Q4 representation.
 *
 * Slope:
 *  The data is coded in U4Q4 representation.
 *
 * Coring offset:
 *  The data is coded in U8Q0 representation.
 *
 * The new luminance value is computed as:
 *  Y += HPF(Y) x max(GAIN, (HPF(Y) - CORE) x SLOP + 8) >> 4.
 */
static void resizer_set_luma(struct isp_res_device *res,
			     struct resizer_luma_yenh *luma)
{
	struct isp_device *isp = to_isp_device(res);
	u32 rgval = 0;

	rgval  = (luma->algo << ISPRSZ_YENH_ALGO_SHIFT)
		  & ISPRSZ_YENH_ALGO_MASK;
	rgval |= (luma->gain << ISPRSZ_YENH_GAIN_SHIFT)
		  & ISPRSZ_YENH_GAIN_MASK;
	rgval |= (luma->slope << ISPRSZ_YENH_SLOP_SHIFT)
		  & ISPRSZ_YENH_SLOP_MASK;
	rgval |= (luma->core << ISPRSZ_YENH_CORE_SHIFT)
		  & ISPRSZ_YENH_CORE_MASK;

	isp_reg_writel(isp, rgval, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_YENH);
}

/*
 * resizer_set_source - Input source select
 * @res: Device context.
 * @source: Input source type
 *
 * If this field is set to RESIZER_INPUT_VP, the resizer input is fed from
 * Preview/CCDC engine, otherwise from memory.
 */
static void resizer_set_source(struct isp_res_device *res,
			       enum resizer_input_entity source)
{
	struct isp_device *isp = to_isp_device(res);

	if (source == RESIZER_INPUT_MEMORY)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
			    ISPRSZ_CNT_INPSRC);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
			    ISPRSZ_CNT_INPSRC);
}

/*
 * resizer_set_ratio - Setup horizontal and vertical resizing value
 * @res: Device context.
 * @ratio: Structure for ratio parameters.
 *
 * Resizing range from 64 to 1024
 */
static void resizer_set_ratio(struct isp_res_device *res,
			      const struct resizer_ratio *ratio)
{
	struct isp_device *isp = to_isp_device(res);
	const u16 *h_filter, *v_filter;
	u32 rgval = 0;

	rgval = isp_reg_readl(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT) &
			      ~(ISPRSZ_CNT_HRSZ_MASK | ISPRSZ_CNT_VRSZ_MASK);
	rgval |= ((ratio->horz - 1) << ISPRSZ_CNT_HRSZ_SHIFT)
		  & ISPRSZ_CNT_HRSZ_MASK;
	rgval |= ((ratio->vert - 1) << ISPRSZ_CNT_VRSZ_SHIFT)
		  & ISPRSZ_CNT_VRSZ_MASK;
	isp_reg_writel(isp, rgval, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT);

	/* prepare horizontal filter coefficients */
	if (ratio->horz > MID_RESIZE_VALUE)
		h_filter = &filter_coefs.h_filter_coef_7tap[0];
	else
		h_filter = &filter_coefs.h_filter_coef_4tap[0];

	/* prepare vertical filter coefficients */
	if (ratio->vert > MID_RESIZE_VALUE)
		v_filter = &filter_coefs.v_filter_coef_7tap[0];
	else
		v_filter = &filter_coefs.v_filter_coef_4tap[0];

	resizer_set_filters(res, h_filter, v_filter);
}

/*
 * resizer_set_dst_size - Setup the output height and width
 * @res: Device context.
 * @width: Output width.
 * @height: Output height.
 *
 * Width :
 *  The value must be EVEN.
 *
 * Height:
 *  The number of bytes written to SDRAM must be
 *  a multiple of 16-bytes if the vertical resizing factor
 *  is greater than 1x (upsizing)
 */
static void resizer_set_output_size(struct isp_res_device *res,
				    u32 width, u32 height)
{
	struct isp_device *isp = to_isp_device(res);
	u32 rgval = 0;

	dev_dbg(isp->dev, "Output size[w/h]: %dx%d\n", width, height);
	rgval  = (width << ISPRSZ_OUT_SIZE_HORZ_SHIFT)
		 & ISPRSZ_OUT_SIZE_HORZ_MASK;
	rgval |= (height << ISPRSZ_OUT_SIZE_VERT_SHIFT)
		 & ISPRSZ_OUT_SIZE_VERT_MASK;
	isp_reg_writel(isp, rgval, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_OUT_SIZE);
}

/*
 * resizer_set_output_offset - Setup memory offset for the output lines.
 * @res: Device context.
 * @offset: Memory offset.
 *
 * The 5 LSBs are forced to be zeros by the hardware to align on a 32-byte
 * boundary; the 5 LSBs are read-only. For optimal use of SDRAM bandwidth,
 * the SDRAM line offset must be set on a 256-byte boundary
 */
static void resizer_set_output_offset(struct isp_res_device *res, u32 offset)
{
	struct isp_device *isp = to_isp_device(res);

	isp_reg_writel(isp, offset, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTOFF);
}

/*
 * resizer_set_start - Setup vertical and horizontal start position
 * @res: Device context.
 * @left: Horizontal start position.
 * @top: Vertical start position.
 *
 * Vertical start line:
 *  This field makes sense only when the resizer obtains its input
 *  from the preview engine/CCDC
 *
 * Horizontal start pixel:
 *  Pixels are coded on 16 bits for YUV and 8 bits for color separate data.
 *  When the resizer gets its input from SDRAM, this field must be set
 *  to <= 15 for YUV 16-bit data and <= 31 for 8-bit color separate data
 */
static void resizer_set_start(struct isp_res_device *res, u32 left, u32 top)
{
	struct isp_device *isp = to_isp_device(res);
	u32 rgval = 0;

	rgval = (left << ISPRSZ_IN_START_HORZ_ST_SHIFT)
		& ISPRSZ_IN_START_HORZ_ST_MASK;
	rgval |= (top << ISPRSZ_IN_START_VERT_ST_SHIFT)
		 & ISPRSZ_IN_START_VERT_ST_MASK;

	isp_reg_writel(isp, rgval, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_START);
}

/*
 * resizer_set_input_size - Setup the input size
 * @res: Device context.
 * @width: The range is 0 to 4095 pixels
 * @height: The range is 0 to 4095 lines
 */
static void resizer_set_input_size(struct isp_res_device *res,
				   u32 width, u32 height)
{
	struct isp_device *isp = to_isp_device(res);
	u32 rgval = 0;

	dev_dbg(isp->dev, "Input size[w/h]: %dx%d\n", width, height);

	rgval = (width << ISPRSZ_IN_SIZE_HORZ_SHIFT)
		& ISPRSZ_IN_SIZE_HORZ_MASK;
	rgval |= (height << ISPRSZ_IN_SIZE_VERT_SHIFT)
		 & ISPRSZ_IN_SIZE_VERT_MASK;

	isp_reg_writel(isp, rgval, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_IN_SIZE);
}

/*
 * resizer_set_src_offs - Setup the memory offset for the input lines
 * @res: Device context.
 * @offset: Memory offset.
 *
 * The 5 LSBs are forced to be zeros by the hardware to align on a 32-byte
 * boundary; the 5 LSBs are read-only. This field must be programmed to be
 * 0x0 if the resizer input is from preview engine/CCDC.
 */
static void resizer_set_input_offset(struct isp_res_device *res, u32 offset)
{
	struct isp_device *isp = to_isp_device(res);

	isp_reg_writel(isp, offset, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INOFF);
}

/*
 * resizer_set_intype - Input type select
 * @res: Device context.
 * @type: Pixel format type.
 */
static void resizer_set_intype(struct isp_res_device *res,
			       enum resizer_colors_type type)
{
	struct isp_device *isp = to_isp_device(res);

	if (type == RSZ_COLOR8)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
			    ISPRSZ_CNT_INPTYP);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_CNT,
			    ISPRSZ_CNT_INPTYP);
}

/*
 * __resizer_set_inaddr - Helper function for set input address
 * @res : pointer to resizer private data structure
 * @addr: input address
 * return none
 */
static void __resizer_set_inaddr(struct isp_res_device *res, u32 addr)
{
	struct isp_device *isp = to_isp_device(res);

	isp_reg_writel(isp, addr, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_INADD);
}

/*
 * The data rate at the horizontal resizer output must not exceed half the
 * functional clock or 100 MP/s, whichever is lower. According to the TRM
 * there's no similar requirement for the vertical resizer output. However
 * experience showed that vertical upscaling by 4 leads to SBL overflows (with
 * data rates at the resizer output exceeding 300 MP/s). Limiting the resizer
 * output data rate to the functional clock or 200 MP/s, whichever is lower,
 * seems to get rid of SBL overflows.
 *
 * The maximum data rate at the output of the horizontal resizer can thus be
 * computed with
 *
 * max intermediate rate <= L3 clock * input height / output height
 * max intermediate rate <= L3 clock / 2
 *
 * The maximum data rate at the resizer input is then
 *
 * max input rate <= max intermediate rate * input width / output width
 *
 * where the input width and height are the resizer input crop rectangle size.
 * The TRM doesn't clearly explain if that's a maximum instant data rate or a
 * maximum average data rate.
 */
void omap3isp_resizer_max_rate(struct isp_res_device *res,
			       unsigned int *max_rate)
{
	struct isp_pipeline *pipe = to_isp_pipeline(&res->subdev.entity);
	const struct v4l2_mbus_framefmt *ofmt = &res->formats[RESZ_PAD_SOURCE];
	unsigned long limit = min(pipe->l3_ick, 200000000UL);
	unsigned long clock;

	clock = div_u64((u64)limit * res->crop.active.height, ofmt->height);
	clock = min(clock, limit / 2);
	*max_rate = div_u64((u64)clock * res->crop.active.width, ofmt->width);
}

/*
 * When the resizer processes images from memory, the driver must slow down read
 * requests on the input to at least comply with the internal data rate
 * requirements. If the application real-time requirements can cope with slower
 * processing, the resizer can be slowed down even more to put less pressure on
 * the overall system.
 *
 * When the resizer processes images on the fly (either from the CCDC or the
 * preview module), the same data rate requirements apply but they can't be
 * enforced at the resizer level. The image input module (sensor, CCP2 or
 * preview module) must not provide image data faster than the resizer can
 * process.
 *
 * For live image pipelines, the data rate is set by the frame format, size and
 * rate. The sensor output frame rate must not exceed the maximum resizer data
 * rate.
 *
 * The resizer slows down read requests by inserting wait cycles in the SBL
 * requests. The maximum number of 256-byte requests per second can be computed
 * as (the data rate is multiplied by 2 to convert from pixels per second to
 * bytes per second)
 *
 * request per second = data rate * 2 / 256
 * cycles per request = cycles per second / requests per second
 *
 * The number of cycles per second is controlled by the L3 clock, leading to
 *
 * cycles per request = L3 frequency / 2 * 256 / data rate
 */
static void resizer_adjust_bandwidth(struct isp_res_device *res)
{
	struct isp_pipeline *pipe = to_isp_pipeline(&res->subdev.entity);
	struct isp_device *isp = to_isp_device(res);
	unsigned long l3_ick = pipe->l3_ick;
	struct v4l2_fract *timeperframe;
	unsigned int cycles_per_frame;
	unsigned int requests_per_frame;
	unsigned int cycles_per_request;
	unsigned int granularity;
	unsigned int minimum;
	unsigned int maximum;
	unsigned int value;

	if (res->input != RESIZER_INPUT_MEMORY) {
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_SBL, ISPSBL_SDR_REQ_EXP,
			    ISPSBL_SDR_REQ_RSZ_EXP_MASK);
		return;
	}

	switch (isp->revision) {
	case ISP_REVISION_1_0:
	case ISP_REVISION_2_0:
	default:
		granularity = 1024;
		break;

	case ISP_REVISION_15_0:
		granularity = 32;
		break;
	}

	/* Compute the minimum number of cycles per request, based on the
	 * pipeline maximum data rate. This is an absolute lower bound if we
	 * don't want SBL overflows, so round the value up.
	 */
	cycles_per_request = div_u64((u64)l3_ick / 2 * 256 + pipe->max_rate - 1,
				     pipe->max_rate);
	minimum = DIV_ROUND_UP(cycles_per_request, granularity);

	/* Compute the maximum number of cycles per request, based on the
	 * requested frame rate. This is a soft upper bound to achieve a frame
	 * rate equal or higher than the requested value, so round the value
	 * down.
	 */
	timeperframe = &pipe->max_timeperframe;

	requests_per_frame = DIV_ROUND_UP(res->crop.active.width * 2, 256)
			   * res->crop.active.height;
	cycles_per_frame = div_u64((u64)l3_ick * timeperframe->numerator,
				   timeperframe->denominator);
	cycles_per_request = cycles_per_frame / requests_per_frame;

	maximum = cycles_per_request / granularity;

	value = max(minimum, maximum);

	dev_dbg(isp->dev, "%s: cycles per request = %u\n", __func__, value);
	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_SBL, ISPSBL_SDR_REQ_EXP,
			ISPSBL_SDR_REQ_RSZ_EXP_MASK,
			value << ISPSBL_SDR_REQ_RSZ_EXP_SHIFT);
}

/*
 * omap3isp_resizer_busy - Checks if ISP resizer is busy.
 *
 * Returns busy field from ISPRSZ_PCR register.
 */
int omap3isp_resizer_busy(struct isp_res_device *res)
{
	struct isp_device *isp = to_isp_device(res);

	return isp_reg_readl(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR) &
			     ISPRSZ_PCR_BUSY;
}

/*
 * resizer_set_inaddr - Sets the memory address of the input frame.
 * @addr: 32bit memory address aligned on 32byte boundary.
 */
static void resizer_set_inaddr(struct isp_res_device *res, u32 addr)
{
	res->addr_base = addr;

	/* This will handle crop settings in stream off state */
	if (res->crop_offset)
		addr += res->crop_offset & ~0x1f;

	__resizer_set_inaddr(res, addr);
}

/*
 * Configures the memory address to which the output frame is written.
 * @addr: 32bit memory address aligned on 32byte boundary.
 * Note: For SBL efficiency reasons the address should be on a 256-byte
 * boundary.
 */
static void resizer_set_outaddr(struct isp_res_device *res, u32 addr)
{
	struct isp_device *isp = to_isp_device(res);

	/*
	 * Set output address. This needs to be in its own function
	 * because it changes often.
	 */
	isp_reg_writel(isp, addr << ISPRSZ_SDR_OUTADD_ADDR_SHIFT,
		       OMAP3_ISP_IOMEM_RESZ, ISPRSZ_SDR_OUTADD);
}

/*
 * resizer_print_status - Prints the values of the resizer module registers.
 */
#define RSZ_PRINT_REGISTER(isp, name)\
	dev_dbg(isp->dev, "###RSZ " #name "=0x%08x\n", \
		isp_reg_readl(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_##name))

static void resizer_print_status(struct isp_res_device *res)
{
	struct isp_device *isp = to_isp_device(res);

	dev_dbg(isp->dev, "-------------Resizer Register dump----------\n");

	RSZ_PRINT_REGISTER(isp, PCR);
	RSZ_PRINT_REGISTER(isp, CNT);
	RSZ_PRINT_REGISTER(isp, OUT_SIZE);
	RSZ_PRINT_REGISTER(isp, IN_START);
	RSZ_PRINT_REGISTER(isp, IN_SIZE);
	RSZ_PRINT_REGISTER(isp, SDR_INADD);
	RSZ_PRINT_REGISTER(isp, SDR_INOFF);
	RSZ_PRINT_REGISTER(isp, SDR_OUTADD);
	RSZ_PRINT_REGISTER(isp, SDR_OUTOFF);
	RSZ_PRINT_REGISTER(isp, YENH);

	dev_dbg(isp->dev, "--------------------------------------------\n");
}

/*
 * resizer_calc_ratios - Helper function for calculating resizer ratios
 * @res: pointer to resizer private data structure
 * @input: input frame size
 * @output: output frame size
 * @ratio : return calculated ratios
 * return none
 *
 * The resizer uses a polyphase sample rate converter. The upsampling filter
 * has a fixed number of phases that depend on the resizing ratio. As the ratio
 * computation depends on the number of phases, we need to compute a first
 * approximation and then refine it.
 *
 * The input/output/ratio relationship is given by the OMAP34xx TRM:
 *
 * - 8-phase, 4-tap mode (RSZ = 64 ~ 512)
 *	iw = (32 * sph + (ow - 1) * hrsz + 16) >> 8 + 7
 *	ih = (32 * spv + (oh - 1) * vrsz + 16) >> 8 + 4
 * - 4-phase, 7-tap mode (RSZ = 513 ~ 1024)
 *	iw = (64 * sph + (ow - 1) * hrsz + 32) >> 8 + 7
 *	ih = (64 * spv + (oh - 1) * vrsz + 32) >> 8 + 7
 *
 * iw and ih are the input width and height after cropping. Those equations need
 * to be satisfied exactly for the resizer to work correctly.
 *
 * The equations can't be easily reverted, as the >> 8 operation is not linear.
 * In addition, not all input sizes can be achieved for a given output size. To
 * get the highest input size lower than or equal to the requested input size,
 * we need to compute the highest resizing ratio that satisfies the following
 * inequality (taking the 4-tap mode width equation as an example)
 *
 *	iw >= (32 * sph + (ow - 1) * hrsz + 16) >> 8 - 7
 *
 * (where iw is the requested input width) which can be rewritten as
 *
 *	  iw - 7            >= (32 * sph + (ow - 1) * hrsz + 16) >> 8
 *	 (iw - 7) << 8      >=  32 * sph + (ow - 1) * hrsz + 16 - b
 *	((iw - 7) << 8) + b >=  32 * sph + (ow - 1) * hrsz + 16
 *
 * where b is the value of the 8 least significant bits of the right hand side
 * expression of the last inequality. The highest resizing ratio value will be
 * achieved when b is equal to its maximum value of 255. That resizing ratio
 * value will still satisfy the original inequality, as b will disappear when
 * the expression will be shifted right by 8.
 *
 * The reverted equations thus become
 *
 * - 8-phase, 4-tap mode
 *	hrsz = ((iw - 7) * 256 + 255 - 16 - 32 * sph) / (ow - 1)
 *	vrsz = ((ih - 4) * 256 + 255 - 16 - 32 * spv) / (oh - 1)
 * - 4-phase, 7-tap mode
 *	hrsz = ((iw - 7) * 256 + 255 - 32 - 64 * sph) / (ow - 1)
 *	vrsz = ((ih - 7) * 256 + 255 - 32 - 64 * spv) / (oh - 1)
 *
 * The ratios are integer values, and are rounded down to ensure that the
 * cropped input size is not bigger than the uncropped input size.
 *
 * As the number of phases/taps, used to select the correct equations to compute
 * the ratio, depends on the ratio, we start with the 4-tap mode equations to
 * compute an approximation of the ratio, and switch to the 7-tap mode equations
 * if the approximation is higher than the ratio threshold.
 *
 * As the 7-tap mode equations will return a ratio smaller than or equal to the
 * 4-tap mode equations, the resulting ratio could become lower than or equal to
 * the ratio threshold. This 'equations loop' isn't an issue as long as the
 * correct equations are used to compute the final input size. Starting with the
 * 4-tap mode equations ensure that, in case of values resulting in a 'ratio
 * loop', the smallest of the ratio values will be used, never exceeding the
 * requested input size.
 *
 * We first clamp the output size according to the hardware capability to avoid
 * auto-cropping the input more than required to satisfy the TRM equations. The
 * minimum output size is achieved with a scaling factor of 1024. It is thus
 * computed using the 7-tap equations.
 *
 *	min ow = ((iw - 7) * 256 - 32 - 64 * sph) / 1024 + 1
 *	min oh = ((ih - 7) * 256 - 32 - 64 * spv) / 1024 + 1
 *
 * Similarly, the maximum output size is achieved with a scaling factor of 64
 * and computed using the 4-tap equations.
 *
 *	max ow = ((iw - 7) * 256 + 255 - 16 - 32 * sph) / 64 + 1
 *	max oh = ((ih - 4) * 256 + 255 - 16 - 32 * spv) / 64 + 1
 *
 * The additional +255 term compensates for the round down operation performed
 * by the TRM equations when shifting the value right by 8 bits.
 *
 * We then compute and clamp the ratios (x1/4 ~ x4). Clamping the output size to
 * the maximum value guarantees that the ratio value will never be smaller than
 * the minimum, but it could still slightly exceed the maximum. Clamping the
 * ratio will thus result in a resizing factor slightly larger than the
 * requested value.
 *
 * To accommodate that, and make sure the TRM equations are satisfied exactly, we
 * compute the input crop rectangle as the last step.
 *
 * As if the situation wasn't complex enough, the maximum output width depends
 * on the vertical resizing ratio.  Fortunately, the output height doesn't
 * depend on the horizontal resizing ratio. We can then start by computing the
 * output height and the vertical ratio, and then move to computing the output
 * width and the horizontal ratio.
 */
static void resizer_calc_ratios(struct isp_res_device *res,
				struct v4l2_rect *input,
				struct v4l2_mbus_framefmt *output,
				struct resizer_ratio *ratio)
{
	struct isp_device *isp = to_isp_device(res);
	const unsigned int spv = DEFAULT_PHASE;
	const unsigned int sph = DEFAULT_PHASE;
	unsigned int upscaled_width;
	unsigned int upscaled_height;
	unsigned int min_width;
	unsigned int min_height;
	unsigned int max_width;
	unsigned int max_height;
	unsigned int width_alignment;
	unsigned int width;
	unsigned int height;

	/*
	 * Clamp the output height based on the hardware capabilities and
	 * compute the vertical resizing ratio.
	 */
	min_height = ((input->height - 7) * 256 - 32 - 64 * spv) / 1024 + 1;
	min_height = max_t(unsigned int, min_height, MIN_OUT_HEIGHT);
	max_height = ((input->height - 4) * 256 + 255 - 16 - 32 * spv) / 64 + 1;
	max_height = min_t(unsigned int, max_height, MAX_OUT_HEIGHT);
	output->height = clamp(output->height, min_height, max_height);

	ratio->vert = ((input->height - 4) * 256 + 255 - 16 - 32 * spv)
		    / (output->height - 1);
	if (ratio->vert > MID_RESIZE_VALUE)
		ratio->vert = ((input->height - 7) * 256 + 255 - 32 - 64 * spv)
			    / (output->height - 1);
	ratio->vert = clamp_t(unsigned int, ratio->vert,
			      MIN_RESIZE_VALUE, MAX_RESIZE_VALUE);

	if (ratio->vert <= MID_RESIZE_VALUE) {
		upscaled_height = (output->height - 1) * ratio->vert
				+ 32 * spv + 16;
		height = (upscaled_height >> 8) + 4;
	} else {
		upscaled_height = (output->height - 1) * ratio->vert
				+ 64 * spv + 32;
		height = (upscaled_height >> 8) + 7;
	}

	/*
	 * Compute the minimum and maximum output widths based on the hardware
	 * capabilities. The maximum depends on the vertical resizing ratio.
	 */
	min_width = ((input->width - 7) * 256 - 32 - 64 * sph) / 1024 + 1;
	min_width = max_t(unsigned int, min_width, MIN_OUT_WIDTH);

	if (ratio->vert <= MID_RESIZE_VALUE) {
		switch (isp->revision) {
		case ISP_REVISION_1_0:
			max_width = MAX_4TAP_OUT_WIDTH_ES1;
			break;

		case ISP_REVISION_2_0:
		default:
			max_width = MAX_4TAP_OUT_WIDTH_ES2;
			break;

		case ISP_REVISION_15_0:
			max_width = MAX_4TAP_OUT_WIDTH_3630;
			break;
		}
	} else {
		switch (isp->revision) {
		case ISP_REVISION_1_0:
			max_width = MAX_7TAP_OUT_WIDTH_ES1;
			break;

		case ISP_REVISION_2_0:
		default:
			max_width = MAX_7TAP_OUT_WIDTH_ES2;
			break;

		case ISP_REVISION_15_0:
			max_width = MAX_7TAP_OUT_WIDTH_3630;
			break;
		}
	}
	max_width = min(((input->width - 7) * 256 + 255 - 16 - 32 * sph) / 64
			+ 1, max_width);

	/*
	 * The output width must be even, and must be a multiple of 16 bytes
	 * when upscaling vertically. Clamp the output width to the valid range.
	 * Take the alignment into account (the maximum width in 7-tap mode on
	 * ES2 isn't a multiple of 8) and align the result up to make sure it
	 * won't be smaller than the minimum.
	 */
	width_alignment = ratio->vert < 256 ? 8 : 2;
	output->width = clamp(output->width, min_width,
			      max_width & ~(width_alignment - 1));
	output->width = ALIGN(output->width, width_alignment);

	ratio->horz = ((input->width - 7) * 256 + 255 - 16 - 32 * sph)
		    / (output->width - 1);
	if (ratio->horz > MID_RESIZE_VALUE)
		ratio->horz = ((input->width - 7) * 256 + 255 - 32 - 64 * sph)
			    / (output->width - 1);
	ratio->horz = clamp_t(unsigned int, ratio->horz,
			      MIN_RESIZE_VALUE, MAX_RESIZE_VALUE);

	if (ratio->horz <= MID_RESIZE_VALUE) {
		upscaled_width = (output->width - 1) * ratio->horz
			       + 32 * sph + 16;
		width = (upscaled_width >> 8) + 7;
	} else {
		upscaled_width = (output->width - 1) * ratio->horz
			       + 64 * sph + 32;
		width = (upscaled_width >> 8) + 7;
	}

	/* Center the new crop rectangle. */
	input->left += (input->width - width) / 2;
	input->top += (input->height - height) / 2;
	input->width = width;
	input->height = height;
}

/*
 * resizer_set_crop_params - Setup hardware with cropping parameters
 * @res : resizer private structure
 * @input : format on sink pad
 * @output : format on source pad
 * return none
 */
static void resizer_set_crop_params(struct isp_res_device *res,
				    const struct v4l2_mbus_framefmt *input,
				    const struct v4l2_mbus_framefmt *output)
{
	resizer_set_ratio(res, &res->ratio);

	/* Set chrominance horizontal algorithm */
	if (res->ratio.horz >= RESIZE_DIVISOR)
		resizer_set_bilinear(res, RSZ_THE_SAME);
	else
		resizer_set_bilinear(res, RSZ_BILINEAR);

	resizer_adjust_bandwidth(res);

	if (res->input == RESIZER_INPUT_MEMORY) {
		/* Calculate additional offset for crop */
		res->crop_offset = (res->crop.active.top * input->width +
				    res->crop.active.left) * 2;
		/*
		 * Write lowest 4 bits of horizontal pixel offset (in pixels),
		 * vertical start must be 0.
		 */
		resizer_set_start(res, (res->crop_offset / 2) & 0xf, 0);

		/*
		 * Set start (read) address for cropping, in bytes.
		 * Lowest 5 bits must be zero.
		 */
		__resizer_set_inaddr(res,
				res->addr_base + (res->crop_offset & ~0x1f));
	} else {
		/*
		 * Set vertical start line and horizontal starting pixel.
		 * If the input is from CCDC/PREV, horizontal start field is
		 * in bytes (twice number of pixels).
		 */
		resizer_set_start(res, res->crop.active.left * 2,
				  res->crop.active.top);
		/* Input address and offset must be 0 for preview/ccdc input */
		__resizer_set_inaddr(res, 0);
		resizer_set_input_offset(res, 0);
	}

	/* Set the input size */
	resizer_set_input_size(res, res->crop.active.width,
			       res->crop.active.height);
}

static void resizer_configure(struct isp_res_device *res)
{
	struct v4l2_mbus_framefmt *informat, *outformat;
	struct resizer_luma_yenh luma = {0, 0, 0, 0};

	resizer_set_source(res, res->input);

	informat = &res->formats[RESZ_PAD_SINK];
	outformat = &res->formats[RESZ_PAD_SOURCE];

	/* RESZ_PAD_SINK */
	if (res->input == RESIZER_INPUT_VP)
		resizer_set_input_offset(res, 0);
	else
		resizer_set_input_offset(res, ALIGN(informat->width, 0x10) * 2);

	/* YUV422 interleaved, default phase, no luma enhancement */
	resizer_set_intype(res, RSZ_YUV422);
	resizer_set_ycpos(res, informat->code);
	resizer_set_phase(res, DEFAULT_PHASE, DEFAULT_PHASE);
	resizer_set_luma(res, &luma);

	/* RESZ_PAD_SOURCE */
	resizer_set_output_offset(res, ALIGN(outformat->width * 2, 32));
	resizer_set_output_size(res, outformat->width, outformat->height);

	resizer_set_crop_params(res, informat, outformat);
}

/* -----------------------------------------------------------------------------
 * Interrupt handling
 */

static void resizer_enable_oneshot(struct isp_res_device *res)
{
	struct isp_device *isp = to_isp_device(res);

	isp_reg_set(isp, OMAP3_ISP_IOMEM_RESZ, ISPRSZ_PCR,
		    ISPRSZ_PCR_ENABLE | ISPRSZ_PCR_ONESHOT);
}

void omap3isp_resizer_isr_frame_sync(struct isp_res_device *res)
{
	/*
	 * If ISP_VIDEO_DMAQUEUE_QUEUED is set, DMA queue had an underrun
	 * condition, the module was paused and now we have a buffer queued
	 * on the output again. Restart the pipeline if running in continuous
	 * mode.
	 */
	if (res->state == ISP_PIPELINE_STREAM_CONTINUOUS &&
	    res->video_out.dmaqueue_flags & ISP_VIDEO_DMAQUEUE_QUEUED) {
		resizer_enable_oneshot(res);
		isp_video_dmaqueue_flags_clr(&res->video_out);
	}
}

static void resizer_isr_buffer(struct isp_res_device *res)
{
	struct isp_pipeline *pipe = to_isp_pipeline(&res->subdev.entity);
	struct isp_buffer *buffer;
	int restart = 0;

	if (res->state == ISP_PIPELINE_STREAM_STOPPED)
		return;

	/* Complete the output buffer and, if reading from memory, the input
	 * buffer.
	 */
	buffer = omap3isp_video_buffer_next(&res->video_out);
	if (buffer != NULL) {
		resizer_set_outaddr(res, buffer->isp_addr);
		restart = 1;
	}

	pipe->state |= ISP_PIPELINE_IDLE_OUTPUT;

	if (res->input == RESIZER_INPUT_MEMORY) {
		buffer = omap3isp_video_buffer_next(&res->video_in);
		if (buffer != NULL)
			resizer_set_inaddr(res, buffer->isp_addr);
		pipe->state |= ISP_PIPELINE_IDLE_INPUT;
	}

	if (res->state == ISP_PIPELINE_STREAM_SINGLESHOT) {
		if (isp_pipeline_ready(pipe))
			omap3isp_pipeline_set_stream(pipe,
						ISP_PIPELINE_STREAM_SINGLESHOT);
	} else {
		/* If an underrun occurs, the video queue operation handler will
		 * restart the resizer. Otherwise restart it immediately.
		 */
		if (restart)
			resizer_enable_oneshot(res);
	}
}

/*
 * omap3isp_resizer_isr - ISP resizer interrupt handler
 *
 * Manage the resizer video buffers and configure shadowed and busy-locked
 * registers.
 */
void omap3isp_resizer_isr(struct isp_res_device *res)
{
	struct v4l2_mbus_framefmt *informat, *outformat;

	if (omap3isp_module_sync_is_stopping(&res->wait, &res->stopping))
		return;

	if (res->applycrop) {
		outformat = __resizer_get_format(res, NULL, RESZ_PAD_SOURCE,
					      V4L2_SUBDEV_FORMAT_ACTIVE);
		informat = __resizer_get_format(res, NULL, RESZ_PAD_SINK,
					      V4L2_SUBDEV_FORMAT_ACTIVE);
		resizer_set_crop_params(res, informat, outformat);
		res->applycrop = 0;
	}

	resizer_isr_buffer(res);
}

/* -----------------------------------------------------------------------------
 * ISP video operations
 */

static int resizer_video_queue(struct isp_video *video,
			       struct isp_buffer *buffer)
{
	struct isp_res_device *res = &video->isp->isp_res;

	if (video->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		resizer_set_inaddr(res, buffer->isp_addr);

	/*
	 * We now have a buffer queued on the output. Despite what the
	 * TRM says, the resizer can't be restarted immediately.
	 * Enabling it in one shot mode in the middle of a frame (or at
	 * least asynchronously to the frame) results in the output
	 * being shifted randomly left/right and up/down, as if the
	 * hardware didn't synchronize itself to the beginning of the
	 * frame correctly.
	 *
	 * Restart the resizer on the next sync interrupt if running in
	 * continuous mode or when starting the stream.
	 */
	if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		resizer_set_outaddr(res, buffer->isp_addr);

	return 0;
}

static const struct isp_video_operations resizer_video_ops = {
	.queue = resizer_video_queue,
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

/*
 * resizer_set_stream - Enable/Disable streaming on resizer subdev
 * @sd: ISP resizer V4L2 subdev
 * @enable: 1 == Enable, 0 == Disable
 *
 * The resizer hardware can't be enabled without a memory buffer to write to.
 * As the s_stream operation is called in response to a STREAMON call without
 * any buffer queued yet, just update the state field and return immediately.
 * The resizer will be enabled in resizer_video_queue().
 */
static int resizer_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct isp_res_device *res = v4l2_get_subdevdata(sd);
	struct isp_video *video_out = &res->video_out;
	struct isp_device *isp = to_isp_device(res);
	struct device *dev = to_device(res);

	if (res->state == ISP_PIPELINE_STREAM_STOPPED) {
		if (enable == ISP_PIPELINE_STREAM_STOPPED)
			return 0;

		omap3isp_subclk_enable(isp, OMAP3_ISP_SUBCLK_RESIZER);
		resizer_configure(res);
		resizer_print_status(res);
	}

	switch (enable) {
	case ISP_PIPELINE_STREAM_CONTINUOUS:
		omap3isp_sbl_enable(isp, OMAP3_ISP_SBL_RESIZER_WRITE);
		if (video_out->dmaqueue_flags & ISP_VIDEO_DMAQUEUE_QUEUED) {
			resizer_enable_oneshot(res);
			isp_video_dmaqueue_flags_clr(video_out);
		}
		break;

	case ISP_PIPELINE_STREAM_SINGLESHOT:
		if (res->input == RESIZER_INPUT_MEMORY)
			omap3isp_sbl_enable(isp, OMAP3_ISP_SBL_RESIZER_READ);
		omap3isp_sbl_enable(isp, OMAP3_ISP_SBL_RESIZER_WRITE);

		resizer_enable_oneshot(res);
		break;

	case ISP_PIPELINE_STREAM_STOPPED:
		if (omap3isp_module_sync_idle(&sd->entity, &res->wait,
					      &res->stopping))
			dev_dbg(dev, "%s: module stop timeout.\n", sd->name);
		omap3isp_sbl_disable(isp, OMAP3_ISP_SBL_RESIZER_READ |
				OMAP3_ISP_SBL_RESIZER_WRITE);
		omap3isp_subclk_disable(isp, OMAP3_ISP_SUBCLK_RESIZER);
		isp_video_dmaqueue_flags_clr(video_out);
		break;
	}

	res->state = enable;
	return 0;
}

/*
 * resizer_try_crop - mangles crop parameters.
 */
static void resizer_try_crop(const struct v4l2_mbus_framefmt *sink,
			     const struct v4l2_mbus_framefmt *source,
			     struct v4l2_rect *crop)
{
	const unsigned int spv = DEFAULT_PHASE;
	const unsigned int sph = DEFAULT_PHASE;

	/* Crop rectangle is constrained by the output size so that zoom ratio
	 * cannot exceed +/-4.0.
	 */
	unsigned int min_width =
		((32 * sph + (source->width - 1) * 64 + 16) >> 8) + 7;
	unsigned int min_height =
		((32 * spv + (source->height - 1) * 64 + 16) >> 8) + 4;
	unsigned int max_width =
		((64 * sph + (source->width - 1) * 1024 + 32) >> 8) + 7;
	unsigned int max_height =
		((64 * spv + (source->height - 1) * 1024 + 32) >> 8) + 7;

	crop->width = clamp_t(u32, crop->width, min_width, max_width);
	crop->height = clamp_t(u32, crop->height, min_height, max_height);

	/* Crop can not go beyond of the input rectangle */
	crop->left = clamp_t(u32, crop->left, 0, sink->width - MIN_IN_WIDTH);
	crop->width = clamp_t(u32, crop->width, MIN_IN_WIDTH,
			      sink->width - crop->left);
	crop->top = clamp_t(u32, crop->top, 0, sink->height - MIN_IN_HEIGHT);
	crop->height = clamp_t(u32, crop->height, MIN_IN_HEIGHT,
			       sink->height - crop->top);
}

/*
 * resizer_get_selection - Retrieve a selection rectangle on a pad
 * @sd: ISP resizer V4L2 subdevice
 * @fh: V4L2 subdev file handle
 * @sel: Selection rectangle
 *
 * The only supported rectangles are the crop rectangles on the sink pad.
 *
 * Return 0 on success or a negative error code otherwise.
 */
static int resizer_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_selection *sel)
{
	struct isp_res_device *res = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format_source;
	struct v4l2_mbus_framefmt *format_sink;
	struct resizer_ratio ratio;

	if (sel->pad != RESZ_PAD_SINK)
		return -EINVAL;

	format_sink = __resizer_get_format(res, fh, RESZ_PAD_SINK,
					   sel->which);
	format_source = __resizer_get_format(res, fh, RESZ_PAD_SOURCE,
					     sel->which);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = INT_MAX;
		sel->r.height = INT_MAX;

		resizer_try_crop(format_sink, format_source, &sel->r);
		resizer_calc_ratios(res, &sel->r, format_source, &ratio);
		break;

	case V4L2_SEL_TGT_CROP:
		sel->r = *__resizer_get_crop(res, fh, sel->which);
		resizer_calc_ratios(res, &sel->r, format_source, &ratio);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * resizer_set_selection - Set a selection rectangle on a pad
 * @sd: ISP resizer V4L2 subdevice
 * @fh: V4L2 subdev file handle
 * @sel: Selection rectangle
 *
 * The only supported rectangle is the actual crop rectangle on the sink pad.
 *
 * FIXME: This function currently behaves as if the KEEP_CONFIG selection flag
 * was always set.
 *
 * Return 0 on success or a negative error code otherwise.
 */
static int resizer_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_selection *sel)
{
	struct isp_res_device *res = v4l2_get_subdevdata(sd);
	struct isp_device *isp = to_isp_device(res);
	struct v4l2_mbus_framefmt *format_sink, *format_source;
	struct resizer_ratio ratio;

	if (sel->target != V4L2_SEL_TGT_CROP ||
	    sel->pad != RESZ_PAD_SINK)
		return -EINVAL;

	format_sink = __resizer_get_format(res, fh, RESZ_PAD_SINK,
					   sel->which);
	format_source = __resizer_get_format(res, fh, RESZ_PAD_SOURCE,
					     sel->which);

	dev_dbg(isp->dev, "%s: L=%d,T=%d,W=%d,H=%d,which=%d\n", __func__,
		sel->r.left, sel->r.top, sel->r.width, sel->r.height,
		sel->which);

	dev_dbg(isp->dev, "%s: input=%dx%d, output=%dx%d\n", __func__,
		format_sink->width, format_sink->height,
		format_source->width, format_source->height);

	/* Clamp the crop rectangle to the bounds, and then mangle it further to
	 * fulfill the TRM equations. Store the clamped but otherwise unmangled
	 * rectangle to avoid cropping the input multiple times: when an
	 * application sets the output format, the current crop rectangle is
	 * mangled during crop rectangle computation, which would lead to a new,
	 * smaller input crop rectangle every time the output size is set if we
	 * stored the mangled rectangle.
	 */
	resizer_try_crop(format_sink, format_source, &sel->r);
	*__resizer_get_crop(res, fh, sel->which) = sel->r;
	resizer_calc_ratios(res, &sel->r, format_source, &ratio);

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	res->ratio = ratio;
	res->crop.active = sel->r;

	/*
	 * set_selection can be called while streaming is on. In this case the
	 * crop values will be set in the next IRQ.
	 */
	if (res->state != ISP_PIPELINE_STREAM_STOPPED)
		res->applycrop = 1;

	return 0;
}

/* resizer pixel formats */
static const unsigned int resizer_formats[] = {
	V4L2_MBUS_FMT_UYVY8_1X16,
	V4L2_MBUS_FMT_YUYV8_1X16,
};

static unsigned int resizer_max_in_width(struct isp_res_device *res)
{
	struct isp_device *isp = to_isp_device(res);

	if (res->input == RESIZER_INPUT_MEMORY) {
		return MAX_IN_WIDTH_MEMORY_MODE;
	} else {
		if (isp->revision == ISP_REVISION_1_0)
			return MAX_IN_WIDTH_ONTHEFLY_MODE_ES1;
		else
			return MAX_IN_WIDTH_ONTHEFLY_MODE_ES2;
	}
}

/*
 * resizer_try_format - Handle try format by pad subdev method
 * @res   : ISP resizer device
 * @fh    : V4L2 subdev file handle
 * @pad   : pad num
 * @fmt   : pointer to v4l2 format structure
 * @which : wanted subdev format
 */
static void resizer_try_format(struct isp_res_device *res,
			       struct v4l2_subdev_fh *fh, unsigned int pad,
			       struct v4l2_mbus_framefmt *fmt,
			       enum v4l2_subdev_format_whence which)
{
	struct v4l2_mbus_framefmt *format;
	struct resizer_ratio ratio;
	struct v4l2_rect crop;

	switch (pad) {
	case RESZ_PAD_SINK:
		if (fmt->code != V4L2_MBUS_FMT_YUYV8_1X16 &&
		    fmt->code != V4L2_MBUS_FMT_UYVY8_1X16)
			fmt->code = V4L2_MBUS_FMT_YUYV8_1X16;

		fmt->width = clamp_t(u32, fmt->width, MIN_IN_WIDTH,
				     resizer_max_in_width(res));
		fmt->height = clamp_t(u32, fmt->height, MIN_IN_HEIGHT,
				      MAX_IN_HEIGHT);
		break;

	case RESZ_PAD_SOURCE:
		format = __resizer_get_format(res, fh, RESZ_PAD_SINK, which);
		fmt->code = format->code;

		crop = *__resizer_get_crop(res, fh, which);
		resizer_calc_ratios(res, &crop, fmt, &ratio);
		break;
	}

	fmt->colorspace = V4L2_COLORSPACE_JPEG;
	fmt->field = V4L2_FIELD_NONE;
}

/*
 * resizer_enum_mbus_code - Handle pixel format enumeration
 * @sd     : pointer to v4l2 subdev structure
 * @fh     : V4L2 subdev file handle
 * @code   : pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int resizer_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct isp_res_device *res = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	if (code->pad == RESZ_PAD_SINK) {
		if (code->index >= ARRAY_SIZE(resizer_formats))
			return -EINVAL;

		code->code = resizer_formats[code->index];
	} else {
		if (code->index != 0)
			return -EINVAL;

		format = __resizer_get_format(res, fh, RESZ_PAD_SINK,
					      V4L2_SUBDEV_FORMAT_TRY);
		code->code = format->code;
	}

	return 0;
}

static int resizer_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct isp_res_device *res = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	resizer_try_format(res, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	resizer_try_format(res, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * resizer_get_format - Handle get format by pads subdev method
 * @sd    : pointer to v4l2 subdev structure
 * @fh    : V4L2 subdev file handle
 * @fmt   : pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int resizer_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct isp_res_device *res = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __resizer_get_format(res, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

/*
 * resizer_set_format - Handle set format by pads subdev method
 * @sd    : pointer to v4l2 subdev structure
 * @fh    : V4L2 subdev file handle
 * @fmt   : pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int resizer_set_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct isp_res_device *res = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	format = __resizer_get_format(res, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	resizer_try_format(res, fh, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	if (fmt->pad == RESZ_PAD_SINK) {
		/* reset crop rectangle */
		crop = __resizer_get_crop(res, fh, fmt->which);
		crop->left = 0;
		crop->top = 0;
		crop->width = fmt->format.width;
		crop->height = fmt->format.height;

		/* Propagate the format from sink to source */
		format = __resizer_get_format(res, fh, RESZ_PAD_SOURCE,
					      fmt->which);
		*format = fmt->format;
		resizer_try_format(res, fh, RESZ_PAD_SOURCE, format,
				   fmt->which);
	}

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		/* Compute and store the active crop rectangle and resizer
		 * ratios. format already points to the source pad active
		 * format.
		 */
		res->crop.active = res->crop.request;
		resizer_calc_ratios(res, &res->crop.active, format,
				       &res->ratio);
	}

	return 0;
}

static int resizer_link_validate(struct v4l2_subdev *sd,
				 struct media_link *link,
				 struct v4l2_subdev_format *source_fmt,
				 struct v4l2_subdev_format *sink_fmt)
{
	struct isp_res_device *res = v4l2_get_subdevdata(sd);
	struct isp_pipeline *pipe = to_isp_pipeline(&sd->entity);

	omap3isp_resizer_max_rate(res, &pipe->max_rate);

	return v4l2_subdev_link_validate_default(sd, link,
						 source_fmt, sink_fmt);
}

/*
 * resizer_init_formats - Initialize formats on all pads
 * @sd: ISP resizer V4L2 subdevice
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
	format.pad = RESZ_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_YUYV8_1X16;
	format.format.width = 4096;
	format.format.height = 4096;
	resizer_set_format(sd, fh, &format);

	return 0;
}

/* subdev video operations */
static const struct v4l2_subdev_video_ops resizer_v4l2_video_ops = {
	.s_stream = resizer_set_stream,
};

/* subdev pad operations */
static const struct v4l2_subdev_pad_ops resizer_v4l2_pad_ops = {
	.enum_mbus_code = resizer_enum_mbus_code,
	.enum_frame_size = resizer_enum_frame_size,
	.get_fmt = resizer_get_format,
	.set_fmt = resizer_set_format,
	.get_selection = resizer_get_selection,
	.set_selection = resizer_set_selection,
	.link_validate = resizer_link_validate,
};

/* subdev operations */
static const struct v4l2_subdev_ops resizer_v4l2_ops = {
	.video = &resizer_v4l2_video_ops,
	.pad = &resizer_v4l2_pad_ops,
};

/* subdev internal operations */
static const struct v4l2_subdev_internal_ops resizer_v4l2_internal_ops = {
	.open = resizer_init_formats,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

/*
 * resizer_link_setup - Setup resizer connections.
 * @entity : Pointer to media entity structure
 * @local  : Pointer to local pad array
 * @remote : Pointer to remote pad array
 * @flags  : Link flags
 * return -EINVAL or zero on success
 */
static int resizer_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct isp_res_device *res = v4l2_get_subdevdata(sd);

	switch (local->index | media_entity_type(remote->entity)) {
	case RESZ_PAD_SINK | MEDIA_ENT_T_DEVNODE:
		/* read from memory */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (res->input == RESIZER_INPUT_VP)
				return -EBUSY;
			res->input = RESIZER_INPUT_MEMORY;
		} else {
			if (res->input == RESIZER_INPUT_MEMORY)
				res->input = RESIZER_INPUT_NONE;
		}
		break;

	case RESZ_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		/* read from ccdc or previewer */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (res->input == RESIZER_INPUT_MEMORY)
				return -EBUSY;
			res->input = RESIZER_INPUT_VP;
		} else {
			if (res->input == RESIZER_INPUT_VP)
				res->input = RESIZER_INPUT_NONE;
		}
		break;

	case RESZ_PAD_SOURCE | MEDIA_ENT_T_DEVNODE:
		/* resizer always write to memory */
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

void omap3isp_resizer_unregister_entities(struct isp_res_device *res)
{
	v4l2_device_unregister_subdev(&res->subdev);
	omap3isp_video_unregister(&res->video_in);
	omap3isp_video_unregister(&res->video_out);
}

int omap3isp_resizer_register_entities(struct isp_res_device *res,
				       struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev and video nodes. */
	ret = v4l2_device_register_subdev(vdev, &res->subdev);
	if (ret < 0)
		goto error;

	ret = omap3isp_video_register(&res->video_in, vdev);
	if (ret < 0)
		goto error;

	ret = omap3isp_video_register(&res->video_out, vdev);
	if (ret < 0)
		goto error;

	return 0;

error:
	omap3isp_resizer_unregister_entities(res);
	return ret;
}

/* -----------------------------------------------------------------------------
 * ISP resizer initialization and cleanup
 */

/*
 * resizer_init_entities - Initialize resizer subdev and media entity.
 * @res : Pointer to resizer device structure
 * return -ENOMEM or zero on success
 */
static int resizer_init_entities(struct isp_res_device *res)
{
	struct v4l2_subdev *sd = &res->subdev;
	struct media_pad *pads = res->pads;
	struct media_entity *me = &sd->entity;
	int ret;

	res->input = RESIZER_INPUT_NONE;

	v4l2_subdev_init(sd, &resizer_v4l2_ops);
	sd->internal_ops = &resizer_v4l2_internal_ops;
	strlcpy(sd->name, "OMAP3 ISP resizer", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for isp subdevs */
	v4l2_set_subdevdata(sd, res);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pads[RESZ_PAD_SINK].flags = MEDIA_PAD_FL_SINK
				    | MEDIA_PAD_FL_MUST_CONNECT;
	pads[RESZ_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	me->ops = &resizer_media_ops;
	ret = media_entity_init(me, RESZ_PADS_NUM, pads, 0);
	if (ret < 0)
		return ret;

	resizer_init_formats(sd, NULL);

	res->video_in.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	res->video_in.ops = &resizer_video_ops;
	res->video_in.isp = to_isp_device(res);
	res->video_in.capture_mem = PAGE_ALIGN(4096 * 4096) * 2 * 3;
	res->video_in.bpl_alignment = 32;
	res->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	res->video_out.ops = &resizer_video_ops;
	res->video_out.isp = to_isp_device(res);
	res->video_out.capture_mem = PAGE_ALIGN(4096 * 4096) * 2 * 3;
	res->video_out.bpl_alignment = 32;

	ret = omap3isp_video_init(&res->video_in, "resizer");
	if (ret < 0)
		goto error_video_in;

	ret = omap3isp_video_init(&res->video_out, "resizer");
	if (ret < 0)
		goto error_video_out;

	res->video_out.video.entity.flags |= MEDIA_ENT_FL_DEFAULT;

	/* Connect the video nodes to the resizer subdev. */
	ret = media_entity_create_link(&res->video_in.video.entity, 0,
			&res->subdev.entity, RESZ_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(&res->subdev.entity, RESZ_PAD_SOURCE,
			&res->video_out.video.entity, 0, 0);
	if (ret < 0)
		goto error_link;

	return 0;

error_link:
	omap3isp_video_cleanup(&res->video_out);
error_video_out:
	omap3isp_video_cleanup(&res->video_in);
error_video_in:
	media_entity_cleanup(&res->subdev.entity);
	return ret;
}

/*
 * isp_resizer_init - Resizer initialization.
 * @isp : Pointer to ISP device
 * return -ENOMEM or zero on success
 */
int omap3isp_resizer_init(struct isp_device *isp)
{
	struct isp_res_device *res = &isp->isp_res;

	init_waitqueue_head(&res->wait);
	atomic_set(&res->stopping, 0);
	return resizer_init_entities(res);
}

void omap3isp_resizer_cleanup(struct isp_device *isp)
{
	struct isp_res_device *res = &isp->isp_res;

	omap3isp_video_cleanup(&res->video_in);
	omap3isp_video_cleanup(&res->video_out);
	media_entity_cleanup(&res->subdev.entity);
}
