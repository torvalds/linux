/*
 * Performance event support for s390x
 *
 *  Copyright IBM Corp. 2012
 *  Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */
#define KMSG_COMPONENT	"perf"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/kvm_host.h>
#include <linux/percpu.h>
#include <linux/export.h>
#include <asm/irq.h>
#include <asm/cpu_mf.h>
#include <asm/lowcore.h>
#include <asm/processor.h>

const char *perf_pmu_name(void)
{
	if (cpum_cf_avail() || cpum_sf_avail())
		return "CPU-measurement facilities (CPUMF)";
	return "pmu";
}
EXPORT_SYMBOL(perf_pmu_name);

int perf_num_counters(void)
{
	int num = 0;

	if (cpum_cf_avail())
		num += PERF_CPUM_CF_MAX_CTR;

	return num;
}
EXPORT_SYMBOL(perf_num_counters);

static struct kvm_s390_sie_block *sie_block(struct pt_regs *regs)
{
	struct stack_frame *stack = (struct stack_frame *) regs->gprs[15];

	if (!stack)
		return NULL;

	return (struct kvm_s390_sie_block *) stack->empty1[0];
}

static bool is_in_guest(struct pt_regs *regs)
{
	unsigned long ip = instruction_pointer(regs);

	if (user_mode(regs))
		return false;

	return ip == (unsigned long) &sie_exit;
}

static unsigned long guest_is_user_mode(struct pt_regs *regs)
{
	return sie_block(regs)->gpsw.mask & PSW_MASK_PSTATE;
}

static unsigned long instruction_pointer_guest(struct pt_regs *regs)
{
	return sie_block(regs)->gpsw.addr & PSW_ADDR_INSN;
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

unsigned long perf_misc_flags(struct pt_regs *regs)
{
	if (is_in_guest(regs))
		return perf_misc_guest_flags(regs);

	return user_mode(regs) ? PERF_RECORD_MISC_USER
			       : PERF_RECORD_MISC_KERNEL;
}

void perf_event_print_debug(void)
{
	struct cpumf_ctr_info cf_info;
	unsigned long flags;
	int cpu;

	if (!cpum_cf_avail())
		return;

	local_irq_save(flags);

	cpu = smp_processor_id();
	memset(&cf_info, 0, sizeof(cf_info));
	if (!qctri(&cf_info)) {
		pr_info("CPU[%i] CPUM_CF: ver=%u.%u A=%04x E=%04x C=%04x\n",
			cpu, cf_info.cfvn, cf_info.csvn,
			cf_info.auth_ctl, cf_info.enable_ctl, cf_info.act_ctl);
		print_hex_dump_bytes("CPUMF Query: ", DUMP_PREFIX_OFFSET,
				     &cf_info, sizeof(cf_info));
	}

	local_irq_restore(flags);
}

/* See also arch/s390/kernel/traps.c */
static unsigned long __store_trace(struct perf_callchain_entry *entry,
				   unsigned long sp,
				   unsigned long low, unsigned long high)
{
	struct stack_frame *sf;
	struct pt_regs *regs;

	while (1) {
		sp = sp & PSW_ADDR_INSN;
		if (sp < low || sp > high - sizeof(*sf))
			return sp;
		sf = (struct stack_frame *) sp;
		perf_callchain_store(entry, sf->gprs[8] & PSW_ADDR_INSN);
		/* Follow the backchain. */
		while (1) {
			low = sp;
			sp = sf->back_chain & PSW_ADDR_INSN;
			if (!sp)
				break;
			if (sp <= low || sp > high - sizeof(*sf))
				return sp;
			sf = (struct stack_frame *) sp;
			perf_callchain_store(entry,
					     sf->gprs[8] & PSW_ADDR_INSN);
		}
		/* Zero backchain detected, check for interrupt frame. */
		sp = (unsigned long) (sf + 1);
		if (sp <= low || sp > high - sizeof(*regs))
			return sp;
		regs = (struct pt_regs *) sp;
		perf_callchain_store(entry, sf->gprs[8] & PSW_ADDR_INSN);
		low = sp;
		sp = regs->gprs[15];
	}
}

void perf_callchain_kernel(struct perf_callchain_entry *entry,
			   struct pt_regs *regs)
{
	unsigned long head;
	struct stack_frame *head_sf;

	if (user_mode(regs))
		return;

	head = regs->gprs[15];
	head_sf = (struct stack_frame *) head;

	if (!head_sf || !head_sf->back_chain)
		return;

	head = head_sf->back_chain;
	head = __store_trace(entry, head, S390_lowcore.async_stack - ASYNC_SIZE,
			     S390_lowcore.async_stack);

	__store_trace(entry, head, S390_lowcore.thread_info,
		      S390_lowcore.thread_info + THREAD_SIZE);
}
