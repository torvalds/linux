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
#include "em28xx-v4l.h"

/* ------------------------------------------------------------------ */

static int vbi_queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], void *alloc_ctxs[])
{
	struct em28xx *dev = vb2_get_drv_priv(vq);
	struct em28xx_v4l2 *v4l2 = dev->v4l2;
	unsigned long size;

	if (fmt)
		size = fmt->fmt.pix.sizeimage;
	else
		size = v4l2->vbi_width * v4l2->vbi_height * 2;

	if (0 == *nbuffers)
		*nbuffers = 32;
	if (*nbuffers < 2)
		*nbuffers = 2;
	if (*nbuffers > 32)
		*nbuffers = 32;

	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static int vbi_buffer_prepare(struct vb2_buffer *vb)
{
	struct em28xx        *dev  = vb2_get_drv_priv(vb->vb2_queue);
	struct em28xx_v4l2   *v4l2 = dev->v4l2;
	struct em28xx_buffer *buf  = container_of(vb, struct em28xx_buffer, vb);
	unsigned long        size;

	size = v4l2->vbi_width * v4l2->vbi_height * 2;

	if (vb2_plane_size(vb, 0) < size) {
		printk(KERN_INFO "%s data will not fit into plane (%lu < %lu)\n",
		       __func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	vb2_set_plane_payload(&buf->vb, 0, size);

	return 0;
}

static void
vbi_buffer_queue(struct vb2_buffer *vb)
{
	struct em28xx *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct em28xx_buffer *buf = container_of(vb, struct em28xx_buffer, vb);
	struct em28xx_dmaqueue *vbiq = &dev->vbiq;
	unsigned long flags = 0;

	buf->mem = vb2_plane_vaddr(vb, 0);
	buf->length = vb2_plane_size(vb, 0);

	spin_lock_irqsave(&dev->slock, flags);
	list_add_tail(&buf->list, &vbiq->active);
	spin_unlock_irqrestore(&dev->slock, flags);
}


struct vb2_ops em28xx_vbi_qops = {
	.queue_setup    = vbi_queue_setup,
	.buf_prepare    = vbi_buffer_prepare,
	.buf_queue      = vbi_buffer_queue,
	.start_streaming = em28xx_start_analog_streaming,
	.stop_streaming = em28xx_stop_vbi_streaming,
	.wait_prepare   = vb2_ops_wait_prepare,
	.wait_finish    = vb2_ops_wait_finish,
};
