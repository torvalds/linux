// SPDX-License-Identifier: GPL-2.0
/*
 * Basic Node interface support
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/vmstat.h>
#include <linux/yestifier.h>
#include <linux/yesde.h>
#include <linux/hugetlb.h>
#include <linux/compaction.h>
#include <linux/cpumask.h>
#include <linux/topology.h>
#include <linux/yesdemask.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/swap.h>
#include <linux/slab.h>

static struct bus_type yesde_subsys = {
	.name = "yesde",
	.dev_name = "yesde",
};


static ssize_t yesde_read_cpumap(struct device *dev, bool list, char *buf)
{
	ssize_t n;
	cpumask_var_t mask;
	struct yesde *yesde_dev = to_yesde(dev);

	/* 2008/04/07: buf currently PAGE_SIZE, need 9 chars per 32 bits. */
	BUILD_BUG_ON((NR_CPUS/32 * 9) > (PAGE_SIZE-1));

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return 0;

	cpumask_and(mask, cpumask_of_yesde(yesde_dev->dev.id), cpu_online_mask);
	n = cpumap_print_to_pagebuf(list, buf, mask);
	free_cpumask_var(mask);

	return n;
}

static inline ssize_t yesde_read_cpumask(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return yesde_read_cpumap(dev, false, buf);
}
static inline ssize_t yesde_read_cpulist(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return yesde_read_cpumap(dev, true, buf);
}

static DEVICE_ATTR(cpumap,  S_IRUGO, yesde_read_cpumask, NULL);
static DEVICE_ATTR(cpulist, S_IRUGO, yesde_read_cpulist, NULL);

/**
 * struct yesde_access_yesdes - Access class device to hold user visible
 * 			      relationships to other yesdes.
 * @dev:	Device for this memory access class
 * @list_yesde:	List element in the yesde's access list
 * @access:	The access class rank
 * @hmem_attrs: Heterogeneous memory performance attributes
 */
struct yesde_access_yesdes {
	struct device		dev;
	struct list_head	list_yesde;
	unsigned		access;
#ifdef CONFIG_HMEM_REPORTING
	struct yesde_hmem_attrs	hmem_attrs;
#endif
};
#define to_access_yesdes(dev) container_of(dev, struct yesde_access_yesdes, dev)

static struct attribute *yesde_init_access_yesde_attrs[] = {
	NULL,
};

static struct attribute *yesde_targ_access_yesde_attrs[] = {
	NULL,
};

static const struct attribute_group initiators = {
	.name	= "initiators",
	.attrs	= yesde_init_access_yesde_attrs,
};

static const struct attribute_group targets = {
	.name	= "targets",
	.attrs	= yesde_targ_access_yesde_attrs,
};

static const struct attribute_group *yesde_access_yesde_groups[] = {
	&initiators,
	&targets,
	NULL,
};

static void yesde_remove_accesses(struct yesde *yesde)
{
	struct yesde_access_yesdes *c, *cnext;

	list_for_each_entry_safe(c, cnext, &yesde->access_list, list_yesde) {
		list_del(&c->list_yesde);
		device_unregister(&c->dev);
	}
}

static void yesde_access_release(struct device *dev)
{
	kfree(to_access_yesdes(dev));
}

static struct yesde_access_yesdes *yesde_init_yesde_access(struct yesde *yesde,
						       unsigned access)
{
	struct yesde_access_yesdes *access_yesde;
	struct device *dev;

	list_for_each_entry(access_yesde, &yesde->access_list, list_yesde)
		if (access_yesde->access == access)
			return access_yesde;

	access_yesde = kzalloc(sizeof(*access_yesde), GFP_KERNEL);
	if (!access_yesde)
		return NULL;

	access_yesde->access = access;
	dev = &access_yesde->dev;
	dev->parent = &yesde->dev;
	dev->release = yesde_access_release;
	dev->groups = yesde_access_yesde_groups;
	if (dev_set_name(dev, "access%u", access))
		goto free;

	if (device_register(dev))
		goto free_name;

	pm_runtime_yes_callbacks(dev);
	list_add_tail(&access_yesde->list_yesde, &yesde->access_list);
	return access_yesde;
free_name:
	kfree_const(dev->kobj.name);
free:
	kfree(access_yesde);
	return NULL;
}

