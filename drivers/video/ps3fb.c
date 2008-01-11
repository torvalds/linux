/*
 *  linux/drivers/video/ps3fb.c -- PS3 GPU frame buffer device
 *
 *	Copyright (C) 2006 Sony Computer Entertainment Inc.
 *	Copyright 2006, 2007 Sony Corporation
 *
 *  This file is based on :
 *
 *  linux/drivers/video/vfb.c -- Virtual frame buffer device
 *
 *	Copyright (C) 2002 James Simmons
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/ioctl.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/uaccess.h>
#include <linux/fb.h>
#include <linux/init.h>

#include <asm/abs_addr.h>
#include <asm/lv1call.h>
#include <asm/ps3av.h>
#include <asm/ps3fb.h>
#include <asm/ps3.h>


#define DEVICE_NAME		"ps3fb"

#define L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC	0x101
#define L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP	0x102
#define L1GPU_CONTEXT_ATTRIBUTE_FB_SETUP	0x600
#define L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT		0x601
#define L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT_SYNC	0x602

#define L1GPU_FB_BLIT_WAIT_FOR_COMPLETION	(1ULL << 32)

#define L1GPU_DISPLAY_SYNC_HSYNC		1
#define L1GPU_DISPLAY_SYNC_VSYNC		2

#define GPU_CMD_BUF_SIZE			(2 * 1024 * 1024)
#define GPU_FB_START				(64 * 1024)
#define GPU_IOIF				(0x0d000000UL)
#define GPU_ALIGN_UP(x)				_ALIGN_UP((x), 64)
#define GPU_MAX_LINE_LENGTH			(65536 - 64)

#define PS3FB_FULL_MODE_BIT			0x80

#define GPU_INTR_STATUS_VSYNC_0			0	/* vsync on head A */
#define GPU_INTR_STATUS_VSYNC_1			1	/* vsync on head B */
#define GPU_INTR_STATUS_FLIP_0			3	/* flip head A */
#define GPU_INTR_STATUS_FLIP_1			4	/* flip head B */
#define GPU_INTR_STATUS_QUEUE_0			5	/* queue head A */
#define GPU_INTR_STATUS_QUEUE_1			6	/* queue head B */

#define GPU_DRIVER_INFO_VERSION			0x211

/* gpu internals */
struct display_head {
	u64 be_time_stamp;
	u32 status;
	u32 offset;
	u32 res1;
	u32 res2;
	u32 field;
	u32 reserved1;

	u64 res3;
	u32 raster;

	u64 vblank_count;
	u32 field_vsync;
	u32 reserved2;
};

struct gpu_irq {
	u32 irq_outlet;
	u32 status;
	u32 mask;
	u32 video_cause;
	u32 graph_cause;
	u32 user_cause;

	u32 res1;
	u64 res2;

	u32 reserved[4];
};

struct gpu_driver_info {
	u32 version_driver;
	u32 version_gpu;
	u32 memory_size;
	u32 hardware_channel;

	u32 nvcore_frequency;
	u32 memory_frequency;

	u32 reserved[1063];
	struct display_head display_head[8];
	struct gpu_irq irq;
};

struct ps3fb_priv {
	unsigned int irq_no;

	u64 context_handle, memory_handle;
	void *xdr_ea;
	size_t xdr_size;
	struct gpu_driver_info *dinfo;

	u64 vblank_count;	/* frame count */
	wait_queue_head_t wait_vsync;

	atomic_t ext_flip;	/* on/off flip with vsync */
	atomic_t f_count;	/* fb_open count */
	int is_blanked;
	int is_kicked;
	struct task_struct *task;
};
static struct ps3fb_priv ps3fb;

struct ps3fb_par {
	u32 pseudo_palette[16];
	int mode_id, new_mode_id;
	int res_index;
	unsigned int num_frames;	/* num of frame buffers */
	unsigned int width;
	unsigned int height;
	unsigned long full_offset;	/* start of fullscreen DDR fb */
	unsigned long fb_offset;	/* start of actual DDR fb */
	unsigned long pan_offset;
};

struct ps3fb_res_table {
	u32 xres;
	u32 yres;
	u32 xoff;
	u32 yoff;
	u32 type;
};
#define PS3FB_RES_FULL 1
static const struct ps3fb_res_table ps3fb_res[] = {
	/* res_x,y   margin_x,y  full */
	{  720,  480,  72,  48 , 0},
	{  720,  576,  72,  58 , 0},
	{ 1280,  720,  78,  38 , 0},
	{ 1920, 1080, 116,  58 , 0},
	/* full mode */
	{  720,  480,   0,   0 , PS3FB_RES_FULL},
	{  720,  576,   0,   0 , PS3FB_RES_FULL},
	{ 1280,  720,   0,   0 , PS3FB_RES_FULL},
	{ 1920, 1080,   0,   0 , PS3FB_RES_FULL},
	/* vesa: normally full mode */
	{ 1280,  768,   0,   0 , 0},
	{ 1280, 1024,   0,   0 , 0},
	{ 1920, 1200,   0,   0 , 0},
	{    0,    0,   0,   0 , 0} };

/* default resolution */
#define GPU_RES_INDEX	0		/* 720 x 480 */

