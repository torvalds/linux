/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#undef TRACE_GPU
#define TRACE_GPU gpu

#if !defined(_TRACE_GPU_H)
#define _TRACE_GPU_H

#include <linux/tracepoint.h>

/*
 * UNIT - the GPU processor type
 *  1 = Vertex Processor
 *  2 = Fragment Processor
 *
 * CORE - the GPU processor core number
 *  this is not the CPU core number
 */

/*
 * Tracepoint for calling GPU unit start activity on core
 */
TRACE_EVENT(gpu_activity_start,

	    TP_PROTO(int gpu_unit, int gpu_core, struct task_struct *p),

	    TP_ARGS(gpu_unit, gpu_core, p),

	    TP_STRUCT__entry(
			     __field(int, gpu_unit)
			     __field(int, gpu_core)
			     __array(char, comm, TASK_COMM_LEN)
			     __field(pid_t, pid)
	    ),

	    TP_fast_assign(
			   __entry->gpu_unit = gpu_unit;
			   __entry->gpu_core = gpu_core;
			   memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
			   __entry->pid = p->pid;
	    ),

	    TP_printk("unit=%d core=%d comm=%s pid=%d",
		      __entry->gpu_unit, __entry->gpu_core, __entry->comm,
		      __entry->pid)
    );

/*
 * Tracepoint for calling GPU unit stop activity on core
 */
TRACE_EVENT(gpu_activity_stop,

	    TP_PROTO(int gpu_unit, int gpu_core),

	    TP_ARGS(gpu_unit, gpu_core),

	    TP_STRUCT__entry(
			     __field(int, gpu_unit)
			     __field(int, gpu_core)
	    ),

	    TP_fast_assign(
			   __entry->gpu_unit = gpu_unit;
			   __entry->gpu_core = gpu_core;
	    ),

	    TP_printk("unit=%d core=%d", __entry->gpu_unit, __entry->gpu_core)
    );

#endif /* _TRACE_GPU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
