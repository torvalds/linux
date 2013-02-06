/* linux/drivers/media/video/samsung/tvout/s5p_tvout_fb.h
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * frame buffer header file. file for Samsung TVOUT driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _LINUX_S5P_TVOUT_FB_H_
#define _LINUX_S5P_TVOUT_FB_H_

#include <linux/fb.h>

extern int s5p_tvout_fb_alloc_framebuffer(struct device *dev_fb);
extern int s5p_tvout_fb_register_framebuffer(struct device *dev_fb);

#endif /* _LINUX_S5P_TVOUT_FB_H_ */
