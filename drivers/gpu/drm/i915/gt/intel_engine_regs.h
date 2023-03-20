/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_ENGINE_REGS__
#define __INTEL_ENGINE_REGS__

#include "i915_reg_defs.h"

#define RING_EXCC(base)				_MMIO((base) + 0x28)
#define RING_TAIL(base)				_MMIO((base) + 0x30)
#define   TAIL_ADDR				0x001FFFF8
#define RING_HEAD(base)				_MMIO((base) + 0x34)
#define   HEAD_WRAP_COUNT			0xFFE00000
#define   HEAD_WRAP_ONE				0x00200000
#define   HEAD_ADDR				0x001FFFFC
#define RING_START(base)			_MMIO((base) + 0x38)
#define RING_CTL(base)				_MMIO((base) + 0x3c)
#define   RING_CTL_SIZE(size)			((size) - PAGE_SIZE) /* in bytes -> pages */
#define   RING_NR_PAGES				0x001FF000
#define   RING_REPORT_MASK			0x00000006
#define   RING_REPORT_64K			0x00000002
#define   RING_REPORT_128K			0x00000004
#define   RING_NO_REPORT			0x00000000
#define   RING_VALID_MASK			0x00000001
#define   RING_VALID				0x00000001
#define   RING_INVALID				0x00000000
#define   RING_WAIT_I8XX			(1 << 0) /* gen2, PRBx_HEAD */
#define   RING_WAIT				(1 << 11) /* gen3+, PRBx_CTL */
#define   RING_WAIT_SEMAPHORE			(1 << 10) /* gen6+ */
#define RING_SYNC_0(base)			_MMIO((base) + 0x40)
#define RING_SYNC_1(base)			_MMIO((base) + 0x44)
#define RING_SYNC_2(base)			_MMIO((base) + 0x48)
#define GEN6_RVSYNC				(RING_SYNC_0(RENDER_RING_BASE))
#define GEN6_RBSYNC				(RING_SYNC_1(RENDER_RING_BASE))
#define GEN6_RVESYNC				(RING_SYNC_2(RENDER_RING_BASE))
#define GEN6_VBSYNC				(RING_SYNC_0(GEN6_BSD_RING_BASE))
#define GEN6_VRSYNC				(RING_SYNC_1(GEN6_BSD_RING_BASE))
#define GEN6_VVESYNC				(RING_SYNC_2(GEN6_BSD_RING_BASE))
#define GEN6_BRSYNC				(RING_SYNC_0(BLT_RING_BASE))
#define GEN6_BVSYNC				(RING_SYNC_1(BLT_RING_BASE))
#define GEN6_BVESYNC				(RING_SYNC_2(BLT_RING_BASE))
#define GEN6_VEBSYNC				(RING_SYNC_0(VEBOX_RING_BASE))
#define GEN6_VERSYNC				(RING_SYNC_1(VEBOX_RING_BASE))
#define GEN6_VEVSYNC				(RING_SYNC_2(VEBOX_RING_BASE))
#define RING_PSMI_CTL(base)			_MMIO((base) + 0x50)
#define   GEN8_RC_SEMA_IDLE_MSG_DISABLE		REG_BIT(12)
#define   GEN8_FF_DOP_CLOCK_GATE_DISABLE	REG_BIT(10)
#define   GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE REG_BIT(7)
#define   GEN6_BSD_GO_INDICATOR			REG_BIT(4)
#define   GEN6_BSD_SLEEP_INDICATOR		REG_BIT(3)
#define   GEN6_BSD_SLEEP_FLUSH_DISABLE		REG_BIT(2)
#define   GEN6_PSMI_SLEEP_MSG_DISABLE		REG_BIT(0)
#define RING_MAX_IDLE(base)			_MMIO((base) + 0x54)
#define  PWRCTX_MAXCNT(base)			_MMIO((base) + 0x54)
#define    IDLE_TIME_MASK			0xFFFFF
#define RING_ACTHD_UDW(base)			_MMIO((base) + 0x5c)
#define RING_DMA_FADD_UDW(base)			_MMIO((base) + 0x60) /* gen8+ */
#define RING_IPEIR(base)			_MMIO((base) + 0x64)
#define RING_IPEHR(base)			_MMIO((base) + 0x68)
#define RING_INSTDONE(base)			_MMIO((base) + 0x6c)
#define RING_INSTPS(base)			_MMIO((base) + 0x70)
#define RING_DMA_FADD(base)			_MMIO((base) + 0x78)
#define RING_ACTHD(base)			_MMIO((base) + 0x74)
#define RING_HWS_PGA(base)			_MMIO((base) + 0x80)
#define RING_CMD_BUF_CCTL(base)			_MMIO((base) + 0x84)
#define IPEIR(base)				_MMIO((base) + 0x88)
#define IPEHR(base)				_MMIO((base) + 0x8c)
#define RING_ID(base)				_MMIO((base) + 0x8c)
#define RING_NOPID(base)			_MMIO((base) + 0x94)
#define RING_HWSTAM(base)			_MMIO((base) + 0x98)
#define RING_MI_MODE(base)			_MMIO((base) + 0x9c)
#define   ASYNC_FLIP_PERF_DISABLE		REG_BIT(14)
#define   MI_FLUSH_ENABLE			REG_BIT(12)
#define   TGL_NESTED_BB_EN			REG_BIT(12)
#define   MODE_IDLE				REG_BIT(9)
#define   STOP_RING				REG_BIT(8)
#define   VS_TIMER_DISPATCH			REG_BIT(6)
#define RING_IMR(base)				_MMIO((base) + 0xa8)
#define RING_EIR(base)				_MMIO((base) + 0xb0)
#define RING_EMR(base)				_MMIO((base) + 0xb4)
#define RING_ESR(base)				_MMIO((base) + 0xb8)
#define GEN12_STATE_ACK_DEBUG(base)		_MMIO((base) + 0xbc)
#define RING_INSTPM(base)			_MMIO((base) + 0xc0)
#define RING_CMD_CCTL(base)			_MMIO((base) + 0xc4)
#define ACTHD(base)				_MMIO((base) + 0xc8)
#define GEN8_R_PWR_CLK_STATE(base)		_MMIO((base) + 0xc8)
#define   GEN8_RPCS_ENABLE			(1 << 31)
#define   GEN8_RPCS_S_CNT_ENABLE		(1 << 18)
#define   GEN8_RPCS_S_CNT_SHIFT			15
#define   GEN8_RPCS_S_CNT_MASK			(0x7 << GEN8_RPCS_S_CNT_SHIFT)
#define   GEN11_RPCS_S_CNT_SHIFT		12
#define   GEN11_RPCS_S_CNT_MASK			(0x3f << GEN11_RPCS_S_CNT_SHIFT)
#define   GEN8_RPCS_SS_CNT_ENABLE		(1 << 11)
#define   GEN8_RPCS_SS_CNT_SHIFT		8
#define   GEN8_RPCS_SS_CNT_MASK			(0x7 << GEN8_RPCS_SS_CNT_SHIFT)
#define   GEN8_RPCS_EU_MAX_SHIFT		4
#define   GEN8_RPCS_EU_MAX_MASK			(0xf << GEN8_RPCS_EU_MAX_SHIFT)
#define   GEN8_RPCS_EU_MIN_SHIFT		0
#define   GEN8_RPCS_EU_MIN_MASK			(0xf << GEN8_RPCS_EU_MIN_SHIFT)

