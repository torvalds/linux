// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include "vpu.h"
#include "vpu_defs.h"
#include "vpu_cmds.h"
#include "vpu_rpc.h"
#include "vpu_mbox.h"

struct vpu_cmd_request {
	u32 request;
	u32 response;
	u32 handled;
};

struct vpu_cmd_t {
	struct list_head list;
	u32 id;
	struct vpu_cmd_request *request;
	struct vpu_rpc_event *pkt;
	unsigned long key;
};

static struct vpu_cmd_request vpu_cmd_requests[] = {
	{
		.request = VPU_CMD_ID_CONFIGURE_CODEC,
		.response = VPU_MSG_ID_MEM_REQUEST,
		.handled = 1,
	},
	{
		.request = VPU_CMD_ID_START,
		.response = VPU_MSG_ID_START_DONE,
		.handled = 0,
	},
	{
		.request = VPU_CMD_ID_STOP,
		.response = VPU_MSG_ID_STOP_DONE,
		.handled = 0,
	},
	{
		.request = VPU_CMD_ID_ABORT,
		.response = VPU_MSG_ID_ABORT_DONE,
		.handled = 0,
	},
	{
		.request = VPU_CMD_ID_RST_BUF,
		.response = VPU_MSG_ID_BUF_RST,
		.handled = 1,
	},
};

static int vpu_cmd_send(struct vpu_core *core, struct vpu_rpc_event *pkt)
{
	int ret = 0;

	ret = vpu_iface_send_cmd(core, pkt);
	if (ret)
		return ret;

	/*write cmd data to cmd buffer before trigger a cmd interrupt*/
	mb();
	vpu_mbox_send_type(core, COMMAND);

	return ret;
}

static struct vpu_cmd_t *vpu_alloc_cmd(struct vpu_inst *inst, u32 id, void *data)
{
	struct vpu_cmd_t *cmd;
	int i;
	int ret;

	cmd = vzalloc(sizeof(*cmd));
	if (!cmd)
		return NULL;

	cmd->pkt = vzalloc(sizeof(*cmd->pkt));
	if (!cmd->pkt) {
		vfree(cmd);
		return NULL;
	}

	cmd->id = id;
	ret = vpu_iface_pack_cmd(inst->core, cmd->pkt, inst->id, id, data);
	if (ret) {
		dev_err(inst->dev, "iface pack cmd %s fail\n", vpu_id_name(id));
		vfree(cmd->pkt);
		vfree(cmd);
		return NULL;
	}
	for (i = 0; i < ARRAY_SIZE(vpu_cmd_requests); i++) {
		if (vpu_cmd_requests[i].request == id) {
			cmd->request = &vpu_cmd_requests[i];
			break;
		}
	}

	return cmd;
}

static void vpu_free_cmd(struct vpu_cmd_t *cmd)
{
	if (!cmd)
		return;
	vfree(cmd->pkt);
	vfree(cmd);
}

static int vpu_session_process_cmd(struct vpu_inst *inst, struct vpu_cmd_t *cmd)
{
	int ret;

	dev_dbg(inst->dev, "[%d]send cmd %s\n", inst->id, vpu_id_name(cmd->id));
	vpu_iface_pre_send_cmd(inst);
	ret = vpu_cmd_send(inst->core, cmd->pkt);
	if (!ret) {
		vpu_iface_post_send_cmd(inst);
		vpu_inst_record_flow(inst, cmd->id);
	} else {
		dev_err(inst->dev, "[%d] iface send cmd %s fail\n", inst->id, vpu_id_name(cmd->id));
	}

	return ret;
}

static void vpu_process_cmd_request(struct vpu_inst *inst)
{
	struct vpu_cmd_t *cmd;
	struct vpu_cmd_t *tmp;

	if (!inst || inst->pending)
		return;

	list_for_each_entry_safe(cmd, tmp, &inst->cmd_q, list) {
		list_del_init(&cmd->list);
		if (vpu_session_process_cmd(inst, cmd))
			dev_err(inst->dev, "[%d] process cmd %s fail\n",
				inst->id, vpu_id_name(cmd->id));
		if (cmd->request) {
			inst->pending = (void *)cmd;
			break;
		}
		vpu_free_cmd(cmd);
	}
}

