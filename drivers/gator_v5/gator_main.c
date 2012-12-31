/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

static unsigned long gator_protocol_version = 9;

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
#include <asm/stacktrace.h>
#include <asm/uaccess.h>

#include "gator.h"
#include "gator_events.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
#error kernels prior to 2.6.32 are not supported
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

#if defined(__arm__) && defined(CONFIG_SMP) && !defined(CONFIG_LOCAL_TIMERS)
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
#define BACKTRACE_BUFFER_SIZE    (128*1024)
#define COUNTER_BUFFER_SIZE      (128*1024)
#define ANNOTATE_BUFFER_SIZE     (64*1024) // annotate  counters have the core as part of the data and the core value in the frame header may be discarded
#define SCHED_TRACE_BUFFER_SIZE  (128*1024)
#define GPU_TRACE_BUFFER_SIZE    (64*1024)
#define COUNTER2_BUFFER_SIZE     (64*1024) // counters2 counters have the core as part of the data and the core value in the frame header may be discarded
#define WFI_BUFFER_SIZE          (32*1024) // wfi       counters have the core as part of the data and the core value in the frame header may be discarded

#define NO_COOKIE				0UL
#define INVALID_COOKIE			~0UL

#define FRAME_BACKTRACE     1
#define FRAME_COUNTER       2
#define FRAME_ANNOTATE      3
#define FRAME_SCHED_TRACE   4
#define FRAME_GPU_TRACE     5
#define FRAME_COUNTER2      6
#define FRAME_WFI           7

#define MESSAGE_COOKIE              1
#define MESSAGE_START_BACKTRACE     5
#define MESSAGE_END_BACKTRACE       7
#define MESSAGE_SUMMARY             9
#define MESSAGE_PID_NAME            11

#define MAXSIZE_PACK32		5
#define MAXSIZE_PACK64		9

#if defined(__arm__)
#define PC_REG regs->ARM_pc
#else
#define PC_REG regs->ip
#endif

enum {BACKTRACE_BUF, COUNTER_BUF, SCHED_TRACE_BUF, GPU_TRACE_BUF, ANNOTATE_BUF, COUNTER2_BUF, WFI_BUF, NUM_GATOR_BUFS};

/******************************************************************************
 * Globals
 ******************************************************************************/
static unsigned long gator_cpu_cores;
static unsigned long userspace_buffer_size;
static unsigned long gator_backtrace_depth;

static unsigned long gator_started;
static unsigned long gator_buffer_opened;
static unsigned long gator_timer_count;
static unsigned long gator_response_type;
static DEFINE_MUTEX(start_mutex);
static DEFINE_MUTEX(gator_buffer_mutex);

bool event_based_sampling;

static DECLARE_WAIT_QUEUE_HEAD(gator_buffer_wait);
static LIST_HEAD(gator_events);

/******************************************************************************
 * Prototypes
 ******************************************************************************/
static void buffer_check(int cpu, int buftype);
static int buffer_bytes_available(int cpu, int buftype);
static bool buffer_check_space(int cpu, int buftype, int bytes);
static int contiguous_space_available(int cpu, int bufytpe);
static void gator_buffer_write_packed_int(int cpu, int buftype, unsigned int x);
static void gator_buffer_write_packed_int64(int cpu, int buftype, unsigned long long x);
static void gator_buffer_write_bytes(int cpu, int buftype, char *x, int len);
static void gator_buffer_write_string(int cpu, int buftype, char *x);
static void gator_add_trace(int cpu, int buftype, unsigned int address);
static void gator_add_sample(int cpu, int buftype, struct pt_regs * const regs);
static uint64_t gator_get_time(void);

static uint32_t gator_buffer_size[NUM_GATOR_BUFS];
static uint32_t gator_buffer_mask[NUM_GATOR_BUFS];
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_read);
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_write);
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_commit);
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], buffer_space_available);
static DEFINE_PER_CPU(char *[NUM_GATOR_BUFS], gator_buffer);

/******************************************************************************
 * Application Includes
 ******************************************************************************/
