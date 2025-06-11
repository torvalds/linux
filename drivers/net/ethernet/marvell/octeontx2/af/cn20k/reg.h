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
#define RVU_MBOX_AF_AFPFX_TRIGX(a)		(0x9000 | (a) << 3)
#define RVU_MBOX_AF_PFAF_INT(a)			(0x2980 | (a) << 6)
#define RVU_MBOX_AF_PFAF_INT_W1S(a)		(0x2988 | (a) << 6)
#define RVU_MBOX_AF_PFAF_INT_ENA_W1S(a)		(0x2990 | (a) << 6)
#define RVU_MBOX_AF_PFAF_INT_ENA_W1C(a)		(0x2998 | (a) << 6)
#define RVU_MBOX_AF_PFAF1_INT(a)		(0x29A0 | (a) << 6)
#define RVU_MBOX_AF_PFAF1_INT_W1S(a)		(0x29A8 | (a) << 6)
#define RVU_MBOX_AF_PFAF1_INT_ENA_W1S(a)	(0x29B0 | (a) << 6)
#define RVU_MBOX_AF_PFAF1_INT_ENA_W1C(a)	(0x29B8 | (a) << 6)

/* RVU PF => AF mbox registers */
#define RVU_MBOX_PF_PFAF_TRIGX(a)		(0xC00 | (a) << 3)
#define RVU_MBOX_PF_INT				(0xC20)
#define RVU_MBOX_PF_INT_W1S			(0xC28)
#define RVU_MBOX_PF_INT_ENA_W1S			(0xC30)
#define RVU_MBOX_PF_INT_ENA_W1C			(0xC38)

#define RVU_AF_BAR2_SEL				(0x9000000)
#define RVU_AF_BAR2_PFID			(0x16400)
#define NIX_CINTX_INT_W1S(a)			(0xd30 | (a) << 12)
#define NIX_QINTX_CNT(a)			(0xc00 | (a) << 12)

#endif /* RVU_MBOX_REG_H */