static const struct fb_videomode ps3fb_modedb[] = {
    /* 60 Hz broadcast modes (modes "1" to "5") */
    {
        /* 480i */
        "480i", 60, 576, 384, 74074, 130, 89, 78, 57, 63, 6,
        FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    },    {
        /* 480p */
        "480p", 60, 576, 384, 37037, 130, 89, 78, 57, 63, 6,
        FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    },    {
        /* 720p */
        "720p", 60, 1124, 644, 13481, 298, 148, 57, 44, 80, 5,
        FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    },    {
        /* 1080i */
        "1080i", 60, 1688, 964, 13481, 264, 160, 94, 62, 88, 5,
        FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    },    {
        /* 1080p */
        "1080p", 60, 1688, 964, 6741, 264, 160, 94, 62, 88, 5,
        FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    },

    /* 50 Hz broadcast modes (modes "6" to "10") */
    {
        /* 576i */
        "576i", 50, 576, 460, 74074, 142, 83, 97, 63, 63, 5,
        FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    },    {
        /* 576p */
        "576p", 50, 576, 460, 37037, 142, 83, 97, 63, 63, 5,
        FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    },    {
        /* 720p */
        "720p", 50, 1124, 644, 13468, 298, 478, 57, 44, 80, 5,
        FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    },    {
        /* 1080 */
        "1080i", 50, 1688, 964, 13468, 264, 600, 94, 62, 88, 5,
        FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    },    {
        /* 1080p */
        "1080p", 50, 1688, 964, 6734, 264, 600, 94, 62, 88, 5,
        FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    },

    /* VESA modes (modes "11" to "13") */
    {
	/* WXGA */
	"wxga", 60, 1280, 768, 12924, 160, 24, 29, 3, 136, 6,
	0, FB_VMODE_NONINTERLACED,
	FB_MODE_IS_VESA
    }, {
	/* SXGA */
	"sxga", 60, 1280, 1024, 9259, 248, 48, 38, 1, 112, 3,
	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED,
	FB_MODE_IS_VESA
    }, {
	/* WUXGA */
	"wuxga", 60, 1920, 1200, 6494, 80, 48, 26, 3, 32, 6,
	FB_SYNC_HOR_HIGH_ACT, FB_VMODE_NONINTERLACED,
	FB_MODE_IS_VESA
    },

    /* 60 Hz broadcast modes (full resolution versions of modes "1" to "5") */
    {
	/* 480if */
	"480if", 60, 720, 480, 74074, 58, 17, 30, 9, 63, 6,
	FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    }, {
	/* 480pf */
	"480pf", 60, 720, 480, 37037, 58, 17, 30, 9, 63, 6,
	FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    }, {
	/* 720pf */
	"720pf", 60, 1280, 720, 13481, 220, 70, 19, 6, 80, 5,
	FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    }, {
	/* 1080if */
	"1080if", 60, 1920, 1080, 13481, 148, 44, 36, 4, 88, 5,
	FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    }, {
	/* 1080pf */
	"1080pf", 60, 1920, 1080, 6741, 148, 44, 36, 4, 88, 5,
	FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    },

    /* 50 Hz broadcast modes (full resolution versions of modes "6" to "10") */
    {
	/* 576if */
	"576if", 50, 720, 576, 74074, 70, 11, 39, 5, 63, 5,
	FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    }, {
	/* 576pf */
	"576pf", 50, 720, 576, 37037, 70, 11, 39, 5, 63, 5,
	FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    }, {
	/* 720pf */
	"720pf", 50, 1280, 720, 13468, 220, 400, 19, 6, 80, 5,
	FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    }, {
	/* 1080if */
	"1080f", 50, 1920, 1080, 13468, 148, 484, 36, 4, 88, 5,
	FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    }, {
	/* 1080pf */
	"1080pf", 50, 1920, 1080, 6734, 148, 484, 36, 4, 88, 5,
	FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    }
};


#define HEAD_A
#define HEAD_B

#define X_OFF(i)	(ps3fb_res[i].xoff)	/* left/right margin (pixel) */
#define Y_OFF(i)	(ps3fb_res[i].yoff)	/* top/bottom margin (pixel) */
#define WIDTH(i)	(ps3fb_res[i].xres)	/* width of FB */
#define HEIGHT(i)	(ps3fb_res[i].yres)	/* height of FB */
#define BPP		4			/* number of bytes per pixel */

/* Start of the virtual frame buffer (relative to fullscreen ) */
#define VP_OFF(i)	((WIDTH(i) * Y_OFF(i) + X_OFF(i)) * BPP)


static int ps3fb_mode;
module_param(ps3fb_mode, int, 0);

static char *mode_option __devinitdata;

static int ps3fb_get_res_table(u32 xres, u32 yres, int mode)
{
	int full_mode;
	unsigned int i;
	u32 x, y, f;

	full_mode = (mode & PS3FB_FULL_MODE_BIT) ? PS3FB_RES_FULL : 0;
	for (i = 0;; i++) {
		x = ps3fb_res[i].xres;
		y = ps3fb_res[i].yres;
		f = ps3fb_res[i].type;

		if (!x) {
			pr_debug("ERROR: ps3fb_get_res_table()\n");
			return -1;
		}

		if (full_mode == PS3FB_RES_FULL && f != PS3FB_RES_FULL)
			continue;

		if (x == xres && (yres == 0 || y == yres))
			break;

		x = x - 2 * ps3fb_res[i].xoff;
		y = y - 2 * ps3fb_res[i].yoff;
		if (x == xres && (yres == 0 || y == yres))
			break;
	}
	return i;
}

