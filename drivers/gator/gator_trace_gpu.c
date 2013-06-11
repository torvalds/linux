/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "gator.h"

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/math64.h>

#ifdef MALI_SUPPORT
#include "linux/mali_linux_trace.h"
#endif
#include "gator_trace_gpu.h"

/*
 * Taken from MALI_PROFILING_EVENT_TYPE_* items in Mali DDK.
 */
#define EVENT_TYPE_SINGLE  0
#define EVENT_TYPE_START   1
#define EVENT_TYPE_STOP    2
#define EVENT_TYPE_SUSPEND 3
#define EVENT_TYPE_RESUME  4

/* Note whether tracepoints have been registered */
static int mali_timeline_trace_registered;
static int mali_job_slots_trace_registered;
static int gpu_trace_registered;

enum {
	GPU_UNIT_NONE = 0,
	GPU_UNIT_VP,
	GPU_UNIT_FP,
	GPU_UNIT_CL,
	NUMBER_OF_GPU_UNITS
};

#define MALI_400     (0x0b07)
#define MALI_T6xx    (0x0056)

struct mali_gpu_job {
	int count;
	int last_core;
	int last_tgid;
	int last_pid;
};

#define NUMBER_OF_GPU_CORES 16
static struct mali_gpu_job mali_gpu_jobs[NUMBER_OF_GPU_UNITS][NUMBER_OF_GPU_CORES];
static DEFINE_SPINLOCK(mali_gpu_jobs_lock);

static void mali_gpu_enqueue(int unit, int core, int tgid, int pid)
{
	int count;

	spin_lock(&mali_gpu_jobs_lock);
	count = mali_gpu_jobs[unit][core].count;
	BUG_ON(count < 0);
	++mali_gpu_jobs[unit][core].count;
	if (count) {
		mali_gpu_jobs[unit][core].last_core = core;
		mali_gpu_jobs[unit][core].last_tgid = tgid;
		mali_gpu_jobs[unit][core].last_pid = pid;
	}
	spin_unlock(&mali_gpu_jobs_lock);

	if (!count) {
		marshal_sched_gpu_start(unit, core, tgid, pid);
	}
}

static void mali_gpu_stop(int unit, int core)
{
	int count;
	int last_core = 0;
	int last_tgid = 0;
	int last_pid = 0;

	spin_lock(&mali_gpu_jobs_lock);
	if (mali_gpu_jobs[unit][core].count == 0) {
		spin_unlock(&mali_gpu_jobs_lock);
		return;
	}
	--mali_gpu_jobs[unit][core].count;
	count = mali_gpu_jobs[unit][core].count;
	if (count) {
		last_core = mali_gpu_jobs[unit][core].last_core;
		last_tgid = mali_gpu_jobs[unit][core].last_tgid;
		last_pid = mali_gpu_jobs[unit][core].last_pid;
	}
	spin_unlock(&mali_gpu_jobs_lock);

	marshal_sched_gpu_stop(unit, core);
	if (count) {
		marshal_sched_gpu_start(unit, last_core, last_tgid, last_pid);
	}
}

#if defined(MALI_SUPPORT) && (MALI_SUPPORT != MALI_T6xx)
#include "gator_events_mali_400.h"

/*
 * Taken from MALI_PROFILING_EVENT_CHANNEL_* in Mali DDK.
 */
enum {
	EVENT_CHANNEL_SOFTWARE = 0,
	EVENT_CHANNEL_VP0 = 1,
	EVENT_CHANNEL_FP0 = 5,
	EVENT_CHANNEL_FP1,
	EVENT_CHANNEL_FP2,
	EVENT_CHANNEL_FP3,
	EVENT_CHANNEL_FP4,
	EVENT_CHANNEL_FP5,
	EVENT_CHANNEL_FP6,
	EVENT_CHANNEL_FP7,
	EVENT_CHANNEL_GPU = 21
};

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_SINGLE is used from the GPU channel
 */
enum {
	EVENT_REASON_SINGLE_GPU_NONE = 0,
	EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE = 1,
};

GATOR_DEFINE_PROBE(mali_timeline_event, TP_PROTO(unsigned int event_id, unsigned int d0, unsigned int d1, unsigned int d2, unsigned int d3, unsigned int d4))
{
	unsigned int component, state;

	// do as much work as possible before disabling interrupts
	component = (event_id >> 16) & 0xFF;	// component is an 8-bit field
	state = (event_id >> 24) & 0xF;	// state is a 4-bit field

	switch (state) {
	case EVENT_TYPE_START:
		if (component == EVENT_CHANNEL_VP0) {
			/* tgid = d0; pid = d1; */
			mali_gpu_enqueue(GPU_UNIT_VP, 0, d0, d1);
		} else if (component >= EVENT_CHANNEL_FP0 && component <= EVENT_CHANNEL_FP7) {
			/* tgid = d0; pid = d1; */
			mali_gpu_enqueue(GPU_UNIT_FP, component - EVENT_CHANNEL_FP0, d0, d1);
		}
		break;

	case EVENT_TYPE_STOP:
		if (component == EVENT_CHANNEL_VP0) {
			mali_gpu_stop(GPU_UNIT_VP, 0);
		} else if (component >= EVENT_CHANNEL_FP0 && component <= EVENT_CHANNEL_FP7) {
			mali_gpu_stop(GPU_UNIT_FP, component - EVENT_CHANNEL_FP0);
		}
		break;

	case EVENT_TYPE_SINGLE:
		if (component == EVENT_CHANNEL_GPU) {
			unsigned int reason = (event_id & 0xffff);

			if (reason == EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE) {
				gator_events_mali_log_dvfs_event(d0, d1);
			}
		}
		break;

	default:
		break;
	}
}
#endif

#if defined(MALI_SUPPORT) && (MALI_SUPPORT == MALI_T6xx)
GATOR_DEFINE_PROBE(mali_job_slots_event, TP_PROTO(unsigned int event_id, unsigned int tgid, unsigned int pid))
{
	unsigned int component, state, unit;

	component = (event_id >> 16) & 0xFF;	// component is an 8-bit field
	state = (event_id >> 24) & 0xF;	// state is a 4-bit field

	switch (component) {
	case 0:
		unit = GPU_UNIT_FP;
		break;
	case 1:
		unit = GPU_UNIT_VP;
		break;
	case 2:
		unit = GPU_UNIT_CL;
		break;
	default:
		unit = GPU_UNIT_NONE;
	}

	if (unit != GPU_UNIT_NONE) {
		switch (state) {
		case EVENT_TYPE_START:
			mali_gpu_enqueue(unit, 0, tgid, (pid != 0 ? pid : tgid));
			break;
		case EVENT_TYPE_STOP:
			mali_gpu_stop(unit, 0);
			break;
		default:
			/*
			 * Some jobs can be soft-stopped, so ensure that this terminates the activity trace.
			 */
			mali_gpu_stop(unit, 0);
		}
	}
}
#endif

GATOR_DEFINE_PROBE(gpu_activity_start, TP_PROTO(int gpu_unit, int gpu_core, struct task_struct *p))
{
	mali_gpu_enqueue(gpu_unit, gpu_core, (int)p->tgid, (int)p->pid);
}

GATOR_DEFINE_PROBE(gpu_activity_stop, TP_PROTO(int gpu_unit, int gpu_core))
{
	mali_gpu_stop(gpu_unit, gpu_core);
}

int gator_trace_gpu_start(void)
{
	/*
	 * Returns nonzero for installation failed
	 * Absence of gpu trace points is not an error
	 */

	gpu_trace_registered = mali_timeline_trace_registered = mali_job_slots_trace_registered = 0;

#if defined(MALI_SUPPORT) && (MALI_SUPPORT != MALI_T6xx)
	if (!GATOR_REGISTER_TRACE(mali_timeline_event)) {
		mali_timeline_trace_registered = 1;
	}
#endif

#if defined(MALI_SUPPORT) && (MALI_SUPPORT == MALI_T6xx)
	if (!GATOR_REGISTER_TRACE(mali_job_slots_event)) {
		mali_job_slots_trace_registered = 1;
	}
#endif

	if (!mali_timeline_trace_registered) {
		if (GATOR_REGISTER_TRACE(gpu_activity_start)) {
			return 0;
		}
		if (GATOR_REGISTER_TRACE(gpu_activity_stop)) {
			GATOR_UNREGISTER_TRACE(gpu_activity_start);
			return 0;
		}
		gpu_trace_registered = 1;
	}

	return 0;
}

void gator_trace_gpu_stop(void)
{
#if defined(MALI_SUPPORT) && (MALI_SUPPORT != MALI_T6xx)
	if (mali_timeline_trace_registered) {
		GATOR_UNREGISTER_TRACE(mali_timeline_event);
	}
#endif

#if defined(MALI_SUPPORT) && (MALI_SUPPORT == MALI_T6xx)
	if (mali_job_slots_trace_registered) {
		GATOR_UNREGISTER_TRACE(mali_job_slots_event);
	}
#endif

	if (gpu_trace_registered) {
		GATOR_UNREGISTER_TRACE(gpu_activity_stop);
		GATOR_UNREGISTER_TRACE(gpu_activity_start);
	}

	gpu_trace_registered = mali_timeline_trace_registered = mali_job_slots_trace_registered = 0;
}
