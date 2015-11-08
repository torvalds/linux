/*
 * cacheinfo support - processor cache information via sysfs
 *
 * Based on arch/x86/kernel/cpu/intel_cacheinfo.c
 * Author: Sudeep Holla <sudeep.holla@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/bitops.h>
#include <linux/cacheinfo.h>
#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/sysfs.h>

/* pointer to per cpu cacheinfo */
static DEFINE_PER_CPU(struct cpu_cacheinfo, ci_cpu_cacheinfo);
#define ci_cacheinfo(cpu)	(&per_cpu(ci_cpu_cacheinfo, cpu))
#define cache_leaves(cpu)	(ci_cacheinfo(cpu)->num_leaves)
#define per_cpu_cacheinfo(cpu)	(ci_cacheinfo(cpu)->info_list)

struct cpu_cacheinfo *get_cpu_cacheinfo(unsigned int cpu)
{
	return ci_cacheinfo(cpu);
}

#ifdef CONFIG_OF
static int cache_setup_of_node(unsigned int cpu)
{
	struct device_node *np;
	struct cacheinfo *this_leaf;
	struct device *cpu_dev = get_cpu_device(cpu);
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	unsigned int index = 0;

	/* skip if of_node is already populated */
	if (this_cpu_ci->info_list->of_node)
		return 0;

	if (!cpu_dev) {
		pr_err("No cpu device for CPU %d\n", cpu);
		return -ENODEV;
	}
	np = cpu_dev->of_node;
	if (!np) {
		pr_err("Failed to find cpu%d device node\n", cpu);
		return -ENOENT;
	}

	while (index < cache_leaves(cpu)) {
		this_leaf = this_cpu_ci->info_list + index;
		if (this_leaf->level != 1)
			np = of_find_next_cache_node(np);
		else
			np = of_node_get(np);/* cpu node itself */
		if (!np)
			break;
		this_leaf->of_node = np;
		index++;
	}

	if (index != cache_leaves(cpu)) /* not all OF nodes populated */
		return -ENOENT;

	return 0;
}

static inline bool cache_leaves_are_shared(struct cacheinfo *this_leaf,
					   struct cacheinfo *sib_leaf)
{
	return sib_leaf->of_node == this_leaf->of_node;
}
#else
static inline int cache_setup_of_node(unsigned int cpu) { return 0; }
static inline bool cache_leaves_are_shared(struct cacheinfo *this_leaf,
					   struct cacheinfo *sib_leaf)
{
	/*
	 * For non-DT systems, assume unique level 1 cache, system-wide
	 * shared caches for all other levels. This will be used only if
	 * arch specific code has not populated shared_cpu_map
	 */
	return !(this_leaf->level == 1);
}
#endif

static int cache_shared_cpu_map_setup(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf, *sib_leaf;
	unsigned int index;
	int ret;

	ret = cache_setup_of_node(cpu);
	if (ret)
		return ret;

	for (index = 0; index < cache_leaves(cpu); index++) {
		unsigned int i;

		this_leaf = this_cpu_ci->info_list + index;
		/* skip if shared_cpu_map is already populated */
		if (!cpumask_empty(&this_leaf->shared_cpu_map))
			continue;

		cpumask_set_cpu(cpu, &this_leaf->shared_cpu_map);
		for_each_online_cpu(i) {
			struct cpu_cacheinfo *sib_cpu_ci = get_cpu_cacheinfo(i);

			if (i == cpu || !sib_cpu_ci->info_list)
				continue;/* skip if itself or no cacheinfo */
			sib_leaf = sib_cpu_ci->info_list + index;
			if (cache_leaves_are_shared(this_leaf, sib_leaf)) {
				cpumask_set_cpu(cpu, &sib_leaf->shared_cpu_map);
				cpumask_set_cpu(i, &this_leaf->shared_cpu_map);
			}
		}
	}

	return 0;
}

static void cache_shared_cpu_map_remove(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf, *sib_leaf;
	unsigned int sibling, index;

	for (index = 0; index < cache_leaves(cpu); index++) {
		this_leaf = this_cpu_ci->info_list + index;
		for_each_cpu(sibling, &this_leaf->shared_cpu_map) {
			struct cpu_cacheinfo *sib_cpu_ci;

			if (sibling == cpu) /* skip itself */
				continue;

			sib_cpu_ci = get_cpu_cacheinfo(sibling);
			if (!sib_cpu_ci->info_list)
				continue;

			sib_leaf = sib_cpu_ci->info_list + index;
			cpumask_clear_cpu(cpu, &sib_leaf->shared_cpu_map);
			cpumask_clear_cpu(sibling, &this_leaf->shared_cpu_map);
		}
		of_node_put(this_leaf->of_node);
	}
}

static void free_cache_attributes(unsigned int cpu)
{
	if (!per_cpu_cacheinfo(cpu))
		return;

	cache_shared_cpu_map_remove(cpu);

	kfree(per_cpu_cacheinfo(cpu));
	per_cpu_cacheinfo(cpu) = NULL;
}

