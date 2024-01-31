/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_H

#include <linux/tracepoint.h>
#include <asm/vmx.h>
#include <asm/svm.h>
#include <asm/clocksource.h>
#include <asm/pvclock-abi.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

/*
 * Tracepoint for guest mode entry.
 */
TRACE_EVENT(kvm_entry,
	TP_PROTO(struct kvm_vcpu *vcpu),
	TP_ARGS(vcpu),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id		)
		__field(	unsigned long,	rip		)
	),

	TP_fast_assign(
		__entry->vcpu_id        = vcpu->vcpu_id;
		__entry->rip		= kvm_rip_read(vcpu);
	),

	TP_printk("vcpu %u, rip 0x%lx", __entry->vcpu_id, __entry->rip)
);

/*
 * Tracepoint for hypercall.
 */
TRACE_EVENT(kvm_hypercall,
	TP_PROTO(unsigned long nr, unsigned long a0, unsigned long a1,
		 unsigned long a2, unsigned long a3),
	TP_ARGS(nr, a0, a1, a2, a3),

	TP_STRUCT__entry(
		__field(	unsigned long, 	nr		)
		__field(	unsigned long,	a0		)
		__field(	unsigned long,	a1		)
		__field(	unsigned long,	a2		)
		__field(	unsigned long,	a3		)
	),

	TP_fast_assign(
		__entry->nr		= nr;
		__entry->a0		= a0;
		__entry->a1		= a1;
		__entry->a2		= a2;
		__entry->a3		= a3;
	),

	TP_printk("nr 0x%lx a0 0x%lx a1 0x%lx a2 0x%lx a3 0x%lx",
		 __entry->nr, __entry->a0, __entry->a1,  __entry->a2,
		 __entry->a3)
);

/*
 * Tracepoint for hypercall.
 */
TRACE_EVENT(kvm_hv_hypercall,
	TP_PROTO(__u16 code, bool fast,  __u16 var_cnt, __u16 rep_cnt,
		 __u16 rep_idx, __u64 ingpa, __u64 outgpa),
	TP_ARGS(code, fast, var_cnt, rep_cnt, rep_idx, ingpa, outgpa),

	TP_STRUCT__entry(
		__field(	__u16,		rep_cnt		)
		__field(	__u16,		rep_idx		)
		__field(	__u64,		ingpa		)
		__field(	__u64,		outgpa		)
		__field(	__u16, 		code		)
		__field(	__u16,		var_cnt		)
		__field(	bool,		fast		)
	),

	TP_fast_assign(
		__entry->rep_cnt	= rep_cnt;
		__entry->rep_idx	= rep_idx;
		__entry->ingpa		= ingpa;
		__entry->outgpa		= outgpa;
		__entry->code		= code;
		__entry->var_cnt	= var_cnt;
		__entry->fast		= fast;
	),

	TP_printk("code 0x%x %s var_cnt 0x%x rep_cnt 0x%x idx 0x%x in 0x%llx out 0x%llx",
		  __entry->code, __entry->fast ? "fast" : "slow",
		  __entry->var_cnt, __entry->rep_cnt, __entry->rep_idx,
		  __entry->ingpa, __entry->outgpa)
);

TRACE_EVENT(kvm_hv_hypercall_done,
	TP_PROTO(u64 result),
	TP_ARGS(result),

	TP_STRUCT__entry(
		__field(__u64, result)
	),

	TP_fast_assign(
		__entry->result	= result;
	),

	TP_printk("result 0x%llx", __entry->result)
);

/*
 * Tracepoint for Xen hypercall.
 */
TRACE_EVENT(kvm_xen_hypercall,
	    TP_PROTO(u8 cpl, unsigned long nr,
		     unsigned long a0, unsigned long a1, unsigned long a2,
		     unsigned long a3, unsigned long a4, unsigned long a5),
	    TP_ARGS(cpl, nr, a0, a1, a2, a3, a4, a5),

	TP_STRUCT__entry(
		__field(u8, cpl)
		__field(unsigned long, nr)
		__field(unsigned long, a0)
		__field(unsigned long, a1)
		__field(unsigned long, a2)
		__field(unsigned long, a3)
		__field(unsigned long, a4)
		__field(unsigned long, a5)
	),

	TP_fast_assign(
		__entry->cpl = cpl;
		__entry->nr = nr;
		__entry->a0 = a0;
		__entry->a1 = a1;
		__entry->a2 = a2;
		__entry->a3 = a3;
		__entry->a4 = a4;
		__entry->a4 = a5;
	),

	TP_printk("cpl %d nr 0x%lx a0 0x%lx a1 0x%lx a2 0x%lx a3 0x%lx a4 0x%lx a5 %lx",
		  __entry->cpl, __entry->nr,
		  __entry->a0, __entry->a1, __entry->a2,
		  __entry->a3, __entry->a4, __entry->a5)
);



/*
 * Tracepoint for PIO.
 */

#define KVM_PIO_IN   0
#define KVM_PIO_OUT  1

TRACE_EVENT(kvm_pio,
	TP_PROTO(unsigned int rw, unsigned int port, unsigned int size,
		 unsigned int count, const void *data),
	TP_ARGS(rw, port, size, count, data),

	TP_STRUCT__entry(
		__field(	unsigned int, 	rw		)
		__field(	unsigned int, 	port		)
		__field(	unsigned int, 	size		)
		__field(	unsigned int,	count		)
		__field(	unsigned int,	val		)
	),

	TP_fast_assign(
		__entry->rw		= rw;
		__entry->port		= port;
		__entry->size		= size;
		__entry->count		= count;
		if (size == 1)
			__entry->val	= *(unsigned char *)data;
		else if (size == 2)
			__entry->val	= *(unsigned short *)data;
		else
			__entry->val	= *(unsigned int *)data;
	),

	TP_printk("pio_%s at 0x%x size %d count %d val 0x%x %s",
		  __entry->rw ? "write" : "read",
		  __entry->port, __entry->size, __entry->count, __entry->val,
		  __entry->count > 1 ? "(...)" : "")
);

/*
 * Tracepoint for fast mmio.
 */
TRACE_EVENT(kvm_fast_mmio,
	TP_PROTO(u64 gpa),
	TP_ARGS(gpa),

	TP_STRUCT__entry(
		__field(u64,	gpa)
	),

	TP_fast_assign(
		__entry->gpa		= gpa;
	),

	TP_printk("fast mmio at gpa 0x%llx", __entry->gpa)
);

/*
 * Tracepoint for cpuid.
 */
TRACE_EVENT(kvm_cpuid,
	TP_PROTO(unsigned int function, unsigned int index, unsigned long rax,
		 unsigned long rbx, unsigned long rcx, unsigned long rdx,
		 bool found, bool used_max_basic),
	TP_ARGS(function, index, rax, rbx, rcx, rdx, found, used_max_basic),

	TP_STRUCT__entry(
		__field(	unsigned int,	function	)
		__field(	unsigned int,	index		)
		__field(	unsigned long,	rax		)
		__field(	unsigned long,	rbx		)
		__field(	unsigned long,	rcx		)
		__field(	unsigned long,	rdx		)
		__field(	bool,		found		)
		__field(	bool,		used_max_basic	)
	),

	TP_fast_assign(
		__entry->function	= function;
		__entry->index		= index;
		__entry->rax		= rax;
		__entry->rbx		= rbx;
		__entry->rcx		= rcx;
		__entry->rdx		= rdx;
		__entry->found		= found;
		__entry->used_max_basic	= used_max_basic;
	),

	TP_printk("func %x idx %x rax %lx rbx %lx rcx %lx rdx %lx, cpuid entry %s%s",
		  __entry->function, __entry->index, __entry->rax,
		  __entry->rbx, __entry->rcx, __entry->rdx,
		  __entry->found ? "found" : "not found",
		  __entry->used_max_basic ? ", used max basic" : "")
);

