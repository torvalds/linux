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
#include <asm/cell-regs.h>
#include <asm/lv1call.h>
#include <asm/ps3av.h>
#include <asm/ps3fb.h>
#include <asm/ps3.h>
#include <asm/ps3gpu.h>


#define DEVICE_NAME		"ps3fb"

#define GPU_CMD_BUF_SIZE			(2 * 1024 * 1024)
#define GPU_FB_START				(64 * 1024)
#define GPU_IOIF				(0x0d000000UL)
#define GPU_ALIGN_UP(x)				_ALIGN_UP((x), 64)
#define GPU_MAX_LINE_LENGTH			(65536 - 64)

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
	unsigned int num_frames;	/* num of frame buffers */
	unsigned int width;
	unsigned int height;
	unsigned int ddr_line_length;
	unsigned int ddr_frame_size;
	unsigned int xdr_frame_size;
	unsigned int full_offset;	/* start of fullscreen DDR fb */
	unsigned int fb_offset;		/* start of actual DDR fb */
	unsigned int pan_offset;
};


#define FIRST_NATIVE_MODE_INDEX	10

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
        /* 1080i */
        "1080i", 50, 1688, 964, 13468, 264, 600, 94, 62, 88, 5,
        FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    },    {
        /* 1080p */
        "1080p", 50, 1688, 964, 6734, 264, 600, 94, 62, 88, 5,
        FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED
    },

    [FIRST_NATIVE_MODE_INDEX] =
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
	"1080if", 50, 1920, 1080, 13468, 148, 484, 36, 4, 88, 5,
	FB_SYNC_BROADCAST, FB_VMODE_INTERLACED
    }, {
	/* 1080pf */
	"1080pf", 50, 1920, 1080, 6734, 148, 484, 36, 4, 88, 5,
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
    }
};


#define HEAD_A
#define HEAD_B

#define BPP		4			/* number of bytes per pixel */


static int ps3fb_mode;
module_param(ps3fb_mode, int, 0);

static char *mode_option __devinitdata;

static int ps3fb_cmp_mode(const struct fb_videomode *vmode,
			  const struct fb_var_screeninfo *var)
{
	long xres, yres, left_margin, right_margin, upper_margin, lower_margin;
	long dx, dy;

	/* maximum values */
	if (var->xres > vmode->xres || var->yres > vmode->yres ||
	    var->pixclock > vmode->pixclock ||
	    var->hsync_len > vmode->hsync_len ||
	    var->vsync_len > vmode->vsync_len)
		return -1;

	/* progressive/interlaced must match */
	if ((var->vmode & FB_VMODE_MASK) != vmode->vmode)
		return -1;

	/* minimum resolution */
	xres = max(var->xres, 1U);
	yres = max(var->yres, 1U);

	/* minimum margins */
	left_margin = max(var->left_margin, vmode->left_margin);
	right_margin = max(var->right_margin, vmode->right_margin);
	upper_margin = max(var->upper_margin, vmode->upper_margin);
	lower_margin = max(var->lower_margin, vmode->lower_margin);

	/* resolution + margins may not exceed native parameters */
	dx = ((long)vmode->left_margin + (long)vmode->xres +
	      (long)vmode->right_margin) -
	     (left_margin + xres + right_margin);
	if (dx < 0)
		return -1;

	dy = ((long)vmode->upper_margin + (long)vmode->yres +
	      (long)vmode->lower_margin) -
	     (upper_margin + yres + lower_margin);
	if (dy < 0)
		return -1;

	/* exact match */
	if (!dx && !dy)
		return 0;

	/* resolution difference */
	return (vmode->xres - xres) * (vmode->yres - yres);
}

static const struct fb_videomode *ps3fb_native_vmode(enum ps3av_mode_num id)
{
	return &ps3fb_modedb[FIRST_NATIVE_MODE_INDEX + id - 1];
}

