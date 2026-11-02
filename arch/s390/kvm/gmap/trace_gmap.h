/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(GMAP_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define GMAP_TRACE_KVM_H

#include <linux/tracepoint.h>

#undef UNPACK_KVM
#define UNPACK_KVM(name) name
#undef TRACE_SYSTEM
#define TRACE_SYSTEM UNPACK_KVM(KVM_DEV_NAME)
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../gmap
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_gmap

#ifdef KVM_S390_ARM64
#define __KVM_FIELDS \
	__field(unsigned long, pstate) \
	__field(unsigned long, pc)
#define __KVM_ASSIGN ({\
	__entry->pstate = vcpu->arch.sae_block.pstate; \
	__entry->pc = vcpu->arch.sae_block.pc; \
	})
#define __KVM_PRINT \
	__entry->pstate, \
	__entry->pc
#else
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
#endif

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
