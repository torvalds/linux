// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2007, 2011
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#define KMSG_COMPONENT "cpu"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/workqueue.h>
#include <linux/bootmem.h>
#include <linux/uaccess.h>
#include <linux/sysctl.h>
#include <linux/cpuset.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/topology.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/nodemask.h>
#include <linux/node.h>
#include <asm/sysinfo.h>
#include <asm/numa.h>

#define PTF_HORIZONTAL	(0UL)
#define PTF_VERTICAL	(1UL)
#define PTF_CHECK	(2UL)

enum {
	TOPOLOGY_MODE_HW,
	TOPOLOGY_MODE_SINGLE,
	TOPOLOGY_MODE_PACKAGE,
	TOPOLOGY_MODE_UNINITIALIZED
};

struct mask_info {
	struct mask_info *next;
	unsigned char id;
	cpumask_t mask;
};

static int topology_mode = TOPOLOGY_MODE_UNINITIALIZED;
static void set_topology_timer(void);
static void topology_work_fn(struct work_struct *work);
static struct sysinfo_15_1_x *tl_info;

static DECLARE_WORK(topology_work, topology_work_fn);

/*
 * Socket/Book linked lists and cpu_topology updates are
 * protected by "sched_domains_mutex".
 */
static struct mask_info socket_info;
static struct mask_info book_info;
static struct mask_info drawer_info;

struct cpu_topology_s390 cpu_topology[NR_CPUS];
EXPORT_SYMBOL_GPL(cpu_topology);

cpumask_t cpus_with_topology;

static cpumask_t cpu_group_map(struct mask_info *info, unsigned int cpu)
{
	cpumask_t mask;

	cpumask_copy(&mask, cpumask_of(cpu));
	switch (topology_mode) {
	case TOPOLOGY_MODE_HW:
		while (info) {
			if (cpumask_test_cpu(cpu, &info->mask)) {
				mask = info->mask;
				break;
			}
			info = info->next;
		}
		if (cpumask_empty(&mask))
			cpumask_copy(&mask, cpumask_of(cpu));
		break;
	case TOPOLOGY_MODE_PACKAGE:
		cpumask_copy(&mask, cpu_present_mask);
		break;
	default:
		/* fallthrough */
	case TOPOLOGY_MODE_SINGLE:
		cpumask_copy(&mask, cpumask_of(cpu));
		break;
	}
	return mask;
}

static cpumask_t cpu_thread_map(unsigned int cpu)
{
	cpumask_t mask;
	int i;

	cpumask_copy(&mask, cpumask_of(cpu));
	if (topology_mode != TOPOLOGY_MODE_HW)
		return mask;
	cpu -= cpu % (smp_cpu_mtid + 1);
	for (i = 0; i <= smp_cpu_mtid; i++)
		if (cpu_present(cpu + i))
			cpumask_set_cpu(cpu + i, &mask);
	return mask;
}

#define TOPOLOGY_CORE_BITS	64

static void add_cpus_to_mask(struct topology_core *tl_core,
			     struct mask_info *drawer,
			     struct mask_info *book,
			     struct mask_info *socket)
{
	struct cpu_topology_s390 *topo;
	unsigned int core;

	for_each_set_bit(core, &tl_core->mask, TOPOLOGY_CORE_BITS) {
		unsigned int rcore;
		int lcpu, i;

		rcore = TOPOLOGY_CORE_BITS - 1 - core + tl_core->origin;
		lcpu = smp_find_processor_id(rcore << smp_cpu_mt_shift);
		if (lcpu < 0)
			continue;
		for (i = 0; i <= smp_cpu_mtid; i++) {
			topo = &cpu_topology[lcpu + i];
			topo->drawer_id = drawer->id;
			topo->book_id = book->id;
			topo->socket_id = socket->id;
			topo->core_id = rcore;
			topo->thread_id = lcpu + i;
			topo->dedicated = tl_core->d;
			cpumask_set_cpu(lcpu + i, &drawer->mask);
			cpumask_set_cpu(lcpu + i, &book->mask);
			cpumask_set_cpu(lcpu + i, &socket->mask);
			cpumask_set_cpu(lcpu + i, &cpus_with_topology);
			smp_cpu_set_polarization(lcpu + i, tl_core->pp);
		}
	}
}

