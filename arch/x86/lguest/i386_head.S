#include <linux/linkage.h>
#include <linux/lguest.h>
#include <asm/lguest_hcall.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/processor-flags.h>

/*G:020 Our story starts with the kernel booting into startup_32 in
 * arch/x86/kernel/head_32.S.  It expects a boot header, which is created by
 * the bootloader (the Launcher in our case).
 *
 * The startup_32 function does very little: it clears the uninitialized global
 * C variables which we expect to be zero (ie. BSS) and then copies the boot
 * header and kernel command line somewhere safe.  Finally it checks the
 * 'hardware_subarch' field.  This was introduced in 2.6.24 for lguest and Xen:
 * if it's set to '1' (lguest's assigned number), then it calls us here.
 *
 * WARNING: be very careful here!  We're running at addresses equal to physical
 * addesses (around 0), not above PAGE_OFFSET as most code expectes
 * (eg. 0xC0000000).  Jumps are relative, so they're OK, but we can't touch any
 * data without remembering to subtract __PAGE_OFFSET!
 *
 * The .section line puts this code in .init.text so it will be discarded after
 * boot. */
.section .init.text, "ax", @progbits
ENTRY(lguest_entry)
	/* We make the "initialization" hypercall now to tell the Host about
	 * us, and also find out where it put our page tables. */
	movl $LHCALL_LGUEST_INIT, %eax
	movl $lguest_data - __PAGE_OFFSET, %edx
	int $LGUEST_TRAP_ENTRY

	/* Set up the initial stack so we can run C code. */
	movl $(init_thread_union+THREAD_SIZE),%esp

	/* Jumps are relative, and we're running __PAGE_OFFSET too low at the
	 * moment. */
	jmp lguest_init+__PAGE_OFFSET

/*G:055 We create a macro which puts the assembler code between lgstart_ and
 * lgend_ markers.  These templates are put in the .text section: they can't be
 * discarded after boot as we may need to patch modules, too. */
.text
#define LGUEST_PATCH(name, insns...)			\
	lgstart_##name:	insns; lgend_##name:;		\
	.globl lgstart_##name; .globl lgend_##name

LGUEST_PATCH(cli, movl $0, lguest_data+LGUEST_DATA_irq_enabled)
LGUEST_PATCH(sti, movl $X86_EFLAGS_IF, lguest_data+LGUEST_DATA_irq_enabled)
LGUEST_PATCH(popf, movl %eax, lguest_data+LGUEST_DATA_irq_enabled)
LGUEST_PATCH(pushf, movl lguest_data+LGUEST_DATA_irq_enabled, %eax)
/*:*/

/* These demark the EIP range where host should never deliver interrupts. */
.global lguest_noirq_start
.global lguest_noirq_end

/*M:004 When the Host reflects a trap or injects an interrupt into the Guest,
 * it sets the eflags interrupt bit on the stack based on
 * lguest_data.irq_enabled, so the Guest iret logic does the right thing when
 * restoring it.  However, when the Host sets the Guest up for direct traps,
 * such as system calls, the processor is the one to push eflags onto the
 * stack, and the interrupt bit will be 1 (in reality, interrupts are always
 * enabled in the Guest).
 *
 * This turns out to be harmless: the only trap which should happen under Linux
 * with interrupts disabled is Page Fault (due to our lazy mapping of vmalloc
 * regions), which has to be reflected through the Host anyway.  If another
 * trap *does* go off when interrupts are disabled, the Guest will panic, and
 * we'll never get to this iret! :*/

/*G:045 There is one final paravirt_op that the Guest implements, and glancing
 * at it you can see why I left it to last.  It's *cool*!  It's in *assembler*!
 *
 * The "iret" instruction is used to return from an interrupt or trap.  The
 * stack looks like this:
 *   old address
 *   old code segment & privilege level
 *   old processor flags ("eflags")
 *
 * The "iret" instruction pops those values off the stack and restores them all
 * at once.  The only problem is that eflags includes the Interrupt Flag which
 * the Guest can't change: the CPU will simply ignore it when we do an "iret".
 * So we have to copy eflags from the stack to lguest_data.irq_enabled before
 * we do the "iret".
 *
 * There are two problems with this: firstly, we need to use a register to do
 * the copy and secondly, the whole thing needs to be atomic.  The first
 * problem is easy to solve: push %eax on the stack so we can use it, and then
 * restore it at the end just before the real "iret".
 *
 * The second is harder: copying eflags to lguest_data.irq_enabled will turn
 * interrupts on before we're finished, so we could be interrupted before we
 * return to userspace or wherever.  Our solution to this is to surround the
 * code with lguest_noirq_start: and lguest_noirq_end: labels.  We tell the
 * Host that it is *never* to interrupt us there, even if interrupts seem to be
 * enabled. */
ENTRY(lguest_iret)
	pushl	%eax
	movl	12(%esp), %eax
lguest_noirq_start:
	/* Note the %ss: segment prefix here.  Normal data accesses use the
	 * "ds" segment, but that will have already been restored for whatever
	 * we're returning to (such as userspace): we can't trust it.  The %ss:
	 * prefix makes sure we use the stack segment, which is still valid. */
	movl	%eax,%ss:lguest_data+LGUEST_DATA_irq_enabled
	popl	%eax
	iret
lguest_noirq_end:
