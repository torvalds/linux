// SPDX-License-Identifier: GPL-2.0
#include <linux/cleanup.h>
#include <linux/cpu.h>
#include <asm/cpufeature.h>
#include <asm/fpu/xcr.h>
#include <linux/misc_cgroup.h>
#include <linux/mmu_context.h>
#include <asm/tdx.h>
#include "capabilities.h"
#include "mmu.h"
#include "x86_ops.h"
#include "lapic.h"
#include "tdx.h"
#include "vmx.h"
#include "mmu/spte.h"
#include "common.h"
#include "posted_intr.h"
#include "irq.h"
#include <trace/events/kvm.h>
#include "trace.h"

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

#define TDX_SHARED_BIT_PWL_5 gpa_to_gfn(BIT_ULL(51))
#define TDX_SHARED_BIT_PWL_4 gpa_to_gfn(BIT_ULL(47))

static enum cpuhp_state tdx_cpuhp_state;

static const struct tdx_sys_info *tdx_sysinfo;

void tdh_vp_rd_failed(struct vcpu_tdx *tdx, char *uclass, u32 field, u64 err)
{
	KVM_BUG_ON(1, tdx->vcpu.kvm);
	pr_err("TDH_VP_RD[%s.0x%x] failed 0x%llx\n", uclass, field, err);
}

void tdh_vp_wr_failed(struct vcpu_tdx *tdx, char *uclass, char *op, u32 field,
		      u64 val, u64 err)
{
	KVM_BUG_ON(1, tdx->vcpu.kvm);
	pr_err("TDH_VP_WR[%s.0x%x]%s0x%llx failed: 0x%llx\n", uclass, field, op, val, err);
}

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

static int tdx_get_guest_phys_addr_bits(const u32 eax)
{
	return (eax & GENMASK(23, 16)) >> 16;
}

static u32 tdx_set_guest_phys_addr_bits(const u32 eax, int addr_bits)
{
	return (eax & ~GENMASK(23, 16)) | (addr_bits & 0xff) << 16;
}

#define TDX_FEATURE_TSX (__feature_bit(X86_FEATURE_HLE) | __feature_bit(X86_FEATURE_RTM))

static bool has_tsx(const struct kvm_cpuid_entry2 *entry)
{
	return entry->function == 7 && entry->index == 0 &&
	       (entry->ebx & TDX_FEATURE_TSX);
}

static void clear_tsx(struct kvm_cpuid_entry2 *entry)
{
	entry->ebx &= ~TDX_FEATURE_TSX;
}

static bool has_waitpkg(const struct kvm_cpuid_entry2 *entry)
{
	return entry->function == 7 && entry->index == 0 &&
	       (entry->ecx & __feature_bit(X86_FEATURE_WAITPKG));
}

static void clear_waitpkg(struct kvm_cpuid_entry2 *entry)
{
	entry->ecx &= ~__feature_bit(X86_FEATURE_WAITPKG);
}

static void tdx_clear_unsupported_cpuid(struct kvm_cpuid_entry2 *entry)
{
	if (has_tsx(entry))
		clear_tsx(entry);

	if (has_waitpkg(entry))
		clear_waitpkg(entry);
}

static bool tdx_unsupported_cpuid(const struct kvm_cpuid_entry2 *entry)
{
	return has_tsx(entry) || has_waitpkg(entry);
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

	tdx_clear_unsupported_cpuid(entry);
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

static atomic_t nr_configured_hkid;

static bool tdx_operand_busy(u64 err)
{
	return (err & TDX_SEAMCALL_STATUS_MASK) == TDX_OPERAND_BUSY;
}


/*
 * A per-CPU list of TD vCPUs associated with a given CPU.
 * Protected by interrupt mask. Only manipulated by the CPU owning this per-CPU
 * list.
 * - When a vCPU is loaded onto a CPU, it is removed from the per-CPU list of
 *   the old CPU during the IPI callback running on the old CPU, and then added
 *   to the per-CPU list of the new CPU.
 * - When a TD is tearing down, all vCPUs are disassociated from their current
 *   running CPUs and removed from the per-CPU list during the IPI callback
 *   running on those CPUs.
 * - When a CPU is brought down, traverse the per-CPU list to disassociate all
 *   associated TD vCPUs and remove them from the per-CPU list.
 */
static DEFINE_PER_CPU(struct list_head, associated_tdvcpus);

static __always_inline unsigned long tdvmcall_exit_type(struct kvm_vcpu *vcpu)
{
	return to_tdx(vcpu)->vp_enter_args.r10;
}

static __always_inline unsigned long tdvmcall_leaf(struct kvm_vcpu *vcpu)
{
	return to_tdx(vcpu)->vp_enter_args.r11;
}

static __always_inline void tdvmcall_set_return_code(struct kvm_vcpu *vcpu,
						     long val)
{
	to_tdx(vcpu)->vp_enter_args.r10 = val;
}

static __always_inline void tdvmcall_set_return_val(struct kvm_vcpu *vcpu,
						    unsigned long val)
{
	to_tdx(vcpu)->vp_enter_args.r11 = val;
}

static inline void tdx_hkid_free(struct kvm_tdx *kvm_tdx)
{
	tdx_guest_keyid_free(kvm_tdx->hkid);
	kvm_tdx->hkid = -1;
	atomic_dec(&nr_configured_hkid);
	misc_cg_uncharge(MISC_CG_RES_TDX, kvm_tdx->misc_cg, 1);
	put_misc_cg(kvm_tdx->misc_cg);
	kvm_tdx->misc_cg = NULL;
}

static inline bool is_hkid_assigned(struct kvm_tdx *kvm_tdx)
{
	return kvm_tdx->hkid > 0;
}

static inline void tdx_disassociate_vp(struct kvm_vcpu *vcpu)
{
	lockdep_assert_irqs_disabled();

	list_del(&to_tdx(vcpu)->cpu_list);

	/*
	 * Ensure tdx->cpu_list is updated before setting vcpu->cpu to -1,
	 * otherwise, a different CPU can see vcpu->cpu = -1 and add the vCPU
	 * to its list before it's deleted from this CPU's list.
	 */
	smp_wmb();

	vcpu->cpu = -1;
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

static void tdx_no_vcpus_enter_start(struct kvm *kvm)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);

	lockdep_assert_held_write(&kvm->mmu_lock);

	WRITE_ONCE(kvm_tdx->wait_for_sept_zap, true);

	kvm_make_all_cpus_request(kvm, KVM_REQ_OUTSIDE_GUEST_MODE);
}

static void tdx_no_vcpus_enter_stop(struct kvm *kvm)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);

	lockdep_assert_held_write(&kvm->mmu_lock);

	WRITE_ONCE(kvm_tdx->wait_for_sept_zap, false);
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

struct tdx_flush_vp_arg {
	struct kvm_vcpu *vcpu;
	u64 err;
};

static void tdx_flush_vp(void *_arg)
{
	struct tdx_flush_vp_arg *arg = _arg;
	struct kvm_vcpu *vcpu = arg->vcpu;
	u64 err;

	arg->err = 0;
	lockdep_assert_irqs_disabled();

	/* Task migration can race with CPU offlining. */
	if (unlikely(vcpu->cpu != raw_smp_processor_id()))
		return;

	/*
	 * No need to do TDH_VP_FLUSH if the vCPU hasn't been initialized.  The
	 * list tracking still needs to be updated so that it's correct if/when
	 * the vCPU does get initialized.
	 */
	if (to_tdx(vcpu)->state != VCPU_TD_STATE_UNINITIALIZED) {
		/*
		 * No need to retry.  TDX Resources needed for TDH.VP.FLUSH are:
		 * TDVPR as exclusive, TDR as shared, and TDCS as shared.  This
		 * vp flush function is called when destructing vCPU/TD or vCPU
		 * migration.  No other thread uses TDVPR in those cases.
		 */
		err = tdh_vp_flush(&to_tdx(vcpu)->vp);
		if (unlikely(err && err != TDX_VCPU_NOT_ASSOCIATED)) {
			/*
			 * This function is called in IPI context. Do not use
			 * printk to avoid console semaphore.
			 * The caller prints out the error message, instead.
			 */
			if (err)
				arg->err = err;
		}
	}

	tdx_disassociate_vp(vcpu);
}

static void tdx_flush_vp_on_cpu(struct kvm_vcpu *vcpu)
{
	struct tdx_flush_vp_arg arg = {
		.vcpu = vcpu,
	};
	int cpu = vcpu->cpu;

	if (unlikely(cpu == -1))
		return;

	smp_call_function_single(cpu, tdx_flush_vp, &arg, 1);
	if (KVM_BUG_ON(arg.err, vcpu->kvm))
		pr_tdx_error(TDH_VP_FLUSH, arg.err);
}

void tdx_disable_virtualization_cpu(void)
{
	int cpu = raw_smp_processor_id();
	struct list_head *tdvcpus = &per_cpu(associated_tdvcpus, cpu);
	struct tdx_flush_vp_arg arg;
	struct vcpu_tdx *tdx, *tmp;
	unsigned long flags;

	local_irq_save(flags);
	/* Safe variant needed as tdx_disassociate_vp() deletes the entry. */
	list_for_each_entry_safe(tdx, tmp, tdvcpus, cpu_list) {
		arg.vcpu = &tdx->vcpu;
		tdx_flush_vp(&arg);
	}
	local_irq_restore(flags);
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
	struct kvm_vcpu *vcpu;
	unsigned long j;
	int i;
	u64 err;

	if (!is_hkid_assigned(kvm_tdx))
		return;

	packages_allocated = zalloc_cpumask_var(&packages, GFP_KERNEL);
	targets_allocated = zalloc_cpumask_var(&targets, GFP_KERNEL);
	cpus_read_lock();

	kvm_for_each_vcpu(j, vcpu, kvm)
		tdx_flush_vp_on_cpu(vcpu);

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
	err = tdh_mng_vpflushdone(&kvm_tdx->td);
	if (err == TDX_FLUSHVP_NOT_DONE)
		goto out;
	if (KVM_BUG_ON(err, kvm)) {
		pr_tdx_error(TDH_MNG_VPFLUSHDONE, err);
		pr_err("tdh_mng_vpflushdone() failed. HKID %d is leaked.\n",
		       kvm_tdx->hkid);
		goto out;
	}

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

out:
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
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);

	tdx_reclaim_td_control_pages(kvm);

	kvm_tdx->state = TD_STATE_UNINITIALIZED;
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

int tdx_vm_init(struct kvm *kvm)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);

	kvm->arch.has_protected_state = true;
	kvm->arch.has_private_mem = true;
	kvm->arch.disabled_quirks |= KVM_X86_QUIRK_IGNORE_GUEST_PAT;

	/*
	 * Because guest TD is protected, VMM can't parse the instruction in TD.
	 * Instead, guest uses MMIO hypercall.  For unmodified device driver,
	 * #VE needs to be injected for MMIO and #VE handler in TD converts MMIO
	 * instruction into MMIO hypercall.
	 *
	 * SPTE value for MMIO needs to be setup so that #VE is injected into
	 * TD instead of triggering EPT MISCONFIG.
	 * - RWX=0 so that EPT violation is triggered.
	 * - suppress #VE bit is cleared to inject #VE.
	 */
	kvm_mmu_set_mmio_spte_value(kvm, 0);

	/*
	 * TDX has its own limit of maximum vCPUs it can support for all
	 * TDX guests in addition to KVM_MAX_VCPUS.  TDX module reports
	 * such limit via the MAX_VCPU_PER_TD global metadata.  In
	 * practice, it reflects the number of logical CPUs that ALL
	 * platforms that the TDX module supports can possibly have.
	 *
	 * Limit TDX guest's maximum vCPUs to the number of logical CPUs
	 * the platform has.  Simply forwarding the MAX_VCPU_PER_TD to
	 * userspace would result in an unpredictable ABI.
	 */
	kvm->max_vcpus = min_t(int, kvm->max_vcpus, num_present_cpus());

	kvm_tdx->state = TD_STATE_UNINITIALIZED;

	return 0;
}

int tdx_vcpu_create(struct kvm_vcpu *vcpu)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(vcpu->kvm);
	struct vcpu_tdx *tdx = to_tdx(vcpu);

	if (kvm_tdx->state != TD_STATE_INITIALIZED)
		return -EIO;

	/*
	 * TDX module mandates APICv, which requires an in-kernel local APIC.
	 * Disallow an in-kernel I/O APIC, because level-triggered interrupts
	 * and thus the I/O APIC as a whole can't be faithfully emulated in KVM.
	 */
	if (!irqchip_split(vcpu->kvm))
		return -EINVAL;

	fpstate_set_confidential(&vcpu->arch.guest_fpu);
	vcpu->arch.apic->guest_apic_protected = true;
	INIT_LIST_HEAD(&tdx->vt.pi_wakeup_list);

	vcpu->arch.efer = EFER_SCE | EFER_LME | EFER_LMA | EFER_NX;

	vcpu->arch.switch_db_regs = KVM_DEBUGREG_AUTO_SWITCH;
	vcpu->arch.cr0_guest_owned_bits = -1ul;
	vcpu->arch.cr4_guest_owned_bits = -1ul;

	/* KVM can't change TSC offset/multiplier as TDX module manages them. */
	vcpu->arch.guest_tsc_protected = true;
	vcpu->arch.tsc_offset = kvm_tdx->tsc_offset;
	vcpu->arch.l1_tsc_offset = vcpu->arch.tsc_offset;
	vcpu->arch.tsc_scaling_ratio = kvm_tdx->tsc_multiplier;
	vcpu->arch.l1_tsc_scaling_ratio = kvm_tdx->tsc_multiplier;

	vcpu->arch.guest_state_protected =
		!(to_kvm_tdx(vcpu->kvm)->attributes & TDX_TD_ATTR_DEBUG);

	if ((kvm_tdx->xfam & XFEATURE_MASK_XTILE) == XFEATURE_MASK_XTILE)
		vcpu->arch.xfd_no_write_intercept = true;

	tdx->vt.pi_desc.nv = POSTED_INTR_VECTOR;
	__pi_set_sn(&tdx->vt.pi_desc);

	tdx->state = VCPU_TD_STATE_UNINITIALIZED;

	return 0;
}

