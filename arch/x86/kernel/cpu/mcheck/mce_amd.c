/*
 *  (c) 2005-2015 Advanced Micro Devices, Inc.
 *  Your use of this code is subject to the terms and conditions of the
 *  GNU general public license version 2. See "COPYING" or
 *  http://www.gnu.org/licenses/gpl.html
 *
 *  Written by Jacob Shin - AMD, Inc.
 *  Maintained by: Borislav Petkov <bp@alien8.de>
 *
 *  All MC4_MISCi registers are shared between cores on a node.
 */
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/kobject.h>
#include <linux/percpu.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/smp.h>

#include <asm/amd_nb.h>
#include <asm/apic.h>
#include <asm/idle.h>
#include <asm/mce.h>
#include <asm/msr.h>
#include <asm/trace/irq_vectors.h>

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

/* Deferred error settings */
#define MSR_CU_DEF_ERR		0xC0000410
#define MASK_DEF_LVTOFF		0x000000F0
#define MASK_DEF_INT_TYPE	0x00000006
#define DEF_LVT_OFF		0x2
#define DEF_INT_TYPE_APIC	0x2

static const char * const th_names[] = {
	"load_store",
	"insn_fetch",
	"combined_unit",
	"decode_unit",
	"northbridge",
	"execution_unit",
};

static DEFINE_PER_CPU(struct threshold_bank **, threshold_banks);
static DEFINE_PER_CPU(unsigned char, bank_map);	/* see which banks are on */

static void amd_threshold_interrupt(void);
static void amd_deferred_error_interrupt(void);

static void default_deferred_error_interrupt(void)
{
	pr_err("Unexpected deferred interrupt at vector %x\n", DEFERRED_ERROR_VECTOR);
}
void (*deferred_error_int_vector)(void) = default_deferred_error_interrupt;

/*
 * CPU Initialization
 */

struct thresh_restart {
	struct threshold_block	*b;
	int			reset;
	int			set_lvt_off;
	int			lvt_off;
	u16			old_limit;
};

static inline bool is_shared_bank(int bank)
{
	/* Bank 4 is for northbridge reporting and is thus shared */
	return (bank == 4);
}

static const char *bank4_names(const struct threshold_block *b)
{
	switch (b->address) {
	/* MSR4_MISC0 */
	case 0x00000413:
		return "dram";

	case 0xc0000408:
		return "ht_links";

	case 0xc0000409:
		return "l3_cache";

	default:
		WARN(1, "Funny MSR: 0x%08x\n", b->address);
		return "";
	}
};


static bool lvt_interrupt_supported(unsigned int bank, u32 msr_high_bits)
{
	/*
	 * bank 4 supports APIC LVT interrupts implicitly since forever.
	 */
	if (bank == 4)
		return true;

	/*
	 * IntP: interrupt present; if this bit is set, the thresholding
	 * bank can generate APIC LVT interrupts
	 */
	return msr_high_bits & BIT(28);
}

static int lvt_off_valid(struct threshold_block *b, int apic, u32 lo, u32 hi)
{
	int msr = (hi & MASK_LVTOFF_HI) >> 20;

	if (apic < 0) {
		pr_err(FW_BUG "cpu %d, failed to setup threshold interrupt "
		       "for bank %d, block %d (MSR%08X=0x%x%08x)\n", b->cpu,
		       b->bank, b->block, b->address, hi, lo);
		return 0;
	}

	if (apic != msr) {
		pr_err(FW_BUG "cpu %d, invalid threshold interrupt offset %d "
		       "for bank %d, block %d (MSR%08X=0x%x%08x)\n",
		       b->cpu, apic, b->bank, b->block, b->address, hi, lo);
		return 0;
	}

	return 1;
};

/*
 * Called via smp_call_function_single(), must be called with correct
 * cpu affinity.
 */
