/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/* This version must match the gator daemon version */
#define PROTOCOL_VERSION 21
static unsigned long gator_protocol_version = PROTOCOL_VERSION;

#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/utsname.h>
#include <linux/kthread.h>
#include <asm/stacktrace.h>
#include <linux/uaccess.h>

#include "gator.h"
#include "gator_src_md5.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
#error kernels prior to 2.6.32 are not supported
#endif

#if defined(MODULE) && !defined(CONFIG_MODULES)
#error Cannot build a module against a kernel that does not support modules. To resolve, either rebuild the kernel to support modules or build gator as part of the kernel.
#endif

#if !defined(CONFIG_GENERIC_TRACER) && !defined(CONFIG_TRACING)
#error gator requires the kernel to have CONFIG_GENERIC_TRACER or CONFIG_TRACING defined
#endif

#ifndef CONFIG_PROFILING
#error gator requires the kernel to have CONFIG_PROFILING defined
#endif

#ifndef CONFIG_HIGH_RES_TIMERS
#error gator requires the kernel to have CONFIG_HIGH_RES_TIMERS defined to support PC sampling
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0) && defined(__arm__) && defined(CONFIG_SMP) && !defined(CONFIG_LOCAL_TIMERS)
#error gator requires the kernel to have CONFIG_LOCAL_TIMERS defined on SMP systems
#endif

#if (GATOR_PERF_SUPPORT) && (!(GATOR_PERF_PMU_SUPPORT))
#ifndef CONFIG_PERF_EVENTS
#error gator requires the kernel to have CONFIG_PERF_EVENTS defined to support pmu hardware counters
#elif !defined CONFIG_HW_PERF_EVENTS
#error gator requires the kernel to have CONFIG_HW_PERF_EVENTS defined to support pmu hardware counters
#endif
#endif

/******************************************************************************
 * DEFINES
 ******************************************************************************/
#define SUMMARY_BUFFER_SIZE       (1*1024)
#define BACKTRACE_BUFFER_SIZE     (128*1024)
#define NAME_BUFFER_SIZE          (64*1024)
#define COUNTER_BUFFER_SIZE       (64*1024)	/* counters have the core as part of the data and the core value in the frame header may be discarded */
#define BLOCK_COUNTER_BUFFER_SIZE (128*1024)
#define ANNOTATE_BUFFER_SIZE      (128*1024)	/* annotate counters have the core as part of the data and the core value in the frame header may be discarded */
#define SCHED_TRACE_BUFFER_SIZE   (128*1024)
#define IDLE_BUFFER_SIZE          (32*1024)	/* idle counters have the core as part of the data and the core value in the frame header may be discarded */
#define ACTIVITY_BUFFER_SIZE      (128*1024)

#define NO_COOKIE      0U
#define UNRESOLVED_COOKIE ~0U

#define FRAME_SUMMARY       1
#define FRAME_BACKTRACE     2
#define FRAME_NAME          3
#define FRAME_COUNTER       4
#define FRAME_BLOCK_COUNTER 5
#define FRAME_ANNOTATE      6
#define FRAME_SCHED_TRACE   7
#define FRAME_IDLE          9
#define FRAME_ACTIVITY     13

#define MESSAGE_END_BACKTRACE 1

/* Name Frame Messages */
#define MESSAGE_COOKIE      1
#define MESSAGE_THREAD_NAME 2

/* Scheduler Trace Frame Messages */
#define MESSAGE_SCHED_SWITCH 1
#define MESSAGE_SCHED_EXIT   2

/* Summary Frame Messages */
#define MESSAGE_SUMMARY   1
#define MESSAGE_CORE_NAME 3

/* Activity Frame Messages */
#define MESSAGE_LINK   1
#define MESSAGE_SWITCH 2
#define MESSAGE_EXIT   3

#define MAXSIZE_PACK32     5
#define MAXSIZE_PACK64    10

#define FRAME_HEADER_SIZE 3

#if defined(__arm__)
#define PC_REG regs->ARM_pc
#elif defined(__aarch64__)
#define PC_REG regs->pc
#else
#define PC_REG regs->ip
#endif

enum {
	SUMMARY_BUF,
	BACKTRACE_BUF,
	NAME_BUF,
	COUNTER_BUF,
	BLOCK_COUNTER_BUF,
	ANNOTATE_BUF,
	SCHED_TRACE_BUF,
	IDLE_BUF,
	ACTIVITY_BUF,
	NUM_GATOR_BUFS
};

/******************************************************************************
 * Globals
 ******************************************************************************/
static unsigned long gator_cpu_cores;
/* Size of the largest buffer. Effectively constant, set in gator_op_create_files */
static unsigned long userspace_buffer_size;
static unsigned long gator_backtrace_depth;
/* How often to commit the buffers for live in nanoseconds */
static u64 gator_live_rate;

static unsigned long gator_started;
static u64 gator_monotonic_started;
static u64 gator_sync_time;
static u64 gator_hibernate_time;
static unsigned long gator_buffer_opened;
static unsigned long gator_timer_count;
static unsigned long gator_response_type;
static DEFINE_MUTEX(start_mutex);
static DEFINE_MUTEX(gator_buffer_mutex);

bool event_based_sampling;

static DECLARE_WAIT_QUEUE_HEAD(gator_buffer_wait);
static DECLARE_WAIT_QUEUE_HEAD(gator_annotate_wait);
static struct timer_list gator_buffer_wake_up_timer;
static bool gator_buffer_wake_run;
/* Initialize semaphore unlocked to initialize memory values */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
static DECLARE_MUTEX(gator_buffer_wake_sem);
#else
static DEFINE_SEMAPHORE(gator_buffer_wake_sem);
#endif
static struct task_struct *gator_buffer_wake_thread;
static LIST_HEAD(gator_events);

static DEFINE_PER_CPU(u64, last_timestamp);

static bool printed_monotonic_warning;

static u32 gator_cpuids[NR_CPUS];
static bool sent_core_name[NR_CPUS];

