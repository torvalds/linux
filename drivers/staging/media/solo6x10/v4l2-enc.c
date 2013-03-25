/*
 * Copyright (C) 2010-2013 Bluecherry, LLC <http://www.bluecherrydvr.com>
 *
 * Original author:
 * Ben Collins <bcollins@ubuntu.com>
 *
 * Additional work by:
 * John Brooks <john.brooks@bluecherry.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/videobuf-dma-sg.h>

#include "solo6x10.h"
#include "tw28.h"
#include "solo6x10-jpeg.h"

#define MIN_VID_BUFFERS		2
#define FRAME_BUF_SIZE		(196 * 1024)
#define MP4_QS			16
#define DMA_ALIGN		4096

enum solo_enc_types {
	SOLO_ENC_TYPE_STD,
	SOLO_ENC_TYPE_EXT,
};

struct solo_enc_fh {
	struct			solo_enc_dev *enc;
	u32			fmt;
	u8			enc_on;
	enum solo_enc_types	type;
	struct videobuf_queue	vidq;
	struct list_head	vidq_active;
	int			desc_count;
	int			desc_nelts;
	struct solo_p2m_desc	*desc_items;
	dma_addr_t		desc_dma;
	spinlock_t		av_lock;
	struct list_head	list;
};

struct solo_videobuf {
	struct videobuf_buffer	vb;
	unsigned int		flags;
};

/* 6010 M4V */
static unsigned char vop_6010_ntsc_d1[] = {
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20,
	0x02, 0x48, 0x1d, 0xc0, 0x00, 0x40, 0x00, 0x40,
	0x00, 0x40, 0x00, 0x80, 0x00, 0x97, 0x53, 0x04,
	0x1f, 0x4c, 0x58, 0x10, 0xf0, 0x71, 0x18, 0x3f,
};

static unsigned char vop_6010_ntsc_cif[] = {
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20,
	0x02, 0x48, 0x1d, 0xc0, 0x00, 0x40, 0x00, 0x40,
	0x00, 0x40, 0x00, 0x80, 0x00, 0x97, 0x53, 0x04,
	0x1f, 0x4c, 0x2c, 0x10, 0x78, 0x51, 0x18, 0x3f,
};

static unsigned char vop_6010_pal_d1[] = {
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20,
	0x02, 0x48, 0x15, 0xc0, 0x00, 0x40, 0x00, 0x40,
	0x00, 0x40, 0x00, 0x80, 0x00, 0x97, 0x53, 0x04,
	0x1f, 0x4c, 0x58, 0x11, 0x20, 0x71, 0x18, 0x3f,
};

static unsigned char vop_6010_pal_cif[] = {
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20,
	0x02, 0x48, 0x15, 0xc0, 0x00, 0x40, 0x00, 0x40,
	0x00, 0x40, 0x00, 0x80, 0x00, 0x97, 0x53, 0x04,
	0x1f, 0x4c, 0x2c, 0x10, 0x90, 0x51, 0x18, 0x3f,
};

/* 6110 h.264 */
static unsigned char vop_6110_ntsc_d1[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
	0x9a, 0x74, 0x05, 0x81, 0xec, 0x80, 0x00, 0x00,
	0x00, 0x01, 0x68, 0xce, 0x32, 0x28, 0x00, 0x00,
};

static unsigned char vop_6110_ntsc_cif[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
	0x9a, 0x74, 0x0b, 0x0f, 0xc8, 0x00, 0x00, 0x00,
	0x01, 0x68, 0xce, 0x32, 0x28, 0x00, 0x00, 0x00,
};

static unsigned char vop_6110_pal_d1[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
	0x9a, 0x74, 0x05, 0x80, 0x93, 0x20, 0x00, 0x00,
	0x00, 0x01, 0x68, 0xce, 0x32, 0x28, 0x00, 0x00,
};

static unsigned char vop_6110_pal_cif[] = {
	0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
	0x9a, 0x74, 0x0b, 0x04, 0xb2, 0x00, 0x00, 0x00,
	0x01, 0x68, 0xce, 0x32, 0x28, 0x00, 0x00, 0x00,
};


static const u32 solo_user_ctrls[] = {
	V4L2_CID_BRIGHTNESS,
	V4L2_CID_CONTRAST,
	V4L2_CID_SATURATION,
	V4L2_CID_HUE,
	V4L2_CID_SHARPNESS,
	0
};

static const u32 solo_mpeg_ctrls[] = {
	V4L2_CID_MPEG_VIDEO_ENCODING,
	V4L2_CID_MPEG_VIDEO_GOP_SIZE,
	0
};

static const u32 solo_private_ctrls[] = {
	V4L2_CID_MOTION_ENABLE,
	V4L2_CID_MOTION_THRESHOLD,
	0
};

static const u32 solo_fmtx_ctrls[] = {
	V4L2_CID_RDS_TX_RADIO_TEXT,
	0
};

static const u32 *solo_ctrl_classes[] = {
	solo_user_ctrls,
	solo_mpeg_ctrls,
	solo_fmtx_ctrls,
	solo_private_ctrls,
	NULL
};

struct vop_header {
	/* VE_STATUS0 */
	u32 mpeg_size:20, sad_motion_flag:1, video_motion_flag:1, vop_type:2,
		channel:5, source_fl:1, interlace:1, progressive:1;

