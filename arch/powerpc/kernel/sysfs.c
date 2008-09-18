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
#include <asm/machdep.h>
#include <asm/smp.h>

#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/lppaca.h>
#endif

static DEFINE_PER_CPU(struct cpu, cpu_devices);

static DEFINE_PER_CPU(struct kobject *, cache_toplevel);

/*
 * SMT snooze delay stuff, 64-bit only for now
 */

#ifdef CONFIG_PPC64

/* Time in microseconds we delay before sleeping in the idle loop */
DEFINE_PER_CPU(unsigned long, smt_snooze_delay) = { 100 };

static ssize_t store_smt_snooze_delay(struct sys_device *dev,
				      struct sysdev_attribute *attr,
				      const char *buf,
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

static ssize_t show_smt_snooze_delay(struct sys_device *dev,
				     struct sysdev_attribute *attr,
				     char *buf)
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

#endif /* CONFIG_PPC64 */

/*
 * Enabling PMCs will slow partition context switch times so we only do
 * it the first time we write to the PMCs.
 */

static DEFINE_PER_CPU(char, pmcs_enabled);

void ppc_enable_pmcs(void)
{
	/* Only need to enable them once */
	if (__get_cpu_var(pmcs_enabled))
		return;

	__get_cpu_var(pmcs_enabled) = 1;

	if (ppc_md.enable_pmcs)
		ppc_md.enable_pmcs();
}
EXPORT_SYMBOL(ppc_enable_pmcs);

#if defined(CONFIG_6xx) || defined(CONFIG_PPC64)
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
#endif

#define SYSFS_PMCSETUP(NAME, ADDRESS) \
static unsigned long read_##NAME(unsigned long junk) \
{ \
	return mfspr(ADDRESS); \
} \
static unsigned long write_##NAME(unsigned long val) \
{ \
	ppc_enable_pmcs(); \
	mtspr(ADDRESS, val); \
	return 0; \
} \
static ssize_t show_##NAME(struct sys_device *dev, \
			struct sysdev_attribute *attr, \
			char *buf) \
{ \
	struct cpu *cpu = container_of(dev, struct cpu, sysdev); \
	unsigned long val = run_on_cpu(cpu->sysdev.id, read_##NAME, 0); \
	return sprintf(buf, "%lx\n", val); \
} \
static ssize_t __used \
	store_##NAME(struct sys_device *dev, struct sysdev_attribute *attr, \
			const char *buf, size_t count) \
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

#if defined(CONFIG_PPC64)
#define HAS_PPC_PMC_CLASSIC	1
#define HAS_PPC_PMC_IBM		1
#define HAS_PPC_PMC_PA6T	1
#elif defined(CONFIG_6xx)
#define HAS_PPC_PMC_CLASSIC	1
#define HAS_PPC_PMC_IBM		1
#define HAS_PPC_PMC_G4		1
#endif


#ifdef HAS_PPC_PMC_CLASSIC
SYSFS_PMCSETUP(mmcr0, SPRN_MMCR0);
SYSFS_PMCSETUP(mmcr1, SPRN_MMCR1);
SYSFS_PMCSETUP(pmc1, SPRN_PMC1);
SYSFS_PMCSETUP(pmc2, SPRN_PMC2);
SYSFS_PMCSETUP(pmc3, SPRN_PMC3);
SYSFS_PMCSETUP(pmc4, SPRN_PMC4);
SYSFS_PMCSETUP(pmc5, SPRN_PMC5);
SYSFS_PMCSETUP(pmc6, SPRN_PMC6);

#ifdef HAS_PPC_PMC_G4
SYSFS_PMCSETUP(mmcr2, SPRN_MMCR2);
#endif

#ifdef CONFIG_PPC64
SYSFS_PMCSETUP(pmc7, SPRN_PMC7);
SYSFS_PMCSETUP(pmc8, SPRN_PMC8);

SYSFS_PMCSETUP(mmcra, SPRN_MMCRA);
SYSFS_PMCSETUP(purr, SPRN_PURR);
SYSFS_PMCSETUP(spurr, SPRN_SPURR);
SYSFS_PMCSETUP(dscr, SPRN_DSCR);