#include "gator_marshaling.c"
#include "gator_hrtimer_perf.c"
#include "gator_hrtimer_gator.c"
#include "gator_cookies.c"
#include "gator_trace_sched.c"
#include "gator_trace_power.c"
#include "gator_trace_gpu.c"
#include "gator_backtrace.c"
#include "gator_annotate.c"
#include "gator_fs.c"
#include "gator_ebs.c"
#include "gator_pack.c"

/******************************************************************************
 * Misc
 ******************************************************************************/
#if defined(__arm__)
u32 gator_cpuid(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r" (val));
	return (val >> 4) & 0xfff;
}
#endif

/******************************************************************************
 * Commit interface
 ******************************************************************************/
static bool buffer_commit_ready(int* cpu, int* buftype)
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
	return false;
}

/******************************************************************************
 * Buffer management
 ******************************************************************************/
static int buffer_bytes_available(int cpu, int buftype)
{
	int remaining, filled;

	filled = per_cpu(gator_buffer_write, cpu)[buftype] - per_cpu(gator_buffer_read, cpu)[buftype];
	if (filled < 0) {
		filled += gator_buffer_size[buftype];
	}

	remaining = gator_buffer_size[buftype] - filled;

	if (per_cpu(buffer_space_available, cpu)[buftype]) {
		// Give some extra room; also allows space to insert the overflow error packet
		remaining -= 200;
	} else {
		// Hysteresis, prevents multiple overflow messages
		remaining -= 2000;
	}

	return remaining;
}

static int contiguous_space_available(int cpu, int buftype)
{
	int remaining = buffer_bytes_available(cpu, buftype);
	int contiguous = gator_buffer_size[buftype] - per_cpu(gator_buffer_write, cpu)[buftype];
	if (remaining < contiguous)
		return remaining;
	else
		return contiguous;
}

static bool buffer_check_space(int cpu, int buftype, int bytes)
{
	int remaining = buffer_bytes_available(cpu, buftype);

	if (remaining < bytes) {
		per_cpu(buffer_space_available, cpu)[buftype] = false;
	} else {
		per_cpu(buffer_space_available, cpu)[buftype] = true;
	}

	return per_cpu(buffer_space_available, cpu)[buftype];
}

static void gator_buffer_write_bytes(int cpu, int buftype, char *x, int len)
{
	int i;
	u32 write = per_cpu(gator_buffer_write, cpu)[buftype];
	u32 mask = gator_buffer_mask[buftype];
	char* buffer = per_cpu(gator_buffer, cpu)[buftype];

	for (i = 0; i < len; i++) {
		buffer[write] = x[i];
		write = (write + 1) & mask;
	}

	per_cpu(gator_buffer_write, cpu)[buftype] = write;
}

static void gator_buffer_write_string(int cpu, int buftype, char *x)
{
	int len = strlen(x);
	gator_buffer_write_packed_int(cpu, buftype, len);
	gator_buffer_write_bytes(cpu, buftype, x, len);
}

static void gator_buffer_header(int cpu, int buftype)
{
	int frame;

	if (buftype == BACKTRACE_BUF)
		frame = FRAME_BACKTRACE;
	else if (buftype == COUNTER_BUF)
		frame = FRAME_COUNTER;
	else if (buftype == ANNOTATE_BUF)
		frame = FRAME_ANNOTATE;
	else if (buftype == SCHED_TRACE_BUF)
		frame = FRAME_SCHED_TRACE;
	else if (buftype == GPU_TRACE_BUF)
		frame = FRAME_GPU_TRACE;
	else if (buftype == COUNTER2_BUF)
		frame = FRAME_COUNTER2;
	else if (buftype == WFI_BUF)
		frame = FRAME_WFI;
	else
		frame = -1;

	if (per_cpu(gator_buffer, cpu)[buftype]) {
		marshal_frame(cpu, buftype, frame);
	}
}

static void gator_commit_buffer(int cpu, int buftype)
{
	if (!per_cpu(gator_buffer, cpu)[buftype])
		return;

	per_cpu(gator_buffer_commit, cpu)[buftype] = per_cpu(gator_buffer_write, cpu)[buftype];
	gator_buffer_header(cpu, buftype);
	wake_up(&gator_buffer_wait);
}