	/* VE_STATUS1 */
	u32 vsize:8, hsize:8, last_queue:4, nop0:8, scale:4;

	/* VE_STATUS2 */
	u32 mpeg_off;

	/* VE_STATUS3 */
	u32 jpeg_off;

	/* VE_STATUS4 */
	u32 jpeg_size:20, interval:10, nop1:2;

	/* VE_STATUS5/6 */
	u32 sec, usec;

	/* VE_STATUS7/8/9 */
	u32 nop2[3];

	/* VE_STATUS10 */
	u32 mpeg_size_alt:20, nop3:12;

	u32 end_nops[5];
} __packed;

struct solo_enc_buf {
	enum solo_enc_types	type;
	struct vop_header	*vh;
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

/* MUST be called with solo_enc->enable_lock held */
static void solo_update_mode(struct solo_enc_dev *solo_enc)
{
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	int vop_len;
	unsigned char *vop;

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

/* MUST be called with solo_enc->enable_lock held */
static int __solo_enc_on(struct solo_enc_fh *fh)
{
	struct solo_enc_dev *solo_enc = fh->enc;
	u8 ch = solo_enc->ch;
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	u8 interval;

	BUG_ON(!mutex_is_locked(&solo_enc->enable_lock));

	if (fh->enc_on)
		return 0;

	solo_update_mode(solo_enc);

	/* Make sure to bw check on first reader */
	if (!atomic_read(&solo_enc->readers)) {
		if (solo_enc->bw_weight > solo_dev->enc_bw_remain)
			return -EBUSY;
		else
			solo_dev->enc_bw_remain -= solo_enc->bw_weight;
	}

	fh->enc_on = 1;
	list_add(&fh->list, &solo_enc->listeners);

	if (fh->type == SOLO_ENC_TYPE_EXT)
		solo_reg_write(solo_dev, SOLO_CAP_CH_COMP_ENA_E(ch), 1);

	/* Reset the encoder if we are the first mpeg reader, else only reset
	 * on the first mjpeg reader. */
	if (fh->fmt == V4L2_PIX_FMT_MPEG) {
		atomic_inc(&solo_enc->readers);
		if (atomic_inc_return(&solo_enc->mpeg_readers) > 1)
			return 0;
	} else if (atomic_inc_return(&solo_enc->readers) > 1) {
		return 0;
	}

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

static int solo_enc_on(struct solo_enc_fh *fh)
{
	struct solo_enc_dev *solo_enc = fh->enc;
	int ret;

	mutex_lock(&solo_enc->enable_lock);
	ret = __solo_enc_on(fh);
	mutex_unlock(&solo_enc->enable_lock);

	return ret;
}

static void __solo_enc_off(struct solo_enc_fh *fh)
{
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	BUG_ON(!mutex_is_locked(&solo_enc->enable_lock));

	if (!fh->enc_on)
		return;

	list_del(&fh->list);
	fh->enc_on = 0;

	if (fh->fmt == V4L2_PIX_FMT_MPEG)
		atomic_dec(&solo_enc->mpeg_readers);

	if (atomic_dec_return(&solo_enc->readers) > 0)
		return;

	solo_dev->enc_bw_remain += solo_enc->bw_weight;

	solo_reg_write(solo_dev, SOLO_CAP_CH_SCALE(solo_enc->ch), 0);
	solo_reg_write(solo_dev, SOLO_CAP_CH_COMP_ENA_E(solo_enc->ch), 0);
}

static void solo_enc_off(struct solo_enc_fh *fh)
{
	struct solo_enc_dev *solo_enc = fh->enc;

	mutex_lock(&solo_enc->enable_lock);
	__solo_enc_off(fh);
	mutex_unlock(&solo_enc->enable_lock);
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
static int solo_send_desc(struct solo_enc_fh *fh, int skip,
			  struct videobuf_dmabuf *vbuf, int off, int size,
			  unsigned int base, unsigned int base_size)
{
	struct solo_dev *solo_dev = fh->enc->solo_dev;
	struct scatterlist *sg;
	int i;
	int ret;

	if (WARN_ON_ONCE(size > FRAME_BUF_SIZE))
		return -EINVAL;

	fh->desc_count = 1;

	for_each_sg(vbuf->sglist, sg, vbuf->sglen, i) {
		struct solo_p2m_desc *desc;
		dma_addr_t dma;
		int len;
		int left = base_size - off;

		desc = &fh->desc_items[fh->desc_count++];
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

			fh->desc_count--;
		}

		size -= len;
		if (size <= 0)
			break;

		off += len;
		if (off >= base_size)
			off -= base_size;

		/* Because we may use two descriptors per loop */
		if (fh->desc_count >= (fh->desc_nelts - 1)) {
			ret = solo_p2m_dma_desc(solo_dev, fh->desc_items,
						fh->desc_dma,
						fh->desc_count - 1);
			if (ret)
				return ret;
			fh->desc_count = 1;
		}
	}

	if (fh->desc_count <= 1)
		return 0;

	return solo_p2m_dma_desc(solo_dev, fh->desc_items, fh->desc_dma,
				 fh->desc_count - 1);
}

static int solo_fill_jpeg(struct solo_enc_fh *fh, struct videobuf_buffer *vb,
			  struct videobuf_dmabuf *vbuf, struct vop_header *vh)
{
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct solo_videobuf *svb = (struct solo_videobuf *)vb;
	int frame_size;

	svb->flags |= V4L2_BUF_FLAG_KEYFRAME;

	if (vb->bsize < (vh->jpeg_size + solo_enc->jpeg_len))
		return -EIO;

	vb->width = solo_enc->width;
	vb->height = solo_enc->height;
	vb->size = vh->jpeg_size + solo_enc->jpeg_len;

	sg_copy_from_buffer(vbuf->sglist, vbuf->sglen,
			    solo_enc->jpeg_header,
			    solo_enc->jpeg_len);

	frame_size = (vh->jpeg_size + solo_enc->jpeg_len + (DMA_ALIGN - 1))
		& ~(DMA_ALIGN - 1);

	return solo_send_desc(fh, solo_enc->jpeg_len, vbuf, vh->jpeg_off,
			      frame_size, SOLO_JPEG_EXT_ADDR(solo_dev),
			      SOLO_JPEG_EXT_SIZE(solo_dev));
}

static int solo_fill_mpeg(struct solo_enc_fh *fh, struct videobuf_buffer *vb,
			  struct videobuf_dmabuf *vbuf, struct vop_header *vh)
{
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct solo_videobuf *svb = (struct solo_videobuf *)vb;
	int frame_off, frame_size;
	int skip = 0;

	if (vb->bsize < vh->mpeg_size)
		return -EIO;

	vb->width = vh->hsize << 4;
	vb->height = vh->vsize << 4;
	vb->size = vh->mpeg_size;

	/* If this is a key frame, add extra header */
	if (!vh->vop_type) {
		sg_copy_from_buffer(vbuf->sglist, vbuf->sglen,
				    solo_enc->vop,
				    solo_enc->vop_len);

		skip = solo_enc->vop_len;
		vb->size += solo_enc->vop_len;

		svb->flags |= V4L2_BUF_FLAG_KEYFRAME;
	} else {
		svb->flags |= V4L2_BUF_FLAG_PFRAME;
	}

	/* Now get the actual mpeg payload */
	frame_off = (vh->mpeg_off + sizeof(*vh))
		% SOLO_MP4E_EXT_SIZE(solo_dev);
	frame_size = (vh->mpeg_size + skip + (DMA_ALIGN - 1))
		& ~(DMA_ALIGN - 1);

	return solo_send_desc(fh, skip, vbuf, frame_off, frame_size,
			      SOLO_MP4E_EXT_ADDR(solo_dev),
			      SOLO_MP4E_EXT_SIZE(solo_dev));
}

static int solo_enc_fillbuf(struct solo_enc_fh *fh,
			    struct videobuf_buffer *vb,
			    struct solo_enc_buf *enc_buf)
{
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_videobuf *svb = (struct solo_videobuf *)vb;
	struct videobuf_dmabuf *vbuf = NULL;
	struct vop_header *vh = enc_buf->vh;
	int ret;

	vbuf = videobuf_to_dma(vb);
	if (WARN_ON_ONCE(!vbuf)) {
		ret = -EIO;
		goto vbuf_error;
	}

	/* Setup some common flags for both types */
	svb->flags = V4L2_BUF_FLAG_TIMECODE;
	vb->ts.tv_sec = vh->sec;
	vb->ts.tv_usec = vh->usec;

	/* Check for motion flags */
	if (solo_is_motion_on(solo_enc)) {
		svb->flags |= V4L2_BUF_FLAG_MOTION_ON;
		if (enc_buf->motion)
			svb->flags |= V4L2_BUF_FLAG_MOTION_DETECTED;
	}

	if (fh->fmt == V4L2_PIX_FMT_MPEG)
		ret = solo_fill_mpeg(fh, vb, vbuf, vh);
	else
		ret = solo_fill_jpeg(fh, vb, vbuf, vh);

vbuf_error:
	/* On error, we push this buffer back into the queue. The
	 * videobuf-core doesn't handle error packets very well. Plus
	 * we recover nicely internally anyway. */
	if (ret) {
		unsigned long flags;

		spin_lock_irqsave(&fh->av_lock, flags);
		list_add(&vb->queue, &fh->vidq_active);
		vb->state = VIDEOBUF_QUEUED;
		spin_unlock_irqrestore(&fh->av_lock, flags);
	} else {
		vb->state = VIDEOBUF_DONE;
		vb->field_count++;
		vb->width = solo_enc->width;
		vb->height = solo_enc->height;

		wake_up(&vb->done);
	}

	return ret;
}

static void solo_enc_handle_one(struct solo_enc_dev *solo_enc,
				struct solo_enc_buf *enc_buf)
{
	struct solo_enc_fh *fh;

	mutex_lock(&solo_enc->enable_lock);

	list_for_each_entry(fh, &solo_enc->listeners, list) {
		struct videobuf_buffer *vb;
		unsigned long flags;

		if (fh->type != enc_buf->type)
			continue;


		if (list_empty(&fh->vidq_active))
			continue;

		spin_lock_irqsave(&fh->av_lock, flags);

		vb = list_first_entry(&fh->vidq_active,
				      struct videobuf_buffer, queue);

		list_del(&vb->queue);
		vb->state = VIDEOBUF_ACTIVE;

		spin_unlock_irqrestore(&fh->av_lock, flags);

		solo_enc_fillbuf(fh, vb, enc_buf);
	}

	mutex_unlock(&solo_enc->enable_lock);
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
				     sizeof(struct vop_header)))
			continue;

		enc_buf.vh = (struct vop_header *)solo_dev->vh_buf;
		enc_buf.vh->mpeg_off -= SOLO_MP4E_EXT_ADDR(solo_dev);
		enc_buf.vh->jpeg_off -= SOLO_JPEG_EXT_ADDR(solo_dev);

		/* Sanity check */
		if (enc_buf.vh->mpeg_off != off)
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
		solo_irq_off(solo_dev, SOLO_IRQ_ENCODER);
		solo_handle_ring(solo_dev);
		solo_irq_on(solo_dev, SOLO_IRQ_ENCODER);
		try_to_freeze();
	}

