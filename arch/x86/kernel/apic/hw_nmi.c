// SPDX-License-Identifier: GPL-2.0
/*
 *  HW NMI watchdog support
 *
 *  started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 *  Arch specific calls to support NMI watchdog
 *
 *  Bits copied from original nmi.c file
 *
 */
#include <linux/thread_info.h>
#include <asm/apic.h>
#include <asm/nmi.h>

#include <linux/cpumask.h>
#include <linux/kdebug.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/delay.h>

#include "local.h"

#ifdef CONFIG_HARDLOCKUP_DETECTOR_PERF
u64 hw_nmi_get_sample_period(int watchdog_thresh)
{
	return (u64)(cpu_khz) * 1000 * watchdog_thresh;
}
#endif

#ifdef arch_trigger_cpumask_backtrace
static void nmi_raise_cpu_backtrace(cpumask_t *mask)
{
	__apic_send_IPI_mask(mask, NMI_VECTOR);
}

void arch_trigger_cpumask_backtrace(const cpumask_t *mask, int exclude_cpu)
{
	nmi_trigger_cpumask_backtrace(mask, exclude_cpu,
				      nmi_raise_cpu_backtrace);
}

static int nmi_cpu_backtrace_handler(unsigned int cmd, struct pt_regs *regs)
{
	if (nmi_cpu_backtrace(regs))
		return NMI_HANDLED;

	return NMI_DONE;
}
NOKPROBE_SYMBOL(nmi_cpu_backtrace_handler);

static int __init register_nmi_cpu_backtrace_handler(void)
{
	register_nmi_handler(NMI_LOCAL, nmi_cpu_backtrace_handler,
				0, "arch_bt");
	return 0;
}
early_initcall(register_nmi_cpu_backtrace_handler);
#endif