static void threshold_restart_bank(void *_tr)
{
	struct thresh_restart *tr = _tr;
	u32 hi, lo;

	rdmsr(tr->b->address, lo, hi);

	if (tr->b->threshold_limit < (hi & THRESHOLD_MAX))
		tr->reset = 1;	/* limit cannot be lower than err count */

	if (tr->reset) {		/* reset err count and overflow bit */
		hi =
		    (hi & ~(MASK_ERR_COUNT_HI | MASK_OVERFLOW_HI)) |
		    (THRESHOLD_MAX - tr->b->threshold_limit);
	} else if (tr->old_limit) {	/* change limit w/o reset */
		int new_count = (hi & THRESHOLD_MAX) +
		    (tr->old_limit - tr->b->threshold_limit);

		hi = (hi & ~MASK_ERR_COUNT_HI) |
		    (new_count & THRESHOLD_MAX);
	}

	/* clear IntType */
	hi &= ~MASK_INT_TYPE_HI;

	if (!tr->b->interrupt_capable)
		goto done;

	if (tr->set_lvt_off) {
		if (lvt_off_valid(tr->b, tr->lvt_off, lo, hi)) {
			/* set new lvt offset */
			hi &= ~MASK_LVTOFF_HI;
			hi |= tr->lvt_off << 20;
		}
	}

	if (tr->b->interrupt_enable)
		hi |= INT_TYPE_APIC;

 done:

	hi |= MASK_COUNT_EN_HI;
	wrmsr(tr->b->address, lo, hi);
}

static void mce_threshold_block_init(struct threshold_block *b, int offset)
{
	struct thresh_restart tr = {
		.b			= b,
		.set_lvt_off		= 1,
		.lvt_off		= offset,
	};

	b->threshold_limit		= THRESHOLD_MAX;
	threshold_restart_bank(&tr);
};

static int setup_APIC_mce_threshold(int reserved, int new)
{
	if (reserved < 0 && !setup_APIC_eilvt(new, THRESHOLD_APIC_VECTOR,
					      APIC_EILVT_MSG_FIX, 0))
		return new;

	return reserved;
}

static int setup_APIC_deferred_error(int reserved, int new)
{
	if (reserved < 0 && !setup_APIC_eilvt(new, DEFERRED_ERROR_VECTOR,
					      APIC_EILVT_MSG_FIX, 0))
		return new;

	return reserved;
}

static void deferred_error_interrupt_enable(struct cpuinfo_x86 *c)
{
	u32 low = 0, high = 0;
	int def_offset = -1, def_new;

	if (rdmsr_safe(MSR_CU_DEF_ERR, &low, &high))
		return;

	def_new = (low & MASK_DEF_LVTOFF) >> 4;
	if (!(low & MASK_DEF_LVTOFF)) {
		pr_err(FW_BUG "Your BIOS is not setting up LVT offset 0x2 for deferred error IRQs correctly.\n");
		def_new = DEF_LVT_OFF;
		low = (low & ~MASK_DEF_LVTOFF) | (DEF_LVT_OFF << 4);
	}

	def_offset = setup_APIC_deferred_error(def_offset, def_new);
	if ((def_offset == def_new) &&
	    (deferred_error_int_vector != amd_deferred_error_interrupt))
		deferred_error_int_vector = amd_deferred_error_interrupt;

	low = (low & ~MASK_DEF_INT_TYPE) | DEF_INT_TYPE_APIC;
	wrmsr(MSR_CU_DEF_ERR, low, high);
}

