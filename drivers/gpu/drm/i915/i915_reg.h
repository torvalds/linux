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
 * Try to re-use existing register macro definitions. Only add new macros for
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

#define _VGA_MSR_WRITE _MMIO(0x3c2)

#define _GEN7_PIPEA_DE_LOAD_SL	0x70068
#define _GEN7_PIPEB_DE_LOAD_SL	0x71068
#define GEN7_PIPE_DE_LOAD_SL(pipe) _MMIO_PIPE(pipe, _GEN7_PIPEA_DE_LOAD_SL, _GEN7_PIPEB_DE_LOAD_SL)

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

#define DPIO_CTL			_MMIO(VLV_DISPLAY_BASE + 0x2110)
#define  DPIO_MODSEL1			(1 << 3) /* if ref clk b == 27 */
#define  DPIO_MODSEL0			(1 << 2) /* if ref clk a == 27 */
#define  DPIO_SFR_BYPASS		(1 << 1)
#define  DPIO_CMNRST			(1 << 0)

#define BXT_P_CR_GT_DISP_PWRON		_MMIO(0x138090)
#define  MIPIO_RST_CTRL				(1 << 2)

#define _BXT_PHY_CTL_DDI_A		0x64C00
#define _BXT_PHY_CTL_DDI_B		0x64C10
#define _BXT_PHY_CTL_DDI_C		0x64C20
#define   BXT_PHY_CMNLANE_POWERDOWN_ACK	(1 << 10)
#define   BXT_PHY_LANE_POWERDOWN_ACK	(1 << 9)
#define   BXT_PHY_LANE_ENABLED		(1 << 8)
#define BXT_PHY_CTL(port)		_MMIO_PORT(port, _BXT_PHY_CTL_DDI_A, \
							 _BXT_PHY_CTL_DDI_B)

#define _PHY_CTL_FAMILY_DDI		0x64C90
#define _PHY_CTL_FAMILY_EDP		0x64C80
#define _PHY_CTL_FAMILY_DDI_C		0x64CA0
#define   COMMON_RESET_DIS		(1 << 31)
#define BXT_PHY_CTL_FAMILY(phy)							\
	 _MMIO(_PICK_EVEN_2RANGES(phy, 1,					\
				  _PHY_CTL_FAMILY_DDI, _PHY_CTL_FAMILY_DDI,	\
				  _PHY_CTL_FAMILY_EDP, _PHY_CTL_FAMILY_DDI_C))

/* UAIMI scratch pad register 1 */
#define UAIMI_SPR1			_MMIO(0x4F074)
/* SKL VccIO mask */
#define SKL_VCCIO_MASK			0x1
/* SKL balance leg register */
#define DISPIO_CR_TX_BMU_CR0		_MMIO(0x6C00C)
/* I_boost values */
#define BALANCE_LEG_SHIFT(port)		(8 + 3 * (port))
#define BALANCE_LEG_MASK(port)		(7 << (8 + 3 * (port)))
/* Balance leg disable bits */
#define BALANCE_LEG_DISABLE_SHIFT	23
#define BALANCE_LEG_DISABLE(port)	(1 << (23 + (port)))

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

#define DERRMR		_MMIO(0x44050)
/* Note that HBLANK events are reserved on bdw+ */
#define   DERRMR_PIPEA_SCANLINE		(1 << 0)
#define   DERRMR_PIPEA_PRI_FLIP_DONE	(1 << 1)
#define   DERRMR_PIPEA_SPR_FLIP_DONE	(1 << 2)
#define   DERRMR_PIPEA_VBLANK		(1 << 3)
#define   DERRMR_PIPEA_HBLANK		(1 << 5)
#define   DERRMR_PIPEB_SCANLINE		(1 << 8)
#define   DERRMR_PIPEB_PRI_FLIP_DONE	(1 << 9)
#define   DERRMR_PIPEB_SPR_FLIP_DONE	(1 << 10)
#define   DERRMR_PIPEB_VBLANK		(1 << 11)
#define   DERRMR_PIPEB_HBLANK		(1 << 13)
/* Note that PIPEC is not a simple translation of PIPEA/PIPEB */
#define   DERRMR_PIPEC_SCANLINE		(1 << 14)
#define   DERRMR_PIPEC_PRI_FLIP_DONE	(1 << 15)
#define   DERRMR_PIPEC_SPR_FLIP_DONE	(1 << 20)
#define   DERRMR_PIPEC_VBLANK		(1 << 21)
#define   DERRMR_PIPEC_HBLANK		(1 << 22)

#define VLV_GU_CTL0	_MMIO(VLV_DISPLAY_BASE + 0x2030)
#define VLV_GU_CTL1	_MMIO(VLV_DISPLAY_BASE + 0x2034)
#define SCPD0		_MMIO(0x209c) /* 915+ only */
#define  SCPD_FBC_IGNORE_3D			(1 << 6)
#define  CSTATE_RENDER_CLOCK_GATE_DISABLE	(1 << 5)
#define GEN2_IER	_MMIO(0x20a0)
#define GEN2_IIR	_MMIO(0x20a4)
#define GEN2_IMR	_MMIO(0x20a8)
#define GEN2_ISR	_MMIO(0x20ac)
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

#define   DISPLAY_PLANE_FLIP_PENDING(plane) (1 << (11 - (plane))) /* A and B only */
#define EIR		_MMIO(0x20b0)
#define EMR		_MMIO(0x20b4)
#define ESR		_MMIO(0x20b8)
#define   GM45_ERROR_PAGE_TABLE				(1 << 5)
#define   GM45_ERROR_MEM_PRIV				(1 << 4)
#define   I915_ERROR_PAGE_TABLE				(1 << 4)
#define   GM45_ERROR_CP_PRIV				(1 << 3)
#define   I915_ERROR_MEMORY_REFRESH			(1 << 1)
#define   I915_ERROR_INSTRUCTION			(1 << 0)
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
#define   FW_BLC_SELF_EN_MASK      (1 << 31)
#define   FW_BLC_SELF_FIFO_MASK    (1 << 16) /* 945 only */
#define   FW_BLC_SELF_EN           (1 << 15) /* 945 only */
#define MM_BURST_LENGTH     0x00700000
#define MM_FIFO_WATERMARK   0x0001F000
#define LM_BURST_LENGTH     0x00000700
#define LM_FIFO_WATERMARK   0x0000001F
#define MI_ARB_STATE	_MMIO(0x20e4) /* 915+ only */

#define _MBUS_ABOX0_CTL			0x45038
#define _MBUS_ABOX1_CTL			0x45048
#define _MBUS_ABOX2_CTL			0x4504C
#define MBUS_ABOX_CTL(x)							\
	_MMIO(_PICK_EVEN_2RANGES(x, 2,						\
				 _MBUS_ABOX0_CTL, _MBUS_ABOX1_CTL,		\
				 _MBUS_ABOX2_CTL, _MBUS_ABOX2_CTL))

#define MBUS_ABOX_BW_CREDIT_MASK	(3 << 20)
#define MBUS_ABOX_BW_CREDIT(x)		((x) << 20)
#define MBUS_ABOX_B_CREDIT_MASK		(0xF << 16)
#define MBUS_ABOX_B_CREDIT(x)		((x) << 16)
#define MBUS_ABOX_BT_CREDIT_POOL2_MASK	(0x1F << 8)
#define MBUS_ABOX_BT_CREDIT_POOL2(x)	((x) << 8)
#define MBUS_ABOX_BT_CREDIT_POOL1_MASK	(0x1F << 0)
#define MBUS_ABOX_BT_CREDIT_POOL1(x)	((x) << 0)

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

#define IPS_CTL		_MMIO(0x43408)
#define   IPS_ENABLE		REG_BIT(31)
#define   IPS_FALSE_COLOR	REG_BIT(4)

/*
 * Clock control & power management
 */
#define _DPLL_A			0x6014
#define _DPLL_B			0x6018
#define _CHV_DPLL_C		0x6030
#define DPLL(pipe)		_MMIO_BASE_PIPE3(DISPLAY_MMIO_BASE(dev_priv), \
						 (pipe), _DPLL_A, _DPLL_B, _CHV_DPLL_C)

#define VGA0	_MMIO(0x6000)
#define VGA1	_MMIO(0x6004)
#define VGA_PD	_MMIO(0x6010)
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
#define   DPLL_REF_CLK_ENABLE_VLV	(1 << 29)
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
#define   DPLL_LOCK_VLV			(1 << 15)
#define   DPLL_INTEGRATED_CRI_CLK_VLV	(1 << 14)
#define   DPLL_INTEGRATED_REF_CLK_VLV	(1 << 13)
#define   DPLL_SSC_REF_CLK_CHV		(1 << 13)
#define   DPLL_PORTC_READY_MASK		(0xf << 4)
#define   DPLL_PORTB_READY_MASK		(0xf)

#define   DPLL_FPA01_P1_POST_DIV_MASK_I830	0x001f0000

/* Additional CHV pll/phy registers */
#define DPIO_PHY_STATUS			_MMIO(VLV_DISPLAY_BASE + 0x6240)
#define   DPLL_PORTD_READY_MASK		(0xf)
#define DISPLAY_PHY_CONTROL _MMIO(VLV_DISPLAY_BASE + 0x60100)
#define   PHY_CH_POWER_DOWN_OVRD_EN(phy, ch)	(1 << (2 * (phy) + (ch) + 27))
#define   PHY_LDO_DELAY_0NS			0x0
#define   PHY_LDO_DELAY_200NS			0x1
#define   PHY_LDO_DELAY_600NS			0x2
#define   PHY_LDO_SEQ_DELAY(delay, phy)		((delay) << (2 * (phy) + 23))
#define   PHY_CH_POWER_DOWN_OVRD(mask, phy, ch)	((mask) << (8 * (phy) + 4 * (ch) + 11))
#define   PHY_CH_SU_PSR				0x1
#define   PHY_CH_DEEP_PSR			0x7
#define   PHY_CH_POWER_MODE(mode, phy, ch)	((mode) << (6 * (phy) + 3 * (ch) + 2))
#define   PHY_COM_LANE_RESET_DEASSERT(phy)	(1 << (phy))
#define DISPLAY_PHY_STATUS _MMIO(VLV_DISPLAY_BASE + 0x60104)
#define   PHY_POWERGOOD(phy)	(((phy) == DPIO_PHY0) ? (1 << 31) : (1 << 30))
#define   PHY_STATUS_CMN_LDO(phy, ch)                   (1 << (6 - (6 * (phy) + 3 * (ch))))
#define   PHY_STATUS_SPLINE_LDO(phy, ch, spline)        (1 << (8 - (6 * (phy) + 3 * (ch) + (spline))))

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
# define PLL_REF_SDVO_HDMI_MULTIPLIER(x)	(((x) - 1) << 9)
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

#define _DPLL_A_MD		0x601c
#define _DPLL_B_MD		0x6020
#define _CHV_DPLL_C_MD		0x603c
#define DPLL_MD(pipe)		_MMIO_BASE_PIPE3(DISPLAY_MMIO_BASE(dev_priv), \
						 (pipe), _DPLL_A_MD, _DPLL_B_MD, _CHV_DPLL_C_MD)

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

#define RAWCLK_FREQ_VLV		_MMIO(VLV_DISPLAY_BASE + 0x6024)

#define _FPA0	0x6040
#define _FPA1	0x6044
#define _FPB0	0x6048
#define _FPB1	0x604c
#define FP0(pipe) _MMIO_PIPE(pipe, _FPA0, _FPB0)
#define FP1(pipe) _MMIO_PIPE(pipe, _FPA1, _FPB1)
#define   FP_N_DIV_MASK		0x003f0000
#define   FP_N_PINEVIEW_DIV_MASK	0x00ff0000
#define   FP_N_DIV_SHIFT		16
#define   FP_M1_DIV_MASK	0x00003f00
#define   FP_M1_DIV_SHIFT		 8
#define   FP_M2_DIV_MASK	0x0000003f
#define   FP_M2_PINEVIEW_DIV_MASK	0x000000ff
#define   FP_M2_DIV_SHIFT		 0
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
#define DSPCLK_GATE_D(__i915)		_MMIO(DISPLAY_MMIO_BASE(__i915) + 0x6200)
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

#define FW_BLC_SELF_VLV		_MMIO(VLV_DISPLAY_BASE + 0x6500)
#define  FW_CSPWRDWNEN		(1 << 15)

#define MI_ARB_VLV		_MMIO(VLV_DISPLAY_BASE + 0x6504)

#define CZCLK_CDCLK_FREQ_RATIO	_MMIO(VLV_DISPLAY_BASE + 0x6508)
#define   CDCLK_FREQ_SHIFT	4
#define   CDCLK_FREQ_MASK	(0x1f << CDCLK_FREQ_SHIFT)
#define   CZCLK_FREQ_MASK	0xf

#define GCI_CONTROL		_MMIO(VLV_DISPLAY_BASE + 0x650C)
#define   PFI_CREDIT_63		(9 << 28)		/* chv only */
#define   PFI_CREDIT_31		(8 << 28)		/* chv only */
#define   PFI_CREDIT(x)		(((x) - 8) << 28)	/* 8-15 */
#define   PFI_CREDIT_RESEND	(1 << 27)
#define   VGA_FAST_MODE_DISABLE	(1 << 14)

#define GMBUSFREQ_VLV		_MMIO(VLV_DISPLAY_BASE + 0x6510)

#define PEG_BAND_GAP_DATA	_MMIO(0x14d68)

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
 * Overlay regs
 */

#define OVADD			_MMIO(0x30000)
#define DOVSTA			_MMIO(0x30008)
#define OC_BUF			(0x3 << 20)
#define OGAMC5			_MMIO(0x30010)
#define OGAMC4			_MMIO(0x30014)
#define OGAMC3			_MMIO(0x30018)
#define OGAMC2			_MMIO(0x3001c)
#define OGAMC1			_MMIO(0x30020)
#define OGAMC0			_MMIO(0x30024)

/*
 * GEN9 clock gating regs
 */
#define GEN9_CLKGATE_DIS_0		_MMIO(0x46530)
#define   DARBF_GATING_DIS		REG_BIT(27)
#define   MTL_PIPEDMC_GATING_DIS_A	REG_BIT(15)
#define   MTL_PIPEDMC_GATING_DIS_B	REG_BIT(14)
#define   PWM2_GATING_DIS		REG_BIT(14)
#define   PWM1_GATING_DIS		REG_BIT(13)

#define GEN9_CLKGATE_DIS_3		_MMIO(0x46538)
#define   TGL_VRH_GATING_DIS		REG_BIT(31)
#define   DPT_GATING_DIS		REG_BIT(22)

#define GEN9_CLKGATE_DIS_4		_MMIO(0x4653C)
#define   BXT_GMBUS_GATING_DIS		(1 << 14)

#define GEN9_CLKGATE_DIS_5		_MMIO(0x46540)
#define   DPCE_GATING_DIS		REG_BIT(17)

#define _CLKGATE_DIS_PSL_A		0x46520
#define _CLKGATE_DIS_PSL_B		0x46524
#define _CLKGATE_DIS_PSL_C		0x46528
#define   DUPS1_GATING_DIS		(1 << 15)
#define   DUPS2_GATING_DIS		(1 << 19)
#define   DUPS3_GATING_DIS		(1 << 23)
#define   CURSOR_GATING_DIS		REG_BIT(28)
#define   DPF_GATING_DIS		(1 << 10)
#define   DPF_RAM_GATING_DIS		(1 << 9)
#define   DPFR_GATING_DIS		(1 << 8)

#define CLKGATE_DIS_PSL(pipe) \
	_MMIO_PIPE(pipe, _CLKGATE_DIS_PSL_A, _CLKGATE_DIS_PSL_B)

#define _CLKGATE_DIS_PSL_EXT_A		0x4654C
#define _CLKGATE_DIS_PSL_EXT_B		0x46550
#define   PIPEDMC_GATING_DIS		REG_BIT(12)

#define CLKGATE_DIS_PSL_EXT(pipe) \
	_MMIO_PIPE(pipe, _CLKGATE_DIS_PSL_EXT_A, _CLKGATE_DIS_PSL_EXT_B)

/* DDI Buffer Control */
#define _DDI_CLK_VALFREQ_A		0x64030
#define _DDI_CLK_VALFREQ_B		0x64130
#define DDI_CLK_VALFREQ(port)		_MMIO_PORT(port, _DDI_CLK_VALFREQ_A, _DDI_CLK_VALFREQ_B)

/*
 * Display engine regs
 */

/* Pipe A CRC regs */
#define _PIPE_CRC_CTL_A			0x60050
#define   PIPE_CRC_ENABLE		REG_BIT(31)
/* skl+ source selection */
#define   PIPE_CRC_SOURCE_MASK_SKL	REG_GENMASK(30, 28)
#define   PIPE_CRC_SOURCE_PLANE_1_SKL	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_SKL, 0)
#define   PIPE_CRC_SOURCE_PLANE_2_SKL	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_SKL, 2)
#define   PIPE_CRC_SOURCE_DMUX_SKL	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_SKL, 4)
#define   PIPE_CRC_SOURCE_PLANE_3_SKL	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_SKL, 6)
#define   PIPE_CRC_SOURCE_PLANE_4_SKL	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_SKL, 7)
#define   PIPE_CRC_SOURCE_PLANE_5_SKL	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_SKL, 5)
#define   PIPE_CRC_SOURCE_PLANE_6_SKL	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_SKL, 3)
#define   PIPE_CRC_SOURCE_PLANE_7_SKL	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_SKL, 1)
/* ivb+ source selection */
#define   PIPE_CRC_SOURCE_MASK_IVB	REG_GENMASK(30, 29)
#define   PIPE_CRC_SOURCE_PRIMARY_IVB	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_IVB, 0)
#define   PIPE_CRC_SOURCE_SPRITE_IVB	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_IVB, 1)
#define   PIPE_CRC_SOURCE_PF_IVB	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_IVB, 2)
/* ilk+ source selection */
#define   PIPE_CRC_SOURCE_MASK_ILK	REG_GENMASK(30, 28)
#define   PIPE_CRC_SOURCE_PRIMARY_ILK	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_ILK, 0)
#define   PIPE_CRC_SOURCE_SPRITE_ILK	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_ILK, 1)
#define   PIPE_CRC_SOURCE_PIPE_ILK	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_ILK, 2)
/* embedded DP port on the north display block */
#define   PIPE_CRC_SOURCE_PORT_A_ILK	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_ILK, 4)
#define   PIPE_CRC_SOURCE_FDI_ILK	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_ILK, 5)
/* vlv source selection */
#define   PIPE_CRC_SOURCE_MASK_VLV	REG_GENMASK(30, 27)
#define   PIPE_CRC_SOURCE_PIPE_VLV	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_VLV, 0)
#define   PIPE_CRC_SOURCE_HDMIB_VLV	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_VLV, 1)
#define   PIPE_CRC_SOURCE_HDMIC_VLV	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_VLV, 2)
/* with DP port the pipe source is invalid */
#define   PIPE_CRC_SOURCE_DP_D_VLV	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_VLV, 3)
#define   PIPE_CRC_SOURCE_DP_B_VLV	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_VLV, 6)
#define   PIPE_CRC_SOURCE_DP_C_VLV	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_VLV, 7)
/* gen3+ source selection */
#define   PIPE_CRC_SOURCE_MASK_I9XX	REG_GENMASK(30, 28)
#define   PIPE_CRC_SOURCE_PIPE_I9XX	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_I9XX, 0)
#define   PIPE_CRC_SOURCE_SDVOB_I9XX	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_I9XX, 1)
#define   PIPE_CRC_SOURCE_SDVOC_I9XX	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_I9XX, 2)
/* with DP/TV port the pipe source is invalid */
#define   PIPE_CRC_SOURCE_DP_D_G4X	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_I9XX, 3)
#define   PIPE_CRC_SOURCE_TV_PRE	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_I9XX, 4)
#define   PIPE_CRC_SOURCE_TV_POST	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_I9XX, 5)
#define   PIPE_CRC_SOURCE_DP_B_G4X	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_I9XX, 6)
#define   PIPE_CRC_SOURCE_DP_C_G4X	REG_FIELD_PREP(PIPE_CRC_SOURCE_MASK_I9XX, 7)
/* gen2 doesn't have source selection bits */
#define   PIPE_CRC_INCLUDE_BORDER_I8XX	REG_BIT(30)

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

#define PIPE_CRC_CTL(dev_priv, pipe)		_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_CTL_A)
#define PIPE_CRC_RES_1_IVB(dev_priv, pipe)	_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_1_A_IVB)
#define PIPE_CRC_RES_2_IVB(dev_priv, pipe)	_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_2_A_IVB)
#define PIPE_CRC_RES_3_IVB(dev_priv, pipe)	_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_3_A_IVB)
#define PIPE_CRC_RES_4_IVB(dev_priv, pipe)	_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_4_A_IVB)
#define PIPE_CRC_RES_5_IVB(dev_priv, pipe)	_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_5_A_IVB)

#define PIPE_CRC_RES_RED(dev_priv, pipe)		_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_RED_A)
#define PIPE_CRC_RES_GREEN(dev_priv, pipe)	_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_GREEN_A)
#define PIPE_CRC_RES_BLUE(dev_priv, pipe)		_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_BLUE_A)
#define PIPE_CRC_RES_RES1_I915(dev_priv, pipe)	_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_RES1_A_I915)
#define PIPE_CRC_RES_RES2_G4X(dev_priv, pipe)	_MMIO_TRANS2(dev_priv, pipe, _PIPE_CRC_RES_RES2_A_G4X)

/* Pipe/transcoder A timing regs */
#define _TRANS_HTOTAL_A		0x60000
#define   HTOTAL_MASK			REG_GENMASK(31, 16)
#define   HTOTAL(htotal)		REG_FIELD_PREP(HTOTAL_MASK, (htotal))
#define   HACTIVE_MASK			REG_GENMASK(15, 0)
#define   HACTIVE(hdisplay)		REG_FIELD_PREP(HACTIVE_MASK, (hdisplay))
#define _TRANS_HBLANK_A		0x60004
#define   HBLANK_END_MASK		REG_GENMASK(31, 16)
#define   HBLANK_END(hblank_end)	REG_FIELD_PREP(HBLANK_END_MASK, (hblank_end))
#define   HBLANK_START_MASK		REG_GENMASK(15, 0)
#define   HBLANK_START(hblank_start)	REG_FIELD_PREP(HBLANK_START_MASK, (hblank_start))
#define _TRANS_HSYNC_A		0x60008
#define   HSYNC_END_MASK		REG_GENMASK(31, 16)
#define   HSYNC_END(hsync_end)		REG_FIELD_PREP(HSYNC_END_MASK, (hsync_end))
#define   HSYNC_START_MASK		REG_GENMASK(15, 0)
#define   HSYNC_START(hsync_start)	REG_FIELD_PREP(HSYNC_START_MASK, (hsync_start))
#define _TRANS_VTOTAL_A		0x6000c
#define   VTOTAL_MASK			REG_GENMASK(31, 16)
#define   VTOTAL(vtotal)		REG_FIELD_PREP(VTOTAL_MASK, (vtotal))
#define   VACTIVE_MASK			REG_GENMASK(15, 0)
#define   VACTIVE(vdisplay)		REG_FIELD_PREP(VACTIVE_MASK, (vdisplay))
#define _TRANS_VBLANK_A		0x60010
#define   VBLANK_END_MASK		REG_GENMASK(31, 16)
#define   VBLANK_END(vblank_end)	REG_FIELD_PREP(VBLANK_END_MASK, (vblank_end))
#define   VBLANK_START_MASK		REG_GENMASK(15, 0)
#define   VBLANK_START(vblank_start)	REG_FIELD_PREP(VBLANK_START_MASK, (vblank_start))
#define _TRANS_VSYNC_A		0x60014
#define   VSYNC_END_MASK		REG_GENMASK(31, 16)
#define   VSYNC_END(vsync_end)		REG_FIELD_PREP(VSYNC_END_MASK, (vsync_end))
#define   VSYNC_START_MASK		REG_GENMASK(15, 0)
#define   VSYNC_START(vsync_start)	REG_FIELD_PREP(VSYNC_START_MASK, (vsync_start))
#define _TRANS_EXITLINE_A	0x60018
#define _PIPEASRC		0x6001c
#define   PIPESRC_WIDTH_MASK	REG_GENMASK(31, 16)
#define   PIPESRC_WIDTH(w)	REG_FIELD_PREP(PIPESRC_WIDTH_MASK, (w))
#define   PIPESRC_HEIGHT_MASK	REG_GENMASK(15, 0)
#define   PIPESRC_HEIGHT(h)	REG_FIELD_PREP(PIPESRC_HEIGHT_MASK, (h))
#define _BCLRPAT_A		0x60020
#define _TRANS_VSYNCSHIFT_A	0x60028
#define _TRANS_MULT_A		0x6002c

/* Pipe/transcoder B timing regs */
#define _TRANS_HTOTAL_B		0x61000
#define _TRANS_HBLANK_B		0x61004
#define _TRANS_HSYNC_B		0x61008
#define _TRANS_VTOTAL_B		0x6100c
#define _TRANS_VBLANK_B		0x61010
#define _TRANS_VSYNC_B		0x61014
#define _PIPEBSRC		0x6101c
#define _BCLRPAT_B		0x61020
#define _TRANS_VSYNCSHIFT_B	0x61028
#define _TRANS_MULT_B		0x6102c

/* DSI 0 timing regs */
#define _TRANS_HTOTAL_DSI0	0x6b000
#define _TRANS_HSYNC_DSI0	0x6b008
#define _TRANS_VTOTAL_DSI0	0x6b00c
#define _TRANS_VSYNC_DSI0	0x6b014
#define _TRANS_VSYNCSHIFT_DSI0	0x6b028

/* DSI 1 timing regs */
#define _TRANS_HTOTAL_DSI1	0x6b800
#define _TRANS_HSYNC_DSI1	0x6b808
#define _TRANS_VTOTAL_DSI1	0x6b80c
#define _TRANS_VSYNC_DSI1	0x6b814
#define _TRANS_VSYNCSHIFT_DSI1	0x6b828

#define TRANS_HTOTAL(trans)	_MMIO_TRANS2(dev_priv, (trans), _TRANS_HTOTAL_A)
#define TRANS_HBLANK(trans)	_MMIO_TRANS2(dev_priv, (trans), _TRANS_HBLANK_A)
#define TRANS_HSYNC(trans)	_MMIO_TRANS2(dev_priv, (trans), _TRANS_HSYNC_A)
#define TRANS_VTOTAL(trans)	_MMIO_TRANS2(dev_priv, (trans), _TRANS_VTOTAL_A)
#define TRANS_VBLANK(trans)	_MMIO_TRANS2(dev_priv, (trans), _TRANS_VBLANK_A)
#define TRANS_VSYNC(trans)	_MMIO_TRANS2(dev_priv, (trans), _TRANS_VSYNC_A)
#define BCLRPAT(trans)		_MMIO_TRANS2(dev_priv, (trans), _BCLRPAT_A)
#define TRANS_VSYNCSHIFT(trans)	_MMIO_TRANS2(dev_priv, (trans), _TRANS_VSYNCSHIFT_A)
#define PIPESRC(pipe)		_MMIO_TRANS2(dev_priv, (pipe), _PIPEASRC)
#define TRANS_MULT(trans)	_MMIO_TRANS2(dev_priv, (trans), _TRANS_MULT_A)

/* VRR registers */
#define _TRANS_VRR_CTL_A		0x60420
#define _TRANS_VRR_CTL_B		0x61420
#define _TRANS_VRR_CTL_C		0x62420
#define _TRANS_VRR_CTL_D		0x63420
#define TRANS_VRR_CTL(dev_priv, trans)			_MMIO_TRANS2(dev_priv, trans, _TRANS_VRR_CTL_A)
#define   VRR_CTL_VRR_ENABLE			REG_BIT(31)
#define   VRR_CTL_IGN_MAX_SHIFT			REG_BIT(30)
#define   VRR_CTL_FLIP_LINE_EN			REG_BIT(29)
#define   VRR_CTL_PIPELINE_FULL_MASK		REG_GENMASK(10, 3)
#define   VRR_CTL_PIPELINE_FULL(x)		REG_FIELD_PREP(VRR_CTL_PIPELINE_FULL_MASK, (x))
#define   VRR_CTL_PIPELINE_FULL_OVERRIDE	REG_BIT(0)
#define	  XELPD_VRR_CTL_VRR_GUARDBAND_MASK	REG_GENMASK(15, 0)
#define	  XELPD_VRR_CTL_VRR_GUARDBAND(x)	REG_FIELD_PREP(XELPD_VRR_CTL_VRR_GUARDBAND_MASK, (x))

#define _TRANS_VRR_VMAX_A		0x60424
#define _TRANS_VRR_VMAX_B		0x61424
#define _TRANS_VRR_VMAX_C		0x62424
#define _TRANS_VRR_VMAX_D		0x63424
#define TRANS_VRR_VMAX(dev_priv, trans)		_MMIO_TRANS2(dev_priv, trans, _TRANS_VRR_VMAX_A)
#define   VRR_VMAX_MASK			REG_GENMASK(19, 0)

#define _TRANS_VRR_VMIN_A		0x60434
#define _TRANS_VRR_VMIN_B		0x61434
#define _TRANS_VRR_VMIN_C		0x62434
#define _TRANS_VRR_VMIN_D		0x63434
#define TRANS_VRR_VMIN(dev_priv, trans)		_MMIO_TRANS2(dev_priv, trans, _TRANS_VRR_VMIN_A)
#define   VRR_VMIN_MASK			REG_GENMASK(15, 0)

#define _TRANS_VRR_VMAXSHIFT_A		0x60428
#define _TRANS_VRR_VMAXSHIFT_B		0x61428
#define _TRANS_VRR_VMAXSHIFT_C		0x62428
#define _TRANS_VRR_VMAXSHIFT_D		0x63428
#define TRANS_VRR_VMAXSHIFT(dev_priv, trans)	_MMIO_TRANS2(dev_priv, trans, \
					_TRANS_VRR_VMAXSHIFT_A)
#define   VRR_VMAXSHIFT_DEC_MASK	REG_GENMASK(29, 16)
#define   VRR_VMAXSHIFT_DEC		REG_BIT(16)
#define   VRR_VMAXSHIFT_INC_MASK	REG_GENMASK(12, 0)

