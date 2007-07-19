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
/* Time in microseconds we delay before sleeping in the idle loop */
DEFINE_PER_CPU(unsigned long, smt_snooze_delay) = { 100 };

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
	const unsigned int *val;
	unsigned int cpu;

	if (!cpu_has_feature(CPU_FTR_SMT))
		return -ENODEV;

	options = of_find_node_by_path("/options");
	if (!options)
		return -ENODEV;

	val = of_get_property(options, "ibm,smt-snooze-delay", NULL);
	if (!smt_snooze_cmdline && val) {
		for_each_possible_cpu(cpu)
			per_cpu(smt_snooze_delay, cpu) = *val;
	}

	of_node_put(options);
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


/* Let's define all possible registers, we'll only hook up the ones
 * that are implemented on the current processor
 */

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
SYSFS_PMCSETUP(spurr, SPRN_SPURR);
SYSFS_PMCSETUP(dscr, SPRN_DSCR);

SYSFS_PMCSETUP(pa6t_pmc0, SPRN_PA6T_PMC0);
SYSFS_PMCSETUP(pa6t_pmc1, SPRN_PA6T_PMC1);
SYSFS_PMCSETUP(pa6t_pmc2, SPRN_PA6T_PMC2);
SYSFS_PMCSETUP(pa6t_pmc3, SPRN_PA6T_PMC3);
SYSFS_PMCSETUP(pa6t_pmc4, SPRN_PA6T_PMC4);
SYSFS_PMCSETUP(pa6t_pmc5, SPRN_PA6T_PMC5);


static SYSDEV_ATTR(mmcra, 0600, show_mmcra, store_mmcra);
static SYSDEV_ATTR(spurr, 0600, show_spurr, NULL);
static SYSDEV_ATTR(dscr, 0600, show_dscr, store_dscr);
static SYSDEV_ATTR(purr, 0600, show_purr, store_purr);

static struct sysdev_attribute ibm_common_attrs[] = {
	_SYSDEV_ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0),
	_SYSDEV_ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1),
};

static struct sysdev_attribute ibm_pmc_attrs[] = {
	_SYSDEV_ATTR(pmc1, 0600, show_pmc1, store_pmc1),
	_SYSDEV_ATTR(pmc2, 0600, show_pmc2, store_pmc2),
	_SYSDEV_ATTR(pmc3, 0600, show_pmc3, store_pmc3),
	_SYSDEV_ATTR(pmc4, 0600, show_pmc4, store_pmc4),
	_SYSDEV_ATTR(pmc5, 0600, show_pmc5, store_pmc5),
	_SYSDEV_ATTR(pmc6, 0600, show_pmc6, store_pmc6),
	_SYSDEV_ATTR(pmc7, 0600, show_pmc7, store_pmc7),
	_SYSDEV_ATTR(pmc8, 0600, show_pmc8, store_pmc8),
};

static struct sysdev_attribute pa6t_attrs[] = {
	_SYSDEV_ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0),
	_SYSDEV_ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1),
	_SYSDEV_ATTR(pmc0, 0600, show_pa6t_pmc0, store_pa6t_pmc0),
	_SYSDEV_ATTR(pmc1, 0600, show_pa6t_pmc1, store_pa6t_pmc1),
	_SYSDEV_ATTR(pmc2, 0600, show_pa6t_pmc2, store_pa6t_pmc2),
	_SYSDEV_ATTR(pmc3, 0600, show_pa6t_pmc3, store_pa6t_pmc3),
	_SYSDEV_ATTR(pmc4, 0600, show_pa6t_pmc4, store_pa6t_pmc4),
	_SYSDEV_ATTR(pmc5, 0600, show_pa6t_pmc5, store_pa6t_pmc5),
};


