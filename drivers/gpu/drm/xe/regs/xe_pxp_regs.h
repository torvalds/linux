/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024, Intel Corporation. All rights reserved.
 */

#ifndef __XE_PXP_REGS_H__
#define __XE_PXP_REGS_H__

#include "regs/xe_regs.h"

/* The following registers are only valid on platforms with a media GT */

/* KCR enable/disable control */
#define KCR_INIT				XE_REG(0x3860f0)
#define   KCR_INIT_ALLOW_DISPLAY_ME_WRITES	REG_BIT(14)

/* KCR hwdrm session in play status 0-31 */
#define KCR_SIP					XE_REG(0x386260)

/* PXP global terminate register for session termination */
#define KCR_GLOBAL_TERMINATE			XE_REG(0x3860f8)

#endif /* __XE_PXP_REGS_H__ */
