/* Architecture specific portion of the lguest hypercalls */
#ifndef _ASM_X86_LGUEST_HCALL_H
#define _ASM_X86_LGUEST_HCALL_H

#define LHCALL_FLUSH_ASYNC	0
#define LHCALL_LGUEST_INIT	1
#define LHCALL_SHUTDOWN		2
#define LHCALL_NEW_PGTABLE	4
#define LHCALL_FLUSH_TLB	5
#define LHCALL_LOAD_IDT_ENTRY	6
#define LHCALL_SET_STACK	7
#define LHCALL_TS		8
#define LHCALL_SET_CLOCKEVENT	9
#define LHCALL_HALT		10
#define LHCALL_SET_PMD		13
#define LHCALL_SET_PTE		14
#define LHCALL_SET_PGD		15
#define LHCALL_LOAD_TLS		16
#define LHCALL_LOAD_GDT_ENTRY	18
#define LHCALL_SEND_INTERRUPTS	19

#define LGUEST_TRAP_ENTRY 0x1F

/* Argument number 3 to LHCALL_LGUEST_SHUTDOWN */
#define LGUEST_SHUTDOWN_POWEROFF	1
#define LGUEST_SHUTDOWN_RESTART		2

#ifndef __ASSEMBLY__
#include <asm/hw_irq.h>

/*G:030
 * But first, how does our Guest contact the Host to ask for privileged
 * operations?  There are two ways: the direct way is to make a "hypercall",
 * to make requests of the Host Itself.
 *
 * Our hypercall mechanism uses the highest unused trap code (traps 32 and
 * above are used by real hardware interrupts).  Seventeen hypercalls are
 * available: the hypercall number is put in the %eax register, and the
 * arguments (when required) are placed in %ebx, %ecx, %edx and %esi.
 * If a return value makes sense, it's returned in %eax.
 *
 * Grossly invalid calls result in Sudden Death at the hands of the vengeful
 * Host, rather than returning failure.  This reflects Winston Churchill's
 * definition of a gentleman: "someone who is only rude intentionally".
 */
static inline unsigned long
hcall(unsigned long call,
      unsigned long arg1, unsigned long arg2, unsigned long arg3,
      unsigned long arg4)
{
	/* "int" is the Intel instruction to trigger a trap. */
	asm volatile("int $" __stringify(LGUEST_TRAP_ENTRY)
		     /* The call in %eax (aka "a") might be overwritten */
		     : "=a"(call)
		       /* The arguments are in %eax, %ebx, %ecx, %edx & %esi */
		     : "a"(call), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
		       /* "memory" means this might write somewhere in memory.
			* This isn't true for all calls, but it's safe to tell
			* gcc that it might happen so it doesn't get clever. */
		     : "memory");
	return call;
}
/*:*/

/* Can't use our min() macro here: needs to be a constant */
#define LGUEST_IRQS (NR_IRQS < 32 ? NR_IRQS: 32)

#define LHCALL_RING_SIZE 64
struct hcall_args {
	/* These map directly onto eax/ebx/ecx/edx/esi in struct lguest_regs */
	unsigned long arg0, arg1, arg2, arg3, arg4;
};

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_LGUEST_HCALL_H */
