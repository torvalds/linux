// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pSeries NUMA support
 *
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 */
#define pr_fmt(fmt) "numa: " fmt

#include <linux/threads.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/export.h>
#include <linux/nodemask.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/pfn.h>
#include <linux/cpuset.h>
#include <linux/node.h>
#include <linux/stop_machine.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <asm/cputhreads.h>
#include <asm/sparsemem.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/topology.h>
#include <asm/firmware.h>
#include <asm/paca.h>
#include <asm/hvcall.h>
#include <asm/setup.h>
#include <asm/vdso.h>
#include <asm/drmem.h>

static int numa_enabled = 1;

static char *cmdline __initdata;

int numa_cpu_lookup_table[NR_CPUS];
cpumask_var_t node_to_cpumask_map[MAX_NUMNODES];
struct pglist_data *node_data[MAX_NUMNODES];

EXPORT_SYMBOL(numa_cpu_lookup_table);
EXPORT_SYMBOL(node_to_cpumask_map);
EXPORT_SYMBOL(node_data);

static int primary_domain_index;
static int n_mem_addr_cells, n_mem_size_cells;

#define FORM0_AFFINITY 0
#define FORM1_AFFINITY 1
#define FORM2_AFFINITY 2
static int affinity_form;

#define MAX_DISTANCE_REF_POINTS 4
static int distance_ref_points_depth;
static const __be32 *distance_ref_points;
static int distance_lookup_table[MAX_NUMNODES][MAX_DISTANCE_REF_POINTS];
static int numa_distance_table[MAX_NUMNODES][MAX_NUMNODES] = {
	[0 ... MAX_NUMNODES - 1] = { [0 ... MAX_NUMNODES - 1] = -1 }
};
static int numa_id_index_table[MAX_NUMNODES] = { [0 ... MAX_NUMNODES - 1] = NUMA_NO_NODE };

/*
 * Allocate node_to_cpumask_map based on number of available nodes
 * Requires node_possible_map to be valid.
 *
 * Note: cpumask_of_node() is not valid until after this is done.
 */
static void __init setup_node_to_cpumask_map(void)
{
	unsigned int node;

	/* setup nr_node_ids if not done yet */
	if (nr_node_ids == MAX_NUMNODES)
		setup_nr_node_ids();

	/* allocate the map */
	for_each_node(node)
		alloc_bootmem_cpumask_var(&node_to_cpumask_map[node]);

	/* cpumask_of_node() will now work */
	pr_debug("Node to cpumask map for %u nodes\n", nr_node_ids);
}

static int __init fake_numa_create_new_node(unsigned long end_pfn,
						unsigned int *nid)
{
	unsigned long long mem;
	char *p = cmdline;
	static unsigned int fake_nid;
	static unsigned long long curr_boundary;

	/*
	 * Modify node id, iff we started creating NUMA nodes
	 * We want to continue from where we left of the last time
	 */
	if (fake_nid)
		*nid = fake_nid;
	/*
	 * In case there are no more arguments to parse, the
	 * node_id should be the same as the last fake node id
	 * (we've handled this above).
	 */
	if (!p)
		return 0;

	mem = memparse(p, &p);
	if (!mem)
		return 0;

	if (mem < curr_boundary)
		return 0;

	curr_boundary = mem;

	if ((end_pfn << PAGE_SHIFT) > mem) {
		/*
		 * Skip commas and spaces
		 */
		while (*p == ',' || *p == ' ' || *p == '\t')
			p++;

		cmdline = p;
		fake_nid++;
		*nid = fake_nid;
		pr_debug("created new fake_node with id %d\n", fake_nid);
		return 1;
	}
	return 0;
}

static void reset_numa_cpu_lookup_table(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		numa_cpu_lookup_table[cpu] = -1;
}

static void map_cpu_to_node(int cpu, int node)
{
	update_numa_cpu_lookup_table(cpu, node);

	pr_debug("adding cpu %d to node %d\n", cpu, node);

	if (!(cpumask_test_cpu(cpu, node_to_cpumask_map[node])))
		cpumask_set_cpu(cpu, node_to_cpumask_map[node]);
}

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_PPC_SPLPAR)
static void unmap_cpu_from_node(unsigned long cpu)
{
	int node = numa_cpu_lookup_table[cpu];

	pr_debug("removing cpu %lu from node %d\n", cpu, node);

	if (cpumask_test_cpu(cpu, node_to_cpumask_map[node])) {
		cpumask_clear_cpu(cpu, node_to_cpumask_map[node]);
	} else {
		pr_warn("Warning: cpu %lu not found in node %d\n", cpu, node);
	}
}
#endif /* CONFIG_HOTPLUG_CPU || CONFIG_PPC_SPLPAR */

static int __associativity_to_nid(const __be32 *associativity,
				  int max_array_sz)
{
	int nid;
	/*
	 * primary_domain_index is 1 based array index.
	 */
	int index = primary_domain_index  - 1;

	if (!numa_enabled || index >= max_array_sz)
		return NUMA_NO_NODE;

	nid = of_read_number(&associativity[index], 1);

	/* POWER4 LPAR uses 0xffff as invalid node */
	if (nid == 0xffff || nid >= nr_node_ids)
		nid = NUMA_NO_NODE;
	return nid;
}
/*
 * Returns nid in the range [0..nr_node_ids], or -1 if no useful NUMA
 * info is found.
 */
static int associativity_to_nid(const __be32 *associativity)
{
	int array_sz = of_read_number(associativity, 1);

	/* Skip the first element in the associativity array */
	return __associativity_to_nid((associativity + 1), array_sz);
}