/* cpu init entry point, called from mce.c with preempt off */
void mce_amd_feature_init(struct cpuinfo_x86 *c)
{
	struct threshold_block b;
	unsigned int cpu = smp_processor_id();
	u32 low = 0, high = 0, address = 0;
	unsigned int bank, block;
	int offset = -1, new;

	for (bank = 0; bank < mca_cfg.banks; ++bank) {
		for (block = 0; block < NR_BLOCKS; ++block) {
			if (block == 0)
				address = MSR_IA32_MCx_MISC(bank);
			else if (block == 1) {
				address = (low & MASK_BLKPTR_LO) >> 21;
				if (!address)
					break;

				address += MCG_XBLK_ADDR;
			} else
				++address;

			if (rdmsr_safe(address, &low, &high))
				break;

			if (!(high & MASK_VALID_HI))
				continue;

			if (!(high & MASK_CNTP_HI)  ||
			     (high & MASK_LOCKED_HI))
				continue;

			if (!block)
				per_cpu(bank_map, cpu) |= (1 << bank);

			memset(&b, 0, sizeof(b));
			b.cpu			= cpu;
			b.bank			= bank;
			b.block			= block;
			b.address		= address;
			b.interrupt_capable	= lvt_interrupt_supported(bank, high);

			if (!b.interrupt_capable)
				goto init;

			b.interrupt_enable = 1;
			new	= (high & MASK_LVTOFF_HI) >> 20;
			offset  = setup_APIC_mce_threshold(offset, new);

			if ((offset == new) &&
			    (mce_threshold_vector != amd_threshold_interrupt))
				mce_threshold_vector = amd_threshold_interrupt;

init:
			mce_threshold_block_init(&b, offset);
		}
	}

	if (mce_flags.succor)
		deferred_error_interrupt_enable(c);
}

static void __log_error(unsigned int bank, bool threshold_err, u64 misc)
{
	struct mce m;
	u64 status;

	rdmsrl(MSR_IA32_MCx_STATUS(bank), status);
	if (!(status & MCI_STATUS_VAL))
		return;

	mce_setup(&m);

	m.status = status;
	m.bank = bank;

	if (threshold_err)
		m.misc = misc;

	if (m.status & MCI_STATUS_ADDRV)
		rdmsrl(MSR_IA32_MCx_ADDR(bank), m.addr);

	mce_log(&m);
	wrmsrl(MSR_IA32_MCx_STATUS(bank), 0);
}

static inline void __smp_deferred_error_interrupt(void)
{
	inc_irq_stat(irq_deferred_error_count);
	deferred_error_int_vector();
}

asmlinkage __visible void smp_deferred_error_interrupt(void)
{
	entering_irq();
	__smp_deferred_error_interrupt();
	exiting_ack_irq();
}

asmlinkage __visible void smp_trace_deferred_error_interrupt(void)
{
	entering_irq();
	trace_deferred_error_apic_entry(DEFERRED_ERROR_VECTOR);
	__smp_deferred_error_interrupt();
	trace_deferred_error_apic_exit(DEFERRED_ERROR_VECTOR);
	exiting_ack_irq();
}