#define AREG(x) { APIC_##x, "APIC_" #x }

#define kvm_trace_symbol_apic						    \
	AREG(ID), AREG(LVR), AREG(TASKPRI), AREG(ARBPRI), AREG(PROCPRI),    \
	AREG(EOI), AREG(RRR), AREG(LDR), AREG(DFR), AREG(SPIV), AREG(ISR),  \
	AREG(TMR), AREG(IRR), AREG(ESR), AREG(ICR), AREG(ICR2), AREG(LVTT), \
	AREG(LVTTHMR), AREG(LVTPC), AREG(LVT0), AREG(LVT1), AREG(LVTERR),   \
	AREG(TMICT), AREG(TMCCT), AREG(TDCR), AREG(SELF_IPI), AREG(EFEAT),  \
	AREG(ECTRL)
/*
 * Tracepoint for apic access.
 */
TRACE_EVENT(kvm_apic,
	TP_PROTO(unsigned int rw, unsigned int reg, u64 val),
	TP_ARGS(rw, reg, val),

	TP_STRUCT__entry(
		__field(	unsigned int,	rw		)
		__field(	unsigned int,	reg		)
		__field(	u64,		val		)
	),

	TP_fast_assign(
		__entry->rw		= rw;
		__entry->reg		= reg;
		__entry->val		= val;
	),

	TP_printk("apic_%s %s = 0x%llx",
		  __entry->rw ? "write" : "read",
		  __print_symbolic(__entry->reg, kvm_trace_symbol_apic),
		  __entry->val)
);

#define trace_kvm_apic_read(reg, val)		trace_kvm_apic(0, reg, val)
#define trace_kvm_apic_write(reg, val)		trace_kvm_apic(1, reg, val)

#define KVM_ISA_VMX   1
#define KVM_ISA_SVM   2

#define kvm_print_exit_reason(exit_reason, isa)				\
	(isa == KVM_ISA_VMX) ?						\
	__print_symbolic(exit_reason & 0xffff, VMX_EXIT_REASONS) :	\
	__print_symbolic(exit_reason, SVM_EXIT_REASONS),		\
	(isa == KVM_ISA_VMX && exit_reason & ~0xffff) ? " " : "",	\
	(isa == KVM_ISA_VMX) ?						\
	__print_flags(exit_reason & ~0xffff, " ", VMX_EXIT_REASON_FLAGS) : ""

#define TRACE_EVENT_KVM_EXIT(name)					     \
TRACE_EVENT(name,							     \
	TP_PROTO(struct kvm_vcpu *vcpu, u32 isa),			     \
	TP_ARGS(vcpu, isa),						     \
									     \
	TP_STRUCT__entry(						     \
		__field(	unsigned int,	exit_reason	)	     \
		__field(	unsigned long,	guest_rip	)	     \
		__field(	u32,	        isa             )	     \
		__field(	u64,	        info1           )	     \
		__field(	u64,	        info2           )	     \
		__field(	u32,	        intr_info	)	     \
		__field(	u32,	        error_code	)	     \
		__field(	unsigned int,	vcpu_id         )	     \
	),								     \
									     \
	TP_fast_assign(							     \
		__entry->guest_rip	= kvm_rip_read(vcpu);		     \
		__entry->isa            = isa;				     \
		__entry->vcpu_id        = vcpu->vcpu_id;		     \
		static_call(kvm_x86_get_exit_info)(vcpu,		     \
					  &__entry->exit_reason,	     \
					  &__entry->info1,		     \
					  &__entry->info2,		     \
					  &__entry->intr_info,		     \
					  &__entry->error_code);	     \
	),								     \
									     \
	TP_printk("vcpu %u reason %s%s%s rip 0x%lx info1 0x%016llx "	     \
		  "info2 0x%016llx intr_info 0x%08x error_code 0x%08x",	     \
		  __entry->vcpu_id,					     \
		  kvm_print_exit_reason(__entry->exit_reason, __entry->isa), \
		  __entry->guest_rip, __entry->info1, __entry->info2,	     \
		  __entry->intr_info, __entry->error_code)		     \
)

/*
 * Tracepoint for kvm guest exit:
 */
TRACE_EVENT_KVM_EXIT(kvm_exit);

/*
 * Tracepoint for kvm interrupt injection:
 */
TRACE_EVENT(kvm_inj_virq,
	TP_PROTO(unsigned int vector, bool soft, bool reinjected),
	TP_ARGS(vector, soft, reinjected),

	TP_STRUCT__entry(
		__field(	unsigned int,	vector		)
		__field(	bool,		soft		)
		__field(	bool,		reinjected	)
	),

	TP_fast_assign(
		__entry->vector		= vector;
		__entry->soft		= soft;
		__entry->reinjected	= reinjected;
	),

	TP_printk("%s 0x%x%s",
		  __entry->soft ? "Soft/INTn" : "IRQ", __entry->vector,
		  __entry->reinjected ? " [reinjected]" : "")
);

#define EXS(x) { x##_VECTOR, "#" #x }

#define kvm_trace_sym_exc						\
	EXS(DE), EXS(DB), EXS(BP), EXS(OF), EXS(BR), EXS(UD), EXS(NM),	\
	EXS(DF), EXS(TS), EXS(NP), EXS(SS), EXS(GP), EXS(PF),		\
	EXS(MF), EXS(AC), EXS(MC)

/*
 * Tracepoint for kvm interrupt injection:
 */
TRACE_EVENT(kvm_inj_exception,
	TP_PROTO(unsigned exception, bool has_error, unsigned error_code,
		 bool reinjected),
	TP_ARGS(exception, has_error, error_code, reinjected),

	TP_STRUCT__entry(
		__field(	u8,	exception	)
		__field(	u8,	has_error	)
		__field(	u32,	error_code	)
		__field(	bool,	reinjected	)
	),

	TP_fast_assign(
		__entry->exception	= exception;
		__entry->has_error	= has_error;
		__entry->error_code	= error_code;
		__entry->reinjected	= reinjected;
	),

	TP_printk("%s%s%s%s%s",
		  __print_symbolic(__entry->exception, kvm_trace_sym_exc),
		  !__entry->has_error ? "" : " (",
		  !__entry->has_error ? "" : __print_symbolic(__entry->error_code, { }),
		  !__entry->has_error ? "" : ")",
		  __entry->reinjected ? " [reinjected]" : "")
);

/*
 * Tracepoint for page fault.
 */
TRACE_EVENT(kvm_page_fault,
	TP_PROTO(struct kvm_vcpu *vcpu, u64 fault_address, u64 error_code),
	TP_ARGS(vcpu, fault_address, error_code),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id		)
		__field(	unsigned long,	guest_rip	)
		__field(	u64,		fault_address	)
		__field(	u64,		error_code	)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu->vcpu_id;
		__entry->guest_rip	= kvm_rip_read(vcpu);
		__entry->fault_address	= fault_address;
		__entry->error_code	= error_code;
	),

	TP_printk("vcpu %u rip 0x%lx address 0x%016llx error_code 0x%llx",
		  __entry->vcpu_id, __entry->guest_rip,
		  __entry->fault_address, __entry->error_code)
);

/*
 * Tracepoint for guest MSR access.
 */
