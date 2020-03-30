// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009. SUSE Linux Products GmbH. All rights reserved.
 *
 * Authors:
 *    Alexander Graf <agraf@suse.de>
 *    Kevin Wolf <mail@kevin-wolf.de>
 *    Paul Mackerras <paulus@samba.org>
 *
 * Description:
 * Functions relating to running KVM on Book 3S processors where
 * we don't have access to hypervisor mode, and we run the guest
 * in problem state (user mode).
 *
 * This file is derived from arch/powerpc/kvm/44x.c,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
 */

#include <linux/kvm_host.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/cacheflush.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu_context.h>
#include <asm/switch_to.h>
#include <asm/firmware.h>
#include <asm/setup.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <asm/asm-prototypes.h>
#include <asm/tm.h>

#include "book3s.h"

#define CREATE_TRACE_POINTS
#include "trace_pr.h"

/* #define EXIT_DEBUG */
/* #define DEBUG_EXT */

static int kvmppc_handle_ext(struct kvm_vcpu *vcpu, unsigned int exit_nr,
			     ulong msr);
#ifdef CONFIG_PPC_BOOK3S_64
static int kvmppc_handle_fac(struct kvm_vcpu *vcpu, ulong fac);
#endif

/* Some compatibility defines */
#ifdef CONFIG_PPC_BOOK3S_32
#define MSR_USER32 MSR_USER
#define MSR_USER64 MSR_USER
#define HW_PAGE_SIZE PAGE_SIZE
#define HPTE_R_M   _PAGE_COHERENT
#endif

static bool kvmppc_is_split_real(struct kvm_vcpu *vcpu)
{
	ulong msr = kvmppc_get_msr(vcpu);
	return (msr & (MSR_IR|MSR_DR)) == MSR_DR;
}

static void kvmppc_fixup_split_real(struct kvm_vcpu *vcpu)
{
	ulong msr = kvmppc_get_msr(vcpu);
	ulong pc = kvmppc_get_pc(vcpu);

	/* We are in DR only split real mode */
	if ((msr & (MSR_IR|MSR_DR)) != MSR_DR)
		return;

	/* We have not fixed up the guest already */
	if (vcpu->arch.hflags & BOOK3S_HFLAG_SPLIT_HACK)
		return;

	/* The code is in fixupable address space */
	if (pc & SPLIT_HACK_MASK)
		return;

	vcpu->arch.hflags |= BOOK3S_HFLAG_SPLIT_HACK;
	kvmppc_set_pc(vcpu, pc | SPLIT_HACK_OFFS);
}

static void kvmppc_unfixup_split_real(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.hflags & BOOK3S_HFLAG_SPLIT_HACK) {
		ulong pc = kvmppc_get_pc(vcpu);
		ulong lr = kvmppc_get_lr(vcpu);
		if ((pc & SPLIT_HACK_MASK) == SPLIT_HACK_OFFS)
			kvmppc_set_pc(vcpu, pc & ~SPLIT_HACK_MASK);
		if ((lr & SPLIT_HACK_MASK) == SPLIT_HACK_OFFS)
			kvmppc_set_lr(vcpu, lr & ~SPLIT_HACK_MASK);
		vcpu->arch.hflags &= ~BOOK3S_HFLAG_SPLIT_HACK;
	}
}

static void kvmppc_inject_interrupt_pr(struct kvm_vcpu *vcpu, int vec, u64 srr1_flags)
{
	unsigned long msr, pc, new_msr, new_pc;

	kvmppc_unfixup_split_real(vcpu);

	msr = kvmppc_get_msr(vcpu);
	pc = kvmppc_get_pc(vcpu);
	new_msr = vcpu->arch.intr_msr;
	new_pc = to_book3s(vcpu)->hior + vec;

#ifdef CONFIG_PPC_BOOK3S_64
	/* If transactional, change to suspend mode on IRQ delivery */
	if (MSR_TM_TRANSACTIONAL(msr))
		new_msr |= MSR_TS_S;
	else
		new_msr |= msr & MSR_TS_MASK;
#endif

	kvmppc_set_srr0(vcpu, pc);
	kvmppc_set_srr1(vcpu, (msr & SRR1_MSR_BITS) | srr1_flags);
	kvmppc_set_pc(vcpu, new_pc);
	kvmppc_set_msr(vcpu, new_msr);
}

static void kvmppc_core_vcpu_load_pr(struct kvm_vcpu *vcpu, int cpu)
{
#ifdef CONFIG_PPC_BOOK3S_64
	struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
	memcpy(svcpu->slb, to_book3s(vcpu)->slb_shadow, sizeof(svcpu->slb));
	svcpu->slb_max = to_book3s(vcpu)->slb_shadow_max;
	svcpu->in_use = 0;
	svcpu_put(svcpu);
#endif

	/* Disable AIL if supported */
	if (cpu_has_feature(CPU_FTR_HVMODE) &&
	    cpu_has_feature(CPU_FTR_ARCH_207S))
		mtspr(SPRN_LPCR, mfspr(SPRN_LPCR) & ~LPCR_AIL);

	vcpu->cpu = smp_processor_id();
#ifdef CONFIG_PPC_BOOK3S_32
	current->thread.kvm_shadow_vcpu = vcpu->arch.shadow_vcpu;
#endif

	if (kvmppc_is_split_real(vcpu))
		kvmppc_fixup_split_real(vcpu);

	kvmppc_restore_tm_pr(vcpu);
}

static void kvmppc_core_vcpu_put_pr(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_PPC_BOOK3S_64
	struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
	if (svcpu->in_use) {
		kvmppc_copy_from_svcpu(vcpu);
	}
	memcpy(to_book3s(vcpu)->slb_shadow, svcpu->slb, sizeof(svcpu->slb));
	to_book3s(vcpu)->slb_shadow_max = svcpu->slb_max;
	svcpu_put(svcpu);
#endif

	if (kvmppc_is_split_real(vcpu))
		kvmppc_unfixup_split_real(vcpu);

	kvmppc_giveup_ext(vcpu, MSR_FP | MSR_VEC | MSR_VSX);
	kvmppc_giveup_fac(vcpu, FSCR_TAR_LG);
	kvmppc_save_tm_pr(vcpu);

	/* Enable AIL if supported */
	if (cpu_has_feature(CPU_FTR_HVMODE) &&
	    cpu_has_feature(CPU_FTR_ARCH_207S))
		mtspr(SPRN_LPCR, mfspr(SPRN_LPCR) | LPCR_AIL_3);

	vcpu->cpu = -1;
}

/* Copy data needed by real-mode code from vcpu to shadow vcpu */
void kvmppc_copy_to_svcpu(struct kvm_vcpu *vcpu)
{
	struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);

	svcpu->gpr[0] = vcpu->arch.regs.gpr[0];
	svcpu->gpr[1] = vcpu->arch.regs.gpr[1];
	svcpu->gpr[2] = vcpu->arch.regs.gpr[2];
	svcpu->gpr[3] = vcpu->arch.regs.gpr[3];
	svcpu->gpr[4] = vcpu->arch.regs.gpr[4];
	svcpu->gpr[5] = vcpu->arch.regs.gpr[5];
	svcpu->gpr[6] = vcpu->arch.regs.gpr[6];
	svcpu->gpr[7] = vcpu->arch.regs.gpr[7];
	svcpu->gpr[8] = vcpu->arch.regs.gpr[8];
	svcpu->gpr[9] = vcpu->arch.regs.gpr[9];
	svcpu->gpr[10] = vcpu->arch.regs.gpr[10];
	svcpu->gpr[11] = vcpu->arch.regs.gpr[11];
	svcpu->gpr[12] = vcpu->arch.regs.gpr[12];
	svcpu->gpr[13] = vcpu->arch.regs.gpr[13];
	svcpu->cr  = vcpu->arch.regs.ccr;
	svcpu->xer = vcpu->arch.regs.xer;
	svcpu->ctr = vcpu->arch.regs.ctr;
	svcpu->lr  = vcpu->arch.regs.link;
	svcpu->pc  = vcpu->arch.regs.nip;
#ifdef CONFIG_PPC_BOOK3S_64
	svcpu->shadow_fscr = vcpu->arch.shadow_fscr;
#endif
	/*
	 * Now also save the current time base value. We use this
	 * to find the guest purr and spurr value.
	 */
	vcpu->arch.entry_tb = get_tb();
	vcpu->arch.entry_vtb = get_vtb();
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		vcpu->arch.entry_ic = mfspr(SPRN_IC);
	svcpu->in_use = true;

	svcpu_put(svcpu);
}

static void kvmppc_recalc_shadow_msr(struct kvm_vcpu *vcpu)
{
	ulong guest_msr = kvmppc_get_msr(vcpu);
	ulong smsr = guest_msr;

	/* Guest MSR values */
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	smsr &= MSR_FE0 | MSR_FE1 | MSR_SF | MSR_SE | MSR_BE | MSR_LE |
		MSR_TM | MSR_TS_MASK;
#else
	smsr &= MSR_FE0 | MSR_FE1 | MSR_SF | MSR_SE | MSR_BE | MSR_LE;
#endif
	/* Process MSR values */
	smsr |= MSR_ME | MSR_RI | MSR_IR | MSR_DR | MSR_PR | MSR_EE;
	/* External providers the guest reserved */
	smsr |= (guest_msr & vcpu->arch.guest_owned_ext);
	/* 64-bit Process MSR values */
#ifdef CONFIG_PPC_BOOK3S_64
	smsr |= MSR_ISF | MSR_HV;
#endif
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	/*
	 * in guest privileged state, we want to fail all TM transactions.
	 * So disable MSR TM bit so that all tbegin. will be able to be
	 * trapped into host.
	 */
	if (!(guest_msr & MSR_PR))
		smsr &= ~MSR_TM;
#endif
	vcpu->arch.shadow_msr = smsr;
}