static unsigned int ps3fb_find_mode(const struct fb_var_screeninfo *var,
				    u32 *ddr_line_length, u32 *xdr_line_length)
{
	unsigned int i, mode;

	for (i = 0; i < ARRAY_SIZE(ps3fb_modedb); i++)
		if (var->xres == ps3fb_modedb[i].xres &&
		    var->yres == ps3fb_modedb[i].yres &&
		    var->pixclock == ps3fb_modedb[i].pixclock &&
		    var->hsync_len == ps3fb_modedb[i].hsync_len &&
		    var->vsync_len == ps3fb_modedb[i].vsync_len &&
		    var->left_margin == ps3fb_modedb[i].left_margin &&
		    var->right_margin == ps3fb_modedb[i].right_margin &&
		    var->upper_margin == ps3fb_modedb[i].upper_margin &&
		    var->lower_margin == ps3fb_modedb[i].lower_margin &&
		    var->sync == ps3fb_modedb[i].sync &&
		    (var->vmode & FB_VMODE_MASK) == ps3fb_modedb[i].vmode)
			goto found;

	pr_debug("ps3fb_find_mode: mode not found\n");
	return 0;

found:
	/* Cropped broadcast modes use the full line length */
	*ddr_line_length = ps3fb_modedb[i < 10 ? i + 13 : i].xres * BPP;

	if (ps3_compare_firmware_version(1, 9, 0) >= 0) {
		*xdr_line_length = GPU_ALIGN_UP(max(var->xres,
						    var->xres_virtual) * BPP);
		if (*xdr_line_length > GPU_MAX_LINE_LENGTH)
			*xdr_line_length = GPU_MAX_LINE_LENGTH;
	} else
		*xdr_line_length = *ddr_line_length;

	/* Full broadcast modes have the full mode bit set */
	mode = i > 12 ? (i - 12) | PS3FB_FULL_MODE_BIT : i + 1;

	pr_debug("ps3fb_find_mode: mode %u\n", mode);

	return mode;
}

static const struct fb_videomode *ps3fb_default_mode(int id)
{
	u32 mode = id & PS3AV_MODE_MASK;
	u32 flags;

	if (mode < 1 || mode > 13)
		return NULL;

	flags = id & ~PS3AV_MODE_MASK;

	if (mode <= 10 && flags & PS3FB_FULL_MODE_BIT) {
		/* Full broadcast mode */
		return &ps3fb_modedb[mode + 12];
	}

	return &ps3fb_modedb[mode - 1];
}

static void ps3fb_sync_image(struct device *dev, u64 frame_offset,
			     u64 dst_offset, u64 src_offset, u32 width,
			     u32 height, u32 dst_line_length,
			     u32 src_line_length)
{
	int status;
	u64 line_length;

	line_length = dst_line_length;
	if (src_line_length != dst_line_length)
		line_length |= (u64)src_line_length << 32;

	src_offset += GPU_FB_START;
	status = lv1_gpu_context_attribute(ps3fb.context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT,
					   dst_offset, GPU_IOIF + src_offset,
					   L1GPU_FB_BLIT_WAIT_FOR_COMPLETION |
					   (width << 16) | height,
					   line_length);
	if (status)
		dev_err(dev,
			"%s: lv1_gpu_context_attribute FB_BLIT failed: %d\n",
			__func__, status);
#ifdef HEAD_A
	status = lv1_gpu_context_attribute(ps3fb.context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP,
					   0, frame_offset, 0, 0);
	if (status)
		dev_err(dev, "%s: lv1_gpu_context_attribute FLIP failed: %d\n",
			__func__, status);
#endif
#ifdef HEAD_B
	status = lv1_gpu_context_attribute(ps3fb.context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP,
					   1, frame_offset, 0, 0);
	if (status)
		dev_err(dev, "%s: lv1_gpu_context_attribute FLIP failed: %d\n",
			__func__, status);
#endif
}

static int ps3fb_sync(struct fb_info *info, u32 frame)
{
	struct ps3fb_par *par = info->par;
	int i, error = 0;
	u32 ddr_line_length, xdr_line_length;
	u64 ddr_base, xdr_base;

	acquire_console_sem();

	if (frame > par->num_frames - 1) {
		dev_dbg(info->device, "%s: invalid frame number (%u)\n",
			__func__, frame);
		error = -EINVAL;
		goto out;
	}

	i = par->res_index;
	xdr_line_length = info->fix.line_length;
	ddr_line_length = ps3fb_res[i].xres * BPP;
	xdr_base = frame * info->var.yres_virtual * xdr_line_length;
	ddr_base = frame * ps3fb_res[i].yres * ddr_line_length;

	ps3fb_sync_image(info->device, ddr_base + par->full_offset,
			 ddr_base + par->fb_offset, xdr_base + par->pan_offset,
			 par->width, par->height, ddr_line_length,
			 xdr_line_length);

out:
	release_console_sem();
	return error;
}

static int ps3fb_open(struct fb_info *info, int user)
{
	atomic_inc(&ps3fb.f_count);
	return 0;
}

static int ps3fb_release(struct fb_info *info, int user)
{
	if (atomic_dec_and_test(&ps3fb.f_count)) {
		if (atomic_read(&ps3fb.ext_flip)) {
			atomic_set(&ps3fb.ext_flip, 0);
			ps3fb_sync(info, 0);	/* single buffer */
		}
	}
	return 0;
}

    /*
     *  Setting the video mode has been split into two parts.
     *  First part, xxxfb_check_var, must not write anything
     *  to hardware, it should only verify and adjust var.
     *  This means it doesn't alter par but it does use hardware
     *  data from it to check this var.
     */