#define RING_RESET_CTL(base)			_MMIO((base) + 0xd0)
#define   RESET_CTL_CAT_ERROR			REG_BIT(2)
#define   RESET_CTL_READY_TO_RESET		REG_BIT(1)
#define   RESET_CTL_REQUEST_RESET		REG_BIT(0)
#define DMA_FADD_I8XX(base)			_MMIO((base) + 0xd0)
#define RING_BBSTATE(base)			_MMIO((base) + 0x110)
#define   RING_BB_PPGTT				(1 << 5)
#define RING_SBBADDR(base)			_MMIO((base) + 0x114) /* hsw+ */
#define RING_SBBSTATE(base)			_MMIO((base) + 0x118) /* hsw+ */
#define RING_SBBADDR_UDW(base)			_MMIO((base) + 0x11c) /* gen8+ */
#define RING_BBADDR(base)			_MMIO((base) + 0x140)
#define RING_BB_OFFSET(base)			_MMIO((base) + 0x158)
#define RING_BBADDR_UDW(base)			_MMIO((base) + 0x168) /* gen8+ */
#define CCID(base)				_MMIO((base) + 0x180)
#define   CCID_EN				BIT(0)
#define   CCID_EXTENDED_STATE_RESTORE		BIT(2)
#define   CCID_EXTENDED_STATE_SAVE		BIT(3)
#define RING_BB_PER_CTX_PTR(base)		_MMIO((base) + 0x1c0) /* gen8+ */
#define RING_INDIRECT_CTX(base)			_MMIO((base) + 0x1c4) /* gen8+ */
#define RING_INDIRECT_CTX_OFFSET(base)		_MMIO((base) + 0x1c8) /* gen8+ */
#define ECOSKPD(base)				_MMIO((base) + 0x1d0)
#define   ECO_CONSTANT_BUFFER_SR_DISABLE	REG_BIT(4)
#define   ECO_GATING_CX_ONLY			REG_BIT(3)
#define   GEN6_BLITTER_FBC_NOTIFY		REG_BIT(3)
#define   ECO_FLIP_DONE				REG_BIT(0)
#define   GEN6_BLITTER_LOCK_SHIFT		16

