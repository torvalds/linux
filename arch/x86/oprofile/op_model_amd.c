/*
 * @file op_model_amd.c
 * athlon / K7 / K8 / Family 10h model-specific MSR operations
 *
 * @remark Copyright 2002-2009 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 * @author Graydon Hoare
 * @author Robert Richter <robert.richter@amd.com>
 * @author Barry Kasindorf <barry.kasindorf@amd.com>
 * @author Jason Yeh <jason.yeh@amd.com>
 * @author Suravee Suthikulpanit <suravee.suthikulpanit@amd.com>
 */

#include <linux/oprofile.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/percpu.h>

#include <asm/ptrace.h>
#include <asm/msr.h>
#include <asm/nmi.h>
#include <asm/apic.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>

#include "op_x86_model.h"
#include "op_counter.h"

#define NUM_COUNTERS 4
#define NUM_CONTROLS 4
#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX
#define NUM_VIRT_COUNTERS 32
#define NUM_VIRT_CONTROLS 32
#else
#define NUM_VIRT_COUNTERS NUM_COUNTERS
#define NUM_VIRT_CONTROLS NUM_CONTROLS
#endif

#define OP_EVENT_MASK			0x0FFF
#define OP_CTR_OVERFLOW			(1ULL<<31)

#define MSR_AMD_EVENTSEL_RESERVED	((0xFFFFFCF0ULL<<32)|(1ULL<<21))

static unsigned long reset_value[NUM_VIRT_COUNTERS];

#define IBS_FETCH_SIZE			6
#define IBS_OP_SIZE			12

static u32 ibs_caps;

struct op_ibs_config {
	unsigned long op_enabled;
	unsigned long fetch_enabled;
	unsigned long max_cnt_fetch;
	unsigned long max_cnt_op;
	unsigned long rand_en;
	unsigned long dispatched_ops;
};

static struct op_ibs_config ibs_config;
static u64 ibs_op_ctl;

/*
 * IBS cpuid feature detection
 */

#define IBS_CPUID_FEATURES      0x8000001b

/*
 * Same bit mask as for IBS cpuid feature flags (Fn8000_001B_EAX), but
 * bit 0 is used to indicate the existence of IBS.
 */
#define IBS_CAPS_AVAIL			(1LL<<0)
#define IBS_CAPS_RDWROPCNT		(1LL<<3)
#define IBS_CAPS_OPCNT			(1LL<<4)

/*
 * IBS randomization macros
 */
#define IBS_RANDOM_BITS			12
#define IBS_RANDOM_MASK			((1ULL << IBS_RANDOM_BITS) - 1)
#define IBS_RANDOM_MAXCNT_OFFSET	(1ULL << (IBS_RANDOM_BITS - 5))

static u32 get_ibs_caps(void)
{
	u32 ibs_caps;
	unsigned int max_level;

	if (!boot_cpu_has(X86_FEATURE_IBS))
		return 0;

	/* check IBS cpuid feature flags */
	max_level = cpuid_eax(0x80000000);
	if (max_level < IBS_CPUID_FEATURES)
		return IBS_CAPS_AVAIL;

	ibs_caps = cpuid_eax(IBS_CPUID_FEATURES);
	if (!(ibs_caps & IBS_CAPS_AVAIL))
		/* cpuid flags not valid */
		return IBS_CAPS_AVAIL;

	return ibs_caps;
}

#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX

static void op_mux_switch_ctrl(struct op_x86_model_spec const *model,
			       struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	/* enable active counters */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		int virt = op_x86_phys_to_virt(i);
		if (!reset_value[virt])
			continue;
		rdmsrl(msrs->controls[i].addr, val);
		val &= model->reserved;
		val |= op_x86_get_ctrl(model, &counter_config[virt]);
		wrmsrl(msrs->controls[i].addr, val);
	}
}

#endif

/* functions for op_amd_spec */

