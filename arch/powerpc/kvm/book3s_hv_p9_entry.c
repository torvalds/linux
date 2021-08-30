// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <asm/asm-prototypes.h>
#include <asm/dbell.h>
#include <asm/kvm_ppc.h>
#include <asm/ppc-opcode.h>

#ifdef CONFIG_KVM_BOOK3S_HV_EXIT_TIMING
static void __start_timing(struct kvm_vcpu *vcpu, struct kvmhv_tb_accumulator *next)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	u64 tb = mftb() - vc->tb_offset_applied;

	vcpu->arch.cur_activity = next;
	vcpu->arch.cur_tb_start = tb;
}

static void __accumulate_time(struct kvm_vcpu *vcpu, struct kvmhv_tb_accumulator *next)
{
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	struct kvmhv_tb_accumulator *curr;
	u64 tb = mftb() - vc->tb_offset_applied;
	u64 prev_tb;
	u64 delta;
	u64 seq;

	curr = vcpu->arch.cur_activity;
	vcpu->arch.cur_activity = next;
	prev_tb = vcpu->arch.cur_tb_start;
	vcpu->arch.cur_tb_start = tb;

	if (!curr)
		return;

	delta = tb - prev_tb;

	seq = curr->seqcount;
	curr->seqcount = seq + 1;
	smp_wmb();
	curr->tb_total += delta;
	if (seq == 0 || delta < curr->tb_min)
		curr->tb_min = delta;
	if (delta > curr->tb_max)
		curr->tb_max = delta;
	smp_wmb();
	curr->seqcount = seq + 2;
}

#define start_timing(vcpu, next) __start_timing(vcpu, next)
#define end_timing(vcpu) __start_timing(vcpu, NULL)
#define accumulate_time(vcpu, next) __accumulate_time(vcpu, next)
#else
#define start_timing(vcpu, next) do {} while (0)
#define end_timing(vcpu) do {} while (0)
#define accumulate_time(vcpu, next) do {} while (0)
#endif

static inline void mfslb(unsigned int idx, u64 *slbee, u64 *slbev)
{
	asm volatile("slbmfev  %0,%1" : "=r" (*slbev) : "r" (idx));
	asm volatile("slbmfee  %0,%1" : "=r" (*slbee) : "r" (idx));
}

static inline void mtslb(u64 slbee, u64 slbev)
{
	asm volatile("slbmte %0,%1" :: "r" (slbev), "r" (slbee));
}

static inline void clear_slb_entry(unsigned int idx)
{
	mtslb(idx, 0);
}

static inline void slb_clear_invalidate_partition(void)
{
	clear_slb_entry(0);
	asm volatile(PPC_SLBIA(6));
}

/*
 * Malicious or buggy radix guests may have inserted SLB entries
 * (only 0..3 because radix always runs with UPRT=1), so these must
 * be cleared here to avoid side-channels. slbmte is used rather
 * than slbia, as it won't clear cached translations.
 */
static void radix_clear_slb(void)
{
	int i;

	for (i = 0; i < 4; i++)
		clear_slb_entry(i);
}

static void switch_mmu_to_guest_radix(struct kvm *kvm, struct kvm_vcpu *vcpu, u64 lpcr)
{
	struct kvm_nested_guest *nested = vcpu->arch.nested;
	u32 lpid;

	lpid = nested ? nested->shadow_lpid : kvm->arch.lpid;

	/*
	 * All the isync()s are overkill but trivially follow the ISA
	 * requirements. Some can likely be replaced with justification
	 * comment for why they are not needed.
	 */
	isync();
	mtspr(SPRN_LPID, lpid);
	isync();
	mtspr(SPRN_LPCR, lpcr);
	isync();
	mtspr(SPRN_PID, vcpu->arch.pid);
	isync();
}

static void switch_mmu_to_guest_hpt(struct kvm *kvm, struct kvm_vcpu *vcpu, u64 lpcr)
{
	u32 lpid;
	int i;

	lpid = kvm->arch.lpid;

	mtspr(SPRN_LPID, lpid);
	mtspr(SPRN_LPCR, lpcr);
	mtspr(SPRN_PID, vcpu->arch.pid);

	for (i = 0; i < vcpu->arch.slb_max; i++)
		mtslb(vcpu->arch.slb[i].orige, vcpu->arch.slb[i].origv);

	isync();
}

