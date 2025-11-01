/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2025 Intel Corporation
 */

#ifndef __INTEL_LT_PHY_REGS_H__
#define __INTEL_LT_PHY_REGS_H__

/* LT Phy Vendor Register */
#define LT_PHY_VDR_0_CONFIG	0xC02
#define  LT_PHY_VDR_DP_PLL_ENABLE	REG_BIT(7)
#define LT_PHY_VDR_1_CONFIG	0xC03
#define  LT_PHY_VDR_RATE_ENCODING_MASK	REG_GENMASK8(6, 3)
#define  LT_PHY_VDR_MODE_ENCODING_MASK	REG_GENMASK8(2, 0)
#define LT_PHY_VDR_2_CONFIG	0xCC3

#define LT_PHY_VDR_X_ADDR_MSB(idx)	(0xC04 + 0x6 * (idx))
#define LT_PHY_VDR_X_ADDR_LSB(idx)	(0xC05 + 0x6 * (idx))

#define LT_PHY_VDR_X_DATAY(idx, y)	((0xC06 + (3 - (y))) + 0x6 * (idx))

#define LT_PHY_RATE_UPDATE		0xCC4

#endif /* __INTEL_LT_PHY_REGS_H__ */
