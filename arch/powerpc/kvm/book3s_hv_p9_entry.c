// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <asm/asm-prototypes.h>
#include <asm/dbell.h>
#include <asm/kvm_ppc.h>

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

static inline void mtslb(u64 slbee, u64 slbev)
{
	asm volatile("slbmte %0,%1" :: "r" (slbev), "r" (slbee));
}

static inline void clear_slb_entry(unsigned int idx)
{
	mtslb(idx, 0);
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

int __kvmhv_vcpu_entry_p9(struct kvm_vcpu *vcpu)
{
	u64 *exsave;
	unsigned long msr = mfmsr();
	int trap;

	start_timing(vcpu, &vcpu->arch.rm_entry);

	vcpu->arch.ceded = 0;

	WARN_ON_ONCE(vcpu->arch.shregs.msr & MSR_HV);
	WARN_ON_ONCE(!(vcpu->arch.shregs.msr & MSR_ME));

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

	accumulate_time(vcpu, &vcpu->arch.guest_time);

	local_paca->kvm_hstate.in_guest = KVM_GUEST_MODE_HV_FAST;
	kvmppc_p9_enter_guest(vcpu);
	// Radix host and guest means host never runs with guest MMU state
	local_paca->kvm_hstate.in_guest = KVM_GUEST_MODE_NONE;

	accumulate_time(vcpu, &vcpu->arch.rm_intr);

	/* XXX: Could get these from r11/12 and paca exsave instead */
	vcpu->arch.shregs.srr0 = mfspr(SPRN_SRR0);
	vcpu->arch.shregs.srr1 = mfspr(SPRN_SRR1);
	vcpu->arch.shregs.dar = mfspr(SPRN_DAR);
	vcpu->arch.shregs.dsisr = mfspr(SPRN_DSISR);

	/* 0x2 bit for HSRR is only used by PR and P7/8 HV paths, clear it */
	trap = local_paca->kvm_hstate.scratch0 & ~0x2;
	if (likely(trap > BOOK3S_INTERRUPT_MACHINE_CHECK)) {
		exsave = local_paca->exgen;
	} else if (trap == BOOK3S_INTERRUPT_SYSTEM_RESET) {
		exsave = local_paca->exnmi;
	} else { /* trap == 0x200 */
		exsave = local_paca->exmc;
	}

	vcpu->arch.regs.gpr[1] = local_paca->kvm_hstate.scratch1;
	vcpu->arch.regs.gpr[3] = local_paca->kvm_hstate.scratch2;
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
		if (local_paca->kvm_hstate.fake_suspend &&
				(vcpu->arch.shregs.msr & MSR_TS_S)) {
			if (kvmhv_p9_tm_emulation_early(vcpu)) {
				/* Prevent it being handled again. */
				trap = 0;
			}
		}
#endif
	}

	radix_clear_slb();

	__mtmsrd(msr, 0);

	accumulate_time(vcpu, &vcpu->arch.rm_exit);

	end_timing(vcpu);

	return trap;
}
EXPORT_SYMBOL_GPL(__kvmhv_vcpu_entry_p9);
