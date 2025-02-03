// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * AMD SVM-SEV support
 *
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/psp.h>
#include <linux/psp-sev.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/misc_cgroup.h>
#include <linux/processor.h>
#include <linux/trace_events.h>
#include <uapi/linux/sev-guest.h>

#include <asm/pkru.h>
#include <asm/trapnr.h>
#include <asm/fpu/xcr.h>
#include <asm/fpu/xstate.h>
#include <asm/debugreg.h>
#include <asm/sev.h>

#include "mmu.h"
#include "x86.h"
#include "svm.h"
#include "svm_ops.h"
#include "cpuid.h"
#include "trace.h"

#define GHCB_VERSION_MAX	2ULL
#define GHCB_VERSION_DEFAULT	2ULL
#define GHCB_VERSION_MIN	1ULL

#define GHCB_HV_FT_SUPPORTED	(GHCB_HV_FT_SNP | GHCB_HV_FT_SNP_AP_CREATION)

/* enable/disable SEV support */
static bool sev_enabled = true;
module_param_named(sev, sev_enabled, bool, 0444);

/* enable/disable SEV-ES support */
static bool sev_es_enabled = true;
module_param_named(sev_es, sev_es_enabled, bool, 0444);

/* enable/disable SEV-SNP support */
static bool sev_snp_enabled = true;
module_param_named(sev_snp, sev_snp_enabled, bool, 0444);

/* enable/disable SEV-ES DebugSwap support */
static bool sev_es_debug_swap_enabled = true;
module_param_named(debug_swap, sev_es_debug_swap_enabled, bool, 0444);
static u64 sev_supported_vmsa_features;

#define AP_RESET_HOLD_NONE		0
#define AP_RESET_HOLD_NAE_EVENT		1
#define AP_RESET_HOLD_MSR_PROTO		2

/* As defined by SEV-SNP Firmware ABI, under "Guest Policy". */
#define SNP_POLICY_MASK_API_MINOR	GENMASK_ULL(7, 0)
#define SNP_POLICY_MASK_API_MAJOR	GENMASK_ULL(15, 8)
#define SNP_POLICY_MASK_SMT		BIT_ULL(16)
#define SNP_POLICY_MASK_RSVD_MBO	BIT_ULL(17)
#define SNP_POLICY_MASK_DEBUG		BIT_ULL(19)
#define SNP_POLICY_MASK_SINGLE_SOCKET	BIT_ULL(20)

#define SNP_POLICY_MASK_VALID		(SNP_POLICY_MASK_API_MINOR	| \
					 SNP_POLICY_MASK_API_MAJOR	| \
					 SNP_POLICY_MASK_SMT		| \
					 SNP_POLICY_MASK_RSVD_MBO	| \
					 SNP_POLICY_MASK_DEBUG		| \
					 SNP_POLICY_MASK_SINGLE_SOCKET)

#define INITIAL_VMSA_GPA 0xFFFFFFFFF000

static u8 sev_enc_bit;
static DECLARE_RWSEM(sev_deactivate_lock);
static DEFINE_MUTEX(sev_bitmap_lock);
unsigned int max_sev_asid;
static unsigned int min_sev_asid;
static unsigned long sev_me_mask;
static unsigned int nr_asids;
static unsigned long *sev_asid_bitmap;
static unsigned long *sev_reclaim_asid_bitmap;

static int snp_decommission_context(struct kvm *kvm);

struct enc_region {
	struct list_head list;
	unsigned long npages;
	struct page **pages;
	unsigned long uaddr;
	unsigned long size;
};

/* Called with the sev_bitmap_lock held, or on shutdown  */
static int sev_flush_asids(unsigned int min_asid, unsigned int max_asid)
{
	int ret, error = 0;
	unsigned int asid;

	/* Check if there are any ASIDs to reclaim before performing a flush */
	asid = find_next_bit(sev_reclaim_asid_bitmap, nr_asids, min_asid);
	if (asid > max_asid)
		return -EBUSY;

	/*
	 * DEACTIVATE will clear the WBINVD indicator causing DF_FLUSH to fail,
	 * so it must be guarded.
	 */
	down_write(&sev_deactivate_lock);

	wbinvd_on_all_cpus();

	if (sev_snp_enabled)
		ret = sev_do_cmd(SEV_CMD_SNP_DF_FLUSH, NULL, &error);
	else
		ret = sev_guest_df_flush(&error);

	up_write(&sev_deactivate_lock);

	if (ret)
		pr_err("SEV%s: DF_FLUSH failed, ret=%d, error=%#x\n",
		       sev_snp_enabled ? "-SNP" : "", ret, error);

	return ret;
}

static inline bool is_mirroring_enc_context(struct kvm *kvm)
{
	return !!to_kvm_sev_info(kvm)->enc_context_owner;
}

static bool sev_vcpu_has_debug_swap(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct kvm_sev_info *sev = &to_kvm_svm(vcpu->kvm)->sev_info;

	return sev->vmsa_features & SVM_SEV_FEAT_DEBUG_SWAP;
}

/* Must be called with the sev_bitmap_lock held */
static bool __sev_recycle_asids(unsigned int min_asid, unsigned int max_asid)
{
	if (sev_flush_asids(min_asid, max_asid))
		return false;

	/* The flush process will flush all reclaimable SEV and SEV-ES ASIDs */
	bitmap_xor(sev_asid_bitmap, sev_asid_bitmap, sev_reclaim_asid_bitmap,
		   nr_asids);
	bitmap_zero(sev_reclaim_asid_bitmap, nr_asids);

	return true;
}

static int sev_misc_cg_try_charge(struct kvm_sev_info *sev)
{
	enum misc_res_type type = sev->es_active ? MISC_CG_RES_SEV_ES : MISC_CG_RES_SEV;
	return misc_cg_try_charge(type, sev->misc_cg, 1);
}

static void sev_misc_cg_uncharge(struct kvm_sev_info *sev)
{
	enum misc_res_type type = sev->es_active ? MISC_CG_RES_SEV_ES : MISC_CG_RES_SEV;
	misc_cg_uncharge(type, sev->misc_cg, 1);
}

static int sev_asid_new(struct kvm_sev_info *sev)
{
	/*
	 * SEV-enabled guests must use asid from min_sev_asid to max_sev_asid.
	 * SEV-ES-enabled guest can use from 1 to min_sev_asid - 1.
	 * Note: min ASID can end up larger than the max if basic SEV support is
	 * effectively disabled by disallowing use of ASIDs for SEV guests.
	 */
	unsigned int min_asid = sev->es_active ? 1 : min_sev_asid;
	unsigned int max_asid = sev->es_active ? min_sev_asid - 1 : max_sev_asid;
	unsigned int asid;
	bool retry = true;
	int ret;

	if (min_asid > max_asid)
		return -ENOTTY;

	WARN_ON(sev->misc_cg);
	sev->misc_cg = get_current_misc_cg();
	ret = sev_misc_cg_try_charge(sev);
	if (ret) {
		put_misc_cg(sev->misc_cg);
		sev->misc_cg = NULL;
		return ret;
	}

	mutex_lock(&sev_bitmap_lock);

again:
	asid = find_next_zero_bit(sev_asid_bitmap, max_asid + 1, min_asid);
	if (asid > max_asid) {
		if (retry && __sev_recycle_asids(min_asid, max_asid)) {
			retry = false;
			goto again;
		}
		mutex_unlock(&sev_bitmap_lock);
		ret = -EBUSY;
		goto e_uncharge;
	}

	__set_bit(asid, sev_asid_bitmap);

	mutex_unlock(&sev_bitmap_lock);

	sev->asid = asid;
	return 0;
e_uncharge:
	sev_misc_cg_uncharge(sev);
	put_misc_cg(sev->misc_cg);
	sev->misc_cg = NULL;
	return ret;
}

static unsigned int sev_get_asid(struct kvm *kvm)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;

	return sev->asid;
}

static void sev_asid_free(struct kvm_sev_info *sev)
{
	struct svm_cpu_data *sd;
	int cpu;

	mutex_lock(&sev_bitmap_lock);

	__set_bit(sev->asid, sev_reclaim_asid_bitmap);

	for_each_possible_cpu(cpu) {
		sd = per_cpu_ptr(&svm_data, cpu);
		sd->sev_vmcbs[sev->asid] = NULL;
	}

	mutex_unlock(&sev_bitmap_lock);

	sev_misc_cg_uncharge(sev);
	put_misc_cg(sev->misc_cg);
	sev->misc_cg = NULL;
}

static void sev_decommission(unsigned int handle)
{
	struct sev_data_decommission decommission;

	if (!handle)
		return;

	decommission.handle = handle;
	sev_guest_decommission(&decommission, NULL);
}

/*
 * Transition a page to hypervisor-owned/shared state in the RMP table. This
 * should not fail under normal conditions, but leak the page should that
 * happen since it will no longer be usable by the host due to RMP protections.
 */
static int kvm_rmp_make_shared(struct kvm *kvm, u64 pfn, enum pg_level level)
{
	if (KVM_BUG_ON(rmp_make_shared(pfn, level), kvm)) {
		snp_leak_pages(pfn, page_level_size(level) >> PAGE_SHIFT);
		return -EIO;
	}

	return 0;
}

/*
 * Certain page-states, such as Pre-Guest and Firmware pages (as documented
 * in Chapter 5 of the SEV-SNP Firmware ABI under "Page States") cannot be
 * directly transitioned back to normal/hypervisor-owned state via RMPUPDATE
 * unless they are reclaimed first.
 *
 * Until they are reclaimed and subsequently transitioned via RMPUPDATE, they
 * might not be usable by the host due to being set as immutable or still
 * being associated with a guest ASID.
 *
 * Bug the VM and leak the page if reclaim fails, or if the RMP entry can't be
 * converted back to shared, as the page is no longer usable due to RMP
 * protections, and it's infeasible for the guest to continue on.
 */
static int snp_page_reclaim(struct kvm *kvm, u64 pfn)
{
	struct sev_data_snp_page_reclaim data = {0};
	int fw_err, rc;

	data.paddr = __sme_set(pfn << PAGE_SHIFT);
	rc = sev_do_cmd(SEV_CMD_SNP_PAGE_RECLAIM, &data, &fw_err);
	if (KVM_BUG(rc, kvm, "Failed to reclaim PFN %llx, rc %d fw_err %d", pfn, rc, fw_err)) {
		snp_leak_pages(pfn, 1);
		return -EIO;
	}

	if (kvm_rmp_make_shared(kvm, pfn, PG_LEVEL_4K))
		return -EIO;

	return rc;
}

static void sev_unbind_asid(struct kvm *kvm, unsigned int handle)
{
	struct sev_data_deactivate deactivate;

	if (!handle)
		return;

	deactivate.handle = handle;

	/* Guard DEACTIVATE against WBINVD/DF_FLUSH used in ASID recycling */
	down_read(&sev_deactivate_lock);
	sev_guest_deactivate(&deactivate, NULL);
	up_read(&sev_deactivate_lock);

	sev_decommission(handle);
}

/*
 * This sets up bounce buffers/firmware pages to handle SNP Guest Request
 * messages (e.g. attestation requests). See "SNP Guest Request" in the GHCB
 * 2.0 specification for more details.
 *
 * Technically, when an SNP Guest Request is issued, the guest will provide its
 * own request/response pages, which could in theory be passed along directly
 * to firmware rather than using bounce pages. However, these pages would need
 * special care:
 *
 *   - Both pages are from shared guest memory, so they need to be protected
 *     from migration/etc. occurring while firmware reads/writes to them. At a
 *     minimum, this requires elevating the ref counts and potentially needing
 *     an explicit pinning of the memory. This places additional restrictions
 *     on what type of memory backends userspace can use for shared guest
 *     memory since there is some reliance on using refcounted pages.
 *
 *   - The response page needs to be switched to Firmware-owned[1] state
 *     before the firmware can write to it, which can lead to potential
 *     host RMP #PFs if the guest is misbehaved and hands the host a
 *     guest page that KVM might write to for other reasons (e.g. virtio
 *     buffers/etc.).
 *
 * Both of these issues can be avoided completely by using separately-allocated
 * bounce pages for both the request/response pages and passing those to
 * firmware instead. So that's what is being set up here.
 *
 * Guest requests rely on message sequence numbers to ensure requests are
 * issued to firmware in the order the guest issues them, so concurrent guest
 * requests generally shouldn't happen. But a misbehaved guest could issue
 * concurrent guest requests in theory, so a mutex is used to serialize
 * access to the bounce buffers.
 *
 * [1] See the "Page States" section of the SEV-SNP Firmware ABI for more
 *     details on Firmware-owned pages, along with "RMP and VMPL Access Checks"
 *     in the APM for details on the related RMP restrictions.
 */
static int snp_guest_req_init(struct kvm *kvm)
{
	struct kvm_sev_info *sev = to_kvm_sev_info(kvm);
	struct page *req_page;

	req_page = alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!req_page)
		return -ENOMEM;

	sev->guest_resp_buf = snp_alloc_firmware_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!sev->guest_resp_buf) {
		__free_page(req_page);
		return -EIO;
	}

	sev->guest_req_buf = page_address(req_page);
	mutex_init(&sev->guest_req_mutex);

	return 0;
}

static void snp_guest_req_cleanup(struct kvm *kvm)
{
	struct kvm_sev_info *sev = to_kvm_sev_info(kvm);

	if (sev->guest_resp_buf)
		snp_free_firmware_page(sev->guest_resp_buf);

	if (sev->guest_req_buf)
		__free_page(virt_to_page(sev->guest_req_buf));

	sev->guest_req_buf = NULL;
	sev->guest_resp_buf = NULL;
}

static int __sev_guest_init(struct kvm *kvm, struct kvm_sev_cmd *argp,
			    struct kvm_sev_init *data,
			    unsigned long vm_type)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_platform_init_args init_args = {0};
	bool es_active = vm_type != KVM_X86_SEV_VM;
	u64 valid_vmsa_features = es_active ? sev_supported_vmsa_features : 0;
	int ret;

	if (kvm->created_vcpus)
		return -EINVAL;

	if (data->flags)
		return -EINVAL;

	if (data->vmsa_features & ~valid_vmsa_features)
		return -EINVAL;

	if (data->ghcb_version > GHCB_VERSION_MAX || (!es_active && data->ghcb_version))
		return -EINVAL;

	if (unlikely(sev->active))
		return -EINVAL;

	sev->active = true;
	sev->es_active = es_active;
	sev->vmsa_features = data->vmsa_features;
	sev->ghcb_version = data->ghcb_version;

	/*
	 * Currently KVM supports the full range of mandatory features defined
	 * by version 2 of the GHCB protocol, so default to that for SEV-ES
	 * guests created via KVM_SEV_INIT2.
	 */
	if (sev->es_active && !sev->ghcb_version)
		sev->ghcb_version = GHCB_VERSION_DEFAULT;

	if (vm_type == KVM_X86_SNP_VM)
		sev->vmsa_features |= SVM_SEV_FEAT_SNP_ACTIVE;

	ret = sev_asid_new(sev);
	if (ret)
		goto e_no_asid;

	init_args.probe = false;
	ret = sev_platform_init(&init_args);
	if (ret)
		goto e_free;

	/* This needs to happen after SEV/SNP firmware initialization. */
	if (vm_type == KVM_X86_SNP_VM) {
		ret = snp_guest_req_init(kvm);
		if (ret)
			goto e_free;
	}

	INIT_LIST_HEAD(&sev->regions_list);
	INIT_LIST_HEAD(&sev->mirror_vms);
	sev->need_init = false;

	kvm_set_apicv_inhibit(kvm, APICV_INHIBIT_REASON_SEV);

	return 0;

e_free:
	argp->error = init_args.error;
	sev_asid_free(sev);
	sev->asid = 0;
e_no_asid:
	sev->vmsa_features = 0;
	sev->es_active = false;
	sev->active = false;
	return ret;
}

static int sev_guest_init(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_init data = {
		.vmsa_features = 0,
		.ghcb_version = 0,
	};
	unsigned long vm_type;

	if (kvm->arch.vm_type != KVM_X86_DEFAULT_VM)
		return -EINVAL;

	vm_type = (argp->id == KVM_SEV_INIT ? KVM_X86_SEV_VM : KVM_X86_SEV_ES_VM);

	/*
	 * KVM_SEV_ES_INIT has been deprecated by KVM_SEV_INIT2, so it will
	 * continue to only ever support the minimal GHCB protocol version.
	 */
	if (vm_type == KVM_X86_SEV_ES_VM)
		data.ghcb_version = GHCB_VERSION_MIN;

	return __sev_guest_init(kvm, argp, &data, vm_type);
}

static int sev_guest_init2(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct kvm_sev_init data;

	if (!sev->need_init)
		return -EINVAL;

	if (kvm->arch.vm_type != KVM_X86_SEV_VM &&
	    kvm->arch.vm_type != KVM_X86_SEV_ES_VM &&
	    kvm->arch.vm_type != KVM_X86_SNP_VM)
		return -EINVAL;

	if (copy_from_user(&data, u64_to_user_ptr(argp->data), sizeof(data)))
		return -EFAULT;

	return __sev_guest_init(kvm, argp, &data, kvm->arch.vm_type);
}

static int sev_bind_asid(struct kvm *kvm, unsigned int handle, int *error)
{
	unsigned int asid = sev_get_asid(kvm);
	struct sev_data_activate activate;
	int ret;

	/* activate ASID on the given handle */
	activate.handle = handle;
	activate.asid   = asid;
	ret = sev_guest_activate(&activate, error);

	return ret;
}

static int __sev_issue_cmd(int fd, int id, void *data, int *error)
{
	CLASS(fd, f)(fd);

	if (fd_empty(f))
		return -EBADF;

	return sev_issue_cmd_external_user(fd_file(f), id, data, error);
}

static int sev_issue_cmd(struct kvm *kvm, int id, void *data, int *error)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;

	return __sev_issue_cmd(sev->fd, id, data, error);
}

static int sev_launch_start(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_launch_start start;
	struct kvm_sev_launch_start params;
	void *dh_blob, *session_blob;
	int *error = &argp->error;
	int ret;

	if (!sev_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data), sizeof(params)))
		return -EFAULT;

	memset(&start, 0, sizeof(start));

	dh_blob = NULL;
	if (params.dh_uaddr) {
		dh_blob = psp_copy_user_blob(params.dh_uaddr, params.dh_len);
		if (IS_ERR(dh_blob))
			return PTR_ERR(dh_blob);

		start.dh_cert_address = __sme_set(__pa(dh_blob));
		start.dh_cert_len = params.dh_len;
	}

	session_blob = NULL;
	if (params.session_uaddr) {
		session_blob = psp_copy_user_blob(params.session_uaddr, params.session_len);
		if (IS_ERR(session_blob)) {
			ret = PTR_ERR(session_blob);
			goto e_free_dh;
		}

		start.session_address = __sme_set(__pa(session_blob));
		start.session_len = params.session_len;
	}

	start.handle = params.handle;
	start.policy = params.policy;

	/* create memory encryption context */
	ret = __sev_issue_cmd(argp->sev_fd, SEV_CMD_LAUNCH_START, &start, error);
	if (ret)
		goto e_free_session;

	/* Bind ASID to this guest */
	ret = sev_bind_asid(kvm, start.handle, error);
	if (ret) {
		sev_decommission(start.handle);
		goto e_free_session;
	}

	/* return handle to userspace */
	params.handle = start.handle;
	if (copy_to_user(u64_to_user_ptr(argp->data), &params, sizeof(params))) {
		sev_unbind_asid(kvm, start.handle);
		ret = -EFAULT;
		goto e_free_session;
	}

	sev->handle = start.handle;
	sev->fd = argp->sev_fd;

e_free_session:
	kfree(session_blob);
e_free_dh:
	kfree(dh_blob);
	return ret;
}

static struct page **sev_pin_memory(struct kvm *kvm, unsigned long uaddr,
				    unsigned long ulen, unsigned long *n,
				    int write)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	unsigned long npages, size;
	int npinned;
	unsigned long locked, lock_limit;
	struct page **pages;
	unsigned long first, last;
	int ret;

	lockdep_assert_held(&kvm->lock);

	if (ulen == 0 || uaddr + ulen < uaddr)
		return ERR_PTR(-EINVAL);

	/* Calculate number of pages. */
	first = (uaddr & PAGE_MASK) >> PAGE_SHIFT;
	last = ((uaddr + ulen - 1) & PAGE_MASK) >> PAGE_SHIFT;
	npages = (last - first + 1);

	locked = sev->pages_locked + npages;
	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	if (locked > lock_limit && !capable(CAP_IPC_LOCK)) {
		pr_err("SEV: %lu locked pages exceed the lock limit of %lu.\n", locked, lock_limit);
		return ERR_PTR(-ENOMEM);
	}

	if (WARN_ON_ONCE(npages > INT_MAX))
		return ERR_PTR(-EINVAL);

	/* Avoid using vmalloc for smaller buffers. */
	size = npages * sizeof(struct page *);
	if (size > PAGE_SIZE)
		pages = __vmalloc(size, GFP_KERNEL_ACCOUNT);
	else
		pages = kmalloc(size, GFP_KERNEL_ACCOUNT);

	if (!pages)
		return ERR_PTR(-ENOMEM);

	/* Pin the user virtual address. */
	npinned = pin_user_pages_fast(uaddr, npages, write ? FOLL_WRITE : 0, pages);
	if (npinned != npages) {
		pr_err("SEV: Failure locking %lu pages.\n", npages);
		ret = -ENOMEM;
		goto err;
	}

	*n = npages;
	sev->pages_locked = locked;

	return pages;

