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

#include "i915_reg_defs.h"
#include "display/intel_display_reg_defs.h"

/**
 * DOC: The i915 register macro definition style guide
 *
 * Follow the style described here for new macros, and while changing existing
 * macros. Do **not** mass change existing definitions just to update the style.
 *
 * File Layout
 * ~~~~~~~~~~~
 *
 * Keep helper macros near the top. For example, _PIPE() and friends.
 *
 * Prefix macros that generally should not be used outside of this file with
 * underscore '_'. For example, _PIPE() and friends, single instances of
 * registers that are defined solely for the use by function-like macros.
 *
 * Avoid using the underscore prefixed macros outside of this file. There are
 * exceptions, but keep them to a minimum.
 *
 * There are two basic types of register definitions: Single registers and
 * register groups. Register groups are registers which have two or more
 * instances, for example one per pipe, port, transcoder, etc. Register groups
 * should be defined using function-like macros.
 *
 * For single registers, define the register offset first, followed by register
 * contents.
 *
 * For register groups, define the register instance offsets first, prefixed
 * with underscore, followed by a function-like macro choosing the right
 * instance based on the parameter, followed by register contents.
 *
 * Define the register contents (i.e. bit and bit field macros) from most
 * significant to least significant bit. Indent the register content macros
 * using two extra spaces between ``#define`` and the macro name.
 *
 * Define bit fields using ``REG_GENMASK(h, l)``. Define bit field contents
 * using ``REG_FIELD_PREP(mask, value)``. This will define the values already
 * shifted in place, so they can be directly OR'd together. For convenience,
 * function-like macros may be used to define bit fields, but do note that the
 * macros may be needed to read as well as write the register contents.
 *
 * Define bits using ``REG_BIT(N)``. Do **not** add ``_BIT`` suffix to the name.
 *
 * Group the register and its contents together without blank lines, separate
 * from other registers and their contents with one blank line.
 *
 * Indent macro values from macro names using TABs. Align values vertically. Use
 * braces in macro values as needed to avoid unintended precedence after macro
 * substitution. Use spaces in macro values according to kernel coding
 * style. Use lower case in hexadecimal values.
 *
 * Naming
 * ~~~~~~
 *
 * Try to name registers according to the specs. If the register name changes in
 * the specs from platform to another, stick to the original name.
 *
 * Try to reuse existing register macro definitions. Only add new macros for
 * new register offsets, or when the register contents have changed enough to
 * warrant a full redefinition.
 *
 * When a register macro changes for a new platform, prefix the new macro using
 * the platform acronym or generation. For example, ``SKL_`` or ``GEN8_``. The
 * prefix signifies the start platform/generation using the register.
 *
 * When a bit (field) macro changes or gets added for a new platform, while
 * retaining the existing register macro, add a platform acronym or generation
 * suffix to the name. For example, ``_SKL`` or ``_GEN8``.
 *
 * Examples
 * ~~~~~~~~
 *
 * (Note that the values in the example are indented using spaces instead of
 * TABs to avoid misalignment in generated documentation. Use TABs in the
 * definitions.)::
 *
 *  #define _FOO_A                      0xf000
 *  #define _FOO_B                      0xf001
 *  #define FOO(pipe)                   _MMIO_PIPE(pipe, _FOO_A, _FOO_B)
 *  #define   FOO_ENABLE                REG_BIT(31)
 *  #define   FOO_MODE_MASK             REG_GENMASK(19, 16)
 *  #define   FOO_MODE_BAR              REG_FIELD_PREP(FOO_MODE_MASK, 0)
 *  #define   FOO_MODE_BAZ              REG_FIELD_PREP(FOO_MODE_MASK, 1)
 *  #define   FOO_MODE_QUX_SNB          REG_FIELD_PREP(FOO_MODE_MASK, 2)
 *
 *  #define BAR                         _MMIO(0xb000)
 *  #define GEN8_BAR                    _MMIO(0xb888)
 */

#define GU_CNTL_PROTECTED		_MMIO(0x10100C)
#define   DEPRESENT			REG_BIT(9)

#define GU_CNTL				_MMIO(0x101010)
#define   LMEM_INIT			REG_BIT(7)
#define   DRIVERFLR			REG_BIT(31)
#define GU_DEBUG			_MMIO(0x101018)
#define   DRIVERFLR_STATUS		REG_BIT(31)

#define GEN6_STOLEN_RESERVED		_MMIO(0x1082C0)
#define GEN6_STOLEN_RESERVED_ADDR_MASK	(0xFFF << 20)
#define GEN7_STOLEN_RESERVED_ADDR_MASK	(0x3FFF << 18)
#define GEN6_STOLEN_RESERVED_SIZE_MASK	(3 << 4)
#define GEN6_STOLEN_RESERVED_1M		(0 << 4)
#define GEN6_STOLEN_RESERVED_512K	(1 << 4)
#define GEN6_STOLEN_RESERVED_256K	(2 << 4)
#define GEN6_STOLEN_RESERVED_128K	(3 << 4)
#define GEN7_STOLEN_RESERVED_SIZE_MASK	(1 << 5)
#define GEN7_STOLEN_RESERVED_1M		(0 << 5)
#define GEN7_STOLEN_RESERVED_256K	(1 << 5)
#define GEN8_STOLEN_RESERVED_SIZE_MASK	(3 << 7)
#define GEN8_STOLEN_RESERVED_1M		(0 << 7)
#define GEN8_STOLEN_RESERVED_2M		(1 << 7)
#define GEN8_STOLEN_RESERVED_4M		(2 << 7)
#define GEN8_STOLEN_RESERVED_8M		(3 << 7)
#define GEN6_STOLEN_RESERVED_ENABLE	(1 << 0)
#define GEN11_STOLEN_RESERVED_ADDR_MASK	(0xFFFFFFFFFFFULL << 20)

/*
 * Reset registers
 */
#define DEBUG_RESET_I830		_MMIO(0x6070)
#define  DEBUG_RESET_FULL		(1 << 7)
#define  DEBUG_RESET_RENDER		(1 << 8)
#define  DEBUG_RESET_DISPLAY		(1 << 9)

/*
 * IOSF sideband
 */
#define VLV_IOSF_DOORBELL_REQ			_MMIO(VLV_DISPLAY_BASE + 0x2100)
#define   IOSF_DEVFN_SHIFT			24
#define   IOSF_OPCODE_SHIFT			16
#define   IOSF_PORT_SHIFT			8
#define   IOSF_BYTE_ENABLES_SHIFT		4
#define   IOSF_BAR_SHIFT			1
#define   IOSF_SB_BUSY				(1 << 0)
#define   IOSF_PORT_BUNIT			0x03
#define   IOSF_PORT_PUNIT			0x04
#define   IOSF_PORT_NC				0x11
#define   IOSF_PORT_DPIO			0x12
#define   IOSF_PORT_GPIO_NC			0x13
#define   IOSF_PORT_CCK				0x14
#define   IOSF_PORT_DPIO_2			0x1a
#define   IOSF_PORT_FLISDSI			0x1b
#define   IOSF_PORT_GPIO_SC			0x48
#define   IOSF_PORT_GPIO_SUS			0xa8
#define   IOSF_PORT_CCU				0xa9
#define   CHV_IOSF_PORT_GPIO_N			0x13
#define   CHV_IOSF_PORT_GPIO_SE			0x48
#define   CHV_IOSF_PORT_GPIO_E			0xa8
#define   CHV_IOSF_PORT_GPIO_SW			0xb2
#define VLV_IOSF_DATA				_MMIO(VLV_DISPLAY_BASE + 0x2104)
#define VLV_IOSF_ADDR				_MMIO(VLV_DISPLAY_BASE + 0x2108)

/* DPIO registers */
#define DPIO_DEVFN			0

/*
 * Fence registers
 * [0-7]  @ 0x2000 gen2,gen3
 * [8-15] @ 0x3000 945,g33,pnv
 *
 * [0-15] @ 0x3000 gen4,gen5
 *
 * [0-15] @ 0x100000 gen6,vlv,chv
 * [0-31] @ 0x100000 gen7+
 */
#define FENCE_REG(i)			_MMIO(0x2000 + (((i) & 8) << 9) + ((i) & 7) * 4)
#define   I830_FENCE_START_MASK		0x07f80000
#define   I830_FENCE_TILING_Y_SHIFT	12
#define   I830_FENCE_SIZE_BITS(size)	((ffs((size) >> 19) - 1) << 8)
#define   I830_FENCE_PITCH_SHIFT	4
#define   I830_FENCE_REG_VALID		(1 << 0)
#define   I915_FENCE_MAX_PITCH_VAL	4
#define   I830_FENCE_MAX_PITCH_VAL	6
#define   I830_FENCE_MAX_SIZE_VAL	(1 << 8)

