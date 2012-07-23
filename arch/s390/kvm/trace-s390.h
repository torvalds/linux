#if !defined(_TRACE_KVMS390_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVMS390_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm-s390
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-s390

/*
 * Trace point for the creation of the kvm instance.
 */
TRACE_EVENT(kvm_s390_create_vm,
	    TP_PROTO(unsigned long type),
	    TP_ARGS(type),

	    TP_STRUCT__entry(
		    __field(unsigned long, type)
		    ),

	    TP_fast_assign(
		    __entry->type = type;
		    ),

	    TP_printk("create vm%s",
		      __entry->type & KVM_VM_S390_UCONTROL ? " (UCONTROL)" : "")
	);

/*
 * Trace points for creation and destruction of vpcus.
 */
TRACE_EVENT(kvm_s390_create_vcpu,
	    TP_PROTO(unsigned int id, struct kvm_vcpu *vcpu,
		     struct kvm_s390_sie_block *sie_block),
	    TP_ARGS(id, vcpu, sie_block),

	    TP_STRUCT__entry(
		    __field(unsigned int, id)
		    __field(struct kvm_vcpu *, vcpu)
		    __field(struct kvm_s390_sie_block *, sie_block)
		    ),

	    TP_fast_assign(
		    __entry->id = id;
		    __entry->vcpu = vcpu;
		    __entry->sie_block = sie_block;
		    ),

	    TP_printk("create cpu %d at %p, sie block at %p", __entry->id,
		      __entry->vcpu, __entry->sie_block)
	);

TRACE_EVENT(kvm_s390_destroy_vcpu,
	    TP_PROTO(unsigned int id),
	    TP_ARGS(id),

	    TP_STRUCT__entry(
		    __field(unsigned int, id)
		    ),

	    TP_fast_assign(
		    __entry->id = id;
		    ),

	    TP_printk("destroy cpu %d", __entry->id)
	);

/*
 * Trace points for injection of interrupts, either per machine or
 * per vcpu.
 */

#define kvm_s390_int_type						\
	{KVM_S390_SIGP_STOP, "sigp stop"},				\
	{KVM_S390_PROGRAM_INT, "program interrupt"},			\
	{KVM_S390_SIGP_SET_PREFIX, "sigp set prefix"},			\
	{KVM_S390_RESTART, "sigp restart"},				\
	{KVM_S390_INT_VIRTIO, "virtio interrupt"},			\
	{KVM_S390_INT_SERVICE, "sclp interrupt"},			\
	{KVM_S390_INT_EMERGENCY, "sigp emergency"},			\
	{KVM_S390_INT_EXTERNAL_CALL, "sigp ext call"}

TRACE_EVENT(kvm_s390_inject_vm,
	    TP_PROTO(__u64 type, __u32 parm, __u64 parm64, int who),
	    TP_ARGS(type, parm, parm64, who),

	    TP_STRUCT__entry(
		    __field(__u32, inttype)
		    __field(__u32, parm)
		    __field(__u64, parm64)
		    __field(int, who)
		    ),

	    TP_fast_assign(
		    __entry->inttype = type & 0x00000000ffffffff;
		    __entry->parm = parm;
		    __entry->parm64 = parm64;
		    __entry->who = who;
		    ),

	    TP_printk("inject%s: type:%x (%s) parm:%x parm64:%llx",
		      (__entry->who == 1) ? " (from kernel)" :
		      (__entry->who == 2) ? " (from user)" : "",
		      __entry->inttype,
		      __print_symbolic(__entry->inttype, kvm_s390_int_type),
		      __entry->parm, __entry->parm64)
	);

TRACE_EVENT(kvm_s390_inject_vcpu,
	    TP_PROTO(unsigned int id, __u64 type, __u32 parm, __u64 parm64, \
		     int who),
	    TP_ARGS(id, type, parm, parm64, who),

	    TP_STRUCT__entry(
		    __field(int, id)
		    __field(__u32, inttype)
		    __field(__u32, parm)
		    __field(__u64, parm64)
		    __field(int, who)
		    ),

	    TP_fast_assign(
		    __entry->id = id;
		    __entry->inttype = type & 0x00000000ffffffff;
		    __entry->parm = parm;
		    __entry->parm64 = parm64;
		    __entry->who = who;
		    ),

	    TP_printk("inject%s (vcpu %d): type:%x (%s) parm:%x parm64:%llx",
		      (__entry->who == 1) ? " (from kernel)" :
		      (__entry->who == 2) ? " (from user)" : "",
		      __entry->id, __entry->inttype,
		      __print_symbolic(__entry->inttype, kvm_s390_int_type),
		      __entry->parm, __entry->parm64)
	);

/*
 * Trace point for the actual delivery of interrupts.
 */
TRACE_EVENT(kvm_s390_deliver_interrupt,
	    TP_PROTO(unsigned int id, __u64 type, __u32 data0, __u64 data1),
	    TP_ARGS(id, type, data0, data1),

	    TP_STRUCT__entry(
		    __field(int, id)
		    __field(__u32, inttype)
		    __field(__u32, data0)
		    __field(__u64, data1)
		    ),

	    TP_fast_assign(
		    __entry->id = id;
		    __entry->inttype = type & 0x00000000ffffffff;
		    __entry->data0 = data0;
		    __entry->data1 = data1;
		    ),

	    TP_printk("deliver interrupt (vcpu %d): type:%x (%s) "	\
		      "data:%08x %016llx",
		      __entry->id, __entry->inttype,
		      __print_symbolic(__entry->inttype, kvm_s390_int_type),
		      __entry->data0, __entry->data1)
	);

/*
 * Trace point for resets that may be requested from userspace.
 */
TRACE_EVENT(kvm_s390_request_resets,
	    TP_PROTO(__u64 resets),
	    TP_ARGS(resets),

	    TP_STRUCT__entry(
		    __field(__u64, resets)
		    ),

	    TP_fast_assign(
		    __entry->resets = resets;
		    ),

	    TP_printk("requesting userspace resets %llx",
		      __entry->resets)
	);

/*
 * Trace point for a vcpu's stop requests.
 */
TRACE_EVENT(kvm_s390_stop_request,
	    TP_PROTO(unsigned int action_bits),
	    TP_ARGS(action_bits),

	    TP_STRUCT__entry(
		    __field(unsigned int, action_bits)
		    ),

	    TP_fast_assign(
		    __entry->action_bits = action_bits;
		    ),

	    TP_printk("stop request, action_bits = %08x",
		      __entry->action_bits)
	);


#endif /* _TRACE_KVMS390_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
