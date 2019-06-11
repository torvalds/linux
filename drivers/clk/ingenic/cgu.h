/*
 * Ingenic SoC CGU driver
 *
 * Copyright (c) 2013-2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DRIVERS_CLK_INGENIC_CGU_H__
#define __DRIVERS_CLK_INGENIC_CGU_H__

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/spinlock.h>

/**
 * struct ingenic_cgu_pll_info - information about a PLL
 * @reg: the offset of the PLL's control register within the CGU
 * @m_shift: the number of bits to shift the multiplier value by (ie. the
 *           index of the lowest bit of the multiplier value in the PLL's
 *           control register)
 * @m_bits: the size of the multiplier field in bits
 * @m_offset: the multiplier value which encodes to 0 in the PLL's control
 *            register
 * @n_shift: the number of bits to shift the divider value by (ie. the
 *           index of the lowest bit of the divider value in the PLL's
 *           control register)
 * @n_bits: the size of the divider field in bits
 * @n_offset: the divider value which encodes to 0 in the PLL's control
 *            register
 * @od_shift: the number of bits to shift the post-VCO divider value by (ie.
 *            the index of the lowest bit of the post-VCO divider value in
 *            the PLL's control register)
 * @od_bits: the size of the post-VCO divider field in bits
 * @od_max: the maximum post-VCO divider value
 * @od_encoding: a pointer to an array mapping post-VCO divider values to
 *               their encoded values in the PLL control register, or -1 for
 *               unsupported values
 * @bypass_bit: the index of the bypass bit in the PLL control register
 * @enable_bit: the index of the enable bit in the PLL control register
 * @stable_bit: the index of the stable bit in the PLL control register
 * @no_bypass_bit: if set, the PLL has no bypass functionality
 */
struct ingenic_cgu_pll_info {
	unsigned reg;
	const s8 *od_encoding;
	u8 m_shift, m_bits, m_offset;
	u8 n_shift, n_bits, n_offset;
	u8 od_shift, od_bits, od_max;
	u8 bypass_bit;
	u8 enable_bit;
	u8 stable_bit;
	bool no_bypass_bit;
};

/**
 * struct ingenic_cgu_mux_info - information about a clock mux
 * @reg: offset of the mux control register within the CGU
 * @shift: number of bits to shift the mux value by (ie. the index of
 *         the lowest bit of the mux value within its control register)
 * @bits: the size of the mux value in bits
 */
struct ingenic_cgu_mux_info {
	unsigned reg;
	u8 shift;
	u8 bits;
};

/**
 * struct ingenic_cgu_div_info - information about a divider
 * @reg: offset of the divider control register within the CGU
 * @shift: number of bits to left shift the divide value by (ie. the index of
 *         the lowest bit of the divide value within its control register)
 * @div: number to divide the divider value by (i.e. if the
 *	 effective divider value is the value written to the register
 *	 multiplied by some constant)
 * @bits: the size of the divide value in bits
 * @ce_bit: the index of the change enable bit within reg, or -1 if there
 *          isn't one
 * @busy_bit: the index of the busy bit within reg, or -1 if there isn't one
 * @stop_bit: the index of the stop bit within reg, or -1 if there isn't one
 * @div_table: optional table to map the value read from the register to the
 *             actual divider value
 */
struct ingenic_cgu_div_info {
	unsigned reg;
	u8 shift;
	u8 div;
	u8 bits;
	s8 ce_bit;
	s8 busy_bit;
	s8 stop_bit;
	const u8 *div_table;
};

/**
 * struct ingenic_cgu_fixdiv_info - information about a fixed divider
 * @div: the divider applied to the parent clock
 */
struct ingenic_cgu_fixdiv_info {
	unsigned div;
};

/**
 * struct ingenic_cgu_gate_info - information about a clock gate
 * @reg: offset of the gate control register within the CGU
 * @bit: offset of the bit in the register that controls the gate
 * @clear_to_gate: if set, the clock is gated when the bit is cleared
 * @delay_us: delay in microseconds after which the clock is considered stable
 */