static int ps3fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	u32 xdr_line_length, ddr_line_length;
	int mode;

	dev_dbg(info->device, "var->xres:%u info->var.xres:%u\n", var->xres,
		info->var.xres);
	dev_dbg(info->device, "var->yres:%u info->var.yres:%u\n", var->yres,
		info->var.yres);

	/* FIXME For now we do exact matches only */
	mode = ps3fb_find_mode(var, &ddr_line_length, &xdr_line_length);
	if (!mode)
		return -EINVAL;

	/* Virtual screen */
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	if (var->xres_virtual > xdr_line_length / BPP) {
		dev_dbg(info->device,
			"Horizontal virtual screen size too large\n");
		return -EINVAL;
	}

	if (var->xoffset + var->xres > var->xres_virtual ||
	    var->yoffset + var->yres > var->yres_virtual) {
		dev_dbg(info->device, "panning out-of-range\n");
		return -EINVAL;
	}

	/* We support ARGB8888 only */
	if (var->bits_per_pixel > 32 || var->grayscale ||
	    var->red.offset > 16 || var->green.offset > 8 ||
	    var->blue.offset > 0 || var->transp.offset > 24 ||
	    var->red.length > 8 || var->green.length > 8 ||
	    var->blue.length > 8 || var->transp.length > 8 ||
	    var->red.msb_right || var->green.msb_right ||
	    var->blue.msb_right || var->transp.msb_right || var->nonstd) {
		dev_dbg(info->device, "We support ARGB8888 only\n");
		return -EINVAL;
	}

	var->bits_per_pixel = 32;
	var->red.offset = 16;
	var->green.offset = 8;
	var->blue.offset = 0;
	var->transp.offset = 24;
	var->red.length = 8;
	var->green.length = 8;
	var->blue.length = 8;
	var->transp.length = 8;
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	/* Rotation is not supported */
	if (var->rotate) {
		dev_dbg(info->device, "Rotation is not supported\n");
		return -EINVAL;
	}

	/* Memory limit */
	if (var->yres_virtual * xdr_line_length > ps3fb.xdr_size) {
		dev_dbg(info->device, "Not enough memory\n");
		return -ENOMEM;
	}

	var->height = -1;
	var->width = -1;

	return 0;
}

    /*
     * This routine actually sets the video mode.
     */

static int ps3fb_set_par(struct fb_info *info)
{
	struct ps3fb_par *par = info->par;
	unsigned int mode, ddr_line_length, xdr_line_length, lines, maxlines;
	int i;
	unsigned long offset;
	u64 dst;

	dev_dbg(info->device, "xres:%d xv:%d yres:%d yv:%d clock:%d\n",
		info->var.xres, info->var.xres_virtual,
		info->var.yres, info->var.yres_virtual, info->var.pixclock);

	mode = ps3fb_find_mode(&info->var, &ddr_line_length, &xdr_line_length);
	if (!mode)
		return -EINVAL;

	i = ps3fb_get_res_table(info->var.xres, info->var.yres, mode);
	par->res_index = i;

	info->fix.smem_start = virt_to_abs(ps3fb.xdr_ea);
	info->fix.smem_len = ps3fb.xdr_size;
	info->fix.xpanstep = info->var.xres_virtual > info->var.xres ? 1 : 0;
	info->fix.ypanstep = info->var.yres_virtual > info->var.yres ? 1 : 0;
	info->fix.line_length = xdr_line_length;

	info->screen_base = (char __iomem *)ps3fb.xdr_ea;

	par->num_frames = ps3fb.xdr_size /
			  max(ps3fb_res[i].yres * ddr_line_length,
			      info->var.yres_virtual * xdr_line_length);

	/* Keep the special bits we cannot set using fb_var_screeninfo */
	par->new_mode_id = (par->new_mode_id & ~PS3AV_MODE_MASK) | mode;

	par->width = info->var.xres;
	par->height = info->var.yres;
	offset = VP_OFF(i);
	par->fb_offset = GPU_ALIGN_UP(offset);
	par->full_offset = par->fb_offset - offset;
	par->pan_offset = info->var.yoffset * xdr_line_length +
			  info->var.xoffset * BPP;

	if (par->new_mode_id != par->mode_id) {
		if (ps3av_set_video_mode(par->new_mode_id)) {
			par->new_mode_id = par->mode_id;
			return -EINVAL;
		}
		par->mode_id = par->new_mode_id;
	}

	/* Clear XDR frame buffer memory */
	memset(ps3fb.xdr_ea, 0, ps3fb.xdr_size);

	/* Clear DDR frame buffer memory */
	lines = ps3fb_res[i].yres * par->num_frames;
	if (par->full_offset)
		lines++;
	maxlines = ps3fb.xdr_size / ddr_line_length;
	for (dst = 0; lines; dst += maxlines * ddr_line_length) {
		unsigned int l = min(lines, maxlines);
		ps3fb_sync_image(info->device, 0, dst, 0, ps3fb_res[i].xres, l,
				 ddr_line_length, ddr_line_length);
		lines -= l;
	}

	return 0;
}

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int ps3fb_setcolreg(unsigned int regno, unsigned int red,
			   unsigned int green, unsigned int blue,
			   unsigned int transp, struct fb_info *info)
{
	if (regno >= 16)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;
	transp >>= 8;

	((u32 *)info->pseudo_palette)[regno] = transp << 24 | red << 16 |
					       green << 8 | blue;
	return 0;
}

