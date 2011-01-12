/*
   em28xx-vbi.c - VBI driver for em28xx

   Copyright (C) 2009 Devin Heitmueller <dheitmueller@kernellabs.com>

   This work was sponsored by EyeMagnet Limited.

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/init.h>

#include "em28xx.h"

static unsigned int vbibufs = 5;
module_param(vbibufs, int, 0644);
MODULE_PARM_DESC(vbibufs, "number of vbi buffers, range 2-32");

static unsigned int vbi_debug;
module_param(vbi_debug, int, 0644);
MODULE_PARM_DESC(vbi_debug, "enable debug messages [vbi]");

#define dprintk(level, fmt, arg...)	if (vbi_debug >= level) \
	printk(KERN_DEBUG "%s: " fmt, dev->core->name , ## arg)

/* ------------------------------------------------------------------ */

static void
free_buffer(struct videobuf_queue *vq, struct em28xx_buffer *buf)
{
	struct em28xx_fh     *fh  = vq->priv_data;
	struct em28xx        *dev = fh->dev;
	unsigned long flags = 0;
	if (in_interrupt())
		BUG();

	/* We used to wait for the buffer to finish here, but this didn't work
	   because, as we were keeping the state as VIDEOBUF_QUEUED,
	   videobuf_queue_cancel marked it as finished for us.
	   (Also, it could wedge forever if the hardware was misconfigured.)

	   This should be safe; by the time we get here, the buffer isn't
	   queued anymore. If we ever start marking the buffers as
	   VIDEOBUF_ACTIVE, it won't be, though.
	*/
	spin_lock_irqsave(&dev->slock, flags);
	if (dev->isoc_ctl.vbi_buf == buf)
		dev->isoc_ctl.vbi_buf = NULL;
	spin_unlock_irqrestore(&dev->slock, flags);

	videobuf_vmalloc_free(&buf->vb);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int
vbi_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	struct em28xx_fh     *fh  = q->priv_data;
	struct em28xx        *dev = fh->dev;

	*size = dev->vbi_width * dev->vbi_height * 2;

	if (0 == *count)
		*count = vbibufs;
	if (*count < 2)
		*count = 2;
	if (*count > 32)
		*count = 32;
	return 0;
}

static int
vbi_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
	    enum v4l2_field field)
{
	struct em28xx_fh     *fh  = q->priv_data;
	struct em28xx        *dev = fh->dev;
	struct em28xx_buffer *buf = container_of(vb, struct em28xx_buffer, vb);
	int                  rc = 0;

	buf->vb.size = dev->vbi_width * dev->vbi_height * 2;

	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
		return -EINVAL;

	buf->vb.width  = dev->vbi_width;
	buf->vb.height = dev->vbi_height;
	buf->vb.field  = field;

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		rc = videobuf_iolock(q, &buf->vb, NULL);
		if (rc < 0)
			goto fail;
	}

	buf->vb.state = VIDEOBUF_PREPARED;
	return 0;

fail:
	free_buffer(q, buf);
	return rc;
}

static void
vbi_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	struct em28xx_buffer    *buf     = container_of(vb,
							struct em28xx_buffer,
							vb);
	struct em28xx_fh        *fh      = vq->priv_data;
	struct em28xx           *dev     = fh->dev;
	struct em28xx_dmaqueue  *vbiq    = &dev->vbiq;

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &vbiq->active);
}

static void vbi_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct em28xx_buffer *buf = container_of(vb, struct em28xx_buffer, vb);
	free_buffer(q, buf);
}

struct videobuf_queue_ops em28xx_vbi_qops = {
	.buf_setup    = vbi_setup,
	.buf_prepare  = vbi_prepare,
	.buf_queue    = vbi_queue,
	.buf_release  = vbi_release,
};
