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
 * cmd_checkout() - checks out an AFU command
 * @afu:	AFU to checkout from.
 *
 * Commands are checked out in a round-robin fashion. Note that since
 * the command pool is larger than the hardware queue, the majority of
 * times we will only loop once or twice before getting a command. The
 * buffer and CDB within the command are initialized (zeroed) prior to
 * returning.
 *
 * Return: The checked out command or NULL when command pool is empty.
 */
static struct afu_cmd *cmd_checkout(struct afu *afu)
{
	int k, dec = CXLFLASH_NUM_CMDS;
	struct afu_cmd *cmd;

	while (dec--) {
		k = (afu->cmd_couts++ & (CXLFLASH_NUM_CMDS - 1));

		cmd = &afu->cmd[k];

		if (!atomic_dec_if_positive(&cmd->free)) {
			pr_devel("%s: returning found index=%d cmd=%p\n",
				 __func__, cmd->slot, cmd);
			memset(cmd->buf, 0, CMD_BUFSIZE);
			memset(cmd->rcb.cdb, 0, sizeof(cmd->rcb.cdb));
			return cmd;
		}
	}

	return NULL;
}

/**
 * cmd_checkin() - checks in an AFU command
 * @cmd:	AFU command to checkin.
 *
 * Safe to pass commands that have already been checked in. Several
 * internal tracking fields are reset as part of the checkin. Note
 * that these are intentionally reset prior to toggling the free bit
 * to avoid clobbering values in the event that the command is checked
 * out right away.
 */
static void cmd_checkin(struct afu_cmd *cmd)
{
	cmd->rcb.scp = NULL;
	cmd->rcb.timeout = 0;
	cmd->sa.ioasc = 0;
	cmd->cmd_tmf = false;
	cmd->sa.host_use[0] = 0; /* clears both completion and retry bytes */

	if (unlikely(atomic_inc_return(&cmd->free) != 1)) {
		pr_err("%s: Freeing cmd (%d) that is not in use!\n",
		       __func__, cmd->slot);
		return;
	}

	pr_devel("%s: released cmd %p index=%d\n", __func__, cmd, cmd->slot);
}

/**
 * process_cmd_err() - command error handler
 * @cmd:	AFU command that experienced the error.
 * @scp:	SCSI command associated with the AFU command in error.
 *
 * Translates error bits from AFU command to SCSI command results.
 */
static void process_cmd_err(struct afu_cmd *cmd, struct scsi_cmnd *scp)
{
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
		pr_debug("%s: cmd underrun cmd = %p scp = %p, resid = %d\n",
			 __func__, cmd, scp, resid);
	}

	if (ioasa->rc.flags & SISL_RC_FLAGS_OVERRUN) {
		pr_debug("%s: cmd underrun cmd = %p scp = %p\n",
			 __func__, cmd, scp);
		scp->result = (DID_ERROR << 16);
	}

	pr_debug("%s: cmd failed afu_rc=%d scsi_rc=%d fc_rc=%d "
		 "afu_extra=0x%X, scsi_extra=0x%X, fc_extra=0x%X\n",
		 __func__, ioasa->rc.afu_rc, ioasa->rc.scsi_rc,
		 ioasa->rc.fc_rc, ioasa->afu_extra, ioasa->scsi_extra,
		 ioasa->fc_extra);

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
 * (rcb.scp populated) commands.
 */
