// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010-2013 Bluecherry, LLC <https://www.bluecherrydvr.com>
 *
 * Original author:
 * Ben Collins <bcollins@ubuntu.com>
 *
 * Additional work by:
 * John Brooks <john.brooks@bluecherry.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-dma-sg.h>

#include "solo6x10.h"
#include "solo6x10-tw28.h"
#include "solo6x10-jpeg.h"

#define MIN_VID_BUFFERS		2
#define FRAME_BUF_SIZE		(400 * 1024)
#define MP4_QS			16
#define DMA_ALIGN		4096

/* 6010 M4V */
static u8 vop_6010_ntsc_d1[] = {
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20,
	0x02, 0x48, 0x1d, 0xc0, 0x00, 0x40, 0x00, 0x40,
	0x00, 0x40, 0x00, 0x80, 0x00, 0x97, 0x53, 0x04,
	0x1f, 0x4c, 0x58, 0x10, 0xf0, 0x71, 0x18, 0x3f,
};

static u8 vop_6010_ntsc_cif[] = {
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20,
	0x02, 0x48, 0x1d, 0xc0, 0x00, 0x40, 0x00, 0x40,
	0x00, 0x40, 0x00, 0x80, 0x00, 0x97, 0x53, 0x04,
	0x1f, 0x4c, 0x2c, 0x10, 0x78, 0x51, 0x18, 0x3f,
};

static u8 vop_6010_pal_d1[] = {
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20,
	0x02, 0x48, 0x15, 0xc0, 0x00, 0x40, 0x00, 0x40,
	0x00, 0x40, 0x00, 0x80, 0x00, 0x97, 0x53, 0x04,
	0x1f, 0x4c, 0x58, 0x11, 0x20, 0x71, 0x18, 0x3f,
};

static u8 vop_6010_pal_cif[] = {
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20,
	0x02, 0x48, 0x15, 0xc0, 0x00, 0x40, 0x00, 0x40,
	0x00, 0x40, 0x00, 0x80, 0x00, 0x97, 0x53, 0x04,
	0x1f, 0x4c, 0x2c, 0x10, 0x90, 0x51, 0x18, 0x3f,
};

/* 6110 h.264 */
static u8 vop_6110_ntsc_d1[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
	0x9a, 0x74, 0x05, 0x81, 0xec, 0x80, 0x00, 0x00,
	0x00, 0x01, 0x68, 0xce, 0x32, 0x28, 0x00, 0x00,
};

static u8 vop_6110_ntsc_cif[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
	0x9a, 0x74, 0x0b, 0x0f, 0xc8, 0x00, 0x00, 0x00,
	0x01, 0x68, 0xce, 0x32, 0x28, 0x00, 0x00, 0x00,
};

static u8 vop_6110_pal_d1[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
	0x9a, 0x74, 0x05, 0x80, 0x93, 0x20, 0x00, 0x00,
	0x00, 0x01, 0x68, 0xce, 0x32, 0x28, 0x00, 0x00,
};

static u8 vop_6110_pal_cif[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
	0x9a, 0x74, 0x0b, 0x04, 0xb2, 0x00, 0x00, 0x00,
	0x01, 0x68, 0xce, 0x32, 0x28, 0x00, 0x00, 0x00,
};

typedef __le32 vop_header[16];

struct solo_enc_buf {
	enum solo_enc_types	type;
	const vop_header	*vh;
	int			motion;
};

static int solo_is_motion_on(struct solo_enc_dev *solo_enc)
{
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	return (solo_dev->motion_mask >> solo_enc->ch) & 1;
}

static int solo_motion_detected(struct solo_enc_dev *solo_enc)
{
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	unsigned long flags;
	u32 ch_mask = 1 << solo_enc->ch;
	int ret = 0;

	spin_lock_irqsave(&solo_enc->motion_lock, flags);
	if (solo_reg_read(solo_dev, SOLO_VI_MOT_STATUS) & ch_mask) {
		solo_reg_write(solo_dev, SOLO_VI_MOT_CLEAR, ch_mask);
		ret = 1;
	}
	spin_unlock_irqrestore(&solo_enc->motion_lock, flags);

	return ret;
}

static void solo_motion_toggle(struct solo_enc_dev *solo_enc, int on)
{
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	u32 mask = 1 << solo_enc->ch;
	unsigned long flags;

	spin_lock_irqsave(&solo_enc->motion_lock, flags);

	if (on)
		solo_dev->motion_mask |= mask;
	else
		solo_dev->motion_mask &= ~mask;

	solo_reg_write(solo_dev, SOLO_VI_MOT_CLEAR, mask);

	solo_reg_write(solo_dev, SOLO_VI_MOT_ADR,
		       SOLO_VI_MOTION_EN(solo_dev->motion_mask) |
		       (SOLO_MOTION_EXT_ADDR(solo_dev) >> 16));

	spin_unlock_irqrestore(&solo_enc->motion_lock, flags);
}

