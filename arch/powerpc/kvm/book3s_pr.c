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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kvm_host.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/mmu_context.h>
#include <asm/switch_to.h>
#include <asm/firmware.h>
#include <asm/hvcall.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>

#include "trace.h"

/* #define EXIT_DEBUG */
/* #define DEBUG_EXT */

static int kvmppc_handle_ext(struct kvm_vcpu *vcpu, unsigned int exit_nr,
			     ulong msr);

/* Some compatibility defines */
#ifdef CONFIG_PPC_BOOK3S_32
#define MSR_USER32 MSR_USER
#define MSR_USER64 MSR_USER
#define HW_PAGE_SIZE PAGE_SIZE
#endif

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
#ifdef CONFIG_PPC_BOOK3S_64
	struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
	memcpy(svcpu->slb, to_book3s(vcpu)->slb_shadow, sizeof(svcpu->slb));
	memcpy(&get_paca()->shadow_vcpu, to_book3s(vcpu)->shadow_vcpu,
	       sizeof(get_paca()->shadow_vcpu));
	svcpu->slb_max = to_book3s(vcpu)->slb_shadow_max;
	svcpu_put(svcpu);
#endif
	vcpu->cpu = smp_processor_id();
#ifdef CONFIG_PPC_BOOK3S_32
	current->thread.kvm_shadow_vcpu = to_book3s(vcpu)->shadow_vcpu;
#endif
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_PPC_BOOK3S_64
	struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
	memcpy(to_book3s(vcpu)->slb_shadow, svcpu->slb, sizeof(svcpu->slb));
	memcpy(to_book3s(vcpu)->shadow_vcpu, &get_paca()->shadow_vcpu,
	       sizeof(get_paca()->shadow_vcpu));
	to_book3s(vcpu)->slb_shadow_max = svcpu->slb_max;
	svcpu_put(svcpu);
#endif

	kvmppc_giveup_ext(vcpu, MSR_FP | MSR_VEC | MSR_VSX);
	vcpu->cpu = -1;
}

int kvmppc_core_check_requests(struct kvm_vcpu *vcpu)
{
	int r = 1; /* Indicate we want to get back into the guest */

	/* We misuse TLB_FLUSH to indicate that we want to clear
	   all shadow cache entries */
	if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu))
		kvmppc_mmu_pte_flush(vcpu, 0, 0);

	return r;
}

/************* MMU Notifiers *************/

int kvm_unmap_hva(struct kvm *kvm, unsigned long hva)
{
	trace_kvm_unmap_hva(hva);

	/*
	 * Flush all shadow tlb entries everywhere. This is slow, but
	 * we are 100% sure that we catch the to be unmapped page
	 */
	kvm_flush_remote_tlbs(kvm);

	return 0;
}

int kvm_unmap_hva_range(struct kvm *kvm, unsigned long start, unsigned long end)
{
	/* kvm_unmap_hva flushes everything anyways */
	kvm_unmap_hva(kvm, start);

	return 0;
}

int kvm_age_hva(struct kvm *kvm, unsigned long hva)
{
	/* XXX could be more clever ;) */
	return 0;
}

int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	/* XXX could be more clever ;) */
	return 0;
}

void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	/* The page will get remapped properly on its next fault */
	kvm_unmap_hva(kvm, hva);
}

/*****************************************/

static void kvmppc_recalc_shadow_msr(struct kvm_vcpu *vcpu)
{
	ulong smsr = vcpu->arch.shared->msr;

	/* Guest MSR values */
	smsr &= MSR_FE0 | MSR_FE1 | MSR_SF | MSR_SE | MSR_BE;
	/* Process MSR values */
	smsr |= MSR_ME | MSR_RI | MSR_IR | MSR_DR | MSR_PR | MSR_EE;
	/* External providers the guest reserved */
	smsr |= (vcpu->arch.shared->msr & vcpu->arch.guest_owned_ext);
	/* 64-bit Process MSR values */
#ifdef CONFIG_PPC_BOOK3S_64
	smsr |= MSR_ISF | MSR_HV;
#endif
	vcpu->arch.shadow_msr = smsr;
}

