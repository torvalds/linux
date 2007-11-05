/*P:010
 * A hypervisor allows multiple Operating Systems to run on a single machine.
 * To quote David Wheeler: "Any problem in computer science can be solved with
 * another layer of indirection."
 *
 * We keep things simple in two ways.  First, we start with a normal Linux
 * kernel and insert a module (lg.ko) which allows us to run other Linux
 * kernels the same way we'd run processes.  We call the first kernel the Host,
 * and the others the Guests.  The program which sets up and configures Guests
 * (such as the example in Documentation/lguest/lguest.c) is called the
 * Launcher.
 *
 * Secondly, we only run specially modified Guests, not normal kernels.  When
 * you set CONFIG_LGUEST to 'y' or 'm', this automatically sets
 * CONFIG_LGUEST_GUEST=y, which compiles this file into the kernel so it knows
 * how to be a Guest.  This means that you can use the same kernel you boot
 * normally (ie. as a Host) as a Guest.
 *
 * These Guests know that they cannot do privileged operations, such as disable
 * interrupts, and that they have to ask the Host to do such things explicitly.
 * This file consists of all the replacements for such low-level native
 * hardware operations: these special Guest versions call the Host.
 *
 * So how does the kernel know it's a Guest?  The Guest starts at a special
 * entry point marked with a magic string, which sets up a few things then
 * calls here.  We replace the native functions various "paravirt" structures
 * with our Guest versions, then boot like normal. :*/

/*
 * Copyright (C) 2006, Rusty Russell <rusty@rustcorp.com.au> IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/start_kernel.h>
#include <linux/string.h>
#include <linux/console.h>
#include <linux/screen_info.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/lguest.h>
#include <linux/lguest_launcher.h>
#include <linux/virtio_console.h>
#include <linux/pm.h>
#include <asm/paravirt.h>
#include <asm/param.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/desc.h>
#include <asm/setup.h>
#include <asm/e820.h>
#include <asm/mce.h>
#include <asm/io.h>
#include <asm/i387.h>

/*G:010 Welcome to the Guest!
 *
 * The Guest in our tale is a simple creature: identical to the Host but
 * behaving in simplified but equivalent ways.  In particular, the Guest is the
 * same kernel as the Host (or at least, built from the same source code). :*/

/* Declarations for definitions in lguest_guest.S */
extern char lguest_noirq_start[], lguest_noirq_end[];
extern const char lgstart_cli[], lgend_cli[];
extern const char lgstart_sti[], lgend_sti[];
extern const char lgstart_popf[], lgend_popf[];
extern const char lgstart_pushf[], lgend_pushf[];
extern const char lgstart_iret[], lgend_iret[];
extern void lguest_iret(void);

struct lguest_data lguest_data = {
	.hcall_status = { [0 ... LHCALL_RING_SIZE-1] = 0xFF },
	.noirq_start = (u32)lguest_noirq_start,
	.noirq_end = (u32)lguest_noirq_end,
	.kernel_address = PAGE_OFFSET,
	.blocked_interrupts = { 1 }, /* Block timer interrupts */
	.syscall_vec = SYSCALL_VECTOR,
};
static cycle_t clock_base;

/*G:037 async_hcall() is pretty simple: I'm quite proud of it really.  We have a
 * ring buffer of stored hypercalls which the Host will run though next time we
 * do a normal hypercall.  Each entry in the ring has 4 slots for the hypercall
 * arguments, and a "hcall_status" word which is 0 if the call is ready to go,
 * and 255 once the Host has finished with it.
 *
 * If we come around to a slot which hasn't been finished, then the table is
 * full and we just make the hypercall directly.  This has the nice side
 * effect of causing the Host to run all the stored calls in the ring buffer
 * which empties it for next time! */
static void async_hcall(unsigned long call, unsigned long arg1,
			unsigned long arg2, unsigned long arg3)
{
	/* Note: This code assumes we're uniprocessor. */
	static unsigned int next_call;
	unsigned long flags;

	/* Disable interrupts if not already disabled: we don't want an
	 * interrupt handler making a hypercall while we're already doing
	 * one! */
	local_irq_save(flags);
	if (lguest_data.hcall_status[next_call] != 0xFF) {
		/* Table full, so do normal hcall which will flush table. */
		hcall(call, arg1, arg2, arg3);
	} else {
		lguest_data.hcalls[next_call].arg0 = call;
		lguest_data.hcalls[next_call].arg1 = arg1;
		lguest_data.hcalls[next_call].arg2 = arg2;
		lguest_data.hcalls[next_call].arg3 = arg3;
		/* Arguments must all be written before we mark it to go */
		wmb();
		lguest_data.hcall_status[next_call] = 0;
		if (++next_call == LHCALL_RING_SIZE)
			next_call = 0;
	}
	local_irq_restore(flags);
}

/*G:035 Notice the lazy_hcall() above, rather than hcall().  This is our first
 * real optimization trick!
 *
 * When lazy_mode is set, it means we're allowed to defer all hypercalls and do
 * them as a batch when lazy_mode is eventually turned off.  Because hypercalls
 * are reasonably expensive, batching them up makes sense.  For example, a
 * large munmap might update dozens of page table entries: that code calls
 * paravirt_enter_lazy_mmu(), does the dozen updates, then calls
 * lguest_leave_lazy_mode().
 *
 * So, when we're in lazy mode, we call async_hcall() to store the call for
 * future processing. */
