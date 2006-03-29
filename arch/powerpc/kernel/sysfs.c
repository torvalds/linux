#include <linux/config.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/nodemask.h>
#include <linux/cpumask.h>
#include <linux/notifier.h>

#include <asm/current.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <asm/prom.h>
#include <asm/paca.h>
#include <asm/lppaca.h>
#include <asm/machdep.h>
#include <asm/smp.h>

static DEFINE_PER_CPU(struct cpu, cpu_devices);

/* SMT stuff */

#ifdef CONFIG_PPC_MULTIPLATFORM
/* default to snooze disabled */
DEFINE_PER_CPU(unsigned long, smt_snooze_delay);

static ssize_t store_smt_snooze_delay(struct sys_device *dev, const char *buf,
				      size_t count)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);
	ssize_t ret;
	unsigned long snooze;

	ret = sscanf(buf, "%lu", &snooze);
	if (ret != 1)
		return -EINVAL;

	per_cpu(smt_snooze_delay, cpu->sysdev.id) = snooze;

	return count;
}

static ssize_t show_smt_snooze_delay(struct sys_device *dev, char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);

	return sprintf(buf, "%lu\n", per_cpu(smt_snooze_delay, cpu->sysdev.id));
}

static SYSDEV_ATTR(smt_snooze_delay, 0644, show_smt_snooze_delay,
		   store_smt_snooze_delay);

/* Only parse OF options if the matching cmdline option was not specified */
static int smt_snooze_cmdline;

static int __init smt_setup(void)
{
	struct device_node *options;
	unsigned int *val;
	unsigned int cpu;

	if (!cpu_has_feature(CPU_FTR_SMT))
		return -ENODEV;

	options = find_path_device("/options");
	if (!options)
		return -ENODEV;

	val = (unsigned int *)get_property(options, "ibm,smt-snooze-delay",
					   NULL);
	if (!smt_snooze_cmdline && val) {
		for_each_possible_cpu(cpu)
			per_cpu(smt_snooze_delay, cpu) = *val;
	}

	return 0;
}
__initcall(smt_setup);

static int __init setup_smt_snooze_delay(char *str)
{
	unsigned int cpu;
	int snooze;

	if (!cpu_has_feature(CPU_FTR_SMT))
		return 1;

	smt_snooze_cmdline = 1;

	if (get_option(&str, &snooze)) {
		for_each_possible_cpu(cpu)
			per_cpu(smt_snooze_delay, cpu) = snooze;
	}

	return 1;
}
__setup("smt-snooze-delay=", setup_smt_snooze_delay);

#endif /* CONFIG_PPC_MULTIPLATFORM */

/*
 * Enabling PMCs will slow partition context switch times so we only do
 * it the first time we write to the PMCs.
 */

static DEFINE_PER_CPU(char, pmcs_enabled);

void ppc64_enable_pmcs(void)
{
	/* Only need to enable them once */
	if (__get_cpu_var(pmcs_enabled))
		return;

	__get_cpu_var(pmcs_enabled) = 1;

	if (ppc_md.enable_pmcs)
		ppc_md.enable_pmcs();
}
EXPORT_SYMBOL(ppc64_enable_pmcs);

/* XXX convert to rusty's on_one_cpu */
static unsigned long run_on_cpu(unsigned long cpu,
			        unsigned long (*func)(unsigned long),
				unsigned long arg)
{
	cpumask_t old_affinity = current->cpus_allowed;
	unsigned long ret;

	/* should return -EINVAL to userspace */
	if (set_cpus_allowed(current, cpumask_of_cpu(cpu)))
		return 0;

	ret = func(arg);

	set_cpus_allowed(current, old_affinity);

	return ret;
}

