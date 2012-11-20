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
#define _TRANSCODER(tran, a, b) ((a) + (tran)*((b)-(a)))

#define _PORT(port, a, b) ((a) + (port)*((b)-(a)))

#define _MASKED_BIT_ENABLE(a) (((a) << 16) | (a))
#define _MASKED_BIT_DISABLE(a) ((a) << 16)

/*
 * The Bridge device's PCI config space has information about the
 * fb aperture size and the amount of pre-reserved memory.
 * This is all handled in the intel-gtt.ko module. i915.ko only
 * cares about the vga bit for the vga rbiter.
 */
#define INTEL_GMCH_CTRL		0x52
#define INTEL_GMCH_VGA_DISABLE  (1 << 1)
#define SNB_GMCH_CTRL		0x50
#define    SNB_GMCH_GGMS_SHIFT	8 /* GTT Graphics Memory Size */
#define    SNB_GMCH_GGMS_MASK	0x3
#define    SNB_GMCH_GMS_SHIFT   3 /* Graphics Mode Select */
#define    SNB_GMCH_GMS_MASK    0x1f
#define    IVB_GMCH_GMS_SHIFT   4
#define    IVB_GMCH_GMS_MASK    0xf


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
#define LBB	0xf4

/* Graphics reset regs */
#define I965_GDRST 0xc0 /* PCI config register */
#define ILK_GDSR 0x2ca4 /* MCHBAR offset */
#define  GRDOM_FULL	(0<<2)
#define  GRDOM_RENDER	(1<<2)
#define  GRDOM_MEDIA	(3<<2)
#define  GRDOM_RESET_ENABLE (1<<0)

#define GEN6_MBCUNIT_SNPCR	0x900c /* for LLC config */
#define   GEN6_MBC_SNPCR_SHIFT	21
#define   GEN6_MBC_SNPCR_MASK	(3<<21)
#define   GEN6_MBC_SNPCR_MAX	(0<<21)
#define   GEN6_MBC_SNPCR_MED	(1<<21)
#define   GEN6_MBC_SNPCR_LOW	(2<<21)
#define   GEN6_MBC_SNPCR_MIN	(3<<21) /* only 1/16th of the cache is shared */

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

#define GAM_ECOCHK			0x4090
#define   ECOCHK_SNB_BIT		(1<<10)
#define   ECOCHK_PPGTT_CACHE64B		(0x3<<3)
#define   ECOCHK_PPGTT_CACHE4B		(0x0<<3)

#define GAC_ECO_BITS			0x14090
#define   ECOBITS_PPGTT_CACHE64B	(3<<8)
#define   ECOBITS_PPGTT_CACHE4B		(0<<8)

#define GAB_CTL				0x24000
#define   GAB_CTL_CONT_AFTER_PAGEFAULT	(1<<8)

/* VGA stuff */

#define VGA_ST01_MDA 0x3ba
#define VGA_ST01_CGA 0x3da

#define VGA_MSR_WRITE 0x3c2
#define VGA_MSR_READ 0x3cc
#define   VGA_MSR_MEM_EN (1<<1)
#define   VGA_MSR_CGA_MODE (1<<0)

#define VGA_SR_INDEX 0x3c4
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
 * Memory interface instructions used by the kernel
 */
#define MI_INSTR(opcode, flags) (((opcode) << 23) | (flags))

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
#define MI_BATCH_BUFFER_END	MI_INSTR(0x0a, 0)
#define MI_SUSPEND_FLUSH	MI_INSTR(0x0b, 0)
#define   MI_SUSPEND_FLUSH_EN	(1<<0)
#define MI_REPORT_HEAD		MI_INSTR(0x07, 0)
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
#define MI_ARB_ON_OFF		MI_INSTR(0x08, 0)
#define   MI_ARB_ENABLE			(1<<0)
#define   MI_ARB_DISABLE		(0<<0)

#define MI_SET_CONTEXT		MI_INSTR(0x18, 0)
#define   MI_MM_SPACE_GTT		(1<<8)
#define   MI_MM_SPACE_PHYSICAL		(0<<8)
#define   MI_SAVE_EXT_STATE_EN		(1<<3)
#define   MI_RESTORE_EXT_STATE_EN	(1<<2)
#define   MI_FORCE_RESTORE		(1<<1)
#define   MI_RESTORE_INHIBIT		(1<<0)
#define MI_STORE_DWORD_IMM	MI_INSTR(0x20, 1)
#define   MI_MEM_VIRTUAL	(1 << 22) /* 965+ only */
#define MI_STORE_DWORD_INDEX	MI_INSTR(0x21, 1)
#define   MI_STORE_DWORD_INDEX_SHIFT 2
/* Official intel docs are somewhat sloppy concerning MI_LOAD_REGISTER_IMM:
 * - Always issue a MI_NOOP _before_ the MI_LOAD_REGISTER_IMM - otherwise hw
 *   simply ignores the register load under certain conditions.
 * - One can actually load arbitrary many arbitrary registers: Simply issue x
 *   address/value pairs. Don't overdue it, though, x <= 2^4 must hold!
 */
#define MI_LOAD_REGISTER_IMM(x)	MI_INSTR(0x22, 2*x-1)
#define MI_FLUSH_DW		MI_INSTR(0x26, 1) /* for GEN6 */
#define   MI_FLUSH_DW_STORE_INDEX	(1<<21)
#define   MI_INVALIDATE_TLB		(1<<18)
#define   MI_FLUSH_DW_OP_STOREDW	(1<<14)
#define   MI_INVALIDATE_BSD		(1<<7)
#define   MI_FLUSH_DW_USE_GTT		(1<<2)
#define   MI_FLUSH_DW_USE_PPGTT		(0<<2)
#define MI_BATCH_BUFFER		MI_INSTR(0x30, 1)
#define   MI_BATCH_NON_SECURE		(1)
/* for snb/ivb/vlv this also means "batch in ppgtt" when ppgtt is enabled. */
#define   MI_BATCH_NON_SECURE_I965 	(1<<8)
#define   MI_BATCH_PPGTT_HSW		(1<<8)
#define   MI_BATCH_NON_SECURE_HSW 	(1<<13)
#define MI_BATCH_BUFFER_START	MI_INSTR(0x31, 0)
#define   MI_BATCH_GTT		    (2<<6) /* aliased with (1<<7) on gen4 */
#define MI_SEMAPHORE_MBOX	MI_INSTR(0x16, 1) /* gen6+ */
#define  MI_SEMAPHORE_GLOBAL_GTT    (1<<22)
#define  MI_SEMAPHORE_UPDATE	    (1<<21)
#define  MI_SEMAPHORE_COMPARE	    (1<<20)
#define  MI_SEMAPHORE_REGISTER	    (1<<18)
#define  MI_SEMAPHORE_SYNC_RV	    (2<<16)
#define  MI_SEMAPHORE_SYNC_RB	    (0<<16)
#define  MI_SEMAPHORE_SYNC_VR	    (0<<16)
#define  MI_SEMAPHORE_SYNC_VB	    (2<<16)
#define  MI_SEMAPHORE_SYNC_BR	    (2<<16)
#define  MI_SEMAPHORE_SYNC_BV	    (0<<16)
#define  MI_SEMAPHORE_SYNC_INVALID  (1<<0)
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
#define SRC_COPY_BLT_CMD                ((2<<29)|(0x43<<22)|4)
#define XY_SRC_COPY_BLT_CMD		((2<<29)|(0x53<<22)|6)
#define XY_MONO_SRC_COPY_IMM_BLT	((2<<29)|(0x71<<22)|5)
#define XY_SRC_COPY_BLT_WRITE_ALPHA	(1<<21)
#define XY_SRC_COPY_BLT_WRITE_RGB	(1<<20)
#define   BLT_DEPTH_8			(0<<24)
#define   BLT_DEPTH_16_565		(1<<24)
#define   BLT_DEPTH_16_1555		(2<<24)
#define   BLT_DEPTH_32			(3<<24)
#define   BLT_ROP_GXCOPY		(0xcc<<16)
#define XY_SRC_COPY_BLT_SRC_TILED	(1<<15) /* 965+ only */
#define XY_SRC_COPY_BLT_DST_TILED	(1<<11) /* 965+ only */
#define CMD_OP_DISPLAYBUFFER_INFO ((0x0<<29)|(0x14<<23)|2)
#define   ASYNC_FLIP                (1<<22)
#define   DISPLAY_PLANE_A           (0<<20)
#define   DISPLAY_PLANE_B           (1<<20)
#define GFX_OP_PIPE_CONTROL(len)	((0x3<<29)|(0x3<<27)|(0x2<<24)|(len-2))
#define   PIPE_CONTROL_CS_STALL				(1<<20)
#define   PIPE_CONTROL_TLB_INVALIDATE			(1<<18)
#define   PIPE_CONTROL_QW_WRITE				(1<<14)
#define   PIPE_CONTROL_DEPTH_STALL			(1<<13)
#define   PIPE_CONTROL_WRITE_FLUSH			(1<<12)
#define   PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH	(1<<12) /* gen6+ */
#define   PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE	(1<<11) /* MBZ on Ironlake */
#define   PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE		(1<<10) /* GM45+ only */
#define   PIPE_CONTROL_INDIRECT_STATE_DISABLE		(1<<9)
#define   PIPE_CONTROL_NOTIFY				(1<<8)
#define   PIPE_CONTROL_VF_CACHE_INVALIDATE		(1<<4)
#define   PIPE_CONTROL_CONST_CACHE_INVALIDATE		(1<<3)
#define   PIPE_CONTROL_STATE_CACHE_INVALIDATE		(1<<2)
#define   PIPE_CONTROL_STALL_AT_SCOREBOARD		(1<<1)
#define   PIPE_CONTROL_DEPTH_CACHE_FLUSH		(1<<0)
#define   PIPE_CONTROL_GLOBAL_GTT (1<<2) /* in addr dword */


/*
 * Reset registers
 */
#define DEBUG_RESET_I830		0x6070
#define  DEBUG_RESET_FULL		(1<<7)
#define  DEBUG_RESET_RENDER		(1<<8)
#define  DEBUG_RESET_DISPLAY		(1<<9)

/*
 * DPIO - a special bus for various display related registers to hide behind:
 *  0x800c: m1, m2, n, p1, p2, k dividers
 *  0x8014: REF and SFR select
 *  0x8014: N divider, VCO select
 *  0x801c/3c: core clock bits
 *  0x8048/68: low pass filter coefficients
 *  0x8100: fast clock controls
 */
#define DPIO_PKT			0x2100
#define  DPIO_RID			(0<<24)
#define  DPIO_OP_WRITE			(1<<16)
#define  DPIO_OP_READ			(0<<16)
#define  DPIO_PORTID			(0x12<<8)
#define  DPIO_BYTE			(0xf<<4)
#define  DPIO_BUSY			(1<<0) /* status only */
#define DPIO_DATA			0x2104
#define DPIO_REG			0x2108
#define DPIO_CTL			0x2110
#define  DPIO_MODSEL1			(1<<3) /* if ref clk b == 27 */
#define  DPIO_MODSEL0			(1<<2) /* if ref clk a == 27 */
#define  DPIO_SFR_BYPASS		(1<<1)
#define  DPIO_RESET			(1<<0)

#define _DPIO_DIV_A			0x800c
#define   DPIO_POST_DIV_SHIFT		(28) /* 3 bits */
#define   DPIO_K_SHIFT			(24) /* 4 bits */
#define   DPIO_P1_SHIFT			(21) /* 3 bits */
#define   DPIO_P2_SHIFT			(16) /* 5 bits */
#define   DPIO_N_SHIFT			(12) /* 4 bits */
#define   DPIO_ENABLE_CALIBRATION	(1<<11)
#define   DPIO_M1DIV_SHIFT		(8) /* 3 bits */
#define   DPIO_M2DIV_MASK		0xff
#define _DPIO_DIV_B			0x802c
#define DPIO_DIV(pipe) _PIPE(pipe, _DPIO_DIV_A, _DPIO_DIV_B)

#define _DPIO_REFSFR_A			0x8014
#define   DPIO_REFSEL_OVERRIDE		27
#define   DPIO_PLL_MODESEL_SHIFT	24 /* 3 bits */
#define   DPIO_BIAS_CURRENT_CTL_SHIFT	21 /* 3 bits, always 0x7 */
#define   DPIO_PLL_REFCLK_SEL_SHIFT	16 /* 2 bits */
#define   DPIO_PLL_REFCLK_SEL_MASK	3
#define   DPIO_DRIVER_CTL_SHIFT		12 /* always set to 0x8 */
#define   DPIO_CLK_BIAS_CTL_SHIFT	8 /* always set to 0x5 */
#define _DPIO_REFSFR_B			0x8034
#define DPIO_REFSFR(pipe) _PIPE(pipe, _DPIO_REFSFR_A, _DPIO_REFSFR_B)

#define _DPIO_CORE_CLK_A		0x801c
#define _DPIO_CORE_CLK_B		0x803c
#define DPIO_CORE_CLK(pipe) _PIPE(pipe, _DPIO_CORE_CLK_A, _DPIO_CORE_CLK_B)

#define _DPIO_LFP_COEFF_A		0x8048
#define _DPIO_LFP_COEFF_B		0x8068
#define DPIO_LFP_COEFF(pipe) _PIPE(pipe, _DPIO_LFP_COEFF_A, _DPIO_LFP_COEFF_B)

#define DPIO_FASTCLK_DISABLE		0x8100

#define DPIO_DATA_CHANNEL1		0x8220
#define DPIO_DATA_CHANNEL2		0x8420

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

/* control register for cpu gtt access */
#define TILECTL				0x101000
#define   TILECTL_SWZCTL			(1 << 0)
#define   TILECTL_TLB_PREFETCH_DIS	(1 << 2)
#define   TILECTL_BACKSNOOP_DIS		(1 << 3)

/*
 * Instruction and interrupt control regs
 */
#define PGTBL_ER	0x02024
#define RENDER_RING_BASE	0x02000
#define BSD_RING_BASE		0x04000
#define GEN6_BSD_RING_BASE	0x12000
#define BLT_RING_BASE		0x22000
#define RING_TAIL(base)		((base)+0x30)
#define RING_HEAD(base)		((base)+0x34)
#define RING_START(base)	((base)+0x38)
#define RING_CTL(base)		((base)+0x3c)
#define RING_SYNC_0(base)	((base)+0x40)
#define RING_SYNC_1(base)	((base)+0x44)
#define GEN6_RVSYNC (RING_SYNC_0(RENDER_RING_BASE))
#define GEN6_RBSYNC (RING_SYNC_1(RENDER_RING_BASE))
#define GEN6_VRSYNC (RING_SYNC_1(GEN6_BSD_RING_BASE))
#define GEN6_VBSYNC (RING_SYNC_0(GEN6_BSD_RING_BASE))
#define GEN6_BRSYNC (RING_SYNC_0(BLT_RING_BASE))
#define GEN6_BVSYNC (RING_SYNC_1(BLT_RING_BASE))
#define RING_MAX_IDLE(base)	((base)+0x54)
#define RING_HWS_PGA(base)	((base)+0x80)
#define RING_HWS_PGA_GEN6(base)	((base)+0x2080)
#define ARB_MODE		0x04030
#define   ARB_MODE_SWIZZLE_SNB	(1<<4)
#define   ARB_MODE_SWIZZLE_IVB	(1<<5)
#define RENDER_HWS_PGA_GEN7	(0x04080)
#define RING_FAULT_REG(ring)	(0x4094 + 0x100*(ring)->id)
#define DONE_REG		0x40b0
#define BSD_HWS_PGA_GEN7	(0x04180)
#define BLT_HWS_PGA_GEN7	(0x04280)
#define RING_ACTHD(base)	((base)+0x74)
#define RING_NOPID(base)	((base)+0x94)
#define RING_IMR(base)		((base)+0xa8)
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
#define RING_INSTPM(base)	((base)+0xc0)
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

#define ERROR_GEN6	0x040a0
#define GEN7_ERR_INT	0x44040
#define   ERR_INT_MMIO_UNCLAIMED (1<<13)

/* GM45+ chicken bits -- debug workaround bits that may be required
 * for various sorts of correct behavior.  The top 16 bits of each are
 * the enables for writing to the corresponding low bit.
 */
#define _3D_CHICKEN	0x02084
#define _3D_CHICKEN2	0x0208c
/* Disables pipelining of read flushes past the SF-WIZ interface.
 * Required on all Ironlake steppings according to the B-Spec, but the
 * particular danger of not doing so is not specified.
 */
