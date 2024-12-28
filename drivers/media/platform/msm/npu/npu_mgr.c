// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* -------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------
 */
#include "npu_hw_access.h"
#include "npu_mgr.h"
#include "npu_firmware.h"
#include "npu_hw.h"
#include "npu_host_ipc.h"
#include "npu_common.h"

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
#define NPU_FW_TIMEOUT_POLL_INTERVAL_MS  10
#define NPU_FW_TIMEOUT_MS                1000

/* -------------------------------------------------------------------------
 * File Scope Function Prototypes
 * -------------------------------------------------------------------------
 */
static void host_irq_wq(struct work_struct *work);
static void fw_deinit_wq(struct work_struct *work);
static void turn_off_fw_logging(struct npu_device *npu_dev);
static int wait_for_status_ready(struct npu_device *npu_dev,
	uint32_t status_reg, uint32_t status_bits, bool poll);
static struct npu_network *alloc_network(struct npu_host_ctx *ctx,
	struct npu_client *client);
static struct npu_network *get_network_by_hdl(struct npu_host_ctx *ctx,
	struct npu_client *client, uint32_t hdl);
static struct npu_network *get_network_by_id(struct npu_host_ctx *ctx,
	struct npu_client *client, int64_t id);
static void free_network(struct npu_host_ctx *ctx, struct npu_client *client,
	int64_t id);
static int network_get(struct npu_network *network);
static int network_put(struct npu_network *network);
static void app_msg_proc(struct npu_host_ctx *host_ctx, uint32_t *msg);
static void host_session_msg_hdlr(struct npu_device *npu_dev);
static int host_error_hdlr(struct npu_device *npu_dev, bool force);
static int npu_send_network_cmd(struct npu_device *npu_dev,
	struct npu_network *network, void *cmd_ptr);
static int npu_send_misc_cmd(struct npu_device *npu_dev, uint32_t q_idx,
	void *cmd_ptr);
static int npu_notify_dsp(struct npu_device *npu_dev, bool pwr_up);
static int npu_notify_aop(struct npu_device *npu_dev, bool on);
static int update_dcvs_activity(struct npu_device *npu_dev, uint32_t activity);
static void npu_destroy_wq(struct npu_host_ctx *host_ctx);
static struct workqueue_struct *npu_create_wq(struct npu_host_ctx *host_ctx,
	const char *name);

/* -------------------------------------------------------------------------
 * Function Definitions - Init / Deinit
 * -------------------------------------------------------------------------
 */
int fw_init(struct npu_device *npu_dev)
{
	uint32_t reg_val;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	int ret = 0, retry_cnt = 3;
	bool need_retry;

	mutex_lock(&host_ctx->lock);
	if (host_ctx->fw_state == FW_ENABLED) {
		host_ctx->fw_ref_cnt++;
		pr_debug("fw_ref_cnt %d\n", host_ctx->fw_ref_cnt);
		mutex_unlock(&host_ctx->lock);
		return 0;
	}

retry:
	need_retry = false;
	npu_notify_aop(npu_dev, true);

	if (npu_enable_core_power(npu_dev)) {
		ret = -EPERM;
		goto enable_pw_fail;
	}

	if (npu_enable_sys_cache(npu_dev)) {
		ret = -EPERM;
		goto enable_sys_cache_fail;
	}
	/* Boot the NPU subsystem */
	if (npu_subsystem_get(npu_dev, "npu.mdt")) {
		pr_err("pil load npu fw failed\n");
		ret = -ENODEV;
		goto subsystem_get_fail;
	}

	/* Clear control/status registers */
	REGW(npu_dev, REG_NPU_FW_CTRL_STATUS, 0x0);
	REGW(npu_dev, REG_NPU_HOST_CTRL_VALUE, 0x0);
	REGW(npu_dev, REG_FW_TO_HOST_EVENT, 0x0);
	pr_debug("fw_dbg_mode %x\n", host_ctx->fw_dbg_mode);
	reg_val = 0;
	if (host_ctx->fw_dbg_mode & FW_DBG_MODE_PAUSE)
		reg_val |= HOST_CTRL_STATUS_FW_PAUSE_VAL;

	if (host_ctx->fw_dbg_mode & FW_DBG_DISABLE_WDOG)
		reg_val |= HOST_CTRL_STATUS_DISABLE_WDOG_VAL;

	REGW(npu_dev, REG_NPU_HOST_CTRL_STATUS, reg_val);
	/* Read back to flush all registers for fw to read */
	REGR(npu_dev, REG_NPU_HOST_CTRL_STATUS);

	/* Post PIL clocks */
	if (npu_enable_post_pil_clocks(npu_dev)) {
		ret = -EPERM;
		goto enable_post_clk_fail;
	}

	/*
	 * Set logging state and clock gating state
	 * during FW bootup initialization
	 */
	reg_val = REGR(npu_dev, REG_NPU_HOST_CTRL_STATUS);

	/* Enable clock gating only if the HW access platform allows it */
	if (npu_hw_clk_gating_enabled())
		reg_val |= HOST_CTRL_STATUS_BOOT_ENABLE_CLK_GATE_VAL;
	if (host_ctx->fw_dbg_mode & FW_DBG_ENABLE_LOGGING) {
		//Enable logging
		reg_val |= HOST_CTRL_STATUS_BOOT_ENABLE_LOGGING_VAL;
	}
	REGW(npu_dev, REG_NPU_HOST_CTRL_STATUS, reg_val);

	/* Initialize the host side IPC */
	ret = npu_host_ipc_pre_init(npu_dev);
	if (ret) {
		pr_err("npu_host_ipc_pre_init failed %d\n", ret);
		goto enable_post_clk_fail;
	}

	/* Keep reading ctrl status until NPU is ready */
	pr_debug("waiting for status ready from fw\n");

	if (wait_for_status_ready(npu_dev, REG_NPU_FW_CTRL_STATUS,
		FW_CTRL_STATUS_MAIN_THREAD_READY_VAL, true)) {
		ret = -EPERM;
		need_retry = true;
		goto wait_fw_ready_fail;
	}

	npu_host_ipc_post_init(npu_dev);

	if (npu_enable_irq(npu_dev)) {
		ret = -EPERM;
		goto wait_fw_ready_fail;
	}

	npu_notify_dsp(npu_dev, true);
	host_ctx->fw_state = FW_ENABLED;
	host_ctx->fw_error = false;
	host_ctx->fw_ref_cnt++;
	reinit_completion(&host_ctx->fw_deinit_done);

	mutex_unlock(&host_ctx->lock);
	pr_debug("firmware init complete\n");
	pr_debug("fw_ref_cnt %d\n", host_ctx->fw_ref_cnt);

	/* Set logging state */
	if (!npu_hw_log_enabled()) {
		pr_debug("fw logging disabled\n");
		turn_off_fw_logging(npu_dev);
	}

	return ret;

wait_fw_ready_fail:
	npu_disable_post_pil_clocks(npu_dev);
enable_post_clk_fail:
	npu_subsystem_put(npu_dev);
subsystem_get_fail:
	npu_disable_sys_cache(npu_dev);
enable_sys_cache_fail:
	npu_disable_core_power(npu_dev);
enable_pw_fail:
	npu_notify_aop(npu_dev, false);
	host_ctx->fw_state = FW_DISABLED;
	if (need_retry && (retry_cnt > 0)) {
		retry_cnt--;
		pr_warn("retry fw init %d\n", retry_cnt);
		goto retry;
	}
	mutex_unlock(&host_ctx->lock);
	return ret;
}

void fw_deinit(struct npu_device *npu_dev, bool ssr, bool fw_alive)
{
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	struct ipc_cmd_shutdown_pkt cmd_shutdown_pkt;
	struct npu_network *network = NULL;
	int ret = 0, i;

	mutex_lock(&host_ctx->lock);
	if (!ssr && (host_ctx->fw_ref_cnt > 0))
		host_ctx->fw_ref_cnt--;

	pr_debug("fw_ref_cnt %d\n", host_ctx->fw_ref_cnt);

	if (host_ctx->fw_state != FW_ENABLED) {
		pr_err("fw is not enabled\n");
		mutex_unlock(&host_ctx->lock);
		return;
	}

	if ((host_ctx->fw_ref_cnt > 0) && !ssr) {
		mutex_unlock(&host_ctx->lock);
		return;
	}

	npu_disable_irq(npu_dev);

	if (fw_alive) {
		/* Command header */
		cmd_shutdown_pkt.header.cmd_type = NPU_IPC_CMD_SHUTDOWN;
		cmd_shutdown_pkt.header.size =
			sizeof(struct ipc_cmd_shutdown_pkt);
		cmd_shutdown_pkt.header.trans_id =
			atomic_add_return(1, &host_ctx->ipc_trans_id);
		cmd_shutdown_pkt.header.flags = 0xF;
		ret = npu_host_ipc_send_cmd(npu_dev,
			IPC_QUEUE_CMD_HIGH_PRIORITY, &cmd_shutdown_pkt);

		pr_debug("NPU_IPC_CMD_SHUTDOWN sent status: %d\n", ret);

		if (ret) {
			pr_err("npu_host_ipc_send_cmd failed\n");
		} else {
			/* Keep reading ctrl status until NPU shuts down */
			pr_debug("waiting for shutdown status from fw\n");
			if (wait_for_status_ready(npu_dev,
				REG_NPU_FW_CTRL_STATUS,
				FW_CTRL_STATUS_SHUTDOWN_DONE_VAL, true)) {
				pr_err("wait for fw shutdown timedout\n");
				ret = -ETIMEDOUT;
			}
		}
	}

	npu_disable_post_pil_clocks(npu_dev);
	npu_disable_sys_cache(npu_dev);
	npu_subsystem_put(npu_dev);
	host_ctx->fw_state = FW_DISABLED;

	/*
	 * if fw is still alive, notify dsp before power off
	 * otherwise delay 500 ms to make sure dsp has finished
	 * its own ssr handling.
	 */
	if (fw_alive)
		npu_notify_dsp(npu_dev, false);
	else
		msleep(500);

	npu_disable_core_power(npu_dev);

	if (ssr) {
		/* mark all existing network to error state */
		for (i = 0; i < MAX_LOADED_NETWORK; i++) {
			network = &host_ctx->networks[i];
			if (network->is_valid)
				network->fw_error = true;
		}
	}

	complete(&host_ctx->fw_deinit_done);
	mutex_unlock(&host_ctx->lock);
	pr_debug("firmware deinit complete\n");
	npu_notify_aop(npu_dev, false);
}

