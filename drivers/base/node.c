/*
 * drivers/base/node.c - basic Node class support
 */

#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memory.h>
#include <linux/vmstat.h>
#include <linux/node.h>
#include <linux/hugetlb.h>
#include <linux/compaction.h>
#include <linux/cpumask.h>
#include <linux/topology.h>
#include <linux/nodemask.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/swap.h>
#include <linux/slab.h>

static struct sysdev_class_attribute *node_state_attrs[];

static struct sysdev_class node_class = {
	.name = "node",
	.attrs = node_state_attrs,
};


static ssize_t node_read_cpumap(struct sys_device *dev, int type, char *buf)
{
	struct node *node_dev = to_node(dev);
	const struct cpumask *mask = cpumask_of_node(node_dev->sysdev.id);
	int len;

	/* 2008/04/07: buf currently PAGE_SIZE, need 9 chars per 32 bits. */
	BUILD_BUG_ON((NR_CPUS/32 * 9) > (PAGE_SIZE-1));

	len = type?
		cpulist_scnprintf(buf, PAGE_SIZE-2, mask) :
		cpumask_scnprintf(buf, PAGE_SIZE-2, mask);
 	buf[len++] = '\n';
 	buf[len] = '\0';
	return len;
}

static inline ssize_t node_read_cpumask(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	return node_read_cpumap(dev, 0, buf);
}
static inline ssize_t node_read_cpulist(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	return node_read_cpumap(dev, 1, buf);
}

static SYSDEV_ATTR(cpumap,  S_IRUGO, node_read_cpumask, NULL);
static SYSDEV_ATTR(cpulist, S_IRUGO, node_read_cpulist, NULL);

#define K(x) ((x) << (PAGE_SHIFT - 10))
static ssize_t node_read_meminfo(struct sys_device * dev,
			struct sysdev_attribute *attr, char * buf)
{
	int n;
	int nid = dev->id;
	struct sysinfo i;

	si_meminfo_node(&i, nid);
	n = sprintf(buf,
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
		       nid, K(node_page_state(nid, NR_ACTIVE_ANON) +
				node_page_state(nid, NR_ACTIVE_FILE)),
		       nid, K(node_page_state(nid, NR_INACTIVE_ANON) +
				node_page_state(nid, NR_INACTIVE_FILE)),
		       nid, K(node_page_state(nid, NR_ACTIVE_ANON)),
		       nid, K(node_page_state(nid, NR_INACTIVE_ANON)),
		       nid, K(node_page_state(nid, NR_ACTIVE_FILE)),
		       nid, K(node_page_state(nid, NR_INACTIVE_FILE)),
		       nid, K(node_page_state(nid, NR_UNEVICTABLE)),
		       nid, K(node_page_state(nid, NR_MLOCK)));

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
		       "Node %d AnonPages:      %8lu kB\n"
		       "Node %d Shmem:          %8lu kB\n"
		       "Node %d KernelStack:    %8lu kB\n"
		       "Node %d PageTables:     %8lu kB\n"
		       "Node %d NFS_Unstable:   %8lu kB\n"
		       "Node %d Bounce:         %8lu kB\n"
		       "Node %d WritebackTmp:   %8lu kB\n"
		       "Node %d Slab:           %8lu kB\n"
		       "Node %d SReclaimable:   %8lu kB\n"
		       "Node %d SUnreclaim:     %8lu kB\n"
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		       "Node %d AnonHugePages:  %8lu kB\n"
#endif
			,
		       nid, K(node_page_state(nid, NR_FILE_DIRTY)),
		       nid, K(node_page_state(nid, NR_WRITEBACK)),
		       nid, K(node_page_state(nid, NR_FILE_PAGES)),
		       nid, K(node_page_state(nid, NR_FILE_MAPPED)),
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		       nid, K(node_page_state(nid, NR_ANON_PAGES)
			+ node_page_state(nid, NR_ANON_TRANSPARENT_HUGEPAGES) *
			HPAGE_PMD_NR),
#else
		       nid, K(node_page_state(nid, NR_ANON_PAGES)),
#endif
		       nid, K(node_page_state(nid, NR_SHMEM)),
		       nid, node_page_state(nid, NR_KERNEL_STACK) *
				THREAD_SIZE / 1024,
		       nid, K(node_page_state(nid, NR_PAGETABLE)),
		       nid, K(node_page_state(nid, NR_UNSTABLE_NFS)),
		       nid, K(node_page_state(nid, NR_BOUNCE)),
		       nid, K(node_page_state(nid, NR_WRITEBACK_TEMP)),
		       nid, K(node_page_state(nid, NR_SLAB_RECLAIMABLE) +
				node_page_state(nid, NR_SLAB_UNRECLAIMABLE)),
		       nid, K(node_page_state(nid, NR_SLAB_RECLAIMABLE)),
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		       nid, K(node_page_state(nid, NR_SLAB_UNRECLAIMABLE))
			, nid,
			K(node_page_state(nid, NR_ANON_TRANSPARENT_HUGEPAGES) *
			HPAGE_PMD_NR));
