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

static struct sysdev_class node_class = {
	set_kset_name("node"),
};


static ssize_t node_read_cpumap(struct sys_device * dev, char * buf)
{
	struct node *node_dev = to_node(dev);
	cpumask_t mask = node_to_cpumask(node_dev->sysdev.id);
	int len;

	/* 2004/06/03: buf currently PAGE_SIZE, need > 1 char per 4 bits. */
	BUILD_BUG_ON(MAX_NUMNODES/4 > PAGE_SIZE/2);

	len = cpumask_scnprintf(buf, PAGE_SIZE-1, mask);
	len += sprintf(buf + len, "\n");
	return len;
}

static SYSDEV_ATTR(cpumap, S_IRUGO, node_read_cpumap, NULL);

#define K(x) ((x) << (PAGE_SHIFT - 10))
static ssize_t node_read_meminfo(struct sys_device * dev, char * buf)
{
	int n;
	int nid = dev->id;
	struct sysinfo i;
	struct page_state ps;
	unsigned long inactive;
	unsigned long active;
	unsigned long free;

	si_meminfo_node(&i, nid);
	get_page_state_node(&ps, nid);
	__get_zone_counts(&active, &inactive, &free, NODE_DATA(nid));

	/* Check for negative values in these approximate counters */
	if ((long)ps.nr_dirty < 0)
		ps.nr_dirty = 0;
	if ((long)ps.nr_writeback < 0)
		ps.nr_writeback = 0;
	if ((long)ps.nr_mapped < 0)
		ps.nr_mapped = 0;
	if ((long)ps.nr_slab < 0)
		ps.nr_slab = 0;

	n = sprintf(buf, "\n"
		       "Node %d MemTotal:     %8lu kB\n"
		       "Node %d MemFree:      %8lu kB\n"
		       "Node %d MemUsed:      %8lu kB\n"
		       "Node %d Active:       %8lu kB\n"
		       "Node %d Inactive:     %8lu kB\n"
		       "Node %d HighTotal:    %8lu kB\n"
		       "Node %d HighFree:     %8lu kB\n"
		       "Node %d LowTotal:     %8lu kB\n"
		       "Node %d LowFree:      %8lu kB\n"
		       "Node %d Dirty:        %8lu kB\n"
		       "Node %d Writeback:    %8lu kB\n"
		       "Node %d Mapped:       %8lu kB\n"
		       "Node %d Slab:         %8lu kB\n",
		       nid, K(i.totalram),
		       nid, K(i.freeram),
		       nid, K(i.totalram - i.freeram),
		       nid, K(active),
		       nid, K(inactive),
		       nid, K(i.totalhigh),
		       nid, K(i.freehigh),
		       nid, K(i.totalram - i.totalhigh),
		       nid, K(i.freeram - i.freehigh),
		       nid, K(ps.nr_dirty),
		       nid, K(ps.nr_writeback),
		       nid, K(ps.nr_mapped),
		       nid, K(ps.nr_slab));
	n += hugetlb_report_node_meminfo(nid, buf + n);
	return n;
}

#undef K
static SYSDEV_ATTR(meminfo, S_IRUGO, node_read_meminfo, NULL);

static ssize_t node_read_numastat(struct sys_device * dev, char * buf)
{
	unsigned long numa_hit, numa_miss, interleave_hit, numa_foreign;
	unsigned long local_node, other_node;
	int i, cpu;
	pg_data_t *pg = NODE_DATA(dev->id);
	numa_hit = 0;
	numa_miss = 0;
	interleave_hit = 0;
	numa_foreign = 0;
	local_node = 0;
	other_node = 0;
	for (i = 0; i < MAX_NR_ZONES; i++) {
		struct zone *z = &pg->node_zones[i];
		for_each_online_cpu(cpu) {
			struct per_cpu_pageset *ps = zone_pcp(z,cpu);
			numa_hit += ps->numa_hit;
			numa_miss += ps->numa_miss;
			numa_foreign += ps->numa_foreign;
			interleave_hit += ps->interleave_hit;
			local_node += ps->local_node;
			other_node += ps->other_node;
		}
	}
	return sprintf(buf,
		       "numa_hit %lu\n"
		       "numa_miss %lu\n"
		       "numa_foreign %lu\n"
		       "interleave_hit %lu\n"
		       "local_node %lu\n"
		       "other_node %lu\n",
		       numa_hit,
		       numa_miss,
		       numa_foreign,
		       interleave_hit,
		       local_node,
		       other_node);
}
static SYSDEV_ATTR(numastat, S_IRUGO, node_read_numastat, NULL);

static ssize_t node_read_distance(struct sys_device * dev, char * buf)
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
 * register_node - Setup a driverfs device for a node.
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
	sysdev_remove_file(&node->sysdev, &attr_meminfo);
	sysdev_remove_file(&node->sysdev, &attr_numastat);
	sysdev_remove_file(&node->sysdev, &attr_distance);

	sysdev_unregister(&node->sysdev);
}

static int __init register_node_type(void)
{
	return sysdev_class_register(&node_class);
}
postcore_initcall(register_node_type);
