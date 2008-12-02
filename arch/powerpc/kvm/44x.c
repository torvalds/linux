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
#include <linux/err.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/tlbflush.h>
#include <asm/kvm_44x.h>
#include <asm/kvm_ppc.h>

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
	kvmppc_44x_tlb_load(vcpu);
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
	kvmppc_44x_tlb_put(vcpu);
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

int kvmppc_core_vcpu_setup(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	struct kvmppc_44x_tlbe *tlbe = &vcpu_44x->guest_tlb[0];
	int i;

	tlbe->tid = 0;
	tlbe->word0 = PPC44x_TLB_16M | PPC44x_TLB_VALID;
	tlbe->word1 = 0;
	tlbe->word2 = PPC44x_TLB_SX | PPC44x_TLB_SW | PPC44x_TLB_SR;

	tlbe++;
	tlbe->tid = 0;
	tlbe->word0 = 0xef600000 | PPC44x_TLB_4K | PPC44x_TLB_VALID;
	tlbe->word1 = 0xef600000;
	tlbe->word2 = PPC44x_TLB_SX | PPC44x_TLB_SW | PPC44x_TLB_SR
	              | PPC44x_TLB_I | PPC44x_TLB_G;

	/* Since the guest can directly access the timebase, it must know the
	 * real timebase frequency. Accordingly, it must see the state of
	 * CCR1[TCS]. */
	vcpu->arch.ccr1 = mfspr(SPRN_CCR1);

	for (i = 0; i < ARRAY_SIZE(vcpu_44x->shadow_refs); i++)
		vcpu_44x->shadow_refs[i].gtlb_index = -1;

	return 0;
}

/* 'linear_address' is actually an encoding of AS|PID|EADDR . */
int kvmppc_core_vcpu_translate(struct kvm_vcpu *vcpu,
                               struct kvm_translation *tr)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);
	struct kvmppc_44x_tlbe *gtlbe;
	int index;
	gva_t eaddr;
	u8 pid;
	u8 as;

	eaddr = tr->linear_address;
	pid = (tr->linear_address >> 32) & 0xff;
	as = (tr->linear_address >> 40) & 0x1;

	index = kvmppc_44x_tlb_index(vcpu, eaddr, pid, as);
	if (index == -1) {
		tr->valid = 0;
		return 0;
	}

	gtlbe = &vcpu_44x->guest_tlb[index];

	tr->physical_address = tlb_xlate(gtlbe, eaddr);
	/* XXX what does "writeable" and "usermode" even mean? */
	tr->valid = 1;

	return 0;
}

struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm, unsigned int id)
{
	struct kvmppc_vcpu_44x *vcpu_44x;
	struct kvm_vcpu *vcpu;
	int err;

	vcpu_44x = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!vcpu_44x) {
		err = -ENOMEM;
		goto out;
	}

	vcpu = &vcpu_44x->vcpu;
	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	return vcpu;

free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vcpu_44x);
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);

	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu_44x);
}

static int kvmppc_44x_init(void)
{
	int r;

	r = kvmppc_booke_init();
	if (r)
		return r;

	return kvm_init(NULL, sizeof(struct kvmppc_vcpu_44x), THIS_MODULE);
}

static void kvmppc_44x_exit(void)
{
	kvmppc_booke_exit();
}

module_init(kvmppc_44x_init);
module_exit(kvmppc_44x_exit);
