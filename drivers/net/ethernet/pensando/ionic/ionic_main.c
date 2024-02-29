// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#include <linux/printk.h>
#include <linux/dynamic_debug.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/utsname.h>
#include <generated/utsrelease.h>
#include <linux/ctype.h>

#include "ionic.h"
#include "ionic_bus.h"
#include "ionic_lif.h"
#include "ionic_debugfs.h"

MODULE_DESCRIPTION(IONIC_DRV_DESCRIPTION);
MODULE_AUTHOR("Pensando Systems, Inc");
MODULE_LICENSE("GPL");

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
	case IONIC_RC_ENOSUPP:
		return "IONIC_RC_ENOSUPP";
	case IONIC_RC_ERROR:
		return "IONIC_RC_ERROR";
	case IONIC_RC_ERDMA:
		return "IONIC_RC_ERDMA";
	case IONIC_RC_EBAD_FW:
		return "IONIC_RC_EBAD_FW";
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
	case IONIC_RC_ENOSUPP:
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
	case IONIC_CMD_LIF_SETPHC:
		return "IONIC_CMD_LIF_SETPHC";
	case IONIC_CMD_RX_MODE_SET:
		return "IONIC_CMD_RX_MODE_SET";
	case IONIC_CMD_RX_FILTER_ADD:
		return "IONIC_CMD_RX_FILTER_ADD";
	case IONIC_CMD_RX_FILTER_DEL:
		return "IONIC_CMD_RX_FILTER_DEL";
	case IONIC_CMD_Q_IDENTIFY:
		return "IONIC_CMD_Q_IDENTIFY";
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
	case IONIC_CMD_FW_DOWNLOAD_V1:
		return "IONIC_CMD_FW_DOWNLOAD_V1";
	case IONIC_CMD_FW_CONTROL_V1:
		return "IONIC_CMD_FW_CONTROL_V1";
	case IONIC_CMD_VF_GETATTR:
		return "IONIC_CMD_VF_GETATTR";
	case IONIC_CMD_VF_SETATTR:
		return "IONIC_CMD_VF_SETATTR";
	default:
		return "DEVCMD_UNKNOWN";
	}
}

static void ionic_adminq_flush(struct ionic_lif *lif)
{
	struct ionic_desc_info *desc_info;
	unsigned long irqflags;
	struct ionic_queue *q;

	spin_lock_irqsave(&lif->adminq_lock, irqflags);
	if (!lif->adminqcq) {
		spin_unlock_irqrestore(&lif->adminq_lock, irqflags);
		return;
	}

	q = &lif->adminqcq->q;

	while (q->tail_idx != q->head_idx) {
		desc_info = &q->info[q->tail_idx];
		memset(desc_info->desc, 0, sizeof(union ionic_adminq_cmd));
		desc_info->cb = NULL;
		desc_info->cb_arg = NULL;
		q->tail_idx = (q->tail_idx + 1) & (q->num_descs - 1);
	}
	spin_unlock_irqrestore(&lif->adminq_lock, irqflags);
}

void ionic_adminq_netdev_err_print(struct ionic_lif *lif, u8 opcode,
				   u8 status, int err)
{
	const char *stat_str;

	stat_str = (err == -ETIMEDOUT) ? "TIMEOUT" :
					 ionic_error_to_str(status);

	netdev_err(lif->netdev, "%s (%d) failed: %s (%d)\n",
		   ionic_opcode_to_str(opcode), opcode, stat_str, err);
}

