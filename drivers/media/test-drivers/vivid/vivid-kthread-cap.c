// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-kthread-cap.h - video/vbi capture thread support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/font.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/random.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/jiffies.h>
#include <asm/div64.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-rect.h>

#include "vivid-core.h"
#include "vivid-vid-common.h"
#include "vivid-vid-cap.h"
#include "vivid-vid-out.h"
#include "vivid-radio-common.h"
#include "vivid-radio-rx.h"
#include "vivid-radio-tx.h"
#include "vivid-sdr-cap.h"
#include "vivid-vbi-cap.h"
#include "vivid-vbi-out.h"
#include "vivid-osd.h"
#include "vivid-ctrls.h"
#include "vivid-kthread-cap.h"
#include "vivid-meta-cap.h"

static inline v4l2_std_id vivid_get_std_cap(const struct vivid_dev *dev)
{
	if (vivid_is_sdtv_cap(dev))
		return dev->std_cap[dev->input];
	return 0;
}

static void copy_pix(struct vivid_dev *dev, int win_y, int win_x,
			u16 *cap, const u16 *osd)
{
	u16 out;

	out = *cap;
	*cap = *osd;

	if ((dev->fbuf_out_flags & V4L2_FBUF_FLAG_CHROMAKEY) &&
	    *osd != dev->chromakey_out)
		return;
	if ((dev->fbuf_out_flags & V4L2_FBUF_FLAG_SRC_CHROMAKEY) &&
	    out == dev->chromakey_out)
		return;
	if (dev->fmt_cap->alpha_mask) {
		if ((dev->fbuf_out_flags & V4L2_FBUF_FLAG_GLOBAL_ALPHA) &&
		    dev->global_alpha_out)
			return;
		if ((dev->fbuf_out_flags & V4L2_FBUF_FLAG_LOCAL_ALPHA) &&
		    *cap & dev->fmt_cap->alpha_mask)
			return;
		if ((dev->fbuf_out_flags & V4L2_FBUF_FLAG_LOCAL_INV_ALPHA) &&
		    !(*cap & dev->fmt_cap->alpha_mask))
			return;
	}
	*cap = out;
}

static void blend_line(struct vivid_dev *dev, unsigned y_offset, unsigned x_offset,
		u8 *vcapbuf, const u8 *vosdbuf,
		unsigned width, unsigned pixsize)
{
	unsigned x;

	for (x = 0; x < width; x++, vcapbuf += pixsize, vosdbuf += pixsize) {
		copy_pix(dev, y_offset, x_offset + x,
			 (u16 *)vcapbuf, (const u16 *)vosdbuf);
	}
}

static void scale_line(const u8 *src, u8 *dst, unsigned srcw, unsigned dstw, unsigned twopixsize)
{
	/* Coarse scaling with Bresenham */
	unsigned int_part;
	unsigned fract_part;
	unsigned src_x = 0;
	unsigned error = 0;
	unsigned x;

	/*
	 * We always combine two pixels to prevent color bleed in the packed
	 * yuv case.
	 */
	srcw /= 2;
	dstw /= 2;
	int_part = srcw / dstw;
	fract_part = srcw % dstw;
	for (x = 0; x < dstw; x++, dst += twopixsize) {
		memcpy(dst, src + src_x * twopixsize, twopixsize);
		src_x += int_part;
		error += fract_part;
		if (error >= dstw) {
			error -= dstw;
			src_x++;
		}
	}
}

/*
 * Precalculate the rectangles needed to perform video looping:
 *
 * The nominal pipeline is that the video output buffer is cropped by
 * crop_out, scaled to compose_out, overlaid with the output overlay,
 * cropped on the capture side by crop_cap and scaled again to the video
 * capture buffer using compose_cap.
 *
 * To keep things efficient we calculate the intersection of compose_out
 * and crop_cap (since that's the only part of the video that will
 * actually end up in the capture buffer), determine which part of the
 * video output buffer that is and which part of the video capture buffer
 * so we can scale the video straight from the output buffer to the capture
 * buffer without any intermediate steps.
 *
 * If we need to deal with an output overlay, then there is no choice and
 * that intermediate step still has to be taken. For the output overlay
 * support we calculate the intersection of the framebuffer and the overlay
 * window (which may be partially or wholly outside of the framebuffer
 * itself) and the intersection of that with loop_vid_copy (i.e. the part of
 * the actual looped video that will be overlaid). The result is calculated
 * both in framebuffer coordinates (loop_fb_copy) and compose_out coordinates
 * (loop_vid_overlay). Finally calculate the part of the capture buffer that
 * will receive that overlaid video.
 */