# define _3D_CHICKEN2_WM_READ_PIPELINED			(1 << 14)
#define _3D_CHICKEN3	0x02090
#define  _3D_CHICKEN_SF_DISABLE_OBJEND_CULL		(1 << 10)
#define  _3D_CHICKEN3_SF_DISABLE_FASTCLIP_CULL		(1 << 5)

#define MI_MODE		0x0209c
# define VS_TIMER_DISPATCH				(1 << 6)
# define MI_FLUSH_ENABLE				(1 << 12)

#define GEN6_GT_MODE	0x20d0
#define   GEN6_GT_MODE_HI	(1 << 9)

#define GFX_MODE	0x02520
#define GFX_MODE_GEN7	0x0229c
#define RING_MODE_GEN7(ring)	((ring)->mmio_base+0x29c)
#define   GFX_RUN_LIST_ENABLE		(1<<15)
#define   GFX_TLB_INVALIDATE_ALWAYS	(1<<13)
#define   GFX_SURFACE_FAULT_ENABLE	(1<<12)
#define   GFX_REPLAY_MODE		(1<<11)
#define   GFX_PSMI_GRANULARITY		(1<<10)
#define   GFX_PPGTT_ENABLE		(1<<9)

#define VLV_DISPLAY_BASE 0x180000

#define SCPD0		0x0209c /* 915+ only */
#define IER		0x020a0
#define IIR		0x020a4
#define IMR		0x020a8
#define ISR		0x020ac
#define VLV_GUNIT_CLOCK_GATE	0x182060
#define   GCFG_DIS		(1<<8)
#define VLV_IIR_RW	0x182084
#define VLV_IER		0x1820a0
#define VLV_IIR		0x1820a4
#define VLV_IMR		0x1820a8
#define VLV_ISR		0x1820ac
#define   I915_PIPE_CONTROL_NOTIFY_INTERRUPT		(1<<18)
#define   I915_DISPLAY_PORT_INTERRUPT			(1<<17)
#define   I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT	(1<<15)
#define   I915_GMCH_THERMAL_SENSOR_EVENT_INTERRUPT	(1<<14) /* p-state */
#define   I915_HWB_OOM_INTERRUPT			(1<<13)
#define   I915_SYNC_STATUS_INTERRUPT			(1<<12)
#define   I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT	(1<<11)
#define   I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT	(1<<10)
#define   I915_OVERLAY_PLANE_FLIP_PENDING_INTERRUPT	(1<<9)
#define   I915_DISPLAY_PLANE_C_FLIP_PENDING_INTERRUPT	(1<<8)
#define   I915_DISPLAY_PIPE_A_VBLANK_INTERRUPT		(1<<7)
#define   I915_DISPLAY_PIPE_A_EVENT_INTERRUPT		(1<<6)
#define   I915_DISPLAY_PIPE_B_VBLANK_INTERRUPT		(1<<5)
#define   I915_DISPLAY_PIPE_B_EVENT_INTERRUPT		(1<<4)
#define   I915_DEBUG_INTERRUPT				(1<<2)
#define   I915_USER_INTERRUPT				(1<<1)
#define   I915_ASLE_INTERRUPT				(1<<0)
#define   I915_BSD_USER_INTERRUPT                      (1<<25)
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
#define   INSTPM_AGPBUSY_DIS (1<<11) /* gen3: when disabled, pending interrupts
					will not assert AGPBUSY# and will only
					be delivered when out of C3. */
#define   INSTPM_FORCE_ORDERING				(1<<7) /* GEN6+ */
#define ACTHD	        0x020c8
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

#define CACHE_MODE_0	0x02120 /* 915+ only */
#define   CM0_PIPELINED_RENDER_FLUSH_DISABLE (1<<8)
#define   CM0_IZ_OPT_DISABLE      (1<<6)
#define   CM0_ZR_OPT_DISABLE      (1<<5)
#define	  CM0_STC_EVICT_DISABLE_LRA_SNB	(1<<5)
#define   CM0_DEPTH_EVICT_DISABLE (1<<4)
#define   CM0_COLOR_EVICT_DISABLE (1<<3)
#define   CM0_DEPTH_WRITE_DISABLE (1<<1)
#define   CM0_RC_OP_FLUSH_DISABLE (1<<0)
#define BB_ADDR		0x02140 /* 8 bytes */
#define GFX_FLSH_CNTL	0x02170 /* 915+ only */
#define GFX_FLSH_CNTL_GEN6	0x101008
#define   GFX_FLSH_CNTL_EN	(1<<0)
#define ECOSKPD		0x021d0
#define   ECO_GATING_CX_ONLY	(1<<3)
#define   ECO_FLIP_DONE		(1<<0)

#define CACHE_MODE_1		0x7004 /* IVB+ */
#define   PIXEL_SUBSPAN_COLLECT_OPT_DISABLE (1<<6)

/* GEN6 interrupt control
 * Note that the per-ring interrupt bits do alias with the global interrupt bits
 * in GTIMR. */
#define GEN6_RENDER_HWSTAM	0x2098
#define GEN6_RENDER_IMR		0x20a8
#define   GEN6_RENDER_CONTEXT_SWITCH_INTERRUPT		(1 << 8)
#define   GEN6_RENDER_PPGTT_PAGE_FAULT			(1 << 7)
#define   GEN6_RENDER_TIMEOUT_COUNTER_EXPIRED		(1 << 6)
#define   GEN6_RENDER_L3_PARITY_ERROR			(1 << 5)
#define   GEN6_RENDER_PIPE_CONTROL_NOTIFY_INTERRUPT	(1 << 4)
#define   GEN6_RENDER_COMMAND_PARSER_MASTER_ERROR	(1 << 3)
#define   GEN6_RENDER_SYNC_STATUS			(1 << 2)
#define   GEN6_RENDER_DEBUG_INTERRUPT			(1 << 1)
#define   GEN6_RENDER_USER_INTERRUPT			(1 << 0)

#define GEN6_BLITTER_HWSTAM	0x22098
#define GEN6_BLITTER_IMR	0x220a8
#define   GEN6_BLITTER_MI_FLUSH_DW_NOTIFY_INTERRUPT	(1 << 26)
#define   GEN6_BLITTER_COMMAND_PARSER_MASTER_ERROR	(1 << 25)
#define   GEN6_BLITTER_SYNC_STATUS			(1 << 24)
#define   GEN6_BLITTER_USER_INTERRUPT			(1 << 22)

#define GEN6_BLITTER_ECOSKPD	0x221d0
#define   GEN6_BLITTER_LOCK_SHIFT			16
#define   GEN6_BLITTER_FBC_NOTIFY			(1<<3)

#define GEN6_BSD_SLEEP_PSMI_CONTROL	0x12050
#define   GEN6_BSD_SLEEP_MSG_DISABLE	(1 << 0)
#define   GEN6_BSD_SLEEP_FLUSH_DISABLE	(1 << 2)
#define   GEN6_BSD_SLEEP_INDICATOR	(1 << 3)
#define   GEN6_BSD_GO_INDICATOR		(1 << 4)

#define GEN6_BSD_HWSTAM			0x12098
#define GEN6_BSD_IMR			0x120a8
#define   GEN6_BSD_USER_INTERRUPT	(1 << 12)

#define GEN6_BSD_RNCID			0x12198

#define GEN7_FF_THREAD_MODE		0x20a0
#define   GEN7_FF_SCHED_MASK		0x0077070
#define   GEN7_FF_TS_SCHED_HS1		(0x5<<16)
#define   GEN7_FF_TS_SCHED_HS0		(0x3<<16)
#define   GEN7_FF_TS_SCHED_LOAD_BALANCE	(0x1<<16)
#define   GEN7_FF_TS_SCHED_HW		(0x0<<16) /* Default */
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
#define   FBC_CTL_FENCENO	(1<<0)
#define FBC_COMMAND		0x0320c
#define   FBC_CMD_COMPRESS	(1<<0)
#define FBC_STATUS		0x03210
#define   FBC_STAT_COMPRESSING	(1<<31)
#define   FBC_STAT_COMPRESSED	(1<<30)
#define   FBC_STAT_MODIFIED	(1<<29)
#define   FBC_STAT_CURRENT_LINE	(1<<0)
#define FBC_CONTROL2		0x03214
#define   FBC_CTL_FENCE_DBL	(0<<4)
#define   FBC_CTL_IDLE_IMM	(0<<2)
#define   FBC_CTL_IDLE_FULL	(1<<2)
#define   FBC_CTL_IDLE_LINE	(2<<2)
#define   FBC_CTL_IDLE_DEBUG	(3<<2)
#define   FBC_CTL_CPU_FENCE	(1<<1)
#define   FBC_CTL_PLANEA	(0<<0)
#define   FBC_CTL_PLANEB	(1<<0)
#define FBC_FENCE_OFF		0x0321b
#define FBC_TAG			0x03300

#define FBC_LL_SIZE		(1536)

/* Framebuffer compression for GM45+ */
#define DPFC_CB_BASE		0x3200
#define DPFC_CONTROL		0x3208
#define   DPFC_CTL_EN		(1<<31)
#define   DPFC_CTL_PLANEA	(0<<30)
#define   DPFC_CTL_PLANEB	(1<<30)
#define   DPFC_CTL_FENCE_EN	(1<<29)
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
/* The bit 28-8 is reserved */
#define   DPFC_RESERVED		(0x1FFFFF00)
#define ILK_DPFC_RECOMP_CTL	0x4320c
#define ILK_DPFC_STATUS		0x43210
#define ILK_DPFC_FENCE_YOFF	0x43218
#define ILK_DPFC_CHICKEN	0x43224
#define ILK_FBC_RT_BASE		0x2128
#define   ILK_FBC_RT_VALID	(1<<0)

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
#define _DPLL_A	0x06014
#define _DPLL_B	0x06018
#define DPLL(pipe) _PIPE(pipe, _DPLL_A, _DPLL_B)
#define   DPLL_VCO_ENABLE		(1 << 31)
#define   DPLL_DVO_HIGH_SPEED		(1 << 30)
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
#define   DPLL_INTEGRATED_CLOCK_VLV	(1<<13)

#define SRX_INDEX		0x3c4
#define SRX_DATA		0x3c5
#define SR01			1
#define SR01_SCREEN_OFF		(1<<5)

#define PPCR			0x61204
#define PPCR_ON			(1<<0)

#define DVOB			0x61140
#define DVOB_ON			(1<<31)
#define DVOC			0x61160
#define DVOC_ON			(1<<31)
#define LVDS			0x61180
#define LVDS_ON			(1<<31)

/* Scratch pad debug 0 reg:
 */
#define   DPLL_FPA01_P1_POST_DIV_MASK_I830	0x001f0000
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
#define _DPLL_A_MD 0x0601c /* 965+ only */
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
#define _DPLL_B_MD 0x06020 /* 965+ only */
#define DPLL_MD(pipe) _PIPE(pipe, _DPLL_A_MD, _DPLL_B_MD)

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
#define DSPCLK_GATE_D		0x6200
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
/**
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
/** This bit must be unset on 855,865 */
# define MECI_CLOCK_GATE_DISABLE		(1 << 4)
# define DCMP_CLOCK_GATE_DISABLE		(1 << 3)
# define MEC_CLOCK_GATE_DISABLE			(1 << 2)
# define MECO_CLOCK_GATE_DISABLE		(1 << 1)
/** This bit must be set on 855,865. */
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
/** This bit must always be set on 965G/965GM */
# define I965_RCC_CLOCK_GATE_DISABLE		(1 << 29)
# define I965_RCPB_CLOCK_GATE_DISABLE		(1 << 28)
# define I965_DAP_CLOCK_GATE_DISABLE		(1 << 27)
# define I965_ROC_CLOCK_GATE_DISABLE		(1 << 26)
# define I965_GW_CLOCK_GATE_DISABLE		(1 << 25)
# define I965_TD_CLOCK_GATE_DISABLE		(1 << 24)
/** This bit must always be set on 965G */
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
#define RAMCLK_GATE_D		0x6210		/* CRL only */
#define DEUC			0x6214          /* CRL only */

#define FW_BLC_SELF_VLV		0x6500
#define  FW_CSPWRDWNEN		(1<<15)

/*
 * Palette regs
 */

#define _PALETTE_A		0x0a000
#define _PALETTE_B		0x0a800
#define PALETTE(pipe) _PIPE(pipe, _PALETTE_A, _PALETTE_B)

/* MCH MMIO space */

/*
 * MCHBAR mirror.
 *
 * This mirrors the MCHBAR MMIO space whose location is determined by
 * device 0 function 0's pci config register 0x44 or 0x48 and matches it in
 * every way.  It is not accessible from the CP register read instructions.
 *
 */
#define MCHBAR_MIRROR_BASE	0x10000

#define MCHBAR_MIRROR_BASE_SNB	0x140000

/** 915-945 and GM965 MCH register controlling DRAM channel access */
#define DCC			0x10200
#define DCC_ADDRESSING_MODE_SINGLE_CHANNEL		(0 << 0)
#define DCC_ADDRESSING_MODE_DUAL_CHANNEL_ASYMMETRIC	(1 << 0)
#define DCC_ADDRESSING_MODE_DUAL_CHANNEL_INTERLEAVED	(2 << 0)
#define DCC_ADDRESSING_MODE_MASK			(3 << 0)
#define DCC_CHANNEL_XOR_DISABLE				(1 << 10)
#define DCC_CHANNEL_XOR_BIT_17				(1 << 9)

/** Pineview MCH register contains DDR3 setting */
#define CSHRDDR3CTL            0x101a8
#define CSHRDDR3CTL_DDR3       (1 << 2)

/** 965 MCH register controlling DRAM channel configuration */
#define C0DRB3			0x10206
#define C1DRB3			0x10606

/** snb MCH registers for reading the DRAM channel configuration */
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
#define GEN6_GT_THREAD_STATUS_CORE_MASK_HSW (0x7 | (0x07 << 16))

#define GEN6_GT_PERF_STATUS	0x145948
#define GEN6_RP_STATE_LIMITS	0x145994
#define GEN6_RP_STATE_CAP	0x145998

/*
 * Logical Context regs
 */
#define CCID			0x2180
#define   CCID_EN		(1<<0)
#define CXT_SIZE		0x21a0
#define GEN6_CXT_POWER_SIZE(cxt_reg)	((cxt_reg >> 24) & 0x3f)
#define GEN6_CXT_RING_SIZE(cxt_reg)	((cxt_reg >> 18) & 0x3f)
#define GEN6_CXT_RENDER_SIZE(cxt_reg)	((cxt_reg >> 12) & 0x3f)
#define GEN6_CXT_EXTENDED_SIZE(cxt_reg)	((cxt_reg >> 6) & 0x3f)
#define GEN6_CXT_PIPELINE_SIZE(cxt_reg)	((cxt_reg >> 0) & 0x3f)
#define GEN6_CXT_TOTAL_SIZE(cxt_reg)	(GEN6_CXT_POWER_SIZE(cxt_reg) + \
					GEN6_CXT_RING_SIZE(cxt_reg) + \
					GEN6_CXT_RENDER_SIZE(cxt_reg) + \
					GEN6_CXT_EXTENDED_SIZE(cxt_reg) + \
					GEN6_CXT_PIPELINE_SIZE(cxt_reg))
#define GEN7_CXT_SIZE		0x21a8
#define GEN7_CXT_POWER_SIZE(ctx_reg)	((ctx_reg >> 25) & 0x7f)
#define GEN7_CXT_RING_SIZE(ctx_reg)	((ctx_reg >> 22) & 0x7)
#define GEN7_CXT_RENDER_SIZE(ctx_reg)	((ctx_reg >> 16) & 0x3f)
#define GEN7_CXT_EXTENDED_SIZE(ctx_reg)	((ctx_reg >> 9) & 0x7f)
#define GEN7_CXT_GT1_SIZE(ctx_reg)	((ctx_reg >> 6) & 0x7)
#define GEN7_CXT_VFSTATE_SIZE(ctx_reg)	((ctx_reg >> 0) & 0x3f)
#define GEN7_CXT_TOTAL_SIZE(ctx_reg)	(GEN7_CXT_POWER_SIZE(ctx_reg) + \
					 GEN7_CXT_RING_SIZE(ctx_reg) + \
					 GEN7_CXT_RENDER_SIZE(ctx_reg) + \
					 GEN7_CXT_EXTENDED_SIZE(ctx_reg) + \
					 GEN7_CXT_GT1_SIZE(ctx_reg) + \
					 GEN7_CXT_VFSTATE_SIZE(ctx_reg))