err:
	if (npinned > 0)
		unpin_user_pages(pages, npinned);

	kvfree(pages);
	return ERR_PTR(ret);
}

static void sev_unpin_memory(struct kvm *kvm, struct page **pages,
			     unsigned long npages)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;

	unpin_user_pages(pages, npages);
	kvfree(pages);
	sev->pages_locked -= npages;
}

static void sev_clflush_pages(struct page *pages[], unsigned long npages)
{
	uint8_t *page_virtual;
	unsigned long i;

	if (this_cpu_has(X86_FEATURE_SME_COHERENT) || npages == 0 ||
	    pages == NULL)
		return;

	for (i = 0; i < npages; i++) {
		page_virtual = kmap_local_page(pages[i]);
		clflush_cache_range(page_virtual, PAGE_SIZE);
		kunmap_local(page_virtual);
		cond_resched();
	}
}

static unsigned long get_num_contig_pages(unsigned long idx,
				struct page **inpages, unsigned long npages)
{
	unsigned long paddr, next_paddr;
	unsigned long i = idx + 1, pages = 1;

	/* find the number of contiguous pages starting from idx */
	paddr = __sme_page_pa(inpages[idx]);
	while (i < npages) {
		next_paddr = __sme_page_pa(inpages[i++]);
		if ((paddr + PAGE_SIZE) == next_paddr) {
			pages++;
			paddr = next_paddr;
			continue;
		}
		break;
	}

	return pages;
}

static int sev_launch_update_data(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	unsigned long vaddr, vaddr_end, next_vaddr, npages, pages, size, i;
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct kvm_sev_launch_update_data params;
	struct sev_data_launch_update_data data;
	struct page **inpages;
	int ret;

	if (!sev_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data), sizeof(params)))
		return -EFAULT;

	vaddr = params.uaddr;
	size = params.len;
	vaddr_end = vaddr + size;

	/* Lock the user memory. */
	inpages = sev_pin_memory(kvm, vaddr, size, &npages, 1);
	if (IS_ERR(inpages))
		return PTR_ERR(inpages);

	/*
	 * Flush (on non-coherent CPUs) before LAUNCH_UPDATE encrypts pages in
	 * place; the cache may contain the data that was written unencrypted.
	 */
	sev_clflush_pages(inpages, npages);

	data.reserved = 0;
	data.handle = sev->handle;

	for (i = 0; vaddr < vaddr_end; vaddr = next_vaddr, i += pages) {
		int offset, len;

		/*
		 * If the user buffer is not page-aligned, calculate the offset
		 * within the page.
		 */
		offset = vaddr & (PAGE_SIZE - 1);

		/* Calculate the number of pages that can be encrypted in one go. */
		pages = get_num_contig_pages(i, inpages, npages);

		len = min_t(size_t, ((pages * PAGE_SIZE) - offset), size);

		data.len = len;
		data.address = __sme_page_pa(inpages[i]) + offset;
		ret = sev_issue_cmd(kvm, SEV_CMD_LAUNCH_UPDATE_DATA, &data, &argp->error);
		if (ret)
			goto e_unpin;

		size -= len;
		next_vaddr = vaddr + len;
	}

e_unpin:
	/* content of memory is updated, mark pages dirty */
	for (i = 0; i < npages; i++) {
		set_page_dirty_lock(inpages[i]);
		mark_page_accessed(inpages[i]);
	}
	/* unlock the user pages */
	sev_unpin_memory(kvm, inpages, npages);
	return ret;
}

static int sev_es_sync_vmsa(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct kvm_sev_info *sev = &to_kvm_svm(vcpu->kvm)->sev_info;
	struct sev_es_save_area *save = svm->sev_es.vmsa;
	struct xregs_state *xsave;
	const u8 *s;
	u8 *d;
	int i;

	/* Check some debug related fields before encrypting the VMSA */
	if (svm->vcpu.guest_debug || (svm->vmcb->save.dr7 & ~DR7_FIXED_1))
		return -EINVAL;

	/*
	 * SEV-ES will use a VMSA that is pointed to by the VMCB, not
	 * the traditional VMSA that is part of the VMCB. Copy the
	 * traditional VMSA as it has been built so far (in prep
	 * for LAUNCH_UPDATE_VMSA) to be the initial SEV-ES state.
	 */
	memcpy(save, &svm->vmcb->save, sizeof(svm->vmcb->save));

	/* Sync registgers */
	save->rax = svm->vcpu.arch.regs[VCPU_REGS_RAX];
	save->rbx = svm->vcpu.arch.regs[VCPU_REGS_RBX];
	save->rcx = svm->vcpu.arch.regs[VCPU_REGS_RCX];
	save->rdx = svm->vcpu.arch.regs[VCPU_REGS_RDX];
	save->rsp = svm->vcpu.arch.regs[VCPU_REGS_RSP];
	save->rbp = svm->vcpu.arch.regs[VCPU_REGS_RBP];
	save->rsi = svm->vcpu.arch.regs[VCPU_REGS_RSI];
	save->rdi = svm->vcpu.arch.regs[VCPU_REGS_RDI];
#ifdef CONFIG_X86_64
	save->r8  = svm->vcpu.arch.regs[VCPU_REGS_R8];
	save->r9  = svm->vcpu.arch.regs[VCPU_REGS_R9];
	save->r10 = svm->vcpu.arch.regs[VCPU_REGS_R10];
	save->r11 = svm->vcpu.arch.regs[VCPU_REGS_R11];
	save->r12 = svm->vcpu.arch.regs[VCPU_REGS_R12];
	save->r13 = svm->vcpu.arch.regs[VCPU_REGS_R13];
	save->r14 = svm->vcpu.arch.regs[VCPU_REGS_R14];
	save->r15 = svm->vcpu.arch.regs[VCPU_REGS_R15];
#endif
	save->rip = svm->vcpu.arch.regs[VCPU_REGS_RIP];

	/* Sync some non-GPR registers before encrypting */
	save->xcr0 = svm->vcpu.arch.xcr0;
	save->pkru = svm->vcpu.arch.pkru;
	save->xss  = svm->vcpu.arch.ia32_xss;
	save->dr6  = svm->vcpu.arch.dr6;

	save->sev_features = sev->vmsa_features;

	/*
	 * Skip FPU and AVX setup with KVM_SEV_ES_INIT to avoid
	 * breaking older measurements.
	 */
	if (vcpu->kvm->arch.vm_type != KVM_X86_DEFAULT_VM) {
		xsave = &vcpu->arch.guest_fpu.fpstate->regs.xsave;
		save->x87_dp = xsave->i387.rdp;
		save->mxcsr = xsave->i387.mxcsr;
		save->x87_ftw = xsave->i387.twd;
		save->x87_fsw = xsave->i387.swd;
		save->x87_fcw = xsave->i387.cwd;
		save->x87_fop = xsave->i387.fop;
		save->x87_ds = 0;
		save->x87_cs = 0;
		save->x87_rip = xsave->i387.rip;

		for (i = 0; i < 8; i++) {
			/*
			 * The format of the x87 save area is undocumented and
			 * definitely not what you would expect.  It consists of
			 * an 8*8 bytes area with bytes 0-7, and an 8*2 bytes
			 * area with bytes 8-9 of each register.
			 */
			d = save->fpreg_x87 + i * 8;
			s = ((u8 *)xsave->i387.st_space) + i * 16;
			memcpy(d, s, 8);
			save->fpreg_x87[64 + i * 2] = s[8];
			save->fpreg_x87[64 + i * 2 + 1] = s[9];
		}
		memcpy(save->fpreg_xmm, xsave->i387.xmm_space, 256);

		s = get_xsave_addr(xsave, XFEATURE_YMM);
		if (s)
			memcpy(save->fpreg_ymm, s, 256);
		else
			memset(save->fpreg_ymm, 0, 256);
	}

	pr_debug("Virtual Machine Save Area (VMSA):\n");
	print_hex_dump_debug("", DUMP_PREFIX_NONE, 16, 1, save, sizeof(*save), false);

	return 0;
}

static int __sev_launch_update_vmsa(struct kvm *kvm, struct kvm_vcpu *vcpu,
				    int *error)
{
	struct sev_data_launch_update_vmsa vmsa;
	struct vcpu_svm *svm = to_svm(vcpu);
	int ret;

	if (vcpu->guest_debug) {
		pr_warn_once("KVM_SET_GUEST_DEBUG for SEV-ES guest is not supported");
		return -EINVAL;
	}

	/* Perform some pre-encryption checks against the VMSA */
	ret = sev_es_sync_vmsa(svm);
	if (ret)
		return ret;

	/*
	 * The LAUNCH_UPDATE_VMSA command will perform in-place encryption of
	 * the VMSA memory content (i.e it will write the same memory region
	 * with the guest's key), so invalidate it first.
	 */
	clflush_cache_range(svm->sev_es.vmsa, PAGE_SIZE);

	vmsa.reserved = 0;
	vmsa.handle = to_kvm_sev_info(kvm)->handle;
	vmsa.address = __sme_pa(svm->sev_es.vmsa);
	vmsa.len = PAGE_SIZE;
	ret = sev_issue_cmd(kvm, SEV_CMD_LAUNCH_UPDATE_VMSA, &vmsa, error);
	if (ret)
	  return ret;

	/*
	 * SEV-ES guests maintain an encrypted version of their FPU
	 * state which is restored and saved on VMRUN and VMEXIT.
	 * Mark vcpu->arch.guest_fpu->fpstate as scratch so it won't
	 * do xsave/xrstor on it.
	 */
	fpstate_set_confidential(&vcpu->arch.guest_fpu);
	vcpu->arch.guest_state_protected = true;

	/*
	 * SEV-ES guest mandates LBR Virtualization to be _always_ ON. Enable it
	 * only after setting guest_state_protected because KVM_SET_MSRS allows
	 * dynamic toggling of LBRV (for performance reason) on write access to
	 * MSR_IA32_DEBUGCTLMSR when guest_state_protected is not set.
	 */
	svm_enable_lbrv(vcpu);
	return 0;
}

static int sev_launch_update_vmsa(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;
	int ret;

	if (!sev_es_guest(kvm))
		return -ENOTTY;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		ret = mutex_lock_killable(&vcpu->mutex);
		if (ret)
			return ret;

		ret = __sev_launch_update_vmsa(kvm, vcpu, &argp->error);

		mutex_unlock(&vcpu->mutex);
		if (ret)
			return ret;
	}

	return 0;
}

static int sev_launch_measure(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	void __user *measure = u64_to_user_ptr(argp->data);
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_launch_measure data;
	struct kvm_sev_launch_measure params;
	void __user *p = NULL;
	void *blob = NULL;
	int ret;

	if (!sev_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, measure, sizeof(params)))
		return -EFAULT;

	memset(&data, 0, sizeof(data));

	/* User wants to query the blob length */
	if (!params.len)
		goto cmd;

	p = u64_to_user_ptr(params.uaddr);
	if (p) {
		if (params.len > SEV_FW_BLOB_MAX_SIZE)
			return -EINVAL;

		blob = kzalloc(params.len, GFP_KERNEL_ACCOUNT);
		if (!blob)
			return -ENOMEM;

		data.address = __psp_pa(blob);
		data.len = params.len;
	}

cmd:
	data.handle = sev->handle;
	ret = sev_issue_cmd(kvm, SEV_CMD_LAUNCH_MEASURE, &data, &argp->error);

	/*
	 * If we query the session length, FW responded with expected data.
	 */
	if (!params.len)
		goto done;

	if (ret)
		goto e_free_blob;

	if (blob) {
		if (copy_to_user(p, blob, params.len))
			ret = -EFAULT;
	}

done:
	params.len = data.len;
	if (copy_to_user(measure, &params, sizeof(params)))
		ret = -EFAULT;
e_free_blob:
	kfree(blob);
	return ret;
}

static int sev_launch_finish(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_launch_finish data;

	if (!sev_guest(kvm))
		return -ENOTTY;

	data.handle = sev->handle;
	return sev_issue_cmd(kvm, SEV_CMD_LAUNCH_FINISH, &data, &argp->error);
}

static int sev_guest_status(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct kvm_sev_guest_status params;
	struct sev_data_guest_status data;
	int ret;

	if (!sev_guest(kvm))
		return -ENOTTY;

	memset(&data, 0, sizeof(data));

	data.handle = sev->handle;
	ret = sev_issue_cmd(kvm, SEV_CMD_GUEST_STATUS, &data, &argp->error);
	if (ret)
		return ret;

	params.policy = data.policy;
	params.state = data.state;
	params.handle = data.handle;

	if (copy_to_user(u64_to_user_ptr(argp->data), &params, sizeof(params)))
		ret = -EFAULT;

	return ret;
}

static int __sev_issue_dbg_cmd(struct kvm *kvm, unsigned long src,
			       unsigned long dst, int size,
			       int *error, bool enc)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_dbg data;

	data.reserved = 0;
	data.handle = sev->handle;
	data.dst_addr = dst;
	data.src_addr = src;
	data.len = size;

	return sev_issue_cmd(kvm,
			     enc ? SEV_CMD_DBG_ENCRYPT : SEV_CMD_DBG_DECRYPT,
			     &data, error);
}

static int __sev_dbg_decrypt(struct kvm *kvm, unsigned long src_paddr,
			     unsigned long dst_paddr, int sz, int *err)
{
	int offset;

	/*
	 * Its safe to read more than we are asked, caller should ensure that
	 * destination has enough space.
	 */
	offset = src_paddr & 15;
	src_paddr = round_down(src_paddr, 16);
	sz = round_up(sz + offset, 16);

	return __sev_issue_dbg_cmd(kvm, src_paddr, dst_paddr, sz, err, false);
}

static int __sev_dbg_decrypt_user(struct kvm *kvm, unsigned long paddr,
				  void __user *dst_uaddr,
				  unsigned long dst_paddr,
				  int size, int *err)
{
	struct page *tpage = NULL;
	int ret, offset;

	/* if inputs are not 16-byte then use intermediate buffer */
	if (!IS_ALIGNED(dst_paddr, 16) ||
	    !IS_ALIGNED(paddr,     16) ||
	    !IS_ALIGNED(size,      16)) {
		tpage = (void *)alloc_page(GFP_KERNEL_ACCOUNT | __GFP_ZERO);
		if (!tpage)
			return -ENOMEM;

		dst_paddr = __sme_page_pa(tpage);
	}

	ret = __sev_dbg_decrypt(kvm, paddr, dst_paddr, size, err);
	if (ret)
		goto e_free;

	if (tpage) {
		offset = paddr & 15;
		if (copy_to_user(dst_uaddr, page_address(tpage) + offset, size))
			ret = -EFAULT;
	}

e_free:
	if (tpage)
		__free_page(tpage);

	return ret;
}

static int __sev_dbg_encrypt_user(struct kvm *kvm, unsigned long paddr,
				  void __user *vaddr,
				  unsigned long dst_paddr,
				  void __user *dst_vaddr,
				  int size, int *error)
{
	struct page *src_tpage = NULL;
	struct page *dst_tpage = NULL;
	int ret, len = size;

	/* If source buffer is not aligned then use an intermediate buffer */
	if (!IS_ALIGNED((unsigned long)vaddr, 16)) {
		src_tpage = alloc_page(GFP_KERNEL_ACCOUNT);
		if (!src_tpage)
			return -ENOMEM;

		if (copy_from_user(page_address(src_tpage), vaddr, size)) {
			__free_page(src_tpage);
			return -EFAULT;
		}

		paddr = __sme_page_pa(src_tpage);
	}

	/*
	 *  If destination buffer or length is not aligned then do read-modify-write:
	 *   - decrypt destination in an intermediate buffer
	 *   - copy the source buffer in an intermediate buffer
	 *   - use the intermediate buffer as source buffer
	 */
	if (!IS_ALIGNED((unsigned long)dst_vaddr, 16) || !IS_ALIGNED(size, 16)) {
		int dst_offset;

		dst_tpage = alloc_page(GFP_KERNEL_ACCOUNT);
		if (!dst_tpage) {
			ret = -ENOMEM;
			goto e_free;
		}

		ret = __sev_dbg_decrypt(kvm, dst_paddr,
					__sme_page_pa(dst_tpage), size, error);
		if (ret)
			goto e_free;

		/*
		 *  If source is kernel buffer then use memcpy() otherwise
		 *  copy_from_user().
		 */
		dst_offset = dst_paddr & 15;

		if (src_tpage)
			memcpy(page_address(dst_tpage) + dst_offset,
			       page_address(src_tpage), size);
		else {
			if (copy_from_user(page_address(dst_tpage) + dst_offset,
					   vaddr, size)) {
				ret = -EFAULT;
				goto e_free;
			}
		}

		paddr = __sme_page_pa(dst_tpage);
		dst_paddr = round_down(dst_paddr, 16);
		len = round_up(size, 16);
	}

	ret = __sev_issue_dbg_cmd(kvm, paddr, dst_paddr, len, error, true);

e_free:
	if (src_tpage)
		__free_page(src_tpage);
	if (dst_tpage)
		__free_page(dst_tpage);
	return ret;
}

static int sev_dbg_crypt(struct kvm *kvm, struct kvm_sev_cmd *argp, bool dec)
{
	unsigned long vaddr, vaddr_end, next_vaddr;
	unsigned long dst_vaddr;
	struct page **src_p, **dst_p;
	struct kvm_sev_dbg debug;
	unsigned long n;
	unsigned int size;
	int ret;

	if (!sev_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&debug, u64_to_user_ptr(argp->data), sizeof(debug)))
		return -EFAULT;

	if (!debug.len || debug.src_uaddr + debug.len < debug.src_uaddr)
		return -EINVAL;
	if (!debug.dst_uaddr)
		return -EINVAL;

	vaddr = debug.src_uaddr;
	size = debug.len;
	vaddr_end = vaddr + size;
	dst_vaddr = debug.dst_uaddr;

	for (; vaddr < vaddr_end; vaddr = next_vaddr) {
		int len, s_off, d_off;

		/* lock userspace source and destination page */
		src_p = sev_pin_memory(kvm, vaddr & PAGE_MASK, PAGE_SIZE, &n, 0);
		if (IS_ERR(src_p))
			return PTR_ERR(src_p);

		dst_p = sev_pin_memory(kvm, dst_vaddr & PAGE_MASK, PAGE_SIZE, &n, 1);
		if (IS_ERR(dst_p)) {
			sev_unpin_memory(kvm, src_p, n);
			return PTR_ERR(dst_p);
		}

		/*
		 * Flush (on non-coherent CPUs) before DBG_{DE,EN}CRYPT read or modify
		 * the pages; flush the destination too so that future accesses do not
		 * see stale data.
		 */
		sev_clflush_pages(src_p, 1);
		sev_clflush_pages(dst_p, 1);

		/*
		 * Since user buffer may not be page aligned, calculate the
		 * offset within the page.
		 */
		s_off = vaddr & ~PAGE_MASK;
		d_off = dst_vaddr & ~PAGE_MASK;
		len = min_t(size_t, (PAGE_SIZE - s_off), size);

		if (dec)
			ret = __sev_dbg_decrypt_user(kvm,
						     __sme_page_pa(src_p[0]) + s_off,
						     (void __user *)dst_vaddr,
						     __sme_page_pa(dst_p[0]) + d_off,
						     len, &argp->error);
		else
			ret = __sev_dbg_encrypt_user(kvm,
						     __sme_page_pa(src_p[0]) + s_off,
						     (void __user *)vaddr,
						     __sme_page_pa(dst_p[0]) + d_off,
						     (void __user *)dst_vaddr,
						     len, &argp->error);

		sev_unpin_memory(kvm, src_p, n);
		sev_unpin_memory(kvm, dst_p, n);

		if (ret)
			goto err;

		next_vaddr = vaddr + len;
		dst_vaddr = dst_vaddr + len;
		size -= len;
	}
err:
	return ret;
}

static int sev_launch_secret(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_launch_secret data;
	struct kvm_sev_launch_secret params;
	struct page **pages;
	void *blob, *hdr;
	unsigned long n, i;
	int ret, offset;

	if (!sev_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data), sizeof(params)))
		return -EFAULT;

	pages = sev_pin_memory(kvm, params.guest_uaddr, params.guest_len, &n, 1);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	/*
	 * Flush (on non-coherent CPUs) before LAUNCH_SECRET encrypts pages in
	 * place; the cache may contain the data that was written unencrypted.
	 */
	sev_clflush_pages(pages, n);

	/*
	 * The secret must be copied into contiguous memory region, lets verify
	 * that userspace memory pages are contiguous before we issue command.
	 */
	if (get_num_contig_pages(0, pages, n) != n) {
		ret = -EINVAL;
		goto e_unpin_memory;
	}

	memset(&data, 0, sizeof(data));

	offset = params.guest_uaddr & (PAGE_SIZE - 1);
	data.guest_address = __sme_page_pa(pages[0]) + offset;
	data.guest_len = params.guest_len;

	blob = psp_copy_user_blob(params.trans_uaddr, params.trans_len);
	if (IS_ERR(blob)) {
		ret = PTR_ERR(blob);
		goto e_unpin_memory;
	}

	data.trans_address = __psp_pa(blob);
	data.trans_len = params.trans_len;

	hdr = psp_copy_user_blob(params.hdr_uaddr, params.hdr_len);
	if (IS_ERR(hdr)) {
		ret = PTR_ERR(hdr);
		goto e_free_blob;
	}
	data.hdr_address = __psp_pa(hdr);
	data.hdr_len = params.hdr_len;

	data.handle = sev->handle;
	ret = sev_issue_cmd(kvm, SEV_CMD_LAUNCH_UPDATE_SECRET, &data, &argp->error);

	kfree(hdr);

