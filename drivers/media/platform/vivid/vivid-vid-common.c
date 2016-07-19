/*
 * vivid-vid-common.c - common video support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/videodev2.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-dv-timings.h>

#include "vivid-core.h"
#include "vivid-vid-common.h"

const struct v4l2_dv_timings_cap vivid_dv_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(0, MAX_WIDTH, 0, MAX_HEIGHT, 14000000, 775000000,
		V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
		V4L2_DV_BT_STD_CVT | V4L2_DV_BT_STD_GTF,
		V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_INTERLACED)
};

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct vivid_fmt vivid_formats[] = {
	{
		.fourcc   = V4L2_PIX_FMT_YUYV,
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.is_yuv   = true,
		.planes   = 1,
		.buffers = 1,
		.data_offset = { PLANE0_DATA_OFFSET },
	},
	{
		.fourcc   = V4L2_PIX_FMT_UYVY,
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.is_yuv   = true,
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YVYU,
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.is_yuv   = true,
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_VYUY,
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.is_yuv   = true,
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YUV422P,
		.vdownsampling = { 1, 1, 1 },
		.bit_depth = { 8, 4, 4 },
		.is_yuv   = true,
		.planes   = 3,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YUV420,
		.vdownsampling = { 1, 2, 2 },
		.bit_depth = { 8, 4, 4 },
		.is_yuv   = true,
		.planes   = 3,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YVU420,
		.vdownsampling = { 1, 2, 2 },
		.bit_depth = { 8, 4, 4 },
		.is_yuv   = true,
		.planes   = 3,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_NV12,
		.vdownsampling = { 1, 2 },
		.bit_depth = { 8, 8 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_NV21,
		.vdownsampling = { 1, 2 },
		.bit_depth = { 8, 8 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_NV16,
		.vdownsampling = { 1, 1 },
		.bit_depth = { 8, 8 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_NV61,
		.vdownsampling = { 1, 1 },
		.bit_depth = { 8, 8 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_NV24,
		.vdownsampling = { 1, 1 },
		.bit_depth = { 8, 16 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_NV42,
		.vdownsampling = { 1, 1 },
		.bit_depth = { 8, 16 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YUV555, /* uuuvvvvv ayyyyyuu */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
		.alpha_mask = 0x8000,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YUV565, /* uuuvvvvv yyyyyuuu */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YUV444, /* uuuuvvvv aaaayyyy */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
		.alpha_mask = 0xf000,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YUV32, /* ayuv */
		.vdownsampling = { 1 },
		.bit_depth = { 32 },
		.planes   = 1,
		.buffers = 1,
		.alpha_mask = 0x000000ff,
	},
	{
		.fourcc   = V4L2_PIX_FMT_GREY,
		.vdownsampling = { 1 },
		.bit_depth = { 8 },
		.is_yuv   = true,
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_Y16,
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.is_yuv   = true,
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_Y16_BE,
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.is_yuv   = true,
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_RGB332, /* rrrgggbb */
		.vdownsampling = { 1 },
		.bit_depth = { 8 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_RGB565, /* gggbbbbb rrrrrggg */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
		.can_do_overlay = true,
	},
	{
		.fourcc   = V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
		.can_do_overlay = true,
	},
	{
		.fourcc   = V4L2_PIX_FMT_RGB444, /* xxxxrrrr ggggbbbb */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_XRGB444, /* xxxxrrrr ggggbbbb */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_ARGB444, /* aaaarrrr ggggbbbb */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
		.alpha_mask = 0x00f0,
	},
	{
		.fourcc   = V4L2_PIX_FMT_RGB555, /* gggbbbbb xrrrrrgg */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
		.can_do_overlay = true,
	},
	{
		.fourcc   = V4L2_PIX_FMT_XRGB555, /* gggbbbbb xrrrrrgg */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
		.can_do_overlay = true,
	},
	{
		.fourcc   = V4L2_PIX_FMT_ARGB555, /* gggbbbbb arrrrrgg */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
		.can_do_overlay = true,
		.alpha_mask = 0x8000,
	},
	{
		.fourcc   = V4L2_PIX_FMT_RGB555X, /* xrrrrrgg gggbbbbb */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_XRGB555X, /* xrrrrrgg gggbbbbb */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_ARGB555X, /* arrrrrgg gggbbbbb */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
		.alpha_mask = 0x0080,
	},
	{
		.fourcc   = V4L2_PIX_FMT_RGB24, /* rgb */
		.vdownsampling = { 1 },
		.bit_depth = { 24 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_BGR24, /* bgr */
		.vdownsampling = { 1 },
		.bit_depth = { 24 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_BGR666, /* bbbbbbgg ggggrrrr rrxxxxxx */
		.vdownsampling = { 1 },
		.bit_depth = { 32 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_RGB32, /* xrgb */
		.vdownsampling = { 1 },
		.bit_depth = { 32 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_BGR32, /* bgrx */
		.vdownsampling = { 1 },
		.bit_depth = { 32 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_XRGB32, /* xrgb */
		.vdownsampling = { 1 },
		.bit_depth = { 32 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_XBGR32, /* bgrx */
		.vdownsampling = { 1 },
		.bit_depth = { 32 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_ARGB32, /* argb */
		.vdownsampling = { 1 },
		.bit_depth = { 32 },
		.planes   = 1,
		.buffers = 1,
		.alpha_mask = 0x000000ff,
	},
	{
		.fourcc   = V4L2_PIX_FMT_ABGR32, /* bgra */
		.vdownsampling = { 1 },
		.bit_depth = { 32 },
		.planes   = 1,
		.buffers = 1,
		.alpha_mask = 0xff000000,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SBGGR8, /* Bayer BG/GR */
		.vdownsampling = { 1 },
		.bit_depth = { 8 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SGBRG8, /* Bayer GB/RG */
		.vdownsampling = { 1 },
		.bit_depth = { 8 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SGRBG8, /* Bayer GR/BG */
		.vdownsampling = { 1 },
		.bit_depth = { 8 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SRGGB8, /* Bayer RG/GB */
		.vdownsampling = { 1 },
		.bit_depth = { 8 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SBGGR10, /* Bayer BG/GR */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SGBRG10, /* Bayer GB/RG */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SGRBG10, /* Bayer GR/BG */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SRGGB10, /* Bayer RG/GB */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SBGGR12, /* Bayer BG/GR */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SGBRG12, /* Bayer GB/RG */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SGRBG12, /* Bayer GR/BG */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},
	{
		.fourcc   = V4L2_PIX_FMT_SRGGB12, /* Bayer RG/GB */
		.vdownsampling = { 1 },
		.bit_depth = { 16 },
		.planes   = 1,
		.buffers = 1,
	},

	/* Multiplanar formats */

	{
		.fourcc   = V4L2_PIX_FMT_NV16M,
		.vdownsampling = { 1, 1 },
		.bit_depth = { 8, 8 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 2,
		.data_offset = { PLANE0_DATA_OFFSET, 0 },
	},
	{
		.fourcc   = V4L2_PIX_FMT_NV61M,
		.vdownsampling = { 1, 1 },
		.bit_depth = { 8, 8 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 2,
		.data_offset = { 0, PLANE0_DATA_OFFSET },
	},
	{
		.fourcc   = V4L2_PIX_FMT_YUV420M,
		.vdownsampling = { 1, 2, 2 },
		.bit_depth = { 8, 4, 4 },
		.is_yuv   = true,
		.planes   = 3,
		.buffers = 3,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YVU420M,
		.vdownsampling = { 1, 2, 2 },
		.bit_depth = { 8, 4, 4 },
		.is_yuv   = true,
		.planes   = 3,
		.buffers = 3,
	},
	{
		.fourcc   = V4L2_PIX_FMT_NV12M,
		.vdownsampling = { 1, 2 },
		.bit_depth = { 8, 8 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 2,
	},
	{
		.fourcc   = V4L2_PIX_FMT_NV21M,
		.vdownsampling = { 1, 2 },
		.bit_depth = { 8, 8 },
		.is_yuv   = true,
		.planes   = 2,
		.buffers = 2,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YUV422M,
		.vdownsampling = { 1, 1, 1 },
		.bit_depth = { 8, 4, 4 },
		.is_yuv   = true,
		.planes   = 3,
		.buffers = 3,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YVU422M,
		.vdownsampling = { 1, 1, 1 },
		.bit_depth = { 8, 4, 4 },
		.is_yuv   = true,
		.planes   = 3,
		.buffers = 3,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YUV444M,
		.vdownsampling = { 1, 1, 1 },
		.bit_depth = { 8, 8, 8 },
		.is_yuv   = true,
		.planes   = 3,
		.buffers = 3,
	},
	{
		.fourcc   = V4L2_PIX_FMT_YVU444M,
		.vdownsampling = { 1, 1, 1 },
		.bit_depth = { 8, 8, 8 },
		.is_yuv   = true,
		.planes   = 3,
		.buffers = 3,
	},
};

/* There are this many multiplanar formats in the list */
#define VIVID_MPLANAR_FORMATS 10

const struct vivid_fmt *vivid_get_format(struct vivid_dev *dev, u32 pixelformat)
{
	const struct vivid_fmt *fmt;
	unsigned k;

	for (k = 0; k < ARRAY_SIZE(vivid_formats); k++) {
		fmt = &vivid_formats[k];
		if (fmt->fourcc == pixelformat)
			if (fmt->buffers == 1 || dev->multiplanar)
				return fmt;
	}

	return NULL;
}

bool vivid_vid_can_loop(struct vivid_dev *dev)
{
	if (dev->src_rect.width != dev->sink_rect.width ||
	    dev->src_rect.height != dev->sink_rect.height)
		return false;
	if (dev->fmt_cap->fourcc != dev->fmt_out->fourcc)
		return false;
	if (dev->field_cap != dev->field_out)
		return false;
	/*
	 * While this can be supported, it is just too much work
	 * to actually implement.
	 */
	if (dev->field_cap == V4L2_FIELD_SEQ_TB ||
	    dev->field_cap == V4L2_FIELD_SEQ_BT)
		return false;
	if (vivid_is_svid_cap(dev) && vivid_is_svid_out(dev)) {
		if (!(dev->std_cap & V4L2_STD_525_60) !=
		    !(dev->std_out & V4L2_STD_525_60))
			return false;
		return true;
	}
	if (vivid_is_hdmi_cap(dev) && vivid_is_hdmi_out(dev))
		return true;
	return false;
}

void vivid_send_source_change(struct vivid_dev *dev, unsigned type)
{
	struct v4l2_event ev = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};
	unsigned i;

	for (i = 0; i < dev->num_inputs; i++) {
		ev.id = i;
		if (dev->input_type[i] == type) {
			if (video_is_registered(&dev->vid_cap_dev) && dev->has_vid_cap)
				v4l2_event_queue(&dev->vid_cap_dev, &ev);
			if (video_is_registered(&dev->vbi_cap_dev) && dev->has_vbi_cap)
				v4l2_event_queue(&dev->vbi_cap_dev, &ev);
		}
	}
}

/*
 * Conversion function that converts a single-planar format to a
 * single-plane multiplanar format.
 */
void fmt_sp2mp(const struct v4l2_format *sp_fmt, struct v4l2_format *mp_fmt)
{
	struct v4l2_pix_format_mplane *mp = &mp_fmt->fmt.pix_mp;
	struct v4l2_plane_pix_format *ppix = &mp->plane_fmt[0];
	const struct v4l2_pix_format *pix = &sp_fmt->fmt.pix;
	bool is_out = sp_fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT;

	memset(mp->reserved, 0, sizeof(mp->reserved));
	mp_fmt->type = is_out ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE :
			   V4L2_CAP_VIDEO_CAPTURE_MPLANE;
	mp->width = pix->width;
	mp->height = pix->height;
	mp->pixelformat = pix->pixelformat;
	mp->field = pix->field;
	mp->colorspace = pix->colorspace;
	mp->xfer_func = pix->xfer_func;
	mp->ycbcr_enc = pix->ycbcr_enc;
	mp->quantization = pix->quantization;
	mp->num_planes = 1;
	mp->flags = pix->flags;
	ppix->sizeimage = pix->sizeimage;
	ppix->bytesperline = pix->bytesperline;
	memset(ppix->reserved, 0, sizeof(ppix->reserved));
}

int fmt_sp2mp_func(struct file *file, void *priv,
		struct v4l2_format *f, fmtfunc func)
{
	struct v4l2_format fmt;
	struct v4l2_pix_format_mplane *mp = &fmt.fmt.pix_mp;
	struct v4l2_plane_pix_format *ppix = &mp->plane_fmt[0];
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int ret;

	/* Converts to a mplane format */
	fmt_sp2mp(f, &fmt);
	/* Passes it to the generic mplane format function */
	ret = func(file, priv, &fmt);
	/* Copies back the mplane data to the single plane format */
	pix->width = mp->width;
	pix->height = mp->height;
	pix->pixelformat = mp->pixelformat;
	pix->field = mp->field;
	pix->colorspace = mp->colorspace;
	pix->xfer_func = mp->xfer_func;
	pix->ycbcr_enc = mp->ycbcr_enc;
	pix->quantization = mp->quantization;
	pix->sizeimage = ppix->sizeimage;
	pix->bytesperline = ppix->bytesperline;
	pix->flags = mp->flags;
	return ret;
}

/* v4l2_rect helper function: copy the width/height values */
void rect_set_size_to(struct v4l2_rect *r, const struct v4l2_rect *size)
{
	r->width = size->width;
	r->height = size->height;
}

/* v4l2_rect helper function: width and height of r should be >= min_size */
void rect_set_min_size(struct v4l2_rect *r, const struct v4l2_rect *min_size)
{
	if (r->width < min_size->width)
		r->width = min_size->width;
	if (r->height < min_size->height)
		r->height = min_size->height;
}

/* v4l2_rect helper function: width and height of r should be <= max_size */
void rect_set_max_size(struct v4l2_rect *r, const struct v4l2_rect *max_size)
{
	if (r->width > max_size->width)
		r->width = max_size->width;
	if (r->height > max_size->height)
		r->height = max_size->height;
}

/* v4l2_rect helper function: r should be inside boundary */
void rect_map_inside(struct v4l2_rect *r, const struct v4l2_rect *boundary)
{
	rect_set_max_size(r, boundary);
	if (r->left < boundary->left)
		r->left = boundary->left;
	if (r->top < boundary->top)
		r->top = boundary->top;
	if (r->left + r->width > boundary->width)
		r->left = boundary->width - r->width;
	if (r->top + r->height > boundary->height)
		r->top = boundary->height - r->height;
}

/* v4l2_rect helper function: return true if r1 has the same size as r2 */
bool rect_same_size(const struct v4l2_rect *r1, const struct v4l2_rect *r2)
{
	return r1->width == r2->width && r1->height == r2->height;
}

/* v4l2_rect helper function: calculate the intersection of two rects */
struct v4l2_rect rect_intersect(const struct v4l2_rect *a, const struct v4l2_rect *b)
{
	struct v4l2_rect r;
	int right, bottom;

	r.top = max(a->top, b->top);
	r.left = max(a->left, b->left);
	bottom = min(a->top + a->height, b->top + b->height);
	right = min(a->left + a->width, b->left + b->width);
	r.height = max(0, bottom - r.top);
	r.width = max(0, right - r.left);
	return r;
}

/*
 * v4l2_rect helper function: scale rect r by to->width / from->width and
 * to->height / from->height.
 */
void rect_scale(struct v4l2_rect *r, const struct v4l2_rect *from,
				     const struct v4l2_rect *to)
{
	if (from->width == 0 || from->height == 0) {
		r->left = r->top = r->width = r->height = 0;
		return;
	}
	r->left = (((r->left - from->left) * to->width) / from->width) & ~1;
	r->width = ((r->width * to->width) / from->width) & ~1;
	r->top = ((r->top - from->top) * to->height) / from->height;
	r->height = (r->height * to->height) / from->height;
}

bool rect_overlap(const struct v4l2_rect *r1, const struct v4l2_rect *r2)
{
	/*
	 * IF the left side of r1 is to the right of the right side of r2 OR
	 *    the left side of r2 is to the right of the right side of r1 THEN
	 * they do not overlap.
	 */
	if (r1->left >= r2->left + r2->width ||
	    r2->left >= r1->left + r1->width)
		return false;
	/*
	 * IF the top side of r1 is below the bottom of r2 OR
	 *    the top side of r2 is below the bottom of r1 THEN
	 * they do not overlap.
	 */
	if (r1->top >= r2->top + r2->height ||
	    r2->top >= r1->top + r1->height)
		return false;
	return true;
}
int vivid_vid_adjust_sel(unsigned flags, struct v4l2_rect *r)
{
	unsigned w = r->width;
	unsigned h = r->height;

	/* sanitize w and h in case someone passes ~0 as the value */
	w &= 0xffff;
	h &= 0xffff;
	if (!(flags & V4L2_SEL_FLAG_LE)) {
		w++;
		h++;
		if (w < 2)
			w = 2;
		if (h < 2)
			h = 2;
	}
	if (!(flags & V4L2_SEL_FLAG_GE)) {
		if (w > MAX_WIDTH)
			w = MAX_WIDTH;
		if (h > MAX_HEIGHT)
			h = MAX_HEIGHT;
	}
	w = w & ~1;
	h = h & ~1;
	if (w < 2 || h < 2)
		return -ERANGE;
	if (w > MAX_WIDTH || h > MAX_HEIGHT)
		return -ERANGE;
	if (r->top < 0)
		r->top = 0;
	if (r->left < 0)
		r->left = 0;
	/* sanitize left and top in case someone passes ~0 as the value */
	r->left &= 0xfffe;
	r->top &= 0xfffe;
	if (r->left + w > MAX_WIDTH)
		r->left = MAX_WIDTH - w;
	if (r->top + h > MAX_HEIGHT)
		r->top = MAX_HEIGHT - h;
	if ((flags & (V4L2_SEL_FLAG_GE | V4L2_SEL_FLAG_LE)) ==
			(V4L2_SEL_FLAG_GE | V4L2_SEL_FLAG_LE) &&
	    (r->width != w || r->height != h))
		return -ERANGE;
	r->width = w;
	r->height = h;
	return 0;
}

int vivid_enum_fmt_vid(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct vivid_dev *dev = video_drvdata(file);
	const struct vivid_fmt *fmt;

	if (f->index >= ARRAY_SIZE(vivid_formats) -
	    (dev->multiplanar ? 0 : VIVID_MPLANAR_FORMATS))
		return -EINVAL;

	fmt = &vivid_formats[f->index];

	f->pixelformat = fmt->fourcc;
	return 0;
}

int vidioc_enum_fmt_vid_mplane(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct vivid_dev *dev = video_drvdata(file);

	if (!dev->multiplanar)
		return -ENOTTY;
	return vivid_enum_fmt_vid(file, priv, f);
}

int vidioc_enum_fmt_vid(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	struct vivid_dev *dev = video_drvdata(file);

	if (dev->multiplanar)
		return -ENOTTY;
	return vivid_enum_fmt_vid(file, priv, f);
}

int vidioc_g_std(struct file *file, void *priv, v4l2_std_id *id)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);

	if (vdev->vfl_dir == VFL_DIR_RX) {
		if (!vivid_is_sdtv_cap(dev))
			return -ENODATA;
		*id = dev->std_cap;
	} else {
		if (!vivid_is_svid_out(dev))
			return -ENODATA;
		*id = dev->std_out;
	}
	return 0;
}

int vidioc_g_dv_timings(struct file *file, void *_fh,
				    struct v4l2_dv_timings *timings)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);

	if (vdev->vfl_dir == VFL_DIR_RX) {
		if (!vivid_is_hdmi_cap(dev))
			return -ENODATA;
		*timings = dev->dv_timings_cap;
	} else {
		if (!vivid_is_hdmi_out(dev))
			return -ENODATA;
		*timings = dev->dv_timings_out;
	}
	return 0;
}

int vidioc_enum_dv_timings(struct file *file, void *_fh,
				    struct v4l2_enum_dv_timings *timings)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);

	if (vdev->vfl_dir == VFL_DIR_RX) {
		if (!vivid_is_hdmi_cap(dev))
			return -ENODATA;
	} else {
		if (!vivid_is_hdmi_out(dev))
			return -ENODATA;
	}
	return v4l2_enum_dv_timings_cap(timings, &vivid_dv_timings_cap,
			NULL, NULL);
}

int vidioc_dv_timings_cap(struct file *file, void *_fh,
				    struct v4l2_dv_timings_cap *cap)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);

	if (vdev->vfl_dir == VFL_DIR_RX) {
		if (!vivid_is_hdmi_cap(dev))
			return -ENODATA;
	} else {
		if (!vivid_is_hdmi_out(dev))
			return -ENODATA;
	}
	*cap = vivid_dv_timings_cap;
	return 0;
}

int vidioc_g_edid(struct file *file, void *_fh,
			 struct v4l2_edid *edid)
{
	struct vivid_dev *dev = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);

	memset(edid->reserved, 0, sizeof(edid->reserved));
	if (vdev->vfl_dir == VFL_DIR_RX) {
		if (edid->pad >= dev->num_inputs)
			return -EINVAL;
		if (dev->input_type[edid->pad] != HDMI)
			return -EINVAL;
	} else {
		if (edid->pad >= dev->num_outputs)
			return -EINVAL;
		if (dev->output_type[edid->pad] != HDMI)
			return -EINVAL;
	}
	if (edid->start_block == 0 && edid->blocks == 0) {
		edid->blocks = dev->edid_blocks;
		return 0;
	}
	if (dev->edid_blocks == 0)
		return -ENODATA;
	if (edid->start_block >= dev->edid_blocks)
		return -EINVAL;
	if (edid->start_block + edid->blocks > dev->edid_blocks)
		edid->blocks = dev->edid_blocks - edid->start_block;
	memcpy(edid->edid, dev->edid, edid->blocks * 128);
	return 0;
}
