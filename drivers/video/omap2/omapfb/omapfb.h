/*
 * linux/drivers/video/omap2/omapfb.h
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DRIVERS_VIDEO_OMAP2_OMAPFB_H__
#define __DRIVERS_VIDEO_OMAP2_OMAPFB_H__

#ifdef CONFIG_FB_OMAP2_DEBUG_SUPPORT
#define DEBUG
#endif

#include <linux/rwsem.h>

#include <video/omapdss.h>

#ifdef DEBUG
extern unsigned int omapfb_debug;
#define DBG(format, ...) \
	do { \
		if (omapfb_debug) \
			printk(KERN_DEBUG "OMAPFB: " format, ## __VA_ARGS__); \
	} while (0)
#else
#define DBG(format, ...)
#endif

#define FB2OFB(fb_info) ((struct omapfb_info *)(fb_info->par))

/* max number of overlays to which a framebuffer data can be direct */
#define OMAPFB_MAX_OVL_PER_FB 3

struct omapfb2_mem_region {
	int             id;
	u32		paddr;
	void __iomem	*vaddr;
	struct vrfb	vrfb;
	unsigned long	size;
	u8		type;		/* OMAPFB_PLANE_MEM_* */
	bool		alloc;		/* allocated by the driver */
	bool		map;		/* kernel mapped by the driver */
	atomic_t	map_count;
	struct rw_semaphore lock;
	atomic_t	lock_count;
};

/* appended to fb_info */
struct omapfb_info {
	int id;
	struct omapfb2_mem_region *region;
	int num_overlays;
	struct omap_overlay *overlays[OMAPFB_MAX_OVL_PER_FB];
	struct omapfb2_device *fbdev;
	enum omap_dss_rotation_type rotation_type;
	u8 rotation[OMAPFB_MAX_OVL_PER_FB];
	bool mirror;
};

struct omapfb2_device {
	struct device *dev;
	struct mutex  mtx;

	u32 pseudo_palette[17];

	int state;

	unsigned num_fbs;
	struct fb_info *fbs[10];
	struct omapfb2_mem_region regions[10];

	unsigned num_displays;
	struct omap_dss_device *displays[10];
	unsigned num_overlays;
	struct omap_overlay *overlays[10];
	unsigned num_managers;
	struct omap_overlay_manager *managers[10];

	unsigned num_bpp_overrides;
	struct {
		struct omap_dss_device *dssdev;
		u8 bpp;
	} bpp_overrides[10];
};

struct omapfb_colormode {
	enum omap_color_mode dssmode;
	u32 bits_per_pixel;
	u32 nonstd;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
};

void set_fb_fix(struct fb_info *fbi);
int check_fb_var(struct fb_info *fbi, struct fb_var_screeninfo *var);
int omapfb_realloc_fbmem(struct fb_info *fbi, unsigned long size, int type);
int omapfb_apply_changes(struct fb_info *fbi, int init);

int omapfb_create_sysfs(struct omapfb2_device *fbdev);
void omapfb_remove_sysfs(struct omapfb2_device *fbdev);

int omapfb_ioctl(struct fb_info *fbi, unsigned int cmd, unsigned long arg);

int omapfb_update_window(struct fb_info *fbi,
		u32 x, u32 y, u32 w, u32 h);

int dss_mode_to_fb_mode(enum omap_color_mode dssmode,
			struct fb_var_screeninfo *var);

int omapfb_setup_overlay(struct fb_info *fbi, struct omap_overlay *ovl,
		u16 posx, u16 posy, u16 outw, u16 outh);

/* find the display connected to this fb, if any */
static inline struct omap_dss_device *fb2display(struct fb_info *fbi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	int i;

	/* XXX: returns the display connected to first attached overlay */
	for (i = 0; i < ofbi->num_overlays; i++) {
		if (ofbi->overlays[i]->manager)
			return ofbi->overlays[i]->manager->device;
	}

	return NULL;
}

static inline void omapfb_lock(struct omapfb2_device *fbdev)
{
	mutex_lock(&fbdev->mtx);
}

static inline void omapfb_unlock(struct omapfb2_device *fbdev)
{
	mutex_unlock(&fbdev->mtx);
}

static inline int omapfb_overlay_enable(struct omap_overlay *ovl,
		int enable)
{
	struct omap_overlay_info info;

	ovl->get_overlay_info(ovl, &info);
	if (info.enabled == enable)
		return 0;
	info.enabled = enable;
	return ovl->set_overlay_info(ovl, &info);
}

static inline struct omapfb2_mem_region *
omapfb_get_mem_region(struct omapfb2_mem_region *rg)
{
	down_read_nested(&rg->lock, rg->id);
	atomic_inc(&rg->lock_count);
	return rg;
}

static inline void omapfb_put_mem_region(struct omapfb2_mem_region *rg)
{
	atomic_dec(&rg->lock_count);
	up_read(&rg->lock);
}

#endif
