/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_HW_ERROR_REGS_H_
#define _XE_HW_ERROR_REGS_H_

#define HEC_UNCORR_ERR_STATUS(base)			XE_REG((base) + 0x118)
#define   UNCORR_FW_REPORTED_ERR			REG_BIT(6)

#define HEC_UNCORR_FW_ERR_DW0(base)			XE_REG((base) + 0x124)

#define ERR_STAT_GT_COR					0x100160
#define   EU_GRF_COR_ERR				REG_BIT(15)
#define   EU_IC_COR_ERR					REG_BIT(14)
#define   SLM_COR_ERR					REG_BIT(13)
#define   GUC_COR_ERR					REG_BIT(1)

#define ERR_STAT_GT_NONFATAL				0x100164
#define ERR_STAT_GT_FATAL				0x100168
#define   EU_GRF_FAT_ERR				REG_BIT(15)
#define   SLM_FAT_ERR					REG_BIT(13)
#define   GUC_FAT_ERR					REG_BIT(6)
#define   FPU_FAT_ERR					REG_BIT(3)

#define ERR_STAT_GT_REG(x)				XE_REG(_PICK_EVEN((x), \
									  ERR_STAT_GT_COR, \
									  ERR_STAT_GT_NONFATAL))

#define PVC_COR_ERR_MASK				(GUC_COR_ERR | SLM_COR_ERR | \
							 EU_IC_COR_ERR | EU_GRF_COR_ERR)

#define PVC_FAT_ERR_MASK				(FPU_FAT_ERR | GUC_FAT_ERR | \
							 EU_GRF_FAT_ERR | SLM_FAT_ERR)

#define DEV_ERR_STAT_NONFATAL				0x100178
#define DEV_ERR_STAT_CORRECTABLE			0x10017c
#define DEV_ERR_STAT_REG(x)				XE_REG(_PICK_EVEN((x), \
									  DEV_ERR_STAT_CORRECTABLE, \
									  DEV_ERR_STAT_NONFATAL))

#define   XE_CSC_ERROR					17
#define   XE_SOC_ERROR					16
#define   XE_GT_ERROR					0

#define ERR_STAT_GT_FATAL_VECTOR_0			0x100260
#define ERR_STAT_GT_FATAL_VECTOR_1			0x100264

#define ERR_STAT_GT_FATAL_VECTOR_REG(x)			XE_REG(_PICK_EVEN((x), \
									  ERR_STAT_GT_FATAL_VECTOR_0, \
									  ERR_STAT_GT_FATAL_VECTOR_1))

#define ERR_STAT_GT_COR_VECTOR_0			0x1002a0
#define ERR_STAT_GT_COR_VECTOR_1			0x1002a4

#define ERR_STAT_GT_COR_VECTOR_REG(x)			XE_REG(_PICK_EVEN((x), \
									  ERR_STAT_GT_COR_VECTOR_0, \
									  ERR_STAT_GT_COR_VECTOR_1))

#define ERR_STAT_GT_VECTOR_REG(hw_err, x)		(hw_err == HARDWARE_ERROR_CORRECTABLE ? \
							 ERR_STAT_GT_COR_VECTOR_REG(x) : \
							 ERR_STAT_GT_FATAL_VECTOR_REG(x))

#define SOC_PVC_MASTER_BASE				0x282000
#define SOC_PVC_SLAVE_BASE				0x283000

#define SOC_GCOERRSTS					0x200
#define SOC_GNFERRSTS					0x210
#define SOC_GLOBAL_ERR_STAT_REG(base, x)		XE_REG(_PICK_EVEN((x), \
									  (base) + SOC_GCOERRSTS, \
									  (base) + SOC_GNFERRSTS))
#define   SOC_SLAVE_IEH					REG_BIT(1)
#define   SOC_IEH0_LOCAL_ERR_STATUS			REG_BIT(0)
#define   SOC_IEH1_LOCAL_ERR_STATUS			REG_BIT(0)

#define SOC_GSYSEVTCTL					0x264
#define SOC_GSYSEVTCTL_REG(master, slave, x)		XE_REG(_PICK_EVEN((x), \
									  (master) + SOC_GSYSEVTCTL, \
									  (slave) + SOC_GSYSEVTCTL))

#define SOC_LERRUNCSTS					0x280
#define SOC_LERRCORSTS					0x294
#define SOC_LOCAL_ERR_STAT_REG(base, hw_err)		XE_REG(hw_err == HARDWARE_ERROR_CORRECTABLE ? \
							       (base) + SOC_LERRCORSTS : \
							       (base) + SOC_LERRUNCSTS)

#endif
