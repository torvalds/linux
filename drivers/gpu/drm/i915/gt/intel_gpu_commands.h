/* SPDX-License-Identifier: MIT*/
/*
 * Copyright © 2003-2018 Intel Corporation
 */

#ifndef _INTEL_GPU_COMMANDS_H_
#define _INTEL_GPU_COMMANDS_H_

#include <linux/bitops.h>

/*
 * Target address alignments required for GPU access e.g.
 * MI_STORE_DWORD_IMM.
 */
#define alignof_dword 4
#define alignof_qword 8

/*
 * Instruction field definitions used by the command parser
 */
#define INSTR_CLIENT_SHIFT      29
#define   INSTR_MI_CLIENT       0x0
#define   INSTR_BC_CLIENT       0x2
#define   INSTR_RC_CLIENT       0x3
#define INSTR_SUBCLIENT_SHIFT   27
#define INSTR_SUBCLIENT_MASK    0x18000000
#define   INSTR_MEDIA_SUBCLIENT 0x2
#define INSTR_26_TO_24_MASK	0x7000000
#define   INSTR_26_TO_24_SHIFT	24

#define __INSTR(client) ((client) << INSTR_CLIENT_SHIFT)

/*
 * Memory interface instructions used by the kernel
 */
#define MI_INSTR(opcode, flags) \
	(__INSTR(INSTR_MI_CLIENT) | (opcode) << 23 | (flags))
/* Many MI commands use bit 22 of the header dword for GGTT vs PPGTT */
#define  MI_GLOBAL_GTT    (1<<22)

