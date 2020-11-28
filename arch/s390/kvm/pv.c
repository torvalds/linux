// SPDX-License-Identifier: GPL-2.0
/*
 * Hosting Protected Virtual Machines
 *
 * Copyright IBM Corp. 2019, 2020
 *    Author(s): Janosch Frank <frankja@linux.ibm.com>
 */
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/pagemap.h>
#include <linux/sched/signal.h>
#include <asm/gmap.h>
#include <asm/uv.h>
#include <asm/mman.h>
#include "kvm-s390.h"

int kvm_s390_pv_destroy_cpu(struct kvm_vcpu *vcpu, u16 *rc, u16 *rrc)
{
	int cc = 0;

	if (kvm_s390_pv_cpu_get_handle(vcpu)) {
		cc = uv_cmd_nodata(kvm_s390_pv_cpu_get_handle(vcpu),
				   UVC_CMD_DESTROY_SEC_CPU, rc, rrc);

		KVM_UV_EVENT(vcpu->kvm, 3,
			     "PROTVIRT DESTROY VCPU %d: rc %x rrc %x",
			     vcpu->vcpu_id, *rc, *rrc);
		WARN_ONCE(cc, "protvirt destroy cpu failed rc %x rrc %x",
			  *rc, *rrc);
	}
	/* Intended memory leak for something that should never happen. */
	if (!cc)
		free_pages(vcpu->arch.pv.stor_base,
			   get_order(uv_info.guest_cpu_stor_len));

	free_page(sida_origin(vcpu->arch.sie_block));
	vcpu->arch.sie_block->pv_handle_cpu = 0;
	vcpu->arch.sie_block->pv_handle_config = 0;
	memset(&vcpu->arch.pv, 0, sizeof(vcpu->arch.pv));
	vcpu->arch.sie_block->sdf = 0;
	/*
	 * The sidad field (for sdf == 2) is now the gbea field (for sdf == 0).
	 * Use the reset value of gbea to avoid leaking the kernel pointer of
	 * the just freed sida.
	 */
	vcpu->arch.sie_block->gbea = 1;
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);

	return cc ? EIO : 0;
}

int kvm_s390_pv_create_cpu(struct kvm_vcpu *vcpu, u16 *rc, u16 *rrc)
{
	struct uv_cb_csc uvcb = {
		.header.cmd = UVC_CMD_CREATE_SEC_CPU,
		.header.len = sizeof(uvcb),
	};
	int cc;

	if (kvm_s390_pv_cpu_get_handle(vcpu))
		return -EINVAL;

	vcpu->arch.pv.stor_base = __get_free_pages(GFP_KERNEL,
						   get_order(uv_info.guest_cpu_stor_len));
	if (!vcpu->arch.pv.stor_base)
		return -ENOMEM;

	/* Input */
	uvcb.guest_handle = kvm_s390_pv_get_handle(vcpu->kvm);
	uvcb.num = vcpu->arch.sie_block->icpua;
	uvcb.state_origin = (u64)vcpu->arch.sie_block;
	uvcb.stor_origin = (u64)vcpu->arch.pv.stor_base;

	/* Alloc Secure Instruction Data Area Designation */
	vcpu->arch.sie_block->sidad = __get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!vcpu->arch.sie_block->sidad) {
		free_pages(vcpu->arch.pv.stor_base,
			   get_order(uv_info.guest_cpu_stor_len));
		return -ENOMEM;
	}

	cc = uv_call(0, (u64)&uvcb);
	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;
	KVM_UV_EVENT(vcpu->kvm, 3,
		     "PROTVIRT CREATE VCPU: cpu %d handle %llx rc %x rrc %x",
		     vcpu->vcpu_id, uvcb.cpu_handle, uvcb.header.rc,
		     uvcb.header.rrc);

	if (cc) {
		u16 dummy;

		kvm_s390_pv_destroy_cpu(vcpu, &dummy, &dummy);
		return -EIO;
	}

	/* Output */
	vcpu->arch.pv.handle = uvcb.cpu_handle;
	vcpu->arch.sie_block->pv_handle_cpu = uvcb.cpu_handle;
	vcpu->arch.sie_block->pv_handle_config = kvm_s390_pv_get_handle(vcpu->kvm);
	vcpu->arch.sie_block->sdf = 2;
	kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
	return 0;
}

