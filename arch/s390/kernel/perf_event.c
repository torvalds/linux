// SPDX-License-Identifier: GPL-2.0
/*
 * Performance event support for s390x
 *
 *  Copyright IBM Corp. 2012, 2013
 *  Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#define KMSG_COMPONENT	"perf"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/kvm_host.h>
#include <linux/percpu.h>
#include <linux/export.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/sysfs.h>
#include <asm/stacktrace.h>
#include <asm/irq.h>
#include <asm/cpu_mf.h>
#include <asm/lowcore.h>
#include <asm/processor.h>
#include <asm/sysinfo.h>
#include <asm/unwind.h>

static struct kvm_s390_sie_block *sie_block(struct pt_regs *regs)
{
	struct stack_frame *stack = (struct stack_frame *) regs->gprs[15];

	if (!stack)
		return NULL;

	return (struct kvm_s390_sie_block *)stack->sie_control_block;
}

static bool is_in_guest(struct pt_regs *regs)
{
	if (user_mode(regs))
		return false;
#if IS_ENABLED(CONFIG_KVM)
	return instruction_pointer(regs) == (unsigned long) &sie_exit;
#else
	return false;
#endif
}

static unsigned long guest_is_user_mode(struct pt_regs *regs)
{
	return sie_block(regs)->gpsw.mask & PSW_MASK_PSTATE;
}

static unsigned long instruction_pointer_guest(struct pt_regs *regs)
{
	return sie_block(regs)->gpsw.addr;
}

unsigned long perf_instruction_pointer(struct pt_regs *regs)
{
	return is_in_guest(regs) ? instruction_pointer_guest(regs)
				 : instruction_pointer(regs);
}

static unsigned long perf_misc_guest_flags(struct pt_regs *regs)
{
	return guest_is_user_mode(regs) ? PERF_RECORD_MISC_GUEST_USER
					: PERF_RECORD_MISC_GUEST_KERNEL;
}

static unsigned long perf_misc_flags_sf(struct pt_regs *regs)
{
	struct perf_sf_sde_regs *sde_regs;
	unsigned long flags;

	sde_regs = (struct perf_sf_sde_regs *) &regs->int_parm_long;
	if (sde_regs->in_guest)
		flags = user_mode(regs) ? PERF_RECORD_MISC_GUEST_USER
					: PERF_RECORD_MISC_GUEST_KERNEL;
	else
		flags = user_mode(regs) ? PERF_RECORD_MISC_USER
					: PERF_RECORD_MISC_KERNEL;
	return flags;
}

unsigned long perf_misc_flags(struct pt_regs *regs)
{
	/* Check if the cpum_sf PMU has created the pt_regs structure.
	 * In this case, perf misc flags can be easily extracted.  Otherwise,
	 * do regular checks on the pt_regs content.
	 */
	if (regs->int_code == 0x1407 && regs->int_parm == CPU_MF_INT_SF_PRA)
		if (!regs->gprs[15])
			return perf_misc_flags_sf(regs);

	if (is_in_guest(regs))
		return perf_misc_guest_flags(regs);

	return user_mode(regs) ? PERF_RECORD_MISC_USER
			       : PERF_RECORD_MISC_KERNEL;
}

static void print_debug_cf(void)
{
	struct cpumf_ctr_info cf_info;
	int cpu = smp_processor_id();

	memset(&cf_info, 0, sizeof(cf_info));
	if (!qctri(&cf_info))
		pr_info("CPU[%i] CPUM_CF: ver=%u.%u A=%04x E=%04x C=%04x\n",
			cpu, cf_info.cfvn, cf_info.csvn,
			cf_info.auth_ctl, cf_info.enable_ctl, cf_info.act_ctl);
}

