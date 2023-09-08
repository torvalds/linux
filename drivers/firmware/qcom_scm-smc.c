// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015,2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/io.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/qcom_scm.h>
#include <linux/arm-smccc.h>
#include <linux/dma-mapping.h>
#include <linux/qtee_shmbridge.h>
#include <linux/qcom_scm_hab.h>

#include "qcom_scm.h"

static bool hab_calling_convention;

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
	bool atomic = ARM_SMCCC_IS_FAST_CALL(smc->args[0]) ? true : false;

	quirk.state.a6 = 0;

	if (hab_calling_convention) {
		scm_call_qcpe(smc, res, atomic);
	} else {
		do {
			arm_smccc_smc_quirk(a0, smc->args[1], smc->args[2],
					smc->args[3], smc->args[4],
					smc->args[5], quirk.state.a6,
					smc->args[7], res, &quirk);
			if (res->a0 == QCOM_SCM_INTERRUPTED)
				a0 = res->a0;
		} while (res->a0 == QCOM_SCM_INTERRUPTED);
	}

}

#define IS_WAITQ_SLEEP_OR_WAKE(res) \
	(res->a0 == QCOM_SCM_WAITQ_SLEEP || res->a0 == QCOM_SCM_WAITQ_WAKE)

static void fill_wq_resume_args(struct arm_smccc_args *resume, u32 smc_call_ctx)
{
	memset(resume->args, 0, ARRAY_SIZE(resume->args));

	resume->args[0] = ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL,
			 ARM_SMCCC_SMC_64, ARM_SMCCC_OWNER_SIP,
			 SCM_SMC_FNID(QCOM_SCM_SVC_WAITQ, QCOM_SCM_WAITQ_RESUME));

	resume->args[1] = QCOM_SCM_ARGS(1);

	resume->args[2] = smc_call_ctx;
}

static void fill_wq_wake_ack_args(struct arm_smccc_args *wake_ack, u32 smc_call_ctx)
{
	memset(wake_ack->args, 0, ARRAY_SIZE(wake_ack->args));

	wake_ack->args[0] = ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL,
			 ARM_SMCCC_SMC_64, ARM_SMCCC_OWNER_SIP,
			 SCM_SMC_FNID(QCOM_SCM_SVC_WAITQ, QCOM_SCM_WAITQ_ACK));

	wake_ack->args[1] = QCOM_SCM_ARGS(1);

	wake_ack->args[2] = smc_call_ctx;
}

static void fill_get_wq_ctx_args(struct arm_smccc_args *get_wq_ctx)
{
	memset(get_wq_ctx->args, 0, ARRAY_SIZE(get_wq_ctx->args));

	get_wq_ctx->args[0] = ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,
			 ARM_SMCCC_SMC_64, ARM_SMCCC_OWNER_SIP,
			 SCM_SMC_FNID(QCOM_SCM_SVC_WAITQ, QCOM_SCM_WAITQ_GET_WQ_CTX));
}

int scm_get_wq_ctx(u32 *wq_ctx, u32 *flags, u32 *more_pending)
{
	int ret;
	struct arm_smccc_args get_wq_ctx = {0};
	struct arm_smccc_res get_wq_res;

	fill_get_wq_ctx_args(&get_wq_ctx);

	__scm_smc_do_quirk(&get_wq_ctx, &get_wq_res);
	/* Guaranteed to return only success or error, no WAITQ_* */
	ret = get_wq_res.a0;
	if (ret)
		return ret;

	*wq_ctx = get_wq_res.a1;
	*flags  = get_wq_res.a2;
	*more_pending = get_wq_res.a3;

	return 0;
}

static int scm_smc_do_quirk(struct device *dev, struct arm_smccc_args *smc,
		    struct arm_smccc_res *res)
{
	struct completion *wq = NULL;
	struct qcom_scm *qscm;
	struct arm_smccc_args original = *smc;
	u32 wq_ctx, smc_call_ctx, flags;

	do {
		__scm_smc_do_quirk(smc, res);

		if (IS_WAITQ_SLEEP_OR_WAKE(res)) {
			wq_ctx = res->a1;
			smc_call_ctx = res->a2;
			flags = res->a3;

			if (!dev)
				return -EPROBE_DEFER;

			qscm = dev_get_drvdata(dev);
			wq = qcom_scm_lookup_wq(qscm, wq_ctx);
			if (IS_ERR_OR_NULL(wq)) {
				pr_err("Did not find waitqueue for wq_ctx %d: %d\n",
						wq_ctx, PTR_ERR(wq));
				return PTR_ERR(wq);
			}

			if (res->a0 == QCOM_SCM_WAITQ_SLEEP) {
				wait_for_completion(wq);
				fill_wq_resume_args(smc, smc_call_ctx);
				continue;
			} else {
				fill_wq_wake_ack_args(smc, smc_call_ctx);
				scm_waitq_flag_handler(wq, flags);
				continue;
			}
		} else if ((long)res->a0 < 0) {
			/* Error, return to caller with original SMC call */
			*smc = original;
			break;
		} else
			return 0;
	} while (IS_WAITQ_SLEEP_OR_WAKE(res));

