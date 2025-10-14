// SPDX-License-Identifier: GPL-2.0
/*
 * Hosting Protected Virtual Machines
 *
 * Copyright IBM Corp. 2019, 2020
 *    Author(s): Janosch Frank <frankja@linux.ibm.com>
 */

#include <linux/export.h>
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

bool kvm_s390_pv_is_protected(struct kvm *kvm)
{
	lockdep_assert_held(&kvm->lock);
	return !!kvm_s390_pv_get_handle(kvm);
}
EXPORT_SYMBOL_GPL(kvm_s390_pv_is_protected);

bool kvm_s390_pv_cpu_is_protected(struct kvm_vcpu *vcpu)
{
	lockdep_assert_held(&vcpu->mutex);
	return !!kvm_s390_pv_cpu_get_handle(vcpu);
}
EXPORT_SYMBOL_GPL(kvm_s390_pv_cpu_is_protected);

/**
 * kvm_s390_pv_make_secure() - make one guest page secure
 * @kvm: the guest
 * @gaddr: the guest address that needs to be made secure
 * @uvcb: the UVCB specifying which operation needs to be performed
 *
 * Context: needs to be called with kvm->srcu held.
 * Return: 0 on success, < 0 in case of error.
 */
int kvm_s390_pv_make_secure(struct kvm *kvm, unsigned long gaddr, void *uvcb)
{
	unsigned long vmaddr;

	lockdep_assert_held(&kvm->srcu);

	vmaddr = gfn_to_hva(kvm, gpa_to_gfn(gaddr));
	if (kvm_is_error_hva(vmaddr))
		return -EFAULT;
	return make_hva_secure(kvm->mm, vmaddr, uvcb);
}

int kvm_s390_pv_convert_to_secure(struct kvm *kvm, unsigned long gaddr)
{
	struct uv_cb_cts uvcb = {
		.header.cmd = UVC_CMD_CONV_TO_SEC_STOR,
		.header.len = sizeof(uvcb),
		.guest_handle = kvm_s390_pv_get_handle(kvm),
		.gaddr = gaddr,
	};

	return kvm_s390_pv_make_secure(kvm, gaddr, &uvcb);
}

/**
 * kvm_s390_pv_destroy_page() - Destroy a guest page.
 * @kvm: the guest
 * @gaddr: the guest address to destroy
 *
 * An attempt will be made to destroy the given guest page. If the attempt
 * fails, an attempt is made to export the page. If both attempts fail, an
 * appropriate error is returned.
 *
 * Context: may sleep.
 */
int kvm_s390_pv_destroy_page(struct kvm *kvm, unsigned long gaddr)
{
	struct page *page;
	int rc = 0;

	mmap_read_lock(kvm->mm);
	page = gfn_to_page(kvm, gpa_to_gfn(gaddr));
	if (page)
		rc = __kvm_s390_pv_destroy_page(page);
	kvm_release_page_clean(page);
	mmap_read_unlock(kvm->mm);
	return rc;
}

/**
 * struct pv_vm_to_be_destroyed - Represents a protected VM that needs to
 * be destroyed
 *
 * @list: list head for the list of leftover VMs
 * @old_gmap_table: the gmap table of the leftover protected VM
 * @handle: the handle of the leftover protected VM
 * @stor_var: pointer to the variable storage of the leftover protected VM
 * @stor_base: address of the base storage of the leftover protected VM
 *
 * Represents a protected VM that is still registered with the Ultravisor,
 * but which does not correspond any longer to an active KVM VM. It should
 * be destroyed at some point later, either asynchronously or when the
 * process terminates.
 */
struct pv_vm_to_be_destroyed {
	struct list_head list;
	unsigned long old_gmap_table;
	u64 handle;
	void *stor_var;
	unsigned long stor_base;
};

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

	free_page((unsigned long)sida_addr(vcpu->arch.sie_block));
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
	void *sida_addr;
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
	uvcb.state_origin = virt_to_phys(vcpu->arch.sie_block);
	uvcb.stor_origin = virt_to_phys((void *)vcpu->arch.pv.stor_base);

	/* Alloc Secure Instruction Data Area Designation */
	sida_addr = (void *)__get_free_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!sida_addr) {
		free_pages(vcpu->arch.pv.stor_base,
			   get_order(uv_info.guest_cpu_stor_len));
		return -ENOMEM;
	}
	vcpu->arch.sie_block->sidad = virt_to_phys(sida_addr);

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