e_free_blob:
	kfree(blob);
e_unpin_memory:
	/* content of memory is updated, mark pages dirty */
	for (i = 0; i < n; i++) {
		set_page_dirty_lock(pages[i]);
		mark_page_accessed(pages[i]);
	}
	sev_unpin_memory(kvm, pages, n);
	return ret;
}

static int sev_get_attestation_report(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	void __user *report = u64_to_user_ptr(argp->data);
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_attestation_report data;
	struct kvm_sev_attestation_report params;
	void __user *p;
	void *blob = NULL;
	int ret;

	if (!sev_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data), sizeof(params)))
		return -EFAULT;

	memset(&data, 0, sizeof(data));

	/* User wants to query the blob length */
	if (!params.len)
		goto cmd;

	p = u64_to_user_ptr(params.uaddr);
	if (p) {
		if (params.len > SEV_FW_BLOB_MAX_SIZE)
			return -EINVAL;

		blob = kzalloc(params.len, GFP_KERNEL_ACCOUNT);
		if (!blob)
			return -ENOMEM;

		data.address = __psp_pa(blob);
		data.len = params.len;
		memcpy(data.mnonce, params.mnonce, sizeof(params.mnonce));
	}
cmd:
	data.handle = sev->handle;
	ret = sev_issue_cmd(kvm, SEV_CMD_ATTESTATION_REPORT, &data, &argp->error);
	/*
	 * If we query the session length, FW responded with expected data.
	 */
	if (!params.len)
		goto done;

	if (ret)
		goto e_free_blob;

	if (blob) {
		if (copy_to_user(p, blob, params.len))
			ret = -EFAULT;
	}

done:
	params.len = data.len;
	if (copy_to_user(report, &params, sizeof(params)))
		ret = -EFAULT;
e_free_blob:
	kfree(blob);
	return ret;
}

/* Userspace wants to query session length. */
static int
__sev_send_start_query_session_length(struct kvm *kvm, struct kvm_sev_cmd *argp,
				      struct kvm_sev_send_start *params)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_send_start data;
	int ret;

	memset(&data, 0, sizeof(data));
	data.handle = sev->handle;
	ret = sev_issue_cmd(kvm, SEV_CMD_SEND_START, &data, &argp->error);

	params->session_len = data.session_len;
	if (copy_to_user(u64_to_user_ptr(argp->data), params,
				sizeof(struct kvm_sev_send_start)))
		ret = -EFAULT;

	return ret;
}

static int sev_send_start(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_send_start data;
	struct kvm_sev_send_start params;
	void *amd_certs, *session_data;
	void *pdh_cert, *plat_certs;
	int ret;

	if (!sev_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data),
				sizeof(struct kvm_sev_send_start)))
		return -EFAULT;

	/* if session_len is zero, userspace wants to query the session length */
	if (!params.session_len)
		return __sev_send_start_query_session_length(kvm, argp,
				&params);

	/* some sanity checks */
	if (!params.pdh_cert_uaddr || !params.pdh_cert_len ||
	    !params.session_uaddr || params.session_len > SEV_FW_BLOB_MAX_SIZE)
		return -EINVAL;

	/* allocate the memory to hold the session data blob */
	session_data = kzalloc(params.session_len, GFP_KERNEL_ACCOUNT);
	if (!session_data)
		return -ENOMEM;

	/* copy the certificate blobs from userspace */
	pdh_cert = psp_copy_user_blob(params.pdh_cert_uaddr,
				params.pdh_cert_len);
	if (IS_ERR(pdh_cert)) {
		ret = PTR_ERR(pdh_cert);
		goto e_free_session;
	}

	plat_certs = psp_copy_user_blob(params.plat_certs_uaddr,
				params.plat_certs_len);
	if (IS_ERR(plat_certs)) {
		ret = PTR_ERR(plat_certs);
		goto e_free_pdh;
	}

	amd_certs = psp_copy_user_blob(params.amd_certs_uaddr,
				params.amd_certs_len);
	if (IS_ERR(amd_certs)) {
		ret = PTR_ERR(amd_certs);
		goto e_free_plat_cert;
	}

	/* populate the FW SEND_START field with system physical address */
	memset(&data, 0, sizeof(data));
	data.pdh_cert_address = __psp_pa(pdh_cert);
	data.pdh_cert_len = params.pdh_cert_len;
	data.plat_certs_address = __psp_pa(plat_certs);
	data.plat_certs_len = params.plat_certs_len;
	data.amd_certs_address = __psp_pa(amd_certs);
	data.amd_certs_len = params.amd_certs_len;
	data.session_address = __psp_pa(session_data);
	data.session_len = params.session_len;
	data.handle = sev->handle;

	ret = sev_issue_cmd(kvm, SEV_CMD_SEND_START, &data, &argp->error);

	if (!ret && copy_to_user(u64_to_user_ptr(params.session_uaddr),
			session_data, params.session_len)) {
		ret = -EFAULT;
		goto e_free_amd_cert;
	}

	params.policy = data.policy;
	params.session_len = data.session_len;
	if (copy_to_user(u64_to_user_ptr(argp->data), &params,
				sizeof(struct kvm_sev_send_start)))
		ret = -EFAULT;

e_free_amd_cert:
	kfree(amd_certs);
e_free_plat_cert:
	kfree(plat_certs);
e_free_pdh:
	kfree(pdh_cert);
e_free_session:
	kfree(session_data);
	return ret;
}

/* Userspace wants to query either header or trans length. */
static int
__sev_send_update_data_query_lengths(struct kvm *kvm, struct kvm_sev_cmd *argp,
				     struct kvm_sev_send_update_data *params)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_send_update_data data;
	int ret;

	memset(&data, 0, sizeof(data));
	data.handle = sev->handle;
	ret = sev_issue_cmd(kvm, SEV_CMD_SEND_UPDATE_DATA, &data, &argp->error);

	params->hdr_len = data.hdr_len;
	params->trans_len = data.trans_len;

	if (copy_to_user(u64_to_user_ptr(argp->data), params,
			 sizeof(struct kvm_sev_send_update_data)))
		ret = -EFAULT;

	return ret;
}

static int sev_send_update_data(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_send_update_data data;
	struct kvm_sev_send_update_data params;
	void *hdr, *trans_data;
	struct page **guest_page;
	unsigned long n;
	int ret, offset;

	if (!sev_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data),
			sizeof(struct kvm_sev_send_update_data)))
		return -EFAULT;

	/* userspace wants to query either header or trans length */
	if (!params.trans_len || !params.hdr_len)
		return __sev_send_update_data_query_lengths(kvm, argp, &params);

	if (!params.trans_uaddr || !params.guest_uaddr ||
	    !params.guest_len || !params.hdr_uaddr)
		return -EINVAL;

	/* Check if we are crossing the page boundary */
	offset = params.guest_uaddr & (PAGE_SIZE - 1);
	if (params.guest_len > PAGE_SIZE || (params.guest_len + offset) > PAGE_SIZE)
		return -EINVAL;

	/* Pin guest memory */
	guest_page = sev_pin_memory(kvm, params.guest_uaddr & PAGE_MASK,
				    PAGE_SIZE, &n, 0);
	if (IS_ERR(guest_page))
		return PTR_ERR(guest_page);

	/* allocate memory for header and transport buffer */
	ret = -ENOMEM;
	hdr = kzalloc(params.hdr_len, GFP_KERNEL_ACCOUNT);
	if (!hdr)
		goto e_unpin;

	trans_data = kzalloc(params.trans_len, GFP_KERNEL_ACCOUNT);
	if (!trans_data)
		goto e_free_hdr;

	memset(&data, 0, sizeof(data));
	data.hdr_address = __psp_pa(hdr);
	data.hdr_len = params.hdr_len;
	data.trans_address = __psp_pa(trans_data);
	data.trans_len = params.trans_len;

	/* The SEND_UPDATE_DATA command requires C-bit to be always set. */
	data.guest_address = (page_to_pfn(guest_page[0]) << PAGE_SHIFT) + offset;
	data.guest_address |= sev_me_mask;
	data.guest_len = params.guest_len;
	data.handle = sev->handle;

	ret = sev_issue_cmd(kvm, SEV_CMD_SEND_UPDATE_DATA, &data, &argp->error);

	if (ret)
		goto e_free_trans_data;

	/* copy transport buffer to user space */
	if (copy_to_user(u64_to_user_ptr(params.trans_uaddr),
			 trans_data, params.trans_len)) {
		ret = -EFAULT;
		goto e_free_trans_data;
	}

	/* Copy packet header to userspace. */
	if (copy_to_user(u64_to_user_ptr(params.hdr_uaddr), hdr,
			 params.hdr_len))
		ret = -EFAULT;

e_free_trans_data:
	kfree(trans_data);
e_free_hdr:
	kfree(hdr);
e_unpin:
	sev_unpin_memory(kvm, guest_page, n);

	return ret;
}

static int sev_send_finish(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_send_finish data;

	if (!sev_guest(kvm))
		return -ENOTTY;

	data.handle = sev->handle;
	return sev_issue_cmd(kvm, SEV_CMD_SEND_FINISH, &data, &argp->error);
}

static int sev_send_cancel(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_send_cancel data;

	if (!sev_guest(kvm))
		return -ENOTTY;

	data.handle = sev->handle;
	return sev_issue_cmd(kvm, SEV_CMD_SEND_CANCEL, &data, &argp->error);
}

static int sev_receive_start(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_receive_start start;
	struct kvm_sev_receive_start params;
	int *error = &argp->error;
	void *session_data;
	void *pdh_data;
	int ret;

	if (!sev_guest(kvm))
		return -ENOTTY;

	/* Get parameter from the userspace */
	if (copy_from_user(&params, u64_to_user_ptr(argp->data),
			sizeof(struct kvm_sev_receive_start)))
		return -EFAULT;

	/* some sanity checks */
	if (!params.pdh_uaddr || !params.pdh_len ||
	    !params.session_uaddr || !params.session_len)
		return -EINVAL;

	pdh_data = psp_copy_user_blob(params.pdh_uaddr, params.pdh_len);
	if (IS_ERR(pdh_data))
		return PTR_ERR(pdh_data);

	session_data = psp_copy_user_blob(params.session_uaddr,
			params.session_len);
	if (IS_ERR(session_data)) {
		ret = PTR_ERR(session_data);
		goto e_free_pdh;
	}

	memset(&start, 0, sizeof(start));
	start.handle = params.handle;
	start.policy = params.policy;
	start.pdh_cert_address = __psp_pa(pdh_data);
	start.pdh_cert_len = params.pdh_len;
	start.session_address = __psp_pa(session_data);
	start.session_len = params.session_len;

	/* create memory encryption context */
	ret = __sev_issue_cmd(argp->sev_fd, SEV_CMD_RECEIVE_START, &start,
				error);
	if (ret)
		goto e_free_session;

	/* Bind ASID to this guest */
	ret = sev_bind_asid(kvm, start.handle, error);
	if (ret) {
		sev_decommission(start.handle);
		goto e_free_session;
	}

	params.handle = start.handle;
	if (copy_to_user(u64_to_user_ptr(argp->data),
			 &params, sizeof(struct kvm_sev_receive_start))) {
		ret = -EFAULT;
		sev_unbind_asid(kvm, start.handle);
		goto e_free_session;
	}

    	sev->handle = start.handle;
	sev->fd = argp->sev_fd;

e_free_session:
	kfree(session_data);
e_free_pdh:
	kfree(pdh_data);

	return ret;
}

static int sev_receive_update_data(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct kvm_sev_receive_update_data params;
	struct sev_data_receive_update_data data;
	void *hdr = NULL, *trans = NULL;
	struct page **guest_page;
	unsigned long n;
	int ret, offset;

	if (!sev_guest(kvm))
		return -EINVAL;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data),
			sizeof(struct kvm_sev_receive_update_data)))
		return -EFAULT;

	if (!params.hdr_uaddr || !params.hdr_len ||
	    !params.guest_uaddr || !params.guest_len ||
	    !params.trans_uaddr || !params.trans_len)
		return -EINVAL;

	/* Check if we are crossing the page boundary */
	offset = params.guest_uaddr & (PAGE_SIZE - 1);
	if (params.guest_len > PAGE_SIZE || (params.guest_len + offset) > PAGE_SIZE)
		return -EINVAL;

	hdr = psp_copy_user_blob(params.hdr_uaddr, params.hdr_len);
	if (IS_ERR(hdr))
		return PTR_ERR(hdr);

	trans = psp_copy_user_blob(params.trans_uaddr, params.trans_len);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto e_free_hdr;
	}

	memset(&data, 0, sizeof(data));
	data.hdr_address = __psp_pa(hdr);
	data.hdr_len = params.hdr_len;
	data.trans_address = __psp_pa(trans);
	data.trans_len = params.trans_len;

	/* Pin guest memory */
	guest_page = sev_pin_memory(kvm, params.guest_uaddr & PAGE_MASK,
				    PAGE_SIZE, &n, 1);
	if (IS_ERR(guest_page)) {
		ret = PTR_ERR(guest_page);
		goto e_free_trans;
	}

	/*
	 * Flush (on non-coherent CPUs) before RECEIVE_UPDATE_DATA, the PSP
	 * encrypts the written data with the guest's key, and the cache may
	 * contain dirty, unencrypted data.
	 */
	sev_clflush_pages(guest_page, n);

	/* The RECEIVE_UPDATE_DATA command requires C-bit to be always set. */
	data.guest_address = (page_to_pfn(guest_page[0]) << PAGE_SHIFT) + offset;
	data.guest_address |= sev_me_mask;
	data.guest_len = params.guest_len;
	data.handle = sev->handle;

	ret = sev_issue_cmd(kvm, SEV_CMD_RECEIVE_UPDATE_DATA, &data,
				&argp->error);

	sev_unpin_memory(kvm, guest_page, n);

e_free_trans:
	kfree(trans);
e_free_hdr:
	kfree(hdr);

	return ret;
}

static int sev_receive_finish(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_receive_finish data;

	if (!sev_guest(kvm))
		return -ENOTTY;

	data.handle = sev->handle;
	return sev_issue_cmd(kvm, SEV_CMD_RECEIVE_FINISH, &data, &argp->error);
}

static bool is_cmd_allowed_from_mirror(u32 cmd_id)
{
	/*
	 * Allow mirrors VM to call KVM_SEV_LAUNCH_UPDATE_VMSA to enable SEV-ES
	 * active mirror VMs. Also allow the debugging and status commands.
	 */
	if (cmd_id == KVM_SEV_LAUNCH_UPDATE_VMSA ||
	    cmd_id == KVM_SEV_GUEST_STATUS || cmd_id == KVM_SEV_DBG_DECRYPT ||
	    cmd_id == KVM_SEV_DBG_ENCRYPT)
		return true;

	return false;
}

static int sev_lock_two_vms(struct kvm *dst_kvm, struct kvm *src_kvm)
{
	struct kvm_sev_info *dst_sev = &to_kvm_svm(dst_kvm)->sev_info;
	struct kvm_sev_info *src_sev = &to_kvm_svm(src_kvm)->sev_info;
	int r = -EBUSY;

	if (dst_kvm == src_kvm)
		return -EINVAL;

	/*
	 * Bail if these VMs are already involved in a migration to avoid
	 * deadlock between two VMs trying to migrate to/from each other.
	 */
	if (atomic_cmpxchg_acquire(&dst_sev->migration_in_progress, 0, 1))
		return -EBUSY;

	if (atomic_cmpxchg_acquire(&src_sev->migration_in_progress, 0, 1))
		goto release_dst;

	r = -EINTR;
	if (mutex_lock_killable(&dst_kvm->lock))
		goto release_src;
	if (mutex_lock_killable_nested(&src_kvm->lock, SINGLE_DEPTH_NESTING))
		goto unlock_dst;
	return 0;

unlock_dst:
	mutex_unlock(&dst_kvm->lock);
release_src:
	atomic_set_release(&src_sev->migration_in_progress, 0);
release_dst:
	atomic_set_release(&dst_sev->migration_in_progress, 0);
	return r;
}

static void sev_unlock_two_vms(struct kvm *dst_kvm, struct kvm *src_kvm)
{
	struct kvm_sev_info *dst_sev = &to_kvm_svm(dst_kvm)->sev_info;
	struct kvm_sev_info *src_sev = &to_kvm_svm(src_kvm)->sev_info;

	mutex_unlock(&dst_kvm->lock);
	mutex_unlock(&src_kvm->lock);
	atomic_set_release(&dst_sev->migration_in_progress, 0);
	atomic_set_release(&src_sev->migration_in_progress, 0);
}

/* vCPU mutex subclasses.  */
enum sev_migration_role {
	SEV_MIGRATION_SOURCE = 0,
	SEV_MIGRATION_TARGET,
	SEV_NR_MIGRATION_ROLES,
};

static int sev_lock_vcpus_for_migration(struct kvm *kvm,
					enum sev_migration_role role)
{
	struct kvm_vcpu *vcpu;
	unsigned long i, j;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (mutex_lock_killable_nested(&vcpu->mutex, role))
			goto out_unlock;

#ifdef CONFIG_PROVE_LOCKING
		if (!i)
			/*
			 * Reset the role to one that avoids colliding with
			 * the role used for the first vcpu mutex.
			 */
			role = SEV_NR_MIGRATION_ROLES;
		else
			mutex_release(&vcpu->mutex.dep_map, _THIS_IP_);
#endif
	}

	return 0;

out_unlock:

	kvm_for_each_vcpu(j, vcpu, kvm) {
		if (i == j)
			break;

#ifdef CONFIG_PROVE_LOCKING
		if (j)
			mutex_acquire(&vcpu->mutex.dep_map, role, 0, _THIS_IP_);
#endif

		mutex_unlock(&vcpu->mutex);
	}
	return -EINTR;
}

static void sev_unlock_vcpus_for_migration(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;
	unsigned long i;
	bool first = true;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (first)
			first = false;
		else
			mutex_acquire(&vcpu->mutex.dep_map,
				      SEV_NR_MIGRATION_ROLES, 0, _THIS_IP_);

		mutex_unlock(&vcpu->mutex);
	}
}

static void sev_migrate_from(struct kvm *dst_kvm, struct kvm *src_kvm)
{
	struct kvm_sev_info *dst = &to_kvm_svm(dst_kvm)->sev_info;
	struct kvm_sev_info *src = &to_kvm_svm(src_kvm)->sev_info;
	struct kvm_vcpu *dst_vcpu, *src_vcpu;
	struct vcpu_svm *dst_svm, *src_svm;
	struct kvm_sev_info *mirror;
	unsigned long i;

	dst->active = true;
	dst->asid = src->asid;
	dst->handle = src->handle;
	dst->pages_locked = src->pages_locked;
	dst->enc_context_owner = src->enc_context_owner;
	dst->es_active = src->es_active;
	dst->vmsa_features = src->vmsa_features;

	src->asid = 0;
	src->active = false;
	src->handle = 0;
	src->pages_locked = 0;
	src->enc_context_owner = NULL;
	src->es_active = false;

	list_cut_before(&dst->regions_list, &src->regions_list, &src->regions_list);

	/*
	 * If this VM has mirrors, "transfer" each mirror's refcount of the
	 * source to the destination (this KVM).  The caller holds a reference
	 * to the source, so there's no danger of use-after-free.
	 */
	list_cut_before(&dst->mirror_vms, &src->mirror_vms, &src->mirror_vms);
	list_for_each_entry(mirror, &dst->mirror_vms, mirror_entry) {
		kvm_get_kvm(dst_kvm);
		kvm_put_kvm(src_kvm);
		mirror->enc_context_owner = dst_kvm;
	}

	/*
	 * If this VM is a mirror, remove the old mirror from the owners list
	 * and add the new mirror to the list.
	 */
	if (is_mirroring_enc_context(dst_kvm)) {
		struct kvm_sev_info *owner_sev_info =
			&to_kvm_svm(dst->enc_context_owner)->sev_info;

		list_del(&src->mirror_entry);
		list_add_tail(&dst->mirror_entry, &owner_sev_info->mirror_vms);
	}

	kvm_for_each_vcpu(i, dst_vcpu, dst_kvm) {
		dst_svm = to_svm(dst_vcpu);

		sev_init_vmcb(dst_svm);

		if (!dst->es_active)
			continue;

		/*
		 * Note, the source is not required to have the same number of
		 * vCPUs as the destination when migrating a vanilla SEV VM.
		 */
		src_vcpu = kvm_get_vcpu(src_kvm, i);
		src_svm = to_svm(src_vcpu);

		/*
		 * Transfer VMSA and GHCB state to the destination.  Nullify and
		 * clear source fields as appropriate, the state now belongs to
		 * the destination.
		 */
		memcpy(&dst_svm->sev_es, &src_svm->sev_es, sizeof(src_svm->sev_es));
		dst_svm->vmcb->control.ghcb_gpa = src_svm->vmcb->control.ghcb_gpa;
		dst_svm->vmcb->control.vmsa_pa = src_svm->vmcb->control.vmsa_pa;
		dst_vcpu->arch.guest_state_protected = true;

		memset(&src_svm->sev_es, 0, sizeof(src_svm->sev_es));
		src_svm->vmcb->control.ghcb_gpa = INVALID_PAGE;
		src_svm->vmcb->control.vmsa_pa = INVALID_PAGE;
		src_vcpu->arch.guest_state_protected = false;
	}
}