void solo_update_mode(struct solo_enc_dev *solo_enc)
{
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	int vop_len;
	u8 *vop;

	solo_enc->interlaced = (solo_enc->mode & 0x08) ? 1 : 0;
	solo_enc->bw_weight = max(solo_dev->fps / solo_enc->interval, 1);

	if (solo_enc->mode == SOLO_ENC_MODE_CIF) {
		solo_enc->width = solo_dev->video_hsize >> 1;
		solo_enc->height = solo_dev->video_vsize;
		if (solo_dev->type == SOLO_DEV_6110) {
			if (solo_dev->video_type == SOLO_VO_FMT_TYPE_NTSC) {
				vop = vop_6110_ntsc_cif;
				vop_len = sizeof(vop_6110_ntsc_cif);
			} else {
				vop = vop_6110_pal_cif;
				vop_len = sizeof(vop_6110_pal_cif);
			}
		} else {
			if (solo_dev->video_type == SOLO_VO_FMT_TYPE_NTSC) {
				vop = vop_6010_ntsc_cif;
				vop_len = sizeof(vop_6010_ntsc_cif);
			} else {
				vop = vop_6010_pal_cif;
				vop_len = sizeof(vop_6010_pal_cif);
			}
		}
	} else {
		solo_enc->width = solo_dev->video_hsize;
		solo_enc->height = solo_dev->video_vsize << 1;
		solo_enc->bw_weight <<= 2;
		if (solo_dev->type == SOLO_DEV_6110) {
			if (solo_dev->video_type == SOLO_VO_FMT_TYPE_NTSC) {
				vop = vop_6110_ntsc_d1;
				vop_len = sizeof(vop_6110_ntsc_d1);
			} else {
				vop = vop_6110_pal_d1;
				vop_len = sizeof(vop_6110_pal_d1);
			}
		} else {
			if (solo_dev->video_type == SOLO_VO_FMT_TYPE_NTSC) {
				vop = vop_6010_ntsc_d1;
				vop_len = sizeof(vop_6010_ntsc_d1);
			} else {
				vop = vop_6010_pal_d1;
				vop_len = sizeof(vop_6010_pal_d1);
			}
		}
	}

	memcpy(solo_enc->vop, vop, vop_len);

	/* Some fixups for 6010/M4V */
	if (solo_dev->type == SOLO_DEV_6010) {
		u16 fps = solo_dev->fps * 1000;
		u16 interval = solo_enc->interval * 1000;

		vop = solo_enc->vop;

		/* Frame rate and interval */
		vop[22] = fps >> 4;
		vop[23] = ((fps << 4) & 0xf0) | 0x0c
			| ((interval >> 13) & 0x3);
		vop[24] = (interval >> 5) & 0xff;
		vop[25] = ((interval << 3) & 0xf8) | 0x04;
	}

	solo_enc->vop_len = vop_len;

	/* Now handle the jpeg header */
	vop = solo_enc->jpeg_header;
	vop[SOF0_START + 5] = 0xff & (solo_enc->height >> 8);
	vop[SOF0_START + 6] = 0xff & solo_enc->height;
	vop[SOF0_START + 7] = 0xff & (solo_enc->width >> 8);
	vop[SOF0_START + 8] = 0xff & solo_enc->width;

	memcpy(vop + DQT_START,
	       jpeg_dqt[solo_g_jpeg_qp(solo_dev, solo_enc->ch)], DQT_LEN);
}

static int solo_enc_on(struct solo_enc_dev *solo_enc)
{
	u8 ch = solo_enc->ch;
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	u8 interval;

	solo_update_mode(solo_enc);

	/* Make sure to do a bandwidth check */
	if (solo_enc->bw_weight > solo_dev->enc_bw_remain)
		return -EBUSY;
	solo_enc->sequence = 0;
	solo_dev->enc_bw_remain -= solo_enc->bw_weight;

	if (solo_enc->type == SOLO_ENC_TYPE_EXT)
		solo_reg_write(solo_dev, SOLO_CAP_CH_COMP_ENA_E(ch), 1);

	/* Disable all encoding for this channel */
	solo_reg_write(solo_dev, SOLO_CAP_CH_SCALE(ch), 0);

	/* Common for both std and ext encoding */
	solo_reg_write(solo_dev, SOLO_VE_CH_INTL(ch),
		       solo_enc->interlaced ? 1 : 0);

	if (solo_enc->interlaced)
		interval = solo_enc->interval - 1;
	else
		interval = solo_enc->interval;

	/* Standard encoding only */
	solo_reg_write(solo_dev, SOLO_VE_CH_GOP(ch), solo_enc->gop);
	solo_reg_write(solo_dev, SOLO_VE_CH_QP(ch), solo_enc->qp);
	solo_reg_write(solo_dev, SOLO_CAP_CH_INTV(ch), interval);

	/* Extended encoding only */
	solo_reg_write(solo_dev, SOLO_VE_CH_GOP_E(ch), solo_enc->gop);
	solo_reg_write(solo_dev, SOLO_VE_CH_QP_E(ch), solo_enc->qp);
	solo_reg_write(solo_dev, SOLO_CAP_CH_INTV_E(ch), interval);

	/* Enables the standard encoder */
	solo_reg_write(solo_dev, SOLO_CAP_CH_SCALE(ch), solo_enc->mode);

	return 0;
}

static void solo_enc_off(struct solo_enc_dev *solo_enc)
{
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	solo_dev->enc_bw_remain += solo_enc->bw_weight;

	solo_reg_write(solo_dev, SOLO_CAP_CH_SCALE(solo_enc->ch), 0);
	solo_reg_write(solo_dev, SOLO_CAP_CH_COMP_ENA_E(solo_enc->ch), 0);
}

static int enc_get_mpeg_dma(struct solo_dev *solo_dev, dma_addr_t dma,
			      unsigned int off, unsigned int size)
{
	int ret;

	if (off > SOLO_MP4E_EXT_SIZE(solo_dev))
		return -EINVAL;

	/* Single shot */
	if (off + size <= SOLO_MP4E_EXT_SIZE(solo_dev)) {
		return solo_p2m_dma_t(solo_dev, 0, dma,
				      SOLO_MP4E_EXT_ADDR(solo_dev) + off, size,
				      0, 0);
	}

	/* Buffer wrap */
	ret = solo_p2m_dma_t(solo_dev, 0, dma,
			     SOLO_MP4E_EXT_ADDR(solo_dev) + off,
			     SOLO_MP4E_EXT_SIZE(solo_dev) - off, 0, 0);

	if (!ret) {
		ret = solo_p2m_dma_t(solo_dev, 0,
			     dma + SOLO_MP4E_EXT_SIZE(solo_dev) - off,
			     SOLO_MP4E_EXT_ADDR(solo_dev),
			     size + off - SOLO_MP4E_EXT_SIZE(solo_dev), 0, 0);
	}

	return ret;
}

/* Build a descriptor queue out of an SG list and send it to the P2M for
 * processing. */
