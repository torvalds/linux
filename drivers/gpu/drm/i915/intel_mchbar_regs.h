/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_MCHBAR_REGS__
#define __INTEL_MCHBAR_REGS__

#include "i915_reg_defs.h"

/*
 * MCHBAR mirror.
 *
 * This mirrors the MCHBAR MMIO space whose location is determined by
 * device 0 function 0's pci config register 0x44 or 0x48 and matches it in
 * every way.  It is not accessible from the CP register read instructions.
 *
 * Starting from Haswell, you can't write registers using the MCHBAR mirror,
 * just read.
 */

#define MCHBAR_MIRROR_BASE			0x10000
#define MCHBAR_MIRROR_BASE_SNB			0x140000

#define CTG_STOLEN_RESERVED			_MMIO(MCHBAR_MIRROR_BASE + 0x34)
#define ELK_STOLEN_RESERVED			_MMIO(MCHBAR_MIRROR_BASE + 0x48)
#define   G4X_STOLEN_RESERVED_ADDR1_MASK	(0xFFFF << 16)
#define   G4X_STOLEN_RESERVED_ADDR2_MASK	(0xFFF << 4)
#define   G4X_STOLEN_RESERVED_ENABLE		(1 << 0)

/* Pineview MCH register contains DDR3 setting */
#define CSHRDDR3CTL				_MMIO(MCHBAR_MIRROR_BASE + 0x1a8)
#define   CSHRDDR3CTL_DDR3			(1 << 2)

/* 915-945 and GM965 MCH register controlling DRAM channel access */
#define DCC					_MMIO(MCHBAR_MIRROR_BASE + 0x200)
#define   DCC_ADDRESSING_MODE_SINGLE_CHANNEL	(0 << 0)
#define   DCC_ADDRESSING_MODE_DUAL_CHANNEL_ASYMMETRIC	(1 << 0)
#define   DCC_ADDRESSING_MODE_DUAL_CHANNEL_INTERLEAVED	(2 << 0)
#define   DCC_ADDRESSING_MODE_MASK		(3 << 0)
#define   DCC_CHANNEL_XOR_DISABLE		(1 << 10)
#define   DCC_CHANNEL_XOR_BIT_17		(1 << 9)
#define DCC2					_MMIO(MCHBAR_MIRROR_BASE + 0x204)
#define   DCC2_MODIFIED_ENHANCED_DISABLE	(1 << 20)

/* 965 MCH register controlling DRAM channel configuration */
#define C0DRB3_BW				_MMIO(MCHBAR_MIRROR_BASE + 0x206)
#define C1DRB3_BW				_MMIO(MCHBAR_MIRROR_BASE + 0x606)

/* Clocking configuration register */
#define CLKCFG					_MMIO(MCHBAR_MIRROR_BASE + 0xc00)
#define CLKCFG_FSB_400				(0 << 0)	/* hrawclk 100 */
#define CLKCFG_FSB_400_ALT			(5 << 0)	/* hrawclk 100 */
#define CLKCFG_FSB_533				(1 << 0)	/* hrawclk 133 */
#define CLKCFG_FSB_667				(3 << 0)	/* hrawclk 166 */
#define CLKCFG_FSB_800				(2 << 0)	/* hrawclk 200 */
#define CLKCFG_FSB_1067				(6 << 0)	/* hrawclk 266 */
#define CLKCFG_FSB_1067_ALT			(0 << 0)	/* hrawclk 266 */
#define CLKCFG_FSB_1333				(7 << 0)	/* hrawclk 333 */
#define CLKCFG_FSB_1333_ALT			(4 << 0)	/* hrawclk 333 */
#define CLKCFG_FSB_1600_ALT			(6 << 0)	/* hrawclk 400 */
#define CLKCFG_FSB_MASK				(7 << 0)
#define CLKCFG_MEM_533				(1 << 4)
#define CLKCFG_MEM_667				(2 << 4)
#define CLKCFG_MEM_800				(3 << 4)
#define CLKCFG_MEM_MASK				(7 << 4)

