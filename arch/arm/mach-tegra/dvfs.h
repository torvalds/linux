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
struct dvfs_rail;

/*
 * dvfs_relationship between to rails, "from" and "to"
 * when the rail changes, it will call dvfs_rail_update on the rails
 * in the relationship_to list.
 * when determining the voltage to set a rail to, it will consider each
 * rail in the relationship_from list.
 */
struct dvfs_relationship {
	struct dvfs_rail *to;
	struct dvfs_rail *from;
	int (*solve)(struct dvfs_rail *, struct dvfs_rail *);

	struct list_head to_node; /* node in relationship_to list */
	struct list_head from_node; /* node in relationship_from list */
};

struct dvfs_rail {
	const char *reg_id;
	int min_millivolts;
	int max_millivolts;
	int nominal_millivolts;
	int step;
	bool disabled;

	struct list_head node;  /* node in dvfs_rail_list */
	struct list_head dvfs;  /* list head of attached dvfs clocks */
	struct list_head relationships_to;
	struct list_head relationships_from;
	struct regulator *reg;
	int millivolts;
	int new_millivolts;
	bool suspended;
};

struct dvfs {
	/* Used only by tegra2_clock.c */
	const char *clk_name;
	int cpu_process_id;

	/* Must be initialized before tegra_dvfs_init */
	int freqs_mult;
	unsigned long freqs[MAX_DVFS_FREQS];
	const int *millivolts;
	struct dvfs_rail *dvfs_rail;
	bool auto_dvfs;

	/* Filled in by tegra_dvfs_init */
	int max_millivolts;
	int num_freqs;

	int cur_millivolts;
	unsigned long cur_rate;
	struct list_head node;
	struct list_head debug_node;
	struct list_head reg_node;
};

void tegra2_init_dvfs(void);
int tegra_enable_dvfs_on_clk(struct clk *c, struct dvfs *d);
int dvfs_debugfs_init(struct dentry *clk_debugfs_root);
int tegra_dvfs_late_init(void);
int tegra_dvfs_init_rails(struct dvfs_rail *dvfs_rails[], int n);
void tegra_dvfs_add_relationships(struct dvfs_relationship *rels, int n);
void tegra_dvfs_rail_enable(struct dvfs_rail *rail);
void tegra_dvfs_rail_disable(struct dvfs_rail *rail);

#endif