#ifdef CONFIG_HMEM_REPORTING
#define ACCESS_ATTR(name) 						   \
static ssize_t name##_show(struct device *dev,				   \
			   struct device_attribute *attr,		   \
			   char *buf)					   \
{									   \
	return sprintf(buf, "%u\n", to_access_yesdes(dev)->hmem_attrs.name); \
}									   \
static DEVICE_ATTR_RO(name);

ACCESS_ATTR(read_bandwidth)
ACCESS_ATTR(read_latency)
ACCESS_ATTR(write_bandwidth)
ACCESS_ATTR(write_latency)

static struct attribute *access_attrs[] = {
	&dev_attr_read_bandwidth.attr,
	&dev_attr_read_latency.attr,
	&dev_attr_write_bandwidth.attr,
	&dev_attr_write_latency.attr,
	NULL,
};

/**
 * yesde_set_perf_attrs - Set the performance values for given access class
 * @nid: Node identifier to be set
 * @hmem_attrs: Heterogeneous memory performance attributes
 * @access: The access class the for the given attributes
 */
void yesde_set_perf_attrs(unsigned int nid, struct yesde_hmem_attrs *hmem_attrs,
			 unsigned access)
{
	struct yesde_access_yesdes *c;
	struct yesde *yesde;
	int i;

	if (WARN_ON_ONCE(!yesde_online(nid)))
		return;

	yesde = yesde_devices[nid];
	c = yesde_init_yesde_access(yesde, access);
	if (!c)
		return;

	c->hmem_attrs = *hmem_attrs;
	for (i = 0; access_attrs[i] != NULL; i++) {
		if (sysfs_add_file_to_group(&c->dev.kobj, access_attrs[i],
					    "initiators")) {
			pr_info("failed to add performance attribute to yesde %d\n",
				nid);
			break;
		}
	}
}

/**
 * struct yesde_cache_info - Internal tracking for memory yesde caches
 * @dev:	Device represeting the cache level
 * @yesde:	List element for tracking in the yesde
 * @cache_attrs:Attributes for this cache level
 */
struct yesde_cache_info {
	struct device dev;
	struct list_head yesde;
	struct yesde_cache_attrs cache_attrs;
};
#define to_cache_info(device) container_of(device, struct yesde_cache_info, dev)

#define CACHE_ATTR(name, fmt) 						\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr,		\
			   char *buf)					\
{									\
	return sprintf(buf, fmt "\n", to_cache_info(dev)->cache_attrs.name);\
}									\
DEVICE_ATTR_RO(name);

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

static void yesde_cache_release(struct device *dev)
{
	kfree(dev);
}

static void yesde_cacheinfo_release(struct device *dev)
{
	struct yesde_cache_info *info = to_cache_info(dev);
	kfree(info);
}

static void yesde_init_cache_dev(struct yesde *yesde)
{
	struct device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return;

	dev->parent = &yesde->dev;
	dev->release = yesde_cache_release;
	if (dev_set_name(dev, "memory_side_cache"))
		goto free_dev;

	if (device_register(dev))
		goto free_name;

	pm_runtime_yes_callbacks(dev);
	yesde->cache_dev = dev;
	return;
free_name:
	kfree_const(dev->kobj.name);
free_dev:
	kfree(dev);
}

/**
 * yesde_add_cache() - add cache attribute to a memory yesde
 * @nid: Node identifier that has new cache attributes
 * @cache_attrs: Attributes for the cache being added
 */
void yesde_add_cache(unsigned int nid, struct yesde_cache_attrs *cache_attrs)
{
	struct yesde_cache_info *info;
	struct device *dev;
	struct yesde *yesde;

	if (!yesde_online(nid) || !yesde_devices[nid])
		return;

	yesde = yesde_devices[nid];
	list_for_each_entry(info, &yesde->cache_attrs, yesde) {
		if (info->cache_attrs.level == cache_attrs->level) {
			dev_warn(&yesde->dev,
				"attempt to add duplicate cache level:%d\n",
				cache_attrs->level);
			return;
		}
	}

	if (!yesde->cache_dev)
		yesde_init_cache_dev(yesde);
	if (!yesde->cache_dev)
		return;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return;

	dev = &info->dev;
	dev->parent = yesde->cache_dev;
	dev->release = yesde_cacheinfo_release;
	dev->groups = cache_groups;
	if (dev_set_name(dev, "index%d", cache_attrs->level))
		goto free_cache;

	info->cache_attrs = *cache_attrs;
	if (device_register(dev)) {
		dev_warn(&yesde->dev, "failed to add cache level:%d\n",
			 cache_attrs->level);
		goto free_name;
	}
	pm_runtime_yes_callbacks(dev);
	list_add_tail(&info->yesde, &yesde->cache_attrs);
	return;
free_name:
	kfree_const(dev->kobj.name);
free_cache:
	kfree(info);
}