static void lazy_hcall(unsigned long call,
		       unsigned long arg1,
		       unsigned long arg2,
		       unsigned long arg3)
{
	if (paravirt_get_lazy_mode() == PARAVIRT_LAZY_NONE)
		hcall(call, arg1, arg2, arg3);
	else
		async_hcall(call, arg1, arg2, arg3);
}

/* When lazy mode is turned off reset the per-cpu lazy mode variable and then
 * issue a hypercall to flush any stored calls. */
static void lguest_leave_lazy_mode(void)
{
	paravirt_leave_lazy(paravirt_get_lazy_mode());
	hcall(LHCALL_FLUSH_ASYNC, 0, 0, 0);
}

/*G:033
 * After that diversion we return to our first native-instruction
 * replacements: four functions for interrupt control.
 *
 * The simplest way of implementing these would be to have "turn interrupts
 * off" and "turn interrupts on" hypercalls.  Unfortunately, this is too slow:
 * these are by far the most commonly called functions of those we override.
 *
 * So instead we keep an "irq_enabled" field inside our "struct lguest_data",
 * which the Guest can update with a single instruction.  The Host knows to
 * check there when it wants to deliver an interrupt.
 */

/* save_flags() is expected to return the processor state (ie. "eflags").  The
 * eflags word contains all kind of stuff, but in practice Linux only cares
 * about the interrupt flag.  Our "save_flags()" just returns that. */
static unsigned long save_fl(void)
{
	return lguest_data.irq_enabled;
}

/* restore_flags() just sets the flags back to the value given. */
static void restore_fl(unsigned long flags)
{
	lguest_data.irq_enabled = flags;
}

/* Interrupts go off... */
static void irq_disable(void)
{
	lguest_data.irq_enabled = 0;
}

/* Interrupts go on... */
static void irq_enable(void)
{
	lguest_data.irq_enabled = X86_EFLAGS_IF;
}
/*:*/
/*M:003 Note that we don't check for outstanding interrupts when we re-enable
 * them (or when we unmask an interrupt).  This seems to work for the moment,
 * since interrupts are rare and we'll just get the interrupt on the next timer
 * tick, but when we turn on CONFIG_NO_HZ, we should revisit this.  One way
 * would be to put the "irq_enabled" field in a page by itself, and have the
 * Host write-protect it when an interrupt comes in when irqs are disabled.
 * There will then be a page fault as soon as interrupts are re-enabled. :*/

/*G:034
 * The Interrupt Descriptor Table (IDT).
 *
 * The IDT tells the processor what to do when an interrupt comes in.  Each
 * entry in the table is a 64-bit descriptor: this holds the privilege level,
 * address of the handler, and... well, who cares?  The Guest just asks the
 * Host to make the change anyway, because the Host controls the real IDT.
 */
static void lguest_write_idt_entry(struct desc_struct *dt,
				   int entrynum, u32 low, u32 high)
{
	/* Keep the local copy up to date. */
	write_dt_entry(dt, entrynum, low, high);
	/* Tell Host about this new entry. */
	hcall(LHCALL_LOAD_IDT_ENTRY, entrynum, low, high);
}

/* Changing to a different IDT is very rare: we keep the IDT up-to-date every
 * time it is written, so we can simply loop through all entries and tell the
 * Host about them. */
static void lguest_load_idt(const struct Xgt_desc_struct *desc)
{
	unsigned int i;
	struct desc_struct *idt = (void *)desc->address;

	for (i = 0; i < (desc->size+1)/8; i++)
		hcall(LHCALL_LOAD_IDT_ENTRY, i, idt[i].a, idt[i].b);
}

/*
 * The Global Descriptor Table.
 *
 * The Intel architecture defines another table, called the Global Descriptor
 * Table (GDT).  You tell the CPU where it is (and its size) using the "lgdt"
 * instruction, and then several other instructions refer to entries in the
 * table.  There are three entries which the Switcher needs, so the Host simply
 * controls the entire thing and the Guest asks it to make changes using the
 * LOAD_GDT hypercall.
 *
 * This is the opposite of the IDT code where we have a LOAD_IDT_ENTRY
 * hypercall and use that repeatedly to load a new IDT.  I don't think it
 * really matters, but wouldn't it be nice if they were the same?
 */
static void lguest_load_gdt(const struct Xgt_desc_struct *desc)
{
	BUG_ON((desc->size+1)/8 != GDT_ENTRIES);
	hcall(LHCALL_LOAD_GDT, __pa(desc->address), GDT_ENTRIES, 0);
}

/* For a single GDT entry which changes, we do the lazy thing: alter our GDT,
 * then tell the Host to reload the entire thing.  This operation is so rare
 * that this naive implementation is reasonable. */
static void lguest_write_gdt_entry(struct desc_struct *dt,
				   int entrynum, u32 low, u32 high)
{
	write_dt_entry(dt, entrynum, low, high);
	hcall(LHCALL_LOAD_GDT, __pa(dt), GDT_ENTRIES, 0);
}

/* OK, I lied.  There are three "thread local storage" GDT entries which change
 * on every context switch (these three entries are how glibc implements
 * __thread variables).  So we have a hypercall specifically for this case. */
