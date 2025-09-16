/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas RZ/V2H(P) Clock Pulse Generator
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#ifndef __RENESAS_RZV2H_CPG_H__
#define __RENESAS_RZV2H_CPG_H__

#include <linux/bitfield.h>
#include <linux/types.h>

/**
 * struct pll - Structure for PLL configuration
 *
 * @offset: STBY register offset
 * @has_clkn: Flag to indicate if CLK1/2 are accessible or not
 */
struct pll {
	unsigned int offset:9;
	unsigned int has_clkn:1;
};

#define PLL_PACK(_offset, _has_clkn) \
	((struct pll){ \
		.offset = _offset, \
		.has_clkn = _has_clkn \
	})

#define PLLCA55		PLL_PACK(0x60, 1)
#define PLLGPU		PLL_PACK(0x120, 1)

/**
 * struct ddiv - Structure for dynamic switching divider
 *
 * @offset: register offset
 * @shift: position of the divider bit
 * @width: width of the divider
 * @monbit: monitor bit in CPG_CLKSTATUS0 register
 * @no_rmw: flag to indicate if the register is read-modify-write
 *        (1: no RMW, 0: RMW)
 */
struct ddiv {
	unsigned int offset:11;
	unsigned int shift:4;
	unsigned int width:4;
	unsigned int monbit:5;
	unsigned int no_rmw:1;
};

/*
 * On RZ/V2H(P), the dynamic divider clock supports up to 19 monitor bits,
 * while on RZ/G3E, it supports up to 16 monitor bits. Use the maximum value
 * `0x1f` to indicate that monitor bits are not supported for static divider
 * clocks.
 */
#define CSDIV_NO_MON	(0x1f)

#define DDIV_PACK(_offset, _shift, _width, _monbit) \
	((struct ddiv){ \
		.offset = _offset, \
		.shift = _shift, \
		.width = _width, \
		.monbit = _monbit \
	})

#define DDIV_PACK_NO_RMW(_offset, _shift, _width, _monbit) \
	((struct ddiv){ \
		.offset = (_offset), \
		.shift = (_shift), \
		.width = (_width), \
		.monbit = (_monbit), \
		.no_rmw = 1 \
	})

/**
 * struct smuxed - Structure for static muxed clocks
 *
 * @offset: register offset
 * @shift: position of the divider field
 * @width: width of the divider field
 */
struct smuxed {
	unsigned int offset:11;
	unsigned int shift:4;
	unsigned int width:4;
};

#define SMUX_PACK(_offset, _shift, _width) \
	((struct smuxed){ \
		.offset = (_offset), \
		.shift = (_shift), \
		.width = (_width), \
	})

/**
 * struct fixed_mod_conf - Structure for fixed module configuration
 *
 * @mon_index: monitor index
 * @mon_bit: monitor bit
 */
struct fixed_mod_conf {
	u8 mon_index;
	u8 mon_bit;
};

#define FIXED_MOD_CONF_PACK(_index, _bit) \
	((struct fixed_mod_conf){ \
		.mon_index = (_index), \
		.mon_bit = (_bit), \
	})

#define CPG_SSEL0		(0x300)
#define CPG_SSEL1		(0x304)
#define CPG_CDDIV0		(0x400)
#define CPG_CDDIV1		(0x404)
#define CPG_CDDIV3		(0x40C)
#define CPG_CDDIV4		(0x410)
#define CPG_CSDIV0		(0x500)

#define CDDIV0_DIVCTL1	DDIV_PACK(CPG_CDDIV0, 4, 3, 1)
#define CDDIV0_DIVCTL2	DDIV_PACK(CPG_CDDIV0, 8, 3, 2)
#define CDDIV1_DIVCTL0	DDIV_PACK(CPG_CDDIV1, 0, 2, 4)
#define CDDIV1_DIVCTL1	DDIV_PACK(CPG_CDDIV1, 4, 2, 5)
#define CDDIV1_DIVCTL2	DDIV_PACK(CPG_CDDIV1, 8, 2, 6)
#define CDDIV1_DIVCTL3	DDIV_PACK(CPG_CDDIV1, 12, 2, 7)
#define CDDIV3_DIVCTL1	DDIV_PACK(CPG_CDDIV3, 4, 3, 13)
#define CDDIV3_DIVCTL2	DDIV_PACK(CPG_CDDIV3, 8, 3, 14)
#define CDDIV3_DIVCTL3	DDIV_PACK(CPG_CDDIV3, 12, 1, 15)
#define CDDIV4_DIVCTL0	DDIV_PACK(CPG_CDDIV4, 0, 1, 16)
#define CDDIV4_DIVCTL1	DDIV_PACK(CPG_CDDIV4, 4, 1, 17)
#define CDDIV4_DIVCTL2	DDIV_PACK(CPG_CDDIV4, 8, 1, 18)