static int __cpu_form2_relative_distance(__be32 *cpu1_assoc, __be32 *cpu2_assoc)
{
	int dist;
	int node1, node2;

	node1 = associativity_to_nid(cpu1_assoc);
	node2 = associativity_to_nid(cpu2_assoc);

	dist = numa_distance_table[node1][node2];
	if (dist <= LOCAL_DISTANCE)
		return 0;
	else if (dist <= REMOTE_DISTANCE)
		return 1;
	else
		return 2;
}

static int __cpu_form1_relative_distance(__be32 *cpu1_assoc, __be32 *cpu2_assoc)
{
	int dist = 0;

	int i, index;

	for (i = 0; i < distance_ref_points_depth; i++) {
		index = be32_to_cpu(distance_ref_points[i]);
		if (cpu1_assoc[index] == cpu2_assoc[index])
			break;
		dist++;
	}

	return dist;
}

int cpu_relative_distance(__be32 *cpu1_assoc, __be32 *cpu2_assoc)
{
	/* We should not get called with FORM0 */
	VM_WARN_ON(affinity_form == FORM0_AFFINITY);
	if (affinity_form == FORM1_AFFINITY)
		return __cpu_form1_relative_distance(cpu1_assoc, cpu2_assoc);
	return __cpu_form2_relative_distance(cpu1_assoc, cpu2_assoc);
}

/* must hold reference to node during call */
static const __be32 *of_get_associativity(struct device_node *dev)
{
	return of_get_property(dev, "ibm,associativity", NULL);
}

int __node_distance(int a, int b)
{
	int i;
	int distance = LOCAL_DISTANCE;

	if (affinity_form == FORM2_AFFINITY)
		return numa_distance_table[a][b];
	else if (affinity_form == FORM0_AFFINITY)
		return ((a == b) ? LOCAL_DISTANCE : REMOTE_DISTANCE);

	for (i = 0; i < distance_ref_points_depth; i++) {
		if (distance_lookup_table[a][i] == distance_lookup_table[b][i])
			break;

		/* Double the distance for each NUMA level */
		distance *= 2;
	}

	return distance;
}
EXPORT_SYMBOL(__node_distance);

/* Returns the nid associated with the given device tree node,
 * or -1 if not found.
 */
static int of_node_to_nid_single(struct device_node *device)
{
	int nid = NUMA_NO_NODE;
	const __be32 *tmp;

	tmp = of_get_associativity(device);
	if (tmp)
		nid = associativity_to_nid(tmp);
	return nid;
}

/* Walk the device tree upwards, looking for an associativity id */
int of_node_to_nid(struct device_node *device)
{
	int nid = NUMA_NO_NODE;

	of_node_get(device);
	while (device) {
		nid = of_node_to_nid_single(device);
		if (nid != -1)
			break;

		device = of_get_next_parent(device);
	}
	of_node_put(device);

	return nid;
}
EXPORT_SYMBOL(of_node_to_nid);

static void __initialize_form1_numa_distance(const __be32 *associativity,
					     int max_array_sz)
{
	int i, nid;

	if (affinity_form != FORM1_AFFINITY)
		return;

	nid = __associativity_to_nid(associativity, max_array_sz);
	if (nid != NUMA_NO_NODE) {
		for (i = 0; i < distance_ref_points_depth; i++) {
			const __be32 *entry;
			int index = be32_to_cpu(distance_ref_points[i]) - 1;

			/*
			 * broken hierarchy, return with broken distance table
			 */
			if (WARN(index >= max_array_sz, "Broken ibm,associativity property"))
				return;

			entry = &associativity[index];
			distance_lookup_table[nid][i] = of_read_number(entry, 1);
		}
	}
}

static void initialize_form1_numa_distance(const __be32 *associativity)
{
	int array_sz;

	array_sz = of_read_number(associativity, 1);
	/* Skip the first element in the associativity array */
	__initialize_form1_numa_distance(associativity + 1, array_sz);
}

/*
 * Used to update distance information w.r.t newly added node.
 */
void update_numa_distance(struct device_node *node)
{
	int nid;

	if (affinity_form == FORM0_AFFINITY)
		return;
	else if (affinity_form == FORM1_AFFINITY) {
		const __be32 *associativity;

		associativity = of_get_associativity(node);
		if (!associativity)
			return;

		initialize_form1_numa_distance(associativity);
		return;
	}

	/* FORM2 affinity  */
	nid = of_node_to_nid_single(node);
	if (nid == NUMA_NO_NODE)
		return;

	/*
	 * With FORM2 we expect NUMA distance of all possible NUMA
	 * nodes to be provided during boot.
	 */
	WARN(numa_distance_table[nid][nid] == -1,
	     "NUMA distance details for node %d not provided\n", nid);
}

/*
 * ibm,numa-lookup-index-table= {N, domainid1, domainid2, ..... domainidN}
 * ibm,numa-distance-table = { N, 1, 2, 4, 5, 1, 6, .... N elements}
 */
