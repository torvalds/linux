/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef _VLV_SIDEBAND_H_
#define _VLV_SIDEBAND_H_

#include <linux/types.h>

#include <drm/intel/vlv_iosf_sb_regs.h>

enum dpio_phy;
struct intel_display;

void vlv_bunit_get(struct intel_display *display);
u32 vlv_bunit_read(struct intel_display *display, u32 reg);
void vlv_bunit_write(struct intel_display *display, u32 reg, u32 val);
void vlv_bunit_put(struct intel_display *display);

void vlv_cck_get(struct intel_display *display);
u32 vlv_cck_read(struct intel_display *display, u32 reg);
void vlv_cck_write(struct intel_display *display, u32 reg, u32 val);
void vlv_cck_put(struct intel_display *display);

void vlv_ccu_get(struct intel_display *display);
u32 vlv_ccu_read(struct intel_display *display, u32 reg);
void vlv_ccu_write(struct intel_display *display, u32 reg, u32 val);
void vlv_ccu_put(struct intel_display *display);

void vlv_dpio_get(struct intel_display *display);
u32 vlv_dpio_read(struct intel_display *display, enum dpio_phy phy, int reg);
void vlv_dpio_write(struct intel_display *display, enum dpio_phy phy, int reg, u32 val);
void vlv_dpio_put(struct intel_display *display);

void vlv_flisdsi_get(struct intel_display *display);
u32 vlv_flisdsi_read(struct intel_display *display, u32 reg);
void vlv_flisdsi_write(struct intel_display *display, u32 reg, u32 val);
void vlv_flisdsi_put(struct intel_display *display);

void vlv_nc_get(struct intel_display *display);
u32 vlv_nc_read(struct intel_display *display, u8 addr);
void vlv_nc_put(struct intel_display *display);

void vlv_punit_get(struct intel_display *display);
u32 vlv_punit_read(struct intel_display *display, u32 addr);
int vlv_punit_write(struct intel_display *display, u32 addr, u32 val);
void vlv_punit_put(struct intel_display *display);

#endif /* _VLV_SIDEBAND_H_ */
