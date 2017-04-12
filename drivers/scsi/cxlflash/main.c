/*
 * CXL Flash Device Driver
 *
 * Written by: Manoj N. Kumar <manoj@linux.vnet.ibm.com>, IBM Corporation
 *             Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2015 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <asm/unaligned.h>

#include <misc/cxl.h>

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
	struct sisl_ioarcb *ioarcb;
	struct sisl_ioasa *ioasa;
	u32 resid;

	if (unlikely(!cmd))
		return;

	ioarcb = &(cmd->rcb);
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
			scp->result = (DID_ALLOC_FAILURE << 16);
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
 * Prepares and submits command that has either completed or timed out to
 * the SCSI stack. Checks AFU command back into command pool for non-internal
 * (cmd->scp populated) commands.
 */
static void cmd_complete(struct afu_cmd *cmd)
{
	struct scsi_cmnd *scp;
	ulong lock_flags;
	struct afu *afu = cmd->parent;
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	bool cmd_is_tmf;

	if (cmd->scp) {
		scp = cmd->scp;
		if (unlikely(cmd->sa.ioasc))
			process_cmd_err(cmd, scp);
		else
			scp->result = (DID_OK << 16);

		cmd_is_tmf = cmd->cmd_tmf;

		dev_dbg_ratelimited(dev, "%s:scp=%p result=%08x ioasc=%08x\n",
				    __func__, scp, scp->result, cmd->sa.ioasc);

		scsi_dma_unmap(scp);
		scp->scsi_done(scp);

		if (cmd_is_tmf) {
			spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
			cfg->tmf_active = false;
			wake_up_all_locked(&cfg->tmf_waitq);
			spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);
		}
	} else
		complete(&cmd->cevent);
}

/**
 * context_reset() - reset command owner context via specified register
 * @cmd:	AFU command that timed out.
 * @reset_reg:	MMIO register to perform reset.
 */
static void context_reset(struct afu_cmd *cmd, __be64 __iomem *reset_reg)
{
	int nretry = 0;
	u64 rrin = 0x1;
	struct afu *afu = cmd->parent;
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;

	dev_dbg(dev, "%s: cmd=%p\n", __func__, cmd);

	writeq_be(rrin, reset_reg);
	do {
		rrin = readq_be(reset_reg);
		if (rrin != 0x1)
			break;
		/* Double delay each time */
		udelay(1 << nretry);
	} while (nretry++ < MC_ROOM_RETRY_CNT);

	dev_dbg(dev, "%s: returning rrin=%016llx nretry=%d\n",
		__func__, rrin, nretry);
}

/**
 * context_reset_ioarrin() - reset command owner context via IOARRIN register
 * @cmd:	AFU command that timed out.
 */
static void context_reset_ioarrin(struct afu_cmd *cmd)
{
	struct afu *afu = cmd->parent;

	context_reset(cmd, &afu->host_map->ioarrin);
}

/**
 * context_reset_sq() - reset command owner context w/ SQ Context Reset register
 * @cmd:	AFU command that timed out.
 */
static void context_reset_sq(struct afu_cmd *cmd)
{
	struct afu *afu = cmd->parent;

	context_reset(cmd, &afu->host_map->sq_ctx_reset);
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
	int rc = 0;
	s64 room;
	ulong lock_flags;

	/*
	 * To avoid the performance penalty of MMIO, spread the update of
	 * 'room' over multiple commands.
	 */
	spin_lock_irqsave(&afu->rrin_slock, lock_flags);
	if (--afu->room < 0) {
		room = readq_be(&afu->host_map->cmd_room);
		if (room <= 0) {
			dev_dbg_ratelimited(dev, "%s: no cmd_room to send "
					    "0x%02X, room=0x%016llX\n",
					    __func__, cmd->rcb.cdb[0], room);
			afu->room = 0;
			rc = SCSI_MLQUEUE_HOST_BUSY;
			goto out;
		}
		afu->room = room - 1;
	}

	writeq_be((u64)&cmd->rcb, &afu->host_map->ioarrin);
out:
	spin_unlock_irqrestore(&afu->rrin_slock, lock_flags);
	dev_dbg(dev, "%s: cmd=%p len=%u ea=%016llx rc=%d\n", __func__,
		cmd, cmd->rcb.data_len, cmd->rcb.data_ea, rc);
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
	int rc = 0;
	int newval;
	ulong lock_flags;

	newval = atomic_dec_if_positive(&afu->hsq_credits);
	if (newval <= 0) {
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	cmd->rcb.ioasa = &cmd->sa;

	spin_lock_irqsave(&afu->hsq_slock, lock_flags);

	*afu->hsq_curr = cmd->rcb;
	if (afu->hsq_curr < afu->hsq_end)
		afu->hsq_curr++;
	else
		afu->hsq_curr = afu->hsq_start;
	writeq_be((u64)afu->hsq_curr, &afu->host_map->sq_tail);

	spin_unlock_irqrestore(&afu->hsq_slock, lock_flags);
out:
	dev_dbg(dev, "%s: cmd=%p len=%u ea=%016llx ioasa=%p rc=%d curr=%p "
	       "head=%016llx tail=%016llx\n", __func__, cmd, cmd->rcb.data_len,
	       cmd->rcb.data_ea, cmd->rcb.ioasa, rc, afu->hsq_curr,
	       readq_be(&afu->host_map->sq_head),
	       readq_be(&afu->host_map->sq_tail));
	return rc;
}

/**
 * wait_resp() - polls for a response or timeout to a sent AFU command
 * @afu:	AFU associated with the host.
 * @cmd:	AFU command that was sent.
 *
 * Return:
 *	0 on success, -1 on timeout/error
 */
static int wait_resp(struct afu *afu, struct afu_cmd *cmd)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	int rc = 0;
	ulong timeout = msecs_to_jiffies(cmd->rcb.timeout * 2 * 1000);

	timeout = wait_for_completion_timeout(&cmd->cevent, timeout);
	if (!timeout) {
		afu->context_reset(cmd);
		rc = -1;
	}

	if (unlikely(cmd->sa.ioasc != 0)) {
		dev_err(dev, "%s: cmd %02x failed, ioasc=%08x\n",
			__func__, cmd->rcb.cdb[0], cmd->sa.ioasc);
		rc = -1;
	}

	return rc;
}