static SYSDEV_ATTR(mmcra, 0600, show_mmcra, store_mmcra);
static SYSDEV_ATTR(spurr, 0600, show_spurr, NULL);
static SYSDEV_ATTR(dscr, 0600, show_dscr, store_dscr);
static SYSDEV_ATTR(purr, 0600, show_purr, store_purr);
#endif /* CONFIG_PPC64 */

#ifdef HAS_PPC_PMC_PA6T
SYSFS_PMCSETUP(pa6t_pmc0, SPRN_PA6T_PMC0);
SYSFS_PMCSETUP(pa6t_pmc1, SPRN_PA6T_PMC1);
SYSFS_PMCSETUP(pa6t_pmc2, SPRN_PA6T_PMC2);
SYSFS_PMCSETUP(pa6t_pmc3, SPRN_PA6T_PMC3);
SYSFS_PMCSETUP(pa6t_pmc4, SPRN_PA6T_PMC4);
SYSFS_PMCSETUP(pa6t_pmc5, SPRN_PA6T_PMC5);
#ifdef CONFIG_DEBUG_KERNEL
SYSFS_PMCSETUP(hid0, SPRN_HID0);
SYSFS_PMCSETUP(hid1, SPRN_HID1);
SYSFS_PMCSETUP(hid4, SPRN_HID4);
SYSFS_PMCSETUP(hid5, SPRN_HID5);
SYSFS_PMCSETUP(ima0, SPRN_PA6T_IMA0);
SYSFS_PMCSETUP(ima1, SPRN_PA6T_IMA1);
SYSFS_PMCSETUP(ima2, SPRN_PA6T_IMA2);
SYSFS_PMCSETUP(ima3, SPRN_PA6T_IMA3);
SYSFS_PMCSETUP(ima4, SPRN_PA6T_IMA4);
SYSFS_PMCSETUP(ima5, SPRN_PA6T_IMA5);
SYSFS_PMCSETUP(ima6, SPRN_PA6T_IMA6);
SYSFS_PMCSETUP(ima7, SPRN_PA6T_IMA7);
SYSFS_PMCSETUP(ima8, SPRN_PA6T_IMA8);
SYSFS_PMCSETUP(ima9, SPRN_PA6T_IMA9);
SYSFS_PMCSETUP(imaat, SPRN_PA6T_IMAAT);
SYSFS_PMCSETUP(btcr, SPRN_PA6T_BTCR);
SYSFS_PMCSETUP(pccr, SPRN_PA6T_PCCR);
SYSFS_PMCSETUP(rpccr, SPRN_PA6T_RPCCR);
SYSFS_PMCSETUP(der, SPRN_PA6T_DER);
SYSFS_PMCSETUP(mer, SPRN_PA6T_MER);
SYSFS_PMCSETUP(ber, SPRN_PA6T_BER);
SYSFS_PMCSETUP(ier, SPRN_PA6T_IER);
SYSFS_PMCSETUP(sier, SPRN_PA6T_SIER);
SYSFS_PMCSETUP(siar, SPRN_PA6T_SIAR);
SYSFS_PMCSETUP(tsr0, SPRN_PA6T_TSR0);
SYSFS_PMCSETUP(tsr1, SPRN_PA6T_TSR1);
SYSFS_PMCSETUP(tsr2, SPRN_PA6T_TSR2);
SYSFS_PMCSETUP(tsr3, SPRN_PA6T_TSR3);
#endif /* CONFIG_DEBUG_KERNEL */
#endif /* HAS_PPC_PMC_PA6T */

#ifdef HAS_PPC_PMC_IBM
static struct sysdev_attribute ibm_common_attrs[] = {
	_SYSDEV_ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0),
	_SYSDEV_ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1),
};
#endif /* HAS_PPC_PMC_G4 */

#ifdef HAS_PPC_PMC_G4
static struct sysdev_attribute g4_common_attrs[] = {
	_SYSDEV_ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0),
	_SYSDEV_ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1),
	_SYSDEV_ATTR(mmcr2, 0600, show_mmcr2, store_mmcr2),
};
#endif /* HAS_PPC_PMC_G4 */

