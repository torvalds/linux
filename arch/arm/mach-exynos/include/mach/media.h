/* linux/arch/arm/mach-exysnos4/include/mach/media.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Samsung Media device descriptions for exynos4
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _EXYNOS4_MEDIA_H
#define _EXYNOS4_MEDIA_H

#ifdef CONFIG_CMA
#define S5P_MDEV_FIMC0		0
#define S5P_MDEV_FIMC1		1
#define S5P_MDEV_FIMC2		2
#define S5P_MDEV_FIMC3		3
#define S5P_MDEV_MFC		4
#define S5P_MDEV_JPEG		5
#define S5P_MDEV_FIMD		6
#define S5P_MDEV_FIMG2D		7
#define S5P_MDEV_SRP		8
#define S5P_MDEV_TVOUT          9


#if defined(CONFIG_MACH_U1) || defined(CONFIG_MACH_TRATS)
#define S5P_MDEV_PMEM		10
#endif


#define S5P_RANGE_MFC		SZ_256M
#endif /* CONFIG_CMA */
#endif