static int sev_check_source_vcpus(struct kvm *dst, struct kvm *src)
{
	struct kvm_vcpu *src_vcpu;
	unsigned long i;

	if (!sev_es_guest(src))
		return 0;

	if (atomic_read(&src->online_vcpus) != atomic_read(&dst->online_vcpus))
		return -EINVAL;

	kvm_for_each_vcpu(i, src_vcpu, src) {
		if (!src_vcpu->arch.guest_state_protected)
			return -EINVAL;
	}

	return 0;
}

int sev_vm_move_enc_context_from(struct kvm *kvm, unsigned int source_fd)
{
	struct kvm_sev_info *dst_sev = &to_kvm_svm(kvm)->sev_info;
	struct kvm_sev_info *src_sev, *cg_cleanup_sev;
	CLASS(fd, f)(source_fd);
	struct kvm *source_kvm;
	bool charged = false;
	int ret;

	if (fd_empty(f))
		return -EBADF;

	if (!file_is_kvm(fd_file(f)))
		return -EBADF;

	source_kvm = fd_file(f)->private_data;
	ret = sev_lock_two_vms(kvm, source_kvm);
	if (ret)
		return ret;

	if (kvm->arch.vm_type != source_kvm->arch.vm_type ||
	    sev_guest(kvm) || !sev_guest(source_kvm)) {
		ret = -EINVAL;
		goto out_unlock;
	}

	src_sev = &to_kvm_svm(source_kvm)->sev_info;

	dst_sev->misc_cg = get_current_misc_cg();
	cg_cleanup_sev = dst_sev;
	if (dst_sev->misc_cg != src_sev->misc_cg) {
		ret = sev_misc_cg_try_charge(dst_sev);
		if (ret)
			goto out_dst_cgroup;
		charged = true;
	}

	ret = sev_lock_vcpus_for_migration(kvm, SEV_MIGRATION_SOURCE);
	if (ret)
		goto out_dst_cgroup;
	ret = sev_lock_vcpus_for_migration(source_kvm, SEV_MIGRATION_TARGET);
	if (ret)
		goto out_dst_vcpu;

	ret = sev_check_source_vcpus(kvm, source_kvm);
	if (ret)
		goto out_source_vcpu;

	sev_migrate_from(kvm, source_kvm);
	kvm_vm_dead(source_kvm);
	cg_cleanup_sev = src_sev;
	ret = 0;

out_source_vcpu:
	sev_unlock_vcpus_for_migration(source_kvm);
out_dst_vcpu:
	sev_unlock_vcpus_for_migration(kvm);
out_dst_cgroup:
	/* Operates on the source on success, on the destination on failure.  */
	if (charged)
		sev_misc_cg_uncharge(cg_cleanup_sev);
	put_misc_cg(cg_cleanup_sev->misc_cg);
	cg_cleanup_sev->misc_cg = NULL;
out_unlock:
	sev_unlock_two_vms(kvm, source_kvm);
	return ret;
}

int sev_dev_get_attr(u32 group, u64 attr, u64 *val)
{
	if (group != KVM_X86_GRP_SEV)
		return -ENXIO;

	switch (attr) {
	case KVM_X86_SEV_VMSA_FEATURES:
		*val = sev_supported_vmsa_features;
		return 0;

	default:
		return -ENXIO;
	}
}

/*
 * The guest context contains all the information, keys and metadata
 * associated with the guest that the firmware tracks to implement SEV
 * and SNP features. The firmware stores the guest context in hypervisor
 * provide page via the SNP_GCTX_CREATE command.
 */
static void *snp_context_create(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct sev_data_snp_addr data = {};
	void *context;
	int rc;

	/* Allocate memory for context page */
	context = snp_alloc_firmware_page(GFP_KERNEL_ACCOUNT);
	if (!context)
		return NULL;

	data.address = __psp_pa(context);
	rc = __sev_issue_cmd(argp->sev_fd, SEV_CMD_SNP_GCTX_CREATE, &data, &argp->error);
	if (rc) {
		pr_warn("Failed to create SEV-SNP context, rc %d fw_error %d",
			rc, argp->error);
		snp_free_firmware_page(context);
		return NULL;
	}

	return context;
}

static int snp_bind_asid(struct kvm *kvm, int *error)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_snp_activate data = {0};

	data.gctx_paddr = __psp_pa(sev->snp_context);
	data.asid = sev_get_asid(kvm);
	return sev_issue_cmd(kvm, SEV_CMD_SNP_ACTIVATE, &data, error);
}

static int snp_launch_start(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_snp_launch_start start = {0};
	struct kvm_sev_snp_launch_start params;
	int rc;

	if (!sev_snp_guest(kvm))
		return -ENOTTY;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data), sizeof(params)))
		return -EFAULT;

	/* Don't allow userspace to allocate memory for more than 1 SNP context. */
	if (sev->snp_context)
		return -EINVAL;

	if (params.flags)
		return -EINVAL;

	if (params.policy & ~SNP_POLICY_MASK_VALID)
		return -EINVAL;

	/* Check for policy bits that must be set */
	if (!(params.policy & SNP_POLICY_MASK_RSVD_MBO) ||
	    !(params.policy & SNP_POLICY_MASK_SMT))
		return -EINVAL;

	if (params.policy & SNP_POLICY_MASK_SINGLE_SOCKET)
		return -EINVAL;

	sev->snp_context = snp_context_create(kvm, argp);
	if (!sev->snp_context)
		return -ENOTTY;

	start.gctx_paddr = __psp_pa(sev->snp_context);
	start.policy = params.policy;
	memcpy(start.gosvw, params.gosvw, sizeof(params.gosvw));
	rc = __sev_issue_cmd(argp->sev_fd, SEV_CMD_SNP_LAUNCH_START, &start, &argp->error);
	if (rc) {
		pr_debug("%s: SEV_CMD_SNP_LAUNCH_START firmware command failed, rc %d\n",
			 __func__, rc);
		goto e_free_context;
	}

	sev->fd = argp->sev_fd;
	rc = snp_bind_asid(kvm, &argp->error);
	if (rc) {
		pr_debug("%s: Failed to bind ASID to SEV-SNP context, rc %d\n",
			 __func__, rc);
		goto e_free_context;
	}

	return 0;

e_free_context:
	snp_decommission_context(kvm);

	return rc;
}

struct sev_gmem_populate_args {
	__u8 type;
	int sev_fd;
	int fw_error;
};

static int sev_gmem_post_populate(struct kvm *kvm, gfn_t gfn_start, kvm_pfn_t pfn,
				  void __user *src, int order, void *opaque)
{
	struct sev_gmem_populate_args *sev_populate_args = opaque;
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	int n_private = 0, ret, i;
	int npages = (1 << order);
	gfn_t gfn;

	if (WARN_ON_ONCE(sev_populate_args->type != KVM_SEV_SNP_PAGE_TYPE_ZERO && !src))
		return -EINVAL;

	for (gfn = gfn_start, i = 0; gfn < gfn_start + npages; gfn++, i++) {
		struct sev_data_snp_launch_update fw_args = {0};
		bool assigned = false;
		int level;

		ret = snp_lookup_rmpentry((u64)pfn + i, &assigned, &level);
		if (ret || assigned) {
			pr_debug("%s: Failed to ensure GFN 0x%llx RMP entry is initial shared state, ret: %d assigned: %d\n",
				 __func__, gfn, ret, assigned);
			ret = ret ? -EINVAL : -EEXIST;
			goto err;
		}

		if (src) {
			void *vaddr = kmap_local_pfn(pfn + i);

			if (copy_from_user(vaddr, src + i * PAGE_SIZE, PAGE_SIZE)) {
				ret = -EFAULT;
				goto err;
			}
			kunmap_local(vaddr);
		}

		ret = rmp_make_private(pfn + i, gfn << PAGE_SHIFT, PG_LEVEL_4K,
				       sev_get_asid(kvm), true);
		if (ret)
			goto err;

		n_private++;

		fw_args.gctx_paddr = __psp_pa(sev->snp_context);
		fw_args.address = __sme_set(pfn_to_hpa(pfn + i));
		fw_args.page_size = PG_LEVEL_TO_RMP(PG_LEVEL_4K);
		fw_args.page_type = sev_populate_args->type;

		ret = __sev_issue_cmd(sev_populate_args->sev_fd, SEV_CMD_SNP_LAUNCH_UPDATE,
				      &fw_args, &sev_populate_args->fw_error);
		if (ret)
			goto fw_err;
	}

	return 0;

fw_err:
	/*
	 * If the firmware command failed handle the reclaim and cleanup of that
	 * PFN specially vs. prior pages which can be cleaned up below without
	 * needing to reclaim in advance.
	 *
	 * Additionally, when invalid CPUID function entries are detected,
	 * firmware writes the expected values into the page and leaves it
	 * unencrypted so it can be used for debugging and error-reporting.
	 *
	 * Copy this page back into the source buffer so userspace can use this
	 * information to provide information on which CPUID leaves/fields
	 * failed CPUID validation.
	 */
	if (!snp_page_reclaim(kvm, pfn + i) &&
	    sev_populate_args->type == KVM_SEV_SNP_PAGE_TYPE_CPUID &&
	    sev_populate_args->fw_error == SEV_RET_INVALID_PARAM) {
		void *vaddr = kmap_local_pfn(pfn + i);

		if (copy_to_user(src + i * PAGE_SIZE, vaddr, PAGE_SIZE))
			pr_debug("Failed to write CPUID page back to userspace\n");

		kunmap_local(vaddr);
	}

	/* pfn + i is hypervisor-owned now, so skip below cleanup for it. */
	n_private--;

err:
	pr_debug("%s: exiting with error ret %d (fw_error %d), restoring %d gmem PFNs to shared.\n",
		 __func__, ret, sev_populate_args->fw_error, n_private);
	for (i = 0; i < n_private; i++)
		kvm_rmp_make_shared(kvm, pfn + i, PG_LEVEL_4K);

	return ret;
}

static int snp_launch_update(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_gmem_populate_args sev_populate_args = {0};
	struct kvm_sev_snp_launch_update params;
	struct kvm_memory_slot *memslot;
	long npages, count;
	void __user *src;
	int ret = 0;

	if (!sev_snp_guest(kvm) || !sev->snp_context)
		return -EINVAL;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data), sizeof(params)))
		return -EFAULT;

	pr_debug("%s: GFN start 0x%llx length 0x%llx type %d flags %d\n", __func__,
		 params.gfn_start, params.len, params.type, params.flags);

	if (!PAGE_ALIGNED(params.len) || params.flags ||
	    (params.type != KVM_SEV_SNP_PAGE_TYPE_NORMAL &&
	     params.type != KVM_SEV_SNP_PAGE_TYPE_ZERO &&
	     params.type != KVM_SEV_SNP_PAGE_TYPE_UNMEASURED &&
	     params.type != KVM_SEV_SNP_PAGE_TYPE_SECRETS &&
	     params.type != KVM_SEV_SNP_PAGE_TYPE_CPUID))
		return -EINVAL;

	npages = params.len / PAGE_SIZE;

	/*
	 * For each GFN that's being prepared as part of the initial guest
	 * state, the following pre-conditions are verified:
	 *
	 *   1) The backing memslot is a valid private memslot.
	 *   2) The GFN has been set to private via KVM_SET_MEMORY_ATTRIBUTES
	 *      beforehand.
	 *   3) The PFN of the guest_memfd has not already been set to private
	 *      in the RMP table.
	 *
	 * The KVM MMU relies on kvm->mmu_invalidate_seq to retry nested page
	 * faults if there's a race between a fault and an attribute update via
	 * KVM_SET_MEMORY_ATTRIBUTES, and a similar approach could be utilized
	 * here. However, kvm->slots_lock guards against both this as well as
	 * concurrent memslot updates occurring while these checks are being
	 * performed, so use that here to make it easier to reason about the
	 * initial expected state and better guard against unexpected
	 * situations.
	 */
	mutex_lock(&kvm->slots_lock);

	memslot = gfn_to_memslot(kvm, params.gfn_start);
	if (!kvm_slot_can_be_private(memslot)) {
		ret = -EINVAL;
		goto out;
	}

	sev_populate_args.sev_fd = argp->sev_fd;
	sev_populate_args.type = params.type;
	src = params.type == KVM_SEV_SNP_PAGE_TYPE_ZERO ? NULL : u64_to_user_ptr(params.uaddr);

	count = kvm_gmem_populate(kvm, params.gfn_start, src, npages,
				  sev_gmem_post_populate, &sev_populate_args);
	if (count < 0) {
		argp->error = sev_populate_args.fw_error;
		pr_debug("%s: kvm_gmem_populate failed, ret %ld (fw_error %d)\n",
			 __func__, count, argp->error);
		ret = -EIO;
	} else {
		params.gfn_start += count;
		params.len -= count * PAGE_SIZE;
		if (params.type != KVM_SEV_SNP_PAGE_TYPE_ZERO)
			params.uaddr += count * PAGE_SIZE;

		ret = 0;
		if (copy_to_user(u64_to_user_ptr(argp->data), &params, sizeof(params)))
			ret = -EFAULT;
	}

out:
	mutex_unlock(&kvm->slots_lock);

	return ret;
}

static int snp_launch_update_vmsa(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_snp_launch_update data = {};
	struct kvm_vcpu *vcpu;
	unsigned long i;
	int ret;

	data.gctx_paddr = __psp_pa(sev->snp_context);
	data.page_type = SNP_PAGE_TYPE_VMSA;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		struct vcpu_svm *svm = to_svm(vcpu);
		u64 pfn = __pa(svm->sev_es.vmsa) >> PAGE_SHIFT;

		ret = sev_es_sync_vmsa(svm);
		if (ret)
			return ret;

		/* Transition the VMSA page to a firmware state. */
		ret = rmp_make_private(pfn, INITIAL_VMSA_GPA, PG_LEVEL_4K, sev->asid, true);
		if (ret)
			return ret;

		/* Issue the SNP command to encrypt the VMSA */
		data.address = __sme_pa(svm->sev_es.vmsa);
		ret = __sev_issue_cmd(argp->sev_fd, SEV_CMD_SNP_LAUNCH_UPDATE,
				      &data, &argp->error);
		if (ret) {
			snp_page_reclaim(kvm, pfn);

			return ret;
		}

		svm->vcpu.arch.guest_state_protected = true;
		/*
		 * SEV-ES (and thus SNP) guest mandates LBR Virtualization to
		 * be _always_ ON. Enable it only after setting
		 * guest_state_protected because KVM_SET_MSRS allows dynamic
		 * toggling of LBRV (for performance reason) on write access to
		 * MSR_IA32_DEBUGCTLMSR when guest_state_protected is not set.
		 */
		svm_enable_lbrv(vcpu);
	}

	return 0;
}

static int snp_launch_finish(struct kvm *kvm, struct kvm_sev_cmd *argp)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct kvm_sev_snp_launch_finish params;
	struct sev_data_snp_launch_finish *data;
	void *id_block = NULL, *id_auth = NULL;
	int ret;

	if (!sev_snp_guest(kvm))
		return -ENOTTY;

	if (!sev->snp_context)
		return -EINVAL;

	if (copy_from_user(&params, u64_to_user_ptr(argp->data), sizeof(params)))
		return -EFAULT;

	if (params.flags)
		return -EINVAL;

	/* Measure all vCPUs using LAUNCH_UPDATE before finalizing the launch flow. */
	ret = snp_launch_update_vmsa(kvm, argp);
	if (ret)
		return ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL_ACCOUNT);
	if (!data)
		return -ENOMEM;

	if (params.id_block_en) {
		id_block = psp_copy_user_blob(params.id_block_uaddr, KVM_SEV_SNP_ID_BLOCK_SIZE);
		if (IS_ERR(id_block)) {
			ret = PTR_ERR(id_block);
			goto e_free;
		}

		data->id_block_en = 1;
		data->id_block_paddr = __sme_pa(id_block);

		id_auth = psp_copy_user_blob(params.id_auth_uaddr, KVM_SEV_SNP_ID_AUTH_SIZE);
		if (IS_ERR(id_auth)) {
			ret = PTR_ERR(id_auth);
			goto e_free_id_block;
		}

		data->id_auth_paddr = __sme_pa(id_auth);

		if (params.auth_key_en)
			data->auth_key_en = 1;
	}

	data->vcek_disabled = params.vcek_disabled;

	memcpy(data->host_data, params.host_data, KVM_SEV_SNP_FINISH_DATA_SIZE);
	data->gctx_paddr = __psp_pa(sev->snp_context);
	ret = sev_issue_cmd(kvm, SEV_CMD_SNP_LAUNCH_FINISH, data, &argp->error);

	/*
	 * Now that there will be no more SNP_LAUNCH_UPDATE ioctls, private pages
	 * can be given to the guest simply by marking the RMP entry as private.
	 * This can happen on first access and also with KVM_PRE_FAULT_MEMORY.
	 */
	if (!ret)
		kvm->arch.pre_fault_allowed = true;

	kfree(id_auth);

e_free_id_block:
	kfree(id_block);

e_free:
	kfree(data);

	return ret;
}

int sev_mem_enc_ioctl(struct kvm *kvm, void __user *argp)
{
	struct kvm_sev_cmd sev_cmd;
	int r;

	if (!sev_enabled)
		return -ENOTTY;

	if (!argp)
		return 0;

	if (copy_from_user(&sev_cmd, argp, sizeof(struct kvm_sev_cmd)))
		return -EFAULT;

	mutex_lock(&kvm->lock);

	/* Only the enc_context_owner handles some memory enc operations. */
	if (is_mirroring_enc_context(kvm) &&
	    !is_cmd_allowed_from_mirror(sev_cmd.id)) {
		r = -EINVAL;
		goto out;
	}

	/*
	 * Once KVM_SEV_INIT2 initializes a KVM instance as an SNP guest, only
	 * allow the use of SNP-specific commands.
	 */
	if (sev_snp_guest(kvm) && sev_cmd.id < KVM_SEV_SNP_LAUNCH_START) {
		r = -EPERM;
		goto out;
	}

	switch (sev_cmd.id) {
	case KVM_SEV_ES_INIT:
		if (!sev_es_enabled) {
			r = -ENOTTY;
			goto out;
		}
		fallthrough;
	case KVM_SEV_INIT:
		r = sev_guest_init(kvm, &sev_cmd);
		break;
	case KVM_SEV_INIT2:
		r = sev_guest_init2(kvm, &sev_cmd);
		break;
	case KVM_SEV_LAUNCH_START:
		r = sev_launch_start(kvm, &sev_cmd);
		break;
	case KVM_SEV_LAUNCH_UPDATE_DATA:
		r = sev_launch_update_data(kvm, &sev_cmd);
		break;
	case KVM_SEV_LAUNCH_UPDATE_VMSA:
		r = sev_launch_update_vmsa(kvm, &sev_cmd);
		break;
	case KVM_SEV_LAUNCH_MEASURE:
		r = sev_launch_measure(kvm, &sev_cmd);
		break;
	case KVM_SEV_LAUNCH_FINISH:
		r = sev_launch_finish(kvm, &sev_cmd);
		break;
	case KVM_SEV_GUEST_STATUS:
		r = sev_guest_status(kvm, &sev_cmd);
		break;
	case KVM_SEV_DBG_DECRYPT:
		r = sev_dbg_crypt(kvm, &sev_cmd, true);
		break;
	case KVM_SEV_DBG_ENCRYPT:
		r = sev_dbg_crypt(kvm, &sev_cmd, false);
		break;
	case KVM_SEV_LAUNCH_SECRET:
		r = sev_launch_secret(kvm, &sev_cmd);
		break;
	case KVM_SEV_GET_ATTESTATION_REPORT:
		r = sev_get_attestation_report(kvm, &sev_cmd);
		break;
	case KVM_SEV_SEND_START:
		r = sev_send_start(kvm, &sev_cmd);
		break;
	case KVM_SEV_SEND_UPDATE_DATA:
		r = sev_send_update_data(kvm, &sev_cmd);
		break;
	case KVM_SEV_SEND_FINISH:
		r = sev_send_finish(kvm, &sev_cmd);
		break;
	case KVM_SEV_SEND_CANCEL:
		r = sev_send_cancel(kvm, &sev_cmd);
		break;
	case KVM_SEV_RECEIVE_START:
		r = sev_receive_start(kvm, &sev_cmd);
		break;
	case KVM_SEV_RECEIVE_UPDATE_DATA:
		r = sev_receive_update_data(kvm, &sev_cmd);
		break;
	case KVM_SEV_RECEIVE_FINISH:
		r = sev_receive_finish(kvm, &sev_cmd);
		break;
	case KVM_SEV_SNP_LAUNCH_START:
		r = snp_launch_start(kvm, &sev_cmd);
		break;
	case KVM_SEV_SNP_LAUNCH_UPDATE:
		r = snp_launch_update(kvm, &sev_cmd);
		break;
	case KVM_SEV_SNP_LAUNCH_FINISH:
		r = snp_launch_finish(kvm, &sev_cmd);
		break;
	default:
		r = -EINVAL;
		goto out;
	}

	if (copy_to_user(argp, &sev_cmd, sizeof(struct kvm_sev_cmd)))
		r = -EFAULT;

out:
	mutex_unlock(&kvm->lock);
	return r;
}

