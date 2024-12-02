// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CXL Flash Device Driver
 *
 * Written by: Manoj N. Kumar <manoj@linux.vnet.ibm.com>, IBM Corporation
 *             Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2015 IBM Corporation
 */

#include <linux/delay.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <asm/unaligned.h>

#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <uapi/scsi/cxlflash_ioctl.h>

#include "main.h"
#include "sislite.h"
#include "common.h"

MODULE_DESCRIPTION(CXLFLASH_ADAPTER_NAME);
MODULE_AUTHOR("Manoj N. Kumar <manoj@linux.vnet.ibm.com>");
MODULE_AUTHOR("Matthew R. Ochs <mrochs@linux.vnet.ibm.com>");
MODULE_LICENSE("GPL");

static struct class *cxlflash_class;
static u32 cxlflash_major;
static DECLARE_BITMAP(cxlflash_minor, CXLFLASH_MAX_ADAPTERS);

/**
 * process_cmd_err() - command error handler
 * @cmd:	AFU command that experienced the error.
 * @scp:	SCSI command associated with the AFU command in error.
 *
 * Translates error bits from AFU command to SCSI command results.
 */
static void process_cmd_err(struct afu_cmd *cmd, struct scsi_cmnd *scp)
{
	struct afu *afu = cmd->parent;
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	struct sisl_ioasa *ioasa;
	u32 resid;

	ioasa = &(cmd->sa);

	if (ioasa->rc.flags & SISL_RC_FLAGS_UNDERRUN) {
		resid = ioasa->resid;
		scsi_set_resid(scp, resid);
		dev_dbg(dev, "%s: cmd underrun cmd = %p scp = %p, resid = %d\n",
			__func__, cmd, scp, resid);
	}

	if (ioasa->rc.flags & SISL_RC_FLAGS_OVERRUN) {
		dev_dbg(dev, "%s: cmd underrun cmd = %p scp = %p\n",
			__func__, cmd, scp);
		scp->result = (DID_ERROR << 16);
	}

	dev_dbg(dev, "%s: cmd failed afu_rc=%02x scsi_rc=%02x fc_rc=%02x "
		"afu_extra=%02x scsi_extra=%02x fc_extra=%02x\n", __func__,
		ioasa->rc.afu_rc, ioasa->rc.scsi_rc, ioasa->rc.fc_rc,
		ioasa->afu_extra, ioasa->scsi_extra, ioasa->fc_extra);

	if (ioasa->rc.scsi_rc) {
		/* We have a SCSI status */
		if (ioasa->rc.flags & SISL_RC_FLAGS_SENSE_VALID) {
			memcpy(scp->sense_buffer, ioasa->sense_data,
			       SISL_SENSE_DATA_LEN);
			scp->result = ioasa->rc.scsi_rc;
		} else
			scp->result = ioasa->rc.scsi_rc | (DID_ERROR << 16);
	}

	/*
	 * We encountered an error. Set scp->result based on nature
	 * of error.
	 */
	if (ioasa->rc.fc_rc) {
		/* We have an FC status */
		switch (ioasa->rc.fc_rc) {
		case SISL_FC_RC_LINKDOWN:
			scp->result = (DID_REQUEUE << 16);
			break;
		case SISL_FC_RC_RESID:
			/* This indicates an FCP resid underrun */
			if (!(ioasa->rc.flags & SISL_RC_FLAGS_OVERRUN)) {
				/* If the SISL_RC_FLAGS_OVERRUN flag was set,
				 * then we will handle this error else where.
				 * If not then we must handle it here.
				 * This is probably an AFU bug.
				 */
				scp->result = (DID_ERROR << 16);
			}
			break;
		case SISL_FC_RC_RESIDERR:
			/* Resid mismatch between adapter and device */
		case SISL_FC_RC_TGTABORT:
		case SISL_FC_RC_ABORTOK:
		case SISL_FC_RC_ABORTFAIL:
		case SISL_FC_RC_NOLOGI:
		case SISL_FC_RC_ABORTPEND:
		case SISL_FC_RC_WRABORTPEND:
		case SISL_FC_RC_NOEXP:
		case SISL_FC_RC_INUSE:
			scp->result = (DID_ERROR << 16);
			break;
		}
	}

	if (ioasa->rc.afu_rc) {
		/* We have an AFU error */
		switch (ioasa->rc.afu_rc) {
		case SISL_AFU_RC_NO_CHANNELS:
			scp->result = (DID_NO_CONNECT << 16);
			break;
		case SISL_AFU_RC_DATA_DMA_ERR:
			switch (ioasa->afu_extra) {
			case SISL_AFU_DMA_ERR_PAGE_IN:
				/* Retry */
				scp->result = (DID_IMM_RETRY << 16);
				break;
			case SISL_AFU_DMA_ERR_INVALID_EA:
			default:
				scp->result = (DID_ERROR << 16);
			}
			break;
		case SISL_AFU_RC_OUT_OF_DATA_BUFS:
			/* Retry */
			scp->result = (DID_ERROR << 16);
			break;
		default:
			scp->result = (DID_ERROR << 16);
		}
	}
}

/**
 * cmd_complete() - command completion handler
 * @cmd:	AFU command that has completed.
 *
 * For SCSI commands this routine prepares and submits commands that have
 * either completed or timed out to the SCSI stack. For internal commands
 * (TMF or AFU), this routine simply notifies the originator that the
 * command has completed.
 */
static void cmd_complete(struct afu_cmd *cmd)
{
	struct scsi_cmnd *scp;
	ulong lock_flags;
	struct afu *afu = cmd->parent;
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq = get_hwq(afu, cmd->hwq_index);

	spin_lock_irqsave(&hwq->hsq_slock, lock_flags);
	list_del(&cmd->list);
	spin_unlock_irqrestore(&hwq->hsq_slock, lock_flags);

	if (cmd->scp) {
		scp = cmd->scp;
		if (unlikely(cmd->sa.ioasc))
			process_cmd_err(cmd, scp);
		else
			scp->result = (DID_OK << 16);

		dev_dbg_ratelimited(dev, "%s:scp=%p result=%08x ioasc=%08x\n",
				    __func__, scp, scp->result, cmd->sa.ioasc);
		scsi_done(scp);
	} else if (cmd->cmd_tmf) {
		spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
		cfg->tmf_active = false;
		wake_up_all_locked(&cfg->tmf_waitq);
		spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);
	} else
		complete(&cmd->cevent);
}

/**
 * flush_pending_cmds() - flush all pending commands on this hardware queue
 * @hwq:	Hardware queue to flush.
 *
 * The hardware send queue lock associated with this hardware queue must be
 * held when calling this routine.
 */
static void flush_pending_cmds(struct hwq *hwq)
{
	struct cxlflash_cfg *cfg = hwq->afu->parent;
	struct afu_cmd *cmd, *tmp;
	struct scsi_cmnd *scp;
	ulong lock_flags;

	list_for_each_entry_safe(cmd, tmp, &hwq->pending_cmds, list) {
		/* Bypass command when on a doneq, cmd_complete() will handle */
		if (!list_empty(&cmd->queue))
			continue;

		list_del(&cmd->list);

		if (cmd->scp) {
			scp = cmd->scp;
			scp->result = (DID_IMM_RETRY << 16);
			scsi_done(scp);
		} else {
			cmd->cmd_aborted = true;

			if (cmd->cmd_tmf) {
				spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
				cfg->tmf_active = false;
				wake_up_all_locked(&cfg->tmf_waitq);
				spin_unlock_irqrestore(&cfg->tmf_slock,
						       lock_flags);
			} else
				complete(&cmd->cevent);
		}
	}
}

/**
 * context_reset() - reset context via specified register
 * @hwq:	Hardware queue owning the context to be reset.
 * @reset_reg:	MMIO register to perform reset.
 *
 * When the reset is successful, the SISLite specification guarantees that
 * the AFU has aborted all currently pending I/O. Accordingly, these commands
 * must be flushed.
 *
 * Return: 0 on success, -errno on failure
 */
static int context_reset(struct hwq *hwq, __be64 __iomem *reset_reg)
{
	struct cxlflash_cfg *cfg = hwq->afu->parent;
	struct device *dev = &cfg->dev->dev;
	int rc = -ETIMEDOUT;
	int nretry = 0;
	u64 val = 0x1;
	ulong lock_flags;

	dev_dbg(dev, "%s: hwq=%p\n", __func__, hwq);

	spin_lock_irqsave(&hwq->hsq_slock, lock_flags);

	writeq_be(val, reset_reg);
	do {
		val = readq_be(reset_reg);
		if ((val & 0x1) == 0x0) {
			rc = 0;
			break;
		}

		/* Double delay each time */
		udelay(1 << nretry);
	} while (nretry++ < MC_ROOM_RETRY_CNT);

	if (!rc)
		flush_pending_cmds(hwq);

	spin_unlock_irqrestore(&hwq->hsq_slock, lock_flags);

	dev_dbg(dev, "%s: returning rc=%d, val=%016llx nretry=%d\n",
		__func__, rc, val, nretry);
	return rc;
}

/**
 * context_reset_ioarrin() - reset context via IOARRIN register
 * @hwq:	Hardware queue owning the context to be reset.
 *
 * Return: 0 on success, -errno on failure
 */
static int context_reset_ioarrin(struct hwq *hwq)
{
	return context_reset(hwq, &hwq->host_map->ioarrin);
}

/**
 * context_reset_sq() - reset context via SQ_CONTEXT_RESET register
 * @hwq:	Hardware queue owning the context to be reset.
 *
 * Return: 0 on success, -errno on failure
 */
static int context_reset_sq(struct hwq *hwq)
{
	return context_reset(hwq, &hwq->host_map->sq_ctx_reset);
}

/**
 * send_cmd_ioarrin() - sends an AFU command via IOARRIN register
 * @afu:	AFU associated with the host.
 * @cmd:	AFU command to send.
 *
 * Return:
 *	0 on success, SCSI_MLQUEUE_HOST_BUSY on failure
 */
static int send_cmd_ioarrin(struct afu *afu, struct afu_cmd *cmd)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq = get_hwq(afu, cmd->hwq_index);
	int rc = 0;
	s64 room;
	ulong lock_flags;

	/*
	 * To avoid the performance penalty of MMIO, spread the update of
	 * 'room' over multiple commands.
	 */
	spin_lock_irqsave(&hwq->hsq_slock, lock_flags);
	if (--hwq->room < 0) {
		room = readq_be(&hwq->host_map->cmd_room);
		if (room <= 0) {
			dev_dbg_ratelimited(dev, "%s: no cmd_room to send "
					    "0x%02X, room=0x%016llX\n",
					    __func__, cmd->rcb.cdb[0], room);
			hwq->room = 0;
			rc = SCSI_MLQUEUE_HOST_BUSY;
			goto out;
		}
		hwq->room = room - 1;
	}

	list_add(&cmd->list, &hwq->pending_cmds);
	writeq_be((u64)&cmd->rcb, &hwq->host_map->ioarrin);
out:
	spin_unlock_irqrestore(&hwq->hsq_slock, lock_flags);
	dev_dbg_ratelimited(dev, "%s: cmd=%p len=%u ea=%016llx rc=%d\n",
		__func__, cmd, cmd->rcb.data_len, cmd->rcb.data_ea, rc);
	return rc;
}

/**
 * send_cmd_sq() - sends an AFU command via SQ ring
 * @afu:	AFU associated with the host.
 * @cmd:	AFU command to send.
 *
 * Return:
 *	0 on success, SCSI_MLQUEUE_HOST_BUSY on failure
 */
static int send_cmd_sq(struct afu *afu, struct afu_cmd *cmd)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq = get_hwq(afu, cmd->hwq_index);
	int rc = 0;
	int newval;
	ulong lock_flags;

	newval = atomic_dec_if_positive(&hwq->hsq_credits);
	if (newval <= 0) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	cmd->rcb.ioasa = &cmd->sa;

	spin_lock_irqsave(&hwq->hsq_slock, lock_flags);

	*hwq->hsq_curr = cmd->rcb;
	if (hwq->hsq_curr < hwq->hsq_end)
		hwq->hsq_curr++;
	else
		hwq->hsq_curr = hwq->hsq_start;

	list_add(&cmd->list, &hwq->pending_cmds);
	writeq_be((u64)hwq->hsq_curr, &hwq->host_map->sq_tail);

	spin_unlock_irqrestore(&hwq->hsq_slock, lock_flags);
out:
	dev_dbg(dev, "%s: cmd=%p len=%u ea=%016llx ioasa=%p rc=%d curr=%p "
	       "head=%016llx tail=%016llx\n", __func__, cmd, cmd->rcb.data_len,
	       cmd->rcb.data_ea, cmd->rcb.ioasa, rc, hwq->hsq_curr,
	       readq_be(&hwq->host_map->sq_head),
	       readq_be(&hwq->host_map->sq_tail));
	return rc;
}

/**
 * wait_resp() - polls for a response or timeout to a sent AFU command
 * @afu:	AFU associated with the host.
 * @cmd:	AFU command that was sent.
 *
 * Return: 0 on success, -errno on failure
 */
static int wait_resp(struct afu *afu, struct afu_cmd *cmd)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	int rc = 0;
	ulong timeout = msecs_to_jiffies(cmd->rcb.timeout * 2 * 1000);

	timeout = wait_for_completion_timeout(&cmd->cevent, timeout);
	if (!timeout)
		rc = -ETIMEDOUT;

	if (cmd->cmd_aborted)
		rc = -EAGAIN;

	if (unlikely(cmd->sa.ioasc != 0)) {
		dev_err(dev, "%s: cmd %02x failed, ioasc=%08x\n",
			__func__, cmd->rcb.cdb[0], cmd->sa.ioasc);
		rc = -EIO;
	}

	return rc;
}

/**
 * cmd_to_target_hwq() - selects a target hardware queue for a SCSI command
 * @host:	SCSI host associated with device.
 * @scp:	SCSI command to send.
 * @afu:	SCSI command to send.
 *
 * Hashes a command based upon the hardware queue mode.
 *
 * Return: Trusted index of target hardware queue
 */
static u32 cmd_to_target_hwq(struct Scsi_Host *host, struct scsi_cmnd *scp,
			     struct afu *afu)
{
	u32 tag;
	u32 hwq = 0;

	if (afu->num_hwqs == 1)
		return 0;

	switch (afu->hwq_mode) {
	case HWQ_MODE_RR:
		hwq = afu->hwq_rr_count++ % afu->num_hwqs;
		break;
	case HWQ_MODE_TAG:
		tag = blk_mq_unique_tag(scsi_cmd_to_rq(scp));
		hwq = blk_mq_unique_tag_to_hwq(tag);
		break;
	case HWQ_MODE_CPU:
		hwq = smp_processor_id() % afu->num_hwqs;
		break;
	default:
		WARN_ON_ONCE(1);
	}

	return hwq;
}

/**
 * send_tmf() - sends a Task Management Function (TMF)
 * @cfg:	Internal structure associated with the host.
 * @sdev:	SCSI device destined for TMF.
 * @tmfcmd:	TMF command to send.
 *
 * Return:
 *	0 on success, SCSI_MLQUEUE_HOST_BUSY or -errno on failure
 */
