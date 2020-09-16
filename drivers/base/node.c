// SPDX-License-Identifier: GPL-2.0
/*
 * Basic Node interface support
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/vmstat.h>
#include <linux/notifier.h>
#include <linux/node.h>
#include <linux/hugetlb.h>
#include <linux/compaction.h>
#include <linux/cpumask.h>
#include <linux/topology.h>
#include <linux/nodemask.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/swap.h>
#include <linux/slab.h>

static struct bus_type node_subsys = {
	.name = "node",
	.dev_name = "node",
};


static ssize_t node_read_cpumap(struct device *dev, bool list, char *buf)
{
	ssize_t n;
	cpumask_var_t mask;
	struct node *node_dev = to_node(dev);

	/* 2008/04/07: buf currently PAGE_SIZE, need 9 chars per 32 bits. */
	BUILD_BUG_ON((NR_CPUS/32 * 9) > (PAGE_SIZE-1));

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return 0;

	cpumask_and(mask, cpumask_of_node(node_dev->dev.id), cpu_online_mask);
	n = cpumap_print_to_pagebuf(list, buf, mask);
	free_cpumask_var(mask);

	return n;
}

static inline ssize_t cpumap_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	return node_read_cpumap(dev, false, buf);
}

static DEVICE_ATTR_RO(cpumap);

static inline ssize_t cpulist_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	return node_read_cpumap(dev, true, buf);
}

static DEVICE_ATTR_RO(cpulist);

/**
 * struct node_access_nodes - Access class device to hold user visible
 * 			      relationships to other nodes.
 * @dev:	Device for this memory access class
 * @list_node:	List element in the node's access list
 * @access:	The access class rank
 * @hmem_attrs: Heterogeneous memory performance attributes
 */
struct node_access_nodes {
	struct device		dev;
	struct list_head	list_node;
	unsigned		access;
#ifdef CONFIG_HMEM_REPORTING
	struct node_hmem_attrs	hmem_attrs;
#endif
};
#define to_access_nodes(dev) container_of(dev, struct node_access_nodes, dev)

static struct attribute *node_init_access_node_attrs[] = {
	NULL,
};

static struct attribute *node_targ_access_node_attrs[] = {
	NULL,
};

static const struct attribute_group initiators = {
	.name	= "initiators",
	.attrs	= node_init_access_node_attrs,
};

static const struct attribute_group targets = {
	.name	= "targets",
	.attrs	= node_targ_access_node_attrs,
};

static const struct attribute_group *node_access_node_groups[] = {
	&initiators,
	&targets,
	NULL,
};

static void node_remove_accesses(struct node *node)
{
	struct node_access_nodes *c, *cnext;

	list_for_each_entry_safe(c, cnext, &node->access_list, list_node) {
		list_del(&c->list_node);
		device_unregister(&c->dev);
	}
}

static void node_access_release(struct device *dev)
{
	kfree(to_access_nodes(dev));
}

static struct node_access_nodes *node_init_node_access(struct node *node,
						       unsigned access)
{
	struct node_access_nodes *access_node;
	struct device *dev;

	list_for_each_entry(access_node, &node->access_list, list_node)
		if (access_node->access == access)
			return access_node;

	access_node = kzalloc(sizeof(*access_node), GFP_KERNEL);
	if (!access_node)
		return NULL;

	access_node->access = access;
	dev = &access_node->dev;
	dev->parent = &node->dev;
	dev->release = node_access_release;
	dev->groups = node_access_node_groups;
	if (dev_set_name(dev, "access%u", access))
		goto free;

	if (device_register(dev))
		goto free_name;

	pm_runtime_no_callbacks(dev);
	list_add_tail(&access_node->list_node, &node->access_list);
	return access_node;
free_name:
	kfree_const(dev->kobj.name);
free:
	kfree(access_node);
	return NULL;
}

#ifdef CONFIG_HMEM_REPORTING
#define ACCESS_ATTR(name)						\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr,		\
			   char *buf)					\
{									\
	return sysfs_emit(buf, "%u\n",					\
			  to_access_nodes(dev)->hmem_attrs.name);	\
}									\
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
 * node_set_perf_attrs - Set the performance values for given access class
 * @nid: Node identifier to be set
 * @hmem_attrs: Heterogeneous memory performance attributes
 * @access: The access class the for the given attributes
 */
