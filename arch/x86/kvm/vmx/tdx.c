// SPDX-License-Identifier: GPL-2.0
#include <linux/cpu.h>
#include <asm/cpufeature.h>
#include <asm/tdx.h>
#include "capabilities.h"
#include "x86_ops.h"
#include "tdx.h"

#pragma GCC poison to_vmx

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define pr_tdx_error(__fn, __err)	\
	pr_err_ratelimited("SEAMCALL %s failed: 0x%llx\n", #__fn, __err)

#define __pr_tdx_error_N(__fn_str, __err, __fmt, ...)		\
	pr_err_ratelimited("SEAMCALL " __fn_str " failed: 0x%llx, " __fmt,  __err,  __VA_ARGS__)

#define pr_tdx_error_1(__fn, __err, __rcx)		\
	__pr_tdx_error_N(#__fn, __err, "rcx 0x%llx\n", __rcx)

#define pr_tdx_error_2(__fn, __err, __rcx, __rdx)	\
	__pr_tdx_error_N(#__fn, __err, "rcx 0x%llx, rdx 0x%llx\n", __rcx, __rdx)

#define pr_tdx_error_3(__fn, __err, __rcx, __rdx, __r8)	\
	__pr_tdx_error_N(#__fn, __err, "rcx 0x%llx, rdx 0x%llx, r8 0x%llx\n", __rcx, __rdx, __r8)

bool enable_tdx __ro_after_init;
module_param_named(tdx, enable_tdx, bool, 0444);

static enum cpuhp_state tdx_cpuhp_state;

static const struct tdx_sys_info *tdx_sysinfo;

#define KVM_SUPPORTED_TD_ATTRS (TDX_TD_ATTR_SEPT_VE_DISABLE)

static __always_inline struct kvm_tdx *to_kvm_tdx(struct kvm *kvm)
{
	return container_of(kvm, struct kvm_tdx, kvm);
}

static __always_inline struct vcpu_tdx *to_tdx(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct vcpu_tdx, vcpu);
}

static u64 tdx_get_supported_attrs(const struct tdx_sys_info_td_conf *td_conf)
{
	u64 val = KVM_SUPPORTED_TD_ATTRS;

	if ((val & td_conf->attributes_fixed1) != td_conf->attributes_fixed1)
		return 0;

	val &= td_conf->attributes_fixed0;

	return val;
}

static u64 tdx_get_supported_xfam(const struct tdx_sys_info_td_conf *td_conf)
{
	u64 val = kvm_caps.supported_xcr0 | kvm_caps.supported_xss;

	if ((val & td_conf->xfam_fixed1) != td_conf->xfam_fixed1)
		return 0;

	val &= td_conf->xfam_fixed0;

	return val;
}

static u32 tdx_set_guest_phys_addr_bits(const u32 eax, int addr_bits)
{
	return (eax & ~GENMASK(23, 16)) | (addr_bits & 0xff) << 16;
}

#define KVM_TDX_CPUID_NO_SUBLEAF	((__u32)-1)

static void td_init_cpuid_entry2(struct kvm_cpuid_entry2 *entry, unsigned char idx)
{
	const struct tdx_sys_info_td_conf *td_conf = &tdx_sysinfo->td_conf;

	entry->function = (u32)td_conf->cpuid_config_leaves[idx];
	entry->index = td_conf->cpuid_config_leaves[idx] >> 32;
	entry->eax = (u32)td_conf->cpuid_config_values[idx][0];
	entry->ebx = td_conf->cpuid_config_values[idx][0] >> 32;
	entry->ecx = (u32)td_conf->cpuid_config_values[idx][1];
	entry->edx = td_conf->cpuid_config_values[idx][1] >> 32;

	if (entry->index == KVM_TDX_CPUID_NO_SUBLEAF)
		entry->index = 0;

	/*
	 * The TDX module doesn't allow configuring the guest phys addr bits
	 * (EAX[23:16]).  However, KVM uses it as an interface to the userspace
	 * to configure the GPAW.  Report these bits as configurable.
	 */
	if (entry->function == 0x80000008)
		entry->eax = tdx_set_guest_phys_addr_bits(entry->eax, 0xff);
}

static int init_kvm_tdx_caps(const struct tdx_sys_info_td_conf *td_conf,
			     struct kvm_tdx_capabilities *caps)
{
	int i;

	caps->supported_attrs = tdx_get_supported_attrs(td_conf);
	if (!caps->supported_attrs)
		return -EIO;

	caps->supported_xfam = tdx_get_supported_xfam(td_conf);
	if (!caps->supported_xfam)
		return -EIO;

	caps->cpuid.nent = td_conf->num_cpuid_config;

	for (i = 0; i < td_conf->num_cpuid_config; i++)
		td_init_cpuid_entry2(&caps->cpuid.entries[i], i);

	return 0;
}

/*
 * Some SEAMCALLs acquire the TDX module globally, and can fail with
 * TDX_OPERAND_BUSY.  Use a global mutex to serialize these SEAMCALLs.
 */
static DEFINE_MUTEX(tdx_lock);

static inline void tdx_hkid_free(struct kvm_tdx *kvm_tdx)
{
	tdx_guest_keyid_free(kvm_tdx->hkid);
	kvm_tdx->hkid = -1;
}

static inline bool is_hkid_assigned(struct kvm_tdx *kvm_tdx)
{
	return kvm_tdx->hkid > 0;
}

static void tdx_clear_page(struct page *page)
{
	const void *zero_page = (const void *) page_to_virt(ZERO_PAGE(0));
	void *dest = page_to_virt(page);
	unsigned long i;

	/*
	 * The page could have been poisoned.  MOVDIR64B also clears
	 * the poison bit so the kernel can safely use the page again.
	 */
	for (i = 0; i < PAGE_SIZE; i += 64)
		movdir64b(dest + i, zero_page);
	/*
	 * MOVDIR64B store uses WC buffer.  Prevent following memory reads
	 * from seeing potentially poisoned cache.
	 */
	__mb();
}

/* TDH.PHYMEM.PAGE.RECLAIM is allowed only when destroying the TD. */
static int __tdx_reclaim_page(struct page *page)
{
	u64 err, rcx, rdx, r8;

	err = tdh_phymem_page_reclaim(page, &rcx, &rdx, &r8);

	/*
	 * No need to check for TDX_OPERAND_BUSY; all TD pages are freed
	 * before the HKID is released and control pages have also been
	 * released at this point, so there is no possibility of contention.
	 */
	if (WARN_ON_ONCE(err)) {
		pr_tdx_error_3(TDH_PHYMEM_PAGE_RECLAIM, err, rcx, rdx, r8);
		return -EIO;
	}
	return 0;
}

static int tdx_reclaim_page(struct page *page)
{
	int r;

	r = __tdx_reclaim_page(page);
	if (!r)
		tdx_clear_page(page);
	return r;
}


/*
 * Reclaim the TD control page(s) which are crypto-protected by TDX guest's
 * private KeyID.  Assume the cache associated with the TDX private KeyID has
 * been flushed.
 */
static void tdx_reclaim_control_page(struct page *ctrl_page)
{
	/*
	 * Leak the page if the kernel failed to reclaim the page.
	 * The kernel cannot use it safely anymore.
	 */
	if (tdx_reclaim_page(ctrl_page))
		return;

	__free_page(ctrl_page);
}

#define TDX_SEAMCALL_RETRIES 10000

static void smp_func_do_phymem_cache_wb(void *unused)
{
	u64 err = 0;
	bool resume;
	int i;

	/*
	 * TDH.PHYMEM.CACHE.WB flushes caches associated with any TDX private
	 * KeyID on the package or core.  The TDX module may not finish the
	 * cache flush but return TDX_INTERRUPTED_RESUMEABLE instead.  The
	 * kernel should retry it until it returns success w/o rescheduling.
	 */
	for (i = TDX_SEAMCALL_RETRIES; i > 0; i--) {
		resume = !!err;
		err = tdh_phymem_cache_wb(resume);
		switch (err) {
		case TDX_INTERRUPTED_RESUMABLE:
			continue;
		case TDX_NO_HKID_READY_TO_WBCACHE:
			err = TDX_SUCCESS; /* Already done by other thread */
			fallthrough;
		default:
			goto out;
		}
	}

out:
	if (WARN_ON_ONCE(err))
		pr_tdx_error(TDH_PHYMEM_CACHE_WB, err);
}

void tdx_mmu_release_hkid(struct kvm *kvm)
{
	bool packages_allocated, targets_allocated;
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	cpumask_var_t packages, targets;
	u64 err;
	int i;

	if (!is_hkid_assigned(kvm_tdx))
		return;

	/* KeyID has been allocated but guest is not yet configured */
	if (!kvm_tdx->td.tdr_page) {
		tdx_hkid_free(kvm_tdx);
		return;
	}

	packages_allocated = zalloc_cpumask_var(&packages, GFP_KERNEL);
	targets_allocated = zalloc_cpumask_var(&targets, GFP_KERNEL);
	cpus_read_lock();

	/*
	 * TDH.PHYMEM.CACHE.WB tries to acquire the TDX module global lock
	 * and can fail with TDX_OPERAND_BUSY when it fails to get the lock.
	 * Multiple TDX guests can be destroyed simultaneously. Take the
	 * mutex to prevent it from getting error.
	 */
	mutex_lock(&tdx_lock);

	/*
	 * Releasing HKID is in vm_destroy().
	 * After the above flushing vps, there should be no more vCPU
	 * associations, as all vCPU fds have been released at this stage.
	 */
	for_each_online_cpu(i) {
		if (packages_allocated &&
		    cpumask_test_and_set_cpu(topology_physical_package_id(i),
					     packages))
			continue;
		if (targets_allocated)
			cpumask_set_cpu(i, targets);
	}
	if (targets_allocated)
		on_each_cpu_mask(targets, smp_func_do_phymem_cache_wb, NULL, true);
	else
		on_each_cpu(smp_func_do_phymem_cache_wb, NULL, true);
	/*
	 * In the case of error in smp_func_do_phymem_cache_wb(), the following
	 * tdh_mng_key_freeid() will fail.
	 */
	err = tdh_mng_key_freeid(&kvm_tdx->td);
	if (KVM_BUG_ON(err, kvm)) {
		pr_tdx_error(TDH_MNG_KEY_FREEID, err);
		pr_err("tdh_mng_key_freeid() failed. HKID %d is leaked.\n",
		       kvm_tdx->hkid);
	} else {
		tdx_hkid_free(kvm_tdx);
	}

	mutex_unlock(&tdx_lock);
	cpus_read_unlock();
	free_cpumask_var(targets);
	free_cpumask_var(packages);
}

static void tdx_reclaim_td_control_pages(struct kvm *kvm)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	u64 err;
	int i;

	/*
	 * tdx_mmu_release_hkid() failed to reclaim HKID.  Something went wrong
	 * heavily with TDX module.  Give up freeing TD pages.  As the function
	 * already warned, don't warn it again.
	 */
	if (is_hkid_assigned(kvm_tdx))
		return;

	if (kvm_tdx->td.tdcs_pages) {
		for (i = 0; i < kvm_tdx->td.tdcs_nr_pages; i++) {
			if (!kvm_tdx->td.tdcs_pages[i])
				continue;

			tdx_reclaim_control_page(kvm_tdx->td.tdcs_pages[i]);
		}
		kfree(kvm_tdx->td.tdcs_pages);
		kvm_tdx->td.tdcs_pages = NULL;
	}

	if (!kvm_tdx->td.tdr_page)
		return;

	if (__tdx_reclaim_page(kvm_tdx->td.tdr_page))
		return;

	/*
	 * Use a SEAMCALL to ask the TDX module to flush the cache based on the
	 * KeyID. TDX module may access TDR while operating on TD (Especially
	 * when it is reclaiming TDCS).
	 */
	err = tdh_phymem_page_wbinvd_tdr(&kvm_tdx->td);
	if (KVM_BUG_ON(err, kvm)) {
		pr_tdx_error(TDH_PHYMEM_PAGE_WBINVD, err);
		return;
	}
	tdx_clear_page(kvm_tdx->td.tdr_page);

	__free_page(kvm_tdx->td.tdr_page);
	kvm_tdx->td.tdr_page = NULL;
}

void tdx_vm_destroy(struct kvm *kvm)
{
	tdx_reclaim_td_control_pages(kvm);
}

static int tdx_do_tdh_mng_key_config(void *param)
{
	struct kvm_tdx *kvm_tdx = param;
	u64 err;

	/* TDX_RND_NO_ENTROPY related retries are handled by sc_retry() */
	err = tdh_mng_key_config(&kvm_tdx->td);

	if (KVM_BUG_ON(err, &kvm_tdx->kvm)) {
		pr_tdx_error(TDH_MNG_KEY_CONFIG, err);
		return -EIO;
	}

	return 0;
}

static int __tdx_td_init(struct kvm *kvm);

int tdx_vm_init(struct kvm *kvm)
{
	kvm->arch.has_protected_state = true;
	kvm->arch.has_private_mem = true;

	/* Place holder for TDX specific logic. */
	return __tdx_td_init(kvm);
}

static int tdx_get_capabilities(struct kvm_tdx_cmd *cmd)
{
	const struct tdx_sys_info_td_conf *td_conf = &tdx_sysinfo->td_conf;
	struct kvm_tdx_capabilities __user *user_caps;
	struct kvm_tdx_capabilities *caps = NULL;
	int ret = 0;

	/* flags is reserved for future use */
	if (cmd->flags)
		return -EINVAL;

	caps = kmalloc(sizeof(*caps) +
		       sizeof(struct kvm_cpuid_entry2) * td_conf->num_cpuid_config,
		       GFP_KERNEL);
	if (!caps)
		return -ENOMEM;

	user_caps = u64_to_user_ptr(cmd->data);
	if (copy_from_user(caps, user_caps, sizeof(*caps))) {
		ret = -EFAULT;
		goto out;
	}

	if (caps->cpuid.nent < td_conf->num_cpuid_config) {
		ret = -E2BIG;
		goto out;
	}

	ret = init_kvm_tdx_caps(td_conf, caps);
	if (ret)
		goto out;

	if (copy_to_user(user_caps, caps, sizeof(*caps))) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_to_user(user_caps->cpuid.entries, caps->cpuid.entries,
			 caps->cpuid.nent *
			 sizeof(caps->cpuid.entries[0])))
		ret = -EFAULT;

out:
	/* kfree() accepts NULL. */
	kfree(caps);
	return ret;
}

static int __tdx_td_init(struct kvm *kvm)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	cpumask_var_t packages;
	struct page **tdcs_pages = NULL;
	struct page *tdr_page;
	int ret, i;
	u64 err;

	ret = tdx_guest_keyid_alloc();
	if (ret < 0)
		return ret;
	kvm_tdx->hkid = ret;

	ret = -ENOMEM;

	tdr_page = alloc_page(GFP_KERNEL);
	if (!tdr_page)
		goto free_hkid;

	kvm_tdx->td.tdcs_nr_pages = tdx_sysinfo->td_ctrl.tdcs_base_size / PAGE_SIZE;
	tdcs_pages = kcalloc(kvm_tdx->td.tdcs_nr_pages, sizeof(*kvm_tdx->td.tdcs_pages),
			     GFP_KERNEL | __GFP_ZERO);
	if (!tdcs_pages)
		goto free_tdr;

	for (i = 0; i < kvm_tdx->td.tdcs_nr_pages; i++) {
		tdcs_pages[i] = alloc_page(GFP_KERNEL);
		if (!tdcs_pages[i])
			goto free_tdcs;
	}

	if (!zalloc_cpumask_var(&packages, GFP_KERNEL))
		goto free_tdcs;

	cpus_read_lock();

	/*
	 * Need at least one CPU of the package to be online in order to
	 * program all packages for host key id.  Check it.
	 */
	for_each_present_cpu(i)
		cpumask_set_cpu(topology_physical_package_id(i), packages);
	for_each_online_cpu(i)
		cpumask_clear_cpu(topology_physical_package_id(i), packages);
	if (!cpumask_empty(packages)) {
		ret = -EIO;
		/*
		 * Because it's hard for human operator to figure out the
		 * reason, warn it.
		 */
#define MSG_ALLPKG	"All packages need to have online CPU to create TD. Online CPU and retry.\n"
		pr_warn_ratelimited(MSG_ALLPKG);
		goto free_packages;
	}

	/*
	 * TDH.MNG.CREATE tries to grab the global TDX module and fails
	 * with TDX_OPERAND_BUSY when it fails to grab.  Take the global
	 * lock to prevent it from failure.
	 */
	mutex_lock(&tdx_lock);
	kvm_tdx->td.tdr_page = tdr_page;
	err = tdh_mng_create(&kvm_tdx->td, kvm_tdx->hkid);
	mutex_unlock(&tdx_lock);

	if (err == TDX_RND_NO_ENTROPY) {
		ret = -EAGAIN;
		goto free_packages;
	}

	if (WARN_ON_ONCE(err)) {
		pr_tdx_error(TDH_MNG_CREATE, err);
		ret = -EIO;
		goto free_packages;
	}

	for_each_online_cpu(i) {
		int pkg = topology_physical_package_id(i);

		if (cpumask_test_and_set_cpu(pkg, packages))
			continue;

		/*
		 * Program the memory controller in the package with an
		 * encryption key associated to a TDX private host key id
		 * assigned to this TDR.  Concurrent operations on same memory
		 * controller results in TDX_OPERAND_BUSY. No locking needed
		 * beyond the cpus_read_lock() above as it serializes against
		 * hotplug and the first online CPU of the package is always
		 * used. We never have two CPUs in the same socket trying to
		 * program the key.
		 */
		ret = smp_call_on_cpu(i, tdx_do_tdh_mng_key_config,
				      kvm_tdx, true);
		if (ret)
			break;
	}
	cpus_read_unlock();
	free_cpumask_var(packages);
	if (ret) {
		i = 0;
		goto teardown;
	}

	kvm_tdx->td.tdcs_pages = tdcs_pages;
	for (i = 0; i < kvm_tdx->td.tdcs_nr_pages; i++) {
		err = tdh_mng_addcx(&kvm_tdx->td, tdcs_pages[i]);
		if (err == TDX_RND_NO_ENTROPY) {
			/* Here it's hard to allow userspace to retry. */
			ret = -EAGAIN;
			goto teardown;
		}
		if (WARN_ON_ONCE(err)) {
			pr_tdx_error(TDH_MNG_ADDCX, err);
			ret = -EIO;
			goto teardown;
		}
	}

	/*
	 * Note, TDH_MNG_INIT cannot be invoked here.  TDH_MNG_INIT requires a dedicated
	 * ioctl() to define the configure CPUID values for the TD.
	 */
	return 0;

	/*
	 * The sequence for freeing resources from a partially initialized TD
	 * varies based on where in the initialization flow failure occurred.
	 * Simply use the full teardown and destroy, which naturally play nice
	 * with partial initialization.
	 */
teardown:
	/* Only free pages not yet added, so start at 'i' */
	for (; i < kvm_tdx->td.tdcs_nr_pages; i++) {
		if (tdcs_pages[i]) {
			__free_page(tdcs_pages[i]);
			tdcs_pages[i] = NULL;
		}
	}
	if (!kvm_tdx->td.tdcs_pages)
		kfree(tdcs_pages);

	tdx_mmu_release_hkid(kvm);
	tdx_reclaim_td_control_pages(kvm);

	return ret;

free_packages:
	cpus_read_unlock();
	free_cpumask_var(packages);

free_tdcs:
	for (i = 0; i < kvm_tdx->td.tdcs_nr_pages; i++) {
		if (tdcs_pages[i])
			__free_page(tdcs_pages[i]);
	}
	kfree(tdcs_pages);
	kvm_tdx->td.tdcs_pages = NULL;

free_tdr:
	if (tdr_page)
		__free_page(tdr_page);
	kvm_tdx->td.tdr_page = 0;

free_hkid:
	tdx_hkid_free(kvm_tdx);

	return ret;
}