static void initialize_form2_numa_distance_lookup_table(void)
{
	int i, j;
	struct device_node *root;
	const __u8 *numa_dist_table;
	const __be32 *numa_lookup_index;
	int numa_dist_table_length;
	int max_numa_index, distance_index;

	if (firmware_has_feature(FW_FEATURE_OPAL))
		root = of_find_node_by_path("/ibm,opal");
	else
		root = of_find_node_by_path("/rtas");
	if (!root)
		root = of_find_node_by_path("/");

	numa_lookup_index = of_get_property(root, "ibm,numa-lookup-index-table", NULL);
	max_numa_index = of_read_number(&numa_lookup_index[0], 1);

	/* first element of the array is the size and is encode-int */
	numa_dist_table = of_get_property(root, "ibm,numa-distance-table", NULL);
	numa_dist_table_length = of_read_number((const __be32 *)&numa_dist_table[0], 1);
	/* Skip the size which is encoded int */
	numa_dist_table += sizeof(__be32);

	pr_debug("numa_dist_table_len = %d, numa_dist_indexes_len = %d\n",
		 numa_dist_table_length, max_numa_index);

	for (i = 0; i < max_numa_index; i++)
		/* +1 skip the max_numa_index in the property */
		numa_id_index_table[i] = of_read_number(&numa_lookup_index[i + 1], 1);


	if (numa_dist_table_length != max_numa_index * max_numa_index) {
		WARN(1, "Wrong NUMA distance information\n");
		/* consider everybody else just remote. */
		for (i = 0;  i < max_numa_index; i++) {
			for (j = 0; j < max_numa_index; j++) {
				int nodeA = numa_id_index_table[i];
				int nodeB = numa_id_index_table[j];

				if (nodeA == nodeB)
					numa_distance_table[nodeA][nodeB] = LOCAL_DISTANCE;
				else
					numa_distance_table[nodeA][nodeB] = REMOTE_DISTANCE;
			}
		}
	}

	distance_index = 0;
	for (i = 0;  i < max_numa_index; i++) {
		for (j = 0; j < max_numa_index; j++) {
			int nodeA = numa_id_index_table[i];
			int nodeB = numa_id_index_table[j];

			numa_distance_table[nodeA][nodeB] = numa_dist_table[distance_index++];
			pr_debug("dist[%d][%d]=%d ", nodeA, nodeB, numa_distance_table[nodeA][nodeB]);
		}
	}
	of_node_put(root);
}

static int __init find_primary_domain_index(void)
{
	int index;
	struct device_node *root;

	/*
	 * Check for which form of affinity.
	 */
	if (firmware_has_feature(FW_FEATURE_OPAL)) {
		affinity_form = FORM1_AFFINITY;
	} else if (firmware_has_feature(FW_FEATURE_FORM2_AFFINITY)) {
		pr_debug("Using form 2 affinity\n");
		affinity_form = FORM2_AFFINITY;
	} else if (firmware_has_feature(FW_FEATURE_FORM1_AFFINITY)) {
		pr_debug("Using form 1 affinity\n");
		affinity_form = FORM1_AFFINITY;
	} else
		affinity_form = FORM0_AFFINITY;

	if (firmware_has_feature(FW_FEATURE_OPAL))
		root = of_find_node_by_path("/ibm,opal");
	else
		root = of_find_node_by_path("/rtas");
	if (!root)
		root = of_find_node_by_path("/");

	/*
	 * This property is a set of 32-bit integers, each representing
	 * an index into the ibm,associativity nodes.
	 *
	 * With form 0 affinity the first integer is for an SMP configuration
	 * (should be all 0's) and the second is for a normal NUMA
	 * configuration. We have only one level of NUMA.
	 *
	 * With form 1 affinity the first integer is the most significant
	 * NUMA boundary and the following are progressively less significant
	 * boundaries. There can be more than one level of NUMA.
	 */
	distance_ref_points = of_get_property(root,
					"ibm,associativity-reference-points",
					&distance_ref_points_depth);

	if (!distance_ref_points) {
		pr_debug("ibm,associativity-reference-points not found.\n");
		goto err;
	}

	distance_ref_points_depth /= sizeof(int);
	if (affinity_form == FORM0_AFFINITY) {
		if (distance_ref_points_depth < 2) {
			pr_warn("short ibm,associativity-reference-points\n");
			goto err;
		}

		index = of_read_number(&distance_ref_points[1], 1);
	} else {
		/*
		 * Both FORM1 and FORM2 affinity find the primary domain details
		 * at the same offset.
		 */
		index = of_read_number(distance_ref_points, 1);
	}
	/*
	 * Warn and cap if the hardware supports more than
	 * MAX_DISTANCE_REF_POINTS domains.
	 */
	if (distance_ref_points_depth > MAX_DISTANCE_REF_POINTS) {
		pr_warn("distance array capped at %d entries\n",
			MAX_DISTANCE_REF_POINTS);
		distance_ref_points_depth = MAX_DISTANCE_REF_POINTS;
	}

	of_node_put(root);
	return index;

err:
	of_node_put(root);
	return -1;
}

static void __init get_n_mem_cells(int *n_addr_cells, int *n_size_cells)
{
	struct device_node *memory = NULL;

	memory = of_find_node_by_type(memory, "memory");
	if (!memory)
		panic("numa.c: No memory nodes found!");

	*n_addr_cells = of_n_addr_cells(memory);
	*n_size_cells = of_n_size_cells(memory);
	of_node_put(memory);
}

static unsigned long read_n_cells(int n, const __be32 **buf)
{
	unsigned long result = 0;

	while (n--) {
		result = (result << 32) | of_read_number(*buf, 1);
		(*buf)++;
	}
	return result;
}

struct assoc_arrays {
	u32	n_arrays;
	u32	array_sz;
	const __be32 *arrays;
};

/*
 * Retrieve and validate the list of associativity arrays for drconf
 * memory from the ibm,associativity-lookup-arrays property of the
 * device tree..
 *
 * The layout of the ibm,associativity-lookup-arrays property is a number N
 * indicating the number of associativity arrays, followed by a number M
 * indicating the size of each associativity array, followed by a list
 * of N associativity arrays.
 */
