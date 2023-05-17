// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2021 Intel Corporation. */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/sgx.h>

#include "cpuid.h"
#include "kvm_cache_regs.h"
#include "nested.h"
#include "sgx.h"
#include "vmx.h"
#include "x86.h"

bool __read_mostly enable_sgx = 1;
module_param_named(sgx, enable_sgx, bool, 0444);

/* Initial value of guest's virtual SGX_LEPUBKEYHASHn MSRs */
static u64 sgx_pubkey_hash[4] __ro_after_init;

/*
 * ENCLS's memory operands use a fixed segment (DS) and a fixed
 * address size based on the mode.  Related prefixes are ignored.
 */
static int sgx_get_encls_gva(struct kvm_vcpu *vcpu, unsigned long offset,
			     int size, int alignment, gva_t *gva)
{
	struct kvm_segment s;
	bool fault;

	/* Skip vmcs.GUEST_DS retrieval for 64-bit mode to avoid VMREADs. */
	*gva = offset;
	if (!is_64_bit_mode(vcpu)) {
		vmx_get_segment(vcpu, &s, VCPU_SREG_DS);
		*gva += s.base;
	}

	if (!IS_ALIGNED(*gva, alignment)) {
		fault = true;
	} else if (likely(is_64_bit_mode(vcpu))) {
		fault = is_noncanonical_address(*gva, vcpu);
	} else {
		*gva &= 0xffffffff;
		fault = (s.unusable) ||
			(s.type != 2 && s.type != 3) ||
			(*gva > s.limit) ||
			((s.base != 0 || s.limit != 0xffffffff) &&
			(((u64)*gva + size - 1) > s.limit + 1));
	}
	if (fault)
		kvm_inject_gp(vcpu, 0);
	return fault ? -EINVAL : 0;
}

static void sgx_handle_emulation_failure(struct kvm_vcpu *vcpu, u64 addr,
					 unsigned int size)
{
	uint64_t data[2] = { addr, size };

	__kvm_prepare_emulation_failure_exit(vcpu, data, ARRAY_SIZE(data));
}

static int sgx_read_hva(struct kvm_vcpu *vcpu, unsigned long hva, void *data,
			unsigned int size)
{
	if (__copy_from_user(data, (void __user *)hva, size)) {
		sgx_handle_emulation_failure(vcpu, hva, size);
		return -EFAULT;
	}

	return 0;
}

static int sgx_gva_to_gpa(struct kvm_vcpu *vcpu, gva_t gva, bool write,
			  gpa_t *gpa)
{
	struct x86_exception ex;

	if (write)
		*gpa = kvm_mmu_gva_to_gpa_write(vcpu, gva, &ex);
	else
		*gpa = kvm_mmu_gva_to_gpa_read(vcpu, gva, &ex);

	if (*gpa == INVALID_GPA) {
		kvm_inject_emulated_page_fault(vcpu, &ex);
		return -EFAULT;
	}

	return 0;
}

static int sgx_gpa_to_hva(struct kvm_vcpu *vcpu, gpa_t gpa, unsigned long *hva)
{
	*hva = kvm_vcpu_gfn_to_hva(vcpu, PFN_DOWN(gpa));
	if (kvm_is_error_hva(*hva)) {
		sgx_handle_emulation_failure(vcpu, gpa, 1);
		return -EFAULT;
	}

	*hva |= gpa & ~PAGE_MASK;

	return 0;
}

static int sgx_inject_fault(struct kvm_vcpu *vcpu, gva_t gva, int trapnr)
{
	struct x86_exception ex;

	/*
	 * A non-EPCM #PF indicates a bad userspace HVA.  This *should* check
	 * for PFEC.SGX and not assume any #PF on SGX2 originated in the EPC,
	 * but the error code isn't (yet) plumbed through the ENCLS helpers.
	 */
	if (trapnr == PF_VECTOR && !boot_cpu_has(X86_FEATURE_SGX2)) {
		kvm_prepare_emulation_failure_exit(vcpu);
		return 0;
	}

	/*
	 * If the guest thinks it's running on SGX2 hardware, inject an SGX
	 * #PF if the fault matches an EPCM fault signature (#GP on SGX1,
	 * #PF on SGX2).  The assumption is that EPCM faults are much more
	 * likely than a bad userspace address.
	 */
	if ((trapnr == PF_VECTOR || !boot_cpu_has(X86_FEATURE_SGX2)) &&
	    guest_cpuid_has(vcpu, X86_FEATURE_SGX2)) {
		memset(&ex, 0, sizeof(ex));
		ex.vector = PF_VECTOR;
		ex.error_code = PFERR_PRESENT_MASK | PFERR_WRITE_MASK |
				PFERR_SGX_MASK;
		ex.address = gva;
		ex.error_code_valid = true;
		ex.nested_page_fault = false;
		kvm_inject_emulated_page_fault(vcpu, &ex);
	} else {
		kvm_inject_gp(vcpu, 0);
	}
	return 1;
}