static int solo_send_desc(struct solo_enc_dev *solo_enc, int skip,
			  struct sg_table *vbuf, int off, int size,
			  unsigned int base, unsigned int base_size)
{
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct scatterlist *sg;
	int i;
	int ret;

	if (WARN_ON_ONCE(size > FRAME_BUF_SIZE))
		return -EINVAL;

	solo_enc->desc_count = 1;

	for_each_sg(vbuf->sgl, sg, vbuf->nents, i) {
		struct solo_p2m_desc *desc;
		dma_addr_t dma;
		int len;
		int left = base_size - off;

		desc = &solo_enc->desc_items[solo_enc->desc_count++];
		dma = sg_dma_address(sg);
		len = sg_dma_len(sg);

		/* We assume this is smaller than the scatter size */
		BUG_ON(skip >= len);
		if (skip) {
			len -= skip;
			dma += skip;
			size -= skip;
			skip = 0;
		}

		len = min(len, size);

		if (len <= left) {
			/* Single descriptor */
			solo_p2m_fill_desc(desc, 0, dma, base + off,
					   len, 0, 0);
		} else {
			/* Buffer wrap */
			/* XXX: Do these as separate DMA requests, to avoid
			   timeout errors triggered by awkwardly sized
			   descriptors. See
			   <https://github.com/bluecherrydvr/solo6x10/issues/8>
			 */
			ret = solo_p2m_dma_t(solo_dev, 0, dma, base + off,
					     left, 0, 0);
			if (ret)
				return ret;

			ret = solo_p2m_dma_t(solo_dev, 0, dma + left, base,
					     len - left, 0, 0);
			if (ret)
				return ret;

			solo_enc->desc_count--;
		}

		size -= len;
		if (size <= 0)
			break;

		off += len;
		if (off >= base_size)
			off -= base_size;

		/* Because we may use two descriptors per loop */
		if (solo_enc->desc_count >= (solo_enc->desc_nelts - 1)) {
			ret = solo_p2m_dma_desc(solo_dev, solo_enc->desc_items,
						solo_enc->desc_dma,
						solo_enc->desc_count - 1);
			if (ret)
				return ret;
			solo_enc->desc_count = 1;
		}
	}

	if (solo_enc->desc_count <= 1)
		return 0;

	return solo_p2m_dma_desc(solo_dev, solo_enc->desc_items,
			solo_enc->desc_dma, solo_enc->desc_count - 1);
}

/* Extract values from VOP header - VE_STATUSxx */
static inline int vop_interlaced(const vop_header *vh)
{
	return (__le32_to_cpu((*vh)[0]) >> 30) & 1;
}

static inline u8 vop_channel(const vop_header *vh)
{
	return (__le32_to_cpu((*vh)[0]) >> 24) & 0x1F;
}

static inline u8 vop_type(const vop_header *vh)
{
	return (__le32_to_cpu((*vh)[0]) >> 22) & 3;
}

static inline u32 vop_mpeg_size(const vop_header *vh)
{
	return __le32_to_cpu((*vh)[0]) & 0xFFFFF;
}

static inline u8 vop_hsize(const vop_header *vh)
{
	return (__le32_to_cpu((*vh)[1]) >> 8) & 0xFF;
}

static inline u8 vop_vsize(const vop_header *vh)
{
	return __le32_to_cpu((*vh)[1]) & 0xFF;
}

static inline u32 vop_mpeg_offset(const vop_header *vh)
{
	return __le32_to_cpu((*vh)[2]);
}

static inline u32 vop_jpeg_offset(const vop_header *vh)
{
	return __le32_to_cpu((*vh)[3]);
}

static inline u32 vop_jpeg_size(const vop_header *vh)
{
	return __le32_to_cpu((*vh)[4]) & 0xFFFFF;
}

static inline u32 vop_sec(const vop_header *vh)
{
	return __le32_to_cpu((*vh)[5]);
}

static inline u32 vop_usec(const vop_header *vh)
{
	return __le32_to_cpu((*vh)[6]);
}

static int solo_fill_jpeg(struct solo_enc_dev *solo_enc,
			  struct vb2_buffer *vb, const vop_header *vh)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, 0);
	int frame_size;

	vbuf->flags |= V4L2_BUF_FLAG_KEYFRAME;

	if (vb2_plane_size(vb, 0) < vop_jpeg_size(vh) + solo_enc->jpeg_len)
		return -EIO;

	frame_size = ALIGN(vop_jpeg_size(vh) + solo_enc->jpeg_len, DMA_ALIGN);
	vb2_set_plane_payload(vb, 0, vop_jpeg_size(vh) + solo_enc->jpeg_len);

	return solo_send_desc(solo_enc, solo_enc->jpeg_len, sgt,
			     vop_jpeg_offset(vh) - SOLO_JPEG_EXT_ADDR(solo_dev),
			     frame_size, SOLO_JPEG_EXT_ADDR(solo_dev),
			     SOLO_JPEG_EXT_SIZE(solo_dev));
}

static int solo_fill_mpeg(struct solo_enc_dev *solo_enc,
		struct vb2_buffer *vb, const vop_header *vh)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, 0);
	int frame_off, frame_size;
	int skip = 0;

	if (vb2_plane_size(vb, 0) < vop_mpeg_size(vh))
		return -EIO;

	/* If this is a key frame, add extra header */
	vbuf->flags &= ~(V4L2_BUF_FLAG_KEYFRAME | V4L2_BUF_FLAG_PFRAME |
		V4L2_BUF_FLAG_BFRAME);
	if (!vop_type(vh)) {
		skip = solo_enc->vop_len;
		vbuf->flags |= V4L2_BUF_FLAG_KEYFRAME;
		vb2_set_plane_payload(vb, 0, vop_mpeg_size(vh) +
			solo_enc->vop_len);
	} else {
		vbuf->flags |= V4L2_BUF_FLAG_PFRAME;
		vb2_set_plane_payload(vb, 0, vop_mpeg_size(vh));
	}

	/* Now get the actual mpeg payload */
	frame_off = (vop_mpeg_offset(vh) - SOLO_MP4E_EXT_ADDR(solo_dev) +
		sizeof(*vh)) % SOLO_MP4E_EXT_SIZE(solo_dev);
	frame_size = ALIGN(vop_mpeg_size(vh) + skip, DMA_ALIGN);

	return solo_send_desc(solo_enc, skip, sgt, frame_off, frame_size,
			SOLO_MP4E_EXT_ADDR(solo_dev),
			SOLO_MP4E_EXT_SIZE(solo_dev));
}

