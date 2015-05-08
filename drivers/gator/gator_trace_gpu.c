/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
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
#ifdef MALI_DIR_MIDGARD
/* New DDK Directory structure with kernel/drivers/gpu/arm/midgard*/
#include "mali_linux_trace.h"
#else
/* Old DDK Directory structure with kernel/drivers/gpu/arm/t6xx*/
#include "linux/mali_linux_trace.h"
#endif
#endif

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

enum {
	GPU_UNIT_NONE = 0,
	GPU_UNIT_VP,
	GPU_UNIT_FP,
	GPU_UNIT_CL,
	NUMBER_OF_GPU_UNITS
};

#if defined(MALI_SUPPORT)

struct mali_activity {
	int core;
	int key;
	int count;
	int last_activity;
	int last_pid;
};

#define NUMBER_OF_GPU_CORES 16
static struct mali_activity mali_activities[NUMBER_OF_GPU_UNITS*NUMBER_OF_GPU_CORES];
static DEFINE_SPINLOCK(mali_activities_lock);

/* Only one event should be running on a unit and core at a time (ie,
 * a start event can only be followed by a stop and vice versa), but
 * because the kernel only knows when a job is enqueued and not
 * started, it is possible for a start1, start2, stop1, stop2. Change
 * it back into start1, stop1, start2, stop2 by queueing up start2 and
 * releasing it when stop1 is received.
 */

static int mali_activity_index(int core, int key)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mali_activities); ++i) {
		if ((mali_activities[i].core == core) && (mali_activities[i].key == key))
			break;
		if ((mali_activities[i].core == 0) && (mali_activities[i].key == 0)) {
			mali_activities[i].core = core;
			mali_activities[i].key = key;
			break;
		}
	}
	BUG_ON(i >= ARRAY_SIZE(mali_activities));

	return i;
}

static void mali_activity_enqueue(int core, int key, int activity, int pid)
{
	int i;
	int count;

	spin_lock(&mali_activities_lock);
	i = mali_activity_index(core, key);

	count = mali_activities[i].count;
	BUG_ON(count < 0);
	++mali_activities[i].count;
	if (count) {
		mali_activities[i].last_activity = activity;
		mali_activities[i].last_pid = pid;
	}
	spin_unlock(&mali_activities_lock);

	if (!count)
		gator_marshal_activity_switch(core, key, activity, pid);
}

static void mali_activity_stop(int core, int key)
{
	int i;
	int count;
	int last_activity = 0;
	int last_pid = 0;

	spin_lock(&mali_activities_lock);
	i = mali_activity_index(core, key);

	if (mali_activities[i].count == 0) {
		spin_unlock(&mali_activities_lock);
		return;
	}
	--mali_activities[i].count;
	count = mali_activities[i].count;
	if (count) {
		last_activity = mali_activities[i].last_activity;
		last_pid = mali_activities[i].last_pid;
	}
	spin_unlock(&mali_activities_lock);

	gator_marshal_activity_switch(core, key, 0, 0);
	if (count)
		gator_marshal_activity_switch(core, key, last_activity, last_pid);
}

void mali_activity_clear(struct mali_counter mali_activity[], size_t mali_activity_size)
{
	int activity;
	int cores;
	int core;

	for (activity = 0; activity < mali_activity_size; ++activity) {
		cores = mali_activity[activity].cores;
		if (cores < 0)
			cores = 1;
		for (core = 0; core < cores; ++core) {
			if (mali_activity[activity].enabled) {
				preempt_disable();
				gator_marshal_activity_switch(core, mali_activity[activity].key, 0, 0);
				preempt_enable();
			}
		}
	}
}

#endif

#if defined(MALI_SUPPORT) && (MALI_SUPPORT != MALI_MIDGARD)
#include "gator_events_mali_4xx.h"

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

struct mali_counter mali_activity[2];