void node_set_perf_attrs(unsigned int nid, struct node_hmem_attrs *hmem_attrs,
			 unsigned access)
{
	struct node_access_nodes *c;
	struct node *node;
	int i;

	if (WARN_ON_ONCE(!node_online(nid)))
		return;

	node = node_devices[nid];
	c = node_init_node_access(node, access);
	if (!c)
		return;

	c->hmem_attrs = *hmem_attrs;
	for (i = 0; access_attrs[i] != NULL; i++) {
		if (sysfs_add_file_to_group(&c->dev.kobj, access_attrs[i],
					    "initiators")) {
			pr_info("failed to add performance attribute to node %d\n",
				nid);
			break;
		}
	}
}

/**
 * struct node_cache_info - Internal tracking for memory node caches
 * @dev:	Device represeting the cache level
 * @node:	List element for tracking in the node
 * @cache_attrs:Attributes for this cache level
 */
struct node_cache_info {
	struct device dev;
	struct list_head node;
	struct node_cache_attrs cache_attrs;
};
#define to_cache_info(device) container_of(device, struct node_cache_info, dev)

#define CACHE_ATTR(name, fmt) 						\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr,		\
			   char *buf)					\
{									\
	return sysfs_emit(buf, fmt "\n",				\
			  to_cache_info(dev)->cache_attrs.name);	\
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

static void node_cache_release(struct device *dev)
{
	kfree(dev);
}

static void node_cacheinfo_release(struct device *dev)
{
	struct node_cache_info *info = to_cache_info(dev);
	kfree(info);
}

static void node_init_cache_dev(struct node *node)
{
	struct device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return;

	dev->parent = &node->dev;
	dev->release = node_cache_release;
	if (dev_set_name(dev, "memory_side_cache"))
		goto free_dev;

	if (device_register(dev))
		goto free_name;

	pm_runtime_no_callbacks(dev);
	node->cache_dev = dev;
	return;
free_name:
	kfree_const(dev->kobj.name);
free_dev:
	kfree(dev);
}

/**
 * node_add_cache() - add cache attribute to a memory node
 * @nid: Node identifier that has new cache attributes
 * @cache_attrs: Attributes for the cache being added
 */
void node_add_cache(unsigned int nid, struct node_cache_attrs *cache_attrs)
{
	struct node_cache_info *info;
	struct device *dev;
	struct node *node;

	if (!node_online(nid) || !node_devices[nid])
		return;

	node = node_devices[nid];
	list_for_each_entry(info, &node->cache_attrs, node) {
		if (info->cache_attrs.level == cache_attrs->level) {
			dev_warn(&node->dev,
				"attempt to add duplicate cache level:%d\n",
				cache_attrs->level);
			return;
		}
	}

	if (!node->cache_dev)
		node_init_cache_dev(node);
	if (!node->cache_dev)
		return;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return;

	dev = &info->dev;
	dev->parent = node->cache_dev;
	dev->release = node_cacheinfo_release;
	dev->groups = cache_groups;
	if (dev_set_name(dev, "index%d", cache_attrs->level))
		goto free_cache;

	info->cache_attrs = *cache_attrs;
	if (device_register(dev)) {
		dev_warn(&node->dev, "failed to add cache level:%d\n",
			 cache_attrs->level);
		goto free_name;
	}
	pm_runtime_no_callbacks(dev);
	list_add_tail(&info->node, &node->cache_attrs);
	return;
free_name:
	kfree_const(dev->kobj.name);
free_cache:
	kfree(info);
}

static void node_remove_caches(struct node *node)
{
	struct node_cache_info *info, *next;

	if (!node->cache_dev)
		return;

	list_for_each_entry_safe(info, next, &node->cache_attrs, node) {
		list_del(&info->node);
		device_unregister(&info->dev);
	}
	device_unregister(node->cache_dev);
}

static void node_init_caches(unsigned int nid)
{
	INIT_LIST_HEAD(&node_devices[nid]->cache_attrs);
}
#else
static void node_init_caches(unsigned int nid) { }
static void node_remove_caches(struct node *node) { }
#endif

#define K(x) ((x) << (PAGE_SHIFT - 10))
static ssize_t node_read_meminfo(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int len = 0;
	int nid = dev->id;
	struct pglist_data *pgdat = NODE_DATA(nid);
	struct sysinfo i;
	unsigned long sreclaimable, sunreclaimable;

	si_meminfo_node(&i, nid);
	sreclaimable = node_page_state_pages(pgdat, NR_SLAB_RECLAIMABLE_B);
	sunreclaimable = node_page_state_pages(pgdat, NR_SLAB_UNRECLAIMABLE_B);
	len = sysfs_emit_at(buf, len,
			    "Node %d MemTotal:       %8lu kB\n"
			    "Node %d MemFree:        %8lu kB\n"
			    "Node %d MemUsed:        %8lu kB\n"
			    "Node %d Active:         %8lu kB\n"
			    "Node %d Inactive:       %8lu kB\n"
			    "Node %d Active(anon):   %8lu kB\n"
			    "Node %d Inactive(anon): %8lu kB\n"
			    "Node %d Active(file):   %8lu kB\n"
			    "Node %d Inactive(file): %8lu kB\n"
			    "Node %d Unevictable:    %8lu kB\n"
			    "Node %d Mlocked:        %8lu kB\n",
			    nid, K(i.totalram),
			    nid, K(i.freeram),
			    nid, K(i.totalram - i.freeram),
			    nid, K(node_page_state(pgdat, NR_ACTIVE_ANON) +
				   node_page_state(pgdat, NR_ACTIVE_FILE)),
			    nid, K(node_page_state(pgdat, NR_INACTIVE_ANON) +
				   node_page_state(pgdat, NR_INACTIVE_FILE)),
			    nid, K(node_page_state(pgdat, NR_ACTIVE_ANON)),
			    nid, K(node_page_state(pgdat, NR_INACTIVE_ANON)),
			    nid, K(node_page_state(pgdat, NR_ACTIVE_FILE)),
			    nid, K(node_page_state(pgdat, NR_INACTIVE_FILE)),
			    nid, K(node_page_state(pgdat, NR_UNEVICTABLE)),
			    nid, K(sum_zone_node_page_state(nid, NR_MLOCK)));

#ifdef CONFIG_HIGHMEM
	len += sysfs_emit_at(buf, len,
			     "Node %d HighTotal:      %8lu kB\n"
			     "Node %d HighFree:       %8lu kB\n"
			     "Node %d LowTotal:       %8lu kB\n"
			     "Node %d LowFree:        %8lu kB\n",
			     nid, K(i.totalhigh),
			     nid, K(i.freehigh),
			     nid, K(i.totalram - i.totalhigh),
			     nid, K(i.freeram - i.freehigh));
#endif
	len += sysfs_emit_at(buf, len,
			     "Node %d Dirty:          %8lu kB\n"
			     "Node %d Writeback:      %8lu kB\n"
			     "Node %d FilePages:      %8lu kB\n"
			     "Node %d Mapped:         %8lu kB\n"
			     "Node %d AnonPages:      %8lu kB\n"
			     "Node %d Shmem:          %8lu kB\n"
			     "Node %d KernelStack:    %8lu kB\n"
#ifdef CONFIG_SHADOW_CALL_STACK
			     "Node %d ShadowCallStack:%8lu kB\n"
#endif
			     "Node %d PageTables:     %8lu kB\n"
			     "Node %d NFS_Unstable:   %8lu kB\n"
			     "Node %d Bounce:         %8lu kB\n"
			     "Node %d WritebackTmp:   %8lu kB\n"
			     "Node %d KReclaimable:   %8lu kB\n"
			     "Node %d Slab:           %8lu kB\n"
			     "Node %d SReclaimable:   %8lu kB\n"
			     "Node %d SUnreclaim:     %8lu kB\n"
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			     "Node %d AnonHugePages:  %8lu kB\n"
			     "Node %d ShmemHugePages: %8lu kB\n"
			     "Node %d ShmemPmdMapped: %8lu kB\n"
			     "Node %d FileHugePages: %8lu kB\n"
			     "Node %d FilePmdMapped: %8lu kB\n"
#endif
			     ,
			     nid, K(node_page_state(pgdat, NR_FILE_DIRTY)),
			     nid, K(node_page_state(pgdat, NR_WRITEBACK)),
			     nid, K(node_page_state(pgdat, NR_FILE_PAGES)),
			     nid, K(node_page_state(pgdat, NR_FILE_MAPPED)),
			     nid, K(node_page_state(pgdat, NR_ANON_MAPPED)),
			     nid, K(i.sharedram),
			     nid, node_page_state(pgdat, NR_KERNEL_STACK_KB),
#ifdef CONFIG_SHADOW_CALL_STACK
			     nid, node_page_state(pgdat, NR_KERNEL_SCS_KB),
#endif
			     nid, K(sum_zone_node_page_state(nid, NR_PAGETABLE)),
			     nid, 0UL,
			     nid, K(sum_zone_node_page_state(nid, NR_BOUNCE)),
			     nid, K(node_page_state(pgdat, NR_WRITEBACK_TEMP)),
			     nid, K(sreclaimable +
				    node_page_state(pgdat, NR_KERNEL_MISC_RECLAIMABLE)),
			     nid, K(sreclaimable + sunreclaimable),
			     nid, K(sreclaimable),
			     nid, K(sunreclaimable)
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			     ,
			     nid, K(node_page_state(pgdat, NR_ANON_THPS) *
				    HPAGE_PMD_NR),
			     nid, K(node_page_state(pgdat, NR_SHMEM_THPS) *
				    HPAGE_PMD_NR),
			     nid, K(node_page_state(pgdat, NR_SHMEM_PMDMAPPED) *
				    HPAGE_PMD_NR),
			     nid, K(node_page_state(pgdat, NR_FILE_THPS) *
				    HPAGE_PMD_NR),
			     nid, K(node_page_state(pgdat, NR_FILE_PMDMAPPED) *
				    HPAGE_PMD_NR)
#endif
			    );
	len += hugetlb_report_node_meminfo(nid, buf + len);
	return len;
}

#undef K
static DEVICE_ATTR(meminfo, 0444, node_read_meminfo, NULL);

static ssize_t node_read_numastat(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf,
			  "numa_hit %lu\n"
			  "numa_miss %lu\n"
			  "numa_foreign %lu\n"
			  "interleave_hit %lu\n"
			  "local_node %lu\n"
			  "other_node %lu\n",
			  sum_zone_numa_state(dev->id, NUMA_HIT),
			  sum_zone_numa_state(dev->id, NUMA_MISS),
			  sum_zone_numa_state(dev->id, NUMA_FOREIGN),
			  sum_zone_numa_state(dev->id, NUMA_INTERLEAVE_HIT),
			  sum_zone_numa_state(dev->id, NUMA_LOCAL),
			  sum_zone_numa_state(dev->id, NUMA_OTHER));
}
static DEVICE_ATTR(numastat, 0444, node_read_numastat, NULL);

