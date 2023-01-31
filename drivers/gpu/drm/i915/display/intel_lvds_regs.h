/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_LVDS_REGS_H__
#define __INTEL_LVDS_REGS_H__

#include "intel_display_reg_defs.h"

/* LVDS port control */
#define LVDS		_MMIO(0x61180)
/*
 * Enables the LVDS port.  This bit must be set before DPLLs are enabled, as
 * the DPLL semantics change when the LVDS is assigned to that pipe.
 */
#define   LVDS_PORT_EN			REG_BIT(31)
/* Selects pipe B for LVDS data.  Must be set on pre-965. */
#define   LVDS_PIPE_SEL_MASK		REG_BIT(30)
#define   LVDS_PIPE_SEL(pipe)		REG_FIELD_PREP(LVDS_PIPE_SEL_MASK, (pipe))
#define   LVDS_PIPE_SEL_MASK_CPT	REG_GENMASK(30, 29)
#define   LVDS_PIPE_SEL_CPT(pipe)	REG_FIELD_PREP(LVDS_PIPE_SEL_MASK_CPT, (pipe))
/* LVDS dithering flag on 965/g4x platform */
#define   LVDS_ENABLE_DITHER		REG_BIT(25)
/* LVDS sync polarity flags. Set to invert (i.e. negative) */
#define   LVDS_VSYNC_POLARITY		REG_BIT(21)
#define   LVDS_HSYNC_POLARITY		REG_BIT(20)

/* Enable border for unscaled (or aspect-scaled) display */
#define   LVDS_BORDER_ENABLE		REG_BIT(15)
/*
 * Enables the A0-A2 data pairs and CLKA, containing 18 bits of color data per
 * pixel.
 */
#define   LVDS_A0A2_CLKA_POWER_MASK	REG_GENMASK(9, 8)
#define   LVDS_A0A2_CLKA_POWER_DOWN	REG_FIELD_PREP(LVDS_A0A2_CLKA_POWER_MASK, 0)
#define   LVDS_A0A2_CLKA_POWER_UP	REG_FIELD_PREP(LVDS_A0A2_CLKA_POWER_MASK, 3)
/*
 * Controls the A3 data pair, which contains the additional LSBs for 24 bit
 * mode.  Only enabled if LVDS_A0A2_CLKA_POWER_UP also indicates it should be
 * on.
 */
#define   LVDS_A3_POWER_MASK		REG_GENMASK(7, 6)
#define   LVDS_A3_POWER_DOWN		REG_FIELD_PREP(LVDS_A3_POWER_MASK, 0)
#define   LVDS_A3_POWER_UP		REG_FIELD_PREP(LVDS_A3_POWER_MASK, 3)
/*
 * Controls the CLKB pair.  This should only be set when LVDS_B0B3_POWER_UP
 * is set.
 */
#define   LVDS_CLKB_POWER_MASK		REG_GENMASK(5, 4)
#define   LVDS_CLKB_POWER_DOWN		REG_FIELD_PREP(LVDS_CLKB_POWER_MASK, 0)
#define   LVDS_CLKB_POWER_UP		REG_FIELD_PREP(LVDS_CLKB_POWER_MASK, 3)
/*
 * Controls the B0-B3 data pairs.  This must be set to match the DPLL p2
 * setting for whether we are in dual-channel mode.  The B3 pair will
 * additionally only be powered up when LVDS_A3_POWER_UP is set.
 */
#define   LVDS_B0B3_POWER_MASK		REG_GENMASK(3, 2)
#define   LVDS_B0B3_POWER_DOWN		REG_FIELD_PREP(LVDS_B0B3_POWER_MASK, 0)
#define   LVDS_B0B3_POWER_UP		REG_FIELD_PREP(LVDS_B0B3_POWER_MASK, 3)

#define PCH_LVDS	_MMIO(0xe1180)
#define   LVDS_DETECTED			REG_BIT(1)

#endif /* __INTEL_LVDS_REGS_H__ */