static void lguest_load_tls(struct thread_struct *t, unsigned int cpu)
{
	/* There's one problem which normal hardware doesn't have: the Host
	 * can't handle us removing entries we're currently using.  So we clear
	 * the GS register here: if it's needed it'll be reloaded anyway. */
	loadsegment(gs, 0);
	lazy_hcall(LHCALL_LOAD_TLS, __pa(&t->tls_array), cpu, 0);
}

/*G:038 That's enough excitement for now, back to ploughing through each of
 * the different pv_ops structures (we're about 1/3 of the way through).
 *
 * This is the Local Descriptor Table, another weird Intel thingy.  Linux only
 * uses this for some strange applications like Wine.  We don't do anything
 * here, so they'll get an informative and friendly Segmentation Fault. */
static void lguest_set_ldt(const void *addr, unsigned entries)
{
}

/* This loads a GDT entry into the "Task Register": that entry points to a
 * structure called the Task State Segment.  Some comments scattered though the
 * kernel code indicate that this used for task switching in ages past, along
 * with blood sacrifice and astrology.
 *
 * Now there's nothing interesting in here that we don't get told elsewhere.
 * But the native version uses the "ltr" instruction, which makes the Host
 * complain to the Guest about a Segmentation Fault and it'll oops.  So we
 * override the native version with a do-nothing version. */
static void lguest_load_tr_desc(void)
{
}

/* The "cpuid" instruction is a way of querying both the CPU identity
 * (manufacturer, model, etc) and its features.  It was introduced before the
 * Pentium in 1993 and keeps getting extended by both Intel and AMD.  As you
 * might imagine, after a decade and a half this treatment, it is now a giant
 * ball of hair.  Its entry in the current Intel manual runs to 28 pages.
 *
 * This instruction even it has its own Wikipedia entry.  The Wikipedia entry
 * has been translated into 4 languages.  I am not making this up!
 *
 * We could get funky here and identify ourselves as "GenuineLguest", but
 * instead we just use the real "cpuid" instruction.  Then I pretty much turned
 * off feature bits until the Guest booted.  (Don't say that: you'll damage
 * lguest sales!)  Shut up, inner voice!  (Hey, just pointing out that this is
 * hardly future proof.)  Noone's listening!  They don't like you anyway,
 * parenthetic weirdo!
 *
 * Replacing the cpuid so we can turn features off is great for the kernel, but
 * anyone (including userspace) can just use the raw "cpuid" instruction and
 * the Host won't even notice since it isn't privileged.  So we try not to get
 * too worked up about it. */
static void lguest_cpuid(unsigned int *eax, unsigned int *ebx,
			 unsigned int *ecx, unsigned int *edx)
{
	int function = *eax;

	native_cpuid(eax, ebx, ecx, edx);
	switch (function) {
	case 1:	/* Basic feature request. */
		/* We only allow kernel to see SSE3, CMPXCHG16B and SSSE3 */
		*ecx &= 0x00002201;
		/* SSE, SSE2, FXSR, MMX, CMOV, CMPXCHG8B, FPU. */
		*edx &= 0x07808101;
		/* The Host can do a nice optimization if it knows that the
		 * kernel mappings (addresses above 0xC0000000 or whatever
		 * PAGE_OFFSET is set to) haven't changed.  But Linux calls
		 * flush_tlb_user() for both user and kernel mappings unless
		 * the Page Global Enable (PGE) feature bit is set. */
		*edx |= 0x00002000;
		break;
	case 0x80000000:
		/* Futureproof this a little: if they ask how much extended
		 * processor information there is, limit it to known fields. */
		if (*eax > 0x80000008)
			*eax = 0x80000008;
		break;
	}
}

/* Intel has four control registers, imaginatively named cr0, cr2, cr3 and cr4.
 * I assume there's a cr1, but it hasn't bothered us yet, so we'll not bother
 * it.  The Host needs to know when the Guest wants to change them, so we have
 * a whole series of functions like read_cr0() and write_cr0().
 *
 * We start with cr0.  cr0 allows you to turn on and off all kinds of basic
 * features, but Linux only really cares about one: the horrifically-named Task
 * Switched (TS) bit at bit 3 (ie. 8)
 *
 * What does the TS bit do?  Well, it causes the CPU to trap (interrupt 7) if
 * the floating point unit is used.  Which allows us to restore FPU state
 * lazily after a task switch, and Linux uses that gratefully, but wouldn't a
 * name like "FPUTRAP bit" be a little less cryptic?
 *
 * We store cr0 (and cr3) locally, because the Host never changes it.  The
 * Guest sometimes wants to read it and we'd prefer not to bother the Host
 * unnecessarily. */
static unsigned long current_cr0, current_cr3;
static void lguest_write_cr0(unsigned long val)
{
	lazy_hcall(LHCALL_TS, val & X86_CR0_TS, 0, 0);
	current_cr0 = val;
}

static unsigned long lguest_read_cr0(void)
{
	return current_cr0;
}

/* Intel provided a special instruction to clear the TS bit for people too cool
 * to use write_cr0() to do it.  This "clts" instruction is faster, because all
 * the vowels have been optimized out. */
static void lguest_clts(void)
{
	lazy_hcall(LHCALL_TS, 0, 0, 0);
	current_cr0 &= ~X86_CR0_TS;
}

/* cr2 is the virtual address of the last page fault, which the Guest only ever
 * reads.  The Host kindly writes this into our "struct lguest_data", so we
 * just read it out of there. */