#define HSW_CXT_POWER_SIZE(ctx_reg)	((ctx_reg >> 26) & 0x3f)
#define HSW_CXT_RING_SIZE(ctx_reg)	((ctx_reg >> 23) & 0x7)
#define HSW_CXT_RENDER_SIZE(ctx_reg)	((ctx_reg >> 15) & 0xff)
#define HSW_CXT_TOTAL_SIZE(ctx_reg)	(HSW_CXT_POWER_SIZE(ctx_reg) + \
					 HSW_CXT_RING_SIZE(ctx_reg) + \
					 HSW_CXT_RENDER_SIZE(ctx_reg) + \
					 GEN7_CXT_VFSTATE_SIZE(ctx_reg))


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

/* Pipe A timing regs */
#define _HTOTAL_A	0x60000
#define _HBLANK_A	0x60004
#define _HSYNC_A		0x60008
#define _VTOTAL_A	0x6000c
#define _VBLANK_A	0x60010
#define _VSYNC_A		0x60014
#define _PIPEASRC	0x6001c
#define _BCLRPAT_A	0x60020
#define _VSYNCSHIFT_A	0x60028

/* Pipe B timing regs */
#define _HTOTAL_B	0x61000
#define _HBLANK_B	0x61004
#define _HSYNC_B		0x61008
#define _VTOTAL_B	0x6100c
#define _VBLANK_B	0x61010
#define _VSYNC_B		0x61014
#define _PIPEBSRC	0x6101c
#define _BCLRPAT_B	0x61020
#define _VSYNCSHIFT_B	0x61028


#define HTOTAL(trans) _TRANSCODER(trans, _HTOTAL_A, _HTOTAL_B)
#define HBLANK(trans) _TRANSCODER(trans, _HBLANK_A, _HBLANK_B)
#define HSYNC(trans) _TRANSCODER(trans, _HSYNC_A, _HSYNC_B)
#define VTOTAL(trans) _TRANSCODER(trans, _VTOTAL_A, _VTOTAL_B)
#define VBLANK(trans) _TRANSCODER(trans, _VBLANK_A, _VBLANK_B)
#define VSYNC(trans) _TRANSCODER(trans, _VSYNC_A, _VSYNC_B)
#define BCLRPAT(pipe) _PIPE(pipe, _BCLRPAT_A, _BCLRPAT_B)
#define VSYNCSHIFT(trans) _TRANSCODER(trans, _VSYNCSHIFT_A, _VSYNCSHIFT_B)

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
#define   ADPA_VSYNC_CNTL_DISABLE (1<<11)
#define   ADPA_VSYNC_CNTL_ENABLE 0
#define   ADPA_HSYNC_CNTL_DISABLE (1<<10)
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
#define PORT_HOTPLUG_EN		0x61110
#define   HDMIB_HOTPLUG_INT_EN			(1 << 29)
#define   DPB_HOTPLUG_INT_EN			(1 << 29)
#define   HDMIC_HOTPLUG_INT_EN			(1 << 28)
#define   DPC_HOTPLUG_INT_EN			(1 << 28)
#define   HDMID_HOTPLUG_INT_EN			(1 << 27)
#define   DPD_HOTPLUG_INT_EN			(1 << 27)
#define   SDVOB_HOTPLUG_INT_EN			(1 << 26)
#define   SDVOC_HOTPLUG_INT_EN			(1 << 25)
#define   TV_HOTPLUG_INT_EN			(1 << 18)
#define   CRT_HOTPLUG_INT_EN			(1 << 9)
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

#define PORT_HOTPLUG_STAT	0x61114
/* HDMI/DP bits are gen4+ */
#define   DPB_HOTPLUG_LIVE_STATUS               (1 << 29)
#define   DPC_HOTPLUG_LIVE_STATUS               (1 << 28)
#define   DPD_HOTPLUG_LIVE_STATUS               (1 << 27)
#define   DPD_HOTPLUG_INT_STATUS		(3 << 21)
#define   DPC_HOTPLUG_INT_STATUS		(3 << 19)
#define   DPB_HOTPLUG_INT_STATUS		(3 << 17)
/* HDMI bits are shared with the DP bits */
#define   HDMIB_HOTPLUG_LIVE_STATUS             (1 << 29)
#define   HDMIC_HOTPLUG_LIVE_STATUS             (1 << 28)
#define   HDMID_HOTPLUG_LIVE_STATUS             (1 << 27)
#define   HDMID_HOTPLUG_INT_STATUS		(3 << 21)
#define   HDMIC_HOTPLUG_INT_STATUS		(3 << 19)
#define   HDMIB_HOTPLUG_INT_STATUS		(3 << 17)
/* CRT/TV common between gen3+ */
#define   CRT_HOTPLUG_INT_STATUS		(1 << 11)
#define   TV_HOTPLUG_INT_STATUS			(1 << 10)
#define   CRT_HOTPLUG_MONITOR_MASK		(3 << 8)
#define   CRT_HOTPLUG_MONITOR_COLOR		(3 << 8)
#define   CRT_HOTPLUG_MONITOR_MONO		(2 << 8)
#define   CRT_HOTPLUG_MONITOR_NONE		(0 << 8)
/* SDVO is different across gen3/4 */
#define   SDVOC_HOTPLUG_INT_STATUS_G4X		(1 << 3)
#define   SDVOB_HOTPLUG_INT_STATUS_G4X		(1 << 2)
#define   SDVOC_HOTPLUG_INT_STATUS_I965		(3 << 4)
#define   SDVOB_HOTPLUG_INT_STATUS_I965		(3 << 2)
#define   SDVOC_HOTPLUG_INT_STATUS_I915		(1 << 7)
#define   SDVOB_HOTPLUG_INT_STATUS_I915		(1 << 6)

/* SDVO port control */
#define SDVOB			0x61140
#define SDVOC			0x61160
#define   SDVO_ENABLE		(1 << 31)
#define   SDVO_PIPE_B_SELECT	(1 << 30)
#define   SDVO_STALL_SELECT	(1 << 29)
#define   SDVO_INTERRUPT_ENABLE	(1 << 26)
/**
 * 915G/GM SDVO pixel multiplier.
 *
 * Programmed value is multiplier - 1, up to 5x.
 *
 * \sa DPLL_MD_UDI_MULTIPLIER_MASK
 */
#define   SDVO_PORT_MULTIPLY_MASK	(7 << 23)
#define   SDVO_PORT_MULTIPLY_SHIFT		23
#define   SDVO_PHASE_SELECT_MASK	(15 << 19)
#define   SDVO_PHASE_SELECT_DEFAULT	(6 << 19)
#define   SDVO_CLOCK_OUTPUT_INVERT	(1 << 18)
#define   SDVOC_GANG_MODE		(1 << 16)
#define   SDVO_ENCODING_SDVO		(0x0 << 10)
#define   SDVO_ENCODING_HDMI		(0x2 << 10)
/** Requird for HDMI operation */
#define   SDVO_NULL_PACKETS_DURING_VSYNC (1 << 9)
#define   SDVO_COLOR_RANGE_16_235	(1 << 8)
#define   SDVO_BORDER_ENABLE		(1 << 7)
#define   SDVO_AUDIO_ENABLE		(1 << 6)
/** New with 965, default is to be set */
#define   SDVO_VSYNC_ACTIVE_HIGH	(1 << 4)
/** New with 965, default is to be set */
#define   SDVO_HSYNC_ACTIVE_HIGH	(1 << 3)
#define   SDVOB_PCIE_CONCURRENCY	(1 << 3)
#define   SDVO_DETECTED			(1 << 2)
/* Bits to be preserved when writing */
#define   SDVOB_PRESERVE_MASK ((1 << 17) | (1 << 16) | (1 << 14) | (1 << 26))
#define   SDVOC_PRESERVE_MASK ((1 << 17) | (1 << 26))

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
#define VIDEO_DIP_CTL		0x61170
/* Pre HSW: */
#define   VIDEO_DIP_ENABLE		(1 << 31)
#define   VIDEO_DIP_PORT_B		(1 << 29)
#define   VIDEO_DIP_PORT_C		(2 << 29)
#define   VIDEO_DIP_PORT_D		(3 << 29)
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
#define PFIT_CONTROL	0x61230
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
#define PFIT_PGM_RATIOS	0x61234
#define   PFIT_VERT_SCALE_MASK			0xfff00000
#define   PFIT_HORIZ_SCALE_MASK			0x0000fff0
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

#define PFIT_AUTO_RATIOS 0x61238

/* Backlight control */
#define BLC_PWM_CTL2		0x61250 /* 965+ only */
#define   BLM_PWM_ENABLE		(1 << 31)
#define   BLM_COMBINATION_MODE		(1 << 30) /* gen4 only */
#define   BLM_PIPE_SELECT		(1 << 29)
#define   BLM_PIPE_SELECT_IVB		(3 << 29)
#define   BLM_PIPE_A			(0 << 29)
#define   BLM_PIPE_B			(1 << 29)
#define   BLM_PIPE_C			(2 << 29) /* ivb + */
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
#define BLC_PWM_CTL		0x61254
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

#define BLC_HIST_CTL		0x61260

/* New registers for PCH-split platforms. Safe where new bits show up, the
 * register layout machtes with gen4 BLC_PWM_CTL[12]. */
#define BLC_PWM_CPU_CTL2	0x48250
#define BLC_PWM_CPU_CTL		0x48254

/* PCH CTL1 is totally different, all but the below bits are reserved. CTL2 is
 * like the normal CTL from gen4 and earlier. Hooray for confusing naming. */
#define BLC_PWM_PCH_CTL1	0xc8250
#define   BLM_PCH_PWM_ENABLE			(1 << 31)
#define   BLM_PCH_OVERRIDE_ENABLE		(1 << 30)
#define   BLM_PCH_POLARITY			(1 << 29)
#define BLC_PWM_PCH_CTL2	0xc8254

/* TV port control */
#define TV_CTL			0x68000
/** Enables the TV encoder */
# define TV_ENC_ENABLE			(1 << 31)
/** Sources the TV encoder input from pipe B instead of A. */
# define TV_ENC_PIPEB_SELECT		(1 << 30)
/** Outputs composite video (DAC A only) */
# define TV_ENC_OUTPUT_COMPOSITE	(0 << 28)
/** Outputs SVideo video (DAC B/C) */
# define TV_ENC_OUTPUT_SVIDEO		(1 << 28)
/** Outputs Component video (DAC A/B/C) */
# define TV_ENC_OUTPUT_COMPONENT	(2 << 28)
/** Outputs Composite and SVideo (DAC A/B/C) */
# define TV_ENC_OUTPUT_SVIDEO_COMPOSITE	(3 << 28)
# define TV_TRILEVEL_SYNC		(1 << 21)
/** Enables slow sync generation (945GM only) */
# define TV_SLOW_SYNC			(1 << 20)
/** Selects 4x oversampling for 480i and 576p */
# define TV_OVERSAMPLE_4X		(0 << 18)
/** Selects 2x oversampling for 720p and 1080i */
# define TV_OVERSAMPLE_2X		(1 << 18)
/** Selects no oversampling for 1080p */
# define TV_OVERSAMPLE_NONE		(2 << 18)
/** Selects 8x oversampling */
# define TV_OVERSAMPLE_8X		(3 << 18)
/** Selects progressive mode rather than interlaced */
# define TV_PROGRESSIVE			(1 << 17)
/** Sets the colorburst to PAL mode.  Required for non-M PAL modes. */
# define TV_PAL_BURST			(1 << 16)
/** Field for setting delay of Y compared to C */
# define TV_YC_SKEW_MASK		(7 << 12)
/** Enables a fix for 480p/576p standard definition modes on the 915GM only */
# define TV_ENC_SDP_FIX			(1 << 11)
/**
 * Enables a fix for the 915GM only.
 *
 * Not sure what it does.
 */
# define TV_ENC_C0_FIX			(1 << 10)
/** Bits that must be preserved by software */
# define TV_CTL_SAVE			((1 << 11) | (3 << 9) | (7 << 6) | 0xf)
# define TV_FUSE_STATE_MASK		(3 << 4)
/** Read-only state that reports all features enabled */
# define TV_FUSE_STATE_ENABLED		(0 << 4)
/** Read-only state that reports that Macrovision is disabled in hardware*/
# define TV_FUSE_STATE_NO_MACROVISION	(1 << 4)
/** Read-only state that reports that TV-out is disabled in hardware. */
# define TV_FUSE_STATE_DISABLED		(2 << 4)
/** Normal operation */
# define TV_TEST_MODE_NORMAL		(0 << 0)
/** Encoder test pattern 1 - combo pattern */
# define TV_TEST_MODE_PATTERN_1		(1 << 0)
/** Encoder test pattern 2 - full screen vertical 75% color bars */
# define TV_TEST_MODE_PATTERN_2		(2 << 0)
/** Encoder test pattern 3 - full screen horizontal 75% color bars */
# define TV_TEST_MODE_PATTERN_3		(3 << 0)
/** Encoder test pattern 4 - random noise */
# define TV_TEST_MODE_PATTERN_4		(4 << 0)
/** Encoder test pattern 5 - linear color ramps */
# define TV_TEST_MODE_PATTERN_5		(5 << 0)
/**
 * This test mode forces the DACs to 50% of full output.
 *
 * This is used for load detection in combination with TVDAC_SENSE_MASK
 */
# define TV_TEST_MODE_MONITOR_DETECT	(7 << 0)
# define TV_TEST_MODE_MASK		(7 << 0)

#define TV_DAC			0x68004
# define TV_DAC_SAVE		0x00ffff00
/**
 * Reports that DAC state change logic has reported change (RO).
 *
 * This gets cleared when TV_DAC_STATE_EN is cleared
*/
# define TVDAC_STATE_CHG		(1 << 31)
# define TVDAC_SENSE_MASK		(7 << 28)
/** Reports that DAC A voltage is above the detect threshold */
# define TVDAC_A_SENSE			(1 << 30)
/** Reports that DAC B voltage is above the detect threshold */
# define TVDAC_B_SENSE			(1 << 29)
/** Reports that DAC C voltage is above the detect threshold */
# define TVDAC_C_SENSE			(1 << 28)
/**
 * Enables DAC state detection logic, for load-based TV detection.
 *
 * The PLL of the chosen pipe (in TV_CTL) must be running, and the encoder set
 * to off, for load detection to work.
 */
# define TVDAC_STATE_CHG_EN		(1 << 27)
/** Sets the DAC A sense value to high */
# define TVDAC_A_SENSE_CTL		(1 << 26)
/** Sets the DAC B sense value to high */
# define TVDAC_B_SENSE_CTL		(1 << 25)
/** Sets the DAC C sense value to high */
# define TVDAC_C_SENSE_CTL		(1 << 24)
/** Overrides the ENC_ENABLE and DAC voltage levels */
# define DAC_CTL_OVERRIDE		(1 << 7)
/** Sets the slew rate.  Must be preserved in software */
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

/**
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
/**
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
/**
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
/**
 * V attenuation for component video.
 *
 * Stored in 1.9 fixed point.
 */
# define TV_AV_MASK			0x000007ff
# define TV_AV_SHIFT			0

#define TV_CLR_KNOBS		0x68028
/** 2s-complement brightness adjustment */
# define TV_BRIGHTNESS_MASK		0xff000000
# define TV_BRIGHTNESS_SHIFT		24
/** Contrast adjustment, as a 2.6 unsigned floating point number */
# define TV_CONTRAST_MASK		0x00ff0000
# define TV_CONTRAST_SHIFT		16
/** Saturation adjustment, as a 2.6 unsigned floating point number */
# define TV_SATURATION_MASK		0x0000ff00
# define TV_SATURATION_SHIFT		8
/** Hue adjustment, as an integer phase angle in degrees */
# define TV_HUE_MASK			0x000000ff
# define TV_HUE_SHIFT			0

#define TV_CLR_LEVEL		0x6802c
/** Controls the DAC level for black */
# define TV_BLACK_LEVEL_MASK		0x01ff0000
# define TV_BLACK_LEVEL_SHIFT		16
/** Controls the DAC level for blanking */
# define TV_BLANK_LEVEL_MASK		0x000001ff
# define TV_BLANK_LEVEL_SHIFT		0

#define TV_H_CTL_1		0x68030
/** Number of pixels in the hsync. */
# define TV_HSYNC_END_MASK		0x1fff0000
# define TV_HSYNC_END_SHIFT		16
/** Total number of pixels minus one in the line (display and blanking). */
# define TV_HTOTAL_MASK			0x00001fff
# define TV_HTOTAL_SHIFT		0