TRACE_EVENT(kvm_msr,
	TP_PROTO(unsigned write, u32 ecx, u64 data, bool exception),
	TP_ARGS(write, ecx, data, exception),

	TP_STRUCT__entry(
		__field(	unsigned,	write		)
		__field(	u32,		ecx		)
		__field(	u64,		data		)
		__field(	u8,		exception	)
	),

	TP_fast_assign(
		__entry->write		= write;
		__entry->ecx		= ecx;
		__entry->data		= data;
		__entry->exception	= exception;
	),

	TP_printk("msr_%s %x = 0x%llx%s",
		  __entry->write ? "write" : "read",
		  __entry->ecx, __entry->data,
		  __entry->exception ? " (#GP)" : "")
);

#define trace_kvm_msr_read(ecx, data)      trace_kvm_msr(0, ecx, data, false)
#define trace_kvm_msr_write(ecx, data)     trace_kvm_msr(1, ecx, data, false)
#define trace_kvm_msr_read_ex(ecx)         trace_kvm_msr(0, ecx, 0, true)
#define trace_kvm_msr_write_ex(ecx, data)  trace_kvm_msr(1, ecx, data, true)

/*
 * Tracepoint for guest CR access.
 */
TRACE_EVENT(kvm_cr,
	TP_PROTO(unsigned int rw, unsigned int cr, unsigned long val),
	TP_ARGS(rw, cr, val),

	TP_STRUCT__entry(
		__field(	unsigned int,	rw		)
		__field(	unsigned int,	cr		)
		__field(	unsigned long,	val		)
	),

	TP_fast_assign(
		__entry->rw		= rw;
		__entry->cr		= cr;
		__entry->val		= val;
	),

	TP_printk("cr_%s %x = 0x%lx",
		  __entry->rw ? "write" : "read",
		  __entry->cr, __entry->val)
);

#define trace_kvm_cr_read(cr, val)		trace_kvm_cr(0, cr, val)
#define trace_kvm_cr_write(cr, val)		trace_kvm_cr(1, cr, val)

TRACE_EVENT(kvm_pic_set_irq,
	    TP_PROTO(__u8 chip, __u8 pin, __u8 elcr, __u8 imr, bool coalesced),
	    TP_ARGS(chip, pin, elcr, imr, coalesced),

	TP_STRUCT__entry(
		__field(	__u8,		chip		)
		__field(	__u8,		pin		)
		__field(	__u8,		elcr		)
		__field(	__u8,		imr		)
		__field(	bool,		coalesced	)
	),

	TP_fast_assign(
		__entry->chip		= chip;
		__entry->pin		= pin;
		__entry->elcr		= elcr;
		__entry->imr		= imr;
		__entry->coalesced	= coalesced;
	),

	TP_printk("chip %u pin %u (%s%s)%s",
		  __entry->chip, __entry->pin,
		  (__entry->elcr & (1 << __entry->pin)) ? "level":"edge",
		  (__entry->imr & (1 << __entry->pin)) ? "|masked":"",
		  __entry->coalesced ? " (coalesced)" : "")
);

#define kvm_apic_dst_shorthand		\
	{0x0, "dst"},			\
	{0x1, "self"},			\
	{0x2, "all"},			\
	{0x3, "all-but-self"}

TRACE_EVENT(kvm_apic_ipi,
	    TP_PROTO(__u32 icr_low, __u32 dest_id),
	    TP_ARGS(icr_low, dest_id),

	TP_STRUCT__entry(
		__field(	__u32,		icr_low		)
		__field(	__u32,		dest_id		)
	),

	TP_fast_assign(
		__entry->icr_low	= icr_low;
		__entry->dest_id	= dest_id;
	),

	TP_printk("dst %x vec %u (%s|%s|%s|%s|%s)",
		  __entry->dest_id, (u8)__entry->icr_low,
		  __print_symbolic((__entry->icr_low >> 8 & 0x7),
				   kvm_deliver_mode),
		  (__entry->icr_low & (1<<11)) ? "logical" : "physical",
		  (__entry->icr_low & (1<<14)) ? "assert" : "de-assert",
		  (__entry->icr_low & (1<<15)) ? "level" : "edge",
		  __print_symbolic((__entry->icr_low >> 18 & 0x3),
				   kvm_apic_dst_shorthand))
);

TRACE_EVENT(kvm_apic_accept_irq,
	    TP_PROTO(__u32 apicid, __u16 dm, __u16 tm, __u8 vec),
	    TP_ARGS(apicid, dm, tm, vec),

	TP_STRUCT__entry(
		__field(	__u32,		apicid		)
		__field(	__u16,		dm		)
		__field(	__u16,		tm		)
		__field(	__u8,		vec		)
	),

	TP_fast_assign(
		__entry->apicid		= apicid;
		__entry->dm		= dm;
		__entry->tm		= tm;
		__entry->vec		= vec;
	),

	TP_printk("apicid %x vec %u (%s|%s)",
		  __entry->apicid, __entry->vec,
		  __print_symbolic((__entry->dm >> 8 & 0x7), kvm_deliver_mode),
		  __entry->tm ? "level" : "edge")
);

TRACE_EVENT(kvm_eoi,
	    TP_PROTO(struct kvm_lapic *apic, int vector),
	    TP_ARGS(apic, vector),

	TP_STRUCT__entry(
		__field(	__u32,		apicid		)
		__field(	int,		vector		)
	),

	TP_fast_assign(
		__entry->apicid		= apic->vcpu->vcpu_id;
		__entry->vector		= vector;
	),

	TP_printk("apicid %x vector %d", __entry->apicid, __entry->vector)
);

TRACE_EVENT(kvm_pv_eoi,
	    TP_PROTO(struct kvm_lapic *apic, int vector),
	    TP_ARGS(apic, vector),

	TP_STRUCT__entry(
		__field(	__u32,		apicid		)
		__field(	int,		vector		)
	),

	TP_fast_assign(
		__entry->apicid		= apic->vcpu->vcpu_id;
		__entry->vector		= vector;
	),

	TP_printk("apicid %x vector %d", __entry->apicid, __entry->vector)
);

/*
 * Tracepoint for nested VMRUN
 */
TRACE_EVENT(kvm_nested_vmenter,
	    TP_PROTO(__u64 rip, __u64 vmcb, __u64 nested_rip, __u32 int_ctl,
		     __u32 event_inj, bool tdp_enabled, __u64 guest_tdp_pgd,
		     __u64 guest_cr3, __u32 isa),
	    TP_ARGS(rip, vmcb, nested_rip, int_ctl, event_inj, tdp_enabled,
		    guest_tdp_pgd, guest_cr3, isa),

	TP_STRUCT__entry(
		__field(	__u64,		rip		)
		__field(	__u64,		vmcb		)
		__field(	__u64,		nested_rip	)
		__field(	__u32,		int_ctl		)
		__field(	__u32,		event_inj	)
		__field(	bool,		tdp_enabled	)
		__field(	__u64,		guest_pgd	)
		__field(	__u32,		isa		)
	),

	TP_fast_assign(
		__entry->rip		= rip;
		__entry->vmcb		= vmcb;
		__entry->nested_rip	= nested_rip;
		__entry->int_ctl	= int_ctl;
		__entry->event_inj	= event_inj;
		__entry->tdp_enabled	= tdp_enabled;
		__entry->guest_pgd	= tdp_enabled ? guest_tdp_pgd : guest_cr3;
		__entry->isa		= isa;
	),

	TP_printk("rip: 0x%016llx %s: 0x%016llx nested_rip: 0x%016llx "
		  "int_ctl: 0x%08x event_inj: 0x%08x nested_%s=%s %s: 0x%016llx",
		  __entry->rip,
		  __entry->isa == KVM_ISA_VMX ? "vmcs" : "vmcb",
		  __entry->vmcb,
		  __entry->nested_rip,
		  __entry->int_ctl,
		  __entry->event_inj,
		  __entry->isa == KVM_ISA_VMX ? "ept" : "npt",
		  __entry->tdp_enabled ? "y" : "n",
		  !__entry->tdp_enabled ? "guest_cr3" :
		  __entry->isa == KVM_ISA_VMX ? "nested_eptp" : "nested_cr3",
		  __entry->guest_pgd)
);

