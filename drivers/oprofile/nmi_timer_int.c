// SPDX-License-Identifier: GPL-2.0
/**
 * @file nmi_timer_int.c
 *
 * @remark Copyright 2011 Advanced Micro Devices, Inc.
 *
 * @author Robert Richter <robert.richter@amd.com>
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/oprofile.h>
#include <linux/perf_event.h>

#ifdef CONFIG_OPROFILE_NMI_TIMER

static DEFINE_PER_CPU(struct perf_event *, nmi_timer_events);
static int ctr_running;

static struct perf_event_attr nmi_timer_attr = {
	.type           = PERF_TYPE_HARDWARE,
	.config         = PERF_COUNT_HW_CPU_CYCLES,
	.size           = sizeof(struct perf_event_attr),
	.pinned         = 1,
	.disabled       = 1,
};

static void nmi_timer_callback(struct perf_event *event,
			       struct perf_sample_data *data,
			       struct pt_regs *regs)
{
	event->hw.interrupts = 0;       /* don't throttle interrupts */
	oprofile_add_sample(regs, 0);
}

static int nmi_timer_start_cpu(int cpu)
{
	struct perf_event *event = per_cpu(nmi_timer_events, cpu);

	if (!event) {
		event = perf_event_create_kernel_counter(&nmi_timer_attr, cpu, NULL,
							 nmi_timer_callback, NULL);
		if (IS_ERR(event))
			return PTR_ERR(event);
		per_cpu(nmi_timer_events, cpu) = event;
	}

	if (event && ctr_running)
		perf_event_enable(event);

	return 0;
}

static void nmi_timer_stop_cpu(int cpu)
{
	struct perf_event *event = per_cpu(nmi_timer_events, cpu);

	if (event && ctr_running)
		perf_event_disable(event);
}

static int nmi_timer_cpu_online(unsigned int cpu)
{
	nmi_timer_start_cpu(cpu);
	return 0;
}
static int nmi_timer_cpu_predown(unsigned int cpu)
{
	nmi_timer_stop_cpu(cpu);
	return 0;
}

static int nmi_timer_start(void)
{
	int cpu;

	get_online_cpus();
	ctr_running = 1;
	for_each_online_cpu(cpu)
		nmi_timer_start_cpu(cpu);
	put_online_cpus();

	return 0;
}

static void nmi_timer_stop(void)
{
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		nmi_timer_stop_cpu(cpu);
	ctr_running = 0;
	put_online_cpus();
}

static enum cpuhp_state hp_online;

static void nmi_timer_shutdown(void)
{
	struct perf_event *event;
	int cpu;

	cpuhp_remove_state(hp_online);
	for_each_possible_cpu(cpu) {
		event = per_cpu(nmi_timer_events, cpu);
		if (!event)
			continue;
		perf_event_disable(event);
		per_cpu(nmi_timer_events, cpu) = NULL;
		perf_event_release_kernel(event);
	}
}

static int nmi_timer_setup(void)
{
	int err;
	u64 period;

	/* clock cycles per tick: */
	period = (u64)cpu_khz * 1000;
	do_div(period, HZ);
	nmi_timer_attr.sample_period = period;

	err = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "oprofile/nmi:online",
				nmi_timer_cpu_online, nmi_timer_cpu_predown);
	if (err < 0) {
		nmi_timer_shutdown();
		return err;
	}
	hp_online = err;
	return 0;
}

int __init op_nmi_timer_init(struct oprofile_operations *ops)
{
	int err = 0;

	err = nmi_timer_setup();
	if (err)
		return err;
	nmi_timer_shutdown();		/* only check, don't alloc */

	ops->create_files	= NULL;
	ops->setup		= nmi_timer_setup;
	ops->shutdown		= nmi_timer_shutdown;
	ops->start		= nmi_timer_start;
	ops->stop		= nmi_timer_stop;
	ops->cpu_type		= "timer";

	printk(KERN_INFO "oprofile: using NMI timer interrupt.\n");

	return 0;
}

#endif
