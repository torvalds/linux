/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __INTEL_ROM_H__
#define __INTEL_ROM_H__

#include <linux/types.h>

struct drm_i915_private;
struct intel_rom;

struct intel_rom *intel_rom_spi(struct drm_i915_private *i915);
struct intel_rom *intel_rom_pci(struct drm_i915_private *i915);

u32 intel_rom_read32(struct intel_rom *rom, loff_t offset);
u16 intel_rom_read16(struct intel_rom *rom, loff_t offset);
void intel_rom_read_block(struct intel_rom *rom, void *data,
			  loff_t offset, size_t size);
loff_t intel_rom_find(struct intel_rom *rom, u32 needle);
size_t intel_rom_size(struct intel_rom *rom);
void intel_rom_free(struct intel_rom *rom);

#endif /* __INTEL_ROM_H__ */