static void switch_mmu_to_host(struct kvm *kvm, u32 pid)
{
	isync();
	mtspr(SPRN_PID, pid);
	isync();
	mtspr(SPRN_LPID, kvm->arch.host_lpid);
	isync();
	mtspr(SPRN_LPCR, kvm->arch.host_lpcr);
	isync();

	if (!radix_enabled())
		slb_restore_bolted_realmode();
}

static void save_clear_host_mmu(struct kvm *kvm)
{
	if (!radix_enabled()) {
		/*
		 * Hash host could save and restore host SLB entries to
		 * reduce SLB fault overheads of VM exits, but for now the
		 * existing code clears all entries and restores just the
		 * bolted ones when switching back to host.
		 */
		slb_clear_invalidate_partition();
	}
}

static void save_clear_guest_mmu(struct kvm *kvm, struct kvm_vcpu *vcpu)
{
	if (kvm_is_radix(kvm)) {
		radix_clear_slb();
	} else {
		int i;
		int nr = 0;

		/*
		 * This must run before switching to host (radix host can't
		 * access all SLBs).
		 */
		for (i = 0; i < vcpu->arch.slb_nr; i++) {
			u64 slbee, slbev;
			mfslb(i, &slbee, &slbev);
			if (slbee & SLB_ESID_V) {
				vcpu->arch.slb[nr].orige = slbee | i;
				vcpu->arch.slb[nr].origv = slbev;
				nr++;
			}
		}
		vcpu->arch.slb_max = nr;
		slb_clear_invalidate_partition();
	}
}