#define MI_NOOP			MI_INSTR(0, 0)
#define MI_SET_PREDICATE	MI_INSTR(0x01, 0)
#define   MI_SET_PREDICATE_DISABLE	(0 << 0)
#define MI_USER_INTERRUPT	MI_INSTR(0x02, 0)
#define MI_WAIT_FOR_EVENT       MI_INSTR(0x03, 0)
#define   MI_WAIT_FOR_OVERLAY_FLIP	(1<<16)
#define   MI_WAIT_FOR_PLANE_B_FLIP      (1<<6)
#define   MI_WAIT_FOR_PLANE_A_FLIP      (1<<2)
#define   MI_WAIT_FOR_PLANE_A_SCANLINES (1<<1)
#define MI_FLUSH		MI_INSTR(0x04, 0)
#define   MI_READ_FLUSH		(1 << 0)
#define   MI_EXE_FLUSH		(1 << 1)
#define   MI_NO_WRITE_FLUSH	(1 << 2)
#define   MI_SCENE_COUNT	(1 << 3) /* just increment scene count */
#define   MI_END_SCENE		(1 << 4) /* flush binner and incr scene count */
#define   MI_INVALIDATE_ISP	(1 << 5) /* invalidate indirect state pointers */
#define MI_REPORT_HEAD		MI_INSTR(0x07, 0)
#define MI_ARB_ON_OFF		MI_INSTR(0x08, 0)
#define   MI_ARB_ENABLE			(1<<0)
#define   MI_ARB_DISABLE		(0<<0)
#define MI_BATCH_BUFFER_END	MI_INSTR(0x0a, 0)
#define MI_SUSPEND_FLUSH	MI_INSTR(0x0b, 0)
#define   MI_SUSPEND_FLUSH_EN	(1<<0)
#define MI_SET_APPID		MI_INSTR(0x0e, 0)
#define   MI_SET_APPID_SESSION_ID(x)	((x) << 0)
#define MI_OVERLAY_FLIP		MI_INSTR(0x11, 0)
#define   MI_OVERLAY_CONTINUE	(0x0<<21)
#define   MI_OVERLAY_ON		(0x1<<21)
#define   MI_OVERLAY_OFF	(0x2<<21)
#define MI_LOAD_SCAN_LINES_INCL MI_INSTR(0x12, 0)
#define MI_DISPLAY_FLIP		MI_INSTR(0x14, 2)
#define MI_DISPLAY_FLIP_I915	MI_INSTR(0x14, 1)
#define   MI_DISPLAY_FLIP_PLANE(n) ((n) << 20)
/* IVB has funny definitions for which plane to flip. */
#define   MI_DISPLAY_FLIP_IVB_PLANE_A  (0 << 19)
#define   MI_DISPLAY_FLIP_IVB_PLANE_B  (1 << 19)
#define   MI_DISPLAY_FLIP_IVB_SPRITE_A (2 << 19)
#define   MI_DISPLAY_FLIP_IVB_SPRITE_B (3 << 19)
#define   MI_DISPLAY_FLIP_IVB_PLANE_C  (4 << 19)
#define   MI_DISPLAY_FLIP_IVB_SPRITE_C (5 << 19)
/* SKL ones */
#define   MI_DISPLAY_FLIP_SKL_PLANE_1_A	(0 << 8)
#define   MI_DISPLAY_FLIP_SKL_PLANE_1_B	(1 << 8)
#define   MI_DISPLAY_FLIP_SKL_PLANE_1_C	(2 << 8)
#define   MI_DISPLAY_FLIP_SKL_PLANE_2_A	(4 << 8)
#define   MI_DISPLAY_FLIP_SKL_PLANE_2_B	(5 << 8)
#define   MI_DISPLAY_FLIP_SKL_PLANE_2_C	(6 << 8)
#define   MI_DISPLAY_FLIP_SKL_PLANE_3_A	(7 << 8)
#define   MI_DISPLAY_FLIP_SKL_PLANE_3_B	(8 << 8)
#define   MI_DISPLAY_FLIP_SKL_PLANE_3_C	(9 << 8)
#define MI_SEMAPHORE_MBOX	MI_INSTR(0x16, 1) /* gen6, gen7 */
#define   MI_SEMAPHORE_GLOBAL_GTT    (1<<22)
#define   MI_SEMAPHORE_UPDATE	    (1<<21)
#define   MI_SEMAPHORE_COMPARE	    (1<<20)
#define   MI_SEMAPHORE_REGISTER	    (1<<18)
#define   MI_SEMAPHORE_SYNC_VR	    (0<<16) /* RCS  wait for VCS  (RVSYNC) */
#define   MI_SEMAPHORE_SYNC_VER	    (1<<16) /* RCS  wait for VECS (RVESYNC) */
#define   MI_SEMAPHORE_SYNC_BR	    (2<<16) /* RCS  wait for BCS  (RBSYNC) */
#define   MI_SEMAPHORE_SYNC_BV	    (0<<16) /* VCS  wait for BCS  (VBSYNC) */
#define   MI_SEMAPHORE_SYNC_VEV	    (1<<16) /* VCS  wait for VECS (VVESYNC) */
#define   MI_SEMAPHORE_SYNC_RV	    (2<<16) /* VCS  wait for RCS  (VRSYNC) */
#define   MI_SEMAPHORE_SYNC_RB	    (0<<16) /* BCS  wait for RCS  (BRSYNC) */
#define   MI_SEMAPHORE_SYNC_VEB	    (1<<16) /* BCS  wait for VECS (BVESYNC) */
#define   MI_SEMAPHORE_SYNC_VB	    (2<<16) /* BCS  wait for VCS  (BVSYNC) */
#define   MI_SEMAPHORE_SYNC_BVE	    (0<<16) /* VECS wait for BCS  (VEBSYNC) */
#define   MI_SEMAPHORE_SYNC_VVE	    (1<<16) /* VECS wait for VCS  (VEVSYNC) */
#define   MI_SEMAPHORE_SYNC_RVE	    (2<<16) /* VECS wait for RCS  (VERSYNC) */
#define   MI_SEMAPHORE_SYNC_INVALID (3<<16)
#define   MI_SEMAPHORE_SYNC_MASK    (3<<16)
#define MI_SET_CONTEXT		MI_INSTR(0x18, 0)
#define   MI_MM_SPACE_GTT		(1<<8)
#define   MI_MM_SPACE_PHYSICAL		(0<<8)
#define   MI_SAVE_EXT_STATE_EN		(1<<3)
#define   MI_RESTORE_EXT_STATE_EN	(1<<2)
#define   MI_FORCE_RESTORE		(1<<1)
#define   MI_RESTORE_INHIBIT		(1<<0)
#define   HSW_MI_RS_SAVE_STATE_EN       (1<<3)
#define   HSW_MI_RS_RESTORE_STATE_EN    (1<<2)
#define MI_SEMAPHORE_SIGNAL	MI_INSTR(0x1b, 0) /* GEN8+ */
#define   MI_SEMAPHORE_TARGET(engine)	((engine)<<15)
#define MI_SEMAPHORE_WAIT	MI_INSTR(0x1c, 2) /* GEN8+ */
#define MI_SEMAPHORE_WAIT_TOKEN	MI_INSTR(0x1c, 3) /* GEN12+ */
#define   MI_SEMAPHORE_REGISTER_POLL	(1 << 16)
#define   MI_SEMAPHORE_POLL		(1 << 15)
#define   MI_SEMAPHORE_SAD_GT_SDD	(0 << 12)
#define   MI_SEMAPHORE_SAD_GTE_SDD	(1 << 12)
#define   MI_SEMAPHORE_SAD_LT_SDD	(2 << 12)
#define   MI_SEMAPHORE_SAD_LTE_SDD	(3 << 12)
#define   MI_SEMAPHORE_SAD_EQ_SDD	(4 << 12)
#define   MI_SEMAPHORE_SAD_NEQ_SDD	(5 << 12)
#define   MI_SEMAPHORE_TOKEN_MASK	REG_GENMASK(9, 5)
#define   MI_SEMAPHORE_TOKEN_SHIFT	5
#define MI_STORE_DATA_IMM	MI_INSTR(0x20, 0)
#define MI_STORE_DWORD_IMM	MI_INSTR(0x20, 1)
#define MI_STORE_DWORD_IMM_GEN4	MI_INSTR(0x20, 2)
#define MI_STORE_QWORD_IMM_GEN8 (MI_INSTR(0x20, 3) | REG_BIT(21))
#define   MI_MEM_VIRTUAL	(1 << 22) /* 945,g33,965 */
#define   MI_USE_GGTT		(1 << 22) /* g4x+ */
#define MI_STORE_DWORD_INDEX	MI_INSTR(0x21, 1)
#define MI_ATOMIC		MI_INSTR(0x2f, 1)
#define MI_ATOMIC_INLINE	(MI_INSTR(0x2f, 9) | MI_ATOMIC_INLINE_DATA)
#define   MI_ATOMIC_GLOBAL_GTT		(1 << 22)
#define   MI_ATOMIC_INLINE_DATA		(1 << 18)
#define   MI_ATOMIC_CS_STALL		(1 << 17)
#define	  MI_ATOMIC_MOVE		(0x4 << 8)

