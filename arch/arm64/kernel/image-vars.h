/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linker script variables to be set after section resolution, as
 * ld.lld does not like variables assigned before SECTIONS is processed.
 */
#ifndef __ARM64_KERNEL_IMAGE_VARS_H
#define __ARM64_KERNEL_IMAGE_VARS_H

#ifndef LINKER_SCRIPT
#error This file should only be included in vmlinux.lds.S
#endif

#ifdef CONFIG_EFI

__efistub_kernel_size		= _edata - _text;
__efistub_primary_entry_offset	= primary_entry - _text;


/*
 * The EFI stub has its own symbol namespace prefixed by __efistub_, to
 * isolate it from the kernel proper. The following symbols are legally
 * accessed by the stub, so provide some aliases to make them accessible.
 * Only include data symbols here, or text symbols of functions that are
 * guaranteed to be safe when executed at another offset than they were
 * linked at. The routines below are all implemented in assembler in a
 * position independent manner
 */
__efistub_memcmp		= __pi_memcmp;
__efistub_memchr		= __pi_memchr;
__efistub_memcpy		= __pi_memcpy;
__efistub_memmove		= __pi_memmove;
__efistub_memset		= __pi_memset;
__efistub_strlen		= __pi_strlen;
__efistub_strnlen		= __pi_strnlen;
__efistub_strcmp		= __pi_strcmp;
__efistub_strncmp		= __pi_strncmp;
__efistub_strrchr		= __pi_strrchr;
__efistub___clean_dcache_area_poc = __pi___clean_dcache_area_poc;

#ifdef CONFIG_KASAN
__efistub___memcpy		= __pi_memcpy;
__efistub___memmove		= __pi_memmove;
__efistub___memset		= __pi_memset;
#endif

__efistub__text			= _text;
__efistub__end			= _end;
__efistub__edata		= _edata;
__efistub_screen_info		= screen_info;
__efistub__ctype		= _ctype;

#endif

#ifdef CONFIG_KVM

/*
 * KVM nVHE code has its own symbol namespace prefixed with __kvm_nvhe_, to
 * separate it from the kernel proper. The following symbols are legally
 * accessed by it, therefore provide aliases to make them linkable.
 * Do not include symbols which may not be safely accessed under hypervisor
 * memory mappings.
 */

#define KVM_NVHE_ALIAS(sym) __kvm_nvhe_##sym = sym;

/* Alternative callbacks for init-time patching of nVHE hyp code. */
KVM_NVHE_ALIAS(arm64_enable_wa2_handling);
KVM_NVHE_ALIAS(kvm_patch_vector_branch);
KVM_NVHE_ALIAS(kvm_update_va_mask);

/* Global kernel state accessed by nVHE hyp code. */
KVM_NVHE_ALIAS(arm64_ssbd_callback_required);
KVM_NVHE_ALIAS(kvm_host_data);
KVM_NVHE_ALIAS(kvm_vgic_global_state);

/* Kernel constant needed to compute idmap addresses. */
KVM_NVHE_ALIAS(kimage_voffset);

/* Kernel symbols used to call panic() from nVHE hyp code (via ERET). */
KVM_NVHE_ALIAS(__hyp_panic_string);
KVM_NVHE_ALIAS(panic);

/* Vectors installed by hyp-init on reset HVC. */
KVM_NVHE_ALIAS(__hyp_stub_vectors);

/* IDMAP TCR_EL1.T0SZ as computed by the EL1 init code */
KVM_NVHE_ALIAS(idmap_t0sz);

/* Kernel symbol used by icache_is_vpipt(). */
KVM_NVHE_ALIAS(__icache_flags);

/* Kernel symbols needed for cpus_have_final/const_caps checks. */
KVM_NVHE_ALIAS(arm64_const_caps_ready);
KVM_NVHE_ALIAS(cpu_hwcap_keys);
KVM_NVHE_ALIAS(cpu_hwcaps);

/* Static keys which are set if a vGIC trap should be handled in hyp. */
KVM_NVHE_ALIAS(vgic_v2_cpuif_trap);
KVM_NVHE_ALIAS(vgic_v3_cpuif_trap);

/* Static key checked in pmr_sync(). */
#ifdef CONFIG_ARM64_PSEUDO_NMI
KVM_NVHE_ALIAS(gic_pmr_sync);
#endif

/* EL2 exception handling */
KVM_NVHE_ALIAS(__start___kvm_ex_table);
KVM_NVHE_ALIAS(__stop___kvm_ex_table);

#endif /* CONFIG_KVM */

#endif /* __ARM64_KERNEL_IMAGE_VARS_H */
