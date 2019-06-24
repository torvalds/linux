// SPDX-License-Identifier: GPL-2.0-only
/*
 *	linux/arch/arm/mach-nspire/clcd.c
 *
 *	Copyright (C) 2013 Daniel Tang <tangrs@tangrs.id.au>
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/amba/bus.h>
#include <linux/amba/clcd.h>
#include <linux/dma-mapping.h>

static struct clcd_panel nspire_cx_lcd_panel = {
	.mode		= {
		.name		= "Color LCD",
		.refresh	= 60,
		.xres		= 320,
		.yres		= 240,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
		.pixclock	= 1,
		.hsync_len	= 6,
		.vsync_len	= 1,
		.right_margin	= 50,
		.left_margin	= 38,
		.lower_margin	= 3,
		.upper_margin	= 17,
	},
	.width		= 65, /* ~6.50 cm */
	.height		= 49, /* ~4.87 cm */
	.tim2		= TIM2_IPC,
	.cntl		= CNTL_LCDTFT | CNTL_LCDVCOMP(1),
	.bpp		= 16,
	.caps		= CLCD_CAP_565,
};

static struct clcd_panel nspire_classic_lcd_panel = {
	.mode		= {
		.name		= "Grayscale LCD",
		.refresh	= 60,
		.xres		= 320,
		.yres		= 240,
		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode		= FB_VMODE_NONINTERLACED,
		.pixclock	= 1,
		.hsync_len	= 6,
		.vsync_len	= 1,
		.right_margin	= 6,
		.left_margin	= 6,
	},
	.width		= 71, /* 7.11cm */
	.height		= 53, /* 5.33cm */
	.tim2		= 0x80007d0,
	.cntl		= CNTL_LCDMONO8,
	.bpp		= 8,
	.grayscale	= 1,
	.caps		= CLCD_CAP_5551,
};

int nspire_clcd_setup(struct clcd_fb *fb)
{
	struct clcd_panel *panel;
	size_t panel_size;
	const char *type;
	dma_addr_t dma;
	int err;

	BUG_ON(!fb->dev->dev.of_node);

	err = of_property_read_string(fb->dev->dev.of_node, "lcd-type", &type);
	if (err) {
		pr_err("CLCD: Could not find lcd-type property\n");
		return err;
	}

	if (!strcmp(type, "cx")) {
		panel = &nspire_cx_lcd_panel;
	} else if (!strcmp(type, "classic")) {
		panel = &nspire_classic_lcd_panel;
	} else {
		pr_err("CLCD: Unknown lcd-type %s\n", type);
		return -EINVAL;
	}

	panel_size = ((panel->mode.xres * panel->mode.yres) * panel->bpp) / 8;
	panel_size = ALIGN(panel_size, PAGE_SIZE);

	fb->fb.screen_base = dma_alloc_wc(&fb->dev->dev, panel_size, &dma,
					  GFP_KERNEL);

	if (!fb->fb.screen_base) {
		pr_err("CLCD: unable to map framebuffer\n");
		return -ENOMEM;
	}

	fb->fb.fix.smem_start = dma;
	fb->fb.fix.smem_len = panel_size;
	fb->panel = panel;

	return 0;
}

int nspire_clcd_mmap(struct clcd_fb *fb, struct vm_area_struct *vma)
{
	return dma_mmap_wc(&fb->dev->dev, vma, fb->fb.screen_base,
			   fb->fb.fix.smem_start, fb->fb.fix.smem_len);
}

void nspire_clcd_remove(struct clcd_fb *fb)
{
	dma_free_wc(&fb->dev->dev, fb->fb.fix.smem_len, fb->fb.screen_base,
		    fb->fb.fix.smem_start);
}