TRACE_EVENT(kvm_nested_intercepts,
	    TP_PROTO(__u16 cr_read, __u16 cr_write, __u32 exceptions,
		     __u32 intercept1, __u32 intercept2, __u32 intercept3),
	    TP_ARGS(cr_read, cr_write, exceptions, intercept1,
		    intercept2, intercept3),

	TP_STRUCT__entry(
		__field(	__u16,		cr_read		)
		__field(	__u16,		cr_write	)
		__field(	__u32,		exceptions	)
		__field(	__u32,		intercept1	)
		__field(	__u32,		intercept2	)
		__field(	__u32,		intercept3	)
	),

	TP_fast_assign(
		__entry->cr_read	= cr_read;
		__entry->cr_write	= cr_write;
		__entry->exceptions	= exceptions;
		__entry->intercept1	= intercept1;
		__entry->intercept2	= intercept2;
		__entry->intercept3	= intercept3;
	),

	TP_printk("cr_read: %04x cr_write: %04x excp: %08x "
		  "intercepts: %08x %08x %08x",
		  __entry->cr_read, __entry->cr_write, __entry->exceptions,
		  __entry->intercept1, __entry->intercept2, __entry->intercept3)
);
/*
 * Tracepoint for #VMEXIT while nested
 */
TRACE_EVENT_KVM_EXIT(kvm_nested_vmexit);

/*
 * Tracepoint for #VMEXIT reinjected to the guest
 */
TRACE_EVENT(kvm_nested_vmexit_inject,
	    TP_PROTO(__u32 exit_code,
		     __u64 exit_info1, __u64 exit_info2,
		     __u32 exit_int_info, __u32 exit_int_info_err, __u32 isa),
	    TP_ARGS(exit_code, exit_info1, exit_info2,
		    exit_int_info, exit_int_info_err, isa),

	TP_STRUCT__entry(
		__field(	__u32,		exit_code		)
		__field(	__u64,		exit_info1		)
		__field(	__u64,		exit_info2		)
		__field(	__u32,		exit_int_info		)
		__field(	__u32,		exit_int_info_err	)
		__field(	__u32,		isa			)
	),

	TP_fast_assign(
		__entry->exit_code		= exit_code;
		__entry->exit_info1		= exit_info1;
		__entry->exit_info2		= exit_info2;
		__entry->exit_int_info		= exit_int_info;
		__entry->exit_int_info_err	= exit_int_info_err;
		__entry->isa			= isa;
	),

	TP_printk("reason: %s%s%s ext_inf1: 0x%016llx "
		  "ext_inf2: 0x%016llx ext_int: 0x%08x ext_int_err: 0x%08x",
		  kvm_print_exit_reason(__entry->exit_code, __entry->isa),
		  __entry->exit_info1, __entry->exit_info2,
		  __entry->exit_int_info, __entry->exit_int_info_err)
);

/*
 * Tracepoint for nested #vmexit because of interrupt pending
 */
TRACE_EVENT(kvm_nested_intr_vmexit,
	    TP_PROTO(__u64 rip),
	    TP_ARGS(rip),

	TP_STRUCT__entry(
		__field(	__u64,	rip	)
	),

	TP_fast_assign(
		__entry->rip	=	rip
	),

	TP_printk("rip: 0x%016llx", __entry->rip)
);

/*
 * Tracepoint for nested #vmexit because of interrupt pending
 */
TRACE_EVENT(kvm_invlpga,
	    TP_PROTO(__u64 rip, unsigned int asid, u64 address),
	    TP_ARGS(rip, asid, address),

	TP_STRUCT__entry(
		__field(	__u64,		rip	)
		__field(	unsigned int,	asid	)
		__field(	__u64,		address	)
	),

	TP_fast_assign(
		__entry->rip		=	rip;
		__entry->asid		=	asid;
		__entry->address	=	address;
	),

	TP_printk("rip: 0x%016llx asid: %u address: 0x%016llx",
		  __entry->rip, __entry->asid, __entry->address)
);

/*
 * Tracepoint for nested #vmexit because of interrupt pending
 */
TRACE_EVENT(kvm_skinit,
	    TP_PROTO(__u64 rip, __u32 slb),
	    TP_ARGS(rip, slb),

	TP_STRUCT__entry(
		__field(	__u64,	rip	)
		__field(	__u32,	slb	)
	),

	TP_fast_assign(
		__entry->rip		=	rip;
		__entry->slb		=	slb;
	),

	TP_printk("rip: 0x%016llx slb: 0x%08x",
		  __entry->rip, __entry->slb)
);

#define KVM_EMUL_INSN_F_CR0_PE (1 << 0)
#define KVM_EMUL_INSN_F_EFL_VM (1 << 1)
#define KVM_EMUL_INSN_F_CS_D   (1 << 2)
#define KVM_EMUL_INSN_F_CS_L   (1 << 3)

#define kvm_trace_symbol_emul_flags	                  \
	{ 0,   			    "real" },		  \
	{ KVM_EMUL_INSN_F_CR0_PE			  \
	  | KVM_EMUL_INSN_F_EFL_VM, "vm16" },		  \
	{ KVM_EMUL_INSN_F_CR0_PE,   "prot16" },		  \
	{ KVM_EMUL_INSN_F_CR0_PE			  \
	  | KVM_EMUL_INSN_F_CS_D,   "prot32" },		  \
	{ KVM_EMUL_INSN_F_CR0_PE			  \
	  | KVM_EMUL_INSN_F_CS_L,   "prot64" }

#define kei_decode_mode(mode) ({			\
	u8 flags = 0xff;				\
	switch (mode) {					\
	case X86EMUL_MODE_REAL:				\
		flags = 0;				\
		break;					\
	case X86EMUL_MODE_VM86:				\
		flags = KVM_EMUL_INSN_F_EFL_VM;		\
		break;					\
	case X86EMUL_MODE_PROT16:			\
		flags = KVM_EMUL_INSN_F_CR0_PE;		\
		break;					\
	case X86EMUL_MODE_PROT32:			\
		flags = KVM_EMUL_INSN_F_CR0_PE		\
			| KVM_EMUL_INSN_F_CS_D;		\
		break;					\
	case X86EMUL_MODE_PROT64:			\
		flags = KVM_EMUL_INSN_F_CR0_PE		\
			| KVM_EMUL_INSN_F_CS_L;		\
		break;					\
	}						\
	flags;						\
	})

TRACE_EVENT(kvm_emulate_insn,
	TP_PROTO(struct kvm_vcpu *vcpu, __u8 failed),
	TP_ARGS(vcpu, failed),

	TP_STRUCT__entry(
		__field(    __u64, rip                       )
		__field(    __u32, csbase                    )
		__field(    __u8,  len                       )
		__array(    __u8,  insn,    15	             )
		__field(    __u8,  flags       	   	     )
		__field(    __u8,  failed                    )
		),

	TP_fast_assign(
		__entry->csbase = static_call(kvm_x86_get_segment_base)(vcpu, VCPU_SREG_CS);
		__entry->len = vcpu->arch.emulate_ctxt->fetch.ptr
			       - vcpu->arch.emulate_ctxt->fetch.data;
		__entry->rip = vcpu->arch.emulate_ctxt->_eip - __entry->len;
		memcpy(__entry->insn,
		       vcpu->arch.emulate_ctxt->fetch.data,
		       15);
		__entry->flags = kei_decode_mode(vcpu->arch.emulate_ctxt->mode);
		__entry->failed = failed;
		),

	TP_printk("%x:%llx:%s (%s)%s",
		  __entry->csbase, __entry->rip,
		  __print_hex(__entry->insn, __entry->len),
		  __print_symbolic(__entry->flags,
				   kvm_trace_symbol_emul_flags),
		  __entry->failed ? " failed" : ""
		)
	);