static int of_get_assoc_arrays(struct assoc_arrays *aa)
{
	struct device_node *memory;
	const __be32 *prop;
	u32 len;

	memory = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (!memory)
		return -1;

	prop = of_get_property(memory, "ibm,associativity-lookup-arrays", &len);
	if (!prop || len < 2 * sizeof(unsigned int)) {
		of_node_put(memory);
		return -1;
	}

	aa->n_arrays = of_read_number(prop++, 1);
	aa->array_sz = of_read_number(prop++, 1);

	of_node_put(memory);

	/* Now that we know the number of arrays and size of each array,
	 * revalidate the size of the property read in.
	 */
	if (len < (aa->n_arrays * aa->array_sz + 2) * sizeof(unsigned int))
		return -1;

	aa->arrays = prop;
	return 0;
}

static int get_nid_and_numa_distance(struct drmem_lmb *lmb)
{
	struct assoc_arrays aa = { .arrays = NULL };
	int default_nid = NUMA_NO_NODE;
	int nid = default_nid;
	int rc, index;

	if ((primary_domain_index < 0) || !numa_enabled)
		return default_nid;

	rc = of_get_assoc_arrays(&aa);
	if (rc)
		return default_nid;

	if (primary_domain_index <= aa.array_sz &&
	    !(lmb->flags & DRCONF_MEM_AI_INVALID) && lmb->aa_index < aa.n_arrays) {
		const __be32 *associativity;

		index = lmb->aa_index * aa.array_sz;
		associativity = &aa.arrays[index];
		nid = __associativity_to_nid(associativity, aa.array_sz);
		if (nid > 0 && affinity_form == FORM1_AFFINITY) {
			/*
			 * lookup array associativity entries have
			 * no length of the array as the first element.
			 */
			__initialize_form1_numa_distance(associativity, aa.array_sz);
		}
	}
	return nid;
}

/*
 * This is like of_node_to_nid_single() for memory represented in the
 * ibm,dynamic-reconfiguration-memory node.
 */
int of_drconf_to_nid_single(struct drmem_lmb *lmb)
{
	struct assoc_arrays aa = { .arrays = NULL };
	int default_nid = NUMA_NO_NODE;
	int nid = default_nid;
	int rc, index;

	if ((primary_domain_index < 0) || !numa_enabled)
		return default_nid;

	rc = of_get_assoc_arrays(&aa);
	if (rc)
		return default_nid;

	if (primary_domain_index <= aa.array_sz &&
	    !(lmb->flags & DRCONF_MEM_AI_INVALID) && lmb->aa_index < aa.n_arrays) {
		const __be32 *associativity;

		index = lmb->aa_index * aa.array_sz;
		associativity = &aa.arrays[index];
		nid = __associativity_to_nid(associativity, aa.array_sz);
	}
	return nid;
}

#ifdef CONFIG_PPC_SPLPAR

static int __vphn_get_associativity(long lcpu, __be32 *associativity)
{
	long rc, hwid;

	/*
	 * On a shared lpar, device tree will not have node associativity.
	 * At this time lppaca, or its __old_status field may not be
	 * updated. Hence kernel cannot detect if its on a shared lpar. So
	 * request an explicit associativity irrespective of whether the
	 * lpar is shared or dedicated. Use the device tree property as a
	 * fallback. cpu_to_phys_id is only valid between
	 * smp_setup_cpu_maps() and smp_setup_pacas().
	 */
	if (firmware_has_feature(FW_FEATURE_VPHN)) {
		if (cpu_to_phys_id)
			hwid = cpu_to_phys_id[lcpu];
		else
			hwid = get_hard_smp_processor_id(lcpu);

		rc = hcall_vphn(hwid, VPHN_FLAG_VCPU, associativity);
		if (rc == H_SUCCESS)
			return 0;
	}

	return -1;
}

static int vphn_get_nid(long lcpu)
{
	__be32 associativity[VPHN_ASSOC_BUFSIZE] = {0};


	if (!__vphn_get_associativity(lcpu, associativity))
		return associativity_to_nid(associativity);

	return NUMA_NO_NODE;

}
#else

static int __vphn_get_associativity(long lcpu, __be32 *associativity)
{
	return -1;
}

static int vphn_get_nid(long unused)
{
	return NUMA_NO_NODE;
}
#endif  /* CONFIG_PPC_SPLPAR */

/*
 * Figure out to which domain a cpu belongs and stick it there.
 * Return the id of the domain used.
 */
static int numa_setup_cpu(unsigned long lcpu)
{
	struct device_node *cpu;
	int fcpu = cpu_first_thread_sibling(lcpu);
	int nid = NUMA_NO_NODE;

	if (!cpu_present(lcpu)) {
		set_cpu_numa_node(lcpu, first_online_node);
		return first_online_node;
	}

	/*
	 * If a valid cpu-to-node mapping is already available, use it
	 * directly instead of querying the firmware, since it represents
	 * the most recent mapping notified to us by the platform (eg: VPHN).
	 * Since cpu_to_node binding remains the same for all threads in the
	 * core. If a valid cpu-to-node mapping is already available, for
	 * the first thread in the core, use it.
	 */
	nid = numa_cpu_lookup_table[fcpu];
	if (nid >= 0) {
		map_cpu_to_node(lcpu, nid);
		return nid;
	}

	nid = vphn_get_nid(lcpu);
	if (nid != NUMA_NO_NODE)
		goto out_present;

	cpu = of_get_cpu_node(lcpu, NULL);

	if (!cpu) {
		WARN_ON(1);
		if (cpu_present(lcpu))
			goto out_present;
		else
			goto out;
	}

	nid = of_node_to_nid_single(cpu);
	of_node_put(cpu);

out_present:
	if (nid < 0 || !node_possible(nid))
		nid = first_online_node;

	/*
	 * Update for the first thread of the core. All threads of a core
	 * have to be part of the same node. This not only avoids querying
	 * for every other thread in the core, but always avoids a case
	 * where virtual node associativity change causes subsequent threads
	 * of a core to be associated with different nid. However if first
	 * thread is already online, expect it to have a valid mapping.
	 */
	if (fcpu != lcpu) {
		WARN_ON(cpu_online(fcpu));
		map_cpu_to_node(fcpu, nid);
	}

	map_cpu_to_node(lcpu, nid);
out:
	return nid;
}

