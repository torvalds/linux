// SPDX-License-Identifier: GPL-2.0
#include <linux/tboot.h>

#include <asm/cpufeature.h>
#include <asm/msr-index.h>
#include <asm/processor.h>
#include <asm/vmx.h>
#include "cpu.h"

#undef pr_fmt
#define pr_fmt(fmt)	"x86/cpu: " fmt

#ifdef CONFIG_X86_VMX_FEATURE_NAMES
enum vmx_feature_leafs {
	MISC_FEATURES = 0,
	PRIMARY_CTLS,
	SECONDARY_CTLS,
	NR_VMX_FEATURE_WORDS,
};

#define VMX_F(x) BIT(VMX_FEATURE_##x & 0x1f)

static void init_vmx_capabilities(struct cpuinfo_x86 *c)
{
	u32 supported, funcs, ept, vpid, ign;

	BUILD_BUG_ON(NVMXINTS != NR_VMX_FEATURE_WORDS);

	/*
	 * The high bits contain the allowed-1 settings, i.e. features that can
	 * be turned on.  The low bits contain the allowed-0 settings, i.e.
	 * features that can be turned off.  Ignore the allowed-0 settings,
	 * if a feature can be turned on then it's supported.
	 *
	 * Use raw rdmsr() for primary processor controls and pin controls MSRs
	 * as they exist on any CPU that supports VMX, i.e. we want the WARN if
	 * the RDMSR faults.
	 */
	rdmsr(MSR_IA32_VMX_PROCBASED_CTLS, ign, supported);
	c->vmx_capability[PRIMARY_CTLS] = supported;

	rdmsr_safe(MSR_IA32_VMX_PROCBASED_CTLS2, &ign, &supported);
	c->vmx_capability[SECONDARY_CTLS] = supported;

	rdmsr(MSR_IA32_VMX_PINBASED_CTLS, ign, supported);
	rdmsr_safe(MSR_IA32_VMX_VMFUNC, &ign, &funcs);

	/*
	 * Except for EPT+VPID, which enumerates support for both in a single
	 * MSR, low for EPT, high for VPID.
	 */
	rdmsr_safe(MSR_IA32_VMX_EPT_VPID_CAP, &ept, &vpid);

	/* Pin, EPT, VPID and VM-Func are merged into a single word. */
	WARN_ON_ONCE(supported >> 16);
	WARN_ON_ONCE(funcs >> 4);
	c->vmx_capability[MISC_FEATURES] = (supported & 0xffff) |
					   ((vpid & 0x1) << 16) |
					   ((funcs & 0xf) << 28);

	/* EPT bits are full on scattered and must be manually handled. */
	if (ept & VMX_EPT_EXECUTE_ONLY_BIT)
		c->vmx_capability[MISC_FEATURES] |= VMX_F(EPT_EXECUTE_ONLY);
	if (ept & VMX_EPT_AD_BIT)
		c->vmx_capability[MISC_FEATURES] |= VMX_F(EPT_AD);
	if (ept & VMX_EPT_1GB_PAGE_BIT)
		c->vmx_capability[MISC_FEATURES] |= VMX_F(EPT_1GB);

	/* Synthetic APIC features that are aggregates of multiple features. */
	if ((c->vmx_capability[PRIMARY_CTLS] & VMX_F(VIRTUAL_TPR)) &&
	    (c->vmx_capability[SECONDARY_CTLS] & VMX_F(VIRT_APIC_ACCESSES)))
		c->vmx_capability[MISC_FEATURES] |= VMX_F(FLEXPRIORITY);

	if ((c->vmx_capability[PRIMARY_CTLS] & VMX_F(VIRTUAL_TPR)) &&
	    (c->vmx_capability[SECONDARY_CTLS] & VMX_F(APIC_REGISTER_VIRT)) &&
	    (c->vmx_capability[SECONDARY_CTLS] & VMX_F(VIRT_INTR_DELIVERY)) &&
	    (c->vmx_capability[MISC_FEATURES] & VMX_F(POSTED_INTR)))
		c->vmx_capability[MISC_FEATURES] |= VMX_F(APICV);

	/* Set the synthetic cpufeatures to preserve /proc/cpuinfo's ABI. */
	if (c->vmx_capability[PRIMARY_CTLS] & VMX_F(VIRTUAL_TPR))
		set_cpu_cap(c, X86_FEATURE_TPR_SHADOW);
	if (c->vmx_capability[MISC_FEATURES] & VMX_F(FLEXPRIORITY))
		set_cpu_cap(c, X86_FEATURE_FLEXPRIORITY);
	if (c->vmx_capability[MISC_FEATURES] & VMX_F(VIRTUAL_NMIS))
		set_cpu_cap(c, X86_FEATURE_VNMI);
	if (c->vmx_capability[SECONDARY_CTLS] & VMX_F(EPT))
		set_cpu_cap(c, X86_FEATURE_EPT);
	if (c->vmx_capability[MISC_FEATURES] & VMX_F(EPT_AD))
		set_cpu_cap(c, X86_FEATURE_EPT_AD);
	if (c->vmx_capability[MISC_FEATURES] & VMX_F(VPID))
		set_cpu_cap(c, X86_FEATURE_VPID);
}
#endif /* CONFIG_X86_VMX_FEATURE_NAMES */

