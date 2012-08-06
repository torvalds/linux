/*
 * arch/arm/mach-tegra/include/mach/clock.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2012 NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include <mach/clk.h>

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
#define PLLX                    (1 << 15)
#define MUX_PWM                 (1 << 16)
#define MUX8                    (1 << 17)
#define DIV_U71_UART            (1 << 18)
#define MUX_CLK_OUT             (1 << 19)
#define PLLM                    (1 << 20)
#define DIV_U71_INT             (1 << 21)
#define DIV_U71_IDLE            (1 << 22)
#define ENABLE_ON_INIT		(1 << 28)
#define PERIPH_ON_APB           (1 << 29)

struct clk;

#ifdef CONFIG_COMMON_CLK
struct clk_tegra;
#define to_clk_tegra(_hw) container_of(_hw, struct clk_tegra, hw)
#endif

struct clk_mux_sel {
	struct clk	*input;
	u32		value;
};

struct clk_pll_freq_table {
	unsigned long	input_rate;
	unsigned long	output_rate;
	u16		n;
	u16		m;
	u8		p;
	u8		cpcon;
};

enum clk_state {
	UNINITIALIZED = 0,
	ON,
	OFF,
};

#ifndef CONFIG_COMMON_CLK
struct clk_ops {
	void		(*init)(struct clk *);
	int		(*enable)(struct clk *);
	void		(*disable)(struct clk *);
	int		(*set_parent)(struct clk *, struct clk *);
	int		(*set_rate)(struct clk *, unsigned long);
	long		(*round_rate)(struct clk *, unsigned long);
	void		(*reset)(struct clk *, bool);
	int		(*clk_cfg_ex)(struct clk *,
				enum tegra_clk_ex_param, u32);
};

struct clk {
	/* node for master clocks list */
	struct list_head	node;		/* node for list of all clocks */
	struct clk_lookup	lookup;

#ifdef CONFIG_DEBUG_FS
	struct dentry		*dent;
#endif
	bool			set;
	struct clk_ops		*ops;
	unsigned long		rate;
	unsigned long		max_rate;
	unsigned long		min_rate;
	u32			flags;
	const char		*name;

	u32			refcnt;
	enum clk_state		state;
	struct clk		*parent;
	u32			div;
	u32			mul;

	const struct clk_mux_sel	*inputs;
	u32				reg;
	u32				reg_shift;

	struct list_head		shared_bus_list;

	union {
		struct {
			unsigned int			clk_num;
		} periph;
		struct {
			unsigned long			input_min;
			unsigned long			input_max;
			unsigned long			cf_min;
			unsigned long			cf_max;
			unsigned long			vco_min;
			unsigned long			vco_max;
			const struct clk_pll_freq_table	*freq_table;
			int				lock_delay;
			unsigned long			fixed_rate;
		} pll;
		struct {
			u32				sel;
			u32				reg_mask;
		} mux;
		struct {
			struct clk			*main;
			struct clk			*backup;
		} cpu;
		struct {
			struct list_head		node;
			bool				enabled;
			unsigned long			rate;
		} shared_bus_user;
	} u;

	spinlock_t spinlock;
};

#else

struct clk_tegra {
	/* node for master clocks list */
	struct list_head	node;	/* node for list of all clocks */
	struct clk_lookup	lookup;
	struct clk_hw		hw;

	bool			set;
	unsigned long		fixed_rate;
	unsigned long		max_rate;
	unsigned long		min_rate;
	u32			flags;
	const char		*name;

	enum clk_state		state;
	u32			div;
	u32			mul;

	u32				reg;
	u32				reg_shift;

	struct list_head		shared_bus_list;

	union {
		struct {
			unsigned int			clk_num;
		} periph;
		struct {
			unsigned long			input_min;
			unsigned long			input_max;
			unsigned long			cf_min;
			unsigned long			cf_max;
			unsigned long			vco_min;
			unsigned long			vco_max;
			const struct clk_pll_freq_table	*freq_table;
			int				lock_delay;
			unsigned long			fixed_rate;
		} pll;
		struct {
			u32				sel;
			u32				reg_mask;
		} mux;
		struct {
			struct clk			*main;
			struct clk			*backup;
		} cpu;
		struct {
			struct list_head		node;
			bool				enabled;
			unsigned long			rate;
		} shared_bus_user;
	} u;

	void (*reset)(struct clk_hw *, bool);
	int (*clk_cfg_ex)(struct clk_hw *, enum tegra_clk_ex_param, u32);
};
#endif /* !CONFIG_COMMON_CLK */

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

#ifndef CONFIG_COMMON_CLK
void clk_init(struct clk *clk);
unsigned long clk_get_rate_locked(struct clk *c);
int clk_set_rate_locked(struct clk *c, unsigned long rate);
int clk_reparent(struct clk *c, struct clk *parent);
#endif /* !CONFIG_COMMON_CLK */

void tegra2_init_clocks(void);
void tegra30_init_clocks(void);
struct clk *tegra_get_clock_by_name(const char *name);
void tegra_clk_init_from_table(struct tegra_clk_init_table *table);

#endif
