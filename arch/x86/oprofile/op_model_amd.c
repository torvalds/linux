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
 * @author Barry Kasindorf
 */

#include <linux/oprofile.h>
#include <linux/device.h>
#include <linux/pci.h>

#include <asm/ptrace.h>
#include <asm/msr.h>
#include <asm/nmi.h>

#include "op_x86_model.h"
#include "op_counter.h"

#define NUM_COUNTERS 4
#define NUM_CONTROLS 4
#define OP_EVENT_MASK			0x0FFF
#define OP_CTR_OVERFLOW			(1ULL<<31)

#define MSR_AMD_EVENTSEL_RESERVED	((0xFFFFFCF0ULL<<32)|(1ULL<<21))

static unsigned long reset_value[NUM_COUNTERS];

#ifdef CONFIG_OPROFILE_IBS

/* IbsFetchCtl bits/masks */
#define IBS_FETCH_HIGH_VALID_BIT	(1UL << 17)	/* bit 49 */
#define IBS_FETCH_HIGH_ENABLE		(1UL << 16)	/* bit 48 */
#define IBS_FETCH_LOW_MAX_CNT_MASK	0x0000FFFFUL	/* MaxCnt mask */

/*IbsOpCtl bits */
#define IBS_OP_LOW_VALID_BIT		(1ULL<<18)	/* bit 18 */
#define IBS_OP_LOW_ENABLE		(1ULL<<17)	/* bit 17 */

#define IBS_FETCH_SIZE	6
#define IBS_OP_SIZE	12

static int has_ibs;	/* AMD Family10h and later */

struct op_ibs_config {
	unsigned long op_enabled;
	unsigned long fetch_enabled;
	unsigned long max_cnt_fetch;
	unsigned long max_cnt_op;
	unsigned long rand_en;
	unsigned long dispatched_ops;
};

static struct op_ibs_config ibs_config;

#endif

/* functions for op_amd_spec */

static void op_amd_fill_in_addresses(struct op_msrs * const msrs)
{
	int i;

	for (i = 0; i < NUM_COUNTERS; i++) {
		if (reserve_perfctr_nmi(MSR_K7_PERFCTR0 + i))
			msrs->counters[i].addr = MSR_K7_PERFCTR0 + i;
		else
			msrs->counters[i].addr = 0;
	}

	for (i = 0; i < NUM_CONTROLS; i++) {
		if (reserve_evntsel_nmi(MSR_K7_EVNTSEL0 + i))
			msrs->controls[i].addr = MSR_K7_EVNTSEL0 + i;
		else
			msrs->controls[i].addr = 0;
	}
}

static void op_amd_setup_ctrs(struct op_x86_model_spec const *model,
			      struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	/* clear all counters */
	for (i = 0 ; i < NUM_CONTROLS; ++i) {
		if (unlikely(!msrs->controls[i].addr))
			continue;
		rdmsrl(msrs->controls[i].addr, val);
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
		if (counter_config[i].enabled && msrs->counters[i].addr) {
			reset_value[i] = counter_config[i].count;
			wrmsrl(msrs->counters[i].addr,
			       -(s64)counter_config[i].count);
			rdmsrl(msrs->controls[i].addr, val);
			val &= model->reserved;
			val |= op_x86_get_ctrl(model, &counter_config[i]);
			wrmsrl(msrs->controls[i].addr, val);
		} else {
			reset_value[i] = 0;
		}
	}
}

#ifdef CONFIG_OPROFILE_IBS

static inline int
op_amd_handle_ibs(struct pt_regs * const regs,
		  struct op_msrs const * const msrs)
{
	u32 low, high;
	u64 msr;
	struct op_entry entry;

	if (!has_ibs)
		return 1;