int npu_host_init(struct npu_device *npu_dev)
{
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	memset(host_ctx, 0, sizeof(*host_ctx));
	init_completion(&host_ctx->misc_done);
	init_completion(&host_ctx->fw_deinit_done);
	mutex_init(&host_ctx->lock);
	atomic_set(&host_ctx->ipc_trans_id, 1);
	host_ctx->npu_dev = npu_dev;

	host_ctx->wq = npu_create_wq(host_ctx, "npu_wq");
	if (!host_ctx->wq)
		return -EPERM;

	host_ctx->prop_buf = kzalloc(sizeof(struct msm_npu_property),
		GFP_KERNEL);
	if (!host_ctx->prop_buf)
		return -ENOMEM;

	host_ctx->misc_pending = false;

	return 0;
}

void npu_host_deinit(struct npu_device *npu_dev)
{
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	kfree(host_ctx->prop_buf);
	npu_destroy_wq(host_ctx);
	mutex_destroy(&host_ctx->lock);
}

/* -------------------------------------------------------------------------
 * Function Definitions - Interrupt Handler
 * -------------------------------------------------------------------------
 */
irqreturn_t npu_intr_hdler(int irq, void *ptr)
{
	/* Check the interrupt we received */
	/* Currently this is the IPC interrupt */
	struct npu_device *npu_dev = (struct npu_device *)ptr;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	INTERRUPT_ACK(npu_dev, irq);

	/* Check that the event thread currently is running */
	if (host_ctx->wq)
		queue_work(host_ctx->wq, &host_ctx->irq_work);

	return IRQ_HANDLED;
}

/* -------------------------------------------------------------------------
 * Function Definitions - Control
 * -------------------------------------------------------------------------
 */
static int host_error_hdlr(struct npu_device *npu_dev, bool force)
{
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	struct npu_network *network = NULL;
	int i;

	if ((host_ctx->wdg_irq_sts == 0) && (host_ctx->err_irq_sts == 0)
		&& !force)
		return 0;

	if (host_ctx->wdg_irq_sts)
		pr_info("watchdog irq triggered\n");

	fw_deinit(npu_dev, true, force);
	host_ctx->wdg_irq_sts = 0;
	host_ctx->err_irq_sts = 0;

	/* flush all pending npu cmds */
	mutex_lock(&host_ctx->lock);
	for (i = 0; i < MAX_LOADED_NETWORK; i++) {
		network = &host_ctx->networks[i];
		if (network->is_valid && network->cmd_pending &&
			network->fw_error) {
			network->cmd_pending = false;
			pr_debug("complete network %llx\n",
				network->id);
			complete(&network->cmd_done);
		}
	}
	host_ctx->misc_pending = false;
	complete_all(&host_ctx->misc_done);
	mutex_unlock(&host_ctx->lock);

	return 1;
}

static void host_irq_wq(struct work_struct *work)
{
	struct npu_host_ctx *host_ctx;
	struct npu_device *npu_dev;

	host_ctx = container_of(work, struct npu_host_ctx, irq_work);
	npu_dev = container_of(host_ctx, struct npu_device, host_ctx);

	if (host_error_hdlr(npu_dev, false))
		return;

	host_session_msg_hdlr(npu_dev);
}

static void fw_deinit_wq(struct work_struct *work)
{
	struct npu_host_ctx *host_ctx;
	struct npu_device *npu_dev;

	pr_debug("%s: deinit fw\n", __func__);
	host_ctx = container_of(work, struct npu_host_ctx, fw_deinit_work.work);
	npu_dev = container_of(host_ctx, struct npu_device, host_ctx);

	if (atomic_read(&host_ctx->fw_deinit_work_cnt) == 0)
		return;

	do {
		fw_deinit(npu_dev, false, true);
	} while (!atomic_dec_and_test(&host_ctx->fw_deinit_work_cnt));
}

static void npu_destroy_wq(struct npu_host_ctx *host_ctx)
{
	flush_delayed_work(&host_ctx->fw_deinit_work);
	destroy_workqueue(host_ctx->wq);
}

static struct workqueue_struct *npu_create_wq(struct npu_host_ctx *host_ctx,
	const char *name)
{
	struct workqueue_struct *wq =
		alloc_workqueue(name, WQ_HIGHPRI | WQ_UNBOUND, 0);

	INIT_WORK(&host_ctx->irq_work, host_irq_wq);
	INIT_DELAYED_WORK(&host_ctx->fw_deinit_work, fw_deinit_wq);

	return wq;
}

static void turn_off_fw_logging(struct npu_device *npu_dev)
{
	struct ipc_cmd_log_state_pkt log_packet;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	int ret = 0;

	log_packet.header.cmd_type = NPU_IPC_CMD_CONFIG_LOG;
	log_packet.header.size = sizeof(struct ipc_cmd_log_state_pkt);
	log_packet.header.trans_id =
		atomic_add_return(1, &host_ctx->ipc_trans_id);
	log_packet.header.flags = 0xF;
	log_packet.log_state.module_msk = 0;
	log_packet.log_state.level_msk = 0;
	ret = npu_send_misc_cmd(npu_dev, IPC_QUEUE_CMD_HIGH_PRIORITY,
		&log_packet);

	pr_debug("NPU_IPC_CMD_CONFIG_LOG sent status: %d\n", ret);

	if (ret)
		pr_err("npu_host_ipc_send_cmd failed\n");
}

static int wait_for_status_ready(struct npu_device *npu_dev,
	uint32_t status_reg, uint32_t status_bits, bool poll)
{
	uint32_t ctrl_sts = 0;
	uint32_t wait_cnt = 0, max_wait_ms;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	max_wait_ms = (host_ctx->fw_dbg_mode & FW_DBG_MODE_INC_TIMEOUT) ?
		NW_DEBUG_TIMEOUT_MS : NPU_FW_TIMEOUT_MS;
	if (poll)
		wait_cnt = max_wait_ms * 10;
	else
		wait_cnt = max_wait_ms / NPU_FW_TIMEOUT_POLL_INTERVAL_MS;

	/* keep reading status register until bits are set */
	do {
		ctrl_sts = REGR(npu_dev, status_reg);
		if ((ctrl_sts & status_bits) == status_bits) {
			pr_debug("status %x[reg %x] ready received\n",
				status_bits, status_reg);
			break;
		}

		if (!wait_cnt) {
			pr_err("timeout wait for status %x[%x] in reg %x\n",
				status_bits, ctrl_sts, status_reg);
			return -ETIMEDOUT;
		}

		if (poll)
			udelay(100);
		else
			msleep(NPU_FW_TIMEOUT_POLL_INTERVAL_MS);

		wait_cnt--;
	} while (1);

	return 0;

}

static int npu_notify_dsp(struct npu_device *npu_dev, bool pwr_up)
{
	uint32_t ack_val, notify_val;
	int ret = 0;

	if (pwr_up) {
		notify_val = HOST_DSP_CTRL_STATUS_PWR_UP_VAL;
		ack_val = HOST_DSP_CTRL_STATUS_PWR_UP_ACK_VAL;
	} else {
		notify_val = HOST_DSP_CTRL_STATUS_PWR_DWN_VAL;
		ack_val = HOST_DSP_CTRL_STATUS_PWR_DWN_ACK_VAL;
	}

	REGW(npu_dev, REG_HOST_DSP_CTRL_STATUS,
		notify_val);
	/* Read back to flush register for dsp to read */
	REGR(npu_dev, REG_HOST_DSP_CTRL_STATUS);

	INTERRUPT_RAISE_DSP(npu_dev);

	ret = wait_for_status_ready(npu_dev, REG_HOST_DSP_CTRL_STATUS,
		ack_val, true);
	if (ret)
		pr_warn("No response from dsp\n");

	return ret;
}

#define MAX_LEN 128

