/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KVM nVHE hypervisor stack tracing support.
 *
 * The unwinder implementation depends on the nVHE mode:
 *
 *   1) Non-protected nVHE mode - the host can directly access the
 *      HYP stack pages and unwind the HYP stack in EL1. This saves having
 *      to allocate shared buffers for the host to read the unwinded
 *      stacktrace.
 *
 *   2) pKVM (protected nVHE) mode - the host cannot directly access
 *      the HYP memory. The stack is unwinded in EL2 and dumped to a shared
 *      buffer where the host can read and print the stacktrace.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/stacktrace/nvhe.h>

/*
 * kvm_nvhe_stack_kern_va - Convert KVM nVHE HYP stack addresses to a kernel VAs
 *
 * The nVHE hypervisor stack is mapped in the flexible 'private' VA range, to
 * allow for guard pages below the stack. Consequently, the fixed offset address
 * translation macros won't work here.
 *
 * The kernel VA is calculated as an offset from the kernel VA of the hypervisor
 * stack base.
 *
 * Returns true on success and updates @addr to its corresponding kernel VA;
 * otherwise returns false.
 */
static bool kvm_nvhe_stack_kern_va(unsigned long *addr,
				   enum stack_type type)
{
	struct kvm_nvhe_stacktrace_info *stacktrace_info;
	unsigned long hyp_base, kern_base, hyp_offset;

	stacktrace_info = this_cpu_ptr_nvhe_sym(kvm_stacktrace_info);

	switch (type) {
	case STACK_TYPE_HYP:
		kern_base = (unsigned long)*this_cpu_ptr(&kvm_arm_hyp_stack_page);
		hyp_base = (unsigned long)stacktrace_info->stack_base;
		break;
	case STACK_TYPE_OVERFLOW:
		kern_base = (unsigned long)this_cpu_ptr_nvhe_sym(overflow_stack);
		hyp_base = (unsigned long)stacktrace_info->overflow_stack_base;
		break;
	default:
		return false;
	}

	hyp_offset = *addr - hyp_base;

	*addr = kern_base + hyp_offset;

	return true;
}

static bool on_overflow_stack(unsigned long sp, unsigned long size,
			      struct stack_info *info)
{
	struct kvm_nvhe_stacktrace_info *stacktrace_info
				= this_cpu_ptr_nvhe_sym(kvm_stacktrace_info);
	unsigned long low = (unsigned long)stacktrace_info->overflow_stack_base;
	unsigned long high = low + OVERFLOW_STACK_SIZE;

	return on_stack(sp, size, low, high, STACK_TYPE_OVERFLOW, info);
}

static bool on_hyp_stack(unsigned long sp, unsigned long size,
			 struct stack_info *info)
{
	struct kvm_nvhe_stacktrace_info *stacktrace_info
				= this_cpu_ptr_nvhe_sym(kvm_stacktrace_info);
	unsigned long low = (unsigned long)stacktrace_info->stack_base;
	unsigned long high = low + PAGE_SIZE;

	return on_stack(sp, size, low, high, STACK_TYPE_HYP, info);
}

static bool on_accessible_stack(const struct task_struct *tsk,
				unsigned long sp, unsigned long size,
				struct stack_info *info)
{
	if (info)
		info->type = STACK_TYPE_UNKNOWN;

	return (on_overflow_stack(sp, size, info) ||
		on_hyp_stack(sp, size, info));
}

static int unwind_next(struct unwind_state *state)
{
	struct stack_info info;

	return unwind_next_common(state, &info, on_accessible_stack,
				  kvm_nvhe_stack_kern_va);
}

static void unwind(struct unwind_state *state,
		   stack_trace_consume_fn consume_entry, void *cookie)
{
	while (1) {
		int ret;

		if (!consume_entry(cookie, state->pc))
			break;
		ret = unwind_next(state);
		if (ret < 0)
			break;
	}
}