static int send_tmf(struct cxlflash_cfg *cfg, struct scsi_device *sdev,
		    u64 tmfcmd)
{
	struct afu *afu = cfg->afu;
	struct afu_cmd *cmd = NULL;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq = get_hwq(afu, PRIMARY_HWQ);
	bool needs_deletion = false;
	char *buf = NULL;
	ulong lock_flags;
	int rc = 0;
	ulong to;

	buf = kzalloc(sizeof(*cmd) + __alignof__(*cmd) - 1, GFP_KERNEL);
	if (unlikely(!buf)) {
		dev_err(dev, "%s: no memory for command\n", __func__);
		rc = -ENOMEM;
		goto out;
	}

	cmd = (struct afu_cmd *)PTR_ALIGN(buf, __alignof__(*cmd));
	INIT_LIST_HEAD(&cmd->queue);

	/* When Task Management Function is active do not send another */
	spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
	if (cfg->tmf_active)
		wait_event_interruptible_lock_irq(cfg->tmf_waitq,
						  !cfg->tmf_active,
						  cfg->tmf_slock);
	cfg->tmf_active = true;
	spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);

	cmd->parent = afu;
	cmd->cmd_tmf = true;
	cmd->hwq_index = hwq->index;

	cmd->rcb.ctx_id = hwq->ctx_hndl;
	cmd->rcb.msi = SISL_MSI_RRQ_UPDATED;
	cmd->rcb.port_sel = CHAN2PORTMASK(sdev->channel);
	cmd->rcb.lun_id = lun_to_lunid(sdev->lun);
	cmd->rcb.req_flags = (SISL_REQ_FLAGS_PORT_LUN_ID |
			      SISL_REQ_FLAGS_SUP_UNDERRUN |
			      SISL_REQ_FLAGS_TMF_CMD);
	memcpy(cmd->rcb.cdb, &tmfcmd, sizeof(tmfcmd));

	rc = afu->send_cmd(afu, cmd);
	if (unlikely(rc)) {
		spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
		cfg->tmf_active = false;
		spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);
		goto out;
	}

	spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
	to = msecs_to_jiffies(5000);
	to = wait_event_interruptible_lock_irq_timeout(cfg->tmf_waitq,
						       !cfg->tmf_active,
						       cfg->tmf_slock,
						       to);
	if (!to) {
		dev_err(dev, "%s: TMF timed out\n", __func__);
		rc = -ETIMEDOUT;
		needs_deletion = true;
	} else if (cmd->cmd_aborted) {
		dev_err(dev, "%s: TMF aborted\n", __func__);
		rc = -EAGAIN;
	} else if (cmd->sa.ioasc) {
		dev_err(dev, "%s: TMF failed ioasc=%08x\n",
			__func__, cmd->sa.ioasc);
		rc = -EIO;
	}
	cfg->tmf_active = false;
	spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);

	if (needs_deletion) {
		spin_lock_irqsave(&hwq->hsq_slock, lock_flags);
		list_del(&cmd->list);
		spin_unlock_irqrestore(&hwq->hsq_slock, lock_flags);
	}
out:
	kfree(buf);
	return rc;
}

/**
 * cxlflash_driver_info() - information handler for this host driver
 * @host:	SCSI host associated with device.
 *
 * Return: A string describing the device.
 */
static const char *cxlflash_driver_info(struct Scsi_Host *host)
{
	return CXLFLASH_ADAPTER_NAME;
}

/**
 * cxlflash_queuecommand() - sends a mid-layer request
 * @host:	SCSI host associated with device.
 * @scp:	SCSI command to send.
 *
 * Return: 0 on success, SCSI_MLQUEUE_HOST_BUSY on failure
 */
static int cxlflash_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *scp)
{
	struct cxlflash_cfg *cfg = shost_priv(host);
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct afu_cmd *cmd = sc_to_afuci(scp);
	struct scatterlist *sg = scsi_sglist(scp);
	int hwq_index = cmd_to_target_hwq(host, scp, afu);
	struct hwq *hwq = get_hwq(afu, hwq_index);
	u16 req_flags = SISL_REQ_FLAGS_SUP_UNDERRUN;
	ulong lock_flags;
	int rc = 0;

	dev_dbg_ratelimited(dev, "%s: (scp=%p) %d/%d/%d/%llu "
			    "cdb=(%08x-%08x-%08x-%08x)\n",
			    __func__, scp, host->host_no, scp->device->channel,
			    scp->device->id, scp->device->lun,
			    get_unaligned_be32(&((u32 *)scp->cmnd)[0]),
			    get_unaligned_be32(&((u32 *)scp->cmnd)[1]),
			    get_unaligned_be32(&((u32 *)scp->cmnd)[2]),
			    get_unaligned_be32(&((u32 *)scp->cmnd)[3]));

	/*
	 * If a Task Management Function is active, wait for it to complete
	 * before continuing with regular commands.
	 */
	spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
	if (cfg->tmf_active) {
		spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}
	spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);

	switch (cfg->state) {
	case STATE_PROBING:
	case STATE_PROBED:
	case STATE_RESET:
		dev_dbg_ratelimited(dev, "%s: device is in reset\n", __func__);
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	case STATE_FAILTERM:
		dev_dbg_ratelimited(dev, "%s: device has failed\n", __func__);
		scp->result = (DID_NO_CONNECT << 16);
		scsi_done(scp);
		rc = 0;
		goto out;
	default:
		atomic_inc(&afu->cmds_active);
		break;
	}

	if (likely(sg)) {
		cmd->rcb.data_len = sg->length;
		cmd->rcb.data_ea = (uintptr_t)sg_virt(sg);
	}

	cmd->scp = scp;
	cmd->parent = afu;
	cmd->hwq_index = hwq_index;

	cmd->sa.ioasc = 0;
	cmd->rcb.ctx_id = hwq->ctx_hndl;
	cmd->rcb.msi = SISL_MSI_RRQ_UPDATED;
	cmd->rcb.port_sel = CHAN2PORTMASK(scp->device->channel);
	cmd->rcb.lun_id = lun_to_lunid(scp->device->lun);

	if (scp->sc_data_direction == DMA_TO_DEVICE)
		req_flags |= SISL_REQ_FLAGS_HOST_WRITE;

	cmd->rcb.req_flags = req_flags;
	memcpy(cmd->rcb.cdb, scp->cmnd, sizeof(cmd->rcb.cdb));

	rc = afu->send_cmd(afu, cmd);
	atomic_dec(&afu->cmds_active);
out:
	return rc;
}

/**
 * cxlflash_wait_for_pci_err_recovery() - wait for error recovery during probe
 * @cfg:	Internal structure associated with the host.
 */
static void cxlflash_wait_for_pci_err_recovery(struct cxlflash_cfg *cfg)
{
	struct pci_dev *pdev = cfg->dev;

	if (pci_channel_offline(pdev))
		wait_event_timeout(cfg->reset_waitq,
				   !pci_channel_offline(pdev),
				   CXLFLASH_PCI_ERROR_RECOVERY_TIMEOUT);
}

/**
 * free_mem() - free memory associated with the AFU
 * @cfg:	Internal structure associated with the host.
 */
static void free_mem(struct cxlflash_cfg *cfg)
{
	struct afu *afu = cfg->afu;

	if (cfg->afu) {
		free_pages((ulong)afu, get_order(sizeof(struct afu)));
		cfg->afu = NULL;
	}
}

/**
 * cxlflash_reset_sync() - synchronizing point for asynchronous resets
 * @cfg:	Internal structure associated with the host.
 */
static void cxlflash_reset_sync(struct cxlflash_cfg *cfg)
{
	if (cfg->async_reset_cookie == 0)
		return;

	/* Wait until all async calls prior to this cookie have completed */
	async_synchronize_cookie(cfg->async_reset_cookie + 1);
	cfg->async_reset_cookie = 0;
}

/**
 * stop_afu() - stops the AFU command timers and unmaps the MMIO space
 * @cfg:	Internal structure associated with the host.
 *
 * Safe to call with AFU in a partially allocated/initialized state.
 *
 * Cancels scheduled worker threads, waits for any active internal AFU
 * commands to timeout, disables IRQ polling and then unmaps the MMIO space.
 */
static void stop_afu(struct cxlflash_cfg *cfg)
{
	struct afu *afu = cfg->afu;
	struct hwq *hwq;
	int i;

	cancel_work_sync(&cfg->work_q);
	if (!current_is_async())
		cxlflash_reset_sync(cfg);

	if (likely(afu)) {
		while (atomic_read(&afu->cmds_active))
			ssleep(1);

		if (afu_is_irqpoll_enabled(afu)) {
			for (i = 0; i < afu->num_hwqs; i++) {
				hwq = get_hwq(afu, i);

				irq_poll_disable(&hwq->irqpoll);
			}
		}

		if (likely(afu->afu_map)) {
			cfg->ops->psa_unmap(afu->afu_map);
			afu->afu_map = NULL;
		}
	}
}

/**
 * term_intr() - disables all AFU interrupts
 * @cfg:	Internal structure associated with the host.
 * @level:	Depth of allocation, where to begin waterfall tear down.
 * @index:	Index of the hardware queue.
 *
 * Safe to call with AFU/MC in partially allocated/initialized state.
 */
static void term_intr(struct cxlflash_cfg *cfg, enum undo_level level,
		      u32 index)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq;

	if (!afu) {
		dev_err(dev, "%s: returning with NULL afu\n", __func__);
		return;
	}

	hwq = get_hwq(afu, index);

	if (!hwq->ctx_cookie) {
		dev_err(dev, "%s: returning with NULL MC\n", __func__);
		return;
	}

	switch (level) {
	case UNMAP_THREE:
		/* SISL_MSI_ASYNC_ERROR is setup only for the primary HWQ */
		if (index == PRIMARY_HWQ)
			cfg->ops->unmap_afu_irq(hwq->ctx_cookie, 3, hwq);
		fallthrough;
	case UNMAP_TWO:
		cfg->ops->unmap_afu_irq(hwq->ctx_cookie, 2, hwq);
		fallthrough;
	case UNMAP_ONE:
		cfg->ops->unmap_afu_irq(hwq->ctx_cookie, 1, hwq);
		fallthrough;
	case FREE_IRQ:
		cfg->ops->free_afu_irqs(hwq->ctx_cookie);
		fallthrough;
	case UNDO_NOOP:
		/* No action required */
		break;
	}
}

/**
 * term_mc() - terminates the master context
 * @cfg:	Internal structure associated with the host.
 * @index:	Index of the hardware queue.
 *
 * Safe to call with AFU/MC in partially allocated/initialized state.
 */
static void term_mc(struct cxlflash_cfg *cfg, u32 index)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq;
	ulong lock_flags;

	if (!afu) {
		dev_err(dev, "%s: returning with NULL afu\n", __func__);
		return;
	}

	hwq = get_hwq(afu, index);

	if (!hwq->ctx_cookie) {
		dev_err(dev, "%s: returning with NULL MC\n", __func__);
		return;
	}

	WARN_ON(cfg->ops->stop_context(hwq->ctx_cookie));
	if (index != PRIMARY_HWQ)
		WARN_ON(cfg->ops->release_context(hwq->ctx_cookie));
	hwq->ctx_cookie = NULL;

	spin_lock_irqsave(&hwq->hrrq_slock, lock_flags);
	hwq->hrrq_online = false;
	spin_unlock_irqrestore(&hwq->hrrq_slock, lock_flags);

	spin_lock_irqsave(&hwq->hsq_slock, lock_flags);
	flush_pending_cmds(hwq);
	spin_unlock_irqrestore(&hwq->hsq_slock, lock_flags);
}

/**
 * term_afu() - terminates the AFU
 * @cfg:	Internal structure associated with the host.
 *
 * Safe to call with AFU/MC in partially allocated/initialized state.
 */
static void term_afu(struct cxlflash_cfg *cfg)
{
	struct device *dev = &cfg->dev->dev;
	int k;

	/*
	 * Tear down is carefully orchestrated to ensure
	 * no interrupts can come in when the problem state
	 * area is unmapped.
	 *
	 * 1) Disable all AFU interrupts for each master
	 * 2) Unmap the problem state area
	 * 3) Stop each master context
	 */
	for (k = cfg->afu->num_hwqs - 1; k >= 0; k--)
		term_intr(cfg, UNMAP_THREE, k);

	stop_afu(cfg);

	for (k = cfg->afu->num_hwqs - 1; k >= 0; k--)
		term_mc(cfg, k);

	dev_dbg(dev, "%s: returning\n", __func__);
}

/**
 * notify_shutdown() - notifies device of pending shutdown
 * @cfg:	Internal structure associated with the host.
 * @wait:	Whether to wait for shutdown processing to complete.
 *
 * This function will notify the AFU that the adapter is being shutdown
 * and will wait for shutdown processing to complete if wait is true.
 * This notification should flush pending I/Os to the device and halt
 * further I/Os until the next AFU reset is issued and device restarted.
 */
static void notify_shutdown(struct cxlflash_cfg *cfg, bool wait)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct dev_dependent_vals *ddv;
	__be64 __iomem *fc_port_regs;
	u64 reg, status;
	int i, retry_cnt = 0;

	ddv = (struct dev_dependent_vals *)cfg->dev_id->driver_data;
	if (!(ddv->flags & CXLFLASH_NOTIFY_SHUTDOWN))
		return;

	if (!afu || !afu->afu_map) {
		dev_dbg(dev, "%s: Problem state area not mapped\n", __func__);
		return;
	}

	/* Notify AFU */
	for (i = 0; i < cfg->num_fc_ports; i++) {
		fc_port_regs = get_fc_port_regs(cfg, i);

		reg = readq_be(&fc_port_regs[FC_CONFIG2 / 8]);
		reg |= SISL_FC_SHUTDOWN_NORMAL;
		writeq_be(reg, &fc_port_regs[FC_CONFIG2 / 8]);
	}

	if (!wait)
		return;

	/* Wait up to 1.5 seconds for shutdown processing to complete */
	for (i = 0; i < cfg->num_fc_ports; i++) {
		fc_port_regs = get_fc_port_regs(cfg, i);
		retry_cnt = 0;

		while (true) {
			status = readq_be(&fc_port_regs[FC_STATUS / 8]);
			if (status & SISL_STATUS_SHUTDOWN_COMPLETE)
				break;
			if (++retry_cnt >= MC_RETRY_CNT) {
				dev_dbg(dev, "%s: port %d shutdown processing "
					"not yet completed\n", __func__, i);
				break;
			}
			msleep(100 * retry_cnt);
		}
	}
}

/**
 * cxlflash_get_minor() - gets the first available minor number
 *
 * Return: Unique minor number that can be used to create the character device.
 */
static int cxlflash_get_minor(void)
{
	int minor;
	long bit;

	bit = find_first_zero_bit(cxlflash_minor, CXLFLASH_MAX_ADAPTERS);
	if (bit >= CXLFLASH_MAX_ADAPTERS)
		return -1;

	minor = bit & MINORMASK;
	set_bit(minor, cxlflash_minor);
	return minor;
}

/**
 * cxlflash_put_minor() - releases the minor number
 * @minor:	Minor number that is no longer needed.
 */
static void cxlflash_put_minor(int minor)
{
	clear_bit(minor, cxlflash_minor);
}

/**
 * cxlflash_release_chrdev() - release the character device for the host
 * @cfg:	Internal structure associated with the host.
 */
static void cxlflash_release_chrdev(struct cxlflash_cfg *cfg)
{
	device_unregister(cfg->chardev);
	cfg->chardev = NULL;
	cdev_del(&cfg->cdev);
	cxlflash_put_minor(MINOR(cfg->cdev.dev));
}

/**
 * cxlflash_remove() - PCI entry point to tear down host
 * @pdev:	PCI device associated with the host.
 *
 * Safe to use as a cleanup in partially allocated/initialized state. Note that
 * the reset_waitq is flushed as part of the stop/termination of user contexts.
 */
static void cxlflash_remove(struct pci_dev *pdev)
{
	struct cxlflash_cfg *cfg = pci_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	ulong lock_flags;

	if (!pci_is_enabled(pdev)) {
		dev_dbg(dev, "%s: Device is disabled\n", __func__);
		return;
	}

	/* Yield to running recovery threads before continuing with remove */
	wait_event(cfg->reset_waitq, cfg->state != STATE_RESET &&
				     cfg->state != STATE_PROBING);
	spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
	if (cfg->tmf_active)
		wait_event_interruptible_lock_irq(cfg->tmf_waitq,
						  !cfg->tmf_active,
						  cfg->tmf_slock);
	spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);

	/* Notify AFU and wait for shutdown processing to complete */
	notify_shutdown(cfg, true);

	cfg->state = STATE_FAILTERM;
	cxlflash_stop_term_user_contexts(cfg);

	switch (cfg->init_state) {
	case INIT_STATE_CDEV:
		cxlflash_release_chrdev(cfg);
		fallthrough;
	case INIT_STATE_SCSI:
		cxlflash_term_local_luns(cfg);
		scsi_remove_host(cfg->host);
		fallthrough;
	case INIT_STATE_AFU:
		term_afu(cfg);
		fallthrough;
	case INIT_STATE_PCI:
		cfg->ops->destroy_afu(cfg->afu_cookie);
		pci_disable_device(pdev);
		fallthrough;
	case INIT_STATE_NONE:
		free_mem(cfg);
		scsi_host_put(cfg->host);
		break;
	}

	dev_dbg(dev, "%s: returning\n", __func__);
}

