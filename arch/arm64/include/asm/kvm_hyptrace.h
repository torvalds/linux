/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ARM64_KVM_HYPTRACE_H_
#define __ARM64_KVM_HYPTRACE_H_

#include <linux/ring_buffer.h>

struct hyp_trace_desc {
	unsigned long			bpages_backing_start;
	size_t				bpages_backing_size;
	struct trace_buffer_desc	trace_buffer_desc;

};
#endif
