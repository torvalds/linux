/*
 * Copyright 2010 Matt Turner.
 * Copyright 2012 Red Hat 
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 * 	    Matt Turner
 *	    Dave Airlie
 */
#ifndef __MGAG200_DRV_H__
#define __MGAG200_DRV_H__

#include <video/vga.h>

#include <drm/drm_fb_helper.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_memory.h>
#include <drm/ttm/ttm_module.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include "mgag200_reg.h"

#define DRIVER_AUTHOR		"Matthew Garrett"

#define DRIVER_NAME		"mgag200"
#define DRIVER_DESC		"MGA G200 SE"
#define DRIVER_DATE		"20110418"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

#define MGAG200FB_CONN_LIMIT 1

#define RREG8(reg) ioread8(((void __iomem *)mdev->rmmio) + (reg))
#define WREG8(reg, v) iowrite8(v, ((void __iomem *)mdev->rmmio) + (reg))
#define RREG32(reg) ioread32(((void __iomem *)mdev->rmmio) + (reg))
#define WREG32(reg, v) iowrite32(v, ((void __iomem *)mdev->rmmio) + (reg))

#define ATTR_INDEX 0x1fc0
#define ATTR_DATA 0x1fc1

#define WREG_ATTR(reg, v)					\
	do {							\
		RREG8(0x1fda);					\
		WREG8(ATTR_INDEX, reg);				\
		WREG8(ATTR_DATA, v);				\
	} while (0)						\

#define WREG_SEQ(reg, v)					\
	do {							\
		WREG8(MGAREG_SEQ_INDEX, reg);			\
		WREG8(MGAREG_SEQ_DATA, v);			\
	} while (0)						\

#define WREG_CRT(reg, v)					\
	do {							\
		WREG8(MGAREG_CRTC_INDEX, reg);			\
		WREG8(MGAREG_CRTC_DATA, v);			\
	} while (0)						\


#define WREG_ECRT(reg, v)					\
	do {							\
		WREG8(MGAREG_CRTCEXT_INDEX, reg);				\
		WREG8(MGAREG_CRTCEXT_DATA, v);				\
	} while (0)						\

#define GFX_INDEX 0x1fce
#define GFX_DATA 0x1fcf

#define WREG_GFX(reg, v)					\
	do {							\
		WREG8(GFX_INDEX, reg);				\
		WREG8(GFX_DATA, v);				\
	} while (0)						\

#define DAC_INDEX 0x3c00
#define DAC_DATA 0x3c0a

#define WREG_DAC(reg, v)					\
	do {							\
		WREG8(DAC_INDEX, reg);				\
		WREG8(DAC_DATA, v);				\
	} while (0)						\

#define MGA_MISC_OUT 0x1fc2
#define MGA_MISC_IN 0x1fcc

#define MGAG200_MAX_FB_HEIGHT 4096
#define MGAG200_MAX_FB_WIDTH 4096

#define MATROX_DPMS_CLEARED (-1)

#define to_mga_crtc(x) container_of(x, struct mga_crtc, base)
#define to_mga_encoder(x) container_of(x, struct mga_encoder, base)
#define to_mga_connector(x) container_of(x, struct mga_connector, base)
#define to_mga_framebuffer(x) container_of(x, struct mga_framebuffer, base)

struct mga_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
};

struct mga_fbdev {
	struct drm_fb_helper helper;
	struct mga_framebuffer mfb;
	void *sysram;
	int size;
	struct ttm_bo_kmap_obj mapping;
	int x1, y1, x2, y2; /* dirty rect */
	spinlock_t dirty_lock;
};

struct mga_crtc {
	struct drm_crtc base;
	u8 lut_r[256], lut_g[256], lut_b[256];
	int last_dpms;
	bool enabled;
};

struct mga_mode_info {
	bool mode_config_initialized;
	struct mga_crtc *crtc;
};

struct mga_encoder {
	struct drm_encoder base;
	int last_dpms;
};


struct mga_i2c_chan {
	struct i2c_adapter adapter;
	struct drm_device *dev;
	struct i2c_algo_bit_data bit;
	int data, clock;
};

struct mga_connector {
	struct drm_connector base;
	struct mga_i2c_chan *i2c;
};

struct mga_cursor {
	/*
	   We have to have 2 buffers for the cursor to avoid occasional
	   corruption while switching cursor icons.
	   If either of these is NULL, then don't do hardware cursors, and
	   fall back to software.
	*/
	struct mgag200_bo *pixels_1;
	struct mgag200_bo *pixels_2;
	u64 pixels_1_gpu_addr, pixels_2_gpu_addr;
	/* The currently displayed icon, this points to one of pixels_1, or pixels_2 */
	struct mgag200_bo *pixels_current;
	/* The previously displayed icon */
	struct mgag200_bo *pixels_prev;
};

