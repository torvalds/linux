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

#define VLV_MIPI_BASE			VLV_DISPLAY_BASE
#define BXT_MIPI_BASE			0x60000

#define DISPLAY_MMIO_BASE(dev_priv)	(INTEL_INFO(dev_priv)->display_mmio_offset)

/*
 * Given the first two numbers __a and __b of arbitrarily many evenly spaced
 * numbers, pick the 0-based __index'th value.
 *
 * Always prefer this over _PICK() if the numbers are evenly spaced.
 */
#define _PICK_EVEN(__index, __a, __b) ((__a) + (__index) * ((__b) - (__a)))

/*
 * Given the arbitrary numbers in varargs, pick the 0-based __index'th number.
 *
 * Always prefer _PICK_EVEN() over this if the numbers are evenly spaced.
 */
#define _PICK(__index, ...) (((const u32 []){ __VA_ARGS__ })[__index])

/*
 * Named helper wrappers around _PICK_EVEN() and _PICK().
 */
#define _PIPE(pipe, a, b)		_PICK_EVEN(pipe, a, b)
#define _PLANE(plane, a, b)		_PICK_EVEN(plane, a, b)
#define _TRANS(tran, a, b)		_PICK_EVEN(tran, a, b)
#define _PORT(port, a, b)		_PICK_EVEN(port, a, b)
#define _PLL(pll, a, b)			_PICK_EVEN(pll, a, b)
#define _PHY(phy, a, b)			_PICK_EVEN(phy, a, b)

#define _MMIO_PIPE(pipe, a, b)		_MMIO(_PIPE(pipe, a, b))
#define _MMIO_PLANE(plane, a, b)	_MMIO(_PLANE(plane, a, b))
#define _MMIO_TRANS(tran, a, b)		_MMIO(_TRANS(tran, a, b))
#define _MMIO_PORT(port, a, b)		_MMIO(_PORT(port, a, b))
#define _MMIO_PLL(pll, a, b)		_MMIO(_PLL(pll, a, b))
#define _MMIO_PHY(phy, a, b)		_MMIO(_PHY(phy, a, b))

#define _PHY3(phy, ...)			_PICK(phy, __VA_ARGS__)

#define _MMIO_PIPE3(pipe, a, b, c)	_MMIO(_PICK(pipe, a, b, c))
#define _MMIO_PORT3(pipe, a, b, c)	_MMIO(_PICK(pipe, a, b, c))
#define _MMIO_PHY3(phy, a, b, c)	_MMIO(_PHY3(phy, a, b, c))
#define _MMIO_PLL3(pll, ...)		_MMIO(_PICK(pll, __VA_ARGS__))


/*
 * Device info offset array based helpers for groups of registers with unevenly
 * spaced base offsets.
 */
#define _MMIO_PIPE2(pipe, reg)		_MMIO(INTEL_INFO(dev_priv)->pipe_offsets[pipe] - \
					      INTEL_INFO(dev_priv)->pipe_offsets[PIPE_A] + (reg) + \
					      DISPLAY_MMIO_BASE(dev_priv))
#define _TRANS2(tran, reg)		(INTEL_INFO(dev_priv)->trans_offsets[(tran)] - \
					 INTEL_INFO(dev_priv)->trans_offsets[TRANSCODER_A] + (reg) + \
					 DISPLAY_MMIO_BASE(dev_priv))
#define _MMIO_TRANS2(tran, reg)		_MMIO(_TRANS2(tran, reg))
#define _CURSOR2(pipe, reg)		_MMIO(INTEL_INFO(dev_priv)->cursor_offsets[(pipe)] - \
					      INTEL_INFO(dev_priv)->cursor_offsets[PIPE_A] + (reg) + \
					      DISPLAY_MMIO_BASE(dev_priv))

#define __MASKED_FIELD(mask, value) ((mask) << 16 | (value))
#define _MASKED_FIELD(mask, value) ({					   \
	if (__builtin_constant_p(mask))					   \
		BUILD_BUG_ON_MSG(((mask) & 0xffff0000), "Incorrect mask"); \
	if (__builtin_constant_p(value))				   \
		BUILD_BUG_ON_MSG((value) & 0xffff0000, "Incorrect value"); \
	if (__builtin_constant_p(mask) && __builtin_constant_p(value))	   \
		BUILD_BUG_ON_MSG((value) & ~(mask),			   \
				 "Incorrect value for mask");		   \
	__MASKED_FIELD(mask, value); })
#define _MASKED_BIT_ENABLE(a)	({ typeof(a) _a = (a); _MASKED_FIELD(_a, _a); })
#define _MASKED_BIT_DISABLE(a)	(_MASKED_FIELD((a), 0))

#define GU_CNTL				_MMIO(0x101010)
#define   LMEM_INIT			REG_BIT(7)

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

#define DPIO_PHY(pipe)			((pipe) >> 1)

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
#define   DPIO_ENABLE_CALIBRATION	(1 << 11)
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
#define   DPIO_PCS_TX_LANE2_RESET	(1 << 16)
#define   DPIO_PCS_TX_LANE1_RESET	(1 << 7)
#define   DPIO_LEFT_TXFIFO_RST_MASTER2	(1 << 4)
#define   DPIO_RIGHT_TXFIFO_RST_MASTER2	(1 << 3)
#define VLV_PCS_DW0(ch) _PORT(ch, _VLV_PCS_DW0_CH0, _VLV_PCS_DW0_CH1)

#define _VLV_PCS01_DW0_CH0		0x200
#define _VLV_PCS23_DW0_CH0		0x400
#define _VLV_PCS01_DW0_CH1		0x2600
#define _VLV_PCS23_DW0_CH1		0x2800
#define VLV_PCS01_DW0(ch) _PORT(ch, _VLV_PCS01_DW0_CH0, _VLV_PCS01_DW0_CH1)
#define VLV_PCS23_DW0(ch) _PORT(ch, _VLV_PCS23_DW0_CH0, _VLV_PCS23_DW0_CH1)

#define _VLV_PCS_DW1_CH0		0x8204
#define _VLV_PCS_DW1_CH1		0x8404
#define   CHV_PCS_REQ_SOFTRESET_EN	(1 << 23)
#define   DPIO_PCS_CLK_CRI_RXEB_EIOS_EN	(1 << 22)
#define   DPIO_PCS_CLK_CRI_RXDIGFILTSG_EN (1 << 21)
#define   DPIO_PCS_CLK_DATAWIDTH_SHIFT	(6)
#define   DPIO_PCS_CLK_SOFT_RESET	(1 << 5)
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
#define   DPIO_PCS_TX2MARGIN_MASK	(0x7 << 13)
#define   DPIO_PCS_TX2MARGIN_000	(0 << 13)
#define   DPIO_PCS_TX2MARGIN_101	(1 << 13)
#define   DPIO_PCS_TX1MARGIN_MASK	(0x7 << 10)
#define   DPIO_PCS_TX1MARGIN_000	(0 << 10)
#define   DPIO_PCS_TX1MARGIN_101	(1 << 10)
#define	VLV_PCS_DW9(ch) _PORT(ch, _VLV_PCS_DW9_CH0, _VLV_PCS_DW9_CH1)

#define _VLV_PCS01_DW9_CH0		0x224
#define _VLV_PCS23_DW9_CH0		0x424
#define _VLV_PCS01_DW9_CH1		0x2624
#define _VLV_PCS23_DW9_CH1		0x2824
#define VLV_PCS01_DW9(ch) _PORT(ch, _VLV_PCS01_DW9_CH0, _VLV_PCS01_DW9_CH1)
#define VLV_PCS23_DW9(ch) _PORT(ch, _VLV_PCS23_DW9_CH0, _VLV_PCS23_DW9_CH1)

#define _CHV_PCS_DW10_CH0		0x8228
#define _CHV_PCS_DW10_CH1		0x8428
#define   DPIO_PCS_SWING_CALC_TX0_TX2	(1 << 30)
#define   DPIO_PCS_SWING_CALC_TX1_TX3	(1 << 31)
#define   DPIO_PCS_TX2DEEMP_MASK	(0xf << 24)
#define   DPIO_PCS_TX2DEEMP_9P5		(0 << 24)
#define   DPIO_PCS_TX2DEEMP_6P0		(2 << 24)
#define   DPIO_PCS_TX1DEEMP_MASK	(0xf << 16)
#define   DPIO_PCS_TX1DEEMP_9P5		(0 << 16)
#define   DPIO_PCS_TX1DEEMP_6P0		(2 << 16)
#define CHV_PCS_DW10(ch) _PORT(ch, _CHV_PCS_DW10_CH0, _CHV_PCS_DW10_CH1)

#define _VLV_PCS01_DW10_CH0		0x0228
#define _VLV_PCS23_DW10_CH0		0x0428
#define _VLV_PCS01_DW10_CH1		0x2628
#define _VLV_PCS23_DW10_CH1		0x2828
#define VLV_PCS01_DW10(port) _PORT(port, _VLV_PCS01_DW10_CH0, _VLV_PCS01_DW10_CH1)
#define VLV_PCS23_DW10(port) _PORT(port, _VLV_PCS23_DW10_CH0, _VLV_PCS23_DW10_CH1)

#define _VLV_PCS_DW11_CH0		0x822c
#define _VLV_PCS_DW11_CH1		0x842c
#define   DPIO_TX2_STAGGER_MASK(x)	((x) << 24)
#define   DPIO_LANEDESKEW_STRAP_OVRD	(1 << 3)
#define   DPIO_LEFT_TXFIFO_RST_MASTER	(1 << 1)
#define   DPIO_RIGHT_TXFIFO_RST_MASTER	(1 << 0)
#define VLV_PCS_DW11(ch) _PORT(ch, _VLV_PCS_DW11_CH0, _VLV_PCS_DW11_CH1)

#define _VLV_PCS01_DW11_CH0		0x022c
#define _VLV_PCS23_DW11_CH0		0x042c
#define _VLV_PCS01_DW11_CH1		0x262c
#define _VLV_PCS23_DW11_CH1		0x282c
#define VLV_PCS01_DW11(ch) _PORT(ch, _VLV_PCS01_DW11_CH0, _VLV_PCS01_DW11_CH1)
#define VLV_PCS23_DW11(ch) _PORT(ch, _VLV_PCS23_DW11_CH0, _VLV_PCS23_DW11_CH1)

#define _VLV_PCS01_DW12_CH0		0x0230
#define _VLV_PCS23_DW12_CH0		0x0430
#define _VLV_PCS01_DW12_CH1		0x2630
#define _VLV_PCS23_DW12_CH1		0x2830
#define VLV_PCS01_DW12(ch) _PORT(ch, _VLV_PCS01_DW12_CH0, _VLV_PCS01_DW12_CH1)
#define VLV_PCS23_DW12(ch) _PORT(ch, _VLV_PCS23_DW12_CH0, _VLV_PCS23_DW12_CH1)

#define _VLV_PCS_DW12_CH0		0x8230
#define _VLV_PCS_DW12_CH1		0x8430
#define   DPIO_TX2_STAGGER_MULT(x)	((x) << 20)
#define   DPIO_TX1_STAGGER_MULT(x)	((x) << 16)
#define   DPIO_TX1_STAGGER_MASK(x)	((x) << 8)
#define   DPIO_LANESTAGGER_STRAP_OVRD	(1 << 6)
#define   DPIO_LANESTAGGER_STRAP(x)	((x) << 0)
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
#define   DPIO_TX_UNIQ_TRANS_SCALE_EN	(1 << 27)
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
#define   DPIO_TX_OCALINIT_EN		(1 << 31)
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
#define  DPIO_CHV_FEEDFWD_GAIN_MASK		(0xF << 0)
#define CHV_PLL_DW3(ch) _PIPE(ch, _CHV_PLL_DW3_CH0, _CHV_PLL_DW3_CH1)

#define _CHV_PLL_DW6_CH0		0x8018
#define _CHV_PLL_DW6_CH1		0x8198
#define   DPIO_CHV_GAIN_CTRL_SHIFT	16
#define	  DPIO_CHV_INT_COEFF_SHIFT	8
#define   DPIO_CHV_PROP_COEFF_SHIFT	0
#define CHV_PLL_DW6(ch) _PIPE(ch, _CHV_PLL_DW6_CH0, _CHV_PLL_DW6_CH1)

#define _CHV_PLL_DW8_CH0		0x8020
#define _CHV_PLL_DW8_CH1		0x81A0
#define   DPIO_CHV_TDC_TARGET_CNT_SHIFT 0
#define   DPIO_CHV_TDC_TARGET_CNT_MASK  (0x3FF << 0)
#define CHV_PLL_DW8(ch) _PIPE(ch, _CHV_PLL_DW8_CH0, _CHV_PLL_DW8_CH1)

#define _CHV_PLL_DW9_CH0		0x8024
#define _CHV_PLL_DW9_CH1		0x81A4
#define  DPIO_CHV_INT_LOCK_THRESHOLD_SHIFT		1 /* 3 bits */
#define  DPIO_CHV_INT_LOCK_THRESHOLD_MASK		(7 << 1)
#define  DPIO_CHV_INT_LOCK_THRESHOLD_SEL_COARSE	1 /* 1: coarse & 0 : fine  */
#define CHV_PLL_DW9(ch) _PIPE(ch, _CHV_PLL_DW9_CH0, _CHV_PLL_DW9_CH1)

#define _CHV_CMN_DW0_CH0               0x8100
#define   DPIO_ALLDL_POWERDOWN_SHIFT_CH0	19
#define   DPIO_ANYDL_POWERDOWN_SHIFT_CH0	18
#define   DPIO_ALLDL_POWERDOWN			(1 << 1)
#define   DPIO_ANYDL_POWERDOWN			(1 << 0)

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
#define   DPIO_ALLDL_POWERDOWN_SHIFT_CH1	30 /* CL2 DW6 only */
#define   DPIO_ANYDL_POWERDOWN_SHIFT_CH1	29 /* CL2 DW6 only */
#define   DPIO_DYNPWRDOWNEN_CH1		(1 << 28) /* CL2 DW6 only */
#define   CHV_CMN_USEDCLKCHANNEL	(1 << 13)

#define CHV_CMN_DW19(ch) _PIPE(ch, _CHV_CMN_DW19_CH0, _CHV_CMN_DW6_CH1)

#define CHV_CMN_DW28			0x8170
#define   DPIO_CL1POWERDOWNEN		(1 << 23)
#define   DPIO_DYNPWRDOWNEN_CH0		(1 << 22)
#define   DPIO_SUS_CLK_CONFIG_ON		(0 << 0)
#define   DPIO_SUS_CLK_CONFIG_CLKREQ		(1 << 0)
#define   DPIO_SUS_CLK_CONFIG_GATE		(2 << 0)
#define   DPIO_SUS_CLK_CONFIG_GATE_CLKREQ	(3 << 0)

#define CHV_CMN_DW30			0x8178
#define   DPIO_CL2_LDOFUSE_PWRENB	(1 << 6)
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

/* BXT PHY registers */
#define _BXT_PHY0_BASE			0x6C000
#define _BXT_PHY1_BASE			0x162000
#define _BXT_PHY2_BASE			0x163000
#define BXT_PHY_BASE(phy)		_PHY3((phy), _BXT_PHY0_BASE, \
						     _BXT_PHY1_BASE, \
						     _BXT_PHY2_BASE)

#define _BXT_PHY(phy, reg)						\
	_MMIO(BXT_PHY_BASE(phy) - _BXT_PHY0_BASE + (reg))

#define _BXT_PHY_CH(phy, ch, reg_ch0, reg_ch1)		\
	(BXT_PHY_BASE(phy) + _PIPE((ch), (reg_ch0) - _BXT_PHY0_BASE,	\
					 (reg_ch1) - _BXT_PHY0_BASE))
#define _MMIO_BXT_PHY_CH(phy, ch, reg_ch0, reg_ch1)		\
	_MMIO(_BXT_PHY_CH(phy, ch, reg_ch0, reg_ch1))

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

#define _PHY_CTL_FAMILY_EDP		0x64C80
#define _PHY_CTL_FAMILY_DDI		0x64C90
#define _PHY_CTL_FAMILY_DDI_C		0x64CA0
#define   COMMON_RESET_DIS		(1 << 31)
#define BXT_PHY_CTL_FAMILY(phy)		_MMIO_PHY3((phy), _PHY_CTL_FAMILY_DDI, \
							  _PHY_CTL_FAMILY_EDP, \
							  _PHY_CTL_FAMILY_DDI_C)

/* BXT PHY PLL registers */
#define _PORT_PLL_A			0x46074
#define _PORT_PLL_B			0x46078
#define _PORT_PLL_C			0x4607c
#define   PORT_PLL_ENABLE		(1 << 31)
#define   PORT_PLL_LOCK			(1 << 30)
#define   PORT_PLL_REF_SEL		(1 << 27)
#define   PORT_PLL_POWER_ENABLE		(1 << 26)
#define   PORT_PLL_POWER_STATE		(1 << 25)
#define BXT_PORT_PLL_ENABLE(port)	_MMIO_PORT(port, _PORT_PLL_A, _PORT_PLL_B)

#define _PORT_PLL_EBB_0_A		0x162034
#define _PORT_PLL_EBB_0_B		0x6C034
#define _PORT_PLL_EBB_0_C		0x6C340
#define   PORT_PLL_P1_SHIFT		13
#define   PORT_PLL_P1_MASK		(0x07 << PORT_PLL_P1_SHIFT)
#define   PORT_PLL_P1(x)		((x)  << PORT_PLL_P1_SHIFT)
#define   PORT_PLL_P2_SHIFT		8
#define   PORT_PLL_P2_MASK		(0x1f << PORT_PLL_P2_SHIFT)
#define   PORT_PLL_P2(x)		((x)  << PORT_PLL_P2_SHIFT)
#define BXT_PORT_PLL_EBB_0(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PLL_EBB_0_B, \
							 _PORT_PLL_EBB_0_C)

#define _PORT_PLL_EBB_4_A		0x162038
#define _PORT_PLL_EBB_4_B		0x6C038
#define _PORT_PLL_EBB_4_C		0x6C344
#define   PORT_PLL_10BIT_CLK_ENABLE	(1 << 13)
#define   PORT_PLL_RECALIBRATE		(1 << 14)
#define BXT_PORT_PLL_EBB_4(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PLL_EBB_4_B, \
							 _PORT_PLL_EBB_4_C)

#define _PORT_PLL_0_A			0x162100
#define _PORT_PLL_0_B			0x6C100
#define _PORT_PLL_0_C			0x6C380
/* PORT_PLL_0_A */
#define   PORT_PLL_M2_MASK		0xFF
/* PORT_PLL_1_A */
#define   PORT_PLL_N_SHIFT		8
#define   PORT_PLL_N_MASK		(0x0F << PORT_PLL_N_SHIFT)
#define   PORT_PLL_N(x)			((x) << PORT_PLL_N_SHIFT)
/* PORT_PLL_2_A */
#define   PORT_PLL_M2_FRAC_MASK		0x3FFFFF
/* PORT_PLL_3_A */
#define   PORT_PLL_M2_FRAC_ENABLE	(1 << 16)
/* PORT_PLL_6_A */
#define   PORT_PLL_PROP_COEFF_MASK	0xF
#define   PORT_PLL_INT_COEFF_MASK	(0x1F << 8)
#define   PORT_PLL_INT_COEFF(x)		((x)  << 8)
#define   PORT_PLL_GAIN_CTL_MASK	(0x07 << 16)
#define   PORT_PLL_GAIN_CTL(x)		((x)  << 16)
/* PORT_PLL_8_A */
#define   PORT_PLL_TARGET_CNT_MASK	0x3FF
/* PORT_PLL_9_A */
#define  PORT_PLL_LOCK_THRESHOLD_SHIFT	1
#define  PORT_PLL_LOCK_THRESHOLD_MASK	(0x7 << PORT_PLL_LOCK_THRESHOLD_SHIFT)
/* PORT_PLL_10_A */
#define  PORT_PLL_DCO_AMP_OVR_EN_H	(1 << 27)
#define  PORT_PLL_DCO_AMP_DEFAULT	15
#define  PORT_PLL_DCO_AMP_MASK		0x3c00
#define  PORT_PLL_DCO_AMP(x)		((x) << 10)
#define _PORT_PLL_BASE(phy, ch)		_BXT_PHY_CH(phy, ch, \
						    _PORT_PLL_0_B, \
						    _PORT_PLL_0_C)
#define BXT_PORT_PLL(phy, ch, idx)	_MMIO(_PORT_PLL_BASE(phy, ch) + \
					      (idx) * 4)

/* BXT PHY common lane registers */
#define _PORT_CL1CM_DW0_A		0x162000
#define _PORT_CL1CM_DW0_BC		0x6C000
#define   PHY_POWER_GOOD		(1 << 16)
#define   PHY_RESERVED			(1 << 7)
#define BXT_PORT_CL1CM_DW0(phy)		_BXT_PHY((phy), _PORT_CL1CM_DW0_BC)

#define _PORT_CL1CM_DW9_A		0x162024
#define _PORT_CL1CM_DW9_BC		0x6C024
#define   IREF0RC_OFFSET_SHIFT		8
#define   IREF0RC_OFFSET_MASK		(0xFF << IREF0RC_OFFSET_SHIFT)
#define BXT_PORT_CL1CM_DW9(phy)		_BXT_PHY((phy), _PORT_CL1CM_DW9_BC)

#define _PORT_CL1CM_DW10_A		0x162028
#define _PORT_CL1CM_DW10_BC		0x6C028
#define   IREF1RC_OFFSET_SHIFT		8
#define   IREF1RC_OFFSET_MASK		(0xFF << IREF1RC_OFFSET_SHIFT)
#define BXT_PORT_CL1CM_DW10(phy)	_BXT_PHY((phy), _PORT_CL1CM_DW10_BC)

#define _PORT_CL1CM_DW28_A		0x162070
#define _PORT_CL1CM_DW28_BC		0x6C070
#define   OCL1_POWER_DOWN_EN		(1 << 23)
#define   DW28_OLDO_DYN_PWR_DOWN_EN	(1 << 22)
#define   SUS_CLK_CONFIG		0x3
#define BXT_PORT_CL1CM_DW28(phy)	_BXT_PHY((phy), _PORT_CL1CM_DW28_BC)

#define _PORT_CL1CM_DW30_A		0x162078
#define _PORT_CL1CM_DW30_BC		0x6C078
#define   OCL2_LDOFUSE_PWR_DIS		(1 << 6)
#define BXT_PORT_CL1CM_DW30(phy)	_BXT_PHY((phy), _PORT_CL1CM_DW30_BC)

/* The spec defines this only for BXT PHY0, but lets assume that this
 * would exist for PHY1 too if it had a second channel.
 */
#define _PORT_CL2CM_DW6_A		0x162358
#define _PORT_CL2CM_DW6_BC		0x6C358
#define BXT_PORT_CL2CM_DW6(phy)		_BXT_PHY((phy), _PORT_CL2CM_DW6_BC)
#define   DW6_OLDO_DYN_PWR_DOWN_EN	(1 << 28)

/* BXT PHY Ref registers */
#define _PORT_REF_DW3_A			0x16218C
#define _PORT_REF_DW3_BC		0x6C18C
#define   GRC_DONE			(1 << 22)
#define BXT_PORT_REF_DW3(phy)		_BXT_PHY((phy), _PORT_REF_DW3_BC)

#define _PORT_REF_DW6_A			0x162198
#define _PORT_REF_DW6_BC		0x6C198
#define   GRC_CODE_SHIFT		24
#define   GRC_CODE_MASK			(0xFF << GRC_CODE_SHIFT)
#define   GRC_CODE_FAST_SHIFT		16
#define   GRC_CODE_FAST_MASK		(0xFF << GRC_CODE_FAST_SHIFT)
#define   GRC_CODE_SLOW_SHIFT		8
#define   GRC_CODE_SLOW_MASK		(0xFF << GRC_CODE_SLOW_SHIFT)
#define   GRC_CODE_NOM_MASK		0xFF
#define BXT_PORT_REF_DW6(phy)		_BXT_PHY((phy), _PORT_REF_DW6_BC)

#define _PORT_REF_DW8_A			0x1621A0
#define _PORT_REF_DW8_BC		0x6C1A0
#define   GRC_DIS			(1 << 15)
#define   GRC_RDY_OVRD			(1 << 1)
#define BXT_PORT_REF_DW8(phy)		_BXT_PHY((phy), _PORT_REF_DW8_BC)

/* BXT PHY PCS registers */
#define _PORT_PCS_DW10_LN01_A		0x162428
#define _PORT_PCS_DW10_LN01_B		0x6C428
#define _PORT_PCS_DW10_LN01_C		0x6C828
#define _PORT_PCS_DW10_GRP_A		0x162C28
#define _PORT_PCS_DW10_GRP_B		0x6CC28
#define _PORT_PCS_DW10_GRP_C		0x6CE28
#define BXT_PORT_PCS_DW10_LN01(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW10_LN01_B, \
							 _PORT_PCS_DW10_LN01_C)
#define BXT_PORT_PCS_DW10_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW10_GRP_B, \
							 _PORT_PCS_DW10_GRP_C)

#define   TX2_SWING_CALC_INIT		(1 << 31)
#define   TX1_SWING_CALC_INIT		(1 << 30)

#define _PORT_PCS_DW12_LN01_A		0x162430
#define _PORT_PCS_DW12_LN01_B		0x6C430
#define _PORT_PCS_DW12_LN01_C		0x6C830
#define _PORT_PCS_DW12_LN23_A		0x162630
#define _PORT_PCS_DW12_LN23_B		0x6C630
#define _PORT_PCS_DW12_LN23_C		0x6CA30
#define _PORT_PCS_DW12_GRP_A		0x162c30
#define _PORT_PCS_DW12_GRP_B		0x6CC30
#define _PORT_PCS_DW12_GRP_C		0x6CE30
#define   LANESTAGGER_STRAP_OVRD	(1 << 6)
#define   LANE_STAGGER_MASK		0x1F
#define BXT_PORT_PCS_DW12_LN01(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW12_LN01_B, \
							 _PORT_PCS_DW12_LN01_C)
#define BXT_PORT_PCS_DW12_LN23(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW12_LN23_B, \
							 _PORT_PCS_DW12_LN23_C)
#define BXT_PORT_PCS_DW12_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_PCS_DW12_GRP_B, \
							 _PORT_PCS_DW12_GRP_C)

/* BXT PHY TX registers */
#define _BXT_LANE_OFFSET(lane)           (((lane) >> 1) * 0x200 +	\
					  ((lane) & 1) * 0x80)

#define _PORT_TX_DW2_LN0_A		0x162508
#define _PORT_TX_DW2_LN0_B		0x6C508
#define _PORT_TX_DW2_LN0_C		0x6C908
#define _PORT_TX_DW2_GRP_A		0x162D08
#define _PORT_TX_DW2_GRP_B		0x6CD08
#define _PORT_TX_DW2_GRP_C		0x6CF08
#define BXT_PORT_TX_DW2_LN0(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW2_LN0_B, \
							 _PORT_TX_DW2_LN0_C)
#define BXT_PORT_TX_DW2_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW2_GRP_B, \
							 _PORT_TX_DW2_GRP_C)
#define   MARGIN_000_SHIFT		16
#define   MARGIN_000			(0xFF << MARGIN_000_SHIFT)
#define   UNIQ_TRANS_SCALE_SHIFT	8
#define   UNIQ_TRANS_SCALE		(0xFF << UNIQ_TRANS_SCALE_SHIFT)

#define _PORT_TX_DW3_LN0_A		0x16250C
#define _PORT_TX_DW3_LN0_B		0x6C50C
#define _PORT_TX_DW3_LN0_C		0x6C90C
#define _PORT_TX_DW3_GRP_A		0x162D0C
#define _PORT_TX_DW3_GRP_B		0x6CD0C
#define _PORT_TX_DW3_GRP_C		0x6CF0C
#define BXT_PORT_TX_DW3_LN0(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW3_LN0_B, \
							 _PORT_TX_DW3_LN0_C)
#define BXT_PORT_TX_DW3_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW3_GRP_B, \
							 _PORT_TX_DW3_GRP_C)
#define   SCALE_DCOMP_METHOD		(1 << 26)
#define   UNIQUE_TRANGE_EN_METHOD	(1 << 27)

#define _PORT_TX_DW4_LN0_A		0x162510
#define _PORT_TX_DW4_LN0_B		0x6C510
#define _PORT_TX_DW4_LN0_C		0x6C910
#define _PORT_TX_DW4_GRP_A		0x162D10
#define _PORT_TX_DW4_GRP_B		0x6CD10
#define _PORT_TX_DW4_GRP_C		0x6CF10
#define BXT_PORT_TX_DW4_LN0(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW4_LN0_B, \
							 _PORT_TX_DW4_LN0_C)
#define BXT_PORT_TX_DW4_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW4_GRP_B, \
							 _PORT_TX_DW4_GRP_C)
#define   DEEMPH_SHIFT			24
#define   DE_EMPHASIS			(0xFF << DEEMPH_SHIFT)

#define _PORT_TX_DW5_LN0_A		0x162514
#define _PORT_TX_DW5_LN0_B		0x6C514
#define _PORT_TX_DW5_LN0_C		0x6C914
#define _PORT_TX_DW5_GRP_A		0x162D14
#define _PORT_TX_DW5_GRP_B		0x6CD14
#define _PORT_TX_DW5_GRP_C		0x6CF14
#define BXT_PORT_TX_DW5_LN0(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW5_LN0_B, \
							 _PORT_TX_DW5_LN0_C)
#define BXT_PORT_TX_DW5_GRP(phy, ch)	_MMIO_BXT_PHY_CH(phy, ch, \
							 _PORT_TX_DW5_GRP_B, \
							 _PORT_TX_DW5_GRP_C)
#define   DCC_DELAY_RANGE_1		(1 << 9)
#define   DCC_DELAY_RANGE_2		(1 << 8)

#define _PORT_TX_DW14_LN0_A		0x162538
#define _PORT_TX_DW14_LN0_B		0x6C538
#define _PORT_TX_DW14_LN0_C		0x6C938
#define   LATENCY_OPTIM_SHIFT		30
#define   LATENCY_OPTIM			(1 << LATENCY_OPTIM_SHIFT)
#define BXT_PORT_TX_DW14_LN(phy, ch, lane)				\
	_MMIO(_BXT_PHY_CH(phy, ch, _PORT_TX_DW14_LN0_B,			\
				   _PORT_TX_DW14_LN0_C) +		\
	      _BXT_LANE_OFFSET(lane))

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
#define BLT_RING_BASE		0x22000



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
#define MBUS_ABOX_CTL(x)		_MMIO(_PICK(x, _MBUS_ABOX0_CTL, \
						    _MBUS_ABOX1_CTL, \
						    _MBUS_ABOX2_CTL))
#define MBUS_ABOX_BW_CREDIT_MASK	(3 << 20)
#define MBUS_ABOX_BW_CREDIT(x)		((x) << 20)
#define MBUS_ABOX_B_CREDIT_MASK		(0xF << 16)
#define MBUS_ABOX_B_CREDIT(x)		((x) << 16)
#define MBUS_ABOX_BT_CREDIT_POOL2_MASK	(0x1F << 8)
#define MBUS_ABOX_BT_CREDIT_POOL2(x)	((x) << 8)
#define MBUS_ABOX_BT_CREDIT_POOL1_MASK	(0x1F << 0)
#define MBUS_ABOX_BT_CREDIT_POOL1(x)	((x) << 0)

#define _PIPEA_MBUS_DBOX_CTL		0x7003C
#define _PIPEB_MBUS_DBOX_CTL		0x7103C
#define PIPE_MBUS_DBOX_CTL(pipe)	_MMIO_PIPE(pipe, _PIPEA_MBUS_DBOX_CTL, \
						   _PIPEB_MBUS_DBOX_CTL)
#define MBUS_DBOX_BW_CREDIT_MASK	(3 << 14)
#define MBUS_DBOX_BW_CREDIT(x)		((x) << 14)
#define MBUS_DBOX_B_CREDIT_MASK		(0x1F << 8)
#define MBUS_DBOX_B_CREDIT(x)		((x) << 8)
#define MBUS_DBOX_A_CREDIT_MASK		(0xF << 0)
#define MBUS_DBOX_A_CREDIT(x)		((x) << 0)

#define MBUS_UBOX_CTL			_MMIO(0x4503C)
#define MBUS_BBOX_CTL_S1		_MMIO(0x45040)
#define MBUS_BBOX_CTL_S2		_MMIO(0x45044)

#define MBUS_CTL			_MMIO(0x4438C)
#define MBUS_JOIN			REG_BIT(31)
#define MBUS_HASHING_MODE_MASK		REG_BIT(30)
#define MBUS_HASHING_MODE_2x2		REG_FIELD_PREP(MBUS_HASHING_MODE_MASK, 0)
#define MBUS_HASHING_MODE_1x4		REG_FIELD_PREP(MBUS_HASHING_MODE_MASK, 1)
#define MBUS_JOIN_PIPE_SELECT_MASK	REG_GENMASK(28, 26)
#define MBUS_JOIN_PIPE_SELECT(pipe)	REG_FIELD_PREP(MBUS_JOIN_PIPE_SELECT_MASK, pipe)
#define MBUS_JOIN_PIPE_SELECT_NONE	MBUS_JOIN_PIPE_SELECT(7)

#define HDPORT_STATE			_MMIO(0x45050)
#define   HDPORT_DPLL_USED_MASK		REG_GENMASK(15, 12)
#define   HDPORT_DDI_USED(phy)		REG_BIT(2 * (phy) + 1)
#define   HDPORT_ENABLED		REG_BIT(0)

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

#define I915_HDMI_LPE_AUDIO_BASE	(VLV_DISPLAY_BASE + 0x65000)
#define I915_HDMI_LPE_AUDIO_SIZE	0x1000

/* DisplayPort Audio w/ LPE */
#define VLV_AUD_CHICKEN_BIT_REG		_MMIO(VLV_DISPLAY_BASE + 0x62F38)
#define VLV_CHICKEN_BIT_DBG_ENABLE	(1 << 0)

#define _VLV_AUD_PORT_EN_B_DBG		(VLV_DISPLAY_BASE + 0x62F20)
#define _VLV_AUD_PORT_EN_C_DBG		(VLV_DISPLAY_BASE + 0x62F30)
#define _VLV_AUD_PORT_EN_D_DBG		(VLV_DISPLAY_BASE + 0x62F34)
#define VLV_AUD_PORT_EN_DBG(port)	_MMIO_PORT3((port) - PORT_B,	   \
						    _VLV_AUD_PORT_EN_B_DBG, \
						    _VLV_AUD_PORT_EN_C_DBG, \
						    _VLV_AUD_PORT_EN_D_DBG)
#define VLV_AMP_MUTE		        (1 << 1)

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

/*
 * Framebuffer compression (915+ only)
 */

#define FBC_CFB_BASE		_MMIO(0x3200) /* 4k page aligned */
#define FBC_LL_BASE		_MMIO(0x3204) /* 4k page aligned */
#define FBC_CONTROL		_MMIO(0x3208)
#define   FBC_CTL_EN			REG_BIT(31)
#define   FBC_CTL_PERIODIC		REG_BIT(30)
#define   FBC_CTL_INTERVAL_MASK		REG_GENMASK(29, 16)
#define   FBC_CTL_INTERVAL(x)		REG_FIELD_PREP(FBC_CTL_INTERVAL_MASK, (x))
#define   FBC_CTL_STOP_ON_MOD		REG_BIT(15)
#define   FBC_CTL_UNCOMPRESSIBLE	REG_BIT(14) /* i915+ */
#define   FBC_CTL_C3_IDLE		REG_BIT(13) /* i945gm only */
#define   FBC_CTL_STRIDE_MASK		REG_GENMASK(12, 5)
#define   FBC_CTL_STRIDE(x)		REG_FIELD_PREP(FBC_CTL_STRIDE_MASK, (x))
#define   FBC_CTL_FENCENO_MASK		REG_GENMASK(3, 0)
#define   FBC_CTL_FENCENO(x)		REG_FIELD_PREP(FBC_CTL_FENCENO_MASK, (x))
#define FBC_COMMAND		_MMIO(0x320c)
#define   FBC_CMD_COMPRESS		REG_BIT(0)
#define FBC_STATUS		_MMIO(0x3210)
#define   FBC_STAT_COMPRESSING		REG_BIT(31)
#define   FBC_STAT_COMPRESSED		REG_BIT(30)
#define   FBC_STAT_MODIFIED		REG_BIT(29)
#define   FBC_STAT_CURRENT_LINE_MASK	REG_GENMASK(10, 0)
#define FBC_CONTROL2		_MMIO(0x3214) /* i965gm only */
#define   FBC_CTL_FENCE_DBL		REG_BIT(4)
#define   FBC_CTL_IDLE_MASK		REG_GENMASK(3, 2)
#define   FBC_CTL_IDLE_IMM		REG_FIELD_PREP(FBC_CTL_IDLE_MASK, 0)
#define   FBC_CTL_IDLE_FULL		REG_FIELD_PREP(FBC_CTL_IDLE_MASK, 1)
#define   FBC_CTL_IDLE_LINE		REG_FIELD_PREP(FBC_CTL_IDLE_MASK, 2)
#define   FBC_CTL_IDLE_DEBUG		REG_FIELD_PREP(FBC_CTL_IDLE_MASK, 3)
#define   FBC_CTL_CPU_FENCE_EN		REG_BIT(1)
#define   FBC_CTL_PLANE_MASK		REG_GENMASK(1, 0)
#define   FBC_CTL_PLANE(i9xx_plane)	REG_FIELD_PREP(FBC_CTL_PLANE_MASK, (i9xx_plane))
#define FBC_FENCE_OFF		_MMIO(0x3218)  /* i965gm only, BSpec typo has 321Bh */
#define FBC_MOD_NUM		_MMIO(0x3220)  /* i965gm only */
#define   FBC_MOD_NUM_MASK		REG_GENMASK(31, 1)
#define   FBC_MOD_NUM_VALID		REG_BIT(0)
#define FBC_TAG(i)		_MMIO(0x3300 + (i) * 4) /* 49 reisters */
#define   FBC_TAG_MASK			REG_GENMASK(1, 0) /* 16 tags per register */
#define   FBC_TAG_MODIFIED		REG_FIELD_PREP(FBC_TAG_MASK, 0)
#define   FBC_TAG_UNCOMPRESSED		REG_FIELD_PREP(FBC_TAG_MASK, 1)
#define   FBC_TAG_UNCOMPRESSIBLE	REG_FIELD_PREP(FBC_TAG_MASK, 2)
#define   FBC_TAG_COMPRESSED		REG_FIELD_PREP(FBC_TAG_MASK, 3)

#define FBC_LL_SIZE		(1536)

/* Framebuffer compression for GM45+ */
#define DPFC_CB_BASE			_MMIO(0x3200)
#define ILK_DPFC_CB_BASE(fbc_id)	_MMIO_PIPE((fbc_id), 0x43200, 0x43240)
#define DPFC_CONTROL			_MMIO(0x3208)
#define ILK_DPFC_CONTROL(fbc_id)	_MMIO_PIPE((fbc_id), 0x43208, 0x43248)
#define   DPFC_CTL_EN				REG_BIT(31)
#define   DPFC_CTL_PLANE_MASK_G4X		REG_BIT(30) /* g4x-snb */
#define   DPFC_CTL_PLANE_G4X(i9xx_plane)	REG_FIELD_PREP(DPFC_CTL_PLANE_MASK_G4X, (i9xx_plane))
#define   DPFC_CTL_FENCE_EN_G4X			REG_BIT(29) /* g4x-snb */
#define   DPFC_CTL_PLANE_MASK_IVB		REG_GENMASK(30, 29) /* ivb only */
#define   DPFC_CTL_PLANE_IVB(i9xx_plane)	REG_FIELD_PREP(DPFC_CTL_PLANE_MASK_IVB, (i9xx_plane))
#define   DPFC_CTL_FENCE_EN_IVB			REG_BIT(28) /* ivb+ */
#define   DPFC_CTL_PERSISTENT_MODE		REG_BIT(25) /* g4x-snb */
#define   DPFC_CTL_FALSE_COLOR			REG_BIT(10) /* ivb+ */
#define   DPFC_CTL_SR_EN			REG_BIT(10) /* g4x only */
#define   DPFC_CTL_SR_EXIT_DIS			REG_BIT(9) /* g4x only */
#define   DPFC_CTL_LIMIT_MASK			REG_GENMASK(7, 6)
#define   DPFC_CTL_LIMIT_1X			REG_FIELD_PREP(DPFC_CTL_LIMIT_MASK, 0)
#define   DPFC_CTL_LIMIT_2X			REG_FIELD_PREP(DPFC_CTL_LIMIT_MASK, 1)
#define   DPFC_CTL_LIMIT_4X			REG_FIELD_PREP(DPFC_CTL_LIMIT_MASK, 2)
#define   DPFC_CTL_FENCENO_MASK			REG_GENMASK(3, 0)
#define   DPFC_CTL_FENCENO(fence)		REG_FIELD_PREP(DPFC_CTL_FENCENO_MASK, (fence))
#define DPFC_RECOMP_CTL			_MMIO(0x320c)
#define ILK_DPFC_RECOMP_CTL(fbc_id)	_MMIO_PIPE((fbc_id), 0x4320c, 0x4324c)
#define   DPFC_RECOMP_STALL_EN			REG_BIT(27)
#define   DPFC_RECOMP_STALL_WM_MASK		REG_GENMASK(26, 16)
#define   DPFC_RECOMP_TIMER_COUNT_MASK		REG_GENMASK(5, 0)
#define DPFC_STATUS			_MMIO(0x3210)
#define ILK_DPFC_STATUS(fbc_id)		_MMIO_PIPE((fbc_id), 0x43210, 0x43250)
#define   DPFC_INVAL_SEG_MASK			REG_GENMASK(26, 16)
#define   DPFC_COMP_SEG_MASK			REG_GENMASK(10, 0)
#define DPFC_STATUS2			_MMIO(0x3214)
#define ILK_DPFC_STATUS2(fbc_id)	_MMIO_PIPE((fbc_id), 0x43214, 0x43254)
#define   DPFC_COMP_SEG_MASK_IVB		REG_GENMASK(11, 0)
#define DPFC_FENCE_YOFF			_MMIO(0x3218)
#define ILK_DPFC_FENCE_YOFF(fbc_id)	_MMIO_PIPE((fbc_id), 0x43218, 0x43258)
#define DPFC_CHICKEN			_MMIO(0x3224)
#define ILK_DPFC_CHICKEN(fbc_id)	_MMIO_PIPE((fbc_id), 0x43224, 0x43264)
#define   DPFC_HT_MODIFY			REG_BIT(31) /* pre-ivb */
#define   DPFC_NUKE_ON_ANY_MODIFICATION		REG_BIT(23) /* bdw+ */
#define   DPFC_CHICKEN_COMP_DUMMY_PIXEL		REG_BIT(14) /* glk+ */
#define   DPFC_DISABLE_DUMMY0			REG_BIT(8) /* ivb+ */

#define GLK_FBC_STRIDE(fbc_id)	_MMIO_PIPE((fbc_id), 0x43228, 0x43268)
#define   FBC_STRIDE_OVERRIDE	REG_BIT(15)
#define   FBC_STRIDE_MASK	REG_GENMASK(14, 0)
#define   FBC_STRIDE(x)		REG_FIELD_PREP(FBC_STRIDE_MASK, (x))

#define ILK_FBC_RT_BASE		_MMIO(0x2128)
#define   ILK_FBC_RT_VALID	REG_BIT(0)
#define   SNB_FBC_FRONT_BUFFER	REG_BIT(1)

#define ILK_DISPLAY_CHICKEN1	_MMIO(0x42000)
#define   ILK_FBCQ_DIS		(1 << 22)
#define   ILK_PABSTRETCH_DIS	REG_BIT(21)
#define   ILK_SABSTRETCH_DIS	REG_BIT(20)
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


/*
 * Framebuffer compression for Sandybridge
 *
 * The following two registers are of type GTTMMADR
 */
#define SNB_DPFC_CTL_SA		_MMIO(0x100100)
#define   SNB_DPFC_FENCE_EN		REG_BIT(29)
#define   SNB_DPFC_FENCENO_MASK		REG_GENMASK(4, 0)
#define   SNB_DPFC_FENCENO(fence)	REG_FIELD_PREP(SNB_DPFC_FENCENO_MASK, (fence))
#define SNB_DPFC_CPU_FENCE_OFFSET	_MMIO(0x100104)

/* Framebuffer compression for Ivybridge */
#define IVB_FBC_RT_BASE			_MMIO(0x7020)
#define IVB_FBC_RT_BASE_UPPER		_MMIO(0x7024)

#define IPS_CTL		_MMIO(0x43408)
#define   IPS_ENABLE	(1 << 31)

#define MSG_FBC_REND_STATE(fbc_id)	_MMIO_PIPE((fbc_id), 0x50380, 0x50384)
#define   FBC_REND_NUKE			REG_BIT(2)
#define   FBC_REND_CACHE_CLEAN		REG_BIT(1)

/*
 * GPIO regs
 */
#define GPIO(gpio)		_MMIO(dev_priv->gpio_mmio_base + 0x5010 + \
				      4 * (gpio))

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

#define GMBUS0			_MMIO(dev_priv->gpio_mmio_base + 0x5100) /* clock/port select */
#define   GMBUS_AKSV_SELECT	(1 << 11)
#define   GMBUS_RATE_100KHZ	(0 << 8)
#define   GMBUS_RATE_50KHZ	(1 << 8)
#define   GMBUS_RATE_400KHZ	(2 << 8) /* reserved on Pineview */
#define   GMBUS_RATE_1MHZ	(3 << 8) /* reserved on Pineview */
#define   GMBUS_HOLD_EXT	(1 << 7) /* 300ns hold time, rsvd on Pineview */
#define   GMBUS_BYTE_CNT_OVERRIDE (1 << 6)

#define GMBUS1			_MMIO(dev_priv->gpio_mmio_base + 0x5104) /* command/status */
#define   GMBUS_SW_CLR_INT	(1 << 31)
#define   GMBUS_SW_RDY		(1 << 30)
#define   GMBUS_ENT		(1 << 29) /* enable timeout */
#define   GMBUS_CYCLE_NONE	(0 << 25)
#define   GMBUS_CYCLE_WAIT	(1 << 25)
#define   GMBUS_CYCLE_INDEX	(2 << 25)
#define   GMBUS_CYCLE_STOP	(4 << 25)
#define   GMBUS_BYTE_COUNT_SHIFT 16
#define   GMBUS_BYTE_COUNT_MAX   256U
#define   GEN9_GMBUS_BYTE_COUNT_MAX 511U
#define   GMBUS_SLAVE_INDEX_SHIFT 8
#define   GMBUS_SLAVE_ADDR_SHIFT 1
#define   GMBUS_SLAVE_READ	(1 << 0)
#define   GMBUS_SLAVE_WRITE	(0 << 0)
#define GMBUS2			_MMIO(dev_priv->gpio_mmio_base + 0x5108) /* status */
#define   GMBUS_INUSE		(1 << 15)
#define   GMBUS_HW_WAIT_PHASE	(1 << 14)
#define   GMBUS_STALL_TIMEOUT	(1 << 13)
#define   GMBUS_INT		(1 << 12)
#define   GMBUS_HW_RDY		(1 << 11)
#define   GMBUS_SATOER		(1 << 10)
#define   GMBUS_ACTIVE		(1 << 9)
#define GMBUS3			_MMIO(dev_priv->gpio_mmio_base + 0x510c) /* data buffer bytes 3-0 */
#define GMBUS4			_MMIO(dev_priv->gpio_mmio_base + 0x5110) /* interrupt mask (Pineview+) */
#define   GMBUS_SLAVE_TIMEOUT_EN (1 << 4)
#define   GMBUS_NAK_EN		(1 << 3)
#define   GMBUS_IDLE_EN		(1 << 2)
#define   GMBUS_HW_WAIT_EN	(1 << 1)
#define   GMBUS_HW_RDY_EN	(1 << 0)
#define GMBUS5			_MMIO(dev_priv->gpio_mmio_base + 0x5120) /* byte index */
#define   GMBUS_2BYTE_INDEX_EN	(1 << 31)

/*
 * Clock control & power management
 */
#define _DPLL_A (DISPLAY_MMIO_BASE(dev_priv) + 0x6014)
#define _DPLL_B (DISPLAY_MMIO_BASE(dev_priv) + 0x6018)
#define _CHV_DPLL_C (DISPLAY_MMIO_BASE(dev_priv) + 0x6030)
#define DPLL(pipe) _MMIO_PIPE3((pipe), _DPLL_A, _DPLL_B, _CHV_DPLL_C)

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

#define _DPLL_A_MD (DISPLAY_MMIO_BASE(dev_priv) + 0x601c)
#define _DPLL_B_MD (DISPLAY_MMIO_BASE(dev_priv) + 0x6020)
#define _CHV_DPLL_C_MD (DISPLAY_MMIO_BASE(dev_priv) + 0x603c)
#define DPLL_MD(pipe) _MMIO_PIPE3((pipe), _DPLL_A_MD, _DPLL_B_MD, _CHV_DPLL_C_MD)

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
#define DSPCLK_GATE_D	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x6200)
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

/*
 * Palette regs
 */
#define _PALETTE_A		0xa000
#define _PALETTE_B		0xa800
#define _CHV_PALETTE_C		0xc000
#define PALETTE_RED_MASK        REG_GENMASK(23, 16)
#define PALETTE_GREEN_MASK      REG_GENMASK(15, 8)
#define PALETTE_BLUE_MASK       REG_GENMASK(7, 0)
#define PALETTE(pipe, i)	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + \
				      _PICK((pipe), _PALETTE_A,		\
					    _PALETTE_B, _CHV_PALETTE_C) + \
				      (i) * 4)

#define PEG_BAND_GAP_DATA	_MMIO(0x14d68)

#define BXT_RP_STATE_CAP        _MMIO(0x138170)
#define GEN9_RP_STATE_LIMITS	_MMIO(0x138148)
#define XEHPSDV_RP_STATE_CAP	_MMIO(0x250014)

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
#define   DARBF_GATING_DIS		(1 << 27)
#define   PWM2_GATING_DIS		(1 << 14)
#define   PWM1_GATING_DIS		(1 << 13)

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

#define PIPE_CRC_CTL(pipe)		_MMIO_TRANS2(pipe, _PIPE_CRC_CTL_A)
#define PIPE_CRC_RES_1_IVB(pipe)	_MMIO_TRANS2(pipe, _PIPE_CRC_RES_1_A_IVB)
#define PIPE_CRC_RES_2_IVB(pipe)	_MMIO_TRANS2(pipe, _PIPE_CRC_RES_2_A_IVB)
#define PIPE_CRC_RES_3_IVB(pipe)	_MMIO_TRANS2(pipe, _PIPE_CRC_RES_3_A_IVB)
#define PIPE_CRC_RES_4_IVB(pipe)	_MMIO_TRANS2(pipe, _PIPE_CRC_RES_4_A_IVB)
#define PIPE_CRC_RES_5_IVB(pipe)	_MMIO_TRANS2(pipe, _PIPE_CRC_RES_5_A_IVB)

#define PIPE_CRC_RES_RED(pipe)		_MMIO_TRANS2(pipe, _PIPE_CRC_RES_RED_A)
#define PIPE_CRC_RES_GREEN(pipe)	_MMIO_TRANS2(pipe, _PIPE_CRC_RES_GREEN_A)
#define PIPE_CRC_RES_BLUE(pipe)		_MMIO_TRANS2(pipe, _PIPE_CRC_RES_BLUE_A)
#define PIPE_CRC_RES_RES1_I915(pipe)	_MMIO_TRANS2(pipe, _PIPE_CRC_RES_RES1_A_I915)
#define PIPE_CRC_RES_RES2_G4X(pipe)	_MMIO_TRANS2(pipe, _PIPE_CRC_RES_RES2_A_G4X)

/* Pipe A timing regs */
#define _HTOTAL_A	0x60000
#define _HBLANK_A	0x60004
#define _HSYNC_A	0x60008
#define _VTOTAL_A	0x6000c
#define _VBLANK_A	0x60010
#define _VSYNC_A	0x60014
#define _EXITLINE_A	0x60018
#define _PIPEASRC	0x6001c
#define   PIPESRC_WIDTH_MASK	REG_GENMASK(31, 16)
#define   PIPESRC_WIDTH(w)	REG_FIELD_PREP(PIPESRC_WIDTH_MASK, (w))
#define   PIPESRC_HEIGHT_MASK	REG_GENMASK(15, 0)
#define   PIPESRC_HEIGHT(h)	REG_FIELD_PREP(PIPESRC_HEIGHT_MASK, (h))
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

/* DSI 0 timing regs */
#define _HTOTAL_DSI0		0x6b000
#define _HSYNC_DSI0		0x6b008
#define _VTOTAL_DSI0		0x6b00c
#define _VSYNC_DSI0		0x6b014
#define _VSYNCSHIFT_DSI0	0x6b028

/* DSI 1 timing regs */
#define _HTOTAL_DSI1		0x6b800
#define _HSYNC_DSI1		0x6b808
#define _VTOTAL_DSI1		0x6b80c
#define _VSYNC_DSI1		0x6b814
#define _VSYNCSHIFT_DSI1	0x6b828

#define TRANSCODER_A_OFFSET 0x60000
#define TRANSCODER_B_OFFSET 0x61000
#define TRANSCODER_C_OFFSET 0x62000
#define CHV_TRANSCODER_C_OFFSET 0x63000
#define TRANSCODER_D_OFFSET 0x63000
#define TRANSCODER_EDP_OFFSET 0x6f000
#define TRANSCODER_DSI0_OFFSET	0x6b000
#define TRANSCODER_DSI1_OFFSET	0x6b800

#define HTOTAL(trans)		_MMIO_TRANS2(trans, _HTOTAL_A)
#define HBLANK(trans)		_MMIO_TRANS2(trans, _HBLANK_A)
#define HSYNC(trans)		_MMIO_TRANS2(trans, _HSYNC_A)
#define VTOTAL(trans)		_MMIO_TRANS2(trans, _VTOTAL_A)
#define VBLANK(trans)		_MMIO_TRANS2(trans, _VBLANK_A)
#define VSYNC(trans)		_MMIO_TRANS2(trans, _VSYNC_A)
#define BCLRPAT(trans)		_MMIO_TRANS2(trans, _BCLRPAT_A)
#define VSYNCSHIFT(trans)	_MMIO_TRANS2(trans, _VSYNCSHIFT_A)
#define PIPESRC(trans)		_MMIO_TRANS2(trans, _PIPEASRC)
#define PIPE_MULT(trans)	_MMIO_TRANS2(trans, _PIPE_MULT_A)

#define EXITLINE(trans)		_MMIO_TRANS2(trans, _EXITLINE_A)
#define   EXITLINE_ENABLE	REG_BIT(31)
#define   EXITLINE_MASK		REG_GENMASK(12, 0)
#define   EXITLINE_SHIFT	0

/* VRR registers */
#define _TRANS_VRR_CTL_A		0x60420
#define _TRANS_VRR_CTL_B		0x61420
#define _TRANS_VRR_CTL_C		0x62420
#define _TRANS_VRR_CTL_D		0x63420
#define TRANS_VRR_CTL(trans)			_MMIO_TRANS2(trans, _TRANS_VRR_CTL_A)
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
#define TRANS_VRR_VMAX(trans)		_MMIO_TRANS2(trans, _TRANS_VRR_VMAX_A)
#define   VRR_VMAX_MASK			REG_GENMASK(19, 0)

#define _TRANS_VRR_VMIN_A		0x60434
#define _TRANS_VRR_VMIN_B		0x61434
#define _TRANS_VRR_VMIN_C		0x62434
#define _TRANS_VRR_VMIN_D		0x63434
#define TRANS_VRR_VMIN(trans)		_MMIO_TRANS2(trans, _TRANS_VRR_VMIN_A)
#define   VRR_VMIN_MASK			REG_GENMASK(15, 0)

#define _TRANS_VRR_VMAXSHIFT_A		0x60428
#define _TRANS_VRR_VMAXSHIFT_B		0x61428
#define _TRANS_VRR_VMAXSHIFT_C		0x62428
#define _TRANS_VRR_VMAXSHIFT_D		0x63428
#define TRANS_VRR_VMAXSHIFT(trans)	_MMIO_TRANS2(trans, \
					_TRANS_VRR_VMAXSHIFT_A)
#define   VRR_VMAXSHIFT_DEC_MASK	REG_GENMASK(29, 16)
#define   VRR_VMAXSHIFT_DEC		REG_BIT(16)
#define   VRR_VMAXSHIFT_INC_MASK	REG_GENMASK(12, 0)

#define _TRANS_VRR_STATUS_A		0x6042C
#define _TRANS_VRR_STATUS_B		0x6142C
#define _TRANS_VRR_STATUS_C		0x6242C
#define _TRANS_VRR_STATUS_D		0x6342C
#define TRANS_VRR_STATUS(trans)		_MMIO_TRANS2(trans, _TRANS_VRR_STATUS_A)
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
#define TRANS_VRR_VTOTAL_PREV(trans)	_MMIO_TRANS2(trans, \
					_TRANS_VRR_VTOTAL_PREV_A)
#define   VRR_VTOTAL_FLIP_BEFR_BNDR	REG_BIT(31)
#define   VRR_VTOTAL_FLIP_AFTER_BNDR	REG_BIT(30)
#define   VRR_VTOTAL_FLIP_AFTER_DBLBUF	REG_BIT(29)
#define   VRR_VTOTAL_PREV_FRAME_MASK	REG_GENMASK(19, 0)

#define _TRANS_VRR_FLIPLINE_A		0x60438
#define _TRANS_VRR_FLIPLINE_B		0x61438
#define _TRANS_VRR_FLIPLINE_C		0x62438
#define _TRANS_VRR_FLIPLINE_D		0x63438
#define TRANS_VRR_FLIPLINE(trans)	_MMIO_TRANS2(trans, \
					_TRANS_VRR_FLIPLINE_A)
#define   VRR_FLIPLINE_MASK		REG_GENMASK(19, 0)

#define _TRANS_VRR_STATUS2_A		0x6043C
#define _TRANS_VRR_STATUS2_B		0x6143C
#define _TRANS_VRR_STATUS2_C		0x6243C
#define _TRANS_VRR_STATUS2_D		0x6343C
#define TRANS_VRR_STATUS2(trans)	_MMIO_TRANS2(trans, _TRANS_VRR_STATUS2_A)
#define   VRR_STATUS2_VERT_LN_CNT_MASK	REG_GENMASK(19, 0)

#define _TRANS_PUSH_A			0x60A70
#define _TRANS_PUSH_B			0x61A70
#define _TRANS_PUSH_C			0x62A70
#define _TRANS_PUSH_D			0x63A70
#define TRANS_PUSH(trans)		_MMIO_TRANS2(trans, _TRANS_PUSH_A)
#define   TRANS_PUSH_EN			REG_BIT(31)
#define   TRANS_PUSH_SEND		REG_BIT(30)

/*
 * HSW+ eDP PSR registers
 *
 * HSW PSR registers are relative to DDIA(_DDI_BUF_CTL_A + 0x800) with just one
 * instance of it
 */
#define _SRD_CTL_A				0x60800
#define _SRD_CTL_EDP				0x6f800
#define EDP_PSR_CTL(tran)			_MMIO(_TRANS2(tran, _SRD_CTL_A))
#define   EDP_PSR_ENABLE			(1 << 31)
#define   BDW_PSR_SINGLE_FRAME			(1 << 30)
#define   EDP_PSR_RESTORE_PSR_ACTIVE_CTX_MASK	(1 << 29) /* SW can't modify */
#define   EDP_PSR_LINK_STANDBY			(1 << 27)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_MASK	(3 << 25)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_8_LINES	(0 << 25)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_4_LINES	(1 << 25)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_2_LINES	(2 << 25)
#define   EDP_PSR_MIN_LINK_ENTRY_TIME_0_LINES	(3 << 25)
#define   EDP_PSR_MAX_SLEEP_TIME_SHIFT		20
#define   EDP_PSR_SKIP_AUX_EXIT			(1 << 12)
#define   EDP_PSR_TP1_TP2_SEL			(0 << 11)
#define   EDP_PSR_TP1_TP3_SEL			(1 << 11)
#define   EDP_PSR_CRC_ENABLE			(1 << 10) /* BDW+ */
#define   EDP_PSR_TP2_TP3_TIME_500us		(0 << 8)
#define   EDP_PSR_TP2_TP3_TIME_100us		(1 << 8)
#define   EDP_PSR_TP2_TP3_TIME_2500us		(2 << 8)
#define   EDP_PSR_TP2_TP3_TIME_0us		(3 << 8)
#define   EDP_PSR_TP4_TIME_0US			(3 << 6) /* ICL+ */
#define   EDP_PSR_TP1_TIME_500us		(0 << 4)
#define   EDP_PSR_TP1_TIME_100us		(1 << 4)
#define   EDP_PSR_TP1_TIME_2500us		(2 << 4)
#define   EDP_PSR_TP1_TIME_0us			(3 << 4)
#define   EDP_PSR_IDLE_FRAME_SHIFT		0

/*
 * Until TGL, IMR/IIR are fixed at 0x648xx. On TGL+ those registers are relative
 * to transcoder and bits defined for each one as if using no shift (i.e. as if
 * it was for TRANSCODER_EDP)
 */
#define EDP_PSR_IMR				_MMIO(0x64834)
#define EDP_PSR_IIR				_MMIO(0x64838)
#define _PSR_IMR_A				0x60814
#define _PSR_IIR_A				0x60818
#define TRANS_PSR_IMR(tran)			_MMIO_TRANS2(tran, _PSR_IMR_A)
#define TRANS_PSR_IIR(tran)			_MMIO_TRANS2(tran, _PSR_IIR_A)
#define   _EDP_PSR_TRANS_SHIFT(trans)		((trans) == TRANSCODER_EDP ? \
						 0 : ((trans) - TRANSCODER_A + 1) * 8)
#define   EDP_PSR_TRANS_MASK(trans)		(0x7 << _EDP_PSR_TRANS_SHIFT(trans))
#define   EDP_PSR_ERROR(trans)			(0x4 << _EDP_PSR_TRANS_SHIFT(trans))
#define   EDP_PSR_POST_EXIT(trans)		(0x2 << _EDP_PSR_TRANS_SHIFT(trans))
#define   EDP_PSR_PRE_ENTRY(trans)		(0x1 << _EDP_PSR_TRANS_SHIFT(trans))

#define _SRD_AUX_DATA_A				0x60814
#define _SRD_AUX_DATA_EDP			0x6f814
#define EDP_PSR_AUX_DATA(tran, i)		_MMIO(_TRANS2(tran, _SRD_AUX_DATA_A) + (i) + 4) /* 5 registers */

#define _SRD_STATUS_A				0x60840
#define _SRD_STATUS_EDP				0x6f840
#define EDP_PSR_STATUS(tran)			_MMIO(_TRANS2(tran, _SRD_STATUS_A))
#define   EDP_PSR_STATUS_STATE_MASK		(7 << 29)
#define   EDP_PSR_STATUS_STATE_SHIFT		29
#define   EDP_PSR_STATUS_STATE_IDLE		(0 << 29)
#define   EDP_PSR_STATUS_STATE_SRDONACK		(1 << 29)
#define   EDP_PSR_STATUS_STATE_SRDENT		(2 << 29)
#define   EDP_PSR_STATUS_STATE_BUFOFF		(3 << 29)
#define   EDP_PSR_STATUS_STATE_BUFON		(4 << 29)
#define   EDP_PSR_STATUS_STATE_AUXACK		(5 << 29)
#define   EDP_PSR_STATUS_STATE_SRDOFFACK	(6 << 29)
#define   EDP_PSR_STATUS_LINK_MASK		(3 << 26)
#define   EDP_PSR_STATUS_LINK_FULL_OFF		(0 << 26)
#define   EDP_PSR_STATUS_LINK_FULL_ON		(1 << 26)
#define   EDP_PSR_STATUS_LINK_STANDBY		(2 << 26)
#define   EDP_PSR_STATUS_MAX_SLEEP_TIMER_SHIFT	20
#define   EDP_PSR_STATUS_MAX_SLEEP_TIMER_MASK	0x1f
#define   EDP_PSR_STATUS_COUNT_SHIFT		16
#define   EDP_PSR_STATUS_COUNT_MASK		0xf
#define   EDP_PSR_STATUS_AUX_ERROR		(1 << 15)
#define   EDP_PSR_STATUS_AUX_SENDING		(1 << 12)
#define   EDP_PSR_STATUS_SENDING_IDLE		(1 << 9)
#define   EDP_PSR_STATUS_SENDING_TP2_TP3	(1 << 8)
#define   EDP_PSR_STATUS_SENDING_TP1		(1 << 4)
#define   EDP_PSR_STATUS_IDLE_MASK		0xf

#define _SRD_PERF_CNT_A			0x60844
#define _SRD_PERF_CNT_EDP		0x6f844
#define EDP_PSR_PERF_CNT(tran)		_MMIO(_TRANS2(tran, _SRD_PERF_CNT_A))
#define   EDP_PSR_PERF_CNT_MASK		0xffffff

/* PSR_MASK on SKL+ */
#define _SRD_DEBUG_A				0x60860
#define _SRD_DEBUG_EDP				0x6f860
#define EDP_PSR_DEBUG(tran)			_MMIO(_TRANS2(tran, _SRD_DEBUG_A))
#define   EDP_PSR_DEBUG_MASK_MAX_SLEEP         (1 << 28)
#define   EDP_PSR_DEBUG_MASK_LPSP              (1 << 27)
#define   EDP_PSR_DEBUG_MASK_MEMUP             (1 << 26)
#define   EDP_PSR_DEBUG_MASK_HPD               (1 << 25)
#define   EDP_PSR_DEBUG_MASK_DISP_REG_WRITE    (1 << 16) /* Reserved in ICL+ */
#define   EDP_PSR_DEBUG_EXIT_ON_PIXEL_UNDERRUN (1 << 15) /* SKL+ */

#define _PSR2_CTL_A				0x60900
#define _PSR2_CTL_EDP				0x6f900
#define EDP_PSR2_CTL(tran)			_MMIO_TRANS2(tran, _PSR2_CTL_A)
#define   EDP_PSR2_ENABLE			(1 << 31)
#define   EDP_SU_TRACK_ENABLE			(1 << 30) /* up to adl-p */
#define   TGL_EDP_PSR2_BLOCK_COUNT_NUM_2	(0 << 28)
#define   TGL_EDP_PSR2_BLOCK_COUNT_NUM_3	(1 << 28)
#define   EDP_Y_COORDINATE_ENABLE		REG_BIT(25) /* display 10, 11 and 12 */
#define   EDP_PSR2_SU_SDP_SCANLINE		REG_BIT(25) /* display 13+ */
#define   EDP_MAX_SU_DISABLE_TIME(t)		((t) << 20)
#define   EDP_MAX_SU_DISABLE_TIME_MASK		(0x1f << 20)
#define   EDP_PSR2_IO_BUFFER_WAKE_MAX_LINES	8
#define   EDP_PSR2_IO_BUFFER_WAKE(lines)	((EDP_PSR2_IO_BUFFER_WAKE_MAX_LINES - (lines)) << 13)
#define   EDP_PSR2_IO_BUFFER_WAKE_MASK		(3 << 13)
#define   TGL_EDP_PSR2_IO_BUFFER_WAKE_MIN_LINES	5
#define   TGL_EDP_PSR2_IO_BUFFER_WAKE_SHIFT	13
#define   TGL_EDP_PSR2_IO_BUFFER_WAKE(lines)	(((lines) - TGL_EDP_PSR2_IO_BUFFER_WAKE_MIN_LINES) << TGL_EDP_PSR2_IO_BUFFER_WAKE_SHIFT)
#define   TGL_EDP_PSR2_IO_BUFFER_WAKE_MASK	(7 << 13)
#define   EDP_PSR2_FAST_WAKE_MAX_LINES		8
#define   EDP_PSR2_FAST_WAKE(lines)		((EDP_PSR2_FAST_WAKE_MAX_LINES - (lines)) << 11)
#define   EDP_PSR2_FAST_WAKE_MASK		(3 << 11)
#define   TGL_EDP_PSR2_FAST_WAKE_MIN_LINES	5
#define   TGL_EDP_PSR2_FAST_WAKE_MIN_SHIFT	10
#define   TGL_EDP_PSR2_FAST_WAKE(lines)		(((lines) - TGL_EDP_PSR2_FAST_WAKE_MIN_LINES) << TGL_EDP_PSR2_FAST_WAKE_MIN_SHIFT)
#define   TGL_EDP_PSR2_FAST_WAKE_MASK		(7 << 10)
#define   EDP_PSR2_TP2_TIME_500us		(0 << 8)
#define   EDP_PSR2_TP2_TIME_100us		(1 << 8)
#define   EDP_PSR2_TP2_TIME_2500us		(2 << 8)
#define   EDP_PSR2_TP2_TIME_50us		(3 << 8)
#define   EDP_PSR2_TP2_TIME_MASK		(3 << 8)
#define   EDP_PSR2_FRAME_BEFORE_SU_SHIFT	4
#define   EDP_PSR2_FRAME_BEFORE_SU_MASK		(0xf << 4)
#define   EDP_PSR2_FRAME_BEFORE_SU(a)		((a) << 4)
#define   EDP_PSR2_IDLE_FRAME_MASK		0xf
#define   EDP_PSR2_IDLE_FRAME_SHIFT		0

#define _PSR_EVENT_TRANS_A			0x60848
#define _PSR_EVENT_TRANS_B			0x61848
#define _PSR_EVENT_TRANS_C			0x62848
#define _PSR_EVENT_TRANS_D			0x63848
#define _PSR_EVENT_TRANS_EDP			0x6f848
#define PSR_EVENT(tran)				_MMIO_TRANS2(tran, _PSR_EVENT_TRANS_A)
#define  PSR_EVENT_PSR2_WD_TIMER_EXPIRE		(1 << 17)
#define  PSR_EVENT_PSR2_DISABLED		(1 << 16)
#define  PSR_EVENT_SU_DIRTY_FIFO_UNDERRUN	(1 << 15)
#define  PSR_EVENT_SU_CRC_FIFO_UNDERRUN		(1 << 14)
#define  PSR_EVENT_GRAPHICS_RESET		(1 << 12)
#define  PSR_EVENT_PCH_INTERRUPT		(1 << 11)
#define  PSR_EVENT_MEMORY_UP			(1 << 10)
#define  PSR_EVENT_FRONT_BUFFER_MODIFY		(1 << 9)
#define  PSR_EVENT_WD_TIMER_EXPIRE		(1 << 8)
#define  PSR_EVENT_PIPE_REGISTERS_UPDATE	(1 << 6)
#define  PSR_EVENT_REGISTER_UPDATE		(1 << 5) /* Reserved in ICL+ */
#define  PSR_EVENT_HDCP_ENABLE			(1 << 4)
#define  PSR_EVENT_KVMR_SESSION_ENABLE		(1 << 3)
#define  PSR_EVENT_VBI_ENABLE			(1 << 2)
#define  PSR_EVENT_LPSP_MODE_EXIT		(1 << 1)
#define  PSR_EVENT_PSR_DISABLE			(1 << 0)

#define _PSR2_STATUS_A				0x60940
#define _PSR2_STATUS_EDP			0x6f940
#define EDP_PSR2_STATUS(tran)			_MMIO_TRANS2(tran, _PSR2_STATUS_A)
#define EDP_PSR2_STATUS_STATE_MASK		REG_GENMASK(31, 28)
#define EDP_PSR2_STATUS_STATE_DEEP_SLEEP	REG_FIELD_PREP(EDP_PSR2_STATUS_STATE_MASK, 0x8)

#define _PSR2_SU_STATUS_A		0x60914
#define _PSR2_SU_STATUS_EDP		0x6f914
#define _PSR2_SU_STATUS(tran, index)	_MMIO(_TRANS2(tran, _PSR2_SU_STATUS_A) + (index) * 4)
#define PSR2_SU_STATUS(tran, frame)	(_PSR2_SU_STATUS(tran, (frame) / 3))
#define PSR2_SU_STATUS_SHIFT(frame)	(((frame) % 3) * 10)
#define PSR2_SU_STATUS_MASK(frame)	(0x3ff << PSR2_SU_STATUS_SHIFT(frame))
#define PSR2_SU_STATUS_FRAMES		8

#define _PSR2_MAN_TRK_CTL_A					0x60910
#define _PSR2_MAN_TRK_CTL_EDP					0x6f910
#define PSR2_MAN_TRK_CTL(tran)					_MMIO_TRANS2(tran, _PSR2_MAN_TRK_CTL_A)
#define  PSR2_MAN_TRK_CTL_ENABLE				REG_BIT(31)
#define  PSR2_MAN_TRK_CTL_SU_REGION_START_ADDR_MASK		REG_GENMASK(30, 21)
#define  PSR2_MAN_TRK_CTL_SU_REGION_START_ADDR(val)		REG_FIELD_PREP(PSR2_MAN_TRK_CTL_SU_REGION_START_ADDR_MASK, val)
#define  PSR2_MAN_TRK_CTL_SU_REGION_END_ADDR_MASK		REG_GENMASK(20, 11)
#define  PSR2_MAN_TRK_CTL_SU_REGION_END_ADDR(val)		REG_FIELD_PREP(PSR2_MAN_TRK_CTL_SU_REGION_END_ADDR_MASK, val)
#define  PSR2_MAN_TRK_CTL_SF_SINGLE_FULL_FRAME			REG_BIT(3)
#define  PSR2_MAN_TRK_CTL_SF_CONTINUOS_FULL_FRAME		REG_BIT(2)
#define  PSR2_MAN_TRK_CTL_SF_PARTIAL_FRAME_UPDATE		REG_BIT(1)
#define  ADLP_PSR2_MAN_TRK_CTL_SU_REGION_START_ADDR_MASK	REG_GENMASK(28, 16)
#define  ADLP_PSR2_MAN_TRK_CTL_SU_REGION_START_ADDR(val)	REG_FIELD_PREP(ADLP_PSR2_MAN_TRK_CTL_SU_REGION_START_ADDR_MASK, val)
#define  ADLP_PSR2_MAN_TRK_CTL_SU_REGION_END_ADDR_MASK		REG_GENMASK(12, 0)
#define  ADLP_PSR2_MAN_TRK_CTL_SU_REGION_END_ADDR(val)		REG_FIELD_PREP(ADLP_PSR2_MAN_TRK_CTL_SU_REGION_END_ADDR_MASK, val)
#define  ADLP_PSR2_MAN_TRK_CTL_SF_SINGLE_FULL_FRAME		REG_BIT(14)
#define  ADLP_PSR2_MAN_TRK_CTL_SF_CONTINUOS_FULL_FRAME		REG_BIT(13)

/* Icelake DSC Rate Control Range Parameter Registers */
#define DSCA_RC_RANGE_PARAMETERS_0		_MMIO(0x6B240)
#define DSCA_RC_RANGE_PARAMETERS_0_UDW		_MMIO(0x6B240 + 4)
#define DSCC_RC_RANGE_PARAMETERS_0		_MMIO(0x6BA40)
#define DSCC_RC_RANGE_PARAMETERS_0_UDW		_MMIO(0x6BA40 + 4)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_0_PB	(0x78208)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_0_UDW_PB	(0x78208 + 4)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_0_PB	(0x78308)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_0_UDW_PB	(0x78308 + 4)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_0_PC	(0x78408)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_0_UDW_PC	(0x78408 + 4)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_0_PC	(0x78508)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_0_UDW_PC	(0x78508 + 4)
#define ICL_DSC0_RC_RANGE_PARAMETERS_0(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_0_PB, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_0_PC)
#define ICL_DSC0_RC_RANGE_PARAMETERS_0_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_0_UDW_PB, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_0_UDW_PC)
#define ICL_DSC1_RC_RANGE_PARAMETERS_0(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_0_PB, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_0_PC)
#define ICL_DSC1_RC_RANGE_PARAMETERS_0_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_0_UDW_PB, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_0_UDW_PC)
#define RC_BPG_OFFSET_SHIFT			10
#define RC_MAX_QP_SHIFT				5
#define RC_MIN_QP_SHIFT				0

#define DSCA_RC_RANGE_PARAMETERS_1		_MMIO(0x6B248)
#define DSCA_RC_RANGE_PARAMETERS_1_UDW		_MMIO(0x6B248 + 4)
#define DSCC_RC_RANGE_PARAMETERS_1		_MMIO(0x6BA48)
#define DSCC_RC_RANGE_PARAMETERS_1_UDW		_MMIO(0x6BA48 + 4)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_1_PB	(0x78210)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_1_UDW_PB	(0x78210 + 4)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_1_PB	(0x78310)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_1_UDW_PB	(0x78310 + 4)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_1_PC	(0x78410)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_1_UDW_PC	(0x78410 + 4)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_1_PC	(0x78510)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_1_UDW_PC	(0x78510 + 4)
#define ICL_DSC0_RC_RANGE_PARAMETERS_1(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_1_PB, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_1_PC)
#define ICL_DSC0_RC_RANGE_PARAMETERS_1_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_1_UDW_PB, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_1_UDW_PC)
#define ICL_DSC1_RC_RANGE_PARAMETERS_1(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_1_PB, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_1_PC)
#define ICL_DSC1_RC_RANGE_PARAMETERS_1_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_1_UDW_PB, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_1_UDW_PC)

#define DSCA_RC_RANGE_PARAMETERS_2		_MMIO(0x6B250)
#define DSCA_RC_RANGE_PARAMETERS_2_UDW		_MMIO(0x6B250 + 4)
#define DSCC_RC_RANGE_PARAMETERS_2		_MMIO(0x6BA50)
#define DSCC_RC_RANGE_PARAMETERS_2_UDW		_MMIO(0x6BA50 + 4)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_2_PB	(0x78218)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_2_UDW_PB	(0x78218 + 4)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_2_PB	(0x78318)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_2_UDW_PB	(0x78318 + 4)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_2_PC	(0x78418)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_2_UDW_PC	(0x78418 + 4)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_2_PC	(0x78518)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_2_UDW_PC	(0x78518 + 4)
#define ICL_DSC0_RC_RANGE_PARAMETERS_2(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_2_PB, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_2_PC)
#define ICL_DSC0_RC_RANGE_PARAMETERS_2_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_2_UDW_PB, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_2_UDW_PC)
#define ICL_DSC1_RC_RANGE_PARAMETERS_2(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_2_PB, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_2_PC)
#define ICL_DSC1_RC_RANGE_PARAMETERS_2_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_2_UDW_PB, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_2_UDW_PC)

#define DSCA_RC_RANGE_PARAMETERS_3		_MMIO(0x6B258)
#define DSCA_RC_RANGE_PARAMETERS_3_UDW		_MMIO(0x6B258 + 4)
#define DSCC_RC_RANGE_PARAMETERS_3		_MMIO(0x6BA58)
#define DSCC_RC_RANGE_PARAMETERS_3_UDW		_MMIO(0x6BA58 + 4)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_3_PB	(0x78220)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_3_UDW_PB	(0x78220 + 4)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_3_PB	(0x78320)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_3_UDW_PB	(0x78320 + 4)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_3_PC	(0x78420)
#define _ICL_DSC0_RC_RANGE_PARAMETERS_3_UDW_PC	(0x78420 + 4)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_3_PC	(0x78520)
#define _ICL_DSC1_RC_RANGE_PARAMETERS_3_UDW_PC	(0x78520 + 4)
#define ICL_DSC0_RC_RANGE_PARAMETERS_3(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_3_PB, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_3_PC)
#define ICL_DSC0_RC_RANGE_PARAMETERS_3_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_3_UDW_PB, \
							_ICL_DSC0_RC_RANGE_PARAMETERS_3_UDW_PC)
#define ICL_DSC1_RC_RANGE_PARAMETERS_3(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_3_PB, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_3_PC)
#define ICL_DSC1_RC_RANGE_PARAMETERS_3_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_3_UDW_PB, \
							_ICL_DSC1_RC_RANGE_PARAMETERS_3_UDW_PC)

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
/*
 * HDMI/DP bits are g4x+
 *
 * WARNING: Bspec for hpd status bits on gen4 seems to be completely confused.
 * Please check the detailed lore in the commit message for for experimental
 * evidence.
 */
/* Bspec says GM45 should match G4X/VLV/CHV, but reality disagrees */
#define   PORTD_HOTPLUG_LIVE_STATUS_GM45	(1 << 29)
#define   PORTC_HOTPLUG_LIVE_STATUS_GM45	(1 << 28)
#define   PORTB_HOTPLUG_LIVE_STATUS_GM45	(1 << 27)
/* G4X/VLV/CHV DP/HDMI bits again match Bspec */
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
#define PORT_DFT2_G4X		_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61154)
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


/* DVO port control */
#define _DVOA			0x61120
#define DVOA			_MMIO(_DVOA)
#define _DVOB			0x61140
#define DVOB			_MMIO(_DVOB)
#define _DVOC			0x61160
#define DVOC			_MMIO(_DVOC)
#define   DVO_ENABLE			(1 << 31)
#define   DVO_PIPE_SEL_SHIFT		30
#define   DVO_PIPE_SEL_MASK		(1 << 30)
#define   DVO_PIPE_SEL(pipe)		((pipe) << 30)
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
#define   DVO_PRESERVE_MASK		(0x7 << 24)
#define DVOA_SRCDIM		_MMIO(0x61124)
#define DVOB_SRCDIM		_MMIO(0x61144)
#define DVOC_SRCDIM		_MMIO(0x61164)
#define   DVO_SRCDIM_HORIZONTAL_SHIFT	12
#define   DVO_SRCDIM_VERTICAL_SHIFT	0

/* LVDS port control */
#define LVDS			_MMIO(0x61180)
/*
 * Enables the LVDS port.  This bit must be set before DPLLs are enabled, as
 * the DPLL semantics change when the LVDS is assigned to that pipe.
 */
#define   LVDS_PORT_EN			(1 << 31)
/* Selects pipe B for LVDS data.  Must be set on pre-965. */
#define   LVDS_PIPE_SEL_SHIFT		30
#define   LVDS_PIPE_SEL_MASK		(1 << 30)
#define   LVDS_PIPE_SEL(pipe)		((pipe) << 30)
#define   LVDS_PIPE_SEL_SHIFT_CPT	29
#define   LVDS_PIPE_SEL_MASK_CPT	(3 << 29)
#define   LVDS_PIPE_SEL_CPT(pipe)	((pipe) << 29)
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
#define VIDEO_DIP_DATA		_MMIO(0x61178)
/* Read the description of VIDEO_DIP_DATA (before Haswell) or VIDEO_DIP_ECC
 * (Haswell and newer) to see which VIDEO_DIP_DATA byte corresponds to each byte
 * of the infoframe structure specified by CEA-861. */
#define   VIDEO_DIP_DATA_SIZE	32
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

/* Panel power sequencing */
#define PPS_BASE			0x61200
#define VLV_PPS_BASE			(VLV_DISPLAY_BASE + PPS_BASE)
#define PCH_PPS_BASE			0xC7200

#define _MMIO_PPS(pps_idx, reg)		_MMIO(dev_priv->pps_mmio_base -	\
					      PPS_BASE + (reg) +	\
					      (pps_idx) * 0x100)

#define _PP_STATUS			0x61200
#define PP_STATUS(pps_idx)		_MMIO_PPS(pps_idx, _PP_STATUS)
#define   PP_ON				REG_BIT(31)
/*
 * Indicates that all dependencies of the panel are on:
 *
 * - PLL enabled
 * - pipe enabled
 * - LVDS/DVOB/DVOC on
 */
#define   PP_READY			REG_BIT(30)
#define   PP_SEQUENCE_MASK		REG_GENMASK(29, 28)
#define   PP_SEQUENCE_NONE		REG_FIELD_PREP(PP_SEQUENCE_MASK, 0)
#define   PP_SEQUENCE_POWER_UP		REG_FIELD_PREP(PP_SEQUENCE_MASK, 1)
#define   PP_SEQUENCE_POWER_DOWN	REG_FIELD_PREP(PP_SEQUENCE_MASK, 2)
#define   PP_CYCLE_DELAY_ACTIVE		REG_BIT(27)
#define   PP_SEQUENCE_STATE_MASK	REG_GENMASK(3, 0)
#define   PP_SEQUENCE_STATE_OFF_IDLE	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x0)
#define   PP_SEQUENCE_STATE_OFF_S0_1	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x1)
#define   PP_SEQUENCE_STATE_OFF_S0_2	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x2)
#define   PP_SEQUENCE_STATE_OFF_S0_3	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x3)
#define   PP_SEQUENCE_STATE_ON_IDLE	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x8)
#define   PP_SEQUENCE_STATE_ON_S1_1	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x9)
#define   PP_SEQUENCE_STATE_ON_S1_2	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0xa)
#define   PP_SEQUENCE_STATE_ON_S1_3	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0xb)
#define   PP_SEQUENCE_STATE_RESET	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0xf)

#define _PP_CONTROL			0x61204
#define PP_CONTROL(pps_idx)		_MMIO_PPS(pps_idx, _PP_CONTROL)
#define  PANEL_UNLOCK_MASK		REG_GENMASK(31, 16)
#define  PANEL_UNLOCK_REGS		REG_FIELD_PREP(PANEL_UNLOCK_MASK, 0xabcd)
#define  BXT_POWER_CYCLE_DELAY_MASK	REG_GENMASK(8, 4)
#define  EDP_FORCE_VDD			REG_BIT(3)
#define  EDP_BLC_ENABLE			REG_BIT(2)
#define  PANEL_POWER_RESET		REG_BIT(1)
#define  PANEL_POWER_ON			REG_BIT(0)

#define _PP_ON_DELAYS			0x61208
#define PP_ON_DELAYS(pps_idx)		_MMIO_PPS(pps_idx, _PP_ON_DELAYS)
#define  PANEL_PORT_SELECT_MASK		REG_GENMASK(31, 30)
#define  PANEL_PORT_SELECT_LVDS		REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, 0)
#define  PANEL_PORT_SELECT_DPA		REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, 1)
#define  PANEL_PORT_SELECT_DPC		REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, 2)
#define  PANEL_PORT_SELECT_DPD		REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, 3)
#define  PANEL_PORT_SELECT_VLV(port)	REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, port)
#define  PANEL_POWER_UP_DELAY_MASK	REG_GENMASK(28, 16)
#define  PANEL_LIGHT_ON_DELAY_MASK	REG_GENMASK(12, 0)

#define _PP_OFF_DELAYS			0x6120C
#define PP_OFF_DELAYS(pps_idx)		_MMIO_PPS(pps_idx, _PP_OFF_DELAYS)
#define  PANEL_POWER_DOWN_DELAY_MASK	REG_GENMASK(28, 16)
#define  PANEL_LIGHT_OFF_DELAY_MASK	REG_GENMASK(12, 0)

#define _PP_DIVISOR			0x61210
#define PP_DIVISOR(pps_idx)		_MMIO_PPS(pps_idx, _PP_DIVISOR)
#define  PP_REFERENCE_DIVIDER_MASK	REG_GENMASK(31, 8)
#define  PANEL_POWER_CYCLE_DELAY_MASK	REG_GENMASK(4, 0)

/* Panel fitting */
#define PFIT_CONTROL	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61230)
#define   PFIT_ENABLE		(1 << 31)
#define   PFIT_PIPE_MASK	(3 << 29)
#define   PFIT_PIPE_SHIFT	29
#define   PFIT_PIPE(pipe)	((pipe) << 29)
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
#define PFIT_PGM_RATIOS _MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61234)
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

#define PFIT_AUTO_RATIOS _MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61238)

#define _VLV_BLC_PWM_CTL2_A (DISPLAY_MMIO_BASE(dev_priv) + 0x61250)
#define _VLV_BLC_PWM_CTL2_B (DISPLAY_MMIO_BASE(dev_priv) + 0x61350)
#define VLV_BLC_PWM_CTL2(pipe) _MMIO_PIPE(pipe, _VLV_BLC_PWM_CTL2_A, \
					 _VLV_BLC_PWM_CTL2_B)

#define _VLV_BLC_PWM_CTL_A (DISPLAY_MMIO_BASE(dev_priv) + 0x61254)
#define _VLV_BLC_PWM_CTL_B (DISPLAY_MMIO_BASE(dev_priv) + 0x61354)
#define VLV_BLC_PWM_CTL(pipe) _MMIO_PIPE(pipe, _VLV_BLC_PWM_CTL_A, \
					_VLV_BLC_PWM_CTL_B)

#define _VLV_BLC_HIST_CTL_A (DISPLAY_MMIO_BASE(dev_priv) + 0x61260)
#define _VLV_BLC_HIST_CTL_B (DISPLAY_MMIO_BASE(dev_priv) + 0x61360)
#define VLV_BLC_HIST_CTL(pipe) _MMIO_PIPE(pipe, _VLV_BLC_HIST_CTL_A, \
					 _VLV_BLC_HIST_CTL_B)

/* Backlight control */
#define BLC_PWM_CTL2	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61250) /* 965+ only */
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
#define BLC_PWM_CTL	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61254)
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

#define BLC_HIST_CTL	_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x61260)
#define  BLM_HISTOGRAM_ENABLE			(1 << 31)

/* New registers for PCH-split platforms. Safe where new bits show up, the
 * register layout machtes with gen4 BLC_PWM_CTL[12]. */
#define BLC_PWM_CPU_CTL2	_MMIO(0x48250)
#define BLC_PWM_CPU_CTL		_MMIO(0x48254)

#define HSW_BLC_PWM2_CTL	_MMIO(0x48350)

/* PCH CTL1 is totally different, all but the below bits are reserved. CTL2 is
 * like the normal CTL from gen4 and earlier. Hooray for confusing naming. */
#define BLC_PWM_PCH_CTL1	_MMIO(0xc8250)
#define   BLM_PCH_PWM_ENABLE			(1 << 31)
#define   BLM_PCH_OVERRIDE_ENABLE		(1 << 30)
#define   BLM_PCH_POLARITY			(1 << 29)
#define BLC_PWM_PCH_CTL2	_MMIO(0xc8254)

#define UTIL_PIN_CTL			_MMIO(0x48400)
#define   UTIL_PIN_ENABLE		(1 << 31)
#define   UTIL_PIN_PIPE_MASK		(3 << 29)
#define   UTIL_PIN_PIPE(x)		((x) << 29)
#define   UTIL_PIN_MODE_MASK		(0xf << 24)
#define   UTIL_PIN_MODE_DATA		(0 << 24)
#define   UTIL_PIN_MODE_PWM		(1 << 24)
#define   UTIL_PIN_MODE_VBLANK		(4 << 24)
#define   UTIL_PIN_MODE_VSYNC		(5 << 24)
#define   UTIL_PIN_MODE_EYE_LEVEL	(8 << 24)
#define   UTIL_PIN_OUTPUT_DATA		(1 << 23)
#define   UTIL_PIN_POLARITY		(1 << 22)
#define   UTIL_PIN_DIRECTION_INPUT	(1 << 19)
#define   UTIL_PIN_INPUT_DATA		(1 << 16)

/* BXT backlight register definition. */
#define _BXT_BLC_PWM_CTL1			0xC8250
#define   BXT_BLC_PWM_ENABLE			(1 << 31)
#define   BXT_BLC_PWM_POLARITY			(1 << 29)
#define _BXT_BLC_PWM_FREQ1			0xC8254
#define _BXT_BLC_PWM_DUTY1			0xC8258

#define _BXT_BLC_PWM_CTL2			0xC8350
#define _BXT_BLC_PWM_FREQ2			0xC8354
#define _BXT_BLC_PWM_DUTY2			0xC8358

#define BXT_BLC_PWM_CTL(controller)    _MMIO_PIPE(controller,		\
					_BXT_BLC_PWM_CTL1, _BXT_BLC_PWM_CTL2)
#define BXT_BLC_PWM_FREQ(controller)   _MMIO_PIPE(controller, \
					_BXT_BLC_PWM_FREQ1, _BXT_BLC_PWM_FREQ2)
#define BXT_BLC_PWM_DUTY(controller)   _MMIO_PIPE(controller, \
					_BXT_BLC_PWM_DUTY1, _BXT_BLC_PWM_DUTY2)

#define PCH_GTC_CTL		_MMIO(0xe7000)
#define   PCH_GTC_ENABLE	(1 << 31)

/* TV port control */
#define TV_CTL			_MMIO(0x68000)
/* Enables the TV encoder */
# define TV_ENC_ENABLE			(1 << 31)
/* Sources the TV encoder input from pipe B instead of A. */
# define TV_ENC_PIPE_SEL_SHIFT		30
# define TV_ENC_PIPE_SEL_MASK		(1 << 30)
# define TV_ENC_PIPE_SEL(pipe)		((pipe) << 30)
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
# define TV_OVERSAMPLE_MASK		(3 << 18)
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

#define TV_DAC			_MMIO(0x68004)
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
#define TV_CSC_Y		_MMIO(0x68010)
# define TV_RY_MASK			0x07ff0000
# define TV_RY_SHIFT			16
# define TV_GY_MASK			0x00000fff
# define TV_GY_SHIFT			0

#define TV_CSC_Y2		_MMIO(0x68014)
# define TV_BY_MASK			0x07ff0000
# define TV_BY_SHIFT			16
/*
 * Y attenuation for component video.
 *
 * Stored in 1.9 fixed point.
 */
# define TV_AY_MASK			0x000003ff
# define TV_AY_SHIFT			0

#define TV_CSC_U		_MMIO(0x68018)
# define TV_RU_MASK			0x07ff0000
# define TV_RU_SHIFT			16
# define TV_GU_MASK			0x000007ff
# define TV_GU_SHIFT			0

#define TV_CSC_U2		_MMIO(0x6801c)
# define TV_BU_MASK			0x07ff0000
# define TV_BU_SHIFT			16
/*
 * U attenuation for component video.
 *
 * Stored in 1.9 fixed point.
 */
# define TV_AU_MASK			0x000003ff
# define TV_AU_SHIFT			0

#define TV_CSC_V		_MMIO(0x68020)
# define TV_RV_MASK			0x0fff0000
# define TV_RV_SHIFT			16
# define TV_GV_MASK			0x000007ff
# define TV_GV_SHIFT			0

#define TV_CSC_V2		_MMIO(0x68024)
# define TV_BV_MASK			0x07ff0000
# define TV_BV_SHIFT			16
/*
 * V attenuation for component video.
 *
 * Stored in 1.9 fixed point.
 */
# define TV_AV_MASK			0x000007ff
# define TV_AV_SHIFT			0

#define TV_CLR_KNOBS		_MMIO(0x68028)
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

#define TV_CLR_LEVEL		_MMIO(0x6802c)
/* Controls the DAC level for black */
# define TV_BLACK_LEVEL_MASK		0x01ff0000
# define TV_BLACK_LEVEL_SHIFT		16
/* Controls the DAC level for blanking */
# define TV_BLANK_LEVEL_MASK		0x000001ff
# define TV_BLANK_LEVEL_SHIFT		0

#define TV_H_CTL_1		_MMIO(0x68030)
/* Number of pixels in the hsync. */
# define TV_HSYNC_END_MASK		0x1fff0000
# define TV_HSYNC_END_SHIFT		16
/* Total number of pixels minus one in the line (display and blanking). */
# define TV_HTOTAL_MASK			0x00001fff
# define TV_HTOTAL_SHIFT		0

#define TV_H_CTL_2		_MMIO(0x68034)
/* Enables the colorburst (needed for non-component color) */
# define TV_BURST_ENA			(1 << 31)
/* Offset of the colorburst from the start of hsync, in pixels minus one. */
# define TV_HBURST_START_SHIFT		16
# define TV_HBURST_START_MASK		0x1fff0000
/* Length of the colorburst */
# define TV_HBURST_LEN_SHIFT		0
# define TV_HBURST_LEN_MASK		0x0001fff

#define TV_H_CTL_3		_MMIO(0x68038)
/* End of hblank, measured in pixels minus one from start of hsync */
# define TV_HBLANK_END_SHIFT		16
# define TV_HBLANK_END_MASK		0x1fff0000
/* Start of hblank, measured in pixels minus one from start of hsync */
# define TV_HBLANK_START_SHIFT		0
# define TV_HBLANK_START_MASK		0x0001fff

#define TV_V_CTL_1		_MMIO(0x6803c)
/* XXX */
# define TV_NBR_END_SHIFT		16
# define TV_NBR_END_MASK		0x07ff0000
/* XXX */
# define TV_VI_END_F1_SHIFT		8
# define TV_VI_END_F1_MASK		0x00003f00
/* XXX */
# define TV_VI_END_F2_SHIFT		0
# define TV_VI_END_F2_MASK		0x0000003f

#define TV_V_CTL_2		_MMIO(0x68040)
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

#define TV_V_CTL_3		_MMIO(0x68044)
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

#define TV_V_CTL_4		_MMIO(0x68048)
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

#define TV_V_CTL_5		_MMIO(0x6804c)
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

#define TV_V_CTL_6		_MMIO(0x68050)
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

#define TV_V_CTL_7		_MMIO(0x68054)
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

#define TV_SC_CTL_1		_MMIO(0x68060)
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

#define TV_SC_CTL_2		_MMIO(0x68064)
/* Sets the rollover for the second subcarrier phase generation DDA */
# define TV_SCDDA2_SIZE_MASK		0x7fff0000
# define TV_SCDDA2_SIZE_SHIFT		16
/* Sets the increent of the second subcarrier phase generation DDA */
# define TV_SCDDA2_INC_MASK		0x00007fff
# define TV_SCDDA2_INC_SHIFT		0

#define TV_SC_CTL_3		_MMIO(0x68068)
/* Sets the rollover for the third subcarrier phase generation DDA */
# define TV_SCDDA3_SIZE_MASK		0x7fff0000
# define TV_SCDDA3_SIZE_SHIFT		16
/* Sets the increent of the third subcarrier phase generation DDA */
# define TV_SCDDA3_INC_MASK		0x00007fff
# define TV_SCDDA3_INC_SHIFT		0

#define TV_WIN_POS		_MMIO(0x68070)
/* X coordinate of the display from the start of horizontal active */
# define TV_XPOS_MASK			0x1fff0000
# define TV_XPOS_SHIFT			16
/* Y coordinate of the display from the start of vertical active (NBR) */
# define TV_YPOS_MASK			0x00000fff
# define TV_YPOS_SHIFT			0

#define TV_WIN_SIZE		_MMIO(0x68074)
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

#define TV_FILTER_CTL_1		_MMIO(0x68080)
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

#define TV_FILTER_CTL_2		_MMIO(0x68084)
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

#define TV_FILTER_CTL_3		_MMIO(0x68088)
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

#define TV_CC_CONTROL		_MMIO(0x68090)
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

#define TV_CC_DATA		_MMIO(0x68094)
# define TV_CC_RDY			(1 << 31)
/* Second word of CC data to be transmitted. */
# define TV_CC_DATA_2_MASK		0x007f0000
# define TV_CC_DATA_2_SHIFT		16
/* First word of CC data to be transmitted. */
# define TV_CC_DATA_1_MASK		0x0000007f
# define TV_CC_DATA_1_SHIFT		0

#define TV_H_LUMA(i)		_MMIO(0x68100 + (i) * 4) /* 60 registers */
#define TV_H_CHROMA(i)		_MMIO(0x68200 + (i) * 4) /* 60 registers */
#define TV_V_LUMA(i)		_MMIO(0x68300 + (i) * 4) /* 43 registers */
#define TV_V_CHROMA(i)		_MMIO(0x68400 + (i) * 4) /* 43 registers */

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

/* The aux channel provides a way to talk to the
 * signal sink for DDC etc. Max packet size supported
 * is 20 bytes in each direction, hence the 5 fixed
 * data registers
 */
#define _DPA_AUX_CH_CTL		(DISPLAY_MMIO_BASE(dev_priv) + 0x64010)
#define _DPA_AUX_CH_DATA1	(DISPLAY_MMIO_BASE(dev_priv) + 0x64014)

#define _DPB_AUX_CH_CTL		(DISPLAY_MMIO_BASE(dev_priv) + 0x64110)
#define _DPB_AUX_CH_DATA1	(DISPLAY_MMIO_BASE(dev_priv) + 0x64114)

#define DP_AUX_CH_CTL(aux_ch)	_MMIO_PORT(aux_ch, _DPA_AUX_CH_CTL, _DPB_AUX_CH_CTL)
#define DP_AUX_CH_DATA(aux_ch, i)	_MMIO(_PORT(aux_ch, _DPA_AUX_CH_DATA1, _DPB_AUX_CH_DATA1) + (i) * 4) /* 5 registers */

#define   DP_AUX_CH_CTL_SEND_BUSY	    (1 << 31)
#define   DP_AUX_CH_CTL_DONE		    (1 << 30)
#define   DP_AUX_CH_CTL_INTERRUPT	    (1 << 29)
#define   DP_AUX_CH_CTL_TIME_OUT_ERROR	    (1 << 28)
#define   DP_AUX_CH_CTL_TIME_OUT_400us	    (0 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_600us	    (1 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_800us	    (2 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_MAX	    (3 << 26) /* Varies per platform */
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
#define   DP_AUX_CH_CTL_PSR_DATA_AUX_REG_SKL	(1 << 14)
#define   DP_AUX_CH_CTL_FS_DATA_AUX_REG_SKL	(1 << 13)
#define   DP_AUX_CH_CTL_GTC_DATA_AUX_REG_SKL	(1 << 12)
#define   DP_AUX_CH_CTL_TBT_IO			(1 << 11)
#define   DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL_MASK (0x1f << 5)
#define   DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(c) (((c) - 1) << 5)
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

/* Display & cursor control */

/* Pipe A */
#define _PIPEADSL		0x70000
#define   PIPEDSL_CURR_FIELD	REG_BIT(31) /* ctg+ */
#define   PIPEDSL_LINE_MASK	REG_GENMASK(19, 0)
#define _PIPEACONF		0x70008
#define   PIPECONF_ENABLE			REG_BIT(31)
#define   PIPECONF_DOUBLE_WIDE			REG_BIT(30) /* pre-i965 */
#define   PIPECONF_STATE_ENABLE			REG_BIT(30) /* i965+ */
#define   PIPECONF_DSI_PLL_LOCKED		REG_BIT(29) /* vlv & pipe A only */
#define   PIPECONF_FRAME_START_DELAY_MASK	REG_GENMASK(28, 27) /* pre-hsw */
#define   PIPECONF_FRAME_START_DELAY(x)		REG_FIELD_PREP(PIPECONF_FRAME_START_DELAY_MASK, (x)) /* pre-hsw: 0-3 */
#define   PIPECONF_PIPE_LOCKED			REG_BIT(25)
#define   PIPECONF_FORCE_BORDER			REG_BIT(25)
#define   PIPECONF_GAMMA_MODE_MASK_I9XX		REG_BIT(24) /* gmch */
#define   PIPECONF_GAMMA_MODE_MASK_ILK		REG_GENMASK(25, 24) /* ilk-ivb */
#define   PIPECONF_GAMMA_MODE_8BIT		REG_FIELD_PREP(PIPECONF_GAMMA_MODE_MASK, 0)
#define   PIPECONF_GAMMA_MODE_10BIT		REG_FIELD_PREP(PIPECONF_GAMMA_MODE_MASK, 1)
#define   PIPECONF_GAMMA_MODE_12BIT		REG_FIELD_PREP(PIPECONF_GAMMA_MODE_MASK_ILK, 2) /* ilk-ivb */
#define   PIPECONF_GAMMA_MODE_SPLIT		REG_FIELD_PREP(PIPECONF_GAMMA_MODE_MASK_ILK, 3) /* ivb */
#define   PIPECONF_GAMMA_MODE(x)		REG_FIELD_PREP(PIPECONF_GAMMA_MODE_MASK_ILK, (x)) /* pass in GAMMA_MODE_MODE_* */
#define   PIPECONF_INTERLACE_MASK		REG_GENMASK(23, 21) /* gen3+ */
#define   PIPECONF_INTERLACE_PROGRESSIVE	REG_FIELD_PREP(PIPECONF_INTERLACE_MASK, 0)
#define   PIPECONF_INTERLACE_W_SYNC_SHIFT_PANEL	REG_FIELD_PREP(PIPECONF_INTERLACE_MASK, 4) /* gen4 only */
#define   PIPECONF_INTERLACE_W_SYNC_SHIFT	REG_FIELD_PREP(PIPECONF_INTERLACE_MASK, 5) /* gen4 only */
#define   PIPECONF_INTERLACE_W_FIELD_INDICATION	REG_FIELD_PREP(PIPECONF_INTERLACE_MASK, 6)
#define   PIPECONF_INTERLACE_FIELD_0_ONLY	REG_FIELD_PREP(PIPECONF_INTERLACE_MASK, 7) /* gen3 only */
/*
 * ilk+: PF/D=progressive fetch/display, IF/D=interlaced fetch/display,
 * DBL=power saving pixel doubling, PF-ID* requires panel fitter
 */
#define   PIPECONF_INTERLACE_MASK_ILK		REG_GENMASK(23, 21) /* ilk+ */
#define   PIPECONF_INTERLACE_MASK_HSW		REG_GENMASK(22, 21) /* hsw+ */
#define   PIPECONF_INTERLACE_PF_PD_ILK		REG_FIELD_PREP(PIPECONF_INTERLACE_MASK_ILK, 0)
#define   PIPECONF_INTERLACE_PF_ID_ILK		REG_FIELD_PREP(PIPECONF_INTERLACE_MASK_ILK, 1)
#define   PIPECONF_INTERLACE_IF_ID_ILK		REG_FIELD_PREP(PIPECONF_INTERLACE_MASK_ILK, 3)
#define   PIPECONF_INTERLACE_IF_ID_DBL_ILK	REG_FIELD_PREP(PIPECONF_INTERLACE_MASK_ILK, 4) /* ilk/snb only */
#define   PIPECONF_INTERLACE_PF_ID_DBL_ILK	REG_FIELD_PREP(PIPECONF_INTERLACE_MASK_ILK, 5) /* ilk/snb only */
#define   PIPECONF_EDP_RR_MODE_SWITCH		REG_BIT(20)
#define   PIPECONF_CXSR_DOWNCLOCK		REG_BIT(16)
#define   PIPECONF_EDP_RR_MODE_SWITCH_VLV	REG_BIT(14)
#define   PIPECONF_COLOR_RANGE_SELECT		REG_BIT(13)
#define   PIPECONF_OUTPUT_COLORSPACE_MASK	REG_GENMASK(12, 11) /* ilk-ivb */
#define   PIPECONF_OUTPUT_COLORSPACE_RGB	REG_FIELD_PREP(PIPECONF_OUTPUT_COLORSPACE_MASK, 0) /* ilk-ivb */
#define   PIPECONF_OUTPUT_COLORSPACE_YUV601	REG_FIELD_PREP(PIPECONF_OUTPUT_COLORSPACE_MASK, 1) /* ilk-ivb */
#define   PIPECONF_OUTPUT_COLORSPACE_YUV709	REG_FIELD_PREP(PIPECONF_OUTPUT_COLORSPACE_MASK, 2) /* ilk-ivb */
#define   PIPECONF_OUTPUT_COLORSPACE_YUV_HSW	REG_BIT(11) /* hsw only */
#define   PIPECONF_BPC_MASK			REG_GENMASK(7, 5) /* ctg-ivb */
#define   PIPECONF_BPC_8			REG_FIELD_PREP(PIPECONF_BPC_MASK, 0)
#define   PIPECONF_BPC_10			REG_FIELD_PREP(PIPECONF_BPC_MASK, 1)
#define   PIPECONF_BPC_6			REG_FIELD_PREP(PIPECONF_BPC_MASK, 2)
#define   PIPECONF_BPC_12			REG_FIELD_PREP(PIPECONF_BPC_MASK, 3)
#define   PIPECONF_DITHER_EN			REG_BIT(4)
#define   PIPECONF_DITHER_TYPE_MASK		REG_GENMASK(3, 2)
#define   PIPECONF_DITHER_TYPE_SP		REG_FIELD_PREP(PIPECONF_DITHER_TYPE_MASK, 0)
#define   PIPECONF_DITHER_TYPE_ST1		REG_FIELD_PREP(PIPECONF_DITHER_TYPE_MASK, 1)
#define   PIPECONF_DITHER_TYPE_ST2		REG_FIELD_PREP(PIPECONF_DITHER_TYPE_MASK, 2)
#define   PIPECONF_DITHER_TYPE_TEMP		REG_FIELD_PREP(PIPECONF_DITHER_TYPE_MASK, 3)
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

#define PIPE_A_OFFSET		0x70000
#define PIPE_B_OFFSET		0x71000
#define PIPE_C_OFFSET		0x72000
#define PIPE_D_OFFSET		0x73000
#define CHV_PIPE_C_OFFSET	0x74000
/*
 * There's actually no pipe EDP. Some pipe registers have
 * simply shifted from the pipe to the transcoder, while
 * keeping their original offset. Thus we need PIPE_EDP_OFFSET
 * to access such registers in transcoder EDP.
 */
#define PIPE_EDP_OFFSET	0x7f000

/* ICL DSI 0 and 1 */
#define PIPE_DSI0_OFFSET	0x7b000
#define PIPE_DSI1_OFFSET	0x7b800

#define PIPECONF(pipe)		_MMIO_PIPE2(pipe, _PIPEACONF)
#define PIPEDSL(pipe)		_MMIO_PIPE2(pipe, _PIPEADSL)
#define PIPEFRAME(pipe)		_MMIO_PIPE2(pipe, _PIPEAFRAMEHIGH)
#define PIPEFRAMEPIXEL(pipe)	_MMIO_PIPE2(pipe, _PIPEAFRAMEPIXEL)
#define PIPESTAT(pipe)		_MMIO_PIPE2(pipe, _PIPEASTAT)

#define  _PIPEAGCMAX           0x70010
#define  _PIPEBGCMAX           0x71010
#define PIPEGCMAX(pipe, i)     _MMIO_PIPE2(pipe, _PIPEAGCMAX + (i) * 4)

#define _PIPE_ARB_CTL_A			0x70028 /* icl+ */
#define PIPE_ARB_CTL(pipe)		_MMIO_PIPE2(pipe, _PIPE_ARB_CTL_A)
#define   PIPE_ARB_USE_PROG_SLOTS	REG_BIT(13)

#define _PIPE_MISC_A			0x70030
#define _PIPE_MISC_B			0x71030
#define   PIPEMISC_YUV420_ENABLE		REG_BIT(27) /* glk+ */
#define   PIPEMISC_YUV420_MODE_FULL_BLEND	REG_BIT(26) /* glk+ */
#define   PIPEMISC_HDR_MODE_PRECISION		REG_BIT(23) /* icl+ */
#define   PIPEMISC_OUTPUT_COLORSPACE_YUV	REG_BIT(11)
#define   PIPEMISC_PIXEL_ROUNDING_TRUNC		REG_BIT(8) /* tgl+ */
/*
 * For Display < 13, Bits 5-7 of PIPE MISC represent DITHER BPC with
 * valid values of: 6, 8, 10 BPC.
 * ADLP+, the bits 5-7 represent PORT OUTPUT BPC with valid values of:
 * 6, 8, 10, 12 BPC.
 */
#define   PIPEMISC_BPC_MASK			REG_GENMASK(7, 5)
#define   PIPEMISC_BPC_8			REG_FIELD_PREP(PIPEMISC_BPC_MASK, 0)
#define   PIPEMISC_BPC_10			REG_FIELD_PREP(PIPEMISC_BPC_MASK, 1)
#define   PIPEMISC_BPC_6			REG_FIELD_PREP(PIPEMISC_BPC_MASK, 2)
#define   PIPEMISC_BPC_12_ADLP			REG_FIELD_PREP(PIPEMISC_BPC_MASK, 4) /* adlp+ */
#define   PIPEMISC_DITHER_ENABLE		REG_BIT(4)
#define   PIPEMISC_DITHER_TYPE_MASK		REG_GENMASK(3, 2)
#define   PIPEMISC_DITHER_TYPE_SP		REG_FIELD_PREP(PIPEMISC_DITHER_TYPE_MASK, 0)
#define   PIPEMISC_DITHER_TYPE_ST1		REG_FIELD_PREP(PIPEMISC_DITHER_TYPE_MASK, 1)
#define   PIPEMISC_DITHER_TYPE_ST2		REG_FIELD_PREP(PIPEMISC_DITHER_TYPE_MASK, 2)
#define   PIPEMISC_DITHER_TYPE_TEMP		REG_FIELD_PREP(PIPEMISC_DITHER_TYPE_MASK, 3)
#define PIPEMISC(pipe)			_MMIO_PIPE2(pipe, _PIPE_MISC_A)

#define _PIPE_MISC2_A					0x7002C
#define _PIPE_MISC2_B					0x7102C
#define   PIPE_MISC2_BUBBLE_COUNTER_MASK	REG_GENMASK(31, 24)
#define   PIPE_MISC2_BUBBLE_COUNTER_SCALER_EN	REG_FIELD_PREP(PIPE_MISC2_BUBBLE_COUNTER_MASK, 80)
#define   PIPE_MISC2_BUBBLE_COUNTER_SCALER_DIS	REG_FIELD_PREP(PIPE_MISC2_BUBBLE_COUNTER_MASK, 20)
#define PIPE_MISC2(pipe)					_MMIO_PIPE2(pipe, _PIPE_MISC2_A)

/* Skylake+ pipe bottom (background) color */
#define _SKL_BOTTOM_COLOR_A		0x70034
#define   SKL_BOTTOM_COLOR_GAMMA_ENABLE		REG_BIT(31)
#define   SKL_BOTTOM_COLOR_CSC_ENABLE		REG_BIT(30)
#define SKL_BOTTOM_COLOR(pipe)		_MMIO_PIPE2(pipe, _SKL_BOTTOM_COLOR_A)

#define _ICL_PIPE_A_STATUS			0x70058
#define ICL_PIPESTATUS(pipe)			_MMIO_PIPE2(pipe, _ICL_PIPE_A_STATUS)
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

/* Watermark register definitions for SKL */
#define _CUR_WM_A_0		0x70140
#define _CUR_WM_B_0		0x71140
#define _CUR_WM_SAGV_A		0x70158
#define _CUR_WM_SAGV_B		0x71158
#define _CUR_WM_SAGV_TRANS_A	0x7015C
#define _CUR_WM_SAGV_TRANS_B	0x7115C
#define _CUR_WM_TRANS_A		0x70168
#define _CUR_WM_TRANS_B		0x71168
#define _PLANE_WM_1_A_0		0x70240
#define _PLANE_WM_1_B_0		0x71240
#define _PLANE_WM_2_A_0		0x70340
#define _PLANE_WM_2_B_0		0x71340
#define _PLANE_WM_SAGV_1_A	0x70258
#define _PLANE_WM_SAGV_1_B	0x71258
#define _PLANE_WM_SAGV_2_A	0x70358
#define _PLANE_WM_SAGV_2_B	0x71358
#define _PLANE_WM_SAGV_TRANS_1_A	0x7025C
#define _PLANE_WM_SAGV_TRANS_1_B	0x7125C
#define _PLANE_WM_SAGV_TRANS_2_A	0x7035C
#define _PLANE_WM_SAGV_TRANS_2_B	0x7135C
#define _PLANE_WM_TRANS_1_A	0x70268
#define _PLANE_WM_TRANS_1_B	0x71268
#define _PLANE_WM_TRANS_2_A	0x70368
#define _PLANE_WM_TRANS_2_B	0x71368
#define   PLANE_WM_EN		(1 << 31)
#define   PLANE_WM_IGNORE_LINES	(1 << 30)
#define   PLANE_WM_LINES_MASK	REG_GENMASK(26, 14)
#define   PLANE_WM_BLOCKS_MASK	REG_GENMASK(11, 0)

#define _CUR_WM_0(pipe) _PIPE(pipe, _CUR_WM_A_0, _CUR_WM_B_0)
#define CUR_WM(pipe, level) _MMIO(_CUR_WM_0(pipe) + ((4) * (level)))
#define CUR_WM_SAGV(pipe) _MMIO_PIPE(pipe, _CUR_WM_SAGV_A, _CUR_WM_SAGV_B)
#define CUR_WM_SAGV_TRANS(pipe) _MMIO_PIPE(pipe, _CUR_WM_SAGV_TRANS_A, _CUR_WM_SAGV_TRANS_B)
#define CUR_WM_TRANS(pipe) _MMIO_PIPE(pipe, _CUR_WM_TRANS_A, _CUR_WM_TRANS_B)
#define _PLANE_WM_1(pipe) _PIPE(pipe, _PLANE_WM_1_A_0, _PLANE_WM_1_B_0)
#define _PLANE_WM_2(pipe) _PIPE(pipe, _PLANE_WM_2_A_0, _PLANE_WM_2_B_0)
#define _PLANE_WM_BASE(pipe, plane) \
	_PLANE(plane, _PLANE_WM_1(pipe), _PLANE_WM_2(pipe))
#define PLANE_WM(pipe, plane, level) \
	_MMIO(_PLANE_WM_BASE(pipe, plane) + ((4) * (level)))
#define _PLANE_WM_SAGV_1(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_1_A, _PLANE_WM_SAGV_1_B)
#define _PLANE_WM_SAGV_2(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_2_A, _PLANE_WM_SAGV_2_B)
#define PLANE_WM_SAGV(pipe, plane) \
	_MMIO(_PLANE(plane, _PLANE_WM_SAGV_1(pipe), _PLANE_WM_SAGV_2(pipe)))
#define _PLANE_WM_SAGV_TRANS_1(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_TRANS_1_A, _PLANE_WM_SAGV_TRANS_1_B)
#define _PLANE_WM_SAGV_TRANS_2(pipe) \
	_PIPE(pipe, _PLANE_WM_SAGV_TRANS_2_A, _PLANE_WM_SAGV_TRANS_2_B)
#define PLANE_WM_SAGV_TRANS(pipe, plane) \
	_MMIO(_PLANE(plane, _PLANE_WM_SAGV_TRANS_1(pipe), _PLANE_WM_SAGV_TRANS_2(pipe)))
#define _PLANE_WM_TRANS_1(pipe) \
	_PIPE(pipe, _PLANE_WM_TRANS_1_A, _PLANE_WM_TRANS_1_B)
#define _PLANE_WM_TRANS_2(pipe) \
	_PIPE(pipe, _PLANE_WM_TRANS_2_A, _PLANE_WM_TRANS_2_B)
#define PLANE_WM_TRANS(pipe, plane) \
	_MMIO(_PLANE(plane, _PLANE_WM_TRANS_1(pipe), _PLANE_WM_TRANS_2(pipe)))

/* define the Watermark register on Ironlake */
#define _WM0_PIPEA_ILK		0x45100
#define _WM0_PIPEB_ILK		0x45104
#define _WM0_PIPEC_IVB		0x45200
#define WM0_PIPE_ILK(pipe)	_MMIO_PIPE3((pipe), _WM0_PIPEA_ILK, \
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
#define PIPE_FRMCOUNT_G4X(pipe) _MMIO_PIPE2(pipe, _PIPEA_FRMCOUNT_G4X)
#define PIPE_FLIPCOUNT_G4X(pipe) _MMIO_PIPE2(pipe, _PIPEA_FLIPCOUNT_G4X)

/* Cursor A & B regs */
#define _CURACNTR		0x70080
/* Old style CUR*CNTR flags (desktop 8xx) */
#define   CURSOR_ENABLE			REG_BIT(31)
#define   CURSOR_PIPE_GAMMA_ENABLE	REG_BIT(30)
#define   CURSOR_STRIDE_MASK	REG_GENMASK(29, 28)
#define   CURSOR_STRIDE(stride)	REG_FIELD_PREP(CURSOR_STRIDE_MASK, ffs(stride) - 9) /* 256,512,1k,2k */
#define   CURSOR_FORMAT_MASK	REG_GENMASK(26, 24)
#define   CURSOR_FORMAT_2C	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 0)
#define   CURSOR_FORMAT_3C	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 1)
#define   CURSOR_FORMAT_4C	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 2)
#define   CURSOR_FORMAT_ARGB	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 4)
#define   CURSOR_FORMAT_XRGB	REG_FIELD_PREP(CURSOR_FORMAT_MASK, 5)
/* New style CUR*CNTR flags */
#define   MCURSOR_ARB_SLOTS_MASK	REG_GENMASK(30, 28) /* icl+ */
#define   MCURSOR_ARB_SLOTS(x)		REG_FIELD_PREP(MCURSOR_ARB_SLOTS_MASK, (x)) /* icl+ */
#define   MCURSOR_PIPE_SEL_MASK		REG_GENMASK(29, 28)
#define   MCURSOR_PIPE_SEL(pipe)	REG_FIELD_PREP(MCURSOR_PIPE_SEL_MASK, (pipe))
#define   MCURSOR_PIPE_GAMMA_ENABLE	REG_BIT(26)
#define   MCURSOR_PIPE_CSC_ENABLE	REG_BIT(24) /* ilk+ */
#define   MCURSOR_ROTATE_180		REG_BIT(15)
#define   MCURSOR_TRICKLE_FEED_DISABLE	REG_BIT(14)
#define   MCURSOR_MODE_MASK		0x27
#define   MCURSOR_MODE_DISABLE		0x00
#define   MCURSOR_MODE_128_32B_AX	0x02
#define   MCURSOR_MODE_256_32B_AX	0x03
#define   MCURSOR_MODE_64_32B_AX	0x07
#define   MCURSOR_MODE_128_ARGB_AX	(0x20 | MCURSOR_MODE_128_32B_AX)
#define   MCURSOR_MODE_256_ARGB_AX	(0x20 | MCURSOR_MODE_256_32B_AX)
#define   MCURSOR_MODE_64_ARGB_AX	(0x20 | MCURSOR_MODE_64_32B_AX)
#define _CURABASE		0x70084
#define _CURAPOS		0x70088
#define   CURSOR_POS_Y_SIGN		REG_BIT(31)
#define   CURSOR_POS_Y_MASK		REG_GENMASK(30, 16)
#define   CURSOR_POS_Y(y)		REG_FIELD_PREP(CURSOR_POS_Y_MASK, (y))
#define   CURSOR_POS_X_SIGN		REG_BIT(15)
#define   CURSOR_POS_X_MASK		REG_GENMASK(14, 0)
#define   CURSOR_POS_X(x)		REG_FIELD_PREP(CURSOR_POS_X_MASK, (x))
#define _CURASIZE		0x700a0 /* 845/865 */
#define   CURSOR_HEIGHT_MASK		REG_GENMASK(21, 12)
#define   CURSOR_HEIGHT(h)		REG_FIELD_PREP(CURSOR_HEIGHT_MASK, (h))
#define   CURSOR_WIDTH_MASK		REG_GENMASK(9, 0)
#define   CURSOR_WIDTH(w)		REG_FIELD_PREP(CURSOR_WIDTH_MASK, (w))
#define _CUR_FBC_CTL_A		0x700a0 /* ivb+ */
#define   CUR_FBC_EN			REG_BIT(31)
#define   CUR_FBC_HEIGHT_MASK		REG_GENMASK(7, 0)
#define   CUR_FBC_HEIGHT(h)		REG_FIELD_PREP(CUR_FBC_HEIGHT_MASK, (h))
#define _CURASURFLIVE		0x700ac /* g4x+ */
#define _CURBCNTR		0x700c0
#define _CURBBASE		0x700c4
#define _CURBPOS		0x700c8

#define _CURBCNTR_IVB		0x71080
#define _CURBBASE_IVB		0x71084
#define _CURBPOS_IVB		0x71088

#define CURCNTR(pipe) _CURSOR2(pipe, _CURACNTR)
#define CURBASE(pipe) _CURSOR2(pipe, _CURABASE)
#define CURPOS(pipe) _CURSOR2(pipe, _CURAPOS)
#define CURSIZE(pipe) _CURSOR2(pipe, _CURASIZE)
#define CUR_FBC_CTL(pipe) _CURSOR2(pipe, _CUR_FBC_CTL_A)
#define CURSURFLIVE(pipe) _CURSOR2(pipe, _CURASURFLIVE)

#define CURSOR_A_OFFSET 0x70080
#define CURSOR_B_OFFSET 0x700c0
#define CHV_CURSOR_C_OFFSET 0x700e0
#define IVB_CURSOR_B_OFFSET 0x71080
#define IVB_CURSOR_C_OFFSET 0x72080
#define TGL_CURSOR_D_OFFSET 0x73080

/* Display A control */
#define _DSPAADDR_VLV				0x7017C /* vlv/chv */
#define _DSPACNTR				0x70180
#define   DISP_ENABLE			REG_BIT(31)
#define   DISP_PIPE_GAMMA_ENABLE	REG_BIT(30)
#define   DISP_FORMAT_MASK		REG_GENMASK(29, 26)
#define   DISP_FORMAT_8BPP		REG_FIELD_PREP(DISP_FORMAT_MASK, 2)
#define   DISP_FORMAT_BGRA555		REG_FIELD_PREP(DISP_FORMAT_MASK, 3)
#define   DISP_FORMAT_BGRX555		REG_FIELD_PREP(DISP_FORMAT_MASK, 4)
#define   DISP_FORMAT_BGRX565		REG_FIELD_PREP(DISP_FORMAT_MASK, 5)
#define   DISP_FORMAT_BGRX888		REG_FIELD_PREP(DISP_FORMAT_MASK, 6)
#define   DISP_FORMAT_BGRA888		REG_FIELD_PREP(DISP_FORMAT_MASK, 7)
#define   DISP_FORMAT_RGBX101010	REG_FIELD_PREP(DISP_FORMAT_MASK, 8)
#define   DISP_FORMAT_RGBA101010	REG_FIELD_PREP(DISP_FORMAT_MASK, 9)
#define   DISP_FORMAT_BGRX101010	REG_FIELD_PREP(DISP_FORMAT_MASK, 10)
#define   DISP_FORMAT_BGRA101010	REG_FIELD_PREP(DISP_FORMAT_MASK, 11)
#define   DISP_FORMAT_RGBX161616	REG_FIELD_PREP(DISP_FORMAT_MASK, 12)
#define   DISP_FORMAT_RGBX888		REG_FIELD_PREP(DISP_FORMAT_MASK, 14)
#define   DISP_FORMAT_RGBA888		REG_FIELD_PREP(DISP_FORMAT_MASK, 15)
#define   DISP_STEREO_ENABLE		REG_BIT(25)
#define   DISP_PIPE_CSC_ENABLE		REG_BIT(24) /* ilk+ */
#define   DISP_PIPE_SEL_MASK		REG_GENMASK(25, 24)
#define   DISP_PIPE_SEL(pipe)		REG_FIELD_PREP(DISP_PIPE_SEL_MASK, (pipe))
#define   DISP_SRC_KEY_ENABLE		REG_BIT(22)
#define   DISP_LINE_DOUBLE		REG_BIT(20)
#define   DISP_STEREO_POLARITY_SECOND	REG_BIT(18)
#define   DISP_ALPHA_PREMULTIPLY	REG_BIT(16) /* CHV pipe B */
#define   DISP_ROTATE_180		REG_BIT(15)
#define   DISP_TRICKLE_FEED_DISABLE	REG_BIT(14) /* g4x+ */
#define   DISP_TILED			REG_BIT(10)
#define   DISP_ASYNC_FLIP		REG_BIT(9) /* g4x+ */
#define   DISP_MIRROR			REG_BIT(8) /* CHV pipe B */
#define _DSPAADDR				0x70184
#define _DSPASTRIDE				0x70188
#define _DSPAPOS				0x7018C /* reserved */
#define   DISP_POS_Y_MASK		REG_GENMASK(31, 0)
#define   DISP_POS_Y(y)			REG_FIELD_PREP(DISP_POS_Y_MASK, (y))
#define   DISP_POS_X_MASK		REG_GENMASK(15, 0)
#define   DISP_POS_X(x)			REG_FIELD_PREP(DISP_POS_X_MASK, (x))
#define _DSPASIZE				0x70190
#define   DISP_HEIGHT_MASK		REG_GENMASK(31, 0)
#define   DISP_HEIGHT(h)		REG_FIELD_PREP(DISP_HEIGHT_MASK, (h))
#define   DISP_WIDTH_MASK		REG_GENMASK(15, 0)
#define   DISP_WIDTH(w)			REG_FIELD_PREP(DISP_WIDTH_MASK, (w))
#define _DSPASURF				0x7019C /* 965+ only */
#define   DISP_ADDR_MASK		REG_GENMASK(31, 12)
#define _DSPATILEOFF				0x701A4 /* 965+ only */
#define   DISP_OFFSET_Y_MASK		REG_GENMASK(31, 16)
#define   DISP_OFFSET_Y(y)		REG_FIELD_PREP(DISP_OFFSET_Y_MASK, (y))
#define   DISP_OFFSET_X_MASK		REG_GENMASK(15, 0)
#define   DISP_OFFSET_X(x)		REG_FIELD_PREP(DISP_OFFSET_X_MASK, (x))
#define _DSPAOFFSET				0x701A4 /* HSW */
#define _DSPASURFLIVE				0x701AC
#define _DSPAGAMC				0x701E0

#define DSPADDR_VLV(plane)	_MMIO_PIPE2(plane, _DSPAADDR_VLV)
#define DSPCNTR(plane)		_MMIO_PIPE2(plane, _DSPACNTR)
#define DSPADDR(plane)		_MMIO_PIPE2(plane, _DSPAADDR)
#define DSPSTRIDE(plane)	_MMIO_PIPE2(plane, _DSPASTRIDE)
#define DSPPOS(plane)		_MMIO_PIPE2(plane, _DSPAPOS)
#define DSPSIZE(plane)		_MMIO_PIPE2(plane, _DSPASIZE)
#define DSPSURF(plane)		_MMIO_PIPE2(plane, _DSPASURF)
#define DSPTILEOFF(plane)	_MMIO_PIPE2(plane, _DSPATILEOFF)
#define DSPLINOFF(plane)	DSPADDR(plane)
#define DSPOFFSET(plane)	_MMIO_PIPE2(plane, _DSPAOFFSET)
#define DSPSURFLIVE(plane)	_MMIO_PIPE2(plane, _DSPASURFLIVE)
#define DSPGAMC(plane, i)	_MMIO(_PIPE2(plane, _DSPAGAMC) + (5 - (i)) * 4) /* plane C only, 6 x u0.8 */

/* CHV pipe B blender and primary plane */
#define _CHV_BLEND_A		0x60a00
#define   CHV_BLEND_MASK	REG_GENMASK(31, 30)
#define   CHV_BLEND_LEGACY	REG_FIELD_PREP(CHV_BLEND_MASK, 0)
#define   CHV_BLEND_ANDROID	REG_FIELD_PREP(CHV_BLEND_MASK, 1)
#define   CHV_BLEND_MPO		REG_FIELD_PREP(CHV_BLEND_MASK, 2)
#define _CHV_CANVAS_A		0x60a04
#define   CHV_CANVAS_RED_MASK	REG_GENMASK(29, 20)
#define   CHV_CANVAS_GREEN_MASK	REG_GENMASK(19, 10)
#define   CHV_CANVAS_BLUE_MASK	REG_GENMASK(9, 0)
#define _PRIMPOS_A		0x60a08
#define   PRIM_POS_Y_MASK	REG_GENMASK(31, 16)
#define   PRIM_POS_Y(y)		REG_FIELD_PREP(PRIM_POS_Y_MASK, (y))
#define   PRIM_POS_X_MASK	REG_GENMASK(15, 0)
#define   PRIM_POS_X(x)		REG_FIELD_PREP(PRIM_POS_X_MASK, (x))
#define _PRIMSIZE_A		0x60a0c
#define   PRIM_HEIGHT_MASK	REG_GENMASK(31, 16)
#define   PRIM_HEIGHT(h)	REG_FIELD_PREP(PRIM_HEIGHT_MASK, (h))
#define   PRIM_WIDTH_MASK	REG_GENMASK(15, 0)
#define   PRIM_WIDTH(w)		REG_FIELD_PREP(PRIM_WIDTH_MASK, (w))
#define _PRIMCNSTALPHA_A	0x60a10
#define   PRIM_CONST_ALPHA_ENABLE	REG_BIT(31)
#define   PRIM_CONST_ALPHA_MASK		REG_GENMASK(7, 0)
#define   PRIM_CONST_ALPHA(alpha)	REG_FIELD_PREP(PRIM_CONST_ALPHA_MASK, (alpha))

#define CHV_BLEND(pipe)		_MMIO_TRANS2(pipe, _CHV_BLEND_A)
#define CHV_CANVAS(pipe)	_MMIO_TRANS2(pipe, _CHV_CANVAS_A)
#define PRIMPOS(plane)		_MMIO_TRANS2(plane, _PRIMPOS_A)
#define PRIMSIZE(plane)		_MMIO_TRANS2(plane, _PRIMSIZE_A)
#define PRIMCNSTALPHA(plane)	_MMIO_TRANS2(plane, _PRIMCNSTALPHA_A)

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
#define _PIPEBCONF		(DISPLAY_MMIO_BASE(dev_priv) + 0x71008)
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

/* Sprite A control */
#define _DVSACNTR		0x72180
#define   DVS_ENABLE			REG_BIT(31)
#define   DVS_PIPE_GAMMA_ENABLE		REG_BIT(30)
#define   DVS_YUV_RANGE_CORRECTION_DISABLE	REG_BIT(27)
#define   DVS_FORMAT_MASK		REG_GENMASK(26, 25)
#define   DVS_FORMAT_YUV422		REG_FIELD_PREP(DVS_FORMAT_MASK, 0)
#define   DVS_FORMAT_RGBX101010		REG_FIELD_PREP(DVS_FORMAT_MASK, 1)
#define   DVS_FORMAT_RGBX888		REG_FIELD_PREP(DVS_FORMAT_MASK, 2)
#define   DVS_FORMAT_RGBX161616		REG_FIELD_PREP(DVS_FORMAT_MASK, 3)
#define   DVS_PIPE_CSC_ENABLE		REG_BIT(24)
#define   DVS_SOURCE_KEY		REG_BIT(22)
#define   DVS_RGB_ORDER_XBGR		REG_BIT(20)
#define   DVS_YUV_FORMAT_BT709		REG_BIT(18)
#define   DVS_YUV_ORDER_MASK		REG_GENMASK(17, 16)
#define   DVS_YUV_ORDER_YUYV		REG_FIELD_PREP(DVS_YUV_ORDER_MASK, 0)
#define   DVS_YUV_ORDER_UYVY		REG_FIELD_PREP(DVS_YUV_ORDER_MASK, 1)
#define   DVS_YUV_ORDER_YVYU		REG_FIELD_PREP(DVS_YUV_ORDER_MASK, 2)
#define   DVS_YUV_ORDER_VYUY		REG_FIELD_PREP(DVS_YUV_ORDER_MASK, 3)
#define   DVS_ROTATE_180		REG_BIT(15)
#define   DVS_TRICKLE_FEED_DISABLE	REG_BIT(14)
#define   DVS_TILED			REG_BIT(10)
#define   DVS_DEST_KEY			REG_BIT(2)
#define _DVSALINOFF		0x72184
#define _DVSASTRIDE		0x72188
#define _DVSAPOS		0x7218c
#define   DVS_POS_Y_MASK		REG_GENMASK(31, 16)
#define   DVS_POS_Y(y)			REG_FIELD_PREP(DVS_POS_Y_MASK, (y))
#define   DVS_POS_X_MASK		REG_GENMASK(15, 0)
#define   DVS_POS_X(x)			REG_FIELD_PREP(DVS_POS_X_MASK, (x))
#define _DVSASIZE		0x72190
#define   DVS_HEIGHT_MASK		REG_GENMASK(31, 16)
#define   DVS_HEIGHT(h)			REG_FIELD_PREP(DVS_HEIGHT_MASK, (h))
#define   DVS_WIDTH_MASK		REG_GENMASK(15, 0)
#define   DVS_WIDTH(w)			REG_FIELD_PREP(DVS_WIDTH_MASK, (w))
#define _DVSAKEYVAL		0x72194
#define _DVSAKEYMSK		0x72198
#define _DVSASURF		0x7219c
#define   DVS_ADDR_MASK			REG_GENMASK(31, 12)
#define _DVSAKEYMAXVAL		0x721a0
#define _DVSATILEOFF		0x721a4
#define   DVS_OFFSET_Y_MASK		REG_GENMASK(31, 16)
#define   DVS_OFFSET_Y(y)		REG_FIELD_PREP(DVS_OFFSET_Y_MASK, (y))
#define   DVS_OFFSET_X_MASK		REG_GENMASK(15, 0)
#define   DVS_OFFSET_X(x)		REG_FIELD_PREP(DVS_OFFSET_X_MASK, (x))
#define _DVSASURFLIVE		0x721ac
#define _DVSAGAMC_G4X		0x721e0 /* g4x */
#define _DVSASCALE		0x72204
#define   DVS_SCALE_ENABLE		REG_BIT(31)
#define   DVS_FILTER_MASK		REG_GENMASK(30, 29)
#define   DVS_FILTER_MEDIUM		REG_FIELD_PREP(DVS_FILTER_MASK, 0)
#define   DVS_FILTER_ENHANCING		REG_FIELD_PREP(DVS_FILTER_MASK, 1)
#define   DVS_FILTER_SOFTENING		REG_FIELD_PREP(DVS_FILTER_MASK, 2)
#define   DVS_VERTICAL_OFFSET_HALF	REG_BIT(28) /* must be enabled below */
#define   DVS_VERTICAL_OFFSET_ENABLE	REG_BIT(27)
#define   DVS_SRC_WIDTH_MASK		REG_GENMASK(26, 16)
#define   DVS_SRC_WIDTH(w)		REG_FIELD_PREP(DVS_SRC_WIDTH_MASK, (w))
#define   DVS_SRC_HEIGHT_MASK		REG_GENMASK(10, 0)
#define   DVS_SRC_HEIGHT(h)		REG_FIELD_PREP(DVS_SRC_HEIGHT_MASK, (h))
#define _DVSAGAMC_ILK		0x72300 /* ilk/snb */
#define _DVSAGAMCMAX_ILK	0x72340 /* ilk/snb */

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
#define _DVSBGAMC_G4X		0x731e0 /* g4x */
#define _DVSBSCALE		0x73204
#define _DVSBGAMC_ILK		0x73300 /* ilk/snb */
#define _DVSBGAMCMAX_ILK	0x73340 /* ilk/snb */

#define DVSCNTR(pipe) _MMIO_PIPE(pipe, _DVSACNTR, _DVSBCNTR)
#define DVSLINOFF(pipe) _MMIO_PIPE(pipe, _DVSALINOFF, _DVSBLINOFF)
#define DVSSTRIDE(pipe) _MMIO_PIPE(pipe, _DVSASTRIDE, _DVSBSTRIDE)
#define DVSPOS(pipe) _MMIO_PIPE(pipe, _DVSAPOS, _DVSBPOS)
#define DVSSURF(pipe) _MMIO_PIPE(pipe, _DVSASURF, _DVSBSURF)
#define DVSKEYMAX(pipe) _MMIO_PIPE(pipe, _DVSAKEYMAXVAL, _DVSBKEYMAXVAL)
#define DVSSIZE(pipe) _MMIO_PIPE(pipe, _DVSASIZE, _DVSBSIZE)
#define DVSSCALE(pipe) _MMIO_PIPE(pipe, _DVSASCALE, _DVSBSCALE)
#define DVSTILEOFF(pipe) _MMIO_PIPE(pipe, _DVSATILEOFF, _DVSBTILEOFF)
#define DVSKEYVAL(pipe) _MMIO_PIPE(pipe, _DVSAKEYVAL, _DVSBKEYVAL)
#define DVSKEYMSK(pipe) _MMIO_PIPE(pipe, _DVSAKEYMSK, _DVSBKEYMSK)
#define DVSSURFLIVE(pipe) _MMIO_PIPE(pipe, _DVSASURFLIVE, _DVSBSURFLIVE)
#define DVSGAMC_G4X(pipe, i) _MMIO(_PIPE(pipe, _DVSAGAMC_G4X, _DVSBGAMC_G4X) + (5 - (i)) * 4) /* 6 x u0.8 */
#define DVSGAMC_ILK(pipe, i) _MMIO(_PIPE(pipe, _DVSAGAMC_ILK, _DVSBGAMC_ILK) + (i) * 4) /* 16 x u0.10 */
#define DVSGAMCMAX_ILK(pipe, i) _MMIO(_PIPE(pipe, _DVSAGAMCMAX_ILK, _DVSBGAMCMAX_ILK) + (i) * 4) /* 3 x u1.10 */

#define _SPRA_CTL		0x70280
#define   SPRITE_ENABLE				REG_BIT(31)
#define   SPRITE_PIPE_GAMMA_ENABLE		REG_BIT(30)
#define   SPRITE_YUV_RANGE_CORRECTION_DISABLE	REG_BIT(28)
#define   SPRITE_FORMAT_MASK			REG_GENMASK(27, 25)
#define   SPRITE_FORMAT_YUV422			REG_FIELD_PREP(SPRITE_FORMAT_MASK, 0)
#define   SPRITE_FORMAT_RGBX101010		REG_FIELD_PREP(SPRITE_FORMAT_MASK, 1)
#define   SPRITE_FORMAT_RGBX888			REG_FIELD_PREP(SPRITE_FORMAT_MASK, 2)
#define   SPRITE_FORMAT_RGBX161616		REG_FIELD_PREP(SPRITE_FORMAT_MASK, 3)
#define   SPRITE_FORMAT_YUV444			REG_FIELD_PREP(SPRITE_FORMAT_MASK, 4)
#define   SPRITE_FORMAT_XR_BGR101010		REG_FIELD_PREP(SPRITE_FORMAT_MASK, 5) /* Extended range */
#define   SPRITE_PIPE_CSC_ENABLE		REG_BIT(24)
#define   SPRITE_SOURCE_KEY			REG_BIT(22)
#define   SPRITE_RGB_ORDER_RGBX			REG_BIT(20) /* only for 888 and 161616 */
#define   SPRITE_YUV_TO_RGB_CSC_DISABLE		REG_BIT(19)
#define   SPRITE_YUV_TO_RGB_CSC_FORMAT_BT709	REG_BIT(18) /* 0 is BT601 */
#define   SPRITE_YUV_ORDER_MASK			REG_GENMASK(17, 16)
#define   SPRITE_YUV_ORDER_YUYV			REG_FIELD_PREP(SPRITE_YUV_ORDER_MASK, 0)
#define   SPRITE_YUV_ORDER_UYVY			REG_FIELD_PREP(SPRITE_YUV_ORDER_MASK, 1)
#define   SPRITE_YUV_ORDER_YVYU			REG_FIELD_PREP(SPRITE_YUV_ORDER_MASK, 2)
#define   SPRITE_YUV_ORDER_VYUY			REG_FIELD_PREP(SPRITE_YUV_ORDER_MASK, 3)
#define   SPRITE_ROTATE_180			REG_BIT(15)
#define   SPRITE_TRICKLE_FEED_DISABLE		REG_BIT(14)
#define   SPRITE_PLANE_GAMMA_DISABLE		REG_BIT(13)
#define   SPRITE_TILED				REG_BIT(10)
#define   SPRITE_DEST_KEY			REG_BIT(2)
#define _SPRA_LINOFF		0x70284
#define _SPRA_STRIDE		0x70288
#define _SPRA_POS		0x7028c
#define   SPRITE_POS_Y_MASK	REG_GENMASK(31, 16)
#define   SPRITE_POS_Y(y)	REG_FIELD_PREP(SPRITE_POS_Y_MASK, (y))
#define   SPRITE_POS_X_MASK	REG_GENMASK(15, 0)
#define   SPRITE_POS_X(x)	REG_FIELD_PREP(SPRITE_POS_X_MASK, (x))
#define _SPRA_SIZE		0x70290
#define   SPRITE_HEIGHT_MASK	REG_GENMASK(31, 16)
#define   SPRITE_HEIGHT(h)	REG_FIELD_PREP(SPRITE_HEIGHT_MASK, (h))
#define   SPRITE_WIDTH_MASK	REG_GENMASK(15, 0)
#define   SPRITE_WIDTH(w)	REG_FIELD_PREP(SPRITE_WIDTH_MASK, (w))
#define _SPRA_KEYVAL		0x70294
#define _SPRA_KEYMSK		0x70298
#define _SPRA_SURF		0x7029c
#define   SPRITE_ADDR_MASK	REG_GENMASK(31, 12)
#define _SPRA_KEYMAX		0x702a0
#define _SPRA_TILEOFF		0x702a4
#define   SPRITE_OFFSET_Y_MASK	REG_GENMASK(31, 16)
#define   SPRITE_OFFSET_Y(y)	REG_FIELD_PREP(SPRITE_OFFSET_Y_MASK, (y))
#define   SPRITE_OFFSET_X_MASK	REG_GENMASK(15, 0)
#define   SPRITE_OFFSET_X(x)	REG_FIELD_PREP(SPRITE_OFFSET_X_MASK, (x))
#define _SPRA_OFFSET		0x702a4
#define _SPRA_SURFLIVE		0x702ac
#define _SPRA_SCALE		0x70304
#define   SPRITE_SCALE_ENABLE			REG_BIT(31)
#define   SPRITE_FILTER_MASK			REG_GENMASK(30, 29)
#define   SPRITE_FILTER_MEDIUM			REG_FIELD_PREP(SPRITE_FILTER_MASK, 0)
#define   SPRITE_FILTER_ENHANCING		REG_FIELD_PREP(SPRITE_FILTER_MASK, 1)
#define   SPRITE_FILTER_SOFTENING		REG_FIELD_PREP(SPRITE_FILTER_MASK, 2)
#define   SPRITE_VERTICAL_OFFSET_HALF		REG_BIT(28) /* must be enabled below */
#define   SPRITE_VERTICAL_OFFSET_ENABLE		REG_BIT(27)
#define   SPRITE_SRC_WIDTH_MASK			REG_GENMASK(26, 16)
#define   SPRITE_SRC_WIDTH(w)			REG_FIELD_PREP(SPRITE_SRC_WIDTH_MASK, (w))
#define   SPRITE_SRC_HEIGHT_MASK		REG_GENMASK(10, 0)
#define   SPRITE_SRC_HEIGHT(h)			REG_FIELD_PREP(SPRITE_SRC_HEIGHT_MASK, (h))
#define _SPRA_GAMC		0x70400
#define _SPRA_GAMC16		0x70440
#define _SPRA_GAMC17		0x7044c

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
#define _SPRB_GAMC16		0x71440
#define _SPRB_GAMC17		0x7144c

#define SPRCTL(pipe) _MMIO_PIPE(pipe, _SPRA_CTL, _SPRB_CTL)
#define SPRLINOFF(pipe) _MMIO_PIPE(pipe, _SPRA_LINOFF, _SPRB_LINOFF)
#define SPRSTRIDE(pipe) _MMIO_PIPE(pipe, _SPRA_STRIDE, _SPRB_STRIDE)
#define SPRPOS(pipe) _MMIO_PIPE(pipe, _SPRA_POS, _SPRB_POS)
#define SPRSIZE(pipe) _MMIO_PIPE(pipe, _SPRA_SIZE, _SPRB_SIZE)
#define SPRKEYVAL(pipe) _MMIO_PIPE(pipe, _SPRA_KEYVAL, _SPRB_KEYVAL)
#define SPRKEYMSK(pipe) _MMIO_PIPE(pipe, _SPRA_KEYMSK, _SPRB_KEYMSK)
#define SPRSURF(pipe) _MMIO_PIPE(pipe, _SPRA_SURF, _SPRB_SURF)
#define SPRKEYMAX(pipe) _MMIO_PIPE(pipe, _SPRA_KEYMAX, _SPRB_KEYMAX)
#define SPRTILEOFF(pipe) _MMIO_PIPE(pipe, _SPRA_TILEOFF, _SPRB_TILEOFF)
#define SPROFFSET(pipe) _MMIO_PIPE(pipe, _SPRA_OFFSET, _SPRB_OFFSET)
#define SPRSCALE(pipe) _MMIO_PIPE(pipe, _SPRA_SCALE, _SPRB_SCALE)
#define SPRGAMC(pipe, i) _MMIO(_PIPE(pipe, _SPRA_GAMC, _SPRB_GAMC) + (i) * 4) /* 16 x u0.10 */
#define SPRGAMC16(pipe, i) _MMIO(_PIPE(pipe, _SPRA_GAMC16, _SPRB_GAMC16) + (i) * 4) /* 3 x u1.10 */
#define SPRGAMC17(pipe, i) _MMIO(_PIPE(pipe, _SPRA_GAMC17, _SPRB_GAMC17) + (i) * 4) /* 3 x u2.10 */
#define SPRSURFLIVE(pipe) _MMIO_PIPE(pipe, _SPRA_SURFLIVE, _SPRB_SURFLIVE)

#define _SPACNTR		(VLV_DISPLAY_BASE + 0x72180)
#define   SP_ENABLE			REG_BIT(31)
#define   SP_PIPE_GAMMA_ENABLE		REG_BIT(30)
#define   SP_FORMAT_MASK		REG_GENMASK(29, 26)
#define   SP_FORMAT_YUV422		REG_FIELD_PREP(SP_FORMAT_MASK, 0)
#define   SP_FORMAT_8BPP		REG_FIELD_PREP(SP_FORMAT_MASK, 2)
#define   SP_FORMAT_BGR565		REG_FIELD_PREP(SP_FORMAT_MASK, 5)
#define   SP_FORMAT_BGRX8888		REG_FIELD_PREP(SP_FORMAT_MASK, 6)
#define   SP_FORMAT_BGRA8888		REG_FIELD_PREP(SP_FORMAT_MASK, 7)
#define   SP_FORMAT_RGBX1010102		REG_FIELD_PREP(SP_FORMAT_MASK, 8)
#define   SP_FORMAT_RGBA1010102		REG_FIELD_PREP(SP_FORMAT_MASK, 9)
#define   SP_FORMAT_BGRX1010102		REG_FIELD_PREP(SP_FORMAT_MASK, 10) /* CHV pipe B */
#define   SP_FORMAT_BGRA1010102		REG_FIELD_PREP(SP_FORMAT_MASK, 11) /* CHV pipe B */
#define   SP_FORMAT_RGBX8888		REG_FIELD_PREP(SP_FORMAT_MASK, 14)
#define   SP_FORMAT_RGBA8888		REG_FIELD_PREP(SP_FORMAT_MASK, 15)
#define   SP_ALPHA_PREMULTIPLY		REG_BIT(23) /* CHV pipe B */
#define   SP_SOURCE_KEY			REG_BIT(22)
#define   SP_YUV_FORMAT_BT709		REG_BIT(18)
#define   SP_YUV_ORDER_MASK		REG_GENMASK(17, 16)
#define   SP_YUV_ORDER_YUYV		REG_FIELD_PREP(SP_YUV_ORDER_MASK, 0)
#define   SP_YUV_ORDER_UYVY		REG_FIELD_PREP(SP_YUV_ORDER_MASK, 1)
#define   SP_YUV_ORDER_YVYU		REG_FIELD_PREP(SP_YUV_ORDER_MASK, 2)
#define   SP_YUV_ORDER_VYUY		REG_FIELD_PREP(SP_YUV_ORDER_MASK, 3)
#define   SP_ROTATE_180			REG_BIT(15)
#define   SP_TILED			REG_BIT(10)
#define   SP_MIRROR			REG_BIT(8) /* CHV pipe B */
#define _SPALINOFF		(VLV_DISPLAY_BASE + 0x72184)
#define _SPASTRIDE		(VLV_DISPLAY_BASE + 0x72188)
#define _SPAPOS			(VLV_DISPLAY_BASE + 0x7218c)
#define   SP_POS_Y_MASK			REG_GENMASK(31, 16)
#define   SP_POS_Y(y)			REG_FIELD_PREP(SP_POS_Y_MASK, (y))
#define   SP_POS_X_MASK			REG_GENMASK(15, 0)
#define   SP_POS_X(x)			REG_FIELD_PREP(SP_POS_X_MASK, (x))
#define _SPASIZE		(VLV_DISPLAY_BASE + 0x72190)
#define   SP_HEIGHT_MASK		REG_GENMASK(31, 16)
#define   SP_HEIGHT(h)			REG_FIELD_PREP(SP_HEIGHT_MASK, (h))
#define   SP_WIDTH_MASK			REG_GENMASK(15, 0)
#define   SP_WIDTH(w)			REG_FIELD_PREP(SP_WIDTH_MASK, (w))
#define _SPAKEYMINVAL		(VLV_DISPLAY_BASE + 0x72194)
#define _SPAKEYMSK		(VLV_DISPLAY_BASE + 0x72198)
#define _SPASURF		(VLV_DISPLAY_BASE + 0x7219c)
#define   SP_ADDR_MASK			REG_GENMASK(31, 12)
#define _SPAKEYMAXVAL		(VLV_DISPLAY_BASE + 0x721a0)
#define _SPATILEOFF		(VLV_DISPLAY_BASE + 0x721a4)
#define   SP_OFFSET_Y_MASK		REG_GENMASK(31, 16)
#define   SP_OFFSET_Y(y)		REG_FIELD_PREP(SP_OFFSET_Y_MASK, (y))
#define   SP_OFFSET_X_MASK		REG_GENMASK(15, 0)
#define   SP_OFFSET_X(x)		REG_FIELD_PREP(SP_OFFSET_X_MASK, (x))
#define _SPACONSTALPHA		(VLV_DISPLAY_BASE + 0x721a8)
#define   SP_CONST_ALPHA_ENABLE		REG_BIT(31)
#define   SP_CONST_ALPHA_MASK		REG_GENMASK(7, 0)
#define   SP_CONST_ALPHA(alpha)		REG_FIELD_PREP(SP_CONST_ALPHA_MASK, (alpha))
#define _SPACLRC0		(VLV_DISPLAY_BASE + 0x721d0)
#define   SP_CONTRAST_MASK		REG_GENMASK(26, 18)
#define   SP_CONTRAST(x)		REG_FIELD_PREP(SP_CONTRAST_MASK, (x)) /* u3.6 */
#define   SP_BRIGHTNESS_MASK		REG_GENMASK(7, 0)
#define   SP_BRIGHTNESS(x)		REG_FIELD_PREP(SP_BRIGHTNESS_MASK, (x)) /* s8 */
#define _SPACLRC1		(VLV_DISPLAY_BASE + 0x721d4)
#define   SP_SH_SIN_MASK		REG_GENMASK(26, 16)
#define   SP_SH_SIN(x)			REG_FIELD_PREP(SP_SH_SIN_MASK, (x)) /* s4.7 */
#define   SP_SH_COS_MASK		REG_GENMASK(9, 0)
#define   SP_SH_COS(x)			REG_FIELD_PREP(SP_SH_COS_MASK, (x)) /* u3.7 */
#define _SPAGAMC		(VLV_DISPLAY_BASE + 0x721e0)

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
#define _SPBCLRC0		(VLV_DISPLAY_BASE + 0x722d0)
#define _SPBCLRC1		(VLV_DISPLAY_BASE + 0x722d4)
#define _SPBGAMC		(VLV_DISPLAY_BASE + 0x722e0)

#define _VLV_SPR(pipe, plane_id, reg_a, reg_b) \
	_PIPE((pipe) * 2 + (plane_id) - PLANE_SPRITE0, (reg_a), (reg_b))
#define _MMIO_VLV_SPR(pipe, plane_id, reg_a, reg_b) \
	_MMIO(_VLV_SPR((pipe), (plane_id), (reg_a), (reg_b)))

#define SPCNTR(pipe, plane_id)		_MMIO_VLV_SPR((pipe), (plane_id), _SPACNTR, _SPBCNTR)
#define SPLINOFF(pipe, plane_id)	_MMIO_VLV_SPR((pipe), (plane_id), _SPALINOFF, _SPBLINOFF)
#define SPSTRIDE(pipe, plane_id)	_MMIO_VLV_SPR((pipe), (plane_id), _SPASTRIDE, _SPBSTRIDE)
#define SPPOS(pipe, plane_id)		_MMIO_VLV_SPR((pipe), (plane_id), _SPAPOS, _SPBPOS)
#define SPSIZE(pipe, plane_id)		_MMIO_VLV_SPR((pipe), (plane_id), _SPASIZE, _SPBSIZE)
#define SPKEYMINVAL(pipe, plane_id)	_MMIO_VLV_SPR((pipe), (plane_id), _SPAKEYMINVAL, _SPBKEYMINVAL)
#define SPKEYMSK(pipe, plane_id)	_MMIO_VLV_SPR((pipe), (plane_id), _SPAKEYMSK, _SPBKEYMSK)
#define SPSURF(pipe, plane_id)		_MMIO_VLV_SPR((pipe), (plane_id), _SPASURF, _SPBSURF)
#define SPKEYMAXVAL(pipe, plane_id)	_MMIO_VLV_SPR((pipe), (plane_id), _SPAKEYMAXVAL, _SPBKEYMAXVAL)
#define SPTILEOFF(pipe, plane_id)	_MMIO_VLV_SPR((pipe), (plane_id), _SPATILEOFF, _SPBTILEOFF)
#define SPCONSTALPHA(pipe, plane_id)	_MMIO_VLV_SPR((pipe), (plane_id), _SPACONSTALPHA, _SPBCONSTALPHA)
#define SPCLRC0(pipe, plane_id)		_MMIO_VLV_SPR((pipe), (plane_id), _SPACLRC0, _SPBCLRC0)
#define SPCLRC1(pipe, plane_id)		_MMIO_VLV_SPR((pipe), (plane_id), _SPACLRC1, _SPBCLRC1)
#define SPGAMC(pipe, plane_id, i)	_MMIO(_VLV_SPR((pipe), (plane_id), _SPAGAMC, _SPBGAMC) + (5 - (i)) * 4) /* 6 x u0.10 */

/*
 * CHV pipe B sprite CSC
 *
 * |cr|   |c0 c1 c2|   |cr + cr_ioff|   |cr_ooff|
 * |yg| = |c3 c4 c5| x |yg + yg_ioff| + |yg_ooff|
 * |cb|   |c6 c7 c8|   |cb + cr_ioff|   |cb_ooff|
 */
#define _MMIO_CHV_SPCSC(plane_id, reg) \
	_MMIO(VLV_DISPLAY_BASE + ((plane_id) - PLANE_SPRITE0) * 0x1000 + (reg))

#define SPCSCYGOFF(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d900)
#define SPCSCCBOFF(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d904)
#define SPCSCCROFF(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d908)
#define  SPCSC_OOFF_MASK	REG_GENMASK(26, 16)
#define  SPCSC_OOFF(x)		REG_FIELD_PREP(SPCSC_OOFF_MASK, (x) & 0x7ff) /* s11 */
#define  SPCSC_IOFF_MASK	REG_GENMASK(10, 0)
#define  SPCSC_IOFF(x)		REG_FIELD_PREP(SPCSC_IOFF_MASK, (x) & 0x7ff) /* s11 */

#define SPCSCC01(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d90c)
#define SPCSCC23(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d910)
#define SPCSCC45(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d914)
#define SPCSCC67(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d918)
#define SPCSCC8(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d91c)
#define  SPCSC_C1_MASK		REG_GENMASK(30, 16)
#define  SPCSC_C1(x)		REG_FIELD_PREP(SPCSC_C1_MASK, (x) & 0x7fff) /* s3.12 */
#define  SPCSC_C0_MASK		REG_GENMASK(14, 0)
#define  SPCSC_C0(x)		REG_FIELD_PREP(SPCSC_C0_MASK, (x) & 0x7fff) /* s3.12 */

#define SPCSCYGICLAMP(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d920)
#define SPCSCCBICLAMP(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d924)
#define SPCSCCRICLAMP(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d928)
#define  SPCSC_IMAX_MASK	REG_GENMASK(26, 16)
#define  SPCSC_IMAX(x)		REG_FIELD_PREP(SPCSC_IMAX_MASK, (x) & 0x7ff) /* s11 */
#define  SPCSC_IMIN_MASK	REG_GENMASK(10, 0)
#define  SPCSC_IMIN(x)		REG_FIELD_PREP(SPCSC_IMIN_MASK, (x) & 0x7ff) /* s11 */

#define SPCSCYGOCLAMP(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d92c)
#define SPCSCCBOCLAMP(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d930)
#define SPCSCCROCLAMP(plane_id)	_MMIO_CHV_SPCSC(plane_id, 0x6d934)
#define  SPCSC_OMAX_MASK	REG_GENMASK(25, 16)
#define  SPCSC_OMAX(x)		REG_FIELD_PREP(SPCSC_OMAX_MASK, (x)) /* u10 */
#define  SPCSC_OMIN_MASK	REG_GENMASK(9, 0)
#define  SPCSC_OMIN(x)		REG_FIELD_PREP(SPCSC_OMIN_MASK, (x)) /* u10 */

/* Skylake plane registers */

#define _PLANE_CTL_1_A				0x70180
#define _PLANE_CTL_2_A				0x70280
#define _PLANE_CTL_3_A				0x70380
#define   PLANE_CTL_ENABLE			REG_BIT(31)
#define   PLANE_CTL_ARB_SLOTS_MASK		REG_GENMASK(30, 28) /* icl+ */
#define   PLANE_CTL_ARB_SLOTS(x)		REG_FIELD_PREP(PLANE_CTL_ARB_SLOTS_MASK, (x)) /* icl+ */
#define   PLANE_CTL_PIPE_GAMMA_ENABLE		REG_BIT(30) /* Pre-GLK */
#define   PLANE_CTL_YUV_RANGE_CORRECTION_DISABLE	REG_BIT(28)
/*
 * ICL+ uses the same PLANE_CTL_FORMAT bits, but the field definition
 * expanded to include bit 23 as well. However, the shift-24 based values
 * correctly map to the same formats in ICL, as long as bit 23 is set to 0
 */
#define   PLANE_CTL_FORMAT_MASK_SKL		REG_GENMASK(27, 24) /* pre-icl */
#define   PLANE_CTL_FORMAT_MASK_ICL		REG_GENMASK(27, 23) /* icl+ */
#define   PLANE_CTL_FORMAT_YUV422		REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 0)
#define   PLANE_CTL_FORMAT_NV12			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 1)
#define   PLANE_CTL_FORMAT_XRGB_2101010		REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 2)
#define   PLANE_CTL_FORMAT_P010			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 3)
#define   PLANE_CTL_FORMAT_XRGB_8888		REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 4)
#define   PLANE_CTL_FORMAT_P012			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 5)
#define   PLANE_CTL_FORMAT_XRGB_16161616F	REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 6)
#define   PLANE_CTL_FORMAT_P016			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 7)
#define   PLANE_CTL_FORMAT_XYUV			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 8)
#define   PLANE_CTL_FORMAT_INDEXED		REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 12)
#define   PLANE_CTL_FORMAT_RGB_565		REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_SKL, 14)
#define   PLANE_CTL_FORMAT_Y210			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_ICL, 1)
#define   PLANE_CTL_FORMAT_Y212			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_ICL, 3)
#define   PLANE_CTL_FORMAT_Y216			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_ICL, 5)
#define   PLANE_CTL_FORMAT_Y410			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_ICL, 7)
#define   PLANE_CTL_FORMAT_Y412			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_ICL, 9)
#define   PLANE_CTL_FORMAT_Y416			REG_FIELD_PREP(PLANE_CTL_FORMAT_MASK_ICL, 11)
#define   PLANE_CTL_PIPE_CSC_ENABLE		REG_BIT(23) /* Pre-GLK */
#define   PLANE_CTL_KEY_ENABLE_MASK		REG_GENMASK(22, 21)
#define   PLANE_CTL_KEY_ENABLE_SOURCE		REG_FIELD_PREP(PLANE_CTL_KEY_ENABLE_MASK, 1)
#define   PLANE_CTL_KEY_ENABLE_DESTINATION	REG_FIELD_PREP(PLANE_CTL_KEY_ENABLE_MASK, 2)
#define   PLANE_CTL_ORDER_RGBX			REG_BIT(20)
#define   PLANE_CTL_YUV420_Y_PLANE		REG_BIT(19)
#define   PLANE_CTL_YUV_TO_RGB_CSC_FORMAT_BT709	REG_BIT(18)
#define   PLANE_CTL_YUV422_ORDER_MASK		REG_GENMASK(17, 16)
#define   PLANE_CTL_YUV422_ORDER_YUYV		REG_FIELD_PREP(PLANE_CTL_YUV422_ORDER_MASK, 0)
#define   PLANE_CTL_YUV422_ORDER_UYVY		REG_FIELD_PREP(PLANE_CTL_YUV422_ORDER_MASK, 1)
#define   PLANE_CTL_YUV422_ORDER_YVYU		REG_FIELD_PREP(PLANE_CTL_YUV422_ORDER_MASK, 2)
#define   PLANE_CTL_YUV422_ORDER_VYUY		REG_FIELD_PREP(PLANE_CTL_YUV422_ORDER_MASK, 3)
#define   PLANE_CTL_RENDER_DECOMPRESSION_ENABLE	REG_BIT(15)
#define   PLANE_CTL_TRICKLE_FEED_DISABLE	REG_BIT(14)
#define   PLANE_CTL_CLEAR_COLOR_DISABLE		REG_BIT(13) /* TGL+ */
#define   PLANE_CTL_PLANE_GAMMA_DISABLE		REG_BIT(13) /* Pre-GLK */
#define   PLANE_CTL_TILED_MASK			REG_GENMASK(12, 10)
#define   PLANE_CTL_TILED_LINEAR		REG_FIELD_PREP(PLANE_CTL_TILED_MASK, 0)
#define   PLANE_CTL_TILED_X			REG_FIELD_PREP(PLANE_CTL_TILED_MASK, 1)
#define   PLANE_CTL_TILED_Y			REG_FIELD_PREP(PLANE_CTL_TILED_MASK, 4)
#define   PLANE_CTL_TILED_YF			REG_FIELD_PREP(PLANE_CTL_TILED_MASK, 5)
#define   PLANE_CTL_ASYNC_FLIP			REG_BIT(9)
#define   PLANE_CTL_FLIP_HORIZONTAL		REG_BIT(8)
#define   PLANE_CTL_MEDIA_DECOMPRESSION_ENABLE	REG_BIT(4) /* TGL+ */
#define   PLANE_CTL_ALPHA_MASK			REG_GENMASK(5, 4) /* Pre-GLK */
#define   PLANE_CTL_ALPHA_DISABLE		REG_FIELD_PREP(PLANE_CTL_ALPHA_MASK, 0)
#define   PLANE_CTL_ALPHA_SW_PREMULTIPLY	REG_FIELD_PREP(PLANE_CTL_ALPHA_MASK, 2)
#define   PLANE_CTL_ALPHA_HW_PREMULTIPLY	REG_FIELD_PREP(PLANE_CTL_ALPHA_MASK, 3)
#define   PLANE_CTL_ROTATE_MASK			REG_GENMASK(1, 0)
#define   PLANE_CTL_ROTATE_0			REG_FIELD_PREP(PLANE_CTL_ROTATE_MASK, 0)
#define   PLANE_CTL_ROTATE_90			REG_FIELD_PREP(PLANE_CTL_ROTATE_MASK, 1)
#define   PLANE_CTL_ROTATE_180			REG_FIELD_PREP(PLANE_CTL_ROTATE_MASK, 2)
#define   PLANE_CTL_ROTATE_270			REG_FIELD_PREP(PLANE_CTL_ROTATE_MASK, 3)
#define _PLANE_STRIDE_1_A			0x70188
#define _PLANE_STRIDE_2_A			0x70288
#define _PLANE_STRIDE_3_A			0x70388
#define   PLANE_STRIDE__MASK			REG_GENMASK(11, 0)
#define   PLANE_STRIDE_(stride)			REG_FIELD_PREP(PLANE_STRIDE__MASK, (stride))
#define _PLANE_POS_1_A				0x7018c
#define _PLANE_POS_2_A				0x7028c
#define _PLANE_POS_3_A				0x7038c
#define   PLANE_POS_Y_MASK			REG_GENMASK(31, 16)
#define   PLANE_POS_Y(y)			REG_FIELD_PREP(PLANE_POS_Y_MASK, (y))
#define   PLANE_POS_X_MASK			REG_GENMASK(15, 0)
#define   PLANE_POS_X(x)			REG_FIELD_PREP(PLANE_POS_X_MASK, (x))
#define _PLANE_SIZE_1_A				0x70190
#define _PLANE_SIZE_2_A				0x70290
#define _PLANE_SIZE_3_A				0x70390
#define   PLANE_HEIGHT_MASK			REG_GENMASK(31, 16)
#define   PLANE_HEIGHT(h)			REG_FIELD_PREP(PLANE_HEIGHT_MASK, (h))
#define   PLANE_WIDTH_MASK			REG_GENMASK(15, 0)
#define   PLANE_WIDTH(w)			REG_FIELD_PREP(PLANE_WIDTH_MASK, (w))
#define _PLANE_SURF_1_A				0x7019c
#define _PLANE_SURF_2_A				0x7029c
#define _PLANE_SURF_3_A				0x7039c
#define   PLANE_SURF_ADDR_MASK			REG_GENMASK(31, 12)
#define   PLANE_SURF_DECRYPT			REG_BIT(2)
#define _PLANE_OFFSET_1_A			0x701a4
#define _PLANE_OFFSET_2_A			0x702a4
#define _PLANE_OFFSET_3_A			0x703a4
#define   PLANE_OFFSET_Y_MASK			REG_GENMASK(31, 16)
#define   PLANE_OFFSET_Y(y)			REG_FIELD_PREP(PLANE_OFFSET_Y_MASK, (y))
#define   PLANE_OFFSET_X_MASK			REG_GENMASK(15, 0)
#define   PLANE_OFFSET_X(x)			REG_FIELD_PREP(PLANE_OFFSET_X_MASK, (x))
#define _PLANE_KEYVAL_1_A			0x70194
#define _PLANE_KEYVAL_2_A			0x70294
#define _PLANE_KEYMSK_1_A			0x70198
#define _PLANE_KEYMSK_2_A			0x70298
#define  PLANE_KEYMSK_ALPHA_ENABLE		(1 << 31)
#define _PLANE_KEYMAX_1_A			0x701a0
#define _PLANE_KEYMAX_2_A			0x702a0
#define  PLANE_KEYMAX_ALPHA(a)			((a) << 24)
#define _PLANE_CC_VAL_1_A			0x701b4
#define _PLANE_CC_VAL_2_A			0x702b4
#define _PLANE_AUX_DIST_1_A			0x701c0
#define   PLANE_AUX_DISTANCE_MASK		REG_GENMASK(31, 12)
#define   PLANE_AUX_STRIDE_MASK			REG_GENMASK(11, 0)
#define   PLANE_AUX_STRIDE(stride)		REG_FIELD_PREP(PLANE_AUX_STRIDE_MASK, (stride))
#define _PLANE_AUX_DIST_2_A			0x702c0
#define _PLANE_AUX_OFFSET_1_A			0x701c4
#define _PLANE_AUX_OFFSET_2_A			0x702c4
#define _PLANE_CUS_CTL_1_A			0x701c8
#define _PLANE_CUS_CTL_2_A			0x702c8
#define   PLANE_CUS_ENABLE			REG_BIT(31)
#define   PLANE_CUS_Y_PLANE_MASK			REG_BIT(30)
#define   PLANE_CUS_Y_PLANE_4_RKL		REG_FIELD_PREP(PLANE_CUS_Y_PLANE_MASK, 0)
#define   PLANE_CUS_Y_PLANE_5_RKL		REG_FIELD_PREP(PLANE_CUS_Y_PLANE_MASK, 1)
#define   PLANE_CUS_Y_PLANE_6_ICL		REG_FIELD_PREP(PLANE_CUS_Y_PLANE_MASK, 0)
#define   PLANE_CUS_Y_PLANE_7_ICL		REG_FIELD_PREP(PLANE_CUS_Y_PLANE_MASK, 1)
#define   PLANE_CUS_HPHASE_SIGN_NEGATIVE		REG_BIT(19)
#define   PLANE_CUS_HPHASE_MASK			REG_GENMASK(17, 16)
#define   PLANE_CUS_HPHASE_0			REG_FIELD_PREP(PLANE_CUS_HPHASE_MASK, 0)
#define   PLANE_CUS_HPHASE_0_25			REG_FIELD_PREP(PLANE_CUS_HPHASE_MASK, 1)
#define   PLANE_CUS_HPHASE_0_5			REG_FIELD_PREP(PLANE_CUS_HPHASE_MASK, 2)
#define   PLANE_CUS_VPHASE_SIGN_NEGATIVE		REG_BIT(15)
#define   PLANE_CUS_VPHASE_MASK			REG_GENMASK(13, 12)
#define   PLANE_CUS_VPHASE_0			REG_FIELD_PREP(PLANE_CUS_VPHASE_MASK, 0)
#define   PLANE_CUS_VPHASE_0_25			REG_FIELD_PREP(PLANE_CUS_VPHASE_MASK, 1)
#define   PLANE_CUS_VPHASE_0_5			REG_FIELD_PREP(PLANE_CUS_VPHASE_MASK, 2)
#define _PLANE_COLOR_CTL_1_A			0x701CC /* GLK+ */
#define _PLANE_COLOR_CTL_2_A			0x702CC /* GLK+ */
#define _PLANE_COLOR_CTL_3_A			0x703CC /* GLK+ */
#define   PLANE_COLOR_PIPE_GAMMA_ENABLE			REG_BIT(30) /* Pre-ICL */
#define   PLANE_COLOR_YUV_RANGE_CORRECTION_DISABLE	REG_BIT(28)
#define   PLANE_COLOR_PIPE_CSC_ENABLE			REG_BIT(23) /* Pre-ICL */
#define   PLANE_COLOR_PLANE_CSC_ENABLE			REG_BIT(21) /* ICL+ */
#define   PLANE_COLOR_INPUT_CSC_ENABLE			REG_BIT(20) /* ICL+ */
#define   PLANE_COLOR_CSC_MODE_MASK			REG_GENMASK(19, 17)
#define   PLANE_COLOR_CSC_MODE_BYPASS			REG_FIELD_PREP(PLANE_COLOR_CSC_MODE_MASK, 0)
#define   PLANE_COLOR_CSC_MODE_YUV601_TO_RGB601		REG_FIELD_PREP(PLANE_COLOR_CSC_MODE_MASK, 1)
#define   PLANE_COLOR_CSC_MODE_YUV709_TO_RGB709		REG_FIELD_PREP(PLANE_COLOR_CSC_MODE_MASK, 2)
#define   PLANE_COLOR_CSC_MODE_YUV2020_TO_RGB2020	REG_FIELD_PREP(PLANE_COLOR_CSC_MODE_MASK, 3)
#define   PLANE_COLOR_CSC_MODE_RGB709_TO_RGB2020	REG_FIELD_PREP(PLANE_COLOR_CSC_MODE_MASK, 4)
#define   PLANE_COLOR_PLANE_GAMMA_DISABLE		REG_BIT(13)
#define   PLANE_COLOR_ALPHA_MASK			REG_GENMASK(5, 4)
#define   PLANE_COLOR_ALPHA_DISABLE			REG_FIELD_PREP(PLANE_COLOR_ALPHA_MASK, 0)
#define   PLANE_COLOR_ALPHA_SW_PREMULTIPLY		REG_FIELD_PREP(PLANE_COLOR_ALPHA_MASK, 2)
#define   PLANE_COLOR_ALPHA_HW_PREMULTIPLY		REG_FIELD_PREP(PLANE_COLOR_ALPHA_MASK, 3)
#define _PLANE_BUF_CFG_1_A			0x7027c
#define _PLANE_BUF_CFG_2_A			0x7037c
#define _PLANE_NV12_BUF_CFG_1_A		0x70278
#define _PLANE_NV12_BUF_CFG_2_A		0x70378

#define _PLANE_CC_VAL_1_B		0x711b4
#define _PLANE_CC_VAL_2_B		0x712b4
#define _PLANE_CC_VAL_1(pipe, dw)	(_PIPE(pipe, _PLANE_CC_VAL_1_A, _PLANE_CC_VAL_1_B) + (dw) * 4)
#define _PLANE_CC_VAL_2(pipe, dw)	(_PIPE(pipe, _PLANE_CC_VAL_2_A, _PLANE_CC_VAL_2_B) + (dw) * 4)
#define PLANE_CC_VAL(pipe, plane, dw) \
	_MMIO_PLANE((plane), _PLANE_CC_VAL_1((pipe), (dw)), _PLANE_CC_VAL_2((pipe), (dw)))

/* Input CSC Register Definitions */
#define _PLANE_INPUT_CSC_RY_GY_1_A	0x701E0
#define _PLANE_INPUT_CSC_RY_GY_2_A	0x702E0

#define _PLANE_INPUT_CSC_RY_GY_1_B	0x711E0
#define _PLANE_INPUT_CSC_RY_GY_2_B	0x712E0

#define _PLANE_INPUT_CSC_RY_GY_1(pipe)	\
	_PIPE(pipe, _PLANE_INPUT_CSC_RY_GY_1_A, \
	     _PLANE_INPUT_CSC_RY_GY_1_B)
#define _PLANE_INPUT_CSC_RY_GY_2(pipe)	\
	_PIPE(pipe, _PLANE_INPUT_CSC_RY_GY_2_A, \
	     _PLANE_INPUT_CSC_RY_GY_2_B)

#define PLANE_INPUT_CSC_COEFF(pipe, plane, index)	\
	_MMIO_PLANE(plane, _PLANE_INPUT_CSC_RY_GY_1(pipe) +  (index) * 4, \
		    _PLANE_INPUT_CSC_RY_GY_2(pipe) + (index) * 4)

#define _PLANE_INPUT_CSC_PREOFF_HI_1_A		0x701F8
#define _PLANE_INPUT_CSC_PREOFF_HI_2_A		0x702F8

#define _PLANE_INPUT_CSC_PREOFF_HI_1_B		0x711F8
#define _PLANE_INPUT_CSC_PREOFF_HI_2_B		0x712F8

#define _PLANE_INPUT_CSC_PREOFF_HI_1(pipe)	\
	_PIPE(pipe, _PLANE_INPUT_CSC_PREOFF_HI_1_A, \
	     _PLANE_INPUT_CSC_PREOFF_HI_1_B)
#define _PLANE_INPUT_CSC_PREOFF_HI_2(pipe)	\
	_PIPE(pipe, _PLANE_INPUT_CSC_PREOFF_HI_2_A, \
	     _PLANE_INPUT_CSC_PREOFF_HI_2_B)
#define PLANE_INPUT_CSC_PREOFF(pipe, plane, index)	\
	_MMIO_PLANE(plane, _PLANE_INPUT_CSC_PREOFF_HI_1(pipe) + (index) * 4, \
		    _PLANE_INPUT_CSC_PREOFF_HI_2(pipe) + (index) * 4)

#define _PLANE_INPUT_CSC_POSTOFF_HI_1_A		0x70204
#define _PLANE_INPUT_CSC_POSTOFF_HI_2_A		0x70304

#define _PLANE_INPUT_CSC_POSTOFF_HI_1_B		0x71204
#define _PLANE_INPUT_CSC_POSTOFF_HI_2_B		0x71304

#define _PLANE_INPUT_CSC_POSTOFF_HI_1(pipe)	\
	_PIPE(pipe, _PLANE_INPUT_CSC_POSTOFF_HI_1_A, \
	     _PLANE_INPUT_CSC_POSTOFF_HI_1_B)
#define _PLANE_INPUT_CSC_POSTOFF_HI_2(pipe)	\
	_PIPE(pipe, _PLANE_INPUT_CSC_POSTOFF_HI_2_A, \
	     _PLANE_INPUT_CSC_POSTOFF_HI_2_B)
#define PLANE_INPUT_CSC_POSTOFF(pipe, plane, index)	\
	_MMIO_PLANE(plane, _PLANE_INPUT_CSC_POSTOFF_HI_1(pipe) + (index) * 4, \
		    _PLANE_INPUT_CSC_POSTOFF_HI_2(pipe) + (index) * 4)

#define _PLANE_CTL_1_B				0x71180
#define _PLANE_CTL_2_B				0x71280
#define _PLANE_CTL_3_B				0x71380
#define _PLANE_CTL_1(pipe)	_PIPE(pipe, _PLANE_CTL_1_A, _PLANE_CTL_1_B)
#define _PLANE_CTL_2(pipe)	_PIPE(pipe, _PLANE_CTL_2_A, _PLANE_CTL_2_B)
#define _PLANE_CTL_3(pipe)	_PIPE(pipe, _PLANE_CTL_3_A, _PLANE_CTL_3_B)
#define PLANE_CTL(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_CTL_1(pipe), _PLANE_CTL_2(pipe))

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
	_MMIO_PLANE(plane, _PLANE_STRIDE_1(pipe), _PLANE_STRIDE_2(pipe))

#define _PLANE_POS_1_B				0x7118c
#define _PLANE_POS_2_B				0x7128c
#define _PLANE_POS_3_B				0x7138c
#define _PLANE_POS_1(pipe)	_PIPE(pipe, _PLANE_POS_1_A, _PLANE_POS_1_B)
#define _PLANE_POS_2(pipe)	_PIPE(pipe, _PLANE_POS_2_A, _PLANE_POS_2_B)
#define _PLANE_POS_3(pipe)	_PIPE(pipe, _PLANE_POS_3_A, _PLANE_POS_3_B)
#define PLANE_POS(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_POS_1(pipe), _PLANE_POS_2(pipe))

#define _PLANE_SIZE_1_B				0x71190
#define _PLANE_SIZE_2_B				0x71290
#define _PLANE_SIZE_3_B				0x71390
#define _PLANE_SIZE_1(pipe)	_PIPE(pipe, _PLANE_SIZE_1_A, _PLANE_SIZE_1_B)
#define _PLANE_SIZE_2(pipe)	_PIPE(pipe, _PLANE_SIZE_2_A, _PLANE_SIZE_2_B)
#define _PLANE_SIZE_3(pipe)	_PIPE(pipe, _PLANE_SIZE_3_A, _PLANE_SIZE_3_B)
#define PLANE_SIZE(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_SIZE_1(pipe), _PLANE_SIZE_2(pipe))

#define _PLANE_SURF_1_B				0x7119c
#define _PLANE_SURF_2_B				0x7129c
#define _PLANE_SURF_3_B				0x7139c
#define _PLANE_SURF_1(pipe)	_PIPE(pipe, _PLANE_SURF_1_A, _PLANE_SURF_1_B)
#define _PLANE_SURF_2(pipe)	_PIPE(pipe, _PLANE_SURF_2_A, _PLANE_SURF_2_B)
#define _PLANE_SURF_3(pipe)	_PIPE(pipe, _PLANE_SURF_3_A, _PLANE_SURF_3_B)
#define PLANE_SURF(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_SURF_1(pipe), _PLANE_SURF_2(pipe))

#define _PLANE_OFFSET_1_B			0x711a4
#define _PLANE_OFFSET_2_B			0x712a4
#define _PLANE_OFFSET_1(pipe) _PIPE(pipe, _PLANE_OFFSET_1_A, _PLANE_OFFSET_1_B)
#define _PLANE_OFFSET_2(pipe) _PIPE(pipe, _PLANE_OFFSET_2_A, _PLANE_OFFSET_2_B)
#define PLANE_OFFSET(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_OFFSET_1(pipe), _PLANE_OFFSET_2(pipe))

#define _PLANE_KEYVAL_1_B			0x71194
#define _PLANE_KEYVAL_2_B			0x71294
#define _PLANE_KEYVAL_1(pipe) _PIPE(pipe, _PLANE_KEYVAL_1_A, _PLANE_KEYVAL_1_B)
#define _PLANE_KEYVAL_2(pipe) _PIPE(pipe, _PLANE_KEYVAL_2_A, _PLANE_KEYVAL_2_B)
#define PLANE_KEYVAL(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_KEYVAL_1(pipe), _PLANE_KEYVAL_2(pipe))

#define _PLANE_KEYMSK_1_B			0x71198
#define _PLANE_KEYMSK_2_B			0x71298
#define _PLANE_KEYMSK_1(pipe) _PIPE(pipe, _PLANE_KEYMSK_1_A, _PLANE_KEYMSK_1_B)
#define _PLANE_KEYMSK_2(pipe) _PIPE(pipe, _PLANE_KEYMSK_2_A, _PLANE_KEYMSK_2_B)
#define PLANE_KEYMSK(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_KEYMSK_1(pipe), _PLANE_KEYMSK_2(pipe))

#define _PLANE_KEYMAX_1_B			0x711a0
#define _PLANE_KEYMAX_2_B			0x712a0
#define _PLANE_KEYMAX_1(pipe) _PIPE(pipe, _PLANE_KEYMAX_1_A, _PLANE_KEYMAX_1_B)
#define _PLANE_KEYMAX_2(pipe) _PIPE(pipe, _PLANE_KEYMAX_2_A, _PLANE_KEYMAX_2_B)
#define PLANE_KEYMAX(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_KEYMAX_1(pipe), _PLANE_KEYMAX_2(pipe))

#define _PLANE_BUF_CFG_1_B			0x7127c
#define _PLANE_BUF_CFG_2_B			0x7137c
/* skl+: 10 bits, icl+ 11 bits, adlp+ 12 bits */
#define   PLANE_BUF_END_MASK		REG_GENMASK(27, 16)
#define   PLANE_BUF_END(end)		REG_FIELD_PREP(PLANE_BUF_END_MASK, (end))
#define   PLANE_BUF_START_MASK		REG_GENMASK(11, 0)
#define   PLANE_BUF_START(start)	REG_FIELD_PREP(PLANE_BUF_START_MASK, (start))
#define _PLANE_BUF_CFG_1(pipe)	\
	_PIPE(pipe, _PLANE_BUF_CFG_1_A, _PLANE_BUF_CFG_1_B)
#define _PLANE_BUF_CFG_2(pipe)	\
	_PIPE(pipe, _PLANE_BUF_CFG_2_A, _PLANE_BUF_CFG_2_B)
#define PLANE_BUF_CFG(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_BUF_CFG_1(pipe), _PLANE_BUF_CFG_2(pipe))

#define _PLANE_NV12_BUF_CFG_1_B		0x71278
#define _PLANE_NV12_BUF_CFG_2_B		0x71378
#define _PLANE_NV12_BUF_CFG_1(pipe)	\
	_PIPE(pipe, _PLANE_NV12_BUF_CFG_1_A, _PLANE_NV12_BUF_CFG_1_B)
#define _PLANE_NV12_BUF_CFG_2(pipe)	\
	_PIPE(pipe, _PLANE_NV12_BUF_CFG_2_A, _PLANE_NV12_BUF_CFG_2_B)
#define PLANE_NV12_BUF_CFG(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_NV12_BUF_CFG_1(pipe), _PLANE_NV12_BUF_CFG_2(pipe))

#define _PLANE_AUX_DIST_1_B		0x711c0
#define _PLANE_AUX_DIST_2_B		0x712c0
#define _PLANE_AUX_DIST_1(pipe) \
			_PIPE(pipe, _PLANE_AUX_DIST_1_A, _PLANE_AUX_DIST_1_B)
#define _PLANE_AUX_DIST_2(pipe) \
			_PIPE(pipe, _PLANE_AUX_DIST_2_A, _PLANE_AUX_DIST_2_B)
#define PLANE_AUX_DIST(pipe, plane)     \
	_MMIO_PLANE(plane, _PLANE_AUX_DIST_1(pipe), _PLANE_AUX_DIST_2(pipe))

#define _PLANE_AUX_OFFSET_1_B		0x711c4
#define _PLANE_AUX_OFFSET_2_B		0x712c4
#define _PLANE_AUX_OFFSET_1(pipe)       \
		_PIPE(pipe, _PLANE_AUX_OFFSET_1_A, _PLANE_AUX_OFFSET_1_B)
#define _PLANE_AUX_OFFSET_2(pipe)       \
		_PIPE(pipe, _PLANE_AUX_OFFSET_2_A, _PLANE_AUX_OFFSET_2_B)
#define PLANE_AUX_OFFSET(pipe, plane)   \
	_MMIO_PLANE(plane, _PLANE_AUX_OFFSET_1(pipe), _PLANE_AUX_OFFSET_2(pipe))

#define _PLANE_CUS_CTL_1_B		0x711c8
#define _PLANE_CUS_CTL_2_B		0x712c8
#define _PLANE_CUS_CTL_1(pipe)       \
		_PIPE(pipe, _PLANE_CUS_CTL_1_A, _PLANE_CUS_CTL_1_B)
#define _PLANE_CUS_CTL_2(pipe)       \
		_PIPE(pipe, _PLANE_CUS_CTL_2_A, _PLANE_CUS_CTL_2_B)
#define PLANE_CUS_CTL(pipe, plane)   \
	_MMIO_PLANE(plane, _PLANE_CUS_CTL_1(pipe), _PLANE_CUS_CTL_2(pipe))

#define _PLANE_COLOR_CTL_1_B			0x711CC
#define _PLANE_COLOR_CTL_2_B			0x712CC
#define _PLANE_COLOR_CTL_3_B			0x713CC
#define _PLANE_COLOR_CTL_1(pipe)	\
	_PIPE(pipe, _PLANE_COLOR_CTL_1_A, _PLANE_COLOR_CTL_1_B)
#define _PLANE_COLOR_CTL_2(pipe)	\
	_PIPE(pipe, _PLANE_COLOR_CTL_2_A, _PLANE_COLOR_CTL_2_B)
#define PLANE_COLOR_CTL(pipe, plane)	\
	_MMIO_PLANE(plane, _PLANE_COLOR_CTL_1(pipe), _PLANE_COLOR_CTL_2(pipe))

#define _SEL_FETCH_PLANE_BASE_1_A		0x70890
#define _SEL_FETCH_PLANE_BASE_2_A		0x708B0
#define _SEL_FETCH_PLANE_BASE_3_A		0x708D0
#define _SEL_FETCH_PLANE_BASE_4_A		0x708F0
#define _SEL_FETCH_PLANE_BASE_5_A		0x70920
#define _SEL_FETCH_PLANE_BASE_6_A		0x70940
#define _SEL_FETCH_PLANE_BASE_7_A		0x70960
#define _SEL_FETCH_PLANE_BASE_CUR_A		0x70880
#define _SEL_FETCH_PLANE_BASE_1_B		0x70990

#define _SEL_FETCH_PLANE_BASE_A(plane) _PICK(plane, \
					     _SEL_FETCH_PLANE_BASE_1_A, \
					     _SEL_FETCH_PLANE_BASE_2_A, \
					     _SEL_FETCH_PLANE_BASE_3_A, \
					     _SEL_FETCH_PLANE_BASE_4_A, \
					     _SEL_FETCH_PLANE_BASE_5_A, \
					     _SEL_FETCH_PLANE_BASE_6_A, \
					     _SEL_FETCH_PLANE_BASE_7_A, \
					     _SEL_FETCH_PLANE_BASE_CUR_A)
#define _SEL_FETCH_PLANE_BASE_1(pipe) _PIPE(pipe, _SEL_FETCH_PLANE_BASE_1_A, _SEL_FETCH_PLANE_BASE_1_B)
#define _SEL_FETCH_PLANE_BASE(pipe, plane) (_SEL_FETCH_PLANE_BASE_1(pipe) - \
					    _SEL_FETCH_PLANE_BASE_1_A + \
					    _SEL_FETCH_PLANE_BASE_A(plane))

#define _SEL_FETCH_PLANE_CTL_1_A		0x70890
#define PLANE_SEL_FETCH_CTL(pipe, plane) _MMIO(_SEL_FETCH_PLANE_BASE(pipe, plane) + \
					       _SEL_FETCH_PLANE_CTL_1_A - \
					       _SEL_FETCH_PLANE_BASE_1_A)
#define PLANE_SEL_FETCH_CTL_ENABLE		REG_BIT(31)

#define _SEL_FETCH_PLANE_POS_1_A		0x70894
#define PLANE_SEL_FETCH_POS(pipe, plane) _MMIO(_SEL_FETCH_PLANE_BASE(pipe, plane) + \
					       _SEL_FETCH_PLANE_POS_1_A - \
					       _SEL_FETCH_PLANE_BASE_1_A)

#define _SEL_FETCH_PLANE_SIZE_1_A		0x70898
#define PLANE_SEL_FETCH_SIZE(pipe, plane) _MMIO(_SEL_FETCH_PLANE_BASE(pipe, plane) + \
						_SEL_FETCH_PLANE_SIZE_1_A - \
						_SEL_FETCH_PLANE_BASE_1_A)

#define _SEL_FETCH_PLANE_OFFSET_1_A		0x7089C
#define PLANE_SEL_FETCH_OFFSET(pipe, plane) _MMIO(_SEL_FETCH_PLANE_BASE(pipe, plane) + \
						  _SEL_FETCH_PLANE_OFFSET_1_A - \
						  _SEL_FETCH_PLANE_BASE_1_A)

/* SKL new cursor registers */
#define _CUR_BUF_CFG_A				0x7017c
#define _CUR_BUF_CFG_B				0x7117c
#define CUR_BUF_CFG(pipe)	_MMIO_PIPE(pipe, _CUR_BUF_CFG_A, _CUR_BUF_CFG_B)

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

#define FDI_PLL_BIOS_0  _MMIO(0x46000)
#define  FDI_PLL_FB_CLOCK_MASK  0xff
#define FDI_PLL_BIOS_1  _MMIO(0x46004)
#define FDI_PLL_BIOS_2  _MMIO(0x46008)
#define DISPLAY_PORT_PLL_BIOS_0         _MMIO(0x4600c)
#define DISPLAY_PORT_PLL_BIOS_1         _MMIO(0x46010)
#define DISPLAY_PORT_PLL_BIOS_2         _MMIO(0x46014)

#define PCH_3DCGDIS0		_MMIO(0x46020)
# define MARIUNIT_CLOCK_GATE_DISABLE		(1 << 18)
# define SVSMUNIT_CLOCK_GATE_DISABLE		(1 << 1)

#define PCH_3DCGDIS1		_MMIO(0x46024)
# define VFMUNIT_CLOCK_GATE_DISABLE		(1 << 11)

#define FDI_PLL_FREQ_CTL        _MMIO(0x46030)
#define  FDI_PLL_FREQ_CHANGE_REQUEST    (1 << 24)
#define  FDI_PLL_FREQ_LOCK_LIMIT_MASK   0xfff00
#define  FDI_PLL_FREQ_DISABLE_COUNT_LIMIT_MASK  0xff


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

#define PIPE_DATA_M1(tran) _MMIO_TRANS2(tran, _PIPEA_DATA_M1)
#define PIPE_DATA_N1(tran) _MMIO_TRANS2(tran, _PIPEA_DATA_N1)
#define PIPE_DATA_M2(tran) _MMIO_TRANS2(tran, _PIPEA_DATA_M2)
#define PIPE_DATA_N2(tran) _MMIO_TRANS2(tran, _PIPEA_DATA_N2)
#define PIPE_LINK_M1(tran) _MMIO_TRANS2(tran, _PIPEA_LINK_M1)
#define PIPE_LINK_N1(tran) _MMIO_TRANS2(tran, _PIPEA_LINK_N1)
#define PIPE_LINK_M2(tran) _MMIO_TRANS2(tran, _PIPEA_LINK_M2)
#define PIPE_LINK_N2(tran) _MMIO_TRANS2(tran, _PIPEA_LINK_N2)

/* CPU panel fitter */
/* IVB+ has 3 fitters, 0 is 7x5 capable, the other two only 3x3 */
#define _PFA_CTL_1               0x68080
#define _PFB_CTL_1               0x68880
#define  PF_ENABLE              (1 << 31)
#define  PF_PIPE_SEL_MASK_IVB	(3 << 29)
#define  PF_PIPE_SEL_IVB(pipe)	((pipe) << 29)
#define  PF_FILTER_MASK		(3 << 23)
#define  PF_FILTER_PROGRAMMED	(0 << 23)
#define  PF_FILTER_MED_3x3	(1 << 23)
#define  PF_FILTER_EDGE_ENHANCE	(2 << 23)
#define  PF_FILTER_EDGE_SOFTEN	(3 << 23)
#define _PFA_WIN_SZ		0x68074
#define _PFB_WIN_SZ		0x68874
#define _PFA_WIN_POS		0x68070
#define _PFB_WIN_POS		0x68870
#define _PFA_VSCALE		0x68084
#define _PFB_VSCALE		0x68884
#define _PFA_HSCALE		0x68090
#define _PFB_HSCALE		0x68890

#define PF_CTL(pipe)		_MMIO_PIPE(pipe, _PFA_CTL_1, _PFB_CTL_1)
#define PF_WIN_SZ(pipe)		_MMIO_PIPE(pipe, _PFA_WIN_SZ, _PFB_WIN_SZ)
#define PF_WIN_POS(pipe)	_MMIO_PIPE(pipe, _PFA_WIN_POS, _PFB_WIN_POS)
#define PF_VSCALE(pipe)		_MMIO_PIPE(pipe, _PFA_VSCALE, _PFB_VSCALE)
#define PF_HSCALE(pipe)		_MMIO_PIPE(pipe, _PFA_HSCALE, _PFB_HSCALE)

#define _PSA_CTL		0x68180
#define _PSB_CTL		0x68980
#define PS_ENABLE		(1 << 31)
#define _PSA_WIN_SZ		0x68174
#define _PSB_WIN_SZ		0x68974
#define _PSA_WIN_POS		0x68170
#define _PSB_WIN_POS		0x68970

#define PS_CTL(pipe)		_MMIO_PIPE(pipe, _PSA_CTL, _PSB_CTL)
#define PS_WIN_SZ(pipe)		_MMIO_PIPE(pipe, _PSA_WIN_SZ, _PSB_WIN_SZ)
#define PS_WIN_POS(pipe)	_MMIO_PIPE(pipe, _PSA_WIN_POS, _PSB_WIN_POS)

/*
 * Skylake scalers
 */
#define _PS_1A_CTRL      0x68180
#define _PS_2A_CTRL      0x68280
#define _PS_1B_CTRL      0x68980
#define _PS_2B_CTRL      0x68A80
#define _PS_1C_CTRL      0x69180
#define PS_SCALER_EN        (1 << 31)
#define SKL_PS_SCALER_MODE_MASK (3 << 28)
#define SKL_PS_SCALER_MODE_DYN  (0 << 28)
#define SKL_PS_SCALER_MODE_HQ  (1 << 28)
#define SKL_PS_SCALER_MODE_NV12 (2 << 28)
#define PS_SCALER_MODE_PLANAR (1 << 29)
#define PS_SCALER_MODE_NORMAL (0 << 29)
#define PS_PLANE_SEL_MASK  (7 << 25)
#define PS_PLANE_SEL(plane) (((plane) + 1) << 25)
#define PS_FILTER_MASK         (3 << 23)
#define PS_FILTER_MEDIUM       (0 << 23)
#define PS_FILTER_PROGRAMMED   (1 << 23)
#define PS_FILTER_EDGE_ENHANCE (2 << 23)
#define PS_FILTER_BILINEAR     (3 << 23)
#define PS_VERT3TAP            (1 << 21)
#define PS_VERT_INT_INVERT_FIELD1 (0 << 20)
#define PS_VERT_INT_INVERT_FIELD0 (1 << 20)
#define PS_PWRUP_PROGRESS         (1 << 17)
#define PS_V_FILTER_BYPASS        (1 << 8)
#define PS_VADAPT_EN              (1 << 7)
#define PS_VADAPT_MODE_MASK        (3 << 5)
#define PS_VADAPT_MODE_LEAST_ADAPT (0 << 5)
#define PS_VADAPT_MODE_MOD_ADAPT   (1 << 5)
#define PS_VADAPT_MODE_MOST_ADAPT  (3 << 5)
#define PS_PLANE_Y_SEL_MASK  (7 << 5)
#define PS_PLANE_Y_SEL(plane) (((plane) + 1) << 5)
#define PS_Y_VERT_FILTER_SELECT(set)   ((set) << 4)
#define PS_Y_HORZ_FILTER_SELECT(set)   ((set) << 3)
#define PS_UV_VERT_FILTER_SELECT(set)  ((set) << 2)
#define PS_UV_HORZ_FILTER_SELECT(set)  ((set) << 1)

#define _PS_PWR_GATE_1A     0x68160
#define _PS_PWR_GATE_2A     0x68260
#define _PS_PWR_GATE_1B     0x68960
#define _PS_PWR_GATE_2B     0x68A60
#define _PS_PWR_GATE_1C     0x69160
#define PS_PWR_GATE_DIS_OVERRIDE       (1 << 31)
#define PS_PWR_GATE_SETTLING_TIME_32   (0 << 3)
#define PS_PWR_GATE_SETTLING_TIME_64   (1 << 3)
#define PS_PWR_GATE_SETTLING_TIME_96   (2 << 3)
#define PS_PWR_GATE_SETTLING_TIME_128  (3 << 3)
#define PS_PWR_GATE_SLPEN_8             0
#define PS_PWR_GATE_SLPEN_16            1
#define PS_PWR_GATE_SLPEN_24            2
#define PS_PWR_GATE_SLPEN_32            3

#define _PS_WIN_POS_1A      0x68170
#define _PS_WIN_POS_2A      0x68270
#define _PS_WIN_POS_1B      0x68970
#define _PS_WIN_POS_2B      0x68A70
#define _PS_WIN_POS_1C      0x69170

#define _PS_WIN_SZ_1A       0x68174
#define _PS_WIN_SZ_2A       0x68274
#define _PS_WIN_SZ_1B       0x68974
#define _PS_WIN_SZ_2B       0x68A74
#define _PS_WIN_SZ_1C       0x69174

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
#define  PS_Y_PHASE(x)		((x) << 16)
#define  PS_UV_RGB_PHASE(x)	((x) << 0)
#define   PS_PHASE_MASK	(0x7fff << 1) /* u2.13 */
#define   PS_PHASE_TRIP	(1 << 0)

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
#define PS_COEE_INDEX_AUTO_INC	   (1 << 10)

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
/* legacy palette */
#define _LGC_PALETTE_A           0x4a000
#define _LGC_PALETTE_B           0x4a800
#define LGC_PALETTE_RED_MASK     REG_GENMASK(23, 16)
#define LGC_PALETTE_GREEN_MASK   REG_GENMASK(15, 8)
#define LGC_PALETTE_BLUE_MASK    REG_GENMASK(7, 0)
#define LGC_PALETTE(pipe, i) _MMIO(_PIPE(pipe, _LGC_PALETTE_A, _LGC_PALETTE_B) + (i) * 4)

/* ilk/snb precision palette */
#define _PREC_PALETTE_A           0x4b000
#define _PREC_PALETTE_B           0x4c000
#define   PREC_PALETTE_RED_MASK   REG_GENMASK(29, 20)
#define   PREC_PALETTE_GREEN_MASK REG_GENMASK(19, 10)
#define   PREC_PALETTE_BLUE_MASK  REG_GENMASK(9, 0)
#define PREC_PALETTE(pipe, i) _MMIO(_PIPE(pipe, _PREC_PALETTE_A, _PREC_PALETTE_B) + (i) * 4)

#define  _PREC_PIPEAGCMAX              0x4d000
#define  _PREC_PIPEBGCMAX              0x4d010
#define PREC_PIPEGCMAX(pipe, i)        _MMIO(_PIPE(pipe, _PIPEAGCMAX, _PIPEBGCMAX) + (i) * 4)

#define _GAMMA_MODE_A		0x4a480
#define _GAMMA_MODE_B		0x4ac80
#define GAMMA_MODE(pipe) _MMIO_PIPE(pipe, _GAMMA_MODE_A, _GAMMA_MODE_B)
#define  PRE_CSC_GAMMA_ENABLE	(1 << 31)
#define  POST_CSC_GAMMA_ENABLE	(1 << 30)
#define  GAMMA_MODE_MODE_MASK	(3 << 0)
#define  GAMMA_MODE_MODE_8BIT	(0 << 0)
#define  GAMMA_MODE_MODE_10BIT	(1 << 0)
#define  GAMMA_MODE_MODE_12BIT	(2 << 0)
#define  GAMMA_MODE_MODE_SPLIT	(3 << 0) /* ivb-bdw */
#define  GAMMA_MODE_MODE_12BIT_MULTI_SEGMENTED	(3 << 0) /* icl + */

/* DMC */
#define DMC_PROGRAM(addr, i)	_MMIO((addr) + (i) * 4)
#define DMC_SSP_BASE_ADDR_GEN9	0x00002FC0
#define DMC_HTP_ADDR_SKL	0x00500034
#define DMC_SSP_BASE		_MMIO(0x8F074)
#define DMC_HTP_SKL		_MMIO(0x8F004)
#define DMC_LAST_WRITE		_MMIO(0x8F034)
#define DMC_LAST_WRITE_VALUE	0xc003b400
/* MMIO address range for DMC program (0x80000 - 0x82FFF) */
#define DMC_MMIO_START_RANGE	0x80000
#define DMC_MMIO_END_RANGE	0x8FFFF
#define SKL_DMC_DC3_DC5_COUNT	_MMIO(0x80030)
#define SKL_DMC_DC5_DC6_COUNT	_MMIO(0x8002C)
#define BXT_DMC_DC3_DC5_COUNT	_MMIO(0x80038)
#define TGL_DMC_DEBUG_DC5_COUNT	_MMIO(0x101084)
#define TGL_DMC_DEBUG_DC6_COUNT	_MMIO(0x101088)
#define DG1_DMC_DEBUG_DC5_COUNT	_MMIO(0x134154)

#define TGL_DMC_DEBUG3		_MMIO(0x101090)
#define DG1_DMC_DEBUG3		_MMIO(0x13415c)

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
#define  GEN8_DE_MISC_GSE		(1 << 27)
#define  GEN8_DE_EDP_PSR		(1 << 19)

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

#define ILK_DISPLAY_CHICKEN2	_MMIO(0x42004)
/* Required on all Ironlake and Sandybridge according to the B-Spec. */
#define  ILK_ELPIN_409_SELECT	(1 << 25)
#define  ILK_DPARB_GATE	(1 << 22)
#define  ILK_VSDPFD_FULL	(1 << 21)
#define FUSE_STRAP			_MMIO(0x42014)
#define  ILK_INTERNAL_GRAPHICS_DISABLE	(1 << 31)
#define  ILK_INTERNAL_DISPLAY_DISABLE	(1 << 30)
#define  ILK_DISPLAY_DEBUG_DISABLE	(1 << 29)
#define  IVB_PIPE_C_DISABLE		(1 << 28)
#define  ILK_HDCP_DISABLE		(1 << 25)
#define  ILK_eDP_A_DISABLE		(1 << 24)
#define  HSW_CDCLK_LIMIT		(1 << 24)
#define  ILK_DESKTOP			(1 << 23)
#define  HSW_CPU_SSC_ENABLE		(1 << 21)

#define FUSE_STRAP3			_MMIO(0x42020)
#define  HSW_REF_CLK_SELECT		(1 << 1)

#define ILK_DSPCLK_GATE_D			_MMIO(0x42020)
#define   ILK_VRHUNIT_CLOCK_GATE_DISABLE	(1 << 28)
#define   ILK_DPFCUNIT_CLOCK_GATE_DISABLE	(1 << 9)
#define   ILK_DPFCRUNIT_CLOCK_GATE_DISABLE	(1 << 8)
#define   ILK_DPFDUNIT_CLOCK_GATE_ENABLE	(1 << 7)
#define   ILK_DPARBUNIT_CLOCK_GATE_ENABLE	(1 << 5)

#define IVB_CHICKEN3	_MMIO(0x4200c)
# define CHICKEN3_DGMG_REQ_OUT_FIX_DISABLE	(1 << 5)
# define CHICKEN3_DGMG_DONE_FIX_DISABLE		(1 << 2)

#define CHICKEN_PAR1_1			_MMIO(0x42080)
#define  IGNORE_KVMR_PIPE_A		REG_BIT(23)
#define  KBL_ARB_FILL_SPARE_22		REG_BIT(22)
#define  DIS_RAM_BYPASS_PSR2_MAN_TRACK	(1 << 16)
#define  SKL_DE_COMPRESSED_HASH_MODE	(1 << 15)
#define  DPA_MASK_VBLANK_SRD		(1 << 15)
#define  FORCE_ARB_IDLE_PLANES		(1 << 14)
#define  SKL_EDP_PSR_FIX_RDWRAP		(1 << 3)
#define  IGNORE_PSR2_HW_TRACKING	(1 << 1)

#define CHICKEN_PAR2_1		_MMIO(0x42090)
#define  KVM_CONFIG_CHANGE_NOTIFICATION_SELECT	(1 << 14)

#define CHICKEN_MISC_2		_MMIO(0x42084)
#define  KBL_ARB_FILL_SPARE_14	REG_BIT(14)
#define  KBL_ARB_FILL_SPARE_13	REG_BIT(13)
#define  GLK_CL2_PWR_DOWN	(1 << 12)
#define  GLK_CL1_PWR_DOWN	(1 << 11)
#define  GLK_CL0_PWR_DOWN	(1 << 10)

#define CHICKEN_MISC_4		_MMIO(0x4208c)
#define   CHICKEN_FBC_STRIDE_OVERRIDE	REG_BIT(13)
#define   CHICKEN_FBC_STRIDE_MASK	REG_GENMASK(12, 0)
#define   CHICKEN_FBC_STRIDE(x)		REG_FIELD_PREP(CHICKEN_FBC_STRIDE_MASK, (x))

#define _CHICKEN_PIPESL_1_A	0x420b0
#define _CHICKEN_PIPESL_1_B	0x420b4
#define  HSW_PRI_STRETCH_MAX_MASK	REG_GENMASK(28, 27)
#define  HSW_PRI_STRETCH_MAX_X8		REG_FIELD_PREP(HSW_PRI_STRETCH_MAX_MASK, 0)
#define  HSW_PRI_STRETCH_MAX_X4		REG_FIELD_PREP(HSW_PRI_STRETCH_MAX_MASK, 1)
#define  HSW_PRI_STRETCH_MAX_X2		REG_FIELD_PREP(HSW_PRI_STRETCH_MAX_MASK, 2)
#define  HSW_PRI_STRETCH_MAX_X1		REG_FIELD_PREP(HSW_PRI_STRETCH_MAX_MASK, 3)
#define  HSW_SPR_STRETCH_MAX_MASK	REG_GENMASK(26, 25)
#define  HSW_SPR_STRETCH_MAX_X8		REG_FIELD_PREP(HSW_SPR_STRETCH_MAX_MASK, 0)
#define  HSW_SPR_STRETCH_MAX_X4		REG_FIELD_PREP(HSW_SPR_STRETCH_MAX_MASK, 1)
#define  HSW_SPR_STRETCH_MAX_X2		REG_FIELD_PREP(HSW_SPR_STRETCH_MAX_MASK, 2)
#define  HSW_SPR_STRETCH_MAX_X1		REG_FIELD_PREP(HSW_SPR_STRETCH_MAX_MASK, 3)
#define  HSW_FBCQ_DIS			(1 << 22)
#define  BDW_DPRS_MASK_VBLANK_SRD	(1 << 0)
#define  SKL_PLANE1_STRETCH_MAX_MASK	REG_GENMASK(1, 0)
#define  SKL_PLANE1_STRETCH_MAX_X8	REG_FIELD_PREP(SKL_PLANE1_STRETCH_MAX_MASK, 0)
#define  SKL_PLANE1_STRETCH_MAX_X4	REG_FIELD_PREP(SKL_PLANE1_STRETCH_MAX_MASK, 1)
#define  SKL_PLANE1_STRETCH_MAX_X2	REG_FIELD_PREP(SKL_PLANE1_STRETCH_MAX_MASK, 2)
#define  SKL_PLANE1_STRETCH_MAX_X1	REG_FIELD_PREP(SKL_PLANE1_STRETCH_MAX_MASK, 3)
#define CHICKEN_PIPESL_1(pipe) _MMIO_PIPE(pipe, _CHICKEN_PIPESL_1_A, _CHICKEN_PIPESL_1_B)

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
#define  HSW_FRAME_START_DELAY_MASK	REG_GENMASK(28, 27)
#define  HSW_FRAME_START_DELAY(x)	REG_FIELD_PREP(HSW_FRAME_START_DELAY_MASK, x)
#define  VSC_DATA_SEL_SOFTWARE_CONTROL	REG_BIT(25) /* GLK */
#define  FECSTALL_DIS_DPTSTREAM_DPTTG	REG_BIT(23)
#define  DDI_TRAINING_OVERRIDE_ENABLE	REG_BIT(19)
#define  ADLP_1_BASED_X_GRANULARITY	REG_BIT(18)
#define  DDI_TRAINING_OVERRIDE_VALUE	REG_BIT(18)
#define  DDIE_TRAINING_OVERRIDE_ENABLE	REG_BIT(17) /* CHICKEN_TRANS_A only */
#define  DDIE_TRAINING_OVERRIDE_VALUE	REG_BIT(16) /* CHICKEN_TRANS_A only */
#define  PSR2_ADD_VERTICAL_LINE_COUNT	REG_BIT(15)
#define  PSR2_VSC_ENABLE_PROG_HEADER	REG_BIT(12)

#define DISP_ARB_CTL	_MMIO(0x45000)
#define  DISP_FBC_MEMORY_WAKE		(1 << 31)
#define  DISP_TILE_SURFACE_SWIZZLING	(1 << 13)
#define  DISP_FBC_WM_DIS		(1 << 15)
#define DISP_ARB_CTL2	_MMIO(0x45004)
#define  DISP_DATA_PARTITION_5_6	(1 << 6)
#define  DISP_IPC_ENABLE		(1 << 3)

/*
 * The below are numbered starting from "S1" on gen11/gen12, but starting
 * with display 13, the bspec switches to a 0-based numbering scheme
 * (although the addresses stay the same so new S0 = old S1, new S1 = old S2).
 * We'll just use the 0-based numbering here for all platforms since it's the
 * way things will be named by the hardware team going forward, plus it's more
 * consistent with how most of the rest of our registers are named.
 */
#define _DBUF_CTL_S0				0x45008
#define _DBUF_CTL_S1				0x44FE8
#define _DBUF_CTL_S2				0x44300
#define _DBUF_CTL_S3				0x44304
#define DBUF_CTL_S(slice)			_MMIO(_PICK(slice, \
							    _DBUF_CTL_S0, \
							    _DBUF_CTL_S1, \
							    _DBUF_CTL_S2, \
							    _DBUF_CTL_S3))
#define  DBUF_POWER_REQUEST			REG_BIT(31)
#define  DBUF_POWER_STATE			REG_BIT(30)
#define  DBUF_TRACKER_STATE_SERVICE_MASK	REG_GENMASK(23, 19)
#define  DBUF_TRACKER_STATE_SERVICE(x)		REG_FIELD_PREP(DBUF_TRACKER_STATE_SERVICE_MASK, x)
#define  DBUF_MIN_TRACKER_STATE_SERVICE_MASK	REG_GENMASK(18, 16) /* ADL-P+ */
#define  DBUF_MIN_TRACKER_STATE_SERVICE(x)		REG_FIELD_PREP(DBUF_MIN_TRACKER_STATE_SERVICE_MASK, x) /* ADL-P+ */

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
#define  RESET_PCH_HANDSHAKE_ENABLE	(1 << 4)

#define GEN8_CHICKEN_DCPR_1			_MMIO(0x46430)
#define   SKL_SELECT_ALTERNATE_DC_EXIT		REG_BIT(30)
#define   LATENCY_REPORTING_REMOVED_PIPE_C	REG_BIT(25)
#define   LATENCY_REPORTING_REMOVED_PIPE_B	REG_BIT(24)
#define   LATENCY_REPORTING_REMOVED_PIPE_A	REG_BIT(23)
#define   ICL_DELAY_PMRSP			REG_BIT(22)
#define   DISABLE_FLR_SRC			REG_BIT(15)
#define   MASK_WAKEMEM				REG_BIT(13)

#define GEN11_CHICKEN_DCPR_2			_MMIO(0x46434)
#define   DCPR_MASK_MAXLATENCY_MEMUP_CLR	REG_BIT(27)
#define   DCPR_MASK_LPMODE			REG_BIT(26)
#define   DCPR_SEND_RESP_IMM			REG_BIT(25)
#define   DCPR_CLEAR_MEMSTAT_DIS		REG_BIT(24)

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

#define SKL_DSSM				_MMIO(0x51004)
#define ICL_DSSM_CDCLK_PLL_REFCLK_MASK		(7 << 29)
#define ICL_DSSM_CDCLK_PLL_REFCLK_24MHz		(0 << 29)
#define ICL_DSSM_CDCLK_PLL_REFCLK_19_2MHz	(1 << 29)
#define ICL_DSSM_CDCLK_PLL_REFCLK_38_4MHz	(2 << 29)

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

/* south display engine interrupt: ICP/TGP */
#define SDE_GMBUS_ICP			(1 << 23)
#define SDE_TC_HOTPLUG_ICP(hpd_pin)	REG_BIT(24 + _HPD_PIN_TC(hpd_pin))
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
#define _VLV_VIDEO_DIP_CTL_A		(VLV_DISPLAY_BASE + 0x60200)
#define _VLV_VIDEO_DIP_DATA_A		(VLV_DISPLAY_BASE + 0x60208)
#define _VLV_VIDEO_DIP_GDCP_PAYLOAD_A	(VLV_DISPLAY_BASE + 0x60210)

#define _VLV_VIDEO_DIP_CTL_B		(VLV_DISPLAY_BASE + 0x61170)
#define _VLV_VIDEO_DIP_DATA_B		(VLV_DISPLAY_BASE + 0x61174)
#define _VLV_VIDEO_DIP_GDCP_PAYLOAD_B	(VLV_DISPLAY_BASE + 0x61178)

#define _CHV_VIDEO_DIP_CTL_C		(VLV_DISPLAY_BASE + 0x611f0)
#define _CHV_VIDEO_DIP_DATA_C		(VLV_DISPLAY_BASE + 0x611f4)
#define _CHV_VIDEO_DIP_GDCP_PAYLOAD_C	(VLV_DISPLAY_BASE + 0x611f8)

#define VLV_TVIDEO_DIP_CTL(pipe) \
	_MMIO_PIPE3((pipe), _VLV_VIDEO_DIP_CTL_A, \
	       _VLV_VIDEO_DIP_CTL_B, _CHV_VIDEO_DIP_CTL_C)
#define VLV_TVIDEO_DIP_DATA(pipe) \
	_MMIO_PIPE3((pipe), _VLV_VIDEO_DIP_DATA_A, \
	       _VLV_VIDEO_DIP_DATA_B, _CHV_VIDEO_DIP_DATA_C)
#define VLV_TVIDEO_DIP_GCP(pipe) \
	_MMIO_PIPE3((pipe), _VLV_VIDEO_DIP_GDCP_PAYLOAD_A, \
		_VLV_VIDEO_DIP_GDCP_PAYLOAD_B, _CHV_VIDEO_DIP_GDCP_PAYLOAD_C)

/* Haswell DIP controls */

#define _HSW_VIDEO_DIP_CTL_A		0x60200
#define _HSW_VIDEO_DIP_AVI_DATA_A	0x60220
#define _HSW_VIDEO_DIP_VS_DATA_A	0x60260
#define _HSW_VIDEO_DIP_SPD_DATA_A	0x602A0
#define _HSW_VIDEO_DIP_GMP_DATA_A	0x602E0
#define _HSW_VIDEO_DIP_VSC_DATA_A	0x60320
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

#define HSW_TVIDEO_DIP_CTL(trans)		_MMIO_TRANS2(trans, _HSW_VIDEO_DIP_CTL_A)
#define HSW_TVIDEO_DIP_GCP(trans)		_MMIO_TRANS2(trans, _HSW_VIDEO_DIP_GCP_A)
#define HSW_TVIDEO_DIP_AVI_DATA(trans, i)	_MMIO_TRANS2(trans, _HSW_VIDEO_DIP_AVI_DATA_A + (i) * 4)
#define HSW_TVIDEO_DIP_VS_DATA(trans, i)	_MMIO_TRANS2(trans, _HSW_VIDEO_DIP_VS_DATA_A + (i) * 4)
#define HSW_TVIDEO_DIP_SPD_DATA(trans, i)	_MMIO_TRANS2(trans, _HSW_VIDEO_DIP_SPD_DATA_A + (i) * 4)
#define HSW_TVIDEO_DIP_GMP_DATA(trans, i)	_MMIO_TRANS2(trans, _HSW_VIDEO_DIP_GMP_DATA_A + (i) * 4)
#define HSW_TVIDEO_DIP_VSC_DATA(trans, i)	_MMIO_TRANS2(trans, _HSW_VIDEO_DIP_VSC_DATA_A + (i) * 4)
#define GLK_TVIDEO_DIP_DRM_DATA(trans, i)	_MMIO_TRANS2(trans, _GLK_VIDEO_DIP_DRM_DATA_A + (i) * 4)
#define ICL_VIDEO_DIP_PPS_DATA(trans, i)	_MMIO_TRANS2(trans, _ICL_VIDEO_DIP_PPS_DATA_A + (i) * 4)
#define ICL_VIDEO_DIP_PPS_ECC(trans, i)		_MMIO_TRANS2(trans, _ICL_VIDEO_DIP_PPS_ECC_A + (i) * 4)

#define _HSW_STEREO_3D_CTL_A		0x70020
#define   S3D_ENABLE			(1 << 31)
#define _HSW_STEREO_3D_CTL_B		0x71020

#define HSW_STEREO_3D_CTL(trans)	_MMIO_PIPE2(trans, _HSW_STEREO_3D_CTL_A)

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
#define  TRANS_CHICKEN1_HDMIUNIT_GC_DISABLE	(1 << 10)
#define  TRANS_CHICKEN1_DP0UNIT_GC_DISABLE	(1 << 4)
#define _TRANSA_CHICKEN2	 0xf0064
#define _TRANSB_CHICKEN2	 0xf1064
#define TRANS_CHICKEN2(pipe)	_MMIO_PIPE(pipe, _TRANSA_CHICKEN2, _TRANSB_CHICKEN2)
#define  TRANS_CHICKEN2_TIMING_OVERRIDE			(1 << 31)
#define  TRANS_CHICKEN2_FDI_POLARITY_REVERSED		(1 << 29)
#define  TRANS_CHICKEN2_FRAME_START_DELAY_MASK		(3 << 27)
#define  TRANS_CHICKEN2_FRAME_START_DELAY(x)		((x) << 27) /* 0-3 */
#define  TRANS_CHICKEN2_DISABLE_DEEP_COLOR_COUNTER	(1 << 26)
#define  TRANS_CHICKEN2_DISABLE_DEEP_COLOR_MODESWITCH	(1 << 25)

#define SOUTH_CHICKEN1		_MMIO(0xc2000)
#define  FDIA_PHASE_SYNC_SHIFT_OVR	19
#define  FDIA_PHASE_SYNC_SHIFT_EN	18
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
#define  SPT_PWM_GRANULARITY		(1 << 0)
#define SOUTH_CHICKEN2		_MMIO(0xc2004)
#define  FDI_MPHY_IOSFSB_RESET_STATUS	(1 << 13)
#define  FDI_MPHY_IOSFSB_RESET_CTL	(1 << 12)
#define  LPT_PWM_GRANULARITY		(1 << 5)
#define  DPLS_EDP_PPS_FIX_DIS		(1 << 0)

#define _FDI_RXA_CHICKEN        0xc200c
#define _FDI_RXB_CHICKEN        0xc2010
#define  FDI_RX_PHASE_SYNC_POINTER_OVR	(1 << 1)
#define  FDI_RX_PHASE_SYNC_POINTER_EN	(1 << 0)
#define FDI_RX_CHICKEN(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_CHICKEN, _FDI_RXB_CHICKEN)

#define SOUTH_DSPCLK_GATE_D	_MMIO(0xc2020)
#define  PCH_GMBUSUNIT_CLOCK_GATE_DISABLE (1 << 31)
#define  PCH_DPLUNIT_CLOCK_GATE_DISABLE (1 << 30)
#define  PCH_DPLSUNIT_CLOCK_GATE_DISABLE (1 << 29)
#define  PCH_DPMGUNIT_CLOCK_GATE_DISABLE (1 << 15)
#define  PCH_CPUNIT_CLOCK_GATE_DISABLE (1 << 14)
#define  CNP_PWM_CGE_GATING_DISABLE (1 << 13)
#define  PCH_LP_PARTITION_LEVEL_DISABLE  (1 << 12)

/* CPU: FDI_TX */
#define _FDI_TXA_CTL            0x60100
#define _FDI_TXB_CTL            0x61100
#define FDI_TX_CTL(pipe)	_MMIO_PIPE(pipe, _FDI_TXA_CTL, _FDI_TXB_CTL)
#define  FDI_TX_DISABLE         (0 << 31)
#define  FDI_TX_ENABLE          (1 << 31)
#define  FDI_LINK_TRAIN_PATTERN_1       (0 << 28)
#define  FDI_LINK_TRAIN_PATTERN_2       (1 << 28)
#define  FDI_LINK_TRAIN_PATTERN_IDLE    (2 << 28)
#define  FDI_LINK_TRAIN_NONE            (3 << 28)
#define  FDI_LINK_TRAIN_VOLTAGE_0_4V    (0 << 25)
#define  FDI_LINK_TRAIN_VOLTAGE_0_6V    (1 << 25)
#define  FDI_LINK_TRAIN_VOLTAGE_0_8V    (2 << 25)
#define  FDI_LINK_TRAIN_VOLTAGE_1_2V    (3 << 25)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_NONE (0 << 22)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_1_5X (1 << 22)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_2X   (2 << 22)
#define  FDI_LINK_TRAIN_PRE_EMPHASIS_3X   (3 << 22)
/* ILK always use 400mV 0dB for voltage swing and pre-emphasis level.
   SNB has different settings. */
/* SNB A-stepping */
#define  FDI_LINK_TRAIN_400MV_0DB_SNB_A		(0x38 << 22)
#define  FDI_LINK_TRAIN_400MV_6DB_SNB_A		(0x02 << 22)
#define  FDI_LINK_TRAIN_600MV_3_5DB_SNB_A	(0x01 << 22)
#define  FDI_LINK_TRAIN_800MV_0DB_SNB_A		(0x0 << 22)
/* SNB B-stepping */
#define  FDI_LINK_TRAIN_400MV_0DB_SNB_B		(0x0 << 22)
#define  FDI_LINK_TRAIN_400MV_6DB_SNB_B		(0x3a << 22)
#define  FDI_LINK_TRAIN_600MV_3_5DB_SNB_B	(0x39 << 22)
#define  FDI_LINK_TRAIN_800MV_0DB_SNB_B		(0x38 << 22)
#define  FDI_LINK_TRAIN_VOL_EMP_MASK		(0x3f << 22)
#define  FDI_DP_PORT_WIDTH_SHIFT		19
#define  FDI_DP_PORT_WIDTH_MASK			(7 << FDI_DP_PORT_WIDTH_SHIFT)
#define  FDI_DP_PORT_WIDTH(width)           (((width) - 1) << FDI_DP_PORT_WIDTH_SHIFT)
#define  FDI_TX_ENHANCE_FRAME_ENABLE    (1 << 18)
/* Ironlake: hardwired to 1 */
#define  FDI_TX_PLL_ENABLE              (1 << 14)

/* Ivybridge has different bits for lolz */
#define  FDI_LINK_TRAIN_PATTERN_1_IVB       (0 << 8)
#define  FDI_LINK_TRAIN_PATTERN_2_IVB       (1 << 8)
#define  FDI_LINK_TRAIN_PATTERN_IDLE_IVB    (2 << 8)
#define  FDI_LINK_TRAIN_NONE_IVB            (3 << 8)

/* both Tx and Rx */
#define  FDI_COMPOSITE_SYNC		(1 << 11)
#define  FDI_LINK_TRAIN_AUTO		(1 << 10)
#define  FDI_SCRAMBLING_ENABLE          (0 << 7)
#define  FDI_SCRAMBLING_DISABLE         (1 << 7)

/* FDI_RX, FDI_X is hard-wired to Transcoder_X */
#define _FDI_RXA_CTL             0xf000c
#define _FDI_RXB_CTL             0xf100c
#define FDI_RX_CTL(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_CTL, _FDI_RXB_CTL)
#define  FDI_RX_ENABLE          (1 << 31)
/* train, dp width same as FDI_TX */
#define  FDI_FS_ERRC_ENABLE		(1 << 27)
#define  FDI_FE_ERRC_ENABLE		(1 << 26)
#define  FDI_RX_POLARITY_REVERSED_LPT	(1 << 16)
#define  FDI_8BPC                       (0 << 16)
#define  FDI_10BPC                      (1 << 16)
#define  FDI_6BPC                       (2 << 16)
#define  FDI_12BPC                      (3 << 16)
#define  FDI_RX_LINK_REVERSAL_OVERRIDE  (1 << 15)
#define  FDI_DMI_LINK_REVERSE_MASK      (1 << 14)
#define  FDI_RX_PLL_ENABLE              (1 << 13)
#define  FDI_FS_ERR_CORRECT_ENABLE      (1 << 11)
#define  FDI_FE_ERR_CORRECT_ENABLE      (1 << 10)
#define  FDI_FS_ERR_REPORT_ENABLE       (1 << 9)
#define  FDI_FE_ERR_REPORT_ENABLE       (1 << 8)
#define  FDI_RX_ENHANCE_FRAME_ENABLE    (1 << 6)
#define  FDI_PCDCLK	                (1 << 4)
/* CPT */
#define  FDI_AUTO_TRAINING			(1 << 10)
#define  FDI_LINK_TRAIN_PATTERN_1_CPT		(0 << 8)
#define  FDI_LINK_TRAIN_PATTERN_2_CPT		(1 << 8)
#define  FDI_LINK_TRAIN_PATTERN_IDLE_CPT	(2 << 8)
#define  FDI_LINK_TRAIN_NORMAL_CPT		(3 << 8)
#define  FDI_LINK_TRAIN_PATTERN_MASK_CPT	(3 << 8)

#define _FDI_RXA_MISC			0xf0010
#define _FDI_RXB_MISC			0xf1010
#define  FDI_RX_PWRDN_LANE1_MASK	(3 << 26)
#define  FDI_RX_PWRDN_LANE1_VAL(x)	((x) << 26)
#define  FDI_RX_PWRDN_LANE0_MASK	(3 << 24)
#define  FDI_RX_PWRDN_LANE0_VAL(x)	((x) << 24)
#define  FDI_RX_TP1_TO_TP2_48		(2 << 20)
#define  FDI_RX_TP1_TO_TP2_64		(3 << 20)
#define  FDI_RX_FDI_DELAY_90		(0x90 << 0)
#define FDI_RX_MISC(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_MISC, _FDI_RXB_MISC)

#define _FDI_RXA_TUSIZE1        0xf0030
#define _FDI_RXA_TUSIZE2        0xf0038
#define _FDI_RXB_TUSIZE1        0xf1030
#define _FDI_RXB_TUSIZE2        0xf1038
#define FDI_RX_TUSIZE1(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_TUSIZE1, _FDI_RXB_TUSIZE1)
#define FDI_RX_TUSIZE2(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_TUSIZE2, _FDI_RXB_TUSIZE2)

/* FDI_RX interrupt register format */
#define FDI_RX_INTER_LANE_ALIGN         (1 << 10)
#define FDI_RX_SYMBOL_LOCK              (1 << 9) /* train 2 */
#define FDI_RX_BIT_LOCK                 (1 << 8) /* train 1 */
#define FDI_RX_TRAIN_PATTERN_2_FAIL     (1 << 7)
#define FDI_RX_FS_CODE_ERR              (1 << 6)
#define FDI_RX_FE_CODE_ERR              (1 << 5)
#define FDI_RX_SYMBOL_ERR_RATE_ABOVE    (1 << 4)
#define FDI_RX_HDCP_LINK_FAIL           (1 << 3)
#define FDI_RX_PIXEL_FIFO_OVERFLOW      (1 << 2)
#define FDI_RX_CROSS_CLOCK_OVERFLOW     (1 << 1)
#define FDI_RX_SYMBOL_QUEUE_OVERFLOW    (1 << 0)

#define _FDI_RXA_IIR            0xf0014
#define _FDI_RXA_IMR            0xf0018
#define _FDI_RXB_IIR            0xf1014
#define _FDI_RXB_IMR            0xf1018
#define FDI_RX_IIR(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_IIR, _FDI_RXB_IIR)
#define FDI_RX_IMR(pipe)	_MMIO_PIPE(pipe, _FDI_RXA_IMR, _FDI_RXB_IMR)

#define FDI_PLL_CTL_1           _MMIO(0xfe000)
#define FDI_PLL_CTL_2           _MMIO(0xfe004)

#define PCH_LVDS	_MMIO(0xe1180)
#define  LVDS_DETECTED	(1 << 1)

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
#define     GEN9_MEM_LATENCY_LEVEL_MASK		0xFF
#define     GEN9_MEM_LATENCY_LEVEL_1_5_SHIFT	8
#define     GEN9_MEM_LATENCY_LEVEL_2_6_SHIFT	16
#define     GEN9_MEM_LATENCY_LEVEL_3_7_SHIFT	24
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
#define   ICL_PCODE_SAGV_DE_MEM_SS_CONFIG	0xe
#define     ICL_PCODE_POINTS_RESTRICTED		0x0
#define     ICL_PCODE_POINTS_RESTRICTED_MASK	0xf
#define   ADLS_PSF_PT_SHIFT			8
#define   ADLS_QGV_PT_MASK			REG_GENMASK(7, 0)
#define   ADLS_PSF_PT_MASK			REG_GENMASK(10, 8)
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
#define GEN12_PCODE_READ_SAGV_BLOCK_TIME_US	0x23
#define GEN6_PCODE_DATA				_MMIO(0x138128)
#define   GEN6_PCODE_FREQ_IA_RATIO_SHIFT	8
#define   GEN6_PCODE_FREQ_RING_RATIO_SHIFT	16
#define GEN6_PCODE_DATA1			_MMIO(0x13812C)

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

/* Audio */
#define G4X_AUD_VID_DID			_MMIO(DISPLAY_MMIO_BASE(dev_priv) + 0x62020)
#define   INTEL_AUDIO_DEVCL		0x808629FB
#define   INTEL_AUDIO_DEVBLC		0x80862801
#define   INTEL_AUDIO_DEVCTG		0x80862802

#define G4X_AUD_CNTL_ST			_MMIO(0x620B4)
#define   G4X_ELDV_DEVCL_DEVBLC		(1 << 13)
#define   G4X_ELDV_DEVCTG		(1 << 14)
#define   G4X_ELD_ADDR_MASK		(0xf << 5)
#define   G4X_ELD_ACK			(1 << 4)
#define G4X_HDMIW_HDMIEDID		_MMIO(0x6210C)

#define _IBX_HDMIW_HDMIEDID_A		0xE2050
#define _IBX_HDMIW_HDMIEDID_B		0xE2150
#define IBX_HDMIW_HDMIEDID(pipe)	_MMIO_PIPE(pipe, _IBX_HDMIW_HDMIEDID_A, \
						  _IBX_HDMIW_HDMIEDID_B)
#define _IBX_AUD_CNTL_ST_A		0xE20B4
#define _IBX_AUD_CNTL_ST_B		0xE21B4
#define IBX_AUD_CNTL_ST(pipe)		_MMIO_PIPE(pipe, _IBX_AUD_CNTL_ST_A, \
						  _IBX_AUD_CNTL_ST_B)
#define   IBX_ELD_BUFFER_SIZE_MASK	(0x1f << 10)
#define   IBX_ELD_ADDRESS_MASK		(0x1f << 5)
#define   IBX_ELD_ACK			(1 << 4)
#define IBX_AUD_CNTL_ST2		_MMIO(0xE20C0)
#define   IBX_CP_READY(port)		((1 << 1) << (((port) - 1) * 4))
#define   IBX_ELD_VALID(port)		((1 << 0) << (((port) - 1) * 4))

#define _CPT_HDMIW_HDMIEDID_A		0xE5050
#define _CPT_HDMIW_HDMIEDID_B		0xE5150
#define CPT_HDMIW_HDMIEDID(pipe)	_MMIO_PIPE(pipe, _CPT_HDMIW_HDMIEDID_A, _CPT_HDMIW_HDMIEDID_B)
#define _CPT_AUD_CNTL_ST_A		0xE50B4
#define _CPT_AUD_CNTL_ST_B		0xE51B4
#define CPT_AUD_CNTL_ST(pipe)		_MMIO_PIPE(pipe, _CPT_AUD_CNTL_ST_A, _CPT_AUD_CNTL_ST_B)
#define CPT_AUD_CNTRL_ST2		_MMIO(0xE50C0)

#define _VLV_HDMIW_HDMIEDID_A		(VLV_DISPLAY_BASE + 0x62050)
#define _VLV_HDMIW_HDMIEDID_B		(VLV_DISPLAY_BASE + 0x62150)
#define VLV_HDMIW_HDMIEDID(pipe)	_MMIO_PIPE(pipe, _VLV_HDMIW_HDMIEDID_A, _VLV_HDMIW_HDMIEDID_B)
#define _VLV_AUD_CNTL_ST_A		(VLV_DISPLAY_BASE + 0x620B4)
#define _VLV_AUD_CNTL_ST_B		(VLV_DISPLAY_BASE + 0x621B4)
#define VLV_AUD_CNTL_ST(pipe)		_MMIO_PIPE(pipe, _VLV_AUD_CNTL_ST_A, _VLV_AUD_CNTL_ST_B)
#define VLV_AUD_CNTL_ST2		_MMIO(VLV_DISPLAY_BASE + 0x620C0)

/* These are the 4 32-bit write offset registers for each stream
 * output buffer.  It determines the offset from the
 * 3DSTATE_SO_BUFFERs that the next streamed vertex output goes to.
 */
#define GEN7_SO_WRITE_OFFSET(n)		_MMIO(0x5280 + (n) * 4)

#define _IBX_AUD_CONFIG_A		0xe2000
#define _IBX_AUD_CONFIG_B		0xe2100
#define IBX_AUD_CFG(pipe)		_MMIO_PIPE(pipe, _IBX_AUD_CONFIG_A, _IBX_AUD_CONFIG_B)
#define _CPT_AUD_CONFIG_A		0xe5000
#define _CPT_AUD_CONFIG_B		0xe5100
#define CPT_AUD_CFG(pipe)		_MMIO_PIPE(pipe, _CPT_AUD_CONFIG_A, _CPT_AUD_CONFIG_B)
#define _VLV_AUD_CONFIG_A		(VLV_DISPLAY_BASE + 0x62000)
#define _VLV_AUD_CONFIG_B		(VLV_DISPLAY_BASE + 0x62100)
#define VLV_AUD_CFG(pipe)		_MMIO_PIPE(pipe, _VLV_AUD_CONFIG_A, _VLV_AUD_CONFIG_B)

#define   AUD_CONFIG_N_VALUE_INDEX		(1 << 29)
#define   AUD_CONFIG_N_PROG_ENABLE		(1 << 28)
#define   AUD_CONFIG_UPPER_N_SHIFT		20
#define   AUD_CONFIG_UPPER_N_MASK		(0xff << 20)
#define   AUD_CONFIG_LOWER_N_SHIFT		4
#define   AUD_CONFIG_LOWER_N_MASK		(0xfff << 4)
#define   AUD_CONFIG_N_MASK			(AUD_CONFIG_UPPER_N_MASK | AUD_CONFIG_LOWER_N_MASK)
#define   AUD_CONFIG_N(n) \
	(((((n) >> 12) & 0xff) << AUD_CONFIG_UPPER_N_SHIFT) |	\
	 (((n) & 0xfff) << AUD_CONFIG_LOWER_N_SHIFT))
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
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_296703	(10 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_297000	(11 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_593407	(12 << 16)
#define   AUD_CONFIG_PIXEL_CLOCK_HDMI_594000	(13 << 16)
#define   AUD_CONFIG_DISABLE_NCTS		(1 << 3)

/* HSW Audio */
#define _HSW_AUD_CONFIG_A		0x65000
#define _HSW_AUD_CONFIG_B		0x65100
#define HSW_AUD_CFG(trans)		_MMIO_TRANS(trans, _HSW_AUD_CONFIG_A, _HSW_AUD_CONFIG_B)

#define _HSW_AUD_MISC_CTRL_A		0x65010
#define _HSW_AUD_MISC_CTRL_B		0x65110
#define HSW_AUD_MISC_CTRL(trans)	_MMIO_TRANS(trans, _HSW_AUD_MISC_CTRL_A, _HSW_AUD_MISC_CTRL_B)

#define _HSW_AUD_M_CTS_ENABLE_A		0x65028
#define _HSW_AUD_M_CTS_ENABLE_B		0x65128
#define HSW_AUD_M_CTS_ENABLE(trans)	_MMIO_TRANS(trans, _HSW_AUD_M_CTS_ENABLE_A, _HSW_AUD_M_CTS_ENABLE_B)
#define   AUD_M_CTS_M_VALUE_INDEX	(1 << 21)
#define   AUD_M_CTS_M_PROG_ENABLE	(1 << 20)
#define   AUD_CONFIG_M_MASK		0xfffff

#define _HSW_AUD_DIP_ELD_CTRL_ST_A	0x650b4
#define _HSW_AUD_DIP_ELD_CTRL_ST_B	0x651b4
#define HSW_AUD_DIP_ELD_CTRL(trans)	_MMIO_TRANS(trans, _HSW_AUD_DIP_ELD_CTRL_ST_A, _HSW_AUD_DIP_ELD_CTRL_ST_B)

/* Audio Digital Converter */
#define _HSW_AUD_DIG_CNVT_1		0x65080
#define _HSW_AUD_DIG_CNVT_2		0x65180
#define AUD_DIG_CNVT(trans)		_MMIO_TRANS(trans, _HSW_AUD_DIG_CNVT_1, _HSW_AUD_DIG_CNVT_2)
#define DIP_PORT_SEL_MASK		0x3

#define _HSW_AUD_EDID_DATA_A		0x65050
#define _HSW_AUD_EDID_DATA_B		0x65150
#define HSW_AUD_EDID_DATA(trans)	_MMIO_TRANS(trans, _HSW_AUD_EDID_DATA_A, _HSW_AUD_EDID_DATA_B)

#define HSW_AUD_PIPE_CONV_CFG		_MMIO(0x6507c)
#define HSW_AUD_PIN_ELD_CP_VLD		_MMIO(0x650c0)
#define   AUDIO_INACTIVE(trans)		((1 << 3) << ((trans) * 4))
#define   AUDIO_OUTPUT_ENABLE(trans)	((1 << 2) << ((trans) * 4))
#define   AUDIO_CP_READY(trans)		((1 << 1) << ((trans) * 4))
#define   AUDIO_ELD_VALID(trans)	((1 << 0) << ((trans) * 4))

#define _AUD_TCA_DP_2DOT0_CTRL		0x650bc
#define _AUD_TCB_DP_2DOT0_CTRL		0x651bc
#define AUD_DP_2DOT0_CTRL(trans)	_MMIO_TRANS(trans, _AUD_TCA_DP_2DOT0_CTRL, _AUD_TCB_DP_2DOT0_CTRL)
#define  AUD_ENABLE_SDP_SPLIT		REG_BIT(31)

#define HSW_AUD_CHICKENBIT			_MMIO(0x65f10)
#define   SKL_AUD_CODEC_WAKE_SIGNAL		(1 << 15)

#define AUD_FREQ_CNTRL			_MMIO(0x65900)
#define AUD_PIN_BUF_CTL		_MMIO(0x48414)
#define   AUD_PIN_BUF_ENABLE		REG_BIT(31)

#define AUD_TS_CDCLK_M			_MMIO(0x65ea0)
#define   AUD_TS_CDCLK_M_EN		REG_BIT(31)
#define AUD_TS_CDCLK_N			_MMIO(0x65ea4)

/* Display Audio Config Reg */
#define AUD_CONFIG_BE			_MMIO(0x65ef0)
#define HBLANK_EARLY_ENABLE_ICL(pipe)		(0x1 << (20 - (pipe)))
#define HBLANK_EARLY_ENABLE_TGL(pipe)		(0x1 << (24 + (pipe)))
#define HBLANK_START_COUNT_MASK(pipe)		(0x7 << (3 + ((pipe) * 6)))
#define HBLANK_START_COUNT(pipe, val)		(((val) & 0x7) << (3 + ((pipe)) * 6))
#define NUMBER_SAMPLES_PER_LINE_MASK(pipe)	(0x3 << ((pipe) * 6))
#define NUMBER_SAMPLES_PER_LINE(pipe, val)	(((val) & 0x3) << ((pipe) * 6))

#define HBLANK_START_COUNT_8	0
#define HBLANK_START_COUNT_16	1
#define HBLANK_START_COUNT_32	2
#define HBLANK_START_COUNT_64	3
#define HBLANK_START_COUNT_96	4
#define HBLANK_START_COUNT_128	5

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

#define _ICL_AUX_REG_IDX(pw_idx)	((pw_idx) - ICL_PW_CTL_IDX_AUX_A)
#define _ICL_AUX_ANAOVRD1_A		0x162398
#define _ICL_AUX_ANAOVRD1_B		0x6C398
#define ICL_AUX_ANAOVRD1(pw_idx)	_MMIO(_PICK(_ICL_AUX_REG_IDX(pw_idx), \
						    _ICL_AUX_ANAOVRD1_A, \
						    _ICL_AUX_ANAOVRD1_B))
#define   ICL_AUX_ANAOVRD1_LDO_BYPASS	(1 << 7)
#define   ICL_AUX_ANAOVRD1_ENABLE	(1 << 0)

/* HDCP Key Registers */
#define HDCP_KEY_CONF			_MMIO(0x66c00)
#define  HDCP_AKSV_SEND_TRIGGER		BIT(31)
#define  HDCP_CLEAR_KEYS_TRIGGER	BIT(30)
#define  HDCP_KEY_LOAD_TRIGGER		BIT(8)
#define HDCP_KEY_STATUS			_MMIO(0x66c04)
#define  HDCP_FUSE_IN_PROGRESS		BIT(7)
#define  HDCP_FUSE_ERROR		BIT(6)
#define  HDCP_FUSE_DONE			BIT(5)
#define  HDCP_KEY_LOAD_STATUS		BIT(1)
#define  HDCP_KEY_LOAD_DONE		BIT(0)
#define HDCP_AKSV_LO			_MMIO(0x66c10)
#define HDCP_AKSV_HI			_MMIO(0x66c14)

/* HDCP Repeater Registers */
#define HDCP_REP_CTL			_MMIO(0x66d00)
#define  HDCP_TRANSA_REP_PRESENT	BIT(31)
#define  HDCP_TRANSB_REP_PRESENT	BIT(30)
#define  HDCP_TRANSC_REP_PRESENT	BIT(29)
#define  HDCP_TRANSD_REP_PRESENT	BIT(28)
#define  HDCP_DDIB_REP_PRESENT		BIT(30)
#define  HDCP_DDIA_REP_PRESENT		BIT(29)
#define  HDCP_DDIC_REP_PRESENT		BIT(28)
#define  HDCP_DDID_REP_PRESENT		BIT(27)
#define  HDCP_DDIF_REP_PRESENT		BIT(26)
#define  HDCP_DDIE_REP_PRESENT		BIT(25)
#define  HDCP_TRANSA_SHA1_M0		(1 << 20)
#define  HDCP_TRANSB_SHA1_M0		(2 << 20)
#define  HDCP_TRANSC_SHA1_M0		(3 << 20)
#define  HDCP_TRANSD_SHA1_M0		(4 << 20)
#define  HDCP_DDIB_SHA1_M0		(1 << 20)
#define  HDCP_DDIA_SHA1_M0		(2 << 20)
#define  HDCP_DDIC_SHA1_M0		(3 << 20)
#define  HDCP_DDID_SHA1_M0		(4 << 20)
#define  HDCP_DDIF_SHA1_M0		(5 << 20)
#define  HDCP_DDIE_SHA1_M0		(6 << 20) /* Bspec says 5? */
#define  HDCP_SHA1_BUSY			BIT(16)
#define  HDCP_SHA1_READY		BIT(17)
#define  HDCP_SHA1_COMPLETE		BIT(18)
#define  HDCP_SHA1_V_MATCH		BIT(19)
#define  HDCP_SHA1_TEXT_32		(1 << 1)
#define  HDCP_SHA1_COMPLETE_HASH	(2 << 1)
#define  HDCP_SHA1_TEXT_24		(4 << 1)
#define  HDCP_SHA1_TEXT_16		(5 << 1)
#define  HDCP_SHA1_TEXT_8		(6 << 1)
#define  HDCP_SHA1_TEXT_0		(7 << 1)
#define HDCP_SHA_V_PRIME_H0		_MMIO(0x66d04)
#define HDCP_SHA_V_PRIME_H1		_MMIO(0x66d08)
#define HDCP_SHA_V_PRIME_H2		_MMIO(0x66d0C)
#define HDCP_SHA_V_PRIME_H3		_MMIO(0x66d10)
#define HDCP_SHA_V_PRIME_H4		_MMIO(0x66d14)
#define HDCP_SHA_V_PRIME(h)		_MMIO((0x66d04 + (h) * 4))
#define HDCP_SHA_TEXT			_MMIO(0x66d18)

/* HDCP Auth Registers */
#define _PORTA_HDCP_AUTHENC		0x66800
#define _PORTB_HDCP_AUTHENC		0x66500
#define _PORTC_HDCP_AUTHENC		0x66600
#define _PORTD_HDCP_AUTHENC		0x66700
#define _PORTE_HDCP_AUTHENC		0x66A00
#define _PORTF_HDCP_AUTHENC		0x66900
#define _PORT_HDCP_AUTHENC(port, x)	_MMIO(_PICK(port, \
					  _PORTA_HDCP_AUTHENC, \
					  _PORTB_HDCP_AUTHENC, \
					  _PORTC_HDCP_AUTHENC, \
					  _PORTD_HDCP_AUTHENC, \
					  _PORTE_HDCP_AUTHENC, \
					  _PORTF_HDCP_AUTHENC) + (x))
#define PORT_HDCP_CONF(port)		_PORT_HDCP_AUTHENC(port, 0x0)
#define _TRANSA_HDCP_CONF		0x66400
#define _TRANSB_HDCP_CONF		0x66500
#define TRANS_HDCP_CONF(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP_CONF, \
						    _TRANSB_HDCP_CONF)
#define HDCP_CONF(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_CONF(trans) : \
					 PORT_HDCP_CONF(port))

#define  HDCP_CONF_CAPTURE_AN		BIT(0)
#define  HDCP_CONF_AUTH_AND_ENC		(BIT(1) | BIT(0))
#define PORT_HDCP_ANINIT(port)		_PORT_HDCP_AUTHENC(port, 0x4)
#define _TRANSA_HDCP_ANINIT		0x66404
#define _TRANSB_HDCP_ANINIT		0x66504
#define TRANS_HDCP_ANINIT(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_ANINIT, \
						    _TRANSB_HDCP_ANINIT)
#define HDCP_ANINIT(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_ANINIT(trans) : \
					 PORT_HDCP_ANINIT(port))

#define PORT_HDCP_ANLO(port)		_PORT_HDCP_AUTHENC(port, 0x8)
#define _TRANSA_HDCP_ANLO		0x66408
#define _TRANSB_HDCP_ANLO		0x66508
#define TRANS_HDCP_ANLO(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP_ANLO, \
						    _TRANSB_HDCP_ANLO)
#define HDCP_ANLO(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_ANLO(trans) : \
					 PORT_HDCP_ANLO(port))

#define PORT_HDCP_ANHI(port)		_PORT_HDCP_AUTHENC(port, 0xC)
#define _TRANSA_HDCP_ANHI		0x6640C
#define _TRANSB_HDCP_ANHI		0x6650C
#define TRANS_HDCP_ANHI(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP_ANHI, \
						    _TRANSB_HDCP_ANHI)
#define HDCP_ANHI(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_ANHI(trans) : \
					 PORT_HDCP_ANHI(port))

#define PORT_HDCP_BKSVLO(port)		_PORT_HDCP_AUTHENC(port, 0x10)
#define _TRANSA_HDCP_BKSVLO		0x66410
#define _TRANSB_HDCP_BKSVLO		0x66510
#define TRANS_HDCP_BKSVLO(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_BKSVLO, \
						    _TRANSB_HDCP_BKSVLO)
#define HDCP_BKSVLO(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_BKSVLO(trans) : \
					 PORT_HDCP_BKSVLO(port))

#define PORT_HDCP_BKSVHI(port)		_PORT_HDCP_AUTHENC(port, 0x14)
#define _TRANSA_HDCP_BKSVHI		0x66414
#define _TRANSB_HDCP_BKSVHI		0x66514
#define TRANS_HDCP_BKSVHI(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_BKSVHI, \
						    _TRANSB_HDCP_BKSVHI)
#define HDCP_BKSVHI(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_BKSVHI(trans) : \
					 PORT_HDCP_BKSVHI(port))

#define PORT_HDCP_RPRIME(port)		_PORT_HDCP_AUTHENC(port, 0x18)
#define _TRANSA_HDCP_RPRIME		0x66418
#define _TRANSB_HDCP_RPRIME		0x66518
#define TRANS_HDCP_RPRIME(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_RPRIME, \
						    _TRANSB_HDCP_RPRIME)
#define HDCP_RPRIME(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_RPRIME(trans) : \
					 PORT_HDCP_RPRIME(port))

#define PORT_HDCP_STATUS(port)		_PORT_HDCP_AUTHENC(port, 0x1C)
#define _TRANSA_HDCP_STATUS		0x6641C
#define _TRANSB_HDCP_STATUS		0x6651C
#define TRANS_HDCP_STATUS(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP_STATUS, \
						    _TRANSB_HDCP_STATUS)
#define HDCP_STATUS(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP_STATUS(trans) : \
					 PORT_HDCP_STATUS(port))

#define  HDCP_STATUS_STREAM_A_ENC	BIT(31)
#define  HDCP_STATUS_STREAM_B_ENC	BIT(30)
#define  HDCP_STATUS_STREAM_C_ENC	BIT(29)
#define  HDCP_STATUS_STREAM_D_ENC	BIT(28)
#define  HDCP_STATUS_AUTH		BIT(21)
#define  HDCP_STATUS_ENC		BIT(20)
#define  HDCP_STATUS_RI_MATCH		BIT(19)
#define  HDCP_STATUS_R0_READY		BIT(18)
#define  HDCP_STATUS_AN_READY		BIT(17)
#define  HDCP_STATUS_CIPHER		BIT(16)
#define  HDCP_STATUS_FRAME_CNT(x)	(((x) >> 8) & 0xff)

/* HDCP2.2 Registers */
#define _PORTA_HDCP2_BASE		0x66800
#define _PORTB_HDCP2_BASE		0x66500
#define _PORTC_HDCP2_BASE		0x66600
#define _PORTD_HDCP2_BASE		0x66700
#define _PORTE_HDCP2_BASE		0x66A00
#define _PORTF_HDCP2_BASE		0x66900
#define _PORT_HDCP2_BASE(port, x)	_MMIO(_PICK((port), \
					  _PORTA_HDCP2_BASE, \
					  _PORTB_HDCP2_BASE, \
					  _PORTC_HDCP2_BASE, \
					  _PORTD_HDCP2_BASE, \
					  _PORTE_HDCP2_BASE, \
					  _PORTF_HDCP2_BASE) + (x))

#define PORT_HDCP2_AUTH(port)		_PORT_HDCP2_BASE(port, 0x98)
#define _TRANSA_HDCP2_AUTH		0x66498
#define _TRANSB_HDCP2_AUTH		0x66598
#define TRANS_HDCP2_AUTH(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP2_AUTH, \
						    _TRANSB_HDCP2_AUTH)
#define   AUTH_LINK_AUTHENTICATED	BIT(31)
#define   AUTH_LINK_TYPE		BIT(30)
#define   AUTH_FORCE_CLR_INPUTCTR	BIT(19)
#define   AUTH_CLR_KEYS			BIT(18)
#define HDCP2_AUTH(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_AUTH(trans) : \
					 PORT_HDCP2_AUTH(port))

#define PORT_HDCP2_CTL(port)		_PORT_HDCP2_BASE(port, 0xB0)
#define _TRANSA_HDCP2_CTL		0x664B0
#define _TRANSB_HDCP2_CTL		0x665B0
#define TRANS_HDCP2_CTL(trans)		_MMIO_TRANS(trans, _TRANSA_HDCP2_CTL, \
						    _TRANSB_HDCP2_CTL)
#define   CTL_LINK_ENCRYPTION_REQ	BIT(31)
#define HDCP2_CTL(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_CTL(trans) : \
					 PORT_HDCP2_CTL(port))

#define PORT_HDCP2_STATUS(port)		_PORT_HDCP2_BASE(port, 0xB4)
#define _TRANSA_HDCP2_STATUS		0x664B4
#define _TRANSB_HDCP2_STATUS		0x665B4
#define TRANS_HDCP2_STATUS(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP2_STATUS, \
						    _TRANSB_HDCP2_STATUS)
#define   LINK_TYPE_STATUS		BIT(22)
#define   LINK_AUTH_STATUS		BIT(21)
#define   LINK_ENCRYPTION_STATUS	BIT(20)
#define HDCP2_STATUS(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_STATUS(trans) : \
					 PORT_HDCP2_STATUS(port))

#define _PIPEA_HDCP2_STREAM_STATUS	0x668C0
#define _PIPEB_HDCP2_STREAM_STATUS	0x665C0
#define _PIPEC_HDCP2_STREAM_STATUS	0x666C0
#define _PIPED_HDCP2_STREAM_STATUS	0x667C0
#define PIPE_HDCP2_STREAM_STATUS(pipe)		_MMIO(_PICK((pipe), \
						      _PIPEA_HDCP2_STREAM_STATUS, \
						      _PIPEB_HDCP2_STREAM_STATUS, \
						      _PIPEC_HDCP2_STREAM_STATUS, \
						      _PIPED_HDCP2_STREAM_STATUS))

#define _TRANSA_HDCP2_STREAM_STATUS		0x664C0
#define _TRANSB_HDCP2_STREAM_STATUS		0x665C0
#define TRANS_HDCP2_STREAM_STATUS(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP2_STREAM_STATUS, \
						    _TRANSB_HDCP2_STREAM_STATUS)
#define   STREAM_ENCRYPTION_STATUS	BIT(31)
#define   STREAM_TYPE_STATUS		BIT(30)
#define HDCP2_STREAM_STATUS(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_STREAM_STATUS(trans) : \
					 PIPE_HDCP2_STREAM_STATUS(pipe))

#define _PORTA_HDCP2_AUTH_STREAM		0x66F00
#define _PORTB_HDCP2_AUTH_STREAM		0x66F04
#define PORT_HDCP2_AUTH_STREAM(port)	_MMIO_PORT(port, \
						   _PORTA_HDCP2_AUTH_STREAM, \
						   _PORTB_HDCP2_AUTH_STREAM)
#define _TRANSA_HDCP2_AUTH_STREAM		0x66F00
#define _TRANSB_HDCP2_AUTH_STREAM		0x66F04
#define TRANS_HDCP2_AUTH_STREAM(trans)	_MMIO_TRANS(trans, \
						    _TRANSA_HDCP2_AUTH_STREAM, \
						    _TRANSB_HDCP2_AUTH_STREAM)
#define   AUTH_STREAM_TYPE		BIT(31)
#define HDCP2_AUTH_STREAM(dev_priv, trans, port) \
					(GRAPHICS_VER(dev_priv) >= 12 ? \
					 TRANS_HDCP2_AUTH_STREAM(trans) : \
					 PORT_HDCP2_AUTH_STREAM(port))

/* Per-pipe DDI Function Control */
#define _TRANS_DDI_FUNC_CTL_A		0x60400
#define _TRANS_DDI_FUNC_CTL_B		0x61400
#define _TRANS_DDI_FUNC_CTL_C		0x62400
#define _TRANS_DDI_FUNC_CTL_D		0x63400
#define _TRANS_DDI_FUNC_CTL_EDP		0x6F400
#define _TRANS_DDI_FUNC_CTL_DSI0	0x6b400
#define _TRANS_DDI_FUNC_CTL_DSI1	0x6bc00
#define TRANS_DDI_FUNC_CTL(tran) _MMIO_TRANS2(tran, _TRANS_DDI_FUNC_CTL_A)

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
#define TRANS_DDI_FUNC_CTL2(tran)	_MMIO_TRANS2(tran, _TRANS_DDI_FUNC_CTL2_A)
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
#define TGL_DP_TP_CTL(tran) _MMIO_TRANS2((tran), _TGL_DP_TP_CTL_A)
#define  DP_TP_CTL_ENABLE			(1 << 31)
#define  DP_TP_CTL_FEC_ENABLE			(1 << 30)
#define  DP_TP_CTL_MODE_SST			(0 << 27)
#define  DP_TP_CTL_MODE_MST			(1 << 27)
#define  DP_TP_CTL_FORCE_ACT			(1 << 25)
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
#define TGL_DP_TP_STATUS(tran) _MMIO_TRANS2((tran), _TGL_DP_TP_STATUS_A)
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
#define DDI_BUF_CTL(port) _MMIO_PORT(port, _DDI_BUF_CTL_A, _DDI_BUF_CTL_B)
#define  DDI_BUF_CTL_ENABLE			(1 << 31)
#define  DDI_BUF_TRANS_SELECT(n)	((n) << 24)
#define  DDI_BUF_EMP_MASK			(0xf << 24)
#define  DDI_BUF_PHY_LINK_RATE(r)		((r) << 20)
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
#define  PORT_CLK_SEL_LCPLL_2700	(0 << 29)
#define  PORT_CLK_SEL_LCPLL_1350	(1 << 29)
#define  PORT_CLK_SEL_LCPLL_810		(2 << 29)
#define  PORT_CLK_SEL_SPLL		(3 << 29)
#define  PORT_CLK_SEL_WRPLL(pll)	(((pll) + 4) << 29)
#define  PORT_CLK_SEL_WRPLL1		(4 << 29)
#define  PORT_CLK_SEL_WRPLL2		(5 << 29)
#define  PORT_CLK_SEL_NONE		(7 << 29)
#define  PORT_CLK_SEL_MASK		(7 << 29)

/* On ICL+ this is the same as PORT_CLK_SEL, but all bits change. */
#define DDI_CLK_SEL(port)		PORT_CLK_SEL(port)
#define  DDI_CLK_SEL_NONE		(0x0 << 28)
#define  DDI_CLK_SEL_MG			(0x8 << 28)
#define  DDI_CLK_SEL_TBT_162		(0xC << 28)
#define  DDI_CLK_SEL_TBT_270		(0xD << 28)
#define  DDI_CLK_SEL_TBT_540		(0xE << 28)
#define  DDI_CLK_SEL_TBT_810		(0xF << 28)
#define  DDI_CLK_SEL_MASK		(0xF << 28)

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
#define TRANS_MSA_MISC(tran) _MMIO_TRANS2(tran, _TRANSA_MSA_MISC)
/* See DP_MSA_MISC_* for the bit definitions */

#define _TRANS_A_SET_CONTEXT_LATENCY		0x6007C
#define _TRANS_B_SET_CONTEXT_LATENCY		0x6107C
#define _TRANS_C_SET_CONTEXT_LATENCY		0x6207C
#define _TRANS_D_SET_CONTEXT_LATENCY		0x6307C
#define TRANS_SET_CONTEXT_LATENCY(tran)		_MMIO_TRANS2(tran, _TRANS_A_SET_CONTEXT_LATENCY)
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
#define  CDCLK_FREQ_SEL_MASK		(3 << 26)
#define  CDCLK_FREQ_450_432		(0 << 26)
#define  CDCLK_FREQ_540			(1 << 26)
#define  CDCLK_FREQ_337_308		(2 << 26)
#define  CDCLK_FREQ_675_617		(3 << 26)
#define  BXT_CDCLK_CD2X_DIV_SEL_MASK	(3 << 22)
#define  BXT_CDCLK_CD2X_DIV_SEL_1	(0 << 22)
#define  BXT_CDCLK_CD2X_DIV_SEL_1_5	(1 << 22)
#define  BXT_CDCLK_CD2X_DIV_SEL_2	(2 << 22)
#define  BXT_CDCLK_CD2X_DIV_SEL_4	(3 << 22)
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
#define DPLL0_ENABLE		0x46010
#define DPLL1_ENABLE		0x46014
#define _ADLS_DPLL2_ENABLE	0x46018
#define _ADLS_DPLL3_ENABLE	0x46030
#define  PLL_ENABLE		(1 << 31)
#define  PLL_LOCK		(1 << 30)
#define  PLL_POWER_ENABLE	(1 << 27)
#define  PLL_POWER_STATE	(1 << 26)
#define ICL_DPLL_ENABLE(pll)	_MMIO_PLL3(pll, DPLL0_ENABLE, DPLL1_ENABLE, \
					   _ADLS_DPLL2_ENABLE, _ADLS_DPLL3_ENABLE)

#define _DG2_PLL3_ENABLE	0x4601C

#define DG2_PLL_ENABLE(pll) _MMIO_PLL3(pll, DPLL0_ENABLE, DPLL1_ENABLE, \
				       _ADLS_DPLL2_ENABLE, _DG2_PLL3_ENABLE)

#define TBT_PLL_ENABLE		_MMIO(0x46020)

#define _MG_PLL1_ENABLE		0x46030
#define _MG_PLL2_ENABLE		0x46034
#define _MG_PLL3_ENABLE		0x46038
#define _MG_PLL4_ENABLE		0x4603C
/* Bits are the same as DPLL0_ENABLE */
#define MG_PLL_ENABLE(tc_port)	_MMIO_PORT((tc_port), _MG_PLL1_ENABLE, \
					   _MG_PLL2_ENABLE)

/* DG1 PLL */
#define DG1_DPLL_ENABLE(pll)    _MMIO_PLL3(pll, DPLL0_ENABLE, DPLL1_ENABLE, \
					   _MG_PLL1_ENABLE, _MG_PLL2_ENABLE)

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
#define TGL_DPLL_CFGCR0(pll)		_MMIO_PLL3(pll, _TGL_DPLL0_CFGCR0, \
						  _TGL_DPLL1_CFGCR0, \
						  _TGL_TBTPLL_CFGCR0)
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
#define TGL_DPLL_CFGCR1(pll)		_MMIO_PLL3(pll, _TGL_DPLL0_CFGCR1, \
						   _TGL_DPLL1_CFGCR1, \
						   _TGL_TBTPLL_CFGCR1)
#define RKL_DPLL_CFGCR1(pll)		_MMIO_PLL(pll, _TGL_DPLL0_CFGCR1, \
						  _TGL_DPLL1_CFGCR1)

#define _DG1_DPLL2_CFGCR0		0x16C284
#define _DG1_DPLL3_CFGCR0		0x16C28C
#define DG1_DPLL_CFGCR0(pll)		_MMIO_PLL3(pll, _TGL_DPLL0_CFGCR0, \
						   _TGL_DPLL1_CFGCR0, \
						   _DG1_DPLL2_CFGCR0, \
						   _DG1_DPLL3_CFGCR0)

#define _DG1_DPLL2_CFGCR1               0x16C288
#define _DG1_DPLL3_CFGCR1               0x16C290
#define DG1_DPLL_CFGCR1(pll)            _MMIO_PLL3(pll, _TGL_DPLL0_CFGCR1, \
						   _TGL_DPLL1_CFGCR1, \
						   _DG1_DPLL2_CFGCR1, \
						   _DG1_DPLL3_CFGCR1)

/* For ADL-S DPLL4_CFGCR0/1 are used to control DPLL2 */
#define _ADLS_DPLL3_CFGCR0		0x1642C0
#define _ADLS_DPLL4_CFGCR0		0x164294
#define ADLS_DPLL_CFGCR0(pll)		_MMIO_PLL3(pll, _TGL_DPLL0_CFGCR0, \
						   _TGL_DPLL1_CFGCR0, \
						   _ADLS_DPLL4_CFGCR0, \
						   _ADLS_DPLL3_CFGCR0)

#define _ADLS_DPLL3_CFGCR1		0x1642C4
#define _ADLS_DPLL4_CFGCR1		0x164298
#define ADLS_DPLL_CFGCR1(pll)		_MMIO_PLL3(pll, _TGL_DPLL0_CFGCR1, \
						   _TGL_DPLL1_CFGCR1, \
						   _ADLS_DPLL4_CFGCR1, \
						   _ADLS_DPLL3_CFGCR1)

#define _DKL_PHY1_BASE			0x168000
#define _DKL_PHY2_BASE			0x169000
#define _DKL_PHY3_BASE			0x16A000
#define _DKL_PHY4_BASE			0x16B000
#define _DKL_PHY5_BASE			0x16C000
#define _DKL_PHY6_BASE			0x16D000

/* DEKEL PHY MMIO Address = Phy base + (internal address & ~index_mask) */
#define _DKL_PLL_DIV0			0x200
#define   DKL_PLL_DIV0_AFC_STARTUP_MASK	REG_GENMASK(27, 25)
#define   DKL_PLL_DIV0_AFC_STARTUP(val)	REG_FIELD_PREP(DKL_PLL_DIV0_AFC_STARTUP_MASK, (val))
#define   DKL_PLL_DIV0_INTEG_COEFF(x)	((x) << 16)
#define   DKL_PLL_DIV0_INTEG_COEFF_MASK	(0x1F << 16)
#define   DKL_PLL_DIV0_PROP_COEFF(x)	((x) << 12)
#define   DKL_PLL_DIV0_PROP_COEFF_MASK	(0xF << 12)
#define   DKL_PLL_DIV0_FBPREDIV_SHIFT   (8)
#define   DKL_PLL_DIV0_FBPREDIV(x)	((x) << DKL_PLL_DIV0_FBPREDIV_SHIFT)
#define   DKL_PLL_DIV0_FBPREDIV_MASK	(0xF << DKL_PLL_DIV0_FBPREDIV_SHIFT)
#define   DKL_PLL_DIV0_FBDIV_INT(x)	((x) << 0)
#define   DKL_PLL_DIV0_FBDIV_INT_MASK	(0xFF << 0)
#define   DKL_PLL_DIV0_MASK		(DKL_PLL_DIV0_INTEG_COEFF_MASK | \
					 DKL_PLL_DIV0_PROP_COEFF_MASK | \
					 DKL_PLL_DIV0_FBPREDIV_MASK | \
					 DKL_PLL_DIV0_FBDIV_INT_MASK)
#define DKL_PLL_DIV0(tc_port)		_MMIO(_PORT(tc_port, _DKL_PHY1_BASE, \
						    _DKL_PHY2_BASE) + \
						    _DKL_PLL_DIV0)

#define _DKL_PLL_DIV1				0x204
#define   DKL_PLL_DIV1_IREF_TRIM(x)		((x) << 16)
#define   DKL_PLL_DIV1_IREF_TRIM_MASK		(0x1F << 16)
#define   DKL_PLL_DIV1_TDC_TARGET_CNT(x)	((x) << 0)
#define   DKL_PLL_DIV1_TDC_TARGET_CNT_MASK	(0xFF << 0)
#define DKL_PLL_DIV1(tc_port)		_MMIO(_PORT(tc_port, _DKL_PHY1_BASE, \
						    _DKL_PHY2_BASE) + \
						    _DKL_PLL_DIV1)

#define _DKL_PLL_SSC				0x210
#define   DKL_PLL_SSC_IREF_NDIV_RATIO(x)	((x) << 29)
#define   DKL_PLL_SSC_IREF_NDIV_RATIO_MASK	(0x7 << 29)
#define   DKL_PLL_SSC_STEP_LEN(x)		((x) << 16)
#define   DKL_PLL_SSC_STEP_LEN_MASK		(0xFF << 16)
#define   DKL_PLL_SSC_STEP_NUM(x)		((x) << 11)
#define   DKL_PLL_SSC_STEP_NUM_MASK		(0x7 << 11)
#define   DKL_PLL_SSC_EN			(1 << 9)
#define DKL_PLL_SSC(tc_port)		_MMIO(_PORT(tc_port, _DKL_PHY1_BASE, \
						    _DKL_PHY2_BASE) + \
						    _DKL_PLL_SSC)

#define _DKL_PLL_BIAS			0x214
#define   DKL_PLL_BIAS_FRAC_EN_H	(1 << 30)
#define   DKL_PLL_BIAS_FBDIV_SHIFT	(8)
#define   DKL_PLL_BIAS_FBDIV_FRAC(x)	((x) << DKL_PLL_BIAS_FBDIV_SHIFT)
#define   DKL_PLL_BIAS_FBDIV_FRAC_MASK	(0x3FFFFF << DKL_PLL_BIAS_FBDIV_SHIFT)
#define DKL_PLL_BIAS(tc_port)		_MMIO(_PORT(tc_port, _DKL_PHY1_BASE, \
						    _DKL_PHY2_BASE) + \
						    _DKL_PLL_BIAS)

#define _DKL_PLL_TDC_COLDST_BIAS		0x218
#define   DKL_PLL_TDC_SSC_STEP_SIZE(x)		((x) << 8)
#define   DKL_PLL_TDC_SSC_STEP_SIZE_MASK	(0xFF << 8)
#define   DKL_PLL_TDC_FEED_FWD_GAIN(x)		((x) << 0)
#define   DKL_PLL_TDC_FEED_FWD_GAIN_MASK	(0xFF << 0)
#define DKL_PLL_TDC_COLDST_BIAS(tc_port) _MMIO(_PORT(tc_port, \
						     _DKL_PHY1_BASE, \
						     _DKL_PHY2_BASE) + \
						     _DKL_PLL_TDC_COLDST_BIAS)

#define _DKL_REFCLKIN_CTL		0x12C
/* Bits are the same as MG_REFCLKIN_CTL */
#define DKL_REFCLKIN_CTL(tc_port)	_MMIO(_PORT(tc_port, \
						    _DKL_PHY1_BASE, \
						    _DKL_PHY2_BASE) + \
					      _DKL_REFCLKIN_CTL)

#define _DKL_CLKTOP2_HSCLKCTL		0xD4
/* Bits are the same as MG_CLKTOP2_HSCLKCTL */
#define DKL_CLKTOP2_HSCLKCTL(tc_port)	_MMIO(_PORT(tc_port, \
						    _DKL_PHY1_BASE, \
						    _DKL_PHY2_BASE) + \
					      _DKL_CLKTOP2_HSCLKCTL)

#define _DKL_CLKTOP2_CORECLKCTL1		0xD8
/* Bits are the same as MG_CLKTOP2_CORECLKCTL1 */
#define DKL_CLKTOP2_CORECLKCTL1(tc_port)	_MMIO(_PORT(tc_port, \
							    _DKL_PHY1_BASE, \
							    _DKL_PHY2_BASE) + \
						      _DKL_CLKTOP2_CORECLKCTL1)

#define _DKL_TX_DPCNTL0				0x2C0
#define  DKL_TX_PRESHOOT_COEFF(x)			((x) << 13)
#define  DKL_TX_PRESHOOT_COEFF_MASK			(0x1f << 13)
#define  DKL_TX_DE_EMPHASIS_COEFF(x)		((x) << 8)
#define  DKL_TX_DE_EMPAHSIS_COEFF_MASK		(0x1f << 8)
#define  DKL_TX_VSWING_CONTROL(x)			((x) << 0)
#define  DKL_TX_VSWING_CONTROL_MASK			(0x7 << 0)
#define DKL_TX_DPCNTL0(tc_port) _MMIO(_PORT(tc_port, \
						     _DKL_PHY1_BASE, \
						     _DKL_PHY2_BASE) + \
						     _DKL_TX_DPCNTL0)

#define _DKL_TX_DPCNTL1				0x2C4
/* Bits are the same as DKL_TX_DPCNTRL0 */
#define DKL_TX_DPCNTL1(tc_port) _MMIO(_PORT(tc_port, \
						     _DKL_PHY1_BASE, \
						     _DKL_PHY2_BASE) + \
						     _DKL_TX_DPCNTL1)

#define _DKL_TX_DPCNTL2					0x2C8
#define  DKL_TX_DP20BITMODE				REG_BIT(2)
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1_MASK	REG_GENMASK(4, 3)
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1(val)	REG_FIELD_PREP(DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1_MASK, (val))
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2_MASK	REG_GENMASK(6, 5)
#define  DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2(val)	REG_FIELD_PREP(DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2_MASK, (val))
#define DKL_TX_DPCNTL2(tc_port) _MMIO(_PORT(tc_port, \
						     _DKL_PHY1_BASE, \
						     _DKL_PHY2_BASE) + \
						     _DKL_TX_DPCNTL2)

#define _DKL_TX_FW_CALIB				0x2F8
#define  DKL_TX_CFG_DISABLE_WAIT_INIT			(1 << 7)
#define DKL_TX_FW_CALIB(tc_port) _MMIO(_PORT(tc_port, \
						     _DKL_PHY1_BASE, \
						     _DKL_PHY2_BASE) + \
						     _DKL_TX_FW_CALIB)

#define _DKL_TX_PMD_LANE_SUS				0xD00
#define DKL_TX_PMD_LANE_SUS(tc_port) _MMIO(_PORT(tc_port, \
							  _DKL_PHY1_BASE, \
							  _DKL_PHY2_BASE) + \
							  _DKL_TX_PMD_LANE_SUS)

#define _DKL_TX_DW17					0xDC4
#define DKL_TX_DW17(tc_port) _MMIO(_PORT(tc_port, \
						     _DKL_PHY1_BASE, \
						     _DKL_PHY2_BASE) + \
						     _DKL_TX_DW17)

#define _DKL_TX_DW18					0xDC8
#define DKL_TX_DW18(tc_port) _MMIO(_PORT(tc_port, \
						     _DKL_PHY1_BASE, \
						     _DKL_PHY2_BASE) + \
						     _DKL_TX_DW18)

#define _DKL_DP_MODE					0xA0
#define DKL_DP_MODE(tc_port) _MMIO(_PORT(tc_port, \
						     _DKL_PHY1_BASE, \
						     _DKL_PHY2_BASE) + \
						     _DKL_DP_MODE)

#define _DKL_CMN_UC_DW27			0x36C
#define  DKL_CMN_UC_DW27_UC_HEALTH		(0x1 << 15)
#define DKL_CMN_UC_DW_27(tc_port)		_MMIO(_PORT(tc_port, \
							    _DKL_PHY1_BASE, \
							    _DKL_PHY2_BASE) + \
							    _DKL_CMN_UC_DW27)

/*
 * Each Dekel PHY is addressed through a 4KB aperture. Each PHY has more than
 * 4KB of register space, so a separate index is programmed in HIP_INDEX_REG0
 * or HIP_INDEX_REG1, based on the port number, to set the upper 2 address
 * bits that point the 4KB window into the full PHY register space.
 */
#define _HIP_INDEX_REG0			0x1010A0
#define _HIP_INDEX_REG1			0x1010A4
#define HIP_INDEX_REG(tc_port)		_MMIO((tc_port) < 4 ? _HIP_INDEX_REG0 \
					      : _HIP_INDEX_REG1)
#define _HIP_INDEX_SHIFT(tc_port)	(8 * ((tc_port) % 4))
#define HIP_INDEX_VAL(tc_port, val)	((val) << _HIP_INDEX_SHIFT(tc_port))

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

/* pipe CSC */
#define _PIPE_A_CSC_COEFF_RY_GY	0x49010
#define _PIPE_A_CSC_COEFF_BY	0x49014
#define _PIPE_A_CSC_COEFF_RU_GU	0x49018
#define _PIPE_A_CSC_COEFF_BU	0x4901c
#define _PIPE_A_CSC_COEFF_RV_GV	0x49020
#define _PIPE_A_CSC_COEFF_BV	0x49024

#define _PIPE_A_CSC_MODE	0x49028
#define  ICL_CSC_ENABLE			(1 << 31) /* icl+ */
#define  ICL_OUTPUT_CSC_ENABLE		(1 << 30) /* icl+ */
#define  CSC_BLACK_SCREEN_OFFSET	(1 << 2) /* ilk/snb */
#define  CSC_POSITION_BEFORE_GAMMA	(1 << 1) /* pre-glk */
#define  CSC_MODE_YUV_TO_RGB		(1 << 0) /* ilk/snb */

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

#define PIPE_CSC_COEFF_RY_GY(pipe)	_MMIO_PIPE(pipe, _PIPE_A_CSC_COEFF_RY_GY, _PIPE_B_CSC_COEFF_RY_GY)
#define PIPE_CSC_COEFF_BY(pipe)		_MMIO_PIPE(pipe, _PIPE_A_CSC_COEFF_BY, _PIPE_B_CSC_COEFF_BY)
#define PIPE_CSC_COEFF_RU_GU(pipe)	_MMIO_PIPE(pipe, _PIPE_A_CSC_COEFF_RU_GU, _PIPE_B_CSC_COEFF_RU_GU)
#define PIPE_CSC_COEFF_BU(pipe)		_MMIO_PIPE(pipe, _PIPE_A_CSC_COEFF_BU, _PIPE_B_CSC_COEFF_BU)
#define PIPE_CSC_COEFF_RV_GV(pipe)	_MMIO_PIPE(pipe, _PIPE_A_CSC_COEFF_RV_GV, _PIPE_B_CSC_COEFF_RV_GV)
#define PIPE_CSC_COEFF_BV(pipe)		_MMIO_PIPE(pipe, _PIPE_A_CSC_COEFF_BV, _PIPE_B_CSC_COEFF_BV)
#define PIPE_CSC_MODE(pipe)		_MMIO_PIPE(pipe, _PIPE_A_CSC_MODE, _PIPE_B_CSC_MODE)
#define PIPE_CSC_PREOFF_HI(pipe)	_MMIO_PIPE(pipe, _PIPE_A_CSC_PREOFF_HI, _PIPE_B_CSC_PREOFF_HI)
#define PIPE_CSC_PREOFF_ME(pipe)	_MMIO_PIPE(pipe, _PIPE_A_CSC_PREOFF_ME, _PIPE_B_CSC_PREOFF_ME)
#define PIPE_CSC_PREOFF_LO(pipe)	_MMIO_PIPE(pipe, _PIPE_A_CSC_PREOFF_LO, _PIPE_B_CSC_PREOFF_LO)
#define PIPE_CSC_POSTOFF_HI(pipe)	_MMIO_PIPE(pipe, _PIPE_A_CSC_POSTOFF_HI, _PIPE_B_CSC_POSTOFF_HI)
#define PIPE_CSC_POSTOFF_ME(pipe)	_MMIO_PIPE(pipe, _PIPE_A_CSC_POSTOFF_ME, _PIPE_B_CSC_POSTOFF_ME)
#define PIPE_CSC_POSTOFF_LO(pipe)	_MMIO_PIPE(pipe, _PIPE_A_CSC_POSTOFF_LO, _PIPE_B_CSC_POSTOFF_LO)

/* Pipe Output CSC */
#define _PIPE_A_OUTPUT_CSC_COEFF_RY_GY	0x49050
#define _PIPE_A_OUTPUT_CSC_COEFF_BY	0x49054
#define _PIPE_A_OUTPUT_CSC_COEFF_RU_GU	0x49058
#define _PIPE_A_OUTPUT_CSC_COEFF_BU	0x4905c
#define _PIPE_A_OUTPUT_CSC_COEFF_RV_GV	0x49060
#define _PIPE_A_OUTPUT_CSC_COEFF_BV	0x49064
#define _PIPE_A_OUTPUT_CSC_PREOFF_HI	0x49068
#define _PIPE_A_OUTPUT_CSC_PREOFF_ME	0x4906c
#define _PIPE_A_OUTPUT_CSC_PREOFF_LO	0x49070
#define _PIPE_A_OUTPUT_CSC_POSTOFF_HI	0x49074
#define _PIPE_A_OUTPUT_CSC_POSTOFF_ME	0x49078
#define _PIPE_A_OUTPUT_CSC_POSTOFF_LO	0x4907c

#define _PIPE_B_OUTPUT_CSC_COEFF_RY_GY	0x49150
#define _PIPE_B_OUTPUT_CSC_COEFF_BY	0x49154
#define _PIPE_B_OUTPUT_CSC_COEFF_RU_GU	0x49158
#define _PIPE_B_OUTPUT_CSC_COEFF_BU	0x4915c
#define _PIPE_B_OUTPUT_CSC_COEFF_RV_GV	0x49160
#define _PIPE_B_OUTPUT_CSC_COEFF_BV	0x49164
#define _PIPE_B_OUTPUT_CSC_PREOFF_HI	0x49168
#define _PIPE_B_OUTPUT_CSC_PREOFF_ME	0x4916c
#define _PIPE_B_OUTPUT_CSC_PREOFF_LO	0x49170
#define _PIPE_B_OUTPUT_CSC_POSTOFF_HI	0x49174
#define _PIPE_B_OUTPUT_CSC_POSTOFF_ME	0x49178
#define _PIPE_B_OUTPUT_CSC_POSTOFF_LO	0x4917c

#define PIPE_CSC_OUTPUT_COEFF_RY_GY(pipe)	_MMIO_PIPE(pipe,\
							   _PIPE_A_OUTPUT_CSC_COEFF_RY_GY,\
							   _PIPE_B_OUTPUT_CSC_COEFF_RY_GY)
#define PIPE_CSC_OUTPUT_COEFF_BY(pipe)		_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_COEFF_BY, \
							   _PIPE_B_OUTPUT_CSC_COEFF_BY)
#define PIPE_CSC_OUTPUT_COEFF_RU_GU(pipe)	_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_COEFF_RU_GU, \
							   _PIPE_B_OUTPUT_CSC_COEFF_RU_GU)
#define PIPE_CSC_OUTPUT_COEFF_BU(pipe)		_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_COEFF_BU, \
							   _PIPE_B_OUTPUT_CSC_COEFF_BU)
#define PIPE_CSC_OUTPUT_COEFF_RV_GV(pipe)	_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_COEFF_RV_GV, \
							   _PIPE_B_OUTPUT_CSC_COEFF_RV_GV)
#define PIPE_CSC_OUTPUT_COEFF_BV(pipe)		_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_COEFF_BV, \
							   _PIPE_B_OUTPUT_CSC_COEFF_BV)
#define PIPE_CSC_OUTPUT_PREOFF_HI(pipe)		_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_PREOFF_HI, \
							   _PIPE_B_OUTPUT_CSC_PREOFF_HI)
#define PIPE_CSC_OUTPUT_PREOFF_ME(pipe)		_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_PREOFF_ME, \
							   _PIPE_B_OUTPUT_CSC_PREOFF_ME)
#define PIPE_CSC_OUTPUT_PREOFF_LO(pipe)		_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_PREOFF_LO, \
							   _PIPE_B_OUTPUT_CSC_PREOFF_LO)
#define PIPE_CSC_OUTPUT_POSTOFF_HI(pipe)	_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_POSTOFF_HI, \
							   _PIPE_B_OUTPUT_CSC_POSTOFF_HI)
#define PIPE_CSC_OUTPUT_POSTOFF_ME(pipe)	_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_POSTOFF_ME, \
							   _PIPE_B_OUTPUT_CSC_POSTOFF_ME)
#define PIPE_CSC_OUTPUT_POSTOFF_LO(pipe)	_MMIO_PIPE(pipe, \
							   _PIPE_A_OUTPUT_CSC_POSTOFF_LO, \
							   _PIPE_B_OUTPUT_CSC_POSTOFF_LO)

/* pipe degamma/gamma LUTs on IVB+ */
#define _PAL_PREC_INDEX_A	0x4A400
#define _PAL_PREC_INDEX_B	0x4AC00
#define _PAL_PREC_INDEX_C	0x4B400
#define   PAL_PREC_10_12_BIT		(0 << 31)
#define   PAL_PREC_SPLIT_MODE		(1 << 31)
#define   PAL_PREC_AUTO_INCREMENT	(1 << 15)
#define   PAL_PREC_INDEX_VALUE_MASK	(0x3ff << 0)
#define   PAL_PREC_INDEX_VALUE(x)	((x) << 0)
#define _PAL_PREC_DATA_A	0x4A404
#define _PAL_PREC_DATA_B	0x4AC04
#define _PAL_PREC_DATA_C	0x4B404
#define _PAL_PREC_GC_MAX_A	0x4A410
#define _PAL_PREC_GC_MAX_B	0x4AC10
#define _PAL_PREC_GC_MAX_C	0x4B410
#define   PREC_PAL_DATA_RED_MASK	REG_GENMASK(29, 20)
#define   PREC_PAL_DATA_GREEN_MASK	REG_GENMASK(19, 10)
#define   PREC_PAL_DATA_BLUE_MASK	REG_GENMASK(9, 0)
#define _PAL_PREC_EXT_GC_MAX_A	0x4A420
#define _PAL_PREC_EXT_GC_MAX_B	0x4AC20
#define _PAL_PREC_EXT_GC_MAX_C	0x4B420
#define _PAL_PREC_EXT2_GC_MAX_A	0x4A430
#define _PAL_PREC_EXT2_GC_MAX_B	0x4AC30
#define _PAL_PREC_EXT2_GC_MAX_C	0x4B430

#define PREC_PAL_INDEX(pipe)		_MMIO_PIPE(pipe, _PAL_PREC_INDEX_A, _PAL_PREC_INDEX_B)
#define PREC_PAL_DATA(pipe)		_MMIO_PIPE(pipe, _PAL_PREC_DATA_A, _PAL_PREC_DATA_B)
#define PREC_PAL_GC_MAX(pipe, i)	_MMIO(_PIPE(pipe, _PAL_PREC_GC_MAX_A, _PAL_PREC_GC_MAX_B) + (i) * 4)
#define PREC_PAL_EXT_GC_MAX(pipe, i)	_MMIO(_PIPE(pipe, _PAL_PREC_EXT_GC_MAX_A, _PAL_PREC_EXT_GC_MAX_B) + (i) * 4)
#define PREC_PAL_EXT2_GC_MAX(pipe, i)	_MMIO(_PIPE(pipe, _PAL_PREC_EXT2_GC_MAX_A, _PAL_PREC_EXT2_GC_MAX_B) + (i) * 4)

#define _PRE_CSC_GAMC_INDEX_A	0x4A484
#define _PRE_CSC_GAMC_INDEX_B	0x4AC84
#define _PRE_CSC_GAMC_INDEX_C	0x4B484
#define   PRE_CSC_GAMC_AUTO_INCREMENT	(1 << 10)
#define _PRE_CSC_GAMC_DATA_A	0x4A488
#define _PRE_CSC_GAMC_DATA_B	0x4AC88
#define _PRE_CSC_GAMC_DATA_C	0x4B488

#define PRE_CSC_GAMC_INDEX(pipe)	_MMIO_PIPE(pipe, _PRE_CSC_GAMC_INDEX_A, _PRE_CSC_GAMC_INDEX_B)
#define PRE_CSC_GAMC_DATA(pipe)		_MMIO_PIPE(pipe, _PRE_CSC_GAMC_DATA_A, _PRE_CSC_GAMC_DATA_B)

/* ICL Multi segmented gamma */
#define _PAL_PREC_MULTI_SEG_INDEX_A	0x4A408
#define _PAL_PREC_MULTI_SEG_INDEX_B	0x4AC08
#define  PAL_PREC_MULTI_SEGMENT_AUTO_INCREMENT		REG_BIT(15)
#define  PAL_PREC_MULTI_SEGMENT_INDEX_VALUE_MASK	REG_GENMASK(4, 0)

#define _PAL_PREC_MULTI_SEG_DATA_A	0x4A40C
#define _PAL_PREC_MULTI_SEG_DATA_B	0x4AC0C
#define  PAL_PREC_MULTI_SEG_RED_LDW_MASK   REG_GENMASK(29, 24)
#define  PAL_PREC_MULTI_SEG_RED_UDW_MASK   REG_GENMASK(29, 20)
#define  PAL_PREC_MULTI_SEG_GREEN_LDW_MASK REG_GENMASK(19, 14)
#define  PAL_PREC_MULTI_SEG_GREEN_UDW_MASK REG_GENMASK(19, 10)
#define  PAL_PREC_MULTI_SEG_BLUE_LDW_MASK  REG_GENMASK(9, 4)
#define  PAL_PREC_MULTI_SEG_BLUE_UDW_MASK  REG_GENMASK(9, 0)

#define PREC_PAL_MULTI_SEG_INDEX(pipe)	_MMIO_PIPE(pipe, \
					_PAL_PREC_MULTI_SEG_INDEX_A, \
					_PAL_PREC_MULTI_SEG_INDEX_B)
#define PREC_PAL_MULTI_SEG_DATA(pipe)	_MMIO_PIPE(pipe, \
					_PAL_PREC_MULTI_SEG_DATA_A, \
					_PAL_PREC_MULTI_SEG_DATA_B)

#define _MMIO_PLANE_GAMC(plane, i, a, b)  _MMIO(_PIPE(plane, a, b) + (i) * 4)

/* Plane CSC Registers */
#define _PLANE_CSC_RY_GY_1_A	0x70210
#define _PLANE_CSC_RY_GY_2_A	0x70310

#define _PLANE_CSC_RY_GY_1_B	0x71210
#define _PLANE_CSC_RY_GY_2_B	0x71310

#define _PLANE_CSC_RY_GY_1(pipe)	_PIPE(pipe, _PLANE_CSC_RY_GY_1_A, \
					      _PLANE_CSC_RY_GY_1_B)
#define _PLANE_CSC_RY_GY_2(pipe)	_PIPE(pipe, _PLANE_INPUT_CSC_RY_GY_2_A, \
					      _PLANE_INPUT_CSC_RY_GY_2_B)
#define PLANE_CSC_COEFF(pipe, plane, index)	_MMIO_PLANE(plane, \
							    _PLANE_CSC_RY_GY_1(pipe) +  (index) * 4, \
							    _PLANE_CSC_RY_GY_2(pipe) + (index) * 4)

#define _PLANE_CSC_PREOFF_HI_1_A		0x70228
#define _PLANE_CSC_PREOFF_HI_2_A		0x70328

#define _PLANE_CSC_PREOFF_HI_1_B		0x71228
#define _PLANE_CSC_PREOFF_HI_2_B		0x71328

#define _PLANE_CSC_PREOFF_HI_1(pipe)	_PIPE(pipe, _PLANE_CSC_PREOFF_HI_1_A, \
					      _PLANE_CSC_PREOFF_HI_1_B)
#define _PLANE_CSC_PREOFF_HI_2(pipe)	_PIPE(pipe, _PLANE_CSC_PREOFF_HI_2_A, \
					      _PLANE_CSC_PREOFF_HI_2_B)
#define PLANE_CSC_PREOFF(pipe, plane, index)	_MMIO_PLANE(plane, _PLANE_CSC_PREOFF_HI_1(pipe) + \
							    (index) * 4, _PLANE_CSC_PREOFF_HI_2(pipe) + \
							    (index) * 4)

#define _PLANE_CSC_POSTOFF_HI_1_A		0x70234
#define _PLANE_CSC_POSTOFF_HI_2_A		0x70334

#define _PLANE_CSC_POSTOFF_HI_1_B		0x71234
#define _PLANE_CSC_POSTOFF_HI_2_B		0x71334

#define _PLANE_CSC_POSTOFF_HI_1(pipe)	_PIPE(pipe, _PLANE_CSC_POSTOFF_HI_1_A, \
					      _PLANE_CSC_POSTOFF_HI_1_B)
#define _PLANE_CSC_POSTOFF_HI_2(pipe)	_PIPE(pipe, _PLANE_CSC_POSTOFF_HI_2_A, \
					      _PLANE_CSC_POSTOFF_HI_2_B)
#define PLANE_CSC_POSTOFF(pipe, plane, index)	_MMIO_PLANE(plane, _PLANE_CSC_POSTOFF_HI_1(pipe) + \
							    (index) * 4, _PLANE_CSC_POSTOFF_HI_2(pipe) + \
							    (index) * 4)

/* pipe CSC & degamma/gamma LUTs on CHV */
#define _CGM_PIPE_A_CSC_COEFF01	(VLV_DISPLAY_BASE + 0x67900)
#define _CGM_PIPE_A_CSC_COEFF23	(VLV_DISPLAY_BASE + 0x67904)
#define _CGM_PIPE_A_CSC_COEFF45	(VLV_DISPLAY_BASE + 0x67908)
#define _CGM_PIPE_A_CSC_COEFF67	(VLV_DISPLAY_BASE + 0x6790C)
#define _CGM_PIPE_A_CSC_COEFF8	(VLV_DISPLAY_BASE + 0x67910)
#define _CGM_PIPE_A_DEGAMMA	(VLV_DISPLAY_BASE + 0x66000)
#define   CGM_PIPE_DEGAMMA_RED_MASK	REG_GENMASK(13, 0)
#define   CGM_PIPE_DEGAMMA_GREEN_MASK	REG_GENMASK(29, 16)
#define   CGM_PIPE_DEGAMMA_BLUE_MASK	REG_GENMASK(13, 0)
#define _CGM_PIPE_A_GAMMA	(VLV_DISPLAY_BASE + 0x67000)
#define   CGM_PIPE_GAMMA_RED_MASK	REG_GENMASK(9, 0)
#define   CGM_PIPE_GAMMA_GREEN_MASK	REG_GENMASK(25, 16)
#define   CGM_PIPE_GAMMA_BLUE_MASK	REG_GENMASK(9, 0)
#define _CGM_PIPE_A_MODE	(VLV_DISPLAY_BASE + 0x67A00)
#define   CGM_PIPE_MODE_GAMMA	(1 << 2)
#define   CGM_PIPE_MODE_CSC	(1 << 1)
#define   CGM_PIPE_MODE_DEGAMMA	(1 << 0)

#define _CGM_PIPE_B_CSC_COEFF01	(VLV_DISPLAY_BASE + 0x69900)
#define _CGM_PIPE_B_CSC_COEFF23	(VLV_DISPLAY_BASE + 0x69904)
#define _CGM_PIPE_B_CSC_COEFF45	(VLV_DISPLAY_BASE + 0x69908)
#define _CGM_PIPE_B_CSC_COEFF67	(VLV_DISPLAY_BASE + 0x6990C)
#define _CGM_PIPE_B_CSC_COEFF8	(VLV_DISPLAY_BASE + 0x69910)
#define _CGM_PIPE_B_DEGAMMA	(VLV_DISPLAY_BASE + 0x68000)
#define _CGM_PIPE_B_GAMMA	(VLV_DISPLAY_BASE + 0x69000)
#define _CGM_PIPE_B_MODE	(VLV_DISPLAY_BASE + 0x69A00)

#define CGM_PIPE_CSC_COEFF01(pipe)	_MMIO_PIPE(pipe, _CGM_PIPE_A_CSC_COEFF01, _CGM_PIPE_B_CSC_COEFF01)
#define CGM_PIPE_CSC_COEFF23(pipe)	_MMIO_PIPE(pipe, _CGM_PIPE_A_CSC_COEFF23, _CGM_PIPE_B_CSC_COEFF23)
#define CGM_PIPE_CSC_COEFF45(pipe)	_MMIO_PIPE(pipe, _CGM_PIPE_A_CSC_COEFF45, _CGM_PIPE_B_CSC_COEFF45)
#define CGM_PIPE_CSC_COEFF67(pipe)	_MMIO_PIPE(pipe, _CGM_PIPE_A_CSC_COEFF67, _CGM_PIPE_B_CSC_COEFF67)
#define CGM_PIPE_CSC_COEFF8(pipe)	_MMIO_PIPE(pipe, _CGM_PIPE_A_CSC_COEFF8, _CGM_PIPE_B_CSC_COEFF8)
#define CGM_PIPE_DEGAMMA(pipe, i, w)	_MMIO(_PIPE(pipe, _CGM_PIPE_A_DEGAMMA, _CGM_PIPE_B_DEGAMMA) + (i) * 8 + (w) * 4)
#define CGM_PIPE_GAMMA(pipe, i, w)	_MMIO(_PIPE(pipe, _CGM_PIPE_A_GAMMA, _CGM_PIPE_B_GAMMA) + (i) * 8 + (w) * 4)
#define CGM_PIPE_MODE(pipe)		_MMIO_PIPE(pipe, _CGM_PIPE_A_MODE, _CGM_PIPE_B_MODE)

/* MIPI DSI registers */

#define _MIPI_PORT(port, a, c)	(((port) == PORT_A) ? a : c)	/* ports A and C only */
#define _MMIO_MIPI(port, a, c)	_MMIO(_MIPI_PORT(port, a, c))

/* Gen11 DSI */
#define _MMIO_DSI(tc, dsi0, dsi1)	_MMIO_TRANS((tc) - TRANSCODER_DSI_0, \
						    dsi0, dsi1)

#define MIPIO_TXESC_CLK_DIV1			_MMIO(0x160004)
#define  GLK_TX_ESC_CLK_DIV1_MASK			0x3FF
#define MIPIO_TXESC_CLK_DIV2			_MMIO(0x160008)
#define  GLK_TX_ESC_CLK_DIV2_MASK			0x3FF

#define _ICL_DSI_ESC_CLK_DIV0		0x6b090
#define _ICL_DSI_ESC_CLK_DIV1		0x6b890
#define ICL_DSI_ESC_CLK_DIV(port)	_MMIO_PORT((port),	\
							_ICL_DSI_ESC_CLK_DIV0, \
							_ICL_DSI_ESC_CLK_DIV1)
#define _ICL_DPHY_ESC_CLK_DIV0		0x162190
#define _ICL_DPHY_ESC_CLK_DIV1		0x6C190
#define ICL_DPHY_ESC_CLK_DIV(port)	_MMIO_PORT((port),	\
						_ICL_DPHY_ESC_CLK_DIV0, \
						_ICL_DPHY_ESC_CLK_DIV1)
#define  ICL_BYTE_CLK_PER_ESC_CLK_MASK		(0x1f << 16)
#define  ICL_BYTE_CLK_PER_ESC_CLK_SHIFT	16
#define  ICL_ESC_CLK_DIV_MASK			0x1ff
#define  ICL_ESC_CLK_DIV_SHIFT			0
#define DSI_MAX_ESC_CLK			20000		/* in KHz */

#define _ADL_MIPIO_REG			0x180
#define ADL_MIPIO_DW(port, dw)		_MMIO(_ICL_COMBOPHY(port) + _ADL_MIPIO_REG + 4 * (dw))
#define   TX_ESC_CLK_DIV_PHY_SEL	REGBIT(16)
#define   TX_ESC_CLK_DIV_PHY_MASK	REG_GENMASK(23, 16)
#define   TX_ESC_CLK_DIV_PHY		REG_FIELD_PREP(TX_ESC_CLK_DIV_PHY_MASK, 0x7f)

#define _DSI_CMD_FRMCTL_0		0x6b034
#define _DSI_CMD_FRMCTL_1		0x6b834
#define DSI_CMD_FRMCTL(port)		_MMIO_PORT(port,	\
						   _DSI_CMD_FRMCTL_0,\
						   _DSI_CMD_FRMCTL_1)
#define   DSI_FRAME_UPDATE_REQUEST		(1 << 31)
#define   DSI_PERIODIC_FRAME_UPDATE_ENABLE	(1 << 29)
#define   DSI_NULL_PACKET_ENABLE		(1 << 28)
#define   DSI_FRAME_IN_PROGRESS			(1 << 0)

#define _DSI_INTR_MASK_REG_0		0x6b070
#define _DSI_INTR_MASK_REG_1		0x6b870
#define DSI_INTR_MASK_REG(port)		_MMIO_PORT(port,	\
						   _DSI_INTR_MASK_REG_0,\
						   _DSI_INTR_MASK_REG_1)

#define _DSI_INTR_IDENT_REG_0		0x6b074
#define _DSI_INTR_IDENT_REG_1		0x6b874
#define DSI_INTR_IDENT_REG(port)	_MMIO_PORT(port,	\
						   _DSI_INTR_IDENT_REG_0,\
						   _DSI_INTR_IDENT_REG_1)
#define   DSI_TE_EVENT				(1 << 31)
#define   DSI_RX_DATA_OR_BTA_TERMINATED		(1 << 30)
#define   DSI_TX_DATA				(1 << 29)
#define   DSI_ULPS_ENTRY_DONE			(1 << 28)
#define   DSI_NON_TE_TRIGGER_RECEIVED		(1 << 27)
#define   DSI_HOST_CHKSUM_ERROR			(1 << 26)
#define   DSI_HOST_MULTI_ECC_ERROR		(1 << 25)
#define   DSI_HOST_SINGL_ECC_ERROR		(1 << 24)
#define   DSI_HOST_CONTENTION_DETECTED		(1 << 23)
#define   DSI_HOST_FALSE_CONTROL_ERROR		(1 << 22)
#define   DSI_HOST_TIMEOUT_ERROR		(1 << 21)
#define   DSI_HOST_LOW_POWER_TX_SYNC_ERROR	(1 << 20)
#define   DSI_HOST_ESCAPE_MODE_ENTRY_ERROR	(1 << 19)
#define   DSI_FRAME_UPDATE_DONE			(1 << 16)
#define   DSI_PROTOCOL_VIOLATION_REPORTED	(1 << 15)
#define   DSI_INVALID_TX_LENGTH			(1 << 13)
#define   DSI_INVALID_VC			(1 << 12)
#define   DSI_INVALID_DATA_TYPE			(1 << 11)
#define   DSI_PERIPHERAL_CHKSUM_ERROR		(1 << 10)
#define   DSI_PERIPHERAL_MULTI_ECC_ERROR	(1 << 9)
#define   DSI_PERIPHERAL_SINGLE_ECC_ERROR	(1 << 8)
#define   DSI_PERIPHERAL_CONTENTION_DETECTED	(1 << 7)
#define   DSI_PERIPHERAL_FALSE_CTRL_ERROR	(1 << 6)
#define   DSI_PERIPHERAL_TIMEOUT_ERROR		(1 << 5)
#define   DSI_PERIPHERAL_LP_TX_SYNC_ERROR	(1 << 4)
#define   DSI_PERIPHERAL_ESC_MODE_ENTRY_CMD_ERR	(1 << 3)
#define   DSI_EOT_SYNC_ERROR			(1 << 2)
#define   DSI_SOT_SYNC_ERROR			(1 << 1)
#define   DSI_SOT_ERROR				(1 << 0)

/* Gen4+ Timestamp and Pipe Frame time stamp registers */
#define GEN4_TIMESTAMP		_MMIO(0x2358)
#define ILK_TIMESTAMP_HI	_MMIO(0x70070)
#define IVB_TIMESTAMP_CTR	_MMIO(0x44070)

#define GEN9_TIMESTAMP_OVERRIDE				_MMIO(0x44074)
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_SHIFT	0
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_MASK	0x3ff
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_SHIFT	12
#define  GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_MASK	(0xf << 12)

#define _PIPE_FRMTMSTMP_A		0x70048
#define PIPE_FRMTMSTMP(pipe)		\
			_MMIO_PIPE2(pipe, _PIPE_FRMTMSTMP_A)

/* BXT MIPI clock controls */
#define BXT_MAX_VAR_OUTPUT_KHZ			39500

#define BXT_MIPI_CLOCK_CTL			_MMIO(0x46090)
#define  BXT_MIPI1_DIV_SHIFT			26
#define  BXT_MIPI2_DIV_SHIFT			10
#define  BXT_MIPI_DIV_SHIFT(port)		\
			_MIPI_PORT(port, BXT_MIPI1_DIV_SHIFT, \
					BXT_MIPI2_DIV_SHIFT)

/* TX control divider to select actual TX clock output from (8x/var) */
#define  BXT_MIPI1_TX_ESCLK_SHIFT		26
#define  BXT_MIPI2_TX_ESCLK_SHIFT		10
#define  BXT_MIPI_TX_ESCLK_SHIFT(port)		\
			_MIPI_PORT(port, BXT_MIPI1_TX_ESCLK_SHIFT, \
					BXT_MIPI2_TX_ESCLK_SHIFT)
#define  BXT_MIPI1_TX_ESCLK_FIXDIV_MASK		(0x3F << 26)
#define  BXT_MIPI2_TX_ESCLK_FIXDIV_MASK		(0x3F << 10)
#define  BXT_MIPI_TX_ESCLK_FIXDIV_MASK(port)	\
			_MIPI_PORT(port, BXT_MIPI1_TX_ESCLK_FIXDIV_MASK, \
					BXT_MIPI2_TX_ESCLK_FIXDIV_MASK)
#define  BXT_MIPI_TX_ESCLK_DIVIDER(port, val)	\
		(((val) & 0x3F) << BXT_MIPI_TX_ESCLK_SHIFT(port))
/* RX upper control divider to select actual RX clock output from 8x */
#define  BXT_MIPI1_RX_ESCLK_UPPER_SHIFT		21
#define  BXT_MIPI2_RX_ESCLK_UPPER_SHIFT		5
#define  BXT_MIPI_RX_ESCLK_UPPER_SHIFT(port)		\
			_MIPI_PORT(port, BXT_MIPI1_RX_ESCLK_UPPER_SHIFT, \
					BXT_MIPI2_RX_ESCLK_UPPER_SHIFT)
#define  BXT_MIPI1_RX_ESCLK_UPPER_FIXDIV_MASK		(3 << 21)
#define  BXT_MIPI2_RX_ESCLK_UPPER_FIXDIV_MASK		(3 << 5)
#define  BXT_MIPI_RX_ESCLK_UPPER_FIXDIV_MASK(port)	\
			_MIPI_PORT(port, BXT_MIPI1_RX_ESCLK_UPPER_FIXDIV_MASK, \
					BXT_MIPI2_RX_ESCLK_UPPER_FIXDIV_MASK)
#define  BXT_MIPI_RX_ESCLK_UPPER_DIVIDER(port, val)	\
		(((val) & 3) << BXT_MIPI_RX_ESCLK_UPPER_SHIFT(port))
/* 8/3X divider to select the actual 8/3X clock output from 8x */
#define  BXT_MIPI1_8X_BY3_SHIFT                19
#define  BXT_MIPI2_8X_BY3_SHIFT                3
#define  BXT_MIPI_8X_BY3_SHIFT(port)          \
			_MIPI_PORT(port, BXT_MIPI1_8X_BY3_SHIFT, \
					BXT_MIPI2_8X_BY3_SHIFT)
#define  BXT_MIPI1_8X_BY3_DIVIDER_MASK         (3 << 19)
#define  BXT_MIPI2_8X_BY3_DIVIDER_MASK         (3 << 3)
#define  BXT_MIPI_8X_BY3_DIVIDER_MASK(port)    \
			_MIPI_PORT(port, BXT_MIPI1_8X_BY3_DIVIDER_MASK, \
						BXT_MIPI2_8X_BY3_DIVIDER_MASK)
#define  BXT_MIPI_8X_BY3_DIVIDER(port, val)    \
			(((val) & 3) << BXT_MIPI_8X_BY3_SHIFT(port))
/* RX lower control divider to select actual RX clock output from 8x */
#define  BXT_MIPI1_RX_ESCLK_LOWER_SHIFT		16
#define  BXT_MIPI2_RX_ESCLK_LOWER_SHIFT		0
#define  BXT_MIPI_RX_ESCLK_LOWER_SHIFT(port)		\
			_MIPI_PORT(port, BXT_MIPI1_RX_ESCLK_LOWER_SHIFT, \
					BXT_MIPI2_RX_ESCLK_LOWER_SHIFT)
#define  BXT_MIPI1_RX_ESCLK_LOWER_FIXDIV_MASK		(3 << 16)
#define  BXT_MIPI2_RX_ESCLK_LOWER_FIXDIV_MASK		(3 << 0)
#define  BXT_MIPI_RX_ESCLK_LOWER_FIXDIV_MASK(port)	\
			_MIPI_PORT(port, BXT_MIPI1_RX_ESCLK_LOWER_FIXDIV_MASK, \
					BXT_MIPI2_RX_ESCLK_LOWER_FIXDIV_MASK)
#define  BXT_MIPI_RX_ESCLK_LOWER_DIVIDER(port, val)	\
		(((val) & 3) << BXT_MIPI_RX_ESCLK_LOWER_SHIFT(port))

#define RX_DIVIDER_BIT_1_2                     0x3
#define RX_DIVIDER_BIT_3_4                     0xC

/* BXT MIPI mode configure */
#define  _BXT_MIPIA_TRANS_HACTIVE			0x6B0F8
#define  _BXT_MIPIC_TRANS_HACTIVE			0x6B8F8
#define  BXT_MIPI_TRANS_HACTIVE(tc)	_MMIO_MIPI(tc, \
		_BXT_MIPIA_TRANS_HACTIVE, _BXT_MIPIC_TRANS_HACTIVE)

#define  _BXT_MIPIA_TRANS_VACTIVE			0x6B0FC
#define  _BXT_MIPIC_TRANS_VACTIVE			0x6B8FC
#define  BXT_MIPI_TRANS_VACTIVE(tc)	_MMIO_MIPI(tc, \
		_BXT_MIPIA_TRANS_VACTIVE, _BXT_MIPIC_TRANS_VACTIVE)

#define  _BXT_MIPIA_TRANS_VTOTAL			0x6B100
#define  _BXT_MIPIC_TRANS_VTOTAL			0x6B900
#define  BXT_MIPI_TRANS_VTOTAL(tc)	_MMIO_MIPI(tc, \
		_BXT_MIPIA_TRANS_VTOTAL, _BXT_MIPIC_TRANS_VTOTAL)

#define BXT_DSI_PLL_CTL			_MMIO(0x161000)
#define  BXT_DSI_PLL_PVD_RATIO_SHIFT	16
#define  BXT_DSI_PLL_PVD_RATIO_MASK	(3 << BXT_DSI_PLL_PVD_RATIO_SHIFT)
#define  BXT_DSI_PLL_PVD_RATIO_1	(1 << BXT_DSI_PLL_PVD_RATIO_SHIFT)
#define  BXT_DSIC_16X_BY1		(0 << 10)
#define  BXT_DSIC_16X_BY2		(1 << 10)
#define  BXT_DSIC_16X_BY3		(2 << 10)
#define  BXT_DSIC_16X_BY4		(3 << 10)
#define  BXT_DSIC_16X_MASK		(3 << 10)
#define  BXT_DSIA_16X_BY1		(0 << 8)
#define  BXT_DSIA_16X_BY2		(1 << 8)
#define  BXT_DSIA_16X_BY3		(2 << 8)
#define  BXT_DSIA_16X_BY4		(3 << 8)
#define  BXT_DSIA_16X_MASK		(3 << 8)
#define  BXT_DSI_FREQ_SEL_SHIFT		8
#define  BXT_DSI_FREQ_SEL_MASK		(0xF << BXT_DSI_FREQ_SEL_SHIFT)

#define BXT_DSI_PLL_RATIO_MAX		0x7D
#define BXT_DSI_PLL_RATIO_MIN		0x22
#define GLK_DSI_PLL_RATIO_MAX		0x6F
#define GLK_DSI_PLL_RATIO_MIN		0x22
#define BXT_DSI_PLL_RATIO_MASK		0xFF
#define BXT_REF_CLOCK_KHZ		19200

#define BXT_DSI_PLL_ENABLE		_MMIO(0x46080)
#define  BXT_DSI_PLL_DO_ENABLE		(1 << 31)
#define  BXT_DSI_PLL_LOCKED		(1 << 30)

#define _MIPIA_PORT_CTRL			(VLV_DISPLAY_BASE + 0x61190)
#define _MIPIC_PORT_CTRL			(VLV_DISPLAY_BASE + 0x61700)
#define MIPI_PORT_CTRL(port)	_MMIO_MIPI(port, _MIPIA_PORT_CTRL, _MIPIC_PORT_CTRL)

 /* BXT port control */
#define _BXT_MIPIA_PORT_CTRL				0x6B0C0
#define _BXT_MIPIC_PORT_CTRL				0x6B8C0
#define BXT_MIPI_PORT_CTRL(tc)	_MMIO_MIPI(tc, _BXT_MIPIA_PORT_CTRL, _BXT_MIPIC_PORT_CTRL)

/* ICL DSI MODE control */
#define _ICL_DSI_IO_MODECTL_0				0x6B094
#define _ICL_DSI_IO_MODECTL_1				0x6B894
#define ICL_DSI_IO_MODECTL(port)	_MMIO_PORT(port,	\
						    _ICL_DSI_IO_MODECTL_0, \
						    _ICL_DSI_IO_MODECTL_1)
#define  COMBO_PHY_MODE_DSI				(1 << 0)

/* TGL DSI Chicken register */
#define _TGL_DSI_CHKN_REG_0			0x6B0C0
#define _TGL_DSI_CHKN_REG_1			0x6B8C0
#define TGL_DSI_CHKN_REG(port)		_MMIO_PORT(port,	\
						    _TGL_DSI_CHKN_REG_0, \
						    _TGL_DSI_CHKN_REG_1)
#define TGL_DSI_CHKN_LSHS_GB_MASK		REG_GENMASK(15, 12)
#define TGL_DSI_CHKN_LSHS_GB(byte_clocks)	REG_FIELD_PREP(TGL_DSI_CHKN_LSHS_GB_MASK, \
							       (byte_clocks))

/* Display Stream Splitter Control */
#define DSS_CTL1				_MMIO(0x67400)
#define  SPLITTER_ENABLE			(1 << 31)
#define  JOINER_ENABLE				(1 << 30)
#define  DUAL_LINK_MODE_INTERLEAVE		(1 << 24)
#define  DUAL_LINK_MODE_FRONTBACK		(0 << 24)
#define  OVERLAP_PIXELS_MASK			(0xf << 16)
#define  OVERLAP_PIXELS(pixels)			((pixels) << 16)
#define  LEFT_DL_BUF_TARGET_DEPTH_MASK		(0xfff << 0)
#define  LEFT_DL_BUF_TARGET_DEPTH(pixels)	((pixels) << 0)
#define  MAX_DL_BUFFER_TARGET_DEPTH		0x5a0

#define DSS_CTL2				_MMIO(0x67404)
#define  LEFT_BRANCH_VDSC_ENABLE		(1 << 31)
#define  RIGHT_BRANCH_VDSC_ENABLE		(1 << 15)
#define  RIGHT_DL_BUF_TARGET_DEPTH_MASK		(0xfff << 0)
#define  RIGHT_DL_BUF_TARGET_DEPTH(pixels)	((pixels) << 0)

#define _ICL_PIPE_DSS_CTL1_PB			0x78200
#define _ICL_PIPE_DSS_CTL1_PC			0x78400
#define ICL_PIPE_DSS_CTL1(pipe)			_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_PIPE_DSS_CTL1_PB, \
							   _ICL_PIPE_DSS_CTL1_PC)
#define  BIG_JOINER_ENABLE			(1 << 29)
#define  MASTER_BIG_JOINER_ENABLE		(1 << 28)
#define  VGA_CENTERING_ENABLE			(1 << 27)
#define  SPLITTER_CONFIGURATION_MASK		REG_GENMASK(26, 25)
#define  SPLITTER_CONFIGURATION_2_SEGMENT	REG_FIELD_PREP(SPLITTER_CONFIGURATION_MASK, 0)
#define  SPLITTER_CONFIGURATION_4_SEGMENT	REG_FIELD_PREP(SPLITTER_CONFIGURATION_MASK, 1)
#define  UNCOMPRESSED_JOINER_MASTER		(1 << 21)
#define  UNCOMPRESSED_JOINER_SLAVE		(1 << 20)

#define _ICL_PIPE_DSS_CTL2_PB			0x78204
#define _ICL_PIPE_DSS_CTL2_PC			0x78404
#define ICL_PIPE_DSS_CTL2(pipe)			_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_PIPE_DSS_CTL2_PB, \
							   _ICL_PIPE_DSS_CTL2_PC)

#define BXT_P_DSI_REGULATOR_CFG			_MMIO(0x160020)
#define  STAP_SELECT					(1 << 0)

#define BXT_P_DSI_REGULATOR_TX_CTRL		_MMIO(0x160054)
#define  HS_IO_CTRL_SELECT				(1 << 0)

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
#define MIPI_TEARING_CTRL(port)			_MMIO_MIPI(port, _MIPIA_TEARING_CTRL, _MIPIC_TEARING_CTRL)
#define  TEARING_EFFECT_DELAY_SHIFT			0
#define  TEARING_EFFECT_DELAY_MASK			(0xffff << 0)

/* XXX: all bits reserved */
#define _MIPIA_AUTOPWG			(VLV_DISPLAY_BASE + 0x611a0)

/* MIPI DSI Controller and D-PHY registers */

#define _MIPIA_DEVICE_READY		(dev_priv->mipi_mmio_base + 0xb000)
#define _MIPIC_DEVICE_READY		(dev_priv->mipi_mmio_base + 0xb800)
#define MIPI_DEVICE_READY(port)		_MMIO_MIPI(port, _MIPIA_DEVICE_READY, _MIPIC_DEVICE_READY)
#define  BUS_POSSESSION					(1 << 3) /* set to give bus to receiver */
#define  ULPS_STATE_MASK				(3 << 1)
#define  ULPS_STATE_ENTER				(2 << 1)
#define  ULPS_STATE_EXIT				(1 << 1)
#define  ULPS_STATE_NORMAL_OPERATION			(0 << 1)
#define  DEVICE_READY					(1 << 0)

#define _MIPIA_INTR_STAT		(dev_priv->mipi_mmio_base + 0xb004)
#define _MIPIC_INTR_STAT		(dev_priv->mipi_mmio_base + 0xb804)
#define MIPI_INTR_STAT(port)		_MMIO_MIPI(port, _MIPIA_INTR_STAT, _MIPIC_INTR_STAT)
#define _MIPIA_INTR_EN			(dev_priv->mipi_mmio_base + 0xb008)
#define _MIPIC_INTR_EN			(dev_priv->mipi_mmio_base + 0xb808)
#define MIPI_INTR_EN(port)		_MMIO_MIPI(port, _MIPIA_INTR_EN, _MIPIC_INTR_EN)
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
#define MIPI_DSI_FUNC_PRG(port)		_MMIO_MIPI(port, _MIPIA_DSI_FUNC_PRG, _MIPIC_DSI_FUNC_PRG)
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
#define  VID_MODE_FORMAT_RGB666_PACKED			(2 << 7)
#define  VID_MODE_FORMAT_RGB666				(3 << 7)
#define  VID_MODE_FORMAT_RGB888				(4 << 7)
#define  CMD_MODE_CHANNEL_NUMBER_SHIFT			5
#define  CMD_MODE_CHANNEL_NUMBER_MASK			(3 << 5)
#define  VID_MODE_CHANNEL_NUMBER_SHIFT			3
#define  VID_MODE_CHANNEL_NUMBER_MASK			(3 << 3)
#define  DATA_LANES_PRG_REG_SHIFT			0
#define  DATA_LANES_PRG_REG_MASK			(7 << 0)

#define _MIPIA_HS_TX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb010)
#define _MIPIC_HS_TX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb810)
#define MIPI_HS_TX_TIMEOUT(port)	_MMIO_MIPI(port, _MIPIA_HS_TX_TIMEOUT, _MIPIC_HS_TX_TIMEOUT)
#define  HIGH_SPEED_TX_TIMEOUT_COUNTER_MASK		0xffffff

#define _MIPIA_LP_RX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb014)
#define _MIPIC_LP_RX_TIMEOUT		(dev_priv->mipi_mmio_base + 0xb814)
#define MIPI_LP_RX_TIMEOUT(port)	_MMIO_MIPI(port, _MIPIA_LP_RX_TIMEOUT, _MIPIC_LP_RX_TIMEOUT)
#define  LOW_POWER_RX_TIMEOUT_COUNTER_MASK		0xffffff

#define _MIPIA_TURN_AROUND_TIMEOUT	(dev_priv->mipi_mmio_base + 0xb018)
#define _MIPIC_TURN_AROUND_TIMEOUT	(dev_priv->mipi_mmio_base + 0xb818)
#define MIPI_TURN_AROUND_TIMEOUT(port)	_MMIO_MIPI(port, _MIPIA_TURN_AROUND_TIMEOUT, _MIPIC_TURN_AROUND_TIMEOUT)
#define  TURN_AROUND_TIMEOUT_MASK			0x3f

#define _MIPIA_DEVICE_RESET_TIMER	(dev_priv->mipi_mmio_base + 0xb01c)
#define _MIPIC_DEVICE_RESET_TIMER	(dev_priv->mipi_mmio_base + 0xb81c)
#define MIPI_DEVICE_RESET_TIMER(port)	_MMIO_MIPI(port, _MIPIA_DEVICE_RESET_TIMER, _MIPIC_DEVICE_RESET_TIMER)
#define  DEVICE_RESET_TIMER_MASK			0xffff

#define _MIPIA_DPI_RESOLUTION		(dev_priv->mipi_mmio_base + 0xb020)
#define _MIPIC_DPI_RESOLUTION		(dev_priv->mipi_mmio_base + 0xb820)
#define MIPI_DPI_RESOLUTION(port)	_MMIO_MIPI(port, _MIPIA_DPI_RESOLUTION, _MIPIC_DPI_RESOLUTION)
#define  VERTICAL_ADDRESS_SHIFT				16
#define  VERTICAL_ADDRESS_MASK				(0xffff << 16)
#define  HORIZONTAL_ADDRESS_SHIFT			0
#define  HORIZONTAL_ADDRESS_MASK			0xffff

#define _MIPIA_DBI_FIFO_THROTTLE	(dev_priv->mipi_mmio_base + 0xb024)
#define _MIPIC_DBI_FIFO_THROTTLE	(dev_priv->mipi_mmio_base + 0xb824)
#define MIPI_DBI_FIFO_THROTTLE(port)	_MMIO_MIPI(port, _MIPIA_DBI_FIFO_THROTTLE, _MIPIC_DBI_FIFO_THROTTLE)
#define  DBI_FIFO_EMPTY_HALF				(0 << 0)
#define  DBI_FIFO_EMPTY_QUARTER				(1 << 0)
#define  DBI_FIFO_EMPTY_7_LOCATIONS			(2 << 0)

/* regs below are bits 15:0 */
#define _MIPIA_HSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb028)
#define _MIPIC_HSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb828)
#define MIPI_HSYNC_PADDING_COUNT(port)	_MMIO_MIPI(port, _MIPIA_HSYNC_PADDING_COUNT, _MIPIC_HSYNC_PADDING_COUNT)

#define _MIPIA_HBP_COUNT		(dev_priv->mipi_mmio_base + 0xb02c)
#define _MIPIC_HBP_COUNT		(dev_priv->mipi_mmio_base + 0xb82c)
#define MIPI_HBP_COUNT(port)		_MMIO_MIPI(port, _MIPIA_HBP_COUNT, _MIPIC_HBP_COUNT)

#define _MIPIA_HFP_COUNT		(dev_priv->mipi_mmio_base + 0xb030)
#define _MIPIC_HFP_COUNT		(dev_priv->mipi_mmio_base + 0xb830)
#define MIPI_HFP_COUNT(port)		_MMIO_MIPI(port, _MIPIA_HFP_COUNT, _MIPIC_HFP_COUNT)

#define _MIPIA_HACTIVE_AREA_COUNT	(dev_priv->mipi_mmio_base + 0xb034)
#define _MIPIC_HACTIVE_AREA_COUNT	(dev_priv->mipi_mmio_base + 0xb834)
#define MIPI_HACTIVE_AREA_COUNT(port)	_MMIO_MIPI(port, _MIPIA_HACTIVE_AREA_COUNT, _MIPIC_HACTIVE_AREA_COUNT)

#define _MIPIA_VSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb038)
#define _MIPIC_VSYNC_PADDING_COUNT	(dev_priv->mipi_mmio_base + 0xb838)
#define MIPI_VSYNC_PADDING_COUNT(port)	_MMIO_MIPI(port, _MIPIA_VSYNC_PADDING_COUNT, _MIPIC_VSYNC_PADDING_COUNT)

#define _MIPIA_VBP_COUNT		(dev_priv->mipi_mmio_base + 0xb03c)
#define _MIPIC_VBP_COUNT		(dev_priv->mipi_mmio_base + 0xb83c)
#define MIPI_VBP_COUNT(port)		_MMIO_MIPI(port, _MIPIA_VBP_COUNT, _MIPIC_VBP_COUNT)

#define _MIPIA_VFP_COUNT		(dev_priv->mipi_mmio_base + 0xb040)
#define _MIPIC_VFP_COUNT		(dev_priv->mipi_mmio_base + 0xb840)
#define MIPI_VFP_COUNT(port)		_MMIO_MIPI(port, _MIPIA_VFP_COUNT, _MIPIC_VFP_COUNT)

#define _MIPIA_HIGH_LOW_SWITCH_COUNT	(dev_priv->mipi_mmio_base + 0xb044)
#define _MIPIC_HIGH_LOW_SWITCH_COUNT	(dev_priv->mipi_mmio_base + 0xb844)
#define MIPI_HIGH_LOW_SWITCH_COUNT(port)	_MMIO_MIPI(port,	_MIPIA_HIGH_LOW_SWITCH_COUNT, _MIPIC_HIGH_LOW_SWITCH_COUNT)

/* regs above are bits 15:0 */

#define _MIPIA_DPI_CONTROL		(dev_priv->mipi_mmio_base + 0xb048)
#define _MIPIC_DPI_CONTROL		(dev_priv->mipi_mmio_base + 0xb848)
#define MIPI_DPI_CONTROL(port)		_MMIO_MIPI(port, _MIPIA_DPI_CONTROL, _MIPIC_DPI_CONTROL)
#define  DPI_LP_MODE					(1 << 6)
#define  BACKLIGHT_OFF					(1 << 5)
#define  BACKLIGHT_ON					(1 << 4)
#define  COLOR_MODE_OFF					(1 << 3)
#define  COLOR_MODE_ON					(1 << 2)
#define  TURN_ON					(1 << 1)
#define  SHUTDOWN					(1 << 0)

#define _MIPIA_DPI_DATA			(dev_priv->mipi_mmio_base + 0xb04c)
#define _MIPIC_DPI_DATA			(dev_priv->mipi_mmio_base + 0xb84c)
#define MIPI_DPI_DATA(port)		_MMIO_MIPI(port, _MIPIA_DPI_DATA, _MIPIC_DPI_DATA)
#define  COMMAND_BYTE_SHIFT				0
#define  COMMAND_BYTE_MASK				(0x3f << 0)

#define _MIPIA_INIT_COUNT		(dev_priv->mipi_mmio_base + 0xb050)
#define _MIPIC_INIT_COUNT		(dev_priv->mipi_mmio_base + 0xb850)
#define MIPI_INIT_COUNT(port)		_MMIO_MIPI(port, _MIPIA_INIT_COUNT, _MIPIC_INIT_COUNT)
#define  MASTER_INIT_TIMER_SHIFT			0
#define  MASTER_INIT_TIMER_MASK				(0xffff << 0)

#define _MIPIA_MAX_RETURN_PKT_SIZE	(dev_priv->mipi_mmio_base + 0xb054)
#define _MIPIC_MAX_RETURN_PKT_SIZE	(dev_priv->mipi_mmio_base + 0xb854)
#define MIPI_MAX_RETURN_PKT_SIZE(port)	_MMIO_MIPI(port, \
			_MIPIA_MAX_RETURN_PKT_SIZE, _MIPIC_MAX_RETURN_PKT_SIZE)
#define  MAX_RETURN_PKT_SIZE_SHIFT			0
#define  MAX_RETURN_PKT_SIZE_MASK			(0x3ff << 0)

#define _MIPIA_VIDEO_MODE_FORMAT	(dev_priv->mipi_mmio_base + 0xb058)
#define _MIPIC_VIDEO_MODE_FORMAT	(dev_priv->mipi_mmio_base + 0xb858)
#define MIPI_VIDEO_MODE_FORMAT(port)	_MMIO_MIPI(port, _MIPIA_VIDEO_MODE_FORMAT, _MIPIC_VIDEO_MODE_FORMAT)
#define  RANDOM_DPI_DISPLAY_RESOLUTION			(1 << 4)
#define  DISABLE_VIDEO_BTA				(1 << 3)
#define  IP_TG_CONFIG					(1 << 2)
#define  VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE		(1 << 0)
#define  VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS		(2 << 0)
#define  VIDEO_MODE_BURST				(3 << 0)

#define _MIPIA_EOT_DISABLE		(dev_priv->mipi_mmio_base + 0xb05c)
#define _MIPIC_EOT_DISABLE		(dev_priv->mipi_mmio_base + 0xb85c)
#define MIPI_EOT_DISABLE(port)		_MMIO_MIPI(port, _MIPIA_EOT_DISABLE, _MIPIC_EOT_DISABLE)
#define  BXT_DEFEATURE_DPI_FIFO_CTR			(1 << 9)
#define  BXT_DPHY_DEFEATURE_EN				(1 << 8)
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
#define MIPI_LP_BYTECLK(port)		_MMIO_MIPI(port, _MIPIA_LP_BYTECLK, _MIPIC_LP_BYTECLK)
#define  LP_BYTECLK_SHIFT				0
#define  LP_BYTECLK_MASK				(0xffff << 0)

#define _MIPIA_TLPX_TIME_COUNT		(dev_priv->mipi_mmio_base + 0xb0a4)
#define _MIPIC_TLPX_TIME_COUNT		(dev_priv->mipi_mmio_base + 0xb8a4)
#define MIPI_TLPX_TIME_COUNT(port)	 _MMIO_MIPI(port, _MIPIA_TLPX_TIME_COUNT, _MIPIC_TLPX_TIME_COUNT)

#define _MIPIA_CLK_LANE_TIMING		(dev_priv->mipi_mmio_base + 0xb098)
#define _MIPIC_CLK_LANE_TIMING		(dev_priv->mipi_mmio_base + 0xb898)
#define MIPI_CLK_LANE_TIMING(port)	 _MMIO_MIPI(port, _MIPIA_CLK_LANE_TIMING, _MIPIC_CLK_LANE_TIMING)

/* bits 31:0 */
#define _MIPIA_LP_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb064)
#define _MIPIC_LP_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb864)
#define MIPI_LP_GEN_DATA(port)		_MMIO_MIPI(port, _MIPIA_LP_GEN_DATA, _MIPIC_LP_GEN_DATA)

/* bits 31:0 */
#define _MIPIA_HS_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb068)
#define _MIPIC_HS_GEN_DATA		(dev_priv->mipi_mmio_base + 0xb868)
#define MIPI_HS_GEN_DATA(port)		_MMIO_MIPI(port, _MIPIA_HS_GEN_DATA, _MIPIC_HS_GEN_DATA)

#define _MIPIA_LP_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb06c)
#define _MIPIC_LP_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb86c)
#define MIPI_LP_GEN_CTRL(port)		_MMIO_MIPI(port, _MIPIA_LP_GEN_CTRL, _MIPIC_LP_GEN_CTRL)
#define _MIPIA_HS_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb070)
#define _MIPIC_HS_GEN_CTRL		(dev_priv->mipi_mmio_base + 0xb870)
#define MIPI_HS_GEN_CTRL(port)		_MMIO_MIPI(port, _MIPIA_HS_GEN_CTRL, _MIPIC_HS_GEN_CTRL)
#define  LONG_PACKET_WORD_COUNT_SHIFT			8
#define  LONG_PACKET_WORD_COUNT_MASK			(0xffff << 8)
#define  SHORT_PACKET_PARAM_SHIFT			8
#define  SHORT_PACKET_PARAM_MASK			(0xffff << 8)
#define  VIRTUAL_CHANNEL_SHIFT				6
#define  VIRTUAL_CHANNEL_MASK				(3 << 6)
#define  DATA_TYPE_SHIFT				0
#define  DATA_TYPE_MASK					(0x3f << 0)
/* data type values, see include/video/mipi_display.h */

#define _MIPIA_GEN_FIFO_STAT		(dev_priv->mipi_mmio_base + 0xb074)
#define _MIPIC_GEN_FIFO_STAT		(dev_priv->mipi_mmio_base + 0xb874)
#define MIPI_GEN_FIFO_STAT(port)	_MMIO_MIPI(port, _MIPIA_GEN_FIFO_STAT, _MIPIC_GEN_FIFO_STAT)
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
#define MIPI_HS_LP_DBI_ENABLE(port)	_MMIO_MIPI(port, _MIPIA_HS_LS_DBI_ENABLE, _MIPIC_HS_LS_DBI_ENABLE)
#define  DBI_HS_LP_MODE_MASK				(1 << 0)
#define  DBI_LP_MODE					(1 << 0)
#define  DBI_HS_MODE					(0 << 0)

#define _MIPIA_DPHY_PARAM		(dev_priv->mipi_mmio_base + 0xb080)
#define _MIPIC_DPHY_PARAM		(dev_priv->mipi_mmio_base + 0xb880)
#define MIPI_DPHY_PARAM(port)		_MMIO_MIPI(port, _MIPIA_DPHY_PARAM, _MIPIC_DPHY_PARAM)
#define  EXIT_ZERO_COUNT_SHIFT				24
#define  EXIT_ZERO_COUNT_MASK				(0x3f << 24)
#define  TRAIL_COUNT_SHIFT				16
#define  TRAIL_COUNT_MASK				(0x1f << 16)
#define  CLK_ZERO_COUNT_SHIFT				8
#define  CLK_ZERO_COUNT_MASK				(0xff << 8)
#define  PREPARE_COUNT_SHIFT				0
#define  PREPARE_COUNT_MASK				(0x3f << 0)

#define _ICL_DSI_T_INIT_MASTER_0	0x6b088
#define _ICL_DSI_T_INIT_MASTER_1	0x6b888
#define ICL_DSI_T_INIT_MASTER(port)	_MMIO_PORT(port,	\
						   _ICL_DSI_T_INIT_MASTER_0,\
						   _ICL_DSI_T_INIT_MASTER_1)

#define _DPHY_CLK_TIMING_PARAM_0	0x162180
#define _DPHY_CLK_TIMING_PARAM_1	0x6c180
#define DPHY_CLK_TIMING_PARAM(port)	_MMIO_PORT(port,	\
						   _DPHY_CLK_TIMING_PARAM_0,\
						   _DPHY_CLK_TIMING_PARAM_1)
#define _DSI_CLK_TIMING_PARAM_0		0x6b080
#define _DSI_CLK_TIMING_PARAM_1		0x6b880
#define DSI_CLK_TIMING_PARAM(port)	_MMIO_PORT(port,	\
						   _DSI_CLK_TIMING_PARAM_0,\
						   _DSI_CLK_TIMING_PARAM_1)
#define  CLK_PREPARE_OVERRIDE		(1 << 31)
#define  CLK_PREPARE(x)		((x) << 28)
#define  CLK_PREPARE_MASK		(0x7 << 28)
#define  CLK_PREPARE_SHIFT		28
#define  CLK_ZERO_OVERRIDE		(1 << 27)
#define  CLK_ZERO(x)			((x) << 20)
#define  CLK_ZERO_MASK			(0xf << 20)
#define  CLK_ZERO_SHIFT		20
#define  CLK_PRE_OVERRIDE		(1 << 19)
#define  CLK_PRE(x)			((x) << 16)
#define  CLK_PRE_MASK			(0x3 << 16)
#define  CLK_PRE_SHIFT			16
#define  CLK_POST_OVERRIDE		(1 << 15)
#define  CLK_POST(x)			((x) << 8)
#define  CLK_POST_MASK			(0x7 << 8)
#define  CLK_POST_SHIFT		8
#define  CLK_TRAIL_OVERRIDE		(1 << 7)
#define  CLK_TRAIL(x)			((x) << 0)
#define  CLK_TRAIL_MASK		(0xf << 0)
#define  CLK_TRAIL_SHIFT		0

#define _DPHY_DATA_TIMING_PARAM_0	0x162184
#define _DPHY_DATA_TIMING_PARAM_1	0x6c184
#define DPHY_DATA_TIMING_PARAM(port)	_MMIO_PORT(port,	\
						   _DPHY_DATA_TIMING_PARAM_0,\
						   _DPHY_DATA_TIMING_PARAM_1)
#define _DSI_DATA_TIMING_PARAM_0	0x6B084
#define _DSI_DATA_TIMING_PARAM_1	0x6B884
#define DSI_DATA_TIMING_PARAM(port)	_MMIO_PORT(port,	\
						   _DSI_DATA_TIMING_PARAM_0,\
						   _DSI_DATA_TIMING_PARAM_1)
#define  HS_PREPARE_OVERRIDE		(1 << 31)
#define  HS_PREPARE(x)			((x) << 24)
#define  HS_PREPARE_MASK		(0x7 << 24)
#define  HS_PREPARE_SHIFT		24
#define  HS_ZERO_OVERRIDE		(1 << 23)
#define  HS_ZERO(x)			((x) << 16)
#define  HS_ZERO_MASK			(0xf << 16)
#define  HS_ZERO_SHIFT			16
#define  HS_TRAIL_OVERRIDE		(1 << 15)
#define  HS_TRAIL(x)			((x) << 8)
#define  HS_TRAIL_MASK			(0x7 << 8)
#define  HS_TRAIL_SHIFT		8
#define  HS_EXIT_OVERRIDE		(1 << 7)
#define  HS_EXIT(x)			((x) << 0)
#define  HS_EXIT_MASK			(0x7 << 0)
#define  HS_EXIT_SHIFT			0

#define _DPHY_TA_TIMING_PARAM_0		0x162188
#define _DPHY_TA_TIMING_PARAM_1		0x6c188
#define DPHY_TA_TIMING_PARAM(port)	_MMIO_PORT(port,	\
						   _DPHY_TA_TIMING_PARAM_0,\
						   _DPHY_TA_TIMING_PARAM_1)
#define _DSI_TA_TIMING_PARAM_0		0x6b098
#define _DSI_TA_TIMING_PARAM_1		0x6b898
#define DSI_TA_TIMING_PARAM(port)	_MMIO_PORT(port,	\
						   _DSI_TA_TIMING_PARAM_0,\
						   _DSI_TA_TIMING_PARAM_1)
#define  TA_SURE_OVERRIDE		(1 << 31)
#define  TA_SURE(x)			((x) << 16)
#define  TA_SURE_MASK			(0x1f << 16)
#define  TA_SURE_SHIFT			16
#define  TA_GO_OVERRIDE		(1 << 15)
#define  TA_GO(x)			((x) << 8)
#define  TA_GO_MASK			(0xf << 8)
#define  TA_GO_SHIFT			8
#define  TA_GET_OVERRIDE		(1 << 7)
#define  TA_GET(x)			((x) << 0)
#define  TA_GET_MASK			(0xf << 0)
#define  TA_GET_SHIFT			0

/* DSI transcoder configuration */
#define _DSI_TRANS_FUNC_CONF_0		0x6b030
#define _DSI_TRANS_FUNC_CONF_1		0x6b830
#define DSI_TRANS_FUNC_CONF(tc)		_MMIO_DSI(tc,	\
						  _DSI_TRANS_FUNC_CONF_0,\
						  _DSI_TRANS_FUNC_CONF_1)
#define  OP_MODE_MASK			(0x3 << 28)
#define  OP_MODE_SHIFT			28
#define  CMD_MODE_NO_GATE		(0x0 << 28)
#define  CMD_MODE_TE_GATE		(0x1 << 28)
#define  VIDEO_MODE_SYNC_EVENT		(0x2 << 28)
#define  VIDEO_MODE_SYNC_PULSE		(0x3 << 28)
#define  TE_SOURCE_GPIO			(1 << 27)
#define  LINK_READY			(1 << 20)
#define  PIX_FMT_MASK			(0x3 << 16)
#define  PIX_FMT_SHIFT			16
#define  PIX_FMT_RGB565			(0x0 << 16)
#define  PIX_FMT_RGB666_PACKED		(0x1 << 16)
#define  PIX_FMT_RGB666_LOOSE		(0x2 << 16)
#define  PIX_FMT_RGB888			(0x3 << 16)
#define  PIX_FMT_RGB101010		(0x4 << 16)
#define  PIX_FMT_RGB121212		(0x5 << 16)
#define  PIX_FMT_COMPRESSED		(0x6 << 16)
#define  BGR_TRANSMISSION		(1 << 15)
#define  PIX_VIRT_CHAN(x)		((x) << 12)
#define  PIX_VIRT_CHAN_MASK		(0x3 << 12)
#define  PIX_VIRT_CHAN_SHIFT		12
#define  PIX_BUF_THRESHOLD_MASK		(0x3 << 10)
#define  PIX_BUF_THRESHOLD_SHIFT	10
#define  PIX_BUF_THRESHOLD_1_4		(0x0 << 10)
#define  PIX_BUF_THRESHOLD_1_2		(0x1 << 10)
#define  PIX_BUF_THRESHOLD_3_4		(0x2 << 10)
#define  PIX_BUF_THRESHOLD_FULL		(0x3 << 10)
#define  CONTINUOUS_CLK_MASK		(0x3 << 8)
#define  CONTINUOUS_CLK_SHIFT		8
#define  CLK_ENTER_LP_AFTER_DATA	(0x0 << 8)
#define  CLK_HS_OR_LP			(0x2 << 8)
#define  CLK_HS_CONTINUOUS		(0x3 << 8)
#define  LINK_CALIBRATION_MASK		(0x3 << 4)
#define  LINK_CALIBRATION_SHIFT		4
#define  CALIBRATION_DISABLED		(0x0 << 4)
#define  CALIBRATION_ENABLED_INITIAL_ONLY	(0x2 << 4)
#define  CALIBRATION_ENABLED_INITIAL_PERIODIC	(0x3 << 4)
#define  BLANKING_PACKET_ENABLE		(1 << 2)
#define  S3D_ORIENTATION_LANDSCAPE	(1 << 1)
#define  EOTP_DISABLED			(1 << 0)

#define _DSI_CMD_RXCTL_0		0x6b0d4
#define _DSI_CMD_RXCTL_1		0x6b8d4
#define DSI_CMD_RXCTL(tc)		_MMIO_DSI(tc,	\
						  _DSI_CMD_RXCTL_0,\
						  _DSI_CMD_RXCTL_1)
#define  READ_UNLOADS_DW		(1 << 16)
#define  RECEIVED_UNASSIGNED_TRIGGER	(1 << 15)
#define  RECEIVED_ACKNOWLEDGE_TRIGGER	(1 << 14)
#define  RECEIVED_TEAR_EFFECT_TRIGGER	(1 << 13)
#define  RECEIVED_RESET_TRIGGER		(1 << 12)
#define  RECEIVED_PAYLOAD_WAS_LOST	(1 << 11)
#define  RECEIVED_CRC_WAS_LOST		(1 << 10)
#define  NUMBER_RX_PLOAD_DW_MASK	(0xff << 0)
#define  NUMBER_RX_PLOAD_DW_SHIFT	0

#define _DSI_CMD_TXCTL_0		0x6b0d0
#define _DSI_CMD_TXCTL_1		0x6b8d0
#define DSI_CMD_TXCTL(tc)		_MMIO_DSI(tc,	\
						  _DSI_CMD_TXCTL_0,\
						  _DSI_CMD_TXCTL_1)
#define  KEEP_LINK_IN_HS		(1 << 24)
#define  FREE_HEADER_CREDIT_MASK	(0x1f << 8)
#define  FREE_HEADER_CREDIT_SHIFT	0x8
#define  FREE_PLOAD_CREDIT_MASK		(0xff << 0)
#define  FREE_PLOAD_CREDIT_SHIFT	0
#define  MAX_HEADER_CREDIT		0x10
#define  MAX_PLOAD_CREDIT		0x40

#define _DSI_CMD_TXHDR_0		0x6b100
#define _DSI_CMD_TXHDR_1		0x6b900
#define DSI_CMD_TXHDR(tc)		_MMIO_DSI(tc,	\
						  _DSI_CMD_TXHDR_0,\
						  _DSI_CMD_TXHDR_1)
#define  PAYLOAD_PRESENT		(1 << 31)
#define  LP_DATA_TRANSFER		(1 << 30)
#define  VBLANK_FENCE			(1 << 29)
#define  PARAM_WC_MASK			(0xffff << 8)
#define  PARAM_WC_LOWER_SHIFT		8
#define  PARAM_WC_UPPER_SHIFT		16
#define  VC_MASK			(0x3 << 6)
#define  VC_SHIFT			6
#define  DT_MASK			(0x3f << 0)
#define  DT_SHIFT			0

#define _DSI_CMD_TXPYLD_0		0x6b104
#define _DSI_CMD_TXPYLD_1		0x6b904
#define DSI_CMD_TXPYLD(tc)		_MMIO_DSI(tc,	\
						  _DSI_CMD_TXPYLD_0,\
						  _DSI_CMD_TXPYLD_1)

#define _DSI_LP_MSG_0			0x6b0d8
#define _DSI_LP_MSG_1			0x6b8d8
#define DSI_LP_MSG(tc)			_MMIO_DSI(tc,	\
						  _DSI_LP_MSG_0,\
						  _DSI_LP_MSG_1)
#define  LPTX_IN_PROGRESS		(1 << 17)
#define  LINK_IN_ULPS			(1 << 16)
#define  LINK_ULPS_TYPE_LP11		(1 << 8)
#define  LINK_ENTER_ULPS		(1 << 0)

/* DSI timeout registers */
#define _DSI_HSTX_TO_0			0x6b044
#define _DSI_HSTX_TO_1			0x6b844
#define DSI_HSTX_TO(tc)			_MMIO_DSI(tc,	\
						  _DSI_HSTX_TO_0,\
						  _DSI_HSTX_TO_1)
#define  HSTX_TIMEOUT_VALUE_MASK	(0xffff << 16)
#define  HSTX_TIMEOUT_VALUE_SHIFT	16
#define  HSTX_TIMEOUT_VALUE(x)		((x) << 16)
#define  HSTX_TIMED_OUT			(1 << 0)

#define _DSI_LPRX_HOST_TO_0		0x6b048
#define _DSI_LPRX_HOST_TO_1		0x6b848
#define DSI_LPRX_HOST_TO(tc)		_MMIO_DSI(tc,	\
						  _DSI_LPRX_HOST_TO_0,\
						  _DSI_LPRX_HOST_TO_1)
#define  LPRX_TIMED_OUT			(1 << 16)
#define  LPRX_TIMEOUT_VALUE_MASK	(0xffff << 0)
#define  LPRX_TIMEOUT_VALUE_SHIFT	0
#define  LPRX_TIMEOUT_VALUE(x)		((x) << 0)

#define _DSI_PWAIT_TO_0			0x6b040
#define _DSI_PWAIT_TO_1			0x6b840
#define DSI_PWAIT_TO(tc)		_MMIO_DSI(tc,	\
						  _DSI_PWAIT_TO_0,\
						  _DSI_PWAIT_TO_1)
#define  PRESET_TIMEOUT_VALUE_MASK	(0xffff << 16)
#define  PRESET_TIMEOUT_VALUE_SHIFT	16
#define  PRESET_TIMEOUT_VALUE(x)	((x) << 16)
#define  PRESPONSE_TIMEOUT_VALUE_MASK	(0xffff << 0)
#define  PRESPONSE_TIMEOUT_VALUE_SHIFT	0
#define  PRESPONSE_TIMEOUT_VALUE(x)	((x) << 0)

#define _DSI_TA_TO_0			0x6b04c
#define _DSI_TA_TO_1			0x6b84c
#define DSI_TA_TO(tc)			_MMIO_DSI(tc,	\
						  _DSI_TA_TO_0,\
						  _DSI_TA_TO_1)
#define  TA_TIMED_OUT			(1 << 16)
#define  TA_TIMEOUT_VALUE_MASK		(0xffff << 0)
#define  TA_TIMEOUT_VALUE_SHIFT		0
#define  TA_TIMEOUT_VALUE(x)		((x) << 0)

/* bits 31:0 */
#define _MIPIA_DBI_BW_CTRL		(dev_priv->mipi_mmio_base + 0xb084)
#define _MIPIC_DBI_BW_CTRL		(dev_priv->mipi_mmio_base + 0xb884)
#define MIPI_DBI_BW_CTRL(port)		_MMIO_MIPI(port, _MIPIA_DBI_BW_CTRL, _MIPIC_DBI_BW_CTRL)

#define _MIPIA_CLK_LANE_SWITCH_TIME_CNT		(dev_priv->mipi_mmio_base + 0xb088)
#define _MIPIC_CLK_LANE_SWITCH_TIME_CNT		(dev_priv->mipi_mmio_base + 0xb888)
#define MIPI_CLK_LANE_SWITCH_TIME_CNT(port)	_MMIO_MIPI(port, _MIPIA_CLK_LANE_SWITCH_TIME_CNT, _MIPIC_CLK_LANE_SWITCH_TIME_CNT)
#define  LP_HS_SSW_CNT_SHIFT				16
#define  LP_HS_SSW_CNT_MASK				(0xffff << 16)
#define  HS_LP_PWR_SW_CNT_SHIFT				0
#define  HS_LP_PWR_SW_CNT_MASK				(0xffff << 0)

#define _MIPIA_STOP_STATE_STALL		(dev_priv->mipi_mmio_base + 0xb08c)
#define _MIPIC_STOP_STATE_STALL		(dev_priv->mipi_mmio_base + 0xb88c)
#define MIPI_STOP_STATE_STALL(port)	_MMIO_MIPI(port, _MIPIA_STOP_STATE_STALL, _MIPIC_STOP_STATE_STALL)
#define  STOP_STATE_STALL_COUNTER_SHIFT			0
#define  STOP_STATE_STALL_COUNTER_MASK			(0xff << 0)

#define _MIPIA_INTR_STAT_REG_1		(dev_priv->mipi_mmio_base + 0xb090)
#define _MIPIC_INTR_STAT_REG_1		(dev_priv->mipi_mmio_base + 0xb890)
#define MIPI_INTR_STAT_REG_1(port)	_MMIO_MIPI(port, _MIPIA_INTR_STAT_REG_1, _MIPIC_INTR_STAT_REG_1)
#define _MIPIA_INTR_EN_REG_1		(dev_priv->mipi_mmio_base + 0xb094)
#define _MIPIC_INTR_EN_REG_1		(dev_priv->mipi_mmio_base + 0xb894)
#define MIPI_INTR_EN_REG_1(port)	_MMIO_MIPI(port, _MIPIA_INTR_EN_REG_1, _MIPIC_INTR_EN_REG_1)
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
#define MIPI_CTRL(port)			_MMIO_MIPI(port, _MIPIA_CTRL, _MIPIC_CTRL)
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

#define  BXT_PIPE_SELECT_SHIFT				7
#define  BXT_PIPE_SELECT_MASK				(7 << 7)
#define  BXT_PIPE_SELECT(pipe)				((pipe) << 7)
#define  GLK_PHY_STATUS_PORT_READY			(1 << 31) /* RO */
#define  GLK_ULPS_NOT_ACTIVE				(1 << 30) /* RO */
#define  GLK_MIPIIO_RESET_RELEASED			(1 << 28)
#define  GLK_CLOCK_LANE_STOP_STATE			(1 << 27) /* RO */
#define  GLK_DATA_LANE_STOP_STATE			(1 << 26) /* RO */
#define  GLK_LP_WAKE					(1 << 22)
#define  GLK_LP11_LOW_PWR_MODE				(1 << 21)
#define  GLK_LP00_LOW_PWR_MODE				(1 << 20)
#define  GLK_FIREWALL_ENABLE				(1 << 16)
#define  BXT_PIXEL_OVERLAP_CNT_MASK			(0xf << 10)
#define  BXT_PIXEL_OVERLAP_CNT_SHIFT			10
#define  BXT_DSC_ENABLE					(1 << 3)
#define  BXT_RGB_FLIP					(1 << 2)
#define  GLK_MIPIIO_PORT_POWERED			(1 << 1) /* RO */
#define  GLK_MIPIIO_ENABLE				(1 << 0)

#define _MIPIA_DATA_ADDRESS		(dev_priv->mipi_mmio_base + 0xb108)
#define _MIPIC_DATA_ADDRESS		(dev_priv->mipi_mmio_base + 0xb908)
#define MIPI_DATA_ADDRESS(port)		_MMIO_MIPI(port, _MIPIA_DATA_ADDRESS, _MIPIC_DATA_ADDRESS)
#define  DATA_MEM_ADDRESS_SHIFT				5
#define  DATA_MEM_ADDRESS_MASK				(0x7ffffff << 5)
#define  DATA_VALID					(1 << 0)

#define _MIPIA_DATA_LENGTH		(dev_priv->mipi_mmio_base + 0xb10c)
#define _MIPIC_DATA_LENGTH		(dev_priv->mipi_mmio_base + 0xb90c)
#define MIPI_DATA_LENGTH(port)		_MMIO_MIPI(port, _MIPIA_DATA_LENGTH, _MIPIC_DATA_LENGTH)
#define  DATA_LENGTH_SHIFT				0
#define  DATA_LENGTH_MASK				(0xfffff << 0)

#define _MIPIA_COMMAND_ADDRESS		(dev_priv->mipi_mmio_base + 0xb110)
#define _MIPIC_COMMAND_ADDRESS		(dev_priv->mipi_mmio_base + 0xb910)
#define MIPI_COMMAND_ADDRESS(port)	_MMIO_MIPI(port, _MIPIA_COMMAND_ADDRESS, _MIPIC_COMMAND_ADDRESS)
#define  COMMAND_MEM_ADDRESS_SHIFT			5
#define  COMMAND_MEM_ADDRESS_MASK			(0x7ffffff << 5)
#define  AUTO_PWG_ENABLE				(1 << 2)
#define  MEMORY_WRITE_DATA_FROM_PIPE_RENDERING		(1 << 1)
#define  COMMAND_VALID					(1 << 0)

#define _MIPIA_COMMAND_LENGTH		(dev_priv->mipi_mmio_base + 0xb114)
#define _MIPIC_COMMAND_LENGTH		(dev_priv->mipi_mmio_base + 0xb914)
#define MIPI_COMMAND_LENGTH(port)	_MMIO_MIPI(port, _MIPIA_COMMAND_LENGTH, _MIPIC_COMMAND_LENGTH)
#define  COMMAND_LENGTH_SHIFT(n)			(8 * (n)) /* n: 0...3 */
#define  COMMAND_LENGTH_MASK(n)				(0xff << (8 * (n)))

#define _MIPIA_READ_DATA_RETURN0	(dev_priv->mipi_mmio_base + 0xb118)
#define _MIPIC_READ_DATA_RETURN0	(dev_priv->mipi_mmio_base + 0xb918)
#define MIPI_READ_DATA_RETURN(port, n) _MMIO(_MIPI(port, _MIPIA_READ_DATA_RETURN0, _MIPIC_READ_DATA_RETURN0) + 4 * (n)) /* n: 0...7 */

#define _MIPIA_READ_DATA_VALID		(dev_priv->mipi_mmio_base + 0xb138)
#define _MIPIC_READ_DATA_VALID		(dev_priv->mipi_mmio_base + 0xb938)
#define MIPI_READ_DATA_VALID(port)	_MMIO_MIPI(port, _MIPIA_READ_DATA_VALID, _MIPIC_READ_DATA_VALID)
#define  READ_DATA_VALID(n)				(1 << (n))

#define GEN12_GSMBASE			_MMIO(0x108100)
#define GEN12_DSMBASE			_MMIO(0x1080C0)

#define XEHP_CLOCK_GATE_DIS		_MMIO(0x101014)
#define   SGSI_SIDECLK_DIS		REG_BIT(17)
#define   SGGI_DIS			REG_BIT(15)
#define   SGR_DIS			REG_BIT(13)

#define _ICL_PHY_MISC_A		0x64C00
#define _ICL_PHY_MISC_B		0x64C04
#define ICL_PHY_MISC(port)	_MMIO_PORT(port, _ICL_PHY_MISC_A, \
						 _ICL_PHY_MISC_B)
#define  ICL_PHY_MISC_MUX_DDID			(1 << 28)
#define  ICL_PHY_MISC_DE_IO_COMP_PWR_DOWN	(1 << 23)
#define  DG2_PHY_DP_TX_ACK_MASK			REG_GENMASK(23, 20)

/* Icelake Display Stream Compression Registers */
#define DSCA_PICTURE_PARAMETER_SET_0		_MMIO(0x6B200)
#define DSCC_PICTURE_PARAMETER_SET_0		_MMIO(0x6BA00)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_0_PB	0x78270
#define _ICL_DSC1_PICTURE_PARAMETER_SET_0_PB	0x78370
#define _ICL_DSC0_PICTURE_PARAMETER_SET_0_PC	0x78470
#define _ICL_DSC1_PICTURE_PARAMETER_SET_0_PC	0x78570
#define ICL_DSC0_PICTURE_PARAMETER_SET_0(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_0_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_0_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_0(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_0_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_0_PC)
#define  DSC_VBR_ENABLE			(1 << 19)
#define  DSC_422_ENABLE			(1 << 18)
#define  DSC_COLOR_SPACE_CONVERSION	(1 << 17)
#define  DSC_BLOCK_PREDICTION		(1 << 16)
#define  DSC_LINE_BUF_DEPTH_SHIFT	12
#define  DSC_BPC_SHIFT			8
#define  DSC_VER_MIN_SHIFT		4
#define  DSC_VER_MAJ			(0x1 << 0)

#define DSCA_PICTURE_PARAMETER_SET_1		_MMIO(0x6B204)
#define DSCC_PICTURE_PARAMETER_SET_1		_MMIO(0x6BA04)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_1_PB	0x78274
#define _ICL_DSC1_PICTURE_PARAMETER_SET_1_PB	0x78374
#define _ICL_DSC0_PICTURE_PARAMETER_SET_1_PC	0x78474
#define _ICL_DSC1_PICTURE_PARAMETER_SET_1_PC	0x78574
#define ICL_DSC0_PICTURE_PARAMETER_SET_1(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_1_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_1_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_1(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_1_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_1_PC)
#define  DSC_BPP(bpp)				((bpp) << 0)

#define DSCA_PICTURE_PARAMETER_SET_2		_MMIO(0x6B208)
#define DSCC_PICTURE_PARAMETER_SET_2		_MMIO(0x6BA08)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_2_PB	0x78278
#define _ICL_DSC1_PICTURE_PARAMETER_SET_2_PB	0x78378
#define _ICL_DSC0_PICTURE_PARAMETER_SET_2_PC	0x78478
#define _ICL_DSC1_PICTURE_PARAMETER_SET_2_PC	0x78578
#define ICL_DSC0_PICTURE_PARAMETER_SET_2(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_2_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_2_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_2(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
					    _ICL_DSC1_PICTURE_PARAMETER_SET_2_PB, \
					    _ICL_DSC1_PICTURE_PARAMETER_SET_2_PC)
#define  DSC_PIC_WIDTH(pic_width)	((pic_width) << 16)
#define  DSC_PIC_HEIGHT(pic_height)	((pic_height) << 0)

#define DSCA_PICTURE_PARAMETER_SET_3		_MMIO(0x6B20C)
#define DSCC_PICTURE_PARAMETER_SET_3		_MMIO(0x6BA0C)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_3_PB	0x7827C
#define _ICL_DSC1_PICTURE_PARAMETER_SET_3_PB	0x7837C
#define _ICL_DSC0_PICTURE_PARAMETER_SET_3_PC	0x7847C
#define _ICL_DSC1_PICTURE_PARAMETER_SET_3_PC	0x7857C
#define ICL_DSC0_PICTURE_PARAMETER_SET_3(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_3_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_3_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_3(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_3_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_3_PC)
#define  DSC_SLICE_WIDTH(slice_width)   ((slice_width) << 16)
#define  DSC_SLICE_HEIGHT(slice_height) ((slice_height) << 0)

#define DSCA_PICTURE_PARAMETER_SET_4		_MMIO(0x6B210)
#define DSCC_PICTURE_PARAMETER_SET_4		_MMIO(0x6BA10)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_4_PB	0x78280
#define _ICL_DSC1_PICTURE_PARAMETER_SET_4_PB	0x78380
#define _ICL_DSC0_PICTURE_PARAMETER_SET_4_PC	0x78480
#define _ICL_DSC1_PICTURE_PARAMETER_SET_4_PC	0x78580
#define ICL_DSC0_PICTURE_PARAMETER_SET_4(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_4_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_4_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_4(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_4_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_4_PC)
#define  DSC_INITIAL_DEC_DELAY(dec_delay)       ((dec_delay) << 16)
#define  DSC_INITIAL_XMIT_DELAY(xmit_delay)     ((xmit_delay) << 0)

#define DSCA_PICTURE_PARAMETER_SET_5		_MMIO(0x6B214)
#define DSCC_PICTURE_PARAMETER_SET_5		_MMIO(0x6BA14)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_5_PB	0x78284
#define _ICL_DSC1_PICTURE_PARAMETER_SET_5_PB	0x78384
#define _ICL_DSC0_PICTURE_PARAMETER_SET_5_PC	0x78484
#define _ICL_DSC1_PICTURE_PARAMETER_SET_5_PC	0x78584
#define ICL_DSC0_PICTURE_PARAMETER_SET_5(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_5_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_5_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_5(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_5_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_5_PC)
#define  DSC_SCALE_DEC_INT(scale_dec)	((scale_dec) << 16)
#define  DSC_SCALE_INC_INT(scale_inc)		((scale_inc) << 0)

#define DSCA_PICTURE_PARAMETER_SET_6		_MMIO(0x6B218)
#define DSCC_PICTURE_PARAMETER_SET_6		_MMIO(0x6BA18)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_6_PB	0x78288
#define _ICL_DSC1_PICTURE_PARAMETER_SET_6_PB	0x78388
#define _ICL_DSC0_PICTURE_PARAMETER_SET_6_PC	0x78488
#define _ICL_DSC1_PICTURE_PARAMETER_SET_6_PC	0x78588
#define ICL_DSC0_PICTURE_PARAMETER_SET_6(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_6_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_6_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_6(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_6_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_6_PC)
#define  DSC_FLATNESS_MAX_QP(max_qp)		((max_qp) << 24)
#define  DSC_FLATNESS_MIN_QP(min_qp)		((min_qp) << 16)
#define  DSC_FIRST_LINE_BPG_OFFSET(offset)	((offset) << 8)
#define  DSC_INITIAL_SCALE_VALUE(value)		((value) << 0)

#define DSCA_PICTURE_PARAMETER_SET_7		_MMIO(0x6B21C)
#define DSCC_PICTURE_PARAMETER_SET_7		_MMIO(0x6BA1C)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_7_PB	0x7828C
#define _ICL_DSC1_PICTURE_PARAMETER_SET_7_PB	0x7838C
#define _ICL_DSC0_PICTURE_PARAMETER_SET_7_PC	0x7848C
#define _ICL_DSC1_PICTURE_PARAMETER_SET_7_PC	0x7858C
#define ICL_DSC0_PICTURE_PARAMETER_SET_7(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							    _ICL_DSC0_PICTURE_PARAMETER_SET_7_PB, \
							    _ICL_DSC0_PICTURE_PARAMETER_SET_7_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_7(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							    _ICL_DSC1_PICTURE_PARAMETER_SET_7_PB, \
							    _ICL_DSC1_PICTURE_PARAMETER_SET_7_PC)
#define  DSC_NFL_BPG_OFFSET(bpg_offset)		((bpg_offset) << 16)
#define  DSC_SLICE_BPG_OFFSET(bpg_offset)	((bpg_offset) << 0)

#define DSCA_PICTURE_PARAMETER_SET_8		_MMIO(0x6B220)
#define DSCC_PICTURE_PARAMETER_SET_8		_MMIO(0x6BA20)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_8_PB	0x78290
#define _ICL_DSC1_PICTURE_PARAMETER_SET_8_PB	0x78390
#define _ICL_DSC0_PICTURE_PARAMETER_SET_8_PC	0x78490
#define _ICL_DSC1_PICTURE_PARAMETER_SET_8_PC	0x78590
#define ICL_DSC0_PICTURE_PARAMETER_SET_8(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_8_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_8_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_8(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_8_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_8_PC)
#define  DSC_INITIAL_OFFSET(initial_offset)		((initial_offset) << 16)
#define  DSC_FINAL_OFFSET(final_offset)			((final_offset) << 0)

#define DSCA_PICTURE_PARAMETER_SET_9		_MMIO(0x6B224)
#define DSCC_PICTURE_PARAMETER_SET_9		_MMIO(0x6BA24)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_9_PB	0x78294
#define _ICL_DSC1_PICTURE_PARAMETER_SET_9_PB	0x78394
#define _ICL_DSC0_PICTURE_PARAMETER_SET_9_PC	0x78494
#define _ICL_DSC1_PICTURE_PARAMETER_SET_9_PC	0x78594
#define ICL_DSC0_PICTURE_PARAMETER_SET_9(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_9_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_9_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_9(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_9_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_9_PC)
#define  DSC_RC_EDGE_FACTOR(rc_edge_fact)	((rc_edge_fact) << 16)
#define  DSC_RC_MODEL_SIZE(rc_model_size)	((rc_model_size) << 0)

#define DSCA_PICTURE_PARAMETER_SET_10		_MMIO(0x6B228)
#define DSCC_PICTURE_PARAMETER_SET_10		_MMIO(0x6BA28)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_10_PB	0x78298
#define _ICL_DSC1_PICTURE_PARAMETER_SET_10_PB	0x78398
#define _ICL_DSC0_PICTURE_PARAMETER_SET_10_PC	0x78498
#define _ICL_DSC1_PICTURE_PARAMETER_SET_10_PC	0x78598
#define ICL_DSC0_PICTURE_PARAMETER_SET_10(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_10_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_10_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_10(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_10_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_10_PC)
#define  DSC_RC_TARGET_OFF_LOW(rc_tgt_off_low)		((rc_tgt_off_low) << 20)
#define  DSC_RC_TARGET_OFF_HIGH(rc_tgt_off_high)	((rc_tgt_off_high) << 16)
#define  DSC_RC_QUANT_INC_LIMIT1(lim)			((lim) << 8)
#define  DSC_RC_QUANT_INC_LIMIT0(lim)			((lim) << 0)

#define DSCA_PICTURE_PARAMETER_SET_11		_MMIO(0x6B22C)
#define DSCC_PICTURE_PARAMETER_SET_11		_MMIO(0x6BA2C)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_11_PB	0x7829C
#define _ICL_DSC1_PICTURE_PARAMETER_SET_11_PB	0x7839C
#define _ICL_DSC0_PICTURE_PARAMETER_SET_11_PC	0x7849C
#define _ICL_DSC1_PICTURE_PARAMETER_SET_11_PC	0x7859C
#define ICL_DSC0_PICTURE_PARAMETER_SET_11(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_11_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_11_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_11(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_11_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_11_PC)

#define DSCA_PICTURE_PARAMETER_SET_12		_MMIO(0x6B260)
#define DSCC_PICTURE_PARAMETER_SET_12		_MMIO(0x6BA60)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_12_PB	0x782A0
#define _ICL_DSC1_PICTURE_PARAMETER_SET_12_PB	0x783A0
#define _ICL_DSC0_PICTURE_PARAMETER_SET_12_PC	0x784A0
#define _ICL_DSC1_PICTURE_PARAMETER_SET_12_PC	0x785A0
#define ICL_DSC0_PICTURE_PARAMETER_SET_12(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_12_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_12_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_12(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_12_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_12_PC)

#define DSCA_PICTURE_PARAMETER_SET_13		_MMIO(0x6B264)
#define DSCC_PICTURE_PARAMETER_SET_13		_MMIO(0x6BA64)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_13_PB	0x782A4
#define _ICL_DSC1_PICTURE_PARAMETER_SET_13_PB	0x783A4
#define _ICL_DSC0_PICTURE_PARAMETER_SET_13_PC	0x784A4
#define _ICL_DSC1_PICTURE_PARAMETER_SET_13_PC	0x785A4
#define ICL_DSC0_PICTURE_PARAMETER_SET_13(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_13_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_13_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_13(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_13_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_13_PC)

#define DSCA_PICTURE_PARAMETER_SET_14		_MMIO(0x6B268)
#define DSCC_PICTURE_PARAMETER_SET_14		_MMIO(0x6BA68)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_14_PB	0x782A8
#define _ICL_DSC1_PICTURE_PARAMETER_SET_14_PB	0x783A8
#define _ICL_DSC0_PICTURE_PARAMETER_SET_14_PC	0x784A8
#define _ICL_DSC1_PICTURE_PARAMETER_SET_14_PC	0x785A8
#define ICL_DSC0_PICTURE_PARAMETER_SET_14(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_14_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_14_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_14(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_14_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_14_PC)

#define DSCA_PICTURE_PARAMETER_SET_15		_MMIO(0x6B26C)
#define DSCC_PICTURE_PARAMETER_SET_15		_MMIO(0x6BA6C)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_15_PB	0x782AC
#define _ICL_DSC1_PICTURE_PARAMETER_SET_15_PB	0x783AC
#define _ICL_DSC0_PICTURE_PARAMETER_SET_15_PC	0x784AC
#define _ICL_DSC1_PICTURE_PARAMETER_SET_15_PC	0x785AC
#define ICL_DSC0_PICTURE_PARAMETER_SET_15(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_15_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_15_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_15(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_15_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_15_PC)

#define DSCA_PICTURE_PARAMETER_SET_16		_MMIO(0x6B270)
#define DSCC_PICTURE_PARAMETER_SET_16		_MMIO(0x6BA70)
#define _ICL_DSC0_PICTURE_PARAMETER_SET_16_PB	0x782B0
#define _ICL_DSC1_PICTURE_PARAMETER_SET_16_PB	0x783B0
#define _ICL_DSC0_PICTURE_PARAMETER_SET_16_PC	0x784B0
#define _ICL_DSC1_PICTURE_PARAMETER_SET_16_PC	0x785B0
#define ICL_DSC0_PICTURE_PARAMETER_SET_16(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_16_PB, \
							   _ICL_DSC0_PICTURE_PARAMETER_SET_16_PC)
#define ICL_DSC1_PICTURE_PARAMETER_SET_16(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_16_PB, \
							   _ICL_DSC1_PICTURE_PARAMETER_SET_16_PC)
#define  DSC_SLICE_ROW_PER_FRAME(slice_row_per_frame)	((slice_row_per_frame) << 20)
#define  DSC_SLICE_PER_LINE(slice_per_line)		((slice_per_line) << 16)
#define  DSC_SLICE_CHUNK_SIZE(slice_chunk_size)		((slice_chunk_size) << 0)

/* Icelake Rate Control Buffer Threshold Registers */
#define DSCA_RC_BUF_THRESH_0			_MMIO(0x6B230)
#define DSCA_RC_BUF_THRESH_0_UDW		_MMIO(0x6B230 + 4)
#define DSCC_RC_BUF_THRESH_0			_MMIO(0x6BA30)
#define DSCC_RC_BUF_THRESH_0_UDW		_MMIO(0x6BA30 + 4)
#define _ICL_DSC0_RC_BUF_THRESH_0_PB		(0x78254)
#define _ICL_DSC0_RC_BUF_THRESH_0_UDW_PB	(0x78254 + 4)
#define _ICL_DSC1_RC_BUF_THRESH_0_PB		(0x78354)
#define _ICL_DSC1_RC_BUF_THRESH_0_UDW_PB	(0x78354 + 4)
#define _ICL_DSC0_RC_BUF_THRESH_0_PC		(0x78454)
#define _ICL_DSC0_RC_BUF_THRESH_0_UDW_PC	(0x78454 + 4)
#define _ICL_DSC1_RC_BUF_THRESH_0_PC		(0x78554)
#define _ICL_DSC1_RC_BUF_THRESH_0_UDW_PC	(0x78554 + 4)
#define ICL_DSC0_RC_BUF_THRESH_0(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
						_ICL_DSC0_RC_BUF_THRESH_0_PB, \
						_ICL_DSC0_RC_BUF_THRESH_0_PC)
#define ICL_DSC0_RC_BUF_THRESH_0_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
						_ICL_DSC0_RC_BUF_THRESH_0_UDW_PB, \
						_ICL_DSC0_RC_BUF_THRESH_0_UDW_PC)
#define ICL_DSC1_RC_BUF_THRESH_0(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
						_ICL_DSC1_RC_BUF_THRESH_0_PB, \
						_ICL_DSC1_RC_BUF_THRESH_0_PC)
#define ICL_DSC1_RC_BUF_THRESH_0_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
						_ICL_DSC1_RC_BUF_THRESH_0_UDW_PB, \
						_ICL_DSC1_RC_BUF_THRESH_0_UDW_PC)

#define DSCA_RC_BUF_THRESH_1			_MMIO(0x6B238)
#define DSCA_RC_BUF_THRESH_1_UDW		_MMIO(0x6B238 + 4)
#define DSCC_RC_BUF_THRESH_1			_MMIO(0x6BA38)
#define DSCC_RC_BUF_THRESH_1_UDW		_MMIO(0x6BA38 + 4)
#define _ICL_DSC0_RC_BUF_THRESH_1_PB		(0x7825C)
#define _ICL_DSC0_RC_BUF_THRESH_1_UDW_PB	(0x7825C + 4)
#define _ICL_DSC1_RC_BUF_THRESH_1_PB		(0x7835C)
#define _ICL_DSC1_RC_BUF_THRESH_1_UDW_PB	(0x7835C + 4)
#define _ICL_DSC0_RC_BUF_THRESH_1_PC		(0x7845C)
#define _ICL_DSC0_RC_BUF_THRESH_1_UDW_PC	(0x7845C + 4)
#define _ICL_DSC1_RC_BUF_THRESH_1_PC		(0x7855C)
#define _ICL_DSC1_RC_BUF_THRESH_1_UDW_PC	(0x7855C + 4)
#define ICL_DSC0_RC_BUF_THRESH_1(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
						_ICL_DSC0_RC_BUF_THRESH_1_PB, \
						_ICL_DSC0_RC_BUF_THRESH_1_PC)
#define ICL_DSC0_RC_BUF_THRESH_1_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
						_ICL_DSC0_RC_BUF_THRESH_1_UDW_PB, \
						_ICL_DSC0_RC_BUF_THRESH_1_UDW_PC)
#define ICL_DSC1_RC_BUF_THRESH_1(pipe)		_MMIO_PIPE((pipe) - PIPE_B, \
						_ICL_DSC1_RC_BUF_THRESH_1_PB, \
						_ICL_DSC1_RC_BUF_THRESH_1_PC)
#define ICL_DSC1_RC_BUF_THRESH_1_UDW(pipe)	_MMIO_PIPE((pipe) - PIPE_B, \
						_ICL_DSC1_RC_BUF_THRESH_1_UDW_PB, \
						_ICL_DSC1_RC_BUF_THRESH_1_UDW_PC)

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

/* This register controls the Display State Buffer (DSB) engines. */
#define _DSBSL_INSTANCE_BASE		0x70B00
#define DSBSL_INSTANCE(pipe, id)	(_DSBSL_INSTANCE_BASE + \
					 (pipe) * 0x1000 + (id) * 0x100)
#define DSB_HEAD(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x0)
#define DSB_TAIL(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x4)
#define DSB_CTRL(pipe, id)		_MMIO(DSBSL_INSTANCE(pipe, id) + 0x8)
#define   DSB_ENABLE			(1 << 31)
#define   DSB_STATUS			(1 << 0)

#define TGL_ROOT_DEVICE_ID		0x9A00
#define TGL_ROOT_DEVICE_MASK		0xFF00
#define TGL_ROOT_DEVICE_SKU_MASK	0xF
#define TGL_ROOT_DEVICE_SKU_ULX		0x2
#define TGL_ROOT_DEVICE_SKU_ULT		0x4

#define CLKREQ_POLICY			_MMIO(0x101038)
#define  CLKREQ_POLICY_MEM_UP_OVRD	REG_BIT(1)

#define CLKGATE_DIS_MISC			_MMIO(0x46534)
#define  CLKGATE_DIS_MISC_DMASC_GATING_DIS	REG_BIT(21)

#endif /* _I915_REG_H_ */