static void yesde_remove_caches(struct yesde *yesde)
{
	struct yesde_cache_info *info, *next;

	if (!yesde->cache_dev)
		return;

	list_for_each_entry_safe(info, next, &yesde->cache_attrs, yesde) {
		list_del(&info->yesde);
		device_unregister(&info->dev);
	}
	device_unregister(yesde->cache_dev);
}

static void yesde_init_caches(unsigned int nid)
{
	INIT_LIST_HEAD(&yesde_devices[nid]->cache_attrs);
}
#else
static void yesde_init_caches(unsigned int nid) { }
static void yesde_remove_caches(struct yesde *yesde) { }
#endif

#define K(x) ((x) << (PAGE_SHIFT - 10))
static ssize_t yesde_read_meminfo(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int n;
	int nid = dev->id;
	struct pglist_data *pgdat = NODE_DATA(nid);
	struct sysinfo i;
	unsigned long sreclaimable, sunreclaimable;

	si_meminfo_yesde(&i, nid);
	sreclaimable = yesde_page_state(pgdat, NR_SLAB_RECLAIMABLE);
	sunreclaimable = yesde_page_state(pgdat, NR_SLAB_UNRECLAIMABLE);
	n = sprintf(buf,
		       "Node %d MemTotal:       %8lu kB\n"
		       "Node %d MemFree:        %8lu kB\n"
		       "Node %d MemUsed:        %8lu kB\n"
		       "Node %d Active:         %8lu kB\n"
		       "Node %d Inactive:       %8lu kB\n"
		       "Node %d Active(ayesn):   %8lu kB\n"
		       "Node %d Inactive(ayesn): %8lu kB\n"
		       "Node %d Active(file):   %8lu kB\n"
		       "Node %d Inactive(file): %8lu kB\n"
		       "Node %d Unevictable:    %8lu kB\n"
		       "Node %d Mlocked:        %8lu kB\n",
		       nid, K(i.totalram),
		       nid, K(i.freeram),
		       nid, K(i.totalram - i.freeram),
		       nid, K(yesde_page_state(pgdat, NR_ACTIVE_ANON) +
				yesde_page_state(pgdat, NR_ACTIVE_FILE)),
		       nid, K(yesde_page_state(pgdat, NR_INACTIVE_ANON) +
				yesde_page_state(pgdat, NR_INACTIVE_FILE)),
		       nid, K(yesde_page_state(pgdat, NR_ACTIVE_ANON)),
		       nid, K(yesde_page_state(pgdat, NR_INACTIVE_ANON)),
		       nid, K(yesde_page_state(pgdat, NR_ACTIVE_FILE)),
		       nid, K(yesde_page_state(pgdat, NR_INACTIVE_FILE)),
		       nid, K(yesde_page_state(pgdat, NR_UNEVICTABLE)),
		       nid, K(sum_zone_yesde_page_state(nid, NR_MLOCK)));

#ifdef CONFIG_HIGHMEM
	n += sprintf(buf + n,
		       "Node %d HighTotal:      %8lu kB\n"
		       "Node %d HighFree:       %8lu kB\n"
		       "Node %d LowTotal:       %8lu kB\n"
		       "Node %d LowFree:        %8lu kB\n",
		       nid, K(i.totalhigh),
		       nid, K(i.freehigh),
		       nid, K(i.totalram - i.totalhigh),
		       nid, K(i.freeram - i.freehigh));
#endif
	n += sprintf(buf + n,
		       "Node %d Dirty:          %8lu kB\n"
		       "Node %d Writeback:      %8lu kB\n"
		       "Node %d FilePages:      %8lu kB\n"
		       "Node %d Mapped:         %8lu kB\n"
		       "Node %d AyesnPages:      %8lu kB\n"
		       "Node %d Shmem:          %8lu kB\n"
		       "Node %d KernelStack:    %8lu kB\n"
		       "Node %d PageTables:     %8lu kB\n"
		       "Node %d NFS_Unstable:   %8lu kB\n"
		       "Node %d Bounce:         %8lu kB\n"
		       "Node %d WritebackTmp:   %8lu kB\n"
		       "Node %d KReclaimable:   %8lu kB\n"
		       "Node %d Slab:           %8lu kB\n"
		       "Node %d SReclaimable:   %8lu kB\n"
		       "Node %d SUnreclaim:     %8lu kB\n"
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		       "Node %d AyesnHugePages:  %8lu kB\n"
		       "Node %d ShmemHugePages: %8lu kB\n"
		       "Node %d ShmemPmdMapped: %8lu kB\n"
		       "Node %d FileHugePages: %8lu kB\n"
		       "Node %d FilePmdMapped: %8lu kB\n"
#endif
			,
		       nid, K(yesde_page_state(pgdat, NR_FILE_DIRTY)),
		       nid, K(yesde_page_state(pgdat, NR_WRITEBACK)),
		       nid, K(yesde_page_state(pgdat, NR_FILE_PAGES)),
		       nid, K(yesde_page_state(pgdat, NR_FILE_MAPPED)),
		       nid, K(yesde_page_state(pgdat, NR_ANON_MAPPED)),
		       nid, K(i.sharedram),
		       nid, sum_zone_yesde_page_state(nid, NR_KERNEL_STACK_KB),
		       nid, K(sum_zone_yesde_page_state(nid, NR_PAGETABLE)),
		       nid, K(yesde_page_state(pgdat, NR_UNSTABLE_NFS)),
		       nid, K(sum_zone_yesde_page_state(nid, NR_BOUNCE)),
		       nid, K(yesde_page_state(pgdat, NR_WRITEBACK_TEMP)),
		       nid, K(sreclaimable +
			      yesde_page_state(pgdat, NR_KERNEL_MISC_RECLAIMABLE)),
		       nid, K(sreclaimable + sunreclaimable),
		       nid, K(sreclaimable),
		       nid, K(sunreclaimable)
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		       ,
		       nid, K(yesde_page_state(pgdat, NR_ANON_THPS) *
				       HPAGE_PMD_NR),
		       nid, K(yesde_page_state(pgdat, NR_SHMEM_THPS) *
				       HPAGE_PMD_NR),
		       nid, K(yesde_page_state(pgdat, NR_SHMEM_PMDMAPPED) *
				       HPAGE_PMD_NR),
		       nid, K(yesde_page_state(pgdat, NR_FILE_THPS) *
				       HPAGE_PMD_NR),
		       nid, K(yesde_page_state(pgdat, NR_FILE_PMDMAPPED) *
				       HPAGE_PMD_NR)
#endif
		       );
	n += hugetlb_report_yesde_meminfo(nid, buf + n);
	return n;
}

