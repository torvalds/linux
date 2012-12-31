/* linux/arch/arm/mach-exynos/include/mach/videonode-exynos5.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5 - Video node definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __MACH_VIDEONODE_EXYNOS5_H
#define __MACH_VIDEONODE_EXYNOS5_H __FILE__

#define S5P_VIDEONODE_MFC_DEC		6
#define S5P_VIDEONODE_MFC_ENC		7

#define EXYNOS_VIDEONODE_ROTATOR		21

#define EXYNOS_VIDEONODE_GSC_M2M(x)		(23 + (x) * 3)
#define EXYNOS_VIDEONODE_GSC_OUT(x)		(24 + (x) * 3)
#define EXYNOS_VIDEONODE_GSC_CAP(x)		(25 + (x) * 3)

#define EXYNOS_VIDEONODE_FLITE(x)		(36 + x)
/* Exynos4x12 supports video, graphic0~1 layer
 * Exynos5250 supports graphic0~3 layer */
#define EXYNOS_VIDEONODE_MXR_GRP(x)		(16 + x)
#define EXYNOS_VIDEONODE_MXR_VIDEO		20
#define EXYNOS_VIDEONODE_FIMC_IS			(40)

#endif /* __MACH_VIDEONODE_EXYNOS5_H */