	remove_wait_queue(&solo_dev->ring_thread_wait, &wait);

	return 0;
}

static int solo_enc_buf_setup(struct videobuf_queue *vq, unsigned int *count,
			      unsigned int *size)
{
	*size = FRAME_BUF_SIZE;

	if (*count < MIN_VID_BUFFERS)
		*count = MIN_VID_BUFFERS;

	return 0;
}

static int solo_enc_buf_prepare(struct videobuf_queue *vq,
				struct videobuf_buffer *vb,
				enum v4l2_field field)
{
	vb->size = FRAME_BUF_SIZE;
	if (vb->baddr != 0 && vb->bsize < vb->size)
		return -EINVAL;

	/* This property only change when queue is idle */
	vb->field = field;

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		int rc = videobuf_iolock(vq, vb, NULL);
		if (rc < 0) {
			struct videobuf_dmabuf *dma = videobuf_to_dma(vb);
			videobuf_dma_unmap(vq->dev, dma);
			videobuf_dma_free(dma);

			return rc;
		}
	}
	vb->state = VIDEOBUF_PREPARED;

	return 0;
}

static void solo_enc_buf_queue(struct videobuf_queue *vq,
			       struct videobuf_buffer *vb)
{
	struct solo_enc_fh *fh = vq->priv_data;

	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &fh->vidq_active);
}

