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
#include <linux/slab.h>
#include <linux/err.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/tlbflush.h>
#include <asm/kvm_44x.h>
#include <asm/kvm_ppc.h>

#include "44x_tlb.h"

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

	if (strncmp(cur_cpu_spec->platform, "ppc440", 6) == 0)
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
	/* XXX CCR1 doesn't exist on all 440 SoCs. */
	vcpu->arch.ccr1 = mfspr(SPRN_CCR1);

	for (i = 0; i < ARRAY_SIZE(vcpu_44x->shadow_refs); i++)
		vcpu_44x->shadow_refs[i].gtlb_index = -1;

	return 0;
}

/* 'linear_address' is actually an encoding of AS|PID|EADDR . */
int kvmppc_core_vcpu_translate(struct kvm_vcpu *vcpu,
                               struct kvm_translation *tr)
{
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

	tr->physical_address = kvmppc_mmu_xlate(vcpu, index, eaddr);
	/* XXX what does "writeable" and "usermode" even mean? */
	tr->valid = 1;

	return 0;
}

void kvmppc_core_get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	kvmppc_get_sregs_ivor(vcpu, sregs);
}

int kvmppc_core_set_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs)
{
	return kvmppc_set_sregs_ivor(vcpu, sregs);
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

	vcpu->arch.shared = (void*)__get_free_page(GFP_KERNEL|__GFP_ZERO);
	if (!vcpu->arch.shared)
		goto uninit_vcpu;

	return vcpu;

uninit_vcpu:
	kvm_vcpu_uninit(vcpu);
free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vcpu_44x);
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_44x *vcpu_44x = to_44x(vcpu);

	free_page((unsigned long)vcpu->arch.shared);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu_44x);
}

static int __init kvmppc_44x_init(void)
{
	int r;

	r = kvmppc_booke_init();
	if (r)
		return r;

	return kvm_init(NULL, sizeof(struct kvmppc_vcpu_44x), 0, THIS_MODULE);
}

static void __exit kvmppc_44x_exit(void)
{
	kvmppc_booke_exit();
}

module_init(kvmppc_44x_init);
module_exit(kvmppc_44x_exit);
