/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __BXT_DPIO_PHY_REGS_H__
#define __BXT_DPIO_PHY_REGS_H__

#include "intel_display_reg_defs.h"

/* BXT PHY registers */
#define _BXT_PHY0_BASE			0x6C000
#define _BXT_PHY1_BASE			0x162000
#define _BXT_PHY2_BASE			0x163000
#define BXT_PHY_BASE(phy) \
	 _PICK_EVEN_2RANGES(phy, 1, \
			    _BXT_PHY0_BASE, _BXT_PHY0_BASE, \
			    _BXT_PHY1_BASE, _BXT_PHY2_BASE)

#define _BXT_PHY(phy, reg) \
	_MMIO(BXT_PHY_BASE(phy) - _BXT_PHY0_BASE + (reg))

#define _BXT_PHY_CH(phy, ch, reg_ch0, reg_ch1) \
	(BXT_PHY_BASE(phy) + _PIPE((ch), (reg_ch0) - _BXT_PHY0_BASE, \
					 (reg_ch1) - _BXT_PHY0_BASE))
#define _MMIO_BXT_PHY_CH(phy, ch, reg_ch0, reg_ch1) \
	_MMIO(_BXT_PHY_CH(phy, ch, reg_ch0, reg_ch1))
#define _BXT_LANE_OFFSET(lane)           (((lane) >> 1) * 0x200 + \
					  ((lane) & 1) * 0x80)
#define _MMIO_BXT_PHY_CH_LN(phy, ch, lane, reg_ch0, reg_ch1) \
	_MMIO(_BXT_PHY_CH(phy, ch, reg_ch0, reg_ch1) + _BXT_LANE_OFFSET(lane))

/* BXT PHY PLL registers */
#define _PORT_PLL_A			0x46074
#define _PORT_PLL_B			0x46078
#define _PORT_PLL_C			0x4607c
#define   PORT_PLL_ENABLE		REG_BIT(31)
#define   PORT_PLL_LOCK			REG_BIT(30)
#define   PORT_PLL_REF_SEL		REG_BIT(27)
#define   PORT_PLL_POWER_ENABLE		REG_BIT(26)
#define   PORT_PLL_POWER_STATE		REG_BIT(25)
#define BXT_PORT_PLL_ENABLE(port)	_MMIO_PORT(port, _PORT_PLL_A, _PORT_PLL_B)

#define _PORT_PLL_EBB_0_A		0x162034
#define _PORT_PLL_EBB_0_B		0x6C034
#define _PORT_PLL_EBB_0_C		0x6C340
#define   PORT_PLL_P1_MASK		REG_GENMASK(15, 13)
#define   PORT_PLL_P1(p1)		REG_FIELD_PREP(PORT_PLL_P1_MASK, (p1))
#define   PORT_PLL_P2_MASK		REG_GENMASK(12, 8)
#define   PORT_PLL_P2(p2)		REG_FIELD_PREP(PORT_PLL_P2_MASK, (p2))
#define BXT_PORT_PLL_EBB_0(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PLL_EBB_0_B, \
							 _PORT_PLL_EBB_0_C)

#define _PORT_PLL_EBB_4_A		0x162038
#define _PORT_PLL_EBB_4_B		0x6C038
#define _PORT_PLL_EBB_4_C		0x6C344
#define   PORT_PLL_RECALIBRATE		REG_BIT(14)
#define   PORT_PLL_10BIT_CLK_ENABLE	REG_BIT(13)
#define BXT_PORT_PLL_EBB_4(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PLL_EBB_4_B, \
							 _PORT_PLL_EBB_4_C)

