// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Machine check exception handling CPU-side for power7 and power8
 *
 * Copyright 2013 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#undef DEBUG
#define pr_fmt(fmt) "mce_power: " fmt

#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/extable.h>
#include <linux/pgtable.h>
#include <asm/mmu.h>
#include <asm/mce.h>
#include <asm/machdep.h>
#include <asm/pte-walk.h>
#include <asm/sstep.h>
#include <asm/exception-64s.h>
#include <asm/extable.h>
#include <asm/inst.h>

/*
 * Convert an address related to an mm to a PFN. NOTE: we are in real
 * mode, we could potentially race with page table updates.
 */
unsigned long addr_to_pfn(struct pt_regs *regs, unsigned long addr)
{
	pte_t *ptep, pte;
	unsigned int shift;
	unsigned long pfn, flags;
	struct mm_struct *mm;

	if (user_mode(regs))
		mm = current->mm;
	else
		mm = &init_mm;

	local_irq_save(flags);
	ptep = __find_linux_pte(mm->pgd, addr, NULL, &shift);
	if (!ptep) {
		pfn = ULONG_MAX;
		goto out;
	}
	pte = READ_ONCE(*ptep);

	if (!pte_present(pte) || pte_special(pte)) {
		pfn = ULONG_MAX;
		goto out;
	}

	if (shift <= PAGE_SHIFT)
		pfn = pte_pfn(pte);
	else {
		unsigned long rpnmask = (1ul << shift) - PAGE_SIZE;
		pfn = pte_pfn(__pte(pte_val(pte) | (addr & rpnmask)));
	}
out:
	local_irq_restore(flags);
	return pfn;
}

static bool mce_in_guest(void)
{
#ifdef CONFIG_KVM_BOOK3S_HANDLER
	/*
	 * If machine check is hit when in guest context or low level KVM
	 * code, avoid looking up any translations or making any attempts
	 * to recover, just record the event and pass to KVM.
	 */
	if (get_paca()->kvm_hstate.in_guest)
		return true;
#endif
	return false;
}

/* flush SLBs and reload */
#ifdef CONFIG_PPC_BOOK3S_64
void flush_and_reload_slb(void)
{
	/* Invalidate all SLBs */
	slb_flush_all_realmode();

	if (early_radix_enabled())
		return;

	/*
	 * This probably shouldn't happen, but it may be possible it's
	 * called in early boot before SLB shadows are allocated.
	 */
	if (!get_slb_shadow())
		return;

	slb_restore_bolted_realmode();
}
#endif

void flush_erat(void)
{
#ifdef CONFIG_PPC_BOOK3S_64
	if (!early_cpu_has_feature(CPU_FTR_ARCH_300)) {
		flush_and_reload_slb();
		return;
	}
#endif
	asm volatile(PPC_ISA_3_0_INVALIDATE_ERAT : : :"memory");
}

#define MCE_FLUSH_SLB 1
#define MCE_FLUSH_TLB 2
#define MCE_FLUSH_ERAT 3

static int mce_flush(int what)
{
#ifdef CONFIG_PPC_BOOK3S_64
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
		tlbiel_all();
		return 1;
	}

	return 0;
}

#define SRR1_MC_LOADSTORE(srr1)	((srr1) & PPC_BIT(42))

struct mce_ierror_table {
	unsigned long srr1_mask;
	unsigned long srr1_value;
	bool nip_valid; /* nip is a valid indicator of faulting address */
	unsigned int error_type;
	unsigned int error_subtype;
	unsigned int error_class;
	unsigned int initiator;
	unsigned int severity;
	bool sync_error;
};

static const struct mce_ierror_table mce_p7_ierror_table[] = {
{ 0x00000000001c0000, 0x0000000000040000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000001c0000, 0x0000000000080000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_PARITY, MCE_ECLASS_HARD_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000001c0000, 0x00000000000c0000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000001c0000, 0x0000000000100000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_INDETERMINATE, /* BOTH */
  MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000001c0000, 0x0000000000140000, true,
  MCE_ERROR_TYPE_TLB, MCE_TLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000001c0000, 0x0000000000180000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000001c0000, 0x00000000001c0000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0, 0, 0, 0, 0, 0, 0 } };

