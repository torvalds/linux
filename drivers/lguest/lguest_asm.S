#include <linux/linkage.h>
#include <linux/lguest.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

/* FIXME: Once asm/processor-flags.h goes in, include that */
#define X86_EFLAGS_IF 0x00000200

/*
 * This is where we begin: we have a magic signature which the launcher looks
 * for.  The plan is that the Linux boot protocol will be extended with a
 * "platform type" field which will guide us here from the normal entry point,
 * but for the moment this suffices.  We pass the virtual address of the boot
 * info to lguest_init().
 *
 * We put it in .init.text will be discarded after boot.
 */
.section .init.text, "ax", @progbits
.ascii "GenuineLguest"
	/* Set up initial stack. */
 	movl $(init_thread_union+THREAD_SIZE),%esp
	movl %esi, %eax
	addl $__PAGE_OFFSET, %eax
	jmp lguest_init

/* The templates for inline patching. */
#define LGUEST_PATCH(name, insns...)			\
	lgstart_##name:	insns; lgend_##name:;		\
	.globl lgstart_##name; .globl lgend_##name

LGUEST_PATCH(cli, movl $0, lguest_data+LGUEST_DATA_irq_enabled)
LGUEST_PATCH(sti, movl $X86_EFLAGS_IF, lguest_data+LGUEST_DATA_irq_enabled)
LGUEST_PATCH(popf, movl %eax, lguest_data+LGUEST_DATA_irq_enabled)
LGUEST_PATCH(pushf, movl lguest_data+LGUEST_DATA_irq_enabled, %eax)

.text
/* These demark the EIP range where host should never deliver interrupts. */
.global lguest_noirq_start
.global lguest_noirq_end

/*
 * We move eflags word to lguest_data.irq_enabled to restore interrupt state.
 * For page faults, gpfs and virtual interrupts, the hypervisor has saved
 * eflags manually, otherwise it was delivered directly and so eflags reflects
 * the real machine IF state, ie. interrupts on.  Since the kernel always dies
 * if it takes such a trap with interrupts disabled anyway, turning interrupts
 * back on unconditionally here is OK.
 */
ENTRY(lguest_iret)
	pushl	%eax
	movl	12(%esp), %eax
lguest_noirq_start:
	movl	%eax,%ss:lguest_data+LGUEST_DATA_irq_enabled
	popl	%eax
	iret
lguest_noirq_end:
