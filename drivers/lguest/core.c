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

static __init int map_switcher(void)
{
	int i, err;
	struct page **pagep;

	switcher_page = kmalloc(sizeof(switcher_page[0])*TOTAL_SWITCHER_PAGES,
				GFP_KERNEL);
	if (!switcher_page) {
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < TOTAL_SWITCHER_PAGES; i++) {
		unsigned long addr = get_zeroed_page(GFP_KERNEL);
		if (!addr) {
			err = -ENOMEM;
			goto free_some_pages;
		}
		switcher_page[i] = virt_to_page(addr);
	}

	switcher_vma = __get_vm_area(TOTAL_SWITCHER_PAGES * PAGE_SIZE,
				       VM_ALLOC, SWITCHER_ADDR, VMALLOC_END);
	if (!switcher_vma) {
		err = -ENOMEM;
		printk("lguest: could not map switcher pages high\n");
		goto free_pages;
	}

	pagep = switcher_page;
	err = map_vm_area(switcher_vma, PAGE_KERNEL, &pagep);
	if (err) {
		printk("lguest: map_vm_area failed: %i\n", err);
		goto free_vma;
	}
	memcpy(switcher_vma->addr, start_switcher_text,
	       end_switcher_text - start_switcher_text);

	/* Fix up IDT entries to point into copied text. */
	for (i = 0; i < IDT_ENTRIES; i++)
		default_idt_entries[i] += switcher_offset();

	for_each_possible_cpu(i) {
		struct lguest_pages *pages = lguest_pages(i);
		struct lguest_ro_state *state = &pages->state;

		/* These fields are static: rest done in copy_in_guest_info */
		state->host_gdt_desc.size = GDT_SIZE-1;
		state->host_gdt_desc.address = (long)get_cpu_gdt_table(i);
		store_idt(&state->host_idt_desc);
		state->guest_idt_desc.size = sizeof(state->guest_idt)-1;
		state->guest_idt_desc.address = (long)&state->guest_idt;
		state->guest_gdt_desc.size = sizeof(state->guest_gdt)-1;
		state->guest_gdt_desc.address = (long)&state->guest_gdt;
		state->guest_tss.esp0 = (long)(&pages->regs + 1);
		state->guest_tss.ss0 = LGUEST_DS;
		/* No I/O for you! */
		state->guest_tss.io_bitmap_base = sizeof(state->guest_tss);
		setup_default_gdt_entries(state);
		setup_default_idt_entries(state, default_idt_entries);

		/* Setup LGUEST segments on all cpus */
		get_cpu_gdt_table(i)[GDT_ENTRY_LGUEST_CS] = FULL_EXEC_SEGMENT;
		get_cpu_gdt_table(i)[GDT_ENTRY_LGUEST_DS] = FULL_SEGMENT;
	}

	/* Initialize entry point into switcher. */
	lguest_entry.offset = (long)switch_to_guest + switcher_offset();
	lguest_entry.segment = LGUEST_CS;

	printk(KERN_INFO "lguest: mapped switcher at %p\n",
	       switcher_vma->addr);
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

static void unmap_switcher(void)
{
	unsigned int i;

	vunmap(switcher_vma->addr);
	for (i = 0; i < TOTAL_SWITCHER_PAGES; i++)
		__free_pages(switcher_page[i], 0);
}

/* IN/OUT insns: enough to get us past boot-time probing. */
static int emulate_insn(struct lguest *lg)
{
	u8 insn;
	unsigned int insnlen = 0, in = 0, shift = 0;
	unsigned long physaddr = guest_pa(lg, lg->regs->eip);

	/* This only works for addresses in linear mapping... */
	if (lg->regs->eip < lg->page_offset)
		return 0;
	lgread(lg, &insn, physaddr, 1);

	/* Operand size prefix means it's actually for ax. */
	if (insn == 0x66) {
		shift = 16;
		insnlen = 1;
		lgread(lg, &insn, physaddr + insnlen, 1);
	}

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
		return 0;
	}

	if (in) {
		/* Lower bit tells is whether it's a 16 or 32 bit access */
		if (insn & 0x1)
			lg->regs->eax = 0xFFFFFFFF;
		else
			lg->regs->eax |= (0xFFFF << shift);
	}
	lg->regs->eip += insnlen;
	return 1;
}

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

static void copy_in_guest_info(struct lguest *lg, struct lguest_pages *pages)
{
	if (__get_cpu_var(last_guest) != lg || lg->last_pages != pages) {
		__get_cpu_var(last_guest) = lg;
		lg->last_pages = pages;
		lg->changed = CHANGED_ALL;
	}

	/* These are pretty cheap, so we do them unconditionally. */
	pages->state.host_cr3 = __pa(current->mm->pgd);
	map_switcher_in_guest(lg, pages);
	pages->state.guest_tss.esp1 = lg->esp1;
	pages->state.guest_tss.ss1 = lg->ss1;

	/* Copy direct trap entries. */
	if (lg->changed & CHANGED_IDT)
		copy_traps(lg, pages->state.guest_idt, default_idt_entries);

	/* Copy all GDT entries but the TSS. */
	if (lg->changed & CHANGED_GDT)
		copy_gdt(lg, pages->state.guest_gdt);
	/* If only the TLS entries have changed, copy them. */
	else if (lg->changed & CHANGED_GDT_TLS)
		copy_gdt_tls(lg, pages->state.guest_gdt);

	lg->changed = 0;
}

