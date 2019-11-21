// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2017 Arm Ltd.
#define pr_fmt(fmt) "sdei: " fmt

#include <linux/arm-smccc.h>
#include <linux/arm_sdei.h>
#include <linux/hardirq.h>
#include <linux/irqflags.h>
#include <linux/sched/task_stack.h>
#include <linux/uaccess.h>

#include <asm/alternative.h>
#include <asm/kprobes.h>
#include <asm/mmu.h>
#include <asm/ptrace.h>
#include <asm/sections.h>
#include <asm/stacktrace.h>
#include <asm/sysreg.h>
#include <asm/vmap_stack.h>

unsigned long sdei_exit_mode;

/*
 * VMAP'd stacks checking for stack overflow on exception using sp as a scratch
 * register, meaning SDEI has to switch to its own stack. We need two stacks as
 * a critical event may interrupt a normal event that has just taken a
 * synchronous exception, and is using sp as scratch register. For a critical
 * event interrupting a normal event, we can't reliably tell if we were on the
 * sdei stack.
 * For now, we allocate stacks when the driver is probed.
 */
DECLARE_PER_CPU(unsigned long *, sdei_stack_normal_ptr);
DECLARE_PER_CPU(unsigned long *, sdei_stack_critical_ptr);

#ifdef CONFIG_VMAP_STACK
DEFINE_PER_CPU(unsigned long *, sdei_stack_normal_ptr);
DEFINE_PER_CPU(unsigned long *, sdei_stack_critical_ptr);
#endif

static void _free_sdei_stack(unsigned long * __percpu *ptr, int cpu)
{
	unsigned long *p;

	p = per_cpu(*ptr, cpu);
	if (p) {
		per_cpu(*ptr, cpu) = NULL;
		vfree(p);
	}
}

static void free_sdei_stacks(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		_free_sdei_stack(&sdei_stack_normal_ptr, cpu);
		_free_sdei_stack(&sdei_stack_critical_ptr, cpu);
	}
}

static int _init_sdei_stack(unsigned long * __percpu *ptr, int cpu)
{
	unsigned long *p;

	p = arch_alloc_vmap_stack(SDEI_STACK_SIZE, cpu_to_node(cpu));
	if (!p)
		return -ENOMEM;
	per_cpu(*ptr, cpu) = p;

	return 0;
}

static int init_sdei_stacks(void)
{
	int cpu;
	int err = 0;

	for_each_possible_cpu(cpu) {
		err = _init_sdei_stack(&sdei_stack_normal_ptr, cpu);
		if (err)
			break;
		err = _init_sdei_stack(&sdei_stack_critical_ptr, cpu);
		if (err)
			break;
	}

	if (err)
		free_sdei_stacks();

	return err;
}

static bool on_sdei_normal_stack(unsigned long sp, struct stack_info *info)
{
	unsigned long low = (unsigned long)raw_cpu_read(sdei_stack_normal_ptr);
	unsigned long high = low + SDEI_STACK_SIZE;

	if (!low)
		return false;

	if (sp < low || sp >= high)
		return false;

	if (info) {
		info->low = low;
		info->high = high;
		info->type = STACK_TYPE_SDEI_NORMAL;
	}

	return true;
}

static bool on_sdei_critical_stack(unsigned long sp, struct stack_info *info)
{
	unsigned long low = (unsigned long)raw_cpu_read(sdei_stack_critical_ptr);
	unsigned long high = low + SDEI_STACK_SIZE;

	if (!low)
		return false;

	if (sp < low || sp >= high)
		return false;

	if (info) {
		info->low = low;
		info->high = high;
		info->type = STACK_TYPE_SDEI_CRITICAL;
	}

	return true;
}

bool _on_sdei_stack(unsigned long sp, struct stack_info *info)
{
	if (!IS_ENABLED(CONFIG_VMAP_STACK))
		return false;

	if (on_sdei_critical_stack(sp, info))
		return true;

	if (on_sdei_normal_stack(sp, info))
		return true;

	return false;
}

