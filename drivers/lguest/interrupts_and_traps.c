#include <linux/uaccess.h>
#include "lg.h"

static unsigned long idt_address(u32 lo, u32 hi)
{
	return (lo & 0x0000FFFF) | (hi & 0xFFFF0000);
}

static int idt_type(u32 lo, u32 hi)
{
	return (hi >> 8) & 0xF;
}

static int idt_present(u32 lo, u32 hi)
{
	return (hi & 0x8000);
}

static void push_guest_stack(struct lguest *lg, unsigned long *gstack, u32 val)
{
	*gstack -= 4;
	lgwrite_u32(lg, *gstack, val);
}

static void set_guest_interrupt(struct lguest *lg, u32 lo, u32 hi, int has_err)
{
	unsigned long gstack;
	u32 eflags, ss, irq_enable;

	/* If they want a ring change, we use new stack and push old ss/esp */
	if ((lg->regs->ss&0x3) != GUEST_PL) {
		gstack = guest_pa(lg, lg->esp1);
		ss = lg->ss1;
		push_guest_stack(lg, &gstack, lg->regs->ss);
		push_guest_stack(lg, &gstack, lg->regs->esp);
	} else {
		gstack = guest_pa(lg, lg->regs->esp);
		ss = lg->regs->ss;
	}

	/* We use IF bit in eflags to indicate whether irqs were enabled
	   (it's always 1, since irqs are enabled when guest is running). */
	eflags = lg->regs->eflags;
	if (get_user(irq_enable, &lg->lguest_data->irq_enabled) == 0
	    && !(irq_enable & X86_EFLAGS_IF))
		eflags &= ~X86_EFLAGS_IF;

	push_guest_stack(lg, &gstack, eflags);
	push_guest_stack(lg, &gstack, lg->regs->cs);
	push_guest_stack(lg, &gstack, lg->regs->eip);

	if (has_err)
		push_guest_stack(lg, &gstack, lg->regs->errcode);

	/* Change the real stack so switcher returns to trap handler */
	lg->regs->ss = ss;
	lg->regs->esp = gstack + lg->page_offset;
	lg->regs->cs = (__KERNEL_CS|GUEST_PL);
	lg->regs->eip = idt_address(lo, hi);

	/* Disable interrupts for an interrupt gate. */
	if (idt_type(lo, hi) == 0xE)
		if (put_user(0, &lg->lguest_data->irq_enabled))
			kill_guest(lg, "Disabling interrupts");
}

void maybe_do_interrupt(struct lguest *lg)
{
	unsigned int irq;
	DECLARE_BITMAP(blk, LGUEST_IRQS);
	struct desc_struct *idt;

	if (!lg->lguest_data)
		return;

	/* Mask out any interrupts they have blocked. */
	if (copy_from_user(&blk, lg->lguest_data->blocked_interrupts,
			   sizeof(blk)))
		return;

	bitmap_andnot(blk, lg->irqs_pending, blk, LGUEST_IRQS);

	irq = find_first_bit(blk, LGUEST_IRQS);
	if (irq >= LGUEST_IRQS)
		return;

	if (lg->regs->eip >= lg->noirq_start && lg->regs->eip < lg->noirq_end)
		return;

	/* If they're halted, we re-enable interrupts. */
	if (lg->halted) {
		/* Re-enable interrupts. */
		if (put_user(X86_EFLAGS_IF, &lg->lguest_data->irq_enabled))
			kill_guest(lg, "Re-enabling interrupts");
		lg->halted = 0;
	} else {
		/* Maybe they have interrupts disabled? */
		u32 irq_enabled;
		if (get_user(irq_enabled, &lg->lguest_data->irq_enabled))
			irq_enabled = 0;
		if (!irq_enabled)
			return;
	}

	idt = &lg->idt[FIRST_EXTERNAL_VECTOR+irq];
	if (idt_present(idt->a, idt->b)) {
		clear_bit(irq, lg->irqs_pending);
		set_guest_interrupt(lg, idt->a, idt->b, 0);
	}
}

static int has_err(unsigned int trap)
{
	return (trap == 8 || (trap >= 10 && trap <= 14) || trap == 17);
}

