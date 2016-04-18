/*P:400
 * This contains run_guest() which actually calls into the Host<->Guest
 * Switcher and analyzes the return, such as determining if the Guest wants the
 * Host to do something.  This file also contains useful helper routines.
:*/
#include <linux/module.h>
#include <linux/stringify.h>
#include <linux/stddef.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/cpu.h>
#include <linux/freezer.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <asm/paravirt.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/poll.h>
#include <asm/asm-offsets.h>
#include "lg.h"

unsigned long switcher_addr;
struct page **lg_switcher_pages;
static struct vm_struct *switcher_text_vma;
static struct vm_struct *switcher_stacks_vma;

/* This One Big lock protects all inter-guest data structures. */
DEFINE_MUTEX(lguest_lock);

/*H:010
 * We need to set up the Switcher at a high virtual address.  Remember the
 * Switcher is a few hundred bytes of assembler code which actually changes the
 * CPU to run the Guest, and then changes back to the Host when a trap or
 * interrupt happens.
 *
 * The Switcher code must be at the same virtual address in the Guest as the
 * Host since it will be running as the switchover occurs.
 *
 * Trying to map memory at a particular address is an unusual thing to do, so
 * it's not a simple one-liner.
 */
static __init int map_switcher(void)
{
	int i, err;

	/*
	 * Map the Switcher in to high memory.
	 *
	 * It turns out that if we choose the address 0xFFC00000 (4MB under the
	 * top virtual address), it makes setting up the page tables really
	 * easy.
	 */

	/* We assume Switcher text fits into a single page. */
	if (end_switcher_text - start_switcher_text > PAGE_SIZE) {
		printk(KERN_ERR "lguest: switcher text too large (%zu)\n",
		       end_switcher_text - start_switcher_text);
		return -EINVAL;
	}

	/*
	 * We allocate an array of struct page pointers.  map_vm_area() wants
	 * this, rather than just an array of pages.
	 */
	lg_switcher_pages = kmalloc(sizeof(lg_switcher_pages[0])
				    * TOTAL_SWITCHER_PAGES,
				    GFP_KERNEL);
	if (!lg_switcher_pages) {
		err = -ENOMEM;
		goto out;
	}

	/*
	 * Now we actually allocate the pages.  The Guest will see these pages,
	 * so we make sure they're zeroed.
	 */
	for (i = 0; i < TOTAL_SWITCHER_PAGES; i++) {
		lg_switcher_pages[i] = alloc_page(GFP_KERNEL|__GFP_ZERO);
		if (!lg_switcher_pages[i]) {
			err = -ENOMEM;
			goto free_some_pages;
		}
	}

	/*
	 * Copy in the compiled-in Switcher code (from x86/switcher_32.S).
	 * It goes in the first page, which we map in momentarily.
	 */
	memcpy(kmap(lg_switcher_pages[0]), start_switcher_text,
	       end_switcher_text - start_switcher_text);
	kunmap(lg_switcher_pages[0]);

	/*
	 * We place the Switcher underneath the fixmap area, which is the
	 * highest virtual address we can get.  This is important, since we
	 * tell the Guest it can't access this memory, so we want its ceiling
	 * as high as possible.
	 */
	switcher_addr = FIXADDR_START - TOTAL_SWITCHER_PAGES*PAGE_SIZE;

	/*
	 * Now we reserve the "virtual memory area"s we want.  We might
	 * not get them in theory, but in practice it's worked so far.
	 *
	 * We want the switcher text to be read-only and executable, and
	 * the stacks to be read-write and non-executable.
	 */
	switcher_text_vma = __get_vm_area(PAGE_SIZE, VM_ALLOC|VM_NO_GUARD,
					  switcher_addr,
					  switcher_addr + PAGE_SIZE);

	if (!switcher_text_vma) {
		err = -ENOMEM;
		printk("lguest: could not map switcher pages high\n");
		goto free_pages;
	}

	switcher_stacks_vma = __get_vm_area(SWITCHER_STACK_PAGES * PAGE_SIZE,
					    VM_ALLOC|VM_NO_GUARD,
					    switcher_addr + PAGE_SIZE,
					    switcher_addr + TOTAL_SWITCHER_PAGES * PAGE_SIZE);
	if (!switcher_stacks_vma) {
		err = -ENOMEM;
		printk("lguest: could not map switcher pages high\n");
		goto free_text_vma;
	}

	/*
	 * This code actually sets up the pages we've allocated to appear at
	 * switcher_addr.  map_vm_area() takes the vma we allocated above, the
	 * kind of pages we're mapping (kernel text pages and kernel writable
	 * pages respectively), and a pointer to our array of struct pages.
	 */
	err = map_vm_area(switcher_text_vma, PAGE_KERNEL_RX, lg_switcher_pages);
	if (err) {
		printk("lguest: text map_vm_area failed: %i\n", err);
		goto free_vmas;
	}

	err = map_vm_area(switcher_stacks_vma, PAGE_KERNEL,
			  lg_switcher_pages + SWITCHER_TEXT_PAGES);
	if (err) {
		printk("lguest: stacks map_vm_area failed: %i\n", err);
		goto free_vmas;
	}

	/*
	 * Now the Switcher is mapped at the right address, we can't fail!
	 */
	printk(KERN_INFO "lguest: mapped switcher at %p\n",
	       switcher_text_vma->addr);
	/* And we succeeded... */
	return 0;

free_vmas:
	/* Undoes map_vm_area and __get_vm_area */
	vunmap(switcher_stacks_vma->addr);
free_text_vma:
	vunmap(switcher_text_vma->addr);
free_pages:
	i = TOTAL_SWITCHER_PAGES;
free_some_pages:
	for (--i; i >= 0; i--)
		__free_pages(lg_switcher_pages[i], 0);
	kfree(lg_switcher_pages);
out:
	return err;
}
/*:*/