#define   I915_FENCE_START_MASK		0x0ff00000
#define   I915_FENCE_SIZE_BITS(size)	((ffs((size) >> 20) - 1) << 8)

#define FENCE_REG_965_LO(i)		_MMIO(0x03000 + (i) * 8)
#define FENCE_REG_965_HI(i)		_MMIO(0x03000 + (i) * 8 + 4)
#define   I965_FENCE_PITCH_SHIFT	2
#define   I965_FENCE_TILING_Y_SHIFT	1
#define   I965_FENCE_REG_VALID		(1 << 0)
#define   I965_FENCE_MAX_PITCH_VAL	0x0400

#define FENCE_REG_GEN6_LO(i)		_MMIO(0x100000 + (i) * 8)
#define FENCE_REG_GEN6_HI(i)		_MMIO(0x100000 + (i) * 8 + 4)
#define   GEN6_FENCE_PITCH_SHIFT	32
#define   GEN7_FENCE_MAX_PITCH_VAL	0x0800


/* control register for cpu gtt access */
#define TILECTL				_MMIO(0x101000)
#define   TILECTL_SWZCTL			(1 << 0)
#define   TILECTL_TLBPF			(1 << 1)
#define   TILECTL_TLB_PREFETCH_DIS	(1 << 2)
#define   TILECTL_BACKSNOOP_DIS		(1 << 3)

/*
 * Instruction and interrupt control regs
 */
#define PGTBL_CTL	_MMIO(0x02020)
#define   PGTBL_ADDRESS_LO_MASK	0xfffff000 /* bits [31:12] */
#define   PGTBL_ADDRESS_HI_MASK	0x000000f0 /* bits [35:32] (gen4) */
#define PGTBL_ER	_MMIO(0x02024)
#define PRB0_BASE	(0x2030 - 0x30)
#define PRB1_BASE	(0x2040 - 0x30) /* 830,gen3 */
#define PRB2_BASE	(0x2050 - 0x30) /* gen3 */
#define SRB0_BASE	(0x2100 - 0x30) /* gen2 */
#define SRB1_BASE	(0x2110 - 0x30) /* gen2 */
#define SRB2_BASE	(0x2120 - 0x30) /* 830 */
#define SRB3_BASE	(0x2130 - 0x30) /* 830 */
#define RENDER_RING_BASE	0x02000
#define BSD_RING_BASE		0x04000
#define GEN6_BSD_RING_BASE	0x12000
#define GEN8_BSD2_RING_BASE	0x1c000
#define GEN11_BSD_RING_BASE	0x1c0000
#define GEN11_BSD2_RING_BASE	0x1c4000
#define GEN11_BSD3_RING_BASE	0x1d0000
#define GEN11_BSD4_RING_BASE	0x1d4000
#define XEHP_BSD5_RING_BASE	0x1e0000
#define XEHP_BSD6_RING_BASE	0x1e4000
#define XEHP_BSD7_RING_BASE	0x1f0000
#define XEHP_BSD8_RING_BASE	0x1f4000
#define VEBOX_RING_BASE		0x1a000
#define GEN11_VEBOX_RING_BASE		0x1c8000
#define GEN11_VEBOX2_RING_BASE		0x1d8000
#define XEHP_VEBOX3_RING_BASE		0x1e8000
#define XEHP_VEBOX4_RING_BASE		0x1f8000
#define MTL_GSC_RING_BASE		0x11a000
#define GEN12_COMPUTE0_RING_BASE	0x1a000
#define GEN12_COMPUTE1_RING_BASE	0x1c000
#define GEN12_COMPUTE2_RING_BASE	0x1e000
#define GEN12_COMPUTE3_RING_BASE	0x26000
#define BLT_RING_BASE		0x22000
#define XEHPC_BCS1_RING_BASE	0x3e0000
#define XEHPC_BCS2_RING_BASE	0x3e2000
#define XEHPC_BCS3_RING_BASE	0x3e4000
#define XEHPC_BCS4_RING_BASE	0x3e6000
#define XEHPC_BCS5_RING_BASE	0x3e8000
#define XEHPC_BCS6_RING_BASE	0x3ea000
#define XEHPC_BCS7_RING_BASE	0x3ec000
#define XEHPC_BCS8_RING_BASE	0x3ee000
#define DG1_GSC_HECI1_BASE	0x00258000
#define DG1_GSC_HECI2_BASE	0x00259000
#define DG2_GSC_HECI1_BASE	0x00373000
#define DG2_GSC_HECI2_BASE	0x00374000
#define MTL_GSC_HECI1_BASE	0x00116000
#define MTL_GSC_HECI2_BASE	0x00117000

#define HECI_H_CSR(base)	_MMIO((base) + 0x4)
#define   HECI_H_CSR_IE		REG_BIT(0)
#define   HECI_H_CSR_IS		REG_BIT(1)
#define   HECI_H_CSR_IG		REG_BIT(2)
#define   HECI_H_CSR_RDY	REG_BIT(3)
#define   HECI_H_CSR_RST	REG_BIT(4)

#define HECI_H_GS1(base)	_MMIO((base) + 0xc4c)
#define   HECI_H_GS1_ER_PREP	REG_BIT(0)

/*
 * The FWSTS register values are FW defined and can be different between
 * HECI1 and HECI2
 */
#define HECI_FWSTS1				0xc40
#define   HECI1_FWSTS1_CURRENT_STATE			REG_GENMASK(3, 0)
#define   HECI1_FWSTS1_CURRENT_STATE_RESET		0
#define   HECI1_FWSTS1_PROXY_STATE_NORMAL		5
#define   HECI1_FWSTS1_INIT_COMPLETE			REG_BIT(9)
#define HECI_FWSTS2				0xc48
#define HECI_FWSTS3				0xc60
#define HECI_FWSTS4				0xc64
#define HECI_FWSTS5				0xc68
#define   HECI1_FWSTS5_HUC_AUTH_DONE	(1 << 19)
#define HECI_FWSTS6				0xc6c

/* the FWSTS regs are 1-based, so we use -base for index 0 to get an invalid reg */
#define HECI_FWSTS(base, x) _MMIO((base) + _PICK(x, -(base), \
						    HECI_FWSTS1, \
						    HECI_FWSTS2, \
						    HECI_FWSTS3, \
						    HECI_FWSTS4, \
						    HECI_FWSTS5, \
						    HECI_FWSTS6))

#define HSW_GTT_CACHE_EN	_MMIO(0x4024)
#define   GTT_CACHE_EN_ALL	0xF0007FFF
#define GEN7_WR_WATERMARK	_MMIO(0x4028)
#define GEN7_GFX_PRIO_CTRL	_MMIO(0x402C)
#define ARB_MODE		_MMIO(0x4030)
#define   ARB_MODE_SWIZZLE_SNB	(1 << 4)
#define   ARB_MODE_SWIZZLE_IVB	(1 << 5)
#define GEN7_GFX_PEND_TLB0	_MMIO(0x4034)
#define GEN7_GFX_PEND_TLB1	_MMIO(0x4038)
/* L3, CVS, ZTLB, RCC, CASC LRA min, max values */
#define GEN7_LRA_LIMITS(i)	_MMIO(0x403C + (i) * 4)
#define GEN7_LRA_LIMITS_REG_NUM	13
#define GEN7_MEDIA_MAX_REQ_COUNT	_MMIO(0x4070)
#define GEN7_GFX_MAX_REQ_COUNT		_MMIO(0x4074)

#define GEN7_ERR_INT	_MMIO(0x44040)
#define   ERR_INT_POISON		(1 << 31)
#define   ERR_INT_INVALID_GTT_PTE	(1 << 29)
#define   ERR_INT_INVALID_PTE_DATA	(1 << 28)
#define   ERR_INT_SPRITE_C_FAULT	(1 << 23)
#define   ERR_INT_PRIMARY_C_FAULT	(1 << 22)
#define   ERR_INT_CURSOR_C_FAULT	(1 << 21)
#define   ERR_INT_SPRITE_B_FAULT	(1 << 20)
#define   ERR_INT_PRIMARY_B_FAULT	(1 << 19)
#define   ERR_INT_CURSOR_B_FAULT	(1 << 18)
#define   ERR_INT_SPRITE_A_FAULT	(1 << 17)
#define   ERR_INT_PRIMARY_A_FAULT	(1 << 16)
#define   ERR_INT_CURSOR_A_FAULT	(1 << 15)
#define   ERR_INT_MMIO_UNCLAIMED	(1 << 13)
#define   ERR_INT_PIPE_CRC_DONE_C	(1 << 8)
#define   ERR_INT_FIFO_UNDERRUN_C	(1 << 6)
#define   ERR_INT_PIPE_CRC_DONE_B	(1 << 5)
#define   ERR_INT_FIFO_UNDERRUN_B	(1 << 3)
#define   ERR_INT_PIPE_CRC_DONE_A	(1 << 2)
#define   ERR_INT_PIPE_CRC_DONE(pipe)	(1 << (2 + (pipe) * 3))
#define   ERR_INT_FIFO_UNDERRUN_A	(1 << 0)
#define   ERR_INT_FIFO_UNDERRUN(pipe)	(1 << ((pipe) * 3))

