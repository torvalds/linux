// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015,2019 The Linux Foundation. All rights reserved.
 */

#include <linux/cleanup.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/firmware/qcom/qcom_tzmem.h>
#include <linux/arm-smccc.h>
#include <linux/dma-mapping.h>

#include "qcom_scm.h"

/**
 * struct arm_smccc_args
 * @args:	The array of values used in registers in smc instruction
 */
struct arm_smccc_args {
	unsigned long args[8];
};

static DEFINE_MUTEX(qcom_scm_lock);

#define QCOM_SCM_EBUSY_WAIT_MS 30
#define QCOM_SCM_EBUSY_MAX_RETRY 20

#define SCM_SMC_N_REG_ARGS	4
#define SCM_SMC_FIRST_EXT_IDX	(SCM_SMC_N_REG_ARGS - 1)
#define SCM_SMC_N_EXT_ARGS	(MAX_QCOM_SCM_ARGS - SCM_SMC_N_REG_ARGS + 1)
#define SCM_SMC_FIRST_REG_IDX	2
#define SCM_SMC_LAST_REG_IDX	(SCM_SMC_FIRST_REG_IDX + SCM_SMC_N_REG_ARGS - 1)

static void __scm_smc_do_quirk(const struct arm_smccc_args *smc,
			       struct arm_smccc_res *res)
{
	unsigned long a0 = smc->args[0];
	struct arm_smccc_quirk quirk = { .id = ARM_SMCCC_QUIRK_QCOM_A6 };

	quirk.state.a6 = 0;

	do {
		arm_smccc_smc_quirk(a0, smc->args[1], smc->args[2],
				    smc->args[3], smc->args[4], smc->args[5],
				    quirk.state.a6, smc->args[7], res, &quirk);

		if (res->a0 == QCOM_SCM_INTERRUPTED)
			a0 = res->a0;

	} while (res->a0 == QCOM_SCM_INTERRUPTED);
}

static void fill_wq_resume_args(struct arm_smccc_args *resume, u32 smc_call_ctx)
{
	memset(resume->args, 0, sizeof(resume->args[0]) * ARRAY_SIZE(resume->args));

	resume->args[0] = ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL,
					ARM_SMCCC_SMC_64, ARM_SMCCC_OWNER_SIP,
					SCM_SMC_FNID(QCOM_SCM_SVC_WAITQ, QCOM_SCM_WAITQ_RESUME));

	resume->args[1] = QCOM_SCM_ARGS(1);

	resume->args[2] = smc_call_ctx;
}

int scm_get_wq_ctx(u32 *wq_ctx, u32 *flags, u32 *more_pending)
{
	int ret;
	struct arm_smccc_res get_wq_res;
	struct arm_smccc_args get_wq_ctx = {0};

	get_wq_ctx.args[0] = ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,
				ARM_SMCCC_SMC_64, ARM_SMCCC_OWNER_SIP,
				SCM_SMC_FNID(QCOM_SCM_SVC_WAITQ, QCOM_SCM_WAITQ_GET_WQ_CTX));

	/* Guaranteed to return only success or error, no WAITQ_* */
	__scm_smc_do_quirk(&get_wq_ctx, &get_wq_res);
	ret = get_wq_res.a0;
	if (ret)
		return ret;

	*wq_ctx = get_wq_res.a1;
	*flags  = get_wq_res.a2;
	*more_pending = get_wq_res.a3;

	return 0;
}

static int __scm_smc_do_quirk_handle_waitq(struct device *dev, struct arm_smccc_args *waitq,
					   struct arm_smccc_res *res)
{
	int ret;
	u32 wq_ctx, smc_call_ctx;
	struct arm_smccc_args resume;
	struct arm_smccc_args *smc = waitq;

