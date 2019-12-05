/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2010 Matt Turner.
 * Copyright 2012 Red Hat
 *
 * Authors: Matthew Garrett
 * 	    Matt Turner
 *	    Dave Airlie
 */
#ifndef __MGAG200_DRV_H__
#define __MGAG200_DRV_H__

#include <linux/i2c-algo-bit.h>
#include <linux/i2c.h>

#include <video/vga.h>

#include <drm/drm_encoder.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_vram_helper.h>

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
	struct drm_gem_vram_object *gbo[2];
	unsigned int next_index;
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
	G200_EH3,
	G200_ER,
	G200_EW3,
};

/* HW does not handle 'startadd' field correct. */
#define MGAG200_FLAG_HW_BUG_NO_STARTADD	(1ul << 8)

#define MGAG200_TYPE_MASK	(0x000000ff)
#define MGAG200_FLAG_MASK	(0x00ffff00)

#define IS_G200_SE(mdev) (mdev->type == G200_SE_A || mdev->type == G200_SE_B)

struct mga_device {
	struct drm_device		*dev;
	unsigned long			flags;

	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	void __iomem			*rmmio;

	struct mga_mc			mc;
	struct mga_mode_info		mode_info;

	struct mga_cursor cursor;

	size_t vram_fb_available;

	bool				suspended;
	int				num_crtc;
	enum mga_type			type;
	int				has_sdram;
	struct drm_display_mode		mode;

	int bpp_shifts[4];

	int fb_mtrr;

	/* SE model number stored in reg 0x1e24 */
	u32 unique_rev_id;
};

static inline enum mga_type
mgag200_type_from_driver_data(kernel_ulong_t driver_data)
{
	return (enum mga_type)(driver_data & MGAG200_TYPE_MASK);
}

static inline unsigned long
mgag200_flags_from_driver_data(kernel_ulong_t driver_data)
{
	return driver_data & MGAG200_FLAG_MASK;
}

				/* mgag200_mode.c */
int mgag200_modeset_init(struct mga_device *mdev);
void mgag200_modeset_fini(struct mga_device *mdev);

				/* mgag200_main.c */
int mgag200_driver_load(struct drm_device *dev, unsigned long flags);
void mgag200_driver_unload(struct drm_device *dev);

				/* mgag200_i2c.c */
struct mga_i2c_chan *mgag200_i2c_create(struct drm_device *dev);
void mgag200_i2c_destroy(struct mga_i2c_chan *i2c);

int mgag200_mm_init(struct mga_device *mdev);
void mgag200_mm_fini(struct mga_device *mdev);
int mgag200_mmap(struct file *filp, struct vm_area_struct *vma);

int mgag200_cursor_init(struct mga_device *mdev);
void mgag200_cursor_fini(struct mga_device *mdev);
int mgag200_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
			    uint32_t handle, uint32_t width, uint32_t height);
int mgag200_crtc_cursor_move(struct drm_crtc *crtc, int x, int y);

#endif				/* __MGAG200_DRV_H__ */
