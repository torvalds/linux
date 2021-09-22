/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RZ/G2L Clock Pulse Generator
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 *
 */

#ifndef __RENESAS_RZG2L_CPG_H__
#define __RENESAS_RZG2L_CPG_H__

#define CPG_PL2_DDIV		(0x204)
#define CPG_PL3A_DDIV		(0x208)
#define CPG_PL6_ETH_SSEL	(0x418)

/* n = 0/1/2 for PLL1/4/6 */
#define CPG_SAMPLL_CLK1(n)	(0x04 + (16 * n))
#define CPG_SAMPLL_CLK2(n)	(0x08 + (16 * n))

#define PLL146_CONF(n)	(CPG_SAMPLL_CLK1(n) << 22 | CPG_SAMPLL_CLK2(n) << 12)

#define DDIV_PACK(offset, bitpos, size) \
		(((offset) << 20) | ((bitpos) << 12) | ((size) << 8))
#define DIVPL2A		DDIV_PACK(CPG_PL2_DDIV, 0, 3)
#define DIVPL3A		DDIV_PACK(CPG_PL3A_DDIV, 0, 3)
#define DIVPL3B		DDIV_PACK(CPG_PL3A_DDIV, 4, 3)

#define SEL_PLL_PACK(offset, bitpos, size) \
		(((offset) << 20) | ((bitpos) << 12) | ((size) << 8))

#define SEL_PLL6_2	SEL_PLL_PACK(CPG_PL6_ETH_SSEL, 0, 1)

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
	unsigned int conf;
	const struct clk_div_table *dtable;
	const char * const *parent_names;
	int flag;
	int mux_flags;
	int num_parents;
};

enum clk_types {
	/* Generic */
	CLK_TYPE_IN,		/* External Clock Input */
	CLK_TYPE_FF,		/* Fixed Factor Clock */
	CLK_TYPE_SAM_PLL,

	/* Clock with divider */
	CLK_TYPE_DIV,

	/* Clock with clock source selector */
	CLK_TYPE_MUX,
};

#define DEF_TYPE(_name, _id, _type...) \
	{ .name = _name, .id = _id, .type = _type }
#define DEF_BASE(_name, _id, _type, _parent...) \
	DEF_TYPE(_name, _id, _type, .parent = _parent)
#define DEF_SAMPLL(_name, _id, _parent, _conf) \
	DEF_TYPE(_name, _id, CLK_TYPE_SAM_PLL, .parent = _parent, .conf = _conf)
#define DEF_INPUT(_name, _id) \
	DEF_TYPE(_name, _id, CLK_TYPE_IN)
#define DEF_FIXED(_name, _id, _parent, _mult, _div) \
	DEF_BASE(_name, _id, CLK_TYPE_FF, _parent, .div = _div, .mult = _mult)
#define DEF_DIV(_name, _id, _parent, _conf, _dtable, _flag) \
	DEF_TYPE(_name, _id, CLK_TYPE_DIV, .conf = _conf, \
		 .parent = _parent, .dtable = _dtable, .flag = _flag)
#define DEF_MUX(_name, _id, _conf, _parent_names, _num_parents, _flag, \
		_mux_flags) \
	DEF_TYPE(_name, _id, CLK_TYPE_MUX, .conf = _conf, \
		 .parent_names = _parent_names, .num_parents = _num_parents, \
		 .flag = _flag, .mux_flags = _mux_flags)

/**
 * struct rzg2l_mod_clk - Module Clocks definitions
 *
 * @name: handle between common and hardware-specific interfaces
 * @id: clock index in array containing all Core and Module Clocks
 * @parent: id of parent clock
 * @off: register offset
 * @bit: ON/MON bit
 * @is_coupled: flag to indicate coupled clock
 */
struct rzg2l_mod_clk {
	const char *name;
	unsigned int id;
	unsigned int parent;
	u16 off;
	u8 bit;
	bool is_coupled;
};

#define DEF_MOD_BASE(_name, _id, _parent, _off, _bit, _is_coupled)	\
	{ \
		.name = _name, \
		.id = MOD_CLK_BASE + (_id), \
		.parent = (_parent), \
		.off = (_off), \
		.bit = (_bit), \
		.is_coupled = (_is_coupled), \
	}

#define DEF_MOD(_name, _id, _parent, _off, _bit)	\
	DEF_MOD_BASE(_name, _id, _parent, _off, _bit, false)

#define DEF_COUPLED(_name, _id, _parent, _off, _bit)	\
	DEF_MOD_BASE(_name, _id, _parent, _off, _bit, true)

/**
 * struct rzg2l_reset - Reset definitions
 *
 * @off: register offset
 * @bit: reset bit
 */
struct rzg2l_reset {
	u16 off;
	u8 bit;
};

#define DEF_RST(_id, _off, _bit)	\
	[_id] = { \
		.off = (_off), \
		.bit = (_bit) \
	}

/**
 * struct rzg2l_cpg_info - SoC-specific CPG Description
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
 * @crit_mod_clks: Array with Module Clock IDs of critical clocks that
 *                 should not be disabled without a knowledgeable driver
 * @num_crit_mod_clks: Number of entries in crit_mod_clks[]
 */
struct rzg2l_cpg_info {
	/* Core Clocks */
	const struct cpg_core_clk *core_clks;
	unsigned int num_core_clks;
	unsigned int last_dt_core_clk;
	unsigned int num_total_core_clks;

	/* Module Clocks */
	const struct rzg2l_mod_clk *mod_clks;
	unsigned int num_mod_clks;
	unsigned int num_hw_mod_clks;

	/* Resets */
	const struct rzg2l_reset *resets;
	unsigned int num_resets;

	/* Critical Module Clocks that should not be disabled */
	const unsigned int *crit_mod_clks;
	unsigned int num_crit_mod_clks;
};

extern const struct rzg2l_cpg_info r9a07g044_cpg_info;

#endif
