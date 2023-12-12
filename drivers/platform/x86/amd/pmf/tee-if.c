// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Platform Management Framework Driver - TEE Interface
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include <linux/tee_drv.h>
#include <linux/uuid.h>
#include "pmf.h"

#define MAX_TEE_PARAM	4

/* Policy binary actions sampling frequency (in ms) */
static int pb_actions_ms = MSEC_PER_SEC;
#ifdef CONFIG_AMD_PMF_DEBUG
module_param(pb_actions_ms, int, 0644);
MODULE_PARM_DESC(pb_actions_ms, "Policy binary actions sampling frequency (default = 1000ms)");
#endif

static const uuid_t amd_pmf_ta_uuid = UUID_INIT(0x6fd93b77, 0x3fb8, 0x524d,
						0xb1, 0x2d, 0xc5, 0x29, 0xb1, 0x3d, 0x85, 0x43);

static void amd_pmf_prepare_args(struct amd_pmf_dev *dev, int cmd,
				 struct tee_ioctl_invoke_arg *arg,
				 struct tee_param *param)
{
	memset(arg, 0, sizeof(*arg));
	memset(param, 0, MAX_TEE_PARAM * sizeof(*param));

	arg->func = cmd;
	arg->session = dev->session_id;
	arg->num_params = MAX_TEE_PARAM;

	/* Fill invoke cmd params */
	param[0].u.memref.size = sizeof(struct ta_pmf_shared_memory);
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.shm = dev->fw_shm_pool;
	param[0].u.memref.shm_offs = 0;
}

static int amd_pmf_invoke_cmd_enact(struct amd_pmf_dev *dev)
{
	struct ta_pmf_shared_memory *ta_sm = NULL;
	struct tee_param param[MAX_TEE_PARAM];
	struct tee_ioctl_invoke_arg arg;
	int ret = 0;

	if (!dev->tee_ctx)
		return -ENODEV;

	ta_sm = dev->shbuf;
	memset(ta_sm, 0, sizeof(*ta_sm));
	ta_sm->command_id = TA_PMF_COMMAND_POLICY_BUILDER_ENACT_POLICIES;
	ta_sm->if_version = PMF_TA_IF_VERSION_MAJOR;

	amd_pmf_prepare_args(dev, TA_PMF_COMMAND_POLICY_BUILDER_ENACT_POLICIES, &arg, param);

	ret = tee_client_invoke_func(dev->tee_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(dev->dev, "TEE enact cmd failed. err: %x, ret:%d\n", arg.ret, ret);
		return ret;
	}

	return 0;
}

static int amd_pmf_invoke_cmd_init(struct amd_pmf_dev *dev)
{
	struct ta_pmf_shared_memory *ta_sm = NULL;
	struct tee_param param[MAX_TEE_PARAM];
	struct tee_ioctl_invoke_arg arg;
	int ret = 0;

	if (!dev->tee_ctx) {
		dev_err(dev->dev, "Failed to get TEE context\n");
		return -ENODEV;
	}

	ta_sm = dev->shbuf;
	ta_sm->command_id = TA_PMF_COMMAND_POLICY_BUILDER_INITIALIZE;
	ta_sm->if_version = PMF_TA_IF_VERSION_MAJOR;

	amd_pmf_prepare_args(dev, TA_PMF_COMMAND_POLICY_BUILDER_INITIALIZE, &arg, param);

	ret = tee_client_invoke_func(dev->tee_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(dev->dev, "Failed to invoke TEE init cmd. err: %x, ret:%d\n", arg.ret, ret);
		return ret;
	}

	return ta_sm->pmf_result;
}

static void amd_pmf_invoke_cmd(struct work_struct *work)
{
	struct amd_pmf_dev *dev = container_of(work, struct amd_pmf_dev, pb_work.work);

	amd_pmf_invoke_cmd_enact(dev);
	schedule_delayed_work(&dev->pb_work, msecs_to_jiffies(pb_actions_ms));
}

static int amd_pmf_amdtee_ta_match(struct tee_ioctl_version_data *ver, const void *data)
{
	return ver->impl_id == TEE_IMPL_ID_AMDTEE;
}

static int amd_pmf_ta_open_session(struct tee_context *ctx, u32 *id)
{
	struct tee_ioctl_open_session_arg sess_arg = {};
	int rc;

	export_uuid(sess_arg.uuid, &amd_pmf_ta_uuid);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	rc = tee_client_open_session(ctx, &sess_arg, NULL);
	if (rc < 0 || sess_arg.ret != 0) {
		pr_err("Failed to open TEE session err:%#x, rc:%d\n", sess_arg.ret, rc);
		return rc;
	}

	*id = sess_arg.session;

	return rc;
}

static int amd_pmf_tee_init(struct amd_pmf_dev *dev)
{
	u32 size;
	int ret;

	dev->tee_ctx = tee_client_open_context(NULL, amd_pmf_amdtee_ta_match, NULL, NULL);
	if (IS_ERR(dev->tee_ctx)) {
		dev_err(dev->dev, "Failed to open TEE context\n");
		return PTR_ERR(dev->tee_ctx);
	}

	ret = amd_pmf_ta_open_session(dev->tee_ctx, &dev->session_id);
	if (ret) {
		dev_err(dev->dev, "Failed to open TA session (%d)\n", ret);
		ret = -EINVAL;
		goto out_ctx;
	}

	size = sizeof(struct ta_pmf_shared_memory);
	dev->fw_shm_pool = tee_shm_alloc_kernel_buf(dev->tee_ctx, size);
	if (IS_ERR(dev->fw_shm_pool)) {
		dev_err(dev->dev, "Failed to alloc TEE shared memory\n");
		ret = PTR_ERR(dev->fw_shm_pool);
		goto out_sess;
	}

	dev->shbuf = tee_shm_get_va(dev->fw_shm_pool, 0);
	if (IS_ERR(dev->shbuf)) {
		dev_err(dev->dev, "Failed to get TEE virtual address\n");
		ret = PTR_ERR(dev->shbuf);
		goto out_shm;
	}
	dev_dbg(dev->dev, "TEE init done\n");

	return 0;

out_shm:
	tee_shm_free(dev->fw_shm_pool);
out_sess:
	tee_client_close_session(dev->tee_ctx, dev->session_id);
out_ctx:
	tee_client_close_context(dev->tee_ctx);

	return ret;
}

static void amd_pmf_tee_deinit(struct amd_pmf_dev *dev)
{
	tee_shm_free(dev->fw_shm_pool);
	tee_client_close_session(dev->tee_ctx, dev->session_id);
	tee_client_close_context(dev->tee_ctx);
}

int amd_pmf_init_smart_pc(struct amd_pmf_dev *dev)
{
	int ret;

	ret = amd_pmf_tee_init(dev);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&dev->pb_work, amd_pmf_invoke_cmd);
	return 0;
}

void amd_pmf_deinit_smart_pc(struct amd_pmf_dev *dev)
{
	cancel_delayed_work_sync(&dev->pb_work);
	amd_pmf_tee_deinit(dev);
}
