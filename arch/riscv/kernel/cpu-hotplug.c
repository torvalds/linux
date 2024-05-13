// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/cpuhotplug.h>
#include <linux/cpu.h>
#include <linux/sched/hotplug.h>
#include <asm/irq.h>
#include <asm/cpu_ops.h>
#include <asm/numa.h>
#include <asm/smp.h>

bool cpu_has_hotplug(unsigned int cpu)
{
	if (cpu_ops[cpu]->cpu_stop)
		return true;

	return false;
}

/*
 * __cpu_disable runs on the processor to be shutdown.
 */
int __cpu_disable(void)
{
	int ret = 0;
	unsigned int cpu = smp_processor_id();

	if (!cpu_ops[cpu] || !cpu_ops[cpu]->cpu_stop)
		return -EOPNOTSUPP;

	if (cpu_ops[cpu]->cpu_disable)
		ret = cpu_ops[cpu]->cpu_disable(cpu);

	if (ret)
		return ret;

	remove_cpu_topology(cpu);
	numa_remove_cpu(cpu);
	set_cpu_online(cpu, false);
	riscv_ipi_disable();
	irq_migrate_all_off_this_cpu();

	return ret;
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * Called on the thread which is asking for a CPU to be shutdown, if the
 * CPU reported dead to the hotplug core.
 */
void arch_cpuhp_cleanup_dead_cpu(unsigned int cpu)
{
	int ret = 0;

	pr_notice("CPU%u: off\n", cpu);

	/* Verify from the firmware if the cpu is really stopped*/
	if (cpu_ops[cpu]->cpu_is_stopped)
		ret = cpu_ops[cpu]->cpu_is_stopped(cpu);
	if (ret)
		pr_warn("CPU%d may not have stopped: %d\n", cpu, ret);
}

/*
 * Called from the idle thread for the CPU which has been shutdown.
 */
void __noreturn arch_cpu_idle_dead(void)
{
	idle_task_exit();

	cpuhp_ap_report_dead();

	cpu_ops[smp_processor_id()]->cpu_stop();
	/* It should never reach here */
	BUG();
}
#endif
