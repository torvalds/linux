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

/* IbsFetchCtl bits/masks */
#define IBS_FETCH_RAND_EN		(1ULL<<57)
#define IBS_FETCH_VAL			(1ULL<<49)
#define IBS_FETCH_ENABLE		(1ULL<<48)
#define IBS_FETCH_CNT_MASK		0xFFFF0000ULL

/*IbsOpCtl bits */
#define IBS_OP_CNT_CTL			(1ULL<<19)
#define IBS_OP_VAL			(1ULL<<18)
#define IBS_OP_ENABLE			(1ULL<<17)

#define IBS_FETCH_SIZE			6
#define IBS_OP_SIZE			12

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

#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX

static void op_mux_fill_in_addresses(struct op_msrs * const msrs)
{
	int i;

	for (i = 0; i < NUM_VIRT_COUNTERS; i++) {
		int hw_counter = op_x86_virt_to_phys(i);
		if (reserve_perfctr_nmi(MSR_K7_PERFCTR0 + i))
			msrs->multiplex[i].addr = MSR_K7_PERFCTR0 + hw_counter;
		else
			msrs->multiplex[i].addr = 0;
	}
}

static void op_mux_switch_ctrl(struct op_x86_model_spec const *model,
			       struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	/* enable active counters */
	for (i = 0; i < NUM_COUNTERS; ++i) {
		int virt = op_x86_phys_to_virt(i);
		if (!counter_config[virt].enabled)
			continue;
		rdmsrl(msrs->controls[i].addr, val);
		val &= model->reserved;
		val |= op_x86_get_ctrl(model, &counter_config[virt]);
		wrmsrl(msrs->controls[i].addr, val);
	}
}

#else

static inline void op_mux_fill_in_addresses(struct op_msrs * const msrs) { }

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

	op_mux_fill_in_addresses(msrs);
}

static void op_amd_setup_ctrs(struct op_x86_model_spec const *model,
			      struct op_msrs const * const msrs)
{
	u64 val;
	int i;

	/* setup reset_value */
	for (i = 0; i < NUM_VIRT_COUNTERS; ++i) {
		if (counter_config[i].enabled)
			reset_value[i] = counter_config[i].count;
		else
			reset_value[i] = 0;
	}

	/* clear all counters */
	for (i = 0; i < NUM_CONTROLS; ++i) {
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
		int virt = op_x86_phys_to_virt(i);
		if (!counter_config[virt].enabled)
			continue;
		if (!msrs->counters[i].addr)
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

static inline void
op_amd_handle_ibs(struct pt_regs * const regs,
		  struct op_msrs const * const msrs)
{
	u64 val, ctl;
	struct op_entry entry;

	if (!has_ibs)
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
			ctl &= ~(IBS_FETCH_VAL | IBS_FETCH_CNT_MASK);
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
			ctl &= ~IBS_OP_VAL & 0xFFFFFFFF;
			ctl |= IBS_OP_ENABLE;
			wrmsrl(MSR_AMD64_IBSOPCTL, ctl);
		}
	}
}

static inline void op_amd_start_ibs(void)
{
	u64 val;
	if (has_ibs && ibs_config.fetch_enabled) {
		val = (ibs_config.max_cnt_fetch >> 4) & 0xFFFF;
		val |= ibs_config.rand_en ? IBS_FETCH_RAND_EN : 0;
		val |= IBS_FETCH_ENABLE;
		wrmsrl(MSR_AMD64_IBSFETCHCTL, val);
	}

	if (has_ibs && ibs_config.op_enabled) {
		val = (ibs_config.max_cnt_op >> 4) & 0xFFFF;
		val |= ibs_config.dispatched_ops ? IBS_OP_CNT_CTL : 0;
		val |= IBS_OP_ENABLE;
		wrmsrl(MSR_AMD64_IBSOPCTL, val);
	}
}

static void op_amd_stop_ibs(void)
{
	if (has_ibs && ibs_config.fetch_enabled)
		/* clear max count and enable */
		wrmsrl(MSR_AMD64_IBSFETCHCTL, 0);

	if (has_ibs && ibs_config.op_enabled)
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
		val |= ARCH_PERFMON_EVENTSEL0_ENABLE;
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
		val &= ~ARCH_PERFMON_EVENTSEL0_ENABLE;
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