#define _TRANS_VRR_STATUS_A		0x6042C
#define _TRANS_VRR_STATUS_B		0x6142C
#define _TRANS_VRR_STATUS_C		0x6242C
#define _TRANS_VRR_STATUS_D		0x6342C
#define TRANS_VRR_STATUS(dev_priv, trans)		_MMIO_TRANS2(dev_priv, trans, _TRANS_VRR_STATUS_A)
#define   VRR_STATUS_VMAX_REACHED	REG_BIT(31)
#define   VRR_STATUS_NOFLIP_TILL_BNDR	REG_BIT(30)
#define   VRR_STATUS_FLIP_BEF_BNDR	REG_BIT(29)
#define   VRR_STATUS_NO_FLIP_FRAME	REG_BIT(28)
#define   VRR_STATUS_VRR_EN_LIVE	REG_BIT(27)
#define   VRR_STATUS_FLIPS_SERVICED	REG_BIT(26)
#define   VRR_STATUS_VBLANK_MASK	REG_GENMASK(22, 20)
#define   STATUS_FSM_IDLE		REG_FIELD_PREP(VRR_STATUS_VBLANK_MASK, 0)
#define   STATUS_FSM_WAIT_TILL_FDB	REG_FIELD_PREP(VRR_STATUS_VBLANK_MASK, 1)
#define   STATUS_FSM_WAIT_TILL_FS	REG_FIELD_PREP(VRR_STATUS_VBLANK_MASK, 2)
#define   STATUS_FSM_WAIT_TILL_FLIP	REG_FIELD_PREP(VRR_STATUS_VBLANK_MASK, 3)
#define   STATUS_FSM_PIPELINE_FILL	REG_FIELD_PREP(VRR_STATUS_VBLANK_MASK, 4)
#define   STATUS_FSM_ACTIVE		REG_FIELD_PREP(VRR_STATUS_VBLANK_MASK, 5)
#define   STATUS_FSM_LEGACY_VBLANK	REG_FIELD_PREP(VRR_STATUS_VBLANK_MASK, 6)

#define _TRANS_VRR_VTOTAL_PREV_A	0x60480
#define _TRANS_VRR_VTOTAL_PREV_B	0x61480
#define _TRANS_VRR_VTOTAL_PREV_C	0x62480
#define _TRANS_VRR_VTOTAL_PREV_D	0x63480
#define TRANS_VRR_VTOTAL_PREV(dev_priv, trans)	_MMIO_TRANS2(dev_priv, trans, \
					_TRANS_VRR_VTOTAL_PREV_A)
#define   VRR_VTOTAL_FLIP_BEFR_BNDR	REG_BIT(31)
#define   VRR_VTOTAL_FLIP_AFTER_BNDR	REG_BIT(30)
#define   VRR_VTOTAL_FLIP_AFTER_DBLBUF	REG_BIT(29)
#define   VRR_VTOTAL_PREV_FRAME_MASK	REG_GENMASK(19, 0)

#define _TRANS_VRR_FLIPLINE_A		0x60438
#define _TRANS_VRR_FLIPLINE_B		0x61438
#define _TRANS_VRR_FLIPLINE_C		0x62438
#define _TRANS_VRR_FLIPLINE_D		0x63438
#define TRANS_VRR_FLIPLINE(dev_priv, trans)	_MMIO_TRANS2(dev_priv, trans, \
					_TRANS_VRR_FLIPLINE_A)
#define   VRR_FLIPLINE_MASK		REG_GENMASK(19, 0)

#define _TRANS_VRR_STATUS2_A		0x6043C
#define _TRANS_VRR_STATUS2_B		0x6143C
#define _TRANS_VRR_STATUS2_C		0x6243C
#define _TRANS_VRR_STATUS2_D		0x6343C
#define TRANS_VRR_STATUS2(dev_priv, trans)	_MMIO_TRANS2(dev_priv, trans, _TRANS_VRR_STATUS2_A)
#define   VRR_STATUS2_VERT_LN_CNT_MASK	REG_GENMASK(19, 0)

#define _TRANS_PUSH_A			0x60A70
#define _TRANS_PUSH_B			0x61A70
#define _TRANS_PUSH_C			0x62A70
#define _TRANS_PUSH_D			0x63A70
#define TRANS_PUSH(dev_priv, trans)		_MMIO_TRANS2(dev_priv, trans, _TRANS_PUSH_A)
#define   TRANS_PUSH_EN			REG_BIT(31)
#define   TRANS_PUSH_SEND		REG_BIT(30)

#define _TRANS_VRR_VSYNC_A		0x60078
#define TRANS_VRR_VSYNC(dev_priv, trans)		_MMIO_TRANS2(dev_priv, trans, _TRANS_VRR_VSYNC_A)
#define VRR_VSYNC_END_MASK		REG_GENMASK(28, 16)
#define VRR_VSYNC_END(vsync_end)	REG_FIELD_PREP(VRR_VSYNC_END_MASK, (vsync_end))
#define VRR_VSYNC_START_MASK		REG_GENMASK(12, 0)
#define VRR_VSYNC_START(vsync_start)	REG_FIELD_PREP(VRR_VSYNC_START_MASK, (vsync_start))

/* VGA port control */
#define ADPA			_MMIO(0x61100)
#define PCH_ADPA                _MMIO(0xe1100)
#define VLV_ADPA		_MMIO(VLV_DISPLAY_BASE + 0x61100)

#define   ADPA_DAC_ENABLE	(1 << 31)
#define   ADPA_DAC_DISABLE	0
#define   ADPA_PIPE_SEL_SHIFT		30
#define   ADPA_PIPE_SEL_MASK		(1 << 30)
#define   ADPA_PIPE_SEL(pipe)		((pipe) << 30)
#define   ADPA_PIPE_SEL_SHIFT_CPT	29
#define   ADPA_PIPE_SEL_MASK_CPT	(3 << 29)
#define   ADPA_PIPE_SEL_CPT(pipe)	((pipe) << 29)
#define   ADPA_CRT_HOTPLUG_MASK  0x03ff0000 /* bit 25-16 */
#define   ADPA_CRT_HOTPLUG_MONITOR_NONE  (0 << 24)
#define   ADPA_CRT_HOTPLUG_MONITOR_MASK  (3 << 24)
#define   ADPA_CRT_HOTPLUG_MONITOR_COLOR (3 << 24)
#define   ADPA_CRT_HOTPLUG_MONITOR_MONO  (2 << 24)
#define   ADPA_CRT_HOTPLUG_ENABLE        (1 << 23)
#define   ADPA_CRT_HOTPLUG_PERIOD_64     (0 << 22)
#define   ADPA_CRT_HOTPLUG_PERIOD_128    (1 << 22)
#define   ADPA_CRT_HOTPLUG_WARMUP_5MS    (0 << 21)
#define   ADPA_CRT_HOTPLUG_WARMUP_10MS   (1 << 21)
#define   ADPA_CRT_HOTPLUG_SAMPLE_2S     (0 << 20)
#define   ADPA_CRT_HOTPLUG_SAMPLE_4S     (1 << 20)
#define   ADPA_CRT_HOTPLUG_VOLTAGE_40    (0 << 18)
#define   ADPA_CRT_HOTPLUG_VOLTAGE_50    (1 << 18)
#define   ADPA_CRT_HOTPLUG_VOLTAGE_60    (2 << 18)
#define   ADPA_CRT_HOTPLUG_VOLTAGE_70    (3 << 18)
#define   ADPA_CRT_HOTPLUG_VOLREF_325MV  (0 << 17)
#define   ADPA_CRT_HOTPLUG_VOLREF_475MV  (1 << 17)
#define   ADPA_CRT_HOTPLUG_FORCE_TRIGGER (1 << 16)
#define   ADPA_USE_VGA_HVPOLARITY (1 << 15)
#define   ADPA_SETS_HVPOLARITY	0
#define   ADPA_VSYNC_CNTL_DISABLE (1 << 10)
#define   ADPA_VSYNC_CNTL_ENABLE 0
#define   ADPA_HSYNC_CNTL_DISABLE (1 << 11)
#define   ADPA_HSYNC_CNTL_ENABLE 0
#define   ADPA_VSYNC_ACTIVE_HIGH (1 << 4)
#define   ADPA_VSYNC_ACTIVE_LOW	0
#define   ADPA_HSYNC_ACTIVE_HIGH (1 << 3)
#define   ADPA_HSYNC_ACTIVE_LOW	0
#define   ADPA_DPMS_MASK	(~(3 << 10))
#define   ADPA_DPMS_ON		(0 << 10)
#define   ADPA_DPMS_SUSPEND	(1 << 10)
#define   ADPA_DPMS_STANDBY	(2 << 10)
#define   ADPA_DPMS_OFF		(3 << 10)


/* Hotplug control (945+ only) */
#define PORT_HOTPLUG_EN		_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61110)
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

#define PORT_HOTPLUG_STAT	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61114)
/* HDMI/DP bits are g4x+ */
#define   PORTD_HOTPLUG_LIVE_STATUS_G4X		(1 << 27)
#define   PORTC_HOTPLUG_LIVE_STATUS_G4X		(1 << 28)
#define   PORTB_HOTPLUG_LIVE_STATUS_G4X		(1 << 29)
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
#define _GEN3_SDVOB	0x61140
#define _GEN3_SDVOC	0x61160
#define GEN3_SDVOB	_MMIO(_GEN3_SDVOB)
#define GEN3_SDVOC	_MMIO(_GEN3_SDVOC)
#define GEN4_HDMIB	GEN3_SDVOB
#define GEN4_HDMIC	GEN3_SDVOC
#define VLV_HDMIB	_MMIO(VLV_DISPLAY_BASE + 0x61140)
#define VLV_HDMIC	_MMIO(VLV_DISPLAY_BASE + 0x61160)
#define CHV_HDMID	_MMIO(VLV_DISPLAY_BASE + 0x6116C)
#define PCH_SDVOB	_MMIO(0xe1140)
#define PCH_HDMIB	PCH_SDVOB
#define PCH_HDMIC	_MMIO(0xe1150)
#define PCH_HDMID	_MMIO(0xe1160)

#define PORT_DFT_I9XX				_MMIO(0x61150)
#define   DC_BALANCE_RESET			(1 << 25)
#define PORT_DFT2_G4X(dev_priv)		_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61154)
#define   DC_BALANCE_RESET_VLV			(1 << 31)
#define   PIPE_SCRAMBLE_RESET_MASK		((1 << 14) | (0x3 << 0))
#define   PIPE_C_SCRAMBLE_RESET			REG_BIT(14) /* chv */
#define   PIPE_B_SCRAMBLE_RESET			REG_BIT(1)
#define   PIPE_A_SCRAMBLE_RESET			REG_BIT(0)

/* Gen 3 SDVO bits: */
#define   SDVO_ENABLE				(1 << 31)
#define   SDVO_PIPE_SEL_SHIFT			30
#define   SDVO_PIPE_SEL_MASK			(1 << 30)
#define   SDVO_PIPE_SEL(pipe)			((pipe) << 30)
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
#define   HDMI_AUDIO_ENABLE			(1 << 6) /* HDMI only */
/* VSYNC/HSYNC bits new with 965, default is to be set */
#define   SDVO_VSYNC_ACTIVE_HIGH		(1 << 4)
#define   SDVO_HSYNC_ACTIVE_HIGH		(1 << 3)

/* Gen 5 (IBX) SDVO/HDMI bits: */
#define   HDMI_COLOR_FORMAT_12bpc		(3 << 26) /* HDMI only */
#define   SDVOB_HOTPLUG_ENABLE			(1 << 23) /* SDVO only */

/* Gen 6 (CPT) SDVO/HDMI bits: */
#define   SDVO_PIPE_SEL_SHIFT_CPT		29
#define   SDVO_PIPE_SEL_MASK_CPT		(3 << 29)
#define   SDVO_PIPE_SEL_CPT(pipe)		((pipe) << 29)

/* CHV SDVO/HDMI bits: */
#define   SDVO_PIPE_SEL_SHIFT_CHV		24
#define   SDVO_PIPE_SEL_MASK_CHV		(3 << 24)
#define   SDVO_PIPE_SEL_CHV(pipe)		((pipe) << 24)

/* Video Data Island Packet control */
#define VIDEO_DIP_DATA		_MMIO(0x61178)
/* Read the description of VIDEO_DIP_DATA (before Haswell) or VIDEO_DIP_ECC
 * (Haswell and newer) to see which VIDEO_DIP_DATA byte corresponds to each byte
 * of the infoframe structure specified by CEA-861. */
#define   VIDEO_DIP_DATA_SIZE	32
#define   VIDEO_DIP_ASYNC_DATA_SIZE	36
#define   VIDEO_DIP_GMP_DATA_SIZE	36
#define   VIDEO_DIP_VSC_DATA_SIZE	36
#define   VIDEO_DIP_PPS_DATA_SIZE	132
#define VIDEO_DIP_CTL		_MMIO(0x61170)
/* Pre HSW: */
#define   VIDEO_DIP_ENABLE		(1 << 31)
#define   VIDEO_DIP_PORT(port)		((port) << 29)
#define   VIDEO_DIP_PORT_MASK		(3 << 29)
#define   VIDEO_DIP_ENABLE_GCP		(1 << 25) /* ilk+ */
#define   VIDEO_DIP_ENABLE_AVI		(1 << 21)
#define   VIDEO_DIP_ENABLE_VENDOR	(2 << 21)
#define   VIDEO_DIP_ENABLE_GAMUT	(4 << 21) /* ilk+ */
#define   VIDEO_DIP_ENABLE_SPD		(8 << 21)
#define   VIDEO_DIP_SELECT_AVI		(0 << 19)
#define   VIDEO_DIP_SELECT_VENDOR	(1 << 19)
#define   VIDEO_DIP_SELECT_GAMUT	(2 << 19)
#define   VIDEO_DIP_SELECT_SPD		(3 << 19)
#define   VIDEO_DIP_SELECT_MASK		(3 << 19)
#define   VIDEO_DIP_FREQ_ONCE		(0 << 16)
#define   VIDEO_DIP_FREQ_VSYNC		(1 << 16)
#define   VIDEO_DIP_FREQ_2VSYNC		(2 << 16)
#define   VIDEO_DIP_FREQ_MASK		(3 << 16)
/* HSW and later: */
#define   VIDEO_DIP_ENABLE_DRM_GLK	(1 << 28)
#define   PSR_VSC_BIT_7_SET		(1 << 27)
#define   VSC_SELECT_MASK		(0x3 << 25)
#define   VSC_SELECT_SHIFT		25
#define   VSC_DIP_HW_HEA_DATA		(0 << 25)
#define   VSC_DIP_HW_HEA_SW_DATA	(1 << 25)
#define   VSC_DIP_HW_DATA_SW_HEA	(2 << 25)
#define   VSC_DIP_SW_HEA_DATA		(3 << 25)
#define   VDIP_ENABLE_PPS		(1 << 24)
#define   VIDEO_DIP_ENABLE_VSC_HSW	(1 << 20)
#define   VIDEO_DIP_ENABLE_GCP_HSW	(1 << 16)
#define   VIDEO_DIP_ENABLE_AVI_HSW	(1 << 12)
#define   VIDEO_DIP_ENABLE_VS_HSW	(1 << 8)
#define   VIDEO_DIP_ENABLE_GMP_HSW	(1 << 4)
#define   VIDEO_DIP_ENABLE_SPD_HSW	(1 << 0)
/* ADL and later: */
#define   VIDEO_DIP_ENABLE_AS_ADL	REG_BIT(23)

/* Panel fitting */
#define PFIT_CONTROL	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61230)
#define   PFIT_ENABLE			REG_BIT(31)
#define   PFIT_PIPE_MASK		REG_GENMASK(30, 29) /* 965+ */
#define   PFIT_PIPE(pipe)		REG_FIELD_PREP(PFIT_PIPE_MASK, (pipe))
#define   PFIT_SCALING_MASK		REG_GENMASK(28, 26) /* 965+ */
#define   PFIT_SCALING_AUTO		REG_FIELD_PREP(PFIT_SCALING_MASK, 0)
#define   PFIT_SCALING_PROGRAMMED	REG_FIELD_PREP(PFIT_SCALING_MASK, 1)
#define   PFIT_SCALING_PILLAR		REG_FIELD_PREP(PFIT_SCALING_MASK, 2)
#define   PFIT_SCALING_LETTER		REG_FIELD_PREP(PFIT_SCALING_MASK, 3)
#define   PFIT_FILTER_MASK		REG_GENMASK(25, 24) /* 965+ */
#define   PFIT_FILTER_FUZZY		REG_FIELD_PREP(PFIT_FILTER_MASK, 0)
#define   PFIT_FILTER_CRISP		REG_FIELD_PREP(PFIT_FILTER_MASK, 1)
#define   PFIT_FILTER_MEDIAN		REG_FIELD_PREP(PFIT_FILTER_MASK, 2)
#define   PFIT_VERT_INTERP_MASK		REG_GENMASK(11, 10) /* pre-965 */
#define   PFIT_VERT_INTERP_BILINEAR	REG_FIELD_PREP(PFIT_VERT_INTERP_MASK, 1)
#define   PFIT_VERT_AUTO_SCALE		REG_BIT(9) /* pre-965 */
#define   PFIT_HORIZ_INTERP_MASK	REG_GENMASK(7, 6) /* pre-965 */
#define   PFIT_HORIZ_INTERP_BILINEAR	REG_FIELD_PREP(PFIT_HORIZ_INTERP_MASK, 1)
#define   PFIT_HORIZ_AUTO_SCALE		REG_BIT(5) /* pre-965 */
#define   PFIT_PANEL_8TO6_DITHER_ENABLE	REG_BIT(3) /* pre-965 */

#define PFIT_PGM_RATIOS _MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61234)
#define   PFIT_VERT_SCALE_MASK		REG_GENMASK(31, 20) /* pre-965 */
#define   PFIT_VERT_SCALE(x)		REG_FIELD_PREP(PFIT_VERT_SCALE_MASK, (x))
#define   PFIT_HORIZ_SCALE_MASK		REG_GENMASK(15, 4) /* pre-965 */
#define   PFIT_HORIZ_SCALE(x)		REG_FIELD_PREP(PFIT_HORIZ_SCALE_MASK, (x))
#define   PFIT_VERT_SCALE_MASK_965	REG_GENMASK(28, 16) /* 965+ */
#define   PFIT_HORIZ_SCALE_MASK_965	REG_GENMASK(12, 0) /* 965+ */

#define PFIT_AUTO_RATIOS _MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61238)

#define PCH_GTC_CTL		_MMIO(0xe7000)
#define   PCH_GTC_ENABLE	(1 << 31)

/* Display Port */
#define DP_A			_MMIO(0x64000) /* eDP */
#define DP_B			_MMIO(0x64100)
#define DP_C			_MMIO(0x64200)
#define DP_D			_MMIO(0x64300)

#define VLV_DP_B		_MMIO(VLV_DISPLAY_BASE + 0x64100)
#define VLV_DP_C		_MMIO(VLV_DISPLAY_BASE + 0x64200)
#define CHV_DP_D		_MMIO(VLV_DISPLAY_BASE + 0x64300)

#define   DP_PORT_EN			(1 << 31)
#define   DP_PIPE_SEL_SHIFT		30
#define   DP_PIPE_SEL_MASK		(1 << 30)
#define   DP_PIPE_SEL(pipe)		((pipe) << 30)
#define   DP_PIPE_SEL_SHIFT_IVB		29
#define   DP_PIPE_SEL_MASK_IVB		(3 << 29)
#define   DP_PIPE_SEL_IVB(pipe)		((pipe) << 29)
#define   DP_PIPE_SEL_SHIFT_CHV		16
#define   DP_PIPE_SEL_MASK_CHV		(3 << 16)
#define   DP_PIPE_SEL_CHV(pipe)		((pipe) << 16)

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
#define   DP_PORT_WIDTH(width)		(((width) - 1) << 19)
#define   DP_PORT_WIDTH_MASK		(7 << 19)
#define   DP_PORT_WIDTH_SHIFT		19

/* Mystic DPCD version 1.1 special mode */
#define   DP_ENHANCED_FRAMING		(1 << 18)

/* eDP */
#define   DP_PLL_FREQ_270MHZ		(0 << 16)
#define   DP_PLL_FREQ_162MHZ		(1 << 16)
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
#define  TU_SIZE_MASK		REG_GENMASK(30, 25)
#define  TU_SIZE(x)		REG_FIELD_PREP(TU_SIZE_MASK, (x) - 1) /* default size 64 */

#define  DATA_LINK_M_N_MASK	REG_GENMASK(23, 0)
#define  DATA_LINK_N_MAX	(0x800000)

#define _PIPEA_DATA_N_G4X	0x70054
#define _PIPEB_DATA_N_G4X	0x71054

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
#define _PIPEA_LINK_N_G4X	0x70064
#define _PIPEB_LINK_N_G4X	0x71064

#define PIPE_DATA_M_G4X(pipe) _MMIO_PIPE(pipe, _PIPEA_DATA_M_G4X, _PIPEB_DATA_M_G4X)
#define PIPE_DATA_N_G4X(pipe) _MMIO_PIPE(pipe, _PIPEA_DATA_N_G4X, _PIPEB_DATA_N_G4X)
#define PIPE_LINK_M_G4X(pipe) _MMIO_PIPE(pipe, _PIPEA_LINK_M_G4X, _PIPEB_LINK_M_G4X)
#define PIPE_LINK_N_G4X(pipe) _MMIO_PIPE(pipe, _PIPEA_LINK_N_G4X, _PIPEB_LINK_N_G4X)

/* Pipe A */
#define _PIPEADSL		0x70000
#define   PIPEDSL_CURR_FIELD	REG_BIT(31) /* ctg+ */
#define   PIPEDSL_LINE_MASK	REG_GENMASK(19, 0)
#define _TRANSACONF		0x70008
#define   TRANSCONF_ENABLE			REG_BIT(31)
#define   TRANSCONF_DOUBLE_WIDE			REG_BIT(30) /* pre-i965 */
#define   TRANSCONF_STATE_ENABLE			REG_BIT(30) /* i965+ */
#define   TRANSCONF_DSI_PLL_LOCKED		REG_BIT(29) /* vlv & pipe A only */
#define   TRANSCONF_FRAME_START_DELAY_MASK	REG_GENMASK(28, 27) /* pre-hsw */
#define   TRANSCONF_FRAME_START_DELAY(x)		REG_FIELD_PREP(TRANSCONF_FRAME_START_DELAY_MASK, (x)) /* pre-hsw: 0-3 */
#define   TRANSCONF_PIPE_LOCKED			REG_BIT(25)
#define   TRANSCONF_FORCE_BORDER			REG_BIT(25)
#define   TRANSCONF_GAMMA_MODE_MASK_I9XX		REG_BIT(24) /* gmch */
#define   TRANSCONF_GAMMA_MODE_MASK_ILK		REG_GENMASK(25, 24) /* ilk-ivb */
#define   TRANSCONF_GAMMA_MODE_8BIT		REG_FIELD_PREP(TRANSCONF_GAMMA_MODE_MASK, 0)
#define   TRANSCONF_GAMMA_MODE_10BIT		REG_FIELD_PREP(TRANSCONF_GAMMA_MODE_MASK, 1)
#define   TRANSCONF_GAMMA_MODE_12BIT		REG_FIELD_PREP(TRANSCONF_GAMMA_MODE_MASK_ILK, 2) /* ilk-ivb */
#define   TRANSCONF_GAMMA_MODE_SPLIT		REG_FIELD_PREP(TRANSCONF_GAMMA_MODE_MASK_ILK, 3) /* ivb */
#define   TRANSCONF_GAMMA_MODE(x)		REG_FIELD_PREP(TRANSCONF_GAMMA_MODE_MASK_ILK, (x)) /* pass in GAMMA_MODE_MODE_* */
#define   TRANSCONF_INTERLACE_MASK		REG_GENMASK(23, 21) /* gen3+ */
#define   TRANSCONF_INTERLACE_PROGRESSIVE	REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK, 0)
#define   TRANSCONF_INTERLACE_W_SYNC_SHIFT_PANEL	REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK, 4) /* gen4 only */
#define   TRANSCONF_INTERLACE_W_SYNC_SHIFT	REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK, 5) /* gen4 only */
#define   TRANSCONF_INTERLACE_W_FIELD_INDICATION	REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK, 6)
#define   TRANSCONF_INTERLACE_FIELD_0_ONLY	REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK, 7) /* gen3 only */
/*
 * ilk+: PF/D=progressive fetch/display, IF/D=interlaced fetch/display,
 * DBL=power saving pixel doubling, PF-ID* requires panel fitter
 */
#define   TRANSCONF_INTERLACE_MASK_ILK		REG_GENMASK(23, 21) /* ilk+ */
#define   TRANSCONF_INTERLACE_MASK_HSW		REG_GENMASK(22, 21) /* hsw+ */
#define   TRANSCONF_INTERLACE_PF_PD_ILK		REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK_ILK, 0)
#define   TRANSCONF_INTERLACE_PF_ID_ILK		REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK_ILK, 1)
#define   TRANSCONF_INTERLACE_IF_ID_ILK		REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK_ILK, 3)
#define   TRANSCONF_INTERLACE_IF_ID_DBL_ILK	REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK_ILK, 4) /* ilk/snb only */
#define   TRANSCONF_INTERLACE_PF_ID_DBL_ILK	REG_FIELD_PREP(TRANSCONF_INTERLACE_MASK_ILK, 5) /* ilk/snb only */
#define   TRANSCONF_REFRESH_RATE_ALT_ILK		REG_BIT(20)
#define   TRANSCONF_MSA_TIMING_DELAY_MASK	REG_GENMASK(19, 18) /* ilk/snb/ivb */
#define   TRANSCONF_MSA_TIMING_DELAY(x)		REG_FIELD_PREP(TRANSCONF_MSA_TIMING_DELAY_MASK, (x))
#define   TRANSCONF_CXSR_DOWNCLOCK		REG_BIT(16)
#define   TRANSCONF_WGC_ENABLE			REG_BIT(15) /* vlv/chv only */
#define   TRANSCONF_REFRESH_RATE_ALT_VLV		REG_BIT(14)
#define   TRANSCONF_COLOR_RANGE_SELECT		REG_BIT(13)
#define   TRANSCONF_OUTPUT_COLORSPACE_MASK	REG_GENMASK(12, 11) /* ilk-ivb */
#define   TRANSCONF_OUTPUT_COLORSPACE_RGB	REG_FIELD_PREP(TRANSCONF_OUTPUT_COLORSPACE_MASK, 0) /* ilk-ivb */
#define   TRANSCONF_OUTPUT_COLORSPACE_YUV601	REG_FIELD_PREP(TRANSCONF_OUTPUT_COLORSPACE_MASK, 1) /* ilk-ivb */
#define   TRANSCONF_OUTPUT_COLORSPACE_YUV709	REG_FIELD_PREP(TRANSCONF_OUTPUT_COLORSPACE_MASK, 2) /* ilk-ivb */
#define   TRANSCONF_OUTPUT_COLORSPACE_YUV_HSW	REG_BIT(11) /* hsw only */
#define   TRANSCONF_BPC_MASK			REG_GENMASK(7, 5) /* ctg-ivb */
#define   TRANSCONF_BPC_8			REG_FIELD_PREP(TRANSCONF_BPC_MASK, 0)
#define   TRANSCONF_BPC_10			REG_FIELD_PREP(TRANSCONF_BPC_MASK, 1)
#define   TRANSCONF_BPC_6			REG_FIELD_PREP(TRANSCONF_BPC_MASK, 2)
#define   TRANSCONF_BPC_12			REG_FIELD_PREP(TRANSCONF_BPC_MASK, 3)
#define   TRANSCONF_DITHER_EN			REG_BIT(4)
#define   TRANSCONF_DITHER_TYPE_MASK		REG_GENMASK(3, 2)
#define   TRANSCONF_DITHER_TYPE_SP		REG_FIELD_PREP(TRANSCONF_DITHER_TYPE_MASK, 0)
#define   TRANSCONF_DITHER_TYPE_ST1		REG_FIELD_PREP(TRANSCONF_DITHER_TYPE_MASK, 1)
#define   TRANSCONF_DITHER_TYPE_ST2		REG_FIELD_PREP(TRANSCONF_DITHER_TYPE_MASK, 2)
#define   TRANSCONF_DITHER_TYPE_TEMP		REG_FIELD_PREP(TRANSCONF_DITHER_TYPE_MASK, 3)
#define   TRANSCONF_PIXEL_COUNT_SCALING_MASK	REG_GENMASK(1, 0)
#define   TRANSCONF_PIXEL_COUNT_SCALING_X4	1

