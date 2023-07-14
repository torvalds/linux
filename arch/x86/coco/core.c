// SPDX-License-Identifier: GPL-2.0-only
/*
 * Confidential Computing Platform Capability checks
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 */

#include <linux/export.h>
#include <linux/cc_platform.h>

#include <asm/coco.h>
#include <asm/processor.h>

enum cc_vendor cc_vendor __ro_after_init = CC_VENDOR_NONE;
static u64 cc_mask __ro_after_init;

static bool noinstr intel_cc_platform_has(enum cc_attr attr)
{
	switch (attr) {
	case CC_ATTR_GUEST_UNROLL_STRING_IO:
	case CC_ATTR_HOTPLUG_DISABLED:
	case CC_ATTR_GUEST_MEM_ENCRYPT:
	case CC_ATTR_MEM_ENCRYPT:
		return true;
	default:
		return false;
	}
}

/*
 * Handle the SEV-SNP vTOM case where sme_me_mask is zero, and
 * the other levels of SME/SEV functionality, including C-bit
 * based SEV-SNP, are not enabled.
 */
static __maybe_unused __always_inline bool amd_cc_platform_vtom(enum cc_attr attr)
{
	switch (attr) {
	case CC_ATTR_GUEST_MEM_ENCRYPT:
	case CC_ATTR_MEM_ENCRYPT:
		return true;
	default:
		return false;
	}
}

/*
 * SME and SEV are very similar but they are not the same, so there are
 * times that the kernel will need to distinguish between SME and SEV. The
 * cc_platform_has() function is used for this.  When a distinction isn't
 * needed, the CC_ATTR_MEM_ENCRYPT attribute can be used.
 *
 * The trampoline code is a good example for this requirement.  Before
 * paging is activated, SME will access all memory as decrypted, but SEV
 * will access all memory as encrypted.  So, when APs are being brought
 * up under SME the trampoline area cannot be encrypted, whereas under SEV
 * the trampoline area must be encrypted.
 */

static bool noinstr amd_cc_platform_has(enum cc_attr attr)
{
#ifdef CONFIG_AMD_MEM_ENCRYPT

	if (sev_status & MSR_AMD64_SNP_VTOM)
		return amd_cc_platform_vtom(attr);

	switch (attr) {
	case CC_ATTR_MEM_ENCRYPT:
		return sme_me_mask;

	case CC_ATTR_HOST_MEM_ENCRYPT:
		return sme_me_mask && !(sev_status & MSR_AMD64_SEV_ENABLED);

	case CC_ATTR_GUEST_MEM_ENCRYPT:
		return sev_status & MSR_AMD64_SEV_ENABLED;

	case CC_ATTR_GUEST_STATE_ENCRYPT:
		return sev_status & MSR_AMD64_SEV_ES_ENABLED;

	/*
	 * With SEV, the rep string I/O instructions need to be unrolled
	 * but SEV-ES supports them through the #VC handler.
	 */
	case CC_ATTR_GUEST_UNROLL_STRING_IO:
		return (sev_status & MSR_AMD64_SEV_ENABLED) &&
			!(sev_status & MSR_AMD64_SEV_ES_ENABLED);

	case CC_ATTR_GUEST_SEV_SNP:
		return sev_status & MSR_AMD64_SEV_SNP_ENABLED;

	default:
		return false;
	}
#else
	return false;
#endif
}

bool noinstr cc_platform_has(enum cc_attr attr)
{
	switch (cc_vendor) {
	case CC_VENDOR_AMD:
		return amd_cc_platform_has(attr);
	case CC_VENDOR_INTEL:
		return intel_cc_platform_has(attr);
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(cc_platform_has);

u64 cc_mkenc(u64 val)
{
	/*
	 * Both AMD and Intel use a bit in the page table to indicate
	 * encryption status of the page.
	 *
	 * - for AMD, bit *set* means the page is encrypted
	 * - for AMD with vTOM and for Intel, *clear* means encrypted
	 */
	switch (cc_vendor) {
	case CC_VENDOR_AMD:
		if (sev_status & MSR_AMD64_SNP_VTOM)
			return val & ~cc_mask;
		else
			return val | cc_mask;
	case CC_VENDOR_INTEL:
		return val & ~cc_mask;
	default:
		return val;
	}
}

u64 cc_mkdec(u64 val)
{
	/* See comment in cc_mkenc() */
	switch (cc_vendor) {
	case CC_VENDOR_AMD:
		if (sev_status & MSR_AMD64_SNP_VTOM)
			return val | cc_mask;
		else
			return val & ~cc_mask;
	case CC_VENDOR_INTEL:
		return val | cc_mask;
	default:
		return val;
	}
}
EXPORT_SYMBOL_GPL(cc_mkdec);

__init void cc_set_mask(u64 mask)
{
	cc_mask = mask;
}