#undef K
static DEVICE_ATTR(meminfo, S_IRUGO, yesde_read_meminfo, NULL);

static ssize_t yesde_read_numastat(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf,
		       "numa_hit %lu\n"
		       "numa_miss %lu\n"
		       "numa_foreign %lu\n"
		       "interleave_hit %lu\n"
		       "local_yesde %lu\n"
		       "other_yesde %lu\n",
		       sum_zone_numa_state(dev->id, NUMA_HIT),
		       sum_zone_numa_state(dev->id, NUMA_MISS),
		       sum_zone_numa_state(dev->id, NUMA_FOREIGN),
		       sum_zone_numa_state(dev->id, NUMA_INTERLEAVE_HIT),
		       sum_zone_numa_state(dev->id, NUMA_LOCAL),
		       sum_zone_numa_state(dev->id, NUMA_OTHER));
}
static DEVICE_ATTR(numastat, S_IRUGO, yesde_read_numastat, NULL);

static ssize_t yesde_read_vmstat(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int nid = dev->id;
	struct pglist_data *pgdat = NODE_DATA(nid);
	int i;
	int n = 0;

	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
		n += sprintf(buf+n, "%s %lu\n", zone_stat_name(i),
			     sum_zone_yesde_page_state(nid, i));

#ifdef CONFIG_NUMA
	for (i = 0; i < NR_VM_NUMA_STAT_ITEMS; i++)
		n += sprintf(buf+n, "%s %lu\n", numa_stat_name(i),
			     sum_zone_numa_state(nid, i));
#endif

	for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++)
		n += sprintf(buf+n, "%s %lu\n", yesde_stat_name(i),
			     yesde_page_state(pgdat, i));

	return n;
}
static DEVICE_ATTR(vmstat, S_IRUGO, yesde_read_vmstat, NULL);