#define _PIPEASTAT		0x70024
#define   PIPE_FIFO_UNDERRUN_STATUS		(1UL << 31)
#define   SPRITE1_FLIP_DONE_INT_EN_VLV		(1UL << 30)
#define   PIPE_CRC_ERROR_ENABLE			(1UL << 29)
#define   PIPE_CRC_DONE_ENABLE			(1UL << 28)
#define   PERF_COUNTER2_INTERRUPT_EN		(1UL << 27)
#define   PIPE_GMBUS_EVENT_ENABLE		(1UL << 27)
#define   PLANE_FLIP_DONE_INT_EN_VLV		(1UL << 26)
#define   PIPE_HOTPLUG_INTERRUPT_ENABLE		(1UL << 26)
#define   PIPE_VSYNC_INTERRUPT_ENABLE		(1UL << 25)
#define   PIPE_DISPLAY_LINE_COMPARE_ENABLE	(1UL << 24)
#define   PIPE_DPST_EVENT_ENABLE		(1UL << 23)
#define   SPRITE0_FLIP_DONE_INT_EN_VLV		(1UL << 22)
#define   PIPE_LEGACY_BLC_EVENT_ENABLE		(1UL << 22)
#define   PIPE_ODD_FIELD_INTERRUPT_ENABLE	(1UL << 21)
#define   PIPE_EVEN_FIELD_INTERRUPT_ENABLE	(1UL << 20)
#define   PIPE_B_PSR_INTERRUPT_ENABLE_VLV	(1UL << 19)
#define   PERF_COUNTER_INTERRUPT_EN		(1UL << 19)
#define   PIPE_HOTPLUG_TV_INTERRUPT_ENABLE	(1UL << 18) /* pre-965 */
#define   PIPE_START_VBLANK_INTERRUPT_ENABLE	(1UL << 18) /* 965 or later */
#define   PIPE_FRAMESTART_INTERRUPT_ENABLE	(1UL << 17)
#define   PIPE_VBLANK_INTERRUPT_ENABLE		(1UL << 17)
#define   PIPEA_HBLANK_INT_EN_VLV		(1UL << 16)
#define   PIPE_OVERLAY_UPDATED_ENABLE		(1UL << 16)
#define   SPRITE1_FLIP_DONE_INT_STATUS_VLV	(1UL << 15)
#define   SPRITE0_FLIP_DONE_INT_STATUS_VLV	(1UL << 14)
#define   PIPE_CRC_ERROR_INTERRUPT_STATUS	(1UL << 13)
#define   PIPE_CRC_DONE_INTERRUPT_STATUS	(1UL << 12)
#define   PERF_COUNTER2_INTERRUPT_STATUS	(1UL << 11)
#define   PIPE_GMBUS_INTERRUPT_STATUS		(1UL << 11)
#define   PLANE_FLIP_DONE_INT_STATUS_VLV	(1UL << 10)
#define   PIPE_HOTPLUG_INTERRUPT_STATUS		(1UL << 10)
#define   PIPE_VSYNC_INTERRUPT_STATUS		(1UL << 9)
#define   PIPE_DISPLAY_LINE_COMPARE_STATUS	(1UL << 8)
#define   PIPE_DPST_EVENT_STATUS		(1UL << 7)
#define   PIPE_A_PSR_STATUS_VLV			(1UL << 6)
#define   PIPE_LEGACY_BLC_EVENT_STATUS		(1UL << 6)
#define   PIPE_ODD_FIELD_INTERRUPT_STATUS	(1UL << 5)
#define   PIPE_EVEN_FIELD_INTERRUPT_STATUS	(1UL << 4)
#define   PIPE_B_PSR_STATUS_VLV			(1UL << 3)
#define   PERF_COUNTER_INTERRUPT_STATUS		(1UL << 3)
#define   PIPE_HOTPLUG_TV_INTERRUPT_STATUS	(1UL << 2) /* pre-965 */
#define   PIPE_START_VBLANK_INTERRUPT_STATUS	(1UL << 2) /* 965 or later */
#define   PIPE_FRAMESTART_INTERRUPT_STATUS	(1UL << 1)
#define   PIPE_VBLANK_INTERRUPT_STATUS		(1UL << 1)
#define   PIPE_HBLANK_INT_STATUS		(1UL << 0)
#define   PIPE_OVERLAY_UPDATED_STATUS		(1UL << 0)

#define PIPESTAT_INT_ENABLE_MASK		0x7fff0000
#define PIPESTAT_INT_STATUS_MASK		0x0000ffff

#define TRANSCONF(trans)	_MMIO_PIPE2(dev_priv, (trans), _TRANSACONF)
#define PIPEDSL(pipe)		_MMIO_PIPE2(dev_priv, pipe, _PIPEADSL)
#define PIPEFRAME(pipe)		_MMIO_PIPE2(dev_priv, pipe, _PIPEAFRAMEHIGH)
#define PIPEFRAMEPIXEL(pipe)	_MMIO_PIPE2(dev_priv, pipe, _PIPEAFRAMEPIXEL)
#define PIPESTAT(pipe)		_MMIO_PIPE2(dev_priv, pipe, _PIPEASTAT)

#define _PIPE_ARB_CTL_A			0x70028 /* icl+ */
#define PIPE_ARB_CTL(pipe)		_MMIO_PIPE2(dev_priv, pipe, _PIPE_ARB_CTL_A)
#define   PIPE_ARB_USE_PROG_SLOTS	REG_BIT(13)

#define _PIPE_MISC_A			0x70030
#define _PIPE_MISC_B			0x71030
#define   PIPE_MISC_YUV420_ENABLE		REG_BIT(27) /* glk+ */
#define   PIPE_MISC_YUV420_MODE_FULL_BLEND	REG_BIT(26) /* glk+ */
#define   PIPE_MISC_HDR_MODE_PRECISION		REG_BIT(23) /* icl+ */
#define   PIPE_MISC_PSR_MASK_PRIMARY_FLIP	REG_BIT(23) /* bdw */
#define   PIPE_MISC_PSR_MASK_SPRITE_ENABLE	REG_BIT(22) /* bdw */
#define   PIPE_MISC_PSR_MASK_PIPE_REG_WRITE	REG_BIT(21) /* skl+ */
#define   PIPE_MISC_PSR_MASK_CURSOR_MOVE	REG_BIT(21) /* bdw */
#define   PIPE_MISC_PSR_MASK_VBLANK_VSYNC_INT	REG_BIT(20)
#define   PIPE_MISC_OUTPUT_COLORSPACE_YUV	REG_BIT(11)
#define   PIPE_MISC_PIXEL_ROUNDING_TRUNC	REG_BIT(8) /* tgl+ */
/*
 * For Display < 13, Bits 5-7 of PIPE MISC represent DITHER BPC with
 * valid values of: 6, 8, 10 BPC.
 * ADLP+, the bits 5-7 represent PORT OUTPUT BPC with valid values of:
 * 6, 8, 10, 12 BPC.
 */
#define   PIPE_MISC_BPC_MASK			REG_GENMASK(7, 5)
#define   PIPE_MISC_BPC_8			REG_FIELD_PREP(PIPE_MISC_BPC_MASK, 0)
#define   PIPE_MISC_BPC_10			REG_FIELD_PREP(PIPE_MISC_BPC_MASK, 1)
#define   PIPE_MISC_BPC_6			REG_FIELD_PREP(PIPE_MISC_BPC_MASK, 2)
#define   PIPE_MISC_BPC_12_ADLP			REG_FIELD_PREP(PIPE_MISC_BPC_MASK, 4) /* adlp+ */
#define   PIPE_MISC_DITHER_ENABLE		REG_BIT(4)
#define   PIPE_MISC_DITHER_TYPE_MASK		REG_GENMASK(3, 2)
#define   PIPE_MISC_DITHER_TYPE_SP		REG_FIELD_PREP(PIPE_MISC_DITHER_TYPE_MASK, 0)
#define   PIPE_MISC_DITHER_TYPE_ST1		REG_FIELD_PREP(PIPE_MISC_DITHER_TYPE_MASK, 1)
#define   PIPE_MISC_DITHER_TYPE_ST2		REG_FIELD_PREP(PIPE_MISC_DITHER_TYPE_MASK, 2)
#define   PIPE_MISC_DITHER_TYPE_TEMP		REG_FIELD_PREP(PIPE_MISC_DITHER_TYPE_MASK, 3)
#define PIPE_MISC(pipe)			_MMIO_PIPE(pipe, _PIPE_MISC_A, _PIPE_MISC_B)

#define _PIPE_MISC2_A					0x7002C
#define _PIPE_MISC2_B					0x7102C
#define   PIPE_MISC2_BUBBLE_COUNTER_MASK	REG_GENMASK(31, 24)
#define   PIPE_MISC2_BUBBLE_COUNTER_SCALER_EN	REG_FIELD_PREP(PIPE_MISC2_BUBBLE_COUNTER_MASK, 80)
#define   PIPE_MISC2_BUBBLE_COUNTER_SCALER_DIS	REG_FIELD_PREP(PIPE_MISC2_BUBBLE_COUNTER_MASK, 20)
#define   PIPE_MISC2_FLIP_INFO_PLANE_SEL_MASK		REG_GENMASK(2, 0) /* tgl+ */
#define   PIPE_MISC2_FLIP_INFO_PLANE_SEL(plane_id)	REG_FIELD_PREP(PIPE_MISC2_FLIP_INFO_PLANE_SEL_MASK, (plane_id))
#define PIPE_MISC2(pipe)		_MMIO_PIPE(pipe, _PIPE_MISC2_A, _PIPE_MISC2_B)

#define _ICL_PIPE_A_STATUS			0x70058
#define ICL_PIPESTATUS(pipe)			_MMIO_PIPE2(dev_priv, pipe, _ICL_PIPE_A_STATUS)
#define   PIPE_STATUS_UNDERRUN				REG_BIT(31)
#define   PIPE_STATUS_SOFT_UNDERRUN_XELPD		REG_BIT(28)
#define   PIPE_STATUS_HARD_UNDERRUN_XELPD		REG_BIT(27)
#define   PIPE_STATUS_PORT_UNDERRUN_XELPD		REG_BIT(26)

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

#define DPINVGTT				_MMIO(VLV_DISPLAY_BASE + 0x7002c) /* VLV/CHV only */
#define   DPINVGTT_EN_MASK_CHV				REG_GENMASK(27, 16)
#define   DPINVGTT_EN_MASK_VLV				REG_GENMASK(23, 16)
#define   SPRITEF_INVALID_GTT_INT_EN			REG_BIT(27)
#define   SPRITEE_INVALID_GTT_INT_EN			REG_BIT(26)
#define   PLANEC_INVALID_GTT_INT_EN			REG_BIT(25)
#define   CURSORC_INVALID_GTT_INT_EN			REG_BIT(24)
#define   CURSORB_INVALID_GTT_INT_EN			REG_BIT(23)
#define   CURSORA_INVALID_GTT_INT_EN			REG_BIT(22)
#define   SPRITED_INVALID_GTT_INT_EN			REG_BIT(21)
#define   SPRITEC_INVALID_GTT_INT_EN			REG_BIT(20)
#define   PLANEB_INVALID_GTT_INT_EN			REG_BIT(19)
#define   SPRITEB_INVALID_GTT_INT_EN			REG_BIT(18)
#define   SPRITEA_INVALID_GTT_INT_EN			REG_BIT(17)
#define   PLANEA_INVALID_GTT_INT_EN			REG_BIT(16)
#define   DPINVGTT_STATUS_MASK_CHV			REG_GENMASK(11, 0)
#define   DPINVGTT_STATUS_MASK_VLV			REG_GENMASK(7, 0)
#define   SPRITEF_INVALID_GTT_STATUS			REG_BIT(11)
#define   SPRITEE_INVALID_GTT_STATUS			REG_BIT(10)
#define   PLANEC_INVALID_GTT_STATUS			REG_BIT(9)
#define   CURSORC_INVALID_GTT_STATUS			REG_BIT(8)
#define   CURSORB_INVALID_GTT_STATUS			REG_BIT(7)
#define   CURSORA_INVALID_GTT_STATUS			REG_BIT(6)
#define   SPRITED_INVALID_GTT_STATUS			REG_BIT(5)
#define   SPRITEC_INVALID_GTT_STATUS			REG_BIT(4)
#define   PLANEB_INVALID_GTT_STATUS			REG_BIT(3)
#define   SPRITEB_INVALID_GTT_STATUS			REG_BIT(2)
#define   SPRITEA_INVALID_GTT_STATUS			REG_BIT(1)
#define   PLANEA_INVALID_GTT_STATUS			REG_BIT(0)

#define DSPARB			_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x70030)
#define   DSPARB_CSTART_MASK	(0x7f << 7)
#define   DSPARB_CSTART_SHIFT	7
#define   DSPARB_BSTART_MASK	(0x7f)
#define   DSPARB_BSTART_SHIFT	0
#define   DSPARB_BEND_SHIFT	9 /* on 855 */
#define   DSPARB_AEND_SHIFT	0
#define   DSPARB_SPRITEA_SHIFT_VLV	0
#define   DSPARB_SPRITEA_MASK_VLV	(0xff << 0)
#define   DSPARB_SPRITEB_SHIFT_VLV	8
#define   DSPARB_SPRITEB_MASK_VLV	(0xff << 8)
#define   DSPARB_SPRITEC_SHIFT_VLV	16
#define   DSPARB_SPRITEC_MASK_VLV	(0xff << 16)
#define   DSPARB_SPRITED_SHIFT_VLV	24
#define   DSPARB_SPRITED_MASK_VLV	(0xff << 24)
#define DSPARB2				_MMIO(VLV_DISPLAY_BASE + 0x70060) /* vlv/chv */
#define   DSPARB_SPRITEA_HI_SHIFT_VLV	0
#define   DSPARB_SPRITEA_HI_MASK_VLV	(0x1 << 0)
#define   DSPARB_SPRITEB_HI_SHIFT_VLV	4
#define   DSPARB_SPRITEB_HI_MASK_VLV	(0x1 << 4)
#define   DSPARB_SPRITEC_HI_SHIFT_VLV	8
#define   DSPARB_SPRITEC_HI_MASK_VLV	(0x1 << 8)
#define   DSPARB_SPRITED_HI_SHIFT_VLV	12
#define   DSPARB_SPRITED_HI_MASK_VLV	(0x1 << 12)
#define   DSPARB_SPRITEE_HI_SHIFT_VLV	16
#define   DSPARB_SPRITEE_HI_MASK_VLV	(0x1 << 16)
#define   DSPARB_SPRITEF_HI_SHIFT_VLV	20
#define   DSPARB_SPRITEF_HI_MASK_VLV	(0x1 << 20)
#define DSPARB3				_MMIO(VLV_DISPLAY_BASE + 0x7006c) /* chv */
#define   DSPARB_SPRITEE_SHIFT_VLV	0
#define   DSPARB_SPRITEE_MASK_VLV	(0xff << 0)
#define   DSPARB_SPRITEF_SHIFT_VLV	8
#define   DSPARB_SPRITEF_MASK_VLV	(0xff << 8)

/* pnv/gen4/g4x/vlv/chv */
#define DSPFW1		_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x70034)
#define   DSPFW_SR_SHIFT		23
#define   DSPFW_SR_MASK			(0x1ff << 23)
#define   DSPFW_CURSORB_SHIFT		16
#define   DSPFW_CURSORB_MASK		(0x3f << 16)
#define   DSPFW_PLANEB_SHIFT		8
#define   DSPFW_PLANEB_MASK		(0x7f << 8)
#define   DSPFW_PLANEB_MASK_VLV		(0xff << 8) /* vlv/chv */
#define   DSPFW_PLANEA_SHIFT		0
#define   DSPFW_PLANEA_MASK		(0x7f << 0)
#define   DSPFW_PLANEA_MASK_VLV		(0xff << 0) /* vlv/chv */
#define DSPFW2		_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x70038)
#define   DSPFW_FBC_SR_EN		(1 << 31)	  /* g4x */
#define   DSPFW_FBC_SR_SHIFT		28
#define   DSPFW_FBC_SR_MASK		(0x7 << 28) /* g4x */
#define   DSPFW_FBC_HPLL_SR_SHIFT	24
#define   DSPFW_FBC_HPLL_SR_MASK	(0xf << 24) /* g4x */
#define   DSPFW_SPRITEB_SHIFT		(16)
#define   DSPFW_SPRITEB_MASK		(0x7f << 16) /* g4x */
#define   DSPFW_SPRITEB_MASK_VLV	(0xff << 16) /* vlv/chv */
#define   DSPFW_CURSORA_SHIFT		8
#define   DSPFW_CURSORA_MASK		(0x3f << 8)
#define   DSPFW_PLANEC_OLD_SHIFT	0
#define   DSPFW_PLANEC_OLD_MASK		(0x7f << 0) /* pre-gen4 sprite C */
#define   DSPFW_SPRITEA_SHIFT		0
#define   DSPFW_SPRITEA_MASK		(0x7f << 0) /* g4x */
#define   DSPFW_SPRITEA_MASK_VLV	(0xff << 0) /* vlv/chv */
#define DSPFW3		_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x7003c)
#define   DSPFW_HPLL_SR_EN		(1 << 31)
#define   PINEVIEW_SELF_REFRESH_EN	(1 << 30)
#define   DSPFW_CURSOR_SR_SHIFT		24
#define   DSPFW_CURSOR_SR_MASK		(0x3f << 24)
#define   DSPFW_HPLL_CURSOR_SHIFT	16
#define   DSPFW_HPLL_CURSOR_MASK	(0x3f << 16)
#define   DSPFW_HPLL_SR_SHIFT		0
#define   DSPFW_HPLL_SR_MASK		(0x1ff << 0)

/* vlv/chv */
#define DSPFW4		_MMIO(VLV_DISPLAY_BASE + 0x70070)
#define   DSPFW_SPRITEB_WM1_SHIFT	16
#define   DSPFW_SPRITEB_WM1_MASK	(0xff << 16)
#define   DSPFW_CURSORA_WM1_SHIFT	8
#define   DSPFW_CURSORA_WM1_MASK	(0x3f << 8)
#define   DSPFW_SPRITEA_WM1_SHIFT	0
#define   DSPFW_SPRITEA_WM1_MASK	(0xff << 0)
#define DSPFW5		_MMIO(VLV_DISPLAY_BASE + 0x70074)
#define   DSPFW_PLANEB_WM1_SHIFT	24
#define   DSPFW_PLANEB_WM1_MASK		(0xff << 24)
#define   DSPFW_PLANEA_WM1_SHIFT	16
#define   DSPFW_PLANEA_WM1_MASK		(0xff << 16)
#define   DSPFW_CURSORB_WM1_SHIFT	8
#define   DSPFW_CURSORB_WM1_MASK	(0x3f << 8)
#define   DSPFW_CURSOR_SR_WM1_SHIFT	0
#define   DSPFW_CURSOR_SR_WM1_MASK	(0x3f << 0)
#define DSPFW6		_MMIO(VLV_DISPLAY_BASE + 0x70078)
#define   DSPFW_SR_WM1_SHIFT		0
#define   DSPFW_SR_WM1_MASK		(0x1ff << 0)
#define DSPFW7		_MMIO(VLV_DISPLAY_BASE + 0x7007c)
#define DSPFW7_CHV	_MMIO(VLV_DISPLAY_BASE + 0x700b4) /* wtf #1? */
#define   DSPFW_SPRITED_WM1_SHIFT	24
#define   DSPFW_SPRITED_WM1_MASK	(0xff << 24)
#define   DSPFW_SPRITED_SHIFT		16
#define   DSPFW_SPRITED_MASK_VLV	(0xff << 16)
#define   DSPFW_SPRITEC_WM1_SHIFT	8
#define   DSPFW_SPRITEC_WM1_MASK	(0xff << 8)
#define   DSPFW_SPRITEC_SHIFT		0
#define   DSPFW_SPRITEC_MASK_VLV	(0xff << 0)
#define DSPFW8_CHV	_MMIO(VLV_DISPLAY_BASE + 0x700b8)
#define   DSPFW_SPRITEF_WM1_SHIFT	24
#define   DSPFW_SPRITEF_WM1_MASK	(0xff << 24)
#define   DSPFW_SPRITEF_SHIFT		16
#define   DSPFW_SPRITEF_MASK_VLV	(0xff << 16)
#define   DSPFW_SPRITEE_WM1_SHIFT	8
#define   DSPFW_SPRITEE_WM1_MASK	(0xff << 8)
#define   DSPFW_SPRITEE_SHIFT		0
#define   DSPFW_SPRITEE_MASK_VLV	(0xff << 0)
#define DSPFW9_CHV	_MMIO(VLV_DISPLAY_BASE + 0x7007c) /* wtf #2? */
#define   DSPFW_PLANEC_WM1_SHIFT	24
#define   DSPFW_PLANEC_WM1_MASK		(0xff << 24)
#define   DSPFW_PLANEC_SHIFT		16
#define   DSPFW_PLANEC_MASK_VLV		(0xff << 16)
#define   DSPFW_CURSORC_WM1_SHIFT	8
#define   DSPFW_CURSORC_WM1_MASK	(0x3f << 16)
#define   DSPFW_CURSORC_SHIFT		0
#define   DSPFW_CURSORC_MASK		(0x3f << 0)

/* vlv/chv high order bits */
#define DSPHOWM		_MMIO(VLV_DISPLAY_BASE + 0x70064)
#define   DSPFW_SR_HI_SHIFT		24
#define   DSPFW_SR_HI_MASK		(3 << 24) /* 2 bits for chv, 1 for vlv */
#define   DSPFW_SPRITEF_HI_SHIFT	23
#define   DSPFW_SPRITEF_HI_MASK		(1 << 23)
#define   DSPFW_SPRITEE_HI_SHIFT	22
#define   DSPFW_SPRITEE_HI_MASK		(1 << 22)
#define   DSPFW_PLANEC_HI_SHIFT		21
#define   DSPFW_PLANEC_HI_MASK		(1 << 21)
#define   DSPFW_SPRITED_HI_SHIFT	20
#define   DSPFW_SPRITED_HI_MASK		(1 << 20)
#define   DSPFW_SPRITEC_HI_SHIFT	16
#define   DSPFW_SPRITEC_HI_MASK		(1 << 16)
#define   DSPFW_PLANEB_HI_SHIFT		12
#define   DSPFW_PLANEB_HI_MASK		(1 << 12)
#define   DSPFW_SPRITEB_HI_SHIFT	8
#define   DSPFW_SPRITEB_HI_MASK		(1 << 8)
#define   DSPFW_SPRITEA_HI_SHIFT	4
#define   DSPFW_SPRITEA_HI_MASK		(1 << 4)
#define   DSPFW_PLANEA_HI_SHIFT		0
#define   DSPFW_PLANEA_HI_MASK		(1 << 0)
#define DSPHOWM1	_MMIO(VLV_DISPLAY_BASE + 0x70068)
#define   DSPFW_SR_WM1_HI_SHIFT		24
#define   DSPFW_SR_WM1_HI_MASK		(3 << 24) /* 2 bits for chv, 1 for vlv */
#define   DSPFW_SPRITEF_WM1_HI_SHIFT	23
#define   DSPFW_SPRITEF_WM1_HI_MASK	(1 << 23)
#define   DSPFW_SPRITEE_WM1_HI_SHIFT	22
#define   DSPFW_SPRITEE_WM1_HI_MASK	(1 << 22)
#define   DSPFW_PLANEC_WM1_HI_SHIFT	21
#define   DSPFW_PLANEC_WM1_HI_MASK	(1 << 21)
#define   DSPFW_SPRITED_WM1_HI_SHIFT	20
#define   DSPFW_SPRITED_WM1_HI_MASK	(1 << 20)
#define   DSPFW_SPRITEC_WM1_HI_SHIFT	16
#define   DSPFW_SPRITEC_WM1_HI_MASK	(1 << 16)
#define   DSPFW_PLANEB_WM1_HI_SHIFT	12
#define   DSPFW_PLANEB_WM1_HI_MASK	(1 << 12)
#define   DSPFW_SPRITEB_WM1_HI_SHIFT	8
#define   DSPFW_SPRITEB_WM1_HI_MASK	(1 << 8)
#define   DSPFW_SPRITEA_WM1_HI_SHIFT	4
#define   DSPFW_SPRITEA_WM1_HI_MASK	(1 << 4)
#define   DSPFW_PLANEA_WM1_HI_SHIFT	0
#define   DSPFW_PLANEA_WM1_HI_MASK	(1 << 0)

/* drain latency register values*/
#define VLV_DDL(pipe)			_MMIO(VLV_DISPLAY_BASE + 0x70050 + 4 * (pipe))
#define DDL_CURSOR_SHIFT		24
#define DDL_SPRITE_SHIFT(sprite)	(8 + 8 * (sprite))
#define DDL_PLANE_SHIFT			0
#define DDL_PRECISION_HIGH		(1 << 7)
#define DDL_PRECISION_LOW		(0 << 7)
#define DRAIN_LATENCY_MASK		0x7f

#define CBR1_VLV			_MMIO(VLV_DISPLAY_BASE + 0x70400)
#define  CBR_PND_DEADLINE_DISABLE	(1 << 31)
#define  CBR_PWM_CLOCK_MUX_SELECT	(1 << 30)

#define CBR4_VLV			_MMIO(VLV_DISPLAY_BASE + 0x70450)
#define  CBR_DPLLBMD_PIPE(pipe)		(1 << (7 + (pipe) * 11)) /* pipes B and C */

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
#define _WM0_PIPEA_ILK		0x45100
#define _WM0_PIPEB_ILK		0x45104
#define _WM0_PIPEC_IVB		0x45200
#define WM0_PIPE_ILK(pipe)	_MMIO_BASE_PIPE3(0, (pipe), _WM0_PIPEA_ILK, \
						 _WM0_PIPEB_ILK, _WM0_PIPEC_IVB)
#define  WM0_PIPE_PRIMARY_MASK	REG_GENMASK(31, 16)
#define  WM0_PIPE_SPRITE_MASK	REG_GENMASK(15, 8)
#define  WM0_PIPE_CURSOR_MASK	REG_GENMASK(7, 0)
#define  WM0_PIPE_PRIMARY(x)	REG_FIELD_PREP(WM0_PIPE_PRIMARY_MASK, (x))
#define  WM0_PIPE_SPRITE(x)	REG_FIELD_PREP(WM0_PIPE_SPRITE_MASK, (x))
#define  WM0_PIPE_CURSOR(x)	REG_FIELD_PREP(WM0_PIPE_CURSOR_MASK, (x))
#define WM1_LP_ILK		_MMIO(0x45108)
#define WM2_LP_ILK		_MMIO(0x4510c)
#define WM3_LP_ILK		_MMIO(0x45110)
#define  WM_LP_ENABLE		REG_BIT(31)
#define  WM_LP_LATENCY_MASK	REG_GENMASK(30, 24)
#define  WM_LP_FBC_MASK_BDW	REG_GENMASK(23, 19)
#define  WM_LP_FBC_MASK_ILK	REG_GENMASK(23, 20)
#define  WM_LP_PRIMARY_MASK	REG_GENMASK(18, 8)
#define  WM_LP_CURSOR_MASK	REG_GENMASK(7, 0)
#define  WM_LP_LATENCY(x)	REG_FIELD_PREP(WM_LP_LATENCY_MASK, (x))
#define  WM_LP_FBC_BDW(x)	REG_FIELD_PREP(WM_LP_FBC_MASK_BDW, (x))
#define  WM_LP_FBC_ILK(x)	REG_FIELD_PREP(WM_LP_FBC_MASK_ILK, (x))
#define  WM_LP_PRIMARY(x)	REG_FIELD_PREP(WM_LP_PRIMARY_MASK, (x))
#define  WM_LP_CURSOR(x)	REG_FIELD_PREP(WM_LP_CURSOR_MASK, (x))
#define WM1S_LP_ILK		_MMIO(0x45120)
#define WM2S_LP_IVB		_MMIO(0x45124)
#define WM3S_LP_IVB		_MMIO(0x45128)
#define  WM_LP_SPRITE_ENABLE	REG_BIT(31) /* ilk/snb WM1S only */
#define  WM_LP_SPRITE_MASK	REG_GENMASK(10, 0)
#define  WM_LP_SPRITE(x)	REG_FIELD_PREP(WM_LP_SPRITE_MASK, (x))

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
#define _PIPEA_FRMCOUNT_G4X	0x70040
#define _PIPEA_FLIPCOUNT_G4X	0x70044
#define PIPE_FRMCOUNT_G4X(pipe) _MMIO_PIPE2(dev_priv, pipe, _PIPEA_FRMCOUNT_G4X)
#define PIPE_FLIPCOUNT_G4X(pipe) _MMIO_PIPE2(dev_priv, pipe, _PIPEA_FLIPCOUNT_G4X)

/* CHV pipe B blender */
#define _CHV_BLEND_A		0x60a00
#define   CHV_BLEND_MASK	REG_GENMASK(31, 30)
#define   CHV_BLEND_LEGACY	REG_FIELD_PREP(CHV_BLEND_MASK, 0)
#define   CHV_BLEND_ANDROID	REG_FIELD_PREP(CHV_BLEND_MASK, 1)
#define   CHV_BLEND_MPO		REG_FIELD_PREP(CHV_BLEND_MASK, 2)
#define _CHV_CANVAS_A		0x60a04
#define   CHV_CANVAS_RED_MASK	REG_GENMASK(29, 20)
#define   CHV_CANVAS_GREEN_MASK	REG_GENMASK(19, 10)
#define   CHV_CANVAS_BLUE_MASK	REG_GENMASK(9, 0)

#define CHV_BLEND(pipe)		_MMIO_TRANS2(dev_priv, pipe, _CHV_BLEND_A)
#define CHV_CANVAS(pipe)	_MMIO_TRANS2(dev_priv, pipe, _CHV_CANVAS_A)

/* Display/Sprite base address macros */
#define DISP_BASEADDR_MASK	(0xfffff000)
#define I915_LO_DISPBASE(val)	((val) & ~DISP_BASEADDR_MASK)
#define I915_HI_DISPBASE(val)	((val) & DISP_BASEADDR_MASK)

/*
 * VBIOS flags
 * gen2:
 * [00:06] alm,mgm
 * [10:16] all
 * [30:32] alm,mgm
 * gen3+:
 * [00:0f] all
 * [10:1f] all
 * [30:32] all
 */
#define SWF0(i)	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x70410 + (i) * 4)
#define SWF1(i)	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x71410 + (i) * 4)
#define SWF3(i)	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x72414 + (i) * 4)
#define SWF_ILK(i)	_MMIO(0x4F000 + (i) * 4)

/* Pipe B */
#define _PIPEBDSL		(DISPLAY_MMIO_BASE(dev_priv) + 0x71000)
#define _TRANSBCONF		(DISPLAY_MMIO_BASE(dev_priv) + 0x71008)
#define _PIPEBSTAT		(DISPLAY_MMIO_BASE(dev_priv) + 0x71024)
#define _PIPEBFRAMEHIGH		0x71040
#define _PIPEBFRAMEPIXEL	0x71044
#define _PIPEB_FRMCOUNT_G4X	(DISPLAY_MMIO_BASE(dev_priv) + 0x71040)
#define _PIPEB_FLIPCOUNT_G4X	(DISPLAY_MMIO_BASE(dev_priv) + 0x71044)


/* Display B control */
#define _DSPBCNTR		(DISPLAY_MMIO_BASE(dev_priv) + 0x71180)
#define   DISP_ALPHA_TRANS_ENABLE	REG_BIT(15)
#define   DISP_SPRITE_ABOVE_OVERLAY	REG_BIT(0)
#define _DSPBADDR		(DISPLAY_MMIO_BASE(dev_priv) + 0x71184)
#define _DSPBSTRIDE		(DISPLAY_MMIO_BASE(dev_priv) + 0x71188)
#define _DSPBPOS		(DISPLAY_MMIO_BASE(dev_priv) + 0x7118C)
#define _DSPBSIZE		(DISPLAY_MMIO_BASE(dev_priv) + 0x71190)
#define _DSPBSURF		(DISPLAY_MMIO_BASE(dev_priv) + 0x7119C)
#define _DSPBTILEOFF		(DISPLAY_MMIO_BASE(dev_priv) + 0x711A4)
#define _DSPBOFFSET		(DISPLAY_MMIO_BASE(dev_priv) + 0x711A4)
#define _DSPBSURFLIVE		(DISPLAY_MMIO_BASE(dev_priv) + 0x711AC)

