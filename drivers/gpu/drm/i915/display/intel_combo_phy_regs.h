/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_COMBO_PHY_REGS__
#define __INTEL_COMBO_PHY_REGS__

#include "i915_reg_defs.h"

#define _ICL_COMBOPHY_A				0x162000
#define _ICL_COMBOPHY_B				0x6C000
#define _EHL_COMBOPHY_C				0x160000
#define _RKL_COMBOPHY_D				0x161000
#define _ADL_COMBOPHY_E				0x16B000

#define _ICL_COMBOPHY(phy)			_PICK(phy, _ICL_COMBOPHY_A, \
						      _ICL_COMBOPHY_B, \
						      _EHL_COMBOPHY_C, \
						      _RKL_COMBOPHY_D, \
						      _ADL_COMBOPHY_E)

/* ICL Port CL_DW registers */
#define _ICL_PORT_CL_DW(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 4 * (dw))

#define ICL_PORT_CL_DW5(phy)			_MMIO(_ICL_PORT_CL_DW(5, phy))
#define   CL_POWER_DOWN_ENABLE			(1 << 4)
#define   SUS_CLOCK_CONFIG			(3 << 0)

#define ICL_PORT_CL_DW10(phy)			_MMIO(_ICL_PORT_CL_DW(10, phy))
#define  PG_SEQ_DELAY_OVERRIDE_MASK		(3 << 25)
#define  PG_SEQ_DELAY_OVERRIDE_SHIFT		25
#define  PG_SEQ_DELAY_OVERRIDE_ENABLE		(1 << 24)
#define  PWR_UP_ALL_LANES			(0x0 << 4)
#define  PWR_DOWN_LN_3_2_1			(0xe << 4)
#define  PWR_DOWN_LN_3_2			(0xc << 4)
#define  PWR_DOWN_LN_3				(0x8 << 4)
#define  PWR_DOWN_LN_2_1_0			(0x7 << 4)
#define  PWR_DOWN_LN_1_0			(0x3 << 4)
#define  PWR_DOWN_LN_3_1			(0xa << 4)
#define  PWR_DOWN_LN_3_1_0			(0xb << 4)
#define  PWR_DOWN_LN_MASK			(0xf << 4)
#define  PWR_DOWN_LN_SHIFT			4
#define  EDP4K2K_MODE_OVRD_EN			(1 << 3)
#define  EDP4K2K_MODE_OVRD_OPTIMIZED		(1 << 2)

#define ICL_PORT_CL_DW12(phy)			_MMIO(_ICL_PORT_CL_DW(12, phy))
#define   ICL_LANE_ENABLE_AUX			(1 << 0)

/* ICL Port COMP_DW registers */
#define _ICL_PORT_COMP				0x100
#define _ICL_PORT_COMP_DW(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 _ICL_PORT_COMP + 4 * (dw))

#define ICL_PORT_COMP_DW0(phy)			_MMIO(_ICL_PORT_COMP_DW(0, phy))
#define   COMP_INIT				(1 << 31)

#define ICL_PORT_COMP_DW1(phy)			_MMIO(_ICL_PORT_COMP_DW(1, phy))

#define ICL_PORT_COMP_DW3(phy)			_MMIO(_ICL_PORT_COMP_DW(3, phy))
#define   PROCESS_INFO_DOT_0			(0 << 26)
#define   PROCESS_INFO_DOT_1			(1 << 26)
#define   PROCESS_INFO_DOT_4			(2 << 26)
#define   PROCESS_INFO_MASK			(7 << 26)
#define   PROCESS_INFO_SHIFT			26
#define   VOLTAGE_INFO_0_85V			(0 << 24)
#define   VOLTAGE_INFO_0_95V			(1 << 24)
#define   VOLTAGE_INFO_1_05V			(2 << 24)
#define   VOLTAGE_INFO_MASK			(3 << 24)
#define   VOLTAGE_INFO_SHIFT			24

#define ICL_PORT_COMP_DW8(phy)			_MMIO(_ICL_PORT_COMP_DW(8, phy))
#define   IREFGEN				(1 << 24)

#define ICL_PORT_COMP_DW9(phy)			_MMIO(_ICL_PORT_COMP_DW(9, phy))

#define ICL_PORT_COMP_DW10(phy)			_MMIO(_ICL_PORT_COMP_DW(10, phy))

/* ICL Port PCS registers */
#define _ICL_PORT_PCS_AUX			0x300
#define _ICL_PORT_PCS_GRP			0x600
#define _ICL_PORT_PCS_LN(ln)			(0x800 + (ln) * 0x100)
#define _ICL_PORT_PCS_DW_AUX(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 _ICL_PORT_PCS_AUX + 4 * (dw))
#define _ICL_PORT_PCS_DW_GRP(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 _ICL_PORT_PCS_GRP + 4 * (dw))
#define _ICL_PORT_PCS_DW_LN(dw, ln, phy)	 (_ICL_COMBOPHY(phy) + \
						  _ICL_PORT_PCS_LN(ln) + 4 * (dw))