static struct sysdev_attribute classic_pmc_attrs[] = {
	_SYSDEV_ATTR(pmc1, 0600, show_pmc1, store_pmc1),
	_SYSDEV_ATTR(pmc2, 0600, show_pmc2, store_pmc2),
	_SYSDEV_ATTR(pmc3, 0600, show_pmc3, store_pmc3),
	_SYSDEV_ATTR(pmc4, 0600, show_pmc4, store_pmc4),
	_SYSDEV_ATTR(pmc5, 0600, show_pmc5, store_pmc5),
	_SYSDEV_ATTR(pmc6, 0600, show_pmc6, store_pmc6),
#ifdef CONFIG_PPC64
	_SYSDEV_ATTR(pmc7, 0600, show_pmc7, store_pmc7),
	_SYSDEV_ATTR(pmc8, 0600, show_pmc8, store_pmc8),
#endif
};

#ifdef HAS_PPC_PMC_PA6T
static struct sysdev_attribute pa6t_attrs[] = {
	_SYSDEV_ATTR(mmcr0, 0600, show_mmcr0, store_mmcr0),
	_SYSDEV_ATTR(mmcr1, 0600, show_mmcr1, store_mmcr1),
	_SYSDEV_ATTR(pmc0, 0600, show_pa6t_pmc0, store_pa6t_pmc0),
	_SYSDEV_ATTR(pmc1, 0600, show_pa6t_pmc1, store_pa6t_pmc1),
	_SYSDEV_ATTR(pmc2, 0600, show_pa6t_pmc2, store_pa6t_pmc2),
	_SYSDEV_ATTR(pmc3, 0600, show_pa6t_pmc3, store_pa6t_pmc3),
	_SYSDEV_ATTR(pmc4, 0600, show_pa6t_pmc4, store_pa6t_pmc4),
	_SYSDEV_ATTR(pmc5, 0600, show_pa6t_pmc5, store_pa6t_pmc5),
#ifdef CONFIG_DEBUG_KERNEL
	_SYSDEV_ATTR(hid0, 0600, show_hid0, store_hid0),
	_SYSDEV_ATTR(hid1, 0600, show_hid1, store_hid1),
	_SYSDEV_ATTR(hid4, 0600, show_hid4, store_hid4),
	_SYSDEV_ATTR(hid5, 0600, show_hid5, store_hid5),
	_SYSDEV_ATTR(ima0, 0600, show_ima0, store_ima0),
	_SYSDEV_ATTR(ima1, 0600, show_ima1, store_ima1),
	_SYSDEV_ATTR(ima2, 0600, show_ima2, store_ima2),
	_SYSDEV_ATTR(ima3, 0600, show_ima3, store_ima3),
	_SYSDEV_ATTR(ima4, 0600, show_ima4, store_ima4),
	_SYSDEV_ATTR(ima5, 0600, show_ima5, store_ima5),
	_SYSDEV_ATTR(ima6, 0600, show_ima6, store_ima6),
	_SYSDEV_ATTR(ima7, 0600, show_ima7, store_ima7),
	_SYSDEV_ATTR(ima8, 0600, show_ima8, store_ima8),
	_SYSDEV_ATTR(ima9, 0600, show_ima9, store_ima9),
	_SYSDEV_ATTR(imaat, 0600, show_imaat, store_imaat),
	_SYSDEV_ATTR(btcr, 0600, show_btcr, store_btcr),
	_SYSDEV_ATTR(pccr, 0600, show_pccr, store_pccr),
	_SYSDEV_ATTR(rpccr, 0600, show_rpccr, store_rpccr),
	_SYSDEV_ATTR(der, 0600, show_der, store_der),
	_SYSDEV_ATTR(mer, 0600, show_mer, store_mer),
	_SYSDEV_ATTR(ber, 0600, show_ber, store_ber),
	_SYSDEV_ATTR(ier, 0600, show_ier, store_ier),
	_SYSDEV_ATTR(sier, 0600, show_sier, store_sier),
	_SYSDEV_ATTR(siar, 0600, show_siar, store_siar),
	_SYSDEV_ATTR(tsr0, 0600, show_tsr0, store_tsr0),
	_SYSDEV_ATTR(tsr1, 0600, show_tsr1, store_tsr1),
	_SYSDEV_ATTR(tsr2, 0600, show_tsr2, store_tsr2),
	_SYSDEV_ATTR(tsr3, 0600, show_tsr3, store_tsr3),
#endif /* CONFIG_DEBUG_KERNEL */
};
#endif /* HAS_PPC_PMC_PA6T */
#endif /* HAS_PPC_PMC_CLASSIC */

