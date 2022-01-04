/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This file contains NUMA specific variables and functions which are used on
 * NUMA machines with contiguous memory.
 * 		2002/08/07 Erich Focht <efocht@ess.nec.de>
 * Populate cpu entries in sysfs for non-numa systems as well
 *  	Intel Corporation - Ashok Raj
 * 02/27/2006 Zhang, Yanmin
 *	Populate cpu cache entries in sysfs for cpu cache info
 */

#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/node.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/nodemask.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <asm/mmzone.h>
#include <asm/numa.h>
#include <asm/cpu.h>

static struct ia64_cpu *sysfs_cpus;

void arch_fix_phys_package_id(int num, u32 slot)
{
#ifdef CONFIG_SMP
	if (cpu_data(num)->socket_id == -1)
		cpu_data(num)->socket_id = slot;
#endif
}
EXPORT_SYMBOL_GPL(arch_fix_phys_package_id);


#ifdef CONFIG_HOTPLUG_CPU
int __ref arch_register_cpu(int num)
{
	/*
	 * If CPEI can be re-targeted or if this is not
	 * CPEI target, then it is hotpluggable
	 */
	if (can_cpei_retarget() || !is_cpu_cpei_target(num))
		sysfs_cpus[num].cpu.hotpluggable = 1;
	map_cpu_to_node(num, node_cpuid[num].nid);
	return register_cpu(&sysfs_cpus[num].cpu, num);
}
EXPORT_SYMBOL(arch_register_cpu);

void __ref arch_unregister_cpu(int num)
{
	unregister_cpu(&sysfs_cpus[num].cpu);
	unmap_cpu_from_node(num, cpu_to_node(num));
}
EXPORT_SYMBOL(arch_unregister_cpu);
#else
static int __init arch_register_cpu(int num)
{
	return register_cpu(&sysfs_cpus[num].cpu, num);
}
#endif /*CONFIG_HOTPLUG_CPU*/


static int __init topology_init(void)
{
	int i, err = 0;

#ifdef CONFIG_NUMA
	/*
	 * MCD - Do we want to register all ONLINE nodes, or all POSSIBLE nodes?
	 */
	for_each_online_node(i) {
		if ((err = register_one_node(i)))
			goto out;
	}
#endif

	sysfs_cpus = kcalloc(NR_CPUS, sizeof(struct ia64_cpu), GFP_KERNEL);
	if (!sysfs_cpus)
		panic("kzalloc in topology_init failed - NR_CPUS too big?");

	for_each_present_cpu(i) {
		if((err = arch_register_cpu(i)))
			goto out;
	}
out:
	return err;
}

subsys_initcall(topology_init);


/*
 * Export cpu cache information through sysfs
 */

/*
 *  A bunch of string array to get pretty printing
 */
static const char *cache_types[] = {
	"",			/* not used */
	"Instruction",
	"Data",
	"Unified"	/* unified */
};

static const char *cache_mattrib[]={
	"WriteThrough",
	"WriteBack",
	"",		/* reserved */
	""		/* reserved */
};

struct cache_info {
	pal_cache_config_info_t	cci;
	cpumask_t shared_cpu_map;
	int level;
	int type;
	struct kobject kobj;
};

struct cpu_cache_info {
	struct cache_info *cache_leaves;
	int	num_cache_leaves;
	struct kobject kobj;
};

static struct cpu_cache_info	all_cpu_cache_info[NR_CPUS];
#define LEAF_KOBJECT_PTR(x,y)    (&all_cpu_cache_info[x].cache_leaves[y])

#ifdef CONFIG_SMP
static void cache_shared_cpu_map_setup(unsigned int cpu,
		struct cache_info * this_leaf)
{
	pal_cache_shared_info_t	csi;
	int num_shared, i = 0;
	unsigned int j;

	if (cpu_data(cpu)->threads_per_core <= 1 &&
		cpu_data(cpu)->cores_per_socket <= 1) {
		cpumask_set_cpu(cpu, &this_leaf->shared_cpu_map);
		return;
	}

	if (ia64_pal_cache_shared_info(this_leaf->level,
					this_leaf->type,
					0,
					&csi) != PAL_STATUS_SUCCESS)
		return;

