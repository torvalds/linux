// SPDX-License-Identifier: GPL-2.0-only
/*
 *	Copyright (C)  2025 Zsolt Kajtar (soci@c64.rulez.org)
 */
#include <linux/module.h>
#include <linux/fb.h>
#include <linux/bitrev.h>
#include <asm/types.h>

#ifdef CONFIG_FB_CFB_REV_PIXELS_IN_BYTE
#define FB_REV_PIXELS_IN_BYTE
#endif

#include "cfbmem.h"
#include "fb_copyarea.h"

void cfb_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	if (p->state != FBINFO_STATE_RUNNING)
		return;

	if (p->flags & FBINFO_VIRTFB)
		fb_warn_once(p, "%s: framebuffer is not in I/O address space.\n", __func__);

	if (p->fbops->fb_sync)
		p->fbops->fb_sync(p);

	fb_copyarea(p, area);
}
EXPORT_SYMBOL(cfb_copyarea);

MODULE_AUTHOR("Zsolt Kajtar <soci@c64.rulez.org>");
MODULE_DESCRIPTION("I/O memory packed pixel framebuffer area copy");
MODULE_LICENSE("GPL");