#define _PORT_PLL_0_A			0x162100
#define _PORT_PLL_0_B			0x6C100
#define _PORT_PLL_0_C			0x6C380
/* PORT_PLL_0_A */
#define   PORT_PLL_M2_INT_MASK		REG_GENMASK(7, 0)
#define   PORT_PLL_M2_INT(m2_int)	REG_FIELD_PREP(PORT_PLL_M2_INT_MASK, (m2_int))
/* PORT_PLL_1_A */
#define   PORT_PLL_N_MASK		REG_GENMASK(11, 8)
#define   PORT_PLL_N(n)			REG_FIELD_PREP(PORT_PLL_N_MASK, (n))
/* PORT_PLL_2_A */
#define   PORT_PLL_M2_FRAC_MASK		REG_GENMASK(21, 0)
#define   PORT_PLL_M2_FRAC(m2_frac)	REG_FIELD_PREP(PORT_PLL_M2_FRAC_MASK, (m2_frac))
/* PORT_PLL_3_A */
#define   PORT_PLL_M2_FRAC_ENABLE	REG_BIT(16)
/* PORT_PLL_6_A */
#define   PORT_PLL_GAIN_CTL_MASK	REG_GENMASK(18, 16)
#define   PORT_PLL_GAIN_CTL(x)		REG_FIELD_PREP(PORT_PLL_GAIN_CTL_MASK, (x))
#define   PORT_PLL_INT_COEFF_MASK	REG_GENMASK(12, 8)
#define   PORT_PLL_INT_COEFF(x)		REG_FIELD_PREP(PORT_PLL_INT_COEFF_MASK, (x))
#define   PORT_PLL_PROP_COEFF_MASK	REG_GENMASK(3, 0)
#define   PORT_PLL_PROP_COEFF(x)	REG_FIELD_PREP(PORT_PLL_PROP_COEFF_MASK, (x))
/* PORT_PLL_8_A */
#define   PORT_PLL_TARGET_CNT_MASK	REG_GENMASK(9, 0)
#define   PORT_PLL_TARGET_CNT(x)	REG_FIELD_PREP(PORT_PLL_TARGET_CNT_MASK, (x))
/* PORT_PLL_9_A */
#define  PORT_PLL_LOCK_THRESHOLD_MASK	REG_GENMASK(3, 1)
#define  PORT_PLL_LOCK_THRESHOLD(x)	REG_FIELD_PREP(PORT_PLL_LOCK_THRESHOLD_MASK, (x))
/* PORT_PLL_10_A */
#define  PORT_PLL_DCO_AMP_OVR_EN_H	REG_BIT(27)
#define  PORT_PLL_DCO_AMP_MASK		REG_GENMASK(13, 10)
#define  PORT_PLL_DCO_AMP(x)		REG_FIELD_PREP(PORT_PLL_DCO_AMP_MASK, (x))
#define _PORT_PLL_BASE(phy, ch)		_BXT_PHY_CH(phy, ch, \
						    _PORT_PLL_0_B, \
						    _PORT_PLL_0_C)
#define BXT_PORT_PLL(phy, ch, idx)	_MMIO(_PORT_PLL_BASE(phy, ch) + \
					      (idx) * 4)

/* BXT PHY common lane registers */
#define _PORT_CL1CM_DW0_A		0x162000
#define _PORT_CL1CM_DW0_BC		0x6C000
#define   PHY_POWER_GOOD		REG_BIT(16)
#define   PHY_RESERVED			REG_BIT(7)
#define BXT_PORT_CL1CM_DW0(phy)		_BXT_PHY((phy), _PORT_CL1CM_DW0_BC)

#define _PORT_CL1CM_DW9_A		0x162024
#define _PORT_CL1CM_DW9_BC		0x6C024
#define   IREF0RC_OFFSET_MASK		REG_GENMASK(15, 8)
#define   IREF0RC_OFFSET(x)		REG_FIELD_PREP(IREF0RC_OFFSET_MASK, (x))
#define BXT_PORT_CL1CM_DW9(phy)		_BXT_PHY((phy), _PORT_CL1CM_DW9_BC)

#define _PORT_CL1CM_DW10_A		0x162028
#define _PORT_CL1CM_DW10_BC		0x6C028
#define   IREF1RC_OFFSET_MASK		REG_GENMASK(15, 8)
#define   IREF1RC_OFFSET(x)		REG_FIELD_PREP(IREF1RC_OFFSET_MASK, (x))
#define BXT_PORT_CL1CM_DW10(phy)	_BXT_PHY((phy), _PORT_CL1CM_DW10_BC)

#define _PORT_CL1CM_DW28_A		0x162070
#define _PORT_CL1CM_DW28_BC		0x6C070
#define   OCL1_POWER_DOWN_EN		REG_BIT(23)
#define   DW28_OLDO_DYN_PWR_DOWN_EN	REG_BIT(22)
#define   SUS_CLK_CONFIG		REG_GENMASK(1, 0)
#define BXT_PORT_CL1CM_DW28(phy)	_BXT_PHY((phy), _PORT_CL1CM_DW28_BC)

#define _PORT_CL1CM_DW30_A		0x162078
#define _PORT_CL1CM_DW30_BC		0x6C078
#define   OCL2_LDOFUSE_PWR_DIS		REG_BIT(6)
#define BXT_PORT_CL1CM_DW30(phy)	_BXT_PHY((phy), _PORT_CL1CM_DW30_BC)

