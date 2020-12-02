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

static unsigned long psci_call(unsigned long fn, unsigned long arg0,
			       unsigned long arg1, unsigned long arg2)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(fn, arg0, arg1, arg2, &res);
	return res.a0;
}

static unsigned long psci_forward(struct kvm_cpu_context *host_ctxt)
{
	return psci_call(cpu_reg(host_ctxt, 0), cpu_reg(host_ctxt, 1),
			 cpu_reg(host_ctxt, 2), cpu_reg(host_ctxt, 3));
}

static __noreturn unsigned long psci_forward_noreturn(struct kvm_cpu_context *host_ctxt)
{
	psci_forward(host_ctxt);
	hyp_panic(); /* unreachable */
}

static unsigned long psci_0_1_handler(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	if ((func_id == kvm_host_psci_0_1_function_ids.cpu_off) ||
	    (func_id == kvm_host_psci_0_1_function_ids.migrate))
		return psci_forward(host_ctxt);
	else
		return PSCI_RET_NOT_SUPPORTED;
}

static unsigned long psci_0_2_handler(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	switch (func_id) {
	case PSCI_0_2_FN_PSCI_VERSION:
	case PSCI_0_2_FN_CPU_OFF:
	case PSCI_0_2_FN64_AFFINITY_INFO:
	case PSCI_0_2_FN64_MIGRATE:
	case PSCI_0_2_FN_MIGRATE_INFO_TYPE:
	case PSCI_0_2_FN64_MIGRATE_INFO_UP_CPU:
		return psci_forward(host_ctxt);
	case PSCI_0_2_FN_SYSTEM_OFF:
	case PSCI_0_2_FN_SYSTEM_RESET:
		psci_forward_noreturn(host_ctxt);
		unreachable();
	default:
		return PSCI_RET_NOT_SUPPORTED;
	}
}

static unsigned long psci_1_0_handler(u64 func_id, struct kvm_cpu_context *host_ctxt)
{
	switch (func_id) {
	case PSCI_1_0_FN_PSCI_FEATURES:
	case PSCI_1_0_FN_SET_SUSPEND_MODE:
	case PSCI_1_1_FN64_SYSTEM_RESET2:
		return psci_forward(host_ctxt);
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