/* Copy data touched by real-mode code from shadow vcpu back to vcpu */
void kvmppc_copy_from_svcpu(struct kvm_vcpu *vcpu)
{
	struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	ulong old_msr;
#endif

	/*
	 * Maybe we were already preempted and synced the svcpu from
	 * our preempt notifiers. Don't bother touching this svcpu then.
	 */
	if (!svcpu->in_use)
		goto out;

	vcpu->arch.regs.gpr[0] = svcpu->gpr[0];
	vcpu->arch.regs.gpr[1] = svcpu->gpr[1];
	vcpu->arch.regs.gpr[2] = svcpu->gpr[2];
	vcpu->arch.regs.gpr[3] = svcpu->gpr[3];
	vcpu->arch.regs.gpr[4] = svcpu->gpr[4];
	vcpu->arch.regs.gpr[5] = svcpu->gpr[5];
	vcpu->arch.regs.gpr[6] = svcpu->gpr[6];
	vcpu->arch.regs.gpr[7] = svcpu->gpr[7];
	vcpu->arch.regs.gpr[8] = svcpu->gpr[8];
	vcpu->arch.regs.gpr[9] = svcpu->gpr[9];
	vcpu->arch.regs.gpr[10] = svcpu->gpr[10];
	vcpu->arch.regs.gpr[11] = svcpu->gpr[11];
	vcpu->arch.regs.gpr[12] = svcpu->gpr[12];
	vcpu->arch.regs.gpr[13] = svcpu->gpr[13];
	vcpu->arch.regs.ccr  = svcpu->cr;
	vcpu->arch.regs.xer = svcpu->xer;
	vcpu->arch.regs.ctr = svcpu->ctr;
	vcpu->arch.regs.link  = svcpu->lr;
	vcpu->arch.regs.nip  = svcpu->pc;
	vcpu->arch.shadow_srr1 = svcpu->shadow_srr1;
	vcpu->arch.fault_dar   = svcpu->fault_dar;
	vcpu->arch.fault_dsisr = svcpu->fault_dsisr;
	vcpu->arch.last_inst   = svcpu->last_inst;
#ifdef CONFIG_PPC_BOOK3S_64
	vcpu->arch.shadow_fscr = svcpu->shadow_fscr;
#endif
	/*
	 * Update purr and spurr using time base on exit.
	 */
	vcpu->arch.purr += get_tb() - vcpu->arch.entry_tb;
	vcpu->arch.spurr += get_tb() - vcpu->arch.entry_tb;
	to_book3s(vcpu)->vtb += get_vtb() - vcpu->arch.entry_vtb;
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		vcpu->arch.ic += mfspr(SPRN_IC) - vcpu->arch.entry_ic;

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	/*
	 * Unlike other MSR bits, MSR[TS]bits can be changed at guest without
	 * notifying host:
	 *  modified by unprivileged instructions like "tbegin"/"tend"/
	 * "tresume"/"tsuspend" in PR KVM guest.
	 *
	 * It is necessary to sync here to calculate a correct shadow_msr.
	 *
	 * privileged guest's tbegin will be failed at present. So we
	 * only take care of problem state guest.
	 */
	old_msr = kvmppc_get_msr(vcpu);
	if (unlikely((old_msr & MSR_PR) &&
		(vcpu->arch.shadow_srr1 & (MSR_TS_MASK)) !=
				(old_msr & (MSR_TS_MASK)))) {
		old_msr &= ~(MSR_TS_MASK);
		old_msr |= (vcpu->arch.shadow_srr1 & (MSR_TS_MASK));
		kvmppc_set_msr_fast(vcpu, old_msr);
		kvmppc_recalc_shadow_msr(vcpu);
	}
#endif

	svcpu->in_use = false;

out:
	svcpu_put(svcpu);
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
void kvmppc_save_tm_sprs(struct kvm_vcpu *vcpu)
{
	tm_enable();
	vcpu->arch.tfhar = mfspr(SPRN_TFHAR);
	vcpu->arch.texasr = mfspr(SPRN_TEXASR);
	vcpu->arch.tfiar = mfspr(SPRN_TFIAR);
	tm_disable();
}

void kvmppc_restore_tm_sprs(struct kvm_vcpu *vcpu)
{
	tm_enable();
	mtspr(SPRN_TFHAR, vcpu->arch.tfhar);
	mtspr(SPRN_TEXASR, vcpu->arch.texasr);
	mtspr(SPRN_TFIAR, vcpu->arch.tfiar);
	tm_disable();
}

/* loadup math bits which is enabled at kvmppc_get_msr() but not enabled at
 * hardware.
 */
static void kvmppc_handle_lost_math_exts(struct kvm_vcpu *vcpu)
{
	ulong exit_nr;
	ulong ext_diff = (kvmppc_get_msr(vcpu) & ~vcpu->arch.guest_owned_ext) &
		(MSR_FP | MSR_VEC | MSR_VSX);

	if (!ext_diff)
		return;

	if (ext_diff == MSR_FP)
		exit_nr = BOOK3S_INTERRUPT_FP_UNAVAIL;
	else if (ext_diff == MSR_VEC)
		exit_nr = BOOK3S_INTERRUPT_ALTIVEC;
	else
		exit_nr = BOOK3S_INTERRUPT_VSX;

	kvmppc_handle_ext(vcpu, exit_nr, ext_diff);
}

void kvmppc_save_tm_pr(struct kvm_vcpu *vcpu)
{
	if (!(MSR_TM_ACTIVE(kvmppc_get_msr(vcpu)))) {
		kvmppc_save_tm_sprs(vcpu);
		return;
	}

	kvmppc_giveup_fac(vcpu, FSCR_TAR_LG);
	kvmppc_giveup_ext(vcpu, MSR_VSX);

	preempt_disable();
	_kvmppc_save_tm_pr(vcpu, mfmsr());
	preempt_enable();
}

void kvmppc_restore_tm_pr(struct kvm_vcpu *vcpu)
{
	if (!MSR_TM_ACTIVE(kvmppc_get_msr(vcpu))) {
		kvmppc_restore_tm_sprs(vcpu);
		if (kvmppc_get_msr(vcpu) & MSR_TM) {
			kvmppc_handle_lost_math_exts(vcpu);
			if (vcpu->arch.fscr & FSCR_TAR)
				kvmppc_handle_fac(vcpu, FSCR_TAR_LG);
		}
		return;
	}

	preempt_disable();
	_kvmppc_restore_tm_pr(vcpu, kvmppc_get_msr(vcpu));
	preempt_enable();

	if (kvmppc_get_msr(vcpu) & MSR_TM) {
		kvmppc_handle_lost_math_exts(vcpu);
		if (vcpu->arch.fscr & FSCR_TAR)
			kvmppc_handle_fac(vcpu, FSCR_TAR_LG);
	}
}
#endif

static int kvmppc_core_check_requests_pr(struct kvm_vcpu *vcpu)
{
	int r = 1; /* Indicate we want to get back into the guest */

	/* We misuse TLB_FLUSH to indicate that we want to clear
	   all shadow cache entries */
	if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu))
		kvmppc_mmu_pte_flush(vcpu, 0, 0);

	return r;
}

/************* MMU Notifiers *************/
static void do_kvm_unmap_hva(struct kvm *kvm, unsigned long start,
			     unsigned long end)
{
	long i;
	struct kvm_vcpu *vcpu;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;

	slots = kvm_memslots(kvm);
	kvm_for_each_memslot(memslot, slots) {
		unsigned long hva_start, hva_end;
		gfn_t gfn, gfn_end;

		hva_start = max(start, memslot->userspace_addr);
		hva_end = min(end, memslot->userspace_addr +
					(memslot->npages << PAGE_SHIFT));
		if (hva_start >= hva_end)
			continue;
		/*
		 * {gfn(page) | page intersects with [hva_start, hva_end)} =
		 * {gfn, gfn+1, ..., gfn_end-1}.
		 */
		gfn = hva_to_gfn_memslot(hva_start, memslot);
		gfn_end = hva_to_gfn_memslot(hva_end + PAGE_SIZE - 1, memslot);
		kvm_for_each_vcpu(i, vcpu, kvm)
			kvmppc_mmu_pte_pflush(vcpu, gfn << PAGE_SHIFT,
					      gfn_end << PAGE_SHIFT);
	}
}

static int kvm_unmap_hva_range_pr(struct kvm *kvm, unsigned long start,
				  unsigned long end)
{
	do_kvm_unmap_hva(kvm, start, end);

	return 0;
}

static int kvm_age_hva_pr(struct kvm *kvm, unsigned long start,
			  unsigned long end)
{
	/* XXX could be more clever ;) */
	return 0;
}

static int kvm_test_age_hva_pr(struct kvm *kvm, unsigned long hva)
{
	/* XXX could be more clever ;) */
	return 0;
}

static void kvm_set_spte_hva_pr(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	/* The page will get remapped properly on its next fault */
	do_kvm_unmap_hva(kvm, hva, hva + PAGE_SIZE);
}

/*****************************************/