static int solo_enc_fillbuf(struct solo_enc_dev *solo_enc,
			    struct vb2_buffer *vb, struct solo_enc_buf *enc_buf)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	const vop_header *vh = enc_buf->vh;
	int ret;

	switch (solo_enc->fmt) {
	case V4L2_PIX_FMT_MPEG4:
	case V4L2_PIX_FMT_H264:
		ret = solo_fill_mpeg(solo_enc, vb, vh);
		break;
	default: /* V4L2_PIX_FMT_MJPEG */
		ret = solo_fill_jpeg(solo_enc, vb, vh);
		break;
	}

	if (!ret) {
		vbuf->sequence = solo_enc->sequence++;
		vb->timestamp = ktime_get_ns();

		/* Check for motion flags */
		if (solo_is_motion_on(solo_enc) && enc_buf->motion) {
			struct v4l2_event ev = {
				.type = V4L2_EVENT_MOTION_DET,
				.u.motion_det = {
					.flags
					= V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ,
					.frame_sequence = vbuf->sequence,
					.region_mask = enc_buf->motion ? 1 : 0,
				},
			};

			v4l2_event_queue(solo_enc->vfd, &ev);
		}
	}

	vb2_buffer_done(vb, ret ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE);

	return ret;
}

static void solo_enc_handle_one(struct solo_enc_dev *solo_enc,
				struct solo_enc_buf *enc_buf)
{
	struct solo_vb2_buf *vb;
	unsigned long flags;

	mutex_lock(&solo_enc->lock);
	if (solo_enc->type != enc_buf->type)
		goto unlock;

	spin_lock_irqsave(&solo_enc->av_lock, flags);
	if (list_empty(&solo_enc->vidq_active)) {
		spin_unlock_irqrestore(&solo_enc->av_lock, flags);
		goto unlock;
	}
	vb = list_first_entry(&solo_enc->vidq_active, struct solo_vb2_buf,
		list);
	list_del(&vb->list);
	spin_unlock_irqrestore(&solo_enc->av_lock, flags);

	solo_enc_fillbuf(solo_enc, &vb->vb.vb2_buf, enc_buf);
unlock:
	mutex_unlock(&solo_enc->lock);
}

void solo_enc_v4l2_isr(struct solo_dev *solo_dev)
{
	wake_up_interruptible_all(&solo_dev->ring_thread_wait);
}

static void solo_handle_ring(struct solo_dev *solo_dev)
{
	for (;;) {
		struct solo_enc_dev *solo_enc;
		struct solo_enc_buf enc_buf;
		u32 mpeg_current, off;
		u8 ch;
		u8 cur_q;

		/* Check if the hardware has any new ones in the queue */
		cur_q = solo_reg_read(solo_dev, SOLO_VE_STATE(11)) & 0xff;
		if (cur_q == solo_dev->enc_idx)
			break;

		mpeg_current = solo_reg_read(solo_dev,
					SOLO_VE_MPEG4_QUE(solo_dev->enc_idx));
		solo_dev->enc_idx = (solo_dev->enc_idx + 1) % MP4_QS;

		ch = (mpeg_current >> 24) & 0x1f;
		off = mpeg_current & 0x00ffffff;

		if (ch >= SOLO_MAX_CHANNELS) {
			ch -= SOLO_MAX_CHANNELS;
			enc_buf.type = SOLO_ENC_TYPE_EXT;
		} else
			enc_buf.type = SOLO_ENC_TYPE_STD;

		solo_enc = solo_dev->v4l2_enc[ch];
		if (solo_enc == NULL) {
			dev_err(&solo_dev->pdev->dev,
				"Got spurious packet for channel %d\n", ch);
			continue;
		}

		/* FAIL... */
		if (enc_get_mpeg_dma(solo_dev, solo_dev->vh_dma, off,
				     sizeof(vop_header)))
			continue;

		enc_buf.vh = solo_dev->vh_buf;

		/* Sanity check */
		if (vop_mpeg_offset(enc_buf.vh) !=
			SOLO_MP4E_EXT_ADDR(solo_dev) + off)
			continue;

		if (solo_motion_detected(solo_enc))
			enc_buf.motion = 1;
		else
			enc_buf.motion = 0;

		solo_enc_handle_one(solo_enc, &enc_buf);
	}
}

static int solo_ring_thread(void *data)
{
	struct solo_dev *solo_dev = data;
	DECLARE_WAITQUEUE(wait, current);

	set_freezable();
	add_wait_queue(&solo_dev->ring_thread_wait, &wait);

	for (;;) {
		long timeout = schedule_timeout_interruptible(HZ);

		if (timeout == -ERESTARTSYS || kthread_should_stop())
			break;
		solo_handle_ring(solo_dev);
		try_to_freeze();
	}

	remove_wait_queue(&solo_dev->ring_thread_wait, &wait);

	return 0;
}

static int solo_enc_queue_setup(struct vb2_queue *q,
				unsigned int *num_buffers,
				unsigned int *num_planes, unsigned int sizes[],
				struct device *alloc_devs[])
{
	sizes[0] = FRAME_BUF_SIZE;
	*num_planes = 1;

	if (*num_buffers < MIN_VID_BUFFERS)
		*num_buffers = MIN_VID_BUFFERS;

	return 0;
}

static void solo_enc_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct solo_enc_dev *solo_enc = vb2_get_drv_priv(vq);
	struct solo_vb2_buf *solo_vb =
		container_of(vbuf, struct solo_vb2_buf, vb);

	spin_lock(&solo_enc->av_lock);
	list_add_tail(&solo_vb->list, &solo_enc->vidq_active);
	spin_unlock(&solo_enc->av_lock);
}

