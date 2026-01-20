// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc.
 */

#include <linux/cpufeature.h>
#include <linux/errno.h>
#include <linux/kvm_host.h>
#include <linux/nospec.h>
#include <linux/pgtable.h>
#include <asm/kvm_isa.h>
#include <asm/vector.h>

#define KVM_ISA_EXT_ARR(ext)		\
[KVM_RISCV_ISA_EXT_##ext] = RISCV_ISA_EXT_##ext

/* Mapping between KVM ISA Extension ID & guest ISA extension ID */
static const unsigned long kvm_isa_ext_arr[] = {
	/* Single letter extensions (alphabetically sorted) */
	[KVM_RISCV_ISA_EXT_A] = RISCV_ISA_EXT_a,
	[KVM_RISCV_ISA_EXT_C] = RISCV_ISA_EXT_c,
	[KVM_RISCV_ISA_EXT_D] = RISCV_ISA_EXT_d,
	[KVM_RISCV_ISA_EXT_F] = RISCV_ISA_EXT_f,
	[KVM_RISCV_ISA_EXT_H] = RISCV_ISA_EXT_h,
	[KVM_RISCV_ISA_EXT_I] = RISCV_ISA_EXT_i,
	[KVM_RISCV_ISA_EXT_M] = RISCV_ISA_EXT_m,
	[KVM_RISCV_ISA_EXT_V] = RISCV_ISA_EXT_v,
	/* Multi letter extensions (alphabetically sorted) */
	KVM_ISA_EXT_ARR(SMNPM),
	KVM_ISA_EXT_ARR(SMSTATEEN),
	KVM_ISA_EXT_ARR(SSAIA),
	KVM_ISA_EXT_ARR(SSCOFPMF),
	KVM_ISA_EXT_ARR(SSNPM),
	KVM_ISA_EXT_ARR(SSTC),
	KVM_ISA_EXT_ARR(SVADE),
	KVM_ISA_EXT_ARR(SVADU),
	KVM_ISA_EXT_ARR(SVINVAL),
	KVM_ISA_EXT_ARR(SVNAPOT),
	KVM_ISA_EXT_ARR(SVPBMT),
	KVM_ISA_EXT_ARR(SVVPTC),
	KVM_ISA_EXT_ARR(ZAAMO),
	KVM_ISA_EXT_ARR(ZABHA),
	KVM_ISA_EXT_ARR(ZACAS),
	KVM_ISA_EXT_ARR(ZALASR),
	KVM_ISA_EXT_ARR(ZALRSC),
	KVM_ISA_EXT_ARR(ZAWRS),
	KVM_ISA_EXT_ARR(ZBA),
	KVM_ISA_EXT_ARR(ZBB),
	KVM_ISA_EXT_ARR(ZBC),
	KVM_ISA_EXT_ARR(ZBKB),
	KVM_ISA_EXT_ARR(ZBKC),
	KVM_ISA_EXT_ARR(ZBKX),
	KVM_ISA_EXT_ARR(ZBS),
	KVM_ISA_EXT_ARR(ZCA),
	KVM_ISA_EXT_ARR(ZCB),
	KVM_ISA_EXT_ARR(ZCD),
	KVM_ISA_EXT_ARR(ZCF),
	KVM_ISA_EXT_ARR(ZCLSD),
	KVM_ISA_EXT_ARR(ZCMOP),
	KVM_ISA_EXT_ARR(ZFA),
	KVM_ISA_EXT_ARR(ZFBFMIN),
	KVM_ISA_EXT_ARR(ZFH),
	KVM_ISA_EXT_ARR(ZFHMIN),
	KVM_ISA_EXT_ARR(ZICBOM),
	KVM_ISA_EXT_ARR(ZICBOP),
	KVM_ISA_EXT_ARR(ZICBOZ),
	KVM_ISA_EXT_ARR(ZICCRSE),
	KVM_ISA_EXT_ARR(ZICNTR),
	KVM_ISA_EXT_ARR(ZICOND),
	KVM_ISA_EXT_ARR(ZICSR),
	KVM_ISA_EXT_ARR(ZIFENCEI),
	KVM_ISA_EXT_ARR(ZIHINTNTL),
	KVM_ISA_EXT_ARR(ZIHINTPAUSE),
	KVM_ISA_EXT_ARR(ZIHPM),
	KVM_ISA_EXT_ARR(ZILSD),
	KVM_ISA_EXT_ARR(ZIMOP),
	KVM_ISA_EXT_ARR(ZKND),
	KVM_ISA_EXT_ARR(ZKNE),
	KVM_ISA_EXT_ARR(ZKNH),
	KVM_ISA_EXT_ARR(ZKR),
	KVM_ISA_EXT_ARR(ZKSED),
	KVM_ISA_EXT_ARR(ZKSH),
	KVM_ISA_EXT_ARR(ZKT),
	KVM_ISA_EXT_ARR(ZTSO),
	KVM_ISA_EXT_ARR(ZVBB),
	KVM_ISA_EXT_ARR(ZVBC),
	KVM_ISA_EXT_ARR(ZVFBFMIN),
	KVM_ISA_EXT_ARR(ZVFBFWMA),
	KVM_ISA_EXT_ARR(ZVFH),
	KVM_ISA_EXT_ARR(ZVFHMIN),
	KVM_ISA_EXT_ARR(ZVKB),
	KVM_ISA_EXT_ARR(ZVKG),
	KVM_ISA_EXT_ARR(ZVKNED),
	KVM_ISA_EXT_ARR(ZVKNHA),
	KVM_ISA_EXT_ARR(ZVKNHB),
	KVM_ISA_EXT_ARR(ZVKSED),
	KVM_ISA_EXT_ARR(ZVKSH),
	KVM_ISA_EXT_ARR(ZVKT),
};

unsigned long kvm_riscv_base2isa_ext(unsigned long base_ext)
{
	unsigned long i;

	for (i = 0; i < KVM_RISCV_ISA_EXT_MAX; i++) {
		if (kvm_isa_ext_arr[i] == base_ext)
			return i;
	}

	return KVM_RISCV_ISA_EXT_MAX;
}

int __kvm_riscv_isa_check_host(unsigned long kvm_ext, unsigned long *base_ext)
{
	unsigned long host_ext;

	if (kvm_ext >= KVM_RISCV_ISA_EXT_MAX ||
	    kvm_ext >= ARRAY_SIZE(kvm_isa_ext_arr))
		return -ENOENT;

	kvm_ext = array_index_nospec(kvm_ext, ARRAY_SIZE(kvm_isa_ext_arr));
	switch (kvm_isa_ext_arr[kvm_ext]) {
	case RISCV_ISA_EXT_SMNPM:
		/*
		 * Pointer masking effective in (H)S-mode is provided by the
		 * Smnpm extension, so that extension is reported to the guest,
		 * even though the CSR bits for configuring VS-mode pointer
		 * masking on the host side are part of the Ssnpm extension.
		 */
		host_ext = RISCV_ISA_EXT_SSNPM;
		break;
	default:
		host_ext = kvm_isa_ext_arr[kvm_ext];
		break;
	}

	if (!__riscv_isa_extension_available(NULL, host_ext))
		return -ENOENT;

	if (base_ext)
		*base_ext = kvm_isa_ext_arr[kvm_ext];

	return 0;
}

bool kvm_riscv_isa_enable_allowed(unsigned long ext)
{
	switch (ext) {
	case KVM_RISCV_ISA_EXT_H:
		return false;
	case KVM_RISCV_ISA_EXT_SSCOFPMF:
		/* Sscofpmf depends on interrupt filtering defined in ssaia */
		return !kvm_riscv_isa_check_host(SSAIA);
	case KVM_RISCV_ISA_EXT_SVADU:
		/*
		 * The henvcfg.ADUE is read-only zero if menvcfg.ADUE is zero.
		 * Guest OS can use Svadu only when host OS enable Svadu.
		 */
		return arch_has_hw_pte_young();
	case KVM_RISCV_ISA_EXT_V:
		return riscv_v_vstate_ctrl_user_allowed();
	default:
		break;
	}

	return true;
}

bool kvm_riscv_isa_disable_allowed(unsigned long ext)
{
	switch (ext) {
	/* Extensions which don't have any mechanism to disable */
	case KVM_RISCV_ISA_EXT_A:
	case KVM_RISCV_ISA_EXT_C:
	case KVM_RISCV_ISA_EXT_I:
	case KVM_RISCV_ISA_EXT_M:
	/* There is not architectural config bit to disable sscofpmf completely */
	case KVM_RISCV_ISA_EXT_SSCOFPMF:
	case KVM_RISCV_ISA_EXT_SSNPM:
	case KVM_RISCV_ISA_EXT_SSTC:
	case KVM_RISCV_ISA_EXT_SVINVAL:
	case KVM_RISCV_ISA_EXT_SVNAPOT:
	case KVM_RISCV_ISA_EXT_SVVPTC:
	case KVM_RISCV_ISA_EXT_ZAAMO:
	case KVM_RISCV_ISA_EXT_ZABHA:
	case KVM_RISCV_ISA_EXT_ZACAS:
	case KVM_RISCV_ISA_EXT_ZALASR:
	case KVM_RISCV_ISA_EXT_ZALRSC:
	case KVM_RISCV_ISA_EXT_ZAWRS:
	case KVM_RISCV_ISA_EXT_ZBA:
	case KVM_RISCV_ISA_EXT_ZBB:
	case KVM_RISCV_ISA_EXT_ZBC:
	case KVM_RISCV_ISA_EXT_ZBKB:
	case KVM_RISCV_ISA_EXT_ZBKC:
	case KVM_RISCV_ISA_EXT_ZBKX:
	case KVM_RISCV_ISA_EXT_ZBS:
	case KVM_RISCV_ISA_EXT_ZCA:
	case KVM_RISCV_ISA_EXT_ZCB:
	case KVM_RISCV_ISA_EXT_ZCD:
	case KVM_RISCV_ISA_EXT_ZCF:
	case KVM_RISCV_ISA_EXT_ZCMOP:
	case KVM_RISCV_ISA_EXT_ZFA:
	case KVM_RISCV_ISA_EXT_ZFBFMIN:
	case KVM_RISCV_ISA_EXT_ZFH:
	case KVM_RISCV_ISA_EXT_ZFHMIN:
	case KVM_RISCV_ISA_EXT_ZICBOP:
	case KVM_RISCV_ISA_EXT_ZICCRSE:
	case KVM_RISCV_ISA_EXT_ZICNTR:
	case KVM_RISCV_ISA_EXT_ZICOND:
	case KVM_RISCV_ISA_EXT_ZICSR:
	case KVM_RISCV_ISA_EXT_ZIFENCEI:
	case KVM_RISCV_ISA_EXT_ZIHINTNTL:
	case KVM_RISCV_ISA_EXT_ZIHINTPAUSE:
	case KVM_RISCV_ISA_EXT_ZIHPM:
	case KVM_RISCV_ISA_EXT_ZIMOP:
	case KVM_RISCV_ISA_EXT_ZKND:
	case KVM_RISCV_ISA_EXT_ZKNE:
	case KVM_RISCV_ISA_EXT_ZKNH:
	case KVM_RISCV_ISA_EXT_ZKR:
	case KVM_RISCV_ISA_EXT_ZKSED:
	case KVM_RISCV_ISA_EXT_ZKSH:
	case KVM_RISCV_ISA_EXT_ZKT:
	case KVM_RISCV_ISA_EXT_ZTSO:
	case KVM_RISCV_ISA_EXT_ZVBB:
	case KVM_RISCV_ISA_EXT_ZVBC:
	case KVM_RISCV_ISA_EXT_ZVFBFMIN:
	case KVM_RISCV_ISA_EXT_ZVFBFWMA:
	case KVM_RISCV_ISA_EXT_ZVFH:
	case KVM_RISCV_ISA_EXT_ZVFHMIN:
	case KVM_RISCV_ISA_EXT_ZVKB:
	case KVM_RISCV_ISA_EXT_ZVKG:
	case KVM_RISCV_ISA_EXT_ZVKNED:
	case KVM_RISCV_ISA_EXT_ZVKNHA:
	case KVM_RISCV_ISA_EXT_ZVKNHB:
	case KVM_RISCV_ISA_EXT_ZVKSED:
	case KVM_RISCV_ISA_EXT_ZVKSH:
	case KVM_RISCV_ISA_EXT_ZVKT:
		return false;
	/* Extensions which can be disabled using Smstateen */
	case KVM_RISCV_ISA_EXT_SSAIA:
		return riscv_has_extension_unlikely(RISCV_ISA_EXT_SMSTATEEN);
	case KVM_RISCV_ISA_EXT_SVADE:
		/*
		 * The henvcfg.ADUE is read-only zero if menvcfg.ADUE is zero.
		 * Svade can't be disabled unless we support Svadu.
		 */
		return arch_has_hw_pte_young();
	default:
		break;
	}

	return true;
}
