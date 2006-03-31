/*

    bttv - Bt848 frame grabber driver
    vbi interface

    (c) 2002 Gerd Knorr <kraxel@bytesex.org>

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
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <asm/io.h>
#include "bttvp.h"

/* Offset from line sync pulse leading edge (0H) in 1 / sampling_rate:
   bt8x8 /HRESET pulse starts at 0H and has length 64 / fCLKx1 (E|O_VTC
   HSFMT = 0). VBI_HDELAY (always 0) is an offset from the trailing edge
   of /HRESET in 1 / fCLKx1, and the sampling_rate tvnorm->Fsc is fCLKx2. */
#define VBI_OFFSET ((64 + 0) * 2)

#define VBI_DEFLINES 16
#define VBI_MAXLINES 32

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

/* ----------------------------------------------------------------------- */
/* vbi risc code + mm                                                      */

static int
vbi_buffer_risc(struct bttv *btv, struct bttv_buffer *buf, int lines)
{
	int bpl = 2048;

	bttv_risc_packed(btv, &buf->top, buf->vb.dma.sglist,
			 0, bpl-4, 4, lines);
	bttv_risc_packed(btv, &buf->bottom, buf->vb.dma.sglist,
			 lines * bpl, bpl-4, 4, lines);
	return 0;
}

static int vbi_buffer_setup(struct videobuf_queue *q,
			    unsigned int *count, unsigned int *size)
{
	struct bttv_fh *fh = q->priv_data;
	struct bttv *btv = fh->btv;

	if (0 == *count)
		*count = vbibufs;
	*size = fh->lines * 2 * 2048;
	dprintk("setup: lines=%d\n",fh->lines);
	return 0;
}

static int vbi_buffer_prepare(struct videobuf_queue *q,
			      struct videobuf_buffer *vb,
			      enum v4l2_field field)
{
	struct bttv_fh *fh = q->priv_data;
	struct bttv *btv = fh->btv;
	struct bttv_buffer *buf = container_of(vb,struct bttv_buffer,vb);
	int rc;

	buf->vb.size = fh->lines * 2 * 2048;
	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	if (STATE_NEEDS_INIT == buf->vb.state) {
		if (0 != (rc = videobuf_iolock(q, &buf->vb, NULL)))
			goto fail;
		if (0 != (rc = vbi_buffer_risc(btv,buf,fh->lines)))
			goto fail;
	}
	buf->vb.state = STATE_PREPARED;
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
	buf->vb.state = STATE_QUEUED;
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
	bttv_dma_free(&fh->cap,fh->btv,buf);
}

struct videobuf_queue_ops bttv_vbi_qops = {
	.buf_setup    = vbi_buffer_setup,
	.buf_prepare  = vbi_buffer_prepare,
	.buf_queue    = vbi_buffer_queue,
	.buf_release  = vbi_buffer_release,
};

/* ----------------------------------------------------------------------- */

void bttv_vbi_setlines(struct bttv_fh *fh, struct bttv *btv, int lines)
{
	int vdelay;

	if (lines < 1)
		lines = 1;
	if (lines > VBI_MAXLINES)
		lines = VBI_MAXLINES;
	fh->lines = lines;

	vdelay = btread(BT848_E_VDELAY_LO);
	if (vdelay < lines*2) {
		vdelay = lines*2;
		btwrite(vdelay,BT848_E_VDELAY_LO);
		btwrite(vdelay,BT848_O_VDELAY_LO);
	}
}

void bttv_vbi_try_fmt(struct bttv_fh *fh, struct v4l2_format *f)
{
	const struct bttv_tvnorm *tvnorm;
	s64 count0,count1,count;

	tvnorm = &bttv_tvnorms[fh->btv->tvnorm];
	f->type = V4L2_BUF_TYPE_VBI_CAPTURE;
	f->fmt.vbi.sampling_rate    = tvnorm->Fsc;
	f->fmt.vbi.samples_per_line = 2048;
	f->fmt.vbi.sample_format    = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset           = VBI_OFFSET;
	f->fmt.vbi.flags            = 0;

	/* s64 to prevent overflow. */
	count0 = (s64) f->fmt.vbi.start[0] + f->fmt.vbi.count[0]
		- tvnorm->vbistart[0];
	count1 = (s64) f->fmt.vbi.start[1] + f->fmt.vbi.count[1]
		- tvnorm->vbistart[1];
	count  = clamp (max (count0, count1), 1LL, (s64) VBI_MAXLINES);

	f->fmt.vbi.start[0] = tvnorm->vbistart[0];
	f->fmt.vbi.start[1] = tvnorm->vbistart[1];
	f->fmt.vbi.count[0] = count;
	f->fmt.vbi.count[1] = count;

	f->fmt.vbi.reserved[0] = 0;
	f->fmt.vbi.reserved[1] = 0;
}

void bttv_vbi_get_fmt(struct bttv_fh *fh, struct v4l2_format *f)
{
	const struct bttv_tvnorm *tvnorm;

	tvnorm = &bttv_tvnorms[fh->btv->tvnorm];
	memset(f,0,sizeof(*f));
	f->type = V4L2_BUF_TYPE_VBI_CAPTURE;
	f->fmt.vbi.sampling_rate    = tvnorm->Fsc;
	f->fmt.vbi.samples_per_line = 2048;
	f->fmt.vbi.sample_format    = V4L2_PIX_FMT_GREY;
	f->fmt.vbi.offset           = VBI_OFFSET;
	f->fmt.vbi.start[0]         = tvnorm->vbistart[0];
	f->fmt.vbi.start[1]         = tvnorm->vbistart[1];
	f->fmt.vbi.count[0]         = fh->lines;
	f->fmt.vbi.count[1]         = fh->lines;
	f->fmt.vbi.flags            = 0;
}

/* ----------------------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
