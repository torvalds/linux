/*

    bttv - Bt848 frame grabber driver
    vbi interface

    (c) 2002 Gerd Knorr <kraxel@bytesex.org>

    Copyright (C) 2005, 2006 Michael H. Schimek <mschimek@gmx.at>
    Sponsored by OPQ Systems AB

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <asm/io.h>
#include "bttvp.h"

/* Offset from line sync pulse leading edge (0H) to start of VBI capture,
   in fCLKx2 pixels.  According to the datasheet, VBI capture starts
   VBI_HDELAY fCLKx1 pixels from the tailing edgeof /HRESET, and /HRESET
   is 64 fCLKx1 pixels wide.  VBI_HDELAY is set to 0, so this should be
   (64 + 0) * 2 = 128 fCLKx2 pixels.  But it's not!  The datasheet is
   Just Plain Wrong.  The real value appears to be different for
   different revisions of the bt8x8 chips, and to be affected by the
   horizontal scaling factor.  Experimentally, the value is measured
   to be about 244.  */
#define VBI_OFFSET 244

/* 2048 for compatibility with earlier driver versions. The driver
   really stores 1024 + tvnorm->vbipack * 4 samples per line in the
   buffer. Note tvnorm->vbipack is <= 0xFF (limit of VBIPACK_LO + HI
   is 0x1FF DWORDs) and VBI read()s store a frame counter in the last
   four bytes of the VBI image. */
#define VBI_BPL 2048

/* Compatibility. */
#define VBI_DEFLINES 16

static unsigned int vbibufs = 4;
static unsigned int vbi_debug = 0;

module_param(vbibufs,   int, 0444);
module_param(vbi_debug, int, 0644);
MODULE_PARM_DESC(vbibufs,"number of vbi buffers, range 2-32, default 4");
MODULE_PARM_DESC(vbi_debug,"vbi code debug messages, default is 0 (no)");

