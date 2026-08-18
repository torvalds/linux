/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(GMAP_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define GMAP_TRACE_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../gmap
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_gmap

#define __KVM_FIELDS \
	__field(unsigned long, pswmask) \
	__field(unsigned long, pswaddr)
#define __KVM_ASSIGN ({\
	__entry->pswmask = vcpu->arch.sie_block->gpsw.mask; \
	__entry->pswaddr = vcpu->arch.sie_block->gpsw.addr; \
	})
#define __KVM_PRINT \
	__entry->pswmask,\
	__entry->pswaddr

TRACE_EVENT(kvm_s390_major_guest_pfault,
	    TP_PROTO(struct kvm_vcpu *vcpu),
	    TP_ARGS(vcpu),

	    TP_STRUCT__entry(
		__field(int, id)
		__KVM_FIELDS
		),

	    TP_fast_assign(
		__entry->id = vcpu->vcpu_id;
		__KVM_ASSIGN
		),
	    TP_printk("%02d[%016lx-%016lx]: major fault, maybe applicable for pfault",
		__entry->id,
		__KVM_PRINT
		)
	    );

#endif /* GMAP_TRACE_KVM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
