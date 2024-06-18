/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2022 Linaro Limited. All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#ifndef _CORESIGHT_TRACE_ID_H
#define _CORESIGHT_TRACE_ID_H

/*
 * Coresight trace ID allocation API
 *
 * With multi cpu systems, and more additional trace sources a scalable
 * trace ID reservation system is required.
 *
 * The system will allocate Ids on a demand basis, and allow them to be
 * released when done.
 *
 * In order to ensure that a consistent cpu / ID matching is maintained
 * throughout a perf cs_etm event session - a session in progress flag will
 * be maintained, and released IDs not cleared until the perf session is
 * complete. This allows the same CPU to be re-allocated its prior ID.
 *
 *
 * Trace ID maps will be created and initialised to prevent architecturally
 * reserved IDs from being allocated.
 *
 * API permits multiple maps to be maintained - for large systems where
 * different sets of cpus trace into different independent sinks.
 */

#include <linux/bitops.h>
#include <linux/types.h>


/* architecturally we have 128 IDs some of which are reserved */
#define CORESIGHT_TRACE_IDS_MAX 128

/* ID 0 is reserved */
#define CORESIGHT_TRACE_ID_RES_0 0

/* ID 0x70 onwards are reserved */
#define CORESIGHT_TRACE_ID_RES_TOP 0x70

/* check an ID is in the valid range */
#define IS_VALID_CS_TRACE_ID(id)	\
	((id > CORESIGHT_TRACE_ID_RES_0) && (id < CORESIGHT_TRACE_ID_RES_TOP))

/**
 * Trace ID map.
 *
 * @used_ids:	Bitmap to register available (bit = 0) and in use (bit = 1) IDs.
 *		Initialised so that the reserved IDs are permanently marked as
 *		in use.
 * @pend_rel_ids: CPU IDs that have been released by the trace source but not
 *		  yet marked as available, to allow re-allocation to the same
 *		  CPU during a perf session.
 */
struct coresight_trace_id_map {
	DECLARE_BITMAP(used_ids, CORESIGHT_TRACE_IDS_MAX);
	DECLARE_BITMAP(pend_rel_ids, CORESIGHT_TRACE_IDS_MAX);
};

/* Allocate and release IDs for a single default trace ID map */

/**
 * Read and optionally allocate a CoreSight trace ID and associate with a CPU.
 *
 * Function will read the current trace ID for the associated CPU,
 * allocating an new ID if one is not currently allocated.
 *
 * Numeric ID values allocated use legacy allocation algorithm if possible,
 * otherwise any available ID is used.
 *
 * @cpu: The CPU index to allocate for.
 *
 * return: CoreSight trace ID or -EINVAL if allocation impossible.
 */
int coresight_trace_id_get_cpu_id(int cpu);

/**
 * Release an allocated trace ID associated with the CPU.
 *
 * This will release the CoreSight trace ID associated with the CPU,
 * unless a perf session is in operation.
 *
 * If a perf session is in operation then the ID will be marked as pending
 * release.
 *
 * @cpu: The CPU index to release the associated trace ID.
 */
void coresight_trace_id_put_cpu_id(int cpu);

/**
 * Read the current allocated CoreSight Trace ID value for the CPU.
 *
 * Fast read of the current value that does not allocate if no ID allocated
 * for the CPU.
 *
 * Used in perf context  where it is known that the value for the CPU will not
 * be changing, when perf starts and event on a core and outputs the Trace ID
 * for the CPU as a packet in the data file. IDs cannot change during a perf
 * session.
 *
 * This function does not take the lock protecting the ID lists, avoiding
 * locking dependency issues with perf locks.
 *
 * @cpu: The CPU index to read.
 *
 * return: current value, will be 0 if unallocated.
 */
int coresight_trace_id_read_cpu_id(int cpu);

/**
 * Allocate a CoreSight trace ID for a system component.
 *
 * Unconditionally allocates a Trace ID, without associating the ID with a CPU.
 *
 * Used to allocate IDs for system trace sources such as STM.
 *
 * return: Trace ID or -EINVAL if allocation is impossible.
 */
int coresight_trace_id_get_system_id(void);

/**
 * Release an allocated system trace ID.
 *
 * Unconditionally release a trace ID allocated to a system component.
 *
 * @id: value of trace ID allocated.
 */
void coresight_trace_id_put_system_id(int id);

/* notifiers for perf session start and stop */

/**
 * Notify the Trace ID allocator that a perf session is starting.
 *
 * Increase the perf session reference count - called by perf when setting up
 * a trace event.
 *
 * This reference count is used by the ID allocator to ensure that trace IDs
 * associated with a CPU cannot change or be released during a perf session.
 */
void coresight_trace_id_perf_start(void);

/**
 * Notify the ID allocator that a perf session is stopping.
 *
 * Decrease the perf session reference count.
 * if this causes the count to go to zero, then all Trace IDs marked as pending
 * release, will be released.
 */
void coresight_trace_id_perf_stop(void);

#endif /* _CORESIGHT_TRACE_ID_H */
