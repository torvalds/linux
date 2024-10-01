#ifndef __ARM64_KVM_HYPTRACE_H_
#define __ARM64_KVM_HYPTRACE_H_
#include <asm/kvm_hyp.h>

#include <linux/ring_buffer_ext.h>

/*
 * Host donations to the hypervisor to store the struct hyp_buffer_page.
 */
struct hyp_buffer_pages_backing {
	unsigned long start;
	size_t size;
};

struct hyp_trace_pack {
	struct hyp_buffer_pages_backing		backing;
	struct kvm_nvhe_clock_data		trace_clock_data;
	struct trace_buffer_pack		trace_buffer_pack;

};
#endif