/*
 * Official intel docs are somewhat sloppy concerning MI_LOAD_REGISTER_IMM:
 * - Always issue a MI_NOOP _before_ the MI_LOAD_REGISTER_IMM - otherwise hw
 *   simply ignores the register load under certain conditions.
 * - One can actually load arbitrary many arbitrary registers: Simply issue x
 *   address/value pairs. Don't overdue it, though, x <= 2^4 must hold!
 */
#define MI_LOAD_REGISTER_IMM(x)	MI_INSTR(0x22, 2*(x)-1)
/* Gen11+. addr = base + (ctx_restore ? offset & GENMASK(12,2) : offset) */
#define   MI_LRI_LRM_CS_MMIO		REG_BIT(19)
#define   MI_LRI_MMIO_REMAP_EN		REG_BIT(17)
#define   MI_LRI_FORCE_POSTED		(1<<12)
#define MI_LOAD_REGISTER_IMM_MAX_REGS (126)
#define MI_STORE_REGISTER_MEM        MI_INSTR(0x24, 1)
#define MI_STORE_REGISTER_MEM_GEN8   MI_INSTR(0x24, 2)
#define   MI_SRM_LRM_GLOBAL_GTT		(1<<22)
#define MI_FLUSH_DW		MI_INSTR(0x26, 1) /* for GEN6 */
#define   MI_FLUSH_DW_PROTECTED_MEM_EN	(1 << 22)
#define   MI_FLUSH_DW_STORE_INDEX	(1<<21)
#define   MI_INVALIDATE_TLB		(1<<18)
#define   MI_FLUSH_DW_CCS		(1<<16)
#define   MI_FLUSH_DW_OP_STOREDW	(1<<14)
#define   MI_FLUSH_DW_OP_MASK		(3<<14)
#define   MI_FLUSH_DW_LLC		(1<<9)
#define   MI_FLUSH_DW_NOTIFY		(1<<8)
#define   MI_INVALIDATE_BSD		(1<<7)
#define   MI_FLUSH_DW_USE_GTT		(1<<2)
#define   MI_FLUSH_DW_USE_PPGTT		(0<<2)
#define MI_LOAD_REGISTER_MEM	   MI_INSTR(0x29, 1)
#define MI_LOAD_REGISTER_MEM_GEN8  MI_INSTR(0x29, 2)
#define MI_LOAD_REGISTER_REG    MI_INSTR(0x2A, 1)
#define   MI_LRR_SOURCE_CS_MMIO		REG_BIT(18)
#define MI_BATCH_BUFFER		MI_INSTR(0x30, 1)
#define   MI_BATCH_NON_SECURE		(1)
/* for snb/ivb/vlv this also means "batch in ppgtt" when ppgtt is enabled. */
#define   MI_BATCH_NON_SECURE_I965	(1<<8)
#define   MI_BATCH_PPGTT_HSW		(1<<8)
#define   MI_BATCH_NON_SECURE_HSW	(1<<13)
#define MI_BATCH_BUFFER_START	MI_INSTR(0x31, 0)
#define   MI_BATCH_GTT		    (2<<6) /* aliased with (1<<7) on gen4 */
#define MI_BATCH_BUFFER_START_GEN8	MI_INSTR(0x31, 1)
#define   MI_BATCH_RESOURCE_STREAMER REG_BIT(10)
#define   MI_BATCH_PREDICATE         REG_BIT(15) /* HSW+ on RCS only*/

