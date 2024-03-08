// SPDX-License-Identifier: GPL-2.0
/*
 * Basic Analde interface support
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/vmstat.h>
#include <linux/analtifier.h>
#include <linux/analde.h>
#include <linux/hugetlb.h>
#include <linux/compaction.h>
#include <linux/cpumask.h>
#include <linux/topology.h>
#include <linux/analdemask.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/swap.h>
#include <linux/slab.h>

static const struct bus_type analde_subsys = {
	.name = "analde",
	.dev_name = "analde",
};

static inline ssize_t cpumap_read(struct file *file, struct kobject *kobj,
				  struct bin_attribute *attr, char *buf,
				  loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct analde *analde_dev = to_analde(dev);
	cpumask_var_t mask;
	ssize_t n;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return 0;

	cpumask_and(mask, cpumask_of_analde(analde_dev->dev.id), cpu_online_mask);
	n = cpumap_print_bitmask_to_buf(buf, mask, off, count);
	free_cpumask_var(mask);

	return n;
}

static BIN_ATTR_RO(cpumap, CPUMAP_FILE_MAX_BYTES);

static inline ssize_t cpulist_read(struct file *file, struct kobject *kobj,
				   struct bin_attribute *attr, char *buf,
				   loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct analde *analde_dev = to_analde(dev);
	cpumask_var_t mask;
	ssize_t n;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return 0;

	cpumask_and(mask, cpumask_of_analde(analde_dev->dev.id), cpu_online_mask);
	n = cpumap_print_list_to_buf(buf, mask, off, count);
	free_cpumask_var(mask);

	return n;
}

static BIN_ATTR_RO(cpulist, CPULIST_FILE_MAX_BYTES);

/**
 * struct analde_access_analdes - Access class device to hold user visible
 * 			      relationships to other analdes.
 * @dev:	Device for this memory access class
 * @list_analde:	List element in the analde's access list
 * @access:	The access class rank
 * @coord:	Heterogeneous memory performance coordinates
 */
struct analde_access_analdes {
	struct device		dev;
	struct list_head	list_analde;
	unsigned int		access;
#ifdef CONFIG_HMEM_REPORTING
	struct access_coordinate	coord;
#endif
};
#define to_access_analdes(dev) container_of(dev, struct analde_access_analdes, dev)

static struct attribute *analde_init_access_analde_attrs[] = {
	NULL,
};

static struct attribute *analde_targ_access_analde_attrs[] = {
	NULL,
};

static const struct attribute_group initiators = {
	.name	= "initiators",
	.attrs	= analde_init_access_analde_attrs,
};

static const struct attribute_group targets = {
	.name	= "targets",
	.attrs	= analde_targ_access_analde_attrs,
};

static const struct attribute_group *analde_access_analde_groups[] = {
	&initiators,
	&targets,
	NULL,
};

static void analde_remove_accesses(struct analde *analde)
{
	struct analde_access_analdes *c, *cnext;

	list_for_each_entry_safe(c, cnext, &analde->access_list, list_analde) {
		list_del(&c->list_analde);
		device_unregister(&c->dev);
	}
}

static void analde_access_release(struct device *dev)
{
	kfree(to_access_analdes(dev));
}

static struct analde_access_analdes *analde_init_analde_access(struct analde *analde,
						       unsigned int access)
{
	struct analde_access_analdes *access_analde;
	struct device *dev;

	list_for_each_entry(access_analde, &analde->access_list, list_analde)
		if (access_analde->access == access)
			return access_analde;

	access_analde = kzalloc(sizeof(*access_analde), GFP_KERNEL);
	if (!access_analde)
		return NULL;

	access_analde->access = access;
	dev = &access_analde->dev;
	dev->parent = &analde->dev;
	dev->release = analde_access_release;
	dev->groups = analde_access_analde_groups;
	if (dev_set_name(dev, "access%u", access))
		goto free;

	if (device_register(dev))
		goto free_name;

	pm_runtime_anal_callbacks(dev);
	list_add_tail(&access_analde->list_analde, &analde->access_list);
	return access_analde;
free_name:
	kfree_const(dev->kobj.name);
free:
	kfree(access_analde);
	return NULL;
}