/**
 * send_tmf() - sends a Task Management Function (TMF)
 * @afu:	AFU to checkout from.
 * @scp:	SCSI command from stack.
 * @tmfcmd:	TMF command to send.
 *
 * Return:
 *	0 on success, SCSI_MLQUEUE_HOST_BUSY on failure
 */
static int send_tmf(struct afu *afu, struct scsi_cmnd *scp, u64 tmfcmd)
{
	u32 port_sel = scp->device->channel + 1;
	struct cxlflash_cfg *cfg = shost_priv(scp->device->host);
	struct afu_cmd *cmd = sc_to_afucz(scp);
	struct device *dev = &cfg->dev->dev;
	ulong lock_flags;
	int rc = 0;
	ulong to;

	/* When Task Management Function is active do not send another */
	spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
	if (cfg->tmf_active)
		wait_event_interruptible_lock_irq(cfg->tmf_waitq,
						  !cfg->tmf_active,
						  cfg->tmf_slock);
	cfg->tmf_active = true;
	spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);

	cmd->scp = scp;
	cmd->parent = afu;
	cmd->cmd_tmf = true;

	cmd->rcb.ctx_id = afu->ctx_hndl;
	cmd->rcb.msi = SISL_MSI_RRQ_UPDATED;
	cmd->rcb.port_sel = port_sel;
	cmd->rcb.lun_id = lun_to_lunid(scp->device->lun);
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
		cfg->tmf_active = false;
		dev_err(dev, "%s: TMF timed out\n", __func__);
		rc = -1;
	}
	spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);
out:
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
	struct afu_cmd *cmd = sc_to_afucz(scp);
	struct scatterlist *sg = scsi_sglist(scp);
	u32 port_sel = scp->device->channel + 1;
	u16 req_flags = SISL_REQ_FLAGS_SUP_UNDERRUN;
	ulong lock_flags;
	int nseg = 0;
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
	case STATE_RESET:
		dev_dbg_ratelimited(dev, "%s: device is in reset\n", __func__);
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	case STATE_FAILTERM:
		dev_dbg_ratelimited(dev, "%s: device has failed\n", __func__);
		scp->result = (DID_NO_CONNECT << 16);
		scp->scsi_done(scp);
		rc = 0;
		goto out;
	default:
		break;
	}

	if (likely(sg)) {
		nseg = scsi_dma_map(scp);
		if (unlikely(nseg < 0)) {
			dev_err(dev, "%s: Fail DMA map\n", __func__);
			rc = SCSI_MLQUEUE_HOST_BUSY;
			goto out;
		}

		cmd->rcb.data_len = sg_dma_len(sg);
		cmd->rcb.data_ea = sg_dma_address(sg);
	}

	cmd->scp = scp;
	cmd->parent = afu;

	cmd->rcb.ctx_id = afu->ctx_hndl;
	cmd->rcb.msi = SISL_MSI_RRQ_UPDATED;
	cmd->rcb.port_sel = port_sel;
	cmd->rcb.lun_id = lun_to_lunid(scp->device->lun);

	if (scp->sc_data_direction == DMA_TO_DEVICE)
		req_flags |= SISL_REQ_FLAGS_HOST_WRITE;

	cmd->rcb.req_flags = req_flags;
	memcpy(cmd->rcb.cdb, scp->cmnd, sizeof(cmd->rcb.cdb));

	rc = afu->send_cmd(afu, cmd);
	if (unlikely(rc))
		scsi_dma_unmap(scp);
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
 * stop_afu() - stops the AFU command timers and unmaps the MMIO space
 * @cfg:	Internal structure associated with the host.
 *
 * Safe to call with AFU in a partially allocated/initialized state.
 *
 * Cancels scheduled worker threads, waits for any active internal AFU
 * commands to timeout and then unmaps the MMIO space.
 */
static void stop_afu(struct cxlflash_cfg *cfg)
{
	struct afu *afu = cfg->afu;

	cancel_work_sync(&cfg->work_q);

	if (likely(afu)) {
		while (atomic_read(&afu->cmds_active))
			ssleep(1);
		if (likely(afu->afu_map)) {
			cxl_psa_unmap((void __iomem *)afu->afu_map);
			afu->afu_map = NULL;
		}
	}
}

/**
 * term_intr() - disables all AFU interrupts
 * @cfg:	Internal structure associated with the host.
 * @level:	Depth of allocation, where to begin waterfall tear down.
 *
 * Safe to call with AFU/MC in partially allocated/initialized state.
 */
static void term_intr(struct cxlflash_cfg *cfg, enum undo_level level)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;

	if (!afu || !cfg->mcctx) {
		dev_err(dev, "%s: returning with NULL afu or MC\n", __func__);
		return;
	}

	switch (level) {
	case UNMAP_THREE:
		cxl_unmap_afu_irq(cfg->mcctx, 3, afu);
	case UNMAP_TWO:
		cxl_unmap_afu_irq(cfg->mcctx, 2, afu);
	case UNMAP_ONE:
		cxl_unmap_afu_irq(cfg->mcctx, 1, afu);
	case FREE_IRQ:
		cxl_free_afu_irqs(cfg->mcctx);
		/* fall through */
	case UNDO_NOOP:
		/* No action required */
		break;
	}
}