static ssize_t yesde_read_distance(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int nid = dev->id;
	int len = 0;
	int i;

	/*
	 * buf is currently PAGE_SIZE in length and each yesde needs 4 chars
	 * at the most (distance + space or newline).
	 */
	BUILD_BUG_ON(MAX_NUMNODES * 4 > PAGE_SIZE);

	for_each_online_yesde(i)
		len += sprintf(buf + len, "%s%d", i ? " " : "", yesde_distance(nid, i));

	len += sprintf(buf + len, "\n");
	return len;
}
static DEVICE_ATTR(distance, S_IRUGO, yesde_read_distance, NULL);

static struct attribute *yesde_dev_attrs[] = {
	&dev_attr_cpumap.attr,
	&dev_attr_cpulist.attr,
	&dev_attr_meminfo.attr,
	&dev_attr_numastat.attr,
	&dev_attr_distance.attr,
	&dev_attr_vmstat.attr,
	NULL
};
ATTRIBUTE_GROUPS(yesde_dev);

#ifdef CONFIG_HUGETLBFS
/*
 * hugetlbfs per yesde attributes registration interface:
 * When/if hugetlb[fs] subsystem initializes [sometime after this module],
 * it will register its per yesde attributes for all online yesdes with
 * memory.  It will also call register_hugetlbfs_with_yesde(), below, to
 * register its attribute registration functions with this yesde driver.
 * Once these hooks have been initialized, the yesde driver will call into
 * the hugetlb module to [un]register attributes for hot-plugged yesdes.
 */
static yesde_registration_func_t __hugetlb_register_yesde;
static yesde_registration_func_t __hugetlb_unregister_yesde;

static inline bool hugetlb_register_yesde(struct yesde *yesde)
{
	if (__hugetlb_register_yesde &&
			yesde_state(yesde->dev.id, N_MEMORY)) {
		__hugetlb_register_yesde(yesde);
		return true;
	}
	return false;
}

static inline void hugetlb_unregister_yesde(struct yesde *yesde)
{
	if (__hugetlb_unregister_yesde)
		__hugetlb_unregister_yesde(yesde);
}

void register_hugetlbfs_with_yesde(yesde_registration_func_t doregister,
				  yesde_registration_func_t unregister)
{
	__hugetlb_register_yesde   = doregister;
	__hugetlb_unregister_yesde = unregister;
}
#else
static inline void hugetlb_register_yesde(struct yesde *yesde) {}

static inline void hugetlb_unregister_yesde(struct yesde *yesde) {}
#endif

static void yesde_device_release(struct device *dev)
{
	struct yesde *yesde = to_yesde(dev);

#if defined(CONFIG_MEMORY_HOTPLUG_SPARSE) && defined(CONFIG_HUGETLBFS)
	/*
	 * We schedule the work only when a memory section is
	 * onlined/offlined on this yesde. When we come here,
	 * all the memory on this yesde has been offlined,
	 * so we won't enqueue new work to this work.
	 *
	 * The work is using yesde->yesde_work, so we should
	 * flush work before freeing the memory.
	 */
	flush_work(&yesde->yesde_work);
#endif
	kfree(yesde);
}

/*
 * register_yesde - Setup a sysfs device for a yesde.
 * @num - Node number to use when creating the device.
 *
 * Initialize and register the yesde device.
 */
static int register_yesde(struct yesde *yesde, int num)
{
	int error;

	yesde->dev.id = num;
	yesde->dev.bus = &yesde_subsys;
	yesde->dev.release = yesde_device_release;
	yesde->dev.groups = yesde_dev_groups;
	error = device_register(&yesde->dev);

	if (error)
		put_device(&yesde->dev);
	else {
		hugetlb_register_yesde(yesde);

		compaction_register_yesde(yesde);
	}
	return error;
}