static int ps3fb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct ps3fb_par *par = info->par;

	par->pan_offset = var->yoffset * info->fix.line_length +
			  var->xoffset * BPP;
	return 0;
}

    /*
     *  As we have a virtual frame buffer, we need our own mmap function
     */

static int ps3fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long size, offset;

	size = vma->vm_end - vma->vm_start;
	offset = vma->vm_pgoff << PAGE_SHIFT;
	if (offset + size > info->fix.smem_len)
		return -EINVAL;

	offset += info->fix.smem_start;
	if (remap_pfn_range(vma, vma->vm_start, offset >> PAGE_SHIFT,
			    size, vma->vm_page_prot))
		return -EAGAIN;

	dev_dbg(info->device, "ps3fb: mmap framebuffer P(%lx)->V(%lx)\n",
		offset, vma->vm_start);
	return 0;
}

    /*
     * Blank the display
     */

static int ps3fb_blank(int blank, struct fb_info *info)
{
	int retval;

	dev_dbg(info->device, "%s: blank:%d\n", __func__, blank);
	switch (blank) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		retval = ps3av_video_mute(1);	/* mute on */
		if (!retval)
			ps3fb.is_blanked = 1;
		break;

	default:		/* unblank */
		retval = ps3av_video_mute(0);	/* mute off */
		if (!retval)
			ps3fb.is_blanked = 0;
		break;
	}
	return retval;
}

static int ps3fb_get_vblank(struct fb_vblank *vblank)
{
	memset(vblank, 0, sizeof(*vblank));
	vblank->flags = FB_VBLANK_HAVE_VSYNC;
	return 0;
}

static int ps3fb_wait_for_vsync(u32 crtc)
{
	int ret;
	u64 count;

	count = ps3fb.vblank_count;
	ret = wait_event_interruptible_timeout(ps3fb.wait_vsync,
					       count != ps3fb.vblank_count,
					       HZ / 10);
	if (!ret)
		return -ETIMEDOUT;

	return 0;
}

static void ps3fb_flip_ctl(int on, void *data)
{
	struct ps3fb_priv *priv = data;
	if (on)
		atomic_dec_if_positive(&priv->ext_flip);
	else
		atomic_inc(&priv->ext_flip);
}


    /*
     * ioctl
     */

static int ps3fb_ioctl(struct fb_info *info, unsigned int cmd,
		       unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	u32 val;
	int retval = -EFAULT;

	switch (cmd) {
	case FBIOGET_VBLANK:
		{
			struct fb_vblank vblank;
			dev_dbg(info->device, "FBIOGET_VBLANK:\n");
			retval = ps3fb_get_vblank(&vblank);
			if (retval)
				break;

			if (copy_to_user(argp, &vblank, sizeof(vblank)))
				retval = -EFAULT;
			break;
		}

	case FBIO_WAITFORVSYNC:
		{
			u32 crt;
			dev_dbg(info->device, "FBIO_WAITFORVSYNC:\n");
			if (get_user(crt, (u32 __user *) arg))
				break;

			retval = ps3fb_wait_for_vsync(crt);
			break;
		}

	case PS3FB_IOCTL_SETMODE:
		{
			struct ps3fb_par *par = info->par;
			const struct fb_videomode *mode;
			struct fb_var_screeninfo var;

			if (copy_from_user(&val, argp, sizeof(val)))
				break;

			if (!(val & PS3AV_MODE_MASK)) {
				u32 id = ps3av_get_auto_mode();
				if (id > 0)
					val = (val & ~PS3AV_MODE_MASK) | id;
			}
			dev_dbg(info->device, "PS3FB_IOCTL_SETMODE:%x\n", val);
			retval = -EINVAL;
			mode = ps3fb_default_mode(val);
			if (mode) {
				var = info->var;
				fb_videomode_to_var(&var, mode);
				acquire_console_sem();
				info->flags |= FBINFO_MISC_USEREVENT;
				/* Force, in case only special bits changed */
				var.activate |= FB_ACTIVATE_FORCE;
				par->new_mode_id = val;
				retval = fb_set_var(info, &var);
				info->flags &= ~FBINFO_MISC_USEREVENT;
				release_console_sem();
			}
			break;
		}

	case PS3FB_IOCTL_GETMODE:
		val = ps3av_get_mode();
		dev_dbg(info->device, "PS3FB_IOCTL_GETMODE:%x\n", val);
		if (!copy_to_user(argp, &val, sizeof(val)))
			retval = 0;
		break;

	case PS3FB_IOCTL_SCREENINFO:
		{
			struct ps3fb_par *par = info->par;
			struct ps3fb_ioctl_res res;
			dev_dbg(info->device, "PS3FB_IOCTL_SCREENINFO:\n");
			res.xres = info->fix.line_length / BPP;
			res.yres = info->var.yres_virtual;
			res.xoff = (res.xres - info->var.xres) / 2;
			res.yoff = (res.yres - info->var.yres) / 2;
			res.num_frames = par->num_frames;
			if (!copy_to_user(argp, &res, sizeof(res)))
				retval = 0;
			break;
		}

	case PS3FB_IOCTL_ON:
		dev_dbg(info->device, "PS3FB_IOCTL_ON:\n");
		atomic_inc(&ps3fb.ext_flip);
		retval = 0;
		break;

	case PS3FB_IOCTL_OFF:
		dev_dbg(info->device, "PS3FB_IOCTL_OFF:\n");
		atomic_dec_if_positive(&ps3fb.ext_flip);
		retval = 0;
		break;

	case PS3FB_IOCTL_FSEL:
		if (copy_from_user(&val, argp, sizeof(val)))
			break;

		dev_dbg(info->device, "PS3FB_IOCTL_FSEL:%d\n", val);
		retval = ps3fb_sync(info, val);
		break;

	default:
		retval = -ENOIOCTLCMD;
		break;
	}
	return retval;
}