/**
 * alloc_mem() - allocates the AFU and its command pool
 * @cfg:	Internal structure associated with the host.
 *
 * A partially allocated state remains on failure.
 *
 * Return:
 *	0 on success
 *	-ENOMEM on failure to allocate memory
 */
static int alloc_mem(struct cxlflash_cfg *cfg)
{
	int rc = 0;
	struct device *dev = &cfg->dev->dev;

	/* AFU is ~28k, i.e. only one 64k page or up to seven 4k pages */
	cfg->afu = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					    get_order(sizeof(struct afu)));
	if (unlikely(!cfg->afu)) {
		dev_err(dev, "%s: cannot get %d free pages\n",
			__func__, get_order(sizeof(struct afu)));
		rc = -ENOMEM;
		goto out;
	}
	cfg->afu->parent = cfg;
	cfg->afu->desired_hwqs = CXLFLASH_DEF_HWQS;
	cfg->afu->afu_map = NULL;
out:
	return rc;
}

/**
 * init_pci() - initializes the host as a PCI device
 * @cfg:	Internal structure associated with the host.
 *
 * Return: 0 on success, -errno on failure
 */
static int init_pci(struct cxlflash_cfg *cfg)
{
	struct pci_dev *pdev = cfg->dev;
	struct device *dev = &cfg->dev->dev;
	int rc = 0;

	rc = pci_enable_device(pdev);
	if (rc || pci_channel_offline(pdev)) {
		if (pci_channel_offline(pdev)) {
			cxlflash_wait_for_pci_err_recovery(cfg);
			rc = pci_enable_device(pdev);
		}

		if (rc) {
			dev_err(dev, "%s: Cannot enable adapter\n", __func__);
			cxlflash_wait_for_pci_err_recovery(cfg);
			goto out;
		}
	}

out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * init_scsi() - adds the host to the SCSI stack and kicks off host scan
 * @cfg:	Internal structure associated with the host.
 *
 * Return: 0 on success, -errno on failure
 */
static int init_scsi(struct cxlflash_cfg *cfg)
{
	struct pci_dev *pdev = cfg->dev;
	struct device *dev = &cfg->dev->dev;
	int rc = 0;

	rc = scsi_add_host(cfg->host, &pdev->dev);
	if (rc) {
		dev_err(dev, "%s: scsi_add_host failed rc=%d\n", __func__, rc);
		goto out;
	}

	scsi_scan_host(cfg->host);

out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * set_port_online() - transitions the specified host FC port to online state
 * @fc_regs:	Top of MMIO region defined for specified port.
 *
 * The provided MMIO region must be mapped prior to call. Online state means
 * that the FC link layer has synced, completed the handshaking process, and
 * is ready for login to start.
 */
static void set_port_online(__be64 __iomem *fc_regs)
{
	u64 cmdcfg;

	cmdcfg = readq_be(&fc_regs[FC_MTIP_CMDCONFIG / 8]);
	cmdcfg &= (~FC_MTIP_CMDCONFIG_OFFLINE);	/* clear OFF_LINE */
	cmdcfg |= (FC_MTIP_CMDCONFIG_ONLINE);	/* set ON_LINE */
	writeq_be(cmdcfg, &fc_regs[FC_MTIP_CMDCONFIG / 8]);
}

/**
 * set_port_offline() - transitions the specified host FC port to offline state
 * @fc_regs:	Top of MMIO region defined for specified port.
 *
 * The provided MMIO region must be mapped prior to call.
 */
static void set_port_offline(__be64 __iomem *fc_regs)
{
	u64 cmdcfg;

	cmdcfg = readq_be(&fc_regs[FC_MTIP_CMDCONFIG / 8]);
	cmdcfg &= (~FC_MTIP_CMDCONFIG_ONLINE);	/* clear ON_LINE */
	cmdcfg |= (FC_MTIP_CMDCONFIG_OFFLINE);	/* set OFF_LINE */
	writeq_be(cmdcfg, &fc_regs[FC_MTIP_CMDCONFIG / 8]);
}

/**
 * wait_port_online() - waits for the specified host FC port come online
 * @fc_regs:	Top of MMIO region defined for specified port.
 * @delay_us:	Number of microseconds to delay between reading port status.
 * @nretry:	Number of cycles to retry reading port status.
 *
 * The provided MMIO region must be mapped prior to call. This will timeout
 * when the cable is not plugged in.
 *
 * Return:
 *	TRUE (1) when the specified port is online
 *	FALSE (0) when the specified port fails to come online after timeout
 */
static bool wait_port_online(__be64 __iomem *fc_regs, u32 delay_us, u32 nretry)
{
	u64 status;

	WARN_ON(delay_us < 1000);

	do {
		msleep(delay_us / 1000);
		status = readq_be(&fc_regs[FC_MTIP_STATUS / 8]);
		if (status == U64_MAX)
			nretry /= 2;
	} while ((status & FC_MTIP_STATUS_MASK) != FC_MTIP_STATUS_ONLINE &&
		 nretry--);

	return ((status & FC_MTIP_STATUS_MASK) == FC_MTIP_STATUS_ONLINE);
}

/**
 * wait_port_offline() - waits for the specified host FC port go offline
 * @fc_regs:	Top of MMIO region defined for specified port.
 * @delay_us:	Number of microseconds to delay between reading port status.
 * @nretry:	Number of cycles to retry reading port status.
 *
 * The provided MMIO region must be mapped prior to call.
 *
 * Return:
 *	TRUE (1) when the specified port is offline
 *	FALSE (0) when the specified port fails to go offline after timeout
 */
static bool wait_port_offline(__be64 __iomem *fc_regs, u32 delay_us, u32 nretry)
{
	u64 status;

	WARN_ON(delay_us < 1000);

	do {
		msleep(delay_us / 1000);
		status = readq_be(&fc_regs[FC_MTIP_STATUS / 8]);
		if (status == U64_MAX)
			nretry /= 2;
	} while ((status & FC_MTIP_STATUS_MASK) != FC_MTIP_STATUS_OFFLINE &&
		 nretry--);

	return ((status & FC_MTIP_STATUS_MASK) == FC_MTIP_STATUS_OFFLINE);
}

/**
 * afu_set_wwpn() - configures the WWPN for the specified host FC port
 * @afu:	AFU associated with the host that owns the specified FC port.
 * @port:	Port number being configured.
 * @fc_regs:	Top of MMIO region defined for specified port.
 * @wwpn:	The world-wide-port-number previously discovered for port.
 *
 * The provided MMIO region must be mapped prior to call. As part of the
 * sequence to configure the WWPN, the port is toggled offline and then back
 * online. This toggling action can cause this routine to delay up to a few
 * seconds. When configured to use the internal LUN feature of the AFU, a
 * failure to come online is overridden.
 */
static void afu_set_wwpn(struct afu *afu, int port, __be64 __iomem *fc_regs,
			 u64 wwpn)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;

	set_port_offline(fc_regs);
	if (!wait_port_offline(fc_regs, FC_PORT_STATUS_RETRY_INTERVAL_US,
			       FC_PORT_STATUS_RETRY_CNT)) {
		dev_dbg(dev, "%s: wait on port %d to go offline timed out\n",
			__func__, port);
	}

	writeq_be(wwpn, &fc_regs[FC_PNAME / 8]);

	set_port_online(fc_regs);
	if (!wait_port_online(fc_regs, FC_PORT_STATUS_RETRY_INTERVAL_US,
			      FC_PORT_STATUS_RETRY_CNT)) {
		dev_dbg(dev, "%s: wait on port %d to go online timed out\n",
			__func__, port);
	}
}

/**
 * afu_link_reset() - resets the specified host FC port
 * @afu:	AFU associated with the host that owns the specified FC port.
 * @port:	Port number being configured.
 * @fc_regs:	Top of MMIO region defined for specified port.
 *
 * The provided MMIO region must be mapped prior to call. The sequence to
 * reset the port involves toggling it offline and then back online. This
 * action can cause this routine to delay up to a few seconds. An effort
 * is made to maintain link with the device by switching to host to use
 * the alternate port exclusively while the reset takes place.
 * failure to come online is overridden.
 */
static void afu_link_reset(struct afu *afu, int port, __be64 __iomem *fc_regs)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	u64 port_sel;

	/* first switch the AFU to the other links, if any */
	port_sel = readq_be(&afu->afu_map->global.regs.afu_port_sel);
	port_sel &= ~(1ULL << port);
	writeq_be(port_sel, &afu->afu_map->global.regs.afu_port_sel);
	cxlflash_afu_sync(afu, 0, 0, AFU_GSYNC);

	set_port_offline(fc_regs);
	if (!wait_port_offline(fc_regs, FC_PORT_STATUS_RETRY_INTERVAL_US,
			       FC_PORT_STATUS_RETRY_CNT))
		dev_err(dev, "%s: wait on port %d to go offline timed out\n",
			__func__, port);

	set_port_online(fc_regs);
	if (!wait_port_online(fc_regs, FC_PORT_STATUS_RETRY_INTERVAL_US,
			      FC_PORT_STATUS_RETRY_CNT))
		dev_err(dev, "%s: wait on port %d to go online timed out\n",
			__func__, port);

	/* switch back to include this port */
	port_sel |= (1ULL << port);
	writeq_be(port_sel, &afu->afu_map->global.regs.afu_port_sel);
	cxlflash_afu_sync(afu, 0, 0, AFU_GSYNC);

	dev_dbg(dev, "%s: returning port_sel=%016llx\n", __func__, port_sel);
}

/**
 * afu_err_intr_init() - clears and initializes the AFU for error interrupts
 * @afu:	AFU associated with the host.
 */
static void afu_err_intr_init(struct afu *afu)
{
	struct cxlflash_cfg *cfg = afu->parent;
	__be64 __iomem *fc_port_regs;
	int i;
	struct hwq *hwq = get_hwq(afu, PRIMARY_HWQ);
	u64 reg;

	/* global async interrupts: AFU clears afu_ctrl on context exit
	 * if async interrupts were sent to that context. This prevents
	 * the AFU form sending further async interrupts when
	 * there is
	 * nobody to receive them.
	 */

	/* mask all */
	writeq_be(-1ULL, &afu->afu_map->global.regs.aintr_mask);
	/* set LISN# to send and point to primary master context */
	reg = ((u64) (((hwq->ctx_hndl << 8) | SISL_MSI_ASYNC_ERROR)) << 40);

	if (afu->internal_lun)
		reg |= 1;	/* Bit 63 indicates local lun */
	writeq_be(reg, &afu->afu_map->global.regs.afu_ctrl);
	/* clear all */
	writeq_be(-1ULL, &afu->afu_map->global.regs.aintr_clear);
	/* unmask bits that are of interest */
	/* note: afu can send an interrupt after this step */
	writeq_be(SISL_ASTATUS_MASK, &afu->afu_map->global.regs.aintr_mask);
	/* clear again in case a bit came on after previous clear but before */
	/* unmask */
	writeq_be(-1ULL, &afu->afu_map->global.regs.aintr_clear);

	/* Clear/Set internal lun bits */
	fc_port_regs = get_fc_port_regs(cfg, 0);
	reg = readq_be(&fc_port_regs[FC_CONFIG2 / 8]);
	reg &= SISL_FC_INTERNAL_MASK;
	if (afu->internal_lun)
		reg |= ((u64)(afu->internal_lun - 1) << SISL_FC_INTERNAL_SHIFT);
	writeq_be(reg, &fc_port_regs[FC_CONFIG2 / 8]);

	/* now clear FC errors */
	for (i = 0; i < cfg->num_fc_ports; i++) {
		fc_port_regs = get_fc_port_regs(cfg, i);

		writeq_be(0xFFFFFFFFU, &fc_port_regs[FC_ERROR / 8]);
		writeq_be(0, &fc_port_regs[FC_ERRCAP / 8]);
	}

	/* sync interrupts for master's IOARRIN write */
	/* note that unlike asyncs, there can be no pending sync interrupts */
	/* at this time (this is a fresh context and master has not written */
	/* IOARRIN yet), so there is nothing to clear. */

	/* set LISN#, it is always sent to the context that wrote IOARRIN */
	for (i = 0; i < afu->num_hwqs; i++) {
		hwq = get_hwq(afu, i);

		reg = readq_be(&hwq->host_map->ctx_ctrl);
		WARN_ON((reg & SISL_CTX_CTRL_LISN_MASK) != 0);
		reg |= SISL_MSI_SYNC_ERROR;
		writeq_be(reg, &hwq->host_map->ctx_ctrl);
		writeq_be(SISL_ISTATUS_MASK, &hwq->host_map->intr_mask);
	}
}

/**
 * cxlflash_sync_err_irq() - interrupt handler for synchronous errors
 * @irq:	Interrupt number.
 * @data:	Private data provided at interrupt registration, the AFU.
 *
 * Return: Always return IRQ_HANDLED.
 */
static irqreturn_t cxlflash_sync_err_irq(int irq, void *data)
{
	struct hwq *hwq = (struct hwq *)data;
	struct cxlflash_cfg *cfg = hwq->afu->parent;
	struct device *dev = &cfg->dev->dev;
	u64 reg;
	u64 reg_unmasked;

	reg = readq_be(&hwq->host_map->intr_status);
	reg_unmasked = (reg & SISL_ISTATUS_UNMASK);

	if (reg_unmasked == 0UL) {
		dev_err(dev, "%s: spurious interrupt, intr_status=%016llx\n",
			__func__, reg);
		goto cxlflash_sync_err_irq_exit;
	}

	dev_err(dev, "%s: unexpected interrupt, intr_status=%016llx\n",
		__func__, reg);

	writeq_be(reg_unmasked, &hwq->host_map->intr_clear);

cxlflash_sync_err_irq_exit:
	return IRQ_HANDLED;
}

/**
 * process_hrrq() - process the read-response queue
 * @hwq:	HWQ associated with the host.
 * @doneq:	Queue of commands harvested from the RRQ.
 * @budget:	Threshold of RRQ entries to process.
 *
 * This routine must be called holding the disabled RRQ spin lock.
 *
 * Return: The number of entries processed.
 */
static int process_hrrq(struct hwq *hwq, struct list_head *doneq, int budget)
{
	struct afu *afu = hwq->afu;
	struct afu_cmd *cmd;
	struct sisl_ioasa *ioasa;
	struct sisl_ioarcb *ioarcb;
	bool toggle = hwq->toggle;
	int num_hrrq = 0;
	u64 entry,
	    *hrrq_start = hwq->hrrq_start,
	    *hrrq_end = hwq->hrrq_end,
	    *hrrq_curr = hwq->hrrq_curr;

	/* Process ready RRQ entries up to the specified budget (if any) */
	while (true) {
		entry = *hrrq_curr;

		if ((entry & SISL_RESP_HANDLE_T_BIT) != toggle)
			break;

		entry &= ~SISL_RESP_HANDLE_T_BIT;

		if (afu_is_sq_cmd_mode(afu)) {
			ioasa = (struct sisl_ioasa *)entry;
			cmd = container_of(ioasa, struct afu_cmd, sa);
		} else {
			ioarcb = (struct sisl_ioarcb *)entry;
			cmd = container_of(ioarcb, struct afu_cmd, rcb);
		}

		list_add_tail(&cmd->queue, doneq);

		/* Advance to next entry or wrap and flip the toggle bit */
		if (hrrq_curr < hrrq_end)
			hrrq_curr++;
		else {
			hrrq_curr = hrrq_start;
			toggle ^= SISL_RESP_HANDLE_T_BIT;
		}

		atomic_inc(&hwq->hsq_credits);
		num_hrrq++;

		if (budget > 0 && num_hrrq >= budget)
			break;
	}

	hwq->hrrq_curr = hrrq_curr;
	hwq->toggle = toggle;

	return num_hrrq;
}

/**
 * process_cmd_doneq() - process a queue of harvested RRQ commands
 * @doneq:	Queue of completed commands.
 *
 * Note that upon return the queue can no longer be trusted.
 */
static void process_cmd_doneq(struct list_head *doneq)
{
	struct afu_cmd *cmd, *tmp;

	WARN_ON(list_empty(doneq));

	list_for_each_entry_safe(cmd, tmp, doneq, queue)
		cmd_complete(cmd);
}

/**
 * cxlflash_irqpoll() - process a queue of harvested RRQ commands
 * @irqpoll:	IRQ poll structure associated with queue to poll.
 * @budget:	Threshold of RRQ entries to process per poll.
 *
 * Return: The number of entries processed.
 */