static int vpu_request_cmd(struct vpu_inst *inst, u32 id, void *data,
			   unsigned long *key, int *sync)
{
	struct vpu_core *core;
	struct vpu_cmd_t *cmd;

	if (!inst || !inst->core)
		return -EINVAL;

	core = inst->core;
	cmd = vpu_alloc_cmd(inst, id, data);
	if (!cmd)
		return -ENOMEM;

	mutex_lock(&core->cmd_lock);
	cmd->key = core->cmd_seq++;
	if (key)
		*key = cmd->key;
	if (sync)
		*sync = cmd->request ? true : false;
	list_add_tail(&cmd->list, &inst->cmd_q);
	vpu_process_cmd_request(inst);
	mutex_unlock(&core->cmd_lock);

	return 0;
}

static void vpu_clear_pending(struct vpu_inst *inst)
{
	if (!inst || !inst->pending)
		return;

	vpu_free_cmd(inst->pending);
	wake_up_all(&inst->core->ack_wq);
	inst->pending = NULL;
}

static bool vpu_check_response(struct vpu_cmd_t *cmd, u32 response, u32 handled)
{
	struct vpu_cmd_request *request;

	if (!cmd || !cmd->request)
		return false;

	request = cmd->request;
	if (request->response != response)
		return false;
	if (request->handled != handled)
		return false;

	return true;
}

int vpu_response_cmd(struct vpu_inst *inst, u32 response, u32 handled)
{
	struct vpu_core *core;

	if (!inst || !inst->core)
		return -EINVAL;

	core = inst->core;
	mutex_lock(&core->cmd_lock);
	if (vpu_check_response(inst->pending, response, handled))
		vpu_clear_pending(inst);

	vpu_process_cmd_request(inst);
	mutex_unlock(&core->cmd_lock);

	return 0;
}

void vpu_clear_request(struct vpu_inst *inst)
{
	struct vpu_cmd_t *cmd;
	struct vpu_cmd_t *tmp;

	mutex_lock(&inst->core->cmd_lock);
	if (inst->pending)
		vpu_clear_pending(inst);

	list_for_each_entry_safe(cmd, tmp, &inst->cmd_q, list) {
		list_del_init(&cmd->list);
		vpu_free_cmd(cmd);
	}
	mutex_unlock(&inst->core->cmd_lock);
}

static bool check_is_responsed(struct vpu_inst *inst, unsigned long key)
{
	struct vpu_core *core = inst->core;
	struct vpu_cmd_t *cmd;
	bool flag = true;

	mutex_lock(&core->cmd_lock);
	cmd = inst->pending;
	if (cmd && key == cmd->key) {
		flag = false;
		goto exit;
	}
	list_for_each_entry(cmd, &inst->cmd_q, list) {
		if (key == cmd->key) {
			flag = false;
			break;
		}
	}
exit:
	mutex_unlock(&core->cmd_lock);

	return flag;
}

static int sync_session_response(struct vpu_inst *inst, unsigned long key, long timeout, int try)
{
	struct vpu_core *core;

	if (!inst || !inst->core)
		return -EINVAL;

	core = inst->core;

	call_void_vop(inst, wait_prepare);
	wait_event_timeout(core->ack_wq, check_is_responsed(inst, key), timeout);
	call_void_vop(inst, wait_finish);

	if (!check_is_responsed(inst, key)) {
		if (try)
			return -EINVAL;
		dev_err(inst->dev, "[%d] sync session timeout\n", inst->id);
		set_bit(inst->id, &core->hang_mask);
		mutex_lock(&inst->core->cmd_lock);
		vpu_clear_pending(inst);
		mutex_unlock(&inst->core->cmd_lock);
		return -EINVAL;
	}

	return 0;
}

static void vpu_core_keep_active(struct vpu_core *core)
{
	struct vpu_rpc_event pkt;

	memset(&pkt, 0, sizeof(pkt));
	vpu_iface_pack_cmd(core, &pkt, 0, VPU_CMD_ID_NOOP, NULL);

	dev_dbg(core->dev, "try to wake up\n");
	mutex_lock(&core->cmd_lock);
	if (vpu_cmd_send(core, &pkt))
		dev_err(core->dev, "fail to keep active\n");
	mutex_unlock(&core->cmd_lock);
}

static int vpu_session_send_cmd(struct vpu_inst *inst, u32 id, void *data)
{
	unsigned long key;
	int sync = false;
	int ret;

	if (inst->id < 0)
		return -EINVAL;

	ret = vpu_request_cmd(inst, id, data, &key, &sync);
	if (ret)
		goto exit;

	/* workaround for a firmware issue,
	 * firmware should be waked up by start or configure command,
	 * but there is a very small change that firmware failed to wakeup.
	 * in such case, try to wakeup firmware again by sending a noop command
	 */
	if (sync && (id == VPU_CMD_ID_CONFIGURE_CODEC || id == VPU_CMD_ID_START)) {
		if (sync_session_response(inst, key, VPU_TIMEOUT_WAKEUP, 1))
			vpu_core_keep_active(inst->core);
		else
			goto exit;
	}

	if (sync)
		ret = sync_session_response(inst, key, VPU_TIMEOUT, 0);

exit:
	if (ret)
		dev_err(inst->dev, "[%d] send cmd %s fail\n", inst->id, vpu_id_name(id));

	return ret;
}