void tdx_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);

	vmx_vcpu_pi_load(vcpu, cpu);
	if (vcpu->cpu == cpu || !is_hkid_assigned(to_kvm_tdx(vcpu->kvm)))
		return;

	tdx_flush_vp_on_cpu(vcpu);

	KVM_BUG_ON(cpu != raw_smp_processor_id(), vcpu->kvm);
	local_irq_disable();
	/*
	 * Pairs with the smp_wmb() in tdx_disassociate_vp() to ensure
	 * vcpu->cpu is read before tdx->cpu_list.
	 */
	smp_rmb();

	list_add(&tdx->cpu_list, &per_cpu(associated_tdvcpus, cpu));
	local_irq_enable();
}

bool tdx_interrupt_allowed(struct kvm_vcpu *vcpu)
{
	/*
	 * KVM can't get the interrupt status of TDX guest and it assumes
	 * interrupt is always allowed unless TDX guest calls TDVMCALL with HLT,
	 * which passes the interrupt blocked flag.
	 */
	return vmx_get_exit_reason(vcpu).basic != EXIT_REASON_HLT ||
	       !to_tdx(vcpu)->vp_enter_args.r12;
}

bool tdx_protected_apic_has_interrupt(struct kvm_vcpu *vcpu)
{
	u64 vcpu_state_details;

	if (pi_has_pending_interrupt(vcpu))
		return true;

	/*
	 * Only check RVI pending for HALTED case with IRQ enabled.
	 * For non-HLT cases, KVM doesn't care about STI/SS shadows.  And if the
	 * interrupt was pending before TD exit, then it _must_ be blocked,
	 * otherwise the interrupt would have been serviced at the instruction
	 * boundary.
	 */
	if (vmx_get_exit_reason(vcpu).basic != EXIT_REASON_HLT ||
	    to_tdx(vcpu)->vp_enter_args.r12)
		return false;

	vcpu_state_details =
		td_state_non_arch_read64(to_tdx(vcpu), TD_VCPU_STATE_DETAILS_NON_ARCH);

	return tdx_vcpu_state_details_intr_pending(vcpu_state_details);
}

/*
 * Compared to vmx_prepare_switch_to_guest(), there is not much to do
 * as SEAMCALL/SEAMRET calls take care of most of save and restore.
 */
void tdx_prepare_switch_to_guest(struct kvm_vcpu *vcpu)
{
	struct vcpu_vt *vt = to_vt(vcpu);

	if (vt->guest_state_loaded)
		return;

	if (likely(is_64bit_mm(current->mm)))
		vt->msr_host_kernel_gs_base = current->thread.gsbase;
	else
		vt->msr_host_kernel_gs_base = read_msr(MSR_KERNEL_GS_BASE);

	vt->host_debugctlmsr = get_debugctlmsr();

	vt->guest_state_loaded = true;
}

struct tdx_uret_msr {
	u32 msr;
	unsigned int slot;
	u64 defval;
};

static struct tdx_uret_msr tdx_uret_msrs[] = {
	{.msr = MSR_SYSCALL_MASK, .defval = 0x20200 },
	{.msr = MSR_STAR,},
	{.msr = MSR_LSTAR,},
	{.msr = MSR_TSC_AUX,},
};

static void tdx_user_return_msr_update_cache(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tdx_uret_msrs); i++)
		kvm_user_return_msr_update_cache(tdx_uret_msrs[i].slot,
						 tdx_uret_msrs[i].defval);
}

static void tdx_prepare_switch_to_host(struct kvm_vcpu *vcpu)
{
	struct vcpu_vt *vt = to_vt(vcpu);
	struct vcpu_tdx *tdx = to_tdx(vcpu);

	if (!vt->guest_state_loaded)
		return;

	++vcpu->stat.host_state_reload;
	wrmsrl(MSR_KERNEL_GS_BASE, vt->msr_host_kernel_gs_base);

	if (tdx->guest_entered) {
		tdx_user_return_msr_update_cache();
		tdx->guest_entered = false;
	}

	vt->guest_state_loaded = false;
}

void tdx_vcpu_put(struct kvm_vcpu *vcpu)
{
	vmx_vcpu_pi_put(vcpu);
	tdx_prepare_switch_to_host(vcpu);
}

void tdx_vcpu_free(struct kvm_vcpu *vcpu)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(vcpu->kvm);
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	int i;

	/*
	 * It is not possible to reclaim pages while hkid is assigned. It might
	 * be assigned if:
	 * 1. the TD VM is being destroyed but freeing hkid failed, in which
	 * case the pages are leaked
	 * 2. TD VCPU creation failed and this on the error path, in which case
	 * there is nothing to do anyway
	 */
	if (is_hkid_assigned(kvm_tdx))
		return;

	if (tdx->vp.tdcx_pages) {
		for (i = 0; i < kvm_tdx->td.tdcx_nr_pages; i++) {
			if (tdx->vp.tdcx_pages[i])
				tdx_reclaim_control_page(tdx->vp.tdcx_pages[i]);
		}
		kfree(tdx->vp.tdcx_pages);
		tdx->vp.tdcx_pages = NULL;
	}
	if (tdx->vp.tdvpr_page) {
		tdx_reclaim_control_page(tdx->vp.tdvpr_page);
		tdx->vp.tdvpr_page = 0;
	}

	tdx->state = VCPU_TD_STATE_UNINITIALIZED;
}

int tdx_vcpu_pre_run(struct kvm_vcpu *vcpu)
{
	if (unlikely(to_tdx(vcpu)->state != VCPU_TD_STATE_INITIALIZED ||
		     to_kvm_tdx(vcpu->kvm)->state != TD_STATE_RUNNABLE))
		return -EINVAL;

	return 1;
}

static __always_inline u32 tdcall_to_vmx_exit_reason(struct kvm_vcpu *vcpu)
{
	switch (tdvmcall_leaf(vcpu)) {
	case EXIT_REASON_CPUID:
	case EXIT_REASON_HLT:
	case EXIT_REASON_IO_INSTRUCTION:
	case EXIT_REASON_MSR_READ:
	case EXIT_REASON_MSR_WRITE:
		return tdvmcall_leaf(vcpu);
	case EXIT_REASON_EPT_VIOLATION:
		return EXIT_REASON_EPT_MISCONFIG;
	default:
		break;
	}

	return EXIT_REASON_TDCALL;
}

static __always_inline u32 tdx_to_vmx_exit_reason(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	u32 exit_reason;

	switch (tdx->vp_enter_ret & TDX_SEAMCALL_STATUS_MASK) {
	case TDX_SUCCESS:
	case TDX_NON_RECOVERABLE_VCPU:
	case TDX_NON_RECOVERABLE_TD:
	case TDX_NON_RECOVERABLE_TD_NON_ACCESSIBLE:
	case TDX_NON_RECOVERABLE_TD_WRONG_APIC_MODE:
		break;
	default:
		return -1u;
	}

	exit_reason = tdx->vp_enter_ret;

	switch (exit_reason) {
	case EXIT_REASON_TDCALL:
		if (tdvmcall_exit_type(vcpu))
			return EXIT_REASON_VMCALL;

		return tdcall_to_vmx_exit_reason(vcpu);
	case EXIT_REASON_EPT_MISCONFIG:
		/*
		 * Defer KVM_BUG_ON() until tdx_handle_exit() because this is in
		 * non-instrumentable code with interrupts disabled.
		 */
		return -1u;
	default:
		break;
	}

	return exit_reason;
}

static noinstr void tdx_vcpu_enter_exit(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	struct vcpu_vt *vt = to_vt(vcpu);

	guest_state_enter_irqoff();

	tdx->vp_enter_ret = tdh_vp_enter(&tdx->vp, &tdx->vp_enter_args);

	vt->exit_reason.full = tdx_to_vmx_exit_reason(vcpu);

	vt->exit_qualification = tdx->vp_enter_args.rcx;
	tdx->ext_exit_qualification = tdx->vp_enter_args.rdx;
	tdx->exit_gpa = tdx->vp_enter_args.r8;
	vt->exit_intr_info = tdx->vp_enter_args.r9;

	vmx_handle_nmi(vcpu);

	guest_state_exit_irqoff();
}

static bool tdx_failed_vmentry(struct kvm_vcpu *vcpu)
{
	return vmx_get_exit_reason(vcpu).failed_vmentry &&
	       vmx_get_exit_reason(vcpu).full != -1u;
}

static fastpath_t tdx_exit_handlers_fastpath(struct kvm_vcpu *vcpu)
{
	u64 vp_enter_ret = to_tdx(vcpu)->vp_enter_ret;

	/*
	 * TDX_OPERAND_BUSY could be returned for SEPT due to 0-step mitigation
	 * or for TD EPOCH due to contention with TDH.MEM.TRACK on TDH.VP.ENTER.
	 *
	 * When KVM requests KVM_REQ_OUTSIDE_GUEST_MODE, which has both
	 * KVM_REQUEST_WAIT and KVM_REQUEST_NO_ACTION set, it requires target
	 * vCPUs leaving fastpath so that interrupt can be enabled to ensure the
	 * IPIs can be delivered. Return EXIT_FASTPATH_EXIT_HANDLED instead of
	 * EXIT_FASTPATH_REENTER_GUEST to exit fastpath, otherwise, the
	 * requester may be blocked endlessly.
	 */
	if (unlikely(tdx_operand_busy(vp_enter_ret)))
		return EXIT_FASTPATH_EXIT_HANDLED;

	return EXIT_FASTPATH_NONE;
}

#define TDX_REGS_AVAIL_SET	(BIT_ULL(VCPU_EXREG_EXIT_INFO_1) | \
				 BIT_ULL(VCPU_EXREG_EXIT_INFO_2) | \
				 BIT_ULL(VCPU_REGS_RAX) | \
				 BIT_ULL(VCPU_REGS_RBX) | \
				 BIT_ULL(VCPU_REGS_RCX) | \
				 BIT_ULL(VCPU_REGS_RDX) | \
				 BIT_ULL(VCPU_REGS_RBP) | \
				 BIT_ULL(VCPU_REGS_RSI) | \
				 BIT_ULL(VCPU_REGS_RDI) | \
				 BIT_ULL(VCPU_REGS_R8) | \
				 BIT_ULL(VCPU_REGS_R9) | \
				 BIT_ULL(VCPU_REGS_R10) | \
				 BIT_ULL(VCPU_REGS_R11) | \
				 BIT_ULL(VCPU_REGS_R12) | \
				 BIT_ULL(VCPU_REGS_R13) | \
				 BIT_ULL(VCPU_REGS_R14) | \
				 BIT_ULL(VCPU_REGS_R15))

static void tdx_load_host_xsave_state(struct kvm_vcpu *vcpu)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(vcpu->kvm);

	/*
	 * All TDX hosts support PKRU; but even if they didn't,
	 * vcpu->arch.host_pkru would be 0 and the wrpkru would be
	 * skipped.
	 */
	if (vcpu->arch.host_pkru != 0)
		wrpkru(vcpu->arch.host_pkru);

	if (kvm_host.xcr0 != (kvm_tdx->xfam & kvm_caps.supported_xcr0))
		xsetbv(XCR_XFEATURE_ENABLED_MASK, kvm_host.xcr0);

	/*
	 * Likewise, even if a TDX hosts didn't support XSS both arms of
	 * the comparison would be 0 and the wrmsrl would be skipped.
	 */
	if (kvm_host.xss != (kvm_tdx->xfam & kvm_caps.supported_xss))
		wrmsrl(MSR_IA32_XSS, kvm_host.xss);
}

#define TDX_DEBUGCTL_PRESERVED (DEBUGCTLMSR_BTF | \
				DEBUGCTLMSR_FREEZE_PERFMON_ON_PMI | \
				DEBUGCTLMSR_FREEZE_IN_SMM)

fastpath_t tdx_vcpu_run(struct kvm_vcpu *vcpu, bool force_immediate_exit)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	struct vcpu_vt *vt = to_vt(vcpu);

	/*
	 * force_immediate_exit requires vCPU entering for events injection with
	 * an immediately exit followed. But The TDX module doesn't guarantee
	 * entry, it's already possible for KVM to _think_ it completely entry
	 * to the guest without actually having done so.
	 * Since KVM never needs to force an immediate exit for TDX, and can't
	 * do direct injection, just warn on force_immediate_exit.
	 */
	WARN_ON_ONCE(force_immediate_exit);

	/*
	 * Wait until retry of SEPT-zap-related SEAMCALL completes before
	 * allowing vCPU entry to avoid contention with tdh_vp_enter() and
	 * TDCALLs.
	 */
	if (unlikely(READ_ONCE(to_kvm_tdx(vcpu->kvm)->wait_for_sept_zap)))
		return EXIT_FASTPATH_EXIT_HANDLED;

	trace_kvm_entry(vcpu, force_immediate_exit);

	if (pi_test_on(&vt->pi_desc)) {
		apic->send_IPI_self(POSTED_INTR_VECTOR);

		if (pi_test_pir(kvm_lapic_get_reg(vcpu->arch.apic, APIC_LVTT) &
			       APIC_VECTOR_MASK, &vt->pi_desc))
			kvm_wait_lapic_expire(vcpu);
	}

	tdx_vcpu_enter_exit(vcpu);

	if (vt->host_debugctlmsr & ~TDX_DEBUGCTL_PRESERVED)
		update_debugctlmsr(vt->host_debugctlmsr);

	tdx_load_host_xsave_state(vcpu);
	tdx->guest_entered = true;

	vcpu->arch.regs_avail &= TDX_REGS_AVAIL_SET;

	if (unlikely(tdx->vp_enter_ret == EXIT_REASON_EPT_MISCONFIG))
		return EXIT_FASTPATH_NONE;

	if (unlikely((tdx->vp_enter_ret & TDX_SW_ERROR) == TDX_SW_ERROR))
		return EXIT_FASTPATH_NONE;

	if (unlikely(vmx_get_exit_reason(vcpu).basic == EXIT_REASON_MCE_DURING_VMENTRY))
		kvm_machine_check();

	trace_kvm_exit(vcpu, KVM_ISA_VMX);

	if (unlikely(tdx_failed_vmentry(vcpu)))
		return EXIT_FASTPATH_NONE;

	return tdx_exit_handlers_fastpath(vcpu);
}