static int npu_notify_aop(struct npu_device *npu_dev, bool on)
{
	char buf[MAX_LEN];
	struct qmp_pkt pkt;
	int buf_size, rc = 0;

	if (!npu_dev->mbox_aop.chan) {
		pr_warn("aop mailbox channel is not available\n");
		return 0;
	}

	buf_size = scnprintf(buf, MAX_LEN, "{class: bcm, res: npu_on, val: %d}",
		on ? 1 : 0);
	if (buf_size < 0) {
		pr_err("prepare qmp notify buf failed\n");
		return -EINVAL;
	}

	pr_debug("send msg %s to aop\n", buf);
	memset(&pkt, 0, sizeof(pkt));
	pkt.size = (buf_size + 3) & ~0x3;
	pkt.data = buf;

	rc = mbox_send_message(npu_dev->mbox_aop.chan, &pkt);
	if (rc < 0)
		pr_err("qmp message send failed, ret=%d\n", rc);

	return rc;
}

/* -------------------------------------------------------------------------
 * Function Definitions - Network Management
 * -------------------------------------------------------------------------
 */
static int network_put(struct npu_network *network)
{
	if (!network)
		return 0;

	return atomic_dec_return(&network->ref_cnt);
}

static int network_get(struct npu_network *network)
{
	if (!network)
		return 0;

	return atomic_inc_return(&network->ref_cnt);
}

static struct npu_network *alloc_network(struct npu_host_ctx *ctx,
	struct npu_client *client)
{
	int32_t i;
	struct npu_network *network = ctx->networks;

	WARN_ON(!mutex_is_locked(&ctx->lock));

	for (i = 0; i < MAX_LOADED_NETWORK; i++) {
		if (network->id == 0)
			break;

		network++;
	}

	if (i == MAX_LOADED_NETWORK) {
		pr_err("No free network\n");
		return NULL;
	}

	memset(network, 0, sizeof(struct npu_network));
	network->id = i + 1;
	init_completion(&network->cmd_done);
	network->is_valid = true;
	network->client = client;
	network->stats_buf = kzalloc(NPU_MAX_STATS_BUF_SIZE,
		GFP_KERNEL);
	if (!network->stats_buf) {
		memset(network, 0, sizeof(struct npu_network));
		return NULL;
	}

	ctx->network_num++;
	pr_debug("%s:Active network num %d\n", __func__, ctx->network_num);

	return network;
}

static struct npu_network *get_network_by_hdl(struct npu_host_ctx *ctx,
	struct npu_client *client, uint32_t hdl)
{
	int32_t i;
	struct npu_network *network = ctx->networks;

	WARN_ON(!mutex_is_locked(&ctx->lock));

	for (i = 0; i < MAX_LOADED_NETWORK; i++) {
		if (network->network_hdl == hdl)
			break;

		network++;
	}

	if ((i == MAX_LOADED_NETWORK) || !network->is_valid) {
		pr_err("network hdl invalid %d\n", hdl);
		return NULL;
	}

	if (client && (client != network->client)) {
		pr_err("network %lld doesn't belong to this client\n",
			network->id);
		return NULL;
	}

	network_get(network);
	return network;
}

static struct npu_network *get_network_by_id(struct npu_host_ctx *ctx,
	struct npu_client *client, int64_t id)
{
	struct npu_network *network = NULL;

	WARN_ON(!mutex_is_locked(&ctx->lock));

	if (id < 1 || id > MAX_LOADED_NETWORK ||
		!ctx->networks[id - 1].is_valid) {
		pr_err("Invalid network id %d\n", (int32_t)id);
		return NULL;
	}

	network = &ctx->networks[id - 1];
	if (client && (client != network->client)) {
		pr_err("network %lld doesn't belong to this client\n", id);
		return NULL;
	}

	network_get(network);
	return network;
}

static void free_network(struct npu_host_ctx *ctx, struct npu_client *client,
	int64_t id)
{
	struct npu_network *network = NULL;

	WARN_ON(!mutex_is_locked(&ctx->lock));

	network = get_network_by_id(ctx, client, id);
	if (network) {
		network_put(network);
		if (atomic_read(&network->ref_cnt) == 0) {
			kfree(network->stats_buf);
			memset(network, 0, sizeof(struct npu_network));
			ctx->network_num--;
			pr_debug("%s:Active network num %d\n", __func__,
				ctx->network_num);
		} else {
			pr_warn("network %lld:%d is in use\n", network->id,
				atomic_read(&network->ref_cnt));
		}
	}
}

/* -------------------------------------------------------------------------
 * Function Definitions - IPC
 * -------------------------------------------------------------------------
 */