static void op_amd_fill_in_addresses(struct op_msrs * const msrs)
{
	int i;

	for (i = 0; i < NUM_COUNTERS; i++) {
		if (reserve_perfctr_nmi(MSR_K7_PERFCTR0 + i))
			msrs->counters[i].addr = MSR_K7_PERFCTR0 + i;
	}

	for (i = 0; i < NUM_CONTROLS; i++) {
		if (reserve_evntsel_nmi(MSR_K7_EVNTSEL0 + i))
			msrs->controls[i].addr = MSR_K7_EVNTSEL0 + i;
	}
}

static void op_amd_setup_ctrs(struct op_x86_model_spec const *model,
			      struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	/* setup reset_value */
	for (i = 0; i < NUM_VIRT_COUNTERS; ++i) {
		if (counter_config[i].enabled
		    && msrs->counters[op_x86_virt_to_phys(i)].addr)
			reset_value[i] = counter_config[i].count;
		else
			reset_value[i] = 0;
	}

	/* clear all counters */
	for (i = 0; i < NUM_CONTROLS; ++i) {
		if (unlikely(!msrs->controls[i].addr)) {
			if (counter_config[i].enabled && !smp_processor_id())
				/*
				 * counter is reserved, this is on all
				 * cpus, so report only for cpu #0
				 */
				op_x86_warn_reserved(i);
			continue;
		}
		rdmsrl(msrs->controls[i].addr, val);
		if (val & ARCH_PERFMON_EVENTSEL_ENABLE)
			op_x86_warn_in_use(i);
		val &= model->reserved;
		wrmsrl(msrs->controls[i].addr, val);
	}

	/* avoid a false detection of ctr overflows in NMI handler */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		if (unlikely(!msrs->counters[i].addr))
			continue;
		wrmsrl(msrs->counters[i].addr, -1LL);
	}

	/* enable active counters */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		int virt = op_x86_phys_to_virt(i);
		if (!reset_value[virt])
			continue;

		/* setup counter registers */
		wrmsrl(msrs->counters[i].addr, -(u64)reset_value[virt]);

		/* setup control registers */
		rdmsrl(msrs->controls[i].addr, val);
		val &= model->reserved;
		val |= op_x86_get_ctrl(model, &counter_config[virt]);
		wrmsrl(msrs->controls[i].addr, val);
	}
}

/*
 * 16-bit Linear Feedback Shift Register (LFSR)
 *
 *                       16   14   13    11
 * Feedback polynomial = X  + X  + X  +  X  + 1
 */
static unsigned int lfsr_random(void)
{
	static unsigned int lfsr_value = 0xF00D;
	unsigned int bit;

	/* Compute next bit to shift in */
	bit = ((lfsr_value >> 0) ^
	       (lfsr_value >> 2) ^
	       (lfsr_value >> 3) ^
	       (lfsr_value >> 5)) & 0x0001;

	/* Advance to next register value */
	lfsr_value = (lfsr_value >> 1) | (bit << 15);

	return lfsr_value;
}

/*
 * IBS software randomization
 *
 * The IBS periodic op counter is randomized in software. The lower 12
 * bits of the 20 bit counter are randomized. IbsOpCurCnt is
 * initialized with a 12 bit random value.
 */
static inline u64 op_amd_randomize_ibs_op(u64 val)
{
	unsigned int random = lfsr_random();

	if (!(ibs_caps & IBS_CAPS_RDWROPCNT))
		/*
		 * Work around if the hw can not write to IbsOpCurCnt
		 *
		 * Randomize the lower 8 bits of the 16 bit
		 * IbsOpMaxCnt [15:0] value in the range of -128 to
		 * +127 by adding/subtracting an offset to the
		 * maximum count (IbsOpMaxCnt).
		 *
		 * To avoid over or underflows and protect upper bits
		 * starting at bit 16, the initial value for
		 * IbsOpMaxCnt must fit in the range from 0x0081 to
		 * 0xff80.
		 */
		val += (s8)(random >> 4);
	else
		val |= (u64)(random & IBS_RANDOM_MASK) << 32;

	return val;
}