	if (ibs_config.fetch_enabled) {
		rdmsr(MSR_AMD64_IBSFETCHCTL, low, high);
		if (high & IBS_FETCH_HIGH_VALID_BIT) {
			rdmsrl(MSR_AMD64_IBSFETCHLINAD, msr);
			oprofile_write_reserve(&entry, regs, msr,
					       IBS_FETCH_CODE, IBS_FETCH_SIZE);
			oprofile_add_data(&entry, (u32)msr);
			oprofile_add_data(&entry, (u32)(msr >> 32));
			oprofile_add_data(&entry, low);
			oprofile_add_data(&entry, high);
			rdmsrl(MSR_AMD64_IBSFETCHPHYSAD, msr);
			oprofile_add_data(&entry, (u32)msr);
			oprofile_add_data(&entry, (u32)(msr >> 32));
			oprofile_write_commit(&entry);

			/* reenable the IRQ */
			high &= ~IBS_FETCH_HIGH_VALID_BIT;
			high |= IBS_FETCH_HIGH_ENABLE;
			low &= IBS_FETCH_LOW_MAX_CNT_MASK;
			wrmsr(MSR_AMD64_IBSFETCHCTL, low, high);
		}
	}

	if (ibs_config.op_enabled) {
		rdmsr(MSR_AMD64_IBSOPCTL, low, high);
		if (low & IBS_OP_LOW_VALID_BIT) {
			rdmsrl(MSR_AMD64_IBSOPRIP, msr);
			oprofile_write_reserve(&entry, regs, msr,
					       IBS_OP_CODE, IBS_OP_SIZE);
			oprofile_add_data(&entry, (u32)msr);
			oprofile_add_data(&entry, (u32)(msr >> 32));
			rdmsrl(MSR_AMD64_IBSOPDATA, msr);
			oprofile_add_data(&entry, (u32)msr);
			oprofile_add_data(&entry, (u32)(msr >> 32));
			rdmsrl(MSR_AMD64_IBSOPDATA2, msr);
			oprofile_add_data(&entry, (u32)msr);
			oprofile_add_data(&entry, (u32)(msr >> 32));
			rdmsrl(MSR_AMD64_IBSOPDATA3, msr);
			oprofile_add_data(&entry, (u32)msr);
			oprofile_add_data(&entry, (u32)(msr >> 32));
			rdmsrl(MSR_AMD64_IBSDCLINAD, msr);
			oprofile_add_data(&entry, (u32)msr);
			oprofile_add_data(&entry, (u32)(msr >> 32));
			rdmsrl(MSR_AMD64_IBSDCPHYSAD, msr);
			oprofile_add_data(&entry, (u32)msr);
			oprofile_add_data(&entry, (u32)(msr >> 32));
			oprofile_write_commit(&entry);

			/* reenable the IRQ */
			high = 0;
			low &= ~IBS_OP_LOW_VALID_BIT;
			low |= IBS_OP_LOW_ENABLE;
			wrmsr(MSR_AMD64_IBSOPCTL, low, high);
		}
	}

	return 1;
}

static inline void op_amd_start_ibs(void)
{
	unsigned int low, high;
	if (has_ibs && ibs_config.fetch_enabled) {
		low = (ibs_config.max_cnt_fetch >> 4) & 0xFFFF;
		high = ((ibs_config.rand_en & 0x1) << 25) /* bit 57 */
			+ IBS_FETCH_HIGH_ENABLE;
		wrmsr(MSR_AMD64_IBSFETCHCTL, low, high);
	}

	if (has_ibs && ibs_config.op_enabled) {
		low = ((ibs_config.max_cnt_op >> 4) & 0xFFFF)
			+ ((ibs_config.dispatched_ops & 0x1) << 19) /* bit 19 */
			+ IBS_OP_LOW_ENABLE;
		high = 0;
		wrmsr(MSR_AMD64_IBSOPCTL, low, high);
	}
}

static void op_amd_stop_ibs(void)
{
	unsigned int low, high;
	if (has_ibs && ibs_config.fetch_enabled) {
		/* clear max count and enable */
		low = 0;
		high = 0;
		wrmsr(MSR_AMD64_IBSFETCHCTL, low, high);
	}

	if (has_ibs && ibs_config.op_enabled) {
		/* clear max count and enable */
		low = 0;
		high = 0;
		wrmsr(MSR_AMD64_IBSOPCTL, low, high);
	}
}

