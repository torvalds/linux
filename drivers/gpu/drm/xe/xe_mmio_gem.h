/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_MMIO_GEM_H_
#define _XE_MMIO_GEM_H_

#include <linux/types.h>

struct drm_file;
struct xe_device;
struct xe_mmio_gem;

struct xe_mmio_gem *xe_mmio_gem_create(struct xe_device *xe, struct drm_file *file,
				       phys_addr_t phys_addr, size_t size);
u64 xe_mmio_gem_mmap_offset(struct xe_mmio_gem *gem);
void xe_mmio_gem_destroy(struct xe_mmio_gem *gem);

#endif /* _XE_MMIO_GEM_H_ */