#define SYSFS_PMCSETUP(NAME, ADDRESS) \
static unsigned long read_##NAME(unsigned long junk) \
{ \
	return mfspr(ADDRESS); \
} \
static unsigned long write_##NAME(unsigned long val) \
{ \
	ppc64_enable_pmcs(); \
	mtspr(ADDRESS, val); \
	return 0; \
} \
static ssize_t show_##NAME(struct sys_device *dev, char *buf) \
{ \
	struct cpu *cpu = container_of(dev, struct cpu, sysdev); \
	unsigned long val = run_on_cpu(cpu->sysdev.id, read_##NAME, 0); \
	return sprintf(buf, "%lx\n", val); \
} \
static ssize_t __attribute_used__ \
	store_##NAME(struct sys_device *dev, const char *buf, size_t count) \
{ \
	struct cpu *cpu = container_of(dev, struct cpu, sysdev); \
	unsigned long val; \
	int ret = sscanf(buf, "%lx", &val); \
	if (ret != 1) \
		return -EINVAL; \
	run_on_cpu(cpu->sysdev.id, write_##NAME, val); \
	return count; \
}

SYSFS_PMCSETUP(mmcr0, SPRN_MMCR0);
SYSFS_PMCSETUP(mmcr1, SPRN_MMCR1);
SYSFS_PMCSETUP(mmcra, SPRN_MMCRA);
SYSFS_PMCSETUP(pmc1, SPRN_PMC1);
SYSFS_PMCSETUP(pmc2, SPRN_PMC2);
SYSFS_PMCSETUP(pmc3, SPRN_PMC3);
SYSFS_PMCSETUP(pmc4, SPRN_PMC4);
SYSFS_PMCSETUP(pmc5, SPRN_PMC5);
SYSFS_PMCSETUP(pmc6, SPRN_PMC6);
SYSFS_PMCSETUP(pmc7, SPRN_PMC7);
SYSFS_PMCSETUP(pmc8, SPRN_PMC8);
SYSFS_PMCSETUP(purr, SPRN_PURR);

static SYSDEV_ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0);
static SYSDEV_ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1);
static SYSDEV_ATTR(mmcra, 0600, show_mmcra, store_mmcra);
static SYSDEV_ATTR(pmc1, 0600, show_pmc1, store_pmc1);
static SYSDEV_ATTR(pmc2, 0600, show_pmc2, store_pmc2);
static SYSDEV_ATTR(pmc3, 0600, show_pmc3, store_pmc3);
static SYSDEV_ATTR(pmc4, 0600, show_pmc4, store_pmc4);
static SYSDEV_ATTR(pmc5, 0600, show_pmc5, store_pmc5);
static SYSDEV_ATTR(pmc6, 0600, show_pmc6, store_pmc6);
static SYSDEV_ATTR(pmc7, 0600, show_pmc7, store_pmc7);
static SYSDEV_ATTR(pmc8, 0600, show_pmc8, store_pmc8);
static SYSDEV_ATTR(purr, 0600, show_purr, NULL);

static void register_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;

#ifndef CONFIG_PPC_ISERIES
	if (cpu_has_feature(CPU_FTR_SMT))
		sysdev_create_file(s, &attr_smt_snooze_delay);
#endif

	/* PMC stuff */

	sysdev_create_file(s, &attr_mmcr0);
	sysdev_create_file(s, &attr_mmcr1);

	if (cpu_has_feature(CPU_FTR_MMCRA))
		sysdev_create_file(s, &attr_mmcra);

	if (cur_cpu_spec->num_pmcs >= 1)
		sysdev_create_file(s, &attr_pmc1);
	if (cur_cpu_spec->num_pmcs >= 2)
		sysdev_create_file(s, &attr_pmc2);
	if (cur_cpu_spec->num_pmcs >= 3)
		sysdev_create_file(s, &attr_pmc3);
	if (cur_cpu_spec->num_pmcs >= 4)
		sysdev_create_file(s, &attr_pmc4);
	if (cur_cpu_spec->num_pmcs >= 5)
		sysdev_create_file(s, &attr_pmc5);
	if (cur_cpu_spec->num_pmcs >= 6)
		sysdev_create_file(s, &attr_pmc6);
	if (cur_cpu_spec->num_pmcs >= 7)
		sysdev_create_file(s, &attr_pmc7);
	if (cur_cpu_spec->num_pmcs >= 8)
		sysdev_create_file(s, &attr_pmc8);

	if (cpu_has_feature(CPU_FTR_SMT))
		sysdev_create_file(s, &attr_purr);
}

#ifdef CONFIG_HOTPLUG_CPU
static void unregister_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;

	BUG_ON(c->no_control);