static void kvmppc_set_msr_pr(struct kvm_vcpu *vcpu, u64 msr)
{
	ulong old_msr;

	/* For PAPR guest, make sure MSR reflects guest mode */
	if (vcpu->arch.papr_enabled)
		msr = (msr & ~MSR_HV) | MSR_ME;

#ifdef EXIT_DEBUG
	printk(KERN_INFO "KVM: Set MSR to 0x%llx\n", msr);
#endif

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	/* We should never target guest MSR to TS=10 && PR=0,
	 * since we always fail transaction for guest privilege
	 * state.
	 */
	if (!(msr & MSR_PR) && MSR_TM_TRANSACTIONAL(msr))
		kvmppc_emulate_tabort(vcpu,
			TM_CAUSE_KVM_FAC_UNAV | TM_CAUSE_PERSISTENT);
#endif

	old_msr = kvmppc_get_msr(vcpu);
	msr &= to_book3s(vcpu)->msr_mask;
	kvmppc_set_msr_fast(vcpu, msr);
	kvmppc_recalc_shadow_msr(vcpu);

	if (msr & MSR_POW) {
		if (!vcpu->arch.pending_exceptions) {
			kvm_vcpu_block(vcpu);
			kvm_clear_request(KVM_REQ_UNHALT, vcpu);
			vcpu->stat.halt_wakeup++;

			/* Unset POW bit after we woke up */
			msr &= ~MSR_POW;
			kvmppc_set_msr_fast(vcpu, msr);
		}
	}

	if (kvmppc_is_split_real(vcpu))
		kvmppc_fixup_split_real(vcpu);
	else
		kvmppc_unfixup_split_real(vcpu);

	if ((kvmppc_get_msr(vcpu) & (MSR_PR|MSR_IR|MSR_DR)) !=
		   (old_msr & (MSR_PR|MSR_IR|MSR_DR))) {
		kvmppc_mmu_flush_segments(vcpu);
		kvmppc_mmu_map_segment(vcpu, kvmppc_get_pc(vcpu));

		/* Preload magic page segment when in kernel mode */
		if (!(msr & MSR_PR) && vcpu->arch.magic_page_pa) {
			struct kvm_vcpu_arch *a = &vcpu->arch;

			if (msr & MSR_DR)
				kvmppc_mmu_map_segment(vcpu, a->magic_page_ea);
			else
				kvmppc_mmu_map_segment(vcpu, a->magic_page_pa);
		}
	}

	/*
	 * When switching from 32 to 64-bit, we may have a stale 32-bit
	 * magic page around, we need to flush it. Typically 32-bit magic
	 * page will be instantiated when calling into RTAS. Note: We
	 * assume that such transition only happens while in kernel mode,
	 * ie, we never transition from user 32-bit to kernel 64-bit with
	 * a 32-bit magic page around.
	 */
	if (vcpu->arch.magic_page_pa &&
	    !(old_msr & MSR_PR) && !(old_msr & MSR_SF) && (msr & MSR_SF)) {
		/* going from RTAS to normal kernel code */
		kvmppc_mmu_pte_flush(vcpu, (uint32_t)vcpu->arch.magic_page_pa,
				     ~0xFFFUL);
	}

	/* Preload FPU if it's enabled */
	if (kvmppc_get_msr(vcpu) & MSR_FP)
		kvmppc_handle_ext(vcpu, BOOK3S_INTERRUPT_FP_UNAVAIL, MSR_FP);

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	if (kvmppc_get_msr(vcpu) & MSR_TM)
		kvmppc_handle_lost_math_exts(vcpu);
#endif
}

void kvmppc_set_pvr_pr(struct kvm_vcpu *vcpu, u32 pvr)
{
	u32 host_pvr;

	vcpu->arch.hflags &= ~BOOK3S_HFLAG_SLB;
	vcpu->arch.pvr = pvr;
#ifdef CONFIG_PPC_BOOK3S_64
	if ((pvr >= 0x330000) && (pvr < 0x70330000)) {
		kvmppc_mmu_book3s_64_init(vcpu);
		if (!to_book3s(vcpu)->hior_explicit)
			to_book3s(vcpu)->hior = 0xfff00000;
		to_book3s(vcpu)->msr_mask = 0xffffffffffffffffULL;
		vcpu->arch.cpu_type = KVM_CPU_3S_64;
	} else
#endif
	{
		kvmppc_mmu_book3s_32_init(vcpu);
		if (!to_book3s(vcpu)->hior_explicit)
			to_book3s(vcpu)->hior = 0;
		to_book3s(vcpu)->msr_mask = 0xffffffffULL;
		vcpu->arch.cpu_type = KVM_CPU_3S_32;
	}

	kvmppc_sanity_check(vcpu);

	/* If we are in hypervisor level on 970, we can tell the CPU to
	 * treat DCBZ as 32 bytes store */
	vcpu->arch.hflags &= ~BOOK3S_HFLAG_DCBZ32;
	if (vcpu->arch.mmu.is_dcbz32(vcpu) && (mfmsr() & MSR_HV) &&
	    !strcmp(cur_cpu_spec->platform, "ppc970"))
		vcpu->arch.hflags |= BOOK3S_HFLAG_DCBZ32;

	/* Cell performs badly if MSR_FEx are set. So let's hope nobody
	   really needs them in a VM on Cell and force disable them. */
	if (!strcmp(cur_cpu_spec->platform, "ppc-cell-be"))
		to_book3s(vcpu)->msr_mask &= ~(MSR_FE0 | MSR_FE1);

	/*
	 * If they're asking for POWER6 or later, set the flag
	 * indicating that we can do multiple large page sizes
	 * and 1TB segments.
	 * Also set the flag that indicates that tlbie has the large
	 * page bit in the RB operand instead of the instruction.
	 */
	switch (PVR_VER(pvr)) {
	case PVR_POWER6:
	case PVR_POWER7:
	case PVR_POWER7p:
	case PVR_POWER8:
	case PVR_POWER8E:
	case PVR_POWER8NVL:
	case PVR_POWER9:
		vcpu->arch.hflags |= BOOK3S_HFLAG_MULTI_PGSIZE |
			BOOK3S_HFLAG_NEW_TLBIE;
		break;
	}

#ifdef CONFIG_PPC_BOOK3S_32
	/* 32 bit Book3S always has 32 byte dcbz */
	vcpu->arch.hflags |= BOOK3S_HFLAG_DCBZ32;
#endif

	/* On some CPUs we can execute paired single operations natively */
	asm ( "mfpvr %0" : "=r"(host_pvr));
	switch (host_pvr) {
	case 0x00080200:	/* lonestar 2.0 */
	case 0x00088202:	/* lonestar 2.2 */
	case 0x70000100:	/* gekko 1.0 */
	case 0x00080100:	/* gekko 2.0 */
	case 0x00083203:	/* gekko 2.3a */
	case 0x00083213:	/* gekko 2.3b */
	case 0x00083204:	/* gekko 2.4 */
	case 0x00083214:	/* gekko 2.4e (8SE) - retail HW2 */
	case 0x00087200:	/* broadway */
		vcpu->arch.hflags |= BOOK3S_HFLAG_NATIVE_PS;
		/* Enable HID2.PSE - in case we need it later */
		mtspr(SPRN_HID2_GEKKO, mfspr(SPRN_HID2_GEKKO) | (1 << 29));
	}
}

/* Book3s_32 CPUs always have 32 bytes cache line size, which Linux assumes. To
 * make Book3s_32 Linux work on Book3s_64, we have to make sure we trap dcbz to
 * emulate 32 bytes dcbz length.
 *
 * The Book3s_64 inventors also realized this case and implemented a special bit
 * in the HID5 register, which is a hypervisor ressource. Thus we can't use it.
 *
 * My approach here is to patch the dcbz instruction on executing pages.
 */
static void kvmppc_patch_dcbz(struct kvm_vcpu *vcpu, struct kvmppc_pte *pte)
{
	struct page *hpage;
	u64 hpage_offset;
	u32 *page;
	int i;

	hpage = gfn_to_page(vcpu->kvm, pte->raddr >> PAGE_SHIFT);
	if (is_error_page(hpage))
		return;

	hpage_offset = pte->raddr & ~PAGE_MASK;
	hpage_offset &= ~0xFFFULL;
	hpage_offset /= 4;

	get_page(hpage);
	page = kmap_atomic(hpage);

	/* patch dcbz into reserved instruction, so we trap */
	for (i=hpage_offset; i < hpage_offset + (HW_PAGE_SIZE / 4); i++)
		if ((be32_to_cpu(page[i]) & 0xff0007ff) == INS_DCBZ)
			page[i] &= cpu_to_be32(0xfffffff7);

	kunmap_atomic(page);
	put_page(hpage);
}

static bool kvmppc_visible_gpa(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	ulong mp_pa = vcpu->arch.magic_page_pa;

	if (!(kvmppc_get_msr(vcpu) & MSR_SF))
		mp_pa = (uint32_t)mp_pa;

	gpa &= ~0xFFFULL;
	if (unlikely(mp_pa) && unlikely((mp_pa & KVM_PAM) == (gpa & KVM_PAM))) {
		return true;
	}

	return kvm_is_visible_gfn(vcpu->kvm, gpa >> PAGE_SHIFT);
}