void tdx_inject_nmi(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.nmi_injections;
	td_management_write8(to_tdx(vcpu), TD_VCPU_PEND_NMI, 1);
	/*
	 * From KVM's perspective, NMI injection is completed right after
	 * writing to PEND_NMI.  KVM doesn't care whether an NMI is injected by
	 * the TDX module or not.
	 */
	vcpu->arch.nmi_injected = false;
	/*
	 * TDX doesn't support KVM to request NMI window exit.  If there is
	 * still a pending vNMI, KVM is not able to inject it along with the
	 * one pending in TDX module in a back-to-back way.  Since the previous
	 * vNMI is still pending in TDX module, i.e. it has not been delivered
	 * to TDX guest yet, it's OK to collapse the pending vNMI into the
	 * previous one.  The guest is expected to handle all the NMI sources
	 * when handling the first vNMI.
	 */
	vcpu->arch.nmi_pending = 0;
}

static int tdx_handle_exception_nmi(struct kvm_vcpu *vcpu)
{
	u32 intr_info = vmx_get_intr_info(vcpu);

	/*
	 * Machine checks are handled by handle_exception_irqoff(), or by
	 * tdx_handle_exit() with TDX_NON_RECOVERABLE set if a #MC occurs on
	 * VM-Entry.  NMIs are handled by tdx_vcpu_enter_exit().
	 */
	if (is_nmi(intr_info) || is_machine_check(intr_info))
		return 1;

	vcpu->run->exit_reason = KVM_EXIT_EXCEPTION;
	vcpu->run->ex.exception = intr_info & INTR_INFO_VECTOR_MASK;
	vcpu->run->ex.error_code = 0;

	return 0;
}

static int complete_hypercall_exit(struct kvm_vcpu *vcpu)
{
	tdvmcall_set_return_code(vcpu, vcpu->run->hypercall.ret);
	return 1;
}

static int tdx_emulate_vmcall(struct kvm_vcpu *vcpu)
{
	kvm_rax_write(vcpu, to_tdx(vcpu)->vp_enter_args.r10);
	kvm_rbx_write(vcpu, to_tdx(vcpu)->vp_enter_args.r11);
	kvm_rcx_write(vcpu, to_tdx(vcpu)->vp_enter_args.r12);
	kvm_rdx_write(vcpu, to_tdx(vcpu)->vp_enter_args.r13);
	kvm_rsi_write(vcpu, to_tdx(vcpu)->vp_enter_args.r14);

	return __kvm_emulate_hypercall(vcpu, 0, complete_hypercall_exit);
}

/*
 * Split into chunks and check interrupt pending between chunks.  This allows
 * for timely injection of interrupts to prevent issues with guest lockup
 * detection.
 */
#define TDX_MAP_GPA_MAX_LEN (2 * 1024 * 1024)
static void __tdx_map_gpa(struct vcpu_tdx *tdx);

static int tdx_complete_vmcall_map_gpa(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);

	if (vcpu->run->hypercall.ret) {
		tdvmcall_set_return_code(vcpu, TDVMCALL_STATUS_INVALID_OPERAND);
		tdx->vp_enter_args.r11 = tdx->map_gpa_next;
		return 1;
	}

	tdx->map_gpa_next += TDX_MAP_GPA_MAX_LEN;
	if (tdx->map_gpa_next >= tdx->map_gpa_end)
		return 1;

	/*
	 * Stop processing the remaining part if there is a pending interrupt,
	 * which could be qualified to deliver.  Skip checking pending RVI for
	 * TDVMCALL_MAP_GPA, see comments in tdx_protected_apic_has_interrupt().
	 */
	if (kvm_vcpu_has_events(vcpu)) {
		tdvmcall_set_return_code(vcpu, TDVMCALL_STATUS_RETRY);
		tdx->vp_enter_args.r11 = tdx->map_gpa_next;
		return 1;
	}

	__tdx_map_gpa(tdx);
	return 0;
}

static void __tdx_map_gpa(struct vcpu_tdx *tdx)
{
	u64 gpa = tdx->map_gpa_next;
	u64 size = tdx->map_gpa_end - tdx->map_gpa_next;

	if (size > TDX_MAP_GPA_MAX_LEN)
		size = TDX_MAP_GPA_MAX_LEN;

	tdx->vcpu.run->exit_reason       = KVM_EXIT_HYPERCALL;
	tdx->vcpu.run->hypercall.nr      = KVM_HC_MAP_GPA_RANGE;
	/*
	 * In principle this should have been -KVM_ENOSYS, but userspace (QEMU <=9.2)
	 * assumed that vcpu->run->hypercall.ret is never changed by KVM and thus that
	 * it was always zero on KVM_EXIT_HYPERCALL.  Since KVM is now overwriting
	 * vcpu->run->hypercall.ret, ensuring that it is zero to not break QEMU.
	 */
	tdx->vcpu.run->hypercall.ret = 0;
	tdx->vcpu.run->hypercall.args[0] = gpa & ~gfn_to_gpa(kvm_gfn_direct_bits(tdx->vcpu.kvm));
	tdx->vcpu.run->hypercall.args[1] = size / PAGE_SIZE;
	tdx->vcpu.run->hypercall.args[2] = vt_is_tdx_private_gpa(tdx->vcpu.kvm, gpa) ?
					   KVM_MAP_GPA_RANGE_ENCRYPTED :
					   KVM_MAP_GPA_RANGE_DECRYPTED;
	tdx->vcpu.run->hypercall.flags   = KVM_EXIT_HYPERCALL_LONG_MODE;

	tdx->vcpu.arch.complete_userspace_io = tdx_complete_vmcall_map_gpa;
}

static int tdx_map_gpa(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	u64 gpa = tdx->vp_enter_args.r12;
	u64 size = tdx->vp_enter_args.r13;
	u64 ret;

	/*
	 * Converting TDVMCALL_MAP_GPA to KVM_HC_MAP_GPA_RANGE requires
	 * userspace to enable KVM_CAP_EXIT_HYPERCALL with KVM_HC_MAP_GPA_RANGE
	 * bit set.  This is a base call so it should always be supported, but
	 * KVM has no way to ensure that userspace implements the GHCI correctly.
	 * So if KVM_HC_MAP_GPA_RANGE does not cause a VMEXIT, return an error
	 * to the guest.
	 */
	if (!user_exit_on_hypercall(vcpu->kvm, KVM_HC_MAP_GPA_RANGE)) {
		ret = TDVMCALL_STATUS_SUBFUNC_UNSUPPORTED;
		goto error;
	}

	if (gpa + size <= gpa || !kvm_vcpu_is_legal_gpa(vcpu, gpa) ||
	    !kvm_vcpu_is_legal_gpa(vcpu, gpa + size - 1) ||
	    (vt_is_tdx_private_gpa(vcpu->kvm, gpa) !=
	     vt_is_tdx_private_gpa(vcpu->kvm, gpa + size - 1))) {
		ret = TDVMCALL_STATUS_INVALID_OPERAND;
		goto error;
	}

	if (!PAGE_ALIGNED(gpa) || !PAGE_ALIGNED(size)) {
		ret = TDVMCALL_STATUS_ALIGN_ERROR;
		goto error;
	}

	tdx->map_gpa_end = gpa + size;
	tdx->map_gpa_next = gpa;

	__tdx_map_gpa(tdx);
	return 0;

error:
	tdvmcall_set_return_code(vcpu, ret);
	tdx->vp_enter_args.r11 = gpa;
	return 1;
}

static int tdx_report_fatal_error(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	u64 *regs = vcpu->run->system_event.data;
	u64 *module_regs = &tdx->vp_enter_args.r8;
	int index = VCPU_REGS_RAX;

	vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
	vcpu->run->system_event.type = KVM_SYSTEM_EVENT_TDX_FATAL;
	vcpu->run->system_event.ndata = 16;

	/* Dump 16 general-purpose registers to userspace in ascending order. */
	regs[index++] = tdx->vp_enter_ret;
	regs[index++] = tdx->vp_enter_args.rcx;
	regs[index++] = tdx->vp_enter_args.rdx;
	regs[index++] = tdx->vp_enter_args.rbx;
	regs[index++] = 0;
	regs[index++] = 0;
	regs[index++] = tdx->vp_enter_args.rsi;
	regs[index] = tdx->vp_enter_args.rdi;
	for (index = 0; index < 8; index++)
		regs[VCPU_REGS_R8 + index] = module_regs[index];

	return 0;
}

static int tdx_emulate_cpuid(struct kvm_vcpu *vcpu)
{
	u32 eax, ebx, ecx, edx;
	struct vcpu_tdx *tdx = to_tdx(vcpu);

	/* EAX and ECX for cpuid is stored in R12 and R13. */
	eax = tdx->vp_enter_args.r12;
	ecx = tdx->vp_enter_args.r13;

	kvm_cpuid(vcpu, &eax, &ebx, &ecx, &edx, false);

	tdx->vp_enter_args.r12 = eax;
	tdx->vp_enter_args.r13 = ebx;
	tdx->vp_enter_args.r14 = ecx;
	tdx->vp_enter_args.r15 = edx;

	return 1;
}

static int tdx_complete_pio_out(struct kvm_vcpu *vcpu)
{
	vcpu->arch.pio.count = 0;
	return 1;
}

static int tdx_complete_pio_in(struct kvm_vcpu *vcpu)
{
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;
	unsigned long val = 0;
	int ret;

	ret = ctxt->ops->pio_in_emulated(ctxt, vcpu->arch.pio.size,
					 vcpu->arch.pio.port, &val, 1);

	WARN_ON_ONCE(!ret);

	tdvmcall_set_return_val(vcpu, val);

	return 1;
}

static int tdx_emulate_io(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	struct x86_emulate_ctxt *ctxt = vcpu->arch.emulate_ctxt;
	unsigned long val = 0;
	unsigned int port;
	u64 size, write;
	int ret;

	++vcpu->stat.io_exits;

	size = tdx->vp_enter_args.r12;
	write = tdx->vp_enter_args.r13;
	port = tdx->vp_enter_args.r14;

	if ((write != 0 && write != 1) || (size != 1 && size != 2 && size != 4)) {
		tdvmcall_set_return_code(vcpu, TDVMCALL_STATUS_INVALID_OPERAND);
		return 1;
	}

	if (write) {
		val = tdx->vp_enter_args.r15;
		ret = ctxt->ops->pio_out_emulated(ctxt, size, port, &val, 1);
	} else {
		ret = ctxt->ops->pio_in_emulated(ctxt, size, port, &val, 1);
	}

	if (!ret)
		vcpu->arch.complete_userspace_io = write ? tdx_complete_pio_out :
							   tdx_complete_pio_in;
	else if (!write)
		tdvmcall_set_return_val(vcpu, val);

	return ret;
}

static int tdx_complete_mmio_read(struct kvm_vcpu *vcpu)
{
	unsigned long val = 0;
	gpa_t gpa;
	int size;

	gpa = vcpu->mmio_fragments[0].gpa;
	size = vcpu->mmio_fragments[0].len;

	memcpy(&val, vcpu->run->mmio.data, size);
	tdvmcall_set_return_val(vcpu, val);
	trace_kvm_mmio(KVM_TRACE_MMIO_READ, size, gpa, &val);
	return 1;
}

static inline int tdx_mmio_write(struct kvm_vcpu *vcpu, gpa_t gpa, int size,
				 unsigned long val)
{
	if (!kvm_io_bus_write(vcpu, KVM_FAST_MMIO_BUS, gpa, 0, NULL)) {
		trace_kvm_fast_mmio(gpa);
		return 0;
	}

	trace_kvm_mmio(KVM_TRACE_MMIO_WRITE, size, gpa, &val);
	if (kvm_io_bus_write(vcpu, KVM_MMIO_BUS, gpa, size, &val))
		return -EOPNOTSUPP;

	return 0;
}

static inline int tdx_mmio_read(struct kvm_vcpu *vcpu, gpa_t gpa, int size)
{
	unsigned long val;

	if (kvm_io_bus_read(vcpu, KVM_MMIO_BUS, gpa, size, &val))
		return -EOPNOTSUPP;

	tdvmcall_set_return_val(vcpu, val);
	trace_kvm_mmio(KVM_TRACE_MMIO_READ, size, gpa, &val);
	return 0;
}

