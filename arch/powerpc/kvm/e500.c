/*
 * Copyright (C) 2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu, <yu.liu@freescale.com>
 *
 * Description:
 * This file is derived from arch/powerpc/kvm/44x.c,
 * by Hollis Blanchard <hollisb@us.ibm.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/kvm_host.h>
#include <linux/err.h>

#include <asm/reg.h>
#include <asm/cputable.h>
#include <asm/tlbflush.h>
#include <asm/kvm_e500.h>
#include <asm/kvm_ppc.h>

#include "booke.h"
#include "e500_tlb.h"

void kvmppc_core_load_host_debugstate(struct kvm_vcpu *vcpu)
{
}

void kvmppc_core_load_guest_debugstate(struct kvm_vcpu *vcpu)
{
}

void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	kvmppc_e500_tlb_load(vcpu, cpu);
}

void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu)
{
	kvmppc_e500_tlb_put(vcpu);
}

int kvmppc_core_check_processor_compat(void)
{
	int r;

	if (strcmp(cur_cpu_spec->cpu_name, "e500v2") == 0)
		r = 0;
	else
		r = -ENOTSUPP;

	return r;
}

int kvmppc_core_vcpu_setup(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	kvmppc_e500_tlb_setup(vcpu_e500);

	/* Use the same core vertion as host's */
	vcpu->arch.pvr = mfspr(SPRN_PVR);

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

	index = kvmppc_e500_tlb_search(vcpu, eaddr, pid, as);
	if (index < 0) {
		tr->valid = 0;
		return 0;
	}

	tr->physical_address = kvmppc_mmu_xlate(vcpu, index, eaddr);
	/* XXX what does "writeable" and "usermode" even mean? */
	tr->valid = 1;

	return 0;
}

struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm, unsigned int id)
{
	struct kvmppc_vcpu_e500 *vcpu_e500;
	struct kvm_vcpu *vcpu;
	int err;

	vcpu_e500 = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!vcpu_e500) {
		err = -ENOMEM;
		goto out;
	}

	vcpu = &vcpu_e500->vcpu;
	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	err = kvmppc_e500_tlb_init(vcpu_e500);
	if (err)
		goto uninit_vcpu;

	return vcpu;

uninit_vcpu:
	kvm_vcpu_uninit(vcpu);
free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vcpu_e500);
out:
	return ERR_PTR(err);
}

void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu)
{
	struct kvmppc_vcpu_e500 *vcpu_e500 = to_e500(vcpu);

	kvmppc_e500_tlb_uninit(vcpu_e500);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu_e500);
}

static int kvmppc_e500_init(void)
{
	int r, i;
	unsigned long ivor[3];
	unsigned long max_ivor = 0;

	r = kvmppc_booke_init();
	if (r)
		return r;

	/* copy extra E500 exception handlers */
	ivor[0] = mfspr(SPRN_IVOR32);
	ivor[1] = mfspr(SPRN_IVOR33);
	ivor[2] = mfspr(SPRN_IVOR34);
	for (i = 0; i < 3; i++) {
		if (ivor[i] > max_ivor)
			max_ivor = ivor[i];

		memcpy((void *)kvmppc_booke_handlers + ivor[i],
		       kvmppc_handlers_start + (i + 16) * kvmppc_handler_len,
		       kvmppc_handler_len);
	}
	flush_icache_range(kvmppc_booke_handlers,
			kvmppc_booke_handlers + max_ivor + kvmppc_handler_len);

	return kvm_init(NULL, sizeof(struct kvmppc_vcpu_e500), THIS_MODULE);
}

static void kvmppc_e500_exit(void)
{
	kvmppc_booke_exit();
}

module_init(kvmppc_e500_init);
module_exit(kvmppc_e500_exit);
