/*P:800 Interrupts (traps) are complicated enough to earn their own file.
 * There are three classes of interrupts:
 *
 * 1) Real hardware interrupts which occur while we're running the Guest,
 * 2) Interrupts for virtual devices attached to the Guest, and
 * 3) Traps and faults from the Guest.
 *
 * Real hardware interrupts must be delivered to the Host, not the Guest.
 * Virtual interrupts must be delivered to the Guest, but we make them look
 * just like real hardware would deliver them.  Traps from the Guest can be set
 * up to go directly back into the Guest, but sometimes the Host wants to see
 * them first, so we also have a way of "reflecting" them into the Guest as if
 * they had been delivered to it directly. :*/
#include <linux/uaccess.h>
#include "lg.h"

/* The address of the interrupt handler is split into two bits: */
static unsigned long idt_address(u32 lo, u32 hi)
{
	return (lo & 0x0000FFFF) | (hi & 0xFFFF0000);
}

/* The "type" of the interrupt handler is a 4 bit field: we only support a
 * couple of types. */
static int idt_type(u32 lo, u32 hi)
{
	return (hi >> 8) & 0xF;
}

/* An IDT entry can't be used unless the "present" bit is set. */
static int idt_present(u32 lo, u32 hi)
{
	return (hi & 0x8000);
}

/* We need a helper to "push" a value onto the Guest's stack, since that's a
 * big part of what delivering an interrupt does. */
static void push_guest_stack(struct lguest *lg, unsigned long *gstack, u32 val)
{
	/* Stack grows upwards: move stack then write value. */
	*gstack -= 4;
	lgwrite_u32(lg, *gstack, val);
}

/*H:210 The set_guest_interrupt() routine actually delivers the interrupt or
 * trap.  The mechanics of delivering traps and interrupts to the Guest are the
 * same, except some traps have an "error code" which gets pushed onto the
 * stack as well: the caller tells us if this is one.
 *
 * "lo" and "hi" are the two parts of the Interrupt Descriptor Table for this
 * interrupt or trap.  It's split into two parts for traditional reasons: gcc
 * on i386 used to be frightened by 64 bit numbers.
 *
 * We set up the stack just like the CPU does for a real interrupt, so it's
 * identical for the Guest (and the standard "iret" instruction will undo
 * it). */
static void set_guest_interrupt(struct lguest *lg, u32 lo, u32 hi, int has_err)
{
	unsigned long gstack;
	u32 eflags, ss, irq_enable;

	/* There are two cases for interrupts: one where the Guest is already
	 * in the kernel, and a more complex one where the Guest is in
	 * userspace.  We check the privilege level to find out. */
	if ((lg->regs->ss&0x3) != GUEST_PL) {
		/* The Guest told us their kernel stack with the SET_STACK
		 * hypercall: both the virtual address and the segment */
		gstack = guest_pa(lg, lg->esp1);
		ss = lg->ss1;
		/* We push the old stack segment and pointer onto the new
		 * stack: when the Guest does an "iret" back from the interrupt
		 * handler the CPU will notice they're dropping privilege
		 * levels and expect these here. */
		push_guest_stack(lg, &gstack, lg->regs->ss);
		push_guest_stack(lg, &gstack, lg->regs->esp);
	} else {
		/* We're staying on the same Guest (kernel) stack. */
		gstack = guest_pa(lg, lg->regs->esp);
		ss = lg->regs->ss;
	}

	/* Remember that we never let the Guest actually disable interrupts, so
	 * the "Interrupt Flag" bit is always set.  We copy that bit from the
	 * Guest's "irq_enabled" field into the eflags word: the Guest copies
	 * it back in "lguest_iret". */
	eflags = lg->regs->eflags;
	if (get_user(irq_enable, &lg->lguest_data->irq_enabled) == 0
	    && !(irq_enable & X86_EFLAGS_IF))
		eflags &= ~X86_EFLAGS_IF;

	/* An interrupt is expected to push three things on the stack: the old
	 * "eflags" word, the old code segment, and the old instruction
	 * pointer. */
	push_guest_stack(lg, &gstack, eflags);
	push_guest_stack(lg, &gstack, lg->regs->cs);
	push_guest_stack(lg, &gstack, lg->regs->eip);

	/* For the six traps which supply an error code, we push that, too. */
	if (has_err)
		push_guest_stack(lg, &gstack, lg->regs->errcode);

	/* Now we've pushed all the old state, we change the stack, the code
	 * segment and the address to execute. */
	lg->regs->ss = ss;
	lg->regs->esp = gstack + lg->page_offset;
	lg->regs->cs = (__KERNEL_CS|GUEST_PL);
	lg->regs->eip = idt_address(lo, hi);

	/* There are two kinds of interrupt handlers: 0xE is an "interrupt
	 * gate" which expects interrupts to be disabled on entry. */
	if (idt_type(lo, hi) == 0xE)
		if (put_user(0, &lg->lguest_data->irq_enabled))
			kill_guest(lg, "Disabling interrupts");
}

