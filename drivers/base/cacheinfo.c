// SPDX-License-Identifier: GPL-2.0
/*
 * cacheinfo support - processor cache information via sysfs
 *
 * Based on arch/x86/kernel/cpu/intel_cacheinfo.c
 * Author: Sudeep Holla <sudeep.holla@arm.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/cacheinfo.h>
#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/sysfs.h>

/* pointer to per cpu cacheinfo */
static DEFINE_PER_CPU(struct cpu_cacheinfo, ci_cpu_cacheinfo);
#define ci_cacheinfo(cpu)	(&per_cpu(ci_cpu_cacheinfo, cpu))
#define cache_leaves(cpu)	(ci_cacheinfo(cpu)->num_leaves)
#define per_cpu_cacheinfo(cpu)	(ci_cacheinfo(cpu)->info_list)
#define per_cpu_cacheinfo_idx(cpu, idx)		\
				(per_cpu_cacheinfo(cpu) + (idx))

struct cpu_cacheinfo *get_cpu_cacheinfo(unsigned int cpu)
{
	return ci_cacheinfo(cpu);
}

static inline bool cache_leaves_are_shared(struct cacheinfo *this_leaf,
					   struct cacheinfo *sib_leaf)
{
	/*
	 * For non DT/ACPI systems, assume unique level 1 caches,
	 * system-wide shared caches for all other levels.
	 */
	if (!(IS_ENABLED(CONFIG_OF) || IS_ENABLED(CONFIG_ACPI)))
		return (this_leaf->level != 1) && (sib_leaf->level != 1);

	if ((sib_leaf->attributes & CACHE_ID) &&
	    (this_leaf->attributes & CACHE_ID))
		return sib_leaf->id == this_leaf->id;

	return sib_leaf->fw_token == this_leaf->fw_token;
}

bool last_level_cache_is_valid(unsigned int cpu)
{
	struct cacheinfo *llc;

	if (!cache_leaves(cpu))
		return false;

	llc = per_cpu_cacheinfo_idx(cpu, cache_leaves(cpu) - 1);

	return (llc->attributes & CACHE_ID) || !!llc->fw_token;

}

bool last_level_cache_is_shared(unsigned int cpu_x, unsigned int cpu_y)
{
	struct cacheinfo *llc_x, *llc_y;

	if (!last_level_cache_is_valid(cpu_x) ||
	    !last_level_cache_is_valid(cpu_y))
		return false;

	llc_x = per_cpu_cacheinfo_idx(cpu_x, cache_leaves(cpu_x) - 1);
	llc_y = per_cpu_cacheinfo_idx(cpu_y, cache_leaves(cpu_y) - 1);

	return cache_leaves_are_shared(llc_x, llc_y);
}

#ifdef CONFIG_OF
/* OF properties to query for a given cache type */
struct cache_type_info {
	const char *size_prop;
	const char *line_size_props[2];
	const char *nr_sets_prop;
};

static const struct cache_type_info cache_type_info[] = {
	{
		.size_prop       = "cache-size",
		.line_size_props = { "cache-line-size",
				     "cache-block-size", },
		.nr_sets_prop    = "cache-sets",
	}, {
		.size_prop       = "i-cache-size",
		.line_size_props = { "i-cache-line-size",
				     "i-cache-block-size", },
		.nr_sets_prop    = "i-cache-sets",
	}, {
		.size_prop       = "d-cache-size",
		.line_size_props = { "d-cache-line-size",
				     "d-cache-block-size", },
		.nr_sets_prop    = "d-cache-sets",
	},
};

static inline int get_cacheinfo_idx(enum cache_type type)
{
	if (type == CACHE_TYPE_UNIFIED)
		return 0;
	return type;
}

static void cache_size(struct cacheinfo *this_leaf, struct device_node *np)
{
	const char *propname;
	int ct_idx;

	ct_idx = get_cacheinfo_idx(this_leaf->type);
	propname = cache_type_info[ct_idx].size_prop;

	of_property_read_u32(np, propname, &this_leaf->size);
}

/* not cache_line_size() because that's a macro in include/linux/cache.h */
static void cache_get_line_size(struct cacheinfo *this_leaf,
				struct device_node *np)
{
	int i, lim, ct_idx;

	ct_idx = get_cacheinfo_idx(this_leaf->type);
	lim = ARRAY_SIZE(cache_type_info[ct_idx].line_size_props);

	for (i = 0; i < lim; i++) {
		int ret;
		u32 line_size;
		const char *propname;

		propname = cache_type_info[ct_idx].line_size_props[i];
		ret = of_property_read_u32(np, propname, &line_size);
		if (!ret) {
			this_leaf->coherency_line_size = line_size;
			break;
		}
	}
}

