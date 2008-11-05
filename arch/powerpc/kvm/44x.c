/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#include <linux/kvm_host.h>
#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/tlbflush.h>

#include "44x_tlb.h"

/* Note: clearing MSR[DE] just means that the debug interrupt will not be
 * delivered *immediately*. Instead, it simply sets the appropriate DBSR bits.
 * If those DBSR bits are still set when MSR[DE] is re-enabled, the interrupt
 * will be delivered as an "imprecise debug event" (which is indicated by
 * DBSR[IDE].
 */
static void kvm44x_disable_debug_interrupts(void)
{
	mtmsr(mfmsr() & ~MSR_DE);
}

void kvmppc_core_load_host_debugstate(struct kvm_vcpu *vcpu)
{
	kvm44x_disable_debug_interrupts();

	mtspr(SPRN_IAC1, vcpu->arch.host_iac[0]);
	mtspr(SPRN_IAC2, vcpu->arch.host_iac[1]);
	mtspr(SPRN_IAC3, vcpu->arch.host_iac[2]);
	mtspr(SPRN_IAC4, vcpu->arch.host_iac[3]);
	mtspr(SPRN_DBCR1, vcpu->arch.host_dbcr1);
	mtspr(SPRN_DBCR2, vcpu->arch.host_dbcr2);
	mtspr(SPRN_DBCR0, vcpu->arch.host_dbcr0);
	mtmsr(vcpu->arch.host_msr);
}

void kvmppc_core_load_guest_debugstate(struct kvm_vcpu *vcpu)
{
	struct kvm_guest_debug *dbg = &vcpu->guest_debug;
	u32 dbcr0 = 0;

	vcpu->arch.host_msr = mfmsr();
	kvm44x_disable_debug_interrupts();

	/* Save host debug register state. */
	vcpu->arch.host_iac[0] = mfspr(SPRN_IAC1);
	vcpu->arch.host_iac[1] = mfspr(SPRN_IAC2);
	vcpu->arch.host_iac[2] = mfspr(SPRN_IAC3);
	vcpu->arch.host_iac[3] = mfspr(SPRN_IAC4);
	vcpu->arch.host_dbcr0 = mfspr(SPRN_DBCR0);
	vcpu->arch.host_dbcr1 = mfspr(SPRN_DBCR1);
	vcpu->arch.host_dbcr2 = mfspr(SPRN_DBCR2);

	/* set registers up for guest */

	if (dbg->bp[0]) {
		mtspr(SPRN_IAC1, dbg->bp[0]);
		dbcr0 |= DBCR0_IAC1 | DBCR0_IDM;
	}
	if (dbg->bp[1]) {
		mtspr(SPRN_IAC2, dbg->bp[1]);
		dbcr0 |= DBCR0_IAC2 | DBCR0_IDM;
	}
	if (dbg->bp[2]) {
		mtspr(SPRN_IAC3, dbg->bp[2]);
		dbcr0 |= DBCR0_IAC3 | DBCR0_IDM;
	}
	if (dbg->bp[3]) {
		mtspr(SPRN_IAC4, dbg->bp[3]);
		dbcr0 |= DBCR0_IAC4 | DBCR0_IDM;
	}

	mtspr(SPRN_DBCR0, dbcr0);
	mtspr(SPRN_DBCR1, 0);
	mtspr(SPRN_DBCR2, 0);
}

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	int i;

	/* Mark every guest entry in the shadow TLB entry modified, so that they
	 * will all be reloaded on the next vcpu run (instead of being
	 * demand-faulted). */
	for (i = 0; i <= tlb_44x_hwater; i++)
		kvmppc_tlbe_set_modified(vcpu, i);
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
	/* Don't leave guest TLB entries resident when being de-scheduled. */
	/* XXX It would be nice to differentiate between heavyweight exit and
	 * sched_out here, since we could avoid the TLB flush for heavyweight
	 * exits. */
	_tlbia();
}

int kvmppc_core_check_processor_compat(void)
{
	int r;

	if (strcmp(cur_cpu_spec->platform, "ppc440") == 0)
		r = 0;
	else
		r = -ENOTSUPP;

	return r;
}