GATOR_DEFINE_PROBE(mali_timeline_event, TP_PROTO(unsigned int event_id, unsigned int d0, unsigned int d1, unsigned int d2, unsigned int d3, unsigned int d4))
{
	unsigned int component, state;

	/* do as much work as possible before disabling interrupts */
	component = (event_id >> 16) & 0xFF;	/* component is an 8-bit field */
	state = (event_id >> 24) & 0xF;	/* state is a 4-bit field */

	switch (state) {
	case EVENT_TYPE_START:
		if (component == EVENT_CHANNEL_VP0) {
			/* tgid = d0; pid = d1; */
			if (mali_activity[1].enabled)
				mali_activity_enqueue(0, mali_activity[1].key, 1, d1);
		} else if (component >= EVENT_CHANNEL_FP0 && component <= EVENT_CHANNEL_FP7) {
			/* tgid = d0; pid = d1; */
			if (mali_activity[0].enabled)
				mali_activity_enqueue(component - EVENT_CHANNEL_FP0, mali_activity[0].key, 1, d1);
		}
		break;

	case EVENT_TYPE_STOP:
		if (component == EVENT_CHANNEL_VP0) {
			if (mali_activity[1].enabled)
				mali_activity_stop(0, mali_activity[1].key);
		} else if (component >= EVENT_CHANNEL_FP0 && component <= EVENT_CHANNEL_FP7) {
			if (mali_activity[0].enabled)
				mali_activity_stop(component - EVENT_CHANNEL_FP0, mali_activity[0].key);
		}
		break;

	case EVENT_TYPE_SINGLE:
		if (component == EVENT_CHANNEL_GPU) {
			unsigned int reason = (event_id & 0xffff);

			if (reason == EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE)
				gator_events_mali_log_dvfs_event(d0, d1);
		}
		break;

	default:
		break;
	}
}
#endif

#if defined(MALI_SUPPORT) && (MALI_SUPPORT == MALI_MIDGARD)

struct mali_counter mali_activity[3];

#if defined(MALI_JOB_SLOTS_EVENT_CHANGED)
GATOR_DEFINE_PROBE(mali_job_slots_event, TP_PROTO(unsigned int event_id, unsigned int tgid, unsigned int pid, unsigned char job_id))
#else
GATOR_DEFINE_PROBE(mali_job_slots_event, TP_PROTO(unsigned int event_id, unsigned int tgid, unsigned int pid))
#endif
{
	unsigned int component, state, unit;
#if !defined(MALI_JOB_SLOTS_EVENT_CHANGED)
	unsigned char job_id = 0;
#endif

	component = (event_id >> 16) & 0xFF;	/* component is an 8-bit field */
	state = (event_id >> 24) & 0xF;	/* state is a 4-bit field */

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
			if (mali_activity[component].enabled)
				mali_activity_enqueue(0, mali_activity[component].key, 1, (pid != 0 ? pid : tgid));
			break;
		case EVENT_TYPE_STOP:
		default: /* Some jobs can be soft-stopped, so ensure that this terminates the activity trace. */
			if (mali_activity[component].enabled)
				mali_activity_stop(0, mali_activity[component].key);
			break;
		}
	}
}
#endif

static int gator_trace_gpu_start(void)
{
	/*
	 * Returns nonzero for installation failed
	 * Absence of gpu trace points is not an error
	 */

#if defined(MALI_SUPPORT)
	memset(&mali_activities, 0, sizeof(mali_activities));
#endif
	mali_timeline_trace_registered = mali_job_slots_trace_registered = 0;

#if defined(MALI_SUPPORT) && (MALI_SUPPORT != MALI_MIDGARD)
	mali_activity_clear(mali_activity, ARRAY_SIZE(mali_activity));
	if (!GATOR_REGISTER_TRACE(mali_timeline_event))
		mali_timeline_trace_registered = 1;
#endif

#if defined(MALI_SUPPORT) && (MALI_SUPPORT == MALI_MIDGARD)
	mali_activity_clear(mali_activity, ARRAY_SIZE(mali_activity));
	if (!GATOR_REGISTER_TRACE(mali_job_slots_event))
		mali_job_slots_trace_registered = 1;
#endif

	return 0;
}

static void gator_trace_gpu_stop(void)
{
#if defined(MALI_SUPPORT) && (MALI_SUPPORT != MALI_MIDGARD)
	if (mali_timeline_trace_registered)
		GATOR_UNREGISTER_TRACE(mali_timeline_event);
#endif

#if defined(MALI_SUPPORT) && (MALI_SUPPORT == MALI_MIDGARD)
	if (mali_job_slots_trace_registered)
		GATOR_UNREGISTER_TRACE(mali_job_slots_event);
#endif

	mali_timeline_trace_registered = mali_job_slots_trace_registered = 0;
}
