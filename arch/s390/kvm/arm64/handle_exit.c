// SPDX-License-Identifier: GPL-2.0

#include <linux/kvm_host.h>

#include <arm64/esr.h>
#include <arm64/kvm_emulate.h>

#include "handle_exit.h"

typedef int (*exit_handle_fn)(struct kvm_vcpu *);

#define PSCI_0_2_FN_SYSTEM_OFF		0x84000008
#define PSCI_RET_NOT_SUPPORTED		-1
#define PSCI_RET_INTERNAL_FAILURE	-6
/*
 * Temporary smc/hvc handler. Non-compliant implementation (features missing).
 * Implements only system off so that test programs are able to end their execution.
 * Multi-vCPU power state management and KVM_REQ_SLEEP handling are deferred
 * to future SMP series.
 */
static int kvm_smccc_call_handler(struct kvm_vcpu *vcpu)
{
	u32 func_id = vcpu_get_reg(vcpu, 0);
	u64 val = PSCI_RET_NOT_SUPPORTED;
	int ret = 1;

	if (func_id == PSCI_0_2_FN_SYSTEM_OFF) {
		scoped_guard(spinlock, &vcpu->arch.mp_state_lock) {
			WRITE_ONCE(vcpu->arch.mp_state.mp_state, KVM_MP_STATE_STOPPED);
		}
		memset(&vcpu->run->system_event, 0,
		       sizeof(vcpu->run->system_event));
		vcpu->run->system_event.type = KVM_SYSTEM_EVENT_SHUTDOWN;
		vcpu->run->system_event.ndata = 1;
		vcpu->run->system_event.data[0] = 0;
		vcpu->run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
		val = PSCI_RET_INTERNAL_FAILURE;
		ret = 0;
	}
	vcpu_set_reg(vcpu, 0, val);

	return ret;
}

static int handle_hvc(struct kvm_vcpu *vcpu)
{
	vcpu->stat.hvc_exit_stat++;
	return kvm_smccc_call_handler(vcpu);
}

static int kvm_handle_unknown_ec(struct kvm_vcpu *vcpu)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);

	kvm_pr_unimpl("Unknown exception class: esr: %#016llx -- %s\n",
		      esr, esr_get_class_string(esr));

	kvm_inject_undefined(vcpu);
	return 1;
}

static exit_handle_fn arm_exit_handlers[] = {
	[0 ... ESR_ELx_EC_MAX]	= kvm_handle_unknown_ec,
	[ESR_ELx_EC_HVC64]	= handle_hvc,
	[ESR_ELx_EC_IABT_LOW]	= kvm_handle_guest_abort,
	[ESR_ELx_EC_DABT_LOW]	= kvm_handle_guest_abort,
};

static exit_handle_fn kvm_get_exit_handler(struct kvm_vcpu *vcpu)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);
	u8 esr_ec = ESR_ELx_EC(esr);

	return arm_exit_handlers[esr_ec];
}

/*
 * We may be single-stepping an emulated instruction. If the emulation
 * has been completed in the kernel, we can return to userspace with a
 * KVM_EXIT_DEBUG, otherwise userspace needs to complete its
 * emulation first.
 */
static int handle_trap_exceptions(struct kvm_vcpu *vcpu)
{
	exit_handle_fn exit_handler;

	exit_handler = kvm_get_exit_handler(vcpu);
	return exit_handler(vcpu);
}

/*
 * Return > 0 to return to guest, < 0 on error, 0 (and set exit_reason) on
 * proper exit to userspace.
 */
int handle_exit(struct kvm_vcpu *vcpu)
{
	u8 icptr = vcpu->arch.sae_block.icptr;
	int ret = 1;

	switch (icptr) {
	case SAE_ICPTR_SPURIOUS:
		break;
	case SAE_ICPTR_VALIDITY:
		WARN_ONCE(true, "SAE: validity intercept. vir: 0x%04x",
			  vcpu->arch.sae_block.vir);
		vcpu->run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		ret = 0;
		break;
	case SAE_ICPTR_GUEST_ADDRESS_SIZE:
	case SAE_ICPTR_HOST_ACCESS_EXCEPTION:
	case SAE_ICPTR_SYNCHRONOUS_EXCEPTION:
		ret = handle_trap_exceptions(vcpu);
		break;
	default:
		vcpu->run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		ret = 0;
	}
	return ret;
}

/* manually copied from arch/arm64/kernel/traps.c */
static const char * const esr_class_str[] = {
	[0 ... ESR_ELx_EC_MAX]		= "UNRECOGNIZED EC",
	[ESR_ELx_EC_UNKNOWN]		= "Unknown/Uncategorized",
	[ESR_ELx_EC_WFx]		= "WFI/WFE",
	[ESR_ELx_EC_FP_ASIMD]		= "ASIMD",
	[ESR_ELx_EC_PAC]		= "PAC",
	[ESR_ELx_EC_BTI]		= "BTI",
	[ESR_ELx_EC_ILL]		= "PSTATE.IL",
	[ESR_ELx_EC_SVC64]		= "SVC (AArch64)",
	[ESR_ELx_EC_HVC64]		= "HVC (AArch64)",
	[ESR_ELx_EC_SMC64]		= "SMC (AArch64)",
	[ESR_ELx_EC_SYS64]		= "MSR/MRS (AArch64)",
	[ESR_ELx_EC_SVE]		= "SVE",
	[ESR_ELx_EC_ERET]		= "ERET/ERETAA/ERETAB",
	[ESR_ELx_EC_FPAC]		= "FPAC",
	[ESR_ELx_EC_SME]		= "SME",
	[ESR_ELx_EC_IMP_DEF]		= "EL3 IMP DEF",
	[ESR_ELx_EC_IABT_LOW]		= "IABT (lower EL)",
	[ESR_ELx_EC_IABT_CUR]		= "IABT (current EL)",
	[ESR_ELx_EC_PC_ALIGN]		= "PC Alignment",
	[ESR_ELx_EC_DABT_LOW]		= "DABT (lower EL)",
	[ESR_ELx_EC_DABT_CUR]		= "DABT (current EL)",
	[ESR_ELx_EC_SP_ALIGN]		= "SP Alignment",
	[ESR_ELx_EC_MOPS]		= "MOPS",
	[ESR_ELx_EC_FP_EXC64]		= "FP (AArch64)",
	[ESR_ELx_EC_GCS]		= "Guarded Control Stack",
	[ESR_ELx_EC_SERROR]		= "SError",
	[ESR_ELx_EC_BREAKPT_LOW]	= "Breakpoint (lower EL)",
	[ESR_ELx_EC_BREAKPT_CUR]	= "Breakpoint (current EL)",
	[ESR_ELx_EC_SOFTSTP_LOW]	= "Software Step (lower EL)",
	[ESR_ELx_EC_SOFTSTP_CUR]	= "Software Step (current EL)",
	[ESR_ELx_EC_WATCHPT_LOW]	= "Watchpoint (lower EL)",
	[ESR_ELx_EC_WATCHPT_CUR]	= "Watchpoint (current EL)",
	[ESR_ELx_EC_BRK64]		= "BRK (AArch64)",
};

const char *esr_get_class_string(unsigned long esr)
{
	return esr_class_str[ESR_ELx_EC(esr)];
}