/* ICL DSI 0 and 1 */
#define _PIPEDSI0CONF		0x7b008
#define _PIPEDSI1CONF		0x7b808


/* VBIOS regs */
#define VGACNTRL		_MMIO(0x71400)
# define VGA_DISP_DISABLE			(1 << 31)
# define VGA_2X_MODE				(1 << 30)
# define VGA_PIPE_B_SELECT			(1 << 29)

#define VLV_VGACNTRL		_MMIO(VLV_DISPLAY_BASE + 0x71400)

/* Ironlake */

#define CPU_VGACNTRL	_MMIO(0x41000)

#define DIGITAL_PORT_HOTPLUG_CNTRL	_MMIO(0x44030)
#define  DIGITAL_PORTA_HOTPLUG_ENABLE		(1 << 4)
#define  DIGITAL_PORTA_PULSE_DURATION_2ms	(0 << 2) /* pre-HSW */
#define  DIGITAL_PORTA_PULSE_DURATION_4_5ms	(1 << 2) /* pre-HSW */
#define  DIGITAL_PORTA_PULSE_DURATION_6ms	(2 << 2) /* pre-HSW */
#define  DIGITAL_PORTA_PULSE_DURATION_100ms	(3 << 2) /* pre-HSW */
#define  DIGITAL_PORTA_PULSE_DURATION_MASK	(3 << 2) /* pre-HSW */
#define  DIGITAL_PORTA_HOTPLUG_STATUS_MASK	(3 << 0)
#define  DIGITAL_PORTA_HOTPLUG_NO_DETECT	(0 << 0)
#define  DIGITAL_PORTA_HOTPLUG_SHORT_DETECT	(1 << 0)
#define  DIGITAL_PORTA_HOTPLUG_LONG_DETECT	(2 << 0)

/* refresh rate hardware control */
#define RR_HW_CTL       _MMIO(0x45300)
#define  RR_HW_LOW_POWER_FRAMES_MASK    0xff
#define  RR_HW_HIGH_POWER_FRAMES_MASK   0xff00

#define PCH_3DCGDIS0		_MMIO(0x46020)
# define MARIUNIT_CLOCK_GATE_DISABLE		(1 << 18)
# define SVSMUNIT_CLOCK_GATE_DISABLE		(1 << 1)

#define PCH_3DCGDIS1		_MMIO(0x46024)
# define VFMUNIT_CLOCK_GATE_DISABLE		(1 << 11)

#define _PIPEA_DATA_M1		0x60030
#define _PIPEA_DATA_N1		0x60034
#define _PIPEA_DATA_M2		0x60038
#define _PIPEA_DATA_N2		0x6003c
#define _PIPEA_LINK_M1		0x60040
#define _PIPEA_LINK_N1		0x60044
#define _PIPEA_LINK_M2		0x60048
#define _PIPEA_LINK_N2		0x6004c

/* PIPEB timing regs are same start from 0x61000 */

#define _PIPEB_DATA_M1		0x61030
#define _PIPEB_DATA_N1		0x61034
#define _PIPEB_DATA_M2		0x61038
#define _PIPEB_DATA_N2		0x6103c
#define _PIPEB_LINK_M1		0x61040
#define _PIPEB_LINK_N1		0x61044
#define _PIPEB_LINK_M2		0x61048
#define _PIPEB_LINK_N2		0x6104c

#define PIPE_DATA_M1(tran) _MMIO_TRANS2(dev_priv, tran, _PIPEA_DATA_M1)
#define PIPE_DATA_N1(tran) _MMIO_TRANS2(dev_priv, tran, _PIPEA_DATA_N1)
#define PIPE_DATA_M2(tran) _MMIO_TRANS2(dev_priv, tran, _PIPEA_DATA_M2)
#define PIPE_DATA_N2(tran) _MMIO_TRANS2(dev_priv, tran, _PIPEA_DATA_N2)
#define PIPE_LINK_M1(tran) _MMIO_TRANS2(dev_priv, tran, _PIPEA_LINK_M1)
#define PIPE_LINK_N1(tran) _MMIO_TRANS2(dev_priv, tran, _PIPEA_LINK_N1)
#define PIPE_LINK_M2(tran) _MMIO_TRANS2(dev_priv, tran, _PIPEA_LINK_M2)
#define PIPE_LINK_N2(tran) _MMIO_TRANS2(dev_priv, tran, _PIPEA_LINK_N2)

/* CPU panel fitter */
/* IVB+ has 3 fitters, 0 is 7x5 capable, the other two only 3x3 */
#define _PFA_CTL_1		0x68080
#define _PFB_CTL_1		0x68880
#define   PF_ENABLE			REG_BIT(31)
#define   PF_PIPE_SEL_MASK_IVB		REG_GENMASK(30, 29) /* ivb/hsw */
#define   PF_PIPE_SEL_IVB(pipe)		REG_FIELD_PREP(PF_PIPE_SEL_MASK_IVB, (pipe))
#define   PF_FILTER_MASK		REG_GENMASK(24, 23)
#define   PF_FILTER_PROGRAMMED		REG_FIELD_PREP(PF_FILTER_MASK, 0)
#define   PF_FILTER_MED_3x3		REG_FIELD_PREP(PF_FILTER_MASK, 1)
#define   PF_FILTER_EDGE_ENHANCE	REG_FIELD_PREP(PF_FILTER_EDGE_MASK, 2)
#define   PF_FILTER_EDGE_SOFTEN		REG_FIELD_PREP(PF_FILTER_EDGE_MASK, 3)
#define _PFA_WIN_SZ		0x68074
#define _PFB_WIN_SZ		0x68874
#define   PF_WIN_XSIZE_MASK	REG_GENMASK(31, 16)
#define   PF_WIN_XSIZE(w)	REG_FIELD_PREP(PF_WIN_XSIZE_MASK, (w))
#define   PF_WIN_YSIZE_MASK	REG_GENMASK(15, 0)
#define   PF_WIN_YSIZE(h)	REG_FIELD_PREP(PF_WIN_YSIZE_MASK, (h))
#define _PFA_WIN_POS		0x68070
#define _PFB_WIN_POS		0x68870
#define   PF_WIN_XPOS_MASK	REG_GENMASK(31, 16)
#define   PF_WIN_XPOS(x)	REG_FIELD_PREP(PF_WIN_XPOS_MASK, (x))
#define   PF_WIN_YPOS_MASK	REG_GENMASK(15, 0)
#define   PF_WIN_YPOS(y)	REG_FIELD_PREP(PF_WIN_YPOS_MASK, (y))
#define _PFA_VSCALE		0x68084
#define _PFB_VSCALE		0x68884
#define _PFA_HSCALE		0x68090
#define _PFB_HSCALE		0x68890

#define PF_CTL(pipe)		_MMIO_PIPE(pipe, _PFA_CTL_1, _PFB_CTL_1)
#define PF_WIN_SZ(pipe)		_MMIO_PIPE(pipe, _PFA_WIN_SZ, _PFB_WIN_SZ)
#define PF_WIN_POS(pipe)	_MMIO_PIPE(pipe, _PFA_WIN_POS, _PFB_WIN_POS)
#define PF_VSCALE(pipe)		_MMIO_PIPE(pipe, _PFA_VSCALE, _PFB_VSCALE)
#define PF_HSCALE(pipe)		_MMIO_PIPE(pipe, _PFA_HSCALE, _PFB_HSCALE)

/*
 * Skylake scalers
 */
#define _PS_1A_CTRL      0x68180
#define _PS_2A_CTRL      0x68280
#define _PS_1B_CTRL      0x68980
#define _PS_2B_CTRL      0x68A80
#define _PS_1C_CTRL      0x69180
#define   PS_SCALER_EN				REG_BIT(31)
#define   PS_SCALER_TYPE_MASK			REG_BIT(30) /* icl+ */
#define   PS_SCALER_TYPE_NON_LINEAR		REG_FIELD_PREP(PS_SCALER_TYPE_MASK, 0)
#define   PS_SCALER_TYPE_LINEAR			REG_FIELD_PREP(PS_SCALER_TYPE_MASK, 1)
#define   SKL_PS_SCALER_MODE_MASK		REG_GENMASK(29, 28) /* skl/bxt */
#define   SKL_PS_SCALER_MODE_DYN		REG_FIELD_PREP(SKL_PS_SCALER_MODE_MASK, 0)
#define   SKL_PS_SCALER_MODE_HQ			REG_FIELD_PREP(SKL_PS_SCALER_MODE_MASK, 1)
#define   SKL_PS_SCALER_MODE_NV12		REG_FIELD_PREP(SKL_PS_SCALER_MODE_MASK, 2)
#define   PS_SCALER_MODE_MASK			REG_BIT(29) /* glk-tgl */
#define   PS_SCALER_MODE_NORMAL			REG_FIELD_PREP(PS_SCALER_MODE_MASK, 0)
#define   PS_SCALER_MODE_PLANAR			REG_FIELD_PREP(PS_SCALER_MODE_MASK, 1)
#define   PS_ADAPTIVE_FILTERING_EN		REG_BIT(28) /* icl+ */
#define   PS_BINDING_MASK			REG_GENMASK(27, 25)
#define   PS_BINDING_PIPE			REG_FIELD_PREP(PS_BINDING_MASK, 0)
#define   PS_BINDING_PLANE(plane_id)		REG_FIELD_PREP(PS_BINDING_MASK, (plane_id) + 1)
#define   PS_FILTER_MASK			REG_GENMASK(24, 23)
#define   PS_FILTER_MEDIUM			REG_FIELD_PREP(PS_FILTER_MASK, 0)
#define   PS_FILTER_PROGRAMMED			REG_FIELD_PREP(PS_FILTER_MASK, 1)
#define   PS_FILTER_EDGE_ENHANCE		REG_FIELD_PREP(PS_FILTER_MASK, 2)
#define   PS_FILTER_BILINEAR			REG_FIELD_PREP(PS_FILTER_MASK, 3)
#define   PS_ADAPTIVE_FILTER_MASK		REG_BIT(22) /* icl+ */
#define   PS_ADAPTIVE_FILTER_MEDIUM		REG_FIELD_PREP(PS_ADAPTIVE_FILTER_MASK, 0)
#define   PS_ADAPTIVE_FILTER_EDGE_ENHANCE	REG_FIELD_PREP(PS_ADAPTIVE_FILTER_MASK, 1)
#define   PS_PIPE_SCALER_LOC_MASK		REG_BIT(21) /* icl+ */
#define   PS_PIPE_SCALER_LOC_AFTER_OUTPUT_CSC	REG_FIELD_PREP(PS_SCALER_LOCATION_MASK, 0) /* non-linear */
#define   PS_PIPE_SCALER_LOC_AFTER_CSC		REG_FIELD_PREP(PS_SCALER_LOCATION_MASK, 1) /* linear */
#define   PS_VERT3TAP				REG_BIT(21) /* skl/bxt */
#define   PS_VERT_INT_INVERT_FIELD		REG_BIT(20)
#define   PS_PROG_SCALE_FACTOR			REG_BIT(19) /* tgl+ */
#define   PS_PWRUP_PROGRESS			REG_BIT(17)
#define   PS_V_FILTER_BYPASS			REG_BIT(8)
#define   PS_VADAPT_EN				REG_BIT(7) /* skl/bxt */
#define   PS_VADAPT_MODE_MASK			REG_GENMASK(6, 5) /* skl/bxt */
#define   PS_VADAPT_MODE_LEAST_ADAPT		REG_FIELD_PREP(PS_VADAPT_MODE_MASK, 0)
#define   PS_VADAPT_MODE_MOD_ADAPT		REG_FIELD_PREP(PS_VADAPT_MODE_MASK, 1)
#define   PS_VADAPT_MODE_MOST_ADAPT		REG_FIELD_PREP(PS_VADAPT_MODE_MASK, 3)
#define   PS_BINDING_Y_MASK			REG_GENMASK(7, 5) /* icl-tgl */
#define   PS_BINDING_Y_PLANE(plane_id)		REG_FIELD_PREP(PS_BINDING_Y_MASK, (plane_id) + 1)
#define   PS_Y_VERT_FILTER_SELECT_MASK		REG_BIT(4) /* glk+ */
#define   PS_Y_VERT_FILTER_SELECT(set)		REG_FIELD_PREP(PS_Y_VERT_FILTER_SELECT_MASK, (set))
#define   PS_Y_HORZ_FILTER_SELECT_MASK		REG_BIT(3) /* glk+ */
#define   PS_Y_HORZ_FILTER_SELECT(set)		REG_FIELD_PREP(PS_Y_HORZ_FILTER_SELECT_MASK, (set))
#define   PS_UV_VERT_FILTER_SELECT_MASK		REG_BIT(2) /* glk+ */
#define   PS_UV_VERT_FILTER_SELECT(set)		REG_FIELD_PREP(PS_UV_VERT_FILTER_SELECT_MASK, (set))
#define   PS_UV_HORZ_FILTER_SELECT_MASK		REG_BIT(1) /* glk+ */
#define   PS_UV_HORZ_FILTER_SELECT(set)		REG_FIELD_PREP(PS_UV_HORZ_FILTER_SELECT_MASK, (set))

#define _PS_PWR_GATE_1A     0x68160
#define _PS_PWR_GATE_2A     0x68260
#define _PS_PWR_GATE_1B     0x68960
#define _PS_PWR_GATE_2B     0x68A60
#define _PS_PWR_GATE_1C     0x69160
#define   PS_PWR_GATE_DIS_OVERRIDE		REG_BIT(31)
#define   PS_PWR_GATE_SETTLING_TIME_MASK	REG_GENMASK(4, 3)
#define   PS_PWR_GATE_SETTLING_TIME_32		REG_FIELD_PREP(PS_PWR_GATE_SETTLING_TIME_MASK, 0)
#define   PS_PWR_GATE_SETTLING_TIME_64		REG_FIELD_PREP(PS_PWR_GATE_SETTLING_TIME_MASK, 1)
#define   PS_PWR_GATE_SETTLING_TIME_96		REG_FIELD_PREP(PS_PWR_GATE_SETTLING_TIME_MASK, 2)
#define   PS_PWR_GATE_SETTLING_TIME_128		REG_FIELD_PREP(PS_PWR_GATE_SETTLING_TIME_MASK, 3)
#define   PS_PWR_GATE_SLPEN_MASK		REG_GENMASK(1, 0)
#define   PS_PWR_GATE_SLPEN_8			REG_FIELD_PREP(PS_PWR_GATE_SLPEN_MASK, 0)
#define   PS_PWR_GATE_SLPEN_16			REG_FIELD_PREP(PS_PWR_GATE_SLPEN_MASK, 1)
#define   PS_PWR_GATE_SLPEN_24			REG_FIELD_PREP(PS_PWR_GATE_SLPEN_MASK, 2)
#define   PS_PWR_GATE_SLPEN_32			REG_FIELD_PREP(PS_PWR_GATE_SLPEN_MASK, 3)

#define _PS_WIN_POS_1A      0x68170
#define _PS_WIN_POS_2A      0x68270
#define _PS_WIN_POS_1B      0x68970
#define _PS_WIN_POS_2B      0x68A70
#define _PS_WIN_POS_1C      0x69170
#define   PS_WIN_XPOS_MASK			REG_GENMASK(31, 16)
#define   PS_WIN_XPOS(x)			REG_FIELD_PREP(PS_WIN_XPOS_MASK, (x))
#define   PS_WIN_YPOS_MASK			REG_GENMASK(15, 0)
#define   PS_WIN_YPOS(y)			REG_FIELD_PREP(PS_WIN_YPOS_MASK, (y))

#define _PS_WIN_SZ_1A       0x68174
#define _PS_WIN_SZ_2A       0x68274
#define _PS_WIN_SZ_1B       0x68974
#define _PS_WIN_SZ_2B       0x68A74
#define _PS_WIN_SZ_1C       0x69174
#define   PS_WIN_XSIZE_MASK			REG_GENMASK(31, 16)
#define   PS_WIN_XSIZE(w)			REG_FIELD_PREP(PS_WIN_XSIZE_MASK, (w))
#define   PS_WIN_YSIZE_MASK			REG_GENMASK(15, 0)
#define   PS_WIN_YSIZE(h)			REG_FIELD_PREP(PS_WIN_YSIZE_MASK, (h))

#define _PS_VSCALE_1A       0x68184
#define _PS_VSCALE_2A       0x68284
#define _PS_VSCALE_1B       0x68984
#define _PS_VSCALE_2B       0x68A84
#define _PS_VSCALE_1C       0x69184

#define _PS_HSCALE_1A       0x68190
#define _PS_HSCALE_2A       0x68290
#define _PS_HSCALE_1B       0x68990
#define _PS_HSCALE_2B       0x68A90
#define _PS_HSCALE_1C       0x69190

#define _PS_VPHASE_1A       0x68188
#define _PS_VPHASE_2A       0x68288
#define _PS_VPHASE_1B       0x68988
#define _PS_VPHASE_2B       0x68A88
#define _PS_VPHASE_1C       0x69188
#define   PS_Y_PHASE_MASK			REG_GENMASK(31, 16)
#define   PS_Y_PHASE(x)				REG_FIELD_PREP(PS_Y_PHASE_MASK, (x))
#define   PS_UV_RGB_PHASE_MASK			REG_GENMASK(15, 0)
#define   PS_UV_RGB_PHASE(x)			REG_FIELD_PREP(PS_UV_RGB_PHASE_MASK, (x))
#define   PS_PHASE_MASK				(0x7fff << 1) /* u2.13 */
#define   PS_PHASE_TRIP				(1 << 0)

#define _PS_HPHASE_1A       0x68194
#define _PS_HPHASE_2A       0x68294
#define _PS_HPHASE_1B       0x68994
#define _PS_HPHASE_2B       0x68A94
#define _PS_HPHASE_1C       0x69194

#define _PS_ECC_STAT_1A     0x681D0
#define _PS_ECC_STAT_2A     0x682D0
#define _PS_ECC_STAT_1B     0x689D0
#define _PS_ECC_STAT_2B     0x68AD0
#define _PS_ECC_STAT_1C     0x691D0

#define _PS_COEF_SET0_INDEX_1A	   0x68198
#define _PS_COEF_SET0_INDEX_2A	   0x68298
#define _PS_COEF_SET0_INDEX_1B	   0x68998
#define _PS_COEF_SET0_INDEX_2B	   0x68A98
#define   PS_COEF_INDEX_AUTO_INC		REG_BIT(10)

#define _PS_COEF_SET0_DATA_1A	   0x6819C
#define _PS_COEF_SET0_DATA_2A	   0x6829C
#define _PS_COEF_SET0_DATA_1B	   0x6899C
#define _PS_COEF_SET0_DATA_2B	   0x68A9C

#define _ID(id, a, b) _PICK_EVEN(id, a, b)
#define SKL_PS_CTRL(pipe, id) _MMIO_PIPE(pipe,        \
			_ID(id, _PS_1A_CTRL, _PS_2A_CTRL),       \
			_ID(id, _PS_1B_CTRL, _PS_2B_CTRL))
#define SKL_PS_PWR_GATE(pipe, id) _MMIO_PIPE(pipe,    \
			_ID(id, _PS_PWR_GATE_1A, _PS_PWR_GATE_2A), \
			_ID(id, _PS_PWR_GATE_1B, _PS_PWR_GATE_2B))
#define SKL_PS_WIN_POS(pipe, id) _MMIO_PIPE(pipe,     \
			_ID(id, _PS_WIN_POS_1A, _PS_WIN_POS_2A), \
			_ID(id, _PS_WIN_POS_1B, _PS_WIN_POS_2B))
#define SKL_PS_WIN_SZ(pipe, id)  _MMIO_PIPE(pipe,     \
			_ID(id, _PS_WIN_SZ_1A, _PS_WIN_SZ_2A),   \
			_ID(id, _PS_WIN_SZ_1B, _PS_WIN_SZ_2B))
#define SKL_PS_VSCALE(pipe, id)  _MMIO_PIPE(pipe,     \
			_ID(id, _PS_VSCALE_1A, _PS_VSCALE_2A),   \
			_ID(id, _PS_VSCALE_1B, _PS_VSCALE_2B))
#define SKL_PS_HSCALE(pipe, id)  _MMIO_PIPE(pipe,     \
			_ID(id, _PS_HSCALE_1A, _PS_HSCALE_2A),   \
			_ID(id, _PS_HSCALE_1B, _PS_HSCALE_2B))
#define SKL_PS_VPHASE(pipe, id)  _MMIO_PIPE(pipe,     \
			_ID(id, _PS_VPHASE_1A, _PS_VPHASE_2A),   \
			_ID(id, _PS_VPHASE_1B, _PS_VPHASE_2B))
#define SKL_PS_HPHASE(pipe, id)  _MMIO_PIPE(pipe,     \
			_ID(id, _PS_HPHASE_1A, _PS_HPHASE_2A),   \
			_ID(id, _PS_HPHASE_1B, _PS_HPHASE_2B))
#define SKL_PS_ECC_STAT(pipe, id)  _MMIO_PIPE(pipe,     \
			_ID(id, _PS_ECC_STAT_1A, _PS_ECC_STAT_2A),   \
			_ID(id, _PS_ECC_STAT_1B, _PS_ECC_STAT_2B))
#define GLK_PS_COEF_INDEX_SET(pipe, id, set)  _MMIO_PIPE(pipe,    \
			_ID(id, _PS_COEF_SET0_INDEX_1A, _PS_COEF_SET0_INDEX_2A) + (set) * 8, \
			_ID(id, _PS_COEF_SET0_INDEX_1B, _PS_COEF_SET0_INDEX_2B) + (set) * 8)

#define GLK_PS_COEF_DATA_SET(pipe, id, set)  _MMIO_PIPE(pipe,     \
			_ID(id, _PS_COEF_SET0_DATA_1A, _PS_COEF_SET0_DATA_2A) + (set) * 8, \
			_ID(id, _PS_COEF_SET0_DATA_1B, _PS_COEF_SET0_DATA_2B) + (set) * 8)

/* Display Internal Timeout Register */
#define RM_TIMEOUT		_MMIO(0x42060)
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

/* More Ivybridge lolz */
#define DE_ERR_INT_IVB			(1 << 30)
#define DE_GSE_IVB			(1 << 29)
#define DE_PCH_EVENT_IVB		(1 << 28)
#define DE_DP_A_HOTPLUG_IVB		(1 << 27)
#define DE_AUX_CHANNEL_A_IVB		(1 << 26)
#define DE_EDP_PSR_INT_HSW		(1 << 19)
#define DE_SPRITEC_FLIP_DONE_IVB	(1 << 14)
#define DE_PLANEC_FLIP_DONE_IVB		(1 << 13)
#define DE_PIPEC_VBLANK_IVB		(1 << 10)
#define DE_SPRITEB_FLIP_DONE_IVB	(1 << 9)
#define DE_PLANEB_FLIP_DONE_IVB		(1 << 8)
#define DE_PIPEB_VBLANK_IVB		(1 << 5)
#define DE_SPRITEA_FLIP_DONE_IVB	(1 << 4)
#define DE_PLANEA_FLIP_DONE_IVB		(1 << 3)
#define DE_PLANE_FLIP_DONE_IVB(plane)	(1 << (3 + 5 * (plane)))
#define DE_PIPEA_VBLANK_IVB		(1 << 0)
#define DE_PIPE_VBLANK_IVB(pipe)	(1 << ((pipe) * 5))

#define VLV_MASTER_IER			_MMIO(0x4400c) /* Gunit master IER */
#define   MASTER_INTERRUPT_ENABLE	(1 << 31)

#define DEISR   _MMIO(0x44000)
#define DEIMR   _MMIO(0x44004)
#define DEIIR   _MMIO(0x44008)
#define DEIER   _MMIO(0x4400c)

#define GTISR   _MMIO(0x44010)
#define GTIMR   _MMIO(0x44014)
#define GTIIR   _MMIO(0x44018)
#define GTIER   _MMIO(0x4401c)

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

#define XELPD_DISPLAY_ERR_FATAL_MASK	_MMIO(0x4421c)

#define GEN8_GT_ISR(which) _MMIO(0x44300 + (0x10 * (which)))
#define GEN8_GT_IMR(which) _MMIO(0x44304 + (0x10 * (which)))
#define GEN8_GT_IIR(which) _MMIO(0x44308 + (0x10 * (which)))
#define GEN8_GT_IER(which) _MMIO(0x4430c + (0x10 * (which)))

#define GEN8_RCS_IRQ_SHIFT 0
#define GEN8_BCS_IRQ_SHIFT 16
#define GEN8_VCS0_IRQ_SHIFT 0  /* NB: VCS1 in bspec! */
#define GEN8_VCS1_IRQ_SHIFT 16 /* NB: VCS2 in bpsec! */
#define GEN8_VECS_IRQ_SHIFT 0
#define GEN8_WD_IRQ_SHIFT 16

#define GEN8_DE_PIPE_ISR(pipe) _MMIO(0x44400 + (0x10 * (pipe)))
#define GEN8_DE_PIPE_IMR(pipe) _MMIO(0x44404 + (0x10 * (pipe)))
#define GEN8_DE_PIPE_IIR(pipe) _MMIO(0x44408 + (0x10 * (pipe)))
#define GEN8_DE_PIPE_IER(pipe) _MMIO(0x4440c + (0x10 * (pipe)))
#define  GEN8_PIPE_FIFO_UNDERRUN	(1 << 31)
#define  GEN8_PIPE_CDCLK_CRC_ERROR	(1 << 29)
#define  GEN8_PIPE_CDCLK_CRC_DONE	(1 << 28)
#define  XELPD_PIPE_SOFT_UNDERRUN	(1 << 22)
#define  XELPD_PIPE_HARD_UNDERRUN	(1 << 21)
#define  GEN12_PIPE_VBLANK_UNMOD	(1 << 19)
#define  GEN8_PIPE_CURSOR_FAULT		(1 << 10)
#define  GEN8_PIPE_SPRITE_FAULT		(1 << 9)
#define  GEN8_PIPE_PRIMARY_FAULT	(1 << 8)
#define  GEN8_PIPE_SPRITE_FLIP_DONE	(1 << 5)
#define  GEN8_PIPE_PRIMARY_FLIP_DONE	(1 << 4)
#define  GEN8_PIPE_SCAN_LINE_EVENT	(1 << 2)
#define  GEN8_PIPE_VSYNC		(1 << 1)
#define  GEN8_PIPE_VBLANK		(1 << 0)
#define  GEN9_PIPE_CURSOR_FAULT		(1 << 11)
#define  GEN11_PIPE_PLANE7_FAULT	(1 << 22)
#define  GEN11_PIPE_PLANE6_FAULT	(1 << 21)
#define  GEN11_PIPE_PLANE5_FAULT	(1 << 20)
#define  GEN9_PIPE_PLANE4_FAULT		(1 << 10)
#define  GEN9_PIPE_PLANE3_FAULT		(1 << 9)
#define  GEN9_PIPE_PLANE2_FAULT		(1 << 8)
#define  GEN9_PIPE_PLANE1_FAULT		(1 << 7)
#define  GEN9_PIPE_PLANE4_FLIP_DONE	(1 << 6)
#define  GEN9_PIPE_PLANE3_FLIP_DONE	(1 << 5)
#define  GEN9_PIPE_PLANE2_FLIP_DONE	(1 << 4)
#define  GEN9_PIPE_PLANE1_FLIP_DONE	(1 << 3)
#define  GEN9_PIPE_PLANE_FLIP_DONE(p)	(1 << (3 + (p)))
#define GEN8_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN8_PIPE_CURSOR_FAULT | \
	 GEN8_PIPE_SPRITE_FAULT | \
	 GEN8_PIPE_PRIMARY_FAULT)
#define GEN9_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN9_PIPE_CURSOR_FAULT | \
	 GEN9_PIPE_PLANE4_FAULT | \
	 GEN9_PIPE_PLANE3_FAULT | \
	 GEN9_PIPE_PLANE2_FAULT | \
	 GEN9_PIPE_PLANE1_FAULT)
#define GEN11_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN9_DE_PIPE_IRQ_FAULT_ERRORS | \
	 GEN11_PIPE_PLANE7_FAULT | \
	 GEN11_PIPE_PLANE6_FAULT | \
	 GEN11_PIPE_PLANE5_FAULT)
#define RKL_DE_PIPE_IRQ_FAULT_ERRORS \
	(GEN9_DE_PIPE_IRQ_FAULT_ERRORS | \
	 GEN11_PIPE_PLANE5_FAULT)

#define _HPD_PIN_DDI(hpd_pin)	((hpd_pin) - HPD_PORT_A)
#define _HPD_PIN_TC(hpd_pin)	((hpd_pin) - HPD_PORT_TC1)

#define GEN8_DE_PORT_ISR _MMIO(0x44440)
#define GEN8_DE_PORT_IMR _MMIO(0x44444)
#define GEN8_DE_PORT_IIR _MMIO(0x44448)
#define GEN8_DE_PORT_IER _MMIO(0x4444c)
#define  DSI1_NON_TE			(1 << 31)
#define  DSI0_NON_TE			(1 << 30)
#define  ICL_AUX_CHANNEL_E		(1 << 29)
#define  ICL_AUX_CHANNEL_F		(1 << 28)
#define  GEN9_AUX_CHANNEL_D		(1 << 27)
#define  GEN9_AUX_CHANNEL_C		(1 << 26)
#define  GEN9_AUX_CHANNEL_B		(1 << 25)
#define  DSI1_TE			(1 << 24)
#define  DSI0_TE			(1 << 23)
#define  GEN8_DE_PORT_HOTPLUG(hpd_pin)	REG_BIT(3 + _HPD_PIN_DDI(hpd_pin))
#define  BXT_DE_PORT_HOTPLUG_MASK	(GEN8_DE_PORT_HOTPLUG(HPD_PORT_A) | \
					 GEN8_DE_PORT_HOTPLUG(HPD_PORT_B) | \
					 GEN8_DE_PORT_HOTPLUG(HPD_PORT_C))