static void clear_masks(void)
{
	struct mask_info *info;

	info = &socket_info;
	while (info) {
		cpumask_clear(&info->mask);
		info = info->next;
	}
	info = &book_info;
	while (info) {
		cpumask_clear(&info->mask);
		info = info->next;
	}
	info = &drawer_info;
	while (info) {
		cpumask_clear(&info->mask);
		info = info->next;
	}
}

static union topology_entry *next_tle(union topology_entry *tle)
{
	if (!tle->nl)
		return (union topology_entry *)((struct topology_core *)tle + 1);
	return (union topology_entry *)((struct topology_container *)tle + 1);
}

static void tl_to_masks(struct sysinfo_15_1_x *info)
{
	struct mask_info *socket = &socket_info;
	struct mask_info *book = &book_info;
	struct mask_info *drawer = &drawer_info;
	union topology_entry *tle, *end;

	clear_masks();
	tle = info->tle;
	end = (union topology_entry *)((unsigned long)info + info->length);
	while (tle < end) {
		switch (tle->nl) {
		case 3:
			drawer = drawer->next;
			drawer->id = tle->container.id;
			break;
		case 2:
			book = book->next;
			book->id = tle->container.id;
			break;
		case 1:
			socket = socket->next;
			socket->id = tle->container.id;
			break;
		case 0:
			add_cpus_to_mask(&tle->cpu, drawer, book, socket);
			break;
		default:
			clear_masks();
			return;
		}
		tle = next_tle(tle);
	}
}

static void topology_update_polarization_simple(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		smp_cpu_set_polarization(cpu, POLARIZATION_HRZ);
}

static int ptf(unsigned long fc)
{
	int rc;

	asm volatile(
		"	.insn	rre,0xb9a20000,%1,%1\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (rc)
		: "d" (fc)  : "cc");
	return rc;
}

int topology_set_cpu_management(int fc)
{
	int cpu, rc;

	if (!MACHINE_HAS_TOPOLOGY)
		return -EOPNOTSUPP;
	if (fc)
		rc = ptf(PTF_VERTICAL);
	else
		rc = ptf(PTF_HORIZONTAL);
	if (rc)
		return -EBUSY;
	for_each_possible_cpu(cpu)
		smp_cpu_set_polarization(cpu, POLARIZATION_UNKNOWN);
	return rc;
}

static void update_cpu_masks(void)
{
	struct cpu_topology_s390 *topo;
	int cpu, id;

	for_each_possible_cpu(cpu) {
		topo = &cpu_topology[cpu];
		topo->thread_mask = cpu_thread_map(cpu);
		topo->core_mask = cpu_group_map(&socket_info, cpu);
		topo->book_mask = cpu_group_map(&book_info, cpu);
		topo->drawer_mask = cpu_group_map(&drawer_info, cpu);
		if (topology_mode != TOPOLOGY_MODE_HW) {
			id = topology_mode == TOPOLOGY_MODE_PACKAGE ? 0 : cpu;
			topo->thread_id = cpu;
			topo->core_id = cpu;
			topo->socket_id = id;
			topo->book_id = id;
			topo->drawer_id = id;
			if (cpu_present(cpu))
				cpumask_set_cpu(cpu, &cpus_with_topology);
		}
	}
	numa_update_cpu_topology();
}

void store_topology(struct sysinfo_15_1_x *info)
{
	stsi(info, 15, 1, topology_mnest_limit());
}

static void __arch_update_dedicated_flag(void *arg)
{
	if (topology_cpu_dedicated(smp_processor_id()))
		set_cpu_flag(CIF_DEDICATED_CPU);
	else
		clear_cpu_flag(CIF_DEDICATED_CPU);
}

static int __arch_update_cpu_topology(void)
{
	struct sysinfo_15_1_x *info = tl_info;
	int rc = 0;

	mutex_lock(&smp_cpu_state_mutex);
	cpumask_clear(&cpus_with_topology);
	if (MACHINE_HAS_TOPOLOGY) {
		rc = 1;
		store_topology(info);
		tl_to_masks(info);
	}
	update_cpu_masks();
	if (!MACHINE_HAS_TOPOLOGY)
		topology_update_polarization_simple();
	mutex_unlock(&smp_cpu_state_mutex);
	return rc;
}

int arch_update_cpu_topology(void)
{
	struct device *dev;
	int cpu, rc;

	rc = __arch_update_cpu_topology();
	on_each_cpu(__arch_update_dedicated_flag, NULL, 0);
	for_each_online_cpu(cpu) {
		dev = get_cpu_device(cpu);
		if (dev)
			kobject_uevent(&dev->kobj, KOBJ_CHANGE);
	}
	return rc;
}

