// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/math64.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-dv-timings.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>

#include "hws.h"
#include "hws_reg.h"
#include "hws_video.h"
#include "hws_v4l2_ioctl.h"

struct hws_dv_mode {
	struct v4l2_dv_timings timings;
	u32 refresh_hz;
};

static const struct hws_dv_mode *
hws_find_dv_by_wh(u32 w, u32 h, bool interlaced);
static const struct hws_dv_mode *
hws_find_dv_by_wh_fps(u32 w, u32 h, bool interlaced, u32 fps);
static u32 hws_get_live_fps(struct hws_video *vid);
static u32 hws_input_status(struct hws_video *vid);
static int hws_fill_dv_timings(u32 w, u32 h, bool interlace, u32 fps,
			       struct v4l2_dv_timings *timings);

static const struct hws_dv_mode hws_dv_modes[] = {
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1920,
				.height = 1080,
				.hfrontporch = 88,
				.hsync = 44,
				.hbackporch = 148,
				.vfrontporch = 4,
				.vsync = 5,
				.vbackporch = 36,
				.pixelclock = 148500000,
				.polarities = V4L2_DV_VSYNC_POS_POL |
					      V4L2_DV_HSYNC_POS_POL,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1920,
				.height = 1080,
				.hfrontporch = 88,
				.hsync = 44,
				.hbackporch = 148,
				.vfrontporch = 4,
				.vsync = 5,
				.vbackporch = 36,
				.pixelclock = 74250000,
				.polarities = V4L2_DV_VSYNC_POS_POL |
					      V4L2_DV_HSYNC_POS_POL,
				.interlaced = 0,
			},
		},
		30,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1280,
				.height = 720,
				.hfrontporch = 110,
				.hsync = 40,
				.hbackporch = 220,
				.vfrontporch = 5,
				.vsync = 5,
				.vbackporch = 20,
				.pixelclock = 74250000,
				.polarities = V4L2_DV_VSYNC_POS_POL |
					      V4L2_DV_HSYNC_POS_POL,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 720,
				.height = 480,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 720,
				.height = 576,
				.interlaced = 0,
			},
		},
		50,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 800,
				.height = 600,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 640,
				.height = 480,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1024,
				.height = 768,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1280,
				.height = 768,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1280,
				.height = 800,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1280,
				.height = 1024,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1360,
				.height = 768,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1440,
				.height = 900,
				.interlaced = 0,
			},
		},
		60,
	},
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1680,
				.height = 1050,
				.interlaced = 0,
			},
		},
		60,
	},
	/* Portrait */
	{
		{
			.type = V4L2_DV_BT_656_1120,
			.bt = {
				.width = 1080,
				.height = 1920,
				.interlaced = 0,
			},
		},
		60,
	},
};

static const size_t hws_dv_modes_cnt = ARRAY_SIZE(hws_dv_modes);

/* YUYV: 16 bpp; align to 64 as you did elsewhere */
static inline u32 hws_calc_bpl_yuyv(u32 w)     { return ALIGN(w * 2, 64); }
static inline u32 hws_calc_size_yuyv(u32 w, u32 h) { return hws_calc_bpl_yuyv(w) * h; }
static inline u32 hws_calc_half_size(u32 sizeimage)
{
	return sizeimage / 2;
}

static inline void hws_hw_write_bchs(struct hws_pcie_dev *hws, unsigned int ch,
				     u8 br, u8 co, u8 hu, u8 sa)
{
	u32 packed = (sa << 24) | (hu << 16) | (co << 8) | br;

	if (!hws || !hws->bar0_base || ch >= hws->max_channels)
		return;
	writel_relaxed(packed, hws->bar0_base + HWS_REG_BCHS(ch));
	(void)readl(hws->bar0_base + HWS_REG_BCHS(ch)); /* post write */
}