static int tdx_emulate_mmio(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	int size, write, r;
	unsigned long val;
	gpa_t gpa;

	size = tdx->vp_enter_args.r12;
	write = tdx->vp_enter_args.r13;
	gpa = tdx->vp_enter_args.r14;
	val = write ? tdx->vp_enter_args.r15 : 0;

	if (size != 1 && size != 2 && size != 4 && size != 8)
		goto error;
	if (write != 0 && write != 1)
		goto error;

	/*
	 * TDG.VP.VMCALL<MMIO> allows only shared GPA, it makes no sense to
	 * do MMIO emulation for private GPA.
	 */
	if (vt_is_tdx_private_gpa(vcpu->kvm, gpa) ||
	    vt_is_tdx_private_gpa(vcpu->kvm, gpa + size - 1))
		goto error;

	gpa = gpa & ~gfn_to_gpa(kvm_gfn_direct_bits(vcpu->kvm));

	if (write)
		r = tdx_mmio_write(vcpu, gpa, size, val);
	else
		r = tdx_mmio_read(vcpu, gpa, size);
	if (!r)
		/* Kernel completed device emulation. */
		return 1;

	/* Request the device emulation to userspace device model. */
	vcpu->mmio_is_write = write;
	if (!write)
		vcpu->arch.complete_userspace_io = tdx_complete_mmio_read;

	vcpu->run->mmio.phys_addr = gpa;
	vcpu->run->mmio.len = size;
	vcpu->run->mmio.is_write = write;
	vcpu->run->exit_reason = KVM_EXIT_MMIO;

	if (write) {
		memcpy(vcpu->run->mmio.data, &val, size);
	} else {
		vcpu->mmio_fragments[0].gpa = gpa;
		vcpu->mmio_fragments[0].len = size;
		trace_kvm_mmio(KVM_TRACE_MMIO_READ_UNSATISFIED, size, gpa, NULL);
	}
	return 0;

error:
	tdvmcall_set_return_code(vcpu, TDVMCALL_STATUS_INVALID_OPERAND);
	return 1;
}

static int tdx_complete_get_td_vm_call_info(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);

	tdvmcall_set_return_code(vcpu, vcpu->run->tdx.get_tdvmcall_info.ret);

	/*
	 * For now, there is no TDVMCALL beyond GHCI base API supported by KVM
	 * directly without the support from userspace, just set the value
	 * returned from userspace.
	 */
	tdx->vp_enter_args.r11 = vcpu->run->tdx.get_tdvmcall_info.r11;
	tdx->vp_enter_args.r12 = vcpu->run->tdx.get_tdvmcall_info.r12;
	tdx->vp_enter_args.r13 = vcpu->run->tdx.get_tdvmcall_info.r13;
	tdx->vp_enter_args.r14 = vcpu->run->tdx.get_tdvmcall_info.r14;

	return 1;
}

static int tdx_get_td_vm_call_info(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);

	switch (tdx->vp_enter_args.r12) {
	case 0:
		tdx->vp_enter_args.r11 = 0;
		tdx->vp_enter_args.r12 = 0;
		tdx->vp_enter_args.r13 = 0;
		tdx->vp_enter_args.r14 = 0;
		tdvmcall_set_return_code(vcpu, TDVMCALL_STATUS_SUCCESS);
		return 1;
	case 1:
		vcpu->run->tdx.get_tdvmcall_info.leaf = tdx->vp_enter_args.r12;
		vcpu->run->exit_reason = KVM_EXIT_TDX;
		vcpu->run->tdx.flags = 0;
		vcpu->run->tdx.nr = TDVMCALL_GET_TD_VM_CALL_INFO;
		vcpu->run->tdx.get_tdvmcall_info.ret = TDVMCALL_STATUS_SUCCESS;
		vcpu->run->tdx.get_tdvmcall_info.r11 = 0;
		vcpu->run->tdx.get_tdvmcall_info.r12 = 0;
		vcpu->run->tdx.get_tdvmcall_info.r13 = 0;
		vcpu->run->tdx.get_tdvmcall_info.r14 = 0;
		vcpu->arch.complete_userspace_io = tdx_complete_get_td_vm_call_info;
		return 0;
	default:
		tdvmcall_set_return_code(vcpu, TDVMCALL_STATUS_INVALID_OPERAND);
		return 1;
	}
}

static int tdx_complete_simple(struct kvm_vcpu *vcpu)
{
	tdvmcall_set_return_code(vcpu, vcpu->run->tdx.unknown.ret);
	return 1;
}

static int tdx_get_quote(struct kvm_vcpu *vcpu)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	u64 gpa = tdx->vp_enter_args.r12;
	u64 size = tdx->vp_enter_args.r13;

	/* The gpa of buffer must have shared bit set. */
	if (vt_is_tdx_private_gpa(vcpu->kvm, gpa)) {
		tdvmcall_set_return_code(vcpu, TDVMCALL_STATUS_INVALID_OPERAND);
		return 1;
	}

	vcpu->run->exit_reason = KVM_EXIT_TDX;
	vcpu->run->tdx.flags = 0;
	vcpu->run->tdx.nr = TDVMCALL_GET_QUOTE;
	vcpu->run->tdx.get_quote.ret = TDVMCALL_STATUS_SUBFUNC_UNSUPPORTED;
	vcpu->run->tdx.get_quote.gpa = gpa & ~gfn_to_gpa(kvm_gfn_direct_bits(tdx->vcpu.kvm));
	vcpu->run->tdx.get_quote.size = size;

	vcpu->arch.complete_userspace_io = tdx_complete_simple;

	return 0;
}

static int handle_tdvmcall(struct kvm_vcpu *vcpu)
{
	switch (tdvmcall_leaf(vcpu)) {
	case TDVMCALL_MAP_GPA:
		return tdx_map_gpa(vcpu);
	case TDVMCALL_REPORT_FATAL_ERROR:
		return tdx_report_fatal_error(vcpu);
	case TDVMCALL_GET_TD_VM_CALL_INFO:
		return tdx_get_td_vm_call_info(vcpu);
	case TDVMCALL_GET_QUOTE:
		return tdx_get_quote(vcpu);
	default:
		break;
	}

	tdvmcall_set_return_code(vcpu, TDVMCALL_STATUS_SUBFUNC_UNSUPPORTED);
	return 1;
}

void tdx_load_mmu_pgd(struct kvm_vcpu *vcpu, hpa_t root_hpa, int pgd_level)
{
	u64 shared_bit = (pgd_level == 5) ? TDX_SHARED_BIT_PWL_5 :
			  TDX_SHARED_BIT_PWL_4;

	if (KVM_BUG_ON(shared_bit != kvm_gfn_direct_bits(vcpu->kvm), vcpu->kvm))
		return;

	td_vmcs_write64(to_tdx(vcpu), SHARED_EPT_POINTER, root_hpa);
}

static void tdx_unpin(struct kvm *kvm, struct page *page)
{
	put_page(page);
}

static int tdx_mem_page_aug(struct kvm *kvm, gfn_t gfn,
			    enum pg_level level, struct page *page)
{
	int tdx_level = pg_level_to_tdx_sept_level(level);
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	gpa_t gpa = gfn_to_gpa(gfn);
	u64 entry, level_state;
	u64 err;

	err = tdh_mem_page_aug(&kvm_tdx->td, gpa, tdx_level, page, &entry, &level_state);
	if (unlikely(tdx_operand_busy(err))) {
		tdx_unpin(kvm, page);
		return -EBUSY;
	}

	if (KVM_BUG_ON(err, kvm)) {
		pr_tdx_error_2(TDH_MEM_PAGE_AUG, err, entry, level_state);
		tdx_unpin(kvm, page);
		return -EIO;
	}

	return 0;
}

/*
 * KVM_TDX_INIT_MEM_REGION calls kvm_gmem_populate() to map guest pages; the
 * callback tdx_gmem_post_populate() then maps pages into private memory.
 * through the a seamcall TDH.MEM.PAGE.ADD().  The SEAMCALL also requires the
 * private EPT structures for the page to have been built before, which is
 * done via kvm_tdp_map_page(). nr_premapped counts the number of pages that
 * were added to the EPT structures but not added with TDH.MEM.PAGE.ADD().
 * The counter has to be zero on KVM_TDX_FINALIZE_VM, to ensure that there
 * are no half-initialized shared EPT pages.
 */
static int tdx_mem_page_record_premap_cnt(struct kvm *kvm, gfn_t gfn,
					  enum pg_level level, kvm_pfn_t pfn)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);

	if (KVM_BUG_ON(kvm->arch.pre_fault_allowed, kvm))
		return -EINVAL;

	/* nr_premapped will be decreased when tdh_mem_page_add() is called. */
	atomic64_inc(&kvm_tdx->nr_premapped);
	return 0;
}

int tdx_sept_set_private_spte(struct kvm *kvm, gfn_t gfn,
			      enum pg_level level, kvm_pfn_t pfn)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	struct page *page = pfn_to_page(pfn);

	/* TODO: handle large pages. */
	if (KVM_BUG_ON(level != PG_LEVEL_4K, kvm))
		return -EINVAL;

	/*
	 * Because guest_memfd doesn't support page migration with
	 * a_ops->migrate_folio (yet), no callback is triggered for KVM on page
	 * migration.  Until guest_memfd supports page migration, prevent page
	 * migration.
	 * TODO: Once guest_memfd introduces callback on page migration,
	 * implement it and remove get_page/put_page().
	 */
	get_page(page);

	/*
	 * Read 'pre_fault_allowed' before 'kvm_tdx->state'; see matching
	 * barrier in tdx_td_finalize().
	 */
	smp_rmb();
	if (likely(kvm_tdx->state == TD_STATE_RUNNABLE))
		return tdx_mem_page_aug(kvm, gfn, level, page);

	return tdx_mem_page_record_premap_cnt(kvm, gfn, level, pfn);
}

static int tdx_sept_drop_private_spte(struct kvm *kvm, gfn_t gfn,
				      enum pg_level level, struct page *page)
{
	int tdx_level = pg_level_to_tdx_sept_level(level);
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	gpa_t gpa = gfn_to_gpa(gfn);
	u64 err, entry, level_state;

	/* TODO: handle large pages. */
	if (KVM_BUG_ON(level != PG_LEVEL_4K, kvm))
		return -EINVAL;

	if (KVM_BUG_ON(!is_hkid_assigned(kvm_tdx), kvm))
		return -EINVAL;

	/*
	 * When zapping private page, write lock is held. So no race condition
	 * with other vcpu sept operation.
	 * Race with TDH.VP.ENTER due to (0-step mitigation) and Guest TDCALLs.
	 */
	err = tdh_mem_page_remove(&kvm_tdx->td, gpa, tdx_level, &entry,
				  &level_state);

	if (unlikely(tdx_operand_busy(err))) {
		/*
		 * The second retry is expected to succeed after kicking off all
		 * other vCPUs and prevent them from invoking TDH.VP.ENTER.
		 */
		tdx_no_vcpus_enter_start(kvm);
		err = tdh_mem_page_remove(&kvm_tdx->td, gpa, tdx_level, &entry,
					  &level_state);
		tdx_no_vcpus_enter_stop(kvm);
	}

	if (KVM_BUG_ON(err, kvm)) {
		pr_tdx_error_2(TDH_MEM_PAGE_REMOVE, err, entry, level_state);
		return -EIO;
	}

	err = tdh_phymem_page_wbinvd_hkid((u16)kvm_tdx->hkid, page);

	if (KVM_BUG_ON(err, kvm)) {
		pr_tdx_error(TDH_PHYMEM_PAGE_WBINVD, err);
		return -EIO;
	}
	tdx_clear_page(page);
	tdx_unpin(kvm, page);
	return 0;
}

int tdx_sept_link_private_spt(struct kvm *kvm, gfn_t gfn,
			      enum pg_level level, void *private_spt)
{
	int tdx_level = pg_level_to_tdx_sept_level(level);
	gpa_t gpa = gfn_to_gpa(gfn);
	struct page *page = virt_to_page(private_spt);
	u64 err, entry, level_state;

	err = tdh_mem_sept_add(&to_kvm_tdx(kvm)->td, gpa, tdx_level, page, &entry,
			       &level_state);
	if (unlikely(tdx_operand_busy(err)))
		return -EBUSY;

	if (KVM_BUG_ON(err, kvm)) {
		pr_tdx_error_2(TDH_MEM_SEPT_ADD, err, entry, level_state);
		return -EIO;
	}

	return 0;
}

/*
 * Check if the error returned from a SEPT zap SEAMCALL is due to that a page is
 * mapped by KVM_TDX_INIT_MEM_REGION without tdh_mem_page_add() being called
 * successfully.
 *
 * Since tdh_mem_sept_add() must have been invoked successfully before a
 * non-leaf entry present in the mirrored page table, the SEPT ZAP related
 * SEAMCALLs should not encounter err TDX_EPT_WALK_FAILED. They should instead
 * find TDX_EPT_ENTRY_STATE_INCORRECT due to an empty leaf entry found in the
 * SEPT.
 *
 * Further check if the returned entry from SEPT walking is with RWX permissions
 * to filter out anything unexpected.
 *
 * Note: @level is pg_level, not the tdx_level. The tdx_level extracted from
 * level_state returned from a SEAMCALL error is the same as that passed into
 * the SEAMCALL.
 */
static int tdx_is_sept_zap_err_due_to_premap(struct kvm_tdx *kvm_tdx, u64 err,
					     u64 entry, int level)
{
	if (!err || kvm_tdx->state == TD_STATE_RUNNABLE)
		return false;

	if (err != (TDX_EPT_ENTRY_STATE_INCORRECT | TDX_OPERAND_ID_RCX))
		return false;

	if ((is_last_spte(entry, level) && (entry & VMX_EPT_RWX_MASK)))
		return false;

	return true;
}

static int tdx_sept_zap_private_spte(struct kvm *kvm, gfn_t gfn,
				     enum pg_level level, struct page *page)
{
	int tdx_level = pg_level_to_tdx_sept_level(level);
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	gpa_t gpa = gfn_to_gpa(gfn) & KVM_HPAGE_MASK(level);
	u64 err, entry, level_state;

	/* For now large page isn't supported yet. */
	WARN_ON_ONCE(level != PG_LEVEL_4K);