static void verify_cpu_node_mapping(int cpu, int node)
{
	int base, sibling, i;

	/* Verify that all the threads in the core belong to the same node */
	base = cpu_first_thread_sibling(cpu);

	for (i = 0; i < threads_per_core; i++) {
		sibling = base + i;

		if (sibling == cpu || cpu_is_offline(sibling))
			continue;

		if (cpu_to_node(sibling) != node) {
			WARN(1, "CPU thread siblings %d and %d don't belong"
				" to the same node!\n", cpu, sibling);
			break;
		}
	}
}

/* Must run before sched domains notifier. */
static int ppc_numa_cpu_prepare(unsigned int cpu)
{
	int nid;

	nid = numa_setup_cpu(cpu);
	verify_cpu_node_mapping(cpu, nid);
	return 0;
}

static int ppc_numa_cpu_dead(unsigned int cpu)
{
#ifdef CONFIG_HOTPLUG_CPU
	unmap_cpu_from_node(cpu);
#endif
	return 0;
}

/*
 * Check and possibly modify a memory region to enforce the memory limit.
 *
 * Returns the size the region should have to enforce the memory limit.
 * This will either be the original value of size, a truncated value,
 * or zero. If the returned value of size is 0 the region should be
 * discarded as it lies wholly above the memory limit.
 */
static unsigned long __init numa_enforce_memory_limit(unsigned long start,
						      unsigned long size)
{
	/*
	 * We use memblock_end_of_DRAM() in here instead of memory_limit because
	 * we've already adjusted it for the limit and it takes care of
	 * having memory holes below the limit.  Also, in the case of
	 * iommu_is_off, memory_limit is not set but is implicitly enforced.
	 */

	if (start + size <= memblock_end_of_DRAM())
		return size;

	if (start >= memblock_end_of_DRAM())
		return 0;

	return memblock_end_of_DRAM() - start;
}

/*
 * Reads the counter for a given entry in
 * linux,drconf-usable-memory property
 */
static inline int __init read_usm_ranges(const __be32 **usm)
{
	/*
	 * For each lmb in ibm,dynamic-memory a corresponding
	 * entry in linux,drconf-usable-memory property contains
	 * a counter followed by that many (base, size) duple.
	 * read the counter from linux,drconf-usable-memory
	 */
	return read_n_cells(n_mem_size_cells, usm);
}

/*
 * Extract NUMA information from the ibm,dynamic-reconfiguration-memory
 * node.  This assumes n_mem_{addr,size}_cells have been set.
 */
static int __init numa_setup_drmem_lmb(struct drmem_lmb *lmb,
					const __be32 **usm,
					void *data)
{
	unsigned int ranges, is_kexec_kdump = 0;
	unsigned long base, size, sz;
	int nid;

	/*
	 * Skip this block if the reserved bit is set in flags (0x80)
	 * or if the block is not assigned to this partition (0x8)
	 */
	if ((lmb->flags & DRCONF_MEM_RESERVED)
	    || !(lmb->flags & DRCONF_MEM_ASSIGNED))
		return 0;

	if (*usm)
		is_kexec_kdump = 1;

	base = lmb->base_addr;
	size = drmem_lmb_size();
	ranges = 1;

	if (is_kexec_kdump) {
		ranges = read_usm_ranges(usm);
		if (!ranges) /* there are no (base, size) duple */
			return 0;
	}

	do {
		if (is_kexec_kdump) {
			base = read_n_cells(n_mem_addr_cells, usm);
			size = read_n_cells(n_mem_size_cells, usm);
		}

		nid = get_nid_and_numa_distance(lmb);
		fake_numa_create_new_node(((base + size) >> PAGE_SHIFT),
					  &nid);
		node_set_online(nid);
		sz = numa_enforce_memory_limit(base, size);
		if (sz)
			memblock_set_node(base, sz, &memblock.memory, nid);
	} while (--ranges);

	return 0;
}