#define trace_kvm_emulate_insn_start(vcpu) trace_kvm_emulate_insn(vcpu, 0)
#define trace_kvm_emulate_insn_failed(vcpu) trace_kvm_emulate_insn(vcpu, 1)

TRACE_EVENT(
	vcpu_match_mmio,
	TP_PROTO(gva_t gva, gpa_t gpa, bool write, bool gpa_match),
	TP_ARGS(gva, gpa, write, gpa_match),

	TP_STRUCT__entry(
		__field(gva_t, gva)
		__field(gpa_t, gpa)
		__field(bool, write)
		__field(bool, gpa_match)
		),

	TP_fast_assign(
		__entry->gva = gva;
		__entry->gpa = gpa;
		__entry->write = write;
		__entry->gpa_match = gpa_match
		),

	TP_printk("gva %#lx gpa %#llx %s %s", __entry->gva, __entry->gpa,
		  __entry->write ? "Write" : "Read",
		  __entry->gpa_match ? "GPA" : "GVA")
);

TRACE_EVENT(kvm_write_tsc_offset,
	TP_PROTO(unsigned int vcpu_id, __u64 previous_tsc_offset,
		 __u64 next_tsc_offset),
	TP_ARGS(vcpu_id, previous_tsc_offset, next_tsc_offset),

	TP_STRUCT__entry(
		__field( unsigned int,	vcpu_id				)
		__field(	__u64,	previous_tsc_offset		)
		__field(	__u64,	next_tsc_offset			)
	),

	TP_fast_assign(
		__entry->vcpu_id		= vcpu_id;
		__entry->previous_tsc_offset	= previous_tsc_offset;
		__entry->next_tsc_offset	= next_tsc_offset;
	),

	TP_printk("vcpu=%u prev=%llu next=%llu", __entry->vcpu_id,
		  __entry->previous_tsc_offset, __entry->next_tsc_offset)
);

#ifdef CONFIG_X86_64

#define host_clocks					\
	{VDSO_CLOCKMODE_NONE, "none"},			\
	{VDSO_CLOCKMODE_TSC,  "tsc"}			\

TRACE_EVENT(kvm_update_master_clock,
	TP_PROTO(bool use_master_clock, unsigned int host_clock, bool offset_matched),
	TP_ARGS(use_master_clock, host_clock, offset_matched),

	TP_STRUCT__entry(
		__field(		bool,	use_master_clock	)
		__field(	unsigned int,	host_clock		)
		__field(		bool,	offset_matched		)
	),

	TP_fast_assign(
		__entry->use_master_clock	= use_master_clock;
		__entry->host_clock		= host_clock;
		__entry->offset_matched		= offset_matched;
	),

	TP_printk("masterclock %d hostclock %s offsetmatched %u",
		  __entry->use_master_clock,
		  __print_symbolic(__entry->host_clock, host_clocks),
		  __entry->offset_matched)
);

TRACE_EVENT(kvm_track_tsc,
	TP_PROTO(unsigned int vcpu_id, unsigned int nr_matched,
		 unsigned int online_vcpus, bool use_master_clock,
		 unsigned int host_clock),
	TP_ARGS(vcpu_id, nr_matched, online_vcpus, use_master_clock,
		host_clock),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id			)
		__field(	unsigned int,	nr_vcpus_matched_tsc	)
		__field(	unsigned int,	online_vcpus		)
		__field(	bool,		use_master_clock	)
		__field(	unsigned int,	host_clock		)
	),

	TP_fast_assign(
		__entry->vcpu_id		= vcpu_id;
		__entry->nr_vcpus_matched_tsc	= nr_matched;
		__entry->online_vcpus		= online_vcpus;
		__entry->use_master_clock	= use_master_clock;
		__entry->host_clock		= host_clock;
	),

	TP_printk("vcpu_id %u masterclock %u offsetmatched %u nr_online %u"
		  " hostclock %s",
		  __entry->vcpu_id, __entry->use_master_clock,
		  __entry->nr_vcpus_matched_tsc, __entry->online_vcpus,
		  __print_symbolic(__entry->host_clock, host_clocks))
);

#endif /* CONFIG_X86_64 */

/*
 * Tracepoint for PML full VMEXIT.
 */
TRACE_EVENT(kvm_pml_full,
	TP_PROTO(unsigned int vcpu_id),
	TP_ARGS(vcpu_id),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id			)
	),

	TP_fast_assign(
		__entry->vcpu_id		= vcpu_id;
	),

	TP_printk("vcpu %d: PML full", __entry->vcpu_id)
);

TRACE_EVENT(kvm_ple_window_update,
	TP_PROTO(unsigned int vcpu_id, unsigned int new, unsigned int old),
	TP_ARGS(vcpu_id, new, old),

	TP_STRUCT__entry(
		__field(        unsigned int,   vcpu_id         )
		__field(        unsigned int,       new         )
		__field(        unsigned int,       old         )
	),

	TP_fast_assign(
		__entry->vcpu_id        = vcpu_id;
		__entry->new            = new;
		__entry->old            = old;
	),

	TP_printk("vcpu %u old %u new %u (%s)",
	          __entry->vcpu_id, __entry->old, __entry->new,
		  __entry->old < __entry->new ? "growed" : "shrinked")
);

TRACE_EVENT(kvm_pvclock_update,
	TP_PROTO(unsigned int vcpu_id, struct pvclock_vcpu_time_info *pvclock),
	TP_ARGS(vcpu_id, pvclock),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id			)
		__field(	__u32,		version			)
		__field(	__u64,		tsc_timestamp		)
		__field(	__u64,		system_time		)
		__field(	__u32,		tsc_to_system_mul	)
		__field(	__s8,		tsc_shift		)
		__field(	__u8,		flags			)
	),

	TP_fast_assign(
		__entry->vcpu_id	   = vcpu_id;
		__entry->version	   = pvclock->version;
		__entry->tsc_timestamp	   = pvclock->tsc_timestamp;
		__entry->system_time	   = pvclock->system_time;
		__entry->tsc_to_system_mul = pvclock->tsc_to_system_mul;
		__entry->tsc_shift	   = pvclock->tsc_shift;
		__entry->flags		   = pvclock->flags;
	),

	TP_printk("vcpu_id %u, pvclock { version %u, tsc_timestamp 0x%llx, "
		  "system_time 0x%llx, tsc_to_system_mul 0x%x, tsc_shift %d, "
		  "flags 0x%x }",
		  __entry->vcpu_id,
		  __entry->version,
		  __entry->tsc_timestamp,
		  __entry->system_time,
		  __entry->tsc_to_system_mul,
		  __entry->tsc_shift,
		  __entry->flags)
);

TRACE_EVENT(kvm_wait_lapic_expire,
	TP_PROTO(unsigned int vcpu_id, s64 delta),
	TP_ARGS(vcpu_id, delta),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id		)
		__field(	s64,		delta		)
	),

	TP_fast_assign(
		__entry->vcpu_id	   = vcpu_id;
		__entry->delta             = delta;
	),

	TP_printk("vcpu %u: delta %lld (%s)",
		  __entry->vcpu_id,
		  __entry->delta,
		  __entry->delta < 0 ? "early" : "late")
);

