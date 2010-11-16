/*
 * arch/arm/mach-tegra/include/mach/clock.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_TEGRA_CLOCK_H
#define __MACH_TEGRA_CLOCK_H

#include <linux/list.h>
#include <asm/clkdev.h>

#define DIV_BUS			(1 << 0)
#define DIV_U71			(1 << 1)
#define DIV_U71_FIXED		(1 << 2)
#define DIV_2			(1 << 3)
#define DIV_U16			(1 << 4)
#define PLL_FIXED		(1 << 5)
#define PLL_HAS_CPCON		(1 << 6)
#define MUX			(1 << 7)
#define PLLD			(1 << 8)
#define PERIPH_NO_RESET		(1 << 9)
#define PERIPH_NO_ENB		(1 << 10)
#define PERIPH_EMC_ENB		(1 << 11)
#define PERIPH_MANUAL_RESET	(1 << 12)
#define PLL_ALT_MISC_REG	(1 << 13)
#define PLLU			(1 << 14)
#define ENABLE_ON_INIT		(1 << 28)

struct clk;
struct regulator;

struct dvfs_table {
	unsigned long rate;
	int millivolts;
};

struct dvfs_process_id_table {
	int process_id;
	struct dvfs_table *table;
};


struct dvfs {
	struct regulator *reg;
	struct dvfs_table *table;
	int max_millivolts;

	int process_id_table_length;
	const char *reg_id;
	bool cpu;
	struct dvfs_process_id_table process_id_table[];
};

struct clk_mux_sel {
	struct clk	*input;
	u32		value;
};

struct clk_pll_table {
	unsigned long	input_rate;
	unsigned long	output_rate;
	u16		n;
	u16		m;
	u8		p;
	u8		cpcon;
};

struct clk_ops {
	void		(*init)(struct clk *);
	int		(*enable)(struct clk *);
	void		(*disable)(struct clk *);
	int		(*set_parent)(struct clk *, struct clk *);
	int		(*set_rate)(struct clk *, unsigned long);
	long		(*round_rate)(struct clk *, unsigned long);
};

enum clk_state {
	UNINITIALIZED = 0,
	ON,
	OFF,
};

struct clk {
	/* node for master clocks list */
	struct list_head		node;
	struct list_head		children;	/* list of children */
	struct list_head		sibling;	/* node for children */
#ifdef CONFIG_DEBUG_FS
	struct dentry			*dent;
	struct dentry			*parent_dent;
#endif
	struct clk_ops			*ops;
	struct clk			*parent;
	struct clk_lookup		lookup;
	unsigned long			rate;
	unsigned long			max_rate;
	u32				flags;
	u32				refcnt;
	const char			*name;
	u32				reg;
	u32				reg_shift;
	unsigned int			clk_num;
	enum clk_state			state;
#ifdef CONFIG_DEBUG_FS
	bool				set;
#endif

	/* PLL */
	unsigned long			input_min;
	unsigned long			input_max;
	unsigned long			cf_min;
	unsigned long			cf_max;
	unsigned long			vco_min;
	unsigned long			vco_max;
	const struct clk_pll_table	*pll_table;

	/* DIV */
	u32				div;
	u32				mul;

	/* MUX */
	const struct clk_mux_sel	*inputs;
	u32				sel;
	u32				reg_mask;

	/* Virtual cpu clock */
	struct clk			*main;
	struct clk			*backup;

	struct dvfs			*dvfs;
};


struct clk_duplicate {
	const char *name;
	struct clk_lookup lookup;
};

struct tegra_clk_init_table {
	const char *name;
	const char *parent;
	unsigned long rate;
	bool enabled;
};

void tegra2_init_clocks(void);
void tegra2_periph_reset_deassert(struct clk *c);
void tegra2_periph_reset_assert(struct clk *c);
void clk_init(struct clk *clk);
struct clk *tegra_get_clock_by_name(const char *name);
unsigned long clk_measure_input_freq(void);
void clk_disable_locked(struct clk *c);
int clk_enable_locked(struct clk *c);
int clk_set_parent_locked(struct clk *c, struct clk *parent);
int clk_set_rate_locked(struct clk *c, unsigned long rate);
int clk_reparent(struct clk *c, struct clk *parent);
void tegra_clk_init_from_table(struct tegra_clk_init_table *table);

#endif
