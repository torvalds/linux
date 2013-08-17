/* linux/arch/arm/mach-exynos/include/mach/videonode-exynos4.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - Video node definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __MACH_VIDEONODE_EXYNOS4_H
#define __MACH_VIDEONODE_EXYNOS4_H __FILE__

#define S5P_VIDEONODE_MFC_DEC			6
#define S5P_VIDEONODE_MFC_ENC			7

#define S5P_VIDEONODE_FIMC_M2M(x)		(23 + (x) * 2)
#define S5P_VIDEONODE_FIMC_CAP(x)		(24 + (x) * 2)

#endif /* __MACH_VIDEONODE_EXYNOS4_H */