static void app_msg_proc(struct npu_host_ctx *host_ctx, uint32_t *msg)
{
	uint32_t msg_id;
	struct npu_network *network = NULL;
	struct npu_device *npu_dev = host_ctx->npu_dev;

	msg_id = msg[1];
	switch (msg_id) {
	case NPU_IPC_MSG_EXECUTE_DONE:
	{
		struct ipc_msg_execute_pkt *exe_rsp_pkt =
			(struct ipc_msg_execute_pkt *)msg;

		pr_debug("NPU_IPC_MSG_EXECUTE_DONE status: %d\n",
			exe_rsp_pkt->header.status);
		pr_debug("trans_id : %d\n", exe_rsp_pkt->header.trans_id);
		pr_debug("e2e_IPC_time: %d (in tick count)\n",
			exe_rsp_pkt->stats.e2e_ipc_tick_count);
		pr_debug("aco_load_time: %d (in tick count)\n",
			exe_rsp_pkt->stats.aco_load_tick_count);
		pr_debug("aco_execute_time: %d (in tick count)\n",
			exe_rsp_pkt->stats.aco_execution_tick_count);
		pr_debug("total_num_layers: %d\n",
			exe_rsp_pkt->stats.exe_stats.total_num_layers);

		network = get_network_by_hdl(host_ctx, NULL,
			exe_rsp_pkt->network_hdl);
		if (!network) {
			pr_err("can't find network %x\n",
				exe_rsp_pkt->network_hdl);
			break;
		}

		if (network->trans_id != exe_rsp_pkt->header.trans_id) {
			pr_err("execute_pkt trans_id is not match %d:%d\n",
				network->trans_id,
				exe_rsp_pkt->header.trans_id);
			network_put(network);
			break;
		}

		network->cmd_pending = false;
		network->cmd_ret_status = exe_rsp_pkt->header.status;

		complete(&network->cmd_done);
		network_put(network);

		break;
	}
	case NPU_IPC_MSG_EXECUTE_V2_DONE:
	{
		struct ipc_msg_execute_pkt_v2 *exe_rsp_pkt =
			(struct ipc_msg_execute_pkt_v2 *)msg;
		uint32_t stats_size = 0;

		pr_debug("NPU_IPC_MSG_EXECUTE_V2_DONE status: %d\n",
			exe_rsp_pkt->header.status);
		pr_debug("trans_id : %d\n", exe_rsp_pkt->header.trans_id);

		network = get_network_by_hdl(host_ctx, NULL,
			exe_rsp_pkt->network_hdl);
		if (!network) {
			pr_err("can't find network %x\n",
				exe_rsp_pkt->network_hdl);
			break;
		}

		if (network->trans_id != exe_rsp_pkt->header.trans_id) {
			pr_err("execute_pkt_v2 trans_id is not match %d:%d\n",
				network->trans_id,
				exe_rsp_pkt->header.trans_id);
			network_put(network);
			break;
		}

		pr_debug("network id : %llu\n", network->id);
		if (exe_rsp_pkt->header.size < sizeof(*exe_rsp_pkt)) {
			pr_err("invalid packet header size, header.size: %d\n",
				exe_rsp_pkt->header.size);
			network_put(network);
			break;
		}
		stats_size = exe_rsp_pkt->header.size - sizeof(*exe_rsp_pkt);
		pr_debug("stats_size %d:%d\n", exe_rsp_pkt->header.size,
			stats_size);
		stats_size = stats_size < network->stats_buf_size ?
			stats_size : network->stats_buf_size;
		if (stats_size)
			memcpy(network->stats_buf, exe_rsp_pkt->stats_data,
				stats_size);

		network->stats_buf_size = stats_size;
		network->cmd_pending = false;
		network->cmd_ret_status = exe_rsp_pkt->header.status;
		complete(&network->cmd_done);
		network_put(network);
		break;
	}
	case NPU_IPC_MSG_LOAD_DONE:
	{
		uint32_t network_id = 0;
		struct ipc_msg_load_pkt *load_rsp_pkt =
			(struct ipc_msg_load_pkt *)msg;

		pr_debug("NPU_IPC_MSG_LOAD_DONE status: %d, trans_id: %d\n",
			load_rsp_pkt->header.status,
			load_rsp_pkt->header.trans_id);

		/*
		 * The upper 8 bits in flags is the current active
		 * network count in fw
		 */
		pr_debug("Current active network count in FW is %d\n",
			load_rsp_pkt->header.flags >> 24);

		/*
		 * the upper 16 bits in returned network_hdl is
		 * the network ID
		 */
		pr_debug("network_hdl: %x\n", load_rsp_pkt->network_hdl);
		network_id = load_rsp_pkt->network_hdl >> 16;
		network = get_network_by_id(host_ctx, NULL, network_id);
		if (!network) {
			pr_err("can't find network %d\n", network_id);
			break;
		}

		if (network->trans_id != load_rsp_pkt->header.trans_id) {
			pr_err("load_rsp_pkt trans_id is not match %d:%d\n",
				network->trans_id,
				load_rsp_pkt->header.trans_id);
			network_put(network);
			break;
		}

		network->network_hdl = load_rsp_pkt->network_hdl;
		network->cmd_pending = false;
		network->cmd_ret_status = load_rsp_pkt->header.status;

		complete(&network->cmd_done);
		network_put(network);
		break;
	}
	case NPU_IPC_MSG_UNLOAD_DONE:
	{
		struct ipc_msg_unload_pkt *unload_rsp_pkt =
			(struct ipc_msg_unload_pkt *)msg;

		pr_debug("NPU_IPC_MSG_UNLOAD_DONE status: %d, trans_id: %d\n",
			unload_rsp_pkt->header.status,
			unload_rsp_pkt->header.trans_id);

		/*
		 * The upper 8 bits in flags is the current active
		 * network count in fw
		 */
		pr_debug("Current active network count in FW is %d\n",
			unload_rsp_pkt->header.flags >> 24);

		network = get_network_by_hdl(host_ctx, NULL,
			unload_rsp_pkt->network_hdl);
		if (!network) {
			pr_err("can't find network %x\n",
				unload_rsp_pkt->network_hdl);
			break;
		}

		if (network->trans_id != unload_rsp_pkt->header.trans_id) {
			pr_err("unload_rsp_pkt trans_id is not match %d:%d\n",
				network->trans_id,
				unload_rsp_pkt->header.trans_id);
			network_put(network);
			break;
		}

		network->cmd_pending = false;
		network->cmd_ret_status = unload_rsp_pkt->header.status;

		complete(&network->cmd_done);
		network_put(network);
		break;
	}
	case NPU_IPC_MSG_LOOPBACK_DONE:
	{
		struct ipc_msg_loopback_pkt *lb_rsp_pkt =
			(struct ipc_msg_loopback_pkt *)msg;

		pr_debug("NPU_IPC_MSG_LOOPBACK_DONE loopbackParams: 0x%x\n",
			lb_rsp_pkt->loopbackParams);
		host_ctx->misc_pending = false;

		complete_all(&host_ctx->misc_done);
		break;
	}
	case NPU_IPC_MSG_SET_PROPERTY_DONE:
	{
		struct ipc_msg_prop_pkt *prop_rsp_pkt =
			(struct ipc_msg_prop_pkt *)msg;
		uint32_t *param = (uint32_t *)((uint8_t *)prop_rsp_pkt +
			sizeof(struct ipc_msg_prop_pkt));
		pr_debug("NPU_IPC_MSG_SET_PROPERTY_DONE %d:0x%x:%d\n",
			prop_rsp_pkt->network_hdl,
			prop_rsp_pkt->prop_id,
			param[0]);

		host_ctx->cmd_ret_status = prop_rsp_pkt->header.status;
		host_ctx->misc_pending = false;

		complete_all(&host_ctx->misc_done);
		break;
	}
	case NPU_IPC_MSG_GET_PROPERTY_DONE:
	{
		struct ipc_msg_prop_pkt *prop_rsp_pkt =
			(struct ipc_msg_prop_pkt *)msg;
		uint32_t prop_size = 0;
		uint32_t *prop_data = (uint32_t *)((uint8_t *)prop_rsp_pkt +
			sizeof(struct ipc_msg_header_pkt));

		pr_debug("NPU_IPC_MSG_GET_PROPERTY_DONE %d:0x%x:%d:%d\n",
			prop_rsp_pkt->network_hdl,
			prop_rsp_pkt->prop_id,
			prop_rsp_pkt->num_params,
			prop_rsp_pkt->prop_param[0]);

		if (prop_rsp_pkt->header.size <
			sizeof(struct ipc_msg_header_pkt)) {
			pr_err("Invalid rsp pkt size %d\n",
				prop_rsp_pkt->header.size);
			break;
		}

		host_ctx->cmd_ret_status = prop_rsp_pkt->header.status;

		if (prop_rsp_pkt->num_params > 0) {
			/* Copy prop data to kernel buffer */
			prop_size = prop_rsp_pkt->header.size -
				sizeof(struct ipc_msg_header_pkt);
			memcpy(host_ctx->prop_buf, prop_data, prop_size);
		}
		host_ctx->misc_pending = false;

		complete_all(&host_ctx->misc_done);
		break;
	}
	case NPU_IPC_MSG_GENERAL_NOTIFY:
	{
		struct ipc_msg_general_notify_pkt *notify_msg_pkt =
			(struct ipc_msg_general_notify_pkt *)msg;

		pr_debug("NPU_IPC_MSG_GENERAL_NOTIFY %d:0x%x:%d\n",
			notify_msg_pkt->network_hdl,
			notify_msg_pkt->notify_id,
			notify_msg_pkt->notify_param[0]);

		switch (notify_msg_pkt->notify_id) {
		case NPU_NOTIFY_DCVS_MODE:
			pr_debug("NPU_IPC_MSG_GENERAL_NOTIFY DCVS_MODE %d\n",
				notify_msg_pkt->notify_param[0]);
			update_dcvs_activity(npu_dev,
				notify_msg_pkt->notify_param[0]);
			break;
		default:
			pr_err("Nothing to do\n");
			break;
		}
		break;
	}
	default:
		pr_err("Not supported apps response received %d\n",
			msg_id);
		break;
	}
}

static void host_session_msg_hdlr(struct npu_device *npu_dev)
{
	uint32_t *msg;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	msg = kzalloc(sizeof(uint32_t) * NPU_IPC_BUF_LENGTH, GFP_KERNEL);
	if (!msg)
		return;

	mutex_lock(&host_ctx->lock);
	if (host_ctx->fw_state == FW_DISABLED) {
		pr_warn("handle npu session msg when FW is disabled\n");
		goto skip_read_msg;
	}

	while (npu_host_ipc_read_msg(npu_dev, IPC_QUEUE_APPS_RSP, msg) == 0) {
		pr_debug("received from msg queue\n");
		app_msg_proc(host_ctx, msg);
	}

skip_read_msg:
	mutex_unlock(&host_ctx->lock);
	kfree(msg);
}


/* -------------------------------------------------------------------------
 * Function Definitions - Functionality
 * -------------------------------------------------------------------------
 */
int32_t npu_host_get_info(struct npu_device *npu_dev,
			struct msm_npu_get_info_ioctl *get_info_ioctl)
{
	get_info_ioctl->firmware_version = FIRMWARE_VERSION;
	get_info_ioctl->flags = npu_dev->pwrctrl.num_pwrlevels;
	return 0;
}

int32_t npu_host_map_buf(struct npu_client *client,
			struct msm_npu_map_buf_ioctl *map_ioctl)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	int ret;

	mutex_lock(&host_ctx->lock);
	ret = npu_mem_map(client, map_ioctl->buf_ion_hdl, map_ioctl->size,
		&map_ioctl->npu_phys_addr);
	mutex_unlock(&host_ctx->lock);

	return ret;
}

int32_t npu_host_unmap_buf(struct npu_client *client,
			struct msm_npu_unmap_buf_ioctl *unmap_ioctl)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	/*
	 * Once SSR occurs, all buffers only can be unmapped until
	 * fw is disabled
	 */
	if (host_ctx->fw_error && (host_ctx->fw_state == FW_ENABLED) &&
		!wait_for_completion_timeout(
		&host_ctx->fw_deinit_done, NW_CMD_TIMEOUT))
		pr_warn("npu: wait for fw_deinit_done time out\n");

	mutex_lock(&host_ctx->lock);
	npu_mem_unmap(client, unmap_ioctl->buf_ion_hdl,
		unmap_ioctl->npu_phys_addr);
	mutex_unlock(&host_ctx->lock);
	return 0;
}

static int npu_send_network_cmd(struct npu_device *npu_dev,
	struct npu_network *network, void *cmd_ptr)
{
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	int ret = 0;

	if (network->fw_error || host_ctx->fw_error ||
		(host_ctx->fw_state == FW_DISABLED)) {
		pr_err("fw is in error state or disabled, can't send network cmd\n");
		ret = -EIO;
	} else if (network->cmd_pending) {
		pr_err("Another cmd is pending\n");
		ret = -EBUSY;
	} else {
		pr_debug("Send cmd %d network id %lld\n",
			((struct ipc_cmd_header_pkt *)cmd_ptr)->cmd_type,
			network->id);
		network->cmd_ret_status = 0;
		network->cmd_pending = true;
		network->trans_id = ((struct ipc_cmd_header_pkt *)cmd_ptr)->trans_id;
		ret = npu_host_ipc_send_cmd(npu_dev,
			IPC_QUEUE_APPS_EXEC, cmd_ptr);
		if (ret)
			network->cmd_pending = false;
	}

	return ret;
}