TRACE_EVENT(kvm_smm_transition,
	TP_PROTO(unsigned int vcpu_id, u64 smbase, bool entering),
	TP_ARGS(vcpu_id, smbase, entering),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id		)
		__field(	u64,		smbase		)
		__field(	bool,		entering	)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu_id;
		__entry->smbase		= smbase;
		__entry->entering	= entering;
	),

	TP_printk("vcpu %u: %s SMM, smbase 0x%llx",
		  __entry->vcpu_id,
		  __entry->entering ? "entering" : "leaving",
		  __entry->smbase)
);

/*
 * Tracepoint for VT-d posted-interrupts.
 */
TRACE_EVENT(kvm_pi_irte_update,
	TP_PROTO(unsigned int host_irq, unsigned int vcpu_id,
		 unsigned int gsi, unsigned int gvec,
		 u64 pi_desc_addr, bool set),
	TP_ARGS(host_irq, vcpu_id, gsi, gvec, pi_desc_addr, set),

	TP_STRUCT__entry(
		__field(	unsigned int,	host_irq	)
		__field(	unsigned int,	vcpu_id		)
		__field(	unsigned int,	gsi		)
		__field(	unsigned int,	gvec		)
		__field(	u64,		pi_desc_addr	)
		__field(	bool,		set		)
	),

	TP_fast_assign(
		__entry->host_irq	= host_irq;
		__entry->vcpu_id	= vcpu_id;
		__entry->gsi		= gsi;
		__entry->gvec		= gvec;
		__entry->pi_desc_addr	= pi_desc_addr;
		__entry->set		= set;
	),

	TP_printk("VT-d PI is %s for irq %u, vcpu %u, gsi: 0x%x, "
		  "gvec: 0x%x, pi_desc_addr: 0x%llx",
		  __entry->set ? "enabled and being updated" : "disabled",
		  __entry->host_irq,
		  __entry->vcpu_id,
		  __entry->gsi,
		  __entry->gvec,
		  __entry->pi_desc_addr)
);

/*
 * Tracepoint for kvm_hv_notify_acked_sint.
 */
TRACE_EVENT(kvm_hv_notify_acked_sint,
	TP_PROTO(int vcpu_id, u32 sint),
	TP_ARGS(vcpu_id, sint),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(u32, sint)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->sint = sint;
	),

	TP_printk("vcpu_id %d sint %u", __entry->vcpu_id, __entry->sint)
);

/*
 * Tracepoint for synic_set_irq.
 */
TRACE_EVENT(kvm_hv_synic_set_irq,
	TP_PROTO(int vcpu_id, u32 sint, int vector, int ret),
	TP_ARGS(vcpu_id, sint, vector, ret),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(u32, sint)
		__field(int, vector)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->sint = sint;
		__entry->vector = vector;
		__entry->ret = ret;
	),

	TP_printk("vcpu_id %d sint %u vector %d ret %d",
		  __entry->vcpu_id, __entry->sint, __entry->vector,
		  __entry->ret)
);

/*
 * Tracepoint for kvm_hv_synic_send_eoi.
 */
TRACE_EVENT(kvm_hv_synic_send_eoi,
	TP_PROTO(int vcpu_id, int vector),
	TP_ARGS(vcpu_id, vector),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(u32, sint)
		__field(int, vector)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->vector	= vector;
	),

	TP_printk("vcpu_id %d vector %d", __entry->vcpu_id, __entry->vector)
);

/*
 * Tracepoint for synic_set_msr.
 */
TRACE_EVENT(kvm_hv_synic_set_msr,
	TP_PROTO(int vcpu_id, u32 msr, u64 data, bool host),
	TP_ARGS(vcpu_id, msr, data, host),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(u32, msr)
		__field(u64, data)
		__field(bool, host)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->msr = msr;
		__entry->data = data;
		__entry->host = host
	),

	TP_printk("vcpu_id %d msr 0x%x data 0x%llx host %d",
		  __entry->vcpu_id, __entry->msr, __entry->data, __entry->host)
);

/*
 * Tracepoint for stimer_set_config.
 */
TRACE_EVENT(kvm_hv_stimer_set_config,
	TP_PROTO(int vcpu_id, int timer_index, u64 config, bool host),
	TP_ARGS(vcpu_id, timer_index, config, host),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(int, timer_index)
		__field(u64, config)
		__field(bool, host)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->timer_index = timer_index;
		__entry->config = config;
		__entry->host = host;
	),

	TP_printk("vcpu_id %d timer %d config 0x%llx host %d",
		  __entry->vcpu_id, __entry->timer_index, __entry->config,
		  __entry->host)
);

/*
 * Tracepoint for stimer_set_count.
 */
TRACE_EVENT(kvm_hv_stimer_set_count,
	TP_PROTO(int vcpu_id, int timer_index, u64 count, bool host),
	TP_ARGS(vcpu_id, timer_index, count, host),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(int, timer_index)
		__field(u64, count)
		__field(bool, host)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->timer_index = timer_index;
		__entry->count = count;
		__entry->host = host;
	),

	TP_printk("vcpu_id %d timer %d count %llu host %d",
		  __entry->vcpu_id, __entry->timer_index, __entry->count,
		  __entry->host)
);

/*
 * Tracepoint for stimer_start(periodic timer case).
 */
TRACE_EVENT(kvm_hv_stimer_start_periodic,
	TP_PROTO(int vcpu_id, int timer_index, u64 time_now, u64 exp_time),
	TP_ARGS(vcpu_id, timer_index, time_now, exp_time),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(int, timer_index)
		__field(u64, time_now)
		__field(u64, exp_time)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->timer_index = timer_index;
		__entry->time_now = time_now;
		__entry->exp_time = exp_time;
	),

	TP_printk("vcpu_id %d timer %d time_now %llu exp_time %llu",
		  __entry->vcpu_id, __entry->timer_index, __entry->time_now,
		  __entry->exp_time)
);

/*
 * Tracepoint for stimer_start(one-shot timer case).
 */
TRACE_EVENT(kvm_hv_stimer_start_one_shot,
	TP_PROTO(int vcpu_id, int timer_index, u64 time_now, u64 count),
	TP_ARGS(vcpu_id, timer_index, time_now, count),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(int, timer_index)
		__field(u64, time_now)
		__field(u64, count)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->timer_index = timer_index;
		__entry->time_now = time_now;
		__entry->count = count;
	),

	TP_printk("vcpu_id %d timer %d time_now %llu count %llu",
		  __entry->vcpu_id, __entry->timer_index, __entry->time_now,
		  __entry->count)
);

/*
 * Tracepoint for stimer_timer_callback.
 */
TRACE_EVENT(kvm_hv_stimer_callback,
	TP_PROTO(int vcpu_id, int timer_index),
	TP_ARGS(vcpu_id, timer_index),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(int, timer_index)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->timer_index = timer_index;
	),

	TP_printk("vcpu_id %d timer %d",
		  __entry->vcpu_id, __entry->timer_index)
);

/*
 * Tracepoint for stimer_expiration.
 */
TRACE_EVENT(kvm_hv_stimer_expiration,
	TP_PROTO(int vcpu_id, int timer_index, int direct, int msg_send_result),
	TP_ARGS(vcpu_id, timer_index, direct, msg_send_result),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(int, timer_index)
		__field(int, direct)
		__field(int, msg_send_result)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->timer_index = timer_index;
		__entry->direct = direct;
		__entry->msg_send_result = msg_send_result;
	),

	TP_printk("vcpu_id %d timer %d direct %d send result %d",
		  __entry->vcpu_id, __entry->timer_index,
		  __entry->direct, __entry->msg_send_result)
);

