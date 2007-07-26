/*P:500 Just as userspace programs request kernel operations through a system
 * call, the Guest requests Host operations through a "hypercall".  You might
 * notice this nomenclature doesn't really follow any logic, but the name has
 * been around for long enough that we're stuck with it.  As you'd expect, this
 * code is basically a one big switch statement. :*/

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
#include <asm/page.h>
#include <asm/pgtable.h>
#include <irq_vectors.h>
#include "lg.h"

/*H:120 This is the core hypercall routine: where the Guest gets what it
 * wants.  Or gets killed.  Or, in the case of LHCALL_CRASH, both.
 *
 * Remember from the Guest: %eax == which call to make, and the arguments are
 * packed into %edx, %ebx and %ecx if needed. */
static void do_hcall(struct lguest *lg, struct lguest_regs *regs)
{
	switch (regs->eax) {
	case LHCALL_FLUSH_ASYNC:
		/* This call does nothing, except by breaking out of the Guest
		 * it makes us process all the asynchronous hypercalls. */
		break;
	case LHCALL_LGUEST_INIT:
		/* You can't get here unless you're already initialized.  Don't
		 * do that. */
		kill_guest(lg, "already have lguest_data");
		break;
	case LHCALL_CRASH: {
		/* Crash is such a trivial hypercall that we do it in four
		 * lines right here. */
		char msg[128];
		/* If the lgread fails, it will call kill_guest() itself; the
		 * kill_guest() with the message will be ignored. */
		lgread(lg, msg, regs->edx, sizeof(msg));
		msg[sizeof(msg)-1] = '\0';
		kill_guest(lg, "CRASH: %s", msg);
		break;
	}
	case LHCALL_FLUSH_TLB:
		/* FLUSH_TLB comes in two flavors, depending on the
		 * argument: */
		if (regs->edx)
			guest_pagetable_clear_all(lg);
		else
			guest_pagetable_flush_user(lg);
		break;
	case LHCALL_GET_WALLCLOCK: {
		/* The Guest wants to know the real time in seconds since 1970,
		 * in good Unix tradition. */
		struct timespec ts;
		ktime_get_real_ts(&ts);
		regs->eax = ts.tv_sec;
		break;
	}
	case LHCALL_BIND_DMA:
		/* BIND_DMA really wants four arguments, but it's the only call
		 * which does.  So the Guest packs the number of buffers and
		 * the interrupt number into the final argument, and we decode
		 * it here.  This can legitimately fail, since we currently
		 * place a limit on the number of DMA pools a Guest can have.
		 * So we return true or false from this call. */
		regs->eax = bind_dma(lg, regs->edx, regs->ebx,
				     regs->ecx >> 8, regs->ecx & 0xFF);
		break;

	/* All these calls simply pass the arguments through to the right
	 * routines. */
	case LHCALL_SEND_DMA:
		send_dma(lg, regs->edx, regs->ebx);
		break;
	case LHCALL_LOAD_GDT:
		load_guest_gdt(lg, regs->edx, regs->ebx);
		break;
	case LHCALL_LOAD_IDT_ENTRY:
		load_guest_idt_entry(lg, regs->edx, regs->ebx, regs->ecx);
		break;
	case LHCALL_NEW_PGTABLE:
		guest_new_pagetable(lg, regs->edx);
		break;
	case LHCALL_SET_STACK:
		guest_set_stack(lg, regs->edx, regs->ebx, regs->ecx);
		break;
	case LHCALL_SET_PTE:
		guest_set_pte(lg, regs->edx, regs->ebx, mkgpte(regs->ecx));
		break;
	case LHCALL_SET_PMD:
		guest_set_pmd(lg, regs->edx, regs->ebx);
		break;
	case LHCALL_LOAD_TLS:
		guest_load_tls(lg, regs->edx);
		break;
	case LHCALL_SET_CLOCKEVENT:
		guest_set_clockevent(lg, regs->edx);
		break;

	case LHCALL_TS:
		/* This sets the TS flag, as we saw used in run_guest(). */
		lg->ts = regs->edx;
		break;
	case LHCALL_HALT:
		/* Similarly, this sets the halted flag for run_guest(). */
		lg->halted = 1;
		break;
	default:
		kill_guest(lg, "Bad hypercall %li\n", regs->eax);
	}
}

/* Asynchronous hypercalls are easy: we just look in the array in the Guest's
 * "struct lguest_data" and see if there are any new ones marked "ready".
 *
 * We are careful to do these in order: obviously we respect the order the
 * Guest put them in the ring, but we also promise the Guest that they will
 * happen before any normal hypercall (which is why we check this before
 * checking for a normal hcall). */