#define BLIT_CCTL(base)				_MMIO((base) + 0x204)
#define   BLIT_CCTL_DST_MOCS_MASK		REG_GENMASK(14, 8)
#define   BLIT_CCTL_SRC_MOCS_MASK		REG_GENMASK(6, 0)
#define   BLIT_CCTL_MASK (BLIT_CCTL_DST_MOCS_MASK | \
			  BLIT_CCTL_SRC_MOCS_MASK)
#define   BLIT_CCTL_MOCS(dst, src)				       \
		(REG_FIELD_PREP(BLIT_CCTL_DST_MOCS_MASK, (dst) << 1) | \
		 REG_FIELD_PREP(BLIT_CCTL_SRC_MOCS_MASK, (src) << 1))

#define RING_CSCMDOP(base)			_MMIO((base) + 0x20c)

/*
 * CMD_CCTL read/write fields take a MOCS value and _not_ a table index.
 * The lsb of each can be considered a separate enabling bit for encryption.
 * 6:0 == default MOCS value for reads  =>  6:1 == table index for reads.
 * 13:7 == default MOCS value for writes => 13:8 == table index for writes.
 * 15:14 == Reserved => 31:30 are set to 0.
 */
#define CMD_CCTL_WRITE_OVERRIDE_MASK REG_GENMASK(13, 7)
#define CMD_CCTL_READ_OVERRIDE_MASK REG_GENMASK(6, 0)
#define CMD_CCTL_MOCS_MASK (CMD_CCTL_WRITE_OVERRIDE_MASK | \
			    CMD_CCTL_READ_OVERRIDE_MASK)
#define CMD_CCTL_MOCS_OVERRIDE(write, read)				      \
		(REG_FIELD_PREP(CMD_CCTL_WRITE_OVERRIDE_MASK, (write) << 1) | \
		 REG_FIELD_PREP(CMD_CCTL_READ_OVERRIDE_MASK, (read) << 1))

#define RING_PREDICATE_RESULT(base)		_MMIO((base) + 0x3b8) /* gen12+ */