static void solo_enc_buf_release(struct videobuf_queue *vq,
				 struct videobuf_buffer *vb)
{
	struct videobuf_dmabuf *dma = videobuf_to_dma(vb);
	videobuf_dma_unmap(vq->dev, dma);
	videobuf_dma_free(dma);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static const struct videobuf_queue_ops solo_enc_video_qops = {
	.buf_setup	= solo_enc_buf_setup,
	.buf_prepare	= solo_enc_buf_prepare,
	.buf_queue	= solo_enc_buf_queue,
	.buf_release	= solo_enc_buf_release,
};

static unsigned int solo_enc_poll(struct file *file,
				  struct poll_table_struct *wait)
{
	struct solo_enc_fh *fh = file->private_data;

	return videobuf_poll_stream(file, &fh->vidq, wait);
}

static int solo_enc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct solo_enc_fh *fh = file->private_data;

	return videobuf_mmap_mapper(&fh->vidq, vma);
}

static int solo_ring_start(struct solo_dev *solo_dev)
{
	if (atomic_inc_return(&solo_dev->enc_users) > 1)
		return 0;

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
	if (atomic_dec_return(&solo_dev->enc_users) > 0)
		return;

	if (solo_dev->ring_thread) {
		kthread_stop(solo_dev->ring_thread);
		solo_dev->ring_thread = NULL;
	}

	solo_irq_off(solo_dev, SOLO_IRQ_ENCODER);
}

static int solo_enc_open(struct file *file)
{
	struct solo_enc_dev *solo_enc = video_drvdata(file);
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct solo_enc_fh *fh;
	int ret;

	ret = solo_ring_start(solo_dev);
	if (ret)
		return ret;

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (fh == NULL) {
		solo_ring_stop(solo_dev);
		return -ENOMEM;
	}

	fh->desc_nelts = 32;
	fh->desc_items = pci_alloc_consistent(solo_dev->pdev,
				      sizeof(struct solo_p2m_desc) *
				      fh->desc_nelts, &fh->desc_dma);
	if (fh->desc_items == NULL) {
		kfree(fh);
		solo_ring_stop(solo_dev);
		return -ENOMEM;
	}

	fh->enc = solo_enc;
	spin_lock_init(&fh->av_lock);
	file->private_data = fh;
	INIT_LIST_HEAD(&fh->vidq_active);
	fh->fmt = V4L2_PIX_FMT_MPEG;
	fh->type = SOLO_ENC_TYPE_STD;

	videobuf_queue_sg_init(&fh->vidq, &solo_enc_video_qops,
				&solo_dev->pdev->dev,
				&fh->av_lock,
				V4L2_BUF_TYPE_VIDEO_CAPTURE,
				V4L2_FIELD_INTERLACED,
				sizeof(struct solo_videobuf),
				fh, NULL);

	return 0;
}

static ssize_t solo_enc_read(struct file *file, char __user *data,
			     size_t count, loff_t *ppos)
{
	struct solo_enc_fh *fh = file->private_data;
	int ret;

	/* Make sure the encoder is on */
	ret = solo_enc_on(fh);
	if (ret)
		return ret;

	return videobuf_read_stream(&fh->vidq, data, count, ppos, 0,
				    file->f_flags & O_NONBLOCK);
}

