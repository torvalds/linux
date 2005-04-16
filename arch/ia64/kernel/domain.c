/*
 * arch/ia64/kernel/domain.c
 * Architecture specific sched-domains builder.
 *
 * Copyright (C) 2004 Jesse Barnes
 * Copyright (C) 2004 Silicon Graphics, Inc.
 */

#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/topology.h>
#include <linux/nodemask.h>

#define SD_NODES_PER_DOMAIN 6

#ifdef CONFIG_NUMA
/**
 * find_next_best_node - find the next node to include in a sched_domain
 * @node: node whose sched_domain we're building
 * @used_nodes: nodes already in the sched_domain
 *
 * Find the next node to include in a given scheduling domain.  Simply
 * finds the closest node not already in the @used_nodes map.
 *
 * Should use nodemask_t.
 */
static int __devinit find_next_best_node(int node, unsigned long *used_nodes)
{
	int i, n, val, min_val, best_node = 0;

	min_val = INT_MAX;

	for (i = 0; i < MAX_NUMNODES; i++) {
		/* Start at @node */
		n = (node + i) % MAX_NUMNODES;

		if (!nr_cpus_node(n))
			continue;

		/* Skip already used nodes */
		if (test_bit(n, used_nodes))
			continue;

		/* Simple min distance search */
		val = node_distance(node, n);

		if (val < min_val) {
			min_val = val;
			best_node = n;
		}
	}

	set_bit(best_node, used_nodes);
	return best_node;
}

/**
 * sched_domain_node_span - get a cpumask for a node's sched_domain
 * @node: node whose cpumask we're constructing
 * @size: number of nodes to include in this span
 *
 * Given a node, construct a good cpumask for its sched_domain to span.  It
 * should be one that prevents unnecessary balancing, but also spreads tasks
 * out optimally.
 */
static cpumask_t __devinit sched_domain_node_span(int node)
{
	int i;
	cpumask_t span, nodemask;
	DECLARE_BITMAP(used_nodes, MAX_NUMNODES);

	cpus_clear(span);
	bitmap_zero(used_nodes, MAX_NUMNODES);

	nodemask = node_to_cpumask(node);
	cpus_or(span, span, nodemask);
	set_bit(node, used_nodes);

	for (i = 1; i < SD_NODES_PER_DOMAIN; i++) {
		int next_node = find_next_best_node(node, used_nodes);
		nodemask = node_to_cpumask(next_node);
		cpus_or(span, span, nodemask);
	}

	return span;
}
#endif

/*
 * At the moment, CONFIG_SCHED_SMT is never defined, but leave it in so we
 * can switch it on easily if needed.
 */
#ifdef CONFIG_SCHED_SMT
static DEFINE_PER_CPU(struct sched_domain, cpu_domains);
static struct sched_group sched_group_cpus[NR_CPUS];
static int __devinit cpu_to_cpu_group(int cpu)
{
	return cpu;
}
#endif

static DEFINE_PER_CPU(struct sched_domain, phys_domains);
static struct sched_group sched_group_phys[NR_CPUS];
static int __devinit cpu_to_phys_group(int cpu)
{
#ifdef CONFIG_SCHED_SMT
	return first_cpu(cpu_sibling_map[cpu]);
#else
	return cpu;
#endif
}

#ifdef CONFIG_NUMA
/*
 * The init_sched_build_groups can't handle what we want to do with node
 * groups, so roll our own. Now each node has its own list of groups which
 * gets dynamically allocated.
 */
static DEFINE_PER_CPU(struct sched_domain, node_domains);
static struct sched_group *sched_group_nodes[MAX_NUMNODES];

static DEFINE_PER_CPU(struct sched_domain, allnodes_domains);
static struct sched_group sched_group_allnodes[MAX_NUMNODES];

static int __devinit cpu_to_allnodes_group(int cpu)
{
	return cpu_to_node(cpu);
}
#endif

/*
 * Set up scheduler domains and groups.  Callers must hold the hotplug lock.
 */
