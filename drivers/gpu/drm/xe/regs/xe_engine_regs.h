/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_ENGINE_REGS_H_
#define _XE_ENGINE_REGS_H_

#include <asm/page.h>

#include "regs/xe_reg_defs.h"

#define RING_TAIL(base)				XE_REG((base) + 0x30)

#define RING_HEAD(base)				XE_REG((base) + 0x34)
#define   HEAD_ADDR				0x001FFFFC

#define RING_START(base)			XE_REG((base) + 0x38)

#define RING_CTL(base)				XE_REG((base) + 0x3c)
#define   RING_CTL_SIZE(size)			((size) - PAGE_SIZE) /* in bytes -> pages */
#define   RING_CTL_SIZE(size)			((size) - PAGE_SIZE) /* in bytes -> pages */

#define RING_PSMI_CTL(base)			XE_REG((base) + 0x50)
#define   RC_SEMA_IDLE_MSG_DISABLE		REG_BIT(12)
#define   WAIT_FOR_EVENT_POWER_DOWN_DISABLE	REG_BIT(7)

#define RING_ACTHD_UDW(base)			XE_REG((base) + 0x5c)
#define RING_DMA_FADD_UDW(base)			XE_REG((base) + 0x60)
#define RING_IPEIR(base)			XE_REG((base) + 0x64)
#define RING_IPEHR(base)			XE_REG((base) + 0x68)
#define RING_ACTHD(base)			XE_REG((base) + 0x74)
#define RING_DMA_FADD(base)			XE_REG((base) + 0x78)
#define RING_HWS_PGA(base)			XE_REG((base) + 0x80)
#define IPEIR(base)				XE_REG((base) + 0x88)
#define IPEHR(base)				XE_REG((base) + 0x8c)
#define RING_HWSTAM(base)			XE_REG((base) + 0x98)
#define RING_MI_MODE(base)			XE_REG((base) + 0x9c)
#define RING_NOPID(base)			XE_REG((base) + 0x94)

#define RING_IMR(base)				XE_REG((base) + 0xa8)
#define   RING_MAX_NONPRIV_SLOTS  12

#define RING_EIR(base)				XE_REG((base) + 0xb0)
#define RING_EMR(base)				XE_REG((base) + 0xb4)
#define RING_ESR(base)				XE_REG((base) + 0xb8)
#define RING_BBADDR(base)			XE_REG((base) + 0x140)
#define RING_BBADDR_UDW(base)			XE_REG((base) + 0x168)
#define RING_EXECLIST_STATUS_LO(base)		XE_REG((base) + 0x234)
#define RING_EXECLIST_STATUS_HI(base)		XE_REG((base) + 0x234 + 4)

#define RING_CONTEXT_CONTROL(base)		XE_REG((base) + 0x244)
#define	  CTX_CTRL_INHIBIT_SYN_CTX_SWITCH	REG_BIT(3)
#define	  CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT	REG_BIT(0)

#define RING_MODE(base)				XE_REG((base) + 0x29c)
#define   GFX_DISABLE_LEGACY_MODE		REG_BIT(3)

#define RING_TIMESTAMP(base)			XE_REG((base) + 0x358)

#define RING_TIMESTAMP_UDW(base)		XE_REG((base) + 0x358 + 4)
#define   RING_VALID_MASK			0x00000001
#define   RING_VALID				0x00000001
#define   STOP_RING				REG_BIT(8)
#define   TAIL_ADDR				0x001FFFF8

#define RING_CTX_TIMESTAMP(base)		XE_REG((base) + 0x3a8)

#define RING_FORCE_TO_NONPRIV(base, i)		XE_REG(((base) + 0x4d0) + (i) * 4)
#define   RING_FORCE_TO_NONPRIV_DENY		REG_BIT(30)
#define   RING_FORCE_TO_NONPRIV_ACCESS_MASK	REG_GENMASK(29, 28)
#define   RING_FORCE_TO_NONPRIV_ACCESS_RW	REG_FIELD_PREP(RING_FORCE_TO_NONPRIV_ACCESS_MASK, 0)
#define   RING_FORCE_TO_NONPRIV_ACCESS_RD	REG_FIELD_PREP(RING_FORCE_TO_NONPRIV_ACCESS_MASK, 1)
#define   RING_FORCE_TO_NONPRIV_ACCESS_WR	REG_FIELD_PREP(RING_FORCE_TO_NONPRIV_ACCESS_MASK, 2)
#define   RING_FORCE_TO_NONPRIV_ACCESS_INVALID	REG_FIELD_PREP(RING_FORCE_TO_NONPRIV_ACCESS_MASK, 3)
#define   RING_FORCE_TO_NONPRIV_ADDRESS_MASK	REG_GENMASK(25, 2)
#define   RING_FORCE_TO_NONPRIV_RANGE_MASK	REG_GENMASK(1, 0)
#define   RING_FORCE_TO_NONPRIV_RANGE_1		REG_FIELD_PREP(RING_FORCE_TO_NONPRIV_RANGE_MASK, 0)
#define   RING_FORCE_TO_NONPRIV_RANGE_4		REG_FIELD_PREP(RING_FORCE_TO_NONPRIV_RANGE_MASK, 1)
#define   RING_FORCE_TO_NONPRIV_RANGE_16	REG_FIELD_PREP(RING_FORCE_TO_NONPRIV_RANGE_MASK, 2)
#define   RING_FORCE_TO_NONPRIV_RANGE_64	REG_FIELD_PREP(RING_FORCE_TO_NONPRIV_RANGE_MASK, 3)
#define   RING_FORCE_TO_NONPRIV_MASK_VALID	(RING_FORCE_TO_NONPRIV_RANGE_MASK | \
						 RING_FORCE_TO_NONPRIV_ACCESS_MASK | \
						 RING_FORCE_TO_NONPRIV_DENY)
#define   RING_MAX_NONPRIV_SLOTS  12

#define RING_EXECLIST_SQ_CONTENTS_LO(base)	XE_REG((base) + 0x510)
#define RING_EXECLIST_SQ_CONTENTS_HI(base)	XE_REG((base) + 0x510 + 4)

#define RING_EXECLIST_CONTROL(base)		XE_REG((base) + 0x550)
#define	  EL_CTRL_LOAD				REG_BIT(0)

#define VDBOX_CGCTL3F10(base)			XE_REG((base) + 0x3f10)
#define   IECPUNIT_CLKGATE_DIS			REG_BIT(22)

#define VDBOX_CGCTL3F18(base)			XE_REG((base) + 0x3f18)
#define   ALNUNIT_CLKGATE_DIS			REG_BIT(13)

#endif