static void cmd_complete(struct afu_cmd *cmd)
{
	struct scsi_cmnd *scp;
	ulong lock_flags;
	struct afu *afu = cmd->parent;
	struct cxlflash_cfg *cfg = afu->parent;
	bool cmd_is_tmf;

	spin_lock_irqsave(&cmd->slock, lock_flags);
	cmd->sa.host_use_b[0] |= B_DONE;
	spin_unlock_irqrestore(&cmd->slock, lock_flags);

	if (cmd->rcb.scp) {
		scp = cmd->rcb.scp;
		if (unlikely(cmd->sa.ioasc))
			process_cmd_err(cmd, scp);
		else
			scp->result = (DID_OK << 16);

		cmd_is_tmf = cmd->cmd_tmf;
		cmd_checkin(cmd); /* Don't use cmd after here */

		pr_debug_ratelimited("%s: calling scsi_done scp=%p result=%X "
				     "ioasc=%d\n", __func__, scp, scp->result,
				     cmd->sa.ioasc);

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
 * context_reset() - timeout handler for AFU commands
 * @cmd:	AFU command that timed out.
 *
 * Sends a reset to the AFU.
 */
static void context_reset(struct afu_cmd *cmd)
{
	int nretry = 0;
	u64 rrin = 0x1;
	u64 room = 0;
	struct afu *afu = cmd->parent;
	ulong lock_flags;

	pr_debug("%s: cmd=%p\n", __func__, cmd);

	spin_lock_irqsave(&cmd->slock, lock_flags);

	/* Already completed? */
	if (cmd->sa.host_use_b[0] & B_DONE) {
		spin_unlock_irqrestore(&cmd->slock, lock_flags);
		return;
	}

	cmd->sa.host_use_b[0] |= (B_DONE | B_ERROR | B_TIMEOUT);
	spin_unlock_irqrestore(&cmd->slock, lock_flags);

	/*
	 * We really want to send this reset at all costs, so spread
	 * out wait time on successive retries for available room.
	 */
	do {
		room = readq_be(&afu->host_map->cmd_room);
		atomic64_set(&afu->room, room);
		if (room)
			goto write_rrin;
		udelay(nretry);
	} while (nretry++ < MC_ROOM_RETRY_CNT);

	pr_err("%s: no cmd_room to send reset\n", __func__);
	return;

write_rrin:
	nretry = 0;
	writeq_be(rrin, &afu->host_map->ioarrin);
	do {
		rrin = readq_be(&afu->host_map->ioarrin);
		if (rrin != 0x1)
			break;
		/* Double delay each time */
		udelay(2 << nretry);
	} while (nretry++ < MC_ROOM_RETRY_CNT);
}

/**
 * send_cmd() - sends an AFU command
 * @afu:	AFU associated with the host.
 * @cmd:	AFU command to send.
 *
 * Return:
 *	0 on success, SCSI_MLQUEUE_HOST_BUSY on failure
 */
static int send_cmd(struct afu *afu, struct afu_cmd *cmd)
{
	struct cxlflash_cfg *cfg = afu->parent;
	struct device *dev = &cfg->dev->dev;
	int nretry = 0;
	int rc = 0;
	u64 room;
	long newval;

	/*
	 * This routine is used by critical users such an AFU sync and to
	 * send a task management function (TMF). Thus we want to retry a
	 * bit before returning an error. To avoid the performance penalty
	 * of MMIO, we spread the update of 'room' over multiple commands.
	 */
retry:
	newval = atomic64_dec_if_positive(&afu->room);
	if (!newval) {
		do {
			room = readq_be(&afu->host_map->cmd_room);
			atomic64_set(&afu->room, room);
			if (room)
				goto write_ioarrin;
			udelay(nretry);
		} while (nretry++ < MC_ROOM_RETRY_CNT);

		dev_err(dev, "%s: no cmd_room to send 0x%X\n",
		       __func__, cmd->rcb.cdb[0]);

		goto no_room;
	} else if (unlikely(newval < 0)) {
		/* This should be rare. i.e. Only if two threads race and
		 * decrement before the MMIO read is done. In this case
		 * just benefit from the other thread having updated
		 * afu->room.
		 */
		if (nretry++ < MC_ROOM_RETRY_CNT) {
			udelay(nretry);
			goto retry;
		}

		goto no_room;
	}

write_ioarrin:
	writeq_be((u64)&cmd->rcb, &afu->host_map->ioarrin);
out:
	pr_devel("%s: cmd=%p len=%d ea=%p rc=%d\n", __func__, cmd,
		 cmd->rcb.data_len, (void *)cmd->rcb.data_ea, rc);
	return rc;

no_room:
	afu->read_room = true;
	kref_get(&cfg->afu->mapcount);
	schedule_work(&cfg->work_q);
	rc = SCSI_MLQUEUE_HOST_BUSY;
	goto out;
}

/**
 * wait_resp() - polls for a response or timeout to a sent AFU command
 * @afu:	AFU associated with the host.
 * @cmd:	AFU command that was sent.
 */
static void wait_resp(struct afu *afu, struct afu_cmd *cmd)
{
	ulong timeout = msecs_to_jiffies(cmd->rcb.timeout * 2 * 1000);

	timeout = wait_for_completion_timeout(&cmd->cevent, timeout);
	if (!timeout)
		context_reset(cmd);

	if (unlikely(cmd->sa.ioasc != 0))
		pr_err("%s: CMD 0x%X failed, IOASC: flags 0x%X, afu_rc 0x%X, "
		       "scsi_rc 0x%X, fc_rc 0x%X\n", __func__, cmd->rcb.cdb[0],
		       cmd->sa.rc.flags, cmd->sa.rc.afu_rc, cmd->sa.rc.scsi_rc,
		       cmd->sa.rc.fc_rc);
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
	struct afu_cmd *cmd;

	u32 port_sel = scp->device->channel + 1;
	short lflag = 0;
	struct Scsi_Host *host = scp->device->host;
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)host->hostdata;
	struct device *dev = &cfg->dev->dev;
	ulong lock_flags;
	int rc = 0;
	ulong to;

	cmd = cmd_checkout(afu);
	if (unlikely(!cmd)) {
		dev_err(dev, "%s: could not get a free command\n", __func__);
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	/* When Task Management Function is active do not send another */
	spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
	if (cfg->tmf_active)
		wait_event_interruptible_lock_irq(cfg->tmf_waitq,
						  !cfg->tmf_active,
						  cfg->tmf_slock);
	cfg->tmf_active = true;
	cmd->cmd_tmf = true;
	spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);

	cmd->rcb.ctx_id = afu->ctx_hndl;
	cmd->rcb.port_sel = port_sel;
	cmd->rcb.lun_id = lun_to_lunid(scp->device->lun);

	lflag = SISL_REQ_FLAGS_TMF_CMD;

	cmd->rcb.req_flags = (SISL_REQ_FLAGS_PORT_LUN_ID |
			      SISL_REQ_FLAGS_SUP_UNDERRUN | lflag);

	/* Stash the scp in the reserved field, for reuse during interrupt */
	cmd->rcb.scp = scp;

	/* Copy the CDB from the cmd passed in */
	memcpy(cmd->rcb.cdb, &tmfcmd, sizeof(tmfcmd));

	/* Send the command */
	rc = send_cmd(afu, cmd);
	if (unlikely(rc)) {
		cmd_checkin(cmd);
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
		dev_err(dev, "%s: TMF timed out!\n", __func__);
		rc = -1;
	}
	spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);
out:
	return rc;
}