static int ionic_adminq_check_err(struct ionic_lif *lif,
				  struct ionic_admin_ctx *ctx,
				  const bool timeout,
				  const bool do_msg)
{
	int err = 0;

	if (ctx->comp.comp.status || timeout) {
		err = timeout ? -ETIMEDOUT :
				ionic_error_to_errno(ctx->comp.comp.status);

		if (do_msg)
			ionic_adminq_netdev_err_print(lif, ctx->cmd.cmd.opcode,
						      ctx->comp.comp.status, err);

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

	if (!ctx)
		return;

	comp = cq_info->cq_desc;

	memcpy(&ctx->comp, comp, sizeof(*comp));

	dev_dbg(q->dev, "comp admin queue command:\n");
	dynamic_hex_dump("comp ", DUMP_PREFIX_OFFSET, 16, 1,
			 &ctx->comp, sizeof(ctx->comp), true);

	complete_all(&ctx->work);
}

bool ionic_adminq_poke_doorbell(struct ionic_queue *q)
{
	struct ionic_lif *lif = q->lif;
	unsigned long now, then, dif;
	unsigned long irqflags;

	spin_lock_irqsave(&lif->adminq_lock, irqflags);

	if (q->tail_idx == q->head_idx) {
		spin_unlock_irqrestore(&lif->adminq_lock, irqflags);
		return false;
	}

	now = READ_ONCE(jiffies);
	then = q->dbell_jiffies;
	dif = now - then;

	if (dif > q->dbell_deadline) {
		ionic_dbell_ring(q->lif->kern_dbpage, q->hw_type,
				 q->dbval | q->head_idx);

		q->dbell_jiffies = now;
	}

	spin_unlock_irqrestore(&lif->adminq_lock, irqflags);

	return true;
}

int ionic_adminq_post(struct ionic_lif *lif, struct ionic_admin_ctx *ctx)
{
	struct ionic_desc_info *desc_info;
	unsigned long irqflags;
	struct ionic_queue *q;
	int err = 0;

	spin_lock_irqsave(&lif->adminq_lock, irqflags);
	if (!lif->adminqcq) {
		spin_unlock_irqrestore(&lif->adminq_lock, irqflags);
		return -EIO;
	}

	q = &lif->adminqcq->q;

	if (!ionic_q_has_space(q, 1)) {
		err = -ENOSPC;
		goto err_out;
	}

	err = ionic_heartbeat_check(lif->ionic);
	if (err)
		goto err_out;

	desc_info = &q->info[q->head_idx];
	memcpy(desc_info->desc, &ctx->cmd, sizeof(ctx->cmd));

	dev_dbg(&lif->netdev->dev, "post admin queue command:\n");
	dynamic_hex_dump("cmd ", DUMP_PREFIX_OFFSET, 16, 1,
			 &ctx->cmd, sizeof(ctx->cmd), true);

	ionic_q_post(q, true, ionic_adminq_cb, ctx);

err_out:
	spin_unlock_irqrestore(&lif->adminq_lock, irqflags);

	return err;
}

int ionic_adminq_wait(struct ionic_lif *lif, struct ionic_admin_ctx *ctx,
		      const int err, const bool do_msg)
{
	struct net_device *netdev = lif->netdev;
	unsigned long time_limit;
	unsigned long time_start;
	unsigned long time_done;
	unsigned long remaining;
	const char *name;

	name = ionic_opcode_to_str(ctx->cmd.cmd.opcode);

	if (err) {
		if (do_msg && !test_bit(IONIC_LIF_F_FW_RESET, lif->state))
			netdev_err(netdev, "Posting of %s (%d) failed: %d\n",
				   name, ctx->cmd.cmd.opcode, err);
		ctx->comp.comp.status = IONIC_RC_ERROR;
		return err;
	}

	time_start = jiffies;
	time_limit = time_start + HZ * (ulong)DEVCMD_TIMEOUT;
	do {
		remaining = wait_for_completion_timeout(&ctx->work,
							IONIC_ADMINQ_TIME_SLICE);

		/* check for done */
		if (remaining)
			break;

		/* force a check of FW status and break out if FW reset */
		ionic_heartbeat_check(lif->ionic);
		if ((test_bit(IONIC_LIF_F_FW_RESET, lif->state) &&
		     !lif->ionic->idev.fw_status_ready) ||
		    test_bit(IONIC_LIF_F_FW_STOPPING, lif->state)) {
			if (do_msg)
				netdev_warn(netdev, "%s (%d) interrupted, FW in reset\n",
					    name, ctx->cmd.cmd.opcode);
			ctx->comp.comp.status = IONIC_RC_ERROR;
			return -ENXIO;
		}

	} while (time_before(jiffies, time_limit));
	time_done = jiffies;

	dev_dbg(lif->ionic->dev, "%s: elapsed %d msecs\n",
		__func__, jiffies_to_msecs(time_done - time_start));

	return ionic_adminq_check_err(lif, ctx,
				      time_after_eq(time_done, time_limit),
				      do_msg);
}

static int __ionic_adminq_post_wait(struct ionic_lif *lif,
				    struct ionic_admin_ctx *ctx,
				    const bool do_msg)
{
	int err;

	if (!ionic_is_fw_running(&lif->ionic->idev))
		return 0;

	err = ionic_adminq_post(lif, ctx);

	return ionic_adminq_wait(lif, ctx, err, do_msg);
}

int ionic_adminq_post_wait(struct ionic_lif *lif, struct ionic_admin_ctx *ctx)
{
	return __ionic_adminq_post_wait(lif, ctx, true);
}

int ionic_adminq_post_wait_nomsg(struct ionic_lif *lif, struct ionic_admin_ctx *ctx)
{
	return __ionic_adminq_post_wait(lif, ctx, false);
}

static void ionic_dev_cmd_clean(struct ionic *ionic)
{
	struct ionic_dev *idev = &ionic->idev;

	iowrite32(0, &idev->dev_cmd_regs->doorbell);
	memset_io(&idev->dev_cmd_regs->cmd, 0, sizeof(idev->dev_cmd_regs->cmd));
}

void ionic_dev_cmd_dev_err_print(struct ionic *ionic, u8 opcode, u8 status,
				 int err)
{
	const char *stat_str;

	stat_str = (err == -ETIMEDOUT) ? "TIMEOUT" :
					 ionic_error_to_str(status);

	dev_err(ionic->dev, "DEV_CMD %s (%d) error, %s (%d) failed\n",
		ionic_opcode_to_str(opcode), opcode, stat_str, err);
}

static int __ionic_dev_cmd_wait(struct ionic *ionic, unsigned long max_seconds,
				const bool do_msg)
{
	struct ionic_dev *idev = &ionic->idev;
	unsigned long start_time;
	unsigned long max_wait;
	unsigned long duration;
	int done = 0;
	bool fw_up;
	int opcode;
	int err;

	/* Wait for dev cmd to complete, retrying if we get EAGAIN,
	 * but don't wait any longer than max_seconds.
	 */
	max_wait = jiffies + (max_seconds * HZ);
try_again:
	opcode = idev->opcode;
	start_time = jiffies;
	for (fw_up = ionic_is_fw_running(idev);
	     !done && fw_up && time_before(jiffies, max_wait);
	     fw_up = ionic_is_fw_running(idev)) {
		done = ionic_dev_cmd_done(idev);
		if (done)
			break;
		usleep_range(100, 200);
	}
	duration = jiffies - start_time;

	dev_dbg(ionic->dev, "DEVCMD %s (%d) done=%d took %ld secs (%ld jiffies)\n",
		ionic_opcode_to_str(opcode), opcode,
		done, duration / HZ, duration);

	if (!done && !fw_up) {
		ionic_dev_cmd_clean(ionic);
		dev_warn(ionic->dev, "DEVCMD %s (%d) interrupted - FW is down\n",
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
		if (err == IONIC_RC_EAGAIN &&
		    time_before(jiffies, (max_wait - HZ))) {
			dev_dbg(ionic->dev, "DEV_CMD %s (%d), %s (%d) retrying...\n",
				ionic_opcode_to_str(opcode), opcode,
				ionic_error_to_str(err), err);

			iowrite32(0, &idev->dev_cmd_regs->done);
			msleep(1000);
			iowrite32(1, &idev->dev_cmd_regs->doorbell);
			goto try_again;
		}

		if (!(opcode == IONIC_CMD_FW_CONTROL && err == IONIC_RC_EAGAIN))
			if (do_msg)
				ionic_dev_cmd_dev_err_print(ionic, opcode, err,
							    ionic_error_to_errno(err));

		return ionic_error_to_errno(err);
	}

	ionic_dev_cmd_clean(ionic);

	return 0;
}

int ionic_dev_cmd_wait(struct ionic *ionic, unsigned long max_seconds)
{
	return __ionic_dev_cmd_wait(ionic, max_seconds, true);
}

int ionic_dev_cmd_wait_nomsg(struct ionic *ionic, unsigned long max_seconds)
{
	return __ionic_dev_cmd_wait(ionic, max_seconds, false);
}

int ionic_setup(struct ionic *ionic)
{
	int err;

	err = ionic_dev_setup(ionic);
	if (err)
		return err;
	ionic_reset(ionic);

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
	strscpy(ident->drv.driver_ver_str, UTS_RELEASE,
		sizeof(ident->drv.driver_ver_str));

	mutex_lock(&ionic->dev_cmd_lock);

	sz = min(sizeof(ident->drv), sizeof(idev->dev_cmd_regs->data));
	memcpy_toio(&idev->dev_cmd_regs->data, &ident->drv, sz);

	ionic_dev_cmd_identify(idev, IONIC_DEV_IDENTITY_VERSION_2);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
	if (!err) {
		sz = min(sizeof(ident->dev), sizeof(idev->dev_cmd_regs->data));
		memcpy_fromio(&ident->dev, &idev->dev_cmd_regs->data, sz);
	}
	mutex_unlock(&ionic->dev_cmd_lock);

	if (err) {
		dev_err(ionic->dev, "Cannot identify ionic: %d\n", err);
		goto err_out;
	}

	if (isprint(idev->dev_info.fw_version[0]) &&
	    isascii(idev->dev_info.fw_version[0]))
		dev_info(ionic->dev, "FW: %.*s\n",
			 (int)(sizeof(idev->dev_info.fw_version) - 1),
			 idev->dev_info.fw_version);
	else
		dev_info(ionic->dev, "FW: (invalid string) 0x%02x 0x%02x 0x%02x 0x%02x ...\n",
			 (u8)idev->dev_info.fw_version[0],
			 (u8)idev->dev_info.fw_version[1],
			 (u8)idev->dev_info.fw_version[2],
			 (u8)idev->dev_info.fw_version[3]);

	err = ionic_lif_identify(ionic, IONIC_LIF_TYPE_CLASSIC,
				 &ionic->ident.lif);
	if (err) {
		dev_err(ionic->dev, "Cannot identify LIFs: %d\n", err);
		goto err_out;
	}

	return 0;

err_out:
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

	if (!ionic_is_fw_running(idev))
		return 0;

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

	if (!idev->port_info) {
		idev->port_info_sz = ALIGN(sizeof(*idev->port_info), PAGE_SIZE);
		idev->port_info = dma_alloc_coherent(ionic->dev,
						     idev->port_info_sz,
						     &idev->port_info_pa,
						     GFP_KERNEL);
		if (!idev->port_info)
			return -ENOMEM;
	}

	sz = min(sizeof(ident->port.config), sizeof(idev->dev_cmd_regs->data));

	mutex_lock(&ionic->dev_cmd_lock);

	memcpy_toio(&idev->dev_cmd_regs->data, &ident->port.config, sz);
	ionic_dev_cmd_port_init(idev);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);

	ionic_dev_cmd_port_state(&ionic->idev, IONIC_PORT_ADMIN_STATE_UP);
	ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);

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
	int err = 0;

	if (!idev->port_info)
		return 0;

	if (ionic_is_fw_running(idev)) {
		mutex_lock(&ionic->dev_cmd_lock);
		ionic_dev_cmd_port_reset(idev);
		err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);
		mutex_unlock(&ionic->dev_cmd_lock);
	}

	dma_free_coherent(ionic->dev, idev->port_info_sz,
			  idev->port_info, idev->port_info_pa);

	idev->port_info = NULL;
	idev->port_info_pa = 0;

	return err;
}

static int __init ionic_init_module(void)
{
	int ret;

	ionic_debugfs_create();
	ret = ionic_bus_register_driver();
	if (ret)
		ionic_debugfs_destroy();

	return ret;
}

static void __exit ionic_cleanup_module(void)
{
	ionic_bus_unregister_driver();
	ionic_debugfs_destroy();

	pr_info("%s removed\n", IONIC_DRV_NAME);
}

module_init(ionic_init_module);
module_exit(ionic_cleanup_module);
