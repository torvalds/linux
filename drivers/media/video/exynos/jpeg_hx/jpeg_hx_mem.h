/* linux/drivers/media/video/exynos/jpeg_hx/jpeg_hx_mem.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Definition for Operation of Jpeg encoder/docoder with memory
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __JPEG_MEM_H__
#define __JPEG_MEM_H__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/cma.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <media/videobuf2-core.h>
#include <media/videobuf2-memops.h>

#include <asm/cacheflush.h>

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
extern const struct jpeg_vb2 jpeg_hx_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
extern const struct jpeg_vb2 jpeg_hx_vb2_ion;
#endif

#define MAX_JPEG_WIDTH	5000
#define MAX_JPEG_HEIGHT	4000

#endif /* __JPEG_MEM_H__ */