static int __init parse_numa_properties(void)
{
	struct device_node *memory;
	int default_nid = 0;
	unsigned long i;
	const __be32 *associativity;

	if (numa_enabled == 0) {
		pr_warn("disabled by user\n");
		return -1;
	}

	primary_domain_index = find_primary_domain_index();

	if (primary_domain_index < 0) {
		/*
		 * if we fail to parse primary_domain_index from device tree
		 * mark the numa disabled, boot with numa disabled.
		 */
		numa_enabled = false;
		return primary_domain_index;
	}

	pr_debug("associativity depth for CPU/Memory: %d\n", primary_domain_index);

	/*
	 * If it is FORM2 initialize the distance table here.
	 */
	if (affinity_form == FORM2_AFFINITY)
		initialize_form2_numa_distance_lookup_table();

	/*
	 * Even though we connect cpus to numa domains later in SMP
	 * init, we need to know the node ids now. This is because
	 * each node to be onlined must have NODE_DATA etc backing it.
	 */
	for_each_present_cpu(i) {
		__be32 vphn_assoc[VPHN_ASSOC_BUFSIZE];
		struct device_node *cpu;
		int nid = NUMA_NO_NODE;

		memset(vphn_assoc, 0, VPHN_ASSOC_BUFSIZE * sizeof(__be32));

		if (__vphn_get_associativity(i, vphn_assoc) == 0) {
			nid = associativity_to_nid(vphn_assoc);
			initialize_form1_numa_distance(vphn_assoc);
		} else {

			/*
			 * Don't fall back to default_nid yet -- we will plug
			 * cpus into nodes once the memory scan has discovered
			 * the topology.
			 */
			cpu = of_get_cpu_node(i, NULL);
			BUG_ON(!cpu);

			associativity = of_get_associativity(cpu);
			if (associativity) {
				nid = associativity_to_nid(associativity);
				initialize_form1_numa_distance(associativity);
			}
			of_node_put(cpu);
		}

		node_set_online(nid);
	}

	get_n_mem_cells(&n_mem_addr_cells, &n_mem_size_cells);

	for_each_node_by_type(memory, "memory") {
		unsigned long start;
		unsigned long size;
		int nid;
		int ranges;
		const __be32 *memcell_buf;
		unsigned int len;

		memcell_buf = of_get_property(memory,
			"linux,usable-memory", &len);
		if (!memcell_buf || len <= 0)
			memcell_buf = of_get_property(memory, "reg", &len);
		if (!memcell_buf || len <= 0)
			continue;

		/* ranges in cell */
		ranges = (len >> 2) / (n_mem_addr_cells + n_mem_size_cells);
new_range:
		/* these are order-sensitive, and modify the buffer pointer */
		start = read_n_cells(n_mem_addr_cells, &memcell_buf);
		size = read_n_cells(n_mem_size_cells, &memcell_buf);

		/*
		 * Assumption: either all memory nodes or none will
		 * have associativity properties.  If none, then
		 * everything goes to default_nid.
		 */
		associativity = of_get_associativity(memory);
		if (associativity) {
			nid = associativity_to_nid(associativity);
			initialize_form1_numa_distance(associativity);
		} else
			nid = default_nid;

		fake_numa_create_new_node(((start + size) >> PAGE_SHIFT), &nid);
		node_set_online(nid);

		size = numa_enforce_memory_limit(start, size);
		if (size)
			memblock_set_node(start, size, &memblock.memory, nid);

		if (--ranges)
			goto new_range;
	}

	/*
	 * Now do the same thing for each MEMBLOCK listed in the
	 * ibm,dynamic-memory property in the
	 * ibm,dynamic-reconfiguration-memory node.
	 */
	memory = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (memory) {
		walk_drmem_lmbs(memory, NULL, numa_setup_drmem_lmb);
		of_node_put(memory);
	}

	return 0;
}

static void __init setup_nonnuma(void)
{
	unsigned long top_of_ram = memblock_end_of_DRAM();
	unsigned long total_ram = memblock_phys_mem_size();
	unsigned long start_pfn, end_pfn;
	unsigned int nid = 0;
	int i;

	pr_debug("Top of RAM: 0x%lx, Total RAM: 0x%lx\n", top_of_ram, total_ram);
	pr_debug("Memory hole size: %ldMB\n", (top_of_ram - total_ram) >> 20);

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, NULL) {
		fake_numa_create_new_node(end_pfn, &nid);
		memblock_set_node(PFN_PHYS(start_pfn),
				  PFN_PHYS(end_pfn - start_pfn),
				  &memblock.memory, nid);
		node_set_online(nid);
	}
}

void __init dump_numa_cpu_topology(void)
{
	unsigned int node;
	unsigned int cpu, count;

	if (!numa_enabled)
		return;

	for_each_online_node(node) {
		pr_info("Node %d CPUs:", node);

		count = 0;
		/*
		 * If we used a CPU iterator here we would miss printing
		 * the holes in the cpumap.
		 */
		for (cpu = 0; cpu < nr_cpu_ids; cpu++) {
			if (cpumask_test_cpu(cpu,
					node_to_cpumask_map[node])) {
				if (count == 0)
					pr_cont(" %u", cpu);
				++count;
			} else {
				if (count > 1)
					pr_cont("-%u", cpu - 1);
				count = 0;
			}
		}

		if (count > 1)
			pr_cont("-%u", nr_cpu_ids - 1);
		pr_cont("\n");
	}
}

/* Initialize NODE_DATA for a node on the local memory */
static void __init setup_node_data(int nid, u64 start_pfn, u64 end_pfn)
{
	u64 spanned_pages = end_pfn - start_pfn;
	const size_t nd_size = roundup(sizeof(pg_data_t), SMP_CACHE_BYTES);
	u64 nd_pa;
	void *nd;
	int tnid;

	nd_pa = memblock_phys_alloc_try_nid(nd_size, SMP_CACHE_BYTES, nid);
	if (!nd_pa)
		panic("Cannot allocate %zu bytes for node %d data\n",
		      nd_size, nid);

	nd = __va(nd_pa);

	/* report and initialize */
	pr_info("  NODE_DATA [mem %#010Lx-%#010Lx]\n",
		nd_pa, nd_pa + nd_size - 1);
	tnid = early_pfn_to_nid(nd_pa >> PAGE_SHIFT);
	if (tnid != nid)
		pr_info("    NODE_DATA(%d) on node %d\n", nid, tnid);

	node_data[nid] = nd;
	memset(NODE_DATA(nid), 0, sizeof(pg_data_t));
	NODE_DATA(nid)->node_id = nid;
	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = spanned_pages;
}