static int solo_ring_start(struct solo_dev *solo_dev)
{
	solo_dev->ring_thread = kthread_run(solo_ring_thread, solo_dev,
					    SOLO6X10_NAME "_ring");
	if (IS_ERR(solo_dev->ring_thread)) {
		int err = PTR_ERR(solo_dev->ring_thread);

		solo_dev->ring_thread = NULL;
		return err;
	}

	solo_irq_on(solo_dev, SOLO_IRQ_ENCODER);

	return 0;
}

static void solo_ring_stop(struct solo_dev *solo_dev)
{
	if (solo_dev->ring_thread) {
		kthread_stop(solo_dev->ring_thread);
		solo_dev->ring_thread = NULL;
	}

	solo_irq_off(solo_dev, SOLO_IRQ_ENCODER);
}

static int solo_enc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct solo_enc_dev *solo_enc = vb2_get_drv_priv(q);

	return solo_enc_on(solo_enc);
}

static void solo_enc_stop_streaming(struct vb2_queue *q)
{
	struct solo_enc_dev *solo_enc = vb2_get_drv_priv(q);
	unsigned long flags;

	spin_lock_irqsave(&solo_enc->av_lock, flags);
	solo_enc_off(solo_enc);
	while (!list_empty(&solo_enc->vidq_active)) {
		struct solo_vb2_buf *buf = list_entry(
				solo_enc->vidq_active.next,
				struct solo_vb2_buf, list);

		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&solo_enc->av_lock, flags);
}

static void solo_enc_buf_finish(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct solo_enc_dev *solo_enc = vb2_get_drv_priv(vb->vb2_queue);
	struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, 0);

	switch (solo_enc->fmt) {
	case V4L2_PIX_FMT_MPEG4:
	case V4L2_PIX_FMT_H264:
		if (vbuf->flags & V4L2_BUF_FLAG_KEYFRAME)
			sg_copy_from_buffer(sgt->sgl, sgt->nents,
					solo_enc->vop, solo_enc->vop_len);
		break;
	default: /* V4L2_PIX_FMT_MJPEG */
		sg_copy_from_buffer(sgt->sgl, sgt->nents,
				solo_enc->jpeg_header, solo_enc->jpeg_len);
		break;
	}
}

static const struct vb2_ops solo_enc_video_qops = {
	.queue_setup	= solo_enc_queue_setup,
	.buf_queue	= solo_enc_buf_queue,
	.buf_finish	= solo_enc_buf_finish,
	.start_streaming = solo_enc_start_streaming,
	.stop_streaming = solo_enc_stop_streaming,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
};

static int solo_enc_querycap(struct file *file, void  *priv,
			     struct v4l2_capability *cap)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	strscpy(cap->driver, SOLO6X10_NAME, sizeof(cap->driver));
	snprintf(cap->card, sizeof(cap->card), "Softlogic 6x10 Enc %d",
		 solo_enc->ch);
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s",
		 pci_name(solo_dev->pdev));
	return 0;
}

static int solo_enc_enum_input(struct file *file, void *priv,
			       struct v4l2_input *input)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	if (input->index)
		return -EINVAL;

	snprintf(input->name, sizeof(input->name), "Encoder %d",
		 solo_enc->ch + 1);
	input->type = V4L2_INPUT_TYPE_CAMERA;
	input->std = solo_enc->vfd->tvnorms;

	if (!tw28_get_video_status(solo_dev, solo_enc->ch))
		input->status = V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static int solo_enc_set_input(struct file *file, void *priv,
			      unsigned int index)
{
	if (index)
		return -EINVAL;

	return 0;
}

static int solo_enc_get_input(struct file *file, void *priv,
			      unsigned int *index)
{
	*index = 0;

	return 0;
}

static int solo_enc_enum_fmt_cap(struct file *file, void *priv,
				 struct v4l2_fmtdesc *f)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	int dev_type = solo_enc->solo_dev->type;

	switch (f->index) {
	case 0:
		switch (dev_type) {
		case SOLO_DEV_6010:
			f->pixelformat = V4L2_PIX_FMT_MPEG4;
			break;
		case SOLO_DEV_6110:
			f->pixelformat = V4L2_PIX_FMT_H264;
			break;
		}
		break;
	case 1:
		f->pixelformat = V4L2_PIX_FMT_MJPEG;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static inline int solo_valid_pixfmt(u32 pixfmt, int dev_type)
{
	return (pixfmt == V4L2_PIX_FMT_H264 && dev_type == SOLO_DEV_6110)
		|| (pixfmt == V4L2_PIX_FMT_MPEG4 && dev_type == SOLO_DEV_6010)
		|| pixfmt == V4L2_PIX_FMT_MJPEG ? 0 : -EINVAL;
}

static int solo_enc_try_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (solo_valid_pixfmt(pix->pixelformat, solo_dev->type))
		return -EINVAL;

	if (pix->width < solo_dev->video_hsize ||
	    pix->height < solo_dev->video_vsize << 1) {
		/* Default to CIF 1/2 size */
		pix->width = solo_dev->video_hsize >> 1;
		pix->height = solo_dev->video_vsize;
	} else {
		/* Full frame */
		pix->width = solo_dev->video_hsize;
		pix->height = solo_dev->video_vsize << 1;
	}

	switch (pix->field) {
	case V4L2_FIELD_NONE:
	case V4L2_FIELD_INTERLACED:
		break;
	case V4L2_FIELD_ANY:
	default:
		pix->field = V4L2_FIELD_INTERLACED;
		break;
	}

	/* Just set these */
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pix->sizeimage = FRAME_BUF_SIZE;
	pix->bytesperline = 0;

	return 0;
}