/* The spec defines this only for BXT PHY0, but lets assume that this
 * would exist for PHY1 too if it had a second channel.
 */
#define _PORT_CL2CM_DW6_A		0x162358
#define _PORT_CL2CM_DW6_BC		0x6C358
#define BXT_PORT_CL2CM_DW6(phy)		_BXT_PHY((phy), _PORT_CL2CM_DW6_BC)
#define   DW6_OLDO_DYN_PWR_DOWN_EN	REG_BIT(28)

/* BXT PHY Ref registers */
#define _PORT_REF_DW3_A			0x16218C
#define _PORT_REF_DW3_BC		0x6C18C
#define   GRC_DONE			REG_BIT(22)
#define BXT_PORT_REF_DW3(phy)		_BXT_PHY((phy), _PORT_REF_DW3_BC)

#define _PORT_REF_DW6_A			0x162198
#define _PORT_REF_DW6_BC		0x6C198
#define   GRC_CODE_MASK			REG_GENMASK(31, 24)
#define   GRC_CODE(x)			REG_FIELD_PREP(GRC_CODE_MASK, (x))
#define   GRC_CODE_FAST_MASK		REG_GENMASK(23, 16)
#define   GRC_CODE_FAST(x)		REG_FIELD_PREP(GRC_CODE_FAST_MASK, (x))
#define   GRC_CODE_SLOW_MASK		REG_GENMASK(15, 8)
#define   GRC_CODE_SLOW(x)		REG_FIELD_PREP(GRC_CODE_SLOW_MASK, (x))
#define   GRC_CODE_NOM_MASK		REG_GENMASK(7, 0)
#define   GRC_CODE_NOM(x)		REG_FIELD_PREP(GRC_CODE_NOM_MASK, (x))
#define BXT_PORT_REF_DW6(phy)		_BXT_PHY((phy), _PORT_REF_DW6_BC)

#define _PORT_REF_DW8_A			0x1621A0
#define _PORT_REF_DW8_BC		0x6C1A0
#define   GRC_DIS			REG_BIT(15)
#define   GRC_RDY_OVRD			REG_BIT(1)
#define BXT_PORT_REF_DW8(phy)		_BXT_PHY((phy), _PORT_REF_DW8_BC)

/* BXT PHY PCS registers */
#define _PORT_PCS_DW10_LN01_A		0x162428
#define _PORT_PCS_DW10_LN01_B		0x6C428
#define _PORT_PCS_DW10_LN01_C		0x6C828
#define _PORT_PCS_DW10_GRP_A		0x162C28
#define _PORT_PCS_DW10_GRP_B		0x6CC28
#define _PORT_PCS_DW10_GRP_C		0x6CE28
#define BXT_PORT_PCS_DW10_LN01(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW10_LN01_B, \
							 _PORT_PCS_DW10_LN01_C)
#define BXT_PORT_PCS_DW10_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW10_GRP_B, \
							 _PORT_PCS_DW10_GRP_C)

#define   TX2_SWING_CALC_INIT		REG_BIT(31)
#define   TX1_SWING_CALC_INIT		REG_BIT(30)

#define _PORT_PCS_DW12_LN01_A		0x162430
#define _PORT_PCS_DW12_LN01_B		0x6C430
#define _PORT_PCS_DW12_LN01_C		0x6C830
#define _PORT_PCS_DW12_LN23_A		0x162630
#define _PORT_PCS_DW12_LN23_B		0x6C630
#define _PORT_PCS_DW12_LN23_C		0x6CA30
#define _PORT_PCS_DW12_GRP_A		0x162c30
#define _PORT_PCS_DW12_GRP_B		0x6CC30
#define _PORT_PCS_DW12_GRP_C		0x6CE30
#define   LANESTAGGER_STRAP_OVRD	REG_BIT(6)
#define   LANE_STAGGER_MASK		REG_GENMASK(4, 0)
#define BXT_PORT_PCS_DW12_LN01(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW12_LN01_B, \
							 _PORT_PCS_DW12_LN01_C)
#define BXT_PORT_PCS_DW12_LN23(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW12_LN23_B, \
							 _PORT_PCS_DW12_LN23_C)
#define BXT_PORT_PCS_DW12_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW12_GRP_B, \
							 _PORT_PCS_DW12_GRP_C)