struct ingenic_cgu_gate_info {
	unsigned reg;
	u8 bit;
	bool clear_to_gate;
	u16 delay_us;
};

/**
 * struct ingenic_cgu_custom_info - information about a custom (SoC) clock
 * @clk_ops: custom clock operation callbacks
 */
struct ingenic_cgu_custom_info {
	const struct clk_ops *clk_ops;
};

/**
 * struct ingenic_cgu_clk_info - information about a clock
 * @name: name of the clock
 * @type: a bitmask formed from CGU_CLK_* values
 * @parents: an array of the indices of potential parents of this clock
 *           within the clock_info array of the CGU, or -1 in entries
 *           which correspond to no valid parent
 * @pll: information valid if type includes CGU_CLK_PLL
 * @gate: information valid if type includes CGU_CLK_GATE
 * @mux: information valid if type includes CGU_CLK_MUX
 * @div: information valid if type includes CGU_CLK_DIV
 * @fixdiv: information valid if type includes CGU_CLK_FIXDIV
 * @custom: information valid if type includes CGU_CLK_CUSTOM
 */
struct ingenic_cgu_clk_info {
	const char *name;

	enum {
		CGU_CLK_NONE		= 0,
		CGU_CLK_EXT		= BIT(0),
		CGU_CLK_PLL		= BIT(1),
		CGU_CLK_GATE		= BIT(2),
		CGU_CLK_MUX		= BIT(3),
		CGU_CLK_MUX_GLITCHFREE	= BIT(4),
		CGU_CLK_DIV		= BIT(5),
		CGU_CLK_FIXDIV		= BIT(6),
		CGU_CLK_CUSTOM		= BIT(7),
	} type;

	int parents[4];

	union {
		struct ingenic_cgu_pll_info pll;

		struct {
			struct ingenic_cgu_gate_info gate;
			struct ingenic_cgu_mux_info mux;
			struct ingenic_cgu_div_info div;
			struct ingenic_cgu_fixdiv_info fixdiv;
		};

		struct ingenic_cgu_custom_info custom;
	};
};

/**
 * struct ingenic_cgu - data about the CGU
 * @np: the device tree node that caused the CGU to be probed
 * @base: the ioremap'ed base address of the CGU registers
 * @clock_info: an array containing information about implemented clocks
 * @clocks: used to provide clocks to DT, allows lookup of struct clk*
 * @lock: lock to be held whilst manipulating CGU registers
 */
struct ingenic_cgu {
	struct device_node *np;
	void __iomem *base;

	const struct ingenic_cgu_clk_info *clock_info;
	struct clk_onecell_data clocks;

	spinlock_t lock;
};

/**
 * struct ingenic_clk - private data for a clock
 * @hw: see Documentation/driver-api/clk.rst
 * @cgu: a pointer to the CGU data
 * @idx: the index of this clock in cgu->clock_info
 */
struct ingenic_clk {
	struct clk_hw hw;
	struct ingenic_cgu *cgu;
	unsigned idx;
};

#define to_ingenic_clk(_hw) container_of(_hw, struct ingenic_clk, hw)

/**
 * ingenic_cgu_new() - create a new CGU instance
 * @clock_info: an array of clock information structures describing the clocks
 *              which are implemented by the CGU
 * @num_clocks: the number of entries in clock_info
 * @np: the device tree node which causes this CGU to be probed
 *
 * Return: a pointer to the CGU instance if initialisation is successful,
 *         otherwise NULL.
 */
struct ingenic_cgu *
ingenic_cgu_new(const struct ingenic_cgu_clk_info *clock_info,
		unsigned num_clocks, struct device_node *np);

/**
 * ingenic_cgu_register_clocks() - Registers the clocks
 * @cgu: pointer to cgu data
 *
 * Register the clocks described by the CGU with the common clock framework.
 *
 * Return: 0 on success or -errno if unsuccesful.
 */
int ingenic_cgu_register_clocks(struct ingenic_cgu *cgu);

#endif /* __DRIVERS_CLK_INGENIC_CGU_H__ */