int kvmppc_handle_pagefault(struct kvm_run *run, struct kvm_vcpu *vcpu,
			    ulong eaddr, int vec)
{
	bool data = (vec == BOOK3S_INTERRUPT_DATA_STORAGE);
	bool iswrite = false;
	int r = RESUME_GUEST;
	int relocated;
	int page_found = 0;
	struct kvmppc_pte pte = { 0 };
	bool dr = (kvmppc_get_msr(vcpu) & MSR_DR) ? true : false;
	bool ir = (kvmppc_get_msr(vcpu) & MSR_IR) ? true : false;
	u64 vsid;

	relocated = data ? dr : ir;
	if (data && (vcpu->arch.fault_dsisr & DSISR_ISSTORE))
		iswrite = true;

	/* Resolve real address if translation turned on */
	if (relocated) {
		page_found = vcpu->arch.mmu.xlate(vcpu, eaddr, &pte, data, iswrite);
	} else {
		pte.may_execute = true;
		pte.may_read = true;
		pte.may_write = true;
		pte.raddr = eaddr & KVM_PAM;
		pte.eaddr = eaddr;
		pte.vpage = eaddr >> 12;
		pte.page_size = MMU_PAGE_64K;
		pte.wimg = HPTE_R_M;
	}

	switch (kvmppc_get_msr(vcpu) & (MSR_DR|MSR_IR)) {
	case 0:
		pte.vpage |= ((u64)VSID_REAL << (SID_SHIFT - 12));
		break;
	case MSR_DR:
		if (!data &&
		    (vcpu->arch.hflags & BOOK3S_HFLAG_SPLIT_HACK) &&
		    ((pte.raddr & SPLIT_HACK_MASK) == SPLIT_HACK_OFFS))
			pte.raddr &= ~SPLIT_HACK_MASK;
		/* fall through */
	case MSR_IR:
		vcpu->arch.mmu.esid_to_vsid(vcpu, eaddr >> SID_SHIFT, &vsid);

		if ((kvmppc_get_msr(vcpu) & (MSR_DR|MSR_IR)) == MSR_DR)
			pte.vpage |= ((u64)VSID_REAL_DR << (SID_SHIFT - 12));
		else
			pte.vpage |= ((u64)VSID_REAL_IR << (SID_SHIFT - 12));
		pte.vpage |= vsid;

		if (vsid == -1)
			page_found = -EINVAL;
		break;
	}

	if (vcpu->arch.mmu.is_dcbz32(vcpu) &&
	   (!(vcpu->arch.hflags & BOOK3S_HFLAG_DCBZ32))) {
		/*
		 * If we do the dcbz hack, we have to NX on every execution,
		 * so we can patch the executing code. This renders our guest
		 * NX-less.
		 */
		pte.may_execute = !data;
	}

	if (page_found == -ENOENT || page_found == -EPERM) {
		/* Page not found in guest PTE entries, or protection fault */
		u64 flags;

		if (page_found == -EPERM)
			flags = DSISR_PROTFAULT;
		else
			flags = DSISR_NOHPTE;
		if (data) {
			flags |= vcpu->arch.fault_dsisr & DSISR_ISSTORE;
			kvmppc_core_queue_data_storage(vcpu, eaddr, flags);
		} else {
			kvmppc_core_queue_inst_storage(vcpu, flags);
		}
	} else if (page_found == -EINVAL) {
		/* Page not found in guest SLB */
		kvmppc_set_dar(vcpu, kvmppc_get_fault_dar(vcpu));
		kvmppc_book3s_queue_irqprio(vcpu, vec + 0x80);
	} else if (kvmppc_visible_gpa(vcpu, pte.raddr)) {
		if (data && !(vcpu->arch.fault_dsisr & DSISR_NOHPTE)) {
			/*
			 * There is already a host HPTE there, presumably
			 * a read-only one for a page the guest thinks
			 * is writable, so get rid of it first.
			 */
			kvmppc_mmu_unmap_page(vcpu, &pte);
		}
		/* The guest's PTE is not mapped yet. Map on the host */
		if (kvmppc_mmu_map_page(vcpu, &pte, iswrite) == -EIO) {
			/* Exit KVM if mapping failed */
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			return RESUME_HOST;
		}
		if (data)
			vcpu->stat.sp_storage++;
		else if (vcpu->arch.mmu.is_dcbz32(vcpu) &&
			 (!(vcpu->arch.hflags & BOOK3S_HFLAG_DCBZ32)))
			kvmppc_patch_dcbz(vcpu, &pte);
	} else {
		/* MMIO */
		vcpu->stat.mmio_exits++;
		vcpu->arch.paddr_accessed = pte.raddr;
		vcpu->arch.vaddr_accessed = pte.eaddr;
		r = kvmppc_emulate_mmio(run, vcpu);
		if ( r == RESUME_HOST_NV )
			r = RESUME_HOST;
	}

	return r;
}

/* Give up external provider (FPU, Altivec, VSX) */
void kvmppc_giveup_ext(struct kvm_vcpu *vcpu, ulong msr)
{
	struct thread_struct *t = &current->thread;

	/*
	 * VSX instructions can access FP and vector registers, so if
	 * we are giving up VSX, make sure we give up FP and VMX as well.
	 */
	if (msr & MSR_VSX)
		msr |= MSR_FP | MSR_VEC;

	msr &= vcpu->arch.guest_owned_ext;
	if (!msr)
		return;

#ifdef DEBUG_EXT
	printk(KERN_INFO "Giving up ext 0x%lx\n", msr);
#endif

	if (msr & MSR_FP) {
		/*
		 * Note that on CPUs with VSX, giveup_fpu stores
		 * both the traditional FP registers and the added VSX
		 * registers into thread.fp_state.fpr[].
		 */
		if (t->regs->msr & MSR_FP)
			giveup_fpu(current);
		t->fp_save_area = NULL;
	}

#ifdef CONFIG_ALTIVEC
	if (msr & MSR_VEC) {
		if (current->thread.regs->msr & MSR_VEC)
			giveup_altivec(current);
		t->vr_save_area = NULL;
	}
#endif

	vcpu->arch.guest_owned_ext &= ~(msr | MSR_VSX);
	kvmppc_recalc_shadow_msr(vcpu);
}

/* Give up facility (TAR / EBB / DSCR) */
void kvmppc_giveup_fac(struct kvm_vcpu *vcpu, ulong fac)
{
#ifdef CONFIG_PPC_BOOK3S_64
	if (!(vcpu->arch.shadow_fscr & (1ULL << fac))) {
		/* Facility not available to the guest, ignore giveup request*/
		return;
	}

	switch (fac) {
	case FSCR_TAR_LG:
		vcpu->arch.tar = mfspr(SPRN_TAR);
		mtspr(SPRN_TAR, current->thread.tar);
		vcpu->arch.shadow_fscr &= ~FSCR_TAR;
		break;
	}
#endif
}

/* Handle external providers (FPU, Altivec, VSX) */
static int kvmppc_handle_ext(struct kvm_vcpu *vcpu, unsigned int exit_nr,
			     ulong msr)
{
	struct thread_struct *t = &current->thread;

	/* When we have paired singles, we emulate in software */
	if (vcpu->arch.hflags & BOOK3S_HFLAG_PAIRED_SINGLE)
		return RESUME_GUEST;

	if (!(kvmppc_get_msr(vcpu) & msr)) {
		kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		return RESUME_GUEST;
	}

	if (msr == MSR_VSX) {
		/* No VSX?  Give an illegal instruction interrupt */
#ifdef CONFIG_VSX
		if (!cpu_has_feature(CPU_FTR_VSX))
#endif
		{
			kvmppc_core_queue_program(vcpu, SRR1_PROGILL);
			return RESUME_GUEST;
		}

		/*
		 * We have to load up all the FP and VMX registers before
		 * we can let the guest use VSX instructions.
		 */
		msr = MSR_FP | MSR_VEC | MSR_VSX;
	}

	/* See if we already own all the ext(s) needed */
	msr &= ~vcpu->arch.guest_owned_ext;
	if (!msr)
		return RESUME_GUEST;

#ifdef DEBUG_EXT
	printk(KERN_INFO "Loading up ext 0x%lx\n", msr);
#endif

	if (msr & MSR_FP) {
		preempt_disable();
		enable_kernel_fp();
		load_fp_state(&vcpu->arch.fp);
		disable_kernel_fp();
		t->fp_save_area = &vcpu->arch.fp;
		preempt_enable();
	}

	if (msr & MSR_VEC) {
#ifdef CONFIG_ALTIVEC
		preempt_disable();
		enable_kernel_altivec();
		load_vr_state(&vcpu->arch.vr);
		disable_kernel_altivec();
		t->vr_save_area = &vcpu->arch.vr;
		preempt_enable();
#endif
	}

	t->regs->msr |= msr;
	vcpu->arch.guest_owned_ext |= msr;
	kvmppc_recalc_shadow_msr(vcpu);

	return RESUME_GUEST;
}

/*
 * Kernel code using FP or VMX could have flushed guest state to
 * the thread_struct; if so, get it back now.
 */
static void kvmppc_handle_lost_ext(struct kvm_vcpu *vcpu)
{
	unsigned long lost_ext;

	lost_ext = vcpu->arch.guest_owned_ext & ~current->thread.regs->msr;
	if (!lost_ext)
		return;

	if (lost_ext & MSR_FP) {
		preempt_disable();
		enable_kernel_fp();
		load_fp_state(&vcpu->arch.fp);
		disable_kernel_fp();
		preempt_enable();
	}
#ifdef CONFIG_ALTIVEC
	if (lost_ext & MSR_VEC) {
		preempt_disable();
		enable_kernel_altivec();
		load_vr_state(&vcpu->arch.vr);
		disable_kernel_altivec();
		preempt_enable();
	}
#endif
	current->thread.regs->msr |= lost_ext;
}

#ifdef CONFIG_PPC_BOOK3S_64

void kvmppc_trigger_fac_interrupt(struct kvm_vcpu *vcpu, ulong fac)
{
	/* Inject the Interrupt Cause field and trigger a guest interrupt */
	vcpu->arch.fscr &= ~(0xffULL << 56);
	vcpu->arch.fscr |= (fac << 56);
	kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_FAC_UNAVAIL);
}

static void kvmppc_emulate_fac(struct kvm_vcpu *vcpu, ulong fac)
{
	enum emulation_result er = EMULATE_FAIL;

	if (!(kvmppc_get_msr(vcpu) & MSR_PR))
		er = kvmppc_emulate_instruction(vcpu->run, vcpu);

	if ((er != EMULATE_DONE) && (er != EMULATE_AGAIN)) {
		/* Couldn't emulate, trigger interrupt in guest */
		kvmppc_trigger_fac_interrupt(vcpu, fac);
	}
}