void __devinit arch_init_sched_domains(void)
{
	int i;
	cpumask_t cpu_default_map;

	/*
	 * Setup mask for cpus without special case scheduling requirements.
	 * For now this just excludes isolated cpus, but could be used to
	 * exclude other special cases in the future.
	 */
	cpus_complement(cpu_default_map, cpu_isolated_map);
	cpus_and(cpu_default_map, cpu_default_map, cpu_online_map);

	/*
	 * Set up domains. Isolated domains just stay on the dummy domain.
	 */
	for_each_cpu_mask(i, cpu_default_map) {
		int group;
		struct sched_domain *sd = NULL, *p;
		cpumask_t nodemask = node_to_cpumask(cpu_to_node(i));

		cpus_and(nodemask, nodemask, cpu_default_map);

#ifdef CONFIG_NUMA
		if (num_online_cpus()
				> SD_NODES_PER_DOMAIN*cpus_weight(nodemask)) {
			sd = &per_cpu(allnodes_domains, i);
			*sd = SD_ALLNODES_INIT;
			sd->span = cpu_default_map;
			group = cpu_to_allnodes_group(i);
			sd->groups = &sched_group_allnodes[group];
			p = sd;
		} else
			p = NULL;

		sd = &per_cpu(node_domains, i);
		*sd = SD_NODE_INIT;
		sd->span = sched_domain_node_span(cpu_to_node(i));
		sd->parent = p;
		cpus_and(sd->span, sd->span, cpu_default_map);
#endif

		p = sd;
		sd = &per_cpu(phys_domains, i);
		group = cpu_to_phys_group(i);
		*sd = SD_CPU_INIT;
		sd->span = nodemask;
		sd->parent = p;
		sd->groups = &sched_group_phys[group];

#ifdef CONFIG_SCHED_SMT
		p = sd;
		sd = &per_cpu(cpu_domains, i);
		group = cpu_to_cpu_group(i);
		*sd = SD_SIBLING_INIT;
		sd->span = cpu_sibling_map[i];
		cpus_and(sd->span, sd->span, cpu_default_map);
		sd->parent = p;
		sd->groups = &sched_group_cpus[group];
#endif
	}

#ifdef CONFIG_SCHED_SMT
	/* Set up CPU (sibling) groups */
	for_each_cpu_mask(i, cpu_default_map) {
		cpumask_t this_sibling_map = cpu_sibling_map[i];
		cpus_and(this_sibling_map, this_sibling_map, cpu_default_map);
		if (i != first_cpu(this_sibling_map))
			continue;

		init_sched_build_groups(sched_group_cpus, this_sibling_map,
						&cpu_to_cpu_group);
	}
#endif

	/* Set up physical groups */
	for (i = 0; i < MAX_NUMNODES; i++) {
		cpumask_t nodemask = node_to_cpumask(i);

		cpus_and(nodemask, nodemask, cpu_default_map);
		if (cpus_empty(nodemask))
			continue;

		init_sched_build_groups(sched_group_phys, nodemask,
						&cpu_to_phys_group);
	}

#ifdef CONFIG_NUMA
	init_sched_build_groups(sched_group_allnodes, cpu_default_map,
				&cpu_to_allnodes_group);

	for (i = 0; i < MAX_NUMNODES; i++) {
		/* Set up node groups */
		struct sched_group *sg, *prev;
		cpumask_t nodemask = node_to_cpumask(i);
		cpumask_t domainspan;
		cpumask_t covered = CPU_MASK_NONE;
		int j;

		cpus_and(nodemask, nodemask, cpu_default_map);
		if (cpus_empty(nodemask))
			continue;

		domainspan = sched_domain_node_span(i);
		cpus_and(domainspan, domainspan, cpu_default_map);

		sg = kmalloc(sizeof(struct sched_group), GFP_KERNEL);
		sched_group_nodes[i] = sg;
		for_each_cpu_mask(j, nodemask) {
			struct sched_domain *sd;
			sd = &per_cpu(node_domains, j);
			sd->groups = sg;
			if (sd->groups == NULL) {
				/* Turn off balancing if we have no groups */
				sd->flags = 0;
			}
		}
		if (!sg) {
			printk(KERN_WARNING
			"Can not alloc domain group for node %d\n", i);
			continue;
		}
		sg->cpu_power = 0;
		sg->cpumask = nodemask;
		cpus_or(covered, covered, nodemask);
		prev = sg;

		for (j = 0; j < MAX_NUMNODES; j++) {
			cpumask_t tmp, notcovered;
			int n = (i + j) % MAX_NUMNODES;

			cpus_complement(notcovered, covered);
			cpus_and(tmp, notcovered, cpu_default_map);
			cpus_and(tmp, tmp, domainspan);
			if (cpus_empty(tmp))
				break;

			nodemask = node_to_cpumask(n);
			cpus_and(tmp, tmp, nodemask);
			if (cpus_empty(tmp))
				continue;

			sg = kmalloc(sizeof(struct sched_group), GFP_KERNEL);
			if (!sg) {
				printk(KERN_WARNING
				"Can not alloc domain group for node %d\n", j);
				break;
			}
			sg->cpu_power = 0;
			sg->cpumask = tmp;
			cpus_or(covered, covered, tmp);
			prev->next = sg;
			prev = sg;
		}
		prev->next = sched_group_nodes[i];
	}
#endif

	/* Calculate CPU power for physical packages and nodes */
	for_each_cpu_mask(i, cpu_default_map) {
		int power;
		struct sched_domain *sd;
#ifdef CONFIG_SCHED_SMT
		sd = &per_cpu(cpu_domains, i);
		power = SCHED_LOAD_SCALE;
		sd->groups->cpu_power = power;
#endif

		sd = &per_cpu(phys_domains, i);
		power = SCHED_LOAD_SCALE + SCHED_LOAD_SCALE *
				(cpus_weight(sd->groups->cpumask)-1) / 10;
		sd->groups->cpu_power = power;

#ifdef CONFIG_NUMA
		sd = &per_cpu(allnodes_domains, i);
		if (sd->groups) {
			power = SCHED_LOAD_SCALE + SCHED_LOAD_SCALE *
				(cpus_weight(sd->groups->cpumask)-1) / 10;
			sd->groups->cpu_power = power;
		}
#endif
	}

#ifdef CONFIG_NUMA
	for (i = 0; i < MAX_NUMNODES; i++) {
		struct sched_group *sg = sched_group_nodes[i];
		int j;

		if (sg == NULL)
			continue;
next_sg:
		for_each_cpu_mask(j, sg->cpumask) {
			struct sched_domain *sd;
			int power;

			sd = &per_cpu(phys_domains, j);
			if (j != first_cpu(sd->groups->cpumask)) {
				/*
				 * Only add "power" once for each
				 * physical package.
				 */
				continue;
			}
			power = SCHED_LOAD_SCALE + SCHED_LOAD_SCALE *
				(cpus_weight(sd->groups->cpumask)-1) / 10;

			sg->cpu_power += power;
		}
		sg = sg->next;
		if (sg != sched_group_nodes[i])
			goto next_sg;
	}
#endif

	/* Attach the domains */
	for_each_online_cpu(i) {
		struct sched_domain *sd;
#ifdef CONFIG_SCHED_SMT
		sd = &per_cpu(cpu_domains, i);
#else
		sd = &per_cpu(phys_domains, i);
#endif
		cpu_attach_domain(sd, i);
	}
}

void __devinit arch_destroy_sched_domains(void)
{
#ifdef CONFIG_NUMA
	int i;
	for (i = 0; i < MAX_NUMNODES; i++) {
		struct sched_group *oldsg, *sg = sched_group_nodes[i];
		if (sg == NULL)
			continue;
		sg = sg->next;
next_sg:
		oldsg = sg;
		sg = sg->next;
		kfree(oldsg);
		if (oldsg != sched_group_nodes[i])
			goto next_sg;
		sched_group_nodes[i] = NULL;
	}
#endif
}