#define MI_PREDICATE_RESULT_2(base)		_MMIO((base) + 0x3bc)
#define   LOWER_SLICE_ENABLED			(1 << 0)
#define   LOWER_SLICE_DISABLED			(0 << 0)
#define MI_PREDICATE_SRC0(base)			_MMIO((base) + 0x400)
#define MI_PREDICATE_SRC0_UDW(base)		_MMIO((base) + 0x400 + 4)
#define MI_PREDICATE_SRC1(base)			_MMIO((base) + 0x408)
#define MI_PREDICATE_SRC1_UDW(base)		_MMIO((base) + 0x408 + 4)
#define MI_PREDICATE_DATA(base)			_MMIO((base) + 0x410)
#define MI_PREDICATE_RESULT(base)		_MMIO((base) + 0x418)
#define MI_PREDICATE_RESULT_1(base)		_MMIO((base) + 0x41c)

#define RING_PP_DIR_DCLV(base)			_MMIO((base) + 0x220)
#define   PP_DIR_DCLV_2G			0xffffffff
#define RING_PP_DIR_BASE(base)			_MMIO((base) + 0x228)
#define RING_ELSP(base)				_MMIO((base) + 0x230)
#define RING_EXECLIST_STATUS_LO(base)		_MMIO((base) + 0x234)
#define RING_EXECLIST_STATUS_HI(base)		_MMIO((base) + 0x234 + 4)
#define RING_CONTEXT_CONTROL(base)		_MMIO((base) + 0x244)
#define	  CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT	REG_BIT(0)
#define   CTX_CTRL_RS_CTX_ENABLE		REG_BIT(1)
#define	  CTX_CTRL_ENGINE_CTX_SAVE_INHIBIT	REG_BIT(2)
#define	  CTX_CTRL_INHIBIT_SYN_CTX_SWITCH	REG_BIT(3)
#define	  GEN12_CTX_CTRL_OAR_CONTEXT_ENABLE	REG_BIT(8)
#define RING_CTX_SR_CTL(base)			_MMIO((base) + 0x244)
#define RING_SEMA_WAIT_POLL(base)		_MMIO((base) + 0x24c)
#define GEN8_RING_PDP_UDW(base, n)		_MMIO((base) + 0x270 + (n) * 8 + 4)
#define GEN8_RING_PDP_LDW(base, n)		_MMIO((base) + 0x270 + (n) * 8)
#define RING_MODE_GEN7(base)			_MMIO((base) + 0x29c)
#define   GFX_RUN_LIST_ENABLE			(1 << 15)
#define   GFX_INTERRUPT_STEERING		(1 << 14)
#define   GFX_TLB_INVALIDATE_EXPLICIT		(1 << 13)
#define   GFX_SURFACE_FAULT_ENABLE		(1 << 12)
#define   GFX_REPLAY_MODE			(1 << 11)
#define   GFX_PSMI_GRANULARITY			(1 << 10)
#define   GEN12_GFX_PREFETCH_DISABLE		REG_BIT(10)
#define   GFX_PPGTT_ENABLE			(1 << 9)
#define   GEN8_GFX_PPGTT_48B			(1 << 7)
#define   GFX_FORWARD_VBLANK_MASK		(3 << 5)
#define   GFX_FORWARD_VBLANK_NEVER		(0 << 5)
#define   GFX_FORWARD_VBLANK_ALWAYS		(1 << 5)
#define   GFX_FORWARD_VBLANK_COND		(2 << 5)
#define   GEN11_GFX_DISABLE_LEGACY_MODE		(1 << 3)
#define RING_TIMESTAMP(base)			_MMIO((base) + 0x358)
#define RING_TIMESTAMP_UDW(base)		_MMIO((base) + 0x358 + 4)
#define RING_CONTEXT_STATUS_PTR(base)		_MMIO((base) + 0x3a0)
#define RING_CTX_TIMESTAMP(base)		_MMIO((base) + 0x3a8) /* gen8+ */
#define RING_PREDICATE_RESULT(base)		_MMIO((base) + 0x3b8)
#define MI_PREDICATE_RESULT_2_ENGINE(base)	_MMIO((base) + 0x3bc)
#define RING_FORCE_TO_NONPRIV(base, i)		_MMIO(((base) + 0x4D0) + (i) * 4)
#define   RING_FORCE_TO_NONPRIV_DENY		REG_BIT(30)
#define   RING_FORCE_TO_NONPRIV_ADDRESS_MASK	REG_GENMASK(25, 2)
#define   RING_FORCE_TO_NONPRIV_ACCESS_RW	(0 << 28)    /* CFL+ & Gen11+ */
#define   RING_FORCE_TO_NONPRIV_ACCESS_RD	(1 << 28)
#define   RING_FORCE_TO_NONPRIV_ACCESS_WR	(2 << 28)
#define   RING_FORCE_TO_NONPRIV_ACCESS_INVALID	(3 << 28)
#define   RING_FORCE_TO_NONPRIV_ACCESS_MASK	(3 << 28)
#define   RING_FORCE_TO_NONPRIV_RANGE_1		(0 << 0)     /* CFL+ & Gen11+ */
#define   RING_FORCE_TO_NONPRIV_RANGE_4		(1 << 0)
#define   RING_FORCE_TO_NONPRIV_RANGE_16	(2 << 0)
#define   RING_FORCE_TO_NONPRIV_RANGE_64	(3 << 0)
#define   RING_FORCE_TO_NONPRIV_RANGE_MASK	(3 << 0)
#define   RING_FORCE_TO_NONPRIV_MASK_VALID	\
	(RING_FORCE_TO_NONPRIV_RANGE_MASK | \
	 RING_FORCE_TO_NONPRIV_ACCESS_MASK | \
	 RING_FORCE_TO_NONPRIV_DENY)