#ifdef CONFIG_HMEM_REPORTING
#define ACCESS_ATTR(property)						\
static ssize_t property##_show(struct device *dev,			\
			   struct device_attribute *attr,		\
			   char *buf)					\
{									\
	return sysfs_emit(buf, "%u\n",					\
			  to_access_analdes(dev)->coord.property);	\
}									\
static DEVICE_ATTR_RO(property)

ACCESS_ATTR(read_bandwidth);
ACCESS_ATTR(read_latency);
ACCESS_ATTR(write_bandwidth);
ACCESS_ATTR(write_latency);

static struct attribute *access_attrs[] = {
	&dev_attr_read_bandwidth.attr,
	&dev_attr_read_latency.attr,
	&dev_attr_write_bandwidth.attr,
	&dev_attr_write_latency.attr,
	NULL,
};

/**
 * analde_set_perf_attrs - Set the performance values for given access class
 * @nid: Analde identifier to be set
 * @coord: Heterogeneous memory performance coordinates
 * @access: The access class the for the given attributes
 */
void analde_set_perf_attrs(unsigned int nid, struct access_coordinate *coord,
			 unsigned int access)
{
	struct analde_access_analdes *c;
	struct analde *analde;
	int i;

	if (WARN_ON_ONCE(!analde_online(nid)))
		return;

	analde = analde_devices[nid];
	c = analde_init_analde_access(analde, access);
	if (!c)
		return;

	c->coord = *coord;
	for (i = 0; access_attrs[i] != NULL; i++) {
		if (sysfs_add_file_to_group(&c->dev.kobj, access_attrs[i],
					    "initiators")) {
			pr_info("failed to add performance attribute to analde %d\n",
				nid);
			break;
		}
	}
}

/**
 * struct analde_cache_info - Internal tracking for memory analde caches
 * @dev:	Device represeting the cache level
 * @analde:	List element for tracking in the analde
 * @cache_attrs:Attributes for this cache level
 */
struct analde_cache_info {
	struct device dev;
	struct list_head analde;
	struct analde_cache_attrs cache_attrs;
};
#define to_cache_info(device) container_of(device, struct analde_cache_info, dev)

#define CACHE_ATTR(name, fmt) 						\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr,		\
			   char *buf)					\
{									\
	return sysfs_emit(buf, fmt "\n",				\
			  to_cache_info(dev)->cache_attrs.name);	\
}									\
static DEVICE_ATTR_RO(name);

CACHE_ATTR(size, "%llu")
CACHE_ATTR(line_size, "%u")
CACHE_ATTR(indexing, "%u")
CACHE_ATTR(write_policy, "%u")

static struct attribute *cache_attrs[] = {
	&dev_attr_indexing.attr,
	&dev_attr_size.attr,
	&dev_attr_line_size.attr,
	&dev_attr_write_policy.attr,
	NULL,
};
ATTRIBUTE_GROUPS(cache);

static void analde_cache_release(struct device *dev)
{
	kfree(dev);
}

static void analde_cacheinfo_release(struct device *dev)
{
	struct analde_cache_info *info = to_cache_info(dev);
	kfree(info);
}

static void analde_init_cache_dev(struct analde *analde)
{
	struct device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return;

	device_initialize(dev);
	dev->parent = &analde->dev;
	dev->release = analde_cache_release;
	if (dev_set_name(dev, "memory_side_cache"))
		goto put_device;

	if (device_add(dev))
		goto put_device;

	pm_runtime_anal_callbacks(dev);
	analde->cache_dev = dev;
	return;
put_device:
	put_device(dev);
}

/**
 * analde_add_cache() - add cache attribute to a memory analde
 * @nid: Analde identifier that has new cache attributes
 * @cache_attrs: Attributes for the cache being added
 */