#define TV_H_CTL_2		0x68034
/** Enables the colorburst (needed for non-component color) */
# define TV_BURST_ENA			(1 << 31)
/** Offset of the colorburst from the start of hsync, in pixels minus one. */
# define TV_HBURST_START_SHIFT		16
# define TV_HBURST_START_MASK		0x1fff0000
/** Length of the colorburst */
# define TV_HBURST_LEN_SHIFT		0
# define TV_HBURST_LEN_MASK		0x0001fff

#define TV_H_CTL_3		0x68038
/** End of hblank, measured in pixels minus one from start of hsync */
# define TV_HBLANK_END_SHIFT		16
# define TV_HBLANK_END_MASK		0x1fff0000
/** Start of hblank, measured in pixels minus one from start of hsync */
# define TV_HBLANK_START_SHIFT		0
# define TV_HBLANK_START_MASK		0x0001fff

#define TV_V_CTL_1		0x6803c
/** XXX */
# define TV_NBR_END_SHIFT		16
# define TV_NBR_END_MASK		0x07ff0000
/** XXX */
# define TV_VI_END_F1_SHIFT		8
# define TV_VI_END_F1_MASK		0x00003f00
/** XXX */
# define TV_VI_END_F2_SHIFT		0
# define TV_VI_END_F2_MASK		0x0000003f

#define TV_V_CTL_2		0x68040
/** Length of vsync, in half lines */
# define TV_VSYNC_LEN_MASK		0x07ff0000
# define TV_VSYNC_LEN_SHIFT		16
/** Offset of the start of vsync in field 1, measured in one less than the
 * number of half lines.
 */
# define TV_VSYNC_START_F1_MASK		0x00007f00
# define TV_VSYNC_START_F1_SHIFT	8
/**
 * Offset of the start of vsync in field 2, measured in one less than the
 * number of half lines.
 */
# define TV_VSYNC_START_F2_MASK		0x0000007f
# define TV_VSYNC_START_F2_SHIFT	0

#define TV_V_CTL_3		0x68044
/** Enables generation of the equalization signal */
# define TV_EQUAL_ENA			(1 << 31)
/** Length of vsync, in half lines */
# define TV_VEQ_LEN_MASK		0x007f0000
# define TV_VEQ_LEN_SHIFT		16
/** Offset of the start of equalization in field 1, measured in one less than
 * the number of half lines.
 */
# define TV_VEQ_START_F1_MASK		0x0007f00
# define TV_VEQ_START_F1_SHIFT		8
/**
 * Offset of the start of equalization in field 2, measured in one less than
 * the number of half lines.
 */
# define TV_VEQ_START_F2_MASK		0x000007f
# define TV_VEQ_START_F2_SHIFT		0

#define TV_V_CTL_4		0x68048
/**
 * Offset to start of vertical colorburst, measured in one less than the
 * number of lines from vertical start.
 */
# define TV_VBURST_START_F1_MASK	0x003f0000
# define TV_VBURST_START_F1_SHIFT	16
/**
 * Offset to the end of vertical colorburst, measured in one less than the
 * number of lines from the start of NBR.
 */
# define TV_VBURST_END_F1_MASK		0x000000ff
# define TV_VBURST_END_F1_SHIFT		0

#define TV_V_CTL_5		0x6804c
/**
 * Offset to start of vertical colorburst, measured in one less than the
 * number of lines from vertical start.
 */
# define TV_VBURST_START_F2_MASK	0x003f0000
# define TV_VBURST_START_F2_SHIFT	16
/**
 * Offset to the end of vertical colorburst, measured in one less than the
 * number of lines from the start of NBR.
 */
# define TV_VBURST_END_F2_MASK		0x000000ff
# define TV_VBURST_END_F2_SHIFT		0

#define TV_V_CTL_6		0x68050
/**
 * Offset to start of vertical colorburst, measured in one less than the
 * number of lines from vertical start.
 */
# define TV_VBURST_START_F3_MASK	0x003f0000
# define TV_VBURST_START_F3_SHIFT	16
/**
 * Offset to the end of vertical colorburst, measured in one less than the
 * number of lines from the start of NBR.
 */
# define TV_VBURST_END_F3_MASK		0x000000ff
# define TV_VBURST_END_F3_SHIFT		0

#define TV_V_CTL_7		0x68054
/**
 * Offset to start of vertical colorburst, measured in one less than the
 * number of lines from vertical start.
 */
# define TV_VBURST_START_F4_MASK	0x003f0000
# define TV_VBURST_START_F4_SHIFT	16
/**
 * Offset to the end of vertical colorburst, measured in one less than the
 * number of lines from the start of NBR.
 */
# define TV_VBURST_END_F4_MASK		0x000000ff
# define TV_VBURST_END_F4_SHIFT		0

#define TV_SC_CTL_1		0x68060
/** Turns on the first subcarrier phase generation DDA */
# define TV_SC_DDA1_EN			(1 << 31)
/** Turns on the first subcarrier phase generation DDA */
# define TV_SC_DDA2_EN			(1 << 30)
/** Turns on the first subcarrier phase generation DDA */
# define TV_SC_DDA3_EN			(1 << 29)
/** Sets the subcarrier DDA to reset frequency every other field */
# define TV_SC_RESET_EVERY_2		(0 << 24)
/** Sets the subcarrier DDA to reset frequency every fourth field */
# define TV_SC_RESET_EVERY_4		(1 << 24)
/** Sets the subcarrier DDA to reset frequency every eighth field */
# define TV_SC_RESET_EVERY_8		(2 << 24)
/** Sets the subcarrier DDA to never reset the frequency */
# define TV_SC_RESET_NEVER		(3 << 24)
/** Sets the peak amplitude of the colorburst.*/
# define TV_BURST_LEVEL_MASK		0x00ff0000
# define TV_BURST_LEVEL_SHIFT		16
/** Sets the increment of the first subcarrier phase generation DDA */
# define TV_SCDDA1_INC_MASK		0x00000fff
# define TV_SCDDA1_INC_SHIFT		0

#define TV_SC_CTL_2		0x68064
/** Sets the rollover for the second subcarrier phase generation DDA */
# define TV_SCDDA2_SIZE_MASK		0x7fff0000
# define TV_SCDDA2_SIZE_SHIFT		16
/** Sets the increent of the second subcarrier phase generation DDA */
# define TV_SCDDA2_INC_MASK		0x00007fff
# define TV_SCDDA2_INC_SHIFT		0

#define TV_SC_CTL_3		0x68068
/** Sets the rollover for the third subcarrier phase generation DDA */
# define TV_SCDDA3_SIZE_MASK		0x7fff0000
# define TV_SCDDA3_SIZE_SHIFT		16
/** Sets the increent of the third subcarrier phase generation DDA */
# define TV_SCDDA3_INC_MASK		0x00007fff
# define TV_SCDDA3_INC_SHIFT		0

#define TV_WIN_POS		0x68070
/** X coordinate of the display from the start of horizontal active */
# define TV_XPOS_MASK			0x1fff0000
# define TV_XPOS_SHIFT			16
/** Y coordinate of the display from the start of vertical active (NBR) */
# define TV_YPOS_MASK			0x00000fff
# define TV_YPOS_SHIFT			0

#define TV_WIN_SIZE		0x68074
/** Horizontal size of the display window, measured in pixels*/
# define TV_XSIZE_MASK			0x1fff0000
# define TV_XSIZE_SHIFT			16
/**
 * Vertical size of the display window, measured in pixels.
 *
 * Must be even for interlaced modes.
 */
# define TV_YSIZE_MASK			0x00000fff
# define TV_YSIZE_SHIFT			0

#define TV_FILTER_CTL_1		0x68080
/**
 * Enables automatic scaling calculation.
 *
 * If set, the rest of the registers are ignored, and the calculated values can
 * be read back from the register.
 */
# define TV_AUTO_SCALE			(1 << 31)
/**
 * Disables the vertical filter.
 *
 * This is required on modes more than 1024 pixels wide */
# define TV_V_FILTER_BYPASS		(1 << 29)
/** Enables adaptive vertical filtering */
# define TV_VADAPT			(1 << 28)
# define TV_VADAPT_MODE_MASK		(3 << 26)
/** Selects the least adaptive vertical filtering mode */
# define TV_VADAPT_MODE_LEAST		(0 << 26)
/** Selects the moderately adaptive vertical filtering mode */
# define TV_VADAPT_MODE_MODERATE	(1 << 26)
/** Selects the most adaptive vertical filtering mode */
# define TV_VADAPT_MODE_MOST		(3 << 26)
/**
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
/**
 * Sets the integer part of the 3.15 fixed-point vertical scaling factor.
 *
 * TV_VSCALE should be (src height - 1) / ((interlace * dest height) - 1)
 */
# define TV_VSCALE_INT_MASK		0x00038000
# define TV_VSCALE_INT_SHIFT		15
/**
 * Sets the fractional part of the 3.15 fixed-point vertical scaling factor.
 *
 * \sa TV_VSCALE_INT_MASK
 */
# define TV_VSCALE_FRAC_MASK		0x00007fff
# define TV_VSCALE_FRAC_SHIFT		0

#define TV_FILTER_CTL_3		0x68088
/**
 * Sets the integer part of the 3.15 fixed-point vertical scaling factor.
 *
 * TV_VSCALE should be (src height - 1) / (1/4 * (dest height - 1))
 *
 * For progressive modes, TV_VSCALE_IP_INT should be set to zeroes.
 */
# define TV_VSCALE_IP_INT_MASK		0x00038000
# define TV_VSCALE_IP_INT_SHIFT		15
/**
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
/**
 * Specifies which field to send the CC data in.
 *
 * CC data is usually sent in field 0.
 */
# define TV_CC_FID_MASK			(1 << 27)
# define TV_CC_FID_SHIFT		27
/** Sets the horizontal position of the CC data.  Usually 135. */
# define TV_CC_HOFF_MASK		0x03ff0000
# define TV_CC_HOFF_SHIFT		16
/** Sets the vertical position of the CC data.  Usually 21 */
# define TV_CC_LINE_MASK		0x0000003f
# define TV_CC_LINE_SHIFT		0

#define TV_CC_DATA		0x68094
# define TV_CC_RDY			(1 << 31)
/** Second word of CC data to be transmitted. */
# define TV_CC_DATA_2_MASK		0x007f0000
# define TV_CC_DATA_2_SHIFT		16
/** First word of CC data to be transmitted. */
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

/* Link training mode - select a suitable mode for each stage */
#define   DP_LINK_TRAIN_PAT_1		(0 << 28)
#define   DP_LINK_TRAIN_PAT_2		(1 << 28)
#define   DP_LINK_TRAIN_PAT_IDLE	(2 << 28)
#define   DP_LINK_TRAIN_OFF		(3 << 28)
#define   DP_LINK_TRAIN_MASK		(3 << 28)
#define   DP_LINK_TRAIN_SHIFT		28

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
#define   DP_PORT_WIDTH_1		(0 << 19)
#define   DP_PORT_WIDTH_2		(1 << 19)
#define   DP_PORT_WIDTH_4		(3 << 19)
#define   DP_PORT_WIDTH_MASK		(7 << 19)

/* Mystic DPCD version 1.1 special mode */
#define   DP_ENHANCED_FRAMING		(1 << 18)

/* eDP */
#define   DP_PLL_FREQ_270MHZ		(0 << 16)
#define   DP_PLL_FREQ_160MHZ		(1 << 16)
#define   DP_PLL_FREQ_MASK		(3 << 16)

/** locked once port is enabled */
#define   DP_PORT_REVERSAL		(1 << 15)

/* eDP */
#define   DP_PLL_ENABLE			(1 << 14)

/** sends the clock on lane 15 of the PEG for debug */
#define   DP_CLOCK_OUTPUT_ENABLE	(1 << 13)

#define   DP_SCRAMBLING_DISABLE		(1 << 12)
#define   DP_SCRAMBLING_DISABLE_IRONLAKE	(1 << 7)

/** limit RGB values to avoid confusing TVs */
#define   DP_COLOR_RANGE_16_235		(1 << 8)

/** Turn on the audio link */
#define   DP_AUDIO_OUTPUT_ENABLE	(1 << 6)

/** vs and hs sync polarity */
#define   DP_SYNC_VS_HIGH		(1 << 4)
#define   DP_SYNC_HS_HIGH		(1 << 3)

/** A fantasy */
#define   DP_DETECTED			(1 << 2)

/** The aux channel provides a way to talk to the
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
#define _PIPEA_GMCH_DATA_M			0x70050
#define _PIPEB_GMCH_DATA_M			0x71050

/* Transfer unit size for display port - 1, default is 0x3f (for TU size 64) */
#define   PIPE_GMCH_DATA_M_TU_SIZE_MASK		(0x3f << 25)
#define   PIPE_GMCH_DATA_M_TU_SIZE_SHIFT	25

#define   PIPE_GMCH_DATA_M_MASK			(0xffffff)

#define _PIPEA_GMCH_DATA_N			0x70054
#define _PIPEB_GMCH_DATA_N			0x71054
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

#define _PIPEA_DP_LINK_M				0x70060
#define _PIPEB_DP_LINK_M				0x71060
#define   PIPEA_DP_LINK_M_MASK			(0xffffff)

#define _PIPEA_DP_LINK_N				0x70064
#define _PIPEB_DP_LINK_N				0x71064
#define   PIPEA_DP_LINK_N_MASK			(0xffffff)

#define PIPE_GMCH_DATA_M(pipe) _PIPE(pipe, _PIPEA_GMCH_DATA_M, _PIPEB_GMCH_DATA_M)
#define PIPE_GMCH_DATA_N(pipe) _PIPE(pipe, _PIPEA_GMCH_DATA_N, _PIPEB_GMCH_DATA_N)
#define PIPE_DP_LINK_M(pipe) _PIPE(pipe, _PIPEA_DP_LINK_M, _PIPEB_DP_LINK_M)
#define PIPE_DP_LINK_N(pipe) _PIPE(pipe, _PIPEA_DP_LINK_N, _PIPEB_DP_LINK_N)

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
#define   PIPECONF_CXSR_DOWNCLOCK	(1<<16)
#define   PIPECONF_BPP_MASK	(0x000000e0)
#define   PIPECONF_BPP_8	(0<<5)
#define   PIPECONF_BPP_10	(1<<5)
#define   PIPECONF_BPP_6	(2<<5)
#define   PIPECONF_BPP_12	(3<<5)
#define   PIPECONF_DITHER_EN	(1<<4)
#define   PIPECONF_DITHER_TYPE_MASK (0x0000000c)
#define   PIPECONF_DITHER_TYPE_SP (0<<2)
#define   PIPECONF_DITHER_TYPE_ST1 (1<<2)
#define   PIPECONF_DITHER_TYPE_ST2 (2<<2)
#define   PIPECONF_DITHER_TYPE_TEMP (3<<2)
#define _PIPEASTAT		0x70024
#define   PIPE_FIFO_UNDERRUN_STATUS		(1UL<<31)
#define   SPRITE1_FLIPDONE_INT_EN_VLV		(1UL<<30)
#define   PIPE_CRC_ERROR_ENABLE			(1UL<<29)
#define   PIPE_CRC_DONE_ENABLE			(1UL<<28)
#define   PIPE_GMBUS_EVENT_ENABLE		(1UL<<27)
#define   PLANE_FLIP_DONE_INT_EN_VLV		(1UL<<26)
#define   PIPE_HOTPLUG_INTERRUPT_ENABLE		(1UL<<26)
#define   PIPE_VSYNC_INTERRUPT_ENABLE		(1UL<<25)
#define   PIPE_DISPLAY_LINE_COMPARE_ENABLE	(1UL<<24)
#define   PIPE_DPST_EVENT_ENABLE		(1UL<<23)
#define   SPRITE0_FLIP_DONE_INT_EN_VLV		(1UL<<26)
#define   PIPE_LEGACY_BLC_EVENT_ENABLE		(1UL<<22)
#define   PIPE_ODD_FIELD_INTERRUPT_ENABLE	(1UL<<21)
#define   PIPE_EVEN_FIELD_INTERRUPT_ENABLE	(1UL<<20)
#define   PIPE_HOTPLUG_TV_INTERRUPT_ENABLE	(1UL<<18) /* pre-965 */
#define   PIPE_START_VBLANK_INTERRUPT_ENABLE	(1UL<<18) /* 965 or later */
#define   PIPE_VBLANK_INTERRUPT_ENABLE		(1UL<<17)
#define   PIPEA_HBLANK_INT_EN_VLV		(1UL<<16)
#define   PIPE_OVERLAY_UPDATED_ENABLE		(1UL<<16)
#define   SPRITE1_FLIPDONE_INT_STATUS_VLV	(1UL<<15)
#define   SPRITE0_FLIPDONE_INT_STATUS_VLV	(1UL<<15)
#define   PIPE_CRC_ERROR_INTERRUPT_STATUS	(1UL<<13)
#define   PIPE_CRC_DONE_INTERRUPT_STATUS	(1UL<<12)
#define   PIPE_GMBUS_INTERRUPT_STATUS		(1UL<<11)
#define   PLANE_FLIPDONE_INT_STATUS_VLV		(1UL<<10)
#define   PIPE_HOTPLUG_INTERRUPT_STATUS		(1UL<<10)
#define   PIPE_VSYNC_INTERRUPT_STATUS		(1UL<<9)
#define   PIPE_DISPLAY_LINE_COMPARE_STATUS	(1UL<<8)
#define   PIPE_DPST_EVENT_STATUS		(1UL<<7)
#define   PIPE_LEGACY_BLC_EVENT_STATUS		(1UL<<6)
#define   PIPE_ODD_FIELD_INTERRUPT_STATUS	(1UL<<5)
#define   PIPE_EVEN_FIELD_INTERRUPT_STATUS	(1UL<<4)
#define   PIPE_HOTPLUG_TV_INTERRUPT_STATUS	(1UL<<2) /* pre-965 */
#define   PIPE_START_VBLANK_INTERRUPT_STATUS	(1UL<<2) /* 965 or later */
#define   PIPE_VBLANK_INTERRUPT_STATUS		(1UL<<1)
#define   PIPE_OVERLAY_UPDATED_STATUS		(1UL<<0)
#define   PIPE_BPC_MASK				(7 << 5) /* Ironlake */
#define   PIPE_8BPC				(0 << 5)
#define   PIPE_10BPC				(1 << 5)
#define   PIPE_6BPC				(2 << 5)
#define   PIPE_12BPC				(3 << 5)

