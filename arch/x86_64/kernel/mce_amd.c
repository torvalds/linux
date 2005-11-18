/*
 *  (c) 2005 Advanced Micro Devices, Inc.
 *  Your use of this code is subject to the terms and conditions of the
 *  GNU general public license version 2. See "COPYING" or
 *  http://www.gnu.org/licenses/gpl.html
 *
 *  Written by Jacob Shin - AMD, Inc.
 *
 *  Support : jacob.shin@amd.com
 *
 *  MC4_MISC0 DRAM ECC Error Threshold available under AMD K8 Rev F.
 *  MC4_MISC0 exists per physical processor.
 *
 */

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/sysdev.h>
#include <linux/sysfs.h>
#include <asm/apic.h>
#include <asm/mce.h>
#include <asm/msr.h>
#include <asm/percpu.h>

#define PFX "mce_threshold: "
#define VERSION "version 1.00.9"
#define NR_BANKS 5
#define THRESHOLD_MAX 0xFFF
#define INT_TYPE_APIC 0x00020000
#define MASK_VALID_HI 0x80000000
#define MASK_LVTOFF_HI 0x00F00000
#define MASK_COUNT_EN_HI 0x00080000
#define MASK_INT_TYPE_HI 0x00060000
#define MASK_OVERFLOW_HI 0x00010000
#define MASK_ERR_COUNT_HI 0x00000FFF
#define MASK_OVERFLOW 0x0001000000000000L

struct threshold_bank {
	unsigned int cpu;
	u8 bank;
	u8 interrupt_enable;
	u16 threshold_limit;
	struct kobject kobj;
};

static struct threshold_bank threshold_defaults = {
	.interrupt_enable = 0,
	.threshold_limit = THRESHOLD_MAX,
};

#ifdef CONFIG_SMP
static unsigned char shared_bank[NR_BANKS] = {
	0, 0, 0, 0, 1
};
#endif

static DEFINE_PER_CPU(unsigned char, bank_map);	/* see which banks are on */

/*
 * CPU Initialization
 */

/* must be called with correct cpu affinity */
static void threshold_restart_bank(struct threshold_bank *b,
				   int reset, u16 old_limit)
{
	u32 mci_misc_hi, mci_misc_lo;

	rdmsr(MSR_IA32_MC0_MISC + b->bank * 4, mci_misc_lo, mci_misc_hi);

	if (b->threshold_limit < (mci_misc_hi & THRESHOLD_MAX))
		reset = 1;	/* limit cannot be lower than err count */

	if (reset) {		/* reset err count and overflow bit */
		mci_misc_hi =
		    (mci_misc_hi & ~(MASK_ERR_COUNT_HI | MASK_OVERFLOW_HI)) |
		    (THRESHOLD_MAX - b->threshold_limit);
	} else if (old_limit) {	/* change limit w/o reset */
		int new_count = (mci_misc_hi & THRESHOLD_MAX) +
		    (old_limit - b->threshold_limit);
		mci_misc_hi = (mci_misc_hi & ~MASK_ERR_COUNT_HI) |
		    (new_count & THRESHOLD_MAX);
	}

	b->interrupt_enable ?
	    (mci_misc_hi = (mci_misc_hi & ~MASK_INT_TYPE_HI) | INT_TYPE_APIC) :
	    (mci_misc_hi &= ~MASK_INT_TYPE_HI);

	mci_misc_hi |= MASK_COUNT_EN_HI;
	wrmsr(MSR_IA32_MC0_MISC + b->bank * 4, mci_misc_lo, mci_misc_hi);
}

void __cpuinit mce_amd_feature_init(struct cpuinfo_x86 *c)
{
	int bank;
	u32 mci_misc_lo, mci_misc_hi;
	unsigned int cpu = smp_processor_id();

	for (bank = 0; bank < NR_BANKS; ++bank) {
		rdmsr(MSR_IA32_MC0_MISC + bank * 4, mci_misc_lo, mci_misc_hi);

		/* !valid, !counter present, bios locked */
		if (!(mci_misc_hi & MASK_VALID_HI) ||
		    !(mci_misc_hi & MASK_VALID_HI >> 1) ||
		    (mci_misc_hi & MASK_VALID_HI >> 2))
			continue;

		per_cpu(bank_map, cpu) |= (1 << bank);

#ifdef CONFIG_SMP
		if (shared_bank[bank] && cpu_core_id[cpu])
			continue;
#endif

		setup_threshold_lvt((mci_misc_hi & MASK_LVTOFF_HI) >> 20);
		threshold_defaults.cpu = cpu;
		threshold_defaults.bank = bank;
		threshold_restart_bank(&threshold_defaults, 0, 0);
	}
}