static int __handle_encls_ecreate(struct kvm_vcpu *vcpu,
				  struct sgx_pageinfo *pageinfo,
				  unsigned long secs_hva,
				  gva_t secs_gva)
{
	struct sgx_secs *contents = (struct sgx_secs *)pageinfo->contents;
	struct kvm_cpuid_entry2 *sgx_12_0, *sgx_12_1;
	u64 attributes, xfrm, size;
	u32 miscselect;
	u8 max_size_log2;
	int trapnr, ret;

	sgx_12_0 = kvm_find_cpuid_entry_index(vcpu, 0x12, 0);
	sgx_12_1 = kvm_find_cpuid_entry_index(vcpu, 0x12, 1);
	if (!sgx_12_0 || !sgx_12_1) {
		kvm_prepare_emulation_failure_exit(vcpu);
		return 0;
	}

	miscselect = contents->miscselect;
	attributes = contents->attributes;
	xfrm = contents->xfrm;
	size = contents->size;

	/* Enforce restriction of access to the PROVISIONKEY. */
	if (!vcpu->kvm->arch.sgx_provisioning_allowed &&
	    (attributes & SGX_ATTR_PROVISIONKEY)) {
		if (sgx_12_1->eax & SGX_ATTR_PROVISIONKEY)
			pr_warn_once("SGX PROVISIONKEY advertised but not allowed\n");
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	/* Enforce CPUID restrictions on MISCSELECT, ATTRIBUTES and XFRM. */
	if ((u32)miscselect & ~sgx_12_0->ebx ||
	    (u32)attributes & ~sgx_12_1->eax ||
	    (u32)(attributes >> 32) & ~sgx_12_1->ebx ||
	    (u32)xfrm & ~sgx_12_1->ecx ||
	    (u32)(xfrm >> 32) & ~sgx_12_1->edx) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	/* Enforce CPUID restriction on max enclave size. */
	max_size_log2 = (attributes & SGX_ATTR_MODE64BIT) ? sgx_12_0->edx >> 8 :
							    sgx_12_0->edx;
	if (size >= BIT_ULL(max_size_log2)) {
		kvm_inject_gp(vcpu, 0);
		return 1;
	}

	/*
	 * sgx_virt_ecreate() returns:
	 *  1) 0:	ECREATE was successful
	 *  2) -EFAULT:	ECREATE was run but faulted, and trapnr was set to the
	 *		exception number.
	 *  3) -EINVAL:	access_ok() on @secs_hva failed. This should never
	 *		happen as KVM checks host addresses at memslot creation.
	 *		sgx_virt_ecreate() has already warned in this case.
	 */
	ret = sgx_virt_ecreate(pageinfo, (void __user *)secs_hva, &trapnr);
	if (!ret)
		return kvm_skip_emulated_instruction(vcpu);
	if (ret == -EFAULT)
		return sgx_inject_fault(vcpu, secs_gva, trapnr);

	return ret;
}

static int handle_encls_ecreate(struct kvm_vcpu *vcpu)
{
	gva_t pageinfo_gva, secs_gva;
	gva_t metadata_gva, contents_gva;
	gpa_t metadata_gpa, contents_gpa, secs_gpa;
	unsigned long metadata_hva, contents_hva, secs_hva;
	struct sgx_pageinfo pageinfo;
	struct sgx_secs *contents;
	struct x86_exception ex;
	int r;

	if (sgx_get_encls_gva(vcpu, kvm_rbx_read(vcpu), 32, 32, &pageinfo_gva) ||
	    sgx_get_encls_gva(vcpu, kvm_rcx_read(vcpu), 4096, 4096, &secs_gva))
		return 1;

	/*
	 * Copy the PAGEINFO to local memory, its pointers need to be
	 * translated, i.e. we need to do a deep copy/translate.
	 */
	r = kvm_read_guest_virt(vcpu, pageinfo_gva, &pageinfo,
				sizeof(pageinfo), &ex);
	if (r == X86EMUL_PROPAGATE_FAULT) {
		kvm_inject_emulated_page_fault(vcpu, &ex);
		return 1;
	} else if (r != X86EMUL_CONTINUE) {
		sgx_handle_emulation_failure(vcpu, pageinfo_gva,
					     sizeof(pageinfo));
		return 0;
	}

	if (sgx_get_encls_gva(vcpu, pageinfo.metadata, 64, 64, &metadata_gva) ||
	    sgx_get_encls_gva(vcpu, pageinfo.contents, 4096, 4096,
			      &contents_gva))
		return 1;

	/*
	 * Translate the SECINFO, SOURCE and SECS pointers from GVA to GPA.
	 * Resume the guest on failure to inject a #PF.
	 */
	if (sgx_gva_to_gpa(vcpu, metadata_gva, false, &metadata_gpa) ||
	    sgx_gva_to_gpa(vcpu, contents_gva, false, &contents_gpa) ||
	    sgx_gva_to_gpa(vcpu, secs_gva, true, &secs_gpa))
		return 1;

	/*
	 * ...and then to HVA.  The order of accesses isn't architectural, i.e.
	 * KVM doesn't have to fully process one address at a time.  Exit to
	 * userspace if a GPA is invalid.
	 */
	if (sgx_gpa_to_hva(vcpu, metadata_gpa, &metadata_hva) ||
	    sgx_gpa_to_hva(vcpu, contents_gpa, &contents_hva) ||
	    sgx_gpa_to_hva(vcpu, secs_gpa, &secs_hva))
		return 0;

	/*
	 * Copy contents into kernel memory to prevent TOCTOU attack. E.g. the
	 * guest could do ECREATE w/ SECS.SGX_ATTR_PROVISIONKEY=0, and
	 * simultaneously set SGX_ATTR_PROVISIONKEY to bypass the check to
	 * enforce restriction of access to the PROVISIONKEY.
	 */
	contents = (struct sgx_secs *)__get_free_page(GFP_KERNEL_ACCOUNT);
	if (!contents)
		return -ENOMEM;

	/* Exit to userspace if copying from a host userspace address fails. */
	if (sgx_read_hva(vcpu, contents_hva, (void *)contents, PAGE_SIZE)) {
		free_page((unsigned long)contents);
		return 0;
	}

	pageinfo.metadata = metadata_hva;
	pageinfo.contents = (u64)contents;

	r = __handle_encls_ecreate(vcpu, &pageinfo, secs_hva, secs_gva);

	free_page((unsigned long)contents);

	return r;
}

static int handle_encls_einit(struct kvm_vcpu *vcpu)
{
	unsigned long sig_hva, secs_hva, token_hva, rflags;
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	gva_t sig_gva, secs_gva, token_gva;
	gpa_t sig_gpa, secs_gpa, token_gpa;
	int ret, trapnr;

	if (sgx_get_encls_gva(vcpu, kvm_rbx_read(vcpu), 1808, 4096, &sig_gva) ||
	    sgx_get_encls_gva(vcpu, kvm_rcx_read(vcpu), 4096, 4096, &secs_gva) ||
	    sgx_get_encls_gva(vcpu, kvm_rdx_read(vcpu), 304, 512, &token_gva))
		return 1;

	/*
	 * Translate the SIGSTRUCT, SECS and TOKEN pointers from GVA to GPA.
	 * Resume the guest on failure to inject a #PF.
	 */
	if (sgx_gva_to_gpa(vcpu, sig_gva, false, &sig_gpa) ||
	    sgx_gva_to_gpa(vcpu, secs_gva, true, &secs_gpa) ||
	    sgx_gva_to_gpa(vcpu, token_gva, false, &token_gpa))
		return 1;

	/*
	 * ...and then to HVA.  The order of accesses isn't architectural, i.e.
	 * KVM doesn't have to fully process one address at a time.  Exit to
	 * userspace if a GPA is invalid.  Note, all structures are aligned and
	 * cannot split pages.
	 */
	if (sgx_gpa_to_hva(vcpu, sig_gpa, &sig_hva) ||
	    sgx_gpa_to_hva(vcpu, secs_gpa, &secs_hva) ||
	    sgx_gpa_to_hva(vcpu, token_gpa, &token_hva))
		return 0;

	ret = sgx_virt_einit((void __user *)sig_hva, (void __user *)token_hva,
			     (void __user *)secs_hva,
			     vmx->msr_ia32_sgxlepubkeyhash, &trapnr);

	if (ret == -EFAULT)
		return sgx_inject_fault(vcpu, secs_gva, trapnr);

	/*
	 * sgx_virt_einit() returns -EINVAL when access_ok() fails on @sig_hva,
	 * @token_hva or @secs_hva. This should never happen as KVM checks host
	 * addresses at memslot creation. sgx_virt_einit() has already warned
	 * in this case, so just return.
	 */
	if (ret < 0)
		return ret;

	rflags = vmx_get_rflags(vcpu) & ~(X86_EFLAGS_CF | X86_EFLAGS_PF |
					  X86_EFLAGS_AF | X86_EFLAGS_SF |
					  X86_EFLAGS_OF);
	if (ret)
		rflags |= X86_EFLAGS_ZF;
	else
		rflags &= ~X86_EFLAGS_ZF;
	vmx_set_rflags(vcpu, rflags);

	kvm_rax_write(vcpu, ret);
	return kvm_skip_emulated_instruction(vcpu);
}

static inline bool encls_leaf_enabled_in_guest(struct kvm_vcpu *vcpu, u32 leaf)
{
	if (!enable_sgx || !guest_cpuid_has(vcpu, X86_FEATURE_SGX))
		return false;

	if (leaf >= ECREATE && leaf <= ETRACK)
		return guest_cpuid_has(vcpu, X86_FEATURE_SGX1);

	if (leaf >= EAUG && leaf <= EMODT)
		return guest_cpuid_has(vcpu, X86_FEATURE_SGX2);

	return false;
}

static inline bool sgx_enabled_in_guest_bios(struct kvm_vcpu *vcpu)
{
	const u64 bits = FEAT_CTL_SGX_ENABLED | FEAT_CTL_LOCKED;

	return (to_vmx(vcpu)->msr_ia32_feature_control & bits) == bits;
}

int handle_encls(struct kvm_vcpu *vcpu)
{
	u32 leaf = (u32)kvm_rax_read(vcpu);

	if (!encls_leaf_enabled_in_guest(vcpu, leaf)) {
		kvm_queue_exception(vcpu, UD_VECTOR);
	} else if (!sgx_enabled_in_guest_bios(vcpu)) {
		kvm_inject_gp(vcpu, 0);
	} else {
		if (leaf == ECREATE)
			return handle_encls_ecreate(vcpu);
		if (leaf == EINIT)
			return handle_encls_einit(vcpu);
		WARN_ONCE(1, "unexpected exit on ENCLS[%u]", leaf);
		vcpu->run->exit_reason = KVM_EXIT_UNKNOWN;
		vcpu->run->hw.hardware_exit_reason = EXIT_REASON_ENCLS;
		return 0;
	}
	return 1;
}

void setup_default_sgx_lepubkeyhash(void)
{
	/*
	 * Use Intel's default value for Skylake hardware if Launch Control is
	 * not supported, i.e. Intel's hash is hardcoded into silicon, or if
	 * Launch Control is supported and enabled, i.e. mimic the reset value
	 * and let the guest write the MSRs at will.  If Launch Control is
	 * supported but disabled, then use the current MSR values as the hash
	 * MSRs exist but are read-only (locked and not writable).
	 */
	if (!enable_sgx || boot_cpu_has(X86_FEATURE_SGX_LC) ||
	    rdmsrl_safe(MSR_IA32_SGXLEPUBKEYHASH0, &sgx_pubkey_hash[0])) {
		sgx_pubkey_hash[0] = 0xa6053e051270b7acULL;
		sgx_pubkey_hash[1] = 0x6cfbe8ba8b3b413dULL;
		sgx_pubkey_hash[2] = 0xc4916d99f2b3735dULL;
		sgx_pubkey_hash[3] = 0xd4f8c05909f9bb3bULL;
	} else {
		/* MSR_IA32_SGXLEPUBKEYHASH0 is read above */
		rdmsrl(MSR_IA32_SGXLEPUBKEYHASH1, sgx_pubkey_hash[1]);
		rdmsrl(MSR_IA32_SGXLEPUBKEYHASH2, sgx_pubkey_hash[2]);
		rdmsrl(MSR_IA32_SGXLEPUBKEYHASH3, sgx_pubkey_hash[3]);
	}
}

void vcpu_setup_sgx_lepubkeyhash(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);

	memcpy(vmx->msr_ia32_sgxlepubkeyhash, sgx_pubkey_hash,
	       sizeof(sgx_pubkey_hash));
}

