/*
 * Serial Attached SCSI (SAS) class SCSI Host glue.
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 *
 * This file is licensed under GPLv2.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/export.h>
#include <linux/ctype.h>

#include "sas_internal.h"

#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>
#include <scsi/sas_ata.h>
#include "../scsi_sas_internal.h"
#include "../scsi_transport_api.h"
#include "../scsi_priv.h"

#include <linux/err.h>
#include <linux/blkdev.h>
#include <linux/freezer.h>
#include <linux/gfp.h>
#include <linux/scatterlist.h>
#include <linux/libata.h>

/* record final status and free the task */
static void sas_end_task(struct scsi_cmnd *sc, struct sas_task *task)
{
	struct task_status_struct *ts = &task->task_status;
	int hs = 0, stat = 0;

	if (ts->resp == SAS_TASK_UNDELIVERED) {
		/* transport error */
		hs = DID_NO_CONNECT;
	} else { /* ts->resp == SAS_TASK_COMPLETE */
		/* task delivered, what happened afterwards? */
		switch (ts->stat) {
		case SAS_DEV_NO_RESPONSE:
		case SAS_INTERRUPTED:
		case SAS_PHY_DOWN:
		case SAS_NAK_R_ERR:
		case SAS_OPEN_TO:
			hs = DID_NO_CONNECT;
			break;
		case SAS_DATA_UNDERRUN:
			scsi_set_resid(sc, ts->residual);
			if (scsi_bufflen(sc) - scsi_get_resid(sc) < sc->underflow)
				hs = DID_ERROR;
			break;
		case SAS_DATA_OVERRUN:
			hs = DID_ERROR;
			break;
		case SAS_QUEUE_FULL:
			hs = DID_SOFT_ERROR; /* retry */
			break;
		case SAS_DEVICE_UNKNOWN:
			hs = DID_BAD_TARGET;
			break;
		case SAS_SG_ERR:
			hs = DID_PARITY;
			break;
		case SAS_OPEN_REJECT:
			if (ts->open_rej_reason == SAS_OREJ_RSVD_RETRY)
				hs = DID_SOFT_ERROR; /* retry */
			else
				hs = DID_ERROR;
			break;
		case SAS_PROTO_RESPONSE:
			SAS_DPRINTK("LLDD:%s sent SAS_PROTO_RESP for an SSP "
				    "task; please report this\n",
				    task->dev->port->ha->sas_ha_name);
			break;
		case SAS_ABORTED_TASK:
			hs = DID_ABORT;
			break;
		case SAM_STAT_CHECK_CONDITION:
			memcpy(sc->sense_buffer, ts->buf,
			       min(SCSI_SENSE_BUFFERSIZE, ts->buf_valid_size));
			stat = SAM_STAT_CHECK_CONDITION;
			break;
		default:
			stat = ts->stat;
			break;
		}
	}

	sc->result = (hs << 16) | stat;
	ASSIGN_SAS_TASK(sc, NULL);
	list_del_init(&task->list);
	sas_free_task(task);
}

static void sas_scsi_task_done(struct sas_task *task)
{
	struct scsi_cmnd *sc = task->uldd_task;
	struct domain_device *dev = task->dev;
	struct sas_ha_struct *ha = dev->port->ha;
	unsigned long flags;

	spin_lock_irqsave(&dev->done_lock, flags);
	if (test_bit(SAS_HA_FROZEN, &ha->state))
		task = NULL;
	else
		ASSIGN_SAS_TASK(sc, NULL);
	spin_unlock_irqrestore(&dev->done_lock, flags);

	if (unlikely(!task)) {
		/* task will be completed by the error handler */
		SAS_DPRINTK("task done but aborted\n");
		return;
	}

	if (unlikely(!sc)) {
		SAS_DPRINTK("task_done called with non existing SCSI cmnd!\n");
		list_del_init(&task->list);
		sas_free_task(task);
		return;
	}

	sas_end_task(sc, task);
	sc->scsi_done(sc);
}

static struct sas_task *sas_create_task(struct scsi_cmnd *cmd,
					       struct domain_device *dev,
					       gfp_t gfp_flags)
{
	struct sas_task *task = sas_alloc_task(gfp_flags);
	struct scsi_lun lun;

	if (!task)
		return NULL;

	task->uldd_task = cmd;
	ASSIGN_SAS_TASK(cmd, task);

	task->dev = dev;
	task->task_proto = task->dev->tproto; /* BUG_ON(!SSP) */

	task->ssp_task.retry_count = 1;
	int_to_scsilun(cmd->device->lun, &lun);
	memcpy(task->ssp_task.LUN, &lun.scsi_lun, 8);
	task->ssp_task.task_attr = TASK_ATTR_SIMPLE;
	task->ssp_task.cmd = cmd;

	task->scatter = scsi_sglist(cmd);
	task->num_scatter = scsi_sg_count(cmd);
	task->total_xfer_len = scsi_bufflen(cmd);
	task->data_dir = cmd->sc_data_direction;

	task->task_done = sas_scsi_task_done;

	return task;
}

