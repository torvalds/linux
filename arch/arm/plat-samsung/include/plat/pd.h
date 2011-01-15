/* linux/arch/arm/plat-samsung/include/plat/pd.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_PLAT_SAMSUNG_PD_H
#define __ASM_PLAT_SAMSUNG_PD_H __FILE__

struct samsung_pd_info {
	int (*enable)(struct device *dev);
	int (*disable)(struct device *dev);
	void __iomem *base;
};

enum s5pv310_pd_block {
	PD_MFC,
	PD_G3D,
	PD_LCD0,
	PD_LCD1,
	PD_TV,
	PD_CAM,
	PD_GPS
};

#endif /* __ASM_PLAT_SAMSUNG_PD_H */