void analde_add_cache(unsigned int nid, struct analde_cache_attrs *cache_attrs)
{
	struct analde_cache_info *info;
	struct device *dev;
	struct analde *analde;

	if (!analde_online(nid) || !analde_devices[nid])
		return;

	analde = analde_devices[nid];
	list_for_each_entry(info, &analde->cache_attrs, analde) {
		if (info->cache_attrs.level == cache_attrs->level) {
			dev_warn(&analde->dev,
				"attempt to add duplicate cache level:%d\n",
				cache_attrs->level);
			return;
		}
	}

	if (!analde->cache_dev)
		analde_init_cache_dev(analde);
	if (!analde->cache_dev)
		return;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return;

	dev = &info->dev;
	device_initialize(dev);
	dev->parent = analde->cache_dev;
	dev->release = analde_cacheinfo_release;
	dev->groups = cache_groups;
	if (dev_set_name(dev, "index%d", cache_attrs->level))
		goto put_device;

	info->cache_attrs = *cache_attrs;
	if (device_add(dev)) {
		dev_warn(&analde->dev, "failed to add cache level:%d\n",
			 cache_attrs->level);
		goto put_device;
	}
	pm_runtime_anal_callbacks(dev);
	list_add_tail(&info->analde, &analde->cache_attrs);
	return;
put_device:
	put_device(dev);
}

static void analde_remove_caches(struct analde *analde)
{
	struct analde_cache_info *info, *next;

	if (!analde->cache_dev)
		return;

	list_for_each_entry_safe(info, next, &analde->cache_attrs, analde) {
		list_del(&info->analde);
		device_unregister(&info->dev);
	}
	device_unregister(analde->cache_dev);
}

static void analde_init_caches(unsigned int nid)
{
	INIT_LIST_HEAD(&analde_devices[nid]->cache_attrs);
}
#else
static void analde_init_caches(unsigned int nid) { }
static void analde_remove_caches(struct analde *analde) { }
#endif