static const struct mce_ierror_table mce_p8_ierror_table[] = {
{ 0x00000000081c0000, 0x0000000000040000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000000080000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_PARITY, MCE_ECLASS_HARD_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x00000000000c0000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x0000000000100000, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x0000000000140000, true,
  MCE_ERROR_TYPE_TLB, MCE_TLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x0000000000180000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x00000000001c0000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000008000000, true,
  MCE_ERROR_TYPE_LINK, MCE_LINK_ERROR_IFETCH_TIMEOUT, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000008040000, true,
  MCE_ERROR_TYPE_LINK,MCE_LINK_ERROR_PAGE_TABLE_WALK_IFETCH_TIMEOUT,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0, 0, 0, 0, 0, 0, 0 } };

static const struct mce_ierror_table mce_p9_ierror_table[] = {
{ 0x00000000081c0000, 0x0000000000040000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000000080000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_PARITY, MCE_ECLASS_HARD_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x00000000000c0000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x0000000000100000, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x0000000000140000, true,
  MCE_ERROR_TYPE_TLB, MCE_TLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x0000000000180000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x00000000001c0000, true,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_IFETCH_FOREIGN, MCE_ECLASS_SOFTWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000008000000, true,
  MCE_ERROR_TYPE_LINK, MCE_LINK_ERROR_IFETCH_TIMEOUT, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000008040000, true,
  MCE_ERROR_TYPE_LINK,MCE_LINK_ERROR_PAGE_TABLE_WALK_IFETCH_TIMEOUT,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x00000000080c0000, true,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_IFETCH, MCE_ECLASS_SOFTWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000008100000, true,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH, MCE_ECLASS_SOFTWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000008140000, false,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_STORE, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_FATAL, false }, /* ASYNC is fatal */
{ 0x00000000081c0000, 0x0000000008180000, false,
  MCE_ERROR_TYPE_LINK,MCE_LINK_ERROR_STORE_TIMEOUT,
  MCE_INITIATOR_CPU,  MCE_SEV_FATAL, false }, /* ASYNC is fatal */
{ 0x00000000081c0000, 0x00000000081c0000, true, MCE_ECLASS_HARDWARE,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH_FOREIGN,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0, 0, 0, 0, 0, 0, 0 } };

static const struct mce_ierror_table mce_p10_ierror_table[] = {
{ 0x00000000081c0000, 0x0000000000040000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_IFETCH, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000000080000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_PARITY, MCE_ECLASS_HARD_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x00000000000c0000, true,
  MCE_ERROR_TYPE_SLB, MCE_SLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x0000000000100000, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x0000000000140000, true,
  MCE_ERROR_TYPE_TLB, MCE_TLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x0000000000180000, true,
  MCE_ERROR_TYPE_UE,  MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x00000000001c0000, true,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_IFETCH_FOREIGN, MCE_ECLASS_SOFTWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000008080000, true,
  MCE_ERROR_TYPE_USER,MCE_USER_ERROR_SCV, MCE_ECLASS_SOFTWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_WARNING, true },
{ 0x00000000081c0000, 0x00000000080c0000, true,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_IFETCH, MCE_ECLASS_SOFTWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000008100000, true,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH, MCE_ECLASS_SOFTWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0x00000000081c0000, 0x0000000008140000, false,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_STORE, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,  MCE_SEV_FATAL, false }, /* ASYNC is fatal */
{ 0x00000000081c0000, 0x00000000081c0000, true, MCE_ECLASS_HARDWARE,
  MCE_ERROR_TYPE_RA,  MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH_FOREIGN,
  MCE_INITIATOR_CPU,  MCE_SEV_SEVERE, true },
{ 0, 0, 0, 0, 0, 0, 0 } };

struct mce_derror_table {
	unsigned long dsisr_value;
	bool dar_valid; /* dar is a valid indicator of faulting address */
	unsigned int error_type;
	unsigned int error_subtype;
	unsigned int error_class;
	unsigned int initiator;
	unsigned int severity;
	bool sync_error;
};

static const struct mce_derror_table mce_p7_derror_table[] = {
{ 0x00008000, false,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_LOAD_STORE, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00004000, true,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000800, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000400, true,
  MCE_ERROR_TYPE_TLB,  MCE_TLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000080, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000100, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_PARITY, MCE_ECLASS_HARD_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000040, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_INDETERMINATE, /* BOTH */
  MCE_ECLASS_HARD_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0, false, 0, 0, 0, 0, 0 } };

