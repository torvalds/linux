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
 * Generic routines to flush TLB on POWER processors. These routines
 * are used as flush_tlb hook in the cpu_spec.
 *
 * action => TLB_INVAL_SCOPE_GLOBAL:  Invalidate all TLBs.
 *	     TLB_INVAL_SCOPE_LPID: Invalidate TLB for current LPID.
 */
void __flush_tlb_power7(unsigned int action)
{
	flush_tlb_206(POWER7_TLB_SETS, action);
}

void __flush_tlb_power8(unsigned int action)
{
	flush_tlb_206(POWER8_TLB_SETS, action);
}

void __flush_tlb_power9(unsigned int action)
{
	if (radix_enabled())
		flush_tlb_206(POWER9_TLB_SETS_RADIX, action);

	flush_tlb_206(POWER9_TLB_SETS_HASH, action);
}


/* flush SLBs and reload */
#ifdef CONFIG_PPC_STD_MMU_64
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
#endif

static void flush_erat(void)
{
	asm volatile(PPC_INVALIDATE_ERAT : : :"memory");
}

#define MCE_FLUSH_SLB 1
#define MCE_FLUSH_TLB 2
#define MCE_FLUSH_ERAT 3

static int mce_flush(int what)
{
#ifdef CONFIG_PPC_STD_MMU_64
	if (what == MCE_FLUSH_SLB) {
		flush_and_reload_slb();
		return 1;
	}
#endif
	if (what == MCE_FLUSH_ERAT) {
		flush_erat();
		return 1;
	}
	if (what == MCE_FLUSH_TLB) {
		if (cur_cpu_spec && cur_cpu_spec->flush_tlb) {
			cur_cpu_spec->flush_tlb(TLB_INVAL_SCOPE_GLOBAL);
			return 1;
		}
	}

	return 0;
}

static int mce_handle_flush_derrors(uint64_t dsisr, uint64_t slb, uint64_t tlb, uint64_t erat)
{
	if ((dsisr & slb) && mce_flush(MCE_FLUSH_SLB))
		dsisr &= ~slb;
	if ((dsisr & erat) && mce_flush(MCE_FLUSH_ERAT))
		dsisr &= ~erat;
	if ((dsisr & tlb) && mce_flush(MCE_FLUSH_TLB))
		dsisr &= ~tlb;
	/* Any other errors we don't understand? */
	if (dsisr)
		return 0;
	return 1;
}


/*
 * Machine Check bits on power7 and power8
 */
#define P7_SRR1_MC_LOADSTORE(srr1)	((srr1) & PPC_BIT(42)) /* P8 too */

/* SRR1 bits for machine check (On Power7 and Power8) */
#define P7_SRR1_MC_IFETCH(srr1)	((srr1) & PPC_BITMASK(43, 45)) /* P8 too */

#define P7_SRR1_MC_IFETCH_UE		(0x1 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_SLB_PARITY	(0x2 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_SLB_MULTIHIT	(0x3 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_SLB_BOTH	(0x4 << PPC_BITLSHIFT(45))
#define P7_SRR1_MC_IFETCH_TLB_MULTIHIT	(0x5 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_UE_TLB_RELOAD	(0x6 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_UE_IFU_INTERNAL	(0x7 << PPC_BITLSHIFT(45))

/* SRR1 bits for machine check (On Power8) */
#define P8_SRR1_MC_IFETCH_ERAT_MULTIHIT	(0x4 << PPC_BITLSHIFT(45))

/* DSISR bits for machine check (On Power7 and Power8) */
#define P7_DSISR_MC_UE			(PPC_BIT(48))	/* P8 too */
#define P7_DSISR_MC_UE_TABLEWALK	(PPC_BIT(49))	/* P8 too */
#define P7_DSISR_MC_ERAT_MULTIHIT	(PPC_BIT(52))	/* P8 too */
#define P7_DSISR_MC_TLB_MULTIHIT_MFTLB	(PPC_BIT(53))	/* P8 too */
#define P7_DSISR_MC_SLB_PARITY_MFSLB	(PPC_BIT(55))	/* P8 too */
#define P7_DSISR_MC_SLB_MULTIHIT	(PPC_BIT(56))	/* P8 too */
#define P7_DSISR_MC_SLB_MULTIHIT_PARITY	(PPC_BIT(57))	/* P8 too */