#define CSDIV0_DIVCTL0	DDIV_PACK(CPG_CSDIV0, 0, 2, CSDIV_NO_MON)
#define CSDIV0_DIVCTL1	DDIV_PACK(CPG_CSDIV0, 4, 2, CSDIV_NO_MON)
#define CSDIV0_DIVCTL3	DDIV_PACK_NO_RMW(CPG_CSDIV0, 12, 2, CSDIV_NO_MON)

#define SSEL0_SELCTL2	SMUX_PACK(CPG_SSEL0, 8, 1)
#define SSEL0_SELCTL3	SMUX_PACK(CPG_SSEL0, 12, 1)
#define SSEL1_SELCTL0	SMUX_PACK(CPG_SSEL1, 0, 1)
#define SSEL1_SELCTL1	SMUX_PACK(CPG_SSEL1, 4, 1)
#define SSEL1_SELCTL2	SMUX_PACK(CPG_SSEL1, 8, 1)
#define SSEL1_SELCTL3	SMUX_PACK(CPG_SSEL1, 12, 1)

#define BUS_MSTOP_IDX_MASK	GENMASK(31, 16)
#define BUS_MSTOP_BITS_MASK	GENMASK(15, 0)
#define BUS_MSTOP(idx, mask)	(FIELD_PREP_CONST(BUS_MSTOP_IDX_MASK, (idx)) | \
				 FIELD_PREP_CONST(BUS_MSTOP_BITS_MASK, (mask)))
#define BUS_MSTOP_NONE		GENMASK(31, 0)

#define FIXED_MOD_CONF_XSPI	FIXED_MOD_CONF_PACK(5, 1)

/**
 * Definitions of CPG Core Clocks
 *
 * These include:
 *   - Clock outputs exported to DT
 *   - External input clocks
 *   - Internal CPG clocks
 */
struct cpg_core_clk {
	const char *name;
	unsigned int id;
	unsigned int parent;
	unsigned int div;
	unsigned int mult;
	unsigned int type;
	union {
		unsigned int conf;
		struct ddiv ddiv;
		struct pll pll;
		struct smuxed smux;
		struct fixed_mod_conf fixed_mod;
	} cfg;
	const struct clk_div_table *dtable;
	const char * const *parent_names;
	unsigned int num_parents;
	u8 mux_flags;
	u32 flag;
};

enum clk_types {
	/* Generic */
	CLK_TYPE_IN,		/* External Clock Input */
	CLK_TYPE_FF,		/* Fixed Factor Clock */
	CLK_TYPE_FF_MOD_STATUS,	/* Fixed Factor Clock which can report the status of module clock */
	CLK_TYPE_PLL,
	CLK_TYPE_DDIV,		/* Dynamic Switching Divider */
	CLK_TYPE_SMUX,		/* Static Mux */
};

#define DEF_TYPE(_name, _id, _type...) \
	{ .name = _name, .id = _id, .type = _type }
#define DEF_BASE(_name, _id, _type, _parent...) \
	DEF_TYPE(_name, _id, _type, .parent = _parent)
#define DEF_PLL(_name, _id, _parent, _pll_packed) \
	DEF_TYPE(_name, _id, CLK_TYPE_PLL, .parent = _parent, .cfg.pll = _pll_packed)
#define DEF_INPUT(_name, _id) \
	DEF_TYPE(_name, _id, CLK_TYPE_IN)
#define DEF_FIXED(_name, _id, _parent, _mult, _div) \
	DEF_BASE(_name, _id, CLK_TYPE_FF, _parent, .div = _div, .mult = _mult)
#define DEF_FIXED_MOD_STATUS(_name, _id, _parent, _mult, _div, _gate) \
	DEF_BASE(_name, _id, CLK_TYPE_FF_MOD_STATUS, _parent, .div = _div, \
		 .mult = _mult, .cfg.fixed_mod = _gate)
#define DEF_DDIV(_name, _id, _parent, _ddiv_packed, _dtable) \
	DEF_TYPE(_name, _id, CLK_TYPE_DDIV, \
		.cfg.ddiv = _ddiv_packed, \
		.parent = _parent, \
		.dtable = _dtable, \
		.flag = CLK_DIVIDER_HIWORD_MASK)
#define DEF_CSDIV(_name, _id, _parent, _ddiv_packed, _dtable) \
	DEF_DDIV(_name, _id, _parent, _ddiv_packed, _dtable)
#define DEF_SMUX(_name, _id, _smux_packed, _parent_names) \
	DEF_TYPE(_name, _id, CLK_TYPE_SMUX, \
		 .cfg.smux = _smux_packed, \
		 .parent_names = _parent_names, \
		 .num_parents = ARRAY_SIZE(_parent_names), \
		 .flag = CLK_SET_RATE_PARENT, \
		 .mux_flags = CLK_MUX_HIWORD_MASK)