int sas_queue_up(struct sas_task *task)
{
	struct sas_ha_struct *sas_ha = task->dev->port->ha;
	struct scsi_core *core = &sas_ha->core;
	unsigned long flags;
	LIST_HEAD(list);

	spin_lock_irqsave(&core->task_queue_lock, flags);
	if (sas_ha->lldd_queue_size < core->task_queue_size + 1) {
		spin_unlock_irqrestore(&core->task_queue_lock, flags);
		return -SAS_QUEUE_FULL;
	}
	list_add_tail(&task->list, &core->task_queue);
	core->task_queue_size += 1;
	spin_unlock_irqrestore(&core->task_queue_lock, flags);
	wake_up_process(core->queue_thread);

	return 0;
}

int sas_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmd)
{
	struct sas_internal *i = to_sas_internal(host->transportt);
	struct domain_device *dev = cmd_to_domain_dev(cmd);
	struct sas_ha_struct *sas_ha = dev->port->ha;
	struct sas_task *task;
	int res = 0;

	/* If the device fell off, no sense in issuing commands */
	if (test_bit(SAS_DEV_GONE, &dev->state)) {
		cmd->result = DID_BAD_TARGET << 16;
		goto out_done;
	}

	if (dev_is_sata(dev)) {
		spin_lock_irq(dev->sata_dev.ap->lock);
		res = ata_sas_queuecmd(cmd, dev->sata_dev.ap);
		spin_unlock_irq(dev->sata_dev.ap->lock);
		return res;
	}

	task = sas_create_task(cmd, dev, GFP_ATOMIC);
	if (!task)
		return SCSI_MLQUEUE_HOST_BUSY;

	/* Queue up, Direct Mode or Task Collector Mode. */
	if (sas_ha->lldd_max_execute_num < 2)
		res = i->dft->lldd_execute_task(task, 1, GFP_ATOMIC);
	else
		res = sas_queue_up(task);

	if (res)
		goto out_free_task;
	return 0;

out_free_task:
	SAS_DPRINTK("lldd_execute_task returned: %d\n", res);
	ASSIGN_SAS_TASK(cmd, NULL);
	sas_free_task(task);
	if (res == -SAS_QUEUE_FULL)
		cmd->result = DID_SOFT_ERROR << 16; /* retry */
	else
		cmd->result = DID_ERROR << 16;
out_done:
	cmd->scsi_done(cmd);
	return 0;
}

static void sas_eh_finish_cmd(struct scsi_cmnd *cmd)
{
	struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(cmd->device->host);
	struct sas_task *task = TO_SAS_TASK(cmd);

	/* At this point, we only get called following an actual abort
	 * of the task, so we should be guaranteed not to be racing with
	 * any completions from the LLD.  Task is freed after this.
	 */
	sas_end_task(cmd, task);

	/* now finish the command and move it on to the error
	 * handler done list, this also takes it off the
	 * error handler pending list.
	 */
	scsi_eh_finish_cmd(cmd, &sas_ha->eh_done_q);
}

static void sas_eh_defer_cmd(struct scsi_cmnd *cmd)
{
	struct domain_device *dev = cmd_to_domain_dev(cmd);
	struct sas_ha_struct *ha = dev->port->ha;
	struct sas_task *task = TO_SAS_TASK(cmd);

	if (!dev_is_sata(dev)) {
		sas_eh_finish_cmd(cmd);
		return;
	}

	/* report the timeout to libata */
	sas_end_task(cmd, task);
	list_move_tail(&cmd->eh_entry, &ha->eh_ata_q);
}

static void sas_scsi_clear_queue_lu(struct list_head *error_q, struct scsi_cmnd *my_cmd)
{
	struct scsi_cmnd *cmd, *n;

	list_for_each_entry_safe(cmd, n, error_q, eh_entry) {
		if (cmd->device->sdev_target == my_cmd->device->sdev_target &&
		    cmd->device->lun == my_cmd->device->lun)
			sas_eh_defer_cmd(cmd);
	}
}

static void sas_scsi_clear_queue_I_T(struct list_head *error_q,
				     struct domain_device *dev)
{
	struct scsi_cmnd *cmd, *n;

	list_for_each_entry_safe(cmd, n, error_q, eh_entry) {
		struct domain_device *x = cmd_to_domain_dev(cmd);

		if (x == dev)
			sas_eh_finish_cmd(cmd);
	}
}

static void sas_scsi_clear_queue_port(struct list_head *error_q,
				      struct asd_sas_port *port)
{
	struct scsi_cmnd *cmd, *n;

	list_for_each_entry_safe(cmd, n, error_q, eh_entry) {
		struct domain_device *dev = cmd_to_domain_dev(cmd);
		struct asd_sas_port *x = dev->port;

		if (x == port)
			sas_eh_finish_cmd(cmd);
	}
}

enum task_disposition {
	TASK_IS_DONE,
	TASK_IS_ABORTED,
	TASK_IS_AT_LU,
	TASK_IS_NOT_AT_HA,
	TASK_IS_NOT_AT_LU,
	TASK_ABORT_FAILED,
};