/* Helper: find a supported DV mode by W/H + interlace flag */
static const struct hws_dv_mode *
hws_match_supported_dv(const struct v4l2_dv_timings *req)
{
	const struct v4l2_bt_timings *bt;
	u32 fps;

	if (!req || req->type != V4L2_DV_BT_656_1120)
		return NULL;

	bt = &req->bt;
	fps = 0;
	if (bt->pixelclock) {
		u32 total_w = bt->width + bt->hfrontporch + bt->hsync +
			      bt->hbackporch;
		u32 total_h = bt->height + bt->vfrontporch + bt->vsync +
			      bt->vbackporch;

		if (total_w && total_h)
			fps = DIV_ROUND_CLOSEST_ULL((u64)bt->pixelclock,
						    (u64)total_w * total_h);
	}
	if (fps) {
		const struct hws_dv_mode *exact =
			hws_find_dv_by_wh_fps(bt->width, bt->height,
					      !!bt->interlaced, fps);
		if (exact)
			return exact;
	}
	return hws_find_dv_by_wh(bt->width, bt->height, !!bt->interlaced);
}

/* Helper: find a supported DV mode by W/H + interlace flag */
static const struct hws_dv_mode *
hws_find_dv_by_wh(u32 w, u32 h, bool interlaced)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(hws_dv_modes); i++) {
		const struct hws_dv_mode *t = &hws_dv_modes[i];
		const struct v4l2_bt_timings *bt = &t->timings.bt;

		if (t->timings.type != V4L2_DV_BT_656_1120)
			continue;

		if (bt->width == w && bt->height == h &&
		    !!bt->interlaced == interlaced)
			return t;
	}
	return NULL;
}

static const struct hws_dv_mode *
hws_find_dv_by_wh_fps(u32 w, u32 h, bool interlaced, u32 fps)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(hws_dv_modes); i++) {
		const struct hws_dv_mode *t = &hws_dv_modes[i];
		const struct v4l2_bt_timings *bt = &t->timings.bt;

		if (t->timings.type != V4L2_DV_BT_656_1120)
			continue;

		if (bt->width == w && bt->height == h &&
		    !!bt->interlaced == interlaced &&
		    t->refresh_hz == fps)
			return t;
	}
	return NULL;
}

static bool hws_get_live_dv_geometry(struct hws_video *vid,
				     u32 *w, u32 *h, bool *interlaced)
{
	struct hws_pcie_dev *pdx;
	u32 reg;

	if (!vid)
		return false;

	pdx = vid->parent;
	if (!pdx || !pdx->bar0_base)
		return false;

	reg = readl(pdx->bar0_base + HWS_REG_IN_RES(vid->channel_index));
	if (!reg || reg == 0xFFFFFFFF)
		return false;

	if (w)
		*w = reg & 0xFFFF;
	if (h)
		*h = (reg >> 16) & 0xFFFF;
	if (interlaced) {
		reg = readl(pdx->bar0_base + HWS_REG_ACTIVE_STATUS);
		*interlaced = !!(reg & BIT(8 + vid->channel_index));
	}
	return true;
}

static u32 hws_get_live_fps(struct hws_video *vid)
{
	struct hws_pcie_dev *pdx;
	u32 fps;

	if (!vid)
		return 0;

	pdx = vid->parent;
	if (!pdx || !pdx->bar0_base)
		return 0;

	fps = readl(pdx->bar0_base + HWS_REG_FRAME_RATE(vid->channel_index));
	if (!fps || fps == 0xFFFFFFFF || fps > 240)
		return 0;

	return fps;
}

static u32 hws_pick_fps_from_mode(u32 w, u32 h, bool interlaced)
{
	const struct hws_dv_mode *m = hws_find_dv_by_wh(w, h, interlaced);

	if (m && m->refresh_hz)
		return m->refresh_hz;
	/* Fallback to a sane default */
	return 60;
}

static int hws_fill_dv_timings(u32 w, u32 h, bool interlace, u32 fps,
			       struct v4l2_dv_timings *timings)
{
	const struct hws_dv_mode *m;

	m = fps ? hws_find_dv_by_wh_fps(w, h, interlace, fps) : NULL;
	if (!m)
		m = hws_find_dv_by_wh(w, h, interlace);
	if (!m)
		return -ENOLINK;

	*timings = m->timings;
	return 0;
}

static u32 hws_input_status(struct hws_video *vid)
{
	struct hws_pcie_dev *pdx;
	u32 reg;

	if (!vid)
		return V4L2_IN_ST_NO_SIGNAL;

	pdx = vid->parent;
	if (!pdx || !pdx->bar0_base)
		return V4L2_IN_ST_NO_SIGNAL;

	reg = readl(pdx->bar0_base + HWS_REG_ACTIVE_STATUS);
	if (reg == 0xffffffff)
		return V4L2_IN_ST_NO_SIGNAL;

	return (reg & BIT(vid->channel_index)) ? 0 : V4L2_IN_ST_NO_SIGNAL;
}