/* only free resources when the destroy was successful */
static void kvm_s390_pv_dealloc_vm(struct kvm *kvm)
{
	vfree(kvm->arch.pv.stor_var);
	free_pages(kvm->arch.pv.stor_base,
		   get_order(uv_info.guest_base_stor_len));
	memset(&kvm->arch.pv, 0, sizeof(kvm->arch.pv));
}

static int kvm_s390_pv_alloc_vm(struct kvm *kvm)
{
	unsigned long base = uv_info.guest_base_stor_len;
	unsigned long virt = uv_info.guest_virt_var_stor_len;
	unsigned long npages = 0, vlen = 0;
	struct kvm_memory_slot *memslot;

	kvm->arch.pv.stor_var = NULL;
	kvm->arch.pv.stor_base = __get_free_pages(GFP_KERNEL, get_order(base));
	if (!kvm->arch.pv.stor_base)
		return -ENOMEM;

	/*
	 * Calculate current guest storage for allocation of the
	 * variable storage, which is based on the length in MB.
	 *
	 * Slots are sorted by GFN
	 */
	mutex_lock(&kvm->slots_lock);
	memslot = kvm_memslots(kvm)->memslots;
	npages = memslot->base_gfn + memslot->npages;
	mutex_unlock(&kvm->slots_lock);

	kvm->arch.pv.guest_len = npages * PAGE_SIZE;

	/* Allocate variable storage */
	vlen = ALIGN(virt * ((npages * PAGE_SIZE) / HPAGE_SIZE), PAGE_SIZE);
	vlen += uv_info.guest_virt_base_stor_len;
	kvm->arch.pv.stor_var = vzalloc(vlen);
	if (!kvm->arch.pv.stor_var)
		goto out_err;
	return 0;

out_err:
	kvm_s390_pv_dealloc_vm(kvm);
	return -ENOMEM;
}

/* this should not fail, but if it does, we must not free the donated memory */
int kvm_s390_pv_deinit_vm(struct kvm *kvm, u16 *rc, u16 *rrc)
{
	int cc;

	/* make all pages accessible before destroying the guest */
	s390_reset_acc(kvm->mm);

	cc = uv_cmd_nodata(kvm_s390_pv_get_handle(kvm),
			   UVC_CMD_DESTROY_SEC_CONF, rc, rrc);
	WRITE_ONCE(kvm->arch.gmap->guest_handle, 0);
	atomic_set(&kvm->mm->context.is_protected, 0);
	KVM_UV_EVENT(kvm, 3, "PROTVIRT DESTROY VM: rc %x rrc %x", *rc, *rrc);
	WARN_ONCE(cc, "protvirt destroy vm failed rc %x rrc %x", *rc, *rrc);
	/* Inteded memory leak on "impossible" error */
	if (!cc)
		kvm_s390_pv_dealloc_vm(kvm);
	return cc ? -EIO : 0;
}

int kvm_s390_pv_init_vm(struct kvm *kvm, u16 *rc, u16 *rrc)
{
	struct uv_cb_cgc uvcb = {
		.header.cmd = UVC_CMD_CREATE_SEC_CONF,
		.header.len = sizeof(uvcb)
	};
	int cc, ret;
	u16 dummy;

	ret = kvm_s390_pv_alloc_vm(kvm);
	if (ret)
		return ret;

	/* Inputs */
	uvcb.guest_stor_origin = 0; /* MSO is 0 for KVM */
	uvcb.guest_stor_len = kvm->arch.pv.guest_len;
	uvcb.guest_asce = kvm->arch.gmap->asce;
	uvcb.guest_sca = (unsigned long)kvm->arch.sca;
	uvcb.conf_base_stor_origin = (u64)kvm->arch.pv.stor_base;
	uvcb.conf_virt_stor_origin = (u64)kvm->arch.pv.stor_var;

	cc = uv_call(0, (u64)&uvcb);
	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;
	KVM_UV_EVENT(kvm, 3, "PROTVIRT CREATE VM: handle %llx len %llx rc %x rrc %x",
		     uvcb.guest_handle, uvcb.guest_stor_len, *rc, *rrc);

	/* Outputs */
	kvm->arch.pv.handle = uvcb.guest_handle;

	if (cc) {
		if (uvcb.header.rc & UVC_RC_NEED_DESTROY)
			kvm_s390_pv_deinit_vm(kvm, &dummy, &dummy);
		else
			kvm_s390_pv_dealloc_vm(kvm);
		return -EIO;
	}
	kvm->arch.gmap->guest_handle = uvcb.guest_handle;
	return 0;
}

