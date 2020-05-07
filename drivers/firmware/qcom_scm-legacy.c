// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2010,2015,2019 The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 */

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/qcom_scm.h>
#include <linux/arm-smccc.h>
#include <linux/dma-mapping.h>

#include "qcom_scm.h"

static DEFINE_MUTEX(qcom_scm_lock);


/**
 * struct arm_smccc_args
 * @args:	The array of values used in registers in smc instruction
 */
struct arm_smccc_args {
	unsigned long args[8];
};


/**
 * struct scm_legacy_command - one SCM command buffer
 * @len: total available memory for command and response
 * @buf_offset: start of command buffer
 * @resp_hdr_offset: start of response buffer
 * @id: command to be executed
 * @buf: buffer returned from scm_legacy_get_command_buffer()
 *
 * An SCM command is laid out in memory as follows:
 *
 *	------------------- <--- struct scm_legacy_command
 *	| command header  |
 *	------------------- <--- scm_legacy_get_command_buffer()
 *	| command buffer  |
 *	------------------- <--- struct scm_legacy_response and
 *	| response header |      scm_legacy_command_to_response()
 *	------------------- <--- scm_legacy_get_response_buffer()
 *	| response buffer |
 *	-------------------
 *
 * There can be arbitrary padding between the headers and buffers so
 * you should always use the appropriate scm_legacy_get_*_buffer() routines
 * to access the buffers in a safe manner.
 */
struct scm_legacy_command {
	__le32 len;
	__le32 buf_offset;
	__le32 resp_hdr_offset;
	__le32 id;
	__le32 buf[0];
};

/**
 * struct scm_legacy_response - one SCM response buffer
 * @len: total available memory for response
 * @buf_offset: start of response data relative to start of scm_legacy_response
 * @is_complete: indicates if the command has finished processing
 */
struct scm_legacy_response {
	__le32 len;
	__le32 buf_offset;
	__le32 is_complete;
};

/**
 * scm_legacy_command_to_response() - Get a pointer to a scm_legacy_response
 * @cmd: command
 *
 * Returns a pointer to a response for a command.
 */
static inline struct scm_legacy_response *scm_legacy_command_to_response(
		const struct scm_legacy_command *cmd)
{
	return (void *)cmd + le32_to_cpu(cmd->resp_hdr_offset);
}

/**
 * scm_legacy_get_command_buffer() - Get a pointer to a command buffer
 * @cmd: command
 *
 * Returns a pointer to the command buffer of a command.
 */
static inline void *scm_legacy_get_command_buffer(
		const struct scm_legacy_command *cmd)
{
	return (void *)cmd->buf;
}

/**
 * scm_legacy_get_response_buffer() - Get a pointer to a response buffer
 * @rsp: response
 *
 * Returns a pointer to a response buffer of a response.
 */
static inline void *scm_legacy_get_response_buffer(
		const struct scm_legacy_response *rsp)
{
	return (void *)rsp + le32_to_cpu(rsp->buf_offset);
}

static void __scm_legacy_do(const struct arm_smccc_args *smc,
			    struct arm_smccc_res *res)
{
	do {
		arm_smccc_smc(smc->args[0], smc->args[1], smc->args[2],
			      smc->args[3], smc->args[4], smc->args[5],
			      smc->args[6], smc->args[7], res);
	} while (res->a0 == QCOM_SCM_INTERRUPTED);
}

/**
 * qcom_scm_call() - Sends a command to the SCM and waits for the command to
 * finish processing.
 *
 * A note on cache maintenance:
 * Note that any buffers that are expected to be accessed by the secure world
 * must be flushed before invoking qcom_scm_call and invalidated in the cache
 * immediately after qcom_scm_call returns. Cache maintenance on the command
 * and response buffers is taken care of by qcom_scm_call; however, callers are
 * responsible for any other cached buffers passed over to the secure world.
 */