unsigned long sdei_arch_get_entry_point(int conduit)
{
	/*
	 * SDEI works between adjacent exception levels. If we booted at EL1 we
	 * assume a hypervisor is marshalling events. If we booted at EL2 and
	 * dropped to EL1 because we don't support VHE, then we can't support
	 * SDEI.
	 */
	if (is_hyp_mode_available() && !is_kernel_in_hyp_mode()) {
		pr_err("Not supported on this hardware/boot configuration\n");
		return 0;
	}

	if (IS_ENABLED(CONFIG_VMAP_STACK)) {
		if (init_sdei_stacks())
			return 0;
	}

	sdei_exit_mode = (conduit == SMCCC_CONDUIT_HVC) ? SDEI_EXIT_HVC : SDEI_EXIT_SMC;

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
	if (arm64_kernel_unmapped_at_el0()) {
		unsigned long offset;

		offset = (unsigned long)__sdei_asm_entry_trampoline -
			 (unsigned long)__entry_tramp_text_start;
		return TRAMP_VALIAS + offset;
	} else
#endif /* CONFIG_UNMAP_KERNEL_AT_EL0 */
		return (unsigned long)__sdei_asm_handler;

}

/*
 * __sdei_handler() returns one of:
 *  SDEI_EV_HANDLED -  success, return to the interrupted context.
 *  SDEI_EV_FAILED  -  failure, return this error code to firmare.
 *  virtual-address -  success, return to this address.
 */
static __kprobes unsigned long _sdei_handler(struct pt_regs *regs,
					     struct sdei_registered_event *arg)
{
	u32 mode;
	int i, err = 0;
	int clobbered_registers = 4;
	u64 elr = read_sysreg(elr_el1);
	u32 kernel_mode = read_sysreg(CurrentEL) | 1;	/* +SPSel */
	unsigned long vbar = read_sysreg(vbar_el1);

	if (arm64_kernel_unmapped_at_el0())
		clobbered_registers++;

	/* Retrieve the missing registers values */
	for (i = 0; i < clobbered_registers; i++) {
		/* from within the handler, this call always succeeds */
		sdei_api_event_context(i, &regs->regs[i]);
	}

	/*
	 * We didn't take an exception to get here, set PAN. UAO will be cleared
	 * by sdei_event_handler()s set_fs(USER_DS) call.
	 */
	__uaccess_enable_hw_pan();

	err = sdei_event_handler(regs, arg);
	if (err)
		return SDEI_EV_FAILED;

	if (elr != read_sysreg(elr_el1)) {
		/*
		 * We took a synchronous exception from the SDEI handler.
		 * This could deadlock, and if you interrupt KVM it will
		 * hyp-panic instead.
		 */
		pr_warn("unsafe: exception during handler\n");
	}

	mode = regs->pstate & (PSR_MODE32_BIT | PSR_MODE_MASK);

	/*
	 * If we interrupted the kernel with interrupts masked, we always go
	 * back to wherever we came from.
	 */
	if (mode == kernel_mode && !interrupts_enabled(regs))
		return SDEI_EV_HANDLED;

	/*
	 * Otherwise, we pretend this was an IRQ. This lets user space tasks
	 * receive signals before we return to them, and KVM to invoke it's
	 * world switch to do the same.
	 *
	 * See DDI0487B.a Table D1-7 'Vector offsets from vector table base
	 * address'.
	 */
	if (mode == kernel_mode)
		return vbar + 0x280;
	else if (mode & PSR_MODE32_BIT)
		return vbar + 0x680;

	return vbar + 0x480;
}


asmlinkage __kprobes notrace unsigned long
__sdei_handler(struct pt_regs *regs, struct sdei_registered_event *arg)
{
	unsigned long ret;
	bool do_nmi_exit = false;

	/*
	 * nmi_enter() deals with printk() re-entrance and use of RCU when
	 * RCU believed this CPU was idle. Because critical events can
	 * interrupt normal events, we may already be in_nmi().
	 */
	if (!in_nmi()) {
		nmi_enter();
		do_nmi_exit = true;
	}

	ret = _sdei_handler(regs, arg);

	if (do_nmi_exit)
		nmi_exit();

	return ret;
}