int sev_mem_enc_register_region(struct kvm *kvm,
				struct kvm_enc_region *range)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct enc_region *region;
	int ret = 0;

	if (!sev_guest(kvm))
		return -ENOTTY;

	/* If kvm is mirroring encryption context it isn't responsible for it */
	if (is_mirroring_enc_context(kvm))
		return -EINVAL;

	if (range->addr > ULONG_MAX || range->size > ULONG_MAX)
		return -EINVAL;

	region = kzalloc(sizeof(*region), GFP_KERNEL_ACCOUNT);
	if (!region)
		return -ENOMEM;

	mutex_lock(&kvm->lock);
	region->pages = sev_pin_memory(kvm, range->addr, range->size, &region->npages, 1);
	if (IS_ERR(region->pages)) {
		ret = PTR_ERR(region->pages);
		mutex_unlock(&kvm->lock);
		goto e_free;
	}

	/*
	 * The guest may change the memory encryption attribute from C=0 -> C=1
	 * or vice versa for this memory range. Lets make sure caches are
	 * flushed to ensure that guest data gets written into memory with
	 * correct C-bit.  Note, this must be done before dropping kvm->lock,
	 * as region and its array of pages can be freed by a different task
	 * once kvm->lock is released.
	 */
	sev_clflush_pages(region->pages, region->npages);

	region->uaddr = range->addr;
	region->size = range->size;

	list_add_tail(&region->list, &sev->regions_list);
	mutex_unlock(&kvm->lock);

	return ret;

e_free:
	kfree(region);
	return ret;
}

static struct enc_region *
find_enc_region(struct kvm *kvm, struct kvm_enc_region *range)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct list_head *head = &sev->regions_list;
	struct enc_region *i;

	list_for_each_entry(i, head, list) {
		if (i->uaddr == range->addr &&
		    i->size == range->size)
			return i;
	}

	return NULL;
}

static void __unregister_enc_region_locked(struct kvm *kvm,
					   struct enc_region *region)
{
	sev_unpin_memory(kvm, region->pages, region->npages);
	list_del(&region->list);
	kfree(region);
}

int sev_mem_enc_unregister_region(struct kvm *kvm,
				  struct kvm_enc_region *range)
{
	struct enc_region *region;
	int ret;

	/* If kvm is mirroring encryption context it isn't responsible for it */
	if (is_mirroring_enc_context(kvm))
		return -EINVAL;

	mutex_lock(&kvm->lock);

	if (!sev_guest(kvm)) {
		ret = -ENOTTY;
		goto failed;
	}

	region = find_enc_region(kvm, range);
	if (!region) {
		ret = -EINVAL;
		goto failed;
	}

	/*
	 * Ensure that all guest tagged cache entries are flushed before
	 * releasing the pages back to the system for use. CLFLUSH will
	 * not do this, so issue a WBINVD.
	 */
	wbinvd_on_all_cpus();

	__unregister_enc_region_locked(kvm, region);

	mutex_unlock(&kvm->lock);
	return 0;

failed:
	mutex_unlock(&kvm->lock);
	return ret;
}

int sev_vm_copy_enc_context_from(struct kvm *kvm, unsigned int source_fd)
{
	CLASS(fd, f)(source_fd);
	struct kvm *source_kvm;
	struct kvm_sev_info *source_sev, *mirror_sev;
	int ret;

	if (fd_empty(f))
		return -EBADF;

	if (!file_is_kvm(fd_file(f)))
		return -EBADF;

	source_kvm = fd_file(f)->private_data;
	ret = sev_lock_two_vms(kvm, source_kvm);
	if (ret)
		return ret;

	/*
	 * Mirrors of mirrors should work, but let's not get silly.  Also
	 * disallow out-of-band SEV/SEV-ES init if the target is already an
	 * SEV guest, or if vCPUs have been created.  KVM relies on vCPUs being
	 * created after SEV/SEV-ES initialization, e.g. to init intercepts.
	 */
	if (sev_guest(kvm) || !sev_guest(source_kvm) ||
	    is_mirroring_enc_context(source_kvm) || kvm->created_vcpus) {
		ret = -EINVAL;
		goto e_unlock;
	}

	/*
	 * The mirror kvm holds an enc_context_owner ref so its asid can't
	 * disappear until we're done with it
	 */
	source_sev = &to_kvm_svm(source_kvm)->sev_info;
	kvm_get_kvm(source_kvm);
	mirror_sev = &to_kvm_svm(kvm)->sev_info;
	list_add_tail(&mirror_sev->mirror_entry, &source_sev->mirror_vms);

	/* Set enc_context_owner and copy its encryption context over */
	mirror_sev->enc_context_owner = source_kvm;
	mirror_sev->active = true;
	mirror_sev->asid = source_sev->asid;
	mirror_sev->fd = source_sev->fd;
	mirror_sev->es_active = source_sev->es_active;
	mirror_sev->need_init = false;
	mirror_sev->handle = source_sev->handle;
	INIT_LIST_HEAD(&mirror_sev->regions_list);
	INIT_LIST_HEAD(&mirror_sev->mirror_vms);
	ret = 0;

	/*
	 * Do not copy ap_jump_table. Since the mirror does not share the same
	 * KVM contexts as the original, and they may have different
	 * memory-views.
	 */

e_unlock:
	sev_unlock_two_vms(kvm, source_kvm);
	return ret;
}

static int snp_decommission_context(struct kvm *kvm)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct sev_data_snp_addr data = {};
	int ret;

	/* If context is not created then do nothing */
	if (!sev->snp_context)
		return 0;

	/* Do the decommision, which will unbind the ASID from the SNP context */
	data.address = __sme_pa(sev->snp_context);
	down_write(&sev_deactivate_lock);
	ret = sev_do_cmd(SEV_CMD_SNP_DECOMMISSION, &data, NULL);
	up_write(&sev_deactivate_lock);

	if (WARN_ONCE(ret, "Failed to release guest context, ret %d", ret))
		return ret;

	snp_free_firmware_page(sev->snp_context);
	sev->snp_context = NULL;

	return 0;
}

void sev_vm_destroy(struct kvm *kvm)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	struct list_head *head = &sev->regions_list;
	struct list_head *pos, *q;

	if (!sev_guest(kvm))
		return;

	WARN_ON(!list_empty(&sev->mirror_vms));

	/* If this is a mirror_kvm release the enc_context_owner and skip sev cleanup */
	if (is_mirroring_enc_context(kvm)) {
		struct kvm *owner_kvm = sev->enc_context_owner;

		mutex_lock(&owner_kvm->lock);
		list_del(&sev->mirror_entry);
		mutex_unlock(&owner_kvm->lock);
		kvm_put_kvm(owner_kvm);
		return;
	}

	/*
	 * Ensure that all guest tagged cache entries are flushed before
	 * releasing the pages back to the system for use. CLFLUSH will
	 * not do this, so issue a WBINVD.
	 */
	wbinvd_on_all_cpus();

	/*
	 * if userspace was terminated before unregistering the memory regions
	 * then lets unpin all the registered memory.
	 */
	if (!list_empty(head)) {
		list_for_each_safe(pos, q, head) {
			__unregister_enc_region_locked(kvm,
				list_entry(pos, struct enc_region, list));
			cond_resched();
		}
	}

	if (sev_snp_guest(kvm)) {
		snp_guest_req_cleanup(kvm);

		/*
		 * Decomission handles unbinding of the ASID. If it fails for
		 * some unexpected reason, just leak the ASID.
		 */
		if (snp_decommission_context(kvm))
			return;
	} else {
		sev_unbind_asid(kvm, sev->handle);
	}

	sev_asid_free(sev);
}

void __init sev_set_cpu_caps(void)
{
	if (sev_enabled) {
		kvm_cpu_cap_set(X86_FEATURE_SEV);
		kvm_caps.supported_vm_types |= BIT(KVM_X86_SEV_VM);
	}
	if (sev_es_enabled) {
		kvm_cpu_cap_set(X86_FEATURE_SEV_ES);
		kvm_caps.supported_vm_types |= BIT(KVM_X86_SEV_ES_VM);
	}
	if (sev_snp_enabled) {
		kvm_cpu_cap_set(X86_FEATURE_SEV_SNP);
		kvm_caps.supported_vm_types |= BIT(KVM_X86_SNP_VM);
	}
}

void __init sev_hardware_setup(void)
{
	unsigned int eax, ebx, ecx, edx, sev_asid_count, sev_es_asid_count;
	bool sev_snp_supported = false;
	bool sev_es_supported = false;
	bool sev_supported = false;

	if (!sev_enabled || !npt_enabled || !nrips)
		goto out;

	/*
	 * SEV must obviously be supported in hardware.  Sanity check that the
	 * CPU supports decode assists, which is mandatory for SEV guests to
	 * support instruction emulation.  Ditto for flushing by ASID, as SEV
	 * guests are bound to a single ASID, i.e. KVM can't rotate to a new
	 * ASID to effect a TLB flush.
	 */
	if (!boot_cpu_has(X86_FEATURE_SEV) ||
	    WARN_ON_ONCE(!boot_cpu_has(X86_FEATURE_DECODEASSISTS)) ||
	    WARN_ON_ONCE(!boot_cpu_has(X86_FEATURE_FLUSHBYASID)))
		goto out;

	/* Retrieve SEV CPUID information */
	cpuid(0x8000001f, &eax, &ebx, &ecx, &edx);

	/* Set encryption bit location for SEV-ES guests */
	sev_enc_bit = ebx & 0x3f;

	/* Maximum number of encrypted guests supported simultaneously */
	max_sev_asid = ecx;
	if (!max_sev_asid)
		goto out;

	/* Minimum ASID value that should be used for SEV guest */
	min_sev_asid = edx;
	sev_me_mask = 1UL << (ebx & 0x3f);

	/*
	 * Initialize SEV ASID bitmaps. Allocate space for ASID 0 in the bitmap,
	 * even though it's never used, so that the bitmap is indexed by the
	 * actual ASID.
	 */
	nr_asids = max_sev_asid + 1;
	sev_asid_bitmap = bitmap_zalloc(nr_asids, GFP_KERNEL);
	if (!sev_asid_bitmap)
		goto out;

	sev_reclaim_asid_bitmap = bitmap_zalloc(nr_asids, GFP_KERNEL);
	if (!sev_reclaim_asid_bitmap) {
		bitmap_free(sev_asid_bitmap);
		sev_asid_bitmap = NULL;
		goto out;
	}

	if (min_sev_asid <= max_sev_asid) {
		sev_asid_count = max_sev_asid - min_sev_asid + 1;
		WARN_ON_ONCE(misc_cg_set_capacity(MISC_CG_RES_SEV, sev_asid_count));
	}
	sev_supported = true;

	/* SEV-ES support requested? */
	if (!sev_es_enabled)
		goto out;

	/*
	 * SEV-ES requires MMIO caching as KVM doesn't have access to the guest
	 * instruction stream, i.e. can't emulate in response to a #NPF and
	 * instead relies on #NPF(RSVD) being reflected into the guest as #VC
	 * (the guest can then do a #VMGEXIT to request MMIO emulation).
	 */
	if (!enable_mmio_caching)
		goto out;

	/* Does the CPU support SEV-ES? */
	if (!boot_cpu_has(X86_FEATURE_SEV_ES))
		goto out;

	if (!lbrv) {
		WARN_ONCE(!boot_cpu_has(X86_FEATURE_LBRV),
			  "LBRV must be present for SEV-ES support");
		goto out;
	}

	/* Has the system been allocated ASIDs for SEV-ES? */
	if (min_sev_asid == 1)
		goto out;

	sev_es_asid_count = min_sev_asid - 1;
	WARN_ON_ONCE(misc_cg_set_capacity(MISC_CG_RES_SEV_ES, sev_es_asid_count));
	sev_es_supported = true;
	sev_snp_supported = sev_snp_enabled && cc_platform_has(CC_ATTR_HOST_SEV_SNP);

out:
	if (boot_cpu_has(X86_FEATURE_SEV))
		pr_info("SEV %s (ASIDs %u - %u)\n",
			sev_supported ? min_sev_asid <= max_sev_asid ? "enabled" :
								       "unusable" :
								       "disabled",
			min_sev_asid, max_sev_asid);
	if (boot_cpu_has(X86_FEATURE_SEV_ES))
		pr_info("SEV-ES %s (ASIDs %u - %u)\n",
			sev_es_supported ? "enabled" : "disabled",
			min_sev_asid > 1 ? 1 : 0, min_sev_asid - 1);
	if (boot_cpu_has(X86_FEATURE_SEV_SNP))
		pr_info("SEV-SNP %s (ASIDs %u - %u)\n",
			sev_snp_supported ? "enabled" : "disabled",
			min_sev_asid > 1 ? 1 : 0, min_sev_asid - 1);

	sev_enabled = sev_supported;
	sev_es_enabled = sev_es_supported;
	sev_snp_enabled = sev_snp_supported;

	if (!sev_es_enabled || !cpu_feature_enabled(X86_FEATURE_DEBUG_SWAP) ||
	    !cpu_feature_enabled(X86_FEATURE_NO_NESTED_DATA_BP))
		sev_es_debug_swap_enabled = false;

	sev_supported_vmsa_features = 0;
	if (sev_es_debug_swap_enabled)
		sev_supported_vmsa_features |= SVM_SEV_FEAT_DEBUG_SWAP;
}

void sev_hardware_unsetup(void)
{
	if (!sev_enabled)
		return;

	/* No need to take sev_bitmap_lock, all VMs have been destroyed. */
	sev_flush_asids(1, max_sev_asid);

	bitmap_free(sev_asid_bitmap);
	bitmap_free(sev_reclaim_asid_bitmap);

	misc_cg_set_capacity(MISC_CG_RES_SEV, 0);
	misc_cg_set_capacity(MISC_CG_RES_SEV_ES, 0);
}

int sev_cpu_init(struct svm_cpu_data *sd)
{
	if (!sev_enabled)
		return 0;

	sd->sev_vmcbs = kcalloc(nr_asids, sizeof(void *), GFP_KERNEL);
	if (!sd->sev_vmcbs)
		return -ENOMEM;

	return 0;
}

/*
 * Pages used by hardware to hold guest encrypted state must be flushed before
 * returning them to the system.
 */
static void sev_flush_encrypted_page(struct kvm_vcpu *vcpu, void *va)
{
	unsigned int asid = sev_get_asid(vcpu->kvm);

	/*
	 * Note!  The address must be a kernel address, as regular page walk
	 * checks are performed by VM_PAGE_FLUSH, i.e. operating on a user
	 * address is non-deterministic and unsafe.  This function deliberately
	 * takes a pointer to deter passing in a user address.
	 */
	unsigned long addr = (unsigned long)va;

	/*
	 * If CPU enforced cache coherency for encrypted mappings of the
	 * same physical page is supported, use CLFLUSHOPT instead. NOTE: cache
	 * flush is still needed in order to work properly with DMA devices.
	 */
	if (boot_cpu_has(X86_FEATURE_SME_COHERENT)) {
		clflush_cache_range(va, PAGE_SIZE);
		return;
	}

	/*
	 * VM Page Flush takes a host virtual address and a guest ASID.  Fall
	 * back to WBINVD if this faults so as not to make any problems worse
	 * by leaving stale encrypted data in the cache.
	 */
	if (WARN_ON_ONCE(wrmsrl_safe(MSR_AMD64_VM_PAGE_FLUSH, addr | asid)))
		goto do_wbinvd;

	return;

do_wbinvd:
	wbinvd_on_all_cpus();
}

void sev_guest_memory_reclaimed(struct kvm *kvm)
{
	/*
	 * With SNP+gmem, private/encrypted memory is unreachable via the
	 * hva-based mmu notifiers, so these events are only actually
	 * pertaining to shared pages where there is no need to perform
	 * the WBINVD to flush associated caches.
	 */
	if (!sev_guest(kvm) || sev_snp_guest(kvm))
		return;

	wbinvd_on_all_cpus();
}

void sev_free_vcpu(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm;

	if (!sev_es_guest(vcpu->kvm))
		return;

	svm = to_svm(vcpu);

	/*
	 * If it's an SNP guest, then the VMSA was marked in the RMP table as
	 * a guest-owned page. Transition the page to hypervisor state before
	 * releasing it back to the system.
	 */
	if (sev_snp_guest(vcpu->kvm)) {
		u64 pfn = __pa(svm->sev_es.vmsa) >> PAGE_SHIFT;

		if (kvm_rmp_make_shared(vcpu->kvm, pfn, PG_LEVEL_4K))
			goto skip_vmsa_free;
	}

	if (vcpu->arch.guest_state_protected)
		sev_flush_encrypted_page(vcpu, svm->sev_es.vmsa);

	__free_page(virt_to_page(svm->sev_es.vmsa));

skip_vmsa_free:
	if (svm->sev_es.ghcb_sa_free)
		kvfree(svm->sev_es.ghcb_sa);
}

static void dump_ghcb(struct vcpu_svm *svm)
{
	struct ghcb *ghcb = svm->sev_es.ghcb;
	unsigned int nbits;

	/* Re-use the dump_invalid_vmcb module parameter */
	if (!dump_invalid_vmcb) {
		pr_warn_ratelimited("set kvm_amd.dump_invalid_vmcb=1 to dump internal KVM state.\n");
		return;
	}

	nbits = sizeof(ghcb->save.valid_bitmap) * 8;

	pr_err("GHCB (GPA=%016llx):\n", svm->vmcb->control.ghcb_gpa);
	pr_err("%-20s%016llx is_valid: %u\n", "sw_exit_code",
	       ghcb->save.sw_exit_code, ghcb_sw_exit_code_is_valid(ghcb));
	pr_err("%-20s%016llx is_valid: %u\n", "sw_exit_info_1",
	       ghcb->save.sw_exit_info_1, ghcb_sw_exit_info_1_is_valid(ghcb));
	pr_err("%-20s%016llx is_valid: %u\n", "sw_exit_info_2",
	       ghcb->save.sw_exit_info_2, ghcb_sw_exit_info_2_is_valid(ghcb));
	pr_err("%-20s%016llx is_valid: %u\n", "sw_scratch",
	       ghcb->save.sw_scratch, ghcb_sw_scratch_is_valid(ghcb));
	pr_err("%-20s%*pb\n", "valid_bitmap", nbits, ghcb->save.valid_bitmap);
}

static void sev_es_sync_to_ghcb(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct ghcb *ghcb = svm->sev_es.ghcb;

	/*
	 * The GHCB protocol so far allows for the following data
	 * to be returned:
	 *   GPRs RAX, RBX, RCX, RDX
	 *
	 * Copy their values, even if they may not have been written during the
	 * VM-Exit.  It's the guest's responsibility to not consume random data.
	 */
	ghcb_set_rax(ghcb, vcpu->arch.regs[VCPU_REGS_RAX]);
	ghcb_set_rbx(ghcb, vcpu->arch.regs[VCPU_REGS_RBX]);
	ghcb_set_rcx(ghcb, vcpu->arch.regs[VCPU_REGS_RCX]);
	ghcb_set_rdx(ghcb, vcpu->arch.regs[VCPU_REGS_RDX]);
}

static void sev_es_sync_from_ghcb(struct vcpu_svm *svm)
{
	struct vmcb_control_area *control = &svm->vmcb->control;
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct ghcb *ghcb = svm->sev_es.ghcb;
	u64 exit_code;

	/*
	 * The GHCB protocol so far allows for the following data
	 * to be supplied:
	 *   GPRs RAX, RBX, RCX, RDX
	 *   XCR0
	 *   CPL
	 *
	 * VMMCALL allows the guest to provide extra registers. KVM also
	 * expects RSI for hypercalls, so include that, too.
	 *
	 * Copy their values to the appropriate location if supplied.
	 */
	memset(vcpu->arch.regs, 0, sizeof(vcpu->arch.regs));

	BUILD_BUG_ON(sizeof(svm->sev_es.valid_bitmap) != sizeof(ghcb->save.valid_bitmap));
	memcpy(&svm->sev_es.valid_bitmap, &ghcb->save.valid_bitmap, sizeof(ghcb->save.valid_bitmap));

	vcpu->arch.regs[VCPU_REGS_RAX] = kvm_ghcb_get_rax_if_valid(svm, ghcb);
	vcpu->arch.regs[VCPU_REGS_RBX] = kvm_ghcb_get_rbx_if_valid(svm, ghcb);
	vcpu->arch.regs[VCPU_REGS_RCX] = kvm_ghcb_get_rcx_if_valid(svm, ghcb);
	vcpu->arch.regs[VCPU_REGS_RDX] = kvm_ghcb_get_rdx_if_valid(svm, ghcb);
	vcpu->arch.regs[VCPU_REGS_RSI] = kvm_ghcb_get_rsi_if_valid(svm, ghcb);

	svm->vmcb->save.cpl = kvm_ghcb_get_cpl_if_valid(svm, ghcb);

	if (kvm_ghcb_xcr0_is_valid(svm)) {
		vcpu->arch.xcr0 = ghcb_get_xcr0(ghcb);
		kvm_update_cpuid_runtime(vcpu);
	}

	/* Copy the GHCB exit information into the VMCB fields */
	exit_code = ghcb_get_sw_exit_code(ghcb);
	control->exit_code = lower_32_bits(exit_code);
	control->exit_code_hi = upper_32_bits(exit_code);
	control->exit_info_1 = ghcb_get_sw_exit_info_1(ghcb);
	control->exit_info_2 = ghcb_get_sw_exit_info_2(ghcb);
	svm->sev_es.sw_scratch = kvm_ghcb_get_sw_scratch_if_valid(svm, ghcb);

	/* Clear the valid entries fields */
	memset(ghcb->save.valid_bitmap, 0, sizeof(ghcb->save.valid_bitmap));
}