/* Enable facilities (TAR, EBB, DSCR) for the guest */
static int kvmppc_handle_fac(struct kvm_vcpu *vcpu, ulong fac)
{
	bool guest_fac_enabled;
	BUG_ON(!cpu_has_feature(CPU_FTR_ARCH_207S));

	/*
	 * Not every facility is enabled by FSCR bits, check whether the
	 * guest has this facility enabled at all.
	 */
	switch (fac) {
	case FSCR_TAR_LG:
	case FSCR_EBB_LG:
		guest_fac_enabled = (vcpu->arch.fscr & (1ULL << fac));
		break;
	case FSCR_TM_LG:
		guest_fac_enabled = kvmppc_get_msr(vcpu) & MSR_TM;
		break;
	default:
		guest_fac_enabled = false;
		break;
	}

	if (!guest_fac_enabled) {
		/* Facility not enabled by the guest */
		kvmppc_trigger_fac_interrupt(vcpu, fac);
		return RESUME_GUEST;
	}

	switch (fac) {
	case FSCR_TAR_LG:
		/* TAR switching isn't lazy in Linux yet */
		current->thread.tar = mfspr(SPRN_TAR);
		mtspr(SPRN_TAR, vcpu->arch.tar);
		vcpu->arch.shadow_fscr |= FSCR_TAR;
		break;
	default:
		kvmppc_emulate_fac(vcpu, fac);
		break;
	}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	/* Since we disabled MSR_TM at privilege state, the mfspr instruction
	 * for TM spr can trigger TM fac unavailable. In this case, the
	 * emulation is handled by kvmppc_emulate_fac(), which invokes
	 * kvmppc_emulate_mfspr() finally. But note the mfspr can include
	 * RT for NV registers. So it need to restore those NV reg to reflect
	 * the update.
	 */
	if ((fac == FSCR_TM_LG) && !(kvmppc_get_msr(vcpu) & MSR_PR))
		return RESUME_GUEST_NV;
#endif

	return RESUME_GUEST;
}

void kvmppc_set_fscr(struct kvm_vcpu *vcpu, u64 fscr)
{
	if ((vcpu->arch.fscr & FSCR_TAR) && !(fscr & FSCR_TAR)) {
		/* TAR got dropped, drop it in shadow too */
		kvmppc_giveup_fac(vcpu, FSCR_TAR_LG);
	} else if (!(vcpu->arch.fscr & FSCR_TAR) && (fscr & FSCR_TAR)) {
		vcpu->arch.fscr = fscr;
		kvmppc_handle_fac(vcpu, FSCR_TAR_LG);
		return;
	}

	vcpu->arch.fscr = fscr;
}
#endif

static void kvmppc_setup_debug(struct kvm_vcpu *vcpu)
{
	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP) {
		u64 msr = kvmppc_get_msr(vcpu);

		kvmppc_set_msr(vcpu, msr | MSR_SE);
	}
}

static void kvmppc_clear_debug(struct kvm_vcpu *vcpu)
{
	if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP) {
		u64 msr = kvmppc_get_msr(vcpu);

		kvmppc_set_msr(vcpu, msr & ~MSR_SE);
	}
}

static int kvmppc_exit_pr_progint(struct kvm_run *run, struct kvm_vcpu *vcpu,
				  unsigned int exit_nr)
{
	enum emulation_result er;
	ulong flags;
	u32 last_inst;
	int emul, r;

	/*
	 * shadow_srr1 only contains valid flags if we came here via a program
	 * exception. The other exceptions (emulation assist, FP unavailable,
	 * etc.) do not provide flags in SRR1, so use an illegal-instruction
	 * exception when injecting a program interrupt into the guest.
	 */
	if (exit_nr == BOOK3S_INTERRUPT_PROGRAM)
		flags = vcpu->arch.shadow_srr1 & 0x1f0000ull;
	else
		flags = SRR1_PROGILL;

	emul = kvmppc_get_last_inst(vcpu, INST_GENERIC, &last_inst);
	if (emul != EMULATE_DONE)
		return RESUME_GUEST;

	if (kvmppc_get_msr(vcpu) & MSR_PR) {
#ifdef EXIT_DEBUG
		pr_info("Userspace triggered 0x700 exception at\n 0x%lx (0x%x)\n",
			kvmppc_get_pc(vcpu), last_inst);
#endif
		if ((last_inst & 0xff0007ff) != (INS_DCBZ & 0xfffffff7)) {
			kvmppc_core_queue_program(vcpu, flags);
			return RESUME_GUEST;
		}
	}

	vcpu->stat.emulated_inst_exits++;
	er = kvmppc_emulate_instruction(run, vcpu);
	switch (er) {
	case EMULATE_DONE:
		r = RESUME_GUEST_NV;
		break;
	case EMULATE_AGAIN:
		r = RESUME_GUEST;
		break;
	case EMULATE_FAIL:
		pr_crit("%s: emulation at %lx failed (%08x)\n",
			__func__, kvmppc_get_pc(vcpu), last_inst);
		kvmppc_core_queue_program(vcpu, flags);
		r = RESUME_GUEST;
		break;
	case EMULATE_DO_MMIO:
		run->exit_reason = KVM_EXIT_MMIO;
		r = RESUME_HOST_NV;
		break;
	case EMULATE_EXIT_USER:
		r = RESUME_HOST_NV;
		break;
	default:
		BUG();
	}

	return r;
}