static DEFINE_PER_CPU(bool, in_scheduler_context);

/******************************************************************************
 * Prototypes
 ******************************************************************************/
static u64 gator_get_time(void);
static void gator_emit_perf_time(u64 time);
static void gator_op_create_files(struct super_block *sb, struct dentry *root);

/* gator_buffer is protected by being per_cpu and by having IRQs
 * disabled when writing to it. Most marshal_* calls take care of this
 * except for marshal_cookie*, marshal_backtrace* and marshal_frame
 * where the caller is responsible for doing so. No synchronization is
 * needed with the backtrace buffer as it is per cpu and is only used
 * from the hrtimer. The annotate_lock must be held when using the
 * annotation buffer as it is not per cpu. collect_counters which is
 * the sole writer to the block counter frame is additionally
 * protected by the per cpu collecting flag.
 */

/* Size of the buffer, must be a power of 2. Effectively constant, set in gator_op_setup. */
static uint32_t gator_buffer_size[NUM_GATOR_BUFS];
/* gator_buffer_size - 1, bitwise and with pos to get offset into the array. Effectively constant, set in gator_op_setup. */
static uint32_t gator_buffer_mask[NUM_GATOR_BUFS];
/* Read position in the buffer. Initialized to zero in gator_op_setup and incremented after bytes are read by userspace in userspace_buffer_read */
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_read);
/* Write position in the buffer. Initialized to zero in gator_op_setup and incremented after bytes are written to the buffer */
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_write);
/* Commit position in the buffer. Initialized to zero in gator_op_setup and incremented after a frame is ready to be read by userspace */
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_commit);
/* If set to false, decreases the number of bytes returned by
 * buffer_bytes_available. Set in buffer_check_space if no space is
 * remaining. Initialized to true in gator_op_setup. This means that
 * if we run out of space, continue to report that no space is
 * available until bytes are read by userspace
 */
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], buffer_space_available);
/* The buffer. Allocated in gator_op_setup */
static DEFINE_PER_CPU(char *[NUM_GATOR_BUFS], gator_buffer);
/* The time after which the buffer should be committed for live display */
static DEFINE_PER_CPU(u64, gator_buffer_commit_time);

/* List of all gator events - new events must be added to this list */
#define GATOR_EVENTS_LIST \
	GATOR_EVENT(gator_events_armv6_init) \
	GATOR_EVENT(gator_events_armv7_init) \
	GATOR_EVENT(gator_events_block_init) \
	GATOR_EVENT(gator_events_ccn504_init) \
	GATOR_EVENT(gator_events_irq_init) \
	GATOR_EVENT(gator_events_l2c310_init) \
	GATOR_EVENT(gator_events_mali_init) \
	GATOR_EVENT(gator_events_mali_midgard_hw_init) \
	GATOR_EVENT(gator_events_mali_midgard_init) \
	GATOR_EVENT(gator_events_meminfo_init) \
	GATOR_EVENT(gator_events_mmapped_init) \
	GATOR_EVENT(gator_events_net_init) \
	GATOR_EVENT(gator_events_perf_pmu_init) \
	GATOR_EVENT(gator_events_sched_init) \
	GATOR_EVENT(gator_events_scorpion_init) \

#define GATOR_EVENT(EVENT_INIT) __weak int EVENT_INIT(void);
GATOR_EVENTS_LIST
#undef GATOR_EVENT

static int (*gator_events_list[])(void) = {
#define GATOR_EVENT(EVENT_INIT) EVENT_INIT,
GATOR_EVENTS_LIST
#undef GATOR_EVENT
};

/******************************************************************************
 * Application Includes
 ******************************************************************************/
#include "gator_fs.c"
#include "gator_buffer_write.c"
#include "gator_buffer.c"
#include "gator_marshaling.c"
#include "gator_hrtimer_gator.c"
#include "gator_cookies.c"
#include "gator_annotate.c"
#include "gator_trace_sched.c"
#include "gator_trace_power.c"
#include "gator_trace_gpu.c"
#include "gator_backtrace.c"

/******************************************************************************
 * Misc
 ******************************************************************************/

MODULE_PARM_DESC(gator_src_md5, "Gator driver source code md5sum");
module_param_named(src_md5, gator_src_md5, charp, 0444);

