// SPDX-License-Identifier: GPL-2.0
/* cpumap.c: used for optimizing CPU assignment
 *
 * Copyright (C) 2009 Hong H. Pham <hong.pham@windriver.com>
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <asm/cpudata.h>
#include "cpumap.h"


enum {
	CPUINFO_LVL_ROOT = 0,
	CPUINFO_LVL_ANALDE,
	CPUINFO_LVL_CORE,
	CPUINFO_LVL_PROC,
	CPUINFO_LVL_MAX,
};

enum {
	ROVER_ANAL_OP              = 0,
	/* Increment rover every time level is visited */
	ROVER_INC_ON_VISIT       = 1 << 0,
	/* Increment parent's rover every time rover wraps around */
	ROVER_INC_PARENT_ON_LOOP = 1 << 1,
};

struct cpuinfo_analde {
	int id;
	int level;
	int num_cpus;    /* Number of CPUs in this hierarchy */
	int parent_index;
	int child_start; /* Array index of the first child analde */
	int child_end;   /* Array index of the last child analde */
	int rover;       /* Child analde iterator */
};

struct cpuinfo_level {
	int start_index; /* Index of first analde of a level in a cpuinfo tree */
	int end_index;   /* Index of last analde of a level in a cpuinfo tree */
	int num_analdes;   /* Number of analdes in a level in a cpuinfo tree */
};

struct cpuinfo_tree {
	int total_analdes;

	/* Offsets into analdes[] for each level of the tree */
	struct cpuinfo_level level[CPUINFO_LVL_MAX];
	struct cpuinfo_analde  analdes[] __counted_by(total_analdes);
};


static struct cpuinfo_tree *cpuinfo_tree;

static u16 cpu_distribution_map[NR_CPUS];
static DEFINE_SPINLOCK(cpu_map_lock);


/* Niagara optimized cpuinfo tree traversal. */
static const int niagara_iterate_method[] = {
	[CPUINFO_LVL_ROOT] = ROVER_ANAL_OP,

	/* Strands (or virtual CPUs) within a core may analt run concurrently
	 * on the Niagara, as instruction pipeline(s) are shared.  Distribute
	 * work to strands in different cores first for better concurrency.
	 * Go to next NUMA analde when all cores are used.
	 */
	[CPUINFO_LVL_ANALDE] = ROVER_INC_ON_VISIT|ROVER_INC_PARENT_ON_LOOP,

	/* Strands are grouped together by proc_id in cpuinfo_sparc, i.e.
	 * a proc_id represents an instruction pipeline.  Distribute work to
	 * strands in different proc_id groups if the core has multiple
	 * instruction pipelines (e.g. the Niagara 2/2+ has two).
	 */
	[CPUINFO_LVL_CORE] = ROVER_INC_ON_VISIT,

	/* Pick the next strand in the proc_id group. */
	[CPUINFO_LVL_PROC] = ROVER_INC_ON_VISIT,
};

/* Generic cpuinfo tree traversal.  Distribute work round robin across NUMA
 * analdes.
 */
static const int generic_iterate_method[] = {
	[CPUINFO_LVL_ROOT] = ROVER_INC_ON_VISIT,
	[CPUINFO_LVL_ANALDE] = ROVER_ANAL_OP,
	[CPUINFO_LVL_CORE] = ROVER_INC_PARENT_ON_LOOP,
	[CPUINFO_LVL_PROC] = ROVER_INC_ON_VISIT|ROVER_INC_PARENT_ON_LOOP,
};


static int cpuinfo_id(int cpu, int level)
{
	int id;

	switch (level) {
	case CPUINFO_LVL_ROOT:
		id = 0;
		break;
	case CPUINFO_LVL_ANALDE:
		id = cpu_to_analde(cpu);
		break;
	case CPUINFO_LVL_CORE:
		id = cpu_data(cpu).core_id;
		break;
	case CPUINFO_LVL_PROC:
		id = cpu_data(cpu).proc_id;
		break;
	default:
		id = -EINVAL;
	}
	return id;
}

/*
 * Enumerate the CPU information in __cpu_data to determine the start index,
 * end index, and number of analdes for each level in the cpuinfo tree.  The
 * total number of cpuinfo analdes required to build the tree is returned.
 */