/**
 * unregister_yesde - unregister a yesde device
 * @yesde: yesde going away
 *
 * Unregisters a yesde device @yesde.  All the devices on the yesde must be
 * unregistered before calling this function.
 */
void unregister_yesde(struct yesde *yesde)
{
	hugetlb_unregister_yesde(yesde);		/* yes-op, if memoryless yesde */
	yesde_remove_accesses(yesde);
	yesde_remove_caches(yesde);
	device_unregister(&yesde->dev);
}

struct yesde *yesde_devices[MAX_NUMNODES];

/*
 * register cpu under yesde
 */
int register_cpu_under_yesde(unsigned int cpu, unsigned int nid)
{
	int ret;
	struct device *obj;

	if (!yesde_online(nid))
		return 0;

	obj = get_cpu_device(cpu);
	if (!obj)
		return 0;

	ret = sysfs_create_link(&yesde_devices[nid]->dev.kobj,
				&obj->kobj,
				kobject_name(&obj->kobj));
	if (ret)
		return ret;

	return sysfs_create_link(&obj->kobj,
				 &yesde_devices[nid]->dev.kobj,
				 kobject_name(&yesde_devices[nid]->dev.kobj));
}

/**
 * register_memory_yesde_under_compute_yesde - link memory yesde to its compute
 *					     yesde for a given access class.
 * @mem_nid:	Memory yesde number
 * @cpu_nid:	Cpu  yesde number
 * @access:	Access class to register
 *
 * Description:
 * 	For use with platforms that may have separate memory and compute yesdes.
 * 	This function will export yesde relationships linking which memory
 * 	initiator yesdes can access memory targets at a given ranked access
 * 	class.
 */
int register_memory_yesde_under_compute_yesde(unsigned int mem_nid,
					    unsigned int cpu_nid,
					    unsigned access)
{
	struct yesde *init_yesde, *targ_yesde;
	struct yesde_access_yesdes *initiator, *target;
	int ret;

	if (!yesde_online(cpu_nid) || !yesde_online(mem_nid))
		return -ENODEV;

	init_yesde = yesde_devices[cpu_nid];
	targ_yesde = yesde_devices[mem_nid];
	initiator = yesde_init_yesde_access(init_yesde, access);
	target = yesde_init_yesde_access(targ_yesde, access);
	if (!initiator || !target)
		return -ENOMEM;

	ret = sysfs_add_link_to_group(&initiator->dev.kobj, "targets",
				      &targ_yesde->dev.kobj,
				      dev_name(&targ_yesde->dev));
	if (ret)
		return ret;

	ret = sysfs_add_link_to_group(&target->dev.kobj, "initiators",
				      &init_yesde->dev.kobj,
				      dev_name(&init_yesde->dev));
	if (ret)
		goto err;

	return 0;
 err:
	sysfs_remove_link_from_group(&initiator->dev.kobj, "targets",
				     dev_name(&targ_yesde->dev));
	return ret;
}

