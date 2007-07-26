/*P:400 This contains run_guest() which actually calls into the Host<->Guest
 * Switcher and analyzes the return, such as determining if the Guest wants the
 * Host to do something.  This file also contains useful helper routines, and a
 * couple of non-obvious setup and teardown pieces which were implemented after
 * days of debugging pain. :*/
#include <linux/module.h>
#include <linux/stringify.h>
#include <linux/stddef.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/cpu.h>
#include <linux/freezer.h>
#include <asm/paravirt.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/poll.h>
#include <asm/highmem.h>
#include <asm/asm-offsets.h>
#include <asm/i387.h>
#include "lg.h"

/* Found in switcher.S */
extern char start_switcher_text[], end_switcher_text[], switch_to_guest[];
extern unsigned long default_idt_entries[];

/* Every guest maps the core switcher code. */
#define SHARED_SWITCHER_PAGES \
	DIV_ROUND_UP(end_switcher_text - start_switcher_text, PAGE_SIZE)
/* Pages for switcher itself, then two pages per cpu */
#define TOTAL_SWITCHER_PAGES (SHARED_SWITCHER_PAGES + 2 * NR_CPUS)

/* We map at -4M for ease of mapping into the guest (one PTE page). */
#define SWITCHER_ADDR 0xFFC00000

static struct vm_struct *switcher_vma;
static struct page **switcher_page;

static int cpu_had_pge;
static struct {
	unsigned long offset;
	unsigned short segment;
} lguest_entry;

/* This One Big lock protects all inter-guest data structures. */
DEFINE_MUTEX(lguest_lock);
static DEFINE_PER_CPU(struct lguest *, last_guest);

/* FIXME: Make dynamic. */
#define MAX_LGUEST_GUESTS 16
struct lguest lguests[MAX_LGUEST_GUESTS];

/* Offset from where switcher.S was compiled to where we've copied it */
static unsigned long switcher_offset(void)
{
	return SWITCHER_ADDR - (unsigned long)start_switcher_text;
}

/* This cpu's struct lguest_pages. */
static struct lguest_pages *lguest_pages(unsigned int cpu)
{
	return &(((struct lguest_pages *)
		  (SWITCHER_ADDR + SHARED_SWITCHER_PAGES*PAGE_SIZE))[cpu]);
}

/*H:010 We need to set up the Switcher at a high virtual address.  Remember the
 * Switcher is a few hundred bytes of assembler code which actually changes the
 * CPU to run the Guest, and then changes back to the Host when a trap or
 * interrupt happens.
 *
 * The Switcher code must be at the same virtual address in the Guest as the
 * Host since it will be running as the switchover occurs.
 *
 * Trying to map memory at a particular address is an unusual thing to do, so
 * it's not a simple one-liner.  We also set up the per-cpu parts of the
 * Switcher here.
 */
