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
#include <asm/machdep.h>

static void flush_tlb_206(unsigned int num_sets, unsigned int action)
{
	unsigned long rb;
	unsigned int i;

	switch (action) {
	case TLB_INVAL_SCOPE_GLOBAL:
		rb = TLBIEL_INVAL_SET;
		break;
	case TLB_INVAL_SCOPE_LPID:
		rb = TLBIEL_INVAL_SET_LPID;
		break;
	default:
		BUG();
		break;
	}

	asm volatile("ptesync" : : : "memory");
	for (i = 0; i < num_sets; i++) {
		asm volatile("tlbiel %0" : : "r" (rb));
		rb += 1 << TLBIEL_INVAL_SET_SHIFT;
	}
	asm volatile("ptesync" : : : "memory");
}

/*
 * Generic routine to flush TLB on power7. This routine is used as
 * flush_tlb hook in cpu_spec for Power7 processor.
 *
 * action => TLB_INVAL_SCOPE_GLOBAL:  Invalidate all TLBs.
 *	     TLB_INVAL_SCOPE_LPID: Invalidate TLB for current LPID.
 */
void __flush_tlb_power7(unsigned int action)
{
	flush_tlb_206(POWER7_TLB_SETS, action);
}

/*
 * Generic routine to flush TLB on power8. This routine is used as
 * flush_tlb hook in cpu_spec for power8 processor.
 *
 * action => TLB_INVAL_SCOPE_GLOBAL:  Invalidate all TLBs.
 *	     TLB_INVAL_SCOPE_LPID: Invalidate TLB for current LPID.
 */
