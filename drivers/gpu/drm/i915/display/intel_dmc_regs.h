/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DMC_REGS_H__
#define __INTEL_DMC_REGS_H__

#include "i915_reg_defs.h"

#define DMC_PROGRAM(addr, i)	_MMIO((addr) + (i) * 4)
#define DMC_SSP_BASE_ADDR_GEN9	0x00002FC0

#define _ADLP_PIPEDMC_REG_MMIO_BASE_A	0x5f000
#define _TGL_PIPEDMC_REG_MMIO_BASE_A	0x92000

#define __PIPEDMC_REG_MMIO_BASE(i915, dmc_id) \
	((DISPLAY_VER(i915) >= 13 ? _ADLP_PIPEDMC_REG_MMIO_BASE_A : \
				    _TGL_PIPEDMC_REG_MMIO_BASE_A) + \
	 0x400 * ((dmc_id) - 1))

#define __DMC_REG_MMIO_BASE		0x8f000

#define _DMC_REG_MMIO_BASE(i915, dmc_id) \
	((dmc_id) == DMC_FW_MAIN ? __DMC_REG_MMIO_BASE : \
				   __PIPEDMC_REG_MMIO_BASE(i915, dmc_id))

#define _DMC_REG(i915, dmc_id, reg) \
	((reg) - __DMC_REG_MMIO_BASE + _DMC_REG_MMIO_BASE(i915, dmc_id))

#define _DMC_EVT_HTP_0			0x8f004

#define DMC_EVT_HTP(i915, dmc_id, handler) \
	_MMIO(_DMC_REG(i915, dmc_id, _DMC_EVT_HTP_0) + 4 * (handler))

#define _DMC_EVT_CTL_0			0x8f034

#define DMC_EVT_CTL(i915, dmc_id, handler) \
	_MMIO(_DMC_REG(i915, dmc_id, _DMC_EVT_CTL_0) + 4 * (handler))

#define DMC_EVT_CTL_ENABLE		REG_BIT(31)
#define DMC_EVT_CTL_RECURRING		REG_BIT(30)
#define DMC_EVT_CTL_TYPE_MASK		REG_GENMASK(17, 16)
#define DMC_EVT_CTL_TYPE_LEVEL_0	0
#define DMC_EVT_CTL_TYPE_LEVEL_1	1
#define DMC_EVT_CTL_TYPE_EDGE_1_0	2
#define DMC_EVT_CTL_TYPE_EDGE_0_1	3

#define DMC_EVT_CTL_EVENT_ID_MASK	REG_GENMASK(15, 8)
#define DMC_EVT_CTL_EVENT_ID_FALSE	0x01
/* An event handler scheduled to run at a 1 kHz frequency. */
#define DMC_EVT_CTL_EVENT_ID_CLK_MSEC	0xbf

#define DMC_HTP_ADDR_SKL	0x00500034
#define DMC_SSP_BASE		_MMIO(0x8F074)
#define DMC_HTP_SKL		_MMIO(0x8F004)
#define DMC_LAST_WRITE		_MMIO(0x8F034)
#define DMC_LAST_WRITE_VALUE	0xc003b400
#define DMC_MMIO_START_RANGE	0x80000
#define DMC_MMIO_END_RANGE     0x8FFFF
#define DMC_V1_MMIO_START_RANGE		0x80000
#define TGL_MAIN_MMIO_START		0x8F000
#define TGL_MAIN_MMIO_END		0x8FFFF
#define _TGL_PIPEA_MMIO_START		0x92000
#define _TGL_PIPEA_MMIO_END		0x93FFF
#define _TGL_PIPEB_MMIO_START		0x96000
#define _TGL_PIPEB_MMIO_END		0x97FFF
#define ADLP_PIPE_MMIO_START		0x5F000
#define ADLP_PIPE_MMIO_END		0x5FFFF

#define TGL_PIPE_MMIO_START(dmc_id)	_PICK_EVEN(((dmc_id) - 1), _TGL_PIPEA_MMIO_START,\
					      _TGL_PIPEB_MMIO_START)

#define TGL_PIPE_MMIO_END(dmc_id)	_PICK_EVEN(((dmc_id) - 1), _TGL_PIPEA_MMIO_END,\
					      _TGL_PIPEB_MMIO_END)

#define SKL_DMC_DC3_DC5_COUNT	_MMIO(0x80030)
#define SKL_DMC_DC5_DC6_COUNT	_MMIO(0x8002C)
#define BXT_DMC_DC3_DC5_COUNT	_MMIO(0x80038)
#define TGL_DMC_DEBUG_DC5_COUNT	_MMIO(0x101084)
#define TGL_DMC_DEBUG_DC6_COUNT	_MMIO(0x101088)
#define DG1_DMC_DEBUG_DC5_COUNT	_MMIO(0x134154)

#define TGL_DMC_DEBUG3		_MMIO(0x101090)
#define DG1_DMC_DEBUG3		_MMIO(0x13415c)

#endif /* __INTEL_DMC_REGS_H__ */
