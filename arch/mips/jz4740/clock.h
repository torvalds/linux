/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SoC clock support
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __MIPS_JZ4740_CLOCK_H__
#define __MIPS_JZ4740_CLOCK_H__

#include <linux/list.h>

struct jz4740_clock_board_data {
	unsigned long ext_rate;
	unsigned long rtc_rate;
};

extern struct jz4740_clock_board_data jz4740_clock_bdata;

void jz4740_clock_suspend(void);
void jz4740_clock_resume(void);

struct clk;

struct clk_ops {
	unsigned long (*get_rate)(struct clk *clk);
	unsigned long (*round_rate)(struct clk *clk, unsigned long rate);
	int (*set_rate)(struct clk *clk, unsigned long rate);
	int (*enable)(struct clk *clk);
	int (*disable)(struct clk *clk);
	int (*is_enabled)(struct clk *clk);

	int (*set_parent)(struct clk *clk, struct clk *parent);

};

struct clk {
	const char *name;
	struct clk *parent;

	uint32_t gate_bit;

	const struct clk_ops *ops;

	struct list_head list;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_entry;
	struct dentry *debugfs_parent_entry;
#endif

};

#define JZ4740_CLK_NOT_GATED ((uint32_t)-1)

int clk_is_enabled(struct clk *clk);

#ifdef CONFIG_DEBUG_FS
void jz4740_clock_debugfs_init(void);
void jz4740_clock_debugfs_add_clk(struct clk *clk);
void jz4740_clock_debugfs_update_parent(struct clk *clk);
#else
static inline void jz4740_clock_debugfs_init(void) {};
static inline void jz4740_clock_debugfs_add_clk(struct clk *clk) {};
static inline void jz4740_clock_debugfs_update_parent(struct clk *clk) {};
#endif

#endif
