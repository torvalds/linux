/*
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

#ifndef _TEGRA_DVFS_H_
#define _TEGRA_DVFS_H_

#define MAX_DVFS_FREQS	16

struct clk;

struct dvfs {
	/* Used only by tegra2_clock.c */
	const char *clk_name;
	int process_id;
	bool cpu;

	/* Must be initialized before tegra_dvfs_init */
	const char *reg_id;
	int freqs_mult;
	unsigned long freqs[MAX_DVFS_FREQS];
	unsigned long millivolts[MAX_DVFS_FREQS];
	bool auto_dvfs;
	bool higher;

	/* Filled in by tegra_dvfs_init */
	int max_millivolts;
	int num_freqs;
	struct dvfs_reg *dvfs_reg;

	int cur_millivolts;
	unsigned long cur_rate;
	struct list_head node;
	struct list_head debug_node;
	struct list_head reg_node;
};

void lock_dvfs(void);
void unlock_dvfs(void);

void tegra2_init_dvfs(void);
int tegra_enable_dvfs_on_clk(struct clk *c, struct dvfs *d);
int dvfs_debugfs_init(struct dentry *clk_debugfs_root);

#endif
