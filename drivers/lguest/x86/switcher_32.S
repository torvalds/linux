/*P:900
 * This is the Switcher: code which sits at 0xFFC00000 (or 0xFFE00000) astride
 * both the Host and Guest to do the low-level Guest<->Host switch.  It is as
 * simple as it can be made, but it's naturally very specific to x86.
 *
 * You have now completed Preparation.  If this has whet your appetite; if you
 * are feeling invigorated and refreshed then the next, more challenging stage
 * can be found in "make Guest".
 :*/

/*M:012
 * Lguest is meant to be simple: my rule of thumb is that 1% more LOC must
 * gain at least 1% more performance.  Since neither LOC nor performance can be
 * measured beforehand, it generally means implementing a feature then deciding
 * if it's worth it.  And once it's implemented, who can say no?
 *
 * This is why I haven't implemented this idea myself.  I want to, but I
 * haven't.  You could, though.
 *
 * The main place where lguest performance sucks is Guest page faulting.  When
 * a Guest userspace process hits an unmapped page we switch back to the Host,
 * walk the page tables, find it's not mapped, switch back to the Guest page
 * fault handler, which calls a hypercall to set the page table entry, then
 * finally returns to userspace.  That's two round-trips.
 *
 * If we had a small walker in the Switcher, we could quickly check the Guest
 * page table and if the page isn't mapped, immediately reflect the fault back
 * into the Guest.  This means the Switcher would have to know the top of the
 * Guest page table and the page fault handler address.
 *
 * For simplicity, the Guest should only handle the case where the privilege
 * level of the fault is 3 and probably only not present or write faults.  It
 * should also detect recursive faults, and hand the original fault to the
 * Host (which is actually really easy).
 *
 * Two questions remain.  Would the performance gain outweigh the complexity?
 * And who would write the verse documenting it?
:*/

/*M:011
 * Lguest64 handles NMI.  This gave me NMI envy (until I looked at their
 * code).  It's worth doing though, since it would let us use oprofile in the
 * Host when a Guest is running.
:*/

/*S:100
 * Welcome to the Switcher itself!
 *
 * This file contains the low-level code which changes the CPU to run the Guest
 * code, and returns to the Host when something happens.  Understand this, and
 * you understand the heart of our journey.
 *
 * Because this is in assembler rather than C, our tale switches from prose to
 * verse.  First I tried limericks:
 *
 *	There once was an eax reg,
 *	To which our pointer was fed,
 *	It needed an add,
 *	Which asm-offsets.h had
 *	But this limerick is hurting my head.
 *
 * Next I tried haikus, but fitting the required reference to the seasons in
 * every stanza was quickly becoming tiresome:
 *
 *	The %eax reg
 *	Holds "struct lguest_pages" now:
 *	Cherry blossoms fall.
 *
 * Then I started with Heroic Verse, but the rhyming requirement leeched away
 * the content density and led to some uniquely awful oblique rhymes:
 *
 *	These constants are coming from struct offsets
 *	For use within the asm switcher text.
 *
 * Finally, I settled for something between heroic hexameter, and normal prose
 * with inappropriate linebreaks.  Anyway, it aint no Shakespeare.
 */

// Not all kernel headers work from assembler
// But these ones are needed: the ENTRY() define
// And constants extracted from struct offsets
// To avoid magic numbers and breakage:
// Should they change the compiler can't save us
// Down here in the depths of assembler code.
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/segment.h>
#include <asm/lguest.h>

// We mark the start of the code to copy
// It's placed in .text tho it's never run here
// You'll see the trick macro at the end
// Which interleaves data and text to effect.
.text
ENTRY(start_switcher_text)