static __init int map_switcher(void)
{
	int i, err;
	struct page **pagep;

	/*
	 * Map the Switcher in to high memory.
	 *
	 * It turns out that if we choose the address 0xFFC00000 (4MB under the
	 * top virtual address), it makes setting up the page tables really
	 * easy.
	 */

	/* We allocate an array of "struct page"s.  map_vm_area() wants the
	 * pages in this form, rather than just an array of pointers. */
	switcher_page = kmalloc(sizeof(switcher_page[0])*TOTAL_SWITCHER_PAGES,
				GFP_KERNEL);
	if (!switcher_page) {
		err = -ENOMEM;
		goto out;
	}

	/* Now we actually allocate the pages.  The Guest will see these pages,
	 * so we make sure they're zeroed. */
	for (i = 0; i < TOTAL_SWITCHER_PAGES; i++) {
		unsigned long addr = get_zeroed_page(GFP_KERNEL);
		if (!addr) {
			err = -ENOMEM;
			goto free_some_pages;
		}
		switcher_page[i] = virt_to_page(addr);
	}

	/* Now we reserve the "virtual memory area" we want: 0xFFC00000
	 * (SWITCHER_ADDR).  We might not get it in theory, but in practice
	 * it's worked so far. */
	switcher_vma = __get_vm_area(TOTAL_SWITCHER_PAGES * PAGE_SIZE,
				       VM_ALLOC, SWITCHER_ADDR, VMALLOC_END);
	if (!switcher_vma) {
		err = -ENOMEM;
		printk("lguest: could not map switcher pages high\n");
		goto free_pages;
	}

	/* This code actually sets up the pages we've allocated to appear at
	 * SWITCHER_ADDR.  map_vm_area() takes the vma we allocated above, the
	 * kind of pages we're mapping (kernel pages), and a pointer to our
	 * array of struct pages.  It increments that pointer, but we don't
	 * care. */
	pagep = switcher_page;
	err = map_vm_area(switcher_vma, PAGE_KERNEL, &pagep);
	if (err) {
		printk("lguest: map_vm_area failed: %i\n", err);
		goto free_vma;
	}

	/* Now the switcher is mapped at the right address, we can't fail!
	 * Copy in the compiled-in Switcher code (from switcher.S). */
	memcpy(switcher_vma->addr, start_switcher_text,
	       end_switcher_text - start_switcher_text);

	/* Most of the switcher.S doesn't care that it's been moved; on Intel,
	 * jumps are relative, and it doesn't access any references to external
	 * code or data.
	 *
	 * The only exception is the interrupt handlers in switcher.S: their
	 * addresses are placed in a table (default_idt_entries), so we need to
	 * update the table with the new addresses.  switcher_offset() is a
	 * convenience function which returns the distance between the builtin
	 * switcher code and the high-mapped copy we just made. */
	for (i = 0; i < IDT_ENTRIES; i++)
		default_idt_entries[i] += switcher_offset();

	/*
	 * Set up the Switcher's per-cpu areas.
	 *
	 * Each CPU gets two pages of its own within the high-mapped region
	 * (aka. "struct lguest_pages").  Much of this can be initialized now,
	 * but some depends on what Guest we are running (which is set up in
	 * copy_in_guest_info()).
	 */
	for_each_possible_cpu(i) {
		/* lguest_pages() returns this CPU's two pages. */
		struct lguest_pages *pages = lguest_pages(i);
		/* This is a convenience pointer to make the code fit one
		 * statement to a line. */
		struct lguest_ro_state *state = &pages->state;

		/* The Global Descriptor Table: the Host has a different one
		 * for each CPU.  We keep a descriptor for the GDT which says
		 * where it is and how big it is (the size is actually the last
		 * byte, not the size, hence the "-1"). */
		state->host_gdt_desc.size = GDT_SIZE-1;
		state->host_gdt_desc.address = (long)get_cpu_gdt_table(i);

		/* All CPUs on the Host use the same Interrupt Descriptor
		 * Table, so we just use store_idt(), which gets this CPU's IDT
		 * descriptor. */
		store_idt(&state->host_idt_desc);

		/* The descriptors for the Guest's GDT and IDT can be filled
		 * out now, too.  We copy the GDT & IDT into ->guest_gdt and
		 * ->guest_idt before actually running the Guest. */
		state->guest_idt_desc.size = sizeof(state->guest_idt)-1;
		state->guest_idt_desc.address = (long)&state->guest_idt;
		state->guest_gdt_desc.size = sizeof(state->guest_gdt)-1;
		state->guest_gdt_desc.address = (long)&state->guest_gdt;

		/* We know where we want the stack to be when the Guest enters
		 * the switcher: in pages->regs.  The stack grows upwards, so
		 * we start it at the end of that structure. */
		state->guest_tss.esp0 = (long)(&pages->regs + 1);
		/* And this is the GDT entry to use for the stack: we keep a
		 * couple of special LGUEST entries. */
		state->guest_tss.ss0 = LGUEST_DS;

		/* x86 can have a finegrained bitmap which indicates what I/O
		 * ports the process can use.  We set it to the end of our
		 * structure, meaning "none". */
		state->guest_tss.io_bitmap_base = sizeof(state->guest_tss);

		/* Some GDT entries are the same across all Guests, so we can
		 * set them up now. */
		setup_default_gdt_entries(state);
		/* Most IDT entries are the same for all Guests, too.*/
		setup_default_idt_entries(state, default_idt_entries);

		/* The Host needs to be able to use the LGUEST segments on this
		 * CPU, too, so put them in the Host GDT. */
		get_cpu_gdt_table(i)[GDT_ENTRY_LGUEST_CS] = FULL_EXEC_SEGMENT;
		get_cpu_gdt_table(i)[GDT_ENTRY_LGUEST_DS] = FULL_SEGMENT;
	}

	/* In the Switcher, we want the %cs segment register to use the
	 * LGUEST_CS GDT entry: we've put that in the Host and Guest GDTs, so
	 * it will be undisturbed when we switch.  To change %cs and jump we
	 * need this structure to feed to Intel's "lcall" instruction. */
	lguest_entry.offset = (long)switch_to_guest + switcher_offset();
	lguest_entry.segment = LGUEST_CS;

	printk(KERN_INFO "lguest: mapped switcher at %p\n",
	       switcher_vma->addr);
	/* And we succeeded... */
	return 0;

free_vma:
	vunmap(switcher_vma->addr);
free_pages:
	i = TOTAL_SWITCHER_PAGES;
free_some_pages:
	for (--i; i >= 0; i--)
		__free_pages(switcher_page[i], 0);
	kfree(switcher_page);
out:
	return err;
}
/*:*/