static ssize_t node_read_vmstat(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int nid = dev->id;
	struct pglist_data *pgdat = NODE_DATA(nid);
	int i;
	int len = 0;

	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
		len += sysfs_emit_at(buf, len, "%s %lu\n",
				     zone_stat_name(i),
				     sum_zone_node_page_state(nid, i));

#ifdef CONFIG_NUMA
	for (i = 0; i < NR_VM_NUMA_STAT_ITEMS; i++)
		len += sysfs_emit_at(buf, len, "%s %lu\n",
				     numa_stat_name(i),
				     sum_zone_numa_state(nid, i));

#endif
	for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++)
		len += sysfs_emit_at(buf, len, "%s %lu\n",
				     node_stat_name(i),
				     node_page_state_pages(pgdat, i));

	return len;
}
static DEVICE_ATTR(vmstat, 0444, node_read_vmstat, NULL);

static ssize_t node_read_distance(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int nid = dev->id;
	int len = 0;
	int i;

	/*
	 * buf is currently PAGE_SIZE in length and each node needs 4 chars
	 * at the most (distance + space or newline).
	 */
	BUILD_BUG_ON(MAX_NUMNODES * 4 > PAGE_SIZE);

	for_each_online_node(i) {
		len += sysfs_emit_at(buf, len, "%s%d",
				     i ? " " : "", node_distance(nid, i));
	}

	len += sysfs_emit_at(buf, len, "\n");
	return len;
}
static DEVICE_ATTR(distance, 0444, node_read_distance, NULL);