#define HPLLVCO_MOBILE				_MMIO(MCHBAR_MIRROR_BASE + 0xc0f)
#define HPLLVCO					_MMIO(MCHBAR_MIRROR_BASE + 0xc38)

#define TSC1					_MMIO(MCHBAR_MIRROR_BASE + 0x1001)
#define   TSE					(1 << 0)
#define TR1					_MMIO(MCHBAR_MIRROR_BASE + 0x1006)
#define TSFS					_MMIO(MCHBAR_MIRROR_BASE + 0x1020)
#define   TSFS_SLOPE_MASK			0x0000ff00
#define   TSFS_SLOPE_SHIFT			8
#define   TSFS_INTR_MASK			0x000000ff

/* Memory latency timer register */
#define MLTR_ILK				_MMIO(MCHBAR_MIRROR_BASE + 0x1222)
/* the unit of memory self-refresh latency time is 0.5us */
#define   MLTR_WM2_MASK				REG_GENMASK(13, 8)
#define   MLTR_WM1_MASK				REG_GENMASK(5, 0)

#define CSIPLL0					_MMIO(MCHBAR_MIRROR_BASE + 0x2c10)
#define DDRMPLL1				_MMIO(MCHBAR_MIRROR_BASE + 0x2c20)

#define ILK_GDSR				_MMIO(MCHBAR_MIRROR_BASE + 0x2ca4)
#define  ILK_GRDOM_FULL				(0 << 1)
#define  ILK_GRDOM_RENDER			(1 << 1)
#define  ILK_GRDOM_MEDIA			(3 << 1)
#define  ILK_GRDOM_MASK				(3 << 1)
#define  ILK_GRDOM_RESET_ENABLE			(1 << 0)

#define BXT_D_CR_DRP0_DUNIT8			0x1000
#define BXT_D_CR_DRP0_DUNIT9			0x1200
#define   BXT_D_CR_DRP0_DUNIT_START		8
#define   BXT_D_CR_DRP0_DUNIT_END		11
#define BXT_D_CR_DRP0_DUNIT(x)			_MMIO(MCHBAR_MIRROR_BASE_SNB + \
						      _PICK_EVEN((x) - 8, BXT_D_CR_DRP0_DUNIT8,\
								 BXT_D_CR_DRP0_DUNIT9))
#define   BXT_DRAM_RANK_MASK			0x3
#define   BXT_DRAM_RANK_SINGLE			0x1
#define   BXT_DRAM_RANK_DUAL			0x3
#define   BXT_DRAM_WIDTH_MASK			(0x3 << 4)
#define   BXT_DRAM_WIDTH_SHIFT			4
#define   BXT_DRAM_WIDTH_X8			(0x0 << 4)
#define   BXT_DRAM_WIDTH_X16			(0x1 << 4)
#define   BXT_DRAM_WIDTH_X32			(0x2 << 4)
#define   BXT_DRAM_WIDTH_X64			(0x3 << 4)
#define   BXT_DRAM_SIZE_MASK			(0x7 << 6)
#define   BXT_DRAM_SIZE_SHIFT			6
#define   BXT_DRAM_SIZE_4GBIT			(0x0 << 6)
#define   BXT_DRAM_SIZE_6GBIT			(0x1 << 6)
#define   BXT_DRAM_SIZE_8GBIT			(0x2 << 6)
#define   BXT_DRAM_SIZE_12GBIT			(0x3 << 6)
#define   BXT_DRAM_SIZE_16GBIT			(0x4 << 6)
#define   BXT_DRAM_TYPE_MASK			(0x7 << 22)
#define   BXT_DRAM_TYPE_SHIFT			22
#define   BXT_DRAM_TYPE_DDR3			(0x0 << 22)
#define   BXT_DRAM_TYPE_LPDDR3			(0x1 << 22)
#define   BXT_DRAM_TYPE_LPDDR4			(0x2 << 22)
#define   BXT_DRAM_TYPE_DDR4			(0x4 << 22)