/*
 * 3D instructions used by the kernel
 */
#define GFX_INSTR(opcode, flags) ((0x3 << 29) | ((opcode) << 24) | (flags))

#define GEN9_MEDIA_POOL_STATE     ((0x3 << 29) | (0x2 << 27) | (0x5 << 16) | 4)
#define   GEN9_MEDIA_POOL_ENABLE  (1 << 31)
#define GFX_OP_RASTER_RULES    ((0x3<<29)|(0x7<<24))
#define GFX_OP_SCISSOR         ((0x3<<29)|(0x1c<<24)|(0x10<<19))
#define   SC_UPDATE_SCISSOR       (0x1<<1)
#define   SC_ENABLE_MASK          (0x1<<0)
#define   SC_ENABLE               (0x1<<0)
#define GFX_OP_LOAD_INDIRECT   ((0x3<<29)|(0x1d<<24)|(0x7<<16))
#define GFX_OP_SCISSOR_INFO    ((0x3<<29)|(0x1d<<24)|(0x81<<16)|(0x1))
#define   SCI_YMIN_MASK      (0xffff<<16)
#define   SCI_XMIN_MASK      (0xffff<<0)
#define   SCI_YMAX_MASK      (0xffff<<16)
#define   SCI_XMAX_MASK      (0xffff<<0)
#define GFX_OP_SCISSOR_ENABLE	 ((0x3<<29)|(0x1c<<24)|(0x10<<19))
#define GFX_OP_SCISSOR_RECT	 ((0x3<<29)|(0x1d<<24)|(0x81<<16)|1)
#define GFX_OP_COLOR_FACTOR      ((0x3<<29)|(0x1d<<24)|(0x1<<16)|0x0)
#define GFX_OP_STIPPLE           ((0x3<<29)|(0x1d<<24)|(0x83<<16))
#define GFX_OP_MAP_INFO          ((0x3<<29)|(0x1d<<24)|0x4)
#define GFX_OP_DESTBUFFER_VARS   ((0x3<<29)|(0x1d<<24)|(0x85<<16)|0x0)
#define GFX_OP_DESTBUFFER_INFO	 ((0x3<<29)|(0x1d<<24)|(0x8e<<16)|1)
#define GFX_OP_DRAWRECT_INFO     ((0x3<<29)|(0x1d<<24)|(0x80<<16)|(0x3))
#define GFX_OP_DRAWRECT_INFO_I965  ((0x7900<<16)|0x2)

