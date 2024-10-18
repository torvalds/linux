/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Renesas RZ/V2H(P) Clock Pulse Generator
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */

#ifndef __RENESAS_RZV2H_CPG_H__
#define __RENESAS_RZV2H_CPG_H__

/**
 * struct ddiv - Structure for dynamic switching divider
 *
 * @offset: register offset
 * @shift: position of the divider bit
 * @width: width of the divider
 * @monbit: monitor bit in CPG_CLKSTATUS0 register
 */
struct ddiv {
	unsigned int offset:11;
	unsigned int shift:4;
	unsigned int width:4;
	unsigned int monbit:5;
};

#define DDIV_PACK(_offset, _shift, _width, _monbit) \
	((struct ddiv){ \
		.offset = _offset, \
		.shift = _shift, \
		.width = _width, \
		.monbit = _monbit \
	})

#define CPG_CDDIV0		(0x400)
#define CPG_CDDIV1		(0x404)

#define CDDIV0_DIVCTL2	DDIV_PACK(CPG_CDDIV0, 8, 3, 2)
#define CDDIV1_DIVCTL0	DDIV_PACK(CPG_CDDIV1, 0, 2, 4)
#define CDDIV1_DIVCTL1	DDIV_PACK(CPG_CDDIV1, 4, 2, 5)
#define CDDIV1_DIVCTL2	DDIV_PACK(CPG_CDDIV1, 8, 2, 6)
#define CDDIV1_DIVCTL3	DDIV_PACK(CPG_CDDIV1, 12, 2, 7)

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
	} cfg;
	const struct clk_div_table *dtable;
	u32 flag;
};

enum clk_types {
	/* Generic */
	CLK_TYPE_IN,		/* External Clock Input */
	CLK_TYPE_FF,		/* Fixed Factor Clock */
	CLK_TYPE_PLL,
	CLK_TYPE_DDIV,		/* Dynamic Switching Divider */
};

/* BIT(31) indicates if CLK1/2 are accessible or not */
#define PLL_CONF(n)		(BIT(31) | ((n) & ~GENMASK(31, 16)))
#define PLL_CLK_ACCESS(n)	((n) & BIT(31) ? 1 : 0)
#define PLL_CLK1_OFFSET(n)	((n) & ~GENMASK(31, 16))
#define PLL_CLK2_OFFSET(n)	(((n) & ~GENMASK(31, 16)) + (0x4))

#define DEF_TYPE(_name, _id, _type...) \
	{ .name = _name, .id = _id, .type = _type }
#define DEF_BASE(_name, _id, _type, _parent...) \
	DEF_TYPE(_name, _id, _type, .parent = _parent)
#define DEF_PLL(_name, _id, _parent, _conf) \
	DEF_TYPE(_name, _id, CLK_TYPE_PLL, .parent = _parent, .cfg.conf = _conf)
#define DEF_INPUT(_name, _id) \
	DEF_TYPE(_name, _id, CLK_TYPE_IN)
#define DEF_FIXED(_name, _id, _parent, _mult, _div) \
	DEF_BASE(_name, _id, CLK_TYPE_FF, _parent, .div = _div, .mult = _mult)
#define DEF_DDIV(_name, _id, _parent, _ddiv_packed, _dtable) \
	DEF_TYPE(_name, _id, CLK_TYPE_DDIV, \
		.cfg.ddiv = _ddiv_packed, \
		.parent = _parent, \
		.dtable = _dtable, \
		.flag = CLK_DIVIDER_HIWORD_MASK)

/**
 * struct rzv2h_mod_clk - Module Clocks definitions
 *
 * @name: handle between common and hardware-specific interfaces
 * @parent: id of parent clock
 * @critical: flag to indicate the clock is critical
 * @on_index: control register index
 * @on_bit: ON bit
 * @mon_index: monitor register index
 * @mon_bit: monitor bit
 */
struct rzv2h_mod_clk {
	const char *name;
	u16 parent;
	bool critical;
	u8 on_index;
	u8 on_bit;
	s8 mon_index;
	u8 mon_bit;
};

#define DEF_MOD_BASE(_name, _parent, _critical, _onindex, _onbit, _monindex, _monbit) \
	{ \
		.name = (_name), \
		.parent = (_parent), \
		.critical = (_critical), \
		.on_index = (_onindex), \
		.on_bit = (_onbit), \
		.mon_index = (_monindex), \
		.mon_bit = (_monbit), \
	}

#define DEF_MOD(_name, _parent, _onindex, _onbit, _monindex, _monbit)		\
	DEF_MOD_BASE(_name, _parent, false, _onindex, _onbit, _monindex, _monbit)

#define DEF_MOD_CRITICAL(_name, _parent, _onindex, _onbit, _monindex, _monbit)	\
	DEF_MOD_BASE(_name, _parent, true, _onindex, _onbit, _monindex, _monbit)

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
};

extern const struct rzv2h_cpg_info r9a09g057_cpg_info;

#endif	/* __RENESAS_RZV2H_CPG_H__ */