static void buffer_check(int cpu, int buftype)
{
	int filled = per_cpu(gator_buffer_write, cpu)[buftype] - per_cpu(gator_buffer_commit, cpu)[buftype];
	if (filled < 0) {
		filled += gator_buffer_size[buftype];
	}
	if (filled >= ((gator_buffer_size[buftype] * 3) / 4)) {
		gator_commit_buffer(cpu, buftype);
	}
}

static void gator_add_trace(int cpu, int buftype, unsigned int address)
{
	off_t offset = 0;
	unsigned long cookie = get_address_cookie(cpu, buftype, current, address & ~1, &offset);

	if (cookie == NO_COOKIE || cookie == INVALID_COOKIE) {
		offset = address;
	}

	marshal_backtrace(offset & ~1, cookie);
}

static void gator_add_sample(int cpu, int buftype, struct pt_regs * const regs)
{
	int inKernel = regs ? !user_mode(regs) : 1;
	unsigned long exec_cookie = inKernel ? NO_COOKIE : get_exec_cookie(cpu, buftype, current);

	if (!regs)
		return;

	if (!marshal_backtrace_header(exec_cookie, current->tgid, current->pid, inKernel))
		return;

	if (inKernel) {
		kernel_backtrace(cpu, buftype, regs);
	} else {
		// Cookie+PC
		gator_add_trace(cpu, buftype, PC_REG);

		// Backtrace
		if (gator_backtrace_depth)
			arm_backtrace_eabi(cpu, buftype, regs, gator_backtrace_depth);
	}

	marshal_backtrace_footer();
}

/******************************************************************************
 * hrtimer interrupt processing
 ******************************************************************************/
static void gator_timer_interrupt(void)
{
	struct pt_regs * const regs = get_irq_regs();
	int cpu = smp_processor_id();

	// Output backtrace
	gator_add_sample(cpu, BACKTRACE_BUF, regs);

	// Collect counters
	collect_counters();
}

static int gator_running;

// This function runs in interrupt context and on the appropriate core
static void gator_timer_offline(void* unused)
{
	struct gator_interface *gi;
	int i, len, cpu = smp_processor_id();
	int* buffer;

	gator_trace_sched_offline();
	gator_trace_power_offline();

	gator_hrtimer_offline(cpu);

	// Offline any events and output counters
	if (marshal_event_header()) {
		list_for_each_entry(gi, &gator_events, list) {
			if (gi->offline) {
				len = gi->offline(&buffer);
				marshal_event(len, buffer);
			}
		}
	}

	// Flush all buffers on this core
	for (i = 0; i < NUM_GATOR_BUFS; i++)
		gator_commit_buffer(cpu, i);
}

// This function runs in process context and may be running on a core other than core 'cpu'
static void gator_timer_offline_dispatch(int cpu)
{
	struct gator_interface *gi;

	list_for_each_entry(gi, &gator_events, list)
		if (gi->offline_dispatch)
			gi->offline_dispatch(cpu);

	gator_event_sampling_offline_dispatch(cpu);
}

static void gator_timer_stop(void)
{
	int cpu;

	if (gator_running) {
		on_each_cpu(gator_timer_offline, NULL, 1);
		for_each_online_cpu(cpu) {
			gator_timer_offline_dispatch(cpu);
		}

		gator_running = 0;
		gator_hrtimer_shutdown();
	}
}

// This function runs in interrupt context and on the appropriate core
static void gator_timer_online(void* unused)
{
	struct gator_interface *gi;
	int len, cpu = smp_processor_id();
	int* buffer;

	gator_trace_power_online();

	// online any events and output counters
	if (marshal_event_header()) {
		list_for_each_entry(gi, &gator_events, list) {
			if (gi->online) {
				len = gi->online(&buffer);
				marshal_event(len, buffer);
			}
		}
	}

	gator_hrtimer_online(cpu);
}

// This function runs in interrupt context and may be running on a core other than core 'cpu'
static void gator_timer_online_dispatch(int cpu)
{
	struct gator_interface *gi;

	list_for_each_entry(gi, &gator_events, list)
		if (gi->online_dispatch)
			gi->online_dispatch(cpu);

	gator_event_sampling_online_dispatch(cpu);
}