/*
 * APIC Interrupt Handler
 */

/*
 * threshold interrupt handler will service THRESHOLD_APIC_VECTOR.
 * the interrupt goes off when error_count reaches threshold_limit.
 * the handler will simply log mcelog w/ software defined bank number.
 */
asmlinkage void mce_threshold_interrupt(void)
{
	int bank;
	struct mce m;

	ack_APIC_irq();
	irq_enter();

	memset(&m, 0, sizeof(m));
	rdtscll(m.tsc);
	m.cpu = smp_processor_id();

	/* assume first bank caused it */
	for (bank = 0; bank < NR_BANKS; ++bank) {
		m.bank = MCE_THRESHOLD_BASE + bank;
		rdmsrl(MSR_IA32_MC0_MISC + bank * 4, m.misc);

		if (m.misc & MASK_OVERFLOW) {
			mce_log(&m);
			goto out;
		}
	}
      out:
	irq_exit();
}

/*
 * Sysfs Interface
 */

static struct sysdev_class threshold_sysclass = {
	set_kset_name("threshold"),
};

static DEFINE_PER_CPU(struct sys_device, device_threshold);

struct threshold_attr {
        struct attribute attr;
        ssize_t(*show) (struct threshold_bank *, char *);
        ssize_t(*store) (struct threshold_bank *, const char *, size_t count);
};

static DEFINE_PER_CPU(struct threshold_bank *, threshold_banks[NR_BANKS]);

static cpumask_t affinity_set(unsigned int cpu)
{
	cpumask_t oldmask = current->cpus_allowed;
	cpumask_t newmask = CPU_MASK_NONE;
	cpu_set(cpu, newmask);
	set_cpus_allowed(current, newmask);
	return oldmask;
}

static void affinity_restore(cpumask_t oldmask)
{
	set_cpus_allowed(current, oldmask);
}

#define SHOW_FIELDS(name) \
        static ssize_t show_ ## name(struct threshold_bank * b, char *buf) \
        { \
                return sprintf(buf, "%lx\n", (unsigned long) b->name); \
        }
SHOW_FIELDS(interrupt_enable)
SHOW_FIELDS(threshold_limit)

static ssize_t store_interrupt_enable(struct threshold_bank *b,
				      const char *buf, size_t count)
{
	char *end;
	cpumask_t oldmask;
	unsigned long new = simple_strtoul(buf, &end, 0);
	if (end == buf)
		return -EINVAL;
	b->interrupt_enable = !!new;

	oldmask = affinity_set(b->cpu);
	threshold_restart_bank(b, 0, 0);
	affinity_restore(oldmask);

	return end - buf;
}

static ssize_t store_threshold_limit(struct threshold_bank *b,
				     const char *buf, size_t count)
{
	char *end;
	cpumask_t oldmask;
	u16 old;
	unsigned long new = simple_strtoul(buf, &end, 0);
	if (end == buf)
		return -EINVAL;
	if (new > THRESHOLD_MAX)
		new = THRESHOLD_MAX;
	if (new < 1)
		new = 1;
	old = b->threshold_limit;
	b->threshold_limit = new;

	oldmask = affinity_set(b->cpu);
	threshold_restart_bank(b, 0, old);
	affinity_restore(oldmask);

	return end - buf;
}

static ssize_t show_error_count(struct threshold_bank *b, char *buf)
{
	u32 high, low;
	cpumask_t oldmask;
	oldmask = affinity_set(b->cpu);
	rdmsr(MSR_IA32_MC0_MISC + b->bank * 4, low, high); /* ignore low 32 */
	affinity_restore(oldmask);
	return sprintf(buf, "%x\n",
		       (high & 0xFFF) - (THRESHOLD_MAX - b->threshold_limit));
}