/* APIC interrupt handler for deferred errors */
static void amd_deferred_error_interrupt(void)
{
	u64 status;
	unsigned int bank;

	for (bank = 0; bank < mca_cfg.banks; ++bank) {
		rdmsrl(MSR_IA32_MCx_STATUS(bank), status);

		if (!(status & MCI_STATUS_VAL) ||
		    !(status & MCI_STATUS_DEFERRED))
			continue;

		__log_error(bank, false, 0);
		break;
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

static void amd_threshold_interrupt(void)
{
	u32 low = 0, high = 0, address = 0;
	int cpu = smp_processor_id();
	unsigned int bank, block;

	/* assume first bank caused it */
	for (bank = 0; bank < mca_cfg.banks; ++bank) {
		if (!(per_cpu(bank_map, cpu) & (1 << bank)))
			continue;
		for (block = 0; block < NR_BLOCKS; ++block) {
			if (block == 0) {
				address = MSR_IA32_MCx_MISC(bank);
			} else if (block == 1) {
				address = (low & MASK_BLKPTR_LO) >> 21;
				if (!address)
					break;
				address += MCG_XBLK_ADDR;
			} else {
				++address;
			}

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

			/*
			 * Log the machine check that caused the threshold
			 * event.
			 */
			if (high & MASK_OVERFLOW_HI)
				goto log;
		}
	}
	return;

log:
	__log_error(bank, true, ((u64)high << 32) | low);
}

/*
 * Sysfs Interface
 */

struct threshold_attr {
	struct attribute attr;
	ssize_t (*show) (struct threshold_block *, char *);
	ssize_t (*store) (struct threshold_block *, const char *, size_t count);
};

#define SHOW_FIELDS(name)						\
static ssize_t show_ ## name(struct threshold_block *b, char *buf)	\
{									\
	return sprintf(buf, "%lu\n", (unsigned long) b->name);		\
}
SHOW_FIELDS(interrupt_enable)
SHOW_FIELDS(threshold_limit)

static ssize_t
store_interrupt_enable(struct threshold_block *b, const char *buf, size_t size)
{
	struct thresh_restart tr;
	unsigned long new;

	if (!b->interrupt_capable)
		return -EINVAL;

	if (kstrtoul(buf, 0, &new) < 0)
		return -EINVAL;

	b->interrupt_enable = !!new;

	memset(&tr, 0, sizeof(tr));
	tr.b		= b;

	smp_call_function_single(b->cpu, threshold_restart_bank, &tr, 1);

	return size;
}

static ssize_t
store_threshold_limit(struct threshold_block *b, const char *buf, size_t size)
{
	struct thresh_restart tr;
	unsigned long new;

	if (kstrtoul(buf, 0, &new) < 0)
		return -EINVAL;

	if (new > THRESHOLD_MAX)
		new = THRESHOLD_MAX;
	if (new < 1)
		new = 1;

	memset(&tr, 0, sizeof(tr));
	tr.old_limit = b->threshold_limit;
	b->threshold_limit = new;
	tr.b = b;

	smp_call_function_single(b->cpu, threshold_restart_bank, &tr, 1);

	return size;
}

static ssize_t show_error_count(struct threshold_block *b, char *buf)
{
	u32 lo, hi;

	rdmsr_on_cpu(b->cpu, b->address, &lo, &hi);

	return sprintf(buf, "%u\n", ((hi & THRESHOLD_MAX) -
				     (THRESHOLD_MAX - b->threshold_limit)));
}

static struct threshold_attr error_count = {
	.attr = {.name = __stringify(error_count), .mode = 0444 },
	.show = show_error_count,
};

#define RW_ATTR(val)							\
static struct threshold_attr val = {					\
	.attr	= {.name = __stringify(val), .mode = 0644 },		\
	.show	= show_## val,						\
	.store	= store_## val,						\
};

RW_ATTR(interrupt_enable);
RW_ATTR(threshold_limit);

static struct attribute *default_attrs[] = {
	&threshold_limit.attr,
	&error_count.attr,
	NULL,	/* possibly interrupt_enable if supported, see below */
	NULL,
};

#define to_block(k)	container_of(k, struct threshold_block, kobj)
#define to_attr(a)	container_of(a, struct threshold_attr, attr)

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

static const struct sysfs_ops threshold_ops = {
	.show			= show,
	.store			= store,
};

static struct kobj_type threshold_ktype = {
	.sysfs_ops		= &threshold_ops,
	.default_attrs		= default_attrs,
};

static int allocate_threshold_blocks(unsigned int cpu, unsigned int bank,
				     unsigned int block, u32 address)
{
	struct threshold_block *b = NULL;
	u32 low, high;
	int err;

	if ((bank >= mca_cfg.banks) || (block >= NR_BLOCKS))
		return 0;

	if (rdmsr_safe_on_cpu(cpu, address, &low, &high))
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

	b->block		= block;
	b->bank			= bank;
	b->cpu			= cpu;
	b->address		= address;
	b->interrupt_enable	= 0;
	b->interrupt_capable	= lvt_interrupt_supported(bank, high);
	b->threshold_limit	= THRESHOLD_MAX;

	if (b->interrupt_capable) {
		threshold_ktype.default_attrs[2] = &interrupt_enable.attr;
		b->interrupt_enable = 1;
	} else {
		threshold_ktype.default_attrs[2] = NULL;
	}

	INIT_LIST_HEAD(&b->miscj);

	if (per_cpu(threshold_banks, cpu)[bank]->blocks) {
		list_add(&b->miscj,
			 &per_cpu(threshold_banks, cpu)[bank]->blocks->miscj);
	} else {
		per_cpu(threshold_banks, cpu)[bank]->blocks = b;
	}

	err = kobject_init_and_add(&b->kobj, &threshold_ktype,
				   per_cpu(threshold_banks, cpu)[bank]->kobj,
				   (bank == 4 ? bank4_names(b) : th_names[bank]));
	if (err)
		goto out_free;
recurse:
	if (!block) {
		address = (low & MASK_BLKPTR_LO) >> 21;
		if (!address)
			return 0;
		address += MCG_XBLK_ADDR;
	} else {
		++address;
	}

	err = allocate_threshold_blocks(cpu, bank, ++block, address);
	if (err)
		goto out_free;

	if (b)
		kobject_uevent(&b->kobj, KOBJ_ADD);

	return err;

out_free:
	if (b) {
		kobject_put(&b->kobj);
		list_del(&b->miscj);
		kfree(b);
	}
	return err;
}

static int __threshold_add_blocks(struct threshold_bank *b)
{
	struct list_head *head = &b->blocks->miscj;
	struct threshold_block *pos = NULL;
	struct threshold_block *tmp = NULL;
	int err = 0;

	err = kobject_add(&b->blocks->kobj, b->kobj, b->blocks->kobj.name);
	if (err)
		return err;

	list_for_each_entry_safe(pos, tmp, head, miscj) {

		err = kobject_add(&pos->kobj, b->kobj, pos->kobj.name);
		if (err) {
			list_for_each_entry_safe_reverse(pos, tmp, head, miscj)
				kobject_del(&pos->kobj);

			return err;
		}
	}
	return err;
}

static int threshold_create_bank(unsigned int cpu, unsigned int bank)
{
	struct device *dev = per_cpu(mce_device, cpu);
	struct amd_northbridge *nb = NULL;
	struct threshold_bank *b = NULL;
	const char *name = th_names[bank];
	int err = 0;

	if (is_shared_bank(bank)) {
		nb = node_to_amd_nb(amd_get_nb_id(cpu));

		/* threshold descriptor already initialized on this node? */
		if (nb && nb->bank4) {
			/* yes, use it */
			b = nb->bank4;
			err = kobject_add(b->kobj, &dev->kobj, name);
			if (err)
				goto out;

			per_cpu(threshold_banks, cpu)[bank] = b;
			atomic_inc(&b->cpus);

			err = __threshold_add_blocks(b);

			goto out;
		}
	}

	b = kzalloc(sizeof(struct threshold_bank), GFP_KERNEL);
	if (!b) {
		err = -ENOMEM;
		goto out;
	}

	b->kobj = kobject_create_and_add(name, &dev->kobj);
	if (!b->kobj) {
		err = -EINVAL;
		goto out_free;
	}

	per_cpu(threshold_banks, cpu)[bank] = b;

	if (is_shared_bank(bank)) {
		atomic_set(&b->cpus, 1);

		/* nb is already initialized, see above */
		if (nb) {
			WARN_ON(nb->bank4);
			nb->bank4 = b;
		}
	}

	err = allocate_threshold_blocks(cpu, bank, 0, MSR_IA32_MCx_MISC(bank));
	if (!err)
		goto out;

 out_free:
	kfree(b);

 out:
	return err;
}

/* create dir/files for all valid threshold banks */
static int threshold_create_device(unsigned int cpu)
{
	unsigned int bank;
	struct threshold_bank **bp;
	int err = 0;

	bp = kzalloc(sizeof(struct threshold_bank *) * mca_cfg.banks,
		     GFP_KERNEL);
	if (!bp)
		return -ENOMEM;

	per_cpu(threshold_banks, cpu) = bp;

	for (bank = 0; bank < mca_cfg.banks; ++bank) {
		if (!(per_cpu(bank_map, cpu) & (1 << bank)))
			continue;
		err = threshold_create_bank(cpu, bank);
		if (err)
			return err;
	}

	return err;
}

static void deallocate_threshold_block(unsigned int cpu,
						 unsigned int bank)
{
	struct threshold_block *pos = NULL;
	struct threshold_block *tmp = NULL;
	struct threshold_bank *head = per_cpu(threshold_banks, cpu)[bank];

	if (!head)
		return;

	list_for_each_entry_safe(pos, tmp, &head->blocks->miscj, miscj) {
		kobject_put(&pos->kobj);
		list_del(&pos->miscj);
		kfree(pos);
	}

	kfree(per_cpu(threshold_banks, cpu)[bank]->blocks);
	per_cpu(threshold_banks, cpu)[bank]->blocks = NULL;
}

static void __threshold_remove_blocks(struct threshold_bank *b)
{
	struct threshold_block *pos = NULL;
	struct threshold_block *tmp = NULL;

	kobject_del(b->kobj);

	list_for_each_entry_safe(pos, tmp, &b->blocks->miscj, miscj)
		kobject_del(&pos->kobj);
}

static void threshold_remove_bank(unsigned int cpu, int bank)
{
	struct amd_northbridge *nb;
	struct threshold_bank *b;

	b = per_cpu(threshold_banks, cpu)[bank];
	if (!b)
		return;

	if (!b->blocks)
		goto free_out;

	if (is_shared_bank(bank)) {
		if (!atomic_dec_and_test(&b->cpus)) {
			__threshold_remove_blocks(b);
			per_cpu(threshold_banks, cpu)[bank] = NULL;
			return;
		} else {
			/*
			 * the last CPU on this node using the shared bank is
			 * going away, remove that bank now.
			 */
			nb = node_to_amd_nb(amd_get_nb_id(cpu));
			nb->bank4 = NULL;
		}
	}

	deallocate_threshold_block(cpu, bank);

free_out:
	kobject_del(b->kobj);
	kobject_put(b->kobj);
	kfree(b);
	per_cpu(threshold_banks, cpu)[bank] = NULL;
}

static void threshold_remove_device(unsigned int cpu)
{
	unsigned int bank;

	for (bank = 0; bank < mca_cfg.banks; ++bank) {
		if (!(per_cpu(bank_map, cpu) & (1 << bank)))
			continue;
		threshold_remove_bank(cpu, bank);
	}
	kfree(per_cpu(threshold_banks, cpu));
}

/* get notified when a cpu comes on/off */
static void
amd_64_threshold_cpu_callback(unsigned long action, unsigned int cpu)
{
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
}

static __init int threshold_init_device(void)
{
	unsigned lcpu = 0;

	/* to hit CPUs online before the notifier is up */
	for_each_online_cpu(lcpu) {
		int err = threshold_create_device(lcpu);

		if (err)
			return err;
	}
	threshold_cpu_callback = amd_64_threshold_cpu_callback;

	return 0;
}
/*
 * there are 3 funcs which need to be _initcalled in a logic sequence:
 * 1. xen_late_init_mcelog
 * 2. mcheck_init_device
 * 3. threshold_init_device
 *
 * xen_late_init_mcelog must register xen_mce_chrdev_device before
 * native mce_chrdev_device registration if running under xen platform;
 *
 * mcheck_init_device should be inited before threshold_init_device to
 * initialize mce_device, otherwise a NULL ptr dereference will cause panic.
 *
 * so we use following _initcalls
 * 1. device_initcall(xen_late_init_mcelog);
 * 2. device_initcall_sync(mcheck_init_device);
 * 3. late_initcall(threshold_init_device);
 *
 * when running under xen, the initcall order is 1,2,3;
 * on baremetal, we skip 1 and we do only 2 and 3.
 */
late_initcall(threshold_init_device);