static void afu_unmap(struct kref *ref)
{
	struct afu *afu = container_of(ref, struct afu, mapcount);

	if (likely(afu->afu_map)) {
		cxl_psa_unmap((void __iomem *)afu->afu_map);
		afu->afu_map = NULL;
	}
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
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)host->hostdata;
	struct afu *afu = cfg->afu;
	struct device *dev = &cfg->dev->dev;
	struct afu_cmd *cmd;
	u32 port_sel = scp->device->channel + 1;
	int nseg, i, ncount;
	struct scatterlist *sg;
	ulong lock_flags;
	short lflag = 0;
	int rc = 0;
	int kref_got = 0;

	dev_dbg_ratelimited(dev, "%s: (scp=%p) %d/%d/%d/%llu "
			    "cdb=(%08X-%08X-%08X-%08X)\n",
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
		dev_dbg_ratelimited(dev, "%s: device is in reset!\n", __func__);
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	case STATE_FAILTERM:
		dev_dbg_ratelimited(dev, "%s: device has failed!\n", __func__);
		scp->result = (DID_NO_CONNECT << 16);
		scp->scsi_done(scp);
		rc = 0;
		goto out;
	default:
		break;
	}

	cmd = cmd_checkout(afu);
	if (unlikely(!cmd)) {
		dev_err(dev, "%s: could not get a free command\n", __func__);
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	kref_get(&cfg->afu->mapcount);
	kref_got = 1;

	cmd->rcb.ctx_id = afu->ctx_hndl;
	cmd->rcb.port_sel = port_sel;
	cmd->rcb.lun_id = lun_to_lunid(scp->device->lun);

	if (scp->sc_data_direction == DMA_TO_DEVICE)
		lflag = SISL_REQ_FLAGS_HOST_WRITE;
	else
		lflag = SISL_REQ_FLAGS_HOST_READ;

	cmd->rcb.req_flags = (SISL_REQ_FLAGS_PORT_LUN_ID |
			      SISL_REQ_FLAGS_SUP_UNDERRUN | lflag);

	/* Stash the scp in the reserved field, for reuse during interrupt */
	cmd->rcb.scp = scp;

	nseg = scsi_dma_map(scp);
	if (unlikely(nseg < 0)) {
		dev_err(dev, "%s: Fail DMA map! nseg=%d\n",
			__func__, nseg);
		rc = SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}

	ncount = scsi_sg_count(scp);
	scsi_for_each_sg(scp, sg, ncount, i) {
		cmd->rcb.data_len = sg_dma_len(sg);
		cmd->rcb.data_ea = sg_dma_address(sg);
	}

	/* Copy the CDB from the scsi_cmnd passed in */
	memcpy(cmd->rcb.cdb, scp->cmnd, sizeof(cmd->rcb.cdb));

	/* Send the command */
	rc = send_cmd(afu, cmd);
	if (unlikely(rc)) {
		cmd_checkin(cmd);
		scsi_dma_unmap(scp);
	}

out:
	if (kref_got)
		kref_put(&afu->mapcount, afu_unmap);
	pr_devel("%s: returning rc=%d\n", __func__, rc);
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
	int i;
	char *buf = NULL;
	struct afu *afu = cfg->afu;

	if (cfg->afu) {
		for (i = 0; i < CXLFLASH_NUM_CMDS; i++) {
			buf = afu->cmd[i].buf;
			if (!((u64)buf & (PAGE_SIZE - 1)))
				free_page((ulong)buf);
		}

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
 * Cleans up all state associated with the command queue, and unmaps
 * the MMIO space.
 *
 *  - complete() will take care of commands we initiated (they'll be checked
 *  in as part of the cleanup that occurs after the completion)
 *
 *  - cmd_checkin() will take care of entries that we did not initiate and that
 *  have not (and will not) complete because they are sitting on a [now stale]
 *  hardware queue
 */
static void stop_afu(struct cxlflash_cfg *cfg)
{
	int i;
	struct afu *afu = cfg->afu;
	struct afu_cmd *cmd;

	if (likely(afu)) {
		for (i = 0; i < CXLFLASH_NUM_CMDS; i++) {
			cmd = &afu->cmd[i];
			complete(&cmd->cevent);
			if (!atomic_read(&cmd->free))
				cmd_checkin(cmd);
		}

		if (likely(afu->afu_map)) {
			cxl_psa_unmap((void __iomem *)afu->afu_map);
			afu->afu_map = NULL;
		}
		kref_put(&afu->mapcount, afu_unmap);
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

	pr_debug("%s: returning\n", __func__);
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
	ulong lock_flags;

	/* If a Task Management Function is active, wait for it to complete
	 * before continuing with remove.
	 */
	spin_lock_irqsave(&cfg->tmf_slock, lock_flags);
	if (cfg->tmf_active)
		wait_event_interruptible_lock_irq(cfg->tmf_waitq,
						  !cfg->tmf_active,
						  cfg->tmf_slock);
	spin_unlock_irqrestore(&cfg->tmf_slock, lock_flags);

	cfg->state = STATE_FAILTERM;
	cxlflash_stop_term_user_contexts(cfg);

	switch (cfg->init_state) {
	case INIT_STATE_SCSI:
		cxlflash_term_local_luns(cfg);
		scsi_remove_host(cfg->host);
		/* fall through */
	case INIT_STATE_AFU:
		cancel_work_sync(&cfg->work_q);
		term_afu(cfg);
	case INIT_STATE_PCI:
		pci_disable_device(pdev);
	case INIT_STATE_NONE:
		free_mem(cfg);
		scsi_host_put(cfg->host);
		break;
	}

	pr_debug("%s: returning\n", __func__);
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
	int i;
	char *buf = NULL;
	struct device *dev = &cfg->dev->dev;

	/* AFU is ~12k, i.e. only one 64k page or up to four 4k pages */
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

	for (i = 0; i < CXLFLASH_NUM_CMDS; buf += CMD_BUFSIZE, i++) {
		if (!((u64)buf & (PAGE_SIZE - 1))) {
			buf = (void *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
			if (unlikely(!buf)) {
				dev_err(dev,
					"%s: Allocate command buffers fail!\n",
				       __func__);
				rc = -ENOMEM;
				free_mem(cfg);
				goto out;
			}
		}

		cfg->afu->cmd[i].buf = buf;
		atomic_set(&cfg->afu->cmd[i].free, 1);
		cfg->afu->cmd[i].slot = i;
	}

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
	int rc = 0;

	rc = pci_enable_device(pdev);
	if (rc || pci_channel_offline(pdev)) {
		if (pci_channel_offline(pdev)) {
			cxlflash_wait_for_pci_err_recovery(cfg);
			rc = pci_enable_device(pdev);
		}

		if (rc) {
			dev_err(&pdev->dev, "%s: Cannot enable adapter\n",
				__func__);
			cxlflash_wait_for_pci_err_recovery(cfg);
			goto out;
		}
	}

out:
	pr_debug("%s: returning rc=%d\n", __func__, rc);
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
	int rc = 0;

	rc = scsi_add_host(cfg->host, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev, "%s: scsi_add_host failed (rc=%d)\n",
			__func__, rc);
		goto out;
	}

	scsi_scan_host(cfg->host);

out:
	pr_debug("%s: returning rc=%d\n", __func__, rc);
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
 *	-EINVAL when @delay_us is less than 1000
 */
static int wait_port_online(__be64 __iomem *fc_regs, u32 delay_us, u32 nretry)
{
	u64 status;

	if (delay_us < 1000) {
		pr_err("%s: invalid delay specified %d\n", __func__, delay_us);
		return -EINVAL;
	}

	do {
		msleep(delay_us / 1000);
		status = readq_be(&fc_regs[FC_MTIP_STATUS / 8]);
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
 *	-EINVAL when @delay_us is less than 1000
 */
static int wait_port_offline(__be64 __iomem *fc_regs, u32 delay_us, u32 nretry)
{
	u64 status;

	if (delay_us < 1000) {
		pr_err("%s: invalid delay specified %d\n", __func__, delay_us);
		return -EINVAL;
	}

	do {
		msleep(delay_us / 1000);
		status = readq_be(&fc_regs[FC_MTIP_STATUS / 8]);
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
 *
 * Return:
 *	0 when the WWPN is successfully written and the port comes back online
 *	-1 when the port fails to go offline or come back up online
 */
static int afu_set_wwpn(struct afu *afu, int port, __be64 __iomem *fc_regs,
			u64 wwpn)
{
	int rc = 0;

	set_port_offline(fc_regs);

	if (!wait_port_offline(fc_regs, FC_PORT_STATUS_RETRY_INTERVAL_US,
			       FC_PORT_STATUS_RETRY_CNT)) {
		pr_debug("%s: wait on port %d to go offline timed out\n",
			 __func__, port);
		rc = -1; /* but continue on to leave the port back online */
	}

	if (rc == 0)
		writeq_be(wwpn, &fc_regs[FC_PNAME / 8]);

	/* Always return success after programming WWPN */
	rc = 0;

	set_port_online(fc_regs);

	if (!wait_port_online(fc_regs, FC_PORT_STATUS_RETRY_INTERVAL_US,
			      FC_PORT_STATUS_RETRY_CNT)) {
		pr_err("%s: wait on port %d to go online timed out\n",
		       __func__, port);
	}

	pr_debug("%s: returning rc=%d\n", __func__, rc);

	return rc;
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
	u64 port_sel;

	/* first switch the AFU to the other links, if any */
	port_sel = readq_be(&afu->afu_map->global.regs.afu_port_sel);
	port_sel &= ~(1ULL << port);
	writeq_be(port_sel, &afu->afu_map->global.regs.afu_port_sel);
	cxlflash_afu_sync(afu, 0, 0, AFU_GSYNC);

	set_port_offline(fc_regs);
	if (!wait_port_offline(fc_regs, FC_PORT_STATUS_RETRY_INTERVAL_US,
			       FC_PORT_STATUS_RETRY_CNT))
		pr_err("%s: wait on port %d to go offline timed out\n",
		       __func__, port);

	set_port_online(fc_regs);
	if (!wait_port_online(fc_regs, FC_PORT_STATUS_RETRY_INTERVAL_US,
			      FC_PORT_STATUS_RETRY_CNT))
		pr_err("%s: wait on port %d to go online timed out\n",
		       __func__, port);

	/* switch back to include this port */
	port_sel |= (1ULL << port);
	writeq_be(port_sel, &afu->afu_map->global.regs.afu_port_sel);
	cxlflash_afu_sync(afu, 0, 0, AFU_GSYNC);

	pr_debug("%s: returning port_sel=%lld\n", __func__, port_sel);
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
	{SISL_ASTATUS_FC0_LINK_UP, "link up", 0, SCAN_HOST},
	{SISL_ASTATUS_FC1_OTHER, "other error", 1, CLR_FC_ERROR | LINK_RESET},
	{SISL_ASTATUS_FC1_LOGO, "target initiated LOGO", 1, 0},
	{SISL_ASTATUS_FC1_CRC_T, "CRC threshold exceeded", 1, LINK_RESET},
	{SISL_ASTATUS_FC1_LOGI_R, "login timed out, retrying", 1, LINK_RESET},
	{SISL_ASTATUS_FC1_LOGI_F, "login failed", 1, CLR_FC_ERROR},
	{SISL_ASTATUS_FC1_LOGI_S, "login succeeded", 1, SCAN_HOST},
	{SISL_ASTATUS_FC1_LINK_DN, "link down", 1, 0},
	{SISL_ASTATUS_FC1_LINK_UP, "link up", 1, SCAN_HOST},
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
	u64 reg;
	u64 reg_unmasked;

	reg = readq_be(&afu->host_map->intr_status);
	reg_unmasked = (reg & SISL_ISTATUS_UNMASK);

	if (reg_unmasked == 0UL) {
		pr_err("%s: %llX: spurious interrupt, intr_status %016llX\n",
		       __func__, (u64)afu, reg);
		goto cxlflash_sync_err_irq_exit;
	}

	pr_err("%s: %llX: unexpected interrupt, intr_status %016llX\n",
	       __func__, (u64)afu, reg);

	writeq_be(reg_unmasked, &afu->host_map->intr_clear);

cxlflash_sync_err_irq_exit:
	pr_debug("%s: returning rc=%d\n", __func__, IRQ_HANDLED);
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

		cmd = (struct afu_cmd *)(entry & ~SISL_RESP_HANDLE_T_BIT);
		cmd_complete(cmd);

		/* Advance to next entry or wrap and flip the toggle bit */
		if (hrrq_curr < hrrq_end)
			hrrq_curr++;
		else {
			hrrq_curr = hrrq_start;
			toggle ^= SISL_RESP_HANDLE_T_BIT;
		}
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
		dev_err(dev, "%s: spurious interrupt, aintr_status 0x%016llX\n",
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

		dev_err(dev, "%s: FC Port %d -> %s, fc_status 0x%08llX\n",
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
			kref_get(&cfg->afu->mapcount);
			schedule_work(&cfg->work_q);
		}

		if (info->action & CLR_FC_ERROR) {
			reg = readq_be(&global->fc_regs[port][FC_ERROR / 8]);

			/*
			 * Since all errors are unmasked, FC_ERROR and FC_ERRCAP
			 * should be the same and tracing one is sufficient.
			 */

			dev_err(dev, "%s: fc %d: clearing fc_error 0x%08llX\n",
				__func__, port, reg);

			writeq_be(reg, &global->fc_regs[port][FC_ERROR / 8]);
			writeq_be(0, &global->fc_regs[port][FC_ERRCAP / 8]);
		}

		if (info->action & SCAN_HOST) {
			atomic_inc(&cfg->scan_host_needed);
			kref_get(&cfg->afu->mapcount);
			schedule_work(&cfg->work_q);
		}
	}

out:
	dev_dbg(dev, "%s: returning IRQ_HANDLED, afu=%p\n", __func__, afu);
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
	int rc = 0;

	rc = cxl_start_context(cfg->mcctx,
			       cfg->afu->work.work_element_descriptor,
			       NULL);

	pr_debug("%s: returning rc=%d\n", __func__, rc);
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
	struct pci_dev *dev = cfg->parent_dev;
	int rc = 0;
	int ro_start, ro_size, i, j, k;
	ssize_t vpd_size;
	char vpd_data[CXLFLASH_VPD_LEN];
	char tmp_buf[WWPN_BUF_LEN] = { 0 };
	char *wwpn_vpd_tags[NUM_FC_PORTS] = { "V5", "V6" };

	/* Get the VPD data from the device */
	vpd_size = pci_read_vpd(dev, 0, sizeof(vpd_data), vpd_data);
	if (unlikely(vpd_size <= 0)) {
		dev_err(&dev->dev, "%s: Unable to read VPD (size = %ld)\n",
		       __func__, vpd_size);
		rc = -ENODEV;
		goto out;
	}

	/* Get the read only section offset */
	ro_start = pci_vpd_find_tag(vpd_data, 0, vpd_size,
				    PCI_VPD_LRDT_RO_DATA);
	if (unlikely(ro_start < 0)) {
		dev_err(&dev->dev, "%s: VPD Read-only data not found\n",
			__func__);
		rc = -ENODEV;
		goto out;
	}

	/* Get the read only section size, cap when extends beyond read VPD */
	ro_size = pci_vpd_lrdt_size(&vpd_data[ro_start]);
	j = ro_size;
	i = ro_start + PCI_VPD_LRDT_TAG_SIZE;
	if (unlikely((i + j) > vpd_size)) {
		pr_debug("%s: Might need to read more VPD (%d > %ld)\n",
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
			dev_err(&dev->dev, "%s: Port %d WWPN not found "
				"in VPD\n", __func__, k);
			rc = -ENODEV;
			goto out;
		}

		j = pci_vpd_info_field_size(&vpd_data[i]);
		i += PCI_VPD_INFO_FLD_HDR_SIZE;
		if (unlikely((i + j > vpd_size) || (j != WWPN_LEN))) {
			dev_err(&dev->dev, "%s: Port %d WWPN incomplete or "
				"VPD corrupt\n",
			       __func__, k);
			rc = -ENODEV;
			goto out;
		}

		memcpy(tmp_buf, &vpd_data[i], WWPN_LEN);
		rc = kstrtoul(tmp_buf, WWPN_LEN, (ulong *)&wwpn[k]);
		if (unlikely(rc)) {
			dev_err(&dev->dev, "%s: Fail to convert port %d WWPN "
				"to integer\n", __func__, k);
			rc = -ENODEV;
			goto out;
		}
	}

out:
	pr_debug("%s: returning rc=%d\n", __func__, rc);
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

	/* Initialize cmd fields that never change */
	for (i = 0; i < CXLFLASH_NUM_CMDS; i++) {
		afu->cmd[i].rcb.ctx_id = afu->ctx_hndl;
		afu->cmd[i].rcb.msi = SISL_MSI_RRQ_UPDATED;
		afu->cmd[i].rcb.rrq = 0x0;
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
	u64 wwpn[NUM_FC_PORTS];	/* wwpn of AFU ports */
	int i = 0, num_ports = 0;
	int rc = 0;
	u64 reg;

	rc = read_vpd(cfg, &wwpn[0]);
	if (rc) {
		dev_err(dev, "%s: could not read vpd rc=%d\n", __func__, rc);
		goto out;
	}

	pr_debug("%s: wwpn0=0x%llX wwpn1=0x%llX\n", __func__, wwpn[0], wwpn[1]);

	/* Set up RRQ in AFU for master issued cmds */
	writeq_be((u64) afu->hrrq_start, &afu->host_map->rrq_start);
	writeq_be((u64) afu->hrrq_end, &afu->host_map->rrq_end);

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
		if (wwpn[i] != 0 &&
		    afu_set_wwpn(afu, i,
				 &afu->afu_map->global.fc_regs[i][0],
				 wwpn[i])) {
			dev_err(dev, "%s: failed to set WWPN on port %d\n",
			       __func__, i);
			rc = -EIO;
			goto out;
		}
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
	struct afu_cmd *cmd;

	int i = 0;
	int rc = 0;

	for (i = 0; i < CXLFLASH_NUM_CMDS; i++) {
		cmd = &afu->cmd[i];

		init_completion(&cmd->cevent);
		spin_lock_init(&cmd->slock);
		cmd->parent = afu;
	}

	init_pcr(cfg);

	/* After an AFU reset, RRQ entries are stale, clear them */
	memset(&afu->rrq_entry, 0, sizeof(afu->rrq_entry));

	/* Initialize RRQ pointers */
	afu->hrrq_start = &afu->rrq_entry[0];
	afu->hrrq_end = &afu->rrq_entry[NUM_RRQ_ENTRY - 1];
	afu->hrrq_curr = afu->hrrq_start;
	afu->toggle = 1;

	rc = init_global(cfg);

	pr_debug("%s: returning rc=%d\n", __func__, rc);
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
		dev_err(dev, "%s: call to allocate_afu_irqs failed rc=%d!\n",
			__func__, rc);
		level = UNDO_NOOP;
		goto out;
	}

	rc = cxl_map_afu_irq(ctx, 1, cxlflash_sync_err_irq, afu,
			     "SISL_MSI_SYNC_ERROR");
	if (unlikely(rc <= 0)) {
		dev_err(dev, "%s: IRQ 1 (SISL_MSI_SYNC_ERROR) map failed!\n",
			__func__);
		level = FREE_IRQ;
		goto out;
	}

	rc = cxl_map_afu_irq(ctx, 2, cxlflash_rrq_irq, afu,
			     "SISL_MSI_RRQ_UPDATED");
	if (unlikely(rc <= 0)) {
		dev_err(dev, "%s: IRQ 2 (SISL_MSI_RRQ_UPDATED) map failed!\n",
			__func__);
		level = UNMAP_ONE;
		goto out;
	}

	rc = cxl_map_afu_irq(ctx, 3, cxlflash_async_err_irq, afu,
			     "SISL_MSI_ASYNC_ERROR");
	if (unlikely(rc <= 0)) {
		dev_err(dev, "%s: IRQ 3 (SISL_MSI_ASYNC_ERROR) map failed!\n",
			__func__);
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
		dev_err(dev, "%s: initial AFU reset failed rc=%d\n",
			__func__, rc);
		goto ret;
	}

	level = init_intr(cfg, ctx);
	if (unlikely(level)) {
		dev_err(dev, "%s: setting up interrupts failed rc=%d\n",
			__func__, rc);
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
	pr_debug("%s: returning rc=%d\n", __func__, rc);
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
		dev_err(dev, "%s: call to init_mc failed, rc=%d!\n",
			__func__, rc);
		goto out;
	}

	/* Map the entire MMIO space of the AFU */
	afu->afu_map = cxl_psa_map(cfg->mcctx);
	if (!afu->afu_map) {
		dev_err(dev, "%s: call to cxl_psa_map failed!\n", __func__);
		rc = -ENOMEM;
		goto err1;
	}
	kref_init(&afu->mapcount);

	/* No byte reverse on reading afu_version or string will be backwards */
	reg = readq(&afu->afu_map->global.regs.afu_version);
	memcpy(afu->version, &reg, sizeof(reg));
	afu->interface_version =
	    readq_be(&afu->afu_map->global.regs.interface_version);
	if ((afu->interface_version + 1) == 0) {
		pr_err("Back level AFU, please upgrade. AFU version %s "
		       "interface version 0x%llx\n", afu->version,
		       afu->interface_version);
		rc = -EINVAL;
		goto err2;
	}

	pr_debug("%s: afu version %s, interface version 0x%llX\n", __func__,
		 afu->version, afu->interface_version);

	rc = start_afu(cfg);
	if (rc) {
		dev_err(dev, "%s: call to start_afu failed, rc=%d!\n",
			__func__, rc);
		goto err2;
	}

	afu_err_intr_init(cfg->afu);
	atomic64_set(&afu->room, readq_be(&afu->host_map->cmd_room));

	/* Restore the LUN mappings */
	cxlflash_restore_luntable(cfg);
out:
	pr_debug("%s: returning rc=%d\n", __func__, rc);
	return rc;

err2:
	kref_put(&afu->mapcount, afu_unmap);
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
	int rc = 0;
	int retry_cnt = 0;
	static DEFINE_MUTEX(sync_active);

	if (cfg->state != STATE_NORMAL) {
		pr_debug("%s: Sync not required! (%u)\n", __func__, cfg->state);
		return 0;
	}

	mutex_lock(&sync_active);
retry:
	cmd = cmd_checkout(afu);
	if (unlikely(!cmd)) {
		retry_cnt++;
		udelay(1000 * retry_cnt);
		if (retry_cnt < MC_RETRY_CNT)
			goto retry;
		dev_err(dev, "%s: could not get a free command\n", __func__);
		rc = -1;
		goto out;
	}

	pr_debug("%s: afu=%p cmd=%p %d\n", __func__, afu, cmd, ctx_hndl_u);

	memset(cmd->rcb.cdb, 0, sizeof(cmd->rcb.cdb));

	cmd->rcb.req_flags = SISL_REQ_FLAGS_AFU_CMD;
	cmd->rcb.port_sel = 0x0;	/* NA */
	cmd->rcb.lun_id = 0x0;	/* NA */
	cmd->rcb.data_len = 0x0;
	cmd->rcb.data_ea = 0x0;
	cmd->rcb.timeout = MC_AFU_SYNC_TIMEOUT;

	cmd->rcb.cdb[0] = 0xC0;	/* AFU Sync */
	cmd->rcb.cdb[1] = mode;

	/* The cdb is aligned, no unaligned accessors required */
	*((__be16 *)&cmd->rcb.cdb[2]) = cpu_to_be16(ctx_hndl_u);
	*((__be32 *)&cmd->rcb.cdb[4]) = cpu_to_be32(res_hndl_u);

	rc = send_cmd(afu, cmd);
	if (unlikely(rc))
		goto out;

	wait_resp(afu, cmd);

	/* Set on timeout */
	if (unlikely((cmd->sa.ioasc != 0) ||
		     (cmd->sa.host_use_b[0] & B_ERROR)))
		rc = -1;
out:
	mutex_unlock(&sync_active);
	if (cmd)
		cmd_checkin(cmd);
	pr_debug("%s: returning rc=%d\n", __func__, rc);
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
	int rc = 0;
	/* Stop the context before the reset. Since the context is
	 * no longer available restart it after the reset is complete
	 */

	term_afu(cfg);

	rc = init_afu(cfg);

	pr_debug("%s: returning rc=%d\n", __func__, rc);
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
	struct Scsi_Host *host = scp->device->host;
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)host->hostdata;
	struct afu *afu = cfg->afu;
	int rcr = 0;

	pr_debug("%s: (scp=%p) %d/%d/%d/%llu "
		 "cdb=(%08X-%08X-%08X-%08X)\n", __func__, scp,
		 host->host_no, scp->device->channel,
		 scp->device->id, scp->device->lun,
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

	pr_debug("%s: returning rc=%d\n", __func__, rc);
	return rc;
}

/**
 * cxlflash_eh_host_reset_handler() - reset the host adapter
 * @scp:	SCSI command from stack identifying host.
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
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)host->hostdata;

	pr_debug("%s: (scp=%p) %d/%d/%d/%llu "
		 "cdb=(%08X-%08X-%08X-%08X)\n", __func__, scp,
		 host->host_no, scp->device->channel,
		 scp->device->id, scp->device->lun,
		 get_unaligned_be32(&((u32 *)scp->cmnd)[0]),
		 get_unaligned_be32(&((u32 *)scp->cmnd)[1]),
		 get_unaligned_be32(&((u32 *)scp->cmnd)[2]),
		 get_unaligned_be32(&((u32 *)scp->cmnd)[3]));

	switch (cfg->state) {
	case STATE_NORMAL:
		cfg->state = STATE_RESET;
		cxlflash_mark_contexts_error(cfg);
		rcr = afu_reset(cfg);
		if (rcr) {
			rc = FAILED;
			cfg->state = STATE_FAILTERM;
		} else
			cfg->state = STATE_NORMAL;
		wake_up_all(&cfg->reset_waitq);
		break;
	case STATE_RESET:
		wait_event(cfg->reset_waitq, cfg->state != STATE_RESET);
		if (cfg->state == STATE_NORMAL)
			break;
		/* fall through */
	default:
		rc = FAILED;
		break;
	}

	pr_debug("%s: returning rc=%d\n", __func__, rc);
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
	struct Scsi_Host *shost = class_to_shost(dev);
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)shost->hostdata;
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
	struct Scsi_Host *shost = class_to_shost(dev);
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)shost->hostdata;
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
	struct Scsi_Host *shost = class_to_shost(dev);
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)shost->hostdata;
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
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)shost->hostdata;
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
				   "%03d: %016llX\n", i, readq_be(&fc_port[i]));
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
	struct Scsi_Host *shost = class_to_shost(dev);
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)shost->hostdata;
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
	struct Scsi_Host *shost = class_to_shost(dev);
	struct cxlflash_cfg *cfg = (struct cxlflash_cfg *)shost->hostdata;
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
	.this_id = -1,
	.sg_tablesize = SG_NONE,	/* No scatter gather support */
	.max_sectors = CXLFLASH_MAX_SECTORS,
	.use_clustering = ENABLE_CLUSTERING,
	.shost_attrs = cxlflash_host_attrs,
	.sdev_attrs = cxlflash_dev_attrs,
};

