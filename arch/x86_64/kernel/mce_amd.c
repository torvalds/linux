/*
 *  (c) 2005, 2006 Advanced Micro Devices, Inc.
 *  Your use of this code is subject to the terms and conditions of the
 *  GNU general public license version 2. See "COPYING" or
 *  http://www.gnu.org/licenses/gpl.html
 *
 *  Written by Jacob Shin - AMD, Inc.
 *
 *  Support : jacob.shin@amd.com
 *
 *  April 2006
 *     - added support for AMD Family 0x10 processors
 *
 *  All MC4_MISCi registers are shared between multi-cores
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
#include <asm/idle.h>

#define PFX               "mce_threshold: "
#define VERSION           "version 1.1.1"
#define NR_BANKS          6
#define NR_BLOCKS         9
#define THRESHOLD_MAX     0xFFF
#define INT_TYPE_APIC     0x00020000
#define MASK_VALID_HI     0x80000000
#define MASK_CNTP_HI      0x40000000
#define MASK_LOCKED_HI    0x20000000
#define MASK_LVTOFF_HI    0x00F00000
#define MASK_COUNT_EN_HI  0x00080000
#define MASK_INT_TYPE_HI  0x00060000
#define MASK_OVERFLOW_HI  0x00010000
#define MASK_ERR_COUNT_HI 0x00000FFF
#define MASK_BLKPTR_LO    0xFF000000
#define MCG_XBLK_ADDR     0xC0000400

struct threshold_block {
	unsigned int block;
	unsigned int bank;
	unsigned int cpu;
	u32 address;
	u16 interrupt_enable;
	u16 threshold_limit;
	struct kobject kobj;
	struct list_head miscj;
};

/* defaults used early on boot */
static struct threshold_block threshold_defaults = {
	.interrupt_enable = 0,
	.threshold_limit = THRESHOLD_MAX,
};

struct threshold_bank {
	struct kobject kobj;
	struct threshold_block *blocks;
	cpumask_t cpus;
};
static DEFINE_PER_CPU(struct threshold_bank *, threshold_banks[NR_BANKS]);

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
static void threshold_restart_bank(struct threshold_block *b,
				   int reset, u16 old_limit)
{
	u32 mci_misc_hi, mci_misc_lo;

	rdmsr(b->address, mci_misc_lo, mci_misc_hi);

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
	wrmsr(b->address, mci_misc_lo, mci_misc_hi);
}