	num_shared = (int) csi.num_shared;
	do {
		for_each_possible_cpu(j)
			if (cpu_data(cpu)->socket_id == cpu_data(j)->socket_id
				&& cpu_data(j)->core_id == csi.log1_cid
				&& cpu_data(j)->thread_id == csi.log1_tid)
				cpumask_set_cpu(j, &this_leaf->shared_cpu_map);

		i++;
	} while (i < num_shared &&
		ia64_pal_cache_shared_info(this_leaf->level,
				this_leaf->type,
				i,
				&csi) == PAL_STATUS_SUCCESS);
}
#else
static void cache_shared_cpu_map_setup(unsigned int cpu,
		struct cache_info * this_leaf)
{
	cpumask_set_cpu(cpu, &this_leaf->shared_cpu_map);
	return;
}
#endif

static ssize_t show_coherency_line_size(struct cache_info *this_leaf,
					char *buf)
{
	return sprintf(buf, "%u\n", 1 << this_leaf->cci.pcci_line_size);
}

static ssize_t show_ways_of_associativity(struct cache_info *this_leaf,
					char *buf)
{
	return sprintf(buf, "%u\n", this_leaf->cci.pcci_assoc);
}

static ssize_t show_attributes(struct cache_info *this_leaf, char *buf)
{
	return sprintf(buf,
			"%s\n",
			cache_mattrib[this_leaf->cci.pcci_cache_attr]);
}

static ssize_t show_size(struct cache_info *this_leaf, char *buf)
{
	return sprintf(buf, "%uK\n", this_leaf->cci.pcci_cache_size / 1024);
}

static ssize_t show_number_of_sets(struct cache_info *this_leaf, char *buf)
{
	unsigned number_of_sets = this_leaf->cci.pcci_cache_size;
	number_of_sets /= this_leaf->cci.pcci_assoc;
	number_of_sets /= 1 << this_leaf->cci.pcci_line_size;

	return sprintf(buf, "%u\n", number_of_sets);
}

static ssize_t show_shared_cpu_map(struct cache_info *this_leaf, char *buf)
{
	cpumask_t shared_cpu_map;

	cpumask_and(&shared_cpu_map,
				&this_leaf->shared_cpu_map, cpu_online_mask);
	return scnprintf(buf, PAGE_SIZE, "%*pb\n",
			 cpumask_pr_args(&shared_cpu_map));
}

static ssize_t show_type(struct cache_info *this_leaf, char *buf)
{
	int type = this_leaf->type + this_leaf->cci.pcci_unified;
	return sprintf(buf, "%s\n", cache_types[type]);
}

static ssize_t show_level(struct cache_info *this_leaf, char *buf)
{
	return sprintf(buf, "%u\n", this_leaf->level);
}

struct cache_attr {
	struct attribute attr;
	ssize_t (*show)(struct cache_info *, char *);
	ssize_t (*store)(struct cache_info *, const char *, size_t count);
};

#ifdef define_one_ro
	#undef define_one_ro
#endif
#define define_one_ro(_name) \
	static struct cache_attr _name = \
