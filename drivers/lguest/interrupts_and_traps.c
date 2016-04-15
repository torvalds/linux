/*P:800
 * Interrupts (traps) are complicated enough to earn their own file.
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
 * they had been delivered to it directly.
:*/
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/sched.h>
#include "lg.h"

/* Allow Guests to use a non-128 (ie. non-Linux) syscall trap. */
static unsigned int syscall_vector = IA32_SYSCALL_VECTOR;
module_param(syscall_vector, uint, 0444);

/* The address of the interrupt handler is split into two bits: */
static unsigned long idt_address(u32 lo, u32 hi)
{
	return (lo & 0x0000FFFF) | (hi & 0xFFFF0000);
}

/*
 * The "type" of the interrupt handler is a 4 bit field: we only support a
 * couple of types.
 */
static int idt_type(u32 lo, u32 hi)
{
	return (hi >> 8) & 0xF;
}

/* An IDT entry can't be used unless the "present" bit is set. */
static bool idt_present(u32 lo, u32 hi)
{
	return (hi & 0x8000);
}

/*
 * We need a helper to "push" a value onto the Guest's stack, since that's a
 * big part of what delivering an interrupt does.
 */
static void push_guest_stack(struct lg_cpu *cpu, unsigned long *gstack, u32 val)
{
	/* Stack grows upwards: move stack then write value. */
	*gstack -= 4;
	lgwrite(cpu, *gstack, u32, val);
}

/*H:210
 * The push_guest_interrupt_stack() routine saves Guest state on the stack for
 * an interrupt or trap.  The mechanics of delivering traps and interrupts to
 * the Guest are the same, except some traps have an "error code" which gets
 * pushed onto the stack as well: the caller tells us if this is one.
 *
 * We set up the stack just like the CPU does for a real interrupt, so it's
 * identical for the Guest (and the standard "iret" instruction will undo
 * it).
 */
static void push_guest_interrupt_stack(struct lg_cpu *cpu, bool has_err)
{
	unsigned long gstack, origstack;
	u32 eflags, ss, irq_enable;
	unsigned long virtstack;

	/*
	 * There are two cases for interrupts: one where the Guest is already
	 * in the kernel, and a more complex one where the Guest is in
	 * userspace.  We check the privilege level to find out.
	 */
	if ((cpu->regs->ss&0x3) != GUEST_PL) {
		/*
		 * The Guest told us their kernel stack with the SET_STACK
		 * hypercall: both the virtual address and the segment.
		 */
		virtstack = cpu->esp1;
		ss = cpu->ss1;

		origstack = gstack = guest_pa(cpu, virtstack);
		/*
		 * We push the old stack segment and pointer onto the new
		 * stack: when the Guest does an "iret" back from the interrupt
		 * handler the CPU will notice they're dropping privilege
		 * levels and expect these here.
		 */
		push_guest_stack(cpu, &gstack, cpu->regs->ss);
		push_guest_stack(cpu, &gstack, cpu->regs->esp);
	} else {
		/* We're staying on the same Guest (kernel) stack. */
		virtstack = cpu->regs->esp;
		ss = cpu->regs->ss;

		origstack = gstack = guest_pa(cpu, virtstack);
	}

	/*
	 * Remember that we never let the Guest actually disable interrupts, so
	 * the "Interrupt Flag" bit is always set.  We copy that bit from the
	 * Guest's "irq_enabled" field into the eflags word: we saw the Guest
	 * copy it back in "lguest_iret".
	 */
	eflags = cpu->regs->eflags;
	if (get_user(irq_enable, &cpu->lg->lguest_data->irq_enabled) == 0
	    && !(irq_enable & X86_EFLAGS_IF))
		eflags &= ~X86_EFLAGS_IF;

	/*
	 * An interrupt is expected to push three things on the stack: the old
	 * "eflags" word, the old code segment, and the old instruction
	 * pointer.
	 */
	push_guest_stack(cpu, &gstack, eflags);
	push_guest_stack(cpu, &gstack, cpu->regs->cs);
	push_guest_stack(cpu, &gstack, cpu->regs->eip);

	/* For the six traps which supply an error code, we push that, too. */
	if (has_err)
		push_guest_stack(cpu, &gstack, cpu->regs->errcode);

	/* Adjust the stack pointer and stack segment. */
	cpu->regs->ss = ss;
	cpu->regs->esp = virtstack + (gstack - origstack);
}

