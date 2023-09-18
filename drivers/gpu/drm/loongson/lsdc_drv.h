/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef __LSDC_DRV_H__
#define __LSDC_DRV_H__

#include <linux/pci.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_file.h>
#include <drm/drm_plane.h>
#include <drm/ttm/ttm_device.h>

#include "lsdc_i2c.h"
#include "lsdc_irq.h"
#include "lsdc_gfxpll.h"
#include "lsdc_output.h"
#include "lsdc_pixpll.h"
#include "lsdc_regs.h"

/* Currently, all Loongson display controllers have two display pipes. */
#define LSDC_NUM_CRTC           2

/*
 * LS7A1000/LS7A2000 chipsets function as the south & north bridges of the
 * Loongson 3 series processors, they are equipped with on-board video RAM
 * typically. While Loongson LS2K series are low cost SoCs which share the
 * system RAM as video RAM, they don't has a dedicated VRAM.
 *
 * There is only a 1:1 mapping of crtcs, encoders and connectors for the DC
 *
 * display pipe 0 = crtc0 + dvo0 + encoder0 + connector0 + cursor0 + primary0
 * display pipe 1 = crtc1 + dvo1 + encoder1 + connectro1 + cursor1 + primary1
 */

enum loongson_chip_id {
	CHIP_LS7A1000 = 0,
	CHIP_LS7A2000 = 1,
	CHIP_LS_LAST,
};

const struct lsdc_desc *
lsdc_device_probe(struct pci_dev *pdev, enum loongson_chip_id chip);

struct lsdc_kms_funcs;

/* DC specific */

struct lsdc_desc {
	u32 num_of_crtc;
	u32 max_pixel_clk;
	u32 max_width;
	u32 max_height;
	u32 num_of_hw_cursor;
	u32 hw_cursor_w;
	u32 hw_cursor_h;
	u32 pitch_align;         /* CRTC DMA alignment constraint */
	bool has_vblank_counter; /* 32 bit hw vsync counter */

	/* device dependent ops, dc side */
	const struct lsdc_kms_funcs *funcs;
};

/* GFX related resources wrangler */

struct loongson_gfx_desc {
	struct lsdc_desc dc;

	u32 conf_reg_base;

	/* GFXPLL shared by the DC, GMC and GPU */
	struct {
		u32 reg_offset;
		u32 reg_size;
	} gfxpll;

	/* Pixel PLL, per display pipe */
	struct {
		u32 reg_offset;
		u32 reg_size;
	} pixpll[LSDC_NUM_CRTC];

	enum loongson_chip_id chip_id;
	char model[64];
};

static inline const struct loongson_gfx_desc *
to_loongson_gfx(const struct lsdc_desc *dcp)
{
	return container_of_const(dcp, struct loongson_gfx_desc, dc);
};

struct lsdc_reg32 {
	char *name;
	u32 offset;
};

/* crtc hardware related ops */

struct lsdc_crtc;

struct lsdc_crtc_hw_ops {
	void (*enable)(struct lsdc_crtc *lcrtc);
	void (*disable)(struct lsdc_crtc *lcrtc);
	void (*enable_vblank)(struct lsdc_crtc *lcrtc);
	void (*disable_vblank)(struct lsdc_crtc *lcrtc);
	void (*flip)(struct lsdc_crtc *lcrtc);
	void (*clone)(struct lsdc_crtc *lcrtc);
	void (*get_scan_pos)(struct lsdc_crtc *lcrtc, int *hpos, int *vpos);
	void (*set_mode)(struct lsdc_crtc *lcrtc, const struct drm_display_mode *mode);
	void (*soft_reset)(struct lsdc_crtc *lcrtc);
	void (*reset)(struct lsdc_crtc *lcrtc);

	u32  (*get_vblank_counter)(struct lsdc_crtc *lcrtc);
	void (*set_dma_step)(struct lsdc_crtc *lcrtc, enum lsdc_dma_steps step);
};

