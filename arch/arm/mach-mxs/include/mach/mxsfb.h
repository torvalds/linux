/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef __MACH_FB_H
#define __MACH_FB_H

#include <linux/fb.h>

#define STMLCDIF_8BIT 1	/** pixel data bus to the display is of 8 bit width */
#define STMLCDIF_16BIT 0 /** pixel data bus to the display is of 16 bit width */
#define STMLCDIF_18BIT 2 /** pixel data bus to the display is of 18 bit width */
#define STMLCDIF_24BIT 3 /** pixel data bus to the display is of 24 bit width */

#define FB_SYNC_DATA_ENABLE_HIGH_ACT	(1 << 6)
#define FB_SYNC_DOTCLK_FAILING_ACT	(1 << 7) /* failing/negtive edge sampling */

struct mxsfb_platform_data {
	struct fb_videomode *mode_list;
	unsigned mode_count;

	unsigned default_bpp;

	unsigned dotclk_delay;	/* refer manual HW_LCDIF_VDCTRL4 register */
	unsigned ld_intf_width;	/* refer STMLCDIF_* macros */

	unsigned fb_size;	/* Size of the video memory. If zero a
				 * default will be used
				 */
	unsigned long fb_phys;	/* physical address for the video memory. If
				 * zero the framebuffer memory will be dynamically
				 * allocated. If specified,fb_size must also be specified.
				 * fb_phys must be unused by Linux.
				 */
};

#endif /* __MACH_FB_H */