/*
 * This actually makes the Guest start executing the given interrupt/trap
 * handler.
 *
 * "lo" and "hi" are the two parts of the Interrupt Descriptor Table for this
 * interrupt or trap.  It's split into two parts for traditional reasons: gcc
 * on i386 used to be frightened by 64 bit numbers.
 */
static void guest_run_interrupt(struct lg_cpu *cpu, u32 lo, u32 hi)
{
	/* If we're already in the kernel, we don't change stacks. */
	if ((cpu->regs->ss&0x3) != GUEST_PL)
		cpu->regs->ss = cpu->esp1;

	/*
	 * Set the code segment and the address to execute.
	 */
	cpu->regs->cs = (__KERNEL_CS|GUEST_PL);
	cpu->regs->eip = idt_address(lo, hi);

	/*
	 * Trapping always clears these flags:
	 * TF: Trap flag
	 * VM: Virtual 8086 mode
	 * RF: Resume
	 * NT: Nested task.
	 */
	cpu->regs->eflags &=
		~(X86_EFLAGS_TF|X86_EFLAGS_VM|X86_EFLAGS_RF|X86_EFLAGS_NT);

	/*
	 * There are two kinds of interrupt handlers: 0xE is an "interrupt
	 * gate" which expects interrupts to be disabled on entry.
	 */
	if (idt_type(lo, hi) == 0xE)
		if (put_user(0, &cpu->lg->lguest_data->irq_enabled))
			kill_guest(cpu, "Disabling interrupts");
}

/* This restores the eflags word which was pushed on the stack by a trap */
static void restore_eflags(struct lg_cpu *cpu)
{
	/* This is the physical address of the stack. */
	unsigned long stack_pa = guest_pa(cpu, cpu->regs->esp);

	/*
	 * Stack looks like this:
	 * Address	Contents
	 * esp		EIP
	 * esp + 4	CS
	 * esp + 8	EFLAGS
	 */
	cpu->regs->eflags = lgread(cpu, stack_pa + 8, u32);
	cpu->regs->eflags &=
		~(X86_EFLAGS_TF|X86_EFLAGS_VM|X86_EFLAGS_RF|X86_EFLAGS_NT);
}

/*H:205
 * Virtual Interrupts.
 *
 * interrupt_pending() returns the first pending interrupt which isn't blocked
 * by the Guest.  It is called before every entry to the Guest, and just before
 * we go to sleep when the Guest has halted itself.
 */
unsigned int interrupt_pending(struct lg_cpu *cpu, bool *more)
{
	unsigned int irq;
	DECLARE_BITMAP(blk, LGUEST_IRQS);

	/* If the Guest hasn't even initialized yet, we can do nothing. */
	if (!cpu->lg->lguest_data)
		return LGUEST_IRQS;

	/*
	 * Take our "irqs_pending" array and remove any interrupts the Guest
	 * wants blocked: the result ends up in "blk".
	 */
	if (copy_from_user(&blk, cpu->lg->lguest_data->blocked_interrupts,
			   sizeof(blk)))
		return LGUEST_IRQS;
	bitmap_andnot(blk, cpu->irqs_pending, blk, LGUEST_IRQS);

	/* Find the first interrupt. */
	irq = find_first_bit(blk, LGUEST_IRQS);
	*more = find_next_bit(blk, LGUEST_IRQS, irq+1);

	return irq;
}

/*
 * This actually diverts the Guest to running an interrupt handler, once an
 * interrupt has been identified by interrupt_pending().
 */