#define   RING_MAX_NONPRIV_SLOTS  12

#define RING_EXECLIST_SQ_CONTENTS(base)		_MMIO((base) + 0x510)
#define RING_PP_DIR_BASE_READ(base)		_MMIO((base) + 0x518)
#define RING_EXECLIST_CONTROL(base)		_MMIO((base) + 0x550)
#define	  EL_CTRL_LOAD				REG_BIT(0)

/* There are 16 64-bit CS General Purpose Registers per-engine on Gen8+ */
#define GEN8_RING_CS_GPR(base, n)		_MMIO((base) + 0x600 + (n) * 8)
#define GEN8_RING_CS_GPR_UDW(base, n)		_MMIO((base) + 0x600 + (n) * 8 + 4)

#define GEN11_VCS_SFC_FORCED_LOCK(base)		_MMIO((base) + 0x88c)
#define   GEN11_VCS_SFC_FORCED_LOCK_BIT		(1 << 0)
#define GEN11_VCS_SFC_LOCK_STATUS(base)		_MMIO((base) + 0x890)
#define   GEN11_VCS_SFC_USAGE_BIT		(1 << 0)
#define   GEN11_VCS_SFC_LOCK_ACK_BIT		(1 << 1)

#define GEN11_VECS_SFC_FORCED_LOCK(base)	_MMIO((base) + 0x201c)
#define   GEN11_VECS_SFC_FORCED_LOCK_BIT	(1 << 0)
#define GEN11_VECS_SFC_LOCK_ACK(base)		_MMIO((base) + 0x2018)
#define   GEN11_VECS_SFC_LOCK_ACK_BIT		(1 << 0)
#define GEN11_VECS_SFC_USAGE(base)		_MMIO((base) + 0x2014)
#define   GEN11_VECS_SFC_USAGE_BIT		(1 << 0)

#define RING_HWS_PGA_GEN6(base)	_MMIO((base) + 0x2080)

#define GEN12_HCP_SFC_LOCK_STATUS(base)		_MMIO((base) + 0x2914)
#define   GEN12_HCP_SFC_LOCK_ACK_BIT		REG_BIT(1)
#define   GEN12_HCP_SFC_USAGE_BIT		REG_BIT(0)

#define VDBOX_CGCTL3F10(base)			_MMIO((base) + 0x3f10)
#define   IECPUNIT_CLKGATE_DIS			REG_BIT(22)

#define VDBOX_CGCTL3F18(base)			_MMIO((base) + 0x3f18)
#define   ALNUNIT_CLKGATE_DIS			REG_BIT(13)


#endif /* __INTEL_ENGINE_REGS__ */