	do {
		__scm_smc_do_quirk(smc, res);

		if (res->a0 == QCOM_SCM_WAITQ_SLEEP) {
			wq_ctx = res->a1;
			smc_call_ctx = res->a2;

			ret = qcom_scm_wait_for_wq_completion(wq_ctx);
			if (ret)
				return ret;

			fill_wq_resume_args(&resume, smc_call_ctx);
			smc = &resume;
		}
	} while (res->a0 == QCOM_SCM_WAITQ_SLEEP);

	return 0;
}

static int __scm_smc_do(struct device *dev, struct arm_smccc_args *smc,
			struct arm_smccc_res *res, bool atomic)
{
	int ret, retry_count = 0;

	if (atomic) {
		__scm_smc_do_quirk(smc, res);
		return 0;
	}

	do {
		mutex_lock(&qcom_scm_lock);

		ret = __scm_smc_do_quirk_handle_waitq(dev, smc, res);

		mutex_unlock(&qcom_scm_lock);

		if (ret)
			return ret;

		if (res->a0 == QCOM_SCM_V2_EBUSY) {
			if (retry_count++ > QCOM_SCM_EBUSY_MAX_RETRY)
				break;
			msleep(QCOM_SCM_EBUSY_WAIT_MS);
		}
	}  while (res->a0 == QCOM_SCM_V2_EBUSY);

	return 0;
}


int __scm_smc_call(struct device *dev, const struct qcom_scm_desc *desc,
		   enum qcom_scm_convention qcom_convention,
		   struct qcom_scm_res *res, bool atomic)
{
	struct qcom_tzmem_pool *mempool = qcom_scm_get_tzmem_pool();
	int arglen = desc->arginfo & 0xf;
	int i, ret;
	void *args_virt __free(qcom_tzmem) = NULL;
	gfp_t flag = atomic ? GFP_ATOMIC : GFP_KERNEL;
	u32 smccc_call_type = atomic ? ARM_SMCCC_FAST_CALL : ARM_SMCCC_STD_CALL;
	u32 qcom_smccc_convention = (qcom_convention == SMC_CONVENTION_ARM_32) ?
				    ARM_SMCCC_SMC_32 : ARM_SMCCC_SMC_64;
	struct arm_smccc_res smc_res;
	struct arm_smccc_args smc = {0};

	smc.args[0] = ARM_SMCCC_CALL_VAL(
		smccc_call_type,
		qcom_smccc_convention,
		desc->owner,
		SCM_SMC_FNID(desc->svc, desc->cmd));
	smc.args[1] = desc->arginfo;
	for (i = 0; i < SCM_SMC_N_REG_ARGS; i++)
		smc.args[i + SCM_SMC_FIRST_REG_IDX] = desc->args[i];

	if (unlikely(arglen > SCM_SMC_N_REG_ARGS)) {
		args_virt = qcom_tzmem_alloc(mempool,
					     SCM_SMC_N_EXT_ARGS * sizeof(u64),
					     flag);
		if (!args_virt)
			return -ENOMEM;

		if (qcom_smccc_convention == ARM_SMCCC_SMC_32) {
			__le32 *args = args_virt;

			for (i = 0; i < SCM_SMC_N_EXT_ARGS; i++)
				args[i] = cpu_to_le32(desc->args[i +
						      SCM_SMC_FIRST_EXT_IDX]);
		} else {
			__le64 *args = args_virt;

			for (i = 0; i < SCM_SMC_N_EXT_ARGS; i++)
				args[i] = cpu_to_le64(desc->args[i +
						      SCM_SMC_FIRST_EXT_IDX]);
		}

		smc.args[SCM_SMC_LAST_REG_IDX] = qcom_tzmem_to_phys(args_virt);
	}

	ret = __scm_smc_do(dev, &smc, &smc_res, atomic);
	if (ret)
		return ret;

	if (res) {
		res->result[0] = smc_res.a1;
		res->result[1] = smc_res.a2;
		res->result[2] = smc_res.a3;
	}

	return (long)smc_res.a0 ? qcom_scm_remap_error(smc_res.a0) : 0;

}