#define FPGA_DBG		_MMIO(0x42300)
#define   FPGA_DBG_RM_NOCLAIM	REG_BIT(31)

#define CLAIM_ER		_MMIO(VLV_DISPLAY_BASE + 0x2028)
#define   CLAIM_ER_CLR		REG_BIT(31)
#define   CLAIM_ER_OVERFLOW	REG_BIT(16)
#define   CLAIM_ER_CTR_MASK	REG_GENMASK(15, 0)

#define VLV_GU_CTL0	_MMIO(VLV_DISPLAY_BASE + 0x2030)
#define VLV_GU_CTL1	_MMIO(VLV_DISPLAY_BASE + 0x2034)
#define SCPD0		_MMIO(0x209c) /* 915+ only */
#define  SCPD_FBC_IGNORE_3D			(1 << 6)
#define  CSTATE_RENDER_CLOCK_GATE_DISABLE	(1 << 5)
#define GEN2_IER	_MMIO(0x20a0)
#define GEN2_IIR	_MMIO(0x20a4)
#define GEN2_IMR	_MMIO(0x20a8)
#define GEN2_ISR	_MMIO(0x20ac)

#define GEN2_IRQ_REGS		I915_IRQ_REGS(GEN2_IMR, \
					      GEN2_IER, \
					      GEN2_IIR)

#define VLV_GUNIT_CLOCK_GATE	_MMIO(VLV_DISPLAY_BASE + 0x2060)
#define   GINT_DIS		(1 << 22)
#define   GCFG_DIS		(1 << 8)
#define VLV_GUNIT_CLOCK_GATE2	_MMIO(VLV_DISPLAY_BASE + 0x2064)
#define VLV_IIR_RW	_MMIO(VLV_DISPLAY_BASE + 0x2084)
#define VLV_IER		_MMIO(VLV_DISPLAY_BASE + 0x20a0)
#define VLV_IIR		_MMIO(VLV_DISPLAY_BASE + 0x20a4)
#define VLV_IMR		_MMIO(VLV_DISPLAY_BASE + 0x20a8)
#define VLV_ISR		_MMIO(VLV_DISPLAY_BASE + 0x20ac)
#define VLV_PCBR	_MMIO(VLV_DISPLAY_BASE + 0x2120)
#define VLV_PCBR_ADDR_SHIFT	12

#define EIR		_MMIO(0x20b0)
#define EMR		_MMIO(0x20b4)
#define ESR		_MMIO(0x20b8)
#define   GM45_ERROR_PAGE_TABLE				(1 << 5)
#define   GM45_ERROR_MEM_PRIV				(1 << 4)
#define   I915_ERROR_PAGE_TABLE				(1 << 4)
#define   GM45_ERROR_CP_PRIV				(1 << 3)
#define   I915_ERROR_MEMORY_REFRESH			(1 << 1)
#define   I915_ERROR_INSTRUCTION			(1 << 0)

#define GEN2_ERROR_REGS		I915_ERROR_REGS(EMR, EIR)

#define INSTPM	        _MMIO(0x20c0)
#define   INSTPM_SELF_EN (1 << 12) /* 915GM only */
#define   INSTPM_AGPBUSY_INT_EN (1 << 11) /* gen3: when disabled, pending interrupts
					will not assert AGPBUSY# and will only
					be delivered when out of C3. */
#define   INSTPM_FORCE_ORDERING				(1 << 7) /* GEN6+ */
#define   INSTPM_TLB_INVALIDATE	(1 << 9)
#define   INSTPM_SYNC_FLUSH	(1 << 5)
#define MEM_MODE	_MMIO(0x20cc)
#define   MEM_DISPLAY_B_TRICKLE_FEED_DISABLE (1 << 3) /* 830 only */
#define   MEM_DISPLAY_A_TRICKLE_FEED_DISABLE (1 << 2) /* 830/845 only */
#define   MEM_DISPLAY_TRICKLE_FEED_DISABLE (1 << 2) /* 85x only */
#define FW_BLC		_MMIO(0x20d8)
#define FW_BLC2		_MMIO(0x20dc)
#define FW_BLC_SELF	_MMIO(0x20e0) /* 915+ only */
#define   FW_BLC_SELF_EN_MASK      REG_BIT(31)
#define   FW_BLC_SELF_FIFO_MASK    REG_BIT(16) /* 945 only */
#define   FW_BLC_SELF_EN           REG_BIT(15) /* 945 only */
#define MM_BURST_LENGTH     0x00700000
#define MM_FIFO_WATERMARK   0x0001F000
#define LM_BURST_LENGTH     0x00000700
#define LM_FIFO_WATERMARK   0x0000001F
#define MI_ARB_STATE	_MMIO(0x20e4) /* 915+ only */

/*
 * Make render/texture TLB fetches lower priority than associated data
 * fetches. This is not turned on by default.
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

#define MI_STATE	_MMIO(0x20e4) /* gen2 only */
#define   MI_AGPBUSY_INT_EN			(1 << 1) /* 85x only */
#define   MI_AGPBUSY_830_MODE			(1 << 0) /* 85x only */

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
#define GT_WAIT_SEMAPHORE_INTERRUPT		REG_BIT(11) /* bdw+ */
#define GT_CONTEXT_SWITCH_INTERRUPT		(1 <<  8)
#define GT_RENDER_L3_PARITY_ERROR_INTERRUPT	(1 <<  5) /* !snb */
#define GT_RENDER_PIPECTL_NOTIFY_INTERRUPT	(1 <<  4)
#define GT_CS_MASTER_ERROR_INTERRUPT		REG_BIT(3)
#define GT_RENDER_SYNC_STATUS_INTERRUPT		(1 <<  2)
#define GT_RENDER_DEBUG_INTERRUPT		(1 <<  1)
#define GT_RENDER_USER_INTERRUPT		(1 <<  0)

#define PM_VEBOX_CS_ERROR_INTERRUPT		(1 << 12) /* hsw+ */
#define PM_VEBOX_USER_INTERRUPT			(1 << 10) /* hsw+ */

#define GT_PARITY_ERROR(dev_priv) \
	(GT_RENDER_L3_PARITY_ERROR_INTERRUPT | \
	 (IS_HASWELL(dev_priv) ? GT_RENDER_L3_PARITY_ERROR_INTERRUPT_S1 : 0))

/* These are all the "old" interrupts */
#define ILK_BSD_USER_INTERRUPT				(1 << 5)