/*
 * DSISR bits for machine check (Power8) in addition to above.
 * Secondary DERAT Multihit
 */
#define P8_DSISR_MC_ERAT_MULTIHIT_SEC	(PPC_BIT(54))

/* SLB error bits */
#define P7_DSISR_MC_SLB_ERRORS		(P7_DSISR_MC_ERAT_MULTIHIT | \
					 P7_DSISR_MC_SLB_PARITY_MFSLB | \
					 P7_DSISR_MC_SLB_MULTIHIT | \
					 P7_DSISR_MC_SLB_MULTIHIT_PARITY)

#define P8_DSISR_MC_SLB_ERRORS		(P7_DSISR_MC_SLB_ERRORS | \
					 P8_DSISR_MC_ERAT_MULTIHIT_SEC)

/*
 * Machine Check bits on power9
 */
#define P9_SRR1_MC_LOADSTORE(srr1)	(((srr1) >> PPC_BITLSHIFT(42)) & 1)

#define P9_SRR1_MC_IFETCH(srr1)	(	\
	PPC_BITEXTRACT(srr1, 45, 0) |	\
	PPC_BITEXTRACT(srr1, 44, 1) |	\
	PPC_BITEXTRACT(srr1, 43, 2) |	\
	PPC_BITEXTRACT(srr1, 36, 3) )

/* 0 is reserved */
#define P9_SRR1_MC_IFETCH_UE				1
#define P9_SRR1_MC_IFETCH_SLB_PARITY			2
#define P9_SRR1_MC_IFETCH_SLB_MULTIHIT			3
#define P9_SRR1_MC_IFETCH_ERAT_MULTIHIT			4
#define P9_SRR1_MC_IFETCH_TLB_MULTIHIT			5
#define P9_SRR1_MC_IFETCH_UE_TLB_RELOAD			6
/* 7 is reserved */
#define P9_SRR1_MC_IFETCH_LINK_TIMEOUT			8
#define P9_SRR1_MC_IFETCH_LINK_TABLEWALK_TIMEOUT	9
/* 10 ? */
#define P9_SRR1_MC_IFETCH_RA			11
#define P9_SRR1_MC_IFETCH_RA_TABLEWALK		12
#define P9_SRR1_MC_IFETCH_RA_ASYNC_STORE		13
#define P9_SRR1_MC_IFETCH_LINK_ASYNC_STORE_TIMEOUT	14
#define P9_SRR1_MC_IFETCH_RA_TABLEWALK_FOREIGN	15

/* DSISR bits for machine check (On Power9) */
#define P9_DSISR_MC_UE					(PPC_BIT(48))
#define P9_DSISR_MC_UE_TABLEWALK			(PPC_BIT(49))
#define P9_DSISR_MC_LINK_LOAD_TIMEOUT			(PPC_BIT(50))
#define P9_DSISR_MC_LINK_TABLEWALK_TIMEOUT		(PPC_BIT(51))
#define P9_DSISR_MC_ERAT_MULTIHIT			(PPC_BIT(52))
#define P9_DSISR_MC_TLB_MULTIHIT_MFTLB			(PPC_BIT(53))
#define P9_DSISR_MC_USER_TLBIE				(PPC_BIT(54))
#define P9_DSISR_MC_SLB_PARITY_MFSLB			(PPC_BIT(55))
#define P9_DSISR_MC_SLB_MULTIHIT_MFSLB			(PPC_BIT(56))
#define P9_DSISR_MC_RA_LOAD				(PPC_BIT(57))
#define P9_DSISR_MC_RA_TABLEWALK			(PPC_BIT(58))
#define P9_DSISR_MC_RA_TABLEWALK_FOREIGN		(PPC_BIT(59))
#define P9_DSISR_MC_RA_FOREIGN				(PPC_BIT(60))

/* SLB error bits */
#define P9_DSISR_MC_SLB_ERRORS		(P9_DSISR_MC_ERAT_MULTIHIT | \
					 P9_DSISR_MC_SLB_PARITY_MFSLB | \
					 P9_DSISR_MC_SLB_MULTIHIT_MFSLB)

