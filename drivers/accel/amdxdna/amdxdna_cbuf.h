/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */
#ifndef _AMDXDNA_CBUF_H_
#define _AMDXDNA_CBUF_H_

#include "amdxdna_pci_drv.h"
#include <drm/drm_device.h>
#include <linux/dma-buf.h>

bool amdxdna_use_carveout(struct amdxdna_dev *xdna);
int amdxdna_carveout_init(struct amdxdna_dev *xdna, u64 carveout_addr, u64 carveout_size);
void amdxdna_carveout_fini(struct amdxdna_dev *xdna);
void amdxdna_get_carveout_conf(struct amdxdna_dev *xdna, u64 *addr, u64 *size);
struct dma_buf *amdxdna_get_cbuf(struct drm_device *dev, size_t size, u64 alignment);

#endif