#define PIPESRC(pipe) _PIPE(pipe, _PIPEASRC, _PIPEBSRC)
#define PIPECONF(tran) _TRANSCODER(tran, _PIPEACONF, _PIPEBCONF)
#define PIPEDSL(pipe)  _PIPE(pipe, _PIPEADSL, _PIPEBDSL)
#define PIPEFRAME(pipe) _PIPE(pipe, _PIPEAFRAMEHIGH, _PIPEBFRAMEHIGH)
#define PIPEFRAMEPIXEL(pipe)  _PIPE(pipe, _PIPEAFRAMEPIXEL, _PIPEBFRAMEPIXEL)
#define PIPESTAT(pipe) _PIPE(pipe, _PIPEASTAT, _PIPEBSTAT)

#define VLV_DPFLIPSTAT				0x70028
#define   PIPEB_LINE_COMPARE_INT_EN		(1<<29)
#define   PIPEB_HLINE_INT_EN			(1<<28)
#define   PIPEB_VBLANK_INT_EN			(1<<27)
#define   SPRITED_FLIPDONE_INT_EN		(1<<26)
#define   SPRITEC_FLIPDONE_INT_EN		(1<<25)
#define   PLANEB_FLIPDONE_INT_EN		(1<<24)
#define   PIPEA_LINE_COMPARE_INT_EN		(1<<21)
#define   PIPEA_HLINE_INT_EN			(1<<20)
#define   PIPEA_VBLANK_INT_EN			(1<<19)
#define   SPRITEB_FLIPDONE_INT_EN		(1<<18)
#define   SPRITEA_FLIPDONE_INT_EN		(1<<17)
#define   PLANEA_FLIPDONE_INT_EN		(1<<16)

#define DPINVGTT				0x7002c /* VLV only */
#define   CURSORB_INVALID_GTT_INT_EN		(1<<23)
#define   CURSORA_INVALID_GTT_INT_EN		(1<<22)
#define   SPRITED_INVALID_GTT_INT_EN		(1<<21)
#define   SPRITEC_INVALID_GTT_INT_EN		(1<<20)
#define   PLANEB_INVALID_GTT_INT_EN		(1<<19)
#define   SPRITEB_INVALID_GTT_INT_EN		(1<<18)
#define   SPRITEA_INVALID_GTT_INT_EN		(1<<17)
#define   PLANEA_INVALID_GTT_INT_EN		(1<<16)
#define   DPINVGTT_EN_MASK			0xff0000
#define   CURSORB_INVALID_GTT_STATUS		(1<<7)
#define   CURSORA_INVALID_GTT_STATUS		(1<<6)
#define   SPRITED_INVALID_GTT_STATUS		(1<<5)
#define   SPRITEC_INVALID_GTT_STATUS		(1<<4)
#define   PLANEB_INVALID_GTT_STATUS		(1<<3)
#define   SPRITEB_INVALID_GTT_STATUS		(1<<2)
#define   SPRITEA_INVALID_GTT_STATUS		(1<<1)
#define   PLANEA_INVALID_GTT_STATUS		(1<<0)
#define   DPINVGTT_STATUS_MASK			0xff

#define DSPARB			0x70030
#define   DSPARB_CSTART_MASK	(0x7f << 7)
#define   DSPARB_CSTART_SHIFT	7
#define   DSPARB_BSTART_MASK	(0x7f)
#define   DSPARB_BSTART_SHIFT	0
#define   DSPARB_BEND_SHIFT	9 /* on 855 */
#define   DSPARB_AEND_SHIFT	0

#define DSPFW1			0x70034
#define   DSPFW_SR_SHIFT	23
#define   DSPFW_SR_MASK		(0x1ff<<23)
#define   DSPFW_CURSORB_SHIFT	16
#define   DSPFW_CURSORB_MASK	(0x3f<<16)
#define   DSPFW_PLANEB_SHIFT	8
#define   DSPFW_PLANEB_MASK	(0x7f<<8)
#define   DSPFW_PLANEA_MASK	(0x7f)
#define DSPFW2			0x70038
#define   DSPFW_CURSORA_MASK	0x00003f00
#define   DSPFW_CURSORA_SHIFT	8
#define   DSPFW_PLANEC_MASK	(0x7f)
#define DSPFW3			0x7003c
#define   DSPFW_HPLL_SR_EN	(1<<31)
#define   DSPFW_CURSOR_SR_SHIFT	24
#define   PINEVIEW_SELF_REFRESH_EN	(1<<30)
#define   DSPFW_CURSOR_SR_MASK		(0x3f<<24)
#define   DSPFW_HPLL_CURSOR_SHIFT	16
#define   DSPFW_HPLL_CURSOR_MASK	(0x3f<<16)
#define   DSPFW_HPLL_SR_MASK		(0x1ff)

/* drain latency register values*/
#define DRAIN_LATENCY_PRECISION_32	32
#define DRAIN_LATENCY_PRECISION_16	16
#define VLV_DDL1			0x70050
#define DDL_CURSORA_PRECISION_32	(1<<31)
#define DDL_CURSORA_PRECISION_16	(0<<31)
#define DDL_CURSORA_SHIFT		24
#define DDL_PLANEA_PRECISION_32		(1<<7)
#define DDL_PLANEA_PRECISION_16		(0<<7)
#define VLV_DDL2			0x70054
#define DDL_CURSORB_PRECISION_32	(1<<31)
#define DDL_CURSORB_PRECISION_16	(0<<31)
#define DDL_CURSORB_SHIFT		24
#define DDL_PLANEB_PRECISION_32		(1<<7)
#define DDL_PLANEB_PRECISION_16		(0<<7)

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

/* define the Watermark register on Ironlake */
#define WM0_PIPEA_ILK		0x45100
#define  WM0_PIPE_PLANE_MASK	(0x7f<<16)
#define  WM0_PIPE_PLANE_SHIFT	16
#define  WM0_PIPE_SPRITE_MASK	(0x3f<<8)
#define  WM0_PIPE_SPRITE_SHIFT	8
#define  WM0_PIPE_CURSOR_MASK	(0x1f)

#define WM0_PIPEB_ILK		0x45104
#define WM0_PIPEC_IVB		0x45200
#define WM1_LP_ILK		0x45108
#define  WM1_LP_SR_EN		(1<<31)
#define  WM1_LP_LATENCY_SHIFT	24
#define  WM1_LP_LATENCY_MASK	(0x7f<<24)
#define  WM1_LP_FBC_MASK	(0xf<<20)
#define  WM1_LP_FBC_SHIFT	20
#define  WM1_LP_SR_MASK		(0x1ff<<8)
#define  WM1_LP_SR_SHIFT	8
#define  WM1_LP_CURSOR_MASK	(0x3f)
#define WM2_LP_ILK		0x4510c
#define  WM2_LP_EN		(1<<31)
#define WM3_LP_ILK		0x45110
#define  WM3_LP_EN		(1<<31)
#define WM1S_LP_ILK		0x45120
#define WM2S_LP_IVB		0x45124
#define WM3S_LP_IVB		0x45128
#define  WM1S_LP_EN		(1<<31)

/* Memory latency timer register */
#define MLTR_ILK		0x11222
#define  MLTR_WM1_SHIFT		0
#define  MLTR_WM2_SHIFT		8
/* the unit of memory self-refresh latency time is 0.5us */
#define  ILK_SRLT_MASK		0x3f
#define ILK_LATENCY(shift)	(I915_READ(MLTR_ILK) >> (shift) & ILK_SRLT_MASK)
#define ILK_READ_WM1_LATENCY()	ILK_LATENCY(MLTR_WM1_SHIFT)
#define ILK_READ_WM2_LATENCY()	ILK_LATENCY(MLTR_WM2_SHIFT)

/* define the fifo size on Ironlake */
#define ILK_DISPLAY_FIFO	128
#define ILK_DISPLAY_MAXWM	64
#define ILK_DISPLAY_DFTWM	8
#define ILK_CURSOR_FIFO		32
#define ILK_CURSOR_MAXWM	16
#define ILK_CURSOR_DFTWM	8

#define ILK_DISPLAY_SR_FIFO	512
#define ILK_DISPLAY_MAX_SRWM	0x1ff
#define ILK_DISPLAY_DFT_SRWM	0x3f
#define ILK_CURSOR_SR_FIFO	64
#define ILK_CURSOR_MAX_SRWM	0x3f
#define ILK_CURSOR_DFT_SRWM	8

#define ILK_FIFO_LINE_SIZE	64

/* define the WM info on Sandybridge */
#define SNB_DISPLAY_FIFO	128
#define SNB_DISPLAY_MAXWM	0x7f	/* bit 16:22 */
#define SNB_DISPLAY_DFTWM	8
#define SNB_CURSOR_FIFO		32
#define SNB_CURSOR_MAXWM	0x1f	/* bit 4:0 */
#define SNB_CURSOR_DFTWM	8

#define SNB_DISPLAY_SR_FIFO	512
#define SNB_DISPLAY_MAX_SRWM	0x1ff	/* bit 16:8 */
#define SNB_DISPLAY_DFT_SRWM	0x3f
#define SNB_CURSOR_SR_FIFO	64
#define SNB_CURSOR_MAX_SRWM	0x3f	/* bit 5:0 */
#define SNB_CURSOR_DFT_SRWM	8

#define SNB_FBC_MAX_SRWM	0xf	/* bit 23:20 */

#define SNB_FIFO_LINE_SIZE	64


/* the address where we get all kinds of latency value */
#define SSKPD			0x5d10
#define SSKPD_WM_MASK		0x3f
#define SSKPD_WM0_SHIFT		0
#define SSKPD_WM1_SHIFT		8
#define SSKPD_WM2_SHIFT		16
#define SSKPD_WM3_SHIFT		24

#define SNB_LATENCY(shift)	(I915_READ(MCHBAR_MIRROR_BASE_SNB + SSKPD) >> (shift) & SSKPD_WM_MASK)
#define SNB_READ_WM0_LATENCY()		SNB_LATENCY(SSKPD_WM0_SHIFT)
#define SNB_READ_WM1_LATENCY()		SNB_LATENCY(SSKPD_WM1_SHIFT)
#define SNB_READ_WM2_LATENCY()		SNB_LATENCY(SSKPD_WM2_SHIFT)
#define SNB_READ_WM3_LATENCY()		SNB_LATENCY(SSKPD_WM3_SHIFT)

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
#define PIPE_FRMCOUNT_GM45(pipe) _PIPE(pipe, _PIPEA_FRMCOUNT_GM45, _PIPEB_FRMCOUNT_GM45)

/* Cursor A & B regs */
#define _CURACNTR		0x70080
/* Old style CUR*CNTR flags (desktop 8xx) */
#define   CURSOR_ENABLE		0x80000000
#define   CURSOR_GAMMA_ENABLE	0x40000000
#define   CURSOR_STRIDE_MASK	0x30000000
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
#define   CURSOR_MODE_64_32B_AX 0x07
#define   CURSOR_MODE_64_ARGB_AX ((1 << 5) | CURSOR_MODE_64_32B_AX)
#define   MCURSOR_PIPE_SELECT	(1 << 28)
#define   MCURSOR_PIPE_A	0x00
#define   MCURSOR_PIPE_B	(1 << 28)
#define   MCURSOR_GAMMA_ENABLE  (1 << 26)
#define _CURABASE		0x70084
#define _CURAPOS			0x70088
#define   CURSOR_POS_MASK       0x007FF
#define   CURSOR_POS_SIGN       0x8000
#define   CURSOR_X_SHIFT        0
#define   CURSOR_Y_SHIFT        16
#define CURSIZE			0x700a0
#define _CURBCNTR		0x700c0
#define _CURBBASE		0x700c4
#define _CURBPOS			0x700c8

#define _CURBCNTR_IVB		0x71080
#define _CURBBASE_IVB		0x71084
#define _CURBPOS_IVB		0x71088

#define CURCNTR(pipe) _PIPE(pipe, _CURACNTR, _CURBCNTR)
#define CURBASE(pipe) _PIPE(pipe, _CURABASE, _CURBBASE)
#define CURPOS(pipe) _PIPE(pipe, _CURAPOS, _CURBPOS)

#define CURCNTR_IVB(pipe) _PIPE(pipe, _CURACNTR, _CURBCNTR_IVB)
#define CURBASE_IVB(pipe) _PIPE(pipe, _CURABASE, _CURBBASE_IVB)
#define CURPOS_IVB(pipe) _PIPE(pipe, _CURAPOS, _CURBPOS_IVB)

/* Display A control */
#define _DSPACNTR                0x70180
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
#define   DISPPLANE_TRICKLE_FEED_DISABLE	(1<<14) /* Ironlake */
#define   DISPPLANE_TILED			(1<<10)
#define _DSPAADDR		0x70184
#define _DSPASTRIDE		0x70188
#define _DSPAPOS			0x7018C /* reserved */
#define _DSPASIZE		0x70190
#define _DSPASURF		0x7019C /* 965+ only */
#define _DSPATILEOFF		0x701A4 /* 965+ only */
#define _DSPAOFFSET		0x701A4 /* HSW */
#define _DSPASURFLIVE		0x701AC

#define DSPCNTR(plane) _PIPE(plane, _DSPACNTR, _DSPBCNTR)
#define DSPADDR(plane) _PIPE(plane, _DSPAADDR, _DSPBADDR)
#define DSPSTRIDE(plane) _PIPE(plane, _DSPASTRIDE, _DSPBSTRIDE)
#define DSPPOS(plane) _PIPE(plane, _DSPAPOS, _DSPBPOS)
#define DSPSIZE(plane) _PIPE(plane, _DSPASIZE, _DSPBSIZE)
#define DSPSURF(plane) _PIPE(plane, _DSPASURF, _DSPBSURF)
#define DSPTILEOFF(plane) _PIPE(plane, _DSPATILEOFF, _DSPBTILEOFF)
#define DSPLINOFF(plane) DSPADDR(plane)
#define DSPOFFSET(plane) _PIPE(plane, _DSPAOFFSET, _DSPBOFFSET)
#define DSPSURFLIVE(plane) _PIPE(plane, _DSPASURFLIVE, _DSPBSURFLIVE)

/* Display/Sprite base address macros */
#define DISP_BASEADDR_MASK	(0xfffff000)
#define I915_LO_DISPBASE(val)	(val & ~DISP_BASEADDR_MASK)
#define I915_HI_DISPBASE(val)	(val & DISP_BASEADDR_MASK)
#define I915_MODIFY_DISPBASE(reg, gfx_addr) \
		(I915_WRITE((reg), (gfx_addr) | I915_LO_DISPBASE(I915_READ(reg))))

/* VBIOS flags */
#define SWF00			0x71410
#define SWF01			0x71414
#define SWF02			0x71418
#define SWF03			0x7141c
#define SWF04			0x71420
#define SWF05			0x71424
#define SWF06			0x71428
#define SWF10			0x70410
#define SWF11			0x70414
#define SWF14			0x71420
#define SWF30			0x72414
#define SWF31			0x72418
#define SWF32			0x7241c

