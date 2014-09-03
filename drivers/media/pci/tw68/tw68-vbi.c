/*
 *  tw68_controls.c
 *  Part of the device driver for Techwell 68xx based cards
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack <wbrack@mmm.com.hk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "tw68.h"

static int buffer_setup(struct videobuf_queue *q, unsigned int *count,
			unsigned int *size) {
	printk(KERN_INFO "%s: shouldn't be here!\n", __func__);
	return 0;
}
static int buffer_prepare(struct videobuf_queue *q,
			  struct videobuf_buffer *vb,
			  enum v4l2_field field)
{
	printk(KERN_INFO "%s: shouldn't be here!\n", __func__);
	return 0;
}
static void buffer_queue(struct videobuf_queue *q,
			 struct videobuf_buffer *vb)
{
	printk(KERN_INFO "%s: shouldn't be here!\n", __func__);
}
static void buffer_release(struct videobuf_queue *q,
			   struct videobuf_buffer *vb)
{
	printk(KERN_INFO "%s: shouldn't be here!\n", __func__);
}
struct videobuf_queue_ops tw68_vbi_qops = {
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

/* ------------------------------------------------------------------ */

int tw68_vbi_init1(struct tw68_dev *dev)
{
	return 0;
}

int tw68_vbi_fini(struct tw68_dev *dev)
{
	return 0;
}

void tw68_irq_vbi_done(struct tw68_dev *dev, unsigned long status)
{
	return;
}