void try_deliver_interrupt(struct lg_cpu *cpu, unsigned int irq, bool more)
{
	struct desc_struct *idt;

	BUG_ON(irq >= LGUEST_IRQS);

	/* If they're halted, interrupts restart them. */
	if (cpu->halted) {
		/* Re-enable interrupts. */
		if (put_user(X86_EFLAGS_IF, &cpu->lg->lguest_data->irq_enabled))
			kill_guest(cpu, "Re-enabling interrupts");
		cpu->halted = 0;
	} else {
		/* Otherwise we check if they have interrupts disabled. */
		u32 irq_enabled;
		if (get_user(irq_enabled, &cpu->lg->lguest_data->irq_enabled))
			irq_enabled = 0;
		if (!irq_enabled) {
			/* Make sure they know an IRQ is pending. */
			put_user(X86_EFLAGS_IF,
				 &cpu->lg->lguest_data->irq_pending);
			return;
		}
	}

	/*
	 * Look at the IDT entry the Guest gave us for this interrupt.  The
	 * first 32 (FIRST_EXTERNAL_VECTOR) entries are for traps, so we skip
	 * over them.
	 */
	idt = &cpu->arch.idt[FIRST_EXTERNAL_VECTOR+irq];
	/* If they don't have a handler (yet?), we just ignore it */
	if (idt_present(idt->a, idt->b)) {
		/* OK, mark it no longer pending and deliver it. */
		clear_bit(irq, cpu->irqs_pending);

		/*
		 * They may be about to iret, where they asked us never to
		 * deliver interrupts.  In this case, we can emulate that iret
		 * then immediately deliver the interrupt.  This is basically
		 * a noop: the iret would pop the interrupt frame and restore
		 * eflags, and then we'd set it up again.  So just restore the
		 * eflags word and jump straight to the handler in this case.
		 *
		 * Denys Vlasenko points out that this isn't quite right: if
		 * the iret was returning to userspace, then that interrupt
		 * would reset the stack pointer (which the Guest told us
		 * about via LHCALL_SET_STACK).  But unless the Guest is being
		 * *really* weird, that will be the same as the current stack
		 * anyway.
		 */
		if (cpu->regs->eip == cpu->lg->noirq_iret) {
			restore_eflags(cpu);
		} else {
			/*
			 * set_guest_interrupt() takes a flag to say whether
			 * this interrupt pushes an error code onto the stack
			 * as well: virtual interrupts never do.
			 */
			push_guest_interrupt_stack(cpu, false);
		}
		/* Actually make Guest cpu jump to handler. */
		guest_run_interrupt(cpu, idt->a, idt->b);
	}

	/*
	 * Every time we deliver an interrupt, we update the timestamp in the
	 * Guest's lguest_data struct.  It would be better for the Guest if we
	 * did this more often, but it can actually be quite slow: doing it
	 * here is a compromise which means at least it gets updated every
	 * timer interrupt.
	 */
	write_timestamp(cpu);

	/*
	 * If there are no other interrupts we want to deliver, clear
	 * the pending flag.
	 */
	if (!more)
		put_user(0, &cpu->lg->lguest_data->irq_pending);
}

/* And this is the routine when we want to set an interrupt for the Guest. */
void set_interrupt(struct lg_cpu *cpu, unsigned int irq)
{
	/*
	 * Next time the Guest runs, the core code will see if it can deliver
	 * this interrupt.
	 */
	set_bit(irq, cpu->irqs_pending);

	/*
	 * Make sure it sees it; it might be asleep (eg. halted), or running
	 * the Guest right now, in which case kick_process() will knock it out.
	 */
	if (!wake_up_process(cpu->tsk))
		kick_process(cpu->tsk);
}
/*:*/

/*
 * Linux uses trap 128 for system calls.  Plan9 uses 64, and Ron Minnich sent
 * me a patch, so we support that too.  It'd be a big step for lguest if half
 * the Plan 9 user base were to start using it.
 *
 * Actually now I think of it, it's possible that Ron *is* half the Plan 9
 * userbase.  Oh well.
 */
bool could_be_syscall(unsigned int num)
{
	/* Normal Linux IA32_SYSCALL_VECTOR or reserved vector? */
	return num == IA32_SYSCALL_VECTOR || num == syscall_vector;
}

