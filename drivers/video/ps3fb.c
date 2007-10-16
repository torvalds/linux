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
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/ioctl.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <linux/uaccess.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/time.h>

#include <asm/abs_addr.h>
#include <asm/lv1call.h>
#include <asm/ps3av.h>
#include <asm/ps3fb.h>
#include <asm/ps3.h>


#define DEVICE_NAME		"ps3fb"

#ifdef PS3FB_DEBUG
#define DPRINTK(fmt, args...) printk("%s: " fmt, __func__ , ##args)
#else
#define DPRINTK(fmt, args...)
#endif

#define L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC	0x101
#define L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP	0x102
#define L1GPU_CONTEXT_ATTRIBUTE_FB_SETUP	0x600
#define L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT		0x601
#define L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT_SYNC	0x602

#define L1GPU_FB_BLIT_WAIT_FOR_COMPLETION	(1ULL << 32)

#define L1GPU_DISPLAY_SYNC_HSYNC		1
#define L1GPU_DISPLAY_SYNC_VSYNC		2

#define DDR_SIZE				(0)	/* used no ddr */
#define GPU_OFFSET				(64 * 1024)
#define GPU_IOIF				(0x0d000000UL)

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
	struct gpu_driver_info *dinfo;
	u32 res_index;

	u64 vblank_count;	/* frame count */
	wait_queue_head_t wait_vsync;

	u32 num_frames;		/* num of frame buffers */
	atomic_t ext_flip;	/* on/off flip with vsync */
	atomic_t f_count;	/* fb_open count */
	int is_blanked;
	int is_kicked;
	struct task_struct *task;
};
static struct ps3fb_priv ps3fb;

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
#define BPP	4		/* number of bytes per pixel */
#define VP_OFF(i)	(WIDTH(i) * Y_OFF(i) * BPP + X_OFF(i) * BPP)
#define FB_OFF(i)	(GPU_OFFSET - VP_OFF(i) % GPU_OFFSET)

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
			DPRINTK("ERROR: ps3fb_get_res_table()\n");
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
				    u32 *line_length)
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
		    (var->vmode & FB_VMODE_MASK) == ps3fb_modedb[i].vmode) {
			/* Cropped broadcast modes use the full line_length */
			*line_length =
			    ps3fb_modedb[i < 10 ? i + 13 : i].xres * 4;
			/* Full broadcast modes have the full mode bit set */
			mode = i > 12 ? (i - 12) | PS3FB_FULL_MODE_BIT : i + 1;

			DPRINTK("ps3fb_find_mode: mode %u\n", mode);
			return mode;
		}

	DPRINTK("ps3fb_find_mode: mode not found\n");
	return 0;

}

static const struct fb_videomode *ps3fb_default_mode(void)
{
	u32 mode = ps3fb_mode & PS3AV_MODE_MASK;
	u32 flags;

	if (mode < 1 || mode > 13)
		return NULL;

	flags = ps3fb_mode & ~PS3AV_MODE_MASK;

	if (mode <= 10 && flags & PS3FB_FULL_MODE_BIT) {
		/* Full broadcast mode */
		return &ps3fb_modedb[mode + 12];
	}

	return &ps3fb_modedb[mode - 1];
}