	err = tdh_mem_range_block(&kvm_tdx->td, gpa, tdx_level, &entry, &level_state);

	if (unlikely(tdx_operand_busy(err))) {
		/* After no vCPUs enter, the second retry is expected to succeed */
		tdx_no_vcpus_enter_start(kvm);
		err = tdh_mem_range_block(&kvm_tdx->td, gpa, tdx_level, &entry, &level_state);
		tdx_no_vcpus_enter_stop(kvm);
	}
	if (tdx_is_sept_zap_err_due_to_premap(kvm_tdx, err, entry, level) &&
	    !KVM_BUG_ON(!atomic64_read(&kvm_tdx->nr_premapped), kvm)) {
		atomic64_dec(&kvm_tdx->nr_premapped);
		tdx_unpin(kvm, page);
		return 0;
	}

	if (KVM_BUG_ON(err, kvm)) {
		pr_tdx_error_2(TDH_MEM_RANGE_BLOCK, err, entry, level_state);
		return -EIO;
	}
	return 1;
}

/*
 * Ensure shared and private EPTs to be flushed on all vCPUs.
 * tdh_mem_track() is the only caller that increases TD epoch. An increase in
 * the TD epoch (e.g., to value "N + 1") is successful only if no vCPUs are
 * running in guest mode with the value "N - 1".
 *
 * A successful execution of tdh_mem_track() ensures that vCPUs can only run in
 * guest mode with TD epoch value "N" if no TD exit occurs after the TD epoch
 * being increased to "N + 1".
 *
 * Kicking off all vCPUs after that further results in no vCPUs can run in guest
 * mode with TD epoch value "N", which unblocks the next tdh_mem_track() (e.g.
 * to increase TD epoch to "N + 2").
 *
 * TDX module will flush EPT on the next TD enter and make vCPUs to run in
 * guest mode with TD epoch value "N + 1".
 *
 * kvm_make_all_cpus_request() guarantees all vCPUs are out of guest mode by
 * waiting empty IPI handler ack_kick().
 *
 * No action is required to the vCPUs being kicked off since the kicking off
 * occurs certainly after TD epoch increment and before the next
 * tdh_mem_track().
 */
static void tdx_track(struct kvm *kvm)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	u64 err;

	/* If TD isn't finalized, it's before any vcpu running. */
	if (unlikely(kvm_tdx->state != TD_STATE_RUNNABLE))
		return;

	lockdep_assert_held_write(&kvm->mmu_lock);

	err = tdh_mem_track(&kvm_tdx->td);
	if (unlikely(tdx_operand_busy(err))) {
		/* After no vCPUs enter, the second retry is expected to succeed */
		tdx_no_vcpus_enter_start(kvm);
		err = tdh_mem_track(&kvm_tdx->td);
		tdx_no_vcpus_enter_stop(kvm);
	}

	if (KVM_BUG_ON(err, kvm))
		pr_tdx_error(TDH_MEM_TRACK, err);

	kvm_make_all_cpus_request(kvm, KVM_REQ_OUTSIDE_GUEST_MODE);
}

int tdx_sept_free_private_spt(struct kvm *kvm, gfn_t gfn,
			      enum pg_level level, void *private_spt)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);

	/*
	 * free_external_spt() is only called after hkid is freed when TD is
	 * tearing down.
	 * KVM doesn't (yet) zap page table pages in mirror page table while
	 * TD is active, though guest pages mapped in mirror page table could be
	 * zapped during TD is active, e.g. for shared <-> private conversion
	 * and slot move/deletion.
	 */
	if (KVM_BUG_ON(is_hkid_assigned(kvm_tdx), kvm))
		return -EINVAL;

	/*
	 * The HKID assigned to this TD was already freed and cache was
	 * already flushed. We don't have to flush again.
	 */
	return tdx_reclaim_page(virt_to_page(private_spt));
}

int tdx_sept_remove_private_spte(struct kvm *kvm, gfn_t gfn,
				 enum pg_level level, kvm_pfn_t pfn)
{
	struct page *page = pfn_to_page(pfn);
	int ret;

	/*
	 * HKID is released after all private pages have been removed, and set
	 * before any might be populated. Warn if zapping is attempted when
	 * there can't be anything populated in the private EPT.
	 */
	if (KVM_BUG_ON(!is_hkid_assigned(to_kvm_tdx(kvm)), kvm))
		return -EINVAL;

	ret = tdx_sept_zap_private_spte(kvm, gfn, level, page);
	if (ret <= 0)
		return ret;

	/*
	 * TDX requires TLB tracking before dropping private page.  Do
	 * it here, although it is also done later.
	 */
	tdx_track(kvm);

	return tdx_sept_drop_private_spte(kvm, gfn, level, page);
}

void tdx_deliver_interrupt(struct kvm_lapic *apic, int delivery_mode,
			   int trig_mode, int vector)
{
	struct kvm_vcpu *vcpu = apic->vcpu;
	struct vcpu_tdx *tdx = to_tdx(vcpu);

	/* TDX supports only posted interrupt.  No lapic emulation. */
	__vmx_deliver_posted_interrupt(vcpu, &tdx->vt.pi_desc, vector);

	trace_kvm_apicv_accept_irq(vcpu->vcpu_id, delivery_mode, trig_mode, vector);
}

static inline bool tdx_is_sept_violation_unexpected_pending(struct kvm_vcpu *vcpu)
{
	u64 eeq_type = to_tdx(vcpu)->ext_exit_qualification & TDX_EXT_EXIT_QUAL_TYPE_MASK;
	u64 eq = vmx_get_exit_qual(vcpu);

	if (eeq_type != TDX_EXT_EXIT_QUAL_TYPE_PENDING_EPT_VIOLATION)
		return false;

	return !(eq & EPT_VIOLATION_PROT_MASK) && !(eq & EPT_VIOLATION_EXEC_FOR_RING3_LIN);
}

static int tdx_handle_ept_violation(struct kvm_vcpu *vcpu)
{
	unsigned long exit_qual;
	gpa_t gpa = to_tdx(vcpu)->exit_gpa;
	bool local_retry = false;
	int ret;

	if (vt_is_tdx_private_gpa(vcpu->kvm, gpa)) {
		if (tdx_is_sept_violation_unexpected_pending(vcpu)) {
			pr_warn("Guest access before accepting 0x%llx on vCPU %d\n",
				gpa, vcpu->vcpu_id);
			kvm_vm_dead(vcpu->kvm);
			return -EIO;
		}
		/*
		 * Always treat SEPT violations as write faults.  Ignore the
		 * EXIT_QUALIFICATION reported by TDX-SEAM for SEPT violations.
		 * TD private pages are always RWX in the SEPT tables,
		 * i.e. they're always mapped writable.  Just as importantly,
		 * treating SEPT violations as write faults is necessary to
		 * avoid COW allocations, which will cause TDAUGPAGE failures
		 * due to aliasing a single HPA to multiple GPAs.
		 */
		exit_qual = EPT_VIOLATION_ACC_WRITE;

		/* Only private GPA triggers zero-step mitigation */
		local_retry = true;
	} else {
		exit_qual = vmx_get_exit_qual(vcpu);
		/*
		 * EPT violation due to instruction fetch should never be
		 * triggered from shared memory in TDX guest.  If such EPT
		 * violation occurs, treat it as broken hardware.
		 */
		if (KVM_BUG_ON(exit_qual & EPT_VIOLATION_ACC_INSTR, vcpu->kvm))
			return -EIO;
	}

	trace_kvm_page_fault(vcpu, gpa, exit_qual);

	/*
	 * To minimize TDH.VP.ENTER invocations, retry locally for private GPA
	 * mapping in TDX.
	 *
	 * KVM may return RET_PF_RETRY for private GPA due to
	 * - contentions when atomically updating SPTEs of the mirror page table
	 * - in-progress GFN invalidation or memslot removal.
	 * - TDX_OPERAND_BUSY error from TDH.MEM.PAGE.AUG or TDH.MEM.SEPT.ADD,
	 *   caused by contentions with TDH.VP.ENTER (with zero-step mitigation)
	 *   or certain TDCALLs.
	 *
	 * If TDH.VP.ENTER is invoked more times than the threshold set by the
	 * TDX module before KVM resolves the private GPA mapping, the TDX
	 * module will activate zero-step mitigation during TDH.VP.ENTER. This
	 * process acquires an SEPT tree lock in the TDX module, leading to
	 * further contentions with TDH.MEM.PAGE.AUG or TDH.MEM.SEPT.ADD
	 * operations on other vCPUs.
	 *
	 * Breaking out of local retries for kvm_vcpu_has_events() is for
	 * interrupt injection. kvm_vcpu_has_events() should not see pending
	 * events for TDX. Since KVM can't determine if IRQs (or NMIs) are
	 * blocked by TDs, false positives are inevitable i.e., KVM may re-enter
	 * the guest even if the IRQ/NMI can't be delivered.
	 *
	 * Note: even without breaking out of local retries, zero-step
	 * mitigation may still occur due to
	 * - invoking of TDH.VP.ENTER after KVM_EXIT_MEMORY_FAULT,
	 * - a single RIP causing EPT violations for more GFNs than the
	 *   threshold count.
	 * This is safe, as triggering zero-step mitigation only introduces
	 * contentions to page installation SEAMCALLs on other vCPUs, which will
	 * handle retries locally in their EPT violation handlers.
	 */
	while (1) {
		ret = __vmx_handle_ept_violation(vcpu, gpa, exit_qual);

		if (ret != RET_PF_RETRY || !local_retry)
			break;

		if (kvm_vcpu_has_events(vcpu) || signal_pending(current))
			break;

		if (kvm_check_request(KVM_REQ_VM_DEAD, vcpu)) {
			ret = -EIO;
			break;
		}

		cond_resched();
	}
	return ret;
}

int tdx_complete_emulated_msr(struct kvm_vcpu *vcpu, int err)
{
	if (err) {
		tdvmcall_set_return_code(vcpu, TDVMCALL_STATUS_INVALID_OPERAND);
		return 1;
	}

	if (vmx_get_exit_reason(vcpu).basic == EXIT_REASON_MSR_READ)
		tdvmcall_set_return_val(vcpu, kvm_read_edx_eax(vcpu));

	return 1;
}


int tdx_handle_exit(struct kvm_vcpu *vcpu, fastpath_t fastpath)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	u64 vp_enter_ret = tdx->vp_enter_ret;
	union vmx_exit_reason exit_reason = vmx_get_exit_reason(vcpu);

	if (fastpath != EXIT_FASTPATH_NONE)
		return 1;

	if (unlikely(vp_enter_ret == EXIT_REASON_EPT_MISCONFIG)) {
		KVM_BUG_ON(1, vcpu->kvm);
		return -EIO;
	}

	/*
	 * Handle TDX SW errors, including TDX_SEAMCALL_UD, TDX_SEAMCALL_GP and
	 * TDX_SEAMCALL_VMFAILINVALID.
	 */
	if (unlikely((vp_enter_ret & TDX_SW_ERROR) == TDX_SW_ERROR)) {
		KVM_BUG_ON(!kvm_rebooting, vcpu->kvm);
		goto unhandled_exit;
	}

	if (unlikely(tdx_failed_vmentry(vcpu))) {
		/*
		 * If the guest state is protected, that means off-TD debug is
		 * not enabled, TDX_NON_RECOVERABLE must be set.
		 */
		WARN_ON_ONCE(vcpu->arch.guest_state_protected &&
				!(vp_enter_ret & TDX_NON_RECOVERABLE));
		vcpu->run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		vcpu->run->fail_entry.hardware_entry_failure_reason = exit_reason.full;
		vcpu->run->fail_entry.cpu = vcpu->arch.last_vmentry_cpu;
		return 0;
	}

	if (unlikely(vp_enter_ret & (TDX_ERROR | TDX_NON_RECOVERABLE)) &&
		exit_reason.basic != EXIT_REASON_TRIPLE_FAULT) {
		kvm_pr_unimpl("TD vp_enter_ret 0x%llx\n", vp_enter_ret);
		goto unhandled_exit;
	}

	WARN_ON_ONCE(exit_reason.basic != EXIT_REASON_TRIPLE_FAULT &&
		     (vp_enter_ret & TDX_SEAMCALL_STATUS_MASK) != TDX_SUCCESS);

	switch (exit_reason.basic) {
	case EXIT_REASON_TRIPLE_FAULT:
		vcpu->run->exit_reason = KVM_EXIT_SHUTDOWN;
		vcpu->mmio_needed = 0;
		return 0;
	case EXIT_REASON_EXCEPTION_NMI:
		return tdx_handle_exception_nmi(vcpu);
	case EXIT_REASON_EXTERNAL_INTERRUPT:
		++vcpu->stat.irq_exits;
		return 1;
	case EXIT_REASON_CPUID:
		return tdx_emulate_cpuid(vcpu);
	case EXIT_REASON_HLT:
		return kvm_emulate_halt_noskip(vcpu);
	case EXIT_REASON_TDCALL:
		return handle_tdvmcall(vcpu);
	case EXIT_REASON_VMCALL:
		return tdx_emulate_vmcall(vcpu);
	case EXIT_REASON_IO_INSTRUCTION:
		return tdx_emulate_io(vcpu);
	case EXIT_REASON_MSR_READ:
		kvm_rcx_write(vcpu, tdx->vp_enter_args.r12);
		return kvm_emulate_rdmsr(vcpu);
	case EXIT_REASON_MSR_WRITE:
		kvm_rcx_write(vcpu, tdx->vp_enter_args.r12);
		kvm_rax_write(vcpu, tdx->vp_enter_args.r13 & -1u);
		kvm_rdx_write(vcpu, tdx->vp_enter_args.r13 >> 32);
		return kvm_emulate_wrmsr(vcpu);
	case EXIT_REASON_EPT_MISCONFIG:
		return tdx_emulate_mmio(vcpu);
	case EXIT_REASON_EPT_VIOLATION:
		return tdx_handle_ept_violation(vcpu);
	case EXIT_REASON_OTHER_SMI:
		/*
		 * Unlike VMX, SMI in SEAM non-root mode (i.e. when
		 * TD guest vCPU is running) will cause VM exit to TDX module,
		 * then SEAMRET to KVM.  Once it exits to KVM, SMI is delivered
		 * and handled by kernel handler right away.
		 *
		 * The Other SMI exit can also be caused by the SEAM non-root
		 * machine check delivered via Machine Check System Management
		 * Interrupt (MSMI), but it has already been handled by the
		 * kernel machine check handler, i.e., the memory page has been
		 * marked as poisoned and it won't be freed to the free list
		 * when the TDX guest is terminated (the TDX module marks the
		 * guest as dead and prevent it from further running when
		 * machine check happens in SEAM non-root).
		 *
		 * - A MSMI will not reach here, it's handled as non_recoverable
		 *   case above.
		 * - If it's not an MSMI, no need to do anything here.
		 */
		return 1;
	default:
		break;
	}