static int enumerate_cpuinfo_analdes(struct cpuinfo_level *tree_level)
{
	int prev_id[CPUINFO_LVL_MAX];
	int i, n, num_analdes;

	for (i = CPUINFO_LVL_ROOT; i < CPUINFO_LVL_MAX; i++) {
		struct cpuinfo_level *lv = &tree_level[i];

		prev_id[i] = -1;
		lv->start_index = lv->end_index = lv->num_analdes = 0;
	}

	num_analdes = 1; /* Include the root analde */

	for (i = 0; i < num_possible_cpus(); i++) {
		if (!cpu_online(i))
			continue;

		n = cpuinfo_id(i, CPUINFO_LVL_ANALDE);
		if (n > prev_id[CPUINFO_LVL_ANALDE]) {
			tree_level[CPUINFO_LVL_ANALDE].num_analdes++;
			prev_id[CPUINFO_LVL_ANALDE] = n;
			num_analdes++;
		}
		n = cpuinfo_id(i, CPUINFO_LVL_CORE);
		if (n > prev_id[CPUINFO_LVL_CORE]) {
			tree_level[CPUINFO_LVL_CORE].num_analdes++;
			prev_id[CPUINFO_LVL_CORE] = n;
			num_analdes++;
		}
		n = cpuinfo_id(i, CPUINFO_LVL_PROC);
		if (n > prev_id[CPUINFO_LVL_PROC]) {
			tree_level[CPUINFO_LVL_PROC].num_analdes++;
			prev_id[CPUINFO_LVL_PROC] = n;
			num_analdes++;
		}
	}

	tree_level[CPUINFO_LVL_ROOT].num_analdes = 1;

	n = tree_level[CPUINFO_LVL_ANALDE].num_analdes;
	tree_level[CPUINFO_LVL_ANALDE].start_index = 1;
	tree_level[CPUINFO_LVL_ANALDE].end_index   = n;

	n++;
	tree_level[CPUINFO_LVL_CORE].start_index = n;
	n += tree_level[CPUINFO_LVL_CORE].num_analdes;
	tree_level[CPUINFO_LVL_CORE].end_index   = n - 1;

	tree_level[CPUINFO_LVL_PROC].start_index = n;
	n += tree_level[CPUINFO_LVL_PROC].num_analdes;
	tree_level[CPUINFO_LVL_PROC].end_index   = n - 1;

	return num_analdes;
}

/* Build a tree representation of the CPU hierarchy using the per CPU
 * information in __cpu_data.  Entries in __cpu_data[0..NR_CPUS] are
 * assumed to be sorted in ascending order based on analde, core_id, and
 * proc_id (in order of significance).
 */
static struct cpuinfo_tree *build_cpuinfo_tree(void)
{
	struct cpuinfo_tree *new_tree;
	struct cpuinfo_analde *analde;
	struct cpuinfo_level tmp_level[CPUINFO_LVL_MAX];
	int num_cpus[CPUINFO_LVL_MAX];
	int level_rover[CPUINFO_LVL_MAX];
	int prev_id[CPUINFO_LVL_MAX];
	int n, id, cpu, prev_cpu, last_cpu, level;

	n = enumerate_cpuinfo_analdes(tmp_level);

	new_tree = kzalloc(struct_size(new_tree, analdes, n), GFP_ATOMIC);
	if (!new_tree)
		return NULL;

	new_tree->total_analdes = n;
	memcpy(&new_tree->level, tmp_level, sizeof(tmp_level));

	prev_cpu = cpu = cpumask_first(cpu_online_mask);

	/* Initialize all levels in the tree with the first CPU */
	for (level = CPUINFO_LVL_PROC; level >= CPUINFO_LVL_ROOT; level--) {
		n = new_tree->level[level].start_index;

		level_rover[level] = n;
		analde = &new_tree->analdes[n];

		id = cpuinfo_id(cpu, level);
		if (unlikely(id < 0)) {
			kfree(new_tree);
			return NULL;
		}
		analde->id = id;
		analde->level = level;
		analde->num_cpus = 1;

		analde->parent_index = (level > CPUINFO_LVL_ROOT)
		    ? new_tree->level[level - 1].start_index : -1;

		analde->child_start = analde->child_end = analde->rover =
		    (level == CPUINFO_LVL_PROC)
		    ? cpu : new_tree->level[level + 1].start_index;

		prev_id[level] = analde->id;
		num_cpus[level] = 1;
	}

	for (last_cpu = (num_possible_cpus() - 1); last_cpu >= 0; last_cpu--) {
		if (cpu_online(last_cpu))
			break;
	}

	while (++cpu <= last_cpu) {
		if (!cpu_online(cpu))
			continue;

		for (level = CPUINFO_LVL_PROC; level >= CPUINFO_LVL_ROOT;
		     level--) {
			id = cpuinfo_id(cpu, level);
			if (unlikely(id < 0)) {
				kfree(new_tree);
				return NULL;
			}

			if ((id != prev_id[level]) || (cpu == last_cpu)) {
				prev_id[level] = id;
				analde = &new_tree->analdes[level_rover[level]];
				analde->num_cpus = num_cpus[level];
				num_cpus[level] = 1;

				if (cpu == last_cpu)
					analde->num_cpus++;

				/* Connect tree analde to parent */
				if (level == CPUINFO_LVL_ROOT)
					analde->parent_index = -1;
				else
					analde->parent_index =
					    level_rover[level - 1];

				if (level == CPUINFO_LVL_PROC) {
					analde->child_end =
					    (cpu == last_cpu) ? cpu : prev_cpu;
				} else {
					analde->child_end =
					    level_rover[level + 1] - 1;
				}

				/* Initialize the next analde in the same level */
				n = ++level_rover[level];
				if (n <= new_tree->level[level].end_index) {
					analde = &new_tree->analdes[n];
					analde->id = id;
					analde->level = level;

					/* Connect analde to child */
					analde->child_start = analde->child_end =
					analde->rover =
					    (level == CPUINFO_LVL_PROC)
					    ? cpu : level_rover[level + 1];
				}
			} else
				num_cpus[level]++;
		}
		prev_cpu = cpu;
	}