/**
 * term_mc() - terminates the master context
 * @cfg:	Internal structure associated with the host.
 * @level:	Depth of allocation, where to begin waterfall tear down.
 *
 * Safe to call with AFU/MC in partially allocated/initialized state.
 */
static void term_mc(struct cxlflash_cfg *cfg)
{
	int rc = 0;
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;

	if (!afu || !cfg->mcctx) {
		dev_err(dev, "%s: returning with NULL afu or MC\n", __func__);
		return;
	}

	rc = cxl_stop_context(cfg->mcctx);
	WARN_ON(rc);
	cfg->mcctx = NULL;
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

	/*
	 * Tear down is carefully orchestrated to ensure
	 * no interrupts can come in when the problem state
	 * area is unmapped.
	 *
	 * 1) Disable all AFU interrupts
	 * 2) Unmap the problem state area
	 * 3) Stop the master context
	 */
	term_intr(cfg, UNMAP_THREE);
	if (cfg->afu)
		stop_afu(cfg);

	term_mc(cfg);

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
	struct sisl_global_map __iomem *global;
	struct dev_dependent_vals *ddv;
	u64 reg, status;
	int i, retry_cnt = 0;

	ddv = (struct dev_dependent_vals *)cfg->dev_id->driver_data;
	if (!(ddv->flags & CXLFLASH_NOTIFY_SHUTDOWN))
		return;

	if (!afu || !afu->afu_map) {
		dev_dbg(dev, "%s: Problem state area not mapped\n", __func__);
		return;
	}

	global = &afu->afu_map->global;

	/* Notify AFU */
	for (i = 0; i < NUM_FC_PORTS; i++) {
		reg = readq_be(&global->fc_regs[i][FC_CONFIG2 / 8]);
		reg |= SISL_FC_SHUTDOWN_NORMAL;
		writeq_be(reg, &global->fc_regs[i][FC_CONFIG2 / 8]);
	}

	if (!wait)
		return;

	/* Wait up to 1.5 seconds for shutdown processing to complete */
	for (i = 0; i < NUM_FC_PORTS; i++) {
		retry_cnt = 0;
		while (true) {
			status = readq_be(&global->fc_regs[i][FC_STATUS / 8]);
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
 * cxlflash_remove() - PCI entry point to tear down host
 * @pdev:	PCI device associated with the host.
 *
 * Safe to use as a cleanup in partially allocated/initialized state.
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

	/* If a Task Management Function is active, wait for it to complete
	 * before continuing with remove.
	 */
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
	case INIT_STATE_SCSI:
		cxlflash_term_local_luns(cfg);
		scsi_remove_host(cfg->host);
		/* fall through */
	case INIT_STATE_AFU:
		term_afu(cfg);
	case INIT_STATE_PCI:
		pci_disable_device(pdev);
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

/*
 * Asynchronous interrupt information table
 */
static const struct asyc_intr_info ainfo[] = {
	{SISL_ASTATUS_FC0_OTHER, "other error", 0, CLR_FC_ERROR | LINK_RESET},
	{SISL_ASTATUS_FC0_LOGO, "target initiated LOGO", 0, 0},
	{SISL_ASTATUS_FC0_CRC_T, "CRC threshold exceeded", 0, LINK_RESET},
	{SISL_ASTATUS_FC0_LOGI_R, "login timed out, retrying", 0, LINK_RESET},
	{SISL_ASTATUS_FC0_LOGI_F, "login failed", 0, CLR_FC_ERROR},
	{SISL_ASTATUS_FC0_LOGI_S, "login succeeded", 0, SCAN_HOST},
	{SISL_ASTATUS_FC0_LINK_DN, "link down", 0, 0},
	{SISL_ASTATUS_FC0_LINK_UP, "link up", 0, 0},
	{SISL_ASTATUS_FC1_OTHER, "other error", 1, CLR_FC_ERROR | LINK_RESET},
	{SISL_ASTATUS_FC1_LOGO, "target initiated LOGO", 1, 0},
	{SISL_ASTATUS_FC1_CRC_T, "CRC threshold exceeded", 1, LINK_RESET},
	{SISL_ASTATUS_FC1_LOGI_R, "login timed out, retrying", 1, LINK_RESET},
	{SISL_ASTATUS_FC1_LOGI_F, "login failed", 1, CLR_FC_ERROR},
	{SISL_ASTATUS_FC1_LOGI_S, "login succeeded", 1, SCAN_HOST},
	{SISL_ASTATUS_FC1_LINK_DN, "link down", 1, 0},
	{SISL_ASTATUS_FC1_LINK_UP, "link up", 1, 0},
	{0x0, "", 0, 0}		/* terminator */
};

/**
 * find_ainfo() - locates and returns asynchronous interrupt information
 * @status:	Status code set by AFU on error.
 *
 * Return: The located information or NULL when the status code is invalid.
 */
static const struct asyc_intr_info *find_ainfo(u64 status)
{
	const struct asyc_intr_info *info;

	for (info = &ainfo[0]; info->status; info++)
		if (info->status == status)
			return info;

	return NULL;
}

/**
 * afu_err_intr_init() - clears and initializes the AFU for error interrupts
 * @afu:	AFU associated with the host.
 */
static void afu_err_intr_init(struct afu *afu)
{
	int i;
	u64 reg;

	/* global async interrupts: AFU clears afu_ctrl on context exit
	 * if async interrupts were sent to that context. This prevents
	 * the AFU form sending further async interrupts when
	 * there is
	 * nobody to receive them.
	 */

	/* mask all */
	writeq_be(-1ULL, &afu->afu_map->global.regs.aintr_mask);
	/* set LISN# to send and point to master context */
	reg = ((u64) (((afu->ctx_hndl << 8) | SISL_MSI_ASYNC_ERROR)) << 40);

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
	reg = readq_be(&afu->afu_map->global.fc_regs[0][FC_CONFIG2 / 8]);
	reg &= SISL_FC_INTERNAL_MASK;
	if (afu->internal_lun)
		reg |= ((u64)(afu->internal_lun - 1) << SISL_FC_INTERNAL_SHIFT);
	writeq_be(reg, &afu->afu_map->global.fc_regs[0][FC_CONFIG2 / 8]);

	/* now clear FC errors */
	for (i = 0; i < NUM_FC_PORTS; i++) {
		writeq_be(0xFFFFFFFFU,
			  &afu->afu_map->global.fc_regs[i][FC_ERROR / 8]);
		writeq_be(0, &afu->afu_map->global.fc_regs[i][FC_ERRCAP / 8]);
	}

	/* sync interrupts for master's IOARRIN write */
	/* note that unlike asyncs, there can be no pending sync interrupts */
	/* at this time (this is a fresh context and master has not written */
	/* IOARRIN yet), so there is nothing to clear. */

	/* set LISN#, it is always sent to the context that wrote IOARRIN */
	writeq_be(SISL_MSI_SYNC_ERROR, &afu->host_map->ctx_ctrl);
	writeq_be(SISL_ISTATUS_MASK, &afu->host_map->intr_mask);
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
	struct afu *afu = (struct afu *)data;
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	u64 reg;
	u64 reg_unmasked;

	reg = readq_be(&afu->host_map->intr_status);
	reg_unmasked = (reg & SISL_ISTATUS_UNMASK);

	if (reg_unmasked == 0UL) {
		dev_err(dev, "%s: spurious interrupt, intr_status=%016llx\n",
			__func__, reg);
		goto cxlflash_sync_err_irq_exit;
	}

	dev_err(dev, "%s: unexpected interrupt, intr_status=%016llx\n",
		__func__, reg);

	writeq_be(reg_unmasked, &afu->host_map->intr_clear);

cxlflash_sync_err_irq_exit:
	return IRQ_HANDLED;
}

/**
 * cxlflash_rrq_irq() - interrupt handler for read-response queue (normal path)
 * @irq:	Interrupt number.
 * @data:	Private data provided at interrupt registration, the AFU.
 *
 * Return: Always return IRQ_HANDLED.
 */
static irqreturn_t cxlflash_rrq_irq(int irq, void *data)
{
	struct afu *afu = (struct afu *)data;
	struct afu_cmd *cmd;
	struct sisl_ioasa *ioasa;
	struct sisl_ioarcb *ioarcb;
	bool toggle = afu->toggle;
	u64 entry,
	    *hrrq_start = afu->hrrq_start,
	    *hrrq_end = afu->hrrq_end,
	    *hrrq_curr = afu->hrrq_curr;

	/* Process however many RRQ entries that are ready */
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

		cmd_complete(cmd);

		/* Advance to next entry or wrap and flip the toggle bit */
		if (hrrq_curr < hrrq_end)
			hrrq_curr++;
		else {
			hrrq_curr = hrrq_start;
			toggle ^= SISL_RESP_HANDLE_T_BIT;
		}

		atomic_inc(&afu->hsq_credits);
	}

	afu->hrrq_curr = hrrq_curr;
	afu->toggle = toggle;

	return IRQ_HANDLED;
}

/**
 * cxlflash_async_err_irq() - interrupt handler for asynchronous errors
 * @irq:	Interrupt number.
 * @data:	Private data provided at interrupt registration, the AFU.
 *
 * Return: Always return IRQ_HANDLED.
 */
static irqreturn_t cxlflash_async_err_irq(int irq, void *data)
{
	struct afu *afu = (struct afu *)data;
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	u64 reg_unmasked;
	const struct asyc_intr_info *info;
	struct sisl_global_map __iomem *global = &afu->afu_map->global;
	u64 reg;
	u8 port;
	int i;

	reg = readq_be(&global->regs.aintr_status);
	reg_unmasked = (reg & SISL_ASTATUS_UNMASK);

	if (reg_unmasked == 0) {
		dev_err(dev, "%s: spurious interrupt, aintr_status=%016llx\n",
			__func__, reg);
		goto out;
	}

	/* FYI, it is 'okay' to clear AFU status before FC_ERROR */
	writeq_be(reg_unmasked, &global->regs.aintr_clear);

	/* Check each bit that is on */
	for (i = 0; reg_unmasked; i++, reg_unmasked = (reg_unmasked >> 1)) {
		info = find_ainfo(1ULL << i);
		if (((reg_unmasked & 0x1) == 0) || !info)
			continue;

		port = info->port;

		dev_err(dev, "%s: FC Port %d -> %s, fc_status=%016llx\n",
			__func__, port, info->desc,
		       readq_be(&global->fc_regs[port][FC_STATUS / 8]));

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
			reg = readq_be(&global->fc_regs[port][FC_ERROR / 8]);

			/*
			 * Since all errors are unmasked, FC_ERROR and FC_ERRCAP
			 * should be the same and tracing one is sufficient.
			 */

			dev_err(dev, "%s: fc %d: clearing fc_error=%016llx\n",
				__func__, port, reg);

			writeq_be(reg, &global->fc_regs[port][FC_ERROR / 8]);
			writeq_be(0, &global->fc_regs[port][FC_ERRCAP / 8]);
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
 * start_context() - starts the master context
 * @cfg:	Internal structure associated with the host.
 *
 * Return: A success or failure value from CXL services.
 */
static int start_context(struct cxlflash_cfg *cfg)
{
	struct device *dev = &cfg->dev->dev;
	int rc = 0;

	rc = cxl_start_context(cfg->mcctx,
			       cfg->afu->work.work_element_descriptor,
			       NULL);

	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * read_vpd() - obtains the WWPNs from VPD
 * @cfg:	Internal structure associated with the host.
 * @wwpn:	Array of size NUM_FC_PORTS to pass back WWPNs
 *
 * Return: 0 on success, -errno on failure
 */
static int read_vpd(struct cxlflash_cfg *cfg, u64 wwpn[])
{
	struct device *dev = &cfg->dev->dev;
	struct pci_dev *pdev = cfg->dev;
	int rc = 0;
	int ro_start, ro_size, i, j, k;
	ssize_t vpd_size;
	char vpd_data[CXLFLASH_VPD_LEN];
	char tmp_buf[WWPN_BUF_LEN] = { 0 };
	char *wwpn_vpd_tags[NUM_FC_PORTS] = { "V5", "V6" };

	/* Get the VPD data from the device */
	vpd_size = cxl_read_adapter_vpd(pdev, vpd_data, sizeof(vpd_data));
	if (unlikely(vpd_size <= 0)) {
		dev_err(dev, "%s: Unable to read VPD (size = %ld)\n",
			__func__, vpd_size);
		rc = -ENODEV;
		goto out;
	}

	/* Get the read only section offset */
	ro_start = pci_vpd_find_tag(vpd_data, 0, vpd_size,
				    PCI_VPD_LRDT_RO_DATA);
	if (unlikely(ro_start < 0)) {
		dev_err(dev, "%s: VPD Read-only data not found\n", __func__);
		rc = -ENODEV;
		goto out;
	}

	/* Get the read only section size, cap when extends beyond read VPD */
	ro_size = pci_vpd_lrdt_size(&vpd_data[ro_start]);
	j = ro_size;
	i = ro_start + PCI_VPD_LRDT_TAG_SIZE;
	if (unlikely((i + j) > vpd_size)) {
		dev_dbg(dev, "%s: Might need to read more VPD (%d > %ld)\n",
			__func__, (i + j), vpd_size);
		ro_size = vpd_size - i;
	}

	/*
	 * Find the offset of the WWPN tag within the read only
	 * VPD data and validate the found field (partials are
	 * no good to us). Convert the ASCII data to an integer
	 * value. Note that we must copy to a temporary buffer
	 * because the conversion service requires that the ASCII
	 * string be terminated.
	 */
	for (k = 0; k < NUM_FC_PORTS; k++) {
		j = ro_size;
		i = ro_start + PCI_VPD_LRDT_TAG_SIZE;

		i = pci_vpd_find_info_keyword(vpd_data, i, j, wwpn_vpd_tags[k]);
		if (unlikely(i < 0)) {
			dev_err(dev, "%s: Port %d WWPN not found in VPD\n",
				__func__, k);
			rc = -ENODEV;
			goto out;
		}

		j = pci_vpd_info_field_size(&vpd_data[i]);
		i += PCI_VPD_INFO_FLD_HDR_SIZE;
		if (unlikely((i + j > vpd_size) || (j != WWPN_LEN))) {
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
	int i;

	for (i = 0; i < MAX_CONTEXT; i++) {
		ctrl_map = &afu->afu_map->ctrls[i].ctrl;
		/* Disrupt any clients that could be running */
		/* e.g. clients that survived a master restart */
		writeq_be(0, &ctrl_map->rht_start);
		writeq_be(0, &ctrl_map->rht_cnt_id);
		writeq_be(0, &ctrl_map->ctx_cap);
	}

	/* Copy frequently used fields into afu */
	afu->ctx_hndl = (u16) cxl_process_element(cfg->mcctx);
	afu->host_map = &afu->afu_map->hosts[afu->ctx_hndl].host;
	afu->ctrl_map = &afu->afu_map->ctrls[afu->ctx_hndl].ctrl;

	/* Program the Endian Control for the master context */
	writeq_be(SISL_ENDIAN_CTRL, &afu->host_map->endian_ctrl);
}

/**
 * init_global() - initialize AFU global registers
 * @cfg:	Internal structure associated with the host.
 */
static int init_global(struct cxlflash_cfg *cfg)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	u64 wwpn[NUM_FC_PORTS];	/* wwpn of AFU ports */
	int i = 0, num_ports = 0;
	int rc = 0;
	u64 reg;

	rc = read_vpd(cfg, &wwpn[0]);
	if (rc) {
		dev_err(dev, "%s: could not read vpd rc=%d\n", __func__, rc);
		goto out;
	}

	dev_dbg(dev, "%s: wwpn0=%016llx wwpn1=%016llx\n",
		__func__, wwpn[0], wwpn[1]);

	/* Set up RRQ and SQ in AFU for master issued cmds */
	writeq_be((u64) afu->hrrq_start, &afu->host_map->rrq_start);
	writeq_be((u64) afu->hrrq_end, &afu->host_map->rrq_end);

	if (afu_is_sq_cmd_mode(afu)) {
		writeq_be((u64)afu->hsq_start, &afu->host_map->sq_start);
		writeq_be((u64)afu->hsq_end, &afu->host_map->sq_end);
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
		num_ports = NUM_FC_PORTS - 1;
	} else {
		writeq_be(BOTH_PORTS, &afu->afu_map->global.regs.afu_port_sel);
		num_ports = NUM_FC_PORTS;
	}

	for (i = 0; i < num_ports; i++) {
		/* Unmask all errors (but they are still masked at AFU) */
		writeq_be(0, &afu->afu_map->global.fc_regs[i][FC_ERRMSK / 8]);
		/* Clear CRC error cnt & set a threshold */
		(void)readq_be(&afu->afu_map->global.
			       fc_regs[i][FC_CNT_CRCERR / 8]);
		writeq_be(MC_CRC_THRESH, &afu->afu_map->global.fc_regs[i]
			  [FC_CRC_THRESH / 8]);

		/* Set WWPNs. If already programmed, wwpn[i] is 0 */
		if (wwpn[i] != 0)
			afu_set_wwpn(afu, i,
				     &afu->afu_map->global.fc_regs[i][0],
				     wwpn[i]);
		/* Programming WWPN back to back causes additional
		 * offline/online transitions and a PLOGI
		 */
		msleep(100);
	}

	/* Set up master's own CTX_CAP to allow real mode, host translation */
	/* tables, afu cmds and read/write GSCSI cmds. */
	/* First, unlock ctx_cap write by reading mbox */
	(void)readq_be(&afu->ctrl_map->mbox_r);	/* unlock ctx_cap */
	writeq_be((SISL_CTX_CAP_REAL_MODE | SISL_CTX_CAP_HOST_XLATE |
		   SISL_CTX_CAP_READ_CMD | SISL_CTX_CAP_WRITE_CMD |
		   SISL_CTX_CAP_AFU_CMD | SISL_CTX_CAP_GSCSI_CMD),
		  &afu->ctrl_map->ctx_cap);
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
	int rc = 0;

	init_pcr(cfg);

	/* After an AFU reset, RRQ entries are stale, clear them */
	memset(&afu->rrq_entry, 0, sizeof(afu->rrq_entry));

	/* Initialize RRQ pointers */
	afu->hrrq_start = &afu->rrq_entry[0];
	afu->hrrq_end = &afu->rrq_entry[NUM_RRQ_ENTRY - 1];
	afu->hrrq_curr = afu->hrrq_start;
	afu->toggle = 1;

	/* Initialize SQ */
	if (afu_is_sq_cmd_mode(afu)) {
		memset(&afu->sq, 0, sizeof(afu->sq));
		afu->hsq_start = &afu->sq[0];
		afu->hsq_end = &afu->sq[NUM_SQ_ENTRY - 1];
		afu->hsq_curr = afu->hsq_start;

		spin_lock_init(&afu->hsq_slock);
		atomic_set(&afu->hsq_credits, NUM_SQ_ENTRY - 1);
	}

	rc = init_global(cfg);

	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * init_intr() - setup interrupt handlers for the master context
 * @cfg:	Internal structure associated with the host.
 *
 * Return: 0 on success, -errno on failure
 */
static enum undo_level init_intr(struct cxlflash_cfg *cfg,
				 struct cxl_context *ctx)
{
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	int rc = 0;
	enum undo_level level = UNDO_NOOP;

	rc = cxl_allocate_afu_irqs(ctx, 3);
	if (unlikely(rc)) {
		dev_err(dev, "%s: allocate_afu_irqs failed rc=%d\n",
			__func__, rc);
		level = UNDO_NOOP;
		goto out;
	}

	rc = cxl_map_afu_irq(ctx, 1, cxlflash_sync_err_irq, afu,
			     "SISL_MSI_SYNC_ERROR");
	if (unlikely(rc <= 0)) {
		dev_err(dev, "%s: SISL_MSI_SYNC_ERROR map failed\n", __func__);
		level = FREE_IRQ;
		goto out;
	}

	rc = cxl_map_afu_irq(ctx, 2, cxlflash_rrq_irq, afu,
			     "SISL_MSI_RRQ_UPDATED");
	if (unlikely(rc <= 0)) {
		dev_err(dev, "%s: SISL_MSI_RRQ_UPDATED map failed\n", __func__);
		level = UNMAP_ONE;
		goto out;
	}

	rc = cxl_map_afu_irq(ctx, 3, cxlflash_async_err_irq, afu,
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
 *
 * Return: 0 on success, -errno on failure
 */
static int init_mc(struct cxlflash_cfg *cfg)
{
	struct cxl_context *ctx;
	struct device *dev = &cfg->dev->dev;
	int rc = 0;
	enum undo_level level;

	ctx = cxl_get_context(cfg->dev);
	if (unlikely(!ctx)) {
		rc = -ENOMEM;
		goto ret;
	}
	cfg->mcctx = ctx;

	/* Set it up as a master with the CXL */
	cxl_set_master(ctx);

	/* During initialization reset the AFU to start from a clean slate */
	rc = cxl_afu_reset(cfg->mcctx);
	if (unlikely(rc)) {
		dev_err(dev, "%s: AFU reset failed rc=%d\n", __func__, rc);
		goto ret;
	}

	level = init_intr(cfg, ctx);
	if (unlikely(level)) {
		dev_err(dev, "%s: interrupt init failed rc=%d\n", __func__, rc);
		goto out;
	}

	/* This performs the equivalent of the CXL_IOCTL_START_WORK.
	 * The CXL_IOCTL_GET_PROCESS_ELEMENT is implicit in the process
	 * element (pe) that is embedded in the context (ctx)
	 */
	rc = start_context(cfg);
	if (unlikely(rc)) {
		dev_err(dev, "%s: start context failed rc=%d\n", __func__, rc);
		level = UNMAP_THREE;
		goto out;
	}
ret:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
out:
	term_intr(cfg, level);
	goto ret;
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

	cxl_perst_reloads_same_image(cfg->cxl_afu, true);

	rc = init_mc(cfg);
	if (rc) {
		dev_err(dev, "%s: init_mc failed rc=%d\n",
			__func__, rc);
		goto out;
	}

	/* Map the entire MMIO space of the AFU */
	afu->afu_map = cxl_psa_map(cfg->mcctx);
	if (!afu->afu_map) {
		dev_err(dev, "%s: cxl_psa_map failed\n", __func__);
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

	rc = start_afu(cfg);
	if (rc) {
		dev_err(dev, "%s: start_afu failed, rc=%d\n", __func__, rc);
		goto err1;
	}

	afu_err_intr_init(cfg->afu);
	spin_lock_init(&afu->rrin_slock);
	afu->room = readq_be(&afu->host_map->cmd_room);

	/* Restore the LUN mappings */
	cxlflash_restore_luntable(cfg);
out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;

err1:
	term_intr(cfg, UNMAP_THREE);
	term_mc(cfg);
	goto out;
}

/**
 * cxlflash_afu_sync() - builds and sends an AFU sync command
 * @afu:	AFU associated with the host.
 * @ctx_hndl_u:	Identifies context requesting sync.
 * @res_hndl_u:	Identifies resource requesting sync.
 * @mode:	Type of sync to issue (lightweight, heavyweight, global).
 *
 * The AFU can only take 1 sync command at a time. This routine enforces this
 * limitation by using a mutex to provide exclusive access to the AFU during
 * the sync. This design point requires calling threads to not be on interrupt
 * context due to the possibility of sleeping during concurrent sync operations.
 *
 * AFU sync operations are only necessary and allowed when the device is
 * operating normally. When not operating normally, sync requests can occur as
 * part of cleaning up resources associated with an adapter prior to removal.
 * In this scenario, these requests are simply ignored (safe due to the AFU
 * going away).
 *
 * Return:
 *	0 on success
 *	-1 on failure
 */
int cxlflash_afu_sync(struct afu *afu, ctx_hndl_t ctx_hndl_u,
		      res_hndl_t res_hndl_u, u8 mode)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	struct afu_cmd *cmd = NULL;
	char *buf = NULL;
	int rc = 0;
	static DEFINE_MUTEX(sync_active);

	if (cfg->state != STATE_NORMAL) {
		dev_dbg(dev, "%s: Sync not required state=%u\n",
			__func__, cfg->state);
		return 0;
	}

	mutex_lock(&sync_active);
	atomic_inc(&afu->cmds_active);
	buf = kzalloc(sizeof(*cmd) + __alignof__(*cmd) - 1, GFP_KERNEL);
	if (unlikely(!buf)) {
		dev_err(dev, "%s: no memory for command\n", __func__);
		rc = -1;
		goto out;
	}

	cmd = (struct afu_cmd *)PTR_ALIGN(buf, __alignof__(*cmd));
	init_completion(&cmd->cevent);
	cmd->parent = afu;

	dev_dbg(dev, "%s: afu=%p cmd=%p %d\n", __func__, afu, cmd, ctx_hndl_u);

	cmd->rcb.req_flags = SISL_REQ_FLAGS_AFU_CMD;
	cmd->rcb.ctx_id = afu->ctx_hndl;
	cmd->rcb.msi = SISL_MSI_RRQ_UPDATED;
	cmd->rcb.timeout = MC_AFU_SYNC_TIMEOUT;

	cmd->rcb.cdb[0] = 0xC0;	/* AFU Sync */
	cmd->rcb.cdb[1] = mode;

	/* The cdb is aligned, no unaligned accessors required */
	*((__be16 *)&cmd->rcb.cdb[2]) = cpu_to_be16(ctx_hndl_u);
	*((__be32 *)&cmd->rcb.cdb[4]) = cpu_to_be32(res_hndl_u);

	rc = afu->send_cmd(afu, cmd);
	if (unlikely(rc))
		goto out;

	rc = wait_resp(afu, cmd);
	if (unlikely(rc))
		rc = -1;
out:
	atomic_dec(&afu->cmds_active);
	mutex_unlock(&sync_active);
	kfree(buf);
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;
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
	struct Scsi_Host *host = scp->device->host;
	struct cxlflash_cfg *cfg = shost_priv(host);
	struct device *dev = &cfg->dev->dev;
	struct afu *afu = cfg->afu;
	int rcr = 0;

	dev_dbg(dev, "%s: (scp=%p) %d/%d/%d/%llu "
		"cdb=(%08x-%08x-%08x-%08x)\n", __func__, scp, host->host_no,
		scp->device->channel, scp->device->id, scp->device->lun,
		get_unaligned_be32(&((u32 *)scp->cmnd)[0]),
		get_unaligned_be32(&((u32 *)scp->cmnd)[1]),
		get_unaligned_be32(&((u32 *)scp->cmnd)[2]),
		get_unaligned_be32(&((u32 *)scp->cmnd)[3]));

retry:
	switch (cfg->state) {
	case STATE_NORMAL:
		rcr = send_tmf(afu, scp, TMF_LUN_RESET);
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

	dev_dbg(dev, "%s: (scp=%p) %d/%d/%d/%llu "
		"cdb=(%08x-%08x-%08x-%08x)\n", __func__, scp, host->host_no,
		scp->device->channel, scp->device->id, scp->device->lun,
		get_unaligned_be32(&((u32 *)scp->cmnd)[0]),
		get_unaligned_be32(&((u32 *)scp->cmnd)[1]),
		get_unaligned_be32(&((u32 *)scp->cmnd)[2]),
		get_unaligned_be32(&((u32 *)scp->cmnd)[3]));

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
		/* fall through */
	case STATE_RESET:
		wait_event(cfg->reset_waitq, cfg->state != STATE_RESET);
		if (cfg->state == STATE_NORMAL)
			break;
		/* fall through */
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
 * @afu:	AFU owning the specified port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t cxlflash_show_port_status(u32 port, struct afu *afu, char *buf)
{
	char *disp_status;
	u64 status;
	__be64 __iomem *fc_regs;

	if (port >= NUM_FC_PORTS)
		return 0;

	fc_regs = &afu->afu_map->global.fc_regs[port][0];
	status = readq_be(&fc_regs[FC_MTIP_STATUS / 8]);
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
	struct afu *afu = cfg->afu;

	return cxlflash_show_port_status(0, afu, buf);
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
	struct afu *afu = cfg->afu;

	return cxlflash_show_port_status(1, afu, buf);
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
		 * channel number 0, else there will be 2 (default).
		 */
		if (afu->internal_lun)
			shost->max_channel = 0;
		else
			shost->max_channel = NUM_FC_PORTS - 1;

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
	return scnprintf(buf, PAGE_SIZE, "%u\n", DK_CXLFLASH_VERSION_0);
}

/**
 * cxlflash_show_port_lun_table() - queries and presents the port LUN table
 * @port:	Desired port for status reporting.
 * @afu:	AFU owning the specified port.
 * @buf:	Buffer of length PAGE_SIZE to report back port status in ASCII.
 *
 * Return: The size of the ASCII string returned in @buf.
 */
static ssize_t cxlflash_show_port_lun_table(u32 port,
					    struct afu *afu,
					    char *buf)
{
	int i;
	ssize_t bytes = 0;
	__be64 __iomem *fc_port;

	if (port >= NUM_FC_PORTS)
		return 0;

	fc_port = &afu->afu_map->global.fc_port[port][0];

	for (i = 0; i < CXLFLASH_NUM_VLUNS; i++)
		bytes += scnprintf(buf + bytes, PAGE_SIZE - bytes,
				   "%03d: %016llx\n", i, readq_be(&fc_port[i]));
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
	struct afu *afu = cfg->afu;

	return cxlflash_show_port_lun_table(0, afu, buf);
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
	struct afu *afu = cfg->afu;

	return cxlflash_show_port_lun_table(1, afu, buf);
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
static DEVICE_ATTR_RW(lun_mode);
static DEVICE_ATTR_RO(ioctl_version);
static DEVICE_ATTR_RO(port0_lun_table);
static DEVICE_ATTR_RO(port1_lun_table);

static struct device_attribute *cxlflash_host_attrs[] = {
	&dev_attr_port0,
	&dev_attr_port1,
	&dev_attr_lun_mode,
	&dev_attr_ioctl_version,
	&dev_attr_port0_lun_table,
	&dev_attr_port1_lun_table,
	NULL
};

/*
 * Device attributes
 */
static DEVICE_ATTR_RO(mode);

static struct device_attribute *cxlflash_dev_attrs[] = {
	&dev_attr_mode,
	NULL
};

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
	.eh_device_reset_handler = cxlflash_eh_device_reset_handler,
	.eh_host_reset_handler = cxlflash_eh_host_reset_handler,
	.change_queue_depth = cxlflash_change_queue_depth,
	.cmd_per_lun = CXLFLASH_MAX_CMDS_PER_LUN,
	.can_queue = CXLFLASH_MAX_CMDS,
	.cmd_size = sizeof(struct afu_cmd) + __alignof__(struct afu_cmd) - 1,
	.this_id = -1,
	.sg_tablesize = 1,	/* No scatter gather support */
	.max_sectors = CXLFLASH_MAX_SECTORS,
	.use_clustering = ENABLE_CLUSTERING,
	.shost_attrs = cxlflash_host_attrs,
	.sdev_attrs = cxlflash_dev_attrs,
};

/*
 * Device dependent values
 */
static struct dev_dependent_vals dev_corsa_vals = { CXLFLASH_MAX_SECTORS,
					0ULL };
static struct dev_dependent_vals dev_flash_gt_vals = { CXLFLASH_MAX_SECTORS,
					CXLFLASH_NOTIFY_SHUTDOWN };
static struct dev_dependent_vals dev_briard_vals = { CXLFLASH_MAX_SECTORS,
					CXLFLASH_NOTIFY_SHUTDOWN };

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
			afu_link_reset(afu, port,
				       &afu->afu_map->global.fc_regs[port][0]);
			spin_lock_irqsave(cfg->host->host_lock, lock_flags);
		}

		cfg->lr_state = LINK_RESET_COMPLETE;
	}

	spin_unlock_irqrestore(cfg->host->host_lock, lock_flags);

	if (atomic_dec_if_positive(&cfg->scan_host_needed) >= 0)
		scsi_scan_host(cfg->host);
}

/**
 * cxlflash_probe() - PCI entry point to add host
 * @pdev:	PCI device associated with the host.
 * @dev_id:	PCI device id associated with device.
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
	host->max_channel = NUM_FC_PORTS - 1;
	host->unique_id = host->host_no;
	host->max_cmd_len = CXLFLASH_MAX_CDB_LEN;

	cfg = shost_priv(host);
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

	/*
	 * The promoted LUNs move to the top of the LUN table. The rest stay
	 * on the bottom half. The bottom half grows from the end
	 * (index = 255), whereas the top half grows from the beginning
	 * (index = 0).
	 */
	cfg->promote_lun_index  = 0;
	cfg->last_lun_index[0] = CXLFLASH_NUM_VLUNS/2 - 1;
	cfg->last_lun_index[1] = CXLFLASH_NUM_VLUNS/2 - 1;

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

	cfg->cxl_afu = cxl_pci_to_afu(pdev);

	rc = init_pci(cfg);
	if (rc) {
		dev_err(dev, "%s: init_pci failed rc=%d\n", __func__, rc);
		goto out_remove;
	}
	cfg->init_state = INIT_STATE_PCI;

	rc = init_afu(cfg);
	if (rc) {
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

out:
	dev_dbg(dev, "%s: returning rc=%d\n", __func__, rc);
	return rc;

out_remove:
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
		wait_event(cfg->reset_waitq, cfg->state != STATE_RESET);
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
	cxlflash_list_init();

	return pci_register_driver(&cxlflash_driver);
}

/**
 * exit_cxlflash() - module exit point
 */
static void __exit exit_cxlflash(void)
{
	cxlflash_term_global_luns();
	cxlflash_free_errpage();

	pci_unregister_driver(&cxlflash_driver);
}

module_init(init_cxlflash);
module_exit(exit_cxlflash);
