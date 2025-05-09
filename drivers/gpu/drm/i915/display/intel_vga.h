/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_VGA_H__
#define __INTEL_VGA_H__

struct intel_display;

void intel_vga_reset_io_mem(struct intel_display *display);
void intel_vga_disable(struct intel_display *display);
int intel_vga_register(struct intel_display *display);
void intel_vga_unregister(struct intel_display *display);

#endif /* __INTEL_VGA_H__ */