static const struct gator_cpu gator_cpus[] = {
	{
		.cpuid = ARM1136,
		.core_name = "ARM1136",
		.pmnc_name = "ARM_ARM11",
		.dt_name = "arm,arm1136",
		.pmnc_counters = 3,
	},
	{
		.cpuid = ARM1156,
		.core_name = "ARM1156",
		.pmnc_name = "ARM_ARM11",
		.dt_name = "arm,arm1156",
		.pmnc_counters = 3,
	},
	{
		.cpuid = ARM1176,
		.core_name = "ARM1176",
		.pmnc_name = "ARM_ARM11",
		.dt_name = "arm,arm1176",
		.pmnc_counters = 3,
	},
	{
		.cpuid = ARM11MPCORE,
		.core_name = "ARM11MPCore",
		.pmnc_name = "ARM_ARM11MPCore",
		.dt_name = "arm,arm11mpcore",
		.pmnc_counters = 3,
	},
	{
		.cpuid = CORTEX_A5,
		.core_name = "Cortex-A5",
		.pmnc_name = "ARMv7_Cortex_A5",
		.dt_name = "arm,cortex-a5",
		.pmnc_counters = 2,
	},
	{
		.cpuid = CORTEX_A7,
		.core_name = "Cortex-A7",
		.pmnc_name = "ARMv7_Cortex_A7",
		.dt_name = "arm,cortex-a7",
		.pmnc_counters = 4,
	},
	{
		.cpuid = CORTEX_A8,
		.core_name = "Cortex-A8",
		.pmnc_name = "ARMv7_Cortex_A8",
		.dt_name = "arm,cortex-a8",
		.pmnc_counters = 4,
	},
	{
		.cpuid = CORTEX_A9,
		.core_name = "Cortex-A9",
		.pmnc_name = "ARMv7_Cortex_A9",
		.dt_name = "arm,cortex-a9",
		.pmnc_counters = 6,
	},
	{
		.cpuid = CORTEX_A15,
		.core_name = "Cortex-A15",
		.pmnc_name = "ARMv7_Cortex_A15",
		.dt_name = "arm,cortex-a15",
		.pmnc_counters = 6,
	},
	{
		.cpuid = CORTEX_A12,
		.core_name = "Cortex-A17",
		.pmnc_name = "ARMv7_Cortex_A17",
		.dt_name = "arm,cortex-a17",
		.pmnc_counters = 6,
	},
	{
		.cpuid = CORTEX_A17,
		.core_name = "Cortex-A17",
		.pmnc_name = "ARMv7_Cortex_A17",
		.dt_name = "arm,cortex-a17",
		.pmnc_counters = 6,
	},
	{
		.cpuid = SCORPION,
		.core_name = "Scorpion",
		.pmnc_name = "Scorpion",
		.pmnc_counters = 4,
	},
	{
		.cpuid = SCORPIONMP,
		.core_name = "ScorpionMP",
		.pmnc_name = "ScorpionMP",
		.pmnc_counters = 4,
	},
	{
		.cpuid = KRAITSIM,
		.core_name = "KraitSIM",
		.pmnc_name = "Krait",
		.pmnc_counters = 4,
	},
	{
		.cpuid = KRAIT,
		.core_name = "Krait",
		.pmnc_name = "Krait",
		.pmnc_counters = 4,
	},
	{
		.cpuid = KRAIT_S4_PRO,
		.core_name = "Krait S4 Pro",
		.pmnc_name = "Krait",
		.pmnc_counters = 4,
	},
	{
		.cpuid = CORTEX_A53,
		.core_name = "Cortex-A53",
		.pmnc_name = "ARM_Cortex-A53",
		.dt_name = "arm,cortex-a53",
		.pmnc_counters = 6,
	},
	{
		.cpuid = CORTEX_A57,
		.core_name = "Cortex-A57",
		.pmnc_name = "ARM_Cortex-A57",
		.dt_name = "arm,cortex-a57",
		.pmnc_counters = 6,
	},
	{
		.cpuid = CORTEX_A72,
		.core_name = "Cortex-A72",
		.pmnc_name = "ARM_Cortex-A72",
		.dt_name = "arm,cortex-a72",
		.pmnc_counters = 6,
	},
	{
		.cpuid = OTHER,
		.core_name = "Other",
		.pmnc_name = "Other",
		.pmnc_counters = 6,
	},
	{}
};

const struct gator_cpu *gator_find_cpu_by_cpuid(const u32 cpuid)
{
	int i;

	for (i = 0; gator_cpus[i].cpuid != 0; ++i) {
		const struct gator_cpu *const gator_cpu = &gator_cpus[i];

		if (gator_cpu->cpuid == cpuid)
			return gator_cpu;
	}

	return NULL;
}

static const char OLD_PMU_PREFIX[] = "ARMv7 Cortex-";
static const char NEW_PMU_PREFIX[] = "ARMv7_Cortex_";

const struct gator_cpu *gator_find_cpu_by_pmu_name(const char *const name)
{
	int i;

	for (i = 0; gator_cpus[i].cpuid != 0; ++i) {
		const struct gator_cpu *const gator_cpu = &gator_cpus[i];

		if (gator_cpu->pmnc_name != NULL &&
		    /* Do the names match exactly? */
		    (strcasecmp(gator_cpu->pmnc_name, name) == 0 ||
		     /* Do these names match but have the old vs new prefix? */
		     ((strncasecmp(name, OLD_PMU_PREFIX, sizeof(OLD_PMU_PREFIX) - 1) == 0 &&
		       strncasecmp(gator_cpu->pmnc_name, NEW_PMU_PREFIX, sizeof(NEW_PMU_PREFIX) - 1) == 0 &&
		       strcasecmp(name + sizeof(OLD_PMU_PREFIX) - 1, gator_cpu->pmnc_name + sizeof(NEW_PMU_PREFIX) - 1) == 0))))
			return gator_cpu;
	}

	return NULL;
}

u32 gator_cpuid(void)
{
#if defined(__arm__) || defined(__aarch64__)
	u32 val;
#if !defined(__aarch64__)
	asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r" (val));
#else
	asm volatile("mrs %0, midr_el1" : "=r" (val));
#endif
	return ((val & 0xff000000) >> 12) | ((val & 0xfff0) >> 4);
#else
	return OTHER;
#endif
}

static void gator_buffer_wake_up(unsigned long data)
{
	wake_up(&gator_buffer_wait);
}

static int gator_buffer_wake_func(void *data)
{
	for (;;) {
		if (down_killable(&gator_buffer_wake_sem))
			break;

		/* Eat up any pending events */
		while (!down_trylock(&gator_buffer_wake_sem))
			;

		if (!gator_buffer_wake_run)
			break;

		gator_buffer_wake_up(0);
	}

	return 0;
}

/******************************************************************************
 * Commit interface
 ******************************************************************************/
static bool buffer_commit_ready(int *cpu, int *buftype)
{
	int cpu_x, x;

	for_each_present_cpu(cpu_x) {
		for (x = 0; x < NUM_GATOR_BUFS; x++)
			if (per_cpu(gator_buffer_commit, cpu_x)[x] != per_cpu(gator_buffer_read, cpu_x)[x]) {
				*cpu = cpu_x;
				*buftype = x;
				return true;
			}
	}
	*cpu = -1;
	*buftype = -1;
	return false;
}

/******************************************************************************
 * hrtimer interrupt processing
 ******************************************************************************/
static void gator_timer_interrupt(void)
{
	struct pt_regs *const regs = get_irq_regs();

	gator_backtrace_handler(regs);
}