#define I915_PM_INTERRUPT				(1 << 31)
#define I915_ISP_INTERRUPT				(1 << 22)
#define I915_LPE_PIPE_B_INTERRUPT			(1 << 21)
#define I915_LPE_PIPE_A_INTERRUPT			(1 << 20)
#define I915_MIPIC_INTERRUPT				(1 << 19)
#define I915_MIPIA_INTERRUPT				(1 << 18)
#define I915_PIPE_CONTROL_NOTIFY_INTERRUPT		(1 << 18)
#define I915_DISPLAY_PORT_INTERRUPT			(1 << 17)
#define I915_DISPLAY_PIPE_C_HBLANK_INTERRUPT		(1 << 16)
#define I915_MASTER_ERROR_INTERRUPT			(1 << 15)
#define I915_DISPLAY_PIPE_B_HBLANK_INTERRUPT		(1 << 14)
#define I915_GMCH_THERMAL_SENSOR_EVENT_INTERRUPT	(1 << 14) /* p-state */
#define I915_DISPLAY_PIPE_A_HBLANK_INTERRUPT		(1 << 13)
#define I915_HWB_OOM_INTERRUPT				(1 << 13)
#define I915_LPE_PIPE_C_INTERRUPT			(1 << 12)
#define I915_SYNC_STATUS_INTERRUPT			(1 << 12)
#define I915_MISC_INTERRUPT				(1 << 11)
#define I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT	(1 << 11)
#define I915_DISPLAY_PIPE_C_VBLANK_INTERRUPT		(1 << 10)
#define I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT	(1 << 10)
#define I915_DISPLAY_PIPE_C_EVENT_INTERRUPT		(1 << 9)
#define I915_OVERLAY_PLANE_FLIP_PENDING_INTERRUPT	(1 << 9)
#define I915_DISPLAY_PIPE_C_DPBM_INTERRUPT		(1 << 8)
#define I915_DISPLAY_PLANE_C_FLIP_PENDING_INTERRUPT	(1 << 8)
#define I915_DISPLAY_PIPE_A_VBLANK_INTERRUPT		(1 << 7)
#define I915_DISPLAY_PIPE_A_EVENT_INTERRUPT		(1 << 6)
#define I915_DISPLAY_PIPE_B_VBLANK_INTERRUPT		(1 << 5)
#define I915_DISPLAY_PIPE_B_EVENT_INTERRUPT		(1 << 4)
#define I915_DISPLAY_PIPE_A_DPBM_INTERRUPT		(1 << 3)
#define I915_DISPLAY_PIPE_B_DPBM_INTERRUPT		(1 << 2)
#define I915_DEBUG_INTERRUPT				(1 << 2)
#define I915_WINVALID_INTERRUPT				(1 << 1)
#define I915_USER_INTERRUPT				(1 << 1)
#define I915_ASLE_INTERRUPT				(1 << 0)
#define I915_BSD_USER_INTERRUPT				(1 << 25)

#define GEN6_BSD_RNCID			_MMIO(0x12198)

#define GEN7_FF_THREAD_MODE		_MMIO(0x20a0)
#define   GEN7_FF_SCHED_MASK		0x0077070
#define   GEN8_FF_DS_REF_CNT_FFME	(1 << 19)
#define   GEN12_FF_TESSELATION_DOP_GATE_DISABLE BIT(19)
#define   GEN7_FF_TS_SCHED_HS1		(0x5 << 16)
#define   GEN7_FF_TS_SCHED_HS0		(0x3 << 16)
#define   GEN7_FF_TS_SCHED_LOAD_BALANCE	(0x1 << 16)
#define   GEN7_FF_TS_SCHED_HW		(0x0 << 16) /* Default */
#define   GEN7_FF_VS_REF_CNT_FFME	(1 << 15)
#define   GEN7_FF_VS_SCHED_HS1		(0x5 << 12)
#define   GEN7_FF_VS_SCHED_HS0		(0x3 << 12)
#define   GEN7_FF_VS_SCHED_LOAD_BALANCE	(0x1 << 12) /* Default */
#define   GEN7_FF_VS_SCHED_HW		(0x0 << 12)
#define   GEN7_FF_DS_SCHED_HS1		(0x5 << 4)
#define   GEN7_FF_DS_SCHED_HS0		(0x3 << 4)
#define   GEN7_FF_DS_SCHED_LOAD_BALANCE	(0x1 << 4)  /* Default */
#define   GEN7_FF_DS_SCHED_HW		(0x0 << 4)

#define ILK_DISPLAY_CHICKEN1	_MMIO(0x42000)
#define   ILK_FBCQ_DIS			REG_BIT(22)
#define   ILK_PABSTRETCH_DIS		REG_BIT(21)
#define   ILK_SABSTRETCH_DIS		REG_BIT(20)
#define   IVB_PRI_STRETCH_MAX_MASK	REG_GENMASK(21, 20)
#define   IVB_PRI_STRETCH_MAX_X8	REG_FIELD_PREP(IVB_PRI_STRETCH_MAX_MASK, 0)
#define   IVB_PRI_STRETCH_MAX_X4	REG_FIELD_PREP(IVB_PRI_STRETCH_MAX_MASK, 1)
#define   IVB_PRI_STRETCH_MAX_X2	REG_FIELD_PREP(IVB_PRI_STRETCH_MAX_MASK, 2)
#define   IVB_PRI_STRETCH_MAX_X1	REG_FIELD_PREP(IVB_PRI_STRETCH_MAX_MASK, 3)
#define   IVB_SPR_STRETCH_MAX_MASK	REG_GENMASK(19, 18)
#define   IVB_SPR_STRETCH_MAX_X8	REG_FIELD_PREP(IVB_SPR_STRETCH_MAX_MASK, 0)
#define   IVB_SPR_STRETCH_MAX_X4	REG_FIELD_PREP(IVB_SPR_STRETCH_MAX_MASK, 1)
#define   IVB_SPR_STRETCH_MAX_X2	REG_FIELD_PREP(IVB_SPR_STRETCH_MAX_MASK, 2)
#define   IVB_SPR_STRETCH_MAX_X1	REG_FIELD_PREP(IVB_SPR_STRETCH_MAX_MASK, 3)

#define DPLL_TEST	_MMIO(0x606c)
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

#define D_STATE		_MMIO(0x6104)
#define  DSTATE_GFX_RESET_I830			(1 << 6)
#define  DSTATE_PLL_D3_OFF			(1 << 3)
#define  DSTATE_GFX_CLOCK_GATING		(1 << 1)
#define  DSTATE_DOT_CLOCK_GATING		(1 << 0)

#define DSPCLK_GATE_D			_MMIO(0x6200)
#define VLV_DSPCLK_GATE_D		_MMIO(VLV_DISPLAY_BASE + 0x6200)
# define DPUNIT_B_CLOCK_GATE_DISABLE		(1 << 30) /* 965 */
# define VSUNIT_CLOCK_GATE_DISABLE		(1 << 29) /* 965 */
# define VRHUNIT_CLOCK_GATE_DISABLE		(1 << 28) /* 965 */
# define VRDUNIT_CLOCK_GATE_DISABLE		(1 << 27) /* 965 */
# define AUDUNIT_CLOCK_GATE_DISABLE		(1 << 26) /* 965 */
# define DPUNIT_A_CLOCK_GATE_DISABLE		(1 << 25) /* 965 */
# define DPCUNIT_CLOCK_GATE_DISABLE		(1 << 24) /* 965 */
# define PNV_GMBUSUNIT_CLOCK_GATE_DISABLE	(1 << 24) /* pnv */
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

#define RENCLK_GATE_D1		_MMIO(0x6204)
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

#define RENCLK_GATE_D2		_MMIO(0x6208)
#define VF_UNIT_CLOCK_GATE_DISABLE		(1 << 9)
#define GS_UNIT_CLOCK_GATE_DISABLE		(1 << 7)
#define CL_UNIT_CLOCK_GATE_DISABLE		(1 << 6)

#define VDECCLK_GATE_D		_MMIO(0x620C)		/* g4x only */
#define  VCP_UNIT_CLOCK_GATE_DISABLE		(1 << 4)

#define RAMCLK_GATE_D		_MMIO(0x6210)		/* CRL only */
#define DEUC			_MMIO(0x6214)          /* CRL only */

#define BXT_RP_STATE_CAP        _MMIO(0x138170)
#define GEN9_RP_STATE_LIMITS	_MMIO(0x138148)

#define MTL_RP_STATE_CAP	_MMIO(0x138000)
#define MTL_MEDIAP_STATE_CAP	_MMIO(0x138020)
#define   MTL_RP0_CAP_MASK	REG_GENMASK(8, 0)
#define   MTL_RPN_CAP_MASK	REG_GENMASK(24, 16)

#define MTL_GT_RPE_FREQUENCY	_MMIO(0x13800c)
#define MTL_MPE_FREQUENCY	_MMIO(0x13802c)
#define   MTL_RPE_MASK		REG_GENMASK(8, 0)

#define GT0_PERF_LIMIT_REASONS		_MMIO(0x1381a8)
#define   GT0_PERF_LIMIT_REASONS_MASK	0xde3
#define   PROCHOT_MASK			REG_BIT(0)
#define   THERMAL_LIMIT_MASK		REG_BIT(1)
#define   RATL_MASK			REG_BIT(5)
#define   VR_THERMALERT_MASK		REG_BIT(6)
#define   VR_TDC_MASK			REG_BIT(7)
#define   POWER_LIMIT_4_MASK		REG_BIT(8)
#define   POWER_LIMIT_1_MASK		REG_BIT(10)
#define   POWER_LIMIT_2_MASK		REG_BIT(11)
#define   GT0_PERF_LIMIT_REASONS_LOG_MASK REG_GENMASK(31, 16)
#define MTL_MEDIA_PERF_LIMIT_REASONS	_MMIO(0x138030)