#define  BDW_DE_PORT_HOTPLUG_MASK	GEN8_DE_PORT_HOTPLUG(HPD_PORT_A)
#define  BXT_DE_PORT_GMBUS		(1 << 1)
#define  GEN8_AUX_CHANNEL_A		(1 << 0)
#define  TGL_DE_PORT_AUX_USBC6		REG_BIT(13)
#define  XELPD_DE_PORT_AUX_DDIE		REG_BIT(13)
#define  TGL_DE_PORT_AUX_USBC5		REG_BIT(12)
#define  XELPD_DE_PORT_AUX_DDID		REG_BIT(12)
#define  TGL_DE_PORT_AUX_USBC4		REG_BIT(11)
#define  TGL_DE_PORT_AUX_USBC3		REG_BIT(10)
#define  TGL_DE_PORT_AUX_USBC2		REG_BIT(9)
#define  TGL_DE_PORT_AUX_USBC1		REG_BIT(8)
#define  TGL_DE_PORT_AUX_DDIC		REG_BIT(2)
#define  TGL_DE_PORT_AUX_DDIB		REG_BIT(1)
#define  TGL_DE_PORT_AUX_DDIA		REG_BIT(0)

#define GEN8_DE_MISC_ISR _MMIO(0x44460)
#define GEN8_DE_MISC_IMR _MMIO(0x44464)
#define GEN8_DE_MISC_IIR _MMIO(0x44468)
#define GEN8_DE_MISC_IER _MMIO(0x4446c)
#define  XELPDP_PMDEMAND_RSPTOUT_ERR	REG_BIT(27)
#define  GEN8_DE_MISC_GSE		REG_BIT(27)
#define  GEN8_DE_EDP_PSR		REG_BIT(19)
#define  XELPDP_PMDEMAND_RSP		REG_BIT(3)

#define GEN8_PCU_ISR _MMIO(0x444e0)
#define GEN8_PCU_IMR _MMIO(0x444e4)
#define GEN8_PCU_IIR _MMIO(0x444e8)
#define GEN8_PCU_IER _MMIO(0x444ec)

#define GEN11_GU_MISC_ISR	_MMIO(0x444f0)
#define GEN11_GU_MISC_IMR	_MMIO(0x444f4)
#define GEN11_GU_MISC_IIR	_MMIO(0x444f8)
#define GEN11_GU_MISC_IER	_MMIO(0x444fc)
#define  GEN11_GU_MISC_GSE	(1 << 27)

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

#define GEN11_DISPLAY_INT_CTL		_MMIO(0x44200)
#define  GEN11_DISPLAY_IRQ_ENABLE	(1 << 31)
#define  GEN11_AUDIO_CODEC_IRQ		(1 << 24)
#define  GEN11_DE_PCH_IRQ		(1 << 23)
#define  GEN11_DE_MISC_IRQ		(1 << 22)
#define  GEN11_DE_HPD_IRQ		(1 << 21)
#define  GEN11_DE_PORT_IRQ		(1 << 20)
#define  GEN11_DE_PIPE_C		(1 << 18)
#define  GEN11_DE_PIPE_B		(1 << 17)
#define  GEN11_DE_PIPE_A		(1 << 16)

#define GEN11_DE_HPD_ISR		_MMIO(0x44470)
#define GEN11_DE_HPD_IMR		_MMIO(0x44474)
#define GEN11_DE_HPD_IIR		_MMIO(0x44478)
#define GEN11_DE_HPD_IER		_MMIO(0x4447c)
#define  GEN11_TC_HOTPLUG(hpd_pin)		REG_BIT(16 + _HPD_PIN_TC(hpd_pin))
#define  GEN11_DE_TC_HOTPLUG_MASK		(GEN11_TC_HOTPLUG(HPD_PORT_TC6) | \
						 GEN11_TC_HOTPLUG(HPD_PORT_TC5) | \
						 GEN11_TC_HOTPLUG(HPD_PORT_TC4) | \
						 GEN11_TC_HOTPLUG(HPD_PORT_TC3) | \
						 GEN11_TC_HOTPLUG(HPD_PORT_TC2) | \
						 GEN11_TC_HOTPLUG(HPD_PORT_TC1))
#define  GEN11_TBT_HOTPLUG(hpd_pin)		REG_BIT(_HPD_PIN_TC(hpd_pin))
#define  GEN11_DE_TBT_HOTPLUG_MASK		(GEN11_TBT_HOTPLUG(HPD_PORT_TC6) | \
						 GEN11_TBT_HOTPLUG(HPD_PORT_TC5) | \
						 GEN11_TBT_HOTPLUG(HPD_PORT_TC4) | \
						 GEN11_TBT_HOTPLUG(HPD_PORT_TC3) | \
						 GEN11_TBT_HOTPLUG(HPD_PORT_TC2) | \
						 GEN11_TBT_HOTPLUG(HPD_PORT_TC1))

#define GEN11_TBT_HOTPLUG_CTL				_MMIO(0x44030)
#define GEN11_TC_HOTPLUG_CTL				_MMIO(0x44038)
#define  GEN11_HOTPLUG_CTL_ENABLE(hpd_pin)		(8 << (_HPD_PIN_TC(hpd_pin) * 4))
#define  GEN11_HOTPLUG_CTL_LONG_DETECT(hpd_pin)		(2 << (_HPD_PIN_TC(hpd_pin) * 4))
#define  GEN11_HOTPLUG_CTL_SHORT_DETECT(hpd_pin)	(1 << (_HPD_PIN_TC(hpd_pin) * 4))
#define  GEN11_HOTPLUG_CTL_NO_DETECT(hpd_pin)		(0 << (_HPD_PIN_TC(hpd_pin) * 4))

#define PICAINTERRUPT_ISR			_MMIO(0x16FE50)
#define PICAINTERRUPT_IMR			_MMIO(0x16FE54)
#define PICAINTERRUPT_IIR			_MMIO(0x16FE58)
#define PICAINTERRUPT_IER			_MMIO(0x16FE5C)
#define  XELPDP_DP_ALT_HOTPLUG(hpd_pin)		REG_BIT(16 + _HPD_PIN_TC(hpd_pin))
#define  XELPDP_DP_ALT_HOTPLUG_MASK		REG_GENMASK(19, 16)
#define  XELPDP_AUX_TC(hpd_pin)			REG_BIT(8 + _HPD_PIN_TC(hpd_pin))
#define  XELPDP_AUX_TC_MASK			REG_GENMASK(11, 8)
#define  XE2LPD_AUX_DDI(hpd_pin)		REG_BIT(6 + _HPD_PIN_DDI(hpd_pin))
#define  XE2LPD_AUX_DDI_MASK			REG_GENMASK(7, 6)
#define  XELPDP_TBT_HOTPLUG(hpd_pin)		REG_BIT(_HPD_PIN_TC(hpd_pin))
#define  XELPDP_TBT_HOTPLUG_MASK		REG_GENMASK(3, 0)

#define XELPDP_PORT_HOTPLUG_CTL(hpd_pin)	_MMIO(0x16F270 + (_HPD_PIN_TC(hpd_pin) * 0x200))
#define  XELPDP_TBT_HOTPLUG_ENABLE		REG_BIT(6)
#define  XELPDP_TBT_HPD_LONG_DETECT		REG_BIT(5)
#define  XELPDP_TBT_HPD_SHORT_DETECT		REG_BIT(4)
#define  XELPDP_DP_ALT_HOTPLUG_ENABLE		REG_BIT(2)
#define  XELPDP_DP_ALT_HPD_LONG_DETECT		REG_BIT(1)
#define  XELPDP_DP_ALT_HPD_SHORT_DETECT		REG_BIT(0)

#define XELPDP_INITIATE_PMDEMAND_REQUEST(dword)		_MMIO(0x45230 + 4 * (dword))
#define  XELPDP_PMDEMAND_QCLK_GV_BW_MASK		REG_GENMASK(31, 16)
#define  XELPDP_PMDEMAND_VOLTAGE_INDEX_MASK		REG_GENMASK(14, 12)
#define  XELPDP_PMDEMAND_QCLK_GV_INDEX_MASK		REG_GENMASK(11, 8)
#define  XELPDP_PMDEMAND_PIPES_MASK			REG_GENMASK(7, 6)
#define  XELPDP_PMDEMAND_DBUFS_MASK			REG_GENMASK(5, 4)
#define  XELPDP_PMDEMAND_PHYS_MASK			REG_GENMASK(2, 0)

#define  XELPDP_PMDEMAND_REQ_ENABLE			REG_BIT(31)
#define  XELPDP_PMDEMAND_CDCLK_FREQ_MASK		REG_GENMASK(30, 20)
#define  XELPDP_PMDEMAND_DDICLK_FREQ_MASK		REG_GENMASK(18, 8)
#define  XELPDP_PMDEMAND_SCALERS_MASK			REG_GENMASK(6, 4)
#define  XELPDP_PMDEMAND_PLLS_MASK			REG_GENMASK(2, 0)

#define GEN12_DCPR_STATUS_1				_MMIO(0x46440)
#define  XELPDP_PMDEMAND_INFLIGHT_STATUS		REG_BIT(26)

#define ILK_DISPLAY_CHICKEN2	_MMIO(0x42004)
/* Required on all Ironlake and Sandybridge according to the B-Spec. */
#define   ILK_ELPIN_409_SELECT	REG_BIT(25)
#define   ILK_DPARB_GATE	REG_BIT(22)
#define   ILK_VSDPFD_FULL	REG_BIT(21)

#define FUSE_STRAP		_MMIO(0x42014)
#define   ILK_INTERNAL_GRAPHICS_DISABLE	REG_BIT(31)
#define   ILK_INTERNAL_DISPLAY_DISABLE	REG_BIT(30)
#define   ILK_DISPLAY_DEBUG_DISABLE	REG_BIT(29)
#define   IVB_PIPE_C_DISABLE		REG_BIT(28)
#define   ILK_HDCP_DISABLE		REG_BIT(25)
#define   ILK_eDP_A_DISABLE		REG_BIT(24)
#define   HSW_CDCLK_LIMIT		REG_BIT(24)
#define   ILK_DESKTOP			REG_BIT(23)
#define   HSW_CPU_SSC_ENABLE		REG_BIT(21)

#define FUSE_STRAP3		_MMIO(0x42020)
#define   HSW_REF_CLK_SELECT		REG_BIT(1)

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

#define CHICKEN_MISC_2		_MMIO(0x42084)
#define   CHICKEN_MISC_DISABLE_DPT	REG_BIT(30) /* adl,dg2 */
#define   BMG_DARB_HALF_BLK_END_BURST	REG_BIT(27)
#define   KBL_ARB_FILL_SPARE_14		REG_BIT(14)
#define   KBL_ARB_FILL_SPARE_13		REG_BIT(13)
#define   GLK_CL2_PWR_DOWN		REG_BIT(12)
#define   GLK_CL1_PWR_DOWN		REG_BIT(11)
#define   GLK_CL0_PWR_DOWN		REG_BIT(10)

#define CHICKEN_MISC_3		_MMIO(0x42088)
#define   DP_MST_DPT_DPTP_ALIGN_WA(trans)	REG_BIT(9 + (trans) - TRANSCODER_A)
#define   DP_MST_SHORT_HBLANK_WA(trans)		REG_BIT(5 + (trans) - TRANSCODER_A)
#define   DP_MST_FEC_BS_JITTER_WA(trans)	REG_BIT(0 + (trans) - TRANSCODER_A)

#define CHICKEN_MISC_4		_MMIO(0x4208c)
#define   CHICKEN_FBC_STRIDE_OVERRIDE	REG_BIT(13)
#define   CHICKEN_FBC_STRIDE_MASK	REG_GENMASK(12, 0)
#define   CHICKEN_FBC_STRIDE(x)		REG_FIELD_PREP(CHICKEN_FBC_STRIDE_MASK, (x))

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

#define _CHICKEN_TRANS_A	0x420c0
#define _CHICKEN_TRANS_B	0x420c4
#define _CHICKEN_TRANS_C	0x420c8
#define _CHICKEN_TRANS_EDP	0x420cc
#define _CHICKEN_TRANS_D	0x420d8
#define CHICKEN_TRANS(trans)	_MMIO(_PICK((trans), \
					    [TRANSCODER_EDP] = _CHICKEN_TRANS_EDP, \
					    [TRANSCODER_A] = _CHICKEN_TRANS_A, \
					    [TRANSCODER_B] = _CHICKEN_TRANS_B, \
					    [TRANSCODER_C] = _CHICKEN_TRANS_C, \
					    [TRANSCODER_D] = _CHICKEN_TRANS_D))
#define _MTL_CHICKEN_TRANS_A	0x604e0
#define _MTL_CHICKEN_TRANS_B	0x614e0
#define MTL_CHICKEN_TRANS(trans)	_MMIO_TRANS((trans), \
						    _MTL_CHICKEN_TRANS_A, \
						    _MTL_CHICKEN_TRANS_B)
#define   PIPE_VBLANK_WITH_DELAY	REG_BIT(31) /* tgl+ */
#define   SKL_UNMASK_VBL_TO_PIPE_IN_SRD	REG_BIT(30) /* skl+ */
#define   HSW_FRAME_START_DELAY_MASK	REG_GENMASK(28, 27)
#define   HSW_FRAME_START_DELAY(x)	REG_FIELD_PREP(HSW_FRAME_START_DELAY_MASK, x)
#define   VSC_DATA_SEL_SOFTWARE_CONTROL	REG_BIT(25) /* GLK */
#define   FECSTALL_DIS_DPTSTREAM_DPTTG	REG_BIT(23)
#define   DDI_TRAINING_OVERRIDE_ENABLE	REG_BIT(19)
#define   ADLP_1_BASED_X_GRANULARITY	REG_BIT(18)
#define   DDI_TRAINING_OVERRIDE_VALUE	REG_BIT(18)
#define   DDIE_TRAINING_OVERRIDE_ENABLE	REG_BIT(17) /* CHICKEN_TRANS_A only */
#define   DDIE_TRAINING_OVERRIDE_VALUE	REG_BIT(16) /* CHICKEN_TRANS_A only */
#define   PSR2_ADD_VERTICAL_LINE_COUNT	REG_BIT(15)
#define   DP_FEC_BS_JITTER_WA		REG_BIT(15)
#define   PSR2_VSC_ENABLE_PROG_HEADER	REG_BIT(12)
#define   DP_DSC_INSERT_SF_AT_EOL_WA	REG_BIT(4)
#define   HDCP_LINE_REKEY_DISABLE	REG_BIT(0)

#define DISP_ARB_CTL	_MMIO(0x45000)
#define   DISP_FBC_MEMORY_WAKE		REG_BIT(31)
#define   DISP_TILE_SURFACE_SWIZZLING	REG_BIT(13)
#define   DISP_FBC_WM_DIS		REG_BIT(15)

#define DISP_ARB_CTL2	_MMIO(0x45004)
#define   DISP_DATA_PARTITION_5_6	REG_BIT(6)
#define   DISP_IPC_ENABLE		REG_BIT(3)

#define GEN7_MSG_CTL	_MMIO(0x45010)
#define  WAIT_FOR_PCH_RESET_ACK		(1 << 1)
#define  WAIT_FOR_PCH_FLR_ACK		(1 << 0)

#define _BW_BUDDY0_CTL			0x45130
#define _BW_BUDDY1_CTL			0x45140
#define BW_BUDDY_CTL(x)			_MMIO(_PICK_EVEN(x, \
							 _BW_BUDDY0_CTL, \
							 _BW_BUDDY1_CTL))
#define   BW_BUDDY_DISABLE		REG_BIT(31)
#define   BW_BUDDY_TLB_REQ_TIMER_MASK	REG_GENMASK(21, 16)
#define   BW_BUDDY_TLB_REQ_TIMER(x)	REG_FIELD_PREP(BW_BUDDY_TLB_REQ_TIMER_MASK, x)

#define _BW_BUDDY0_PAGE_MASK		0x45134
#define _BW_BUDDY1_PAGE_MASK		0x45144
#define BW_BUDDY_PAGE_MASK(x)		_MMIO(_PICK_EVEN(x, \
							 _BW_BUDDY0_PAGE_MASK, \
							 _BW_BUDDY1_PAGE_MASK))

#define HSW_NDE_RSTWRN_OPT	_MMIO(0x46408)
#define  MTL_RESET_PICA_HANDSHAKE_EN	REG_BIT(6)
#define  RESET_PCH_HANDSHAKE_ENABLE	REG_BIT(4)

#define GEN8_CHICKEN_DCPR_1			_MMIO(0x46430)
#define   LATENCY_REPORTING_REMOVED_PIPE_D	REG_BIT(31)
#define   SKL_SELECT_ALTERNATE_DC_EXIT		REG_BIT(30)
#define   LATENCY_REPORTING_REMOVED_PIPE_C	REG_BIT(25)
#define   LATENCY_REPORTING_REMOVED_PIPE_B	REG_BIT(24)
#define   LATENCY_REPORTING_REMOVED_PIPE_A	REG_BIT(23)
#define   ICL_DELAY_PMRSP			REG_BIT(22)
#define   DISABLE_FLR_SRC			REG_BIT(15)
#define   MASK_WAKEMEM				REG_BIT(13)
#define   DDI_CLOCK_REG_ACCESS			REG_BIT(7)

#define GEN11_CHICKEN_DCPR_2			_MMIO(0x46434)
#define   DCPR_MASK_MAXLATENCY_MEMUP_CLR	REG_BIT(27)
#define   DCPR_MASK_LPMODE			REG_BIT(26)
#define   DCPR_SEND_RESP_IMM			REG_BIT(25)
#define   DCPR_CLEAR_MEMSTAT_DIS		REG_BIT(24)

#define XELPD_CHICKEN_DCPR_3			_MMIO(0x46438)
#define   DMD_RSP_TIMEOUT_DISABLE		REG_BIT(19)

#define SKL_DFSM			_MMIO(0x51000)
#define   SKL_DFSM_DISPLAY_PM_DISABLE	(1 << 27)
#define   SKL_DFSM_DISPLAY_HDCP_DISABLE	(1 << 25)
#define   SKL_DFSM_CDCLK_LIMIT_MASK	(3 << 23)
#define   SKL_DFSM_CDCLK_LIMIT_675	(0 << 23)
#define   SKL_DFSM_CDCLK_LIMIT_540	(1 << 23)
#define   SKL_DFSM_CDCLK_LIMIT_450	(2 << 23)
#define   SKL_DFSM_CDCLK_LIMIT_337_5	(3 << 23)
#define   ICL_DFSM_DMC_DISABLE		(1 << 23)
#define   SKL_DFSM_PIPE_A_DISABLE	(1 << 30)
#define   SKL_DFSM_PIPE_B_DISABLE	(1 << 21)
#define   SKL_DFSM_PIPE_C_DISABLE	(1 << 28)
#define   TGL_DFSM_PIPE_D_DISABLE	(1 << 22)
#define   GLK_DFSM_DISPLAY_DSC_DISABLE	(1 << 7)

#define XE2LPD_DE_CAP			_MMIO(0x41100)
#define   XE2LPD_DE_CAP_3DLUT_MASK	REG_GENMASK(31, 30)
#define   XE2LPD_DE_CAP_DSC_MASK	REG_GENMASK(29, 28)
#define   XE2LPD_DE_CAP_DSC_REMOVED	1
#define   XE2LPD_DE_CAP_SCALER_MASK	REG_GENMASK(27, 26)
#define   XE2LPD_DE_CAP_SCALER_SINGLE	1

#define SKL_DSSM				_MMIO(0x51004)
#define ICL_DSSM_CDCLK_PLL_REFCLK_MASK		(7 << 29)
#define ICL_DSSM_CDCLK_PLL_REFCLK_24MHz		(0 << 29)
#define ICL_DSSM_CDCLK_PLL_REFCLK_19_2MHz	(1 << 29)
#define ICL_DSSM_CDCLK_PLL_REFCLK_38_4MHz	(2 << 29)

#define GMD_ID_DISPLAY				_MMIO(0x510a0)
#define   GMD_ID_ARCH_MASK			REG_GENMASK(31, 22)
#define   GMD_ID_RELEASE_MASK			REG_GENMASK(21, 14)
#define   GMD_ID_STEP				REG_GENMASK(5, 0)

/*GEN11 chicken */
#define _PIPEA_CHICKEN				0x70038
#define _PIPEB_CHICKEN				0x71038
#define _PIPEC_CHICKEN				0x72038
#define PIPE_CHICKEN(pipe)			_MMIO_PIPE(pipe, _PIPEA_CHICKEN,\
							   _PIPEB_CHICKEN)
#define   UNDERRUN_RECOVERY_DISABLE_ADLP	REG_BIT(30)
#define   UNDERRUN_RECOVERY_ENABLE_DG2		REG_BIT(30)
#define   PIXEL_ROUNDING_TRUNC_FB_PASSTHRU	REG_BIT(15)
#define   DG2_RENDER_CCSTAG_4_3_EN		REG_BIT(12)
#define   PER_PIXEL_ALPHA_BYPASS_EN		REG_BIT(7)

/* PCH */

#define PCH_DISPLAY_BASE	0xc0000u

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

/* south display engine interrupt: CPT - CNP */
#define SDE_AUDIO_POWER_D_CPT	(1 << 31)
#define SDE_AUDIO_POWER_C_CPT	(1 << 30)
#define SDE_AUDIO_POWER_B_CPT	(1 << 29)
#define SDE_AUDIO_POWER_SHIFT_CPT   29
#define SDE_AUDIO_POWER_MASK_CPT    (7 << 29)
#define SDE_AUXD_CPT		(1 << 27)
#define SDE_AUXC_CPT		(1 << 26)
#define SDE_AUXB_CPT		(1 << 25)
#define SDE_AUX_MASK_CPT	(7 << 25)
#define SDE_PORTE_HOTPLUG_SPT	(1 << 25)
#define SDE_PORTA_HOTPLUG_SPT	(1 << 24)
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
#define SDE_HOTPLUG_MASK_SPT	(SDE_PORTE_HOTPLUG_SPT |	\
				 SDE_PORTD_HOTPLUG_CPT |	\
				 SDE_PORTC_HOTPLUG_CPT |	\
				 SDE_PORTB_HOTPLUG_CPT |	\
				 SDE_PORTA_HOTPLUG_SPT)
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

/* south display engine interrupt: ICP/TGP/MTP */
#define SDE_PICAINTERRUPT		REG_BIT(31)
#define SDE_GMBUS_ICP			(1 << 23)
#define SDE_TC_HOTPLUG_ICP(hpd_pin)	REG_BIT(24 + _HPD_PIN_TC(hpd_pin))
#define SDE_TC_HOTPLUG_DG2(hpd_pin)	REG_BIT(25 + _HPD_PIN_TC(hpd_pin)) /* sigh */
#define SDE_DDI_HOTPLUG_ICP(hpd_pin)	REG_BIT(16 + _HPD_PIN_DDI(hpd_pin))
#define SDE_DDI_HOTPLUG_MASK_ICP	(SDE_DDI_HOTPLUG_ICP(HPD_PORT_D) | \
					 SDE_DDI_HOTPLUG_ICP(HPD_PORT_C) | \
					 SDE_DDI_HOTPLUG_ICP(HPD_PORT_B) | \
					 SDE_DDI_HOTPLUG_ICP(HPD_PORT_A))
#define SDE_TC_HOTPLUG_MASK_ICP		(SDE_TC_HOTPLUG_ICP(HPD_PORT_TC6) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC5) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC4) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC3) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC2) | \
					 SDE_TC_HOTPLUG_ICP(HPD_PORT_TC1))

#define SDEISR  _MMIO(0xc4000)
#define SDEIMR  _MMIO(0xc4004)
#define SDEIIR  _MMIO(0xc4008)
#define SDEIER  _MMIO(0xc400c)

#define SERR_INT			_MMIO(0xc4040)
#define  SERR_INT_POISON		(1 << 31)
#define  SERR_INT_TRANS_FIFO_UNDERRUN(pipe)	(1 << ((pipe) * 3))

/* digital port hotplug */
#define PCH_PORT_HOTPLUG		_MMIO(0xc4030)	/* SHOTPLUG_CTL */
#define  PORTA_HOTPLUG_ENABLE		(1 << 28) /* LPT:LP+ & BXT */
#define  BXT_DDIA_HPD_INVERT            (1 << 27)
#define  PORTA_HOTPLUG_STATUS_MASK	(3 << 24) /* SPT+ & BXT */
#define  PORTA_HOTPLUG_NO_DETECT	(0 << 24) /* SPT+ & BXT */
#define  PORTA_HOTPLUG_SHORT_DETECT	(1 << 24) /* SPT+ & BXT */
#define  PORTA_HOTPLUG_LONG_DETECT	(2 << 24) /* SPT+ & BXT */
#define  PORTD_HOTPLUG_ENABLE		(1 << 20)
#define  PORTD_PULSE_DURATION_2ms	(0 << 18) /* pre-LPT */
#define  PORTD_PULSE_DURATION_4_5ms	(1 << 18) /* pre-LPT */
#define  PORTD_PULSE_DURATION_6ms	(2 << 18) /* pre-LPT */
#define  PORTD_PULSE_DURATION_100ms	(3 << 18) /* pre-LPT */
#define  PORTD_PULSE_DURATION_MASK	(3 << 18) /* pre-LPT */
#define  PORTD_HOTPLUG_STATUS_MASK	(3 << 16)
#define  PORTD_HOTPLUG_NO_DETECT	(0 << 16)
#define  PORTD_HOTPLUG_SHORT_DETECT	(1 << 16)
#define  PORTD_HOTPLUG_LONG_DETECT	(2 << 16)
#define  PORTC_HOTPLUG_ENABLE		(1 << 12)
#define  BXT_DDIC_HPD_INVERT            (1 << 11)
#define  PORTC_PULSE_DURATION_2ms	(0 << 10) /* pre-LPT */
#define  PORTC_PULSE_DURATION_4_5ms	(1 << 10) /* pre-LPT */
#define  PORTC_PULSE_DURATION_6ms	(2 << 10) /* pre-LPT */
#define  PORTC_PULSE_DURATION_100ms	(3 << 10) /* pre-LPT */
#define  PORTC_PULSE_DURATION_MASK	(3 << 10) /* pre-LPT */
#define  PORTC_HOTPLUG_STATUS_MASK	(3 << 8)
#define  PORTC_HOTPLUG_NO_DETECT	(0 << 8)
#define  PORTC_HOTPLUG_SHORT_DETECT	(1 << 8)
#define  PORTC_HOTPLUG_LONG_DETECT	(2 << 8)
#define  PORTB_HOTPLUG_ENABLE		(1 << 4)
#define  BXT_DDIB_HPD_INVERT            (1 << 3)
#define  PORTB_PULSE_DURATION_2ms	(0 << 2) /* pre-LPT */
#define  PORTB_PULSE_DURATION_4_5ms	(1 << 2) /* pre-LPT */
#define  PORTB_PULSE_DURATION_6ms	(2 << 2) /* pre-LPT */
#define  PORTB_PULSE_DURATION_100ms	(3 << 2) /* pre-LPT */
#define  PORTB_PULSE_DURATION_MASK	(3 << 2) /* pre-LPT */
#define  PORTB_HOTPLUG_STATUS_MASK	(3 << 0)
#define  PORTB_HOTPLUG_NO_DETECT	(0 << 0)
#define  PORTB_HOTPLUG_SHORT_DETECT	(1 << 0)
#define  PORTB_HOTPLUG_LONG_DETECT	(2 << 0)
#define  BXT_DDI_HPD_INVERT_MASK	(BXT_DDIA_HPD_INVERT | \
					BXT_DDIB_HPD_INVERT | \
					BXT_DDIC_HPD_INVERT)

#define PCH_PORT_HOTPLUG2		_MMIO(0xc403C)	/* SHOTPLUG_CTL2 SPT+ */
#define  PORTE_HOTPLUG_ENABLE		(1 << 4)
#define  PORTE_HOTPLUG_STATUS_MASK	(3 << 0)
#define  PORTE_HOTPLUG_NO_DETECT	(0 << 0)
#define  PORTE_HOTPLUG_SHORT_DETECT	(1 << 0)
#define  PORTE_HOTPLUG_LONG_DETECT	(2 << 0)

/* This register is a reuse of PCH_PORT_HOTPLUG register. The
 * functionality covered in PCH_PORT_HOTPLUG is split into
 * SHOTPLUG_CTL_DDI and SHOTPLUG_CTL_TC.
 */

#define SHOTPLUG_CTL_DDI				_MMIO(0xc4030)
#define   SHOTPLUG_CTL_DDI_HPD_ENABLE(hpd_pin)			(0x8 << (_HPD_PIN_DDI(hpd_pin) * 4))
#define   SHOTPLUG_CTL_DDI_HPD_OUTPUT_DATA(hpd_pin)		(0x4 << (_HPD_PIN_DDI(hpd_pin) * 4))
#define   SHOTPLUG_CTL_DDI_HPD_STATUS_MASK(hpd_pin)		(0x3 << (_HPD_PIN_DDI(hpd_pin) * 4))
#define   SHOTPLUG_CTL_DDI_HPD_NO_DETECT(hpd_pin)		(0x0 << (_HPD_PIN_DDI(hpd_pin) * 4))
#define   SHOTPLUG_CTL_DDI_HPD_SHORT_DETECT(hpd_pin)		(0x1 << (_HPD_PIN_DDI(hpd_pin) * 4))
#define   SHOTPLUG_CTL_DDI_HPD_LONG_DETECT(hpd_pin)		(0x2 << (_HPD_PIN_DDI(hpd_pin) * 4))
#define   SHOTPLUG_CTL_DDI_HPD_SHORT_LONG_DETECT(hpd_pin)	(0x3 << (_HPD_PIN_DDI(hpd_pin) * 4))

#define SHOTPLUG_CTL_TC				_MMIO(0xc4034)
#define   ICP_TC_HPD_ENABLE(hpd_pin)		(8 << (_HPD_PIN_TC(hpd_pin) * 4))
#define   ICP_TC_HPD_LONG_DETECT(hpd_pin)	(2 << (_HPD_PIN_TC(hpd_pin) * 4))
#define   ICP_TC_HPD_SHORT_DETECT(hpd_pin)	(1 << (_HPD_PIN_TC(hpd_pin) * 4))

