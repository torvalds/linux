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

struct mga_i2c_chan {
	struct i2c_adapter adapter;
	struct drm_device *dev;
	struct i2c_algo_bit_data bit;
	int data, clock;
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

#define IS_G200_SE(mdev) (mdev->type == G200_SE_A || mdev->type == G200_SE_B)

struct mgag200_device_info {
	u16 max_hdisplay;
	u16 max_vdisplay;

	/*
	 * Maximum memory bandwidth (MiB/sec). Setting this to zero disables
	 * the rsp test during mode validation.
	 */
	unsigned long max_mem_bandwidth;

	/* HW has external source (e.g., BMC) to synchronize with */
	bool has_vidrst:1;

	struct {
		unsigned data_bit:3;
		unsigned clock_bit:3;
	} i2c;

	/*
	 * HW does not handle 'startadd' register correctly. Always set
	 * it's value to 0.
	 */
	bool bug_no_startadd:1;
};

#define MGAG200_DEVICE_INFO_INIT(_max_hdisplay, _max_vdisplay, _max_mem_bandwidth, \
				 _has_vidrst, _i2c_data_bit, _i2c_clock_bit, \
				 _bug_no_startadd) \
	{ \
		.max_hdisplay = (_max_hdisplay), \
		.max_vdisplay = (_max_vdisplay), \
		.max_mem_bandwidth = (_max_mem_bandwidth), \
		.has_vidrst = (_has_vidrst), \
		.i2c = { \
			.data_bit = (_i2c_data_bit), \
			.clock_bit = (_i2c_clock_bit), \
		}, \
		.bug_no_startadd = (_bug_no_startadd), \
	}

struct mga_device {
	struct drm_device base;

	const struct mgag200_device_info *info;

	struct resource			*rmmio_res;
	void __iomem			*rmmio;
	struct mutex			rmmio_lock; /* Protects access to rmmio */

	struct resource			*vram_res;
	void __iomem			*vram;
	resource_size_t			vram_available;

	enum mga_type			type;

	struct mgag200_pll pixpll;
	struct mga_i2c_chan i2c;
	struct drm_connector connector;
	struct drm_simple_display_pipe display_pipe;
};

static inline struct mga_device *to_mga_device(struct drm_device *dev)
{
	return container_of(dev, struct mga_device, base);
}

struct mgag200_g200_device {
	struct mga_device base;

	/* PLL constants */
	long ref_clk;
	long pclk_min;
	long pclk_max;
};

static inline struct mgag200_g200_device *to_mgag200_g200_device(struct drm_device *dev)
{
	return container_of(to_mga_device(dev), struct mgag200_g200_device, base);
}

struct mgag200_g200se_device {
	struct mga_device base;

	/* SE model number stored in reg 0x1e24 */
	u32 unique_rev_id;
};

static inline struct mgag200_g200se_device *to_mgag200_g200se_device(struct drm_device *dev)
{
	return container_of(to_mga_device(dev), struct mgag200_g200se_device, base);
}

				/* mgag200_drv.c */
int mgag200_init_pci_options(struct pci_dev *pdev, u32 option, u32 option2);
resource_size_t mgag200_probe_vram(void __iomem *mem, resource_size_t size);
resource_size_t mgag200_device_probe_vram(struct mga_device *mdev);
int mgag200_device_preinit(struct mga_device *mdev);
int mgag200_device_init(struct mga_device *mdev, enum mga_type type,
			const struct mgag200_device_info *info);

				/* mgag200_<device type>.c */
struct mga_device *mgag200_g200_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
					      enum mga_type type);
struct mga_device *mgag200_g200se_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type);
struct mga_device *mgag200_g200wb_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type);
struct mga_device *mgag200_g200ev_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type);
struct mga_device *mgag200_g200eh_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type);
struct mga_device *mgag200_g200eh3_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						 enum mga_type type);
struct mga_device *mgag200_g200er_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type);
struct mga_device *mgag200_g200ew3_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						 enum mga_type type);

				/* mgag200_mode.c */
resource_size_t mgag200_device_probe_vram(struct mga_device *mdev);
int mgag200_modeset_init(struct mga_device *mdev, resource_size_t vram_fb_available);

				/* mgag200_i2c.c */
int mgag200_i2c_init(struct mga_device *mdev, struct mga_i2c_chan *i2c);

				/* mgag200_pll.c */
int mgag200_pixpll_init(struct mgag200_pll *pixpll, struct mga_device *mdev);

#endif				/* __MGAG200_DRV_H__ */
