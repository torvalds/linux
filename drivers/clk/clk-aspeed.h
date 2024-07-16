/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Structures used by ASPEED clock drivers
 *
 * Copyright 2019 IBM Corp.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>

struct clk_div_table;
struct regmap;

/**
 * struct aspeed_gate_data - Aspeed gated clocks
 * @clock_idx: bit used to gate this clock in the clock register
 * @reset_idx: bit used to reset this IP in the reset register. -1 if no
 *             reset is required when enabling the clock
 * @name: the clock name
 * @parent_name: the name of the parent clock
 * @flags: standard clock framework flags
 */
struct aspeed_gate_data {
	u8		clock_idx;
	s8		reset_idx;
	const char	*name;
	const char	*parent_name;
	unsigned long	flags;
};

/**
 * struct aspeed_clk_gate - Aspeed specific clk_gate structure
 * @hw:		handle between common and hardware-specific interfaces
 * @reg:	register controlling gate
 * @clock_idx:	bit used to gate this clock in the clock register
 * @reset_idx:	bit used to reset this IP in the reset register. -1 if no
 *		reset is required when enabling the clock
 * @flags:	hardware-specific flags
 * @lock:	register lock
 *
 * Some of the clocks in the Aspeed SoC must be put in reset before enabling.
 * This modified version of clk_gate allows an optional reset bit to be
 * specified.
 */
struct aspeed_clk_gate {
	struct clk_hw	hw;
	struct regmap	*map;
	u8		clock_idx;
	s8		reset_idx;
	u8		flags;
	spinlock_t	*lock;
};

#define to_aspeed_clk_gate(_hw) container_of(_hw, struct aspeed_clk_gate, hw)

/**
 * struct aspeed_reset - Aspeed reset controller
 * @map: regmap to access the containing system controller
 * @rcdev: reset controller device
 */
struct aspeed_reset {
	struct regmap			*map;
	struct reset_controller_dev	rcdev;
};

#define to_aspeed_reset(p) container_of((p), struct aspeed_reset, rcdev)

/**
 * struct aspeed_clk_soc_data - Aspeed SoC specific divisor information
 * @div_table: Common divider lookup table
 * @eclk_div_table: Divider lookup table for ECLK
 * @mac_div_table: Divider lookup table for MAC (Ethernet) clocks
 * @calc_pll: Callback to maculate common PLL settings
 */
struct aspeed_clk_soc_data {
	const struct clk_div_table *div_table;
	const struct clk_div_table *eclk_div_table;
	const struct clk_div_table *mac_div_table;
	struct clk_hw *(*calc_pll)(const char *name, u32 val);
};