unhandled_exit:
	vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
	vcpu->run->internal.suberror = KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON;
	vcpu->run->internal.ndata = 2;
	vcpu->run->internal.data[0] = vp_enter_ret;
	vcpu->run->internal.data[1] = vcpu->arch.last_vmentry_cpu;
	return 0;
}

void tdx_get_exit_info(struct kvm_vcpu *vcpu, u32 *reason,
		u64 *info1, u64 *info2, u32 *intr_info, u32 *error_code)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);

	*reason = tdx->vt.exit_reason.full;
	if (*reason != -1u) {
		*info1 = vmx_get_exit_qual(vcpu);
		*info2 = tdx->ext_exit_qualification;
		*intr_info = vmx_get_intr_info(vcpu);
	} else {
		*info1 = 0;
		*info2 = 0;
		*intr_info = 0;
	}

	*error_code = 0;
}

bool tdx_has_emulated_msr(u32 index)
{
	switch (index) {
	case MSR_IA32_UCODE_REV:
	case MSR_IA32_ARCH_CAPABILITIES:
	case MSR_IA32_POWER_CTL:
	case MSR_IA32_CR_PAT:
	case MSR_MTRRcap:
	case MTRRphysBase_MSR(0) ... MSR_MTRRfix4K_F8000:
	case MSR_MTRRdefType:
	case MSR_IA32_TSC_DEADLINE:
	case MSR_IA32_MISC_ENABLE:
	case MSR_PLATFORM_INFO:
	case MSR_MISC_FEATURES_ENABLES:
	case MSR_IA32_APICBASE:
	case MSR_EFER:
	case MSR_IA32_FEAT_CTL:
	case MSR_IA32_MCG_CAP:
	case MSR_IA32_MCG_STATUS:
	case MSR_IA32_MCG_CTL:
	case MSR_IA32_MCG_EXT_CTL:
	case MSR_IA32_MC0_CTL ... MSR_IA32_MCx_CTL(KVM_MAX_MCE_BANKS) - 1:
	case MSR_IA32_MC0_CTL2 ... MSR_IA32_MCx_CTL2(KVM_MAX_MCE_BANKS) - 1:
		/* MSR_IA32_MCx_{CTL, STATUS, ADDR, MISC, CTL2} */
	case MSR_KVM_POLL_CONTROL:
		return true;
	case APIC_BASE_MSR ... APIC_BASE_MSR + 0xff:
		/*
		 * x2APIC registers that are virtualized by the CPU can't be
		 * emulated, KVM doesn't have access to the virtual APIC page.
		 */
		switch (index) {
		case X2APIC_MSR(APIC_TASKPRI):
		case X2APIC_MSR(APIC_PROCPRI):
		case X2APIC_MSR(APIC_EOI):
		case X2APIC_MSR(APIC_ISR) ... X2APIC_MSR(APIC_ISR + APIC_ISR_NR):
		case X2APIC_MSR(APIC_TMR) ... X2APIC_MSR(APIC_TMR + APIC_ISR_NR):
		case X2APIC_MSR(APIC_IRR) ... X2APIC_MSR(APIC_IRR + APIC_ISR_NR):
			return false;
		default:
			return true;
		}
	default:
		return false;
	}
}

static bool tdx_is_read_only_msr(u32 index)
{
	return  index == MSR_IA32_APICBASE || index == MSR_EFER ||
		index == MSR_IA32_FEAT_CTL;
}

int tdx_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr)
{
	switch (msr->index) {
	case MSR_IA32_FEAT_CTL:
		/*
		 * MCE and MCA are advertised via cpuid. Guest kernel could
		 * check if LMCE is enabled or not.
		 */
		msr->data = FEAT_CTL_LOCKED;
		if (vcpu->arch.mcg_cap & MCG_LMCE_P)
			msr->data |= FEAT_CTL_LMCE_ENABLED;
		return 0;
	case MSR_IA32_MCG_EXT_CTL:
		if (!msr->host_initiated && !(vcpu->arch.mcg_cap & MCG_LMCE_P))
			return 1;
		msr->data = vcpu->arch.mcg_ext_ctl;
		return 0;
	default:
		if (!tdx_has_emulated_msr(msr->index))
			return 1;

		return kvm_get_msr_common(vcpu, msr);
	}
}

int tdx_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr)
{
	switch (msr->index) {
	case MSR_IA32_MCG_EXT_CTL:
		if ((!msr->host_initiated && !(vcpu->arch.mcg_cap & MCG_LMCE_P)) ||
		    (msr->data & ~MCG_EXT_CTL_LMCE_EN))
			return 1;
		vcpu->arch.mcg_ext_ctl = msr->data;
		return 0;
	default:
		if (tdx_is_read_only_msr(msr->index))
			return 1;

		if (!tdx_has_emulated_msr(msr->index))
			return 1;

		return kvm_set_msr_common(vcpu, msr);
	}
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

/*
 * KVM reports guest physical address in CPUID.0x800000008.EAX[23:16], which is
 * similar to TDX's GPAW. Use this field as the interface for userspace to
 * configure the GPAW and EPT level for TDs.
 *
 * Only values 48 and 52 are supported. Value 52 means GPAW-52 and EPT level
 * 5, Value 48 means GPAW-48 and EPT level 4. For value 48, GPAW-48 is always
 * supported. Value 52 is only supported when the platform supports 5 level
 * EPT.
 */
static int setup_tdparams_eptp_controls(struct kvm_cpuid2 *cpuid,
					struct td_params *td_params)
{
	const struct kvm_cpuid_entry2 *entry;
	int guest_pa;

	entry = kvm_find_cpuid_entry2(cpuid->entries, cpuid->nent, 0x80000008, 0);
	if (!entry)
		return -EINVAL;

	guest_pa = tdx_get_guest_phys_addr_bits(entry->eax);

	if (guest_pa != 48 && guest_pa != 52)
		return -EINVAL;

	if (guest_pa == 52 && !cpu_has_vmx_ept_5levels())
		return -EINVAL;

	td_params->eptp_controls = VMX_EPTP_MT_WB;
	if (guest_pa == 52) {
		td_params->eptp_controls |= VMX_EPTP_PWL_5;
		td_params->config_flags |= TDX_CONFIG_FLAGS_MAX_GPAW;
	} else {
		td_params->eptp_controls |= VMX_EPTP_PWL_4;
	}

	return 0;
}

static int setup_tdparams_cpuids(struct kvm_cpuid2 *cpuid,
				 struct td_params *td_params)
{
	const struct tdx_sys_info_td_conf *td_conf = &tdx_sysinfo->td_conf;
	const struct kvm_cpuid_entry2 *entry;
	struct tdx_cpuid_value *value;
	int i, copy_cnt = 0;

	/*
	 * td_params.cpuid_values: The number and the order of cpuid_value must
	 * be same to the one of struct tdsysinfo.{num_cpuid_config, cpuid_configs}
	 * It's assumed that td_params was zeroed.
	 */
	for (i = 0; i < td_conf->num_cpuid_config; i++) {
		struct kvm_cpuid_entry2 tmp;

		td_init_cpuid_entry2(&tmp, i);

		entry = kvm_find_cpuid_entry2(cpuid->entries, cpuid->nent,
					      tmp.function, tmp.index);
		if (!entry)
			continue;

		if (tdx_unsupported_cpuid(entry))
			return -EINVAL;

		copy_cnt++;

		value = &td_params->cpuid_values[i];
		value->eax = entry->eax;
		value->ebx = entry->ebx;
		value->ecx = entry->ecx;
		value->edx = entry->edx;

		/*
		 * TDX module does not accept nonzero bits 16..23 for the
		 * CPUID[0x80000008].EAX, see setup_tdparams_eptp_controls().
		 */
		if (tmp.function == 0x80000008)
			value->eax = tdx_set_guest_phys_addr_bits(value->eax, 0);
	}

	/*
	 * Rely on the TDX module to reject invalid configuration, but it can't
	 * check of leafs that don't have a proper slot in td_params->cpuid_values
	 * to stick then. So fail if there were entries that didn't get copied to
	 * td_params.
	 */
	if (copy_cnt != cpuid->nent)
		return -EINVAL;

	return 0;
}

static int setup_tdparams(struct kvm *kvm, struct td_params *td_params,
			struct kvm_tdx_init_vm *init_vm)
{
	const struct tdx_sys_info_td_conf *td_conf = &tdx_sysinfo->td_conf;
	struct kvm_cpuid2 *cpuid = &init_vm->cpuid;
	int ret;

	if (kvm->created_vcpus)
		return -EBUSY;

	if (init_vm->attributes & ~tdx_get_supported_attrs(td_conf))
		return -EINVAL;

	if (init_vm->xfam & ~tdx_get_supported_xfam(td_conf))
		return -EINVAL;

	td_params->max_vcpus = kvm->max_vcpus;
	td_params->attributes = init_vm->attributes | td_conf->attributes_fixed1;
	td_params->xfam = init_vm->xfam | td_conf->xfam_fixed1;

	td_params->config_flags = TDX_CONFIG_FLAGS_NO_RBP_MOD;
	td_params->tsc_frequency = TDX_TSC_KHZ_TO_25MHZ(kvm->arch.default_tsc_khz);

	ret = setup_tdparams_eptp_controls(cpuid, td_params);
	if (ret)
		return ret;

	ret = setup_tdparams_cpuids(cpuid, td_params);
	if (ret)
		return ret;

#define MEMCPY_SAME_SIZE(dst, src)				\
	do {							\
		BUILD_BUG_ON(sizeof(dst) != sizeof(src));	\
		memcpy((dst), (src), sizeof(dst));		\
	} while (0)

	MEMCPY_SAME_SIZE(td_params->mrconfigid, init_vm->mrconfigid);
	MEMCPY_SAME_SIZE(td_params->mrowner, init_vm->mrowner);
	MEMCPY_SAME_SIZE(td_params->mrownerconfig, init_vm->mrownerconfig);

	return 0;
}

static int __tdx_td_init(struct kvm *kvm, struct td_params *td_params,
			 u64 *seamcall_err)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	cpumask_var_t packages;
	struct page **tdcs_pages = NULL;
	struct page *tdr_page;
	int ret, i;
	u64 err, rcx;

	*seamcall_err = 0;
	ret = tdx_guest_keyid_alloc();
	if (ret < 0)
		return ret;
	kvm_tdx->hkid = ret;
	kvm_tdx->misc_cg = get_current_misc_cg();
	ret = misc_cg_try_charge(MISC_CG_RES_TDX, kvm_tdx->misc_cg, 1);
	if (ret)
		goto free_hkid;

	ret = -ENOMEM;

	atomic_inc(&nr_configured_hkid);

	tdr_page = alloc_page(GFP_KERNEL);
	if (!tdr_page)
		goto free_hkid;

	kvm_tdx->td.tdcs_nr_pages = tdx_sysinfo->td_ctrl.tdcs_base_size / PAGE_SIZE;
	/* TDVPS = TDVPR(4K page) + TDCX(multiple 4K pages), -1 for TDVPR. */
	kvm_tdx->td.tdcx_nr_pages = tdx_sysinfo->td_ctrl.tdvps_base_size / PAGE_SIZE - 1;
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

	err = tdh_mng_init(&kvm_tdx->td, __pa(td_params), &rcx);
	if ((err & TDX_SEAMCALL_STATUS_MASK) == TDX_OPERAND_INVALID) {
		/*
		 * Because a user gives operands, don't warn.
		 * Return a hint to the user because it's sometimes hard for the
		 * user to figure out which operand is invalid.  SEAMCALL status
		 * code includes which operand caused invalid operand error.
		 */
		*seamcall_err = err;
		ret = -EINVAL;
		goto teardown;
	} else if (WARN_ON_ONCE(err)) {
		pr_tdx_error_1(TDH_MNG_INIT, err, rcx);
		ret = -EIO;
		goto teardown;
	}

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

static u64 tdx_td_metadata_field_read(struct kvm_tdx *tdx, u64 field_id,
				      u64 *data)
{
	u64 err;

	err = tdh_mng_rd(&tdx->td, field_id, data);

	return err;
}

#define TDX_MD_UNREADABLE_LEAF_MASK	GENMASK(30, 7)
#define TDX_MD_UNREADABLE_SUBLEAF_MASK	GENMASK(31, 7)

static int tdx_read_cpuid(struct kvm_vcpu *vcpu, u32 leaf, u32 sub_leaf,
			  bool sub_leaf_set, int *entry_index,
			  struct kvm_cpuid_entry2 *out)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(vcpu->kvm);
	u64 field_id = TD_MD_FIELD_ID_CPUID_VALUES;
	u64 ebx_eax, edx_ecx;
	u64 err = 0;

	if (sub_leaf > 0b1111111)
		return -EINVAL;

	if (*entry_index >= KVM_MAX_CPUID_ENTRIES)
		return -EINVAL;

	if (leaf & TDX_MD_UNREADABLE_LEAF_MASK ||
	    sub_leaf & TDX_MD_UNREADABLE_SUBLEAF_MASK)
		return -EINVAL;

	/*
	 * bit 23:17, REVSERVED: reserved, must be 0;
	 * bit 16,    LEAF_31: leaf number bit 31;
	 * bit 15:9,  LEAF_6_0: leaf number bits 6:0, leaf bits 30:7 are
	 *                      implicitly 0;
	 * bit 8,     SUBLEAF_NA: sub-leaf not applicable flag;
	 * bit 7:1,   SUBLEAF_6_0: sub-leaf number bits 6:0. If SUBLEAF_NA is 1,
	 *                         the SUBLEAF_6_0 is all-1.
	 *                         sub-leaf bits 31:7 are implicitly 0;
	 * bit 0,     ELEMENT_I: Element index within field;
	 */
	field_id |= ((leaf & 0x80000000) ? 1 : 0) << 16;
	field_id |= (leaf & 0x7f) << 9;
	if (sub_leaf_set)
		field_id |= (sub_leaf & 0x7f) << 1;
	else
		field_id |= 0x1fe;

	err = tdx_td_metadata_field_read(kvm_tdx, field_id, &ebx_eax);
	if (err) //TODO check for specific errors
		goto err_out;

	out->eax = (u32) ebx_eax;
	out->ebx = (u32) (ebx_eax >> 32);

	field_id++;
	err = tdx_td_metadata_field_read(kvm_tdx, field_id, &edx_ecx);
	/*
	 * It's weird that reading edx_ecx fails while reading ebx_eax
	 * succeeded.
	 */
	if (WARN_ON_ONCE(err))
		goto err_out;

	out->ecx = (u32) edx_ecx;
	out->edx = (u32) (edx_ecx >> 32);

	out->function = leaf;
	out->index = sub_leaf;
	out->flags |= sub_leaf_set ? KVM_CPUID_FLAG_SIGNIFCANT_INDEX : 0;

	/*
	 * Work around missing support on old TDX modules, fetch
	 * guest maxpa from gfn_direct_bits.
	 */
	if (leaf == 0x80000008) {
		gpa_t gpa_bits = gfn_to_gpa(kvm_gfn_direct_bits(vcpu->kvm));
		unsigned int g_maxpa = __ffs(gpa_bits) + 1;

		out->eax = tdx_set_guest_phys_addr_bits(out->eax, g_maxpa);
	}

	(*entry_index)++;

	return 0;

err_out:
	out->eax = 0;
	out->ebx = 0;
	out->ecx = 0;
	out->edx = 0;

	return -EIO;
}