static void topology_work_fn(struct work_struct *work)
{
	rebuild_sched_domains();
}

void topology_schedule_update(void)
{
	schedule_work(&topology_work);
}

static void topology_flush_work(void)
{
	flush_work(&topology_work);
}

static void topology_timer_fn(struct timer_list *unused)
{
	if (ptf(PTF_CHECK))
		topology_schedule_update();
	set_topology_timer();
}

static struct timer_list topology_timer;

static atomic_t topology_poll = ATOMIC_INIT(0);

static void set_topology_timer(void)
{
	if (atomic_add_unless(&topology_poll, -1, 0))
		mod_timer(&topology_timer, jiffies + HZ / 10);
	else
		mod_timer(&topology_timer, jiffies + HZ * 60);
}

void topology_expect_change(void)
{
	if (!MACHINE_HAS_TOPOLOGY)
		return;
	/* This is racy, but it doesn't matter since it is just a heuristic.
	 * Worst case is that we poll in a higher frequency for a bit longer.
	 */
	if (atomic_read(&topology_poll) > 60)
		return;
	atomic_add(60, &topology_poll);
	set_topology_timer();
}

static int cpu_management;

static ssize_t dispatching_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	count = sprintf(buf, "%d\n", cpu_management);
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}

static ssize_t dispatching_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t count)
{
	int val, rc;
	char delim;

	if (sscanf(buf, "%d %c", &val, &delim) != 1)
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;
	rc = 0;
	get_online_cpus();
	mutex_lock(&smp_cpu_state_mutex);
	if (cpu_management == val)
		goto out;
	rc = topology_set_cpu_management(val);
	if (rc)
		goto out;
	cpu_management = val;
	topology_expect_change();
out:
	mutex_unlock(&smp_cpu_state_mutex);
	put_online_cpus();
	return rc ? rc : count;
}
static DEVICE_ATTR_RW(dispatching);

static ssize_t cpu_polarization_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int cpu = dev->id;
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	switch (smp_cpu_get_polarization(cpu)) {
	case POLARIZATION_HRZ:
		count = sprintf(buf, "horizontal\n");
		break;
	case POLARIZATION_VL:
		count = sprintf(buf, "vertical:low\n");
		break;
	case POLARIZATION_VM:
		count = sprintf(buf, "vertical:medium\n");
		break;
	case POLARIZATION_VH:
		count = sprintf(buf, "vertical:high\n");
		break;
	default:
		count = sprintf(buf, "unknown\n");
		break;
	}
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}
static DEVICE_ATTR(polarization, 0444, cpu_polarization_show, NULL);

static struct attribute *topology_cpu_attrs[] = {
	&dev_attr_polarization.attr,
	NULL,
};

static struct attribute_group topology_cpu_attr_group = {
	.attrs = topology_cpu_attrs,
};

static ssize_t cpu_dedicated_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int cpu = dev->id;
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	count = sprintf(buf, "%d\n", topology_cpu_dedicated(cpu));
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}
static DEVICE_ATTR(dedicated, 0444, cpu_dedicated_show, NULL);

static struct attribute *topology_extra_cpu_attrs[] = {
	&dev_attr_dedicated.attr,
	NULL,
};

static struct attribute_group topology_extra_cpu_attr_group = {
	.attrs = topology_extra_cpu_attrs,
};

int topology_cpu_init(struct cpu *cpu)
{
	int rc;

	rc = sysfs_create_group(&cpu->dev.kobj, &topology_cpu_attr_group);
	if (rc || !MACHINE_HAS_TOPOLOGY)
		return rc;
	rc = sysfs_create_group(&cpu->dev.kobj, &topology_extra_cpu_attr_group);
	if (rc)
		sysfs_remove_group(&cpu->dev.kobj, &topology_cpu_attr_group);
	return rc;
}

static const struct cpumask *cpu_thread_mask(int cpu)
{
	return &cpu_topology[cpu].thread_mask;
}


const struct cpumask *cpu_coregroup_mask(int cpu)
{
	return &cpu_topology[cpu].core_mask;
}

static const struct cpumask *cpu_book_mask(int cpu)
{
	return &cpu_topology[cpu].book_mask;
}

static const struct cpumask *cpu_drawer_mask(int cpu)
{
	return &cpu_topology[cpu].drawer_mask;
}