#define XY_CTRL_SURF_INSTR_SIZE		5
#define MI_FLUSH_DW_SIZE		3
#define XY_CTRL_SURF_COPY_BLT		((2 << 29) | (0x48 << 22) | 3)
#define   SRC_ACCESS_TYPE_SHIFT		21
#define   DST_ACCESS_TYPE_SHIFT		20
#define   CCS_SIZE_MASK			0x3FF
#define   CCS_SIZE_SHIFT		8
#define   XY_CTRL_SURF_MOCS_MASK	GENMASK(31, 25)
#define   NUM_CCS_BYTES_PER_BLOCK	256
#define   NUM_BYTES_PER_CCS_BYTE	256
#define   NUM_CCS_BLKS_PER_XFER		1024
#define   INDIRECT_ACCESS		0
#define   DIRECT_ACCESS			1

#define COLOR_BLT_CMD			(2 << 29 | 0x40 << 22 | (5 - 2))
#define XY_COLOR_BLT_CMD		(2 << 29 | 0x50 << 22)
#define XY_FAST_COLOR_BLT_CMD		(2 << 29 | 0x44 << 22)
#define   XY_FAST_COLOR_BLT_DEPTH_32	(2 << 19)
#define   XY_FAST_COLOR_BLT_DW		16
#define   XY_FAST_COLOR_BLT_MOCS_MASK	GENMASK(27, 21)
#define   XY_FAST_COLOR_BLT_MEM_TYPE_SHIFT 31

#define   XY_FAST_COPY_BLT_D0_SRC_TILING_MASK     REG_GENMASK(21, 20)
#define   XY_FAST_COPY_BLT_D0_DST_TILING_MASK     REG_GENMASK(14, 13)
#define   XY_FAST_COPY_BLT_D0_SRC_TILE_MODE(mode)  \
	REG_FIELD_PREP(XY_FAST_COPY_BLT_D0_SRC_TILING_MASK, mode)
#define   XY_FAST_COPY_BLT_D0_DST_TILE_MODE(mode)  \
	REG_FIELD_PREP(XY_FAST_COPY_BLT_D0_DST_TILING_MASK, mode)
#define     LINEAR				0
#define     TILE_X				0x1
#define     XMAJOR				0x1
#define     YMAJOR				0x2
#define     TILE_64			0x3
#define   XY_FAST_COPY_BLT_D1_SRC_TILE4	REG_BIT(31)
#define   XY_FAST_COPY_BLT_D1_DST_TILE4	REG_BIT(30)
#define BLIT_CCTL_SRC_MOCS_MASK  REG_GENMASK(6, 0)
#define BLIT_CCTL_DST_MOCS_MASK  REG_GENMASK(14, 8)
/* Note:  MOCS value = (index << 1) */
#define BLIT_CCTL_SRC_MOCS(idx) \
	REG_FIELD_PREP(BLIT_CCTL_SRC_MOCS_MASK, (idx) << 1)
#define BLIT_CCTL_DST_MOCS(idx) \
	REG_FIELD_PREP(BLIT_CCTL_DST_MOCS_MASK, (idx) << 1)