#define MCHBAR_CH0_CR_TC_PRE_0_0_0_MCHBAR	_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x4000)
#define   DG1_DRAM_T_RDPRE_MASK			REG_GENMASK(16, 11)
#define   DG1_DRAM_T_RP_MASK			REG_GENMASK(6, 0)
#define MCHBAR_CH0_CR_TC_PRE_0_0_0_MCHBAR_HIGH	_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x4004)
#define   DG1_DRAM_T_RCD_MASK			REG_GENMASK(15, 9)
#define   DG1_DRAM_T_RAS_MASK			REG_GENMASK(8, 1)

#define SKL_MAD_INTER_CHANNEL_0_0_0_MCHBAR_MCMAIN	_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5000)
#define   SKL_DRAM_DDR_TYPE_MASK		(0x3 << 0)
#define   SKL_DRAM_DDR_TYPE_DDR4		(0 << 0)
#define   SKL_DRAM_DDR_TYPE_DDR3		(1 << 0)
#define   SKL_DRAM_DDR_TYPE_LPDDR3		(2 << 0)
#define   SKL_DRAM_DDR_TYPE_LPDDR4		(3 << 0)

/* snb MCH registers for reading the DRAM channel configuration */
#define MAD_DIMM_C0				_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5004)
#define MAD_DIMM_C1				_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5008)
#define MAD_DIMM_C2				_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x500C)
#define   MAD_DIMM_ECC_MASK			(0x3 << 24)
#define   MAD_DIMM_ECC_OFF			(0x0 << 24)
#define   MAD_DIMM_ECC_IO_ON_LOGIC_OFF		(0x1 << 24)
#define   MAD_DIMM_ECC_IO_OFF_LOGIC_ON		(0x2 << 24)
#define   MAD_DIMM_ECC_ON			(0x3 << 24)
#define   MAD_DIMM_ENH_INTERLEAVE		(0x1 << 22)
#define   MAD_DIMM_RANK_INTERLEAVE		(0x1 << 21)
#define   MAD_DIMM_B_WIDTH_X16			(0x1 << 20) /* X8 chips if unset */
#define   MAD_DIMM_A_WIDTH_X16			(0x1 << 19) /* X8 chips if unset */
#define   MAD_DIMM_B_DUAL_RANK			(0x1 << 18)
#define   MAD_DIMM_A_DUAL_RANK			(0x1 << 17)
#define   MAD_DIMM_A_SELECT			(0x1 << 16)
/* DIMM sizes are in multiples of 256mb. */
#define   MAD_DIMM_B_SIZE_SHIFT			8
#define   MAD_DIMM_B_SIZE_MASK			(0xff << MAD_DIMM_B_SIZE_SHIFT)
#define   MAD_DIMM_A_SIZE_SHIFT			0
#define   MAD_DIMM_A_SIZE_MASK			(0xff << MAD_DIMM_A_SIZE_SHIFT)