	return 0;
}

static int __scm_smc_do(struct device *dev, struct arm_smccc_args *smc,
			 struct arm_smccc_res *res,
			 enum qcom_scm_call_type call_type,
			 bool multicall_allowed)
{
	int ret, retry_count = 0;
	bool multi_smc_call = qcom_scm_multi_call_allow(dev, multicall_allowed);

	if (call_type == QCOM_SCM_CALL_ATOMIC) {
		__scm_smc_do_quirk(smc, res);
		return 0;
	}

	do {
		if (!multi_smc_call)
			mutex_lock(&qcom_scm_lock);
		down(&qcom_scm_sem_lock);
		ret = scm_smc_do_quirk(dev, smc, res);
		up(&qcom_scm_sem_lock);
		if (!multi_smc_call)
			mutex_unlock(&qcom_scm_lock);
		if (ret)
			return ret;

		if (res->a0 == QCOM_SCM_V2_EBUSY) {
			if (retry_count++ > QCOM_SCM_EBUSY_MAX_RETRY ||
				(call_type == QCOM_SCM_CALL_NORETRY))
				break;
			msleep(QCOM_SCM_EBUSY_WAIT_MS);
		}
	}  while (res->a0 == QCOM_SCM_V2_EBUSY);

	return 0;
}

int __scm_smc_call(struct device *dev, const struct qcom_scm_desc *desc,
		   enum qcom_scm_convention qcom_convention,
		   struct qcom_scm_res *res, enum qcom_scm_call_type call_type)
{
	int arglen = desc->arginfo & 0xf;
	int i, ret;
	struct qtee_shm shm = {0};
	bool use_qtee_shmbridge;
	size_t alloc_len;
	const bool atomic = (call_type == QCOM_SCM_CALL_ATOMIC);
	gfp_t flag = atomic ? GFP_ATOMIC : GFP_NOIO;
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
		if (!dev)
			return -EPROBE_DEFER;

		alloc_len = SCM_SMC_N_EXT_ARGS * sizeof(u64);
		use_qtee_shmbridge = qtee_shmbridge_is_enabled();
		if (use_qtee_shmbridge) {
			ret = qtee_shmbridge_allocate_shm(alloc_len, &shm);
			if (ret)
				return ret;
		} else {
			shm.vaddr = kzalloc(PAGE_ALIGN(alloc_len), flag);
			if (!shm.vaddr)
				return -ENOMEM;
		}


		if (qcom_smccc_convention == ARM_SMCCC_SMC_32) {
			__le32 *args = shm.vaddr;

			for (i = 0; i < SCM_SMC_N_EXT_ARGS; i++)
				args[i] = cpu_to_le32(desc->args[i +
						      SCM_SMC_FIRST_EXT_IDX]);
		} else {
			__le64 *args = shm.vaddr;

			for (i = 0; i < SCM_SMC_N_EXT_ARGS; i++)
				args[i] = cpu_to_le64(desc->args[i +
						      SCM_SMC_FIRST_EXT_IDX]);
		}

		shm.paddr = dma_map_single(dev, shm.vaddr, alloc_len,
					   DMA_TO_DEVICE);

		if (dma_mapping_error(dev, shm.paddr)) {
			if (use_qtee_shmbridge)
				qtee_shmbridge_free_shm(&shm);
			else
				kfree(shm.vaddr);
			return -ENOMEM;
		}

		smc.args[SCM_SMC_LAST_REG_IDX] = shm.paddr;
	}

	ret = __scm_smc_do(dev, &smc, &smc_res, call_type, desc->multicall_allowed);
	/* ret error check follows shm cleanup */

	if (shm.vaddr) {
		dma_unmap_single(dev, shm.paddr, alloc_len, DMA_TO_DEVICE);
		if (use_qtee_shmbridge)
			qtee_shmbridge_free_shm(&shm);
		else
			kfree(shm.vaddr);
	}

	if (ret)
		return ret;

	if (res) {
		res->result[0] = smc_res.a1;
		res->result[1] = smc_res.a2;
		res->result[2] = smc_res.a3;
	}

	ret = (long)smc_res.a0 ? qcom_scm_remap_error(smc_res.a0) : 0;

	return ret;
}

void __qcom_scm_init(void)
{
	int ret;
	/**
	 * The HAB connection should be opened before first SMC call.
	 * If not, there could be errors that might cause the
	 * system to crash.
	 */
	ret = scm_qcpe_hab_open();
	if (ret != -EOPNOTSUPP) {
		hab_calling_convention = true;
		pr_debug("using HAB channel communication ret = %d\n", ret);
	}

}

void __qcom_scm_qcpe_exit(void)
{
	scm_qcpe_hab_close();
}