static int cxlflash_irqpoll(struct irq_poll *irqpoll, int budget)
{
	struct hwq *hwq = container_of(irqpoll, struct hwq, irqpoll);
	unsigned long hrrq_flags;
	LIST_HEAD(doneq);
	int num_entries = 0;

	spin_lock_irqsave(&hwq->hrrq_slock, hrrq_flags);

	num_entries = process_hrrq(hwq, &doneq, budget);
	if (num_entries < budget)
		irq_poll_complete(irqpoll);

	spin_unlock_irqrestore(&hwq->hrrq_slock, hrrq_flags);

	process_cmd_doneq(&doneq);
	return num_entries;
}

/**
 * cxlflash_rrq_irq() - interrupt handler for read-response queue (normal path)
 * @irq:	Interrupt number.
 * @data:	Private data provided at interrupt registration, the AFU.
 *
 * Return: IRQ_HANDLED or IRQ_NONE when no ready entries found.
 */
static irqreturn_t cxlflash_rrq_irq(int irq, void *data)
{
	struct hwq *hwq = (struct hwq *)data;
	struct afu *afu = hwq->afu;
	unsigned long hrrq_flags;
	LIST_HEAD(doneq);
	int num_entries = 0;

	spin_lock_irqsave(&hwq->hrrq_slock, hrrq_flags);

	/* Silently drop spurious interrupts when queue is not online */
	if (!hwq->hrrq_online) {
		spin_unlock_irqrestore(&hwq->hrrq_slock, hrrq_flags);
		return IRQ_HANDLED;
	}

	if (afu_is_irqpoll_enabled(afu)) {
		irq_poll_sched(&hwq->irqpoll);
		spin_unlock_irqrestore(&hwq->hrrq_slock, hrrq_flags);
		return IRQ_HANDLED;
	}

	num_entries = process_hrrq(hwq, &doneq, -1);
	spin_unlock_irqrestore(&hwq->hrrq_slock, hrrq_flags);

	if (num_entries == 0)
		return IRQ_NONE;

	process_cmd_doneq(&doneq);
	return IRQ_HANDLED;
}

/*
 * Asynchronous interrupt information table
 *
 * NOTE:
 *	- Order matters here as this array is indexed by bit position.
 *
 *	- The checkpatch script considers the BUILD_SISL_ASTATUS_FC_PORT macro
 *	  as complex and complains due to a lack of parentheses/braces.
 */
#define ASTATUS_FC(_a, _b, _c, _d)					 \
	{ SISL_ASTATUS_FC##_a##_##_b, _c, _a, (_d) }

#define BUILD_SISL_ASTATUS_FC_PORT(_a)					 \
	ASTATUS_FC(_a, LINK_UP, "link up", 0),				 \
	ASTATUS_FC(_a, LINK_DN, "link down", 0),			 \
	ASTATUS_FC(_a, LOGI_S, "login succeeded", SCAN_HOST),		 \
	ASTATUS_FC(_a, LOGI_F, "login failed", CLR_FC_ERROR),		 \
	ASTATUS_FC(_a, LOGI_R, "login timed out, retrying", LINK_RESET), \
	ASTATUS_FC(_a, CRC_T, "CRC threshold exceeded", LINK_RESET),	 \
	ASTATUS_FC(_a, LOGO, "target initiated LOGO", 0),		 \
	ASTATUS_FC(_a, OTHER, "other error", CLR_FC_ERROR | LINK_RESET)

static const struct asyc_intr_info ainfo[] = {
	BUILD_SISL_ASTATUS_FC_PORT(1),
	BUILD_SISL_ASTATUS_FC_PORT(0),
	BUILD_SISL_ASTATUS_FC_PORT(3),
	BUILD_SISL_ASTATUS_FC_PORT(2)
};

/**
 * cxlflash_async_err_irq() - interrupt handler for asynchronous errors
 * @irq:	Interrupt number.
 * @data:	Private data provided at interrupt registration, the AFU.
 *
 * Return: Always return IRQ_HANDLED.
 */
static irqreturn_t cxlflash_async_err_irq(int irq, void *data)
{
	struct hwq *hwq = (struct hwq *)data;
	struct afu *afu = hwq->afu;
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	const struct asyc_intr_info *info;
	struct sisl_global_map __iomem *global = &afu->afu_map->global;
	__be64 __iomem *fc_port_regs;
	u64 reg_unmasked;
	u64 reg;
	u64 bit;
	u8 port;

	reg = readq_be(&global->regs.aintr_status);
	reg_unmasked = (reg & SISL_ASTATUS_UNMASK);

	if (unlikely(reg_unmasked == 0)) {
		dev_err(dev, "%s: spurious interrupt, aintr_status=%016llx\n",
			__func__, reg);
		goto out;
	}

	/* FYI, it is 'okay' to clear AFU status before FC_ERROR */
	writeq_be(reg_unmasked, &global->regs.aintr_clear);

	/* Check each bit that is on */
	for_each_set_bit(bit, (ulong *)&reg_unmasked, BITS_PER_LONG) {
		if (unlikely(bit >= ARRAY_SIZE(ainfo))) {
			WARN_ON_ONCE(1);
			continue;
		}

		info = &ainfo[bit];
		if (unlikely(info->status != 1ULL << bit)) {
			WARN_ON_ONCE(1);
			continue;
		}

		port = info->port;
		fc_port_regs = get_fc_port_regs(cfg, port);

		dev_err(dev, "%s: FC Port %d -> %s, fc_status=%016llx\n",
			__func__, port, info->desc,
		       readq_be(&fc_port_regs[FC_STATUS / 8]));

		/*
		 * Do link reset first, some OTHER errors will set FC_ERROR
		 * again if cleared before or w/o a reset
		 */
		if (info->action & LINK_RESET) {
			dev_err(dev, "%s: FC Port %d: resetting link\n",
				__func__, port);
			cfg->lr_state = LINK_RESET_REQUIRED;
			cfg->lr_port = port;
			schedule_work(&cfg->work_q);
		}

		if (info->action & CLR_FC_ERROR) {
			reg = readq_be(&fc_port_regs[FC_ERROR / 8]);

			/*
			 * Since all errors are unmasked, FC_ERROR and FC_ERRCAP
			 * should be the same and tracing one is sufficient.
			 */

			dev_err(dev, "%s: fc %d: clearing fc_error=%016llx\n",
				__func__, port, reg);

			writeq_be(reg, &fc_port_regs[FC_ERROR / 8]);
			writeq_be(0, &fc_port_regs[FC_ERRCAP / 8]);
		}

		if (info->action & SCAN_HOST) {
			atomic_inc(&cfg->scan_host_needed);
			schedule_work(&cfg->work_q);
		}
	}

out:
	return IRQ_HANDLED;
}

/**
 * read_vpd() - obtains the WWPNs from VPD
 * @cfg:	Internal structure associated with the host.
 * @wwpn:	Array of size MAX_FC_PORTS to pass back WWPNs
 *
 * Return: 0 on success, -errno on failure
 */
static int read_vpd(struct cxlflash_cfg *cfg, u64 wwpn[])
{
	struct device *dev = &cfg->dev->dev;
	struct pci_dev *pdev = cfg->dev;
	int i, k, rc = 0;
	unsigned int kw_size;
	ssize_t vpd_size;
	char vpd_data[CXLFLASH_VPD_LEN];
	char tmp_buf[WWPN_BUF_LEN] = { 0 };
	const struct dev_dependent_vals *ddv = (struct dev_dependent_vals *)
						cfg->dev_id->driver_data;
	const bool wwpn_vpd_required = ddv->flags & CXLFLASH_WWPN_VPD_REQUIRED;
	const char *wwpn_vpd_tags[MAX_FC_PORTS] = { "V5", "V6", "V7", "V8" };

	/* Get the VPD data from the device */
	vpd_size = cfg->ops->read_adapter_vpd(pdev, vpd_data, sizeof(vpd_data));
	if (unlikely(vpd_size <= 0)) {
		dev_err(dev, "%s: Unable to read VPD (size = %ld)\n",
			__func__, vpd_size);
		rc = -ENODEV;
		goto out;
	}

	/*
	 * Find the offset of the WWPN tag within the read only
	 * VPD data and validate the found field (partials are
	 * no good to us). Convert the ASCII data to an integer
	 * value. Note that we must copy to a temporary buffer
	 * because the conversion service requires that the ASCII
	 * string be terminated.
	 *
	 * Allow for WWPN not being found for all devices, setting
	 * the returned WWPN to zero when not found. Notify with a
	 * log error for cards that should have had WWPN keywords
	 * in the VPD - cards requiring WWPN will not have their
	 * ports programmed and operate in an undefined state.
	 */
	for (k = 0; k < cfg->num_fc_ports; k++) {
		i = pci_vpd_find_ro_info_keyword(vpd_data, vpd_size,
						 wwpn_vpd_tags[k], &kw_size);
		if (i == -ENOENT) {
			if (wwpn_vpd_required)
				dev_err(dev, "%s: Port %d WWPN not found\n",
					__func__, k);
			wwpn[k] = 0ULL;
			continue;
		}

		if (i < 0 || kw_size != WWPN_LEN) {
			dev_err(dev, "%s: Port %d WWPN incomplete or bad VPD\n",
				__func__, k);
			rc = -ENODEV;
			goto out;
		}

		memcpy(tmp_buf, &vpd_data[i], WWPN_LEN);
		rc = kstrtoul(tmp_buf, WWPN_LEN, (ulong *)&wwpn[k]);
		if (unlikely(rc)) {
			dev_err(dev, "%s: WWPN conversion failed for port %d\n",
				__func__, k);
			rc = -ENODEV;
			goto out;
		}

		dev_dbg(dev, "%s: wwpn%d=%016llx\n", __func__, k, wwpn[k]);
	}

out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * init_pcr() - initialize the provisioning and control registers
 * @cfg:	Internal structure associated with the host.
 *
 * Also sets up fast access to the mapped registers and initializes AFU
 * command fields that never change.
 */
static void init_pcr(struct cxlflash_cfg *cfg)
{
	struct afu *afu = cfg->afu;
	struct sisl_ctrl_map __iomem *ctrl_map;
	struct hwq *hwq;
	void *cookie;
	int i;

	for (i = 0; i < MAX_CONTEXT; i++) {
		ctrl_map = &afu->afu_map->ctrls[i].ctrl;
		/* Disrupt any clients that could be running */
		/* e.g. clients that survived a master restart */
		writeq_be(0, &ctrl_map->rht_start);
		writeq_be(0, &ctrl_map->rht_cnt_id);
		writeq_be(0, &ctrl_map->ctx_cap);
	}

	/* Copy frequently used fields into hwq */
	for (i = 0; i < afu->num_hwqs; i++) {
		hwq = get_hwq(afu, i);
		cookie = hwq->ctx_cookie;

		hwq->ctx_hndl = (u16) cfg->ops->process_element(cookie);
		hwq->host_map = &afu->afu_map->hosts[hwq->ctx_hndl].host;
		hwq->ctrl_map = &afu->afu_map->ctrls[hwq->ctx_hndl].ctrl;

		/* Program the Endian Control for the master context */
		writeq_be(SISL_ENDIAN_CTRL, &hwq->host_map->endian_ctrl);
	}
}

/**
 * init_global() - initialize AFU global registers
 * @cfg:	Internal structure associated with the host.
 */
static int init_global(struct cxlflash_cfg *cfg)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq;
	struct sisl_host_map __iomem *hmap;
	__be64 __iomem *fc_port_regs;
	u64 wwpn[MAX_FC_PORTS];	/* wwpn of AFU ports */
	int i = 0, num_ports = 0;
	int rc = 0;
	int j;
	void *ctx;
	u64 reg;

	rc = read_vpd(cfg, &wwpn[0]);
	if (rc) {
		dev_err(dev, "%s: could not read vpd rc=%d\n", __func__, rc);
		goto out;
	}

	/* Set up RRQ and SQ in HWQ for master issued cmds */
	for (i = 0; i < afu->num_hwqs; i++) {
		hwq = get_hwq(afu, i);
		hmap = hwq->host_map;

		writeq_be((u64) hwq->hrrq_start, &hmap->rrq_start);
		writeq_be((u64) hwq->hrrq_end, &hmap->rrq_end);
		hwq->hrrq_online = true;

		if (afu_is_sq_cmd_mode(afu)) {
			writeq_be((u64)hwq->hsq_start, &hmap->sq_start);
			writeq_be((u64)hwq->hsq_end, &hmap->sq_end);
		}
	}

	/* AFU configuration */
	reg = readq_be(&afu->afu_map->global.regs.afu_config);
	reg |= SISL_AFUCONF_AR_ALL|SISL_AFUCONF_ENDIAN;
	/* enable all auto retry options and control endianness */
	/* leave others at default: */
	/* CTX_CAP write protected, mbox_r does not clear on read and */
	/* checker on if dual afu */
	writeq_be(reg, &afu->afu_map->global.regs.afu_config);

	/* Global port select: select either port */
	if (afu->internal_lun) {
		/* Only use port 0 */
		writeq_be(PORT0, &afu->afu_map->global.regs.afu_port_sel);
		num_ports = 0;
	} else {
		writeq_be(PORT_MASK(cfg->num_fc_ports),
			  &afu->afu_map->global.regs.afu_port_sel);
		num_ports = cfg->num_fc_ports;
	}

	for (i = 0; i < num_ports; i++) {
		fc_port_regs = get_fc_port_regs(cfg, i);

		/* Unmask all errors (but they are still masked at AFU) */
		writeq_be(0, &fc_port_regs[FC_ERRMSK / 8]);
		/* Clear CRC error cnt & set a threshold */
		(void)readq_be(&fc_port_regs[FC_CNT_CRCERR / 8]);
		writeq_be(MC_CRC_THRESH, &fc_port_regs[FC_CRC_THRESH / 8]);

		/* Set WWPNs. If already programmed, wwpn[i] is 0 */
		if (wwpn[i] != 0)
			afu_set_wwpn(afu, i, &fc_port_regs[0], wwpn[i]);
		/* Programming WWPN back to back causes additional
		 * offline/online transitions and a PLOGI
		 */
		msleep(100);
	}

	if (afu_is_ocxl_lisn(afu)) {
		/* Set up the LISN effective address for each master */
		for (i = 0; i < afu->num_hwqs; i++) {
			hwq = get_hwq(afu, i);
			ctx = hwq->ctx_cookie;

			for (j = 0; j < hwq->num_irqs; j++) {
				reg = cfg->ops->get_irq_objhndl(ctx, j);
				writeq_be(reg, &hwq->ctrl_map->lisn_ea[j]);
			}

			reg = hwq->ctx_hndl;
			writeq_be(SISL_LISN_PASID(reg, reg),
				  &hwq->ctrl_map->lisn_pasid[0]);
			writeq_be(SISL_LISN_PASID(0UL, reg),
				  &hwq->ctrl_map->lisn_pasid[1]);
		}
	}

	/* Set up master's own CTX_CAP to allow real mode, host translation */
	/* tables, afu cmds and read/write GSCSI cmds. */
	/* First, unlock ctx_cap write by reading mbox */
	for (i = 0; i < afu->num_hwqs; i++) {
		hwq = get_hwq(afu, i);

		(void)readq_be(&hwq->ctrl_map->mbox_r);	/* unlock ctx_cap */
		writeq_be((SISL_CTX_CAP_REAL_MODE | SISL_CTX_CAP_HOST_XLATE |
			SISL_CTX_CAP_READ_CMD | SISL_CTX_CAP_WRITE_CMD |
			SISL_CTX_CAP_AFU_CMD | SISL_CTX_CAP_GSCSI_CMD),
			&hwq->ctrl_map->ctx_cap);
	}

	/*
	 * Determine write-same unmap support for host by evaluating the unmap
	 * sector support bit of the context control register associated with
	 * the primary hardware queue. Note that while this status is reflected
	 * in a context register, the outcome can be assumed to be host-wide.
	 */
	hwq = get_hwq(afu, PRIMARY_HWQ);
	reg = readq_be(&hwq->host_map->ctx_ctrl);
	if (reg & SISL_CTX_CTRL_UNMAP_SECTOR)
		cfg->ws_unmap = true;

	/* Initialize heartbeat */
	afu->hb = readq_be(&afu->afu_map->global.regs.afu_hb);
out:
	return rc;
}

/**
 * start_afu() - initializes and starts the AFU
 * @cfg:	Internal structure associated with the host.
 */
static int start_afu(struct cxlflash_cfg *cfg)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq;
	int rc = 0;
	int i;

	init_pcr(cfg);

	/* Initialize each HWQ */
	for (i = 0; i < afu->num_hwqs; i++) {
		hwq = get_hwq(afu, i);

		/* After an AFU reset, RRQ entries are stale, clear them */
		memset(&hwq->rrq_entry, 0, sizeof(hwq->rrq_entry));

		/* Initialize RRQ pointers */
		hwq->hrrq_start = &hwq->rrq_entry[0];
		hwq->hrrq_end = &hwq->rrq_entry[NUM_RRQ_ENTRY - 1];
		hwq->hrrq_curr = hwq->hrrq_start;
		hwq->toggle = 1;

		/* Initialize spin locks */
		spin_lock_init(&hwq->hrrq_slock);
		spin_lock_init(&hwq->hsq_slock);

		/* Initialize SQ */
		if (afu_is_sq_cmd_mode(afu)) {
			memset(&hwq->sq, 0, sizeof(hwq->sq));
			hwq->hsq_start = &hwq->sq[0];
			hwq->hsq_end = &hwq->sq[NUM_SQ_ENTRY - 1];
			hwq->hsq_curr = hwq->hsq_start;

			atomic_set(&hwq->hsq_credits, NUM_SQ_ENTRY - 1);
		}

		/* Initialize IRQ poll */
		if (afu_is_irqpoll_enabled(afu))
			irq_poll_init(&hwq->irqpoll, afu->irqpoll_weight,
				      cxlflash_irqpoll);

	}

	rc = init_global(cfg);

	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * init_intr() - setup interrupt handlers for the master context
 * @cfg:	Internal structure associated with the host.
 * @hwq:	Hardware queue to initialize.
 *
 * Return: 0 on success, -errno on failure
 */
static enum undo_level init_intr(struct cxlflash_cfg *cfg,
				 struct hwq *hwq)
{
	struct device *dev = &cfg->dev->dev;
	void *ctx = hwq->ctx_cookie;
	int rc = 0;
	enum undo_level level = UNDO_NOOP;
	bool is_primary_hwq = (hwq->index == PRIMARY_HWQ);
	int num_irqs = hwq->num_irqs;

	rc = cfg->ops->allocate_afu_irqs(ctx, num_irqs);
	if (unlikely(rc)) {
		dev_err(dev, "%s: allocate_afu_irqs failed rc=%d\n",
			__func__, rc);
		level = UNDO_NOOP;
		goto out;
	}

	rc = cfg->ops->map_afu_irq(ctx, 1, cxlflash_sync_err_irq, hwq,
				   "SISL_MSI_SYNC_ERROR");
	if (unlikely(rc <= 0)) {
		dev_err(dev, "%s: SISL_MSI_SYNC_ERROR map failed\n", __func__);
		level = FREE_IRQ;
		goto out;
	}

	rc = cfg->ops->map_afu_irq(ctx, 2, cxlflash_rrq_irq, hwq,
				   "SISL_MSI_RRQ_UPDATED");
	if (unlikely(rc <= 0)) {
		dev_err(dev, "%s: SISL_MSI_RRQ_UPDATED map failed\n", __func__);
		level = UNMAP_ONE;
		goto out;
	}

	/* SISL_MSI_ASYNC_ERROR is setup only for the primary HWQ */
	if (!is_primary_hwq)
		goto out;

	rc = cfg->ops->map_afu_irq(ctx, 3, cxlflash_async_err_irq, hwq,
				   "SISL_MSI_ASYNC_ERROR");
	if (unlikely(rc <= 0)) {
		dev_err(dev, "%s: SISL_MSI_ASYNC_ERROR map failed\n", __func__);
		level = UNMAP_TWO;
		goto out;
	}
out:
	return level;
}

/**
 * init_mc() - create and register as the master context
 * @cfg:	Internal structure associated with the host.
 * @index:	HWQ Index of the master context.
 *
 * Return: 0 on success, -errno on failure
 */
static int init_mc(struct cxlflash_cfg *cfg, u32 index)
{
	void *ctx;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq = get_hwq(cfg->afu, index);
	int rc = 0;
	int num_irqs;
	enum undo_level level;

	hwq->afu = cfg->afu;
	hwq->index = index;
	INIT_LIST_HEAD(&hwq->pending_cmds);

	if (index == PRIMARY_HWQ) {
		ctx = cfg->ops->get_context(cfg->dev, cfg->afu_cookie);
		num_irqs = 3;
	} else {
		ctx = cfg->ops->dev_context_init(cfg->dev, cfg->afu_cookie);
		num_irqs = 2;
	}
	if (IS_ERR_OR_NULL(ctx)) {
		rc = -ENOMEM;
		goto err1;
	}

	WARN_ON(hwq->ctx_cookie);
	hwq->ctx_cookie = ctx;
	hwq->num_irqs = num_irqs;

	/* Set it up as a master with the CXL */
	cfg->ops->set_master(ctx);

	/* Reset AFU when initializing primary context */
	if (index == PRIMARY_HWQ) {
		rc = cfg->ops->afu_reset(ctx);
		if (unlikely(rc)) {
			dev_err(dev, "%s: AFU reset failed rc=%d\n",
				      __func__, rc);
			goto err1;
		}
	}

	level = init_intr(cfg, hwq);
	if (unlikely(level)) {
		dev_err(dev, "%s: interrupt init failed rc=%d\n", __func__, rc);
		goto err2;
	}

	/* Finally, activate the context by starting it */
	rc = cfg->ops->start_context(hwq->ctx_cookie);
	if (unlikely(rc)) {
		dev_err(dev, "%s: start context failed rc=%d\n", __func__, rc);
		level = UNMAP_THREE;
		goto err2;
	}

out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
err2:
	term_intr(cfg, level, index);
	if (index != PRIMARY_HWQ)
		cfg->ops->release_context(ctx);
err1:
	hwq->ctx_cookie = NULL;
	goto out;
}

/**
 * get_num_afu_ports() - determines and configures the number of AFU ports
 * @cfg:	Internal structure associated with the host.
 *
 * This routine determines the number of AFU ports by converting the global
 * port selection mask. The converted value is only valid following an AFU
 * reset (explicit or power-on). This routine must be invoked shortly after
 * mapping as other routines are dependent on the number of ports during the
 * initialization sequence.
 *
 * To support legacy AFUs that might not have reflected an initial global
 * port mask (value read is 0), default to the number of ports originally
 * supported by the cxlflash driver (2) before hardware with other port
 * offerings was introduced.
 */
static void get_num_afu_ports(struct cxlflash_cfg *cfg)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	u64 port_mask;
	int num_fc_ports = LEGACY_FC_PORTS;

	port_mask = readq_be(&afu->afu_map->global.regs.afu_port_sel);
	if (port_mask != 0ULL)
		num_fc_ports = min(ilog2(port_mask) + 1, MAX_FC_PORTS);

	dev_dbg(dev, "%s: port_mask=%016llx num_fc_ports=%d\n",
		__func__, port_mask, num_fc_ports);

	cfg->num_fc_ports = num_fc_ports;
	cfg->host->max_channel = PORTNUM2CHAN(num_fc_ports);
}