static int ps3fbd(void *arg)
{
	struct fb_info *info = arg;

	set_freezable();
	while (!kthread_should_stop()) {
		try_to_freeze();
		set_current_state(TASK_INTERRUPTIBLE);
		if (ps3fb.is_kicked) {
			ps3fb.is_kicked = 0;
			ps3fb_sync(info, 0);	/* single buffer */
		}
		schedule();
	}
	return 0;
}

static irqreturn_t ps3fb_vsync_interrupt(int irq, void *ptr)
{
	struct device *dev = ptr;
	u64 v1;
	int status;
	struct display_head *head = &ps3fb.dinfo->display_head[1];

	status = lv1_gpu_context_intr(ps3fb.context_handle, &v1);
	if (status) {
		dev_err(dev, "%s: lv1_gpu_context_intr failed: %d\n", __func__,
			status);
		return IRQ_NONE;
	}

	if (v1 & (1 << GPU_INTR_STATUS_VSYNC_1)) {
		/* VSYNC */
		ps3fb.vblank_count = head->vblank_count;
		if (ps3fb.task && !ps3fb.is_blanked &&
		    !atomic_read(&ps3fb.ext_flip)) {
			ps3fb.is_kicked = 1;
			wake_up_process(ps3fb.task);
		}
		wake_up_interruptible(&ps3fb.wait_vsync);
	}

	return IRQ_HANDLED;
}


static int ps3fb_vsync_settings(struct gpu_driver_info *dinfo,
				struct device *dev)
{
	int error;

	dev_dbg(dev, "version_driver:%x\n", dinfo->version_driver);
	dev_dbg(dev, "irq outlet:%x\n", dinfo->irq.irq_outlet);
	dev_dbg(dev,
		"version_gpu: %x memory_size: %x ch: %x core_freq: %d "
		"mem_freq:%d\n",
		dinfo->version_gpu, dinfo->memory_size, dinfo->hardware_channel,
		dinfo->nvcore_frequency/1000000, dinfo->memory_frequency/1000000);

	if (dinfo->version_driver != GPU_DRIVER_INFO_VERSION) {
		dev_err(dev, "%s: version_driver err:%x\n", __func__,
			dinfo->version_driver);
		return -EINVAL;
	}

	error = ps3_irq_plug_setup(PS3_BINDING_CPU_ANY, dinfo->irq.irq_outlet,
				   &ps3fb.irq_no);
	if (error) {
		dev_err(dev, "%s: ps3_alloc_irq failed %d\n", __func__, error);
		return error;
	}

	error = request_irq(ps3fb.irq_no, ps3fb_vsync_interrupt, IRQF_DISABLED,
			    DEVICE_NAME, dev);
	if (error) {
		dev_err(dev, "%s: request_irq failed %d\n", __func__, error);
		ps3_irq_plug_destroy(ps3fb.irq_no);
		return error;
	}

	dinfo->irq.mask = (1 << GPU_INTR_STATUS_VSYNC_1) |
			  (1 << GPU_INTR_STATUS_FLIP_1);
	return 0;
}

static int ps3fb_xdr_settings(u64 xdr_lpar, struct device *dev)
{
	int status;

	status = lv1_gpu_context_iomap(ps3fb.context_handle, GPU_IOIF,
				       xdr_lpar, ps3fb_videomemory.size, 0);
	if (status) {
		dev_err(dev, "%s: lv1_gpu_context_iomap failed: %d\n",
			__func__, status);
		return -ENXIO;
	}
	dev_dbg(dev,
		"video:%p xdr_ea:%p ioif:%lx lpar:%lx phys:%lx size:%lx\n",
		ps3fb_videomemory.address, ps3fb.xdr_ea, GPU_IOIF, xdr_lpar,
		virt_to_abs(ps3fb.xdr_ea), ps3fb_videomemory.size);

	status = lv1_gpu_context_attribute(ps3fb.context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_FB_SETUP,
					   xdr_lpar, GPU_CMD_BUF_SIZE,
					   GPU_IOIF, 0);
	if (status) {
		dev_err(dev,
			"%s: lv1_gpu_context_attribute FB_SETUP failed: %d\n",
			__func__, status);
		return -ENXIO;
	}
	return 0;
}

static struct fb_ops ps3fb_ops = {
	.fb_open	= ps3fb_open,
	.fb_release	= ps3fb_release,
	.fb_read        = fb_sys_read,
	.fb_write       = fb_sys_write,
	.fb_check_var	= ps3fb_check_var,
	.fb_set_par	= ps3fb_set_par,
	.fb_setcolreg	= ps3fb_setcolreg,
	.fb_pan_display	= ps3fb_pan_display,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
	.fb_mmap	= ps3fb_mmap,
	.fb_blank	= ps3fb_blank,
	.fb_ioctl	= ps3fb_ioctl,
	.fb_compat_ioctl = ps3fb_ioctl
};