void gator_backtrace_handler(struct pt_regs *const regs)
{
	u64 time = gator_get_time();
	int cpu = get_physical_cpu();

	/* Output backtrace */
	gator_add_sample(cpu, regs, time);

	/* Collect counters */
	if (!per_cpu(collecting, cpu))
		collect_counters(time, current, false);

	/* No buffer flushing occurs during sched switch for RT-Preempt full. The block counter frame will be flushed by collect_counters, but the sched buffer needs to be explicitly flushed */
#ifdef CONFIG_PREEMPT_RT_FULL
	buffer_check(cpu, SCHED_TRACE_BUF, time);
#endif
}

static int gator_running;

/* This function runs in interrupt context and on the appropriate core */
static void gator_timer_offline(void *migrate)
{
	struct gator_interface *gi;
	int i, len, cpu = get_physical_cpu();
	int *buffer;
	u64 time;

	gator_trace_sched_offline();
	gator_trace_power_offline();

	if (!migrate)
		gator_hrtimer_offline();

	/* Offline any events and output counters */
	time = gator_get_time();
	if (marshal_event_header(time)) {
		list_for_each_entry(gi, &gator_events, list) {
			if (gi->offline) {
				len = gi->offline(&buffer, migrate);
				marshal_event(len, buffer);
			}
		}
		/* Only check after writing all counters so that time and corresponding counters appear in the same frame */
		buffer_check(cpu, BLOCK_COUNTER_BUF, time);
	}

	/* Flush all buffers on this core */
	for (i = 0; i < NUM_GATOR_BUFS; i++)
		gator_commit_buffer(cpu, i, time);
}

/* This function runs in interrupt context and may be running on a core other than core 'cpu' */
static void gator_timer_offline_dispatch(int cpu, bool migrate)
{
	struct gator_interface *gi;

	list_for_each_entry(gi, &gator_events, list) {
		if (gi->offline_dispatch)
			gi->offline_dispatch(cpu, migrate);
	}
}

static void gator_timer_stop(void)
{
	int cpu;

	if (gator_running) {
		on_each_cpu(gator_timer_offline, NULL, 1);
		for_each_online_cpu(cpu) {
			gator_timer_offline_dispatch(lcpu_to_pcpu(cpu), false);
		}

		gator_running = 0;
		gator_hrtimer_shutdown();
	}
}

static void gator_send_core_name(const int cpu, const u32 cpuid)
{
#if defined(__arm__) || defined(__aarch64__)
	if (!sent_core_name[cpu] || (cpuid != gator_cpuids[cpu])) {
		const struct gator_cpu *const gator_cpu = gator_find_cpu_by_cpuid(cpuid);
		const char *core_name = NULL;
		char core_name_buf[32];

		/* Save off this cpuid */
		gator_cpuids[cpu] = cpuid;
		if (gator_cpu != NULL) {
			core_name = gator_cpu->core_name;
		} else {
			if (cpuid == -1)
				snprintf(core_name_buf, sizeof(core_name_buf), "Unknown");
			else
				snprintf(core_name_buf, sizeof(core_name_buf), "Unknown (0x%.5x)", cpuid);
			core_name = core_name_buf;
		}

		marshal_core_name(cpu, cpuid, core_name);
		sent_core_name[cpu] = true;
	}
#endif
}

static void gator_read_cpuid(void *arg)
{
	gator_cpuids[get_physical_cpu()] = gator_cpuid();
}

/* This function runs in interrupt context and on the appropriate core */
static void gator_timer_online(void *migrate)
{
	struct gator_interface *gi;
	int len, cpu = get_physical_cpu();
	int *buffer;
	u64 time;

	/* Send what is currently running on this core */
	marshal_sched_trace_switch(current->pid, 0);

	gator_trace_power_online();

	/* online any events and output counters */
	time = gator_get_time();
	if (marshal_event_header(time)) {
		list_for_each_entry(gi, &gator_events, list) {
			if (gi->online) {
				len = gi->online(&buffer, migrate);
				marshal_event(len, buffer);
			}
		}
		/* Only check after writing all counters so that time and corresponding counters appear in the same frame */
		buffer_check(cpu, BLOCK_COUNTER_BUF, time);
	}

	if (!migrate)
		gator_hrtimer_online();

	gator_send_core_name(cpu, gator_cpuid());
}

/* This function runs in interrupt context and may be running on a core other than core 'cpu' */
static void gator_timer_online_dispatch(int cpu, bool migrate)
{
	struct gator_interface *gi;

	list_for_each_entry(gi, &gator_events, list) {
		if (gi->online_dispatch)
			gi->online_dispatch(cpu, migrate);
	}
}

#include "gator_iks.c"

static int gator_timer_start(unsigned long sample_rate)
{
	int cpu;

	if (gator_running) {
		pr_notice("gator: already running\n");
		return 0;
	}

	gator_running = 1;

	/* event based sampling trumps hr timer based sampling */
	if (event_based_sampling)
		sample_rate = 0;

	if (gator_hrtimer_init(sample_rate, gator_timer_interrupt) == -1)
		return -1;

	/* Send off the previously saved cpuids */
	for_each_present_cpu(cpu) {
		preempt_disable();
		gator_send_core_name(cpu, gator_cpuids[cpu]);
		preempt_enable();
	}

	gator_send_iks_core_names();
	for_each_online_cpu(cpu) {
		gator_timer_online_dispatch(lcpu_to_pcpu(cpu), false);
	}
	on_each_cpu(gator_timer_online, NULL, 1);

	return 0;
}

static u64 gator_get_time(void)
{
	struct timespec ts;
	u64 timestamp;
	u64 prev_timestamp;
	u64 delta;
	int cpu = smp_processor_id();

	/* Match clock_gettime(CLOCK_MONOTONIC_RAW, &ts) from userspace */
	getrawmonotonic(&ts);
	timestamp = timespec_to_ns(&ts);

	/* getrawmonotonic is not monotonic on all systems. Detect and
	 * attempt to correct these cases. up to 0.5ms delta has been seen
	 * on some systems, which can skew Streamline data when viewing at
	 * high resolution. This doesn't work well with interrupts, but that
	 * it's OK - the real concern is to catch big jumps in time
	 */
	prev_timestamp = per_cpu(last_timestamp, cpu);
	if (prev_timestamp <= timestamp) {
		per_cpu(last_timestamp, cpu) = timestamp;
	} else {
		delta = prev_timestamp - timestamp;
		/* Log the error once */
		if (!printed_monotonic_warning && delta > 500000) {
			pr_err("%s: getrawmonotonic is not monotonic  cpu: %i  delta: %lli\nSkew in Streamline data may be present at the fine zoom levels\n", __func__, cpu, delta);
			printed_monotonic_warning = true;
		}
		timestamp = prev_timestamp;
	}

	return timestamp - gator_monotonic_started;
}