static void clear_sgx_caps(void)
{
	setup_clear_cpu_cap(X86_FEATURE_SGX);
	setup_clear_cpu_cap(X86_FEATURE_SGX_LC);
}

static int __init nosgx(char *str)
{
	clear_sgx_caps();

	return 0;
}

early_param("nosgx", nosgx);

void init_ia32_feat_ctl(struct cpuinfo_x86 *c)
{
	bool tboot = tboot_enabled();
	bool enable_sgx;
	u64 msr;

	if (rdmsrl_safe(MSR_IA32_FEAT_CTL, &msr)) {
		clear_cpu_cap(c, X86_FEATURE_VMX);
		clear_sgx_caps();
		return;
	}

	/*
	 * Enable SGX if and only if the kernel supports SGX and Launch Control
	 * is supported, i.e. disable SGX if the LE hash MSRs can't be written.
	 */
	enable_sgx = cpu_has(c, X86_FEATURE_SGX) &&
		     cpu_has(c, X86_FEATURE_SGX_LC) &&
		     IS_ENABLED(CONFIG_X86_SGX);

	if (msr & FEAT_CTL_LOCKED)
		goto update_caps;

	/*
	 * Ignore whatever value BIOS left in the MSR to avoid enabling random
	 * features or faulting on the WRMSR.
	 */
	msr = FEAT_CTL_LOCKED;

	/*
	 * Enable VMX if and only if the kernel may do VMXON at some point,
	 * i.e. KVM is enabled, to avoid unnecessarily adding an attack vector
	 * for the kernel, e.g. using VMX to hide malicious code.
	 */
	if (cpu_has(c, X86_FEATURE_VMX) && IS_ENABLED(CONFIG_KVM_INTEL)) {
		msr |= FEAT_CTL_VMX_ENABLED_OUTSIDE_SMX;

		if (tboot)
			msr |= FEAT_CTL_VMX_ENABLED_INSIDE_SMX;
	}

	if (enable_sgx)
		msr |= FEAT_CTL_SGX_ENABLED | FEAT_CTL_SGX_LC_ENABLED;

	wrmsrl(MSR_IA32_FEAT_CTL, msr);

update_caps:
	set_cpu_cap(c, X86_FEATURE_MSR_IA32_FEAT_CTL);

	if (!cpu_has(c, X86_FEATURE_VMX))
		goto update_sgx;

	if ( (tboot && !(msr & FEAT_CTL_VMX_ENABLED_INSIDE_SMX)) ||
	    (!tboot && !(msr & FEAT_CTL_VMX_ENABLED_OUTSIDE_SMX))) {
		if (IS_ENABLED(CONFIG_KVM_INTEL))
			pr_err_once("VMX (%s TXT) disabled by BIOS\n",
				    tboot ? "inside" : "outside");
		clear_cpu_cap(c, X86_FEATURE_VMX);
	} else {
#ifdef CONFIG_X86_VMX_FEATURE_NAMES
		init_vmx_capabilities(c);
#endif
	}

update_sgx:
	if (!(msr & FEAT_CTL_SGX_ENABLED) ||
	    !(msr & FEAT_CTL_SGX_LC_ENABLED) || !enable_sgx) {
		if (enable_sgx)
			pr_err_once("SGX disabled by BIOS\n");
		clear_sgx_caps();
	}
}
