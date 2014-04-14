#if !defined(_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_H

#include <linux/tracepoint.h>
#include <asm/sigp.h>
#include <asm/debug.h>
#include <asm/dis.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/*
 * Helpers for vcpu-specific tracepoints containing the same information
 * as s390dbf VCPU_EVENTs.
 */
#define VCPU_PROTO_COMMON struct kvm_vcpu *vcpu
#define VCPU_ARGS_COMMON vcpu
#define VCPU_FIELD_COMMON __field(int, id)			\
	__field(unsigned long, pswmask)				\
	__field(unsigned long, pswaddr)
#define VCPU_ASSIGN_COMMON do {						\
	__entry->id = vcpu->vcpu_id;					\
	__entry->pswmask = vcpu->arch.sie_block->gpsw.mask;		\
	__entry->pswaddr = vcpu->arch.sie_block->gpsw.addr;		\
	} while (0);
#define VCPU_TP_PRINTK(p_str, p_args...)				\
	TP_printk("%02d[%016lx-%016lx]: " p_str, __entry->id,		\
		  __entry->pswmask, __entry->pswaddr, p_args)

/*
 * Tracepoints for SIE entry and exit.
 */
TRACE_EVENT(kvm_s390_sie_enter,
	    TP_PROTO(VCPU_PROTO_COMMON, int cpuflags),
	    TP_ARGS(VCPU_ARGS_COMMON, cpuflags),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(int, cpuflags)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->cpuflags = cpuflags;
		    ),

	    VCPU_TP_PRINTK("entering sie flags %x", __entry->cpuflags)
	);

TRACE_EVENT(kvm_s390_sie_fault,
	    TP_PROTO(VCPU_PROTO_COMMON),
	    TP_ARGS(VCPU_ARGS_COMMON),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    ),

	    VCPU_TP_PRINTK("%s", "fault in sie instruction")
	);

#define sie_intercept_code				\
	{0x04, "Instruction"},				\
	{0x08, "Program interruption"},			\
	{0x0C, "Instruction and program interruption"},	\
	{0x10, "External request"},			\
	{0x14, "External interruption"},		\
	{0x18, "I/O request"},				\
	{0x1C, "Wait state"},				\
	{0x20, "Validity"},				\
	{0x28, "Stop request"}

TRACE_EVENT(kvm_s390_sie_exit,
	    TP_PROTO(VCPU_PROTO_COMMON, u8 icptcode),
	    TP_ARGS(VCPU_ARGS_COMMON, icptcode),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(u8, icptcode)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->icptcode = icptcode;
		    ),

	    VCPU_TP_PRINTK("exit sie icptcode %d (%s)", __entry->icptcode,
			   __print_symbolic(__entry->icptcode,
					    sie_intercept_code))
	);

/*
 * Trace point for intercepted instructions.
 */
TRACE_EVENT(kvm_s390_intercept_instruction,
	    TP_PROTO(VCPU_PROTO_COMMON, __u16 ipa, __u32 ipb),
	    TP_ARGS(VCPU_ARGS_COMMON, ipa, ipb),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(__u64, instruction)
		    __field(char, insn[8])
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->instruction = ((__u64)ipa << 48) |
		    ((__u64)ipb << 16);
		    ),

	    VCPU_TP_PRINTK("intercepted instruction %016llx (%s)",
			   __entry->instruction,
			   insn_to_mnemonic((unsigned char *)
					    &__entry->instruction,
					 __entry->insn, sizeof(__entry->insn)) ?
			   "unknown" : __entry->insn)
	);

/*
 * Trace point for intercepted program interruptions.
 */
TRACE_EVENT(kvm_s390_intercept_prog,
	    TP_PROTO(VCPU_PROTO_COMMON, __u16 code),
	    TP_ARGS(VCPU_ARGS_COMMON, code),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(__u16, code)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->code = code;
		    ),

	    VCPU_TP_PRINTK("intercepted program interruption %04x",
			   __entry->code)
	);

/*
 * Trace point for validity intercepts.
 */
TRACE_EVENT(kvm_s390_intercept_validity,
	    TP_PROTO(VCPU_PROTO_COMMON, __u16 viwhy),
	    TP_ARGS(VCPU_ARGS_COMMON, viwhy),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(__u16, viwhy)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->viwhy = viwhy;
		    ),

	    VCPU_TP_PRINTK("got validity intercept %04x", __entry->viwhy)
	);

/*
 * Trace points for instructions that are of special interest.
 */