static enum task_disposition sas_scsi_find_task(struct sas_task *task)
{
	struct sas_ha_struct *ha = task->dev->port->ha;
	unsigned long flags;
	int i, res;
	struct sas_internal *si =
		to_sas_internal(task->dev->port->ha->core.shost->transportt);

	if (ha->lldd_max_execute_num > 1) {
		struct scsi_core *core = &ha->core;
		struct sas_task *t, *n;

		mutex_lock(&core->task_queue_flush);
		spin_lock_irqsave(&core->task_queue_lock, flags);
		list_for_each_entry_safe(t, n, &core->task_queue, list)
			if (task == t) {
				list_del_init(&t->list);
				break;
			}
		spin_unlock_irqrestore(&core->task_queue_lock, flags);
		mutex_unlock(&core->task_queue_flush);

		if (task == t)
			return TASK_IS_NOT_AT_HA;
	}

	for (i = 0; i < 5; i++) {
		SAS_DPRINTK("%s: aborting task 0x%p\n", __func__, task);
		res = si->dft->lldd_abort_task(task);

		spin_lock_irqsave(&task->task_state_lock, flags);
		if (task->task_state_flags & SAS_TASK_STATE_DONE) {
			spin_unlock_irqrestore(&task->task_state_lock, flags);
			SAS_DPRINTK("%s: task 0x%p is done\n", __func__,
				    task);
			return TASK_IS_DONE;
		}
		spin_unlock_irqrestore(&task->task_state_lock, flags);

		if (res == TMF_RESP_FUNC_COMPLETE) {
			SAS_DPRINTK("%s: task 0x%p is aborted\n",
				    __func__, task);
			return TASK_IS_ABORTED;
		} else if (si->dft->lldd_query_task) {
			SAS_DPRINTK("%s: querying task 0x%p\n",
				    __func__, task);
			res = si->dft->lldd_query_task(task);
			switch (res) {
			case TMF_RESP_FUNC_SUCC:
				SAS_DPRINTK("%s: task 0x%p at LU\n",
					    __func__, task);
				return TASK_IS_AT_LU;
			case TMF_RESP_FUNC_COMPLETE:
				SAS_DPRINTK("%s: task 0x%p not at LU\n",
					    __func__, task);
				return TASK_IS_NOT_AT_LU;
			case TMF_RESP_FUNC_FAILED:
                                SAS_DPRINTK("%s: task 0x%p failed to abort\n",
                                                __func__, task);
                                return TASK_ABORT_FAILED;
                        }

		}
	}
	return res;
}

static int sas_recover_lu(struct domain_device *dev, struct scsi_cmnd *cmd)
{
	int res = TMF_RESP_FUNC_FAILED;
	struct scsi_lun lun;
	struct sas_internal *i =
		to_sas_internal(dev->port->ha->core.shost->transportt);

	int_to_scsilun(cmd->device->lun, &lun);

	SAS_DPRINTK("eh: device %llx LUN %x has the task\n",
		    SAS_ADDR(dev->sas_addr),
		    cmd->device->lun);

	if (i->dft->lldd_abort_task_set)
		res = i->dft->lldd_abort_task_set(dev, lun.scsi_lun);

	if (res == TMF_RESP_FUNC_FAILED) {
		if (i->dft->lldd_clear_task_set)
			res = i->dft->lldd_clear_task_set(dev, lun.scsi_lun);
	}

	if (res == TMF_RESP_FUNC_FAILED) {
		if (i->dft->lldd_lu_reset)
			res = i->dft->lldd_lu_reset(dev, lun.scsi_lun);
	}

	return res;
}

static int sas_recover_I_T(struct domain_device *dev)
{
	int res = TMF_RESP_FUNC_FAILED;
	struct sas_internal *i =
		to_sas_internal(dev->port->ha->core.shost->transportt);

	SAS_DPRINTK("I_T nexus reset for dev %016llx\n",
		    SAS_ADDR(dev->sas_addr));

	if (i->dft->lldd_I_T_nexus_reset)
		res = i->dft->lldd_I_T_nexus_reset(dev);

	return res;
}

/* take a reference on the last known good phy for this device */
struct sas_phy *sas_get_local_phy(struct domain_device *dev)
{
	struct sas_ha_struct *ha = dev->port->ha;
	struct sas_phy *phy;
	unsigned long flags;

	/* a published domain device always has a valid phy, it may be
	 * stale, but it is never NULL
	 */
	BUG_ON(!dev->phy);

	spin_lock_irqsave(&ha->phy_port_lock, flags);
	phy = dev->phy;
	get_device(&phy->dev);
	spin_unlock_irqrestore(&ha->phy_port_lock, flags);

	return phy;
}
EXPORT_SYMBOL_GPL(sas_get_local_phy);