#else
		       nid, K(node_page_state(nid, NR_SLAB_UNRECLAIMABLE)));
#endif
	n += hugetlb_report_node_meminfo(nid, buf + n);
	return n;
}

#undef K
static SYSDEV_ATTR(meminfo, S_IRUGO, node_read_meminfo, NULL);

static ssize_t node_read_numastat(struct sys_device * dev,
				struct sysdev_attribute *attr, char * buf)
{
	return sprintf(buf,
		       "numa_hit %lu\n"
		       "numa_miss %lu\n"
		       "numa_foreign %lu\n"
		       "interleave_hit %lu\n"
		       "local_node %lu\n"
		       "other_node %lu\n",
		       node_page_state(dev->id, NUMA_HIT),
		       node_page_state(dev->id, NUMA_MISS),
		       node_page_state(dev->id, NUMA_FOREIGN),
		       node_page_state(dev->id, NUMA_INTERLEAVE_HIT),
		       node_page_state(dev->id, NUMA_LOCAL),
		       node_page_state(dev->id, NUMA_OTHER));
}
static SYSDEV_ATTR(numastat, S_IRUGO, node_read_numastat, NULL);

static ssize_t node_read_vmstat(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	int nid = dev->id;
	int i;
	int n = 0;

	for (i = 0; i < NR_VM_ZONE_STAT_ITEMS; i++)
		n += sprintf(buf+n, "%s %lu\n", vmstat_text[i],
			     node_page_state(nid, i));

	return n;
}
static SYSDEV_ATTR(vmstat, S_IRUGO, node_read_vmstat, NULL);

static ssize_t node_read_distance(struct sys_device * dev,
			struct sysdev_attribute *attr, char * buf)
{
	int nid = dev->id;
	int len = 0;
	int i;

	/*
	 * buf is currently PAGE_SIZE in length and each node needs 4 chars
	 * at the most (distance + space or newline).
	 */
	BUILD_BUG_ON(MAX_NUMNODES * 4 > PAGE_SIZE);

	for_each_online_node(i)
		len += sprintf(buf + len, "%s%d", i ? " " : "", node_distance(nid, i));