/* Cleaning up the mapping when the module is unloaded is almost...
 * too easy. */
static void unmap_switcher(void)
{
	unsigned int i;

	/* vunmap() undoes *both* map_vm_area() and __get_vm_area(). */
	vunmap(switcher_vma->addr);
	/* Now we just need to free the pages we copied the switcher into */
	for (i = 0; i < TOTAL_SWITCHER_PAGES; i++)
		__free_pages(switcher_page[i], 0);
}

/*H:130 Our Guest is usually so well behaved; it never tries to do things it
 * isn't allowed to.  Unfortunately, "struct paravirt_ops" isn't quite
 * complete, because it doesn't contain replacements for the Intel I/O
 * instructions.  As a result, the Guest sometimes fumbles across one during
 * the boot process as it probes for various things which are usually attached
 * to a PC.
 *
 * When the Guest uses one of these instructions, we get trap #13 (General
 * Protection Fault) and come here.  We see if it's one of those troublesome
 * instructions and skip over it.  We return true if we did. */
static int emulate_insn(struct lguest *lg)
{
	u8 insn;
	unsigned int insnlen = 0, in = 0, shift = 0;
	/* The eip contains the *virtual* address of the Guest's instruction:
	 * guest_pa just subtracts the Guest's page_offset. */
	unsigned long physaddr = guest_pa(lg, lg->regs->eip);

	/* The guest_pa() function only works for Guest kernel addresses, but
	 * that's all we're trying to do anyway. */
	if (lg->regs->eip < lg->page_offset)
		return 0;

	/* Decoding x86 instructions is icky. */
	lgread(lg, &insn, physaddr, 1);

	/* 0x66 is an "operand prefix".  It means it's using the upper 16 bits
	   of the eax register. */
	if (insn == 0x66) {
		shift = 16;
		/* The instruction is 1 byte so far, read the next byte. */
		insnlen = 1;
		lgread(lg, &insn, physaddr + insnlen, 1);
	}

	/* We can ignore the lower bit for the moment and decode the 4 opcodes
	 * we need to emulate. */
	switch (insn & 0xFE) {
	case 0xE4: /* in     <next byte>,%al */
		insnlen += 2;
		in = 1;
		break;
	case 0xEC: /* in     (%dx),%al */
		insnlen += 1;
		in = 1;
		break;
	case 0xE6: /* out    %al,<next byte> */
		insnlen += 2;
		break;
	case 0xEE: /* out    %al,(%dx) */
		insnlen += 1;
		break;
	default:
		/* OK, we don't know what this is, can't emulate. */
		return 0;
	}

	/* If it was an "IN" instruction, they expect the result to be read
	 * into %eax, so we change %eax.  We always return all-ones, which
	 * traditionally means "there's nothing there". */
	if (in) {
		/* Lower bit tells is whether it's a 16 or 32 bit access */
		if (insn & 0x1)
			lg->regs->eax = 0xFFFFFFFF;
		else
			lg->regs->eax |= (0xFFFF << shift);
	}
	/* Finally, we've "done" the instruction, so move past it. */
	lg->regs->eip += insnlen;
	/* Success! */
	return 1;
}
/*:*/