#ifdef dprintk
# undef dprintk
#endif
#define dprintk(fmt, arg...)	if (vbi_debug) \
	printk(KERN_DEBUG "bttv%d/vbi: " fmt, btv->c.nr , ## arg)

#define IMAGE_SIZE(fmt) \
	(((fmt)->count[0] + (fmt)->count[1]) * (fmt)->samples_per_line)

/* ----------------------------------------------------------------------- */
/* vbi risc code + mm                                                      */

static int vbi_buffer_setup(struct videobuf_queue *q,
			    unsigned int *count, unsigned int *size)
{
	struct bttv_fh *fh = q->priv_data;
	struct bttv *btv = fh->btv;

	if (0 == *count)
		*count = vbibufs;

	*size = IMAGE_SIZE(&fh->vbi_fmt.fmt);

	dprintk("setup: samples=%u start=%d,%d count=%u,%u\n",
		fh->vbi_fmt.fmt.samples_per_line,
		fh->vbi_fmt.fmt.start[0],
		fh->vbi_fmt.fmt.start[1],
		fh->vbi_fmt.fmt.count[0],
		fh->vbi_fmt.fmt.count[1]);

	return 0;
}

static int vbi_buffer_prepare(struct videobuf_queue *q,
			      struct videobuf_buffer *vb,
			      enum v4l2_field field)
{
	struct bttv_fh *fh = q->priv_data;
	struct bttv *btv = fh->btv;
	struct bttv_buffer *buf = container_of(vb,struct bttv_buffer,vb);
	const struct bttv_tvnorm *tvnorm;
	unsigned int skip_lines0, skip_lines1, min_vdelay;
	int redo_dma_risc;
	int rc;

	buf->vb.size = IMAGE_SIZE(&fh->vbi_fmt.fmt);
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	tvnorm = fh->vbi_fmt.tvnorm;

	/* There's no VBI_VDELAY register, RISC must skip the lines
	   we don't want. With default parameters we skip zero lines
	   as earlier driver versions did. The driver permits video
	   standard changes while capturing, so we use vbi_fmt.tvnorm
	   instead of btv->tvnorm to skip zero lines after video
	   standard changes as well. */

	skip_lines0 = 0;
	skip_lines1 = 0;

	if (fh->vbi_fmt.fmt.count[0] > 0)
		skip_lines0 = max(0, (fh->vbi_fmt.fmt.start[0]
				      - tvnorm->vbistart[0]));
	if (fh->vbi_fmt.fmt.count[1] > 0)
		skip_lines1 = max(0, (fh->vbi_fmt.fmt.start[1]
				      - tvnorm->vbistart[1]));

	redo_dma_risc = 0;

	if (buf->vbi_skip[0] != skip_lines0 ||
	    buf->vbi_skip[1] != skip_lines1 ||
	    buf->vbi_count[0] != fh->vbi_fmt.fmt.count[0] ||
	    buf->vbi_count[1] != fh->vbi_fmt.fmt.count[1]) {
		buf->vbi_skip[0] = skip_lines0;
		buf->vbi_skip[1] = skip_lines1;
		buf->vbi_count[0] = fh->vbi_fmt.fmt.count[0];
		buf->vbi_count[1] = fh->vbi_fmt.fmt.count[1];
		redo_dma_risc = 1;
	}

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		redo_dma_risc = 1;
		if (0 != (rc = videobuf_iolock(q, &buf->vb, NULL)))
			goto fail;
	}

	if (redo_dma_risc) {
		unsigned int bpl, padding, offset;
		struct videobuf_dmabuf *dma=videobuf_to_dma(&buf->vb);

		bpl = 2044; /* max. vbipack */
		padding = VBI_BPL - bpl;

		if (fh->vbi_fmt.fmt.count[0] > 0) {
			rc = bttv_risc_packed(btv, &buf->top,
					      dma->sglist,
					      /* offset */ 0, bpl,
					      padding, skip_lines0,
					      fh->vbi_fmt.fmt.count[0]);
			if (0 != rc)
				goto fail;
		}

		if (fh->vbi_fmt.fmt.count[1] > 0) {
			offset = fh->vbi_fmt.fmt.count[0] * VBI_BPL;

			rc = bttv_risc_packed(btv, &buf->bottom,
					      dma->sglist,
					      offset, bpl,
					      padding, skip_lines1,
					      fh->vbi_fmt.fmt.count[1]);
			if (0 != rc)
				goto fail;
		}
	}

	/* VBI capturing ends at VDELAY, start of video capturing,
	   no matter where the RISC program ends. VDELAY minimum is 2,
	   bounds.top is the corresponding first field line number
	   times two. VDELAY counts half field lines. */
	min_vdelay = MIN_VDELAY;
	if (fh->vbi_fmt.end >= tvnorm->cropcap.bounds.top)
		min_vdelay += fh->vbi_fmt.end - tvnorm->cropcap.bounds.top;

	/* For bttv_buffer_activate_vbi(). */
	buf->geo.vdelay = min_vdelay;

	buf->vb.state = VIDEOBUF_PREPARED;
	buf->vb.field = field;
	dprintk("buf prepare %p: top=%p bottom=%p field=%s\n",
		vb, &buf->top, &buf->bottom,
		v4l2_field_names[buf->vb.field]);
	return 0;

 fail:
	bttv_dma_free(q,btv,buf);
	return rc;
}

static void
vbi_buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct bttv_fh *fh = q->priv_data;
	struct bttv *btv = fh->btv;
	struct bttv_buffer *buf = container_of(vb,struct bttv_buffer,vb);

	dprintk("queue %p\n",vb);
	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue,&btv->vcapture);
	if (NULL == btv->cvbi) {
		fh->btv->loop_irq |= 4;
		bttv_set_dma(btv,0x0c);
	}
}