static void sas_wait_eh(struct domain_device *dev)
{
	struct sas_ha_struct *ha = dev->port->ha;
	DEFINE_WAIT(wait);

	if (dev_is_sata(dev)) {
		ata_port_wait_eh(dev->sata_dev.ap);
		return;
	}
 retry:
	spin_lock_irq(&ha->lock);

	while (test_bit(SAS_DEV_EH_PENDING, &dev->state)) {
		prepare_to_wait(&ha->eh_wait_q, &wait, TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&ha->lock);
		schedule();
		spin_lock_irq(&ha->lock);
	}
	finish_wait(&ha->eh_wait_q, &wait);

	spin_unlock_irq(&ha->lock);

	/* make sure SCSI EH is complete */
	if (scsi_host_in_recovery(ha->core.shost)) {
		msleep(10);
		goto retry;
	}
}
EXPORT_SYMBOL(sas_wait_eh);

static int sas_queue_reset(struct domain_device *dev, int reset_type, int lun, int wait)
{
	struct sas_ha_struct *ha = dev->port->ha;
	int scheduled = 0, tries = 100;

	/* ata: promote lun reset to bus reset */
	if (dev_is_sata(dev)) {
		sas_ata_schedule_reset(dev);
		if (wait)
			sas_ata_wait_eh(dev);
		return SUCCESS;
	}

	while (!scheduled && tries--) {
		spin_lock_irq(&ha->lock);
		if (!test_bit(SAS_DEV_EH_PENDING, &dev->state) &&
		    !test_bit(reset_type, &dev->state)) {
			scheduled = 1;
			ha->eh_active++;
			list_add_tail(&dev->ssp_dev.eh_list_node, &ha->eh_dev_q);
			set_bit(SAS_DEV_EH_PENDING, &dev->state);
			set_bit(reset_type, &dev->state);
			int_to_scsilun(lun, &dev->ssp_dev.reset_lun);
			scsi_schedule_eh(ha->core.shost);
		}
		spin_unlock_irq(&ha->lock);

		if (wait)
			sas_wait_eh(dev);

		if (scheduled)
			return SUCCESS;
	}

	SAS_DPRINTK("%s reset of %s failed\n",
		    reset_type == SAS_DEV_LU_RESET ? "LUN" : "Bus",
		    dev_name(&dev->rphy->dev));

	return FAILED;
}

int sas_eh_abort_handler(struct scsi_cmnd *cmd)
{
	int res;
	struct sas_task *task = TO_SAS_TASK(cmd);
	struct Scsi_Host *host = cmd->device->host;
	struct sas_internal *i = to_sas_internal(host->transportt);

	if (current != host->ehandler)
		return FAILED;

	if (!i->dft->lldd_abort_task)
		return FAILED;

	res = i->dft->lldd_abort_task(task);
	if (res == TMF_RESP_FUNC_SUCC || res == TMF_RESP_FUNC_COMPLETE)
		return SUCCESS;

	return FAILED;
}
EXPORT_SYMBOL_GPL(sas_eh_abort_handler);

/* Attempt to send a LUN reset message to a device */
int sas_eh_device_reset_handler(struct scsi_cmnd *cmd)
{
	int res;
	struct scsi_lun lun;
	struct Scsi_Host *host = cmd->device->host;
	struct domain_device *dev = cmd_to_domain_dev(cmd);
	struct sas_internal *i = to_sas_internal(host->transportt);

	if (current != host->ehandler)
		return sas_queue_reset(dev, SAS_DEV_LU_RESET, cmd->device->lun, 0);

	int_to_scsilun(cmd->device->lun, &lun);

	if (!i->dft->lldd_lu_reset)
		return FAILED;

	res = i->dft->lldd_lu_reset(dev, lun.scsi_lun);
	if (res == TMF_RESP_FUNC_SUCC || res == TMF_RESP_FUNC_COMPLETE)
		return SUCCESS;

	return FAILED;
}

int sas_eh_bus_reset_handler(struct scsi_cmnd *cmd)
{
	int res;
	struct Scsi_Host *host = cmd->device->host;
	struct domain_device *dev = cmd_to_domain_dev(cmd);
	struct sas_internal *i = to_sas_internal(host->transportt);

	if (current != host->ehandler)
		return sas_queue_reset(dev, SAS_DEV_RESET, 0, 0);

	if (!i->dft->lldd_I_T_nexus_reset)
		return FAILED;

	res = i->dft->lldd_I_T_nexus_reset(dev);
	if (res == TMF_RESP_FUNC_SUCC || res == TMF_RESP_FUNC_COMPLETE ||
	    res == -ENODEV)
		return SUCCESS;

	return FAILED;
}

/* Try to reset a device */
static int try_to_reset_cmd_device(struct scsi_cmnd *cmd)
{
	int res;
	struct Scsi_Host *shost = cmd->device->host;

	if (!shost->hostt->eh_device_reset_handler)
		goto try_bus_reset;

	res = shost->hostt->eh_device_reset_handler(cmd);
	if (res == SUCCESS)
		return res;

try_bus_reset:
	if (shost->hostt->eh_bus_reset_handler)
		return shost->hostt->eh_bus_reset_handler(cmd);

	return FAILED;
}