static int solo_enc_release(struct file *file)
{
	struct solo_enc_fh *fh = file->private_data;
	struct solo_dev *solo_dev = fh->enc->solo_dev;

	solo_enc_off(fh);

	videobuf_stop(&fh->vidq);
	videobuf_mmap_free(&fh->vidq);

	pci_free_consistent(fh->enc->solo_dev->pdev,
			    sizeof(struct solo_p2m_desc) *
			    fh->desc_nelts, fh->desc_items, fh->desc_dma);

	kfree(fh);

	solo_ring_stop(solo_dev);

	return 0;
}

static int solo_enc_querycap(struct file *file, void  *priv,
			     struct v4l2_capability *cap)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	strcpy(cap->driver, SOLO6X10_NAME);
	snprintf(cap->card, sizeof(cap->card), "Softlogic 6x10 Enc %d",
		 solo_enc->ch);
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI %s",
		 pci_name(solo_dev->pdev));
	cap->version = SOLO6X10_VER_NUM;
	cap->capabilities =     V4L2_CAP_VIDEO_CAPTURE |
				V4L2_CAP_READWRITE |
				V4L2_CAP_STREAMING;
	return 0;
}

static int solo_enc_enum_input(struct file *file, void *priv,
			       struct v4l2_input *input)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	if (input->index)
		return -EINVAL;

	snprintf(input->name, sizeof(input->name), "Encoder %d",
		 solo_enc->ch + 1);
	input->type = V4L2_INPUT_TYPE_CAMERA;

	if (solo_dev->video_type == SOLO_VO_FMT_TYPE_NTSC)
		input->std = V4L2_STD_NTSC_M;
	else
		input->std = V4L2_STD_PAL_B;

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
	switch (f->index) {
	case 0:
		f->pixelformat = V4L2_PIX_FMT_MPEG;
		strcpy(f->description, "MPEG-4 AVC");
		break;
	case 1:
		f->pixelformat = V4L2_PIX_FMT_MJPEG;
		strcpy(f->description, "MJPEG");
		break;
	default:
		return -EINVAL;
	}

	f->flags = V4L2_FMT_FLAG_COMPRESSED;

	return 0;
}

static int solo_enc_try_fmt_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (pix->pixelformat != V4L2_PIX_FMT_MPEG &&
	    pix->pixelformat != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	/* We cannot change width/height in mid mpeg */
	if (atomic_read(&solo_enc->mpeg_readers) > 0) {
		if (pix->width != solo_enc->width ||
		    pix->height != solo_enc->height)
			return -EBUSY;
	}

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

	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_INTERLACED;
	else if (pix->field != V4L2_FIELD_INTERLACED)
		pix->field = V4L2_FIELD_INTERLACED;

	/* Just set these */
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pix->sizeimage = FRAME_BUF_SIZE;

	return 0;
}

static int solo_enc_set_fmt_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int ret;

	mutex_lock(&solo_enc->enable_lock);

	ret = solo_enc_try_fmt_cap(file, priv, f);
	if (ret) {
		mutex_unlock(&solo_enc->enable_lock);
		return ret;
	}

	if (pix->width == solo_dev->video_hsize)
		solo_enc->mode = SOLO_ENC_MODE_D1;
	else
		solo_enc->mode = SOLO_ENC_MODE_CIF;

	/* This does not change the encoder at all */
	fh->fmt = pix->pixelformat;

	if (pix->priv)
		fh->type = SOLO_ENC_TYPE_EXT;

	mutex_unlock(&solo_enc->enable_lock);

	return 0;
}

static int solo_enc_get_fmt_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	pix->width = solo_enc->width;
	pix->height = solo_enc->height;
	pix->pixelformat = fh->fmt;
	pix->field = solo_enc->interlaced ? V4L2_FIELD_INTERLACED :
		     V4L2_FIELD_NONE;
	pix->sizeimage = FRAME_BUF_SIZE;
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int solo_enc_reqbufs(struct file *file, void *priv,
			    struct v4l2_requestbuffers *req)
{
	struct solo_enc_fh *fh = priv;

	return videobuf_reqbufs(&fh->vidq, req);
}

static int solo_enc_querybuf(struct file *file, void *priv,
			     struct v4l2_buffer *buf)
{
	struct solo_enc_fh *fh = priv;

	return videobuf_querybuf(&fh->vidq, buf);
}

static int solo_enc_qbuf(struct file *file, void *priv,
			 struct v4l2_buffer *buf)
{
	struct solo_enc_fh *fh = priv;

	return videobuf_qbuf(&fh->vidq, buf);
}

static int solo_enc_dqbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct solo_enc_fh *fh = priv;
	struct solo_videobuf *svb;
	int ret;

	/* Make sure the encoder is on */
	ret = solo_enc_on(fh);
	if (ret)
		return ret;

	ret = videobuf_dqbuf(&fh->vidq, buf, file->f_flags & O_NONBLOCK);
	if (ret)
		return ret;

	/* Copy over the flags */
	svb = (struct solo_videobuf *)fh->vidq.bufs[buf->index];
	buf->flags |= svb->flags;

	return 0;
}

static int solo_enc_streamon(struct file *file, void *priv,
			     enum v4l2_buf_type i)
{
	struct solo_enc_fh *fh = priv;

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	return videobuf_streamon(&fh->vidq);
}