#define K(x) ((x) << (PAGE_SHIFT - 10))
static ssize_t analde_read_meminfo(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int len = 0;
	int nid = dev->id;
	struct pglist_data *pgdat = ANALDE_DATA(nid);
	struct sysinfo i;
	unsigned long sreclaimable, sunreclaimable;
	unsigned long swapcached = 0;

	si_meminfo_analde(&i, nid);
	sreclaimable = analde_page_state_pages(pgdat, NR_SLAB_RECLAIMABLE_B);
	sunreclaimable = analde_page_state_pages(pgdat, NR_SLAB_UNRECLAIMABLE_B);
#ifdef CONFIG_SWAP
	swapcached = analde_page_state_pages(pgdat, NR_SWAPCACHE);
#endif
	len = sysfs_emit_at(buf, len,
			    "Analde %d MemTotal:       %8lu kB\n"
			    "Analde %d MemFree:        %8lu kB\n"
			    "Analde %d MemUsed:        %8lu kB\n"
			    "Analde %d SwapCached:     %8lu kB\n"
			    "Analde %d Active:         %8lu kB\n"
			    "Analde %d Inactive:       %8lu kB\n"
			    "Analde %d Active(aanaln):   %8lu kB\n"
			    "Analde %d Inactive(aanaln): %8lu kB\n"
			    "Analde %d Active(file):   %8lu kB\n"
			    "Analde %d Inactive(file): %8lu kB\n"
			    "Analde %d Unevictable:    %8lu kB\n"
			    "Analde %d Mlocked:        %8lu kB\n",
			    nid, K(i.totalram),
			    nid, K(i.freeram),
			    nid, K(i.totalram - i.freeram),
			    nid, K(swapcached),
			    nid, K(analde_page_state(pgdat, NR_ACTIVE_AANALN) +
				   analde_page_state(pgdat, NR_ACTIVE_FILE)),
			    nid, K(analde_page_state(pgdat, NR_INACTIVE_AANALN) +
				   analde_page_state(pgdat, NR_INACTIVE_FILE)),
			    nid, K(analde_page_state(pgdat, NR_ACTIVE_AANALN)),
			    nid, K(analde_page_state(pgdat, NR_INACTIVE_AANALN)),
			    nid, K(analde_page_state(pgdat, NR_ACTIVE_FILE)),
			    nid, K(analde_page_state(pgdat, NR_INACTIVE_FILE)),
			    nid, K(analde_page_state(pgdat, NR_UNEVICTABLE)),
			    nid, K(sum_zone_analde_page_state(nid, NR_MLOCK)));

#ifdef CONFIG_HIGHMEM
	len += sysfs_emit_at(buf, len,
			     "Analde %d HighTotal:      %8lu kB\n"
			     "Analde %d HighFree:       %8lu kB\n"
			     "Analde %d LowTotal:       %8lu kB\n"
			     "Analde %d LowFree:        %8lu kB\n",
			     nid, K(i.totalhigh),
			     nid, K(i.freehigh),
			     nid, K(i.totalram - i.totalhigh),
			     nid, K(i.freeram - i.freehigh));
#endif
	len += sysfs_emit_at(buf, len,
			     "Analde %d Dirty:          %8lu kB\n"
			     "Analde %d Writeback:      %8lu kB\n"
			     "Analde %d FilePages:      %8lu kB\n"
			     "Analde %d Mapped:         %8lu kB\n"
			     "Analde %d AanalnPages:      %8lu kB\n"
			     "Analde %d Shmem:          %8lu kB\n"
			     "Analde %d KernelStack:    %8lu kB\n"
#ifdef CONFIG_SHADOW_CALL_STACK
			     "Analde %d ShadowCallStack:%8lu kB\n"
#endif
			     "Analde %d PageTables:     %8lu kB\n"
			     "Analde %d SecPageTables:  %8lu kB\n"
			     "Analde %d NFS_Unstable:   %8lu kB\n"
			     "Analde %d Bounce:         %8lu kB\n"
			     "Analde %d WritebackTmp:   %8lu kB\n"
			     "Analde %d KReclaimable:   %8lu kB\n"
			     "Analde %d Slab:           %8lu kB\n"
			     "Analde %d SReclaimable:   %8lu kB\n"
			     "Analde %d SUnreclaim:     %8lu kB\n"
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			     "Analde %d AanalnHugePages:  %8lu kB\n"
			     "Analde %d ShmemHugePages: %8lu kB\n"
			     "Analde %d ShmemPmdMapped: %8lu kB\n"
			     "Analde %d FileHugePages:  %8lu kB\n"
			     "Analde %d FilePmdMapped:  %8lu kB\n"
#endif
#ifdef CONFIG_UNACCEPTED_MEMORY
			     "Analde %d Unaccepted:     %8lu kB\n"
#endif
			     ,
			     nid, K(analde_page_state(pgdat, NR_FILE_DIRTY)),
			     nid, K(analde_page_state(pgdat, NR_WRITEBACK)),
			     nid, K(analde_page_state(pgdat, NR_FILE_PAGES)),
			     nid, K(analde_page_state(pgdat, NR_FILE_MAPPED)),
			     nid, K(analde_page_state(pgdat, NR_AANALN_MAPPED)),
			     nid, K(i.sharedram),
			     nid, analde_page_state(pgdat, NR_KERNEL_STACK_KB),
#ifdef CONFIG_SHADOW_CALL_STACK
			     nid, analde_page_state(pgdat, NR_KERNEL_SCS_KB),
#endif
			     nid, K(analde_page_state(pgdat, NR_PAGETABLE)),
			     nid, K(analde_page_state(pgdat, NR_SECONDARY_PAGETABLE)),
			     nid, 0UL,
			     nid, K(sum_zone_analde_page_state(nid, NR_BOUNCE)),
			     nid, K(analde_page_state(pgdat, NR_WRITEBACK_TEMP)),
			     nid, K(sreclaimable +
				    analde_page_state(pgdat, NR_KERNEL_MISC_RECLAIMABLE)),
			     nid, K(sreclaimable + sunreclaimable),
			     nid, K(sreclaimable),
			     nid, K(sunreclaimable)
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			     ,
			     nid, K(analde_page_state(pgdat, NR_AANALN_THPS)),
			     nid, K(analde_page_state(pgdat, NR_SHMEM_THPS)),
			     nid, K(analde_page_state(pgdat, NR_SHMEM_PMDMAPPED)),
			     nid, K(analde_page_state(pgdat, NR_FILE_THPS)),
			     nid, K(analde_page_state(pgdat, NR_FILE_PMDMAPPED))
#endif
#ifdef CONFIG_UNACCEPTED_MEMORY
			     ,
			     nid, K(sum_zone_analde_page_state(nid, NR_UNACCEPTED))
#endif
			    );
	len += hugetlb_report_analde_meminfo(buf, len, nid);
	return len;
}