static unsigned long lguest_read_cr2(void)
{
	return lguest_data.cr2;
}

/* cr3 is the current toplevel pagetable page: the principle is the same as
 * cr0.  Keep a local copy, and tell the Host when it changes. */
static void lguest_write_cr3(unsigned long cr3)
{
	lazy_hcall(LHCALL_NEW_PGTABLE, cr3, 0, 0);
	current_cr3 = cr3;
}

static unsigned long lguest_read_cr3(void)
{
	return current_cr3;
}

/* cr4 is used to enable and disable PGE, but we don't care. */
static unsigned long lguest_read_cr4(void)
{
	return 0;
}

static void lguest_write_cr4(unsigned long val)
{
}

/*
 * Page Table Handling.
 *
 * Now would be a good time to take a rest and grab a coffee or similarly
 * relaxing stimulant.  The easy parts are behind us, and the trek gradually
 * winds uphill from here.
 *
 * Quick refresher: memory is divided into "pages" of 4096 bytes each.  The CPU
 * maps virtual addresses to physical addresses using "page tables".  We could
 * use one huge index of 1 million entries: each address is 4 bytes, so that's
 * 1024 pages just to hold the page tables.   But since most virtual addresses
 * are unused, we use a two level index which saves space.  The cr3 register
 * contains the physical address of the top level "page directory" page, which
 * contains physical addresses of up to 1024 second-level pages.  Each of these
 * second level pages contains up to 1024 physical addresses of actual pages,
 * or Page Table Entries (PTEs).
 *
 * Here's a diagram, where arrows indicate physical addresses:
 *
 * cr3 ---> +---------+
 *	    |  	   --------->+---------+
 *	    |	      |	     | PADDR1  |
 *	  Top-level   |	     | PADDR2  |
 *	  (PMD) page  |	     | 	       |
 *	    |	      |	   Lower-level |
 *	    |	      |	   (PTE) page  |
 *	    |	      |	     |	       |
 *	      ....    	     	 ....
 *
 * So to convert a virtual address to a physical address, we look up the top
 * level, which points us to the second level, which gives us the physical
 * address of that page.  If the top level entry was not present, or the second
 * level entry was not present, then the virtual address is invalid (we
 * say "the page was not mapped").
 *
 * Put another way, a 32-bit virtual address is divided up like so:
 *
 *  1 1 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
 * |<---- 10 bits ---->|<---- 10 bits ---->|<------ 12 bits ------>|
 *    Index into top     Index into second      Offset within page
 *  page directory page    pagetable page
 *
 * The kernel spends a lot of time changing both the top-level page directory
 * and lower-level pagetable pages.  The Guest doesn't know physical addresses,
 * so while it maintains these page tables exactly like normal, it also needs
 * to keep the Host informed whenever it makes a change: the Host will create
 * the real page tables based on the Guests'.
 */

/* The Guest calls this to set a second-level entry (pte), ie. to map a page
 * into a process' address space.  We set the entry then tell the Host the
 * toplevel and address this corresponds to.  The Guest uses one pagetable per
 * process, so we need to tell the Host which one we're changing (mm->pgd). */
static void lguest_set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
	lazy_hcall(LHCALL_SET_PTE, __pa(mm->pgd), addr, pteval.pte_low);
}

/* The Guest calls this to set a top-level entry.  Again, we set the entry then
 * tell the Host which top-level page we changed, and the index of the entry we
 * changed. */
static void lguest_set_pmd(pmd_t *pmdp, pmd_t pmdval)
{
	*pmdp = pmdval;
	lazy_hcall(LHCALL_SET_PMD, __pa(pmdp)&PAGE_MASK,
		   (__pa(pmdp)&(PAGE_SIZE-1))/4, 0);
}

/* There are a couple of legacy places where the kernel sets a PTE, but we
 * don't know the top level any more.  This is useless for us, since we don't
 * know which pagetable is changing or what address, so we just tell the Host
 * to forget all of them.  Fortunately, this is very rare.
 *
 * ... except in early boot when the kernel sets up the initial pagetables,
 * which makes booting astonishingly slow.  So we don't even tell the Host
 * anything changed until we've done the first page table switch. */
static void lguest_set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
	/* Don't bother with hypercall before initial setup. */
	if (current_cr3)
		lazy_hcall(LHCALL_FLUSH_TLB, 1, 0, 0);
}

/* Unfortunately for Lguest, the pv_mmu_ops for page tables were based on
 * native page table operations.  On native hardware you can set a new page
 * table entry whenever you want, but if you want to remove one you have to do
 * a TLB flush (a TLB is a little cache of page table entries kept by the CPU).
 *
 * So the lguest_set_pte_at() and lguest_set_pmd() functions above are only
 * called when a valid entry is written, not when it's removed (ie. marked not
 * present).  Instead, this is where we come when the Guest wants to remove a
 * page table entry: we tell the Host to set that entry to 0 (ie. the present
 * bit is zero). */
static void lguest_flush_tlb_single(unsigned long addr)
{
	/* Simply set it to zero: if it was not, it will fault back in. */
	lazy_hcall(LHCALL_SET_PTE, current_cr3, addr, 0);
}

/* This is what happens after the Guest has removed a large number of entries.
 * This tells the Host that any of the page table entries for userspace might
 * have changed, ie. virtual addresses below PAGE_OFFSET. */