static int npu_send_misc_cmd(struct npu_device *npu_dev, uint32_t q_idx,
	void *cmd_ptr)
{
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	int ret = 0;

	mutex_lock(&host_ctx->lock);
	if (host_ctx->fw_error || (host_ctx->fw_state == FW_DISABLED)) {
		pr_err("fw is in error state or disabled, can't send misc cmd\n");
		ret = -EIO;
	} else if (host_ctx->misc_pending) {
		pr_err("Another misc cmd is pending\n");
		ret = -EBUSY;
	} else {
		pr_debug("Send cmd %d\n",
			((struct ipc_cmd_header_pkt *)cmd_ptr)->cmd_type);
		host_ctx->cmd_ret_status = 0;
		reinit_completion(&host_ctx->misc_done);
		host_ctx->misc_pending = true;
		ret = npu_host_ipc_send_cmd(npu_dev, q_idx, cmd_ptr);
		if (ret)
			host_ctx->misc_pending = false;
	}
	mutex_unlock(&host_ctx->lock);

	return ret;
}

static void host_copy_patch_data(struct npu_patch_tuple *param, uint32_t value,
		struct msm_npu_layer *layer_info)
{
	param->value = value;
	param->chunk_id = layer_info->patch_info.chunk_id;
	param->loc_offset = layer_info->patch_info.loc_offset;
	param->instruction_size_in_bytes =
		layer_info->patch_info.instruction_size_in_bytes;
	param->shift_value_in_bits =
		layer_info->patch_info.shift_value_in_bits;
	param->variable_size_in_bits =
		layer_info->patch_info.variable_size_in_bits;

	pr_debug("copy_patch_data: %x %d %x %x %x %x\n",
		param->value,
		param->chunk_id,
		param->loc_offset,
		param->instruction_size_in_bytes,
		param->shift_value_in_bits,
		param->variable_size_in_bits);
}

static void host_copy_patch_data_v2(struct npu_patch_tuple_v2 *param,
	struct msm_npu_patch_info_v2 *patch_info)
{
	param->value = patch_info->value;
	param->chunk_id = patch_info->chunk_id;
	param->loc_offset = patch_info->loc_offset;
	param->instruction_size_in_bytes =
		patch_info->instruction_size_in_bytes;
	param->shift_value_in_bits = patch_info->shift_value_in_bits;
	param->variable_size_in_bits = patch_info->variable_size_in_bits;
	pr_debug("copy_patch_data_v2: %x %d %x %x %x %x\n",
		param->value,
		param->chunk_id,
		param->loc_offset,
		param->instruction_size_in_bytes,
		param->shift_value_in_bits,
		param->variable_size_in_bits);
}

static uint32_t find_networks_perf_mode(struct npu_host_ctx *host_ctx)
{
	struct npu_network *network;
	uint32_t max_perf_mode = 0;
	int i = 0;

	network = host_ctx->networks;

	if (!host_ctx->network_num) {
		/* if no network exists, set to the lowest level */
		max_perf_mode = 1;
	} else {
		/* find the max level among all the networks */
		for (i = 0; i < MAX_LOADED_NETWORK; i++) {
			if ((network->id != 0) &&
				(network->cur_perf_mode != 0) &&
				(network->cur_perf_mode > max_perf_mode))
				max_perf_mode = network->cur_perf_mode;
			network++;
		}
	}
	pr_debug("max perf mode for networks: %d\n", max_perf_mode);

	return max_perf_mode;
}

static int set_perf_mode(struct npu_device *npu_dev)
{
	int ret = 0;
	uint32_t networks_perf_mode;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	networks_perf_mode = find_networks_perf_mode(host_ctx);

	if (npu_dev->pwrctrl.perf_mode_override)
		networks_perf_mode = npu_dev->pwrctrl.perf_mode_override;

	if (npu_dev->pwrctrl.cur_dcvs_activity != NPU_DCVS_ACTIVITY_MAX_PERF)
		networks_perf_mode = min_t(uint32_t, networks_perf_mode,
			npu_dev->pwrctrl.cur_dcvs_activity);

	ret = npu_set_uc_power_level(npu_dev, networks_perf_mode);
	if (ret)
		pr_err("network load failed due to power level set\n");

	return ret;
}

static int update_dcvs_activity(struct npu_device *npu_dev, uint32_t activity)
{
	npu_dev->pwrctrl.cur_dcvs_activity = activity;
	pr_debug("update dcvs activity to %d\n", activity);

	return set_perf_mode(npu_dev);
}

int32_t npu_host_set_fw_property(struct npu_device *npu_dev,
	struct msm_npu_property *property)
{
	int ret = 0, i;
	uint32_t prop_param, prop_id;
	struct ipc_cmd_prop_pkt *prop_packet = NULL;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	uint32_t num_of_params, pkt_size;

	prop_id = property->prop_id;
	num_of_params = min_t(uint32_t, property->num_of_params,
		(uint32_t)PROP_PARAM_MAX_SIZE);
	pkt_size = sizeof(*prop_packet) + num_of_params * sizeof(uint32_t);
	prop_packet = kzalloc(pkt_size, GFP_KERNEL);

	if (!prop_packet)
		return -ENOMEM;

	switch (prop_id) {
	case MSM_NPU_PROP_ID_DCVS_MODE:
		prop_param = min_t(uint32_t, property->prop_param[0],
			(uint32_t)(npu_dev->pwrctrl.num_pwrlevels - 1));
		property->prop_param[0] = prop_param;
		pr_debug("setting dcvs_mode to %d\n", prop_param);

		if (property->network_hdl == 0) {
			npu_dev->pwrctrl.dcvs_mode = prop_param;
			pr_debug("Set global dcvs mode %d\n", prop_param);
		}
		break;
	default:
		pr_err("unsupported property received %d\n", property->prop_id);
		goto set_prop_exit;
	}

	ret = fw_init(npu_dev);
	if (ret) {
		pr_err("fw_init fail\n");
		goto set_prop_exit;
	}

	prop_packet->header.cmd_type = NPU_IPC_CMD_SET_PROPERTY;
	prop_packet->header.size = pkt_size;
	prop_packet->header.trans_id =
		atomic_add_return(1, &host_ctx->ipc_trans_id);
	prop_packet->header.flags = 0;

	prop_packet->prop_id = prop_id;
	prop_packet->num_params = num_of_params;
	prop_packet->network_hdl = property->network_hdl;
	for (i = 0; i < num_of_params; i++)
		prop_packet->prop_param[i] = property->prop_param[i];

	ret = npu_send_misc_cmd(npu_dev, IPC_QUEUE_APPS_EXEC,
		prop_packet);

	pr_debug("NPU_IPC_CMD_SET_PROPERTY sent status: %d\n", ret);

	if (ret) {
		pr_err("NPU_IPC_CMD_SET_PROPERTY failed\n");
		goto deinit_fw;
	}

	ret = wait_for_completion_interruptible_timeout(
		&host_ctx->misc_done,
		(host_ctx->fw_dbg_mode & FW_DBG_MODE_INC_TIMEOUT) ?
		NW_DEBUG_TIMEOUT : NW_CMD_TIMEOUT);

	if (!ret) {
		pr_err_ratelimited("npu: NPU_IPC_CMD_SET_PROPERTY time out\n");
		ret = -ETIMEDOUT;
		goto deinit_fw;
	} else if (ret < 0) {
		pr_err("Wait for set_property done interrupted by signal\n");
		goto deinit_fw;
	}

	ret = host_ctx->cmd_ret_status;
	if (ret)
		pr_err("set fw property failed %d\n", ret);

deinit_fw:
	fw_deinit(npu_dev, false, true);
set_prop_exit:
	kfree(prop_packet);
	return ret;
}

int32_t npu_host_get_fw_property(struct npu_device *npu_dev,
	struct msm_npu_property *property)
{
	int ret = 0, i;
	struct ipc_cmd_prop_pkt *prop_packet = NULL;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	struct msm_npu_property *prop_from_fw;
	uint32_t num_of_params, pkt_size;

	num_of_params = min_t(uint32_t, property->num_of_params,
		(uint32_t)PROP_PARAM_MAX_SIZE);
	pkt_size = sizeof(*prop_packet) + num_of_params * sizeof(uint32_t);
	prop_packet = kzalloc(pkt_size, GFP_KERNEL);

	if (!prop_packet)
		return -ENOMEM;

	ret = fw_init(npu_dev);
	if (ret) {
		pr_err("fw_init fail\n");
		goto get_prop_exit;
	}

	prop_packet->header.cmd_type = NPU_IPC_CMD_GET_PROPERTY;
	prop_packet->header.size = pkt_size;
	prop_packet->header.trans_id =
		atomic_add_return(1, &host_ctx->ipc_trans_id);
	prop_packet->header.flags = 0;

	prop_packet->prop_id = property->prop_id;
	prop_packet->num_params = num_of_params;
	prop_packet->network_hdl = property->network_hdl;
	for (i = 0; i < num_of_params; i++)
		prop_packet->prop_param[i] = property->prop_param[i];

	ret = npu_send_misc_cmd(npu_dev, IPC_QUEUE_APPS_EXEC,
		prop_packet);
	pr_debug("NPU_IPC_CMD_GET_PROPERTY sent status: %d\n", ret);

	if (ret) {
		pr_err("NPU_IPC_CMD_GET_PROPERTY failed\n");
		goto deinit_fw;
	}

	ret = wait_for_completion_interruptible_timeout(
		&host_ctx->misc_done,
		(host_ctx->fw_dbg_mode & FW_DBG_MODE_INC_TIMEOUT) ?
		NW_DEBUG_TIMEOUT : NW_CMD_TIMEOUT);