struct lsdc_crtc {
	struct drm_crtc base;
	struct lsdc_pixpll pixpll;
	struct lsdc_device *ldev;
	const struct lsdc_crtc_hw_ops *hw_ops;
	const struct lsdc_reg32 *preg;
	unsigned int nreg;
	struct drm_info_list *p_info_list;
	unsigned int n_info_list;
	bool has_vblank;
};

/* primary plane hardware related ops */

struct lsdc_primary;

struct lsdc_primary_plane_ops {
	void (*update_fb_addr)(struct lsdc_primary *plane, u64 addr);
	void (*update_fb_stride)(struct lsdc_primary *plane, u32 stride);
	void (*update_fb_format)(struct lsdc_primary *plane,
				 const struct drm_format_info *format);
};

struct lsdc_primary {
	struct drm_plane base;
	const struct lsdc_primary_plane_ops *ops;
	struct lsdc_device *ldev;
};

/* cursor plane hardware related ops */

struct lsdc_cursor;

struct lsdc_cursor_plane_ops {
	void (*update_bo_addr)(struct lsdc_cursor *plane, u64 addr);
	void (*update_cfg)(struct lsdc_cursor *plane,
			   enum lsdc_cursor_size cursor_size,
			   enum lsdc_cursor_format);
	void (*update_position)(struct lsdc_cursor *plane, int x, int y);
};

struct lsdc_cursor {
	struct drm_plane base;
	const struct lsdc_cursor_plane_ops *ops;
	struct lsdc_device *ldev;
};

struct lsdc_output {
	struct drm_encoder encoder;
	struct drm_connector connector;
};

static inline struct lsdc_output *
connector_to_lsdc_output(struct drm_connector *connector)
{
	return container_of(connector, struct lsdc_output, connector);
}

static inline struct lsdc_output *
encoder_to_lsdc_output(struct drm_encoder *encoder)
{
	return container_of(encoder, struct lsdc_output, encoder);
}

struct lsdc_display_pipe {
	struct lsdc_crtc crtc;
	struct lsdc_primary primary;
	struct lsdc_cursor cursor;
	struct lsdc_output output;
	struct lsdc_i2c *li2c;
	unsigned int index;
};

static inline struct lsdc_display_pipe *
output_to_display_pipe(struct lsdc_output *output)
{
	return container_of(output, struct lsdc_display_pipe, output);
}

struct lsdc_kms_funcs {
	irqreturn_t (*irq_handler)(int irq, void *arg);

	int (*create_i2c)(struct drm_device *ddev,
			  struct lsdc_display_pipe *dispipe,
			  unsigned int index);

	int (*output_init)(struct drm_device *ddev,
			   struct lsdc_display_pipe *dispipe,
			   struct i2c_adapter *ddc,
			   unsigned int index);

	int (*cursor_plane_init)(struct drm_device *ddev,
				 struct drm_plane *plane,
				 unsigned int index);

	int (*primary_plane_init)(struct drm_device *ddev,
				  struct drm_plane *plane,
				  unsigned int index);

	int (*crtc_init)(struct drm_device *ddev,
			 struct drm_crtc *crtc,
			 struct drm_plane *primary,
			 struct drm_plane *cursor,
			 unsigned int index,
			 bool has_vblank);
};

static inline struct lsdc_crtc *
to_lsdc_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct lsdc_crtc, base);
}

static inline struct lsdc_display_pipe *
crtc_to_display_pipe(struct drm_crtc *crtc)
{
	return container_of(crtc, struct lsdc_display_pipe, crtc.base);
}

static inline struct lsdc_primary *
to_lsdc_primary(struct drm_plane *plane)
{
	return container_of(plane, struct lsdc_primary, base);
}

static inline struct lsdc_cursor *
to_lsdc_cursor(struct drm_plane *plane)
{
	return container_of(plane, struct lsdc_cursor, base);
}