static int solo_enc_streamoff(struct file *file, void *priv,
			      enum v4l2_buf_type i)
{
	struct solo_enc_fh *fh = priv;
	int ret;

	if (i != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	ret = videobuf_streamoff(&fh->vidq);
	if (!ret)
		solo_enc_off(fh);

	return ret;
}

static int solo_enc_s_std(struct file *file, void *priv, v4l2_std_id i)
{
	return 0;
}

static int solo_enum_framesizes(struct file *file, void *priv,
				struct v4l2_frmsizeenum *fsize)
{
	struct solo_enc_fh *fh = priv;
	struct solo_dev *solo_dev = fh->enc->solo_dev;

	if (fsize->pixel_format != V4L2_PIX_FMT_MPEG)
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
	struct solo_enc_fh *fh = priv;
	struct solo_dev *solo_dev = fh->enc->solo_dev;

	if (fintv->pixel_format != V4L2_PIX_FMT_MPEG || fintv->index)
		return -EINVAL;

	fintv->type = V4L2_FRMIVAL_TYPE_STEPWISE;

	fintv->stepwise.min.numerator = solo_dev->fps;
	fintv->stepwise.min.denominator = 1;

	fintv->stepwise.max.numerator = solo_dev->fps;
	fintv->stepwise.max.denominator = 15;

	fintv->stepwise.step.numerator = 1;
	fintv->stepwise.step.denominator = 1;

	return 0;
}

static int solo_g_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *sp)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct v4l2_captureparm *cp = &sp->parm.capture;

	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = solo_enc->interval;
	cp->timeperframe.denominator = solo_dev->fps;
	cp->capturemode = 0;
	/* XXX: Shouldn't we be able to get/set this from videobuf? */
	cp->readbuffers = 2;

	return 0;
}

static int solo_s_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *sp)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;
	struct v4l2_captureparm *cp = &sp->parm.capture;

	mutex_lock(&solo_enc->enable_lock);

	if (atomic_read(&solo_enc->mpeg_readers) > 0) {
		mutex_unlock(&solo_enc->enable_lock);
		return -EBUSY;
	}

	if ((cp->timeperframe.numerator == 0) ||
	    (cp->timeperframe.denominator == 0)) {
		/* reset framerate */
		cp->timeperframe.numerator = 1;
		cp->timeperframe.denominator = solo_dev->fps;
	}

	if (cp->timeperframe.denominator != solo_dev->fps)
		cp->timeperframe.denominator = solo_dev->fps;

	if (cp->timeperframe.numerator > 15)
		cp->timeperframe.numerator = 15;

	solo_enc->interval = cp->timeperframe.numerator;

	cp->capability = V4L2_CAP_TIMEPERFRAME;

	solo_update_mode(solo_enc);

	mutex_unlock(&solo_enc->enable_lock);

	return 0;
}

static int solo_queryctrl(struct file *file, void *priv,
			  struct v4l2_queryctrl *qc)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	qc->id = v4l2_ctrl_next(solo_ctrl_classes, qc->id);
	if (!qc->id)
		return -EINVAL;

	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
		return v4l2_ctrl_query_fill(qc, 0x00, 0xff, 1, 0x80);
	case V4L2_CID_SHARPNESS:
		return v4l2_ctrl_query_fill(qc, 0x00, 0x0f, 1, 0x00);
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		return v4l2_ctrl_query_fill(
			qc, V4L2_MPEG_VIDEO_ENCODING_MPEG_1,
			V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC, 1,
			V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC);
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		return v4l2_ctrl_query_fill(qc, 1, 255, 1, solo_dev->fps);
#ifdef PRIVATE_CIDS
	case V4L2_CID_MOTION_THRESHOLD:
		qc->flags |= V4L2_CTRL_FLAG_SLIDER;
		qc->type = V4L2_CTRL_TYPE_INTEGER;
		qc->minimum = 0;
		qc->maximum = 0xffff;
		qc->step = 1;
		qc->default_value = SOLO_DEF_MOT_THRESH;
		strlcpy(qc->name, "Motion Detection Threshold",
			sizeof(qc->name));
		return 0;
	case V4L2_CID_MOTION_ENABLE:
		qc->type = V4L2_CTRL_TYPE_BOOLEAN;
		qc->minimum = 0;
		qc->maximum = qc->step = 1;
		qc->default_value = 0;
		strlcpy(qc->name, "Motion Detection Enable", sizeof(qc->name));
		return 0;
#else
	case V4L2_CID_MOTION_THRESHOLD:
		return v4l2_ctrl_query_fill(qc, 0, 0xffff, 1,
					    SOLO_DEF_MOT_THRESH);
	case V4L2_CID_MOTION_ENABLE:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 0);
#endif
	case V4L2_CID_RDS_TX_RADIO_TEXT:
		qc->type = V4L2_CTRL_TYPE_STRING;
		qc->minimum = 0;
		qc->maximum = OSD_TEXT_MAX;
		qc->step = 1;
		qc->default_value = 0;
		strlcpy(qc->name, "OSD Text", sizeof(qc->name));
		return 0;
	}

	return -EINVAL;
}

static int solo_querymenu(struct file *file, void *priv,
			  struct v4l2_querymenu *qmenu)
{
	struct v4l2_queryctrl qctrl;
	int err;