static int tdx_td_init(struct kvm *kvm, struct kvm_tdx_cmd *cmd)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	struct kvm_tdx_init_vm *init_vm;
	struct td_params *td_params = NULL;
	int ret;

	BUILD_BUG_ON(sizeof(*init_vm) != 256 + sizeof_field(struct kvm_tdx_init_vm, cpuid));
	BUILD_BUG_ON(sizeof(struct td_params) != 1024);

	if (kvm_tdx->state != TD_STATE_UNINITIALIZED)
		return -EINVAL;

	if (cmd->flags)
		return -EINVAL;

	init_vm = kmalloc(sizeof(*init_vm) +
			  sizeof(init_vm->cpuid.entries[0]) * KVM_MAX_CPUID_ENTRIES,
			  GFP_KERNEL);
	if (!init_vm)
		return -ENOMEM;

	if (copy_from_user(init_vm, u64_to_user_ptr(cmd->data), sizeof(*init_vm))) {
		ret = -EFAULT;
		goto out;
	}

	if (init_vm->cpuid.nent > KVM_MAX_CPUID_ENTRIES) {
		ret = -E2BIG;
		goto out;
	}

	if (copy_from_user(init_vm->cpuid.entries,
			   u64_to_user_ptr(cmd->data) + sizeof(*init_vm),
			   flex_array_size(init_vm, cpuid.entries, init_vm->cpuid.nent))) {
		ret = -EFAULT;
		goto out;
	}

	if (memchr_inv(init_vm->reserved, 0, sizeof(init_vm->reserved))) {
		ret = -EINVAL;
		goto out;
	}

	if (init_vm->cpuid.padding) {
		ret = -EINVAL;
		goto out;
	}

	td_params = kzalloc(sizeof(struct td_params), GFP_KERNEL);
	if (!td_params) {
		ret = -ENOMEM;
		goto out;
	}

	ret = setup_tdparams(kvm, td_params, init_vm);
	if (ret)
		goto out;

	ret = __tdx_td_init(kvm, td_params, &cmd->hw_error);
	if (ret)
		goto out;

	kvm_tdx->tsc_offset = td_tdcs_exec_read64(kvm_tdx, TD_TDCS_EXEC_TSC_OFFSET);
	kvm_tdx->tsc_multiplier = td_tdcs_exec_read64(kvm_tdx, TD_TDCS_EXEC_TSC_MULTIPLIER);
	kvm_tdx->attributes = td_params->attributes;
	kvm_tdx->xfam = td_params->xfam;

	if (td_params->config_flags & TDX_CONFIG_FLAGS_MAX_GPAW)
		kvm->arch.gfn_direct_bits = TDX_SHARED_BIT_PWL_5;
	else
		kvm->arch.gfn_direct_bits = TDX_SHARED_BIT_PWL_4;

	kvm_tdx->state = TD_STATE_INITIALIZED;
out:
	/* kfree() accepts NULL. */
	kfree(init_vm);
	kfree(td_params);

	return ret;
}

void tdx_flush_tlb_current(struct kvm_vcpu *vcpu)
{
	/*
	 * flush_tlb_current() is invoked when the first time for the vcpu to
	 * run or when root of shared EPT is invalidated.
	 * KVM only needs to flush shared EPT because the TDX module handles TLB
	 * invalidation for private EPT in tdh_vp_enter();
	 *
	 * A single context invalidation for shared EPT can be performed here.
	 * However, this single context invalidation requires the private EPTP
	 * rather than the shared EPTP to flush shared EPT, as shared EPT uses
	 * private EPTP as its ASID for TLB invalidation.
	 *
	 * To avoid reading back private EPTP, perform a global invalidation for
	 * shared EPT instead to keep this function simple.
	 */
	ept_sync_global();
}

void tdx_flush_tlb_all(struct kvm_vcpu *vcpu)
{
	/*
	 * TDX has called tdx_track() in tdx_sept_remove_private_spte() to
	 * ensure that private EPT will be flushed on the next TD enter. No need
	 * to call tdx_track() here again even when this callback is a result of
	 * zapping private EPT.
	 *
	 * Due to the lack of the context to determine which EPT has been
	 * affected by zapping, invoke invept() directly here for both shared
	 * EPT and private EPT for simplicity, though it's not necessary for
	 * private EPT.
	 */
	ept_sync_global();
}

static int tdx_td_finalize(struct kvm *kvm, struct kvm_tdx_cmd *cmd)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);

	guard(mutex)(&kvm->slots_lock);

	if (!is_hkid_assigned(kvm_tdx) || kvm_tdx->state == TD_STATE_RUNNABLE)
		return -EINVAL;
	/*
	 * Pages are pending for KVM_TDX_INIT_MEM_REGION to issue
	 * TDH.MEM.PAGE.ADD().
	 */
	if (atomic64_read(&kvm_tdx->nr_premapped))
		return -EINVAL;

	cmd->hw_error = tdh_mr_finalize(&kvm_tdx->td);
	if (tdx_operand_busy(cmd->hw_error))
		return -EBUSY;
	if (KVM_BUG_ON(cmd->hw_error, kvm)) {
		pr_tdx_error(TDH_MR_FINALIZE, cmd->hw_error);
		return -EIO;
	}

	kvm_tdx->state = TD_STATE_RUNNABLE;
	/* TD_STATE_RUNNABLE must be set before 'pre_fault_allowed' */
	smp_wmb();
	kvm->arch.pre_fault_allowed = true;
	return 0;
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
	case KVM_TDX_INIT_VM:
		r = tdx_td_init(kvm, &tdx_cmd);
		break;
	case KVM_TDX_FINALIZE_VM:
		r = tdx_td_finalize(kvm, &tdx_cmd);
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

/* VMM can pass one 64bit auxiliary data to vcpu via RCX for guest BIOS. */
static int tdx_td_vcpu_init(struct kvm_vcpu *vcpu, u64 vcpu_rcx)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(vcpu->kvm);
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	struct page *page;
	int ret, i;
	u64 err;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	tdx->vp.tdvpr_page = page;

	tdx->vp.tdcx_pages = kcalloc(kvm_tdx->td.tdcx_nr_pages, sizeof(*tdx->vp.tdcx_pages),
			       	     GFP_KERNEL);
	if (!tdx->vp.tdcx_pages) {
		ret = -ENOMEM;
		goto free_tdvpr;
	}

	for (i = 0; i < kvm_tdx->td.tdcx_nr_pages; i++) {
		page = alloc_page(GFP_KERNEL);
		if (!page) {
			ret = -ENOMEM;
			goto free_tdcx;
		}
		tdx->vp.tdcx_pages[i] = page;
	}

	err = tdh_vp_create(&kvm_tdx->td, &tdx->vp);
	if (KVM_BUG_ON(err, vcpu->kvm)) {
		ret = -EIO;
		pr_tdx_error(TDH_VP_CREATE, err);
		goto free_tdcx;
	}

	for (i = 0; i < kvm_tdx->td.tdcx_nr_pages; i++) {
		err = tdh_vp_addcx(&tdx->vp, tdx->vp.tdcx_pages[i]);
		if (KVM_BUG_ON(err, vcpu->kvm)) {
			pr_tdx_error(TDH_VP_ADDCX, err);
			/*
			 * Pages already added are reclaimed by the vcpu_free
			 * method, but the rest are freed here.
			 */
			for (; i < kvm_tdx->td.tdcx_nr_pages; i++) {
				__free_page(tdx->vp.tdcx_pages[i]);
				tdx->vp.tdcx_pages[i] = NULL;
			}
			return -EIO;
		}
	}

	err = tdh_vp_init(&tdx->vp, vcpu_rcx, vcpu->vcpu_id);
	if (KVM_BUG_ON(err, vcpu->kvm)) {
		pr_tdx_error(TDH_VP_INIT, err);
		return -EIO;
	}

	vcpu->arch.mp_state = KVM_MP_STATE_RUNNABLE;

	return 0;

free_tdcx:
	for (i = 0; i < kvm_tdx->td.tdcx_nr_pages; i++) {
		if (tdx->vp.tdcx_pages[i])
			__free_page(tdx->vp.tdcx_pages[i]);
		tdx->vp.tdcx_pages[i] = NULL;
	}
	kfree(tdx->vp.tdcx_pages);
	tdx->vp.tdcx_pages = NULL;

free_tdvpr:
	if (tdx->vp.tdvpr_page)
		__free_page(tdx->vp.tdvpr_page);
	tdx->vp.tdvpr_page = 0;

	return ret;
}

/* Sometimes reads multipple subleafs. Return how many enties were written. */
static int tdx_vcpu_get_cpuid_leaf(struct kvm_vcpu *vcpu, u32 leaf, int *entry_index,
				   struct kvm_cpuid_entry2 *output_e)
{
	int sub_leaf = 0;
	int ret;

	/* First try without a subleaf */
	ret = tdx_read_cpuid(vcpu, leaf, 0, false, entry_index, output_e);

	/* If success, or invalid leaf, just give up */
	if (ret != -EIO)
		return ret;

	/*
	 * If the try without a subleaf failed, try reading subleafs until
	 * failure. The TDX module only supports 6 bits of subleaf index.
	 */
	while (1) {
		/* Keep reading subleafs until there is a failure. */
		if (tdx_read_cpuid(vcpu, leaf, sub_leaf, true, entry_index, output_e))
			return !sub_leaf;

		sub_leaf++;
		output_e++;
	}

	return 0;
}

static int tdx_vcpu_get_cpuid(struct kvm_vcpu *vcpu, struct kvm_tdx_cmd *cmd)
{
	struct kvm_cpuid2 __user *output, *td_cpuid;
	int r = 0, i = 0, leaf;
	u32 level;

	output = u64_to_user_ptr(cmd->data);
	td_cpuid = kzalloc(sizeof(*td_cpuid) +
			sizeof(output->entries[0]) * KVM_MAX_CPUID_ENTRIES,
			GFP_KERNEL);
	if (!td_cpuid)
		return -ENOMEM;

	if (copy_from_user(td_cpuid, output, sizeof(*output))) {
		r = -EFAULT;
		goto out;
	}

	/* Read max CPUID for normal range */
	if (tdx_vcpu_get_cpuid_leaf(vcpu, 0, &i, &td_cpuid->entries[i])) {
		r = -EIO;
		goto out;
	}
	level = td_cpuid->entries[0].eax;

	for (leaf = 1; leaf <= level; leaf++)
		tdx_vcpu_get_cpuid_leaf(vcpu, leaf, &i, &td_cpuid->entries[i]);

	/* Read max CPUID for extended range */
	if (tdx_vcpu_get_cpuid_leaf(vcpu, 0x80000000, &i, &td_cpuid->entries[i])) {
		r = -EIO;
		goto out;
	}
	level = td_cpuid->entries[i - 1].eax;

	for (leaf = 0x80000001; leaf <= level; leaf++)
		tdx_vcpu_get_cpuid_leaf(vcpu, leaf, &i, &td_cpuid->entries[i]);

	if (td_cpuid->nent < i)
		r = -E2BIG;
	td_cpuid->nent = i;

	if (copy_to_user(output, td_cpuid, sizeof(*output))) {
		r = -EFAULT;
		goto out;
	}

	if (r == -E2BIG)
		goto out;

	if (copy_to_user(output->entries, td_cpuid->entries,
			 td_cpuid->nent * sizeof(struct kvm_cpuid_entry2)))
		r = -EFAULT;

out:
	kfree(td_cpuid);

	return r;
}

