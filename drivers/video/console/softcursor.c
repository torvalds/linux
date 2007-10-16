/*
 * linux/drivers/video/console/softcursor.c
 *
 * Generic software cursor for frame buffer devices
 *
 *  Created 14 Nov 2002 by James Simmons
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/slab.h>

#include <asm/io.h>

#include "fbcon.h"

int soft_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct fbcon_ops *ops = info->fbcon_par;
	unsigned int scan_align = info->pixmap.scan_align - 1;
	unsigned int buf_align = info->pixmap.buf_align - 1;
	unsigned int i, size, dsize, s_pitch, d_pitch;
	struct fb_image *image;
	u8 *src, *dst;

	if (info->state != FBINFO_STATE_RUNNING)
		return 0;

	s_pitch = (cursor->image.width + 7) >> 3;
	dsize = s_pitch * cursor->image.height;

	if (dsize + sizeof(struct fb_image) != ops->cursor_size) {
		if (ops->cursor_src != NULL)
			kfree(ops->cursor_src);
		ops->cursor_size = dsize + sizeof(struct fb_image);

		ops->cursor_src = kmalloc(ops->cursor_size, GFP_ATOMIC);
		if (!ops->cursor_src) {
			ops->cursor_size = 0;
			return -ENOMEM;
		}
	}

	src = ops->cursor_src + sizeof(struct fb_image);
	image = (struct fb_image *)ops->cursor_src;
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
	return 0;
}

EXPORT_SYMBOL(soft_cursor);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("Generic software cursor");
MODULE_LICENSE("GPL");
