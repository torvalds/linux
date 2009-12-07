/*
 * Performance event support - s390 specific definitions.
 *
 * Copyright 2009 Martin Schwidefsky, IBM Corporation.
 */

static inline void set_perf_event_pending(void) {}
static inline void clear_perf_event_pending(void) {}

#define PERF_EVENT_INDEX_OFFSET 0