static void register_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;
	struct sysdev_attribute *attrs, *pmc_attrs;
	int i, nattrs;

	if (!firmware_has_feature(FW_FEATURE_ISERIES) &&
			cpu_has_feature(CPU_FTR_SMT))
		sysdev_create_file(s, &attr_smt_snooze_delay);

	/* PMC stuff */
	switch (cur_cpu_spec->pmc_type) {
	case PPC_PMC_IBM:
		attrs = ibm_common_attrs;
		nattrs = sizeof(ibm_common_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = ibm_pmc_attrs;
		break;
	case PPC_PMC_PA6T:
		/* PA Semi starts counting at PMC0 */
		attrs = pa6t_attrs;
		nattrs = sizeof(pa6t_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = NULL;
		break;
	default:
		attrs = NULL;
		nattrs = 0;
		pmc_attrs = NULL;
	}

	for (i = 0; i < nattrs; i++)
		sysdev_create_file(s, &attrs[i]);

	if (pmc_attrs)
		for (i = 0; i < cur_cpu_spec->num_pmcs; i++)
			sysdev_create_file(s, &pmc_attrs[i]);

	if (cpu_has_feature(CPU_FTR_MMCRA))
		sysdev_create_file(s, &attr_mmcra);

	if (cpu_has_feature(CPU_FTR_PURR))
		sysdev_create_file(s, &attr_purr);

	if (cpu_has_feature(CPU_FTR_SPURR))
		sysdev_create_file(s, &attr_spurr);

	if (cpu_has_feature(CPU_FTR_DSCR))
		sysdev_create_file(s, &attr_dscr);
}

#ifdef CONFIG_HOTPLUG_CPU
static void unregister_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;
	struct sysdev_attribute *attrs, *pmc_attrs;
	int i, nattrs;

	BUG_ON(!c->hotpluggable);

	if (!firmware_has_feature(FW_FEATURE_ISERIES) &&
			cpu_has_feature(CPU_FTR_SMT))
		sysdev_remove_file(s, &attr_smt_snooze_delay);

	/* PMC stuff */
	switch (cur_cpu_spec->pmc_type) {
	case PPC_PMC_IBM:
		attrs = ibm_common_attrs;
		nattrs = sizeof(ibm_common_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = ibm_pmc_attrs;
		break;
	case PPC_PMC_PA6T:
		/* PA Semi starts counting at PMC0 */
		attrs = pa6t_attrs;
		nattrs = sizeof(pa6t_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = NULL;
		break;
	default:
		attrs = NULL;
		nattrs = 0;
		pmc_attrs = NULL;
	}

	for (i = 0; i < nattrs; i++)
		sysdev_remove_file(s, &attrs[i]);

	if (pmc_attrs)
		for (i = 0; i < cur_cpu_spec->num_pmcs; i++)
			sysdev_remove_file(s, &pmc_attrs[i]);

	if (cpu_has_feature(CPU_FTR_MMCRA))
		sysdev_remove_file(s, &attr_mmcra);

	if (cpu_has_feature(CPU_FTR_PURR))
		sysdev_remove_file(s, &attr_purr);

	if (cpu_has_feature(CPU_FTR_SPURR))
		sysdev_remove_file(s, &attr_spurr);

	if (cpu_has_feature(CPU_FTR_DSCR))
		sysdev_remove_file(s, &attr_dscr);
}
#endif /* CONFIG_HOTPLUG_CPU */

static int __cpuinit sysfs_cpu_notify(struct notifier_block *self,
				      unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned int)(long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		register_cpu_online(cpu);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		unregister_cpu_online(cpu);
		break;
#endif
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata sysfs_cpu_nb = {
	.notifier_call	= sysfs_cpu_notify,
};

static DEFINE_MUTEX(cpu_mutex);

int cpu_add_sysdev_attr(struct sysdev_attribute *attr)
{
	int cpu;

	mutex_lock(&cpu_mutex);

	for_each_possible_cpu(cpu) {
		sysdev_create_file(get_cpu_sysdev(cpu), attr);
	}

	mutex_unlock(&cpu_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(cpu_add_sysdev_attr);

int cpu_add_sysdev_attr_group(struct attribute_group *attrs)
{
	int cpu;
	struct sys_device *sysdev;

	mutex_lock(&cpu_mutex);

	for_each_possible_cpu(cpu) {
		sysdev = get_cpu_sysdev(cpu);
		sysfs_create_group(&sysdev->kobj, attrs);
	}

	mutex_unlock(&cpu_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(cpu_add_sysdev_attr_group);


void cpu_remove_sysdev_attr(struct sysdev_attribute *attr)
{
	int cpu;

	mutex_lock(&cpu_mutex);

	for_each_possible_cpu(cpu) {
		sysdev_remove_file(get_cpu_sysdev(cpu), attr);
	}

	mutex_unlock(&cpu_mutex);
}
EXPORT_SYMBOL_GPL(cpu_remove_sysdev_attr);

void cpu_remove_sysdev_attr_group(struct attribute_group *attrs)
{
	int cpu;
	struct sys_device *sysdev;

	mutex_lock(&cpu_mutex);

	for_each_possible_cpu(cpu) {
		sysdev = get_cpu_sysdev(cpu);
		sysfs_remove_group(&sysdev->kobj, attrs);
	}

	mutex_unlock(&cpu_mutex);
}
EXPORT_SYMBOL_GPL(cpu_remove_sysdev_attr_group);


/* NUMA stuff */

#ifdef CONFIG_NUMA
static void register_nodes(void)
{
	int i;

	for (i = 0; i < MAX_NUMNODES; i++)
		register_one_node(i);
}

int sysfs_add_device_to_node(struct sys_device *dev, int nid)
{
	struct node *node = &node_devices[nid];
	return sysfs_create_link(&node->sysdev.kobj, &dev->kobj,
			kobject_name(&dev->kobj));
}
EXPORT_SYMBOL_GPL(sysfs_add_device_to_node);

void sysfs_remove_device_from_node(struct sys_device *dev, int nid)
{
	struct node *node = &node_devices[nid];
	sysfs_remove_link(&node->sysdev.kobj, kobject_name(&dev->kobj));
}
EXPORT_SYMBOL_GPL(sysfs_remove_device_from_node);

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

	register_nodes();
	register_cpu_notifier(&sysfs_cpu_nb);

	for_each_possible_cpu(cpu) {
		struct cpu *c = &per_cpu(cpu_devices, cpu);

		/*
		 * For now, we just see if the system supports making
		 * the RTAS calls for CPU hotplug.  But, there may be a
		 * more comprehensive way to do this for an individual
		 * CPU.  For instance, the boot cpu might never be valid
		 * for hotplugging.
		 */
		if (ppc_md.cpu_die)
			c->hotpluggable = 1;

		if (cpu_online(cpu) || c->hotpluggable) {
			register_cpu(c, cpu);

			sysdev_create_file(&c->sysdev, &attr_physical_id);
		}

		if (cpu_online(cpu))
			register_cpu_online(cpu);
	}

	return 0;
}
subsys_initcall(topology_init);
