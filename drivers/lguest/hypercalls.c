/*P:500
 * Just as userspace programs request kernel operations through a system
 * call, the Guest requests Host operations through a "hypercall".  You might
 * notice this nomenclature doesn't really follow any logic, but the name has
 * been around for long enough that we're stuck with it.  As you'd expect, this
 * code is basically a one big switch statement.
:*/

/*  Copyright (C) 2006 Rusty Russell IBM Corporation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
*/
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/ktime.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include "lg.h"

/*H:120
 * This is the core hypercall routine: where the Guest gets what it wants.
 * Or gets killed.  Or, in the case of LHCALL_SHUTDOWN, both.
 */
static void do_hcall(struct lg_cpu *cpu, struct hcall_args *args)
{
	switch (args->arg0) {
	case LHCALL_FLUSH_ASYNC:
		/*
		 * This call does nothing, except by breaking out of the Guest
		 * it makes us process all the asynchronous hypercalls.
		 */
		break;
	case LHCALL_SEND_INTERRUPTS:
		/*
		 * This call does nothing too, but by breaking out of the Guest
		 * it makes us process any pending interrupts.
		 */
		break;
	case LHCALL_LGUEST_INIT:
		/*
		 * You can't get here unless you're already initialized.  Don't
		 * do that.
		 */
		kill_guest(cpu, "already have lguest_data");
		break;
	case LHCALL_SHUTDOWN: {
		char msg[128];
		/*
		 * Shutdown is such a trivial hypercall that we do it in five
		 * lines right here.
		 *
		 * If the lgread fails, it will call kill_guest() itself; the
		 * kill_guest() with the message will be ignored.
		 */
		__lgread(cpu, msg, args->arg1, sizeof(msg));
		msg[sizeof(msg)-1] = '\0';
		kill_guest(cpu, "CRASH: %s", msg);
		if (args->arg2 == LGUEST_SHUTDOWN_RESTART)
			cpu->lg->dead = ERR_PTR(-ERESTART);
		break;
	}
	case LHCALL_FLUSH_TLB:
		/* FLUSH_TLB comes in two flavors, depending on the argument: */
		if (args->arg1)
			guest_pagetable_clear_all(cpu);
		else
			guest_pagetable_flush_user(cpu);
		break;

	/*
	 * All these calls simply pass the arguments through to the right
	 * routines.
	 */
	case LHCALL_NEW_PGTABLE:
		guest_new_pagetable(cpu, args->arg1);
		break;
	case LHCALL_SET_STACK:
		guest_set_stack(cpu, args->arg1, args->arg2, args->arg3);
		break;
	case LHCALL_SET_PTE:
#ifdef CONFIG_X86_PAE
		guest_set_pte(cpu, args->arg1, args->arg2,
				__pte(args->arg3 | (u64)args->arg4 << 32));
#else
		guest_set_pte(cpu, args->arg1, args->arg2, __pte(args->arg3));
#endif
		break;
	case LHCALL_SET_PGD:
		guest_set_pgd(cpu->lg, args->arg1, args->arg2);
		break;
#ifdef CONFIG_X86_PAE
	case LHCALL_SET_PMD:
		guest_set_pmd(cpu->lg, args->arg1, args->arg2);
		break;
#endif
	case LHCALL_SET_CLOCKEVENT:
		guest_set_clockevent(cpu, args->arg1);
		break;
	case LHCALL_TS:
		/* This sets the TS flag, as we saw used in run_guest(). */
		cpu->ts = args->arg1;
		break;
	case LHCALL_HALT:
		/* Similarly, this sets the halted flag for run_guest(). */
		cpu->halted = 1;
		break;
	default:
		/* It should be an architecture-specific hypercall. */
		if (lguest_arch_do_hcall(cpu, args))
			kill_guest(cpu, "Bad hypercall %li\n", args->arg0);
	}
}

/*H:124
 * Asynchronous hypercalls are easy: we just look in the array in the
 * Guest's "struct lguest_data" to see if any new ones are marked "ready".
 *
 * We are careful to do these in order: obviously we respect the order the
 * Guest put them in the ring, but we also promise the Guest that they will
 * happen before any normal hypercall (which is why we check this before
 * checking for a normal hcall).
 */