static void gator_emit_perf_time(u64 time)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
	if (time >= gator_sync_time) {
		marshal_event_single64(0, -1, local_clock());
		gator_sync_time += NSEC_PER_SEC;
		if (gator_live_rate <= 0) {
			gator_commit_buffer(get_physical_cpu(), COUNTER_BUF, time);
		}
	}
#endif
}

/******************************************************************************
 * cpu hotplug and pm notifiers
 ******************************************************************************/
static int __cpuinit gator_hotcpu_notify(struct notifier_block *self, unsigned long action, void *hcpu)
{
	int cpu = lcpu_to_pcpu((long)hcpu);

	switch (action) {
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		smp_call_function_single(cpu, gator_timer_offline, NULL, 1);
		gator_timer_offline_dispatch(cpu, false);
		break;
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		gator_timer_online_dispatch(cpu, false);
		smp_call_function_single(cpu, gator_timer_online, NULL, 1);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __refdata gator_hotcpu_notifier = {
	.notifier_call = gator_hotcpu_notify,
};

/* n.b. calling "on_each_cpu" only runs on those that are online.
 * Registered linux events are not disabled, so their counters will
 * continue to collect
 */
static int gator_pm_notify(struct notifier_block *nb, unsigned long event, void *dummy)
{
	int cpu;
	struct timespec ts;

	switch (event) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		unregister_hotcpu_notifier(&gator_hotcpu_notifier);
		unregister_scheduler_tracepoints();
		on_each_cpu(gator_timer_offline, NULL, 1);
		for_each_online_cpu(cpu) {
			gator_timer_offline_dispatch(lcpu_to_pcpu(cpu), false);
		}

		/* Record the wallclock hibernate time */
		getnstimeofday(&ts);
		gator_hibernate_time = timespec_to_ns(&ts) - gator_get_time();
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		/* Adjust gator_monotonic_started for the time spent sleeping, as gator_get_time does not account for it */
		if (gator_hibernate_time > 0) {
			getnstimeofday(&ts);
			gator_monotonic_started += gator_hibernate_time + gator_get_time() - timespec_to_ns(&ts);
			gator_hibernate_time = 0;
		}

		for_each_online_cpu(cpu) {
			gator_timer_online_dispatch(lcpu_to_pcpu(cpu), false);
		}
		on_each_cpu(gator_timer_online, NULL, 1);
		register_scheduler_tracepoints();
		register_hotcpu_notifier(&gator_hotcpu_notifier);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block gator_pm_notifier = {
	.notifier_call = gator_pm_notify,
};

static int gator_notifier_start(void)
{
	int retval;

	retval = register_hotcpu_notifier(&gator_hotcpu_notifier);
	if (retval == 0)
		retval = register_pm_notifier(&gator_pm_notifier);
	return retval;
}

static void gator_notifier_stop(void)
{
	unregister_pm_notifier(&gator_pm_notifier);
	unregister_hotcpu_notifier(&gator_hotcpu_notifier);
}

/******************************************************************************
 * Main
 ******************************************************************************/
static void gator_summary(void)
{
	u64 timestamp, uptime;
	struct timespec ts;
	char uname_buf[512];

	snprintf(uname_buf, sizeof(uname_buf), "%s %s %s %s %s GNU/Linux", utsname()->sysname, utsname()->nodename, utsname()->release, utsname()->version, utsname()->machine);

	getnstimeofday(&ts);
	timestamp = timespec_to_ns(&ts);

	/* Similar to reading /proc/uptime from fs/proc/uptime.c, calculate uptime */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
	{
		void (*m2b)(struct timespec *ts);

		do_posix_clock_monotonic_gettime(&ts);
		/* monotonic_to_bootbased is not defined for some versions of Android */
		m2b = symbol_get(monotonic_to_bootbased);
		if (m2b)
			m2b(&ts);
	}
#else
	get_monotonic_boottime(&ts);
#endif
	uptime = timespec_to_ns(&ts);

	/* Disable preemption as gator_get_time calls smp_processor_id to verify time is monotonic */
	preempt_disable();
	/* Set monotonic_started to zero as gator_get_time is uptime minus monotonic_started */
	gator_monotonic_started = 0;
	gator_monotonic_started = gator_get_time();

	marshal_summary(timestamp, uptime, gator_monotonic_started, uname_buf);
	gator_sync_time = 0;
	gator_emit_perf_time(gator_monotonic_started);
	/* Always flush COUNTER_BUF so that the initial perf_time is received before it's used */
	gator_commit_buffer(get_physical_cpu(), COUNTER_BUF, 0);
	preempt_enable();
}

int gator_events_install(struct gator_interface *interface)
{
	list_add_tail(&interface->list, &gator_events);

	return 0;
}

int gator_events_get_key(void)
{
	/* key 0 is reserved as a timestamp. key 1 is reserved as the marker
	 * for thread specific counters. key 2 is reserved as the marker for
	 * core. Odd keys are assigned by the driver, even keys by the
	 * daemon.
	 */
	static int key = 3;
	const int ret = key;

	key += 2;
	return ret;
}

static int gator_init(void)
{
	int i;

	calc_first_cluster_size();

	/* events sources */
	for (i = 0; i < ARRAY_SIZE(gator_events_list); i++)
		if (gator_events_list[i])
			gator_events_list[i]();

	gator_trace_sched_init();
	gator_trace_power_init();

	return 0;
}

static void gator_exit(void)
{
	struct gator_interface *gi;

	list_for_each_entry(gi, &gator_events, list)
		if (gi->shutdown)
			gi->shutdown();
}

static int gator_start(void)
{
	unsigned long cpu, i;
	struct gator_interface *gi;

	gator_buffer_wake_run = true;
	gator_buffer_wake_thread = kthread_run(gator_buffer_wake_func, NULL, "gator_bwake");
	if (IS_ERR(gator_buffer_wake_thread))
		goto bwake_failure;

	if (gator_migrate_start())
		goto migrate_failure;

	/* Initialize the buffer with the frame type and core */
	for_each_present_cpu(cpu) {
		for (i = 0; i < NUM_GATOR_BUFS; i++)
			marshal_frame(cpu, i);
		per_cpu(last_timestamp, cpu) = 0;
	}
	printed_monotonic_warning = false;

	/* Capture the start time */
	gator_summary();

	/* start all events */
	list_for_each_entry(gi, &gator_events, list) {
		if (gi->start && gi->start() != 0) {
			struct list_head *ptr = gi->list.prev;

			while (ptr != &gator_events) {
				gi = list_entry(ptr, struct gator_interface, list);

				if (gi->stop)
					gi->stop();

				ptr = ptr->prev;
			}
			goto events_failure;
		}
	}

	/* cookies shall be initialized before trace_sched_start() and gator_timer_start() */
	if (cookies_initialize())
		goto cookies_failure;
	if (gator_annotate_start())
		goto annotate_failure;
	if (gator_trace_sched_start())
		goto sched_failure;
	if (gator_trace_power_start())
		goto power_failure;
	if (gator_trace_gpu_start())
		goto gpu_failure;
	if (gator_timer_start(gator_timer_count))
		goto timer_failure;
	if (gator_notifier_start())
		goto notifier_failure;

	return 0;

notifier_failure:
	gator_timer_stop();
timer_failure:
	gator_trace_gpu_stop();
gpu_failure:
	gator_trace_power_stop();
power_failure:
	gator_trace_sched_stop();
sched_failure:
	gator_annotate_stop();
annotate_failure:
	cookies_release();
cookies_failure:
	/* stop all events */
	list_for_each_entry(gi, &gator_events, list)
		if (gi->stop)
			gi->stop();
events_failure:
	gator_migrate_stop();
migrate_failure:
	gator_buffer_wake_run = false;
	up(&gator_buffer_wake_sem);
	gator_buffer_wake_thread = NULL;
bwake_failure:

	return -1;
}

static void gator_stop(void)
{
	struct gator_interface *gi;

	gator_annotate_stop();
	gator_trace_sched_stop();
	gator_trace_power_stop();
	gator_trace_gpu_stop();

	/* stop all interrupt callback reads before tearing down other interfaces */
	gator_notifier_stop();	/* should be called before gator_timer_stop to avoid re-enabling the hrtimer after it has been offlined */
	gator_timer_stop();

	/* stop all events */
	list_for_each_entry(gi, &gator_events, list)
		if (gi->stop)
			gi->stop();

	gator_migrate_stop();

	gator_buffer_wake_run = false;
	up(&gator_buffer_wake_sem);
	gator_buffer_wake_thread = NULL;
}

/******************************************************************************
 * Filesystem
 ******************************************************************************/
/* fopen("buffer") */
static int gator_op_setup(void)
{
	int err = 0;
	int cpu, i;

	mutex_lock(&start_mutex);

	gator_buffer_size[SUMMARY_BUF] = SUMMARY_BUFFER_SIZE;
	gator_buffer_mask[SUMMARY_BUF] = SUMMARY_BUFFER_SIZE - 1;

	gator_buffer_size[BACKTRACE_BUF] = BACKTRACE_BUFFER_SIZE;
	gator_buffer_mask[BACKTRACE_BUF] = BACKTRACE_BUFFER_SIZE - 1;

	gator_buffer_size[NAME_BUF] = NAME_BUFFER_SIZE;
	gator_buffer_mask[NAME_BUF] = NAME_BUFFER_SIZE - 1;

	gator_buffer_size[COUNTER_BUF] = COUNTER_BUFFER_SIZE;
	gator_buffer_mask[COUNTER_BUF] = COUNTER_BUFFER_SIZE - 1;

	gator_buffer_size[BLOCK_COUNTER_BUF] = BLOCK_COUNTER_BUFFER_SIZE;
	gator_buffer_mask[BLOCK_COUNTER_BUF] = BLOCK_COUNTER_BUFFER_SIZE - 1;

	gator_buffer_size[ANNOTATE_BUF] = ANNOTATE_BUFFER_SIZE;
	gator_buffer_mask[ANNOTATE_BUF] = ANNOTATE_BUFFER_SIZE - 1;

	gator_buffer_size[SCHED_TRACE_BUF] = SCHED_TRACE_BUFFER_SIZE;
	gator_buffer_mask[SCHED_TRACE_BUF] = SCHED_TRACE_BUFFER_SIZE - 1;

	gator_buffer_size[IDLE_BUF] = IDLE_BUFFER_SIZE;
	gator_buffer_mask[IDLE_BUF] = IDLE_BUFFER_SIZE - 1;

	gator_buffer_size[ACTIVITY_BUF] = ACTIVITY_BUFFER_SIZE;
	gator_buffer_mask[ACTIVITY_BUF] = ACTIVITY_BUFFER_SIZE - 1;

	/* Initialize percpu per buffer variables */
	for (i = 0; i < NUM_GATOR_BUFS; i++) {
		/* Verify buffers are a power of 2 */
		if (gator_buffer_size[i] & (gator_buffer_size[i] - 1)) {
			err = -ENOEXEC;
			goto setup_error;
		}

		for_each_present_cpu(cpu) {
			per_cpu(gator_buffer_read, cpu)[i] = 0;
			per_cpu(gator_buffer_write, cpu)[i] = 0;
			per_cpu(gator_buffer_commit, cpu)[i] = 0;
			per_cpu(buffer_space_available, cpu)[i] = true;
			per_cpu(gator_buffer_commit_time, cpu) = gator_live_rate;

			/* Annotation is a special case that only uses a single buffer */
			if (cpu > 0 && i == ANNOTATE_BUF) {
				per_cpu(gator_buffer, cpu)[i] = NULL;
				continue;
			}

			per_cpu(gator_buffer, cpu)[i] = vmalloc(gator_buffer_size[i]);
			if (!per_cpu(gator_buffer, cpu)[i]) {
				err = -ENOMEM;
				goto setup_error;
			}
		}
	}

setup_error:
	mutex_unlock(&start_mutex);
	return err;
}

/* Actually start profiling (echo 1>/dev/gator/enable) */
static int gator_op_start(void)
{
	int err = 0;

	mutex_lock(&start_mutex);

	if (gator_started || gator_start())
		err = -EINVAL;
	else
		gator_started = 1;

	mutex_unlock(&start_mutex);

	return err;
}

/* echo 0>/dev/gator/enable */
static void gator_op_stop(void)
{
	mutex_lock(&start_mutex);

	if (gator_started) {
		gator_stop();

		mutex_lock(&gator_buffer_mutex);

		gator_started = 0;
		gator_monotonic_started = 0;
		cookies_release();
		wake_up(&gator_buffer_wait);

		mutex_unlock(&gator_buffer_mutex);
	}

	mutex_unlock(&start_mutex);
}

static void gator_shutdown(void)
{
	int cpu, i;

	mutex_lock(&start_mutex);

	for_each_present_cpu(cpu) {
		mutex_lock(&gator_buffer_mutex);
		for (i = 0; i < NUM_GATOR_BUFS; i++) {
			vfree(per_cpu(gator_buffer, cpu)[i]);
			per_cpu(gator_buffer, cpu)[i] = NULL;
			per_cpu(gator_buffer_read, cpu)[i] = 0;
			per_cpu(gator_buffer_write, cpu)[i] = 0;
			per_cpu(gator_buffer_commit, cpu)[i] = 0;
			per_cpu(buffer_space_available, cpu)[i] = true;
			per_cpu(gator_buffer_commit_time, cpu) = 0;
		}
		mutex_unlock(&gator_buffer_mutex);
	}

	memset(&sent_core_name, 0, sizeof(sent_core_name));

	mutex_unlock(&start_mutex);
}

static int gator_set_backtrace(unsigned long val)
{
	int err = 0;

	mutex_lock(&start_mutex);

	if (gator_started)
		err = -EBUSY;
	else
		gator_backtrace_depth = val;

	mutex_unlock(&start_mutex);

	return err;
}

static ssize_t enable_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return gatorfs_ulong_to_user(gator_started, buf, count, offset);
}

static ssize_t enable_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = gatorfs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;

	if (val)
		retval = gator_op_start();
	else
		gator_op_stop();

	if (retval)
		return retval;
	return count;
}

static const struct file_operations enable_fops = {
	.read = enable_read,
	.write = enable_write,
};

static int userspace_buffer_open(struct inode *inode, struct file *file)
{
	int err = -EPERM;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (test_and_set_bit_lock(0, &gator_buffer_opened))
		return -EBUSY;

	err = gator_op_setup();
	if (err)
		goto fail;

	/* NB: the actual start happens from userspace
	 * echo 1 >/dev/gator/enable
	 */

	return 0;

fail:
	__clear_bit_unlock(0, &gator_buffer_opened);
	return err;
}

static int userspace_buffer_release(struct inode *inode, struct file *file)
{
	gator_op_stop();
	gator_shutdown();
	__clear_bit_unlock(0, &gator_buffer_opened);
	return 0;
}

static ssize_t userspace_buffer_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	int commit, length1, length2, read;
	char *buffer1;
	char *buffer2;
	int cpu, buftype;
	int written = 0;

	/* ensure there is enough space for a whole frame */
	if (count < userspace_buffer_size || *offset)
		return -EINVAL;

	/* sleep until the condition is true or a signal is received the
	 * condition is checked each time gator_buffer_wait is woken up
	 */
	wait_event_interruptible(gator_buffer_wait, buffer_commit_ready(&cpu, &buftype) || !gator_started);

	if (signal_pending(current))
		return -EINTR;

	if (buftype == -1 || cpu == -1)
		return 0;

	mutex_lock(&gator_buffer_mutex);

	do {
		read = per_cpu(gator_buffer_read, cpu)[buftype];
		commit = per_cpu(gator_buffer_commit, cpu)[buftype];

		/* May happen if the buffer is freed during pending reads. */
		if (!per_cpu(gator_buffer, cpu)[buftype])
			break;

		/* determine the size of two halves */
		length1 = commit - read;
		length2 = 0;
		buffer1 = &(per_cpu(gator_buffer, cpu)[buftype][read]);
		buffer2 = &(per_cpu(gator_buffer, cpu)[buftype][0]);
		if (length1 < 0) {
			length1 = gator_buffer_size[buftype] - read;
			length2 = commit;
		}

		if (length1 + length2 > count - written)
			break;

		/* start, middle or end */
		if (length1 > 0 && copy_to_user(&buf[written], buffer1, length1))
			break;

		/* possible wrap around */
		if (length2 > 0 && copy_to_user(&buf[written + length1], buffer2, length2))
			break;

		per_cpu(gator_buffer_read, cpu)[buftype] = commit;
		written += length1 + length2;

		/* Wake up annotate_write if more space is available */
		if (buftype == ANNOTATE_BUF)
			wake_up(&gator_annotate_wait);
	} while (buffer_commit_ready(&cpu, &buftype));

	mutex_unlock(&gator_buffer_mutex);

	/* kick just in case we've lost an SMP event */
	wake_up(&gator_buffer_wait);

	return written > 0 ? written : -EFAULT;
}