#define ICL_PORT_PCS_DW1_AUX(phy)		_MMIO(_ICL_PORT_PCS_DW_AUX(1, phy))
#define ICL_PORT_PCS_DW1_GRP(phy)		_MMIO(_ICL_PORT_PCS_DW_GRP(1, phy))
#define ICL_PORT_PCS_DW1_LN(ln, phy)		_MMIO(_ICL_PORT_PCS_DW_LN(1, ln, phy))
#define   DCC_MODE_SELECT_MASK			REG_GENMASK(21, 20)
#define   RUN_DCC_ONCE				REG_FIELD_PREP(DCC_MODE_SELECT_MASK, 0)
#define   COMMON_KEEPER_EN			(1 << 26)
#define   LATENCY_OPTIM_MASK			(0x3 << 2)
#define   LATENCY_OPTIM_VAL(x)			((x) << 2)

/* ICL Port TX registers */
#define _ICL_PORT_TX_AUX			0x380
#define _ICL_PORT_TX_GRP			0x680
#define _ICL_PORT_TX_LN(ln)			(0x880 + (ln) * 0x100)

#define _ICL_PORT_TX_DW_AUX(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 _ICL_PORT_TX_AUX + 4 * (dw))
#define _ICL_PORT_TX_DW_GRP(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 _ICL_PORT_TX_GRP + 4 * (dw))
#define _ICL_PORT_TX_DW_LN(dw, ln, phy) 	(_ICL_COMBOPHY(phy) + \
						  _ICL_PORT_TX_LN(ln) + 4 * (dw))

#define ICL_PORT_TX_DW2_AUX(phy)		_MMIO(_ICL_PORT_TX_DW_AUX(2, phy))
#define ICL_PORT_TX_DW2_GRP(phy)		_MMIO(_ICL_PORT_TX_DW_GRP(2, phy))
#define ICL_PORT_TX_DW2_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(2, ln, phy))
#define   SWING_SEL_UPPER(x)			(((x) >> 3) << 15)
#define   SWING_SEL_UPPER_MASK			(1 << 15)
#define   SWING_SEL_LOWER(x)			(((x) & 0x7) << 11)
#define   SWING_SEL_LOWER_MASK			(0x7 << 11)
#define   FRC_LATENCY_OPTIM_MASK		(0x7 << 8)
#define   FRC_LATENCY_OPTIM_VAL(x)		((x) << 8)
#define   RCOMP_SCALAR(x)			((x) << 0)
#define   RCOMP_SCALAR_MASK			(0xFF << 0)

#define ICL_PORT_TX_DW4_AUX(phy)		_MMIO(_ICL_PORT_TX_DW_AUX(4, phy))
#define ICL_PORT_TX_DW4_GRP(phy)		_MMIO(_ICL_PORT_TX_DW_GRP(4, phy))
#define ICL_PORT_TX_DW4_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(4, ln, phy))
#define   LOADGEN_SELECT			(1 << 31)
#define   POST_CURSOR_1(x)			((x) << 12)
#define   POST_CURSOR_1_MASK			(0x3F << 12)
#define   POST_CURSOR_2(x)			((x) << 6)
#define   POST_CURSOR_2_MASK			(0x3F << 6)
#define   CURSOR_COEFF(x)			((x) << 0)
#define   CURSOR_COEFF_MASK			(0x3F << 0)

#define ICL_PORT_TX_DW5_AUX(phy)		_MMIO(_ICL_PORT_TX_DW_AUX(5, phy))
#define ICL_PORT_TX_DW5_GRP(phy)		_MMIO(_ICL_PORT_TX_DW_GRP(5, phy))
#define ICL_PORT_TX_DW5_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(5, ln, phy))
#define   TX_TRAINING_EN			(1 << 31)
#define   TAP2_DISABLE				(1 << 30)
#define   TAP3_DISABLE				(1 << 29)
#define   SCALING_MODE_SEL(x)			((x) << 18)
#define   SCALING_MODE_SEL_MASK			(0x7 << 18)
#define   RTERM_SELECT(x)			((x) << 3)
#define   RTERM_SELECT_MASK			(0x7 << 3)

#define ICL_PORT_TX_DW7_AUX(phy)		_MMIO(_ICL_PORT_TX_DW_AUX(7, phy))
#define ICL_PORT_TX_DW7_GRP(phy)		_MMIO(_ICL_PORT_TX_DW_GRP(7, phy))
#define ICL_PORT_TX_DW7_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(7, ln, phy))
#define   N_SCALAR(x)				((x) << 24)
#define   N_SCALAR_MASK				(0x7F << 24)

#define ICL_PORT_TX_DW8_AUX(phy)		_MMIO(_ICL_PORT_TX_DW_AUX(8, phy))
#define ICL_PORT_TX_DW8_GRP(phy)		_MMIO(_ICL_PORT_TX_DW_GRP(8, phy))
#define ICL_PORT_TX_DW8_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(8, ln, phy))
#define   ICL_PORT_TX_DW8_ODCC_CLK_SEL		REG_BIT(31)
#define   ICL_PORT_TX_DW8_ODCC_CLK_DIV_SEL_MASK	REG_GENMASK(30, 29)
#define   ICL_PORT_TX_DW8_ODCC_CLK_DIV_SEL_DIV2	REG_FIELD_PREP(ICL_PORT_TX_DW8_ODCC_CLK_DIV_SEL_MASK, 0x1)

#define _ICL_DPHY_CHKN_REG			0x194
#define ICL_DPHY_CHKN(port)			_MMIO(_ICL_COMBOPHY(port) + _ICL_DPHY_CHKN_REG)
#define   ICL_DPHY_CHKN_AFE_OVER_PPI_STRAP	REG_BIT(7)

#endif /* __INTEL_COMBO_PHY_REGS__ */
