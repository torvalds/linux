// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Transactional Synchronization Extensions (TSX) control.
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * Author:
 *	Pawan Gupta <pawan.kumar.gupta@linux.intel.com>
 */

#include <linux/cpufeature.h>

#include <asm/cmdline.h>

#include "cpu.h"

#undef pr_fmt
#define pr_fmt(fmt) "tsx: " fmt

enum tsx_ctrl_states tsx_ctrl_state __ro_after_init = TSX_CTRL_NOT_SUPPORTED;

void tsx_disable(void)
{
	u64 tsx;

	rdmsrl(MSR_IA32_TSX_CTRL, tsx);

	/* Force all transactions to immediately abort */
	tsx |= TSX_CTRL_RTM_DISABLE;

	/*
	 * Ensure TSX support is not enumerated in CPUID.
	 * This is visible to userspace and will ensure they
	 * do not waste resources trying TSX transactions that
	 * will always abort.
	 */
	tsx |= TSX_CTRL_CPUID_CLEAR;

	wrmsrl(MSR_IA32_TSX_CTRL, tsx);
}

void tsx_enable(void)
{
	u64 tsx;

	rdmsrl(MSR_IA32_TSX_CTRL, tsx);

	/* Enable the RTM feature in the cpu */
	tsx &= ~TSX_CTRL_RTM_DISABLE;

	/*
	 * Ensure TSX support is enumerated in CPUID.
	 * This is visible to userspace and will ensure they
	 * can enumerate and use the TSX feature.
	 */
	tsx &= ~TSX_CTRL_CPUID_CLEAR;

	wrmsrl(MSR_IA32_TSX_CTRL, tsx);
}

static bool __init tsx_ctrl_is_supported(void)
{
	u64 ia32_cap = x86_read_arch_cap_msr();

	/*
	 * TSX is controlled via MSR_IA32_TSX_CTRL.  However, support for this
	 * MSR is enumerated by ARCH_CAP_TSX_MSR bit in MSR_IA32_ARCH_CAPABILITIES.
	 *
	 * TSX control (aka MSR_IA32_TSX_CTRL) is only available after a
	 * microcode update on CPUs that have their MSR_IA32_ARCH_CAPABILITIES
	 * bit MDS_NO=1. CPUs with MDS_NO=0 are not planned to get
	 * MSR_IA32_TSX_CTRL support even after a microcode update. Thus,
	 * tsx= cmdline requests will do nothing on CPUs without
	 * MSR_IA32_TSX_CTRL support.
	 */
	return !!(ia32_cap & ARCH_CAP_TSX_CTRL_MSR);
}

static enum tsx_ctrl_states x86_get_tsx_auto_mode(void)
{
	if (boot_cpu_has_bug(X86_BUG_TAA))
		return TSX_CTRL_DISABLE;

	return TSX_CTRL_ENABLE;
}

void __init tsx_init(void)
{
	char arg[5] = {};
	int ret;

	if (!tsx_ctrl_is_supported())
		return;

	ret = cmdline_find_option(boot_command_line, "tsx", arg, sizeof(arg));
	if (ret >= 0) {
		if (!strcmp(arg, "on")) {
			tsx_ctrl_state = TSX_CTRL_ENABLE;
		} else if (!strcmp(arg, "off")) {
			tsx_ctrl_state = TSX_CTRL_DISABLE;
		} else if (!strcmp(arg, "auto")) {
			tsx_ctrl_state = x86_get_tsx_auto_mode();
		} else {
			tsx_ctrl_state = TSX_CTRL_DISABLE;
			pr_err("invalid option, defaulting to off\n");
		}
	} else {
		/* tsx= not provided */
		if (IS_ENABLED(CONFIG_X86_INTEL_TSX_MODE_AUTO))
			tsx_ctrl_state = x86_get_tsx_auto_mode();
		else if (IS_ENABLED(CONFIG_X86_INTEL_TSX_MODE_OFF))
			tsx_ctrl_state = TSX_CTRL_DISABLE;
		else
			tsx_ctrl_state = TSX_CTRL_ENABLE;
	}

	if (tsx_ctrl_state == TSX_CTRL_DISABLE) {
		tsx_disable();

		/*
		 * tsx_disable() will change the state of the RTM and HLE CPUID
		 * bits. Clear them here since they are now expected to be not
		 * set.
		 */
		setup_clear_cpu_cap(X86_FEATURE_RTM);
		setup_clear_cpu_cap(X86_FEATURE_HLE);
	} else if (tsx_ctrl_state == TSX_CTRL_ENABLE) {

		/*
		 * HW defaults TSX to be enabled at bootup.
		 * We may still need the TSX enable support
		 * during init for special cases like
		 * kexec after TSX is disabled.
		 */
		tsx_enable();

		/*
		 * tsx_enable() will change the state of the RTM and HLE CPUID
		 * bits. Force them here since they are now expected to be set.
		 */
		setup_force_cpu_cap(X86_FEATURE_RTM);
		setup_force_cpu_cap(X86_FEATURE_HLE);
	}
}
