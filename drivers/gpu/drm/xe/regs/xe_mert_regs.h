/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_MERT_REGS_H_
#define _XE_MERT_REGS_H_

#include "regs/xe_reg_defs.h"

#define MERT_LMEM_CFG				XE_REG(0x1448b0)

#define MERT_TLB_CT_INTR_ERR_ID_PORT		XE_REG(0x145190)
#define   CATERR_VFID				REG_GENMASK(16, 9)
#define   CATERR_CODES				REG_GENMASK(5, 0)
#define     CATERR_NO_ERROR			0x00
#define     CATERR_UNMAPPED_GGTT		0x01
#define     CATERR_LMTT_FAULT			0x05

#define MERT_TLB_INV_DESC_A			XE_REG(0x14cf7c)
#define   MERT_TLB_INV_DESC_A_VALID		REG_BIT(0)

#endif