	qctrl.id = qmenu->id;

	err = solo_queryctrl(file, priv, &qctrl);
	if (err)
		return err;

	return v4l2_ctrl_query_menu(qmenu, &qctrl, NULL);
}

static int solo_g_ctrl(struct file *file, void *priv,
		       struct v4l2_control *ctrl)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_SHARPNESS:
		return tw28_get_ctrl_val(solo_dev, ctrl->id, solo_enc->ch,
					 &ctrl->value);
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		ctrl->value = V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		if (atomic_read(&solo_enc->readers) > 0)
			return -EBUSY;
		ctrl->value = solo_enc->gop;
		break;
	case V4L2_CID_MOTION_THRESHOLD:
		ctrl->value = solo_enc->motion_thresh;
		break;
	case V4L2_CID_MOTION_ENABLE:
		ctrl->value = solo_is_motion_on(solo_enc);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int solo_s_ctrl(struct file *file, void *priv,
		       struct v4l2_control *ctrl)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	struct solo_dev *solo_dev = solo_enc->solo_dev;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
	case V4L2_CID_HUE:
	case V4L2_CID_SHARPNESS:
		return tw28_set_ctrl_val(solo_dev, ctrl->id, solo_enc->ch,
					 ctrl->value);
	case V4L2_CID_MPEG_VIDEO_ENCODING:
		if (ctrl->value != V4L2_MPEG_VIDEO_ENCODING_MPEG_4_AVC)
			return -ERANGE;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		if (ctrl->value < 1 || ctrl->value > 255)
			return -ERANGE;
		solo_enc->gop = ctrl->value;
		break;
	case V4L2_CID_MOTION_THRESHOLD:
	{
		u16 block = (ctrl->value >> 16) & 0xffff;
		u16 value = ctrl->value & 0xffff;

		/* Motion thresholds are in a table of 64x64 samples, with
		 * each sample representing 16x16 pixels of the source. In
		 * effect, 44x30 samples are used for NTSC, and 44x36 for PAL.
		 * The 5th sample on the 10th row is (10*64)+5 = 645.
		 *
		 * Block is 0 to set the threshold globally, or any positive
		 * number under 2049 to set block-1 individually. */
		if (block > 2049)
			return -ERANGE;

		if (block == 0) {
			solo_enc->motion_thresh = value;
			return solo_set_motion_threshold(solo_dev,
							 solo_enc->ch, value);
		} else {
			return solo_set_motion_block(solo_dev, solo_enc->ch,
						     value, block - 1);
		}
		break;
	}
	case V4L2_CID_MOTION_ENABLE:
		solo_motion_toggle(solo_enc, ctrl->value);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int solo_s_ext_ctrls(struct file *file, void *priv,
			    struct v4l2_ext_controls *ctrls)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	int i;

	for (i = 0; i < ctrls->count; i++) {
		struct v4l2_ext_control *ctrl = (ctrls->controls + i);
		int err;

		switch (ctrl->id) {
		case V4L2_CID_RDS_TX_RADIO_TEXT:
			if (ctrl->size - 1 > OSD_TEXT_MAX)
				err = -ERANGE;
			else {
				mutex_lock(&solo_enc->enable_lock);
				err = copy_from_user(solo_enc->osd_text,
						     ctrl->string,
						     OSD_TEXT_MAX);
				solo_enc->osd_text[OSD_TEXT_MAX] = '\0';
				if (!err)
					err = solo_osd_print(solo_enc);
				else
					err = -EFAULT;
				mutex_unlock(&solo_enc->enable_lock);
			}
			break;
		default:
			err = -EINVAL;
		}

		if (err < 0) {
			ctrls->error_idx = i;
			return err;
		}
	}

	return 0;
}

static int solo_g_ext_ctrls(struct file *file, void *priv,
			    struct v4l2_ext_controls *ctrls)
{
	struct solo_enc_fh *fh = priv;
	struct solo_enc_dev *solo_enc = fh->enc;
	int i;

	for (i = 0; i < ctrls->count; i++) {
		struct v4l2_ext_control *ctrl = (ctrls->controls + i);
		int err;

		switch (ctrl->id) {
		case V4L2_CID_RDS_TX_RADIO_TEXT:
			if (ctrl->size < OSD_TEXT_MAX) {
				ctrl->size = OSD_TEXT_MAX;
				err = -ENOSPC;
			} else {
				mutex_lock(&solo_enc->enable_lock);
				err = copy_to_user(ctrl->string,
						   solo_enc->osd_text,
						   OSD_TEXT_MAX);
				if (err)
					err = -EFAULT;
				mutex_unlock(&solo_enc->enable_lock);
			}
			break;
		default:
			err = -EINVAL;
		}

		if (err < 0) {
			ctrls->error_idx = i;
			return err;
		}
	}

	return 0;
}

static const struct v4l2_file_operations solo_enc_fops = {
	.owner			= THIS_MODULE,
	.open			= solo_enc_open,
	.release		= solo_enc_release,
	.read			= solo_enc_read,
	.poll			= solo_enc_poll,
	.mmap			= solo_enc_mmap,
	.ioctl			= video_ioctl2,
};