static void print_debug_sf(void)
{
	struct hws_qsi_info_block si;
	int cpu = smp_processor_id();

	memset(&si, 0, sizeof(si));
	if (qsi(&si))
		return;

	pr_info("CPU[%i] CPUM_SF: basic=%i diag=%i min=%lu max=%lu cpu_speed=%u\n",
		cpu, si.as, si.ad, si.min_sampl_rate, si.max_sampl_rate,
		si.cpu_speed);

	if (si.as)
		pr_info("CPU[%i] CPUM_SF: Basic-sampling: a=%i e=%i c=%i"
			" bsdes=%i tear=%016lx dear=%016lx\n", cpu,
			si.as, si.es, si.cs, si.bsdes, si.tear, si.dear);
	if (si.ad)
		pr_info("CPU[%i] CPUM_SF: Diagnostic-sampling: a=%i e=%i c=%i"
			" dsdes=%i tear=%016lx dear=%016lx\n", cpu,
			si.ad, si.ed, si.cd, si.dsdes, si.tear, si.dear);
}

void perf_event_print_debug(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (cpum_cf_avail())
		print_debug_cf();
	if (cpum_sf_avail())
		print_debug_sf();
	local_irq_restore(flags);
}

/* Service level infrastructure */
static void sl_print_counter(struct seq_file *m)
{
	struct cpumf_ctr_info ci;

	memset(&ci, 0, sizeof(ci));
	if (qctri(&ci))
		return;

	seq_printf(m, "CPU-MF: Counter facility: version=%u.%u "
		   "authorization=%04x\n", ci.cfvn, ci.csvn, ci.auth_ctl);
}

static void sl_print_sampling(struct seq_file *m)
{
	struct hws_qsi_info_block si;

	memset(&si, 0, sizeof(si));
	if (qsi(&si))
		return;

	if (!si.as && !si.ad)
		return;

	seq_printf(m, "CPU-MF: Sampling facility: min_rate=%lu max_rate=%lu"
		   " cpu_speed=%u\n", si.min_sampl_rate, si.max_sampl_rate,
		   si.cpu_speed);
	if (si.as)
		seq_printf(m, "CPU-MF: Sampling facility: mode=basic"
			   " sample_size=%u\n", si.bsdes);
	if (si.ad)
		seq_printf(m, "CPU-MF: Sampling facility: mode=diagnostic"
			   " sample_size=%u\n", si.dsdes);
}

static void service_level_perf_print(struct seq_file *m,
				     struct service_level *sl)
{
	if (cpum_cf_avail())
		sl_print_counter(m);
	if (cpum_sf_avail())
		sl_print_sampling(m);
}

static struct service_level service_level_perf = {
	.seq_print = service_level_perf_print,
};

static int __init service_level_perf_register(void)
{
	return register_service_level(&service_level_perf);
}
arch_initcall(service_level_perf_register);

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
			   struct pt_regs *regs)
{
	struct unwind_state state;
	unsigned long addr;

	unwind_for_each_frame(&state, current, regs, 0) {
		addr = unwind_get_return_address(&state);
		if (!addr || perf_callchain_store(entry, addr))
			return;
	}
}

void perf_callchain_user(struct perf_callchain_entry_ctx *entry,
			 struct pt_regs *regs)
{
	struct stack_frame_user __user *sf;
	unsigned long ip, sp;
	bool first = true;

	if (is_compat_task())
		return;
	perf_callchain_store(entry, instruction_pointer(regs));
	sf = (void __user *)user_stack_pointer(regs);
	pagefault_disable();
	while (entry->nr < entry->max_stack) {
		if (__get_user(sp, &sf->back_chain))
			break;
		if (__get_user(ip, &sf->gprs[8]))
			break;
		if (ip & 0x1) {
			/*
			 * If the instruction address is invalid, and this
			 * is the first stack frame, assume r14 has not
			 * been written to the stack yet. Otherwise exit.
			 */
			if (first && !(regs->gprs[14] & 0x1))
				ip = regs->gprs[14];
			else
				break;
		}
		perf_callchain_store(entry, ip);
		/* Sanity check: ABI requires SP to be aligned 8 bytes. */
		if (!sp || sp & 0x7)
			break;
		sf = (void __user *)sp;
		first = false;
	}
	pagefault_enable();
}

/* Perf definitions for PMU event attributes in sysfs */
ssize_t cpumf_events_sysfs_show(struct device *dev,
				struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sprintf(page, "event=0x%04llx\n", pmu_attr->id);
}