#define CHV_CLK_CTL1			_MMIO(0x101100)
#define VLV_CLK_CTL2			_MMIO(0x101104)
#define   CLK_CTL2_CZCOUNT_30NS_SHIFT	28

/*
 * GEN9 clock gating regs
 */
#define GEN9_CLKGATE_DIS_0		_MMIO(0x46530)
#define   DARBF_GATING_DIS		REG_BIT(27)
#define   MTL_PIPEDMC_GATING_DIS(pipe)	REG_BIT(15 - (pipe))
#define   PWM2_GATING_DIS		REG_BIT(14)
#define   PWM1_GATING_DIS		REG_BIT(13)

#define GEN9_CLKGATE_DIS_3		_MMIO(0x46538)
#define   TGL_VRH_GATING_DIS		REG_BIT(31)
#define   DPT_GATING_DIS		REG_BIT(22)

#define VLV_DPFLIPSTAT				_MMIO(VLV_DISPLAY_BASE + 0x70028)
#define   PIPEB_LINE_COMPARE_INT_EN			REG_BIT(29)
#define   PIPEB_HLINE_INT_EN			REG_BIT(28)
#define   PIPEB_VBLANK_INT_EN			REG_BIT(27)
#define   SPRITED_FLIP_DONE_INT_EN			REG_BIT(26)
#define   SPRITEC_FLIP_DONE_INT_EN			REG_BIT(25)
#define   PLANEB_FLIP_DONE_INT_EN			REG_BIT(24)
#define   PIPE_PSR_INT_EN			REG_BIT(22)
#define   PIPEA_LINE_COMPARE_INT_EN			REG_BIT(21)
#define   PIPEA_HLINE_INT_EN			REG_BIT(20)
#define   PIPEA_VBLANK_INT_EN			REG_BIT(19)
#define   SPRITEB_FLIP_DONE_INT_EN			REG_BIT(18)
#define   SPRITEA_FLIP_DONE_INT_EN			REG_BIT(17)
#define   PLANEA_FLIPDONE_INT_EN			REG_BIT(16)
#define   PIPEC_LINE_COMPARE_INT_EN			REG_BIT(13)
#define   PIPEC_HLINE_INT_EN			REG_BIT(12)
#define   PIPEC_VBLANK_INT_EN			REG_BIT(11)
#define   SPRITEF_FLIPDONE_INT_EN			REG_BIT(10)
#define   SPRITEE_FLIPDONE_INT_EN			REG_BIT(9)
#define   PLANEC_FLIPDONE_INT_EN			REG_BIT(8)

#define PCH_3DCGDIS0		_MMIO(0x46020)
# define MARIUNIT_CLOCK_GATE_DISABLE		(1 << 18)
# define SVSMUNIT_CLOCK_GATE_DISABLE		(1 << 1)

#define PCH_3DCGDIS1		_MMIO(0x46024)
# define VFMUNIT_CLOCK_GATE_DISABLE		(1 << 11)

/* Display Internal Timeout Register */
#define RM_TIMEOUT		_MMIO(0x42060)
#define RM_TIMEOUT_REG_CAPTURE	_MMIO(0x420E0)
#define  MMIO_TIMEOUT_US(us)	((us) << 0)

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
#define DE_PIPE_VBLANK(pipe)    (1 << (7 + 8 * (pipe)))
#define DE_PIPEA_EVEN_FIELD     (1 << 6)
#define DE_PIPEA_ODD_FIELD      (1 << 5)
#define DE_PIPEA_LINE_COMPARE   (1 << 4)
#define DE_PIPEA_VSYNC          (1 << 3)
#define DE_PIPEA_CRC_DONE	(1 << 2)
#define DE_PIPE_CRC_DONE(pipe)	(1 << (2 + 8 * (pipe)))
#define DE_PIPEA_FIFO_UNDERRUN  (1 << 0)
#define DE_PIPE_FIFO_UNDERRUN(pipe)  (1 << (8 * (pipe)))

#define VLV_MASTER_IER			_MMIO(0x4400c) /* Gunit master IER */
#define   MASTER_INTERRUPT_ENABLE	(1 << 31)

#define DEISR   _MMIO(0x44000)
#define DEIMR   _MMIO(0x44004)
#define DEIIR   _MMIO(0x44008)
#define DEIER   _MMIO(0x4400c)

#define DE_IRQ_REGS		I915_IRQ_REGS(DEIMR, \
					      DEIER, \
					      DEIIR)

#define GTISR   _MMIO(0x44010)
#define GTIMR   _MMIO(0x44014)
#define GTIIR   _MMIO(0x44018)
#define GTIER   _MMIO(0x4401c)

#define GT_IRQ_REGS		I915_IRQ_REGS(GTIMR, \
					      GTIER, \
					      GTIIR)

#define GEN8_MASTER_IRQ			_MMIO(0x44200)
#define  GEN8_MASTER_IRQ_CONTROL	(1 << 31)
#define  GEN8_PCU_IRQ			(1 << 30)
#define  GEN8_DE_PCH_IRQ		(1 << 23)
#define  GEN8_DE_MISC_IRQ		(1 << 22)
#define  GEN8_DE_PORT_IRQ		(1 << 20)
#define  GEN8_DE_PIPE_C_IRQ		(1 << 18)
#define  GEN8_DE_PIPE_B_IRQ		(1 << 17)
#define  GEN8_DE_PIPE_A_IRQ		(1 << 16)
#define  GEN8_DE_PIPE_IRQ(pipe)		(1 << (16 + (pipe)))
#define  GEN8_GT_VECS_IRQ		(1 << 6)
#define  GEN8_GT_GUC_IRQ		(1 << 5)
#define  GEN8_GT_PM_IRQ			(1 << 4)
#define  GEN8_GT_VCS1_IRQ		(1 << 3) /* NB: VCS2 in bspec! */
#define  GEN8_GT_VCS0_IRQ		(1 << 2) /* NB: VCS1 in bpsec! */
#define  GEN8_GT_BCS_IRQ		(1 << 1)
#define  GEN8_GT_RCS_IRQ		(1 << 0)

#define GEN8_GT_ISR(which) _MMIO(0x44300 + (0x10 * (which)))
#define GEN8_GT_IMR(which) _MMIO(0x44304 + (0x10 * (which)))
#define GEN8_GT_IIR(which) _MMIO(0x44308 + (0x10 * (which)))
#define GEN8_GT_IER(which) _MMIO(0x4430c + (0x10 * (which)))

#define GEN8_GT_IRQ_REGS(which)		I915_IRQ_REGS(GEN8_GT_IMR(which), \
						      GEN8_GT_IER(which), \
						      GEN8_GT_IIR(which))

#define GEN8_RCS_IRQ_SHIFT 0
#define GEN8_BCS_IRQ_SHIFT 16
#define GEN8_VCS0_IRQ_SHIFT 0  /* NB: VCS1 in bspec! */
#define GEN8_VCS1_IRQ_SHIFT 16 /* NB: VCS2 in bpsec! */
#define GEN8_VECS_IRQ_SHIFT 0
#define GEN8_WD_IRQ_SHIFT 16

#define GEN8_PCU_ISR _MMIO(0x444e0)
#define GEN8_PCU_IMR _MMIO(0x444e4)
#define GEN8_PCU_IIR _MMIO(0x444e8)
#define GEN8_PCU_IER _MMIO(0x444ec)

#define GEN8_PCU_IRQ_REGS		I915_IRQ_REGS(GEN8_PCU_IMR, \
						      GEN8_PCU_IER, \
						      GEN8_PCU_IIR)

#define GEN11_GU_MISC_ISR	_MMIO(0x444f0)
#define GEN11_GU_MISC_IMR	_MMIO(0x444f4)
#define GEN11_GU_MISC_IIR	_MMIO(0x444f8)
#define GEN11_GU_MISC_IER	_MMIO(0x444fc)
#define  GEN11_GU_MISC_GSE	(1 << 27)

#define GEN11_GU_MISC_IRQ_REGS		I915_IRQ_REGS(GEN11_GU_MISC_IMR, \
						      GEN11_GU_MISC_IER, \
						      GEN11_GU_MISC_IIR)

#define GEN11_GFX_MSTR_IRQ		_MMIO(0x190010)
#define  GEN11_MASTER_IRQ		(1 << 31)
#define  GEN11_PCU_IRQ			(1 << 30)
#define  GEN11_GU_MISC_IRQ		(1 << 29)
#define  GEN11_DISPLAY_IRQ		(1 << 16)
#define  GEN11_GT_DW_IRQ(x)		(1 << (x))
#define  GEN11_GT_DW1_IRQ		(1 << 1)
#define  GEN11_GT_DW0_IRQ		(1 << 0)

