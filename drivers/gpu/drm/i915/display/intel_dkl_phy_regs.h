/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DKL_PHY_REGS__
#define __INTEL_DKL_PHY_REGS__

#define _DKL_PHY1_BASE					0x168000
#define _DKL_PHY2_BASE					0x169000
#define _DKL_PHY3_BASE					0x16A000
#define _DKL_PHY4_BASE					0x16B000
#define _DKL_PHY5_BASE					0x16C000
#define _DKL_PHY6_BASE					0x16D000

#define DKL_REG_TC_PORT(__reg) \
	(TC_PORT_1 + ((__reg).reg - _DKL_PHY1_BASE) / (_DKL_PHY2_BASE - _DKL_PHY1_BASE))

/* DEKEL PHY MMIO Address = Phy base + (internal address & ~index_mask) */
#define _DKL_PCS_DW5					0x14
#define DKL_PCS_DW5(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_PCS_DW5)
#define   DKL_PCS_DW5_CORE_SOFTRESET			REG_BIT(11)

#define _DKL_PLL_DIV0					0x200
#define DKL_PLL_DIV0(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_PLL_DIV0)
#define   DKL_PLL_DIV0_AFC_STARTUP_MASK			REG_GENMASK(27, 25)
#define   DKL_PLL_DIV0_AFC_STARTUP(val)			REG_FIELD_PREP(DKL_PLL_DIV0_AFC_STARTUP_MASK, (val))
#define   DKL_PLL_DIV0_INTEG_COEFF(x)			((x) << 16)
#define   DKL_PLL_DIV0_INTEG_COEFF_MASK			(0x1F << 16)
#define   DKL_PLL_DIV0_PROP_COEFF(x)			((x) << 12)
#define   DKL_PLL_DIV0_PROP_COEFF_MASK			(0xF << 12)
#define   DKL_PLL_DIV0_FBPREDIV_SHIFT			(8)
#define   DKL_PLL_DIV0_FBPREDIV(x)			((x) << DKL_PLL_DIV0_FBPREDIV_SHIFT)
#define   DKL_PLL_DIV0_FBPREDIV_MASK			(0xF << DKL_PLL_DIV0_FBPREDIV_SHIFT)
#define   DKL_PLL_DIV0_FBDIV_INT(x)			((x) << 0)
#define   DKL_PLL_DIV0_FBDIV_INT_MASK			(0xFF << 0)
#define   DKL_PLL_DIV0_MASK				(DKL_PLL_DIV0_INTEG_COEFF_MASK | \
							 DKL_PLL_DIV0_PROP_COEFF_MASK | \
							 DKL_PLL_DIV0_FBPREDIV_MASK | \
							 DKL_PLL_DIV0_FBDIV_INT_MASK)

#define _DKL_PLL_DIV1					0x204
#define DKL_PLL_DIV1(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_PLL_DIV1)
#define   DKL_PLL_DIV1_IREF_TRIM(x)			((x) << 16)
#define   DKL_PLL_DIV1_IREF_TRIM_MASK			(0x1F << 16)
#define   DKL_PLL_DIV1_TDC_TARGET_CNT(x)		((x) << 0)
#define   DKL_PLL_DIV1_TDC_TARGET_CNT_MASK		(0xFF << 0)

#define _DKL_PLL_SSC					0x210
#define DKL_PLL_SSC(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_PLL_SSC)
#define   DKL_PLL_SSC_IREF_NDIV_RATIO(x)		((x) << 29)
#define   DKL_PLL_SSC_IREF_NDIV_RATIO_MASK		(0x7 << 29)
#define   DKL_PLL_SSC_STEP_LEN(x)			((x) << 16)
#define   DKL_PLL_SSC_STEP_LEN_MASK			(0xFF << 16)
#define   DKL_PLL_SSC_STEP_NUM(x)			((x) << 11)
#define   DKL_PLL_SSC_STEP_NUM_MASK			(0x7 << 11)
#define   DKL_PLL_SSC_EN				(1 << 9)

#define _DKL_PLL_BIAS					0x214
#define DKL_PLL_BIAS(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_PLL_BIAS)
#define   DKL_PLL_BIAS_FRAC_EN_H			(1 << 30)
#define   DKL_PLL_BIAS_FBDIV_SHIFT			(8)
#define   DKL_PLL_BIAS_FBDIV_FRAC(x)			((x) << DKL_PLL_BIAS_FBDIV_SHIFT)
#define   DKL_PLL_BIAS_FBDIV_FRAC_MASK			(0x3FFFFF << DKL_PLL_BIAS_FBDIV_SHIFT)

