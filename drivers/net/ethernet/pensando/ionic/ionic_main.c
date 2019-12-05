// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/printk.h>
#include <linux/dynamic_debug.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/utsname.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_debugfs.h"

MODULE_DESCRIPTION(IONIC_DRV_DESCRIPTION);
MODULE_AUTHOR("Pensando Systems, Inc");
MODULE_LICENSE("GPL");
MODULE_VERSION(IONIC_DRV_VERSION);

static const char *ionic_error_to_str(enum ionic_status_code code)
{
	switch (code) {
	case IONIC_RC_SUCCESS:
		return "IONIC_RC_SUCCESS";
	case IONIC_RC_EVERSION:
		return "IONIC_RC_EVERSION";
	case IONIC_RC_EOPCODE:
		return "IONIC_RC_EOPCODE";
	case IONIC_RC_EIO:
		return "IONIC_RC_EIO";
	case IONIC_RC_EPERM:
		return "IONIC_RC_EPERM";
	case IONIC_RC_EQID:
		return "IONIC_RC_EQID";
	case IONIC_RC_EQTYPE:
		return "IONIC_RC_EQTYPE";
	case IONIC_RC_ENOENT:
		return "IONIC_RC_ENOENT";
	case IONIC_RC_EINTR:
		return "IONIC_RC_EINTR";
	case IONIC_RC_EAGAIN:
		return "IONIC_RC_EAGAIN";
	case IONIC_RC_ENOMEM:
		return "IONIC_RC_ENOMEM";
	case IONIC_RC_EFAULT:
		return "IONIC_RC_EFAULT";
	case IONIC_RC_EBUSY:
		return "IONIC_RC_EBUSY";
	case IONIC_RC_EEXIST:
		return "IONIC_RC_EEXIST";
	case IONIC_RC_EINVAL:
		return "IONIC_RC_EINVAL";
	case IONIC_RC_ENOSPC:
		return "IONIC_RC_ENOSPC";
	case IONIC_RC_ERANGE:
		return "IONIC_RC_ERANGE";
	case IONIC_RC_BAD_ADDR:
		return "IONIC_RC_BAD_ADDR";
	case IONIC_RC_DEV_CMD:
		return "IONIC_RC_DEV_CMD";
	case IONIC_RC_ERROR:
		return "IONIC_RC_ERROR";
	case IONIC_RC_ERDMA:
		return "IONIC_RC_ERDMA";
	default:
		return "IONIC_RC_UNKNOWN";
	}
}

static int ionic_error_to_errno(enum ionic_status_code code)
{
	switch (code) {
	case IONIC_RC_SUCCESS:
		return 0;
	case IONIC_RC_EVERSION:
	case IONIC_RC_EQTYPE:
	case IONIC_RC_EQID:
	case IONIC_RC_EINVAL:
		return -EINVAL;
	case IONIC_RC_EPERM:
		return -EPERM;
	case IONIC_RC_ENOENT:
		return -ENOENT;
	case IONIC_RC_EAGAIN:
		return -EAGAIN;
	case IONIC_RC_ENOMEM:
		return -ENOMEM;
	case IONIC_RC_EFAULT:
		return -EFAULT;
	case IONIC_RC_EBUSY:
		return -EBUSY;
	case IONIC_RC_EEXIST:
		return -EEXIST;
	case IONIC_RC_ENOSPC:
		return -ENOSPC;
	case IONIC_RC_ERANGE:
		return -ERANGE;
	case IONIC_RC_BAD_ADDR:
		return -EFAULT;
	case IONIC_RC_EOPCODE:
	case IONIC_RC_EINTR:
	case IONIC_RC_DEV_CMD:
	case IONIC_RC_ERROR:
	case IONIC_RC_ERDMA:
	case IONIC_RC_EIO:
	default:
		return -EIO;
	}
}