/*L:305
 * Dealing With Guest Memory.
 *
 * When the Guest gives us (what it thinks is) a physical address, we can use
 * the normal copy_from_user() & copy_to_user() on that address: remember,
 * Guest physical == Launcher virtual.
 *
 * But we can't trust the Guest: it might be trying to access the Launcher
 * code.  We have to check that the range is below the pfn_limit the Launcher
 * gave us.  We have to make sure that addr + len doesn't give us a false
 * positive by overflowing, too. */
int lguest_address_ok(const struct lguest *lg,
		      unsigned long addr, unsigned long len)
{
	return (addr+len) / PAGE_SIZE < lg->pfn_limit && (addr+len >= addr);
}

/* This is a convenient routine to get a 32-bit value from the Guest (a very
 * common operation).  Here we can see how useful the kill_lguest() routine we
 * met in the Launcher can be: we return a random value (0) instead of needing
 * to return an error. */
u32 lgread_u32(struct lguest *lg, unsigned long addr)
{
	u32 val = 0;

	/* Don't let them access lguest binary. */
	if (!lguest_address_ok(lg, addr, sizeof(val))
	    || get_user(val, (u32 __user *)addr) != 0)
		kill_guest(lg, "bad read address %#lx", addr);
	return val;
}

/* Same thing for writing a value. */
void lgwrite_u32(struct lguest *lg, unsigned long addr, u32 val)
{
	if (!lguest_address_ok(lg, addr, sizeof(val))
	    || put_user(val, (u32 __user *)addr) != 0)
		kill_guest(lg, "bad write address %#lx", addr);
}

/* This routine is more generic, and copies a range of Guest bytes into a
 * buffer.  If the copy_from_user() fails, we fill the buffer with zeroes, so
 * the caller doesn't end up using uninitialized kernel memory. */
void lgread(struct lguest *lg, void *b, unsigned long addr, unsigned bytes)
{
	if (!lguest_address_ok(lg, addr, bytes)
	    || copy_from_user(b, (void __user *)addr, bytes) != 0) {
		/* copy_from_user should do this, but as we rely on it... */
		memset(b, 0, bytes);
		kill_guest(lg, "bad read address %#lx len %u", addr, bytes);
	}
}

/* Similarly, our generic routine to copy into a range of Guest bytes. */
void lgwrite(struct lguest *lg, unsigned long addr, const void *b,
	     unsigned bytes)
{
	if (!lguest_address_ok(lg, addr, bytes)
	    || copy_to_user((void __user *)addr, b, bytes) != 0)
		kill_guest(lg, "bad write address %#lx len %u", addr, bytes);
}
/* (end of memory access helper routines) :*/

static void set_ts(void)
{
	u32 cr0;

	cr0 = read_cr0();
	if (!(cr0 & 8))
		write_cr0(cr0|8);
}

/*S:010
 * We are getting close to the Switcher.
 *
 * Remember that each CPU has two pages which are visible to the Guest when it
 * runs on that CPU.  This has to contain the state for that Guest: we copy the
 * state in just before we run the Guest.
 *
 * Each Guest has "changed" flags which indicate what has changed in the Guest
 * since it last ran.  We saw this set in interrupts_and_traps.c and
 * segments.c.
 */
static void copy_in_guest_info(struct lguest *lg, struct lguest_pages *pages)
{
	/* Copying all this data can be quite expensive.  We usually run the
	 * same Guest we ran last time (and that Guest hasn't run anywhere else
	 * meanwhile).  If that's not the case, we pretend everything in the
	 * Guest has changed. */
	if (__get_cpu_var(last_guest) != lg || lg->last_pages != pages) {
		__get_cpu_var(last_guest) = lg;
		lg->last_pages = pages;
		lg->changed = CHANGED_ALL;
	}

	/* These copies are pretty cheap, so we do them unconditionally: */
	/* Save the current Host top-level page directory. */
	pages->state.host_cr3 = __pa(current->mm->pgd);
	/* Set up the Guest's page tables to see this CPU's pages (and no
	 * other CPU's pages). */
	map_switcher_in_guest(lg, pages);
	/* Set up the two "TSS" members which tell the CPU what stack to use
	 * for traps which do directly into the Guest (ie. traps at privilege
	 * level 1). */
	pages->state.guest_tss.esp1 = lg->esp1;
	pages->state.guest_tss.ss1 = lg->ss1;

	/* Copy direct-to-Guest trap entries. */
	if (lg->changed & CHANGED_IDT)
		copy_traps(lg, pages->state.guest_idt, default_idt_entries);

	/* Copy all GDT entries which the Guest can change. */
	if (lg->changed & CHANGED_GDT)
		copy_gdt(lg, pages->state.guest_gdt);
	/* If only the TLS entries have changed, copy them. */
	else if (lg->changed & CHANGED_GDT_TLS)
		copy_gdt_tls(lg, pages->state.guest_gdt);

	/* Mark the Guest as unchanged for next time. */
	lg->changed = 0;
}