static ssize_t store_error_count(struct threshold_bank *b,
				 const char *buf, size_t count)
{
	cpumask_t oldmask;
	oldmask = affinity_set(b->cpu);
	threshold_restart_bank(b, 1, 0);
	affinity_restore(oldmask);
	return 1;
}

#define THRESHOLD_ATTR(_name,_mode,_show,_store) {            \
        .attr = {.name = __stringify(_name), .mode = _mode }, \
        .show = _show,                                        \
        .store = _store,                                      \
};

#define ATTR_FIELDS(name) \
        static struct threshold_attr name = \
        THRESHOLD_ATTR(name, 0644, show_## name, store_## name)

ATTR_FIELDS(interrupt_enable);
ATTR_FIELDS(threshold_limit);
ATTR_FIELDS(error_count);

static struct attribute *default_attrs[] = {
	&interrupt_enable.attr,
	&threshold_limit.attr,
	&error_count.attr,
	NULL
};

#define to_bank(k) container_of(k,struct threshold_bank,kobj)
#define to_attr(a) container_of(a,struct threshold_attr,attr)

static ssize_t show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct threshold_bank *b = to_bank(kobj);
	struct threshold_attr *a = to_attr(attr);
	ssize_t ret;
	ret = a->show ? a->show(b, buf) : -EIO;
	return ret;
}

static ssize_t store(struct kobject *kobj, struct attribute *attr,
		     const char *buf, size_t count)
{
	struct threshold_bank *b = to_bank(kobj);
	struct threshold_attr *a = to_attr(attr);
	ssize_t ret;
	ret = a->store ? a->store(b, buf, count) : -EIO;
	return ret;
}

static struct sysfs_ops threshold_ops = {
	.show = show,
	.store = store,
};

static struct kobj_type threshold_ktype = {
	.sysfs_ops = &threshold_ops,
	.default_attrs = default_attrs,
};

/* symlinks sibling shared banks to first core.  first core owns dir/files. */
static __cpuinit int threshold_create_bank(unsigned int cpu, int bank)
{
	int err = 0;
	struct threshold_bank *b = 0;

#ifdef CONFIG_SMP
	if (cpu_core_id[cpu] && shared_bank[bank]) {	/* symlink */
		char name[16];
		unsigned lcpu = first_cpu(cpu_core_map[cpu]);
		if (cpu_core_id[lcpu])
			goto out;	/* first core not up yet */

		b = per_cpu(threshold_banks, lcpu)[bank];
		if (!b)
			goto out;
		sprintf(name, "bank%i", bank);
		err = sysfs_create_link(&per_cpu(device_threshold, cpu).kobj,
					&b->kobj, name);
		if (err)
			goto out;
		per_cpu(threshold_banks, cpu)[bank] = b;
		goto out;
	}
#endif

	b = kmalloc(sizeof(struct threshold_bank), GFP_KERNEL);
	if (!b) {
		err = -ENOMEM;
		goto out;
	}
	memset(b, 0, sizeof(struct threshold_bank));

	b->cpu = cpu;
	b->bank = bank;
	b->interrupt_enable = 0;
	b->threshold_limit = THRESHOLD_MAX;
	kobject_set_name(&b->kobj, "bank%i", bank);
	b->kobj.parent = &per_cpu(device_threshold, cpu).kobj;
	b->kobj.ktype = &threshold_ktype;

	err = kobject_register(&b->kobj);
	if (err) {
		kfree(b);
		goto out;
	}
	per_cpu(threshold_banks, cpu)[bank] = b;
      out:
	return err;
}