static const struct file_operations gator_event_buffer_fops = {
	.open = userspace_buffer_open,
	.release = userspace_buffer_release,
	.read = userspace_buffer_read,
};

static ssize_t depth_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return gatorfs_ulong_to_user(gator_backtrace_depth, buf, count, offset);
}

static ssize_t depth_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = gatorfs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;

	retval = gator_set_backtrace(val);

	if (retval)
		return retval;
	return count;
}

static const struct file_operations depth_fops = {
	.read = depth_read,
	.write = depth_write
};

static void gator_op_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	struct gator_interface *gi;
	int cpu;

	/* reinitialize default values */
	gator_cpu_cores = 0;
	for_each_present_cpu(cpu) {
		gator_cpu_cores++;
	}
	userspace_buffer_size = BACKTRACE_BUFFER_SIZE;
	gator_response_type = 1;
	gator_live_rate = 0;

	gatorfs_create_file(sb, root, "enable", &enable_fops);
	gatorfs_create_file(sb, root, "buffer", &gator_event_buffer_fops);
	gatorfs_create_file(sb, root, "backtrace_depth", &depth_fops);
	gatorfs_create_ro_ulong(sb, root, "cpu_cores", &gator_cpu_cores);
	gatorfs_create_ro_ulong(sb, root, "buffer_size", &userspace_buffer_size);
	gatorfs_create_ulong(sb, root, "tick", &gator_timer_count);
	gatorfs_create_ulong(sb, root, "response_type", &gator_response_type);
	gatorfs_create_ro_ulong(sb, root, "version", &gator_protocol_version);
	gatorfs_create_ro_u64(sb, root, "started", &gator_monotonic_started);
	gatorfs_create_u64(sb, root, "live_rate", &gator_live_rate);

	/* Annotate interface */
	gator_annotate_create_files(sb, root);

	/* Linux Events */
	dir = gatorfs_mkdir(sb, root, "events");
	list_for_each_entry(gi, &gator_events, list)
		if (gi->create_files)
			gi->create_files(sb, dir);

	/* Sched Events */
	sched_trace_create_files(sb, dir);

	/* Power interface */
	gator_trace_power_create_files(sb, dir);
}