#define sigp_order_codes					\
	{SIGP_SENSE, "sense"},					\
	{SIGP_EXTERNAL_CALL, "external call"},			\
	{SIGP_EMERGENCY_SIGNAL, "emergency signal"},		\
	{SIGP_STOP, "stop"},					\
	{SIGP_STOP_AND_STORE_STATUS, "stop and store status"},	\
	{SIGP_SET_ARCHITECTURE, "set architecture"},		\
	{SIGP_SET_PREFIX, "set prefix"},			\
	{SIGP_STORE_STATUS_AT_ADDRESS, "store status at addr"},	\
	{SIGP_SENSE_RUNNING, "sense running"},			\
	{SIGP_RESTART, "restart"}

TRACE_EVENT(kvm_s390_handle_sigp,
	    TP_PROTO(VCPU_PROTO_COMMON, __u8 order_code, __u16 cpu_addr, \
		     __u32 parameter),
	    TP_ARGS(VCPU_ARGS_COMMON, order_code, cpu_addr, parameter),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(__u8, order_code)
		    __field(__u16, cpu_addr)
		    __field(__u32, parameter)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->order_code = order_code;
		    __entry->cpu_addr = cpu_addr;
		    __entry->parameter = parameter;
		    ),

	    VCPU_TP_PRINTK("handle sigp order %02x (%s), cpu address %04x, " \
			   "parameter %08x", __entry->order_code,
			   __print_symbolic(__entry->order_code,
					    sigp_order_codes),
			   __entry->cpu_addr, __entry->parameter)
	);

#define diagnose_codes				\
	{0x10, "release pages"},		\
	{0x44, "time slice end"},		\
	{0x308, "ipl functions"},		\
	{0x500, "kvm hypercall"},		\
	{0x501, "kvm breakpoint"}

TRACE_EVENT(kvm_s390_handle_diag,
	    TP_PROTO(VCPU_PROTO_COMMON, __u16 code),
	    TP_ARGS(VCPU_ARGS_COMMON, code),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(__u16, code)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->code = code;
		    ),

	    VCPU_TP_PRINTK("handle diagnose call %04x (%s)", __entry->code,
			   __print_symbolic(__entry->code, diagnose_codes))
	);

TRACE_EVENT(kvm_s390_handle_lctl,
	    TP_PROTO(VCPU_PROTO_COMMON, int g, int reg1, int reg3, u64 addr),
	    TP_ARGS(VCPU_ARGS_COMMON, g, reg1, reg3, addr),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(int, g)
		    __field(int, reg1)
		    __field(int, reg3)
		    __field(u64, addr)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->g = g;
		    __entry->reg1 = reg1;
		    __entry->reg3 = reg3;
		    __entry->addr = addr;
		    ),

	    VCPU_TP_PRINTK("%s: loading cr %x-%x from %016llx",
			   __entry->g ? "lctlg" : "lctl",
			   __entry->reg1, __entry->reg3, __entry->addr)
	);

TRACE_EVENT(kvm_s390_handle_prefix,
	    TP_PROTO(VCPU_PROTO_COMMON, int set, u32 address),
	    TP_ARGS(VCPU_ARGS_COMMON, set, address),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(int, set)
		    __field(u32, address)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->set = set;
		    __entry->address = address;
		    ),

	    VCPU_TP_PRINTK("%s prefix to %08x",
			   __entry->set ? "setting" : "storing",
			   __entry->address)
	);

TRACE_EVENT(kvm_s390_handle_stap,
	    TP_PROTO(VCPU_PROTO_COMMON, u64 address),
	    TP_ARGS(VCPU_ARGS_COMMON, address),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(u64, address)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->address = address;
		    ),

	    VCPU_TP_PRINTK("storing cpu address to %016llx",
			   __entry->address)
	);

TRACE_EVENT(kvm_s390_handle_stfl,
	    TP_PROTO(VCPU_PROTO_COMMON, unsigned int facility_list),
	    TP_ARGS(VCPU_ARGS_COMMON, facility_list),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(unsigned int, facility_list)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->facility_list = facility_list;
		    ),

	    VCPU_TP_PRINTK("store facility list value %08x",
			   __entry->facility_list)
	);

TRACE_EVENT(kvm_s390_handle_stsi,
	    TP_PROTO(VCPU_PROTO_COMMON, int fc, int sel1, int sel2, u64 addr),
	    TP_ARGS(VCPU_ARGS_COMMON, fc, sel1, sel2, addr),

	    TP_STRUCT__entry(
		    VCPU_FIELD_COMMON
		    __field(int, fc)
		    __field(int, sel1)
		    __field(int, sel2)
		    __field(u64, addr)
		    ),

	    TP_fast_assign(
		    VCPU_ASSIGN_COMMON
		    __entry->fc = fc;
		    __entry->sel1 = sel1;
		    __entry->sel2 = sel2;
		    __entry->addr = addr;
		    ),

	    VCPU_TP_PRINTK("STSI %d.%d.%d information stored to %016llx",
			   __entry->fc, __entry->sel1, __entry->sel2,
			   __entry->addr)
	);

#endif /* _TRACE_KVM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
