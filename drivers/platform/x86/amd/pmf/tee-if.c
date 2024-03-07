// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Platform Management Framework Driver - TEE Interface
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 */

#include <linux/debugfs.h>
#include <linux/tee_drv.h>
#include <linux/uuid.h>
#include "pmf.h"

#define MAX_TEE_PARAM	4

/* Policy binary actions sampling frequency (in ms) */
static int pb_actions_ms = MSEC_PER_SEC;
/* Sideload policy binaries to debug policy failures */
static bool pb_side_load;

#ifdef CONFIG_AMD_PMF_DEBUG
module_param(pb_actions_ms, int, 0644);
MODULE_PARM_DESC(pb_actions_ms, "Policy binary actions sampling frequency (default = 1000ms)");
module_param(pb_side_load, bool, 0444);
MODULE_PARM_DESC(pb_side_load, "Sideload policy binaries debug policy failures");
#endif

static const uuid_t amd_pmf_ta_uuid = UUID_INIT(0x6fd93b77, 0x3fb8, 0x524d,
						0xb1, 0x2d, 0xc5, 0x29, 0xb1, 0x3d, 0x85, 0x43);

static const char *amd_pmf_uevent_as_str(unsigned int state)
{
	switch (state) {
	case SYSTEM_STATE_S0i3:
		return "S0i3";
	case SYSTEM_STATE_S4:
		return "S4";
	case SYSTEM_STATE_SCREEN_LOCK:
		return "SCREEN_LOCK";
	default:
		return "Unknown Smart PC event";
	}
}

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

static int amd_pmf_update_uevents(struct amd_pmf_dev *dev, u16 event)
{
	char *envp[2] = {};

	envp[0] = kasprintf(GFP_KERNEL, "EVENT_ID=%d", event);
	if (!envp[0])
		return -EINVAL;

	kobject_uevent_env(&dev->dev->kobj, KOBJ_CHANGE, envp);

	kfree(envp[0]);
	return 0;
}

static void amd_pmf_apply_policies(struct amd_pmf_dev *dev, struct ta_pmf_enact_result *out)
{
	u32 val;
	int idx;

	for (idx = 0; idx < out->actions_count; idx++) {
		val = out->actions_list[idx].value;
		switch (out->actions_list[idx].action_index) {
		case PMF_POLICY_SPL:
			if (dev->prev_data->spl != val) {
				amd_pmf_send_cmd(dev, SET_SPL, false, val, NULL);
				dev_dbg(dev->dev, "update SPL: %u\n", val);
				dev->prev_data->spl = val;
			}
			break;

		case PMF_POLICY_SPPT:
			if (dev->prev_data->sppt != val) {
				amd_pmf_send_cmd(dev, SET_SPPT, false, val, NULL);
				dev_dbg(dev->dev, "update SPPT: %u\n", val);
				dev->prev_data->sppt = val;
			}
			break;

		case PMF_POLICY_FPPT:
			if (dev->prev_data->fppt != val) {
				amd_pmf_send_cmd(dev, SET_FPPT, false, val, NULL);
				dev_dbg(dev->dev, "update FPPT: %u\n", val);
				dev->prev_data->fppt = val;
			}
			break;

		case PMF_POLICY_SPPT_APU_ONLY:
			if (dev->prev_data->sppt_apuonly != val) {
				amd_pmf_send_cmd(dev, SET_SPPT_APU_ONLY, false, val, NULL);
				dev_dbg(dev->dev, "update SPPT_APU_ONLY: %u\n", val);
				dev->prev_data->sppt_apuonly = val;
			}
			break;

		case PMF_POLICY_STT_MIN:
			if (dev->prev_data->stt_minlimit != val) {
				amd_pmf_send_cmd(dev, SET_STT_MIN_LIMIT, false, val, NULL);
				dev_dbg(dev->dev, "update STT_MIN: %u\n", val);
				dev->prev_data->stt_minlimit = val;
			}
			break;

		case PMF_POLICY_STT_SKINTEMP_APU:
			if (dev->prev_data->stt_skintemp_apu != val) {
				amd_pmf_send_cmd(dev, SET_STT_LIMIT_APU, false, val, NULL);
				dev_dbg(dev->dev, "update STT_SKINTEMP_APU: %u\n", val);
				dev->prev_data->stt_skintemp_apu = val;
			}
			break;

		case PMF_POLICY_STT_SKINTEMP_HS2:
			if (dev->prev_data->stt_skintemp_hs2 != val) {
				amd_pmf_send_cmd(dev, SET_STT_LIMIT_HS2, false, val, NULL);
				dev_dbg(dev->dev, "update STT_SKINTEMP_HS2: %u\n", val);
				dev->prev_data->stt_skintemp_hs2 = val;
			}
			break;

		case PMF_POLICY_P3T:
			if (dev->prev_data->p3t_limit != val) {
				amd_pmf_send_cmd(dev, SET_P3T, false, val, NULL);
				dev_dbg(dev->dev, "update P3T: %u\n", val);
				dev->prev_data->p3t_limit = val;
			}
			break;

		case PMF_POLICY_SYSTEM_STATE:
			amd_pmf_update_uevents(dev, val);
			dev_dbg(dev->dev, "update SYSTEM_STATE: %s\n",
				amd_pmf_uevent_as_str(val));
			break;
		}
	}
}

