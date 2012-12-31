/*
 * linux/drivers/media/video/samsung/mfc5x/mfc.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Global header for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MFC_H_
#define __MFC_H_ __FILE__

#define MAX_HOR_SIZE	1920
#define MAX_VER_SIZE	1088
#define MAX_HOR_RES	1920
#define MAX_VER_RES	1080

#define MAX_MEM_OFFSET	0x10000000

#ifdef CONFIG_VIDEO_MFC_MAX_INSTANCE
#define MFC_MAX_INSTANCE_NUM	(CONFIG_VIDEO_MFC_MAX_INSTANCE)
#else
#define MFC_MAX_INSTANCE_NUM	(1)
#endif

#ifdef CONFIG_EXYNOS4_CONTENT_PATH_PROTECTION

#define MFC_MAX_MEM_CHUNK_NUM	(2)

#define MFC_MAX_MEM_PORT_NUM	(1)

#define MFC_MEMSIZE_PORT_A	(CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_SECURE << 10)
#define MFC_MEMSIZE_PORT_B	(CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC_NORMAL << 10)

#define MFC_MEMSIZE_DRM		0x20000
#define MFC_SHM_OFS_DRM		0x400

#else

#ifdef CONFIG_VIDEO_MFC_MEM_PORT_COUNT
#define MFC_MAX_MEM_PORT_NUM	(CONFIG_VIDEO_MFC_MEM_PORT_COUNT)
#else
#define MFC_MAX_MEM_PORT_NUM	(2)
#endif

#if (CONFIG_VIDEO_MFC_MEM_PORT_COUNT == 1)

#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC
#define MFC_MEMSIZE_PORT_A	(CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC << 10)
#define MFC_MEMSIZE_PORT_B	0
#else
#define MFC_MEMSIZE_PORT_A	0x4000000
#define MFC_MEMSIZE_PORT_B	0
#endif

#else

#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC0
#define MFC_MEMSIZE_PORT_A	(CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC0 << 10)
#else
#define MFC_MEMSIZE_PORT_A	0x2000000
#endif

#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC1
#define MFC_MEMSIZE_PORT_B	(CONFIG_VIDEO_SAMSUNG_MEMSIZE_MFC1 << 10)
#else
#define MFC_MEMSIZE_PORT_B	0x2000000
#endif

#endif

#endif

#if defined(CONFIG_S5P_SYSMMU_MFC_L) && defined(CONFIG_S5P_SYSMMU_MFC_R)
#define SYSMMU_MFC_ON
#endif

#if defined(CONFIG_VIDEO_MFC_VCM_UMP) && !defined(SYSMMU_MFC_ON)
#error In order to use UMP over VCM, you must configure System MMU for MFC_L and MFC_R!
#endif

#if defined(CONFIG_S5P_VMEM) && !defined(SYSMMU_MFC_ON)
#error In order to use S5PVEM, you must configure System MMU for MFC_L and MFC_R!
#endif

/* if possible, the free virtual addr. for MFC be aligned with 128KB */
#if defined(CONFIG_S5P_VMEM)
#if defined(CONFIG_VMSPLIT_3G)
#define MFC_FREEBASE	0xF0000000
#elif defined(CONFIG_VMSPLIT_2G)
#define MFC_FREEBASE	0xE0000000
#else
#error Not support current memory split configuration
#endif
#endif

#endif /* __MFC_H_ */