static void sas_eh_handle_sas_errors(struct Scsi_Host *shost, struct list_head *work_q)
{
	struct scsi_cmnd *cmd, *n;
	enum task_disposition res = TASK_IS_DONE;
	int tmf_resp, need_reset;
	struct sas_internal *i = to_sas_internal(shost->transportt);
	unsigned long flags;
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	LIST_HEAD(done);

	/* clean out any commands that won the completion vs eh race */
	list_for_each_entry_safe(cmd, n, work_q, eh_entry) {
		struct domain_device *dev = cmd_to_domain_dev(cmd);
		struct sas_task *task;

		spin_lock_irqsave(&dev->done_lock, flags);
		/* by this point the lldd has either observed
		 * SAS_HA_FROZEN and is leaving the task alone, or has
		 * won the race with eh and decided to complete it
		 */
		task = TO_SAS_TASK(cmd);
		spin_unlock_irqrestore(&dev->done_lock, flags);

		if (!task)
			list_move_tail(&cmd->eh_entry, &done);
	}

 Again:
	list_for_each_entry_safe(cmd, n, work_q, eh_entry) {
		struct sas_task *task = TO_SAS_TASK(cmd);

		list_del_init(&cmd->eh_entry);

		spin_lock_irqsave(&task->task_state_lock, flags);
		need_reset = task->task_state_flags & SAS_TASK_NEED_DEV_RESET;
		spin_unlock_irqrestore(&task->task_state_lock, flags);

		if (need_reset) {
			SAS_DPRINTK("%s: task 0x%p requests reset\n",
				    __func__, task);
			goto reset;
		}

		SAS_DPRINTK("trying to find task 0x%p\n", task);
		res = sas_scsi_find_task(task);

		cmd->eh_eflags = 0;

		switch (res) {
		case TASK_IS_NOT_AT_HA:
			SAS_DPRINTK("%s: task 0x%p is not at ha: %s\n",
				    __func__, task,
				    cmd->retries ? "retry" : "aborted");
			if (cmd->retries)
				cmd->retries--;
			sas_eh_finish_cmd(cmd);
			continue;
		case TASK_IS_DONE:
			SAS_DPRINTK("%s: task 0x%p is done\n", __func__,
				    task);
			sas_eh_defer_cmd(cmd);
			continue;
		case TASK_IS_ABORTED:
			SAS_DPRINTK("%s: task 0x%p is aborted\n",
				    __func__, task);
			sas_eh_defer_cmd(cmd);
			continue;
		case TASK_IS_AT_LU:
			SAS_DPRINTK("task 0x%p is at LU: lu recover\n", task);
 reset:
			tmf_resp = sas_recover_lu(task->dev, cmd);
			if (tmf_resp == TMF_RESP_FUNC_COMPLETE) {
				SAS_DPRINTK("dev %016llx LU %x is "
					    "recovered\n",
					    SAS_ADDR(task->dev),
					    cmd->device->lun);
				sas_eh_defer_cmd(cmd);
				sas_scsi_clear_queue_lu(work_q, cmd);
				goto Again;
			}
			/* fallthrough */
		case TASK_IS_NOT_AT_LU:
		case TASK_ABORT_FAILED:
			SAS_DPRINTK("task 0x%p is not at LU: I_T recover\n",
				    task);
			tmf_resp = sas_recover_I_T(task->dev);
			if (tmf_resp == TMF_RESP_FUNC_COMPLETE ||
			    tmf_resp == -ENODEV) {
				struct domain_device *dev = task->dev;
				SAS_DPRINTK("I_T %016llx recovered\n",
					    SAS_ADDR(task->dev->sas_addr));
				sas_eh_finish_cmd(cmd);
				sas_scsi_clear_queue_I_T(work_q, dev);
				goto Again;
			}
			/* Hammer time :-) */
			try_to_reset_cmd_device(cmd);
			if (i->dft->lldd_clear_nexus_port) {
				struct asd_sas_port *port = task->dev->port;
				SAS_DPRINTK("clearing nexus for port:%d\n",
					    port->id);
				res = i->dft->lldd_clear_nexus_port(port);
				if (res == TMF_RESP_FUNC_COMPLETE) {
					SAS_DPRINTK("clear nexus port:%d "
						    "succeeded\n", port->id);
					sas_eh_finish_cmd(cmd);
					sas_scsi_clear_queue_port(work_q,
								  port);
					goto Again;
				}
			}
			if (i->dft->lldd_clear_nexus_ha) {
				SAS_DPRINTK("clear nexus ha\n");
				res = i->dft->lldd_clear_nexus_ha(ha);
				if (res == TMF_RESP_FUNC_COMPLETE) {
					SAS_DPRINTK("clear nexus ha "
						    "succeeded\n");
					sas_eh_finish_cmd(cmd);
					goto clear_q;
				}
			}
			/* If we are here -- this means that no amount
			 * of effort could recover from errors.  Quite
			 * possibly the HA just disappeared.
			 */
			SAS_DPRINTK("error from  device %llx, LUN %x "
				    "couldn't be recovered in any way\n",
				    SAS_ADDR(task->dev->sas_addr),
				    cmd->device->lun);

			sas_eh_finish_cmd(cmd);
			goto clear_q;
		}
	}
 out:
	list_splice_tail(&done, work_q);
	list_splice_tail_init(&ha->eh_ata_q, work_q);
	return;

 clear_q:
	SAS_DPRINTK("--- Exit %s -- clear_q\n", __func__);
	list_for_each_entry_safe(cmd, n, work_q, eh_entry)
		sas_eh_finish_cmd(cmd);
	goto out;
}