/* BXT PHY TX registers */
#define _PORT_TX_DW2_LN0_A		0x162508
#define _PORT_TX_DW2_LN0_B		0x6C508
#define _PORT_TX_DW2_LN0_C		0x6C908
#define _PORT_TX_DW2_GRP_A		0x162D08
#define _PORT_TX_DW2_GRP_B		0x6CD08
#define _PORT_TX_DW2_GRP_C		0x6CF08
#define BXT_PORT_TX_DW2_LN(phy, ch, lane)	_MMIO_BXT_PHY_CH_LN(phy, ch, lane, \
								    _PORT_TX_DW2_LN0_B,	\
								    _PORT_TX_DW2_LN0_C)
#define BXT_PORT_TX_DW2_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW2_GRP_B, \
							 _PORT_TX_DW2_GRP_C)
#define   MARGIN_000_MASK		REG_GENMASK(23, 16)
#define   MARGIN_000(x)			REG_FIELD_PREP(MARGIN_000_MASK, (x))
#define   UNIQ_TRANS_SCALE_MASK		REG_GENMASK(15, 8)
#define   UNIQ_TRANS_SCALE(x)		REG_FIELD_PREP(UNIQ_TRANS_SCALE_MASK, (x))

#define _PORT_TX_DW3_LN0_A		0x16250C
#define _PORT_TX_DW3_LN0_B		0x6C50C
#define _PORT_TX_DW3_LN0_C		0x6C90C
#define _PORT_TX_DW3_GRP_A		0x162D0C
#define _PORT_TX_DW3_GRP_B		0x6CD0C
#define _PORT_TX_DW3_GRP_C		0x6CF0C
#define BXT_PORT_TX_DW3_LN(phy, ch, lane)	_MMIO_BXT_PHY_CH_LN(phy, ch, lane, \
								    _PORT_TX_DW3_LN0_B, \
								    _PORT_TX_DW3_LN0_C)
#define BXT_PORT_TX_DW3_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW3_GRP_B, \
							 _PORT_TX_DW3_GRP_C)
#define   SCALE_DCOMP_METHOD		REG_BIT(26)
#define   UNIQUE_TRANGE_EN_METHOD	REG_BIT(27)

#define _PORT_TX_DW4_LN0_A		0x162510
#define _PORT_TX_DW4_LN0_B		0x6C510
#define _PORT_TX_DW4_LN0_C		0x6C910
#define _PORT_TX_DW4_GRP_A		0x162D10
#define _PORT_TX_DW4_GRP_B		0x6CD10
#define _PORT_TX_DW4_GRP_C		0x6CF10
#define BXT_PORT_TX_DW4_LN(phy, ch, lane)	_MMIO_BXT_PHY_CH_LN(phy, ch, lane, \
								    _PORT_TX_DW4_LN0_B, \
								    _PORT_TX_DW4_LN0_C)
#define BXT_PORT_TX_DW4_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW4_GRP_B, \
							 _PORT_TX_DW4_GRP_C)
#define   DE_EMPHASIS_MASK		REG_GENMASK(31, 24)
#define   DE_EMPHASIS(x)		REG_FIELD_PREP(DE_EMPHASIS_MASK, (x))

#define _PORT_TX_DW5_LN0_A		0x162514
#define _PORT_TX_DW5_LN0_B		0x6C514
#define _PORT_TX_DW5_LN0_C		0x6C914
#define _PORT_TX_DW5_GRP_A		0x162D14
#define _PORT_TX_DW5_GRP_B		0x6CD14
#define _PORT_TX_DW5_GRP_C		0x6CF14
#define BXT_PORT_TX_DW5_LN(phy, ch, lane)	_MMIO_BXT_PHY_CH_LN(phy, ch, lane, \
								    _PORT_TX_DW5_LN0_B, \
								    _PORT_TX_DW5_LN0_C)
#define BXT_PORT_TX_DW5_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW5_GRP_B, \
							 _PORT_TX_DW5_GRP_C)
#define   DCC_DELAY_RANGE_1		REG_BIT(9)
#define   DCC_DELAY_RANGE_2		REG_BIT(8)

#define _PORT_TX_DW14_LN0_A		0x162538
#define _PORT_TX_DW14_LN0_B		0x6C538
#define _PORT_TX_DW14_LN0_C		0x6C938
#define   LATENCY_OPTIM			REG_BIT(30)
#define BXT_PORT_TX_DW14_LN(phy, ch, lane)	_MMIO_BXT_PHY_CH_LN(phy, ch, lane, \
								    _PORT_TX_DW14_LN0_B, \
								    _PORT_TX_DW14_LN0_C)

#endif /* __BXT_DPIO_PHY_REGS_H__ */