/* Finally: the code to actually call into the Switcher to run the Guest. */
static void run_guest_once(struct lguest *lg, struct lguest_pages *pages)
{
	/* This is a dummy value we need for GCC's sake. */
	unsigned int clobber;

	/* Copy the guest-specific information into this CPU's "struct
	 * lguest_pages". */
	copy_in_guest_info(lg, pages);

	/* Now: we push the "eflags" register on the stack, then do an "lcall".
	 * This is how we change from using the kernel code segment to using
	 * the dedicated lguest code segment, as well as jumping into the
	 * Switcher.
	 *
	 * The lcall also pushes the old code segment (KERNEL_CS) onto the
	 * stack, then the address of this call.  This stack layout happens to
	 * exactly match the stack of an interrupt... */
	asm volatile("pushf; lcall *lguest_entry"
		     /* This is how we tell GCC that %eax ("a") and %ebx ("b")
		      * are changed by this routine.  The "=" means output. */
		     : "=a"(clobber), "=b"(clobber)
		     /* %eax contains the pages pointer.  ("0" refers to the
		      * 0-th argument above, ie "a").  %ebx contains the
		      * physical address of the Guest's top-level page
		      * directory. */
		     : "0"(pages), "1"(__pa(lg->pgdirs[lg->pgdidx].pgdir))
		     /* We tell gcc that all these registers could change,
		      * which means we don't have to save and restore them in
		      * the Switcher. */
		     : "memory", "%edx", "%ecx", "%edi", "%esi");
}
/*:*/

/*H:030 Let's jump straight to the the main loop which runs the Guest.
 * Remember, this is called by the Launcher reading /dev/lguest, and we keep
 * going around and around until something interesting happens. */