static const char *ionic_opcode_to_str(enum ionic_cmd_opcode opcode)
{
	switch (opcode) {
	case IONIC_CMD_NOP:
		return "IONIC_CMD_NOP";
	case IONIC_CMD_INIT:
		return "IONIC_CMD_INIT";
	case IONIC_CMD_RESET:
		return "IONIC_CMD_RESET";
	case IONIC_CMD_IDENTIFY:
		return "IONIC_CMD_IDENTIFY";
	case IONIC_CMD_GETATTR:
		return "IONIC_CMD_GETATTR";
	case IONIC_CMD_SETATTR:
		return "IONIC_CMD_SETATTR";
	case IONIC_CMD_PORT_IDENTIFY:
		return "IONIC_CMD_PORT_IDENTIFY";
	case IONIC_CMD_PORT_INIT:
		return "IONIC_CMD_PORT_INIT";
	case IONIC_CMD_PORT_RESET:
		return "IONIC_CMD_PORT_RESET";
	case IONIC_CMD_PORT_GETATTR:
		return "IONIC_CMD_PORT_GETATTR";
	case IONIC_CMD_PORT_SETATTR:
		return "IONIC_CMD_PORT_SETATTR";
	case IONIC_CMD_LIF_INIT:
		return "IONIC_CMD_LIF_INIT";
	case IONIC_CMD_LIF_RESET:
		return "IONIC_CMD_LIF_RESET";
	case IONIC_CMD_LIF_IDENTIFY:
		return "IONIC_CMD_LIF_IDENTIFY";
	case IONIC_CMD_LIF_SETATTR:
		return "IONIC_CMD_LIF_SETATTR";
	case IONIC_CMD_LIF_GETATTR:
		return "IONIC_CMD_LIF_GETATTR";
	case IONIC_CMD_RX_MODE_SET:
		return "IONIC_CMD_RX_MODE_SET";
	case IONIC_CMD_RX_FILTER_ADD:
		return "IONIC_CMD_RX_FILTER_ADD";
	case IONIC_CMD_RX_FILTER_DEL:
		return "IONIC_CMD_RX_FILTER_DEL";
	case IONIC_CMD_Q_INIT:
		return "IONIC_CMD_Q_INIT";
	case IONIC_CMD_Q_CONTROL:
		return "IONIC_CMD_Q_CONTROL";
	case IONIC_CMD_RDMA_RESET_LIF:
		return "IONIC_CMD_RDMA_RESET_LIF";
	case IONIC_CMD_RDMA_CREATE_EQ:
		return "IONIC_CMD_RDMA_CREATE_EQ";
	case IONIC_CMD_RDMA_CREATE_CQ:
		return "IONIC_CMD_RDMA_CREATE_CQ";
	case IONIC_CMD_RDMA_CREATE_ADMINQ:
		return "IONIC_CMD_RDMA_CREATE_ADMINQ";
	case IONIC_CMD_FW_DOWNLOAD:
		return "IONIC_CMD_FW_DOWNLOAD";
	case IONIC_CMD_FW_CONTROL:
		return "IONIC_CMD_FW_CONTROL";
	default:
		return "DEVCMD_UNKNOWN";
	}
}

static void ionic_adminq_flush(struct ionic_lif *lif)
{
	struct ionic_queue *adminq = &lif->adminqcq->q;

	spin_lock(&lif->adminq_lock);

	while (adminq->tail != adminq->head) {
		memset(adminq->tail->desc, 0, sizeof(union ionic_adminq_cmd));
		adminq->tail->cb = NULL;
		adminq->tail->cb_arg = NULL;
		adminq->tail = adminq->tail->next;
	}
	spin_unlock(&lif->adminq_lock);
}