/**
 * kvm_s390_pv_dispose_one_leftover - Clean up one leftover protected VM.
 * @kvm: the KVM that was associated with this leftover protected VM
 * @leftover: details about the leftover protected VM that needs a clean up
 * @rc: the RC code of the Destroy Secure Configuration UVC
 * @rrc: the RRC code of the Destroy Secure Configuration UVC
 *
 * Destroy one leftover protected VM.
 * On success, kvm->mm->context.protected_count will be decremented atomically
 * and all other resources used by the VM will be freed.
 *
 * Return: 0 in case of success, otherwise 1
 */
static int kvm_s390_pv_dispose_one_leftover(struct kvm *kvm,
					    struct pv_vm_to_be_destroyed *leftover,
					    u16 *rc, u16 *rrc)
{
	int cc;

	/* It used the destroy-fast UVC, nothing left to do here */
	if (!leftover->handle)
		goto done_fast;
	cc = uv_cmd_nodata(leftover->handle, UVC_CMD_DESTROY_SEC_CONF, rc, rrc);
	KVM_UV_EVENT(kvm, 3, "PROTVIRT DESTROY LEFTOVER VM: rc %x rrc %x", *rc, *rrc);
	WARN_ONCE(cc, "protvirt destroy leftover vm failed rc %x rrc %x", *rc, *rrc);
	if (cc)
		return cc;
	/*
	 * Intentionally leak unusable memory. If the UVC fails, the memory
	 * used for the VM and its metadata is permanently unusable.
	 * This can only happen in case of a serious KVM or hardware bug; it
	 * is not expected to happen in normal operation.
	 */
	free_pages(leftover->stor_base, get_order(uv_info.guest_base_stor_len));
	free_pages(leftover->old_gmap_table, CRST_ALLOC_ORDER);
	vfree(leftover->stor_var);
done_fast:
	atomic_dec(&kvm->mm->context.protected_count);
	return 0;
}

/**
 * kvm_s390_destroy_lower_2g - Destroy the first 2GB of protected guest memory.
 * @kvm: the VM whose memory is to be cleared.
 *
 * Destroy the first 2GB of guest memory, to avoid prefix issues after reboot.
 * The CPUs of the protected VM need to be destroyed beforehand.
 */
static void kvm_s390_destroy_lower_2g(struct kvm *kvm)
{
	const unsigned long pages_2g = SZ_2G / PAGE_SIZE;
	struct kvm_memory_slot *slot;
	unsigned long len;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&kvm->srcu);

	/* Take the memslot containing guest absolute address 0 */
	slot = gfn_to_memslot(kvm, 0);
	/* Clear all slots or parts thereof that are below 2GB */
	while (slot && slot->base_gfn < pages_2g) {
		len = min_t(u64, slot->npages, pages_2g - slot->base_gfn) * PAGE_SIZE;
		s390_uv_destroy_range(kvm->mm, slot->userspace_addr, slot->userspace_addr + len);
		/* Take the next memslot */
		slot = gfn_to_memslot(kvm, slot->base_gfn + slot->npages);
	}

	srcu_read_unlock(&kvm->srcu, srcu_idx);
}

static int kvm_s390_pv_deinit_vm_fast(struct kvm *kvm, u16 *rc, u16 *rrc)
{
	struct uv_cb_destroy_fast uvcb = {
		.header.cmd = UVC_CMD_DESTROY_SEC_CONF_FAST,
		.header.len = sizeof(uvcb),
		.handle = kvm_s390_pv_get_handle(kvm),
	};
	int cc;

	cc = uv_call_sched(0, (u64)&uvcb);
	if (rc)
		*rc = uvcb.header.rc;
	if (rrc)
		*rrc = uvcb.header.rrc;
	WRITE_ONCE(kvm->arch.gmap->guest_handle, 0);
	KVM_UV_EVENT(kvm, 3, "PROTVIRT DESTROY VM FAST: rc %x rrc %x",
		     uvcb.header.rc, uvcb.header.rrc);
	WARN_ONCE(cc && uvcb.header.rc != 0x104,
		  "protvirt destroy vm fast failed handle %llx rc %x rrc %x",
		  kvm_s390_pv_get_handle(kvm), uvcb.header.rc, uvcb.header.rrc);
	/* Intended memory leak on "impossible" error */
	if (!cc)
		kvm_s390_pv_dealloc_vm(kvm);
	return cc ? -EIO : 0;
}

