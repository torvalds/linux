/* linux/arch/arm/plat-s5p/include/plat/jpeg.h
 *
 * Copyright 201i Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#ifndef __ASM_PLAT_JPEG_H
#define __ASM_PLAT_JPEG_H __FILE__

int __init exynos4_jpeg_setup_clock(struct device *dev,
					unsigned long clk_rate);
int __init exynos5_jpeg_setup_clock(struct device *dev,
					unsigned long clk_rate);
#endif /*__ASM_PLAT_JPEG_H */