/**
 * init_afu() - setup as master context and start AFU
 * @cfg:	Internal structure associated with the host.
 *
 * This routine is a higher level of control for configuring the
 * AFU on probe and reset paths.
 *
 * Return: 0 on success, -errno on failure
 */
static int init_afu(struct cxlflash_cfg *cfg)
{
	u64 reg;
	int rc = 0;
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct hwq *hwq;
	int i;

	cfg->ops->perst_reloads_same_image(cfg->afu_cookie, true);

	mutex_init(&afu->sync_active);
	afu->num_hwqs = afu->desired_hwqs;
	for (i = 0; i < afu->num_hwqs; i++) {
		rc = init_mc(cfg, i);
		if (rc) {
			dev_err(dev, "%s: init_mc failed rc=%d index=%d\n",
				__func__, rc, i);
			goto err1;
		}
	}

	/* Map the entire MMIO space of the AFU using the first context */
	hwq = get_hwq(afu, PRIMARY_HWQ);
	afu->afu_map = cfg->ops->psa_map(hwq->ctx_cookie);
	if (!afu->afu_map) {
		dev_err(dev, "%s: psa_map failed\n", __func__);
		rc = -ENOMEM;
		goto err1;
	}

	/* No byte reverse on reading afu_version or string will be backwards */
	reg = readq(&afu->afu_map->global.regs.afu_version);
	memcpy(afu->version, &reg, sizeof(reg));
	afu->interface_version =
	    readq_be(&afu->afu_map->global.regs.interface_version);
	if ((afu->interface_version + 1) == 0) {
		dev_err(dev, "Back level AFU, please upgrade. AFU version %s "
			"interface version %016llx\n", afu->version,
		       afu->interface_version);
		rc = -EINVAL;
		goto err1;
	}

	if (afu_is_sq_cmd_mode(afu)) {
		afu->send_cmd = send_cmd_sq;
		afu->context_reset = context_reset_sq;
	} else {
		afu->send_cmd = send_cmd_ioarrin;
		afu->context_reset = context_reset_ioarrin;
	}

	dev_dbg(dev, "%s: afu_ver=%s interface_ver=%016llx\n", __func__,
		afu->version, afu->interface_version);

	get_num_afu_ports(cfg);

	rc = start_afu(cfg);
	if (rc) {
		dev_err(dev, "%s: start_afu failed, rc=%d\n", __func__, rc);
		goto err1;
	}

	afu_err_intr_init(cfg->afu);
	for (i = 0; i < afu->num_hwqs; i++) {
		hwq = get_hwq(afu, i);

		hwq->room = readq_be(&hwq->host_map->cmd_room);
	}

	/* Restore the LUN mappings */
	cxlflash_restore_luntable(cfg);
out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;

err1:
	for (i = afu->num_hwqs - 1; i >= 0; i--) {
		term_intr(cfg, UNMAP_THREE, i);
		term_mc(cfg, i);
	}
	goto out;
}

/**
 * afu_reset() - resets the AFU
 * @cfg:	Internal structure associated with the host.
 *
 * Return: 0 on success, -errno on failure
 */
static int afu_reset(struct cxlflash_cfg *cfg)
{
	struct device *dev = &cfg->dev->dev;
	int rc = 0;

	/* Stop the context before the reset. Since the context is
	 * no longer available restart it after the reset is complete
	 */
	term_afu(cfg);

	rc = init_afu(cfg);

	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * drain_ioctls() - wait until all currently executing ioctls have completed
 * @cfg:	Internal structure associated with the host.
 *
 * Obtain write access to read/write semaphore that wraps ioctl
 * handling to 'drain' ioctls currently executing.
 */
static void drain_ioctls(struct cxlflash_cfg *cfg)
{
	down_write(&cfg->ioctl_rwsem);
	up_write(&cfg->ioctl_rwsem);
}

/**
 * cxlflash_async_reset_host() - asynchronous host reset handler
 * @data:	Private data provided while scheduling reset.
 * @cookie:	Cookie that can be used for checkpointing.
 */
static void cxlflash_async_reset_host(void *data, async_cookie_t cookie)
{
	struct cxlflash_cfg *cfg = data;
	struct device *dev = &cfg->dev->dev;
	int rc = 0;

	if (cfg->state != STATE_RESET) {
		dev_dbg(dev, "%s: Not performing a reset, state=%d\n",
			__func__, cfg->state);
		goto out;
	}

	drain_ioctls(cfg);
	cxlflash_mark_contexts_error(cfg);
	rc = afu_reset(cfg);
	if (rc)
		cfg->state = STATE_FAILTERM;
	else
		cfg->state = STATE_NORMAL;
	wake_up_all(&cfg->reset_waitq);

out:
	scsi_unblock_requests(cfg->host);
}

/**
 * cxlflash_schedule_async_reset() - schedule an asynchronous host reset
 * @cfg:	Internal structure associated with the host.
 */
static void cxlflash_schedule_async_reset(struct cxlflash_cfg *cfg)
{
	struct device *dev = &cfg->dev->dev;

	if (cfg->state != STATE_NORMAL) {
		dev_dbg(dev, "%s: Not performing reset state=%d\n",
			__func__, cfg->state);
		return;
	}

	cfg->state = STATE_RESET;
	scsi_block_requests(cfg->host);
	cfg->async_reset_cookie = async_schedule(cxlflash_async_reset_host,
						 cfg);
}

/**
 * send_afu_cmd() - builds and sends an internal AFU command
 * @afu:	AFU associated with the host.
 * @rcb:	Pre-populated IOARCB describing command to send.
 *
 * The AFU can only take one internal AFU command at a time. This limitation is
 * enforced by using a mutex to provide exclusive access to the AFU during the
 * operation. This design point requires calling threads to not be on interrupt
 * context due to the possibility of sleeping during concurrent AFU operations.
 *
 * The command status is optionally passed back to the caller when the caller
 * populates the IOASA field of the IOARCB with a pointer to an IOASA structure.
 *
 * Return:
 *	0 on success, -errno on failure
 */
static int send_afu_cmd(struct afu *afu, struct sisl_ioarcb *rcb)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	struct afu_cmd *cmd = NULL;
	struct hwq *hwq = get_hwq(afu, PRIMARY_HWQ);
	ulong lock_flags;
	char *buf = NULL;
	int rc = 0;
	int nretry = 0;

	if (cfg->state != STATE_NORMAL) {
		dev_dbg(dev, "%s: Sync not required state=%u\n",
			__func__, cfg->state);
		return 0;
	}

	mutex_lock(&afu->sync_active);
	atomic_inc(&afu->cmds_active);
	buf = kmalloc(sizeof(*cmd) + __alignof__(*cmd) - 1, GFP_KERNEL);
	if (unlikely(!buf)) {
		dev_err(dev, "%s: no memory for command\n", __func__);
		rc = -ENOMEM;
		goto out;
	}

	cmd = (struct afu_cmd *)PTR_ALIGN(buf, __alignof__(*cmd));

retry:
	memset(cmd, 0, sizeof(*cmd));
	memcpy(&cmd->rcb, rcb, sizeof(*rcb));
	INIT_LIST_HEAD(&cmd->queue);
	init_completion(&cmd->cevent);
	cmd->parent = afu;
	cmd->hwq_index = hwq->index;
	cmd->rcb.ctx_id = hwq->ctx_hndl;

	dev_dbg(dev, "%s: afu=%p cmd=%p type=%02x nretry=%d\n",
		__func__, afu, cmd, cmd->rcb.cdb[0], nretry);

	rc = afu->send_cmd(afu, cmd);
	if (unlikely(rc)) {
		rc = -ENOBUFS;
		goto out;
	}

	rc = wait_resp(afu, cmd);
	switch (rc) {
	case -ETIMEDOUT:
		rc = afu->context_reset(hwq);
		if (rc) {
			/* Delete the command from pending_cmds list */
			spin_lock_irqsave(&hwq->hsq_slock, lock_flags);
			list_del(&cmd->list);
			spin_unlock_irqrestore(&hwq->hsq_slock, lock_flags);

			cxlflash_schedule_async_reset(cfg);
			break;
		}
		fallthrough;	/* to retry */
	case -EAGAIN:
		if (++nretry < 2)
			goto retry;
		fallthrough;	/* to exit */
	default:
		break;
	}

	if (rcb->ioasa)
		*rcb->ioasa = cmd->sa;
out:
	atomic_dec(&afu->cmds_active);
	mutex_unlock(&afu->sync_active);
	kfree(buf);
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * cxlflash_afu_sync() - builds and sends an AFU sync command
 * @afu:	AFU associated with the host.
 * @ctx:	Identifies context requesting sync.
 * @res:	Identifies resource requesting sync.
 * @mode:	Type of sync to issue (lightweight, heavyweight, global).
 *
 * AFU sync operations are only necessary and allowed when the device is
 * operating normally. When not operating normally, sync requests can occur as
 * part of cleaning up resources associated with an adapter prior to removal.
 * In this scenario, these requests are simply ignored (safe due to the AFU
 * going away).
 *
 * Return:
 *	0 on success, -errno on failure
 */
int cxlflash_afu_sync(struct afu *afu, ctx_hndl_t ctx, res_hndl_t res, u8 mode)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	struct sisl_ioarcb rcb = { 0 };

	dev_dbg(dev, "%s: afu=%p ctx=%u res=%u mode=%u\n",
		__func__, afu, ctx, res, mode);

	rcb.req_flags = SISL_REQ_FLAGS_AFU_CMD;
	rcb.msi = SISL_MSI_RRQ_UPDATED;
	rcb.timeout = MC_AFU_SYNC_TIMEOUT;

	rcb.cdb[0] = SISL_AFU_CMD_SYNC;
	rcb.cdb[1] = mode;
	put_unaligned_be16(ctx, &rcb.cdb[2]);
	put_unaligned_be32(res, &rcb.cdb[4]);

	return send_afu_cmd(afu, &rcb);
}