static void vivid_precalc_copy_rects(struct vivid_dev *dev, struct vivid_dev *out_dev)
{
	/* Framebuffer rectangle */
	struct v4l2_rect r_fb = {
		0, 0, dev->display_width, dev->display_height
	};
	/* Overlay window rectangle in framebuffer coordinates */
	struct v4l2_rect r_overlay = {
		out_dev->overlay_out_left, out_dev->overlay_out_top,
		out_dev->compose_out.width, out_dev->compose_out.height
	};

	v4l2_rect_intersect(&dev->loop_vid_copy, &dev->crop_cap, &out_dev->compose_out);

	dev->loop_vid_out = dev->loop_vid_copy;
	v4l2_rect_scale(&dev->loop_vid_out, &out_dev->compose_out, &out_dev->crop_out);
	dev->loop_vid_out.left += out_dev->crop_out.left;
	dev->loop_vid_out.top += out_dev->crop_out.top;

	dev->loop_vid_cap = dev->loop_vid_copy;
	v4l2_rect_scale(&dev->loop_vid_cap, &dev->crop_cap, &dev->compose_cap);

	dprintk(dev, 1,
		"loop_vid_copy: %dx%d@%dx%d loop_vid_out: %dx%d@%dx%d loop_vid_cap: %dx%d@%dx%d\n",
		dev->loop_vid_copy.width, dev->loop_vid_copy.height,
		dev->loop_vid_copy.left, dev->loop_vid_copy.top,
		dev->loop_vid_out.width, dev->loop_vid_out.height,
		dev->loop_vid_out.left, dev->loop_vid_out.top,
		dev->loop_vid_cap.width, dev->loop_vid_cap.height,
		dev->loop_vid_cap.left, dev->loop_vid_cap.top);

	v4l2_rect_intersect(&r_overlay, &r_fb, &r_overlay);

	/* shift r_overlay to the same origin as compose_out */
	r_overlay.left += out_dev->compose_out.left - out_dev->overlay_out_left;
	r_overlay.top += out_dev->compose_out.top - out_dev->overlay_out_top;

	v4l2_rect_intersect(&dev->loop_vid_overlay, &r_overlay, &dev->loop_vid_copy);
	dev->loop_fb_copy = dev->loop_vid_overlay;

	/* shift dev->loop_fb_copy back again to the fb origin */
	dev->loop_fb_copy.left -= out_dev->compose_out.left - out_dev->overlay_out_left;
	dev->loop_fb_copy.top -= out_dev->compose_out.top - out_dev->overlay_out_top;

	dev->loop_vid_overlay_cap = dev->loop_vid_overlay;
	v4l2_rect_scale(&dev->loop_vid_overlay_cap, &dev->crop_cap, &dev->compose_cap);

	dprintk(dev, 1,
		"loop_fb_copy: %dx%d@%dx%d loop_vid_overlay: %dx%d@%dx%d loop_vid_overlay_cap: %dx%d@%dx%d\n",
		dev->loop_fb_copy.width, dev->loop_fb_copy.height,
		dev->loop_fb_copy.left, dev->loop_fb_copy.top,
		dev->loop_vid_overlay.width, dev->loop_vid_overlay.height,
		dev->loop_vid_overlay.left, dev->loop_vid_overlay.top,
		dev->loop_vid_overlay_cap.width, dev->loop_vid_overlay_cap.height,
		dev->loop_vid_overlay_cap.left, dev->loop_vid_overlay_cap.top);
}

static void *plane_vaddr(struct tpg_data *tpg, struct vivid_buffer *buf,
			 unsigned p, unsigned bpl[TPG_MAX_PLANES], unsigned h)
{
	unsigned i;
	void *vbuf;

	if (p == 0 || tpg_g_buffers(tpg) > 1)
		return vb2_plane_vaddr(&buf->vb.vb2_buf, p);
	vbuf = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
	for (i = 0; i < p; i++)
		vbuf += bpl[i] * h / tpg->vdownsampling[i];
	return vbuf;
}

