/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2016-2018 Xilinx
 */

#ifndef __LINUX_CLK_ZYNQMP_H_
#define __LINUX_CLK_ZYNQMP_H_

#include <linux/spinlock.h>

#include <linux/firmware/xlnx-zynqmp.h>

/* Common Flags */
/* must be gated across rate change */
#define ZYNQMP_CLK_SET_RATE_GATE	BIT(0)
/* must be gated across re-parent */
#define ZYNQMP_CLK_SET_PARENT_GATE	BIT(1)
/* propagate rate change up one level */
#define ZYNQMP_CLK_SET_RATE_PARENT	BIT(2)
/* do not gate even if unused */
#define ZYNQMP_CLK_IGNORE_UNUSED	BIT(3)
/* don't re-parent on rate change */
#define ZYNQMP_CLK_SET_RATE_NO_REPARENT	BIT(7)
/* do not gate, ever */
#define ZYNQMP_CLK_IS_CRITICAL		BIT(11)

/* Type Flags for divider clock */
#define ZYNQMP_CLK_DIVIDER_ONE_BASED		BIT(0)
#define ZYNQMP_CLK_DIVIDER_POWER_OF_TWO		BIT(1)
#define ZYNQMP_CLK_DIVIDER_ALLOW_ZERO		BIT(2)
#define ZYNQMP_CLK_DIVIDER_HIWORD_MASK		BIT(3)
#define ZYNQMP_CLK_DIVIDER_ROUND_CLOSEST	BIT(4)
#define ZYNQMP_CLK_DIVIDER_READ_ONLY		BIT(5)
#define ZYNQMP_CLK_DIVIDER_MAX_AT_ZERO		BIT(6)

/* Type Flags for mux clock */
#define ZYNQMP_CLK_MUX_INDEX_ONE		BIT(0)
#define ZYNQMP_CLK_MUX_INDEX_BIT		BIT(1)
#define ZYNQMP_CLK_MUX_HIWORD_MASK		BIT(2)
#define ZYNQMP_CLK_MUX_READ_ONLY		BIT(3)
#define ZYNQMP_CLK_MUX_ROUND_CLOSEST		BIT(4)
#define ZYNQMP_CLK_MUX_BIG_ENDIAN		BIT(5)

enum topology_type {
	TYPE_INVALID,
	TYPE_MUX,
	TYPE_PLL,
	TYPE_FIXEDFACTOR,
	TYPE_DIV1,
	TYPE_DIV2,
	TYPE_GATE,
};

/**
 * struct clock_topology - Clock topology
 * @type:	Type of topology
 * @flag:	Topology flags
 * @type_flag:	Topology type specific flag
 * @custom_type_flag: Topology type specific custom flag
 */
struct clock_topology {
	u32 type;
	u32 flag;
	u32 type_flag;
	u8 custom_type_flag;
};

unsigned long zynqmp_clk_map_common_ccf_flags(const u32 zynqmp_flag);

struct clk_hw *zynqmp_clk_register_pll(const char *name, u32 clk_id,
				       const char * const *parents,
				       u8 num_parents,
				       const struct clock_topology *nodes);

struct clk_hw *zynqmp_clk_register_gate(const char *name, u32 clk_id,
					const char * const *parents,
					u8 num_parents,
					const struct clock_topology *nodes);

struct clk_hw *zynqmp_clk_register_divider(const char *name,
					   u32 clk_id,
					   const char * const *parents,
					   u8 num_parents,
					   const struct clock_topology *nodes);

struct clk_hw *zynqmp_clk_register_mux(const char *name, u32 clk_id,
				       const char * const *parents,
				       u8 num_parents,
				       const struct clock_topology *nodes);

struct clk_hw *zynqmp_clk_register_fixed_factor(const char *name,
					u32 clk_id,
					const char * const *parents,
					u8 num_parents,
					const struct clock_topology *nodes);

#endif