/**
 * cxlflash_eh_abort_handler() - abort a SCSI command
 * @scp:	SCSI command to abort.
 *
 * CXL Flash devices do not support a single command abort. Reset the context
 * as per SISLite specification. Flush any pending commands in the hardware
 * queue before the reset.
 *
 * Return: SUCCESS/FAILED as defined in scsi/scsi.h
 */
static int cxlflash_eh_abort_handler(struct scsi_cmnd *scp)
{
	int rc = FAILED;
	struct Scsi_Host *host = scp->device->host;
	struct cxlflash_cfg *cfg = shost_priv(host);
	struct afu_cmd *cmd = sc_to_afuc(scp);
	struct device *dev = &cfg->dev->dev;
	struct afu *afu = cfg->afu;
	struct hwq *hwq = get_hwq(afu, cmd->hwq_index);

	dev_dbg(dev, "%s: (scp=%p) %d/%d/%d/%llu "
		"cdb=(%08x-%08x-%08x-%08x)\n", __func__, scp, host->host_no,
		scp->device->channel, scp->device->id, scp->device->lun,
		get_unaligned_be32(&((u32 *)scp->cmnd)[0]),
		get_unaligned_be32(&((u32 *)scp->cmnd)[1]),
		get_unaligned_be32(&((u32 *)scp->cmnd)[2]),
		get_unaligned_be32(&((u32 *)scp->cmnd)[3]));

	/* When the state is not normal, another reset/reload is in progress.
	 * Return failed and the mid-layer will invoke host reset handler.
	 */
	if (cfg->state != STATE_NORMAL) {
		dev_dbg(dev, "%s: Invalid state for abort, state=%d\n",
			__func__, cfg->state);
		goto out;
	}

	rc = afu->context_reset(hwq);
	if (unlikely(rc))
		goto out;

	rc = SUCCESS;

out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * cxlflash_eh_device_reset_handler() - reset a single LUN
 * @scp:	SCSI command to send.
 *
 * Return:
 *	SUCCESS as defined in scsi/scsi.h
 *	FAILED as defined in scsi/scsi.h
 */
static int cxlflash_eh_device_reset_handler(struct scsi_cmnd *scp)
{
	int rc = SUCCESS;
	struct scsi_device *sdev = scp->device;
	struct Scsi_Host *host = sdev->host;
	struct cxlflash_cfg *cfg = shost_priv(host);
	struct device *dev = &cfg->dev->dev;
	int rcr = 0;

	dev_dbg(dev, "%s: %d/%d/%d/%llu\n", __func__,
		host->host_no, sdev->channel, sdev->id, sdev->lun);
retry:
	switch (cfg->state) {
	case STATE_NORMAL:
		rcr = send_tmf(cfg, sdev, TMF_LUN_RESET);
		if (unlikely(rcr))
			rc = FAILED;
		break;
	case STATE_RESET:
		wait_event(cfg->reset_waitq, cfg->state != STATE_RESET);
		goto retry;
	default:
		rc = FAILED;
		break;
	}

	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * cxlflash_eh_host_reset_handler() - reset the host adapter
 * @scp:	SCSI command from stack identifying host.
 *
 * Following a reset, the state is evaluated again in case an EEH occurred
 * during the reset. In such a scenario, the host reset will either yield
 * until the EEH recovery is complete or return success or failure based
 * upon the current device state.
 *
 * Return:
 *	SUCCESS as defined in scsi/scsi.h
 *	FAILED as defined in scsi/scsi.h
 */
static int cxlflash_eh_host_reset_handler(struct scsi_cmnd *scp)
{
	int rc = SUCCESS;
	int rcr = 0;
	struct Scsi_Host *host = scp->device->host;
	struct cxlflash_cfg *cfg = shost_priv(host);
	struct device *dev = &cfg->dev->dev;

	dev_dbg(dev, "%s: %d\n", __func__, host->host_no);

	switch (cfg->state) {
	case STATE_NORMAL:
		cfg->state = STATE_RESET;
		drain_ioctls(cfg);
		cxlflash_mark_contexts_error(cfg);
		rcr = afu_reset(cfg);
		if (rcr) {
			rc = FAILED;
			cfg->state = STATE_FAILTERM;
		} else
			cfg->state = STATE_NORMAL;
		wake_up_all(&cfg->reset_waitq);
		ssleep(1);
		fallthrough;
	case STATE_RESET:
		wait_event(cfg->reset_waitq, cfg->state != STATE_RESET);
		if (cfg->state == STATE_NORMAL)
			break;
		fallthrough;
	default:
		rc = FAILED;
		break;
	}

	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * cxlflash_change_queue_depth() - change the queue depth for the device
 * @sdev:	SCSI device destined for queue depth change.
 * @qdepth:	Requested queue depth value to set.
 *
 * The requested queue depth is capped to the maximum supported value.
 *
 * Return: The actual queue depth set.
 */
static int cxlflash_change_queue_depth(struct scsi_device *sdev, int qdepth)
{

	if (qdepth > CXLFLASH_MAX_CMDS_PER_LUN)
		qdepth = CXLFLASH_MAX_CMDS_PER_LUN;

	scsi_change_queue_depth(sdev, qdepth);
	return sdev->queue_depth;
}

/**
 * cxlflash_show_port_status() - queries and presents the current port status
 * @port:	Desired port for status reporting.
 * @cfg:	Internal structure associated with the host.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf or -EINVAL.
 */
static ssize_t cxlflash_show_port_status(u32 port,
					 struct cxlflash_cfg *cfg,
					 char *buf)
{
	struct device *dev = &cfg->dev->dev;
	char *disp_status;
	u64 status;
	__be64 __iomem *fc_port_regs;

	WARN_ON(port >= MAX_FC_PORTS);

	if (port >= cfg->num_fc_ports) {
		dev_info(dev, "%s: Port %d not supported on this card.\n",
			__func__, port);
		return -EINVAL;
	}

	fc_port_regs = get_fc_port_regs(cfg, port);
	status = readq_be(&fc_port_regs[FC_MTIP_STATUS / 8]);
	status &= FC_MTIP_STATUS_MASK;

	if (status == FC_MTIP_STATUS_ONLINE)
		disp_status = "online";
	else if (status == FC_MTIP_STATUS_OFFLINE)
		disp_status = "offline";
	else
		disp_status = "unknown";

	return scnprintf(buf, PAGE_SIZE, "%s\n", disp_status);
}

/**
 * port0_show() - queries and presents the current status of port 0
 * @dev:	Generic device associated with the host owning the port.
 * @attr:	Device attribute representing the port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t port0_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));

	return cxlflash_show_port_status(0, cfg, buf);
}

/**
 * port1_show() - queries and presents the current status of port 1
 * @dev:	Generic device associated with the host owning the port.
 * @attr:	Device attribute representing the port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t port1_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));

	return cxlflash_show_port_status(1, cfg, buf);
}

/**
 * port2_show() - queries and presents the current status of port 2
 * @dev:	Generic device associated with the host owning the port.
 * @attr:	Device attribute representing the port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t port2_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));

	return cxlflash_show_port_status(2, cfg, buf);
}

/**
 * port3_show() - queries and presents the current status of port 3
 * @dev:	Generic device associated with the host owning the port.
 * @attr:	Device attribute representing the port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t port3_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));

	return cxlflash_show_port_status(3, cfg, buf);
}

/**
 * lun_mode_show() - presents the current LUN mode of the host
 * @dev:	Generic device associated with the host.
 * @attr:	Device attribute representing the LUN mode.
 * @buf:	Buffer of length PAGE_SIZE to report back the LUN mode in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t lun_mode_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));
	struct afu *afu = cfg->afu;

	return scnprintf(buf, PAGE_SIZE, "%u\n", afu->internal_lun);
}

/**
 * lun_mode_store() - sets the LUN mode of the host
 * @dev:	Generic device associated with the host.
 * @attr:	Device attribute representing the LUN mode.
 * @buf:	Buffer of length PAGE_SIZE containing the LUN mode in ASCII.
 * @count:	Length of data resizing in @buf.
 *
 * The CXL Flash AFU supports a dummy LUN mode where the external
 * links and storage are not required. Space on the FPGA is used
 * to create 1 or 2 small LUNs which are presented to the system
 * as if they were a normal storage device. This feature is useful
 * during development and also provides manufacturing with a way
 * to test the AFU without an actual device.
 *
 * 0 = external LUN[s] (default)
 * 1 = internal LUN (1 x 64K, 512B blocks, id 0)
 * 2 = internal LUN (1 x 64K, 4K blocks, id 0)
 * 3 = internal LUN (2 x 32K, 512B blocks, ids 0,1)
 * 4 = internal LUN (2 x 32K, 4K blocks, ids 0,1)
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t lun_mode_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct cxlflash_cfg *cfg = shost_priv(shost);
	struct afu *afu = cfg->afu;
	int rc;
	u32 lun_mode;

	rc = kstrtouint(buf, 10, &lun_mode);
	if (!rc && (lun_mode < 5) && (lun_mode != afu->internal_lun)) {
		afu->internal_lun = lun_mode;

		/*
		 * When configured for internal LUN, there is only one channel,
		 * channel number 0, else there will be one less than the number
		 * of fc ports for this card.
		 */
		if (afu->internal_lun)
			shost->max_channel = 0;
		else
			shost->max_channel = PORTNUM2CHAN(cfg->num_fc_ports);

		afu_reset(cfg);
		scsi_scan_host(cfg->host);
	}

	return count;
}

/**
 * ioctl_version_show() - presents the current ioctl version of the host
 * @dev:	Generic device associated with the host.
 * @attr:	Device attribute representing the ioctl version.
 * @buf:	Buffer of length PAGE_SIZE to report back the ioctl version.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t ioctl_version_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t bytes = 0;

	bytes = scnprintf(buf, PAGE_SIZE,
			  "disk: %u\n", DK_CXLFLASH_VERSION_0);
	bytes += scnprintf(buf + bytes, PAGE_SIZE - bytes,
			   "host: %u\n", HT_CXLFLASH_VERSION_0);

	return bytes;
}

/**
 * cxlflash_show_port_lun_table() - queries and presents the port LUN table
 * @port:	Desired port for status reporting.
 * @cfg:	Internal structure associated with the host.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf or -EINVAL.
 */
static ssize_t cxlflash_show_port_lun_table(u32 port,
					    struct cxlflash_cfg *cfg,
					    char *buf)
{
	struct device *dev = &cfg->dev->dev;
	__be64 __iomem *fc_port_luns;
	int i;
	ssize_t bytes = 0;

	WARN_ON(port >= MAX_FC_PORTS);

	if (port >= cfg->num_fc_ports) {
		dev_info(dev, "%s: Port %d not supported on this card.\n",
			__func__, port);
		return -EINVAL;
	}

	fc_port_luns = get_fc_port_luns(cfg, port);

	for (i = 0; i < CXLFLASH_NUM_VLUNS; i++)
		bytes += scnprintf(buf + bytes, PAGE_SIZE - bytes,
				   "%03d: %016llx\n",
				   i, readq_be(&fc_port_luns[i]));
	return bytes;
}

/**
 * port0_lun_table_show() - presents the current LUN table of port 0
 * @dev:	Generic device associated with the host owning the port.
 * @attr:	Device attribute representing the port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t port0_lun_table_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));

	return cxlflash_show_port_lun_table(0, cfg, buf);
}

/**
 * port1_lun_table_show() - presents the current LUN table of port 1
 * @dev:	Generic device associated with the host owning the port.
 * @attr:	Device attribute representing the port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t port1_lun_table_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));

	return cxlflash_show_port_lun_table(1, cfg, buf);
}

/**
 * port2_lun_table_show() - presents the current LUN table of port 2
 * @dev:	Generic device associated with the host owning the port.
 * @attr:	Device attribute representing the port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t port2_lun_table_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));

	return cxlflash_show_port_lun_table(2, cfg, buf);
}

/**
 * port3_lun_table_show() - presents the current LUN table of port 3
 * @dev:	Generic device associated with the host owning the port.
 * @attr:	Device attribute representing the port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t port3_lun_table_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));

	return cxlflash_show_port_lun_table(3, cfg, buf);
}

/**
 * irqpoll_weight_show() - presents the current IRQ poll weight for the host
 * @dev:	Generic device associated with the host.
 * @attr:	Device attribute representing the IRQ poll weight.
 * @buf:	Buffer of length PAGE_SIZE to report back the current IRQ poll
 *		weight in ASCII.
 *
 * An IRQ poll weight of 0 indicates polling is disabled.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t irqpoll_weight_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));
	struct afu *afu = cfg->afu;

	return scnprintf(buf, PAGE_SIZE, "%u\n", afu->irqpoll_weight);
}

/**
 * irqpoll_weight_store() - sets the current IRQ poll weight for the host
 * @dev:	Generic device associated with the host.
 * @attr:	Device attribute representing the IRQ poll weight.
 * @buf:	Buffer of length PAGE_SIZE containing the desired IRQ poll
 *		weight in ASCII.
 * @count:	Length of data resizing in @buf.
 *
 * An IRQ poll weight of 0 indicates polling is disabled.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t irqpoll_weight_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));
	struct device *cfgdev = &cfg->dev->dev;
	struct afu *afu = cfg->afu;
	struct hwq *hwq;
	u32 weight;
	int rc, i;

	rc = kstrtouint(buf, 10, &weight);
	if (rc)
		return -EINVAL;

	if (weight > 256) {
		dev_info(cfgdev,
			 "Invalid IRQ poll weight. It must be 256 or less.\n");
		return -EINVAL;
	}

	if (weight == afu->irqpoll_weight) {
		dev_info(cfgdev,
			 "Current IRQ poll weight has the same weight.\n");
		return -EINVAL;
	}

	if (afu_is_irqpoll_enabled(afu)) {
		for (i = 0; i < afu->num_hwqs; i++) {
			hwq = get_hwq(afu, i);

			irq_poll_disable(&hwq->irqpoll);
		}
	}

	afu->irqpoll_weight = weight;

	if (weight > 0) {
		for (i = 0; i < afu->num_hwqs; i++) {
			hwq = get_hwq(afu, i);

			irq_poll_init(&hwq->irqpoll, weight, cxlflash_irqpoll);
		}
	}

	return count;
}

/**
 * num_hwqs_show() - presents the number of hardware queues for the host
 * @dev:	Generic device associated with the host.
 * @attr:	Device attribute representing the number of hardware queues.
 * @buf:	Buffer of length PAGE_SIZE to report back the number of hardware
 *		queues in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t num_hwqs_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));
	struct afu *afu = cfg->afu;

	return scnprintf(buf, PAGE_SIZE, "%u\n", afu->num_hwqs);
}

/**
 * num_hwqs_store() - sets the number of hardware queues for the host
 * @dev:	Generic device associated with the host.
 * @attr:	Device attribute representing the number of hardware queues.
 * @buf:	Buffer of length PAGE_SIZE containing the number of hardware
 *		queues in ASCII.
 * @count:	Length of data resizing in @buf.
 *
 * n > 0: num_hwqs = n
 * n = 0: num_hwqs = num_online_cpus()
 * n < 0: num_online_cpus() / abs(n)
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t num_hwqs_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));
	struct afu *afu = cfg->afu;
	int rc;
	int nhwqs, num_hwqs;

	rc = kstrtoint(buf, 10, &nhwqs);
	if (rc)
		return -EINVAL;

	if (nhwqs >= 1)
		num_hwqs = nhwqs;
	else if (nhwqs == 0)
		num_hwqs = num_online_cpus();
	else
		num_hwqs = num_online_cpus() / abs(nhwqs);

	afu->desired_hwqs = min(num_hwqs, CXLFLASH_MAX_HWQS);
	WARN_ON_ONCE(afu->desired_hwqs == 0);

retry:
	switch (cfg->state) {
	case STATE_NORMAL:
		cfg->state = STATE_RESET;
		drain_ioctls(cfg);
		cxlflash_mark_contexts_error(cfg);
		rc = afu_reset(cfg);
		if (rc)
			cfg->state = STATE_FAILTERM;
		else
			cfg->state = STATE_NORMAL;
		wake_up_all(&cfg->reset_waitq);
		break;
	case STATE_RESET:
		wait_event(cfg->reset_waitq, cfg->state != STATE_RESET);
		if (cfg->state == STATE_NORMAL)
			goto retry;
		fallthrough;
	default:
		/* Ideally should not happen */
		dev_err(dev, "%s: Device is not ready, state=%d\n",
			__func__, cfg->state);
		break;
	}

	return count;
}

