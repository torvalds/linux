/* Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _I915_REG_H_
#define _I915_REG_H_

#define _PIPE(pipe, a, b) ((a) + (pipe)*((b)-(a)))
#define _PLANE(plane, a, b) _PIPE(plane, a, b)
#define _TRANSCODER(tran, a, b) ((a) + (tran)*((b)-(a)))
#define _PORT(port, a, b) ((a) + (port)*((b)-(a)))
#define _PIPE3(pipe, a, b, c) ((pipe) == PIPE_A ? (a) : \
			       (pipe) == PIPE_B ? (b) : (c))
#define _PORT3(port, a, b, c) ((port) == PORT_A ? (a) : \
			       (port) == PORT_B ? (b) : (c))

#define _MASKED_BIT_ENABLE(a) (((a) << 16) | (a))
#define _MASKED_BIT_DISABLE(a) ((a) << 16)

/* PCI config space */

#define HPLLCC	0xc0 /* 855 only */
#define   GC_CLOCK_CONTROL_MASK		(0xf << 0)
#define   GC_CLOCK_133_200		(0 << 0)
#define   GC_CLOCK_100_200		(1 << 0)
#define   GC_CLOCK_100_133		(2 << 0)
#define   GC_CLOCK_166_250		(3 << 0)
#define GCFGC2	0xda
#define GCFGC	0xf0 /* 915+ only */
#define   GC_LOW_FREQUENCY_ENABLE	(1 << 7)
#define   GC_DISPLAY_CLOCK_190_200_MHZ	(0 << 4)
#define   GC_DISPLAY_CLOCK_333_MHZ	(4 << 4)
#define   GC_DISPLAY_CLOCK_267_MHZ_PNV	(0 << 4)
#define   GC_DISPLAY_CLOCK_333_MHZ_PNV	(1 << 4)
#define   GC_DISPLAY_CLOCK_444_MHZ_PNV	(2 << 4)
#define   GC_DISPLAY_CLOCK_200_MHZ_PNV	(5 << 4)
#define   GC_DISPLAY_CLOCK_133_MHZ_PNV	(6 << 4)
#define   GC_DISPLAY_CLOCK_167_MHZ_PNV	(7 << 4)
#define   GC_DISPLAY_CLOCK_MASK		(7 << 4)
#define   GM45_GC_RENDER_CLOCK_MASK	(0xf << 0)
#define   GM45_GC_RENDER_CLOCK_266_MHZ	(8 << 0)
#define   GM45_GC_RENDER_CLOCK_320_MHZ	(9 << 0)
#define   GM45_GC_RENDER_CLOCK_400_MHZ	(0xb << 0)
#define   GM45_GC_RENDER_CLOCK_533_MHZ	(0xc << 0)
#define   I965_GC_RENDER_CLOCK_MASK	(0xf << 0)
#define   I965_GC_RENDER_CLOCK_267_MHZ	(2 << 0)
#define   I965_GC_RENDER_CLOCK_333_MHZ	(3 << 0)
#define   I965_GC_RENDER_CLOCK_444_MHZ	(4 << 0)
#define   I965_GC_RENDER_CLOCK_533_MHZ	(5 << 0)
#define   I945_GC_RENDER_CLOCK_MASK	(7 << 0)
#define   I945_GC_RENDER_CLOCK_166_MHZ	(0 << 0)
#define   I945_GC_RENDER_CLOCK_200_MHZ	(1 << 0)
#define   I945_GC_RENDER_CLOCK_250_MHZ	(3 << 0)
#define   I945_GC_RENDER_CLOCK_400_MHZ	(5 << 0)
#define   I915_GC_RENDER_CLOCK_MASK	(7 << 0)
#define   I915_GC_RENDER_CLOCK_166_MHZ	(0 << 0)
#define   I915_GC_RENDER_CLOCK_200_MHZ	(1 << 0)
#define   I915_GC_RENDER_CLOCK_333_MHZ	(4 << 0)
#define PCI_LBPC 0xf4 /* legacy/combination backlight modes, also called LBB */


/* Graphics reset regs */
#define I915_GDRST 0xc0 /* PCI config register */
#define  GRDOM_FULL	(0<<2)
#define  GRDOM_RENDER	(1<<2)
#define  GRDOM_MEDIA	(3<<2)
#define  GRDOM_MASK	(3<<2)
#define  GRDOM_RESET_STATUS (1<<1)
#define  GRDOM_RESET_ENABLE (1<<0)

#define ILK_GDSR 0x2ca4 /* MCHBAR offset */
#define  ILK_GRDOM_FULL		(0<<1)
#define  ILK_GRDOM_RENDER	(1<<1)
#define  ILK_GRDOM_MEDIA	(3<<1)
#define  ILK_GRDOM_MASK		(3<<1)
#define  ILK_GRDOM_RESET_ENABLE (1<<0)

#define GEN6_MBCUNIT_SNPCR	0x900c /* for LLC config */
#define   GEN6_MBC_SNPCR_SHIFT	21
#define   GEN6_MBC_SNPCR_MASK	(3<<21)
#define   GEN6_MBC_SNPCR_MAX	(0<<21)
#define   GEN6_MBC_SNPCR_MED	(1<<21)
#define   GEN6_MBC_SNPCR_LOW	(2<<21)
#define   GEN6_MBC_SNPCR_MIN	(3<<21) /* only 1/16th of the cache is shared */

#define VLV_G3DCTL		0x9024
#define VLV_GSCKGCTL		0x9028

#define GEN6_MBCTL		0x0907c
#define   GEN6_MBCTL_ENABLE_BOOT_FETCH	(1 << 4)
#define   GEN6_MBCTL_CTX_FETCH_NEEDED	(1 << 3)
#define   GEN6_MBCTL_BME_UPDATE_ENABLE	(1 << 2)
#define   GEN6_MBCTL_MAE_UPDATE_ENABLE	(1 << 1)
#define   GEN6_MBCTL_BOOT_FETCH_MECH	(1 << 0)

#define GEN6_GDRST	0x941c
#define  GEN6_GRDOM_FULL		(1 << 0)
#define  GEN6_GRDOM_RENDER		(1 << 1)
#define  GEN6_GRDOM_MEDIA		(1 << 2)
#define  GEN6_GRDOM_BLT			(1 << 3)

#define RING_PP_DIR_BASE(ring)		((ring)->mmio_base+0x228)
#define RING_PP_DIR_BASE_READ(ring)	((ring)->mmio_base+0x518)
#define RING_PP_DIR_DCLV(ring)		((ring)->mmio_base+0x220)
#define   PP_DIR_DCLV_2G		0xffffffff

#define GEN8_RING_PDP_UDW(ring, n)	((ring)->mmio_base+0x270 + ((n) * 8 + 4))
#define GEN8_RING_PDP_LDW(ring, n)	((ring)->mmio_base+0x270 + (n) * 8)

#define GAM_ECOCHK			0x4090
#define   ECOCHK_SNB_BIT		(1<<10)
#define   HSW_ECOCHK_ARB_PRIO_SOL	(1<<6)
#define   ECOCHK_PPGTT_CACHE64B		(0x3<<3)
#define   ECOCHK_PPGTT_CACHE4B		(0x0<<3)
#define   ECOCHK_PPGTT_GFDT_IVB		(0x1<<4)
#define   ECOCHK_PPGTT_LLC_IVB		(0x1<<3)
#define   ECOCHK_PPGTT_UC_HSW		(0x1<<3)
#define   ECOCHK_PPGTT_WT_HSW		(0x2<<3)
#define   ECOCHK_PPGTT_WB_HSW		(0x3<<3)

#define GAC_ECO_BITS			0x14090
#define   ECOBITS_SNB_BIT		(1<<13)
#define   ECOBITS_PPGTT_CACHE64B	(3<<8)
#define   ECOBITS_PPGTT_CACHE4B		(0<<8)

#define GAB_CTL				0x24000
#define   GAB_CTL_CONT_AFTER_PAGEFAULT	(1<<8)

#define GEN7_BIOS_RESERVED		0x1082C0
#define GEN7_BIOS_RESERVED_1M		(0 << 5)
#define GEN7_BIOS_RESERVED_256K		(1 << 5)
#define GEN8_BIOS_RESERVED_SHIFT       7
#define GEN7_BIOS_RESERVED_MASK        0x1
#define GEN8_BIOS_RESERVED_MASK        0x3


/* VGA stuff */

#define VGA_ST01_MDA 0x3ba
#define VGA_ST01_CGA 0x3da

#define VGA_MSR_WRITE 0x3c2
#define VGA_MSR_READ 0x3cc
#define   VGA_MSR_MEM_EN (1<<1)
#define   VGA_MSR_CGA_MODE (1<<0)

#define VGA_SR_INDEX 0x3c4
#define SR01			1
#define VGA_SR_DATA 0x3c5

#define VGA_AR_INDEX 0x3c0
#define   VGA_AR_VID_EN (1<<5)
#define VGA_AR_DATA_WRITE 0x3c0
#define VGA_AR_DATA_READ 0x3c1

#define VGA_GR_INDEX 0x3ce
#define VGA_GR_DATA 0x3cf
/* GR05 */
#define   VGA_GR_MEM_READ_MODE_SHIFT 3
#define     VGA_GR_MEM_READ_MODE_PLANE 1
/* GR06 */
#define   VGA_GR_MEM_MODE_MASK 0xc
#define   VGA_GR_MEM_MODE_SHIFT 2
#define   VGA_GR_MEM_A0000_AFFFF 0
#define   VGA_GR_MEM_A0000_BFFFF 1
#define   VGA_GR_MEM_B0000_B7FFF 2
#define   VGA_GR_MEM_B0000_BFFFF 3

#define VGA_DACMASK 0x3c6
#define VGA_DACRX 0x3c7
#define VGA_DACWX 0x3c8
#define VGA_DACDATA 0x3c9

#define VGA_CR_INDEX_MDA 0x3b4
#define VGA_CR_DATA_MDA 0x3b5
#define VGA_CR_INDEX_CGA 0x3d4
#define VGA_CR_DATA_CGA 0x3d5

/*
 * Instruction field definitions used by the command parser
 */
#define INSTR_CLIENT_SHIFT      29
#define INSTR_CLIENT_MASK       0xE0000000
#define   INSTR_MI_CLIENT       0x0
#define   INSTR_BC_CLIENT       0x2
#define   INSTR_RC_CLIENT       0x3
#define INSTR_SUBCLIENT_SHIFT   27
#define INSTR_SUBCLIENT_MASK    0x18000000
#define   INSTR_MEDIA_SUBCLIENT 0x2
#define INSTR_26_TO_24_MASK	0x7000000
#define   INSTR_26_TO_24_SHIFT	24

/*
 * Memory interface instructions used by the kernel
 */
#define MI_INSTR(opcode, flags) (((opcode) << 23) | (flags))
/* Many MI commands use bit 22 of the header dword for GGTT vs PPGTT */
#define  MI_GLOBAL_GTT    (1<<22)

#define MI_NOOP			MI_INSTR(0, 0)
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
#define MI_SEMAPHORE_SIGNAL	MI_INSTR(0x1b, 0) /* GEN8+ */
#define   MI_SEMAPHORE_TARGET(engine)	((engine)<<15)
#define MI_SEMAPHORE_WAIT	MI_INSTR(0x1c, 2) /* GEN8+ */
#define   MI_SEMAPHORE_POLL		(1<<15)
#define   MI_SEMAPHORE_SAD_GTE_SDD	(1<<12)
#define MI_STORE_DWORD_IMM	MI_INSTR(0x20, 1)
#define MI_STORE_DWORD_IMM_GEN4	MI_INSTR(0x20, 2)
#define   MI_MEM_VIRTUAL	(1 << 22) /* 945,g33,965 */
#define   MI_USE_GGTT		(1 << 22) /* g4x+ */
#define MI_STORE_DWORD_INDEX	MI_INSTR(0x21, 1)
#define   MI_STORE_DWORD_INDEX_SHIFT 2
/* Official intel docs are somewhat sloppy concerning MI_LOAD_REGISTER_IMM:
 * - Always issue a MI_NOOP _before_ the MI_LOAD_REGISTER_IMM - otherwise hw
 *   simply ignores the register load under certain conditions.
 * - One can actually load arbitrary many arbitrary registers: Simply issue x
 *   address/value pairs. Don't overdue it, though, x <= 2^4 must hold!
 */
#define MI_LOAD_REGISTER_IMM(x)	MI_INSTR(0x22, 2*(x)-1)
#define   MI_LRI_FORCE_POSTED		(1<<12)
#define MI_STORE_REGISTER_MEM(x) MI_INSTR(0x24, 2*(x)-1)
#define MI_STORE_REGISTER_MEM_GEN8(x) MI_INSTR(0x24, 3*(x)-1)
#define   MI_SRM_LRM_GLOBAL_GTT		(1<<22)
#define MI_FLUSH_DW		MI_INSTR(0x26, 1) /* for GEN6 */
#define   MI_FLUSH_DW_STORE_INDEX	(1<<21)
#define   MI_INVALIDATE_TLB		(1<<18)
#define   MI_FLUSH_DW_OP_STOREDW	(1<<14)
#define   MI_FLUSH_DW_OP_MASK		(3<<14)
#define   MI_FLUSH_DW_NOTIFY		(1<<8)
#define   MI_INVALIDATE_BSD		(1<<7)
#define   MI_FLUSH_DW_USE_GTT		(1<<2)
#define   MI_FLUSH_DW_USE_PPGTT		(0<<2)
#define MI_BATCH_BUFFER		MI_INSTR(0x30, 1)
#define   MI_BATCH_NON_SECURE		(1)
/* for snb/ivb/vlv this also means "batch in ppgtt" when ppgtt is enabled. */
#define   MI_BATCH_NON_SECURE_I965	(1<<8)
#define   MI_BATCH_PPGTT_HSW		(1<<8)
#define   MI_BATCH_NON_SECURE_HSW	(1<<13)
#define MI_BATCH_BUFFER_START	MI_INSTR(0x31, 0)
#define   MI_BATCH_GTT		    (2<<6) /* aliased with (1<<7) on gen4 */
#define MI_BATCH_BUFFER_START_GEN8	MI_INSTR(0x31, 1)

#define MI_PREDICATE_SRC0	(0x2400)
#define MI_PREDICATE_SRC1	(0x2408)

#define MI_PREDICATE_RESULT_2	(0x2214)
#define  LOWER_SLICE_ENABLED	(1<<0)
#define  LOWER_SLICE_DISABLED	(0<<0)

/*
 * 3D instructions used by the kernel
 */
#define GFX_INSTR(opcode, flags) ((0x3 << 29) | ((opcode) << 24) | (flags))

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

#define COLOR_BLT_CMD			(2<<29 | 0x40<<22 | (5-2))
#define SRC_COPY_BLT_CMD		((2<<29)|(0x43<<22)|4)
#define XY_SRC_COPY_BLT_CMD		((2<<29)|(0x53<<22)|6)
#define XY_MONO_SRC_COPY_IMM_BLT	((2<<29)|(0x71<<22)|5)
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
#define GFX_OP_PIPE_CONTROL(len)	((0x3<<29)|(0x3<<27)|(0x2<<24)|(len-2))
#define   PIPE_CONTROL_GLOBAL_GTT_IVB			(1<<24) /* gen7+ */
#define   PIPE_CONTROL_MMIO_WRITE			(1<<23)
#define   PIPE_CONTROL_STORE_DATA_INDEX			(1<<21)
#define   PIPE_CONTROL_CS_STALL				(1<<20)
#define   PIPE_CONTROL_TLB_INVALIDATE			(1<<18)
#define   PIPE_CONTROL_QW_WRITE				(1<<14)
#define   PIPE_CONTROL_POST_SYNC_OP_MASK                (3<<14)
#define   PIPE_CONTROL_DEPTH_STALL			(1<<13)
#define   PIPE_CONTROL_WRITE_FLUSH			(1<<12)
#define   PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH	(1<<12) /* gen6+ */
#define   PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE	(1<<11) /* MBZ on Ironlake */
#define   PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE		(1<<10) /* GM45+ only */
#define   PIPE_CONTROL_INDIRECT_STATE_DISABLE		(1<<9)
#define   PIPE_CONTROL_NOTIFY				(1<<8)
#define   PIPE_CONTROL_FLUSH_ENABLE			(1<<7) /* gen7+ */
#define   PIPE_CONTROL_VF_CACHE_INVALIDATE		(1<<4)
#define   PIPE_CONTROL_CONST_CACHE_INVALIDATE		(1<<3)
#define   PIPE_CONTROL_STATE_CACHE_INVALIDATE		(1<<2)
#define   PIPE_CONTROL_STALL_AT_SCOREBOARD		(1<<1)
#define   PIPE_CONTROL_DEPTH_CACHE_FLUSH		(1<<0)
#define   PIPE_CONTROL_GLOBAL_GTT (1<<2) /* in addr dword */

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
#define MI_LOAD_REGISTER_MEM    MI_INSTR(0x29, 0)
#define MI_LOAD_REGISTER_REG    MI_INSTR(0x2A, 0)
#define MI_RS_STORE_DATA_IMM    MI_INSTR(0x2B, 0)
#define MI_LOAD_URB_MEM         MI_INSTR(0x2C, 0)
#define MI_STORE_URB_MEM        MI_INSTR(0x2D, 0)
#define MI_CONDITIONAL_BATCH_BUFFER_END MI_INSTR(0x36, 0)

#define PIPELINE_SELECT                ((0x3<<29)|(0x1<<27)|(0x1<<24)|(0x4<<16))
#define GFX_OP_3DSTATE_VF_STATISTICS   ((0x3<<29)|(0x1<<27)|(0x0<<24)|(0xB<<16))
#define MEDIA_VFE_STATE                ((0x3<<29)|(0x2<<27)|(0x0<<24)|(0x0<<16))
#define  MEDIA_VFE_STATE_MMIO_ACCESS_MASK (0x18)
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

#define MFX_WAIT  ((0x3<<29)|(0x1<<27)|(0x0<<16))

#define COLOR_BLT     ((0x2<<29)|(0x40<<22))
#define SRC_COPY_BLT  ((0x2<<29)|(0x43<<22))

/*
 * Registers used only by the command parser
 */
#define BCS_SWCTRL 0x22200

#define HS_INVOCATION_COUNT 0x2300
#define DS_INVOCATION_COUNT 0x2308
#define IA_VERTICES_COUNT   0x2310
#define IA_PRIMITIVES_COUNT 0x2318
#define VS_INVOCATION_COUNT 0x2320
#define GS_INVOCATION_COUNT 0x2328
#define GS_PRIMITIVES_COUNT 0x2330
#define CL_INVOCATION_COUNT 0x2338
#define CL_PRIMITIVES_COUNT 0x2340
#define PS_INVOCATION_COUNT 0x2348
#define PS_DEPTH_COUNT      0x2350

/* There are the 4 64-bit counter registers, one for each stream output */
#define GEN7_SO_NUM_PRIMS_WRITTEN(n) (0x5200 + (n) * 8)

#define GEN7_SO_PRIM_STORAGE_NEEDED(n)  (0x5240 + (n) * 8)

#define GEN7_3DPRIM_END_OFFSET          0x2420
#define GEN7_3DPRIM_START_VERTEX        0x2430
#define GEN7_3DPRIM_VERTEX_COUNT        0x2434
#define GEN7_3DPRIM_INSTANCE_COUNT      0x2438
#define GEN7_3DPRIM_START_INSTANCE      0x243C
#define GEN7_3DPRIM_BASE_VERTEX         0x2440

#define OACONTROL 0x2360

#define _GEN7_PIPEA_DE_LOAD_SL	0x70068
#define _GEN7_PIPEB_DE_LOAD_SL	0x71068
#define GEN7_PIPE_DE_LOAD_SL(pipe) _PIPE(pipe, \
					 _GEN7_PIPEA_DE_LOAD_SL, \
					 _GEN7_PIPEB_DE_LOAD_SL)

/*
 * Reset registers
 */
#define DEBUG_RESET_I830		0x6070
#define  DEBUG_RESET_FULL		(1<<7)
#define  DEBUG_RESET_RENDER		(1<<8)
#define  DEBUG_RESET_DISPLAY		(1<<9)

/*
 * IOSF sideband
 */
#define VLV_IOSF_DOORBELL_REQ			(VLV_DISPLAY_BASE + 0x2100)
#define   IOSF_DEVFN_SHIFT			24
#define   IOSF_OPCODE_SHIFT			16
#define   IOSF_PORT_SHIFT			8
#define   IOSF_BYTE_ENABLES_SHIFT		4
#define   IOSF_BAR_SHIFT			1
#define   IOSF_SB_BUSY				(1<<0)
#define   IOSF_PORT_BUNIT			0x3
#define   IOSF_PORT_PUNIT			0x4
#define   IOSF_PORT_NC				0x11
#define   IOSF_PORT_DPIO			0x12
#define   IOSF_PORT_DPIO_2			0x1a
#define   IOSF_PORT_GPIO_NC			0x13
#define   IOSF_PORT_CCK				0x14
#define   IOSF_PORT_CCU				0xA9
#define   IOSF_PORT_GPS_CORE			0x48
#define   IOSF_PORT_FLISDSI			0x1B
#define VLV_IOSF_DATA				(VLV_DISPLAY_BASE + 0x2104)
#define VLV_IOSF_ADDR				(VLV_DISPLAY_BASE + 0x2108)

/* See configdb bunit SB addr map */
#define BUNIT_REG_BISOC				0x11

#define PUNIT_REG_DSPFREQ			0x36
#define   DSPFREQSTAT_SHIFT_CHV			24
#define   DSPFREQSTAT_MASK_CHV			(0x1f << DSPFREQSTAT_SHIFT_CHV)
#define   DSPFREQGUAR_SHIFT_CHV			8
#define   DSPFREQGUAR_MASK_CHV			(0x1f << DSPFREQGUAR_SHIFT_CHV)
#define   DSPFREQSTAT_SHIFT			30
#define   DSPFREQSTAT_MASK			(0x3 << DSPFREQSTAT_SHIFT)
#define   DSPFREQGUAR_SHIFT			14
#define   DSPFREQGUAR_MASK			(0x3 << DSPFREQGUAR_SHIFT)
#define   _DP_SSC(val, pipe)			((val) << (2 * (pipe)))
#define   DP_SSC_MASK(pipe)			_DP_SSC(0x3, (pipe))
#define   DP_SSC_PWR_ON(pipe)			_DP_SSC(0x0, (pipe))
#define   DP_SSC_CLK_GATE(pipe)			_DP_SSC(0x1, (pipe))
#define   DP_SSC_RESET(pipe)			_DP_SSC(0x2, (pipe))
#define   DP_SSC_PWR_GATE(pipe)			_DP_SSC(0x3, (pipe))
#define   _DP_SSS(val, pipe)			((val) << (2 * (pipe) + 16))
#define   DP_SSS_MASK(pipe)			_DP_SSS(0x3, (pipe))
#define   DP_SSS_PWR_ON(pipe)			_DP_SSS(0x0, (pipe))
#define   DP_SSS_CLK_GATE(pipe)			_DP_SSS(0x1, (pipe))
#define   DP_SSS_RESET(pipe)			_DP_SSS(0x2, (pipe))
#define   DP_SSS_PWR_GATE(pipe)			_DP_SSS(0x3, (pipe))

/* See the PUNIT HAS v0.8 for the below bits */
enum punit_power_well {
	PUNIT_POWER_WELL_RENDER			= 0,
	PUNIT_POWER_WELL_MEDIA			= 1,
	PUNIT_POWER_WELL_DISP2D			= 3,
	PUNIT_POWER_WELL_DPIO_CMN_BC		= 5,
	PUNIT_POWER_WELL_DPIO_TX_B_LANES_01	= 6,
	PUNIT_POWER_WELL_DPIO_TX_B_LANES_23	= 7,
	PUNIT_POWER_WELL_DPIO_TX_C_LANES_01	= 8,
	PUNIT_POWER_WELL_DPIO_TX_C_LANES_23	= 9,
	PUNIT_POWER_WELL_DPIO_RX0		= 10,
	PUNIT_POWER_WELL_DPIO_RX1		= 11,
	PUNIT_POWER_WELL_DPIO_CMN_D		= 12,
	/* FIXME: guesswork below */
	PUNIT_POWER_WELL_DPIO_TX_D_LANES_01	= 13,
	PUNIT_POWER_WELL_DPIO_TX_D_LANES_23	= 14,
	PUNIT_POWER_WELL_DPIO_RX2		= 15,

	PUNIT_POWER_WELL_NUM,
};

#define PUNIT_REG_PWRGT_CTRL			0x60
#define PUNIT_REG_PWRGT_STATUS			0x61
#define   PUNIT_PWRGT_MASK(power_well)		(3 << ((power_well) * 2))
#define   PUNIT_PWRGT_PWR_ON(power_well)	(0 << ((power_well) * 2))
#define   PUNIT_PWRGT_CLK_GATE(power_well)	(1 << ((power_well) * 2))
#define   PUNIT_PWRGT_RESET(power_well)		(2 << ((power_well) * 2))
#define   PUNIT_PWRGT_PWR_GATE(power_well)	(3 << ((power_well) * 2))

#define PUNIT_REG_GPU_LFM			0xd3
#define PUNIT_REG_GPU_FREQ_REQ			0xd4
#define PUNIT_REG_GPU_FREQ_STS			0xd8
#define   GPLLENABLE				(1<<4)
#define   GENFREQSTATUS				(1<<0)
#define PUNIT_REG_MEDIA_TURBO_FREQ_REQ		0xdc
#define PUNIT_REG_CZ_TIMESTAMP			0xce

#define PUNIT_FUSE_BUS2				0xf6 /* bits 47:40 */
#define PUNIT_FUSE_BUS1				0xf5 /* bits 55:48 */

#define PUNIT_GPU_STATUS_REG			0xdb
#define PUNIT_GPU_STATUS_MAX_FREQ_SHIFT	16
#define PUNIT_GPU_STATUS_MAX_FREQ_MASK		0xff
#define PUNIT_GPU_STATIS_GFX_MIN_FREQ_SHIFT	8
#define PUNIT_GPU_STATUS_GFX_MIN_FREQ_MASK	0xff

#define PUNIT_GPU_DUTYCYCLE_REG		0xdf
#define PUNIT_GPU_DUTYCYCLE_RPE_FREQ_SHIFT	8
#define PUNIT_GPU_DUTYCYCLE_RPE_FREQ_MASK	0xff

#define IOSF_NC_FB_GFX_FREQ_FUSE		0x1c
#define   FB_GFX_MAX_FREQ_FUSE_SHIFT		3
#define   FB_GFX_MAX_FREQ_FUSE_MASK		0x000007f8
#define   FB_GFX_FGUARANTEED_FREQ_FUSE_SHIFT	11
#define   FB_GFX_FGUARANTEED_FREQ_FUSE_MASK	0x0007f800
#define IOSF_NC_FB_GFX_FMAX_FUSE_HI		0x34
#define   FB_FMAX_VMIN_FREQ_HI_MASK		0x00000007
#define IOSF_NC_FB_GFX_FMAX_FUSE_LO		0x30
#define   FB_FMAX_VMIN_FREQ_LO_SHIFT		27
#define   FB_FMAX_VMIN_FREQ_LO_MASK		0xf8000000

#define VLV_CZ_CLOCK_TO_MILLI_SEC		100000
#define VLV_RP_UP_EI_THRESHOLD			90
#define VLV_RP_DOWN_EI_THRESHOLD		70
#define VLV_INT_COUNT_FOR_DOWN_EI		5

/* vlv2 north clock has */
#define CCK_FUSE_REG				0x8
#define  CCK_FUSE_HPLL_FREQ_MASK		0x3
#define CCK_REG_DSI_PLL_FUSE			0x44
#define CCK_REG_DSI_PLL_CONTROL			0x48
#define  DSI_PLL_VCO_EN				(1 << 31)
#define  DSI_PLL_LDO_GATE			(1 << 30)
#define  DSI_PLL_P1_POST_DIV_SHIFT		17
#define  DSI_PLL_P1_POST_DIV_MASK		(0x1ff << 17)
#define  DSI_PLL_P2_MUX_DSI0_DIV2		(1 << 13)
#define  DSI_PLL_P3_MUX_DSI1_DIV2		(1 << 12)
#define  DSI_PLL_MUX_MASK			(3 << 9)
#define  DSI_PLL_MUX_DSI0_DSIPLL		(0 << 10)
#define  DSI_PLL_MUX_DSI0_CCK			(1 << 10)
#define  DSI_PLL_MUX_DSI1_DSIPLL		(0 << 9)
#define  DSI_PLL_MUX_DSI1_CCK			(1 << 9)
#define  DSI_PLL_CLK_GATE_MASK			(0xf << 5)
#define  DSI_PLL_CLK_GATE_DSI0_DSIPLL		(1 << 8)
#define  DSI_PLL_CLK_GATE_DSI1_DSIPLL		(1 << 7)
#define  DSI_PLL_CLK_GATE_DSI0_CCK		(1 << 6)
#define  DSI_PLL_CLK_GATE_DSI1_CCK		(1 << 5)
#define  DSI_PLL_LOCK				(1 << 0)
#define CCK_REG_DSI_PLL_DIVIDER			0x4c
#define  DSI_PLL_LFSR				(1 << 31)
#define  DSI_PLL_FRACTION_EN			(1 << 30)
#define  DSI_PLL_FRAC_COUNTER_SHIFT		27
#define  DSI_PLL_FRAC_COUNTER_MASK		(7 << 27)
#define  DSI_PLL_USYNC_CNT_SHIFT		18
#define  DSI_PLL_USYNC_CNT_MASK			(0x1ff << 18)
#define  DSI_PLL_N1_DIV_SHIFT			16
#define  DSI_PLL_N1_DIV_MASK			(3 << 16)
#define  DSI_PLL_M1_DIV_SHIFT			0
#define  DSI_PLL_M1_DIV_MASK			(0x1ff << 0)
#define CCK_DISPLAY_CLOCK_CONTROL		0x6b
#define  DISPLAY_TRUNK_FORCE_ON			(1 << 17)
#define  DISPLAY_TRUNK_FORCE_OFF		(1 << 16)
#define  DISPLAY_FREQUENCY_STATUS		(0x1f << 8)
#define  DISPLAY_FREQUENCY_STATUS_SHIFT		8
#define  DISPLAY_FREQUENCY_VALUES		(0x1f << 0)

/**
 * DOC: DPIO
 *
 * VLV and CHV have slightly peculiar display PHYs for driving DP/HDMI
 * ports. DPIO is the name given to such a display PHY. These PHYs
 * don't follow the standard programming model using direct MMIO
 * registers, and instead their registers must be accessed trough IOSF
 * sideband. VLV has one such PHY for driving ports B and C, and CHV
 * adds another PHY for driving port D. Each PHY responds to specific
 * IOSF-SB port.
 *
 * Each display PHY is made up of one or two channels. Each channel
 * houses a common lane part which contains the PLL and other common
 * logic. CH0 common lane also contains the IOSF-SB logic for the
 * Common Register Interface (CRI) ie. the DPIO registers. CRI clock
 * must be running when any DPIO registers are accessed.
 *
 * In addition to having their own registers, the PHYs are also
 * controlled through some dedicated signals from the display
 * controller. These include PLL reference clock enable, PLL enable,
 * and CRI clock selection, for example.
 *
 * Eeach channel also has two splines (also called data lanes), and
 * each spline is made up of one Physical Access Coding Sub-Layer
 * (PCS) block and two TX lanes. So each channel has two PCS blocks
 * and four TX lanes. The TX lanes are used as DP lanes or TMDS
 * data/clock pairs depending on the output type.
 *
 * Additionally the PHY also contains an AUX lane with AUX blocks
 * for each channel. This is used for DP AUX communication, but
 * this fact isn't really relevant for the driver since AUX is
 * controlled from the display controller side. No DPIO registers
 * need to be accessed during AUX communication,
 *
 * Generally the common lane corresponds to the pipe and
 * the spline (PCS/TX) corresponds to the port.
 *
 * For dual channel PHY (VLV/CHV):
 *
 *  pipe A == CMN/PLL/REF CH0
 *
 *  pipe B == CMN/PLL/REF CH1
 *
 *  port B == PCS/TX CH0
 *
 *  port C == PCS/TX CH1
 *
 * This is especially important when we cross the streams
 * ie. drive port B with pipe B, or port C with pipe A.
 *
 * For single channel PHY (CHV):
 *
 *  pipe C == CMN/PLL/REF CH0
 *
 *  port D == PCS/TX CH0
 *
 * Note: digital port B is DDI0, digital port C is DDI1,
 * digital port D is DDI2
 */
/*
 * Dual channel PHY (VLV/CHV)
 * ---------------------------------
 * |      CH0      |      CH1      |
 * |  CMN/PLL/REF  |  CMN/PLL/REF  |
 * |---------------|---------------| Display PHY
 * | PCS01 | PCS23 | PCS01 | PCS23 |
 * |-------|-------|-------|-------|
 * |TX0|TX1|TX2|TX3|TX0|TX1|TX2|TX3|
 * ---------------------------------
 * |     DDI0      |     DDI1      | DP/HDMI ports
 * ---------------------------------
 *
 * Single channel PHY (CHV)
 * -----------------
 * |      CH0      |
 * |  CMN/PLL/REF  |
 * |---------------| Display PHY
 * | PCS01 | PCS23 |
 * |-------|-------|
 * |TX0|TX1|TX2|TX3|
 * -----------------
 * |     DDI2      | DP/HDMI port
 * -----------------
 */
#define DPIO_DEVFN			0

#define DPIO_CTL			(VLV_DISPLAY_BASE + 0x2110)
#define  DPIO_MODSEL1			(1<<3) /* if ref clk b == 27 */
#define  DPIO_MODSEL0			(1<<2) /* if ref clk a == 27 */
#define  DPIO_SFR_BYPASS		(1<<1)
#define  DPIO_CMNRST			(1<<0)

#define DPIO_PHY(pipe)			((pipe) >> 1)
#define DPIO_PHY_IOSF_PORT(phy)		(dev_priv->dpio_phy_iosf_port[phy])

/*
 * Per pipe/PLL DPIO regs
 */
#define _VLV_PLL_DW3_CH0		0x800c
#define   DPIO_POST_DIV_SHIFT		(28) /* 3 bits */
#define   DPIO_POST_DIV_DAC		0
#define   DPIO_POST_DIV_HDMIDP		1 /* DAC 225-400M rate */
#define   DPIO_POST_DIV_LVDS1		2
#define   DPIO_POST_DIV_LVDS2		3
#define   DPIO_K_SHIFT			(24) /* 4 bits */
#define   DPIO_P1_SHIFT			(21) /* 3 bits */
#define   DPIO_P2_SHIFT			(16) /* 5 bits */
#define   DPIO_N_SHIFT			(12) /* 4 bits */
#define   DPIO_ENABLE_CALIBRATION	(1<<11)
#define   DPIO_M1DIV_SHIFT		(8) /* 3 bits */
#define   DPIO_M2DIV_MASK		0xff
#define _VLV_PLL_DW3_CH1		0x802c
#define VLV_PLL_DW3(ch) _PIPE(ch, _VLV_PLL_DW3_CH0, _VLV_PLL_DW3_CH1)

#define _VLV_PLL_DW5_CH0		0x8014
#define   DPIO_REFSEL_OVERRIDE		27
#define   DPIO_PLL_MODESEL_SHIFT	24 /* 3 bits */
#define   DPIO_BIAS_CURRENT_CTL_SHIFT	21 /* 3 bits, always 0x7 */
#define   DPIO_PLL_REFCLK_SEL_SHIFT	16 /* 2 bits */
#define   DPIO_PLL_REFCLK_SEL_MASK	3
#define   DPIO_DRIVER_CTL_SHIFT		12 /* always set to 0x8 */
#define   DPIO_CLK_BIAS_CTL_SHIFT	8 /* always set to 0x5 */
#define _VLV_PLL_DW5_CH1		0x8034
#define VLV_PLL_DW5(ch) _PIPE(ch, _VLV_PLL_DW5_CH0, _VLV_PLL_DW5_CH1)

#define _VLV_PLL_DW7_CH0		0x801c
#define _VLV_PLL_DW7_CH1		0x803c
#define VLV_PLL_DW7(ch) _PIPE(ch, _VLV_PLL_DW7_CH0, _VLV_PLL_DW7_CH1)

#define _VLV_PLL_DW8_CH0		0x8040
#define _VLV_PLL_DW8_CH1		0x8060
#define VLV_PLL_DW8(ch) _PIPE(ch, _VLV_PLL_DW8_CH0, _VLV_PLL_DW8_CH1)

#define VLV_PLL_DW9_BCAST		0xc044
#define _VLV_PLL_DW9_CH0		0x8044
#define _VLV_PLL_DW9_CH1		0x8064
#define VLV_PLL_DW9(ch) _PIPE(ch, _VLV_PLL_DW9_CH0, _VLV_PLL_DW9_CH1)

#define _VLV_PLL_DW10_CH0		0x8048
#define _VLV_PLL_DW10_CH1		0x8068
#define VLV_PLL_DW10(ch) _PIPE(ch, _VLV_PLL_DW10_CH0, _VLV_PLL_DW10_CH1)

#define _VLV_PLL_DW11_CH0		0x804c
#define _VLV_PLL_DW11_CH1		0x806c
#define VLV_PLL_DW11(ch) _PIPE(ch, _VLV_PLL_DW11_CH0, _VLV_PLL_DW11_CH1)

/* Spec for ref block start counts at DW10 */
#define VLV_REF_DW13			0x80ac

#define VLV_CMN_DW0			0x8100

/*
 * Per DDI channel DPIO regs
 */

#define _VLV_PCS_DW0_CH0		0x8200
#define _VLV_PCS_DW0_CH1		0x8400
#define   DPIO_PCS_TX_LANE2_RESET	(1<<16)
#define   DPIO_PCS_TX_LANE1_RESET	(1<<7)
#define   DPIO_LEFT_TXFIFO_RST_MASTER2	(1<<4)
#define   DPIO_RIGHT_TXFIFO_RST_MASTER2	(1<<3)
#define VLV_PCS_DW0(ch) _PORT(ch, _VLV_PCS_DW0_CH0, _VLV_PCS_DW0_CH1)

#define _VLV_PCS01_DW0_CH0		0x200
#define _VLV_PCS23_DW0_CH0		0x400
#define _VLV_PCS01_DW0_CH1		0x2600
#define _VLV_PCS23_DW0_CH1		0x2800
#define VLV_PCS01_DW0(ch) _PORT(ch, _VLV_PCS01_DW0_CH0, _VLV_PCS01_DW0_CH1)
#define VLV_PCS23_DW0(ch) _PORT(ch, _VLV_PCS23_DW0_CH0, _VLV_PCS23_DW0_CH1)

#define _VLV_PCS_DW1_CH0		0x8204
#define _VLV_PCS_DW1_CH1		0x8404
#define   CHV_PCS_REQ_SOFTRESET_EN	(1<<23)
#define   DPIO_PCS_CLK_CRI_RXEB_EIOS_EN	(1<<22)
#define   DPIO_PCS_CLK_CRI_RXDIGFILTSG_EN (1<<21)
#define   DPIO_PCS_CLK_DATAWIDTH_SHIFT	(6)
#define   DPIO_PCS_CLK_SOFT_RESET	(1<<5)
#define VLV_PCS_DW1(ch) _PORT(ch, _VLV_PCS_DW1_CH0, _VLV_PCS_DW1_CH1)

#define _VLV_PCS01_DW1_CH0		0x204
#define _VLV_PCS23_DW1_CH0		0x404
#define _VLV_PCS01_DW1_CH1		0x2604
#define _VLV_PCS23_DW1_CH1		0x2804
#define VLV_PCS01_DW1(ch) _PORT(ch, _VLV_PCS01_DW1_CH0, _VLV_PCS01_DW1_CH1)
#define VLV_PCS23_DW1(ch) _PORT(ch, _VLV_PCS23_DW1_CH0, _VLV_PCS23_DW1_CH1)

#define _VLV_PCS_DW8_CH0		0x8220
#define _VLV_PCS_DW8_CH1		0x8420
#define   CHV_PCS_USEDCLKCHANNEL_OVRRIDE	(1 << 20)
#define   CHV_PCS_USEDCLKCHANNEL		(1 << 21)
#define VLV_PCS_DW8(ch) _PORT(ch, _VLV_PCS_DW8_CH0, _VLV_PCS_DW8_CH1)

#define _VLV_PCS01_DW8_CH0		0x0220
#define _VLV_PCS23_DW8_CH0		0x0420
#define _VLV_PCS01_DW8_CH1		0x2620
#define _VLV_PCS23_DW8_CH1		0x2820
#define VLV_PCS01_DW8(port) _PORT(port, _VLV_PCS01_DW8_CH0, _VLV_PCS01_DW8_CH1)
#define VLV_PCS23_DW8(port) _PORT(port, _VLV_PCS23_DW8_CH0, _VLV_PCS23_DW8_CH1)

#define _VLV_PCS_DW9_CH0		0x8224
#define _VLV_PCS_DW9_CH1		0x8424
#define   DPIO_PCS_TX2MARGIN_MASK	(0x7<<13)
#define   DPIO_PCS_TX2MARGIN_000	(0<<13)
#define   DPIO_PCS_TX2MARGIN_101	(1<<13)
#define   DPIO_PCS_TX1MARGIN_MASK	(0x7<<10)
#define   DPIO_PCS_TX1MARGIN_000	(0<<10)
#define   DPIO_PCS_TX1MARGIN_101	(1<<10)
#define	VLV_PCS_DW9(ch) _PORT(ch, _VLV_PCS_DW9_CH0, _VLV_PCS_DW9_CH1)

#define _VLV_PCS01_DW9_CH0		0x224
#define _VLV_PCS23_DW9_CH0		0x424
#define _VLV_PCS01_DW9_CH1		0x2624
#define _VLV_PCS23_DW9_CH1		0x2824
#define VLV_PCS01_DW9(ch) _PORT(ch, _VLV_PCS01_DW9_CH0, _VLV_PCS01_DW9_CH1)
#define VLV_PCS23_DW9(ch) _PORT(ch, _VLV_PCS23_DW9_CH0, _VLV_PCS23_DW9_CH1)

#define _CHV_PCS_DW10_CH0		0x8228
#define _CHV_PCS_DW10_CH1		0x8428
#define   DPIO_PCS_SWING_CALC_TX0_TX2	(1<<30)
#define   DPIO_PCS_SWING_CALC_TX1_TX3	(1<<31)
#define   DPIO_PCS_TX2DEEMP_MASK	(0xf<<24)
#define   DPIO_PCS_TX2DEEMP_9P5		(0<<24)
#define   DPIO_PCS_TX2DEEMP_6P0		(2<<24)
#define   DPIO_PCS_TX1DEEMP_MASK	(0xf<<16)
#define   DPIO_PCS_TX1DEEMP_9P5		(0<<16)
#define   DPIO_PCS_TX1DEEMP_6P0		(2<<16)
#define CHV_PCS_DW10(ch) _PORT(ch, _CHV_PCS_DW10_CH0, _CHV_PCS_DW10_CH1)

#define _VLV_PCS01_DW10_CH0		0x0228
#define _VLV_PCS23_DW10_CH0		0x0428
#define _VLV_PCS01_DW10_CH1		0x2628
#define _VLV_PCS23_DW10_CH1		0x2828
#define VLV_PCS01_DW10(port) _PORT(port, _VLV_PCS01_DW10_CH0, _VLV_PCS01_DW10_CH1)
#define VLV_PCS23_DW10(port) _PORT(port, _VLV_PCS23_DW10_CH0, _VLV_PCS23_DW10_CH1)

#define _VLV_PCS_DW11_CH0		0x822c
#define _VLV_PCS_DW11_CH1		0x842c
#define   DPIO_LANEDESKEW_STRAP_OVRD	(1<<3)
#define   DPIO_LEFT_TXFIFO_RST_MASTER	(1<<1)
#define   DPIO_RIGHT_TXFIFO_RST_MASTER	(1<<0)
#define VLV_PCS_DW11(ch) _PORT(ch, _VLV_PCS_DW11_CH0, _VLV_PCS_DW11_CH1)

#define _VLV_PCS01_DW11_CH0		0x022c
#define _VLV_PCS23_DW11_CH0		0x042c
#define _VLV_PCS01_DW11_CH1		0x262c
#define _VLV_PCS23_DW11_CH1		0x282c
#define VLV_PCS01_DW11(ch) _PORT(ch, _VLV_PCS01_DW11_CH0, _VLV_PCS01_DW11_CH1)
#define VLV_PCS23_DW11(ch) _PORT(ch, _VLV_PCS23_DW11_CH0, _VLV_PCS23_DW11_CH1)

#define _VLV_PCS_DW12_CH0		0x8230
#define _VLV_PCS_DW12_CH1		0x8430
#define VLV_PCS_DW12(ch) _PORT(ch, _VLV_PCS_DW12_CH0, _VLV_PCS_DW12_CH1)

#define _VLV_PCS_DW14_CH0		0x8238
#define _VLV_PCS_DW14_CH1		0x8438
#define	VLV_PCS_DW14(ch) _PORT(ch, _VLV_PCS_DW14_CH0, _VLV_PCS_DW14_CH1)

#define _VLV_PCS_DW23_CH0		0x825c
#define _VLV_PCS_DW23_CH1		0x845c
#define VLV_PCS_DW23(ch) _PORT(ch, _VLV_PCS_DW23_CH0, _VLV_PCS_DW23_CH1)

#define _VLV_TX_DW2_CH0			0x8288
#define _VLV_TX_DW2_CH1			0x8488
#define   DPIO_SWING_MARGIN000_SHIFT	16
#define   DPIO_SWING_MARGIN000_MASK	(0xff << DPIO_SWING_MARGIN000_SHIFT)
#define   DPIO_UNIQ_TRANS_SCALE_SHIFT	8
#define VLV_TX_DW2(ch) _PORT(ch, _VLV_TX_DW2_CH0, _VLV_TX_DW2_CH1)

#define _VLV_TX_DW3_CH0			0x828c
#define _VLV_TX_DW3_CH1			0x848c
/* The following bit for CHV phy */
#define   DPIO_TX_UNIQ_TRANS_SCALE_EN	(1<<27)
#define   DPIO_SWING_MARGIN101_SHIFT	16
#define   DPIO_SWING_MARGIN101_MASK	(0xff << DPIO_SWING_MARGIN101_SHIFT)
#define VLV_TX_DW3(ch) _PORT(ch, _VLV_TX_DW3_CH0, _VLV_TX_DW3_CH1)

#define _VLV_TX_DW4_CH0			0x8290
#define _VLV_TX_DW4_CH1			0x8490
#define   DPIO_SWING_DEEMPH9P5_SHIFT	24
#define   DPIO_SWING_DEEMPH9P5_MASK	(0xff << DPIO_SWING_DEEMPH9P5_SHIFT)
#define   DPIO_SWING_DEEMPH6P0_SHIFT	16
#define   DPIO_SWING_DEEMPH6P0_MASK	(0xff << DPIO_SWING_DEEMPH6P0_SHIFT)
#define VLV_TX_DW4(ch) _PORT(ch, _VLV_TX_DW4_CH0, _VLV_TX_DW4_CH1)

#define _VLV_TX3_DW4_CH0		0x690
#define _VLV_TX3_DW4_CH1		0x2a90
#define VLV_TX3_DW4(ch) _PORT(ch, _VLV_TX3_DW4_CH0, _VLV_TX3_DW4_CH1)

#define _VLV_TX_DW5_CH0			0x8294
#define _VLV_TX_DW5_CH1			0x8494
#define   DPIO_TX_OCALINIT_EN		(1<<31)
#define VLV_TX_DW5(ch) _PORT(ch, _VLV_TX_DW5_CH0, _VLV_TX_DW5_CH1)

#define _VLV_TX_DW11_CH0		0x82ac
#define _VLV_TX_DW11_CH1		0x84ac
#define VLV_TX_DW11(ch) _PORT(ch, _VLV_TX_DW11_CH0, _VLV_TX_DW11_CH1)

#define _VLV_TX_DW14_CH0		0x82b8
#define _VLV_TX_DW14_CH1		0x84b8
#define VLV_TX_DW14(ch) _PORT(ch, _VLV_TX_DW14_CH0, _VLV_TX_DW14_CH1)

/* CHV dpPhy registers */
#define _CHV_PLL_DW0_CH0		0x8000
#define _CHV_PLL_DW0_CH1		0x8180
#define CHV_PLL_DW0(ch) _PIPE(ch, _CHV_PLL_DW0_CH0, _CHV_PLL_DW0_CH1)

#define _CHV_PLL_DW1_CH0		0x8004
#define _CHV_PLL_DW1_CH1		0x8184
#define   DPIO_CHV_N_DIV_SHIFT		8
#define   DPIO_CHV_M1_DIV_BY_2		(0 << 0)
#define CHV_PLL_DW1(ch) _PIPE(ch, _CHV_PLL_DW1_CH0, _CHV_PLL_DW1_CH1)

#define _CHV_PLL_DW2_CH0		0x8008
#define _CHV_PLL_DW2_CH1		0x8188
#define CHV_PLL_DW2(ch) _PIPE(ch, _CHV_PLL_DW2_CH0, _CHV_PLL_DW2_CH1)

#define _CHV_PLL_DW3_CH0		0x800c
#define _CHV_PLL_DW3_CH1		0x818c
#define  DPIO_CHV_FRAC_DIV_EN		(1 << 16)
#define  DPIO_CHV_FIRST_MOD		(0 << 8)
#define  DPIO_CHV_SECOND_MOD		(1 << 8)
#define  DPIO_CHV_FEEDFWD_GAIN_SHIFT	0
#define CHV_PLL_DW3(ch) _PIPE(ch, _CHV_PLL_DW3_CH0, _CHV_PLL_DW3_CH1)

#define _CHV_PLL_DW6_CH0		0x8018
#define _CHV_PLL_DW6_CH1		0x8198
#define   DPIO_CHV_GAIN_CTRL_SHIFT	16
#define	  DPIO_CHV_INT_COEFF_SHIFT	8
#define   DPIO_CHV_PROP_COEFF_SHIFT	0
#define CHV_PLL_DW6(ch) _PIPE(ch, _CHV_PLL_DW6_CH0, _CHV_PLL_DW6_CH1)

#define _CHV_CMN_DW5_CH0               0x8114
#define   CHV_BUFRIGHTENA1_DISABLE	(0 << 20)
#define   CHV_BUFRIGHTENA1_NORMAL	(1 << 20)
#define   CHV_BUFRIGHTENA1_FORCE	(3 << 20)
#define   CHV_BUFRIGHTENA1_MASK		(3 << 20)
#define   CHV_BUFLEFTENA1_DISABLE	(0 << 22)
#define   CHV_BUFLEFTENA1_NORMAL	(1 << 22)
#define   CHV_BUFLEFTENA1_FORCE		(3 << 22)
#define   CHV_BUFLEFTENA1_MASK		(3 << 22)

#define _CHV_CMN_DW13_CH0		0x8134
#define _CHV_CMN_DW0_CH1		0x8080
#define   DPIO_CHV_S1_DIV_SHIFT		21
#define   DPIO_CHV_P1_DIV_SHIFT		13 /* 3 bits */
#define   DPIO_CHV_P2_DIV_SHIFT		8  /* 5 bits */
#define   DPIO_CHV_K_DIV_SHIFT		4
#define   DPIO_PLL_FREQLOCK		(1 << 1)
#define   DPIO_PLL_LOCK			(1 << 0)
#define CHV_CMN_DW13(ch) _PIPE(ch, _CHV_CMN_DW13_CH0, _CHV_CMN_DW0_CH1)

#define _CHV_CMN_DW14_CH0		0x8138
#define _CHV_CMN_DW1_CH1		0x8084
#define   DPIO_AFC_RECAL		(1 << 14)
#define   DPIO_DCLKP_EN			(1 << 13)
#define   CHV_BUFLEFTENA2_DISABLE	(0 << 17) /* CL2 DW1 only */
#define   CHV_BUFLEFTENA2_NORMAL	(1 << 17) /* CL2 DW1 only */
#define   CHV_BUFLEFTENA2_FORCE		(3 << 17) /* CL2 DW1 only */
#define   CHV_BUFLEFTENA2_MASK		(3 << 17) /* CL2 DW1 only */
#define   CHV_BUFRIGHTENA2_DISABLE	(0 << 19) /* CL2 DW1 only */
#define   CHV_BUFRIGHTENA2_NORMAL	(1 << 19) /* CL2 DW1 only */
#define   CHV_BUFRIGHTENA2_FORCE	(3 << 19) /* CL2 DW1 only */
#define   CHV_BUFRIGHTENA2_MASK		(3 << 19) /* CL2 DW1 only */
#define CHV_CMN_DW14(ch) _PIPE(ch, _CHV_CMN_DW14_CH0, _CHV_CMN_DW1_CH1)

#define _CHV_CMN_DW19_CH0		0x814c
#define _CHV_CMN_DW6_CH1		0x8098
#define   CHV_CMN_USEDCLKCHANNEL	(1 << 13)
#define CHV_CMN_DW19(ch) _PIPE(ch, _CHV_CMN_DW19_CH0, _CHV_CMN_DW6_CH1)

#define CHV_CMN_DW30			0x8178
#define   DPIO_LRC_BYPASS		(1 << 3)

#define _TXLANE(ch, lane, offset) ((ch ? 0x2400 : 0) + \
					(lane) * 0x200 + (offset))

#define CHV_TX_DW0(ch, lane) _TXLANE(ch, lane, 0x80)
#define CHV_TX_DW1(ch, lane) _TXLANE(ch, lane, 0x84)
#define CHV_TX_DW2(ch, lane) _TXLANE(ch, lane, 0x88)
#define CHV_TX_DW3(ch, lane) _TXLANE(ch, lane, 0x8c)
#define CHV_TX_DW4(ch, lane) _TXLANE(ch, lane, 0x90)
#define CHV_TX_DW5(ch, lane) _TXLANE(ch, lane, 0x94)
#define CHV_TX_DW6(ch, lane) _TXLANE(ch, lane, 0x98)
#define CHV_TX_DW7(ch, lane) _TXLANE(ch, lane, 0x9c)
#define CHV_TX_DW8(ch, lane) _TXLANE(ch, lane, 0xa0)
#define CHV_TX_DW9(ch, lane) _TXLANE(ch, lane, 0xa4)
#define CHV_TX_DW10(ch, lane) _TXLANE(ch, lane, 0xa8)
#define CHV_TX_DW11(ch, lane) _TXLANE(ch, lane, 0xac)
#define   DPIO_FRC_LATENCY_SHFIT	8
#define CHV_TX_DW14(ch, lane) _TXLANE(ch, lane, 0xb8)
#define   DPIO_UPAR_SHIFT		30
/*
 * Fence registers
 */
#define FENCE_REG_830_0			0x2000
#define FENCE_REG_945_8			0x3000
#define   I830_FENCE_START_MASK		0x07f80000
#define   I830_FENCE_TILING_Y_SHIFT	12
#define   I830_FENCE_SIZE_BITS(size)	((ffs((size) >> 19) - 1) << 8)
#define   I830_FENCE_PITCH_SHIFT	4
#define   I830_FENCE_REG_VALID		(1<<0)
#define   I915_FENCE_MAX_PITCH_VAL	4
#define   I830_FENCE_MAX_PITCH_VAL	6
#define   I830_FENCE_MAX_SIZE_VAL	(1<<8)

#define   I915_FENCE_START_MASK		0x0ff00000
#define   I915_FENCE_SIZE_BITS(size)	((ffs((size) >> 20) - 1) << 8)

#define FENCE_REG_965_0			0x03000
#define   I965_FENCE_PITCH_SHIFT	2
#define   I965_FENCE_TILING_Y_SHIFT	1
#define   I965_FENCE_REG_VALID		(1<<0)
#define   I965_FENCE_MAX_PITCH_VAL	0x0400

#define FENCE_REG_SANDYBRIDGE_0		0x100000
#define   SANDYBRIDGE_FENCE_PITCH_SHIFT	32
#define   GEN7_FENCE_MAX_PITCH_VAL	0x0800


/* control register for cpu gtt access */
#define TILECTL				0x101000
#define   TILECTL_SWZCTL			(1 << 0)
#define   TILECTL_TLB_PREFETCH_DIS	(1 << 2)
#define   TILECTL_BACKSNOOP_DIS		(1 << 3)

/*
 * Instruction and interrupt control regs
 */
#define PGTBL_CTL	0x02020
#define   PGTBL_ADDRESS_LO_MASK	0xfffff000 /* bits [31:12] */
#define   PGTBL_ADDRESS_HI_MASK	0x000000f0 /* bits [35:32] (gen4) */
#define PGTBL_ER	0x02024
#define PRB0_BASE (0x2030-0x30)
#define PRB1_BASE (0x2040-0x30) /* 830,gen3 */
#define PRB2_BASE (0x2050-0x30) /* gen3 */
#define SRB0_BASE (0x2100-0x30) /* gen2 */
#define SRB1_BASE (0x2110-0x30) /* gen2 */
#define SRB2_BASE (0x2120-0x30) /* 830 */
#define SRB3_BASE (0x2130-0x30) /* 830 */
#define RENDER_RING_BASE	0x02000
#define BSD_RING_BASE		0x04000
#define GEN6_BSD_RING_BASE	0x12000
#define GEN8_BSD2_RING_BASE	0x1c000
#define VEBOX_RING_BASE		0x1a000
#define BLT_RING_BASE		0x22000
#define RING_TAIL(base)		((base)+0x30)
#define RING_HEAD(base)		((base)+0x34)
#define RING_START(base)	((base)+0x38)
#define RING_CTL(base)		((base)+0x3c)
#define RING_SYNC_0(base)	((base)+0x40)
#define RING_SYNC_1(base)	((base)+0x44)
#define RING_SYNC_2(base)	((base)+0x48)
#define GEN6_RVSYNC	(RING_SYNC_0(RENDER_RING_BASE))
#define GEN6_RBSYNC	(RING_SYNC_1(RENDER_RING_BASE))
#define GEN6_RVESYNC	(RING_SYNC_2(RENDER_RING_BASE))
#define GEN6_VBSYNC	(RING_SYNC_0(GEN6_BSD_RING_BASE))
#define GEN6_VRSYNC	(RING_SYNC_1(GEN6_BSD_RING_BASE))
#define GEN6_VVESYNC	(RING_SYNC_2(GEN6_BSD_RING_BASE))
#define GEN6_BRSYNC	(RING_SYNC_0(BLT_RING_BASE))
#define GEN6_BVSYNC	(RING_SYNC_1(BLT_RING_BASE))
#define GEN6_BVESYNC	(RING_SYNC_2(BLT_RING_BASE))
#define GEN6_VEBSYNC	(RING_SYNC_0(VEBOX_RING_BASE))
#define GEN6_VERSYNC	(RING_SYNC_1(VEBOX_RING_BASE))
#define GEN6_VEVSYNC	(RING_SYNC_2(VEBOX_RING_BASE))
#define GEN6_NOSYNC 0
#define RING_MAX_IDLE(base)	((base)+0x54)
#define RING_HWS_PGA(base)	((base)+0x80)
#define RING_HWS_PGA_GEN6(base)	((base)+0x2080)

#define GEN7_WR_WATERMARK	0x4028
#define GEN7_GFX_PRIO_CTRL	0x402C
#define ARB_MODE		0x4030
#define   ARB_MODE_SWIZZLE_SNB	(1<<4)
#define   ARB_MODE_SWIZZLE_IVB	(1<<5)
#define GEN7_GFX_PEND_TLB0	0x4034
#define GEN7_GFX_PEND_TLB1	0x4038
/* L3, CVS, ZTLB, RCC, CASC LRA min, max values */
#define GEN7_LRA_LIMITS_BASE	0x403C
#define GEN7_LRA_LIMITS_REG_NUM	13
#define GEN7_MEDIA_MAX_REQ_COUNT	0x4070
#define GEN7_GFX_MAX_REQ_COUNT		0x4074

#define GAMTARBMODE		0x04a08
#define   ARB_MODE_BWGTLB_DISABLE (1<<9)
#define   ARB_MODE_SWIZZLE_BDW	(1<<1)
#define RENDER_HWS_PGA_GEN7	(0x04080)
#define RING_FAULT_REG(ring)	(0x4094 + 0x100*(ring)->id)
#define   RING_FAULT_GTTSEL_MASK (1<<11)
#define   RING_FAULT_SRCID(x)	((x >> 3) & 0xff)
#define   RING_FAULT_FAULT_TYPE(x) ((x >> 1) & 0x3)
#define   RING_FAULT_VALID	(1<<0)
#define DONE_REG		0x40b0
#define GEN8_PRIVATE_PAT	0x40e0
#define BSD_HWS_PGA_GEN7	(0x04180)
#define BLT_HWS_PGA_GEN7	(0x04280)
#define VEBOX_HWS_PGA_GEN7	(0x04380)
#define RING_ACTHD(base)	((base)+0x74)
#define RING_ACTHD_UDW(base)	((base)+0x5c)
#define RING_NOPID(base)	((base)+0x94)
#define RING_IMR(base)		((base)+0xa8)
#define RING_HWSTAM(base)	((base)+0x98)
#define RING_TIMESTAMP(base)	((base)+0x358)
#define   TAIL_ADDR		0x001FFFF8
#define   HEAD_WRAP_COUNT	0xFFE00000
#define   HEAD_WRAP_ONE		0x00200000
#define   HEAD_ADDR		0x001FFFFC
#define   RING_NR_PAGES		0x001FF000
#define   RING_REPORT_MASK	0x00000006
#define   RING_REPORT_64K	0x00000002
#define   RING_REPORT_128K	0x00000004
#define   RING_NO_REPORT	0x00000000
#define   RING_VALID_MASK	0x00000001
#define   RING_VALID		0x00000001
#define   RING_INVALID		0x00000000
#define   RING_WAIT_I8XX	(1<<0) /* gen2, PRBx_HEAD */
#define   RING_WAIT		(1<<11) /* gen3+, PRBx_CTL */
#define   RING_WAIT_SEMAPHORE	(1<<10) /* gen6+ */

#define GEN7_TLB_RD_ADDR	0x4700

#if 0
#define PRB0_TAIL	0x02030
#define PRB0_HEAD	0x02034
#define PRB0_START	0x02038
#define PRB0_CTL	0x0203c
#define PRB1_TAIL	0x02040 /* 915+ only */
#define PRB1_HEAD	0x02044 /* 915+ only */
#define PRB1_START	0x02048 /* 915+ only */
#define PRB1_CTL	0x0204c /* 915+ only */
#endif
#define IPEIR_I965	0x02064
#define IPEHR_I965	0x02068
#define INSTDONE_I965	0x0206c
#define GEN7_INSTDONE_1		0x0206c
#define GEN7_SC_INSTDONE	0x07100
#define GEN7_SAMPLER_INSTDONE	0x0e160
#define GEN7_ROW_INSTDONE	0x0e164
#define I915_NUM_INSTDONE_REG	4
#define RING_IPEIR(base)	((base)+0x64)
#define RING_IPEHR(base)	((base)+0x68)
#define RING_INSTDONE(base)	((base)+0x6c)
#define RING_INSTPS(base)	((base)+0x70)
#define RING_DMA_FADD(base)	((base)+0x78)
#define RING_DMA_FADD_UDW(base)	((base)+0x60) /* gen8+ */
#define RING_INSTPM(base)	((base)+0xc0)
#define RING_MI_MODE(base)	((base)+0x9c)
#define INSTPS		0x02070 /* 965+ only */
#define INSTDONE1	0x0207c /* 965+ only */
#define ACTHD_I965	0x02074
#define HWS_PGA		0x02080
#define HWS_ADDRESS_MASK	0xfffff000
#define HWS_START_ADDRESS_SHIFT	4
#define PWRCTXA		0x2088 /* 965GM+ only */
#define   PWRCTX_EN	(1<<0)
#define IPEIR		0x02088
#define IPEHR		0x0208c
#define INSTDONE	0x02090
#define NOPID		0x02094
#define HWSTAM		0x02098
#define DMA_FADD_I8XX	0x020d0
#define RING_BBSTATE(base)	((base)+0x110)
#define RING_BBADDR(base)	((base)+0x140)
#define RING_BBADDR_UDW(base)	((base)+0x168) /* gen8+ */

#define ERROR_GEN6	0x040a0
#define GEN7_ERR_INT	0x44040
#define   ERR_INT_POISON		(1<<31)
#define   ERR_INT_MMIO_UNCLAIMED	(1<<13)
#define   ERR_INT_PIPE_CRC_DONE_C	(1<<8)
#define   ERR_INT_FIFO_UNDERRUN_C	(1<<6)
#define   ERR_INT_PIPE_CRC_DONE_B	(1<<5)
#define   ERR_INT_FIFO_UNDERRUN_B	(1<<3)
#define   ERR_INT_PIPE_CRC_DONE_A	(1<<2)
#define   ERR_INT_PIPE_CRC_DONE(pipe)	(1<<(2 + pipe*3))
#define   ERR_INT_FIFO_UNDERRUN_A	(1<<0)
#define   ERR_INT_FIFO_UNDERRUN(pipe)	(1<<(pipe*3))

#define FPGA_DBG		0x42300
#define   FPGA_DBG_RM_NOCLAIM	(1<<31)

#define DERRMR		0x44050
/* Note that HBLANK events are reserved on bdw+ */
#define   DERRMR_PIPEA_SCANLINE		(1<<0)
#define   DERRMR_PIPEA_PRI_FLIP_DONE	(1<<1)
#define   DERRMR_PIPEA_SPR_FLIP_DONE	(1<<2)
#define   DERRMR_PIPEA_VBLANK		(1<<3)
#define   DERRMR_PIPEA_HBLANK		(1<<5)
#define   DERRMR_PIPEB_SCANLINE 	(1<<8)
#define   DERRMR_PIPEB_PRI_FLIP_DONE	(1<<9)
#define   DERRMR_PIPEB_SPR_FLIP_DONE	(1<<10)
#define   DERRMR_PIPEB_VBLANK		(1<<11)
#define   DERRMR_PIPEB_HBLANK		(1<<13)
/* Note that PIPEC is not a simple translation of PIPEA/PIPEB */
#define   DERRMR_PIPEC_SCANLINE		(1<<14)
#define   DERRMR_PIPEC_PRI_FLIP_DONE	(1<<15)
#define   DERRMR_PIPEC_SPR_FLIP_DONE	(1<<20)
#define   DERRMR_PIPEC_VBLANK		(1<<21)
#define   DERRMR_PIPEC_HBLANK		(1<<22)


/* GM45+ chicken bits -- debug workaround bits that may be required
 * for various sorts of correct behavior.  The top 16 bits of each are
 * the enables for writing to the corresponding low bit.
 */
#define _3D_CHICKEN	0x02084
#define  _3D_CHICKEN_HIZ_PLANE_DISABLE_MSAA_4X_SNB	(1 << 10)
#define _3D_CHICKEN2	0x0208c
/* Disables pipelining of read flushes past the SF-WIZ interface.
 * Required on all Ironlake steppings according to the B-Spec, but the
 * particular danger of not doing so is not specified.
 */
# define _3D_CHICKEN2_WM_READ_PIPELINED			(1 << 14)
#define _3D_CHICKEN3	0x02090
#define  _3D_CHICKEN_SF_DISABLE_OBJEND_CULL		(1 << 10)
#define  _3D_CHICKEN3_SF_DISABLE_FASTCLIP_CULL		(1 << 5)
#define  _3D_CHICKEN_SDE_LIMIT_FIFO_POLY_DEPTH(x)	((x)<<1) /* gen8+ */
#define  _3D_CHICKEN3_SF_DISABLE_PIPELINED_ATTR_FETCH	(1 << 1) /* gen6 */

#define MI_MODE		0x0209c
# define VS_TIMER_DISPATCH				(1 << 6)
# define MI_FLUSH_ENABLE				(1 << 12)
# define ASYNC_FLIP_PERF_DISABLE			(1 << 14)
# define MODE_IDLE					(1 << 9)
# define STOP_RING					(1 << 8)

#define GEN6_GT_MODE	0x20d0
#define GEN7_GT_MODE	0x7008
#define   GEN6_WIZ_HASHING(hi, lo)			(((hi) << 9) | ((lo) << 7))
#define   GEN6_WIZ_HASHING_8x8				GEN6_WIZ_HASHING(0, 0)
#define   GEN6_WIZ_HASHING_8x4				GEN6_WIZ_HASHING(0, 1)
#define   GEN6_WIZ_HASHING_16x4				GEN6_WIZ_HASHING(1, 0)
#define   GEN6_WIZ_HASHING_MASK				(GEN6_WIZ_HASHING(1, 1) << 16)
#define   GEN6_TD_FOUR_ROW_DISPATCH_DISABLE		(1 << 5)

#define GFX_MODE	0x02520
#define GFX_MODE_GEN7	0x0229c
#define RING_MODE_GEN7(ring)	((ring)->mmio_base+0x29c)
#define   GFX_RUN_LIST_ENABLE		(1<<15)
#define   GFX_TLB_INVALIDATE_EXPLICIT	(1<<13)
#define   GFX_SURFACE_FAULT_ENABLE	(1<<12)
#define   GFX_REPLAY_MODE		(1<<11)
#define   GFX_PSMI_GRANULARITY		(1<<10)
#define   GFX_PPGTT_ENABLE		(1<<9)

#define VLV_DISPLAY_BASE 0x180000
#define VLV_MIPI_BASE VLV_DISPLAY_BASE

#define VLV_GU_CTL0	(VLV_DISPLAY_BASE + 0x2030)
#define VLV_GU_CTL1	(VLV_DISPLAY_BASE + 0x2034)
#define SCPD0		0x0209c /* 915+ only */
#define IER		0x020a0
#define IIR		0x020a4
#define IMR		0x020a8
#define ISR		0x020ac
#define VLV_GUNIT_CLOCK_GATE	(VLV_DISPLAY_BASE + 0x2060)
#define   GINT_DIS		(1<<22)
#define   GCFG_DIS		(1<<8)
#define VLV_GUNIT_CLOCK_GATE2	(VLV_DISPLAY_BASE + 0x2064)
#define VLV_IIR_RW	(VLV_DISPLAY_BASE + 0x2084)
#define VLV_IER		(VLV_DISPLAY_BASE + 0x20a0)
#define VLV_IIR		(VLV_DISPLAY_BASE + 0x20a4)
#define VLV_IMR		(VLV_DISPLAY_BASE + 0x20a8)
#define VLV_ISR		(VLV_DISPLAY_BASE + 0x20ac)
#define VLV_PCBR	(VLV_DISPLAY_BASE + 0x2120)
#define VLV_PCBR_ADDR_SHIFT	12

#define   DISPLAY_PLANE_FLIP_PENDING(plane) (1<<(11-(plane))) /* A and B only */
#define EIR		0x020b0
#define EMR		0x020b4
#define ESR		0x020b8
#define   GM45_ERROR_PAGE_TABLE				(1<<5)
#define   GM45_ERROR_MEM_PRIV				(1<<4)
#define   I915_ERROR_PAGE_TABLE				(1<<4)
#define   GM45_ERROR_CP_PRIV				(1<<3)
#define   I915_ERROR_MEMORY_REFRESH			(1<<1)
#define   I915_ERROR_INSTRUCTION			(1<<0)
#define INSTPM	        0x020c0
#define   INSTPM_SELF_EN (1<<12) /* 915GM only */
#define   INSTPM_AGPBUSY_INT_EN (1<<11) /* gen3: when disabled, pending interrupts
					will not assert AGPBUSY# and will only
					be delivered when out of C3. */
#define   INSTPM_FORCE_ORDERING				(1<<7) /* GEN6+ */
#define   INSTPM_TLB_INVALIDATE	(1<<9)
#define   INSTPM_SYNC_FLUSH	(1<<5)
#define ACTHD	        0x020c8
#define MEM_MODE	0x020cc
#define   MEM_DISPLAY_B_TRICKLE_FEED_DISABLE (1<<3) /* 830 only */
#define   MEM_DISPLAY_A_TRICKLE_FEED_DISABLE (1<<2) /* 830/845 only */
#define   MEM_DISPLAY_TRICKLE_FEED_DISABLE (1<<2) /* 85x only */
#define FW_BLC		0x020d8
#define FW_BLC2		0x020dc
#define FW_BLC_SELF	0x020e0 /* 915+ only */
#define   FW_BLC_SELF_EN_MASK      (1<<31)
#define   FW_BLC_SELF_FIFO_MASK    (1<<16) /* 945 only */
#define   FW_BLC_SELF_EN           (1<<15) /* 945 only */
#define MM_BURST_LENGTH     0x00700000
#define MM_FIFO_WATERMARK   0x0001F000
#define LM_BURST_LENGTH     0x00000700
#define LM_FIFO_WATERMARK   0x0000001F
#define MI_ARB_STATE	0x020e4 /* 915+ only */

/* Make render/texture TLB fetches lower priorty than associated data
 *   fetches. This is not turned on by default
 */
#define   MI_ARB_RENDER_TLB_LOW_PRIORITY	(1 << 15)

/* Isoch request wait on GTT enable (Display A/B/C streams).
 * Make isoch requests stall on the TLB update. May cause
 * display underruns (test mode only)
 */
#define   MI_ARB_ISOCH_WAIT_GTT			(1 << 14)

/* Block grant count for isoch requests when block count is
 * set to a finite value.
 */
#define   MI_ARB_BLOCK_GRANT_MASK		(3 << 12)
#define   MI_ARB_BLOCK_GRANT_8			(0 << 12)	/* for 3 display planes */
#define   MI_ARB_BLOCK_GRANT_4			(1 << 12)	/* for 2 display planes */
#define   MI_ARB_BLOCK_GRANT_2			(2 << 12)	/* for 1 display plane */
#define   MI_ARB_BLOCK_GRANT_0			(3 << 12)	/* don't use */

/* Enable render writes to complete in C2/C3/C4 power states.
 * If this isn't enabled, render writes are prevented in low
 * power states. That seems bad to me.
 */
#define   MI_ARB_C3_LP_WRITE_ENABLE		(1 << 11)

/* This acknowledges an async flip immediately instead
 * of waiting for 2TLB fetches.
 */
#define   MI_ARB_ASYNC_FLIP_ACK_IMMEDIATE	(1 << 10)

/* Enables non-sequential data reads through arbiter
 */
#define   MI_ARB_DUAL_DATA_PHASE_DISABLE	(1 << 9)

/* Disable FSB snooping of cacheable write cycles from binner/render
 * command stream
 */
#define   MI_ARB_CACHE_SNOOP_DISABLE		(1 << 8)

/* Arbiter time slice for non-isoch streams */
#define   MI_ARB_TIME_SLICE_MASK		(7 << 5)
#define   MI_ARB_TIME_SLICE_1			(0 << 5)
#define   MI_ARB_TIME_SLICE_2			(1 << 5)
#define   MI_ARB_TIME_SLICE_4			(2 << 5)
#define   MI_ARB_TIME_SLICE_6			(3 << 5)
#define   MI_ARB_TIME_SLICE_8			(4 << 5)
#define   MI_ARB_TIME_SLICE_10			(5 << 5)
#define   MI_ARB_TIME_SLICE_14			(6 << 5)
#define   MI_ARB_TIME_SLICE_16			(7 << 5)

/* Low priority grace period page size */
#define   MI_ARB_LOW_PRIORITY_GRACE_4KB		(0 << 4)	/* default */
#define   MI_ARB_LOW_PRIORITY_GRACE_8KB		(1 << 4)

/* Disable display A/B trickle feed */
#define   MI_ARB_DISPLAY_TRICKLE_FEED_DISABLE	(1 << 2)

/* Set display plane priority */
#define   MI_ARB_DISPLAY_PRIORITY_A_B		(0 << 0)	/* display A > display B */
#define   MI_ARB_DISPLAY_PRIORITY_B_A		(1 << 0)	/* display B > display A */

#define MI_STATE	0x020e4 /* gen2 only */
#define   MI_AGPBUSY_INT_EN			(1 << 1) /* 85x only */
#define   MI_AGPBUSY_830_MODE			(1 << 0) /* 85x only */

#define CACHE_MODE_0	0x02120 /* 915+ only */
#define   CM0_PIPELINED_RENDER_FLUSH_DISABLE (1<<8)
#define   CM0_IZ_OPT_DISABLE      (1<<6)
#define   CM0_ZR_OPT_DISABLE      (1<<5)
#define	  CM0_STC_EVICT_DISABLE_LRA_SNB	(1<<5)
#define   CM0_DEPTH_EVICT_DISABLE (1<<4)
#define   CM0_COLOR_EVICT_DISABLE (1<<3)
#define   CM0_DEPTH_WRITE_DISABLE (1<<1)
#define   CM0_RC_OP_FLUSH_DISABLE (1<<0)
#define GFX_FLSH_CNTL	0x02170 /* 915+ only */
#define GFX_FLSH_CNTL_GEN6	0x101008
#define   GFX_FLSH_CNTL_EN	(1<<0)
#define ECOSKPD		0x021d0
#define   ECO_GATING_CX_ONLY	(1<<3)
#define   ECO_FLIP_DONE		(1<<0)

#define CACHE_MODE_0_GEN7	0x7000 /* IVB+ */
#define RC_OP_FLUSH_ENABLE (1<<0)
#define   HIZ_RAW_STALL_OPT_DISABLE (1<<2)
#define CACHE_MODE_1		0x7004 /* IVB+ */
#define   PIXEL_SUBSPAN_COLLECT_OPT_DISABLE	(1<<6)
#define   GEN8_4x4_STC_OPTIMIZATION_DISABLE	(1<<6)

#define GEN6_BLITTER_ECOSKPD	0x221d0
#define   GEN6_BLITTER_LOCK_SHIFT			16
#define   GEN6_BLITTER_FBC_NOTIFY			(1<<3)

#define GEN6_RC_SLEEP_PSMI_CONTROL	0x2050
#define   GEN8_RC_SEMA_IDLE_MSG_DISABLE	(1 << 12)
#define   GEN8_FF_DOP_CLOCK_GATE_DISABLE	(1<<10)

#define GEN6_BSD_SLEEP_PSMI_CONTROL	0x12050
#define   GEN6_BSD_SLEEP_MSG_DISABLE	(1 << 0)
#define   GEN6_BSD_SLEEP_FLUSH_DISABLE	(1 << 2)
#define   GEN6_BSD_SLEEP_INDICATOR	(1 << 3)
#define   GEN6_BSD_GO_INDICATOR		(1 << 4)

/* On modern GEN architectures interrupt control consists of two sets
 * of registers. The first set pertains to the ring generating the
 * interrupt. The second control is for the functional block generating the
 * interrupt. These are PM, GT, DE, etc.
 *
 * Luckily *knocks on wood* all the ring interrupt bits match up with the
 * GT interrupt bits, so we don't need to duplicate the defines.
 *
 * These defines should cover us well from SNB->HSW with minor exceptions
 * it can also work on ILK.
 */
#define GT_BLT_FLUSHDW_NOTIFY_INTERRUPT		(1 << 26)
#define GT_BLT_CS_ERROR_INTERRUPT		(1 << 25)
#define GT_BLT_USER_INTERRUPT			(1 << 22)
#define GT_BSD_CS_ERROR_INTERRUPT		(1 << 15)
#define GT_BSD_USER_INTERRUPT			(1 << 12)
#define GT_RENDER_L3_PARITY_ERROR_INTERRUPT_S1	(1 << 11) /* hsw+; rsvd on snb, ivb, vlv */
#define GT_CONTEXT_SWITCH_INTERRUPT		(1 <<  8)
#define GT_RENDER_L3_PARITY_ERROR_INTERRUPT	(1 <<  5) /* !snb */
#define GT_RENDER_PIPECTL_NOTIFY_INTERRUPT	(1 <<  4)
#define GT_RENDER_CS_MASTER_ERROR_INTERRUPT	(1 <<  3)
#define GT_RENDER_SYNC_STATUS_INTERRUPT		(1 <<  2)
#define GT_RENDER_DEBUG_INTERRUPT		(1 <<  1)
#define GT_RENDER_USER_INTERRUPT		(1 <<  0)

#define PM_VEBOX_CS_ERROR_INTERRUPT		(1 << 12) /* hsw+ */
#define PM_VEBOX_USER_INTERRUPT			(1 << 10) /* hsw+ */

#define GT_PARITY_ERROR(dev) \
	(GT_RENDER_L3_PARITY_ERROR_INTERRUPT | \
	 (IS_HASWELL(dev) ? GT_RENDER_L3_PARITY_ERROR_INTERRUPT_S1 : 0))

/* These are all the "old" interrupts */
#define ILK_BSD_USER_INTERRUPT				(1<<5)

#define I915_PM_INTERRUPT				(1<<31)
#define I915_ISP_INTERRUPT				(1<<22)
#define I915_LPE_PIPE_B_INTERRUPT			(1<<21)
#define I915_LPE_PIPE_A_INTERRUPT			(1<<20)
#define I915_MIPIC_INTERRUPT				(1<<19)
#define I915_MIPIA_INTERRUPT				(1<<18)
#define I915_PIPE_CONTROL_NOTIFY_INTERRUPT		(1<<18)
#define I915_DISPLAY_PORT_INTERRUPT			(1<<17)
#define I915_DISPLAY_PIPE_C_HBLANK_INTERRUPT		(1<<16)
#define I915_MASTER_ERROR_INTERRUPT			(1<<15)
#define I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT	(1<<15)
#define I915_DISPLAY_PIPE_B_HBLANK_INTERRUPT		(1<<14)
#define I915_GMCH_THERMAL_SENSOR_EVENT_INTERRUPT	(1<<14) /* p-state */
#define I915_DISPLAY_PIPE_A_HBLANK_INTERRUPT		(1<<13)
#define I915_HWB_OOM_INTERRUPT				(1<<13)
#define I915_LPE_PIPE_C_INTERRUPT			(1<<12)
#define I915_SYNC_STATUS_INTERRUPT			(1<<12)
#define I915_MISC_INTERRUPT				(1<<11)
#define I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT	(1<<11)
#define I915_DISPLAY_PIPE_C_VBLANK_INTERRUPT		(1<<10)
#define I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT	(1<<10)
#define I915_DISPLAY_PIPE_C_EVENT_INTERRUPT		(1<<9)
#define I915_OVERLAY_PLANE_FLIP_PENDING_INTERRUPT	(1<<9)
#define I915_DISPLAY_PIPE_C_DPBM_INTERRUPT		(1<<8)
#define I915_DISPLAY_PLANE_C_FLIP_PENDING_INTERRUPT	(1<<8)
#define I915_DISPLAY_PIPE_A_VBLANK_INTERRUPT		(1<<7)
#define I915_DISPLAY_PIPE_A_EVENT_INTERRUPT		(1<<6)
#define I915_DISPLAY_PIPE_B_VBLANK_INTERRUPT		(1<<5)
#define I915_DISPLAY_PIPE_B_EVENT_INTERRUPT		(1<<4)
#define I915_DISPLAY_PIPE_A_DPBM_INTERRUPT		(1<<3)
#define I915_DISPLAY_PIPE_B_DPBM_INTERRUPT		(1<<2)
#define I915_DEBUG_INTERRUPT				(1<<2)
#define I915_WINVALID_INTERRUPT				(1<<1)
#define I915_USER_INTERRUPT				(1<<1)
#define I915_ASLE_INTERRUPT				(1<<0)
#define I915_BSD_USER_INTERRUPT				(1<<25)

#define GEN6_BSD_RNCID			0x12198

#define GEN7_FF_THREAD_MODE		0x20a0
#define   GEN7_FF_SCHED_MASK		0x0077070
#define   GEN8_FF_DS_REF_CNT_FFME	(1 << 19)
#define   GEN7_FF_TS_SCHED_HS1		(0x5<<16)
#define   GEN7_FF_TS_SCHED_HS0		(0x3<<16)
#define   GEN7_FF_TS_SCHED_LOAD_BALANCE	(0x1<<16)
#define   GEN7_FF_TS_SCHED_HW		(0x0<<16) /* Default */
#define   GEN7_FF_VS_REF_CNT_FFME	(1 << 15)
#define   GEN7_FF_VS_SCHED_HS1		(0x5<<12)
#define   GEN7_FF_VS_SCHED_HS0		(0x3<<12)
#define   GEN7_FF_VS_SCHED_LOAD_BALANCE	(0x1<<12) /* Default */
#define   GEN7_FF_VS_SCHED_HW		(0x0<<12)
#define   GEN7_FF_DS_SCHED_HS1		(0x5<<4)
#define   GEN7_FF_DS_SCHED_HS0		(0x3<<4)
#define   GEN7_FF_DS_SCHED_LOAD_BALANCE	(0x1<<4)  /* Default */
#define   GEN7_FF_DS_SCHED_HW		(0x0<<4)

/*
 * Framebuffer compression (915+ only)
 */

#define FBC_CFB_BASE		0x03200 /* 4k page aligned */
#define FBC_LL_BASE		0x03204 /* 4k page aligned */
#define FBC_CONTROL		0x03208
#define   FBC_CTL_EN		(1<<31)
#define   FBC_CTL_PERIODIC	(1<<30)
#define   FBC_CTL_INTERVAL_SHIFT (16)
#define   FBC_CTL_UNCOMPRESSIBLE (1<<14)
#define   FBC_CTL_C3_IDLE	(1<<13)
#define   FBC_CTL_STRIDE_SHIFT	(5)
#define   FBC_CTL_FENCENO_SHIFT	(0)
#define FBC_COMMAND		0x0320c
#define   FBC_CMD_COMPRESS	(1<<0)
#define FBC_STATUS		0x03210
#define   FBC_STAT_COMPRESSING	(1<<31)
#define   FBC_STAT_COMPRESSED	(1<<30)
#define   FBC_STAT_MODIFIED	(1<<29)
#define   FBC_STAT_CURRENT_LINE_SHIFT	(0)
#define FBC_CONTROL2		0x03214
#define   FBC_CTL_FENCE_DBL	(0<<4)
#define   FBC_CTL_IDLE_IMM	(0<<2)
#define   FBC_CTL_IDLE_FULL	(1<<2)
#define   FBC_CTL_IDLE_LINE	(2<<2)
#define   FBC_CTL_IDLE_DEBUG	(3<<2)
#define   FBC_CTL_CPU_FENCE	(1<<1)
#define   FBC_CTL_PLANE(plane)	((plane)<<0)
#define FBC_FENCE_OFF		0x03218 /* BSpec typo has 321Bh */
#define FBC_TAG			0x03300

#define FBC_LL_SIZE		(1536)

/* Framebuffer compression for GM45+ */
#define DPFC_CB_BASE		0x3200
#define DPFC_CONTROL		0x3208
#define   DPFC_CTL_EN		(1<<31)
#define   DPFC_CTL_PLANE(plane)	((plane)<<30)
#define   IVB_DPFC_CTL_PLANE(plane)	((plane)<<29)
#define   DPFC_CTL_FENCE_EN	(1<<29)
#define   IVB_DPFC_CTL_FENCE_EN	(1<<28)
#define   DPFC_CTL_PERSISTENT_MODE	(1<<25)
#define   DPFC_SR_EN		(1<<10)
#define   DPFC_CTL_LIMIT_1X	(0<<6)
#define   DPFC_CTL_LIMIT_2X	(1<<6)
#define   DPFC_CTL_LIMIT_4X	(2<<6)
#define DPFC_RECOMP_CTL		0x320c
#define   DPFC_RECOMP_STALL_EN	(1<<27)
#define   DPFC_RECOMP_STALL_WM_SHIFT (16)
#define   DPFC_RECOMP_STALL_WM_MASK (0x07ff0000)
#define   DPFC_RECOMP_TIMER_COUNT_SHIFT (0)
#define   DPFC_RECOMP_TIMER_COUNT_MASK (0x0000003f)
#define DPFC_STATUS		0x3210
#define   DPFC_INVAL_SEG_SHIFT  (16)
#define   DPFC_INVAL_SEG_MASK	(0x07ff0000)
#define   DPFC_COMP_SEG_SHIFT	(0)
#define   DPFC_COMP_SEG_MASK	(0x000003ff)
#define DPFC_STATUS2		0x3214
#define DPFC_FENCE_YOFF		0x3218
#define DPFC_CHICKEN		0x3224
#define   DPFC_HT_MODIFY	(1<<31)

/* Framebuffer compression for Ironlake */
#define ILK_DPFC_CB_BASE	0x43200
#define ILK_DPFC_CONTROL	0x43208
#define   FBC_CTL_FALSE_COLOR	(1<<10)
/* The bit 28-8 is reserved */
#define   DPFC_RESERVED		(0x1FFFFF00)
#define ILK_DPFC_RECOMP_CTL	0x4320c
#define ILK_DPFC_STATUS		0x43210
#define ILK_DPFC_FENCE_YOFF	0x43218
#define ILK_DPFC_CHICKEN	0x43224
#define ILK_FBC_RT_BASE		0x2128
#define   ILK_FBC_RT_VALID	(1<<0)
#define   SNB_FBC_FRONT_BUFFER	(1<<1)

#define ILK_DISPLAY_CHICKEN1	0x42000
#define   ILK_FBCQ_DIS		(1<<22)
#define	  ILK_PABSTRETCH_DIS	(1<<21)


/*
 * Framebuffer compression for Sandybridge
 *
 * The following two registers are of type GTTMMADR
 */
#define SNB_DPFC_CTL_SA		0x100100
#define   SNB_CPU_FENCE_ENABLE	(1<<29)
#define DPFC_CPU_FENCE_OFFSET	0x100104

/* Framebuffer compression for Ivybridge */
#define IVB_FBC_RT_BASE			0x7020

#define IPS_CTL		0x43408
#define   IPS_ENABLE	(1 << 31)

#define MSG_FBC_REND_STATE	0x50380
#define   FBC_REND_NUKE		(1<<2)
#define   FBC_REND_CACHE_CLEAN	(1<<1)

/*
 * GPIO regs
 */
#define GPIOA			0x5010
#define GPIOB			0x5014
#define GPIOC			0x5018
#define GPIOD			0x501c
#define GPIOE			0x5020
#define GPIOF			0x5024
#define GPIOG			0x5028
#define GPIOH			0x502c
# define GPIO_CLOCK_DIR_MASK		(1 << 0)
# define GPIO_CLOCK_DIR_IN		(0 << 1)
# define GPIO_CLOCK_DIR_OUT		(1 << 1)
# define GPIO_CLOCK_VAL_MASK		(1 << 2)
# define GPIO_CLOCK_VAL_OUT		(1 << 3)
# define GPIO_CLOCK_VAL_IN		(1 << 4)
# define GPIO_CLOCK_PULLUP_DISABLE	(1 << 5)
# define GPIO_DATA_DIR_MASK		(1 << 8)
# define GPIO_DATA_DIR_IN		(0 << 9)
# define GPIO_DATA_DIR_OUT		(1 << 9)
# define GPIO_DATA_VAL_MASK		(1 << 10)
# define GPIO_DATA_VAL_OUT		(1 << 11)
# define GPIO_DATA_VAL_IN		(1 << 12)
# define GPIO_DATA_PULLUP_DISABLE	(1 << 13)

#define GMBUS0			0x5100 /* clock/port select */
#define   GMBUS_RATE_100KHZ	(0<<8)
#define   GMBUS_RATE_50KHZ	(1<<8)
#define   GMBUS_RATE_400KHZ	(2<<8) /* reserved on Pineview */
#define   GMBUS_RATE_1MHZ	(3<<8) /* reserved on Pineview */
#define   GMBUS_HOLD_EXT	(1<<7) /* 300ns hold time, rsvd on Pineview */
#define   GMBUS_PORT_DISABLED	0
#define   GMBUS_PORT_SSC	1
#define   GMBUS_PORT_VGADDC	2
#define   GMBUS_PORT_PANEL	3
#define   GMBUS_PORT_DPD_CHV	3 /* HDMID_CHV */
#define   GMBUS_PORT_DPC	4 /* HDMIC */
#define   GMBUS_PORT_DPB	5 /* SDVO, HDMIB */
#define   GMBUS_PORT_DPD	6 /* HDMID */
#define   GMBUS_PORT_RESERVED	7 /* 7 reserved */
#define   GMBUS_NUM_PORTS	(GMBUS_PORT_DPD - GMBUS_PORT_SSC + 1)
#define GMBUS1			0x5104 /* command/status */
#define   GMBUS_SW_CLR_INT	(1<<31)
#define   GMBUS_SW_RDY		(1<<30)
#define   GMBUS_ENT		(1<<29) /* enable timeout */
#define   GMBUS_CYCLE_NONE	(0<<25)
#define   GMBUS_CYCLE_WAIT	(1<<25)
#define   GMBUS_CYCLE_INDEX	(2<<25)
#define   GMBUS_CYCLE_STOP	(4<<25)
#define   GMBUS_BYTE_COUNT_SHIFT 16
#define   GMBUS_SLAVE_INDEX_SHIFT 8
#define   GMBUS_SLAVE_ADDR_SHIFT 1
#define   GMBUS_SLAVE_READ	(1<<0)
#define   GMBUS_SLAVE_WRITE	(0<<0)
#define GMBUS2			0x5108 /* status */
#define   GMBUS_INUSE		(1<<15)
#define   GMBUS_HW_WAIT_PHASE	(1<<14)
#define   GMBUS_STALL_TIMEOUT	(1<<13)
#define   GMBUS_INT		(1<<12)
#define   GMBUS_HW_RDY		(1<<11)
#define   GMBUS_SATOER		(1<<10)
#define   GMBUS_ACTIVE		(1<<9)
#define GMBUS3			0x510c /* data buffer bytes 3-0 */
#define GMBUS4			0x5110 /* interrupt mask (Pineview+) */
#define   GMBUS_SLAVE_TIMEOUT_EN (1<<4)
#define   GMBUS_NAK_EN		(1<<3)
#define   GMBUS_IDLE_EN		(1<<2)
#define   GMBUS_HW_WAIT_EN	(1<<1)
#define   GMBUS_HW_RDY_EN	(1<<0)
#define GMBUS5			0x5120 /* byte index */
#define   GMBUS_2BYTE_INDEX_EN	(1<<31)

/*
 * Clock control & power management
 */
#define _DPLL_A (dev_priv->info.display_mmio_offset + 0x6014)
#define _DPLL_B (dev_priv->info.display_mmio_offset + 0x6018)
#define _CHV_DPLL_C (dev_priv->info.display_mmio_offset + 0x6030)
#define DPLL(pipe) _PIPE3((pipe), _DPLL_A, _DPLL_B, _CHV_DPLL_C)

#define VGA0	0x6000
#define VGA1	0x6004
#define VGA_PD	0x6010
#define   VGA0_PD_P2_DIV_4	(1 << 7)
#define   VGA0_PD_P1_DIV_2	(1 << 5)
#define   VGA0_PD_P1_SHIFT	0
#define   VGA0_PD_P1_MASK	(0x1f << 0)
#define   VGA1_PD_P2_DIV_4	(1 << 15)
#define   VGA1_PD_P1_DIV_2	(1 << 13)
#define   VGA1_PD_P1_SHIFT	8
#define   VGA1_PD_P1_MASK	(0x1f << 8)
#define   DPLL_VCO_ENABLE		(1 << 31)
#define   DPLL_SDVO_HIGH_SPEED		(1 << 30)
#define   DPLL_DVO_2X_MODE		(1 << 30)
#define   DPLL_EXT_BUFFER_ENABLE_VLV	(1 << 30)
#define   DPLL_SYNCLOCK_ENABLE		(1 << 29)
#define   DPLL_REFA_CLK_ENABLE_VLV	(1 << 29)
#define   DPLL_VGA_MODE_DIS		(1 << 28)
#define   DPLLB_MODE_DAC_SERIAL		(1 << 26) /* i915 */
#define   DPLLB_MODE_LVDS		(2 << 26) /* i915 */
#define   DPLL_MODE_MASK		(3 << 26)
#define   DPLL_DAC_SERIAL_P2_CLOCK_DIV_10 (0 << 24) /* i915 */
#define   DPLL_DAC_SERIAL_P2_CLOCK_DIV_5 (1 << 24) /* i915 */
#define   DPLLB_LVDS_P2_CLOCK_DIV_14	(0 << 24) /* i915 */
#define   DPLLB_LVDS_P2_CLOCK_DIV_7	(1 << 24) /* i915 */
#define   DPLL_P2_CLOCK_DIV_MASK	0x03000000 /* i915 */
#define   DPLL_FPA01_P1_POST_DIV_MASK	0x00ff0000 /* i915 */
#define   DPLL_FPA01_P1_POST_DIV_MASK_PINEVIEW	0x00ff8000 /* Pineview */
#define   DPLL_LOCK_VLV			(1<<15)
#define   DPLL_INTEGRATED_CRI_CLK_VLV	(1<<14)
#define   DPLL_INTEGRATED_CLOCK_VLV	(1<<13)
#define   DPLL_SSC_REF_CLOCK_CHV	(1<<13)
#define   DPLL_PORTC_READY_MASK		(0xf << 4)
#define   DPLL_PORTB_READY_MASK		(0xf)

#define   DPLL_FPA01_P1_POST_DIV_MASK_I830	0x001f0000

/* Additional CHV pll/phy registers */
#define DPIO_PHY_STATUS			(VLV_DISPLAY_BASE + 0x6240)
#define   DPLL_PORTD_READY_MASK		(0xf)
#define DISPLAY_PHY_CONTROL (VLV_DISPLAY_BASE + 0x60100)
#define   PHY_COM_LANE_RESET_DEASSERT(phy) (1 << (phy))
#define DISPLAY_PHY_STATUS (VLV_DISPLAY_BASE + 0x60104)
#define   PHY_POWERGOOD(phy)	(((phy) == DPIO_PHY0) ? (1<<31) : (1<<30))

/*
 * The i830 generation, in LVDS mode, defines P1 as the bit number set within
 * this field (only one bit may be set).
 */
#define   DPLL_FPA01_P1_POST_DIV_MASK_I830_LVDS	0x003f0000
#define   DPLL_FPA01_P1_POST_DIV_SHIFT	16
#define   DPLL_FPA01_P1_POST_DIV_SHIFT_PINEVIEW 15
/* i830, required in DVO non-gang */
#define   PLL_P2_DIVIDE_BY_4		(1 << 23)
#define   PLL_P1_DIVIDE_BY_TWO		(1 << 21) /* i830 */
#define   PLL_REF_INPUT_DREFCLK		(0 << 13)
#define   PLL_REF_INPUT_TVCLKINA	(1 << 13) /* i830 */
#define   PLL_REF_INPUT_TVCLKINBC	(2 << 13) /* SDVO TVCLKIN */
#define   PLLB_REF_INPUT_SPREADSPECTRUMIN (3 << 13)
#define   PLL_REF_INPUT_MASK		(3 << 13)
#define   PLL_LOAD_PULSE_PHASE_SHIFT		9
/* Ironlake */
# define PLL_REF_SDVO_HDMI_MULTIPLIER_SHIFT     9
# define PLL_REF_SDVO_HDMI_MULTIPLIER_MASK      (7 << 9)
# define PLL_REF_SDVO_HDMI_MULTIPLIER(x)	(((x)-1) << 9)
# define DPLL_FPA1_P1_POST_DIV_SHIFT            0
# define DPLL_FPA1_P1_POST_DIV_MASK             0xff

/*
 * Parallel to Serial Load Pulse phase selection.
 * Selects the phase for the 10X DPLL clock for the PCIe
 * digital display port. The range is 4 to 13; 10 or more
 * is just a flip delay. The default is 6
 */
#define   PLL_LOAD_PULSE_PHASE_MASK		(0xf << PLL_LOAD_PULSE_PHASE_SHIFT)
#define   DISPLAY_RATE_SELECT_FPA1		(1 << 8)
/*
 * SDVO multiplier for 945G/GM. Not used on 965.
 */
#define   SDVO_MULTIPLIER_MASK			0x000000ff
#define   SDVO_MULTIPLIER_SHIFT_HIRES		4
#define   SDVO_MULTIPLIER_SHIFT_VGA		0

#define _DPLL_A_MD (dev_priv->info.display_mmio_offset + 0x601c)
#define _DPLL_B_MD (dev_priv->info.display_mmio_offset + 0x6020)
#define _CHV_DPLL_C_MD (dev_priv->info.display_mmio_offset + 0x603c)
#define DPLL_MD(pipe) _PIPE3((pipe), _DPLL_A_MD, _DPLL_B_MD, _CHV_DPLL_C_MD)

/*
 * UDI pixel divider, controlling how many pixels are stuffed into a packet.
 *
 * Value is pixels minus 1.  Must be set to 1 pixel for SDVO.
 */
#define   DPLL_MD_UDI_DIVIDER_MASK		0x3f000000
#define   DPLL_MD_UDI_DIVIDER_SHIFT		24
/* UDI pixel divider for VGA, same as DPLL_MD_UDI_DIVIDER_MASK. */
#define   DPLL_MD_VGA_UDI_DIVIDER_MASK		0x003f0000
#define   DPLL_MD_VGA_UDI_DIVIDER_SHIFT		16
/*
 * SDVO/UDI pixel multiplier.
 *
 * SDVO requires that the bus clock rate be between 1 and 2 Ghz, and the bus
 * clock rate is 10 times the DPLL clock.  At low resolution/refresh rate
 * modes, the bus rate would be below the limits, so SDVO allows for stuffing
 * dummy bytes in the datastream at an increased clock rate, with both sides of
 * the link knowing how many bytes are fill.
 *
 * So, for a mode with a dotclock of 65Mhz, we would want to double the clock
 * rate to 130Mhz to get a bus rate of 1.30Ghz.  The DPLL clock rate would be
 * set to 130Mhz, and the SDVO multiplier set to 2x in this register and
 * through an SDVO command.
 *
 * This register field has values of multiplication factor minus 1, with
 * a maximum multiplier of 5 for SDVO.
 */
#define   DPLL_MD_UDI_MULTIPLIER_MASK		0x00003f00
#define   DPLL_MD_UDI_MULTIPLIER_SHIFT		8
/*
 * SDVO/UDI pixel multiplier for VGA, same as DPLL_MD_UDI_MULTIPLIER_MASK.
 * This best be set to the default value (3) or the CRT won't work. No,
 * I don't entirely understand what this does...
 */
#define   DPLL_MD_VGA_UDI_MULTIPLIER_MASK	0x0000003f
#define   DPLL_MD_VGA_UDI_MULTIPLIER_SHIFT	0

#define _FPA0	0x06040
#define _FPA1	0x06044
#define _FPB0	0x06048
#define _FPB1	0x0604c
#define FP0(pipe) _PIPE(pipe, _FPA0, _FPB0)
#define FP1(pipe) _PIPE(pipe, _FPA1, _FPB1)
#define   FP_N_DIV_MASK		0x003f0000
#define   FP_N_PINEVIEW_DIV_MASK	0x00ff0000
#define   FP_N_DIV_SHIFT		16
#define   FP_M1_DIV_MASK	0x00003f00
#define   FP_M1_DIV_SHIFT		 8
#define   FP_M2_DIV_MASK	0x0000003f
#define   FP_M2_PINEVIEW_DIV_MASK	0x000000ff
#define   FP_M2_DIV_SHIFT		 0
#define DPLL_TEST	0x606c
#define   DPLLB_TEST_SDVO_DIV_1		(0 << 22)
#define   DPLLB_TEST_SDVO_DIV_2		(1 << 22)
#define   DPLLB_TEST_SDVO_DIV_4		(2 << 22)
#define   DPLLB_TEST_SDVO_DIV_MASK	(3 << 22)
#define   DPLLB_TEST_N_BYPASS		(1 << 19)
#define   DPLLB_TEST_M_BYPASS		(1 << 18)
#define   DPLLB_INPUT_BUFFER_ENABLE	(1 << 16)
#define   DPLLA_TEST_N_BYPASS		(1 << 3)
#define   DPLLA_TEST_M_BYPASS		(1 << 2)
#define   DPLLA_INPUT_BUFFER_ENABLE	(1 << 0)
#define D_STATE		0x6104
#define  DSTATE_GFX_RESET_I830			(1<<6)
#define  DSTATE_PLL_D3_OFF			(1<<3)
#define  DSTATE_GFX_CLOCK_GATING		(1<<1)
#define  DSTATE_DOT_CLOCK_GATING		(1<<0)
#define DSPCLK_GATE_D	(dev_priv->info.display_mmio_offset + 0x6200)
# define DPUNIT_B_CLOCK_GATE_DISABLE		(1 << 30) /* 965 */
# define VSUNIT_CLOCK_GATE_DISABLE		(1 << 29) /* 965 */
# define VRHUNIT_CLOCK_GATE_DISABLE		(1 << 28) /* 965 */
# define VRDUNIT_CLOCK_GATE_DISABLE		(1 << 27) /* 965 */
# define AUDUNIT_CLOCK_GATE_DISABLE		(1 << 26) /* 965 */
# define DPUNIT_A_CLOCK_GATE_DISABLE		(1 << 25) /* 965 */
# define DPCUNIT_CLOCK_GATE_DISABLE		(1 << 24) /* 965 */
# define TVRUNIT_CLOCK_GATE_DISABLE		(1 << 23) /* 915-945 */
# define TVCUNIT_CLOCK_GATE_DISABLE		(1 << 22) /* 915-945 */
# define TVFUNIT_CLOCK_GATE_DISABLE		(1 << 21) /* 915-945 */
# define TVEUNIT_CLOCK_GATE_DISABLE		(1 << 20) /* 915-945 */
# define DVSUNIT_CLOCK_GATE_DISABLE		(1 << 19) /* 915-945 */
# define DSSUNIT_CLOCK_GATE_DISABLE		(1 << 18) /* 915-945 */
# define DDBUNIT_CLOCK_GATE_DISABLE		(1 << 17) /* 915-945 */
# define DPRUNIT_CLOCK_GATE_DISABLE		(1 << 16) /* 915-945 */
# define DPFUNIT_CLOCK_GATE_DISABLE		(1 << 15) /* 915-945 */
# define DPBMUNIT_CLOCK_GATE_DISABLE		(1 << 14) /* 915-945 */
# define DPLSUNIT_CLOCK_GATE_DISABLE		(1 << 13) /* 915-945 */
# define DPLUNIT_CLOCK_GATE_DISABLE		(1 << 12) /* 915-945 */
# define DPOUNIT_CLOCK_GATE_DISABLE		(1 << 11)
# define DPBUNIT_CLOCK_GATE_DISABLE		(1 << 10)
# define DCUNIT_CLOCK_GATE_DISABLE		(1 << 9)
# define DPUNIT_CLOCK_GATE_DISABLE		(1 << 8)
# define VRUNIT_CLOCK_GATE_DISABLE		(1 << 7) /* 915+: reserved */
# define OVHUNIT_CLOCK_GATE_DISABLE		(1 << 6) /* 830-865 */
# define DPIOUNIT_CLOCK_GATE_DISABLE		(1 << 6) /* 915-945 */
# define OVFUNIT_CLOCK_GATE_DISABLE		(1 << 5)
# define OVBUNIT_CLOCK_GATE_DISABLE		(1 << 4)
/*
 * This bit must be set on the 830 to prevent hangs when turning off the
 * overlay scaler.
 */
# define OVRUNIT_CLOCK_GATE_DISABLE		(1 << 3)
# define OVCUNIT_CLOCK_GATE_DISABLE		(1 << 2)
# define OVUUNIT_CLOCK_GATE_DISABLE		(1 << 1)
# define ZVUNIT_CLOCK_GATE_DISABLE		(1 << 0) /* 830 */
# define OVLUNIT_CLOCK_GATE_DISABLE		(1 << 0) /* 845,865 */

#define RENCLK_GATE_D1		0x6204
# define BLITTER_CLOCK_GATE_DISABLE		(1 << 13) /* 945GM only */
# define MPEG_CLOCK_GATE_DISABLE		(1 << 12) /* 945GM only */
# define PC_FE_CLOCK_GATE_DISABLE		(1 << 11)
# define PC_BE_CLOCK_GATE_DISABLE		(1 << 10)
# define WINDOWER_CLOCK_GATE_DISABLE		(1 << 9)
# define INTERPOLATOR_CLOCK_GATE_DISABLE	(1 << 8)
# define COLOR_CALCULATOR_CLOCK_GATE_DISABLE	(1 << 7)
# define MOTION_COMP_CLOCK_GATE_DISABLE		(1 << 6)
# define MAG_CLOCK_GATE_DISABLE			(1 << 5)
/* This bit must be unset on 855,865 */
# define MECI_CLOCK_GATE_DISABLE		(1 << 4)
# define DCMP_CLOCK_GATE_DISABLE		(1 << 3)
# define MEC_CLOCK_GATE_DISABLE			(1 << 2)
# define MECO_CLOCK_GATE_DISABLE		(1 << 1)
/* This bit must be set on 855,865. */
# define SV_CLOCK_GATE_DISABLE			(1 << 0)
# define I915_MPEG_CLOCK_GATE_DISABLE		(1 << 16)
# define I915_VLD_IP_PR_CLOCK_GATE_DISABLE	(1 << 15)
# define I915_MOTION_COMP_CLOCK_GATE_DISABLE	(1 << 14)
# define I915_BD_BF_CLOCK_GATE_DISABLE		(1 << 13)
# define I915_SF_SE_CLOCK_GATE_DISABLE		(1 << 12)
# define I915_WM_CLOCK_GATE_DISABLE		(1 << 11)
# define I915_IZ_CLOCK_GATE_DISABLE		(1 << 10)
# define I915_PI_CLOCK_GATE_DISABLE		(1 << 9)
# define I915_DI_CLOCK_GATE_DISABLE		(1 << 8)
# define I915_SH_SV_CLOCK_GATE_DISABLE		(1 << 7)
# define I915_PL_DG_QC_FT_CLOCK_GATE_DISABLE	(1 << 6)
# define I915_SC_CLOCK_GATE_DISABLE		(1 << 5)
# define I915_FL_CLOCK_GATE_DISABLE		(1 << 4)
# define I915_DM_CLOCK_GATE_DISABLE		(1 << 3)
# define I915_PS_CLOCK_GATE_DISABLE		(1 << 2)
# define I915_CC_CLOCK_GATE_DISABLE		(1 << 1)
# define I915_BY_CLOCK_GATE_DISABLE		(1 << 0)

# define I965_RCZ_CLOCK_GATE_DISABLE		(1 << 30)
/* This bit must always be set on 965G/965GM */
# define I965_RCC_CLOCK_GATE_DISABLE		(1 << 29)
# define I965_RCPB_CLOCK_GATE_DISABLE		(1 << 28)
# define I965_DAP_CLOCK_GATE_DISABLE		(1 << 27)
# define I965_ROC_CLOCK_GATE_DISABLE		(1 << 26)
# define I965_GW_CLOCK_GATE_DISABLE		(1 << 25)
# define I965_TD_CLOCK_GATE_DISABLE		(1 << 24)
/* This bit must always be set on 965G */
# define I965_ISC_CLOCK_GATE_DISABLE		(1 << 23)
# define I965_IC_CLOCK_GATE_DISABLE		(1 << 22)
# define I965_EU_CLOCK_GATE_DISABLE		(1 << 21)
# define I965_IF_CLOCK_GATE_DISABLE		(1 << 20)
# define I965_TC_CLOCK_GATE_DISABLE		(1 << 19)
# define I965_SO_CLOCK_GATE_DISABLE		(1 << 17)
# define I965_FBC_CLOCK_GATE_DISABLE		(1 << 16)
# define I965_MARI_CLOCK_GATE_DISABLE		(1 << 15)
# define I965_MASF_CLOCK_GATE_DISABLE		(1 << 14)
# define I965_MAWB_CLOCK_GATE_DISABLE		(1 << 13)
# define I965_EM_CLOCK_GATE_DISABLE		(1 << 12)
# define I965_UC_CLOCK_GATE_DISABLE		(1 << 11)
# define I965_SI_CLOCK_GATE_DISABLE		(1 << 6)
# define I965_MT_CLOCK_GATE_DISABLE		(1 << 5)
# define I965_PL_CLOCK_GATE_DISABLE		(1 << 4)
# define I965_DG_CLOCK_GATE_DISABLE		(1 << 3)
# define I965_QC_CLOCK_GATE_DISABLE		(1 << 2)
# define I965_FT_CLOCK_GATE_DISABLE		(1 << 1)
# define I965_DM_CLOCK_GATE_DISABLE		(1 << 0)

#define RENCLK_GATE_D2		0x6208
#define VF_UNIT_CLOCK_GATE_DISABLE		(1 << 9)
#define GS_UNIT_CLOCK_GATE_DISABLE		(1 << 7)
#define CL_UNIT_CLOCK_GATE_DISABLE		(1 << 6)

#define VDECCLK_GATE_D		0x620C		/* g4x only */
#define  VCP_UNIT_CLOCK_GATE_DISABLE		(1 << 4)

#define RAMCLK_GATE_D		0x6210		/* CRL only */
#define DEUC			0x6214          /* CRL only */

#define FW_BLC_SELF_VLV		(VLV_DISPLAY_BASE + 0x6500)
#define  FW_CSPWRDWNEN		(1<<15)

#define MI_ARB_VLV		(VLV_DISPLAY_BASE + 0x6504)

#define CZCLK_CDCLK_FREQ_RATIO	(VLV_DISPLAY_BASE + 0x6508)
#define   CDCLK_FREQ_SHIFT	4
#define   CDCLK_FREQ_MASK	(0x1f << CDCLK_FREQ_SHIFT)
#define   CZCLK_FREQ_MASK	0xf
#define GMBUSFREQ_VLV		(VLV_DISPLAY_BASE + 0x6510)

/*
 * Palette regs
 */
#define PALETTE_A_OFFSET 0xa000
#define PALETTE_B_OFFSET 0xa800
#define CHV_PALETTE_C_OFFSET 0xc000
#define PALETTE(pipe) (dev_priv->info.palette_offsets[pipe] + \
		       dev_priv->info.display_mmio_offset)

/* MCH MMIO space */

/*
 * MCHBAR mirror.
 *
 * This mirrors the MCHBAR MMIO space whose location is determined by
 * device 0 function 0's pci config register 0x44 or 0x48 and matches it in
 * every way.  It is not accessible from the CP register read instructions.
 *
 * Starting from Haswell, you can't write registers using the MCHBAR mirror,
 * just read.
 */
#define MCHBAR_MIRROR_BASE	0x10000

#define MCHBAR_MIRROR_BASE_SNB	0x140000

/* Memory controller frequency in MCHBAR for Haswell (possible SNB+) */
#define DCLK (MCHBAR_MIRROR_BASE_SNB + 0x5e04)

/* 915-945 and GM965 MCH register controlling DRAM channel access */
#define DCC			0x10200
#define DCC_ADDRESSING_MODE_SINGLE_CHANNEL		(0 << 0)
#define DCC_ADDRESSING_MODE_DUAL_CHANNEL_ASYMMETRIC	(1 << 0)
#define DCC_ADDRESSING_MODE_DUAL_CHANNEL_INTERLEAVED	(2 << 0)
#define DCC_ADDRESSING_MODE_MASK			(3 << 0)
#define DCC_CHANNEL_XOR_DISABLE				(1 << 10)
#define DCC_CHANNEL_XOR_BIT_17				(1 << 9)
#define DCC2			0x10204
#define DCC2_MODIFIED_ENHANCED_DISABLE			(1 << 20)

/* Pineview MCH register contains DDR3 setting */
#define CSHRDDR3CTL            0x101a8
#define CSHRDDR3CTL_DDR3       (1 << 2)

/* 965 MCH register controlling DRAM channel configuration */
#define C0DRB3			0x10206
#define C1DRB3			0x10606

/* snb MCH registers for reading the DRAM channel configuration */
#define MAD_DIMM_C0			(MCHBAR_MIRROR_BASE_SNB + 0x5004)
#define MAD_DIMM_C1			(MCHBAR_MIRROR_BASE_SNB + 0x5008)
#define MAD_DIMM_C2			(MCHBAR_MIRROR_BASE_SNB + 0x500C)
#define   MAD_DIMM_ECC_MASK		(0x3 << 24)
#define   MAD_DIMM_ECC_OFF		(0x0 << 24)
#define   MAD_DIMM_ECC_IO_ON_LOGIC_OFF	(0x1 << 24)
#define   MAD_DIMM_ECC_IO_OFF_LOGIC_ON	(0x2 << 24)
#define   MAD_DIMM_ECC_ON		(0x3 << 24)
#define   MAD_DIMM_ENH_INTERLEAVE	(0x1 << 22)
#define   MAD_DIMM_RANK_INTERLEAVE	(0x1 << 21)
#define   MAD_DIMM_B_WIDTH_X16		(0x1 << 20) /* X8 chips if unset */
#define   MAD_DIMM_A_WIDTH_X16		(0x1 << 19) /* X8 chips if unset */
#define   MAD_DIMM_B_DUAL_RANK		(0x1 << 18)
#define   MAD_DIMM_A_DUAL_RANK		(0x1 << 17)
#define   MAD_DIMM_A_SELECT		(0x1 << 16)
/* DIMM sizes are in multiples of 256mb. */
#define   MAD_DIMM_B_SIZE_SHIFT		8
#define   MAD_DIMM_B_SIZE_MASK		(0xff << MAD_DIMM_B_SIZE_SHIFT)
#define   MAD_DIMM_A_SIZE_SHIFT		0
#define   MAD_DIMM_A_SIZE_MASK		(0xff << MAD_DIMM_A_SIZE_SHIFT)

/* snb MCH registers for priority tuning */
#define MCH_SSKPD			(MCHBAR_MIRROR_BASE_SNB + 0x5d10)
#define   MCH_SSKPD_WM0_MASK		0x3f
#define   MCH_SSKPD_WM0_VAL		0xc

#define MCH_SECP_NRG_STTS		(MCHBAR_MIRROR_BASE_SNB + 0x592c)

/* Clocking configuration register */
#define CLKCFG			0x10c00
#define CLKCFG_FSB_400					(5 << 0)	/* hrawclk 100 */
#define CLKCFG_FSB_533					(1 << 0)	/* hrawclk 133 */
#define CLKCFG_FSB_667					(3 << 0)	/* hrawclk 166 */
#define CLKCFG_FSB_800					(2 << 0)	/* hrawclk 200 */
#define CLKCFG_FSB_1067					(6 << 0)	/* hrawclk 266 */
#define CLKCFG_FSB_1333					(7 << 0)	/* hrawclk 333 */
/* Note, below two are guess */
#define CLKCFG_FSB_1600					(4 << 0)	/* hrawclk 400 */
#define CLKCFG_FSB_1600_ALT				(0 << 0)	/* hrawclk 400 */
#define CLKCFG_FSB_MASK					(7 << 0)
#define CLKCFG_MEM_533					(1 << 4)
#define CLKCFG_MEM_667					(2 << 4)
#define CLKCFG_MEM_800					(3 << 4)
#define CLKCFG_MEM_MASK					(7 << 4)

#define TSC1			0x11001
#define   TSE			(1<<0)
#define TR1			0x11006
#define TSFS			0x11020
#define   TSFS_SLOPE_MASK	0x0000ff00
#define   TSFS_SLOPE_SHIFT	8
#define   TSFS_INTR_MASK	0x000000ff

#define CRSTANDVID		0x11100
#define PXVFREQ_BASE		0x11110 /* P[0-15]VIDFREQ (0x1114c) (Ironlake) */
#define   PXVFREQ_PX_MASK	0x7f000000
#define   PXVFREQ_PX_SHIFT	24
#define VIDFREQ_BASE		0x11110
#define VIDFREQ1		0x11110 /* VIDFREQ1-4 (0x1111c) (Cantiga) */
#define VIDFREQ2		0x11114
#define VIDFREQ3		0x11118
#define VIDFREQ4		0x1111c
#define   VIDFREQ_P0_MASK	0x1f000000
#define   VIDFREQ_P0_SHIFT	24
#define   VIDFREQ_P0_CSCLK_MASK	0x00f00000
#define   VIDFREQ_P0_CSCLK_SHIFT 20
#define   VIDFREQ_P0_CRCLK_MASK	0x000f0000
#define   VIDFREQ_P0_CRCLK_SHIFT 16
#define   VIDFREQ_P1_MASK	0x00001f00
#define   VIDFREQ_P1_SHIFT	8
#define   VIDFREQ_P1_CSCLK_MASK	0x000000f0
#define   VIDFREQ_P1_CSCLK_SHIFT 4
#define   VIDFREQ_P1_CRCLK_MASK	0x0000000f
#define INTTOEXT_BASE_ILK	0x11300
#define INTTOEXT_BASE		0x11120 /* INTTOEXT1-8 (0x1113c) */
#define   INTTOEXT_MAP3_SHIFT	24
#define   INTTOEXT_MAP3_MASK	(0x1f << INTTOEXT_MAP3_SHIFT)
#define   INTTOEXT_MAP2_SHIFT	16
#define   INTTOEXT_MAP2_MASK	(0x1f << INTTOEXT_MAP2_SHIFT)
#define   INTTOEXT_MAP1_SHIFT	8
#define   INTTOEXT_MAP1_MASK	(0x1f << INTTOEXT_MAP1_SHIFT)
#define   INTTOEXT_MAP0_SHIFT	0
#define   INTTOEXT_MAP0_MASK	(0x1f << INTTOEXT_MAP0_SHIFT)
#define MEMSWCTL		0x11170 /* Ironlake only */
#define   MEMCTL_CMD_MASK	0xe000
#define   MEMCTL_CMD_SHIFT	13
#define   MEMCTL_CMD_RCLK_OFF	0
#define   MEMCTL_CMD_RCLK_ON	1
#define   MEMCTL_CMD_CHFREQ	2
#define   MEMCTL_CMD_CHVID	3
#define   MEMCTL_CMD_VMMOFF	4
#define   MEMCTL_CMD_VMMON	5
#define   MEMCTL_CMD_STS	(1<<12) /* write 1 triggers command, clears
					   when command complete */
#define   MEMCTL_FREQ_MASK	0x0f00 /* jitter, from 0-15 */
#define   MEMCTL_FREQ_SHIFT	8
#define   MEMCTL_SFCAVM		(1<<7)
#define   MEMCTL_TGT_VID_MASK	0x007f
#define MEMIHYST		0x1117c
#define MEMINTREN		0x11180 /* 16 bits */
#define   MEMINT_RSEXIT_EN	(1<<8)
#define   MEMINT_CX_SUPR_EN	(1<<7)
#define   MEMINT_CONT_BUSY_EN	(1<<6)
#define   MEMINT_AVG_BUSY_EN	(1<<5)
#define   MEMINT_EVAL_CHG_EN	(1<<4)
#define   MEMINT_MON_IDLE_EN	(1<<3)
#define   MEMINT_UP_EVAL_EN	(1<<2)
#define   MEMINT_DOWN_EVAL_EN	(1<<1)
#define   MEMINT_SW_CMD_EN	(1<<0)
#define MEMINTRSTR		0x11182 /* 16 bits */
#define   MEM_RSEXIT_MASK	0xc000
#define   MEM_RSEXIT_SHIFT	14
#define   MEM_CONT_BUSY_MASK	0x3000
#define   MEM_CONT_BUSY_SHIFT	12
#define   MEM_AVG_BUSY_MASK	0x0c00
#define   MEM_AVG_BUSY_SHIFT	10
#define   MEM_EVAL_CHG_MASK	0x0300
#define   MEM_EVAL_BUSY_SHIFT	8
#define   MEM_MON_IDLE_MASK	0x00c0
#define   MEM_MON_IDLE_SHIFT	6
#define   MEM_UP_EVAL_MASK	0x0030
#define   MEM_UP_EVAL_SHIFT	4
#define   MEM_DOWN_EVAL_MASK	0x000c
#define   MEM_DOWN_EVAL_SHIFT	2
#define   MEM_SW_CMD_MASK	0x0003
#define   MEM_INT_STEER_GFX	0
#define   MEM_INT_STEER_CMR	1
#define   MEM_INT_STEER_SMI	2
#define   MEM_INT_STEER_SCI	3
#define MEMINTRSTS		0x11184
#define   MEMINT_RSEXIT		(1<<7)
#define   MEMINT_CONT_BUSY	(1<<6)
#define   MEMINT_AVG_BUSY	(1<<5)
#define   MEMINT_EVAL_CHG	(1<<4)
#define   MEMINT_MON_IDLE	(1<<3)
#define   MEMINT_UP_EVAL	(1<<2)
#define   MEMINT_DOWN_EVAL	(1<<1)
#define   MEMINT_SW_CMD		(1<<0)
#define MEMMODECTL		0x11190
#define   MEMMODE_BOOST_EN	(1<<31)
#define   MEMMODE_BOOST_FREQ_MASK 0x0f000000 /* jitter for boost, 0-15 */
#define   MEMMODE_BOOST_FREQ_SHIFT 24
#define   MEMMODE_IDLE_MODE_MASK 0x00030000
#define   MEMMODE_IDLE_MODE_SHIFT 16
#define   MEMMODE_IDLE_MODE_EVAL 0
#define   MEMMODE_IDLE_MODE_CONT 1
#define   MEMMODE_HWIDLE_EN	(1<<15)
#define   MEMMODE_SWMODE_EN	(1<<14)
#define   MEMMODE_RCLK_GATE	(1<<13)
#define   MEMMODE_HW_UPDATE	(1<<12)
#define   MEMMODE_FSTART_MASK	0x00000f00 /* starting jitter, 0-15 */
#define   MEMMODE_FSTART_SHIFT	8
#define   MEMMODE_FMAX_MASK	0x000000f0 /* max jitter, 0-15 */
#define   MEMMODE_FMAX_SHIFT	4
#define   MEMMODE_FMIN_MASK	0x0000000f /* min jitter, 0-15 */
#define RCBMAXAVG		0x1119c
#define MEMSWCTL2		0x1119e /* Cantiga only */
#define   SWMEMCMD_RENDER_OFF	(0 << 13)
#define   SWMEMCMD_RENDER_ON	(1 << 13)
#define   SWMEMCMD_SWFREQ	(2 << 13)
#define   SWMEMCMD_TARVID	(3 << 13)
#define   SWMEMCMD_VRM_OFF	(4 << 13)
#define   SWMEMCMD_VRM_ON	(5 << 13)
#define   CMDSTS		(1<<12)
#define   SFCAVM		(1<<11)
#define   SWFREQ_MASK		0x0380 /* P0-7 */
#define   SWFREQ_SHIFT		7
#define   TARVID_MASK		0x001f
#define MEMSTAT_CTG		0x111a0
#define RCBMINAVG		0x111a0
#define RCUPEI			0x111b0
#define RCDNEI			0x111b4
#define RSTDBYCTL		0x111b8
#define   RS1EN			(1<<31)
#define   RS2EN			(1<<30)
#define   RS3EN			(1<<29)
#define   D3RS3EN		(1<<28) /* Display D3 imlies RS3 */
#define   SWPROMORSX		(1<<27) /* RSx promotion timers ignored */
#define   RCWAKERW		(1<<26) /* Resetwarn from PCH causes wakeup */
#define   DPRSLPVREN		(1<<25) /* Fast voltage ramp enable */
#define   GFXTGHYST		(1<<24) /* Hysteresis to allow trunk gating */
#define   RCX_SW_EXIT		(1<<23) /* Leave RSx and prevent re-entry */
#define   RSX_STATUS_MASK	(7<<20)
#define   RSX_STATUS_ON		(0<<20)
#define   RSX_STATUS_RC1	(1<<20)
#define   RSX_STATUS_RC1E	(2<<20)
#define   RSX_STATUS_RS1	(3<<20)
#define   RSX_STATUS_RS2	(4<<20) /* aka rc6 */
#define   RSX_STATUS_RSVD	(5<<20) /* deep rc6 unsupported on ilk */
#define   RSX_STATUS_RS3	(6<<20) /* rs3 unsupported on ilk */
#define   RSX_STATUS_RSVD2	(7<<20)
#define   UWRCRSXE		(1<<19) /* wake counter limit prevents rsx */
#define   RSCRP			(1<<18) /* rs requests control on rs1/2 reqs */
#define   JRSC			(1<<17) /* rsx coupled to cpu c-state */
#define   RS2INC0		(1<<16) /* allow rs2 in cpu c0 */
#define   RS1CONTSAV_MASK	(3<<14)
#define   RS1CONTSAV_NO_RS1	(0<<14) /* rs1 doesn't save/restore context */
#define   RS1CONTSAV_RSVD	(1<<14)
#define   RS1CONTSAV_SAVE_RS1	(2<<14) /* rs1 saves context */
#define   RS1CONTSAV_FULL_RS1	(3<<14) /* rs1 saves and restores context */
#define   NORMSLEXLAT_MASK	(3<<12)
#define   SLOW_RS123		(0<<12)
#define   SLOW_RS23		(1<<12)
#define   SLOW_RS3		(2<<12)
#define   NORMAL_RS123		(3<<12)
#define   RCMODE_TIMEOUT	(1<<11) /* 0 is eval interval method */
#define   IMPROMOEN		(1<<10) /* promo is immediate or delayed until next idle interval (only for timeout method above) */
#define   RCENTSYNC		(1<<9) /* rs coupled to cpu c-state (3/6/7) */
#define   STATELOCK		(1<<7) /* locked to rs_cstate if 0 */
#define   RS_CSTATE_MASK	(3<<4)
#define   RS_CSTATE_C367_RS1	(0<<4)
#define   RS_CSTATE_C36_RS1_C7_RS2 (1<<4)
#define   RS_CSTATE_RSVD	(2<<4)
#define   RS_CSTATE_C367_RS2	(3<<4)
#define   REDSAVES		(1<<3) /* no context save if was idle during rs0 */
#define   REDRESTORES		(1<<2) /* no restore if was idle during rs0 */
#define VIDCTL			0x111c0
#define VIDSTS			0x111c8
#define VIDSTART		0x111cc /* 8 bits */
#define MEMSTAT_ILK			0x111f8
#define   MEMSTAT_VID_MASK	0x7f00
#define   MEMSTAT_VID_SHIFT	8
#define   MEMSTAT_PSTATE_MASK	0x00f8
#define   MEMSTAT_PSTATE_SHIFT  3
#define   MEMSTAT_MON_ACTV	(1<<2)
#define   MEMSTAT_SRC_CTL_MASK	0x0003
#define   MEMSTAT_SRC_CTL_CORE	0
#define   MEMSTAT_SRC_CTL_TRB	1
#define   MEMSTAT_SRC_CTL_THM	2
#define   MEMSTAT_SRC_CTL_STDBY 3
#define RCPREVBSYTUPAVG		0x113b8
#define RCPREVBSYTDNAVG		0x113bc
#define PMMISC			0x11214
#define   MCPPCE_EN		(1<<0) /* enable PM_MSG from PCH->MPC */
#define SDEW			0x1124c
#define CSIEW0			0x11250
#define CSIEW1			0x11254
#define CSIEW2			0x11258
#define PEW			0x1125c
#define DEW			0x11270
#define MCHAFE			0x112c0
#define CSIEC			0x112e0
#define DMIEC			0x112e4
#define DDREC			0x112e8
#define PEG0EC			0x112ec
#define PEG1EC			0x112f0
#define GFXEC			0x112f4
#define RPPREVBSYTUPAVG		0x113b8
#define RPPREVBSYTDNAVG		0x113bc
#define ECR			0x11600
#define   ECR_GPFE		(1<<31)
#define   ECR_IMONE		(1<<30)
#define   ECR_CAP_MASK		0x0000001f /* Event range, 0-31 */
#define OGW0			0x11608
#define OGW1			0x1160c
#define EG0			0x11610
#define EG1			0x11614
#define EG2			0x11618
#define EG3			0x1161c
#define EG4			0x11620
#define EG5			0x11624
#define EG6			0x11628
#define EG7			0x1162c
#define PXW			0x11664
#define PXWL			0x11680
#define LCFUSE02		0x116c0
#define   LCFUSE_HIV_MASK	0x000000ff
#define CSIPLL0			0x12c10
#define DDRMPLL1		0X12c20
#define PEG_BAND_GAP_DATA	0x14d68

#define GEN6_GT_THREAD_STATUS_REG 0x13805c
#define GEN6_GT_THREAD_STATUS_CORE_MASK 0x7

#define GEN6_GT_PERF_STATUS	(MCHBAR_MIRROR_BASE_SNB + 0x5948)
#define GEN6_RP_STATE_LIMITS	(MCHBAR_MIRROR_BASE_SNB + 0x5994)
#define GEN6_RP_STATE_CAP	(MCHBAR_MIRROR_BASE_SNB + 0x5998)

/*
 * Logical Context regs
 */
#define CCID			0x2180
#define   CCID_EN		(1<<0)
/*
 * Notes on SNB/IVB/VLV context size:
 * - Power context is saved elsewhere (LLC or stolen)
 * - Ring/execlist context is saved on SNB, not on IVB
 * - Extended context size already includes render context size
 * - We always need to follow the extended context size.
 *   SNB BSpec has comments indicating that we should use the
 *   render context size instead if execlists are disabled, but
 *   based on empirical testing that's just nonsense.
 * - Pipelined/VF state is saved on SNB/IVB respectively
 * - GT1 size just indicates how much of render context
 *   doesn't need saving on GT1
 */
#define CXT_SIZE		0x21a0
#define GEN6_CXT_POWER_SIZE(cxt_reg)	((cxt_reg >> 24) & 0x3f)
#define GEN6_CXT_RING_SIZE(cxt_reg)	((cxt_reg >> 18) & 0x3f)
#define GEN6_CXT_RENDER_SIZE(cxt_reg)	((cxt_reg >> 12) & 0x3f)
#define GEN6_CXT_EXTENDED_SIZE(cxt_reg)	((cxt_reg >> 6) & 0x3f)
#define GEN6_CXT_PIPELINE_SIZE(cxt_reg)	((cxt_reg >> 0) & 0x3f)
#define GEN6_CXT_TOTAL_SIZE(cxt_reg)	(GEN6_CXT_RING_SIZE(cxt_reg) + \
					GEN6_CXT_EXTENDED_SIZE(cxt_reg) + \
					GEN6_CXT_PIPELINE_SIZE(cxt_reg))
#define GEN7_CXT_SIZE		0x21a8
#define GEN7_CXT_POWER_SIZE(ctx_reg)	((ctx_reg >> 25) & 0x7f)
#define GEN7_CXT_RING_SIZE(ctx_reg)	((ctx_reg >> 22) & 0x7)
#define GEN7_CXT_RENDER_SIZE(ctx_reg)	((ctx_reg >> 16) & 0x3f)
#define GEN7_CXT_EXTENDED_SIZE(ctx_reg)	((ctx_reg >> 9) & 0x7f)
#define GEN7_CXT_GT1_SIZE(ctx_reg)	((ctx_reg >> 6) & 0x7)
#define GEN7_CXT_VFSTATE_SIZE(ctx_reg)	((ctx_reg >> 0) & 0x3f)
#define GEN7_CXT_TOTAL_SIZE(ctx_reg)	(GEN7_CXT_EXTENDED_SIZE(ctx_reg) + \
					 GEN7_CXT_VFSTATE_SIZE(ctx_reg))
/* Haswell does have the CXT_SIZE register however it does not appear to be
 * valid. Now, docs explain in dwords what is in the context object. The full
 * size is 70720 bytes, however, the power context and execlist context will
 * never be saved (power context is stored elsewhere, and execlists don't work
 * on HSW) - so the final size is 66944 bytes, which rounds to 17 pages.
 */
#define HSW_CXT_TOTAL_SIZE		(17 * PAGE_SIZE)
/* Same as Haswell, but 72064 bytes now. */
#define GEN8_CXT_TOTAL_SIZE		(18 * PAGE_SIZE)

#define CHV_CLK_CTL1			0x101100
#define VLV_CLK_CTL2			0x101104
#define   CLK_CTL2_CZCOUNT_30NS_SHIFT	28

/*
 * Overlay regs
 */

#define OVADD			0x30000
#define DOVSTA			0x30008
#define OC_BUF			(0x3<<20)
#define OGAMC5			0x30010
#define OGAMC4			0x30014
#define OGAMC3			0x30018
#define OGAMC2			0x3001c
#define OGAMC1			0x30020
#define OGAMC0			0x30024

/*
 * Display engine regs
 */

/* Pipe A CRC regs */
#define _PIPE_CRC_CTL_A			0x60050
#define   PIPE_CRC_ENABLE		(1 << 31)
/* ivb+ source selection */
#define   PIPE_CRC_SOURCE_PRIMARY_IVB	(0 << 29)
#define   PIPE_CRC_SOURCE_SPRITE_IVB	(1 << 29)
#define   PIPE_CRC_SOURCE_PF_IVB	(2 << 29)
/* ilk+ source selection */
#define   PIPE_CRC_SOURCE_PRIMARY_ILK	(0 << 28)
#define   PIPE_CRC_SOURCE_SPRITE_ILK	(1 << 28)
#define   PIPE_CRC_SOURCE_PIPE_ILK	(2 << 28)
/* embedded DP port on the north display block, reserved on ivb */
#define   PIPE_CRC_SOURCE_PORT_A_ILK	(4 << 28)
#define   PIPE_CRC_SOURCE_FDI_ILK	(5 << 28) /* reserved on ivb */
/* vlv source selection */
#define   PIPE_CRC_SOURCE_PIPE_VLV	(0 << 27)
#define   PIPE_CRC_SOURCE_HDMIB_VLV	(1 << 27)
#define   PIPE_CRC_SOURCE_HDMIC_VLV	(2 << 27)
/* with DP port the pipe source is invalid */
#define   PIPE_CRC_SOURCE_DP_D_VLV	(3 << 27)
#define   PIPE_CRC_SOURCE_DP_B_VLV	(6 << 27)
#define   PIPE_CRC_SOURCE_DP_C_VLV	(7 << 27)
/* gen3+ source selection */
#define   PIPE_CRC_SOURCE_PIPE_I9XX	(0 << 28)
#define   PIPE_CRC_SOURCE_SDVOB_I9XX	(1 << 28)
#define   PIPE_CRC_SOURCE_SDVOC_I9XX	(2 << 28)
/* with DP/TV port the pipe source is invalid */
#define   PIPE_CRC_SOURCE_DP_D_G4X	(3 << 28)
#define   PIPE_CRC_SOURCE_TV_PRE	(4 << 28)
#define   PIPE_CRC_SOURCE_TV_POST	(5 << 28)
#define   PIPE_CRC_SOURCE_DP_B_G4X	(6 << 28)
#define   PIPE_CRC_SOURCE_DP_C_G4X	(7 << 28)
/* gen2 doesn't have source selection bits */
#define   PIPE_CRC_INCLUDE_BORDER_I8XX	(1 << 30)

#define _PIPE_CRC_RES_1_A_IVB		0x60064
#define _PIPE_CRC_RES_2_A_IVB		0x60068
#define _PIPE_CRC_RES_3_A_IVB		0x6006c
#define _PIPE_CRC_RES_4_A_IVB		0x60070
#define _PIPE_CRC_RES_5_A_IVB		0x60074

#define _PIPE_CRC_RES_RED_A		0x60060
#define _PIPE_CRC_RES_GREEN_A		0x60064
#define _PIPE_CRC_RES_BLUE_A		0x60068
#define _PIPE_CRC_RES_RES1_A_I915	0x6006c
#define _PIPE_CRC_RES_RES2_A_G4X	0x60080

/* Pipe B CRC regs */
#define _PIPE_CRC_RES_1_B_IVB		0x61064
#define _PIPE_CRC_RES_2_B_IVB		0x61068
#define _PIPE_CRC_RES_3_B_IVB		0x6106c
#define _PIPE_CRC_RES_4_B_IVB		0x61070
#define _PIPE_CRC_RES_5_B_IVB		0x61074

#define PIPE_CRC_CTL(pipe) _TRANSCODER2(pipe, _PIPE_CRC_CTL_A)
#define PIPE_CRC_RES_1_IVB(pipe)	\
	_TRANSCODER2(pipe, _PIPE_CRC_RES_1_A_IVB)
#define PIPE_CRC_RES_2_IVB(pipe)	\
	_TRANSCODER2(pipe, _PIPE_CRC_RES_2_A_IVB)
#define PIPE_CRC_RES_3_IVB(pipe)	\
	_TRANSCODER2(pipe, _PIPE_CRC_RES_3_A_IVB)
#define PIPE_CRC_RES_4_IVB(pipe)	\
	_TRANSCODER2(pipe, _PIPE_CRC_RES_4_A_IVB)
#define PIPE_CRC_RES_5_IVB(pipe)	\
	_TRANSCODER2(pipe, _PIPE_CRC_RES_5_A_IVB)

#define PIPE_CRC_RES_RED(pipe) \
	_TRANSCODER2(pipe, _PIPE_CRC_RES_RED_A)
#define PIPE_CRC_RES_GREEN(pipe) \
	_TRANSCODER2(pipe, _PIPE_CRC_RES_GREEN_A)
#define PIPE_CRC_RES_BLUE(pipe) \
	_TRANSCODER2(pipe, _PIPE_CRC_RES_BLUE_A)
#define PIPE_CRC_RES_RES1_I915(pipe) \
	_TRANSCODER2(pipe, _PIPE_CRC_RES_RES1_A_I915)
#define PIPE_CRC_RES_RES2_G4X(pipe) \
	_TRANSCODER2(pipe, _PIPE_CRC_RES_RES2_A_G4X)

/* Pipe A timing regs */
#define _HTOTAL_A	0x60000
#define _HBLANK_A	0x60004
#define _HSYNC_A	0x60008
#define _VTOTAL_A	0x6000c
#define _VBLANK_A	0x60010
#define _VSYNC_A	0x60014
#define _PIPEASRC	0x6001c
#define _BCLRPAT_A	0x60020
#define _VSYNCSHIFT_A	0x60028
#define _PIPE_MULT_A	0x6002c

/* Pipe B timing regs */
#define _HTOTAL_B	0x61000
#define _HBLANK_B	0x61004
#define _HSYNC_B	0x61008
#define _VTOTAL_B	0x6100c
#define _VBLANK_B	0x61010
#define _VSYNC_B	0x61014
#define _PIPEBSRC	0x6101c
#define _BCLRPAT_B	0x61020
#define _VSYNCSHIFT_B	0x61028
#define _PIPE_MULT_B	0x6102c

#define TRANSCODER_A_OFFSET 0x60000
#define TRANSCODER_B_OFFSET 0x61000
#define TRANSCODER_C_OFFSET 0x62000
#define CHV_TRANSCODER_C_OFFSET 0x63000
#define TRANSCODER_EDP_OFFSET 0x6f000

#define _TRANSCODER2(pipe, reg) (dev_priv->info.trans_offsets[(pipe)] - \
	dev_priv->info.trans_offsets[TRANSCODER_A] + (reg) + \
	dev_priv->info.display_mmio_offset)

#define HTOTAL(trans) _TRANSCODER2(trans, _HTOTAL_A)
#define HBLANK(trans) _TRANSCODER2(trans, _HBLANK_A)
#define HSYNC(trans) _TRANSCODER2(trans, _HSYNC_A)
#define VTOTAL(trans) _TRANSCODER2(trans, _VTOTAL_A)
#define VBLANK(trans) _TRANSCODER2(trans, _VBLANK_A)
#define VSYNC(trans) _TRANSCODER2(trans, _VSYNC_A)
#define BCLRPAT(trans) _TRANSCODER2(trans, _BCLRPAT_A)
#define VSYNCSHIFT(trans) _TRANSCODER2(trans, _VSYNCSHIFT_A)
#define PIPESRC(trans) _TRANSCODER2(trans, _PIPEASRC)
#define PIPE_MULT(trans) _TRANSCODER2(trans, _PIPE_MULT_A)

/* VLV eDP PSR registers */
#define _PSRCTLA				(VLV_DISPLAY_BASE + 0x60090)
#define _PSRCTLB				(VLV_DISPLAY_BASE + 0x61090)
#define  VLV_EDP_PSR_ENABLE			(1<<0)
#define  VLV_EDP_PSR_RESET			(1<<1)
#define  VLV_EDP_PSR_MODE_MASK			(7<<2)
#define  VLV_EDP_PSR_MODE_HW_TIMER		(1<<3)
#define  VLV_EDP_PSR_MODE_SW_TIMER		(1<<2)
#define  VLV_EDP_PSR_SINGLE_FRAME_UPDATE	(1<<7)
#define  VLV_EDP_PSR_ACTIVE_ENTRY		(1<<8)
#define  VLV_EDP_PSR_SRC_TRANSMITTER_STATE	(1<<9)
#define  VLV_EDP_PSR_DBL_FRAME			(1<<10)
#define  VLV_EDP_PSR_FRAME_COUNT_MASK		(0xff<<16)
#define  VLV_EDP_PSR_IDLE_FRAME_SHIFT		16
#define VLV_PSRCTL(pipe) _PIPE(pipe, _PSRCTLA, _PSRCTLB)

#define _VSCSDPA			(VLV_DISPLAY_BASE + 0x600a0)
#define _VSCSDPB			(VLV_DISPLAY_BASE + 0x610a0)
#define  VLV_EDP_PSR_SDP_FREQ_MASK	(3<<30)
#define  VLV_EDP_PSR_SDP_FREQ_ONCE	(1<<31)
#define  VLV_EDP_PSR_SDP_FREQ_EVFRAME	(1<<30)
#define VLV_VSCSDP(pipe)	_PIPE(pipe, _VSCSDPA, _VSCSDPB)

#define _PSRSTATA			(VLV_DISPLAY_BASE + 0x60094)
#define _PSRSTATB			(VLV_DISPLAY_BASE + 0x61094)
#define  VLV_EDP_PSR_LAST_STATE_MASK	(7<<3)
#define  VLV_EDP_PSR_CURR_STATE_MASK	7
#define  VLV_EDP_PSR_DISABLED		(0<<0)
#define  VLV_EDP_PSR_INACTIVE		(1<<0)
#define  VLV_EDP_PSR_IN_TRANS_TO_ACTIVE	(2<<0)
#define  VLV_EDP_PSR_ACTIVE_NORFB_UP	(3<<0)
#define  VLV_EDP_PSR_ACTIVE_SF_UPDATE	(4<<0)
#define  VLV_EDP_PSR_EXIT		(5<<0)
#define  VLV_EDP_PSR_IN_TRANS		(1<<7)
#define VLV_PSRSTAT(pipe) _PIPE(pipe, _PSRSTATA, _PSRSTATB)

/* HSW+ eDP PSR registers */
#define EDP_PSR_BASE(dev)                       (IS_HASWELL(dev) ? 0x64800 : 0x6f800)
#define EDP_PSR_CTL(dev)			(EDP_PSR_BASE(dev) + 0)
#define   EDP_PSR_ENABLE			(1<<31)
#define   BDW_PSR_SINGLE_FRAME			(1<<30)
#define   EDP_PSR_LINK_DISABLE			(0<<27)
#define   EDP_PSR_LINK_STANDBY			(1<<27)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_MASK	(3<<25)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_8_LINES	(0<<25)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_4_LINES	(1<<25)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_2_LINES	(2<<25)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_0_LINES	(3<<25)
#define   EDP_PSR_MAX_SLEEP_TIME_SHIFT		20
#define   EDP_PSR_SKIP_AUX_EXIT			(1<<12)
#define   EDP_PSR_TP1_TP2_SEL			(0<<11)
#define   EDP_PSR_TP1_TP3_SEL			(1<<11)
#define   EDP_PSR_TP2_TP3_TIME_500us		(0<<8)
#define   EDP_PSR_TP2_TP3_TIME_100us		(1<<8)
#define   EDP_PSR_TP2_TP3_TIME_2500us		(2<<8)
#define   EDP_PSR_TP2_TP3_TIME_0us		(3<<8)
#define   EDP_PSR_TP1_TIME_500us		(0<<4)
#define   EDP_PSR_TP1_TIME_100us		(1<<4)
#define   EDP_PSR_TP1_TIME_2500us		(2<<4)
#define   EDP_PSR_TP1_TIME_0us			(3<<4)
#define   EDP_PSR_IDLE_FRAME_SHIFT		0

#define EDP_PSR_AUX_CTL(dev)			(EDP_PSR_BASE(dev) + 0x10)
#define EDP_PSR_AUX_DATA1(dev)			(EDP_PSR_BASE(dev) + 0x14)
#define EDP_PSR_AUX_DATA2(dev)			(EDP_PSR_BASE(dev) + 0x18)
#define EDP_PSR_AUX_DATA3(dev)			(EDP_PSR_BASE(dev) + 0x1c)
#define EDP_PSR_AUX_DATA4(dev)			(EDP_PSR_BASE(dev) + 0x20)
#define EDP_PSR_AUX_DATA5(dev)			(EDP_PSR_BASE(dev) + 0x24)

#define EDP_PSR_STATUS_CTL(dev)			(EDP_PSR_BASE(dev) + 0x40)
#define   EDP_PSR_STATUS_STATE_MASK		(7<<29)
#define   EDP_PSR_STATUS_STATE_IDLE		(0<<29)
#define   EDP_PSR_STATUS_STATE_SRDONACK		(1<<29)
#define   EDP_PSR_STATUS_STATE_SRDENT		(2<<29)
#define   EDP_PSR_STATUS_STATE_BUFOFF		(3<<29)
#define   EDP_PSR_STATUS_STATE_BUFON		(4<<29)
#define   EDP_PSR_STATUS_STATE_AUXACK		(5<<29)
#define   EDP_PSR_STATUS_STATE_SRDOFFACK	(6<<29)
#define   EDP_PSR_STATUS_LINK_MASK		(3<<26)
#define   EDP_PSR_STATUS_LINK_FULL_OFF		(0<<26)
#define   EDP_PSR_STATUS_LINK_FULL_ON		(1<<26)
#define   EDP_PSR_STATUS_LINK_STANDBY		(2<<26)
#define   EDP_PSR_STATUS_MAX_SLEEP_TIMER_SHIFT	20
#define   EDP_PSR_STATUS_MAX_SLEEP_TIMER_MASK	0x1f
#define   EDP_PSR_STATUS_COUNT_SHIFT		16
#define   EDP_PSR_STATUS_COUNT_MASK		0xf
#define   EDP_PSR_STATUS_AUX_ERROR		(1<<15)
#define   EDP_PSR_STATUS_AUX_SENDING		(1<<12)
#define   EDP_PSR_STATUS_SENDING_IDLE		(1<<9)
#define   EDP_PSR_STATUS_SENDING_TP2_TP3	(1<<8)
#define   EDP_PSR_STATUS_SENDING_TP1		(1<<4)
#define   EDP_PSR_STATUS_IDLE_MASK		0xf

#define EDP_PSR_PERF_CNT(dev)		(EDP_PSR_BASE(dev) + 0x44)
#define   EDP_PSR_PERF_CNT_MASK		0xffffff

#define EDP_PSR_DEBUG_CTL(dev)		(EDP_PSR_BASE(dev) + 0x60)
#define   EDP_PSR_DEBUG_MASK_LPSP	(1<<27)
#define   EDP_PSR_DEBUG_MASK_MEMUP	(1<<26)
#define   EDP_PSR_DEBUG_MASK_HPD	(1<<25)

/* VGA port control */
#define ADPA			0x61100
#define PCH_ADPA                0xe1100
#define VLV_ADPA		(VLV_DISPLAY_BASE + ADPA)

#define   ADPA_DAC_ENABLE	(1<<31)
#define   ADPA_DAC_DISABLE	0
#define   ADPA_PIPE_SELECT_MASK	(1<<30)
#define   ADPA_PIPE_A_SELECT	0
#define   ADPA_PIPE_B_SELECT	(1<<30)
#define   ADPA_PIPE_SELECT(pipe) ((pipe) << 30)
/* CPT uses bits 29:30 for pch transcoder select */
#define   ADPA_CRT_HOTPLUG_MASK  0x03ff0000 /* bit 25-16 */
#define   ADPA_CRT_HOTPLUG_MONITOR_NONE  (0<<24)
#define   ADPA_CRT_HOTPLUG_MONITOR_MASK  (3<<24)
#define   ADPA_CRT_HOTPLUG_MONITOR_COLOR (3<<24)
#define   ADPA_CRT_HOTPLUG_MONITOR_MONO  (2<<24)
#define   ADPA_CRT_HOTPLUG_ENABLE        (1<<23)
#define   ADPA_CRT_HOTPLUG_PERIOD_64     (0<<22)
#define   ADPA_CRT_HOTPLUG_PERIOD_128    (1<<22)
#define   ADPA_CRT_HOTPLUG_WARMUP_5MS    (0<<21)
#define   ADPA_CRT_HOTPLUG_WARMUP_10MS   (1<<21)
#define   ADPA_CRT_HOTPLUG_SAMPLE_2S     (0<<20)
#define   ADPA_CRT_HOTPLUG_SAMPLE_4S     (1<<20)
#define   ADPA_CRT_HOTPLUG_VOLTAGE_40    (0<<18)
#define   ADPA_CRT_HOTPLUG_VOLTAGE_50    (1<<18)
#define   ADPA_CRT_HOTPLUG_VOLTAGE_60    (2<<18)
#define   ADPA_CRT_HOTPLUG_VOLTAGE_70    (3<<18)
#define   ADPA_CRT_HOTPLUG_VOLREF_325MV  (0<<17)
#define   ADPA_CRT_HOTPLUG_VOLREF_475MV  (1<<17)
#define   ADPA_CRT_HOTPLUG_FORCE_TRIGGER (1<<16)
#define   ADPA_USE_VGA_HVPOLARITY (1<<15)
#define   ADPA_SETS_HVPOLARITY	0
#define   ADPA_VSYNC_CNTL_DISABLE (1<<10)
#define   ADPA_VSYNC_CNTL_ENABLE 0
#define   ADPA_HSYNC_CNTL_DISABLE (1<<11)
#define   ADPA_HSYNC_CNTL_ENABLE 0
#define   ADPA_VSYNC_ACTIVE_HIGH (1<<4)
#define   ADPA_VSYNC_ACTIVE_LOW	0
#define   ADPA_HSYNC_ACTIVE_HIGH (1<<3)
#define   ADPA_HSYNC_ACTIVE_LOW	0
#define   ADPA_DPMS_MASK	(~(3<<10))
#define   ADPA_DPMS_ON		(0<<10)
#define   ADPA_DPMS_SUSPEND	(1<<10)
#define   ADPA_DPMS_STANDBY	(2<<10)
#define   ADPA_DPMS_OFF		(3<<10)


/* Hotplug control (945+ only) */
#define PORT_HOTPLUG_EN		(dev_priv->info.display_mmio_offset + 0x61110)
#define   PORTB_HOTPLUG_INT_EN			(1 << 29)
#define   PORTC_HOTPLUG_INT_EN			(1 << 28)
#define   PORTD_HOTPLUG_INT_EN			(1 << 27)
#define   SDVOB_HOTPLUG_INT_EN			(1 << 26)
#define   SDVOC_HOTPLUG_INT_EN			(1 << 25)
#define   TV_HOTPLUG_INT_EN			(1 << 18)
#define   CRT_HOTPLUG_INT_EN			(1 << 9)
#define HOTPLUG_INT_EN_MASK			(PORTB_HOTPLUG_INT_EN | \
						 PORTC_HOTPLUG_INT_EN | \
						 PORTD_HOTPLUG_INT_EN | \
						 SDVOC_HOTPLUG_INT_EN | \
						 SDVOB_HOTPLUG_INT_EN | \
						 CRT_HOTPLUG_INT_EN)
#define   CRT_HOTPLUG_FORCE_DETECT		(1 << 3)
#define CRT_HOTPLUG_ACTIVATION_PERIOD_32	(0 << 8)
/* must use period 64 on GM45 according to docs */
#define CRT_HOTPLUG_ACTIVATION_PERIOD_64	(1 << 8)
#define CRT_HOTPLUG_DAC_ON_TIME_2M		(0 << 7)
#define CRT_HOTPLUG_DAC_ON_TIME_4M		(1 << 7)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_40		(0 << 5)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_50		(1 << 5)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_60		(2 << 5)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_70		(3 << 5)
#define CRT_HOTPLUG_VOLTAGE_COMPARE_MASK	(3 << 5)
#define CRT_HOTPLUG_DETECT_DELAY_1G		(0 << 4)
#define CRT_HOTPLUG_DETECT_DELAY_2G		(1 << 4)
#define CRT_HOTPLUG_DETECT_VOLTAGE_325MV	(0 << 2)
#define CRT_HOTPLUG_DETECT_VOLTAGE_475MV	(1 << 2)

#define PORT_HOTPLUG_STAT	(dev_priv->info.display_mmio_offset + 0x61114)
/*
 * HDMI/DP bits are gen4+
 *
 * WARNING: Bspec for hpd status bits on gen4 seems to be completely confused.
 * Please check the detailed lore in the commit message for for experimental
 * evidence.
 */
#define   PORTD_HOTPLUG_LIVE_STATUS_G4X		(1 << 29)
#define   PORTC_HOTPLUG_LIVE_STATUS_G4X		(1 << 28)
#define   PORTB_HOTPLUG_LIVE_STATUS_G4X		(1 << 27)
/* VLV DP/HDMI bits again match Bspec */
#define   PORTD_HOTPLUG_LIVE_STATUS_VLV		(1 << 27)
#define   PORTC_HOTPLUG_LIVE_STATUS_VLV		(1 << 28)
#define   PORTB_HOTPLUG_LIVE_STATUS_VLV		(1 << 29)
#define   PORTD_HOTPLUG_INT_STATUS		(3 << 21)
#define   PORTD_HOTPLUG_INT_LONG_PULSE		(2 << 21)
#define   PORTD_HOTPLUG_INT_SHORT_PULSE		(1 << 21)
#define   PORTC_HOTPLUG_INT_STATUS		(3 << 19)
#define   PORTC_HOTPLUG_INT_LONG_PULSE		(2 << 19)
#define   PORTC_HOTPLUG_INT_SHORT_PULSE		(1 << 19)
#define   PORTB_HOTPLUG_INT_STATUS		(3 << 17)
#define   PORTB_HOTPLUG_INT_LONG_PULSE		(2 << 17)
#define   PORTB_HOTPLUG_INT_SHORT_PLUSE		(1 << 17)
/* CRT/TV common between gen3+ */
#define   CRT_HOTPLUG_INT_STATUS		(1 << 11)
#define   TV_HOTPLUG_INT_STATUS			(1 << 10)
#define   CRT_HOTPLUG_MONITOR_MASK		(3 << 8)
#define   CRT_HOTPLUG_MONITOR_COLOR		(3 << 8)
#define   CRT_HOTPLUG_MONITOR_MONO		(2 << 8)
#define   CRT_HOTPLUG_MONITOR_NONE		(0 << 8)
#define   DP_AUX_CHANNEL_D_INT_STATUS_G4X	(1 << 6)
#define   DP_AUX_CHANNEL_C_INT_STATUS_G4X	(1 << 5)
#define   DP_AUX_CHANNEL_B_INT_STATUS_G4X	(1 << 4)
#define   DP_AUX_CHANNEL_MASK_INT_STATUS_G4X	(7 << 4)

/* SDVO is different across gen3/4 */
#define   SDVOC_HOTPLUG_INT_STATUS_G4X		(1 << 3)
#define   SDVOB_HOTPLUG_INT_STATUS_G4X		(1 << 2)
/*
 * Bspec seems to be seriously misleaded about the SDVO hpd bits on i965g/gm,
 * since reality corrobates that they're the same as on gen3. But keep these
 * bits here (and the comment!) to help any other lost wanderers back onto the
 * right tracks.
 */
#define   SDVOC_HOTPLUG_INT_STATUS_I965		(3 << 4)
#define   SDVOB_HOTPLUG_INT_STATUS_I965		(3 << 2)
#define   SDVOC_HOTPLUG_INT_STATUS_I915		(1 << 7)
#define   SDVOB_HOTPLUG_INT_STATUS_I915		(1 << 6)
#define   HOTPLUG_INT_STATUS_G4X		(CRT_HOTPLUG_INT_STATUS | \
						 SDVOB_HOTPLUG_INT_STATUS_G4X | \
						 SDVOC_HOTPLUG_INT_STATUS_G4X | \
						 PORTB_HOTPLUG_INT_STATUS | \
						 PORTC_HOTPLUG_INT_STATUS | \
						 PORTD_HOTPLUG_INT_STATUS)

#define HOTPLUG_INT_STATUS_I915			(CRT_HOTPLUG_INT_STATUS | \
						 SDVOB_HOTPLUG_INT_STATUS_I915 | \
						 SDVOC_HOTPLUG_INT_STATUS_I915 | \
						 PORTB_HOTPLUG_INT_STATUS | \
						 PORTC_HOTPLUG_INT_STATUS | \
						 PORTD_HOTPLUG_INT_STATUS)

/* SDVO and HDMI port control.
 * The same register may be used for SDVO or HDMI */
#define GEN3_SDVOB	0x61140
#define GEN3_SDVOC	0x61160
#define GEN4_HDMIB	GEN3_SDVOB
#define GEN4_HDMIC	GEN3_SDVOC
#define CHV_HDMID	0x6116C
#define PCH_SDVOB	0xe1140
#define PCH_HDMIB	PCH_SDVOB
#define PCH_HDMIC	0xe1150
#define PCH_HDMID	0xe1160

#define PORT_DFT_I9XX				0x61150
#define   DC_BALANCE_RESET			(1 << 25)
#define PORT_DFT2_G4X		(dev_priv->info.display_mmio_offset + 0x61154)
#define   DC_BALANCE_RESET_VLV			(1 << 31)
#define   PIPE_SCRAMBLE_RESET_MASK		(0x3 << 0)
#define   PIPE_B_SCRAMBLE_RESET			(1 << 1)
#define   PIPE_A_SCRAMBLE_RESET			(1 << 0)

/* Gen 3 SDVO bits: */
#define   SDVO_ENABLE				(1 << 31)
#define   SDVO_PIPE_SEL(pipe)			((pipe) << 30)
#define   SDVO_PIPE_SEL_MASK			(1 << 30)
#define   SDVO_PIPE_B_SELECT			(1 << 30)
#define   SDVO_STALL_SELECT			(1 << 29)
#define   SDVO_INTERRUPT_ENABLE			(1 << 26)
/*
 * 915G/GM SDVO pixel multiplier.
 * Programmed value is multiplier - 1, up to 5x.
 * \sa DPLL_MD_UDI_MULTIPLIER_MASK
 */
#define   SDVO_PORT_MULTIPLY_MASK		(7 << 23)
#define   SDVO_PORT_MULTIPLY_SHIFT		23
#define   SDVO_PHASE_SELECT_MASK		(15 << 19)
#define   SDVO_PHASE_SELECT_DEFAULT		(6 << 19)
#define   SDVO_CLOCK_OUTPUT_INVERT		(1 << 18)
#define   SDVOC_GANG_MODE			(1 << 16) /* Port C only */
#define   SDVO_BORDER_ENABLE			(1 << 7) /* SDVO only */
#define   SDVOB_PCIE_CONCURRENCY		(1 << 3) /* Port B only */
#define   SDVO_DETECTED				(1 << 2)
/* Bits to be preserved when writing */
#define   SDVOB_PRESERVE_MASK ((1 << 17) | (1 << 16) | (1 << 14) | \
			       SDVO_INTERRUPT_ENABLE)
#define   SDVOC_PRESERVE_MASK ((1 << 17) | SDVO_INTERRUPT_ENABLE)

/* Gen 4 SDVO/HDMI bits: */
#define   SDVO_COLOR_FORMAT_8bpc		(0 << 26)
#define   SDVO_COLOR_FORMAT_MASK		(7 << 26)
#define   SDVO_ENCODING_SDVO			(0 << 10)
#define   SDVO_ENCODING_HDMI			(2 << 10)
#define   HDMI_MODE_SELECT_HDMI			(1 << 9) /* HDMI only */
#define   HDMI_MODE_SELECT_DVI			(0 << 9) /* HDMI only */
#define   HDMI_COLOR_RANGE_16_235		(1 << 8) /* HDMI only */
#define   SDVO_AUDIO_ENABLE			(1 << 6)
/* VSYNC/HSYNC bits new with 965, default is to be set */
#define   SDVO_VSYNC_ACTIVE_HIGH		(1 << 4)
#define   SDVO_HSYNC_ACTIVE_HIGH		(1 << 3)

/* Gen 5 (IBX) SDVO/HDMI bits: */
#define   HDMI_COLOR_FORMAT_12bpc		(3 << 26) /* HDMI only */
#define   SDVOB_HOTPLUG_ENABLE			(1 << 23) /* SDVO only */

/* Gen 6 (CPT) SDVO/HDMI bits: */
#define   SDVO_PIPE_SEL_CPT(pipe)		((pipe) << 29)
#define   SDVO_PIPE_SEL_MASK_CPT		(3 << 29)

/* CHV SDVO/HDMI bits: */
#define   SDVO_PIPE_SEL_CHV(pipe)		((pipe) << 24)
#define   SDVO_PIPE_SEL_MASK_CHV		(3 << 24)


/* DVO port control */
#define DVOA			0x61120
#define DVOB			0x61140
#define DVOC			0x61160
#define   DVO_ENABLE			(1 << 31)
#define   DVO_PIPE_B_SELECT		(1 << 30)
#define   DVO_PIPE_STALL_UNUSED		(0 << 28)
#define   DVO_PIPE_STALL		(1 << 28)
#define   DVO_PIPE_STALL_TV		(2 << 28)
#define   DVO_PIPE_STALL_MASK		(3 << 28)
#define   DVO_USE_VGA_SYNC		(1 << 15)
#define   DVO_DATA_ORDER_I740		(0 << 14)
#define   DVO_DATA_ORDER_FP		(1 << 14)
#define   DVO_VSYNC_DISABLE		(1 << 11)
#define   DVO_HSYNC_DISABLE		(1 << 10)
#define   DVO_VSYNC_TRISTATE		(1 << 9)
#define   DVO_HSYNC_TRISTATE		(1 << 8)
#define   DVO_BORDER_ENABLE		(1 << 7)
#define   DVO_DATA_ORDER_GBRG		(1 << 6)
#define   DVO_DATA_ORDER_RGGB		(0 << 6)
#define   DVO_DATA_ORDER_GBRG_ERRATA	(0 << 6)
#define   DVO_DATA_ORDER_RGGB_ERRATA	(1 << 6)
#define   DVO_VSYNC_ACTIVE_HIGH		(1 << 4)
#define   DVO_HSYNC_ACTIVE_HIGH		(1 << 3)
#define   DVO_BLANK_ACTIVE_HIGH		(1 << 2)
#define   DVO_OUTPUT_CSTATE_PIXELS	(1 << 1)	/* SDG only */
#define   DVO_OUTPUT_SOURCE_SIZE_PIXELS	(1 << 0)	/* SDG only */
#define   DVO_PRESERVE_MASK		(0x7<<24)
#define DVOA_SRCDIM		0x61124
#define DVOB_SRCDIM		0x61144
#define DVOC_SRCDIM		0x61164
#define   DVO_SRCDIM_HORIZONTAL_SHIFT	12
#define   DVO_SRCDIM_VERTICAL_SHIFT	0

/* LVDS port control */
#define LVDS			0x61180
/*
 * Enables the LVDS port.  This bit must be set before DPLLs are enabled, as
 * the DPLL semantics change when the LVDS is assigned to that pipe.
 */
#define   LVDS_PORT_EN			(1 << 31)
/* Selects pipe B for LVDS data.  Must be set on pre-965. */
#define   LVDS_PIPEB_SELECT		(1 << 30)
#define   LVDS_PIPE_MASK		(1 << 30)
#define   LVDS_PIPE(pipe)		((pipe) << 30)
/* LVDS dithering flag on 965/g4x platform */
#define   LVDS_ENABLE_DITHER		(1 << 25)
/* LVDS sync polarity flags. Set to invert (i.e. negative) */
#define   LVDS_VSYNC_POLARITY		(1 << 21)
#define   LVDS_HSYNC_POLARITY		(1 << 20)

/* Enable border for unscaled (or aspect-scaled) display */
#define   LVDS_BORDER_ENABLE		(1 << 15)
/*
 * Enables the A0-A2 data pairs and CLKA, containing 18 bits of color data per
 * pixel.
 */
#define   LVDS_A0A2_CLKA_POWER_MASK	(3 << 8)
#define   LVDS_A0A2_CLKA_POWER_DOWN	(0 << 8)
#define   LVDS_A0A2_CLKA_POWER_UP	(3 << 8)
/*
 * Controls the A3 data pair, which contains the additional LSBs for 24 bit
 * mode.  Only enabled if LVDS_A0A2_CLKA_POWER_UP also indicates it should be
 * on.
 */
#define   LVDS_A3_POWER_MASK		(3 << 6)
#define   LVDS_A3_POWER_DOWN		(0 << 6)
#define   LVDS_A3_POWER_UP		(3 << 6)
/*
 * Controls the CLKB pair.  This should only be set when LVDS_B0B3_POWER_UP
 * is set.
 */
#define   LVDS_CLKB_POWER_MASK		(3 << 4)
#define   LVDS_CLKB_POWER_DOWN		(0 << 4)
#define   LVDS_CLKB_POWER_UP		(3 << 4)
/*
 * Controls the B0-B3 data pairs.  This must be set to match the DPLL p2
 * setting for whether we are in dual-channel mode.  The B3 pair will
 * additionally only be powered up when LVDS_A3_POWER_UP is set.
 */
#define   LVDS_B0B3_POWER_MASK		(3 << 2)
#define   LVDS_B0B3_POWER_DOWN		(0 << 2)
#define   LVDS_B0B3_POWER_UP		(3 << 2)

/* Video Data Island Packet control */
#define VIDEO_DIP_DATA		0x61178
/* Read the description of VIDEO_DIP_DATA (before Haswel) or VIDEO_DIP_ECC
 * (Haswell and newer) to see which VIDEO_DIP_DATA byte corresponds to each byte
 * of the infoframe structure specified by CEA-861. */
#define   VIDEO_DIP_DATA_SIZE	32
#define   VIDEO_DIP_VSC_DATA_SIZE	36
#define VIDEO_DIP_CTL		0x61170
/* Pre HSW: */
#define   VIDEO_DIP_ENABLE		(1 << 31)
#define   VIDEO_DIP_PORT(port)		((port) << 29)
#define   VIDEO_DIP_PORT_MASK		(3 << 29)
#define   VIDEO_DIP_ENABLE_GCP		(1 << 25)
#define   VIDEO_DIP_ENABLE_AVI		(1 << 21)
#define   VIDEO_DIP_ENABLE_VENDOR	(2 << 21)
#define   VIDEO_DIP_ENABLE_GAMUT	(4 << 21)
#define   VIDEO_DIP_ENABLE_SPD		(8 << 21)
#define   VIDEO_DIP_SELECT_AVI		(0 << 19)
#define   VIDEO_DIP_SELECT_VENDOR	(1 << 19)
#define   VIDEO_DIP_SELECT_SPD		(3 << 19)
#define   VIDEO_DIP_SELECT_MASK		(3 << 19)
#define   VIDEO_DIP_FREQ_ONCE		(0 << 16)
#define   VIDEO_DIP_FREQ_VSYNC		(1 << 16)
#define   VIDEO_DIP_FREQ_2VSYNC		(2 << 16)
#define   VIDEO_DIP_FREQ_MASK		(3 << 16)
/* HSW and later: */
#define   VIDEO_DIP_ENABLE_VSC_HSW	(1 << 20)
#define   VIDEO_DIP_ENABLE_GCP_HSW	(1 << 16)
#define   VIDEO_DIP_ENABLE_AVI_HSW	(1 << 12)
#define   VIDEO_DIP_ENABLE_VS_HSW	(1 << 8)
#define   VIDEO_DIP_ENABLE_GMP_HSW	(1 << 4)
#define   VIDEO_DIP_ENABLE_SPD_HSW	(1 << 0)

/* Panel power sequencing */
#define PP_STATUS	0x61200
#define   PP_ON		(1 << 31)
/*
 * Indicates that all dependencies of the panel are on:
 *
 * - PLL enabled
 * - pipe enabled
 * - LVDS/DVOB/DVOC on
 */
#define   PP_READY		(1 << 30)
#define   PP_SEQUENCE_NONE	(0 << 28)
#define   PP_SEQUENCE_POWER_UP	(1 << 28)
#define   PP_SEQUENCE_POWER_DOWN (2 << 28)
#define   PP_SEQUENCE_MASK	(3 << 28)
#define   PP_SEQUENCE_SHIFT	28
#define   PP_CYCLE_DELAY_ACTIVE	(1 << 27)
#define   PP_SEQUENCE_STATE_MASK 0x0000000f
#define   PP_SEQUENCE_STATE_OFF_IDLE	(0x0 << 0)
#define   PP_SEQUENCE_STATE_OFF_S0_1	(0x1 << 0)
#define   PP_SEQUENCE_STATE_OFF_S0_2	(0x2 << 0)
#define   PP_SEQUENCE_STATE_OFF_S0_3	(0x3 << 0)
#define   PP_SEQUENCE_STATE_ON_IDLE	(0x8 << 0)
#define   PP_SEQUENCE_STATE_ON_S1_0	(0x9 << 0)
#define   PP_SEQUENCE_STATE_ON_S1_2	(0xa << 0)
#define   PP_SEQUENCE_STATE_ON_S1_3	(0xb << 0)
#define   PP_SEQUENCE_STATE_RESET	(0xf << 0)
#define PP_CONTROL	0x61204
#define   POWER_TARGET_ON	(1 << 0)
#define PP_ON_DELAYS	0x61208
#define PP_OFF_DELAYS	0x6120c
#define PP_DIVISOR	0x61210

/* Panel fitting */
#define PFIT_CONTROL	(dev_priv->info.display_mmio_offset + 0x61230)
#define   PFIT_ENABLE		(1 << 31)
#define   PFIT_PIPE_MASK	(3 << 29)
#define   PFIT_PIPE_SHIFT	29
#define   VERT_INTERP_DISABLE	(0 << 10)
#define   VERT_INTERP_BILINEAR	(1 << 10)
#define   VERT_INTERP_MASK	(3 << 10)
#define   VERT_AUTO_SCALE	(1 << 9)
#define   HORIZ_INTERP_DISABLE	(0 << 6)
#define   HORIZ_INTERP_BILINEAR	(1 << 6)
#define   HORIZ_INTERP_MASK	(3 << 6)
#define   HORIZ_AUTO_SCALE	(1 << 5)
#define   PANEL_8TO6_DITHER_ENABLE (1 << 3)
#define   PFIT_FILTER_FUZZY	(0 << 24)
#define   PFIT_SCALING_AUTO	(0 << 26)
#define   PFIT_SCALING_PROGRAMMED (1 << 26)
#define   PFIT_SCALING_PILLAR	(2 << 26)
#define   PFIT_SCALING_LETTER	(3 << 26)
#define PFIT_PGM_RATIOS	(dev_priv->info.display_mmio_offset + 0x61234)
/* Pre-965 */
#define		PFIT_VERT_SCALE_SHIFT		20
#define		PFIT_VERT_SCALE_MASK		0xfff00000
#define		PFIT_HORIZ_SCALE_SHIFT		4
#define		PFIT_HORIZ_SCALE_MASK		0x0000fff0
/* 965+ */
#define		PFIT_VERT_SCALE_SHIFT_965	16
#define		PFIT_VERT_SCALE_MASK_965	0x1fff0000
#define		PFIT_HORIZ_SCALE_SHIFT_965	0
#define		PFIT_HORIZ_SCALE_MASK_965	0x00001fff

#define PFIT_AUTO_RATIOS (dev_priv->info.display_mmio_offset + 0x61238)

#define _VLV_BLC_PWM_CTL2_A (dev_priv->info.display_mmio_offset + 0x61250)
#define _VLV_BLC_PWM_CTL2_B (dev_priv->info.display_mmio_offset + 0x61350)
#define VLV_BLC_PWM_CTL2(pipe) _PIPE(pipe, _VLV_BLC_PWM_CTL2_A, \
				     _VLV_BLC_PWM_CTL2_B)

#define _VLV_BLC_PWM_CTL_A (dev_priv->info.display_mmio_offset + 0x61254)
#define _VLV_BLC_PWM_CTL_B (dev_priv->info.display_mmio_offset + 0x61354)
#define VLV_BLC_PWM_CTL(pipe) _PIPE(pipe, _VLV_BLC_PWM_CTL_A, \
				    _VLV_BLC_PWM_CTL_B)

#define _VLV_BLC_HIST_CTL_A (dev_priv->info.display_mmio_offset + 0x61260)
#define _VLV_BLC_HIST_CTL_B (dev_priv->info.display_mmio_offset + 0x61360)
#define VLV_BLC_HIST_CTL(pipe) _PIPE(pipe, _VLV_BLC_HIST_CTL_A, \
				     _VLV_BLC_HIST_CTL_B)

/* Backlight control */
#define BLC_PWM_CTL2	(dev_priv->info.display_mmio_offset + 0x61250) /* 965+ only */
#define   BLM_PWM_ENABLE		(1 << 31)
#define   BLM_COMBINATION_MODE		(1 << 30) /* gen4 only */
#define   BLM_PIPE_SELECT		(1 << 29)
#define   BLM_PIPE_SELECT_IVB		(3 << 29)
#define   BLM_PIPE_A			(0 << 29)
#define   BLM_PIPE_B			(1 << 29)
#define   BLM_PIPE_C			(2 << 29) /* ivb + */
#define   BLM_TRANSCODER_A		BLM_PIPE_A /* hsw */
#define   BLM_TRANSCODER_B		BLM_PIPE_B
#define   BLM_TRANSCODER_C		BLM_PIPE_C
#define   BLM_TRANSCODER_EDP		(3 << 29)
#define   BLM_PIPE(pipe)		((pipe) << 29)
#define   BLM_POLARITY_I965		(1 << 28) /* gen4 only */
#define   BLM_PHASE_IN_INTERUPT_STATUS	(1 << 26)
#define   BLM_PHASE_IN_ENABLE		(1 << 25)
#define   BLM_PHASE_IN_INTERUPT_ENABL	(1 << 24)
#define   BLM_PHASE_IN_TIME_BASE_SHIFT	(16)
#define   BLM_PHASE_IN_TIME_BASE_MASK	(0xff << 16)
#define   BLM_PHASE_IN_COUNT_SHIFT	(8)
#define   BLM_PHASE_IN_COUNT_MASK	(0xff << 8)
#define   BLM_PHASE_IN_INCR_SHIFT	(0)
#define   BLM_PHASE_IN_INCR_MASK	(0xff << 0)
#define BLC_PWM_CTL	(dev_priv->info.display_mmio_offset + 0x61254)
/*
 * This is the most significant 15 bits of the number of backlight cycles in a
 * complete cycle of the modulated backlight control.
 *
 * The actual value is this field multiplied by two.
 */
#define   BACKLIGHT_MODULATION_FREQ_SHIFT	(17)
#define   BACKLIGHT_MODULATION_FREQ_MASK	(0x7fff << 17)
#define   BLM_LEGACY_MODE			(1 << 16) /* gen2 only */
/*
 * This is the number of cycles out of the backlight modulation cycle for which
 * the backlight is on.
 *
 * This field must be no greater than the number of cycles in the complete
 * backlight modulation cycle.
 */
#define   BACKLIGHT_DUTY_CYCLE_SHIFT		(0)
#define   BACKLIGHT_DUTY_CYCLE_MASK		(0xffff)
#define   BACKLIGHT_DUTY_CYCLE_MASK_PNV		(0xfffe)
#define   BLM_POLARITY_PNV			(1 << 0) /* pnv only */

#define BLC_HIST_CTL	(dev_priv->info.display_mmio_offset + 0x61260)

/* New registers for PCH-split platforms. Safe where new bits show up, the
 * register layout machtes with gen4 BLC_PWM_CTL[12]. */
#define BLC_PWM_CPU_CTL2	0x48250
#define BLC_PWM_CPU_CTL		0x48254

#define HSW_BLC_PWM2_CTL	0x48350

/* PCH CTL1 is totally different, all but the below bits are reserved. CTL2 is
 * like the normal CTL from gen4 and earlier. Hooray for confusing naming. */
#define BLC_PWM_PCH_CTL1	0xc8250
#define   BLM_PCH_PWM_ENABLE			(1 << 31)
#define   BLM_PCH_OVERRIDE_ENABLE		(1 << 30)
#define   BLM_PCH_POLARITY			(1 << 29)
#define BLC_PWM_PCH_CTL2	0xc8254

#define UTIL_PIN_CTL		0x48400
#define   UTIL_PIN_ENABLE	(1 << 31)

#define PCH_GTC_CTL		0xe7000
#define   PCH_GTC_ENABLE	(1 << 31)

/* TV port control */
#define TV_CTL			0x68000
/* Enables the TV encoder */
# define TV_ENC_ENABLE			(1 << 31)
/* Sources the TV encoder input from pipe B instead of A. */
# define TV_ENC_PIPEB_SELECT		(1 << 30)
/* Outputs composite video (DAC A only) */
# define TV_ENC_OUTPUT_COMPOSITE	(0 << 28)
/* Outputs SVideo video (DAC B/C) */
# define TV_ENC_OUTPUT_SVIDEO		(1 << 28)
/* Outputs Component video (DAC A/B/C) */
# define TV_ENC_OUTPUT_COMPONENT	(2 << 28)
/* Outputs Composite and SVideo (DAC A/B/C) */
# define TV_ENC_OUTPUT_SVIDEO_COMPOSITE	(3 << 28)
# define TV_TRILEVEL_SYNC		(1 << 21)
/* Enables slow sync generation (945GM only) */
# define TV_SLOW_SYNC			(1 << 20)
/* Selects 4x oversampling for 480i and 576p */
# define TV_OVERSAMPLE_4X		(0 << 18)
/* Selects 2x oversampling for 720p and 1080i */
# define TV_OVERSAMPLE_2X		(1 << 18)
/* Selects no oversampling for 1080p */
# define TV_OVERSAMPLE_NONE		(2 << 18)
/* Selects 8x oversampling */
# define TV_OVERSAMPLE_8X		(3 << 18)
/* Selects progressive mode rather than interlaced */
# define TV_PROGRESSIVE			(1 << 17)
/* Sets the colorburst to PAL mode.  Required for non-M PAL modes. */
# define TV_PAL_BURST			(1 << 16)
/* Field for setting delay of Y compared to C */
# define TV_YC_SKEW_MASK		(7 << 12)
/* Enables a fix for 480p/576p standard definition modes on the 915GM only */
# define TV_ENC_SDP_FIX			(1 << 11)
/*
 * Enables a fix for the 915GM only.
 *
 * Not sure what it does.
 */
# define TV_ENC_C0_FIX			(1 << 10)
/* Bits that must be preserved by software */
# define TV_CTL_SAVE			((1 << 11) | (3 << 9) | (7 << 6) | 0xf)
# define TV_FUSE_STATE_MASK		(3 << 4)
/* Read-only state that reports all features enabled */
# define TV_FUSE_STATE_ENABLED		(0 << 4)
/* Read-only state that reports that Macrovision is disabled in hardware*/
# define TV_FUSE_STATE_NO_MACROVISION	(1 << 4)
/* Read-only state that reports that TV-out is disabled in hardware. */
# define TV_FUSE_STATE_DISABLED		(2 << 4)
/* Normal operation */
# define TV_TEST_MODE_NORMAL		(0 << 0)
/* Encoder test pattern 1 - combo pattern */
# define TV_TEST_MODE_PATTERN_1		(1 << 0)
/* Encoder test pattern 2 - full screen vertical 75% color bars */
# define TV_TEST_MODE_PATTERN_2		(2 << 0)
/* Encoder test pattern 3 - full screen horizontal 75% color bars */
# define TV_TEST_MODE_PATTERN_3		(3 << 0)
/* Encoder test pattern 4 - random noise */
# define TV_TEST_MODE_PATTERN_4		(4 << 0)
/* Encoder test pattern 5 - linear color ramps */
# define TV_TEST_MODE_PATTERN_5		(5 << 0)
/*
 * This test mode forces the DACs to 50% of full output.
 *
 * This is used for load detection in combination with TVDAC_SENSE_MASK
 */
# define TV_TEST_MODE_MONITOR_DETECT	(7 << 0)
# define TV_TEST_MODE_MASK		(7 << 0)

#define TV_DAC			0x68004
# define TV_DAC_SAVE		0x00ffff00
/*
 * Reports that DAC state change logic has reported change (RO).
 *
 * This gets cleared when TV_DAC_STATE_EN is cleared
*/
# define TVDAC_STATE_CHG		(1 << 31)
# define TVDAC_SENSE_MASK		(7 << 28)
/* Reports that DAC A voltage is above the detect threshold */
# define TVDAC_A_SENSE			(1 << 30)
/* Reports that DAC B voltage is above the detect threshold */
# define TVDAC_B_SENSE			(1 << 29)
/* Reports that DAC C voltage is above the detect threshold */
# define TVDAC_C_SENSE			(1 << 28)
/*
 * Enables DAC state detection logic, for load-based TV detection.
 *
 * The PLL of the chosen pipe (in TV_CTL) must be running, and the encoder set
 * to off, for load detection to work.
 */
# define TVDAC_STATE_CHG_EN		(1 << 27)
/* Sets the DAC A sense value to high */
# define TVDAC_A_SENSE_CTL		(1 << 26)
/* Sets the DAC B sense value to high */
# define TVDAC_B_SENSE_CTL		(1 << 25)
/* Sets the DAC C sense value to high */
# define TVDAC_C_SENSE_CTL		(1 << 24)
/* Overrides the ENC_ENABLE and DAC voltage levels */
# define DAC_CTL_OVERRIDE		(1 << 7)
/* Sets the slew rate.  Must be preserved in software */
# define ENC_TVDAC_SLEW_FAST		(1 << 6)
# define DAC_A_1_3_V			(0 << 4)
# define DAC_A_1_1_V			(1 << 4)
# define DAC_A_0_7_V			(2 << 4)
# define DAC_A_MASK			(3 << 4)
# define DAC_B_1_3_V			(0 << 2)
# define DAC_B_1_1_V			(1 << 2)
# define DAC_B_0_7_V			(2 << 2)
# define DAC_B_MASK			(3 << 2)
# define DAC_C_1_3_V			(0 << 0)
# define DAC_C_1_1_V			(1 << 0)
# define DAC_C_0_7_V			(2 << 0)
# define DAC_C_MASK			(3 << 0)

/*
 * CSC coefficients are stored in a floating point format with 9 bits of
 * mantissa and 2 or 3 bits of exponent.  The exponent is represented as 2**-n,
 * where 2-bit exponents are unsigned n, and 3-bit exponents are signed n with
 * -1 (0x3) being the only legal negative value.
 */
#define TV_CSC_Y		0x68010
# define TV_RY_MASK			0x07ff0000
# define TV_RY_SHIFT			16
# define TV_GY_MASK			0x00000fff
# define TV_GY_SHIFT			0

#define TV_CSC_Y2		0x68014
# define TV_BY_MASK			0x07ff0000
# define TV_BY_SHIFT			16
/*
 * Y attenuation for component video.
 *
 * Stored in 1.9 fixed point.
 */
# define TV_AY_MASK			0x000003ff
# define TV_AY_SHIFT			0

#define TV_CSC_U		0x68018
# define TV_RU_MASK			0x07ff0000
# define TV_RU_SHIFT			16
# define TV_GU_MASK			0x000007ff
# define TV_GU_SHIFT			0

#define TV_CSC_U2		0x6801c
# define TV_BU_MASK			0x07ff0000
# define TV_BU_SHIFT			16
/*
 * U attenuation for component video.
 *
 * Stored in 1.9 fixed point.
 */
# define TV_AU_MASK			0x000003ff
# define TV_AU_SHIFT			0

#define TV_CSC_V		0x68020
# define TV_RV_MASK			0x0fff0000
# define TV_RV_SHIFT			16
# define TV_GV_MASK			0x000007ff
# define TV_GV_SHIFT			0

#define TV_CSC_V2		0x68024
# define TV_BV_MASK			0x07ff0000
# define TV_BV_SHIFT			16
/*
 * V attenuation for component video.
 *
 * Stored in 1.9 fixed point.
 */
# define TV_AV_MASK			0x000007ff
# define TV_AV_SHIFT			0

#define TV_CLR_KNOBS		0x68028
/* 2s-complement brightness adjustment */
# define TV_BRIGHTNESS_MASK		0xff000000
# define TV_BRIGHTNESS_SHIFT		24
/* Contrast adjustment, as a 2.6 unsigned floating point number */
# define TV_CONTRAST_MASK		0x00ff0000
# define TV_CONTRAST_SHIFT		16
/* Saturation adjustment, as a 2.6 unsigned floating point number */
# define TV_SATURATION_MASK		0x0000ff00
# define TV_SATURATION_SHIFT		8
/* Hue adjustment, as an integer phase angle in degrees */
# define TV_HUE_MASK			0x000000ff
# define TV_HUE_SHIFT			0

#define TV_CLR_LEVEL		0x6802c
/* Controls the DAC level for black */
# define TV_BLACK_LEVEL_MASK		0x01ff0000
# define TV_BLACK_LEVEL_SHIFT		16
/* Controls the DAC level for blanking */
# define TV_BLANK_LEVEL_MASK		0x000001ff
# define TV_BLANK_LEVEL_SHIFT		0

#define TV_H_CTL_1		0x68030
/* Number of pixels in the hsync. */
# define TV_HSYNC_END_MASK		0x1fff0000
# define TV_HSYNC_END_SHIFT		16
/* Total number of pixels minus one in the line (display and blanking). */
# define TV_HTOTAL_MASK			0x00001fff
# define TV_HTOTAL_SHIFT		0

#define TV_H_CTL_2		0x68034
/* Enables the colorburst (needed for non-component color) */
# define TV_BURST_ENA			(1 << 31)
/* Offset of the colorburst from the start of hsync, in pixels minus one. */
# define TV_HBURST_START_SHIFT		16
# define TV_HBURST_START_MASK		0x1fff0000
/* Length of the colorburst */
# define TV_HBURST_LEN_SHIFT		0
# define TV_HBURST_LEN_MASK		0x0001fff

#define TV_H_CTL_3		0x68038
/* End of hblank, measured in pixels minus one from start of hsync */
# define TV_HBLANK_END_SHIFT		16
# define TV_HBLANK_END_MASK		0x1fff0000
/* Start of hblank, measured in pixels minus one from start of hsync */
# define TV_HBLANK_START_SHIFT		0
# define TV_HBLANK_START_MASK		0x0001fff

#define TV_V_CTL_1		0x6803c
/* XXX */
# define TV_NBR_END_SHIFT		16
# define TV_NBR_END_MASK		0x07ff0000
/* XXX */
# define TV_VI_END_F1_SHIFT		8
# define TV_VI_END_F1_MASK		0x00003f00
/* XXX */
# define TV_VI_END_F2_SHIFT		0
# define TV_VI_END_F2_MASK		0x0000003f

#define TV_V_CTL_2		0x68040
/* Length of vsync, in half lines */
# define TV_VSYNC_LEN_MASK		0x07ff0000
# define TV_VSYNC_LEN_SHIFT		16
/* Offset of the start of vsync in field 1, measured in one less than the
 * number of half lines.
 */
# define TV_VSYNC_START_F1_MASK		0x00007f00
# define TV_VSYNC_START_F1_SHIFT	8
/*
 * Offset of the start of vsync in field 2, measured in one less than the
 * number of half lines.
 */
# define TV_VSYNC_START_F2_MASK		0x0000007f
# define TV_VSYNC_START_F2_SHIFT	0

#define TV_V_CTL_3		0x68044
/* Enables generation of the equalization signal */
# define TV_EQUAL_ENA			(1 << 31)
/* Length of vsync, in half lines */
# define TV_VEQ_LEN_MASK		0x007f0000
# define TV_VEQ_LEN_SHIFT		16
/* Offset of the start of equalization in field 1, measured in one less than
 * the number of half lines.
 */
# define TV_VEQ_START_F1_MASK		0x0007f00
# define TV_VEQ_START_F1_SHIFT		8
/*
 * Offset of the start of equalization in field 2, measured in one less than
 * the number of half lines.
 */
# define TV_VEQ_START_F2_MASK		0x000007f
# define TV_VEQ_START_F2_SHIFT		0

#define TV_V_CTL_4		0x68048
/*
 * Offset to start of vertical colorburst, measured in one less than the
 * number of lines from vertical start.
 */
# define TV_VBURST_START_F1_MASK	0x003f0000
# define TV_VBURST_START_F1_SHIFT	16
/*
 * Offset to the end of vertical colorburst, measured in one less than the
 * number of lines from the start of NBR.
 */
# define TV_VBURST_END_F1_MASK		0x000000ff
# define TV_VBURST_END_F1_SHIFT		0

#define TV_V_CTL_5		0x6804c
/*
 * Offset to start of vertical colorburst, measured in one less than the
 * number of lines from vertical start.
 */
# define TV_VBURST_START_F2_MASK	0x003f0000
# define TV_VBURST_START_F2_SHIFT	16
/*
 * Offset to the end of vertical colorburst, measured in one less than the
 * number of lines from the start of NBR.
 */
# define TV_VBURST_END_F2_MASK		0x000000ff
# define TV_VBURST_END_F2_SHIFT		0

#define TV_V_CTL_6		0x68050
/*
 * Offset to start of vertical colorburst, measured in one less than the
 * number of lines from vertical start.
 */
# define TV_VBURST_START_F3_MASK	0x003f0000
# define TV_VBURST_START_F3_SHIFT	16
/*
 * Offset to the end of vertical colorburst, measured in one less than the
 * number of lines from the start of NBR.
 */
# define TV_VBURST_END_F3_MASK		0x000000ff
# define TV_VBURST_END_F3_SHIFT		0

#define TV_V_CTL_7		0x68054
/*
 * Offset to start of vertical colorburst, measured in one less than the
 * number of lines from vertical start.
 */
# define TV_VBURST_START_F4_MASK	0x003f0000
# define TV_VBURST_START_F4_SHIFT	16
/*
 * Offset to the end of vertical colorburst, measured in one less than the
 * number of lines from the start of NBR.
 */
# define TV_VBURST_END_F4_MASK		0x000000ff
# define TV_VBURST_END_F4_SHIFT		0

#define TV_SC_CTL_1		0x68060
/* Turns on the first subcarrier phase generation DDA */
# define TV_SC_DDA1_EN			(1 << 31)
/* Turns on the first subcarrier phase generation DDA */
# define TV_SC_DDA2_EN			(1 << 30)
/* Turns on the first subcarrier phase generation DDA */
# define TV_SC_DDA3_EN			(1 << 29)
/* Sets the subcarrier DDA to reset frequency every other field */
# define TV_SC_RESET_EVERY_2		(0 << 24)
/* Sets the subcarrier DDA to reset frequency every fourth field */
# define TV_SC_RESET_EVERY_4		(1 << 24)
/* Sets the subcarrier DDA to reset frequency every eighth field */
# define TV_SC_RESET_EVERY_8		(2 << 24)
/* Sets the subcarrier DDA to never reset the frequency */
# define TV_SC_RESET_NEVER		(3 << 24)
/* Sets the peak amplitude of the colorburst.*/
# define TV_BURST_LEVEL_MASK		0x00ff0000
# define TV_BURST_LEVEL_SHIFT		16
/* Sets the increment of the first subcarrier phase generation DDA */
# define TV_SCDDA1_INC_MASK		0x00000fff
# define TV_SCDDA1_INC_SHIFT		0

#define TV_SC_CTL_2		0x68064
/* Sets the rollover for the second subcarrier phase generation DDA */
# define TV_SCDDA2_SIZE_MASK		0x7fff0000
# define TV_SCDDA2_SIZE_SHIFT		16
/* Sets the increent of the second subcarrier phase generation DDA */
# define TV_SCDDA2_INC_MASK		0x00007fff
# define TV_SCDDA2_INC_SHIFT		0

#define TV_SC_CTL_3		0x68068
/* Sets the rollover for the third subcarrier phase generation DDA */
# define TV_SCDDA3_SIZE_MASK		0x7fff0000
# define TV_SCDDA3_SIZE_SHIFT		16
/* Sets the increent of the third subcarrier phase generation DDA */
# define TV_SCDDA3_INC_MASK		0x00007fff
# define TV_SCDDA3_INC_SHIFT		0

#define TV_WIN_POS		0x68070
/* X coordinate of the display from the start of horizontal active */
# define TV_XPOS_MASK			0x1fff0000
# define TV_XPOS_SHIFT			16
/* Y coordinate of the display from the start of vertical active (NBR) */
# define TV_YPOS_MASK			0x00000fff
# define TV_YPOS_SHIFT			0

#define TV_WIN_SIZE		0x68074
/* Horizontal size of the display window, measured in pixels*/
# define TV_XSIZE_MASK			0x1fff0000
# define TV_XSIZE_SHIFT			16
/*
 * Vertical size of the display window, measured in pixels.
 *
 * Must be even for interlaced modes.
 */
# define TV_YSIZE_MASK			0x00000fff
# define TV_YSIZE_SHIFT			0

#define TV_FILTER_CTL_1		0x68080
/*
 * Enables automatic scaling calculation.
 *
 * If set, the rest of the registers are ignored, and the calculated values can
 * be read back from the register.
 */
# define TV_AUTO_SCALE			(1 << 31)
/*
 * Disables the vertical filter.
 *
 * This is required on modes more than 1024 pixels wide */
# define TV_V_FILTER_BYPASS		(1 << 29)
/* Enables adaptive vertical filtering */
# define TV_VADAPT			(1 << 28)
# define TV_VADAPT_MODE_MASK		(3 << 26)
/* Selects the least adaptive vertical filtering mode */
# define TV_VADAPT_MODE_LEAST		(0 << 26)
/* Selects the moderately adaptive vertical filtering mode */
# define TV_VADAPT_MODE_MODERATE	(1 << 26)
/* Selects the most adaptive vertical filtering mode */
# define TV_VADAPT_MODE_MOST		(3 << 26)
/*
 * Sets the horizontal scaling factor.
 *
 * This should be the fractional part of the horizontal scaling factor divided
 * by the oversampling rate.  TV_HSCALE should be less than 1, and set to:
 *
 * (src width - 1) / ((oversample * dest width) - 1)
 */
# define TV_HSCALE_FRAC_MASK		0x00003fff
# define TV_HSCALE_FRAC_SHIFT		0

#define TV_FILTER_CTL_2		0x68084
/*
 * Sets the integer part of the 3.15 fixed-point vertical scaling factor.
 *
 * TV_VSCALE should be (src height - 1) / ((interlace * dest height) - 1)
 */
# define TV_VSCALE_INT_MASK		0x00038000
# define TV_VSCALE_INT_SHIFT		15
/*
 * Sets the fractional part of the 3.15 fixed-point vertical scaling factor.
 *
 * \sa TV_VSCALE_INT_MASK
 */
# define TV_VSCALE_FRAC_MASK		0x00007fff
# define TV_VSCALE_FRAC_SHIFT		0

#define TV_FILTER_CTL_3		0x68088
/*
 * Sets the integer part of the 3.15 fixed-point vertical scaling factor.
 *
 * TV_VSCALE should be (src height - 1) / (1/4 * (dest height - 1))
 *
 * For progressive modes, TV_VSCALE_IP_INT should be set to zeroes.
 */
# define TV_VSCALE_IP_INT_MASK		0x00038000
# define TV_VSCALE_IP_INT_SHIFT		15
/*
 * Sets the fractional part of the 3.15 fixed-point vertical scaling factor.
 *
 * For progressive modes, TV_VSCALE_IP_INT should be set to zeroes.
 *
 * \sa TV_VSCALE_IP_INT_MASK
 */
# define TV_VSCALE_IP_FRAC_MASK		0x00007fff
# define TV_VSCALE_IP_FRAC_SHIFT		0

#define TV_CC_CONTROL		0x68090
# define TV_CC_ENABLE			(1 << 31)
/*
 * Specifies which field to send the CC data in.
 *
 * CC data is usually sent in field 0.
 */
# define TV_CC_FID_MASK			(1 << 27)
# define TV_CC_FID_SHIFT		27
/* Sets the horizontal position of the CC data.  Usually 135. */
# define TV_CC_HOFF_MASK		0x03ff0000
# define TV_CC_HOFF_SHIFT		16
/* Sets the vertical position of the CC data.  Usually 21 */
# define TV_CC_LINE_MASK		0x0000003f
# define TV_CC_LINE_SHIFT		0

#define TV_CC_DATA		0x68094
# define TV_CC_RDY			(1 << 31)
/* Second word of CC data to be transmitted. */
# define TV_CC_DATA_2_MASK		0x007f0000
# define TV_CC_DATA_2_SHIFT		16
/* First word of CC data to be transmitted. */
# define TV_CC_DATA_1_MASK		0x0000007f
# define TV_CC_DATA_1_SHIFT		0

#define TV_H_LUMA_0		0x68100
#define TV_H_LUMA_59		0x681ec
#define TV_H_CHROMA_0		0x68200
#define TV_H_CHROMA_59		0x682ec
#define TV_V_LUMA_0		0x68300
#define TV_V_LUMA_42		0x683a8
#define TV_V_CHROMA_0		0x68400
#define TV_V_CHROMA_42		0x684a8

/* Display Port */
#define DP_A				0x64000 /* eDP */
#define DP_B				0x64100
#define DP_C				0x64200
#define DP_D				0x64300

#define   DP_PORT_EN			(1 << 31)
#define   DP_PIPEB_SELECT		(1 << 30)
#define   DP_PIPE_MASK			(1 << 30)
#define   DP_PIPE_SELECT_CHV(pipe)	((pipe) << 16)
#define   DP_PIPE_MASK_CHV		(3 << 16)

/* Link training mode - select a suitable mode for each stage */
#define   DP_LINK_TRAIN_PAT_1		(0 << 28)
#define   DP_LINK_TRAIN_PAT_2		(1 << 28)
#define   DP_LINK_TRAIN_PAT_IDLE	(2 << 28)
#define   DP_LINK_TRAIN_OFF		(3 << 28)
#define   DP_LINK_TRAIN_MASK		(3 << 28)
#define   DP_LINK_TRAIN_SHIFT		28
#define   DP_LINK_TRAIN_PAT_3_CHV	(1 << 14)
#define   DP_LINK_TRAIN_MASK_CHV	((3 << 28)|(1<<14))

/* CPT Link training mode */
#define   DP_LINK_TRAIN_PAT_1_CPT	(0 << 8)
#define   DP_LINK_TRAIN_PAT_2_CPT	(1 << 8)
#define   DP_LINK_TRAIN_PAT_IDLE_CPT	(2 << 8)
#define   DP_LINK_TRAIN_OFF_CPT		(3 << 8)
#define   DP_LINK_TRAIN_MASK_CPT	(7 << 8)
#define   DP_LINK_TRAIN_SHIFT_CPT	8

/* Signal voltages. These are mostly controlled by the other end */
#define   DP_VOLTAGE_0_4		(0 << 25)
#define   DP_VOLTAGE_0_6		(1 << 25)
#define   DP_VOLTAGE_0_8		(2 << 25)
#define   DP_VOLTAGE_1_2		(3 << 25)
#define   DP_VOLTAGE_MASK		(7 << 25)
#define   DP_VOLTAGE_SHIFT		25

/* Signal pre-emphasis levels, like voltages, the other end tells us what
 * they want
 */
#define   DP_PRE_EMPHASIS_0		(0 << 22)
#define   DP_PRE_EMPHASIS_3_5		(1 << 22)
#define   DP_PRE_EMPHASIS_6		(2 << 22)
#define   DP_PRE_EMPHASIS_9_5		(3 << 22)
#define   DP_PRE_EMPHASIS_MASK		(7 << 22)
#define   DP_PRE_EMPHASIS_SHIFT		22

/* How many wires to use. I guess 3 was too hard */
#define   DP_PORT_WIDTH(width)		(((width) - 1) << 19)
#define   DP_PORT_WIDTH_MASK		(7 << 19)

/* Mystic DPCD version 1.1 special mode */
#define   DP_ENHANCED_FRAMING		(1 << 18)

/* eDP */
#define   DP_PLL_FREQ_270MHZ		(0 << 16)
#define   DP_PLL_FREQ_160MHZ		(1 << 16)
#define   DP_PLL_FREQ_MASK		(3 << 16)

/* locked once port is enabled */
#define   DP_PORT_REVERSAL		(1 << 15)

/* eDP */
#define   DP_PLL_ENABLE			(1 << 14)

/* sends the clock on lane 15 of the PEG for debug */
#define   DP_CLOCK_OUTPUT_ENABLE	(1 << 13)

#define   DP_SCRAMBLING_DISABLE		(1 << 12)
#define   DP_SCRAMBLING_DISABLE_IRONLAKE	(1 << 7)

/* limit RGB values to avoid confusing TVs */
#define   DP_COLOR_RANGE_16_235		(1 << 8)

/* Turn on the audio link */
#define   DP_AUDIO_OUTPUT_ENABLE	(1 << 6)

/* vs and hs sync polarity */
#define   DP_SYNC_VS_HIGH		(1 << 4)
#define   DP_SYNC_HS_HIGH		(1 << 3)

/* A fantasy */
#define   DP_DETECTED			(1 << 2)

/* The aux channel provides a way to talk to the
 * signal sink for DDC etc. Max packet size supported
 * is 20 bytes in each direction, hence the 5 fixed
 * data registers
 */
#define DPA_AUX_CH_CTL			0x64010
#define DPA_AUX_CH_DATA1		0x64014
#define DPA_AUX_CH_DATA2		0x64018
#define DPA_AUX_CH_DATA3		0x6401c
#define DPA_AUX_CH_DATA4		0x64020
#define DPA_AUX_CH_DATA5		0x64024

#define DPB_AUX_CH_CTL			0x64110
#define DPB_AUX_CH_DATA1		0x64114
#define DPB_AUX_CH_DATA2		0x64118
#define DPB_AUX_CH_DATA3		0x6411c
#define DPB_AUX_CH_DATA4		0x64120
#define DPB_AUX_CH_DATA5		0x64124

#define DPC_AUX_CH_CTL			0x64210
#define DPC_AUX_CH_DATA1		0x64214
#define DPC_AUX_CH_DATA2		0x64218
#define DPC_AUX_CH_DATA3		0x6421c
#define DPC_AUX_CH_DATA4		0x64220
#define DPC_AUX_CH_DATA5		0x64224

#define DPD_AUX_CH_CTL			0x64310
#define DPD_AUX_CH_DATA1		0x64314
#define DPD_AUX_CH_DATA2		0x64318
#define DPD_AUX_CH_DATA3		0x6431c
#define DPD_AUX_CH_DATA4		0x64320
#define DPD_AUX_CH_DATA5		0x64324

#define   DP_AUX_CH_CTL_SEND_BUSY	    (1 << 31)
#define   DP_AUX_CH_CTL_DONE		    (1 << 30)
#define   DP_AUX_CH_CTL_INTERRUPT	    (1 << 29)
#define   DP_AUX_CH_CTL_TIME_OUT_ERROR	    (1 << 28)
#define   DP_AUX_CH_CTL_TIME_OUT_400us	    (0 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_600us	    (1 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_800us	    (2 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_1600us	    (3 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_MASK	    (3 << 26)
#define   DP_AUX_CH_CTL_RECEIVE_ERROR	    (1 << 25)
#define   DP_AUX_CH_CTL_MESSAGE_SIZE_MASK    (0x1f << 20)
#define   DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT   20
#define   DP_AUX_CH_CTL_PRECHARGE_2US_MASK   (0xf << 16)
#define   DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT  16
#define   DP_AUX_CH_CTL_AUX_AKSV_SELECT	    (1 << 15)
#define   DP_AUX_CH_CTL_MANCHESTER_TEST	    (1 << 14)
#define   DP_AUX_CH_CTL_SYNC_TEST	    (1 << 13)
#define   DP_AUX_CH_CTL_DEGLITCH_TEST	    (1 << 12)
#define   DP_AUX_CH_CTL_PRECHARGE_TEST	    (1 << 11)
#define   DP_AUX_CH_CTL_BIT_CLOCK_2X_MASK    (0x7ff)
#define   DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT   0
#define   DP_AUX_CH_CTL_SYNC_PULSE_SKL(c)   ((c) - 1)

/*
 * Computing GMCH M and N values for the Display Port link
 *
 * GMCH M/N = dot clock * bytes per pixel / ls_clk * # of lanes
 *
 * ls_clk (we assume) is the DP link clock (1.62 or 2.7 GHz)
 *
 * The GMCH value is used internally
 *
 * bytes_per_pixel is the number of bytes coming out of the plane,
 * which is after the LUTs, so we want the bytes for our color format.
 * For our current usage, this is always 3, one byte for R, G and B.
 */
#define _PIPEA_DATA_M_G4X	0x70050
#define _PIPEB_DATA_M_G4X	0x71050

/* Transfer unit size for display port - 1, default is 0x3f (for TU size 64) */
#define  TU_SIZE(x)             (((x)-1) << 25) /* default size 64 */
#define  TU_SIZE_SHIFT		25
#define  TU_SIZE_MASK           (0x3f << 25)

#define  DATA_LINK_M_N_MASK	(0xffffff)
#define  DATA_LINK_N_MAX	(0x800000)

#define _PIPEA_DATA_N_G4X	0x70054
#define _PIPEB_DATA_N_G4X	0x71054
#define   PIPE_GMCH_DATA_N_MASK			(0xffffff)

/*
 * Computing Link M and N values for the Display Port link
 *
 * Link M / N = pixel_clock / ls_clk
 *
 * (the DP spec calls pixel_clock the 'strm_clk')
 *
 * The Link value is transmitted in the Main Stream
 * Attributes and VB-ID.
 */

#define _PIPEA_LINK_M_G4X	0x70060
#define _PIPEB_LINK_M_G4X	0x71060
#define   PIPEA_DP_LINK_M_MASK			(0xffffff)

#define _PIPEA_LINK_N_G4X	0x70064
#define _PIPEB_LINK_N_G4X	0x71064
#define   PIPEA_DP_LINK_N_MASK			(0xffffff)

#define PIPE_DATA_M_G4X(pipe) _PIPE(pipe, _PIPEA_DATA_M_G4X, _PIPEB_DATA_M_G4X)
#define PIPE_DATA_N_G4X(pipe) _PIPE(pipe, _PIPEA_DATA_N_G4X, _PIPEB_DATA_N_G4X)
#define PIPE_LINK_M_G4X(pipe) _PIPE(pipe, _PIPEA_LINK_M_G4X, _PIPEB_LINK_M_G4X)
#define PIPE_LINK_N_G4X(pipe) _PIPE(pipe, _PIPEA_LINK_N_G4X, _PIPEB_LINK_N_G4X)

/* Display & cursor control */

/* Pipe A */
#define _PIPEADSL		0x70000
#define   DSL_LINEMASK_GEN2	0x00000fff
#define   DSL_LINEMASK_GEN3	0x00001fff
#define _PIPEACONF		0x70008
#define   PIPECONF_ENABLE	(1<<31)
#define   PIPECONF_DISABLE	0
#define   PIPECONF_DOUBLE_WIDE	(1<<30)
#define   I965_PIPECONF_ACTIVE	(1<<30)
#define   PIPECONF_DSI_PLL_LOCKED	(1<<29) /* vlv & pipe A only */
#define   PIPECONF_FRAME_START_DELAY_MASK (3<<27)
#define   PIPECONF_SINGLE_WIDE	0
#define   PIPECONF_PIPE_UNLOCKED 0
#define   PIPECONF_PIPE_LOCKED	(1<<25)
#define   PIPECONF_PALETTE	0
#define   PIPECONF_GAMMA		(1<<24)
#define   PIPECONF_FORCE_BORDER	(1<<25)
#define   PIPECONF_INTERLACE_MASK	(7 << 21)
#define   PIPECONF_INTERLACE_MASK_HSW	(3 << 21)
/* Note that pre-gen3 does not support interlaced display directly. Panel
 * fitting must be disabled on pre-ilk for interlaced. */
#define   PIPECONF_PROGRESSIVE			(0 << 21)
#define   PIPECONF_INTERLACE_W_SYNC_SHIFT_PANEL	(4 << 21) /* gen4 only */
#define   PIPECONF_INTERLACE_W_SYNC_SHIFT	(5 << 21) /* gen4 only */
#define   PIPECONF_INTERLACE_W_FIELD_INDICATION	(6 << 21)
#define   PIPECONF_INTERLACE_FIELD_0_ONLY	(7 << 21) /* gen3 only */
/* Ironlake and later have a complete new set of values for interlaced. PFIT
 * means panel fitter required, PF means progressive fetch, DBL means power
 * saving pixel doubling. */
#define   PIPECONF_PFIT_PF_INTERLACED_ILK	(1 << 21)
#define   PIPECONF_INTERLACED_ILK		(3 << 21)
#define   PIPECONF_INTERLACED_DBL_ILK		(4 << 21) /* ilk/snb only */
#define   PIPECONF_PFIT_PF_INTERLACED_DBL_ILK	(5 << 21) /* ilk/snb only */
#define   PIPECONF_INTERLACE_MODE_MASK		(7 << 21)
#define   PIPECONF_EDP_RR_MODE_SWITCH		(1 << 20)
#define   PIPECONF_CXSR_DOWNCLOCK	(1<<16)
#define   PIPECONF_COLOR_RANGE_SELECT	(1 << 13)
#define   PIPECONF_BPC_MASK	(0x7 << 5)
#define   PIPECONF_8BPC		(0<<5)
#define   PIPECONF_10BPC	(1<<5)
#define   PIPECONF_6BPC		(2<<5)
#define   PIPECONF_12BPC	(3<<5)
#define   PIPECONF_DITHER_EN	(1<<4)
#define   PIPECONF_DITHER_TYPE_MASK (0x0000000c)
#define   PIPECONF_DITHER_TYPE_SP (0<<2)
#define   PIPECONF_DITHER_TYPE_ST1 (1<<2)
#define   PIPECONF_DITHER_TYPE_ST2 (2<<2)
#define   PIPECONF_DITHER_TYPE_TEMP (3<<2)
#define _PIPEASTAT		0x70024
#define   PIPE_FIFO_UNDERRUN_STATUS		(1UL<<31)
#define   SPRITE1_FLIP_DONE_INT_EN_VLV		(1UL<<30)
#define   PIPE_CRC_ERROR_ENABLE			(1UL<<29)
#define   PIPE_CRC_DONE_ENABLE			(1UL<<28)
#define   PERF_COUNTER2_INTERRUPT_EN		(1UL<<27)
#define   PIPE_GMBUS_EVENT_ENABLE		(1UL<<27)
#define   PLANE_FLIP_DONE_INT_EN_VLV		(1UL<<26)
#define   PIPE_HOTPLUG_INTERRUPT_ENABLE		(1UL<<26)
#define   PIPE_VSYNC_INTERRUPT_ENABLE		(1UL<<25)
#define   PIPE_DISPLAY_LINE_COMPARE_ENABLE	(1UL<<24)
#define   PIPE_DPST_EVENT_ENABLE		(1UL<<23)
#define   SPRITE0_FLIP_DONE_INT_EN_VLV		(1UL<<22)
#define   PIPE_LEGACY_BLC_EVENT_ENABLE		(1UL<<22)
#define   PIPE_ODD_FIELD_INTERRUPT_ENABLE	(1UL<<21)
#define   PIPE_EVEN_FIELD_INTERRUPT_ENABLE	(1UL<<20)
#define   PIPE_B_PSR_INTERRUPT_ENABLE_VLV	(1UL<<19)
#define   PERF_COUNTER_INTERRUPT_EN		(1UL<<19)
#define   PIPE_HOTPLUG_TV_INTERRUPT_ENABLE	(1UL<<18) /* pre-965 */
#define   PIPE_START_VBLANK_INTERRUPT_ENABLE	(1UL<<18) /* 965 or later */
#define   PIPE_FRAMESTART_INTERRUPT_ENABLE	(1UL<<17)
#define   PIPE_VBLANK_INTERRUPT_ENABLE		(1UL<<17)
#define   PIPEA_HBLANK_INT_EN_VLV		(1UL<<16)
#define   PIPE_OVERLAY_UPDATED_ENABLE		(1UL<<16)
#define   SPRITE1_FLIP_DONE_INT_STATUS_VLV	(1UL<<15)
#define   SPRITE0_FLIP_DONE_INT_STATUS_VLV	(1UL<<14)
#define   PIPE_CRC_ERROR_INTERRUPT_STATUS	(1UL<<13)
#define   PIPE_CRC_DONE_INTERRUPT_STATUS	(1UL<<12)
#define   PERF_COUNTER2_INTERRUPT_STATUS	(1UL<<11)
#define   PIPE_GMBUS_INTERRUPT_STATUS		(1UL<<11)
#define   PLANE_FLIP_DONE_INT_STATUS_VLV	(1UL<<10)
#define   PIPE_HOTPLUG_INTERRUPT_STATUS		(1UL<<10)
#define   PIPE_VSYNC_INTERRUPT_STATUS		(1UL<<9)
#define   PIPE_DISPLAY_LINE_COMPARE_STATUS	(1UL<<8)
#define   PIPE_DPST_EVENT_STATUS		(1UL<<7)
#define   PIPE_A_PSR_STATUS_VLV			(1UL<<6)
#define   PIPE_LEGACY_BLC_EVENT_STATUS		(1UL<<6)
#define   PIPE_ODD_FIELD_INTERRUPT_STATUS	(1UL<<5)
#define   PIPE_EVEN_FIELD_INTERRUPT_STATUS	(1UL<<4)
#define   PIPE_B_PSR_STATUS_VLV			(1UL<<3)
#define   PERF_COUNTER_INTERRUPT_STATUS		(1UL<<3)
#define   PIPE_HOTPLUG_TV_INTERRUPT_STATUS	(1UL<<2) /* pre-965 */
#define   PIPE_START_VBLANK_INTERRUPT_STATUS	(1UL<<2) /* 965 or later */
#define   PIPE_FRAMESTART_INTERRUPT_STATUS	(1UL<<1)
#define   PIPE_VBLANK_INTERRUPT_STATUS		(1UL<<1)
#define   PIPE_HBLANK_INT_STATUS		(1UL<<0)
#define   PIPE_OVERLAY_UPDATED_STATUS		(1UL<<0)

#define PIPESTAT_INT_ENABLE_MASK		0x7fff0000
#define PIPESTAT_INT_STATUS_MASK		0x0000ffff

#define PIPE_A_OFFSET		0x70000
#define PIPE_B_OFFSET		0x71000
#define PIPE_C_OFFSET		0x72000
#define CHV_PIPE_C_OFFSET	0x74000
/*
 * There's actually no pipe EDP. Some pipe registers have
 * simply shifted from the pipe to the transcoder, while
 * keeping their original offset. Thus we need PIPE_EDP_OFFSET
 * to access such registers in transcoder EDP.
 */
#define PIPE_EDP_OFFSET	0x7f000

#define _PIPE2(pipe, reg) (dev_priv->info.pipe_offsets[pipe] - \
	dev_priv->info.pipe_offsets[PIPE_A] + (reg) + \
	dev_priv->info.display_mmio_offset)

#define PIPECONF(pipe) _PIPE2(pipe, _PIPEACONF)
#define PIPEDSL(pipe)  _PIPE2(pipe, _PIPEADSL)
#define PIPEFRAME(pipe) _PIPE2(pipe, _PIPEAFRAMEHIGH)
#define PIPEFRAMEPIXEL(pipe)  _PIPE2(pipe, _PIPEAFRAMEPIXEL)
#define PIPESTAT(pipe) _PIPE2(pipe, _PIPEASTAT)

#define _PIPE_MISC_A			0x70030
#define _PIPE_MISC_B			0x71030
#define   PIPEMISC_DITHER_BPC_MASK	(7<<5)
#define   PIPEMISC_DITHER_8_BPC		(0<<5)
#define   PIPEMISC_DITHER_10_BPC	(1<<5)
#define   PIPEMISC_DITHER_6_BPC		(2<<5)
#define   PIPEMISC_DITHER_12_BPC	(3<<5)
#define   PIPEMISC_DITHER_ENABLE	(1<<4)
#define   PIPEMISC_DITHER_TYPE_MASK	(3<<2)
#define   PIPEMISC_DITHER_TYPE_SP	(0<<2)
#define PIPEMISC(pipe) _PIPE2(pipe, _PIPE_MISC_A)

#define VLV_DPFLIPSTAT				(VLV_DISPLAY_BASE + 0x70028)
#define   PIPEB_LINE_COMPARE_INT_EN		(1<<29)
#define   PIPEB_HLINE_INT_EN			(1<<28)
#define   PIPEB_VBLANK_INT_EN			(1<<27)
#define   SPRITED_FLIP_DONE_INT_EN		(1<<26)
#define   SPRITEC_FLIP_DONE_INT_EN		(1<<25)
#define   PLANEB_FLIP_DONE_INT_EN		(1<<24)
#define   PIPE_PSR_INT_EN			(1<<22)
#define   PIPEA_LINE_COMPARE_INT_EN		(1<<21)
#define   PIPEA_HLINE_INT_EN			(1<<20)
#define   PIPEA_VBLANK_INT_EN			(1<<19)
#define   SPRITEB_FLIP_DONE_INT_EN		(1<<18)
#define   SPRITEA_FLIP_DONE_INT_EN		(1<<17)
#define   PLANEA_FLIPDONE_INT_EN		(1<<16)
#define   PIPEC_LINE_COMPARE_INT_EN		(1<<13)
#define   PIPEC_HLINE_INT_EN			(1<<12)
#define   PIPEC_VBLANK_INT_EN			(1<<11)
#define   SPRITEF_FLIPDONE_INT_EN		(1<<10)
#define   SPRITEE_FLIPDONE_INT_EN		(1<<9)
#define   PLANEC_FLIPDONE_INT_EN		(1<<8)

#define DPINVGTT				(VLV_DISPLAY_BASE + 0x7002c) /* VLV/CHV only */
#define   SPRITEF_INVALID_GTT_INT_EN		(1<<27)
#define   SPRITEE_INVALID_GTT_INT_EN		(1<<26)
#define   PLANEC_INVALID_GTT_INT_EN		(1<<25)
#define   CURSORC_INVALID_GTT_INT_EN		(1<<24)
#define   CURSORB_INVALID_GTT_INT_EN		(1<<23)
#define   CURSORA_INVALID_GTT_INT_EN		(1<<22)
#define   SPRITED_INVALID_GTT_INT_EN		(1<<21)
#define   SPRITEC_INVALID_GTT_INT_EN		(1<<20)
#define   PLANEB_INVALID_GTT_INT_EN		(1<<19)
#define   SPRITEB_INVALID_GTT_INT_EN		(1<<18)
#define   SPRITEA_INVALID_GTT_INT_EN		(1<<17)
#define   PLANEA_INVALID_GTT_INT_EN		(1<<16)
#define   DPINVGTT_EN_MASK			0xff0000
#define   DPINVGTT_EN_MASK_CHV			0xfff0000
#define   SPRITEF_INVALID_GTT_STATUS		(1<<11)
#define   SPRITEE_INVALID_GTT_STATUS		(1<<10)
#define   PLANEC_INVALID_GTT_STATUS		(1<<9)
#define   CURSORC_INVALID_GTT_STATUS		(1<<8)
#define   CURSORB_INVALID_GTT_STATUS		(1<<7)
#define   CURSORA_INVALID_GTT_STATUS		(1<<6)
#define   SPRITED_INVALID_GTT_STATUS		(1<<5)
#define   SPRITEC_INVALID_GTT_STATUS		(1<<4)
#define   PLANEB_INVALID_GTT_STATUS		(1<<3)
#define   SPRITEB_INVALID_GTT_STATUS		(1<<2)
#define   SPRITEA_INVALID_GTT_STATUS		(1<<1)
#define   PLANEA_INVALID_GTT_STATUS		(1<<0)
#define   DPINVGTT_STATUS_MASK			0xff
#define   DPINVGTT_STATUS_MASK_CHV		0xfff

#define DSPARB			0x70030
#define   DSPARB_CSTART_MASK	(0x7f << 7)
#define   DSPARB_CSTART_SHIFT	7
#define   DSPARB_BSTART_MASK	(0x7f)
#define   DSPARB_BSTART_SHIFT	0
#define   DSPARB_BEND_SHIFT	9 /* on 855 */
#define   DSPARB_AEND_SHIFT	0

/* pnv/gen4/g4x/vlv/chv */
#define DSPFW1			(dev_priv->info.display_mmio_offset + 0x70034)
#define   DSPFW_SR_SHIFT		23
#define   DSPFW_SR_MASK			(0x1ff<<23)
#define   DSPFW_CURSORB_SHIFT		16
#define   DSPFW_CURSORB_MASK		(0x3f<<16)
#define   DSPFW_PLANEB_SHIFT		8
#define   DSPFW_PLANEB_MASK		(0x7f<<8)
#define   DSPFW_PLANEB_MASK_VLV		(0xff<<8) /* vlv/chv */
#define   DSPFW_PLANEA_SHIFT		0
#define   DSPFW_PLANEA_MASK		(0x7f<<0)
#define   DSPFW_PLANEA_MASK_VLV		(0xff<<0) /* vlv/chv */
#define DSPFW2			(dev_priv->info.display_mmio_offset + 0x70038)
#define   DSPFW_FBC_SR_EN		(1<<31)	  /* g4x */
#define   DSPFW_FBC_SR_SHIFT		28
#define   DSPFW_FBC_SR_MASK		(0x7<<28) /* g4x */
#define   DSPFW_FBC_HPLL_SR_SHIFT	24
#define   DSPFW_FBC_HPLL_SR_MASK	(0xf<<24) /* g4x */
#define   DSPFW_SPRITEB_SHIFT		(16)
#define   DSPFW_SPRITEB_MASK		(0x7f<<16) /* g4x */
#define   DSPFW_SPRITEB_MASK_VLV	(0xff<<16) /* vlv/chv */
#define   DSPFW_CURSORA_SHIFT		8
#define   DSPFW_CURSORA_MASK		(0x3f<<8)
#define   DSPFW_PLANEC_SHIFT_OLD	0
#define   DSPFW_PLANEC_MASK_OLD		(0x7f<<0) /* pre-gen4 sprite C */
#define   DSPFW_SPRITEA_SHIFT		0
#define   DSPFW_SPRITEA_MASK		(0x7f<<0) /* g4x */
#define   DSPFW_SPRITEA_MASK_VLV	(0xff<<0) /* vlv/chv */
#define DSPFW3			(dev_priv->info.display_mmio_offset + 0x7003c)
#define   DSPFW_HPLL_SR_EN		(1<<31)
#define   PINEVIEW_SELF_REFRESH_EN	(1<<30)
#define   DSPFW_CURSOR_SR_SHIFT		24
#define   DSPFW_CURSOR_SR_MASK		(0x3f<<24)
#define   DSPFW_HPLL_CURSOR_SHIFT	16
#define   DSPFW_HPLL_CURSOR_MASK	(0x3f<<16)
#define   DSPFW_HPLL_SR_SHIFT		0
#define   DSPFW_HPLL_SR_MASK		(0x1ff<<0)

/* vlv/chv */
#define DSPFW4			(VLV_DISPLAY_BASE + 0x70070)
#define   DSPFW_SPRITEB_WM1_SHIFT	16
#define   DSPFW_SPRITEB_WM1_MASK	(0xff<<16)
#define   DSPFW_CURSORA_WM1_SHIFT	8
#define   DSPFW_CURSORA_WM1_MASK	(0x3f<<8)
#define   DSPFW_SPRITEA_WM1_SHIFT	0
#define   DSPFW_SPRITEA_WM1_MASK	(0xff<<0)
#define DSPFW5			(VLV_DISPLAY_BASE + 0x70074)
#define   DSPFW_PLANEB_WM1_SHIFT	24
#define   DSPFW_PLANEB_WM1_MASK		(0xff<<24)
#define   DSPFW_PLANEA_WM1_SHIFT	16
#define   DSPFW_PLANEA_WM1_MASK		(0xff<<16)
#define   DSPFW_CURSORB_WM1_SHIFT	8
#define   DSPFW_CURSORB_WM1_MASK	(0x3f<<8)
#define   DSPFW_CURSOR_SR_WM1_SHIFT	0
#define   DSPFW_CURSOR_SR_WM1_MASK	(0x3f<<0)
#define DSPFW6			(VLV_DISPLAY_BASE + 0x70078)
#define   DSPFW_SR_WM1_SHIFT		0
#define   DSPFW_SR_WM1_MASK		(0x1ff<<0)
#define DSPFW7			(VLV_DISPLAY_BASE + 0x7007c)
#define DSPFW7_CHV		(VLV_DISPLAY_BASE + 0x700b4) /* wtf #1? */
#define   DSPFW_SPRITED_WM1_SHIFT	24
#define   DSPFW_SPRITED_WM1_MASK	(0xff<<24)
#define   DSPFW_SPRITED_SHIFT		16
#define   DSPFW_SPRITED_MASK		(0xff<<16)
#define   DSPFW_SPRITEC_WM1_SHIFT	8
#define   DSPFW_SPRITEC_WM1_MASK	(0xff<<8)
#define   DSPFW_SPRITEC_SHIFT		0
#define   DSPFW_SPRITEC_MASK		(0xff<<0)
#define DSPFW8_CHV		(VLV_DISPLAY_BASE + 0x700b8)
#define   DSPFW_SPRITEF_WM1_SHIFT	24
#define   DSPFW_SPRITEF_WM1_MASK	(0xff<<24)
#define   DSPFW_SPRITEF_SHIFT		16
#define   DSPFW_SPRITEF_MASK		(0xff<<16)
#define   DSPFW_SPRITEE_WM1_SHIFT	8
#define   DSPFW_SPRITEE_WM1_MASK	(0xff<<8)
#define   DSPFW_SPRITEE_SHIFT		0
#define   DSPFW_SPRITEE_MASK		(0xff<<0)
#define DSPFW9_CHV		(VLV_DISPLAY_BASE + 0x7007c) /* wtf #2? */
#define   DSPFW_PLANEC_WM1_SHIFT	24
#define   DSPFW_PLANEC_WM1_MASK		(0xff<<24)
#define   DSPFW_PLANEC_SHIFT		16
#define   DSPFW_PLANEC_MASK		(0xff<<16)
#define   DSPFW_CURSORC_WM1_SHIFT	8
#define   DSPFW_CURSORC_WM1_MASK	(0x3f<<16)
#define   DSPFW_CURSORC_SHIFT		0
#define   DSPFW_CURSORC_MASK		(0x3f<<0)

/* vlv/chv high order bits */
#define DSPHOWM			(VLV_DISPLAY_BASE + 0x70064)
#define   DSPFW_SR_HI_SHIFT		24
#define   DSPFW_SR_HI_MASK		(1<<24)
#define   DSPFW_SPRITEF_HI_SHIFT	23
#define   DSPFW_SPRITEF_HI_MASK		(1<<23)
#define   DSPFW_SPRITEE_HI_SHIFT	22
#define   DSPFW_SPRITEE_HI_MASK		(1<<22)
#define   DSPFW_PLANEC_HI_SHIFT		21
#define   DSPFW_PLANEC_HI_MASK		(1<<21)
#define   DSPFW_SPRITED_HI_SHIFT	20
#define   DSPFW_SPRITED_HI_MASK		(1<<20)
#define   DSPFW_SPRITEC_HI_SHIFT	16
#define   DSPFW_SPRITEC_HI_MASK		(1<<16)
#define   DSPFW_PLANEB_HI_SHIFT		12
#define   DSPFW_PLANEB_HI_MASK		(1<<12)
#define   DSPFW_SPRITEB_HI_SHIFT	8
#define   DSPFW_SPRITEB_HI_MASK		(1<<8)
#define   DSPFW_SPRITEA_HI_SHIFT	4
#define   DSPFW_SPRITEA_HI_MASK		(1<<4)
#define   DSPFW_PLANEA_HI_SHIFT		0
#define   DSPFW_PLANEA_HI_MASK		(1<<0)
#define DSPHOWM1		(VLV_DISPLAY_BASE + 0x70068)
#define   DSPFW_SR_WM1_HI_SHIFT		24
#define   DSPFW_SR_WM1_HI_MASK		(1<<24)
#define   DSPFW_SPRITEF_WM1_HI_SHIFT	23
#define   DSPFW_SPRITEF_WM1_HI_MASK	(1<<23)
#define   DSPFW_SPRITEE_WM1_HI_SHIFT	22
#define   DSPFW_SPRITEE_WM1_HI_MASK	(1<<22)
#define   DSPFW_PLANEC_WM1_HI_SHIFT	21
#define   DSPFW_PLANEC_WM1_HI_MASK	(1<<21)
#define   DSPFW_SPRITED_WM1_HI_SHIFT	20
#define   DSPFW_SPRITED_WM1_HI_MASK	(1<<20)
#define   DSPFW_SPRITEC_WM1_HI_SHIFT	16
#define   DSPFW_SPRITEC_WM1_HI_MASK	(1<<16)
#define   DSPFW_PLANEB_WM1_HI_SHIFT	12
#define   DSPFW_PLANEB_WM1_HI_MASK	(1<<12)
#define   DSPFW_SPRITEB_WM1_HI_SHIFT	8
#define   DSPFW_SPRITEB_WM1_HI_MASK	(1<<8)
#define   DSPFW_SPRITEA_WM1_HI_SHIFT	4
#define   DSPFW_SPRITEA_WM1_HI_MASK	(1<<4)
#define   DSPFW_PLANEA_WM1_HI_SHIFT	0
#define   DSPFW_PLANEA_WM1_HI_MASK	(1<<0)

/* drain latency register values*/
#define DRAIN_LATENCY_PRECISION_16	16
#define DRAIN_LATENCY_PRECISION_32	32
#define DRAIN_LATENCY_PRECISION_64	64
#define VLV_DDL(pipe)			(VLV_DISPLAY_BASE + 0x70050 + 4 * (pipe))
#define DDL_CURSOR_PRECISION_HIGH	(1<<31)
#define DDL_CURSOR_PRECISION_LOW	(0<<31)
#define DDL_CURSOR_SHIFT		24
#define DDL_SPRITE_PRECISION_HIGH(sprite)	(1<<(15+8*(sprite)))
#define DDL_SPRITE_PRECISION_LOW(sprite)	(0<<(15+8*(sprite)))
#define DDL_SPRITE_SHIFT(sprite)	(8+8*(sprite))
#define DDL_PLANE_PRECISION_HIGH	(1<<7)
#define DDL_PLANE_PRECISION_LOW		(0<<7)
#define DDL_PLANE_SHIFT			0
#define DRAIN_LATENCY_MASK		0x7f

/* FIFO watermark sizes etc */
#define G4X_FIFO_LINE_SIZE	64
#define I915_FIFO_LINE_SIZE	64
#define I830_FIFO_LINE_SIZE	32

#define VALLEYVIEW_FIFO_SIZE	255
#define G4X_FIFO_SIZE		127
#define I965_FIFO_SIZE		512
#define I945_FIFO_SIZE		127
#define I915_FIFO_SIZE		95
#define I855GM_FIFO_SIZE	127 /* In cachelines */
#define I830_FIFO_SIZE		95

#define VALLEYVIEW_MAX_WM	0xff
#define G4X_MAX_WM		0x3f
#define I915_MAX_WM		0x3f

#define PINEVIEW_DISPLAY_FIFO	512 /* in 64byte unit */
#define PINEVIEW_FIFO_LINE_SIZE	64
#define PINEVIEW_MAX_WM		0x1ff
#define PINEVIEW_DFT_WM		0x3f
#define PINEVIEW_DFT_HPLLOFF_WM	0
#define PINEVIEW_GUARD_WM		10
#define PINEVIEW_CURSOR_FIFO		64
#define PINEVIEW_CURSOR_MAX_WM	0x3f
#define PINEVIEW_CURSOR_DFT_WM	0
#define PINEVIEW_CURSOR_GUARD_WM	5

#define VALLEYVIEW_CURSOR_MAX_WM 64
#define I965_CURSOR_FIFO	64
#define I965_CURSOR_MAX_WM	32
#define I965_CURSOR_DFT_WM	8

/* Watermark register definitions for SKL */
#define CUR_WM_A_0		0x70140
#define CUR_WM_B_0		0x71140
#define PLANE_WM_1_A_0		0x70240
#define PLANE_WM_1_B_0		0x71240
#define PLANE_WM_2_A_0		0x70340
#define PLANE_WM_2_B_0		0x71340
#define PLANE_WM_TRANS_1_A_0	0x70268
#define PLANE_WM_TRANS_1_B_0	0x71268
#define PLANE_WM_TRANS_2_A_0	0x70368
#define PLANE_WM_TRANS_2_B_0	0x71368
#define CUR_WM_TRANS_A_0	0x70168
#define CUR_WM_TRANS_B_0	0x71168
#define   PLANE_WM_EN		(1 << 31)
#define   PLANE_WM_LINES_SHIFT	14
#define   PLANE_WM_LINES_MASK	0x1f
#define   PLANE_WM_BLOCKS_MASK	0x3ff

#define CUR_WM_0(pipe) _PIPE(pipe, CUR_WM_A_0, CUR_WM_B_0)
#define CUR_WM(pipe, level) (CUR_WM_0(pipe) + ((4) * (level)))
#define CUR_WM_TRANS(pipe) _PIPE(pipe, CUR_WM_TRANS_A_0, CUR_WM_TRANS_B_0)

#define _PLANE_WM_1(pipe) _PIPE(pipe, PLANE_WM_1_A_0, PLANE_WM_1_B_0)
#define _PLANE_WM_2(pipe) _PIPE(pipe, PLANE_WM_2_A_0, PLANE_WM_2_B_0)
#define _PLANE_WM_BASE(pipe, plane)	\
			_PLANE(plane, _PLANE_WM_1(pipe), _PLANE_WM_2(pipe))
#define PLANE_WM(pipe, plane, level)	\
			(_PLANE_WM_BASE(pipe, plane) + ((4) * (level)))
#define _PLANE_WM_TRANS_1(pipe)	\
			_PIPE(pipe, PLANE_WM_TRANS_1_A_0, PLANE_WM_TRANS_1_B_0)
#define _PLANE_WM_TRANS_2(pipe)	\
			_PIPE(pipe, PLANE_WM_TRANS_2_A_0, PLANE_WM_TRANS_2_B_0)
#define PLANE_WM_TRANS(pipe, plane)	\
		_PLANE(plane, _PLANE_WM_TRANS_1(pipe), _PLANE_WM_TRANS_2(pipe))

/* define the Watermark register on Ironlake */
#define WM0_PIPEA_ILK		0x45100
#define  WM0_PIPE_PLANE_MASK	(0xffff<<16)
#define  WM0_PIPE_PLANE_SHIFT	16
#define  WM0_PIPE_SPRITE_MASK	(0xff<<8)
#define  WM0_PIPE_SPRITE_SHIFT	8
#define  WM0_PIPE_CURSOR_MASK	(0xff)

#define WM0_PIPEB_ILK		0x45104
#define WM0_PIPEC_IVB		0x45200
#define WM1_LP_ILK		0x45108
#define  WM1_LP_SR_EN		(1<<31)
#define  WM1_LP_LATENCY_SHIFT	24
#define  WM1_LP_LATENCY_MASK	(0x7f<<24)
#define  WM1_LP_FBC_MASK	(0xf<<20)
#define  WM1_LP_FBC_SHIFT	20
#define  WM1_LP_FBC_SHIFT_BDW	19
#define  WM1_LP_SR_MASK		(0x7ff<<8)
#define  WM1_LP_SR_SHIFT	8
#define  WM1_LP_CURSOR_MASK	(0xff)
#define WM2_LP_ILK		0x4510c
#define  WM2_LP_EN		(1<<31)
#define WM3_LP_ILK		0x45110
#define  WM3_LP_EN		(1<<31)
#define WM1S_LP_ILK		0x45120
#define WM2S_LP_IVB		0x45124
#define WM3S_LP_IVB		0x45128
#define  WM1S_LP_EN		(1<<31)

#define HSW_WM_LP_VAL(lat, fbc, pri, cur) \
	(WM3_LP_EN | ((lat) << WM1_LP_LATENCY_SHIFT) | \
	 ((fbc) << WM1_LP_FBC_SHIFT) | ((pri) << WM1_LP_SR_SHIFT) | (cur))

/* Memory latency timer register */
#define MLTR_ILK		0x11222
#define  MLTR_WM1_SHIFT		0
#define  MLTR_WM2_SHIFT		8
/* the unit of memory self-refresh latency time is 0.5us */
#define  ILK_SRLT_MASK		0x3f


/* the address where we get all kinds of latency value */
#define SSKPD			0x5d10
#define SSKPD_WM_MASK		0x3f
#define SSKPD_WM0_SHIFT		0
#define SSKPD_WM1_SHIFT		8
#define SSKPD_WM2_SHIFT		16
#define SSKPD_WM3_SHIFT		24

/*
 * The two pipe frame counter registers are not synchronized, so
 * reading a stable value is somewhat tricky. The following code
 * should work:
 *
 *  do {
 *    high1 = ((INREG(PIPEAFRAMEHIGH) & PIPE_FRAME_HIGH_MASK) >>
 *             PIPE_FRAME_HIGH_SHIFT;
 *    low1 =  ((INREG(PIPEAFRAMEPIXEL) & PIPE_FRAME_LOW_MASK) >>
 *             PIPE_FRAME_LOW_SHIFT);
 *    high2 = ((INREG(PIPEAFRAMEHIGH) & PIPE_FRAME_HIGH_MASK) >>
 *             PIPE_FRAME_HIGH_SHIFT);
 *  } while (high1 != high2);
 *  frame = (high1 << 8) | low1;
 */
#define _PIPEAFRAMEHIGH          0x70040
#define   PIPE_FRAME_HIGH_MASK    0x0000ffff
#define   PIPE_FRAME_HIGH_SHIFT   0
#define _PIPEAFRAMEPIXEL         0x70044
#define   PIPE_FRAME_LOW_MASK     0xff000000
#define   PIPE_FRAME_LOW_SHIFT    24
#define   PIPE_PIXEL_MASK         0x00ffffff
#define   PIPE_PIXEL_SHIFT        0
/* GM45+ just has to be different */
#define _PIPEA_FRMCOUNT_GM45	0x70040
#define _PIPEA_FLIPCOUNT_GM45	0x70044
#define PIPE_FRMCOUNT_GM45(pipe) _PIPE2(pipe, _PIPEA_FRMCOUNT_GM45)
#define PIPE_FLIPCOUNT_GM45(pipe) _PIPE2(pipe, _PIPEA_FLIPCOUNT_GM45)

/* Cursor A & B regs */
#define _CURACNTR		0x70080
/* Old style CUR*CNTR flags (desktop 8xx) */
#define   CURSOR_ENABLE		0x80000000
#define   CURSOR_GAMMA_ENABLE	0x40000000
#define   CURSOR_STRIDE_SHIFT	28
#define   CURSOR_STRIDE(x)	((ffs(x)-9) << CURSOR_STRIDE_SHIFT) /* 256,512,1k,2k */
#define   CURSOR_PIPE_CSC_ENABLE (1<<24)
#define   CURSOR_FORMAT_SHIFT	24
#define   CURSOR_FORMAT_MASK	(0x07 << CURSOR_FORMAT_SHIFT)
#define   CURSOR_FORMAT_2C	(0x00 << CURSOR_FORMAT_SHIFT)
#define   CURSOR_FORMAT_3C	(0x01 << CURSOR_FORMAT_SHIFT)
#define   CURSOR_FORMAT_4C	(0x02 << CURSOR_FORMAT_SHIFT)
#define   CURSOR_FORMAT_ARGB	(0x04 << CURSOR_FORMAT_SHIFT)
#define   CURSOR_FORMAT_XRGB	(0x05 << CURSOR_FORMAT_SHIFT)
/* New style CUR*CNTR flags */
#define   CURSOR_MODE		0x27
#define   CURSOR_MODE_DISABLE   0x00
#define   CURSOR_MODE_128_32B_AX 0x02
#define   CURSOR_MODE_256_32B_AX 0x03
#define   CURSOR_MODE_64_32B_AX 0x07
#define   CURSOR_MODE_128_ARGB_AX ((1 << 5) | CURSOR_MODE_128_32B_AX)
#define   CURSOR_MODE_256_ARGB_AX ((1 << 5) | CURSOR_MODE_256_32B_AX)
#define   CURSOR_MODE_64_ARGB_AX ((1 << 5) | CURSOR_MODE_64_32B_AX)
#define   MCURSOR_PIPE_SELECT	(1 << 28)
#define   MCURSOR_PIPE_A	0x00
#define   MCURSOR_PIPE_B	(1 << 28)
#define   MCURSOR_GAMMA_ENABLE  (1 << 26)
#define   CURSOR_ROTATE_180	(1<<15)
#define   CURSOR_TRICKLE_FEED_DISABLE	(1 << 14)
#define _CURABASE		0x70084
#define _CURAPOS		0x70088
#define   CURSOR_POS_MASK       0x007FF
#define   CURSOR_POS_SIGN       0x8000
#define   CURSOR_X_SHIFT        0
#define   CURSOR_Y_SHIFT        16
#define CURSIZE			0x700a0
#define _CURBCNTR		0x700c0
#define _CURBBASE		0x700c4
#define _CURBPOS		0x700c8

#define _CURBCNTR_IVB		0x71080
#define _CURBBASE_IVB		0x71084
#define _CURBPOS_IVB		0x71088

#define _CURSOR2(pipe, reg) (dev_priv->info.cursor_offsets[(pipe)] - \
	dev_priv->info.cursor_offsets[PIPE_A] + (reg) + \
	dev_priv->info.display_mmio_offset)

#define CURCNTR(pipe) _CURSOR2(pipe, _CURACNTR)
#define CURBASE(pipe) _CURSOR2(pipe, _CURABASE)
#define CURPOS(pipe) _CURSOR2(pipe, _CURAPOS)

#define CURSOR_A_OFFSET 0x70080
#define CURSOR_B_OFFSET 0x700c0
#define CHV_CURSOR_C_OFFSET 0x700e0
#define IVB_CURSOR_B_OFFSET 0x71080
#define IVB_CURSOR_C_OFFSET 0x72080

/* Display A control */
#define _DSPACNTR				0x70180
#define   DISPLAY_PLANE_ENABLE			(1<<31)
#define   DISPLAY_PLANE_DISABLE			0
#define   DISPPLANE_GAMMA_ENABLE		(1<<30)
#define   DISPPLANE_GAMMA_DISABLE		0
#define   DISPPLANE_PIXFORMAT_MASK		(0xf<<26)
#define   DISPPLANE_YUV422			(0x0<<26)
#define   DISPPLANE_8BPP			(0x2<<26)
#define   DISPPLANE_BGRA555			(0x3<<26)
#define   DISPPLANE_BGRX555			(0x4<<26)
#define   DISPPLANE_BGRX565			(0x5<<26)
#define   DISPPLANE_BGRX888			(0x6<<26)
#define   DISPPLANE_BGRA888			(0x7<<26)
#define   DISPPLANE_RGBX101010			(0x8<<26)
#define   DISPPLANE_RGBA101010			(0x9<<26)
#define   DISPPLANE_BGRX101010			(0xa<<26)
#define   DISPPLANE_RGBX161616			(0xc<<26)
#define   DISPPLANE_RGBX888			(0xe<<26)
#define   DISPPLANE_RGBA888			(0xf<<26)
#define   DISPPLANE_STEREO_ENABLE		(1<<25)
#define   DISPPLANE_STEREO_DISABLE		0
#define   DISPPLANE_PIPE_CSC_ENABLE		(1<<24)
#define   DISPPLANE_SEL_PIPE_SHIFT		24
#define   DISPPLANE_SEL_PIPE_MASK		(3<<DISPPLANE_SEL_PIPE_SHIFT)
#define   DISPPLANE_SEL_PIPE_A			0
#define   DISPPLANE_SEL_PIPE_B			(1<<DISPPLANE_SEL_PIPE_SHIFT)
#define   DISPPLANE_SRC_KEY_ENABLE		(1<<22)
#define   DISPPLANE_SRC_KEY_DISABLE		0
#define   DISPPLANE_LINE_DOUBLE			(1<<20)
#define   DISPPLANE_NO_LINE_DOUBLE		0
#define   DISPPLANE_STEREO_POLARITY_FIRST	0
#define   DISPPLANE_STEREO_POLARITY_SECOND	(1<<18)
#define   DISPPLANE_ALPHA_PREMULTIPLY		(1<<16) /* CHV pipe B */
#define   DISPPLANE_ROTATE_180			(1<<15)
#define   DISPPLANE_TRICKLE_FEED_DISABLE	(1<<14) /* Ironlake */
#define   DISPPLANE_TILED			(1<<10)
#define   DISPPLANE_MIRROR			(1<<8) /* CHV pipe B */
#define _DSPAADDR				0x70184
#define _DSPASTRIDE				0x70188
#define _DSPAPOS				0x7018C /* reserved */
#define _DSPASIZE				0x70190
#define _DSPASURF				0x7019C /* 965+ only */
#define _DSPATILEOFF				0x701A4 /* 965+ only */
#define _DSPAOFFSET				0x701A4 /* HSW */
#define _DSPASURFLIVE				0x701AC

#define DSPCNTR(plane) _PIPE2(plane, _DSPACNTR)
#define DSPADDR(plane) _PIPE2(plane, _DSPAADDR)
#define DSPSTRIDE(plane) _PIPE2(plane, _DSPASTRIDE)
#define DSPPOS(plane) _PIPE2(plane, _DSPAPOS)
#define DSPSIZE(plane) _PIPE2(plane, _DSPASIZE)
#define DSPSURF(plane) _PIPE2(plane, _DSPASURF)
#define DSPTILEOFF(plane) _PIPE2(plane, _DSPATILEOFF)
#define DSPLINOFF(plane) DSPADDR(plane)
#define DSPOFFSET(plane) _PIPE2(plane, _DSPAOFFSET)
#define DSPSURFLIVE(plane) _PIPE2(plane, _DSPASURFLIVE)

/* CHV pipe B blender and primary plane */
#define _CHV_BLEND_A		0x60a00
#define   CHV_BLEND_LEGACY		(0<<30)
#define   CHV_BLEND_ANDROID		(1<<30)
#define   CHV_BLEND_MPO			(2<<30)
#define   CHV_BLEND_MASK		(3<<30)
#define _CHV_CANVAS_A		0x60a04
#define _PRIMPOS_A		0x60a08
#define _PRIMSIZE_A		0x60a0c
#define _PRIMCNSTALPHA_A	0x60a10
#define   PRIM_CONST_ALPHA_ENABLE	(1<<31)

#define CHV_BLEND(pipe) _TRANSCODER2(pipe, _CHV_BLEND_A)
#define CHV_CANVAS(pipe) _TRANSCODER2(pipe, _CHV_CANVAS_A)
#define PRIMPOS(plane) _TRANSCODER2(plane, _PRIMPOS_A)
#define PRIMSIZE(plane) _TRANSCODER2(plane, _PRIMSIZE_A)
#define PRIMCNSTALPHA(plane) _TRANSCODER2(plane, _PRIMCNSTALPHA_A)

/* Display/Sprite base address macros */
#define DISP_BASEADDR_MASK	(0xfffff000)
#define I915_LO_DISPBASE(val)	(val & ~DISP_BASEADDR_MASK)
#define I915_HI_DISPBASE(val)	(val & DISP_BASEADDR_MASK)

/* VBIOS flags */
#define SWF00			(dev_priv->info.display_mmio_offset + 0x71410)
#define SWF01			(dev_priv->info.display_mmio_offset + 0x71414)
#define SWF02			(dev_priv->info.display_mmio_offset + 0x71418)
#define SWF03			(dev_priv->info.display_mmio_offset + 0x7141c)
#define SWF04			(dev_priv->info.display_mmio_offset + 0x71420)
#define SWF05			(dev_priv->info.display_mmio_offset + 0x71424)
#define SWF06			(dev_priv->info.display_mmio_offset + 0x71428)
#define SWF10			(dev_priv->info.display_mmio_offset + 0x70410)
#define SWF11			(dev_priv->info.display_mmio_offset + 0x70414)
#define SWF14			(dev_priv->info.display_mmio_offset + 0x71420)
#define SWF30			(dev_priv->info.display_mmio_offset + 0x72414)
#define SWF31			(dev_priv->info.display_mmio_offset + 0x72418)
#define SWF32			(dev_priv->info.display_mmio_offset + 0x7241c)

/* Pipe B */
#define _PIPEBDSL		(dev_priv->info.display_mmio_offset + 0x71000)
#define _PIPEBCONF		(dev_priv->info.display_mmio_offset + 0x71008)
#define _PIPEBSTAT		(dev_priv->info.display_mmio_offset + 0x71024)
#define _PIPEBFRAMEHIGH		0x71040
#define _PIPEBFRAMEPIXEL	0x71044
#define _PIPEB_FRMCOUNT_GM45	(dev_priv->info.display_mmio_offset + 0x71040)
#define _PIPEB_FLIPCOUNT_GM45	(dev_priv->info.display_mmio_offset + 0x71044)


/* Display B control */
#define _DSPBCNTR		(dev_priv->info.display_mmio_offset + 0x71180)
#define   DISPPLANE_ALPHA_TRANS_ENABLE		(1<<15)
#define   DISPPLANE_ALPHA_TRANS_DISABLE		0
#define   DISPPLANE_SPRITE_ABOVE_DISPLAY	0
#define   DISPPLANE_SPRITE_ABOVE_OVERLAY	(1)
#define _DSPBADDR		(dev_priv->info.display_mmio_offset + 0x71184)
#define _DSPBSTRIDE		(dev_priv->info.display_mmio_offset + 0x71188)
#define _DSPBPOS		(dev_priv->info.display_mmio_offset + 0x7118C)
#define _DSPBSIZE		(dev_priv->info.display_mmio_offset + 0x71190)
#define _DSPBSURF		(dev_priv->info.display_mmio_offset + 0x7119C)
#define _DSPBTILEOFF		(dev_priv->info.display_mmio_offset + 0x711A4)
#define _DSPBOFFSET		(dev_priv->info.display_mmio_offset + 0x711A4)
#define _DSPBSURFLIVE		(dev_priv->info.display_mmio_offset + 0x711AC)

/* Sprite A control */
#define _DVSACNTR		0x72180
#define   DVS_ENABLE		(1<<31)
#define   DVS_GAMMA_ENABLE	(1<<30)
#define   DVS_PIXFORMAT_MASK	(3<<25)
#define   DVS_FORMAT_YUV422	(0<<25)
#define   DVS_FORMAT_RGBX101010	(1<<25)
#define   DVS_FORMAT_RGBX888	(2<<25)
#define   DVS_FORMAT_RGBX161616	(3<<25)
#define   DVS_PIPE_CSC_ENABLE   (1<<24)
#define   DVS_SOURCE_KEY	(1<<22)
#define   DVS_RGB_ORDER_XBGR	(1<<20)
#define   DVS_YUV_BYTE_ORDER_MASK (3<<16)
#define   DVS_YUV_ORDER_YUYV	(0<<16)
#define   DVS_YUV_ORDER_UYVY	(1<<16)
#define   DVS_YUV_ORDER_YVYU	(2<<16)
#define   DVS_YUV_ORDER_VYUY	(3<<16)
#define   DVS_ROTATE_180	(1<<15)
#define   DVS_DEST_KEY		(1<<2)
#define   DVS_TRICKLE_FEED_DISABLE (1<<14)
#define   DVS_TILED		(1<<10)
#define _DVSALINOFF		0x72184
#define _DVSASTRIDE		0x72188
#define _DVSAPOS		0x7218c
#define _DVSASIZE		0x72190
#define _DVSAKEYVAL		0x72194
#define _DVSAKEYMSK		0x72198
#define _DVSASURF		0x7219c
#define _DVSAKEYMAXVAL		0x721a0
#define _DVSATILEOFF		0x721a4
#define _DVSASURFLIVE		0x721ac
#define _DVSASCALE		0x72204
#define   DVS_SCALE_ENABLE	(1<<31)
#define   DVS_FILTER_MASK	(3<<29)
#define   DVS_FILTER_MEDIUM	(0<<29)
#define   DVS_FILTER_ENHANCING	(1<<29)
#define   DVS_FILTER_SOFTENING	(2<<29)
#define   DVS_VERTICAL_OFFSET_HALF (1<<28) /* must be enabled below */
#define   DVS_VERTICAL_OFFSET_ENABLE (1<<27)
#define _DVSAGAMC		0x72300

#define _DVSBCNTR		0x73180
#define _DVSBLINOFF		0x73184
#define _DVSBSTRIDE		0x73188
#define _DVSBPOS		0x7318c
#define _DVSBSIZE		0x73190
#define _DVSBKEYVAL		0x73194
#define _DVSBKEYMSK		0x73198
#define _DVSBSURF		0x7319c
#define _DVSBKEYMAXVAL		0x731a0
#define _DVSBTILEOFF		0x731a4
#define _DVSBSURFLIVE		0x731ac
#define _DVSBSCALE		0x73204
#define _DVSBGAMC		0x73300

#define DVSCNTR(pipe) _PIPE(pipe, _DVSACNTR, _DVSBCNTR)
#define DVSLINOFF(pipe) _PIPE(pipe, _DVSALINOFF, _DVSBLINOFF)
#define DVSSTRIDE(pipe) _PIPE(pipe, _DVSASTRIDE, _DVSBSTRIDE)
#define DVSPOS(pipe) _PIPE(pipe, _DVSAPOS, _DVSBPOS)
#define DVSSURF(pipe) _PIPE(pipe, _DVSASURF, _DVSBSURF)
#define DVSKEYMAX(pipe) _PIPE(pipe, _DVSAKEYMAXVAL, _DVSBKEYMAXVAL)
#define DVSSIZE(pipe) _PIPE(pipe, _DVSASIZE, _DVSBSIZE)
#define DVSSCALE(pipe) _PIPE(pipe, _DVSASCALE, _DVSBSCALE)
#define DVSTILEOFF(pipe) _PIPE(pipe, _DVSATILEOFF, _DVSBTILEOFF)
#define DVSKEYVAL(pipe) _PIPE(pipe, _DVSAKEYVAL, _DVSBKEYVAL)
#define DVSKEYMSK(pipe) _PIPE(pipe, _DVSAKEYMSK, _DVSBKEYMSK)
#define DVSSURFLIVE(pipe) _PIPE(pipe, _DVSASURFLIVE, _DVSBSURFLIVE)

#define _SPRA_CTL		0x70280
#define   SPRITE_ENABLE			(1<<31)
#define   SPRITE_GAMMA_ENABLE		(1<<30)
#define   SPRITE_PIXFORMAT_MASK		(7<<25)
#define   SPRITE_FORMAT_YUV422		(0<<25)
#define   SPRITE_FORMAT_RGBX101010	(1<<25)
#define   SPRITE_FORMAT_RGBX888		(2<<25)
#define   SPRITE_FORMAT_RGBX161616	(3<<25)
#define   SPRITE_FORMAT_YUV444		(4<<25)
#define   SPRITE_FORMAT_XR_BGR101010	(5<<25) /* Extended range */
#define   SPRITE_PIPE_CSC_ENABLE	(1<<24)
#define   SPRITE_SOURCE_KEY		(1<<22)
#define   SPRITE_RGB_ORDER_RGBX		(1<<20) /* only for 888 and 161616 */
#define   SPRITE_YUV_TO_RGB_CSC_DISABLE	(1<<19)
#define   SPRITE_YUV_CSC_FORMAT_BT709	(1<<18) /* 0 is BT601 */
#define   SPRITE_YUV_BYTE_ORDER_MASK	(3<<16)
#define   SPRITE_YUV_ORDER_YUYV		(0<<16)
#define   SPRITE_YUV_ORDER_UYVY		(1<<16)
#define   SPRITE_YUV_ORDER_YVYU		(2<<16)
#define   SPRITE_YUV_ORDER_VYUY		(3<<16)
#define   SPRITE_ROTATE_180		(1<<15)
#define   SPRITE_TRICKLE_FEED_DISABLE	(1<<14)
#define   SPRITE_INT_GAMMA_ENABLE	(1<<13)
#define   SPRITE_TILED			(1<<10)
#define   SPRITE_DEST_KEY		(1<<2)
#define _SPRA_LINOFF		0x70284
#define _SPRA_STRIDE		0x70288
#define _SPRA_POS		0x7028c
#define _SPRA_SIZE		0x70290
#define _SPRA_KEYVAL		0x70294
#define _SPRA_KEYMSK		0x70298
#define _SPRA_SURF		0x7029c
#define _SPRA_KEYMAX		0x702a0
#define _SPRA_TILEOFF		0x702a4
#define _SPRA_OFFSET		0x702a4
#define _SPRA_SURFLIVE		0x702ac
#define _SPRA_SCALE		0x70304
#define   SPRITE_SCALE_ENABLE	(1<<31)
#define   SPRITE_FILTER_MASK	(3<<29)
#define   SPRITE_FILTER_MEDIUM	(0<<29)
#define   SPRITE_FILTER_ENHANCING	(1<<29)
#define   SPRITE_FILTER_SOFTENING	(2<<29)
#define   SPRITE_VERTICAL_OFFSET_HALF	(1<<28) /* must be enabled below */
#define   SPRITE_VERTICAL_OFFSET_ENABLE	(1<<27)
#define _SPRA_GAMC		0x70400

#define _SPRB_CTL		0x71280
#define _SPRB_LINOFF		0x71284
#define _SPRB_STRIDE		0x71288
#define _SPRB_POS		0x7128c
#define _SPRB_SIZE		0x71290
#define _SPRB_KEYVAL		0x71294
#define _SPRB_KEYMSK		0x71298
#define _SPRB_SURF		0x7129c
#define _SPRB_KEYMAX		0x712a0
#define _SPRB_TILEOFF		0x712a4
#define _SPRB_OFFSET		0x712a4
#define _SPRB_SURFLIVE		0x712ac
#define _SPRB_SCALE		0x71304
#define _SPRB_GAMC		0x71400

#define SPRCTL(pipe) _PIPE(pipe, _SPRA_CTL, _SPRB_CTL)
#define SPRLINOFF(pipe) _PIPE(pipe, _SPRA_LINOFF, _SPRB_LINOFF)
#define SPRSTRIDE(pipe) _PIPE(pipe, _SPRA_STRIDE, _SPRB_STRIDE)
#define SPRPOS(pipe) _PIPE(pipe, _SPRA_POS, _SPRB_POS)
#define SPRSIZE(pipe) _PIPE(pipe, _SPRA_SIZE, _SPRB_SIZE)
#define SPRKEYVAL(pipe) _PIPE(pipe, _SPRA_KEYVAL, _SPRB_KEYVAL)
#define SPRKEYMSK(pipe) _PIPE(pipe, _SPRA_KEYMSK, _SPRB_KEYMSK)
#define SPRSURF(pipe) _PIPE(pipe, _SPRA_SURF, _SPRB_SURF)
#define SPRKEYMAX(pipe) _PIPE(pipe, _SPRA_KEYMAX, _SPRB_KEYMAX)
#define SPRTILEOFF(pipe) _PIPE(pipe, _SPRA_TILEOFF, _SPRB_TILEOFF)
#define SPROFFSET(pipe) _PIPE(pipe, _SPRA_OFFSET, _SPRB_OFFSET)
#define SPRSCALE(pipe) _PIPE(pipe, _SPRA_SCALE, _SPRB_SCALE)
#define SPRGAMC(pipe) _PIPE(pipe, _SPRA_GAMC, _SPRB_GAMC)
#define SPRSURFLIVE(pipe) _PIPE(pipe, _SPRA_SURFLIVE, _SPRB_SURFLIVE)

#define _SPACNTR		(VLV_DISPLAY_BASE + 0x72180)
#define   SP_ENABLE			(1<<31)
#define   SP_GAMMA_ENABLE		(1<<30)
#define   SP_PIXFORMAT_MASK		(0xf<<26)
#define   SP_FORMAT_YUV422		(0<<26)
#define   SP_FORMAT_BGR565		(5<<26)
#define   SP_FORMAT_BGRX8888		(6<<26)
#define   SP_FORMAT_BGRA8888		(7<<26)
#define   SP_FORMAT_RGBX1010102		(8<<26)
#define   SP_FORMAT_RGBA1010102		(9<<26)
#define   SP_FORMAT_RGBX8888		(0xe<<26)
#define   SP_FORMAT_RGBA8888		(0xf<<26)
#define   SP_ALPHA_PREMULTIPLY		(1<<23) /* CHV pipe B */
#define   SP_SOURCE_KEY			(1<<22)
#define   SP_YUV_BYTE_ORDER_MASK	(3<<16)
#define   SP_YUV_ORDER_YUYV		(0<<16)
#define   SP_YUV_ORDER_UYVY		(1<<16)
#define   SP_YUV_ORDER_YVYU		(2<<16)
#define   SP_YUV_ORDER_VYUY		(3<<16)
#define   SP_ROTATE_180			(1<<15)
#define   SP_TILED			(1<<10)
#define   SP_MIRROR			(1<<8) /* CHV pipe B */
#define _SPALINOFF		(VLV_DISPLAY_BASE + 0x72184)
#define _SPASTRIDE		(VLV_DISPLAY_BASE + 0x72188)
#define _SPAPOS			(VLV_DISPLAY_BASE + 0x7218c)
#define _SPASIZE		(VLV_DISPLAY_BASE + 0x72190)
#define _SPAKEYMINVAL		(VLV_DISPLAY_BASE + 0x72194)
#define _SPAKEYMSK		(VLV_DISPLAY_BASE + 0x72198)
#define _SPASURF		(VLV_DISPLAY_BASE + 0x7219c)
#define _SPAKEYMAXVAL		(VLV_DISPLAY_BASE + 0x721a0)
#define _SPATILEOFF		(VLV_DISPLAY_BASE + 0x721a4)
#define _SPACONSTALPHA		(VLV_DISPLAY_BASE + 0x721a8)
#define   SP_CONST_ALPHA_ENABLE		(1<<31)
#define _SPAGAMC		(VLV_DISPLAY_BASE + 0x721f4)

#define _SPBCNTR		(VLV_DISPLAY_BASE + 0x72280)
#define _SPBLINOFF		(VLV_DISPLAY_BASE + 0x72284)
#define _SPBSTRIDE		(VLV_DISPLAY_BASE + 0x72288)
#define _SPBPOS			(VLV_DISPLAY_BASE + 0x7228c)
#define _SPBSIZE		(VLV_DISPLAY_BASE + 0x72290)
#define _SPBKEYMINVAL		(VLV_DISPLAY_BASE + 0x72294)
#define _SPBKEYMSK		(VLV_DISPLAY_BASE + 0x72298)
#define _SPBSURF		(VLV_DISPLAY_BASE + 0x7229c)
#define _SPBKEYMAXVAL		(VLV_DISPLAY_BASE + 0x722a0)
#define _SPBTILEOFF		(VLV_DISPLAY_BASE + 0x722a4)
#define _SPBCONSTALPHA		(VLV_DISPLAY_BASE + 0x722a8)
#define _SPBGAMC		(VLV_DISPLAY_BASE + 0x722f4)

#define SPCNTR(pipe, plane) _PIPE(pipe * 2 + plane, _SPACNTR, _SPBCNTR)
#define SPLINOFF(pipe, plane) _PIPE(pipe * 2 + plane, _SPALINOFF, _SPBLINOFF)
#define SPSTRIDE(pipe, plane) _PIPE(pipe * 2 + plane, _SPASTRIDE, _SPBSTRIDE)
#define SPPOS(pipe, plane) _PIPE(pipe * 2 + plane, _SPAPOS, _SPBPOS)
#define SPSIZE(pipe, plane) _PIPE(pipe * 2 + plane, _SPASIZE, _SPBSIZE)
#define SPKEYMINVAL(pipe, plane) _PIPE(pipe * 2 + plane, _SPAKEYMINVAL, _SPBKEYMINVAL)
#define SPKEYMSK(pipe, plane) _PIPE(pipe * 2 + plane, _SPAKEYMSK, _SPBKEYMSK)
#define SPSURF(pipe, plane) _PIPE(pipe * 2 + plane, _SPASURF, _SPBSURF)
#define SPKEYMAXVAL(pipe, plane) _PIPE(pipe * 2 + plane, _SPAKEYMAXVAL, _SPBKEYMAXVAL)
#define SPTILEOFF(pipe, plane) _PIPE(pipe * 2 + plane, _SPATILEOFF, _SPBTILEOFF)
#define SPCONSTALPHA(pipe, plane) _PIPE(pipe * 2 + plane, _SPACONSTALPHA, _SPBCONSTALPHA)
#define SPGAMC(pipe, plane) _PIPE(pipe * 2 + plane, _SPAGAMC, _SPBGAMC)

/*
 * CHV pipe B sprite CSC
 *
 * |cr|   |c0 c1 c2|   |cr + cr_ioff|   |cr_ooff|
 * |yg| = |c3 c4 c5| x |yg + yg_ioff| + |yg_ooff|
 * |cb|   |c6 c7 c8|   |cb + cr_ioff|   |cb_ooff|
 */
#define SPCSCYGOFF(sprite)	(VLV_DISPLAY_BASE + 0x6d900 + (sprite) * 0x1000)
#define SPCSCCBOFF(sprite)	(VLV_DISPLAY_BASE + 0x6d904 + (sprite) * 0x1000)
#define SPCSCCROFF(sprite)	(VLV_DISPLAY_BASE + 0x6d908 + (sprite) * 0x1000)
#define  SPCSC_OOFF(x)		(((x) & 0x7ff) << 16) /* s11 */
#define  SPCSC_IOFF(x)		(((x) & 0x7ff) << 0) /* s11 */

#define SPCSCC01(sprite)	(VLV_DISPLAY_BASE + 0x6d90c + (sprite) * 0x1000)
#define SPCSCC23(sprite)	(VLV_DISPLAY_BASE + 0x6d910 + (sprite) * 0x1000)
#define SPCSCC45(sprite)	(VLV_DISPLAY_BASE + 0x6d914 + (sprite) * 0x1000)
#define SPCSCC67(sprite)	(VLV_DISPLAY_BASE + 0x6d918 + (sprite) * 0x1000)
#define SPCSCC8(sprite)		(VLV_DISPLAY_BASE + 0x6d91c + (sprite) * 0x1000)
#define  SPCSC_C1(x)		(((x) & 0x7fff) << 16) /* s3.12 */
#define  SPCSC_C0(x)		(((x) & 0x7fff) << 0) /* s3.12 */

#define SPCSCYGICLAMP(sprite)	(VLV_DISPLAY_BASE + 0x6d920 + (sprite) * 0x1000)
#define SPCSCCBICLAMP(sprite)	(VLV_DISPLAY_BASE + 0x6d924 + (sprite) * 0x1000)
#define SPCSCCRICLAMP(sprite)	(VLV_DISPLAY_BASE + 0x6d928 + (sprite) * 0x1000)
#define  SPCSC_IMAX(x)		(((x) & 0x7ff) << 16) /* s11 */
#define  SPCSC_IMIN(x)		(((x) & 0x7ff) << 0) /* s11 */

#define SPCSCYGOCLAMP(sprite)	(VLV_DISPLAY_BASE + 0x6d92c + (sprite) * 0x1000)
#define SPCSCCBOCLAMP(sprite)	(VLV_DISPLAY_BASE + 0x6d930 + (sprite) * 0x1000)
#define SPCSCCROCLAMP(sprite)	(VLV_DISPLAY_BASE + 0x6d934 + (sprite) * 0x1000)
#define  SPCSC_OMAX(x)		((x) << 16) /* u10 */
#define  SPCSC_OMIN(x)		((x) << 0) /* u10 */

/* Skylake plane registers */

#define _PLANE_CTL_1_A				0x70180
#define _PLANE_CTL_2_A				0x70280
#define _PLANE_CTL_3_A				0x70380
#define   PLANE_CTL_ENABLE			(1 << 31)
#define   PLANE_CTL_PIPE_GAMMA_ENABLE		(1 << 30)
#define   PLANE_CTL_FORMAT_MASK			(0xf << 24)
#define   PLANE_CTL_FORMAT_YUV422		(  0 << 24)
#define   PLANE_CTL_FORMAT_NV12			(  1 << 24)
#define   PLANE_CTL_FORMAT_XRGB_2101010		(  2 << 24)
#define   PLANE_CTL_FORMAT_XRGB_8888		(  4 << 24)
#define   PLANE_CTL_FORMAT_XRGB_16161616F	(  6 << 24)
#define   PLANE_CTL_FORMAT_AYUV			(  8 << 24)
#define   PLANE_CTL_FORMAT_INDEXED		( 12 << 24)
#define   PLANE_CTL_FORMAT_RGB_565		( 14 << 24)
#define   PLANE_CTL_PIPE_CSC_ENABLE		(1 << 23)
#define   PLANE_CTL_KEY_ENABLE_MASK		(0x3 << 21)
#define   PLANE_CTL_KEY_ENABLE_SOURCE		(  1 << 21)
#define   PLANE_CTL_KEY_ENABLE_DESTINATION	(  2 << 21)
#define   PLANE_CTL_ORDER_BGRX			(0 << 20)
#define   PLANE_CTL_ORDER_RGBX			(1 << 20)
#define   PLANE_CTL_YUV422_ORDER_MASK		(0x3 << 16)
#define   PLANE_CTL_YUV422_YUYV			(  0 << 16)
#define   PLANE_CTL_YUV422_UYVY			(  1 << 16)
#define   PLANE_CTL_YUV422_YVYU			(  2 << 16)
#define   PLANE_CTL_YUV422_VYUY			(  3 << 16)
#define   PLANE_CTL_DECOMPRESSION_ENABLE	(1 << 15)
#define   PLANE_CTL_TRICKLE_FEED_DISABLE	(1 << 14)
#define   PLANE_CTL_PLANE_GAMMA_DISABLE		(1 << 13)
#define   PLANE_CTL_TILED_MASK			(0x7 << 10)
#define   PLANE_CTL_TILED_LINEAR		(  0 << 10)
#define   PLANE_CTL_TILED_X			(  1 << 10)
#define   PLANE_CTL_TILED_Y			(  4 << 10)
#define   PLANE_CTL_TILED_YF			(  5 << 10)
#define   PLANE_CTL_ALPHA_MASK			(0x3 << 4)
#define   PLANE_CTL_ALPHA_DISABLE		(  0 << 4)
#define   PLANE_CTL_ALPHA_SW_PREMULTIPLY	(  2 << 4)
#define   PLANE_CTL_ALPHA_HW_PREMULTIPLY	(  3 << 4)
#define   PLANE_CTL_ROTATE_MASK			0x3
#define   PLANE_CTL_ROTATE_0			0x0
#define   PLANE_CTL_ROTATE_180			0x2
#define _PLANE_STRIDE_1_A			0x70188
#define _PLANE_STRIDE_2_A			0x70288
#define _PLANE_STRIDE_3_A			0x70388
#define _PLANE_POS_1_A				0x7018c
#define _PLANE_POS_2_A				0x7028c
#define _PLANE_POS_3_A				0x7038c
#define _PLANE_SIZE_1_A				0x70190
#define _PLANE_SIZE_2_A				0x70290
#define _PLANE_SIZE_3_A				0x70390
#define _PLANE_SURF_1_A				0x7019c
#define _PLANE_SURF_2_A				0x7029c
#define _PLANE_SURF_3_A				0x7039c
#define _PLANE_OFFSET_1_A			0x701a4
#define _PLANE_OFFSET_2_A			0x702a4
#define _PLANE_OFFSET_3_A			0x703a4
#define _PLANE_KEYVAL_1_A			0x70194
#define _PLANE_KEYVAL_2_A			0x70294
#define _PLANE_KEYMSK_1_A			0x70198
#define _PLANE_KEYMSK_2_A			0x70298
#define _PLANE_KEYMAX_1_A			0x701a0
#define _PLANE_KEYMAX_2_A			0x702a0
#define _PLANE_BUF_CFG_1_A			0x7027c
#define _PLANE_BUF_CFG_2_A			0x7037c

#define _PLANE_CTL_1_B				0x71180
#define _PLANE_CTL_2_B				0x71280
#define _PLANE_CTL_3_B				0x71380
#define _PLANE_CTL_1(pipe)	_PIPE(pipe, _PLANE_CTL_1_A, _PLANE_CTL_1_B)
#define _PLANE_CTL_2(pipe)	_PIPE(pipe, _PLANE_CTL_2_A, _PLANE_CTL_2_B)
#define _PLANE_CTL_3(pipe)	_PIPE(pipe, _PLANE_CTL_3_A, _PLANE_CTL_3_B)
#define PLANE_CTL(pipe, plane)	\
	_PLANE(plane, _PLANE_CTL_1(pipe), _PLANE_CTL_2(pipe))

#define _PLANE_STRIDE_1_B			0x71188
#define _PLANE_STRIDE_2_B			0x71288
#define _PLANE_STRIDE_3_B			0x71388
#define _PLANE_STRIDE_1(pipe)	\
	_PIPE(pipe, _PLANE_STRIDE_1_A, _PLANE_STRIDE_1_B)
#define _PLANE_STRIDE_2(pipe)	\
	_PIPE(pipe, _PLANE_STRIDE_2_A, _PLANE_STRIDE_2_B)
#define _PLANE_STRIDE_3(pipe)	\
	_PIPE(pipe, _PLANE_STRIDE_3_A, _PLANE_STRIDE_3_B)
#define PLANE_STRIDE(pipe, plane)	\
	_PLANE(plane, _PLANE_STRIDE_1(pipe), _PLANE_STRIDE_2(pipe))

#define _PLANE_POS_1_B				0x7118c
#define _PLANE_POS_2_B				0x7128c
#define _PLANE_POS_3_B				0x7138c
#define _PLANE_POS_1(pipe)	_PIPE(pipe, _PLANE_POS_1_A, _PLANE_POS_1_B)
#define _PLANE_POS_2(pipe)	_PIPE(pipe, _PLANE_POS_2_A, _PLANE_POS_2_B)
#define _PLANE_POS_3(pipe)	_PIPE(pipe, _PLANE_POS_3_A, _PLANE_POS_3_B)
#define PLANE_POS(pipe, plane)	\
	_PLANE(plane, _PLANE_POS_1(pipe), _PLANE_POS_2(pipe))

#define _PLANE_SIZE_1_B				0x71190
#define _PLANE_SIZE_2_B				0x71290
#define _PLANE_SIZE_3_B				0x71390
#define _PLANE_SIZE_1(pipe)	_PIPE(pipe, _PLANE_SIZE_1_A, _PLANE_SIZE_1_B)
#define _PLANE_SIZE_2(pipe)	_PIPE(pipe, _PLANE_SIZE_2_A, _PLANE_SIZE_2_B)
#define _PLANE_SIZE_3(pipe)	_PIPE(pipe, _PLANE_SIZE_3_A, _PLANE_SIZE_3_B)
#define PLANE_SIZE(pipe, plane)	\
	_PLANE(plane, _PLANE_SIZE_1(pipe), _PLANE_SIZE_2(pipe))

#define _PLANE_SURF_1_B				0x7119c
#define _PLANE_SURF_2_B				0x7129c
#define _PLANE_SURF_3_B				0x7139c
#define _PLANE_SURF_1(pipe)	_PIPE(pipe, _PLANE_SURF_1_A, _PLANE_SURF_1_B)
#define _PLANE_SURF_2(pipe)	_PIPE(pipe, _PLANE_SURF_2_A, _PLANE_SURF_2_B)
#define _PLANE_SURF_3(pipe)	_PIPE(pipe, _PLANE_SURF_3_A, _PLANE_SURF_3_B)
#define PLANE_SURF(pipe, plane)	\
	_PLANE(plane, _PLANE_SURF_1(pipe), _PLANE_SURF_2(pipe))

#define _PLANE_OFFSET_1_B			0x711a4
#define _PLANE_OFFSET_2_B			0x712a4
#define _PLANE_OFFSET_1(pipe) _PIPE(pipe, _PLANE_OFFSET_1_A, _PLANE_OFFSET_1_B)
#define _PLANE_OFFSET_2(pipe) _PIPE(pipe, _PLANE_OFFSET_2_A, _PLANE_OFFSET_2_B)
#define PLANE_OFFSET(pipe, plane)	\
	_PLANE(plane, _PLANE_OFFSET_1(pipe), _PLANE_OFFSET_2(pipe))

#define _PLANE_KEYVAL_1_B			0x71194
#define _PLANE_KEYVAL_2_B			0x71294
#define _PLANE_KEYVAL_1(pipe) _PIPE(pipe, _PLANE_KEYVAL_1_A, _PLANE_KEYVAL_1_B)
#define _PLANE_KEYVAL_2(pipe) _PIPE(pipe, _PLANE_KEYVAL_2_A, _PLANE_KEYVAL_2_B)
#define PLANE_KEYVAL(pipe, plane)	\
	_PLANE(plane, _PLANE_KEYVAL_1(pipe), _PLANE_KEYVAL_2(pipe))

#define _PLANE_KEYMSK_1_B			0x71198
#define _PLANE_KEYMSK_2_B			0x71298
#define _PLANE_KEYMSK_1(pipe) _PIPE(pipe, _PLANE_KEYMSK_1_A, _PLANE_KEYMSK_1_B)
#define _PLANE_KEYMSK_2(pipe) _PIPE(pipe, _PLANE_KEYMSK_2_A, _PLANE_KEYMSK_2_B)
#define PLANE_KEYMSK(pipe, plane)	\
	_PLANE(plane, _PLANE_KEYMSK_1(pipe), _PLANE_KEYMSK_2(pipe))

#define _PLANE_KEYMAX_1_B			0x711a0
#define _PLANE_KEYMAX_2_B			0x712a0
#define _PLANE_KEYMAX_1(pipe) _PIPE(pipe, _PLANE_KEYMAX_1_A, _PLANE_KEYMAX_1_B)
#define _PLANE_KEYMAX_2(pipe) _PIPE(pipe, _PLANE_KEYMAX_2_A, _PLANE_KEYMAX_2_B)
#define PLANE_KEYMAX(pipe, plane)	\
	_PLANE(plane, _PLANE_KEYMAX_1(pipe), _PLANE_KEYMAX_2(pipe))

#define _PLANE_BUF_CFG_1_B			0x7127c
#define _PLANE_BUF_CFG_2_B			0x7137c
#define _PLANE_BUF_CFG_1(pipe)	\
	_PIPE(pipe, _PLANE_BUF_CFG_1_A, _PLANE_BUF_CFG_1_B)
#define _PLANE_BUF_CFG_2(pipe)	\
	_PIPE(pipe, _PLANE_BUF_CFG_2_A, _PLANE_BUF_CFG_2_B)
#define PLANE_BUF_CFG(pipe, plane)	\
	_PLANE(plane, _PLANE_BUF_CFG_1(pipe), _PLANE_BUF_CFG_2(pipe))

/* SKL new cursor registers */
#define _CUR_BUF_CFG_A				0x7017c
#define _CUR_BUF_CFG_B				0x7117c
#define CUR_BUF_CFG(pipe)	_PIPE(pipe, _CUR_BUF_CFG_A, _CUR_BUF_CFG_B)

/* VBIOS regs */
#define VGACNTRL		0x71400
# define VGA_DISP_DISABLE			(1 << 31)
# define VGA_2X_MODE				(1 << 30)
# define VGA_PIPE_B_SELECT			(1 << 29)

#define VLV_VGACNTRL		(VLV_DISPLAY_BASE + 0x71400)

/* Ironlake */

#define CPU_VGACNTRL	0x41000

#define DIGITAL_PORT_HOTPLUG_CNTRL      0x44030
#define  DIGITAL_PORTA_HOTPLUG_ENABLE           (1 << 4)
#define  DIGITAL_PORTA_SHORT_PULSE_2MS          (0 << 2)
#define  DIGITAL_PORTA_SHORT_PULSE_4_5MS        (1 << 2)
#define  DIGITAL_PORTA_SHORT_PULSE_6MS          (2 << 2)
#define  DIGITAL_PORTA_SHORT_PULSE_100MS        (3 << 2)
#define  DIGITAL_PORTA_NO_DETECT                (0 << 0)
#define  DIGITAL_PORTA_LONG_PULSE_DETECT_MASK   (1 << 1)
#define  DIGITAL_PORTA_SHORT_PULSE_DETECT_MASK  (1 << 0)

/* refresh rate hardware control */
#define RR_HW_CTL       0x45300
#define  RR_HW_LOW_POWER_FRAMES_MASK    0xff
#define  RR_HW_HIGH_POWER_FRAMES_MASK   0xff00

#define FDI_PLL_BIOS_0  0x46000
#define  FDI_PLL_FB_CLOCK_MASK  0xff
#define FDI_PLL_BIOS_1  0x46004
#define FDI_PLL_BIOS_2  0x46008
#define DISPLAY_PORT_PLL_BIOS_0         0x4600c
#define DISPLAY_PORT_PLL_BIOS_1         0x46010
#define DISPLAY_PORT_PLL_BIOS_2         0x46014

#define PCH_3DCGDIS0		0x46020
# define MARIUNIT_CLOCK_GATE_DISABLE		(1 << 18)
# define SVSMUNIT_CLOCK_GATE_DISABLE		(1 << 1)

#define PCH_3DCGDIS1		0x46024
# define VFMUNIT_CLOCK_GATE_DISABLE		(1 << 11)

#define FDI_PLL_FREQ_CTL        0x46030
#define  FDI_PLL_FREQ_CHANGE_REQUEST    (1<<24)
#define  FDI_PLL_FREQ_LOCK_LIMIT_MASK   0xfff00
#define  FDI_PLL_FREQ_DISABLE_COUNT_LIMIT_MASK  0xff


#define _PIPEA_DATA_M1		0x60030
#define  PIPE_DATA_M1_OFFSET    0
#define _PIPEA_DATA_N1		0x60034
#define  PIPE_DATA_N1_OFFSET    0

#define _PIPEA_DATA_M2		0x60038
#define  PIPE_DATA_M2_OFFSET    0
#define _PIPEA_DATA_N2		0x6003c
#define  PIPE_DATA_N2_OFFSET    0

#define _PIPEA_LINK_M1		0x60040
#define  PIPE_LINK_M1_OFFSET    0
#define _PIPEA_LINK_N1		0x60044
#define  PIPE_LINK_N1_OFFSET    0

#define _PIPEA_LINK_M2		0x60048
#define  PIPE_LINK_M2_OFFSET    0
#define _PIPEA_LINK_N2		0x6004c
#define  PIPE_LINK_N2_OFFSET    0

/* PIPEB timing regs are same start from 0x61000 */

#define _PIPEB_DATA_M1		0x61030
#define _PIPEB_DATA_N1		0x61034
#define _PIPEB_DATA_M2		0x61038
#define _PIPEB_DATA_N2		0x6103c
#define _PIPEB_LINK_M1		0x61040
#define _PIPEB_LINK_N1		0x61044
#define _PIPEB_LINK_M2		0x61048
#define _PIPEB_LINK_N2		0x6104c

#define PIPE_DATA_M1(tran) _TRANSCODER2(tran, _PIPEA_DATA_M1)
#define PIPE_DATA_N1(tran) _TRANSCODER2(tran, _PIPEA_DATA_N1)
#define PIPE_DATA_M2(tran) _TRANSCODER2(tran, _PIPEA_DATA_M2)
#define PIPE_DATA_N2(tran) _TRANSCODER2(tran, _PIPEA_DATA_N2)
#define PIPE_LINK_M1(tran) _TRANSCODER2(tran, _PIPEA_LINK_M1)
#define PIPE_LINK_N1(tran) _TRANSCODER2(tran, _PIPEA_LINK_N1)
#define PIPE_LINK_M2(tran) _TRANSCODER2(tran, _PIPEA_LINK_M2)
#define PIPE_LINK_N2(tran) _TRANSCODER2(tran, _PIPEA_LINK_N2)

/* CPU panel fitter */
/* IVB+ has 3 fitters, 0 is 7x5 capable, the other two only 3x3 */
#define _PFA_CTL_1               0x68080
#define _PFB_CTL_1               0x68880
#define  PF_ENABLE              (1<<31)
#define  PF_PIPE_SEL_MASK_IVB	(3<<29)
#define  PF_PIPE_SEL_IVB(pipe)	((pipe)<<29)
#define  PF_FILTER_MASK		(3<<23)
#define  PF_FILTER_PROGRAMMED	(0<<23)
#define  PF_FILTER_MED_3x3	(1<<23)
#define  PF_FILTER_EDGE_ENHANCE	(2<<23)
#define  PF_FILTER_EDGE_SOFTEN	(3<<23)
#define _PFA_WIN_SZ		0x68074
#define _PFB_WIN_SZ		0x68874
#define _PFA_WIN_POS		0x68070
#define _PFB_WIN_POS		0x68870
#define _PFA_VSCALE		0x68084
#define _PFB_VSCALE		0x68884
#define _PFA_HSCALE		0x68090
#define _PFB_HSCALE		0x68890

#define PF_CTL(pipe)		_PIPE(pipe, _PFA_CTL_1, _PFB_CTL_1)
#define PF_WIN_SZ(pipe)		_PIPE(pipe, _PFA_WIN_SZ, _PFB_WIN_SZ)
#define PF_WIN_POS(pipe)	_PIPE(pipe, _PFA_WIN_POS, _PFB_WIN_POS)
#define PF_VSCALE(pipe)		_PIPE(pipe, _PFA_VSCALE, _PFB_VSCALE)
#define PF_HSCALE(pipe)		_PIPE(pipe, _PFA_HSCALE, _PFB_HSCALE)

#define _PSA_CTL		0x68180
#define _PSB_CTL		0x68980
#define PS_ENABLE		(1<<31)
#define _PSA_WIN_SZ		0x68174
#define _PSB_WIN_SZ		0x68974
#define _PSA_WIN_POS		0x68170
#define _PSB_WIN_POS		0x68970

#define PS_CTL(pipe)		_PIPE(pipe, _PSA_CTL, _PSB_CTL)
#define PS_WIN_SZ(pipe)		_PIPE(pipe, _PSA_WIN_SZ, _PSB_WIN_SZ)
#define PS_WIN_POS(pipe)	_PIPE(pipe, _PSA_WIN_POS, _PSB_WIN_POS)

/* legacy palette */
#define _LGC_PALETTE_A           0x4a000
#define _LGC_PALETTE_B           0x4a800
#define LGC_PALETTE(pipe) _PIPE(pipe, _LGC_PALETTE_A, _LGC_PALETTE_B)

#define _GAMMA_MODE_A		0x4a480
#define _GAMMA_MODE_B		0x4ac80
#define GAMMA_MODE(pipe) _PIPE(pipe, _GAMMA_MODE_A, _GAMMA_MODE_B)
#define GAMMA_MODE_MODE_MASK	(3 << 0)
#define GAMMA_MODE_MODE_8BIT	(0 << 0)
#define GAMMA_MODE_MODE_10BIT	(1 << 0)
#define GAMMA_MODE_MODE_12BIT	(2 << 0)
#define GAMMA_MODE_MODE_SPLIT	(3 << 0)

/* interrupts */
#define DE_MASTER_IRQ_CONTROL   (1 << 31)
#define DE_SPRITEB_FLIP_DONE    (1 << 29)
#define DE_SPRITEA_FLIP_DONE    (1 << 28)
#define DE_PLANEB_FLIP_DONE     (1 << 27)
#define DE_PLANEA_FLIP_DONE     (1 << 26)
#define DE_PLANE_FLIP_DONE(plane) (1 << (26 + (plane)))
#define DE_PCU_EVENT            (1 << 25)
#define DE_GTT_FAULT            (1 << 24)
#define DE_POISON               (1 << 23)
#define DE_PERFORM_COUNTER      (1 << 22)
#define DE_PCH_EVENT            (1 << 21)
#define DE_AUX_CHANNEL_A        (1 << 20)
#define DE_DP_A_HOTPLUG         (1 << 19)
#define DE_GSE                  (1 << 18)
#define DE_PIPEB_VBLANK         (1 << 15)
#define DE_PIPEB_EVEN_FIELD     (1 << 14)
#define DE_PIPEB_ODD_FIELD      (1 << 13)
#define DE_PIPEB_LINE_COMPARE   (1 << 12)
#define DE_PIPEB_VSYNC          (1 << 11)
#define DE_PIPEB_CRC_DONE	(1 << 10)
#define DE_PIPEB_FIFO_UNDERRUN  (1 << 8)
#define DE_PIPEA_VBLANK         (1 << 7)
#define DE_PIPE_VBLANK(pipe)    (1 << (7 + 8*(pipe)))
#define DE_PIPEA_EVEN_FIELD     (1 << 6)
#define DE_PIPEA_ODD_FIELD      (1 << 5)
#define DE_PIPEA_LINE_COMPARE   (1 << 4)
#define DE_PIPEA_VSYNC          (1 << 3)
#define DE_PIPEA_CRC_DONE	(1 << 2)
#define DE_PIPE_CRC_DONE(pipe)	(1 << (2 + 8*(pipe)))
#define DE_PIPEA_FIFO_UNDERRUN  (1 << 0)
#define DE_PIPE_FIFO_UNDERRUN(pipe)  (1 << (8*(pipe)))

/* More Ivybridge lolz */
#define DE_ERR_INT_IVB			(1<<30)
#define DE_GSE_IVB			(1<<29)
#define DE_PCH_EVENT_IVB		(1<<28)
#define DE_DP_A_HOTPLUG_IVB		(1<<27)
#define DE_AUX_CHANNEL_A_IVB		(1<<26)
#define DE_SPRITEC_FLIP_DONE_IVB	(1<<14)
#define DE_PLANEC_FLIP_DONE_IVB		(1<<13)
#define DE_PIPEC_VBLANK_IVB		(1<<10)
#define DE_SPRITEB_FLIP_DONE_IVB	(1<<9)
#define DE_PLANEB_FLIP_DONE_IVB		(1<<8)
#define DE_PIPEB_VBLANK_IVB		(1<<5)
#define DE_SPRITEA_FLIP_DONE_IVB	(1<<4)
#define DE_PLANEA_FLIP_DONE_IVB		(1<<3)
#define DE_PLANE_FLIP_DONE_IVB(plane)	(1<< (3 + 5*(plane)))
#define DE_PIPEA_VBLANK_IVB		(1<<0)
#define DE_PIPE_VBLANK_IVB(pipe)	(1 << (pipe * 5))

#define VLV_MASTER_IER			0x4400c /* Gunit master IER */
#define   MASTER_INTERRUPT_ENABLE	(1<<31)

#define DEISR   0x44000
#define DEIMR   0x44004
#define DEIIR   0x44008
#define DEIER   0x4400c

#define GTISR   0x44010
#define GTIMR   0x44014
#define GTIIR   0x44018
#define GTIER   0x4401c

#define GEN8_MASTER_IRQ			0x44200
#define  GEN8_MASTER_IRQ_CONTROL	(1<<31)
#define  GEN8_PCU_IRQ			(1<<30)
#define  GEN8_DE_PCH_IRQ		(1<<23)
#define  GEN8_DE_MISC_IRQ		(1<<22)
#define  GEN8_DE_PORT_IRQ		(1<<20)
#define  GEN8_DE_PIPE_C_IRQ		(1<<18)
#define  GEN8_DE_PIPE_B_IRQ		(1<<17)
#define  GEN8_DE_PIPE_A_IRQ		(1<<16)
#define  GEN8_DE_PIPE_IRQ(pipe)		(1<<(16+pipe))
#define  GEN8_GT_VECS_IRQ		(1<<6)
#define  GEN8_GT_PM_IRQ			(1<<4)
#define  GEN8_GT_VCS2_IRQ		(1<<3)
#define  GEN8_GT_VCS1_IRQ		(1<<2)
#define  GEN8_GT_BCS_IRQ		(1<<1)
#define  GEN8_GT_RCS_IRQ		(1<<0)

#define GEN8_GT_ISR(which) (0x44300 + (0x10 * (which)))
#define GEN8_GT_IMR(which) (0x44304 + (0x10 * (which)))
#define GEN8_GT_IIR(which) (0x44308 + (0x10 * (which)))
#define GEN8_GT_IER(which) (0x4430c + (0x10 * (which)))

#define GEN8_BCS_IRQ_SHIFT 16
#define GEN8_RCS_IRQ_SHIFT 0
#define GEN8_VCS2_IRQ_SHIFT 16
#define GEN8_VCS1_IRQ_SHIFT 0
#define GEN8_VECS_IRQ_SHIFT 0

#define GEN8_DE_PIPE_ISR(pipe) (0x44400 + (0x10 * (pipe)))
#define GEN8_DE_PIPE_IMR(pipe) (0x44404 + (0x10 * (pipe)))
#define GEN8_DE_PIPE_IIR(pipe) (0x44408 + (0x10 * (pipe)))
#define GEN8_DE_PIPE_IER(pipe) (0x4440c + (0x10 * (pipe)))
#define  GEN8_PIPE_FIFO_UNDERRUN	(1 << 31)
#define  GEN8_PIPE_CDCLK_CRC_ERROR	(1 << 29)
#define  GEN8_PIPE_CDCLK_CRC_DONE	(1 << 28)
#define  GEN8_PIPE_CURSOR_FAULT		(1 << 10)
#define  GEN8_PIPE_SPRITE_FAULT		(1 << 9)
#define  GEN8_PIPE_PRIMARY_FAULT	(1 << 8)
#define  GEN8_PIPE_SPRITE_FLIP_DONE	(1 << 5)
#define  GEN8_PIPE_PRIMARY_FLIP_DONE	(1 << 4)
#define  GEN8_PIPE_SCAN_LINE_EVENT	(1 << 2)
#define  GEN8_PIPE_VSYNC		(1 << 1)
#define  GEN8_PIPE_VBLANK		(1 << 0)
#define  GEN9_PIPE_CURSOR_FAULT		(1 << 11)
#define  GEN9_PIPE_PLANE3_FAULT		(1 << 9)
#define  GEN9_PIPE_PLANE2_FAULT		(1 << 8)
#define  GEN9_PIPE_PLANE1_FAULT		(1 << 7)
#define  GEN9_PIPE_PLANE3_FLIP_DONE	(1 << 5)
#define  GEN9_PIPE_PLANE2_FLIP_DONE	(1 << 4)
#define  GEN9_PIPE_PLANE1_FLIP_DONE	(1 << 3)
#define  GEN9_PIPE_PLANE_FLIP_DONE(p)	(1 << (3 + p))
#define GEN8_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN8_PIPE_CURSOR_FAULT | \
	 GEN8_PIPE_SPRITE_FAULT | \
	 GEN8_PIPE_PRIMARY_FAULT)
#define GEN9_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN9_PIPE_CURSOR_FAULT | \
	 GEN9_PIPE_PLANE3_FAULT | \
	 GEN9_PIPE_PLANE2_FAULT | \
	 GEN9_PIPE_PLANE1_FAULT)

#define GEN8_DE_PORT_ISR 0x44440
#define GEN8_DE_PORT_IMR 0x44444
#define GEN8_DE_PORT_IIR 0x44448
#define GEN8_DE_PORT_IER 0x4444c
#define  GEN8_PORT_DP_A_HOTPLUG		(1 << 3)
#define  GEN9_AUX_CHANNEL_D		(1 << 27)
#define  GEN9_AUX_CHANNEL_C		(1 << 26)
#define  GEN9_AUX_CHANNEL_B		(1 << 25)
#define  GEN8_AUX_CHANNEL_A		(1 << 0)

#define GEN8_DE_MISC_ISR 0x44460
#define GEN8_DE_MISC_IMR 0x44464
#define GEN8_DE_MISC_IIR 0x44468
#define GEN8_DE_MISC_IER 0x4446c
#define  GEN8_DE_MISC_GSE		(1 << 27)

#define GEN8_PCU_ISR 0x444e0
#define GEN8_PCU_IMR 0x444e4
#define GEN8_PCU_IIR 0x444e8
#define GEN8_PCU_IER 0x444ec

#define ILK_DISPLAY_CHICKEN2	0x42004
/* Required on all Ironlake and Sandybridge according to the B-Spec. */
#define  ILK_ELPIN_409_SELECT	(1 << 25)
#define  ILK_DPARB_GATE	(1<<22)
#define  ILK_VSDPFD_FULL	(1<<21)
#define FUSE_STRAP			0x42014
#define  ILK_INTERNAL_GRAPHICS_DISABLE	(1 << 31)
#define  ILK_INTERNAL_DISPLAY_DISABLE	(1 << 30)
#define  ILK_DISPLAY_DEBUG_DISABLE	(1 << 29)
#define  ILK_HDCP_DISABLE		(1 << 25)
#define  ILK_eDP_A_DISABLE		(1 << 24)
#define  HSW_CDCLK_LIMIT		(1 << 24)
#define  ILK_DESKTOP			(1 << 23)

#define ILK_DSPCLK_GATE_D			0x42020
#define   ILK_VRHUNIT_CLOCK_GATE_DISABLE	(1 << 28)
#define   ILK_DPFCUNIT_CLOCK_GATE_DISABLE	(1 << 9)
#define   ILK_DPFCRUNIT_CLOCK_GATE_DISABLE	(1 << 8)
#define   ILK_DPFDUNIT_CLOCK_GATE_ENABLE	(1 << 7)
#define   ILK_DPARBUNIT_CLOCK_GATE_ENABLE	(1 << 5)

#define IVB_CHICKEN3	0x4200c
# define CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE	(1 << 5)
# define CHICKEN3_DGMG_DONE_FIX_DISABLE		(1 << 2)

#define CHICKEN_PAR1_1		0x42080
#define  DPA_MASK_VBLANK_SRD	(1 << 15)
#define  FORCE_ARB_IDLE_PLANES	(1 << 14)

#define _CHICKEN_PIPESL_1_A	0x420b0
#define _CHICKEN_PIPESL_1_B	0x420b4
#define  HSW_FBCQ_DIS			(1 << 22)
#define  BDW_DPRS_MASK_VBLANK_SRD	(1 << 0)
#define CHICKEN_PIPESL_1(pipe) _PIPE(pipe, _CHICKEN_PIPESL_1_A, _CHICKEN_PIPESL_1_B)

#define DISP_ARB_CTL	0x45000
#define  DISP_TILE_SURFACE_SWIZZLING	(1<<13)
#define  DISP_FBC_WM_DIS		(1<<15)
#define DISP_ARB_CTL2	0x45004
#define  DISP_DATA_PARTITION_5_6	(1<<6)
#define GEN7_MSG_CTL	0x45010
#define  WAIT_FOR_PCH_RESET_ACK		(1<<1)
#define  WAIT_FOR_PCH_FLR_ACK		(1<<0)
#define HSW_NDE_RSTWRN_OPT	0x46408
#define  RESET_PCH_HANDSHAKE_ENABLE	(1<<4)

/* GEN7 chicken */
#define GEN7_COMMON_SLICE_CHICKEN1		0x7010
# define GEN7_CSC1_RHWO_OPT_DISABLE_IN_RCC	((1<<10) | (1<<26))
#define COMMON_SLICE_CHICKEN2			0x7014
# define GEN8_CSC2_SBE_VUE_CACHE_CONSERVATIVE	(1<<0)

#define GEN7_L3SQCREG1				0xB010
#define  VLV_B0_WA_L3SQCREG1_VALUE		0x00D30000

#define GEN7_L3CNTLREG1				0xB01C
#define  GEN7_WA_FOR_GEN7_L3_CONTROL			0x3C47FF8C
#define  GEN7_L3AGDIS				(1<<19)
#define GEN7_L3CNTLREG2				0xB020
#define GEN7_L3CNTLREG3				0xB024

#define GEN7_L3_CHICKEN_MODE_REGISTER		0xB030
#define  GEN7_WA_L3_CHICKEN_MODE				0x20000000

#define GEN7_L3SQCREG4				0xb034
#define  L3SQ_URB_READ_CAM_MATCH_DISABLE	(1<<27)

/* GEN8 chicken */
#define HDC_CHICKEN0				0x7300
#define  HDC_FORCE_NON_COHERENT			(1<<4)
#define  HDC_DONOT_FETCH_MEM_WHEN_MASKED	(1<<11)
#define  HDC_FENCE_DEST_SLM_DISABLE		(1<<14)

/* WaCatErrorRejectionIssue */
#define GEN7_SQ_CHICKEN_MBCUNIT_CONFIG		0x9030
#define  GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB	(1<<11)

#define HSW_SCRATCH1				0xb038
#define  HSW_SCRATCH1_L3_DATA_ATOMICS_DISABLE	(1<<27)

/* PCH */

/* south display engine interrupt: IBX */
#define SDE_AUDIO_POWER_D	(1 << 27)
#define SDE_AUDIO_POWER_C	(1 << 26)
#define SDE_AUDIO_POWER_B	(1 << 25)
#define SDE_AUDIO_POWER_SHIFT	(25)
#define SDE_AUDIO_POWER_MASK	(7 << SDE_AUDIO_POWER_SHIFT)
#define SDE_GMBUS		(1 << 24)
#define SDE_AUDIO_HDCP_TRANSB	(1 << 23)
#define SDE_AUDIO_HDCP_TRANSA	(1 << 22)
#define SDE_AUDIO_HDCP_MASK	(3 << 22)
#define SDE_AUDIO_TRANSB	(1 << 21)
#define SDE_AUDIO_TRANSA	(1 << 20)
#define SDE_AUDIO_TRANS_MASK	(3 << 20)
#define SDE_POISON		(1 << 19)
/* 18 reserved */
#define SDE_FDI_RXB		(1 << 17)
#define SDE_FDI_RXA		(1 << 16)
#define SDE_FDI_MASK		(3 << 16)
#define SDE_AUXD		(1 << 15)
#define SDE_AUXC		(1 << 14)
#define SDE_AUXB		(1 << 13)
#define SDE_AUX_MASK		(7 << 13)
/* 12 reserved */
#define SDE_CRT_HOTPLUG         (1 << 11)
#define SDE_PORTD_HOTPLUG       (1 << 10)
#define SDE_PORTC_HOTPLUG       (1 << 9)
#define SDE_PORTB_HOTPLUG       (1 << 8)
#define SDE_SDVOB_HOTPLUG       (1 << 6)
#define SDE_HOTPLUG_MASK        (SDE_CRT_HOTPLUG | \
				 SDE_SDVOB_HOTPLUG |	\
				 SDE_PORTB_HOTPLUG |	\
				 SDE_PORTC_HOTPLUG |	\
				 SDE_PORTD_HOTPLUG)
#define SDE_TRANSB_CRC_DONE	(1 << 5)
#define SDE_TRANSB_CRC_ERR	(1 << 4)
#define SDE_TRANSB_FIFO_UNDER	(1 << 3)
#define SDE_TRANSA_CRC_DONE	(1 << 2)
#define SDE_TRANSA_CRC_ERR	(1 << 1)
#define SDE_TRANSA_FIFO_UNDER	(1 << 0)
#define SDE_TRANS_MASK		(0x3f)

/* south display engine interrupt: CPT/PPT */
#define SDE_AUDIO_POWER_D_CPT	(1 << 31)
#define SDE_AUDIO_POWER_C_CPT	(1 << 30)
#define SDE_AUDIO_POWER_B_CPT	(1 << 29)
#define SDE_AUDIO_POWER_SHIFT_CPT   29
#define SDE_AUDIO_POWER_MASK_CPT    (7 << 29)
#define SDE_AUXD_CPT		(1 << 27)
#define SDE_AUXC_CPT		(1 << 26)
#define SDE_AUXB_CPT		(1 << 25)
#define SDE_AUX_MASK_CPT	(7 << 25)
#define SDE_PORTD_HOTPLUG_CPT	(1 << 23)
#define SDE_PORTC_HOTPLUG_CPT	(1 << 22)
#define SDE_PORTB_HOTPLUG_CPT	(1 << 21)
#define SDE_CRT_HOTPLUG_CPT	(1 << 19)
#define SDE_SDVOB_HOTPLUG_CPT	(1 << 18)
#define SDE_HOTPLUG_MASK_CPT	(SDE_CRT_HOTPLUG_CPT |		\
				 SDE_SDVOB_HOTPLUG_CPT |	\
				 SDE_PORTD_HOTPLUG_CPT |	\
				 SDE_PORTC_HOTPLUG_CPT |	\
				 SDE_PORTB_HOTPLUG_CPT)
#define SDE_GMBUS_CPT		(1 << 17)
#define SDE_ERROR_CPT		(1 << 16)
#define SDE_AUDIO_CP_REQ_C_CPT	(1 << 10)
#define SDE_AUDIO_CP_CHG_C_CPT	(1 << 9)
#define SDE_FDI_RXC_CPT		(1 << 8)
#define SDE_AUDIO_CP_REQ_B_CPT	(1 << 6)
#define SDE_AUDIO_CP_CHG_B_CPT	(1 << 5)
#define SDE_FDI_RXB_CPT		(1 << 4)
#define SDE_AUDIO_CP_REQ_A_CPT	(1 << 2)
#define SDE_AUDIO_CP_CHG_A_CPT	(1 << 1)
#define SDE_FDI_RXA_CPT		(1 << 0)
#define SDE_AUDIO_CP_REQ_CPT	(SDE_AUDIO_CP_REQ_C_CPT | \
				 SDE_AUDIO_CP_REQ_B_CPT | \
				 SDE_AUDIO_CP_REQ_A_CPT)
#define SDE_AUDIO_CP_CHG_CPT	(SDE_AUDIO_CP_CHG_C_CPT | \
				 SDE_AUDIO_CP_CHG_B_CPT | \
				 SDE_AUDIO_CP_CHG_A_CPT)
#define SDE_FDI_MASK_CPT	(SDE_FDI_RXC_CPT | \
				 SDE_FDI_RXB_CPT | \
				 SDE_FDI_RXA_CPT)

#define SDEISR  0xc4000
#define SDEIMR  0xc4004
#define SDEIIR  0xc4008
#define SDEIER  0xc400c

#define SERR_INT			0xc4040
#define  SERR_INT_POISON		(1<<31)
#define  SERR_INT_TRANS_C_FIFO_UNDERRUN	(1<<6)
#define  SERR_INT_TRANS_B_FIFO_UNDERRUN	(1<<3)
#define  SERR_INT_TRANS_A_FIFO_UNDERRUN	(1<<0)
#define  SERR_INT_TRANS_FIFO_UNDERRUN(pipe)	(1<<(pipe*3))

/* digital port hotplug */
#define PCH_PORT_HOTPLUG        0xc4030		/* SHOTPLUG_CTL */
#define PORTD_HOTPLUG_ENABLE            (1 << 20)
#define PORTD_PULSE_DURATION_2ms        (0)
#define PORTD_PULSE_DURATION_4_5ms      (1 << 18)
#define PORTD_PULSE_DURATION_6ms        (2 << 18)
#define PORTD_PULSE_DURATION_100ms      (3 << 18)
#define PORTD_PULSE_DURATION_MASK	(3 << 18)
#define PORTD_HOTPLUG_STATUS_MASK	(0x3 << 16)
#define  PORTD_HOTPLUG_NO_DETECT	(0 << 16)
#define  PORTD_HOTPLUG_SHORT_DETECT	(1 << 16)
#define  PORTD_HOTPLUG_LONG_DETECT	(2 << 16)
#define PORTC_HOTPLUG_ENABLE            (1 << 12)
#define PORTC_PULSE_DURATION_2ms        (0)
#define PORTC_PULSE_DURATION_4_5ms      (1 << 10)
#define PORTC_PULSE_DURATION_6ms        (2 << 10)
#define PORTC_PULSE_DURATION_100ms      (3 << 10)
#define PORTC_PULSE_DURATION_MASK	(3 << 10)
#define PORTC_HOTPLUG_STATUS_MASK	(0x3 << 8)
#define  PORTC_HOTPLUG_NO_DETECT	(0 << 8)
#define  PORTC_HOTPLUG_SHORT_DETECT	(1 << 8)
#define  PORTC_HOTPLUG_LONG_DETECT	(2 << 8)
#define PORTB_HOTPLUG_ENABLE            (1 << 4)
#define PORTB_PULSE_DURATION_2ms        (0)
#define PORTB_PULSE_DURATION_4_5ms      (1 << 2)
#define PORTB_PULSE_DURATION_6ms        (2 << 2)
#define PORTB_PULSE_DURATION_100ms      (3 << 2)
#define PORTB_PULSE_DURATION_MASK	(3 << 2)
#define PORTB_HOTPLUG_STATUS_MASK	(0x3 << 0)
#define  PORTB_HOTPLUG_NO_DETECT	(0 << 0)
#define  PORTB_HOTPLUG_SHORT_DETECT	(1 << 0)
#define  PORTB_HOTPLUG_LONG_DETECT	(2 << 0)

#define PCH_GPIOA               0xc5010
#define PCH_GPIOB               0xc5014
#define PCH_GPIOC               0xc5018
#define PCH_GPIOD               0xc501c
#define PCH_GPIOE               0xc5020
#define PCH_GPIOF               0xc5024

#define PCH_GMBUS0		0xc5100
#define PCH_GMBUS1		0xc5104
#define PCH_GMBUS2		0xc5108
#define PCH_GMBUS3		0xc510c
#define PCH_GMBUS4		0xc5110
#define PCH_GMBUS5		0xc5120

#define _PCH_DPLL_A              0xc6014
#define _PCH_DPLL_B              0xc6018
#define PCH_DPLL(pll) (pll == 0 ? _PCH_DPLL_A : _PCH_DPLL_B)

#define _PCH_FPA0                0xc6040
#define  FP_CB_TUNE		(0x3<<22)
#define _PCH_FPA1                0xc6044
#define _PCH_FPB0                0xc6048
#define _PCH_FPB1                0xc604c
#define PCH_FP0(pll) (pll == 0 ? _PCH_FPA0 : _PCH_FPB0)
#define PCH_FP1(pll) (pll == 0 ? _PCH_FPA1 : _PCH_FPB1)

#define PCH_DPLL_TEST           0xc606c

#define PCH_DREF_CONTROL        0xC6200
#define  DREF_CONTROL_MASK      0x7fc3
#define  DREF_CPU_SOURCE_OUTPUT_DISABLE         (0<<13)
#define  DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD      (2<<13)
#define  DREF_CPU_SOURCE_OUTPUT_NONSPREAD       (3<<13)
#define  DREF_CPU_SOURCE_OUTPUT_MASK		(3<<13)
#define  DREF_SSC_SOURCE_DISABLE                (0<<11)
#define  DREF_SSC_SOURCE_ENABLE                 (2<<11)
#define  DREF_SSC_SOURCE_MASK			(3<<11)
#define  DREF_NONSPREAD_SOURCE_DISABLE          (0<<9)
#define  DREF_NONSPREAD_CK505_ENABLE		(1<<9)
#define  DREF_NONSPREAD_SOURCE_ENABLE           (2<<9)
#define  DREF_NONSPREAD_SOURCE_MASK		(3<<9)
#define  DREF_SUPERSPREAD_SOURCE_DISABLE        (0<<7)
#define  DREF_SUPERSPREAD_SOURCE_ENABLE         (2<<7)
#define  DREF_SUPERSPREAD_SOURCE_MASK		(3<<7)
#define  DREF_SSC4_DOWNSPREAD                   (0<<6)
#define  DREF_SSC4_CENTERSPREAD                 (1<<6)
#define  DREF_SSC1_DISABLE                      (0<<1)
#define  DREF_SSC1_ENABLE                       (1<<1)
#define  DREF_SSC4_DISABLE                      (0)
#define  DREF_SSC4_ENABLE                       (1)

#define PCH_RAWCLK_FREQ         0xc6204
#define  FDL_TP1_TIMER_SHIFT    12
#define  FDL_TP1_TIMER_MASK     (3<<12)
#define  FDL_TP2_TIMER_SHIFT    10
#define  FDL_TP2_TIMER_MASK     (3<<10)
#define  RAWCLK_FREQ_MASK       0x3ff

#define PCH_DPLL_TMR_CFG        0xc6208

#define PCH_SSC4_PARMS          0xc6210
#define PCH_SSC4_AUX_PARMS      0xc6214

#define PCH_DPLL_SEL		0xc7000
#define	 TRANS_DPLLB_SEL(pipe)		(1 << (pipe * 4))
#define	 TRANS_DPLLA_SEL(pipe)		0
#define  TRANS_DPLL_ENABLE(pipe)	(1 << (pipe * 4 + 3))

/* transcoder */

#define _PCH_TRANS_HTOTAL_A		0xe0000
#define  TRANS_HTOTAL_SHIFT		16
#define  TRANS_HACTIVE_SHIFT		0
#define _PCH_TRANS_HBLANK_A		0xe0004
#define  TRANS_HBLANK_END_SHIFT		16
#define  TRANS_HBLANK_START_SHIFT	0
#define _PCH_TRANS_HSYNC_A		0xe0008
#define  TRANS_HSYNC_END_SHIFT		16
#define  TRANS_HSYNC_START_SHIFT	0
#define _PCH_TRANS_VTOTAL_A		0xe000c
#define  TRANS_VTOTAL_SHIFT		16
#define  TRANS_VACTIVE_SHIFT		0
#define _PCH_TRANS_VBLANK_A		0xe0010
#define  TRANS_VBLANK_END_SHIFT		16
#define  TRANS_VBLANK_START_SHIFT	0
#define _PCH_TRANS_VSYNC_A		0xe0014
#define  TRANS_VSYNC_END_SHIFT	 	16
#define  TRANS_VSYNC_START_SHIFT	0
#define _PCH_TRANS_VSYNCSHIFT_A		0xe0028

#define _PCH_TRANSA_DATA_M1	0xe0030
#define _PCH_TRANSA_DATA_N1	0xe0034
#define _PCH_TRANSA_DATA_M2	0xe0038
#define _PCH_TRANSA_DATA_N2	0xe003c
#define _PCH_TRANSA_LINK_M1	0xe0040
#define _PCH_TRANSA_LINK_N1	0xe0044
#define _PCH_TRANSA_LINK_M2	0xe0048
#define _PCH_TRANSA_LINK_N2	0xe004c

/* Per-transcoder DIP controls (PCH) */
#define _VIDEO_DIP_CTL_A         0xe0200
#define _VIDEO_DIP_DATA_A        0xe0208
#define _VIDEO_DIP_GCP_A         0xe0210

#define _VIDEO_DIP_CTL_B         0xe1200
#define _VIDEO_DIP_DATA_B        0xe1208
#define _VIDEO_DIP_GCP_B         0xe1210

#define TVIDEO_DIP_CTL(pipe) _PIPE(pipe, _VIDEO_DIP_CTL_A, _VIDEO_DIP_CTL_B)
#define TVIDEO_DIP_DATA(pipe) _PIPE(pipe, _VIDEO_DIP_DATA_A, _VIDEO_DIP_DATA_B)
#define TVIDEO_DIP_GCP(pipe) _PIPE(pipe, _VIDEO_DIP_GCP_A, _VIDEO_DIP_GCP_B)

/* Per-transcoder DIP controls (VLV) */
#define VLV_VIDEO_DIP_CTL_A		(VLV_DISPLAY_BASE + 0x60200)
#define VLV_VIDEO_DIP_DATA_A		(VLV_DISPLAY_BASE + 0x60208)
#define VLV_VIDEO_DIP_GDCP_PAYLOAD_A	(VLV_DISPLAY_BASE + 0x60210)

#define VLV_VIDEO_DIP_CTL_B		(VLV_DISPLAY_BASE + 0x61170)
#define VLV_VIDEO_DIP_DATA_B		(VLV_DISPLAY_BASE + 0x61174)
#define VLV_VIDEO_DIP_GDCP_PAYLOAD_B	(VLV_DISPLAY_BASE + 0x61178)

#define CHV_VIDEO_DIP_CTL_C		(VLV_DISPLAY_BASE + 0x611f0)
#define CHV_VIDEO_DIP_DATA_C		(VLV_DISPLAY_BASE + 0x611f4)
#define CHV_VIDEO_DIP_GDCP_PAYLOAD_C	(VLV_DISPLAY_BASE + 0x611f8)

#define VLV_TVIDEO_DIP_CTL(pipe) \
	_PIPE3((pipe), VLV_VIDEO_DIP_CTL_A, \
	       VLV_VIDEO_DIP_CTL_B, CHV_VIDEO_DIP_CTL_C)
#define VLV_TVIDEO_DIP_DATA(pipe) \
	_PIPE3((pipe), VLV_VIDEO_DIP_DATA_A, \
	       VLV_VIDEO_DIP_DATA_B, CHV_VIDEO_DIP_DATA_C)
#define VLV_TVIDEO_DIP_GCP(pipe) \
	_PIPE3((pipe), VLV_VIDEO_DIP_GDCP_PAYLOAD_A, \
		VLV_VIDEO_DIP_GDCP_PAYLOAD_B, CHV_VIDEO_DIP_GDCP_PAYLOAD_C)

/* Haswell DIP controls */
#define HSW_VIDEO_DIP_CTL_A		0x60200
#define HSW_VIDEO_DIP_AVI_DATA_A	0x60220
#define HSW_VIDEO_DIP_VS_DATA_A		0x60260
#define HSW_VIDEO_DIP_SPD_DATA_A	0x602A0
#define HSW_VIDEO_DIP_GMP_DATA_A	0x602E0
#define HSW_VIDEO_DIP_VSC_DATA_A	0x60320
#define HSW_VIDEO_DIP_AVI_ECC_A		0x60240
#define HSW_VIDEO_DIP_VS_ECC_A		0x60280
#define HSW_VIDEO_DIP_SPD_ECC_A		0x602C0
#define HSW_VIDEO_DIP_GMP_ECC_A		0x60300
#define HSW_VIDEO_DIP_VSC_ECC_A		0x60344
#define HSW_VIDEO_DIP_GCP_A		0x60210

#define HSW_VIDEO_DIP_CTL_B		0x61200
#define HSW_VIDEO_DIP_AVI_DATA_B	0x61220
#define HSW_VIDEO_DIP_VS_DATA_B		0x61260
#define HSW_VIDEO_DIP_SPD_DATA_B	0x612A0
#define HSW_VIDEO_DIP_GMP_DATA_B	0x612E0
#define HSW_VIDEO_DIP_VSC_DATA_B	0x61320
#define HSW_VIDEO_DIP_BVI_ECC_B		0x61240
#define HSW_VIDEO_DIP_VS_ECC_B		0x61280
#define HSW_VIDEO_DIP_SPD_ECC_B		0x612C0
#define HSW_VIDEO_DIP_GMP_ECC_B		0x61300
#define HSW_VIDEO_DIP_VSC_ECC_B		0x61344
#define HSW_VIDEO_DIP_GCP_B		0x61210

#define HSW_TVIDEO_DIP_CTL(trans) \
	 _TRANSCODER2(trans, HSW_VIDEO_DIP_CTL_A)
#define HSW_TVIDEO_DIP_AVI_DATA(trans) \
	 _TRANSCODER2(trans, HSW_VIDEO_DIP_AVI_DATA_A)
#define HSW_TVIDEO_DIP_VS_DATA(trans) \
	 _TRANSCODER2(trans, HSW_VIDEO_DIP_VS_DATA_A)
#define HSW_TVIDEO_DIP_SPD_DATA(trans) \
	 _TRANSCODER2(trans, HSW_VIDEO_DIP_SPD_DATA_A)
#define HSW_TVIDEO_DIP_GCP(trans) \
	_TRANSCODER2(trans, HSW_VIDEO_DIP_GCP_A)
#define HSW_TVIDEO_DIP_VSC_DATA(trans) \
	 _TRANSCODER2(trans, HSW_VIDEO_DIP_VSC_DATA_A)

#define HSW_STEREO_3D_CTL_A	0x70020
#define   S3D_ENABLE		(1<<31)
#define HSW_STEREO_3D_CTL_B	0x71020

#define HSW_STEREO_3D_CTL(trans) \
	_PIPE2(trans, HSW_STEREO_3D_CTL_A)

#define _PCH_TRANS_HTOTAL_B          0xe1000
#define _PCH_TRANS_HBLANK_B          0xe1004
#define _PCH_TRANS_HSYNC_B           0xe1008
#define _PCH_TRANS_VTOTAL_B          0xe100c
#define _PCH_TRANS_VBLANK_B          0xe1010
#define _PCH_TRANS_VSYNC_B           0xe1014
#define _PCH_TRANS_VSYNCSHIFT_B	 0xe1028

#define PCH_TRANS_HTOTAL(pipe) _PIPE(pipe, _PCH_TRANS_HTOTAL_A, _PCH_TRANS_HTOTAL_B)
#define PCH_TRANS_HBLANK(pipe) _PIPE(pipe, _PCH_TRANS_HBLANK_A, _PCH_TRANS_HBLANK_B)
#define PCH_TRANS_HSYNC(pipe) _PIPE(pipe, _PCH_TRANS_HSYNC_A, _PCH_TRANS_HSYNC_B)
#define PCH_TRANS_VTOTAL(pipe) _PIPE(pipe, _PCH_TRANS_VTOTAL_A, _PCH_TRANS_VTOTAL_B)
#define PCH_TRANS_VBLANK(pipe) _PIPE(pipe, _PCH_TRANS_VBLANK_A, _PCH_TRANS_VBLANK_B)
#define PCH_TRANS_VSYNC(pipe) _PIPE(pipe, _PCH_TRANS_VSYNC_A, _PCH_TRANS_VSYNC_B)
#define PCH_TRANS_VSYNCSHIFT(pipe) _PIPE(pipe, _PCH_TRANS_VSYNCSHIFT_A, \
					 _PCH_TRANS_VSYNCSHIFT_B)

#define _PCH_TRANSB_DATA_M1	0xe1030
#define _PCH_TRANSB_DATA_N1	0xe1034
#define _PCH_TRANSB_DATA_M2	0xe1038
#define _PCH_TRANSB_DATA_N2	0xe103c
#define _PCH_TRANSB_LINK_M1	0xe1040
#define _PCH_TRANSB_LINK_N1	0xe1044
#define _PCH_TRANSB_LINK_M2	0xe1048
#define _PCH_TRANSB_LINK_N2	0xe104c

#define PCH_TRANS_DATA_M1(pipe) _PIPE(pipe, _PCH_TRANSA_DATA_M1, _PCH_TRANSB_DATA_M1)
#define PCH_TRANS_DATA_N1(pipe) _PIPE(pipe, _PCH_TRANSA_DATA_N1, _PCH_TRANSB_DATA_N1)
#define PCH_TRANS_DATA_M2(pipe) _PIPE(pipe, _PCH_TRANSA_DATA_M2, _PCH_TRANSB_DATA_M2)
#define PCH_TRANS_DATA_N2(pipe) _PIPE(pipe, _PCH_TRANSA_DATA_N2, _PCH_TRANSB_DATA_N2)
#define PCH_TRANS_LINK_M1(pipe) _PIPE(pipe, _PCH_TRANSA_LINK_M1, _PCH_TRANSB_LINK_M1)
#define PCH_TRANS_LINK_N1(pipe) _PIPE(pipe, _PCH_TRANSA_LINK_N1, _PCH_TRANSB_LINK_N1)
#define PCH_TRANS_LINK_M2(pipe) _PIPE(pipe, _PCH_TRANSA_LINK_M2, _PCH_TRANSB_LINK_M2)
#define PCH_TRANS_LINK_N2(pipe) _PIPE(pipe, _PCH_TRANSA_LINK_N2, _PCH_TRANSB_LINK_N2)

#define _PCH_TRANSACONF              0xf0008
#define _PCH_TRANSBCONF              0xf1008
#define PCH_TRANSCONF(pipe) _PIPE(pipe, _PCH_TRANSACONF, _PCH_TRANSBCONF)
#define LPT_TRANSCONF		_PCH_TRANSACONF /* lpt has only one transcoder */
#define  TRANS_DISABLE          (0<<31)
#define  TRANS_ENABLE           (1<<31)
#define  TRANS_STATE_MASK       (1<<30)
#define  TRANS_STATE_DISABLE    (0<<30)
#define  TRANS_STATE_ENABLE     (1<<30)
#define  TRANS_FSYNC_DELAY_HB1  (0<<27)
#define  TRANS_FSYNC_DELAY_HB2  (1<<27)
#define  TRANS_FSYNC_DELAY_HB3  (2<<27)
#define  TRANS_FSYNC_DELAY_HB4  (3<<27)
#define  TRANS_INTERLACE_MASK   (7<<21)
#define  TRANS_PROGRESSIVE      (0<<21)
#define  TRANS_INTERLACED       (3<<21)
#define  TRANS_LEGACY_INTERLACED_ILK (2<<21)
#define  TRANS_8BPC             (0<<5)
#define  TRANS_10BPC            (1<<5)
#define  TRANS_6BPC             (2<<5)
#define  TRANS_12BPC            (3<<5)

#define _TRANSA_CHICKEN1	 0xf0060
#define _TRANSB_CHICKEN1	 0xf1060
#define TRANS_CHICKEN1(pipe) _PIPE(pipe, _TRANSA_CHICKEN1, _TRANSB_CHICKEN1)
#define  TRANS_CHICKEN1_DP0UNIT_GC_DISABLE	(1<<4)
#define _TRANSA_CHICKEN2	 0xf0064
#define _TRANSB_CHICKEN2	 0xf1064
#define TRANS_CHICKEN2(pipe) _PIPE(pipe, _TRANSA_CHICKEN2, _TRANSB_CHICKEN2)
#define  TRANS_CHICKEN2_TIMING_OVERRIDE			(1<<31)
#define  TRANS_CHICKEN2_FDI_POLARITY_REVERSED		(1<<29)
#define  TRANS_CHICKEN2_FRAME_START_DELAY_MASK		(3<<27)
#define  TRANS_CHICKEN2_DISABLE_DEEP_COLOR_COUNTER	(1<<26)
#define  TRANS_CHICKEN2_DISABLE_DEEP_COLOR_MODESWITCH	(1<<25)

#define SOUTH_CHICKEN1		0xc2000
#define  FDIA_PHASE_SYNC_SHIFT_OVR	19
#define  FDIA_PHASE_SYNC_SHIFT_EN	18
#define  FDI_PHASE_SYNC_OVR(pipe) (1<<(FDIA_PHASE_SYNC_SHIFT_OVR - ((pipe) * 2)))
#define  FDI_PHASE_SYNC_EN(pipe) (1<<(FDIA_PHASE_SYNC_SHIFT_EN - ((pipe) * 2)))
#define  FDI_BC_BIFURCATION_SELECT	(1 << 12)
#define SOUTH_CHICKEN2		0xc2004
#define  FDI_MPHY_IOSFSB_RESET_STATUS	(1<<13)
#define  FDI_MPHY_IOSFSB_RESET_CTL	(1<<12)
#define  DPLS_EDP_PPS_FIX_DIS		(1<<0)

#define _FDI_RXA_CHICKEN         0xc200c
#define _FDI_RXB_CHICKEN         0xc2010
#define  FDI_RX_PHASE_SYNC_POINTER_OVR	(1<<1)
#define  FDI_RX_PHASE_SYNC_POINTER_EN	(1<<0)
#define FDI_RX_CHICKEN(pipe) _PIPE(pipe, _FDI_RXA_CHICKEN, _FDI_RXB_CHICKEN)

#define SOUTH_DSPCLK_GATE_D	0xc2020
#define  PCH_DPLUNIT_CLOCK_GATE_DISABLE (1<<30)
#define  PCH_DPLSUNIT_CLOCK_GATE_DISABLE (1<<29)
#define  PCH_CPUNIT_CLOCK_GATE_DISABLE (1<<14)
#define  PCH_LP_PARTITION_LEVEL_DISABLE  (1<<12)

/* CPU: FDI_TX */
#define _FDI_TXA_CTL             0x60100
#define _FDI_TXB_CTL             0x61100
#define FDI_TX_CTL(pipe) _PIPE(pipe, _FDI_TXA_CTL, _FDI_TXB_CTL)
#define  FDI_TX_DISABLE         (0<<31)
#define  FDI_TX_ENABLE          (1<<31)
#define  FDI_LINK_TRAIN_PATTERN_1       (0<<28)
#define  FDI_LINK_TRAIN_PATTERN_2       (1<<28)
#define  FDI_LINK_TRAIN_PATTERN_IDLE    (2<<28)
#define  FDI_LINK_TRAIN_NONE            (3<<28)
#define  FDI_LINK_TRAIN_VOLTAGE_0_4V    (0<<25)
#define  FDI_LINK_TRAIN_VOLTAGE_0_6V    (1<<25)
#define  FDI_LINK_TRAIN_VOLTAGE_0_8V    (2<<25)
#define  FDI_LINK_TRAIN_VOLTAGE_1_2V    (3<<25)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_NONE (0<<22)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_1_5X (1<<22)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_2X   (2<<22)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_3X   (3<<22)
/* ILK always use 400mV 0dB for voltage swing and pre-emphasis level.
   SNB has different settings. */
/* SNB A-stepping */
#define  FDI_LINK_TRAIN_400MV_0DB_SNB_A		(0x38<<22)
#define  FDI_LINK_TRAIN_400MV_6DB_SNB_A		(0x02<<22)
#define  FDI_LINK_TRAIN_600MV_3_5DB_SNB_A	(0x01<<22)
#define  FDI_LINK_TRAIN_800MV_0DB_SNB_A		(0x0<<22)
/* SNB B-stepping */
#define  FDI_LINK_TRAIN_400MV_0DB_SNB_B		(0x0<<22)
#define  FDI_LINK_TRAIN_400MV_6DB_SNB_B		(0x3a<<22)
#define  FDI_LINK_TRAIN_600MV_3_5DB_SNB_B	(0x39<<22)
#define  FDI_LINK_TRAIN_800MV_0DB_SNB_B		(0x38<<22)
#define  FDI_LINK_TRAIN_VOL_EMP_MASK		(0x3f<<22)
#define  FDI_DP_PORT_WIDTH_SHIFT		19
#define  FDI_DP_PORT_WIDTH_MASK			(7 << FDI_DP_PORT_WIDTH_SHIFT)
#define  FDI_DP_PORT_WIDTH(width)           (((width) - 1) << FDI_DP_PORT_WIDTH_SHIFT)
#define  FDI_TX_ENHANCE_FRAME_ENABLE    (1<<18)
/* Ironlake: hardwired to 1 */
#define  FDI_TX_PLL_ENABLE              (1<<14)

/* Ivybridge has different bits for lolz */
#define  FDI_LINK_TRAIN_PATTERN_1_IVB       (0<<8)
#define  FDI_LINK_TRAIN_PATTERN_2_IVB       (1<<8)
#define  FDI_LINK_TRAIN_PATTERN_IDLE_IVB    (2<<8)
#define  FDI_LINK_TRAIN_NONE_IVB            (3<<8)

/* both Tx and Rx */
#define  FDI_COMPOSITE_SYNC		(1<<11)
#define  FDI_LINK_TRAIN_AUTO		(1<<10)
#define  FDI_SCRAMBLING_ENABLE          (0<<7)
#define  FDI_SCRAMBLING_DISABLE         (1<<7)

/* FDI_RX, FDI_X is hard-wired to Transcoder_X */
#define _FDI_RXA_CTL             0xf000c
#define _FDI_RXB_CTL             0xf100c
#define FDI_RX_CTL(pipe) _PIPE(pipe, _FDI_RXA_CTL, _FDI_RXB_CTL)
#define  FDI_RX_ENABLE          (1<<31)
/* train, dp width same as FDI_TX */
#define  FDI_FS_ERRC_ENABLE		(1<<27)
#define  FDI_FE_ERRC_ENABLE		(1<<26)
#define  FDI_RX_POLARITY_REVERSED_LPT	(1<<16)
#define  FDI_8BPC                       (0<<16)
#define  FDI_10BPC                      (1<<16)
#define  FDI_6BPC                       (2<<16)
#define  FDI_12BPC                      (3<<16)
#define  FDI_RX_LINK_REVERSAL_OVERRIDE  (1<<15)
#define  FDI_DMI_LINK_REVERSE_MASK      (1<<14)
#define  FDI_RX_PLL_ENABLE              (1<<13)
#define  FDI_FS_ERR_CORRECT_ENABLE      (1<<11)
#define  FDI_FE_ERR_CORRECT_ENABLE      (1<<10)
#define  FDI_FS_ERR_REPORT_ENABLE       (1<<9)
#define  FDI_FE_ERR_REPORT_ENABLE       (1<<8)
#define  FDI_RX_ENHANCE_FRAME_ENABLE    (1<<6)
#define  FDI_PCDCLK	                (1<<4)
/* CPT */
#define  FDI_AUTO_TRAINING			(1<<10)
#define  FDI_LINK_TRAIN_PATTERN_1_CPT		(0<<8)
#define  FDI_LINK_TRAIN_PATTERN_2_CPT		(1<<8)
#define  FDI_LINK_TRAIN_PATTERN_IDLE_CPT	(2<<8)
#define  FDI_LINK_TRAIN_NORMAL_CPT		(3<<8)
#define  FDI_LINK_TRAIN_PATTERN_MASK_CPT	(3<<8)

#define _FDI_RXA_MISC			0xf0010
#define _FDI_RXB_MISC			0xf1010
#define  FDI_RX_PWRDN_LANE1_MASK	(3<<26)
#define  FDI_RX_PWRDN_LANE1_VAL(x)	((x)<<26)
#define  FDI_RX_PWRDN_LANE0_MASK	(3<<24)
#define  FDI_RX_PWRDN_LANE0_VAL(x)	((x)<<24)
#define  FDI_RX_TP1_TO_TP2_48		(2<<20)
#define  FDI_RX_TP1_TO_TP2_64		(3<<20)
#define  FDI_RX_FDI_DELAY_90		(0x90<<0)
#define FDI_RX_MISC(pipe) _PIPE(pipe, _FDI_RXA_MISC, _FDI_RXB_MISC)

#define _FDI_RXA_TUSIZE1         0xf0030
#define _FDI_RXA_TUSIZE2         0xf0038
#define _FDI_RXB_TUSIZE1         0xf1030
#define _FDI_RXB_TUSIZE2         0xf1038
#define FDI_RX_TUSIZE1(pipe) _PIPE(pipe, _FDI_RXA_TUSIZE1, _FDI_RXB_TUSIZE1)
#define FDI_RX_TUSIZE2(pipe) _PIPE(pipe, _FDI_RXA_TUSIZE2, _FDI_RXB_TUSIZE2)

/* FDI_RX interrupt register format */
#define FDI_RX_INTER_LANE_ALIGN         (1<<10)
#define FDI_RX_SYMBOL_LOCK              (1<<9) /* train 2 */
#define FDI_RX_BIT_LOCK                 (1<<8) /* train 1 */
#define FDI_RX_TRAIN_PATTERN_2_FAIL     (1<<7)
#define FDI_RX_FS_CODE_ERR              (1<<6)
#define FDI_RX_FE_CODE_ERR              (1<<5)
#define FDI_RX_SYMBOL_ERR_RATE_ABOVE    (1<<4)
#define FDI_RX_HDCP_LINK_FAIL           (1<<3)
#define FDI_RX_PIXEL_FIFO_OVERFLOW      (1<<2)
#define FDI_RX_CROSS_CLOCK_OVERFLOW     (1<<1)
#define FDI_RX_SYMBOL_QUEUE_OVERFLOW    (1<<0)

#define _FDI_RXA_IIR             0xf0014
#define _FDI_RXA_IMR             0xf0018
#define _FDI_RXB_IIR             0xf1014
#define _FDI_RXB_IMR             0xf1018
#define FDI_RX_IIR(pipe) _PIPE(pipe, _FDI_RXA_IIR, _FDI_RXB_IIR)
#define FDI_RX_IMR(pipe) _PIPE(pipe, _FDI_RXA_IMR, _FDI_RXB_IMR)

#define FDI_PLL_CTL_1           0xfe000
#define FDI_PLL_CTL_2           0xfe004

#define PCH_LVDS	0xe1180
#define  LVDS_DETECTED	(1 << 1)

/* vlv has 2 sets of panel control regs. */
#define PIPEA_PP_STATUS         (VLV_DISPLAY_BASE + 0x61200)
#define PIPEA_PP_CONTROL        (VLV_DISPLAY_BASE + 0x61204)
#define PIPEA_PP_ON_DELAYS      (VLV_DISPLAY_BASE + 0x61208)
#define  PANEL_PORT_SELECT_VLV(port)	((port) << 30)
#define PIPEA_PP_OFF_DELAYS     (VLV_DISPLAY_BASE + 0x6120c)
#define PIPEA_PP_DIVISOR        (VLV_DISPLAY_BASE + 0x61210)

#define PIPEB_PP_STATUS         (VLV_DISPLAY_BASE + 0x61300)
#define PIPEB_PP_CONTROL        (VLV_DISPLAY_BASE + 0x61304)
#define PIPEB_PP_ON_DELAYS      (VLV_DISPLAY_BASE + 0x61308)
#define PIPEB_PP_OFF_DELAYS     (VLV_DISPLAY_BASE + 0x6130c)
#define PIPEB_PP_DIVISOR        (VLV_DISPLAY_BASE + 0x61310)

#define VLV_PIPE_PP_STATUS(pipe) _PIPE(pipe, PIPEA_PP_STATUS, PIPEB_PP_STATUS)
#define VLV_PIPE_PP_CONTROL(pipe) _PIPE(pipe, PIPEA_PP_CONTROL, PIPEB_PP_CONTROL)
#define VLV_PIPE_PP_ON_DELAYS(pipe) \
		_PIPE(pipe, PIPEA_PP_ON_DELAYS, PIPEB_PP_ON_DELAYS)
#define VLV_PIPE_PP_OFF_DELAYS(pipe) \
		_PIPE(pipe, PIPEA_PP_OFF_DELAYS, PIPEB_PP_OFF_DELAYS)
#define VLV_PIPE_PP_DIVISOR(pipe) \
		_PIPE(pipe, PIPEA_PP_DIVISOR, PIPEB_PP_DIVISOR)

#define PCH_PP_STATUS		0xc7200
#define PCH_PP_CONTROL		0xc7204
#define  PANEL_UNLOCK_REGS	(0xabcd << 16)
#define  PANEL_UNLOCK_MASK	(0xffff << 16)
#define  EDP_FORCE_VDD		(1 << 3)
#define  EDP_BLC_ENABLE		(1 << 2)
#define  PANEL_POWER_RESET	(1 << 1)
#define  PANEL_POWER_OFF	(0 << 0)
#define  PANEL_POWER_ON		(1 << 0)
#define PCH_PP_ON_DELAYS	0xc7208
#define  PANEL_PORT_SELECT_MASK	(3 << 30)
#define  PANEL_PORT_SELECT_LVDS	(0 << 30)
#define  PANEL_PORT_SELECT_DPA	(1 << 30)
#define  PANEL_PORT_SELECT_DPC	(2 << 30)
#define  PANEL_PORT_SELECT_DPD	(3 << 30)
#define  PANEL_POWER_UP_DELAY_MASK	(0x1fff0000)
#define  PANEL_POWER_UP_DELAY_SHIFT	16
#define  PANEL_LIGHT_ON_DELAY_MASK	(0x1fff)
#define  PANEL_LIGHT_ON_DELAY_SHIFT	0

#define PCH_PP_OFF_DELAYS	0xc720c
#define  PANEL_POWER_DOWN_DELAY_MASK	(0x1fff0000)
#define  PANEL_POWER_DOWN_DELAY_SHIFT	16
#define  PANEL_LIGHT_OFF_DELAY_MASK	(0x1fff)
#define  PANEL_LIGHT_OFF_DELAY_SHIFT	0

#define PCH_PP_DIVISOR		0xc7210
#define  PP_REFERENCE_DIVIDER_MASK	(0xffffff00)
#define  PP_REFERENCE_DIVIDER_SHIFT	8
#define  PANEL_POWER_CYCLE_DELAY_MASK	(0x1f)
#define  PANEL_POWER_CYCLE_DELAY_SHIFT	0

#define PCH_DP_B		0xe4100
#define PCH_DPB_AUX_CH_CTL	0xe4110
#define PCH_DPB_AUX_CH_DATA1	0xe4114
#define PCH_DPB_AUX_CH_DATA2	0xe4118
#define PCH_DPB_AUX_CH_DATA3	0xe411c
#define PCH_DPB_AUX_CH_DATA4	0xe4120
#define PCH_DPB_AUX_CH_DATA5	0xe4124

#define PCH_DP_C		0xe4200
#define PCH_DPC_AUX_CH_CTL	0xe4210
#define PCH_DPC_AUX_CH_DATA1	0xe4214
#define PCH_DPC_AUX_CH_DATA2	0xe4218
#define PCH_DPC_AUX_CH_DATA3	0xe421c
#define PCH_DPC_AUX_CH_DATA4	0xe4220
#define PCH_DPC_AUX_CH_DATA5	0xe4224

#define PCH_DP_D		0xe4300
#define PCH_DPD_AUX_CH_CTL	0xe4310
#define PCH_DPD_AUX_CH_DATA1	0xe4314
#define PCH_DPD_AUX_CH_DATA2	0xe4318
#define PCH_DPD_AUX_CH_DATA3	0xe431c
#define PCH_DPD_AUX_CH_DATA4	0xe4320
#define PCH_DPD_AUX_CH_DATA5	0xe4324

/* CPT */
#define  PORT_TRANS_A_SEL_CPT	0
#define  PORT_TRANS_B_SEL_CPT	(1<<29)
#define  PORT_TRANS_C_SEL_CPT	(2<<29)
#define  PORT_TRANS_SEL_MASK	(3<<29)
#define  PORT_TRANS_SEL_CPT(pipe)	((pipe) << 29)
#define  PORT_TO_PIPE(val)	(((val) & (1<<30)) >> 30)
#define  PORT_TO_PIPE_CPT(val)	(((val) & PORT_TRANS_SEL_MASK) >> 29)
#define  SDVO_PORT_TO_PIPE_CHV(val)	(((val) & (3<<24)) >> 24)
#define  DP_PORT_TO_PIPE_CHV(val)	(((val) & (3<<16)) >> 16)

#define TRANS_DP_CTL_A		0xe0300
#define TRANS_DP_CTL_B		0xe1300
#define TRANS_DP_CTL_C		0xe2300
#define TRANS_DP_CTL(pipe)	_PIPE(pipe, TRANS_DP_CTL_A, TRANS_DP_CTL_B)
#define  TRANS_DP_OUTPUT_ENABLE	(1<<31)
#define  TRANS_DP_PORT_SEL_B	(0<<29)
#define  TRANS_DP_PORT_SEL_C	(1<<29)
#define  TRANS_DP_PORT_SEL_D	(2<<29)
#define  TRANS_DP_PORT_SEL_NONE	(3<<29)
#define  TRANS_DP_PORT_SEL_MASK	(3<<29)
#define  TRANS_DP_AUDIO_ONLY	(1<<26)
#define  TRANS_DP_ENH_FRAMING	(1<<18)
#define  TRANS_DP_8BPC		(0<<9)
#define  TRANS_DP_10BPC		(1<<9)
#define  TRANS_DP_6BPC		(2<<9)
#define  TRANS_DP_12BPC		(3<<9)
#define  TRANS_DP_BPC_MASK	(3<<9)
#define  TRANS_DP_VSYNC_ACTIVE_HIGH	(1<<4)
#define  TRANS_DP_VSYNC_ACTIVE_LOW	0
#define  TRANS_DP_HSYNC_ACTIVE_HIGH	(1<<3)
#define  TRANS_DP_HSYNC_ACTIVE_LOW	0
#define  TRANS_DP_SYNC_MASK	(3<<3)

/* SNB eDP training params */
/* SNB A-stepping */
#define  EDP_LINK_TRAIN_400MV_0DB_SNB_A		(0x38<<22)
#define  EDP_LINK_TRAIN_400MV_6DB_SNB_A		(0x02<<22)
#define  EDP_LINK_TRAIN_600MV_3_5DB_SNB_A	(0x01<<22)
#define  EDP_LINK_TRAIN_800MV_0DB_SNB_A		(0x0<<22)
/* SNB B-stepping */
#define  EDP_LINK_TRAIN_400_600MV_0DB_SNB_B	(0x0<<22)
#define  EDP_LINK_TRAIN_400MV_3_5DB_SNB_B	(0x1<<22)
#define  EDP_LINK_TRAIN_400_600MV_6DB_SNB_B	(0x3a<<22)
#define  EDP_LINK_TRAIN_600_800MV_3_5DB_SNB_B	(0x39<<22)
#define  EDP_LINK_TRAIN_800_1200MV_0DB_SNB_B	(0x38<<22)
#define  EDP_LINK_TRAIN_VOL_EMP_MASK_SNB	(0x3f<<22)

/* IVB */
#define EDP_LINK_TRAIN_400MV_0DB_IVB		(0x24 <<22)
#define EDP_LINK_TRAIN_400MV_3_5DB_IVB		(0x2a <<22)
#define EDP_LINK_TRAIN_400MV_6DB_IVB		(0x2f <<22)
#define EDP_LINK_TRAIN_600MV_0DB_IVB		(0x30 <<22)
#define EDP_LINK_TRAIN_600MV_3_5DB_IVB		(0x36 <<22)
#define EDP_LINK_TRAIN_800MV_0DB_IVB		(0x38 <<22)
#define EDP_LINK_TRAIN_800MV_3_5DB_IVB		(0x3e <<22)

/* legacy values */
#define EDP_LINK_TRAIN_500MV_0DB_IVB		(0x00 <<22)
#define EDP_LINK_TRAIN_1000MV_0DB_IVB		(0x20 <<22)
#define EDP_LINK_TRAIN_500MV_3_5DB_IVB		(0x02 <<22)
#define EDP_LINK_TRAIN_1000MV_3_5DB_IVB		(0x22 <<22)
#define EDP_LINK_TRAIN_1000MV_6DB_IVB		(0x23 <<22)

#define  EDP_LINK_TRAIN_VOL_EMP_MASK_IVB	(0x3f<<22)

#define  VLV_PMWGICZ				0x1300a4

#define  FORCEWAKE				0xA18C
#define  FORCEWAKE_VLV				0x1300b0
#define  FORCEWAKE_ACK_VLV			0x1300b4
#define  FORCEWAKE_MEDIA_VLV			0x1300b8
#define  FORCEWAKE_ACK_MEDIA_VLV		0x1300bc
#define  FORCEWAKE_ACK_HSW			0x130044
#define  FORCEWAKE_ACK				0x130090
#define  VLV_GTLC_WAKE_CTRL			0x130090
#define   VLV_GTLC_RENDER_CTX_EXISTS		(1 << 25)
#define   VLV_GTLC_MEDIA_CTX_EXISTS		(1 << 24)
#define   VLV_GTLC_ALLOWWAKEREQ			(1 << 0)

#define  VLV_GTLC_PW_STATUS			0x130094
#define   VLV_GTLC_ALLOWWAKEACK			(1 << 0)
#define   VLV_GTLC_ALLOWWAKEERR			(1 << 1)
#define   VLV_GTLC_PW_MEDIA_STATUS_MASK		(1 << 5)
#define   VLV_GTLC_PW_RENDER_STATUS_MASK	(1 << 7)
#define  FORCEWAKE_MT				0xa188 /* multi-threaded */
#define  FORCEWAKE_MEDIA_GEN9			0xa270
#define  FORCEWAKE_RENDER_GEN9			0xa278
#define  FORCEWAKE_BLITTER_GEN9			0xa188
#define  FORCEWAKE_ACK_MEDIA_GEN9		0x0D88
#define  FORCEWAKE_ACK_RENDER_GEN9		0x0D84
#define  FORCEWAKE_ACK_BLITTER_GEN9		0x130044
#define   FORCEWAKE_KERNEL			0x1
#define   FORCEWAKE_USER			0x2
#define  FORCEWAKE_MT_ACK			0x130040
#define  ECOBUS					0xa180
#define    FORCEWAKE_MT_ENABLE			(1<<5)
#define  VLV_SPAREG2H				0xA194

#define  GTFIFODBG				0x120000
#define    GT_FIFO_SBDROPERR			(1<<6)
#define    GT_FIFO_BLOBDROPERR			(1<<5)
#define    GT_FIFO_SB_READ_ABORTERR		(1<<4)
#define    GT_FIFO_DROPERR			(1<<3)
#define    GT_FIFO_OVFERR			(1<<2)
#define    GT_FIFO_IAWRERR			(1<<1)
#define    GT_FIFO_IARDERR			(1<<0)

#define  GTFIFOCTL				0x120008
#define    GT_FIFO_FREE_ENTRIES_MASK		0x7f
#define    GT_FIFO_NUM_RESERVED_ENTRIES		20

#define  HSW_IDICR				0x9008
#define    IDIHASHMSK(x)			(((x) & 0x3f) << 16)
#define  HSW_EDRAM_PRESENT			0x120010

#define GEN6_UCGCTL1				0x9400
# define GEN6_EU_TCUNIT_CLOCK_GATE_DISABLE		(1 << 16)
# define GEN6_BLBUNIT_CLOCK_GATE_DISABLE		(1 << 5)
# define GEN6_CSUNIT_CLOCK_GATE_DISABLE			(1 << 7)

#define GEN6_UCGCTL2				0x9404
# define GEN7_VDSUNIT_CLOCK_GATE_DISABLE		(1 << 30)
# define GEN7_TDLUNIT_CLOCK_GATE_DISABLE		(1 << 22)
# define GEN6_RCZUNIT_CLOCK_GATE_DISABLE		(1 << 13)
# define GEN6_RCPBUNIT_CLOCK_GATE_DISABLE		(1 << 12)
# define GEN6_RCCUNIT_CLOCK_GATE_DISABLE		(1 << 11)

#define GEN6_UCGCTL3				0x9408

#define GEN7_UCGCTL4				0x940c
#define  GEN7_L3BANK2X_CLOCK_GATE_DISABLE	(1<<25)

#define GEN6_RCGCTL1				0x9410
#define GEN6_RCGCTL2				0x9414
#define GEN6_RSTCTL				0x9420

#define GEN8_UCGCTL6				0x9430
#define   GEN8_SDEUNIT_CLOCK_GATE_DISABLE	(1<<14)

#define GEN6_GFXPAUSE				0xA000
#define GEN6_RPNSWREQ				0xA008
#define   GEN6_TURBO_DISABLE			(1<<31)
#define   GEN6_FREQUENCY(x)			((x)<<25)
#define   HSW_FREQUENCY(x)			((x)<<24)
#define   GEN6_OFFSET(x)			((x)<<19)
#define   GEN6_AGGRESSIVE_TURBO			(0<<15)
#define GEN6_RC_VIDEO_FREQ			0xA00C
#define GEN6_RC_CONTROL				0xA090
#define   GEN6_RC_CTL_RC6pp_ENABLE		(1<<16)
#define   GEN6_RC_CTL_RC6p_ENABLE		(1<<17)
#define   GEN6_RC_CTL_RC6_ENABLE		(1<<18)
#define   GEN6_RC_CTL_RC1e_ENABLE		(1<<20)
#define   GEN6_RC_CTL_RC7_ENABLE		(1<<22)
#define   VLV_RC_CTL_CTX_RST_PARALLEL		(1<<24)
#define   GEN7_RC_CTL_TO_MODE			(1<<28)
#define   GEN6_RC_CTL_EI_MODE(x)		((x)<<27)
#define   GEN6_RC_CTL_HW_ENABLE			(1<<31)
#define GEN6_RP_DOWN_TIMEOUT			0xA010
#define GEN6_RP_INTERRUPT_LIMITS		0xA014
#define GEN6_RPSTAT1				0xA01C
#define   GEN6_CAGF_SHIFT			8
#define   HSW_CAGF_SHIFT			7
#define   GEN6_CAGF_MASK			(0x7f << GEN6_CAGF_SHIFT)
#define   HSW_CAGF_MASK				(0x7f << HSW_CAGF_SHIFT)
#define GEN6_RP_CONTROL				0xA024
#define   GEN6_RP_MEDIA_TURBO			(1<<11)
#define   GEN6_RP_MEDIA_MODE_MASK		(3<<9)
#define   GEN6_RP_MEDIA_HW_TURBO_MODE		(3<<9)
#define   GEN6_RP_MEDIA_HW_NORMAL_MODE		(2<<9)
#define   GEN6_RP_MEDIA_HW_MODE			(1<<9)
#define   GEN6_RP_MEDIA_SW_MODE			(0<<9)
#define   GEN6_RP_MEDIA_IS_GFX			(1<<8)
#define   GEN6_RP_ENABLE			(1<<7)
#define   GEN6_RP_UP_IDLE_MIN			(0x1<<3)
#define   GEN6_RP_UP_BUSY_AVG			(0x2<<3)
#define   GEN6_RP_UP_BUSY_CONT			(0x4<<3)
#define   GEN6_RP_DOWN_IDLE_AVG			(0x2<<0)
#define   GEN6_RP_DOWN_IDLE_CONT		(0x1<<0)
#define GEN6_RP_UP_THRESHOLD			0xA02C
#define GEN6_RP_DOWN_THRESHOLD			0xA030
#define GEN6_RP_CUR_UP_EI			0xA050
#define   GEN6_CURICONT_MASK			0xffffff
#define GEN6_RP_CUR_UP				0xA054
#define   GEN6_CURBSYTAVG_MASK			0xffffff
#define GEN6_RP_PREV_UP				0xA058
#define GEN6_RP_CUR_DOWN_EI			0xA05C
#define   GEN6_CURIAVG_MASK			0xffffff
#define GEN6_RP_CUR_DOWN			0xA060
#define GEN6_RP_PREV_DOWN			0xA064
#define GEN6_RP_UP_EI				0xA068
#define GEN6_RP_DOWN_EI				0xA06C
#define GEN6_RP_IDLE_HYSTERSIS			0xA070
#define GEN6_RPDEUHWTC				0xA080
#define GEN6_RPDEUC				0xA084
#define GEN6_RPDEUCSW				0xA088
#define GEN6_RC_STATE				0xA094
#define GEN6_RC1_WAKE_RATE_LIMIT		0xA098
#define GEN6_RC6_WAKE_RATE_LIMIT		0xA09C
#define GEN6_RC6pp_WAKE_RATE_LIMIT		0xA0A0
#define GEN6_RC_EVALUATION_INTERVAL		0xA0A8
#define GEN6_RC_IDLE_HYSTERSIS			0xA0AC
#define GEN6_RC_SLEEP				0xA0B0
#define GEN6_RCUBMABDTMR			0xA0B0
#define GEN6_RC1e_THRESHOLD			0xA0B4
#define GEN6_RC6_THRESHOLD			0xA0B8
#define GEN6_RC6p_THRESHOLD			0xA0BC
#define VLV_RCEDATA				0xA0BC
#define GEN6_RC6pp_THRESHOLD			0xA0C0
#define GEN6_PMINTRMSK				0xA168
#define GEN8_PMINTR_REDIRECT_TO_NON_DISP	(1<<31)
#define VLV_PWRDWNUPCTL				0xA294

#define VLV_CHICKEN_3				(VLV_DISPLAY_BASE + 0x7040C)
#define  PIXEL_OVERLAP_CNT_MASK			(3 << 30)
#define  PIXEL_OVERLAP_CNT_SHIFT		30

#define GEN6_PMISR				0x44020
#define GEN6_PMIMR				0x44024 /* rps_lock */
#define GEN6_PMIIR				0x44028
#define GEN6_PMIER				0x4402C
#define  GEN6_PM_MBOX_EVENT			(1<<25)
#define  GEN6_PM_THERMAL_EVENT			(1<<24)
#define  GEN6_PM_RP_DOWN_TIMEOUT		(1<<6)
#define  GEN6_PM_RP_UP_THRESHOLD		(1<<5)
#define  GEN6_PM_RP_DOWN_THRESHOLD		(1<<4)
#define  GEN6_PM_RP_UP_EI_EXPIRED		(1<<2)
#define  GEN6_PM_RP_DOWN_EI_EXPIRED		(1<<1)
#define  GEN6_PM_RPS_EVENTS			(GEN6_PM_RP_UP_THRESHOLD | \
						 GEN6_PM_RP_DOWN_THRESHOLD | \
						 GEN6_PM_RP_DOWN_TIMEOUT)

#define GEN7_GT_SCRATCH_BASE			0x4F100
#define GEN7_GT_SCRATCH_REG_NUM			8

#define VLV_GTLC_SURVIVABILITY_REG              0x130098
#define VLV_GFX_CLK_STATUS_BIT			(1<<3)
#define VLV_GFX_CLK_FORCE_ON_BIT		(1<<2)

#define GEN6_GT_GFX_RC6_LOCKED			0x138104
#define VLV_COUNTER_CONTROL			0x138104
#define   VLV_COUNT_RANGE_HIGH			(1<<15)
#define   VLV_MEDIA_RC0_COUNT_EN		(1<<5)
#define   VLV_RENDER_RC0_COUNT_EN		(1<<4)
#define   VLV_MEDIA_RC6_COUNT_EN		(1<<1)
#define   VLV_RENDER_RC6_COUNT_EN		(1<<0)
#define GEN6_GT_GFX_RC6				0x138108
#define VLV_GT_RENDER_RC6			0x138108
#define VLV_GT_MEDIA_RC6			0x13810C

#define GEN6_GT_GFX_RC6p			0x13810C
#define GEN6_GT_GFX_RC6pp			0x138110
#define VLV_RENDER_C0_COUNT_REG		0x138118
#define VLV_MEDIA_C0_COUNT_REG			0x13811C

#define GEN6_PCODE_MAILBOX			0x138124
#define   GEN6_PCODE_READY			(1<<31)
#define   GEN6_READ_OC_PARAMS			0xc
#define   GEN6_PCODE_WRITE_MIN_FREQ_TABLE	0x8
#define   GEN6_PCODE_READ_MIN_FREQ_TABLE	0x9
#define	  GEN6_PCODE_WRITE_RC6VIDS		0x4
#define	  GEN6_PCODE_READ_RC6VIDS		0x5
#define   GEN6_PCODE_READ_D_COMP		0x10
#define   GEN6_PCODE_WRITE_D_COMP		0x11
#define   GEN6_ENCODE_RC6_VID(mv)		(((mv) - 245) / 5)
#define   GEN6_DECODE_RC6_VID(vids)		(((vids) * 5) + 245)
#define   DISPLAY_IPS_CONTROL			0x19
#define	  HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL	0x1A
#define GEN6_PCODE_DATA				0x138128
#define   GEN6_PCODE_FREQ_IA_RATIO_SHIFT	8
#define   GEN6_PCODE_FREQ_RING_RATIO_SHIFT	16
#define GEN6_PCODE_DATA1			0x13812C

#define   GEN9_PCODE_READ_MEM_LATENCY		0x6
#define   GEN9_MEM_LATENCY_LEVEL_MASK		0xFF
#define   GEN9_MEM_LATENCY_LEVEL_1_5_SHIFT	8
#define   GEN9_MEM_LATENCY_LEVEL_2_6_SHIFT	16
#define   GEN9_MEM_LATENCY_LEVEL_3_7_SHIFT	24

#define GEN6_GT_CORE_STATUS		0x138060
#define   GEN6_CORE_CPD_STATE_MASK	(7<<4)
#define   GEN6_RCn_MASK			7
#define   GEN6_RC0			0
#define   GEN6_RC3			2
#define   GEN6_RC6			3
#define   GEN6_RC7			4

#define GEN7_MISCCPCTL			(0x9424)
#define   GEN7_DOP_CLOCK_GATE_ENABLE	(1<<0)

/* IVYBRIDGE DPF */
#define GEN7_L3CDERRST1			0xB008 /* L3CD Error Status 1 */
#define HSW_L3CDERRST11			0xB208 /* L3CD Error Status register 1 slice 1 */
#define   GEN7_L3CDERRST1_ROW_MASK	(0x7ff<<14)
#define   GEN7_PARITY_ERROR_VALID	(1<<13)
#define   GEN7_L3CDERRST1_BANK_MASK	(3<<11)
#define   GEN7_L3CDERRST1_SUBBANK_MASK	(7<<8)
#define GEN7_PARITY_ERROR_ROW(reg) \
		((reg & GEN7_L3CDERRST1_ROW_MASK) >> 14)
#define GEN7_PARITY_ERROR_BANK(reg) \
		((reg & GEN7_L3CDERRST1_BANK_MASK) >> 11)
#define GEN7_PARITY_ERROR_SUBBANK(reg) \
		((reg & GEN7_L3CDERRST1_SUBBANK_MASK) >> 8)
#define   GEN7_L3CDERRST1_ENABLE	(1<<7)

#define GEN7_L3LOG_BASE			0xB070
#define HSW_L3LOG_BASE_SLICE1		0xB270
#define GEN7_L3LOG_SIZE			0x80

#define GEN7_HALF_SLICE_CHICKEN1	0xe100 /* IVB GT1 + VLV */
#define GEN7_HALF_SLICE_CHICKEN1_GT2	0xf100
#define   GEN7_MAX_PS_THREAD_DEP		(8<<12)
#define   GEN7_SINGLE_SUBSCAN_DISPATCH_ENABLE	(1<<10)
#define   GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE	(1<<3)

#define GEN9_HALF_SLICE_CHICKEN5	0xe188
#define   GEN9_DG_MIRROR_FIX_ENABLE	(1<<5)

#define GEN8_ROW_CHICKEN		0xe4f0
#define   PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE	(1<<8)
#define   STALL_DOP_GATING_DISABLE		(1<<5)

#define GEN7_ROW_CHICKEN2		0xe4f4
#define GEN7_ROW_CHICKEN2_GT2		0xf4f4
#define   DOP_CLOCK_GATING_DISABLE	(1<<0)

#define HSW_ROW_CHICKEN3		0xe49c
#define  HSW_ROW_CHICKEN3_L3_GLOBAL_ATOMICS_DISABLE    (1 << 6)

#define HALF_SLICE_CHICKEN3		0xe184
#define   GEN8_CENTROID_PIXEL_OPT_DIS	(1<<8)
#define   GEN8_SAMPLER_POWER_BYPASS_DIS	(1<<1)

/* Audio */
#define G4X_AUD_VID_DID			(dev_priv->info.display_mmio_offset + 0x62020)
#define   INTEL_AUDIO_DEVCL		0x808629FB
#define   INTEL_AUDIO_DEVBLC		0x80862801
#define   INTEL_AUDIO_DEVCTG		0x80862802

#define G4X_AUD_CNTL_ST			0x620B4
#define   G4X_ELDV_DEVCL_DEVBLC		(1 << 13)
#define   G4X_ELDV_DEVCTG		(1 << 14)
#define   G4X_ELD_ADDR_MASK		(0xf << 5)
#define   G4X_ELD_ACK			(1 << 4)
#define G4X_HDMIW_HDMIEDID		0x6210C

#define _IBX_HDMIW_HDMIEDID_A		0xE2050
#define _IBX_HDMIW_HDMIEDID_B		0xE2150
#define IBX_HDMIW_HDMIEDID(pipe) _PIPE(pipe, \
					_IBX_HDMIW_HDMIEDID_A, \
					_IBX_HDMIW_HDMIEDID_B)
#define _IBX_AUD_CNTL_ST_A		0xE20B4
#define _IBX_AUD_CNTL_ST_B		0xE21B4
#define IBX_AUD_CNTL_ST(pipe) _PIPE(pipe, \
					_IBX_AUD_CNTL_ST_A, \
					_IBX_AUD_CNTL_ST_B)
#define   IBX_ELD_BUFFER_SIZE_MASK	(0x1f << 10)
#define   IBX_ELD_ADDRESS_MASK		(0x1f << 5)
#define   IBX_ELD_ACK			(1 << 4)
#define IBX_AUD_CNTL_ST2		0xE20C0
#define   IBX_CP_READY(port)		((1 << 1) << (((port) - 1) * 4))
#define   IBX_ELD_VALID(port)		((1 << 0) << (((port) - 1) * 4))

#define _CPT_HDMIW_HDMIEDID_A		0xE5050
#define _CPT_HDMIW_HDMIEDID_B		0xE5150
#define CPT_HDMIW_HDMIEDID(pipe) _PIPE(pipe, \
					_CPT_HDMIW_HDMIEDID_A, \
					_CPT_HDMIW_HDMIEDID_B)
#define _CPT_AUD_CNTL_ST_A		0xE50B4
#define _CPT_AUD_CNTL_ST_B		0xE51B4
#define CPT_AUD_CNTL_ST(pipe) _PIPE(pipe, \
					_CPT_AUD_CNTL_ST_A, \
					_CPT_AUD_CNTL_ST_B)
#define CPT_AUD_CNTRL_ST2		0xE50C0

#define _VLV_HDMIW_HDMIEDID_A		(VLV_DISPLAY_BASE + 0x62050)
#define _VLV_HDMIW_HDMIEDID_B		(VLV_DISPLAY_BASE + 0x62150)
#define VLV_HDMIW_HDMIEDID(pipe) _PIPE(pipe, \
					_VLV_HDMIW_HDMIEDID_A, \
					_VLV_HDMIW_HDMIEDID_B)
#define _VLV_AUD_CNTL_ST_A		(VLV_DISPLAY_BASE + 0x620B4)
#define _VLV_AUD_CNTL_ST_B		(VLV_DISPLAY_BASE + 0x621B4)
#define VLV_AUD_CNTL_ST(pipe) _PIPE(pipe, \
					_VLV_AUD_CNTL_ST_A, \
					_VLV_AUD_CNTL_ST_B)
#define VLV_AUD_CNTL_ST2		(VLV_DISPLAY_BASE + 0x620C0)

/* These are the 4 32-bit write offset registers for each stream
 * output buffer.  It determines the offset from the
 * 3DSTATE_SO_BUFFERs that the next streamed vertex output goes to.
 */
#define GEN7_SO_WRITE_OFFSET(n)		(0x5280 + (n) * 4)

#define _IBX_AUD_CONFIG_A		0xe2000
#define _IBX_AUD_CONFIG_B		0xe2100
#define IBX_AUD_CFG(pipe) _PIPE(pipe, \
					_IBX_AUD_CONFIG_A, \
					_IBX_AUD_CONFIG_B)
#define _CPT_AUD_CONFIG_A		0xe5000
#define _CPT_AUD_CONFIG_B		0xe5100
#define CPT_AUD_CFG(pipe) _PIPE(pipe, \
					_CPT_AUD_CONFIG_A, \
					_CPT_AUD_CONFIG_B)
#define _VLV_AUD_CONFIG_A		(VLV_DISPLAY_BASE + 0x62000)
#define _VLV_AUD_CONFIG_B		(VLV_DISPLAY_BASE + 0x62100)
#define VLV_AUD_CFG(pipe) _PIPE(pipe, \
					_VLV_AUD_CONFIG_A, \
					_VLV_AUD_CONFIG_B)

#define   AUD_CONFIG_N_VALUE_INDEX		(1 << 29)
#define   AUD_CONFIG_N_PROG_ENABLE		(1 << 28)
#define   AUD_CONFIG_UPPER_N_SHIFT		20
#define   AUD_CONFIG_UPPER_N_MASK		(0xff << 20)
#define   AUD_CONFIG_LOWER_N_SHIFT		4
#define   AUD_CONFIG_LOWER_N_MASK		(0xfff << 4)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_SHIFT	16
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_MASK	(0xf << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_25175	(0 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_25200	(1 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_27000	(2 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_27027	(3 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_54000	(4 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_54054	(5 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_74176	(6 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_74250	(7 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_148352	(8 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_148500	(9 << 16)
#define   AUD_CONFIG_DISABLE_NCTS		(1 << 3)

/* HSW Audio */
#define _HSW_AUD_CONFIG_A		0x65000
#define _HSW_AUD_CONFIG_B		0x65100
#define HSW_AUD_CFG(pipe) _PIPE(pipe, \
					_HSW_AUD_CONFIG_A, \
					_HSW_AUD_CONFIG_B)

#define _HSW_AUD_MISC_CTRL_A		0x65010
#define _HSW_AUD_MISC_CTRL_B		0x65110
#define HSW_AUD_MISC_CTRL(pipe) _PIPE(pipe, \
					_HSW_AUD_MISC_CTRL_A, \
					_HSW_AUD_MISC_CTRL_B)

#define _HSW_AUD_DIP_ELD_CTRL_ST_A	0x650b4
#define _HSW_AUD_DIP_ELD_CTRL_ST_B	0x651b4
#define HSW_AUD_DIP_ELD_CTRL(pipe) _PIPE(pipe, \
					_HSW_AUD_DIP_ELD_CTRL_ST_A, \
					_HSW_AUD_DIP_ELD_CTRL_ST_B)

/* Audio Digital Converter */
#define _HSW_AUD_DIG_CNVT_1		0x65080
#define _HSW_AUD_DIG_CNVT_2		0x65180
#define AUD_DIG_CNVT(pipe) _PIPE(pipe, \
					_HSW_AUD_DIG_CNVT_1, \
					_HSW_AUD_DIG_CNVT_2)
#define DIP_PORT_SEL_MASK		0x3

#define _HSW_AUD_EDID_DATA_A		0x65050
#define _HSW_AUD_EDID_DATA_B		0x65150
#define HSW_AUD_EDID_DATA(pipe) _PIPE(pipe, \
					_HSW_AUD_EDID_DATA_A, \
					_HSW_AUD_EDID_DATA_B)

#define HSW_AUD_PIPE_CONV_CFG		0x6507c
#define HSW_AUD_PIN_ELD_CP_VLD		0x650c0
#define   AUDIO_INACTIVE(trans)		((1 << 3) << ((trans) * 4))
#define   AUDIO_OUTPUT_ENABLE(trans)	((1 << 2) << ((trans) * 4))
#define   AUDIO_CP_READY(trans)		((1 << 1) << ((trans) * 4))
#define   AUDIO_ELD_VALID(trans)	((1 << 0) << ((trans) * 4))

/* HSW Power Wells */
#define HSW_PWR_WELL_BIOS			0x45400 /* CTL1 */
#define HSW_PWR_WELL_DRIVER			0x45404 /* CTL2 */
#define HSW_PWR_WELL_KVMR			0x45408 /* CTL3 */
#define HSW_PWR_WELL_DEBUG			0x4540C /* CTL4 */
#define   HSW_PWR_WELL_ENABLE_REQUEST		(1<<31)
#define   HSW_PWR_WELL_STATE_ENABLED		(1<<30)
#define HSW_PWR_WELL_CTL5			0x45410
#define   HSW_PWR_WELL_ENABLE_SINGLE_STEP	(1<<31)
#define   HSW_PWR_WELL_PWR_GATE_OVERRIDE	(1<<20)
#define   HSW_PWR_WELL_FORCE_ON			(1<<19)
#define HSW_PWR_WELL_CTL6			0x45414

/* Per-pipe DDI Function Control */
#define TRANS_DDI_FUNC_CTL_A		0x60400
#define TRANS_DDI_FUNC_CTL_B		0x61400
#define TRANS_DDI_FUNC_CTL_C		0x62400
#define TRANS_DDI_FUNC_CTL_EDP		0x6F400
#define TRANS_DDI_FUNC_CTL(tran) _TRANSCODER2(tran, TRANS_DDI_FUNC_CTL_A)

#define  TRANS_DDI_FUNC_ENABLE		(1<<31)
/* Those bits are ignored by pipe EDP since it can only connect to DDI A */
#define  TRANS_DDI_PORT_MASK		(7<<28)
#define  TRANS_DDI_PORT_SHIFT		28
#define  TRANS_DDI_SELECT_PORT(x)	((x)<<28)
#define  TRANS_DDI_PORT_NONE		(0<<28)
#define  TRANS_DDI_MODE_SELECT_MASK	(7<<24)
#define  TRANS_DDI_MODE_SELECT_HDMI	(0<<24)
#define  TRANS_DDI_MODE_SELECT_DVI	(1<<24)
#define  TRANS_DDI_MODE_SELECT_DP_SST	(2<<24)
#define  TRANS_DDI_MODE_SELECT_DP_MST	(3<<24)
#define  TRANS_DDI_MODE_SELECT_FDI	(4<<24)
#define  TRANS_DDI_BPC_MASK		(7<<20)
#define  TRANS_DDI_BPC_8		(0<<20)
#define  TRANS_DDI_BPC_10		(1<<20)
#define  TRANS_DDI_BPC_6		(2<<20)
#define  TRANS_DDI_BPC_12		(3<<20)
#define  TRANS_DDI_PVSYNC		(1<<17)
#define  TRANS_DDI_PHSYNC		(1<<16)
#define  TRANS_DDI_EDP_INPUT_MASK	(7<<12)
#define  TRANS_DDI_EDP_INPUT_A_ON	(0<<12)
#define  TRANS_DDI_EDP_INPUT_A_ONOFF	(4<<12)
#define  TRANS_DDI_EDP_INPUT_B_ONOFF	(5<<12)
#define  TRANS_DDI_EDP_INPUT_C_ONOFF	(6<<12)
#define  TRANS_DDI_DP_VC_PAYLOAD_ALLOC	(1<<8)
#define  TRANS_DDI_BFI_ENABLE		(1<<4)

/* DisplayPort Transport Control */
#define DP_TP_CTL_A			0x64040
#define DP_TP_CTL_B			0x64140
#define DP_TP_CTL(port) _PORT(port, DP_TP_CTL_A, DP_TP_CTL_B)
#define  DP_TP_CTL_ENABLE			(1<<31)
#define  DP_TP_CTL_MODE_SST			(0<<27)
#define  DP_TP_CTL_MODE_MST			(1<<27)
#define  DP_TP_CTL_FORCE_ACT			(1<<25)
#define  DP_TP_CTL_ENHANCED_FRAME_ENABLE	(1<<18)
#define  DP_TP_CTL_FDI_AUTOTRAIN		(1<<15)
#define  DP_TP_CTL_LINK_TRAIN_MASK		(7<<8)
#define  DP_TP_CTL_LINK_TRAIN_PAT1		(0<<8)
#define  DP_TP_CTL_LINK_TRAIN_PAT2		(1<<8)
#define  DP_TP_CTL_LINK_TRAIN_PAT3		(4<<8)
#define  DP_TP_CTL_LINK_TRAIN_IDLE		(2<<8)
#define  DP_TP_CTL_LINK_TRAIN_NORMAL		(3<<8)
#define  DP_TP_CTL_SCRAMBLE_DISABLE		(1<<7)

/* DisplayPort Transport Status */
#define DP_TP_STATUS_A			0x64044
#define DP_TP_STATUS_B			0x64144
#define DP_TP_STATUS(port) _PORT(port, DP_TP_STATUS_A, DP_TP_STATUS_B)
#define  DP_TP_STATUS_IDLE_DONE			(1<<25)
#define  DP_TP_STATUS_ACT_SENT			(1<<24)
#define  DP_TP_STATUS_MODE_STATUS_MST		(1<<23)
#define  DP_TP_STATUS_AUTOTRAIN_DONE		(1<<12)
#define  DP_TP_STATUS_PAYLOAD_MAPPING_VC2	(3 << 8)
#define  DP_TP_STATUS_PAYLOAD_MAPPING_VC1	(3 << 4)
#define  DP_TP_STATUS_PAYLOAD_MAPPING_VC0	(3 << 0)

/* DDI Buffer Control */
#define DDI_BUF_CTL_A				0x64000
#define DDI_BUF_CTL_B				0x64100
#define DDI_BUF_CTL(port) _PORT(port, DDI_BUF_CTL_A, DDI_BUF_CTL_B)
#define  DDI_BUF_CTL_ENABLE			(1<<31)
#define  DDI_BUF_TRANS_SELECT(n)	((n) << 24)
#define  DDI_BUF_EMP_MASK			(0xf<<24)
#define  DDI_BUF_PORT_REVERSAL			(1<<16)
#define  DDI_BUF_IS_IDLE			(1<<7)
#define  DDI_A_4_LANES				(1<<4)
#define  DDI_PORT_WIDTH(width)			(((width) - 1) << 1)
#define  DDI_INIT_DISPLAY_DETECTED		(1<<0)

/* DDI Buffer Translations */
#define DDI_BUF_TRANS_A				0x64E00
#define DDI_BUF_TRANS_B				0x64E60
#define DDI_BUF_TRANS(port) _PORT(port, DDI_BUF_TRANS_A, DDI_BUF_TRANS_B)

/* Sideband Interface (SBI) is programmed indirectly, via
 * SBI_ADDR, which contains the register offset; and SBI_DATA,
 * which contains the payload */
#define SBI_ADDR			0xC6000
#define SBI_DATA			0xC6004
#define SBI_CTL_STAT			0xC6008
#define  SBI_CTL_DEST_ICLK		(0x0<<16)
#define  SBI_CTL_DEST_MPHY		(0x1<<16)
#define  SBI_CTL_OP_IORD		(0x2<<8)
#define  SBI_CTL_OP_IOWR		(0x3<<8)
#define  SBI_CTL_OP_CRRD		(0x6<<8)
#define  SBI_CTL_OP_CRWR		(0x7<<8)
#define  SBI_RESPONSE_FAIL		(0x1<<1)
#define  SBI_RESPONSE_SUCCESS		(0x0<<1)
#define  SBI_BUSY			(0x1<<0)
#define  SBI_READY			(0x0<<0)

/* SBI offsets */
#define  SBI_SSCDIVINTPHASE6			0x0600
#define   SBI_SSCDIVINTPHASE_DIVSEL_MASK	((0x7f)<<1)
#define   SBI_SSCDIVINTPHASE_DIVSEL(x)		((x)<<1)
#define   SBI_SSCDIVINTPHASE_INCVAL_MASK	((0x7f)<<8)
#define   SBI_SSCDIVINTPHASE_INCVAL(x)		((x)<<8)
#define   SBI_SSCDIVINTPHASE_DIR(x)		((x)<<15)
#define   SBI_SSCDIVINTPHASE_PROPAGATE		(1<<0)
#define  SBI_SSCCTL				0x020c
#define  SBI_SSCCTL6				0x060C
#define   SBI_SSCCTL_PATHALT			(1<<3)
#define   SBI_SSCCTL_DISABLE			(1<<0)
#define  SBI_SSCAUXDIV6				0x0610
#define   SBI_SSCAUXDIV_FINALDIV2SEL(x)		((x)<<4)
#define  SBI_DBUFF0				0x2a00
#define  SBI_GEN0				0x1f00
#define   SBI_GEN0_CFG_BUFFENABLE_DISABLE	(1<<0)

/* LPT PIXCLK_GATE */
#define PIXCLK_GATE			0xC6020
#define  PIXCLK_GATE_UNGATE		(1<<0)
#define  PIXCLK_GATE_GATE		(0<<0)

/* SPLL */
#define SPLL_CTL			0x46020
#define  SPLL_PLL_ENABLE		(1<<31)
#define  SPLL_PLL_SSC			(1<<28)
#define  SPLL_PLL_NON_SSC		(2<<28)
#define  SPLL_PLL_LCPLL			(3<<28)
#define  SPLL_PLL_REF_MASK		(3<<28)
#define  SPLL_PLL_FREQ_810MHz		(0<<26)
#define  SPLL_PLL_FREQ_1350MHz		(1<<26)
#define  SPLL_PLL_FREQ_2700MHz		(2<<26)
#define  SPLL_PLL_FREQ_MASK		(3<<26)

/* WRPLL */
#define WRPLL_CTL1			0x46040
#define WRPLL_CTL2			0x46060
#define WRPLL_CTL(pll)			(pll == 0 ? WRPLL_CTL1 : WRPLL_CTL2)
#define  WRPLL_PLL_ENABLE		(1<<31)
#define  WRPLL_PLL_SSC			(1<<28)
#define  WRPLL_PLL_NON_SSC		(2<<28)
#define  WRPLL_PLL_LCPLL		(3<<28)
#define  WRPLL_PLL_REF_MASK		(3<<28)
/* WRPLL divider programming */
#define  WRPLL_DIVIDER_REFERENCE(x)	((x)<<0)
#define  WRPLL_DIVIDER_REF_MASK		(0xff)
#define  WRPLL_DIVIDER_POST(x)		((x)<<8)
#define  WRPLL_DIVIDER_POST_MASK	(0x3f<<8)
#define  WRPLL_DIVIDER_POST_SHIFT	8
#define  WRPLL_DIVIDER_FEEDBACK(x)	((x)<<16)
#define  WRPLL_DIVIDER_FB_SHIFT		16
#define  WRPLL_DIVIDER_FB_MASK		(0xff<<16)

/* Port clock selection */
#define PORT_CLK_SEL_A			0x46100
#define PORT_CLK_SEL_B			0x46104
#define PORT_CLK_SEL(port) _PORT(port, PORT_CLK_SEL_A, PORT_CLK_SEL_B)
#define  PORT_CLK_SEL_LCPLL_2700	(0<<29)
#define  PORT_CLK_SEL_LCPLL_1350	(1<<29)
#define  PORT_CLK_SEL_LCPLL_810		(2<<29)
#define  PORT_CLK_SEL_SPLL		(3<<29)
#define  PORT_CLK_SEL_WRPLL(pll)	(((pll)+4)<<29)
#define  PORT_CLK_SEL_WRPLL1		(4<<29)
#define  PORT_CLK_SEL_WRPLL2		(5<<29)
#define  PORT_CLK_SEL_NONE		(7<<29)
#define  PORT_CLK_SEL_MASK		(7<<29)

/* Transcoder clock selection */
#define TRANS_CLK_SEL_A			0x46140
#define TRANS_CLK_SEL_B			0x46144
#define TRANS_CLK_SEL(tran) _TRANSCODER(tran, TRANS_CLK_SEL_A, TRANS_CLK_SEL_B)
/* For each transcoder, we need to select the corresponding port clock */
#define  TRANS_CLK_SEL_DISABLED		(0x0<<29)
#define  TRANS_CLK_SEL_PORT(x)		((x+1)<<29)

#define TRANSA_MSA_MISC			0x60410
#define TRANSB_MSA_MISC			0x61410
#define TRANSC_MSA_MISC			0x62410
#define TRANS_EDP_MSA_MISC		0x6f410
#define TRANS_MSA_MISC(tran) _TRANSCODER2(tran, TRANSA_MSA_MISC)

#define  TRANS_MSA_SYNC_CLK		(1<<0)
#define  TRANS_MSA_6_BPC		(0<<5)
#define  TRANS_MSA_8_BPC		(1<<5)
#define  TRANS_MSA_10_BPC		(2<<5)
#define  TRANS_MSA_12_BPC		(3<<5)
#define  TRANS_MSA_16_BPC		(4<<5)

/* LCPLL Control */
#define LCPLL_CTL			0x130040
#define  LCPLL_PLL_DISABLE		(1<<31)
#define  LCPLL_PLL_LOCK			(1<<30)
#define  LCPLL_CLK_FREQ_MASK		(3<<26)
#define  LCPLL_CLK_FREQ_450		(0<<26)
#define  LCPLL_CLK_FREQ_54O_BDW		(1<<26)
#define  LCPLL_CLK_FREQ_337_5_BDW	(2<<26)
#define  LCPLL_CLK_FREQ_675_BDW		(3<<26)
#define  LCPLL_CD_CLOCK_DISABLE		(1<<25)
#define  LCPLL_CD2X_CLOCK_DISABLE	(1<<23)
#define  LCPLL_POWER_DOWN_ALLOW		(1<<22)
#define  LCPLL_CD_SOURCE_FCLK		(1<<21)
#define  LCPLL_CD_SOURCE_FCLK_DONE	(1<<19)

/*
 * SKL Clocks
 */

/* CDCLK_CTL */
#define CDCLK_CTL			0x46000
#define  CDCLK_FREQ_SEL_MASK		(3<<26)
#define  CDCLK_FREQ_450_432		(0<<26)
#define  CDCLK_FREQ_540			(1<<26)
#define  CDCLK_FREQ_337_308		(2<<26)
#define  CDCLK_FREQ_675_617		(3<<26)
#define  CDCLK_FREQ_DECIMAL_MASK	(0x7ff)

/* LCPLL_CTL */
#define LCPLL1_CTL		0x46010
#define LCPLL2_CTL		0x46014
#define  LCPLL_PLL_ENABLE	(1<<31)

/* DPLL control1 */
#define DPLL_CTRL1		0x6C058
#define  DPLL_CTRL1_HDMI_MODE(id)		(1<<((id)*6+5))
#define  DPLL_CTRL1_SSC(id)			(1<<((id)*6+4))
#define  DPLL_CRTL1_LINK_RATE_MASK(id)		(7<<((id)*6+1))
#define  DPLL_CRTL1_LINK_RATE_SHIFT(id)		((id)*6+1)
#define  DPLL_CRTL1_LINK_RATE(linkrate, id)	((linkrate)<<((id)*6+1))
#define  DPLL_CTRL1_OVERRIDE(id)		(1<<((id)*6))
#define  DPLL_CRTL1_LINK_RATE_2700		0
#define  DPLL_CRTL1_LINK_RATE_1350		1
#define  DPLL_CRTL1_LINK_RATE_810		2
#define  DPLL_CRTL1_LINK_RATE_1620		3
#define  DPLL_CRTL1_LINK_RATE_1080		4
#define  DPLL_CRTL1_LINK_RATE_2160		5

/* DPLL control2 */
#define DPLL_CTRL2				0x6C05C
#define  DPLL_CTRL2_DDI_CLK_OFF(port)		(1<<(port+15))
#define  DPLL_CTRL2_DDI_CLK_SEL_MASK(port)	(3<<((port)*3+1))
#define  DPLL_CTRL2_DDI_CLK_SEL_SHIFT(port)    ((port)*3+1)
#define  DPLL_CTRL2_DDI_CLK_SEL(clk, port)	(clk<<((port)*3+1))
#define  DPLL_CTRL2_DDI_SEL_OVERRIDE(port)     (1<<((port)*3))

/* DPLL Status */
#define DPLL_STATUS	0x6C060
#define  DPLL_LOCK(id) (1<<((id)*8))

/* DPLL cfg */
#define DPLL1_CFGCR1	0x6C040
#define DPLL2_CFGCR1	0x6C048
#define DPLL3_CFGCR1	0x6C050
#define  DPLL_CFGCR1_FREQ_ENABLE	(1<<31)
#define  DPLL_CFGCR1_DCO_FRACTION_MASK	(0x7fff<<9)
#define  DPLL_CFGCR1_DCO_FRACTION(x)	(x<<9)
#define  DPLL_CFGCR1_DCO_INTEGER_MASK	(0x1ff)

#define DPLL1_CFGCR2	0x6C044
#define DPLL2_CFGCR2	0x6C04C
#define DPLL3_CFGCR2	0x6C054
#define  DPLL_CFGCR2_QDIV_RATIO_MASK	(0xff<<8)
#define  DPLL_CFGCR2_QDIV_RATIO(x)	(x<<8)
#define  DPLL_CFGCR2_QDIV_MODE(x)	(x<<7)
#define  DPLL_CFGCR2_KDIV_MASK		(3<<5)
#define  DPLL_CFGCR2_KDIV(x)		(x<<5)
#define  DPLL_CFGCR2_KDIV_5 (0<<5)
#define  DPLL_CFGCR2_KDIV_2 (1<<5)
#define  DPLL_CFGCR2_KDIV_3 (2<<5)
#define  DPLL_CFGCR2_KDIV_1 (3<<5)
#define  DPLL_CFGCR2_PDIV_MASK		(7<<2)
#define  DPLL_CFGCR2_PDIV(x)		(x<<2)
#define  DPLL_CFGCR2_PDIV_1 (0<<2)
#define  DPLL_CFGCR2_PDIV_2 (1<<2)
#define  DPLL_CFGCR2_PDIV_3 (2<<2)
#define  DPLL_CFGCR2_PDIV_7 (4<<2)
#define  DPLL_CFGCR2_CENTRAL_FREQ_MASK	(3)

#define GET_CFG_CR1_REG(id) (DPLL1_CFGCR1 + (id - SKL_DPLL1) * 8)
#define GET_CFG_CR2_REG(id) (DPLL1_CFGCR2 + (id - SKL_DPLL1) * 8)

/* Please see hsw_read_dcomp() and hsw_write_dcomp() before using this register,
 * since on HSW we can't write to it using I915_WRITE. */
#define D_COMP_HSW			(MCHBAR_MIRROR_BASE_SNB + 0x5F0C)
#define D_COMP_BDW			0x138144
#define  D_COMP_RCOMP_IN_PROGRESS	(1<<9)
#define  D_COMP_COMP_FORCE		(1<<8)
#define  D_COMP_COMP_DISABLE		(1<<0)

/* Pipe WM_LINETIME - watermark line time */
#define PIPE_WM_LINETIME_A		0x45270
#define PIPE_WM_LINETIME_B		0x45274
#define PIPE_WM_LINETIME(pipe) _PIPE(pipe, PIPE_WM_LINETIME_A, \
					   PIPE_WM_LINETIME_B)
#define   PIPE_WM_LINETIME_MASK			(0x1ff)
#define   PIPE_WM_LINETIME_TIME(x)		((x))
#define   PIPE_WM_LINETIME_IPS_LINETIME_MASK	(0x1ff<<16)
#define   PIPE_WM_LINETIME_IPS_LINETIME(x)	((x)<<16)

/* SFUSE_STRAP */
#define SFUSE_STRAP			0xc2014
#define  SFUSE_STRAP_FUSE_LOCK		(1<<13)
#define  SFUSE_STRAP_DISPLAY_DISABLED	(1<<7)
#define  SFUSE_STRAP_DDIB_DETECTED	(1<<2)
#define  SFUSE_STRAP_DDIC_DETECTED	(1<<1)
#define  SFUSE_STRAP_DDID_DETECTED	(1<<0)

#define WM_MISC				0x45260
#define  WM_MISC_DATA_PARTITION_5_6	(1 << 0)

#define WM_DBG				0x45280
#define  WM_DBG_DISALLOW_MULTIPLE_LP	(1<<0)
#define  WM_DBG_DISALLOW_MAXFIFO	(1<<1)
#define  WM_DBG_DISALLOW_SPRITE		(1<<2)

/* pipe CSC */
#define _PIPE_A_CSC_COEFF_RY_GY	0x49010
#define _PIPE_A_CSC_COEFF_BY	0x49014
#define _PIPE_A_CSC_COEFF_RU_GU	0x49018
#define _PIPE_A_CSC_COEFF_BU	0x4901c
#define _PIPE_A_CSC_COEFF_RV_GV	0x49020
#define _PIPE_A_CSC_COEFF_BV	0x49024
#define _PIPE_A_CSC_MODE	0x49028
#define   CSC_BLACK_SCREEN_OFFSET	(1 << 2)
#define   CSC_POSITION_BEFORE_GAMMA	(1 << 1)
#define   CSC_MODE_YUV_TO_RGB		(1 << 0)
#define _PIPE_A_CSC_PREOFF_HI	0x49030
#define _PIPE_A_CSC_PREOFF_ME	0x49034
#define _PIPE_A_CSC_PREOFF_LO	0x49038
#define _PIPE_A_CSC_POSTOFF_HI	0x49040
#define _PIPE_A_CSC_POSTOFF_ME	0x49044
#define _PIPE_A_CSC_POSTOFF_LO	0x49048

#define _PIPE_B_CSC_COEFF_RY_GY	0x49110
#define _PIPE_B_CSC_COEFF_BY	0x49114
#define _PIPE_B_CSC_COEFF_RU_GU	0x49118
#define _PIPE_B_CSC_COEFF_BU	0x4911c
#define _PIPE_B_CSC_COEFF_RV_GV	0x49120
#define _PIPE_B_CSC_COEFF_BV	0x49124
#define _PIPE_B_CSC_MODE	0x49128
#define _PIPE_B_CSC_PREOFF_HI	0x49130
#define _PIPE_B_CSC_PREOFF_ME	0x49134
#define _PIPE_B_CSC_PREOFF_LO	0x49138
#define _PIPE_B_CSC_POSTOFF_HI	0x49140
#define _PIPE_B_CSC_POSTOFF_ME	0x49144
#define _PIPE_B_CSC_POSTOFF_LO	0x49148

#define PIPE_CSC_COEFF_RY_GY(pipe) _PIPE(pipe, _PIPE_A_CSC_COEFF_RY_GY, _PIPE_B_CSC_COEFF_RY_GY)
#define PIPE_CSC_COEFF_BY(pipe) _PIPE(pipe, _PIPE_A_CSC_COEFF_BY, _PIPE_B_CSC_COEFF_BY)
#define PIPE_CSC_COEFF_RU_GU(pipe) _PIPE(pipe, _PIPE_A_CSC_COEFF_RU_GU, _PIPE_B_CSC_COEFF_RU_GU)
#define PIPE_CSC_COEFF_BU(pipe) _PIPE(pipe, _PIPE_A_CSC_COEFF_BU, _PIPE_B_CSC_COEFF_BU)
#define PIPE_CSC_COEFF_RV_GV(pipe) _PIPE(pipe, _PIPE_A_CSC_COEFF_RV_GV, _PIPE_B_CSC_COEFF_RV_GV)
#define PIPE_CSC_COEFF_BV(pipe) _PIPE(pipe, _PIPE_A_CSC_COEFF_BV, _PIPE_B_CSC_COEFF_BV)
#define PIPE_CSC_MODE(pipe) _PIPE(pipe, _PIPE_A_CSC_MODE, _PIPE_B_CSC_MODE)
#define PIPE_CSC_PREOFF_HI(pipe) _PIPE(pipe, _PIPE_A_CSC_PREOFF_HI, _PIPE_B_CSC_PREOFF_HI)
#define PIPE_CSC_PREOFF_ME(pipe) _PIPE(pipe, _PIPE_A_CSC_PREOFF_ME, _PIPE_B_CSC_PREOFF_ME)
#define PIPE_CSC_PREOFF_LO(pipe) _PIPE(pipe, _PIPE_A_CSC_PREOFF_LO, _PIPE_B_CSC_PREOFF_LO)
#define PIPE_CSC_POSTOFF_HI(pipe) _PIPE(pipe, _PIPE_A_CSC_POSTOFF_HI, _PIPE_B_CSC_POSTOFF_HI)
#define PIPE_CSC_POSTOFF_ME(pipe) _PIPE(pipe, _PIPE_A_CSC_POSTOFF_ME, _PIPE_B_CSC_POSTOFF_ME)
#define PIPE_CSC_POSTOFF_LO(pipe) _PIPE(pipe, _PIPE_A_CSC_POSTOFF_LO, _PIPE_B_CSC_POSTOFF_LO)

/* MIPI DSI registers */

#define _MIPI_PORT(port, a, c)	_PORT3(port, a, 0, c)	/* ports A and C only */

#define _MIPIA_PORT_CTRL			(VLV_DISPLAY_BASE + 0x61190)
#define _MIPIC_PORT_CTRL			(VLV_DISPLAY_BASE + 0x61700)
#define MIPI_PORT_CTRL(port)	_MIPI_PORT(port, _MIPIA_PORT_CTRL, _MIPIC_PORT_CTRL)
#define  DPI_ENABLE					(1 << 31) /* A + C */
#define  MIPIA_MIPI4DPHY_DELAY_COUNT_SHIFT		27
#define  MIPIA_MIPI4DPHY_DELAY_COUNT_MASK		(0xf << 27)
#define  DUAL_LINK_MODE_SHIFT				26
#define  DUAL_LINK_MODE_MASK				(1 << 26)
#define  DUAL_LINK_MODE_FRONT_BACK			(0 << 26)
#define  DUAL_LINK_MODE_PIXEL_ALTERNATIVE		(1 << 26)
#define  DITHERING_ENABLE				(1 << 25) /* A + C */
#define  FLOPPED_HSTX					(1 << 23)
#define  DE_INVERT					(1 << 19) /* XXX */
#define  MIPIA_FLISDSI_DELAY_COUNT_SHIFT		18
#define  MIPIA_FLISDSI_DELAY_COUNT_MASK			(0xf << 18)
#define  AFE_LATCHOUT					(1 << 17)
#define  LP_OUTPUT_HOLD					(1 << 16)
#define  MIPIC_FLISDSI_DELAY_COUNT_HIGH_SHIFT		15
#define  MIPIC_FLISDSI_DELAY_COUNT_HIGH_MASK		(1 << 15)
#define  MIPIC_MIPI4DPHY_DELAY_COUNT_SHIFT		11
#define  MIPIC_MIPI4DPHY_DELAY_COUNT_MASK		(0xf << 11)
#define  CSB_SHIFT					9
#define  CSB_MASK					(3 << 9)
#define  CSB_20MHZ					(0 << 9)
#define  CSB_10MHZ					(1 << 9)
#define  CSB_40MHZ					(2 << 9)
#define  BANDGAP_MASK					(1 << 8)
#define  BANDGAP_PNW_CIRCUIT				(0 << 8)
#define  BANDGAP_LNC_CIRCUIT				(1 << 8)
#define  MIPIC_FLISDSI_DELAY_COUNT_LOW_SHIFT		5
#define  MIPIC_FLISDSI_DELAY_COUNT_LOW_MASK		(7 << 5)
#define  TEARING_EFFECT_DELAY				(1 << 4) /* A + C */
#define  TEARING_EFFECT_SHIFT				2 /* A + C */
#define  TEARING_EFFECT_MASK				(3 << 2)
#define  TEARING_EFFECT_OFF				(0 << 2)
#define  TEARING_EFFECT_DSI				(1 << 2)
#define  TEARING_EFFECT_GPIO				(2 << 2)
#define  LANE_CONFIGURATION_SHIFT			0
#define  LANE_CONFIGURATION_MASK			(3 << 0)
#define  LANE_CONFIGURATION_4LANE			(0 << 0)
#define  LANE_CONFIGURATION_DUAL_LINK_A			(1 << 0)
#define  LANE_CONFIGURATION_DUAL_LINK_B			(2 << 0)

#define _MIPIA_TEARING_CTRL			(VLV_DISPLAY_BASE + 0x61194)
#define _MIPIC_TEARING_CTRL			(VLV_DISPLAY_BASE + 0x61704)
#define MIPI_TEARING_CTRL(port)			_MIPI_PORT(port, \
				_MIPIA_TEARING_CTRL, _MIPIC_TEARING_CTRL)
#define  TEARING_EFFECT_DELAY_SHIFT			0
#define  TEARING_EFFECT_DELAY_MASK			(0xffff << 0)

/* XXX: all bits reserved */
#define _MIPIA_AUTOPWG			(VLV_DISPLAY_BASE + 0x611a0)

/* MIPI DSI Controller and D-PHY registers */

#define _MIPIA_DEVICE_READY		(dev_priv->mipi_mmio_base + 0xb000)
#define _MIPIC_DEVICE_READY		(dev_priv->mipi_mmio_base + 0xb800)
#define MIPI_DEVICE_READY(port)		_MIPI_PORT(port, _MIPIA_DEVICE_READY, \
						_MIPIC_DEVICE_READY)
#define  BUS_POSSESSION					(1 << 3) /* set to give bus to receiver */
#define  ULPS_STATE_MASK				(3 << 1)
#define  ULPS_STATE_ENTER				(2 << 1)
#define  ULPS_STATE_EXIT				(1 << 1)
#define  ULPS_STATE_NORMAL_OPERATION			(0 << 1)
#define  DEVICE_READY					(1 << 0)

#define _MIPIA_INTR_STAT		(dev_priv->mipi_mmio_base + 0xb004)
#define _MIPIC_INTR_STAT		(dev_priv->mipi_mmio_base + 0xb804)
#define MIPI_INTR_STAT(port)		_MIPI_PORT(port, _MIPIA_INTR_STAT, \
					_MIPIC_INTR_STAT)
#define _MIPIA_INTR_EN			(dev_priv->mipi_mmio_base + 0xb008)
#define _MIPIC_INTR_EN			(dev_priv->mipi_mmio_base + 0xb808)
#define MIPI_INTR_EN(port)		_MIPI_PORT(port, _MIPIA_INTR_EN, \
					_MIPIC_INTR_EN)
#define  TEARING_EFFECT					(1 << 31)
#define  SPL_PKT_SENT_INTERRUPT				(1 << 30)
#define  GEN_READ_DATA_AVAIL				(1 << 29)
#define  LP_GENERIC_WR_FIFO_FULL			(1 << 28)
#define  HS_GENERIC_WR_FIFO_FULL			(1 << 27)
#define  RX_PROT_VIOLATION				(1 << 26)
#define  RX_INVALID_TX_LENGTH				(1 << 25)
#define  ACK_WITH_NO_ERROR				(1 << 24)
#define  TURN_AROUND_ACK_TIMEOUT			(1 << 23)
#define  LP_RX_TIMEOUT					(1 << 22)
#define  HS_TX_TIMEOUT					(1 << 21)
#define  DPI_FIFO_UNDERRUN				(1 << 20)
#define  LOW_CONTENTION					(1 << 19)
#define  HIGH_CONTENTION				(1 << 18)
#define  TXDSI_VC_ID_INVALID				(1 << 17)
#define  TXDSI_DATA_TYPE_NOT_RECOGNISED			(1 << 16)
#define  TXCHECKSUM_ERROR				(1 << 15)
#define  TXECC_MULTIBIT_ERROR				(1 << 14)
#define  TXECC_SINGLE_BIT_ERROR				(1 << 13)
#define  TXFALSE_CONTROL_ERROR				(1 << 12)
#define  RXDSI_VC_ID_INVALID				(1 << 11)
#define  RXDSI_DATA_TYPE_NOT_REGOGNISED			(1 << 10)
#define  RXCHECKSUM_ERROR				(1 << 9)
#define  RXECC_MULTIBIT_ERROR				(1 << 8)
#define  RXECC_SINGLE_BIT_ERROR				(1 << 7)
#define  RXFALSE_CONTROL_ERROR				(1 << 6)
#define  RXHS_RECEIVE_TIMEOUT_ERROR			(1 << 5)
#define  RX_LP_TX_SYNC_ERROR				(1 << 4)
#define  RXEXCAPE_MODE_ENTRY_ERROR			(1 << 3)
#define  RXEOT_SYNC_ERROR				(1 << 2)
#define  RXSOT_SYNC_ERROR				(1 << 1)
#define  RXSOT_ERROR					(1 << 0)

#define _MIPIA_DSI_FUNC_PRG		(dev_priv->mipi_mmio_base + 0xb00c)
#define _MIPIC_DSI_FUNC_PRG		(dev_priv->mipi_mmio_base + 0xb80c)
#define MIPI_DSI_FUNC_PRG(port)		_MIPI_PORT(port, _MIPIA_DSI_FUNC_PRG, \
						_MIPIC_DSI_FUNC_PRG)
#define  CMD_MODE_DATA_WIDTH_MASK			(7 << 13)
#define  CMD_MODE_NOT_SUPPORTED				(0 << 13)
#define  CMD_MODE_DATA_WIDTH_16_BIT			(1 << 13)
#define  CMD_MODE_DATA_WIDTH_9_BIT			(2 << 13)
#define  CMD_MODE_DATA_WIDTH_8_BIT			(3 << 13)
#define  CMD_MODE_DATA_WIDTH_OPTION1			(4 << 13)
#define  CMD_MODE_DATA_WIDTH_OPTION2			(5 << 13)
#define  VID_MODE_FORMAT_MASK				(0xf << 7)
#define  VID_MODE_NOT_SUPPORTED				(0 << 7)
#define  VID_MODE_FORMAT_RGB565				(1 << 7)
#define  VID_MODE_FORMAT_RGB666				(2 << 7)
#define  VID_MODE_FORMAT_RGB666_LOOSE			(3 << 7)
#define  VID_MODE_FORMAT_RGB888				(4 << 7)
#define  CMD_MODE_CHANNEL_NUMBER_SHIFT			5
#define  CMD_MODE_CHANNEL_NUMBER_MASK			(3 << 5)
#define  VID_MODE_CHANNEL_NUMBER_SHIFT			3
#define  VID_MODE_CHANNEL_NUMBER_MASK			(3 << 3)
#define  DATA_LANES_PRG_REG_SHIFT			0
#define  DATA_LANES_PRG_REG_MASK			(7 << 0)

#define _MIPIA_HS_TX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb010)
#define _MIPIC_HS_TX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb810)
#define MIPI_HS_TX_TIMEOUT(port)	_MIPI_PORT(port, _MIPIA_HS_TX_TIMEOUT, \
					_MIPIC_HS_TX_TIMEOUT)
#define  HIGH_SPEED_TX_TIMEOUT_COUNTER_MASK		0xffffff

#define _MIPIA_LP_RX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb014)
#define _MIPIC_LP_RX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb814)
#define MIPI_LP_RX_TIMEOUT(port)	_MIPI_PORT(port, _MIPIA_LP_RX_TIMEOUT, \
					_MIPIC_LP_RX_TIMEOUT)
#define  LOW_POWER_RX_TIMEOUT_COUNTER_MASK		0xffffff

#define _MIPIA_TURN_AROUND_TIMEOUT	(dev_priv->mipi_mmio_base + 0xb018)
#define _MIPIC_TURN_AROUND_TIMEOUT	(dev_priv->mipi_mmio_base + 0xb818)
#define MIPI_TURN_AROUND_TIMEOUT(port)	_MIPI_PORT(port, \
			_MIPIA_TURN_AROUND_TIMEOUT, _MIPIC_TURN_AROUND_TIMEOUT)
#define  TURN_AROUND_TIMEOUT_MASK			0x3f

#define _MIPIA_DEVICE_RESET_TIMER	(dev_priv->mipi_mmio_base + 0xb01c)
#define _MIPIC_DEVICE_RESET_TIMER	(dev_priv->mipi_mmio_base + 0xb81c)
#define MIPI_DEVICE_RESET_TIMER(port)	_MIPI_PORT(port, \
			_MIPIA_DEVICE_RESET_TIMER, _MIPIC_DEVICE_RESET_TIMER)
#define  DEVICE_RESET_TIMER_MASK			0xffff

#define _MIPIA_DPI_RESOLUTION		(dev_priv->mipi_mmio_base + 0xb020)
#define _MIPIC_DPI_RESOLUTION		(dev_priv->mipi_mmio_base + 0xb820)
#define MIPI_DPI_RESOLUTION(port)	_MIPI_PORT(port, _MIPIA_DPI_RESOLUTION, \
					_MIPIC_DPI_RESOLUTION)
#define  VERTICAL_ADDRESS_SHIFT				16
#define  VERTICAL_ADDRESS_MASK				(0xffff << 16)
#define  HORIZONTAL_ADDRESS_SHIFT			0
#define  HORIZONTAL_ADDRESS_MASK			0xffff

#define _MIPIA_DBI_FIFO_THROTTLE	(dev_priv->mipi_mmio_base + 0xb024)
#define _MIPIC_DBI_FIFO_THROTTLE	(dev_priv->mipi_mmio_base + 0xb824)
#define MIPI_DBI_FIFO_THROTTLE(port)	_MIPI_PORT(port, \
			_MIPIA_DBI_FIFO_THROTTLE, _MIPIC_DBI_FIFO_THROTTLE)
#define  DBI_FIFO_EMPTY_HALF				(0 << 0)
#define  DBI_FIFO_EMPTY_QUARTER				(1 << 0)
#define  DBI_FIFO_EMPTY_7_LOCATIONS			(2 << 0)

/* regs below are bits 15:0 */
#define _MIPIA_HSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb028)
#define _MIPIC_HSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb828)
#define MIPI_HSYNC_PADDING_COUNT(port)	_MIPI_PORT(port, \
			_MIPIA_HSYNC_PADDING_COUNT, _MIPIC_HSYNC_PADDING_COUNT)

#define _MIPIA_HBP_COUNT		(dev_priv->mipi_mmio_base + 0xb02c)
#define _MIPIC_HBP_COUNT		(dev_priv->mipi_mmio_base + 0xb82c)
#define MIPI_HBP_COUNT(port)		_MIPI_PORT(port, _MIPIA_HBP_COUNT, \
					_MIPIC_HBP_COUNT)

#define _MIPIA_HFP_COUNT		(dev_priv->mipi_mmio_base + 0xb030)
#define _MIPIC_HFP_COUNT		(dev_priv->mipi_mmio_base + 0xb830)
#define MIPI_HFP_COUNT(port)		_MIPI_PORT(port, _MIPIA_HFP_COUNT, \
					_MIPIC_HFP_COUNT)

#define _MIPIA_HACTIVE_AREA_COUNT	(dev_priv->mipi_mmio_base + 0xb034)
#define _MIPIC_HACTIVE_AREA_COUNT	(dev_priv->mipi_mmio_base + 0xb834)
#define MIPI_HACTIVE_AREA_COUNT(port)	_MIPI_PORT(port, \
			_MIPIA_HACTIVE_AREA_COUNT, _MIPIC_HACTIVE_AREA_COUNT)

#define _MIPIA_VSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb038)
#define _MIPIC_VSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb838)
#define MIPI_VSYNC_PADDING_COUNT(port)	_MIPI_PORT(port, \
			_MIPIA_VSYNC_PADDING_COUNT, _MIPIC_VSYNC_PADDING_COUNT)

#define _MIPIA_VBP_COUNT		(dev_priv->mipi_mmio_base + 0xb03c)
#define _MIPIC_VBP_COUNT		(dev_priv->mipi_mmio_base + 0xb83c)
#define MIPI_VBP_COUNT(port)		_MIPI_PORT(port, _MIPIA_VBP_COUNT, \
					_MIPIC_VBP_COUNT)

#define _MIPIA_VFP_COUNT		(dev_priv->mipi_mmio_base + 0xb040)
#define _MIPIC_VFP_COUNT		(dev_priv->mipi_mmio_base + 0xb840)
#define MIPI_VFP_COUNT(port)		_MIPI_PORT(port, _MIPIA_VFP_COUNT, \
					_MIPIC_VFP_COUNT)

#define _MIPIA_HIGH_LOW_SWITCH_COUNT	(dev_priv->mipi_mmio_base + 0xb044)
#define _MIPIC_HIGH_LOW_SWITCH_COUNT	(dev_priv->mipi_mmio_base + 0xb844)
#define MIPI_HIGH_LOW_SWITCH_COUNT(port)	_MIPI_PORT(port,	\
		_MIPIA_HIGH_LOW_SWITCH_COUNT, _MIPIC_HIGH_LOW_SWITCH_COUNT)

/* regs above are bits 15:0 */

#define _MIPIA_DPI_CONTROL		(dev_priv->mipi_mmio_base + 0xb048)
#define _MIPIC_DPI_CONTROL		(dev_priv->mipi_mmio_base + 0xb848)
#define MIPI_DPI_CONTROL(port)		_MIPI_PORT(port, _MIPIA_DPI_CONTROL, \
					_MIPIC_DPI_CONTROL)
#define  DPI_LP_MODE					(1 << 6)
#define  BACKLIGHT_OFF					(1 << 5)
#define  BACKLIGHT_ON					(1 << 4)
#define  COLOR_MODE_OFF					(1 << 3)
#define  COLOR_MODE_ON					(1 << 2)
#define  TURN_ON					(1 << 1)
#define  SHUTDOWN					(1 << 0)

#define _MIPIA_DPI_DATA			(dev_priv->mipi_mmio_base + 0xb04c)
#define _MIPIC_DPI_DATA			(dev_priv->mipi_mmio_base + 0xb84c)
#define MIPI_DPI_DATA(port)		_MIPI_PORT(port, _MIPIA_DPI_DATA, \
					_MIPIC_DPI_DATA)
#define  COMMAND_BYTE_SHIFT				0
#define  COMMAND_BYTE_MASK				(0x3f << 0)

#define _MIPIA_INIT_COUNT		(dev_priv->mipi_mmio_base + 0xb050)
#define _MIPIC_INIT_COUNT		(dev_priv->mipi_mmio_base + 0xb850)
#define MIPI_INIT_COUNT(port)		_MIPI_PORT(port, _MIPIA_INIT_COUNT, \
					_MIPIC_INIT_COUNT)
#define  MASTER_INIT_TIMER_SHIFT			0
#define  MASTER_INIT_TIMER_MASK				(0xffff << 0)

#define _MIPIA_MAX_RETURN_PKT_SIZE	(dev_priv->mipi_mmio_base + 0xb054)
#define _MIPIC_MAX_RETURN_PKT_SIZE	(dev_priv->mipi_mmio_base + 0xb854)
#define MIPI_MAX_RETURN_PKT_SIZE(port)	_MIPI_PORT(port, \
			_MIPIA_MAX_RETURN_PKT_SIZE, _MIPIC_MAX_RETURN_PKT_SIZE)
#define  MAX_RETURN_PKT_SIZE_SHIFT			0
#define  MAX_RETURN_PKT_SIZE_MASK			(0x3ff << 0)

#define _MIPIA_VIDEO_MODE_FORMAT	(dev_priv->mipi_mmio_base + 0xb058)
#define _MIPIC_VIDEO_MODE_FORMAT	(dev_priv->mipi_mmio_base + 0xb858)
#define MIPI_VIDEO_MODE_FORMAT(port)	_MIPI_PORT(port, \
			_MIPIA_VIDEO_MODE_FORMAT, _MIPIC_VIDEO_MODE_FORMAT)
#define  RANDOM_DPI_DISPLAY_RESOLUTION			(1 << 4)
#define  DISABLE_VIDEO_BTA				(1 << 3)
#define  IP_TG_CONFIG					(1 << 2)
#define  VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE		(1 << 0)
#define  VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS		(2 << 0)
#define  VIDEO_MODE_BURST				(3 << 0)

#define _MIPIA_EOT_DISABLE		(dev_priv->mipi_mmio_base + 0xb05c)
#define _MIPIC_EOT_DISABLE		(dev_priv->mipi_mmio_base + 0xb85c)
#define MIPI_EOT_DISABLE(port)		_MIPI_PORT(port, _MIPIA_EOT_DISABLE, \
					_MIPIC_EOT_DISABLE)
#define  LP_RX_TIMEOUT_ERROR_RECOVERY_DISABLE		(1 << 7)
#define  HS_RX_TIMEOUT_ERROR_RECOVERY_DISABLE		(1 << 6)
#define  LOW_CONTENTION_RECOVERY_DISABLE		(1 << 5)
#define  HIGH_CONTENTION_RECOVERY_DISABLE		(1 << 4)
#define  TXDSI_TYPE_NOT_RECOGNISED_ERROR_RECOVERY_DISABLE (1 << 3)
#define  TXECC_MULTIBIT_ERROR_RECOVERY_DISABLE		(1 << 2)
#define  CLOCKSTOP					(1 << 1)
#define  EOT_DISABLE					(1 << 0)

#define _MIPIA_LP_BYTECLK		(dev_priv->mipi_mmio_base + 0xb060)
#define _MIPIC_LP_BYTECLK		(dev_priv->mipi_mmio_base + 0xb860)
#define MIPI_LP_BYTECLK(port)		_MIPI_PORT(port, _MIPIA_LP_BYTECLK, \
					_MIPIC_LP_BYTECLK)
#define  LP_BYTECLK_SHIFT				0
#define  LP_BYTECLK_MASK				(0xffff << 0)

/* bits 31:0 */
#define _MIPIA_LP_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb064)
#define _MIPIC_LP_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb864)
#define MIPI_LP_GEN_DATA(port)		_MIPI_PORT(port, _MIPIA_LP_GEN_DATA, \
					_MIPIC_LP_GEN_DATA)

/* bits 31:0 */
#define _MIPIA_HS_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb068)
#define _MIPIC_HS_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb868)
#define MIPI_HS_GEN_DATA(port)		_MIPI_PORT(port, _MIPIA_HS_GEN_DATA, \
					_MIPIC_HS_GEN_DATA)

#define _MIPIA_LP_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb06c)
#define _MIPIC_LP_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb86c)
#define MIPI_LP_GEN_CTRL(port)		_MIPI_PORT(port, _MIPIA_LP_GEN_CTRL, \
					_MIPIC_LP_GEN_CTRL)
#define _MIPIA_HS_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb070)
#define _MIPIC_HS_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb870)
#define MIPI_HS_GEN_CTRL(port)		_MIPI_PORT(port, _MIPIA_HS_GEN_CTRL, \
					_MIPIC_HS_GEN_CTRL)
#define  LONG_PACKET_WORD_COUNT_SHIFT			8
#define  LONG_PACKET_WORD_COUNT_MASK			(0xffff << 8)
#define  SHORT_PACKET_PARAM_SHIFT			8
#define  SHORT_PACKET_PARAM_MASK			(0xffff << 8)
#define  VIRTUAL_CHANNEL_SHIFT				6
#define  VIRTUAL_CHANNEL_MASK				(3 << 6)
#define  DATA_TYPE_SHIFT				0
#define  DATA_TYPE_MASK					(3f << 0)
/* data type values, see include/video/mipi_display.h */

#define _MIPIA_GEN_FIFO_STAT		(dev_priv->mipi_mmio_base + 0xb074)
#define _MIPIC_GEN_FIFO_STAT		(dev_priv->mipi_mmio_base + 0xb874)
#define MIPI_GEN_FIFO_STAT(port)	_MIPI_PORT(port, _MIPIA_GEN_FIFO_STAT, \
					_MIPIC_GEN_FIFO_STAT)
#define  DPI_FIFO_EMPTY					(1 << 28)
#define  DBI_FIFO_EMPTY					(1 << 27)
#define  LP_CTRL_FIFO_EMPTY				(1 << 26)
#define  LP_CTRL_FIFO_HALF_EMPTY			(1 << 25)
#define  LP_CTRL_FIFO_FULL				(1 << 24)
#define  HS_CTRL_FIFO_EMPTY				(1 << 18)
#define  HS_CTRL_FIFO_HALF_EMPTY			(1 << 17)
#define  HS_CTRL_FIFO_FULL				(1 << 16)
#define  LP_DATA_FIFO_EMPTY				(1 << 10)
#define  LP_DATA_FIFO_HALF_EMPTY			(1 << 9)
#define  LP_DATA_FIFO_FULL				(1 << 8)
#define  HS_DATA_FIFO_EMPTY				(1 << 2)
#define  HS_DATA_FIFO_HALF_EMPTY			(1 << 1)
#define  HS_DATA_FIFO_FULL				(1 << 0)

#define _MIPIA_HS_LS_DBI_ENABLE		(dev_priv->mipi_mmio_base + 0xb078)
#define _MIPIC_HS_LS_DBI_ENABLE		(dev_priv->mipi_mmio_base + 0xb878)
#define MIPI_HS_LP_DBI_ENABLE(port)	_MIPI_PORT(port, \
			_MIPIA_HS_LS_DBI_ENABLE, _MIPIC_HS_LS_DBI_ENABLE)
#define  DBI_HS_LP_MODE_MASK				(1 << 0)
#define  DBI_LP_MODE					(1 << 0)
#define  DBI_HS_MODE					(0 << 0)

#define _MIPIA_DPHY_PARAM		(dev_priv->mipi_mmio_base + 0xb080)
#define _MIPIC_DPHY_PARAM		(dev_priv->mipi_mmio_base + 0xb880)
#define MIPI_DPHY_PARAM(port)		_MIPI_PORT(port, _MIPIA_DPHY_PARAM, \
					_MIPIC_DPHY_PARAM)
#define  EXIT_ZERO_COUNT_SHIFT				24
#define  EXIT_ZERO_COUNT_MASK				(0x3f << 24)
#define  TRAIL_COUNT_SHIFT				16
#define  TRAIL_COUNT_MASK				(0x1f << 16)
#define  CLK_ZERO_COUNT_SHIFT				8
#define  CLK_ZERO_COUNT_MASK				(0xff << 8)
#define  PREPARE_COUNT_SHIFT				0
#define  PREPARE_COUNT_MASK				(0x3f << 0)

/* bits 31:0 */
#define _MIPIA_DBI_BW_CTRL		(dev_priv->mipi_mmio_base + 0xb084)
#define _MIPIC_DBI_BW_CTRL		(dev_priv->mipi_mmio_base + 0xb884)
#define MIPI_DBI_BW_CTRL(port)		_MIPI_PORT(port, _MIPIA_DBI_BW_CTRL, \
					_MIPIC_DBI_BW_CTRL)

#define _MIPIA_CLK_LANE_SWITCH_TIME_CNT		(dev_priv->mipi_mmio_base \
							+ 0xb088)
#define _MIPIC_CLK_LANE_SWITCH_TIME_CNT		(dev_priv->mipi_mmio_base \
							+ 0xb888)
#define MIPI_CLK_LANE_SWITCH_TIME_CNT(port)	_MIPI_PORT(port, \
	_MIPIA_CLK_LANE_SWITCH_TIME_CNT, _MIPIC_CLK_LANE_SWITCH_TIME_CNT)
#define  LP_HS_SSW_CNT_SHIFT				16
#define  LP_HS_SSW_CNT_MASK				(0xffff << 16)
#define  HS_LP_PWR_SW_CNT_SHIFT				0
#define  HS_LP_PWR_SW_CNT_MASK				(0xffff << 0)

#define _MIPIA_STOP_STATE_STALL		(dev_priv->mipi_mmio_base + 0xb08c)
#define _MIPIC_STOP_STATE_STALL		(dev_priv->mipi_mmio_base + 0xb88c)
#define MIPI_STOP_STATE_STALL(port)	_MIPI_PORT(port, \
			_MIPIA_STOP_STATE_STALL, _MIPIC_STOP_STATE_STALL)
#define  STOP_STATE_STALL_COUNTER_SHIFT			0
#define  STOP_STATE_STALL_COUNTER_MASK			(0xff << 0)

#define _MIPIA_INTR_STAT_REG_1		(dev_priv->mipi_mmio_base + 0xb090)
#define _MIPIC_INTR_STAT_REG_1		(dev_priv->mipi_mmio_base + 0xb890)
#define MIPI_INTR_STAT_REG_1(port)	_MIPI_PORT(port, \
				_MIPIA_INTR_STAT_REG_1, _MIPIC_INTR_STAT_REG_1)
#define _MIPIA_INTR_EN_REG_1		(dev_priv->mipi_mmio_base + 0xb094)
#define _MIPIC_INTR_EN_REG_1		(dev_priv->mipi_mmio_base + 0xb894)
#define MIPI_INTR_EN_REG_1(port)	_MIPI_PORT(port, _MIPIA_INTR_EN_REG_1, \
					_MIPIC_INTR_EN_REG_1)
#define  RX_CONTENTION_DETECTED				(1 << 0)

/* XXX: only pipe A ?!? */
#define MIPIA_DBI_TYPEC_CTRL		(dev_priv->mipi_mmio_base + 0xb100)
#define  DBI_TYPEC_ENABLE				(1 << 31)
#define  DBI_TYPEC_WIP					(1 << 30)
#define  DBI_TYPEC_OPTION_SHIFT				28
#define  DBI_TYPEC_OPTION_MASK				(3 << 28)
#define  DBI_TYPEC_FREQ_SHIFT				24
#define  DBI_TYPEC_FREQ_MASK				(0xf << 24)
#define  DBI_TYPEC_OVERRIDE				(1 << 8)
#define  DBI_TYPEC_OVERRIDE_COUNTER_SHIFT		0
#define  DBI_TYPEC_OVERRIDE_COUNTER_MASK		(0xff << 0)


/* MIPI adapter registers */

#define _MIPIA_CTRL			(dev_priv->mipi_mmio_base + 0xb104)
#define _MIPIC_CTRL			(dev_priv->mipi_mmio_base + 0xb904)
#define MIPI_CTRL(port)			_MIPI_PORT(port, _MIPIA_CTRL, \
					_MIPIC_CTRL)
#define  ESCAPE_CLOCK_DIVIDER_SHIFT			5 /* A only */
#define  ESCAPE_CLOCK_DIVIDER_MASK			(3 << 5)
#define  ESCAPE_CLOCK_DIVIDER_1				(0 << 5)
#define  ESCAPE_CLOCK_DIVIDER_2				(1 << 5)
#define  ESCAPE_CLOCK_DIVIDER_4				(2 << 5)
#define  READ_REQUEST_PRIORITY_SHIFT			3
#define  READ_REQUEST_PRIORITY_MASK			(3 << 3)
#define  READ_REQUEST_PRIORITY_LOW			(0 << 3)
#define  READ_REQUEST_PRIORITY_HIGH			(3 << 3)
#define  RGB_FLIP_TO_BGR				(1 << 2)

#define _MIPIA_DATA_ADDRESS		(dev_priv->mipi_mmio_base + 0xb108)
#define _MIPIC_DATA_ADDRESS		(dev_priv->mipi_mmio_base + 0xb908)
#define MIPI_DATA_ADDRESS(port)		_MIPI_PORT(port, _MIPIA_DATA_ADDRESS, \
					_MIPIC_DATA_ADDRESS)
#define  DATA_MEM_ADDRESS_SHIFT				5
#define  DATA_MEM_ADDRESS_MASK				(0x7ffffff << 5)
#define  DATA_VALID					(1 << 0)

#define _MIPIA_DATA_LENGTH		(dev_priv->mipi_mmio_base + 0xb10c)
#define _MIPIC_DATA_LENGTH		(dev_priv->mipi_mmio_base + 0xb90c)
#define MIPI_DATA_LENGTH(port)		_MIPI_PORT(port, _MIPIA_DATA_LENGTH, \
					_MIPIC_DATA_LENGTH)
#define  DATA_LENGTH_SHIFT				0
#define  DATA_LENGTH_MASK				(0xfffff << 0)

#define _MIPIA_COMMAND_ADDRESS		(dev_priv->mipi_mmio_base + 0xb110)
#define _MIPIC_COMMAND_ADDRESS		(dev_priv->mipi_mmio_base + 0xb910)
#define MIPI_COMMAND_ADDRESS(port)	_MIPI_PORT(port, \
				_MIPIA_COMMAND_ADDRESS, _MIPIC_COMMAND_ADDRESS)
#define  COMMAND_MEM_ADDRESS_SHIFT			5
#define  COMMAND_MEM_ADDRESS_MASK			(0x7ffffff << 5)
#define  AUTO_PWG_ENABLE				(1 << 2)
#define  MEMORY_WRITE_DATA_FROM_PIPE_RENDERING		(1 << 1)
#define  COMMAND_VALID					(1 << 0)

#define _MIPIA_COMMAND_LENGTH		(dev_priv->mipi_mmio_base + 0xb114)
#define _MIPIC_COMMAND_LENGTH		(dev_priv->mipi_mmio_base + 0xb914)
#define MIPI_COMMAND_LENGTH(port)	_MIPI_PORT(port, _MIPIA_COMMAND_LENGTH, \
					_MIPIC_COMMAND_LENGTH)
#define  COMMAND_LENGTH_SHIFT(n)			(8 * (n)) /* n: 0...3 */
#define  COMMAND_LENGTH_MASK(n)				(0xff << (8 * (n)))

#define _MIPIA_READ_DATA_RETURN0	(dev_priv->mipi_mmio_base + 0xb118)
#define _MIPIC_READ_DATA_RETURN0	(dev_priv->mipi_mmio_base + 0xb918)
#define MIPI_READ_DATA_RETURN(port, n) \
	(_MIPI_PORT(port, _MIPIA_READ_DATA_RETURN0, _MIPIC_READ_DATA_RETURN0) \
					+ 4 * (n)) /* n: 0...7 */

#define _MIPIA_READ_DATA_VALID		(dev_priv->mipi_mmio_base + 0xb138)
#define _MIPIC_READ_DATA_VALID		(dev_priv->mipi_mmio_base + 0xb938)
#define MIPI_READ_DATA_VALID(port)	_MIPI_PORT(port, \
				_MIPIA_READ_DATA_VALID, _MIPIC_READ_DATA_VALID)
#define  READ_DATA_VALID(n)				(1 << (n))

/* For UMS only (deprecated): */
#define _PALETTE_A (dev_priv->info.display_mmio_offset + 0xa000)
#define _PALETTE_B (dev_priv->info.display_mmio_offset + 0xa800)

#endif /* _I915_REG_H_ */