static int ionic_adminq_check_err(struct ionic_lif *lif,
				  struct ionic_admin_ctx *ctx,
				  bool timeout)
{
	struct net_device *netdev = lif->netdev;
	const char *opcode_str;
	const char *status_str;
	int err = 0;

	if (ctx->comp.comp.status || timeout) {
		opcode_str = ionic_opcode_to_str(ctx->cmd.cmd.opcode);
		status_str = ionic_error_to_str(ctx->comp.comp.status);
		err = timeout ? -ETIMEDOUT :
				ionic_error_to_errno(ctx->comp.comp.status);

		netdev_err(netdev, "%s (%d) failed: %s (%d)\n",
			   opcode_str, ctx->cmd.cmd.opcode,
			   timeout ? "TIMEOUT" : status_str, err);

		if (timeout)
			ionic_adminq_flush(lif);
	}

	return err;
}

static void ionic_adminq_cb(struct ionic_queue *q,
			    struct ionic_desc_info *desc_info,
			    struct ionic_cq_info *cq_info, void *cb_arg)
{
	struct ionic_admin_ctx *ctx = cb_arg;
	struct ionic_admin_comp *comp;
	struct device *dev;

	if (!ctx)
		return;

	comp = cq_info->cq_desc;
	dev = &q->lif->netdev->dev;

	memcpy(&ctx->comp, comp, sizeof(*comp));

	dev_dbg(dev, "comp admin queue command:\n");
	dynamic_hex_dump("comp ", DUMP_PREFIX_OFFSET, 16, 1,
			 &ctx->comp, sizeof(ctx->comp), true);

	complete_all(&ctx->work);
}

static int ionic_adminq_post(struct ionic_lif *lif, struct ionic_admin_ctx *ctx)
{
	struct ionic_queue *adminq = &lif->adminqcq->q;
	int err = 0;

	WARN_ON(in_interrupt());

	spin_lock(&lif->adminq_lock);
	if (!ionic_q_has_space(adminq, 1)) {
		err = -ENOSPC;
		goto err_out;
	}

	err = ionic_heartbeat_check(lif->ionic);
	if (err)
		goto err_out;

	memcpy(adminq->head->desc, &ctx->cmd, sizeof(ctx->cmd));

	dev_dbg(&lif->netdev->dev, "post admin queue command:\n");
	dynamic_hex_dump("cmd ", DUMP_PREFIX_OFFSET, 16, 1,
			 &ctx->cmd, sizeof(ctx->cmd), true);

	ionic_q_post(adminq, true, ionic_adminq_cb, ctx);

err_out:
	spin_unlock(&lif->adminq_lock);

	return err;
}

int ionic_adminq_post_wait(struct ionic_lif *lif, struct ionic_admin_ctx *ctx)
{
	struct net_device *netdev = lif->netdev;
	unsigned long remaining;
	const char *name;
	int err;

	err = ionic_adminq_post(lif, ctx);
	if (err) {
		name = ionic_opcode_to_str(ctx->cmd.cmd.opcode);
		netdev_err(netdev, "Posting of %s (%d) failed: %d\n",
			   name, ctx->cmd.cmd.opcode, err);
		return err;
	}

	remaining = wait_for_completion_timeout(&ctx->work,
						HZ * (ulong)DEVCMD_TIMEOUT);
	return ionic_adminq_check_err(lif, ctx, (remaining == 0));
}

int ionic_napi(struct napi_struct *napi, int budget, ionic_cq_cb cb,
	       ionic_cq_done_cb done_cb, void *done_arg)
{
	struct ionic_qcq *qcq = napi_to_qcq(napi);
	struct ionic_cq *cq = &qcq->cq;
	u32 work_done, flags = 0;

	work_done = ionic_cq_service(cq, budget, cb, done_cb, done_arg);

	if (work_done < budget && napi_complete_done(napi, work_done)) {
		flags |= IONIC_INTR_CRED_UNMASK;
		DEBUG_STATS_INTR_REARM(cq->bound_intr);
	}

	if (work_done || flags) {
		flags |= IONIC_INTR_CRED_RESET_COALESCE;
		ionic_intr_credits(cq->lif->ionic->idev.intr_ctrl,
				   cq->bound_intr->index,
				   work_done, flags);
	}

	DEBUG_STATS_NAPI_POLL(qcq, work_done);

	return work_done;
}

