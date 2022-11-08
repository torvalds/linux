// SPDX-License-Identifier: GPL-2.0
/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */
#define COMPILE_OFFSETS

#include <linux/kbuild.h>
#include "vmx/vmx.h"

static void __used common(void)
{
	if (IS_ENABLED(CONFIG_KVM_INTEL)) {
		BLANK();
		OFFSET(VMX_spec_ctrl, vcpu_vmx, spec_ctrl);
	}
}