static noinline_for_stack int vivid_copy_buffer(struct vivid_dev *dev,
						struct vivid_dev *out_dev, unsigned p,
						u8 *vcapbuf, struct vivid_buffer *vid_cap_buf)
{
	bool blank = dev->must_blank[vid_cap_buf->vb.vb2_buf.index];
	struct tpg_data *tpg = &dev->tpg;
	struct vivid_buffer *vid_out_buf = NULL;
	unsigned vdiv = out_dev->fmt_out->vdownsampling[p];
	unsigned twopixsize = tpg_g_twopixelsize(tpg, p);
	unsigned img_width = tpg_hdiv(tpg, p, dev->compose_cap.width);
	unsigned img_height = dev->compose_cap.height;
	unsigned stride_cap = tpg->bytesperline[p];
	unsigned stride_out = out_dev->bytesperline_out[p];
	unsigned stride_osd = dev->display_byte_stride;
	unsigned hmax = (img_height * tpg->perc_fill) / 100;
	u8 *voutbuf;
	u8 *vosdbuf = NULL;
	unsigned y;
	bool blend = out_dev->fbuf_out_flags;
	/* Coarse scaling with Bresenham */
	unsigned vid_out_int_part;
	unsigned vid_out_fract_part;
	unsigned vid_out_y = 0;
	unsigned vid_out_error = 0;
	unsigned vid_overlay_int_part = 0;
	unsigned vid_overlay_fract_part = 0;
	unsigned vid_overlay_y = 0;
	unsigned vid_overlay_error = 0;
	unsigned vid_cap_left = tpg_hdiv(tpg, p, dev->loop_vid_cap.left);
	unsigned vid_cap_right;
	bool quick;

	vid_out_int_part = dev->loop_vid_out.height / dev->loop_vid_cap.height;
	vid_out_fract_part = dev->loop_vid_out.height % dev->loop_vid_cap.height;

	if (!list_empty(&out_dev->vid_out_active))
		vid_out_buf = list_entry(out_dev->vid_out_active.next,
					 struct vivid_buffer, list);
	if (vid_out_buf == NULL)
		return -ENODATA;

	vid_cap_buf->vb.field = vid_out_buf->vb.field;

	voutbuf = plane_vaddr(tpg, vid_out_buf, p,
			      out_dev->bytesperline_out, out_dev->fmt_out_rect.height);
	if (p < out_dev->fmt_out->buffers)
		voutbuf += vid_out_buf->vb.vb2_buf.planes[p].data_offset;
	voutbuf += tpg_hdiv(tpg, p, dev->loop_vid_out.left) +
		(dev->loop_vid_out.top / vdiv) * stride_out;
	vcapbuf += tpg_hdiv(tpg, p, dev->compose_cap.left) +
		(dev->compose_cap.top / vdiv) * stride_cap;

	if (dev->loop_vid_copy.width == 0 || dev->loop_vid_copy.height == 0) {
		/*
		 * If there is nothing to copy, then just fill the capture window
		 * with black.
		 */
		for (y = 0; y < hmax / vdiv; y++, vcapbuf += stride_cap)
			memcpy(vcapbuf, tpg->black_line[p], img_width);
		return 0;
	}

	if (out_dev->overlay_out_enabled &&
	    dev->loop_vid_overlay.width && dev->loop_vid_overlay.height) {
		vosdbuf = dev->video_vbase;
		vosdbuf += (dev->loop_fb_copy.left * twopixsize) / 2 +
			   dev->loop_fb_copy.top * stride_osd;
		vid_overlay_int_part = dev->loop_vid_overlay.height /
				       dev->loop_vid_overlay_cap.height;
		vid_overlay_fract_part = dev->loop_vid_overlay.height %
					 dev->loop_vid_overlay_cap.height;
	}

	vid_cap_right = tpg_hdiv(tpg, p, dev->loop_vid_cap.left + dev->loop_vid_cap.width);
	/* quick is true if no video scaling is needed */
	quick = dev->loop_vid_out.width == dev->loop_vid_cap.width;

	dev->cur_scaled_line = dev->loop_vid_out.height;
	for (y = 0; y < hmax; y += vdiv, vcapbuf += stride_cap) {
		/* osdline is true if this line requires overlay blending */
		bool osdline = vosdbuf && y >= dev->loop_vid_overlay_cap.top &&
			  y < dev->loop_vid_overlay_cap.top + dev->loop_vid_overlay_cap.height;

		/*
		 * If this line of the capture buffer doesn't get any video, then
		 * just fill with black.
		 */
		if (y < dev->loop_vid_cap.top ||
		    y >= dev->loop_vid_cap.top + dev->loop_vid_cap.height) {
			memcpy(vcapbuf, tpg->black_line[p], img_width);
			continue;
		}

		/* fill the left border with black */
		if (dev->loop_vid_cap.left)
			memcpy(vcapbuf, tpg->black_line[p], vid_cap_left);

		/* fill the right border with black */
		if (vid_cap_right < img_width)
			memcpy(vcapbuf + vid_cap_right, tpg->black_line[p],
				img_width - vid_cap_right);

		if (quick && !osdline) {
			memcpy(vcapbuf + vid_cap_left,
			       voutbuf + vid_out_y * stride_out,
			       tpg_hdiv(tpg, p, dev->loop_vid_cap.width));
			goto update_vid_out_y;
		}
		if (dev->cur_scaled_line == vid_out_y) {
			memcpy(vcapbuf + vid_cap_left, dev->scaled_line,
			       tpg_hdiv(tpg, p, dev->loop_vid_cap.width));
			goto update_vid_out_y;
		}
		if (!osdline) {
			scale_line(voutbuf + vid_out_y * stride_out, dev->scaled_line,
				tpg_hdiv(tpg, p, dev->loop_vid_out.width),
				tpg_hdiv(tpg, p, dev->loop_vid_cap.width),
				tpg_g_twopixelsize(tpg, p));
		} else {
			/*
			 * Offset in bytes within loop_vid_copy to the start of the
			 * loop_vid_overlay rectangle.
			 */
			unsigned offset =
				((dev->loop_vid_overlay.left - dev->loop_vid_copy.left) *
				 twopixsize) / 2;
			u8 *osd = vosdbuf + vid_overlay_y * stride_osd;

			scale_line(voutbuf + vid_out_y * stride_out, dev->blended_line,
				dev->loop_vid_out.width, dev->loop_vid_copy.width,
				tpg_g_twopixelsize(tpg, p));
			if (blend)
				blend_line(dev, vid_overlay_y + dev->loop_vid_overlay.top,
					   dev->loop_vid_overlay.left,
					   dev->blended_line + offset, osd,
					   dev->loop_vid_overlay.width, twopixsize / 2);
			else
				memcpy(dev->blended_line + offset,
				       osd, (dev->loop_vid_overlay.width * twopixsize) / 2);
			scale_line(dev->blended_line, dev->scaled_line,
					dev->loop_vid_copy.width, dev->loop_vid_cap.width,
					tpg_g_twopixelsize(tpg, p));
		}
		dev->cur_scaled_line = vid_out_y;
		memcpy(vcapbuf + vid_cap_left, dev->scaled_line,
		       tpg_hdiv(tpg, p, dev->loop_vid_cap.width));

update_vid_out_y:
		if (osdline) {
			vid_overlay_y += vid_overlay_int_part;
			vid_overlay_error += vid_overlay_fract_part;
			if (vid_overlay_error >= dev->loop_vid_overlay_cap.height) {
				vid_overlay_error -= dev->loop_vid_overlay_cap.height;
				vid_overlay_y++;
			}
		}
		vid_out_y += vid_out_int_part;
		vid_out_error += vid_out_fract_part;
		if (vid_out_error >= dev->loop_vid_cap.height / vdiv) {
			vid_out_error -= dev->loop_vid_cap.height / vdiv;
			vid_out_y++;
		}
	}

	if (!blank)
		return 0;
	for (; y < img_height; y += vdiv, vcapbuf += stride_cap)
		memcpy(vcapbuf, tpg->contrast_line[p], img_width);
	return 0;
}

