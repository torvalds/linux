/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef RVU_MBOX_REG_H
#define RVU_MBOX_REG_H
#include "../rvu.h"
#include "../rvu_reg.h"

/* RVUM block registers */
#define RVU_PF_DISC				(0x0)
#define RVU_PRIV_PFX_DISC(a)			(0x8000208 | (a) << 16)
#define RVU_PRIV_HWVFX_DISC(a)			(0xD000000 | (a) << 12)

/* Mbox Registers */
/* RVU AF BAR0 Mbox registers for AF => PFx */
#define RVU_MBOX_AF_PFX_ADDR(a)			(0x5000 | (a) << 4)
#define RVU_MBOX_AF_PFX_CFG(a)			(0x6000 | (a) << 4)
#define RVU_AF_BAR2_SEL				(0x9000000)
#define RVU_AF_BAR2_PFID			(0x16400)
#define NIX_CINTX_INT_W1S(a)			(0xd30 | (a) << 12)
#define NIX_QINTX_CNT(a)			(0xc00 | (a) << 12)

#endif /* RVU_MBOX_REG_H */