static const struct mce_derror_table mce_p8_derror_table[] = {
{ 0x00008000, false,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_LOAD_STORE, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00004000, true,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00002000, true,
  MCE_ERROR_TYPE_LINK, MCE_LINK_ERROR_LOAD_TIMEOUT, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00001000, true,
  MCE_ERROR_TYPE_LINK, MCE_LINK_ERROR_PAGE_TABLE_WALK_LOAD_STORE_TIMEOUT,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000800, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000400, true,
  MCE_ERROR_TYPE_TLB,  MCE_TLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000200, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT, /* SECONDARY ERAT */
  MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000080, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_MULTIHIT,	/* Before PARITY */
  MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000100, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_PARITY, MCE_ECLASS_HARD_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0, false, 0, 0, 0, 0, 0 } };

static const struct mce_derror_table mce_p9_derror_table[] = {
{ 0x00008000, false,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_LOAD_STORE, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00004000, true,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00002000, true,
  MCE_ERROR_TYPE_LINK, MCE_LINK_ERROR_LOAD_TIMEOUT, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00001000, true,
  MCE_ERROR_TYPE_LINK, MCE_LINK_ERROR_PAGE_TABLE_WALK_LOAD_STORE_TIMEOUT,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000800, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000400, true,
  MCE_ERROR_TYPE_TLB,  MCE_TLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000200, false,
  MCE_ERROR_TYPE_USER, MCE_USER_ERROR_TLBIE, MCE_ECLASS_SOFTWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000080, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_MULTIHIT,	/* Before PARITY */
  MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000100, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_PARITY, MCE_ECLASS_HARD_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000040, true,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_LOAD, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000020, false,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000010, false,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE_FOREIGN,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000008, false,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_LOAD_STORE_FOREIGN, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0, false, 0, 0, 0, 0, 0 } };

static const struct mce_derror_table mce_p10_derror_table[] = {
{ 0x00008000, false,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_LOAD_STORE, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00004000, true,
  MCE_ERROR_TYPE_UE,   MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000800, true,
  MCE_ERROR_TYPE_ERAT, MCE_ERAT_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000400, true,
  MCE_ERROR_TYPE_TLB,  MCE_TLB_ERROR_MULTIHIT, MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000200, false,
  MCE_ERROR_TYPE_USER, MCE_USER_ERROR_TLBIE, MCE_ECLASS_SOFTWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000080, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_MULTIHIT,	/* Before PARITY */
  MCE_ECLASS_SOFT_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_WARNING, true },
{ 0x00000100, true,
  MCE_ERROR_TYPE_SLB,  MCE_SLB_ERROR_PARITY, MCE_ECLASS_HARD_INDETERMINATE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000040, true,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_LOAD, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000020, false,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000010, false,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE_FOREIGN,
  MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0x00000008, false,
  MCE_ERROR_TYPE_RA,   MCE_RA_ERROR_LOAD_STORE_FOREIGN, MCE_ECLASS_HARDWARE,
  MCE_INITIATOR_CPU,   MCE_SEV_SEVERE, true },
{ 0, false, 0, 0, 0, 0, 0 } };

static int mce_find_instr_ea_and_phys(struct pt_regs *regs, uint64_t *addr,
					uint64_t *phys_addr)
{
	/*
	 * Carefully look at the NIP to determine
	 * the instruction to analyse. Reading the NIP
	 * in real-mode is tricky and can lead to recursive
	 * faults
	 */
	struct ppc_inst instr;
	unsigned long pfn, instr_addr;
	struct instruction_op op;
	struct pt_regs tmp = *regs;

	pfn = addr_to_pfn(regs, regs->nip);
	if (pfn != ULONG_MAX) {
		instr_addr = (pfn << PAGE_SHIFT) + (regs->nip & ~PAGE_MASK);
		instr = ppc_inst_read((u32 *)instr_addr);
		if (!analyse_instr(&op, &tmp, instr)) {
			pfn = addr_to_pfn(regs, op.ea);
			*addr = op.ea;
			*phys_addr = (pfn << PAGE_SHIFT);
			return 0;
		}
		/*
		 * analyse_instr() might fail if the instruction
		 * is not a load/store, although this is unexpected
		 * for load/store errors or if we got the NIP
		 * wrong
		 */
	}
	*addr = 0;
	return -1;
}