#define SRC_COPY_BLT_CMD		(2 << 29 | 0x43 << 22)
#define GEN9_XY_FAST_COPY_BLT_CMD	(2 << 29 | 0x42 << 22)
#define XY_SRC_COPY_BLT_CMD		(2 << 29 | 0x53 << 22)
#define XY_MONO_SRC_COPY_IMM_BLT	(2 << 29 | 0x71 << 22 | 5)
#define   BLT_WRITE_A			(2<<20)
#define   BLT_WRITE_RGB			(1<<20)
#define   BLT_WRITE_RGBA		(BLT_WRITE_RGB | BLT_WRITE_A)
#define   BLT_DEPTH_8			(0<<24)
#define   BLT_DEPTH_16_565		(1<<24)
#define   BLT_DEPTH_16_1555		(2<<24)
#define   BLT_DEPTH_32			(3<<24)
#define   BLT_ROP_SRC_COPY		(0xcc<<16)
#define   BLT_ROP_COLOR_COPY		(0xf0<<16)
#define XY_SRC_COPY_BLT_SRC_TILED	(1<<15) /* 965+ only */
#define XY_SRC_COPY_BLT_DST_TILED	(1<<11) /* 965+ only */
#define CMD_OP_DISPLAYBUFFER_INFO ((0x0<<29)|(0x14<<23)|2)
#define   ASYNC_FLIP                (1<<22)
#define   DISPLAY_PLANE_A           (0<<20)
#define   DISPLAY_PLANE_B           (1<<20)
#define GFX_OP_PIPE_CONTROL(len)	((0x3<<29)|(0x3<<27)|(0x2<<24)|((len)-2))
#define   PIPE_CONTROL_COMMAND_CACHE_INVALIDATE		(1<<29) /* gen11+ */
#define   PIPE_CONTROL_TILE_CACHE_FLUSH			(1<<28) /* gen11+ */
#define   PIPE_CONTROL_FLUSH_L3				(1<<27)
#define   PIPE_CONTROL_AMFS_FLUSH			(1<<25) /* gen12+ */
#define   PIPE_CONTROL_GLOBAL_GTT_IVB			(1<<24) /* gen7+ */
#define   PIPE_CONTROL_MMIO_WRITE			(1<<23)
#define   PIPE_CONTROL_STORE_DATA_INDEX			(1<<21)
#define   PIPE_CONTROL_CS_STALL				(1<<20)
#define   PIPE_CONTROL_GLOBAL_SNAPSHOT_RESET		(1<<19)
#define   PIPE_CONTROL_TLB_INVALIDATE			(1<<18)
#define   PIPE_CONTROL_PSD_SYNC				(1<<17) /* gen11+ */
#define   PIPE_CONTROL_MEDIA_STATE_CLEAR		(1<<16)
#define   PIPE_CONTROL_WRITE_TIMESTAMP			(3<<14)
#define   PIPE_CONTROL_QW_WRITE				(1<<14)
#define   PIPE_CONTROL_POST_SYNC_OP_MASK                (3<<14)
#define   PIPE_CONTROL_DEPTH_STALL			(1<<13)
#define   PIPE_CONTROL_WRITE_FLUSH			(1<<12)
#define   PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH	(1<<12) /* gen6+ */
#define   PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE	(1<<11) /* MBZ on ILK */
#define   PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE		(1<<10) /* GM45+ only */
#define   PIPE_CONTROL_INDIRECT_STATE_DISABLE		(1<<9)
#define   PIPE_CONTROL0_HDC_PIPELINE_FLUSH		REG_BIT(9)  /* gen12 */
#define   PIPE_CONTROL_NOTIFY				(1<<8)
#define   PIPE_CONTROL_FLUSH_ENABLE			(1<<7) /* gen7+ */
#define   PIPE_CONTROL_DC_FLUSH_ENABLE			(1<<5)
#define   PIPE_CONTROL_VF_CACHE_INVALIDATE		(1<<4)
#define   PIPE_CONTROL_CONST_CACHE_INVALIDATE		(1<<3)
#define   PIPE_CONTROL_STATE_CACHE_INVALIDATE		(1<<2)
#define   PIPE_CONTROL_STALL_AT_SCOREBOARD		(1<<1)
#define   PIPE_CONTROL_DEPTH_CACHE_FLUSH		(1<<0)
#define   PIPE_CONTROL_GLOBAL_GTT (1<<2) /* in addr dword */

/*
 * 3D-related flags that can't be set on _engines_ that lack access to the 3D
 * pipeline (i.e., CCS engines).
 */
#define PIPE_CONTROL_3D_ENGINE_FLAGS (\
		PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | \
		PIPE_CONTROL_DEPTH_CACHE_FLUSH | \
		PIPE_CONTROL_TILE_CACHE_FLUSH | \
		PIPE_CONTROL_DEPTH_STALL | \
		PIPE_CONTROL_STALL_AT_SCOREBOARD | \
		PIPE_CONTROL_PSD_SYNC | \
		PIPE_CONTROL_AMFS_FLUSH | \
		PIPE_CONTROL_VF_CACHE_INVALIDATE | \
		PIPE_CONTROL_GLOBAL_SNAPSHOT_RESET)

/* 3D-related flags that can't be set on _platforms_ that lack a 3D pipeline */
#define PIPE_CONTROL_3D_ARCH_FLAGS ( \
		PIPE_CONTROL_3D_ENGINE_FLAGS | \
		PIPE_CONTROL_INDIRECT_STATE_DISABLE | \
		PIPE_CONTROL_FLUSH_ENABLE | \
		PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | \
		PIPE_CONTROL_DC_FLUSH_ENABLE)