static u64 kvm_ghcb_get_sw_exit_code(struct vmcb_control_area *control)
{
	return (((u64)control->exit_code_hi) << 32) | control->exit_code;
}

static int sev_es_validate_vmgexit(struct vcpu_svm *svm)
{
	struct vmcb_control_area *control = &svm->vmcb->control;
	struct kvm_vcpu *vcpu = &svm->vcpu;
	u64 exit_code;
	u64 reason;

	/*
	 * Retrieve the exit code now even though it may not be marked valid
	 * as it could help with debugging.
	 */
	exit_code = kvm_ghcb_get_sw_exit_code(control);

	/* Only GHCB Usage code 0 is supported */
	if (svm->sev_es.ghcb->ghcb_usage) {
		reason = GHCB_ERR_INVALID_USAGE;
		goto vmgexit_err;
	}

	reason = GHCB_ERR_MISSING_INPUT;

	if (!kvm_ghcb_sw_exit_code_is_valid(svm) ||
	    !kvm_ghcb_sw_exit_info_1_is_valid(svm) ||
	    !kvm_ghcb_sw_exit_info_2_is_valid(svm))
		goto vmgexit_err;

	switch (exit_code) {
	case SVM_EXIT_READ_DR7:
		break;
	case SVM_EXIT_WRITE_DR7:
		if (!kvm_ghcb_rax_is_valid(svm))
			goto vmgexit_err;
		break;
	case SVM_EXIT_RDTSC:
		break;
	case SVM_EXIT_RDPMC:
		if (!kvm_ghcb_rcx_is_valid(svm))
			goto vmgexit_err;
		break;
	case SVM_EXIT_CPUID:
		if (!kvm_ghcb_rax_is_valid(svm) ||
		    !kvm_ghcb_rcx_is_valid(svm))
			goto vmgexit_err;
		if (vcpu->arch.regs[VCPU_REGS_RAX] == 0xd)
			if (!kvm_ghcb_xcr0_is_valid(svm))
				goto vmgexit_err;
		break;
	case SVM_EXIT_INVD:
		break;
	case SVM_EXIT_IOIO:
		if (control->exit_info_1 & SVM_IOIO_STR_MASK) {
			if (!kvm_ghcb_sw_scratch_is_valid(svm))
				goto vmgexit_err;
		} else {
			if (!(control->exit_info_1 & SVM_IOIO_TYPE_MASK))
				if (!kvm_ghcb_rax_is_valid(svm))
					goto vmgexit_err;
		}
		break;
	case SVM_EXIT_MSR:
		if (!kvm_ghcb_rcx_is_valid(svm))
			goto vmgexit_err;
		if (control->exit_info_1) {
			if (!kvm_ghcb_rax_is_valid(svm) ||
			    !kvm_ghcb_rdx_is_valid(svm))
				goto vmgexit_err;
		}
		break;
	case SVM_EXIT_VMMCALL:
		if (!kvm_ghcb_rax_is_valid(svm) ||
		    !kvm_ghcb_cpl_is_valid(svm))
			goto vmgexit_err;
		break;
	case SVM_EXIT_RDTSCP:
		break;
	case SVM_EXIT_WBINVD:
		break;
	case SVM_EXIT_MONITOR:
		if (!kvm_ghcb_rax_is_valid(svm) ||
		    !kvm_ghcb_rcx_is_valid(svm) ||
		    !kvm_ghcb_rdx_is_valid(svm))
			goto vmgexit_err;
		break;
	case SVM_EXIT_MWAIT:
		if (!kvm_ghcb_rax_is_valid(svm) ||
		    !kvm_ghcb_rcx_is_valid(svm))
			goto vmgexit_err;
		break;
	case SVM_VMGEXIT_MMIO_READ:
	case SVM_VMGEXIT_MMIO_WRITE:
		if (!kvm_ghcb_sw_scratch_is_valid(svm))
			goto vmgexit_err;
		break;
	case SVM_VMGEXIT_AP_CREATION:
		if (!sev_snp_guest(vcpu->kvm))
			goto vmgexit_err;
		if (lower_32_bits(control->exit_info_1) != SVM_VMGEXIT_AP_DESTROY)
			if (!kvm_ghcb_rax_is_valid(svm))
				goto vmgexit_err;
		break;
	case SVM_VMGEXIT_NMI_COMPLETE:
	case SVM_VMGEXIT_AP_HLT_LOOP:
	case SVM_VMGEXIT_AP_JUMP_TABLE:
	case SVM_VMGEXIT_UNSUPPORTED_EVENT:
	case SVM_VMGEXIT_HV_FEATURES:
	case SVM_VMGEXIT_TERM_REQUEST:
		break;
	case SVM_VMGEXIT_PSC:
		if (!sev_snp_guest(vcpu->kvm) || !kvm_ghcb_sw_scratch_is_valid(svm))
			goto vmgexit_err;
		break;
	case SVM_VMGEXIT_GUEST_REQUEST:
	case SVM_VMGEXIT_EXT_GUEST_REQUEST:
		if (!sev_snp_guest(vcpu->kvm) ||
		    !PAGE_ALIGNED(control->exit_info_1) ||
		    !PAGE_ALIGNED(control->exit_info_2) ||
		    control->exit_info_1 == control->exit_info_2)
			goto vmgexit_err;
		break;
	default:
		reason = GHCB_ERR_INVALID_EVENT;
		goto vmgexit_err;
	}

	return 0;

vmgexit_err:
	if (reason == GHCB_ERR_INVALID_USAGE) {
		vcpu_unimpl(vcpu, "vmgexit: ghcb usage %#x is not valid\n",
			    svm->sev_es.ghcb->ghcb_usage);
	} else if (reason == GHCB_ERR_INVALID_EVENT) {
		vcpu_unimpl(vcpu, "vmgexit: exit code %#llx is not valid\n",
			    exit_code);
	} else {
		vcpu_unimpl(vcpu, "vmgexit: exit code %#llx input is not valid\n",
			    exit_code);
		dump_ghcb(svm);
	}

	ghcb_set_sw_exit_info_1(svm->sev_es.ghcb, 2);
	ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, reason);

	/* Resume the guest to "return" the error code. */
	return 1;
}

void sev_es_unmap_ghcb(struct vcpu_svm *svm)
{
	/* Clear any indication that the vCPU is in a type of AP Reset Hold */
	svm->sev_es.ap_reset_hold_type = AP_RESET_HOLD_NONE;

	if (!svm->sev_es.ghcb)
		return;

	if (svm->sev_es.ghcb_sa_free) {
		/*
		 * The scratch area lives outside the GHCB, so there is a
		 * buffer that, depending on the operation performed, may
		 * need to be synced, then freed.
		 */
		if (svm->sev_es.ghcb_sa_sync) {
			kvm_write_guest(svm->vcpu.kvm,
					svm->sev_es.sw_scratch,
					svm->sev_es.ghcb_sa,
					svm->sev_es.ghcb_sa_len);
			svm->sev_es.ghcb_sa_sync = false;
		}

		kvfree(svm->sev_es.ghcb_sa);
		svm->sev_es.ghcb_sa = NULL;
		svm->sev_es.ghcb_sa_free = false;
	}

	trace_kvm_vmgexit_exit(svm->vcpu.vcpu_id, svm->sev_es.ghcb);

	sev_es_sync_to_ghcb(svm);

	kvm_vcpu_unmap(&svm->vcpu, &svm->sev_es.ghcb_map);
	svm->sev_es.ghcb = NULL;
}

void pre_sev_run(struct vcpu_svm *svm, int cpu)
{
	struct svm_cpu_data *sd = per_cpu_ptr(&svm_data, cpu);
	unsigned int asid = sev_get_asid(svm->vcpu.kvm);

	/* Assign the asid allocated with this SEV guest */
	svm->asid = asid;

	/*
	 * Flush guest TLB:
	 *
	 * 1) when different VMCB for the same ASID is to be run on the same host CPU.
	 * 2) or this VMCB was executed on different host CPU in previous VMRUNs.
	 */
	if (sd->sev_vmcbs[asid] == svm->vmcb &&
	    svm->vcpu.arch.last_vmentry_cpu == cpu)
		return;

	sd->sev_vmcbs[asid] = svm->vmcb;
	svm->vmcb->control.tlb_ctl = TLB_CONTROL_FLUSH_ASID;
	vmcb_mark_dirty(svm->vmcb, VMCB_ASID);
}

#define GHCB_SCRATCH_AREA_LIMIT		(16ULL * PAGE_SIZE)
static int setup_vmgexit_scratch(struct vcpu_svm *svm, bool sync, u64 len)
{
	struct vmcb_control_area *control = &svm->vmcb->control;
	u64 ghcb_scratch_beg, ghcb_scratch_end;
	u64 scratch_gpa_beg, scratch_gpa_end;
	void *scratch_va;

	scratch_gpa_beg = svm->sev_es.sw_scratch;
	if (!scratch_gpa_beg) {
		pr_err("vmgexit: scratch gpa not provided\n");
		goto e_scratch;
	}

	scratch_gpa_end = scratch_gpa_beg + len;
	if (scratch_gpa_end < scratch_gpa_beg) {
		pr_err("vmgexit: scratch length (%#llx) not valid for scratch address (%#llx)\n",
		       len, scratch_gpa_beg);
		goto e_scratch;
	}

	if ((scratch_gpa_beg & PAGE_MASK) == control->ghcb_gpa) {
		/* Scratch area begins within GHCB */
		ghcb_scratch_beg = control->ghcb_gpa +
				   offsetof(struct ghcb, shared_buffer);
		ghcb_scratch_end = control->ghcb_gpa +
				   offsetof(struct ghcb, reserved_0xff0);

		/*
		 * If the scratch area begins within the GHCB, it must be
		 * completely contained in the GHCB shared buffer area.
		 */
		if (scratch_gpa_beg < ghcb_scratch_beg ||
		    scratch_gpa_end > ghcb_scratch_end) {
			pr_err("vmgexit: scratch area is outside of GHCB shared buffer area (%#llx - %#llx)\n",
			       scratch_gpa_beg, scratch_gpa_end);
			goto e_scratch;
		}

		scratch_va = (void *)svm->sev_es.ghcb;
		scratch_va += (scratch_gpa_beg - control->ghcb_gpa);
	} else {
		/*
		 * The guest memory must be read into a kernel buffer, so
		 * limit the size
		 */
		if (len > GHCB_SCRATCH_AREA_LIMIT) {
			pr_err("vmgexit: scratch area exceeds KVM limits (%#llx requested, %#llx limit)\n",
			       len, GHCB_SCRATCH_AREA_LIMIT);
			goto e_scratch;
		}
		scratch_va = kvzalloc(len, GFP_KERNEL_ACCOUNT);
		if (!scratch_va)
			return -ENOMEM;

		if (kvm_read_guest(svm->vcpu.kvm, scratch_gpa_beg, scratch_va, len)) {
			/* Unable to copy scratch area from guest */
			pr_err("vmgexit: kvm_read_guest for scratch area failed\n");

			kvfree(scratch_va);
			return -EFAULT;
		}

		/*
		 * The scratch area is outside the GHCB. The operation will
		 * dictate whether the buffer needs to be synced before running
		 * the vCPU next time (i.e. a read was requested so the data
		 * must be written back to the guest memory).
		 */
		svm->sev_es.ghcb_sa_sync = sync;
		svm->sev_es.ghcb_sa_free = true;
	}

	svm->sev_es.ghcb_sa = scratch_va;
	svm->sev_es.ghcb_sa_len = len;

	return 0;

e_scratch:
	ghcb_set_sw_exit_info_1(svm->sev_es.ghcb, 2);
	ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, GHCB_ERR_INVALID_SCRATCH_AREA);

	return 1;
}

static void set_ghcb_msr_bits(struct vcpu_svm *svm, u64 value, u64 mask,
			      unsigned int pos)
{
	svm->vmcb->control.ghcb_gpa &= ~(mask << pos);
	svm->vmcb->control.ghcb_gpa |= (value & mask) << pos;
}

static u64 get_ghcb_msr_bits(struct vcpu_svm *svm, u64 mask, unsigned int pos)
{
	return (svm->vmcb->control.ghcb_gpa >> pos) & mask;
}

static void set_ghcb_msr(struct vcpu_svm *svm, u64 value)
{
	svm->vmcb->control.ghcb_gpa = value;
}

static int snp_rmptable_psmash(kvm_pfn_t pfn)
{
	int ret;

	pfn = pfn & ~(KVM_PAGES_PER_HPAGE(PG_LEVEL_2M) - 1);

	/*
	 * PSMASH_FAIL_INUSE indicates another processor is modifying the
	 * entry, so retry until that's no longer the case.
	 */
	do {
		ret = psmash(pfn);
	} while (ret == PSMASH_FAIL_INUSE);

	return ret;
}

static int snp_complete_psc_msr(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (vcpu->run->hypercall.ret)
		set_ghcb_msr(svm, GHCB_MSR_PSC_RESP_ERROR);
	else
		set_ghcb_msr(svm, GHCB_MSR_PSC_RESP);

	return 1; /* resume guest */
}

static int snp_begin_psc_msr(struct vcpu_svm *svm, u64 ghcb_msr)
{
	u64 gpa = gfn_to_gpa(GHCB_MSR_PSC_REQ_TO_GFN(ghcb_msr));
	u8 op = GHCB_MSR_PSC_REQ_TO_OP(ghcb_msr);
	struct kvm_vcpu *vcpu = &svm->vcpu;

	if (op != SNP_PAGE_STATE_PRIVATE && op != SNP_PAGE_STATE_SHARED) {
		set_ghcb_msr(svm, GHCB_MSR_PSC_RESP_ERROR);
		return 1; /* resume guest */
	}

	if (!(vcpu->kvm->arch.hypercall_exit_enabled & (1 << KVM_HC_MAP_GPA_RANGE))) {
		set_ghcb_msr(svm, GHCB_MSR_PSC_RESP_ERROR);
		return 1; /* resume guest */
	}

	vcpu->run->exit_reason = KVM_EXIT_HYPERCALL;
	vcpu->run->hypercall.nr = KVM_HC_MAP_GPA_RANGE;
	vcpu->run->hypercall.args[0] = gpa;
	vcpu->run->hypercall.args[1] = 1;
	vcpu->run->hypercall.args[2] = (op == SNP_PAGE_STATE_PRIVATE)
				       ? KVM_MAP_GPA_RANGE_ENCRYPTED
				       : KVM_MAP_GPA_RANGE_DECRYPTED;
	vcpu->run->hypercall.args[2] |= KVM_MAP_GPA_RANGE_PAGE_SZ_4K;

	vcpu->arch.complete_userspace_io = snp_complete_psc_msr;

	return 0; /* forward request to userspace */
}

struct psc_buffer {
	struct psc_hdr hdr;
	struct psc_entry entries[];
} __packed;

static int snp_begin_psc(struct vcpu_svm *svm, struct psc_buffer *psc);

static void snp_complete_psc(struct vcpu_svm *svm, u64 psc_ret)
{
	svm->sev_es.psc_inflight = 0;
	svm->sev_es.psc_idx = 0;
	svm->sev_es.psc_2m = false;
	ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, psc_ret);
}

static void __snp_complete_one_psc(struct vcpu_svm *svm)
{
	struct psc_buffer *psc = svm->sev_es.ghcb_sa;
	struct psc_entry *entries = psc->entries;
	struct psc_hdr *hdr = &psc->hdr;
	__u16 idx;

	/*
	 * Everything in-flight has been processed successfully. Update the
	 * corresponding entries in the guest's PSC buffer and zero out the
	 * count of in-flight PSC entries.
	 */
	for (idx = svm->sev_es.psc_idx; svm->sev_es.psc_inflight;
	     svm->sev_es.psc_inflight--, idx++) {
		struct psc_entry *entry = &entries[idx];

		entry->cur_page = entry->pagesize ? 512 : 1;
	}

	hdr->cur_entry = idx;
}

static int snp_complete_one_psc(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct psc_buffer *psc = svm->sev_es.ghcb_sa;

	if (vcpu->run->hypercall.ret) {
		snp_complete_psc(svm, VMGEXIT_PSC_ERROR_GENERIC);
		return 1; /* resume guest */
	}

	__snp_complete_one_psc(svm);

	/* Handle the next range (if any). */
	return snp_begin_psc(svm, psc);
}

static int snp_begin_psc(struct vcpu_svm *svm, struct psc_buffer *psc)
{
	struct psc_entry *entries = psc->entries;
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct psc_hdr *hdr = &psc->hdr;
	struct psc_entry entry_start;
	u16 idx, idx_start, idx_end;
	int npages;
	bool huge;
	u64 gfn;

	if (!(vcpu->kvm->arch.hypercall_exit_enabled & (1 << KVM_HC_MAP_GPA_RANGE))) {
		snp_complete_psc(svm, VMGEXIT_PSC_ERROR_GENERIC);
		return 1;
	}

next_range:
	/* There should be no other PSCs in-flight at this point. */
	if (WARN_ON_ONCE(svm->sev_es.psc_inflight)) {
		snp_complete_psc(svm, VMGEXIT_PSC_ERROR_GENERIC);
		return 1;
	}

	/*
	 * The PSC descriptor buffer can be modified by a misbehaved guest after
	 * validation, so take care to only use validated copies of values used
	 * for things like array indexing.
	 */
	idx_start = hdr->cur_entry;
	idx_end = hdr->end_entry;

	if (idx_end >= VMGEXIT_PSC_MAX_COUNT) {
		snp_complete_psc(svm, VMGEXIT_PSC_ERROR_INVALID_HDR);
		return 1;
	}

	/* Find the start of the next range which needs processing. */
	for (idx = idx_start; idx <= idx_end; idx++, hdr->cur_entry++) {
		entry_start = entries[idx];

		gfn = entry_start.gfn;
		huge = entry_start.pagesize;
		npages = huge ? 512 : 1;

		if (entry_start.cur_page > npages || !IS_ALIGNED(gfn, npages)) {
			snp_complete_psc(svm, VMGEXIT_PSC_ERROR_INVALID_ENTRY);
			return 1;
		}

		if (entry_start.cur_page) {
			/*
			 * If this is a partially-completed 2M range, force 4K handling
			 * for the remaining pages since they're effectively split at
			 * this point. Subsequent code should ensure this doesn't get
			 * combined with adjacent PSC entries where 2M handling is still
			 * possible.
			 */
			npages -= entry_start.cur_page;
			gfn += entry_start.cur_page;
			huge = false;
		}

		if (npages)
			break;
	}

	if (idx > idx_end) {
		/* Nothing more to process. */
		snp_complete_psc(svm, 0);
		return 1;
	}

	svm->sev_es.psc_2m = huge;
	svm->sev_es.psc_idx = idx;
	svm->sev_es.psc_inflight = 1;

	/*
	 * Find all subsequent PSC entries that contain adjacent GPA
	 * ranges/operations and can be combined into a single
	 * KVM_HC_MAP_GPA_RANGE exit.
	 */
	while (++idx <= idx_end) {
		struct psc_entry entry = entries[idx];

		if (entry.operation != entry_start.operation ||
		    entry.gfn != entry_start.gfn + npages ||
		    entry.cur_page || !!entry.pagesize != huge)
			break;

		svm->sev_es.psc_inflight++;
		npages += huge ? 512 : 1;
	}

	switch (entry_start.operation) {
	case VMGEXIT_PSC_OP_PRIVATE:
	case VMGEXIT_PSC_OP_SHARED:
		vcpu->run->exit_reason = KVM_EXIT_HYPERCALL;
		vcpu->run->hypercall.nr = KVM_HC_MAP_GPA_RANGE;
		vcpu->run->hypercall.args[0] = gfn_to_gpa(gfn);
		vcpu->run->hypercall.args[1] = npages;
		vcpu->run->hypercall.args[2] = entry_start.operation == VMGEXIT_PSC_OP_PRIVATE
					       ? KVM_MAP_GPA_RANGE_ENCRYPTED
					       : KVM_MAP_GPA_RANGE_DECRYPTED;
		vcpu->run->hypercall.args[2] |= entry_start.pagesize
						? KVM_MAP_GPA_RANGE_PAGE_SZ_2M
						: KVM_MAP_GPA_RANGE_PAGE_SZ_4K;
		vcpu->arch.complete_userspace_io = snp_complete_one_psc;
		return 0; /* forward request to userspace */
	default:
		/*
		 * Only shared/private PSC operations are currently supported, so if the
		 * entire range consists of unsupported operations (e.g. SMASH/UNSMASH),
		 * then consider the entire range completed and avoid exiting to
		 * userspace. In theory snp_complete_psc() can always be called directly
		 * at this point to complete the current range and start the next one,
		 * but that could lead to unexpected levels of recursion.
		 */
		__snp_complete_one_psc(svm);
		goto next_range;
	}

	BUG();
}