/* Cleaning up the mapping when the module is unloaded is almost... too easy. */
static void unmap_switcher(void)
{
	unsigned int i;

	/* vunmap() undoes *both* map_vm_area() and __get_vm_area(). */
	vunmap(switcher_text_vma->addr);
	vunmap(switcher_stacks_vma->addr);
	/* Now we just need to free the pages we copied the switcher into */
	for (i = 0; i < TOTAL_SWITCHER_PAGES; i++)
		__free_pages(lg_switcher_pages[i], 0);
	kfree(lg_switcher_pages);
}

/*H:032
 * Dealing With Guest Memory.
 *
 * Before we go too much further into the Host, we need to grok the routines
 * we use to deal with Guest memory.
 *
 * When the Guest gives us (what it thinks is) a physical address, we can use
 * the normal copy_from_user() & copy_to_user() on the corresponding place in
 * the memory region allocated by the Launcher.
 *
 * But we can't trust the Guest: it might be trying to access the Launcher
 * code.  We have to check that the range is below the pfn_limit the Launcher
 * gave us.  We have to make sure that addr + len doesn't give us a false
 * positive by overflowing, too.
 */
bool lguest_address_ok(const struct lguest *lg,
		       unsigned long addr, unsigned long len)
{
	return addr+len <= lg->pfn_limit * PAGE_SIZE && (addr+len >= addr);
}

/*
 * This routine copies memory from the Guest.  Here we can see how useful the
 * kill_lguest() routine we met in the Launcher can be: we return a random
 * value (all zeroes) instead of needing to return an error.
 */
void __lgread(struct lg_cpu *cpu, void *b, unsigned long addr, unsigned bytes)
{
	if (!lguest_address_ok(cpu->lg, addr, bytes)
	    || copy_from_user(b, cpu->lg->mem_base + addr, bytes) != 0) {
		/* copy_from_user should do this, but as we rely on it... */
		memset(b, 0, bytes);
		kill_guest(cpu, "bad read address %#lx len %u", addr, bytes);
	}
}

/* This is the write (copy into Guest) version. */
void __lgwrite(struct lg_cpu *cpu, unsigned long addr, const void *b,
	       unsigned bytes)
{
	if (!lguest_address_ok(cpu->lg, addr, bytes)
	    || copy_to_user(cpu->lg->mem_base + addr, b, bytes) != 0)
		kill_guest(cpu, "bad write address %#lx len %u", addr, bytes);
}
/*:*/