/* Query the *current detected* DV timings on the input.
 * If you have a real hardware detector, call it here; otherwise we
 * derive from the cached pix state and map to the closest supported DV mode.
 */
int hws_vidioc_query_dv_timings(struct file *file, void *fh,
				struct v4l2_dv_timings *timings)
{
	struct hws_video *vid = video_drvdata(file);
	u32 w, h;
	u32 fps;
	bool interlace;

	if (!timings)
		return -EINVAL;

	w = vid->pix.width;
	h = vid->pix.height;
	interlace = vid->pix.interlaced;
	hws_get_live_dv_geometry(vid, &w, &h, &interlace);
	fps = hws_get_live_fps(vid);
	if (!fps)
		fps = vid->current_fps ? vid->current_fps :
		      hws_pick_fps_from_mode(w, h, interlace);

	return hws_fill_dv_timings(w, h, interlace, fps, timings);
}

/* Enumerate the Nth supported DV timings from our static table. */
int hws_vidioc_enum_dv_timings(struct file *file, void *fh,
			       struct v4l2_enum_dv_timings *edv)
{
	struct hws_video *vid = video_drvdata(file);
	const struct hws_dv_mode *m;
	u32 w, h;
	u32 fps;
	bool interlace;

	if (!edv)
		return -EINVAL;

	if (edv->pad)
		return -EINVAL;

	w = 0;
	h = 0;
	interlace = false;
	if (hws_get_live_dv_geometry(vid, &w, &h, &interlace)) {
		fps = hws_get_live_fps(vid);
		if (!fps)
			fps = vid->current_fps ? vid->current_fps :
			      hws_pick_fps_from_mode(w, h, interlace);
		m = fps ? hws_find_dv_by_wh_fps(w, h, interlace, fps) : NULL;
		if (!m)
			m = hws_find_dv_by_wh(w, h, interlace);
		if (m) {
			if (edv->index)
				return -EINVAL;
			edv->timings = m->timings;
			return 0;
		}
	}

	if (edv->index >= hws_dv_modes_cnt)
		return -EINVAL;

	edv->timings = hws_dv_modes[edv->index].timings;
	return 0;
}

/* Get the *currently configured* DV timings. */
int hws_vidioc_g_dv_timings(struct file *file, void *fh,
			    struct v4l2_dv_timings *timings)
{
	struct hws_video *vid = video_drvdata(file);
	u32 w, h;
	u32 fps;
	bool interlace;

	if (!timings)
		return -EINVAL;

	w = vid->pix.width;
	h = vid->pix.height;
	interlace = vid->pix.interlaced;
	if (hws_get_live_dv_geometry(vid, &w, &h, &interlace)) {
		fps = hws_get_live_fps(vid);
		if (!fps)
			fps = vid->current_fps ? vid->current_fps :
			      hws_pick_fps_from_mode(w, h, interlace);
		return hws_fill_dv_timings(w, h, interlace, fps, timings);
	}

	*timings = vid->cur_dv_timings;
	return 0;
}

static inline void hws_set_colorimetry_state(struct hws_pix_state *p)
{
	bool sd = p->height <= 576;

	p->colorspace   = sd ? V4L2_COLORSPACE_SMPTE170M : V4L2_COLORSPACE_REC709;
	p->ycbcr_enc    = V4L2_YCBCR_ENC_DEFAULT;
	p->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	p->xfer_func    = V4L2_XFER_FUNC_DEFAULT;
}

/* Set DV timings: must match one of our supported modes.
 * If buffers are queued and this implies a size change, we reject with -EBUSY.
 * Otherwise we update pix state and (optionally) reprogram the HW.
 */
int hws_vidioc_s_dv_timings(struct file *file, void *fh,
			    struct v4l2_dv_timings *timings)
{
	struct hws_video *vid = video_drvdata(file);
	const struct hws_dv_mode *m;
	const struct v4l2_bt_timings *bt;
	u32 new_w, new_h;
	bool interlaced;
	int ret = 0;
	unsigned long was_busy;
	u32 live_w, live_h;
	u32 live_fps;
	bool live_interlaced;
	bool live_present;