struct mce_ierror_table {
	unsigned long srr1_mask;
	unsigned long srr1_value;
	bool nip_valid; /* nip is a valid indicator of faulting address */
	unsigned int error_type;
	unsigned int error_subtype;
	unsigned int initiator;
	unsigned int severity;
};

static const struct mce_ierror_table mce_p7_ierror_table[] = {
{ 0x00000000001c0000, 0x0000000000040000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x0000000000080000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_PARITY,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x00000000000c0000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x0000000000100000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_INDETERMINATE, /* BOTH */
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x0000000000140000, true,
  MCE_ERROR_TYPE_TLB, MCE_TLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x0000000000180000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x00000000001c0000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0, 0, 0, 0, 0, 0 } };

static const struct mce_ierror_table mce_p8_ierror_table[] = {
{ 0x00000000001c0000, 0x0000000000040000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x0000000000080000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_PARITY,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x00000000000c0000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x0000000000100000, true,
  MCE_ERROR_TYPE_ERAT,MCE_ERAT_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x0000000000140000, true,
  MCE_ERROR_TYPE_TLB, MCE_TLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x0000000000180000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000001c0000, 0x00000000001c0000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0, 0, 0, 0, 0, 0 } };

static const struct mce_ierror_table mce_p9_ierror_table[] = {
{ 0x00000000081c0000, 0x0000000000040000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000000080000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_PARITY,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x00000000000c0000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000000100000, true,
  MCE_ERROR_TYPE_ERAT,MCE_ERAT_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000000140000, true,
  MCE_ERROR_TYPE_TLB, MCE_TLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000000180000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000008000000, true,
  MCE_ERROR_TYPE_LINK,MCE_LINK_ERROR_IFETCH_TIMEOUT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000008040000, true,
  MCE_ERROR_TYPE_LINK,MCE_LINK_ERROR_PAGE_TABLE_WALK_IFETCH_TIMEOUT,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x00000000080c0000, true,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000008100000, true,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0x00000000081c0000, 0x0000000008140000, false,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_STORE,
  MCE_INITIATOR_CPU,  MCE_SEV_FATAL, }, /* ASYNC is fatal */
{ 0x00000000081c0000, 0x0000000008180000, false,
  MCE_ERROR_TYPE_LINK,MCE_LINK_ERROR_STORE_TIMEOUT,
  MCE_INITIATOR_CPU,  MCE_SEV_FATAL, }, /* ASYNC is fatal */
{ 0x00000000081c0000, 0x00000000081c0000, true,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH_FOREIGN,
  MCE_INITIATOR_CPU,  MCE_SEV_ERROR_SYNC, },
{ 0, 0, 0, 0, 0, 0 } };

struct mce_derror_table {
	unsigned long dsisr_value;
	bool dar_valid; /* dar is a valid indicator of faulting address */
	unsigned int error_type;
	unsigned int error_subtype;
	unsigned int initiator;
	unsigned int severity;
};

static const struct mce_derror_table mce_p7_derror_table[] = {
{ 0x00008000, false,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_LOAD_STORE,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00004000, true,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000800, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000400, true,
  MCE_ERROR_TYPE_TLB,  MCE_TLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000100, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_PARITY,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000080, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000040, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_INDETERMINATE, /* BOTH */
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0, false, 0, 0, 0, 0 } };

static const struct mce_derror_table mce_p8_derror_table[] = {
{ 0x00008000, false,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_LOAD_STORE,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00004000, true,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000800, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000400, true,
  MCE_ERROR_TYPE_TLB,  MCE_TLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000200, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT, /* SECONDARY ERAT */
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000100, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_PARITY,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000080, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0, false, 0, 0, 0, 0 } };

static const struct mce_derror_table mce_p9_derror_table[] = {
{ 0x00008000, false,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_LOAD_STORE,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00004000, true,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00002000, true,
  MCE_ERROR_TYPE_LINK, MCE_LINK_ERROR_LOAD_TIMEOUT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00001000, true,
  MCE_ERROR_TYPE_LINK, MCE_LINK_ERROR_PAGE_TABLE_WALK_LOAD_STORE_TIMEOUT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000800, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000400, true,
  MCE_ERROR_TYPE_TLB,  MCE_TLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000200, false,
  MCE_ERROR_TYPE_USER, MCE_USER_ERROR_TLBIE,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000100, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_PARITY,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000080, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_MULTIHIT,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000040, true,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_LOAD,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000020, false,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000010, false,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE_FOREIGN,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0x00000008, false,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_LOAD_STORE_FOREIGN,
  MCE_INITIATOR_CPU,   MCE_SEV_ERROR_SYNC, },
{ 0, false, 0, 0, 0, 0 } };