struct cache_desc {
	struct kobject kobj;
	struct cache_desc *next;
	const char *type;	/* Instruction, Data, or Unified */
	u32 size;		/* total cache size in KB */
	u32 line_size;		/* in bytes */
	u32 nr_sets;		/* number of sets */
	u32 level;		/* e.g. 1, 2, 3... */
	u32 associativity;	/* e.g. 8-way... 0 is fully associative */
};

DEFINE_PER_CPU(struct cache_desc *, cache_desc);

static struct cache_desc *kobj_to_cache_desc(struct kobject *k)
{
	return container_of(k, struct cache_desc, kobj);
}

static void cache_desc_release(struct kobject *k)
{
	struct cache_desc *desc = kobj_to_cache_desc(k);

	pr_debug("%s: releasing %s\n", __func__, kobject_name(k));

	if (desc->next)
		kobject_put(&desc->next->kobj);

	kfree(kobj_to_cache_desc(k));
}

static ssize_t cache_desc_show(struct kobject *k, struct attribute *attr, char *buf)
{
	struct kobj_attribute *kobj_attr;

	kobj_attr = container_of(attr, struct kobj_attribute, attr);

	return kobj_attr->show(k, kobj_attr, buf);
}

static struct sysfs_ops cache_desc_sysfs_ops = {
	.show = cache_desc_show,
};

static struct kobj_type cache_desc_type = {
	.release = cache_desc_release,
	.sysfs_ops = &cache_desc_sysfs_ops,
};

static ssize_t cache_size_show(struct kobject *k, struct kobj_attribute *attr, char *buf)
{
	struct cache_desc *cache = kobj_to_cache_desc(k);

	return sprintf(buf, "%uK\n", cache->size);
}

static struct kobj_attribute cache_size_attr =
	__ATTR(size, 0444, cache_size_show, NULL);

static ssize_t cache_line_size_show(struct kobject *k, struct kobj_attribute *attr, char *buf)
{
	struct cache_desc *cache = kobj_to_cache_desc(k);

	return sprintf(buf, "%u\n", cache->line_size);
}

static struct kobj_attribute cache_line_size_attr =
	__ATTR(coherency_line_size, 0444, cache_line_size_show, NULL);

static ssize_t cache_nr_sets_show(struct kobject *k, struct kobj_attribute *attr, char *buf)
{
	struct cache_desc *cache = kobj_to_cache_desc(k);

	return sprintf(buf, "%u\n", cache->nr_sets);
}

static struct kobj_attribute cache_nr_sets_attr =
	__ATTR(number_of_sets, 0444, cache_nr_sets_show, NULL);

static ssize_t cache_type_show(struct kobject *k, struct kobj_attribute *attr, char *buf)
{
	struct cache_desc *cache = kobj_to_cache_desc(k);

	return sprintf(buf, "%s\n", cache->type);
}

static struct kobj_attribute cache_type_attr =
	__ATTR(type, 0444, cache_type_show, NULL);

static ssize_t cache_level_show(struct kobject *k, struct kobj_attribute *attr, char *buf)
{
	struct cache_desc *cache = kobj_to_cache_desc(k);

	return sprintf(buf, "%u\n", cache->level);
}

static struct kobj_attribute cache_level_attr =
	__ATTR(level, 0444, cache_level_show, NULL);

static ssize_t cache_assoc_show(struct kobject *k, struct kobj_attribute *attr, char *buf)
{
	struct cache_desc *cache = kobj_to_cache_desc(k);

	return sprintf(buf, "%u\n", cache->associativity);
}

static struct kobj_attribute cache_assoc_attr =
	__ATTR(ways_of_associativity, 0444, cache_assoc_show, NULL);

struct cache_desc_info {
	const char *type;
	const char *size_prop;
	const char *line_size_prop;
	const char *nr_sets_prop;
};

/* PowerPC Processor binding says the [di]-cache-* must be equal on
 * unified caches, so just use d-cache properties. */
static struct cache_desc_info ucache_info = {
	.type = "Unified",
	.size_prop = "d-cache-size",
	.line_size_prop = "d-cache-line-size",
	.nr_sets_prop = "d-cache-sets",
};

static struct cache_desc_info dcache_info = {
	.type = "Data",
	.size_prop = "d-cache-size",
	.line_size_prop = "d-cache-line-size",
	.nr_sets_prop = "d-cache-sets",
};