void kvmppc_set_msr(struct kvm_vcpu *vcpu, u64 msr)
{
	ulong old_msr = vcpu->arch.shared->msr;

#ifdef EXIT_DEBUG
	printk(KERN_INFO "KVM: Set MSR to 0x%llx\n", msr);
#endif

	msr &= to_book3s(vcpu)->msr_mask;
	vcpu->arch.shared->msr = msr;
	kvmppc_recalc_shadow_msr(vcpu);

	if (msr & MSR_POW) {
		if (!vcpu->arch.pending_exceptions) {
			kvm_vcpu_block(vcpu);
			clear_bit(KVM_REQ_UNHALT, &vcpu->requests);
			vcpu->stat.halt_wakeup++;

			/* Unset POW bit after we woke up */
			msr &= ~MSR_POW;
			vcpu->arch.shared->msr = msr;
		}
	}

	if ((vcpu->arch.shared->msr & (MSR_PR|MSR_IR|MSR_DR)) !=
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
	 * page will be instanciated when calling into RTAS. Note: We
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
	if (vcpu->arch.shared->msr & MSR_FP)
		kvmppc_handle_ext(vcpu, BOOK3S_INTERRUPT_FP_UNAVAIL, MSR_FP);
}

void kvmppc_set_pvr(struct kvm_vcpu *vcpu, u32 pvr)
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
		if ((page[i] & 0xff0007ff) == INS_DCBZ)
			page[i] &= 0xfffffff7;

	kunmap_atomic(page);
	put_page(hpage);
}

static int kvmppc_visible_gfn(struct kvm_vcpu *vcpu, gfn_t gfn)
{
	ulong mp_pa = vcpu->arch.magic_page_pa;

	if (!(vcpu->arch.shared->msr & MSR_SF))
		mp_pa = (uint32_t)mp_pa;

	if (unlikely(mp_pa) &&
	    unlikely((mp_pa & KVM_PAM) >> PAGE_SHIFT == gfn)) {
		return 1;
	}

	return kvm_is_visible_gfn(vcpu->kvm, gfn);
}