#else

static inline int op_amd_handle_ibs(struct pt_regs * const regs,
				    struct op_msrs const * const msrs) { }
static inline void op_amd_start_ibs(void) { }
static inline void op_amd_stop_ibs(void) { }

#endif

static int op_amd_check_ctrs(struct pt_regs * const regs,
			     struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	for (i = 0 ; i < NUM_COUNTERS; ++i) {
		if (!reset_value[i])
			continue;
		rdmsrl(msrs->counters[i].addr, val);
		/* bit is clear if overflowed: */
		if (val & OP_CTR_OVERFLOW)
			continue;
		oprofile_add_sample(regs, i);
		wrmsrl(msrs->counters[i].addr, -(s64)reset_value[i]);
	}

	op_amd_handle_ibs(regs, msrs);

	/* See op_model_ppro.c */
	return 1;
}

static void op_amd_start(struct op_msrs const * const msrs)
{
	u64 val;
	int i;
	for (i = 0 ; i < NUM_COUNTERS ; ++i) {
		if (reset_value[i]) {
			rdmsrl(msrs->controls[i].addr, val);
			val |= ARCH_PERFMON_EVENTSEL0_ENABLE;
			wrmsrl(msrs->controls[i].addr, val);
		}
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
	for (i = 0 ; i < NUM_COUNTERS ; ++i) {
		if (!reset_value[i])
			continue;
		rdmsrl(msrs->controls[i].addr, val);
		val &= ~ARCH_PERFMON_EVENTSEL0_ENABLE;
		wrmsrl(msrs->controls[i].addr, val);
	}

	op_amd_stop_ibs();
}

static void op_amd_shutdown(struct op_msrs const * const msrs)
{
	int i;

	for (i = 0 ; i < NUM_COUNTERS ; ++i) {
		if (msrs->counters[i].addr)
			release_perfctr_nmi(MSR_K7_PERFCTR0 + i);
	}
	for (i = 0 ; i < NUM_CONTROLS ; ++i) {
		if (msrs->controls[i].addr)
			release_evntsel_nmi(MSR_K7_EVNTSEL0 + i);
	}
}

#ifdef CONFIG_OPROFILE_IBS

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

#ifdef CONFIG_NUMA
	/* Sanity check */
	/* Works only for 64bit with proper numa implementation. */
	if (nodes != num_possible_nodes()) {
		printk(KERN_DEBUG "Failed to setup CPU node(s) for IBS, "
			"found: %d, expected %d",
			nodes, num_possible_nodes());
		return 1;
	}
#endif
	return 0;
}

/* uninitialize the APIC for the IBS interrupts if needed */
static void clear_ibs_nmi(void)
{
	if (has_ibs)
		on_each_cpu(apic_clear_ibs_nmi_per_cpu, NULL, 1);
}

/* initialize the APIC for the IBS interrupts if available */
static void ibs_init(void)
{
	has_ibs = boot_cpu_has(X86_FEATURE_IBS);

	if (!has_ibs)
		return;

	if (init_ibs_nmi()) {
		has_ibs = 0;
		return;
	}

	printk(KERN_INFO "oprofile: AMD IBS detected\n");
}

static void ibs_exit(void)
{
	if (!has_ibs)
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

	if (!has_ibs)
		return ret;

	/* model specific files */

	/* setup some reasonable defaults */
	ibs_config.max_cnt_fetch = 250000;
	ibs_config.fetch_enabled = 0;
	ibs_config.max_cnt_op = 250000;
	ibs_config.op_enabled = 0;
	ibs_config.dispatched_ops = 1;

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

#else

/* no IBS support */

static int op_amd_init(struct oprofile_operations *ops)
{
	return 0;
}

static void op_amd_exit(void) {}

#endif /* CONFIG_OPROFILE_IBS */

struct op_x86_model_spec const op_amd_spec = {
	.num_counters		= NUM_COUNTERS,
	.num_controls		= NUM_CONTROLS,
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
};