static struct cache_desc_info icache_info = {
	.type = "Instruction",
	.size_prop = "i-cache-size",
	.line_size_prop = "i-cache-line-size",
	.nr_sets_prop = "i-cache-sets",
};

static struct cache_desc * __cpuinit create_cache_desc(struct device_node *np, struct kobject *parent, int index, int level, struct cache_desc_info *info)
{
	const u32 *cache_line_size;
	struct cache_desc *new;
	const u32 *cache_size;
	const u32 *nr_sets;
	int rc;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	rc = kobject_init_and_add(&new->kobj, &cache_desc_type, parent,
				  "index%d", index);
	if (rc)
		goto err;

	/* type */
	new->type = info->type;
	rc = sysfs_create_file(&new->kobj, &cache_type_attr.attr);
	WARN_ON(rc);

	/* level */
	new->level = level;
	rc = sysfs_create_file(&new->kobj, &cache_level_attr.attr);
	WARN_ON(rc);

	/* size */
	cache_size = of_get_property(np, info->size_prop, NULL);
	if (cache_size) {
		new->size = *cache_size / 1024;
		rc = sysfs_create_file(&new->kobj,
				       &cache_size_attr.attr);
		WARN_ON(rc);
	}

	/* coherency_line_size */
	cache_line_size = of_get_property(np, info->line_size_prop, NULL);
	if (cache_line_size) {
		new->line_size = *cache_line_size;
		rc = sysfs_create_file(&new->kobj,
				       &cache_line_size_attr.attr);
		WARN_ON(rc);
	}

	/* number_of_sets */
	nr_sets = of_get_property(np, info->nr_sets_prop, NULL);
	if (nr_sets) {
		new->nr_sets = *nr_sets;
		rc = sysfs_create_file(&new->kobj,
				       &cache_nr_sets_attr.attr);
		WARN_ON(rc);
	}

	/* ways_of_associativity */
	if (new->nr_sets == 1) {
		/* fully associative */
		new->associativity = 0;
		goto create_assoc;
	}

	if (new->nr_sets && new->size && new->line_size) {
		/* If we have values for all of these we can derive
		 * the associativity. */
		new->associativity =
			((new->size * 1024) / new->nr_sets) / new->line_size;
create_assoc:
		rc = sysfs_create_file(&new->kobj,
				       &cache_assoc_attr.attr);
		WARN_ON(rc);
	}

	return new;
err:
	kfree(new);
	return NULL;
}

static bool cache_is_unified(struct device_node *np)
{
	return of_get_property(np, "cache-unified", NULL);
}

static struct cache_desc * __cpuinit create_cache_index_info(struct device_node *np, struct kobject *parent, int index, int level)
{
	const phandle *next_cache_phandle;
	struct device_node *next_cache;
	struct cache_desc *new, **end;

	pr_debug("%s(node = %s, index = %d)\n", __func__, np->full_name, index);

	if (cache_is_unified(np)) {
		new = create_cache_desc(np, parent, index, level,
					&ucache_info);
	} else {
		new = create_cache_desc(np, parent, index, level,
					&dcache_info);
		if (new) {
			index++;
			new->next = create_cache_desc(np, parent, index, level,
						      &icache_info);
		}
	}
	if (!new)
		return NULL;

	end = &new->next;
	while (*end)
		end = &(*end)->next;

	next_cache_phandle = of_get_property(np, "l2-cache", NULL);
	if (!next_cache_phandle)
		goto out;

	next_cache = of_find_node_by_phandle(*next_cache_phandle);
	if (!next_cache)
		goto out;

	*end = create_cache_index_info(next_cache, parent, ++index, ++level);

	of_node_put(next_cache);
out:
	return new;
}

static void __cpuinit create_cache_info(struct sys_device *sysdev)
{
	struct kobject *cache_toplevel;
	struct device_node *np = NULL;
	int cpu = sysdev->id;

	cache_toplevel = kobject_create_and_add("cache", &sysdev->kobj);
	if (!cache_toplevel)
		return;
	per_cpu(cache_toplevel, cpu) = cache_toplevel;
	np = of_get_cpu_node(cpu, NULL);
	if (np != NULL) {
		per_cpu(cache_desc, cpu) =
			create_cache_index_info(np, cache_toplevel, 0, 1);
		of_node_put(np);
	}
	return;
}

