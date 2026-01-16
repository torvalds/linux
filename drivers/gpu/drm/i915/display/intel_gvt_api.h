/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __INTEL_GVT_API_H__
#define __INTEL_GVT_API_H__

#include <linux/types.h>

enum pipe;
enum transcoder;
struct intel_display;

u32 intel_display_device_pipe_offset(struct intel_display *display, enum pipe pipe);
u32 intel_display_device_trans_offset(struct intel_display *display, enum transcoder trans);
u32 intel_display_device_cursor_offset(struct intel_display *display, enum pipe pipe);
u32 intel_display_device_mmio_base(struct intel_display *display);
bool intel_display_device_pipe_valid(struct intel_display *display, enum pipe pipe);

#endif /* __INTEL_GVT_API_H__ */
