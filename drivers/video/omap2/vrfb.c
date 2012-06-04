/*
 * VRFB Rotation Engine
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*#define DEBUG*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/mutex.h>

#include <plat/vrfb.h>
#include <plat/sdrc.h>

#ifdef DEBUG
#define DBG(format, ...) pr_debug("VRFB: " format, ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

#define SMS_ROT_VIRT_BASE(context, rot) \
	(((context >= 4) ? 0xD0000000 : 0x70000000) \
	 + (0x4000000 * (context)) \
	 + (0x1000000 * (rot)))

#define OMAP_VRFB_SIZE			(2048 * 2048 * 4)

#define VRFB_PAGE_WIDTH_EXP	5 /* Assuming SDRAM pagesize= 1024 */
#define VRFB_PAGE_HEIGHT_EXP	5 /* 1024 = 2^5 * 2^5 */
#define VRFB_PAGE_WIDTH		(1 << VRFB_PAGE_WIDTH_EXP)
#define VRFB_PAGE_HEIGHT	(1 << VRFB_PAGE_HEIGHT_EXP)
#define SMS_IMAGEHEIGHT_OFFSET	16
#define SMS_IMAGEWIDTH_OFFSET	0
#define SMS_PH_OFFSET		8
#define SMS_PW_OFFSET		4
#define SMS_PS_OFFSET		0

#define VRFB_NUM_CTXS 12
/* bitmap of reserved contexts */
static unsigned long ctx_map;

static DEFINE_MUTEX(ctx_lock);

/*
 * Access to this happens from client drivers or the PM core after wake-up.
 * For the first case we require locking at the driver level, for the second
 * we don't need locking, since no drivers will run until after the wake-up
 * has finished.
 */
static struct {
	u32 physical_ba;
	u32 control;
	u32 size;
} vrfb_hw_context[VRFB_NUM_CTXS];

static inline void restore_hw_context(int ctx)
{
	omap2_sms_write_rot_control(vrfb_hw_context[ctx].control, ctx);
	omap2_sms_write_rot_size(vrfb_hw_context[ctx].size, ctx);
	omap2_sms_write_rot_physical_ba(vrfb_hw_context[ctx].physical_ba, ctx);
}

static u32 get_image_width_roundup(u16 width, u8 bytespp)
{
	unsigned long stride = width * bytespp;
	unsigned long ceil_pages_per_stride = (stride / VRFB_PAGE_WIDTH) +
		(stride % VRFB_PAGE_WIDTH != 0);

	return ceil_pages_per_stride * VRFB_PAGE_WIDTH / bytespp;
}

/*
 * This the extra space needed in the VRFB physical area for VRFB to safely wrap
 * any memory accesses to the invisible part of the virtual view to the physical
 * area.
 */
static inline u32 get_extra_physical_size(u16 image_width_roundup, u8 bytespp)
{
	return (OMAP_VRFB_LINE_LEN - image_width_roundup) * VRFB_PAGE_HEIGHT *
		bytespp;
}

void omap_vrfb_restore_context(void)
{
	int i;
	unsigned long map = ctx_map;

	for (i = ffs(map); i; i = ffs(map)) {
		/* i=1..32 */
		i--;
		map &= ~(1 << i);
		restore_hw_context(i);
	}
}

void omap_vrfb_adjust_size(u16 *width, u16 *height,
		u8 bytespp)
{
	*width = ALIGN(*width * bytespp, VRFB_PAGE_WIDTH) / bytespp;
	*height = ALIGN(*height, VRFB_PAGE_HEIGHT);
}
EXPORT_SYMBOL(omap_vrfb_adjust_size);

u32 omap_vrfb_min_phys_size(u16 width, u16 height, u8 bytespp)
{
	unsigned long image_width_roundup = get_image_width_roundup(width,
		bytespp);

	if (image_width_roundup > OMAP_VRFB_LINE_LEN)
		return 0;

	return (width * height * bytespp) + get_extra_physical_size(
		image_width_roundup, bytespp);
}
EXPORT_SYMBOL(omap_vrfb_min_phys_size);

u16 omap_vrfb_max_height(u32 phys_size, u16 width, u8 bytespp)
{
	unsigned long image_width_roundup = get_image_width_roundup(width,
		bytespp);
	unsigned long height;
	unsigned long extra;

	if (image_width_roundup > OMAP_VRFB_LINE_LEN)
		return 0;

	extra = get_extra_physical_size(image_width_roundup, bytespp);

	if (phys_size < extra)
		return 0;

	height = (phys_size - extra) / (width * bytespp);

	/* Virtual views provided by VRFB are limited to 2048x2048. */
	return min_t(unsigned long, height, 2048);
}
EXPORT_SYMBOL(omap_vrfb_max_height);