static int amd_pmf_invoke_cmd_enact(struct amd_pmf_dev *dev)
{
	struct ta_pmf_shared_memory *ta_sm = NULL;
	struct ta_pmf_enact_result *out = NULL;
	struct ta_pmf_enact_table *in = NULL;
	struct tee_param param[MAX_TEE_PARAM];
	struct tee_ioctl_invoke_arg arg;
	int ret = 0;

	if (!dev->tee_ctx)
		return -ENODEV;

	memset(dev->shbuf, 0, dev->policy_sz);
	ta_sm = dev->shbuf;
	out = &ta_sm->pmf_output.policy_apply_table;
	in = &ta_sm->pmf_input.enact_table;

	memset(ta_sm, 0, sizeof(*ta_sm));
	ta_sm->command_id = TA_PMF_COMMAND_POLICY_BUILDER_ENACT_POLICIES;
	ta_sm->if_version = PMF_TA_IF_VERSION_MAJOR;

	amd_pmf_populate_ta_inputs(dev, in);
	amd_pmf_prepare_args(dev, TA_PMF_COMMAND_POLICY_BUILDER_ENACT_POLICIES, &arg, param);

	ret = tee_client_invoke_func(dev->tee_ctx, &arg, param);
	if (ret < 0 || arg.ret != 0) {
		dev_err(dev->dev, "TEE enact cmd failed. err: %x, ret:%d\n", arg.ret, ret);
		return ret;
	}

	if (ta_sm->pmf_result == TA_PMF_TYPE_SUCCESS && out->actions_count) {
		amd_pmf_dump_ta_inputs(dev, in);
		dev_dbg(dev->dev, "action count:%u result:%x\n", out->actions_count,
			ta_sm->pmf_result);
		amd_pmf_apply_policies(dev, out);
	}

	return 0;
}

