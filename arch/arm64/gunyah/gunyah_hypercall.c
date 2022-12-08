// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/arm-smccc.h>
#include <linux/module.h>
#include <linux/gunyah.h>
#include <linux/uuid.h>

static const uuid_t gh_known_uuids[] = {
	/* Qualcomm's version of Gunyah {19bd54bd-0b37-571b-946f-609b54539de6} */
	UUID_INIT(0x19bd54bd, 0x0b37, 0x571b, 0x94, 0x6f, 0x60, 0x9b, 0x54, 0x53, 0x9d, 0xe6),
	/* Standard version of Gunyah {c1d58fcd-a453-5fdb-9265-ce36673d5f14} */
	UUID_INIT(0xc1d58fcd, 0xa453, 0x5fdb, 0x92, 0x65, 0xce, 0x36, 0x67, 0x3d, 0x5f, 0x14),
};

bool arch_is_gh_guest(void)
{
	struct arm_smccc_res res;
	uuid_t uuid;
	int i;

	arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID, &res);

	((u32 *)&uuid.b[0])[0] = lower_32_bits(res.a0);
	((u32 *)&uuid.b[0])[1] = lower_32_bits(res.a1);
	((u32 *)&uuid.b[0])[2] = lower_32_bits(res.a2);
	((u32 *)&uuid.b[0])[3] = lower_32_bits(res.a3);

	for (i = 0; i < ARRAY_SIZE(gh_known_uuids); i++)
		if (uuid_equal(&uuid, &gh_known_uuids[i]))
			return true;

	return false;
}
EXPORT_SYMBOL_GPL(arch_is_gh_guest);

#define GH_HYPERCALL(fn)	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64, \
						   ARM_SMCCC_OWNER_VENDOR_HYP, \
						   fn)

#define GH_HYPERCALL_HYP_IDENTIFY		GH_HYPERCALL(0x8000)
#define GH_HYPERCALL_BELL_SEND			GH_HYPERCALL(0x8012)
#define GH_HYPERCALL_BELL_SET_MASK		GH_HYPERCALL(0x8015)
#define GH_HYPERCALL_MSGQ_SEND			GH_HYPERCALL(0x801B)
#define GH_HYPERCALL_MSGQ_RECV			GH_HYPERCALL(0x801C)
#define GH_HYPERCALL_VCPU_RUN			GH_HYPERCALL(0x8065)

/**
 * gh_hypercall_hyp_identify() - Returns build information and feature flags
 *                               supported by Gunyah.
 * @hyp_identity: filled by the hypercall with the API info and feature flags.
 */
void gh_hypercall_hyp_identify(struct gh_hypercall_hyp_identify_resp *hyp_identity)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_hvc(GH_HYPERCALL_HYP_IDENTIFY, &res);

	hyp_identity->api_info = res.a0;
	hyp_identity->flags[0] = res.a1;
	hyp_identity->flags[1] = res.a2;
	hyp_identity->flags[2] = res.a3;
}
EXPORT_SYMBOL_GPL(gh_hypercall_hyp_identify);

enum gh_error gh_hypercall_bell_send(u64 capid, u64 new_flags, u64 *old_flags)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_hvc(GH_HYPERCALL_BELL_SEND, capid, new_flags, 0, &res);

	if (res.a0 == GH_ERROR_OK)
		*old_flags = res.a1;

	return res.a0;
}
EXPORT_SYMBOL_GPL(gh_hypercall_bell_send);

enum gh_error gh_hypercall_bell_set_mask(u64 capid, u64 enable_mask, u64 ack_mask)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_hvc(GH_HYPERCALL_BELL_SET_MASK, capid, enable_mask, ack_mask, 0, &res);

	return res.a0;
}
EXPORT_SYMBOL_GPL(gh_hypercall_bell_set_mask);

enum gh_error gh_hypercall_msgq_send(u64 capid, size_t size, void *buff, int tx_flags, bool *ready)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_hvc(GH_HYPERCALL_MSGQ_SEND, capid, size, (uintptr_t)buff, tx_flags, 0, &res);

	if (res.a0 == GH_ERROR_OK)
		*ready = !!res.a1;

	return res.a0;
}
EXPORT_SYMBOL_GPL(gh_hypercall_msgq_send);

enum gh_error gh_hypercall_msgq_recv(u64 capid, void *buff, size_t size, size_t *recv_size,
					bool *ready)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_hvc(GH_HYPERCALL_MSGQ_RECV, capid, (uintptr_t)buff, size, 0, &res);

	if (res.a0 == GH_ERROR_OK) {
		*recv_size = res.a1;
		*ready = !!res.a2;
	}

	return res.a0;
}
EXPORT_SYMBOL_GPL(gh_hypercall_msgq_recv);

enum gh_error gh_hypercall_vcpu_run(u64 capid, u64 *resume_data,
					struct gh_hypercall_vcpu_run_resp *resp)
{
	struct arm_smccc_1_2_regs args = {
		.a0 = GH_HYPERCALL_VCPU_RUN,
		.a1 = capid,
		.a2 = resume_data[0],
		.a3 = resume_data[1],
		.a4 = resume_data[2],
		/* C language says this will be implictly zero. Gunyah requires 0, so be explicit */
		.a5 = 0,
	};
	struct arm_smccc_1_2_regs res;

	arm_smccc_1_2_hvc(&args, &res);

	if (res.a0 == GH_ERROR_OK) {
		resp->state = res.a1;
		resp->state_data[0] = res.a2;
		resp->state_data[1] = res.a3;
		resp->state_data[2] = res.a4;
	}

	return res.a0;
}
EXPORT_SYMBOL_GPL(gh_hypercall_vcpu_run);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gunyah Hypervisor Hypercalls");