/* Pipe B */
#define _PIPEBDSL		0x71000
#define _PIPEBCONF		0x71008
#define _PIPEBSTAT		0x71024
#define _PIPEBFRAMEHIGH		0x71040
#define _PIPEBFRAMEPIXEL		0x71044
#define _PIPEB_FRMCOUNT_GM45	0x71040
#define _PIPEB_FLIPCOUNT_GM45	0x71044


/* Display B control */
#define _DSPBCNTR		0x71180
#define   DISPPLANE_ALPHA_TRANS_ENABLE		(1<<15)
#define   DISPPLANE_ALPHA_TRANS_DISABLE		0
#define   DISPPLANE_SPRITE_ABOVE_DISPLAY	0
#define   DISPPLANE_SPRITE_ABOVE_OVERLAY	(1)
#define _DSPBADDR		0x71184
#define _DSPBSTRIDE		0x71188
#define _DSPBPOS			0x7118C
#define _DSPBSIZE		0x71190
#define _DSPBSURF		0x7119C
#define _DSPBTILEOFF		0x711A4
#define _DSPBOFFSET		0x711A4
#define _DSPBSURFLIVE		0x711AC

/* Sprite A control */
#define _DVSACNTR		0x72180
#define   DVS_ENABLE		(1<<31)
#define   DVS_GAMMA_ENABLE	(1<<30)
#define   DVS_PIXFORMAT_MASK	(3<<25)
#define   DVS_FORMAT_YUV422	(0<<25)
#define   DVS_FORMAT_RGBX101010	(1<<25)
#define   DVS_FORMAT_RGBX888	(2<<25)
#define   DVS_FORMAT_RGBX161616	(3<<25)
#define   DVS_SOURCE_KEY	(1<<22)
#define   DVS_RGB_ORDER_XBGR	(1<<20)
#define   DVS_YUV_BYTE_ORDER_MASK (3<<16)
#define   DVS_YUV_ORDER_YUYV	(0<<16)
#define   DVS_YUV_ORDER_UYVY	(1<<16)
#define   DVS_YUV_ORDER_YVYU	(2<<16)
#define   DVS_YUV_ORDER_VYUY	(3<<16)
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
#define   SPRITE_CSC_ENABLE		(1<<24)
#define   SPRITE_SOURCE_KEY		(1<<22)
#define   SPRITE_RGB_ORDER_RGBX		(1<<20) /* only for 888 and 161616 */
#define   SPRITE_YUV_TO_RGB_CSC_DISABLE	(1<<19)
#define   SPRITE_YUV_CSC_FORMAT_BT709	(1<<18) /* 0 is BT601 */
#define   SPRITE_YUV_BYTE_ORDER_MASK	(3<<16)
#define   SPRITE_YUV_ORDER_YUYV		(0<<16)
#define   SPRITE_YUV_ORDER_UYVY		(1<<16)
#define   SPRITE_YUV_ORDER_YVYU		(2<<16)
#define   SPRITE_YUV_ORDER_VYUY		(3<<16)
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

/* VBIOS regs */
#define VGACNTRL		0x71400
# define VGA_DISP_DISABLE			(1 << 31)
# define VGA_2X_MODE				(1 << 30)
# define VGA_PIPE_B_SELECT			(1 << 29)

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


#define _PIPEA_DATA_M1           0x60030
#define  TU_SIZE(x)             (((x)-1) << 25) /* default size 64 */
#define  TU_SIZE_MASK           0x7e000000
#define  PIPE_DATA_M1_OFFSET    0
#define _PIPEA_DATA_N1           0x60034
#define  PIPE_DATA_N1_OFFSET    0

#define _PIPEA_DATA_M2           0x60038
#define  PIPE_DATA_M2_OFFSET    0
#define _PIPEA_DATA_N2           0x6003c
#define  PIPE_DATA_N2_OFFSET    0

#define _PIPEA_LINK_M1           0x60040
#define  PIPE_LINK_M1_OFFSET    0
#define _PIPEA_LINK_N1           0x60044
#define  PIPE_LINK_N1_OFFSET    0

#define _PIPEA_LINK_M2           0x60048
#define  PIPE_LINK_M2_OFFSET    0
#define _PIPEA_LINK_N2           0x6004c
#define  PIPE_LINK_N2_OFFSET    0

/* PIPEB timing regs are same start from 0x61000 */

#define _PIPEB_DATA_M1           0x61030
#define _PIPEB_DATA_N1           0x61034

#define _PIPEB_DATA_M2           0x61038
#define _PIPEB_DATA_N2           0x6103c

#define _PIPEB_LINK_M1           0x61040
#define _PIPEB_LINK_N1           0x61044

#define _PIPEB_LINK_M2           0x61048
#define _PIPEB_LINK_N2           0x6104c

#define PIPE_DATA_M1(tran) _TRANSCODER(tran, _PIPEA_DATA_M1, _PIPEB_DATA_M1)
#define PIPE_DATA_N1(tran) _TRANSCODER(tran, _PIPEA_DATA_N1, _PIPEB_DATA_N1)
#define PIPE_DATA_M2(tran) _TRANSCODER(tran, _PIPEA_DATA_M2, _PIPEB_DATA_M2)
#define PIPE_DATA_N2(tran) _TRANSCODER(tran, _PIPEA_DATA_N2, _PIPEB_DATA_N2)
#define PIPE_LINK_M1(tran) _TRANSCODER(tran, _PIPEA_LINK_M1, _PIPEB_LINK_M1)
#define PIPE_LINK_N1(tran) _TRANSCODER(tran, _PIPEA_LINK_N1, _PIPEB_LINK_N1)
#define PIPE_LINK_M2(tran) _TRANSCODER(tran, _PIPEA_LINK_M2, _PIPEB_LINK_M2)
#define PIPE_LINK_N2(tran) _TRANSCODER(tran, _PIPEA_LINK_N2, _PIPEB_LINK_N2)

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

/* legacy palette */
#define _LGC_PALETTE_A           0x4a000
#define _LGC_PALETTE_B           0x4a800
#define LGC_PALETTE(pipe) _PIPE(pipe, _LGC_PALETTE_A, _LGC_PALETTE_B)

/* interrupts */
#define DE_MASTER_IRQ_CONTROL   (1 << 31)
#define DE_SPRITEB_FLIP_DONE    (1 << 29)
#define DE_SPRITEA_FLIP_DONE    (1 << 28)
#define DE_PLANEB_FLIP_DONE     (1 << 27)
#define DE_PLANEA_FLIP_DONE     (1 << 26)
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
#define DE_PIPEB_FIFO_UNDERRUN  (1 << 8)
#define DE_PIPEA_VBLANK         (1 << 7)
#define DE_PIPEA_EVEN_FIELD     (1 << 6)
#define DE_PIPEA_ODD_FIELD      (1 << 5)
#define DE_PIPEA_LINE_COMPARE   (1 << 4)
#define DE_PIPEA_VSYNC          (1 << 3)
#define DE_PIPEA_FIFO_UNDERRUN  (1 << 0)

/* More Ivybridge lolz */
#define DE_ERR_DEBUG_IVB		(1<<30)
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
#define DE_PIPEA_VBLANK_IVB		(1<<0)

#define VLV_MASTER_IER			0x4400c /* Gunit master IER */
#define   MASTER_INTERRUPT_ENABLE	(1<<31)

#define DEISR   0x44000
#define DEIMR   0x44004
#define DEIIR   0x44008
#define DEIER   0x4400c

/* GT interrupt.
 * Note that for gen6+ the ring-specific interrupt bits do alias with the
 * corresponding bits in the per-ring interrupt control registers. */
#define GT_GEN6_BLT_FLUSHDW_NOTIFY_INTERRUPT	(1 << 26)
#define GT_GEN6_BLT_CS_ERROR_INTERRUPT		(1 << 25)
#define GT_GEN6_BLT_USER_INTERRUPT		(1 << 22)
#define GT_GEN6_BSD_CS_ERROR_INTERRUPT		(1 << 15)
#define GT_GEN6_BSD_USER_INTERRUPT		(1 << 12)
#define GT_BSD_USER_INTERRUPT			(1 << 5) /* ilk only */
#define GT_GEN7_L3_PARITY_ERROR_INTERRUPT	(1 << 5)
#define GT_PIPE_NOTIFY				(1 << 4)
#define GT_RENDER_CS_ERROR_INTERRUPT		(1 << 3)
#define GT_SYNC_STATUS				(1 << 2)
#define GT_USER_INTERRUPT			(1 << 0)

#define GTISR   0x44010
#define GTIMR   0x44014
#define GTIIR   0x44018
#define GTIER   0x4401c

#define ILK_DISPLAY_CHICKEN2	0x42004
/* Required on all Ironlake and Sandybridge according to the B-Spec. */
#define  ILK_ELPIN_409_SELECT	(1 << 25)
#define  ILK_DPARB_GATE	(1<<22)
#define  ILK_VSDPFD_FULL	(1<<21)
#define ILK_DISPLAY_CHICKEN_FUSES	0x42014
#define  ILK_INTERNAL_GRAPHICS_DISABLE	(1<<31)
#define  ILK_INTERNAL_DISPLAY_DISABLE	(1<<30)
#define  ILK_DISPLAY_DEBUG_DISABLE	(1<<29)
#define  ILK_HDCP_DISABLE		(1<<25)
#define  ILK_eDP_A_DISABLE		(1<<24)
#define  ILK_DESKTOP			(1<<23)

#define ILK_DSPCLK_GATE_D			0x42020
#define   ILK_VRHUNIT_CLOCK_GATE_DISABLE	(1 << 28)
#define   ILK_DPFCUNIT_CLOCK_GATE_DISABLE	(1 << 9)
#define   ILK_DPFCRUNIT_CLOCK_GATE_DISABLE	(1 << 8)
#define   ILK_DPFDUNIT_CLOCK_GATE_ENABLE	(1 << 7)
#define   ILK_DPARBUNIT_CLOCK_GATE_ENABLE	(1 << 5)

#define IVB_CHICKEN3	0x4200c
# define CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE	(1 << 5)
# define CHICKEN3_DGMG_DONE_FIX_DISABLE		(1 << 2)

#define DISP_ARB_CTL	0x45000
#define  DISP_TILE_SURFACE_SWIZZLING	(1<<13)
#define  DISP_FBC_WM_DIS		(1<<15)

/* GEN7 chicken */
#define GEN7_COMMON_SLICE_CHICKEN1		0x7010
# define GEN7_CSC1_RHWO_OPT_DISABLE_IN_RCC	((1<<10) | (1<<26))

#define GEN7_L3CNTLREG1				0xB01C
#define  GEN7_WA_FOR_GEN7_L3_CONTROL			0x3C4FFF8C
#define  GEN7_L3AGDIS				(1<<19)

#define GEN7_L3_CHICKEN_MODE_REGISTER		0xB030
#define  GEN7_WA_L3_CHICKEN_MODE				0x20000000

#define GEN7_L3SQCREG4				0xb034
#define  L3SQ_URB_READ_CAM_MATCH_DISABLE	(1<<27)

/* WaCatErrorRejectionIssue */
#define GEN7_SQ_CHICKEN_MBCUNIT_CONFIG		0x9030
#define  GEN7_SQ_CHICKEN_MBCUNIT_SQINTMOB	(1<<11)

#define HSW_FUSE_STRAP		0x42014
#define  HSW_CDCLK_LIMIT	(1 << 24)

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
#define SDE_HOTPLUG_MASK	(0xf << 8)
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
#define SDE_HOTPLUG_MASK_CPT	(SDE_CRT_HOTPLUG_CPT |		\
				 SDE_PORTD_HOTPLUG_CPT |	\
				 SDE_PORTC_HOTPLUG_CPT |	\
				 SDE_PORTB_HOTPLUG_CPT)
#define SDE_GMBUS_CPT		(1 << 17)
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

/* digital port hotplug */
#define PCH_PORT_HOTPLUG        0xc4030		/* SHOTPLUG_CTL */
#define PORTD_HOTPLUG_ENABLE            (1 << 20)
#define PORTD_PULSE_DURATION_2ms        (0)
#define PORTD_PULSE_DURATION_4_5ms      (1 << 18)
#define PORTD_PULSE_DURATION_6ms        (2 << 18)
#define PORTD_PULSE_DURATION_100ms      (3 << 18)
#define PORTD_PULSE_DURATION_MASK	(3 << 18)
#define PORTD_HOTPLUG_NO_DETECT         (0)
#define PORTD_HOTPLUG_SHORT_DETECT      (1 << 16)
#define PORTD_HOTPLUG_LONG_DETECT       (1 << 17)
#define PORTC_HOTPLUG_ENABLE            (1 << 12)
#define PORTC_PULSE_DURATION_2ms        (0)
#define PORTC_PULSE_DURATION_4_5ms      (1 << 10)
#define PORTC_PULSE_DURATION_6ms        (2 << 10)
#define PORTC_PULSE_DURATION_100ms      (3 << 10)
#define PORTC_PULSE_DURATION_MASK	(3 << 10)
#define PORTC_HOTPLUG_NO_DETECT         (0)
#define PORTC_HOTPLUG_SHORT_DETECT      (1 << 8)
#define PORTC_HOTPLUG_LONG_DETECT       (1 << 9)
#define PORTB_HOTPLUG_ENABLE            (1 << 4)
#define PORTB_PULSE_DURATION_2ms        (0)
#define PORTB_PULSE_DURATION_4_5ms      (1 << 2)
#define PORTB_PULSE_DURATION_6ms        (2 << 2)
#define PORTB_PULSE_DURATION_100ms      (3 << 2)
#define PORTB_PULSE_DURATION_MASK	(3 << 2)
#define PORTB_HOTPLUG_NO_DETECT         (0)
#define PORTB_HOTPLUG_SHORT_DETECT      (1 << 0)
#define PORTB_HOTPLUG_LONG_DETECT       (1 << 1)

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
#define _PCH_DPLL(pll) (pll == 0 ? _PCH_DPLL_A : _PCH_DPLL_B)

#define _PCH_FPA0                0xc6040
#define  FP_CB_TUNE		(0x3<<22)
#define _PCH_FPA1                0xc6044
#define _PCH_FPB0                0xc6048
#define _PCH_FPB1                0xc604c
#define _PCH_FP0(pll) (pll == 0 ? _PCH_FPA0 : _PCH_FPB0)
#define _PCH_FP1(pll) (pll == 0 ? _PCH_FPA1 : _PCH_FPB1)

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
#define  TRANSA_DPLL_ENABLE	(1<<3)
#define	 TRANSA_DPLLB_SEL	(1<<0)
#define	 TRANSA_DPLLA_SEL	0
#define  TRANSB_DPLL_ENABLE	(1<<7)
#define	 TRANSB_DPLLB_SEL	(1<<4)
#define	 TRANSB_DPLLA_SEL	(0)
#define  TRANSC_DPLL_ENABLE	(1<<11)
#define	 TRANSC_DPLLB_SEL	(1<<8)
#define	 TRANSC_DPLLA_SEL	(0)

/* transcoder */

#define _TRANS_HTOTAL_A          0xe0000
#define  TRANS_HTOTAL_SHIFT     16
#define  TRANS_HACTIVE_SHIFT    0
#define _TRANS_HBLANK_A          0xe0004
#define  TRANS_HBLANK_END_SHIFT 16
#define  TRANS_HBLANK_START_SHIFT 0
#define _TRANS_HSYNC_A           0xe0008
#define  TRANS_HSYNC_END_SHIFT  16
#define  TRANS_HSYNC_START_SHIFT 0
#define _TRANS_VTOTAL_A          0xe000c
#define  TRANS_VTOTAL_SHIFT     16
#define  TRANS_VACTIVE_SHIFT    0
#define _TRANS_VBLANK_A          0xe0010
#define  TRANS_VBLANK_END_SHIFT 16
#define  TRANS_VBLANK_START_SHIFT 0
#define _TRANS_VSYNC_A           0xe0014
#define  TRANS_VSYNC_END_SHIFT  16
#define  TRANS_VSYNC_START_SHIFT 0
#define _TRANS_VSYNCSHIFT_A	0xe0028

#define _TRANSA_DATA_M1          0xe0030
#define _TRANSA_DATA_N1          0xe0034
#define _TRANSA_DATA_M2          0xe0038
#define _TRANSA_DATA_N2          0xe003c
#define _TRANSA_DP_LINK_M1       0xe0040
#define _TRANSA_DP_LINK_N1       0xe0044
#define _TRANSA_DP_LINK_M2       0xe0048
#define _TRANSA_DP_LINK_N2       0xe004c

