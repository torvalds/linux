/*
 * OF NUMA Parsing support.
 *
 * Copyright (C) 2015 - 2016 Cavium Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "OF: NUMA: " fmt

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/nodemask.h>

#include <asm/numa.h>

/* define default numa node to 0 */
#define DEFAULT_NODE 0

/*
 * Even though we connect cpus to numa domains later in SMP
 * init, we need to know the node ids now for all cpus.
*/
static void __init of_numa_parse_cpu_nodes(void)
{
	u32 nid;
	int r;
	struct device_node *cpus;
	struct device_node *np = NULL;

	cpus = of_find_node_by_path("/cpus");
	if (!cpus)
		return;

	for_each_child_of_node(cpus, np) {
		/* Skip things that are not CPUs */
		if (of_node_cmp(np->type, "cpu") != 0)
			continue;

		r = of_property_read_u32(np, "numa-node-id", &nid);
		if (r)
			continue;

		pr_debug("CPU on %u\n", nid);
		if (nid >= MAX_NUMNODES)
			pr_warn("Node id %u exceeds maximum value\n", nid);
		else
			node_set(nid, numa_nodes_parsed);
	}
}

static int __init of_numa_parse_memory_nodes(void)
{
	struct device_node *np = NULL;
	struct resource rsrc;
	u32 nid;
	int i, r;

	for_each_node_by_type(np, "memory") {
		r = of_property_read_u32(np, "numa-node-id", &nid);
		if (r == -EINVAL)
			/*
			 * property doesn't exist if -EINVAL, continue
			 * looking for more memory nodes with
			 * "numa-node-id" property
			 */
			continue;

		if (nid >= MAX_NUMNODES) {
			pr_warn("Node id %u exceeds maximum value\n", nid);
			r = -EINVAL;
		}

		for (i = 0; !r && !of_address_to_resource(np, i, &rsrc); i++)
			r = numa_add_memblk(nid, rsrc.start, rsrc.end + 1);

		if (!i || r) {
			of_node_put(np);
			pr_err("bad property in memory node\n");
			return r ? : -EINVAL;
		}
	}

	return 0;
}

static int __init of_numa_parse_distance_map_v1(struct device_node *map)
{
	const __be32 *matrix;
	int entry_count;
	int i;

	pr_info("parsing numa-distance-map-v1\n");

	matrix = of_get_property(map, "distance-matrix", NULL);
	if (!matrix) {
		pr_err("No distance-matrix property in distance-map\n");
		return -EINVAL;
	}

	entry_count = of_property_count_u32_elems(map, "distance-matrix");
	if (entry_count <= 0) {
		pr_err("Invalid distance-matrix\n");
		return -EINVAL;
	}

	for (i = 0; i + 2 < entry_count; i += 3) {
		u32 nodea, nodeb, distance;

		nodea = of_read_number(matrix, 1);
		matrix++;
		nodeb = of_read_number(matrix, 1);
		matrix++;
		distance = of_read_number(matrix, 1);
		matrix++;

		numa_set_distance(nodea, nodeb, distance);
		pr_debug("distance[node%d -> node%d] = %d\n",
			 nodea, nodeb, distance);

		/* Set default distance of node B->A same as A->B */
		if (nodeb > nodea)
			numa_set_distance(nodeb, nodea, distance);
	}

	return 0;
}

static int __init of_numa_parse_distance_map(void)
{
	int ret = 0;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL,
				     "numa-distance-map-v1");
	if (np)
		ret = of_numa_parse_distance_map_v1(np);

	of_node_put(np);
	return ret;
}

int of_node_to_nid(struct device_node *device)
{
	struct device_node *np;
	u32 nid;
	int r = -ENODATA;

	np = of_node_get(device);

	while (np) {
		r = of_property_read_u32(np, "numa-node-id", &nid);
		/*
		 * -EINVAL indicates the property was not found, and
		 *  we walk up the tree trying to find a parent with a
		 *  "numa-node-id".  Any other type of error indicates
		 *  a bad device tree and we give up.
		 */
		if (r != -EINVAL)
			break;

		np = of_get_next_parent(np);
	}
	if (np && r)
		pr_warn("Invalid \"numa-node-id\" property in node %s\n",
			np->name);
	of_node_put(np);

	if (!r)
		return nid;

	return NUMA_NO_NODE;
}
EXPORT_SYMBOL(of_node_to_nid);

int __init of_numa_init(void)
{
	int r;

	of_numa_parse_cpu_nodes();
	r = of_numa_parse_memory_nodes();
	if (r)
		return r;
	return of_numa_parse_distance_map();
}
