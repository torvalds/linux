/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_BACKLIGHT_REGS_H__
#define __INTEL_BACKLIGHT_REGS_H__

#include "i915_reg_defs.h"

#define _VLV_BLC_PWM_CTL2_A (DISPLAY_MMIO_BASE(dev_priv) + 0x61250)
#define _VLV_BLC_PWM_CTL2_B (DISPLAY_MMIO_BASE(dev_priv) + 0x61350)
#define VLV_BLC_PWM_CTL2(pipe) _MMIO_PIPE(pipe, _VLV_BLC_PWM_CTL2_A, \
					 _VLV_BLC_PWM_CTL2_B)

#define _VLV_BLC_PWM_CTL_A (DISPLAY_MMIO_BASE(dev_priv) + 0x61254)
#define _VLV_BLC_PWM_CTL_B (DISPLAY_MMIO_BASE(dev_priv) + 0x61354)
#define VLV_BLC_PWM_CTL(pipe) _MMIO_PIPE(pipe, _VLV_BLC_PWM_CTL_A, \
					_VLV_BLC_PWM_CTL_B)

#define _VLV_BLC_HIST_CTL_A (DISPLAY_MMIO_BASE(dev_priv) + 0x61260)
#define _VLV_BLC_HIST_CTL_B (DISPLAY_MMIO_BASE(dev_priv) + 0x61360)
#define VLV_BLC_HIST_CTL(pipe) _MMIO_PIPE(pipe, _VLV_BLC_HIST_CTL_A, \
					 _VLV_BLC_HIST_CTL_B)

/* Backlight control */
#define BLC_PWM_CTL2	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61250) /* 965+ only */
#define   BLM_PWM_ENABLE		(1 << 31)
#define   BLM_COMBINATION_MODE		(1 << 30) /* gen4 only */
#define   BLM_PIPE_SELECT		(1 << 29)
#define   BLM_PIPE_SELECT_IVB		(3 << 29)
#define   BLM_PIPE_A			(0 << 29)
#define   BLM_PIPE_B			(1 << 29)
#define   BLM_PIPE_C			(2 << 29) /* ivb + */
#define   BLM_TRANSCODER_A		BLM_PIPE_A /* hsw */
#define   BLM_TRANSCODER_B		BLM_PIPE_B
#define   BLM_TRANSCODER_C		BLM_PIPE_C
#define   BLM_TRANSCODER_EDP		(3 << 29)
#define   BLM_PIPE(pipe)		((pipe) << 29)
#define   BLM_POLARITY_I965		(1 << 28) /* gen4 only */
#define   BLM_PHASE_IN_INTERUPT_STATUS	(1 << 26)
#define   BLM_PHASE_IN_ENABLE		(1 << 25)
#define   BLM_PHASE_IN_INTERUPT_ENABL	(1 << 24)
#define   BLM_PHASE_IN_TIME_BASE_SHIFT	(16)
#define   BLM_PHASE_IN_TIME_BASE_MASK	(0xff << 16)
#define   BLM_PHASE_IN_COUNT_SHIFT	(8)
#define   BLM_PHASE_IN_COUNT_MASK	(0xff << 8)
#define   BLM_PHASE_IN_INCR_SHIFT	(0)
#define   BLM_PHASE_IN_INCR_MASK	(0xff << 0)
#define BLC_PWM_CTL	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61254)
/*
 * This is the most significant 15 bits of the number of backlight cycles in a
 * complete cycle of the modulated backlight control.
 *
 * The actual value is this field multiplied by two.
 */
#define   BACKLIGHT_MODULATION_FREQ_SHIFT	(17)
#define   BACKLIGHT_MODULATION_FREQ_MASK	(0x7fff << 17)
#define   BLM_LEGACY_MODE			(1 << 16) /* gen2 only */
/*
 * This is the number of cycles out of the backlight modulation cycle for which
 * the backlight is on.
 *
 * This field must be no greater than the number of cycles in the complete
 * backlight modulation cycle.
 */
#define   BACKLIGHT_DUTY_CYCLE_SHIFT		(0)
#define   BACKLIGHT_DUTY_CYCLE_MASK		(0xffff)
#define   BACKLIGHT_DUTY_CYCLE_MASK_PNV		(0xfffe)
#define   BLM_POLARITY_PNV			(1 << 0) /* pnv only */

#define BLC_HIST_CTL	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61260)
#define  BLM_HISTOGRAM_ENABLE			(1 << 31)

/* New registers for PCH-split platforms. Safe where new bits show up, the
 * register layout machtes with gen4 BLC_PWM_CTL[12]. */
#define BLC_PWM_CPU_CTL2	_MMIO(0x48250)
#define BLC_PWM_CPU_CTL		_MMIO(0x48254)

#define HSW_BLC_PWM2_CTL	_MMIO(0x48350)

/* PCH CTL1 is totally different, all but the below bits are reserved. CTL2 is
 * like the normal CTL from gen4 and earlier. Hooray for confusing naming. */
#define BLC_PWM_PCH_CTL1	_MMIO(0xc8250)
#define   BLM_PCH_PWM_ENABLE			(1 << 31)
#define   BLM_PCH_OVERRIDE_ENABLE		(1 << 30)
#define   BLM_PCH_POLARITY			(1 << 29)
#define BLC_PWM_PCH_CTL2	_MMIO(0xc8254)

/* BXT backlight register definition. */
#define _BXT_BLC_PWM_CTL1			0xC8250
#define   BXT_BLC_PWM_ENABLE			(1 << 31)
#define   BXT_BLC_PWM_POLARITY			(1 << 29)
#define _BXT_BLC_PWM_FREQ1			0xC8254
#define _BXT_BLC_PWM_DUTY1			0xC8258

#define _BXT_BLC_PWM_CTL2			0xC8350
#define _BXT_BLC_PWM_FREQ2			0xC8354
#define _BXT_BLC_PWM_DUTY2			0xC8358

#define BXT_BLC_PWM_CTL(controller)    _MMIO_PIPE(controller,		\
					_BXT_BLC_PWM_CTL1, _BXT_BLC_PWM_CTL2)
#define BXT_BLC_PWM_FREQ(controller)   _MMIO_PIPE(controller, \
					_BXT_BLC_PWM_FREQ1, _BXT_BLC_PWM_FREQ2)
#define BXT_BLC_PWM_DUTY(controller)   _MMIO_PIPE(controller, \
					_BXT_BLC_PWM_DUTY1, _BXT_BLC_PWM_DUTY2)

/* Utility pin */
#define UTIL_PIN_CTL			_MMIO(0x48400)
#define   UTIL_PIN_ENABLE		(1 << 31)
#define   UTIL_PIN_PIPE_MASK		(3 << 29)
#define   UTIL_PIN_PIPE(x)		((x) << 29)
#define   UTIL_PIN_MODE_MASK		(0xf << 24)
#define   UTIL_PIN_MODE_DATA		(0 << 24)
#define   UTIL_PIN_MODE_PWM		(1 << 24)
#define   UTIL_PIN_MODE_VBLANK		(4 << 24)
#define   UTIL_PIN_MODE_VSYNC		(5 << 24)
#define   UTIL_PIN_MODE_EYE_LEVEL	(8 << 24)
#define   UTIL_PIN_OUTPUT_DATA		(1 << 23)
#define   UTIL_PIN_POLARITY		(1 << 22)
#define   UTIL_PIN_DIRECTION_INPUT	(1 << 19)
#define   UTIL_PIN_INPUT_DATA		(1 << 16)

#endif /* __INTEL_BACKLIGHT_REGS_H__ */
