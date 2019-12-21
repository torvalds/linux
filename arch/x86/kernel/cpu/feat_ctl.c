// SPDX-License-Identifier: GPL-2.0
#include <linux/tboot.h>

#include <asm/cpufeature.h>
#include <asm/msr-index.h>
#include <asm/processor.h>

void init_ia32_feat_ctl(struct cpuinfo_x86 *c)
{
	u64 msr;

	if (rdmsrl_safe(MSR_IA32_FEAT_CTL, &msr))
		return;

	if (msr & FEAT_CTL_LOCKED)
		return;

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

		if (tboot_enabled())
			msr |= FEAT_CTL_VMX_ENABLED_INSIDE_SMX;
	}

	wrmsrl(MSR_IA32_FEAT_CTL, msr);
}