static struct attribute *node_dev_attrs[] = {
	&dev_attr_cpumap.attr,
	&dev_attr_cpulist.attr,
	&dev_attr_meminfo.attr,
	&dev_attr_numastat.attr,
	&dev_attr_distance.attr,
	&dev_attr_vmstat.attr,
	NULL
};
ATTRIBUTE_GROUPS(node_dev);

#ifdef CONFIG_HUGETLBFS
/*
 * hugetlbfs per node attributes registration interface:
 * When/if hugetlb[fs] subsystem initializes [sometime after this module],
 * it will register its per node attributes for all online nodes with
 * memory.  It will also call register_hugetlbfs_with_node(), below, to
 * register its attribute registration functions with this node driver.
 * Once these hooks have been initialized, the node driver will call into
 * the hugetlb module to [un]register attributes for hot-plugged nodes.
 */
static node_registration_func_t __hugetlb_register_node;
static node_registration_func_t __hugetlb_unregister_node;

static inline bool hugetlb_register_node(struct node *node)
{
	if (__hugetlb_register_node &&
			node_state(node->dev.id, N_MEMORY)) {
		__hugetlb_register_node(node);
		return true;
	}
	return false;
}

static inline void hugetlb_unregister_node(struct node *node)
{
	if (__hugetlb_unregister_node)
		__hugetlb_unregister_node(node);
}