static void sas_eh_handle_resets(struct Scsi_Host *shost)
{
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	struct sas_internal *i = to_sas_internal(shost->transportt);

	/* handle directed resets to sas devices */
	spin_lock_irq(&ha->lock);
	while (!list_empty(&ha->eh_dev_q)) {
		struct domain_device *dev;
		struct ssp_device *ssp;

		ssp = list_entry(ha->eh_dev_q.next, typeof(*ssp), eh_list_node);
		list_del_init(&ssp->eh_list_node);
		dev = container_of(ssp, typeof(*dev), ssp_dev);
		kref_get(&dev->kref);
		WARN_ONCE(dev_is_sata(dev), "ssp reset to ata device?\n");

		spin_unlock_irq(&ha->lock);

		if (test_and_clear_bit(SAS_DEV_LU_RESET, &dev->state))
			i->dft->lldd_lu_reset(dev, ssp->reset_lun.scsi_lun);

		if (test_and_clear_bit(SAS_DEV_RESET, &dev->state))
			i->dft->lldd_I_T_nexus_reset(dev);

		sas_put_device(dev);
		spin_lock_irq(&ha->lock);
		clear_bit(SAS_DEV_EH_PENDING, &dev->state);
		ha->eh_active--;
	}
	spin_unlock_irq(&ha->lock);
}


void sas_scsi_recover_host(struct Scsi_Host *shost)
{
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	LIST_HEAD(eh_work_q);
	int tries = 0;
	bool retry;

retry:
	tries++;
	retry = true;
	spin_lock_irq(shost->host_lock);
	list_splice_init(&shost->eh_cmd_q, &eh_work_q);
	spin_unlock_irq(shost->host_lock);

	SAS_DPRINTK("Enter %s busy: %d failed: %d\n",
		    __func__, shost->host_busy, shost->host_failed);
	/*
	 * Deal with commands that still have SAS tasks (i.e. they didn't
	 * complete via the normal sas_task completion mechanism),
	 * SAS_HA_FROZEN gives eh dominion over all sas_task completion.
	 */
	set_bit(SAS_HA_FROZEN, &ha->state);
	sas_eh_handle_sas_errors(shost, &eh_work_q);
	clear_bit(SAS_HA_FROZEN, &ha->state);
	if (list_empty(&eh_work_q))
		goto out;

	/*
	 * Now deal with SCSI commands that completed ok but have a an error
	 * code (and hopefully sense data) attached.  This is roughly what
	 * scsi_unjam_host does, but we skip scsi_eh_abort_cmds because any
	 * command we see here has no sas_task and is thus unknown to the HA.
	 */
	sas_ata_eh(shost, &eh_work_q, &ha->eh_done_q);
	if (!scsi_eh_get_sense(&eh_work_q, &ha->eh_done_q))
		scsi_eh_ready_devs(shost, &eh_work_q, &ha->eh_done_q);

out:
	if (ha->lldd_max_execute_num > 1)
		wake_up_process(ha->core.queue_thread);

	sas_eh_handle_resets(shost);

	/* now link into libata eh --- if we have any ata devices */
	sas_ata_strategy_handler(shost);

	scsi_eh_flush_done_q(&ha->eh_done_q);

	/* check if any new eh work was scheduled during the last run */
	spin_lock_irq(&ha->lock);
	if (ha->eh_active == 0) {
		shost->host_eh_scheduled = 0;
		retry = false;
	}
	spin_unlock_irq(&ha->lock);

	if (retry)
		goto retry;

	SAS_DPRINTK("--- Exit %s: busy: %d failed: %d tries: %d\n",
		    __func__, shost->host_busy, shost->host_failed, tries);
}

enum blk_eh_timer_return sas_scsi_timed_out(struct scsi_cmnd *cmd)
{
	scmd_dbg(cmd, "command %p timed out\n", cmd);

	return BLK_EH_NOT_HANDLED;
}

int sas_ioctl(struct scsi_device *sdev, int cmd, void __user *arg)
{
	struct domain_device *dev = sdev_to_domain_dev(sdev);

	if (dev_is_sata(dev))
		return ata_sas_scsi_ioctl(dev->sata_dev.ap, sdev, cmd, arg);

	return -EINVAL;
}

