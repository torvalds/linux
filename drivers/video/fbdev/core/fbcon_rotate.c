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
	unsigned char *fontbuffer;
	int ret;

	if (vc->vc_font.data == par->fontdata &&
	    par->p->con_rotate == par->cur_rotate)
		return 0;

	par->fontdata = vc->vc_font.data;
	par->cur_rotate = par->p->con_rotate;

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	fontbuffer = font_data_rotate(par->p->fontdata, vc->vc_font.width,
				      vc->vc_font.height, vc->vc_font.charcount,
				      par->rotate, par->fontbuffer, &par->fd_size);
	if (IS_ERR(fontbuffer)) {
		ret = PTR_ERR(fontbuffer);
		goto err_kfree;
	}

	par->fontbuffer = fontbuffer;

	return 0;

err_kfree:
	kfree(par->fontbuffer);
	par->fontbuffer = NULL; /* clear here to avoid output */

	return ret;
}