int kvmppc_handle_pagefault(struct kvm_run *run, struct kvm_vcpu *vcpu,
			    ulong eaddr, int vec)
{
	bool data = (vec == BOOK3S_INTERRUPT_DATA_STORAGE);
	int r = RESUME_GUEST;
	int relocated;
	int page_found = 0;
	struct kvmppc_pte pte;
	bool is_mmio = false;
	bool dr = (vcpu->arch.shared->msr & MSR_DR) ? true : false;
	bool ir = (vcpu->arch.shared->msr & MSR_IR) ? true : false;
	u64 vsid;

	relocated = data ? dr : ir;

	/* Resolve real address if translation turned on */
	if (relocated) {
		page_found = vcpu->arch.mmu.xlate(vcpu, eaddr, &pte, data);
	} else {
		pte.may_execute = true;
		pte.may_read = true;
		pte.may_write = true;
		pte.raddr = eaddr & KVM_PAM;
		pte.eaddr = eaddr;
		pte.vpage = eaddr >> 12;
	}

	switch (vcpu->arch.shared->msr & (MSR_DR|MSR_IR)) {
	case 0:
		pte.vpage |= ((u64)VSID_REAL << (SID_SHIFT - 12));
		break;
	case MSR_DR:
	case MSR_IR:
		vcpu->arch.mmu.esid_to_vsid(vcpu, eaddr >> SID_SHIFT, &vsid);

		if ((vcpu->arch.shared->msr & (MSR_DR|MSR_IR)) == MSR_DR)
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

	if (page_found == -ENOENT) {
		/* Page not found in guest PTE entries */
		struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
		vcpu->arch.shared->dar = kvmppc_get_fault_dar(vcpu);
		vcpu->arch.shared->dsisr = svcpu->fault_dsisr;
		vcpu->arch.shared->msr |=
			(svcpu->shadow_srr1 & 0x00000000f8000000ULL);
		svcpu_put(svcpu);
		kvmppc_book3s_queue_irqprio(vcpu, vec);
	} else if (page_found == -EPERM) {
		/* Storage protection */
		struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
		vcpu->arch.shared->dar = kvmppc_get_fault_dar(vcpu);
		vcpu->arch.shared->dsisr = svcpu->fault_dsisr & ~DSISR_NOHPTE;
		vcpu->arch.shared->dsisr |= DSISR_PROTFAULT;
		vcpu->arch.shared->msr |=
			svcpu->shadow_srr1 & 0x00000000f8000000ULL;
		svcpu_put(svcpu);
		kvmppc_book3s_queue_irqprio(vcpu, vec);
	} else if (page_found == -EINVAL) {
		/* Page not found in guest SLB */
		vcpu->arch.shared->dar = kvmppc_get_fault_dar(vcpu);
		kvmppc_book3s_queue_irqprio(vcpu, vec + 0x80);
	} else if (!is_mmio &&
		   kvmppc_visible_gfn(vcpu, pte.raddr >> PAGE_SHIFT)) {
		/* The guest's PTE is not mapped yet. Map on the host */
		kvmppc_mmu_map_page(vcpu, &pte);
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

static inline int get_fpr_index(int i)
{
	return i * TS_FPRWIDTH;
}

/* Give up external provider (FPU, Altivec, VSX) */
void kvmppc_giveup_ext(struct kvm_vcpu *vcpu, ulong msr)
{
	struct thread_struct *t = &current->thread;
	u64 *vcpu_fpr = vcpu->arch.fpr;
#ifdef CONFIG_VSX
	u64 *vcpu_vsx = vcpu->arch.vsr;
#endif
	u64 *thread_fpr = (u64*)t->fpr;
	int i;

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
		 * registers into thread.fpr[].
		 */
		if (current->thread.regs->msr & MSR_FP)
			giveup_fpu(current);
		for (i = 0; i < ARRAY_SIZE(vcpu->arch.fpr); i++)
			vcpu_fpr[i] = thread_fpr[get_fpr_index(i)];

		vcpu->arch.fpscr = t->fpscr.val;

#ifdef CONFIG_VSX
		if (cpu_has_feature(CPU_FTR_VSX))
			for (i = 0; i < ARRAY_SIZE(vcpu->arch.vsr) / 2; i++)
				vcpu_vsx[i] = thread_fpr[get_fpr_index(i) + 1];
#endif
	}

#ifdef CONFIG_ALTIVEC
	if (msr & MSR_VEC) {
		if (current->thread.regs->msr & MSR_VEC)
			giveup_altivec(current);
		memcpy(vcpu->arch.vr, t->vr, sizeof(vcpu->arch.vr));
		vcpu->arch.vscr = t->vscr;
	}
#endif

	vcpu->arch.guest_owned_ext &= ~(msr | MSR_VSX);
	kvmppc_recalc_shadow_msr(vcpu);
}

static int kvmppc_read_inst(struct kvm_vcpu *vcpu)
{
	ulong srr0 = kvmppc_get_pc(vcpu);
	u32 last_inst = kvmppc_get_last_inst(vcpu);
	int ret;

	ret = kvmppc_ld(vcpu, &srr0, sizeof(u32), &last_inst, false);
	if (ret == -ENOENT) {
		ulong msr = vcpu->arch.shared->msr;

		msr = kvmppc_set_field(msr, 33, 33, 1);
		msr = kvmppc_set_field(msr, 34, 36, 0);
		vcpu->arch.shared->msr = kvmppc_set_field(msr, 42, 47, 0);
		kvmppc_book3s_queue_irqprio(vcpu, BOOK3S_INTERRUPT_INST_STORAGE);
		return EMULATE_AGAIN;
	}

	return EMULATE_DONE;
}

static int kvmppc_check_ext(struct kvm_vcpu *vcpu, unsigned int exit_nr)
{

	/* Need to do paired single emulation? */
	if (!(vcpu->arch.hflags & BOOK3S_HFLAG_PAIRED_SINGLE))
		return EMULATE_DONE;

	/* Read out the instruction */
	if (kvmppc_read_inst(vcpu) == EMULATE_DONE)
		/* Need to emulate */
		return EMULATE_FAIL;

	return EMULATE_AGAIN;
}

/* Handle external providers (FPU, Altivec, VSX) */
static int kvmppc_handle_ext(struct kvm_vcpu *vcpu, unsigned int exit_nr,
			     ulong msr)
{
	struct thread_struct *t = &current->thread;
	u64 *vcpu_fpr = vcpu->arch.fpr;
#ifdef CONFIG_VSX
	u64 *vcpu_vsx = vcpu->arch.vsr;
#endif
	u64 *thread_fpr = (u64*)t->fpr;
	int i;

	/* When we have paired singles, we emulate in software */
	if (vcpu->arch.hflags & BOOK3S_HFLAG_PAIRED_SINGLE)
		return RESUME_GUEST;

	if (!(vcpu->arch.shared->msr & msr)) {
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
		for (i = 0; i < ARRAY_SIZE(vcpu->arch.fpr); i++)
			thread_fpr[get_fpr_index(i)] = vcpu_fpr[i];
#ifdef CONFIG_VSX
		for (i = 0; i < ARRAY_SIZE(vcpu->arch.vsr) / 2; i++)
			thread_fpr[get_fpr_index(i) + 1] = vcpu_vsx[i];
#endif
		t->fpscr.val = vcpu->arch.fpscr;
		t->fpexc_mode = 0;
		kvmppc_load_up_fpu();
	}

	if (msr & MSR_VEC) {
#ifdef CONFIG_ALTIVEC
		memcpy(t->vr, vcpu->arch.vr, sizeof(vcpu->arch.vr));
		t->vscr = vcpu->arch.vscr;
		t->vrsave = -1;
		kvmppc_load_up_altivec();
#endif
	}

	current->thread.regs->msr |= msr;
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

	if (lost_ext & MSR_FP)
		kvmppc_load_up_fpu();
	if (lost_ext & MSR_VEC)
		kvmppc_load_up_altivec();
	current->thread.regs->msr |= lost_ext;
}

int kvmppc_handle_exit(struct kvm_run *run, struct kvm_vcpu *vcpu,
                       unsigned int exit_nr)
{
	int r = RESUME_HOST;
	int s;

	vcpu->stat.sum_exits++;

	run->exit_reason = KVM_EXIT_UNKNOWN;
	run->ready_for_interrupt_injection = 1;

	/* We get here with MSR.EE=1 */

	trace_kvm_exit(exit_nr, vcpu);
	kvm_guest_exit();

	switch (exit_nr) {
	case BOOK3S_INTERRUPT_INST_STORAGE:
	{
		struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
		ulong shadow_srr1 = svcpu->shadow_srr1;
		vcpu->stat.pf_instruc++;

#ifdef CONFIG_PPC_BOOK3S_32
		/* We set segments as unused segments when invalidating them. So
		 * treat the respective fault as segment fault. */
		if (svcpu->sr[kvmppc_get_pc(vcpu) >> SID_SHIFT] == SR_INVALID) {
			kvmppc_mmu_map_segment(vcpu, kvmppc_get_pc(vcpu));
			r = RESUME_GUEST;
			svcpu_put(svcpu);
			break;
		}
#endif
		svcpu_put(svcpu);

		/* only care about PTEG not found errors, but leave NX alone */
		if (shadow_srr1 & 0x40000000) {
			r = kvmppc_handle_pagefault(run, vcpu, kvmppc_get_pc(vcpu), exit_nr);
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
			vcpu->arch.shared->msr |= shadow_srr1 & 0x58000000;
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			r = RESUME_GUEST;
		}
		break;
	}
	case BOOK3S_INTERRUPT_DATA_STORAGE:
	{
		ulong dar = kvmppc_get_fault_dar(vcpu);
		struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
		u32 fault_dsisr = svcpu->fault_dsisr;
		vcpu->stat.pf_storage++;

#ifdef CONFIG_PPC_BOOK3S_32
		/* We set segments as unused segments when invalidating them. So
		 * treat the respective fault as segment fault. */
		if ((svcpu->sr[dar >> SID_SHIFT]) == SR_INVALID) {
			kvmppc_mmu_map_segment(vcpu, dar);
			r = RESUME_GUEST;
			svcpu_put(svcpu);
			break;
		}
#endif
		svcpu_put(svcpu);

		/* The only case we need to handle is missing shadow PTEs */
		if (fault_dsisr & DSISR_NOHPTE) {
			r = kvmppc_handle_pagefault(run, vcpu, dar, exit_nr);
		} else {
			vcpu->arch.shared->dar = dar;
			vcpu->arch.shared->dsisr = fault_dsisr;
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
			r = RESUME_GUEST;
		}
		break;
	}
	case BOOK3S_INTERRUPT_DATA_SEGMENT:
		if (kvmppc_mmu_map_segment(vcpu, kvmppc_get_fault_dar(vcpu)) < 0) {
			vcpu->arch.shared->dar = kvmppc_get_fault_dar(vcpu);
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
		vcpu->stat.dec_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_EXTERNAL:
	case BOOK3S_INTERRUPT_EXTERNAL_LEVEL:
	case BOOK3S_INTERRUPT_EXTERNAL_HV:
		vcpu->stat.ext_intr_exits++;
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_PERFMON:
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_PROGRAM:
	case BOOK3S_INTERRUPT_H_EMUL_ASSIST:
	{
		enum emulation_result er;
		struct kvmppc_book3s_shadow_vcpu *svcpu;
		ulong flags;

program_interrupt:
		svcpu = svcpu_get(vcpu);
		flags = svcpu->shadow_srr1 & 0x1f0000ull;
		svcpu_put(svcpu);

		if (vcpu->arch.shared->msr & MSR_PR) {
#ifdef EXIT_DEBUG
			printk(KERN_INFO "Userspace triggered 0x700 exception at 0x%lx (0x%x)\n", kvmppc_get_pc(vcpu), kvmppc_get_last_inst(vcpu));
#endif
			if ((kvmppc_get_last_inst(vcpu) & 0xff0007ff) !=
			    (INS_DCBZ & 0xfffffff7)) {
				kvmppc_core_queue_program(vcpu, flags);
				r = RESUME_GUEST;
				break;
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
			printk(KERN_CRIT "%s: emulation at %lx failed (%08x)\n",
			       __func__, kvmppc_get_pc(vcpu), kvmppc_get_last_inst(vcpu));
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
		break;
	}
	case BOOK3S_INTERRUPT_SYSCALL:
		if (vcpu->arch.papr_enabled &&
		    (kvmppc_get_last_inst(vcpu) == 0x44000022) &&
		    !(vcpu->arch.shared->msr & MSR_PR)) {
			/* SC 1 papr hypercalls */
			ulong cmd = kvmppc_get_gpr(vcpu, 3);
			int i;

#ifdef CONFIG_KVM_BOOK3S_64_PR
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
		} else if (!(vcpu->arch.shared->msr & MSR_PR) &&
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
	case BOOK3S_INTERRUPT_FP_UNAVAIL:
	case BOOK3S_INTERRUPT_ALTIVEC:
	case BOOK3S_INTERRUPT_VSX:
	{
		int ext_msr = 0;

		switch (exit_nr) {
		case BOOK3S_INTERRUPT_FP_UNAVAIL: ext_msr = MSR_FP;  break;
		case BOOK3S_INTERRUPT_ALTIVEC:    ext_msr = MSR_VEC; break;
		case BOOK3S_INTERRUPT_VSX:        ext_msr = MSR_VSX; break;
		}

		switch (kvmppc_check_ext(vcpu, exit_nr)) {
		case EMULATE_DONE:
			/* everything ok - let's enable the ext */
			r = kvmppc_handle_ext(vcpu, exit_nr, ext_msr);
			break;
		case EMULATE_FAIL:
			/* we need to emulate this instruction */
			goto program_interrupt;
			break;
		default:
			/* nothing to worry about - go again */
			break;
		}
		break;
	}
	case BOOK3S_INTERRUPT_ALIGNMENT:
		if (kvmppc_read_inst(vcpu) == EMULATE_DONE) {
			vcpu->arch.shared->dsisr = kvmppc_alignment_dsisr(vcpu,
				kvmppc_get_last_inst(vcpu));
			vcpu->arch.shared->dar = kvmppc_alignment_dar(vcpu,
				kvmppc_get_last_inst(vcpu));
			kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		}
		r = RESUME_GUEST;
		break;
	case BOOK3S_INTERRUPT_MACHINE_CHECK:
	case BOOK3S_INTERRUPT_TRACE:
		kvmppc_book3s_queue_irqprio(vcpu, exit_nr);
		r = RESUME_GUEST;
		break;
	default:
	{
		struct kvmppc_book3s_shadow_vcpu *svcpu = svcpu_get(vcpu);
		ulong shadow_srr1 = svcpu->shadow_srr1;
		svcpu_put(svcpu);
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
		local_irq_disable();
		s = kvmppc_prepare_to_enter(vcpu);
		if (s <= 0) {
			local_irq_enable();
			r = s;
		} else {
			kvmppc_fix_ee_before_entry();
		}
		kvmppc_handle_lost_ext(vcpu);
	}

	trace_kvm_book3s_reenter(r, vcpu);

	return r;
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
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
			sregs->u.s.ppc32.sr[i] = vcpu->arch.shared->sr[i];

		for (i = 0; i < 8; i++) {
			sregs->u.s.ppc32.ibat[i] = vcpu3s->ibat[i].raw;
			sregs->u.s.ppc32.dbat[i] = vcpu3s->dbat[i].raw;
		}
	}

	return 0;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
                                  struct kvm_sregs *sregs)
{
	struct kvmppc_vcpu_book3s *vcpu3s = to_book3s(vcpu);
	int i;

	kvmppc_set_pvr(vcpu, sregs->pvr);

	vcpu3s->sdr1 = sregs->u.s.sdr1;
	if (vcpu->arch.hflags & BOOK3S_HFLAG_SLB) {
		for (i = 0; i < 64; i++) {
			vcpu->arch.mmu.slbmte(vcpu, sregs->u.s.ppc64.slb[i].slbv,
						    sregs->u.s.ppc64.slb[i].slbe);
		}
	} else {
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

int kvmppc_get_one_reg(struct kvm_vcpu *vcpu, u64 id, union kvmppc_one_reg *val)
{
	int r = 0;

	switch (id) {
	case KVM_REG_PPC_HIOR:
		*val = get_reg_val(id, to_book3s(vcpu)->hior);
		break;
#ifdef CONFIG_VSX
	case KVM_REG_PPC_VSR0 ... KVM_REG_PPC_VSR31: {
		long int i = id - KVM_REG_PPC_VSR0;

		if (!cpu_has_feature(CPU_FTR_VSX)) {
			r = -ENXIO;
			break;
		}
		val->vsxval[0] = vcpu->arch.fpr[i];
		val->vsxval[1] = vcpu->arch.vsr[i];
		break;
	}
#endif /* CONFIG_VSX */
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

int kvmppc_set_one_reg(struct kvm_vcpu *vcpu, u64 id, union kvmppc_one_reg *val)
{
	int r = 0;

	switch (id) {
	case KVM_REG_PPC_HIOR:
		to_book3s(vcpu)->hior = set_reg_val(id, *val);
		to_book3s(vcpu)->hior_explicit = true;
		break;
#ifdef CONFIG_VSX
	case KVM_REG_PPC_VSR0 ... KVM_REG_PPC_VSR31: {
		long int i = id - KVM_REG_PPC_VSR0;

		if (!cpu_has_feature(CPU_FTR_VSX)) {
			r = -ENXIO;
			break;
		}
		vcpu->arch.fpr[i] = val->vsxval[0];
		vcpu->arch.vsr[i] = val->vsxval[1];
		break;
	}
#endif /* CONFIG_VSX */
	default:
		r = -EINVAL;
		break;
	}

	return r;
}

int kvmppc_core_check_processor_compat(void)
{
	return 0;
}

struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm, unsigned int id)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s;
	struct kvm_vcpu *vcpu;
	int err = -ENOMEM;
	unsigned long p;

	vcpu_book3s = vzalloc(sizeof(struct kvmppc_vcpu_book3s));
	if (!vcpu_book3s)
		goto out;

	vcpu_book3s->shadow_vcpu =
		kzalloc(sizeof(*vcpu_book3s->shadow_vcpu), GFP_KERNEL);
	if (!vcpu_book3s->shadow_vcpu)
		goto free_vcpu;

	vcpu = &vcpu_book3s->vcpu;
	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_shadow_vcpu;

	err = -ENOMEM;
	p = __get_free_page(GFP_KERNEL|__GFP_ZERO);
	if (!p)
		goto uninit_vcpu;
	/* the real shared page fills the last 4k of our page */
	vcpu->arch.shared = (void *)(p + PAGE_SIZE - 4096);

#ifdef CONFIG_PPC_BOOK3S_64
	/* default to book3s_64 (970fx) */
	vcpu->arch.pvr = 0x3C0301;
#else
	/* default to book3s_32 (750) */
	vcpu->arch.pvr = 0x84202;
#endif
	kvmppc_set_pvr(vcpu, vcpu->arch.pvr);
	vcpu->arch.slb_nr = 64;

	vcpu->arch.shadow_msr = MSR_USER64;

	err = kvmppc_mmu_init(vcpu);
	if (err < 0)
		goto uninit_vcpu;

	return vcpu;

uninit_vcpu:
	kvm_vcpu_uninit(vcpu);
free_shadow_vcpu:
	kfree(vcpu_book3s->shadow_vcpu);
free_vcpu:
	vfree(vcpu_book3s);
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_book3s *vcpu_book3s = to_book3s(vcpu);

	free_page((unsigned long)vcpu->arch.shared & PAGE_MASK);
	kvm_vcpu_uninit(vcpu);
	kfree(vcpu_book3s->shadow_vcpu);
	vfree(vcpu_book3s);
}

int kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
{
	int ret;
	double fpr[32][TS_FPRWIDTH];
	unsigned int fpscr;
	int fpexc_mode;
#ifdef CONFIG_ALTIVEC
	vector128 vr[32];
	vector128 vscr;
	unsigned long uninitialized_var(vrsave);
	int used_vr;
#endif
#ifdef CONFIG_VSX
	int used_vsr;
#endif
	ulong ext_msr;

	/* Check if we can run the vcpu at all */
	if (!vcpu->arch.sane) {
		kvm_run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Interrupts could be timers for the guest which we have to inject
	 * again, so let's postpone them until we're in the guest and if we
	 * really did time things so badly, then we just exit again due to
	 * a host external interrupt.
	 */
	local_irq_disable();
	ret = kvmppc_prepare_to_enter(vcpu);
	if (ret <= 0) {
		local_irq_enable();
		goto out;
	}

	/* Save FPU state in stack */
	if (current->thread.regs->msr & MSR_FP)
		giveup_fpu(current);
	memcpy(fpr, current->thread.fpr, sizeof(current->thread.fpr));
	fpscr = current->thread.fpscr.val;
	fpexc_mode = current->thread.fpexc_mode;

#ifdef CONFIG_ALTIVEC
	/* Save Altivec state in stack */
	used_vr = current->thread.used_vr;
	if (used_vr) {
		if (current->thread.regs->msr & MSR_VEC)
			giveup_altivec(current);
		memcpy(vr, current->thread.vr, sizeof(current->thread.vr));
		vscr = current->thread.vscr;
		vrsave = current->thread.vrsave;
	}
#endif

#ifdef CONFIG_VSX
	/* Save VSX state in stack */
	used_vsr = current->thread.used_vsr;
	if (used_vsr && (current->thread.regs->msr & MSR_VSX))
		__giveup_vsx(current);
#endif

	/* Remember the MSR with disabled extensions */
	ext_msr = current->thread.regs->msr;

	/* Preload FPU if it's enabled */
	if (vcpu->arch.shared->msr & MSR_FP)
		kvmppc_handle_ext(vcpu, BOOK3S_INTERRUPT_FP_UNAVAIL, MSR_FP);

	kvmppc_fix_ee_before_entry();

	ret = __kvmppc_vcpu_run(kvm_run, vcpu);

	/* No need for kvm_guest_exit. It's done in handle_exit.
	   We also get here with interrupts enabled. */

	/* Make sure we save the guest FPU/Altivec/VSX state */
	kvmppc_giveup_ext(vcpu, MSR_FP | MSR_VEC | MSR_VSX);

	current->thread.regs->msr = ext_msr;

	/* Restore FPU/VSX state from stack */
	memcpy(current->thread.fpr, fpr, sizeof(current->thread.fpr));
	current->thread.fpscr.val = fpscr;
	current->thread.fpexc_mode = fpexc_mode;

#ifdef CONFIG_ALTIVEC
	/* Restore Altivec state from stack */
	if (used_vr && current->thread.used_vr) {
		memcpy(current->thread.vr, vr, sizeof(current->thread.vr));
		current->thread.vscr = vscr;
		current->thread.vrsave = vrsave;
	}
	current->thread.used_vr = used_vr;
#endif

#ifdef CONFIG_VSX
	current->thread.used_vsr = used_vsr;
#endif

out:
	vcpu->mode = OUTSIDE_GUEST_MODE;
	return ret;
}

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
				      struct kvm_dirty_log *log)
{
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
		memslot = id_to_memslot(kvm->memslots, log->slot);

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

#ifdef CONFIG_PPC64
int kvm_vm_ioctl_get_smmu_info(struct kvm *kvm, struct kvm_ppc_smmu_info *info)
{
	info->flags = KVM_PPC_1T_SEGMENTS;

	/* SLB is always 64 entries */
	info->slb_size = 64;

	/* Standard 4k base page size segment */
	info->sps[0].page_shift = 12;
	info->sps[0].slb_enc = 0;
	info->sps[0].enc[0].page_shift = 12;
	info->sps[0].enc[0].pte_enc = 0;

	/* Standard 16M large page size segment */
	info->sps[1].page_shift = 24;
	info->sps[1].slb_enc = SLB_VSID_L;
	info->sps[1].enc[0].page_shift = 24;
	info->sps[1].enc[0].pte_enc = 0;

	return 0;
}
#endif /* CONFIG_PPC64 */

void kvmppc_core_free_memslot(struct kvm_memory_slot *free,
			      struct kvm_memory_slot *dont)
{
}

int kvmppc_core_create_memslot(struct kvm_memory_slot *slot,
			       unsigned long npages)
{
	return 0;
}

int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				      struct kvm_memory_slot *memslot,
				      struct kvm_userspace_memory_region *mem)
{
	return 0;
}

void kvmppc_core_commit_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old)
{
}

void kvmppc_core_flush_memslot(struct kvm *kvm, struct kvm_memory_slot *memslot)
{
}

static unsigned int kvm_global_user_count = 0;
static DEFINE_SPINLOCK(kvm_global_user_count_lock);

int kvmppc_core_init_vm(struct kvm *kvm)
{
#ifdef CONFIG_PPC64
	INIT_LIST_HEAD(&kvm->arch.spapr_tce_tables);
	INIT_LIST_HEAD(&kvm->arch.rtas_tokens);
#endif

	if (firmware_has_feature(FW_FEATURE_SET_MODE)) {
		spin_lock(&kvm_global_user_count_lock);
		if (++kvm_global_user_count == 1)
			pSeries_disable_reloc_on_exc();
		spin_unlock(&kvm_global_user_count_lock);
	}
	return 0;
}

void kvmppc_core_destroy_vm(struct kvm *kvm)
{
#ifdef CONFIG_PPC64
	WARN_ON(!list_empty(&kvm->arch.spapr_tce_tables));
#endif

	if (firmware_has_feature(FW_FEATURE_SET_MODE)) {
		spin_lock(&kvm_global_user_count_lock);
		BUG_ON(kvm_global_user_count == 0);
		if (--kvm_global_user_count == 0)
			pSeries_enable_reloc_on_exc();
		spin_unlock(&kvm_global_user_count_lock);
	}
}

static int kvmppc_book3s_init(void)
{
	int r;

	r = kvm_init(NULL, sizeof(struct kvmppc_vcpu_book3s), 0,
		     THIS_MODULE);

	if (r)
		return r;

	r = kvmppc_mmu_hpte_sysinit();

	return r;
}

static void kvmppc_book3s_exit(void)
{
	kvmppc_mmu_hpte_sysexit();
	kvm_exit();
}

module_init(kvmppc_book3s_init);
module_exit(kvmppc_book3s_exit);