/* cpu init entry point, called from mce.c with preempt off */
void __cpuinit mce_amd_feature_init(struct cpuinfo_x86 *c)
{
	unsigned int bank, block;
	unsigned int cpu = smp_processor_id();
	u32 low = 0, high = 0, address = 0;

	for (bank = 0; bank < NR_BANKS; ++bank) {
		for (block = 0; block < NR_BLOCKS; ++block) {
			if (block == 0)
				address = MSR_IA32_MC0_MISC + bank * 4;
			else if (block == 1) {
				address = (low & MASK_BLKPTR_LO) >> 21;
				if (!address)
					break;
				address += MCG_XBLK_ADDR;
			}
			else
				++address;

			if (rdmsr_safe(address, &low, &high))
				break;

			if (!(high & MASK_VALID_HI)) {
				if (block)
					continue;
				else
					break;
			}

			if (!(high & MASK_CNTP_HI)  ||
			     (high & MASK_LOCKED_HI))
				continue;

			if (!block)
				per_cpu(bank_map, cpu) |= (1 << bank);
#ifdef CONFIG_SMP
			if (shared_bank[bank] && c->cpu_core_id)
				break;
#endif
			high &= ~MASK_LVTOFF_HI;
			high |= K8_APIC_EXT_LVT_ENTRY_THRESHOLD << 20;
			wrmsr(address, low, high);

			setup_APIC_extended_lvt(K8_APIC_EXT_LVT_ENTRY_THRESHOLD,
						THRESHOLD_APIC_VECTOR,
						K8_APIC_EXT_INT_MSG_FIX, 0);

			threshold_defaults.address = address;
			threshold_restart_bank(&threshold_defaults, 0, 0);
		}
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
	unsigned int bank, block;
	struct mce m;
	u32 low = 0, high = 0, address = 0;

	ack_APIC_irq();
	exit_idle();
	irq_enter();

	memset(&m, 0, sizeof(m));
	rdtscll(m.tsc);
	m.cpu = smp_processor_id();

	/* assume first bank caused it */
	for (bank = 0; bank < NR_BANKS; ++bank) {
		if (!(per_cpu(bank_map, m.cpu) & (1 << bank)))
			continue;
		for (block = 0; block < NR_BLOCKS; ++block) {
			if (block == 0)
				address = MSR_IA32_MC0_MISC + bank * 4;
			else if (block == 1) {
				address = (low & MASK_BLKPTR_LO) >> 21;
				if (!address)
					break;
				address += MCG_XBLK_ADDR;
			}
			else
				++address;

			if (rdmsr_safe(address, &low, &high))
				break;

			if (!(high & MASK_VALID_HI)) {
				if (block)
					continue;
				else
					break;
			}

			if (!(high & MASK_CNTP_HI)  ||
			     (high & MASK_LOCKED_HI))
				continue;

			/* Log the machine check that caused the threshold
			   event. */
			do_machine_check(NULL, 0);

			if (high & MASK_OVERFLOW_HI) {
				rdmsrl(address, m.misc);
				rdmsrl(MSR_IA32_MC0_STATUS + bank * 4,
				       m.status);
				m.bank = K8_MCE_THRESHOLD_BASE
				       + bank * NR_BLOCKS
				       + block;
				mce_log(&m);
				goto out;
			}
		}
	}
out:
	irq_exit();
}

/*
 * Sysfs Interface
 */

struct threshold_attr {
	struct attribute attr;
	ssize_t(*show) (struct threshold_block *, char *);
	ssize_t(*store) (struct threshold_block *, const char *, size_t count);
};

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

#define SHOW_FIELDS(name)                                           \
static ssize_t show_ ## name(struct threshold_block * b, char *buf) \
{                                                                   \
        return sprintf(buf, "%lx\n", (unsigned long) b->name);      \
}
SHOW_FIELDS(interrupt_enable)
SHOW_FIELDS(threshold_limit)

static ssize_t store_interrupt_enable(struct threshold_block *b,
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

static ssize_t store_threshold_limit(struct threshold_block *b,
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

static ssize_t show_error_count(struct threshold_block *b, char *buf)
{
	u32 high, low;
	cpumask_t oldmask;
	oldmask = affinity_set(b->cpu);
	rdmsr(b->address, low, high);
	affinity_restore(oldmask);
	return sprintf(buf, "%x\n",
		       (high & 0xFFF) - (THRESHOLD_MAX - b->threshold_limit));
}

static ssize_t store_error_count(struct threshold_block *b,
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

#define RW_ATTR(name)                                           \
static struct threshold_attr name =                             \
        THRESHOLD_ATTR(name, 0644, show_## name, store_## name)

RW_ATTR(interrupt_enable);
RW_ATTR(threshold_limit);
RW_ATTR(error_count);

static struct attribute *default_attrs[] = {
	&interrupt_enable.attr,
	&threshold_limit.attr,
	&error_count.attr,
	NULL
};

#define to_block(k) container_of(k, struct threshold_block, kobj)
#define to_attr(a) container_of(a, struct threshold_attr, attr)

static ssize_t show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct threshold_block *b = to_block(kobj);
	struct threshold_attr *a = to_attr(attr);
	ssize_t ret;
	ret = a->show ? a->show(b, buf) : -EIO;
	return ret;
}

static ssize_t store(struct kobject *kobj, struct attribute *attr,
		     const char *buf, size_t count)
{
	struct threshold_block *b = to_block(kobj);
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

static __cpuinit int allocate_threshold_blocks(unsigned int cpu,
					       unsigned int bank,
					       unsigned int block,
					       u32 address)
{
	int err;
	u32 low, high;
	struct threshold_block *b = NULL;

	if ((bank >= NR_BANKS) || (block >= NR_BLOCKS))
		return 0;

	if (rdmsr_safe(address, &low, &high))
		return 0;

	if (!(high & MASK_VALID_HI)) {
		if (block)
			goto recurse;
		else
			return 0;
	}

	if (!(high & MASK_CNTP_HI)  ||
	     (high & MASK_LOCKED_HI))
		goto recurse;

	b = kzalloc(sizeof(struct threshold_block), GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	b->block = block;
	b->bank = bank;
	b->cpu = cpu;
	b->address = address;
	b->interrupt_enable = 0;
	b->threshold_limit = THRESHOLD_MAX;

	INIT_LIST_HEAD(&b->miscj);

	if (per_cpu(threshold_banks, cpu)[bank]->blocks)
		list_add(&b->miscj,
			 &per_cpu(threshold_banks, cpu)[bank]->blocks->miscj);
	else
		per_cpu(threshold_banks, cpu)[bank]->blocks = b;

	kobject_set_name(&b->kobj, "misc%i", block);
	b->kobj.parent = &per_cpu(threshold_banks, cpu)[bank]->kobj;
	b->kobj.ktype = &threshold_ktype;
	err = kobject_register(&b->kobj);
	if (err)
		goto out_free;
recurse:
	if (!block) {
		address = (low & MASK_BLKPTR_LO) >> 21;
		if (!address)
			return 0;
		address += MCG_XBLK_ADDR;
	} else
		++address;

	err = allocate_threshold_blocks(cpu, bank, ++block, address);
	if (err)
		goto out_free;

	return err;

out_free:
	if (b) {
		kobject_unregister(&b->kobj);
		kfree(b);
	}
	return err;
}

/* symlinks sibling shared banks to first core.  first core owns dir/files. */
static __cpuinit int threshold_create_bank(unsigned int cpu, unsigned int bank)
{
	int i, err = 0;
	struct threshold_bank *b = NULL;
	cpumask_t oldmask = CPU_MASK_NONE;
	char name[32];

	sprintf(name, "threshold_bank%i", bank);

#ifdef CONFIG_SMP
	if (cpu_data[cpu].cpu_core_id && shared_bank[bank]) {	/* symlink */
		i = first_cpu(cpu_core_map[cpu]);

		/* first core not up yet */
		if (cpu_data[i].cpu_core_id)
			goto out;

		/* already linked */
		if (per_cpu(threshold_banks, cpu)[bank])
			goto out;

		b = per_cpu(threshold_banks, i)[bank];

		if (!b)
			goto out;

		err = sysfs_create_link(&per_cpu(device_mce, cpu).kobj,
					&b->kobj, name);
		if (err)
			goto out;

		b->cpus = cpu_core_map[cpu];
		per_cpu(threshold_banks, cpu)[bank] = b;
		goto out;
	}
#endif

	b = kzalloc(sizeof(struct threshold_bank), GFP_KERNEL);
	if (!b) {
		err = -ENOMEM;
		goto out;
	}

	kobject_set_name(&b->kobj, "threshold_bank%i", bank);
	b->kobj.parent = &per_cpu(device_mce, cpu).kobj;
#ifndef CONFIG_SMP
	b->cpus = CPU_MASK_ALL;
#else
	b->cpus = cpu_core_map[cpu];
#endif
	err = kobject_register(&b->kobj);
	if (err)
		goto out_free;

	per_cpu(threshold_banks, cpu)[bank] = b;

	oldmask = affinity_set(cpu);
	err = allocate_threshold_blocks(cpu, bank, 0,
					MSR_IA32_MC0_MISC + bank * 4);
	affinity_restore(oldmask);

	if (err)
		goto out_free;

	for_each_cpu_mask(i, b->cpus) {
		if (i == cpu)
			continue;

		err = sysfs_create_link(&per_cpu(device_mce, i).kobj,
					&b->kobj, name);
		if (err)
			goto out;

		per_cpu(threshold_banks, i)[bank] = b;
	}

	goto out;

out_free:
	per_cpu(threshold_banks, cpu)[bank] = NULL;
	kfree(b);
out:
	return err;
}

/* create dir/files for all valid threshold banks */
static __cpuinit int threshold_create_device(unsigned int cpu)
{
	unsigned int bank;
	int err = 0;

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

/*
 * let's be hotplug friendly.
 * in case of multiple core processors, the first core always takes ownership
 *   of shared sysfs dir/files, and rest of the cores will be symlinked to it.
 */

static void deallocate_threshold_block(unsigned int cpu,
						 unsigned int bank)
{
	struct threshold_block *pos = NULL;
	struct threshold_block *tmp = NULL;
	struct threshold_bank *head = per_cpu(threshold_banks, cpu)[bank];

	if (!head)
		return;

	list_for_each_entry_safe(pos, tmp, &head->blocks->miscj, miscj) {
		kobject_unregister(&pos->kobj);
		list_del(&pos->miscj);
		kfree(pos);
	}

	kfree(per_cpu(threshold_banks, cpu)[bank]->blocks);
	per_cpu(threshold_banks, cpu)[bank]->blocks = NULL;
}

static void threshold_remove_bank(unsigned int cpu, int bank)
{
	int i = 0;
	struct threshold_bank *b;
	char name[32];

	b = per_cpu(threshold_banks, cpu)[bank];

	if (!b)
		return;

	if (!b->blocks)
		goto free_out;

	sprintf(name, "threshold_bank%i", bank);

#ifdef CONFIG_SMP
	/* sibling symlink */
	if (shared_bank[bank] && b->blocks->cpu != cpu) {
		sysfs_remove_link(&per_cpu(device_mce, cpu).kobj, name);
		per_cpu(threshold_banks, cpu)[bank] = NULL;
		return;
	}
#endif

	/* remove all sibling symlinks before unregistering */
	for_each_cpu_mask(i, b->cpus) {
		if (i == cpu)
			continue;

		sysfs_remove_link(&per_cpu(device_mce, i).kobj, name);
		per_cpu(threshold_banks, i)[bank] = NULL;
	}

	deallocate_threshold_block(cpu, bank);

free_out:
	kobject_unregister(&b->kobj);
	kfree(b);
	per_cpu(threshold_banks, cpu)[bank] = NULL;
}

static void threshold_remove_device(unsigned int cpu)
{
	unsigned int bank;

	for (bank = 0; bank < NR_BANKS; ++bank) {
		if (!(per_cpu(bank_map, cpu) & 1 << bank))
			continue;
		threshold_remove_bank(cpu, bank);
	}
}

/* get notified when a cpu comes on/off */
static int threshold_cpu_callback(struct notifier_block *nfb,
					    unsigned long action, void *hcpu)
{
	/* cpu was unsigned int to begin with */
	unsigned int cpu = (unsigned long)hcpu;

	if (cpu >= NR_CPUS)
		goto out;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		threshold_create_device(cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
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
	unsigned lcpu = 0;

	/* to hit CPUs online before the notifier is up */
	for_each_online_cpu(lcpu) {
		int err = threshold_create_device(lcpu);
		if (err)
			return err;
	}
	register_hotcpu_notifier(&threshold_cpu_notifier);
	return 0;
}

device_initcall(threshold_init_device);