static void do_async_hcalls(struct lguest *lg)
{
	unsigned int i;
	u8 st[LHCALL_RING_SIZE];

	/* For simplicity, we copy the entire call status array in at once. */
	if (copy_from_user(&st, &lg->lguest_data->hcall_status, sizeof(st)))
		return;


	/* We process "struct lguest_data"s hcalls[] ring once. */
	for (i = 0; i < ARRAY_SIZE(st); i++) {
		struct lguest_regs regs;
		/* We remember where we were up to from last time.  This makes
		 * sure that the hypercalls are done in the order the Guest
		 * places them in the ring. */
		unsigned int n = lg->next_hcall;

		/* 0xFF means there's no call here (yet). */
		if (st[n] == 0xFF)
			break;

		/* OK, we have hypercall.  Increment the "next_hcall" cursor,
		 * and wrap back to 0 if we reach the end. */
		if (++lg->next_hcall == LHCALL_RING_SIZE)
			lg->next_hcall = 0;

		/* We copy the hypercall arguments into a fake register
		 * structure.  This makes life simple for do_hcall(). */
		if (get_user(regs.eax, &lg->lguest_data->hcalls[n].eax)
		    || get_user(regs.edx, &lg->lguest_data->hcalls[n].edx)
		    || get_user(regs.ecx, &lg->lguest_data->hcalls[n].ecx)
		    || get_user(regs.ebx, &lg->lguest_data->hcalls[n].ebx)) {
			kill_guest(lg, "Fetching async hypercalls");
			break;
		}

		/* Do the hypercall, same as a normal one. */
		do_hcall(lg, &regs);

		/* Mark the hypercall done. */
		if (put_user(0xFF, &lg->lguest_data->hcall_status[n])) {
			kill_guest(lg, "Writing result for async hypercall");
			break;
		}

 		/* Stop doing hypercalls if we've just done a DMA to the
		 * Launcher: it needs to service this first. */
		if (lg->dma_is_pending)
			break;
	}
}

/* Last of all, we look at what happens first of all.  The very first time the
 * Guest makes a hypercall, we end up here to set things up: */
static void initialize(struct lguest *lg)
{
	u32 tsc_speed;

	/* You can't do anything until you're initialized.  The Guest knows the
	 * rules, so we're unforgiving here. */
	if (lg->regs->eax != LHCALL_LGUEST_INIT) {
		kill_guest(lg, "hypercall %li before LGUEST_INIT",
			   lg->regs->eax);
		return;
	}

	/* We insist that the Time Stamp Counter exist and doesn't change with
	 * cpu frequency.  Some devious chip manufacturers decided that TSC
	 * changes could be handled in software.  I decided that time going
	 * backwards might be good for benchmarks, but it's bad for users.
	 *
	 * We also insist that the TSC be stable: the kernel detects unreliable
	 * TSCs for its own purposes, and we use that here. */
	if (boot_cpu_has(X86_FEATURE_CONSTANT_TSC) && !check_tsc_unstable())
		tsc_speed = tsc_khz;
	else
		tsc_speed = 0;

	/* The pointer to the Guest's "struct lguest_data" is the only
	 * argument. */
	lg->lguest_data = (struct lguest_data __user *)lg->regs->edx;
	/* If we check the address they gave is OK now, we can simply
	 * copy_to_user/from_user from now on rather than using lgread/lgwrite.
	 * I put this in to show that I'm not immune to writing stupid
	 * optimizations. */
	if (!lguest_address_ok(lg, lg->regs->edx, sizeof(*lg->lguest_data))) {
		kill_guest(lg, "bad guest page %p", lg->lguest_data);
		return;
	}
	/* The Guest tells us where we're not to deliver interrupts by putting
	 * the range of addresses into "struct lguest_data". */
	if (get_user(lg->noirq_start, &lg->lguest_data->noirq_start)
	    || get_user(lg->noirq_end, &lg->lguest_data->noirq_end)
	    /* We tell the Guest that it can't use the top 4MB of virtual
	     * addresses used by the Switcher. */
	    || put_user(4U*1024*1024, &lg->lguest_data->reserve_mem)
	    || put_user(tsc_speed, &lg->lguest_data->tsc_khz)
	    /* We also give the Guest a unique id, as used in lguest_net.c. */
	    || put_user(lg->guestid, &lg->lguest_data->guestid))
		kill_guest(lg, "bad guest page %p", lg->lguest_data);

	/* This is the one case where the above accesses might have been the
	 * first write to a Guest page.  This may have caused a copy-on-write
	 * fault, but the Guest might be referring to the old (read-only)
	 * page. */
	guest_pagetable_clear_all(lg);
}
/* Now we've examined the hypercall code; our Guest can make requests.  There
 * is one other way we can do things for the Guest, as we see in
 * emulate_insn(). */

/*H:110 Tricky point: we mark the hypercall as "done" once we've done it.
 * Normally we don't need to do this: the Guest will run again and update the
 * trap number before we come back around the run_guest() loop to
 * do_hypercalls().
 *
 * However, if we are signalled or the Guest sends DMA to the Launcher, that
 * loop will exit without running the Guest.  When it comes back it would try
 * to re-run the hypercall. */
static void clear_hcall(struct lguest *lg)
{
	lg->regs->trapnum = 255;
}

/*H:100
 * Hypercalls
 *
 * Remember from the Guest, hypercalls come in two flavors: normal and
 * asynchronous.  This file handles both of types.
 */
void do_hypercalls(struct lguest *lg)
{
	/* Not initialized yet? */
	if (unlikely(!lg->lguest_data)) {
		/* Did the Guest make a hypercall?  We might have come back for
		 * some other reason (an interrupt, a different trap). */
		if (lg->regs->trapnum == LGUEST_TRAP_ENTRY) {
			/* Set up the "struct lguest_data" */
			initialize(lg);
			/* The hypercall is done. */
			clear_hcall(lg);
		}
		return;
	}

	/* The Guest has initialized.
	 *
	 * Look in the hypercall ring for the async hypercalls: */
	do_async_hcalls(lg);

	/* If we stopped reading the hypercall ring because the Guest did a
	 * SEND_DMA to the Launcher, we want to return now.  Otherwise if the
	 * Guest asked us to do a hypercall, we do it. */
	if (!lg->dma_is_pending && lg->regs->trapnum == LGUEST_TRAP_ENTRY) {
		do_hcall(lg, lg->regs);
		/* The hypercall is done. */
		clear_hcall(lg);
	}
}