#define SKL_MAD_DIMM_CH0_0_0_0_MCHBAR_MCMAIN	_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x500C)
#define SKL_MAD_DIMM_CH1_0_0_0_MCHBAR_MCMAIN	_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5010)
#define   SKL_DRAM_S_SHIFT			16
#define   SKL_DRAM_SIZE_MASK			0x3F
#define   SKL_DRAM_WIDTH_MASK			(0x3 << 8)
#define   SKL_DRAM_WIDTH_SHIFT			8
#define   SKL_DRAM_WIDTH_X8			(0x0 << 8)
#define   SKL_DRAM_WIDTH_X16			(0x1 << 8)
#define   SKL_DRAM_WIDTH_X32			(0x2 << 8)
#define   SKL_DRAM_RANK_MASK			(0x1 << 10)
#define   SKL_DRAM_RANK_SHIFT			10
#define   SKL_DRAM_RANK_1			(0x0 << 10)
#define   SKL_DRAM_RANK_2			(0x1 << 10)
#define   SKL_DRAM_RANK_MASK			(0x1 << 10)
#define   ICL_DRAM_SIZE_MASK			0x7F
#define   ICL_DRAM_WIDTH_MASK			(0x3 << 7)
#define   ICL_DRAM_WIDTH_SHIFT			7
#define   ICL_DRAM_WIDTH_X8			(0x0 << 7)
#define   ICL_DRAM_WIDTH_X16			(0x1 << 7)
#define   ICL_DRAM_WIDTH_X32			(0x2 << 7)
#define   ICL_DRAM_RANK_MASK			(0x3 << 9)
#define   ICL_DRAM_RANK_SHIFT			9
#define   ICL_DRAM_RANK_1			(0x0 << 9)
#define   ICL_DRAM_RANK_2			(0x1 << 9)
#define   ICL_DRAM_RANK_3			(0x2 << 9)
#define   ICL_DRAM_RANK_4			(0x3 << 9)

#define SA_PERF_STATUS_0_0_0_MCHBAR_PC		_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5918)
#define  DG1_QCLK_RATIO_MASK			REG_GENMASK(9, 2)
#define  DG1_QCLK_REFERENCE			REG_BIT(10)

#define GEN6_GT_PERF_STATUS			_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5948)
#define GEN6_RP_STATE_LIMITS			_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5994)
#define GEN6_RP_STATE_CAP			_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5998)
#define   RP0_CAP_MASK				REG_GENMASK(7, 0)
#define   RP1_CAP_MASK				REG_GENMASK(15, 8)
#define   RPN_CAP_MASK				REG_GENMASK(23, 16)

#define GEN10_FREQ_INFO_REC			_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5ef0)
#define   RPE_MASK				REG_GENMASK(15, 8)

/* snb MCH registers for priority tuning */
#define MCH_SSKPD				_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5d10)
#define   SSKPD_NEW_WM0_MASK_HSW		REG_GENMASK64(63, 56)
#define   SSKPD_WM4_MASK_HSW			REG_GENMASK64(40, 32)
#define   SSKPD_WM3_MASK_HSW			REG_GENMASK64(28, 20)
#define   SSKPD_WM2_MASK_HSW			REG_GENMASK64(19, 12)
#define   SSKPD_WM1_MASK_HSW			REG_GENMASK64(11, 4)
#define   SSKPD_OLD_WM0_MASK_HSW		REG_GENMASK64(3, 0)
#define   SSKPD_WM3_MASK_SNB			REG_GENMASK(29, 24)
#define   SSKPD_WM2_MASK_SNB			REG_GENMASK(21, 16)
#define   SSKPD_WM1_MASK_SNB			REG_GENMASK(13, 8)
#define   SSKPD_WM0_MASK_SNB			REG_GENMASK(5, 0)

/* Memory controller frequency in MCHBAR for Haswell (possible SNB+) */
#define DCLK					_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5e04)
#define SKL_MC_BIOS_DATA_0_0_0_MCHBAR_PCU	_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5e04)
#define   DG1_GEAR_TYPE				REG_BIT(16)

/*
 * Please see hsw_read_dcomp() and hsw_write_dcomp() before using this register,
 * since on HSW we can't write to it using intel_uncore_write.
 */
#define D_COMP_HSW				_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x5f0c)
#define  D_COMP_RCOMP_IN_PROGRESS		(1 << 9)
#define  D_COMP_COMP_FORCE			(1 << 8)
#define  D_COMP_COMP_DISABLE			(1 << 0)

#define BXT_GT_PERF_STATUS			_MMIO(MCHBAR_MIRROR_BASE_SNB + 0x7070)

#endif /* __INTEL_MCHBAR_REGS */