static const struct fb_videomode *ps3fb_vmode(int id)
{
	u32 mode = id & PS3AV_MODE_MASK;

	if (mode < PS3AV_MODE_480I || mode > PS3AV_MODE_WUXGA)
		return NULL;

	if (mode <= PS3AV_MODE_1080P50 && !(id & PS3AV_MODE_FULL)) {
		/* Non-fullscreen broadcast mode */
		return &ps3fb_modedb[mode - 1];
	}

	return ps3fb_native_vmode(mode);
}

static unsigned int ps3fb_find_mode(struct fb_var_screeninfo *var,
				    u32 *ddr_line_length, u32 *xdr_line_length)
{
	unsigned int id, best_id;
	int diff, best_diff;
	const struct fb_videomode *vmode;
	long gap;

	best_id = 0;
	best_diff = INT_MAX;
	pr_debug("%s: wanted %u [%u] %u x %u [%u] %u\n", __func__,
		 var->left_margin, var->xres, var->right_margin,
		 var->upper_margin, var->yres, var->lower_margin);
	for (id = PS3AV_MODE_480I; id <= PS3AV_MODE_WUXGA; id++) {
		vmode = ps3fb_native_vmode(id);
		diff = ps3fb_cmp_mode(vmode, var);
		pr_debug("%s: mode %u: %u [%u] %u x %u [%u] %u: diff = %d\n",
			 __func__, id, vmode->left_margin, vmode->xres,
			 vmode->right_margin, vmode->upper_margin,
			 vmode->yres, vmode->lower_margin, diff);
		if (diff < 0)
			continue;
		if (diff < best_diff) {
			best_id = id;
			if (!diff)
				break;
			best_diff = diff;
		}
	}

	if (!best_id) {
		pr_debug("%s: no suitable mode found\n", __func__);
		return 0;
	}

	id = best_id;
	vmode = ps3fb_native_vmode(id);

	*ddr_line_length = vmode->xres * BPP;

	/* minimum resolution */
	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;

	/* minimum virtual resolution */
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	/* minimum margins */
	if (var->left_margin < vmode->left_margin)
		var->left_margin = vmode->left_margin;
	if (var->right_margin < vmode->right_margin)
		var->right_margin = vmode->right_margin;
	if (var->upper_margin < vmode->upper_margin)
		var->upper_margin = vmode->upper_margin;
	if (var->lower_margin < vmode->lower_margin)
		var->lower_margin = vmode->lower_margin;

	/* extra margins */
	gap = ((long)vmode->left_margin + (long)vmode->xres +
	       (long)vmode->right_margin) -
	      ((long)var->left_margin + (long)var->xres +
	       (long)var->right_margin);
	if (gap > 0) {
		var->left_margin += gap/2;
		var->right_margin += (gap+1)/2;
		pr_debug("%s: rounded up H to %u [%u] %u\n", __func__,
			 var->left_margin, var->xres, var->right_margin);
	}

	gap = ((long)vmode->upper_margin + (long)vmode->yres +
	       (long)vmode->lower_margin) -
	      ((long)var->upper_margin + (long)var->yres +
	       (long)var->lower_margin);
	if (gap > 0) {
		var->upper_margin += gap/2;
		var->lower_margin += (gap+1)/2;
		pr_debug("%s: rounded up V to %u [%u] %u\n", __func__,
			 var->upper_margin, var->yres, var->lower_margin);
	}

	/* fixed fields */
	var->pixclock = vmode->pixclock;
	var->hsync_len = vmode->hsync_len;
	var->vsync_len = vmode->vsync_len;
	var->sync = vmode->sync;

	if (ps3_compare_firmware_version(1, 9, 0) >= 0) {
		*xdr_line_length = GPU_ALIGN_UP(var->xres_virtual * BPP);
		if (*xdr_line_length > GPU_MAX_LINE_LENGTH)
			*xdr_line_length = GPU_MAX_LINE_LENGTH;
	} else
		*xdr_line_length = *ddr_line_length;

	if (vmode->sync & FB_SYNC_BROADCAST) {
		/* Full broadcast modes have the full mode bit set */
		if (vmode->xres == var->xres && vmode->yres == var->yres)
			id |= PS3AV_MODE_FULL;
	}

	pr_debug("%s: mode %u\n", __func__, id);
	return id;
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

	mutex_lock(&ps3_gpu_mutex);
	status = lv1_gpu_fb_blit(ps3fb.context_handle, dst_offset,
				 GPU_IOIF + src_offset,
				 L1GPU_FB_BLIT_WAIT_FOR_COMPLETION |
				 (width << 16) | height,
				 line_length);
	mutex_unlock(&ps3_gpu_mutex);

	if (status)
		dev_err(dev, "%s: lv1_gpu_fb_blit failed: %d\n", __func__,
			status);
#ifdef HEAD_A
	status = lv1_gpu_display_flip(ps3fb.context_handle, 0, frame_offset);
	if (status)
		dev_err(dev, "%s: lv1_gpu_display_flip failed: %d\n", __func__,
			status);
#endif
#ifdef HEAD_B
	status = lv1_gpu_display_flip(ps3fb.context_handle, 1, frame_offset);
	if (status)
		dev_err(dev, "%s: lv1_gpu_display_flip failed: %d\n", __func__,
			status);
#endif
}

