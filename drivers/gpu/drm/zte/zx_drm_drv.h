/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ZX_DRM_DRV_H__
#define __ZX_DRM_DRV_H__

extern struct platform_driver zx_crtc_driver;
extern struct platform_driver zx_hdmi_driver;
extern struct platform_driver zx_tvenc_driver;
extern struct platform_driver zx_vga_driver;

static inline u32 zx_readl(void __iomem *reg)
{
	return readl_relaxed(reg);
}

static inline void zx_writel(void __iomem *reg, u32 val)
{
	writel_relaxed(val, reg);
}

static inline void zx_writel_mask(void __iomem *reg, u32 mask, u32 val)
{
	u32 tmp;

	tmp = zx_readl(reg);
	tmp = (tmp & ~mask) | (val & mask);
	zx_writel(reg, tmp);
}

#endif /* __ZX_DRM_DRV_H__ */