/* The syscall vector it wants must be unused by Host. */
bool check_syscall_vector(struct lguest *lg)
{
	u32 vector;

	if (get_user(vector, &lg->lguest_data->syscall_vec))
		return false;

	return could_be_syscall(vector);
}

int init_interrupts(void)
{
	/* If they want some strange system call vector, reserve it now */
	if (syscall_vector != IA32_SYSCALL_VECTOR) {
		if (test_bit(syscall_vector, used_vectors) ||
		    vector_used_by_percpu_irq(syscall_vector)) {
			printk(KERN_ERR "lg: couldn't reserve syscall %u\n",
				 syscall_vector);
			return -EBUSY;
		}
		set_bit(syscall_vector, used_vectors);
	}

	return 0;
}

void free_interrupts(void)
{
	if (syscall_vector != IA32_SYSCALL_VECTOR)
		clear_bit(syscall_vector, used_vectors);
}

/*H:220
 * Now we've got the routines to deliver interrupts, delivering traps like
 * page fault is easy.  The only trick is that Intel decided that some traps
 * should have error codes:
 */
static bool has_err(unsigned int trap)
{
	return (trap == 8 || (trap >= 10 && trap <= 14) || trap == 17);
}

/* deliver_trap() returns true if it could deliver the trap. */
bool deliver_trap(struct lg_cpu *cpu, unsigned int num)
{
	/*
	 * Trap numbers are always 8 bit, but we set an impossible trap number
	 * for traps inside the Switcher, so check that here.
	 */
	if (num >= ARRAY_SIZE(cpu->arch.idt))
		return false;

	/*
	 * Early on the Guest hasn't set the IDT entries (or maybe it put a
	 * bogus one in): if we fail here, the Guest will be killed.
	 */
	if (!idt_present(cpu->arch.idt[num].a, cpu->arch.idt[num].b))
		return false;
	push_guest_interrupt_stack(cpu, has_err(num));
	guest_run_interrupt(cpu, cpu->arch.idt[num].a,
			    cpu->arch.idt[num].b);
	return true;
}

/*H:250
 * Here's the hard part: returning to the Host every time a trap happens
 * and then calling deliver_trap() and re-entering the Guest is slow.
 * Particularly because Guest userspace system calls are traps (usually trap
 * 128).
 *
 * So we'd like to set up the IDT to tell the CPU to deliver traps directly
 * into the Guest.  This is possible, but the complexities cause the size of
 * this file to double!  However, 150 lines of code is worth writing for taking
 * system calls down from 1750ns to 270ns.  Plus, if lguest didn't do it, all
 * the other hypervisors would beat it up at lunchtime.
 *
 * This routine indicates if a particular trap number could be delivered
 * directly.
 *
 * Unfortunately, Linux 4.6 started using an interrupt gate instead of a
 * trap gate for syscalls, so this trick is ineffective.  See Mastery for
 * how we could do this anyway...
 */
static bool direct_trap(unsigned int num)
{
	/*
	 * Hardware interrupts don't go to the Guest at all (except system
	 * call).
	 */
	if (num >= FIRST_EXTERNAL_VECTOR && !could_be_syscall(num))
		return false;

	/*
	 * The Host needs to see page faults (for shadow paging and to save the
	 * fault address), general protection faults (in/out emulation) and
	 * device not available (TS handling) and of course, the hypercall trap.
	 */
	return num != 14 && num != 13 && num != 7 && num != LGUEST_TRAP_ENTRY;
}
/*:*/

/*M:005
 * The Guest has the ability to turn its interrupt gates into trap gates,
 * if it is careful.  The Host will let trap gates can go directly to the
 * Guest, but the Guest needs the interrupts atomically disabled for an
 * interrupt gate.  The Host could provide a mechanism to register more
 * "no-interrupt" regions, and the Guest could point the trap gate at
 * instructions within that region, where it can safely disable interrupts.
 */

/*M:006
 * The Guests do not use the sysenter (fast system call) instruction,
 * because it's hardcoded to enter privilege level 0 and so can't go direct.
 * It's about twice as fast as the older "int 0x80" system call, so it might
 * still be worthwhile to handle it in the Switcher and lcall down to the
 * Guest.  The sysenter semantics are hairy tho: search for that keyword in
 * entry.S
:*/

/*H:260
 * When we make traps go directly into the Guest, we need to make sure
 * the kernel stack is valid (ie. mapped in the page tables).  Otherwise, the
 * CPU trying to deliver the trap will fault while trying to push the interrupt
 * words on the stack: this is called a double fault, and it forces us to kill
 * the Guest.
 *
 * Which is deeply unfair, because (literally!) it wasn't the Guests' fault.
 */
void pin_stack_pages(struct lg_cpu *cpu)
{
	unsigned int i;

	/*
	 * Depending on the CONFIG_4KSTACKS option, the Guest can have one or
	 * two pages of stack space.
	 */
	for (i = 0; i < cpu->lg->stack_pages; i++)
		/*
		 * The stack grows *upwards*, so the address we're given is the
		 * start of the page after the kernel stack.  Subtract one to
		 * get back onto the first stack page, and keep subtracting to
		 * get to the rest of the stack pages.
		 */
		pin_page(cpu, cpu->esp1 - 1 - i * PAGE_SIZE);
}

/*
 * Direct traps also mean that we need to know whenever the Guest wants to use
 * a different kernel stack, so we can change the guest TSS to use that
 * stack.  The TSS entries expect a virtual address, so unlike most addresses
 * the Guest gives us, the "esp" (stack pointer) value here is virtual, not
 * physical.
 *
 * In Linux each process has its own kernel stack, so this happens a lot: we
 * change stacks on each context switch.
 */
void guest_set_stack(struct lg_cpu *cpu, u32 seg, u32 esp, unsigned int pages)
{
	/*
	 * You're not allowed a stack segment with privilege level 0: bad Guest!
	 */
	if ((seg & 0x3) != GUEST_PL)
		kill_guest(cpu, "bad stack segment %i", seg);
	/* We only expect one or two stack pages. */
	if (pages > 2)
		kill_guest(cpu, "bad stack pages %u", pages);
	/* Save where the stack is, and how many pages */
	cpu->ss1 = seg;
	cpu->esp1 = esp;
	cpu->lg->stack_pages = pages;
	/* Make sure the new stack pages are mapped */
	pin_stack_pages(cpu);
}

/*
 * All this reference to mapping stacks leads us neatly into the other complex
 * part of the Host: page table handling.
 */

/*H:235
 * This is the routine which actually checks the Guest's IDT entry and
 * transfers it into the entry in "struct lguest":
 */
static void set_trap(struct lg_cpu *cpu, struct desc_struct *trap,
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
		kill_guest(cpu, "bad IDT type %i", type);

	/*
	 * We only copy the handler address, present bit, privilege level and
	 * type.  The privilege level controls where the trap can be triggered
	 * manually with an "int" instruction.  This is usually GUEST_PL,
	 * except for system calls which userspace can use.
	 */
	trap->a = ((__KERNEL_CS|GUEST_PL)<<16) | (lo&0x0000FFFF);
	trap->b = (hi&0xFFFFEF00);
}

/*H:230
 * While we're here, dealing with delivering traps and interrupts to the
 * Guest, we might as well complete the picture: how the Guest tells us where
 * it wants them to go.  This would be simple, except making traps fast
 * requires some tricks.
 *
 * We saw the Guest setting Interrupt Descriptor Table (IDT) entries with the
 * LHCALL_LOAD_IDT_ENTRY hypercall before: that comes here.
 */
void load_guest_idt_entry(struct lg_cpu *cpu, unsigned int num, u32 lo, u32 hi)
{
	/*
	 * Guest never handles: NMI, doublefault, spurious interrupt or
	 * hypercall.  We ignore when it tries to set them.
	 */
	if (num == 2 || num == 8 || num == 15 || num == LGUEST_TRAP_ENTRY)
		return;

	/*
	 * Mark the IDT as changed: next time the Guest runs we'll know we have
	 * to copy this again.
	 */
	cpu->changed |= CHANGED_IDT;

	/* Check that the Guest doesn't try to step outside the bounds. */
	if (num >= ARRAY_SIZE(cpu->arch.idt))
		kill_guest(cpu, "Setting idt entry %u", num);
	else
		set_trap(cpu, &cpu->arch.idt[num], num, lo, hi);
}

/*
 * The default entry for each interrupt points into the Switcher routines which
 * simply return to the Host.  The run_guest() loop will then call
 * deliver_trap() to bounce it back into the Guest.
 */
static void default_idt_entry(struct desc_struct *idt,
			      int trap,
			      const unsigned long handler,
			      const struct desc_struct *base)
{
	/* A present interrupt gate. */
	u32 flags = 0x8e00;

	/*
	 * Set the privilege level on the entry for the hypercall: this allows
	 * the Guest to use the "int" instruction to trigger it.
	 */
	if (trap == LGUEST_TRAP_ENTRY)
		flags |= (GUEST_PL << 13);
	else if (base)
		/*
		 * Copy privilege level from what Guest asked for.  This allows
		 * debug (int 3) traps from Guest userspace, for example.
		 */
		flags |= (base->b & 0x6000);

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
		default_idt_entry(&state->guest_idt[i], i, def[i], NULL);
}

/*H:240
 * We don't use the IDT entries in the "struct lguest" directly, instead
 * we copy them into the IDT which we've set up for Guests on this CPU, just
 * before we run the Guest.  This routine does that copy.
 */
void copy_traps(const struct lg_cpu *cpu, struct desc_struct *idt,
		const unsigned long *def)
{
	unsigned int i;

	/*
	 * We can simply copy the direct traps, otherwise we use the default
	 * ones in the Switcher: they will return to the Host.
	 */
	for (i = 0; i < ARRAY_SIZE(cpu->arch.idt); i++) {
		const struct desc_struct *gidt = &cpu->arch.idt[i];

		/* If no Guest can ever override this trap, leave it alone. */
		if (!direct_trap(i))
			continue;

		/*
		 * Only trap gates (type 15) can go direct to the Guest.
		 * Interrupt gates (type 14) disable interrupts as they are
		 * entered, which we never let the Guest do.  Not present
		 * entries (type 0x0) also can't go direct, of course.
		 *
		 * If it can't go direct, we still need to copy the priv. level:
		 * they might want to give userspace access to a software
		 * interrupt.
		 */
		if (idt_type(gidt->a, gidt->b) == 0xF)
			idt[i] = *gidt;
		else
			default_idt_entry(&idt[i], i, def[i], gidt);
	}
}

/*H:200
 * The Guest Clock.
 *
 * There are two sources of virtual interrupts.  We saw one in lguest_user.c:
 * the Launcher sending interrupts for virtual devices.  The other is the Guest
 * timer interrupt.
 *
 * The Guest uses the LHCALL_SET_CLOCKEVENT hypercall to tell us how long to
 * the next timer interrupt (in nanoseconds).  We use the high-resolution timer
 * infrastructure to set a callback at that time.
 *
 * 0 means "turn off the clock".
 */
void guest_set_clockevent(struct lg_cpu *cpu, unsigned long delta)
{
	ktime_t expires;

	if (unlikely(delta == 0)) {
		/* Clock event device is shutting down. */
		hrtimer_cancel(&cpu->hrt);
		return;
	}

	/*
	 * We use wallclock time here, so the Guest might not be running for
	 * all the time between now and the timer interrupt it asked for.  This
	 * is almost always the right thing to do.
	 */
	expires = ktime_add_ns(ktime_get_real(), delta);
	hrtimer_start(&cpu->hrt, expires, HRTIMER_MODE_ABS);
}

/* This is the function called when the Guest's timer expires. */
static enum hrtimer_restart clockdev_fn(struct hrtimer *timer)
{
	struct lg_cpu *cpu = container_of(timer, struct lg_cpu, hrt);

	/* Remember the first interrupt is the timer interrupt. */
	set_interrupt(cpu, 0);
	return HRTIMER_NORESTART;
}

/* This sets up the timer for this Guest. */
void init_clockdev(struct lg_cpu *cpu)
{
	hrtimer_init(&cpu->hrt, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	cpu->hrt.function = clockdev_fn;
}
