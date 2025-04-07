// SPDX-License-Identifier: GPL-2.0-only
/*
 *	Copyright (C)  2025 Zsolt Kajtar (soci@c64.rulez.org)
 */
#include <linux/module.h>
#include <linux/fb.h>
#include <linux/bitrev.h>
#include <asm/types.h>

#ifdef CONFIG_FB_SYS_REV_PIXELS_IN_BYTE
#define FB_REV_PIXELS_IN_BYTE
#endif

#include "sysmem.h"
#include "fb_imageblit.h"

void sys_imageblit(struct fb_info *p, const struct fb_image *image)
{
	if (!(p->flags & FBINFO_VIRTFB))
		fb_warn_once(p, "%s: framebuffer is not in virtual address space.\n", __func__);

	fb_imageblit(p, image);
}
EXPORT_SYMBOL(sys_imageblit);

MODULE_AUTHOR("Zsolt Kajtar <soci@c64.rulez.org>");
MODULE_DESCRIPTION("Virtual memory packed pixel framebuffer image draw");
MODULE_LICENSE("GPL");
