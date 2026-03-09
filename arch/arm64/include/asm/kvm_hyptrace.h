/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ARM64_KVM_HYPTRACE_H_
#define __ARM64_KVM_HYPTRACE_H_

#include <linux/ring_buffer.h>

struct hyp_trace_desc {
	unsigned long			bpages_backing_start;
	size_t				bpages_backing_size;
	struct trace_buffer_desc	trace_buffer_desc;

};

struct hyp_event_id {
	unsigned short	id;
	atomic_t	enabled;
};

extern struct remote_event __hyp_events_start[];
extern struct remote_event __hyp_events_end[];

/* hyp_event section used by the hypervisor */
extern struct hyp_event_id __hyp_event_ids_start[];
extern struct hyp_event_id __hyp_event_ids_end[];

#endif
