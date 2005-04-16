/* 
 * arch/sh/mm/fault-nommu.c
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * Based on linux/arch/sh/mm/fault.c:
 *  Copyright (C) 1999  Niibe Yutaka
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

#if defined(CONFIG_SH_KGDB)
#include <asm/kgdb.h>
#endif

extern void die(const char *,struct pt_regs *,long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long writeaccess,
			      unsigned long address)
{
#if defined(CONFIG_SH_KGDB)
	if (kgdb_nofault && kgdb_bus_err_hook)
		kgdb_bus_err_hook();
#endif

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 *
	 */
	if (address < PAGE_SIZE) {
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	} else {
		printk(KERN_ALERT "Unable to handle kernel paging request");
	}

	printk(" at virtual address %08lx\n", address);
	printk(KERN_ALERT "pc = %08lx\n", regs->pc);

	die("Oops", regs, writeaccess);
	do_exit(SIGKILL);
}

asmlinkage int __do_page_fault(struct pt_regs *regs, unsigned long writeaccess,
			       unsigned long address)
{
#if defined(CONFIG_SH_KGDB)
	if (kgdb_nofault && kgdb_bus_err_hook)
		kgdb_bus_err_hook();
#endif

	if (address >= TASK_SIZE)
		return 1;

	return 0;
}