/******************************************************************************
 * Module
 ******************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)

#define GATOR_TRACEPOINTS \
	GATOR_HANDLE_TRACEPOINT(block_rq_complete); \
	GATOR_HANDLE_TRACEPOINT(cpu_frequency); \
	GATOR_HANDLE_TRACEPOINT(cpu_idle); \
	GATOR_HANDLE_TRACEPOINT(cpu_migrate_begin); \
	GATOR_HANDLE_TRACEPOINT(cpu_migrate_current); \
	GATOR_HANDLE_TRACEPOINT(cpu_migrate_finish); \
	GATOR_HANDLE_TRACEPOINT(irq_handler_exit); \
	GATOR_HANDLE_TRACEPOINT(mali_hw_counter); \
	GATOR_HANDLE_TRACEPOINT(mali_job_slots_event); \
	GATOR_HANDLE_TRACEPOINT(mali_mmu_as_in_use); \
	GATOR_HANDLE_TRACEPOINT(mali_mmu_as_released); \
	GATOR_HANDLE_TRACEPOINT(mali_page_fault_insert_pages); \
	GATOR_HANDLE_TRACEPOINT(mali_pm_status); \
	GATOR_HANDLE_TRACEPOINT(mali_sw_counter); \
	GATOR_HANDLE_TRACEPOINT(mali_sw_counters); \
	GATOR_HANDLE_TRACEPOINT(mali_timeline_event); \
	GATOR_HANDLE_TRACEPOINT(mali_total_alloc_pages_change); \
	GATOR_HANDLE_TRACEPOINT(mm_page_alloc); \
	GATOR_HANDLE_TRACEPOINT(mm_page_free); \
	GATOR_HANDLE_TRACEPOINT(mm_page_free_batched); \
	GATOR_HANDLE_TRACEPOINT(sched_process_exec); \
	GATOR_HANDLE_TRACEPOINT(sched_process_fork); \
	GATOR_HANDLE_TRACEPOINT(sched_process_free); \
	GATOR_HANDLE_TRACEPOINT(sched_switch); \
	GATOR_HANDLE_TRACEPOINT(softirq_exit); \
	GATOR_HANDLE_TRACEPOINT(task_rename); \

#define GATOR_HANDLE_TRACEPOINT(probe_name) \
	struct tracepoint *gator_tracepoint_##probe_name
GATOR_TRACEPOINTS;
#undef GATOR_HANDLE_TRACEPOINT

static void gator_save_tracepoint(struct tracepoint *tp, void *priv)
{
#define GATOR_HANDLE_TRACEPOINT(probe_name) \
	do { \
		if (strcmp(tp->name, #probe_name) == 0) { \
			gator_tracepoint_##probe_name = tp; \
			return; \
		} \
	} while (0)
GATOR_TRACEPOINTS;
#undef GATOR_HANDLE_TRACEPOINT
}

#else

#define for_each_kernel_tracepoint(fct, priv)

#endif

static int __init gator_module_init(void)
{
	for_each_kernel_tracepoint(gator_save_tracepoint, NULL);

	if (gatorfs_register())
		return -1;

	if (gator_init()) {
		gatorfs_unregister();
		return -1;
	}

	setup_timer(&gator_buffer_wake_up_timer, gator_buffer_wake_up, 0);

	/* Initialize the list of cpuids */
	memset(gator_cpuids, -1, sizeof(gator_cpuids));
	on_each_cpu(gator_read_cpuid, NULL, 1);

	return 0;
}

static void __exit gator_module_exit(void)
{
	del_timer_sync(&gator_buffer_wake_up_timer);
	tracepoint_synchronize_unregister();
	gator_exit();
	gatorfs_unregister();
}

module_init(gator_module_init);
module_exit(gator_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ARM Ltd");
MODULE_DESCRIPTION("Gator system profiler");
#define STRIFY2(ARG) #ARG
#define STRIFY(ARG) STRIFY2(ARG)
MODULE_VERSION(STRIFY(PROTOCOL_VERSION));