#define DG1_MSTR_TILE_INTR		_MMIO(0x190008)
#define   DG1_MSTR_IRQ			REG_BIT(31)
#define   DG1_MSTR_TILE(t)		REG_BIT(t)

#define ILK_DISPLAY_CHICKEN2	_MMIO(0x42004)
/* Required on all Ironlake and Sandybridge according to the B-Spec. */
#define   ILK_ELPIN_409_SELECT	REG_BIT(25)
#define   ILK_DPARB_GATE	REG_BIT(22)
#define   ILK_VSDPFD_FULL	REG_BIT(21)

#define ILK_DSPCLK_GATE_D	_MMIO(0x42020)
#define   ILK_VRHUNIT_CLOCK_GATE_DISABLE	REG_BIT(28)
#define   ILK_DPFCUNIT_CLOCK_GATE_DISABLE	REG_BIT(9)
#define   ILK_DPFCRUNIT_CLOCK_GATE_DISABLE	REG_BIT(8)
#define   ILK_DPFDUNIT_CLOCK_GATE_ENABLE	REG_BIT(7)
#define   ILK_DPARBUNIT_CLOCK_GATE_ENABLE	REG_BIT(5)

#define IVB_CHICKEN3		_MMIO(0x4200c)
#define   CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE	REG_BIT(5)
#define   CHICKEN3_DGMG_DONE_FIX_DISABLE	REG_BIT(2)

#define CHICKEN_PAR1_1		_MMIO(0x42080)
#define   IGNORE_KVMR_PIPE_A		REG_BIT(23)
#define   KBL_ARB_FILL_SPARE_22		REG_BIT(22)
#define   DIS_RAM_BYPASS_PSR2_MAN_TRACK	REG_BIT(16)
#define   SKL_DE_COMPRESSED_HASH_MODE	REG_BIT(15)
#define   HSW_MASK_VBL_TO_PIPE_IN_SRD	REG_BIT(15) /* hsw/bdw */
#define   FORCE_ARB_IDLE_PLANES		REG_BIT(14)
#define   SKL_EDP_PSR_FIX_RDWRAP	REG_BIT(3)
#define   IGNORE_PSR2_HW_TRACKING	REG_BIT(1)

#define CHICKEN_PAR2_1		_MMIO(0x42090)
#define   KVM_CONFIG_CHANGE_NOTIFICATION_SELECT	REG_BIT(14)

#define _CHICKEN_PIPESL_1_A	0x420b0
#define _CHICKEN_PIPESL_1_B	0x420b4
#define CHICKEN_PIPESL_1(pipe)	_MMIO_PIPE(pipe, _CHICKEN_PIPESL_1_A, _CHICKEN_PIPESL_1_B)
#define   HSW_PRI_STRETCH_MAX_MASK	REG_GENMASK(28, 27)
#define   HSW_PRI_STRETCH_MAX_X8	REG_FIELD_PREP(HSW_PRI_STRETCH_MAX_MASK, 0)
#define   HSW_PRI_STRETCH_MAX_X4	REG_FIELD_PREP(HSW_PRI_STRETCH_MAX_MASK, 1)
#define   HSW_PRI_STRETCH_MAX_X2	REG_FIELD_PREP(HSW_PRI_STRETCH_MAX_MASK, 2)
#define   HSW_PRI_STRETCH_MAX_X1	REG_FIELD_PREP(HSW_PRI_STRETCH_MAX_MASK, 3)
#define   HSW_SPR_STRETCH_MAX_MASK	REG_GENMASK(26, 25)
#define   HSW_SPR_STRETCH_MAX_X8	REG_FIELD_PREP(HSW_SPR_STRETCH_MAX_MASK, 0)
#define   HSW_SPR_STRETCH_MAX_X4	REG_FIELD_PREP(HSW_SPR_STRETCH_MAX_MASK, 1)
#define   HSW_SPR_STRETCH_MAX_X2	REG_FIELD_PREP(HSW_SPR_STRETCH_MAX_MASK, 2)
#define   HSW_SPR_STRETCH_MAX_X1	REG_FIELD_PREP(HSW_SPR_STRETCH_MAX_MASK, 3)
#define   HSW_FBCQ_DIS			REG_BIT(22)
#define   HSW_UNMASK_VBL_TO_REGS_IN_SRD REG_BIT(15) /* hsw */
#define   SKL_PSR_MASK_PLANE_FLIP	REG_BIT(11) /* skl+ */
#define   SKL_PLANE1_STRETCH_MAX_MASK	REG_GENMASK(1, 0)
#define   SKL_PLANE1_STRETCH_MAX_X8	REG_FIELD_PREP(SKL_PLANE1_STRETCH_MAX_MASK, 0)
#define   SKL_PLANE1_STRETCH_MAX_X4	REG_FIELD_PREP(SKL_PLANE1_STRETCH_MAX_MASK, 1)
#define   SKL_PLANE1_STRETCH_MAX_X2	REG_FIELD_PREP(SKL_PLANE1_STRETCH_MAX_MASK, 2)
#define   SKL_PLANE1_STRETCH_MAX_X1	REG_FIELD_PREP(SKL_PLANE1_STRETCH_MAX_MASK, 3)
#define   BDW_UNMASK_VBL_TO_REGS_IN_SRD	REG_BIT(0) /* bdw */

#define DISP_ARB_CTL	_MMIO(0x45000)
#define   DISP_FBC_MEMORY_WAKE		REG_BIT(31)
#define   DISP_TILE_SURFACE_SWIZZLING	REG_BIT(13)
#define   DISP_FBC_WM_DIS		REG_BIT(15)

#define GEN8_CHICKEN_DCPR_1			_MMIO(0x46430)
#define   _LATENCY_REPORTING_REMOVED_PIPE_D	REG_BIT(31)
#define   SKL_SELECT_ALTERNATE_DC_EXIT		REG_BIT(30)
#define   _LATENCY_REPORTING_REMOVED_PIPE_C	REG_BIT(25)
#define   _LATENCY_REPORTING_REMOVED_PIPE_B	REG_BIT(24)
#define   _LATENCY_REPORTING_REMOVED_PIPE_A	REG_BIT(23)
#define   LATENCY_REPORTING_REMOVED(pipe)	_PICK((pipe), \
						      _LATENCY_REPORTING_REMOVED_PIPE_A, \
						      _LATENCY_REPORTING_REMOVED_PIPE_B, \
						      _LATENCY_REPORTING_REMOVED_PIPE_C, \
						      _LATENCY_REPORTING_REMOVED_PIPE_D)
#define   ICL_DELAY_PMRSP			REG_BIT(22)
#define   DISABLE_FLR_SRC			REG_BIT(15)
#define   MASK_WAKEMEM				REG_BIT(13)
#define   DDI_CLOCK_REG_ACCESS			REG_BIT(7)

#define GMD_ID_DISPLAY				_MMIO(0x510a0)
#define   GMD_ID_ARCH_MASK			REG_GENMASK(31, 22)
#define   GMD_ID_RELEASE_MASK			REG_GENMASK(21, 14)
#define   GMD_ID_STEP				REG_GENMASK(5, 0)

/* PCH */

#define SDEISR  _MMIO(0xc4000)
#define SDEIMR  _MMIO(0xc4004)
#define SDEIIR  _MMIO(0xc4008)
#define SDEIER  _MMIO(0xc400c)

/* Icelake PPS_DATA and _ECC DIP Registers.
 * These are available for transcoders B,C and eDP.
 * Adding the _A so as to reuse the _MMIO_TRANS2
 * definition, with which it offsets to the right location.
 */

#define _TRANSA_CHICKEN1	 0xf0060
#define _TRANSB_CHICKEN1	 0xf1060
#define TRANS_CHICKEN1(pipe)	_MMIO_PIPE(pipe, _TRANSA_CHICKEN1, _TRANSB_CHICKEN1)
#define   TRANS_CHICKEN1_HDMIUNIT_GC_DISABLE	REG_BIT(10)
#define   TRANS_CHICKEN1_DP0UNIT_GC_DISABLE	REG_BIT(4)