static void cache_nr_sets(struct cacheinfo *this_leaf, struct device_node *np)
{
	const char *propname;
	int ct_idx;

	ct_idx = get_cacheinfo_idx(this_leaf->type);
	propname = cache_type_info[ct_idx].nr_sets_prop;

	of_property_read_u32(np, propname, &this_leaf->number_of_sets);
}

static void cache_associativity(struct cacheinfo *this_leaf)
{
	unsigned int line_size = this_leaf->coherency_line_size;
	unsigned int nr_sets = this_leaf->number_of_sets;
	unsigned int size = this_leaf->size;

	/*
	 * If the cache is fully associative, there is no need to
	 * check the other properties.
	 */
	if (!(nr_sets == 1) && (nr_sets > 0 && size > 0 && line_size > 0))
		this_leaf->ways_of_associativity = (size / nr_sets) / line_size;
}

static bool cache_node_is_unified(struct cacheinfo *this_leaf,
				  struct device_node *np)
{
	return of_property_read_bool(np, "cache-unified");
}

static void cache_of_set_props(struct cacheinfo *this_leaf,
			       struct device_node *np)
{
	/*
	 * init_cache_level must setup the cache level correctly
	 * overriding the architecturally specified levels, so
	 * if type is NONE at this stage, it should be unified
	 */
	if (this_leaf->type == CACHE_TYPE_NOCACHE &&
	    cache_node_is_unified(this_leaf, np))
		this_leaf->type = CACHE_TYPE_UNIFIED;
	cache_size(this_leaf, np);
	cache_get_line_size(this_leaf, np);
	cache_nr_sets(this_leaf, np);
	cache_associativity(this_leaf);
}

static int cache_setup_of_node(unsigned int cpu)
{
	struct device_node *np;
	struct cacheinfo *this_leaf;
	unsigned int index = 0;

	np = of_cpu_device_node_get(cpu);
	if (!np) {
		pr_err("Failed to find cpu%d device node\n", cpu);
		return -ENOENT;
	}

	while (index < cache_leaves(cpu)) {
		this_leaf = per_cpu_cacheinfo_idx(cpu, index);
		if (this_leaf->level != 1)
			np = of_find_next_cache_node(np);
		else
			np = of_node_get(np);/* cpu node itself */
		if (!np)
			break;
		cache_of_set_props(this_leaf, np);
		this_leaf->fw_token = np;
		index++;
	}

	if (index != cache_leaves(cpu)) /* not all OF nodes populated */
		return -ENOENT;

	return 0;
}
#else
static inline int cache_setup_of_node(unsigned int cpu) { return 0; }
#endif

int __weak cache_setup_acpi(unsigned int cpu)
{
	return -ENOTSUPP;
}

unsigned int coherency_max_size;

static int cache_setup_properties(unsigned int cpu)
{
	int ret = 0;

	if (of_have_populated_dt())
		ret = cache_setup_of_node(cpu);
	else if (!acpi_disabled)
		ret = cache_setup_acpi(cpu);

	return ret;
}

static int cache_shared_cpu_map_setup(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf, *sib_leaf;
	unsigned int index, sib_index;
	int ret = 0;

	if (this_cpu_ci->cpu_map_populated)
		return 0;

	/*
	 * skip setting up cache properties if LLC is valid, just need
	 * to update the shared cpu_map if the cache attributes were
	 * populated early before all the cpus are brought online
	 */
	if (!last_level_cache_is_valid(cpu)) {
		ret = cache_setup_properties(cpu);
		if (ret)
			return ret;
	}

	for (index = 0; index < cache_leaves(cpu); index++) {
		unsigned int i;

		this_leaf = per_cpu_cacheinfo_idx(cpu, index);

		cpumask_set_cpu(cpu, &this_leaf->shared_cpu_map);
		for_each_online_cpu(i) {
			struct cpu_cacheinfo *sib_cpu_ci = get_cpu_cacheinfo(i);

			if (i == cpu || !sib_cpu_ci->info_list)
				continue;/* skip if itself or no cacheinfo */
			for (sib_index = 0; sib_index < cache_leaves(i); sib_index++) {
				sib_leaf = per_cpu_cacheinfo_idx(i, sib_index);
				if (cache_leaves_are_shared(this_leaf, sib_leaf)) {
					cpumask_set_cpu(cpu, &sib_leaf->shared_cpu_map);
					cpumask_set_cpu(i, &this_leaf->shared_cpu_map);
					break;
				}
			}
		}
		/* record the maximum cache line size */
		if (this_leaf->coherency_line_size > coherency_max_size)
			coherency_max_size = this_leaf->coherency_line_size;
	}

	return 0;
}