struct domain_device *sas_find_dev_by_rphy(struct sas_rphy *rphy)
{
	struct Scsi_Host *shost = dev_to_shost(rphy->dev.parent);
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	struct domain_device *found_dev = NULL;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&ha->phy_port_lock, flags);
	for (i = 0; i < ha->num_phys; i++) {
		struct asd_sas_port *port = ha->sas_port[i];
		struct domain_device *dev;

		spin_lock(&port->dev_list_lock);
		list_for_each_entry(dev, &port->dev_list, dev_list_node) {
			if (rphy == dev->rphy) {
				found_dev = dev;
				spin_unlock(&port->dev_list_lock);
				goto found;
			}
		}
		spin_unlock(&port->dev_list_lock);
	}
 found:
	spin_unlock_irqrestore(&ha->phy_port_lock, flags);

	return found_dev;
}

int sas_target_alloc(struct scsi_target *starget)
{
	struct sas_rphy *rphy = dev_to_rphy(starget->dev.parent);
	struct domain_device *found_dev = sas_find_dev_by_rphy(rphy);

	if (!found_dev)
		return -ENODEV;

	kref_get(&found_dev->kref);
	starget->hostdata = found_dev;
	return 0;
}

#define SAS_DEF_QD 256

int sas_slave_configure(struct scsi_device *scsi_dev)
{
	struct domain_device *dev = sdev_to_domain_dev(scsi_dev);
	struct sas_ha_struct *sas_ha;

	BUG_ON(dev->rphy->identify.device_type != SAS_END_DEVICE);

	if (dev_is_sata(dev)) {
		ata_sas_slave_configure(scsi_dev, dev->sata_dev.ap);
		return 0;
	}

	sas_ha = dev->port->ha;

	sas_read_port_mode_page(scsi_dev);

	if (scsi_dev->tagged_supported) {
		scsi_set_tag_type(scsi_dev, MSG_SIMPLE_TAG);
		scsi_activate_tcq(scsi_dev, SAS_DEF_QD);
	} else {
		SAS_DPRINTK("device %llx, LUN %x doesn't support "
			    "TCQ\n", SAS_ADDR(dev->sas_addr),
			    scsi_dev->lun);
		scsi_dev->tagged_supported = 0;
		scsi_set_tag_type(scsi_dev, 0);
		scsi_deactivate_tcq(scsi_dev, 1);
	}

	scsi_dev->allow_restart = 1;

	return 0;
}

int sas_change_queue_depth(struct scsi_device *sdev, int depth, int reason)
{
	struct domain_device *dev = sdev_to_domain_dev(sdev);

	if (dev_is_sata(dev))
		return __ata_change_queue_depth(dev->sata_dev.ap, sdev, depth,
						reason);

	switch (reason) {
	case SCSI_QDEPTH_DEFAULT:
	case SCSI_QDEPTH_RAMP_UP:
		if (!sdev->tagged_supported)
			depth = 1;
		scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), depth);
		break;
	case SCSI_QDEPTH_QFULL:
		scsi_track_queue_full(sdev, depth);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return depth;
}

int sas_change_queue_type(struct scsi_device *scsi_dev, int qt)
{
	struct domain_device *dev = sdev_to_domain_dev(scsi_dev);

	if (dev_is_sata(dev))
		return -EINVAL;

	if (!scsi_dev->tagged_supported)
		return 0;

	scsi_deactivate_tcq(scsi_dev, 1);

	scsi_set_tag_type(scsi_dev, qt);
	scsi_activate_tcq(scsi_dev, scsi_dev->queue_depth);

	return qt;
}

int sas_bios_param(struct scsi_device *scsi_dev,
			  struct block_device *bdev,
			  sector_t capacity, int *hsc)
{
	hsc[0] = 255;
	hsc[1] = 63;
	sector_div(capacity, 255*63);
	hsc[2] = capacity;

	return 0;
}

/* ---------- Task Collector Thread implementation ---------- */

static void sas_queue(struct sas_ha_struct *sas_ha)
{
	struct scsi_core *core = &sas_ha->core;
	unsigned long flags;
	LIST_HEAD(q);
	int can_queue;
	int res;
	struct sas_internal *i = to_sas_internal(core->shost->transportt);

	mutex_lock(&core->task_queue_flush);
	spin_lock_irqsave(&core->task_queue_lock, flags);
	while (!kthread_should_stop() &&
	       !list_empty(&core->task_queue) &&
	       !test_bit(SAS_HA_FROZEN, &sas_ha->state)) {

		can_queue = sas_ha->lldd_queue_size - core->task_queue_size;
		if (can_queue >= 0) {
			can_queue = core->task_queue_size;
			list_splice_init(&core->task_queue, &q);
		} else {
			struct list_head *a, *n;

			can_queue = sas_ha->lldd_queue_size;
			list_for_each_safe(a, n, &core->task_queue) {
				list_move_tail(a, &q);
				if (--can_queue == 0)
					break;
			}
			can_queue = sas_ha->lldd_queue_size;
		}
		core->task_queue_size -= can_queue;
		spin_unlock_irqrestore(&core->task_queue_lock, flags);
		{
			struct sas_task *task = list_entry(q.next,
							   struct sas_task,
							   list);
			list_del_init(&q);
			res = i->dft->lldd_execute_task(task, can_queue,
							GFP_KERNEL);
			if (unlikely(res))
				__list_add(&q, task->list.prev, &task->list);
		}
		spin_lock_irqsave(&core->task_queue_lock, flags);
		if (res) {
			list_splice_init(&q, &core->task_queue); /*at head*/
			core->task_queue_size += can_queue;
		}
	}
	spin_unlock_irqrestore(&core->task_queue_lock, flags);
	mutex_unlock(&core->task_queue_flush);
}

