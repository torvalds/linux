/* SPDX-License-Identifier: GPL-2.0 */
/* sbuslib.h: SBUS fb helper library interfaces */
#ifndef _SBUSLIB_H
#define _SBUSLIB_H

struct device_node;
struct fb_info;
struct fb_var_screeninfo;
struct vm_area_struct;

struct sbus_mmap_map {
	unsigned long voff;
	unsigned long poff;
	unsigned long size;
};

#define SBUS_MMAP_FBSIZE(n) (-n)
#define SBUS_MMAP_EMPTY	0x80000000

extern void sbusfb_fill_var(struct fb_var_screeninfo *var,
			    struct device_node *dp, int bpp);
extern int sbusfb_mmap_helper(struct sbus_mmap_map *map,
			      unsigned long physbase, unsigned long fbsize,
			      unsigned long iospace,
			      struct vm_area_struct *vma);
int sbusfb_ioctl_helper(unsigned long cmd, unsigned long arg,
			struct fb_info *info,
			int type, int fb_depth, unsigned long fb_size);
int sbusfb_compat_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg);

/*
 * Initialize struct fb_ops for SBUS I/O.
 */

#define __FB_DEFAULT_SBUS_OPS_RDWR(__prefix) \
	.fb_read	= fb_io_read, \
	.fb_write	= fb_io_write

#define __FB_DEFAULT_SBUS_OPS_DRAW(__prefix) \
	.fb_fillrect	= cfb_fillrect, \
	.fb_copyarea	= cfb_copyarea, \
	.fb_imageblit	= cfb_imageblit

#ifdef CONFIG_COMPAT
#define __FB_DEFAULT_SBUS_OPS_IOCTL(__prefix) \
	.fb_ioctl		= __prefix ## _sbusfb_ioctl, \
	.fb_compat_ioctl	= sbusfb_compat_ioctl
#else
#define __FB_DEFAULT_SBUS_OPS_IOCTL(__prefix) \
	.fb_ioctl	= __prefix ## _sbusfb_ioctl
#endif

#define __FB_DEFAULT_SBUS_OPS_MMAP(__prefix) \
	.fb_mmap	= __prefix ## _sbusfb_mmap

#define FB_DEFAULT_SBUS_OPS(__prefix) \
	__FB_DEFAULT_SBUS_OPS_RDWR(__prefix), \
	__FB_DEFAULT_SBUS_OPS_DRAW(__prefix), \
	__FB_DEFAULT_SBUS_OPS_IOCTL(__prefix), \
	__FB_DEFAULT_SBUS_OPS_MMAP(__prefix)

#endif /* _SBUSLIB_H */
