/*
 *  linux/drivers/video/console/fbcon_rotate.h -- Software Display Rotation
 *
 *	Copyright (C) 2005 Antonino Daplas <adaplas@pol.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _FBCON_ROTATE_H
#define _FBCON_ROTATE_H

#define GETVYRES(s,i) ({                           \
        (fb_scrollmode(s) == SCROLL_REDRAW || fb_scrollmode(s) == SCROLL_MOVE) ? \
        (i)->var.yres : (i)->var.yres_virtual; })

#define GETVXRES(s,i) ({                           \
        (fb_scrollmode(s) == SCROLL_REDRAW || fb_scrollmode(s) == SCROLL_MOVE || !(i)->fix.xpanstep) ? \
        (i)->var.xres : (i)->var.xres_virtual; })

int fbcon_rotate_font(struct fb_info *info, struct vc_data *vc);

#if defined(CONFIG_FRAMEBUFFER_CONSOLE_ROTATION)
void fbcon_set_bitops_cw(struct fbcon_par *par);
void fbcon_set_bitops_ud(struct fbcon_par *par);
void fbcon_set_bitops_ccw(struct fbcon_par *par);
#else
static inline void fbcon_set_bitops_cw(struct fbcon_par *par)
{ }
static inline void fbcon_set_bitops_ud(struct fbcon_par *par)
{ }
static inline void fbcon_set_bitops_ccw(struct fbcon_par *par)
{ }
#endif

#endif