	if (!timings)
		return -EINVAL;

	m = hws_match_supported_dv(timings);
	if (!m)
		return -EINVAL;

	bt = &m->timings.bt;
	if (bt->interlaced)
		return -EINVAL; /* only progressive modes are advertised */
	new_w = bt->width;
	new_h = bt->height;
	interlaced = false;

	lockdep_assert_held(&vid->state_lock);
	live_present = hws_get_live_dv_geometry(vid, &live_w, &live_h,
						&live_interlaced);

	/* If vb2 has active buffers and size would change, reject. */
	was_busy = vb2_is_busy(&vid->buffer_queue);
	if (was_busy &&
	    (new_w != vid->pix.width || new_h != vid->pix.height ||
	     interlaced != vid->pix.interlaced)) {
		ret = -EBUSY;
		return ret;
	}

	/* When a live input signal is present, the receiver owns the timing.
	 * Allow setting the already-active timings so v4l2-compliance can
	 * round-trip them, but reject attempts to retime the live source.
	 */
	if (live_present) {
		live_fps = hws_get_live_fps(vid);
		if (!live_fps)
			live_fps = vid->current_fps ? vid->current_fps :
					hws_pick_fps_from_mode(live_w, live_h,
							       live_interlaced);
		if (live_w == new_w && live_h == new_h &&
		    live_interlaced == interlaced &&
		    m->refresh_hz == live_fps)
			return 0;
		return -EBUSY;
	}

	/* Update software pixel state (and recalc sizes) */
	vid->pix.width      = new_w;
	vid->pix.height     = new_h;
	vid->pix.field      = interlaced ? V4L2_FIELD_INTERLACED
					 : V4L2_FIELD_NONE;
	vid->pix.interlaced = interlaced;
	vid->pix.fourcc     = V4L2_PIX_FMT_YUYV;

	hws_set_colorimetry_state(&vid->pix);

	/* Recompute stride, sizeimage, and half_size. */
	vid->pix.bytesperline = hws_calc_bpl_yuyv(new_w);
	vid->pix.sizeimage    = hws_calc_size_yuyv(new_w, new_h);
	vid->pix.half_size    = hws_calc_half_size(vid->pix.sizeimage);
	vid->cur_dv_timings   = m->timings;
	vid->current_fps      = m->refresh_hz;
	return ret;
}

/* Report DV timings capability: advertise BT.656/1120 with
 * the min/max WxH derived from our table and basic progressive support.
 */
int hws_vidioc_dv_timings_cap(struct file *file, void *fh,
			      struct v4l2_dv_timings_cap *cap)
{
	u32 min_w = ~0U, min_h = ~0U;
	u32 max_w = 0,       max_h = 0;
	size_t i, n = 0;

	if (!cap)
		return -EINVAL;

	memset(cap, 0, sizeof(*cap));
	cap->type = V4L2_DV_BT_656_1120;

	for (i = 0; i < ARRAY_SIZE(hws_dv_modes); i++) {
		const struct v4l2_bt_timings *bt = &hws_dv_modes[i].timings.bt;

		if (hws_dv_modes[i].timings.type != V4L2_DV_BT_656_1120)
			continue;
		n++;

		if (bt->width  < min_w)
			min_w = bt->width;
		if (bt->height < min_h)
			min_h = bt->height;
		if (bt->width  > max_w)
			max_w = bt->width;
		if (bt->height > max_h)
			max_h = bt->height;
	}

	/* If the table was empty, fail gracefully. */
	if (!n || min_w == U32_MAX)
		return -ENODATA;

	cap->bt.min_width  = min_w;
	cap->bt.max_width  = max_w;
	cap->bt.min_height = min_h;
	cap->bt.max_height = max_h;

	/* We support both CEA-861- and VESA-style modes in the list. */
	cap->bt.standards =
		V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT | V4L2_DV_BT_STD_CVT;

	/* Only progressive modes are advertised. */
	cap->bt.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE;

	/* Leave pixelclock/porch limits unconstrained (0) for now. */
	return 0;
}