static void ionic_dev_cmd_clean(struct ionic *ionic)
{
	union ionic_dev_cmd_regs *regs = ionic->idev.dev_cmd_regs;

	iowrite32(0, &regs->doorbell);
	memset_io(&regs->cmd, 0, sizeof(regs->cmd));
}

int ionic_dev_cmd_wait(struct ionic *ionic, unsigned long max_seconds)
{
	struct ionic_dev *idev = &ionic->idev;
	unsigned long start_time;
	unsigned long max_wait;
	unsigned long duration;
	int opcode;
	int done;
	int err;
	int hb;

	WARN_ON(in_interrupt());

	/* Wait for dev cmd to complete, retrying if we get EAGAIN,
	 * but don't wait any longer than max_seconds.
	 */
	max_wait = jiffies + (max_seconds * HZ);
try_again:
	start_time = jiffies;
	do {
		done = ionic_dev_cmd_done(idev);
		if (done)
			break;
		msleep(20);
		hb = ionic_heartbeat_check(ionic);
	} while (!done && !hb && time_before(jiffies, max_wait));
	duration = jiffies - start_time;

	opcode = idev->dev_cmd_regs->cmd.cmd.opcode;
	dev_dbg(ionic->dev, "DEVCMD %s (%d) done=%d took %ld secs (%ld jiffies)\n",
		ionic_opcode_to_str(opcode), opcode,
		done, duration / HZ, duration);

	if (!done && hb) {
		ionic_dev_cmd_clean(ionic);
		dev_warn(ionic->dev, "DEVCMD %s (%d) failed - FW halted\n",
			 ionic_opcode_to_str(opcode), opcode);
		return -ENXIO;
	}

	if (!done && !time_before(jiffies, max_wait)) {
		ionic_dev_cmd_clean(ionic);
		dev_warn(ionic->dev, "DEVCMD %s (%d) timeout after %ld secs\n",
			 ionic_opcode_to_str(opcode), opcode, max_seconds);
		return -ETIMEDOUT;
	}

	err = ionic_dev_cmd_status(&ionic->idev);
	if (err) {
		if (err == IONIC_RC_EAGAIN && !time_after(jiffies, max_wait)) {
			dev_err(ionic->dev, "DEV_CMD %s (%d) error, %s (%d) retrying...\n",
				ionic_opcode_to_str(opcode), opcode,
				ionic_error_to_str(err), err);

			msleep(1000);
			iowrite32(0, &idev->dev_cmd_regs->done);
			iowrite32(1, &idev->dev_cmd_regs->doorbell);
			goto try_again;
		}

		dev_err(ionic->dev, "DEV_CMD %s (%d) error, %s (%d) failed\n",
			ionic_opcode_to_str(opcode), opcode,
			ionic_error_to_str(err), err);

		return ionic_error_to_errno(err);
	}

	return 0;
}

int ionic_setup(struct ionic *ionic)
{
	int err;

	err = ionic_dev_setup(ionic);
	if (err)
		return err;

	return 0;
}

int ionic_identify(struct ionic *ionic)
{
	struct ionic_identity *ident = &ionic->ident;
	struct ionic_dev *idev = &ionic->idev;
	size_t sz;
	int err;

	memset(ident, 0, sizeof(*ident));

	ident->drv.os_type = cpu_to_le32(IONIC_OS_TYPE_LINUX);
	strncpy(ident->drv.driver_ver_str, IONIC_DRV_VERSION,
		sizeof(ident->drv.driver_ver_str) - 1);

	mutex_lock(&ionic->dev_cmd_lock);

	sz = min(sizeof(ident->drv), sizeof(idev->dev_cmd_regs->data));
	memcpy_toio(&idev->dev_cmd_regs->data, &ident->drv, sz);

	ionic_dev_cmd_identify(idev, IONIC_IDENTITY_VERSION_1);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	if (!err) {
		sz = min(sizeof(ident->dev), sizeof(idev->dev_cmd_regs->data));
		memcpy_fromio(&ident->dev, &idev->dev_cmd_regs->data, sz);
	}

	mutex_unlock(&ionic->dev_cmd_lock);

	if (err)
		goto err_out_unmap;

	ionic_debugfs_add_ident(ionic);

	return 0;

err_out_unmap:
	return err;
}