static void __init find_possible_nodes(void)
{
	struct device_node *rtas;
	const __be32 *domains = NULL;
	int prop_length, max_nodes;
	u32 i;

	if (!numa_enabled)
		return;

	rtas = of_find_node_by_path("/rtas");
	if (!rtas)
		return;

	/*
	 * ibm,current-associativity-domains is a fairly recent property. If
	 * it doesn't exist, then fallback on ibm,max-associativity-domains.
	 * Current denotes what the platform can support compared to max
	 * which denotes what the Hypervisor can support.
	 *
	 * If the LPAR is migratable, new nodes might be activated after a LPM,
	 * so we should consider the max number in that case.
	 */
	if (!of_get_property(of_root, "ibm,migratable-partition", NULL))
		domains = of_get_property(rtas,
					  "ibm,current-associativity-domains",
					  &prop_length);
	if (!domains) {
		domains = of_get_property(rtas, "ibm,max-associativity-domains",
					&prop_length);
		if (!domains)
			goto out;
	}

	max_nodes = of_read_number(&domains[primary_domain_index], 1);
	pr_info("Partition configured for %d NUMA nodes.\n", max_nodes);

	for (i = 0; i < max_nodes; i++) {
		if (!node_possible(i))
			node_set(i, node_possible_map);
	}

	prop_length /= sizeof(int);
	if (prop_length > primary_domain_index + 2)
		coregroup_enabled = 1;

out:
	of_node_put(rtas);
}

void __init mem_topology_setup(void)
{
	int cpu;

	/*
	 * Linux/mm assumes node 0 to be online at boot. However this is not
	 * true on PowerPC, where node 0 is similar to any other node, it
	 * could be cpuless, memoryless node. So force node 0 to be offline
	 * for now. This will prevent cpuless, memoryless node 0 showing up
	 * unnecessarily as online. If a node has cpus or memory that need
	 * to be online, then node will anyway be marked online.
	 */
	node_set_offline(0);

	if (parse_numa_properties())
		setup_nonnuma();

	/*
	 * Modify the set of possible NUMA nodes to reflect information
	 * available about the set of online nodes, and the set of nodes
	 * that we expect to make use of for this platform's affinity
	 * calculations.
	 */
	nodes_and(node_possible_map, node_possible_map, node_online_map);

	find_possible_nodes();

	setup_node_to_cpumask_map();

	reset_numa_cpu_lookup_table();

	for_each_possible_cpu(cpu) {
		/*
		 * Powerpc with CONFIG_NUMA always used to have a node 0,
		 * even if it was memoryless or cpuless. For all cpus that
		 * are possible but not present, cpu_to_node() would point
		 * to node 0. To remove a cpuless, memoryless dummy node,
		 * powerpc need to make sure all possible but not present
		 * cpu_to_node are set to a proper node.
		 */
		numa_setup_cpu(cpu);
	}
}

void __init initmem_init(void)
{
	int nid;

	max_low_pfn = memblock_end_of_DRAM() >> PAGE_SHIFT;
	max_pfn = max_low_pfn;

	memblock_dump_all();

	for_each_online_node(nid) {
		unsigned long start_pfn, end_pfn;

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
		setup_node_data(nid, start_pfn, end_pfn);
	}

	sparse_init();

	/*
	 * We need the numa_cpu_lookup_table to be accurate for all CPUs,
	 * even before we online them, so that we can use cpu_to_{node,mem}
	 * early in boot, cf. smp_prepare_cpus().
	 * _nocalls() + manual invocation is used because cpuhp is not yet
	 * initialized for the boot CPU.
	 */
	cpuhp_setup_state_nocalls(CPUHP_POWER_NUMA_PREPARE, "powerpc/numa:prepare",
				  ppc_numa_cpu_prepare, ppc_numa_cpu_dead);
}

static int __init early_numa(char *p)
{
	if (!p)
		return 0;

	if (strstr(p, "off"))
		numa_enabled = 0;

	p = strstr(p, "fake=");
	if (p)
		cmdline = p + strlen("fake=");

	return 0;
}
early_param("numa", early_numa);

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * Find the node associated with a hot added memory section for
 * memory represented in the device tree by the property
 * ibm,dynamic-reconfiguration-memory/ibm,dynamic-memory.
 */
static int hot_add_drconf_scn_to_nid(unsigned long scn_addr)
{
	struct drmem_lmb *lmb;
	unsigned long lmb_size;
	int nid = NUMA_NO_NODE;

	lmb_size = drmem_lmb_size();

	for_each_drmem_lmb(lmb) {
		/* skip this block if it is reserved or not assigned to
		 * this partition */
		if ((lmb->flags & DRCONF_MEM_RESERVED)
		    || !(lmb->flags & DRCONF_MEM_ASSIGNED))
			continue;

		if ((scn_addr < lmb->base_addr)
		    || (scn_addr >= (lmb->base_addr + lmb_size)))
			continue;

		nid = of_drconf_to_nid_single(lmb);
		break;
	}

	return nid;
}

/*
 * Find the node associated with a hot added memory section for memory
 * represented in the device tree as a node (i.e. memory@XXXX) for
 * each memblock.
 */
