// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <hyp/sysreg-sr.h>

#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kprobes.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>

/*
 * Non-VHE: Both host and guest must save everything.
 */

void __sysreg_save_state_nvhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_save_el1_state(ctxt);
	__sysreg_save_common_state(ctxt);
	__sysreg_save_user_state(ctxt);
	__sysreg_save_el2_return_state(ctxt);
}

void __sysreg_restore_state_nvhe(struct kvm_cpu_context *ctxt)
{
	__sysreg_restore_el1_state(ctxt);
	__sysreg_restore_common_state(ctxt);
	__sysreg_restore_user_state(ctxt);
	__sysreg_restore_el2_return_state(ctxt);
}

void __kvm_enable_ssbs(void)
{
	u64 tmp;

	asm volatile(
	"mrs	%0, sctlr_el2\n"
	"orr	%0, %0, %1\n"
	"msr	sctlr_el2, %0"
	: "=&r" (tmp) : "L" (SCTLR_ELx_DSSBS));
}
