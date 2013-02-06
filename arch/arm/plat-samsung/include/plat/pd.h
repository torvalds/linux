/* linux/arch/arm/plat-samsung/include/plat/pd.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_SAMSUNG_PD_H
#define __ASM_PLAT_SAMSUNG_PD_H __FILE__

enum exynos_pd_block {
	PD_MFC,
	PD_G3D,
	PD_LCD0,
	PD_LCD1,
	PD_TV,
	PD_CAM,
	PD_GPS,
	PD_GPS_ALIVE,
	PD_ISP,
	PD_MAUDIO,
	PD_GSCL,
	PD_DISP1,
	PD_TOP,
};

struct samsung_pd_info {
	int (*init)(struct device *dev);
	int (*enable)(struct device *dev);
	int (*disable)(struct device *dev);
	int (*save)(struct device *dev);
	int (*restore)(struct device *dev);
	void __iomem *base;
	void *data;
	enum exynos_pd_block id;
};

struct exynos_pd_data {
	void __iomem *clk_base;
	void __iomem *clksrc_base;
	void __iomem *read_base;
	unsigned long read_phy_addr;
};

int exynos_pd_init(struct device *dev);
int exynos_pd_enable(struct device *dev);
int exynos_pd_disable(struct device *dev);
#endif /* __ASM_PLAT_SAMSUNG_PD_H */