static int amd_pmf_invoke_cmd_init(struct amd_pmf_dev *dev)
{
	struct ta_pmf_shared_memory *ta_sm = NULL;
	struct tee_param param[MAX_TEE_PARAM];
	struct ta_pmf_init_table *in = NULL;
	struct tee_ioctl_invoke_arg arg;
	int ret = 0;

	if (!dev->tee_ctx) {
		dev_err(dev->dev, "Failed to get TEE context\n");
		return -ENODEV;
	}

	dev_dbg(dev->dev, "Policy Binary size: %u bytes\n", dev->policy_sz);
	memset(dev->shbuf, 0, dev->policy_sz);
	ta_sm = dev->shbuf;
	in = &ta_sm->pmf_input.init_table;

	ta_sm->command_id = TA_PMF_COMMAND_POLICY_BUILDER_INITIALIZE;
	ta_sm->if_version = PMF_TA_IF_VERSION_MAJOR;

	in->metadata_macrocheck = false;
	in->sku_check = false;
	in->validate = true;
	in->frequency = pb_actions_ms;
	in->policies_table.table_size = dev->policy_sz;

	memcpy(in->policies_table.table, dev->policy_buf, dev->policy_sz);
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

static int amd_pmf_start_policy_engine(struct amd_pmf_dev *dev)
{
	u32 cookie, length;
	int res;

	cookie = readl(dev->policy_buf + POLICY_COOKIE_OFFSET);
	length = readl(dev->policy_buf + POLICY_COOKIE_LEN);

	if (cookie != POLICY_SIGN_COOKIE || !length) {
		dev_dbg(dev->dev, "cookie doesn't match\n");
		return -EINVAL;
	}

	/* Update the actual length */
	dev->policy_sz = length + 512;
	res = amd_pmf_invoke_cmd_init(dev);
	if (res == TA_PMF_TYPE_SUCCESS) {
		/* Now its safe to announce that smart pc is enabled */
		dev->smart_pc_enabled = true;
		/*
		 * Start collecting the data from TA FW after a small delay
		 * or else, we might end up getting stale values.
		 */
		schedule_delayed_work(&dev->pb_work, msecs_to_jiffies(pb_actions_ms * 3));
	} else {
		dev_err(dev->dev, "ta invoke cmd init failed err: %x\n", res);
		dev->smart_pc_enabled = false;
		return res;
	}

	return 0;
}

#ifdef CONFIG_AMD_PMF_DEBUG
static void amd_pmf_hex_dump_pb(struct amd_pmf_dev *dev)
{
	print_hex_dump_debug("(pb):  ", DUMP_PREFIX_OFFSET, 16, 1, dev->policy_buf,
			     dev->policy_sz, false);
}

static ssize_t amd_pmf_get_pb_data(struct file *filp, const char __user *buf,
				   size_t length, loff_t *pos)
{
	struct amd_pmf_dev *dev = filp->private_data;
	unsigned char *new_policy_buf;
	int ret;

	/* Policy binary size cannot exceed POLICY_BUF_MAX_SZ */
	if (length > POLICY_BUF_MAX_SZ || length == 0)
		return -EINVAL;

	/* re-alloc to the new buffer length of the policy binary */
	new_policy_buf = kzalloc(length, GFP_KERNEL);
	if (!new_policy_buf)
		return -ENOMEM;

	if (copy_from_user(new_policy_buf, buf, length)) {
		kfree(new_policy_buf);
		return -EFAULT;
	}

	kfree(dev->policy_buf);
	dev->policy_buf = new_policy_buf;
	dev->policy_sz = length;

	amd_pmf_hex_dump_pb(dev);
	ret = amd_pmf_start_policy_engine(dev);
	if (ret)
		return -EINVAL;

	return length;
}

static const struct file_operations pb_fops = {
	.write = amd_pmf_get_pb_data,
	.open = simple_open,
};

static void amd_pmf_open_pb(struct amd_pmf_dev *dev, struct dentry *debugfs_root)
{
	dev->esbin = debugfs_create_dir("pb", debugfs_root);
	debugfs_create_file("update_policy", 0644, dev->esbin, dev, &pb_fops);
}

static void amd_pmf_remove_pb(struct amd_pmf_dev *dev)
{
	debugfs_remove_recursive(dev->esbin);
}
#else
static void amd_pmf_open_pb(struct amd_pmf_dev *dev, struct dentry *debugfs_root) {}
static void amd_pmf_remove_pb(struct amd_pmf_dev *dev) {}
static void amd_pmf_hex_dump_pb(struct amd_pmf_dev *dev) {}
#endif

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

	size = sizeof(struct ta_pmf_shared_memory) + dev->policy_sz;
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

	ret = apmf_check_smart_pc(dev);
	if (ret) {
		/*
		 * Lets not return from here if Smart PC bit is not advertised in
		 * the BIOS. This way, there will be some amount of power savings
		 * to the user with static slider (if enabled).
		 */
		dev_info(dev->dev, "PMF Smart PC not advertised in BIOS!:%d\n", ret);
		return -ENODEV;
	}

	ret = amd_pmf_tee_init(dev);
	if (ret)
		return ret;

	INIT_DELAYED_WORK(&dev->pb_work, amd_pmf_invoke_cmd);

	ret = amd_pmf_set_dram_addr(dev, true);
	if (ret)
		goto error;

	dev->policy_base = devm_ioremap(dev->dev, dev->policy_addr, dev->policy_sz);
	if (!dev->policy_base) {
		ret = -ENOMEM;
		goto error;
	}

	dev->policy_buf = kzalloc(dev->policy_sz, GFP_KERNEL);
	if (!dev->policy_buf) {
		ret = -ENOMEM;
		goto error;
	}

	memcpy(dev->policy_buf, dev->policy_base, dev->policy_sz);

	amd_pmf_hex_dump_pb(dev);

	dev->prev_data = kzalloc(sizeof(*dev->prev_data), GFP_KERNEL);
	if (!dev->prev_data) {
		ret = -ENOMEM;
		goto error;
	}

	ret = amd_pmf_start_policy_engine(dev);
	if (ret)
		goto error;

	if (pb_side_load)
		amd_pmf_open_pb(dev, dev->dbgfs_dir);

	return 0;

error:
	amd_pmf_deinit_smart_pc(dev);

	return ret;
}

void amd_pmf_deinit_smart_pc(struct amd_pmf_dev *dev)
{
	if (pb_side_load && dev->esbin)
		amd_pmf_remove_pb(dev);

	cancel_delayed_work_sync(&dev->pb_work);
	kfree(dev->prev_data);
	dev->prev_data = NULL;
	kfree(dev->policy_buf);
	dev->policy_buf = NULL;
	kfree(dev->buf);
	dev->buf = NULL;
	amd_pmf_tee_deinit(dev);
}