int run_guest(struct lguest *lg, unsigned long __user *user)
{
	/* We stop running once the Guest is dead. */
	while (!lg->dead) {
		/* We need to initialize this, otherwise gcc complains.  It's
		 * not (yet) clever enough to see that it's initialized when we
		 * need it. */
		unsigned int cr2 = 0; /* Damn gcc */

		/* First we run any hypercalls the Guest wants done: either in
		 * the hypercall ring in "struct lguest_data", or directly by
		 * using int 31 (LGUEST_TRAP_ENTRY). */
		do_hypercalls(lg);
		/* It's possible the Guest did a SEND_DMA hypercall to the
		 * Launcher, in which case we return from the read() now. */
		if (lg->dma_is_pending) {
			if (put_user(lg->pending_dma, user) ||
			    put_user(lg->pending_key, user+1))
				return -EFAULT;
			return sizeof(unsigned long)*2;
		}

		/* Check for signals */
		if (signal_pending(current))
			return -ERESTARTSYS;

		/* If Waker set break_out, return to Launcher. */
		if (lg->break_out)
			return -EAGAIN;

		/* Check if there are any interrupts which can be delivered
		 * now: if so, this sets up the hander to be executed when we
		 * next run the Guest. */
		maybe_do_interrupt(lg);

		/* All long-lived kernel loops need to check with this horrible
		 * thing called the freezer.  If the Host is trying to suspend,
		 * it stops us. */
		try_to_freeze();

		/* Just make absolutely sure the Guest is still alive.  One of
		 * those hypercalls could have been fatal, for example. */
		if (lg->dead)
			break;

		/* If the Guest asked to be stopped, we sleep.  The Guest's
		 * clock timer or LHCALL_BREAK from the Waker will wake us. */
		if (lg->halted) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			continue;
		}

		/* OK, now we're ready to jump into the Guest.  First we put up
		 * the "Do Not Disturb" sign: */
		local_irq_disable();

		/* Remember the awfully-named TS bit?  If the Guest has asked
		 * to set it we set it now, so we can trap and pass that trap
		 * to the Guest if it uses the FPU. */
		if (lg->ts)
			set_ts();

		/* SYSENTER is an optimized way of doing system calls.  We
		 * can't allow it because it always jumps to privilege level 0.
		 * A normal Guest won't try it because we don't advertise it in
		 * CPUID, but a malicious Guest (or malicious Guest userspace
		 * program) could, so we tell the CPU to disable it before
		 * running the Guest. */
		if (boot_cpu_has(X86_FEATURE_SEP))
			wrmsr(MSR_IA32_SYSENTER_CS, 0, 0);

		/* Now we actually run the Guest.  It will pop back out when
		 * something interesting happens, and we can examine its
		 * registers to see what it was doing. */
		run_guest_once(lg, lguest_pages(raw_smp_processor_id()));

		/* The "regs" pointer contains two extra entries which are not
		 * really registers: a trap number which says what interrupt or
		 * trap made the switcher code come back, and an error code
		 * which some traps set.  */

		/* If the Guest page faulted, then the cr2 register will tell
		 * us the bad virtual address.  We have to grab this now,
		 * because once we re-enable interrupts an interrupt could
		 * fault and thus overwrite cr2, or we could even move off to a
		 * different CPU. */
		if (lg->regs->trapnum == 14)
			cr2 = read_cr2();
		/* Similarly, if we took a trap because the Guest used the FPU,
		 * we have to restore the FPU it expects to see. */
		else if (lg->regs->trapnum == 7)
			math_state_restore();

		/* Restore SYSENTER if it's supposed to be on. */
		if (boot_cpu_has(X86_FEATURE_SEP))
			wrmsr(MSR_IA32_SYSENTER_CS, __KERNEL_CS, 0);

		/* Now we're ready to be interrupted or moved to other CPUs */
		local_irq_enable();

		/* OK, so what happened? */
		switch (lg->regs->trapnum) {
		case 13: /* We've intercepted a GPF. */
			/* Check if this was one of those annoying IN or OUT
			 * instructions which we need to emulate.  If so, we
			 * just go back into the Guest after we've done it. */
			if (lg->regs->errcode == 0) {
				if (emulate_insn(lg))
					continue;
			}
			break;
		case 14: /* We've intercepted a page fault. */
			/* The Guest accessed a virtual address that wasn't
			 * mapped.  This happens a lot: we don't actually set
			 * up most of the page tables for the Guest at all when
			 * we start: as it runs it asks for more and more, and
			 * we set them up as required. In this case, we don't
			 * even tell the Guest that the fault happened.
			 *
			 * The errcode tells whether this was a read or a
			 * write, and whether kernel or userspace code. */
			if (demand_page(lg, cr2, lg->regs->errcode))
				continue;

			/* OK, it's really not there (or not OK): the Guest
			 * needs to know.  We write out the cr2 value so it
			 * knows where the fault occurred.
			 *
			 * Note that if the Guest were really messed up, this
			 * could happen before it's done the INITIALIZE
			 * hypercall, so lg->lguest_data will be NULL, so
			 * &lg->lguest_data->cr2 will be address 8.  Writing
			 * into that address won't hurt the Host at all,
			 * though. */
			if (put_user(cr2, &lg->lguest_data->cr2))
				kill_guest(lg, "Writing cr2");
			break;
		case 7: /* We've intercepted a Device Not Available fault. */
			/* If the Guest doesn't want to know, we already
			 * restored the Floating Point Unit, so we just
			 * continue without telling it. */
			if (!lg->ts)
				continue;
			break;
		case 32 ... 255:
			/* These values mean a real interrupt occurred, in
			 * which case the Host handler has already been run.
			 * We just do a friendly check if another process
			 * should now be run, then fall through to loop
			 * around: */
			cond_resched();
		case LGUEST_TRAP_ENTRY: /* Handled at top of loop */
			continue;
		}

		/* If we get here, it's a trap the Guest wants to know
		 * about. */
		if (deliver_trap(lg, lg->regs->trapnum))
			continue;

		/* If the Guest doesn't have a handler (either it hasn't
		 * registered any yet, or it's one of the faults we don't let
		 * it handle), it dies with a cryptic error message. */
		kill_guest(lg, "unhandled trap %li at %#lx (%#lx)",
			   lg->regs->trapnum, lg->regs->eip,
			   lg->regs->trapnum == 14 ? cr2 : lg->regs->errcode);
	}
	/* The Guest is dead => "No such file or directory" */
	return -ENOENT;
}