static inline bool is_destroy_fast_available(void)
{
	return test_bit_inv(BIT_UVC_CMD_DESTROY_SEC_CONF_FAST, uv_info.inst_calls_list);
}

/**
 * kvm_s390_pv_set_aside - Set aside a protected VM for later teardown.
 * @kvm: the VM
 * @rc: return value for the RC field of the UVCB
 * @rrc: return value for the RRC field of the UVCB
 *
 * Set aside the protected VM for a subsequent teardown. The VM will be able
 * to continue immediately as a non-secure VM, and the information needed to
 * properly tear down the protected VM is set aside. If another protected VM
 * was already set aside without starting its teardown, this function will
 * fail.
 * The CPUs of the protected VM need to be destroyed beforehand.
 *
 * Context: kvm->lock needs to be held
 *
 * Return: 0 in case of success, -EINVAL if another protected VM was already set
 * aside, -ENOMEM if the system ran out of memory.
 */
int kvm_s390_pv_set_aside(struct kvm *kvm, u16 *rc, u16 *rrc)
{
	struct pv_vm_to_be_destroyed *priv;
	int res = 0;

	lockdep_assert_held(&kvm->lock);
	/*
	 * If another protected VM was already prepared for teardown, refuse.
	 * A normal deinitialization has to be performed instead.
	 */
	if (kvm->arch.pv.set_aside)
		return -EINVAL;

	/* Guest with segment type ASCE, refuse to destroy asynchronously */
	if ((kvm->arch.gmap->asce & _ASCE_TYPE_MASK) == _ASCE_TYPE_SEGMENT)
		return -EINVAL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (is_destroy_fast_available()) {
		res = kvm_s390_pv_deinit_vm_fast(kvm, rc, rrc);
	} else {
		priv->stor_var = kvm->arch.pv.stor_var;
		priv->stor_base = kvm->arch.pv.stor_base;
		priv->handle = kvm_s390_pv_get_handle(kvm);
		priv->old_gmap_table = (unsigned long)kvm->arch.gmap->table;
		WRITE_ONCE(kvm->arch.gmap->guest_handle, 0);
		if (s390_replace_asce(kvm->arch.gmap))
			res = -ENOMEM;
	}

	if (res) {
		kfree(priv);
		return res;
	}

	kvm_s390_destroy_lower_2g(kvm);
	kvm_s390_clear_pv_state(kvm);
	kvm->arch.pv.set_aside = priv;

	*rc = UVC_RC_EXECUTED;
	*rrc = 42;
	return 0;
}

/**
 * kvm_s390_pv_deinit_vm - Deinitialize the current protected VM
 * @kvm: the KVM whose protected VM needs to be deinitialized
 * @rc: the RC code of the UVC
 * @rrc: the RRC code of the UVC
 *
 * Deinitialize the current protected VM. This function will destroy and
 * cleanup the current protected VM, but it will not cleanup the guest
 * memory. This function should only be called when the protected VM has
 * just been created and therefore does not have any guest memory, or when
 * the caller cleans up the guest memory separately.
 *
 * This function should not fail, but if it does, the donated memory must
 * not be freed.
 *
 * Context: kvm->lock needs to be held
 *
 * Return: 0 in case of success, otherwise -EIO
 */