static void vbi_buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct bttv_fh *fh = q->priv_data;
	struct bttv *btv = fh->btv;
	struct bttv_buffer *buf = container_of(vb,struct bttv_buffer,vb);

	dprintk("free %p\n",vb);
	bttv_dma_free(q,fh->btv,buf);
}

struct videobuf_queue_ops bttv_vbi_qops = {
	.buf_setup    = vbi_buffer_setup,
	.buf_prepare  = vbi_buffer_prepare,
	.buf_queue    = vbi_buffer_queue,
	.buf_release  = vbi_buffer_release,
};

/* ----------------------------------------------------------------------- */

static int try_fmt(struct v4l2_vbi_format *f, const struct bttv_tvnorm *tvnorm,
			__s32 crop_start)
{
	__s32 min_start, max_start, max_end, f2_offset;
	unsigned int i;

	/* For compatibility with earlier driver versions we must pretend
	   the VBI and video capture window may overlap. In reality RISC
	   magic aborts VBI capturing at the first line of video capturing,
	   leaving the rest of the buffer unchanged, usually all zero.
	   VBI capturing must always start before video capturing. >> 1
	   because cropping counts field lines times two. */
	min_start = tvnorm->vbistart[0];
	max_start = (crop_start >> 1) - 1;
	max_end = (tvnorm->cropcap.bounds.top
		   + tvnorm->cropcap.bounds.height) >> 1;

	if (min_start > max_start)
		return -EBUSY;

	BUG_ON(max_start >= max_end);

	f->sampling_rate    = tvnorm->Fsc;
	f->samples_per_line = VBI_BPL;
	f->sample_format    = V4L2_PIX_FMT_GREY;
	f->offset           = VBI_OFFSET;

	f2_offset = tvnorm->vbistart[1] - tvnorm->vbistart[0];

	for (i = 0; i < 2; ++i) {
		if (0 == f->count[i]) {
			/* No data from this field. We leave f->start[i]
			   alone because VIDIOCSVBIFMT is w/o and EINVALs
			   when a driver does not support exactly the
			   requested parameters. */
		} else {
			s64 start, count;

			start = clamp(f->start[i], min_start, max_start);
			/* s64 to prevent overflow. */
			count = (s64) f->start[i] + f->count[i] - start;
			f->start[i] = start;
			f->count[i] = clamp(count, (s64) 1,
					    max_end - start);
		}

		min_start += f2_offset;
		max_start += f2_offset;
		max_end += f2_offset;
	}

	if (0 == (f->count[0] | f->count[1])) {
		/* As in earlier driver versions. */
		f->start[0] = tvnorm->vbistart[0];
		f->start[1] = tvnorm->vbistart[1];
		f->count[0] = 1;
		f->count[1] = 1;
	}

	f->flags = 0;

	f->reserved[0] = 0;
	f->reserved[1] = 0;

	return 0;
}

int bttv_try_fmt_vbi(struct file *file, void *f, struct v4l2_format *frt)
{
	struct bttv_fh *fh = f;
	struct bttv *btv = fh->btv;
	const struct bttv_tvnorm *tvnorm;
	__s32 crop_start;

	mutex_lock(&btv->lock);

	tvnorm = &bttv_tvnorms[btv->tvnorm];
	crop_start = btv->crop_start;

	mutex_unlock(&btv->lock);

	return try_fmt(&frt->fmt.vbi, tvnorm, crop_start);
}