static int tdx_vcpu_init(struct kvm_vcpu *vcpu, struct kvm_tdx_cmd *cmd)
{
	u64 apic_base;
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	int ret;

	if (cmd->flags)
		return -EINVAL;

	if (tdx->state != VCPU_TD_STATE_UNINITIALIZED)
		return -EINVAL;

	/*
	 * TDX requires X2APIC, userspace is responsible for configuring guest
	 * CPUID accordingly.
	 */
	apic_base = APIC_DEFAULT_PHYS_BASE | LAPIC_MODE_X2APIC |
		(kvm_vcpu_is_reset_bsp(vcpu) ? MSR_IA32_APICBASE_BSP : 0);
	if (kvm_apic_set_base(vcpu, apic_base, true))
		return -EINVAL;

	ret = tdx_td_vcpu_init(vcpu, (u64)cmd->data);
	if (ret)
		return ret;

	td_vmcs_write16(tdx, POSTED_INTR_NV, POSTED_INTR_VECTOR);
	td_vmcs_write64(tdx, POSTED_INTR_DESC_ADDR, __pa(&tdx->vt.pi_desc));
	td_vmcs_setbit32(tdx, PIN_BASED_VM_EXEC_CONTROL, PIN_BASED_POSTED_INTR);

	tdx->state = VCPU_TD_STATE_INITIALIZED;

	return 0;
}

void tdx_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event)
{
	/*
	 * Yell on INIT, as TDX doesn't support INIT, i.e. KVM should drop all
	 * INIT events.
	 *
	 * Defer initializing vCPU for RESET state until KVM_TDX_INIT_VCPU, as
	 * userspace needs to define the vCPU model before KVM can initialize
	 * vCPU state, e.g. to enable x2APIC.
	 */
	WARN_ON_ONCE(init_event);
}

struct tdx_gmem_post_populate_arg {
	struct kvm_vcpu *vcpu;
	__u32 flags;
};

static int tdx_gmem_post_populate(struct kvm *kvm, gfn_t gfn, kvm_pfn_t pfn,
				  void __user *src, int order, void *_arg)
{
	u64 error_code = PFERR_GUEST_FINAL_MASK | PFERR_PRIVATE_ACCESS;
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	struct tdx_gmem_post_populate_arg *arg = _arg;
	struct kvm_vcpu *vcpu = arg->vcpu;
	gpa_t gpa = gfn_to_gpa(gfn);
	u8 level = PG_LEVEL_4K;
	struct page *src_page;
	int ret, i;
	u64 err, entry, level_state;

	/*
	 * Get the source page if it has been faulted in. Return failure if the
	 * source page has been swapped out or unmapped in primary memory.
	 */
	ret = get_user_pages_fast((unsigned long)src, 1, 0, &src_page);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -ENOMEM;

	ret = kvm_tdp_map_page(vcpu, gpa, error_code, &level);
	if (ret < 0)
		goto out;

	/*
	 * The private mem cannot be zapped after kvm_tdp_map_page()
	 * because all paths are covered by slots_lock and the
	 * filemap invalidate lock.  Check that they are indeed enough.
	 */
	if (IS_ENABLED(CONFIG_KVM_PROVE_MMU)) {
		scoped_guard(read_lock, &kvm->mmu_lock) {
			if (KVM_BUG_ON(!kvm_tdp_mmu_gpa_is_mapped(vcpu, gpa), kvm)) {
				ret = -EIO;
				goto out;
			}
		}
	}

	ret = 0;
	err = tdh_mem_page_add(&kvm_tdx->td, gpa, pfn_to_page(pfn),
			       src_page, &entry, &level_state);
	if (err) {
		ret = unlikely(tdx_operand_busy(err)) ? -EBUSY : -EIO;
		goto out;
	}

	if (!KVM_BUG_ON(!atomic64_read(&kvm_tdx->nr_premapped), kvm))
		atomic64_dec(&kvm_tdx->nr_premapped);

	if (arg->flags & KVM_TDX_MEASURE_MEMORY_REGION) {
		for (i = 0; i < PAGE_SIZE; i += TDX_EXTENDMR_CHUNKSIZE) {
			err = tdh_mr_extend(&kvm_tdx->td, gpa + i, &entry,
					    &level_state);
			if (err) {
				ret = -EIO;
				break;
			}
		}
	}

out:
	put_page(src_page);
	return ret;
}

static int tdx_vcpu_init_mem_region(struct kvm_vcpu *vcpu, struct kvm_tdx_cmd *cmd)
{
	struct vcpu_tdx *tdx = to_tdx(vcpu);
	struct kvm *kvm = vcpu->kvm;
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(kvm);
	struct kvm_tdx_init_mem_region region;
	struct tdx_gmem_post_populate_arg arg;
	long gmem_ret;
	int ret;

	if (tdx->state != VCPU_TD_STATE_INITIALIZED)
		return -EINVAL;

	guard(mutex)(&kvm->slots_lock);

	/* Once TD is finalized, the initial guest memory is fixed. */
	if (kvm_tdx->state == TD_STATE_RUNNABLE)
		return -EINVAL;

	if (cmd->flags & ~KVM_TDX_MEASURE_MEMORY_REGION)
		return -EINVAL;

	if (copy_from_user(&region, u64_to_user_ptr(cmd->data), sizeof(region)))
		return -EFAULT;

	if (!PAGE_ALIGNED(region.source_addr) || !PAGE_ALIGNED(region.gpa) ||
	    !region.nr_pages ||
	    region.gpa + (region.nr_pages << PAGE_SHIFT) <= region.gpa ||
	    !vt_is_tdx_private_gpa(kvm, region.gpa) ||
	    !vt_is_tdx_private_gpa(kvm, region.gpa + (region.nr_pages << PAGE_SHIFT) - 1))
		return -EINVAL;

	kvm_mmu_reload(vcpu);
	ret = 0;
	while (region.nr_pages) {
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		arg = (struct tdx_gmem_post_populate_arg) {
			.vcpu = vcpu,
			.flags = cmd->flags,
		};
		gmem_ret = kvm_gmem_populate(kvm, gpa_to_gfn(region.gpa),
					     u64_to_user_ptr(region.source_addr),
					     1, tdx_gmem_post_populate, &arg);
		if (gmem_ret < 0) {
			ret = gmem_ret;
			break;
		}

		if (gmem_ret != 1) {
			ret = -EIO;
			break;
		}

		region.source_addr += PAGE_SIZE;
		region.gpa += PAGE_SIZE;
		region.nr_pages--;

		cond_resched();
	}

	if (copy_to_user(u64_to_user_ptr(cmd->data), &region, sizeof(region)))
		ret = -EFAULT;
	return ret;
}

int tdx_vcpu_ioctl(struct kvm_vcpu *vcpu, void __user *argp)
{
	struct kvm_tdx *kvm_tdx = to_kvm_tdx(vcpu->kvm);
	struct kvm_tdx_cmd cmd;
	int ret;

	if (!is_hkid_assigned(kvm_tdx) || kvm_tdx->state == TD_STATE_RUNNABLE)
		return -EINVAL;

	if (copy_from_user(&cmd, argp, sizeof(cmd)))
		return -EFAULT;

	if (cmd.hw_error)
		return -EINVAL;

	switch (cmd.id) {
	case KVM_TDX_INIT_VCPU:
		ret = tdx_vcpu_init(vcpu, &cmd);
		break;
	case KVM_TDX_INIT_MEM_REGION:
		ret = tdx_vcpu_init_mem_region(vcpu, &cmd);
		break;
	case KVM_TDX_GET_CPUID:
		ret = tdx_vcpu_get_cpuid(vcpu, &cmd);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int tdx_gmem_private_max_mapping_level(struct kvm *kvm, kvm_pfn_t pfn)
{
	return PG_LEVEL_4K;
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

static int tdx_offline_cpu(unsigned int cpu)
{
	int i;

	/* No TD is running.  Allow any cpu to be offline. */
	if (!atomic_read(&nr_configured_hkid))
		return 0;

	/*
	 * In order to reclaim TDX HKID, (i.e. when deleting guest TD), need to
	 * call TDH.PHYMEM.PAGE.WBINVD on all packages to program all memory
	 * controller with pconfig.  If we have active TDX HKID, refuse to
	 * offline the last online cpu.
	 */
	for_each_online_cpu(i) {
		/*
		 * Found another online cpu on the same package.
		 * Allow to offline.
		 */
		if (i != cpu && topology_physical_package_id(i) ==
				topology_physical_package_id(cpu))
			return 0;
	}

	/*
	 * This is the last cpu of this package.  Don't offline it.
	 *
	 * Because it's hard for human operator to understand the
	 * reason, warn it.
	 */
#define MSG_ALLPKG_ONLINE \
	"TDX requires all packages to have an online CPU. Delete all TDs in order to offline all CPUs of a package.\n"
	pr_warn_ratelimited(MSG_ALLPKG_ONLINE);
	return -EBUSY;
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
					 tdx_online_cpu, tdx_offline_cpu);
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
	const struct tdx_sys_info_td_conf *td_conf;
	int r, i;

	for (i = 0; i < ARRAY_SIZE(tdx_uret_msrs); i++) {
		/*
		 * Check if MSRs (tdx_uret_msrs) can be saved/restored
		 * before returning to user space.
		 *
		 * this_cpu_ptr(user_return_msrs)->registered isn't checked
		 * because the registration is done at vcpu runtime by
		 * tdx_user_return_msr_update_cache().
		 */
		tdx_uret_msrs[i].slot = kvm_find_user_return_msr(tdx_uret_msrs[i].msr);
		if (tdx_uret_msrs[i].slot == -1) {
			/* If any MSR isn't supported, it is a KVM bug */
			pr_err("MSR %x isn't included by kvm_find_user_return_msr\n",
				tdx_uret_msrs[i].msr);
			return -EIO;
		}
	}

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
	 * TDX has its own limit of maximum vCPUs it can support for all
	 * TDX guests in addition to KVM_MAX_VCPUS.  Userspace needs to
	 * query TDX guest's maximum vCPUs by checking KVM_CAP_MAX_VCPU
	 * extension on per-VM basis.
	 *
	 * TDX module reports such limit via the MAX_VCPU_PER_TD global
	 * metadata.  Different modules may report different values.
	 * Some old module may also not support this metadata (in which
	 * case this limit is U16_MAX).
	 *
	 * In practice, the reported value reflects the maximum logical
	 * CPUs that ALL the platforms that the module supports can
	 * possibly have.
	 *
	 * Simply forwarding the MAX_VCPU_PER_TD to userspace could
	 * result in an unpredictable ABI.  KVM instead always advertise
	 * the number of logical CPUs the platform has as the maximum
	 * vCPUs for TDX guests.
	 *
	 * Make sure MAX_VCPU_PER_TD reported by TDX module is not
	 * smaller than the number of logical CPUs, otherwise KVM will
	 * report an unsupported value to userspace.
	 *
	 * Note, a platform with TDX enabled in the BIOS cannot support
	 * physical CPU hotplug, and TDX requires the BIOS has marked
	 * all logical CPUs in MADT table as enabled.  Just use
	 * num_present_cpus() for the number of logical CPUs.
	 */
	td_conf = &tdx_sysinfo->td_conf;
	if (td_conf->max_vcpus_per_td < num_present_cpus()) {
		pr_err("Disable TDX: MAX_VCPU_PER_TD (%u) smaller than number of logical CPUs (%u).\n",
				td_conf->max_vcpus_per_td, num_present_cpus());
		r = -EINVAL;
		goto get_sysinfo_err;
	}

	if (misc_cg_set_capacity(MISC_CG_RES_TDX, tdx_get_nr_guest_keyids())) {
		r = -EINVAL;
		goto get_sysinfo_err;
	}

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
		misc_cg_set_capacity(MISC_CG_RES_TDX, 0);
		__tdx_cleanup();
		kvm_disable_virtualization();
	}
}

int __init tdx_bringup(void)
{
	int r, i;

	/* tdx_disable_virtualization_cpu() uses associated_tdvcpus. */
	for_each_possible_cpu(i)
		INIT_LIST_HEAD(&per_cpu(associated_tdvcpus, i));

	if (!enable_tdx)
		return 0;

	if (!enable_ept) {
		pr_err("EPT is required for TDX\n");
		goto success_disable_tdx;
	}

	if (!tdp_mmu_enabled || !enable_mmio_caching || !enable_ept_ad_bits) {
		pr_err("TDP MMU and MMIO caching and EPT A/D bit is required for TDX\n");
		goto success_disable_tdx;
	}

	if (!enable_apicv) {
		pr_err("APICv is required for TDX\n");
		goto success_disable_tdx;
	}

	if (!cpu_feature_enabled(X86_FEATURE_OSXSAVE)) {
		pr_err("tdx: OSXSAVE is required for TDX\n");
		goto success_disable_tdx;
	}

	if (!cpu_feature_enabled(X86_FEATURE_MOVDIR64B)) {
		pr_err("tdx: MOVDIR64B is required for TDX\n");
		goto success_disable_tdx;
	}

	if (!cpu_feature_enabled(X86_FEATURE_SELFSNOOP)) {
		pr_err("Self-snoop is required for TDX\n");
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
