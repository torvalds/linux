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
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "mgag200_reg.h"

#define DRIVER_AUTHOR		"Matthew Garrett"

#define DRIVER_NAME		"mgag200"
#define DRIVER_DESC		"MGA G200 SE"
#define DRIVER_DATE		"20110418"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

#define RREG8(reg) ioread8(((void __iomem *)mdev->rmmio) + (reg))
#define WREG8(reg, v) iowrite8(v, ((void __iomem *)mdev->rmmio) + (reg))
#define RREG32(reg) ioread32(((void __iomem *)mdev->rmmio) + (reg))
#define WREG32(reg, v) iowrite32(v, ((void __iomem *)mdev->rmmio) + (reg))

#define MGA_BIOS_OFFSET		0x7ffc

#define ATTR_INDEX 0x1fc0
#define ATTR_DATA 0x1fc1

#define WREG_MISC(v)						\
	WREG8(MGA_MISC_OUT, v)

#define RREG_MISC(v)						\
	((v) = RREG8(MGA_MISC_IN))

#define WREG_MISC_MASKED(v, mask)				\
	do {							\
		u8 misc_;					\
		u8 mask_ = (mask);				\
		RREG_MISC(misc_);				\
		misc_ &= ~mask_;				\
		misc_ |= ((v) & mask_);				\
		WREG_MISC(misc_);				\
	} while (0)

#define WREG_ATTR(reg, v)					\
	do {							\
		RREG8(0x1fda);					\
		WREG8(ATTR_INDEX, reg);				\
		WREG8(ATTR_DATA, v);				\
	} while (0)						\

#define RREG_SEQ(reg, v)					\
	do {							\
		WREG8(MGAREG_SEQ_INDEX, reg);			\
		v = RREG8(MGAREG_SEQ_DATA);			\
	} while (0)						\

#define WREG_SEQ(reg, v)					\
	do {							\
		WREG8(MGAREG_SEQ_INDEX, reg);			\
		WREG8(MGAREG_SEQ_DATA, v);			\
	} while (0)						\

#define RREG_CRT(reg, v)					\
	do {							\
		WREG8(MGAREG_CRTC_INDEX, reg);			\
		v = RREG8(MGAREG_CRTC_DATA);			\
	} while (0)						\

#define WREG_CRT(reg, v)					\
	do {							\
		WREG8(MGAREG_CRTC_INDEX, reg);			\
		WREG8(MGAREG_CRTC_DATA, v);			\
	} while (0)						\

#define RREG_ECRT(reg, v)					\
	do {							\
		WREG8(MGAREG_CRTCEXT_INDEX, reg);		\
		v = RREG8(MGAREG_CRTCEXT_DATA);			\
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

struct mga_device;
struct mgag200_pll;

/*
 * Stores parameters for programming the PLLs
 *
 * Fref: reference frequency (A: 25.175 Mhz, B: 28.361, C: XX Mhz)
 * Fo: output frequency
 * Fvco = Fref * (N / M)
 * Fo = Fvco / P
 *
 * S = [0..3]
 */
struct mgag200_pll_values {
	unsigned int m;
	unsigned int n;
	unsigned int p;
	unsigned int s;
};

struct mgag200_pll_funcs {
	int (*compute)(struct mgag200_pll *pll, long clock, struct mgag200_pll_values *pllc);
	void (*update)(struct mgag200_pll *pll, const struct mgag200_pll_values *pllc);
};

struct mgag200_pll {
	struct mga_device *mdev;

	const struct mgag200_pll_funcs *funcs;
};

struct mgag200_crtc_state {
	struct drm_crtc_state base;

	struct mgag200_pll_values pixpllc;
};

static inline struct mgag200_crtc_state *to_mgag200_crtc_state(struct drm_crtc_state *base)
{
	return container_of(base, struct mgag200_crtc_state, base);
}

#define to_mga_connector(x) container_of(x, struct mga_connector, base)

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

struct mga_mc {
	resource_size_t			vram_size;
	resource_size_t			vram_base;
	resource_size_t			vram_window;
};

enum mga_type {
	G200_PCI,
	G200_AGP,
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
	struct drm_device		base;
	unsigned long			flags;

	resource_size_t			rmmio_base;
	resource_size_t			rmmio_size;
	void __iomem			*rmmio;

	struct mga_mc			mc;

	void __iomem			*vram;
	size_t				vram_fb_available;

	enum mga_type			type;

	int fb_mtrr;

	union {
		struct {
			long ref_clk;
			long pclk_min;
			long pclk_max;
		} g200;
		struct {
			/* SE model number stored in reg 0x1e24 */
			u32 unique_rev_id;
		} g200se;
	} model;

	struct mga_connector connector;
	struct mgag200_pll pixpll;
	struct drm_simple_display_pipe display_pipe;
};

static inline struct mga_device *to_mga_device(struct drm_device *dev)
{
	return container_of(dev, struct mga_device, base);
}

				/* mgag200_mode.c */
int mgag200_modeset_init(struct mga_device *mdev);

				/* mgag200_i2c.c */
struct mga_i2c_chan *mgag200_i2c_create(struct drm_device *dev);
void mgag200_i2c_destroy(struct mga_i2c_chan *i2c);

				/* mgag200_mm.c */
int mgag200_mm_init(struct mga_device *mdev);

				/* mgag200_pll.c */
int mgag200_pixpll_init(struct mgag200_pll *pixpll, struct mga_device *mdev);

#endif				/* __MGAG200_DRV_H__ */