static inline void
op_amd_handle_ibs(struct pt_regs * const regs,
		  struct op_msrs const * const msrs)
{
	u64 val, ctl;
	struct op_entry entry;

	if (!ibs_caps)
		return;

	if (ibs_config.fetch_enabled) {
		rdmsrl(MSR_AMD64_IBSFETCHCTL, ctl);
		if (ctl & IBS_FETCH_VAL) {
			rdmsrl(MSR_AMD64_IBSFETCHLINAD, val);
			oprofile_write_reserve(&entry, regs, val,
					       IBS_FETCH_CODE, IBS_FETCH_SIZE);
			oprofile_add_data64(&entry, val);
			oprofile_add_data64(&entry, ctl);
			rdmsrl(MSR_AMD64_IBSFETCHPHYSAD, val);
			oprofile_add_data64(&entry, val);
			oprofile_write_commit(&entry);

			/* reenable the IRQ */
			ctl &= ~(IBS_FETCH_VAL | IBS_FETCH_CNT);
			ctl |= IBS_FETCH_ENABLE;
			wrmsrl(MSR_AMD64_IBSFETCHCTL, ctl);
		}
	}

	if (ibs_config.op_enabled) {
		rdmsrl(MSR_AMD64_IBSOPCTL, ctl);
		if (ctl & IBS_OP_VAL) {
			rdmsrl(MSR_AMD64_IBSOPRIP, val);
			oprofile_write_reserve(&entry, regs, val,
					       IBS_OP_CODE, IBS_OP_SIZE);
			oprofile_add_data64(&entry, val);
			rdmsrl(MSR_AMD64_IBSOPDATA, val);
			oprofile_add_data64(&entry, val);
			rdmsrl(MSR_AMD64_IBSOPDATA2, val);
			oprofile_add_data64(&entry, val);
			rdmsrl(MSR_AMD64_IBSOPDATA3, val);
			oprofile_add_data64(&entry, val);
			rdmsrl(MSR_AMD64_IBSDCLINAD, val);
			oprofile_add_data64(&entry, val);
			rdmsrl(MSR_AMD64_IBSDCPHYSAD, val);
			oprofile_add_data64(&entry, val);
			oprofile_write_commit(&entry);

			/* reenable the IRQ */
			ctl = op_amd_randomize_ibs_op(ibs_op_ctl);
			wrmsrl(MSR_AMD64_IBSOPCTL, ctl);
		}
	}
}

static inline void op_amd_start_ibs(void)
{
	u64 val;

	if (!ibs_caps)
		return;

	if (ibs_config.fetch_enabled) {
		val = (ibs_config.max_cnt_fetch >> 4) & IBS_FETCH_MAX_CNT;
		val |= ibs_config.rand_en ? IBS_FETCH_RAND_EN : 0;
		val |= IBS_FETCH_ENABLE;
		wrmsrl(MSR_AMD64_IBSFETCHCTL, val);
	}

	if (ibs_config.op_enabled) {
		ibs_op_ctl = ibs_config.max_cnt_op >> 4;
		if (!(ibs_caps & IBS_CAPS_RDWROPCNT)) {
			/*
			 * IbsOpCurCnt not supported.  See
			 * op_amd_randomize_ibs_op() for details.
			 */
			ibs_op_ctl = clamp(ibs_op_ctl, 0x0081ULL, 0xFF80ULL);
		} else {
			/*
			 * The start value is randomized with a
			 * positive offset, we need to compensate it
			 * with the half of the randomized range. Also
			 * avoid underflows.
			 */
			ibs_op_ctl = min(ibs_op_ctl + IBS_RANDOM_MAXCNT_OFFSET,
					 IBS_OP_MAX_CNT);
		}
		if (ibs_caps & IBS_CAPS_OPCNT && ibs_config.dispatched_ops)
			ibs_op_ctl |= IBS_OP_CNT_CTL;
		ibs_op_ctl |= IBS_OP_ENABLE;
		val = op_amd_randomize_ibs_op(ibs_op_ctl);
		wrmsrl(MSR_AMD64_IBSOPCTL, val);
	}
}