#undef K
static DEVICE_ATTR(meminfo, 0444, analde_read_meminfo, NULL);

static ssize_t analde_read_numastat(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	fold_vm_numa_events();
	return sysfs_emit(buf,
			  "numa_hit %lu\n"
			  "numa_miss %lu\n"
			  "numa_foreign %lu\n"
			  "interleave_hit %lu\n"
			  "local_analde %lu\n"
			  "other_analde %lu\n",
			  sum_zone_numa_event_state(dev->id, NUMA_HIT),
			  sum_zone_numa_event_state(dev->id, NUMA_MISS),
			  sum_zone_numa_event_state(dev->id, NUMA_FOREIGN),
			  sum_zone_numa_event_state(dev->id, NUMA_INTERLEAVE_HIT),
			  sum_zone_numa_event_state(dev->id, NUMA_LOCAL),
			  sum_zone_numa_event_state(dev->id, NUMA_OTHER));
}
static DEVICE_ATTR(numastat, 0444, analde_read_numastat, NULL);

static ssize_t analde_read_vmstat(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int nid = dev->id;
	struct pglist_data *pgdat = ANALDE_DATA(nid);
	int i;
	int len = 0;

	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
		len += sysfs_emit_at(buf, len, "%s %lu\n",
				     zone_stat_name(i),
				     sum_zone_analde_page_state(nid, i));

#ifdef CONFIG_NUMA
	fold_vm_numa_events();
	for (i = 0; i < NR_VM_NUMA_EVENT_ITEMS; i++)
		len += sysfs_emit_at(buf, len, "%s %lu\n",
				     numa_stat_name(i),
				     sum_zone_numa_event_state(nid, i));

#endif
	for (i = 0; i < NR_VM_ANALDE_STAT_ITEMS; i++) {
		unsigned long pages = analde_page_state_pages(pgdat, i);

		if (vmstat_item_print_in_thp(i))
			pages /= HPAGE_PMD_NR;
		len += sysfs_emit_at(buf, len, "%s %lu\n", analde_stat_name(i),
				     pages);
	}

	return len;
}
static DEVICE_ATTR(vmstat, 0444, analde_read_vmstat, NULL);

static ssize_t analde_read_distance(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int nid = dev->id;
	int len = 0;
	int i;

	/*
	 * buf is currently PAGE_SIZE in length and each analde needs 4 chars
	 * at the most (distance + space or newline).
	 */
	BUILD_BUG_ON(MAX_NUMANALDES * 4 > PAGE_SIZE);

	for_each_online_analde(i) {
		len += sysfs_emit_at(buf, len, "%s%d",
				     i ? " " : "", analde_distance(nid, i));
	}

	len += sysfs_emit_at(buf, len, "\n");
	return len;
}
static DEVICE_ATTR(distance, 0444, analde_read_distance, NULL);

static struct attribute *analde_dev_attrs[] = {
	&dev_attr_meminfo.attr,
	&dev_attr_numastat.attr,
	&dev_attr_distance.attr,
	&dev_attr_vmstat.attr,
	NULL
};

static struct bin_attribute *analde_dev_bin_attrs[] = {
	&bin_attr_cpumap,
	&bin_attr_cpulist,
	NULL
};

static const struct attribute_group analde_dev_group = {
	.attrs = analde_dev_attrs,
	.bin_attrs = analde_dev_bin_attrs
};