int tdx_vm_ioctl(struct kvm *kvm, void __user *argp)
{
	struct kvm_tdx_cmd tdx_cmd;
	int r;

	if (copy_from_user(&tdx_cmd, argp, sizeof(struct kvm_tdx_cmd)))
		return -EFAULT;

	/*
	 * Userspace should never set hw_error. It is used to fill
	 * hardware-defined error by the kernel.
	 */
	if (tdx_cmd.hw_error)
		return -EINVAL;

	mutex_lock(&kvm->lock);

	switch (tdx_cmd.id) {
	case KVM_TDX_CAPABILITIES:
		r = tdx_get_capabilities(&tdx_cmd);
		break;
	default:
		r = -EINVAL;
		goto out;
	}

	if (copy_to_user(argp, &tdx_cmd, sizeof(struct kvm_tdx_cmd)))
		r = -EFAULT;

out:
	mutex_unlock(&kvm->lock);
	return r;
}

static int tdx_online_cpu(unsigned int cpu)
{
	unsigned long flags;
	int r;

	/* Sanity check CPU is already in post-VMXON */
	WARN_ON_ONCE(!(cr4_read_shadow() & X86_CR4_VMXE));

	local_irq_save(flags);
	r = tdx_cpu_enable();
	local_irq_restore(flags);

	return r;
}