/*
 * Device dependent values
 */
static struct dev_dependent_vals dev_corsa_vals = { CXLFLASH_MAX_SECTORS };
static struct dev_dependent_vals dev_flash_gt_vals = { CXLFLASH_MAX_SECTORS };

/*
 * PCI device binding table
 */
static struct pci_device_id cxlflash_pci_table[] = {
	{PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_CORSA,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, (kernel_ulong_t)&dev_corsa_vals},
	{PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_FLASH_GT,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, (kernel_ulong_t)&dev_flash_gt_vals},
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
 * - Read AFU command room
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

	if (afu->read_room) {
		atomic64_set(&afu->room, readq_be(&afu->host_map->cmd_room));
		afu->read_room = false;
	}

	spin_unlock_irqrestore(cfg->host->host_lock, lock_flags);

	if (atomic_dec_if_positive(&cfg->scan_host_needed) >= 0)
		scsi_scan_host(cfg->host);
	kref_put(&afu->mapcount, afu_unmap);
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
	struct device *phys_dev;
	struct dev_dependent_vals *ddv;
	int rc = 0;

	dev_dbg(&pdev->dev, "%s: Found CXLFLASH with IRQ: %d\n",
		__func__, pdev->irq);

	ddv = (struct dev_dependent_vals *)dev_id->driver_data;
	driver_template.max_sectors = ddv->max_sectors;