/*
 * ECREATE must be intercepted to enforce MISCSELECT, ATTRIBUTES and XFRM
 * restrictions if the guest's allowed-1 settings diverge from hardware.
 */
static bool sgx_intercept_encls_ecreate(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *guest_cpuid;
	u32 eax, ebx, ecx, edx;

	if (!vcpu->kvm->arch.sgx_provisioning_allowed)
		return true;

	guest_cpuid = kvm_find_cpuid_entry_index(vcpu, 0x12, 0);
	if (!guest_cpuid)
		return true;

	cpuid_count(0x12, 0, &eax, &ebx, &ecx, &edx);
	if (guest_cpuid->ebx != ebx || guest_cpuid->edx != edx)
		return true;

	guest_cpuid = kvm_find_cpuid_entry_index(vcpu, 0x12, 1);
	if (!guest_cpuid)
		return true;

	cpuid_count(0x12, 1, &eax, &ebx, &ecx, &edx);
	if (guest_cpuid->eax != eax || guest_cpuid->ebx != ebx ||
	    guest_cpuid->ecx != ecx || guest_cpuid->edx != edx)
		return true;

	return false;
}

void vmx_write_encls_bitmap(struct kvm_vcpu *vcpu, struct vmcs12 *vmcs12)
{
	/*
	 * There is no software enable bit for SGX that is virtualized by
	 * hardware, e.g. there's no CR4.SGXE, so when SGX is disabled in the
	 * guest (either by the host or by the guest's BIOS) but enabled in the
	 * host, trap all ENCLS leafs and inject #UD/#GP as needed to emulate
	 * the expected system behavior for ENCLS.
	 */
	u64 bitmap = -1ull;

	/* Nothing to do if hardware doesn't support SGX */
	if (!cpu_has_vmx_encls_vmexit())
		return;

	if (guest_cpuid_has(vcpu, X86_FEATURE_SGX) &&
	    sgx_enabled_in_guest_bios(vcpu)) {
		if (guest_cpuid_has(vcpu, X86_FEATURE_SGX1)) {
			bitmap &= ~GENMASK_ULL(ETRACK, ECREATE);
			if (sgx_intercept_encls_ecreate(vcpu))
				bitmap |= (1 << ECREATE);
		}

		if (guest_cpuid_has(vcpu, X86_FEATURE_SGX2))
			bitmap &= ~GENMASK_ULL(EMODT, EAUG);

		/*
		 * Trap and execute EINIT if launch control is enabled in the
		 * host using the guest's values for launch control MSRs, even
		 * if the guest's values are fixed to hardware default values.
		 * The MSRs are not loaded/saved on VM-Enter/VM-Exit as writing
		 * the MSRs is extraordinarily expensive.
		 */
		if (boot_cpu_has(X86_FEATURE_SGX_LC))
			bitmap |= (1 << EINIT);

		if (!vmcs12 && is_guest_mode(vcpu))
			vmcs12 = get_vmcs12(vcpu);
		if (vmcs12 && nested_cpu_has_encls_exit(vmcs12))
			bitmap |= vmcs12->encls_exiting_bitmap;
	}
	vmcs_write64(ENCLS_EXITING_BITMAP, bitmap);
}