static void __do_tdx_cleanup(void)
{
	/*
	 * Once TDX module is initialized, it cannot be disabled and
	 * re-initialized again w/o runtime update (which isn't
	 * supported by kernel).  Only need to remove the cpuhp here.
	 * The TDX host core code tracks TDX status and can handle
	 * 'multiple enabling' scenario.
	 */
	WARN_ON_ONCE(!tdx_cpuhp_state);
	cpuhp_remove_state_nocalls_cpuslocked(tdx_cpuhp_state);
	tdx_cpuhp_state = 0;
}

static void __tdx_cleanup(void)
{
	cpus_read_lock();
	__do_tdx_cleanup();
	cpus_read_unlock();
}

static int __init __do_tdx_bringup(void)
{
	int r;

	/*
	 * TDX-specific cpuhp callback to call tdx_cpu_enable() on all
	 * online CPUs before calling tdx_enable(), and on any new
	 * going-online CPU to make sure it is ready for TDX guest.
	 */
	r = cpuhp_setup_state_cpuslocked(CPUHP_AP_ONLINE_DYN,
					 "kvm/cpu/tdx:online",
					 tdx_online_cpu, NULL);
	if (r < 0)
		return r;

	tdx_cpuhp_state = r;

	r = tdx_enable();
	if (r)
		__do_tdx_cleanup();

	return r;
}