int kvmppc_handle_exit_pr(struct kvm_run *run, struct kvm_vcpu *vcpu,
			  unsigned int exit_nr)
{
	int r = RESUME_HOST;
	int s;

	vcpu->stat.sum_exits++;

	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->ready_for_interrupt_injection = 1;

	/* We get here with MSR.EE=1 */

	trace_kvm_exit(exit_nr, vcpu);
	guest_exit();

	switch (exit_nr) {
	case BOOK3S_INTERRUPT_INST_STORAGE:
	{
		ulong shadow_srr1 = vcpu->arch.shadow_srr1;
		vcpu->stat.pf_instruc++;

		if (kvmppc_is_split_real(vcpu))
			kvmppc_fixup_split_real(vcpu);

#ifdef CONFIG_PPC_BOOK3S_32
		/* We set segments as unused segments when invalidating them. So
		 * treat the respective fault as segment fault. */
		{
			struct kvmppc_book3s_shadow_vcpu *svcpu;
			u32 sr;

			svcpu = svcpu_get(vcpu);
			sr = svcpu->sr[kvmppc_get_pc(vcpu) >> SID_SHIFT];
			svcpu_put(svcpu);
			if (sr == SR_INVALID) {
				kvmppc_mmu_map_segment(vcpu, kvmppc_get_pc(vcpu));
				r = RESUME_GUEST;
				break;
			}
		}
#endif

		/* only care about PTEG not found errors, but leave NX alone */
		if (shadow_srr1 & 0x40000000) {
			int idx = srcu_read_lock(&vcpu->kvm->srcu);
			r = kvmppc_handle_pagefault(run, vcpu, kvmppc_get_pc(vcpu), exit_nr);
			srcu_read_unlock(&vcpu->kvm->srcu, idx);
			vcpu->stat.sp_instruc++;
		} else if (vcpu->arch.mmu.is_dcbz32(vcpu) &&
			  (!(vcpu->arch.hflags & BOOK3S_HFLAG_DCBZ32))) {
			/*
			 * XXX If we do the dcbz hack we use the NX bit to flush&patch the page,
			 *     so we can't use the NX bit inside the guest. Let's cross our fingers,
			 *     that no guest that needs the dcbz hack does NX.
			 */
			kvmppc_mmu_pte_flush(vcpu, kvmppc_get_pc(vcpu), ~0xFFFUL);
			r = RESUME_GUEST;
		} else {
			kvmppc_core_queue_inst_storage(vcpu,
						shadow_srr1 & 0x58000000);
			r = RESUME_GUEST;
		}
		break;
	}
	case BOOK3S_INTERRUPT_DATA_STORAGE:
	{
		ulong dar = kvmppc_get_fault_dar(vcpu);
		u32 fault_dsisr = vcpu->arch.fault_dsisr;
		vcpu->stat.pf_storage++;

#ifdef CONFIG_PPC_BOOK3S_32
		/* We set segments as unused segments when invalidating them. So
		 * treat the respective fault as segment fault. */
		{
			struct kvmppc_book3s_shadow_vcpu *svcpu;
			u32 sr;

			svcpu = svcpu_get(vcpu);
			sr = svcpu->sr[dar >> SID_SHIFT];
			svcpu_put(svcpu);
			if (sr == SR_INVALID) {
				kvmppc_mmu_map_segment(vcpu, dar);
				r = RESUME_GUEST;
				break;
			}
		}
#endif

		/*
		 * We need to handle missing shadow PTEs, and
		 * protection faults due to us mapping a page read-only
		 * when the guest thinks it is writable.
		 */
		if (fault_dsisr & (DSISR_NOHPTE | DSISR_PROTFAULT)) {
			int idx = srcu_read_lock(&vcpu->kvm->srcu);
			r = kvmppc_handle_pagefault(run, vcpu, dar, exit_nr);
			srcu_read_unlock(&vcpu->kvm->srcu, idx);
		} else {
			kvmppc_core_queue_data_storage(vcpu, dar, fault_dsisr);
			r = RESUME_GUEST;
		}
		break;
	}
	case BOOK3S_INTERRUPT_DATA_SEGMENT:
		if (kvmppc_mmu_map_segment(vcpu, kvmppc_get_fault_dar(vcpu)) < 0) {
			kvmppc_set_dar(vcpu, kvmppc_get_fault_dar(vcpu));
			kvmppc_book3s_queue_irqprio(vcpu,
				BOOK3S_INTERRUPT_DATA_SEGMENT);
		}
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_INST_SEGMENT:
		if (kvmppc_mmu_map_segment(vcpu, kvmppc_get_pc(vcpu)) < 0) {
			kvmppc_book3s_queue_irqprio(vcpu,
				BOOK3S_INTERRUPT_INST_SEGMENT);
		}
		r = RESUME_GUEST;
		break;
	/* We're good on these - the host merely wanted to get our attention */
	case BOOK3S_INTERRUPT_DECREMENTER:
	case BOOK3S_INTERRUPT_HV_DECREMENTER:
	case BOOK3S_INTERRUPT_DOORBELL:
	case BOOK3S_INTERRUPT_H_DOORBELL:
		vcpu->stat.dec_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_EXTERNAL:
	case BOOK3S_INTERRUPT_EXTERNAL_HV:
	case BOOK3S_INTERRUPT_H_VIRT:
		vcpu->stat.ext_intr_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_HMI:
	case BOOK3S_INTERRUPT_PERFMON:
	case BOOK3S_INTERRUPT_SYSTEM_RESET:
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_PROGRAM:
	case BOOK3S_INTERRUPT_H_EMUL_ASSIST:
		r = kvmppc_exit_pr_progint(run, vcpu, exit_nr);
		break;
	case BOOK3S_INTERRUPT_SYSCALL:
	{
		u32 last_sc;
		int emul;

		/* Get last sc for papr */
		if (vcpu->arch.papr_enabled) {
			/* The sc instuction points SRR0 to the next inst */
			emul = kvmppc_get_last_inst(vcpu, INST_SC, &last_sc);
			if (emul != EMULATE_DONE) {
				kvmppc_set_pc(vcpu, kvmppc_get_pc(vcpu) - 4);
				r = RESUME_GUEST;
				break;
			}
		}

		if (vcpu->arch.papr_enabled &&
		    (last_sc == 0x44000022) &&
		    !(kvmppc_get_msr(vcpu) & MSR_PR)) {
			/* SC 1 papr hypercalls */
			ulong cmd = kvmppc_get_gpr(vcpu, 3);
			int i;

#ifdef CONFIG_PPC_BOOK3S_64
			if (kvmppc_h_pr(vcpu, cmd) == EMULATE_DONE) {
				r = RESUME_GUEST;
				break;
			}
#endif

			run->papr_hcall.nr = cmd;
			for (i = 0; i < 9; ++i) {
				ulong gpr = kvmppc_get_gpr(vcpu, 4 + i);
				run->papr_hcall.args[i] = gpr;
			}
			run->exit_reason = KVM_EXIT_PAPR_HCALL;
			vcpu->arch.hcall_needed = 1;
			r = RESUME_HOST;
		} else if (vcpu->arch.osi_enabled &&
		    (((u32)kvmppc_get_gpr(vcpu, 3)) == OSI_SC_MAGIC_R3) &&
		    (((u32)kvmppc_get_gpr(vcpu, 4)) == OSI_SC_MAGIC_R4)) {
			/* MOL hypercalls */
			u64 *gprs = run->osi.gprs;
			int i;

			run->exit_reason = KVM_EXIT_OSI;
			for (i = 0; i < 32; i++)
				gprs[i] = kvmppc_get_gpr(vcpu, i);
			vcpu->arch.osi_needed = 1;
			r = RESUME_HOST_NV;
		} else if (!(kvmppc_get_msr(vcpu) & MSR_PR) &&
		    (((u32)kvmppc_get_gpr(vcpu, 0)) == KVM_SC_MAGIC_R0)) {
			/* KVM PV hypercalls */
			kvmppc_set_gpr(vcpu, 3, kvmppc_kvm_pv(vcpu));
			r = RESUME_GUEST;
		} else {
			/* Guest syscalls */
			vcpu->stat.syscall_exits++;
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			r = RESUME_GUEST;
		}
		break;
	}
	case BOOK3S_INTERRUPT_FP_UNAVAIL:
	case BOOK3S_INTERRUPT_ALTIVEC:
	case BOOK3S_INTERRUPT_VSX:
	{
		int ext_msr = 0;
		int emul;
		u32 last_inst;

		if (vcpu->arch.hflags & BOOK3S_HFLAG_PAIRED_SINGLE) {
			/* Do paired single instruction emulation */
			emul = kvmppc_get_last_inst(vcpu, INST_GENERIC,
						    &last_inst);
			if (emul == EMULATE_DONE)
				r = kvmppc_exit_pr_progint(run, vcpu, exit_nr);
			else
				r = RESUME_GUEST;

			break;
		}

		/* Enable external provider */
		switch (exit_nr) {
		case BOOK3S_INTERRUPT_FP_UNAVAIL:
			ext_msr = MSR_FP;
			break;

		case BOOK3S_INTERRUPT_ALTIVEC:
			ext_msr = MSR_VEC;
			break;

		case BOOK3S_INTERRUPT_VSX:
			ext_msr = MSR_VSX;
			break;
		}

		r = kvmppc_handle_ext(vcpu, exit_nr, ext_msr);
		break;
	}
	case BOOK3S_INTERRUPT_ALIGNMENT:
	{
		u32 last_inst;
		int emul = kvmppc_get_last_inst(vcpu, INST_GENERIC, &last_inst);

		if (emul == EMULATE_DONE) {
			u32 dsisr;
			u64 dar;

			dsisr = kvmppc_alignment_dsisr(vcpu, last_inst);
			dar = kvmppc_alignment_dar(vcpu, last_inst);

			kvmppc_set_dsisr(vcpu, dsisr);
			kvmppc_set_dar(vcpu, dar);

			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		}
		r = RESUME_GUEST;
		break;
	}
#ifdef CONFIG_PPC_BOOK3S_64
	case BOOK3S_INTERRUPT_FAC_UNAVAIL:
		r = kvmppc_handle_fac(vcpu, vcpu->arch.shadow_fscr >> 56);
		break;
#endif
	case BOOK3S_INTERRUPT_MACHINE_CHECK:
		kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_TRACE:
		if (vcpu->guest_debug & KVM_GUESTDBG_SINGLESTEP) {
			run->exit_reason = KVM_EXIT_DEBUG;
			r = RESUME_HOST;
		} else {
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			r = RESUME_GUEST;
		}
		break;
	default:
	{
		ulong shadow_srr1 = vcpu->arch.shadow_srr1;
		/* Ugh - bork here! What did we get? */
		printk(KERN_EMERG "exit_nr=0x%x | pc=0x%lx | msr=0x%lx\n",
			exit_nr, kvmppc_get_pc(vcpu), shadow_srr1);
		r = RESUME_HOST;
		BUG();
		break;
	}
	}

	if (!(r & RESUME_HOST)) {
		/* To avoid clobbering exit_reason, only check for signals if
		 * we aren't already exiting to userspace for some other
		 * reason. */

		/*
		 * Interrupts could be timers for the guest which we have to
		 * inject again, so let's postpone them until we're in the guest
		 * and if we really did time things so badly, then we just exit
		 * again due to a host external interrupt.
		 */
		s = kvmppc_prepare_to_enter(vcpu);
		if (s <= 0)
			r = s;
		else {
			/* interrupts now hard-disabled */
			kvmppc_fix_ee_before_entry();
		}

		kvmppc_handle_lost_ext(vcpu);
	}

	trace_kvm_book3s_reenter(r, vcpu);

	return r;
}

static int kvm_arch_vcpu_ioctl_get_sregs_pr(struct kvm_vcpu *vcpu,
					    struct kvm_sregs *sregs)
{
	struct kvmppc_vcpu_book3s *vcpu3s = to_book3s(vcpu);
	int i;

	sregs->pvr = vcpu->arch.pvr;

	sregs->u.s.sdr1 = to_book3s(vcpu)->sdr1;
	if (vcpu->arch.hflags & BOOK3S_HFLAG_SLB) {
		for (i = 0; i < 64; i++) {
			sregs->u.s.ppc64.slb[i].slbe = vcpu->arch.slb[i].orige | i;
			sregs->u.s.ppc64.slb[i].slbv = vcpu->arch.slb[i].origv;
		}
	} else {
		for (i = 0; i < 16; i++)
			sregs->u.s.ppc32.sr[i] = kvmppc_get_sr(vcpu, i);

		for (i = 0; i < 8; i++) {
			sregs->u.s.ppc32.ibat[i] = vcpu3s->ibat[i].raw;
			sregs->u.s.ppc32.dbat[i] = vcpu3s->dbat[i].raw;
		}
	}

	return 0;
}

static int kvm_arch_vcpu_ioctl_set_sregs_pr(struct kvm_vcpu *vcpu,
					    struct kvm_sregs *sregs)
{
	struct kvmppc_vcpu_book3s *vcpu3s = to_book3s(vcpu);
	int i;

	kvmppc_set_pvr_pr(vcpu, sregs->pvr);

	vcpu3s->sdr1 = sregs->u.s.sdr1;
#ifdef CONFIG_PPC_BOOK3S_64
	if (vcpu->arch.hflags & BOOK3S_HFLAG_SLB) {
		/* Flush all SLB entries */
		vcpu->arch.mmu.slbmte(vcpu, 0, 0);
		vcpu->arch.mmu.slbia(vcpu);

		for (i = 0; i < 64; i++) {
			u64 rb = sregs->u.s.ppc64.slb[i].slbe;
			u64 rs = sregs->u.s.ppc64.slb[i].slbv;

			if (rb & SLB_ESID_V)
				vcpu->arch.mmu.slbmte(vcpu, rs, rb);
		}
	} else
#endif
	{
		for (i = 0; i < 16; i++) {
			vcpu->arch.mmu.mtsrin(vcpu, i, sregs->u.s.ppc32.sr[i]);
		}
		for (i = 0; i < 8; i++) {
			kvmppc_set_bat(vcpu, &(vcpu3s->ibat[i]), false,
				       (u32)sregs->u.s.ppc32.ibat[i]);
			kvmppc_set_bat(vcpu, &(vcpu3s->ibat[i]), true,
				       (u32)(sregs->u.s.ppc32.ibat[i] >> 32));
			kvmppc_set_bat(vcpu, &(vcpu3s->dbat[i]), false,
				       (u32)sregs->u.s.ppc32.dbat[i]);
			kvmppc_set_bat(vcpu, &(vcpu3s->dbat[i]), true,
				       (u32)(sregs->u.s.ppc32.dbat[i] >> 32));
		}
	}

	/* Flush the MMU after messing with the segments */
	kvmppc_mmu_pte_flush(vcpu, 0, 0);

	return 0;
}

static int kvmppc_get_one_reg_pr(struct kvm_vcpu *vcpu, u64 id,
				 union kvmppc_one_reg *val)
{
	int r = 0;