void __flush_tlb_power8(unsigned int action)
{
	flush_tlb_206(POWER8_TLB_SETS, action);
}

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

	n = min_t(u32, be32_to_cpu(slb->persistent), SLB_MIN_SIZE);

	/* Load up the SLB entries from shadow SLB */
	for (i = 0; i < n; i++) {
		unsigned long rb = be64_to_cpu(slb->save_area[i].esid);
		unsigned long rs = be64_to_cpu(slb->save_area[i].vsid);

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
			cur_cpu_spec->flush_tlb(TLB_INVAL_SCOPE_GLOBAL);
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
			cur_cpu_spec->flush_tlb(TLB_INVAL_SCOPE_GLOBAL);
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

static void mce_get_common_ierror(struct mce_error_info *mce_err, uint64_t srr1)
{
	switch (P7_SRR1_MC_IFETCH(srr1)) {
	case P7_SRR1_MC_IFETCH_SLB_PARITY:
		mce_err->error_type = MCE_ERROR_TYPE_SLB;
		mce_err->u.slb_error_type = MCE_SLB_ERROR_PARITY;
		break;
	case P7_SRR1_MC_IFETCH_SLB_MULTIHIT:
		mce_err->error_type = MCE_ERROR_TYPE_SLB;
		mce_err->u.slb_error_type = MCE_SLB_ERROR_MULTIHIT;
		break;
	case P7_SRR1_MC_IFETCH_TLB_MULTIHIT:
		mce_err->error_type = MCE_ERROR_TYPE_TLB;
		mce_err->u.tlb_error_type = MCE_TLB_ERROR_MULTIHIT;
		break;
	case P7_SRR1_MC_IFETCH_UE:
	case P7_SRR1_MC_IFETCH_UE_IFU_INTERNAL:
		mce_err->error_type = MCE_ERROR_TYPE_UE;
		mce_err->u.ue_error_type = MCE_UE_ERROR_IFETCH;
		break;
	case P7_SRR1_MC_IFETCH_UE_TLB_RELOAD:
		mce_err->error_type = MCE_ERROR_TYPE_UE;
		mce_err->u.ue_error_type =
				MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH;
		break;
	}
}

static void mce_get_ierror_p7(struct mce_error_info *mce_err, uint64_t srr1)
{
	mce_get_common_ierror(mce_err, srr1);
	if (P7_SRR1_MC_IFETCH(srr1) == P7_SRR1_MC_IFETCH_SLB_BOTH) {
		mce_err->error_type = MCE_ERROR_TYPE_SLB;
		mce_err->u.slb_error_type = MCE_SLB_ERROR_INDETERMINATE;
	}
}

static void mce_get_derror_p7(struct mce_error_info *mce_err, uint64_t dsisr)
{
	if (dsisr & P7_DSISR_MC_UE) {
		mce_err->error_type = MCE_ERROR_TYPE_UE;
		mce_err->u.ue_error_type = MCE_UE_ERROR_LOAD_STORE;
	} else if (dsisr & P7_DSISR_MC_UE_TABLEWALK) {
		mce_err->error_type = MCE_ERROR_TYPE_UE;
		mce_err->u.ue_error_type =
				MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE;
	} else if (dsisr & P7_DSISR_MC_ERAT_MULTIHIT) {
		mce_err->error_type = MCE_ERROR_TYPE_ERAT;
		mce_err->u.erat_error_type = MCE_ERAT_ERROR_MULTIHIT;
	} else if (dsisr & P7_DSISR_MC_SLB_MULTIHIT) {
		mce_err->error_type = MCE_ERROR_TYPE_SLB;
		mce_err->u.slb_error_type = MCE_SLB_ERROR_MULTIHIT;
	} else if (dsisr & P7_DSISR_MC_SLB_PARITY_MFSLB) {
		mce_err->error_type = MCE_ERROR_TYPE_SLB;
		mce_err->u.slb_error_type = MCE_SLB_ERROR_PARITY;
	} else if (dsisr & P7_DSISR_MC_TLB_MULTIHIT_MFTLB) {
		mce_err->error_type = MCE_ERROR_TYPE_TLB;
		mce_err->u.tlb_error_type = MCE_TLB_ERROR_MULTIHIT;
	} else if (dsisr & P7_DSISR_MC_SLB_MULTIHIT_PARITY) {
		mce_err->error_type = MCE_ERROR_TYPE_SLB;
		mce_err->u.slb_error_type = MCE_SLB_ERROR_INDETERMINATE;
	}
}

static long mce_handle_ue_error(struct pt_regs *regs)
{
	long handled = 0;

	/*
	 * On specific SCOM read via MMIO we may get a machine check
	 * exception with SRR0 pointing inside opal. If that is the
	 * case OPAL may have recovery address to re-read SCOM data in
	 * different way and hence we can recover from this MC.
	 */

	if (ppc_md.mce_check_early_recovery) {
		if (ppc_md.mce_check_early_recovery(regs))
			handled = 1;
	}
	return handled;
}

long __machine_check_early_realmode_p7(struct pt_regs *regs)
{
	uint64_t srr1, nip, addr;
	long handled = 1;
	struct mce_error_info mce_error_info = { 0 };

	srr1 = regs->msr;
	nip = regs->nip;

	/*
	 * Handle memory errors depending whether this was a load/store or
	 * ifetch exception. Also, populate the mce error_type and
	 * type-specific error_type from either SRR1 or DSISR, depending
	 * whether this was a load/store or ifetch exception
	 */
	if (P7_SRR1_MC_LOADSTORE(srr1)) {
		handled = mce_handle_derror_p7(regs->dsisr);
		mce_get_derror_p7(&mce_error_info, regs->dsisr);
		addr = regs->dar;
	} else {
		handled = mce_handle_ierror_p7(srr1);
		mce_get_ierror_p7(&mce_error_info, srr1);
		addr = regs->nip;
	}

	/* Handle UE error. */
	if (mce_error_info.error_type == MCE_ERROR_TYPE_UE)
		handled = mce_handle_ue_error(regs);

	save_mce_event(regs, handled, &mce_error_info, nip, addr);
	return handled;
}

static void mce_get_ierror_p8(struct mce_error_info *mce_err, uint64_t srr1)
{
	mce_get_common_ierror(mce_err, srr1);
	if (P7_SRR1_MC_IFETCH(srr1) == P8_SRR1_MC_IFETCH_ERAT_MULTIHIT) {
		mce_err->error_type = MCE_ERROR_TYPE_ERAT;
		mce_err->u.erat_error_type = MCE_ERAT_ERROR_MULTIHIT;
	}
}

static void mce_get_derror_p8(struct mce_error_info *mce_err, uint64_t dsisr)
{
	mce_get_derror_p7(mce_err, dsisr);
	if (dsisr & P8_DSISR_MC_ERAT_MULTIHIT_SEC) {
		mce_err->error_type = MCE_ERROR_TYPE_ERAT;
		mce_err->u.erat_error_type = MCE_ERAT_ERROR_MULTIHIT;
	}
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
	uint64_t srr1, nip, addr;
	long handled = 1;
	struct mce_error_info mce_error_info = { 0 };

	srr1 = regs->msr;
	nip = regs->nip;

	if (P7_SRR1_MC_LOADSTORE(srr1)) {
		handled = mce_handle_derror_p8(regs->dsisr);
		mce_get_derror_p8(&mce_error_info, regs->dsisr);
		addr = regs->dar;
	} else {
		handled = mce_handle_ierror_p8(srr1);
		mce_get_ierror_p8(&mce_error_info, srr1);
		addr = regs->nip;
	}

	/* Handle UE error. */
	if (mce_error_info.error_type == MCE_ERROR_TYPE_UE)
		handled = mce_handle_ue_error(regs);

	save_mce_event(regs, handled, &mce_error_info, nip, addr);
	return handled;
}
