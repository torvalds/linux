/* This code sits at 0xFFC00000 to do the low-level guest<->host switch.

   There is are two pages above us for this CPU (struct lguest_pages).
   The second page (struct lguest_ro_state) becomes read-only after the
   context switch.  The first page (the stack for traps) remains writable,
   but while we're in here, the guest cannot be running.
*/
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include "lg.h"

.text
ENTRY(start_switcher_text)

/* %eax points to lguest pages for this CPU.  %ebx contains cr3 value.
   All normal registers can be clobbered! */
ENTRY(switch_to_guest)
	/* Save host segments on host stack. */
	pushl	%es
	pushl	%ds
	pushl	%gs
	pushl	%fs
	/* With CONFIG_FRAME_POINTER, gcc doesn't let us clobber this! */
	pushl	%ebp
	/* Save host stack. */
	movl	%esp, LGUEST_PAGES_host_sp(%eax)
	/* Switch to guest stack: if we get NMI we expect to be there. */
	movl	%eax, %edx
	addl	$LGUEST_PAGES_regs, %edx
	movl	%edx, %esp
	/* Switch to guest's GDT, IDT. */
	lgdt	LGUEST_PAGES_guest_gdt_desc(%eax)
	lidt	LGUEST_PAGES_guest_idt_desc(%eax)
	/* Switch to guest's TSS while GDT still writable. */
	movl	$(GDT_ENTRY_TSS*8), %edx
	ltr	%dx
	/* Set host's TSS GDT entry to available (clear byte 5 bit 2). */
	movl	(LGUEST_PAGES_host_gdt_desc+2)(%eax), %edx
	andb	$0xFD, (GDT_ENTRY_TSS*8 + 5)(%edx)
	/* Switch to guest page tables:	lguest_pages->state now read-only. */
	movl	%ebx, %cr3
	/* Restore guest regs */
	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%esi
	popl	%edi
	popl	%ebp
	popl	%gs
	popl	%eax
	popl	%fs
	popl	%ds
	popl	%es
	/* Skip error code and trap number */
	addl	$8, %esp
	iret

#define SWITCH_TO_HOST							\
	/* Save guest state */						\
	pushl	%es;							\
	pushl	%ds;							\
	pushl	%fs;							\
	pushl	%eax;							\
	pushl	%gs;							\
	pushl	%ebp;							\
	pushl	%edi;							\
	pushl	%esi;							\
	pushl	%edx;							\
	pushl	%ecx;							\
	pushl	%ebx;							\
	/* Load lguest ds segment for convenience. */			\
	movl	$(LGUEST_DS), %eax;					\
	movl	%eax, %ds;						\
	/* Figure out where we are, based on stack (at top of regs). */	\
	movl	%esp, %eax;						\
	subl	$LGUEST_PAGES_regs, %eax;				\
	/* Put trap number in %ebx before we switch cr3 and lose it. */ \
	movl	LGUEST_PAGES_regs_trapnum(%eax), %ebx;			\
	/* Switch to host page tables (host GDT, IDT and stack are in host   \
	   mem, so need this first) */					\
	movl	LGUEST_PAGES_host_cr3(%eax), %edx;			\
	movl	%edx, %cr3;						\
	/* Set guest's TSS to available (clear byte 5 bit 2). */	\
	andb	$0xFD, (LGUEST_PAGES_guest_gdt+GDT_ENTRY_TSS*8+5)(%eax); \
	/* Switch to host's GDT & IDT. */				\
	lgdt	LGUEST_PAGES_host_gdt_desc(%eax);			\
	lidt	LGUEST_PAGES_host_idt_desc(%eax);			\
	/* Switch to host's stack. */					\
	movl	LGUEST_PAGES_host_sp(%eax), %esp;			\
	/* Switch to host's TSS */					\
	movl	$(GDT_ENTRY_TSS*8), %edx;				\
	ltr	%dx;							\
	popl	%ebp;							\
	popl	%fs;							\
	popl	%gs;							\
	popl	%ds;							\
	popl	%es

/* Return to run_guest_once. */
return_to_host:
	SWITCH_TO_HOST
	iret

deliver_to_host:
	SWITCH_TO_HOST
	/* Decode IDT and jump to hosts' irq handler.  When that does iret, it
	 * will return to run_guest_once.  This is a feature. */
	movl	(LGUEST_PAGES_host_idt_desc+2)(%eax), %edx
	leal	(%edx,%ebx,8), %eax
	movzwl	(%eax),%edx
	movl	4(%eax), %eax
	xorw	%ax, %ax
	orl	%eax, %edx
	jmp	*%edx

/* Real hardware interrupts are delivered straight to the host.  Others
   cause us to return to run_guest_once so it can decide what to do.  Note
   that some of these are overridden by the guest to deliver directly, and
   never enter here (see load_guest_idt_entry). */
.macro IRQ_STUB N TARGET
	.data; .long 1f; .text; 1:
 /* Make an error number for most traps, which don't have one. */
 .if (\N <> 8) && (\N < 10 || \N > 14) && (\N <> 17)
	pushl	$0
 .endif
	pushl	$\N
	jmp	\TARGET
	ALIGN
.endm

.macro IRQ_STUBS FIRST LAST TARGET
 irq=\FIRST
 .rept \LAST-\FIRST+1
	IRQ_STUB irq \TARGET
  irq=irq+1
 .endr
.endm

/* We intercept every interrupt, because we may need to switch back to
 * host.  Unfortunately we can't tell them apart except by entry
 * point, so we need 256 entry points.
 */
.data
.global default_idt_entries
default_idt_entries:
.text
	IRQ_STUBS 0 1 return_to_host		/* First two traps */
	IRQ_STUB 2 handle_nmi			/* NMI */
	IRQ_STUBS 3 31 return_to_host		/* Rest of traps */
	IRQ_STUBS 32 127 deliver_to_host	/* Real interrupts */
	IRQ_STUB 128 return_to_host		/* System call (overridden) */
	IRQ_STUBS 129 255 deliver_to_host	/* Other real interrupts */

/* We ignore NMI and return. */
handle_nmi:
	addl	$8, %esp
	iret

ENTRY(end_switcher_text)