int deliver_trap(struct lguest *lg, unsigned int num)
{
	u32 lo = lg->idt[num].a, hi = lg->idt[num].b;

	if (!idt_present(lo, hi))
		return 0;
	set_guest_interrupt(lg, lo, hi, has_err(num));
	return 1;
}

static int direct_trap(const struct lguest *lg,
		       const struct desc_struct *trap,
		       unsigned int num)
{
	/* Hardware interrupts don't go to guest (except syscall). */
	if (num >= FIRST_EXTERNAL_VECTOR && num != SYSCALL_VECTOR)
		return 0;

	/* We intercept page fault (demand shadow paging & cr2 saving)
	   protection fault (in/out emulation) and device not
	   available (TS handling), and hypercall */
	if (num == 14 || num == 13 || num == 7 || num == LGUEST_TRAP_ENTRY)
		return 0;

	/* Interrupt gates (0xE) or not present (0x0) can't go direct. */
	return idt_type(trap->a, trap->b) == 0xF;
}

void pin_stack_pages(struct lguest *lg)
{
	unsigned int i;

	for (i = 0; i < lg->stack_pages; i++)
		pin_page(lg, lg->esp1 - i * PAGE_SIZE);
}

void guest_set_stack(struct lguest *lg, u32 seg, u32 esp, unsigned int pages)
{
	/* You cannot have a stack segment with priv level 0. */
	if ((seg & 0x3) != GUEST_PL)
		kill_guest(lg, "bad stack segment %i", seg);
	if (pages > 2)
		kill_guest(lg, "bad stack pages %u", pages);
	lg->ss1 = seg;
	lg->esp1 = esp;
	lg->stack_pages = pages;
	pin_stack_pages(lg);
}

/* Set up trap in IDT. */
static void set_trap(struct lguest *lg, struct desc_struct *trap,
		     unsigned int num, u32 lo, u32 hi)
{
	u8 type = idt_type(lo, hi);

	if (!idt_present(lo, hi)) {
		trap->a = trap->b = 0;
		return;
	}

	if (type != 0xE && type != 0xF)
		kill_guest(lg, "bad IDT type %i", type);

	trap->a = ((__KERNEL_CS|GUEST_PL)<<16) | (lo&0x0000FFFF);
	trap->b = (hi&0xFFFFEF00);
}

void load_guest_idt_entry(struct lguest *lg, unsigned int num, u32 lo, u32 hi)
{
	/* Guest never handles: NMI, doublefault, hypercall, spurious irq. */
	if (num == 2 || num == 8 || num == 15 || num == LGUEST_TRAP_ENTRY)
		return;

	lg->changed |= CHANGED_IDT;
	if (num < ARRAY_SIZE(lg->idt))
		set_trap(lg, &lg->idt[num], num, lo, hi);
	else if (num == SYSCALL_VECTOR)
		set_trap(lg, &lg->syscall_idt, num, lo, hi);
}

static void default_idt_entry(struct desc_struct *idt,
			      int trap,
			      const unsigned long handler)
{
	u32 flags = 0x8e00;

	/* They can't "int" into any of them except hypercall. */
	if (trap == LGUEST_TRAP_ENTRY)
		flags |= (GUEST_PL << 13);

	idt->a = (LGUEST_CS<<16) | (handler&0x0000FFFF);
	idt->b = (handler&0xFFFF0000) | flags;
}

void setup_default_idt_entries(struct lguest_ro_state *state,
			       const unsigned long *def)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(state->guest_idt); i++)
		default_idt_entry(&state->guest_idt[i], i, def[i]);
}

void copy_traps(const struct lguest *lg, struct desc_struct *idt,
		const unsigned long *def)
{
	unsigned int i;

	/* All hardware interrupts are same whatever the guest: only the
	 * traps might be different. */
	for (i = 0; i < FIRST_EXTERNAL_VECTOR; i++) {
		if (direct_trap(lg, &lg->idt[i], i))
			idt[i] = lg->idt[i];
		else
			default_idt_entry(&idt[i], i, def[i]);
	}
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