static void lguest_flush_tlb_user(void)
{
	lazy_hcall(LHCALL_FLUSH_TLB, 0, 0, 0);
}

/* This is called when the kernel page tables have changed.  That's not very
 * common (unless the Guest is using highmem, which makes the Guest extremely
 * slow), so it's worth separating this from the user flushing above. */
static void lguest_flush_tlb_kernel(void)
{
	lazy_hcall(LHCALL_FLUSH_TLB, 1, 0, 0);
}

/*
 * The Unadvanced Programmable Interrupt Controller.
 *
 * This is an attempt to implement the simplest possible interrupt controller.
 * I spent some time looking though routines like set_irq_chip_and_handler,
 * set_irq_chip_and_handler_name, set_irq_chip_data and set_phasers_to_stun and
 * I *think* this is as simple as it gets.
 *
 * We can tell the Host what interrupts we want blocked ready for using the
 * lguest_data.interrupts bitmap, so disabling (aka "masking") them is as
 * simple as setting a bit.  We don't actually "ack" interrupts as such, we
 * just mask and unmask them.  I wonder if we should be cleverer?
 */
static void disable_lguest_irq(unsigned int irq)
{
	set_bit(irq, lguest_data.blocked_interrupts);
}

static void enable_lguest_irq(unsigned int irq)
{
	clear_bit(irq, lguest_data.blocked_interrupts);
}

/* This structure describes the lguest IRQ controller. */
static struct irq_chip lguest_irq_controller = {
	.name		= "lguest",
	.mask		= disable_lguest_irq,
	.mask_ack	= disable_lguest_irq,
	.unmask		= enable_lguest_irq,
};

/* This sets up the Interrupt Descriptor Table (IDT) entry for each hardware
 * interrupt (except 128, which is used for system calls), and then tells the
 * Linux infrastructure that each interrupt is controlled by our level-based
 * lguest interrupt controller. */
static void __init lguest_init_IRQ(void)
{
	unsigned int i;

	for (i = 0; i < LGUEST_IRQS; i++) {
		int vector = FIRST_EXTERNAL_VECTOR + i;
		if (vector != SYSCALL_VECTOR) {
			set_intr_gate(vector, interrupt[i]);
			set_irq_chip_and_handler(i, &lguest_irq_controller,
						 handle_level_irq);
		}
	}
	/* This call is required to set up for 4k stacks, where we have
	 * separate stacks for hard and soft interrupts. */
	irq_ctx_init(smp_processor_id());
}

/*
 * Time.
 *
 * It would be far better for everyone if the Guest had its own clock, but
 * until then the Host gives us the time on every interrupt.
 */
static unsigned long lguest_get_wallclock(void)
{
	return lguest_data.time.tv_sec;
}

static cycle_t lguest_clock_read(void)
{
	unsigned long sec, nsec;

	/* If the Host tells the TSC speed, we can trust that. */
	if (lguest_data.tsc_khz)
		return native_read_tsc();

	/* If we can't use the TSC, we read the time value written by the Host.
	 * Since it's in two parts (seconds and nanoseconds), we risk reading
	 * it just as it's changing from 99 & 0.999999999 to 100 and 0, and
	 * getting 99 and 0.  As Linux tends to come apart under the stress of
	 * time travel, we must be careful: */
	do {
		/* First we read the seconds part. */
		sec = lguest_data.time.tv_sec;
		/* This read memory barrier tells the compiler and the CPU that
		 * this can't be reordered: we have to complete the above
		 * before going on. */
		rmb();
		/* Now we read the nanoseconds part. */
		nsec = lguest_data.time.tv_nsec;
		/* Make sure we've done that. */
		rmb();
		/* Now if the seconds part has changed, try again. */
	} while (unlikely(lguest_data.time.tv_sec != sec));

	/* Our non-TSC clock is in real nanoseconds. */
	return sec*1000000000ULL + nsec;
}