/* Per-transcoder DIP controls */

#define _VIDEO_DIP_CTL_A         0xe0200
#define _VIDEO_DIP_DATA_A        0xe0208
#define _VIDEO_DIP_GCP_A         0xe0210

#define _VIDEO_DIP_CTL_B         0xe1200
#define _VIDEO_DIP_DATA_B        0xe1208
#define _VIDEO_DIP_GCP_B         0xe1210

#define TVIDEO_DIP_CTL(pipe) _PIPE(pipe, _VIDEO_DIP_CTL_A, _VIDEO_DIP_CTL_B)
#define TVIDEO_DIP_DATA(pipe) _PIPE(pipe, _VIDEO_DIP_DATA_A, _VIDEO_DIP_DATA_B)
#define TVIDEO_DIP_GCP(pipe) _PIPE(pipe, _VIDEO_DIP_GCP_A, _VIDEO_DIP_GCP_B)

#define VLV_VIDEO_DIP_CTL_A		0x60200
#define VLV_VIDEO_DIP_DATA_A		0x60208
#define VLV_VIDEO_DIP_GDCP_PAYLOAD_A	0x60210

#define VLV_VIDEO_DIP_CTL_B		0x61170
#define VLV_VIDEO_DIP_DATA_B		0x61174
#define VLV_VIDEO_DIP_GDCP_PAYLOAD_B	0x61178

#define VLV_TVIDEO_DIP_CTL(pipe) \
	 _PIPE(pipe, VLV_VIDEO_DIP_CTL_A, VLV_VIDEO_DIP_CTL_B)
#define VLV_TVIDEO_DIP_DATA(pipe) \
	 _PIPE(pipe, VLV_VIDEO_DIP_DATA_A, VLV_VIDEO_DIP_DATA_B)
#define VLV_TVIDEO_DIP_GCP(pipe) \
	_PIPE(pipe, VLV_VIDEO_DIP_GDCP_PAYLOAD_A, VLV_VIDEO_DIP_GDCP_PAYLOAD_B)

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

#define HSW_TVIDEO_DIP_CTL(pipe) \
	 _PIPE(pipe, HSW_VIDEO_DIP_CTL_A, HSW_VIDEO_DIP_CTL_B)
#define HSW_TVIDEO_DIP_AVI_DATA(pipe) \
	 _PIPE(pipe, HSW_VIDEO_DIP_AVI_DATA_A, HSW_VIDEO_DIP_AVI_DATA_B)
#define HSW_TVIDEO_DIP_SPD_DATA(pipe) \
	 _PIPE(pipe, HSW_VIDEO_DIP_SPD_DATA_A, HSW_VIDEO_DIP_SPD_DATA_B)
#define HSW_TVIDEO_DIP_GCP(pipe) \
	_PIPE(pipe, HSW_VIDEO_DIP_GCP_A, HSW_VIDEO_DIP_GCP_B)

#define _TRANS_HTOTAL_B          0xe1000
#define _TRANS_HBLANK_B          0xe1004
#define _TRANS_HSYNC_B           0xe1008
#define _TRANS_VTOTAL_B          0xe100c
#define _TRANS_VBLANK_B          0xe1010
#define _TRANS_VSYNC_B           0xe1014
#define _TRANS_VSYNCSHIFT_B	 0xe1028

#define TRANS_HTOTAL(pipe) _PIPE(pipe, _TRANS_HTOTAL_A, _TRANS_HTOTAL_B)
#define TRANS_HBLANK(pipe) _PIPE(pipe, _TRANS_HBLANK_A, _TRANS_HBLANK_B)
#define TRANS_HSYNC(pipe) _PIPE(pipe, _TRANS_HSYNC_A, _TRANS_HSYNC_B)
#define TRANS_VTOTAL(pipe) _PIPE(pipe, _TRANS_VTOTAL_A, _TRANS_VTOTAL_B)
#define TRANS_VBLANK(pipe) _PIPE(pipe, _TRANS_VBLANK_A, _TRANS_VBLANK_B)
#define TRANS_VSYNC(pipe) _PIPE(pipe, _TRANS_VSYNC_A, _TRANS_VSYNC_B)
#define TRANS_VSYNCSHIFT(pipe) _PIPE(pipe, _TRANS_VSYNCSHIFT_A, \
				     _TRANS_VSYNCSHIFT_B)

#define _TRANSB_DATA_M1          0xe1030
#define _TRANSB_DATA_N1          0xe1034
#define _TRANSB_DATA_M2          0xe1038
#define _TRANSB_DATA_N2          0xe103c
#define _TRANSB_DP_LINK_M1       0xe1040
#define _TRANSB_DP_LINK_N1       0xe1044
#define _TRANSB_DP_LINK_M2       0xe1048
#define _TRANSB_DP_LINK_N2       0xe104c

#define TRANSDATA_M1(pipe) _PIPE(pipe, _TRANSA_DATA_M1, _TRANSB_DATA_M1)
#define TRANSDATA_N1(pipe) _PIPE(pipe, _TRANSA_DATA_N1, _TRANSB_DATA_N1)
#define TRANSDATA_M2(pipe) _PIPE(pipe, _TRANSA_DATA_M2, _TRANSB_DATA_M2)
#define TRANSDATA_N2(pipe) _PIPE(pipe, _TRANSA_DATA_N2, _TRANSB_DATA_N2)
#define TRANSDPLINK_M1(pipe) _PIPE(pipe, _TRANSA_DP_LINK_M1, _TRANSB_DP_LINK_M1)
#define TRANSDPLINK_N1(pipe) _PIPE(pipe, _TRANSA_DP_LINK_N1, _TRANSB_DP_LINK_N1)
#define TRANSDPLINK_M2(pipe) _PIPE(pipe, _TRANSA_DP_LINK_M2, _TRANSB_DP_LINK_M2)
#define TRANSDPLINK_N2(pipe) _PIPE(pipe, _TRANSA_DP_LINK_N2, _TRANSB_DP_LINK_N2)

#define _TRANSACONF              0xf0008
#define _TRANSBCONF              0xf1008
#define TRANSCONF(plane) _PIPE(plane, _TRANSACONF, _TRANSBCONF)
#define  TRANS_DISABLE          (0<<31)
#define  TRANS_ENABLE           (1<<31)
#define  TRANS_STATE_MASK       (1<<30)
#define  TRANS_STATE_DISABLE    (0<<30)
#define  TRANS_STATE_ENABLE     (1<<30)
#define  TRANS_FSYNC_DELAY_HB1  (0<<27)
#define  TRANS_FSYNC_DELAY_HB2  (1<<27)
#define  TRANS_FSYNC_DELAY_HB3  (2<<27)
#define  TRANS_FSYNC_DELAY_HB4  (3<<27)
#define  TRANS_DP_AUDIO_ONLY    (1<<26)
#define  TRANS_DP_VIDEO_AUDIO   (0<<26)
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
#define  TRANS_CHICKEN2_TIMING_OVERRIDE		(1<<31)


#define SOUTH_CHICKEN1		0xc2000
#define  FDIA_PHASE_SYNC_SHIFT_OVR	19
#define  FDIA_PHASE_SYNC_SHIFT_EN	18
#define  FDI_PHASE_SYNC_OVR(pipe) (1<<(FDIA_PHASE_SYNC_SHIFT_OVR - ((pipe) * 2)))
#define  FDI_PHASE_SYNC_EN(pipe) (1<<(FDIA_PHASE_SYNC_SHIFT_EN - ((pipe) * 2)))
#define  FDI_BC_BIFURCATION_SELECT	(1 << 12)
#define SOUTH_CHICKEN2		0xc2004
#define  DPLS_EDP_PPS_FIX_DIS	(1<<0)

#define _FDI_RXA_CHICKEN         0xc200c
#define _FDI_RXB_CHICKEN         0xc2010
#define  FDI_RX_PHASE_SYNC_POINTER_OVR	(1<<1)
#define  FDI_RX_PHASE_SYNC_POINTER_EN	(1<<0)
#define FDI_RX_CHICKEN(pipe) _PIPE(pipe, _FDI_RXA_CHICKEN, _FDI_RXB_CHICKEN)

#define SOUTH_DSPCLK_GATE_D	0xc2020
#define  PCH_DPLSUNIT_CLOCK_GATE_DISABLE (1<<29)
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
#define  FDI_DP_PORT_WIDTH_X1           (0<<19)
#define  FDI_DP_PORT_WIDTH_X2           (1<<19)
#define  FDI_DP_PORT_WIDTH_X3           (2<<19)
#define  FDI_DP_PORT_WIDTH_X4           (3<<19)
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
#define  FDI_DP_PORT_WIDTH_X8           (7<<19)
#define  FDI_8BPC                       (0<<16)
#define  FDI_10BPC                      (1<<16)
#define  FDI_6BPC                       (2<<16)
#define  FDI_12BPC                      (3<<16)
#define  FDI_LINK_REVERSE_OVERWRITE     (1<<15)
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
/* LPT */
#define  FDI_PORT_WIDTH_2X_LPT			(1<<19)
#define  FDI_PORT_WIDTH_1X_LPT			(0<<19)

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

/* or SDVOB */
#define HDMIB   0xe1140
#define  PORT_ENABLE    (1 << 31)
#define  TRANSCODER(pipe)       ((pipe) << 30)
#define  TRANSCODER_CPT(pipe)   ((pipe) << 29)
#define  TRANSCODER_MASK        (1 << 30)
#define  TRANSCODER_MASK_CPT    (3 << 29)
#define  COLOR_FORMAT_8bpc      (0)
#define  COLOR_FORMAT_12bpc     (3 << 26)
#define  SDVOB_HOTPLUG_ENABLE   (1 << 23)
#define  SDVO_ENCODING          (0)
#define  TMDS_ENCODING          (2 << 10)
#define  NULL_PACKET_VSYNC_ENABLE       (1 << 9)
/* CPT */
#define  HDMI_MODE_SELECT	(1 << 9)
#define  DVI_MODE_SELECT	(0)
#define  SDVOB_BORDER_ENABLE    (1 << 7)
#define  AUDIO_ENABLE           (1 << 6)
#define  VSYNC_ACTIVE_HIGH      (1 << 4)
#define  HSYNC_ACTIVE_HIGH      (1 << 3)
#define  PORT_DETECTED          (1 << 2)

/* PCH SDVOB multiplex with HDMIB */
#define PCH_SDVOB	HDMIB

#define HDMIC   0xe1150
#define HDMID   0xe1160

#define PCH_LVDS	0xe1180
#define  LVDS_DETECTED	(1 << 1)

/* vlv has 2 sets of panel control regs. */
#define PIPEA_PP_STATUS         0x61200
#define PIPEA_PP_CONTROL        0x61204
#define PIPEA_PP_ON_DELAYS      0x61208
#define PIPEA_PP_OFF_DELAYS     0x6120c
#define PIPEA_PP_DIVISOR        0x61210

#define PIPEB_PP_STATUS         0x61300
#define PIPEB_PP_CONTROL        0x61304
#define PIPEB_PP_ON_DELAYS      0x61308
#define PIPEB_PP_OFF_DELAYS     0x6130c
#define PIPEB_PP_DIVISOR        0x61310

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
#define  EDP_PANEL		(1 << 30)
#define  PANEL_PORT_SELECT_DPC	(2 << 30)
#define  PANEL_PORT_SELECT_DPD	(3 << 30)
#define  PANEL_POWER_UP_DELAY_MASK	(0x1fff0000)
#define  PANEL_POWER_UP_DELAY_SHIFT	16
#define  PANEL_LIGHT_ON_DELAY_MASK	(0x1fff)
#define  PANEL_LIGHT_ON_DELAY_SHIFT	0

#define PCH_PP_OFF_DELAYS	0xc720c
#define  PANEL_POWER_PORT_SELECT_MASK	(0x3 << 30)
#define  PANEL_POWER_PORT_LVDS		(0 << 30)
#define  PANEL_POWER_PORT_DP_A		(1 << 30)
#define  PANEL_POWER_PORT_DP_C		(2 << 30)
#define  PANEL_POWER_PORT_DP_D		(3 << 30)
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
#define EDP_LINK_TRAIN_800MV_3_5DB_IVB		(0x33 <<22)

/* legacy values */
#define EDP_LINK_TRAIN_500MV_0DB_IVB		(0x00 <<22)
#define EDP_LINK_TRAIN_1000MV_0DB_IVB		(0x20 <<22)
#define EDP_LINK_TRAIN_500MV_3_5DB_IVB		(0x02 <<22)
#define EDP_LINK_TRAIN_1000MV_3_5DB_IVB		(0x22 <<22)
#define EDP_LINK_TRAIN_1000MV_6DB_IVB		(0x23 <<22)

#define  EDP_LINK_TRAIN_VOL_EMP_MASK_IVB	(0x3f<<22)

#define  FORCEWAKE				0xA18C
#define  FORCEWAKE_VLV				0x1300b0
#define  FORCEWAKE_ACK_VLV			0x1300b4
#define  FORCEWAKE_ACK_HSW			0x130044
#define  FORCEWAKE_ACK				0x130090
#define  FORCEWAKE_MT				0xa188 /* multi-threaded */
#define   FORCEWAKE_KERNEL			0x1
#define   FORCEWAKE_USER			0x2
#define  FORCEWAKE_MT_ACK			0x130040
#define  ECOBUS					0xa180
#define    FORCEWAKE_MT_ENABLE			(1<<5)

#define  GTFIFODBG				0x120000
#define    GT_FIFO_CPU_ERROR_MASK		7
#define    GT_FIFO_OVFERR			(1<<2)
#define    GT_FIFO_IAWRERR			(1<<1)
#define    GT_FIFO_IARDERR			(1<<0)

#define  GT_FIFO_FREE_ENTRIES			0x120008
#define    GT_FIFO_NUM_RESERVED_ENTRIES		20

#define GEN6_UCGCTL1				0x9400
# define GEN6_BLBUNIT_CLOCK_GATE_DISABLE		(1 << 5)
# define GEN6_CSUNIT_CLOCK_GATE_DISABLE			(1 << 7)

#define GEN6_UCGCTL2				0x9404
# define GEN7_VDSUNIT_CLOCK_GATE_DISABLE		(1 << 30)
# define GEN7_TDLUNIT_CLOCK_GATE_DISABLE		(1 << 22)
# define GEN6_RCZUNIT_CLOCK_GATE_DISABLE		(1 << 13)
# define GEN6_RCPBUNIT_CLOCK_GATE_DISABLE		(1 << 12)
# define GEN6_RCCUNIT_CLOCK_GATE_DISABLE		(1 << 11)

#define GEN7_UCGCTL4				0x940c
#define  GEN7_L3BANK2X_CLOCK_GATE_DISABLE	(1<<25)

#define GEN6_RPNSWREQ				0xA008
#define   GEN6_TURBO_DISABLE			(1<<31)
#define   GEN6_FREQUENCY(x)			((x)<<25)
#define   GEN6_OFFSET(x)			((x)<<19)
#define   GEN6_AGGRESSIVE_TURBO			(0<<15)
#define GEN6_RC_VIDEO_FREQ			0xA00C
#define GEN6_RC_CONTROL				0xA090
#define   GEN6_RC_CTL_RC6pp_ENABLE		(1<<16)
#define   GEN6_RC_CTL_RC6p_ENABLE		(1<<17)
#define   GEN6_RC_CTL_RC6_ENABLE		(1<<18)
#define   GEN6_RC_CTL_RC1e_ENABLE		(1<<20)
#define   GEN6_RC_CTL_RC7_ENABLE		(1<<22)
#define   GEN6_RC_CTL_EI_MODE(x)		((x)<<27)
#define   GEN6_RC_CTL_HW_ENABLE			(1<<31)
#define GEN6_RP_DOWN_TIMEOUT			0xA010
#define GEN6_RP_INTERRUPT_LIMITS		0xA014
#define GEN6_RPSTAT1				0xA01C
#define   GEN6_CAGF_SHIFT			8
#define   GEN6_CAGF_MASK			(0x7f << GEN6_CAGF_SHIFT)
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
#define   GEN7_RP_DOWN_IDLE_AVG			(0x2<<0)
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
#define GEN6_RC_STATE				0xA094
#define GEN6_RC1_WAKE_RATE_LIMIT		0xA098
#define GEN6_RC6_WAKE_RATE_LIMIT		0xA09C
#define GEN6_RC6pp_WAKE_RATE_LIMIT		0xA0A0
#define GEN6_RC_EVALUATION_INTERVAL		0xA0A8
#define GEN6_RC_IDLE_HYSTERSIS			0xA0AC
#define GEN6_RC_SLEEP				0xA0B0
#define GEN6_RC1e_THRESHOLD			0xA0B4
#define GEN6_RC6_THRESHOLD			0xA0B8
#define GEN6_RC6p_THRESHOLD			0xA0BC
#define GEN6_RC6pp_THRESHOLD			0xA0C0
#define GEN6_PMINTRMSK				0xA168

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
#define  GEN6_PM_DEFERRED_EVENTS		(GEN6_PM_RP_UP_THRESHOLD | \
						 GEN6_PM_RP_DOWN_THRESHOLD | \
						 GEN6_PM_RP_DOWN_TIMEOUT)