static struct sched_domain_topology_level s390_topology[] = {
	{ cpu_thread_mask, cpu_smt_flags, SD_INIT_NAME(SMT) },
	{ cpu_coregroup_mask, cpu_core_flags, SD_INIT_NAME(MC) },
	{ cpu_book_mask, SD_INIT_NAME(BOOK) },
	{ cpu_drawer_mask, SD_INIT_NAME(DRAWER) },
	{ cpu_cpu_mask, SD_INIT_NAME(DIE) },
	{ NULL, },
};

static void __init alloc_masks(struct sysinfo_15_1_x *info,
			       struct mask_info *mask, int offset)
{
	int i, nr_masks;

	nr_masks = info->mag[TOPOLOGY_NR_MAG - offset];
	for (i = 0; i < info->mnest - offset; i++)
		nr_masks *= info->mag[TOPOLOGY_NR_MAG - offset - 1 - i];
	nr_masks = max(nr_masks, 1);
	for (i = 0; i < nr_masks; i++) {
		mask->next = memblock_virt_alloc(sizeof(*mask->next), 8);
		mask = mask->next;
	}
}

void __init topology_init_early(void)
{
	struct sysinfo_15_1_x *info;

	set_sched_topology(s390_topology);
	if (topology_mode == TOPOLOGY_MODE_UNINITIALIZED) {
		if (MACHINE_HAS_TOPOLOGY)
			topology_mode = TOPOLOGY_MODE_HW;
		else
			topology_mode = TOPOLOGY_MODE_SINGLE;
	}
	if (!MACHINE_HAS_TOPOLOGY)
		goto out;
	tl_info = memblock_virt_alloc(PAGE_SIZE, PAGE_SIZE);
	info = tl_info;
	store_topology(info);
	pr_info("The CPU configuration topology of the machine is: %d %d %d %d %d %d / %d\n",
		info->mag[0], info->mag[1], info->mag[2], info->mag[3],
		info->mag[4], info->mag[5], info->mnest);
	alloc_masks(info, &socket_info, 1);
	alloc_masks(info, &book_info, 2);
	alloc_masks(info, &drawer_info, 3);
out:
	__arch_update_cpu_topology();
	__arch_update_dedicated_flag(NULL);
}

static inline int topology_get_mode(int enabled)
{
	if (!enabled)
		return TOPOLOGY_MODE_SINGLE;
	return MACHINE_HAS_TOPOLOGY ? TOPOLOGY_MODE_HW : TOPOLOGY_MODE_PACKAGE;
}

static inline int topology_is_enabled(void)
{
	return topology_mode != TOPOLOGY_MODE_SINGLE;
}

static int __init topology_setup(char *str)
{
	bool enabled;
	int rc;

	rc = kstrtobool(str, &enabled);
	if (rc)
		return rc;
	topology_mode = topology_get_mode(enabled);
	return 0;
}
early_param("topology", topology_setup);

static int topology_ctl_handler(struct ctl_table *ctl, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int enabled = topology_is_enabled();
	int new_mode;
	int zero = 0;
	int one = 1;
	int rc;
	struct ctl_table ctl_entry = {
		.procname	= ctl->procname,
		.data		= &enabled,
		.maxlen		= sizeof(int),
		.extra1		= &zero,
		.extra2		= &one,
	};

	rc = proc_douintvec_minmax(&ctl_entry, write, buffer, lenp, ppos);
	if (rc < 0 || !write)
		return rc;

	mutex_lock(&smp_cpu_state_mutex);
	new_mode = topology_get_mode(enabled);
	if (topology_mode != new_mode) {
		topology_mode = new_mode;
		topology_schedule_update();
	}
	mutex_unlock(&smp_cpu_state_mutex);
	topology_flush_work();

	return rc;
}

static struct ctl_table topology_ctl_table[] = {
	{
		.procname	= "topology",
		.mode		= 0644,
		.proc_handler	= topology_ctl_handler,
	},
	{ },
};

static struct ctl_table topology_dir_table[] = {
	{
		.procname	= "s390",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= topology_ctl_table,
	},
	{ },
};

static int __init topology_init(void)
{
	timer_setup(&topology_timer, topology_timer_fn, TIMER_DEFERRABLE);
	if (MACHINE_HAS_TOPOLOGY)
		set_topology_timer();
	else
		topology_update_polarization_simple();
	register_sysctl_table(topology_dir_table);
	return device_create_file(cpu_subsys.dev_root, &dev_attr_dispatching);
}
device_initcall(topology_init);
