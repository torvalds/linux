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
#endif