	if (!ret) {
		pr_err_ratelimited("npu: NPU_IPC_CMD_GET_PROPERTY time out\n");
		ret = -ETIMEDOUT;
		goto deinit_fw;
	} else if (ret < 0) {
		pr_err("Wait for get_property done interrupted by signal\n");
		goto deinit_fw;
	}

	ret = host_ctx->cmd_ret_status;
	if (!ret) {
		/* Return prop data retrieved from fw to user */
		prop_from_fw = (struct msm_npu_property *)(host_ctx->prop_buf);
		if (property->prop_id == prop_from_fw->prop_id &&
			property->network_hdl == prop_from_fw->network_hdl) {
			property->num_of_params = num_of_params;
			for (i = 0; i < num_of_params; i++)
				property->prop_param[i] =
					prop_from_fw->prop_param[i];
		}
	} else {
		pr_err("get fw property failed %d\n", ret);
	}

deinit_fw:
	fw_deinit(npu_dev, false, true);
get_prop_exit:
	kfree(prop_packet);
	return ret;
}

int32_t npu_host_load_network(struct npu_client *client,
			struct msm_npu_load_network_ioctl *load_ioctl)
{
	int ret = 0;
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	struct npu_network *network;
	struct ipc_cmd_load_pkt load_packet;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	ret = fw_init(npu_dev);
	if (ret)
		return ret;

	mutex_lock(&host_ctx->lock);
	network = alloc_network(host_ctx, client);
	if (!network) {
		ret = -ENOMEM;
		goto err_deinit_fw;
	}

	network_get(network);
	network->buf_hdl = load_ioctl->buf_ion_hdl;
	network->size = load_ioctl->buf_size;
	network->phy_add = load_ioctl->buf_phys_addr;
	network->first_block_size = load_ioctl->first_block_size;
	network->priority = load_ioctl->priority;
	network->cur_perf_mode = network->init_perf_mode =
		(load_ioctl->perf_mode == PERF_MODE_DEFAULT) ?
			pwr->num_pwrlevels : load_ioctl->perf_mode;

	/* verify mapped physical address */
	if (!npu_mem_verify_addr(client, network->phy_add)) {
		ret = -EINVAL;
		goto error_free_network;
	}

	load_packet.header.cmd_type = NPU_IPC_CMD_LOAD;
	load_packet.header.size = sizeof(struct ipc_cmd_load_pkt);
	load_packet.header.trans_id =
		atomic_add_return(1, &host_ctx->ipc_trans_id);
	load_packet.header.flags = 0;

	/* ACO Buffer. Use the npu mapped aco address */
	load_packet.buf_pkt.address = (uint64_t)network->phy_add;
	load_packet.buf_pkt.buf_size = network->first_block_size;
	load_packet.buf_pkt.network_id = network->id;

	set_perf_mode(npu_dev);
	/* NPU_IPC_CMD_LOAD will go onto IPC_QUEUE_APPS_EXEC */
	reinit_completion(&network->cmd_done);
	ret = npu_send_network_cmd(npu_dev, network, &load_packet);
	if (ret) {
		pr_err("NPU_IPC_CMD_LOAD sent failed: %d\n", ret);
		goto error_free_network;
	}

	mutex_unlock(&host_ctx->lock);

	ret = wait_for_completion_timeout(
		&network->cmd_done,
		(host_ctx->fw_dbg_mode & FW_DBG_MODE_INC_TIMEOUT) ?
		NW_DEBUG_TIMEOUT : NW_CMD_TIMEOUT);

	mutex_lock(&host_ctx->lock);
	if (!ret) {
		pr_err_ratelimited("NPU_IPC_CMD_LOAD time out\n");
		ret = -ETIMEDOUT;
		goto error_free_network;
	}

	if (network->fw_error) {
		ret = -EIO;
		pr_err("fw is in error state during load network\n");
		goto error_free_network;
	}

	ret = network->cmd_ret_status;
	if (ret)
		goto error_free_network;

	load_ioctl->network_hdl = network->network_hdl;
	network->is_active = true;
	network_put(network);

	mutex_unlock(&host_ctx->lock);

	return ret;

error_free_network:
	network_put(network);
	free_network(host_ctx, client, network->id);
err_deinit_fw:
	mutex_unlock(&host_ctx->lock);
	fw_deinit(npu_dev, false, true);
	return ret;
}

int32_t npu_host_load_network_v2(struct npu_client *client,
			struct msm_npu_load_network_ioctl_v2 *load_ioctl,
			struct msm_npu_patch_info_v2 *patch_info)
{
	int ret = 0, i;
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_pwrctrl *pwr = &npu_dev->pwrctrl;
	struct npu_network *network;
	struct ipc_cmd_load_pkt_v2 *load_packet = NULL;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	uint32_t num_patch_params, pkt_size;

	ret = fw_init(npu_dev);
	if (ret)
		return ret;

	mutex_lock(&host_ctx->lock);
	network = alloc_network(host_ctx, client);
	if (!network) {
		ret = -ENOMEM;
		goto err_deinit_fw;
	}

	network_get(network);
	num_patch_params = load_ioctl->patch_info_num;
	pkt_size = sizeof(*load_packet) +
		num_patch_params * sizeof(struct npu_patch_tuple_v2);
	load_packet = kzalloc(pkt_size, GFP_KERNEL);

	if (!load_packet) {
		ret = -ENOMEM;
		goto error_free_network;
	}

	for (i = 0; i < num_patch_params; i++)
		host_copy_patch_data_v2(&load_packet->patch_params[i],
			&patch_info[i]);

	network->buf_hdl = load_ioctl->buf_ion_hdl;
	network->size = load_ioctl->buf_size;
	network->phy_add = load_ioctl->buf_phys_addr;
	network->first_block_size = load_ioctl->first_block_size;
	network->priority = load_ioctl->priority;
	network->cur_perf_mode = network->init_perf_mode =
		(load_ioctl->perf_mode == PERF_MODE_DEFAULT) ?
		pwr->num_pwrlevels : load_ioctl->perf_mode;
	network->num_layers = load_ioctl->num_layers;

	/* verify mapped physical address */
	if (!npu_mem_verify_addr(client, network->phy_add)) {
		pr_err("Invalid network address %llx\n", network->phy_add);
		ret = -EINVAL;
		goto error_free_network;
	}

	load_packet->header.cmd_type = NPU_IPC_CMD_LOAD_V2;
	load_packet->header.size = pkt_size;
	load_packet->header.trans_id =
		atomic_add_return(1, &host_ctx->ipc_trans_id);
	load_packet->header.flags = 0;

	/* ACO Buffer. Use the npu mapped aco address */
	load_packet->buf_pkt.address = (uint32_t)network->phy_add;
	load_packet->buf_pkt.buf_size = network->first_block_size;
	load_packet->buf_pkt.network_id = network->id;
	load_packet->buf_pkt.num_layers = network->num_layers;
	load_packet->num_patch_params = num_patch_params;

	set_perf_mode(npu_dev);
	/* NPU_IPC_CMD_LOAD_V2 will go onto IPC_QUEUE_APPS_EXEC */
	reinit_completion(&network->cmd_done);
	ret = npu_send_network_cmd(npu_dev, network, load_packet);
	if (ret) {
		pr_debug("NPU_IPC_CMD_LOAD_V2 sent failed: %d\n", ret);
		goto error_free_network;
	}

	mutex_unlock(&host_ctx->lock);

	ret = wait_for_completion_timeout(
		&network->cmd_done,
		(host_ctx->fw_dbg_mode & FW_DBG_MODE_INC_TIMEOUT) ?
		NW_DEBUG_TIMEOUT : NW_CMD_TIMEOUT);

	mutex_lock(&host_ctx->lock);

	if (!ret) {
		pr_err_ratelimited("npu: NPU_IPC_CMD_LOAD_V2 time out\n");
		ret = -ETIMEDOUT;
		goto error_free_network;
	}

	if (network->fw_error) {
		ret = -EIO;
		pr_err("fw is in error state during load_v2 network\n");
		goto error_free_network;
	}

	ret = network->cmd_ret_status;
	if (ret)
		goto error_free_network;

	load_ioctl->network_hdl = network->network_hdl;
	network->is_active = true;
	kfree(load_packet);
	network_put(network);

	mutex_unlock(&host_ctx->lock);

	return ret;

error_free_network:
	kfree(load_packet);
	network_put(network);
	free_network(host_ctx, client, network->id);
err_deinit_fw:
	mutex_unlock(&host_ctx->lock);
	fw_deinit(npu_dev, false, true);
	return ret;
}

int32_t npu_host_unload_network(struct npu_client *client,
			struct msm_npu_unload_network_ioctl *unload)
{
	int ret = 0, retry_cnt = 1;
	struct npu_device *npu_dev = client->npu_dev;
	struct ipc_cmd_unload_pkt unload_packet;
	struct npu_network *network;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	/* get the corresponding network for ipc trans id purpose */
	mutex_lock(&host_ctx->lock);
	network = get_network_by_hdl(host_ctx, client,
		unload->network_hdl);
	if (!network) {
		mutex_unlock(&host_ctx->lock);
		return -EINVAL;
	}