#define MI_MATH(x)			MI_INSTR(0x1a, (x) - 1)
#define MI_MATH_INSTR(opcode, op1, op2) ((opcode) << 20 | (op1) << 10 | (op2))
/* Opcodes for MI_MATH_INSTR */
#define   MI_MATH_NOOP			MI_MATH_INSTR(0x000, 0x0, 0x0)
#define   MI_MATH_LOAD(op1, op2)	MI_MATH_INSTR(0x080, op1, op2)
#define   MI_MATH_LOADINV(op1, op2)	MI_MATH_INSTR(0x480, op1, op2)
#define   MI_MATH_LOAD0(op1)		MI_MATH_INSTR(0x081, op1)
#define   MI_MATH_LOAD1(op1)		MI_MATH_INSTR(0x481, op1)
#define   MI_MATH_ADD			MI_MATH_INSTR(0x100, 0x0, 0x0)
#define   MI_MATH_SUB			MI_MATH_INSTR(0x101, 0x0, 0x0)
#define   MI_MATH_AND			MI_MATH_INSTR(0x102, 0x0, 0x0)
#define   MI_MATH_OR			MI_MATH_INSTR(0x103, 0x0, 0x0)
#define   MI_MATH_XOR			MI_MATH_INSTR(0x104, 0x0, 0x0)
#define   MI_MATH_STORE(op1, op2)	MI_MATH_INSTR(0x180, op1, op2)
#define   MI_MATH_STOREINV(op1, op2)	MI_MATH_INSTR(0x580, op1, op2)
/* Registers used as operands in MI_MATH_INSTR */
#define   MI_MATH_REG(x)		(x)
#define   MI_MATH_REG_SRCA		0x20
#define   MI_MATH_REG_SRCB		0x21
#define   MI_MATH_REG_ACCU		0x31
#define   MI_MATH_REG_ZF		0x32
#define   MI_MATH_REG_CF		0x33

/*
 * Media instructions used by the kernel
 */
#define MEDIA_INSTR(pipe, op, sub_op, flags) \
	(__INSTR(INSTR_RC_CLIENT) | (pipe) << INSTR_SUBCLIENT_SHIFT | \
	(op) << INSTR_26_TO_24_SHIFT | (sub_op) << 16 | (flags))

#define MFX_WAIT				MEDIA_INSTR(1, 0, 0, 0)
#define  MFX_WAIT_DW0_MFX_SYNC_CONTROL_FLAG	REG_BIT(8)
#define  MFX_WAIT_DW0_PXP_SYNC_CONTROL_FLAG	REG_BIT(9)

#define CRYPTO_KEY_EXCHANGE			MEDIA_INSTR(2, 6, 9, 0)

/*
 * Commands used only by the command parser
 */
#define MI_SET_PREDICATE        MI_INSTR(0x01, 0)
#define MI_ARB_CHECK            MI_INSTR(0x05, 0)
#define MI_RS_CONTROL           MI_INSTR(0x06, 0)
#define MI_URB_ATOMIC_ALLOC     MI_INSTR(0x09, 0)
#define MI_PREDICATE            MI_INSTR(0x0C, 0)
#define MI_RS_CONTEXT           MI_INSTR(0x0F, 0)
#define MI_TOPOLOGY_FILTER      MI_INSTR(0x0D, 0)
#define MI_LOAD_SCAN_LINES_EXCL MI_INSTR(0x13, 0)
#define MI_URB_CLEAR            MI_INSTR(0x19, 0)
#define MI_UPDATE_GTT           MI_INSTR(0x23, 0)
#define MI_CLFLUSH              MI_INSTR(0x27, 0)
#define MI_REPORT_PERF_COUNT    MI_INSTR(0x28, 0)
#define   MI_REPORT_PERF_COUNT_GGTT (1<<0)
#define MI_RS_STORE_DATA_IMM    MI_INSTR(0x2B, 0)
#define MI_LOAD_URB_MEM         MI_INSTR(0x2C, 0)
#define MI_STORE_URB_MEM        MI_INSTR(0x2D, 0)
#define MI_CONDITIONAL_BATCH_BUFFER_END MI_INSTR(0x36, 0)