static const struct v4l2_ioctl_ops solo_enc_ioctl_ops = {
	.vidioc_querycap		= solo_enc_querycap,
	.vidioc_s_std			= solo_enc_s_std,
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
	.vidioc_reqbufs			= solo_enc_reqbufs,
	.vidioc_querybuf		= solo_enc_querybuf,
	.vidioc_qbuf			= solo_enc_qbuf,
	.vidioc_dqbuf			= solo_enc_dqbuf,
	.vidioc_streamon		= solo_enc_streamon,
	.vidioc_streamoff		= solo_enc_streamoff,
	/* Frame size and interval */
	.vidioc_enum_framesizes		= solo_enum_framesizes,
	.vidioc_enum_frameintervals	= solo_enum_frameintervals,
	/* Video capture parameters */
	.vidioc_s_parm			= solo_s_parm,
	.vidioc_g_parm			= solo_g_parm,
	/* Controls */
	.vidioc_queryctrl		= solo_queryctrl,
	.vidioc_querymenu		= solo_querymenu,
	.vidioc_g_ctrl			= solo_g_ctrl,
	.vidioc_s_ctrl			= solo_s_ctrl,
	.vidioc_g_ext_ctrls		= solo_g_ext_ctrls,
	.vidioc_s_ext_ctrls		= solo_s_ext_ctrls,
};

static const struct video_device solo_enc_template = {
	.name			= SOLO6X10_NAME,
	.fops			= &solo_enc_fops,
	.ioctl_ops		= &solo_enc_ioctl_ops,
	.minor			= -1,
	.release		= video_device_release,

	.tvnorms		= V4L2_STD_NTSC_M | V4L2_STD_PAL_B,
	.current_norm		= V4L2_STD_NTSC_M,
};

static struct solo_enc_dev *solo_enc_alloc(struct solo_dev *solo_dev,
					   u8 ch, unsigned nr)
{
	struct solo_enc_dev *solo_enc;
	int ret;

	solo_enc = kzalloc(sizeof(*solo_enc), GFP_KERNEL);
	if (!solo_enc)
		return ERR_PTR(-ENOMEM);

	solo_enc->vfd = video_device_alloc();
	if (!solo_enc->vfd) {
		kfree(solo_enc);
		return ERR_PTR(-ENOMEM);
	}

	solo_enc->solo_dev = solo_dev;
	solo_enc->ch = ch;

	*solo_enc->vfd = solo_enc_template;
	solo_enc->vfd->parent = &solo_dev->pdev->dev;
	ret = video_register_device(solo_enc->vfd, VFL_TYPE_GRABBER, nr);
	if (ret < 0) {
		video_device_release(solo_enc->vfd);
		kfree(solo_enc);
		return ERR_PTR(ret);
	}

	video_set_drvdata(solo_enc->vfd, solo_enc);

	snprintf(solo_enc->vfd->name, sizeof(solo_enc->vfd->name),
		 "%s-enc (%i/%i)", SOLO6X10_NAME, solo_dev->vfd->num,
		 solo_enc->vfd->num);

	INIT_LIST_HEAD(&solo_enc->listeners);
	mutex_init(&solo_enc->enable_lock);
	spin_lock_init(&solo_enc->motion_lock);

	atomic_set(&solo_enc->readers, 0);
	atomic_set(&solo_enc->mpeg_readers, 0);

	solo_enc->qp = SOLO_DEFAULT_QP;
	solo_enc->gop = solo_dev->fps;
	solo_enc->interval = 1;
	solo_enc->mode = SOLO_ENC_MODE_CIF;
	solo_enc->motion_thresh = SOLO_DEF_MOT_THRESH;

	mutex_lock(&solo_enc->enable_lock);
	solo_update_mode(solo_enc);
	mutex_unlock(&solo_enc->enable_lock);

	/* Initialize this per encoder */
	solo_enc->jpeg_len = sizeof(jpeg_header);
	memcpy(solo_enc->jpeg_header, jpeg_header, solo_enc->jpeg_len);

	return solo_enc;
}

static void solo_enc_free(struct solo_enc_dev *solo_enc)
{
	if (solo_enc == NULL)
		return;

	video_unregister_device(solo_enc->vfd);
	kfree(solo_enc);
}

int solo_enc_v4l2_init(struct solo_dev *solo_dev, unsigned nr)
{
	int i;

	atomic_set(&solo_dev->enc_users, 0);
	init_waitqueue_head(&solo_dev->ring_thread_wait);

	solo_dev->vh_size = sizeof(struct vop_header);
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
		return ret;
	}

	if (solo_dev->type == SOLO_DEV_6010)
		solo_dev->enc_bw_remain = solo_dev->fps * 4 * 4;
	else
		solo_dev->enc_bw_remain = solo_dev->fps * 4 * 5;

	dev_info(&solo_dev->pdev->dev, "Encoders as /dev/video%d-%d\n",
		 solo_dev->v4l2_enc[0]->vfd->num,
		 solo_dev->v4l2_enc[solo_dev->nr_chans - 1]->vfd->num);

	return 0;
}

void solo_enc_v4l2_exit(struct solo_dev *solo_dev)
{
	int i;

	for (i = 0; i < solo_dev->nr_chans; i++)
		solo_enc_free(solo_dev->v4l2_enc[i]);

	pci_free_consistent(solo_dev->pdev, solo_dev->vh_size,
			    solo_dev->vh_buf, solo_dev->vh_dma);
}
