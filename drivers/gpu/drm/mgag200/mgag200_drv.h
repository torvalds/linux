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

#include <video/vga.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_plane.h>

#include "mgag200_reg.h"

#define DRIVER_AUTHOR		"Matthew Garrett"

#define DRIVER_NAME		"mgag200"
#define DRIVER_DESC		"MGA G200 SE"

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

/*
 * TODO: This is a pretty large set of default values for all kinds of
 *       settings. It should be split and set in the various DRM helpers,
 *       such as the CRTC reset or atomic_enable helpers. The PLL values
 *       probably belong to each model's PLL code.
 */
#define MGAG200_DAC_DEFAULT(xvrefctrl, xpixclkctrl, xmiscctrl, xsyspllm, xsysplln, xsyspllp)	\
	/* 0x00: */        0,    0,    0,    0,    0,    0, 0x00,    0,				\
	/* 0x08: */        0,    0,    0,    0,    0,    0,    0,    0,				\
	/* 0x10: */        0,    0,    0,    0,    0,    0,    0,    0,				\
	/* 0x18: */     (xvrefctrl),								\
	/* 0x19: */        0,									\
	/* 0x1a: */     (xpixclkctrl),								\
	/* 0x1b: */     0xff, 0xbf, 0x20,							\
	/* 0x1e: */	(xmiscctrl),								\
	/* 0x1f: */	0x20,									\
	/* 0x20: */     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,				\
	/* 0x28: */     0x00, 0x00, 0x00, 0x00,							\
	/* 0x2c: */     (xsyspllm),								\
	/* 0x2d: */     (xsysplln),								\
	/* 0x2e: */     (xsyspllp),								\
	/* 0x2f: */     0x40,									\
	/* 0x30: */     0x00, 0xb0, 0x00, 0xc2, 0x34, 0x14, 0x02, 0x83,				\
	/* 0x38: */     0x00, 0x93, 0x00, 0x77, 0x00, 0x00, 0x00, 0x3a,				\
	/* 0x40: */        0,    0,    0,    0,    0,    0,    0,    0,				\
	/* 0x48: */        0,    0,    0,    0,    0,    0,    0,    0				\

#define MGAG200_LUT_SIZE 256

#define MGAG200_MAX_FB_HEIGHT 4096
#define MGAG200_MAX_FB_WIDTH 4096

struct mga_device;

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

struct mgag200_crtc_state {
	struct drm_crtc_state base;

	/* Primary-plane format; required for modesetting and color mgmt. */
	const struct drm_format_info *format;

	struct mgag200_pll_values pixpllc;

	bool set_vidrst;
};

static inline struct mgag200_crtc_state *to_mgag200_crtc_state(struct drm_crtc_state *base)
{
	return container_of(base, struct mgag200_crtc_state, base);
}

enum mga_type {
	G200_PCI,
	G200_AGP,
	G200_SE_A,
	G200_SE_B,
	G200_WB,
	G200_EV,
	G200_EH,
	G200_EH3,
	G200_EH5,
	G200_ER,
	G200_EW3,
};

struct mgag200_device_info {
	u16 max_hdisplay;
	u16 max_vdisplay;

	/*
	 * Maximum memory bandwidth (MiB/sec). Setting this to zero disables
	 * the rsp test during mode validation.
	 */
	unsigned long max_mem_bandwidth;

	/* Synchronize scanout with BMC */
	bool sync_bmc:1;

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
				 _sync_bmc, _i2c_data_bit, _i2c_clock_bit, \
				 _bug_no_startadd) \
	{ \
		.max_hdisplay = (_max_hdisplay), \
		.max_vdisplay = (_max_vdisplay), \
		.max_mem_bandwidth = (_max_mem_bandwidth), \
		.sync_bmc = (_sync_bmc), \
		.i2c = { \
			.data_bit = (_i2c_data_bit), \
			.clock_bit = (_i2c_clock_bit), \
		}, \
		.bug_no_startadd = (_bug_no_startadd), \
	}

struct mgag200_device_funcs {
	/*
	 * Validate that the given state can be programmed into PIXPLLC. On
	 * success, the calculated parameters should be stored in the CRTC's
	 * state in struct @mgag200_crtc_state.pixpllc.
	 */
	int (*pixpllc_atomic_check)(struct drm_crtc *crtc, struct drm_atomic_state *new_state);

	/*
	 * Program PIXPLLC from the CRTC state. The parameters should have been
	 * stored in struct @mgag200_crtc_state.pixpllc by the corresponding
	 * implementation of @pixpllc_atomic_check.
	 */
	void (*pixpllc_atomic_update)(struct drm_crtc *crtc, struct drm_atomic_state *old_state);
};

struct mga_device {
	struct drm_device base;

	const struct mgag200_device_info *info;
	const struct mgag200_device_funcs *funcs;

	struct resource			*rmmio_res;
	void __iomem			*rmmio;
	struct mutex			rmmio_lock; /* Protects access to rmmio */

	struct resource			*vram_res;
	void __iomem			*vram;
	resource_size_t			vram_available;

	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct {
		struct {
			struct drm_encoder encoder;
			struct drm_connector connector;
		} vga;
	} output;
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
int mgag200_device_init(struct mga_device *mdev,
			const struct mgag200_device_info *info,
			const struct mgag200_device_funcs *funcs);

