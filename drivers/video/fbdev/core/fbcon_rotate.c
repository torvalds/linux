/*
 *  linux/drivers/video/console/fbcon_rotate.c -- Software Rotation
 *
 *      Copyright (C) 2005 Antonino Daplas <adaplas @pol.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/font.h>

#include "fbcon.h"
#include "fbcon_rotate.h"

int fbcon_rotate_font(struct fb_info *info, struct vc_data *vc)
{
	struct fbcon_par *par = info->fbcon_par;
	unsigned char *buf;
	int ret;

	if (par->p->fontdata == par->rotated.fontdata && par->rotate == par->rotated.buf_rotate)
		return 0;

	par->rotated.fontdata = par->p->fontdata;
	par->rotated.buf_rotate = par->rotate;

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	buf = font_data_rotate(par->rotated.fontdata, vc->vc_font.width,
			       vc->vc_font.height, vc->vc_font.charcount,
			       par->rotated.buf_rotate, par->rotated.buf,
			       &par->rotated.bufsize);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		goto err_kfree;
	}

	par->rotated.buf = buf;

	return 0;

err_kfree:
	kfree(par->rotated.buf);
	par->rotated.buf = NULL; /* clear here to avoid output */
	par->rotated.bufsize = 0;

	return ret;
}