static void mce_get_ierror(struct pt_regs *regs,
		const struct mce_ierror_table table[],
		struct mce_error_info *mce_err, uint64_t *addr)
{
	uint64_t srr1 = regs->msr;
	int i;

	*addr = 0;

	for (i = 0; table[i].srr1_mask; i++) {
		if ((srr1 & table[i].srr1_mask) != table[i].srr1_value)
			continue;

		mce_err->error_type = table[i].error_type;
		switch (table[i].error_type) {
		case MCE_ERROR_TYPE_UE:
			mce_err->u.ue_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_SLB:
			mce_err->u.slb_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_ERAT:
			mce_err->u.erat_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_TLB:
			mce_err->u.tlb_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_USER:
			mce_err->u.user_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_RA:
			mce_err->u.ra_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_LINK:
			mce_err->u.link_error_type = table[i].error_subtype;
			break;
		}
		mce_err->severity = table[i].severity;
		mce_err->initiator = table[i].initiator;
		if (table[i].nip_valid)
			*addr = regs->nip;
		return;
	}

	mce_err->error_type = MCE_ERROR_TYPE_UNKNOWN;
	mce_err->severity = MCE_SEV_ERROR_SYNC;
	mce_err->initiator = MCE_INITIATOR_CPU;
}

static void mce_get_derror(struct pt_regs *regs,
		const struct mce_derror_table table[],
		struct mce_error_info *mce_err, uint64_t *addr)
{
	uint64_t dsisr = regs->dsisr;
	int i;

	*addr = 0;

	for (i = 0; table[i].dsisr_value; i++) {
		if (!(dsisr & table[i].dsisr_value))
			continue;

		mce_err->error_type = table[i].error_type;
		switch (table[i].error_type) {
		case MCE_ERROR_TYPE_UE:
			mce_err->u.ue_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_SLB:
			mce_err->u.slb_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_ERAT:
			mce_err->u.erat_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_TLB:
			mce_err->u.tlb_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_USER:
			mce_err->u.user_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_RA:
			mce_err->u.ra_error_type = table[i].error_subtype;
			break;
		case MCE_ERROR_TYPE_LINK:
			mce_err->u.link_error_type = table[i].error_subtype;
			break;
		}
		mce_err->severity = table[i].severity;
		mce_err->initiator = table[i].initiator;
		if (table[i].dar_valid)
			*addr = regs->dar;

		return;
	}

