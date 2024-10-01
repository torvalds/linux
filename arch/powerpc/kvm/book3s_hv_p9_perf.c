// SPDX-License-Identifier: GPL-2.0-only

#include <asm/kvm_ppc.h>
#include <asm/pmc.h>

#include "book3s_hv.h"

static void freeze_pmu(unsigned long mmcr0, unsigned long mmcra)
{
	if (!(mmcr0 & MMCR0_FC))
		goto do_freeze;
	if (mmcra & MMCRA_SAMPLE_ENABLE)
		goto do_freeze;
	if (cpu_has_feature(CPU_FTR_ARCH_31)) {
		if (!(mmcr0 & MMCR0_PMCCEXT))
			goto do_freeze;
		if (!(mmcra & MMCRA_BHRB_DISABLE))
			goto do_freeze;
	}
	return;

do_freeze:
	mmcr0 = MMCR0_FC;
	mmcra = 0;
	if (cpu_has_feature(CPU_FTR_ARCH_31)) {
		mmcr0 |= MMCR0_PMCCEXT;
		mmcra = MMCRA_BHRB_DISABLE;
	}

	mtspr(SPRN_MMCR0, mmcr0);
	mtspr(SPRN_MMCRA, mmcra);
	isync();
}

void switch_pmu_to_guest(struct kvm_vcpu *vcpu,
			 struct p9_host_os_sprs *host_os_sprs)
{
	struct lppaca *lp;
	int load_pmu = 1;

	lp = vcpu->arch.vpa.pinned_addr;
	if (lp)
		load_pmu = lp->pmcregs_in_use;

	/* Save host */
	if (ppc_get_pmu_inuse()) {
		/* POWER9, POWER10 do not implement HPMC or SPMC */

		host_os_sprs->mmcr0 = mfspr(SPRN_MMCR0);
		host_os_sprs->mmcra = mfspr(SPRN_MMCRA);

		freeze_pmu(host_os_sprs->mmcr0, host_os_sprs->mmcra);

		host_os_sprs->pmc1 = mfspr(SPRN_PMC1);
		host_os_sprs->pmc2 = mfspr(SPRN_PMC2);
		host_os_sprs->pmc3 = mfspr(SPRN_PMC3);
		host_os_sprs->pmc4 = mfspr(SPRN_PMC4);
		host_os_sprs->pmc5 = mfspr(SPRN_PMC5);
		host_os_sprs->pmc6 = mfspr(SPRN_PMC6);
		host_os_sprs->mmcr1 = mfspr(SPRN_MMCR1);
		host_os_sprs->mmcr2 = mfspr(SPRN_MMCR2);
		host_os_sprs->sdar = mfspr(SPRN_SDAR);
		host_os_sprs->siar = mfspr(SPRN_SIAR);
		host_os_sprs->sier1 = mfspr(SPRN_SIER);

		if (cpu_has_feature(CPU_FTR_ARCH_31)) {
			host_os_sprs->mmcr3 = mfspr(SPRN_MMCR3);
			host_os_sprs->sier2 = mfspr(SPRN_SIER2);
			host_os_sprs->sier3 = mfspr(SPRN_SIER3);
		}
	}

#ifdef CONFIG_PPC_PSERIES
	/* After saving PMU, before loading guest PMU, flip pmcregs_in_use */
	if (kvmhv_on_pseries()) {
		barrier();
		get_lppaca()->pmcregs_in_use = load_pmu;
		barrier();
	}
#endif

	/*
	 * Load guest. If the VPA said the PMCs are not in use but the guest
	 * tried to access them anyway, HFSCR[PM] will be set by the HFAC
	 * fault so we can make forward progress.
	 */
	if (load_pmu || (vcpu->arch.hfscr & HFSCR_PM)) {
		mtspr(SPRN_PMC1, vcpu->arch.pmc[0]);
		mtspr(SPRN_PMC2, vcpu->arch.pmc[1]);
		mtspr(SPRN_PMC3, vcpu->arch.pmc[2]);
		mtspr(SPRN_PMC4, vcpu->arch.pmc[3]);
		mtspr(SPRN_PMC5, vcpu->arch.pmc[4]);
		mtspr(SPRN_PMC6, vcpu->arch.pmc[5]);
		mtspr(SPRN_MMCR1, vcpu->arch.mmcr[1]);
		mtspr(SPRN_MMCR2, vcpu->arch.mmcr[2]);
		mtspr(SPRN_SDAR, vcpu->arch.sdar);
		mtspr(SPRN_SIAR, vcpu->arch.siar);
		mtspr(SPRN_SIER, vcpu->arch.sier[0]);

		if (cpu_has_feature(CPU_FTR_ARCH_31)) {
			mtspr(SPRN_MMCR3, vcpu->arch.mmcr[3]);
			mtspr(SPRN_SIER2, vcpu->arch.sier[1]);
			mtspr(SPRN_SIER3, vcpu->arch.sier[2]);
		}

		/* Set MMCRA then MMCR0 last */
		mtspr(SPRN_MMCRA, vcpu->arch.mmcra);
		mtspr(SPRN_MMCR0, vcpu->arch.mmcr[0]);
		/* No isync necessary because we're starting counters */

		if (!vcpu->arch.nested &&
		    (vcpu->arch.hfscr_permitted & HFSCR_PM))
			vcpu->arch.hfscr |= HFSCR_PM;
	}
}
EXPORT_SYMBOL_GPL(switch_pmu_to_guest);