int scm_legacy_call(struct device *dev, const struct qcom_scm_desc *desc,
		    struct qcom_scm_res *res)
{
	u8 arglen = desc->arginfo & 0xf;
	int ret = 0, context_id;
	unsigned int i;
	struct scm_legacy_command *cmd;
	struct scm_legacy_response *rsp;
	struct arm_smccc_args smc = {0};
	struct arm_smccc_res smc_res;
	const size_t cmd_len = arglen * sizeof(__le32);
	const size_t resp_len = MAX_QCOM_SCM_RETS * sizeof(__le32);
	size_t alloc_len = sizeof(*cmd) + cmd_len + sizeof(*rsp) + resp_len;
	dma_addr_t cmd_phys;
	__le32 *arg_buf;
	const __le32 *res_buf;

	cmd = kzalloc(PAGE_ALIGN(alloc_len), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->len = cpu_to_le32(alloc_len);
	cmd->buf_offset = cpu_to_le32(sizeof(*cmd));
	cmd->resp_hdr_offset = cpu_to_le32(sizeof(*cmd) + cmd_len);
	cmd->id = cpu_to_le32(SCM_LEGACY_FNID(desc->svc, desc->cmd));

	arg_buf = scm_legacy_get_command_buffer(cmd);
	for (i = 0; i < arglen; i++)
		arg_buf[i] = cpu_to_le32(desc->args[i]);

	rsp = scm_legacy_command_to_response(cmd);

	cmd_phys = dma_map_single(dev, cmd, alloc_len, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, cmd_phys)) {
		kfree(cmd);
		return -ENOMEM;
	}

	smc.args[0] = 1;
	smc.args[1] = (unsigned long)&context_id;
	smc.args[2] = cmd_phys;

	mutex_lock(&qcom_scm_lock);
	__scm_legacy_do(&smc, &smc_res);
	if (smc_res.a0)
		ret = qcom_scm_remap_error(smc_res.a0);
	mutex_unlock(&qcom_scm_lock);
	if (ret)
		goto out;

	do {
		dma_sync_single_for_cpu(dev, cmd_phys + sizeof(*cmd) + cmd_len,
					sizeof(*rsp), DMA_FROM_DEVICE);
	} while (!rsp->is_complete);

	dma_sync_single_for_cpu(dev, cmd_phys + sizeof(*cmd) + cmd_len +
				le32_to_cpu(rsp->buf_offset),
				resp_len, DMA_FROM_DEVICE);

	if (res) {
		res_buf = scm_legacy_get_response_buffer(rsp);
		for (i = 0; i < MAX_QCOM_SCM_RETS; i++)
			res->result[i] = le32_to_cpu(res_buf[i]);
	}
out:
	dma_unmap_single(dev, cmd_phys, alloc_len, DMA_TO_DEVICE);
	kfree(cmd);
	return ret;
}

#define SCM_LEGACY_ATOMIC_N_REG_ARGS	5
#define SCM_LEGACY_ATOMIC_FIRST_REG_IDX	2
#define SCM_LEGACY_CLASS_REGISTER		(0x2 << 8)
#define SCM_LEGACY_MASK_IRQS		BIT(5)
#define SCM_LEGACY_ATOMIC_ID(svc, cmd, n) \
				((SCM_LEGACY_FNID(svc, cmd) << 12) | \
				SCM_LEGACY_CLASS_REGISTER | \
				SCM_LEGACY_MASK_IRQS | \
				(n & 0xf))

/**
 * qcom_scm_call_atomic() - Send an atomic SCM command with up to 5 arguments
 * and 3 return values
 * @desc: SCM call descriptor containing arguments
 * @res:  SCM call return values
 *
 * This shall only be used with commands that are guaranteed to be
 * uninterruptable, atomic and SMP safe.
 */
int scm_legacy_call_atomic(struct device *unused,
			   const struct qcom_scm_desc *desc,
			   struct qcom_scm_res *res)
{
	int context_id;
	struct arm_smccc_res smc_res;
	size_t arglen = desc->arginfo & 0xf;

	BUG_ON(arglen > SCM_LEGACY_ATOMIC_N_REG_ARGS);

	arm_smccc_smc(SCM_LEGACY_ATOMIC_ID(desc->svc, desc->cmd, arglen),
		      (unsigned long)&context_id,
		      desc->args[0], desc->args[1], desc->args[2],
		      desc->args[3], desc->args[4], 0, &smc_res);

	if (res) {
		res->result[0] = smc_res.a1;
		res->result[1] = smc_res.a2;
		res->result[2] = smc_res.a3;
	}

	return smc_res.a0;
}
