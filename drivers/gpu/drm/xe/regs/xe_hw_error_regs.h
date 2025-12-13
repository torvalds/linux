/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_HW_ERROR_REGS_H_
#define _XE_HW_ERROR_REGS_H_

#define HEC_UNCORR_ERR_STATUS(base)                    XE_REG((base) + 0x118)
#define    UNCORR_FW_REPORTED_ERR                      BIT(6)

#define HEC_UNCORR_FW_ERR_DW0(base)                    XE_REG((base) + 0x124)

#define DEV_ERR_STAT_NONFATAL			0x100178
#define DEV_ERR_STAT_CORRECTABLE		0x10017c
#define DEV_ERR_STAT_REG(x)			XE_REG(_PICK_EVEN((x), \
								  DEV_ERR_STAT_CORRECTABLE, \
								  DEV_ERR_STAT_NONFATAL))
#define   XE_CSC_ERROR				BIT(17)
#endif