#define SHPD_FILTER_CNT				_MMIO(0xc4038)
#define   SHPD_FILTER_CNT_500_ADJ		0x001D9
#define   SHPD_FILTER_CNT_250			0x000F8

#define _PCH_DPLL_A              0xc6014
#define _PCH_DPLL_B              0xc6018
#define PCH_DPLL(pll) _MMIO((pll) == 0 ? _PCH_DPLL_A : _PCH_DPLL_B)

#define _PCH_FPA0                0xc6040
#define  FP_CB_TUNE		(0x3 << 22)
#define _PCH_FPA1                0xc6044
#define _PCH_FPB0                0xc6048
#define _PCH_FPB1                0xc604c
#define PCH_FP0(pll) _MMIO((pll) == 0 ? _PCH_FPA0 : _PCH_FPB0)
#define PCH_FP1(pll) _MMIO((pll) == 0 ? _PCH_FPA1 : _PCH_FPB1)

#define PCH_DPLL_TEST           _MMIO(0xc606c)

#define PCH_DREF_CONTROL        _MMIO(0xC6200)
#define  DREF_CONTROL_MASK      0x7fc3
#define  DREF_CPU_SOURCE_OUTPUT_DISABLE         (0 << 13)
#define  DREF_CPU_SOURCE_OUTPUT_DOWNSPREAD      (2 << 13)
#define  DREF_CPU_SOURCE_OUTPUT_NONSPREAD       (3 << 13)
#define  DREF_CPU_SOURCE_OUTPUT_MASK		(3 << 13)
#define  DREF_SSC_SOURCE_DISABLE                (0 << 11)
#define  DREF_SSC_SOURCE_ENABLE                 (2 << 11)
#define  DREF_SSC_SOURCE_MASK			(3 << 11)
#define  DREF_NONSPREAD_SOURCE_DISABLE          (0 << 9)
#define  DREF_NONSPREAD_CK505_ENABLE		(1 << 9)
#define  DREF_NONSPREAD_SOURCE_ENABLE           (2 << 9)
#define  DREF_NONSPREAD_SOURCE_MASK		(3 << 9)
#define  DREF_SUPERSPREAD_SOURCE_DISABLE        (0 << 7)
#define  DREF_SUPERSPREAD_SOURCE_ENABLE         (2 << 7)
#define  DREF_SUPERSPREAD_SOURCE_MASK		(3 << 7)
#define  DREF_SSC4_DOWNSPREAD                   (0 << 6)
#define  DREF_SSC4_CENTERSPREAD                 (1 << 6)
#define  DREF_SSC1_DISABLE                      (0 << 1)
#define  DREF_SSC1_ENABLE                       (1 << 1)
#define  DREF_SSC4_DISABLE                      (0)
#define  DREF_SSC4_ENABLE                       (1)

#define PCH_RAWCLK_FREQ         _MMIO(0xc6204)
#define  FDL_TP1_TIMER_SHIFT    12
#define  FDL_TP1_TIMER_MASK     (3 << 12)
#define  FDL_TP2_TIMER_SHIFT    10
#define  FDL_TP2_TIMER_MASK     (3 << 10)
#define  RAWCLK_FREQ_MASK       0x3ff
#define  CNP_RAWCLK_DIV_MASK	(0x3ff << 16)
#define  CNP_RAWCLK_DIV(div)	((div) << 16)
#define  CNP_RAWCLK_FRAC_MASK	(0xf << 26)
#define  CNP_RAWCLK_DEN(den)	((den) << 26)
#define  ICP_RAWCLK_NUM(num)	((num) << 11)

#define PCH_DPLL_TMR_CFG        _MMIO(0xc6208)

#define PCH_SSC4_PARMS          _MMIO(0xc6210)
#define PCH_SSC4_AUX_PARMS      _MMIO(0xc6214)

#define PCH_DPLL_SEL		_MMIO(0xc7000)
#define	 TRANS_DPLLB_SEL(pipe)		(1 << ((pipe) * 4))
#define	 TRANS_DPLLA_SEL(pipe)		0
#define  TRANS_DPLL_ENABLE(pipe)	(1 << ((pipe) * 4 + 3))

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
#define  TRANS_VSYNC_END_SHIFT		16
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
#define  GCP_COLOR_INDICATION		(1 << 2)
#define  GCP_DEFAULT_PHASE_ENABLE	(1 << 1)
#define  GCP_AV_MUTE			(1 << 0)

#define _VIDEO_DIP_CTL_B         0xe1200
#define _VIDEO_DIP_DATA_B        0xe1208
#define _VIDEO_DIP_GCP_B         0xe1210

#define TVIDEO_DIP_CTL(pipe) _MMIO_PIPE(pipe, _VIDEO_DIP_CTL_A, _VIDEO_DIP_CTL_B)
#define TVIDEO_DIP_DATA(pipe) _MMIO_PIPE(pipe, _VIDEO_DIP_DATA_A, _VIDEO_DIP_DATA_B)
#define TVIDEO_DIP_GCP(pipe) _MMIO_PIPE(pipe, _VIDEO_DIP_GCP_A, _VIDEO_DIP_GCP_B)

/* Per-transcoder DIP controls (VLV) */
#define _VLV_VIDEO_DIP_CTL_A		0x60200
#define _VLV_VIDEO_DIP_CTL_B		0x61170
#define _CHV_VIDEO_DIP_CTL_C		0x611f0
#define VLV_TVIDEO_DIP_CTL(pipe)	_MMIO_BASE_PIPE3(VLV_DISPLAY_BASE, (pipe), \
							 _VLV_VIDEO_DIP_CTL_A, \
							 _VLV_VIDEO_DIP_CTL_B, \
							 _CHV_VIDEO_DIP_CTL_C)

#define _VLV_VIDEO_DIP_DATA_A		0x60208
#define _VLV_VIDEO_DIP_DATA_B		0x61174
#define _CHV_VIDEO_DIP_DATA_C		0x611f4
#define VLV_TVIDEO_DIP_DATA(pipe)	_MMIO_BASE_PIPE3(VLV_DISPLAY_BASE, (pipe), \
							 _VLV_VIDEO_DIP_DATA_A, \
							 _VLV_VIDEO_DIP_DATA_B, \
							 _CHV_VIDEO_DIP_DATA_C)

#define _VLV_VIDEO_DIP_GDCP_PAYLOAD_A	0x60210
#define _VLV_VIDEO_DIP_GDCP_PAYLOAD_B	0x61178
#define _CHV_VIDEO_DIP_GDCP_PAYLOAD_C	0x611f8
#define VLV_TVIDEO_DIP_GCP(pipe)	_MMIO_BASE_PIPE3(VLV_DISPLAY_BASE, (pipe), \
							 _VLV_VIDEO_DIP_GDCP_PAYLOAD_A, \
							 _VLV_VIDEO_DIP_GDCP_PAYLOAD_B, \
							 _CHV_VIDEO_DIP_GDCP_PAYLOAD_C)

/* Haswell DIP controls */

#define _HSW_VIDEO_DIP_CTL_A		0x60200
#define _HSW_VIDEO_DIP_AVI_DATA_A	0x60220
#define _HSW_VIDEO_DIP_VS_DATA_A	0x60260
#define _HSW_VIDEO_DIP_SPD_DATA_A	0x602A0
#define _HSW_VIDEO_DIP_GMP_DATA_A	0x602E0
#define _HSW_VIDEO_DIP_VSC_DATA_A	0x60320
#define	_ADL_VIDEO_DIP_AS_DATA_A	0x60484
#define _GLK_VIDEO_DIP_DRM_DATA_A	0x60440
#define _HSW_VIDEO_DIP_AVI_ECC_A	0x60240
#define _HSW_VIDEO_DIP_VS_ECC_A		0x60280
#define _HSW_VIDEO_DIP_SPD_ECC_A	0x602C0
#define _HSW_VIDEO_DIP_GMP_ECC_A	0x60300
#define _HSW_VIDEO_DIP_VSC_ECC_A	0x60344
#define _HSW_VIDEO_DIP_GCP_A		0x60210

#define _HSW_VIDEO_DIP_CTL_B		0x61200
#define _HSW_VIDEO_DIP_AVI_DATA_B	0x61220
#define _HSW_VIDEO_DIP_VS_DATA_B	0x61260
#define _HSW_VIDEO_DIP_SPD_DATA_B	0x612A0
#define _HSW_VIDEO_DIP_GMP_DATA_B	0x612E0
#define _HSW_VIDEO_DIP_VSC_DATA_B	0x61320
#define _ADL_VIDEO_DIP_AS_DATA_B	0x61484
#define _GLK_VIDEO_DIP_DRM_DATA_B	0x61440
#define _HSW_VIDEO_DIP_BVI_ECC_B	0x61240
#define _HSW_VIDEO_DIP_VS_ECC_B		0x61280
#define _HSW_VIDEO_DIP_SPD_ECC_B	0x612C0
#define _HSW_VIDEO_DIP_GMP_ECC_B	0x61300
#define _HSW_VIDEO_DIP_VSC_ECC_B	0x61344
#define _HSW_VIDEO_DIP_GCP_B		0x61210

/* Icelake PPS_DATA and _ECC DIP Registers.
 * These are available for transcoders B,C and eDP.
 * Adding the _A so as to reuse the _MMIO_TRANS2
 * definition, with which it offsets to the right location.
 */

#define _ICL_VIDEO_DIP_PPS_DATA_A	0x60350
#define _ICL_VIDEO_DIP_PPS_DATA_B	0x61350
#define _ICL_VIDEO_DIP_PPS_ECC_A	0x603D4
#define _ICL_VIDEO_DIP_PPS_ECC_B	0x613D4

#define HSW_TVIDEO_DIP_CTL(dev_priv, trans)		_MMIO_TRANS2(dev_priv, trans, _HSW_VIDEO_DIP_CTL_A)
#define HSW_TVIDEO_DIP_GCP(dev_priv, trans)		_MMIO_TRANS2(dev_priv, trans, _HSW_VIDEO_DIP_GCP_A)
#define HSW_TVIDEO_DIP_AVI_DATA(dev_priv, trans, i)	_MMIO_TRANS2(dev_priv, trans, _HSW_VIDEO_DIP_AVI_DATA_A + (i) * 4)
#define HSW_TVIDEO_DIP_VS_DATA(dev_priv, trans, i)	_MMIO_TRANS2(dev_priv, trans, _HSW_VIDEO_DIP_VS_DATA_A + (i) * 4)
#define HSW_TVIDEO_DIP_SPD_DATA(dev_priv, trans, i)	_MMIO_TRANS2(dev_priv, trans, _HSW_VIDEO_DIP_SPD_DATA_A + (i) * 4)
#define HSW_TVIDEO_DIP_GMP_DATA(dev_priv, trans, i)	_MMIO_TRANS2(dev_priv, trans, _HSW_VIDEO_DIP_GMP_DATA_A + (i) * 4)
#define HSW_TVIDEO_DIP_VSC_DATA(dev_priv, trans, i)	_MMIO_TRANS2(dev_priv, trans, _HSW_VIDEO_DIP_VSC_DATA_A + (i) * 4)
#define GLK_TVIDEO_DIP_DRM_DATA(dev_priv, trans, i)	_MMIO_TRANS2(dev_priv, trans, _GLK_VIDEO_DIP_DRM_DATA_A + (i) * 4)
#define ICL_VIDEO_DIP_PPS_DATA(dev_priv, trans, i)	_MMIO_TRANS2(dev_priv, trans, _ICL_VIDEO_DIP_PPS_DATA_A + (i) * 4)
#define ICL_VIDEO_DIP_PPS_ECC(dev_priv, trans, i)		_MMIO_TRANS2(dev_priv, trans, _ICL_VIDEO_DIP_PPS_ECC_A + (i) * 4)
/*ADLP and later: */
#define ADL_TVIDEO_DIP_AS_SDP_DATA(dev_priv, trans, i)	_MMIO_TRANS2(dev_priv, trans,\
							     _ADL_VIDEO_DIP_AS_DATA_A + (i) * 4)

#define _HSW_STEREO_3D_CTL_A		0x70020
#define   S3D_ENABLE			(1 << 31)
#define _HSW_STEREO_3D_CTL_B		0x71020

#define HSW_STEREO_3D_CTL(trans)	_MMIO_PIPE2(dev_priv, trans, _HSW_STEREO_3D_CTL_A)

#define _PCH_TRANS_HTOTAL_B          0xe1000
#define _PCH_TRANS_HBLANK_B          0xe1004
#define _PCH_TRANS_HSYNC_B           0xe1008
#define _PCH_TRANS_VTOTAL_B          0xe100c
#define _PCH_TRANS_VBLANK_B          0xe1010
#define _PCH_TRANS_VSYNC_B           0xe1014
#define _PCH_TRANS_VSYNCSHIFT_B 0xe1028

#define PCH_TRANS_HTOTAL(pipe)		_MMIO_PIPE(pipe, _PCH_TRANS_HTOTAL_A, _PCH_TRANS_HTOTAL_B)
#define PCH_TRANS_HBLANK(pipe)		_MMIO_PIPE(pipe, _PCH_TRANS_HBLANK_A, _PCH_TRANS_HBLANK_B)
#define PCH_TRANS_HSYNC(pipe)		_MMIO_PIPE(pipe, _PCH_TRANS_HSYNC_A, _PCH_TRANS_HSYNC_B)
#define PCH_TRANS_VTOTAL(pipe)		_MMIO_PIPE(pipe, _PCH_TRANS_VTOTAL_A, _PCH_TRANS_VTOTAL_B)
#define PCH_TRANS_VBLANK(pipe)		_MMIO_PIPE(pipe, _PCH_TRANS_VBLANK_A, _PCH_TRANS_VBLANK_B)
#define PCH_TRANS_VSYNC(pipe)		_MMIO_PIPE(pipe, _PCH_TRANS_VSYNC_A, _PCH_TRANS_VSYNC_B)
#define PCH_TRANS_VSYNCSHIFT(pipe)	_MMIO_PIPE(pipe, _PCH_TRANS_VSYNCSHIFT_A, _PCH_TRANS_VSYNCSHIFT_B)

#define _PCH_TRANSB_DATA_M1	0xe1030
#define _PCH_TRANSB_DATA_N1	0xe1034
#define _PCH_TRANSB_DATA_M2	0xe1038
#define _PCH_TRANSB_DATA_N2	0xe103c
#define _PCH_TRANSB_LINK_M1	0xe1040
#define _PCH_TRANSB_LINK_N1	0xe1044
#define _PCH_TRANSB_LINK_M2	0xe1048
#define _PCH_TRANSB_LINK_N2	0xe104c

#define PCH_TRANS_DATA_M1(pipe)	_MMIO_PIPE(pipe, _PCH_TRANSA_DATA_M1, _PCH_TRANSB_DATA_M1)
#define PCH_TRANS_DATA_N1(pipe)	_MMIO_PIPE(pipe, _PCH_TRANSA_DATA_N1, _PCH_TRANSB_DATA_N1)
#define PCH_TRANS_DATA_M2(pipe)	_MMIO_PIPE(pipe, _PCH_TRANSA_DATA_M2, _PCH_TRANSB_DATA_M2)
#define PCH_TRANS_DATA_N2(pipe)	_MMIO_PIPE(pipe, _PCH_TRANSA_DATA_N2, _PCH_TRANSB_DATA_N2)
#define PCH_TRANS_LINK_M1(pipe)	_MMIO_PIPE(pipe, _PCH_TRANSA_LINK_M1, _PCH_TRANSB_LINK_M1)
#define PCH_TRANS_LINK_N1(pipe)	_MMIO_PIPE(pipe, _PCH_TRANSA_LINK_N1, _PCH_TRANSB_LINK_N1)
#define PCH_TRANS_LINK_M2(pipe)	_MMIO_PIPE(pipe, _PCH_TRANSA_LINK_M2, _PCH_TRANSB_LINK_M2)
#define PCH_TRANS_LINK_N2(pipe)	_MMIO_PIPE(pipe, _PCH_TRANSA_LINK_N2, _PCH_TRANSB_LINK_N2)

#define _PCH_TRANSACONF              0xf0008
#define _PCH_TRANSBCONF              0xf1008
#define PCH_TRANSCONF(pipe)	_MMIO_PIPE(pipe, _PCH_TRANSACONF, _PCH_TRANSBCONF)
#define LPT_TRANSCONF		PCH_TRANSCONF(PIPE_A) /* lpt has only one transcoder */
#define  TRANS_ENABLE			REG_BIT(31)
#define  TRANS_STATE_ENABLE		REG_BIT(30)
#define  TRANS_FRAME_START_DELAY_MASK	REG_GENMASK(28, 27) /* ibx */
#define  TRANS_FRAME_START_DELAY(x)	REG_FIELD_PREP(TRANS_FRAME_START_DELAY_MASK, (x)) /* ibx: 0-3 */
#define  TRANS_INTERLACE_MASK		REG_GENMASK(23, 21)
#define  TRANS_INTERLACE_PROGRESSIVE	REG_FIELD_PREP(TRANS_INTERLACE_MASK, 0)
#define  TRANS_INTERLACE_LEGACY_VSYNC_IBX	REG_FIELD_PREP(TRANS_INTERLACE_MASK, 2) /* ibx */
#define  TRANS_INTERLACE_INTERLACED	REG_FIELD_PREP(TRANS_INTERLACE_MASK, 3)
#define  TRANS_BPC_MASK			REG_GENMASK(7, 5) /* ibx */
#define  TRANS_BPC_8			REG_FIELD_PREP(TRANS_BPC_MASK, 0)
#define  TRANS_BPC_10			REG_FIELD_PREP(TRANS_BPC_MASK, 1)
#define  TRANS_BPC_6			REG_FIELD_PREP(TRANS_BPC_MASK, 2)
#define  TRANS_BPC_12			REG_FIELD_PREP(TRANS_BPC_MASK, 3)

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

#define _PCH_DP_B		0xe4100
#define PCH_DP_B		_MMIO(_PCH_DP_B)
#define _PCH_DPB_AUX_CH_CTL	0xe4110
#define _PCH_DPB_AUX_CH_DATA1	0xe4114
#define _PCH_DPB_AUX_CH_DATA2	0xe4118
#define _PCH_DPB_AUX_CH_DATA3	0xe411c
#define _PCH_DPB_AUX_CH_DATA4	0xe4120
#define _PCH_DPB_AUX_CH_DATA5	0xe4124

#define _PCH_DP_C		0xe4200
#define PCH_DP_C		_MMIO(_PCH_DP_C)
#define _PCH_DPC_AUX_CH_CTL	0xe4210
#define _PCH_DPC_AUX_CH_DATA1	0xe4214
#define _PCH_DPC_AUX_CH_DATA2	0xe4218
#define _PCH_DPC_AUX_CH_DATA3	0xe421c
#define _PCH_DPC_AUX_CH_DATA4	0xe4220
#define _PCH_DPC_AUX_CH_DATA5	0xe4224

#define _PCH_DP_D		0xe4300
#define PCH_DP_D		_MMIO(_PCH_DP_D)
#define _PCH_DPD_AUX_CH_CTL	0xe4310
#define _PCH_DPD_AUX_CH_DATA1	0xe4314
#define _PCH_DPD_AUX_CH_DATA2	0xe4318
#define _PCH_DPD_AUX_CH_DATA3	0xe431c
#define _PCH_DPD_AUX_CH_DATA4	0xe4320
#define _PCH_DPD_AUX_CH_DATA5	0xe4324

#define PCH_DP_AUX_CH_CTL(aux_ch)		_MMIO_PORT((aux_ch) - AUX_CH_B, _PCH_DPB_AUX_CH_CTL, _PCH_DPC_AUX_CH_CTL)
#define PCH_DP_AUX_CH_DATA(aux_ch, i)	_MMIO(_PORT((aux_ch) - AUX_CH_B, _PCH_DPB_AUX_CH_DATA1, _PCH_DPC_AUX_CH_DATA1) + (i) * 4) /* 5 registers */

/* CPT */
#define _TRANS_DP_CTL_A		0xe0300
#define _TRANS_DP_CTL_B		0xe1300
#define _TRANS_DP_CTL_C		0xe2300
#define TRANS_DP_CTL(pipe)	_MMIO_PIPE(pipe, _TRANS_DP_CTL_A, _TRANS_DP_CTL_B)
#define  TRANS_DP_OUTPUT_ENABLE		REG_BIT(31)
#define  TRANS_DP_PORT_SEL_MASK		REG_GENMASK(30, 29)
#define  TRANS_DP_PORT_SEL_NONE		REG_FIELD_PREP(TRANS_DP_PORT_SEL_MASK, 3)
#define  TRANS_DP_PORT_SEL(port)	REG_FIELD_PREP(TRANS_DP_PORT_SEL_MASK, (port) - PORT_B)
#define  TRANS_DP_AUDIO_ONLY		REG_BIT(26)
#define  TRANS_DP_ENH_FRAMING		REG_BIT(18)
#define  TRANS_DP_BPC_MASK		REG_GENMASK(10, 9)
#define  TRANS_DP_BPC_8			REG_FIELD_PREP(TRANS_DP_BPC_MASK, 0)
#define  TRANS_DP_BPC_10		REG_FIELD_PREP(TRANS_DP_BPC_MASK, 1)
#define  TRANS_DP_BPC_6			REG_FIELD_PREP(TRANS_DP_BPC_MASK, 2)
#define  TRANS_DP_BPC_12		REG_FIELD_PREP(TRANS_DP_BPC_MASK, 3)
#define  TRANS_DP_VSYNC_ACTIVE_HIGH	REG_BIT(4)
#define  TRANS_DP_HSYNC_ACTIVE_HIGH	REG_BIT(3)

#define _TRANS_DP2_CTL_A			0x600a0
#define _TRANS_DP2_CTL_B			0x610a0
#define _TRANS_DP2_CTL_C			0x620a0
#define _TRANS_DP2_CTL_D			0x630a0
#define TRANS_DP2_CTL(trans)			_MMIO_TRANS(trans, _TRANS_DP2_CTL_A, _TRANS_DP2_CTL_B)
#define  TRANS_DP2_128B132B_CHANNEL_CODING	REG_BIT(31)
#define  TRANS_DP2_PANEL_REPLAY_ENABLE		REG_BIT(30)
#define  TRANS_DP2_DEBUG_ENABLE			REG_BIT(23)

#define _TRANS_DP2_VFREQHIGH_A			0x600a4
#define _TRANS_DP2_VFREQHIGH_B			0x610a4
#define _TRANS_DP2_VFREQHIGH_C			0x620a4
#define _TRANS_DP2_VFREQHIGH_D			0x630a4
#define TRANS_DP2_VFREQHIGH(trans)		_MMIO_TRANS(trans, _TRANS_DP2_VFREQHIGH_A, _TRANS_DP2_VFREQHIGH_B)
#define  TRANS_DP2_VFREQ_PIXEL_CLOCK_MASK	REG_GENMASK(31, 8)
#define  TRANS_DP2_VFREQ_PIXEL_CLOCK(clk_hz)	REG_FIELD_PREP(TRANS_DP2_VFREQ_PIXEL_CLOCK_MASK, (clk_hz))

#define _TRANS_DP2_VFREQLOW_A			0x600a8
#define _TRANS_DP2_VFREQLOW_B			0x610a8
#define _TRANS_DP2_VFREQLOW_C			0x620a8
#define _TRANS_DP2_VFREQLOW_D			0x630a8
#define TRANS_DP2_VFREQLOW(trans)		_MMIO_TRANS(trans, _TRANS_DP2_VFREQLOW_A, _TRANS_DP2_VFREQLOW_B)

/* SNB eDP training params */
/* SNB A-stepping */
#define  EDP_LINK_TRAIN_400MV_0DB_SNB_A		(0x38 << 22)
#define  EDP_LINK_TRAIN_400MV_6DB_SNB_A		(0x02 << 22)
#define  EDP_LINK_TRAIN_600MV_3_5DB_SNB_A	(0x01 << 22)
#define  EDP_LINK_TRAIN_800MV_0DB_SNB_A		(0x0 << 22)
/* SNB B-stepping */
#define  EDP_LINK_TRAIN_400_600MV_0DB_SNB_B	(0x0 << 22)
#define  EDP_LINK_TRAIN_400MV_3_5DB_SNB_B	(0x1 << 22)
#define  EDP_LINK_TRAIN_400_600MV_6DB_SNB_B	(0x3a << 22)
#define  EDP_LINK_TRAIN_600_800MV_3_5DB_SNB_B	(0x39 << 22)
#define  EDP_LINK_TRAIN_800_1200MV_0DB_SNB_B	(0x38 << 22)
#define  EDP_LINK_TRAIN_VOL_EMP_MASK_SNB	(0x3f << 22)

/* IVB */
#define EDP_LINK_TRAIN_400MV_0DB_IVB		(0x24 << 22)
#define EDP_LINK_TRAIN_400MV_3_5DB_IVB		(0x2a << 22)
#define EDP_LINK_TRAIN_400MV_6DB_IVB		(0x2f << 22)
#define EDP_LINK_TRAIN_600MV_0DB_IVB		(0x30 << 22)
#define EDP_LINK_TRAIN_600MV_3_5DB_IVB		(0x36 << 22)
#define EDP_LINK_TRAIN_800MV_0DB_IVB		(0x38 << 22)
#define EDP_LINK_TRAIN_800MV_3_5DB_IVB		(0x3e << 22)

/* legacy values */
#define EDP_LINK_TRAIN_500MV_0DB_IVB		(0x00 << 22)
#define EDP_LINK_TRAIN_1000MV_0DB_IVB		(0x20 << 22)
#define EDP_LINK_TRAIN_500MV_3_5DB_IVB		(0x02 << 22)
#define EDP_LINK_TRAIN_1000MV_3_5DB_IVB		(0x22 << 22)
#define EDP_LINK_TRAIN_1000MV_6DB_IVB		(0x23 << 22)

#define  EDP_LINK_TRAIN_VOL_EMP_MASK_IVB	(0x3f << 22)

#define  VLV_PMWGICZ				_MMIO(0x1300a4)

#define  HSW_EDRAM_CAP				_MMIO(0x120010)
#define    EDRAM_ENABLED			0x1
#define    EDRAM_NUM_BANKS(cap)			(((cap) >> 1) & 0xf)
#define    EDRAM_WAYS_IDX(cap)			(((cap) >> 5) & 0x7)
#define    EDRAM_SETS_IDX(cap)			(((cap) >> 8) & 0x3)

#define VLV_CHICKEN_3				_MMIO(VLV_DISPLAY_BASE + 0x7040C)
#define  PIXEL_OVERLAP_CNT_MASK			(3 << 30)
#define  PIXEL_OVERLAP_CNT_SHIFT		30

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

/*
 * HSW - ICL power wells
 *
 * Platforms have up to 3 power well control register sets, each set
 * controlling up to 16 power wells via a request/status HW flag tuple:
 * - main (HSW_PWR_WELL_CTL[1-4])
 * - AUX  (ICL_PWR_WELL_CTL_AUX[1-4])
 * - DDI  (ICL_PWR_WELL_CTL_DDI[1-4])
 * Each control register set consists of up to 4 registers used by different
 * sources that can request a power well to be enabled:
 * - BIOS   (HSW_PWR_WELL_CTL1/ICL_PWR_WELL_CTL_AUX1/ICL_PWR_WELL_CTL_DDI1)
 * - DRIVER (HSW_PWR_WELL_CTL2/ICL_PWR_WELL_CTL_AUX2/ICL_PWR_WELL_CTL_DDI2)
 * - KVMR   (HSW_PWR_WELL_CTL3)   (only in the main register set)
 * - DEBUG  (HSW_PWR_WELL_CTL4/ICL_PWR_WELL_CTL_AUX4/ICL_PWR_WELL_CTL_DDI4)
 */
#define HSW_PWR_WELL_CTL1			_MMIO(0x45400)
#define HSW_PWR_WELL_CTL2			_MMIO(0x45404)
#define HSW_PWR_WELL_CTL3			_MMIO(0x45408)
#define HSW_PWR_WELL_CTL4			_MMIO(0x4540C)
#define   HSW_PWR_WELL_CTL_REQ(pw_idx)		(0x2 << ((pw_idx) * 2))
#define   HSW_PWR_WELL_CTL_STATE(pw_idx)	(0x1 << ((pw_idx) * 2))

/* HSW/BDW power well */
#define   HSW_PW_CTL_IDX_GLOBAL			15

/* SKL/BXT/GLK power wells */
#define   SKL_PW_CTL_IDX_PW_2			15
#define   SKL_PW_CTL_IDX_PW_1			14
#define   GLK_PW_CTL_IDX_AUX_C			10
#define   GLK_PW_CTL_IDX_AUX_B			9
#define   GLK_PW_CTL_IDX_AUX_A			8
#define   SKL_PW_CTL_IDX_DDI_D			4
#define   SKL_PW_CTL_IDX_DDI_C			3
#define   SKL_PW_CTL_IDX_DDI_B			2
#define   SKL_PW_CTL_IDX_DDI_A_E		1
#define   GLK_PW_CTL_IDX_DDI_A			1
#define   SKL_PW_CTL_IDX_MISC_IO		0

/* ICL/TGL - power wells */
#define   TGL_PW_CTL_IDX_PW_5			4
#define   ICL_PW_CTL_IDX_PW_4			3
#define   ICL_PW_CTL_IDX_PW_3			2
#define   ICL_PW_CTL_IDX_PW_2			1
#define   ICL_PW_CTL_IDX_PW_1			0

/* XE_LPD - power wells */
#define   XELPD_PW_CTL_IDX_PW_D			8
#define   XELPD_PW_CTL_IDX_PW_C			7
#define   XELPD_PW_CTL_IDX_PW_B			6
#define   XELPD_PW_CTL_IDX_PW_A			5