int bttv_s_fmt_vbi(struct file *file, void *f, struct v4l2_format *frt)
{
	struct bttv_fh *fh = f;
	struct bttv *btv = fh->btv;
	const struct bttv_tvnorm *tvnorm;
	__s32 start1, end;
	int rc;

	mutex_lock(&btv->lock);

	rc = -EBUSY;
	if (fh->resources & RESOURCE_VBI)
		goto fail;

	tvnorm = &bttv_tvnorms[btv->tvnorm];

	rc = try_fmt(&frt->fmt.vbi, tvnorm, btv->crop_start);
	if (0 != rc)
		goto fail;

	start1 = frt->fmt.vbi.start[1] - tvnorm->vbistart[1] +
		tvnorm->vbistart[0];

	/* First possible line of video capturing. Should be
	   max(f->start[0] + f->count[0], start1 + f->count[1]) * 2
	   when capturing both fields. But for compatibility we must
	   pretend the VBI and video capture window may overlap,
	   so end = start + 1, the lowest possible value, times two
	   because vbi_fmt.end counts field lines times two. */
	end = max(frt->fmt.vbi.start[0], start1) * 2 + 2;

	mutex_lock(&fh->vbi.vb_lock);

	fh->vbi_fmt.fmt    = frt->fmt.vbi;
	fh->vbi_fmt.tvnorm = tvnorm;
	fh->vbi_fmt.end    = end;

	mutex_unlock(&fh->vbi.vb_lock);

	rc = 0;

 fail:
	mutex_unlock(&btv->lock);

	return rc;
}


int bttv_g_fmt_vbi(struct file *file, void *f, struct v4l2_format *frt)
{
	struct bttv_fh *fh = f;
	const struct bttv_tvnorm *tvnorm;

	frt->fmt.vbi = fh->vbi_fmt.fmt;

	tvnorm = &bttv_tvnorms[fh->btv->tvnorm];

	if (tvnorm != fh->vbi_fmt.tvnorm) {
		__s32 max_end;
		unsigned int i;

		/* As in vbi_buffer_prepare() this imitates the
		   behaviour of earlier driver versions after video
		   standard changes, with default parameters anyway. */

		max_end = (tvnorm->cropcap.bounds.top
			   + tvnorm->cropcap.bounds.height) >> 1;

		frt->fmt.vbi.sampling_rate = tvnorm->Fsc;

		for (i = 0; i < 2; ++i) {
			__s32 new_start;

			new_start = frt->fmt.vbi.start[i]
				+ tvnorm->vbistart[i]
				- fh->vbi_fmt.tvnorm->vbistart[i];

			frt->fmt.vbi.start[i] = min(new_start, max_end - 1);
			frt->fmt.vbi.count[i] =
				min((__s32) frt->fmt.vbi.count[i],
					  max_end - frt->fmt.vbi.start[i]);

			max_end += tvnorm->vbistart[1]
				- tvnorm->vbistart[0];
		}
	}
	return 0;
}

void bttv_vbi_fmt_reset(struct bttv_vbi_fmt *f, int norm)
{
	const struct bttv_tvnorm *tvnorm;
	unsigned int real_samples_per_line;
	unsigned int real_count;

	tvnorm = &bttv_tvnorms[norm];

	f->fmt.sampling_rate    = tvnorm->Fsc;
	f->fmt.samples_per_line = VBI_BPL;
	f->fmt.sample_format    = V4L2_PIX_FMT_GREY;
	f->fmt.offset           = VBI_OFFSET;
	f->fmt.start[0]		= tvnorm->vbistart[0];
	f->fmt.start[1]		= tvnorm->vbistart[1];
	f->fmt.count[0]		= VBI_DEFLINES;
	f->fmt.count[1]		= VBI_DEFLINES;
	f->fmt.flags            = 0;
	f->fmt.reserved[0]      = 0;
	f->fmt.reserved[1]      = 0;

	/* For compatibility the buffer size must be 2 * VBI_DEFLINES *
	   VBI_BPL regardless of the current video standard. */
	real_samples_per_line   = 1024 + tvnorm->vbipack * 4;
	real_count              = ((tvnorm->cropcap.defrect.top >> 1)
				   - tvnorm->vbistart[0]);

	BUG_ON(real_samples_per_line > VBI_BPL);
	BUG_ON(real_count > VBI_DEFLINES);

	f->tvnorm               = tvnorm;

	/* See bttv_vbi_fmt_set(). */
	f->end                  = tvnorm->vbistart[0] * 2 + 2;
}

/* ----------------------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