#define STATE_BASE_ADDRESS \
	((0x3 << 29) | (0x0 << 27) | (0x1 << 24) | (0x1 << 16))
#define BASE_ADDRESS_MODIFY		REG_BIT(0)
#define PIPELINE_SELECT \
	((0x3 << 29) | (0x1 << 27) | (0x1 << 24) | (0x4 << 16))
#define PIPELINE_SELECT_MEDIA	       REG_BIT(0)
#define GFX_OP_3DSTATE_VF_STATISTICS \
	((0x3 << 29) | (0x1 << 27) | (0x0 << 24) | (0xB << 16))
#define MEDIA_VFE_STATE \
	((0x3 << 29) | (0x2 << 27) | (0x0 << 24) | (0x0 << 16))
#define  MEDIA_VFE_STATE_MMIO_ACCESS_MASK (0x18)
#define MEDIA_INTERFACE_DESCRIPTOR_LOAD \
	((0x3 << 29) | (0x2 << 27) | (0x0 << 24) | (0x2 << 16))
#define MEDIA_OBJECT \
	((0x3 << 29) | (0x2 << 27) | (0x1 << 24) | (0x0 << 16))
#define GPGPU_OBJECT                   ((0x3<<29)|(0x2<<27)|(0x1<<24)|(0x4<<16))
#define GPGPU_WALKER                   ((0x3<<29)|(0x2<<27)|(0x1<<24)|(0x5<<16))
#define GFX_OP_3DSTATE_DX9_CONSTANTF_VS \
	((0x3<<29)|(0x3<<27)|(0x0<<24)|(0x39<<16))
#define GFX_OP_3DSTATE_DX9_CONSTANTF_PS \
	((0x3<<29)|(0x3<<27)|(0x0<<24)|(0x3A<<16))
#define GFX_OP_3DSTATE_SO_DECL_LIST \
	((0x3<<29)|(0x3<<27)|(0x1<<24)|(0x17<<16))

#define GFX_OP_3DSTATE_BINDING_TABLE_EDIT_VS \
	((0x3<<29)|(0x3<<27)|(0x0<<24)|(0x43<<16))
#define GFX_OP_3DSTATE_BINDING_TABLE_EDIT_GS \
	((0x3<<29)|(0x3<<27)|(0x0<<24)|(0x44<<16))
#define GFX_OP_3DSTATE_BINDING_TABLE_EDIT_HS \
	((0x3<<29)|(0x3<<27)|(0x0<<24)|(0x45<<16))
#define GFX_OP_3DSTATE_BINDING_TABLE_EDIT_DS \
	((0x3<<29)|(0x3<<27)|(0x0<<24)|(0x46<<16))
#define GFX_OP_3DSTATE_BINDING_TABLE_EDIT_PS \
	((0x3<<29)|(0x3<<27)|(0x0<<24)|(0x47<<16))

#define COLOR_BLT     ((0x2<<29)|(0x40<<22))
#define SRC_COPY_BLT  ((0x2<<29)|(0x43<<22))

/*
 * Used to convert any address to canonical form.
 * Starting from gen8, some commands (e.g. STATE_BASE_ADDRESS,
 * MI_LOAD_REGISTER_MEM and others, see Broadwell PRM Vol2a) require the
 * addresses to be in a canonical form:
 * "GraphicsAddress[63:48] are ignored by the HW and assumed to be in correct
 * canonical form [63:48] == [47]."
 */
#define GEN8_HIGH_ADDRESS_BIT 47
static inline u64 gen8_canonical_addr(u64 address)
{
	return sign_extend64(address, GEN8_HIGH_ADDRESS_BIT);
}

static inline u64 gen8_noncanonical_addr(u64 address)
{
	return address & GENMASK_ULL(GEN8_HIGH_ADDRESS_BIT, 0);
}

static inline u32 *__gen6_emit_bb_start(u32 *cs, u32 addr, unsigned int flags)
{
	*cs++ = MI_BATCH_BUFFER_START | flags;
	*cs++ = addr;

	return cs;
}

#endif /* _INTEL_GPU_COMMANDS_H_ */