static int mce_handle_ierror(struct pt_regs *regs, unsigned long srr1,
		const struct mce_ierror_table table[],
		struct mce_error_info *mce_err, uint64_t *addr,
		uint64_t *phys_addr)
{
	int handled = 0;
	int i;

	*addr = 0;

	for (i = 0; table[i].srr1_mask; i++) {
		if ((srr1 & table[i].srr1_mask) != table[i].srr1_value)
			continue;

		if (!mce_in_guest()) {
			/* attempt to correct the error */
			switch (table[i].error_type) {
			case MCE_ERROR_TYPE_SLB:
				if (local_paca->in_mce == 1)
					slb_save_contents(local_paca->mce_faulty_slbs);
				handled = mce_flush(MCE_FLUSH_SLB);
				break;
			case MCE_ERROR_TYPE_ERAT:
				handled = mce_flush(MCE_FLUSH_ERAT);
				break;
			case MCE_ERROR_TYPE_TLB:
				handled = mce_flush(MCE_FLUSH_TLB);
				break;
			}
		}

		/* now fill in mce_error_info */
		mce_err->error_type = table[i].error_type;
		mce_err->error_class = table[i].error_class;
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
		mce_err->sync_error = table[i].sync_error;
		mce_err->severity = table[i].severity;
		mce_err->initiator = table[i].initiator;
		if (table[i].nip_valid && !mce_in_guest()) {
			*addr = regs->nip;
			if (mce_err->sync_error &&
				table[i].error_type == MCE_ERROR_TYPE_UE) {
				unsigned long pfn;

				if (get_paca()->in_mce < MAX_MCE_DEPTH) {
					pfn = addr_to_pfn(regs, regs->nip);
					if (pfn != ULONG_MAX) {
						*phys_addr =
							(pfn << PAGE_SHIFT);
					}
				}
			}
		}
		return handled;
	}

	mce_err->error_type = MCE_ERROR_TYPE_UNKNOWN;
	mce_err->error_class = MCE_ECLASS_UNKNOWN;
	mce_err->severity = MCE_SEV_SEVERE;
	mce_err->initiator = MCE_INITIATOR_CPU;
	mce_err->sync_error = true;

	return 0;
}

static int mce_handle_derror(struct pt_regs *regs,
		const struct mce_derror_table table[],
		struct mce_error_info *mce_err, uint64_t *addr,
		uint64_t *phys_addr)
{
	uint64_t dsisr = regs->dsisr;
	int handled = 0;
	int found = 0;
	int i;

	*addr = 0;

	for (i = 0; table[i].dsisr_value; i++) {
		if (!(dsisr & table[i].dsisr_value))
			continue;

		if (!mce_in_guest()) {
			/* attempt to correct the error */
			switch (table[i].error_type) {
			case MCE_ERROR_TYPE_SLB:
				if (local_paca->in_mce == 1)
					slb_save_contents(local_paca->mce_faulty_slbs);
				if (mce_flush(MCE_FLUSH_SLB))
					handled = 1;
				break;
			case MCE_ERROR_TYPE_ERAT:
				if (mce_flush(MCE_FLUSH_ERAT))
					handled = 1;
				break;
			case MCE_ERROR_TYPE_TLB:
				if (mce_flush(MCE_FLUSH_TLB))
					handled = 1;
				break;
			}
		}

		/*
		 * Attempt to handle multiple conditions, but only return
		 * one. Ensure uncorrectable errors are first in the table
		 * to match.
		 */
		if (found)
			continue;

		/* now fill in mce_error_info */
		mce_err->error_type = table[i].error_type;
		mce_err->error_class = table[i].error_class;
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
		mce_err->sync_error = table[i].sync_error;
		mce_err->severity = table[i].severity;
		mce_err->initiator = table[i].initiator;
		if (table[i].dar_valid)
			*addr = regs->dar;
		else if (mce_err->sync_error && !mce_in_guest() &&
				table[i].error_type == MCE_ERROR_TYPE_UE) {
			/*
			 * We do a maximum of 4 nested MCE calls, see
			 * kernel/exception-64s.h
			 */
			if (get_paca()->in_mce < MAX_MCE_DEPTH)
				mce_find_instr_ea_and_phys(regs, addr,
							   phys_addr);
		}
		found = 1;
	}

	if (found)
		return handled;

	mce_err->error_type = MCE_ERROR_TYPE_UNKNOWN;
	mce_err->error_class = MCE_ECLASS_UNKNOWN;
	mce_err->severity = MCE_SEV_SEVERE;
	mce_err->initiator = MCE_INITIATOR_CPU;
	mce_err->sync_error = true;

	return 0;
}