static int hws_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hws_video *vid =
		container_of(ctrl->handler, struct hws_video, control_handler);
	struct hws_pcie_dev *pdx = vid->parent;
	bool program = false;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		vid->current_brightness = ctrl->val;
		program = true;
		break;
	case V4L2_CID_CONTRAST:
		vid->current_contrast = ctrl->val;
		program = true;
		break;
	case V4L2_CID_SATURATION:
		vid->current_saturation = ctrl->val;
		program = true;
		break;
	case V4L2_CID_HUE:
		vid->current_hue = ctrl->val;
		program = true;
		break;
	default:
		return -EINVAL;
	}

	if (program) {
		hws_hw_write_bchs(pdx, vid->channel_index,
				  (u8)vid->current_brightness,
				  (u8)vid->current_contrast,
				  (u8)vid->current_hue,
				  (u8)vid->current_saturation);
	}
	return 0;
}

const struct v4l2_ctrl_ops hws_ctrl_ops = {
	.s_ctrl = hws_s_ctrl,
};

int hws_vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct hws_video *vid = video_drvdata(file);
	int vi_index = vid->channel_index + 1; /* keep it simple */

	strscpy(cap->driver, KBUILD_MODNAME, sizeof(cap->driver));
	snprintf(cap->card, sizeof(cap->card),
		 "AVMatrix HWS Capture %d", vi_index);
	return 0;
}

int hws_vidioc_enum_fmt_vid_cap(struct file *file, void *priv_fh, struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL; /* only one format */

	f->pixelformat = V4L2_PIX_FMT_YUYV;
	return 0;
}

int hws_vidioc_g_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct hws_video *vid = video_drvdata(file);

	fmt->fmt.pix.width        = vid->pix.width;
	fmt->fmt.pix.height       = vid->pix.height;
	fmt->fmt.pix.pixelformat  = V4L2_PIX_FMT_YUYV;
	fmt->fmt.pix.field        = vid->pix.field;
	fmt->fmt.pix.bytesperline = vid->pix.bytesperline;
	fmt->fmt.pix.sizeimage    = vid->pix.sizeimage;
	fmt->fmt.pix.colorspace   = vid->pix.colorspace;
	fmt->fmt.pix.ycbcr_enc    = vid->pix.ycbcr_enc;
	fmt->fmt.pix.quantization = vid->pix.quantization;
	fmt->fmt.pix.xfer_func    = vid->pix.xfer_func;
	return 0;
}

static inline void hws_set_colorimetry_fmt(struct v4l2_pix_format *p)
{
	bool sd = p->height <= 576;

	p->colorspace   = sd ? V4L2_COLORSPACE_SMPTE170M : V4L2_COLORSPACE_REC709;
	p->ycbcr_enc    = V4L2_YCBCR_ENC_DEFAULT;
	p->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	p->xfer_func    = V4L2_XFER_FUNC_DEFAULT;
}

int hws_vidioc_try_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct hws_video *vid = file ? video_drvdata(file) : NULL;
	struct hws_pcie_dev *pdev = vid ? vid->parent : NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	u32 req_w = pix->width, req_h = pix->height;
	u32 w, h, min_bpl, bpl;
	size_t size; /* wider than u32 for overflow check */
	size_t max_frame = pdev ? pdev->max_hw_video_buf_sz : MAX_MM_VIDEO_SIZE;

	/* Only YUYV */
	pix->pixelformat = V4L2_PIX_FMT_YUYV;

	/* Defaults then clamp */
	w = (req_w ? req_w : 640);
	h = (req_h ? req_h : 480);
	if (w > MAX_VIDEO_HW_W)
		w = MAX_VIDEO_HW_W;
	if (h > MAX_VIDEO_HW_H)
		h = MAX_VIDEO_HW_H;
	if (!w)
		w = 640; /* hard fallback in case macros are odd */
	if (!h)
		h = 480;

	/* Field policy */
	pix->field = V4L2_FIELD_NONE;

	/* Stride policy for packed 16bpp, 64B align */
	min_bpl = ALIGN(w * 2, 64);

	/* Bound requested bpl to something sane, then align */
	bpl = pix->bytesperline;
	if (bpl < min_bpl) {
		bpl = min_bpl;
	} else {
		/* Cap at 16x width to avoid silly values that overflow sizeimage */
		u32 max_bpl = ALIGN(w * 2 * 16, 64);

		if (bpl > max_bpl)
			bpl = max_bpl;
		bpl = ALIGN(bpl, 64);
	}
	if (h && max_frame) {
		size_t max_bpl_hw = max_frame / h;

		if (max_bpl_hw < min_bpl)
			return -ERANGE;
		max_bpl_hw = rounddown(max_bpl_hw, 64);
		if (!max_bpl_hw)
			return -ERANGE;
		if (bpl > max_bpl_hw) {
			if (pdev)
				dev_dbg(&pdev->pdev->dev,
					"try_fmt: clamp bpl %u -> %zu due to hw buf cap %zu\n",
					bpl, max_bpl_hw, max_frame);
			bpl = (u32)max_bpl_hw;
		}
	}
	size = (size_t)bpl * (size_t)h;
	if (size > max_frame)
		return -ERANGE;

	pix->width        = w;
	pix->height       = h;
	pix->bytesperline = bpl;
	pix->sizeimage    = (u32)size; /* logical size, not page-aligned */

	hws_set_colorimetry_fmt(pix);
	if (pdev)
		dev_dbg(&pdev->pdev->dev,
			"try_fmt: w=%u h=%u bpl=%u size=%u field=%u\n",
			pix->width, pix->height, pix->bytesperline,
			pix->sizeimage, pix->field);
	return 0;
}