__ATTR(_name, 0444, show_##_name, NULL)

define_one_ro(level);
define_one_ro(type);
define_one_ro(coherency_line_size);
define_one_ro(ways_of_associativity);
define_one_ro(size);
define_one_ro(number_of_sets);
define_one_ro(shared_cpu_map);
define_one_ro(attributes);

static struct attribute * cache_default_attrs[] = {
	&type.attr,
	&level.attr,
	&coherency_line_size.attr,
	&ways_of_associativity.attr,
	&attributes.attr,
	&size.attr,
	&number_of_sets.attr,
	&shared_cpu_map.attr,
	NULL
};

#define to_object(k) container_of(k, struct cache_info, kobj)
#define to_attr(a) container_of(a, struct cache_attr, attr)

static ssize_t ia64_cache_show(struct kobject * kobj, struct attribute * attr, char * buf)
{
	struct cache_attr *fattr = to_attr(attr);
	struct cache_info *this_leaf = to_object(kobj);
	ssize_t ret;

	ret = fattr->show ? fattr->show(this_leaf, buf) : 0;
	return ret;
}

static const struct sysfs_ops cache_sysfs_ops = {
	.show   = ia64_cache_show
};

static struct kobj_type cache_ktype = {
	.sysfs_ops	= &cache_sysfs_ops,
	.default_attrs	= cache_default_attrs,
};

static struct kobj_type cache_ktype_percpu_entry = {
	.sysfs_ops	= &cache_sysfs_ops,
};

static void cpu_cache_sysfs_exit(unsigned int cpu)
{
	kfree(all_cpu_cache_info[cpu].cache_leaves);
	all_cpu_cache_info[cpu].cache_leaves = NULL;
	all_cpu_cache_info[cpu].num_cache_leaves = 0;
	memset(&all_cpu_cache_info[cpu].kobj, 0, sizeof(struct kobject));
	return;
}

static int cpu_cache_sysfs_init(unsigned int cpu)
{
	unsigned long i, levels, unique_caches;
	pal_cache_config_info_t cci;
	int j;
	long status;
	struct cache_info *this_cache;
	int num_cache_leaves = 0;

	if ((status = ia64_pal_cache_summary(&levels, &unique_caches)) != 0) {
		printk(KERN_ERR "ia64_pal_cache_summary=%ld\n", status);
		return -1;
	}

	this_cache=kcalloc(unique_caches, sizeof(struct cache_info),
			   GFP_KERNEL);
	if (this_cache == NULL)
		return -ENOMEM;

	for (i=0; i < levels; i++) {
		for (j=2; j >0 ; j--) {
			if ((status=ia64_pal_cache_config_info(i,j, &cci)) !=
					PAL_STATUS_SUCCESS)
				continue;

			this_cache[num_cache_leaves].cci = cci;
			this_cache[num_cache_leaves].level = i + 1;
			this_cache[num_cache_leaves].type = j;

			cache_shared_cpu_map_setup(cpu,
					&this_cache[num_cache_leaves]);
			num_cache_leaves ++;
		}
	}

	all_cpu_cache_info[cpu].cache_leaves = this_cache;
	all_cpu_cache_info[cpu].num_cache_leaves = num_cache_leaves;

	memset(&all_cpu_cache_info[cpu].kobj, 0, sizeof(struct kobject));

	return 0;
}

/* Add cache interface for CPU device */
static int cache_add_dev(unsigned int cpu)
{
	struct device *sys_dev = get_cpu_device(cpu);
	unsigned long i, j;
	struct cache_info *this_object;
	int retval = 0;

	if (all_cpu_cache_info[cpu].kobj.parent)
		return 0;


	retval = cpu_cache_sysfs_init(cpu);
	if (unlikely(retval < 0))
		return retval;

	retval = kobject_init_and_add(&all_cpu_cache_info[cpu].kobj,
				      &cache_ktype_percpu_entry, &sys_dev->kobj,
				      "%s", "cache");
	if (unlikely(retval < 0)) {
		cpu_cache_sysfs_exit(cpu);
		return retval;
	}

	for (i = 0; i < all_cpu_cache_info[cpu].num_cache_leaves; i++) {
		this_object = LEAF_KOBJECT_PTR(cpu,i);
		retval = kobject_init_and_add(&(this_object->kobj),
					      &cache_ktype,
					      &all_cpu_cache_info[cpu].kobj,
					      "index%1lu", i);
		if (unlikely(retval)) {
			for (j = 0; j < i; j++) {
				kobject_put(&(LEAF_KOBJECT_PTR(cpu,j)->kobj));
			}
			kobject_put(&all_cpu_cache_info[cpu].kobj);
			cpu_cache_sysfs_exit(cpu);
			return retval;
		}
		kobject_uevent(&(this_object->kobj), KOBJ_ADD);
	}
	kobject_uevent(&all_cpu_cache_info[cpu].kobj, KOBJ_ADD);
	return retval;
}

/* Remove cache interface for CPU device */
static int cache_remove_dev(unsigned int cpu)
{
	unsigned long i;

	for (i = 0; i < all_cpu_cache_info[cpu].num_cache_leaves; i++)
		kobject_put(&(LEAF_KOBJECT_PTR(cpu,i)->kobj));

	if (all_cpu_cache_info[cpu].kobj.parent) {
		kobject_put(&all_cpu_cache_info[cpu].kobj);
		memset(&all_cpu_cache_info[cpu].kobj,
			0,
			sizeof(struct kobject));
	}

	cpu_cache_sysfs_exit(cpu);

	return 0;
}

static int __init cache_sysfs_init(void)
{
	int ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "ia64/topology:online",
				cache_add_dev, cache_remove_dev);
	WARN_ON(ret < 0);
	return 0;
}
device_initcall(cache_sysfs_init);