int gator_timer_start(unsigned long sample_rate)
{
	int cpu;

	if (gator_running) {
		pr_notice("gator: already running\n");
		return 0;
	}

	gator_running = 1;

	// event based sampling trumps hr timer based sampling
	if (event_based_sampling)
		sample_rate = 0;

	if (gator_hrtimer_init(sample_rate, gator_timer_interrupt) == -1)
		return -1;

	for_each_online_cpu(cpu) {
		gator_timer_online_dispatch(cpu);
	}
	on_each_cpu(gator_timer_online, NULL, 1);

	return 0;
}

static uint64_t gator_get_time(void)
{
	struct timespec ts;
	uint64_t timestamp;

	getnstimeofday(&ts);
	timestamp = timespec_to_ns(&ts);

	return timestamp;
}

/******************************************************************************
 * cpu hotplug and pm notifiers
 ******************************************************************************/
static int __cpuinit gator_hotcpu_notify(struct notifier_block *self, unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
		case CPU_DOWN_PREPARE:
		case CPU_DOWN_PREPARE_FROZEN:
			smp_call_function_single(cpu, gator_timer_offline, NULL, 1);
			gator_timer_offline_dispatch(cpu);
			break;
		case CPU_ONLINE:
		case CPU_ONLINE_FROZEN:
			gator_timer_online_dispatch(cpu);
			smp_call_function_single(cpu, gator_timer_online, NULL, 1);
			break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __refdata gator_hotcpu_notifier = {
	.notifier_call = gator_hotcpu_notify,
};

// n.b. calling "on_each_cpu" only runs on those that are online
// Registered linux events are not disabled, so their counters will continue to collect
static int gator_pm_notify(struct notifier_block *nb, unsigned long event, void *dummy)
{
	int cpu;

	switch (event) {
		case PM_HIBERNATION_PREPARE:
		case PM_SUSPEND_PREPARE:
			unregister_hotcpu_notifier(&gator_hotcpu_notifier);
			unregister_scheduler_tracepoints();
			on_each_cpu(gator_timer_offline, NULL, 1);
			for_each_online_cpu(cpu) {
				gator_timer_offline_dispatch(cpu);
			}
			break;
		case PM_POST_HIBERNATION:
		case PM_POST_SUSPEND:
			for_each_online_cpu(cpu) {
				gator_timer_online_dispatch(cpu);
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
	uint64_t timestamp, uptime = 0;
	struct timespec uptime_ts;
	void (*m2b)(struct timespec *ts);

	timestamp = gator_get_time();

	do_posix_clock_monotonic_gettime(&uptime_ts);
	m2b = symbol_get(monotonic_to_bootbased);
	if (m2b) {
		m2b(&uptime_ts);
		uptime = (long long)uptime_ts.tv_sec * 1000000000 + uptime_ts.tv_nsec;
	}

	marshal_summary(timestamp, uptime);
}

int gator_events_install(struct gator_interface *interface)
{
	list_add_tail(&interface->list, &gator_events);

	return 0;
}

int gator_events_get_key(void)
{
	// key of zero is reserved as a timestamp
	static int key = 1;

	return key++;
}

static int gator_init(void)
{
	int i;

	// events sources (gator_events.h, generated by gator_events.sh)
	for (i = 0; i < ARRAY_SIZE(gator_events_list); i++)
		if (gator_events_list[i])
			gator_events_list[i]();

	gator_trace_power_init();

	return 0;
}

static int gator_start(void)
{
	unsigned long cpu, i;
	struct gator_interface *gi;

	// Initialize the buffer with the frame type and core
	for_each_present_cpu(cpu) {
		for (i = 0; i < NUM_GATOR_BUFS; i++) {
			gator_buffer_header(cpu, i);
		}
	}

	// Capture the start time  
	gator_summary();

	// start all events
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

	// cookies shall be initialized before trace_sched_start() and gator_timer_start()
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
	if (gator_event_sampling_start())
		goto event_sampling_failure;
	if (gator_timer_start(gator_timer_count))
		goto timer_failure;
	if (gator_notifier_start())
		goto notifier_failure;

	return 0;

notifier_failure:
	gator_timer_stop();
timer_failure:
	gator_event_sampling_stop();
event_sampling_failure:
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
	// stop all events
	list_for_each_entry(gi, &gator_events, list)
		if (gi->stop)
			gi->stop();
events_failure:

	return -1;
}

static void gator_stop(void)
{
	struct gator_interface *gi;

	// stop all events
	list_for_each_entry(gi, &gator_events, list)
		if (gi->stop)
			gi->stop();

	gator_annotate_stop();
	gator_trace_sched_stop();
	gator_trace_power_stop();
	gator_trace_gpu_stop();
	gator_event_sampling_stop();

	// stop all interrupt callback reads before tearing down other interfaces
	gator_notifier_stop(); // should be called before gator_timer_stop to avoid re-enabling the hrtimer after it has been offlined
	gator_timer_stop();
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

	gator_buffer_size[BACKTRACE_BUF] = BACKTRACE_BUFFER_SIZE;
	gator_buffer_mask[BACKTRACE_BUF] = BACKTRACE_BUFFER_SIZE - 1;

	gator_buffer_size[COUNTER_BUF] = COUNTER_BUFFER_SIZE;
	gator_buffer_mask[COUNTER_BUF] = COUNTER_BUFFER_SIZE - 1;

	gator_buffer_size[SCHED_TRACE_BUF] = SCHED_TRACE_BUFFER_SIZE;
	gator_buffer_mask[SCHED_TRACE_BUF] = SCHED_TRACE_BUFFER_SIZE - 1;

	gator_buffer_size[GPU_TRACE_BUF] = GPU_TRACE_BUFFER_SIZE;
	gator_buffer_mask[GPU_TRACE_BUF] = GPU_TRACE_BUFFER_SIZE - 1;

	gator_buffer_size[ANNOTATE_BUF] = ANNOTATE_BUFFER_SIZE;
	gator_buffer_mask[ANNOTATE_BUF] = ANNOTATE_BUFFER_SIZE - 1;

	gator_buffer_size[COUNTER2_BUF] = COUNTER2_BUFFER_SIZE;
	gator_buffer_mask[COUNTER2_BUF] = COUNTER2_BUFFER_SIZE - 1;

	gator_buffer_size[WFI_BUF] = WFI_BUFFER_SIZE;
	gator_buffer_mask[WFI_BUF] = WFI_BUFFER_SIZE - 1;

	// Initialize percpu per buffer variables
	for (i = 0; i < NUM_GATOR_BUFS; i++) {
		// Verify buffers are a power of 2
		if (gator_buffer_size[i] & (gator_buffer_size[i] - 1)) {
			err = -ENOEXEC;
			goto setup_error;
		}

		for_each_present_cpu(cpu) {
			per_cpu(gator_buffer_read, cpu)[i] = 0;
			per_cpu(gator_buffer_write, cpu)[i] = 0;
			per_cpu(gator_buffer_commit, cpu)[i] = 0;
			per_cpu(buffer_space_available, cpu)[i] = true;

			// Annotation is a special case that only uses a single buffer
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
		}
		mutex_unlock(&gator_buffer_mutex);
	}

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
	.read		= enable_read,
	.write		= enable_write,
};

static int userspace_buffer_open(struct inode *inode, struct file *file)
{
	int err = -EPERM;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (test_and_set_bit_lock(0, &gator_buffer_opened))
		return -EBUSY;

	if ((err = gator_op_setup()))
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

static ssize_t userspace_buffer_read(struct file *file, char __user *buf,
				 size_t count, loff_t *offset)
{
	int retval = -EINVAL;
	int commit = 0, length, length1, length2, read, byte, type_length;
	char *buffer1;
	char *buffer2 = NULL;
	int cpu, buftype;

	/* do not handle partial reads */
	if (count != userspace_buffer_size || *offset)
		return -EINVAL;

	// sleep until the condition is true or a signal is received
	// the condition is checked each time gator_buffer_wait is woken up
	buftype = cpu = -1;
	wait_event_interruptible(gator_buffer_wait, buffer_commit_ready(&cpu, &buftype) || !gator_started);

	if (signal_pending(current))
		return -EINTR;

	length2 = 0;
	retval = -EFAULT;

	mutex_lock(&gator_buffer_mutex);

	if (buftype == -1 || cpu == -1) {
		retval = 0;
		goto out;
	}

	read = per_cpu(gator_buffer_read, cpu)[buftype];
	commit = per_cpu(gator_buffer_commit, cpu)[buftype];

	/* May happen if the buffer is freed during pending reads. */
	if (!per_cpu(gator_buffer, cpu)[buftype]) {
		retval = -EFAULT;
		goto out;
	}

	/* determine the size of two halves */
	length1 = commit - read;
	buffer1 = &(per_cpu(gator_buffer, cpu)[buftype][read]);
	buffer2 = &(per_cpu(gator_buffer, cpu)[buftype][0]);
	if (length1 < 0) {
		length1 = gator_buffer_size[buftype] - read;
		length2 = commit;
	}

	// post-populate the length, which does not include the response type length nor the length itself, i.e. only the length of the payload
	type_length = gator_response_type ? 1 : 0;
	length = length1 + length2 - type_length - sizeof(int);
	for (byte = 0; byte < sizeof(int); byte++) {
		per_cpu(gator_buffer, cpu)[buftype][(read + type_length + byte) & gator_buffer_mask[buftype]] = (length >> byte * 8) & 0xFF;
	}

	/* start, middle or end */
	if (length1 > 0) {
		if (copy_to_user(&buf[0], buffer1, length1)) {
			goto out;
		}
	}

	/* possible wrap around */
	if (length2 > 0) {
		if (copy_to_user(&buf[length1], buffer2, length2)) {
			goto out;
		}
	}

	per_cpu(gator_buffer_read, cpu)[buftype] = commit;
	retval = length1 + length2;

	/* kick just in case we've lost an SMP event */
	wake_up(&gator_buffer_wait);

out:
	mutex_unlock(&gator_buffer_mutex);
	return retval;
}

const struct file_operations gator_event_buffer_fops = {
	.open		= userspace_buffer_open,
	.release	= userspace_buffer_release,
	.read		= userspace_buffer_read,
};

static ssize_t depth_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return gatorfs_ulong_to_user(gator_backtrace_depth, buf, count,
					offset);
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
	.read		= depth_read,
	.write		= depth_write
};

void gator_op_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	struct gator_interface *gi;
	int cpu;

	/* reinitialize default values */
	gator_cpu_cores = 0;
	for_each_present_cpu(cpu) {
		gator_cpu_cores++;
	}
	userspace_buffer_size =	BACKTRACE_BUFFER_SIZE;
	gator_response_type = 1;

	gatorfs_create_file(sb, root, "enable", &enable_fops);
	gatorfs_create_file(sb, root, "buffer", &gator_event_buffer_fops);
	gatorfs_create_file(sb, root, "backtrace_depth", &depth_fops);
	gatorfs_create_ulong(sb, root, "cpu_cores", &gator_cpu_cores);
	gatorfs_create_ulong(sb, root, "buffer_size", &userspace_buffer_size);
	gatorfs_create_ulong(sb, root, "tick", &gator_timer_count);
	gatorfs_create_ulong(sb, root, "response_type", &gator_response_type);
	gatorfs_create_ro_ulong(sb, root, "version", &gator_protocol_version);

	// Annotate interface
	gator_annotate_create_files(sb, root);

	// Linux Events
	dir = gatorfs_mkdir(sb, root, "events");
	list_for_each_entry(gi, &gator_events, list)
		if (gi->create_files)
			gi->create_files(sb, dir);

	// Power interface
	gator_trace_power_create_files(sb, dir);
}

/******************************************************************************
 * Module
 ******************************************************************************/
static int __init gator_module_init(void)
{
	if (gatorfs_register()) {
		return -1;
	}

	if (gator_init()) {
		gatorfs_unregister();
		return -1;
	}

	return 0;
}

static void __exit gator_module_exit(void)
{
	tracepoint_synchronize_unregister();
	gatorfs_unregister();
}

module_init(gator_module_init);
module_exit(gator_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ARM Ltd");
MODULE_DESCRIPTION("Gator system profiler");
