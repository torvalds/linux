// SPDX-License-Identifier: GPL-2.0
/* sysfs.c: Topology sysfs support code for sparc64.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/init.h>

#include <asm/cpudata.h>
#include <asm/hypervisor.h>
#include <asm/spitfire.h>

static DEFINE_PER_CPU(struct hv_mmu_statistics, mmu_stats) __attribute__((aligned(64)));

#define SHOW_MMUSTAT_ULONG(NAME) \
static ssize_t show_##NAME(struct device *dev, \
			struct device_attribute *attr, char *buf) \
{ \
	struct hv_mmu_statistics *p = &per_cpu(mmu_stats, dev->id); \
	return sprintf(buf, "%lu\n", p->NAME); \
} \
static DEVICE_ATTR(NAME, 0444, show_##NAME, NULL)

SHOW_MMUSTAT_ULONG(immu_tsb_hits_ctx0_8k_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_ticks_ctx0_8k_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_hits_ctx0_64k_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_ticks_ctx0_64k_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_hits_ctx0_4mb_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_ticks_ctx0_4mb_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_hits_ctx0_256mb_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_ticks_ctx0_256mb_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_hits_ctxnon0_8k_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_ticks_ctxnon0_8k_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_hits_ctxnon0_64k_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_ticks_ctxnon0_64k_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_hits_ctxnon0_4mb_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_ticks_ctxnon0_4mb_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_hits_ctxnon0_256mb_tte);
SHOW_MMUSTAT_ULONG(immu_tsb_ticks_ctxnon0_256mb_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_hits_ctx0_8k_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_ticks_ctx0_8k_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_hits_ctx0_64k_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_ticks_ctx0_64k_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_hits_ctx0_4mb_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_ticks_ctx0_4mb_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_hits_ctx0_256mb_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_ticks_ctx0_256mb_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_hits_ctxnon0_8k_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_ticks_ctxnon0_8k_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_hits_ctxnon0_64k_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_ticks_ctxnon0_64k_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_hits_ctxnon0_4mb_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_ticks_ctxnon0_4mb_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_hits_ctxnon0_256mb_tte);
SHOW_MMUSTAT_ULONG(dmmu_tsb_ticks_ctxnon0_256mb_tte);

static struct attribute *mmu_stat_attrs[] = {
	&dev_attr_immu_tsb_hits_ctx0_8k_tte.attr,
	&dev_attr_immu_tsb_ticks_ctx0_8k_tte.attr,
	&dev_attr_immu_tsb_hits_ctx0_64k_tte.attr,
	&dev_attr_immu_tsb_ticks_ctx0_64k_tte.attr,
	&dev_attr_immu_tsb_hits_ctx0_4mb_tte.attr,
	&dev_attr_immu_tsb_ticks_ctx0_4mb_tte.attr,
	&dev_attr_immu_tsb_hits_ctx0_256mb_tte.attr,
	&dev_attr_immu_tsb_ticks_ctx0_256mb_tte.attr,
	&dev_attr_immu_tsb_hits_ctxnon0_8k_tte.attr,
	&dev_attr_immu_tsb_ticks_ctxnon0_8k_tte.attr,
	&dev_attr_immu_tsb_hits_ctxnon0_64k_tte.attr,
	&dev_attr_immu_tsb_ticks_ctxnon0_64k_tte.attr,
	&dev_attr_immu_tsb_hits_ctxnon0_4mb_tte.attr,
	&dev_attr_immu_tsb_ticks_ctxnon0_4mb_tte.attr,
	&dev_attr_immu_tsb_hits_ctxnon0_256mb_tte.attr,
	&dev_attr_immu_tsb_ticks_ctxnon0_256mb_tte.attr,
	&dev_attr_dmmu_tsb_hits_ctx0_8k_tte.attr,
	&dev_attr_dmmu_tsb_ticks_ctx0_8k_tte.attr,
	&dev_attr_dmmu_tsb_hits_ctx0_64k_tte.attr,
	&dev_attr_dmmu_tsb_ticks_ctx0_64k_tte.attr,
	&dev_attr_dmmu_tsb_hits_ctx0_4mb_tte.attr,
	&dev_attr_dmmu_tsb_ticks_ctx0_4mb_tte.attr,
	&dev_attr_dmmu_tsb_hits_ctx0_256mb_tte.attr,
	&dev_attr_dmmu_tsb_ticks_ctx0_256mb_tte.attr,
	&dev_attr_dmmu_tsb_hits_ctxnon0_8k_tte.attr,
	&dev_attr_dmmu_tsb_ticks_ctxnon0_8k_tte.attr,
	&dev_attr_dmmu_tsb_hits_ctxnon0_64k_tte.attr,
	&dev_attr_dmmu_tsb_ticks_ctxnon0_64k_tte.attr,
	&dev_attr_dmmu_tsb_hits_ctxnon0_4mb_tte.attr,
	&dev_attr_dmmu_tsb_ticks_ctxnon0_4mb_tte.attr,
	&dev_attr_dmmu_tsb_hits_ctxnon0_256mb_tte.attr,
	&dev_attr_dmmu_tsb_ticks_ctxnon0_256mb_tte.attr,
	NULL,
};

static struct attribute_group mmu_stat_group = {
	.attrs = mmu_stat_attrs,
	.name = "mmu_stats",
};

static long read_mmustat_enable(void *data __maybe_unused)
{
	unsigned long ra = 0;

	sun4v_mmustat_info(&ra);

	return ra != 0;
}

static long write_mmustat_enable(void *data)
{
	unsigned long ra, orig_ra, *val = data;

	if (*val)
		ra = __pa(&per_cpu(mmu_stats, smp_processor_id()));
	else
		ra = 0UL;

	return sun4v_mmustat_conf(ra, &orig_ra);
}

static ssize_t show_mmustat_enable(struct device *s,
				struct device_attribute *attr, char *buf)
{
	long val = work_on_cpu(s->id, read_mmustat_enable, NULL);

	return sprintf(buf, "%lx\n", val);
}

static ssize_t store_mmustat_enable(struct device *s,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	unsigned long val;
	long err;
	int ret;

	ret = sscanf(buf, "%lu", &val);
	if (ret != 1)
		return -EINVAL;

	err = work_on_cpu(s->id, write_mmustat_enable, &val);
	if (err)
		return -EIO;

	return count;
}

static DEVICE_ATTR(mmustat_enable, 0644, show_mmustat_enable, store_mmustat_enable);

static int mmu_stats_supported;

static int register_mmu_stats(struct device *s)
{
	if (!mmu_stats_supported)
		return 0;
	device_create_file(s, &dev_attr_mmustat_enable);
	return sysfs_create_group(&s->kobj, &mmu_stat_group);
}

#ifdef CONFIG_HOTPLUG_CPU
static void unregister_mmu_stats(struct device *s)
{
	if (!mmu_stats_supported)
		return;
	sysfs_remove_group(&s->kobj, &mmu_stat_group);
	device_remove_file(s, &dev_attr_mmustat_enable);
}
#endif

#define SHOW_CPUDATA_ULONG_NAME(NAME, MEMBER) \
static ssize_t show_##NAME(struct device *dev, \
		struct device_attribute *attr, char *buf) \
{ \
	cpuinfo_sparc *c = &cpu_data(dev->id); \
	return sprintf(buf, "%lu\n", c->MEMBER); \
}

#define SHOW_CPUDATA_UINT_NAME(NAME, MEMBER) \
static ssize_t show_##NAME(struct device *dev, \
		struct device_attribute *attr, char *buf) \
{ \
	cpuinfo_sparc *c = &cpu_data(dev->id); \
	return sprintf(buf, "%u\n", c->MEMBER); \
}

SHOW_CPUDATA_ULONG_NAME(clock_tick, clock_tick);
SHOW_CPUDATA_UINT_NAME(l1_dcache_size, dcache_size);
SHOW_CPUDATA_UINT_NAME(l1_dcache_line_size, dcache_line_size);
SHOW_CPUDATA_UINT_NAME(l1_icache_size, icache_size);
SHOW_CPUDATA_UINT_NAME(l1_icache_line_size, icache_line_size);
SHOW_CPUDATA_UINT_NAME(l2_cache_size, ecache_size);
SHOW_CPUDATA_UINT_NAME(l2_cache_line_size, ecache_line_size);

static struct device_attribute cpu_core_attrs[] = {
	__ATTR(clock_tick,          0444, show_clock_tick, NULL),
	__ATTR(l1_dcache_size,      0444, show_l1_dcache_size, NULL),
	__ATTR(l1_dcache_line_size, 0444, show_l1_dcache_line_size, NULL),
	__ATTR(l1_icache_size,      0444, show_l1_icache_size, NULL),
	__ATTR(l1_icache_line_size, 0444, show_l1_icache_line_size, NULL),
	__ATTR(l2_cache_size,       0444, show_l2_cache_size, NULL),
	__ATTR(l2_cache_line_size,  0444, show_l2_cache_line_size, NULL),
};

static DEFINE_PER_CPU(struct cpu, cpu_devices);

static int register_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct device *s = &c->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(cpu_core_attrs); i++)
		device_create_file(s, &cpu_core_attrs[i]);

	register_mmu_stats(s);
	return 0;
}

static int unregister_cpu_online(unsigned int cpu)
{
#ifdef CONFIG_HOTPLUG_CPU
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct device *s = &c->dev;
	int i;

	unregister_mmu_stats(s);
	for (i = 0; i < ARRAY_SIZE(cpu_core_attrs); i++)
		device_remove_file(s, &cpu_core_attrs[i]);
#endif
	return 0;
}

static void __init check_mmu_stats(void)
{
	unsigned long dummy1, err;

	if (tlb_type != hypervisor)
		return;

	err = sun4v_mmustat_info(&dummy1);
	if (!err)
		mmu_stats_supported = 1;
}

static void register_nodes(void)
{
#ifdef CONFIG_NUMA
	int i;

	for (i = 0; i < MAX_NUMNODES; i++)
		register_one_node(i);
#endif
}

static int __init topology_init(void)
{
	int cpu, ret;

	register_nodes();

	check_mmu_stats();

	for_each_possible_cpu(cpu) {
		struct cpu *c = &per_cpu(cpu_devices, cpu);

		register_cpu(c, cpu);
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "sparc/topology:online",
				register_cpu_online, unregister_cpu_online);
	WARN_ON(ret < 0);
	return 0;
}

subsys_initcall(topology_init);