	host = scsi_host_alloc(&driver_template, sizeof(struct cxlflash_cfg));
	if (!host) {
		dev_err(&pdev->dev, "%s: call to scsi_host_alloc failed!\n",
			__func__);
		rc = -ENOMEM;
		goto out;
	}

	host->max_id = CXLFLASH_MAX_NUM_TARGETS_PER_BUS;
	host->max_lun = CXLFLASH_MAX_NUM_LUNS_PER_TARGET;
	host->max_channel = NUM_FC_PORTS - 1;
	host->unique_id = host->host_no;
	host->max_cmd_len = CXLFLASH_MAX_CDB_LEN;

	cfg = (struct cxlflash_cfg *)host->hostdata;
	cfg->host = host;
	rc = alloc_mem(cfg);
	if (rc) {
		dev_err(&pdev->dev, "%s: call to alloc_mem failed!\n",
			__func__);
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

	/*
	 * Use the special service provided to look up the physical
	 * PCI device, since we are called on the probe of the virtual
	 * PCI host bus (vphb)
	 */
	phys_dev = cxl_get_phys_dev(pdev);
	if (!dev_is_pci(phys_dev)) {
		dev_err(&pdev->dev, "%s: not a pci dev\n", __func__);
		rc = -ENODEV;
		goto out_remove;
	}
	cfg->parent_dev = to_pci_dev(phys_dev);

	cfg->cxl_afu = cxl_pci_to_afu(pdev);

	rc = init_pci(cfg);
	if (rc) {
		dev_err(&pdev->dev, "%s: call to init_pci "
			"failed rc=%d!\n", __func__, rc);
		goto out_remove;
	}
	cfg->init_state = INIT_STATE_PCI;

	rc = init_afu(cfg);
	if (rc) {
		dev_err(&pdev->dev, "%s: call to init_afu "
			"failed rc=%d!\n", __func__, rc);
		goto out_remove;
	}
	cfg->init_state = INIT_STATE_AFU;

	rc = init_scsi(cfg);
	if (rc) {
		dev_err(&pdev->dev, "%s: call to init_scsi "
			"failed rc=%d!\n", __func__, rc);
		goto out_remove;
	}
	cfg->init_state = INIT_STATE_SCSI;

out:
	pr_debug("%s: returning rc=%d\n", __func__, rc);
	return rc;

out_remove:
	cxlflash_remove(pdev);
	goto out;
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
 * cxlflash_pci_error_detected() - called when a PCI error is detected
 * @pdev:	PCI device struct.
 * @state:	PCI channel state.
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
		cfg->state = STATE_RESET;
		scsi_block_requests(cfg->host);
		drain_ioctls(cfg);
		rc = cxlflash_mark_contexts_error(cfg);
		if (unlikely(rc))
			dev_err(dev, "%s: Failed to mark user contexts!(%d)\n",
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
		dev_err(dev, "%s: EEH recovery failed! (%d)\n", __func__, rc);
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
	.err_handler = &cxlflash_err_handler,
};

/**
 * init_cxlflash() - module entry point
 *
 * Return: 0 on success, -errno on failure
 */
static int __init init_cxlflash(void)
{
	pr_info("%s: %s\n", __func__, CXLFLASH_ADAPTER_NAME);

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
