/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _INTEL_DSB_BUFFER_H
#define _INTEL_DSB_BUFFER_H

#include <linux/types.h>

struct drm_device;
struct intel_dsb_buffer;

u32 intel_dsb_buffer_ggtt_offset(struct intel_dsb_buffer *dsb_buf);
void intel_dsb_buffer_write(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val);
u32 intel_dsb_buffer_read(struct intel_dsb_buffer *dsb_buf, u32 idx);
void intel_dsb_buffer_memset(struct intel_dsb_buffer *dsb_buf, u32 idx, u32 val, size_t size);
struct intel_dsb_buffer *intel_dsb_buffer_create(struct drm_device *drm, size_t size);
void intel_dsb_buffer_cleanup(struct intel_dsb_buffer *dsb_buf);
void intel_dsb_buffer_flush_map(struct intel_dsb_buffer *dsb_buf);

#endif
