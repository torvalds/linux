/*
 * vivid-osd.c - osd support for testing overlays.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/font.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/fb.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-common.h>

#include "vivid-core.h"
#include "vivid-osd.h"

#define MAX_OSD_WIDTH  720
#define MAX_OSD_HEIGHT 576

/*
 * Order: white, yellow, cyan, green, magenta, red, blue, black,
 * and same again with the alpha bit set (if any)
 */
static const u16 rgb555[16] = {
	0x7fff, 0x7fe0, 0x03ff, 0x03e0, 0x7c1f, 0x7c00, 0x001f, 0x0000,
	0xffff, 0xffe0, 0x83ff, 0x83e0, 0xfc1f, 0xfc00, 0x801f, 0x8000
};

static const u16 rgb565[16] = {
	0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000,
	0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000
};

void vivid_clear_fb(struct vivid_dev *dev)
{
	void *p = dev->video_vbase;
	const u16 *rgb = rgb555;
	unsigned x, y;

	if (dev->fb_defined.green.length == 6)
		rgb = rgb565;

	for (y = 0; y < dev->display_height; y++) {
		u16 *d = p;

		for (x = 0; x < dev->display_width; x++)
			d[x] = rgb[(y / 16 + x / 16) % 16];
		p += dev->display_byte_stride;
	}
}

/* --------------------------------------------------------------------- */

static int vivid_fb_ioctl(struct fb_info *info, unsigned cmd, unsigned long arg)
{
	struct vivid_dev *dev = (struct vivid_dev *)info->par;

	switch (cmd) {
	case FBIOGET_VBLANK: {
		struct fb_vblank vblank;

		memset(&vblank, 0, sizeof(vblank));
		vblank.flags = FB_VBLANK_HAVE_COUNT | FB_VBLANK_HAVE_VCOUNT |
			FB_VBLANK_HAVE_VSYNC;
		vblank.count = 0;
		vblank.vcount = 0;
		vblank.hcount = 0;
		if (copy_to_user((void __user *)arg, &vblank, sizeof(vblank)))
			return -EFAULT;
		return 0;
	}

	default:
		dprintk(dev, 1, "Unknown ioctl %08x\n", cmd);
		return -EINVAL;
	}
	return 0;
}

/* Framebuffer device handling */

static int vivid_fb_set_var(struct vivid_dev *dev, struct fb_var_screeninfo *var)
{
	dprintk(dev, 1, "vivid_fb_set_var\n");

	if (var->bits_per_pixel != 16) {
		dprintk(dev, 1, "vivid_fb_set_var - Invalid bpp\n");
		return -EINVAL;
	}
	dev->display_byte_stride = var->xres * dev->bytes_per_pixel;

	return 0;
}

static int vivid_fb_get_fix(struct vivid_dev *dev, struct fb_fix_screeninfo *fix)
{
	dprintk(dev, 1, "vivid_fb_get_fix\n");
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strlcpy(fix->id, "vioverlay fb", sizeof(fix->id));
	fix->smem_start = dev->video_pbase;
	fix->smem_len = dev->video_buffer_size;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->visual = FB_VISUAL_TRUECOLOR;
	fix->xpanstep = 1;
	fix->ypanstep = 1;
	fix->ywrapstep = 0;
	fix->line_length = dev->display_byte_stride;
	fix->accel = FB_ACCEL_NONE;
	return 0;
}

/* Check the requested display mode, returning -EINVAL if we can't
   handle it. */

static int _vivid_fb_check_var(struct fb_var_screeninfo *var, struct vivid_dev *dev)
{
	dprintk(dev, 1, "vivid_fb_check_var\n");

	var->bits_per_pixel = 16;
	if (var->green.length == 5) {
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 15;
		var->transp.length = 1;
	} else {
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
	}
	var->xoffset = var->yoffset = 0;
	var->left_margin = var->upper_margin = 0;
	var->nonstd = 0;

	var->vmode &= ~FB_VMODE_MASK;
	var->vmode = FB_VMODE_NONINTERLACED;

	/* Dummy values */
	var->hsync_len = 24;
	var->vsync_len = 2;
	var->pixclock = 84316;
	var->right_margin = 776;
	var->lower_margin = 591;
	return 0;
}

static int vivid_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct vivid_dev *dev = (struct vivid_dev *) info->par;

	dprintk(dev, 1, "vivid_fb_check_var\n");
	return _vivid_fb_check_var(var, dev);
}

static int vivid_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	return 0;
}

static int vivid_fb_set_par(struct fb_info *info)
{
	int rc = 0;
	struct vivid_dev *dev = (struct vivid_dev *) info->par;

	dprintk(dev, 1, "vivid_fb_set_par\n");

	rc = vivid_fb_set_var(dev, &info->var);
	vivid_fb_get_fix(dev, &info->fix);
	return rc;
}

static int vivid_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
				unsigned blue, unsigned transp,
				struct fb_info *info)
{
	u32 color, *palette;

	if (regno >= info->cmap.len)
		return -EINVAL;

	color = ((transp & 0xFF00) << 16) | ((red & 0xFF00) << 8) |
		 (green & 0xFF00) | ((blue & 0xFF00) >> 8);
	if (regno >= 16)
		return -EINVAL;

	palette = info->pseudo_palette;
	if (info->var.bits_per_pixel == 16) {
		switch (info->var.green.length) {
		case 6:
			color = (red & 0xf800) |
				((green & 0xfc00) >> 5) |
				((blue & 0xf800) >> 11);
			break;
		case 5:
			color = ((red & 0xf800) >> 1) |
				((green & 0xf800) >> 6) |
				((blue & 0xf800) >> 11) |
				(transp ? 0x8000 : 0);
			break;
		}
	}
	palette[regno] = color;
	return 0;
}

/* We don't really support blanking. All this does is enable or
   disable the OSD. */
static int vivid_fb_blank(int blank_mode, struct fb_info *info)
{
	struct vivid_dev *dev = (struct vivid_dev *)info->par;

	dprintk(dev, 1, "Set blanking mode : %d\n", blank_mode);
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		break;
	case FB_BLANK_NORMAL:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		break;
	}
	return 0;
}

static struct fb_ops vivid_fb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var   = vivid_fb_check_var,
	.fb_set_par     = vivid_fb_set_par,
	.fb_setcolreg   = vivid_fb_setcolreg,
	.fb_fillrect    = cfb_fillrect,
	.fb_copyarea    = cfb_copyarea,
	.fb_imageblit   = cfb_imageblit,
	.fb_cursor      = NULL,
	.fb_ioctl       = vivid_fb_ioctl,
	.fb_pan_display = vivid_fb_pan_display,
	.fb_blank       = vivid_fb_blank,
};

/* Initialization */


/* Setup our initial video mode */
static int vivid_fb_init_vidmode(struct vivid_dev *dev)
{
	struct v4l2_rect start_window;

	/* Color mode */

	dev->bits_per_pixel = 16;
	dev->bytes_per_pixel = dev->bits_per_pixel / 8;

	start_window.width = MAX_OSD_WIDTH;
	start_window.left = 0;

	dev->display_byte_stride = start_window.width * dev->bytes_per_pixel;

	/* Vertical size & position */

	start_window.height = MAX_OSD_HEIGHT;
	start_window.top = 0;

	dev->display_width = start_window.width;
	dev->display_height = start_window.height;

	/* Generate a valid fb_var_screeninfo */

	dev->fb_defined.xres = dev->display_width;
	dev->fb_defined.yres = dev->display_height;
	dev->fb_defined.xres_virtual = dev->display_width;
	dev->fb_defined.yres_virtual = dev->display_height;
	dev->fb_defined.bits_per_pixel = dev->bits_per_pixel;
	dev->fb_defined.vmode = FB_VMODE_NONINTERLACED;
	dev->fb_defined.left_margin = start_window.left + 1;
	dev->fb_defined.upper_margin = start_window.top + 1;
	dev->fb_defined.accel_flags = FB_ACCEL_NONE;
	dev->fb_defined.nonstd = 0;
	/* set default to 1:5:5:5 */
	dev->fb_defined.green.length = 5;

	/* We've filled in the most data, let the usual mode check
	   routine fill in the rest. */
	_vivid_fb_check_var(&dev->fb_defined, dev);

	/* Generate valid fb_fix_screeninfo */

	vivid_fb_get_fix(dev, &dev->fb_fix);

	/* Generate valid fb_info */

	dev->fb_info.node = -1;
	dev->fb_info.flags = FBINFO_FLAG_DEFAULT;
	dev->fb_info.fbops = &vivid_fb_ops;
	dev->fb_info.par = dev;
	dev->fb_info.var = dev->fb_defined;
	dev->fb_info.fix = dev->fb_fix;
	dev->fb_info.screen_base = (u8 __iomem *)dev->video_vbase;
	dev->fb_info.fbops = &vivid_fb_ops;

	/* Supply some monitor specs. Bogus values will do for now */
	dev->fb_info.monspecs.hfmin = 8000;
	dev->fb_info.monspecs.hfmax = 70000;
	dev->fb_info.monspecs.vfmin = 10;
	dev->fb_info.monspecs.vfmax = 100;

	/* Allocate color map */
	if (fb_alloc_cmap(&dev->fb_info.cmap, 256, 1)) {
		pr_err("abort, unable to alloc cmap\n");
		return -ENOMEM;
	}

	/* Allocate the pseudo palette */
	dev->fb_info.pseudo_palette = kmalloc_array(16, sizeof(u32), GFP_KERNEL);

	return dev->fb_info.pseudo_palette ? 0 : -ENOMEM;
}

/* Release any memory we've grabbed */
void vivid_fb_release_buffers(struct vivid_dev *dev)
{
	if (dev->video_vbase == NULL)
		return;

	/* Release cmap */
	if (dev->fb_info.cmap.len)
		fb_dealloc_cmap(&dev->fb_info.cmap);

	/* Release pseudo palette */
	kfree(dev->fb_info.pseudo_palette);
	kfree((void *)dev->video_vbase);
}

/* Initialize the specified card */

int vivid_fb_init(struct vivid_dev *dev)
{
	int ret;

	dev->video_buffer_size = MAX_OSD_HEIGHT * MAX_OSD_WIDTH * 2;
	dev->video_vbase = kzalloc(dev->video_buffer_size, GFP_KERNEL | GFP_DMA32);
	if (dev->video_vbase == NULL)
		return -ENOMEM;
	dev->video_pbase = virt_to_phys(dev->video_vbase);

	pr_info("Framebuffer at 0x%lx, mapped to 0x%p, size %dk\n",
			dev->video_pbase, dev->video_vbase,
			dev->video_buffer_size / 1024);

	/* Set the startup video mode information */
	ret = vivid_fb_init_vidmode(dev);
	if (ret) {
		vivid_fb_release_buffers(dev);
		return ret;
	}

	vivid_clear_fb(dev);

	/* Register the framebuffer */
	if (register_framebuffer(&dev->fb_info) < 0) {
		vivid_fb_release_buffers(dev);
		return -EINVAL;
	}

	/* Set the card to the requested mode */
	vivid_fb_set_par(&dev->fb_info);
	return 0;

}