void register_hugetlbfs_with_node(node_registration_func_t doregister,
				  node_registration_func_t unregister)
{
	__hugetlb_register_node   = doregister;
	__hugetlb_unregister_node = unregister;
}
#else
static inline void hugetlb_register_node(struct node *node) {}

static inline void hugetlb_unregister_node(struct node *node) {}
#endif

static void node_device_release(struct device *dev)
{
	struct node *node = to_node(dev);

#if defined(CONFIG_MEMORY_HOTPLUG_SPARSE) && defined(CONFIG_HUGETLBFS)
	/*
	 * We schedule the work only when a memory section is
	 * onlined/offlined on this node. When we come here,
	 * all the memory on this node has been offlined,
	 * so we won't enqueue new work to this work.
	 *
	 * The work is using node->node_work, so we should
	 * flush work before freeing the memory.
	 */
	flush_work(&node->node_work);
#endif
	kfree(node);
}

/*
 * register_node - Setup a sysfs device for a node.
 * @num - Node number to use when creating the device.
 *
 * Initialize and register the node device.
 */
static int register_node(struct node *node, int num)
{
	int error;

	node->dev.id = num;
	node->dev.bus = &node_subsys;
	node->dev.release = node_device_release;
	node->dev.groups = node_dev_groups;
	error = device_register(&node->dev);

	if (error)
		put_device(&node->dev);
	else {
		hugetlb_register_node(node);

		compaction_register_node(node);
	}
	return error;
}

/**
 * unregister_node - unregister a node device
 * @node: node going away
 *
 * Unregisters a node device @node.  All the devices on the node must be
 * unregistered before calling this function.
 */
void unregister_node(struct node *node)
{
	hugetlb_unregister_node(node);		/* no-op, if memoryless node */
	node_remove_accesses(node);
	node_remove_caches(node);
	device_unregister(&node->dev);
}

struct node *node_devices[MAX_NUMNODES];

/*
 * register cpu under node
 */
int register_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	int ret;
	struct device *obj;

	if (!node_online(nid))
		return 0;

	obj = get_cpu_device(cpu);
	if (!obj)
		return 0;

	ret = sysfs_create_link(&node_devices[nid]->dev.kobj,
				&obj->kobj,
				kobject_name(&obj->kobj));
	if (ret)
		return ret;

	return sysfs_create_link(&obj->kobj,
				 &node_devices[nid]->dev.kobj,
				 kobject_name(&node_devices[nid]->dev.kobj));
}

/**
 * register_memory_node_under_compute_node - link memory node to its compute
 *					     node for a given access class.
 * @mem_nid:	Memory node number
 * @cpu_nid:	Cpu  node number
 * @access:	Access class to register
 *
 * Description:
 * 	For use with platforms that may have separate memory and compute nodes.
 * 	This function will export node relationships linking which memory
 * 	initiator nodes can access memory targets at a given ranked access
 * 	class.
 */