static int ps3fb_sync(u32 frame)
{
	int i, status;
	u32 xres, yres;
	u64 fb_ioif, offset;

	i = ps3fb.res_index;
	xres = ps3fb_res[i].xres;
	yres = ps3fb_res[i].yres;

	if (frame > ps3fb.num_frames - 1) {
		printk(KERN_WARNING "%s: invalid frame number (%u)\n",
		       __func__, frame);
		return -EINVAL;
	}
	offset = xres * yres * BPP * frame;

	fb_ioif = GPU_IOIF + FB_OFF(i) + offset;
	status = lv1_gpu_context_attribute(ps3fb.context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_FB_BLIT,
					   offset, fb_ioif,
					   L1GPU_FB_BLIT_WAIT_FOR_COMPLETION |
					   (xres << 16) | yres,
					   xres * BPP);	/* line_length */
	if (status)
		printk(KERN_ERR
		       "%s: lv1_gpu_context_attribute FB_BLIT failed: %d\n",
		       __func__, status);
#ifdef HEAD_A
	status = lv1_gpu_context_attribute(ps3fb.context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP,
					   0, offset, 0, 0);
	if (status)
		printk(KERN_ERR
		       "%s: lv1_gpu_context_attribute FLIP failed: %d\n",
		       __func__, status);
#endif
#ifdef HEAD_B
	status = lv1_gpu_context_attribute(ps3fb.context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_FLIP,
					   1, offset, 0, 0);
	if (status)
		printk(KERN_ERR
		       "%s: lv1_gpu_context_attribute FLIP failed: %d\n",
		       __func__, status);
#endif
	return 0;
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
			ps3fb_sync(0);	/* single buffer */
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
	u32 line_length;
	int mode;
	int i;

	DPRINTK("var->xres:%u info->var.xres:%u\n", var->xres, info->var.xres);
	DPRINTK("var->yres:%u info->var.yres:%u\n", var->yres, info->var.yres);

	/* FIXME For now we do exact matches only */
	mode = ps3fb_find_mode(var, &line_length);
	if (!mode)
		return -EINVAL;

	/*
	 *  FB_VMODE_CONUPDATE and FB_VMODE_SMOOTH_XPAN are equal!
	 *  as FB_VMODE_SMOOTH_XPAN is only used internally
	 */

	if (var->vmode & FB_VMODE_CONUPDATE) {
		var->vmode |= FB_VMODE_YWRAP;
		var->xoffset = info->var.xoffset;
		var->yoffset = info->var.yoffset;
	}

	/* Virtual screen and panning are not supported */
	if (var->xres_virtual > var->xres || var->yres_virtual > var->yres ||
	    var->xoffset || var->yoffset) {
		DPRINTK("Virtual screen and panning are not supported\n");
		return -EINVAL;
	}

	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;

	/* We support ARGB8888 only */
	if (var->bits_per_pixel > 32 || var->grayscale ||
	    var->red.offset > 16 || var->green.offset > 8 ||
	    var->blue.offset > 0 || var->transp.offset > 24 ||
	    var->red.length > 8 || var->green.length > 8 ||
	    var->blue.length > 8 || var->transp.length > 8 ||
	    var->red.msb_right || var->green.msb_right ||
	    var->blue.msb_right || var->transp.msb_right || var->nonstd) {
		DPRINTK("We support ARGB8888 only\n");
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
		DPRINTK("Rotation is not supported\n");
		return -EINVAL;
	}

	/* Memory limit */
	i = ps3fb_get_res_table(var->xres, var->yres, mode);
	if (ps3fb_res[i].xres*ps3fb_res[i].yres*BPP > ps3fb_videomemory.size) {
		DPRINTK("Not enough memory\n");
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
	unsigned int mode;
	int i;
	unsigned long offset;
	static int first = 1;

	DPRINTK("xres:%d xv:%d yres:%d yv:%d clock:%d\n",
		info->var.xres, info->var.xres_virtual,
		info->var.yres, info->var.yres_virtual, info->var.pixclock);

	mode = ps3fb_find_mode(&info->var, &info->fix.line_length);
	if (!mode)
		return -EINVAL;

	i = ps3fb_get_res_table(info->var.xres, info->var.yres, mode);
	ps3fb.res_index = i;

	offset = FB_OFF(i) + VP_OFF(i);
	info->fix.smem_len = ps3fb_videomemory.size - offset;
	info->screen_base = (char __iomem *)ps3fb.xdr_ea + offset;
	memset(ps3fb.xdr_ea, 0, ps3fb_videomemory.size);

	ps3fb.num_frames = ps3fb_videomemory.size/
			   (ps3fb_res[i].xres*ps3fb_res[i].yres*BPP);

	/* Keep the special bits we cannot set using fb_var_screeninfo */
	ps3fb_mode = (ps3fb_mode & ~PS3AV_MODE_MASK) | mode;

	if (ps3av_set_video_mode(ps3fb_mode, first))
		return -EINVAL;

	first = 0;
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

    /*
     *  As we have a virtual frame buffer, we need our own mmap function
     */

static int ps3fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long size, offset;
	int i;

	i = ps3fb_get_res_table(info->var.xres, info->var.yres, ps3fb_mode);
	if (i == -1)
		return -EINVAL;

	size = vma->vm_end - vma->vm_start;
	offset = vma->vm_pgoff << PAGE_SHIFT;
	if (offset + size > info->fix.smem_len)
		return -EINVAL;

	offset += info->fix.smem_start + FB_OFF(i) + VP_OFF(i);
	if (remap_pfn_range(vma, vma->vm_start, offset >> PAGE_SHIFT,
			    size, vma->vm_page_prot))
		return -EAGAIN;

	printk(KERN_DEBUG "ps3fb: mmap framebuffer P(%lx)->V(%lx)\n", offset,
	       vma->vm_start);
	return 0;
}

    /*
     * Blank the display
     */

static int ps3fb_blank(int blank, struct fb_info *info)
{
	int retval;

	DPRINTK("%s: blank:%d\n", __func__, blank);
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
	memset(vblank, 0, sizeof(&vblank));
	vblank->flags = FB_VBLANK_HAVE_VSYNC;
	return 0;
}

int ps3fb_wait_for_vsync(u32 crtc)
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

EXPORT_SYMBOL_GPL(ps3fb_wait_for_vsync);

void ps3fb_flip_ctl(int on, void *data)
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
	u32 val, old_mode;
	int retval = -EFAULT;

	switch (cmd) {
	case FBIOGET_VBLANK:
		{
			struct fb_vblank vblank;
			DPRINTK("FBIOGET_VBLANK:\n");
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
			DPRINTK("FBIO_WAITFORVSYNC:\n");
			if (get_user(crt, (u32 __user *) arg))
				break;

			retval = ps3fb_wait_for_vsync(crt);
			break;
		}

	case PS3FB_IOCTL_SETMODE:
		{
			const struct fb_videomode *mode;
			struct fb_var_screeninfo var;

			if (copy_from_user(&val, argp, sizeof(val)))
				break;

			if (!(val & PS3AV_MODE_MASK)) {
				u32 id = ps3av_get_auto_mode(0);
				if (id > 0)
					val = (val & ~PS3AV_MODE_MASK) | id;
			}
			DPRINTK("PS3FB_IOCTL_SETMODE:%x\n", val);
			retval = -EINVAL;
			old_mode = ps3fb_mode;
			ps3fb_mode = val;
			mode = ps3fb_default_mode();
			if (mode) {
				var = info->var;
				fb_videomode_to_var(&var, mode);
				acquire_console_sem();
				info->flags |= FBINFO_MISC_USEREVENT;
				/* Force, in case only special bits changed */
				var.activate |= FB_ACTIVATE_FORCE;
				retval = fb_set_var(info, &var);
				info->flags &= ~FBINFO_MISC_USEREVENT;
				release_console_sem();
			}
			if (retval)
				ps3fb_mode = old_mode;
			break;
		}

	case PS3FB_IOCTL_GETMODE:
		val = ps3av_get_mode();
		DPRINTK("PS3FB_IOCTL_GETMODE:%x\n", val);
		if (!copy_to_user(argp, &val, sizeof(val)))
			retval = 0;
		break;

	case PS3FB_IOCTL_SCREENINFO:
		{
			struct ps3fb_ioctl_res res;
			int i = ps3fb.res_index;
			DPRINTK("PS3FB_IOCTL_SCREENINFO:\n");
			res.xres = ps3fb_res[i].xres;
			res.yres = ps3fb_res[i].yres;
			res.xoff = ps3fb_res[i].xoff;
			res.yoff = ps3fb_res[i].yoff;
			res.num_frames = ps3fb.num_frames;
			if (!copy_to_user(argp, &res, sizeof(res)))
				retval = 0;
			break;
		}

	case PS3FB_IOCTL_ON:
		DPRINTK("PS3FB_IOCTL_ON:\n");
		atomic_inc(&ps3fb.ext_flip);
		retval = 0;
		break;

	case PS3FB_IOCTL_OFF:
		DPRINTK("PS3FB_IOCTL_OFF:\n");
		atomic_dec_if_positive(&ps3fb.ext_flip);
		retval = 0;
		break;

	case PS3FB_IOCTL_FSEL:
		if (copy_from_user(&val, argp, sizeof(val)))
			break;

		DPRINTK("PS3FB_IOCTL_FSEL:%d\n", val);
		retval = ps3fb_sync(val);
		break;

	default:
		retval = -ENOIOCTLCMD;
		break;
	}
	return retval;
}