static void op_amd_stop_ibs(void)
{
	if (!ibs_caps)
		return;

	if (ibs_config.fetch_enabled)
		/* clear max count and enable */
		wrmsrl(MSR_AMD64_IBSFETCHCTL, 0);

	if (ibs_config.op_enabled)
		/* clear max count and enable */
		wrmsrl(MSR_AMD64_IBSOPCTL, 0);
}

static int op_amd_check_ctrs(struct pt_regs * const regs,
			     struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	for (i = 0; i < NUM_COUNTERS; ++i) {
		int virt = op_x86_phys_to_virt(i);
		if (!reset_value[virt])
			continue;
		rdmsrl(msrs->counters[i].addr, val);
		/* bit is clear if overflowed: */
		if (val & OP_CTR_OVERFLOW)
			continue;
		oprofile_add_sample(regs, virt);
		wrmsrl(msrs->counters[i].addr, -(u64)reset_value[virt]);
	}

	op_amd_handle_ibs(regs, msrs);

	/* See op_model_ppro.c */
	return 1;
}

static void op_amd_start(struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	for (i = 0; i < NUM_COUNTERS; ++i) {
		if (!reset_value[op_x86_phys_to_virt(i)])
			continue;
		rdmsrl(msrs->controls[i].addr, val);
		val |= ARCH_PERFMON_EVENTSEL_ENABLE;
		wrmsrl(msrs->controls[i].addr, val);
	}

	op_amd_start_ibs();
}

static void op_amd_stop(struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	/*
	 * Subtle: stop on all counters to avoid race with setting our
	 * pm callback
	 */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		if (!reset_value[op_x86_phys_to_virt(i)])
			continue;
		rdmsrl(msrs->controls[i].addr, val);
		val &= ~ARCH_PERFMON_EVENTSEL_ENABLE;
		wrmsrl(msrs->controls[i].addr, val);
	}

	op_amd_stop_ibs();
}

static void op_amd_shutdown(struct op_msrs const * const msrs)
{
	int i;

	for (i = 0; i < NUM_COUNTERS; ++i) {
		if (msrs->counters[i].addr)
			release_perfctr_nmi(MSR_K7_PERFCTR0 + i);
	}
	for (i = 0; i < NUM_CONTROLS; ++i) {
		if (msrs->controls[i].addr)
			release_evntsel_nmi(MSR_K7_EVNTSEL0 + i);
	}
}

static u8 ibs_eilvt_off;

static inline void apic_init_ibs_nmi_per_cpu(void *arg)
{
	ibs_eilvt_off = setup_APIC_eilvt_ibs(0, APIC_EILVT_MSG_NMI, 0);
}

static inline void apic_clear_ibs_nmi_per_cpu(void *arg)
{
	setup_APIC_eilvt_ibs(0, APIC_EILVT_MSG_FIX, 1);
}

static int init_ibs_nmi(void)
{
#define IBSCTL_LVTOFFSETVAL		(1 << 8)
#define IBSCTL				0x1cc
	struct pci_dev *cpu_cfg;
	int nodes;
	u32 value = 0;

	/* per CPU setup */
	on_each_cpu(apic_init_ibs_nmi_per_cpu, NULL, 1);

	nodes = 0;
	cpu_cfg = NULL;
	do {
		cpu_cfg = pci_get_device(PCI_VENDOR_ID_AMD,
					 PCI_DEVICE_ID_AMD_10H_NB_MISC,
					 cpu_cfg);
		if (!cpu_cfg)
			break;
		++nodes;
		pci_write_config_dword(cpu_cfg, IBSCTL, ibs_eilvt_off
				       | IBSCTL_LVTOFFSETVAL);
		pci_read_config_dword(cpu_cfg, IBSCTL, &value);
		if (value != (ibs_eilvt_off | IBSCTL_LVTOFFSETVAL)) {
			pci_dev_put(cpu_cfg);
			printk(KERN_DEBUG "Failed to setup IBS LVT offset, "
				"IBSCTL = 0x%08x", value);
			return 1;
		}
	} while (1);

	if (!nodes) {
		printk(KERN_DEBUG "No CPU node configured for IBS");
		return 1;
	}

	return 0;
}

