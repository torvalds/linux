// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - Google Inc
 * Author: Andrew Scull <ascull@google.com>
 */

#include <hyp/switch.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

typedef unsigned long (*hypcall_fn_t)
	(unsigned long, unsigned long, unsigned long);

void handle_trap(struct kvm_cpu_context *host_ctxt)
{
	u64 esr = read_sysreg_el2(SYS_ESR);
	hypcall_fn_t func;
	unsigned long ret;

	if (ESR_ELx_EC(esr) != ESR_ELx_EC_HVC64)
		hyp_panic();

	/*
	 * __kvm_call_hyp takes a pointer in the host address space and
	 * up to three arguments.
	 */
	func = (hypcall_fn_t)kern_hyp_va(host_ctxt->regs.regs[0]);
	ret = func(host_ctxt->regs.regs[1],
		   host_ctxt->regs.regs[2],
		   host_ctxt->regs.regs[3]);
	host_ctxt->regs.regs[0] = ret;
}