	switch (id) {
	case KVM_REG_PPC_DEBUG_INST:
		*val = get_reg_val(id, KVMPPC_INST_SW_BREAKPOINT);
		break;
	case KVM_REG_PPC_HIOR:
		*val = get_reg_val(id, to_book3s(vcpu)->hior);
		break;
	case KVM_REG_PPC_VTB:
		*val = get_reg_val(id, to_book3s(vcpu)->vtb);
		break;
	case KVM_REG_PPC_LPCR:
	case KVM_REG_PPC_LPCR_64:
		/*
		 * We are only interested in the LPCR_ILE bit
		 */
		if (vcpu->arch.intr_msr & MSR_LE)
			*val = get_reg_val(id, LPCR_ILE);
		else
			*val = get_reg_val(id, 0);
		break;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	case KVM_REG_PPC_TFHAR:
		*val = get_reg_val(id, vcpu->arch.tfhar);
		break;
	case KVM_REG_PPC_TFIAR:
		*val = get_reg_val(id, vcpu->arch.tfiar);
		break;
	case KVM_REG_PPC_TEXASR:
		*val = get_reg_val(id, vcpu->arch.texasr);
		break;
	case KVM_REG_PPC_TM_GPR0 ... KVM_REG_PPC_TM_GPR31:
		*val = get_reg_val(id,
				vcpu->arch.gpr_tm[id-KVM_REG_PPC_TM_GPR0]);
		break;
	case KVM_REG_PPC_TM_VSR0 ... KVM_REG_PPC_TM_VSR63:
	{
		int i, j;

		i = id - KVM_REG_PPC_TM_VSR0;
		if (i < 32)
			for (j = 0; j < TS_FPRWIDTH; j++)
				val->vsxval[j] = vcpu->arch.fp_tm.fpr[i][j];
		else {
			if (cpu_has_feature(CPU_FTR_ALTIVEC))
				val->vval = vcpu->arch.vr_tm.vr[i-32];
			else
				r = -ENXIO;
		}
		break;
	}
	case KVM_REG_PPC_TM_CR:
		*val = get_reg_val(id, vcpu->arch.cr_tm);
		break;
	case KVM_REG_PPC_TM_XER:
		*val = get_reg_val(id, vcpu->arch.xer_tm);
		break;
	case KVM_REG_PPC_TM_LR:
		*val = get_reg_val(id, vcpu->arch.lr_tm);
		break;
	case KVM_REG_PPC_TM_CTR:
		*val = get_reg_val(id, vcpu->arch.ctr_tm);
		break;
	case KVM_REG_PPC_TM_FPSCR:
		*val = get_reg_val(id, vcpu->arch.fp_tm.fpscr);
		break;
	case KVM_REG_PPC_TM_AMR:
		*val = get_reg_val(id, vcpu->arch.amr_tm);
		break;
	case KVM_REG_PPC_TM_PPR:
		*val = get_reg_val(id, vcpu->arch.ppr_tm);
		break;
	case KVM_REG_PPC_TM_VRSAVE:
		*val = get_reg_val(id, vcpu->arch.vrsave_tm);
		break;
	case KVM_REG_PPC_TM_VSCR:
		if (cpu_has_feature(CPU_FTR_ALTIVEC))
			*val = get_reg_val(id, vcpu->arch.vr_tm.vscr.u[3]);
		else
			r = -ENXIO;
		break;
	case KVM_REG_PPC_TM_DSCR:
		*val = get_reg_val(id, vcpu->arch.dscr_tm);
		break;
	case KVM_REG_PPC_TM_TAR:
		*val = get_reg_val(id, vcpu->arch.tar_tm);
		break;
#endif
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

static void kvmppc_set_lpcr_pr(struct kvm_vcpu *vcpu, u64 new_lpcr)
{
	if (new_lpcr & LPCR_ILE)
		vcpu->arch.intr_msr |= MSR_LE;
	else
		vcpu->arch.intr_msr &= ~MSR_LE;
}

static int kvmppc_set_one_reg_pr(struct kvm_vcpu *vcpu, u64 id,
				 union kvmppc_one_reg *val)
{
	int r = 0;

	switch (id) {
	case KVM_REG_PPC_HIOR:
		to_book3s(vcpu)->hior = set_reg_val(id, *val);
		to_book3s(vcpu)->hior_explicit = true;
		break;
	case KVM_REG_PPC_VTB:
		to_book3s(vcpu)->vtb = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_LPCR:
	case KVM_REG_PPC_LPCR_64:
		kvmppc_set_lpcr_pr(vcpu, set_reg_val(id, *val));
		break;
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	case KVM_REG_PPC_TFHAR:
		vcpu->arch.tfhar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TFIAR:
		vcpu->arch.tfiar = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TEXASR:
		vcpu->arch.texasr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_GPR0 ... KVM_REG_PPC_TM_GPR31:
		vcpu->arch.gpr_tm[id - KVM_REG_PPC_TM_GPR0] =
			set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_VSR0 ... KVM_REG_PPC_TM_VSR63:
	{
		int i, j;

		i = id - KVM_REG_PPC_TM_VSR0;
		if (i < 32)
			for (j = 0; j < TS_FPRWIDTH; j++)
				vcpu->arch.fp_tm.fpr[i][j] = val->vsxval[j];
		else
			if (cpu_has_feature(CPU_FTR_ALTIVEC))
				vcpu->arch.vr_tm.vr[i-32] = val->vval;
			else
				r = -ENXIO;
		break;
	}
	case KVM_REG_PPC_TM_CR:
		vcpu->arch.cr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_XER:
		vcpu->arch.xer_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_LR:
		vcpu->arch.lr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_CTR:
		vcpu->arch.ctr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_FPSCR:
		vcpu->arch.fp_tm.fpscr = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_AMR:
		vcpu->arch.amr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_PPR:
		vcpu->arch.ppr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_VRSAVE:
		vcpu->arch.vrsave_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_VSCR:
		if (cpu_has_feature(CPU_FTR_ALTIVEC))
			vcpu->arch.vr.vscr.u[3] = set_reg_val(id, *val);
		else
			r = -ENXIO;
		break;
	case KVM_REG_PPC_TM_DSCR:
		vcpu->arch.dscr_tm = set_reg_val(id, *val);
		break;
	case KVM_REG_PPC_TM_TAR:
		vcpu->arch.tar_tm = set_reg_val(id, *val);
		break;
#endif
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

static int kvmppc_core_vcpu_create_pr(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s;
	unsigned long p;
	int err;

	err = -ENOMEM;

	vcpu_book3s = vzalloc(sizeof(struct kvmppc_vcpu_book3s));
	if (!vcpu_book3s)
		goto out;
	vcpu->arch.book3s = vcpu_book3s;

#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
	vcpu->arch.shadow_vcpu =
		kzalloc(sizeof(*vcpu->arch.shadow_vcpu), GFP_KERNEL);
	if (!vcpu->arch.shadow_vcpu)
		goto free_vcpu3s;
#endif

	p = __get_free_page(GFP_KERNEL|__GFP_ZERO);
	if (!p)
		goto free_shadow_vcpu;
	vcpu->arch.shared = (void *)p;
#ifdef CONFIG_PPC_BOOK3S_64
	/* Always start the shared struct in native endian mode */
#ifdef __BIG_ENDIAN__
        vcpu->arch.shared_big_endian = true;
#else
        vcpu->arch.shared_big_endian = false;
#endif

	/*
	 * Default to the same as the host if we're on sufficiently
	 * recent machine that we have 1TB segments;
	 * otherwise default to PPC970FX.
	 */
	vcpu->arch.pvr = 0x3C0301;
	if (mmu_has_feature(MMU_FTR_1T_SEGMENT))
		vcpu->arch.pvr = mfspr(SPRN_PVR);
	vcpu->arch.intr_msr = MSR_SF;
#else
	/* default to book3s_32 (750) */
	vcpu->arch.pvr = 0x84202;
	vcpu->arch.intr_msr = 0;
#endif
	kvmppc_set_pvr_pr(vcpu, vcpu->arch.pvr);
	vcpu->arch.slb_nr = 64;

	vcpu->arch.shadow_msr = MSR_USER64 & ~MSR_LE;

	err = kvmppc_mmu_init(vcpu);
	if (err < 0)
		goto free_shared_page;

	return 0;

free_shared_page:
	free_page((unsigned long)vcpu->arch.shared);
free_shadow_vcpu:
#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
	kfree(vcpu->arch.shadow_vcpu);
free_vcpu3s:
#endif
	vfree(vcpu_book3s);
out:
	return err;
}

static void kvmppc_core_vcpu_free_pr(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);

	kvmppc_mmu_destroy_pr(vcpu);
	free_page((unsigned long)vcpu->arch.shared & PAGE_MASK);
#ifdef CONFIG_KVM_BOOK3S_32_HANDLER
	kfree(vcpu->arch.shadow_vcpu);
#endif
	vfree(vcpu_book3s);
}

static int kvmppc_vcpu_run_pr(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	int ret;
#ifdef CONFIG_ALTIVEC
	unsigned long uninitialized_var(vrsave);
#endif

	/* Check if we can run the vcpu at all */
	if (!vcpu->arch.sane) {
		kvm_run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = -EINVAL;
		goto out;
	}

	kvmppc_setup_debug(vcpu);

	/*
	 * Interrupts could be timers for the guest which we have to inject
	 * again, so let's postpone them until we're in the guest and if we
	 * really did time things so badly, then we just exit again due to
	 * a host external interrupt.
	 */
	ret = kvmppc_prepare_to_enter(vcpu);
	if (ret <= 0)
		goto out;
	/* interrupts now hard-disabled */

	/* Save FPU, Altivec and VSX state */
	giveup_all(current);

	/* Preload FPU if it's enabled */
	if (kvmppc_get_msr(vcpu) & MSR_FP)
		kvmppc_handle_ext(vcpu, BOOK3S_INTERRUPT_FP_UNAVAIL, MSR_FP);

	kvmppc_fix_ee_before_entry();

	ret = __kvmppc_vcpu_run(kvm_run, vcpu);

	kvmppc_clear_debug(vcpu);

	/* No need for guest_exit. It's done in handle_exit.
	   We also get here with interrupts enabled. */

	/* Make sure we save the guest FPU/Altivec/VSX state */
	kvmppc_giveup_ext(vcpu, MSR_FP | MSR_VEC | MSR_VSX);

	/* Make sure we save the guest TAR/EBB/DSCR state */
	kvmppc_giveup_fac(vcpu, FSCR_TAR_LG);

out:
	vcpu->mode = OUTSIDE_GUEST_MODE;
	return ret;
}

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
static int kvm_vm_ioctl_get_dirty_log_pr(struct kvm *kvm,
					 struct kvm_dirty_log *log)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	struct kvm_vcpu *vcpu;
	ulong ga, ga_end;
	int is_dirty = 0;
	int r;
	unsigned long n;

	mutex_lock(&kvm->slots_lock);

	r = kvm_get_dirty_log(kvm, log, &is_dirty);
	if (r)
		goto out;

	/* If nothing is dirty, don't bother messing with page tables. */
	if (is_dirty) {
		slots = kvm_memslots(kvm);
		memslot = id_to_memslot(slots, log->slot);

		ga = memslot->base_gfn << PAGE_SHIFT;
		ga_end = ga + (memslot->npages << PAGE_SHIFT);

		kvm_for_each_vcpu(n, vcpu, kvm)
			kvmppc_mmu_pte_pflush(vcpu, ga, ga_end);

		n = kvm_dirty_bitmap_bytes(memslot);
		memset(memslot->dirty_bitmap, 0, n);
	}

	r = 0;
out:
	mutex_unlock(&kvm->slots_lock);
	return r;
}

static void kvmppc_core_flush_memslot_pr(struct kvm *kvm,
					 struct kvm_memory_slot *memslot)
{
	return;
}

static int kvmppc_core_prepare_memory_region_pr(struct kvm *kvm,
					struct kvm_memory_slot *memslot,
					const struct kvm_userspace_memory_region *mem)
{
	return 0;
}

static void kvmppc_core_commit_memory_region_pr(struct kvm *kvm,
				const struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old,
				const struct kvm_memory_slot *new,
				enum kvm_mr_change change)
{
	return;
}

static void kvmppc_core_free_memslot_pr(struct kvm_memory_slot *free,
					struct kvm_memory_slot *dont)
{
	return;
}

static int kvmppc_core_create_memslot_pr(struct kvm_memory_slot *slot,
					 unsigned long npages)
{
	return 0;
}


#ifdef CONFIG_PPC64
static int kvm_vm_ioctl_get_smmu_info_pr(struct kvm *kvm,
					 struct kvm_ppc_smmu_info *info)
{
	long int i;
	struct kvm_vcpu *vcpu;

	info->flags = 0;

	/* SLB is always 64 entries */
	info->slb_size = 64;

	/* Standard 4k base page size segment */
	info->sps[0].page_shift = 12;
	info->sps[0].slb_enc = 0;
	info->sps[0].enc[0].page_shift = 12;
	info->sps[0].enc[0].pte_enc = 0;

	/*
	 * 64k large page size.
	 * We only want to put this in if the CPUs we're emulating
	 * support it, but unfortunately we don't have a vcpu easily
	 * to hand here to test.  Just pick the first vcpu, and if
	 * that doesn't exist yet, report the minimum capability,
	 * i.e., no 64k pages.
	 * 1T segment support goes along with 64k pages.
	 */
	i = 1;
	vcpu = kvm_get_vcpu(kvm, 0);
	if (vcpu && (vcpu->arch.hflags & BOOK3S_HFLAG_MULTI_PGSIZE)) {
		info->flags = KVM_PPC_1T_SEGMENTS;
		info->sps[i].page_shift = 16;
		info->sps[i].slb_enc = SLB_VSID_L | SLB_VSID_LP_01;
		info->sps[i].enc[0].page_shift = 16;
		info->sps[i].enc[0].pte_enc = 1;
		++i;
	}

	/* Standard 16M large page size segment */
	info->sps[i].page_shift = 24;
	info->sps[i].slb_enc = SLB_VSID_L;
	info->sps[i].enc[0].page_shift = 24;
	info->sps[i].enc[0].pte_enc = 0;

	return 0;
}

static int kvm_configure_mmu_pr(struct kvm *kvm, struct kvm_ppc_mmuv3_cfg *cfg)
{
	if (!cpu_has_feature(CPU_FTR_ARCH_300))
		return -ENODEV;
	/* Require flags and process table base and size to all be zero. */
	if (cfg->flags || cfg->process_table)
		return -EINVAL;
	return 0;
}

#else
static int kvm_vm_ioctl_get_smmu_info_pr(struct kvm *kvm,
					 struct kvm_ppc_smmu_info *info)
{
	/* We should not get called */
	BUG();
	return 0;
}
#endif /* CONFIG_PPC64 */

static unsigned int kvm_global_user_count = 0;
static DEFINE_SPINLOCK(kvm_global_user_count_lock);

static int kvmppc_core_init_vm_pr(struct kvm *kvm)
{
	mutex_init(&kvm->arch.hpt_mutex);

#ifdef CONFIG_PPC_BOOK3S_64
	/* Start out with the default set of hcalls enabled */
	kvmppc_pr_init_default_hcalls(kvm);
#endif

	if (firmware_has_feature(FW_FEATURE_SET_MODE)) {
		spin_lock(&kvm_global_user_count_lock);
		if (++kvm_global_user_count == 1)
			pseries_disable_reloc_on_exc();
		spin_unlock(&kvm_global_user_count_lock);
	}
	return 0;
}

static void kvmppc_core_destroy_vm_pr(struct kvm *kvm)
{
#ifdef CONFIG_PPC64
	WARN_ON(!list_empty(&kvm->arch.spapr_tce_tables));
#endif

	if (firmware_has_feature(FW_FEATURE_SET_MODE)) {
		spin_lock(&kvm_global_user_count_lock);
		BUG_ON(kvm_global_user_count == 0);
		if (--kvm_global_user_count == 0)
			pseries_enable_reloc_on_exc();
		spin_unlock(&kvm_global_user_count_lock);
	}
}

static int kvmppc_core_check_processor_compat_pr(void)
{
	/*
	 * PR KVM can work on POWER9 inside a guest partition
	 * running in HPT mode.  It can't work if we are using
	 * radix translation (because radix provides no way for
	 * a process to have unique translations in quadrant 3).
	 */
	if (cpu_has_feature(CPU_FTR_ARCH_300) && radix_enabled())
		return -EIO;
	return 0;
}

static long kvm_arch_vm_ioctl_pr(struct file *filp,
				 unsigned int ioctl, unsigned long arg)
{
	return -ENOTTY;
}

static struct kvmppc_ops kvm_ops_pr = {
	.get_sregs = kvm_arch_vcpu_ioctl_get_sregs_pr,
	.set_sregs = kvm_arch_vcpu_ioctl_set_sregs_pr,
	.get_one_reg = kvmppc_get_one_reg_pr,
	.set_one_reg = kvmppc_set_one_reg_pr,
	.vcpu_load   = kvmppc_core_vcpu_load_pr,
	.vcpu_put    = kvmppc_core_vcpu_put_pr,
	.inject_interrupt = kvmppc_inject_interrupt_pr,
	.set_msr     = kvmppc_set_msr_pr,
	.vcpu_run    = kvmppc_vcpu_run_pr,
	.vcpu_create = kvmppc_core_vcpu_create_pr,
	.vcpu_free   = kvmppc_core_vcpu_free_pr,
	.check_requests = kvmppc_core_check_requests_pr,
	.get_dirty_log = kvm_vm_ioctl_get_dirty_log_pr,
	.flush_memslot = kvmppc_core_flush_memslot_pr,
	.prepare_memory_region = kvmppc_core_prepare_memory_region_pr,
	.commit_memory_region = kvmppc_core_commit_memory_region_pr,
	.unmap_hva_range = kvm_unmap_hva_range_pr,
	.age_hva  = kvm_age_hva_pr,
	.test_age_hva = kvm_test_age_hva_pr,
	.set_spte_hva = kvm_set_spte_hva_pr,
	.mmu_destroy  = kvmppc_mmu_destroy_pr,
	.free_memslot = kvmppc_core_free_memslot_pr,
	.create_memslot = kvmppc_core_create_memslot_pr,
	.init_vm = kvmppc_core_init_vm_pr,
	.destroy_vm = kvmppc_core_destroy_vm_pr,
	.get_smmu_info = kvm_vm_ioctl_get_smmu_info_pr,
	.emulate_op = kvmppc_core_emulate_op_pr,
	.emulate_mtspr = kvmppc_core_emulate_mtspr_pr,
	.emulate_mfspr = kvmppc_core_emulate_mfspr_pr,
	.fast_vcpu_kick = kvm_vcpu_kick,
	.arch_vm_ioctl  = kvm_arch_vm_ioctl_pr,
#ifdef CONFIG_PPC_BOOK3S_64
	.hcall_implemented = kvmppc_hcall_impl_pr,
	.configure_mmu = kvm_configure_mmu_pr,
#endif
	.giveup_ext = kvmppc_giveup_ext,
};


int kvmppc_book3s_init_pr(void)
{
	int r;

	r = kvmppc_core_check_processor_compat_pr();
	if (r < 0)
		return r;

	kvm_ops_pr.owner = THIS_MODULE;
	kvmppc_pr_ops = &kvm_ops_pr;

	r = kvmppc_mmu_hpte_sysinit();
	return r;
}

void kvmppc_book3s_exit_pr(void)
{
	kvmppc_pr_ops = NULL;
	kvmppc_mmu_hpte_sysexit();
}

/*
 * We only support separate modules for book3s 64
 */
#ifdef CONFIG_PPC_BOOK3S_64

module_init(kvmppc_book3s_init_pr);
module_exit(kvmppc_book3s_exit_pr);

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(KVM_MINOR);
MODULE_ALIAS("devname:kvm");
#endif