static int __sev_snp_update_protected_guest_state(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	WARN_ON(!mutex_is_locked(&svm->sev_es.snp_vmsa_mutex));

	/* Mark the vCPU as offline and not runnable */
	vcpu->arch.pv.pv_unhalted = false;
	vcpu->arch.mp_state = KVM_MP_STATE_HALTED;

	/* Clear use of the VMSA */
	svm->vmcb->control.vmsa_pa = INVALID_PAGE;

	if (VALID_PAGE(svm->sev_es.snp_vmsa_gpa)) {
		gfn_t gfn = gpa_to_gfn(svm->sev_es.snp_vmsa_gpa);
		struct kvm_memory_slot *slot;
		struct page *page;
		kvm_pfn_t pfn;

		slot = gfn_to_memslot(vcpu->kvm, gfn);
		if (!slot)
			return -EINVAL;

		/*
		 * The new VMSA will be private memory guest memory, so
		 * retrieve the PFN from the gmem backend.
		 */
		if (kvm_gmem_get_pfn(vcpu->kvm, slot, gfn, &pfn, &page, NULL))
			return -EINVAL;

		/*
		 * From this point forward, the VMSA will always be a
		 * guest-mapped page rather than the initial one allocated
		 * by KVM in svm->sev_es.vmsa. In theory, svm->sev_es.vmsa
		 * could be free'd and cleaned up here, but that involves
		 * cleanups like wbinvd_on_all_cpus() which would ideally
		 * be handled during teardown rather than guest boot.
		 * Deferring that also allows the existing logic for SEV-ES
		 * VMSAs to be re-used with minimal SNP-specific changes.
		 */
		svm->sev_es.snp_has_guest_vmsa = true;

		/* Use the new VMSA */
		svm->vmcb->control.vmsa_pa = pfn_to_hpa(pfn);

		/* Mark the vCPU as runnable */
		vcpu->arch.pv.pv_unhalted = false;
		vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;

		svm->sev_es.snp_vmsa_gpa = INVALID_PAGE;

		/*
		 * gmem pages aren't currently migratable, but if this ever
		 * changes then care should be taken to ensure
		 * svm->sev_es.vmsa is pinned through some other means.
		 */
		kvm_release_page_clean(page);
	}

	/*
	 * When replacing the VMSA during SEV-SNP AP creation,
	 * mark the VMCB dirty so that full state is always reloaded.
	 */
	vmcb_mark_all_dirty(svm->vmcb);

	return 0;
}

/*
 * Invoked as part of svm_vcpu_reset() processing of an init event.
 */
void sev_snp_init_protected_guest_state(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int ret;

	if (!sev_snp_guest(vcpu->kvm))
		return;

	mutex_lock(&svm->sev_es.snp_vmsa_mutex);

	if (!svm->sev_es.snp_ap_waiting_for_reset)
		goto unlock;

	svm->sev_es.snp_ap_waiting_for_reset = false;

	ret = __sev_snp_update_protected_guest_state(vcpu);
	if (ret)
		vcpu_unimpl(vcpu, "snp: AP state update on init failed\n");

unlock:
	mutex_unlock(&svm->sev_es.snp_vmsa_mutex);
}

static int sev_snp_ap_creation(struct vcpu_svm *svm)
{
	struct kvm_sev_info *sev = &to_kvm_svm(svm->vcpu.kvm)->sev_info;
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct kvm_vcpu *target_vcpu;
	struct vcpu_svm *target_svm;
	unsigned int request;
	unsigned int apic_id;
	bool kick;
	int ret;

	request = lower_32_bits(svm->vmcb->control.exit_info_1);
	apic_id = upper_32_bits(svm->vmcb->control.exit_info_1);

	/* Validate the APIC ID */
	target_vcpu = kvm_get_vcpu_by_id(vcpu->kvm, apic_id);
	if (!target_vcpu) {
		vcpu_unimpl(vcpu, "vmgexit: invalid AP APIC ID [%#x] from guest\n",
			    apic_id);
		return -EINVAL;
	}

	ret = 0;

	target_svm = to_svm(target_vcpu);

	/*
	 * The target vCPU is valid, so the vCPU will be kicked unless the
	 * request is for CREATE_ON_INIT. For any errors at this stage, the
	 * kick will place the vCPU in an non-runnable state.
	 */
	kick = true;

	mutex_lock(&target_svm->sev_es.snp_vmsa_mutex);

	target_svm->sev_es.snp_vmsa_gpa = INVALID_PAGE;
	target_svm->sev_es.snp_ap_waiting_for_reset = true;

	/* Interrupt injection mode shouldn't change for AP creation */
	if (request < SVM_VMGEXIT_AP_DESTROY) {
		u64 sev_features;

		sev_features = vcpu->arch.regs[VCPU_REGS_RAX];
		sev_features ^= sev->vmsa_features;

		if (sev_features & SVM_SEV_FEAT_INT_INJ_MODES) {
			vcpu_unimpl(vcpu, "vmgexit: invalid AP injection mode [%#lx] from guest\n",
				    vcpu->arch.regs[VCPU_REGS_RAX]);
			ret = -EINVAL;
			goto out;
		}
	}

	switch (request) {
	case SVM_VMGEXIT_AP_CREATE_ON_INIT:
		kick = false;
		fallthrough;
	case SVM_VMGEXIT_AP_CREATE:
		if (!page_address_valid(vcpu, svm->vmcb->control.exit_info_2)) {
			vcpu_unimpl(vcpu, "vmgexit: invalid AP VMSA address [%#llx] from guest\n",
				    svm->vmcb->control.exit_info_2);
			ret = -EINVAL;
			goto out;
		}

		/*
		 * Malicious guest can RMPADJUST a large page into VMSA which
		 * will hit the SNP erratum where the CPU will incorrectly signal
		 * an RMP violation #PF if a hugepage collides with the RMP entry
		 * of VMSA page, reject the AP CREATE request if VMSA address from
		 * guest is 2M aligned.
		 */
		if (IS_ALIGNED(svm->vmcb->control.exit_info_2, PMD_SIZE)) {
			vcpu_unimpl(vcpu,
				    "vmgexit: AP VMSA address [%llx] from guest is unsafe as it is 2M aligned\n",
				    svm->vmcb->control.exit_info_2);
			ret = -EINVAL;
			goto out;
		}

		target_svm->sev_es.snp_vmsa_gpa = svm->vmcb->control.exit_info_2;
		break;
	case SVM_VMGEXIT_AP_DESTROY:
		break;
	default:
		vcpu_unimpl(vcpu, "vmgexit: invalid AP creation request [%#x] from guest\n",
			    request);
		ret = -EINVAL;
		break;
	}

out:
	if (kick) {
		kvm_make_request(KVM_REQ_UPDATE_PROTECTED_GUEST_STATE, target_vcpu);
		kvm_vcpu_kick(target_vcpu);
	}

	mutex_unlock(&target_svm->sev_es.snp_vmsa_mutex);

	return ret;
}

static int snp_handle_guest_req(struct vcpu_svm *svm, gpa_t req_gpa, gpa_t resp_gpa)
{
	struct sev_data_snp_guest_request data = {0};
	struct kvm *kvm = svm->vcpu.kvm;
	struct kvm_sev_info *sev = to_kvm_sev_info(kvm);
	sev_ret_code fw_err = 0;
	int ret;

	if (!sev_snp_guest(kvm))
		return -EINVAL;

	mutex_lock(&sev->guest_req_mutex);

	if (kvm_read_guest(kvm, req_gpa, sev->guest_req_buf, PAGE_SIZE)) {
		ret = -EIO;
		goto out_unlock;
	}

	data.gctx_paddr = __psp_pa(sev->snp_context);
	data.req_paddr = __psp_pa(sev->guest_req_buf);
	data.res_paddr = __psp_pa(sev->guest_resp_buf);

	/*
	 * Firmware failures are propagated on to guest, but any other failure
	 * condition along the way should be reported to userspace. E.g. if
	 * the PSP is dead and commands are timing out.
	 */
	ret = sev_issue_cmd(kvm, SEV_CMD_SNP_GUEST_REQUEST, &data, &fw_err);
	if (ret && !fw_err)
		goto out_unlock;

	if (kvm_write_guest(kvm, resp_gpa, sev->guest_resp_buf, PAGE_SIZE)) {
		ret = -EIO;
		goto out_unlock;
	}

	ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, SNP_GUEST_ERR(0, fw_err));

	ret = 1; /* resume guest */

out_unlock:
	mutex_unlock(&sev->guest_req_mutex);
	return ret;
}

static int snp_handle_ext_guest_req(struct vcpu_svm *svm, gpa_t req_gpa, gpa_t resp_gpa)
{
	struct kvm *kvm = svm->vcpu.kvm;
	u8 msg_type;

	if (!sev_snp_guest(kvm))
		return -EINVAL;

	if (kvm_read_guest(kvm, req_gpa + offsetof(struct snp_guest_msg_hdr, msg_type),
			   &msg_type, 1))
		return -EIO;

	/*
	 * As per GHCB spec, requests of type MSG_REPORT_REQ also allow for
	 * additional certificate data to be provided alongside the attestation
	 * report via the guest-provided data pages indicated by RAX/RBX. The
	 * certificate data is optional and requires additional KVM enablement
	 * to provide an interface for userspace to provide it, but KVM still
	 * needs to be able to handle extended guest requests either way. So
	 * provide a stub implementation that will always return an empty
	 * certificate table in the guest-provided data pages.
	 */
	if (msg_type == SNP_MSG_REPORT_REQ) {
		struct kvm_vcpu *vcpu = &svm->vcpu;
		u64 data_npages;
		gpa_t data_gpa;

		if (!kvm_ghcb_rax_is_valid(svm) || !kvm_ghcb_rbx_is_valid(svm))
			goto request_invalid;

		data_gpa = vcpu->arch.regs[VCPU_REGS_RAX];
		data_npages = vcpu->arch.regs[VCPU_REGS_RBX];

		if (!PAGE_ALIGNED(data_gpa))
			goto request_invalid;

		/*
		 * As per GHCB spec (see "SNP Extended Guest Request"), the
		 * certificate table is terminated by 24-bytes of zeroes.
		 */
		if (data_npages && kvm_clear_guest(kvm, data_gpa, 24))
			return -EIO;
	}

	return snp_handle_guest_req(svm, req_gpa, resp_gpa);

request_invalid:
	ghcb_set_sw_exit_info_1(svm->sev_es.ghcb, 2);
	ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, GHCB_ERR_INVALID_INPUT);
	return 1; /* resume guest */
}

static int sev_handle_vmgexit_msr_protocol(struct vcpu_svm *svm)
{
	struct vmcb_control_area *control = &svm->vmcb->control;
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct kvm_sev_info *sev = &to_kvm_svm(vcpu->kvm)->sev_info;
	u64 ghcb_info;
	int ret = 1;

	ghcb_info = control->ghcb_gpa & GHCB_MSR_INFO_MASK;

	trace_kvm_vmgexit_msr_protocol_enter(svm->vcpu.vcpu_id,
					     control->ghcb_gpa);

	switch (ghcb_info) {
	case GHCB_MSR_SEV_INFO_REQ:
		set_ghcb_msr(svm, GHCB_MSR_SEV_INFO((__u64)sev->ghcb_version,
						    GHCB_VERSION_MIN,
						    sev_enc_bit));
		break;
	case GHCB_MSR_CPUID_REQ: {
		u64 cpuid_fn, cpuid_reg, cpuid_value;

		cpuid_fn = get_ghcb_msr_bits(svm,
					     GHCB_MSR_CPUID_FUNC_MASK,
					     GHCB_MSR_CPUID_FUNC_POS);

		/* Initialize the registers needed by the CPUID intercept */
		vcpu->arch.regs[VCPU_REGS_RAX] = cpuid_fn;
		vcpu->arch.regs[VCPU_REGS_RCX] = 0;

		ret = svm_invoke_exit_handler(vcpu, SVM_EXIT_CPUID);
		if (!ret) {
			/* Error, keep GHCB MSR value as-is */
			break;
		}

		cpuid_reg = get_ghcb_msr_bits(svm,
					      GHCB_MSR_CPUID_REG_MASK,
					      GHCB_MSR_CPUID_REG_POS);
		if (cpuid_reg == 0)
			cpuid_value = vcpu->arch.regs[VCPU_REGS_RAX];
		else if (cpuid_reg == 1)
			cpuid_value = vcpu->arch.regs[VCPU_REGS_RBX];
		else if (cpuid_reg == 2)
			cpuid_value = vcpu->arch.regs[VCPU_REGS_RCX];
		else
			cpuid_value = vcpu->arch.regs[VCPU_REGS_RDX];

		set_ghcb_msr_bits(svm, cpuid_value,
				  GHCB_MSR_CPUID_VALUE_MASK,
				  GHCB_MSR_CPUID_VALUE_POS);

		set_ghcb_msr_bits(svm, GHCB_MSR_CPUID_RESP,
				  GHCB_MSR_INFO_MASK,
				  GHCB_MSR_INFO_POS);
		break;
	}
	case GHCB_MSR_AP_RESET_HOLD_REQ:
		svm->sev_es.ap_reset_hold_type = AP_RESET_HOLD_MSR_PROTO;
		ret = kvm_emulate_ap_reset_hold(&svm->vcpu);

		/*
		 * Preset the result to a non-SIPI return and then only set
		 * the result to non-zero when delivering a SIPI.
		 */
		set_ghcb_msr_bits(svm, 0,
				  GHCB_MSR_AP_RESET_HOLD_RESULT_MASK,
				  GHCB_MSR_AP_RESET_HOLD_RESULT_POS);

		set_ghcb_msr_bits(svm, GHCB_MSR_AP_RESET_HOLD_RESP,
				  GHCB_MSR_INFO_MASK,
				  GHCB_MSR_INFO_POS);
		break;
	case GHCB_MSR_HV_FT_REQ:
		set_ghcb_msr_bits(svm, GHCB_HV_FT_SUPPORTED,
				  GHCB_MSR_HV_FT_MASK, GHCB_MSR_HV_FT_POS);
		set_ghcb_msr_bits(svm, GHCB_MSR_HV_FT_RESP,
				  GHCB_MSR_INFO_MASK, GHCB_MSR_INFO_POS);
		break;
	case GHCB_MSR_PREF_GPA_REQ:
		if (!sev_snp_guest(vcpu->kvm))
			goto out_terminate;

		set_ghcb_msr_bits(svm, GHCB_MSR_PREF_GPA_NONE, GHCB_MSR_GPA_VALUE_MASK,
				  GHCB_MSR_GPA_VALUE_POS);
		set_ghcb_msr_bits(svm, GHCB_MSR_PREF_GPA_RESP, GHCB_MSR_INFO_MASK,
				  GHCB_MSR_INFO_POS);
		break;
	case GHCB_MSR_REG_GPA_REQ: {
		u64 gfn;

		if (!sev_snp_guest(vcpu->kvm))
			goto out_terminate;

		gfn = get_ghcb_msr_bits(svm, GHCB_MSR_GPA_VALUE_MASK,
					GHCB_MSR_GPA_VALUE_POS);

		svm->sev_es.ghcb_registered_gpa = gfn_to_gpa(gfn);

		set_ghcb_msr_bits(svm, gfn, GHCB_MSR_GPA_VALUE_MASK,
				  GHCB_MSR_GPA_VALUE_POS);
		set_ghcb_msr_bits(svm, GHCB_MSR_REG_GPA_RESP, GHCB_MSR_INFO_MASK,
				  GHCB_MSR_INFO_POS);
		break;
	}
	case GHCB_MSR_PSC_REQ:
		if (!sev_snp_guest(vcpu->kvm))
			goto out_terminate;

		ret = snp_begin_psc_msr(svm, control->ghcb_gpa);
		break;
	case GHCB_MSR_TERM_REQ: {
		u64 reason_set, reason_code;

		reason_set = get_ghcb_msr_bits(svm,
					       GHCB_MSR_TERM_REASON_SET_MASK,
					       GHCB_MSR_TERM_REASON_SET_POS);
		reason_code = get_ghcb_msr_bits(svm,
						GHCB_MSR_TERM_REASON_MASK,
						GHCB_MSR_TERM_REASON_POS);
		pr_info("SEV-ES guest requested termination: %#llx:%#llx\n",
			reason_set, reason_code);

		goto out_terminate;
	}
	default:
		/* Error, keep GHCB MSR value as-is */
		break;
	}

	trace_kvm_vmgexit_msr_protocol_exit(svm->vcpu.vcpu_id,
					    control->ghcb_gpa, ret);

	return ret;

out_terminate:
	vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
	vcpu->run->system_event.type = KVM_SYSTEM_EVENT_SEV_TERM;
	vcpu->run->system_event.ndata = 1;
	vcpu->run->system_event.data[0] = control->ghcb_gpa;

	return 0;
}

int sev_handle_vmgexit(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_control_area *control = &svm->vmcb->control;
	u64 ghcb_gpa, exit_code;
	int ret;

	/* Validate the GHCB */
	ghcb_gpa = control->ghcb_gpa;
	if (ghcb_gpa & GHCB_MSR_INFO_MASK)
		return sev_handle_vmgexit_msr_protocol(svm);

	if (!ghcb_gpa) {
		vcpu_unimpl(vcpu, "vmgexit: GHCB gpa is not set\n");

		/* Without a GHCB, just return right back to the guest */
		return 1;
	}

	if (kvm_vcpu_map(vcpu, ghcb_gpa >> PAGE_SHIFT, &svm->sev_es.ghcb_map)) {
		/* Unable to map GHCB from guest */
		vcpu_unimpl(vcpu, "vmgexit: error mapping GHCB [%#llx] from guest\n",
			    ghcb_gpa);

		/* Without a GHCB, just return right back to the guest */
		return 1;
	}

	svm->sev_es.ghcb = svm->sev_es.ghcb_map.hva;

	trace_kvm_vmgexit_enter(vcpu->vcpu_id, svm->sev_es.ghcb);

	sev_es_sync_from_ghcb(svm);

	/* SEV-SNP guest requires that the GHCB GPA must be registered */
	if (sev_snp_guest(svm->vcpu.kvm) && !ghcb_gpa_is_registered(svm, ghcb_gpa)) {
		vcpu_unimpl(&svm->vcpu, "vmgexit: GHCB GPA [%#llx] is not registered.\n", ghcb_gpa);
		return -EINVAL;
	}

	ret = sev_es_validate_vmgexit(svm);
	if (ret)
		return ret;

	ghcb_set_sw_exit_info_1(svm->sev_es.ghcb, 0);
	ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, 0);

	exit_code = kvm_ghcb_get_sw_exit_code(control);
	switch (exit_code) {
	case SVM_VMGEXIT_MMIO_READ:
		ret = setup_vmgexit_scratch(svm, true, control->exit_info_2);
		if (ret)
			break;

		ret = kvm_sev_es_mmio_read(vcpu,
					   control->exit_info_1,
					   control->exit_info_2,
					   svm->sev_es.ghcb_sa);
		break;
	case SVM_VMGEXIT_MMIO_WRITE:
		ret = setup_vmgexit_scratch(svm, false, control->exit_info_2);
		if (ret)
			break;

		ret = kvm_sev_es_mmio_write(vcpu,
					    control->exit_info_1,
					    control->exit_info_2,
					    svm->sev_es.ghcb_sa);
		break;
	case SVM_VMGEXIT_NMI_COMPLETE:
		++vcpu->stat.nmi_window_exits;
		svm->nmi_masked = false;
		kvm_make_request(KVM_REQ_EVENT, vcpu);
		ret = 1;
		break;
	case SVM_VMGEXIT_AP_HLT_LOOP:
		svm->sev_es.ap_reset_hold_type = AP_RESET_HOLD_NAE_EVENT;
		ret = kvm_emulate_ap_reset_hold(vcpu);
		break;
	case SVM_VMGEXIT_AP_JUMP_TABLE: {
		struct kvm_sev_info *sev = &to_kvm_svm(vcpu->kvm)->sev_info;

		switch (control->exit_info_1) {
		case 0:
			/* Set AP jump table address */
			sev->ap_jump_table = control->exit_info_2;
			break;
		case 1:
			/* Get AP jump table address */
			ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, sev->ap_jump_table);
			break;
		default:
			pr_err("svm: vmgexit: unsupported AP jump table request - exit_info_1=%#llx\n",
			       control->exit_info_1);
			ghcb_set_sw_exit_info_1(svm->sev_es.ghcb, 2);
			ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, GHCB_ERR_INVALID_INPUT);
		}

		ret = 1;
		break;
	}
	case SVM_VMGEXIT_HV_FEATURES:
		ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, GHCB_HV_FT_SUPPORTED);

		ret = 1;
		break;
	case SVM_VMGEXIT_TERM_REQUEST:
		pr_info("SEV-ES guest requested termination: reason %#llx info %#llx\n",
			control->exit_info_1, control->exit_info_2);
		vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
		vcpu->run->system_event.type = KVM_SYSTEM_EVENT_SEV_TERM;
		vcpu->run->system_event.ndata = 1;
		vcpu->run->system_event.data[0] = control->ghcb_gpa;
		break;
	case SVM_VMGEXIT_PSC:
		ret = setup_vmgexit_scratch(svm, true, control->exit_info_2);
		if (ret)
			break;

		ret = snp_begin_psc(svm, svm->sev_es.ghcb_sa);
		break;
	case SVM_VMGEXIT_AP_CREATION:
		ret = sev_snp_ap_creation(svm);
		if (ret) {
			ghcb_set_sw_exit_info_1(svm->sev_es.ghcb, 2);
			ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, GHCB_ERR_INVALID_INPUT);
		}

		ret = 1;
		break;
	case SVM_VMGEXIT_GUEST_REQUEST:
		ret = snp_handle_guest_req(svm, control->exit_info_1, control->exit_info_2);
		break;
	case SVM_VMGEXIT_EXT_GUEST_REQUEST:
		ret = snp_handle_ext_guest_req(svm, control->exit_info_1, control->exit_info_2);
		break;
	case SVM_VMGEXIT_UNSUPPORTED_EVENT:
		vcpu_unimpl(vcpu,
			    "vmgexit: unsupported event - exit_info_1=%#llx, exit_info_2=%#llx\n",
			    control->exit_info_1, control->exit_info_2);
		ret = -EINVAL;
		break;
	default:
		ret = svm_invoke_exit_handler(vcpu, exit_code);
	}

	return ret;
}