/* create dir/files for all valid threshold banks */
static __cpuinit int threshold_create_device(unsigned int cpu)
{
	int bank;
	int err = 0;

	per_cpu(device_threshold, cpu).id = cpu;
	per_cpu(device_threshold, cpu).cls = &threshold_sysclass;
	err = sysdev_register(&per_cpu(device_threshold, cpu));
	if (err)
		goto out;

	for (bank = 0; bank < NR_BANKS; ++bank) {
		if (!(per_cpu(bank_map, cpu) & 1 << bank))
			continue;
		err = threshold_create_bank(cpu, bank);
		if (err)
			goto out;
	}
      out:
	return err;
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * let's be hotplug friendly.
 * in case of multiple core processors, the first core always takes ownership
 *   of shared sysfs dir/files, and rest of the cores will be symlinked to it.
 */

/* cpu hotplug call removes all symlinks before first core dies */
static __cpuinit void threshold_remove_bank(unsigned int cpu, int bank)
{
	struct threshold_bank *b;
	char name[16];

	b = per_cpu(threshold_banks, cpu)[bank];
	if (!b)
		return;
	if (shared_bank[bank] && atomic_read(&b->kobj.kref.refcount) > 2) {
		sprintf(name, "bank%i", bank);
		sysfs_remove_link(&per_cpu(device_threshold, cpu).kobj, name);
		per_cpu(threshold_banks, cpu)[bank] = 0;
	} else {
		kobject_unregister(&b->kobj);
		kfree(per_cpu(threshold_banks, cpu)[bank]);
	}
}

static __cpuinit void threshold_remove_device(unsigned int cpu)
{
	int bank;

	for (bank = 0; bank < NR_BANKS; ++bank) {
		if (!(per_cpu(bank_map, cpu) & 1 << bank))
			continue;
		threshold_remove_bank(cpu, bank);
	}
	sysdev_unregister(&per_cpu(device_threshold, cpu));
}

/* link all existing siblings when first core comes up */
static __cpuinit int threshold_create_symlinks(unsigned int cpu)
{
	int bank, err = 0;
	unsigned int lcpu = 0;

	if (cpu_core_id[cpu])
		return 0;
	for_each_cpu_mask(lcpu, cpu_core_map[cpu]) {
		if (lcpu == cpu)
			continue;
		for (bank = 0; bank < NR_BANKS; ++bank) {
			if (!(per_cpu(bank_map, cpu) & 1 << bank))
				continue;
			if (!shared_bank[bank])
				continue;
			err = threshold_create_bank(lcpu, bank);
		}
	}
	return err;
}

/* remove all symlinks before first core dies. */
static __cpuinit void threshold_remove_symlinks(unsigned int cpu)
{
	int bank;
	unsigned int lcpu = 0;
	if (cpu_core_id[cpu])
		return;
	for_each_cpu_mask(lcpu, cpu_core_map[cpu]) {
		if (lcpu == cpu)
			continue;
		for (bank = 0; bank < NR_BANKS; ++bank) {
			if (!(per_cpu(bank_map, cpu) & 1 << bank))
				continue;
			if (!shared_bank[bank])
				continue;
			threshold_remove_bank(lcpu, bank);
		}
	}
}
#else /* !CONFIG_HOTPLUG_CPU */
static __cpuinit void threshold_create_symlinks(unsigned int cpu)
{
}
static __cpuinit void threshold_remove_symlinks(unsigned int cpu)
{
}
static void threshold_remove_device(unsigned int cpu)
{
}
#endif

/* get notified when a cpu comes on/off */
static __cpuinit int threshold_cpu_callback(struct notifier_block *nfb,
					    unsigned long action, void *hcpu)
{
	/* cpu was unsigned int to begin with */
	unsigned int cpu = (unsigned long)hcpu;

	if (cpu >= NR_CPUS)
		goto out;

	switch (action) {
	case CPU_ONLINE:
		threshold_create_device(cpu);
		threshold_create_symlinks(cpu);
		break;
	case CPU_DOWN_PREPARE:
		threshold_remove_symlinks(cpu);
		break;
	case CPU_DOWN_FAILED:
		threshold_create_symlinks(cpu);
		break;
	case CPU_DEAD:
		threshold_remove_device(cpu);
		break;
	default:
		break;
	}
      out:
	return NOTIFY_OK;
}

static struct notifier_block threshold_cpu_notifier = {
	.notifier_call = threshold_cpu_callback,
};

static __init int threshold_init_device(void)
{
	int err;
	int lcpu = 0;

	err = sysdev_class_register(&threshold_sysclass);
	if (err)
		goto out;

	/* to hit CPUs online before the notifier is up */
	for_each_online_cpu(lcpu) {
		err = threshold_create_device(lcpu);
		if (err)
			goto out;
	}
	register_cpu_notifier(&threshold_cpu_notifier);

      out:
	return err;
}

device_initcall(threshold_init_device);