	mce_err->error_type = MCE_ERROR_TYPE_UNKNOWN;
	mce_err->severity = MCE_SEV_ERROR_SYNC;
	mce_err->initiator = MCE_INITIATOR_CPU;
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

static long mce_handle_derror_p7(uint64_t dsisr)
{
	return mce_handle_flush_derrors(dsisr,
			P7_DSISR_MC_SLB_ERRORS,
			P7_DSISR_MC_TLB_MULTIHIT_MFTLB,
			0);
}

static long mce_handle_ierror_p7(uint64_t srr1)
{
	switch (P7_SRR1_MC_IFETCH(srr1)) {
	case P7_SRR1_MC_IFETCH_SLB_PARITY:
	case P7_SRR1_MC_IFETCH_SLB_MULTIHIT:
	case P7_SRR1_MC_IFETCH_SLB_BOTH:
		return mce_flush(MCE_FLUSH_SLB);

	case P7_SRR1_MC_IFETCH_TLB_MULTIHIT:
		return mce_flush(MCE_FLUSH_TLB);
	default:
		return 0;
	}
}

long __machine_check_early_realmode_p7(struct pt_regs *regs)
{
	uint64_t srr1, nip, addr;
	long handled = 1;
	struct mce_error_info mce_error_info = { 0 };

	srr1 = regs->msr;
	nip = regs->nip;

	/* P7 DD1 leaves top bits of DSISR undefined */
	regs->dsisr &= 0x0000ffff;

	/*
	 * Handle memory errors depending whether this was a load/store or
	 * ifetch exception. Also, populate the mce error_type and
	 * type-specific error_type from either SRR1 or DSISR, depending
	 * whether this was a load/store or ifetch exception
	 */
	if (P7_SRR1_MC_LOADSTORE(srr1)) {
		handled = mce_handle_derror_p7(regs->dsisr);
		mce_get_derror(regs, mce_p7_derror_table,
				&mce_error_info, &addr);
	} else {
		handled = mce_handle_ierror_p7(srr1);
		mce_get_ierror(regs, mce_p7_ierror_table,
				&mce_error_info, &addr);
	}

	/* Handle UE error. */
	if (mce_error_info.error_type == MCE_ERROR_TYPE_UE)
		handled = mce_handle_ue_error(regs);

	save_mce_event(regs, handled, &mce_error_info, nip, addr);
	return handled;
}

static long mce_handle_ierror_p8(uint64_t srr1)
{
	switch (P7_SRR1_MC_IFETCH(srr1)) {
	case P7_SRR1_MC_IFETCH_SLB_PARITY:
	case P7_SRR1_MC_IFETCH_SLB_MULTIHIT:
	case P8_SRR1_MC_IFETCH_ERAT_MULTIHIT:
		return mce_flush(MCE_FLUSH_SLB);

	case P7_SRR1_MC_IFETCH_TLB_MULTIHIT:
		return mce_flush(MCE_FLUSH_TLB);
	default:
		return 0;
	}
}

static long mce_handle_derror_p8(uint64_t dsisr)
{
	return mce_handle_flush_derrors(dsisr,
			P8_DSISR_MC_SLB_ERRORS,
			P7_DSISR_MC_TLB_MULTIHIT_MFTLB,
			0);
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
		mce_get_derror(regs, mce_p8_derror_table,
				&mce_error_info, &addr);
	} else {
		handled = mce_handle_ierror_p8(srr1);
		mce_get_ierror(regs, mce_p8_ierror_table,
				&mce_error_info, &addr);
	}

	/* Handle UE error. */
	if (mce_error_info.error_type == MCE_ERROR_TYPE_UE)
		handled = mce_handle_ue_error(regs);

	save_mce_event(regs, handled, &mce_error_info, nip, addr);
	return handled;
}

static int mce_handle_derror_p9(struct pt_regs *regs)
{
	uint64_t dsisr = regs->dsisr;

	return mce_handle_flush_derrors(dsisr,
			P9_DSISR_MC_SLB_PARITY_MFSLB |
			P9_DSISR_MC_SLB_MULTIHIT_MFSLB,

			P9_DSISR_MC_TLB_MULTIHIT_MFTLB,

			P9_DSISR_MC_ERAT_MULTIHIT);
}

static int mce_handle_ierror_p9(struct pt_regs *regs)
{
	uint64_t srr1 = regs->msr;

	switch (P9_SRR1_MC_IFETCH(srr1)) {
	case P9_SRR1_MC_IFETCH_SLB_PARITY:
	case P9_SRR1_MC_IFETCH_SLB_MULTIHIT:
		return mce_flush(MCE_FLUSH_SLB);
	case P9_SRR1_MC_IFETCH_TLB_MULTIHIT:
		return mce_flush(MCE_FLUSH_TLB);
	case P9_SRR1_MC_IFETCH_ERAT_MULTIHIT:
		return mce_flush(MCE_FLUSH_ERAT);
	default:
		return 0;
	}
}

long __machine_check_early_realmode_p9(struct pt_regs *regs)
{
	uint64_t nip, addr;
	long handled;
	struct mce_error_info mce_error_info = { 0 };

	nip = regs->nip;

	if (P9_SRR1_MC_LOADSTORE(regs->msr)) {
		handled = mce_handle_derror_p9(regs);
		mce_get_derror(regs, mce_p9_derror_table,
				&mce_error_info, &addr);
	} else {
		handled = mce_handle_ierror_p9(regs);
		mce_get_ierror(regs, mce_p9_ierror_table,
				&mce_error_info, &addr);
	}

	/* Handle UE error. */
	if (mce_error_info.error_type == MCE_ERROR_TYPE_UE)
		handled = mce_handle_ue_error(regs);

	save_mce_event(regs, handled, &mce_error_info, nip, addr);
	return handled;
}