#ifndef CONFIG_PPC_ISERIES
	if (cpu_has_feature(CPU_FTR_SMT))
		sysdev_remove_file(s, &attr_smt_snooze_delay);
#endif

	/* PMC stuff */

	sysdev_remove_file(s, &attr_mmcr0);
	sysdev_remove_file(s, &attr_mmcr1);

	if (cpu_has_feature(CPU_FTR_MMCRA))
		sysdev_remove_file(s, &attr_mmcra);

	if (cur_cpu_spec->num_pmcs >= 1)
		sysdev_remove_file(s, &attr_pmc1);
	if (cur_cpu_spec->num_pmcs >= 2)
		sysdev_remove_file(s, &attr_pmc2);
	if (cur_cpu_spec->num_pmcs >= 3)
		sysdev_remove_file(s, &attr_pmc3);
	if (cur_cpu_spec->num_pmcs >= 4)
		sysdev_remove_file(s, &attr_pmc4);
	if (cur_cpu_spec->num_pmcs >= 5)
		sysdev_remove_file(s, &attr_pmc5);
	if (cur_cpu_spec->num_pmcs >= 6)
		sysdev_remove_file(s, &attr_pmc6);
	if (cur_cpu_spec->num_pmcs >= 7)
		sysdev_remove_file(s, &attr_pmc7);
	if (cur_cpu_spec->num_pmcs >= 8)
		sysdev_remove_file(s, &attr_pmc8);

	if (cpu_has_feature(CPU_FTR_SMT))
		sysdev_remove_file(s, &attr_purr);
}
#endif /* CONFIG_HOTPLUG_CPU */

static int __devinit sysfs_cpu_notify(struct notifier_block *self,
				      unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned int)(long)hcpu;

	switch (action) {
	case CPU_ONLINE:
		register_cpu_online(cpu);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
		unregister_cpu_online(cpu);
		break;
#endif
	}
	return NOTIFY_OK;
}

static struct notifier_block __devinitdata sysfs_cpu_nb = {
	.notifier_call	= sysfs_cpu_notify,
};

/* NUMA stuff */

#ifdef CONFIG_NUMA
static struct node node_devices[MAX_NUMNODES];

static void register_nodes(void)
{
	int i;

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (node_online(i)) {
			int p_node = parent_node(i);
			struct node *parent = NULL;

			if (p_node != i)
				parent = &node_devices[p_node];

			register_node(&node_devices[i], i, parent);
		}
	}
}
#else
static void register_nodes(void)
{
	return;
}
#endif

/* Only valid if CPU is present. */
static ssize_t show_physical_id(struct sys_device *dev, char *buf)
{
	struct cpu *cpu = container_of(dev, struct cpu, sysdev);

	return sprintf(buf, "%d\n", get_hard_smp_processor_id(cpu->sysdev.id));
}
static SYSDEV_ATTR(physical_id, 0444, show_physical_id, NULL);

static int __init topology_init(void)
{
	int cpu;
	struct node *parent = NULL;

	register_nodes();

	register_cpu_notifier(&sysfs_cpu_nb);

	for_each_possible_cpu(cpu) {
		struct cpu *c = &per_cpu(cpu_devices, cpu);

#ifdef CONFIG_NUMA
		/* The node to which a cpu belongs can't be known
		 * until the cpu is made present.
		 */
		parent = NULL;
		if (cpu_present(cpu))
			parent = &node_devices[cpu_to_node(cpu)];
#endif
		/*
		 * For now, we just see if the system supports making
		 * the RTAS calls for CPU hotplug.  But, there may be a
		 * more comprehensive way to do this for an individual
		 * CPU.  For instance, the boot cpu might never be valid
		 * for hotplugging.
		 */
		if (!ppc_md.cpu_die)
			c->no_control = 1;

		if (cpu_online(cpu) || (c->no_control == 0)) {
			register_cpu(c, cpu, parent);

			sysdev_create_file(&c->sysdev, &attr_physical_id);
		}

		if (cpu_online(cpu))
			register_cpu_online(cpu);
	}

	return 0;
}
__initcall(topology_init);