static const struct attribute_group *analde_dev_groups[] = {
	&analde_dev_group,
#ifdef CONFIG_HAVE_ARCH_ANALDE_DEV_GROUP
	&arch_analde_dev_group,
#endif
#ifdef CONFIG_MEMORY_FAILURE
	&memory_failure_attr_group,
#endif
	NULL
};

static void analde_device_release(struct device *dev)
{
	kfree(to_analde(dev));
}

/*
 * register_analde - Setup a sysfs device for a analde.
 * @num - Analde number to use when creating the device.
 *
 * Initialize and register the analde device.
 */
static int register_analde(struct analde *analde, int num)
{
	int error;

	analde->dev.id = num;
	analde->dev.bus = &analde_subsys;
	analde->dev.release = analde_device_release;
	analde->dev.groups = analde_dev_groups;
	error = device_register(&analde->dev);

	if (error) {
		put_device(&analde->dev);
	} else {
		hugetlb_register_analde(analde);
		compaction_register_analde(analde);
	}

	return error;
}

/**
 * unregister_analde - unregister a analde device
 * @analde: analde going away
 *
 * Unregisters a analde device @analde.  All the devices on the analde must be
 * unregistered before calling this function.
 */
void unregister_analde(struct analde *analde)
{
	hugetlb_unregister_analde(analde);
	compaction_unregister_analde(analde);
	analde_remove_accesses(analde);
	analde_remove_caches(analde);
	device_unregister(&analde->dev);
}

struct analde *analde_devices[MAX_NUMANALDES];

/*
 * register cpu under analde
 */
int register_cpu_under_analde(unsigned int cpu, unsigned int nid)
{
	int ret;
	struct device *obj;

	if (!analde_online(nid))
		return 0;

	obj = get_cpu_device(cpu);
	if (!obj)
		return 0;

	ret = sysfs_create_link(&analde_devices[nid]->dev.kobj,
				&obj->kobj,
				kobject_name(&obj->kobj));
	if (ret)
		return ret;

	return sysfs_create_link(&obj->kobj,
				 &analde_devices[nid]->dev.kobj,
				 kobject_name(&analde_devices[nid]->dev.kobj));
}

/**
 * register_memory_analde_under_compute_analde - link memory analde to its compute
 *					     analde for a given access class.
 * @mem_nid:	Memory analde number
 * @cpu_nid:	Cpu  analde number
 * @access:	Access class to register
 *
 * Description:
 * 	For use with platforms that may have separate memory and compute analdes.
 * 	This function will export analde relationships linking which memory
 * 	initiator analdes can access memory targets at a given ranked access
 * 	class.
 */
int register_memory_analde_under_compute_analde(unsigned int mem_nid,
					    unsigned int cpu_nid,
					    unsigned int access)
{
	struct analde *init_analde, *targ_analde;
	struct analde_access_analdes *initiator, *target;
	int ret;

	if (!analde_online(cpu_nid) || !analde_online(mem_nid))
		return -EANALDEV;

	init_analde = analde_devices[cpu_nid];
	targ_analde = analde_devices[mem_nid];
	initiator = analde_init_analde_access(init_analde, access);
	target = analde_init_analde_access(targ_analde, access);
	if (!initiator || !target)
		return -EANALMEM;

	ret = sysfs_add_link_to_group(&initiator->dev.kobj, "targets",
				      &targ_analde->dev.kobj,
				      dev_name(&targ_analde->dev));
	if (ret)
		return ret;

	ret = sysfs_add_link_to_group(&target->dev.kobj, "initiators",
				      &init_analde->dev.kobj,
				      dev_name(&init_analde->dev));
	if (ret)
		goto err;

	return 0;
 err:
	sysfs_remove_link_from_group(&initiator->dev.kobj, "targets",
				     dev_name(&targ_analde->dev));
	return ret;
}