// When we reach switch_to_guest we have just left
// The safe and comforting shores of C code
// %eax has the "struct lguest_pages" to use
// Where we save state and still see it from the Guest
// And %ebx holds the Guest shadow pagetable:
// Once set we have truly left Host behind.
ENTRY(switch_to_guest)
	// We told gcc all its regs could fade,
	// Clobbered by our journey into the Guest
	// We could have saved them, if we tried
	// But time is our master and cycles count.

	// Segment registers must be saved for the Host
	// We push them on the Host stack for later
	pushl	%es
	pushl	%ds
	pushl	%gs
	pushl	%fs
	// But the compiler is fickle, and heeds
	// No warning of %ebp clobbers
	// When frame pointers are used.  That register
	// Must be saved and restored or chaos strikes.
	pushl	%ebp
	// The Host's stack is done, now save it away
	// In our "struct lguest_pages" at offset
	// Distilled into asm-offsets.h
	movl	%esp, LGUEST_PAGES_host_sp(%eax)

	// All saved and there's now five steps before us:
	// Stack, GDT, IDT, TSS
	// Then last of all the page tables are flipped.

	// Yet beware that our stack pointer must be
	// Always valid lest an NMI hits
	// %edx does the duty here as we juggle
	// %eax is lguest_pages: our stack lies within.
	movl	%eax, %edx
	addl	$LGUEST_PAGES_regs, %edx
	movl	%edx, %esp

	// The Guest's GDT we so carefully
	// Placed in the "struct lguest_pages" before
	lgdt	LGUEST_PAGES_guest_gdt_desc(%eax)

	// The Guest's IDT we did partially
	// Copy to "struct lguest_pages" as well.
	lidt	LGUEST_PAGES_guest_idt_desc(%eax)

	// The TSS entry which controls traps
	// Must be loaded up with "ltr" now:
	// The GDT entry that TSS uses 
	// Changes type when we load it: damn Intel!
	// For after we switch over our page tables
	// That entry will be read-only: we'd crash.
	movl	$(GDT_ENTRY_TSS*8), %edx
	ltr	%dx

	// Look back now, before we take this last step!
	// The Host's TSS entry was also marked used;
	// Let's clear it again for our return.
	// The GDT descriptor of the Host
	// Points to the table after two "size" bytes
	movl	(LGUEST_PAGES_host_gdt_desc+2)(%eax), %edx
	// Clear "used" from type field (byte 5, bit 2)
	andb	$0xFD, (GDT_ENTRY_TSS*8 + 5)(%edx)

	// Once our page table's switched, the Guest is live!
	// The Host fades as we run this final step.
	// Our "struct lguest_pages" is now read-only.
	movl	%ebx, %cr3

	// The page table change did one tricky thing:
	// The Guest's register page has been mapped
	// Writable under our %esp (stack) --
	// We can simply pop off all Guest regs.
	popl	%eax
	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%esi
	popl	%edi
	popl	%ebp
	popl	%gs
	popl	%fs
	popl	%ds
	popl	%es

	// Near the base of the stack lurk two strange fields
	// Which we fill as we exit the Guest
	// These are the trap number and its error
	// We can simply step past them on our way.
	addl	$8, %esp

	// The last five stack slots hold return address
	// And everything needed to switch privilege
	// From Switcher's level 0 to Guest's 1,
	// And the stack where the Guest had last left it.
	// Interrupts are turned back on: we are Guest.
	iret

// We tread two paths to switch back to the Host
// Yet both must save Guest state and restore Host
// So we put the routine in a macro.
#define SWITCH_TO_HOST							\
	/* We save the Guest state: all registers first			\
	 * Laid out just as "struct lguest_regs" defines */		\
	pushl	%es;							\
	pushl	%ds;							\
	pushl	%fs;							\
	pushl	%gs;							\
	pushl	%ebp;							\
	pushl	%edi;							\
	pushl	%esi;							\
	pushl	%edx;							\
	pushl	%ecx;							\
	pushl	%ebx;							\
	pushl	%eax;							\
	/* Our stack and our code are using segments			\
	 * Set in the TSS and IDT					\
	 * Yet if we were to touch data we'd use			\
	 * Whatever data segment the Guest had.				\
	 * Load the lguest ds segment for now. */			\
	movl	$(LGUEST_DS), %eax;					\
	movl	%eax, %ds;						\
	/* So where are we?  Which CPU, which struct?			\
	 * The stack is our clue: our TSS starts			\
	 * It at the end of "struct lguest_pages".			\
	 * Or we may have stumbled while restoring			\
	 * Our Guest segment regs while in switch_to_guest,		\
	 * The fault pushed atop that part-unwound stack.		\
	 * If we round the stack down to the page start			\
	 * We're at the start of "struct lguest_pages". */		\
	movl	%esp, %eax;						\
	andl	$(~(1 << PAGE_SHIFT - 1)), %eax;			\
	/* Save our trap number: the switch will obscure it		\
	 * (In the Host the Guest regs are not mapped here)		\
	 * %ebx holds it safe for deliver_to_host */			\
	movl	LGUEST_PAGES_regs_trapnum(%eax), %ebx;			\
	/* The Host GDT, IDT and stack!					\
	 * All these lie safely hidden from the Guest:			\
	 * We must return to the Host page tables			\
	 * (Hence that was saved in struct lguest_pages) */		\
	movl	LGUEST_PAGES_host_cr3(%eax), %edx;			\
	movl	%edx, %cr3;						\
	/* As before, when we looked back at the Host			\
	 * As we left and marked TSS unused				\
	 * So must we now for the Guest left behind. */			\
	andb	$0xFD, (LGUEST_PAGES_guest_gdt+GDT_ENTRY_TSS*8+5)(%eax); \
	/* Switch to Host's GDT, IDT. */				\
	lgdt	LGUEST_PAGES_host_gdt_desc(%eax);			\
	lidt	LGUEST_PAGES_host_idt_desc(%eax);			\
	/* Restore the Host's stack where its saved regs lie */		\
	movl	LGUEST_PAGES_host_sp(%eax), %esp;			\
	/* Last the TSS: our Host is returned */			\
	movl	$(GDT_ENTRY_TSS*8), %edx;				\
	ltr	%dx;							\
	/* Restore now the regs saved right at the first. */		\
	popl	%ebp;							\
	popl	%fs;							\
	popl	%gs;							\
	popl	%ds;							\
	popl	%es