static struct fb_fix_screeninfo ps3fb_fix __initdata = {
	.id =		DEVICE_NAME,
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.accel =	FB_ACCEL_NONE,
};

static int ps3fb_set_sync(struct device *dev)
{
	int status;

#ifdef HEAD_A
	status = lv1_gpu_context_attribute(0x0,
					   L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC,
					   0, L1GPU_DISPLAY_SYNC_VSYNC, 0, 0);
	if (status) {
		dev_err(dev,
			"%s: lv1_gpu_context_attribute DISPLAY_SYNC failed: "
			"%d\n",
			__func__, status);
		return -1;
	}
#endif
#ifdef HEAD_B
	status = lv1_gpu_context_attribute(0x0,
					   L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC,
					   1, L1GPU_DISPLAY_SYNC_VSYNC, 0, 0);

	if (status) {
		dev_err(dev,
			"%s: lv1_gpu_context_attribute DISPLAY_SYNC failed: "
			"%d\n",
			__func__, status);
		return -1;
	}
#endif
	return 0;
}

static int __devinit ps3fb_probe(struct ps3_system_bus_device *dev)
{
	struct fb_info *info;
	struct ps3fb_par *par;
	int retval = -ENOMEM;
	u32 xres, yres;
	u64 ddr_lpar = 0;
	u64 lpar_dma_control = 0;
	u64 lpar_driver_info = 0;
	u64 lpar_reports = 0;
	u64 lpar_reports_size = 0;
	u64 xdr_lpar;
	int status, res_index;
	struct task_struct *task;
	unsigned long max_ps3fb_size;

	if (ps3fb_videomemory.size < GPU_CMD_BUF_SIZE) {
		dev_err(&dev->core, "%s: Not enough video memory\n", __func__);
		return -ENOMEM;
	}

	status = ps3_open_hv_device(dev);
	if (status) {
		dev_err(&dev->core, "%s: ps3_open_hv_device failed\n",
			__func__);
		goto err;
	}

	if (!ps3fb_mode)
		ps3fb_mode = ps3av_get_mode();
	dev_dbg(&dev->core, "ps3av_mode:%d\n", ps3fb_mode);

	if (ps3fb_mode > 0 &&
	    !ps3av_video_mode2res(ps3fb_mode, &xres, &yres)) {
		res_index = ps3fb_get_res_table(xres, yres, ps3fb_mode);
		dev_dbg(&dev->core, "res_index:%d\n", res_index);
	} else
		res_index = GPU_RES_INDEX;

	atomic_set(&ps3fb.f_count, -1);	/* fbcon opens ps3fb */
	atomic_set(&ps3fb.ext_flip, 0);	/* for flip with vsync */
	init_waitqueue_head(&ps3fb.wait_vsync);

	ps3fb_set_sync(&dev->core);

	max_ps3fb_size = _ALIGN_UP(GPU_IOIF, 256*1024*1024) - GPU_IOIF;
	if (ps3fb_videomemory.size > max_ps3fb_size) {
		dev_info(&dev->core, "Limiting ps3fb mem size to %lu bytes\n",
			 max_ps3fb_size);
		ps3fb_videomemory.size = max_ps3fb_size;
	}

	/* get gpu context handle */
	status = lv1_gpu_memory_allocate(ps3fb_videomemory.size, 0, 0, 0, 0,
					 &ps3fb.memory_handle, &ddr_lpar);
	if (status) {
		dev_err(&dev->core, "%s: lv1_gpu_memory_allocate failed: %d\n",
			__func__, status);
		goto err;
	}
	dev_dbg(&dev->core, "ddr:lpar:0x%lx\n", ddr_lpar);

	status = lv1_gpu_context_allocate(ps3fb.memory_handle, 0,
					  &ps3fb.context_handle,
					  &lpar_dma_control, &lpar_driver_info,
					  &lpar_reports, &lpar_reports_size);
	if (status) {
		dev_err(&dev->core,
			"%s: lv1_gpu_context_attribute failed: %d\n", __func__,
			status);
		goto err_gpu_memory_free;
	}

	/* vsync interrupt */
	ps3fb.dinfo = ioremap(lpar_driver_info, 128 * 1024);
	if (!ps3fb.dinfo) {
		dev_err(&dev->core, "%s: ioremap failed\n", __func__);
		goto err_gpu_context_free;
	}

	retval = ps3fb_vsync_settings(ps3fb.dinfo, &dev->core);
	if (retval)
		goto err_iounmap_dinfo;

	/* XDR frame buffer */
	ps3fb.xdr_ea = ps3fb_videomemory.address;
	xdr_lpar = ps3_mm_phys_to_lpar(__pa(ps3fb.xdr_ea));

	/* Clear memory to prevent kernel info leakage into userspace */
	memset(ps3fb.xdr_ea, 0, ps3fb_videomemory.size);

	/*
	 * The GPU command buffer is at the start of video memory
	 * As we don't use the full command buffer, we can put the actual
	 * frame buffer at offset GPU_FB_START and save some precious XDR
	 * memory
	 */
	ps3fb.xdr_ea += GPU_FB_START;
	ps3fb.xdr_size = ps3fb_videomemory.size - GPU_FB_START;

	retval = ps3fb_xdr_settings(xdr_lpar, &dev->core);
	if (retval)
		goto err_free_irq;

	info = framebuffer_alloc(sizeof(struct ps3fb_par), &dev->core);
	if (!info)
		goto err_free_irq;

	par = info->par;
	par->mode_id = ~ps3fb_mode;	/* != ps3fb_mode, to trigger change */
	par->new_mode_id = ps3fb_mode;
	par->res_index = res_index;
	par->num_frames = 1;

	info->screen_base = (char __iomem *)ps3fb.xdr_ea;
	info->fbops = &ps3fb_ops;

	info->fix = ps3fb_fix;
	info->fix.smem_start = virt_to_abs(ps3fb.xdr_ea);
	info->fix.smem_len = ps3fb.xdr_size;
	info->pseudo_palette = par->pseudo_palette;
	info->flags = FBINFO_DEFAULT | FBINFO_READS_FAST |
		      FBINFO_HWACCEL_XPAN | FBINFO_HWACCEL_YPAN;

	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0)
		goto err_framebuffer_release;

	if (!fb_find_mode(&info->var, info, mode_option, ps3fb_modedb,
			  ARRAY_SIZE(ps3fb_modedb),
			  ps3fb_default_mode(par->new_mode_id), 32)) {
		retval = -EINVAL;
		goto err_fb_dealloc;
	}

	fb_videomode_to_modelist(ps3fb_modedb, ARRAY_SIZE(ps3fb_modedb),
				 &info->modelist);

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err_fb_dealloc;

	dev->core.driver_data = info;

	dev_info(info->device, "%s %s, using %lu KiB of video memory\n",
		 dev_driver_string(info->dev), info->dev->bus_id,
		 ps3fb.xdr_size >> 10);

	task = kthread_run(ps3fbd, info, DEVICE_NAME);
	if (IS_ERR(task)) {
		retval = PTR_ERR(task);
		goto err_unregister_framebuffer;
	}

	ps3fb.task = task;
	ps3av_register_flip_ctl(ps3fb_flip_ctl, &ps3fb);

	return 0;

err_unregister_framebuffer:
	unregister_framebuffer(info);
err_fb_dealloc:
	fb_dealloc_cmap(&info->cmap);
err_framebuffer_release:
	framebuffer_release(info);
err_free_irq:
	free_irq(ps3fb.irq_no, &dev->core);
	ps3_irq_plug_destroy(ps3fb.irq_no);
err_iounmap_dinfo:
	iounmap((u8 __iomem *)ps3fb.dinfo);
err_gpu_context_free:
	lv1_gpu_context_free(ps3fb.context_handle);
err_gpu_memory_free:
	lv1_gpu_memory_free(ps3fb.memory_handle);
err:
	return retval;
}

static int ps3fb_shutdown(struct ps3_system_bus_device *dev)
{
	int status;
	struct fb_info *info = dev->core.driver_data;

	dev_dbg(&dev->core, " -> %s:%d\n", __func__, __LINE__);

	ps3fb_flip_ctl(0, &ps3fb);	/* flip off */
	ps3fb.dinfo->irq.mask = 0;

	ps3av_register_flip_ctl(NULL, NULL);
	if (ps3fb.task) {
		struct task_struct *task = ps3fb.task;
		ps3fb.task = NULL;
		kthread_stop(task);
	}
	if (ps3fb.irq_no) {
		free_irq(ps3fb.irq_no, &dev->core);
		ps3_irq_plug_destroy(ps3fb.irq_no);
	}
	if (info) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
		info = dev->core.driver_data = NULL;
	}
	iounmap((u8 __iomem *)ps3fb.dinfo);

	status = lv1_gpu_context_free(ps3fb.context_handle);
	if (status)
		dev_dbg(&dev->core, "lv1_gpu_context_free failed: %d\n",
			status);

	status = lv1_gpu_memory_free(ps3fb.memory_handle);
	if (status)
		dev_dbg(&dev->core, "lv1_gpu_memory_free failed: %d\n",
			status);

	ps3_close_hv_device(dev);
	dev_dbg(&dev->core, " <- %s:%d\n", __func__, __LINE__);

	return 0;
}

static struct ps3_system_bus_driver ps3fb_driver = {
	.match_id	= PS3_MATCH_ID_GRAPHICS,
	.core.name	= DEVICE_NAME,
	.core.owner	= THIS_MODULE,
	.probe		= ps3fb_probe,
	.remove		= ps3fb_shutdown,
	.shutdown	= ps3fb_shutdown,
};

static int __init ps3fb_setup(void)
{
	char *options;

#ifdef MODULE
	return 0;
#endif

	if (fb_get_options(DEVICE_NAME, &options))
		return -ENXIO;

	if (!options || !*options)
		return 0;

	while (1) {
		char *this_opt = strsep(&options, ",");

		if (!this_opt)
			break;
		if (!*this_opt)
			continue;
		if (!strncmp(this_opt, "mode:", 5))
			ps3fb_mode = simple_strtoul(this_opt + 5, NULL, 0);
		else
			mode_option = this_opt;
	}
	return 0;
}

static int __init ps3fb_init(void)
{
	if (!ps3fb_videomemory.address ||  ps3fb_setup())
		return -ENXIO;

	return ps3_system_bus_driver_register(&ps3fb_driver);
}

static void __exit ps3fb_exit(void)
{
	pr_debug(" -> %s:%d\n", __func__, __LINE__);
	ps3_system_bus_driver_unregister(&ps3fb_driver);
	pr_debug(" <- %s:%d\n", __func__, __LINE__);
}

module_init(ps3fb_init);
module_exit(ps3fb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 GPU Frame Buffer Driver");
MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_ALIAS(PS3_MODULE_ALIAS_GRAPHICS);
