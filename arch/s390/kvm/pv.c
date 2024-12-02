// SPDX-License-Identifier: GPL-2.0
/*
 * Hosting Protected Virtual Machines
 *
 * Copyright IBM Corp. 2019, 2020
 *    Author(s): Janosch Frank <frankja@linux.ibm.com>
 */
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/minmax.h>
#include <linux/pagemap.h>
#include <linux/sched/signal.h>
#include <asm/gmap.h>
#include <asm/uv.h>
#include <asm/mman.h>
#include <linux/pagewalk.h>
#include <linux/sched/mm.h>
#include <linux/mmu_notifier.h>
#include "kvm-s390.h"

static void kvm_s390_clear_pv_state(struct kvm *kvm)
{
	kvm->arch.pv.handle = 0;
	kvm->arch.pv.guest_len = 0;
	kvm->arch.pv.stor_base = 0;
	kvm->arch.pv.stor_var = NULL;
}

int kvm_s390_pv_destroy_cpu(struct kvm_vcpu *vcpu, u16 *rc, u16 *rrc)
{
	int cc;

	if (!kvm_s390_pv_cpu_get_handle(vcpu))
		return 0;

	cc = uv_cmd_nodata(kvm_s390_pv_cpu_get_handle(vcpu), UVC_CMD_DESTROY_SEC_CPU, rc, rrc);

	KVM_UV_EVENT(vcpu->kvm, 3, "PROTVIRT DESTROY VCPU %d: rc %x rrc %x",
		     vcpu->vcpu_id, *rc, *rrc);
	WARN_ONCE(cc, "protvirt destroy cpu failed rc %x rrc %x", *rc, *rrc);

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

	vcpu->arch.pv.stor_base = __get_free_pages(GFP_KERNEL_ACCOUNT,
						   get_order(uv_info.guest_cpu_stor_len));
	if (!vcpu->arch.pv.stor_base)
		return -ENOMEM;

	/* Input */
	uvcb.guest_handle = kvm_s390_pv_get_handle(vcpu->kvm);
	uvcb.num = vcpu->arch.sie_block->icpua;
	uvcb.state_origin = (u64)vcpu->arch.sie_block;
	uvcb.stor_origin = (u64)vcpu->arch.pv.stor_base;

	/* Alloc Secure Instruction Data Area Designation */
	vcpu->arch.sie_block->sidad = __get_free_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
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
	kvm_s390_clear_pv_state(kvm);
}

static int kvm_s390_pv_alloc_vm(struct kvm *kvm)
{
	unsigned long base = uv_info.guest_base_stor_len;
	unsigned long virt = uv_info.guest_virt_var_stor_len;
	unsigned long npages = 0, vlen = 0;

	kvm->arch.pv.stor_var = NULL;
	kvm->arch.pv.stor_base = __get_free_pages(GFP_KERNEL_ACCOUNT, get_order(base));
	if (!kvm->arch.pv.stor_base)
		return -ENOMEM;

	/*
	 * Calculate current guest storage for allocation of the
	 * variable storage, which is based on the length in MB.
	 *
	 * Slots are sorted by GFN
	 */
	mutex_lock(&kvm->slots_lock);
	npages = kvm_s390_get_gfn_end(kvm_memslots(kvm));
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

	cc = uv_cmd_nodata(kvm_s390_pv_get_handle(kvm),
			   UVC_CMD_DESTROY_SEC_CONF, rc, rrc);
	WRITE_ONCE(kvm->arch.gmap->guest_handle, 0);
	/*
	 * if the mm still has a mapping, make all its pages accessible
	 * before destroying the guest
	 */
	if (mmget_not_zero(kvm->mm)) {
		s390_uv_destroy_range(kvm->mm, 0, TASK_SIZE);
		mmput(kvm->mm);
	}

	if (!cc) {
		atomic_dec(&kvm->mm->context.protected_count);
		kvm_s390_pv_dealloc_vm(kvm);
	} else {
		/* Intended memory leak on "impossible" error */
		s390_replace_asce(kvm->arch.gmap);
	}
	KVM_UV_EVENT(kvm, 3, "PROTVIRT DESTROY VM: rc %x rrc %x", *rc, *rrc);
	WARN_ONCE(cc, "protvirt destroy vm failed rc %x rrc %x", *rc, *rrc);

	return cc ? -EIO : 0;
}

static void kvm_s390_pv_mmu_notifier_release(struct mmu_notifier *subscription,
					     struct mm_struct *mm)
{
	struct kvm *kvm = container_of(subscription, struct kvm, arch.pv.mmu_notifier);
	u16 dummy;

