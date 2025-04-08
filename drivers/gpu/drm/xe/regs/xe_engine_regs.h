/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_ENGINE_REGS_H_
#define _XE_ENGINE_REGS_H_

#include <asm/page.h>

#include "regs/xe_reg_defs.h"

/*
 * These *_BASE values represent the MMIO offset where each hardware engine's
 * registers start.  The other definitions in this header are parameterized
 * macros that will take one of these values as a parameter.
 */
#define RENDER_RING_BASE			0x02000
#define BSD_RING_BASE				0x1c0000
#define BSD2_RING_BASE				0x1c4000
#define BSD3_RING_BASE				0x1d0000
#define BSD4_RING_BASE				0x1d4000
#define XEHP_BSD5_RING_BASE			0x1e0000
#define XEHP_BSD6_RING_BASE			0x1e4000
#define XEHP_BSD7_RING_BASE			0x1f0000
#define XEHP_BSD8_RING_BASE			0x1f4000
#define VEBOX_RING_BASE				0x1c8000
#define VEBOX2_RING_BASE			0x1d8000
#define XEHP_VEBOX3_RING_BASE			0x1e8000
#define XEHP_VEBOX4_RING_BASE			0x1f8000
#define COMPUTE0_RING_BASE			0x1a000
#define COMPUTE1_RING_BASE			0x1c000
#define COMPUTE2_RING_BASE			0x1e000
#define COMPUTE3_RING_BASE			0x26000
#define BLT_RING_BASE				0x22000
#define XEHPC_BCS1_RING_BASE			0x3e0000
#define XEHPC_BCS2_RING_BASE			0x3e2000
#define XEHPC_BCS3_RING_BASE			0x3e4000
#define XEHPC_BCS4_RING_BASE			0x3e6000
#define XEHPC_BCS5_RING_BASE			0x3e8000
#define XEHPC_BCS6_RING_BASE			0x3ea000
#define XEHPC_BCS7_RING_BASE			0x3ec000
#define XEHPC_BCS8_RING_BASE			0x3ee000
#define GSCCS_RING_BASE				0x11a000

#define RING_TAIL(base)				XE_REG((base) + 0x30)
#define   TAIL_ADDR				REG_GENMASK(20, 3)

#define RING_HEAD(base)				XE_REG((base) + 0x34)
#define   HEAD_ADDR				REG_GENMASK(20, 2)

#define RING_START(base)			XE_REG((base) + 0x38)

#define RING_CTL(base)				XE_REG((base) + 0x3c)
#define   RING_CTL_SIZE(size)			((size) - PAGE_SIZE) /* in bytes -> pages */

#define RING_START_UDW(base)			XE_REG((base) + 0x48)

#define RING_PSMI_CTL(base)			XE_REG((base) + 0x50, XE_REG_OPTION_MASKED)
#define   RC_SEMA_IDLE_MSG_DISABLE		REG_BIT(12)
#define   WAIT_FOR_EVENT_POWER_DOWN_DISABLE	REG_BIT(7)
#define   IDLE_MSG_DISABLE			REG_BIT(0)

#define RING_PWRCTX_MAXCNT(base)		XE_REG((base) + 0x54)
#define   IDLE_WAIT_TIME			REG_GENMASK(19, 0)

#define RING_ACTHD_UDW(base)			XE_REG((base) + 0x5c)
#define RING_DMA_FADD_UDW(base)			XE_REG((base) + 0x60)
#define RING_IPEHR(base)			XE_REG((base) + 0x68)
#define RING_INSTDONE(base)			XE_REG((base) + 0x6c)
#define RING_ACTHD(base)			XE_REG((base) + 0x74)
#define RING_DMA_FADD(base)			XE_REG((base) + 0x78)
#define RING_HWS_PGA(base)			XE_REG((base) + 0x80)
#define RING_HWSTAM(base)			XE_REG((base) + 0x98)
#define RING_MI_MODE(base)			XE_REG((base) + 0x9c)
#define RING_NOPID(base)			XE_REG((base) + 0x94)

#define FF_THREAD_MODE(base)			XE_REG((base) + 0xa0)
#define   FF_TESSELATION_DOP_GATE_DISABLE	BIT(19)

#define RING_INT_SRC_RPT_PTR(base)		XE_REG((base) + 0xa4)
#define RING_IMR(base)				XE_REG((base) + 0xa8)
#define RING_INT_STATUS_RPT_PTR(base)		XE_REG((base) + 0xac)

#define CS_INT_VEC(base)			XE_REG((base) + 0x1b8)

#define RING_EIR(base)				XE_REG((base) + 0xb0)
#define RING_EMR(base)				XE_REG((base) + 0xb4)
#define RING_ESR(base)				XE_REG((base) + 0xb8)

#define INSTPM(base)				XE_REG((base) + 0xc0, XE_REG_OPTION_MASKED)
#define   ENABLE_SEMAPHORE_POLL_BIT		REG_BIT(13)

#define RING_CMD_CCTL(base)			XE_REG((base) + 0xc4, XE_REG_OPTION_MASKED)
/*
 * CMD_CCTL read/write fields take a MOCS value and _not_ a table index.
 * The lsb of each can be considered a separate enabling bit for encryption.
 * 6:0 == default MOCS value for reads  =>  6:1 == table index for reads.
 * 13:7 == default MOCS value for writes => 13:8 == table index for writes.
 * 15:14 == Reserved => 31:30 are set to 0.
 */