static void cache_shared_cpu_map_remove(unsigned int cpu)
{
	struct cacheinfo *this_leaf, *sib_leaf;
	unsigned int sibling, index, sib_index;

	for (index = 0; index < cache_leaves(cpu); index++) {
		this_leaf = per_cpu_cacheinfo_idx(cpu, index);
		for_each_cpu(sibling, &this_leaf->shared_cpu_map) {
			struct cpu_cacheinfo *sib_cpu_ci =
						get_cpu_cacheinfo(sibling);

			if (sibling == cpu || !sib_cpu_ci->info_list)
				continue;/* skip if itself or no cacheinfo */

			for (sib_index = 0; sib_index < cache_leaves(sibling); sib_index++) {
				sib_leaf = per_cpu_cacheinfo_idx(sibling, sib_index);
				if (cache_leaves_are_shared(this_leaf, sib_leaf)) {
					cpumask_clear_cpu(cpu, &sib_leaf->shared_cpu_map);
					cpumask_clear_cpu(sibling, &this_leaf->shared_cpu_map);
					break;
				}
			}
		}
		if (of_have_populated_dt())
			of_node_put(this_leaf->fw_token);
	}
}

static void free_cache_attributes(unsigned int cpu)
{
	if (!per_cpu_cacheinfo(cpu))
		return;

	cache_shared_cpu_map_remove(cpu);

	kfree(per_cpu_cacheinfo(cpu));
	per_cpu_cacheinfo(cpu) = NULL;
	cache_leaves(cpu) = 0;
}

int __weak init_cache_level(unsigned int cpu)
{
	return -ENOENT;
}

int __weak populate_cache_leaves(unsigned int cpu)
{
	return -ENOENT;
}

int detect_cache_attributes(unsigned int cpu)
{
	int ret;

	/* Since early detection of the cacheinfo is allowed via this
	 * function and this also gets called as CPU hotplug callbacks via
	 * cacheinfo_cpu_online, the initialisation can be skipped and only
	 * CPU maps can be updated as the CPU online status would be update
	 * if called via cacheinfo_cpu_online path.
	 */
	if (per_cpu_cacheinfo(cpu))
		goto update_cpu_map;

	if (init_cache_level(cpu) || !cache_leaves(cpu))
		return -ENOENT;

	per_cpu_cacheinfo(cpu) = kcalloc(cache_leaves(cpu),
					 sizeof(struct cacheinfo), GFP_ATOMIC);
	if (per_cpu_cacheinfo(cpu) == NULL) {
		cache_leaves(cpu) = 0;
		return -ENOMEM;
	}

	/*
	 * populate_cache_leaves() may completely setup the cache leaves and
	 * shared_cpu_map or it may leave it partially setup.
	 */
	ret = populate_cache_leaves(cpu);
	if (ret)
		goto free_ci;

update_cpu_map:
	/*
	 * For systems using DT for cache hierarchy, fw_token
	 * and shared_cpu_map will be set up here only if they are
	 * not populated already
	 */
	ret = cache_shared_cpu_map_setup(cpu);
	if (ret) {
		pr_warn("Unable to detect cache hierarchy for CPU %d\n", cpu);
		goto free_ci;
	}

	return 0;

free_ci:
	free_cache_attributes(cpu);
	return ret;
}

/* pointer to cpuX/cache device */
static DEFINE_PER_CPU(struct device *, ci_cache_dev);
#define per_cpu_cache_dev(cpu)	(per_cpu(ci_cache_dev, cpu))

static cpumask_t cache_dev_map;

/* pointer to array of devices for cpuX/cache/indexY */
static DEFINE_PER_CPU(struct device **, ci_index_dev);
#define per_cpu_index_dev(cpu)	(per_cpu(ci_index_dev, cpu))
#define per_cache_index_dev(cpu, idx)	((per_cpu_index_dev(cpu))[idx])

#define show_one(file_name, object)				\
static ssize_t file_name##_show(struct device *dev,		\
		struct device_attribute *attr, char *buf)	\
{								\
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);	\
	return sysfs_emit(buf, "%u\n", this_leaf->object);	\
}

show_one(id, id);
show_one(level, level);
show_one(coherency_line_size, coherency_line_size);
show_one(number_of_sets, number_of_sets);
show_one(physical_line_partition, physical_line_partition);
show_one(ways_of_associativity, ways_of_associativity);