#define _DKL_PLL_TDC_COLDST_BIAS			0x218
#define DKL_PLL_TDC_COLDST_BIAS(tc_port)		_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_PLL_TDC_COLDST_BIAS)
#define   DKL_PLL_TDC_SSC_STEP_SIZE(x)			((x) << 8)
#define   DKL_PLL_TDC_SSC_STEP_SIZE_MASK		(0xFF << 8)
#define   DKL_PLL_TDC_FEED_FWD_GAIN(x)			((x) << 0)
#define   DKL_PLL_TDC_FEED_FWD_GAIN_MASK		(0xFF << 0)

#define _DKL_REFCLKIN_CTL				0x12C
#define DKL_REFCLKIN_CTL(tc_port)			_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_REFCLKIN_CTL)
/* Bits are the same as MG_REFCLKIN_CTL */

#define _DKL_CLKTOP2_HSCLKCTL				0xD4
#define DKL_CLKTOP2_HSCLKCTL(tc_port)			_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_CLKTOP2_HSCLKCTL)
/* Bits are the same as MG_CLKTOP2_HSCLKCTL */

#define _DKL_CLKTOP2_CORECLKCTL1			0xD8
#define DKL_CLKTOP2_CORECLKCTL1(tc_port)		_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_CLKTOP2_CORECLKCTL1)
/* Bits are the same as MG_CLKTOP2_CORECLKCTL1 */

#define _DKL_TX_DPCNTL0					0x2C0
#define DKL_TX_DPCNTL0(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_TX_DPCNTL0)
#define  DKL_TX_PRESHOOT_COEFF(x)			((x) << 13)
#define  DKL_TX_PRESHOOT_COEFF_MASK			(0x1f << 13)
#define  DKL_TX_DE_EMPHASIS_COEFF(x)			((x) << 8)
#define  DKL_TX_DE_EMPAHSIS_COEFF_MASK			(0x1f << 8)
#define  DKL_TX_VSWING_CONTROL(x)			((x) << 0)
#define  DKL_TX_VSWING_CONTROL_MASK			(0x7 << 0)

#define _DKL_TX_DPCNTL1					0x2C4
#define DKL_TX_DPCNTL1(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_TX_DPCNTL1)
/* Bits are the same as DKL_TX_DPCNTRL0 */

#define _DKL_TX_DPCNTL2					0x2C8
#define DKL_TX_DPCNTL2(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_TX_DPCNTL2)
#define  DKL_TX_DP20BITMODE				REG_BIT(2)
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1_MASK	REG_GENMASK(4, 3)
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1(val)	REG_FIELD_PREP(DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1_MASK, (val))
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2_MASK	REG_GENMASK(6, 5)
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2(val)	REG_FIELD_PREP(DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2_MASK, (val))

#define _DKL_TX_FW_CALIB				0x2F8
#define DKL_TX_FW_CALIB(tc_port)			_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_TX_FW_CALIB)
#define  DKL_TX_CFG_DISABLE_WAIT_INIT			(1 << 7)

#define _DKL_TX_PMD_LANE_SUS				0xD00
#define DKL_TX_PMD_LANE_SUS(tc_port)			_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_TX_PMD_LANE_SUS)

#define _DKL_TX_DW17					0xDC4
#define DKL_TX_DW17(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_TX_DW17)

#define _DKL_TX_DW18					0xDC8
#define DKL_TX_DW18(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_TX_DW18)

#define _DKL_DP_MODE					0xA0
#define DKL_DP_MODE(tc_port)				_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_DP_MODE)

#define _DKL_CMN_UC_DW27				0x36C
#define DKL_CMN_UC_DW_27(tc_port)			_MMIO(_PORT(tc_port, \
								    _DKL_PHY1_BASE, \
								    _DKL_PHY2_BASE) + \
							      _DKL_CMN_UC_DW27)
#define  DKL_CMN_UC_DW27_UC_HEALTH			(0x1 << 15)

/*
 * Each Dekel PHY is addressed through a 4KB aperture. Each PHY has more than
 * 4KB of register space, so a separate index is programmed in HIP_INDEX_REG0
 * or HIP_INDEX_REG1, based on the port number, to set the upper 2 address
 * bits that point the 4KB window into the full PHY register space.
 */
#define _HIP_INDEX_REG0					0x1010A0
#define _HIP_INDEX_REG1					0x1010A4
#define HIP_INDEX_REG(tc_port)				_MMIO((tc_port) < 4 ? _HIP_INDEX_REG0 \
							      : _HIP_INDEX_REG1)
#define _HIP_INDEX_SHIFT(tc_port)			(8 * ((tc_port) % 4))
#define HIP_INDEX_VAL(tc_port, val)			((val) << _HIP_INDEX_SHIFT(tc_port))

#endif /* __INTEL_DKL_PHY_REGS__ */
