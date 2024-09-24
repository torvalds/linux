// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/tick.h>
#include <linux/percpu-defs.h>

#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/grant_table.h>
#include <xen/events.h>

#include <asm/cpufeatures.h>
#include <asm/msr-index.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>
#include <asm/fixmap.h>

#include "xen-ops.h"

static DEFINE_PER_CPU(u64, spec_ctrl);

void xen_arch_pre_suspend(void)
{
	xen_save_time_memory_area();

	if (xen_pv_domain())
		xen_pv_pre_suspend();
}

void xen_arch_post_suspend(int cancelled)
{
	if (xen_pv_domain())
		xen_pv_post_suspend(cancelled);
	else
		xen_hvm_post_suspend(cancelled);

	xen_restore_time_memory_area();
}

static void xen_vcpu_notify_restore(void *data)
{
	if (xen_pv_domain() && boot_cpu_has(X86_FEATURE_SPEC_CTRL))
		wrmsrl(MSR_IA32_SPEC_CTRL, this_cpu_read(spec_ctrl));

	/* Boot processor notified via generic timekeeping_resume() */
	if (smp_processor_id() == 0)
		return;

	tick_resume_local();
}

static void xen_vcpu_notify_suspend(void *data)
{
	u64 tmp;

	tick_suspend_local();

	if (xen_pv_domain() && boot_cpu_has(X86_FEATURE_SPEC_CTRL)) {
		rdmsrl(MSR_IA32_SPEC_CTRL, tmp);
		this_cpu_write(spec_ctrl, tmp);
		wrmsrl(MSR_IA32_SPEC_CTRL, 0);
	}
}

void xen_arch_resume(void)
{
	int cpu;

	on_each_cpu(xen_vcpu_notify_restore, NULL, 1);

	for_each_online_cpu(cpu)
		xen_pmu_init(cpu);
}

void xen_arch_suspend(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		xen_pmu_finish(cpu);

	on_each_cpu(xen_vcpu_notify_suspend, NULL, 1);
}