static ssize_t size_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%uK\n", this_leaf->size >> 10);
}

static ssize_t shared_cpu_map_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	const struct cpumask *mask = &this_leaf->shared_cpu_map;

	return sysfs_emit(buf, "%*pb\n", nr_cpu_ids, mask);
}

static ssize_t shared_cpu_list_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	const struct cpumask *mask = &this_leaf->shared_cpu_map;

	return sysfs_emit(buf, "%*pbl\n", nr_cpu_ids, mask);
}

static ssize_t type_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	const char *output;

	switch (this_leaf->type) {
	case CACHE_TYPE_DATA:
		output = "Data";
		break;
	case CACHE_TYPE_INST:
		output = "Instruction";
		break;
	case CACHE_TYPE_UNIFIED:
		output = "Unified";
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(buf, "%s\n", output);
}

static ssize_t allocation_policy_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	unsigned int ci_attr = this_leaf->attributes;
	const char *output;

	if ((ci_attr & CACHE_READ_ALLOCATE) && (ci_attr & CACHE_WRITE_ALLOCATE))
		output = "ReadWriteAllocate";
	else if (ci_attr & CACHE_READ_ALLOCATE)
		output = "ReadAllocate";
	else if (ci_attr & CACHE_WRITE_ALLOCATE)
		output = "WriteAllocate";
	else
		return 0;

	return sysfs_emit(buf, "%s\n", output);
}

static ssize_t write_policy_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	unsigned int ci_attr = this_leaf->attributes;
	int n = 0;

	if (ci_attr & CACHE_WRITE_THROUGH)
		n = sysfs_emit(buf, "WriteThrough\n");
	else if (ci_attr & CACHE_WRITE_BACK)
		n = sysfs_emit(buf, "WriteBack\n");
	return n;
}

static DEVICE_ATTR_RO(id);
static DEVICE_ATTR_RO(level);
static DEVICE_ATTR_RO(type);
static DEVICE_ATTR_RO(coherency_line_size);
static DEVICE_ATTR_RO(ways_of_associativity);
static DEVICE_ATTR_RO(number_of_sets);
static DEVICE_ATTR_RO(size);
static DEVICE_ATTR_RO(allocation_policy);
static DEVICE_ATTR_RO(write_policy);
static DEVICE_ATTR_RO(shared_cpu_map);
static DEVICE_ATTR_RO(shared_cpu_list);
static DEVICE_ATTR_RO(physical_line_partition);

static struct attribute *cache_default_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_type.attr,
	&dev_attr_level.attr,
	&dev_attr_shared_cpu_map.attr,
	&dev_attr_shared_cpu_list.attr,
	&dev_attr_coherency_line_size.attr,
	&dev_attr_ways_of_associativity.attr,
	&dev_attr_number_of_sets.attr,
	&dev_attr_size.attr,
	&dev_attr_allocation_policy.attr,
	&dev_attr_write_policy.attr,
	&dev_attr_physical_line_partition.attr,
	NULL
};

static umode_t
cache_default_attrs_is_visible(struct kobject *kobj,
			       struct attribute *attr, int unused)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	const struct cpumask *mask = &this_leaf->shared_cpu_map;
	umode_t mode = attr->mode;

	if ((attr == &dev_attr_id.attr) && (this_leaf->attributes & CACHE_ID))
		return mode;
	if ((attr == &dev_attr_type.attr) && this_leaf->type)
		return mode;
	if ((attr == &dev_attr_level.attr) && this_leaf->level)
		return mode;
	if ((attr == &dev_attr_shared_cpu_map.attr) && !cpumask_empty(mask))
		return mode;
	if ((attr == &dev_attr_shared_cpu_list.attr) && !cpumask_empty(mask))
		return mode;
	if ((attr == &dev_attr_coherency_line_size.attr) &&
	    this_leaf->coherency_line_size)
		return mode;
	if ((attr == &dev_attr_ways_of_associativity.attr) &&
	    this_leaf->size) /* allow 0 = full associativity */
		return mode;
	if ((attr == &dev_attr_number_of_sets.attr) &&
	    this_leaf->number_of_sets)
		return mode;
	if ((attr == &dev_attr_size.attr) && this_leaf->size)
		return mode;
	if ((attr == &dev_attr_write_policy.attr) &&
	    (this_leaf->attributes & CACHE_WRITE_POLICY_MASK))
		return mode;
	if ((attr == &dev_attr_allocation_policy.attr) &&
	    (this_leaf->attributes & CACHE_ALLOCATE_POLICY_MASK))
		return mode;
	if ((attr == &dev_attr_physical_line_partition.attr) &&
	    this_leaf->physical_line_partition)
		return mode;

	return 0;
}

