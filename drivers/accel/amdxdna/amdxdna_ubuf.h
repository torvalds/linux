/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 */
#ifndef _AMDXDNA_UBUF_H_
#define _AMDXDNA_UBUF_H_

#include <drm/drm_device.h>
#include <linux/dma-buf.h>

enum amdxdna_ubuf_flag {
	AMDXDNA_UBUF_FLAG_MAP_DMA = 1,
};

struct dma_buf *amdxdna_get_ubuf(struct drm_device *dev,
				 enum amdxdna_ubuf_flag flags,
				 u32 num_entries, void __user *va_entries);

#endif /* _AMDXDNA_UBUF_H_ */