#define GEN6_GT_GFX_RC6_LOCKED			0x138104
#define GEN6_GT_GFX_RC6				0x138108
#define GEN6_GT_GFX_RC6p			0x13810C
#define GEN6_GT_GFX_RC6pp			0x138110

#define GEN6_PCODE_MAILBOX			0x138124
#define   GEN6_PCODE_READY			(1<<31)
#define   GEN6_READ_OC_PARAMS			0xc
#define   GEN6_PCODE_WRITE_MIN_FREQ_TABLE	0x8
#define   GEN6_PCODE_READ_MIN_FREQ_TABLE	0x9
#define	  GEN6_PCODE_WRITE_RC6VIDS		0x4
#define	  GEN6_PCODE_READ_RC6VIDS		0x5
#define   GEN6_ENCODE_RC6_VID(mv)		(((mv) / 5) - 245) < 0 ?: 0
#define   GEN6_DECODE_RC6_VID(vids)		(((vids) * 5) > 0 ? ((vids) * 5) + 245 : 0)
#define GEN6_PCODE_DATA				0x138128
#define   GEN6_PCODE_FREQ_IA_RATIO_SHIFT	8

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
#define GEN7_L3LOG_SIZE			0x80

#define GEN7_HALF_SLICE_CHICKEN1	0xe100 /* IVB GT1 + VLV */
#define GEN7_HALF_SLICE_CHICKEN1_GT2	0xf100
#define   GEN7_MAX_PS_THREAD_DEP		(8<<12)
#define   GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE	(1<<3)

#define GEN7_ROW_CHICKEN2		0xe4f4
#define GEN7_ROW_CHICKEN2_GT2		0xf4f4
#define   DOP_CLOCK_GATING_DISABLE	(1<<0)

#define G4X_AUD_VID_DID			0x62020
#define INTEL_AUDIO_DEVCL		0x808629FB
#define INTEL_AUDIO_DEVBLC		0x80862801
#define INTEL_AUDIO_DEVCTG		0x80862802

#define G4X_AUD_CNTL_ST			0x620B4
#define G4X_ELDV_DEVCL_DEVBLC		(1 << 13)
#define G4X_ELDV_DEVCTG			(1 << 14)
#define G4X_ELD_ADDR			(0xf << 5)
#define G4X_ELD_ACK			(1 << 4)
#define G4X_HDMIW_HDMIEDID		0x6210C

#define IBX_HDMIW_HDMIEDID_A		0xE2050
#define IBX_HDMIW_HDMIEDID_B		0xE2150
#define IBX_HDMIW_HDMIEDID(pipe) _PIPE(pipe, \
					IBX_HDMIW_HDMIEDID_A, \
					IBX_HDMIW_HDMIEDID_B)
#define IBX_AUD_CNTL_ST_A		0xE20B4
#define IBX_AUD_CNTL_ST_B		0xE21B4
#define IBX_AUD_CNTL_ST(pipe) _PIPE(pipe, \
					IBX_AUD_CNTL_ST_A, \
					IBX_AUD_CNTL_ST_B)
#define IBX_ELD_BUFFER_SIZE		(0x1f << 10)
#define IBX_ELD_ADDRESS			(0x1f << 5)
#define IBX_ELD_ACK			(1 << 4)
#define IBX_AUD_CNTL_ST2		0xE20C0
#define IBX_ELD_VALIDB			(1 << 0)
#define IBX_CP_READYB			(1 << 1)

#define CPT_HDMIW_HDMIEDID_A		0xE5050
#define CPT_HDMIW_HDMIEDID_B		0xE5150
#define CPT_HDMIW_HDMIEDID(pipe) _PIPE(pipe, \
					CPT_HDMIW_HDMIEDID_A, \
					CPT_HDMIW_HDMIEDID_B)
#define CPT_AUD_CNTL_ST_A		0xE50B4
#define CPT_AUD_CNTL_ST_B		0xE51B4
#define CPT_AUD_CNTL_ST(pipe) _PIPE(pipe, \
					CPT_AUD_CNTL_ST_A, \
					CPT_AUD_CNTL_ST_B)
#define CPT_AUD_CNTRL_ST2		0xE50C0

/* These are the 4 32-bit write offset registers for each stream
 * output buffer.  It determines the offset from the
 * 3DSTATE_SO_BUFFERs that the next streamed vertex output goes to.
 */
#define GEN7_SO_WRITE_OFFSET(n)		(0x5280 + (n) * 4)

#define IBX_AUD_CONFIG_A			0xe2000
#define IBX_AUD_CONFIG_B			0xe2100
#define IBX_AUD_CFG(pipe) _PIPE(pipe, \
					IBX_AUD_CONFIG_A, \
					IBX_AUD_CONFIG_B)
#define CPT_AUD_CONFIG_A			0xe5000
#define CPT_AUD_CONFIG_B			0xe5100
#define CPT_AUD_CFG(pipe) _PIPE(pipe, \
					CPT_AUD_CONFIG_A, \
					CPT_AUD_CONFIG_B)
#define   AUD_CONFIG_N_VALUE_INDEX		(1 << 29)
#define   AUD_CONFIG_N_PROG_ENABLE		(1 << 28)
#define   AUD_CONFIG_UPPER_N_SHIFT		20
#define   AUD_CONFIG_UPPER_N_VALUE		(0xff << 20)
#define   AUD_CONFIG_LOWER_N_SHIFT		4
#define   AUD_CONFIG_LOWER_N_VALUE		(0xfff << 4)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_SHIFT	16
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI		(0xf << 16)
#define   AUD_CONFIG_DISABLE_NCTS		(1 << 3)

/* HSW Audio */
#define   HSW_AUD_CONFIG_A		0x65000 /* Audio Configuration Transcoder A */
#define   HSW_AUD_CONFIG_B		0x65100 /* Audio Configuration Transcoder B */
#define   HSW_AUD_CFG(pipe) _PIPE(pipe, \
					HSW_AUD_CONFIG_A, \
					HSW_AUD_CONFIG_B)

#define   HSW_AUD_MISC_CTRL_A		0x65010 /* Audio Misc Control Convert 1 */
#define   HSW_AUD_MISC_CTRL_B		0x65110 /* Audio Misc Control Convert 2 */
#define   HSW_AUD_MISC_CTRL(pipe) _PIPE(pipe, \
					HSW_AUD_MISC_CTRL_A, \
					HSW_AUD_MISC_CTRL_B)

#define   HSW_AUD_DIP_ELD_CTRL_ST_A	0x650b4 /* Audio DIP and ELD Control State Transcoder A */
#define   HSW_AUD_DIP_ELD_CTRL_ST_B	0x651b4 /* Audio DIP and ELD Control State Transcoder B */
#define   HSW_AUD_DIP_ELD_CTRL(pipe) _PIPE(pipe, \
					HSW_AUD_DIP_ELD_CTRL_ST_A, \
					HSW_AUD_DIP_ELD_CTRL_ST_B)

/* Audio Digital Converter */
#define   HSW_AUD_DIG_CNVT_1		0x65080 /* Audio Converter 1 */
#define   HSW_AUD_DIG_CNVT_2		0x65180 /* Audio Converter 1 */
#define   AUD_DIG_CNVT(pipe) _PIPE(pipe, \
					HSW_AUD_DIG_CNVT_1, \
					HSW_AUD_DIG_CNVT_2)
#define   DIP_PORT_SEL_MASK		0x3

#define   HSW_AUD_EDID_DATA_A		0x65050
#define   HSW_AUD_EDID_DATA_B		0x65150
#define   HSW_AUD_EDID_DATA(pipe) _PIPE(pipe, \
					HSW_AUD_EDID_DATA_A, \
					HSW_AUD_EDID_DATA_B)

#define   HSW_AUD_PIPE_CONV_CFG		0x6507c /* Audio pipe and converter configs */
#define   HSW_AUD_PIN_ELD_CP_VLD	0x650c0 /* Audio ELD and CP Ready Status */
#define   AUDIO_INACTIVE_C		(1<<11)
#define   AUDIO_INACTIVE_B		(1<<7)
#define   AUDIO_INACTIVE_A		(1<<3)
#define   AUDIO_OUTPUT_ENABLE_A		(1<<2)
#define   AUDIO_OUTPUT_ENABLE_B		(1<<6)
#define   AUDIO_OUTPUT_ENABLE_C		(1<<10)
#define   AUDIO_ELD_VALID_A		(1<<0)
#define   AUDIO_ELD_VALID_B		(1<<4)
#define   AUDIO_ELD_VALID_C		(1<<8)
#define   AUDIO_CP_READY_A		(1<<1)
#define   AUDIO_CP_READY_B		(1<<5)
#define   AUDIO_CP_READY_C		(1<<9)

/* HSW Power Wells */
#define HSW_PWR_WELL_CTL1			0x45400 /* BIOS */
#define HSW_PWR_WELL_CTL2			0x45404 /* Driver */
#define HSW_PWR_WELL_CTL3			0x45408 /* KVMR */
#define HSW_PWR_WELL_CTL4			0x4540C /* Debug */
#define   HSW_PWR_WELL_ENABLE			(1<<31)
#define   HSW_PWR_WELL_STATE			(1<<30)
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
#define TRANS_DDI_FUNC_CTL(tran) _TRANSCODER(tran, TRANS_DDI_FUNC_CTL_A, \
						   TRANS_DDI_FUNC_CTL_B)
#define  TRANS_DDI_FUNC_ENABLE		(1<<31)
/* Those bits are ignored by pipe EDP since it can only connect to DDI A */
#define  TRANS_DDI_PORT_MASK		(7<<28)
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
#define  TRANS_DDI_BFI_ENABLE		(1<<4)
#define  TRANS_DDI_PORT_WIDTH_X1	(0<<1)
#define  TRANS_DDI_PORT_WIDTH_X2	(1<<1)
#define  TRANS_DDI_PORT_WIDTH_X4	(3<<1)

/* DisplayPort Transport Control */
#define DP_TP_CTL_A			0x64040
#define DP_TP_CTL_B			0x64140
#define DP_TP_CTL(port) _PORT(port, DP_TP_CTL_A, DP_TP_CTL_B)
#define  DP_TP_CTL_ENABLE			(1<<31)
#define  DP_TP_CTL_MODE_SST			(0<<27)
#define  DP_TP_CTL_MODE_MST			(1<<27)
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
#define  DP_TP_STATUS_IDLE_DONE		(1<<25)
#define  DP_TP_STATUS_AUTOTRAIN_DONE	(1<<12)

/* DDI Buffer Control */
#define DDI_BUF_CTL_A				0x64000
#define DDI_BUF_CTL_B				0x64100
#define DDI_BUF_CTL(port) _PORT(port, DDI_BUF_CTL_A, DDI_BUF_CTL_B)
#define  DDI_BUF_CTL_ENABLE			(1<<31)
#define  DDI_BUF_EMP_400MV_0DB_HSW		(0<<24)   /* Sel0 */
#define  DDI_BUF_EMP_400MV_3_5DB_HSW		(1<<24)   /* Sel1 */
#define  DDI_BUF_EMP_400MV_6DB_HSW		(2<<24)   /* Sel2 */
#define  DDI_BUF_EMP_400MV_9_5DB_HSW		(3<<24)   /* Sel3 */
#define  DDI_BUF_EMP_600MV_0DB_HSW		(4<<24)   /* Sel4 */
#define  DDI_BUF_EMP_600MV_3_5DB_HSW		(5<<24)   /* Sel5 */
#define  DDI_BUF_EMP_600MV_6DB_HSW		(6<<24)   /* Sel6 */
#define  DDI_BUF_EMP_800MV_0DB_HSW		(7<<24)   /* Sel7 */
#define  DDI_BUF_EMP_800MV_3_5DB_HSW		(8<<24)   /* Sel8 */
#define  DDI_BUF_EMP_MASK			(0xf<<24)
#define  DDI_BUF_IS_IDLE			(1<<7)
#define  DDI_A_4_LANES				(1<<4)
#define  DDI_PORT_WIDTH_X1			(0<<1)
#define  DDI_PORT_WIDTH_X2			(1<<1)
#define  DDI_PORT_WIDTH_X4			(3<<1)
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
#define   SBI_SSCCTL_DISABLE			(1<<0)
#define  SBI_SSCAUXDIV6				0x0610
#define   SBI_SSCAUXDIV_FINALDIV2SEL(x)		((x)<<4)
#define  SBI_DBUFF0				0x2a00

/* LPT PIXCLK_GATE */
#define PIXCLK_GATE			0xC6020
#define  PIXCLK_GATE_UNGATE		(1<<0)
#define  PIXCLK_GATE_GATE		(0<<0)

/* SPLL */
#define SPLL_CTL			0x46020
#define  SPLL_PLL_ENABLE		(1<<31)
#define  SPLL_PLL_SSC			(1<<28)
#define  SPLL_PLL_NON_SSC		(2<<28)
#define  SPLL_PLL_FREQ_810MHz		(0<<26)
#define  SPLL_PLL_FREQ_1350MHz		(1<<26)

/* WRPLL */
#define WRPLL_CTL1			0x46040
#define WRPLL_CTL2			0x46060
#define  WRPLL_PLL_ENABLE		(1<<31)
#define  WRPLL_PLL_SELECT_SSC		(0x01<<28)
#define  WRPLL_PLL_SELECT_NON_SSC	(0x02<<28)
#define  WRPLL_PLL_SELECT_LCPLL_2700	(0x03<<28)
/* WRPLL divider programming */
#define  WRPLL_DIVIDER_REFERENCE(x)	((x)<<0)
#define  WRPLL_DIVIDER_POST(x)		((x)<<8)
#define  WRPLL_DIVIDER_FEEDBACK(x)	((x)<<16)

/* Port clock selection */
#define PORT_CLK_SEL_A			0x46100
#define PORT_CLK_SEL_B			0x46104
#define PORT_CLK_SEL(port) _PORT(port, PORT_CLK_SEL_A, PORT_CLK_SEL_B)
#define  PORT_CLK_SEL_LCPLL_2700	(0<<29)
#define  PORT_CLK_SEL_LCPLL_1350	(1<<29)
#define  PORT_CLK_SEL_LCPLL_810		(2<<29)
#define  PORT_CLK_SEL_SPLL		(3<<29)
#define  PORT_CLK_SEL_WRPLL1		(4<<29)
#define  PORT_CLK_SEL_WRPLL2		(5<<29)
#define  PORT_CLK_SEL_NONE		(7<<29)

/* Transcoder clock selection */
#define TRANS_CLK_SEL_A			0x46140
#define TRANS_CLK_SEL_B			0x46144
#define TRANS_CLK_SEL(tran) _TRANSCODER(tran, TRANS_CLK_SEL_A, TRANS_CLK_SEL_B)
/* For each transcoder, we need to select the corresponding port clock */
#define  TRANS_CLK_SEL_DISABLED		(0x0<<29)
#define  TRANS_CLK_SEL_PORT(x)		((x+1)<<29)

#define _TRANSA_MSA_MISC		0x60410
#define _TRANSB_MSA_MISC		0x61410
#define TRANS_MSA_MISC(tran) _TRANSCODER(tran, _TRANSA_MSA_MISC, \
					       _TRANSB_MSA_MISC)
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
#define  LCPLL_CD_CLOCK_DISABLE		(1<<25)
#define  LCPLL_CD2X_CLOCK_DISABLE	(1<<23)
#define  LCPLL_CD_SOURCE_FCLK		(1<<21)

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
#define  SFUSE_STRAP_DDIB_DETECTED	(1<<2)
#define  SFUSE_STRAP_DDIC_DETECTED	(1<<1)
#define  SFUSE_STRAP_DDID_DETECTED	(1<<0)

#define WM_DBG				0x45280
#define  WM_DBG_DISALLOW_MULTIPLE_LP	(1<<0)
#define  WM_DBG_DISALLOW_MAXFIFO	(1<<1)
#define  WM_DBG_DISALLOW_SPRITE		(1<<2)

#endif /* _I915_REG_H_ */