void switch_pmu_to_host(struct kvm_vcpu *vcpu,
			struct p9_host_os_sprs *host_os_sprs)
{
	struct lppaca *lp;
	int save_pmu = 1;

	lp = vcpu->arch.vpa.pinned_addr;
	if (lp)
		save_pmu = lp->pmcregs_in_use;
	if (IS_ENABLED(CONFIG_KVM_BOOK3S_HV_NESTED_PMU_WORKAROUND)) {
		/*
		 * Save pmu if this guest is capable of running nested guests.
		 * This is option is for old L1s that do not set their
		 * lppaca->pmcregs_in_use properly when entering their L2.
		 */
		save_pmu |= nesting_enabled(vcpu->kvm);
	}

	if (save_pmu) {
		vcpu->arch.mmcr[0] = mfspr(SPRN_MMCR0);
		vcpu->arch.mmcra = mfspr(SPRN_MMCRA);

		freeze_pmu(vcpu->arch.mmcr[0], vcpu->arch.mmcra);

		vcpu->arch.pmc[0] = mfspr(SPRN_PMC1);
		vcpu->arch.pmc[1] = mfspr(SPRN_PMC2);
		vcpu->arch.pmc[2] = mfspr(SPRN_PMC3);
		vcpu->arch.pmc[3] = mfspr(SPRN_PMC4);
		vcpu->arch.pmc[4] = mfspr(SPRN_PMC5);
		vcpu->arch.pmc[5] = mfspr(SPRN_PMC6);
		vcpu->arch.mmcr[1] = mfspr(SPRN_MMCR1);
		vcpu->arch.mmcr[2] = mfspr(SPRN_MMCR2);
		vcpu->arch.sdar = mfspr(SPRN_SDAR);
		vcpu->arch.siar = mfspr(SPRN_SIAR);
		vcpu->arch.sier[0] = mfspr(SPRN_SIER);

		if (cpu_has_feature(CPU_FTR_ARCH_31)) {
			vcpu->arch.mmcr[3] = mfspr(SPRN_MMCR3);
			vcpu->arch.sier[1] = mfspr(SPRN_SIER2);
			vcpu->arch.sier[2] = mfspr(SPRN_SIER3);
		}

	} else if (vcpu->arch.hfscr & HFSCR_PM) {
		/*
		 * The guest accessed PMC SPRs without specifying they should
		 * be preserved, or it cleared pmcregs_in_use after the last
		 * access. Just ensure they are frozen.
		 */
		freeze_pmu(mfspr(SPRN_MMCR0), mfspr(SPRN_MMCRA));

		/*
		 * Demand-fault PMU register access in the guest.
		 *
		 * This is used to grab the guest's VPA pmcregs_in_use value
		 * and reflect it into the host's VPA in the case of a nested
		 * hypervisor.
		 *
		 * It also avoids having to zero-out SPRs after each guest
		 * exit to avoid side-channels when.
		 *
		 * This is cleared here when we exit the guest, so later HFSCR
		 * interrupt handling can add it back to run the guest with
		 * PM enabled next time.
		 */
		if (!vcpu->arch.nested)
			vcpu->arch.hfscr &= ~HFSCR_PM;
	} /* otherwise the PMU should still be frozen */

#ifdef CONFIG_PPC_PSERIES
	if (kvmhv_on_pseries()) {
		barrier();
		get_lppaca()->pmcregs_in_use = ppc_get_pmu_inuse();
		barrier();
	}
#endif

	if (ppc_get_pmu_inuse()) {
		mtspr(SPRN_PMC1, host_os_sprs->pmc1);
		mtspr(SPRN_PMC2, host_os_sprs->pmc2);
		mtspr(SPRN_PMC3, host_os_sprs->pmc3);
		mtspr(SPRN_PMC4, host_os_sprs->pmc4);
		mtspr(SPRN_PMC5, host_os_sprs->pmc5);
		mtspr(SPRN_PMC6, host_os_sprs->pmc6);
		mtspr(SPRN_MMCR1, host_os_sprs->mmcr1);
		mtspr(SPRN_MMCR2, host_os_sprs->mmcr2);
		mtspr(SPRN_SDAR, host_os_sprs->sdar);
		mtspr(SPRN_SIAR, host_os_sprs->siar);
		mtspr(SPRN_SIER, host_os_sprs->sier1);

		if (cpu_has_feature(CPU_FTR_ARCH_31)) {
			mtspr(SPRN_MMCR3, host_os_sprs->mmcr3);
			mtspr(SPRN_SIER2, host_os_sprs->sier2);
			mtspr(SPRN_SIER3, host_os_sprs->sier3);
		}

		/* Set MMCRA then MMCR0 last */
		mtspr(SPRN_MMCRA, host_os_sprs->mmcra);
		mtspr(SPRN_MMCR0, host_os_sprs->mmcr0);
		isync();
	}
}
EXPORT_SYMBOL_GPL(switch_pmu_to_host);