static int ps3fbd(void *arg)
{
	set_freezable();
	while (!kthread_should_stop()) {
		try_to_freeze();
		set_current_state(TASK_INTERRUPTIBLE);
		if (ps3fb.is_kicked) {
			ps3fb.is_kicked = 0;
			ps3fb_sync(0);	/* single buffer */
		}
		schedule();
	}
	return 0;
}

static irqreturn_t ps3fb_vsync_interrupt(int irq, void *ptr)
{
	u64 v1;
	int status;
	struct display_head *head = &ps3fb.dinfo->display_head[1];

	status = lv1_gpu_context_intr(ps3fb.context_handle, &v1);
	if (status) {
		printk(KERN_ERR "%s: lv1_gpu_context_intr failed: %d\n",
		       __func__, status);
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
				struct ps3_system_bus_device *dev)
{
	int error;

	DPRINTK("version_driver:%x\n", dinfo->version_driver);
	DPRINTK("irq outlet:%x\n", dinfo->irq.irq_outlet);
	DPRINTK("version_gpu:%x memory_size:%x ch:%x core_freq:%d mem_freq:%d\n",
		dinfo->version_gpu, dinfo->memory_size, dinfo->hardware_channel,
		dinfo->nvcore_frequency/1000000, dinfo->memory_frequency/1000000);

	if (dinfo->version_driver != GPU_DRIVER_INFO_VERSION) {
		printk(KERN_ERR "%s: version_driver err:%x\n", __func__,
		       dinfo->version_driver);
		return -EINVAL;
	}

	error = ps3_irq_plug_setup(PS3_BINDING_CPU_ANY, dinfo->irq.irq_outlet,
				   &ps3fb.irq_no);
	if (error) {
		printk(KERN_ERR "%s: ps3_alloc_irq failed %d\n", __func__,
		       error);
		return error;
	}

	error = request_irq(ps3fb.irq_no, ps3fb_vsync_interrupt, IRQF_DISABLED,
			    DEVICE_NAME, dev);
	if (error) {
		printk(KERN_ERR "%s: request_irq failed %d\n", __func__,
		       error);
		ps3_irq_plug_destroy(ps3fb.irq_no);
		return error;
	}

	dinfo->irq.mask = (1 << GPU_INTR_STATUS_VSYNC_1) |
			  (1 << GPU_INTR_STATUS_FLIP_1);
	return 0;
}

static int ps3fb_xdr_settings(u64 xdr_lpar)
{
	int status;

	status = lv1_gpu_context_iomap(ps3fb.context_handle, GPU_IOIF,
				       xdr_lpar, ps3fb_videomemory.size, 0);
	if (status) {
		printk(KERN_ERR "%s: lv1_gpu_context_iomap failed: %d\n",
		       __func__, status);
		return -ENXIO;
	}
	DPRINTK("video:%p xdr_ea:%p ioif:%lx lpar:%lx phys:%lx size:%lx\n",
		ps3fb_videomemory.address, ps3fb.xdr_ea, GPU_IOIF, xdr_lpar,
		virt_to_abs(ps3fb.xdr_ea), ps3fb_videomemory.size);

	status = lv1_gpu_context_attribute(ps3fb.context_handle,
					   L1GPU_CONTEXT_ATTRIBUTE_FB_SETUP,
					   xdr_lpar, ps3fb_videomemory.size,
					   GPU_IOIF, 0);
	if (status) {
		printk(KERN_ERR
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

static int ps3fb_set_sync(void)
{
	int status;

#ifdef HEAD_A
	status = lv1_gpu_context_attribute(0x0,
					   L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC,
					   0, L1GPU_DISPLAY_SYNC_VSYNC, 0, 0);
	if (status) {
		printk(KERN_ERR "%s: lv1_gpu_context_attribute DISPLAY_SYNC "
		       "failed: %d\n", __func__, status);
		return -1;
	}
#endif
#ifdef HEAD_B
	status = lv1_gpu_context_attribute(0x0,
					   L1GPU_CONTEXT_ATTRIBUTE_DISPLAY_SYNC,
					   1, L1GPU_DISPLAY_SYNC_VSYNC, 0, 0);

	if (status) {
		printk(KERN_ERR "%s: lv1_gpu_context_attribute DISPLAY_MODE "
		       "failed: %d\n", __func__, status);
		return -1;
	}
#endif
	return 0;
}

static int __devinit ps3fb_probe(struct ps3_system_bus_device *dev)
{
	struct fb_info *info;
	int retval = -ENOMEM;
	u32 xres, yres;
	u64 ddr_lpar = 0;
	u64 lpar_dma_control = 0;
	u64 lpar_driver_info = 0;
	u64 lpar_reports = 0;
	u64 lpar_reports_size = 0;
	u64 xdr_lpar;
	int status;
	unsigned long offset;
	struct task_struct *task;

	status = ps3_open_hv_device(dev);
	if (status) {
		printk(KERN_ERR "%s: ps3_open_hv_device failed\n", __func__);
		goto err;
	}

	if (!ps3fb_mode)
		ps3fb_mode = ps3av_get_mode();
	DPRINTK("ps3av_mode:%d\n", ps3fb_mode);

	if (ps3fb_mode > 0 &&
	    !ps3av_video_mode2res(ps3fb_mode, &xres, &yres)) {
		ps3fb.res_index = ps3fb_get_res_table(xres, yres, ps3fb_mode);
		DPRINTK("res_index:%d\n", ps3fb.res_index);
	} else
		ps3fb.res_index = GPU_RES_INDEX;

	atomic_set(&ps3fb.f_count, -1);	/* fbcon opens ps3fb */
	atomic_set(&ps3fb.ext_flip, 0);	/* for flip with vsync */
	init_waitqueue_head(&ps3fb.wait_vsync);
	ps3fb.num_frames = 1;

	ps3fb_set_sync();

	/* get gpu context handle */
	status = lv1_gpu_memory_allocate(DDR_SIZE, 0, 0, 0, 0,
					 &ps3fb.memory_handle, &ddr_lpar);
	if (status) {
		printk(KERN_ERR "%s: lv1_gpu_memory_allocate failed: %d\n",
		       __func__, status);
		goto err;
	}
	DPRINTK("ddr:lpar:0x%lx\n", ddr_lpar);

	status = lv1_gpu_context_allocate(ps3fb.memory_handle, 0,
					  &ps3fb.context_handle,
					  &lpar_dma_control, &lpar_driver_info,
					  &lpar_reports, &lpar_reports_size);
	if (status) {
		printk(KERN_ERR "%s: lv1_gpu_context_attribute failed: %d\n",
		       __func__, status);
		goto err_gpu_memory_free;
	}

	/* vsync interrupt */
	ps3fb.dinfo = ioremap(lpar_driver_info, 128 * 1024);
	if (!ps3fb.dinfo) {
		printk(KERN_ERR "%s: ioremap failed\n", __func__);
		goto err_gpu_context_free;
	}

	retval = ps3fb_vsync_settings(ps3fb.dinfo, dev);
	if (retval)
		goto err_iounmap_dinfo;

	/* xdr frame buffer */
	ps3fb.xdr_ea = ps3fb_videomemory.address;
	xdr_lpar = ps3_mm_phys_to_lpar(__pa(ps3fb.xdr_ea));
	retval = ps3fb_xdr_settings(xdr_lpar);
	if (retval)
		goto err_free_irq;

	/*
	 * ps3fb must clear memory to prevent kernel info
	 * leakage into userspace
	 */
	memset(ps3fb.xdr_ea, 0, ps3fb_videomemory.size);
	info = framebuffer_alloc(sizeof(u32) * 16, &dev->core);
	if (!info)
		goto err_free_irq;

	offset = FB_OFF(ps3fb.res_index) + VP_OFF(ps3fb.res_index);
	info->screen_base = (char __iomem *)ps3fb.xdr_ea + offset;
	info->fbops = &ps3fb_ops;

	info->fix = ps3fb_fix;
	info->fix.smem_start = virt_to_abs(ps3fb.xdr_ea);
	info->fix.smem_len = ps3fb_videomemory.size - offset;
	info->pseudo_palette = info->par;
	info->par = NULL;
	info->flags = FBINFO_DEFAULT | FBINFO_READS_FAST;

	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0)
		goto err_framebuffer_release;

	if (!fb_find_mode(&info->var, info, mode_option, ps3fb_modedb,
			  ARRAY_SIZE(ps3fb_modedb), ps3fb_default_mode(), 32)) {
		retval = -EINVAL;
		goto err_fb_dealloc;
	}

	fb_videomode_to_modelist(ps3fb_modedb, ARRAY_SIZE(ps3fb_modedb),
				 &info->modelist);

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err_fb_dealloc;

	dev->core.driver_data = info;

	printk(KERN_INFO
	       "fb%d: PS3 frame buffer device, using %ld KiB of video memory\n",
	       info->node, ps3fb_videomemory.size >> 10);

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
	free_irq(ps3fb.irq_no, dev);
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

	DPRINTK(" -> %s:%d\n", __func__, __LINE__);

	ps3fb_flip_ctl(0, &ps3fb);	/* flip off */
	ps3fb.dinfo->irq.mask = 0;

	if (info) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}

	ps3av_register_flip_ctl(NULL, NULL);
	if (ps3fb.task) {
		struct task_struct *task = ps3fb.task;
		ps3fb.task = NULL;
		kthread_stop(task);
	}
	if (ps3fb.irq_no) {
		free_irq(ps3fb.irq_no, dev);
		ps3_irq_plug_destroy(ps3fb.irq_no);
	}
	iounmap((u8 __iomem *)ps3fb.dinfo);

	status = lv1_gpu_context_free(ps3fb.context_handle);
	if (status)
		DPRINTK("lv1_gpu_context_free failed: %d\n", status);

	status = lv1_gpu_memory_free(ps3fb.memory_handle);
	if (status)
		DPRINTK("lv1_gpu_memory_free failed: %d\n", status);

	ps3_close_hv_device(dev);
	DPRINTK(" <- %s:%d\n", __func__, __LINE__);

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
	DPRINTK(" -> %s:%d\n", __func__, __LINE__);
	ps3_system_bus_driver_unregister(&ps3fb_driver);
	DPRINTK(" <- %s:%d\n", __func__, __LINE__);
}

module_init(ps3fb_init);
module_exit(ps3fb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 GPU Frame Buffer Driver");
MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_ALIAS(PS3_MODULE_ALIAS_GRAPHICS);
