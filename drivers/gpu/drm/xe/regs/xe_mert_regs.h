/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_MERT_REGS_H_
#define _XE_MERT_REGS_H_

#include "regs/xe_reg_defs.h"

#define MERT_LMEM_CFG				XE_REG(0x1448b0)

#define MERT_TLB_INV_DESC_A			XE_REG(0x14cf7c)
#define   MERT_TLB_INV_DESC_A_VALID		REG_BIT(0)

#endif /* _XE_MERT_REGS_H_ */