struct lsdc_crtc_state {
	struct drm_crtc_state base;
	struct lsdc_pixpll_parms pparms;
};

struct lsdc_gem {
	/* @mutex: protect objects list */
	struct mutex mutex;
	struct list_head objects;
};

struct lsdc_device {
	struct drm_device base;
	struct ttm_device bdev;

	/* @descp: features description of the DC variant */
	const struct lsdc_desc *descp;
	struct pci_dev *dc;
	struct pci_dev *gpu;

	struct loongson_gfxpll *gfxpll;

	/* @reglock: protects concurrent access */
	spinlock_t reglock;

	void __iomem *reg_base;
	resource_size_t vram_base;
	resource_size_t vram_size;

	resource_size_t gtt_base;
	resource_size_t gtt_size;

	struct lsdc_display_pipe dispipe[LSDC_NUM_CRTC];

	struct lsdc_gem gem;

	u32 irq_status;

	/* tracking pinned memory */
	size_t vram_pinned_size;
	size_t gtt_pinned_size;

	/* @num_output: count the number of active display pipe */
	unsigned int num_output;
};

static inline struct lsdc_device *tdev_to_ldev(struct ttm_device *bdev)
{
	return container_of(bdev, struct lsdc_device, bdev);
}

static inline struct lsdc_device *to_lsdc(struct drm_device *ddev)
{
	return container_of(ddev, struct lsdc_device, base);
}

static inline struct lsdc_crtc_state *
to_lsdc_crtc_state(struct drm_crtc_state *base)
{
	return container_of(base, struct lsdc_crtc_state, base);
}

void lsdc_debugfs_init(struct drm_minor *minor);

int ls7a1000_crtc_init(struct drm_device *ddev,
		       struct drm_crtc *crtc,
		       struct drm_plane *primary,
		       struct drm_plane *cursor,
		       unsigned int index,
		       bool no_vblank);

int ls7a2000_crtc_init(struct drm_device *ddev,
		       struct drm_crtc *crtc,
		       struct drm_plane *primary,
		       struct drm_plane *cursor,
		       unsigned int index,
		       bool no_vblank);

int lsdc_primary_plane_init(struct drm_device *ddev,
			    struct drm_plane *plane,
			    unsigned int index);

int ls7a1000_cursor_plane_init(struct drm_device *ddev,
			       struct drm_plane *plane,
			       unsigned int index);

int ls7a2000_cursor_plane_init(struct drm_device *ddev,
			       struct drm_plane *plane,
			       unsigned int index);

/* Registers access helpers */

static inline u32 lsdc_rreg32(struct lsdc_device *ldev, u32 offset)
{
	return readl(ldev->reg_base + offset);
}

static inline void lsdc_wreg32(struct lsdc_device *ldev, u32 offset, u32 val)
{
	writel(val, ldev->reg_base + offset);
}

static inline void lsdc_ureg32_set(struct lsdc_device *ldev,
				   u32 offset,
				   u32 mask)
{
	void __iomem *addr = ldev->reg_base + offset;
	u32 val = readl(addr);

	writel(val | mask, addr);
}

static inline void lsdc_ureg32_clr(struct lsdc_device *ldev,
				   u32 offset,
				   u32 mask)
{
	void __iomem *addr = ldev->reg_base + offset;
	u32 val = readl(addr);

	writel(val & ~mask, addr);
}

static inline u32 lsdc_pipe_rreg32(struct lsdc_device *ldev,
				   u32 offset, u32 pipe)
{
	return readl(ldev->reg_base + offset + pipe * CRTC_PIPE_OFFSET);
}

static inline void lsdc_pipe_wreg32(struct lsdc_device *ldev,
				    u32 offset, u32 pipe, u32 val)
{
	writel(val, ldev->reg_base + offset + pipe * CRTC_PIPE_OFFSET);
}

#endif
