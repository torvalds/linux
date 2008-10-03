/*
 * drivers/base/node.c - basic Node class support
 */

#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/node.h>
#include <linux/hugetlb.h>
#include <linux/cpumask.h>
#include <linux/topology.h>
#include <linux/nodemask.h>
#include <linux/cpu.h>
#include <linux/device.h>

static struct sysdev_class node_class = {
	.name = "node",
};


static ssize_t node_read_cpumap(struct sys_device *dev, int type, char *buf)
{
	struct node *node_dev = to_node(dev);
	node_to_cpumask_ptr(mask, node_dev->sysdev.id);
	int len;

	/* 2008/04/07: buf currently PAGE_SIZE, need 9 chars per 32 bits. */
	BUILD_BUG_ON((NR_CPUS/32 * 9) > (PAGE_SIZE-1));

	len = type?
		cpulist_scnprintf(buf, PAGE_SIZE-2, *mask):
		cpumask_scnprintf(buf, PAGE_SIZE-2, *mask);
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

	n = sprintf(buf, "\n"
		       "Node %d MemTotal:     %8lu kB\n"
		       "Node %d MemFree:      %8lu kB\n"
		       "Node %d MemUsed:      %8lu kB\n"
		       "Node %d Active:       %8lu kB\n"
		       "Node %d Inactive:     %8lu kB\n"
#ifdef CONFIG_HIGHMEM
		       "Node %d HighTotal:    %8lu kB\n"
		       "Node %d HighFree:     %8lu kB\n"
		       "Node %d LowTotal:     %8lu kB\n"
		       "Node %d LowFree:      %8lu kB\n"
#endif
		       "Node %d Dirty:        %8lu kB\n"
		       "Node %d Writeback:    %8lu kB\n"
		       "Node %d FilePages:    %8lu kB\n"
		       "Node %d Mapped:       %8lu kB\n"
		       "Node %d AnonPages:    %8lu kB\n"
		       "Node %d PageTables:   %8lu kB\n"
		       "Node %d NFS_Unstable: %8lu kB\n"
		       "Node %d Bounce:       %8lu kB\n"
		       "Node %d WritebackTmp: %8lu kB\n"
		       "Node %d Slab:         %8lu kB\n"
		       "Node %d SReclaimable: %8lu kB\n"
		       "Node %d SUnreclaim:   %8lu kB\n",
		       nid, K(i.totalram),
		       nid, K(i.freeram),
		       nid, K(i.totalram - i.freeram),
		       nid, K(node_page_state(nid, NR_ACTIVE)),
		       nid, K(node_page_state(nid, NR_INACTIVE)),
#ifdef CONFIG_HIGHMEM
		       nid, K(i.totalhigh),
		       nid, K(i.freehigh),
		       nid, K(i.totalram - i.totalhigh),
		       nid, K(i.freeram - i.freehigh),
#endif
		       nid, K(node_page_state(nid, NR_FILE_DIRTY)),
		       nid, K(node_page_state(nid, NR_WRITEBACK)),
		       nid, K(node_page_state(nid, NR_FILE_PAGES)),
		       nid, K(node_page_state(nid, NR_FILE_MAPPED)),
		       nid, K(node_page_state(nid, NR_ANON_PAGES)),
		       nid, K(node_page_state(nid, NR_PAGETABLE)),
		       nid, K(node_page_state(nid, NR_UNSTABLE_NFS)),
		       nid, K(node_page_state(nid, NR_BOUNCE)),
		       nid, K(node_page_state(nid, NR_WRITEBACK_TEMP)),
		       nid, K(node_page_state(nid, NR_SLAB_RECLAIMABLE) +
				node_page_state(nid, NR_SLAB_UNRECLAIMABLE)),
		       nid, K(node_page_state(nid, NR_SLAB_RECLAIMABLE)),
		       nid, K(node_page_state(nid, NR_SLAB_UNRECLAIMABLE)));
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

static ssize_t node_read_distance(struct sys_device * dev,
			struct sysdev_attribute *attr, char * buf)
{
	int nid = dev->id;
	int len = 0;
	int i;

	/* buf currently PAGE_SIZE, need ~4 chars per node */
	BUILD_BUG_ON(MAX_NUMNODES*4 > PAGE_SIZE/2);

	for_each_online_node(i)
		len += sprintf(buf + len, "%s%d", i ? " " : "", node_distance(nid, i));

	len += sprintf(buf + len, "\n");
	return len;
}
static SYSDEV_ATTR(distance, S_IRUGO, node_read_distance, NULL);


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

	sysdev_unregister(&node->sysdev);
}

struct node node_devices[MAX_NUMNODES];

/*
 * register cpu under node
 */
int register_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	if (node_online(nid)) {
		struct sys_device *obj = get_cpu_sysdev(cpu);
		if (!obj)
			return 0;
		return sysfs_create_link(&node_devices[nid].sysdev.kobj,
					 &obj->kobj,
					 kobject_name(&obj->kobj));
	 }

	return 0;
}

int unregister_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	if (node_online(nid)) {
		struct sys_device *obj = get_cpu_sysdev(cpu);
		if (obj)
			sysfs_remove_link(&node_devices[nid].sysdev.kobj,
					 kobject_name(&obj->kobj));
	}
	return 0;
}

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

static ssize_t print_nodes_possible(struct sysdev_class *class, char *buf)
{
	return print_nodes_state(N_POSSIBLE, buf);
}

static ssize_t print_nodes_online(struct sysdev_class *class, char *buf)
{
	return print_nodes_state(N_ONLINE, buf);
}

static ssize_t print_nodes_has_normal_memory(struct sysdev_class *class,
						char *buf)
{
	return print_nodes_state(N_NORMAL_MEMORY, buf);
}

static ssize_t print_nodes_has_cpu(struct sysdev_class *class, char *buf)
{
	return print_nodes_state(N_CPU, buf);
}

static SYSDEV_CLASS_ATTR(possible, 0444, print_nodes_possible, NULL);
static SYSDEV_CLASS_ATTR(online, 0444, print_nodes_online, NULL);
static SYSDEV_CLASS_ATTR(has_normal_memory, 0444, print_nodes_has_normal_memory,
									NULL);
static SYSDEV_CLASS_ATTR(has_cpu, 0444, print_nodes_has_cpu, NULL);

#ifdef CONFIG_HIGHMEM
static ssize_t print_nodes_has_high_memory(struct sysdev_class *class,
						 char *buf)
{
	return print_nodes_state(N_HIGH_MEMORY, buf);
}

static SYSDEV_CLASS_ATTR(has_high_memory, 0444, print_nodes_has_high_memory,
									 NULL);
#endif

struct sysdev_class_attribute *node_state_attr[] = {
	&attr_possible,
	&attr_online,
	&attr_has_normal_memory,
#ifdef CONFIG_HIGHMEM
	&attr_has_high_memory,
#endif
	&attr_has_cpu,
};

static int node_states_init(void)
{
	int i;
	int err = 0;

	for (i = 0;  i < NR_NODE_STATES; i++) {
		int ret;
		ret = sysdev_class_create_file(&node_class, node_state_attr[i]);
		if (!err)
			err = ret;
	}
	return err;
}

static int __init register_node_type(void)
{
	int ret;

	ret = sysdev_class_register(&node_class);
	if (!ret)
		ret = node_states_init();

	/*
	 * Note:  we're not going to unregister the node class if we fail
	 * to register the node state class attribute files.
	 */
	return ret;
}
postcore_initcall(register_node_type);
