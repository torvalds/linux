/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __INTEL_VGA_REGS_H__
#define __INTEL_VGA_REGS_H__

#include "intel_display_reg_defs.h"

#define VGACNTRL	_MMIO(0x71400)
#define VLV_VGACNTRL	_MMIO(VLV_DISPLAY_BASE + 0x71400)
#define CPU_VGACNTRL	_MMIO(0x41000)
#define   VGA_DISP_DISABLE			REG_BIT(31)
#define   VGA_2X_MODE				REG_BIT(30) /* pre-ilk */
#define   VGA_PIPE_SEL_MASK			REG_BIT(29) /* pre-ivb */
#define   VGA_PIPE_SEL(pipe)			REG_FIELD_PREP(VGA_PIPE_SEL_MASK, (pipe))
#define   VGA_PIPE_SEL_MASK_CHV			REG_GENMASK(29, 28) /* chv */
#define   VGA_PIPE_SEL_CHV(pipe)		REG_FIELD_PREP(VGA_PIPE_SEL_MASK_CHV, (pipe))
#define   VGA_BORDER_ENABLE			REG_BIT(26)
#define   VGA_PIPE_CSC_ENABLE			REG_BIT(24) /* ilk+ */
#define   VGA_CENTERING_ENABLE_MASK		REG_GENMASK(25, 24) /* pre-ilk */
#define   VGA_PALETTE_READ_SEL			REG_BIT(23) /* pre-ivb */
#define   VGA_PALETTE_A_WRITE_DISABLE		REG_BIT(22) /* pre-ivb */
#define   VGA_PALETTE_B_WRITE_DISABLE		REG_BIT(21) /* pre-ivb */
#define   VGA_LEGACY_8BIT_PALETTE_ENABLE	REG_BIT(20)
#define   VGA_PALETTE_BYPASS			REG_BIT(19)
#define   VGA_NINE_DOT_DISABLE			REG_BIT(18)
#define   VGA_PALETTE_READ_SEL_HI_CHV		REG_BIT(15) /* chv */
#define   VGA_PALETTE_C_WRITE_DISABLE_CHV	REG_BIT(14) /* chv */
#define   VGA_ACTIVE_THROTTLING_MASK		REG_GENMASK(15, 12) /* ilk+ */
#define   VGA_BLANK_THROTTLING_MASK		REG_GENMASK(11, 8) /* ilk+ */
#define   VGA_BLINK_DUTY_CYCLE_MASK		REG_GENMASK(7, 6)
#define   VGA_VSYNC_BLINK_RATE_MASK		REG_GENMASK(5, 0)

#endif /* __INTEL_VGA_REGS_H__ */