int register_memory_node_under_compute_node(unsigned int mem_nid,
					    unsigned int cpu_nid,
					    unsigned access)
{
	struct node *init_node, *targ_node;
	struct node_access_nodes *initiator, *target;
	int ret;

	if (!node_online(cpu_nid) || !node_online(mem_nid))
		return -ENODEV;

	init_node = node_devices[cpu_nid];
	targ_node = node_devices[mem_nid];
	initiator = node_init_node_access(init_node, access);
	target = node_init_node_access(targ_node, access);
	if (!initiator || !target)
		return -ENOMEM;

	ret = sysfs_add_link_to_group(&initiator->dev.kobj, "targets",
				      &targ_node->dev.kobj,
				      dev_name(&targ_node->dev));
	if (ret)
		return ret;

	ret = sysfs_add_link_to_group(&target->dev.kobj, "initiators",
				      &init_node->dev.kobj,
				      dev_name(&init_node->dev));
	if (ret)
		goto err;

	return 0;
 err:
	sysfs_remove_link_from_group(&initiator->dev.kobj, "targets",
				     dev_name(&targ_node->dev));
	return ret;
}

int unregister_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	struct device *obj;

	if (!node_online(nid))
		return 0;

	obj = get_cpu_device(cpu);
	if (!obj)
		return 0;

	sysfs_remove_link(&node_devices[nid]->dev.kobj,
			  kobject_name(&obj->kobj));
	sysfs_remove_link(&obj->kobj,
			  kobject_name(&node_devices[nid]->dev.kobj));

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

/* register memory section under specified node if it spans that node */
static int register_mem_sect_under_node(struct memory_block *mem_blk,
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
		if (!pfn_in_present_section(pfn)) {
			pfn = round_down(pfn + PAGES_PER_SECTION,
					 PAGES_PER_SECTION) - 1;
			continue;
		}

		/*
		 * We need to check if page belongs to nid only for the boot
		 * case, during hotplug we know that all pages in the memory
		 * block belong to the same node.
		 */
		if (system_state == SYSTEM_BOOTING) {
			page_nid = get_nid_for_pfn(pfn);
			if (page_nid < 0)
				continue;
			if (page_nid != nid)
				continue;
		}

		/*
		 * If this memory block spans multiple nodes, we only indicate
		 * the last processed node.
		 */
		mem_blk->nid = nid;

		ret = sysfs_create_link_nowarn(&node_devices[nid]->dev.kobj,
					&mem_blk->dev.kobj,
					kobject_name(&mem_blk->dev.kobj));
		if (ret)
			return ret;

		return sysfs_create_link_nowarn(&mem_blk->dev.kobj,
				&node_devices[nid]->dev.kobj,
				kobject_name(&node_devices[nid]->dev.kobj));
	}
	/* mem section does not span the specified node */
	return 0;
}

/*
 * Unregister a memory block device under the node it spans. Memory blocks
 * with multiple nodes cannot be offlined and therefore also never be removed.
 */
void unregister_memory_block_under_nodes(struct memory_block *mem_blk)
{
	if (mem_blk->nid == NUMA_NO_NODE)
		return;

	sysfs_remove_link(&node_devices[mem_blk->nid]->dev.kobj,
			  kobject_name(&mem_blk->dev.kobj));
	sysfs_remove_link(&mem_blk->dev.kobj,
			  kobject_name(&node_devices[mem_blk->nid]->dev.kobj));
}

int link_mem_sections(int nid, unsigned long start_pfn, unsigned long end_pfn)
{
	return walk_memory_blocks(PFN_PHYS(start_pfn),
				  PFN_PHYS(end_pfn - start_pfn), (void *)&nid,
				  register_mem_sect_under_node);
}

#ifdef CONFIG_HUGETLBFS
/*
 * Handle per node hstate attribute [un]registration on transistions
 * to/from memoryless state.
 */
static void node_hugetlb_work(struct work_struct *work)
{
	struct node *node = container_of(work, struct node, node_work);

	/*
	 * We only get here when a node transitions to/from memoryless state.
	 * We can detect which transition occurred by examining whether the
	 * node has memory now.  hugetlb_register_node() already check this
	 * so we try to register the attributes.  If that fails, then the
	 * node has transitioned to memoryless, try to unregister the
	 * attributes.
	 */
	if (!hugetlb_register_node(node))
		hugetlb_unregister_node(node);
}

static void init_node_hugetlb_work(int nid)
{
	INIT_WORK(&node_devices[nid]->node_work, node_hugetlb_work);
}

