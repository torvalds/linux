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

static const struct bus_type node_subsys = {
	.name = "node",
	.dev_name = "node",
};

static inline ssize_t cpumap_read(struct file *file, struct kobject *kobj,
				  struct bin_attribute *attr, char *buf,
				  loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct node *node_dev = to_node(dev);
	cpumask_var_t mask;
	ssize_t n;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return 0;

	cpumask_and(mask, cpumask_of_node(node_dev->dev.id), cpu_online_mask);
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
	struct node *node_dev = to_node(dev);
	cpumask_var_t mask;
	ssize_t n;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return 0;

	cpumask_and(mask, cpumask_of_node(node_dev->dev.id), cpu_online_mask);
	n = cpumap_print_list_to_buf(buf, mask, off, count);
	free_cpumask_var(mask);

	return n;
}

static BIN_ATTR_RO(cpulist, CPULIST_FILE_MAX_BYTES);

/**
 * struct node_access_nodes - Access class device to hold user visible
 * 			      relationships to other nodes.
 * @dev:	Device for this memory access class
 * @list_node:	List element in the node's access list
 * @access:	The access class rank
 * @coord:	Heterogeneous memory performance coordinates
 */
struct node_access_nodes {
	struct device		dev;
	struct list_head	list_node;
	unsigned int		access;
#ifdef CONFIG_HMEM_REPORTING
	struct access_coordinate	coord;
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
						       unsigned int access)
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
#define ACCESS_ATTR(property)						\
static ssize_t property##_show(struct device *dev,			\
			   struct device_attribute *attr,		\
			   char *buf)					\
{									\
	return sysfs_emit(buf, "%u\n",					\
			  to_access_nodes(dev)->coord.property);	\
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
 * node_set_perf_attrs - Set the performance values for given access class
 * @nid: Node identifier to be set
 * @coord: Heterogeneous memory performance coordinates
 * @access: The access class the for the given attributes
 */
void node_set_perf_attrs(unsigned int nid, struct access_coordinate *coord,
			 unsigned int access)
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

	c->coord = *coord;
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

	device_initialize(dev);
	dev->parent = &node->dev;
	dev->release = node_cache_release;
	if (dev_set_name(dev, "memory_side_cache"))
		goto put_device;

	if (device_add(dev))
		goto put_device;

	pm_runtime_no_callbacks(dev);
	node->cache_dev = dev;
	return;
put_device:
	put_device(dev);
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
	device_initialize(dev);
	dev->parent = node->cache_dev;
	dev->release = node_cacheinfo_release;
	dev->groups = cache_groups;
	if (dev_set_name(dev, "index%d", cache_attrs->level))
		goto put_device;

	info->cache_attrs = *cache_attrs;
	if (device_add(dev)) {
		dev_warn(&node->dev, "failed to add cache level:%d\n",
			 cache_attrs->level);
		goto put_device;
	}
	pm_runtime_no_callbacks(dev);
	list_add_tail(&info->node, &node->cache_attrs);
	return;
put_device:
	put_device(dev);
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
	unsigned long swapcached = 0;

	si_meminfo_node(&i, nid);
	sreclaimable = node_page_state_pages(pgdat, NR_SLAB_RECLAIMABLE_B);
	sunreclaimable = node_page_state_pages(pgdat, NR_SLAB_UNRECLAIMABLE_B);
#ifdef CONFIG_SWAP
	swapcached = node_page_state_pages(pgdat, NR_SWAPCACHE);
#endif
	len = sysfs_emit_at(buf, len,
			    "Node %d MemTotal:       %8lu kB\n"
			    "Node %d MemFree:        %8lu kB\n"
			    "Node %d MemUsed:        %8lu kB\n"
			    "Node %d SwapCached:     %8lu kB\n"
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
			    nid, K(swapcached),
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
			     "Node %d SecPageTables:  %8lu kB\n"
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
			     "Node %d FileHugePages:  %8lu kB\n"
			     "Node %d FilePmdMapped:  %8lu kB\n"
#endif
#ifdef CONFIG_UNACCEPTED_MEMORY
			     "Node %d Unaccepted:     %8lu kB\n"
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
			     nid, K(node_page_state(pgdat, NR_PAGETABLE)),
			     nid, K(node_page_state(pgdat, NR_SECONDARY_PAGETABLE)),
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
			     nid, K(node_page_state(pgdat, NR_ANON_THPS)),
			     nid, K(node_page_state(pgdat, NR_SHMEM_THPS)),
			     nid, K(node_page_state(pgdat, NR_SHMEM_PMDMAPPED)),
			     nid, K(node_page_state(pgdat, NR_FILE_THPS)),
			     nid, K(node_page_state(pgdat, NR_FILE_PMDMAPPED))
#endif
#ifdef CONFIG_UNACCEPTED_MEMORY
			     ,
			     nid, K(sum_zone_node_page_state(nid, NR_UNACCEPTED))
#endif
			    );
	len += hugetlb_report_node_meminfo(buf, len, nid);
	return len;
}