				/* mgag200_<device type>.c */
struct mga_device *mgag200_g200_device_create(struct pci_dev *pdev, const struct drm_driver *drv);
struct mga_device *mgag200_g200se_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type);
void mgag200_g200wb_init_registers(struct mga_device *mdev);
void mgag200_g200wb_pixpllc_atomic_update(struct drm_crtc *crtc, struct drm_atomic_state *old_state);
struct mga_device *mgag200_g200wb_device_create(struct pci_dev *pdev, const struct drm_driver *drv);
struct mga_device *mgag200_g200ev_device_create(struct pci_dev *pdev, const struct drm_driver *drv);
void mgag200_g200eh_init_registers(struct mga_device *mdev);
void mgag200_g200eh_pixpllc_atomic_update(struct drm_crtc *crtc, struct drm_atomic_state *old_state);
struct mga_device *mgag200_g200eh_device_create(struct pci_dev *pdev,
						const struct drm_driver *drv);
struct mga_device *mgag200_g200eh3_device_create(struct pci_dev *pdev,
						 const struct drm_driver *drv);
struct mga_device *mgag200_g200eh5_device_create(struct pci_dev *pdev,
						 const struct drm_driver *drv);
struct mga_device *mgag200_g200er_device_create(struct pci_dev *pdev,
						const struct drm_driver *drv);
struct mga_device *mgag200_g200ew3_device_create(struct pci_dev *pdev,
						 const struct drm_driver *drv);

/*
 * mgag200_mode.c
 */

struct drm_crtc;
struct drm_crtc_state;
struct drm_display_mode;
struct drm_plane;
struct drm_atomic_state;
struct drm_scanout_buffer;

extern const uint32_t mgag200_primary_plane_formats[];
extern const size_t   mgag200_primary_plane_formats_size;
extern const uint64_t mgag200_primary_plane_fmtmods[];

int mgag200_primary_plane_helper_atomic_check(struct drm_plane *plane,
					      struct drm_atomic_state *new_state);
void mgag200_primary_plane_helper_atomic_update(struct drm_plane *plane,
						struct drm_atomic_state *old_state);
void mgag200_primary_plane_helper_atomic_enable(struct drm_plane *plane,
						struct drm_atomic_state *state);
void mgag200_primary_plane_helper_atomic_disable(struct drm_plane *plane,
						 struct drm_atomic_state *old_state);
int mgag200_primary_plane_helper_get_scanout_buffer(struct drm_plane *plane,
						    struct drm_scanout_buffer *sb);

#define MGAG200_PRIMARY_PLANE_HELPER_FUNCS \
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS, \
	.atomic_check = mgag200_primary_plane_helper_atomic_check, \
	.atomic_update = mgag200_primary_plane_helper_atomic_update, \
	.atomic_enable = mgag200_primary_plane_helper_atomic_enable, \
	.atomic_disable = mgag200_primary_plane_helper_atomic_disable, \
	.get_scanout_buffer = mgag200_primary_plane_helper_get_scanout_buffer

#define MGAG200_PRIMARY_PLANE_FUNCS \
	.update_plane = drm_atomic_helper_update_plane, \
	.disable_plane = drm_atomic_helper_disable_plane, \
	.destroy = drm_plane_cleanup, \
	DRM_GEM_SHADOW_PLANE_FUNCS

void mgag200_crtc_fill_gamma(struct mga_device *mdev, const struct drm_format_info *format);
void mgag200_crtc_load_gamma(struct mga_device *mdev,
			     const struct drm_format_info *format,
			     struct drm_color_lut *lut);

enum drm_mode_status mgag200_crtc_helper_mode_valid(struct drm_crtc *crtc,
						    const struct drm_display_mode *mode);
int mgag200_crtc_helper_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *new_state);
void mgag200_crtc_helper_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *old_state);
void mgag200_crtc_helper_atomic_enable(struct drm_crtc *crtc, struct drm_atomic_state *old_state);
void mgag200_crtc_helper_atomic_disable(struct drm_crtc *crtc, struct drm_atomic_state *old_state);

#define MGAG200_CRTC_HELPER_FUNCS \
	.mode_valid = mgag200_crtc_helper_mode_valid, \
	.atomic_check = mgag200_crtc_helper_atomic_check, \
	.atomic_flush = mgag200_crtc_helper_atomic_flush, \
	.atomic_enable = mgag200_crtc_helper_atomic_enable, \
	.atomic_disable = mgag200_crtc_helper_atomic_disable

void mgag200_crtc_reset(struct drm_crtc *crtc);
struct drm_crtc_state *mgag200_crtc_atomic_duplicate_state(struct drm_crtc *crtc);
void mgag200_crtc_atomic_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state);

#define MGAG200_CRTC_FUNCS \
	.reset = mgag200_crtc_reset, \
	.destroy = drm_crtc_cleanup, \
	.set_config = drm_atomic_helper_set_config, \
	.page_flip = drm_atomic_helper_page_flip, \
	.atomic_duplicate_state = mgag200_crtc_atomic_duplicate_state, \
	.atomic_destroy_state = mgag200_crtc_atomic_destroy_state

void mgag200_set_mode_regs(struct mga_device *mdev, const struct drm_display_mode *mode,
			   bool set_vidrst);
void mgag200_set_format_regs(struct mga_device *mdev, const struct drm_format_info *format);
void mgag200_enable_display(struct mga_device *mdev);
void mgag200_init_registers(struct mga_device *mdev);
int mgag200_mode_config_init(struct mga_device *mdev, resource_size_t vram_available);

/* mgag200_vga_bmc.c */
int mgag200_vga_bmc_output_init(struct mga_device *mdev);

/* mgag200_vga.c */
int mgag200_vga_output_init(struct mga_device *mdev);

/* mgag200_bmc.c */
void mgag200_bmc_stop_scanout(struct mga_device *mdev);
void mgag200_bmc_start_scanout(struct mga_device *mdev);

#endif				/* __MGAG200_DRV_H__ */
