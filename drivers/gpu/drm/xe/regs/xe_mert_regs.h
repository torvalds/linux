/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_MERT_REGS_H_
#define _XE_MERT_REGS_H_

#include "regs/xe_reg_defs.h"

#define MERT_LMEM_CFG				XE_REG(0x1448b0)

#define MERT_TLB_CT_INTR_ERR_ID_PORT		XE_REG(0x145190)
#define   MERT_TLB_CT_VFID_MASK			REG_GENMASK(16, 9)
#define   MERT_TLB_CT_ERROR_MASK		REG_GENMASK(5, 0)
#define     MERT_TLB_CT_LMTT_FAULT		0x05

#define MERT_TLB_INV_DESC_A			XE_REG(0x14cf7c)
#define   MERT_TLB_INV_DESC_A_VALID		REG_BIT(0)

#endif /* _XE_MERT_REGS_H_ */