int hws_vidioc_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct hws_video *vid = video_drvdata(file);
	int ret;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* Normalize the request */
	ret = hws_vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	/* Don't allow buffer layout changes while buffers are queued. */
	if (vb2_is_busy(&vid->buffer_queue)) {
		if (f->fmt.pix.width  != vid->pix.width  ||
		    f->fmt.pix.height != vid->pix.height ||
		    f->fmt.pix.bytesperline != vid->pix.bytesperline)
			return -EBUSY;
	}

	/* Apply to driver state */
	vid->pix.width        = f->fmt.pix.width;
	vid->pix.height       = f->fmt.pix.height;
	vid->pix.fourcc       = V4L2_PIX_FMT_YUYV;
	vid->pix.field        = f->fmt.pix.field;
	vid->pix.colorspace   = f->fmt.pix.colorspace;
	vid->pix.ycbcr_enc    = f->fmt.pix.ycbcr_enc;
	vid->pix.quantization = f->fmt.pix.quantization;
	vid->pix.xfer_func    = f->fmt.pix.xfer_func;

	/* Update negotiated buffer sizes. */
	vid->pix.bytesperline = f->fmt.pix.bytesperline; /* aligned */
	vid->pix.sizeimage    = f->fmt.pix.sizeimage;    /* logical */
	vid->pix.half_size    = hws_calc_half_size(vid->pix.sizeimage);
	vid->pix.interlaced   = false;
	/* S_FMT negotiates buffer layout only. Keep detector-owned DV timing
	 * state unchanged so a harmless restart cannot clobber the live FPS.
	 */
	/* Or:
	 * hws_calc_sizeimage(vid, vid->pix.width, vid->pix.height, false);
	 */

	dev_dbg(&vid->parent->pdev->dev,
		"s_fmt:   w=%u h=%u bpl=%u size=%u\n",
		vid->pix.width, vid->pix.height, vid->pix.bytesperline,
		vid->pix.sizeimage);

	return 0;
}

int hws_vidioc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *param)
{
	struct hws_video *vid = video_drvdata(file);
	u32 fps;

	if (param->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	fps = hws_get_live_fps(vid);
	if (!fps)
		fps = vid->current_fps ? vid->current_fps : 60;

	/* HDMI receivers report the detected frame period, they don't set it. */
	param->parm.capture.capability           = 0;
	param->parm.capture.capturemode          = 0;
	param->parm.capture.timeperframe.numerator   = 1;
	param->parm.capture.timeperframe.denominator = fps;
	param->parm.capture.extendedmode         = 0;
	param->parm.capture.readbuffers          = 0;

	return 0;
}

int hws_vidioc_enum_input(struct file *file, void *priv,
			  struct v4l2_input *input)
{
	struct hws_video *vid = video_drvdata(file);

	if (input->index)
		return -EINVAL;
	input->type         = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, KBUILD_MODNAME, sizeof(input->name));
	input->capabilities = V4L2_IN_CAP_DV_TIMINGS;
	input->status       = hws_input_status(vid);

	return 0;
}

int hws_vidioc_g_input(struct file *file, void *priv, unsigned int *index)
{
	*index = 0;
	return 0;
}

int hws_vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}
