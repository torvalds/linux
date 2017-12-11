/*
   au0828-vbi.c - VBI driver for au0828

   Copyright (C) 2010 Devin Heitmueller <dheitmueller@kernellabs.com>

   This work was sponsored by GetWellNetwork Inc.

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

#include "au0828.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>

/* ------------------------------------------------------------------ */

static int vbi_queue_setup(struct vb2_queue *vq,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct au0828_dev *dev = vb2_get_drv_priv(vq);
	unsigned long size = dev->vbi_width * dev->vbi_height * 2;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = size;
	return 0;
}

static int vbi_buffer_prepare(struct vb2_buffer *vb)
{
	struct au0828_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size;

	size = dev->vbi_width * dev->vbi_height * 2;

	if (vb2_plane_size(vb, 0) < size) {
		pr_err("%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}
	vb2_set_plane_payload(vb, 0, size);

	return 0;
}

static void
vbi_buffer_queue(struct vb2_buffer *vb)
{
	struct au0828_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct au0828_buffer *buf =
			container_of(vbuf, struct au0828_buffer, vb);
	struct au0828_dmaqueue *vbiq = &dev->vbiq;
	unsigned long flags = 0;

	buf->mem = vb2_plane_vaddr(vb, 0);
	buf->length = vb2_plane_size(vb, 0);

	spin_lock_irqsave(&dev->slock, flags);
	list_add_tail(&buf->list, &vbiq->active);
	spin_unlock_irqrestore(&dev->slock, flags);
}

const struct vb2_ops au0828_vbi_qops = {
	.queue_setup     = vbi_queue_setup,
	.buf_prepare     = vbi_buffer_prepare,
	.buf_queue       = vbi_buffer_queue,
	.start_streaming = au0828_start_analog_streaming,
	.stop_streaming  = au0828_stop_vbi_streaming,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
};