/* uninitialize the APIC for the IBS interrupts if needed */
static void clear_ibs_nmi(void)
{
	if (ibs_caps)
		on_each_cpu(apic_clear_ibs_nmi_per_cpu, NULL, 1);
}

/* initialize the APIC for the IBS interrupts if available */
static void ibs_init(void)
{
	ibs_caps = get_ibs_caps();

	if (!ibs_caps)
		return;

	if (init_ibs_nmi()) {
		ibs_caps = 0;
		return;
	}

	printk(KERN_INFO "oprofile: AMD IBS detected (0x%08x)\n",
	       (unsigned)ibs_caps);
}

static void ibs_exit(void)
{
	if (!ibs_caps)
		return;

	clear_ibs_nmi();
}

static int (*create_arch_files)(struct super_block *sb, struct dentry *root);

static int setup_ibs_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	int ret = 0;

	/* architecture specific files */
	if (create_arch_files)
		ret = create_arch_files(sb, root);

	if (ret)
		return ret;

	if (!ibs_caps)
		return ret;

	/* model specific files */

	/* setup some reasonable defaults */
	ibs_config.max_cnt_fetch = 250000;
	ibs_config.fetch_enabled = 0;
	ibs_config.max_cnt_op = 250000;
	ibs_config.op_enabled = 0;
	ibs_config.dispatched_ops = 0;

	dir = oprofilefs_mkdir(sb, root, "ibs_fetch");
	oprofilefs_create_ulong(sb, dir, "enable",
				&ibs_config.fetch_enabled);
	oprofilefs_create_ulong(sb, dir, "max_count",
				&ibs_config.max_cnt_fetch);
	oprofilefs_create_ulong(sb, dir, "rand_enable",
				&ibs_config.rand_en);

	dir = oprofilefs_mkdir(sb, root, "ibs_op");
	oprofilefs_create_ulong(sb, dir, "enable",
				&ibs_config.op_enabled);
	oprofilefs_create_ulong(sb, dir, "max_count",
				&ibs_config.max_cnt_op);
	if (ibs_caps & IBS_CAPS_OPCNT)
		oprofilefs_create_ulong(sb, dir, "dispatched_ops",
					&ibs_config.dispatched_ops);

	return 0;
}

static int op_amd_init(struct oprofile_operations *ops)
{
	ibs_init();
	create_arch_files = ops->create_files;
	ops->create_files = setup_ibs_files;
	return 0;
}

static void op_amd_exit(void)
{
	ibs_exit();
}

struct op_x86_model_spec op_amd_spec = {
	.num_counters		= NUM_COUNTERS,
	.num_controls		= NUM_CONTROLS,
	.num_virt_counters	= NUM_VIRT_COUNTERS,
	.reserved		= MSR_AMD_EVENTSEL_RESERVED,
	.event_mask		= OP_EVENT_MASK,
	.init			= op_amd_init,
	.exit			= op_amd_exit,
	.fill_in_addresses	= &op_amd_fill_in_addresses,
	.setup_ctrs		= &op_amd_setup_ctrs,
	.check_ctrs		= &op_amd_check_ctrs,
	.start			= &op_amd_start,
	.stop			= &op_amd_stop,
	.shutdown		= &op_amd_shutdown,
#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX
	.switch_ctrl		= &op_mux_switch_ctrl,
#endif
};