#define ICL_PWR_WELL_CTL_AUX1			_MMIO(0x45440)
#define ICL_PWR_WELL_CTL_AUX2			_MMIO(0x45444)
#define ICL_PWR_WELL_CTL_AUX4			_MMIO(0x4544C)
#define   TGL_PW_CTL_IDX_AUX_TBT6		14
#define   TGL_PW_CTL_IDX_AUX_TBT5		13
#define   TGL_PW_CTL_IDX_AUX_TBT4		12
#define   ICL_PW_CTL_IDX_AUX_TBT4		11
#define   TGL_PW_CTL_IDX_AUX_TBT3		11
#define   ICL_PW_CTL_IDX_AUX_TBT3		10
#define   TGL_PW_CTL_IDX_AUX_TBT2		10
#define   ICL_PW_CTL_IDX_AUX_TBT2		9
#define   TGL_PW_CTL_IDX_AUX_TBT1		9
#define   ICL_PW_CTL_IDX_AUX_TBT1		8
#define   TGL_PW_CTL_IDX_AUX_TC6		8
#define   XELPD_PW_CTL_IDX_AUX_E			8
#define   TGL_PW_CTL_IDX_AUX_TC5		7
#define   XELPD_PW_CTL_IDX_AUX_D			7
#define   TGL_PW_CTL_IDX_AUX_TC4		6
#define   ICL_PW_CTL_IDX_AUX_F			5
#define   TGL_PW_CTL_IDX_AUX_TC3		5
#define   ICL_PW_CTL_IDX_AUX_E			4
#define   TGL_PW_CTL_IDX_AUX_TC2		4
#define   ICL_PW_CTL_IDX_AUX_D			3
#define   TGL_PW_CTL_IDX_AUX_TC1		3
#define   ICL_PW_CTL_IDX_AUX_C			2
#define   ICL_PW_CTL_IDX_AUX_B			1
#define   ICL_PW_CTL_IDX_AUX_A			0

#define ICL_PWR_WELL_CTL_DDI1			_MMIO(0x45450)
#define ICL_PWR_WELL_CTL_DDI2			_MMIO(0x45454)
#define ICL_PWR_WELL_CTL_DDI4			_MMIO(0x4545C)
#define   XELPD_PW_CTL_IDX_DDI_E			8
#define   TGL_PW_CTL_IDX_DDI_TC6		8
#define   XELPD_PW_CTL_IDX_DDI_D			7
#define   TGL_PW_CTL_IDX_DDI_TC5		7
#define   TGL_PW_CTL_IDX_DDI_TC4		6
#define   ICL_PW_CTL_IDX_DDI_F			5
#define   TGL_PW_CTL_IDX_DDI_TC3		5
#define   ICL_PW_CTL_IDX_DDI_E			4
#define   TGL_PW_CTL_IDX_DDI_TC2		4
#define   ICL_PW_CTL_IDX_DDI_D			3
#define   TGL_PW_CTL_IDX_DDI_TC1		3
#define   ICL_PW_CTL_IDX_DDI_C			2
#define   ICL_PW_CTL_IDX_DDI_B			1
#define   ICL_PW_CTL_IDX_DDI_A			0

/* HSW - power well misc debug registers */
#define HSW_PWR_WELL_CTL5			_MMIO(0x45410)
#define   HSW_PWR_WELL_ENABLE_SINGLE_STEP	(1 << 31)
#define   HSW_PWR_WELL_PWR_GATE_OVERRIDE	(1 << 20)
#define   HSW_PWR_WELL_FORCE_ON			(1 << 19)
#define HSW_PWR_WELL_CTL6			_MMIO(0x45414)

/* SKL Fuse Status */
enum skl_power_gate {
	SKL_PG0,
	SKL_PG1,
	SKL_PG2,
	ICL_PG3,
	ICL_PG4,
};

#define SKL_FUSE_STATUS				_MMIO(0x42000)
#define  SKL_FUSE_DOWNLOAD_STATUS		(1 << 31)
/*
 * PG0 is HW controlled, so doesn't have a corresponding power well control knob
 * SKL_DISP_PW1_IDX..SKL_DISP_PW2_IDX -> PG1..PG2
 */
#define  SKL_PW_CTL_IDX_TO_PG(pw_idx)		\
	((pw_idx) - SKL_PW_CTL_IDX_PW_1 + SKL_PG1)
/*
 * PG0 is HW controlled, so doesn't have a corresponding power well control knob
 * ICL_DISP_PW1_IDX..ICL_DISP_PW4_IDX -> PG1..PG4
 */
#define  ICL_PW_CTL_IDX_TO_PG(pw_idx)		\
	((pw_idx) - ICL_PW_CTL_IDX_PW_1 + SKL_PG1)
#define  SKL_FUSE_PG_DIST_STATUS(pg)		(1 << (27 - (pg)))

/* Per-pipe DDI Function Control */
#define _TRANS_DDI_FUNC_CTL_A		0x60400
#define _TRANS_DDI_FUNC_CTL_B		0x61400
#define _TRANS_DDI_FUNC_CTL_C		0x62400
#define _TRANS_DDI_FUNC_CTL_D		0x63400
#define _TRANS_DDI_FUNC_CTL_EDP		0x6F400
#define _TRANS_DDI_FUNC_CTL_DSI0	0x6b400
#define _TRANS_DDI_FUNC_CTL_DSI1	0x6bc00
#define TRANS_DDI_FUNC_CTL(tran) _MMIO_TRANS2(dev_priv, tran, _TRANS_DDI_FUNC_CTL_A)

#define  TRANS_DDI_FUNC_ENABLE		(1 << 31)
/* Those bits are ignored by pipe EDP since it can only connect to DDI A */
#define  TRANS_DDI_PORT_SHIFT		28
#define  TGL_TRANS_DDI_PORT_SHIFT	27
#define  TRANS_DDI_PORT_MASK		(7 << TRANS_DDI_PORT_SHIFT)
#define  TGL_TRANS_DDI_PORT_MASK	(0xf << TGL_TRANS_DDI_PORT_SHIFT)
#define  TRANS_DDI_SELECT_PORT(x)	((x) << TRANS_DDI_PORT_SHIFT)
#define  TGL_TRANS_DDI_SELECT_PORT(x)	(((x) + 1) << TGL_TRANS_DDI_PORT_SHIFT)
#define  TRANS_DDI_MODE_SELECT_MASK	(7 << 24)
#define  TRANS_DDI_MODE_SELECT_HDMI	(0 << 24)
#define  TRANS_DDI_MODE_SELECT_DVI	(1 << 24)
#define  TRANS_DDI_MODE_SELECT_DP_SST	(2 << 24)
#define  TRANS_DDI_MODE_SELECT_DP_MST	(3 << 24)
#define  TRANS_DDI_MODE_SELECT_FDI_OR_128B132B	(4 << 24)
#define  TRANS_DDI_BPC_MASK		(7 << 20)
#define  TRANS_DDI_BPC_8		(0 << 20)
#define  TRANS_DDI_BPC_10		(1 << 20)
#define  TRANS_DDI_BPC_6		(2 << 20)
#define  TRANS_DDI_BPC_12		(3 << 20)
#define  TRANS_DDI_PORT_SYNC_MASTER_SELECT_MASK	REG_GENMASK(19, 18)
#define  TRANS_DDI_PORT_SYNC_MASTER_SELECT(x)	REG_FIELD_PREP(TRANS_DDI_PORT_SYNC_MASTER_SELECT_MASK, (x))
#define  TRANS_DDI_PVSYNC		(1 << 17)
#define  TRANS_DDI_PHSYNC		(1 << 16)
#define  TRANS_DDI_PORT_SYNC_ENABLE	REG_BIT(15)
#define  TRANS_DDI_EDP_INPUT_MASK	(7 << 12)
#define  TRANS_DDI_EDP_INPUT_A_ON	(0 << 12)
#define  TRANS_DDI_EDP_INPUT_A_ONOFF	(4 << 12)
#define  TRANS_DDI_EDP_INPUT_B_ONOFF	(5 << 12)
#define  TRANS_DDI_EDP_INPUT_C_ONOFF	(6 << 12)
#define  TRANS_DDI_EDP_INPUT_D_ONOFF	(7 << 12)
#define  TRANS_DDI_HDCP_LINE_REKEY_DISABLE	REG_BIT(12)
#define  TRANS_DDI_MST_TRANSPORT_SELECT_MASK	REG_GENMASK(11, 10)
#define  TRANS_DDI_MST_TRANSPORT_SELECT(trans)	\
	REG_FIELD_PREP(TRANS_DDI_MST_TRANSPORT_SELECT_MASK, trans)
#define  TRANS_DDI_HDCP_SIGNALLING	(1 << 9)
#define  TRANS_DDI_DP_VC_PAYLOAD_ALLOC	(1 << 8)
#define  TRANS_DDI_HDMI_SCRAMBLER_CTS_ENABLE (1 << 7)
#define  TRANS_DDI_HDMI_SCRAMBLER_RESET_FREQ (1 << 6)
#define  TRANS_DDI_HDCP_SELECT		REG_BIT(5)
#define  TRANS_DDI_BFI_ENABLE		(1 << 4)
#define  TRANS_DDI_HIGH_TMDS_CHAR_RATE	(1 << 4)
#define  TRANS_DDI_PORT_WIDTH_MASK	REG_GENMASK(3, 1)
#define  TRANS_DDI_PORT_WIDTH(width)	REG_FIELD_PREP(TRANS_DDI_PORT_WIDTH_MASK, (width) - 1)
#define  TRANS_DDI_HDMI_SCRAMBLING	(1 << 0)
#define  TRANS_DDI_HDMI_SCRAMBLING_MASK (TRANS_DDI_HDMI_SCRAMBLER_CTS_ENABLE \
					| TRANS_DDI_HDMI_SCRAMBLER_RESET_FREQ \
					| TRANS_DDI_HDMI_SCRAMBLING)

#define _TRANS_DDI_FUNC_CTL2_A		0x60404
#define _TRANS_DDI_FUNC_CTL2_B		0x61404
#define _TRANS_DDI_FUNC_CTL2_C		0x62404
#define _TRANS_DDI_FUNC_CTL2_EDP	0x6f404
#define _TRANS_DDI_FUNC_CTL2_DSI0	0x6b404
#define _TRANS_DDI_FUNC_CTL2_DSI1	0x6bc04
#define TRANS_DDI_FUNC_CTL2(tran)	_MMIO_TRANS2(dev_priv, tran, _TRANS_DDI_FUNC_CTL2_A)
#define  PORT_SYNC_MODE_ENABLE			REG_BIT(4)
#define  PORT_SYNC_MODE_MASTER_SELECT_MASK	REG_GENMASK(2, 0)
#define  PORT_SYNC_MODE_MASTER_SELECT(x)	REG_FIELD_PREP(PORT_SYNC_MODE_MASTER_SELECT_MASK, (x))

#define TRANS_CMTG_CHICKEN		_MMIO(0x6fa90)
#define  DISABLE_DPT_CLK_GATING		REG_BIT(1)

/* DisplayPort Transport Control */
#define _DP_TP_CTL_A			0x64040
#define _DP_TP_CTL_B			0x64140
#define _TGL_DP_TP_CTL_A		0x60540
#define DP_TP_CTL(port) _MMIO_PORT(port, _DP_TP_CTL_A, _DP_TP_CTL_B)
#define TGL_DP_TP_CTL(tran) _MMIO_TRANS2(dev_priv, (tran), _TGL_DP_TP_CTL_A)
#define  DP_TP_CTL_ENABLE			(1 << 31)
#define  DP_TP_CTL_FEC_ENABLE			(1 << 30)
#define  DP_TP_CTL_MODE_SST			(0 << 27)
#define  DP_TP_CTL_MODE_MST			(1 << 27)
#define  DP_TP_CTL_FORCE_ACT			(1 << 25)
#define  DP_TP_CTL_TRAIN_PAT4_SEL_MASK		(3 << 19)
#define  DP_TP_CTL_TRAIN_PAT4_SEL_TP4A		(0 << 19)
#define  DP_TP_CTL_TRAIN_PAT4_SEL_TP4B		(1 << 19)
#define  DP_TP_CTL_TRAIN_PAT4_SEL_TP4C		(2 << 19)
#define  DP_TP_CTL_ENHANCED_FRAME_ENABLE	(1 << 18)
#define  DP_TP_CTL_FDI_AUTOTRAIN		(1 << 15)
#define  DP_TP_CTL_LINK_TRAIN_MASK		(7 << 8)
#define  DP_TP_CTL_LINK_TRAIN_PAT1		(0 << 8)
#define  DP_TP_CTL_LINK_TRAIN_PAT2		(1 << 8)
#define  DP_TP_CTL_LINK_TRAIN_PAT3		(4 << 8)
#define  DP_TP_CTL_LINK_TRAIN_PAT4		(5 << 8)
#define  DP_TP_CTL_LINK_TRAIN_IDLE		(2 << 8)
#define  DP_TP_CTL_LINK_TRAIN_NORMAL		(3 << 8)
#define  DP_TP_CTL_SCRAMBLE_DISABLE		(1 << 7)

/* DisplayPort Transport Status */
#define _DP_TP_STATUS_A			0x64044
#define _DP_TP_STATUS_B			0x64144
#define _TGL_DP_TP_STATUS_A		0x60544
#define DP_TP_STATUS(port) _MMIO_PORT(port, _DP_TP_STATUS_A, _DP_TP_STATUS_B)
#define TGL_DP_TP_STATUS(tran) _MMIO_TRANS2(dev_priv, (tran), _TGL_DP_TP_STATUS_A)
#define  DP_TP_STATUS_FEC_ENABLE_LIVE		(1 << 28)
#define  DP_TP_STATUS_IDLE_DONE			(1 << 25)
#define  DP_TP_STATUS_ACT_SENT			(1 << 24)
#define  DP_TP_STATUS_MODE_STATUS_MST		(1 << 23)
#define  DP_TP_STATUS_AUTOTRAIN_DONE		(1 << 12)
#define  DP_TP_STATUS_PAYLOAD_MAPPING_VC2	(3 << 8)
#define  DP_TP_STATUS_PAYLOAD_MAPPING_VC1	(3 << 4)
#define  DP_TP_STATUS_PAYLOAD_MAPPING_VC0	(3 << 0)

/* DDI Buffer Control */
#define _DDI_BUF_CTL_A				0x64000
#define _DDI_BUF_CTL_B				0x64100
/* Known as DDI_CTL_DE in MTL+ */
#define DDI_BUF_CTL(port) _MMIO_PORT(port, _DDI_BUF_CTL_A, _DDI_BUF_CTL_B)
#define  DDI_BUF_CTL_ENABLE			(1 << 31)
#define  XE2LPD_DDI_BUF_D2D_LINK_ENABLE		REG_BIT(29)
#define  XE2LPD_DDI_BUF_D2D_LINK_STATE		REG_BIT(28)
#define  DDI_BUF_TRANS_SELECT(n)	((n) << 24)
#define  DDI_BUF_EMP_MASK			(0xf << 24)
#define  DDI_BUF_PHY_LINK_RATE(r)		((r) << 20)
#define  DDI_BUF_PORT_DATA_MASK			REG_GENMASK(19, 18)
#define  DDI_BUF_PORT_DATA_10BIT		REG_FIELD_PREP(DDI_BUF_PORT_DATA_MASK, 0)
#define  DDI_BUF_PORT_DATA_20BIT		REG_FIELD_PREP(DDI_BUF_PORT_DATA_MASK, 1)
#define  DDI_BUF_PORT_DATA_40BIT		REG_FIELD_PREP(DDI_BUF_PORT_DATA_MASK, 2)
#define  DDI_BUF_PORT_REVERSAL			(1 << 16)
#define  DDI_BUF_IS_IDLE			(1 << 7)
#define  DDI_BUF_CTL_TC_PHY_OWNERSHIP		REG_BIT(6)
#define  DDI_A_4_LANES				(1 << 4)
#define  DDI_PORT_WIDTH(width)			(((width) - 1) << 1)
#define  DDI_PORT_WIDTH_MASK			(7 << 1)
#define  DDI_PORT_WIDTH_SHIFT			1
#define  DDI_INIT_DISPLAY_DETECTED		(1 << 0)

/* DDI Buffer Translations */
#define _DDI_BUF_TRANS_A		0x64E00
#define _DDI_BUF_TRANS_B		0x64E60
#define DDI_BUF_TRANS_LO(port, i)	_MMIO(_PORT(port, _DDI_BUF_TRANS_A, _DDI_BUF_TRANS_B) + (i) * 8)
#define  DDI_BUF_BALANCE_LEG_ENABLE	(1 << 31)
#define DDI_BUF_TRANS_HI(port, i)	_MMIO(_PORT(port, _DDI_BUF_TRANS_A, _DDI_BUF_TRANS_B) + (i) * 8 + 4)

/* DDI DP Compliance Control */
#define _DDI_DP_COMP_CTL_A			0x605F0
#define _DDI_DP_COMP_CTL_B			0x615F0
#define DDI_DP_COMP_CTL(pipe)			_MMIO_PIPE(pipe, _DDI_DP_COMP_CTL_A, _DDI_DP_COMP_CTL_B)
#define   DDI_DP_COMP_CTL_ENABLE		(1 << 31)
#define   DDI_DP_COMP_CTL_D10_2			(0 << 28)
#define   DDI_DP_COMP_CTL_SCRAMBLED_0		(1 << 28)
#define   DDI_DP_COMP_CTL_PRBS7			(2 << 28)
#define   DDI_DP_COMP_CTL_CUSTOM80		(3 << 28)
#define   DDI_DP_COMP_CTL_HBR2			(4 << 28)
#define   DDI_DP_COMP_CTL_SCRAMBLED_1		(5 << 28)
#define   DDI_DP_COMP_CTL_HBR2_RESET		(0xFC << 0)

/* DDI DP Compliance Pattern */
#define _DDI_DP_COMP_PAT_A			0x605F4
#define _DDI_DP_COMP_PAT_B			0x615F4
#define DDI_DP_COMP_PAT(pipe, i)		_MMIO(_PIPE(pipe, _DDI_DP_COMP_PAT_A, _DDI_DP_COMP_PAT_B) + (i) * 4)

/* Sideband Interface (SBI) is programmed indirectly, via
 * SBI_ADDR, which contains the register offset; and SBI_DATA,
 * which contains the payload */
#define SBI_ADDR			_MMIO(0xC6000)
#define SBI_DATA			_MMIO(0xC6004)
#define SBI_CTL_STAT			_MMIO(0xC6008)
#define  SBI_CTL_DEST_ICLK		(0x0 << 16)
#define  SBI_CTL_DEST_MPHY		(0x1 << 16)
#define  SBI_CTL_OP_IORD		(0x2 << 8)
#define  SBI_CTL_OP_IOWR		(0x3 << 8)
#define  SBI_CTL_OP_CRRD		(0x6 << 8)
#define  SBI_CTL_OP_CRWR		(0x7 << 8)
#define  SBI_RESPONSE_FAIL		(0x1 << 1)
#define  SBI_RESPONSE_SUCCESS		(0x0 << 1)
#define  SBI_BUSY			(0x1 << 0)
#define  SBI_READY			(0x0 << 0)

/* SBI offsets */
#define  SBI_SSCDIVINTPHASE			0x0200
#define  SBI_SSCDIVINTPHASE6			0x0600
#define   SBI_SSCDIVINTPHASE_DIVSEL_SHIFT	1
#define   SBI_SSCDIVINTPHASE_DIVSEL_MASK	(0x7f << 1)
#define   SBI_SSCDIVINTPHASE_DIVSEL(x)		((x) << 1)
#define   SBI_SSCDIVINTPHASE_INCVAL_SHIFT	8
#define   SBI_SSCDIVINTPHASE_INCVAL_MASK	(0x7f << 8)
#define   SBI_SSCDIVINTPHASE_INCVAL(x)		((x) << 8)
#define   SBI_SSCDIVINTPHASE_DIR(x)		((x) << 15)
#define   SBI_SSCDIVINTPHASE_PROPAGATE		(1 << 0)
#define  SBI_SSCDITHPHASE			0x0204
#define  SBI_SSCCTL				0x020c
#define  SBI_SSCCTL6				0x060C
#define   SBI_SSCCTL_PATHALT			(1 << 3)
#define   SBI_SSCCTL_DISABLE			(1 << 0)
#define  SBI_SSCAUXDIV6				0x0610
#define   SBI_SSCAUXDIV_FINALDIV2SEL_SHIFT	4
#define   SBI_SSCAUXDIV_FINALDIV2SEL_MASK	(1 << 4)
#define   SBI_SSCAUXDIV_FINALDIV2SEL(x)		((x) << 4)
#define  SBI_DBUFF0				0x2a00
#define  SBI_GEN0				0x1f00
#define   SBI_GEN0_CFG_BUFFENABLE_DISABLE	(1 << 0)

/* LPT PIXCLK_GATE */
#define PIXCLK_GATE			_MMIO(0xC6020)
#define  PIXCLK_GATE_UNGATE		(1 << 0)
#define  PIXCLK_GATE_GATE		(0 << 0)

/* SPLL */
#define SPLL_CTL			_MMIO(0x46020)
#define  SPLL_PLL_ENABLE		(1 << 31)
#define  SPLL_REF_BCLK			(0 << 28)
#define  SPLL_REF_MUXED_SSC		(1 << 28) /* CPU SSC if fused enabled, PCH SSC otherwise */
#define  SPLL_REF_NON_SSC_HSW		(2 << 28)
#define  SPLL_REF_PCH_SSC_BDW		(2 << 28)
#define  SPLL_REF_LCPLL			(3 << 28)
#define  SPLL_REF_MASK			(3 << 28)
#define  SPLL_FREQ_810MHz		(0 << 26)
#define  SPLL_FREQ_1350MHz		(1 << 26)
#define  SPLL_FREQ_2700MHz		(2 << 26)
#define  SPLL_FREQ_MASK			(3 << 26)

/* WRPLL */
#define _WRPLL_CTL1			0x46040
#define _WRPLL_CTL2			0x46060
#define WRPLL_CTL(pll)			_MMIO_PIPE(pll, _WRPLL_CTL1, _WRPLL_CTL2)
#define  WRPLL_PLL_ENABLE		(1 << 31)
#define  WRPLL_REF_BCLK			(0 << 28)
#define  WRPLL_REF_PCH_SSC		(1 << 28)
#define  WRPLL_REF_MUXED_SSC_BDW	(2 << 28) /* CPU SSC if fused enabled, PCH SSC otherwise */
#define  WRPLL_REF_SPECIAL_HSW		(2 << 28) /* muxed SSC (ULT), non-SSC (non-ULT) */
#define  WRPLL_REF_LCPLL		(3 << 28)
#define  WRPLL_REF_MASK			(3 << 28)
/* WRPLL divider programming */
#define  WRPLL_DIVIDER_REFERENCE(x)	((x) << 0)
#define  WRPLL_DIVIDER_REF_MASK		(0xff)
#define  WRPLL_DIVIDER_POST(x)		((x) << 8)
#define  WRPLL_DIVIDER_POST_MASK	(0x3f << 8)
#define  WRPLL_DIVIDER_POST_SHIFT	8
#define  WRPLL_DIVIDER_FEEDBACK(x)	((x) << 16)
#define  WRPLL_DIVIDER_FB_SHIFT		16
#define  WRPLL_DIVIDER_FB_MASK		(0xff << 16)

/* Port clock selection */
#define _PORT_CLK_SEL_A			0x46100
#define _PORT_CLK_SEL_B			0x46104
#define PORT_CLK_SEL(port) _MMIO_PORT(port, _PORT_CLK_SEL_A, _PORT_CLK_SEL_B)
#define  PORT_CLK_SEL_MASK		REG_GENMASK(31, 29)
#define  PORT_CLK_SEL_LCPLL_2700	REG_FIELD_PREP(PORT_CLK_SEL_MASK, 0)
#define  PORT_CLK_SEL_LCPLL_1350	REG_FIELD_PREP(PORT_CLK_SEL_MASK, 1)
#define  PORT_CLK_SEL_LCPLL_810		REG_FIELD_PREP(PORT_CLK_SEL_MASK, 2)
#define  PORT_CLK_SEL_SPLL		REG_FIELD_PREP(PORT_CLK_SEL_MASK, 3)
#define  PORT_CLK_SEL_WRPLL(pll)	REG_FIELD_PREP(PORT_CLK_SEL_MASK, 4 + (pll))
#define  PORT_CLK_SEL_WRPLL1		REG_FIELD_PREP(PORT_CLK_SEL_MASK, 4)
#define  PORT_CLK_SEL_WRPLL2		REG_FIELD_PREP(PORT_CLK_SEL_MASK, 5)
#define  PORT_CLK_SEL_NONE		REG_FIELD_PREP(PORT_CLK_SEL_MASK, 7)

/* On ICL+ this is the same as PORT_CLK_SEL, but all bits change. */
#define DDI_CLK_SEL(port)		PORT_CLK_SEL(port)
#define  DDI_CLK_SEL_MASK		REG_GENMASK(31, 28)
#define  DDI_CLK_SEL_NONE		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0x0)
#define  DDI_CLK_SEL_MG			REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0x8)
#define  DDI_CLK_SEL_TBT_162		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0xC)
#define  DDI_CLK_SEL_TBT_270		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0xD)
#define  DDI_CLK_SEL_TBT_540		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0xE)
#define  DDI_CLK_SEL_TBT_810		REG_FIELD_PREP(DDI_CLK_SEL_MASK, 0xF)

/* Transcoder clock selection */
#define _TRANS_CLK_SEL_A		0x46140
#define _TRANS_CLK_SEL_B		0x46144
#define TRANS_CLK_SEL(tran) _MMIO_TRANS(tran, _TRANS_CLK_SEL_A, _TRANS_CLK_SEL_B)
/* For each transcoder, we need to select the corresponding port clock */
#define  TRANS_CLK_SEL_DISABLED		(0x0 << 29)
#define  TRANS_CLK_SEL_PORT(x)		(((x) + 1) << 29)
#define  TGL_TRANS_CLK_SEL_DISABLED	(0x0 << 28)
#define  TGL_TRANS_CLK_SEL_PORT(x)	(((x) + 1) << 28)


#define CDCLK_FREQ			_MMIO(0x46200)

#define _TRANSA_MSA_MISC		0x60410
#define _TRANSB_MSA_MISC		0x61410
#define _TRANSC_MSA_MISC		0x62410
#define _TRANS_EDP_MSA_MISC		0x6f410
#define TRANS_MSA_MISC(tran) _MMIO_TRANS2(dev_priv, tran, _TRANSA_MSA_MISC)
/* See DP_MSA_MISC_* for the bit definitions */

#define _TRANS_A_SET_CONTEXT_LATENCY		0x6007C
#define _TRANS_B_SET_CONTEXT_LATENCY		0x6107C
#define _TRANS_C_SET_CONTEXT_LATENCY		0x6207C
#define _TRANS_D_SET_CONTEXT_LATENCY		0x6307C
#define TRANS_SET_CONTEXT_LATENCY(tran)		_MMIO_TRANS2(dev_priv, tran, _TRANS_A_SET_CONTEXT_LATENCY)
#define  TRANS_SET_CONTEXT_LATENCY_MASK		REG_GENMASK(15, 0)
#define  TRANS_SET_CONTEXT_LATENCY_VALUE(x)	REG_FIELD_PREP(TRANS_SET_CONTEXT_LATENCY_MASK, (x))

/* LCPLL Control */
#define LCPLL_CTL			_MMIO(0x130040)
#define  LCPLL_PLL_DISABLE		(1 << 31)
#define  LCPLL_PLL_LOCK			(1 << 30)
#define  LCPLL_REF_NON_SSC		(0 << 28)
#define  LCPLL_REF_BCLK			(2 << 28)
#define  LCPLL_REF_PCH_SSC		(3 << 28)
#define  LCPLL_REF_MASK			(3 << 28)
#define  LCPLL_CLK_FREQ_MASK		(3 << 26)
#define  LCPLL_CLK_FREQ_450		(0 << 26)
#define  LCPLL_CLK_FREQ_54O_BDW		(1 << 26)
#define  LCPLL_CLK_FREQ_337_5_BDW	(2 << 26)
#define  LCPLL_CLK_FREQ_675_BDW		(3 << 26)
#define  LCPLL_CD_CLOCK_DISABLE		(1 << 25)
#define  LCPLL_ROOT_CD_CLOCK_DISABLE	(1 << 24)
#define  LCPLL_CD2X_CLOCK_DISABLE	(1 << 23)
#define  LCPLL_POWER_DOWN_ALLOW		(1 << 22)
#define  LCPLL_CD_SOURCE_FCLK		(1 << 21)
#define  LCPLL_CD_SOURCE_FCLK_DONE	(1 << 19)

/*
 * SKL Clocks
 */

/* CDCLK_CTL */
#define CDCLK_CTL			_MMIO(0x46000)
#define  CDCLK_FREQ_SEL_MASK		REG_GENMASK(27, 26)
#define  CDCLK_FREQ_450_432		REG_FIELD_PREP(CDCLK_FREQ_SEL_MASK, 0)
#define  CDCLK_FREQ_540		REG_FIELD_PREP(CDCLK_FREQ_SEL_MASK, 1)
#define  CDCLK_FREQ_337_308		REG_FIELD_PREP(CDCLK_FREQ_SEL_MASK, 2)
#define  CDCLK_FREQ_675_617		REG_FIELD_PREP(CDCLK_FREQ_SEL_MASK, 3)
#define  MDCLK_SOURCE_SEL_MASK		REG_GENMASK(25, 25)
#define  MDCLK_SOURCE_SEL_CD2XCLK	REG_FIELD_PREP(MDCLK_SOURCE_SEL_MASK, 0)
#define  MDCLK_SOURCE_SEL_CDCLK_PLL	REG_FIELD_PREP(MDCLK_SOURCE_SEL_MASK, 1)
#define  BXT_CDCLK_CD2X_DIV_SEL_MASK	REG_GENMASK(23, 22)
#define  BXT_CDCLK_CD2X_DIV_SEL_1	REG_FIELD_PREP(BXT_CDCLK_CD2X_DIV_SEL_MASK, 0)
#define  BXT_CDCLK_CD2X_DIV_SEL_1_5	REG_FIELD_PREP(BXT_CDCLK_CD2X_DIV_SEL_MASK, 1)
#define  BXT_CDCLK_CD2X_DIV_SEL_2	REG_FIELD_PREP(BXT_CDCLK_CD2X_DIV_SEL_MASK, 2)
#define  BXT_CDCLK_CD2X_DIV_SEL_4	REG_FIELD_PREP(BXT_CDCLK_CD2X_DIV_SEL_MASK, 3)
#define  BXT_CDCLK_CD2X_PIPE(pipe)	((pipe) << 20)
#define  CDCLK_DIVMUX_CD_OVERRIDE	(1 << 19)
#define  BXT_CDCLK_CD2X_PIPE_NONE	BXT_CDCLK_CD2X_PIPE(3)
#define  ICL_CDCLK_CD2X_PIPE(pipe)	(_PICK(pipe, 0, 2, 6) << 19)
#define  ICL_CDCLK_CD2X_PIPE_NONE	(7 << 19)
#define  TGL_CDCLK_CD2X_PIPE(pipe)	BXT_CDCLK_CD2X_PIPE(pipe)
#define  TGL_CDCLK_CD2X_PIPE_NONE	ICL_CDCLK_CD2X_PIPE_NONE
#define  BXT_CDCLK_SSA_PRECHARGE_ENABLE	(1 << 16)
#define  CDCLK_FREQ_DECIMAL_MASK	(0x7ff)