#define _TRANSA_CHICKEN2	 0xf0064
#define _TRANSB_CHICKEN2	 0xf1064
#define TRANS_CHICKEN2(pipe)	_MMIO_PIPE(pipe, _TRANSA_CHICKEN2, _TRANSB_CHICKEN2)
#define   TRANS_CHICKEN2_TIMING_OVERRIDE		REG_BIT(31)
#define   TRANS_CHICKEN2_FDI_POLARITY_REVERSED		REG_BIT(29)
#define   TRANS_CHICKEN2_FRAME_START_DELAY_MASK		REG_GENMASK(28, 27)
#define   TRANS_CHICKEN2_FRAME_START_DELAY(x)		REG_FIELD_PREP(TRANS_CHICKEN2_FRAME_START_DELAY_MASK, (x)) /* 0-3 */
#define   TRANS_CHICKEN2_DISABLE_DEEP_COLOR_COUNTER	REG_BIT(26)
#define   TRANS_CHICKEN2_DISABLE_DEEP_COLOR_MODESWITCH	REG_BIT(25)

#define SOUTH_CHICKEN1		_MMIO(0xc2000)
#define  FDIA_PHASE_SYNC_SHIFT_OVR	19
#define  FDIA_PHASE_SYNC_SHIFT_EN	18
#define  INVERT_DDIE_HPD			REG_BIT(28)
#define  INVERT_DDID_HPD_MTP			REG_BIT(27)
#define  INVERT_TC4_HPD				REG_BIT(26)
#define  INVERT_TC3_HPD				REG_BIT(25)
#define  INVERT_TC2_HPD				REG_BIT(24)
#define  INVERT_TC1_HPD				REG_BIT(23)
#define  INVERT_DDID_HPD			(1 << 18)
#define  INVERT_DDIC_HPD			(1 << 17)
#define  INVERT_DDIB_HPD			(1 << 16)
#define  INVERT_DDIA_HPD			(1 << 15)
#define  FDI_PHASE_SYNC_OVR(pipe) (1 << (FDIA_PHASE_SYNC_SHIFT_OVR - ((pipe) * 2)))
#define  FDI_PHASE_SYNC_EN(pipe) (1 << (FDIA_PHASE_SYNC_SHIFT_EN - ((pipe) * 2)))
#define  FDI_BC_BIFURCATION_SELECT	(1 << 12)
#define  CHASSIS_CLK_REQ_DURATION_MASK	(0xf << 8)
#define  CHASSIS_CLK_REQ_DURATION(x)	((x) << 8)
#define  SBCLK_RUN_REFCLK_DIS		(1 << 7)
#define  ICP_SECOND_PPS_IO_SELECT	REG_BIT(2)
#define  SPT_PWM_GRANULARITY		(1 << 0)
#define SOUTH_CHICKEN2		_MMIO(0xc2004)
#define  FDI_MPHY_IOSFSB_RESET_STATUS	(1 << 13)
#define  FDI_MPHY_IOSFSB_RESET_CTL	(1 << 12)
#define  LPT_PWM_GRANULARITY		(1 << 5)
#define  DPLS_EDP_PPS_FIX_DIS		(1 << 0)

#define SOUTH_DSPCLK_GATE_D	_MMIO(0xc2020)
#define  PCH_GMBUSUNIT_CLOCK_GATE_DISABLE (1 << 31)
#define  PCH_DPLUNIT_CLOCK_GATE_DISABLE (1 << 30)
#define  PCH_DPLSUNIT_CLOCK_GATE_DISABLE (1 << 29)
#define  PCH_DPMGUNIT_CLOCK_GATE_DISABLE (1 << 15)
#define  PCH_CPUNIT_CLOCK_GATE_DISABLE (1 << 14)
#define  CNP_PWM_CGE_GATING_DISABLE (1 << 13)
#define  PCH_LP_PARTITION_LEVEL_DISABLE  (1 << 12)

#define  VLV_PMWGICZ				_MMIO(0x1300a4)

#define  HSW_EDRAM_CAP				_MMIO(0x120010)
#define    EDRAM_ENABLED			0x1
#define    EDRAM_NUM_BANKS(cap)			(((cap) >> 1) & 0xf)
#define    EDRAM_WAYS_IDX(cap)			(((cap) >> 5) & 0x7)
#define    EDRAM_SETS_IDX(cap)			(((cap) >> 8) & 0x3)

#define GEN6_PCODE_MAILBOX			_MMIO(0x138124)
#define   GEN6_PCODE_READY			(1 << 31)
#define   GEN6_PCODE_MB_PARAM2			REG_GENMASK(23, 16)
#define   GEN6_PCODE_MB_PARAM1			REG_GENMASK(15, 8)
#define   GEN6_PCODE_MB_COMMAND			REG_GENMASK(7, 0)
#define   GEN6_PCODE_ERROR_MASK			0xFF
#define     GEN6_PCODE_SUCCESS			0x0
#define     GEN6_PCODE_ILLEGAL_CMD		0x1
#define     GEN6_PCODE_MIN_FREQ_TABLE_GT_RATIO_OUT_OF_RANGE 0x2
#define     GEN6_PCODE_TIMEOUT			0x3
#define     GEN6_PCODE_UNIMPLEMENTED_CMD	0xFF
#define     GEN7_PCODE_TIMEOUT			0x2
#define     GEN7_PCODE_ILLEGAL_DATA		0x3
#define     GEN11_PCODE_ILLEGAL_SUBCOMMAND	0x4
#define     GEN11_PCODE_LOCKED			0x6
#define     GEN11_PCODE_REJECTED		0x11
#define     GEN7_PCODE_MIN_FREQ_TABLE_GT_RATIO_OUT_OF_RANGE 0x10
#define   GEN6_PCODE_WRITE_RC6VIDS		0x4
#define   GEN6_PCODE_READ_RC6VIDS		0x5
#define     GEN6_ENCODE_RC6_VID(mv)		(((mv) - 245) / 5)
#define     GEN6_DECODE_RC6_VID(vids)		(((vids) * 5) + 245)
#define   BDW_PCODE_DISPLAY_FREQ_CHANGE_REQ	0x18
#define   GEN9_PCODE_READ_MEM_LATENCY		0x6
#define     GEN9_MEM_LATENCY_LEVEL_3_7_MASK	REG_GENMASK(31, 24)
#define     GEN9_MEM_LATENCY_LEVEL_2_6_MASK	REG_GENMASK(23, 16)
#define     GEN9_MEM_LATENCY_LEVEL_1_5_MASK	REG_GENMASK(15, 8)
#define     GEN9_MEM_LATENCY_LEVEL_0_4_MASK	REG_GENMASK(7, 0)
#define   SKL_PCODE_LOAD_HDCP_KEYS		0x5
#define   SKL_PCODE_CDCLK_CONTROL		0x7
#define     SKL_CDCLK_PREPARE_FOR_CHANGE	0x3
#define     SKL_CDCLK_READY_FOR_CHANGE		0x1
#define   GEN6_PCODE_WRITE_MIN_FREQ_TABLE	0x8
#define   GEN6_PCODE_READ_MIN_FREQ_TABLE	0x9
#define   GEN6_READ_OC_PARAMS			0xc
#define   ICL_PCODE_MEM_SUBSYSYSTEM_INFO	0xd
#define     ICL_PCODE_MEM_SS_READ_GLOBAL_INFO	(0x0 << 8)
#define     ICL_PCODE_MEM_SS_READ_QGV_POINT_INFO(point)	(((point) << 16) | (0x1 << 8))
#define     ADL_PCODE_MEM_SS_READ_PSF_GV_INFO	((0) | (0x2 << 8))
#define   DISPLAY_TO_PCODE_CDCLK_MAX		0x28D
#define   DISPLAY_TO_PCODE_VOLTAGE_MASK		REG_GENMASK(1, 0)
#define	  DISPLAY_TO_PCODE_VOLTAGE_MAX		DISPLAY_TO_PCODE_VOLTAGE_MASK
#define   DISPLAY_TO_PCODE_CDCLK_VALID		REG_BIT(27)
#define   DISPLAY_TO_PCODE_PIPE_COUNT_VALID	REG_BIT(31)
#define   DISPLAY_TO_PCODE_CDCLK_MASK		REG_GENMASK(25, 16)
#define   DISPLAY_TO_PCODE_PIPE_COUNT_MASK	REG_GENMASK(30, 28)
#define   DISPLAY_TO_PCODE_CDCLK(x)		REG_FIELD_PREP(DISPLAY_TO_PCODE_CDCLK_MASK, (x))
#define   DISPLAY_TO_PCODE_PIPE_COUNT(x)	REG_FIELD_PREP(DISPLAY_TO_PCODE_PIPE_COUNT_MASK, (x))
#define   DISPLAY_TO_PCODE_VOLTAGE(x)		REG_FIELD_PREP(DISPLAY_TO_PCODE_VOLTAGE_MASK, (x))
#define   DISPLAY_TO_PCODE_UPDATE_MASK(cdclk, num_pipes, voltage_level) \
		((DISPLAY_TO_PCODE_CDCLK(cdclk)) | \
		(DISPLAY_TO_PCODE_PIPE_COUNT(num_pipes)) | \
		(DISPLAY_TO_PCODE_VOLTAGE(voltage_level)))