	if (network->is_unloading) {
		pr_err("network is unloading\n");
		network_put(network);
		mutex_unlock(&host_ctx->lock);
		return -EINVAL;
	}

	if (!network->is_active) {
		pr_err("network is not active\n");
		network_put(network);
		mutex_unlock(&host_ctx->lock);
		return -EINVAL;
	}

	if (network->is_executing) {
		pr_err("network is in execution\n");
		network_put(network);
		mutex_unlock(&host_ctx->lock);
		return -EINVAL;
	}

	if (network->fw_error) {
		pr_err("fw in error state, skip unload network in fw\n");
		goto free_network;
	}

	network->is_unloading = true;

	pr_debug("Unload network %lld\n", network->id);
	/* prepare IPC packet for UNLOAD */
	unload_packet.header.cmd_type = NPU_IPC_CMD_UNLOAD;
	unload_packet.header.size = sizeof(struct ipc_cmd_unload_pkt);
	unload_packet.header.trans_id =
		atomic_add_return(1, &host_ctx->ipc_trans_id);
	unload_packet.header.flags = 0;
	unload_packet.network_hdl = (uint32_t)network->network_hdl;

retry:
	/* NPU_IPC_CMD_UNLOAD will go onto IPC_QUEUE_APPS_EXEC */
	reinit_completion(&network->cmd_done);
	ret = npu_send_network_cmd(npu_dev, network, &unload_packet);

	if (ret) {
		pr_err("NPU_IPC_CMD_UNLOAD sent failed: %d\n", ret);
		/*
		 * If another command is running on this network,
		 * retry after 500ms.
		 */
		if ((ret == -EBUSY) && (retry_cnt > 0)) {
			pr_err("Network is running, retry later\n");
			mutex_unlock(&host_ctx->lock);
			retry_cnt--;
			msleep(500);
			mutex_lock(&host_ctx->lock);
			goto retry;
		}
		goto free_network;
	}

	mutex_unlock(&host_ctx->lock);

	ret = wait_for_completion_timeout(
		&network->cmd_done,
		(host_ctx->fw_dbg_mode & FW_DBG_MODE_INC_TIMEOUT) ?
		NW_DEBUG_TIMEOUT : NW_CMD_TIMEOUT);

	mutex_lock(&host_ctx->lock);

	if (!ret) {
		pr_err_ratelimited("npu: NPU_IPC_CMD_UNLOAD time out\n");
		network->cmd_pending = false;
		ret = -ETIMEDOUT;
		goto free_network;
	}

	if (network->fw_error) {
		ret = -EIO;
		pr_err("fw is in error state during unload network\n");
	} else {
		ret = network->cmd_ret_status;
		pr_debug("unload network status %d\n", ret);
	}

free_network:
	/*
	 * free the network on the kernel if the corresponding ACO
	 * handle is unloaded on the firmware side
	 */
	network_put(network);
	free_network(host_ctx, client, network->id);

	/* recalculate uc_power_level after unload network */
	if (npu_dev->pwrctrl.cur_dcvs_activity)
		set_perf_mode(npu_dev);

	mutex_unlock(&host_ctx->lock);
	if (host_ctx->fw_unload_delay_ms) {
		flush_delayed_work(&host_ctx->fw_deinit_work);
		atomic_inc(&host_ctx->fw_deinit_work_cnt);
		queue_delayed_work(host_ctx->wq, &host_ctx->fw_deinit_work,
			msecs_to_jiffies(host_ctx->fw_unload_delay_ms));
	} else {
		fw_deinit(npu_dev, false, true);
	}
	return ret;
}

int32_t npu_host_exec_network(struct npu_client *client,
			struct msm_npu_exec_network_ioctl *exec_ioctl)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct ipc_cmd_execute_pkt exec_packet;
	/* npu mapped addr */
	uint64_t input_off, output_off;
	int32_t ret;
	struct npu_network *network;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;

	mutex_lock(&host_ctx->lock);
	network = get_network_by_hdl(host_ctx, client,
		exec_ioctl->network_hdl);

	if (!network) {
		mutex_unlock(&host_ctx->lock);
		return -EINVAL;
	}

	if (atomic_inc_return(&host_ctx->network_execute_cnt) == 1)
		npu_notify_cdsprm_cxlimit_activity(npu_dev, true);

	if (!network->is_active) {
		pr_err("network is not active\n");
		ret = -EINVAL;
		goto exec_done;
	}

	if (network->fw_error) {
		pr_err("fw is in error state\n");
		ret = -EIO;
		goto exec_done;
	}

	pr_debug("execute network %lld\n", network->id);
	memset(&exec_packet, 0, sizeof(exec_packet));
	if (exec_ioctl->patching_required) {
		if ((exec_ioctl->input_layer_num != 1) ||
			(exec_ioctl->output_layer_num != 1)) {
			pr_err("Invalid input/output layer num\n");
			ret = -EINVAL;
			goto exec_done;
		}

		input_off = exec_ioctl->input_layers[0].buf_phys_addr;
		output_off = exec_ioctl->output_layers[0].buf_phys_addr;
		/* verify mapped physical address */
		if (!npu_mem_verify_addr(client, input_off) ||
			!npu_mem_verify_addr(client, output_off)) {
			pr_err("Invalid patch buf address\n");
			ret = -EINVAL;
			goto exec_done;
		}

		exec_packet.patch_params.num_params = 2;
		host_copy_patch_data(&exec_packet.patch_params.param[0],
			(uint32_t)input_off, &exec_ioctl->input_layers[0]);
		host_copy_patch_data(&exec_packet.patch_params.param[1],
			(uint32_t)output_off, &exec_ioctl->output_layers[0]);
	} else {
		exec_packet.patch_params.num_params = 0;
	}

	exec_packet.header.cmd_type = NPU_IPC_CMD_EXECUTE;
	exec_packet.header.size = sizeof(struct ipc_cmd_execute_pkt);
	exec_packet.header.trans_id =
		atomic_add_return(1, &host_ctx->ipc_trans_id);
	exec_packet.header.flags = 0xF;
	exec_packet.network_hdl = network->network_hdl;

	/* Send it on the high priority queue */
	reinit_completion(&network->cmd_done);
	ret = npu_send_network_cmd(npu_dev, network, &exec_packet);

	if (ret) {
		pr_err("NPU_IPC_CMD_EXECUTE sent failed: %d\n", ret);
		goto exec_done;
	}

	mutex_unlock(&host_ctx->lock);

	ret = wait_for_completion_timeout(
		&network->cmd_done,
		(host_ctx->fw_dbg_mode & FW_DBG_MODE_INC_TIMEOUT) ?
		NW_DEBUG_TIMEOUT : NW_CMD_TIMEOUT);

	mutex_lock(&host_ctx->lock);
	if (!ret) {
		pr_err_ratelimited("npu: NPU_IPC_CMD_EXECUTE time out\n");
		/* dump debug stats */
		npu_dump_debug_timeout_stats(npu_dev);
		network->cmd_pending = false;
		ret = -ETIMEDOUT;
		goto exec_done;
	}

	if (network->fw_error) {
		ret = -EIO;
		pr_err("fw is in error state during execute network\n");
	} else {
		ret = network->cmd_ret_status;
		pr_debug("execution status %d\n", ret);
	}

exec_done:
	network_put(network);
	mutex_unlock(&host_ctx->lock);

	/*
	 * treat network execution timed our or interrupted by signal
	 * as error in order to force npu fw to stop execution
	 */
	if ((ret == -ETIMEDOUT) || (ret == -ERESTARTSYS)) {
		pr_err("Error handling after execution failure\n");
		host_error_hdlr(npu_dev, true);
	}

	if (atomic_dec_return(&host_ctx->network_execute_cnt) == 0)
		npu_notify_cdsprm_cxlimit_activity(npu_dev, false);

	return ret;
}

int32_t npu_host_exec_network_v2(struct npu_client *client,
	struct msm_npu_exec_network_ioctl_v2 *exec_ioctl,
	struct msm_npu_patch_buf_info *patch_buf_info)
{
	struct npu_device *npu_dev = client->npu_dev;
	struct ipc_cmd_execute_pkt_v2 *exec_packet;
	int32_t ret;
	struct npu_network *network;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	uint32_t num_patch_params, pkt_size;
	int i;

	mutex_lock(&host_ctx->lock);
	network = get_network_by_hdl(host_ctx, client,
		exec_ioctl->network_hdl);

	if (!network) {
		mutex_unlock(&host_ctx->lock);
		return -EINVAL;
	}

	if (atomic_inc_return(&host_ctx->network_execute_cnt) == 1)
		npu_notify_cdsprm_cxlimit_activity(npu_dev, true);

	if (network->is_unloading) {
		pr_err("network is unloading\n");
		ret = -EINVAL;
		goto exec_v2_done;
	}

	if (!network->is_active) {
		pr_err("network is not active\n");
		ret = -EINVAL;
		goto exec_v2_done;
	}

	if (network->is_executing) {
		pr_err("network is already in execution\n");
		ret = -EINVAL;
		goto exec_v2_done;
	}

	if (network->fw_error) {
		pr_err("fw is in error state\n");
		ret = -EIO;
		goto exec_v2_done;
	}

	pr_debug("execute_v2 network %lld\n", network->id);
	num_patch_params = exec_ioctl->patch_buf_info_num;
	pkt_size = num_patch_params * sizeof(struct npu_patch_params_v2) +
		sizeof(*exec_packet);
	exec_packet = kzalloc(pkt_size, GFP_KERNEL);

	if (!exec_packet) {
		ret = -ENOMEM;
		goto exec_v2_done;
	}

	network->is_executing = true;
	for (i = 0; i < num_patch_params; i++) {
		exec_packet->patch_params[i].id = patch_buf_info[i].buf_id;
		pr_debug("%d: patch_id: %x\n", i,
			exec_packet->patch_params[i].id);
		exec_packet->patch_params[i].value =
			patch_buf_info[i].buf_phys_addr;
		pr_debug("%d: patch value: %x\n", i,
			exec_packet->patch_params[i].value);

		/* verify mapped physical address */
		if (!npu_mem_verify_addr(client,
			patch_buf_info[i].buf_phys_addr)) {
			pr_err("Invalid patch value\n");
			ret = -EINVAL;
			goto free_exec_packet;
		}
	}

	exec_packet->header.cmd_type = NPU_IPC_CMD_EXECUTE_V2;
	exec_packet->header.size = pkt_size;
	exec_packet->header.trans_id =
		atomic_add_return(1, &host_ctx->ipc_trans_id);
	exec_packet->header.flags = host_ctx->exec_flags_override > 0 ?
		host_ctx->exec_flags_override : exec_ioctl->flags;
	exec_packet->network_hdl = network->network_hdl;
	exec_packet->num_patch_params = num_patch_params;

	network->stats_buf_u = (void __user *)exec_ioctl->stats_buf_addr;
	network->stats_buf_size = exec_ioctl->stats_buf_size;

	pr_debug("Execute_v2 flags %x stats_buf_size %d\n",
		exec_packet->header.flags, exec_ioctl->stats_buf_size);

	/* Send it on the high priority queue */
	reinit_completion(&network->cmd_done);
	ret = npu_send_network_cmd(npu_dev, network, exec_packet);

	if (ret) {
		pr_err("NPU_IPC_CMD_EXECUTE_V2 sent failed: %d\n", ret);
		goto free_exec_packet;
	}

	mutex_unlock(&host_ctx->lock);

	ret = wait_for_completion_timeout(
		&network->cmd_done,
		(host_ctx->fw_dbg_mode & FW_DBG_MODE_INC_TIMEOUT) ?
		NW_DEBUG_TIMEOUT : NW_CMD_TIMEOUT);

	mutex_lock(&host_ctx->lock);
	if (!ret) {
		pr_err_ratelimited("npu: NPU_IPC_CMD_EXECUTE_V2 time out\n");
		/* dump debug stats */
		npu_dump_debug_timeout_stats(npu_dev);
		network->cmd_pending = false;
		ret = -ETIMEDOUT;
		goto free_exec_packet;
	}

	if (network->fw_error) {
		ret = -EIO;
		pr_err("fw is in error state during execute_v2 network\n");
		goto free_exec_packet;
	}

	ret = network->cmd_ret_status;
	if (!ret) {
		exec_ioctl->stats_buf_size = network->stats_buf_size;
		if (copy_to_user(
			(void __user *)exec_ioctl->stats_buf_addr,
			network->stats_buf,
			exec_ioctl->stats_buf_size)) {
			pr_err("copy stats to user failed\n");
			exec_ioctl->stats_buf_size = 0;
		}
	} else {
		pr_err("execution failed %d\n", ret);
	}

free_exec_packet:
	kfree(exec_packet);
	network->is_executing = false;
exec_v2_done:
	network_put(network);
	mutex_unlock(&host_ctx->lock);

	/*
	 * treat network execution timed our or interrupted by signal
	 * as error in order to force npu fw to stop execution
	 */
	if ((ret == -ETIMEDOUT) || (ret == -ERESTARTSYS)) {
		pr_err("Error handling after execution failure\n");
		host_error_hdlr(npu_dev, true);
	}

	if (atomic_dec_return(&host_ctx->network_execute_cnt) == 0)
		npu_notify_cdsprm_cxlimit_activity(npu_dev, false);

	return ret;
}

int32_t npu_host_loopback_test(struct npu_device *npu_dev)
{
	struct ipc_cmd_loopback_pkt loopback_packet;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	int32_t ret;

	ret = fw_init(npu_dev);
	if (ret)
		return ret;

	loopback_packet.header.cmd_type = NPU_IPC_CMD_LOOPBACK;
	loopback_packet.header.size = sizeof(struct ipc_cmd_loopback_pkt);
	loopback_packet.header.trans_id =
		atomic_add_return(1, &host_ctx->ipc_trans_id);
	loopback_packet.header.flags = 0;
	loopback_packet.loopbackParams = 15;

	ret = npu_send_misc_cmd(npu_dev, IPC_QUEUE_APPS_EXEC, &loopback_packet);

	if (ret) {
		pr_err("NPU_IPC_CMD_LOOPBACK sent failed: %d\n", ret);
		goto loopback_exit;
	}

	ret = wait_for_completion_interruptible_timeout(
		&host_ctx->misc_done,
		(host_ctx->fw_dbg_mode & FW_DBG_MODE_INC_TIMEOUT) ?
		NW_DEBUG_TIMEOUT : NW_CMD_TIMEOUT);

	if (!ret) {
		pr_err_ratelimited("npu: NPU_IPC_CMD_LOOPBACK time out\n");
		ret = -ETIMEDOUT;
	} else if (ret < 0) {
		pr_err("Wait for loopback done interrupted by signal\n");
	}

loopback_exit:
	fw_deinit(npu_dev, false, true);

	return ret;
}

void npu_host_cleanup_networks(struct npu_client *client)
{
	int i;
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	struct msm_npu_unload_network_ioctl unload_req;
	struct msm_npu_unmap_buf_ioctl unmap_req;
	struct npu_network *network;
	struct npu_ion_buf *ion_buf;

	for (i = 0; i < MAX_LOADED_NETWORK; i++) {
		network = &host_ctx->networks[i];
		if (network->client == client) {
			pr_warn("network %d is not unloaded before close\n",
				network->network_hdl);
			unload_req.network_hdl = network->network_hdl;
			npu_host_unload_network(client, &unload_req);
		}
	}

	/* unmap all remaining buffers */
	while (!list_empty(&client->mapped_buffer_list)) {
		ion_buf = list_first_entry(&client->mapped_buffer_list,
			struct npu_ion_buf, list);
		pr_warn("unmap buffer %x:%llx\n", ion_buf->fd, ion_buf->iova);
		unmap_req.buf_ion_hdl = ion_buf->fd;
		unmap_req.npu_phys_addr = ion_buf->iova;
		npu_host_unmap_buf(client, &unmap_req);
	}
}

/*
 * set network or global perf_mode
 * if network_hdl is 0, set global perf_mode_override
 * otherwise set network perf_mode: if perf_mode is 0,
 * change network perf_mode to initial perf_mode from
 * load_network
 */
int32_t npu_host_set_perf_mode(struct npu_client *client, uint32_t network_hdl,
	uint32_t perf_mode)
{
	int ret = 0;
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	struct npu_network *network = NULL;

	mutex_lock(&host_ctx->lock);

	if (network_hdl == 0) {
		pr_debug("change perf_mode_override to %d\n", perf_mode);
		npu_dev->pwrctrl.perf_mode_override = perf_mode;
	} else {
		network = get_network_by_hdl(host_ctx, client, network_hdl);
		if (!network) {
			pr_err("invalid network handle %x\n", network_hdl);
			mutex_unlock(&host_ctx->lock);
			return -EINVAL;
		}

		if (perf_mode == 0) {
			network->cur_perf_mode = network->init_perf_mode;
			pr_debug("change network %d perf_mode back to %d\n",
				network_hdl, network->cur_perf_mode);
		} else {
			network->cur_perf_mode = perf_mode;
			pr_debug("change network %d perf_mode to %d\n",
				network_hdl, network->cur_perf_mode);
		}
	}

	ret = set_perf_mode(npu_dev);
	if (ret)
		pr_err("set_perf_mode failed\n");

	if (network)
		network_put(network);
	mutex_unlock(&host_ctx->lock);

	return ret;
}

/*
 * get the currently set network or global perf_mode
 * if network_hdl is 0, get global perf_mode_override
 * otherwise get network perf_mode
 */
int32_t npu_host_get_perf_mode(struct npu_client *client, uint32_t network_hdl)
{
	int param_val = 0;
	struct npu_device *npu_dev = client->npu_dev;
	struct npu_host_ctx *host_ctx = &npu_dev->host_ctx;
	struct npu_network *network = NULL;

	mutex_lock(&host_ctx->lock);

	if (network_hdl == 0) {
		param_val = npu_dev->pwrctrl.perf_mode_override;
	} else {
		network = get_network_by_hdl(host_ctx, client, network_hdl);
		if (!network) {
			pr_err("invalid network handle %x\n", network_hdl);
			mutex_unlock(&host_ctx->lock);
			return -EINVAL;
		}
		param_val = network->cur_perf_mode;
		network_put(network);
	}

	mutex_unlock(&host_ctx->lock);

	return param_val;
}
