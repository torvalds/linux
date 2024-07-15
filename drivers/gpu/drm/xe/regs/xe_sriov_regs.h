/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _REGS_XE_SRIOV_REGS_H_
#define _REGS_XE_SRIOV_REGS_H_

#include "regs/xe_reg_defs.h"

#define XE2_LMEM_CFG			XE_REG(0x48b0)

#define LMEM_CFG			XE_REG(0xcf58)
#define   LMEM_EN			REG_BIT(31)
#define   LMTT_DIR_PTR			REG_GENMASK(30, 0) /* in multiples of 64KB */

#define VF_CAP_REG			XE_REG(0x1901f8, XE_REG_OPTION_VF)
#define   VF_CAP			REG_BIT(0)

#endif
