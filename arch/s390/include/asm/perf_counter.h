/*
 * Performance counter support - s390 specific definitions.
 *
 * Copyright 2009 Martin Schwidefsky, IBM Corporation.
 */

static inline void set_perf_counter_pending(void) {}
static inline void clear_perf_counter_pending(void) {}

#define PERF_COUNTER_INDEX_OFFSET 0