/* Now we can look at each of the routines this calls, in increasing order of
 * complexity: do_hypercalls(), emulate_insn(), maybe_do_interrupt(),
 * deliver_trap() and demand_page().  After all those, we'll be ready to
 * examine the Switcher, and our philosophical understanding of the Host/Guest
 * duality will be complete. :*/

int find_free_guest(void)
{
	unsigned int i;
	for (i = 0; i < MAX_LGUEST_GUESTS; i++)
		if (!lguests[i].tsk)
			return i;
	return -1;
}

static void adjust_pge(void *on)
{
	if (on)
		write_cr4(read_cr4() | X86_CR4_PGE);
	else
		write_cr4(read_cr4() & ~X86_CR4_PGE);
}

/*H:000
 * Welcome to the Host!
 *
 * By this point your brain has been tickled by the Guest code and numbed by
 * the Launcher code; prepare for it to be stretched by the Host code.  This is
 * the heart.  Let's begin at the initialization routine for the Host's lg
 * module.
 */
static int __init init(void)
{
	int err;

	/* Lguest can't run under Xen, VMI or itself.  It does Tricky Stuff. */
	if (paravirt_enabled()) {
		printk("lguest is afraid of %s\n", paravirt_ops.name);
		return -EPERM;
	}

	/* First we put the Switcher up in very high virtual memory. */
	err = map_switcher();
	if (err)
		return err;

	/* Now we set up the pagetable implementation for the Guests. */
	err = init_pagetables(switcher_page, SHARED_SWITCHER_PAGES);
	if (err) {
		unmap_switcher();
		return err;
	}

	/* The I/O subsystem needs some things initialized. */
	lguest_io_init();

	/* /dev/lguest needs to be registered. */
	err = lguest_device_init();
	if (err) {
		free_pagetables();
		unmap_switcher();
		return err;
	}

	/* Finally, we need to turn off "Page Global Enable".  PGE is an
	 * optimization where page table entries are specially marked to show
	 * they never change.  The Host kernel marks all the kernel pages this
	 * way because it's always present, even when userspace is running.
	 *
	 * Lguest breaks this: unbeknownst to the rest of the Host kernel, we
	 * switch to the Guest kernel.  If you don't disable this on all CPUs,
	 * you'll get really weird bugs that you'll chase for two days.
	 *
	 * I used to turn PGE off every time we switched to the Guest and back
	 * on when we return, but that slowed the Switcher down noticibly. */

	/* We don't need the complexity of CPUs coming and going while we're
	 * doing this. */
	lock_cpu_hotplug();
	if (cpu_has_pge) { /* We have a broader idea of "global". */
		/* Remember that this was originally set (for cleanup). */
		cpu_had_pge = 1;
		/* adjust_pge is a helper function which sets or unsets the PGE
		 * bit on its CPU, depending on the argument (0 == unset). */
		on_each_cpu(adjust_pge, (void *)0, 0, 1);
		/* Turn off the feature in the global feature set. */
		clear_bit(X86_FEATURE_PGE, boot_cpu_data.x86_capability);
	}
	unlock_cpu_hotplug();

	/* All good! */
	return 0;
}

/* Cleaning up is just the same code, backwards.  With a little French. */
static void __exit fini(void)
{
	lguest_device_remove();
	free_pagetables();
	unmap_switcher();

	/* If we had PGE before we started, turn it back on now. */
	lock_cpu_hotplug();
	if (cpu_had_pge) {
		set_bit(X86_FEATURE_PGE, boot_cpu_data.x86_capability);
		/* adjust_pge's argument "1" means set PGE. */
		on_each_cpu(adjust_pge, (void *)1, 0, 1);
	}
	unlock_cpu_hotplug();
}

/* The Host side of lguest can be a module.  This is a nice way for people to
 * play with it.  */
module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rusty Russell <rusty@rustcorp.com.au>");
