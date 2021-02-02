/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright © 2018-2020 Intel Corporation
 */

#ifndef __KMB_DRV_H__
#define __KMB_DRV_H__

#include <drm/drm_device.h>

#include "kmb_plane.h"
#include "kmb_regs.h"

#define KMB_MAX_WIDTH			1920 /*Max width in pixels */
#define KMB_MAX_HEIGHT			1080 /*Max height in pixels */
#define KMB_MIN_WIDTH                   1920 /*Max width in pixels */
#define KMB_MIN_HEIGHT                  1080 /*Max height in pixels */
#define KMB_LCD_DEFAULT_CLK		200000000
#define KMB_SYS_CLK_MHZ			500

#define ICAM_MMIO		0x3b100000
#define ICAM_LCD_OFFSET		0x1080
#define ICAM_MMIO_SIZE		0x2000

struct kmb_dsi;

struct kmb_clock {
	struct clk *clk_lcd;
	struct clk *clk_pll0;
};

struct kmb_drm_private {
	struct drm_device		drm;
	struct kmb_dsi			*kmb_dsi;
	void __iomem			*lcd_mmio;
	struct kmb_clock		kmb_clk;
	struct drm_crtc			crtc;
	struct kmb_plane		*plane;
	struct drm_atomic_state		*state;
	spinlock_t			irq_lock;
	int				irq_lcd;
	int				sys_clk_mhz;
	struct layer_status		plane_status[KMB_MAX_PLANES];
	int				kmb_under_flow;
	int				kmb_flush_done;
	int				layer_no;
};

static inline struct kmb_drm_private *to_kmb(const struct drm_device *dev)
{
	return container_of(dev, struct kmb_drm_private, drm);
}

static inline struct kmb_drm_private *crtc_to_kmb_priv(const struct drm_crtc *x)
{
	return container_of(x, struct kmb_drm_private, crtc);
}

static inline void kmb_write_lcd(struct kmb_drm_private *dev_p,
				 unsigned int reg, u32 value)
{
	writel(value, (dev_p->lcd_mmio + reg));
}

static inline u32 kmb_read_lcd(struct kmb_drm_private *dev_p, unsigned int reg)
{
	return readl(dev_p->lcd_mmio + reg);
}

static inline void kmb_set_bitmask_lcd(struct kmb_drm_private *dev_p,
				       unsigned int reg, u32 mask)
{
	u32 reg_val = kmb_read_lcd(dev_p, reg);

	kmb_write_lcd(dev_p, reg, (reg_val | mask));
}

static inline void kmb_clr_bitmask_lcd(struct kmb_drm_private *dev_p,
				       unsigned int reg, u32 mask)
{
	u32 reg_val = kmb_read_lcd(dev_p, reg);

	kmb_write_lcd(dev_p, reg, (reg_val & (~mask)));
}

int kmb_setup_crtc(struct drm_device *dev);
void kmb_set_scanout(struct kmb_drm_private *lcd);
#endif /* __KMB_DRV_H__ */