/*H:200
 * Virtual Interrupts.
 *
 * maybe_do_interrupt() gets called before every entry to the Guest, to see if
 * we should divert the Guest to running an interrupt handler. */
void maybe_do_interrupt(struct lguest *lg)
{
	unsigned int irq;
	DECLARE_BITMAP(blk, LGUEST_IRQS);
	struct desc_struct *idt;

	/* If the Guest hasn't even initialized yet, we can do nothing. */
	if (!lg->lguest_data)
		return;

	/* Take our "irqs_pending" array and remove any interrupts the Guest
	 * wants blocked: the result ends up in "blk". */
	if (copy_from_user(&blk, lg->lguest_data->blocked_interrupts,
			   sizeof(blk)))
		return;

	bitmap_andnot(blk, lg->irqs_pending, blk, LGUEST_IRQS);

	/* Find the first interrupt. */
	irq = find_first_bit(blk, LGUEST_IRQS);
	/* None?  Nothing to do */
	if (irq >= LGUEST_IRQS)
		return;

	/* They may be in the middle of an iret, where they asked us never to
	 * deliver interrupts. */
	if (lg->regs->eip >= lg->noirq_start && lg->regs->eip < lg->noirq_end)
		return;

	/* If they're halted, interrupts restart them. */
	if (lg->halted) {
		/* Re-enable interrupts. */
		if (put_user(X86_EFLAGS_IF, &lg->lguest_data->irq_enabled))
			kill_guest(lg, "Re-enabling interrupts");
		lg->halted = 0;
	} else {
		/* Otherwise we check if they have interrupts disabled. */
		u32 irq_enabled;
		if (get_user(irq_enabled, &lg->lguest_data->irq_enabled))
			irq_enabled = 0;
		if (!irq_enabled)
			return;
	}

	/* Look at the IDT entry the Guest gave us for this interrupt.  The
	 * first 32 (FIRST_EXTERNAL_VECTOR) entries are for traps, so we skip
	 * over them. */
	idt = &lg->idt[FIRST_EXTERNAL_VECTOR+irq];
	/* If they don't have a handler (yet?), we just ignore it */
	if (idt_present(idt->a, idt->b)) {
		/* OK, mark it no longer pending and deliver it. */
		clear_bit(irq, lg->irqs_pending);
		/* set_guest_interrupt() takes the interrupt descriptor and a
		 * flag to say whether this interrupt pushes an error code onto
		 * the stack as well: virtual interrupts never do. */
		set_guest_interrupt(lg, idt->a, idt->b, 0);
	}

	/* Every time we deliver an interrupt, we update the timestamp in the
	 * Guest's lguest_data struct.  It would be better for the Guest if we
	 * did this more often, but it can actually be quite slow: doing it
	 * here is a compromise which means at least it gets updated every
	 * timer interrupt. */
	write_timestamp(lg);
}

/*H:220 Now we've got the routines to deliver interrupts, delivering traps
 * like page fault is easy.  The only trick is that Intel decided that some
 * traps should have error codes: */
static int has_err(unsigned int trap)
{
	return (trap == 8 || (trap >= 10 && trap <= 14) || trap == 17);
}

/* deliver_trap() returns true if it could deliver the trap. */
int deliver_trap(struct lguest *lg, unsigned int num)
{
	/* Trap numbers are always 8 bit, but we set an impossible trap number
	 * for traps inside the Switcher, so check that here. */
	if (num >= ARRAY_SIZE(lg->idt))
		return 0;

	/* Early on the Guest hasn't set the IDT entries (or maybe it put a
	 * bogus one in): if we fail here, the Guest will be killed. */
	if (!idt_present(lg->idt[num].a, lg->idt[num].b))
		return 0;
	set_guest_interrupt(lg, lg->idt[num].a, lg->idt[num].b, has_err(num));
	return 1;
}

/*H:250 Here's the hard part: returning to the Host every time a trap happens
 * and then calling deliver_trap() and re-entering the Guest is slow.
 * Particularly because Guest userspace system calls are traps (trap 128).
 *
 * So we'd like to set up the IDT to tell the CPU to deliver traps directly
 * into the Guest.  This is possible, but the complexities cause the size of
 * this file to double!  However, 150 lines of code is worth writing for taking
 * system calls down from 1750ns to 270ns.  Plus, if lguest didn't do it, all
 * the other hypervisors would tease it.
 *
 * This routine determines if a trap can be delivered directly. */