/* This is what we tell the kernel is our clocksource.  */
static struct clocksource lguest_clock = {
	.name		= "lguest",
	.rating		= 400,
	.read		= lguest_clock_read,
	.mask		= CLOCKSOURCE_MASK(64),
	.mult		= 1 << 22,
	.shift		= 22,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/* The "scheduler clock" is just our real clock, adjusted to start at zero */
static unsigned long long lguest_sched_clock(void)
{
	return cyc2ns(&lguest_clock, lguest_clock_read() - clock_base);
}

/* We also need a "struct clock_event_device": Linux asks us to set it to go
 * off some time in the future.  Actually, James Morris figured all this out, I
 * just applied the patch. */
static int lguest_clockevent_set_next_event(unsigned long delta,
                                           struct clock_event_device *evt)
{
	if (delta < LG_CLOCK_MIN_DELTA) {
		if (printk_ratelimit())
			printk(KERN_DEBUG "%s: small delta %lu ns\n",
			       __FUNCTION__, delta);
		return -ETIME;
	}
	hcall(LHCALL_SET_CLOCKEVENT, delta, 0, 0);
	return 0;
}

static void lguest_clockevent_set_mode(enum clock_event_mode mode,
                                      struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		/* A 0 argument shuts the clock down. */
		hcall(LHCALL_SET_CLOCKEVENT, 0, 0, 0);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* This is what we expect. */
		break;
	case CLOCK_EVT_MODE_PERIODIC:
		BUG();
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

/* This describes our primitive timer chip. */
static struct clock_event_device lguest_clockevent = {
	.name                   = "lguest",
	.features               = CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event         = lguest_clockevent_set_next_event,
	.set_mode               = lguest_clockevent_set_mode,
	.rating                 = INT_MAX,
	.mult                   = 1,
	.shift                  = 0,
	.min_delta_ns           = LG_CLOCK_MIN_DELTA,
	.max_delta_ns           = LG_CLOCK_MAX_DELTA,
};

/* This is the Guest timer interrupt handler (hardware interrupt 0).  We just
 * call the clockevent infrastructure and it does whatever needs doing. */
static void lguest_time_irq(unsigned int irq, struct irq_desc *desc)
{
	unsigned long flags;

	/* Don't interrupt us while this is running. */
	local_irq_save(flags);
	lguest_clockevent.event_handler(&lguest_clockevent);
	local_irq_restore(flags);
}

/* At some point in the boot process, we get asked to set up our timing
 * infrastructure.  The kernel doesn't expect timer interrupts before this, but
 * we cleverly initialized the "blocked_interrupts" field of "struct
 * lguest_data" so that timer interrupts were blocked until now. */
static void lguest_time_init(void)
{
	/* Set up the timer interrupt (0) to go to our simple timer routine */
	set_irq_handler(0, lguest_time_irq);

	/* Our clock structure looks like arch/x86/kernel/tsc_32.c if we can
	 * use the TSC, otherwise it's a dumb nanosecond-resolution clock.
	 * Either way, the "rating" is set so high that it's always chosen over
	 * any other clocksource. */
	if (lguest_data.tsc_khz)
		lguest_clock.mult = clocksource_khz2mult(lguest_data.tsc_khz,
							 lguest_clock.shift);
	clock_base = lguest_clock_read();
	clocksource_register(&lguest_clock);

	/* Now we've set up our clock, we can use it as the scheduler clock */
	pv_time_ops.sched_clock = lguest_sched_clock;

	/* We can't set cpumask in the initializer: damn C limitations!  Set it
	 * here and register our timer device. */
	lguest_clockevent.cpumask = cpumask_of_cpu(0);
	clockevents_register_device(&lguest_clockevent);

	/* Finally, we unblock the timer interrupt. */
	enable_lguest_irq(0);
}

/*
 * Miscellaneous bits and pieces.
 *
 * Here is an oddball collection of functions which the Guest needs for things
 * to work.  They're pretty simple.
 */

/* The Guest needs to tell the Host what stack it expects traps to use.  For
 * native hardware, this is part of the Task State Segment mentioned above in
 * lguest_load_tr_desc(), but to help hypervisors there's this special call.
 *
 * We tell the Host the segment we want to use (__KERNEL_DS is the kernel data
 * segment), the privilege level (we're privilege level 1, the Host is 0 and
 * will not tolerate us trying to use that), the stack pointer, and the number
 * of pages in the stack. */
static void lguest_load_esp0(struct tss_struct *tss,
				     struct thread_struct *thread)
{
	lazy_hcall(LHCALL_SET_STACK, __KERNEL_DS|0x1, thread->esp0,
		   THREAD_SIZE/PAGE_SIZE);
}

/* Let's just say, I wouldn't do debugging under a Guest. */
static void lguest_set_debugreg(int regno, unsigned long value)
{
	/* FIXME: Implement */
}

/* There are times when the kernel wants to make sure that no memory writes are
 * caught in the cache (that they've all reached real hardware devices).  This
 * doesn't matter for the Guest which has virtual hardware.
 *
 * On the Pentium 4 and above, cpuid() indicates that the Cache Line Flush
 * (clflush) instruction is available and the kernel uses that.  Otherwise, it
 * uses the older "Write Back and Invalidate Cache" (wbinvd) instruction.
 * Unlike clflush, wbinvd can only be run at privilege level 0.  So we can
 * ignore clflush, but replace wbinvd.
 */
static void lguest_wbinvd(void)
{
}

/* If the Guest expects to have an Advanced Programmable Interrupt Controller,
 * we play dumb by ignoring writes and returning 0 for reads.  So it's no
 * longer Programmable nor Controlling anything, and I don't think 8 lines of
 * code qualifies for Advanced.  It will also never interrupt anything.  It
 * does, however, allow us to get through the Linux boot code. */
#ifdef CONFIG_X86_LOCAL_APIC
static void lguest_apic_write(unsigned long reg, unsigned long v)
{
}

static unsigned long lguest_apic_read(unsigned long reg)
{
	return 0;
}
#endif

/* STOP!  Until an interrupt comes in. */
static void lguest_safe_halt(void)
{
	hcall(LHCALL_HALT, 0, 0, 0);
}

/* Perhaps CRASH isn't the best name for this hypercall, but we use it to get a
 * message out when we're crashing as well as elegant termination like powering
 * off.
 *
 * Note that the Host always prefers that the Guest speak in physical addresses
 * rather than virtual addresses, so we use __pa() here. */
static void lguest_power_off(void)
{
	hcall(LHCALL_CRASH, __pa("Power down"), 0, 0);
}

/*
 * Panicing.
 *
 * Don't.  But if you did, this is what happens.
 */
static int lguest_panic(struct notifier_block *nb, unsigned long l, void *p)
{
	hcall(LHCALL_CRASH, __pa(p), 0, 0);
	/* The hcall won't return, but to keep gcc happy, we're "done". */
	return NOTIFY_DONE;
}

static struct notifier_block paniced = {
	.notifier_call = lguest_panic
};

/* Setting up memory is fairly easy. */
static __init char *lguest_memory_setup(void)
{
	/* We do this here and not earlier because lockcheck barfs if we do it
	 * before start_kernel() */
	atomic_notifier_chain_register(&panic_notifier_list, &paniced);

	/* The Linux bootloader header contains an "e820" memory map: the
	 * Launcher populated the first entry with our memory limit. */
	add_memory_region(boot_params.e820_map[0].addr,
			  boot_params.e820_map[0].size,
			  boot_params.e820_map[0].type);

	/* This string is for the boot messages. */
	return "LGUEST";
}

/* We will eventually use the virtio console device to produce console output,
 * but before that is set up we use LHCALL_NOTIFY on normal memory to produce
 * console output. */
static __init int early_put_chars(u32 vtermno, const char *buf, int count)
{
	char scratch[17];
	unsigned int len = count;

	/* We use a nul-terminated string, so we have to make a copy.  Icky,
	 * huh? */
	if (len > sizeof(scratch) - 1)
		len = sizeof(scratch) - 1;
	scratch[len] = '\0';
	memcpy(scratch, buf, len);
	hcall(LHCALL_NOTIFY, __pa(scratch), 0, 0);

	/* This routine returns the number of bytes actually written. */
	return len;
}

/*G:050
 * Patching (Powerfully Placating Performance Pedants)
 *
 * We have already seen that pv_ops structures let us replace simple
 * native instructions with calls to the appropriate back end all throughout
 * the kernel.  This allows the same kernel to run as a Guest and as a native
 * kernel, but it's slow because of all the indirect branches.
 *
 * Remember that David Wheeler quote about "Any problem in computer science can
 * be solved with another layer of indirection"?  The rest of that quote is
 * "... But that usually will create another problem."  This is the first of
 * those problems.
 *
 * Our current solution is to allow the paravirt back end to optionally patch
 * over the indirect calls to replace them with something more efficient.  We
 * patch the four most commonly called functions: disable interrupts, enable
 * interrupts, restore interrupts and save interrupts.  We usually have 6 or 10
 * bytes to patch into: the Guest versions of these operations are small enough
 * that we can fit comfortably.
 *
 * First we need assembly templates of each of the patchable Guest operations,
 * and these are in lguest_asm.S. */

/*G:060 We construct a table from the assembler templates: */
static const struct lguest_insns
{
	const char *start, *end;
} lguest_insns[] = {
	[PARAVIRT_PATCH(pv_irq_ops.irq_disable)] = { lgstart_cli, lgend_cli },
	[PARAVIRT_PATCH(pv_irq_ops.irq_enable)] = { lgstart_sti, lgend_sti },
	[PARAVIRT_PATCH(pv_irq_ops.restore_fl)] = { lgstart_popf, lgend_popf },
	[PARAVIRT_PATCH(pv_irq_ops.save_fl)] = { lgstart_pushf, lgend_pushf },
};

/* Now our patch routine is fairly simple (based on the native one in
 * paravirt.c).  If we have a replacement, we copy it in and return how much of
 * the available space we used. */
static unsigned lguest_patch(u8 type, u16 clobber, void *ibuf,
			     unsigned long addr, unsigned len)
{
	unsigned int insn_len;

	/* Don't do anything special if we don't have a replacement */
	if (type >= ARRAY_SIZE(lguest_insns) || !lguest_insns[type].start)
		return paravirt_patch_default(type, clobber, ibuf, addr, len);

	insn_len = lguest_insns[type].end - lguest_insns[type].start;

	/* Similarly if we can't fit replacement (shouldn't happen, but let's
	 * be thorough). */
	if (len < insn_len)
		return paravirt_patch_default(type, clobber, ibuf, addr, len);

	/* Copy in our instructions. */
	memcpy(ibuf, lguest_insns[type].start, insn_len);
	return insn_len;
}

/*G:030 Once we get to lguest_init(), we know we're a Guest.  The pv_ops
 * structures in the kernel provide points for (almost) every routine we have
 * to override to avoid privileged instructions. */
__init void lguest_init(void)
{
	/* We're under lguest, paravirt is enabled, and we're running at
	 * privilege level 1, not 0 as normal. */
	pv_info.name = "lguest";
	pv_info.paravirt_enabled = 1;
	pv_info.kernel_rpl = 1;

	/* We set up all the lguest overrides for sensitive operations.  These
	 * are detailed with the operations themselves. */

	/* interrupt-related operations */
	pv_irq_ops.init_IRQ = lguest_init_IRQ;
	pv_irq_ops.save_fl = save_fl;
	pv_irq_ops.restore_fl = restore_fl;
	pv_irq_ops.irq_disable = irq_disable;
	pv_irq_ops.irq_enable = irq_enable;
	pv_irq_ops.safe_halt = lguest_safe_halt;

	/* init-time operations */
	pv_init_ops.memory_setup = lguest_memory_setup;
	pv_init_ops.patch = lguest_patch;

	/* Intercepts of various cpu instructions */
	pv_cpu_ops.load_gdt = lguest_load_gdt;
	pv_cpu_ops.cpuid = lguest_cpuid;
	pv_cpu_ops.load_idt = lguest_load_idt;
	pv_cpu_ops.iret = lguest_iret;
	pv_cpu_ops.load_esp0 = lguest_load_esp0;
	pv_cpu_ops.load_tr_desc = lguest_load_tr_desc;
	pv_cpu_ops.set_ldt = lguest_set_ldt;
	pv_cpu_ops.load_tls = lguest_load_tls;
	pv_cpu_ops.set_debugreg = lguest_set_debugreg;
	pv_cpu_ops.clts = lguest_clts;
	pv_cpu_ops.read_cr0 = lguest_read_cr0;
	pv_cpu_ops.write_cr0 = lguest_write_cr0;
	pv_cpu_ops.read_cr4 = lguest_read_cr4;
	pv_cpu_ops.write_cr4 = lguest_write_cr4;
	pv_cpu_ops.write_gdt_entry = lguest_write_gdt_entry;
	pv_cpu_ops.write_idt_entry = lguest_write_idt_entry;
	pv_cpu_ops.wbinvd = lguest_wbinvd;
	pv_cpu_ops.lazy_mode.enter = paravirt_enter_lazy_cpu;
	pv_cpu_ops.lazy_mode.leave = lguest_leave_lazy_mode;

	/* pagetable management */
	pv_mmu_ops.write_cr3 = lguest_write_cr3;
	pv_mmu_ops.flush_tlb_user = lguest_flush_tlb_user;
	pv_mmu_ops.flush_tlb_single = lguest_flush_tlb_single;
	pv_mmu_ops.flush_tlb_kernel = lguest_flush_tlb_kernel;
	pv_mmu_ops.set_pte = lguest_set_pte;
	pv_mmu_ops.set_pte_at = lguest_set_pte_at;
	pv_mmu_ops.set_pmd = lguest_set_pmd;
	pv_mmu_ops.read_cr2 = lguest_read_cr2;
	pv_mmu_ops.read_cr3 = lguest_read_cr3;
	pv_mmu_ops.lazy_mode.enter = paravirt_enter_lazy_mmu;
	pv_mmu_ops.lazy_mode.leave = lguest_leave_lazy_mode;

#ifdef CONFIG_X86_LOCAL_APIC
	/* apic read/write intercepts */
	pv_apic_ops.apic_write = lguest_apic_write;
	pv_apic_ops.apic_write_atomic = lguest_apic_write;
	pv_apic_ops.apic_read = lguest_apic_read;
#endif

	/* time operations */
	pv_time_ops.get_wallclock = lguest_get_wallclock;
	pv_time_ops.time_init = lguest_time_init;

	/* Now is a good time to look at the implementations of these functions
	 * before returning to the rest of lguest_init(). */

	/*G:070 Now we've seen all the paravirt_ops, we return to
	 * lguest_init() where the rest of the fairly chaotic boot setup
	 * occurs. */

	/* The native boot code sets up initial page tables immediately after
	 * the kernel itself, and sets init_pg_tables_end so they're not
	 * clobbered.  The Launcher places our initial pagetables somewhere at
	 * the top of our physical memory, so we don't need extra space: set
	 * init_pg_tables_end to the end of the kernel. */
	init_pg_tables_end = __pa(pg0);

	/* Load the %fs segment register (the per-cpu segment register) with
	 * the normal data segment to get through booting. */
	asm volatile ("mov %0, %%fs" : : "r" (__KERNEL_DS) : "memory");

	/* The Host uses the top of the Guest's virtual address space for the
	 * Host<->Guest Switcher, and it tells us how big that is in
	 * lguest_data.reserve_mem, set up on the LGUEST_INIT hypercall. */
	reserve_top_address(lguest_data.reserve_mem);

	/* If we don't initialize the lock dependency checker now, it crashes
	 * paravirt_disable_iospace. */
	lockdep_init();

	/* The IDE code spends about 3 seconds probing for disks: if we reserve
	 * all the I/O ports up front it can't get them and so doesn't probe.
	 * Other device drivers are similar (but less severe).  This cuts the
	 * kernel boot time on my machine from 4.1 seconds to 0.45 seconds. */
	paravirt_disable_iospace();

	/* This is messy CPU setup stuff which the native boot code does before
	 * start_kernel, so we have to do, too: */
	cpu_detect(&new_cpu_data);
	/* head.S usually sets up the first capability word, so do it here. */
	new_cpu_data.x86_capability[0] = cpuid_edx(1);

	/* Math is always hard! */
	new_cpu_data.hard_math = 1;

#ifdef CONFIG_X86_MCE
	mce_disabled = 1;
#endif
#ifdef CONFIG_ACPI
	acpi_disabled = 1;
	acpi_ht = 0;
#endif

	/* We set the perferred console to "hvc".  This is the "hypervisor
	 * virtual console" driver written by the PowerPC people, which we also
	 * adapted for lguest's use. */
	add_preferred_console("hvc", 0, NULL);

	/* Register our very early console. */
	virtio_cons_early_init(early_put_chars);

	/* Last of all, we set the power management poweroff hook to point to
	 * the Guest routine to power off. */
	pm_power_off = lguest_power_off;

	/* Now we're set up, call start_kernel() in init/main.c and we proceed
	 * to boot as normal.  It never returns. */
	start_kernel();
}
/*
 * This marks the end of stage II of our journey, The Guest.
 *
 * It is now time for us to explore the layer of virtual drivers and complete
 * our understanding of the Guest in "make Drivers".
 */