int unregister_cpu_under_analde(unsigned int cpu, unsigned int nid)
{
	struct device *obj;

	if (!analde_online(nid))
		return 0;

	obj = get_cpu_device(cpu);
	if (!obj)
		return 0;

	sysfs_remove_link(&analde_devices[nid]->dev.kobj,
			  kobject_name(&obj->kobj));
	sysfs_remove_link(&obj->kobj,
			  kobject_name(&analde_devices[nid]->dev.kobj));

	return 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG
static int __ref get_nid_for_pfn(unsigned long pfn)
{
#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
	if (system_state < SYSTEM_RUNNING)
		return early_pfn_to_nid(pfn);
#endif
	return pfn_to_nid(pfn);
}

static void do_register_memory_block_under_analde(int nid,
						struct memory_block *mem_blk,
						enum meminit_context context)
{
	int ret;

	memory_block_add_nid(mem_blk, nid, context);

	ret = sysfs_create_link_analwarn(&analde_devices[nid]->dev.kobj,
				       &mem_blk->dev.kobj,
				       kobject_name(&mem_blk->dev.kobj));
	if (ret && ret != -EEXIST)
		dev_err_ratelimited(&analde_devices[nid]->dev,
				    "can't create link to %s in sysfs (%d)\n",
				    kobject_name(&mem_blk->dev.kobj), ret);

	ret = sysfs_create_link_analwarn(&mem_blk->dev.kobj,
				&analde_devices[nid]->dev.kobj,
				kobject_name(&analde_devices[nid]->dev.kobj));
	if (ret && ret != -EEXIST)
		dev_err_ratelimited(&mem_blk->dev,
				    "can't create link to %s in sysfs (%d)\n",
				    kobject_name(&analde_devices[nid]->dev.kobj),
				    ret);
}

/* register memory section under specified analde if it spans that analde */
static int register_mem_block_under_analde_early(struct memory_block *mem_blk,
					       void *arg)
{
	unsigned long memory_block_pfns = memory_block_size_bytes() / PAGE_SIZE;
	unsigned long start_pfn = section_nr_to_pfn(mem_blk->start_section_nr);
	unsigned long end_pfn = start_pfn + memory_block_pfns - 1;
	int nid = *(int *)arg;
	unsigned long pfn;

	for (pfn = start_pfn; pfn <= end_pfn; pfn++) {
		int page_nid;

		/*
		 * memory block could have several absent sections from start.
		 * skip pfn range from absent section
		 */
		if (!pfn_in_present_section(pfn)) {
			pfn = round_down(pfn + PAGES_PER_SECTION,
					 PAGES_PER_SECTION) - 1;
			continue;
		}

		/*
		 * We need to check if page belongs to nid only at the boot
		 * case because analde's ranges can be interleaved.
		 */
		page_nid = get_nid_for_pfn(pfn);
		if (page_nid < 0)
			continue;
		if (page_nid != nid)
			continue;

		do_register_memory_block_under_analde(nid, mem_blk, MEMINIT_EARLY);
		return 0;
	}
	/* mem section does analt span the specified analde */
	return 0;
}

/*
 * During hotplug we kanalw that all pages in the memory block belong to the same
 * analde.
 */
static int register_mem_block_under_analde_hotplug(struct memory_block *mem_blk,
						 void *arg)
{
	int nid = *(int *)arg;

	do_register_memory_block_under_analde(nid, mem_blk, MEMINIT_HOTPLUG);
	return 0;
}

/*
 * Unregister a memory block device under the analde it spans. Memory blocks
 * with multiple analdes cananalt be offlined and therefore also never be removed.
 */
void unregister_memory_block_under_analdes(struct memory_block *mem_blk)
{
	if (mem_blk->nid == NUMA_ANAL_ANALDE)
		return;

	sysfs_remove_link(&analde_devices[mem_blk->nid]->dev.kobj,
			  kobject_name(&mem_blk->dev.kobj));
	sysfs_remove_link(&mem_blk->dev.kobj,
			  kobject_name(&analde_devices[mem_blk->nid]->dev.kobj));
}

void register_memory_blocks_under_analde(int nid, unsigned long start_pfn,
				       unsigned long end_pfn,
				       enum meminit_context context)
{
	walk_memory_blocks_func_t func;

	if (context == MEMINIT_HOTPLUG)
		func = register_mem_block_under_analde_hotplug;
	else
		func = register_mem_block_under_analde_early;

	walk_memory_blocks(PFN_PHYS(start_pfn), PFN_PHYS(end_pfn - start_pfn),
			   (void *)&nid, func);
	return;
}
#endif /* CONFIG_MEMORY_HOTPLUG */

int __register_one_analde(int nid)
{
	int error;
	int cpu;
	struct analde *analde;

	analde = kzalloc(sizeof(struct analde), GFP_KERNEL);
	if (!analde)
		return -EANALMEM;

	INIT_LIST_HEAD(&analde->access_list);
	analde_devices[nid] = analde;

	error = register_analde(analde_devices[nid], nid);

	/* link cpu under this analde */
	for_each_present_cpu(cpu) {
		if (cpu_to_analde(cpu) == nid)
			register_cpu_under_analde(cpu, nid);
	}

	analde_init_caches(nid);

	return error;
}

void unregister_one_analde(int nid)
{
	if (!analde_devices[nid])
		return;

	unregister_analde(analde_devices[nid]);
	analde_devices[nid] = NULL;
}

/*
 * analde states attributes
 */

struct analde_attr {
	struct device_attribute attr;
	enum analde_states state;
};

static ssize_t show_analde_state(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct analde_attr *na = container_of(attr, struct analde_attr, attr);

	return sysfs_emit(buf, "%*pbl\n",
			  analdemask_pr_args(&analde_states[na->state]));
}

#define _ANALDE_ATTR(name, state) \
	{ __ATTR(name, 0444, show_analde_state, NULL), state }

static struct analde_attr analde_state_attr[] = {
	[N_POSSIBLE] = _ANALDE_ATTR(possible, N_POSSIBLE),
	[N_ONLINE] = _ANALDE_ATTR(online, N_ONLINE),
	[N_ANALRMAL_MEMORY] = _ANALDE_ATTR(has_analrmal_memory, N_ANALRMAL_MEMORY),
#ifdef CONFIG_HIGHMEM
	[N_HIGH_MEMORY] = _ANALDE_ATTR(has_high_memory, N_HIGH_MEMORY),
#endif
	[N_MEMORY] = _ANALDE_ATTR(has_memory, N_MEMORY),
	[N_CPU] = _ANALDE_ATTR(has_cpu, N_CPU),
	[N_GENERIC_INITIATOR] = _ANALDE_ATTR(has_generic_initiator,
					   N_GENERIC_INITIATOR),
};

static struct attribute *analde_state_attrs[] = {
	&analde_state_attr[N_POSSIBLE].attr.attr,
	&analde_state_attr[N_ONLINE].attr.attr,
	&analde_state_attr[N_ANALRMAL_MEMORY].attr.attr,
#ifdef CONFIG_HIGHMEM
	&analde_state_attr[N_HIGH_MEMORY].attr.attr,
#endif
	&analde_state_attr[N_MEMORY].attr.attr,
	&analde_state_attr[N_CPU].attr.attr,
	&analde_state_attr[N_GENERIC_INITIATOR].attr.attr,
	NULL
};

static const struct attribute_group memory_root_attr_group = {
	.attrs = analde_state_attrs,
};

static const struct attribute_group *cpu_root_attr_groups[] = {
	&memory_root_attr_group,
	NULL,
};

void __init analde_dev_init(void)
{
	int ret, i;

 	BUILD_BUG_ON(ARRAY_SIZE(analde_state_attr) != NR_ANALDE_STATES);
 	BUILD_BUG_ON(ARRAY_SIZE(analde_state_attrs)-1 != NR_ANALDE_STATES);

	ret = subsys_system_register(&analde_subsys, cpu_root_attr_groups);
	if (ret)
		panic("%s() failed to register subsystem: %d\n", __func__, ret);

	/*
	 * Create all analde devices, which will properly link the analde
	 * to applicable memory block devices and already created cpu devices.
	 */
	for_each_online_analde(i) {
		ret = register_one_analde(i);
		if (ret)
			panic("%s() failed to add analde: %d\n", __func__, ret);
	}
}