static void vivid_fillbuff(struct vivid_dev *dev, struct vivid_buffer *buf)
{
	struct vivid_dev *out_dev = NULL;
	struct tpg_data *tpg = &dev->tpg;
	unsigned factor = V4L2_FIELD_HAS_T_OR_B(dev->field_cap) ? 2 : 1;
	unsigned line_height = 16 / factor;
	bool is_tv = vivid_is_sdtv_cap(dev);
	bool is_60hz = is_tv && (dev->std_cap[dev->input] & V4L2_STD_525_60);
	unsigned p;
	int line = 1;
	u8 *basep[TPG_MAX_PLANES][2];
	unsigned ms;
	char str[100];
	s32 gain;

	buf->vb.sequence = dev->vid_cap_seq_count;
	v4l2_ctrl_s_ctrl(dev->ro_int32, buf->vb.sequence & 0xff);
	if (dev->field_cap == V4L2_FIELD_ALTERNATE) {
		/*
		 * 60 Hz standards start with the bottom field, 50 Hz standards
		 * with the top field. So if the 0-based seq_count is even,
		 * then the field is TOP for 50 Hz and BOTTOM for 60 Hz
		 * standards.
		 */
		buf->vb.field = ((dev->vid_cap_seq_count & 1) ^ is_60hz) ?
			V4L2_FIELD_BOTTOM : V4L2_FIELD_TOP;
		/*
		 * The sequence counter counts frames, not fields. So divide
		 * by two.
		 */
		buf->vb.sequence /= 2;
	} else {
		buf->vb.field = dev->field_cap;
	}
	tpg_s_field(tpg, buf->vb.field,
		    dev->field_cap == V4L2_FIELD_ALTERNATE);
	tpg_s_perc_fill_blank(tpg, dev->must_blank[buf->vb.vb2_buf.index]);

	if (vivid_vid_can_loop(dev) &&
	    ((vivid_is_svid_cap(dev) &&
	    !VIVID_INVALID_SIGNAL(dev->std_signal_mode[dev->input])) ||
	    (vivid_is_hdmi_cap(dev) &&
	    !VIVID_INVALID_SIGNAL(dev->dv_timings_signal_mode[dev->input])))) {
		out_dev = vivid_input_is_connected_to(dev);
		/*
		 * If the vivid instance of the output device is different
		 * from the vivid instance of this input device, then we
		 * must take care to properly serialize the output device to
		 * prevent that the buffer we are copying from is being freed.
		 *
		 * If the output device is part of the same instance, then the
		 * lock is already taken and there is no need to take the mutex.
		 *
		 * The problem with taking the mutex is that you can get
		 * deadlocked if instance A locks instance B and vice versa.
		 * It is not really worth trying to be very smart about this,
		 * so just try to take the lock, and if you can't, then just
		 * set out_dev to NULL and you will end up with a single frame
		 * of Noise (the default test pattern in this case).
		 */
		if (out_dev && dev != out_dev && !mutex_trylock(&out_dev->mutex))
			out_dev = NULL;
	}

	if (out_dev)
		vivid_precalc_copy_rects(dev, out_dev);

	for (p = 0; p < tpg_g_planes(tpg); p++) {
		void *vbuf = plane_vaddr(tpg, buf, p,
					 tpg->bytesperline, tpg->buf_height);

		/*
		 * The first plane of a multiplanar format has a non-zero
		 * data_offset. This helps testing whether the application
		 * correctly supports non-zero data offsets.
		 */
		if (p < tpg_g_buffers(tpg) && dev->fmt_cap->data_offset[p]) {
			memset(vbuf, dev->fmt_cap->data_offset[p] & 0xff,
			       dev->fmt_cap->data_offset[p]);
			vbuf += dev->fmt_cap->data_offset[p];
		}
		tpg_calc_text_basep(tpg, basep, p, vbuf);
		if (!out_dev || vivid_copy_buffer(dev, out_dev, p, vbuf, buf))
			tpg_fill_plane_buffer(tpg, vivid_get_std_cap(dev),
					p, vbuf);
	}
	if (out_dev && dev != out_dev)
		mutex_unlock(&out_dev->mutex);

	dev->must_blank[buf->vb.vb2_buf.index] = false;

	/* Updates stream time, only update at the start of a new frame. */
	if (dev->field_cap != V4L2_FIELD_ALTERNATE ||
			(dev->vid_cap_seq_count & 1) == 0)
		dev->ms_vid_cap =
			jiffies_to_msecs(jiffies - dev->jiffies_vid_cap);

	ms = dev->ms_vid_cap;
	if (dev->osd_mode <= 1) {
		snprintf(str, sizeof(str), " %02d:%02d:%02d:%03d %u%s",
				(ms / (60 * 60 * 1000)) % 24,
				(ms / (60 * 1000)) % 60,
				(ms / 1000) % 60,
				ms % 1000,
				buf->vb.sequence,
				(dev->field_cap == V4L2_FIELD_ALTERNATE) ?
					(buf->vb.field == V4L2_FIELD_TOP ?
					 " top" : " bottom") : "");
		tpg_gen_text(tpg, basep, line++ * line_height, 16, str);
	}
	if (dev->osd_mode == 0) {
		snprintf(str, sizeof(str), " %dx%d, input %d ",
				dev->src_rect.width, dev->src_rect.height, dev->input);
		tpg_gen_text(tpg, basep, line++ * line_height, 16, str);

		gain = v4l2_ctrl_g_ctrl(dev->gain);
		mutex_lock(dev->ctrl_hdl_user_vid.lock);
		snprintf(str, sizeof(str),
			" brightness %3d, contrast %3d, saturation %3d, hue %d ",
			dev->brightness->cur.val,
			dev->contrast->cur.val,
			dev->saturation->cur.val,
			dev->hue->cur.val);
		tpg_gen_text(tpg, basep, line++ * line_height, 16, str);
		snprintf(str, sizeof(str),
			" autogain %d, gain %3d, alpha 0x%02x ",
			dev->autogain->cur.val, gain, dev->alpha->cur.val);
		mutex_unlock(dev->ctrl_hdl_user_vid.lock);
		tpg_gen_text(tpg, basep, line++ * line_height, 16, str);
		mutex_lock(dev->ctrl_hdl_user_aud.lock);
		snprintf(str, sizeof(str),
			" volume %3d, mute %d ",
			dev->volume->cur.val, dev->mute->cur.val);
		mutex_unlock(dev->ctrl_hdl_user_aud.lock);
		tpg_gen_text(tpg, basep, line++ * line_height, 16, str);
		mutex_lock(dev->ctrl_hdl_user_gen.lock);
		snprintf(str, sizeof(str), " int32 %d, ro_int32 %d, int64 %lld, bitmask %08x ",
			 dev->int32->cur.val,
			 dev->ro_int32->cur.val,
			 *dev->int64->p_cur.p_s64,
			 dev->bitmask->cur.val);
		tpg_gen_text(tpg, basep, line++ * line_height, 16, str);
		snprintf(str, sizeof(str), " boolean %d, menu %s, string \"%s\" ",
			dev->boolean->cur.val,
			dev->menu->qmenu[dev->menu->cur.val],
			dev->string->p_cur.p_char);
		tpg_gen_text(tpg, basep, line++ * line_height, 16, str);
		snprintf(str, sizeof(str), " integer_menu %lld, value %d ",
			dev->int_menu->qmenu_int[dev->int_menu->cur.val],
			dev->int_menu->cur.val);
		mutex_unlock(dev->ctrl_hdl_user_gen.lock);
		tpg_gen_text(tpg, basep, line++ * line_height, 16, str);
		if (dev->button_pressed) {
			dev->button_pressed--;
			snprintf(str, sizeof(str), " button pressed!");
			tpg_gen_text(tpg, basep, line++ * line_height, 16, str);
		}
		if (dev->osd[0]) {
			if (vivid_is_hdmi_cap(dev)) {
				snprintf(str, sizeof(str),
					 " OSD \"%s\"", dev->osd);
				tpg_gen_text(tpg, basep, line++ * line_height,
					     16, str);
			}
			if (dev->osd_jiffies &&
			    time_is_before_jiffies(dev->osd_jiffies + 5 * HZ)) {
				dev->osd[0] = 0;
				dev->osd_jiffies = 0;
			}
		}
	}
}