static int __init __tdx_bringup(void)
{
	int r;

	/*
	 * Enabling TDX requires enabling hardware virtualization first,
	 * as making SEAMCALLs requires CPU being in post-VMXON state.
	 */
	r = kvm_enable_virtualization();
	if (r)
		return r;

	cpus_read_lock();
	r = __do_tdx_bringup();
	cpus_read_unlock();

	if (r)
		goto tdx_bringup_err;

	/* Get TDX global information for later use */
	tdx_sysinfo = tdx_get_sysinfo();
	if (WARN_ON_ONCE(!tdx_sysinfo)) {
		r = -EINVAL;
		goto get_sysinfo_err;
	}

	/* Check TDX module and KVM capabilities */
	if (!tdx_get_supported_attrs(&tdx_sysinfo->td_conf) ||
	    !tdx_get_supported_xfam(&tdx_sysinfo->td_conf))
		goto get_sysinfo_err;

	if (!(tdx_sysinfo->features.tdx_features0 & MD_FIELD_ID_FEATURES0_TOPOLOGY_ENUM))
		goto get_sysinfo_err;

	/*
	 * Leave hardware virtualization enabled after TDX is enabled
	 * successfully.  TDX CPU hotplug depends on this.
	 */
	return 0;

get_sysinfo_err:
	__tdx_cleanup();
tdx_bringup_err:
	kvm_disable_virtualization();
	return r;
}

