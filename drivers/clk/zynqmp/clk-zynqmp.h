/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2016-2018 Xilinx
 */

#ifndef __LINUX_CLK_ZYNQMP_H_
#define __LINUX_CLK_ZYNQMP_H_

#include <linux/spinlock.h>

#include <linux/firmware/xlnx-zynqmp.h>

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
 */
struct clock_topology {
	u32 type;
	u32 flag;
	u32 type_flag;
};

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
