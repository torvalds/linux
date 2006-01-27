/*
 * linux/drivers/video/softcursor.c -- Generic software cursor for frame buffer devices
 *
 *  Created 14 Nov 2002 by James Simmons
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include "fbcon.h"

int soft_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	unsigned int scan_align = info->pixmap.scan_align - 1;
	unsigned int buf_align = info->pixmap.buf_align - 1;
	unsigned int i, size, dsize, s_pitch, d_pitch;
	struct fb_image *image;
	u8 *dst, *src;

	if (info->state != FBINFO_STATE_RUNNING)
		return 0;

	s_pitch = (cursor->image.width + 7) >> 3;
	dsize = s_pitch * cursor->image.height;

	src = kmalloc(dsize + sizeof(struct fb_image), GFP_ATOMIC);
	if (!src)
		return -ENOMEM;

	image = (struct fb_image *) (src + dsize);
	*image = cursor->image;
	d_pitch = (s_pitch + scan_align) & ~scan_align;

	size = d_pitch * image->height + buf_align;
	size &= ~buf_align;
	dst = fb_get_buffer_offset(info, &info->pixmap, size);

	if (cursor->enable) {
		switch (cursor->rop) {
		case ROP_XOR:
			for (i = 0; i < dsize; i++)
				src[i] = image->data[i] ^ cursor->mask[i];
			break;
		case ROP_COPY:
		default:
			for (i = 0; i < dsize; i++)
				src[i] = image->data[i] & cursor->mask[i];
			break;
		}
	} else
		memcpy(src, image->data, dsize);

	fb_pad_aligned_buffer(dst, d_pitch, src, s_pitch, image->height);
	image->data = dst;
	info->fbops->fb_imageblit(info, image);
	kfree(src);
	return 0;
}

EXPORT_SYMBOL(soft_cursor);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software cursor");
MODULE_LICENSE("GPL");