/**
 * sas_queue_thread -- The Task Collector thread
 * @_sas_ha: pointer to struct sas_ha
 */
static int sas_queue_thread(void *_sas_ha)
{
	struct sas_ha_struct *sas_ha = _sas_ha;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		sas_queue(sas_ha);
		if (kthread_should_stop())
			break;
	}

	return 0;
}

int sas_init_queue(struct sas_ha_struct *sas_ha)
{
	struct scsi_core *core = &sas_ha->core;

	spin_lock_init(&core->task_queue_lock);
	mutex_init(&core->task_queue_flush);
	core->task_queue_size = 0;
	INIT_LIST_HEAD(&core->task_queue);

	core->queue_thread = kthread_run(sas_queue_thread, sas_ha,
					 "sas_queue_%d", core->shost->host_no);
	if (IS_ERR(core->queue_thread))
		return PTR_ERR(core->queue_thread);
	return 0;
}

void sas_shutdown_queue(struct sas_ha_struct *sas_ha)
{
	unsigned long flags;
	struct scsi_core *core = &sas_ha->core;
	struct sas_task *task, *n;

	kthread_stop(core->queue_thread);

	if (!list_empty(&core->task_queue))
		SAS_DPRINTK("HA: %llx: scsi core task queue is NOT empty!?\n",
			    SAS_ADDR(sas_ha->sas_addr));

	spin_lock_irqsave(&core->task_queue_lock, flags);
	list_for_each_entry_safe(task, n, &core->task_queue, list) {
		struct scsi_cmnd *cmd = task->uldd_task;

		list_del_init(&task->list);

		ASSIGN_SAS_TASK(cmd, NULL);
		sas_free_task(task);
		cmd->result = DID_ABORT << 16;
		cmd->scsi_done(cmd);
	}
	spin_unlock_irqrestore(&core->task_queue_lock, flags);
}

/*
 * Tell an upper layer that it needs to initiate an abort for a given task.
 * This should only ever be called by an LLDD.
 */
void sas_task_abort(struct sas_task *task)
{
	struct scsi_cmnd *sc = task->uldd_task;

	/* Escape for libsas internal commands */
	if (!sc) {
		struct sas_task_slow *slow = task->slow_task;

		if (!slow)
			return;
		if (!del_timer(&slow->timer))
			return;
		slow->timer.function(slow->timer.data);
		return;
	}

	if (dev_is_sata(task->dev)) {
		sas_ata_task_abort(task);
	} else {
		struct request_queue *q = sc->device->request_queue;
		unsigned long flags;

		spin_lock_irqsave(q->queue_lock, flags);
		blk_abort_request(sc->request);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

void sas_target_destroy(struct scsi_target *starget)
{
	struct domain_device *found_dev = starget->hostdata;

	if (!found_dev)
		return;

	starget->hostdata = NULL;
	sas_put_device(found_dev);
}

static void sas_parse_addr(u8 *sas_addr, const char *p)
{
	int i;
	for (i = 0; i < SAS_ADDR_SIZE; i++) {
		u8 h, l;
		if (!*p)
			break;
		h = isdigit(*p) ? *p-'0' : toupper(*p)-'A'+10;
		p++;
		l = isdigit(*p) ? *p-'0' : toupper(*p)-'A'+10;
		p++;
		sas_addr[i] = (h<<4) | l;
	}
}

#define SAS_STRING_ADDR_SIZE	16

int sas_request_addr(struct Scsi_Host *shost, u8 *addr)
{
	int res;
	const struct firmware *fw;

	res = request_firmware(&fw, "sas_addr", &shost->shost_gendev);
	if (res)
		return res;

	if (fw->size < SAS_STRING_ADDR_SIZE) {
		res = -ENODEV;
		goto out;
	}

	sas_parse_addr(addr, fw->data);

out:
	release_firmware(fw);
	return res;
}
EXPORT_SYMBOL_GPL(sas_request_addr);

EXPORT_SYMBOL_GPL(sas_queuecommand);
EXPORT_SYMBOL_GPL(sas_target_alloc);
EXPORT_SYMBOL_GPL(sas_slave_configure);
EXPORT_SYMBOL_GPL(sas_change_queue_depth);
EXPORT_SYMBOL_GPL(sas_change_queue_type);
EXPORT_SYMBOL_GPL(sas_bios_param);
EXPORT_SYMBOL_GPL(sas_task_abort);
EXPORT_SYMBOL_GPL(sas_phy_reset);
EXPORT_SYMBOL_GPL(sas_eh_device_reset_handler);
EXPORT_SYMBOL_GPL(sas_eh_bus_reset_handler);
EXPORT_SYMBOL_GPL(sas_target_destroy);
EXPORT_SYMBOL_GPL(sas_ioctl);