	/*
	 * No locking is needed since this is the last thread of the last user of this
	 * struct mm.
	 * When the struct kvm gets deinitialized, this notifier is also
	 * unregistered. This means that if this notifier runs, then the
	 * struct kvm is still valid.
	 */
	kvm_s390_cpus_from_pv(kvm, &dummy, &dummy);
}

static const struct mmu_notifier_ops kvm_s390_pv_mmu_notifier_ops = {
	.release = kvm_s390_pv_mmu_notifier_release,
};

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

	cc = uv_call_sched(0, (u64)&uvcb);
	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;
	KVM_UV_EVENT(kvm, 3, "PROTVIRT CREATE VM: handle %llx len %llx rc %x rrc %x",
		     uvcb.guest_handle, uvcb.guest_stor_len, *rc, *rrc);

	/* Outputs */
	kvm->arch.pv.handle = uvcb.guest_handle;

	atomic_inc(&kvm->mm->context.protected_count);
	if (cc) {
		if (uvcb.header.rc & UVC_RC_NEED_DESTROY) {
			kvm_s390_pv_deinit_vm(kvm, &dummy, &dummy);
		} else {
			atomic_dec(&kvm->mm->context.protected_count);
			kvm_s390_pv_dealloc_vm(kvm);
		}
		return -EIO;
	}
	kvm->arch.gmap->guest_handle = uvcb.guest_handle;
	/* Add the notifier only once. No races because we hold kvm->lock */
	if (kvm->arch.pv.mmu_notifier.ops != &kvm_s390_pv_mmu_notifier_ops) {
		kvm->arch.pv.mmu_notifier.ops = &kvm_s390_pv_mmu_notifier_ops;
		mmu_notifier_register(&kvm->arch.pv.mmu_notifier, kvm->mm);
	}
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

int kvm_s390_pv_dump_cpu(struct kvm_vcpu *vcpu, void *buff, u16 *rc, u16 *rrc)
{
	struct uv_cb_dump_cpu uvcb = {
		.header.cmd = UVC_CMD_DUMP_CPU,
		.header.len = sizeof(uvcb),
		.cpu_handle = vcpu->arch.pv.handle,
		.dump_area_origin = (u64)buff,
	};
	int cc;

	cc = uv_call_sched(0, (u64)&uvcb);
	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;
	return cc;
}

/* Size of the cache for the storage state dump data. 1MB for now */
#define DUMP_BUFF_LEN HPAGE_SIZE

/**
 * kvm_s390_pv_dump_stor_state
 *
 * @kvm: pointer to the guest's KVM struct
 * @buff_user: Userspace pointer where we will write the results to
 * @gaddr: Starting absolute guest address for which the storage state
 *	   is requested.
 * @buff_user_len: Length of the buff_user buffer
 * @rc: Pointer to where the uvcb return code is stored
 * @rrc: Pointer to where the uvcb return reason code is stored
 *
 * Stores buff_len bytes of tweak component values to buff_user
 * starting with the 1MB block specified by the absolute guest address
 * (gaddr). The gaddr pointer will be updated with the last address
 * for which data was written when returning to userspace. buff_user
 * might be written to even if an error rc is returned. For instance
 * if we encounter a fault after writing the first page of data.
 *
 * Context: kvm->lock needs to be held
 *
 * Return:
 *  0 on success
 *  -ENOMEM if allocating the cache fails
 *  -EINVAL if gaddr is not aligned to 1MB
 *  -EINVAL if buff_user_len is not aligned to uv_info.conf_dump_storage_state_len
 *  -EINVAL if the UV call fails, rc and rrc will be set in this case
 *  -EFAULT if copying the result to buff_user failed
 */