static void __cpuinit register_cpu_online(unsigned int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;
	struct sysdev_attribute *attrs, *pmc_attrs;
	int i, nattrs;

#ifdef CONFIG_PPC64
	if (!firmware_has_feature(FW_FEATURE_ISERIES) &&
			cpu_has_feature(CPU_FTR_SMT))
		sysdev_create_file(s, &attr_smt_snooze_delay);
#endif

	/* PMC stuff */
	switch (cur_cpu_spec->pmc_type) {
#ifdef HAS_PPC_PMC_IBM
	case PPC_PMC_IBM:
		attrs = ibm_common_attrs;
		nattrs = sizeof(ibm_common_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = classic_pmc_attrs;
		break;
#endif /* HAS_PPC_PMC_IBM */
#ifdef HAS_PPC_PMC_G4
	case PPC_PMC_G4:
		attrs = g4_common_attrs;
		nattrs = sizeof(g4_common_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = classic_pmc_attrs;
		break;
#endif /* HAS_PPC_PMC_G4 */
#ifdef HAS_PPC_PMC_PA6T
	case PPC_PMC_PA6T:
		/* PA Semi starts counting at PMC0 */
		attrs = pa6t_attrs;
		nattrs = sizeof(pa6t_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = NULL;
		break;
#endif /* HAS_PPC_PMC_PA6T */
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

#ifdef CONFIG_PPC64
	if (cpu_has_feature(CPU_FTR_MMCRA))
		sysdev_create_file(s, &attr_mmcra);

	if (cpu_has_feature(CPU_FTR_PURR))
		sysdev_create_file(s, &attr_purr);

	if (cpu_has_feature(CPU_FTR_SPURR))
		sysdev_create_file(s, &attr_spurr);

	if (cpu_has_feature(CPU_FTR_DSCR))
		sysdev_create_file(s, &attr_dscr);
#endif /* CONFIG_PPC64 */

	create_cache_info(s);
}

#ifdef CONFIG_HOTPLUG_CPU
static void remove_cache_info(struct sys_device *sysdev)
{
	struct kobject *cache_toplevel;
	struct cache_desc *cache_desc;
	int cpu = sysdev->id;

	cache_desc = per_cpu(cache_desc, cpu);
	if (cache_desc != NULL)
		kobject_put(&cache_desc->kobj);

	cache_toplevel = per_cpu(cache_toplevel, cpu);
	if (cache_toplevel != NULL)
		kobject_put(cache_toplevel);
}

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
#ifdef HAS_PPC_PMC_IBM
	case PPC_PMC_IBM:
		attrs = ibm_common_attrs;
		nattrs = sizeof(ibm_common_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = classic_pmc_attrs;
		break;
#endif /* HAS_PPC_PMC_IBM */
#ifdef HAS_PPC_PMC_G4
	case PPC_PMC_G4:
		attrs = g4_common_attrs;
		nattrs = sizeof(g4_common_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = classic_pmc_attrs;
		break;
#endif /* HAS_PPC_PMC_G4 */
#ifdef HAS_PPC_PMC_PA6T
	case PPC_PMC_PA6T:
		/* PA Semi starts counting at PMC0 */
		attrs = pa6t_attrs;
		nattrs = sizeof(pa6t_attrs) / sizeof(struct sysdev_attribute);
		pmc_attrs = NULL;
		break;
#endif /* HAS_PPC_PMC_PA6T */
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

#ifdef CONFIG_PPC64
	if (cpu_has_feature(CPU_FTR_MMCRA))
		sysdev_remove_file(s, &attr_mmcra);

	if (cpu_has_feature(CPU_FTR_PURR))
		sysdev_remove_file(s, &attr_purr);

	if (cpu_has_feature(CPU_FTR_SPURR))
		sysdev_remove_file(s, &attr_spurr);

	if (cpu_has_feature(CPU_FTR_DSCR))
		sysdev_remove_file(s, &attr_dscr);
#endif /* CONFIG_PPC64 */

	remove_cache_info(s);
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
	int ret;

	mutex_lock(&cpu_mutex);

	for_each_possible_cpu(cpu) {
		sysdev = get_cpu_sysdev(cpu);
		ret = sysfs_create_group(&sysdev->kobj, attrs);
		WARN_ON(ret != 0);
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
static ssize_t show_physical_id(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
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
