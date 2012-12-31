/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
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

#define ACTIVITY_START  1
#define ACTIVITY_STOP   2

/* Note whether tracepoints have been registered */
static int mali_trace_registered;
static int gpu_trace_registered;

#define GPU_START			1
#define GPU_STOP			2

#define GPU_UNIT_VP			1
#define GPU_UNIT_FP			2
#define GPU_UNIT_CL			3

#ifdef MALI_SUPPORT

enum components {
    COMPONENT_VP0 = 1,
    COMPONENT_FP0 = 5,
    COMPONENT_FP1,
    COMPONENT_FP2,
    COMPONENT_FP3,
    COMPONENT_FP4,
    COMPONENT_FP5,
    COMPONENT_FP6,
    COMPONENT_FP7,
};

GATOR_DEFINE_PROBE(mali_timeline_event, TP_PROTO(unsigned int event_id, unsigned int d0, unsigned int d1, unsigned int d2, unsigned int d3, unsigned int d4))
{
	unsigned int component, state;
	int tgid = 0, pid = 0;

	// do as much work as possible before disabling interrupts
	component = (event_id >> 16) & 0xFF; // component is an 8-bit field
	state = (event_id >> 24) & 0xF;      // state is a 4-bit field

	if ((component == COMPONENT_VP0) || (component >= COMPONENT_FP0 && component <= COMPONENT_FP7)) {
		if (state == ACTIVITY_START || state == ACTIVITY_STOP) {
			unsigned int type = (state == ACTIVITY_START) ? GPU_START : GPU_STOP;
			unsigned int unit = (component < COMPONENT_FP0) ? GPU_UNIT_VP : GPU_UNIT_FP;
			unsigned int core = (component < COMPONENT_FP0) ? component - COMPONENT_VP0 : component - COMPONENT_FP0;
			if (state == ACTIVITY_START) {
				tgid = d0;
				pid = d1;
			}

			marshal_sched_gpu(type, unit, core, tgid, pid);
    	}
    }
}
#endif

GATOR_DEFINE_PROBE(gpu_activity_start, TP_PROTO(int gpu_unit, int gpu_core, struct task_struct *p))
{
	marshal_sched_gpu(GPU_START, gpu_unit, gpu_core, (int)p->tgid, (int)p->pid);
}

GATOR_DEFINE_PROBE(gpu_activity_stop, TP_PROTO(int gpu_unit, int gpu_core))
{
	marshal_sched_gpu(GPU_STOP, gpu_unit, gpu_core, 0, 0);
}

int gator_trace_gpu_start(void)
{
	/*
	 * Returns nonzero for installation failed
	 * Absence of gpu trace points is not an error
	 */

	gpu_trace_registered = mali_trace_registered = 0;

#ifdef MALI_SUPPORT
    if (!GATOR_REGISTER_TRACE(mali_timeline_event)) {
    	mali_trace_registered = 1;
    }
#endif

    if (!mali_trace_registered) {
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
#ifdef MALI_SUPPORT
	if (mali_trace_registered) {
		GATOR_UNREGISTER_TRACE(mali_timeline_event);
	}
#endif
	if (gpu_trace_registered) {
		GATOR_UNREGISTER_TRACE(gpu_activity_stop);
		GATOR_UNREGISTER_TRACE(gpu_activity_start);
	}

	gpu_trace_registered = mali_trace_registered = 0;
}
