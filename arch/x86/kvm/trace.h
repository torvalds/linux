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
	TP_PROTO(unsigned int vcpu_id),
	TP_ARGS(vcpu_id),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id		)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu_id;
	),

	TP_printk("vcpu %u", __entry->vcpu_id)
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
	TP_PROTO(__u16 code, bool fast, __u16 rep_cnt, __u16 rep_idx,
		 __u64 ingpa, __u64 outgpa),
	TP_ARGS(code, fast, rep_cnt, rep_idx, ingpa, outgpa),

	TP_STRUCT__entry(
		__field(	__u16,		rep_cnt		)
		__field(	__u16,		rep_idx		)
		__field(	__u64,		ingpa		)
		__field(	__u64,		outgpa		)
		__field(	__u16, 		code		)
		__field(	bool,		fast		)
	),

	TP_fast_assign(
		__entry->rep_cnt	= rep_cnt;
		__entry->rep_idx	= rep_idx;
		__entry->ingpa		= ingpa;
		__entry->outgpa		= outgpa;
		__entry->code		= code;
		__entry->fast		= fast;
	),

	TP_printk("code 0x%x %s cnt 0x%x idx 0x%x in 0x%llx out 0x%llx",
		  __entry->code, __entry->fast ? "fast" : "slow",
		  __entry->rep_cnt, __entry->rep_idx,  __entry->ingpa,
		  __entry->outgpa)
);

/*
 * Tracepoint for PIO.
 */

#define KVM_PIO_IN   0
#define KVM_PIO_OUT  1

TRACE_EVENT(kvm_pio,
	TP_PROTO(unsigned int rw, unsigned int port, unsigned int size,
		 unsigned int count, void *data),
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
	TP_PROTO(unsigned int function, unsigned long rax, unsigned long rbx,
		 unsigned long rcx, unsigned long rdx),
	TP_ARGS(function, rax, rbx, rcx, rdx),

	TP_STRUCT__entry(
		__field(	unsigned int,	function	)
		__field(	unsigned long,	rax		)
		__field(	unsigned long,	rbx		)
		__field(	unsigned long,	rcx		)
		__field(	unsigned long,	rdx		)
	),

	TP_fast_assign(
		__entry->function	= function;
		__entry->rax		= rax;
		__entry->rbx		= rbx;
		__entry->rcx		= rcx;
		__entry->rdx		= rdx;
	),

	TP_printk("func %x rax %lx rbx %lx rcx %lx rdx %lx",
		  __entry->function, __entry->rax,
		  __entry->rbx, __entry->rcx, __entry->rdx)
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
	TP_PROTO(unsigned int rw, unsigned int reg, unsigned int val),
	TP_ARGS(rw, reg, val),

	TP_STRUCT__entry(
		__field(	unsigned int,	rw		)
		__field(	unsigned int,	reg		)
		__field(	unsigned int,	val		)
	),

	TP_fast_assign(
		__entry->rw		= rw;
		__entry->reg		= reg;
		__entry->val		= val;
	),

	TP_printk("apic_%s %s = 0x%x",
		  __entry->rw ? "write" : "read",
		  __print_symbolic(__entry->reg, kvm_trace_symbol_apic),
		  __entry->val)
);

#define trace_kvm_apic_read(reg, val)		trace_kvm_apic(0, reg, val)
#define trace_kvm_apic_write(reg, val)		trace_kvm_apic(1, reg, val)

#define KVM_ISA_VMX   1
#define KVM_ISA_SVM   2

/*
 * Tracepoint for kvm guest exit:
 */
TRACE_EVENT(kvm_exit,
	TP_PROTO(unsigned int exit_reason, struct kvm_vcpu *vcpu, u32 isa),
	TP_ARGS(exit_reason, vcpu, isa),

	TP_STRUCT__entry(
		__field(	unsigned int,	exit_reason	)
		__field(	unsigned long,	guest_rip	)
		__field(	u32,	        isa             )
		__field(	u64,	        info1           )
		__field(	u64,	        info2           )
	),

	TP_fast_assign(
		__entry->exit_reason	= exit_reason;
		__entry->guest_rip	= kvm_rip_read(vcpu);
		__entry->isa            = isa;
		kvm_x86_ops->get_exit_info(vcpu, &__entry->info1,
					   &__entry->info2);
	),

	TP_printk("reason %s rip 0x%lx info %llx %llx",
		 (__entry->isa == KVM_ISA_VMX) ?
		 __print_symbolic(__entry->exit_reason, VMX_EXIT_REASONS) :
		 __print_symbolic(__entry->exit_reason, SVM_EXIT_REASONS),
		 __entry->guest_rip, __entry->info1, __entry->info2)
);

/*
 * Tracepoint for kvm interrupt injection:
 */