/*H:030
 * Let's jump straight to the the main loop which runs the Guest.
 * Remember, this is called by the Launcher reading /dev/lguest, and we keep
 * going around and around until something interesting happens.
 */
int run_guest(struct lg_cpu *cpu, unsigned long __user *user)
{
	/* If the launcher asked for a register with LHREQ_GETREG */
	if (cpu->reg_read) {
		if (put_user(*cpu->reg_read, user))
			return -EFAULT;
		cpu->reg_read = NULL;
		return sizeof(*cpu->reg_read);
	}

	/* We stop running once the Guest is dead. */
	while (!cpu->lg->dead) {
		unsigned int irq;
		bool more;

		/* First we run any hypercalls the Guest wants done. */
		if (cpu->hcall)
			do_hypercalls(cpu);

		/* Do we have to tell the Launcher about a trap? */
		if (cpu->pending.trap) {
			if (copy_to_user(user, &cpu->pending,
					 sizeof(cpu->pending)))
				return -EFAULT;
			return sizeof(cpu->pending);
		}

		/*
		 * All long-lived kernel loops need to check with this horrible
		 * thing called the freezer.  If the Host is trying to suspend,
		 * it stops us.
		 */
		try_to_freeze();

		/* Check for signals */
		if (signal_pending(current))
			return -ERESTARTSYS;

		/*
		 * Check if there are any interrupts which can be delivered now:
		 * if so, this sets up the hander to be executed when we next
		 * run the Guest.
		 */
		irq = interrupt_pending(cpu, &more);
		if (irq < LGUEST_IRQS)
			try_deliver_interrupt(cpu, irq, more);

		/*
		 * Just make absolutely sure the Guest is still alive.  One of
		 * those hypercalls could have been fatal, for example.
		 */
		if (cpu->lg->dead)
			break;

		/*
		 * If the Guest asked to be stopped, we sleep.  The Guest's
		 * clock timer will wake us.
		 */
		if (cpu->halted) {
			set_current_state(TASK_INTERRUPTIBLE);
			/*
			 * Just before we sleep, make sure no interrupt snuck in
			 * which we should be doing.
			 */
			if (interrupt_pending(cpu, &more) < LGUEST_IRQS)
				set_current_state(TASK_RUNNING);
			else
				schedule();
			continue;
		}

		/*
		 * OK, now we're ready to jump into the Guest.  First we put up
		 * the "Do Not Disturb" sign:
		 */
		local_irq_disable();

		/* Actually run the Guest until something happens. */
		lguest_arch_run_guest(cpu);

		/* Now we're ready to be interrupted or moved to other CPUs */
		local_irq_enable();

		/* Now we deal with whatever happened to the Guest. */
		lguest_arch_handle_trap(cpu);
	}

	/* Special case: Guest is 'dead' but wants a reboot. */
	if (cpu->lg->dead == ERR_PTR(-ERESTART))
		return -ERESTART;

	/* The Guest is dead => "No such file or directory" */
	return -ENOENT;
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
	if (get_kernel_rpl() != 0) {
		printk("lguest is afraid of being a guest\n");
		return -EPERM;
	}

	/* First we put the Switcher up in very high virtual memory. */
	err = map_switcher();
	if (err)
		goto out;

	/* We might need to reserve an interrupt vector. */
	err = init_interrupts();
	if (err)
		goto unmap;

	/* /dev/lguest needs to be registered. */
	err = lguest_device_init();
	if (err)
		goto free_interrupts;

	/* Finally we do some architecture-specific setup. */
	lguest_arch_host_init();

	/* All good! */
	return 0;

free_interrupts:
	free_interrupts();
unmap:
	unmap_switcher();
out:
	return err;
}

/* Cleaning up is just the same code, backwards.  With a little French. */
static void __exit fini(void)
{
	lguest_device_remove();
	free_interrupts();
	unmap_switcher();

	lguest_arch_host_fini();
}
/*:*/

/*
 * The Host side of lguest can be a module.  This is a nice way for people to
 * play with it.
 */
module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rusty Russell <rusty@rustcorp.com.au>");
