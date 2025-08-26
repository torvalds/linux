/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_HW_ERROR_REGS_H_
#define _XE_HW_ERROR_REGS_H_

#define DEV_ERR_STAT_NONFATAL			0x100178
#define DEV_ERR_STAT_CORRECTABLE		0x10017c
#define DEV_ERR_STAT_REG(x)			XE_REG(_PICK_EVEN((x), \
								  DEV_ERR_STAT_CORRECTABLE, \
								  DEV_ERR_STAT_NONFATAL))

#endif