static int solo_enc_set_fmt_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int ret;

	if (vb2_is_busy(&solo_enc->vidq))
		return -EBUSY;

	ret = solo_enc_try_fmt_cap(file, priv, f);
	if (ret)
		return ret;

	if (pix->width == solo_dev->video_hsize)
		solo_enc->mode = SOLO_ENC_MODE_D1;
	else
		solo_enc->mode = SOLO_ENC_MODE_CIF;

	/* This does not change the encoder at all */
	solo_enc->fmt = pix->pixelformat;

	/*
	 * More information is needed about these 'extended' types. As far
	 * as I can tell these are basically additional video streams with
	 * different MPEG encoding attributes that can run in parallel with
	 * the main stream. If so, then this should be implemented as a
	 * second video node. Abusing priv like this is certainly not the
	 * right approach.
	if (pix->priv)
		solo_enc->type = SOLO_ENC_TYPE_EXT;
	 */
	solo_update_mode(solo_enc);
	return 0;
}

static int solo_enc_get_fmt_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct v4l2_pix_format *pix = &f->fmt.pix;

	pix->width = solo_enc->width;
	pix->height = solo_enc->height;
	pix->pixelformat = solo_enc->fmt;
	pix->field = solo_enc->interlaced ? V4L2_FIELD_INTERLACED :
		     V4L2_FIELD_NONE;
	pix->sizeimage = FRAME_BUF_SIZE;
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int solo_enc_g_std(struct file *file, void *priv, v4l2_std_id *i)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	if (solo_dev->video_type == SOLO_VO_FMT_TYPE_NTSC)
		*i = V4L2_STD_NTSC_M;
	else
		*i = V4L2_STD_PAL;
	return 0;
}

static int solo_enc_s_std(struct file *file, void *priv, v4l2_std_id std)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);

	return solo_set_video_type(solo_enc->solo_dev, std & V4L2_STD_625_50);
}

static int solo_enum_framesizes(struct file *file, void *priv,
				struct v4l2_frmsizeenum *fsize)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	if (solo_valid_pixfmt(fsize->pixel_format, solo_dev->type))
		return -EINVAL;

	switch (fsize->index) {
	case 0:
		fsize->discrete.width = solo_dev->video_hsize >> 1;
		fsize->discrete.height = solo_dev->video_vsize;
		break;
	case 1:
		fsize->discrete.width = solo_dev->video_hsize;
		fsize->discrete.height = solo_dev->video_vsize << 1;
		break;
	default:
		return -EINVAL;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;

	return 0;
}

static int solo_enum_frameintervals(struct file *file, void *priv,
				    struct v4l2_frmivalenum *fintv)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	if (solo_valid_pixfmt(fintv->pixel_format, solo_dev->type))
		return -EINVAL;
	if (fintv->index)
		return -EINVAL;
	if ((fintv->width != solo_dev->video_hsize >> 1 ||
	     fintv->height != solo_dev->video_vsize) &&
	    (fintv->width != solo_dev->video_hsize ||
	     fintv->height != solo_dev->video_vsize << 1))
		return -EINVAL;

	fintv->type = V4L2_FRMIVAL_TYPE_STEPWISE;

	fintv->stepwise.min.numerator = 1;
	fintv->stepwise.min.denominator = solo_dev->fps;

	fintv->stepwise.max.numerator = 15;
	fintv->stepwise.max.denominator = solo_dev->fps;

	fintv->stepwise.step.numerator = 1;
	fintv->stepwise.step.denominator = solo_dev->fps;

	return 0;
}

static int solo_g_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *sp)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct v4l2_captureparm *cp = &sp->parm.capture;

	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = solo_enc->interval;
	cp->timeperframe.denominator = solo_enc->solo_dev->fps;
	cp->capturemode = 0;
	/* XXX: Shouldn't we be able to get/set this from videobuf? */
	cp->readbuffers = 2;

	return 0;
}

static inline int calc_interval(u8 fps, u32 n, u32 d)
{
	if (!n || !d)
		return 1;
	if (d == fps)
		return n;
	n *= fps;
	return min(15U, n / d + (n % d >= (fps >> 1)));
}

static int solo_s_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *sp)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct v4l2_fract *t = &sp->parm.capture.timeperframe;
	u8 fps = solo_enc->solo_dev->fps;

	if (vb2_is_streaming(&solo_enc->vidq))
		return -EBUSY;

	solo_enc->interval = calc_interval(fps, t->numerator, t->denominator);
	solo_update_mode(solo_enc);
	return solo_g_parm(file, priv, sp);
}

static int solo_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct solo_enc_dev *solo_enc =
		container_of(ctrl->handler, struct solo_enc_dev, hdl);
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	int err;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_SHARPNESS:
		return tw28_set_ctrl_val(solo_dev, ctrl->id, solo_enc->ch,
					 ctrl->val);
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		solo_enc->gop = ctrl->val;
		solo_reg_write(solo_dev, SOLO_VE_CH_GOP(solo_enc->ch), solo_enc->gop);
		solo_reg_write(solo_dev, SOLO_VE_CH_GOP_E(solo_enc->ch), solo_enc->gop);
		return 0;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		solo_enc->qp = ctrl->val;
		solo_reg_write(solo_dev, SOLO_VE_CH_QP(solo_enc->ch), solo_enc->qp);
		solo_reg_write(solo_dev, SOLO_VE_CH_QP_E(solo_enc->ch), solo_enc->qp);
		return 0;
	case V4L2_CID_DETECT_MD_GLOBAL_THRESHOLD:
		solo_enc->motion_thresh = ctrl->val << 8;
		if (!solo_enc->motion_global || !solo_enc->motion_enabled)
			return 0;
		return solo_set_motion_threshold(solo_dev, solo_enc->ch,
				solo_enc->motion_thresh);
	case V4L2_CID_DETECT_MD_MODE:
		solo_enc->motion_global = ctrl->val == V4L2_DETECT_MD_MODE_GLOBAL;
		solo_enc->motion_enabled = ctrl->val > V4L2_DETECT_MD_MODE_DISABLED;
		if (ctrl->val) {
			if (solo_enc->motion_global)
				err = solo_set_motion_threshold(solo_dev, solo_enc->ch,
					solo_enc->motion_thresh);
			else
				err = solo_set_motion_block(solo_dev, solo_enc->ch,
					solo_enc->md_thresholds->p_cur.p_u16);
			if (err)
				return err;
		}
		solo_motion_toggle(solo_enc, ctrl->val);
		return 0;
	case V4L2_CID_DETECT_MD_THRESHOLD_GRID:
		if (solo_enc->motion_enabled && !solo_enc->motion_global)
			return solo_set_motion_block(solo_dev, solo_enc->ch,
					solo_enc->md_thresholds->p_new.p_u16);
		break;
	case V4L2_CID_OSD_TEXT:
		strscpy(solo_enc->osd_text, ctrl->p_new.p_char,
			sizeof(solo_enc->osd_text));
		return solo_osd_print(solo_enc);
	default:
		return -EINVAL;
	}

	return 0;
}