static int direct_trap(const struct lguest *lg,
		       const struct desc_struct *trap,
		       unsigned int num)
{
	/* Hardware interrupts don't go to the Guest at all (except system
	 * call). */
	if (num >= FIRST_EXTERNAL_VECTOR && num != SYSCALL_VECTOR)
		return 0;

	/* The Host needs to see page faults (for shadow paging and to save the
	 * fault address), general protection faults (in/out emulation) and
	 * device not available (TS handling), and of course, the hypercall
	 * trap. */
	if (num == 14 || num == 13 || num == 7 || num == LGUEST_TRAP_ENTRY)
		return 0;

	/* Only trap gates (type 15) can go direct to the Guest.  Interrupt
	 * gates (type 14) disable interrupts as they are entered, which we
	 * never let the Guest do.  Not present entries (type 0x0) also can't
	 * go direct, of course 8) */
	return idt_type(trap->a, trap->b) == 0xF;
}
/*:*/

/*M:005 The Guest has the ability to turn its interrupt gates into trap gates,
 * if it is careful.  The Host will let trap gates can go directly to the
 * Guest, but the Guest needs the interrupts atomically disabled for an
 * interrupt gate.  It can do this by pointing the trap gate at instructions
 * within noirq_start and noirq_end, where it can safely disable interrupts. */

/*M:006 The Guests do not use the sysenter (fast system call) instruction,
 * because it's hardcoded to enter privilege level 0 and so can't go direct.
 * It's about twice as fast as the older "int 0x80" system call, so it might
 * still be worthwhile to handle it in the Switcher and lcall down to the
 * Guest.  The sysenter semantics are hairy tho: search for that keyword in
 * entry.S :*/

/*H:260 When we make traps go directly into the Guest, we need to make sure
 * the kernel stack is valid (ie. mapped in the page tables).  Otherwise, the
 * CPU trying to deliver the trap will fault while trying to push the interrupt
 * words on the stack: this is called a double fault, and it forces us to kill
 * the Guest.
 *
 * Which is deeply unfair, because (literally!) it wasn't the Guests' fault. */
void pin_stack_pages(struct lguest *lg)
{
	unsigned int i;

	/* Depending on the CONFIG_4KSTACKS option, the Guest can have one or
	 * two pages of stack space. */
	for (i = 0; i < lg->stack_pages; i++)
		/* The stack grows *upwards*, hence the subtraction */
		pin_page(lg, lg->esp1 - i * PAGE_SIZE);
}

/* Direct traps also mean that we need to know whenever the Guest wants to use
 * a different kernel stack, so we can change the IDT entries to use that
 * stack.  The IDT entries expect a virtual address, so unlike most addresses
 * the Guest gives us, the "esp" (stack pointer) value here is virtual, not
 * physical.
 *
 * In Linux each process has its own kernel stack, so this happens a lot: we
 * change stacks on each context switch. */
void guest_set_stack(struct lguest *lg, u32 seg, u32 esp, unsigned int pages)
{
	/* You are not allowd have a stack segment with privilege level 0: bad
	 * Guest! */
	if ((seg & 0x3) != GUEST_PL)
		kill_guest(lg, "bad stack segment %i", seg);
	/* We only expect one or two stack pages. */
	if (pages > 2)
		kill_guest(lg, "bad stack pages %u", pages);
	/* Save where the stack is, and how many pages */
	lg->ss1 = seg;
	lg->esp1 = esp;
	lg->stack_pages = pages;
	/* Make sure the new stack pages are mapped */
	pin_stack_pages(lg);
}

/* All this reference to mapping stacks leads us neatly into the other complex
 * part of the Host: page table handling. */

/*H:235 This is the routine which actually checks the Guest's IDT entry and
 * transfers it into our entry in "struct lguest": */
static void set_trap(struct lguest *lg, struct desc_struct *trap,
		     unsigned int num, u32 lo, u32 hi)
{
	u8 type = idt_type(lo, hi);

	/* We zero-out a not-present entry */
	if (!idt_present(lo, hi)) {
		trap->a = trap->b = 0;
		return;
	}

	/* We only support interrupt and trap gates. */
	if (type != 0xE && type != 0xF)
		kill_guest(lg, "bad IDT type %i", type);

	/* We only copy the handler address, present bit, privilege level and
	 * type.  The privilege level controls where the trap can be triggered
	 * manually with an "int" instruction.  This is usually GUEST_PL,
	 * except for system calls which userspace can use. */
	trap->a = ((__KERNEL_CS|GUEST_PL)<<16) | (lo&0x0000FFFF);
	trap->b = (hi&0xFFFFEF00);
}

