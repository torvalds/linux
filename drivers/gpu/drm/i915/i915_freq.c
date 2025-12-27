// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/drm_print.h>

#include "i915_drv.h"
#include "i915_freq.h"
#include "intel_mchbar_regs.h"

unsigned int i9xx_fsb_freq(struct drm_i915_private *i915)
{
	u32 fsb;

	/*
	 * Note that this only reads the state of the FSB
	 * straps, not the actual FSB frequency. Some BIOSen
	 * let you configure each independently. Ideally we'd
	 * read out the actual FSB frequency but sadly we
	 * don't know which registers have that information,
	 * and all the relevant docs have gone to bit heaven :(
	 */
	fsb = intel_uncore_read(&i915->uncore, CLKCFG) & CLKCFG_FSB_MASK;

	if (IS_PINEVIEW(i915) || IS_MOBILE(i915)) {
		switch (fsb) {
		case CLKCFG_FSB_400:
			return 400000;
		case CLKCFG_FSB_533:
			return 533333;
		case CLKCFG_FSB_667:
			return 666667;
		case CLKCFG_FSB_800:
			return 800000;
		case CLKCFG_FSB_1067:
			return 1066667;
		case CLKCFG_FSB_1333:
			return 1333333;
		default:
			MISSING_CASE(fsb);
			return 1333333;
		}
	} else {
		switch (fsb) {
		case CLKCFG_FSB_400_ALT:
			return 400000;
		case CLKCFG_FSB_533:
			return 533333;
		case CLKCFG_FSB_667:
			return 666667;
		case CLKCFG_FSB_800:
			return 800000;
		case CLKCFG_FSB_1067_ALT:
			return 1066667;
		case CLKCFG_FSB_1333_ALT:
			return 1333333;
		case CLKCFG_FSB_1600_ALT:
			return 1600000;
		default:
			MISSING_CASE(fsb);
			return 1333333;
		}
	}
}

unsigned int ilk_fsb_freq(struct drm_i915_private *i915)
{
	u16 fsb;

	fsb = intel_uncore_read16(&i915->uncore, CSIPLL0) & 0x3ff;

	switch (fsb) {
	case 0x00c:
		return 3200000;
	case 0x00e:
		return 3733333;
	case 0x010:
		return 4266667;
	case 0x012:
		return 4800000;
	case 0x014:
		return 5333333;
	case 0x016:
		return 5866667;
	case 0x018:
		return 6400000;
	default:
		drm_dbg(&i915->drm, "unknown fsb frequency 0x%04x\n", fsb);
		return 0;
	}
}

unsigned int ilk_mem_freq(struct drm_i915_private *i915)
{
	u16 ddrpll;

	ddrpll = intel_uncore_read16(&i915->uncore, DDRMPLL1);
	switch (ddrpll & 0xff) {
	case 0xc:
		return 800000;
	case 0x10:
		return 1066667;
	case 0x14:
		return 1333333;
	case 0x18:
		return 1600000;
	default:
		drm_dbg(&i915->drm, "unknown memory frequency 0x%02x\n",
			ddrpll & 0xff);
		return 0;
	}
}