TRACE_EVENT(kvm_inj_virq,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq),

	TP_STRUCT__entry(
		__field(	unsigned int,	irq		)
	),

	TP_fast_assign(
		__entry->irq		= irq;
	),

	TP_printk("irq %u", __entry->irq)
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
	TP_PROTO(unsigned exception, bool has_error, unsigned error_code),
	TP_ARGS(exception, has_error, error_code),

	TP_STRUCT__entry(
		__field(	u8,	exception	)
		__field(	u8,	has_error	)
		__field(	u32,	error_code	)
	),

	TP_fast_assign(
		__entry->exception	= exception;
		__entry->has_error	= has_error;
		__entry->error_code	= error_code;
	),

	TP_printk("%s (0x%x)",
		  __print_symbolic(__entry->exception, kvm_trace_sym_exc),
		  /* FIXME: don't print error_code if not present */
		  __entry->has_error ? __entry->error_code : 0)
);

/*
 * Tracepoint for page fault.
 */
TRACE_EVENT(kvm_page_fault,
	TP_PROTO(unsigned long fault_address, unsigned int error_code),
	TP_ARGS(fault_address, error_code),

	TP_STRUCT__entry(
		__field(	unsigned long,	fault_address	)
		__field(	unsigned int,	error_code	)
	),

	TP_fast_assign(
		__entry->fault_address	= fault_address;
		__entry->error_code	= error_code;
	),

	TP_printk("address %lx error_code %x",
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
	    TP_PROTO(__u32 apicid, __u16 dm, __u8 tm, __u8 vec),
	    TP_ARGS(apicid, dm, tm, vec),

	TP_STRUCT__entry(
		__field(	__u32,		apicid		)
		__field(	__u16,		dm		)
		__field(	__u8,		tm		)
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
TRACE_EVENT(kvm_nested_vmrun,
	    TP_PROTO(__u64 rip, __u64 vmcb, __u64 nested_rip, __u32 int_ctl,
		     __u32 event_inj, bool npt),
	    TP_ARGS(rip, vmcb, nested_rip, int_ctl, event_inj, npt),

	TP_STRUCT__entry(
		__field(	__u64,		rip		)
		__field(	__u64,		vmcb		)
		__field(	__u64,		nested_rip	)
		__field(	__u32,		int_ctl		)
		__field(	__u32,		event_inj	)
		__field(	bool,		npt		)
	),

	TP_fast_assign(
		__entry->rip		= rip;
		__entry->vmcb		= vmcb;
		__entry->nested_rip	= nested_rip;
		__entry->int_ctl	= int_ctl;
		__entry->event_inj	= event_inj;
		__entry->npt		= npt;
	),

	TP_printk("rip: 0x%016llx vmcb: 0x%016llx nrip: 0x%016llx int_ctl: 0x%08x "
		  "event_inj: 0x%08x npt: %s",
		__entry->rip, __entry->vmcb, __entry->nested_rip,
		__entry->int_ctl, __entry->event_inj,
		__entry->npt ? "on" : "off")
);

TRACE_EVENT(kvm_nested_intercepts,
	    TP_PROTO(__u16 cr_read, __u16 cr_write, __u32 exceptions, __u64 intercept),
	    TP_ARGS(cr_read, cr_write, exceptions, intercept),

	TP_STRUCT__entry(
		__field(	__u16,		cr_read		)
		__field(	__u16,		cr_write	)
		__field(	__u32,		exceptions	)
		__field(	__u64,		intercept	)
	),

	TP_fast_assign(
		__entry->cr_read	= cr_read;
		__entry->cr_write	= cr_write;
		__entry->exceptions	= exceptions;
		__entry->intercept	= intercept;
	),

	TP_printk("cr_read: %04x cr_write: %04x excp: %08x intercept: %016llx",
		__entry->cr_read, __entry->cr_write, __entry->exceptions,
		__entry->intercept)
);
/*
 * Tracepoint for #VMEXIT while nested
 */
TRACE_EVENT(kvm_nested_vmexit,
	    TP_PROTO(__u64 rip, __u32 exit_code,
		     __u64 exit_info1, __u64 exit_info2,
		     __u32 exit_int_info, __u32 exit_int_info_err, __u32 isa),
	    TP_ARGS(rip, exit_code, exit_info1, exit_info2,
		    exit_int_info, exit_int_info_err, isa),

	TP_STRUCT__entry(
		__field(	__u64,		rip			)
		__field(	__u32,		exit_code		)
		__field(	__u64,		exit_info1		)
		__field(	__u64,		exit_info2		)
		__field(	__u32,		exit_int_info		)
		__field(	__u32,		exit_int_info_err	)
		__field(	__u32,		isa			)
	),

	TP_fast_assign(
		__entry->rip			= rip;
		__entry->exit_code		= exit_code;
		__entry->exit_info1		= exit_info1;
		__entry->exit_info2		= exit_info2;
		__entry->exit_int_info		= exit_int_info;
		__entry->exit_int_info_err	= exit_int_info_err;
		__entry->isa			= isa;
	),
	TP_printk("rip: 0x%016llx reason: %s ext_inf1: 0x%016llx "
		  "ext_inf2: 0x%016llx ext_int: 0x%08x ext_int_err: 0x%08x",
		  __entry->rip,
		 (__entry->isa == KVM_ISA_VMX) ?
		 __print_symbolic(__entry->exit_code, VMX_EXIT_REASONS) :
		 __print_symbolic(__entry->exit_code, SVM_EXIT_REASONS),
		  __entry->exit_info1, __entry->exit_info2,
		  __entry->exit_int_info, __entry->exit_int_info_err)
);

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

	TP_printk("reason: %s ext_inf1: 0x%016llx "
		  "ext_inf2: 0x%016llx ext_int: 0x%08x ext_int_err: 0x%08x",
		 (__entry->isa == KVM_ISA_VMX) ?
		 __print_symbolic(__entry->exit_code, VMX_EXIT_REASONS) :
		 __print_symbolic(__entry->exit_code, SVM_EXIT_REASONS),
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
	    TP_PROTO(__u64 rip, int asid, u64 address),
	    TP_ARGS(rip, asid, address),

	TP_STRUCT__entry(
		__field(	__u64,	rip	)
		__field(	int,	asid	)
		__field(	__u64,	address	)
	),

	TP_fast_assign(
		__entry->rip		=	rip;
		__entry->asid		=	asid;
		__entry->address	=	address;
	),

	TP_printk("rip: 0x%016llx asid: %d address: 0x%016llx",
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
		__entry->csbase = kvm_x86_ops->get_segment_base(vcpu, VCPU_SREG_CS);
		__entry->len = vcpu->arch.emulate_ctxt.fetch.ptr
			       - vcpu->arch.emulate_ctxt.fetch.data;
		__entry->rip = vcpu->arch.emulate_ctxt._eip - __entry->len;
		memcpy(__entry->insn,
		       vcpu->arch.emulate_ctxt.fetch.data,
		       15);
		__entry->flags = kei_decode_mode(vcpu->arch.emulate_ctxt.mode);
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
	{VCLOCK_NONE, "none"},				\
	{VCLOCK_TSC,  "tsc"},				\
	{VCLOCK_HPET, "hpet"}				\

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

TRACE_EVENT(kvm_ple_window,
	TP_PROTO(bool grow, unsigned int vcpu_id, int new, int old),
	TP_ARGS(grow, vcpu_id, new, old),

	TP_STRUCT__entry(
		__field(                bool,      grow         )
		__field(        unsigned int,   vcpu_id         )
		__field(                 int,       new         )
		__field(                 int,       old         )
	),

	TP_fast_assign(
		__entry->grow           = grow;
		__entry->vcpu_id        = vcpu_id;
		__entry->new            = new;
		__entry->old            = old;
	),

	TP_printk("vcpu %u: ple_window %d (%s %d)",
	          __entry->vcpu_id,
	          __entry->new,
	          __entry->grow ? "grow" : "shrink",
	          __entry->old)
);

#define trace_kvm_ple_window_grow(vcpu_id, new, old) \
	trace_kvm_ple_window(true, vcpu_id, new, old)
#define trace_kvm_ple_window_shrink(vcpu_id, new, old) \
	trace_kvm_ple_window(false, vcpu_id, new, old)

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

TRACE_EVENT(kvm_enter_smm,
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
	TP_PROTO(unsigned int vcpu_id, unsigned int gsi,
		 unsigned int gvec, u64 pi_desc_addr, bool set),
	TP_ARGS(vcpu_id, gsi, gvec, pi_desc_addr, set),

	TP_STRUCT__entry(
		__field(	unsigned int,	vcpu_id		)
		__field(	unsigned int,	gsi		)
		__field(	unsigned int,	gvec		)
		__field(	u64,		pi_desc_addr	)
		__field(	bool,		set		)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu_id;
		__entry->gsi		= gsi;
		__entry->gvec		= gvec;
		__entry->pi_desc_addr	= pi_desc_addr;
		__entry->set		= set;
	),

	TP_printk("VT-d PI is %s for this irq, vcpu %u, gsi: 0x%x, "
		  "gvec: 0x%x, pi_desc_addr: 0x%llx",
		  __entry->set ? "enabled and being updated" : "disabled",
		  __entry->vcpu_id,
		  __entry->gsi,
		  __entry->gvec,
		  __entry->pi_desc_addr)
);

#endif /* _TRACE_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH arch/x86/kvm
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