int vpu_session_configure_codec(struct vpu_inst *inst)
{
	return vpu_session_send_cmd(inst, VPU_CMD_ID_CONFIGURE_CODEC, NULL);
}

int vpu_session_start(struct vpu_inst *inst)
{
	vpu_trace(inst->dev, "[%d]\n", inst->id);

	return vpu_session_send_cmd(inst, VPU_CMD_ID_START, NULL);
}

int vpu_session_stop(struct vpu_inst *inst)
{
	int ret;

	vpu_trace(inst->dev, "[%d]\n", inst->id);

	ret = vpu_session_send_cmd(inst, VPU_CMD_ID_STOP, NULL);
	/* workaround for a firmware bug,
	 * if the next command is too close after stop cmd,
	 * the firmware may enter wfi wrongly.
	 */
	usleep_range(3000, 5000);
	return ret;
}

int vpu_session_encode_frame(struct vpu_inst *inst, s64 timestamp)
{
	return vpu_session_send_cmd(inst, VPU_CMD_ID_FRAME_ENCODE, &timestamp);
}

int vpu_session_alloc_fs(struct vpu_inst *inst, struct vpu_fs_info *fs)
{
	return vpu_session_send_cmd(inst, VPU_CMD_ID_FS_ALLOC, fs);
}

int vpu_session_release_fs(struct vpu_inst *inst, struct vpu_fs_info *fs)
{
	return vpu_session_send_cmd(inst, VPU_CMD_ID_FS_RELEASE, fs);
}

int vpu_session_abort(struct vpu_inst *inst)
{
	return vpu_session_send_cmd(inst, VPU_CMD_ID_ABORT, NULL);
}

int vpu_session_rst_buf(struct vpu_inst *inst)
{
	return vpu_session_send_cmd(inst, VPU_CMD_ID_RST_BUF, NULL);
}

int vpu_session_fill_timestamp(struct vpu_inst *inst, struct vpu_ts_info *info)
{
	return vpu_session_send_cmd(inst, VPU_CMD_ID_TIMESTAMP, info);
}

int vpu_session_update_parameters(struct vpu_inst *inst, void *arg)
{
	if (inst->type & VPU_CORE_TYPE_DEC)
		vpu_iface_set_decode_params(inst, arg, 1);
	else
		vpu_iface_set_encode_params(inst, arg, 1);

	return vpu_session_send_cmd(inst, VPU_CMD_ID_UPDATE_PARAMETER, arg);
}

int vpu_session_debug(struct vpu_inst *inst)
{
	return vpu_session_send_cmd(inst, VPU_CMD_ID_DEBUG, NULL);
}

int vpu_core_snapshot(struct vpu_core *core)
{
	struct vpu_inst *inst;
	int ret;

	if (!core || list_empty(&core->instances))
		return 0;

	inst = list_first_entry(&core->instances, struct vpu_inst, list);

	reinit_completion(&core->cmp);
	ret = vpu_session_send_cmd(inst, VPU_CMD_ID_SNAPSHOT, NULL);
	if (ret)
		return ret;
	ret = wait_for_completion_timeout(&core->cmp, VPU_TIMEOUT);
	if (!ret) {
		dev_err(core->dev, "snapshot timeout\n");
		return -EINVAL;
	}

	return 0;
}

int vpu_core_sw_reset(struct vpu_core *core)
{
	struct vpu_rpc_event pkt;
	int ret;

	memset(&pkt, 0, sizeof(pkt));
	vpu_iface_pack_cmd(core, &pkt, 0, VPU_CMD_ID_FIRM_RESET, NULL);

	reinit_completion(&core->cmp);
	mutex_lock(&core->cmd_lock);
	ret = vpu_cmd_send(core, &pkt);
	mutex_unlock(&core->cmd_lock);
	if (ret)
		return ret;
	ret = wait_for_completion_timeout(&core->cmp, VPU_TIMEOUT);
	if (!ret) {
		dev_err(core->dev, "sw reset timeout\n");
		return -EINVAL;
	}

	return 0;
}