#define   CMD_CCTL_WRITE_OVERRIDE_MASK		REG_GENMASK(13, 8)
#define   CMD_CCTL_READ_OVERRIDE_MASK		REG_GENMASK(6, 1)

#define CSFE_CHICKEN1(base)			XE_REG((base) + 0xd4, XE_REG_OPTION_MASKED)
#define   GHWSP_CSB_REPORT_DIS			REG_BIT(15)
#define   PPHWSP_CSB_AND_TIMESTAMP_REPORT_DIS	REG_BIT(14)
#define   CS_PRIORITY_MEM_READ			REG_BIT(7)

#define FF_SLICE_CS_CHICKEN1(base)		XE_REG((base) + 0xe0, XE_REG_OPTION_MASKED)
#define   FFSC_PERCTX_PREEMPT_CTRL		REG_BIT(14)

#define CS_DEBUG_MODE1(base)			XE_REG((base) + 0xec, XE_REG_OPTION_MASKED)
#define   FF_DOP_CLOCK_GATE_DISABLE		REG_BIT(1)
#define   REPLAY_MODE_GRANULARITY		REG_BIT(0)

#define INDIRECT_RING_STATE(base)		XE_REG((base) + 0x108)

#define RING_BBADDR(base)			XE_REG((base) + 0x140)
#define RING_BBADDR_UDW(base)			XE_REG((base) + 0x168)

#define BCS_SWCTRL(base)			XE_REG((base) + 0x200, XE_REG_OPTION_MASKED)
#define   BCS_SWCTRL_DISABLE_256B		REG_BIT(2)

/* Handling MOCS value in BLIT_CCTL like it was done CMD_CCTL */
#define BLIT_CCTL(base)				XE_REG((base) + 0x204)
#define   BLIT_CCTL_DST_MOCS_MASK		REG_GENMASK(14, 9)
#define   BLIT_CCTL_SRC_MOCS_MASK		REG_GENMASK(6, 1)

#define RING_EXECLIST_STATUS_LO(base)		XE_REG((base) + 0x234)
#define RING_EXECLIST_STATUS_HI(base)		XE_REG((base) + 0x234 + 4)

#define RING_IDLEDLY(base)			XE_REG((base) + 0x23c)
#define   INHIBIT_SWITCH_UNTIL_PREEMPTED	REG_BIT(31)
#define   IDLE_DELAY				REG_GENMASK(20, 0)

#define RING_CONTEXT_CONTROL(base)		XE_REG((base) + 0x244, XE_REG_OPTION_MASKED)
#define	  CTX_CTRL_PXP_ENABLE			REG_BIT(10)
#define	  CTX_CTRL_OAC_CONTEXT_ENABLE		REG_BIT(8)
#define	  CTX_CTRL_RUN_ALONE			REG_BIT(7)
#define	  CTX_CTRL_INDIRECT_RING_STATE_ENABLE	REG_BIT(4)
#define	  CTX_CTRL_INHIBIT_SYN_CTX_SWITCH	REG_BIT(3)
#define	  CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT	REG_BIT(0)

#define RING_MODE(base)				XE_REG((base) + 0x29c)
#define   GFX_DISABLE_LEGACY_MODE		REG_BIT(3)
#define   GFX_MSIX_INTERRUPT_ENABLE		REG_BIT(13)

#define RING_TIMESTAMP(base)			XE_REG((base) + 0x358)

#define RING_TIMESTAMP_UDW(base)		XE_REG((base) + 0x358 + 4)
#define   RING_VALID_MASK			0x00000001
#define   RING_VALID				0x00000001
#define   STOP_RING				REG_BIT(8)

#define RING_CTX_TIMESTAMP(base)		XE_REG((base) + 0x3a8)
#define CSBE_DEBUG_STATUS(base)			XE_REG((base) + 0x3fc)

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

#define CS_CHICKEN1(base)			XE_REG((base) + 0x580, XE_REG_OPTION_MASKED)
#define   PREEMPT_GPGPU_LEVEL(hi, lo)		(((hi) << 2) | ((lo) << 1))
#define   PREEMPT_GPGPU_MID_THREAD_LEVEL	PREEMPT_GPGPU_LEVEL(0, 0)
#define   PREEMPT_GPGPU_THREAD_GROUP_LEVEL	PREEMPT_GPGPU_LEVEL(0, 1)
#define   PREEMPT_GPGPU_COMMAND_LEVEL		PREEMPT_GPGPU_LEVEL(1, 0)
#define   PREEMPT_GPGPU_LEVEL_MASK		PREEMPT_GPGPU_LEVEL(1, 1)
#define   PREEMPT_3D_OBJECT_LEVEL		REG_BIT(0)

#define VDBOX_CGCTL3F08(base)			XE_REG((base) + 0x3f08)
#define   CG3DDISHRS_CLKGATE_DIS		REG_BIT(5)

#define VDBOX_CGCTL3F10(base)			XE_REG((base) + 0x3f10)
#define   IECPUNIT_CLKGATE_DIS			REG_BIT(22)
#define   RAMDFTUNIT_CLKGATE_DIS		REG_BIT(9)

#define VDBOX_CGCTL3F18(base)			XE_REG((base) + 0x3f18)
#define   ALNUNIT_CLKGATE_DIS			REG_BIT(13)

#define VDBOX_CGCTL3F1C(base)			XE_REG((base) + 0x3f1c)
#define   MFXPIPE_CLKGATE_DIS			REG_BIT(3)

#endif