static void run_guest_once(struct lguest *lg, struct lguest_pages *pages)
{
	unsigned int clobber;

	copy_in_guest_info(lg, pages);

	/* Put eflags on stack, lcall does rest: suitable for iret return. */
	asm volatile("pushf; lcall *lguest_entry"
		     : "=a"(clobber), "=b"(clobber)
		     : "0"(pages), "1"(__pa(lg->pgdirs[lg->pgdidx].pgdir))
		     : "memory", "%edx", "%ecx", "%edi", "%esi");
}

int run_guest(struct lguest *lg, unsigned long __user *user)
{
	while (!lg->dead) {
		unsigned int cr2 = 0; /* Damn gcc */

		/* Hypercalls first: we might have been out to userspace */
		do_hypercalls(lg);
		if (lg->dma_is_pending) {
			if (put_user(lg->pending_dma, user) ||
			    put_user(lg->pending_key, user+1))
				return -EFAULT;
			return sizeof(unsigned long)*2;
		}

		if (signal_pending(current))
			return -ERESTARTSYS;

		/* If Waker set break_out, return to Launcher. */
		if (lg->break_out)
			return -EAGAIN;

		maybe_do_interrupt(lg);

		try_to_freeze();

		if (lg->dead)
			break;

		if (lg->halted) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			continue;
		}

		local_irq_disable();

		/* Even if *we* don't want FPU trap, guest might... */
		if (lg->ts)
			set_ts();

		/* Don't let Guest do SYSENTER: we can't handle it. */
		if (boot_cpu_has(X86_FEATURE_SEP))
			wrmsr(MSR_IA32_SYSENTER_CS, 0, 0);

		run_guest_once(lg, lguest_pages(raw_smp_processor_id()));

		/* Save cr2 now if we page-faulted. */
		if (lg->regs->trapnum == 14)
			cr2 = read_cr2();
		else if (lg->regs->trapnum == 7)
			math_state_restore();

		if (boot_cpu_has(X86_FEATURE_SEP))
			wrmsr(MSR_IA32_SYSENTER_CS, __KERNEL_CS, 0);
		local_irq_enable();

		switch (lg->regs->trapnum) {
		case 13: /* We've intercepted a GPF. */
			if (lg->regs->errcode == 0) {
				if (emulate_insn(lg))
					continue;
			}
			break;
		case 14: /* We've intercepted a page fault. */
			if (demand_page(lg, cr2, lg->regs->errcode))
				continue;

			/* If lguest_data is NULL, this won't hurt. */
			if (put_user(cr2, &lg->lguest_data->cr2))
				kill_guest(lg, "Writing cr2");
			break;
		case 7: /* We've intercepted a Device Not Available fault. */
			/* If they don't want to know, just absorb it. */
			if (!lg->ts)
				continue;
			break;
		case 32 ... 255: /* Real interrupt, fall thru */
			cond_resched();
		case LGUEST_TRAP_ENTRY: /* Handled at top of loop */
			continue;
		}

		if (deliver_trap(lg, lg->regs->trapnum))
			continue;

		kill_guest(lg, "unhandled trap %li at %#lx (%#lx)",
			   lg->regs->trapnum, lg->regs->eip,
			   lg->regs->trapnum == 14 ? cr2 : lg->regs->errcode);
	}
	return -ENOENT;
}

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

static int __init init(void)
{
	int err;

	if (paravirt_enabled()) {
		printk("lguest is afraid of %s\n", paravirt_ops.name);
		return -EPERM;
	}

	err = map_switcher();
	if (err)
		return err;

	err = init_pagetables(switcher_page, SHARED_SWITCHER_PAGES);
	if (err) {
		unmap_switcher();
		return err;
	}
	lguest_io_init();

	err = lguest_device_init();
	if (err) {
		free_pagetables();
		unmap_switcher();
		return err;
	}
	lock_cpu_hotplug();
	if (cpu_has_pge) { /* We have a broader idea of "global". */
		cpu_had_pge = 1;
		on_each_cpu(adjust_pge, (void *)0, 0, 1);
		clear_bit(X86_FEATURE_PGE, boot_cpu_data.x86_capability);
	}
	unlock_cpu_hotplug();
	return 0;
}

static void __exit fini(void)
{
	lguest_device_remove();
	free_pagetables();
	unmap_switcher();
	lock_cpu_hotplug();
	if (cpu_had_pge) {
		set_bit(X86_FEATURE_PGE, boot_cpu_data.x86_capability);
		on_each_cpu(adjust_pge, (void *)1, 0, 1);
	}
	unlock_cpu_hotplug();
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rusty Russell <rusty@rustcorp.com.au>");
