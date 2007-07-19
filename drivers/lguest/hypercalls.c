/*  Actual hypercalls, which allow guests to actually do something.
    Copyright (C) 2006 Rusty Russell IBM Corporation

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

static void do_hcall(struct lguest *lg, struct lguest_regs *regs)
{
	switch (regs->eax) {
	case LHCALL_FLUSH_ASYNC:
		break;
	case LHCALL_LGUEST_INIT:
		kill_guest(lg, "already have lguest_data");
		break;
	case LHCALL_CRASH: {
		char msg[128];
		lgread(lg, msg, regs->edx, sizeof(msg));
		msg[sizeof(msg)-1] = '\0';
		kill_guest(lg, "CRASH: %s", msg);
		break;
	}
	case LHCALL_FLUSH_TLB:
		if (regs->edx)
			guest_pagetable_clear_all(lg);
		else
			guest_pagetable_flush_user(lg);
		break;
	case LHCALL_GET_WALLCLOCK: {
		struct timespec ts;
		ktime_get_real_ts(&ts);
		regs->eax = ts.tv_sec;
		break;
	}
	case LHCALL_BIND_DMA:
		regs->eax = bind_dma(lg, regs->edx, regs->ebx,
				     regs->ecx >> 8, regs->ecx & 0xFF);
		break;
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
		lg->ts = regs->edx;
		break;
	case LHCALL_HALT:
		lg->halted = 1;
		break;
	default:
		kill_guest(lg, "Bad hypercall %li\n", regs->eax);
	}
}

/* We always do queued calls before actual hypercall. */
static void do_async_hcalls(struct lguest *lg)
{
	unsigned int i;
	u8 st[LHCALL_RING_SIZE];

	if (copy_from_user(&st, &lg->lguest_data->hcall_status, sizeof(st)))
		return;

	for (i = 0; i < ARRAY_SIZE(st); i++) {
		struct lguest_regs regs;
		unsigned int n = lg->next_hcall;

		if (st[n] == 0xFF)
			break;

		if (++lg->next_hcall == LHCALL_RING_SIZE)
			lg->next_hcall = 0;

		if (get_user(regs.eax, &lg->lguest_data->hcalls[n].eax)
		    || get_user(regs.edx, &lg->lguest_data->hcalls[n].edx)
		    || get_user(regs.ecx, &lg->lguest_data->hcalls[n].ecx)
		    || get_user(regs.ebx, &lg->lguest_data->hcalls[n].ebx)) {
			kill_guest(lg, "Fetching async hypercalls");
			break;
		}

		do_hcall(lg, &regs);
		if (put_user(0xFF, &lg->lguest_data->hcall_status[n])) {
			kill_guest(lg, "Writing result for async hypercall");
			break;
		}

		if (lg->dma_is_pending)
			break;
	}
}

static void initialize(struct lguest *lg)
{
	u32 tsc_speed;

	if (lg->regs->eax != LHCALL_LGUEST_INIT) {
		kill_guest(lg, "hypercall %li before LGUEST_INIT",
			   lg->regs->eax);
		return;
	}

	/* We only tell the guest to use the TSC if it's reliable. */
	if (boot_cpu_has(X86_FEATURE_CONSTANT_TSC) && !check_tsc_unstable())
		tsc_speed = tsc_khz;
	else
		tsc_speed = 0;

	lg->lguest_data = (struct lguest_data __user *)lg->regs->edx;
	/* We check here so we can simply copy_to_user/from_user */
	if (!lguest_address_ok(lg, lg->regs->edx, sizeof(*lg->lguest_data))) {
		kill_guest(lg, "bad guest page %p", lg->lguest_data);
		return;
	}
	if (get_user(lg->noirq_start, &lg->lguest_data->noirq_start)
	    || get_user(lg->noirq_end, &lg->lguest_data->noirq_end)
	    /* We reserve the top pgd entry. */
	    || put_user(4U*1024*1024, &lg->lguest_data->reserve_mem)
	    || put_user(tsc_speed, &lg->lguest_data->tsc_khz)
	    || put_user(lg->guestid, &lg->lguest_data->guestid))
		kill_guest(lg, "bad guest page %p", lg->lguest_data);

	/* This is the one case where the above accesses might have
	 * been the first write to a Guest page.  This may have caused
	 * a copy-on-write fault, but the Guest might be referring to
	 * the old (read-only) page. */
	guest_pagetable_clear_all(lg);
}

/* Even if we go out to userspace and come back, we don't want to do
 * the hypercall again. */
static void clear_hcall(struct lguest *lg)
{
	lg->regs->trapnum = 255;
}

void do_hypercalls(struct lguest *lg)
{
	if (unlikely(!lg->lguest_data)) {
		if (lg->regs->trapnum == LGUEST_TRAP_ENTRY) {
			initialize(lg);
			clear_hcall(lg);
		}
		return;
	}

	do_async_hcalls(lg);
	if (!lg->dma_is_pending && lg->regs->trapnum == LGUEST_TRAP_ENTRY) {
		do_hcall(lg, lg->regs);
		clear_hcall(lg);
	}
}