static int hot_add_node_scn_to_nid(unsigned long scn_addr)
{
	struct device_node *memory;
	int nid = NUMA_NO_NODE;

	for_each_node_by_type(memory, "memory") {
		unsigned long start, size;
		int ranges;
		const __be32 *memcell_buf;
		unsigned int len;

		memcell_buf = of_get_property(memory, "reg", &len);
		if (!memcell_buf || len <= 0)
			continue;

		/* ranges in cell */
		ranges = (len >> 2) / (n_mem_addr_cells + n_mem_size_cells);

		while (ranges--) {
			start = read_n_cells(n_mem_addr_cells, &memcell_buf);
			size = read_n_cells(n_mem_size_cells, &memcell_buf);

			if ((scn_addr < start) || (scn_addr >= (start + size)))
				continue;

			nid = of_node_to_nid_single(memory);
			break;
		}

		if (nid >= 0)
			break;
	}

	of_node_put(memory);

	return nid;
}

/*
 * Find the node associated with a hot added memory section.  Section
 * corresponds to a SPARSEMEM section, not an MEMBLOCK.  It is assumed that
 * sections are fully contained within a single MEMBLOCK.
 */
int hot_add_scn_to_nid(unsigned long scn_addr)
{
	struct device_node *memory = NULL;
	int nid;

	if (!numa_enabled)
		return first_online_node;

	memory = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (memory) {
		nid = hot_add_drconf_scn_to_nid(scn_addr);
		of_node_put(memory);
	} else {
		nid = hot_add_node_scn_to_nid(scn_addr);
	}

	if (nid < 0 || !node_possible(nid))
		nid = first_online_node;

	return nid;
}

static u64 hot_add_drconf_memory_max(void)
{
	struct device_node *memory = NULL;
	struct device_node *dn = NULL;
	const __be64 *lrdr = NULL;

	dn = of_find_node_by_path("/rtas");
	if (dn) {
		lrdr = of_get_property(dn, "ibm,lrdr-capacity", NULL);
		of_node_put(dn);
		if (lrdr)
			return be64_to_cpup(lrdr);
	}

	memory = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (memory) {
		of_node_put(memory);
		return drmem_lmb_memory_max();
	}
	return 0;
}

/*
 * memory_hotplug_max - return max address of memory that may be added
 *
 * This is currently only used on systems that support drconfig memory
 * hotplug.
 */
u64 memory_hotplug_max(void)
{
        return max(hot_add_drconf_memory_max(), memblock_end_of_DRAM());
}
#endif /* CONFIG_MEMORY_HOTPLUG */

/* Virtual Processor Home Node (VPHN) support */
#ifdef CONFIG_PPC_SPLPAR
static int topology_inited;

/*
 * Retrieve the new associativity information for a virtual processor's
 * home node.
 */
static long vphn_get_associativity(unsigned long cpu,
					__be32 *associativity)
{
	long rc;

	rc = hcall_vphn(get_hard_smp_processor_id(cpu),
				VPHN_FLAG_VCPU, associativity);

	switch (rc) {
	case H_SUCCESS:
		pr_debug("VPHN hcall succeeded. Reset polling...\n");
		goto out;

	case H_FUNCTION:
		pr_err_ratelimited("VPHN unsupported. Disabling polling...\n");
		break;
	case H_HARDWARE:
		pr_err_ratelimited("hcall_vphn() experienced a hardware fault "
			"preventing VPHN. Disabling polling...\n");
		break;
	case H_PARAMETER:
		pr_err_ratelimited("hcall_vphn() was passed an invalid parameter. "
			"Disabling polling...\n");
		break;
	default:
		pr_err_ratelimited("hcall_vphn() returned %ld. Disabling polling...\n"
			, rc);
		break;
	}
out:
	return rc;
}

int find_and_online_cpu_nid(int cpu)
{
	__be32 associativity[VPHN_ASSOC_BUFSIZE] = {0};
	int new_nid;

	/* Use associativity from first thread for all siblings */
	if (vphn_get_associativity(cpu, associativity))
		return cpu_to_node(cpu);

	new_nid = associativity_to_nid(associativity);
	if (new_nid < 0 || !node_possible(new_nid))
		new_nid = first_online_node;

	if (NODE_DATA(new_nid) == NULL) {
#ifdef CONFIG_MEMORY_HOTPLUG
		/*
		 * Need to ensure that NODE_DATA is initialized for a node from
		 * available memory (see memblock_alloc_try_nid). If unable to
		 * init the node, then default to nearest node that has memory
		 * installed. Skip onlining a node if the subsystems are not
		 * yet initialized.
		 */
		if (!topology_inited || try_online_node(new_nid))
			new_nid = first_online_node;
#else
		/*
		 * Default to using the nearest node that has memory installed.
		 * Otherwise, it would be necessary to patch the kernel MM code
		 * to deal with more memoryless-node error conditions.
		 */
		new_nid = first_online_node;
#endif
	}

	pr_debug("%s:%d cpu %d nid %d\n", __FUNCTION__, __LINE__,
		cpu, new_nid);
	return new_nid;
}

int cpu_to_coregroup_id(int cpu)
{
	__be32 associativity[VPHN_ASSOC_BUFSIZE] = {0};
	int index;

	if (cpu < 0 || cpu > nr_cpu_ids)
		return -1;

	if (!coregroup_enabled)
		goto out;

	if (!firmware_has_feature(FW_FEATURE_VPHN))
		goto out;

	if (vphn_get_associativity(cpu, associativity))
		goto out;

	index = of_read_number(associativity, 1);
	if (index > primary_domain_index + 1)
		return of_read_number(&associativity[index - 1], 1);

out:
	return cpu_to_core_id(cpu);
}

static int topology_update_init(void)
{
	topology_inited = 1;
	return 0;
}
device_initcall(topology_update_init);
#endif /* CONFIG_PPC_SPLPAR */