int sev_es_string_io(struct vcpu_svm *svm, int size, unsigned int port, int in)
{
	int count;
	int bytes;
	int r;

	if (svm->vmcb->control.exit_info_2 > INT_MAX)
		return -EINVAL;

	count = svm->vmcb->control.exit_info_2;
	if (unlikely(check_mul_overflow(count, size, &bytes)))
		return -EINVAL;

	r = setup_vmgexit_scratch(svm, in, bytes);
	if (r)
		return r;

	return kvm_sev_es_string_io(&svm->vcpu, size, port, svm->sev_es.ghcb_sa,
				    count, in);
}

static void sev_es_vcpu_after_set_cpuid(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;

	if (boot_cpu_has(X86_FEATURE_V_TSC_AUX)) {
		bool v_tsc_aux = guest_cpuid_has(vcpu, X86_FEATURE_RDTSCP) ||
				 guest_cpuid_has(vcpu, X86_FEATURE_RDPID);

		set_msr_interception(vcpu, svm->msrpm, MSR_TSC_AUX, v_tsc_aux, v_tsc_aux);
	}

	/*
	 * For SEV-ES, accesses to MSR_IA32_XSS should not be intercepted if
	 * the host/guest supports its use.
	 *
	 * guest_can_use() checks a number of requirements on the host/guest to
	 * ensure that MSR_IA32_XSS is available, but it might report true even
	 * if X86_FEATURE_XSAVES isn't configured in the guest to ensure host
	 * MSR_IA32_XSS is always properly restored. For SEV-ES, it is better
	 * to further check that the guest CPUID actually supports
	 * X86_FEATURE_XSAVES so that accesses to MSR_IA32_XSS by misbehaved
	 * guests will still get intercepted and caught in the normal
	 * kvm_emulate_rdmsr()/kvm_emulated_wrmsr() paths.
	 */
	if (guest_can_use(vcpu, X86_FEATURE_XSAVES) &&
	    guest_cpuid_has(vcpu, X86_FEATURE_XSAVES))
		set_msr_interception(vcpu, svm->msrpm, MSR_IA32_XSS, 1, 1);
	else
		set_msr_interception(vcpu, svm->msrpm, MSR_IA32_XSS, 0, 0);
}

void sev_vcpu_after_set_cpuid(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct kvm_cpuid_entry2 *best;

	/* For sev guests, the memory encryption bit is not reserved in CR3.  */
	best = kvm_find_cpuid_entry(vcpu, 0x8000001F);
	if (best)
		vcpu->arch.reserved_gpa_bits &= ~(1UL << (best->ebx & 0x3f));

	if (sev_es_guest(svm->vcpu.kvm))
		sev_es_vcpu_after_set_cpuid(svm);
}

static void sev_es_init_vmcb(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = svm->vmcb01.ptr;
	struct kvm_vcpu *vcpu = &svm->vcpu;

	svm->vmcb->control.nested_ctl |= SVM_NESTED_CTL_SEV_ES_ENABLE;

	/*
	 * An SEV-ES guest requires a VMSA area that is a separate from the
	 * VMCB page. Do not include the encryption mask on the VMSA physical
	 * address since hardware will access it using the guest key.  Note,
	 * the VMSA will be NULL if this vCPU is the destination for intrahost
	 * migration, and will be copied later.
	 */
	if (svm->sev_es.vmsa && !svm->sev_es.snp_has_guest_vmsa)
		svm->vmcb->control.vmsa_pa = __pa(svm->sev_es.vmsa);

	/* Can't intercept CR register access, HV can't modify CR registers */
	svm_clr_intercept(svm, INTERCEPT_CR0_READ);
	svm_clr_intercept(svm, INTERCEPT_CR4_READ);
	svm_clr_intercept(svm, INTERCEPT_CR8_READ);
	svm_clr_intercept(svm, INTERCEPT_CR0_WRITE);
	svm_clr_intercept(svm, INTERCEPT_CR4_WRITE);
	svm_clr_intercept(svm, INTERCEPT_CR8_WRITE);

	svm_clr_intercept(svm, INTERCEPT_SELECTIVE_CR0);

	/* Track EFER/CR register changes */
	svm_set_intercept(svm, TRAP_EFER_WRITE);
	svm_set_intercept(svm, TRAP_CR0_WRITE);
	svm_set_intercept(svm, TRAP_CR4_WRITE);
	svm_set_intercept(svm, TRAP_CR8_WRITE);

	vmcb->control.intercepts[INTERCEPT_DR] = 0;
	if (!sev_vcpu_has_debug_swap(svm)) {
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR7_READ);
		vmcb_set_intercept(&vmcb->control, INTERCEPT_DR7_WRITE);
		recalc_intercepts(svm);
	} else {
		/*
		 * Disable #DB intercept iff DebugSwap is enabled.  KVM doesn't
		 * allow debugging SEV-ES guests, and enables DebugSwap iff
		 * NO_NESTED_DATA_BP is supported, so there's no reason to
		 * intercept #DB when DebugSwap is enabled.  For simplicity
		 * with respect to guest debug, intercept #DB for other VMs
		 * even if NO_NESTED_DATA_BP is supported, i.e. even if the
		 * guest can't DoS the CPU with infinite #DB vectoring.
		 */
		clr_exception_intercept(svm, DB_VECTOR);
	}

	/* Can't intercept XSETBV, HV can't modify XCR0 directly */
	svm_clr_intercept(svm, INTERCEPT_XSETBV);

	/* Clear intercepts on selected MSRs */
	set_msr_interception(vcpu, svm->msrpm, MSR_EFER, 1, 1);
	set_msr_interception(vcpu, svm->msrpm, MSR_IA32_CR_PAT, 1, 1);
}

void sev_init_vmcb(struct vcpu_svm *svm)
{
	svm->vmcb->control.nested_ctl |= SVM_NESTED_CTL_SEV_ENABLE;
	clr_exception_intercept(svm, UD_VECTOR);

	/*
	 * Don't intercept #GP for SEV guests, e.g. for the VMware backdoor, as
	 * KVM can't decrypt guest memory to decode the faulting instruction.
	 */
	clr_exception_intercept(svm, GP_VECTOR);

	if (sev_es_guest(svm->vcpu.kvm))
		sev_es_init_vmcb(svm);
}

void sev_es_vcpu_reset(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	struct kvm_sev_info *sev = &to_kvm_svm(vcpu->kvm)->sev_info;

	/*
	 * Set the GHCB MSR value as per the GHCB specification when emulating
	 * vCPU RESET for an SEV-ES guest.
	 */
	set_ghcb_msr(svm, GHCB_MSR_SEV_INFO((__u64)sev->ghcb_version,
					    GHCB_VERSION_MIN,
					    sev_enc_bit));

	mutex_init(&svm->sev_es.snp_vmsa_mutex);
}

void sev_es_prepare_switch_to_guest(struct vcpu_svm *svm, struct sev_es_save_area *hostsa)
{
	/*
	 * All host state for SEV-ES guests is categorized into three swap types
	 * based on how it is handled by hardware during a world switch:
	 *
	 * A: VMRUN:   Host state saved in host save area
	 *    VMEXIT:  Host state loaded from host save area
	 *
	 * B: VMRUN:   Host state _NOT_ saved in host save area
	 *    VMEXIT:  Host state loaded from host save area
	 *
	 * C: VMRUN:   Host state _NOT_ saved in host save area
	 *    VMEXIT:  Host state initialized to default(reset) values
	 *
	 * Manually save type-B state, i.e. state that is loaded by VMEXIT but
	 * isn't saved by VMRUN, that isn't already saved by VMSAVE (performed
	 * by common SVM code).
	 */
	hostsa->xcr0 = kvm_host.xcr0;
	hostsa->pkru = read_pkru();
	hostsa->xss = kvm_host.xss;

	/*
	 * If DebugSwap is enabled, debug registers are loaded but NOT saved by
	 * the CPU (Type-B). If DebugSwap is disabled/unsupported, the CPU both
	 * saves and loads debug registers (Type-A).
	 */
	if (sev_vcpu_has_debug_swap(svm)) {
		hostsa->dr0 = native_get_debugreg(0);
		hostsa->dr1 = native_get_debugreg(1);
		hostsa->dr2 = native_get_debugreg(2);
		hostsa->dr3 = native_get_debugreg(3);
		hostsa->dr0_addr_mask = amd_get_dr_addr_mask(0);
		hostsa->dr1_addr_mask = amd_get_dr_addr_mask(1);
		hostsa->dr2_addr_mask = amd_get_dr_addr_mask(2);
		hostsa->dr3_addr_mask = amd_get_dr_addr_mask(3);
	}
}

void sev_vcpu_deliver_sipi_vector(struct kvm_vcpu *vcpu, u8 vector)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	/* First SIPI: Use the values as initially set by the VMM */
	if (!svm->sev_es.received_first_sipi) {
		svm->sev_es.received_first_sipi = true;
		return;
	}

	/* Subsequent SIPI */
	switch (svm->sev_es.ap_reset_hold_type) {
	case AP_RESET_HOLD_NAE_EVENT:
		/*
		 * Return from an AP Reset Hold VMGEXIT, where the guest will
		 * set the CS and RIP. Set SW_EXIT_INFO_2 to a non-zero value.
		 */
		ghcb_set_sw_exit_info_2(svm->sev_es.ghcb, 1);
		break;
	case AP_RESET_HOLD_MSR_PROTO:
		/*
		 * Return from an AP Reset Hold VMGEXIT, where the guest will
		 * set the CS and RIP. Set GHCB data field to a non-zero value.
		 */
		set_ghcb_msr_bits(svm, 1,
				  GHCB_MSR_AP_RESET_HOLD_RESULT_MASK,
				  GHCB_MSR_AP_RESET_HOLD_RESULT_POS);

		set_ghcb_msr_bits(svm, GHCB_MSR_AP_RESET_HOLD_RESP,
				  GHCB_MSR_INFO_MASK,
				  GHCB_MSR_INFO_POS);
		break;
	default:
		break;
	}
}

struct page *snp_safe_alloc_page_node(int node, gfp_t gfp)
{
	unsigned long pfn;
	struct page *p;

	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return alloc_pages_node(node, gfp | __GFP_ZERO, 0);

	/*
	 * Allocate an SNP-safe page to workaround the SNP erratum where
	 * the CPU will incorrectly signal an RMP violation #PF if a
	 * hugepage (2MB or 1GB) collides with the RMP entry of a
	 * 2MB-aligned VMCB, VMSA, or AVIC backing page.
	 *
	 * Allocate one extra page, choose a page which is not
	 * 2MB-aligned, and free the other.
	 */
	p = alloc_pages_node(node, gfp | __GFP_ZERO, 1);
	if (!p)
		return NULL;

	split_page(p, 1);

	pfn = page_to_pfn(p);
	if (IS_ALIGNED(pfn, PTRS_PER_PMD))
		__free_page(p++);
	else
		__free_page(p + 1);

	return p;
}

void sev_handle_rmp_fault(struct kvm_vcpu *vcpu, gpa_t gpa, u64 error_code)
{
	struct kvm_memory_slot *slot;
	struct kvm *kvm = vcpu->kvm;
	int order, rmp_level, ret;
	struct page *page;
	bool assigned;
	kvm_pfn_t pfn;
	gfn_t gfn;

	gfn = gpa >> PAGE_SHIFT;

	/*
	 * The only time RMP faults occur for shared pages is when the guest is
	 * triggering an RMP fault for an implicit page-state change from
	 * shared->private. Implicit page-state changes are forwarded to
	 * userspace via KVM_EXIT_MEMORY_FAULT events, however, so RMP faults
	 * for shared pages should not end up here.
	 */
	if (!kvm_mem_is_private(kvm, gfn)) {
		pr_warn_ratelimited("SEV: Unexpected RMP fault for non-private GPA 0x%llx\n",
				    gpa);
		return;
	}

	slot = gfn_to_memslot(kvm, gfn);
	if (!kvm_slot_can_be_private(slot)) {
		pr_warn_ratelimited("SEV: Unexpected RMP fault, non-private slot for GPA 0x%llx\n",
				    gpa);
		return;
	}

	ret = kvm_gmem_get_pfn(kvm, slot, gfn, &pfn, &page, &order);
	if (ret) {
		pr_warn_ratelimited("SEV: Unexpected RMP fault, no backing page for private GPA 0x%llx\n",
				    gpa);
		return;
	}

	ret = snp_lookup_rmpentry(pfn, &assigned, &rmp_level);
	if (ret || !assigned) {
		pr_warn_ratelimited("SEV: Unexpected RMP fault, no assigned RMP entry found for GPA 0x%llx PFN 0x%llx error %d\n",
				    gpa, pfn, ret);
		goto out_no_trace;
	}

	/*
	 * There are 2 cases where a PSMASH may be needed to resolve an #NPF
	 * with PFERR_GUEST_RMP_BIT set:
	 *
	 * 1) RMPADJUST/PVALIDATE can trigger an #NPF with PFERR_GUEST_SIZEM
	 *    bit set if the guest issues them with a smaller granularity than
	 *    what is indicated by the page-size bit in the 2MB RMP entry for
	 *    the PFN that backs the GPA.
	 *
	 * 2) Guest access via NPT can trigger an #NPF if the NPT mapping is
	 *    smaller than what is indicated by the 2MB RMP entry for the PFN
	 *    that backs the GPA.
	 *
	 * In both these cases, the corresponding 2M RMP entry needs to
	 * be PSMASH'd to 512 4K RMP entries.  If the RMP entry is already
	 * split into 4K RMP entries, then this is likely a spurious case which
	 * can occur when there are concurrent accesses by the guest to a 2MB
	 * GPA range that is backed by a 2MB-aligned PFN who's RMP entry is in
	 * the process of being PMASH'd into 4K entries. These cases should
	 * resolve automatically on subsequent accesses, so just ignore them
	 * here.
	 */
	if (rmp_level == PG_LEVEL_4K)
		goto out;

	ret = snp_rmptable_psmash(pfn);
	if (ret) {
		/*
		 * Look it up again. If it's 4K now then the PSMASH may have
		 * raced with another process and the issue has already resolved
		 * itself.
		 */
		if (!snp_lookup_rmpentry(pfn, &assigned, &rmp_level) &&
		    assigned && rmp_level == PG_LEVEL_4K)
			goto out;

		pr_warn_ratelimited("SEV: Unable to split RMP entry for GPA 0x%llx PFN 0x%llx ret %d\n",
				    gpa, pfn, ret);
	}

	kvm_zap_gfn_range(kvm, gfn, gfn + PTRS_PER_PMD);
out:
	trace_kvm_rmp_fault(vcpu, gpa, pfn, error_code, rmp_level, ret);
out_no_trace:
	kvm_release_page_unused(page);
}

static bool is_pfn_range_shared(kvm_pfn_t start, kvm_pfn_t end)
{
	kvm_pfn_t pfn = start;

	while (pfn < end) {
		int ret, rmp_level;
		bool assigned;

		ret = snp_lookup_rmpentry(pfn, &assigned, &rmp_level);
		if (ret) {
			pr_warn_ratelimited("SEV: Failed to retrieve RMP entry: PFN 0x%llx GFN start 0x%llx GFN end 0x%llx RMP level %d error %d\n",
					    pfn, start, end, rmp_level, ret);
			return false;
		}

		if (assigned) {
			pr_debug("%s: overlap detected, PFN 0x%llx start 0x%llx end 0x%llx RMP level %d\n",
				 __func__, pfn, start, end, rmp_level);
			return false;
		}

		pfn++;
	}

	return true;
}

static u8 max_level_for_order(int order)
{
	if (order >= KVM_HPAGE_GFN_SHIFT(PG_LEVEL_2M))
		return PG_LEVEL_2M;

	return PG_LEVEL_4K;
}

static bool is_large_rmp_possible(struct kvm *kvm, kvm_pfn_t pfn, int order)
{
	kvm_pfn_t pfn_aligned = ALIGN_DOWN(pfn, PTRS_PER_PMD);

	/*
	 * If this is a large folio, and the entire 2M range containing the
	 * PFN is currently shared, then the entire 2M-aligned range can be
	 * set to private via a single 2M RMP entry.
	 */
	if (max_level_for_order(order) > PG_LEVEL_4K &&
	    is_pfn_range_shared(pfn_aligned, pfn_aligned + PTRS_PER_PMD))
		return true;

	return false;
}

int sev_gmem_prepare(struct kvm *kvm, kvm_pfn_t pfn, gfn_t gfn, int max_order)
{
	struct kvm_sev_info *sev = &to_kvm_svm(kvm)->sev_info;
	kvm_pfn_t pfn_aligned;
	gfn_t gfn_aligned;
	int level, rc;
	bool assigned;

	if (!sev_snp_guest(kvm))
		return 0;

	rc = snp_lookup_rmpentry(pfn, &assigned, &level);
	if (rc) {
		pr_err_ratelimited("SEV: Failed to look up RMP entry: GFN %llx PFN %llx error %d\n",
				   gfn, pfn, rc);
		return -ENOENT;
	}

	if (assigned) {
		pr_debug("%s: already assigned: gfn %llx pfn %llx max_order %d level %d\n",
			 __func__, gfn, pfn, max_order, level);
		return 0;
	}

	if (is_large_rmp_possible(kvm, pfn, max_order)) {
		level = PG_LEVEL_2M;
		pfn_aligned = ALIGN_DOWN(pfn, PTRS_PER_PMD);
		gfn_aligned = ALIGN_DOWN(gfn, PTRS_PER_PMD);
	} else {
		level = PG_LEVEL_4K;
		pfn_aligned = pfn;
		gfn_aligned = gfn;
	}

	rc = rmp_make_private(pfn_aligned, gfn_to_gpa(gfn_aligned), level, sev->asid, false);
	if (rc) {
		pr_err_ratelimited("SEV: Failed to update RMP entry: GFN %llx PFN %llx level %d error %d\n",
				   gfn, pfn, level, rc);
		return -EINVAL;
	}

	pr_debug("%s: updated: gfn %llx pfn %llx pfn_aligned %llx max_order %d level %d\n",
		 __func__, gfn, pfn, pfn_aligned, max_order, level);

	return 0;
}

void sev_gmem_invalidate(kvm_pfn_t start, kvm_pfn_t end)
{
	kvm_pfn_t pfn;

	if (!cc_platform_has(CC_ATTR_HOST_SEV_SNP))
		return;

	pr_debug("%s: PFN start 0x%llx PFN end 0x%llx\n", __func__, start, end);

	for (pfn = start; pfn < end;) {
		bool use_2m_update = false;
		int rc, rmp_level;
		bool assigned;

		rc = snp_lookup_rmpentry(pfn, &assigned, &rmp_level);
		if (rc || !assigned)
			goto next_pfn;

		use_2m_update = IS_ALIGNED(pfn, PTRS_PER_PMD) &&
				end >= (pfn + PTRS_PER_PMD) &&
				rmp_level > PG_LEVEL_4K;

		/*
		 * If an unaligned PFN corresponds to a 2M region assigned as a
		 * large page in the RMP table, PSMASH the region into individual
		 * 4K RMP entries before attempting to convert a 4K sub-page.
		 */
		if (!use_2m_update && rmp_level > PG_LEVEL_4K) {
			/*
			 * This shouldn't fail, but if it does, report it, but
			 * still try to update RMP entry to shared and pray this
			 * was a spurious error that can be addressed later.
			 */
			rc = snp_rmptable_psmash(pfn);
			WARN_ONCE(rc, "SEV: Failed to PSMASH RMP entry for PFN 0x%llx error %d\n",
				  pfn, rc);
		}

		rc = rmp_make_shared(pfn, use_2m_update ? PG_LEVEL_2M : PG_LEVEL_4K);
		if (WARN_ONCE(rc, "SEV: Failed to update RMP entry for PFN 0x%llx error %d\n",
			      pfn, rc))
			goto next_pfn;

		/*
		 * SEV-ES avoids host/guest cache coherency issues through
		 * WBINVD hooks issued via MMU notifiers during run-time, and
		 * KVM's VM destroy path at shutdown. Those MMU notifier events
		 * don't cover gmem since there is no requirement to map pages
		 * to a HVA in order to use them for a running guest. While the
		 * shutdown path would still likely cover things for SNP guests,
		 * userspace may also free gmem pages during run-time via
		 * hole-punching operations on the guest_memfd, so flush the
		 * cache entries for these pages before free'ing them back to
		 * the host.
		 */
		clflush_cache_range(__va(pfn_to_hpa(pfn)),
				    use_2m_update ? PMD_SIZE : PAGE_SIZE);
next_pfn:
		pfn += use_2m_update ? PTRS_PER_PMD : 1;
		cond_resched();
	}
}

int sev_private_max_mapping_level(struct kvm *kvm, kvm_pfn_t pfn)
{
	int level, rc;
	bool assigned;

	if (!sev_snp_guest(kvm))
		return 0;

	rc = snp_lookup_rmpentry(pfn, &assigned, &level);
	if (rc || !assigned)
		return PG_LEVEL_4K;

	return level;
}
