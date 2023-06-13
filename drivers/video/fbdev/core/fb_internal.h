/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _FB_INTERNAL_H
#define _FB_INTERNAL_H

struct fb_info;

/* fbsysfs.c */
int fb_device_create(struct fb_info *fb_info);
void fb_device_destroy(struct fb_info *fb_info);

#endif