static int solo_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{

	switch (sub->type) {
	case V4L2_EVENT_MOTION_DET:
		/* Allow for up to 30 events (1 second for NTSC) to be
		 * stored. */
		return v4l2_event_subscribe(fh, sub, 30, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static const struct v4l2_file_operations solo_enc_fops = {
	.owner			= THIS_MODULE,
	.open			= v4l2_fh_open,
	.release		= vb2_fop_release,
	.read			= vb2_fop_read,
	.poll			= vb2_fop_poll,
	.mmap			= vb2_fop_mmap,
	.unlocked_ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops solo_enc_ioctl_ops = {
	.vidioc_querycap		= solo_enc_querycap,
	.vidioc_s_std			= solo_enc_s_std,
	.vidioc_g_std			= solo_enc_g_std,
	/* Input callbacks */
	.vidioc_enum_input		= solo_enc_enum_input,
	.vidioc_s_input			= solo_enc_set_input,
	.vidioc_g_input			= solo_enc_get_input,
	/* Video capture format callbacks */
	.vidioc_enum_fmt_vid_cap	= solo_enc_enum_fmt_cap,
	.vidioc_try_fmt_vid_cap		= solo_enc_try_fmt_cap,
	.vidioc_s_fmt_vid_cap		= solo_enc_set_fmt_cap,
	.vidioc_g_fmt_vid_cap		= solo_enc_get_fmt_cap,
	/* Streaming I/O */
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	/* Frame size and interval */
	.vidioc_enum_framesizes		= solo_enum_framesizes,
	.vidioc_enum_frameintervals	= solo_enum_frameintervals,
	/* Video capture parameters */
	.vidioc_s_parm			= solo_s_parm,
	.vidioc_g_parm			= solo_g_parm,
	/* Logging and events */
	.vidioc_log_status		= v4l2_ctrl_log_status,
	.vidioc_subscribe_event		= solo_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static const struct video_device solo_enc_template = {
	.name			= SOLO6X10_NAME,
	.fops			= &solo_enc_fops,
	.ioctl_ops		= &solo_enc_ioctl_ops,
	.minor			= -1,
	.release		= video_device_release,
	.tvnorms		= V4L2_STD_NTSC_M | V4L2_STD_PAL,
	.device_caps		= V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE |
				  V4L2_CAP_STREAMING,
};

static const struct v4l2_ctrl_ops solo_ctrl_ops = {
	.s_ctrl = solo_s_ctrl,
};

static const struct v4l2_ctrl_config solo_osd_text_ctrl = {
	.ops = &solo_ctrl_ops,
	.id = V4L2_CID_OSD_TEXT,
	.name = "OSD Text",
	.type = V4L2_CTRL_TYPE_STRING,
	.max = OSD_TEXT_MAX,
	.step = 1,
};

/* Motion Detection Threshold matrix */
static const struct v4l2_ctrl_config solo_md_thresholds = {
	.ops = &solo_ctrl_ops,
	.id = V4L2_CID_DETECT_MD_THRESHOLD_GRID,
	.dims = { SOLO_MOTION_SZ, SOLO_MOTION_SZ },
	.def = SOLO_DEF_MOT_THRESH,
	.max = 65535,
	.step = 1,
};

static struct solo_enc_dev *solo_enc_alloc(struct solo_dev *solo_dev,
					   u8 ch, unsigned nr)
{
	struct solo_enc_dev *solo_enc;
	struct v4l2_ctrl_handler *hdl;
	int ret;

	solo_enc = kzalloc(sizeof(*solo_enc), GFP_KERNEL);
	if (!solo_enc)
		return ERR_PTR(-ENOMEM);

	hdl = &solo_enc->hdl;
	v4l2_ctrl_handler_init(hdl, 10);
	v4l2_ctrl_new_std(hdl, &solo_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &solo_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &solo_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, 128);
	v4l2_ctrl_new_std(hdl, &solo_ctrl_ops,
			V4L2_CID_HUE, 0, 255, 1, 128);
	if (tw28_has_sharpness(solo_dev, ch))
		v4l2_ctrl_new_std(hdl, &solo_ctrl_ops,
			V4L2_CID_SHARPNESS, 0, 15, 1, 0);
	v4l2_ctrl_new_std(hdl, &solo_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_GOP_SIZE, 1, 255, 1, solo_dev->fps);
	v4l2_ctrl_new_std(hdl, &solo_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_H264_MIN_QP, 0, 31, 1, SOLO_DEFAULT_QP);
	v4l2_ctrl_new_std_menu(hdl, &solo_ctrl_ops,
			V4L2_CID_DETECT_MD_MODE,
			V4L2_DETECT_MD_MODE_THRESHOLD_GRID, 0,
			V4L2_DETECT_MD_MODE_DISABLED);
	v4l2_ctrl_new_std(hdl, &solo_ctrl_ops,
			V4L2_CID_DETECT_MD_GLOBAL_THRESHOLD, 0, 0xff, 1,
			SOLO_DEF_MOT_THRESH >> 8);
	v4l2_ctrl_new_custom(hdl, &solo_osd_text_ctrl, NULL);
	solo_enc->md_thresholds =
		v4l2_ctrl_new_custom(hdl, &solo_md_thresholds, NULL);
	if (hdl->error) {
		ret = hdl->error;
		goto hdl_free;
	}

	solo_enc->solo_dev = solo_dev;
	solo_enc->ch = ch;
	mutex_init(&solo_enc->lock);
	spin_lock_init(&solo_enc->av_lock);
	INIT_LIST_HEAD(&solo_enc->vidq_active);
	solo_enc->fmt = (solo_dev->type == SOLO_DEV_6010) ?
		V4L2_PIX_FMT_MPEG4 : V4L2_PIX_FMT_H264;
	solo_enc->type = SOLO_ENC_TYPE_STD;

	solo_enc->qp = SOLO_DEFAULT_QP;
	solo_enc->gop = solo_dev->fps;
	solo_enc->interval = 1;
	solo_enc->mode = SOLO_ENC_MODE_CIF;
	solo_enc->motion_global = true;
	solo_enc->motion_thresh = SOLO_DEF_MOT_THRESH;
	solo_enc->vidq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	solo_enc->vidq.io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	solo_enc->vidq.ops = &solo_enc_video_qops;
	solo_enc->vidq.mem_ops = &vb2_dma_sg_memops;
	solo_enc->vidq.drv_priv = solo_enc;
	solo_enc->vidq.gfp_flags = __GFP_DMA32 | __GFP_KSWAPD_RECLAIM;
	solo_enc->vidq.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	solo_enc->vidq.buf_struct_size = sizeof(struct solo_vb2_buf);
	solo_enc->vidq.lock = &solo_enc->lock;
	solo_enc->vidq.dev = &solo_dev->pdev->dev;
	ret = vb2_queue_init(&solo_enc->vidq);
	if (ret)
		goto hdl_free;
	solo_update_mode(solo_enc);

	spin_lock_init(&solo_enc->motion_lock);

	/* Initialize this per encoder */
	solo_enc->jpeg_len = sizeof(jpeg_header);
	memcpy(solo_enc->jpeg_header, jpeg_header, solo_enc->jpeg_len);

	solo_enc->desc_nelts = 32;
	solo_enc->desc_items = pci_alloc_consistent(solo_dev->pdev,
				      sizeof(struct solo_p2m_desc) *
				      solo_enc->desc_nelts,
				      &solo_enc->desc_dma);
	ret = -ENOMEM;
	if (solo_enc->desc_items == NULL)
		goto hdl_free;

	solo_enc->vfd = video_device_alloc();
	if (!solo_enc->vfd)
		goto pci_free;

	*solo_enc->vfd = solo_enc_template;
	solo_enc->vfd->v4l2_dev = &solo_dev->v4l2_dev;
	solo_enc->vfd->ctrl_handler = hdl;
	solo_enc->vfd->queue = &solo_enc->vidq;
	solo_enc->vfd->lock = &solo_enc->lock;
	video_set_drvdata(solo_enc->vfd, solo_enc);
	ret = video_register_device(solo_enc->vfd, VFL_TYPE_VIDEO, nr);
	if (ret < 0)
		goto vdev_release;

	snprintf(solo_enc->vfd->name, sizeof(solo_enc->vfd->name),
		 "%s-enc (%i/%i)", SOLO6X10_NAME, solo_dev->vfd->num,
		 solo_enc->vfd->num);

	return solo_enc;

vdev_release:
	video_device_release(solo_enc->vfd);
pci_free:
	pci_free_consistent(solo_enc->solo_dev->pdev,
			sizeof(struct solo_p2m_desc) * solo_enc->desc_nelts,
			solo_enc->desc_items, solo_enc->desc_dma);
hdl_free:
	v4l2_ctrl_handler_free(hdl);
	kfree(solo_enc);
	return ERR_PTR(ret);
}

static void solo_enc_free(struct solo_enc_dev *solo_enc)
{
	if (solo_enc == NULL)
		return;

	pci_free_consistent(solo_enc->solo_dev->pdev,
			sizeof(struct solo_p2m_desc) * solo_enc->desc_nelts,
			solo_enc->desc_items, solo_enc->desc_dma);
	video_unregister_device(solo_enc->vfd);
	v4l2_ctrl_handler_free(&solo_enc->hdl);
	kfree(solo_enc);
}

int solo_enc_v4l2_init(struct solo_dev *solo_dev, unsigned nr)
{
	int i;

	init_waitqueue_head(&solo_dev->ring_thread_wait);

	solo_dev->vh_size = sizeof(vop_header);
	solo_dev->vh_buf = pci_alloc_consistent(solo_dev->pdev,
						solo_dev->vh_size,
						&solo_dev->vh_dma);
	if (solo_dev->vh_buf == NULL)
		return -ENOMEM;

	for (i = 0; i < solo_dev->nr_chans; i++) {
		solo_dev->v4l2_enc[i] = solo_enc_alloc(solo_dev, i, nr);
		if (IS_ERR(solo_dev->v4l2_enc[i]))
			break;
	}

	if (i != solo_dev->nr_chans) {
		int ret = PTR_ERR(solo_dev->v4l2_enc[i]);

		while (i--)
			solo_enc_free(solo_dev->v4l2_enc[i]);
		pci_free_consistent(solo_dev->pdev, solo_dev->vh_size,
				    solo_dev->vh_buf, solo_dev->vh_dma);
		solo_dev->vh_buf = NULL;
		return ret;
	}

	if (solo_dev->type == SOLO_DEV_6010)
		solo_dev->enc_bw_remain = solo_dev->fps * 4 * 4;
	else
		solo_dev->enc_bw_remain = solo_dev->fps * 4 * 5;

	dev_info(&solo_dev->pdev->dev, "Encoders as /dev/video%d-%d\n",
		 solo_dev->v4l2_enc[0]->vfd->num,
		 solo_dev->v4l2_enc[solo_dev->nr_chans - 1]->vfd->num);

	return solo_ring_start(solo_dev);
}

void solo_enc_v4l2_exit(struct solo_dev *solo_dev)
{
	int i;

	solo_ring_stop(solo_dev);

	for (i = 0; i < solo_dev->nr_chans; i++)
		solo_enc_free(solo_dev->v4l2_enc[i]);

	if (solo_dev->vh_buf)
		pci_free_consistent(solo_dev->pdev, solo_dev->vh_size,
			    solo_dev->vh_buf, solo_dev->vh_dma);
}
