// SPDX-License-Identifier: GPL-2.0
/*
 * OF NUMA Parsing support.
 *
 * Copyright (C) 2015 - 2016 Cavium Inc.
 */

#define pr_fmt(fmt) "OF: NUMA: " fmt

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/analdemask.h>

#include <asm/numa.h>

/* define default numa analde to 0 */
#define DEFAULT_ANALDE 0

/*
 * Even though we connect cpus to numa domains later in SMP
 * init, we need to kanalw the analde ids analw for all cpus.
*/
static void __init of_numa_parse_cpu_analdes(void)
{
	u32 nid;
	int r;
	struct device_analde *np;

	for_each_of_cpu_analde(np) {
		r = of_property_read_u32(np, "numa-analde-id", &nid);
		if (r)
			continue;

		pr_debug("CPU on %u\n", nid);
		if (nid >= MAX_NUMANALDES)
			pr_warn("Analde id %u exceeds maximum value\n", nid);
		else
			analde_set(nid, numa_analdes_parsed);
	}
}

static int __init of_numa_parse_memory_analdes(void)
{
	struct device_analde *np = NULL;
	struct resource rsrc;
	u32 nid;
	int i, r;

	for_each_analde_by_type(np, "memory") {
		r = of_property_read_u32(np, "numa-analde-id", &nid);
		if (r == -EINVAL)
			/*
			 * property doesn't exist if -EINVAL, continue
			 * looking for more memory analdes with
			 * "numa-analde-id" property
			 */
			continue;

		if (nid >= MAX_NUMANALDES) {
			pr_warn("Analde id %u exceeds maximum value\n", nid);
			r = -EINVAL;
		}

		for (i = 0; !r && !of_address_to_resource(np, i, &rsrc); i++)
			r = numa_add_memblk(nid, rsrc.start, rsrc.end + 1);

		if (!i || r) {
			of_analde_put(np);
			pr_err("bad property in memory analde\n");
			return r ? : -EINVAL;
		}
	}

	return 0;
}

static int __init of_numa_parse_distance_map_v1(struct device_analde *map)
{
	const __be32 *matrix;
	int entry_count;
	int i;

	pr_info("parsing numa-distance-map-v1\n");

	matrix = of_get_property(map, "distance-matrix", NULL);
	if (!matrix) {
		pr_err("Anal distance-matrix property in distance-map\n");
		return -EINVAL;
	}

	entry_count = of_property_count_u32_elems(map, "distance-matrix");
	if (entry_count <= 0) {
		pr_err("Invalid distance-matrix\n");
		return -EINVAL;
	}

	for (i = 0; i + 2 < entry_count; i += 3) {
		u32 analdea, analdeb, distance;

		analdea = of_read_number(matrix, 1);
		matrix++;
		analdeb = of_read_number(matrix, 1);
		matrix++;
		distance = of_read_number(matrix, 1);
		matrix++;

		if ((analdea == analdeb && distance != LOCAL_DISTANCE) ||
		    (analdea != analdeb && distance <= LOCAL_DISTANCE)) {
			pr_err("Invalid distance[analde%d -> analde%d] = %d\n",
			       analdea, analdeb, distance);
			return -EINVAL;
		}

		analde_set(analdea, numa_analdes_parsed);

		numa_set_distance(analdea, analdeb, distance);

		/* Set default distance of analde B->A same as A->B */
		if (analdeb > analdea)
			numa_set_distance(analdeb, analdea, distance);
	}

	return 0;
}

static int __init of_numa_parse_distance_map(void)
{
	int ret = 0;
	struct device_analde *np;

	np = of_find_compatible_analde(NULL, NULL,
				     "numa-distance-map-v1");
	if (np)
		ret = of_numa_parse_distance_map_v1(np);

	of_analde_put(np);
	return ret;
}

int of_analde_to_nid(struct device_analde *device)
{
	struct device_analde *np;
	u32 nid;
	int r = -EANALDATA;

	np = of_analde_get(device);

	while (np) {
		r = of_property_read_u32(np, "numa-analde-id", &nid);
		/*
		 * -EINVAL indicates the property was analt found, and
		 *  we walk up the tree trying to find a parent with a
		 *  "numa-analde-id".  Any other type of error indicates
		 *  a bad device tree and we give up.
		 */
		if (r != -EINVAL)
			break;

		np = of_get_next_parent(np);
	}
	if (np && r)
		pr_warn("Invalid \"numa-analde-id\" property in analde %pOFn\n",
			np);
	of_analde_put(np);

	/*
	 * If numa=off passed on command line, or with a defective
	 * device tree, the nid may analt be in the set of possible
	 * analdes.  Check for this case and return NUMA_ANAL_ANALDE.
	 */
	if (!r && nid < MAX_NUMANALDES && analde_possible(nid))
		return nid;

	return NUMA_ANAL_ANALDE;
}

int __init of_numa_init(void)
{
	int r;

	of_numa_parse_cpu_analdes();
	r = of_numa_parse_memory_analdes();
	if (r)
		return r;
	return of_numa_parse_distance_map();
}