static const char *hwq_mode_name[MAX_HWQ_MODE] = { "rr", "tag", "cpu" };

/**
 * hwq_mode_show() - presents the HWQ steering mode for the host
 * @dev:	Generic device associated with the host.
 * @attr:	Device attribute representing the HWQ steering mode.
 * @buf:	Buffer of length PAGE_SIZE to report back the HWQ steering mode
 *		as a character string.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t hwq_mode_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct cxlflash_cfg *cfg = shost_priv(class_to_shost(dev));
	struct afu *afu = cfg->afu;

	return scnprintf(buf, PAGE_SIZE, "%s\n", hwq_mode_name[afu->hwq_mode]);
}

/**
 * hwq_mode_store() - sets the HWQ steering mode for the host
 * @dev:	Generic device associated with the host.
 * @attr:	Device attribute representing the HWQ steering mode.
 * @buf:	Buffer of length PAGE_SIZE containing the HWQ steering mode
 *		as a character string.
 * @count:	Length of data resizing in @buf.
 *
 * rr = Round-Robin
 * tag = Block MQ Tagging
 * cpu = CPU Affinity
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t hwq_mode_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct cxlflash_cfg *cfg = shost_priv(shost);
	struct device *cfgdev = &cfg->dev->dev;
	struct afu *afu = cfg->afu;
	int i;
	u32 mode = MAX_HWQ_MODE;

	for (i = 0; i < MAX_HWQ_MODE; i++) {
		if (!strncmp(hwq_mode_name[i], buf, strlen(hwq_mode_name[i]))) {
			mode = i;
			break;
		}
	}

	if (mode >= MAX_HWQ_MODE) {
		dev_info(cfgdev, "Invalid HWQ steering mode.\n");
		return -EINVAL;
	}

	afu->hwq_mode = mode;

	return count;
}

/**
 * mode_show() - presents the current mode of the device
 * @dev:	Generic device associated with the device.
 * @attr:	Device attribute representing the device mode.
 * @buf:	Buffer of length PAGE_SIZE to report back the dev mode in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t mode_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			 sdev->hostdata ? "superpipe" : "legacy");
}

/*
 * Host attributes
 */
static DEVICE_ATTR_RO(port0);
static DEVICE_ATTR_RO(port1);
static DEVICE_ATTR_RO(port2);
static DEVICE_ATTR_RO(port3);
static DEVICE_ATTR_RW(lun_mode);
static DEVICE_ATTR_RO(ioctl_version);
static DEVICE_ATTR_RO(port0_lun_table);
static DEVICE_ATTR_RO(port1_lun_table);
static DEVICE_ATTR_RO(port2_lun_table);
static DEVICE_ATTR_RO(port3_lun_table);
static DEVICE_ATTR_RW(irqpoll_weight);
static DEVICE_ATTR_RW(num_hwqs);
static DEVICE_ATTR_RW(hwq_mode);

static struct attribute *cxlflash_host_attrs[] = {
	&dev_attr_port0.attr,
	&dev_attr_port1.attr,
	&dev_attr_port2.attr,
	&dev_attr_port3.attr,
	&dev_attr_lun_mode.attr,
	&dev_attr_ioctl_version.attr,
	&dev_attr_port0_lun_table.attr,
	&dev_attr_port1_lun_table.attr,
	&dev_attr_port2_lun_table.attr,
	&dev_attr_port3_lun_table.attr,
	&dev_attr_irqpoll_weight.attr,
	&dev_attr_num_hwqs.attr,
	&dev_attr_hwq_mode.attr,
	NULL
};

ATTRIBUTE_GROUPS(cxlflash_host);

/*
 * Device attributes
 */
static DEVICE_ATTR_RO(mode);

static struct attribute *cxlflash_dev_attrs[] = {
	&dev_attr_mode.attr,
	NULL
};

ATTRIBUTE_GROUPS(cxlflash_dev);

/*
 * Host template
 */
static struct scsi_host_template driver_template = {
	.module = THIS_MODULE,
	.name = CXLFLASH_ADAPTER_NAME,
	.info = cxlflash_driver_info,
	.ioctl = cxlflash_ioctl,
	.proc_name = CXLFLASH_NAME,
	.queuecommand = cxlflash_queuecommand,
	.eh_abort_handler = cxlflash_eh_abort_handler,
	.eh_device_reset_handler = cxlflash_eh_device_reset_handler,
	.eh_host_reset_handler = cxlflash_eh_host_reset_handler,
	.change_queue_depth = cxlflash_change_queue_depth,
	.cmd_per_lun = CXLFLASH_MAX_CMDS_PER_LUN,
	.can_queue = CXLFLASH_MAX_CMDS,
	.cmd_size = sizeof(struct afu_cmd) + __alignof__(struct afu_cmd) - 1,
	.this_id = -1,
	.sg_tablesize = 1,	/* No scatter gather support */
	.max_sectors = CXLFLASH_MAX_SECTORS,
	.shost_groups = cxlflash_host_groups,
	.sdev_groups = cxlflash_dev_groups,
};

/*
 * Device dependent values
 */
static struct dev_dependent_vals dev_corsa_vals = { CXLFLASH_MAX_SECTORS,
					CXLFLASH_WWPN_VPD_REQUIRED };
static struct dev_dependent_vals dev_flash_gt_vals = { CXLFLASH_MAX_SECTORS,
					CXLFLASH_NOTIFY_SHUTDOWN };
static struct dev_dependent_vals dev_briard_vals = { CXLFLASH_MAX_SECTORS,
					(CXLFLASH_NOTIFY_SHUTDOWN |
					CXLFLASH_OCXL_DEV) };

/*
 * PCI device binding table
 */
static struct pci_device_id cxlflash_pci_table[] = {
	{PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CORSA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, (kernel_ulong_t)&dev_corsa_vals},
	{PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_FLASH_GT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, (kernel_ulong_t)&dev_flash_gt_vals},
	{PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_BRIARD,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, (kernel_ulong_t)&dev_briard_vals},
	{}
};

MODULE_DEVICE_TABLE(pci, cxlflash_pci_table);

/**
 * cxlflash_worker_thread() - work thread handler for the AFU
 * @work:	Work structure contained within cxlflash associated with host.
 *
 * Handles the following events:
 * - Link reset which cannot be performed on interrupt context due to
 * blocking up to a few seconds
 * - Rescan the host
 */
static void cxlflash_worker_thread(struct work_struct *work)
{
	struct cxlflash_cfg *cfg = container_of(work, struct cxlflash_cfg,
						work_q);
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	__be64 __iomem *fc_port_regs;
	int port;
	ulong lock_flags;

	/* Avoid MMIO if the device has failed */

	if (cfg->state != STATE_NORMAL)
		return;

	spin_lock_irqsave(cfg->host->host_lock, lock_flags);

	if (cfg->lr_state == LINK_RESET_REQUIRED) {
		port = cfg->lr_port;
		if (port < 0)
			dev_err(dev, "%s: invalid port index %d\n",
				__func__, port);
		else {
			spin_unlock_irqrestore(cfg->host->host_lock,
					       lock_flags);

			/* The reset can block... */
			fc_port_regs = get_fc_port_regs(cfg, port);
			afu_link_reset(afu, port, fc_port_regs);
			spin_lock_irqsave(cfg->host->host_lock, lock_flags);
		}

		cfg->lr_state = LINK_RESET_COMPLETE;
	}

	spin_unlock_irqrestore(cfg->host->host_lock, lock_flags);

	if (atomic_dec_if_positive(&cfg->scan_host_needed) >= 0)
		scsi_scan_host(cfg->host);
}

/**
 * cxlflash_chr_open() - character device open handler
 * @inode:	Device inode associated with this character device.
 * @file:	File pointer for this device.
 *
 * Only users with admin privileges are allowed to open the character device.
 *
 * Return: 0 on success, -errno on failure
 */
static int cxlflash_chr_open(struct inode *inode, struct file *file)
{
	struct cxlflash_cfg *cfg;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	cfg = container_of(inode->i_cdev, struct cxlflash_cfg, cdev);
	file->private_data = cfg;

	return 0;
}

/**
 * decode_hioctl() - translates encoded host ioctl to easily identifiable string
 * @cmd:        The host ioctl command to decode.
 *
 * Return: A string identifying the decoded host ioctl.
 */
static char *decode_hioctl(unsigned int cmd)
{
	switch (cmd) {
	case HT_CXLFLASH_LUN_PROVISION:
		return __stringify_1(HT_CXLFLASH_LUN_PROVISION);
	}

	return "UNKNOWN";
}

/**
 * cxlflash_lun_provision() - host LUN provisioning handler
 * @cfg:	Internal structure associated with the host.
 * @lunprov:	Kernel copy of userspace ioctl data structure.
 *
 * Return: 0 on success, -errno on failure
 */
static int cxlflash_lun_provision(struct cxlflash_cfg *cfg,
				  struct ht_cxlflash_lun_provision *lunprov)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct sisl_ioarcb rcb;
	struct sisl_ioasa asa;
	__be64 __iomem *fc_port_regs;
	u16 port = lunprov->port;
	u16 scmd = lunprov->hdr.subcmd;
	u16 type;
	u64 reg;
	u64 size;
	u64 lun_id;
	int rc = 0;

	if (!afu_is_lun_provision(afu)) {
		rc = -ENOTSUPP;
		goto out;
	}

	if (port >= cfg->num_fc_ports) {
		rc = -EINVAL;
		goto out;
	}

	switch (scmd) {
	case HT_CXLFLASH_LUN_PROVISION_SUBCMD_CREATE_LUN:
		type = SISL_AFU_LUN_PROVISION_CREATE;
		size = lunprov->size;
		lun_id = 0;
		break;
	case HT_CXLFLASH_LUN_PROVISION_SUBCMD_DELETE_LUN:
		type = SISL_AFU_LUN_PROVISION_DELETE;
		size = 0;
		lun_id = lunprov->lun_id;
		break;
	case HT_CXLFLASH_LUN_PROVISION_SUBCMD_QUERY_PORT:
		fc_port_regs = get_fc_port_regs(cfg, port);

		reg = readq_be(&fc_port_regs[FC_MAX_NUM_LUNS / 8]);
		lunprov->max_num_luns = reg;
		reg = readq_be(&fc_port_regs[FC_CUR_NUM_LUNS / 8]);
		lunprov->cur_num_luns = reg;
		reg = readq_be(&fc_port_regs[FC_MAX_CAP_PORT / 8]);
		lunprov->max_cap_port = reg;
		reg = readq_be(&fc_port_regs[FC_CUR_CAP_PORT / 8]);
		lunprov->cur_cap_port = reg;

		goto out;
	default:
		rc = -EINVAL;
		goto out;
	}

	memset(&rcb, 0, sizeof(rcb));
	memset(&asa, 0, sizeof(asa));
	rcb.req_flags = SISL_REQ_FLAGS_AFU_CMD;
	rcb.lun_id = lun_id;
	rcb.msi = SISL_MSI_RRQ_UPDATED;
	rcb.timeout = MC_LUN_PROV_TIMEOUT;
	rcb.ioasa = &asa;

	rcb.cdb[0] = SISL_AFU_CMD_LUN_PROVISION;
	rcb.cdb[1] = type;
	rcb.cdb[2] = port;
	put_unaligned_be64(size, &rcb.cdb[8]);

	rc = send_afu_cmd(afu, &rcb);
	if (rc) {
		dev_err(dev, "%s: send_afu_cmd failed rc=%d asc=%08x afux=%x\n",
			__func__, rc, asa.ioasc, asa.afu_extra);
		goto out;
	}

	if (scmd == HT_CXLFLASH_LUN_PROVISION_SUBCMD_CREATE_LUN) {
		lunprov->lun_id = (u64)asa.lunid_hi << 32 | asa.lunid_lo;
		memcpy(lunprov->wwid, asa.wwid, sizeof(lunprov->wwid));
	}
out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * cxlflash_afu_debug() - host AFU debug handler
 * @cfg:	Internal structure associated with the host.
 * @afu_dbg:	Kernel copy of userspace ioctl data structure.
 *
 * For debug requests requiring a data buffer, always provide an aligned
 * (cache line) buffer to the AFU to appease any alignment requirements.
 *
 * Return: 0 on success, -errno on failure
 */
static int cxlflash_afu_debug(struct cxlflash_cfg *cfg,
			      struct ht_cxlflash_afu_debug *afu_dbg)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct sisl_ioarcb rcb;
	struct sisl_ioasa asa;
	char *buf = NULL;
	char *kbuf = NULL;
	void __user *ubuf = (__force void __user *)afu_dbg->data_ea;
	u16 req_flags = SISL_REQ_FLAGS_AFU_CMD;
	u32 ulen = afu_dbg->data_len;
	bool is_write = afu_dbg->hdr.flags & HT_CXLFLASH_HOST_WRITE;
	int rc = 0;

	if (!afu_is_afu_debug(afu)) {
		rc = -ENOTSUPP;
		goto out;
	}

	if (ulen) {
		req_flags |= SISL_REQ_FLAGS_SUP_UNDERRUN;

		if (ulen > HT_CXLFLASH_AFU_DEBUG_MAX_DATA_LEN) {
			rc = -EINVAL;
			goto out;
		}

		buf = kmalloc(ulen + cache_line_size() - 1, GFP_KERNEL);
		if (unlikely(!buf)) {
			rc = -ENOMEM;
			goto out;
		}

		kbuf = PTR_ALIGN(buf, cache_line_size());

		if (is_write) {
			req_flags |= SISL_REQ_FLAGS_HOST_WRITE;

			if (copy_from_user(kbuf, ubuf, ulen)) {
				rc = -EFAULT;
				goto out;
			}
		}
	}

	memset(&rcb, 0, sizeof(rcb));
	memset(&asa, 0, sizeof(asa));

	rcb.req_flags = req_flags;
	rcb.msi = SISL_MSI_RRQ_UPDATED;
	rcb.timeout = MC_AFU_DEBUG_TIMEOUT;
	rcb.ioasa = &asa;

	if (ulen) {
		rcb.data_len = ulen;
		rcb.data_ea = (uintptr_t)kbuf;
	}

	rcb.cdb[0] = SISL_AFU_CMD_DEBUG;
	memcpy(&rcb.cdb[4], afu_dbg->afu_subcmd,
	       HT_CXLFLASH_AFU_DEBUG_SUBCMD_LEN);

	rc = send_afu_cmd(afu, &rcb);
	if (rc) {
		dev_err(dev, "%s: send_afu_cmd failed rc=%d asc=%08x afux=%x\n",
			__func__, rc, asa.ioasc, asa.afu_extra);
		goto out;
	}

	if (ulen && !is_write) {
		if (copy_to_user(ubuf, kbuf, ulen))
			rc = -EFAULT;
	}
out:
	kfree(buf);
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * cxlflash_chr_ioctl() - character device IOCTL handler
 * @file:	File pointer for this device.
 * @cmd:	IOCTL command.
 * @arg:	Userspace ioctl data structure.
 *
 * A read/write semaphore is used to implement a 'drain' of currently
 * running ioctls. The read semaphore is taken at the beginning of each
 * ioctl thread and released upon concluding execution. Additionally the
 * semaphore should be released and then reacquired in any ioctl execution
 * path which will wait for an event to occur that is outside the scope of
 * the ioctl (i.e. an adapter reset). To drain the ioctls currently running,
 * a thread simply needs to acquire the write semaphore.
 *
 * Return: 0 on success, -errno on failure
 */