static int node_memory_callback(struct notifier_block *self,
				unsigned long action, void *arg)
{
	struct memory_notify *mnb = arg;
	int nid = mnb->status_change_nid;

	switch (action) {
	case MEM_ONLINE:
	case MEM_OFFLINE:
		/*
		 * offload per node hstate [un]registration to a work thread
		 * when transitioning to/from memoryless state.
		 */
		if (nid != NUMA_NO_NODE)
			schedule_work(&node_devices[nid]->node_work);
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
static inline int node_memory_callback(struct notifier_block *self,
				unsigned long action, void *arg)
{
	return NOTIFY_OK;
}

static void init_node_hugetlb_work(int nid) { }

#endif

int __register_one_node(int nid)
{
	int error;
	int cpu;

	node_devices[nid] = kzalloc(sizeof(struct node), GFP_KERNEL);
	if (!node_devices[nid])
		return -ENOMEM;

	error = register_node(node_devices[nid], nid);

	/* link cpu under this node */
	for_each_present_cpu(cpu) {
		if (cpu_to_node(cpu) == nid)
			register_cpu_under_node(cpu, nid);
	}

	INIT_LIST_HEAD(&node_devices[nid]->access_list);
	/* initialize work queue for memory hot plug */
	init_node_hugetlb_work(nid);
	node_init_caches(nid);

	return error;
}

void unregister_one_node(int nid)
{
	if (!node_devices[nid])
		return;

	unregister_node(node_devices[nid]);
	node_devices[nid] = NULL;
}

/*
 * node states attributes
 */

struct node_attr {
	struct device_attribute attr;
	enum node_states state;
};

static ssize_t show_node_state(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct node_attr *na = container_of(attr, struct node_attr, attr);

	return sysfs_emit(buf, "%*pbl\n",
			  nodemask_pr_args(&node_states[na->state]));
}

#define _NODE_ATTR(name, state) \
	{ __ATTR(name, 0444, show_node_state, NULL), state }

static struct node_attr node_state_attr[] = {
	[N_POSSIBLE] = _NODE_ATTR(possible, N_POSSIBLE),
	[N_ONLINE] = _NODE_ATTR(online, N_ONLINE),
	[N_NORMAL_MEMORY] = _NODE_ATTR(has_normal_memory, N_NORMAL_MEMORY),
#ifdef CONFIG_HIGHMEM
	[N_HIGH_MEMORY] = _NODE_ATTR(has_high_memory, N_HIGH_MEMORY),
#endif
	[N_MEMORY] = _NODE_ATTR(has_memory, N_MEMORY),
	[N_CPU] = _NODE_ATTR(has_cpu, N_CPU),
};

static struct attribute *node_state_attrs[] = {
	&node_state_attr[N_POSSIBLE].attr.attr,
	&node_state_attr[N_ONLINE].attr.attr,
	&node_state_attr[N_NORMAL_MEMORY].attr.attr,
#ifdef CONFIG_HIGHMEM
	&node_state_attr[N_HIGH_MEMORY].attr.attr,
#endif
	&node_state_attr[N_MEMORY].attr.attr,
	&node_state_attr[N_CPU].attr.attr,
	NULL
};

static struct attribute_group memory_root_attr_group = {
	.attrs = node_state_attrs,
};

static const struct attribute_group *cpu_root_attr_groups[] = {
	&memory_root_attr_group,
	NULL,
};

#define NODE_CALLBACK_PRI	2	/* lower than SLAB */
static int __init register_node_type(void)
{
	int ret;

 	BUILD_BUG_ON(ARRAY_SIZE(node_state_attr) != NR_NODE_STATES);
 	BUILD_BUG_ON(ARRAY_SIZE(node_state_attrs)-1 != NR_NODE_STATES);

	ret = subsys_system_register(&node_subsys, cpu_root_attr_groups);
	if (!ret) {
		static struct notifier_block node_memory_callback_nb = {
			.notifier_call = node_memory_callback,
			.priority = NODE_CALLBACK_PRI,
		};
		register_hotmemory_notifier(&node_memory_callback_nb);
	}

	/*
	 * Note:  we're not going to unregister the node class if we fail
	 * to register the node state class attribute files.
	 */
	return ret;
}
postcore_initcall(register_node_type);
