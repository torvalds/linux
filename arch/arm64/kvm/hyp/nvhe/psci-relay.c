// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <kvm/arm_hypercalls.h>
#include <linux/arm-smccc.h>
#include <linux/psci.h>
#include <kvm/arm_psci.h>
#include <uapi/linux/psci.h>

#include <nvhe/trap_handler.h>

/* Config options set by the host. */
__ro_after_init u32 kvm_host_psci_version;
__ro_after_init struct psci_0_1_function_ids kvm_host_psci_0_1_function_ids;
__ro_after_init s64 hyp_physvirt_offset;

#define __hyp_pa(x) ((phys_addr_t)((x)) + hyp_physvirt_offset)

static u64 get_psci_func_id(struct kvm_cpu_context *host_ctxt)
{
	DECLARE_REG(u64, func_id, host_ctxt, 0);

	return func_id;
}

static bool is_psci_0_1_call(u64 func_id)
{
	return (func_id == kvm_host_psci_0_1_function_ids.cpu_suspend) ||
	       (func_id == kvm_host_psci_0_1_function_ids.cpu_on) ||
	       (func_id == kvm_host_psci_0_1_function_ids.cpu_off) ||
	       (func_id == kvm_host_psci_0_1_function_ids.migrate);
}

static bool is_psci_0_2_call(u64 func_id)
{
	/* SMCCC reserves IDs 0x00-1F with the given 32/64-bit base for PSCI. */
	return (PSCI_0_2_FN(0) <= func_id && func_id <= PSCI_0_2_FN(31)) ||
	       (PSCI_0_2_FN64(0) <= func_id && func_id <= PSCI_0_2_FN64(31));
}

static bool is_psci_call(u64 func_id)
{
	switch (kvm_host_psci_version) {
	case PSCI_VERSION(0, 1):
		return is_psci_0_1_call(func_id);
	default:
		return is_psci_0_2_call(func_id);
	}
}

static unsigned long psci_0_1_handler(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	return PSCI_RET_NOT_SUPPORTED;
}

static unsigned long psci_0_2_handler(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	switch (func_id) {
	default:
		return PSCI_RET_NOT_SUPPORTED;
	}
}

static unsigned long psci_1_0_handler(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	switch (func_id) {
	default:
		return psci_0_2_handler(func_id, host_ctxt);
	}
}

bool kvm_host_psci_handler(struct kvm_cpu_context *host_ctxt)
{
	u64 func_id = get_psci_func_id(host_ctxt);
	unsigned long ret;

	if (!is_psci_call(func_id))
		return false;

	switch (kvm_host_psci_version) {
	case PSCI_VERSION(0, 1):
		ret = psci_0_1_handler(func_id, host_ctxt);
		break;
	case PSCI_VERSION(0, 2):
		ret = psci_0_2_handler(func_id, host_ctxt);
		break;
	default:
		ret = psci_1_0_handler(func_id, host_ctxt);
		break;
	}

	cpu_reg(host_ctxt, 0) = ret;
	cpu_reg(host_ctxt, 1) = 0;
	cpu_reg(host_ctxt, 2) = 0;
	cpu_reg(host_ctxt, 3) = 0;
	return true;
}
