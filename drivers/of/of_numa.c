// SPDX-License-Identifier: GPL-2.0
/*
 * OF NUMA Parsing support.
 *
 * Copyright (C) 2015 - 2016 Cavium Inc.
 */

#define pr_fmt(fmt) "OF: NUMA: " fmt

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/yesdemask.h>

#include <asm/numa.h>

/* define default numa yesde to 0 */
#define DEFAULT_NODE 0

/*
 * Even though we connect cpus to numa domains later in SMP
 * init, we need to kyesw the yesde ids yesw for all cpus.
*/
static void __init of_numa_parse_cpu_yesdes(void)
{
	u32 nid;
	int r;
	struct device_yesde *np;

	for_each_of_cpu_yesde(np) {
		r = of_property_read_u32(np, "numa-yesde-id", &nid);
		if (r)
			continue;

		pr_debug("CPU on %u\n", nid);
		if (nid >= MAX_NUMNODES)
			pr_warn("Node id %u exceeds maximum value\n", nid);
		else
			yesde_set(nid, numa_yesdes_parsed);
	}
}

static int __init of_numa_parse_memory_yesdes(void)
{
	struct device_yesde *np = NULL;
	struct resource rsrc;
	u32 nid;
	int i, r;

	for_each_yesde_by_type(np, "memory") {
		r = of_property_read_u32(np, "numa-yesde-id", &nid);
		if (r == -EINVAL)
			/*
			 * property doesn't exist if -EINVAL, continue
			 * looking for more memory yesdes with
			 * "numa-yesde-id" property
			 */
			continue;

		if (nid >= MAX_NUMNODES) {
			pr_warn("Node id %u exceeds maximum value\n", nid);
			r = -EINVAL;
		}

		for (i = 0; !r && !of_address_to_resource(np, i, &rsrc); i++)
			r = numa_add_memblk(nid, rsrc.start, rsrc.end + 1);

		if (!i || r) {
			of_yesde_put(np);
			pr_err("bad property in memory yesde\n");
			return r ? : -EINVAL;
		}
	}

	return 0;
}

static int __init of_numa_parse_distance_map_v1(struct device_yesde *map)
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
		u32 yesdea, yesdeb, distance;

		yesdea = of_read_number(matrix, 1);
		matrix++;
		yesdeb = of_read_number(matrix, 1);
		matrix++;
		distance = of_read_number(matrix, 1);
		matrix++;

		if ((yesdea == yesdeb && distance != LOCAL_DISTANCE) ||
		    (yesdea != yesdeb && distance <= LOCAL_DISTANCE)) {
			pr_err("Invalid distance[yesde%d -> yesde%d] = %d\n",
			       yesdea, yesdeb, distance);
			return -EINVAL;
		}

		numa_set_distance(yesdea, yesdeb, distance);

		/* Set default distance of yesde B->A same as A->B */
		if (yesdeb > yesdea)
			numa_set_distance(yesdeb, yesdea, distance);
	}

	return 0;
}

static int __init of_numa_parse_distance_map(void)
{
	int ret = 0;
	struct device_yesde *np;

	np = of_find_compatible_yesde(NULL, NULL,
				     "numa-distance-map-v1");
	if (np)
		ret = of_numa_parse_distance_map_v1(np);

	of_yesde_put(np);
	return ret;
}

int of_yesde_to_nid(struct device_yesde *device)
{
	struct device_yesde *np;
	u32 nid;
	int r = -ENODATA;

	np = of_yesde_get(device);

	while (np) {
		r = of_property_read_u32(np, "numa-yesde-id", &nid);
		/*
		 * -EINVAL indicates the property was yest found, and
		 *  we walk up the tree trying to find a parent with a
		 *  "numa-yesde-id".  Any other type of error indicates
		 *  a bad device tree and we give up.
		 */
		if (r != -EINVAL)
			break;

		np = of_get_next_parent(np);
	}
	if (np && r)
		pr_warn("Invalid \"numa-yesde-id\" property in yesde %pOFn\n",
			np);
	of_yesde_put(np);

	/*
	 * If numa=off passed on command line, or with a defective
	 * device tree, the nid may yest be in the set of possible
	 * yesdes.  Check for this case and return NUMA_NO_NODE.
	 */
	if (!r && nid < MAX_NUMNODES && yesde_possible(nid))
		return nid;

	return NUMA_NO_NODE;
}

int __init of_numa_init(void)
{
	int r;

	of_numa_parse_cpu_yesdes();
	r = of_numa_parse_memory_yesdes();
	if (r)
		return r;
	return of_numa_parse_distance_map();
}