/* CDCLK_SQUASH_CTL */
#define CDCLK_SQUASH_CTL		_MMIO(0x46008)
#define  CDCLK_SQUASH_ENABLE		REG_BIT(31)
#define  CDCLK_SQUASH_WINDOW_SIZE_MASK	REG_GENMASK(27, 24)
#define  CDCLK_SQUASH_WINDOW_SIZE(x)	REG_FIELD_PREP(CDCLK_SQUASH_WINDOW_SIZE_MASK, (x))
#define  CDCLK_SQUASH_WAVEFORM_MASK	REG_GENMASK(15, 0)
#define  CDCLK_SQUASH_WAVEFORM(x)	REG_FIELD_PREP(CDCLK_SQUASH_WAVEFORM_MASK, (x))

/* LCPLL_CTL */
#define LCPLL1_CTL		_MMIO(0x46010)
#define LCPLL2_CTL		_MMIO(0x46014)
#define  LCPLL_PLL_ENABLE	(1 << 31)

/* DPLL control1 */
#define DPLL_CTRL1		_MMIO(0x6C058)
#define  DPLL_CTRL1_HDMI_MODE(id)		(1 << ((id) * 6 + 5))
#define  DPLL_CTRL1_SSC(id)			(1 << ((id) * 6 + 4))
#define  DPLL_CTRL1_LINK_RATE_MASK(id)		(7 << ((id) * 6 + 1))
#define  DPLL_CTRL1_LINK_RATE_SHIFT(id)		((id) * 6 + 1)
#define  DPLL_CTRL1_LINK_RATE(linkrate, id)	((linkrate) << ((id) * 6 + 1))
#define  DPLL_CTRL1_OVERRIDE(id)		(1 << ((id) * 6))
#define  DPLL_CTRL1_LINK_RATE_2700		0
#define  DPLL_CTRL1_LINK_RATE_1350		1
#define  DPLL_CTRL1_LINK_RATE_810		2
#define  DPLL_CTRL1_LINK_RATE_1620		3
#define  DPLL_CTRL1_LINK_RATE_1080		4
#define  DPLL_CTRL1_LINK_RATE_2160		5

/* DPLL control2 */
#define DPLL_CTRL2				_MMIO(0x6C05C)
#define  DPLL_CTRL2_DDI_CLK_OFF(port)		(1 << ((port) + 15))
#define  DPLL_CTRL2_DDI_CLK_SEL_MASK(port)	(3 << ((port) * 3 + 1))
#define  DPLL_CTRL2_DDI_CLK_SEL_SHIFT(port)    ((port) * 3 + 1)
#define  DPLL_CTRL2_DDI_CLK_SEL(clk, port)	((clk) << ((port) * 3 + 1))
#define  DPLL_CTRL2_DDI_SEL_OVERRIDE(port)     (1 << ((port) * 3))

/* DPLL Status */
#define DPLL_STATUS	_MMIO(0x6C060)
#define  DPLL_LOCK(id) (1 << ((id) * 8))

/* DPLL cfg */
#define _DPLL1_CFGCR1	0x6C040
#define _DPLL2_CFGCR1	0x6C048
#define _DPLL3_CFGCR1	0x6C050
#define  DPLL_CFGCR1_FREQ_ENABLE	(1 << 31)
#define  DPLL_CFGCR1_DCO_FRACTION_MASK	(0x7fff << 9)
#define  DPLL_CFGCR1_DCO_FRACTION(x)	((x) << 9)
#define  DPLL_CFGCR1_DCO_INTEGER_MASK	(0x1ff)

#define _DPLL1_CFGCR2	0x6C044
#define _DPLL2_CFGCR2	0x6C04C
#define _DPLL3_CFGCR2	0x6C054
#define  DPLL_CFGCR2_QDIV_RATIO_MASK	(0xff << 8)
#define  DPLL_CFGCR2_QDIV_RATIO(x)	((x) << 8)
#define  DPLL_CFGCR2_QDIV_MODE(x)	((x) << 7)
#define  DPLL_CFGCR2_KDIV_MASK		(3 << 5)
#define  DPLL_CFGCR2_KDIV(x)		((x) << 5)
#define  DPLL_CFGCR2_KDIV_5 (0 << 5)
#define  DPLL_CFGCR2_KDIV_2 (1 << 5)
#define  DPLL_CFGCR2_KDIV_3 (2 << 5)
#define  DPLL_CFGCR2_KDIV_1 (3 << 5)
#define  DPLL_CFGCR2_PDIV_MASK		(7 << 2)
#define  DPLL_CFGCR2_PDIV(x)		((x) << 2)
#define  DPLL_CFGCR2_PDIV_1 (0 << 2)
#define  DPLL_CFGCR2_PDIV_2 (1 << 2)
#define  DPLL_CFGCR2_PDIV_3 (2 << 2)
#define  DPLL_CFGCR2_PDIV_7 (4 << 2)
#define  DPLL_CFGCR2_PDIV_7_INVALID	(5 << 2)
#define  DPLL_CFGCR2_CENTRAL_FREQ_MASK	(3)

#define DPLL_CFGCR1(id)	_MMIO_PIPE((id) - SKL_DPLL1, _DPLL1_CFGCR1, _DPLL2_CFGCR1)
#define DPLL_CFGCR2(id)	_MMIO_PIPE((id) - SKL_DPLL1, _DPLL1_CFGCR2, _DPLL2_CFGCR2)

/* ICL Clocks */
#define ICL_DPCLKA_CFGCR0			_MMIO(0x164280)
#define  ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy)	(1 << _PICK(phy, 10, 11, 24, 4, 5))
#define  RKL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy)	REG_BIT((phy) + 10)
#define  ICL_DPCLKA_CFGCR0_TC_CLK_OFF(tc_port)	(1 << ((tc_port) < TC_PORT_4 ? \
						       (tc_port) + 12 : \
						       (tc_port) - TC_PORT_4 + 21))
#define  ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy)	((phy) * 2)
#define  ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy)	(3 << ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy))
#define  ICL_DPCLKA_CFGCR0_DDI_CLK_SEL(pll, phy)	((pll) << ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy))
#define  RKL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy)	_PICK(phy, 0, 2, 4, 27)
#define  RKL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy) \
	(3 << RKL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy))
#define  RKL_DPCLKA_CFGCR0_DDI_CLK_SEL(pll, phy) \
	((pll) << RKL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy))

/*
 * DG1 Clocks
 * First registers controls the first A and B, while the second register
 * controls the phy C and D. The bits on these registers are the
 * same, but refer to different phys
 */
#define _DG1_DPCLKA_CFGCR0				0x164280
#define _DG1_DPCLKA1_CFGCR0				0x16C280
#define _DG1_DPCLKA_PHY_IDX(phy)			((phy) % 2)
#define _DG1_DPCLKA_PLL_IDX(pll)			((pll) % 2)
#define DG1_DPCLKA_CFGCR0(phy)				_MMIO_PHY((phy) / 2, \
								  _DG1_DPCLKA_CFGCR0, \
								  _DG1_DPCLKA1_CFGCR0)
#define   DG1_DPCLKA_CFGCR0_DDI_CLK_OFF(phy)		REG_BIT(_DG1_DPCLKA_PHY_IDX(phy) + 10)
#define   DG1_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy)	(_DG1_DPCLKA_PHY_IDX(phy) * 2)
#define   DG1_DPCLKA_CFGCR0_DDI_CLK_SEL(pll, phy)	(_DG1_DPCLKA_PLL_IDX(pll) << DG1_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy))
#define   DG1_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy)	(0x3 << DG1_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy))

/* ADLS Clocks */
#define _ADLS_DPCLKA_CFGCR0			0x164280
#define _ADLS_DPCLKA_CFGCR1			0x1642BC
#define ADLS_DPCLKA_CFGCR(phy)			_MMIO_PHY((phy) / 3, \
							  _ADLS_DPCLKA_CFGCR0, \
							  _ADLS_DPCLKA_CFGCR1)
#define  ADLS_DPCLKA_CFGCR_DDI_SHIFT(phy)		(((phy) % 3) * 2)
/* ADLS DPCLKA_CFGCR0 DDI mask */
#define  ADLS_DPCLKA_DDII_SEL_MASK			REG_GENMASK(5, 4)
#define  ADLS_DPCLKA_DDIB_SEL_MASK			REG_GENMASK(3, 2)
#define  ADLS_DPCLKA_DDIA_SEL_MASK			REG_GENMASK(1, 0)
/* ADLS DPCLKA_CFGCR1 DDI mask */
#define  ADLS_DPCLKA_DDIK_SEL_MASK			REG_GENMASK(3, 2)
#define  ADLS_DPCLKA_DDIJ_SEL_MASK			REG_GENMASK(1, 0)
#define  ADLS_DPCLKA_CFGCR_DDI_CLK_SEL_MASK(phy)	_PICK((phy), \
							ADLS_DPCLKA_DDIA_SEL_MASK, \
							ADLS_DPCLKA_DDIB_SEL_MASK, \
							ADLS_DPCLKA_DDII_SEL_MASK, \
							ADLS_DPCLKA_DDIJ_SEL_MASK, \
							ADLS_DPCLKA_DDIK_SEL_MASK)

/* ICL PLL */
#define _DPLL0_ENABLE		0x46010
#define _DPLL1_ENABLE		0x46014
#define _ADLS_DPLL2_ENABLE	0x46018
#define _ADLS_DPLL3_ENABLE	0x46030
#define   PLL_ENABLE		REG_BIT(31)
#define   PLL_LOCK		REG_BIT(30)
#define   PLL_POWER_ENABLE	REG_BIT(27)
#define   PLL_POWER_STATE	REG_BIT(26)
#define ICL_DPLL_ENABLE(pll)	_MMIO(_PICK_EVEN_2RANGES(pll, 3,			\
							_DPLL0_ENABLE, _DPLL1_ENABLE,	\
							_ADLS_DPLL3_ENABLE, _ADLS_DPLL3_ENABLE))

#define _DG2_PLL3_ENABLE	0x4601C

#define DG2_PLL_ENABLE(pll)	_MMIO(_PICK_EVEN_2RANGES(pll, 3,			\
							_DPLL0_ENABLE, _DPLL1_ENABLE,	\
							_DG2_PLL3_ENABLE, _DG2_PLL3_ENABLE))

#define TBT_PLL_ENABLE		_MMIO(0x46020)

#define _MG_PLL1_ENABLE		0x46030
#define _MG_PLL2_ENABLE		0x46034
#define _MG_PLL3_ENABLE		0x46038
#define _MG_PLL4_ENABLE		0x4603C
/* Bits are the same as _DPLL0_ENABLE */
#define MG_PLL_ENABLE(tc_port)	_MMIO_PORT((tc_port), _MG_PLL1_ENABLE, \
					   _MG_PLL2_ENABLE)

/* DG1 PLL */
#define DG1_DPLL_ENABLE(pll)    _MMIO(_PICK_EVEN_2RANGES(pll, 2,			\
							_DPLL0_ENABLE, _DPLL1_ENABLE,	\
							_MG_PLL1_ENABLE, _MG_PLL2_ENABLE))

/* ADL-P Type C PLL */
#define PORTTC1_PLL_ENABLE	0x46038
#define PORTTC2_PLL_ENABLE	0x46040

#define ADLP_PORTTC_PLL_ENABLE(tc_port)		_MMIO_PORT((tc_port), \
							    PORTTC1_PLL_ENABLE, \
							    PORTTC2_PLL_ENABLE)

#define _ICL_DPLL0_CFGCR0		0x164000
#define _ICL_DPLL1_CFGCR0		0x164080
#define ICL_DPLL_CFGCR0(pll)		_MMIO_PLL(pll, _ICL_DPLL0_CFGCR0, \
						  _ICL_DPLL1_CFGCR0)
#define   DPLL_CFGCR0_HDMI_MODE		(1 << 30)
#define   DPLL_CFGCR0_SSC_ENABLE	(1 << 29)
#define   DPLL_CFGCR0_SSC_ENABLE_ICL	(1 << 25)
#define   DPLL_CFGCR0_LINK_RATE_MASK	(0xf << 25)
#define   DPLL_CFGCR0_LINK_RATE_2700	(0 << 25)
#define   DPLL_CFGCR0_LINK_RATE_1350	(1 << 25)
#define   DPLL_CFGCR0_LINK_RATE_810	(2 << 25)
#define   DPLL_CFGCR0_LINK_RATE_1620	(3 << 25)
#define   DPLL_CFGCR0_LINK_RATE_1080	(4 << 25)
#define   DPLL_CFGCR0_LINK_RATE_2160	(5 << 25)
#define   DPLL_CFGCR0_LINK_RATE_3240	(6 << 25)
#define   DPLL_CFGCR0_LINK_RATE_4050	(7 << 25)
#define   DPLL_CFGCR0_DCO_FRACTION_MASK	(0x7fff << 10)
#define   DPLL_CFGCR0_DCO_FRACTION_SHIFT	(10)
#define   DPLL_CFGCR0_DCO_FRACTION(x)	((x) << 10)
#define   DPLL_CFGCR0_DCO_INTEGER_MASK	(0x3ff)

#define _ICL_DPLL0_CFGCR1		0x164004
#define _ICL_DPLL1_CFGCR1		0x164084
#define ICL_DPLL_CFGCR1(pll)		_MMIO_PLL(pll, _ICL_DPLL0_CFGCR1, \
						  _ICL_DPLL1_CFGCR1)
#define   DPLL_CFGCR1_QDIV_RATIO_MASK	(0xff << 10)
#define   DPLL_CFGCR1_QDIV_RATIO_SHIFT	(10)
#define   DPLL_CFGCR1_QDIV_RATIO(x)	((x) << 10)
#define   DPLL_CFGCR1_QDIV_MODE_SHIFT	(9)
#define   DPLL_CFGCR1_QDIV_MODE(x)	((x) << 9)
#define   DPLL_CFGCR1_KDIV_MASK		(7 << 6)
#define   DPLL_CFGCR1_KDIV_SHIFT		(6)
#define   DPLL_CFGCR1_KDIV(x)		((x) << 6)
#define   DPLL_CFGCR1_KDIV_1		(1 << 6)
#define   DPLL_CFGCR1_KDIV_2		(2 << 6)
#define   DPLL_CFGCR1_KDIV_3		(4 << 6)
#define   DPLL_CFGCR1_PDIV_MASK		(0xf << 2)
#define   DPLL_CFGCR1_PDIV_SHIFT		(2)
#define   DPLL_CFGCR1_PDIV(x)		((x) << 2)
#define   DPLL_CFGCR1_PDIV_2		(1 << 2)
#define   DPLL_CFGCR1_PDIV_3		(2 << 2)
#define   DPLL_CFGCR1_PDIV_5		(4 << 2)
#define   DPLL_CFGCR1_PDIV_7		(8 << 2)
#define   DPLL_CFGCR1_CENTRAL_FREQ	(3 << 0)
#define   DPLL_CFGCR1_CENTRAL_FREQ_8400	(3 << 0)
#define   TGL_DPLL_CFGCR1_CFSELOVRD_NORMAL_XTAL	(0 << 0)

#define _TGL_DPLL0_CFGCR0		0x164284
#define _TGL_DPLL1_CFGCR0		0x16428C
#define _TGL_TBTPLL_CFGCR0		0x16429C
#define TGL_DPLL_CFGCR0(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR0, _TGL_DPLL1_CFGCR0,	\
					      _TGL_TBTPLL_CFGCR0, _TGL_TBTPLL_CFGCR0))
#define RKL_DPLL_CFGCR0(pll)		_MMIO_PLL(pll, _TGL_DPLL0_CFGCR0, \
						  _TGL_DPLL1_CFGCR0)

#define _TGL_DPLL0_DIV0					0x164B00
#define _TGL_DPLL1_DIV0					0x164C00
#define TGL_DPLL0_DIV0(pll)				_MMIO_PLL(pll, _TGL_DPLL0_DIV0, _TGL_DPLL1_DIV0)
#define   TGL_DPLL0_DIV0_AFC_STARTUP_MASK		REG_GENMASK(27, 25)
#define   TGL_DPLL0_DIV0_AFC_STARTUP(val)		REG_FIELD_PREP(TGL_DPLL0_DIV0_AFC_STARTUP_MASK, (val))

#define _TGL_DPLL0_CFGCR1		0x164288
#define _TGL_DPLL1_CFGCR1		0x164290
#define _TGL_TBTPLL_CFGCR1		0x1642A0
#define TGL_DPLL_CFGCR1(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR1, _TGL_DPLL1_CFGCR1,	\
					      _TGL_TBTPLL_CFGCR1, _TGL_TBTPLL_CFGCR1))
#define RKL_DPLL_CFGCR1(pll)		_MMIO_PLL(pll, _TGL_DPLL0_CFGCR1, \
						  _TGL_DPLL1_CFGCR1)

#define _DG1_DPLL2_CFGCR0		0x16C284
#define _DG1_DPLL3_CFGCR0		0x16C28C
#define DG1_DPLL_CFGCR0(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR0, _TGL_DPLL1_CFGCR0,	\
					      _DG1_DPLL2_CFGCR0, _DG1_DPLL3_CFGCR0))

#define _DG1_DPLL2_CFGCR1               0x16C288
#define _DG1_DPLL3_CFGCR1               0x16C290
#define DG1_DPLL_CFGCR1(pll)            _MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR1, _TGL_DPLL1_CFGCR1,	\
					      _DG1_DPLL2_CFGCR1, _DG1_DPLL3_CFGCR1))

/* For ADL-S DPLL4_CFGCR0/1 are used to control DPLL2 */
#define _ADLS_DPLL4_CFGCR0		0x164294
#define _ADLS_DPLL3_CFGCR0		0x1642C0
#define ADLS_DPLL_CFGCR0(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR0, _TGL_DPLL1_CFGCR0,	\
					      _ADLS_DPLL4_CFGCR0, _ADLS_DPLL3_CFGCR0))

#define _ADLS_DPLL4_CFGCR1		0x164298
#define _ADLS_DPLL3_CFGCR1		0x1642C4
#define ADLS_DPLL_CFGCR1(pll)		_MMIO(_PICK_EVEN_2RANGES(pll, 2,		\
					      _TGL_DPLL0_CFGCR1, _TGL_DPLL1_CFGCR1,	\
					      _ADLS_DPLL4_CFGCR1, _ADLS_DPLL3_CFGCR1))

/* BXT display engine PLL */
#define BXT_DE_PLL_CTL			_MMIO(0x6d000)
#define   BXT_DE_PLL_RATIO(x)		(x)	/* {60,65,100} * 19.2MHz */
#define   BXT_DE_PLL_RATIO_MASK		0xff

#define BXT_DE_PLL_ENABLE		_MMIO(0x46070)
#define   BXT_DE_PLL_PLL_ENABLE		(1 << 31)
#define   BXT_DE_PLL_LOCK		(1 << 30)
#define   BXT_DE_PLL_FREQ_REQ		(1 << 23)
#define   BXT_DE_PLL_FREQ_REQ_ACK	(1 << 22)
#define   ICL_CDCLK_PLL_RATIO(x)	(x)
#define   ICL_CDCLK_PLL_RATIO_MASK	0xff

/* GEN9 DC */
#define DC_STATE_EN			_MMIO(0x45504)
#define  DC_STATE_DISABLE		0
#define  DC_STATE_EN_DC3CO		REG_BIT(30)
#define  DC_STATE_DC3CO_STATUS		REG_BIT(29)
#define  HOLD_PHY_CLKREQ_PG1_LATCH	REG_BIT(21)
#define  HOLD_PHY_PG1_LATCH		REG_BIT(20)
#define  DC_STATE_EN_UPTO_DC5		(1 << 0)
#define  DC_STATE_EN_DC9		(1 << 3)
#define  DC_STATE_EN_UPTO_DC6		(2 << 0)
#define  DC_STATE_EN_UPTO_DC5_DC6_MASK   0x3

#define  DC_STATE_DEBUG                  _MMIO(0x45520)
#define  DC_STATE_DEBUG_MASK_CORES	(1 << 0)
#define  DC_STATE_DEBUG_MASK_MEMORY_UP	(1 << 1)

#define D_COMP_BDW			_MMIO(0x138144)

/* Pipe WM_LINETIME - watermark line time */
#define _WM_LINETIME_A		0x45270
#define _WM_LINETIME_B		0x45274
#define WM_LINETIME(pipe) _MMIO_PIPE(pipe, _WM_LINETIME_A, _WM_LINETIME_B)
#define  HSW_LINETIME_MASK	REG_GENMASK(8, 0)
#define  HSW_LINETIME(x)	REG_FIELD_PREP(HSW_LINETIME_MASK, (x))
#define  HSW_IPS_LINETIME_MASK	REG_GENMASK(24, 16)
#define  HSW_IPS_LINETIME(x)	REG_FIELD_PREP(HSW_IPS_LINETIME_MASK, (x))

/* SFUSE_STRAP */
#define SFUSE_STRAP			_MMIO(0xc2014)
#define  SFUSE_STRAP_FUSE_LOCK		(1 << 13)
#define  SFUSE_STRAP_RAW_FREQUENCY	(1 << 8)
#define  SFUSE_STRAP_DISPLAY_DISABLED	(1 << 7)
#define  SFUSE_STRAP_CRT_DISABLED	(1 << 6)
#define  SFUSE_STRAP_DDIF_DETECTED	(1 << 3)
#define  SFUSE_STRAP_DDIB_DETECTED	(1 << 2)
#define  SFUSE_STRAP_DDIC_DETECTED	(1 << 1)
#define  SFUSE_STRAP_DDID_DETECTED	(1 << 0)

#define WM_MISC				_MMIO(0x45260)
#define  WM_MISC_DATA_PARTITION_5_6	(1 << 0)

#define WM_DBG				_MMIO(0x45280)
#define  WM_DBG_DISALLOW_MULTIPLE_LP	(1 << 0)
#define  WM_DBG_DISALLOW_MAXFIFO	(1 << 1)
#define  WM_DBG_DISALLOW_SPRITE		(1 << 2)

/* Gen4+ Timestamp and Pipe Frame time stamp registers */
#define GEN4_TIMESTAMP		_MMIO(0x2358)
#define ILK_TIMESTAMP_HI	_MMIO(0x70070)
#define IVB_TIMESTAMP_CTR	_MMIO(0x44070)

#define GEN9_TIMESTAMP_OVERRIDE				_MMIO(0x44074)
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_SHIFT	0
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_MASK	0x3ff
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_SHIFT	12
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_MASK	(0xf << 12)

/* g4x+, except vlv/chv! */
#define _PIPE_FRMTMSTMP_A		0x70048
#define _PIPE_FRMTMSTMP_B		0x71048
#define PIPE_FRMTMSTMP(pipe)		\
	_MMIO_PIPE(pipe, _PIPE_FRMTMSTMP_A, _PIPE_FRMTMSTMP_B)

/* g4x+, except vlv/chv! */
#define _PIPE_FLIPTMSTMP_A		0x7004C
#define _PIPE_FLIPTMSTMP_B		0x7104C
#define PIPE_FLIPTMSTMP(pipe)		\
	_MMIO_PIPE(pipe, _PIPE_FLIPTMSTMP_A, _PIPE_FLIPTMSTMP_B)

/* tgl+ */
#define _PIPE_FLIPDONETMSTMP_A		0x70054
#define _PIPE_FLIPDONETMSTMP_B		0x71054
#define PIPE_FLIPDONETIMSTMP(pipe)	\
	_MMIO_PIPE(pipe, _PIPE_FLIPDONETMSTMP_A, _PIPE_FLIPDONETMSTMP_B)

#define _VLV_PIPE_MSA_MISC_A			0x70048
#define VLV_PIPE_MSA_MISC(pipe)		\
			_MMIO_PIPE2(dev_priv, pipe, _VLV_PIPE_MSA_MISC_A)
#define   VLV_MSA_MISC1_HW_ENABLE			REG_BIT(31)
#define   VLV_MSA_MISC1_SW_S3D_MASK			REG_GENMASK(2, 0) /* MSA MISC1 3:1 */

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

#define _ICL_PHY_MISC_A		0x64C00
#define _ICL_PHY_MISC_B		0x64C04
#define _DG2_PHY_MISC_TC1	0x64C14 /* TC1="PHY E" but offset as if "PHY F" */
#define ICL_PHY_MISC(port)	_MMIO_PORT(port, _ICL_PHY_MISC_A, _ICL_PHY_MISC_B)
#define DG2_PHY_MISC(port)	((port) == PHY_E ? _MMIO(_DG2_PHY_MISC_TC1) : \
				 ICL_PHY_MISC(port))
#define  ICL_PHY_MISC_MUX_DDID			(1 << 28)
#define  ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN	(1 << 23)
#define  DG2_PHY_DP_TX_ACK_MASK			REG_GENMASK(23, 20)

#define PORT_TX_DFLEXDPSP(fia)			_MMIO_FIA((fia), 0x008A0)
#define   MODULAR_FIA_MASK			(1 << 4)
#define   TC_LIVE_STATE_TBT(idx)		(1 << ((idx) * 8 + 6))
#define   TC_LIVE_STATE_TC(idx)			(1 << ((idx) * 8 + 5))
#define   DP_LANE_ASSIGNMENT_SHIFT(idx)		((idx) * 8)
#define   DP_LANE_ASSIGNMENT_MASK(idx)		(0xf << ((idx) * 8))
#define   DP_LANE_ASSIGNMENT(idx, x)		((x) << ((idx) * 8))

#define PORT_TX_DFLEXDPPMS(fia)			_MMIO_FIA((fia), 0x00890)
#define   DP_PHY_MODE_STATUS_COMPLETED(idx)	(1 << (idx))

#define PORT_TX_DFLEXDPCSSS(fia)		_MMIO_FIA((fia), 0x00894)
#define   DP_PHY_MODE_STATUS_NOT_SAFE(idx)	(1 << (idx))

#define PORT_TX_DFLEXPA1(fia)			_MMIO_FIA((fia), 0x00880)
#define   DP_PIN_ASSIGNMENT_SHIFT(idx)		((idx) * 4)
#define   DP_PIN_ASSIGNMENT_MASK(idx)		(0xf << ((idx) * 4))
#define   DP_PIN_ASSIGNMENT(idx, x)		((x) << ((idx) * 4))

#define _TCSS_DDI_STATUS_1			0x161500
#define _TCSS_DDI_STATUS_2			0x161504
#define TCSS_DDI_STATUS(tc)			_MMIO(_PICK_EVEN(tc, \
								 _TCSS_DDI_STATUS_1, \
								 _TCSS_DDI_STATUS_2))
#define  TCSS_DDI_STATUS_PIN_ASSIGNMENT_MASK	REG_GENMASK(28, 25)
#define  TCSS_DDI_STATUS_READY			REG_BIT(2)
#define  TCSS_DDI_STATUS_HPD_LIVE_STATUS_TBT	REG_BIT(1)
#define  TCSS_DDI_STATUS_HPD_LIVE_STATUS_ALT	REG_BIT(0)

#define PRIMARY_SPI_TRIGGER			_MMIO(0x102040)
#define PRIMARY_SPI_ADDRESS			_MMIO(0x102080)
#define PRIMARY_SPI_REGIONID			_MMIO(0x102084)
#define SPI_STATIC_REGIONS			_MMIO(0x102090)
#define   OPTIONROM_SPI_REGIONID_MASK		REG_GENMASK(7, 0)
#define OROM_OFFSET				_MMIO(0x1020c0)
#define   OROM_OFFSET_MASK			REG_GENMASK(20, 16)

#define CLKREQ_POLICY			_MMIO(0x101038)
#define  CLKREQ_POLICY_MEM_UP_OVRD	REG_BIT(1)

#define CLKGATE_DIS_MISC			_MMIO(0x46534)
#define  CLKGATE_DIS_MISC_DMASC_GATING_DIS	REG_BIT(21)

#define _MTL_CLKGATE_DIS_TRANS_A			0x604E8
#define _MTL_CLKGATE_DIS_TRANS_B			0x614E8
#define MTL_CLKGATE_DIS_TRANS(trans)			_MMIO_TRANS2(dev_priv, trans, _MTL_CLKGATE_DIS_TRANS_A)
#define  MTL_CLKGATE_DIS_TRANS_DMASC_GATING_DIS		REG_BIT(7)

#define MTL_MEM_SS_INFO_GLOBAL			_MMIO(0x45700)
#define   MTL_N_OF_ENABLED_QGV_POINTS_MASK	REG_GENMASK(11, 8)
#define   MTL_N_OF_POPULATED_CH_MASK		REG_GENMASK(7, 4)
#define   MTL_DDR_TYPE_MASK			REG_GENMASK(3, 0)

#define MTL_MEM_SS_INFO_QGV_POINT_OFFSET	0x45710
#define MTL_MEM_SS_INFO_QGV_POINT_LOW(point)	_MMIO(MTL_MEM_SS_INFO_QGV_POINT_OFFSET + (point) * 8)
#define   MTL_TRCD_MASK			REG_GENMASK(31, 24)
#define   MTL_TRP_MASK			REG_GENMASK(23, 16)
#define   MTL_DCLK_MASK			REG_GENMASK(15, 0)

#define MTL_MEM_SS_INFO_QGV_POINT_HIGH(point)	_MMIO(MTL_MEM_SS_INFO_QGV_POINT_OFFSET + (point) * 8 + 4)
#define   MTL_TRAS_MASK			REG_GENMASK(16, 8)
#define   MTL_TRDPRE_MASK		REG_GENMASK(7, 0)

#define MTL_MEDIA_GSI_BASE		0x380000

#endif /* _I915_REG_H_ */
