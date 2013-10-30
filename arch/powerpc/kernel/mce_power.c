/*
 * Machine check exception handling CPU-side for power7 and power8
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright 2013 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#undef DEBUG
#define pr_fmt(fmt) "mce_power: " fmt

#include <linux/types.h>
#include <linux/ptrace.h>
#include <asm/mmu.h>
#include <asm/mce.h>

/* flush SLBs and reload */
static void flush_and_reload_slb(void)
{
	struct slb_shadow *slb;
	unsigned long i, n;

	/* Invalidate all SLBs */
	asm volatile("slbmte %0,%0; slbia" : : "r" (0));

#ifdef CONFIG_KVM_BOOK3S_HANDLER
	/*
	 * If machine check is hit when in guest or in transition, we will
	 * only flush the SLBs and continue.
	 */
	if (get_paca()->kvm_hstate.in_guest)
		return;
#endif

	/* For host kernel, reload the SLBs from shadow SLB buffer. */
	slb = get_slb_shadow();
	if (!slb)
		return;

	n = min_t(u32, slb->persistent, SLB_MIN_SIZE);

	/* Load up the SLB entries from shadow SLB */
	for (i = 0; i < n; i++) {
		unsigned long rb = slb->save_area[i].esid;
		unsigned long rs = slb->save_area[i].vsid;

		rb = (rb & ~0xFFFul) | i;
		asm volatile("slbmte %0,%1" : : "r" (rs), "r" (rb));
	}
}

static long mce_handle_derror(uint64_t dsisr, uint64_t slb_error_bits)
{
	long handled = 1;

	/*
	 * flush and reload SLBs for SLB errors and flush TLBs for TLB errors.
	 * reset the error bits whenever we handle them so that at the end
	 * we can check whether we handled all of them or not.
	 * */
	if (dsisr & slb_error_bits) {
		flush_and_reload_slb();
		/* reset error bits */
		dsisr &= ~(slb_error_bits);
	}
	if (dsisr & P7_DSISR_MC_TLB_MULTIHIT_MFTLB) {
		if (cur_cpu_spec && cur_cpu_spec->flush_tlb)
			cur_cpu_spec->flush_tlb(TLBIEL_INVAL_PAGE);
		/* reset error bits */
		dsisr &= ~P7_DSISR_MC_TLB_MULTIHIT_MFTLB;
	}
	/* Any other errors we don't understand? */
	if (dsisr & 0xffffffffUL)
		handled = 0;

	return handled;
}

static long mce_handle_derror_p7(uint64_t dsisr)
{
	return mce_handle_derror(dsisr, P7_DSISR_MC_SLB_ERRORS);
}

static long mce_handle_common_ierror(uint64_t srr1)
{
	long handled = 0;

	switch (P7_SRR1_MC_IFETCH(srr1)) {
	case 0:
		break;
	case P7_SRR1_MC_IFETCH_SLB_PARITY:
	case P7_SRR1_MC_IFETCH_SLB_MULTIHIT:
		/* flush and reload SLBs for SLB errors. */
		flush_and_reload_slb();
		handled = 1;
		break;
	case P7_SRR1_MC_IFETCH_TLB_MULTIHIT:
		if (cur_cpu_spec && cur_cpu_spec->flush_tlb) {
			cur_cpu_spec->flush_tlb(TLBIEL_INVAL_PAGE);
			handled = 1;
		}
		break;
	default:
		break;
	}

	return handled;
}

static long mce_handle_ierror_p7(uint64_t srr1)
{
	long handled = 0;

	handled = mce_handle_common_ierror(srr1);

	if (P7_SRR1_MC_IFETCH(srr1) == P7_SRR1_MC_IFETCH_SLB_BOTH) {
		flush_and_reload_slb();
		handled = 1;
	}
	return handled;
}

long __machine_check_early_realmode_p7(struct pt_regs *regs)
{
	uint64_t srr1;
	long handled = 1;

	srr1 = regs->msr;

	if (P7_SRR1_MC_LOADSTORE(srr1))
		handled = mce_handle_derror_p7(regs->dsisr);
	else
		handled = mce_handle_ierror_p7(srr1);

	/* TODO: Decode machine check reason. */
	return handled;
}

static long mce_handle_ierror_p8(uint64_t srr1)
{
	long handled = 0;

	handled = mce_handle_common_ierror(srr1);

	if (P7_SRR1_MC_IFETCH(srr1) == P8_SRR1_MC_IFETCH_ERAT_MULTIHIT) {
		flush_and_reload_slb();
		handled = 1;
	}
	return handled;
}

static long mce_handle_derror_p8(uint64_t dsisr)
{
	return mce_handle_derror(dsisr, P8_DSISR_MC_SLB_ERRORS);
}

long __machine_check_early_realmode_p8(struct pt_regs *regs)
{
	uint64_t srr1;
	long handled = 1;

	srr1 = regs->msr;

	if (P7_SRR1_MC_LOADSTORE(srr1))
		handled = mce_handle_derror_p8(regs->dsisr);
	else
		handled = mce_handle_ierror_p8(srr1);

	/* TODO: Decode machine check reason. */
	return handled;
}