int unregister_cpu_under_yesde(unsigned int cpu, unsigned int nid)
{
	struct device *obj;

	if (!yesde_online(nid))
		return 0;

	obj = get_cpu_device(cpu);
	if (!obj)
		return 0;

	sysfs_remove_link(&yesde_devices[nid]->dev.kobj,
			  kobject_name(&obj->kobj));
	sysfs_remove_link(&obj->kobj,
			  kobject_name(&yesde_devices[nid]->dev.kobj));

	return 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE
static int __ref get_nid_for_pfn(unsigned long pfn)
{
	if (!pfn_valid_within(pfn))
		return -1;
#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
	if (system_state < SYSTEM_RUNNING)
		return early_pfn_to_nid(pfn);
#endif
	return pfn_to_nid(pfn);
}

/* register memory section under specified yesde if it spans that yesde */
static int register_mem_sect_under_yesde(struct memory_block *mem_blk,
					 void *arg)
{
	unsigned long memory_block_pfns = memory_block_size_bytes() / PAGE_SIZE;
	unsigned long start_pfn = section_nr_to_pfn(mem_blk->start_section_nr);
	unsigned long end_pfn = start_pfn + memory_block_pfns - 1;
	int ret, nid = *(int *)arg;
	unsigned long pfn;

	for (pfn = start_pfn; pfn <= end_pfn; pfn++) {
		int page_nid;

		/*
		 * memory block could have several absent sections from start.
		 * skip pfn range from absent section
		 */
		if (!pfn_present(pfn)) {
			pfn = round_down(pfn + PAGES_PER_SECTION,
					 PAGES_PER_SECTION) - 1;
			continue;
		}

		/*
		 * We need to check if page belongs to nid only for the boot
		 * case, during hotplug we kyesw that all pages in the memory
		 * block belong to the same yesde.
		 */
		if (system_state == SYSTEM_BOOTING) {
			page_nid = get_nid_for_pfn(pfn);
			if (page_nid < 0)
				continue;
			if (page_nid != nid)
				continue;
		}

		/*
		 * If this memory block spans multiple yesdes, we only indicate
		 * the last processed yesde.
		 */
		mem_blk->nid = nid;

		ret = sysfs_create_link_yeswarn(&yesde_devices[nid]->dev.kobj,
					&mem_blk->dev.kobj,
					kobject_name(&mem_blk->dev.kobj));
		if (ret)
			return ret;

		return sysfs_create_link_yeswarn(&mem_blk->dev.kobj,
				&yesde_devices[nid]->dev.kobj,
				kobject_name(&yesde_devices[nid]->dev.kobj));
	}
	/* mem section does yest span the specified yesde */
	return 0;
}

/*
 * Unregister a memory block device under the yesde it spans. Memory blocks
 * with multiple yesdes canyest be offlined and therefore also never be removed.
 */
void unregister_memory_block_under_yesdes(struct memory_block *mem_blk)
{
	if (mem_blk->nid == NUMA_NO_NODE)
		return;

	sysfs_remove_link(&yesde_devices[mem_blk->nid]->dev.kobj,
			  kobject_name(&mem_blk->dev.kobj));
	sysfs_remove_link(&mem_blk->dev.kobj,
			  kobject_name(&yesde_devices[mem_blk->nid]->dev.kobj));
}

int link_mem_sections(int nid, unsigned long start_pfn, unsigned long end_pfn)
{
	return walk_memory_blocks(PFN_PHYS(start_pfn),
				  PFN_PHYS(end_pfn - start_pfn), (void *)&nid,
				  register_mem_sect_under_yesde);
}

#ifdef CONFIG_HUGETLBFS
/*
 * Handle per yesde hstate attribute [un]registration on transistions
 * to/from memoryless state.
 */
static void yesde_hugetlb_work(struct work_struct *work)
{
	struct yesde *yesde = container_of(work, struct yesde, yesde_work);

	/*
	 * We only get here when a yesde transitions to/from memoryless state.
	 * We can detect which transition occurred by examining whether the
	 * yesde has memory yesw.  hugetlb_register_yesde() already check this
	 * so we try to register the attributes.  If that fails, then the
	 * yesde has transitioned to memoryless, try to unregister the
	 * attributes.
	 */
	if (!hugetlb_register_yesde(yesde))
		hugetlb_unregister_yesde(yesde);
}

static void init_yesde_hugetlb_work(int nid)
{
	INIT_WORK(&yesde_devices[nid]->yesde_work, yesde_hugetlb_work);
}

static int yesde_memory_callback(struct yestifier_block *self,
				unsigned long action, void *arg)
{
	struct memory_yestify *mnb = arg;
	int nid = mnb->status_change_nid;

	switch (action) {
	case MEM_ONLINE:
	case MEM_OFFLINE:
		/*
		 * offload per yesde hstate [un]registration to a work thread
		 * when transitioning to/from memoryless state.
		 */
		if (nid != NUMA_NO_NODE)
			schedule_work(&yesde_devices[nid]->yesde_work);
		break;

	case MEM_GOING_ONLINE:
	case MEM_GOING_OFFLINE:
	case MEM_CANCEL_ONLINE:
	case MEM_CANCEL_OFFLINE:
	default:
		break;
	}

	return NOTIFY_OK;
}
#endif	/* CONFIG_HUGETLBFS */
#endif /* CONFIG_MEMORY_HOTPLUG_SPARSE */

#if !defined(CONFIG_MEMORY_HOTPLUG_SPARSE) || \
    !defined(CONFIG_HUGETLBFS)
static inline int yesde_memory_callback(struct yestifier_block *self,
				unsigned long action, void *arg)
{
	return NOTIFY_OK;
}

static void init_yesde_hugetlb_work(int nid) { }

#endif

int __register_one_yesde(int nid)
{
	int error;
	int cpu;

	yesde_devices[nid] = kzalloc(sizeof(struct yesde), GFP_KERNEL);
	if (!yesde_devices[nid])
		return -ENOMEM;

	error = register_yesde(yesde_devices[nid], nid);

	/* link cpu under this yesde */
	for_each_present_cpu(cpu) {
		if (cpu_to_yesde(cpu) == nid)
			register_cpu_under_yesde(cpu, nid);
	}

	INIT_LIST_HEAD(&yesde_devices[nid]->access_list);
	/* initialize work queue for memory hot plug */
	init_yesde_hugetlb_work(nid);
	yesde_init_caches(nid);

	return error;
}

void unregister_one_yesde(int nid)
{
	if (!yesde_devices[nid])
		return;

	unregister_yesde(yesde_devices[nid]);
	yesde_devices[nid] = NULL;
}

/*
 * yesde states attributes
 */

static ssize_t print_yesdes_state(enum yesde_states state, char *buf)
{
	int n;

	n = scnprintf(buf, PAGE_SIZE - 1, "%*pbl",
		      yesdemask_pr_args(&yesde_states[state]));
	buf[n++] = '\n';
	buf[n] = '\0';
	return n;
}

struct yesde_attr {
	struct device_attribute attr;
	enum yesde_states state;
};

static ssize_t show_yesde_state(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct yesde_attr *na = container_of(attr, struct yesde_attr, attr);
	return print_yesdes_state(na->state, buf);
}

#define _NODE_ATTR(name, state) \
	{ __ATTR(name, 0444, show_yesde_state, NULL), state }

static struct yesde_attr yesde_state_attr[] = {
	[N_POSSIBLE] = _NODE_ATTR(possible, N_POSSIBLE),
	[N_ONLINE] = _NODE_ATTR(online, N_ONLINE),
	[N_NORMAL_MEMORY] = _NODE_ATTR(has_yesrmal_memory, N_NORMAL_MEMORY),
#ifdef CONFIG_HIGHMEM
	[N_HIGH_MEMORY] = _NODE_ATTR(has_high_memory, N_HIGH_MEMORY),
#endif
	[N_MEMORY] = _NODE_ATTR(has_memory, N_MEMORY),
	[N_CPU] = _NODE_ATTR(has_cpu, N_CPU),
};

static struct attribute *yesde_state_attrs[] = {
	&yesde_state_attr[N_POSSIBLE].attr.attr,
	&yesde_state_attr[N_ONLINE].attr.attr,
	&yesde_state_attr[N_NORMAL_MEMORY].attr.attr,
#ifdef CONFIG_HIGHMEM
	&yesde_state_attr[N_HIGH_MEMORY].attr.attr,
#endif
	&yesde_state_attr[N_MEMORY].attr.attr,
	&yesde_state_attr[N_CPU].attr.attr,
	NULL
};

static struct attribute_group memory_root_attr_group = {
	.attrs = yesde_state_attrs,
};

static const struct attribute_group *cpu_root_attr_groups[] = {
	&memory_root_attr_group,
	NULL,
};

#define NODE_CALLBACK_PRI	2	/* lower than SLAB */
static int __init register_yesde_type(void)
{
	int ret;

 	BUILD_BUG_ON(ARRAY_SIZE(yesde_state_attr) != NR_NODE_STATES);
 	BUILD_BUG_ON(ARRAY_SIZE(yesde_state_attrs)-1 != NR_NODE_STATES);

	ret = subsys_system_register(&yesde_subsys, cpu_root_attr_groups);
	if (!ret) {
		static struct yestifier_block yesde_memory_callback_nb = {
			.yestifier_call = yesde_memory_callback,
			.priority = NODE_CALLBACK_PRI,
		};
		register_hotmemory_yestifier(&yesde_memory_callback_nb);
	}

	/*
	 * Note:  we're yest going to unregister the yesde class if we fail
	 * to register the yesde state class attribute files.
	 */
	return ret;
}
postcore_initcall(register_yesde_type);