static long cxlflash_chr_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	typedef int (*hioctl) (struct cxlflash_cfg *, void *);

	struct cxlflash_cfg *cfg = file->private_data;
	struct device *dev = &cfg->dev->dev;
	char buf[sizeof(union cxlflash_ht_ioctls)];
	void __user *uarg = (void __user *)arg;
	struct ht_cxlflash_hdr *hdr;
	size_t size = 0;
	bool known_ioctl = false;
	int idx = 0;
	int rc = 0;
	hioctl do_ioctl = NULL;

	static const struct {
		size_t size;
		hioctl ioctl;
	} ioctl_tbl[] = {	/* NOTE: order matters here */
	{ sizeof(struct ht_cxlflash_lun_provision),
		(hioctl)cxlflash_lun_provision },
	{ sizeof(struct ht_cxlflash_afu_debug),
		(hioctl)cxlflash_afu_debug },
	};

	/* Hold read semaphore so we can drain if needed */
	down_read(&cfg->ioctl_rwsem);

	dev_dbg(dev, "%s: cmd=%u idx=%d tbl_size=%lu\n",
		__func__, cmd, idx, sizeof(ioctl_tbl));

	switch (cmd) {
	case HT_CXLFLASH_LUN_PROVISION:
	case HT_CXLFLASH_AFU_DEBUG:
		known_ioctl = true;
		idx = _IOC_NR(HT_CXLFLASH_LUN_PROVISION) - _IOC_NR(cmd);
		size = ioctl_tbl[idx].size;
		do_ioctl = ioctl_tbl[idx].ioctl;

		if (likely(do_ioctl))
			break;

		fallthrough;
	default:
		rc = -EINVAL;
		goto out;
	}

	if (unlikely(copy_from_user(&buf, uarg, size))) {
		dev_err(dev, "%s: copy_from_user() fail "
			"size=%lu cmd=%d (%s) uarg=%p\n",
			__func__, size, cmd, decode_hioctl(cmd), uarg);
		rc = -EFAULT;
		goto out;
	}

	hdr = (struct ht_cxlflash_hdr *)&buf;
	if (hdr->version != HT_CXLFLASH_VERSION_0) {
		dev_dbg(dev, "%s: Version %u not supported for %s\n",
			__func__, hdr->version, decode_hioctl(cmd));
		rc = -EINVAL;
		goto out;
	}

	if (hdr->rsvd[0] || hdr->rsvd[1] || hdr->return_flags) {
		dev_dbg(dev, "%s: Reserved/rflags populated\n", __func__);
		rc = -EINVAL;
		goto out;
	}

	rc = do_ioctl(cfg, (void *)&buf);
	if (likely(!rc))
		if (unlikely(copy_to_user(uarg, &buf, size))) {
			dev_err(dev, "%s: copy_to_user() fail "
				"size=%lu cmd=%d (%s) uarg=%p\n",
				__func__, size, cmd, decode_hioctl(cmd), uarg);
			rc = -EFAULT;
		}

	/* fall through to exit */

out:
	up_read(&cfg->ioctl_rwsem);
	if (unlikely(rc && known_ioctl))
		dev_err(dev, "%s: ioctl %s (%08X) returned rc=%d\n",
			__func__, decode_hioctl(cmd), cmd, rc);
	else
		dev_dbg(dev, "%s: ioctl %s (%08X) returned rc=%d\n",
			__func__, decode_hioctl(cmd), cmd, rc);
	return rc;
}

/*
 * Character device file operations
 */
static const struct file_operations cxlflash_chr_fops = {
	.owner          = THIS_MODULE,
	.open           = cxlflash_chr_open,
	.unlocked_ioctl	= cxlflash_chr_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

/**
 * init_chrdev() - initialize the character device for the host
 * @cfg:	Internal structure associated with the host.
 *
 * Return: 0 on success, -errno on failure
 */
static int init_chrdev(struct cxlflash_cfg *cfg)
{
	struct device *dev = &cfg->dev->dev;
	struct device *char_dev;
	dev_t devno;
	int minor;
	int rc = 0;

	minor = cxlflash_get_minor();
	if (unlikely(minor < 0)) {
		dev_err(dev, "%s: Exhausted allowed adapters\n", __func__);
		rc = -ENOSPC;
		goto out;
	}

	devno = MKDEV(cxlflash_major, minor);
	cdev_init(&cfg->cdev, &cxlflash_chr_fops);

	rc = cdev_add(&cfg->cdev, devno, 1);
	if (rc) {
		dev_err(dev, "%s: cdev_add failed rc=%d\n", __func__, rc);
		goto err1;
	}

	char_dev = device_create(cxlflash_class, NULL, devno,
				 NULL, "cxlflash%d", minor);
	if (IS_ERR(char_dev)) {
		rc = PTR_ERR(char_dev);
		dev_err(dev, "%s: device_create failed rc=%d\n",
			__func__, rc);
		goto err2;
	}

	cfg->chardev = char_dev;
out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
err2:
	cdev_del(&cfg->cdev);
err1:
	cxlflash_put_minor(minor);
	goto out;
}

/**
 * cxlflash_probe() - PCI entry point to add host
 * @pdev:	PCI device associated with the host.
 * @dev_id:	PCI device id associated with device.
 *
 * The device will initially start out in a 'probing' state and
 * transition to the 'normal' state at the end of a successful
 * probe. Should an EEH event occur during probe, the notification
 * thread (error_detected()) will wait until the probe handler
 * is nearly complete. At that time, the device will be moved to
 * a 'probed' state and the EEH thread woken up to drive the slot
 * reset and recovery (device moves to 'normal' state). Meanwhile,
 * the probe will be allowed to exit successfully.
 *
 * Return: 0 on success, -errno on failure
 */
static int cxlflash_probe(struct pci_dev *pdev,
			  const struct pci_device_id *dev_id)
{
	struct Scsi_Host *host;
	struct cxlflash_cfg *cfg = NULL;
	struct device *dev = &pdev->dev;
	struct dev_dependent_vals *ddv;
	int rc = 0;
	int k;

	dev_dbg(&pdev->dev, "%s: Found CXLFLASH with IRQ: %d\n",
		__func__, pdev->irq);

	ddv = (struct dev_dependent_vals *)dev_id->driver_data;
	driver_template.max_sectors = ddv->max_sectors;

	host = scsi_host_alloc(&driver_template, sizeof(struct cxlflash_cfg));
	if (!host) {
		dev_err(dev, "%s: scsi_host_alloc failed\n", __func__);
		rc = -ENOMEM;
		goto out;
	}

	host->max_id = CXLFLASH_MAX_NUM_TARGETS_PER_BUS;
	host->max_lun = CXLFLASH_MAX_NUM_LUNS_PER_TARGET;
	host->unique_id = host->host_no;
	host->max_cmd_len = CXLFLASH_MAX_CDB_LEN;

	cfg = shost_priv(host);
	cfg->state = STATE_PROBING;
	cfg->host = host;
	rc = alloc_mem(cfg);
	if (rc) {
		dev_err(dev, "%s: alloc_mem failed\n", __func__);
		rc = -ENOMEM;
		scsi_host_put(cfg->host);
		goto out;
	}

	cfg->init_state = INIT_STATE_NONE;
	cfg->dev = pdev;
	cfg->cxl_fops = cxlflash_cxl_fops;
	cfg->ops = cxlflash_assign_ops(ddv);
	WARN_ON_ONCE(!cfg->ops);

	/*
	 * Promoted LUNs move to the top of the LUN table. The rest stay on
	 * the bottom half. The bottom half grows from the end (index = 255),
	 * whereas the top half grows from the beginning (index = 0).
	 *
	 * Initialize the last LUN index for all possible ports.
	 */
	cfg->promote_lun_index = 0;

	for (k = 0; k < MAX_FC_PORTS; k++)
		cfg->last_lun_index[k] = CXLFLASH_NUM_VLUNS/2 - 1;

	cfg->dev_id = (struct pci_device_id *)dev_id;

	init_waitqueue_head(&cfg->tmf_waitq);
	init_waitqueue_head(&cfg->reset_waitq);

	INIT_WORK(&cfg->work_q, cxlflash_worker_thread);
	cfg->lr_state = LINK_RESET_INVALID;
	cfg->lr_port = -1;
	spin_lock_init(&cfg->tmf_slock);
	mutex_init(&cfg->ctx_tbl_list_mutex);
	mutex_init(&cfg->ctx_recovery_mutex);
	init_rwsem(&cfg->ioctl_rwsem);
	INIT_LIST_HEAD(&cfg->ctx_err_recovery);
	INIT_LIST_HEAD(&cfg->lluns);

	pci_set_drvdata(pdev, cfg);

	rc = init_pci(cfg);
	if (rc) {
		dev_err(dev, "%s: init_pci failed rc=%d\n", __func__, rc);
		goto out_remove;
	}
	cfg->init_state = INIT_STATE_PCI;

	cfg->afu_cookie = cfg->ops->create_afu(pdev);
	if (unlikely(!cfg->afu_cookie)) {
		dev_err(dev, "%s: create_afu failed\n", __func__);
		rc = -ENOMEM;
		goto out_remove;
	}

	rc = init_afu(cfg);
	if (rc && !wq_has_sleeper(&cfg->reset_waitq)) {
		dev_err(dev, "%s: init_afu failed rc=%d\n", __func__, rc);
		goto out_remove;
	}
	cfg->init_state = INIT_STATE_AFU;

	rc = init_scsi(cfg);
	if (rc) {
		dev_err(dev, "%s: init_scsi failed rc=%d\n", __func__, rc);
		goto out_remove;
	}
	cfg->init_state = INIT_STATE_SCSI;

	rc = init_chrdev(cfg);
	if (rc) {
		dev_err(dev, "%s: init_chrdev failed rc=%d\n", __func__, rc);
		goto out_remove;
	}
	cfg->init_state = INIT_STATE_CDEV;

	if (wq_has_sleeper(&cfg->reset_waitq)) {
		cfg->state = STATE_PROBED;
		wake_up_all(&cfg->reset_waitq);
	} else
		cfg->state = STATE_NORMAL;
out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;

out_remove:
	cfg->state = STATE_PROBED;
	cxlflash_remove(pdev);
	goto out;
}

/**
 * cxlflash_pci_error_detected() - called when a PCI error is detected
 * @pdev:	PCI device struct.
 * @state:	PCI channel state.
 *
 * When an EEH occurs during an active reset, wait until the reset is
 * complete and then take action based upon the device state.
 *
 * Return: PCI_ERS_RESULT_NEED_RESET or PCI_ERS_RESULT_DISCONNECT
 */
static pci_ers_result_t cxlflash_pci_error_detected(struct pci_dev *pdev,
						    pci_channel_state_t state)
{
	int rc = 0;
	struct cxlflash_cfg *cfg = pci_get_drvdata(pdev);
	struct device *dev = &cfg->dev->dev;

	dev_dbg(dev, "%s: pdev=%p state=%u\n", __func__, pdev, state);

	switch (state) {
	case pci_channel_io_frozen:
		wait_event(cfg->reset_waitq, cfg->state != STATE_RESET &&
					     cfg->state != STATE_PROBING);
		if (cfg->state == STATE_FAILTERM)
			return PCI_ERS_RESULT_DISCONNECT;

		cfg->state = STATE_RESET;
		scsi_block_requests(cfg->host);
		drain_ioctls(cfg);
		rc = cxlflash_mark_contexts_error(cfg);
		if (unlikely(rc))
			dev_err(dev, "%s: Failed to mark user contexts rc=%d\n",
				__func__, rc);
		term_afu(cfg);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		cfg->state = STATE_FAILTERM;
		wake_up_all(&cfg->reset_waitq);
		scsi_unblock_requests(cfg->host);
		return PCI_ERS_RESULT_DISCONNECT;
	default:
		break;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * cxlflash_pci_slot_reset() - called when PCI slot has been reset
 * @pdev:	PCI device struct.
 *
 * This routine is called by the pci error recovery code after the PCI
 * slot has been reset, just before we should resume normal operations.
 *
 * Return: PCI_ERS_RESULT_RECOVERED or PCI_ERS_RESULT_DISCONNECT
 */
static pci_ers_result_t cxlflash_pci_slot_reset(struct pci_dev *pdev)
{
	int rc = 0;
	struct cxlflash_cfg *cfg = pci_get_drvdata(pdev);
	struct device *dev = &cfg->dev->dev;

	dev_dbg(dev, "%s: pdev=%p\n", __func__, pdev);

	rc = init_afu(cfg);
	if (unlikely(rc)) {
		dev_err(dev, "%s: EEH recovery failed rc=%d\n", __func__, rc);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * cxlflash_pci_resume() - called when normal operation can resume
 * @pdev:	PCI device struct
 */
static void cxlflash_pci_resume(struct pci_dev *pdev)
{
	struct cxlflash_cfg *cfg = pci_get_drvdata(pdev);
	struct device *dev = &cfg->dev->dev;

	dev_dbg(dev, "%s: pdev=%p\n", __func__, pdev);

	cfg->state = STATE_NORMAL;
	wake_up_all(&cfg->reset_waitq);
	scsi_unblock_requests(cfg->host);
}

/**
 * cxlflash_devnode() - provides devtmpfs for devices in the cxlflash class
 * @dev:	Character device.
 * @mode:	Mode that can be used to verify access.
 *
 * Return: Allocated string describing the devtmpfs structure.
 */
static char *cxlflash_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "cxlflash/%s", dev_name(dev));
}

/**
 * cxlflash_class_init() - create character device class
 *
 * Return: 0 on success, -errno on failure
 */
static int cxlflash_class_init(void)
{
	dev_t devno;
	int rc = 0;

	rc = alloc_chrdev_region(&devno, 0, CXLFLASH_MAX_ADAPTERS, "cxlflash");
	if (unlikely(rc)) {
		pr_err("%s: alloc_chrdev_region failed rc=%d\n", __func__, rc);
		goto out;
	}

	cxlflash_major = MAJOR(devno);

	cxlflash_class = class_create(THIS_MODULE, "cxlflash");
	if (IS_ERR(cxlflash_class)) {
		rc = PTR_ERR(cxlflash_class);
		pr_err("%s: class_create failed rc=%d\n", __func__, rc);
		goto err;
	}

	cxlflash_class->devnode = cxlflash_devnode;
out:
	pr_debug("%s: returning rc=%d\n", __func__, rc);
	return rc;
err:
	unregister_chrdev_region(devno, CXLFLASH_MAX_ADAPTERS);
	goto out;
}

/**
 * cxlflash_class_exit() - destroy character device class
 */
static void cxlflash_class_exit(void)
{
	dev_t devno = MKDEV(cxlflash_major, 0);

	class_destroy(cxlflash_class);
	unregister_chrdev_region(devno, CXLFLASH_MAX_ADAPTERS);
}

static const struct pci_error_handlers cxlflash_err_handler = {
	.error_detected = cxlflash_pci_error_detected,
	.slot_reset = cxlflash_pci_slot_reset,
	.resume = cxlflash_pci_resume,
};

/*
 * PCI device structure
 */
static struct pci_driver cxlflash_driver = {
	.name = CXLFLASH_NAME,
	.id_table = cxlflash_pci_table,
	.probe = cxlflash_probe,
	.remove = cxlflash_remove,
	.shutdown = cxlflash_remove,
	.err_handler = &cxlflash_err_handler,
};

/**
 * init_cxlflash() - module entry point
 *
 * Return: 0 on success, -errno on failure
 */
static int __init init_cxlflash(void)
{
	int rc;

	check_sizes();
	cxlflash_list_init();
	rc = cxlflash_class_init();
	if (unlikely(rc))
		goto out;

	rc = pci_register_driver(&cxlflash_driver);
	if (unlikely(rc))
		goto err;
out:
	pr_debug("%s: returning rc=%d\n", __func__, rc);
	return rc;
err:
	cxlflash_class_exit();
	goto out;
}

/**
 * exit_cxlflash() - module exit point
 */
static void __exit exit_cxlflash(void)
{
	cxlflash_term_global_luns();
	cxlflash_free_errpage();

	pci_unregister_driver(&cxlflash_driver);
	cxlflash_class_exit();
}

module_init(init_cxlflash);
module_exit(exit_cxlflash);