static void vivid_cap_update_frame_period(struct vivid_dev *dev)
{
	u64 f_period;

	f_period = (u64)dev->timeperframe_vid_cap.numerator * 1000000000;
	if (WARN_ON(dev->timeperframe_vid_cap.denominator == 0))
		dev->timeperframe_vid_cap.denominator = 1;
	do_div(f_period, dev->timeperframe_vid_cap.denominator);
	if (dev->field_cap == V4L2_FIELD_ALTERNATE)
		f_period >>= 1;
	/*
	 * If "End of Frame", then offset the exposure time by 0.9
	 * of the frame period.
	 */
	dev->cap_frame_eof_offset = f_period * 9;
	do_div(dev->cap_frame_eof_offset, 10);
	dev->cap_frame_period = f_period;
}

static noinline_for_stack void vivid_thread_vid_cap_tick(struct vivid_dev *dev,
							 int dropped_bufs)
{
	struct vivid_buffer *vid_cap_buf = NULL;
	struct vivid_buffer *vbi_cap_buf = NULL;
	struct vivid_buffer *meta_cap_buf = NULL;
	u64 f_time = 0;

	dprintk(dev, 1, "Video Capture Thread Tick\n");

	while (dropped_bufs-- > 1)
		tpg_update_mv_count(&dev->tpg,
				dev->field_cap == V4L2_FIELD_NONE ||
				dev->field_cap == V4L2_FIELD_ALTERNATE);

	/* Drop a certain percentage of buffers. */
	if (dev->perc_dropped_buffers &&
	    get_random_u32_below(100) < dev->perc_dropped_buffers)
		goto update_mv;

	spin_lock(&dev->slock);
	if (!list_empty(&dev->vid_cap_active)) {
		vid_cap_buf = list_entry(dev->vid_cap_active.next, struct vivid_buffer, list);
		list_del(&vid_cap_buf->list);
	}
	if (!list_empty(&dev->vbi_cap_active)) {
		if (dev->field_cap != V4L2_FIELD_ALTERNATE ||
		    (dev->vbi_cap_seq_count & 1)) {
			vbi_cap_buf = list_entry(dev->vbi_cap_active.next,
						 struct vivid_buffer, list);
			list_del(&vbi_cap_buf->list);
		}
	}
	if (!list_empty(&dev->meta_cap_active)) {
		meta_cap_buf = list_entry(dev->meta_cap_active.next,
					  struct vivid_buffer, list);
		list_del(&meta_cap_buf->list);
	}

	spin_unlock(&dev->slock);

	if (!vid_cap_buf && !vbi_cap_buf && !meta_cap_buf)
		goto update_mv;

	f_time = ktime_get_ns() + dev->time_wrap_offset;

	if (vid_cap_buf) {
		v4l2_ctrl_request_setup(vid_cap_buf->vb.vb2_buf.req_obj.req,
					&dev->ctrl_hdl_vid_cap);
		/* Fill buffer */
		vivid_fillbuff(dev, vid_cap_buf);
		dprintk(dev, 1, "filled buffer %d\n",
			vid_cap_buf->vb.vb2_buf.index);

		v4l2_ctrl_request_complete(vid_cap_buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_vid_cap);
		vb2_buffer_done(&vid_cap_buf->vb.vb2_buf, dev->dqbuf_error ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
		dprintk(dev, 2, "vid_cap buffer %d done\n",
				vid_cap_buf->vb.vb2_buf.index);

		vid_cap_buf->vb.vb2_buf.timestamp = f_time;
		if (!dev->tstamp_src_is_soe)
			vid_cap_buf->vb.vb2_buf.timestamp += dev->cap_frame_eof_offset;
	}

	if (vbi_cap_buf) {
		u64 vbi_period;

		v4l2_ctrl_request_setup(vbi_cap_buf->vb.vb2_buf.req_obj.req,
					&dev->ctrl_hdl_vbi_cap);
		if (vbi_cap_buf->vb.vb2_buf.type == V4L2_BUF_TYPE_SLICED_VBI_CAPTURE)
			vivid_sliced_vbi_cap_process(dev, vbi_cap_buf);
		else
			vivid_raw_vbi_cap_process(dev, vbi_cap_buf);
		v4l2_ctrl_request_complete(vbi_cap_buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_vbi_cap);
		vb2_buffer_done(&vbi_cap_buf->vb.vb2_buf, dev->dqbuf_error ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
		dprintk(dev, 2, "vbi_cap %d done\n",
				vbi_cap_buf->vb.vb2_buf.index);

		/* If capturing a VBI, offset by 0.05 */
		vbi_period = dev->cap_frame_period * 5;
		do_div(vbi_period, 100);
		vbi_cap_buf->vb.vb2_buf.timestamp = f_time + dev->cap_frame_eof_offset + vbi_period;
	}

	if (meta_cap_buf) {
		v4l2_ctrl_request_setup(meta_cap_buf->vb.vb2_buf.req_obj.req,
					&dev->ctrl_hdl_meta_cap);
		vivid_meta_cap_fillbuff(dev, meta_cap_buf, f_time);
		v4l2_ctrl_request_complete(meta_cap_buf->vb.vb2_buf.req_obj.req,
					   &dev->ctrl_hdl_meta_cap);
		vb2_buffer_done(&meta_cap_buf->vb.vb2_buf, dev->dqbuf_error ?
				VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);
		dprintk(dev, 2, "meta_cap %d done\n",
			meta_cap_buf->vb.vb2_buf.index);
		meta_cap_buf->vb.vb2_buf.timestamp = f_time + dev->cap_frame_eof_offset;
	}

	dev->dqbuf_error = false;

update_mv:
	/* Update the test pattern movement counters */
	tpg_update_mv_count(&dev->tpg, dev->field_cap == V4L2_FIELD_NONE ||
				       dev->field_cap == V4L2_FIELD_ALTERNATE);
}

static int vivid_thread_vid_cap(void *data)
{
	struct vivid_dev *dev = data;
	u64 numerators_since_start;
	u64 buffers_since_start;
	u64 next_jiffies_since_start;
	unsigned long jiffies_since_start;
	unsigned long cur_jiffies;
	unsigned wait_jiffies;
	unsigned numerator;
	unsigned denominator;
	int dropped_bufs;

	dprintk(dev, 1, "Video Capture Thread Start\n");

	set_freezable();

	/* Resets frame counters */
	dev->cap_seq_offset = 0;
	dev->cap_seq_count = 0;
	dev->cap_seq_resync = false;
	dev->jiffies_vid_cap = jiffies;
	dev->cap_stream_start = ktime_get_ns();
	if (dev->time_wrap)
		dev->time_wrap_offset = dev->time_wrap - dev->cap_stream_start;
	else
		dev->time_wrap_offset = 0;
	vivid_cap_update_frame_period(dev);

	for (;;) {
		try_to_freeze();
		if (kthread_should_stop())
			break;

		if (!mutex_trylock(&dev->mutex)) {
			schedule();
			continue;
		}

		cur_jiffies = jiffies;
		if (dev->cap_seq_resync) {
			dev->jiffies_vid_cap = cur_jiffies;
			dev->cap_seq_offset = dev->cap_seq_count + 1;
			dev->cap_seq_count = 0;
			dev->cap_stream_start += dev->cap_frame_period *
						 dev->cap_seq_offset;
			vivid_cap_update_frame_period(dev);
			dev->cap_seq_resync = false;
		}
		numerator = dev->timeperframe_vid_cap.numerator;
		denominator = dev->timeperframe_vid_cap.denominator;

		if (dev->field_cap == V4L2_FIELD_ALTERNATE)
			denominator *= 2;

		/* Calculate the number of jiffies since we started streaming */
		jiffies_since_start = cur_jiffies - dev->jiffies_vid_cap;
		/* Get the number of buffers streamed since the start */
		buffers_since_start = (u64)jiffies_since_start * denominator +
				      (HZ * numerator) / 2;
		do_div(buffers_since_start, HZ * numerator);

		/*
		 * After more than 0xf0000000 (rounded down to a multiple of
		 * 'jiffies-per-day' to ease jiffies_to_msecs calculation)
		 * jiffies have passed since we started streaming reset the
		 * counters and keep track of the sequence offset.
		 */
		if (jiffies_since_start > JIFFIES_RESYNC) {
			dev->jiffies_vid_cap = cur_jiffies;
			dev->cap_seq_offset = buffers_since_start;
			buffers_since_start = 0;
		}
		dropped_bufs = buffers_since_start + dev->cap_seq_offset - dev->cap_seq_count;
		dev->cap_seq_count = buffers_since_start + dev->cap_seq_offset;
		dev->vid_cap_seq_count = dev->cap_seq_count - dev->vid_cap_seq_start;
		dev->vbi_cap_seq_count = dev->cap_seq_count - dev->vbi_cap_seq_start;
		dev->meta_cap_seq_count = dev->cap_seq_count - dev->meta_cap_seq_start;

		vivid_thread_vid_cap_tick(dev, dropped_bufs);

		/*
		 * Calculate the number of 'numerators' streamed since we started,
		 * including the current buffer.
		 */
		numerators_since_start = ++buffers_since_start * numerator;

		/* And the number of jiffies since we started */
		jiffies_since_start = jiffies - dev->jiffies_vid_cap;

		mutex_unlock(&dev->mutex);

		/*
		 * Calculate when that next buffer is supposed to start
		 * in jiffies since we started streaming.
		 */
		next_jiffies_since_start = numerators_since_start * HZ +
					   denominator / 2;
		do_div(next_jiffies_since_start, denominator);
		/* If it is in the past, then just schedule asap */
		if (next_jiffies_since_start < jiffies_since_start)
			next_jiffies_since_start = jiffies_since_start;

		wait_jiffies = next_jiffies_since_start - jiffies_since_start;
		while (time_is_after_jiffies(cur_jiffies + wait_jiffies) &&
		       !kthread_should_stop())
			schedule();
	}
	dprintk(dev, 1, "Video Capture Thread End\n");
	return 0;
}

static void vivid_grab_controls(struct vivid_dev *dev, bool grab)
{
	v4l2_ctrl_grab(dev->ctrl_has_crop_cap, grab);
	v4l2_ctrl_grab(dev->ctrl_has_compose_cap, grab);
	v4l2_ctrl_grab(dev->ctrl_has_scaler_cap, grab);
}

int vivid_start_generating_vid_cap(struct vivid_dev *dev, bool *pstreaming)
{
	dprintk(dev, 1, "%s\n", __func__);

	if (dev->kthread_vid_cap) {
		u32 seq_count = dev->cap_seq_count + dev->seq_wrap * 128;

		if (pstreaming == &dev->vid_cap_streaming)
			dev->vid_cap_seq_start = seq_count;
		else if (pstreaming == &dev->vbi_cap_streaming)
			dev->vbi_cap_seq_start = seq_count;
		else
			dev->meta_cap_seq_start = seq_count;
		*pstreaming = true;
		return 0;
	}

	/* Resets frame counters */
	tpg_init_mv_count(&dev->tpg);

	dev->vid_cap_seq_start = dev->seq_wrap * 128;
	dev->vbi_cap_seq_start = dev->seq_wrap * 128;
	dev->meta_cap_seq_start = dev->seq_wrap * 128;

	dev->kthread_vid_cap = kthread_run(vivid_thread_vid_cap, dev,
			"%s-vid-cap", dev->v4l2_dev.name);

	if (IS_ERR(dev->kthread_vid_cap)) {
		int err = PTR_ERR(dev->kthread_vid_cap);

		dev->kthread_vid_cap = NULL;
		v4l2_err(&dev->v4l2_dev, "kernel_thread() failed\n");
		return err;
	}
	*pstreaming = true;
	vivid_grab_controls(dev, true);

	dprintk(dev, 1, "returning from %s\n", __func__);
	return 0;
}

void vivid_stop_generating_vid_cap(struct vivid_dev *dev, bool *pstreaming)
{
	dprintk(dev, 1, "%s\n", __func__);

	if (dev->kthread_vid_cap == NULL)
		return;

	*pstreaming = false;
	if (pstreaming == &dev->vid_cap_streaming) {
		/* Release all active buffers */
		while (!list_empty(&dev->vid_cap_active)) {
			struct vivid_buffer *buf;

			buf = list_entry(dev->vid_cap_active.next,
					 struct vivid_buffer, list);
			list_del(&buf->list);
			v4l2_ctrl_request_complete(buf->vb.vb2_buf.req_obj.req,
						   &dev->ctrl_hdl_vid_cap);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			dprintk(dev, 2, "vid_cap buffer %d done\n",
				buf->vb.vb2_buf.index);
		}
	}

	if (pstreaming == &dev->vbi_cap_streaming) {
		while (!list_empty(&dev->vbi_cap_active)) {
			struct vivid_buffer *buf;

			buf = list_entry(dev->vbi_cap_active.next,
					 struct vivid_buffer, list);
			list_del(&buf->list);
			v4l2_ctrl_request_complete(buf->vb.vb2_buf.req_obj.req,
						   &dev->ctrl_hdl_vbi_cap);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			dprintk(dev, 2, "vbi_cap buffer %d done\n",
				buf->vb.vb2_buf.index);
		}
	}

	if (pstreaming == &dev->meta_cap_streaming) {
		while (!list_empty(&dev->meta_cap_active)) {
			struct vivid_buffer *buf;

			buf = list_entry(dev->meta_cap_active.next,
					 struct vivid_buffer, list);
			list_del(&buf->list);
			v4l2_ctrl_request_complete(buf->vb.vb2_buf.req_obj.req,
						   &dev->ctrl_hdl_meta_cap);
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
			dprintk(dev, 2, "meta_cap buffer %d done\n",
				buf->vb.vb2_buf.index);
		}
	}

	if (dev->vid_cap_streaming || dev->vbi_cap_streaming ||
	    dev->meta_cap_streaming)
		return;

	/* shutdown control thread */
	vivid_grab_controls(dev, false);
	kthread_stop(dev->kthread_vid_cap);
	dev->kthread_vid_cap = NULL;
}
