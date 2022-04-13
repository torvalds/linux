/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_PCI_CONFIG_H__
#define __INTEL_PCI_CONFIG_H__

/* BSM in include/drm/i915_drm.h */

#define MCHBAR_I915				0x44
#define MCHBAR_I965				0x48
#define   MCHBAR_SIZE				(4 * 4096)

#define DEVEN					0x54
#define   DEVEN_MCHBAR_EN			(1 << 28)

#define HPLLCC					0xc0 /* 85x only */
#define   GC_CLOCK_CONTROL_MASK			(0x7 << 0)
#define   GC_CLOCK_133_200			(0 << 0)
#define   GC_CLOCK_100_200			(1 << 0)
#define   GC_CLOCK_100_133			(2 << 0)
#define   GC_CLOCK_133_266			(3 << 0)
#define   GC_CLOCK_133_200_2			(4 << 0)
#define   GC_CLOCK_133_266_2			(5 << 0)
#define   GC_CLOCK_166_266			(6 << 0)
#define   GC_CLOCK_166_250			(7 << 0)

#define I915_GDRST				0xc0
#define   GRDOM_FULL				(0 << 2)
#define   GRDOM_RENDER				(1 << 2)
#define   GRDOM_MEDIA				(3 << 2)
#define   GRDOM_MASK				(3 << 2)
#define   GRDOM_RESET_STATUS			(1 << 1)
#define   GRDOM_RESET_ENABLE			(1 << 0)

/* BSpec only has register offset, PCI device and bit found empirically */
#define I830_CLOCK_GATE				0xc8 /* device 0 */
#define   I830_L2_CACHE_CLOCK_GATE_DISABLE	(1 << 2)

#define GCDGMBUS				0xcc

#define GCFGC2					0xda
#define GCFGC					0xf0 /* 915+ only */
#define   GC_LOW_FREQUENCY_ENABLE		(1 << 7)
#define   GC_DISPLAY_CLOCK_190_200_MHZ		(0 << 4)
#define   GC_DISPLAY_CLOCK_333_320_MHZ		(4 << 4)
#define   GC_DISPLAY_CLOCK_267_MHZ_PNV		(0 << 4)
#define   GC_DISPLAY_CLOCK_333_MHZ_PNV		(1 << 4)
#define   GC_DISPLAY_CLOCK_444_MHZ_PNV		(2 << 4)
#define   GC_DISPLAY_CLOCK_200_MHZ_PNV		(5 << 4)
#define   GC_DISPLAY_CLOCK_133_MHZ_PNV		(6 << 4)
#define   GC_DISPLAY_CLOCK_167_MHZ_PNV		(7 << 4)
#define   GC_DISPLAY_CLOCK_MASK			(7 << 4)
#define   GM45_GC_RENDER_CLOCK_MASK		(0xf << 0)
#define   GM45_GC_RENDER_CLOCK_266_MHZ		(8 << 0)
#define   GM45_GC_RENDER_CLOCK_320_MHZ		(9 << 0)
#define   GM45_GC_RENDER_CLOCK_400_MHZ		(0xb << 0)
#define   GM45_GC_RENDER_CLOCK_533_MHZ		(0xc << 0)
#define   I965_GC_RENDER_CLOCK_MASK		(0xf << 0)
#define   I965_GC_RENDER_CLOCK_267_MHZ		(2 << 0)
#define   I965_GC_RENDER_CLOCK_333_MHZ		(3 << 0)
#define   I965_GC_RENDER_CLOCK_444_MHZ		(4 << 0)
#define   I965_GC_RENDER_CLOCK_533_MHZ		(5 << 0)
#define   I945_GC_RENDER_CLOCK_MASK		(7 << 0)
#define   I945_GC_RENDER_CLOCK_166_MHZ		(0 << 0)
#define   I945_GC_RENDER_CLOCK_200_MHZ		(1 << 0)
#define   I945_GC_RENDER_CLOCK_250_MHZ		(3 << 0)
#define   I945_GC_RENDER_CLOCK_400_MHZ		(5 << 0)
#define   I915_GC_RENDER_CLOCK_MASK		(7 << 0)
#define   I915_GC_RENDER_CLOCK_166_MHZ		(0 << 0)
#define   I915_GC_RENDER_CLOCK_200_MHZ		(1 << 0)
#define   I915_GC_RENDER_CLOCK_333_MHZ		(4 << 0)

#define ASLE					0xe4
#define ASLS					0xfc

#define SWSCI					0xe8
#define   SWSCI_SCISEL				(1 << 15)
#define   SWSCI_GSSCIE				(1 << 0)

/* legacy/combination backlight modes, also called LBB */
#define LBPC					0xf4

#endif /* __INTEL_PCI_CONFIG_H__ */