void omap_vrfb_setup(struct vrfb *vrfb, unsigned long paddr,
		u16 width, u16 height,
		unsigned bytespp, bool yuv_mode)
{
	unsigned pixel_size_exp;
	u16 vrfb_width;
	u16 vrfb_height;
	u8 ctx = vrfb->context;
	u32 size;
	u32 control;

	DBG("omapfb_set_vrfb(%d, %lx, %dx%d, %d, %d)\n", ctx, paddr,
			width, height, bytespp, yuv_mode);

	/* For YUV2 and UYVY modes VRFB needs to handle pixels a bit
	 * differently. See TRM. */
	if (yuv_mode) {
		bytespp *= 2;
		width /= 2;
	}

	if (bytespp == 4)
		pixel_size_exp = 2;
	else if (bytespp == 2)
		pixel_size_exp = 1;
	else {
		BUG();
		return;
	}

	vrfb_width = ALIGN(width * bytespp, VRFB_PAGE_WIDTH) / bytespp;
	vrfb_height = ALIGN(height, VRFB_PAGE_HEIGHT);

	DBG("vrfb w %u, h %u bytespp %d\n", vrfb_width, vrfb_height, bytespp);

	size  = vrfb_width << SMS_IMAGEWIDTH_OFFSET;
	size |= vrfb_height << SMS_IMAGEHEIGHT_OFFSET;

	control  = pixel_size_exp << SMS_PS_OFFSET;
	control |= VRFB_PAGE_WIDTH_EXP  << SMS_PW_OFFSET;
	control |= VRFB_PAGE_HEIGHT_EXP << SMS_PH_OFFSET;

	vrfb_hw_context[ctx].physical_ba = paddr;
	vrfb_hw_context[ctx].size = size;
	vrfb_hw_context[ctx].control = control;

	omap2_sms_write_rot_physical_ba(paddr, ctx);
	omap2_sms_write_rot_size(size, ctx);
	omap2_sms_write_rot_control(control, ctx);

	DBG("vrfb offset pixels %d, %d\n",
			vrfb_width - width, vrfb_height - height);

	vrfb->xres = width;
	vrfb->yres = height;
	vrfb->xoffset = vrfb_width - width;
	vrfb->yoffset = vrfb_height - height;
	vrfb->bytespp = bytespp;
	vrfb->yuv_mode = yuv_mode;
}
EXPORT_SYMBOL(omap_vrfb_setup);

int omap_vrfb_map_angle(struct vrfb *vrfb, u16 height, u8 rot)
{
	unsigned long size = height * OMAP_VRFB_LINE_LEN * vrfb->bytespp;

	vrfb->vaddr[rot] = ioremap_wc(vrfb->paddr[rot], size);

	if (!vrfb->vaddr[rot]) {
		printk(KERN_ERR "vrfb: ioremap failed\n");
		return -ENOMEM;
	}

	DBG("ioremapped vrfb area %d of size %lu into %p\n", rot, size,
		vrfb->vaddr[rot]);

	return 0;
}
EXPORT_SYMBOL(omap_vrfb_map_angle);

void omap_vrfb_release_ctx(struct vrfb *vrfb)
{
	int rot;
	int ctx = vrfb->context;

	if (ctx == 0xff)
		return;

	DBG("release ctx %d\n", ctx);

	mutex_lock(&ctx_lock);

	BUG_ON(!(ctx_map & (1 << ctx)));

	clear_bit(ctx, &ctx_map);

	for (rot = 0; rot < 4; ++rot) {
		if (vrfb->paddr[rot]) {
			release_mem_region(vrfb->paddr[rot], OMAP_VRFB_SIZE);
			vrfb->paddr[rot] = 0;
		}
	}

	vrfb->context = 0xff;

	mutex_unlock(&ctx_lock);
}
EXPORT_SYMBOL(omap_vrfb_release_ctx);

int omap_vrfb_request_ctx(struct vrfb *vrfb)
{
	int rot;
	u32 paddr;
	u8 ctx;
	int r;

	DBG("request ctx\n");

	mutex_lock(&ctx_lock);

	for (ctx = 0; ctx < VRFB_NUM_CTXS; ++ctx)
		if ((ctx_map & (1 << ctx)) == 0)
			break;

	if (ctx == VRFB_NUM_CTXS) {
		pr_err("vrfb: no free contexts\n");
		r = -EBUSY;
		goto out;
	}

	DBG("found free ctx %d\n", ctx);

	set_bit(ctx, &ctx_map);

	memset(vrfb, 0, sizeof(*vrfb));

	vrfb->context = ctx;

	for (rot = 0; rot < 4; ++rot) {
		paddr = SMS_ROT_VIRT_BASE(ctx, rot);
		if (!request_mem_region(paddr, OMAP_VRFB_SIZE, "vrfb")) {
			pr_err("vrfb: failed to reserve VRFB "
					"area for ctx %d, rotation %d\n",
					ctx, rot * 90);
			omap_vrfb_release_ctx(vrfb);
			r = -ENOMEM;
			goto out;
		}

		vrfb->paddr[rot] = paddr;

		DBG("VRFB %d/%d: %lx\n", ctx, rot*90, vrfb->paddr[rot]);
	}

	r = 0;
out:
	mutex_unlock(&ctx_lock);
	return r;
}
EXPORT_SYMBOL(omap_vrfb_request_ctx);