	len += sprintf(buf + len, "\n");
	return len;
}
static SYSDEV_ATTR(distance, S_IRUGO, node_read_distance, NULL);

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
			node_state(node->sysdev.id, N_HIGH_MEMORY)) {
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


/*
 * register_node - Setup a sysfs device for a node.
 * @num - Node number to use when creating the device.
 *
 * Initialize and register the node device.
 */
int register_node(struct node *node, int num, struct node *parent)
{
	int error;

	node->sysdev.id = num;
	node->sysdev.cls = &node_class;
	error = sysdev_register(&node->sysdev);

	if (!error){
		sysdev_create_file(&node->sysdev, &attr_cpumap);
		sysdev_create_file(&node->sysdev, &attr_cpulist);
		sysdev_create_file(&node->sysdev, &attr_meminfo);
		sysdev_create_file(&node->sysdev, &attr_numastat);
		sysdev_create_file(&node->sysdev, &attr_distance);
		sysdev_create_file(&node->sysdev, &attr_vmstat);

		scan_unevictable_register_node(node);

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
	sysdev_remove_file(&node->sysdev, &attr_cpumap);
	sysdev_remove_file(&node->sysdev, &attr_cpulist);
	sysdev_remove_file(&node->sysdev, &attr_meminfo);
	sysdev_remove_file(&node->sysdev, &attr_numastat);
	sysdev_remove_file(&node->sysdev, &attr_distance);
	sysdev_remove_file(&node->sysdev, &attr_vmstat);

	scan_unevictable_unregister_node(node);
	hugetlb_unregister_node(node);		/* no-op, if memoryless node */

	sysdev_unregister(&node->sysdev);
}

struct node node_devices[MAX_NUMNODES];

/*
 * register cpu under node
 */
int register_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	int ret;
	struct sys_device *obj;

	if (!node_online(nid))
		return 0;

	obj = get_cpu_sysdev(cpu);
	if (!obj)
		return 0;

	ret = sysfs_create_link(&node_devices[nid].sysdev.kobj,
				&obj->kobj,
				kobject_name(&obj->kobj));
	if (ret)
		return ret;

	return sysfs_create_link(&obj->kobj,
				 &node_devices[nid].sysdev.kobj,
				 kobject_name(&node_devices[nid].sysdev.kobj));
}

int unregister_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	struct sys_device *obj;

	if (!node_online(nid))
		return 0;

	obj = get_cpu_sysdev(cpu);
	if (!obj)
		return 0;

	sysfs_remove_link(&node_devices[nid].sysdev.kobj,
			  kobject_name(&obj->kobj));
	sysfs_remove_link(&obj->kobj,
			  kobject_name(&node_devices[nid].sysdev.kobj));

	return 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE
#define page_initialized(page)  (page->lru.next)

static int get_nid_for_pfn(unsigned long pfn)
{
	struct page *page;

	if (!pfn_valid_within(pfn))
		return -1;
	page = pfn_to_page(pfn);
	if (!page_initialized(page))
		return -1;
	return pfn_to_nid(pfn);
}

/* register memory section under specified node if it spans that node */
int register_mem_sect_under_node(struct memory_block *mem_blk, int nid)
{
	int ret;
	unsigned long pfn, sect_start_pfn, sect_end_pfn;

	if (!mem_blk)
		return -EFAULT;
	if (!node_online(nid))
		return 0;

	sect_start_pfn = section_nr_to_pfn(mem_blk->start_section_nr);
	sect_end_pfn = section_nr_to_pfn(mem_blk->end_section_nr);
	sect_end_pfn += PAGES_PER_SECTION - 1;
	for (pfn = sect_start_pfn; pfn <= sect_end_pfn; pfn++) {
		int page_nid;

		page_nid = get_nid_for_pfn(pfn);
		if (page_nid < 0)
			continue;
		if (page_nid != nid)
			continue;
		ret = sysfs_create_link_nowarn(&node_devices[nid].sysdev.kobj,
					&mem_blk->sysdev.kobj,
					kobject_name(&mem_blk->sysdev.kobj));
		if (ret)
			return ret;

		return sysfs_create_link_nowarn(&mem_blk->sysdev.kobj,
				&node_devices[nid].sysdev.kobj,
				kobject_name(&node_devices[nid].sysdev.kobj));
	}
	/* mem section does not span the specified node */
	return 0;
}

/* unregister memory section under all nodes that it spans */
int unregister_mem_sect_under_nodes(struct memory_block *mem_blk,
				    unsigned long phys_index)
{
	NODEMASK_ALLOC(nodemask_t, unlinked_nodes, GFP_KERNEL);
	unsigned long pfn, sect_start_pfn, sect_end_pfn;

	if (!mem_blk) {
		NODEMASK_FREE(unlinked_nodes);
		return -EFAULT;
	}
	if (!unlinked_nodes)
		return -ENOMEM;
	nodes_clear(*unlinked_nodes);

	sect_start_pfn = section_nr_to_pfn(phys_index);
	sect_end_pfn = sect_start_pfn + PAGES_PER_SECTION - 1;
	for (pfn = sect_start_pfn; pfn <= sect_end_pfn; pfn++) {
		int nid;

		nid = get_nid_for_pfn(pfn);
		if (nid < 0)
			continue;
		if (!node_online(nid))
			continue;
		if (node_test_and_set(nid, *unlinked_nodes))
			continue;
		sysfs_remove_link(&node_devices[nid].sysdev.kobj,
			 kobject_name(&mem_blk->sysdev.kobj));
		sysfs_remove_link(&mem_blk->sysdev.kobj,
			 kobject_name(&node_devices[nid].sysdev.kobj));
	}
	NODEMASK_FREE(unlinked_nodes);
	return 0;
}

static int link_mem_sections(int nid)
{
	unsigned long start_pfn = NODE_DATA(nid)->node_start_pfn;
	unsigned long end_pfn = start_pfn + NODE_DATA(nid)->node_spanned_pages;
	unsigned long pfn;
	struct memory_block *mem_blk = NULL;
	int err = 0;

	for (pfn = start_pfn; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		unsigned long section_nr = pfn_to_section_nr(pfn);
		struct mem_section *mem_sect;
		int ret;

		if (!present_section_nr(section_nr))
			continue;
		mem_sect = __nr_to_section(section_nr);
		mem_blk = find_memory_block_hinted(mem_sect, mem_blk);
		ret = register_mem_sect_under_node(mem_blk, nid);
		if (!err)
			err = ret;

		/* discard ref obtained in find_memory_block() */
	}

	if (mem_blk)
		kobject_put(&mem_blk->sysdev.kobj);
	return err;
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
	INIT_WORK(&node_devices[nid].node_work, node_hugetlb_work);
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
			schedule_work(&node_devices[nid].node_work);
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
#else	/* !CONFIG_MEMORY_HOTPLUG_SPARSE */

static int link_mem_sections(int nid) { return 0; }
#endif	/* CONFIG_MEMORY_HOTPLUG_SPARSE */

#if !defined(CONFIG_MEMORY_HOTPLUG_SPARSE) || \
    !defined(CONFIG_HUGETLBFS)
static inline int node_memory_callback(struct notifier_block *self,
				unsigned long action, void *arg)
{
	return NOTIFY_OK;
}

static void init_node_hugetlb_work(int nid) { }

#endif

int register_one_node(int nid)
{
	int error = 0;
	int cpu;

	if (node_online(nid)) {
		int p_node = parent_node(nid);
		struct node *parent = NULL;

		if (p_node != nid)
			parent = &node_devices[p_node];

		error = register_node(&node_devices[nid], nid, parent);

		/* link cpu under this node */
		for_each_present_cpu(cpu) {
			if (cpu_to_node(cpu) == nid)
				register_cpu_under_node(cpu, nid);
		}

		/* link memory sections under this node */
		error = link_mem_sections(nid);

		/* initialize work queue for memory hot plug */
		init_node_hugetlb_work(nid);
	}

	return error;

}

void unregister_one_node(int nid)
{
	unregister_node(&node_devices[nid]);
}

/*
 * node states attributes
 */

static ssize_t print_nodes_state(enum node_states state, char *buf)
{
	int n;

	n = nodelist_scnprintf(buf, PAGE_SIZE, node_states[state]);
	if (n > 0 && PAGE_SIZE > n + 1) {
		*(buf + n++) = '\n';
		*(buf + n++) = '\0';
	}
	return n;
}

struct node_attr {
	struct sysdev_class_attribute attr;
	enum node_states state;
};

static ssize_t show_node_state(struct sysdev_class *class,
			       struct sysdev_class_attribute *attr, char *buf)
{
	struct node_attr *na = container_of(attr, struct node_attr, attr);
	return print_nodes_state(na->state, buf);
}

#define _NODE_ATTR(name, state) \
	{ _SYSDEV_CLASS_ATTR(name, 0444, show_node_state, NULL), state }

static struct node_attr node_state_attr[] = {
	_NODE_ATTR(possible, N_POSSIBLE),
	_NODE_ATTR(online, N_ONLINE),
	_NODE_ATTR(has_normal_memory, N_NORMAL_MEMORY),
	_NODE_ATTR(has_cpu, N_CPU),
#ifdef CONFIG_HIGHMEM
	_NODE_ATTR(has_high_memory, N_HIGH_MEMORY),
#endif
};

static struct sysdev_class_attribute *node_state_attrs[] = {
	&node_state_attr[0].attr,
	&node_state_attr[1].attr,
	&node_state_attr[2].attr,
	&node_state_attr[3].attr,
#ifdef CONFIG_HIGHMEM
	&node_state_attr[4].attr,
#endif
	NULL
};

#define NODE_CALLBACK_PRI	2	/* lower than SLAB */
static int __init register_node_type(void)
{
	int ret;

 	BUILD_BUG_ON(ARRAY_SIZE(node_state_attr) != NR_NODE_STATES);
 	BUILD_BUG_ON(ARRAY_SIZE(node_state_attrs)-1 != NR_NODE_STATES);

	ret = sysdev_class_register(&node_class);
	if (!ret) {
		hotplug_memory_notifier(node_memory_callback,
					NODE_CALLBACK_PRI);
	}

	/*
	 * Note:  we're not going to unregister the node class if we fail
	 * to register the node state class attribute files.
	 */
	return ret;
}
postcore_initcall(register_node_type);