int kvm_s390_pv_set_sec_parms(struct kvm *kvm, void *hdr, u64 length, u16 *rc,
			      u16 *rrc)
{
	struct uv_cb_ssc uvcb = {
		.header.cmd = UVC_CMD_SET_SEC_CONF_PARAMS,
		.header.len = sizeof(uvcb),
		.sec_header_origin = (u64)hdr,
		.sec_header_len = length,
		.guest_handle = kvm_s390_pv_get_handle(kvm),
	};
	int cc = uv_call(0, (u64)&uvcb);

	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;
	KVM_UV_EVENT(kvm, 3, "PROTVIRT VM SET PARMS: rc %x rrc %x",
		     *rc, *rrc);
	if (!cc)
		atomic_set(&kvm->mm->context.is_protected, 1);
	return cc ? -EINVAL : 0;
}

static int unpack_one(struct kvm *kvm, unsigned long addr, u64 tweak,
		      u64 offset, u16 *rc, u16 *rrc)
{
	struct uv_cb_unp uvcb = {
		.header.cmd = UVC_CMD_UNPACK_IMG,
		.header.len = sizeof(uvcb),
		.guest_handle = kvm_s390_pv_get_handle(kvm),
		.gaddr = addr,
		.tweak[0] = tweak,
		.tweak[1] = offset,
	};
	int ret = gmap_make_secure(kvm->arch.gmap, addr, &uvcb);

	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;

	if (ret && ret != -EAGAIN)
		KVM_UV_EVENT(kvm, 3, "PROTVIRT VM UNPACK: failed addr %llx with rc %x rrc %x",
			     uvcb.gaddr, *rc, *rrc);
	return ret;
}

int kvm_s390_pv_unpack(struct kvm *kvm, unsigned long addr, unsigned long size,
		       unsigned long tweak, u16 *rc, u16 *rrc)
{
	u64 offset = 0;
	int ret = 0;

	if (addr & ~PAGE_MASK || !size || size & ~PAGE_MASK)
		return -EINVAL;

	KVM_UV_EVENT(kvm, 3, "PROTVIRT VM UNPACK: start addr %lx size %lx",
		     addr, size);

	while (offset < size) {
		ret = unpack_one(kvm, addr, tweak, offset, rc, rrc);
		if (ret == -EAGAIN) {
			cond_resched();
			if (fatal_signal_pending(current))
				break;
			continue;
		}
		if (ret)
			break;
		addr += PAGE_SIZE;
		offset += PAGE_SIZE;
	}
	if (!ret)
		KVM_UV_EVENT(kvm, 3, "%s", "PROTVIRT VM UNPACK: successful");
	return ret;
}

int kvm_s390_pv_set_cpu_state(struct kvm_vcpu *vcpu, u8 state)
{
	struct uv_cb_cpu_set_state uvcb = {
		.header.cmd	= UVC_CMD_CPU_SET_STATE,
		.header.len	= sizeof(uvcb),
		.cpu_handle	= kvm_s390_pv_cpu_get_handle(vcpu),
		.state		= state,
	};
	int cc;

	cc = uv_call(0, (u64)&uvcb);
	KVM_UV_EVENT(vcpu->kvm, 3, "PROTVIRT SET CPU %d STATE %d rc %x rrc %x",
		     vcpu->vcpu_id, state, uvcb.header.rc, uvcb.header.rrc);
	if (cc)
		return -EINVAL;
	return 0;
}