static int ps3fb_sync(struct fb_info *info, u32 frame)
{
	struct ps3fb_par *par = info->par;
	int error = 0;
	u64 ddr_base, xdr_base;

	if (frame > par->num_frames - 1) {
		dev_dbg(info->device, "%s: invalid frame number (%u)\n",
			__func__, frame);
		error = -EINVAL;
		goto out;
	}

	xdr_base = frame * par->xdr_frame_size;
	ddr_base = frame * par->ddr_frame_size;

	ps3fb_sync_image(info->device, ddr_base + par->full_offset,
			 ddr_base + par->fb_offset, xdr_base + par->pan_offset,
			 par->width, par->height, par->ddr_line_length,
			 info->fix.line_length);

out:
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
			if (console_trylock()) {
				ps3fb_sync(info, 0);	/* single buffer */
				console_unlock();
			}
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

	mode = ps3fb_find_mode(var, &ddr_line_length, &xdr_line_length);
	if (!mode)
		return -EINVAL;

	/* Virtual screen */
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
	if (var->yres_virtual * xdr_line_length > info->fix.smem_len) {
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
	unsigned int ddr_xoff, ddr_yoff, offset;
	const struct fb_videomode *vmode;
	u64 dst;

	mode = ps3fb_find_mode(&info->var, &ddr_line_length, &xdr_line_length);
	if (!mode)
		return -EINVAL;

	vmode = ps3fb_native_vmode(mode & PS3AV_MODE_MASK);

	info->fix.xpanstep = info->var.xres_virtual > info->var.xres ? 1 : 0;
	info->fix.ypanstep = info->var.yres_virtual > info->var.yres ? 1 : 0;
	info->fix.line_length = xdr_line_length;

	par->ddr_line_length = ddr_line_length;
	par->ddr_frame_size = vmode->yres * ddr_line_length;
	par->xdr_frame_size = info->var.yres_virtual * xdr_line_length;

	par->num_frames = info->fix.smem_len /
			  max(par->ddr_frame_size, par->xdr_frame_size);

	/* Keep the special bits we cannot set using fb_var_screeninfo */
	par->new_mode_id = (par->new_mode_id & ~PS3AV_MODE_MASK) | mode;

	par->width = info->var.xres;
	par->height = info->var.yres;

	/* Start of the virtual frame buffer (relative to fullscreen) */
	ddr_xoff = info->var.left_margin - vmode->left_margin;
	ddr_yoff = info->var.upper_margin - vmode->upper_margin;
	offset = ddr_yoff * ddr_line_length + ddr_xoff * BPP;

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
	memset((void __force *)info->screen_base, 0, info->fix.smem_len);

	/* Clear DDR frame buffer memory */
	lines = vmode->yres * par->num_frames;
	if (par->full_offset)
		lines++;
	maxlines = info->fix.smem_len / ddr_line_length;
	for (dst = 0; lines; dst += maxlines * ddr_line_length) {
		unsigned int l = min(lines, maxlines);
		ps3fb_sync_image(info->device, 0, dst, 0, vmode->xres, l,
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
			const struct fb_videomode *vmode;
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
			vmode = ps3fb_vmode(val);
			if (vmode) {
				var = info->var;
				fb_videomode_to_var(&var, vmode);
				console_lock();
				info->flags |= FBINFO_MISC_USEREVENT;
				/* Force, in case only special bits changed */
				var.activate |= FB_ACTIVATE_FORCE;
				par->new_mode_id = val;
				retval = fb_set_var(info, &var);
				info->flags &= ~FBINFO_MISC_USEREVENT;
				console_unlock();
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
		console_lock();
		retval = ps3fb_sync(info, val);
		console_unlock();
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
			console_lock();
			ps3fb_sync(info, 0);	/* single buffer */
			console_unlock();
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

static int __devinit ps3fb_probe(struct ps3_system_bus_device *dev)
{
	struct fb_info *info;
	struct ps3fb_par *par;
	int retval;
	u64 ddr_lpar = 0;
	u64 lpar_dma_control = 0;
	u64 lpar_driver_info = 0;
	u64 lpar_reports = 0;
	u64 lpar_reports_size = 0;
	u64 xdr_lpar;
	struct gpu_driver_info *dinfo;
	void *fb_start;
	int status;
	struct task_struct *task;
	unsigned long max_ps3fb_size;

	if (ps3fb_videomemory.size < GPU_CMD_BUF_SIZE) {
		dev_err(&dev->core, "%s: Not enough video memory\n", __func__);
		return -ENOMEM;
	}

	retval = ps3_open_hv_device(dev);
	if (retval) {
		dev_err(&dev->core, "%s: ps3_open_hv_device failed\n",
			__func__);
		goto err;
	}

	if (!ps3fb_mode)
		ps3fb_mode = ps3av_get_mode();
	dev_dbg(&dev->core, "ps3fb_mode: %d\n", ps3fb_mode);

	atomic_set(&ps3fb.f_count, -1);	/* fbcon opens ps3fb */
	atomic_set(&ps3fb.ext_flip, 0);	/* for flip with vsync */
	init_waitqueue_head(&ps3fb.wait_vsync);

#ifdef HEAD_A
	status = lv1_gpu_display_sync(0x0, 0, L1GPU_DISPLAY_SYNC_VSYNC);
	if (status) {
		dev_err(&dev->core, "%s: lv1_gpu_display_sync failed: %d\n",
			__func__, status);
		retval = -ENODEV;
		goto err_close_device;
	}
#endif
#ifdef HEAD_B
	status = lv1_gpu_display_sync(0x0, 1, L1GPU_DISPLAY_SYNC_VSYNC);
	if (status) {
		dev_err(&dev->core, "%s: lv1_gpu_display_sync failed: %d\n",
			__func__, status);
		retval = -ENODEV;
		goto err_close_device;
	}
#endif

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
		goto err_close_device;
	}
	dev_dbg(&dev->core, "ddr:lpar:0x%llx\n", ddr_lpar);

	status = lv1_gpu_context_allocate(ps3fb.memory_handle, 0,
					  &ps3fb.context_handle,
					  &lpar_dma_control, &lpar_driver_info,
					  &lpar_reports, &lpar_reports_size);
	if (status) {
		dev_err(&dev->core,
			"%s: lv1_gpu_context_allocate failed: %d\n", __func__,
			status);
		goto err_gpu_memory_free;
	}

	/* vsync interrupt */
	dinfo = (void __force *)ioremap(lpar_driver_info, 128 * 1024);
	if (!dinfo) {
		dev_err(&dev->core, "%s: ioremap failed\n", __func__);
		goto err_gpu_context_free;
	}

	ps3fb.dinfo = dinfo;
	dev_dbg(&dev->core, "version_driver:%x\n", dinfo->version_driver);
	dev_dbg(&dev->core, "irq outlet:%x\n", dinfo->irq.irq_outlet);
	dev_dbg(&dev->core, "version_gpu: %x memory_size: %x ch: %x "
		"core_freq: %d mem_freq:%d\n", dinfo->version_gpu,
		dinfo->memory_size, dinfo->hardware_channel,
		dinfo->nvcore_frequency/1000000,
		dinfo->memory_frequency/1000000);

	if (dinfo->version_driver != GPU_DRIVER_INFO_VERSION) {
		dev_err(&dev->core, "%s: version_driver err:%x\n", __func__,
			dinfo->version_driver);
		retval = -EINVAL;
		goto err_iounmap_dinfo;
	}

	retval = ps3_irq_plug_setup(PS3_BINDING_CPU_ANY, dinfo->irq.irq_outlet,
				    &ps3fb.irq_no);
	if (retval) {
		dev_err(&dev->core, "%s: ps3_alloc_irq failed %d\n", __func__,
			retval);
		goto err_iounmap_dinfo;
	}

	retval = request_irq(ps3fb.irq_no, ps3fb_vsync_interrupt,
			     0, DEVICE_NAME, &dev->core);
	if (retval) {
		dev_err(&dev->core, "%s: request_irq failed %d\n", __func__,
			retval);
		goto err_destroy_plug;
	}

	dinfo->irq.mask = (1 << GPU_INTR_STATUS_VSYNC_1) |
			  (1 << GPU_INTR_STATUS_FLIP_1);

	/* Clear memory to prevent kernel info leakage into userspace */
	memset(ps3fb_videomemory.address, 0, ps3fb_videomemory.size);

	xdr_lpar = ps3_mm_phys_to_lpar(__pa(ps3fb_videomemory.address));

	status = lv1_gpu_context_iomap(ps3fb.context_handle, GPU_IOIF,
				       xdr_lpar, ps3fb_videomemory.size,
				       CBE_IOPTE_PP_W | CBE_IOPTE_PP_R |
				       CBE_IOPTE_M);
	if (status) {
		dev_err(&dev->core, "%s: lv1_gpu_context_iomap failed: %d\n",
			__func__, status);
		retval =  -ENXIO;
		goto err_free_irq;
	}

	dev_dbg(&dev->core, "video:%p ioif:%lx lpar:%llx size:%lx\n",
		ps3fb_videomemory.address, GPU_IOIF, xdr_lpar,
		ps3fb_videomemory.size);

	status = lv1_gpu_fb_setup(ps3fb.context_handle, xdr_lpar,
				  GPU_CMD_BUF_SIZE, GPU_IOIF);
	if (status) {
		dev_err(&dev->core, "%s: lv1_gpu_fb_setup failed: %d\n",
			__func__, status);
		retval = -ENXIO;
		goto err_context_unmap;
	}

	info = framebuffer_alloc(sizeof(struct ps3fb_par), &dev->core);
	if (!info)
		goto err_context_fb_close;

	par = info->par;
	par->mode_id = ~ps3fb_mode;	/* != ps3fb_mode, to trigger change */
	par->new_mode_id = ps3fb_mode;
	par->num_frames = 1;

	info->fbops = &ps3fb_ops;
	info->fix = ps3fb_fix;

	/*
	 * The GPU command buffer is at the start of video memory
	 * As we don't use the full command buffer, we can put the actual
	 * frame buffer at offset GPU_FB_START and save some precious XDR
	 * memory
	 */
	fb_start = ps3fb_videomemory.address + GPU_FB_START;
	info->screen_base = (char __force __iomem *)fb_start;
	info->fix.smem_start = virt_to_abs(fb_start);
	info->fix.smem_len = ps3fb_videomemory.size - GPU_FB_START;

	info->pseudo_palette = par->pseudo_palette;
	info->flags = FBINFO_DEFAULT | FBINFO_READS_FAST |
		      FBINFO_HWACCEL_XPAN | FBINFO_HWACCEL_YPAN;

	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0)
		goto err_framebuffer_release;

	if (!fb_find_mode(&info->var, info, mode_option, ps3fb_modedb,
			  ARRAY_SIZE(ps3fb_modedb),
			  ps3fb_vmode(par->new_mode_id), 32)) {
		retval = -EINVAL;
		goto err_fb_dealloc;
	}

	fb_videomode_to_modelist(ps3fb_modedb, ARRAY_SIZE(ps3fb_modedb),
				 &info->modelist);

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err_fb_dealloc;

	ps3_system_bus_set_drvdata(dev, info);

	dev_info(info->device, "%s %s, using %u KiB of video memory\n",
		 dev_driver_string(info->dev), dev_name(info->dev),
		 info->fix.smem_len >> 10);

	task = kthread_run(ps3fbd, info, DEVICE_NAME);
	if (IS_ERR(task)) {
		retval = PTR_ERR(task);
		goto err_unregister_framebuffer;
	}

	ps3fb.task = task;

	return 0;

err_unregister_framebuffer:
	unregister_framebuffer(info);
err_fb_dealloc:
	fb_dealloc_cmap(&info->cmap);
err_framebuffer_release:
	framebuffer_release(info);
err_context_fb_close:
	lv1_gpu_fb_close(ps3fb.context_handle);
err_context_unmap:
	lv1_gpu_context_iomap(ps3fb.context_handle, GPU_IOIF, xdr_lpar,
			      ps3fb_videomemory.size, CBE_IOPTE_M);
err_free_irq:
	free_irq(ps3fb.irq_no, &dev->core);
err_destroy_plug:
	ps3_irq_plug_destroy(ps3fb.irq_no);
err_iounmap_dinfo:
	iounmap((u8 __force __iomem *)ps3fb.dinfo);
err_gpu_context_free:
	lv1_gpu_context_free(ps3fb.context_handle);
err_gpu_memory_free:
	lv1_gpu_memory_free(ps3fb.memory_handle);
err_close_device:
	ps3_close_hv_device(dev);
err:
	return retval;
}

static int ps3fb_shutdown(struct ps3_system_bus_device *dev)
{
	struct fb_info *info = ps3_system_bus_get_drvdata(dev);
	u64 xdr_lpar = ps3_mm_phys_to_lpar(__pa(ps3fb_videomemory.address));

	dev_dbg(&dev->core, " -> %s:%d\n", __func__, __LINE__);

	atomic_inc(&ps3fb.ext_flip);	/* flip off */
	ps3fb.dinfo->irq.mask = 0;

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
		ps3_system_bus_set_drvdata(dev, NULL);
	}
	iounmap((u8 __force __iomem *)ps3fb.dinfo);
	lv1_gpu_fb_close(ps3fb.context_handle);
	lv1_gpu_context_iomap(ps3fb.context_handle, GPU_IOIF, xdr_lpar,
			      ps3fb_videomemory.size, CBE_IOPTE_M);
	lv1_gpu_context_free(ps3fb.context_handle);
	lv1_gpu_memory_free(ps3fb.memory_handle);
	ps3_close_hv_device(dev);
	dev_dbg(&dev->core, " <- %s:%d\n", __func__, __LINE__);

	return 0;
}

static struct ps3_system_bus_driver ps3fb_driver = {
	.match_id	= PS3_MATCH_ID_GPU,
	.match_sub_id	= PS3_MATCH_SUB_ID_GPU_FB,
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
MODULE_ALIAS(PS3_MODULE_ALIAS_GPU_FB);
