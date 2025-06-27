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

#define RVU_MBOX_AF_VFAF_INT(a)			(0x3000 | (a) << 6)
#define RVU_MBOX_AF_VFAF_INT_W1S(a)		(0x3008 | (a) << 6)
#define RVU_MBOX_AF_VFAF_INT_ENA_W1S(a)		(0x3010 | (a) << 6)
#define RVU_MBOX_AF_VFAF_INT_ENA_W1C(a)		(0x3018 | (a) << 6)
#define RVU_MBOX_AF_VFAF_INT_ENA_W1C(a)		(0x3018 | (a) << 6)
#define RVU_MBOX_AF_VFAF1_INT(a)		(0x3020 | (a) << 6)
#define RVU_MBOX_AF_VFAF1_INT_W1S(a)		(0x3028 | (a) << 6)
#define RVU_MBOX_AF_VFAF1_IN_ENA_W1S(a)		(0x3030 | (a) << 6)
#define RVU_MBOX_AF_VFAF1_IN_ENA_W1C(a)		(0x3038 | (a) << 6)

#define RVU_MBOX_AF_AFVFX_TRIG(a, b)		(0x10000 | (a) << 4 | (b) << 3)
#define RVU_MBOX_AF_VFX_ADDR(a)			(0x20000 | (a) << 4)
#define RVU_MBOX_AF_VFX_CFG(a)			(0x28000 | (a) << 4)

#define RVU_MBOX_PF_VFX_PFVF_TRIGX(a)		(0x2000 | (a) << 3)

#define RVU_MBOX_PF_VFPF_INTX(a)		(0x1000 | (a) << 3)
#define RVU_MBOX_PF_VFPF_INT_W1SX(a)		(0x1020 | (a) << 3)
#define RVU_MBOX_PF_VFPF_INT_ENA_W1SX(a)	(0x1040 | (a) << 3)
#define RVU_MBOX_PF_VFPF_INT_ENA_W1CX(a)	(0x1060 | (a) << 3)

#define RVU_MBOX_PF_VFPF1_INTX(a)		(0x1080 | (a) << 3)
#define RVU_MBOX_PF_VFPF1_INT_W1SX(a)		(0x10a0 | (a) << 3)
#define RVU_MBOX_PF_VFPF1_INT_ENA_W1SX(a)	(0x10c0 | (a) << 3)
#define RVU_MBOX_PF_VFPF1_INT_ENA_W1CX(a)	(0x10e0 | (a) << 3)

#define RVU_MBOX_PF_VF_ADDR			(0xC40)
#define RVU_MBOX_PF_LMTLINE_ADDR		(0xC48)
#define RVU_MBOX_PF_VF_CFG			(0xC60)

#define RVU_MBOX_VF_VFPF_TRIGX(a)		(0x3000 | (a) << 3)
#define RVU_MBOX_VF_INT				(0x20)
#define RVU_MBOX_VF_INT_W1S			(0x28)
#define RVU_MBOX_VF_INT_ENA_W1S			(0x30)
#define RVU_MBOX_VF_INT_ENA_W1C			(0x38)

#define RVU_MBOX_VF_VFAF_TRIGX(a)		(0x2000 | (a) << 3)
#endif /* RVU_MBOX_REG_H */