#undef K
static DEVICE_ATTR(meminfo, 0444, node_read_meminfo, NULL);

static ssize_t node_read_numastat(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	fold_vm_numa_events();
	return sysfs_emit(buf,
			  "numa_hit %lu\n"
			  "numa_miss %lu\n"
			  "numa_foreign %lu\n"
			  "interleave_hit %lu\n"
			  "local_node %lu\n"
			  "other_node %lu\n",
			  sum_zone_numa_event_state(dev->id, NUMA_HIT),
			  sum_zone_numa_event_state(dev->id, NUMA_MISS),
			  sum_zone_numa_event_state(dev->id, NUMA_FOREIGN),
			  sum_zone_numa_event_state(dev->id, NUMA_INTERLEAVE_HIT),
			  sum_zone_numa_event_state(dev->id, NUMA_LOCAL),
			  sum_zone_numa_event_state(dev->id, NUMA_OTHER));
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
	fold_vm_numa_events();
	for (i = 0; i < NR_VM_NUMA_EVENT_ITEMS; i++)
		len += sysfs_emit_at(buf, len, "%s %lu\n",
				     numa_stat_name(i),
				     sum_zone_numa_event_state(nid, i));

#endif
	for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++) {
		unsigned long pages = node_page_state_pages(pgdat, i);

		if (vmstat_item_print_in_thp(i))
			pages /= HPAGE_PMD_NR;
		len += sysfs_emit_at(buf, len, "%s %lu\n", node_stat_name(i),
				     pages);
	}

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
	&dev_attr_meminfo.attr,
	&dev_attr_numastat.attr,
	&dev_attr_distance.attr,
	&dev_attr_vmstat.attr,
	NULL
};

static struct bin_attribute *node_dev_bin_attrs[] = {
	&bin_attr_cpumap,
	&bin_attr_cpulist,
	NULL
};

static const struct attribute_group node_dev_group = {
	.attrs = node_dev_attrs,
	.bin_attrs = node_dev_bin_attrs
};

static const struct attribute_group *node_dev_groups[] = {
	&node_dev_group,
#ifdef CONFIG_HAVE_ARCH_NODE_DEV_GROUP
	&arch_node_dev_group,
#endif
#ifdef CONFIG_MEMORY_FAILURE
	&memory_failure_attr_group,
#endif
	NULL
};