/*
 * Tracepoint for stimer_cleanup.
 */
TRACE_EVENT(kvm_hv_stimer_cleanup,
	TP_PROTO(int vcpu_id, int timer_index),
	TP_ARGS(vcpu_id, timer_index),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(int, timer_index)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->timer_index = timer_index;
	),

	TP_printk("vcpu_id %d timer %d",
		  __entry->vcpu_id, __entry->timer_index)
);

TRACE_EVENT(kvm_apicv_inhibit_changed,
	    TP_PROTO(int reason, bool set, unsigned long inhibits),
	    TP_ARGS(reason, set, inhibits),

	TP_STRUCT__entry(
		__field(int, reason)
		__field(bool, set)
		__field(unsigned long, inhibits)
	),

	TP_fast_assign(
		__entry->reason = reason;
		__entry->set = set;
		__entry->inhibits = inhibits;
	),

	TP_printk("%s reason=%u, inhibits=0x%lx",
		  __entry->set ? "set" : "cleared",
		  __entry->reason, __entry->inhibits)
);

TRACE_EVENT(kvm_apicv_accept_irq,
	    TP_PROTO(__u32 apicid, __u16 dm, __u16 tm, __u8 vec),
	    TP_ARGS(apicid, dm, tm, vec),

	TP_STRUCT__entry(
		__field(	__u32,		apicid		)
		__field(	__u16,		dm		)
		__field(	__u16,		tm		)
		__field(	__u8,		vec		)
	),

	TP_fast_assign(
		__entry->apicid		= apicid;
		__entry->dm		= dm;
		__entry->tm		= tm;
		__entry->vec		= vec;
	),

	TP_printk("apicid %x vec %u (%s|%s)",
		  __entry->apicid, __entry->vec,
		  __print_symbolic((__entry->dm >> 8 & 0x7), kvm_deliver_mode),
		  __entry->tm ? "level" : "edge")
);

/*
 * Tracepoint for AMD AVIC
 */
TRACE_EVENT(kvm_avic_incomplete_ipi,
	    TP_PROTO(u32 vcpu, u32 icrh, u32 icrl, u32 id, u32 index),
	    TP_ARGS(vcpu, icrh, icrl, id, index),

	TP_STRUCT__entry(
		__field(u32, vcpu)
		__field(u32, icrh)
		__field(u32, icrl)
		__field(u32, id)
		__field(u32, index)
	),

	TP_fast_assign(
		__entry->vcpu = vcpu;
		__entry->icrh = icrh;
		__entry->icrl = icrl;
		__entry->id = id;
		__entry->index = index;
	),

	TP_printk("vcpu=%u, icrh:icrl=%#010x:%08x, id=%u, index=%u",
		  __entry->vcpu, __entry->icrh, __entry->icrl,
		  __entry->id, __entry->index)
);

TRACE_EVENT(kvm_avic_unaccelerated_access,
	    TP_PROTO(u32 vcpu, u32 offset, bool ft, bool rw, u32 vec),
	    TP_ARGS(vcpu, offset, ft, rw, vec),

	TP_STRUCT__entry(
		__field(u32, vcpu)
		__field(u32, offset)
		__field(bool, ft)
		__field(bool, rw)
		__field(u32, vec)
	),

	TP_fast_assign(
		__entry->vcpu = vcpu;
		__entry->offset = offset;
		__entry->ft = ft;
		__entry->rw = rw;
		__entry->vec = vec;
	),

	TP_printk("vcpu=%u, offset=%#x(%s), %s, %s, vec=%#x",
		  __entry->vcpu,
		  __entry->offset,
		  __print_symbolic(__entry->offset, kvm_trace_symbol_apic),
		  __entry->ft ? "trap" : "fault",
		  __entry->rw ? "write" : "read",
		  __entry->vec)
);

TRACE_EVENT(kvm_avic_ga_log,
	    TP_PROTO(u32 vmid, u32 vcpuid),
	    TP_ARGS(vmid, vcpuid),

	TP_STRUCT__entry(
		__field(u32, vmid)
		__field(u32, vcpuid)
	),

	TP_fast_assign(
		__entry->vmid = vmid;
		__entry->vcpuid = vcpuid;
	),

	TP_printk("vmid=%u, vcpuid=%u",
		  __entry->vmid, __entry->vcpuid)
);

TRACE_EVENT(kvm_avic_kick_vcpu_slowpath,
	    TP_PROTO(u32 icrh, u32 icrl, u32 index),
	    TP_ARGS(icrh, icrl, index),

	TP_STRUCT__entry(
		__field(u32, icrh)
		__field(u32, icrl)
		__field(u32, index)
	),

	TP_fast_assign(
		__entry->icrh = icrh;
		__entry->icrl = icrl;
		__entry->index = index;
	),

	TP_printk("icrh:icrl=%#08x:%08x, index=%u",
		  __entry->icrh, __entry->icrl, __entry->index)
);

TRACE_EVENT(kvm_avic_doorbell,
	    TP_PROTO(u32 vcpuid, u32 apicid),
	    TP_ARGS(vcpuid, apicid),

	TP_STRUCT__entry(
		__field(u32, vcpuid)
		__field(u32, apicid)
	),

	TP_fast_assign(
		__entry->vcpuid = vcpuid;
		__entry->apicid = apicid;
	),

	TP_printk("vcpuid=%u, apicid=%u",
		  __entry->vcpuid, __entry->apicid)
);

TRACE_EVENT(kvm_hv_timer_state,
		TP_PROTO(unsigned int vcpu_id, unsigned int hv_timer_in_use),
		TP_ARGS(vcpu_id, hv_timer_in_use),
		TP_STRUCT__entry(
			__field(unsigned int, vcpu_id)
			__field(unsigned int, hv_timer_in_use)
			),
		TP_fast_assign(
			__entry->vcpu_id = vcpu_id;
			__entry->hv_timer_in_use = hv_timer_in_use;
			),
		TP_printk("vcpu_id %x hv_timer %x",
			__entry->vcpu_id,
			__entry->hv_timer_in_use)
);

/*
 * Tracepoint for kvm_hv_flush_tlb.
 */
TRACE_EVENT(kvm_hv_flush_tlb,
	TP_PROTO(u64 processor_mask, u64 address_space, u64 flags, bool guest_mode),
	TP_ARGS(processor_mask, address_space, flags, guest_mode),

	TP_STRUCT__entry(
		__field(u64, processor_mask)
		__field(u64, address_space)
		__field(u64, flags)
		__field(bool, guest_mode)
	),

	TP_fast_assign(
		__entry->processor_mask = processor_mask;
		__entry->address_space = address_space;
		__entry->flags = flags;
		__entry->guest_mode = guest_mode;
	),

	TP_printk("processor_mask 0x%llx address_space 0x%llx flags 0x%llx %s",
		  __entry->processor_mask, __entry->address_space,
		  __entry->flags, __entry->guest_mode ? "(L2)" : "")
);

/*
 * Tracepoint for kvm_hv_flush_tlb_ex.
 */
TRACE_EVENT(kvm_hv_flush_tlb_ex,
	TP_PROTO(u64 valid_bank_mask, u64 format, u64 address_space, u64 flags, bool guest_mode),
	TP_ARGS(valid_bank_mask, format, address_space, flags, guest_mode),

	TP_STRUCT__entry(
		__field(u64, valid_bank_mask)
		__field(u64, format)
		__field(u64, address_space)
		__field(u64, flags)
		__field(bool, guest_mode)
	),

	TP_fast_assign(
		__entry->valid_bank_mask = valid_bank_mask;
		__entry->format = format;
		__entry->address_space = address_space;
		__entry->flags = flags;
		__entry->guest_mode = guest_mode;
	),

	TP_printk("valid_bank_mask 0x%llx format 0x%llx "
		  "address_space 0x%llx flags 0x%llx %s",
		  __entry->valid_bank_mask, __entry->format,
		  __entry->address_space, __entry->flags,
		  __entry->guest_mode ? "(L2)" : "")
);