#define   ICL_PCODE_SAGV_DE_MEM_SS_CONFIG	0xe
#define     ICL_PCODE_REP_QGV_MASK		REG_GENMASK(1, 0)
#define     ICL_PCODE_REP_QGV_SAFE		REG_FIELD_PREP(ICL_PCODE_REP_QGV_MASK, 0)
#define     ICL_PCODE_REP_QGV_POLL		REG_FIELD_PREP(ICL_PCODE_REP_QGV_MASK, 1)
#define     ICL_PCODE_REP_QGV_REJECTED		REG_FIELD_PREP(ICL_PCODE_REP_QGV_MASK, 2)
#define     ADLS_PCODE_REP_PSF_MASK		REG_GENMASK(3, 2)
#define     ADLS_PCODE_REP_PSF_SAFE		REG_FIELD_PREP(ADLS_PCODE_REP_PSF_MASK, 0)
#define     ADLS_PCODE_REP_PSF_POLL		REG_FIELD_PREP(ADLS_PCODE_REP_PSF_MASK, 1)
#define     ADLS_PCODE_REP_PSF_REJECTED		REG_FIELD_PREP(ADLS_PCODE_REP_PSF_MASK, 2)
#define     ICL_PCODE_REQ_QGV_PT_MASK		REG_GENMASK(7, 0)
#define     ICL_PCODE_REQ_QGV_PT(x)		REG_FIELD_PREP(ICL_PCODE_REQ_QGV_PT_MASK, (x))
#define     ADLS_PCODE_REQ_PSF_PT_MASK		REG_GENMASK(10, 8)
#define     ADLS_PCODE_REQ_PSF_PT(x)		REG_FIELD_PREP(ADLS_PCODE_REQ_PSF_PT_MASK, (x))
#define   GEN6_PCODE_READ_D_COMP		0x10
#define   GEN6_PCODE_WRITE_D_COMP		0x11
#define   ICL_PCODE_EXIT_TCCOLD			0x12
#define   HSW_PCODE_DE_WRITE_FREQ_REQ		0x17
#define   DISPLAY_IPS_CONTROL			0x19
#define   TGL_PCODE_TCCOLD			0x26
#define     TGL_PCODE_EXIT_TCCOLD_DATA_L_EXIT_FAILED	REG_BIT(0)
#define     TGL_PCODE_EXIT_TCCOLD_DATA_L_BLOCK_REQ	0
#define     TGL_PCODE_EXIT_TCCOLD_DATA_L_UNBLOCK_REQ	REG_BIT(0)
            /* See also IPS_CTL */
#define     IPS_PCODE_CONTROL			(1 << 30)
#define   HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL	0x1A
#define   GEN9_PCODE_SAGV_CONTROL		0x21
#define     GEN9_SAGV_DISABLE			0x0
#define     GEN9_SAGV_IS_DISABLED		0x1
#define     GEN9_SAGV_ENABLE			0x3
#define   DG1_PCODE_STATUS			0x7E
#define     DG1_UNCORE_GET_INIT_STATUS		0x0
#define     DG1_UNCORE_INIT_STATUS_COMPLETE	0x1
#define   PCODE_POWER_SETUP			0x7C
#define     POWER_SETUP_SUBCOMMAND_READ_I1	0x4
#define     POWER_SETUP_SUBCOMMAND_WRITE_I1	0x5
#define	    POWER_SETUP_I1_WATTS		REG_BIT(31)
#define	    POWER_SETUP_I1_SHIFT		6	/* 10.6 fixed point format */
#define	    POWER_SETUP_I1_DATA_MASK		REG_GENMASK(15, 0)
#define     POWER_SETUP_SUBCOMMAND_G8_ENABLE	0x6
#define GEN12_PCODE_READ_SAGV_BLOCK_TIME_US	0x23
#define   XEHP_PCODE_FREQUENCY_CONFIG		0x6e	/* pvc */
/* XEHP_PCODE_FREQUENCY_CONFIG sub-commands (param1) */
#define     PCODE_MBOX_FC_SC_READ_FUSED_P0	0x0
#define     PCODE_MBOX_FC_SC_READ_FUSED_PN	0x1
/* PCODE_MBOX_DOMAIN_* - mailbox domain IDs */
/*   XEHP_PCODE_FREQUENCY_CONFIG param2 */
#define     PCODE_MBOX_DOMAIN_NONE		0x0
#define     PCODE_MBOX_DOMAIN_MEDIAFF		0x3
#define GEN6_PCODE_DATA				_MMIO(0x138128)
#define   GEN6_PCODE_FREQ_IA_RATIO_SHIFT	8
#define   GEN6_PCODE_FREQ_RING_RATIO_SHIFT	16
#define GEN6_PCODE_DATA1			_MMIO(0x13812C)

#define MTL_PCODE_STOLEN_ACCESS			_MMIO(0x138914)
#define   STOLEN_ACCESS_ALLOWED			0x1

/* IVYBRIDGE DPF */
#define GEN7_L3CDERRST1(slice)		_MMIO(0xB008 + (slice) * 0x200) /* L3CD Error Status 1 */
#define   GEN7_L3CDERRST1_ROW_MASK	(0x7ff << 14)
#define   GEN7_PARITY_ERROR_VALID	(1 << 13)
#define   GEN7_L3CDERRST1_BANK_MASK	(3 << 11)
#define   GEN7_L3CDERRST1_SUBBANK_MASK	(7 << 8)
#define GEN7_PARITY_ERROR_ROW(reg) \
		(((reg) & GEN7_L3CDERRST1_ROW_MASK) >> 14)
#define GEN7_PARITY_ERROR_BANK(reg) \
		(((reg) & GEN7_L3CDERRST1_BANK_MASK) >> 11)
#define GEN7_PARITY_ERROR_SUBBANK(reg) \
		(((reg) & GEN7_L3CDERRST1_SUBBANK_MASK) >> 8)
#define   GEN7_L3CDERRST1_ENABLE	(1 << 7)

/* These are the 4 32-bit write offset registers for each stream
 * output buffer.  It determines the offset from the
 * 3DSTATE_SO_BUFFERs that the next streamed vertex output goes to.
 */
#define GEN7_SO_WRITE_OFFSET(n)		_MMIO(0x5280 + (n) * 4)

#define GEN9_TIMESTAMP_OVERRIDE				_MMIO(0x44074)
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_SHIFT	0
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_MASK	0x3ff
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_SHIFT	12
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_MASK	(0xf << 12)

#define GGC				_MMIO(0x108040)
#define   GMS_MASK			REG_GENMASK(15, 8)
#define   GGMS_MASK			REG_GENMASK(7, 6)

#define GEN6_GSMBASE			_MMIO(0x108100)
#define GEN6_DSMBASE			_MMIO(0x1080C0)
#define   GEN6_BDSM_MASK		REG_GENMASK64(31, 20)
#define   GEN11_BDSM_MASK		REG_GENMASK64(63, 20)

#define XEHP_CLOCK_GATE_DIS		_MMIO(0x101014)
#define   SGSI_SIDECLK_DIS		REG_BIT(17)
#define   SGGI_DIS			REG_BIT(15)
#define   SGR_DIS			REG_BIT(13)

#define PRIMARY_SPI_TRIGGER			_MMIO(0x102040)
#define PRIMARY_SPI_ADDRESS			_MMIO(0x102080)
#define PRIMARY_SPI_REGIONID			_MMIO(0x102084)
#define SPI_STATIC_REGIONS			_MMIO(0x102090)
#define   OPTIONROM_SPI_REGIONID_MASK		REG_GENMASK(7, 0)
#define OROM_OFFSET				_MMIO(0x1020c0)
#define   OROM_OFFSET_MASK			REG_GENMASK(20, 16)

#define MTL_MEM_SS_INFO_GLOBAL			_MMIO(0x45700)
#define   MTL_N_OF_ENABLED_QGV_POINTS_MASK	REG_GENMASK(11, 8)
#define   MTL_N_OF_POPULATED_CH_MASK		REG_GENMASK(7, 4)
#define   MTL_DDR_TYPE_MASK			REG_GENMASK(3, 0)

#define MTL_MEDIA_GSI_BASE		0x380000

#endif /* _I915_REG_H_ */