void tdx_cleanup(void)
{
	if (enable_tdx) {
		__tdx_cleanup();
		kvm_disable_virtualization();
	}
}

int __init tdx_bringup(void)
{
	int r;

	if (!enable_tdx)
		return 0;

	if (!cpu_feature_enabled(X86_FEATURE_MOVDIR64B)) {
		pr_err("tdx: MOVDIR64B is required for TDX\n");
		goto success_disable_tdx;
	}

	if (!cpu_feature_enabled(X86_FEATURE_TDX_HOST_PLATFORM)) {
		pr_err("tdx: no TDX private KeyIDs available\n");
		goto success_disable_tdx;
	}

	if (!enable_virt_at_load) {
		pr_err("tdx: tdx requires kvm.enable_virt_at_load=1\n");
		goto success_disable_tdx;
	}

	/*
	 * Ideally KVM should probe whether TDX module has been loaded
	 * first and then try to bring it up.  But TDX needs to use SEAMCALL
	 * to probe whether the module is loaded (there is no CPUID or MSR
	 * for that), and making SEAMCALL requires enabling virtualization
	 * first, just like the rest steps of bringing up TDX module.
	 *
	 * So, for simplicity do everything in __tdx_bringup(); the first
	 * SEAMCALL will return -ENODEV when the module is not loaded.  The
	 * only complication is having to make sure that initialization
	 * SEAMCALLs don't return TDX_SEAMCALL_VMFAILINVALID in other
	 * cases.
	 */
	r = __tdx_bringup();
	if (r) {
		/*
		 * Disable TDX only but don't fail to load module if
		 * the TDX module could not be loaded.  No need to print
		 * message saying "module is not loaded" because it was
		 * printed when the first SEAMCALL failed.
		 */
		if (r == -ENODEV)
			goto success_disable_tdx;

		enable_tdx = 0;
	}

	return r;

success_disable_tdx:
	enable_tdx = 0;
	return 0;
}
