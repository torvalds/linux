/* Copyright (c) 2008-2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>

#include <mach/hardware.h>
#include <asm/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>

#include <linux/fb.h>

#include "mdp.h"
#include "msm_fb.h"

static int cursor_enabled;

int mdp_hw_cursor_update(struct fb_info *info, struct fb_cursor *cursor)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct fb_image *img = &cursor->image;
	int calpha_en, transp_en;
	int alpha;
	int ret = 0;

	if ((img->width > MDP_CURSOR_WIDTH) ||
	    (img->height > MDP_CURSOR_HEIGHT) ||
	    (img->depth != 32))
		return -EINVAL;

	if (cursor->set & FB_CUR_SETPOS)
		MDP_OUTP(MDP_BASE + 0x9004c, (img->dy << 16) | img->dx);

	if (cursor->set & FB_CUR_SETIMAGE) {
		ret = copy_from_user(mfd->cursor_buf, img->data,
					img->width*img->height*4);
		if (ret)
			return ret;

		if (img->bg_color == 0xffffffff)
			transp_en = 0;
		else
			transp_en = 1;

		alpha = (img->fg_color & 0xff000000) >> 24;

		if (alpha)
			calpha_en = 0x2; /* xrgb */
		else
			calpha_en = 0x1; /* argb */

		MDP_OUTP(MDP_BASE + 0x90044, (img->height << 16) | img->width);
		MDP_OUTP(MDP_BASE + 0x90048, mfd->cursor_buf_phys);
		/* order the writes the cursor_buf before updating the
		 * hardware */
//		dma_coherent_pre_ops();
		MDP_OUTP(MDP_BASE + 0x90060,
			 (transp_en << 3) | (calpha_en << 1) |
			 (inp32(MDP_BASE + 0x90060) & 0x1));
#ifdef CONFIG_FB_MSM_MDP40
		MDP_OUTP(MDP_BASE + 0x90064, (alpha << 24));
		MDP_OUTP(MDP_BASE + 0x90068, (0xffffff & img->bg_color));
		MDP_OUTP(MDP_BASE + 0x9006C, (0xffffff & img->bg_color));
#else
		MDP_OUTP(MDP_BASE + 0x90064,
			 (alpha << 24) | (0xffffff & img->bg_color));
		MDP_OUTP(MDP_BASE + 0x90068, 0);
#endif
	}

	if ((cursor->enable) && (!cursor_enabled)) {
		cursor_enabled = 1;
		MDP_OUTP(MDP_BASE + 0x90060, inp32(MDP_BASE + 0x90060) | 0x1);
	} else if ((!cursor->enable) && (cursor_enabled)) {
		cursor_enabled = 0;
		MDP_OUTP(MDP_BASE + 0x90060,
			 inp32(MDP_BASE + 0x90060) & (~0x1));
	}

	return 0;
}