/**
 * struct rzv2h_mod_clk - Module Clocks definitions
 *
 * @name: handle between common and hardware-specific interfaces
 * @mstop_data: packed data mstop register offset and mask
 * @parent: id of parent clock
 * @critical: flag to indicate the clock is critical
 * @no_pm: flag to indicate PM is not supported
 * @on_index: control register index
 * @on_bit: ON bit
 * @mon_index: monitor register index
 * @mon_bit: monitor bit
 * @ext_clk_mux_index: mux index for external clock source, or -1 if internal
 */
struct rzv2h_mod_clk {
	const char *name;
	u32 mstop_data;
	u16 parent;
	bool critical;
	bool no_pm;
	u8 on_index;
	u8 on_bit;
	s8 mon_index;
	u8 mon_bit;
	s8 ext_clk_mux_index;
};

#define DEF_MOD_BASE(_name, _mstop, _parent, _critical, _no_pm, _onindex, \
		     _onbit, _monindex, _monbit, _ext_clk_mux_index) \
	{ \
		.name = (_name), \
		.mstop_data = (_mstop), \
		.parent = (_parent), \
		.critical = (_critical), \
		.no_pm = (_no_pm), \
		.on_index = (_onindex), \
		.on_bit = (_onbit), \
		.mon_index = (_monindex), \
		.mon_bit = (_monbit), \
		.ext_clk_mux_index = (_ext_clk_mux_index), \
	}

#define DEF_MOD(_name, _parent, _onindex, _onbit, _monindex, _monbit, _mstop) \
	DEF_MOD_BASE(_name, _mstop, _parent, false, false, _onindex, _onbit, _monindex, _monbit, -1)

#define DEF_MOD_CRITICAL(_name, _parent, _onindex, _onbit, _monindex, _monbit, _mstop) \
	DEF_MOD_BASE(_name, _mstop, _parent, true, false, _onindex, _onbit, _monindex, _monbit, -1)

#define DEF_MOD_NO_PM(_name, _parent, _onindex, _onbit, _monindex, _monbit, _mstop) \
	DEF_MOD_BASE(_name, _mstop, _parent, false, true, _onindex, _onbit, _monindex, _monbit, -1)

#define DEF_MOD_MUX_EXTERNAL(_name, _parent, _onindex, _onbit, _monindex, _monbit, _mstop, \
			     _ext_clk_mux_index) \
	DEF_MOD_BASE(_name, _mstop, _parent, false, false, _onindex, _onbit, _monindex, _monbit, \
		     _ext_clk_mux_index)

/**
 * struct rzv2h_reset - Reset definitions
 *
 * @reset_index: reset register index
 * @reset_bit: reset bit
 * @mon_index: monitor register index
 * @mon_bit: monitor bit
 */
struct rzv2h_reset {
	u8 reset_index;
	u8 reset_bit;
	u8 mon_index;
	u8 mon_bit;
};

#define DEF_RST_BASE(_resindex, _resbit, _monindex, _monbit)	\
	{ \
		.reset_index = (_resindex), \
		.reset_bit = (_resbit), \
		.mon_index = (_monindex), \
		.mon_bit = (_monbit), \
	}

#define DEF_RST(_resindex, _resbit, _monindex, _monbit)	\
	DEF_RST_BASE(_resindex, _resbit, _monindex, _monbit)

/**
 * struct rzv2h_cpg_info - SoC-specific CPG Description
 *
 * @core_clks: Array of Core Clock definitions
 * @num_core_clks: Number of entries in core_clks[]
 * @last_dt_core_clk: ID of the last Core Clock exported to DT
 * @num_total_core_clks: Total number of Core Clocks (exported + internal)
 *
 * @mod_clks: Array of Module Clock definitions
 * @num_mod_clks: Number of entries in mod_clks[]
 * @num_hw_mod_clks: Number of Module Clocks supported by the hardware
 *
 * @resets: Array of Module Reset definitions
 * @num_resets: Number of entries in resets[]
 *
 * @num_mstop_bits: Maximum number of MSTOP bits supported, equivalent to the
 *		    number of CPG_BUS_m_MSTOP registers multiplied by 16.
 */
struct rzv2h_cpg_info {
	/* Core Clocks */
	const struct cpg_core_clk *core_clks;
	unsigned int num_core_clks;
	unsigned int last_dt_core_clk;
	unsigned int num_total_core_clks;

	/* Module Clocks */
	const struct rzv2h_mod_clk *mod_clks;
	unsigned int num_mod_clks;
	unsigned int num_hw_mod_clks;

	/* Resets */
	const struct rzv2h_reset *resets;
	unsigned int num_resets;

	unsigned int num_mstop_bits;
};

extern const struct rzv2h_cpg_info r9a09g047_cpg_info;
extern const struct rzv2h_cpg_info r9a09g056_cpg_info;
extern const struct rzv2h_cpg_info r9a09g057_cpg_info;

#endif	/* __RENESAS_RZV2H_CPG_H__ */