int kvmhv_vcpu_entry_p9(struct kvm_vcpu *vcpu, u64 time_limit, unsigned long lpcr)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_nested_guest *nested = vcpu->arch.nested;
	struct kvmppc_vcore *vc = vcpu->arch.vcore;
	s64 hdec;
	u64 tb, purr, spurr;
	u64 *exsave;
	bool ri_set;
	int trap;
	unsigned long msr;
	unsigned long host_hfscr;
	unsigned long host_ciabr;
	unsigned long host_dawr0;
	unsigned long host_dawrx0;
	unsigned long host_psscr;
	unsigned long host_pidr;
	unsigned long host_dawr1;
	unsigned long host_dawrx1;

	hdec = time_limit - mftb();
	if (hdec < 0)
		return BOOK3S_INTERRUPT_HV_DECREMENTER;

	WARN_ON_ONCE(vcpu->arch.shregs.msr & MSR_HV);
	WARN_ON_ONCE(!(vcpu->arch.shregs.msr & MSR_ME));

	start_timing(vcpu, &vcpu->arch.rm_entry);

	vcpu->arch.ceded = 0;

	if (vc->tb_offset) {
		u64 new_tb = mftb() + vc->tb_offset;
		mtspr(SPRN_TBU40, new_tb);
		tb = mftb();
		if ((tb & 0xffffff) < (new_tb & 0xffffff))
			mtspr(SPRN_TBU40, new_tb + 0x1000000);
		vc->tb_offset_applied = vc->tb_offset;
	}

	msr = mfmsr();

	host_hfscr = mfspr(SPRN_HFSCR);
	host_ciabr = mfspr(SPRN_CIABR);
	host_dawr0 = mfspr(SPRN_DAWR0);
	host_dawrx0 = mfspr(SPRN_DAWRX0);
	host_psscr = mfspr(SPRN_PSSCR);
	host_pidr = mfspr(SPRN_PID);
	if (cpu_has_feature(CPU_FTR_DAWR1)) {
		host_dawr1 = mfspr(SPRN_DAWR1);
		host_dawrx1 = mfspr(SPRN_DAWRX1);
	}

	if (vc->pcr)
		mtspr(SPRN_PCR, vc->pcr | PCR_MASK);
	mtspr(SPRN_DPDES, vc->dpdes);
	mtspr(SPRN_VTB, vc->vtb);

	local_paca->kvm_hstate.host_purr = mfspr(SPRN_PURR);
	local_paca->kvm_hstate.host_spurr = mfspr(SPRN_SPURR);
	mtspr(SPRN_PURR, vcpu->arch.purr);
	mtspr(SPRN_SPURR, vcpu->arch.spurr);

	if (dawr_enabled()) {
		mtspr(SPRN_DAWR0, vcpu->arch.dawr0);
		mtspr(SPRN_DAWRX0, vcpu->arch.dawrx0);
		if (cpu_has_feature(CPU_FTR_DAWR1)) {
			mtspr(SPRN_DAWR1, vcpu->arch.dawr1);
			mtspr(SPRN_DAWRX1, vcpu->arch.dawrx1);
		}
	}
	mtspr(SPRN_CIABR, vcpu->arch.ciabr);
	mtspr(SPRN_IC, vcpu->arch.ic);

	mtspr(SPRN_PSSCR, vcpu->arch.psscr | PSSCR_EC |
	      (local_paca->kvm_hstate.fake_suspend << PSSCR_FAKE_SUSPEND_LG));

	mtspr(SPRN_HFSCR, vcpu->arch.hfscr);

	mtspr(SPRN_HSRR0, vcpu->arch.regs.nip);
	mtspr(SPRN_HSRR1, (vcpu->arch.shregs.msr & ~MSR_HV) | MSR_ME);

	/*
	 * On POWER9 DD2.1 and below, sometimes on a Hypervisor Data Storage
	 * Interrupt (HDSI) the HDSISR is not be updated at all.
	 *
	 * To work around this we put a canary value into the HDSISR before
	 * returning to a guest and then check for this canary when we take a
	 * HDSI. If we find the canary on a HDSI, we know the hardware didn't
	 * update the HDSISR. In this case we return to the guest to retake the
	 * HDSI which should correctly update the HDSISR the second time HDSI
	 * entry.
	 *
	 * Just do this on all p9 processors for now.
	 */
	mtspr(SPRN_HDSISR, HDSISR_CANARY);

	mtspr(SPRN_SPRG0, vcpu->arch.shregs.sprg0);
	mtspr(SPRN_SPRG1, vcpu->arch.shregs.sprg1);
	mtspr(SPRN_SPRG2, vcpu->arch.shregs.sprg2);
	mtspr(SPRN_SPRG3, vcpu->arch.shregs.sprg3);

	mtspr(SPRN_AMOR, ~0UL);

	local_paca->kvm_hstate.in_guest = KVM_GUEST_MODE_HV_P9;

	/*
	 * Hash host, hash guest, or radix guest with prefetch bug, all have
	 * to disable the MMU before switching to guest MMU state.
	 */
	if (!radix_enabled() || !kvm_is_radix(kvm) ||
			cpu_has_feature(CPU_FTR_P9_RADIX_PREFETCH_BUG))
		__mtmsrd(msr & ~(MSR_IR|MSR_DR|MSR_RI), 0);

	save_clear_host_mmu(kvm);

	if (kvm_is_radix(kvm)) {
		switch_mmu_to_guest_radix(kvm, vcpu, lpcr);
		if (!cpu_has_feature(CPU_FTR_P9_RADIX_PREFETCH_BUG))
			__mtmsrd(0, 1); /* clear RI */

	} else {
		switch_mmu_to_guest_hpt(kvm, vcpu, lpcr);
	}

	/* TLBIEL uses LPID=LPIDR, so run this after setting guest LPID */
	kvmppc_check_need_tlb_flush(kvm, vc->pcpu, nested);

	/*
	 * P9 suppresses the HDEC exception when LPCR[HDICE] = 0,
	 * so set guest LPCR (with HDICE) before writing HDEC.
	 */
	mtspr(SPRN_HDEC, hdec);

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
tm_return_to_guest:
#endif
	mtspr(SPRN_DAR, vcpu->arch.shregs.dar);
	mtspr(SPRN_DSISR, vcpu->arch.shregs.dsisr);
	mtspr(SPRN_SRR0, vcpu->arch.shregs.srr0);
	mtspr(SPRN_SRR1, vcpu->arch.shregs.srr1);

	accumulate_time(vcpu, &vcpu->arch.guest_time);

	kvmppc_p9_enter_guest(vcpu);

	accumulate_time(vcpu, &vcpu->arch.rm_intr);

	/* XXX: Could get these from r11/12 and paca exsave instead */
	vcpu->arch.shregs.srr0 = mfspr(SPRN_SRR0);
	vcpu->arch.shregs.srr1 = mfspr(SPRN_SRR1);
	vcpu->arch.shregs.dar = mfspr(SPRN_DAR);
	vcpu->arch.shregs.dsisr = mfspr(SPRN_DSISR);

	/* 0x2 bit for HSRR is only used by PR and P7/8 HV paths, clear it */
	trap = local_paca->kvm_hstate.scratch0 & ~0x2;

	/* HSRR interrupts leave MSR[RI] unchanged, SRR interrupts clear it. */
	ri_set = false;
	if (likely(trap > BOOK3S_INTERRUPT_MACHINE_CHECK)) {
		if (trap != BOOK3S_INTERRUPT_SYSCALL &&
				(vcpu->arch.shregs.msr & MSR_RI))
			ri_set = true;
		exsave = local_paca->exgen;
	} else if (trap == BOOK3S_INTERRUPT_SYSTEM_RESET) {
		exsave = local_paca->exnmi;
	} else { /* trap == 0x200 */
		exsave = local_paca->exmc;
	}

	vcpu->arch.regs.gpr[1] = local_paca->kvm_hstate.scratch1;
	vcpu->arch.regs.gpr[3] = local_paca->kvm_hstate.scratch2;

	/*
	 * Only set RI after reading machine check regs (DAR, DSISR, SRR0/1)
	 * and hstate scratch (which we need to move into exsave to make
	 * re-entrant vs SRESET/MCE)
	 */
	if (ri_set) {
		if (unlikely(!(mfmsr() & MSR_RI))) {
			__mtmsrd(MSR_RI, 1);
			WARN_ON_ONCE(1);
		}
	} else {
		WARN_ON_ONCE(mfmsr() & MSR_RI);
		__mtmsrd(MSR_RI, 1);
	}

	vcpu->arch.regs.gpr[9] = exsave[EX_R9/sizeof(u64)];
	vcpu->arch.regs.gpr[10] = exsave[EX_R10/sizeof(u64)];
	vcpu->arch.regs.gpr[11] = exsave[EX_R11/sizeof(u64)];
	vcpu->arch.regs.gpr[12] = exsave[EX_R12/sizeof(u64)];
	vcpu->arch.regs.gpr[13] = exsave[EX_R13/sizeof(u64)];
	vcpu->arch.ppr = exsave[EX_PPR/sizeof(u64)];
	vcpu->arch.cfar = exsave[EX_CFAR/sizeof(u64)];
	vcpu->arch.regs.ctr = exsave[EX_CTR/sizeof(u64)];

	vcpu->arch.last_inst = KVM_INST_FETCH_FAILED;

	if (unlikely(trap == BOOK3S_INTERRUPT_MACHINE_CHECK)) {
		vcpu->arch.fault_dar = exsave[EX_DAR/sizeof(u64)];
		vcpu->arch.fault_dsisr = exsave[EX_DSISR/sizeof(u64)];
		kvmppc_realmode_machine_check(vcpu);

	} else if (unlikely(trap == BOOK3S_INTERRUPT_HMI)) {
		kvmppc_realmode_hmi_handler();

	} else if (trap == BOOK3S_INTERRUPT_H_EMUL_ASSIST) {
		vcpu->arch.emul_inst = mfspr(SPRN_HEIR);

	} else if (trap == BOOK3S_INTERRUPT_H_DATA_STORAGE) {
		vcpu->arch.fault_dar = exsave[EX_DAR/sizeof(u64)];
		vcpu->arch.fault_dsisr = exsave[EX_DSISR/sizeof(u64)];
		vcpu->arch.fault_gpa = mfspr(SPRN_ASDR);

	} else if (trap == BOOK3S_INTERRUPT_H_INST_STORAGE) {
		vcpu->arch.fault_gpa = mfspr(SPRN_ASDR);

	} else if (trap == BOOK3S_INTERRUPT_H_FAC_UNAVAIL) {
		vcpu->arch.hfscr = mfspr(SPRN_HFSCR);

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	/*
	 * Softpatch interrupt for transactional memory emulation cases
	 * on POWER9 DD2.2.  This is early in the guest exit path - we
	 * haven't saved registers or done a treclaim yet.
	 */
	} else if (trap == BOOK3S_INTERRUPT_HV_SOFTPATCH) {
		vcpu->arch.emul_inst = mfspr(SPRN_HEIR);

		/*
		 * The cases we want to handle here are those where the guest
		 * is in real suspend mode and is trying to transition to
		 * transactional mode.
		 */
		if (!local_paca->kvm_hstate.fake_suspend &&
				(vcpu->arch.shregs.msr & MSR_TS_S)) {
			if (kvmhv_p9_tm_emulation_early(vcpu)) {
				/*
				 * Go straight back into the guest with the
				 * new NIP/MSR as set by TM emulation.
				 */
				mtspr(SPRN_HSRR0, vcpu->arch.regs.nip);
				mtspr(SPRN_HSRR1, vcpu->arch.shregs.msr);

				/*
				 * tm_return_to_guest re-loads SRR0/1, DAR,
				 * DSISR after RI is cleared, in case they had
				 * been clobbered by a MCE.
				 */
				__mtmsrd(0, 1); /* clear RI */
				goto tm_return_to_guest;
			}
		}
#endif
	}

	accumulate_time(vcpu, &vcpu->arch.rm_exit);

	/* Advance host PURR/SPURR by the amount used by guest */
	purr = mfspr(SPRN_PURR);
	spurr = mfspr(SPRN_SPURR);
	mtspr(SPRN_PURR, local_paca->kvm_hstate.host_purr +
	      purr - vcpu->arch.purr);
	mtspr(SPRN_SPURR, local_paca->kvm_hstate.host_spurr +
	      spurr - vcpu->arch.spurr);
	vcpu->arch.purr = purr;
	vcpu->arch.spurr = spurr;

	vcpu->arch.ic = mfspr(SPRN_IC);
	vcpu->arch.pid = mfspr(SPRN_PID);
	vcpu->arch.psscr = mfspr(SPRN_PSSCR) & PSSCR_GUEST_VIS;

	vcpu->arch.shregs.sprg0 = mfspr(SPRN_SPRG0);
	vcpu->arch.shregs.sprg1 = mfspr(SPRN_SPRG1);
	vcpu->arch.shregs.sprg2 = mfspr(SPRN_SPRG2);
	vcpu->arch.shregs.sprg3 = mfspr(SPRN_SPRG3);

	/* Preserve PSSCR[FAKE_SUSPEND] until we've called kvmppc_save_tm_hv */
	mtspr(SPRN_PSSCR, host_psscr |
	      (local_paca->kvm_hstate.fake_suspend << PSSCR_FAKE_SUSPEND_LG));
	mtspr(SPRN_HFSCR, host_hfscr);
	mtspr(SPRN_CIABR, host_ciabr);
	mtspr(SPRN_DAWR0, host_dawr0);
	mtspr(SPRN_DAWRX0, host_dawrx0);
	if (cpu_has_feature(CPU_FTR_DAWR1)) {
		mtspr(SPRN_DAWR1, host_dawr1);
		mtspr(SPRN_DAWRX1, host_dawrx1);
	}

	if (kvm_is_radix(kvm)) {
		/*
		 * Since this is radix, do a eieio; tlbsync; ptesync sequence
		 * in case we interrupted the guest between a tlbie and a
		 * ptesync.
		 */
		asm volatile("eieio; tlbsync; ptesync");
	}

	/*
	 * cp_abort is required if the processor supports local copy-paste
	 * to clear the copy buffer that was under control of the guest.
	 */
	if (cpu_has_feature(CPU_FTR_ARCH_31))
		asm volatile(PPC_CP_ABORT);

	vc->dpdes = mfspr(SPRN_DPDES);
	vc->vtb = mfspr(SPRN_VTB);
	mtspr(SPRN_DPDES, 0);
	if (vc->pcr)
		mtspr(SPRN_PCR, PCR_MASK);

	if (vc->tb_offset_applied) {
		u64 new_tb = mftb() - vc->tb_offset_applied;
		mtspr(SPRN_TBU40, new_tb);
		tb = mftb();
		if ((tb & 0xffffff) < (new_tb & 0xffffff))
			mtspr(SPRN_TBU40, new_tb + 0x1000000);
		vc->tb_offset_applied = 0;
	}

	mtspr(SPRN_HDEC, 0x7fffffff);

	save_clear_guest_mmu(kvm, vcpu);
	switch_mmu_to_host(kvm, host_pidr);
	local_paca->kvm_hstate.in_guest = KVM_GUEST_MODE_NONE;

	/*
	 * If we are in real mode, only switch MMU on after the MMU is
	 * switched to host, to avoid the P9_RADIX_PREFETCH_BUG.
	 */
	if (IS_ENABLED(CONFIG_PPC_TRANSACTIONAL_MEM) &&
	    vcpu->arch.shregs.msr & MSR_TS_MASK)
		msr |= MSR_TS_S;

	__mtmsrd(msr, 0);

	end_timing(vcpu);

	return trap;
}
EXPORT_SYMBOL_GPL(kvmhv_vcpu_entry_p9);