struct mga_mc {
	resource_size_t			vram_size;
	resource_size_t			vram_base;
	resource_size_t			vram_window;
};

enum mga_type {
	G200_SE_A,
	G200_SE_B,
	G200_WB,
	G200_EV,
	G200_EH,
	G200_ER,
};

#define IS_G200_SE(mdev) (mdev->type == G200_SE_A || mdev->type == G200_SE_B)

struct mga_device {
	struct drm_device		*dev;
	unsigned long			flags;

	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	void __iomem			*rmmio;

	drm_local_map_t			*framebuffer;

	struct mga_mc			mc;
	struct mga_mode_info		mode_info;

	struct mga_fbdev *mfbdev;
	struct mga_cursor cursor;

	bool				suspended;
	int				num_crtc;
	enum mga_type			type;
	int				has_sdram;
	struct drm_display_mode		mode;

	int bpp_shifts[4];

	int fb_mtrr;

	struct {
		struct drm_global_reference mem_global_ref;
		struct ttm_bo_global_ref bo_global_ref;
		struct ttm_bo_device bdev;
	} ttm;

	/* SE model number stored in reg 0x1e24 */
	u32 unique_rev_id;
};


struct mgag200_bo {
	struct ttm_buffer_object bo;
	struct ttm_placement placement;
	struct ttm_bo_kmap_obj kmap;
	struct drm_gem_object gem;
	u32 placements[3];
	int pin_count;
};
#define gem_to_mga_bo(gobj) container_of((gobj), struct mgag200_bo, gem)

static inline struct mgag200_bo *
mgag200_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct mgag200_bo, bo);
}
				/* mgag200_crtc.c */
void mga_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			     u16 blue, int regno);
void mga_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			     u16 *blue, int regno);

				/* mgag200_mode.c */
int mgag200_modeset_init(struct mga_device *mdev);
void mgag200_modeset_fini(struct mga_device *mdev);

				/* mgag200_fb.c */
int mgag200_fbdev_init(struct mga_device *mdev);
void mgag200_fbdev_fini(struct mga_device *mdev);

				/* mgag200_main.c */
int mgag200_framebuffer_init(struct drm_device *dev,
			     struct mga_framebuffer *mfb,
			     struct drm_mode_fb_cmd2 *mode_cmd,
			     struct drm_gem_object *obj);


int mgag200_driver_load(struct drm_device *dev, unsigned long flags);
int mgag200_driver_unload(struct drm_device *dev);
int mgag200_gem_create(struct drm_device *dev,
		   u32 size, bool iskernel,
		       struct drm_gem_object **obj);
int mgag200_gem_init_object(struct drm_gem_object *obj);
int mgag200_dumb_create(struct drm_file *file,
			struct drm_device *dev,
			struct drm_mode_create_dumb *args);
int mgag200_dumb_destroy(struct drm_file *file,
			 struct drm_device *dev,
			 uint32_t handle);
void mgag200_gem_free_object(struct drm_gem_object *obj);
int
mgag200_dumb_mmap_offset(struct drm_file *file,
			 struct drm_device *dev,
			 uint32_t handle,
			 uint64_t *offset);
				/* mgag200_i2c.c */
struct mga_i2c_chan *mgag200_i2c_create(struct drm_device *dev);
void mgag200_i2c_destroy(struct mga_i2c_chan *i2c);

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)
void mgag200_ttm_placement(struct mgag200_bo *bo, int domain);

int mgag200_bo_reserve(struct mgag200_bo *bo, bool no_wait);
void mgag200_bo_unreserve(struct mgag200_bo *bo);
int mgag200_bo_create(struct drm_device *dev, int size, int align,
		      uint32_t flags, struct mgag200_bo **pastbo);
int mgag200_mm_init(struct mga_device *mdev);
void mgag200_mm_fini(struct mga_device *mdev);
int mgag200_mmap(struct file *filp, struct vm_area_struct *vma);
int mgag200_bo_pin(struct mgag200_bo *bo, u32 pl_flag, u64 *gpu_addr);
int mgag200_bo_unpin(struct mgag200_bo *bo);
int mgag200_bo_push_sysram(struct mgag200_bo *bo);
			   /* mgag200_cursor.c */
int mga_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
						uint32_t handle, uint32_t width, uint32_t height);
int mga_crtc_cursor_move(struct drm_crtc *crtc, int x, int y);

#endif				/* __MGAG200_DRV_H__ */