int kvm_s390_pv_dump_stor_state(struct kvm *kvm, void __user *buff_user,
				u64 *gaddr, u64 buff_user_len, u16 *rc, u16 *rrc)
{
	struct uv_cb_dump_stor_state uvcb = {
		.header.cmd = UVC_CMD_DUMP_CONF_STOR_STATE,
		.header.len = sizeof(uvcb),
		.config_handle = kvm->arch.pv.handle,
		.gaddr = *gaddr,
		.dump_area_origin = 0,
	};
	const u64 increment_len = uv_info.conf_dump_storage_state_len;
	size_t buff_kvm_size;
	size_t size_done = 0;
	u8 *buff_kvm = NULL;
	int cc, ret;

	ret = -EINVAL;
	/* UV call processes 1MB guest storage chunks at a time */
	if (!IS_ALIGNED(*gaddr, HPAGE_SIZE))
		goto out;

	/*
	 * We provide the storage state for 1MB chunks of guest
	 * storage. The buffer will need to be aligned to
	 * conf_dump_storage_state_len so we don't end on a partial
	 * chunk.
	 */
	if (!buff_user_len ||
	    !IS_ALIGNED(buff_user_len, increment_len))
		goto out;

	/*
	 * Allocate a buffer from which we will later copy to the user
	 * process. We don't want userspace to dictate our buffer size
	 * so we limit it to DUMP_BUFF_LEN.
	 */
	ret = -ENOMEM;
	buff_kvm_size = min_t(u64, buff_user_len, DUMP_BUFF_LEN);
	buff_kvm = vzalloc(buff_kvm_size);
	if (!buff_kvm)
		goto out;

	ret = 0;
	uvcb.dump_area_origin = (u64)buff_kvm;
	/* We will loop until the user buffer is filled or an error occurs */
	do {
		/* Get 1MB worth of guest storage state data */
		cc = uv_call_sched(0, (u64)&uvcb);

		/* All or nothing */
		if (cc) {
			ret = -EINVAL;
			break;
		}

		size_done += increment_len;
		uvcb.dump_area_origin += increment_len;
		buff_user_len -= increment_len;
		uvcb.gaddr += HPAGE_SIZE;

		/* KVM Buffer full, time to copy to the process */
		if (!buff_user_len || size_done == DUMP_BUFF_LEN) {
			if (copy_to_user(buff_user, buff_kvm, size_done)) {
				ret = -EFAULT;
				break;
			}

			buff_user += size_done;
			size_done = 0;
			uvcb.dump_area_origin = (u64)buff_kvm;
		}
	} while (buff_user_len);

	/* Report back where we ended dumping */
	*gaddr = uvcb.gaddr;

	/* Lets only log errors, we don't want to spam */
out:
	if (ret)
		KVM_UV_EVENT(kvm, 3,
			     "PROTVIRT DUMP STORAGE STATE: addr %llx ret %d, uvcb rc %x rrc %x",
			     uvcb.gaddr, ret, uvcb.header.rc, uvcb.header.rrc);
	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;
	vfree(buff_kvm);

	return ret;
}

/**
 * kvm_s390_pv_dump_complete
 *
 * @kvm: pointer to the guest's KVM struct
 * @buff_user: Userspace pointer where we will write the results to
 * @rc: Pointer to where the uvcb return code is stored
 * @rrc: Pointer to where the uvcb return reason code is stored
 *
 * Completes the dumping operation and writes the completion data to
 * user space.
 *
 * Context: kvm->lock needs to be held
 *
 * Return:
 *  0 on success
 *  -ENOMEM if allocating the completion buffer fails
 *  -EINVAL if the UV call fails, rc and rrc will be set in this case
 *  -EFAULT if copying the result to buff_user failed
 */
int kvm_s390_pv_dump_complete(struct kvm *kvm, void __user *buff_user,
			      u16 *rc, u16 *rrc)
{
	struct uv_cb_dump_complete complete = {
		.header.len = sizeof(complete),
		.header.cmd = UVC_CMD_DUMP_COMPLETE,
		.config_handle = kvm_s390_pv_get_handle(kvm),
	};
	u64 *compl_data;
	int ret;

	/* Allocate dump area */
	compl_data = vzalloc(uv_info.conf_dump_finalize_len);
	if (!compl_data)
		return -ENOMEM;
	complete.dump_area_origin = (u64)compl_data;

	ret = uv_call_sched(0, (u64)&complete);
	*rc = complete.header.rc;
	*rrc = complete.header.rrc;
	KVM_UV_EVENT(kvm, 3, "PROTVIRT DUMP COMPLETE: rc %x rrc %x",
		     complete.header.rc, complete.header.rrc);

	if (!ret) {
		/*
		 * kvm_s390_pv_dealloc_vm() will also (mem)set
		 * this to false on a reboot or other destroy
		 * operation for this vm.
		 */
		kvm->arch.pv.dumping = false;
		kvm_s390_vcpu_unblock_all(kvm);
		ret = copy_to_user(buff_user, compl_data, uv_info.conf_dump_finalize_len);
		if (ret)
			ret = -EFAULT;
	}
	vfree(compl_data);
	/* If the UVC returned an error, translate it to -EINVAL */
	if (ret > 0)
		ret = -EINVAL;
	return ret;
}