static const struct attribute_group cache_default_group = {
	.attrs = cache_default_attrs,
	.is_visible = cache_default_attrs_is_visible,
};

static const struct attribute_group *cache_default_groups[] = {
	&cache_default_group,
	NULL,
};

static const struct attribute_group *cache_private_groups[] = {
	&cache_default_group,
	NULL, /* Place holder for private group */
	NULL,
};

const struct attribute_group *
__weak cache_get_priv_group(struct cacheinfo *this_leaf)
{
	return NULL;
}

static const struct attribute_group **
cache_get_attribute_groups(struct cacheinfo *this_leaf)
{
	const struct attribute_group *priv_group =
			cache_get_priv_group(this_leaf);

	if (!priv_group)
		return cache_default_groups;

	if (!cache_private_groups[1])
		cache_private_groups[1] = priv_group;

	return cache_private_groups;
}

/* Add/Remove cache interface for CPU device */
static void cpu_cache_sysfs_exit(unsigned int cpu)
{
	int i;
	struct device *ci_dev;

	if (per_cpu_index_dev(cpu)) {
		for (i = 0; i < cache_leaves(cpu); i++) {
			ci_dev = per_cache_index_dev(cpu, i);
			if (!ci_dev)
				continue;
			device_unregister(ci_dev);
		}
		kfree(per_cpu_index_dev(cpu));
		per_cpu_index_dev(cpu) = NULL;
	}
	device_unregister(per_cpu_cache_dev(cpu));
	per_cpu_cache_dev(cpu) = NULL;
}

static int cpu_cache_sysfs_init(unsigned int cpu)
{
	struct device *dev = get_cpu_device(cpu);

	if (per_cpu_cacheinfo(cpu) == NULL)
		return -ENOENT;

	per_cpu_cache_dev(cpu) = cpu_device_create(dev, NULL, NULL, "cache");
	if (IS_ERR(per_cpu_cache_dev(cpu)))
		return PTR_ERR(per_cpu_cache_dev(cpu));

	/* Allocate all required memory */
	per_cpu_index_dev(cpu) = kcalloc(cache_leaves(cpu),
					 sizeof(struct device *), GFP_KERNEL);
	if (unlikely(per_cpu_index_dev(cpu) == NULL))
		goto err_out;

	return 0;

err_out:
	cpu_cache_sysfs_exit(cpu);
	return -ENOMEM;
}

static int cache_add_dev(unsigned int cpu)
{
	unsigned int i;
	int rc;
	struct device *ci_dev, *parent;
	struct cacheinfo *this_leaf;
	const struct attribute_group **cache_groups;

	rc = cpu_cache_sysfs_init(cpu);
	if (unlikely(rc < 0))
		return rc;

	parent = per_cpu_cache_dev(cpu);
	for (i = 0; i < cache_leaves(cpu); i++) {
		this_leaf = per_cpu_cacheinfo_idx(cpu, i);
		if (this_leaf->disable_sysfs)
			continue;
		if (this_leaf->type == CACHE_TYPE_NOCACHE)
			break;
		cache_groups = cache_get_attribute_groups(this_leaf);
		ci_dev = cpu_device_create(parent, this_leaf, cache_groups,
					   "index%1u", i);
		if (IS_ERR(ci_dev)) {
			rc = PTR_ERR(ci_dev);
			goto err;
		}
		per_cache_index_dev(cpu, i) = ci_dev;
	}
	cpumask_set_cpu(cpu, &cache_dev_map);

	return 0;
err:
	cpu_cache_sysfs_exit(cpu);
	return rc;
}

static int cacheinfo_cpu_online(unsigned int cpu)
{
	int rc = detect_cache_attributes(cpu);

	if (rc)
		return rc;
	rc = cache_add_dev(cpu);
	if (rc)
		free_cache_attributes(cpu);
	return rc;
}

static int cacheinfo_cpu_pre_down(unsigned int cpu)
{
	if (cpumask_test_and_clear_cpu(cpu, &cache_dev_map))
		cpu_cache_sysfs_exit(cpu);

	free_cache_attributes(cpu);
	return 0;
}

static int __init cacheinfo_sysfs_init(void)
{
	return cpuhp_setup_state(CPUHP_AP_BASE_CACHEINFO_ONLINE,
				 "base/cacheinfo:online",
				 cacheinfo_cpu_online, cacheinfo_cpu_pre_down);
}
device_initcall(cacheinfo_sysfs_init);
