/* Architecture specific portion of the lguest hypercalls */
#ifndef _ASM_X86_LGUEST_HCALL_H
#define _ASM_X86_LGUEST_HCALL_H

#define LHCALL_FLUSH_ASYNC	0
#define LHCALL_LGUEST_INIT	1
#define LHCALL_SHUTDOWN		2
#define LHCALL_LOAD_GDT		3
#define LHCALL_NEW_PGTABLE	4
#define LHCALL_FLUSH_TLB	5
#define LHCALL_LOAD_IDT_ENTRY	6
#define LHCALL_SET_STACK	7
#define LHCALL_TS		8
#define LHCALL_SET_CLOCKEVENT	9
#define LHCALL_HALT		10
#define LHCALL_SET_PTE		14
#define LHCALL_SET_PMD		15
#define LHCALL_LOAD_TLS		16
#define LHCALL_NOTIFY		17

#define LGUEST_TRAP_ENTRY 0x1F

/* Argument number 3 to LHCALL_LGUEST_SHUTDOWN */
#define LGUEST_SHUTDOWN_POWEROFF	1
#define LGUEST_SHUTDOWN_RESTART		2

#ifndef __ASSEMBLY__
#include <asm/hw_irq.h>
#include <asm/kvm_para.h>

/*G:031 But first, how does our Guest contact the Host to ask for privileged
 * operations?  There are two ways: the direct way is to make a "hypercall",
 * to make requests of the Host Itself.
 *
 * We use the KVM hypercall mechanism. Eighteen hypercalls are
 * available: the hypercall number is put in the %eax register, and the
 * arguments (when required) are placed in %ebx, %ecx and %edx.  If a return
 * value makes sense, it's returned in %eax.
 *
 * Grossly invalid calls result in Sudden Death at the hands of the vengeful
 * Host, rather than returning failure.  This reflects Winston Churchill's
 * definition of a gentleman: "someone who is only rude intentionally". */
/*:*/

/* Can't use our min() macro here: needs to be a constant */
#define LGUEST_IRQS (NR_IRQS < 32 ? NR_IRQS: 32)

#define LHCALL_RING_SIZE 64
struct hcall_args {
	/* These map directly onto eax, ebx, ecx, edx in struct lguest_regs */
	unsigned long arg0, arg1, arg2, arg3;
};

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_LGUEST_HCALL_H */