static void do_async_hcalls(struct lg_cpu *cpu)
{
	unsigned int i;
	u8 st[LHCALL_RING_SIZE];

	/* For simplicity, we copy the entire call status array in at once. */
	if (copy_from_user(&st, &cpu->lg->lguest_data->hcall_status, sizeof(st)))
		return;

	/* We process "struct lguest_data"s hcalls[] ring once. */
	for (i = 0; i < ARRAY_SIZE(st); i++) {
		struct hcall_args args;
		/*
		 * We remember where we were up to from last time.  This makes
		 * sure that the hypercalls are done in the order the Guest
		 * places them in the ring.
		 */
		unsigned int n = cpu->next_hcall;

		/* 0xFF means there's no call here (yet). */
		if (st[n] == 0xFF)
			break;

		/*
		 * OK, we have hypercall.  Increment the "next_hcall" cursor,
		 * and wrap back to 0 if we reach the end.
		 */
		if (++cpu->next_hcall == LHCALL_RING_SIZE)
			cpu->next_hcall = 0;

		/*
		 * Copy the hypercall arguments into a local copy of the
		 * hcall_args struct.
		 */
		if (copy_from_user(&args, &cpu->lg->lguest_data->hcalls[n],
				   sizeof(struct hcall_args))) {
			kill_guest(cpu, "Fetching async hypercalls");
			break;
		}

		/* Do the hypercall, same as a normal one. */
		do_hcall(cpu, &args);

		/* Mark the hypercall done. */
		if (put_user(0xFF, &cpu->lg->lguest_data->hcall_status[n])) {
			kill_guest(cpu, "Writing result for async hypercall");
			break;
		}

		/*
		 * Stop doing hypercalls if they want to notify the Launcher:
		 * it needs to service this first.
		 */
		if (cpu->pending.trap)
			break;
	}
}

/*
 * Last of all, we look at what happens first of all.  The very first time the
 * Guest makes a hypercall, we end up here to set things up:
 */
static void initialize(struct lg_cpu *cpu)
{
	/*
	 * You can't do anything until you're initialized.  The Guest knows the
	 * rules, so we're unforgiving here.
	 */
	if (cpu->hcall->arg0 != LHCALL_LGUEST_INIT) {
		kill_guest(cpu, "hypercall %li before INIT", cpu->hcall->arg0);
		return;
	}

	if (lguest_arch_init_hypercalls(cpu))
		kill_guest(cpu, "bad guest page %p", cpu->lg->lguest_data);

	/*
	 * The Guest tells us where we're not to deliver interrupts by putting
	 * the instruction address into "struct lguest_data".
	 */
	if (get_user(cpu->lg->noirq_iret, &cpu->lg->lguest_data->noirq_iret))
		kill_guest(cpu, "bad guest page %p", cpu->lg->lguest_data);

	/*
	 * We write the current time into the Guest's data page once so it can
	 * set its clock.
	 */
	write_timestamp(cpu);

	/* page_tables.c will also do some setup. */
	page_table_guest_data_init(cpu);

	/*
	 * This is the one case where the above accesses might have been the
	 * first write to a Guest page.  This may have caused a copy-on-write
	 * fault, but the old page might be (read-only) in the Guest
	 * pagetable.
	 */
	guest_pagetable_clear_all(cpu);
}
/*:*/

/*M:013
 * If a Guest reads from a page (so creates a mapping) that it has never
 * written to, and then the Launcher writes to it (ie. the output of a virtual
 * device), the Guest will still see the old page.  In practice, this never
 * happens: why would the Guest read a page which it has never written to?  But
 * a similar scenario might one day bite us, so it's worth mentioning.
 *
 * Note that if we used a shared anonymous mapping in the Launcher instead of
 * mapping /dev/zero private, we wouldn't worry about cop-on-write.  And we
 * need that to switch the Launcher to processes (away from threads) anyway.
:*/

/*H:100
 * Hypercalls
 *
 * Remember from the Guest, hypercalls come in two flavors: normal and
 * asynchronous.  This file handles both of types.
 */
void do_hypercalls(struct lg_cpu *cpu)
{
	/* Not initialized yet?  This hypercall must do it. */
	if (unlikely(!cpu->lg->lguest_data)) {
		/* Set up the "struct lguest_data" */
		initialize(cpu);
		/* Hcall is done. */
		cpu->hcall = NULL;
		return;
	}

	/*
	 * The Guest has initialized.
	 *
	 * Look in the hypercall ring for the async hypercalls:
	 */
	do_async_hcalls(cpu);

	/*
	 * If we stopped reading the hypercall ring because the Guest did a
	 * NOTIFY to the Launcher, we want to return now.  Otherwise we do
	 * the hypercall.
	 */
	if (!cpu->pending.trap) {
		do_hcall(cpu, cpu->hcall);
		/*
		 * Tricky point: we reset the hcall pointer to mark the
		 * hypercall as "done".  We use the hcall pointer rather than
		 * the trap number to indicate a hypercall is pending.
		 * Normally it doesn't matter: the Guest will run again and
		 * update the trap number before we come back here.
		 *
		 * However, if we are signalled or the Guest sends I/O to the
		 * Launcher, the run_guest() loop will exit without running the
		 * Guest.  When it comes back it would try to re-run the
		 * hypercall.  Finding that bug sucked.
		 */
		cpu->hcall = NULL;
	}
}

/*
 * This routine supplies the Guest with time: it's used for wallclock time at
 * initial boot and as a rough time source if the TSC isn't available.
 */
void write_timestamp(struct lg_cpu *cpu)
{
	struct timespec now;
	ktime_get_real_ts(&now);
	if (copy_to_user(&cpu->lg->lguest_data->time,
			 &now, sizeof(struct timespec)))
		kill_guest(cpu, "Writing timestamp");
}