int kvm_s390_pv_deinit_vm(struct kvm *kvm, u16 *rc, u16 *rrc)
{
	int cc;

	cc = uv_cmd_nodata(kvm_s390_pv_get_handle(kvm),
			   UVC_CMD_DESTROY_SEC_CONF, rc, rrc);
	WRITE_ONCE(kvm->arch.gmap->guest_handle, 0);
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

/**
 * kvm_s390_pv_deinit_cleanup_all - Clean up all protected VMs associated
 * with a specific KVM.
 * @kvm: the KVM to be cleaned up
 * @rc: the RC code of the first failing UVC
 * @rrc: the RRC code of the first failing UVC
 *
 * This function will clean up all protected VMs associated with a KVM.
 * This includes the active one, the one prepared for deinitialization with
 * kvm_s390_pv_set_aside, and any still pending in the need_cleanup list.
 *
 * Context: kvm->lock needs to be held unless being called from
 * kvm_arch_destroy_vm.
 *
 * Return: 0 if all VMs are successfully cleaned up, otherwise -EIO
 */
int kvm_s390_pv_deinit_cleanup_all(struct kvm *kvm, u16 *rc, u16 *rrc)
{
	struct pv_vm_to_be_destroyed *cur;
	bool need_zap = false;
	u16 _rc, _rrc;
	int cc = 0;

	/*
	 * Nothing to do if the counter was already 0. Otherwise make sure
	 * the counter does not reach 0 before calling s390_uv_destroy_range.
	 */
	if (!atomic_inc_not_zero(&kvm->mm->context.protected_count))
		return 0;

	*rc = 1;
	/* If the current VM is protected, destroy it */
	if (kvm_s390_pv_get_handle(kvm)) {
		cc = kvm_s390_pv_deinit_vm(kvm, rc, rrc);
		need_zap = true;
	}

	/* If a previous protected VM was set aside, put it in the need_cleanup list */
	if (kvm->arch.pv.set_aside) {
		list_add(kvm->arch.pv.set_aside, &kvm->arch.pv.need_cleanup);
		kvm->arch.pv.set_aside = NULL;
	}

	/* Cleanup all protected VMs in the need_cleanup list */
	while (!list_empty(&kvm->arch.pv.need_cleanup)) {
		cur = list_first_entry(&kvm->arch.pv.need_cleanup, typeof(*cur), list);
		need_zap = true;
		if (kvm_s390_pv_dispose_one_leftover(kvm, cur, &_rc, &_rrc)) {
			cc = 1;
			/*
			 * Only return the first error rc and rrc, so make
			 * sure it is not overwritten. All destroys will
			 * additionally be reported via KVM_UV_EVENT().
			 */
			if (*rc == UVC_RC_EXECUTED) {
				*rc = _rc;
				*rrc = _rrc;
			}
		}
		list_del(&cur->list);
		kfree(cur);
	}

	/*
	 * If the mm still has a mapping, try to mark all its pages as
	 * accessible. The counter should not reach zero before this
	 * cleanup has been performed.
	 */
	if (need_zap && mmget_not_zero(kvm->mm)) {
		s390_uv_destroy_range(kvm->mm, 0, TASK_SIZE);
		mmput(kvm->mm);
	}

	/* Now the counter can safely reach 0 */
	atomic_dec(&kvm->mm->context.protected_count);
	return cc ? -EIO : 0;
}

/**
 * kvm_s390_pv_deinit_aside_vm - Teardown a previously set aside protected VM.
 * @kvm: the VM previously associated with the protected VM
 * @rc: return value for the RC field of the UVCB
 * @rrc: return value for the RRC field of the UVCB
 *
 * Tear down the protected VM that had been previously prepared for teardown
 * using kvm_s390_pv_set_aside_vm. Ideally this should be called by
 * userspace asynchronously from a separate thread.
 *
 * Context: kvm->lock must not be held.
 *
 * Return: 0 in case of success, -EINVAL if no protected VM had been
 * prepared for asynchronous teardowm, -EIO in case of other errors.
 */
int kvm_s390_pv_deinit_aside_vm(struct kvm *kvm, u16 *rc, u16 *rrc)
{
	struct pv_vm_to_be_destroyed *p;
	int ret = 0;

	lockdep_assert_not_held(&kvm->lock);
	mutex_lock(&kvm->lock);
	p = kvm->arch.pv.set_aside;
	kvm->arch.pv.set_aside = NULL;
	mutex_unlock(&kvm->lock);
	if (!p)
		return -EINVAL;

	/* When a fatal signal is received, stop immediately */
	if (s390_uv_destroy_range_interruptible(kvm->mm, 0, TASK_SIZE_MAX))
		goto done;
	if (kvm_s390_pv_dispose_one_leftover(kvm, p, rc, rrc))
		ret = -EIO;
	kfree(p);
	p = NULL;
done:
	/*
	 * p is not NULL if we aborted because of a fatal signal, in which
	 * case queue the leftover for later cleanup.
	 */
	if (p) {
		mutex_lock(&kvm->lock);
		list_add(&p->list, &kvm->arch.pv.need_cleanup);
		mutex_unlock(&kvm->lock);
		/* Did not finish, but pretend things went well */
		*rc = UVC_RC_EXECUTED;
		*rrc = 42;
	}
	return ret;
}

static void kvm_s390_pv_mmu_notifier_release(struct mmu_notifier *subscription,
					     struct mm_struct *mm)
{
	struct kvm *kvm = container_of(subscription, struct kvm, arch.pv.mmu_notifier);
	u16 dummy;
	int r;

	/*
	 * No locking is needed since this is the last thread of the last user of this
	 * struct mm.
	 * When the struct kvm gets deinitialized, this notifier is also
	 * unregistered. This means that if this notifier runs, then the
	 * struct kvm is still valid.
	 */
	r = kvm_s390_cpus_from_pv(kvm, &dummy, &dummy);
	if (!r && is_destroy_fast_available() && kvm_s390_pv_get_handle(kvm))
		kvm_s390_pv_deinit_vm_fast(kvm, &dummy, &dummy);
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

	/* Add the notifier only once. No races because we hold kvm->lock */
	if (kvm->arch.pv.mmu_notifier.ops != &kvm_s390_pv_mmu_notifier_ops) {
		/* The notifier will be unregistered when the VM is destroyed */
		kvm->arch.pv.mmu_notifier.ops = &kvm_s390_pv_mmu_notifier_ops;
		ret = mmu_notifier_register(&kvm->arch.pv.mmu_notifier, kvm->mm);
		if (ret) {
			kvm->arch.pv.mmu_notifier.ops = NULL;
			return ret;
		}
	}

	ret = kvm_s390_pv_alloc_vm(kvm);
	if (ret)
		return ret;

	/* Inputs */
	uvcb.guest_stor_origin = 0; /* MSO is 0 for KVM */
	uvcb.guest_stor_len = kvm->arch.pv.guest_len;
	uvcb.guest_asce = kvm->arch.gmap->asce;
	uvcb.guest_sca = virt_to_phys(kvm->arch.sca);
	uvcb.conf_base_stor_origin =
		virt_to_phys((void *)kvm->arch.pv.stor_base);
	uvcb.conf_virt_stor_origin = (u64)kvm->arch.pv.stor_var;
	uvcb.flags.ap_allow_instr = kvm->arch.model.uv_feat_guest.ap;
	uvcb.flags.ap_instr_intr = kvm->arch.model.uv_feat_guest.ap_intr;

	cc = uv_call_sched(0, (u64)&uvcb);
	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;
	KVM_UV_EVENT(kvm, 3, "PROTVIRT CREATE VM: handle %llx len %llx rc %x rrc %x flags %04x",
		     uvcb.guest_handle, uvcb.guest_stor_len, *rc, *rrc, uvcb.flags.raw);

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
	int ret = kvm_s390_pv_make_secure(kvm, addr, &uvcb);
	unsigned long vmaddr;
	bool unlocked;

	*rc = uvcb.header.rc;
	*rrc = uvcb.header.rrc;

	if (ret == -ENXIO) {
		mmap_read_lock(kvm->mm);
		vmaddr = gfn_to_hva(kvm, gpa_to_gfn(addr));
		if (kvm_is_error_hva(vmaddr)) {
			ret = -EFAULT;
		} else {
			ret = fixup_user_fault(kvm->mm, vmaddr, FAULT_FLAG_WRITE, &unlocked);
			if (!ret)
				ret = __gmap_link(kvm->arch.gmap, addr, vmaddr);
		}
		mmap_read_unlock(kvm->mm);
		if (!ret)
			return -EAGAIN;
		return ret;
	}

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

	guard(srcu)(&kvm->srcu);

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