static void node_device_release(struct device *dev)
{
	kfree(to_node(dev));
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

	if (error) {
		put_device(&node->dev);
	} else {
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
	hugetlb_unregister_node(node);
	compaction_unregister_node(node);
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
					    unsigned int access)
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

#ifdef CONFIG_MEMORY_HOTPLUG
static int __ref get_nid_for_pfn(unsigned long pfn)
{
#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
	if (system_state < SYSTEM_RUNNING)
		return early_pfn_to_nid(pfn);
#endif
	return pfn_to_nid(pfn);
}

static void do_register_memory_block_under_node(int nid,
						struct memory_block *mem_blk,
						enum meminit_context context)
{
	int ret;

	memory_block_add_nid(mem_blk, nid, context);

	ret = sysfs_create_link_nowarn(&node_devices[nid]->dev.kobj,
				       &mem_blk->dev.kobj,
				       kobject_name(&mem_blk->dev.kobj));
	if (ret && ret != -EEXIST)
		dev_err_ratelimited(&node_devices[nid]->dev,
				    "can't create link to %s in sysfs (%d)\n",
				    kobject_name(&mem_blk->dev.kobj), ret);

	ret = sysfs_create_link_nowarn(&mem_blk->dev.kobj,
				&node_devices[nid]->dev.kobj,
				kobject_name(&node_devices[nid]->dev.kobj));
	if (ret && ret != -EEXIST)
		dev_err_ratelimited(&mem_blk->dev,
				    "can't create link to %s in sysfs (%d)\n",
				    kobject_name(&node_devices[nid]->dev.kobj),
				    ret);
}

/* register memory section under specified node if it spans that node */
static int register_mem_block_under_node_early(struct memory_block *mem_blk,
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
		 * case because node's ranges can be interleaved.
		 */
		page_nid = get_nid_for_pfn(pfn);
		if (page_nid < 0)
			continue;
		if (page_nid != nid)
			continue;

		do_register_memory_block_under_node(nid, mem_blk, MEMINIT_EARLY);
		return 0;
	}
	/* mem section does not span the specified node */
	return 0;
}

/*
 * During hotplug we know that all pages in the memory block belong to the same
 * node.
 */
static int register_mem_block_under_node_hotplug(struct memory_block *mem_blk,
						 void *arg)
{
	int nid = *(int *)arg;

	do_register_memory_block_under_node(nid, mem_blk, MEMINIT_HOTPLUG);
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

void register_memory_blocks_under_node(int nid, unsigned long start_pfn,
				       unsigned long end_pfn,
				       enum meminit_context context)
{
	walk_memory_blocks_func_t func;

	if (context == MEMINIT_HOTPLUG)
		func = register_mem_block_under_node_hotplug;
	else
		func = register_mem_block_under_node_early;

	walk_memory_blocks(PFN_PHYS(start_pfn), PFN_PHYS(end_pfn - start_pfn),
			   (void *)&nid, func);
	return;
}
#endif /* CONFIG_MEMORY_HOTPLUG */

int __register_one_node(int nid)
{
	int error;
	int cpu;
	struct node *node;

	node = kzalloc(sizeof(struct node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	INIT_LIST_HEAD(&node->access_list);
	node_devices[nid] = node;

	error = register_node(node_devices[nid], nid);

	/* link cpu under this node */
	for_each_present_cpu(cpu) {
		if (cpu_to_node(cpu) == nid)
			register_cpu_under_node(cpu, nid);
	}

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
	[N_GENERIC_INITIATOR] = _NODE_ATTR(has_generic_initiator,
					   N_GENERIC_INITIATOR),
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
	&node_state_attr[N_GENERIC_INITIATOR].attr.attr,
	NULL
};

static const struct attribute_group memory_root_attr_group = {
	.attrs = node_state_attrs,
};

static const struct attribute_group *cpu_root_attr_groups[] = {
	&memory_root_attr_group,
	NULL,
};

void __init node_dev_init(void)
{
	int ret, i;

 	BUILD_BUG_ON(ARRAY_SIZE(node_state_attr) != NR_NODE_STATES);
 	BUILD_BUG_ON(ARRAY_SIZE(node_state_attrs)-1 != NR_NODE_STATES);

	ret = subsys_system_register(&node_subsys, cpu_root_attr_groups);
	if (ret)
		panic("%s() failed to register subsystem: %d\n", __func__, ret);

	/*
	 * Create all node devices, which will properly link the node
	 * to applicable memory block devices and already created cpu devices.
	 */
	for_each_online_node(i) {
		ret = register_one_node(i);
		if (ret)
			panic("%s() failed to add node: %d\n", __func__, ret);
	}
}