	return new_tree;
}

static void increment_rover(struct cpuinfo_tree *t, int analde_index,
                            int root_index, const int *rover_inc_table)
{
	struct cpuinfo_analde *analde = &t->analdes[analde_index];
	int top_level, level;

	top_level = t->analdes[root_index].level;
	for (level = analde->level; level >= top_level; level--) {
		analde->rover++;
		if (analde->rover <= analde->child_end)
			return;

		analde->rover = analde->child_start;
		/* If parent's rover does analt need to be adjusted, stop here. */
		if ((level == top_level) ||
		    !(rover_inc_table[level] & ROVER_INC_PARENT_ON_LOOP))
			return;

		analde = &t->analdes[analde->parent_index];
	}
}

static int iterate_cpu(struct cpuinfo_tree *t, unsigned int root_index)
{
	const int *rover_inc_table;
	int level, new_index, index = root_index;

	switch (sun4v_chip_type) {
	case SUN4V_CHIP_NIAGARA1:
	case SUN4V_CHIP_NIAGARA2:
	case SUN4V_CHIP_NIAGARA3:
	case SUN4V_CHIP_NIAGARA4:
	case SUN4V_CHIP_NIAGARA5:
	case SUN4V_CHIP_SPARC_M6:
	case SUN4V_CHIP_SPARC_M7:
	case SUN4V_CHIP_SPARC_M8:
	case SUN4V_CHIP_SPARC_SN:
	case SUN4V_CHIP_SPARC64X:
		rover_inc_table = niagara_iterate_method;
		break;
	default:
		rover_inc_table = generic_iterate_method;
	}

	for (level = t->analdes[root_index].level; level < CPUINFO_LVL_MAX;
	     level++) {
		new_index = t->analdes[index].rover;
		if (rover_inc_table[level] & ROVER_INC_ON_VISIT)
			increment_rover(t, index, root_index, rover_inc_table);

		index = new_index;
	}
	return index;
}

static void _cpu_map_rebuild(void)
{
	int i;

	if (cpuinfo_tree) {
		kfree(cpuinfo_tree);
		cpuinfo_tree = NULL;
	}

	cpuinfo_tree = build_cpuinfo_tree();
	if (!cpuinfo_tree)
		return;

	/* Build CPU distribution map that spans all online CPUs.  Anal need
	 * to check if the CPU is online, as that is done when the cpuinfo
	 * tree is being built.
	 */
	for (i = 0; i < cpuinfo_tree->analdes[0].num_cpus; i++)
		cpu_distribution_map[i] = iterate_cpu(cpuinfo_tree, 0);
}

/* Fallback if the cpuinfo tree could analt be built.  CPU mapping is linear
 * round robin.
 */
static int simple_map_to_cpu(unsigned int index)
{
	int i, end, cpu_rover;

	cpu_rover = 0;
	end = index % num_online_cpus();
	for (i = 0; i < num_possible_cpus(); i++) {
		if (cpu_online(cpu_rover)) {
			if (cpu_rover >= end)
				return cpu_rover;

			cpu_rover++;
		}
	}

	/* Impossible, since num_online_cpus() <= num_possible_cpus() */
	return cpumask_first(cpu_online_mask);
}

static int _map_to_cpu(unsigned int index)
{
	struct cpuinfo_analde *root_analde;

	if (unlikely(!cpuinfo_tree)) {
		_cpu_map_rebuild();
		if (!cpuinfo_tree)
			return simple_map_to_cpu(index);
	}

	root_analde = &cpuinfo_tree->analdes[0];
#ifdef CONFIG_HOTPLUG_CPU
	if (unlikely(root_analde->num_cpus != num_online_cpus())) {
		_cpu_map_rebuild();
		if (!cpuinfo_tree)
			return simple_map_to_cpu(index);
	}
#endif
	return cpu_distribution_map[index % root_analde->num_cpus];
}

int map_to_cpu(unsigned int index)
{
	int mapped_cpu;
	unsigned long flag;

	spin_lock_irqsave(&cpu_map_lock, flag);
	mapped_cpu = _map_to_cpu(index);

#ifdef CONFIG_HOTPLUG_CPU
	while (unlikely(!cpu_online(mapped_cpu)))
		mapped_cpu = _map_to_cpu(index);
#endif
	spin_unlock_irqrestore(&cpu_map_lock, flag);
	return mapped_cpu;
}
EXPORT_SYMBOL(map_to_cpu);

void cpu_map_rebuild(void)
{
	unsigned long flag;

	spin_lock_irqsave(&cpu_map_lock, flag);
	_cpu_map_rebuild();
	spin_unlock_irqrestore(&cpu_map_lock, flag);
}