/*H:230 While we're here, dealing with delivering traps and interrupts to the
 * Guest, we might as well complete the picture: how the Guest tells us where
 * it wants them to go.  This would be simple, except making traps fast
 * requires some tricks.
 *
 * We saw the Guest setting Interrupt Descriptor Table (IDT) entries with the
 * LHCALL_LOAD_IDT_ENTRY hypercall before: that comes here. */
void load_guest_idt_entry(struct lguest *lg, unsigned int num, u32 lo, u32 hi)
{
	/* Guest never handles: NMI, doublefault, spurious interrupt or
	 * hypercall.  We ignore when it tries to set them. */
	if (num == 2 || num == 8 || num == 15 || num == LGUEST_TRAP_ENTRY)
		return;

	/* Mark the IDT as changed: next time the Guest runs we'll know we have
	 * to copy this again. */
	lg->changed |= CHANGED_IDT;

	/* The IDT which we keep in "struct lguest" only contains 32 entries
	 * for the traps and LGUEST_IRQS (32) entries for interrupts.  We
	 * ignore attempts to set handlers for higher interrupt numbers, except
	 * for the system call "interrupt" at 128: we have a special IDT entry
	 * for that. */
	if (num < ARRAY_SIZE(lg->idt))
		set_trap(lg, &lg->idt[num], num, lo, hi);
	else if (num == SYSCALL_VECTOR)
		set_trap(lg, &lg->syscall_idt, num, lo, hi);
}

/* The default entry for each interrupt points into the Switcher routines which
 * simply return to the Host.  The run_guest() loop will then call
 * deliver_trap() to bounce it back into the Guest. */
static void default_idt_entry(struct desc_struct *idt,
			      int trap,
			      const unsigned long handler)
{
	/* A present interrupt gate. */
	u32 flags = 0x8e00;

	/* Set the privilege level on the entry for the hypercall: this allows
	 * the Guest to use the "int" instruction to trigger it. */
	if (trap == LGUEST_TRAP_ENTRY)
		flags |= (GUEST_PL << 13);

	/* Now pack it into the IDT entry in its weird format. */
	idt->a = (LGUEST_CS<<16) | (handler&0x0000FFFF);
	idt->b = (handler&0xFFFF0000) | flags;
}

/* When the Guest first starts, we put default entries into the IDT. */
void setup_default_idt_entries(struct lguest_ro_state *state,
			       const unsigned long *def)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(state->guest_idt); i++)
		default_idt_entry(&state->guest_idt[i], i, def[i]);
}

/*H:240 We don't use the IDT entries in the "struct lguest" directly, instead
 * we copy them into the IDT which we've set up for Guests on this CPU, just
 * before we run the Guest.  This routine does that copy. */
void copy_traps(const struct lguest *lg, struct desc_struct *idt,
		const unsigned long *def)
{
	unsigned int i;

	/* We can simply copy the direct traps, otherwise we use the default
	 * ones in the Switcher: they will return to the Host. */
	for (i = 0; i < FIRST_EXTERNAL_VECTOR; i++) {
		if (direct_trap(lg, &lg->idt[i], i))
			idt[i] = lg->idt[i];
		else
			default_idt_entry(&idt[i], i, def[i]);
	}

	/* Don't forget the system call trap!  The IDT entries for other
	 * interupts never change, so no need to copy them. */
	i = SYSCALL_VECTOR;
	if (direct_trap(lg, &lg->syscall_idt, i))
		idt[i] = lg->syscall_idt;
	else
		default_idt_entry(&idt[i], i, def[i]);
}

void guest_set_clockevent(struct lguest *lg, unsigned long delta)
{
	ktime_t expires;

	if (unlikely(delta == 0)) {
		/* Clock event device is shutting down. */
		hrtimer_cancel(&lg->hrt);
		return;
	}

	expires = ktime_add_ns(ktime_get_real(), delta);
	hrtimer_start(&lg->hrt, expires, HRTIMER_MODE_ABS);
}

static enum hrtimer_restart clockdev_fn(struct hrtimer *timer)
{
	struct lguest *lg = container_of(timer, struct lguest, hrt);

	set_bit(0, lg->irqs_pending);
	if (lg->halted)
		wake_up_process(lg->tsk);
	return HRTIMER_NORESTART;
}

void init_clockdev(struct lguest *lg)
{
	hrtimer_init(&lg->hrt, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	lg->hrt.function = clockdev_fn;
}