/*
 * kvm_nvhe_dump_backtrace_entry - Symbolize and print an nVHE backtrace entry
 *
 * @arg    : the hypervisor offset, used for address translation
 * @where  : the program counter corresponding to the stack frame
 */
static bool kvm_nvhe_dump_backtrace_entry(void *arg, unsigned long where)
{
	unsigned long va_mask = GENMASK_ULL(vabits_actual - 1, 0);
	unsigned long hyp_offset = (unsigned long)arg;

	/* Mask tags and convert to kern addr */
	where = (where & va_mask) + hyp_offset;
	kvm_err(" [<%016lx>] %pB\n", where, (void *)(where + kaslr_offset()));

	return true;
}

static void kvm_nvhe_dump_backtrace_start(void)
{
	kvm_err("nVHE call trace:\n");
}

static void kvm_nvhe_dump_backtrace_end(void)
{
	kvm_err("---[ end nVHE call trace ]---\n");
}

/*
 * hyp_dump_backtrace - Dump the non-protected nVHE backtrace.
 *
 * @hyp_offset: hypervisor offset, used for address translation.
 *
 * The host can directly access HYP stack pages in non-protected
 * mode, so the unwinding is done directly from EL1. This removes
 * the need for shared buffers between host and hypervisor for
 * the stacktrace.
 */
static void hyp_dump_backtrace(unsigned long hyp_offset)
{
	struct kvm_nvhe_stacktrace_info *stacktrace_info;
	struct unwind_state state;

	stacktrace_info = this_cpu_ptr_nvhe_sym(kvm_stacktrace_info);

	kvm_nvhe_unwind_init(&state, stacktrace_info->fp, stacktrace_info->pc);

	kvm_nvhe_dump_backtrace_start();
	unwind(&state, kvm_nvhe_dump_backtrace_entry, (void *)hyp_offset);
	kvm_nvhe_dump_backtrace_end();
}

#ifdef CONFIG_PROTECTED_NVHE_STACKTRACE
DECLARE_KVM_NVHE_PER_CPU(unsigned long [NVHE_STACKTRACE_SIZE/sizeof(long)],
			 pkvm_stacktrace);

/*
 * pkvm_dump_backtrace - Dump the protected nVHE HYP backtrace.
 *
 * @hyp_offset: hypervisor offset, used for address translation.
 *
 * Dumping of the pKVM HYP backtrace is done by reading the
 * stack addresses from the shared stacktrace buffer, since the
 * host cannot directly access hypervisor memory in protected
 * mode.
 */
static void pkvm_dump_backtrace(unsigned long hyp_offset)
{
	unsigned long *stacktrace
		= (unsigned long *) this_cpu_ptr_nvhe_sym(pkvm_stacktrace);
	int i, size = NVHE_STACKTRACE_SIZE / sizeof(long);

	kvm_nvhe_dump_backtrace_start();
	/* The saved stacktrace is terminated by a null entry */
	for (i = 0; i < size && stacktrace[i]; i++)
		kvm_nvhe_dump_backtrace_entry((void *)hyp_offset, stacktrace[i]);
	kvm_nvhe_dump_backtrace_end();
}
#else	/* !CONFIG_PROTECTED_NVHE_STACKTRACE */
static void pkvm_dump_backtrace(unsigned long hyp_offset)
{
	kvm_err("Cannot dump pKVM nVHE stacktrace: !CONFIG_PROTECTED_NVHE_STACKTRACE\n");
}
#endif /* CONFIG_PROTECTED_NVHE_STACKTRACE */

/*
 * kvm_nvhe_dump_backtrace - Dump KVM nVHE hypervisor backtrace.
 *
 * @hyp_offset: hypervisor offset, used for address translation.
 */
void kvm_nvhe_dump_backtrace(unsigned long hyp_offset)
{
	if (is_protected_kvm_enabled())
		pkvm_dump_backtrace(hyp_offset);
	else
		hyp_dump_backtrace(hyp_offset);
}