/*
 * Tracepoints for kvm_hv_send_ipi.
 */
TRACE_EVENT(kvm_hv_send_ipi,
	TP_PROTO(u32 vector, u64 processor_mask),
	TP_ARGS(vector, processor_mask),

	TP_STRUCT__entry(
		__field(u32, vector)
		__field(u64, processor_mask)
	),

	TP_fast_assign(
		__entry->vector = vector;
		__entry->processor_mask = processor_mask;
	),

	TP_printk("vector %x processor_mask 0x%llx",
		  __entry->vector, __entry->processor_mask)
);

TRACE_EVENT(kvm_hv_send_ipi_ex,
	TP_PROTO(u32 vector, u64 format, u64 valid_bank_mask),
	TP_ARGS(vector, format, valid_bank_mask),

	TP_STRUCT__entry(
		__field(u32, vector)
		__field(u64, format)
		__field(u64, valid_bank_mask)
	),

	TP_fast_assign(
		__entry->vector = vector;
		__entry->format = format;
		__entry->valid_bank_mask = valid_bank_mask;
	),

	TP_printk("vector %x format %llx valid_bank_mask 0x%llx",
		  __entry->vector, __entry->format,
		  __entry->valid_bank_mask)
);

TRACE_EVENT(kvm_pv_tlb_flush,
	TP_PROTO(unsigned int vcpu_id, bool need_flush_tlb),
	TP_ARGS(vcpu_id, need_flush_tlb),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id		)
		__field(	bool,	need_flush_tlb		)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu_id;
		__entry->need_flush_tlb = need_flush_tlb;
	),

	TP_printk("vcpu %u need_flush_tlb %s", __entry->vcpu_id,
		__entry->need_flush_tlb ? "true" : "false")
);

/*
 * Tracepoint for failed nested VMX VM-Enter.
 */
TRACE_EVENT(kvm_nested_vmenter_failed,
	TP_PROTO(const char *msg, u32 err),
	TP_ARGS(msg, err),

	TP_STRUCT__entry(
		__string(msg, msg)
		__field(u32, err)
	),

	TP_fast_assign(
		__assign_str(msg, msg);
		__entry->err = err;
	),

	TP_printk("%s%s", __get_str(msg), !__entry->err ? "" :
		__print_symbolic(__entry->err, VMX_VMENTER_INSTRUCTION_ERRORS))
);

/*
 * Tracepoint for syndbg_set_msr.
 */
TRACE_EVENT(kvm_hv_syndbg_set_msr,
	TP_PROTO(int vcpu_id, u32 vp_index, u32 msr, u64 data),
	TP_ARGS(vcpu_id, vp_index, msr, data),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(u32, vp_index)
		__field(u32, msr)
		__field(u64, data)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->vp_index = vp_index;
		__entry->msr = msr;
		__entry->data = data;
	),

	TP_printk("vcpu_id %d vp_index %u msr 0x%x data 0x%llx",
		  __entry->vcpu_id, __entry->vp_index, __entry->msr,
		  __entry->data)
);

/*
 * Tracepoint for syndbg_get_msr.
 */
TRACE_EVENT(kvm_hv_syndbg_get_msr,
	TP_PROTO(int vcpu_id, u32 vp_index, u32 msr, u64 data),
	TP_ARGS(vcpu_id, vp_index, msr, data),

	TP_STRUCT__entry(
		__field(int, vcpu_id)
		__field(u32, vp_index)
		__field(u32, msr)
		__field(u64, data)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu_id;
		__entry->vp_index = vp_index;
		__entry->msr = msr;
		__entry->data = data;
	),

	TP_printk("vcpu_id %d vp_index %u msr 0x%x data 0x%llx",
		  __entry->vcpu_id, __entry->vp_index, __entry->msr,
		  __entry->data)
);

/*
 * Tracepoint for the start of VMGEXIT processing
 */
TRACE_EVENT(kvm_vmgexit_enter,
	TP_PROTO(unsigned int vcpu_id, struct ghcb *ghcb),
	TP_ARGS(vcpu_id, ghcb),

	TP_STRUCT__entry(
		__field(unsigned int, vcpu_id)
		__field(u64, exit_reason)
		__field(u64, info1)
		__field(u64, info2)
	),

	TP_fast_assign(
		__entry->vcpu_id     = vcpu_id;
		__entry->exit_reason = ghcb->save.sw_exit_code;
		__entry->info1       = ghcb->save.sw_exit_info_1;
		__entry->info2       = ghcb->save.sw_exit_info_2;
	),

	TP_printk("vcpu %u, exit_reason %llx, exit_info1 %llx, exit_info2 %llx",
		  __entry->vcpu_id, __entry->exit_reason,
		  __entry->info1, __entry->info2)
);

/*
 * Tracepoint for the end of VMGEXIT processing
 */
TRACE_EVENT(kvm_vmgexit_exit,
	TP_PROTO(unsigned int vcpu_id, struct ghcb *ghcb),
	TP_ARGS(vcpu_id, ghcb),

	TP_STRUCT__entry(
		__field(unsigned int, vcpu_id)
		__field(u64, exit_reason)
		__field(u64, info1)
		__field(u64, info2)
	),

	TP_fast_assign(
		__entry->vcpu_id     = vcpu_id;
		__entry->exit_reason = ghcb->save.sw_exit_code;
		__entry->info1       = ghcb->save.sw_exit_info_1;
		__entry->info2       = ghcb->save.sw_exit_info_2;
	),

	TP_printk("vcpu %u, exit_reason %llx, exit_info1 %llx, exit_info2 %llx",
		  __entry->vcpu_id, __entry->exit_reason,
		  __entry->info1, __entry->info2)
);

/*
 * Tracepoint for the start of VMGEXIT MSR procotol processing
 */
TRACE_EVENT(kvm_vmgexit_msr_protocol_enter,
	TP_PROTO(unsigned int vcpu_id, u64 ghcb_gpa),
	TP_ARGS(vcpu_id, ghcb_gpa),

	TP_STRUCT__entry(
		__field(unsigned int, vcpu_id)
		__field(u64, ghcb_gpa)
	),

	TP_fast_assign(
		__entry->vcpu_id  = vcpu_id;
		__entry->ghcb_gpa = ghcb_gpa;
	),

	TP_printk("vcpu %u, ghcb_gpa %016llx",
		  __entry->vcpu_id, __entry->ghcb_gpa)
);

/*
 * Tracepoint for the end of VMGEXIT MSR procotol processing
 */
TRACE_EVENT(kvm_vmgexit_msr_protocol_exit,
	TP_PROTO(unsigned int vcpu_id, u64 ghcb_gpa, int result),
	TP_ARGS(vcpu_id, ghcb_gpa, result),

	TP_STRUCT__entry(
		__field(unsigned int, vcpu_id)
		__field(u64, ghcb_gpa)
		__field(int, result)
	),

	TP_fast_assign(
		__entry->vcpu_id  = vcpu_id;
		__entry->ghcb_gpa = ghcb_gpa;
		__entry->result   = result;
	),

	TP_printk("vcpu %u, ghcb_gpa %016llx, result %d",
		  __entry->vcpu_id, __entry->ghcb_gpa, __entry->result)
);

#endif /* _TRACE_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../arch/x86/kvm
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