// The first path is trod when the Guest has trapped:
// (Which trap it was has been pushed on the stack).
// We need only switch back, and the Host will decode
// Why we came home, and what needs to be done.
return_to_host:
	SWITCH_TO_HOST
	iret

// We are lead to the second path like so:
// An interrupt, with some cause external
// Has ajerked us rudely from the Guest's code
// Again we must return home to the Host
deliver_to_host:
	SWITCH_TO_HOST
	// But now we must go home via that place
	// Where that interrupt was supposed to go
	// Had we not been ensconced, running the Guest.
	// Here we see the trickness of run_guest_once():
	// The Host stack is formed like an interrupt
	// With EIP, CS and EFLAGS layered.
	// Interrupt handlers end with "iret"
	// And that will take us home at long long last.

	// But first we must find the handler to call!
	// The IDT descriptor for the Host
	// Has two bytes for size, and four for address:
	// %edx will hold it for us for now.
	movl	(LGUEST_PAGES_host_idt_desc+2)(%eax), %edx
	// We now know the table address we need,
	// And saved the trap's number inside %ebx.
	// Yet the pointer to the handler is smeared
	// Across the bits of the table entry.
	// What oracle can tell us how to extract
	// From such a convoluted encoding?
	// I consulted gcc, and it gave
	// These instructions, which I gladly credit:
	leal	(%edx,%ebx,8), %eax
	movzwl	(%eax),%edx
	movl	4(%eax), %eax
	xorw	%ax, %ax
	orl	%eax, %edx
	// Now the address of the handler's in %edx
	// We call it now: its "iret" drops us home.
	jmp	*%edx

// Every interrupt can come to us here
// But we must truly tell each apart.
// They number two hundred and fifty six
// And each must land in a different spot,
// Push its number on stack, and join the stream.

// And worse, a mere six of the traps stand apart
// And push on their stack an addition:
// An error number, thirty two bits long
// So we punish the other two fifty
// And make them push a zero so they match.

// Yet two fifty six entries is long
// And all will look most the same as the last
// So we create a macro which can make
// As many entries as we need to fill.

// Note the change to .data then .text:
// We plant the address of each entry
// Into a (data) table for the Host
// To know where each Guest interrupt should go.
.macro IRQ_STUB N TARGET
	.data; .long 1f; .text; 1:
 // Trap eight, ten through fourteen and seventeen
 // Supply an error number.  Else zero.
 .if (\N <> 8) && (\N < 10 || \N > 14) && (\N <> 17)
	pushl	$0
 .endif
	pushl	$\N
	jmp	\TARGET
	ALIGN
.endm

// This macro creates numerous entries
// Using GAS macros which out-power C's.
.macro IRQ_STUBS FIRST LAST TARGET
 irq=\FIRST
 .rept \LAST-\FIRST+1
	IRQ_STUB irq \TARGET
  irq=irq+1
 .endr
.endm

// Here's the marker for our pointer table
// Laid in the data section just before
// Each macro places the address of code
// Forming an array: each one points to text
// Which handles interrupt in its turn.
.data
.global default_idt_entries
default_idt_entries:
.text
	// The first two traps go straight back to the Host
	IRQ_STUBS 0 1 return_to_host
	// We'll say nothing, yet, about NMI
	IRQ_STUB 2 handle_nmi
	// Other traps also return to the Host
	IRQ_STUBS 3 31 return_to_host
	// All interrupts go via their handlers
	IRQ_STUBS 32 127 deliver_to_host
	// 'Cept system calls coming from userspace
	// Are to go to the Guest, never the Host.
	IRQ_STUB 128 return_to_host
	IRQ_STUBS 129 255 deliver_to_host

// The NMI, what a fabulous beast
// Which swoops in and stops us no matter that
// We're suspended between heaven and hell,
// (Or more likely between the Host and Guest)
// When in it comes!  We are dazed and confused
// So we do the simplest thing which one can.
// Though we've pushed the trap number and zero
// We discard them, return, and hope we live.
handle_nmi:
	addl	$8, %esp
	iret

// We are done; all that's left is Mastery
// And "make Mastery" is a journey long
// Designed to make your fingers itch to code.

// Here ends the text, the file and poem.
ENTRY(end_switcher_text)