int ionic_init(struct ionic *ionic)
{
	struct ionic_dev *idev = &ionic->idev;
	int err;

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_init(idev);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	mutex_unlock(&ionic->dev_cmd_lock);

	return err;
}

int ionic_reset(struct ionic *ionic)
{
	struct ionic_dev *idev = &ionic->idev;
	int err;

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_reset(idev);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	mutex_unlock(&ionic->dev_cmd_lock);

	return err;
}

int ionic_port_identify(struct ionic *ionic)
{
	struct ionic_identity *ident = &ionic->ident;
	struct ionic_dev *idev = &ionic->idev;
	size_t sz;
	int err;

	mutex_lock(&ionic->dev_cmd_lock);

	ionic_dev_cmd_port_identify(idev);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	if (!err) {
		sz = min(sizeof(ident->port), sizeof(idev->dev_cmd_regs->data));
		memcpy_fromio(&ident->port, &idev->dev_cmd_regs->data, sz);
	}

	mutex_unlock(&ionic->dev_cmd_lock);

	return err;
}

int ionic_port_init(struct ionic *ionic)
{
	struct ionic_identity *ident = &ionic->ident;
	struct ionic_dev *idev = &ionic->idev;
	size_t sz;
	int err;

	if (idev->port_info)
		return 0;

	idev->port_info_sz = ALIGN(sizeof(*idev->port_info), PAGE_SIZE);
	idev->port_info = dma_alloc_coherent(ionic->dev, idev->port_info_sz,
					     &idev->port_info_pa,
					     GFP_KERNEL);
	if (!idev->port_info) {
		dev_err(ionic->dev, "Failed to allocate port info, aborting\n");
		return -ENOMEM;
	}

	sz = min(sizeof(ident->port.config), sizeof(idev->dev_cmd_regs->data));

	mutex_lock(&ionic->dev_cmd_lock);

	memcpy_toio(&idev->dev_cmd_regs->data, &ident->port.config, sz);
	ionic_dev_cmd_port_init(idev);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);

	ionic_dev_cmd_port_state(&ionic->idev, IONIC_PORT_ADMIN_STATE_UP);
	(void)ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);

	mutex_unlock(&ionic->dev_cmd_lock);
	if (err) {
		dev_err(ionic->dev, "Failed to init port\n");
		dma_free_coherent(ionic->dev, idev->port_info_sz,
				  idev->port_info, idev->port_info_pa);
		idev->port_info = NULL;
		idev->port_info_pa = 0;
	}

	return err;
}

int ionic_port_reset(struct ionic *ionic)
{
	struct ionic_dev *idev = &ionic->idev;
	int err;

	if (!idev->port_info)
		return 0;

	mutex_lock(&ionic->dev_cmd_lock);
	ionic_dev_cmd_port_reset(idev);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	mutex_unlock(&ionic->dev_cmd_lock);

	dma_free_coherent(ionic->dev, idev->port_info_sz,
			  idev->port_info, idev->port_info_pa);

	idev->port_info = NULL;
	idev->port_info_pa = 0;

	if (err)
		dev_err(ionic->dev, "Failed to reset port\n");

	return err;
}

static int __init ionic_init_module(void)
{
	pr_info("%s %s, ver %s\n",
		IONIC_DRV_NAME, IONIC_DRV_DESCRIPTION, IONIC_DRV_VERSION);
	ionic_debugfs_create();
	return ionic_bus_register_driver();
}

static void __exit ionic_cleanup_module(void)
{
	ionic_bus_unregister_driver();
	ionic_debugfs_destroy();

	pr_info("%s removed\n", IONIC_DRV_NAME);
}

module_init(ionic_init_module);
module_exit(ionic_cleanup_module);
