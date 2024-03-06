/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(KVM_X86_PMU_OP) || !defined(KVM_X86_PMU_OP_OPTIONAL)
BUILD_BUG_ON(1)
#endif

/*
 * KVM_X86_PMU_OP() and KVM_X86_PMU_OP_OPTIONAL() are used to help generate
 * both DECLARE/DEFINE_STATIC_CALL() invocations and
 * "static_call_update()" calls.
 *
 * KVM_X86_PMU_OP_OPTIONAL() can be used for those functions that can have
 * a NULL definition, for example if "static_call_cond()" will be used
 * at the call sites.
 */
KVM_X86_PMU_OP(hw_event_available)
KVM_X86_PMU_OP(pmc_idx_to_pmc)
KVM_X86_PMU_OP(rdpmc_ecx_to_pmc)
KVM_X86_PMU_OP(msr_idx_to_pmc)
KVM_X86_PMU_OP(is_valid_rdpmc_ecx)
KVM_X86_PMU_OP(is_valid_msr)
KVM_X86_PMU_OP(get_msr)
KVM_X86_PMU_OP(set_msr)
KVM_X86_PMU_OP(refresh)
KVM_X86_PMU_OP(init)
KVM_X86_PMU_OP_OPTIONAL(reset)
KVM_X86_PMU_OP_OPTIONAL(deliver_pmi)
KVM_X86_PMU_OP_OPTIONAL(cleanup)

#undef KVM_X86_PMU_OP
#undef KVM_X86_PMU_OP_OPTIONAL