int __weak init_cache_level(unsigned int cpu)
{
	return -ENOENT;
}

int __weak populate_cache_leaves(unsigned int cpu)
{
	return -ENOENT;
}

static int detect_cache_attributes(unsigned int cpu)
{
	int ret;

	if (init_cache_level(cpu) || !cache_leaves(cpu))
		return -ENOENT;

	per_cpu_cacheinfo(cpu) = kcalloc(cache_leaves(cpu),
					 sizeof(struct cacheinfo), GFP_KERNEL);
	if (per_cpu_cacheinfo(cpu) == NULL)
		return -ENOMEM;

	ret = populate_cache_leaves(cpu);
	if (ret)
		goto free_ci;
	/*
	 * For systems using DT for cache hierarchy, of_node and shared_cpu_map
	 * will be set up here only if they are not populated already
	 */
	ret = cache_shared_cpu_map_setup(cpu);
	if (ret) {
		pr_warn("Unable to detect cache hierarchy from DT for CPU %d\n",
			cpu);
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
	return sprintf(buf, "%u\n", this_leaf->object);		\
}

show_one(level, level);
show_one(coherency_line_size, coherency_line_size);
show_one(number_of_sets, number_of_sets);
show_one(physical_line_partition, physical_line_partition);
show_one(ways_of_associativity, ways_of_associativity);

static ssize_t size_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);

	return sprintf(buf, "%uK\n", this_leaf->size >> 10);
}

static ssize_t shared_cpumap_show_func(struct device *dev, bool list, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	const struct cpumask *mask = &this_leaf->shared_cpu_map;

	return cpumap_print_to_pagebuf(list, buf, mask);
}

static ssize_t shared_cpu_map_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return shared_cpumap_show_func(dev, false, buf);
}

static ssize_t shared_cpu_list_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	return shared_cpumap_show_func(dev, true, buf);
}

static ssize_t type_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);

	switch (this_leaf->type) {
	case CACHE_TYPE_DATA:
		return sprintf(buf, "Data\n");
	case CACHE_TYPE_INST:
		return sprintf(buf, "Instruction\n");
	case CACHE_TYPE_UNIFIED:
		return sprintf(buf, "Unified\n");
	default:
		return -EINVAL;
	}
}

static ssize_t allocation_policy_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	unsigned int ci_attr = this_leaf->attributes;
	int n = 0;

	if ((ci_attr & CACHE_READ_ALLOCATE) && (ci_attr & CACHE_WRITE_ALLOCATE))
		n = sprintf(buf, "ReadWriteAllocate\n");
	else if (ci_attr & CACHE_READ_ALLOCATE)
		n = sprintf(buf, "ReadAllocate\n");
	else if (ci_attr & CACHE_WRITE_ALLOCATE)
		n = sprintf(buf, "WriteAllocate\n");
	return n;
}

static ssize_t write_policy_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	unsigned int ci_attr = this_leaf->attributes;
	int n = 0;

	if (ci_attr & CACHE_WRITE_THROUGH)
		n = sprintf(buf, "WriteThrough\n");
	else if (ci_attr & CACHE_WRITE_BACK)
		n = sprintf(buf, "WriteBack\n");
	return n;
}

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
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	const struct attribute_group **cache_groups;

	rc = cpu_cache_sysfs_init(cpu);
	if (unlikely(rc < 0))
		return rc;

	parent = per_cpu_cache_dev(cpu);
	for (i = 0; i < cache_leaves(cpu); i++) {
		this_leaf = this_cpu_ci->info_list + i;
		if (this_leaf->disable_sysfs)
			continue;
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

static void cache_remove_dev(unsigned int cpu)
{
	if (!cpumask_test_cpu(cpu, &cache_dev_map))
		return;
	cpumask_clear_cpu(cpu, &cache_dev_map);

	cpu_cache_sysfs_exit(cpu);
}

static int cacheinfo_cpu_callback(struct notifier_block *nfb,
				  unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	int rc = 0;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
		rc = detect_cache_attributes(cpu);
		if (!rc)
			rc = cache_add_dev(cpu);
		break;
	case CPU_DEAD:
		cache_remove_dev(cpu);
		free_cache_attributes(cpu);
		break;
	}
	return notifier_from_errno(rc);
}

static int __init cacheinfo_sysfs_init(void)
{
	int cpu, rc = 0;

	cpu_notifier_register_begin();

	for_each_online_cpu(cpu) {
		rc = detect_cache_attributes(cpu);
		if (rc)
			goto out;
		rc = cache_add_dev(cpu);
		if (rc) {
			free_cache_attributes(cpu);
			pr_err("error populating cacheinfo..cpu%d\n", cpu);
			goto out;
		}
	}
	__hotcpu_notifier(cacheinfo_cpu_callback, 0);

out:
	cpu_notifier_register_done();
	return rc;
}

device_initcall(cacheinfo_sysfs_init);