static long mce_handle_ue_error(struct pt_regs *regs,
				struct mce_error_info *mce_err)
{
	if (mce_in_guest())
		return 0;

	mce_common_process_ue(regs, mce_err);
	if (mce_err->ignore_event)
		return 1;

	/*
	 * On specific SCOM read via MMIO we may get a machine check
	 * exception with SRR0 pointing inside opal. If that is the
	 * case OPAL may have recovery address to re-read SCOM data in
	 * different way and hence we can recover from this MC.
	 */

	if (ppc_md.mce_check_early_recovery) {
		if (ppc_md.mce_check_early_recovery(regs))
			return 1;
	}

	return 0;
}

static long mce_handle_error(struct pt_regs *regs,
		unsigned long srr1,
		const struct mce_derror_table dtable[],
		const struct mce_ierror_table itable[])
{
	struct mce_error_info mce_err = { 0 };
	uint64_t addr, phys_addr = ULONG_MAX;
	long handled;

	if (SRR1_MC_LOADSTORE(srr1))
		handled = mce_handle_derror(regs, dtable, &mce_err, &addr,
				&phys_addr);
	else
		handled = mce_handle_ierror(regs, srr1, itable, &mce_err, &addr,
				&phys_addr);

	if (!handled && mce_err.error_type == MCE_ERROR_TYPE_UE)
		handled = mce_handle_ue_error(regs, &mce_err);

	save_mce_event(regs, handled, &mce_err, regs->nip, addr, phys_addr);

	return handled;
}

long __machine_check_early_realmode_p7(struct pt_regs *regs)
{
	/* P7 DD1 leaves top bits of DSISR undefined */
	regs->dsisr &= 0x0000ffff;

	return mce_handle_error(regs, regs->msr,
			mce_p7_derror_table, mce_p7_ierror_table);
}

long __machine_check_early_realmode_p8(struct pt_regs *regs)
{
	return mce_handle_error(regs, regs->msr,
			mce_p8_derror_table, mce_p8_ierror_table);
}

long __machine_check_early_realmode_p9(struct pt_regs *regs)
{
	unsigned long srr1 = regs->msr;

	/*
	 * On POWER9 DD2.1 and below, it's possible to get a machine check
	 * caused by a paste instruction where only DSISR bit 25 is set. This
	 * will result in the MCE handler seeing an unknown event and the kernel
	 * crashing. An MCE that occurs like this is spurious, so we don't need
	 * to do anything in terms of servicing it. If there is something that
	 * needs to be serviced, the CPU will raise the MCE again with the
	 * correct DSISR so that it can be serviced properly. So detect this
	 * case and mark it as handled.
	 */
	if (SRR1_MC_LOADSTORE(regs->msr) && regs->dsisr == 0x02000000)
		return 1;

	/*
	 * Async machine check due to bad real address from store or foreign
	 * link time out comes with the load/store bit (PPC bit 42) set in
	 * SRR1, but the cause comes in SRR1 not DSISR. Clear bit 42 so we're
	 * directed to the ierror table so it will find the cause (which
	 * describes it correctly as a store error).
	 */
	if (SRR1_MC_LOADSTORE(srr1) &&
			((srr1 & 0x081c0000) == 0x08140000 ||
			 (srr1 & 0x081c0000) == 0x08180000)) {
		srr1 &= ~PPC_BIT(42);
	}

	return mce_handle_error(regs, srr1,
			mce_p9_derror_table, mce_p9_ierror_table);
}

long __machine_check_early_realmode_p10(struct pt_regs *regs)
{
	unsigned long srr1 = regs->msr;

	/*
	 * Async machine check due to bad real address from store comes with
	 * the load/store bit (PPC bit 42) set in SRR1, but the cause comes in
	 * SRR1 not DSISR. Clear bit 42 so we're directed to the ierror table
	 * so it will find the cause (which describes it correctly as a store
	 * error).
	 */
	if (SRR1_MC_LOADSTORE(srr1) &&
			(srr1 & 0x081c0000) == 0x08140000) {
		srr1 &= ~PPC_BIT(42);
	}

	return mce_handle_error(regs, srr1,
			mce_p10_derror_table, mce_p10_ierror_table);
}
