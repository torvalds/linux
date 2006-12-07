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

#include "sas_internal.h"

#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_sas.h>
#include "../scsi_sas_internal.h"
#include "../scsi_transport_api.h"

#include <linux/err.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>

/* ---------- SCSI Host glue ---------- */

#define TO_SAS_TASK(_scsi_cmd)  ((void *)(_scsi_cmd)->host_scribble)
#define ASSIGN_SAS_TASK(_sc, _t) do { (_sc)->host_scribble = (void *) _t; } while (0)

static void sas_scsi_task_done(struct sas_task *task)
{
	struct task_status_struct *ts = &task->task_status;
	struct scsi_cmnd *sc = task->uldd_task;
	struct sas_ha_struct *sas_ha = SHOST_TO_SAS_HA(sc->device->host);
	unsigned ts_flags = task->task_state_flags;
	int hs = 0, stat = 0;

	if (unlikely(!sc)) {
		SAS_DPRINTK("task_done called with non existing SCSI cmnd!\n");
		list_del_init(&task->list);
		sas_free_task(task);
		return;
	}

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
			sc->resid = ts->residual;
			if (sc->request_bufflen - sc->resid < sc->underflow)
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
		case SAM_CHECK_COND:
			memcpy(sc->sense_buffer, ts->buf,
			       max(SCSI_SENSE_BUFFERSIZE, ts->buf_valid_size));
			stat = SAM_CHECK_COND;
			break;
		default:
			stat = ts->stat;
			break;
		}
	}
	ASSIGN_SAS_TASK(sc, NULL);
	sc->result = (hs << 16) | stat;
	list_del_init(&task->list);
	sas_free_task(task);
	/* This is very ugly but this is how SCSI Core works. */
	if (ts_flags & SAS_TASK_STATE_ABORTED)
		scsi_eh_finish_cmd(sc, &sas_ha->eh_done_q);
	else
		sc->scsi_done(sc);
}

static enum task_attribute sas_scsi_get_task_attr(struct scsi_cmnd *cmd)
{
	enum task_attribute ta = TASK_ATTR_SIMPLE;
	if (cmd->request && blk_rq_tagged(cmd->request)) {
		if (cmd->device->ordered_tags &&
		    (cmd->request->cmd_flags & REQ_HARDBARRIER))
			ta = TASK_ATTR_HOQ;
	}
	return ta;
}

static struct sas_task *sas_create_task(struct scsi_cmnd *cmd,
					       struct domain_device *dev,
					       gfp_t gfp_flags)
{
	struct sas_task *task = sas_alloc_task(gfp_flags);
	struct scsi_lun lun;

	if (!task)
		return NULL;

	*(u32 *)cmd->sense_buffer = 0;
	task->uldd_task = cmd;
	ASSIGN_SAS_TASK(cmd, task);

	task->dev = dev;
	task->task_proto = task->dev->tproto; /* BUG_ON(!SSP) */

	task->ssp_task.retry_count = 1;
	int_to_scsilun(cmd->device->lun, &lun);
	memcpy(task->ssp_task.LUN, &lun.scsi_lun, 8);
	task->ssp_task.task_attr = sas_scsi_get_task_attr(cmd);
	memcpy(task->ssp_task.cdb, cmd->cmnd, 16);

	task->scatter = cmd->request_buffer;
	task->num_scatter = cmd->use_sg;
	task->total_xfer_len = cmd->request_bufflen;
	task->data_dir = cmd->sc_data_direction;

	task->task_done = sas_scsi_task_done;

	return task;
}

static int sas_queue_up(struct sas_task *task)
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
	up(&core->queue_thread_sema);

	return 0;
}

/**
 * sas_queuecommand -- Enqueue a command for processing
 * @parameters: See SCSI Core documentation
 *
 * Note: XXX: Remove the host unlock/lock pair when SCSI Core can
 * call us without holding an IRQ spinlock...
 */
int sas_queuecommand(struct scsi_cmnd *cmd,
		     void (*scsi_done)(struct scsi_cmnd *))
{
	int res = 0;
	struct domain_device *dev = cmd_to_domain_dev(cmd);
	struct Scsi_Host *host = cmd->device->host;
	struct sas_internal *i = to_sas_internal(host->transportt);

	spin_unlock_irq(host->host_lock);

	{
		struct sas_ha_struct *sas_ha = dev->port->ha;
		struct sas_task *task;

		res = -ENOMEM;
		task = sas_create_task(cmd, dev, GFP_ATOMIC);
		if (!task)
			goto out;

		cmd->scsi_done = scsi_done;
		/* Queue up, Direct Mode or Task Collector Mode. */
		if (sas_ha->lldd_max_execute_num < 2)
			res = i->dft->lldd_execute_task(task, 1, GFP_ATOMIC);
		else
			res = sas_queue_up(task);

		/* Examine */
		if (res) {
			SAS_DPRINTK("lldd_execute_task returned: %d\n", res);
			ASSIGN_SAS_TASK(cmd, NULL);
			sas_free_task(task);
			if (res == -SAS_QUEUE_FULL) {
				cmd->result = DID_SOFT_ERROR << 16; /* retry */
				res = 0;
				scsi_done(cmd);
			}
			goto out;
		}
	}
out:
	spin_lock_irq(host->host_lock);
	return res;
}

static void sas_scsi_clear_queue_lu(struct list_head *error_q, struct scsi_cmnd *my_cmd)
{
	struct scsi_cmnd *cmd, *n;

	list_for_each_entry_safe(cmd, n, error_q, eh_entry) {
		if (cmd == my_cmd)
			list_del_init(&cmd->eh_entry);
	}
}

static void sas_scsi_clear_queue_I_T(struct list_head *error_q,
				     struct domain_device *dev)
{
	struct scsi_cmnd *cmd, *n;

	list_for_each_entry_safe(cmd, n, error_q, eh_entry) {
		struct domain_device *x = cmd_to_domain_dev(cmd);

		if (x == dev)
			list_del_init(&cmd->eh_entry);
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
			list_del_init(&cmd->eh_entry);
	}
}

enum task_disposition {
	TASK_IS_DONE,
	TASK_IS_ABORTED,
	TASK_IS_AT_LU,
	TASK_IS_NOT_AT_LU,
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

		spin_lock_irqsave(&core->task_queue_lock, flags);
		list_for_each_entry_safe(t, n, &core->task_queue, list) {
			if (task == t) {
				list_del_init(&t->list);
				spin_unlock_irqrestore(&core->task_queue_lock,
						       flags);
				SAS_DPRINTK("%s: task 0x%p aborted from "
					    "task_queue\n",
					    __FUNCTION__, task);
				return TASK_IS_ABORTED;
			}
		}
		spin_unlock_irqrestore(&core->task_queue_lock, flags);
	}

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_INITIATOR_ABORTED) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		SAS_DPRINTK("%s: task 0x%p already aborted\n",
			    __FUNCTION__, task);
		return TASK_IS_ABORTED;
	}
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	for (i = 0; i < 5; i++) {
		SAS_DPRINTK("%s: aborting task 0x%p\n", __FUNCTION__, task);
		res = si->dft->lldd_abort_task(task);

		spin_lock_irqsave(&task->task_state_lock, flags);
		if (task->task_state_flags & SAS_TASK_STATE_DONE) {
			spin_unlock_irqrestore(&task->task_state_lock, flags);
			SAS_DPRINTK("%s: task 0x%p is done\n", __FUNCTION__,
				    task);
			return TASK_IS_DONE;
		}
		spin_unlock_irqrestore(&task->task_state_lock, flags);

		if (res == TMF_RESP_FUNC_COMPLETE) {
			SAS_DPRINTK("%s: task 0x%p is aborted\n",
				    __FUNCTION__, task);
			return TASK_IS_ABORTED;
		} else if (si->dft->lldd_query_task) {
			SAS_DPRINTK("%s: querying task 0x%p\n",
				    __FUNCTION__, task);
			res = si->dft->lldd_query_task(task);
			if (res == TMF_RESP_FUNC_SUCC) {
				SAS_DPRINTK("%s: task 0x%p at LU\n",
					    __FUNCTION__, task);
				return TASK_IS_AT_LU;
			} else if (res == TMF_RESP_FUNC_COMPLETE) {
				SAS_DPRINTK("%s: task 0x%p not at LU\n",
					    __FUNCTION__, task);
				return TASK_IS_NOT_AT_LU;
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

void sas_scsi_recover_host(struct Scsi_Host *shost)
{
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	unsigned long flags;
	LIST_HEAD(error_q);
	struct scsi_cmnd *cmd, *n;
	enum task_disposition res = TASK_IS_DONE;
	int tmf_resp;
	struct sas_internal *i = to_sas_internal(shost->transportt);

	spin_lock_irqsave(shost->host_lock, flags);
	list_splice_init(&shost->eh_cmd_q, &error_q);
	spin_unlock_irqrestore(shost->host_lock, flags);

	SAS_DPRINTK("Enter %s\n", __FUNCTION__);

	/* All tasks on this list were marked SAS_TASK_STATE_ABORTED
	 * by sas_scsi_timed_out() callback.
	 */
Again:
	SAS_DPRINTK("going over list...\n");
	list_for_each_entry_safe(cmd, n, &error_q, eh_entry) {
		struct sas_task *task = TO_SAS_TASK(cmd);
		list_del_init(&cmd->eh_entry);

		if (!task) {
			SAS_DPRINTK("%s: taskless cmd?!\n", __FUNCTION__);
			continue;
		}
		SAS_DPRINTK("trying to find task 0x%p\n", task);
		res = sas_scsi_find_task(task);

		cmd->eh_eflags = 0;

		switch (res) {
		case TASK_IS_DONE:
			SAS_DPRINTK("%s: task 0x%p is done\n", __FUNCTION__,
				    task);
			task->task_done(task);
			continue;
		case TASK_IS_ABORTED:
			SAS_DPRINTK("%s: task 0x%p is aborted\n",
				    __FUNCTION__, task);
			task->task_done(task);
			continue;
		case TASK_IS_AT_LU:
			SAS_DPRINTK("task 0x%p is at LU: lu recover\n", task);
			tmf_resp = sas_recover_lu(task->dev, cmd);
			if (tmf_resp == TMF_RESP_FUNC_COMPLETE) {
				SAS_DPRINTK("dev %016llx LU %x is "
					    "recovered\n",
					    SAS_ADDR(task->dev),
					    cmd->device->lun);
				task->task_done(task);
				sas_scsi_clear_queue_lu(&error_q, cmd);
				goto Again;
			}
			/* fallthrough */
		case TASK_IS_NOT_AT_LU:
			SAS_DPRINTK("task 0x%p is not at LU: I_T recover\n",
				    task);
			tmf_resp = sas_recover_I_T(task->dev);
			if (tmf_resp == TMF_RESP_FUNC_COMPLETE) {
				SAS_DPRINTK("I_T %016llx recovered\n",
					    SAS_ADDR(task->dev->sas_addr));
				task->task_done(task);
				sas_scsi_clear_queue_I_T(&error_q, task->dev);
				goto Again;
			}
			/* Hammer time :-) */
			if (i->dft->lldd_clear_nexus_port) {
				struct asd_sas_port *port = task->dev->port;
				SAS_DPRINTK("clearing nexus for port:%d\n",
					    port->id);
				res = i->dft->lldd_clear_nexus_port(port);
				if (res == TMF_RESP_FUNC_COMPLETE) {
					SAS_DPRINTK("clear nexus port:%d "
						    "succeeded\n", port->id);
					task->task_done(task);
					sas_scsi_clear_queue_port(&error_q,
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
					task->task_done(task);
					goto out;
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

			task->task_done(task);
			goto clear_q;
		}
	}
out:
	scsi_eh_flush_done_q(&ha->eh_done_q);
	SAS_DPRINTK("--- Exit %s\n", __FUNCTION__);
	return;
clear_q:
	SAS_DPRINTK("--- Exit %s -- clear_q\n", __FUNCTION__);
	list_for_each_entry_safe(cmd, n, &error_q, eh_entry) {
		struct sas_task *task = TO_SAS_TASK(cmd);
		list_del_init(&cmd->eh_entry);
		task->task_done(task);
	}
}

enum scsi_eh_timer_return sas_scsi_timed_out(struct scsi_cmnd *cmd)
{
	struct sas_task *task = TO_SAS_TASK(cmd);
	unsigned long flags;

	if (!task) {
		SAS_DPRINTK("command 0x%p, task 0x%p, gone: EH_HANDLED\n",
			    cmd, task);
		return EH_HANDLED;
	}

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_INITIATOR_ABORTED) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		SAS_DPRINTK("command 0x%p, task 0x%p, aborted by initiator: "
			    "EH_NOT_HANDLED\n", cmd, task);
		return EH_NOT_HANDLED;
	}
	if (task->task_state_flags & SAS_TASK_STATE_DONE) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		SAS_DPRINTK("command 0x%p, task 0x%p, timed out: EH_HANDLED\n",
			    cmd, task);
		return EH_HANDLED;
	}
	task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	SAS_DPRINTK("command 0x%p, task 0x%p, timed out: EH_NOT_HANDLED\n",
		    cmd, task);

	return EH_NOT_HANDLED;
}

struct domain_device *sas_find_dev_by_rphy(struct sas_rphy *rphy)
{
	struct Scsi_Host *shost = dev_to_shost(rphy->dev.parent);
	struct sas_ha_struct *ha = SHOST_TO_SAS_HA(shost);
	struct domain_device *found_dev = NULL;
	int i;

	spin_lock(&ha->phy_port_lock);
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
	spin_unlock(&ha->phy_port_lock);

	return found_dev;
}

static inline struct domain_device *sas_find_target(struct scsi_target *starget)
{
	struct sas_rphy *rphy = dev_to_rphy(starget->dev.parent);

	return sas_find_dev_by_rphy(rphy);
}

int sas_target_alloc(struct scsi_target *starget)
{
	struct domain_device *found_dev = sas_find_target(starget);

	if (!found_dev)
		return -ENODEV;

	starget->hostdata = found_dev;
	return 0;
}

#define SAS_DEF_QD 32
#define SAS_MAX_QD 64

int sas_slave_configure(struct scsi_device *scsi_dev)
{
	struct domain_device *dev = sdev_to_domain_dev(scsi_dev);
	struct sas_ha_struct *sas_ha;

	BUG_ON(dev->rphy->identify.device_type != SAS_END_DEVICE);

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

	return 0;
}

void sas_slave_destroy(struct scsi_device *scsi_dev)
{
}

int sas_change_queue_depth(struct scsi_device *scsi_dev, int new_depth)
{
	int res = min(new_depth, SAS_MAX_QD);

	if (scsi_dev->tagged_supported)
		scsi_adjust_queue_depth(scsi_dev, scsi_get_tag_type(scsi_dev),
					res);
	else {
		struct domain_device *dev = sdev_to_domain_dev(scsi_dev);
		sas_printk("device %llx LUN %x queue depth changed to 1\n",
			   SAS_ADDR(dev->sas_addr),
			   scsi_dev->lun);
		scsi_adjust_queue_depth(scsi_dev, 0, 1);
		res = 1;
	}

	return res;
}

int sas_change_queue_type(struct scsi_device *scsi_dev, int qt)
{
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

	spin_lock_irqsave(&core->task_queue_lock, flags);
	while (!core->queue_thread_kill &&
	       !list_empty(&core->task_queue)) {

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
}

static DECLARE_COMPLETION(queue_th_comp);

/**
 * sas_queue_thread -- The Task Collector thread
 * @_sas_ha: pointer to struct sas_ha
 */
static int sas_queue_thread(void *_sas_ha)
{
	struct sas_ha_struct *sas_ha = _sas_ha;
	struct scsi_core *core = &sas_ha->core;

	daemonize("sas_queue_%d", core->shost->host_no);
	current->flags |= PF_NOFREEZE;

	complete(&queue_th_comp);

	while (1) {
		down_interruptible(&core->queue_thread_sema);
		sas_queue(sas_ha);
		if (core->queue_thread_kill)
			break;
	}

	complete(&queue_th_comp);

	return 0;
}

int sas_init_queue(struct sas_ha_struct *sas_ha)
{
	int res;
	struct scsi_core *core = &sas_ha->core;

	spin_lock_init(&core->task_queue_lock);
	core->task_queue_size = 0;
	INIT_LIST_HEAD(&core->task_queue);
	init_MUTEX_LOCKED(&core->queue_thread_sema);

	res = kernel_thread(sas_queue_thread, sas_ha, 0);
	if (res >= 0)
		wait_for_completion(&queue_th_comp);

	return res < 0 ? res : 0;
}

void sas_shutdown_queue(struct sas_ha_struct *sas_ha)
{
	unsigned long flags;
	struct scsi_core *core = &sas_ha->core;
	struct sas_task *task, *n;

	init_completion(&queue_th_comp);
	core->queue_thread_kill = 1;
	up(&core->queue_thread_sema);
	wait_for_completion(&queue_th_comp);

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

static int do_sas_task_abort(struct sas_task *task)
{
	struct scsi_cmnd *sc = task->uldd_task;
	struct sas_internal *si =
		to_sas_internal(task->dev->port->ha->core.shost->transportt);
	unsigned long flags;
	int res;

	spin_lock_irqsave(&task->task_state_lock, flags);
	if (task->task_state_flags & SAS_TASK_STATE_ABORTED) {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		SAS_DPRINTK("%s: Task %p already aborted.\n", __FUNCTION__,
			    task);
		return 0;
	}

	task->task_state_flags |= SAS_TASK_INITIATOR_ABORTED;
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE))
		task->task_state_flags |= SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	if (!si->dft->lldd_abort_task)
		return -ENODEV;

	res = si->dft->lldd_abort_task(task);
	if ((task->task_state_flags & SAS_TASK_STATE_DONE) ||
	    (res == TMF_RESP_FUNC_COMPLETE))
	{
		/* SMP commands don't have scsi_cmds(?) */
		if (!sc) {
			task->task_done(task);
			return 0;
		}
		scsi_req_abort_cmd(sc);
		scsi_schedule_eh(sc->device->host);
		return 0;
	}

	spin_lock_irqsave(&task->task_state_lock, flags);
	task->task_state_flags &= ~SAS_TASK_INITIATOR_ABORTED;
	if (!(task->task_state_flags & SAS_TASK_STATE_DONE))
		task->task_state_flags &= ~SAS_TASK_STATE_ABORTED;
	spin_unlock_irqrestore(&task->task_state_lock, flags);

	return -EAGAIN;
}

void sas_task_abort(struct work_struct *work)
{
	struct sas_task *task =
		container_of(work, struct sas_task, abort_work);
	int i;

	for (i = 0; i < 5; i++)
		if (!do_sas_task_abort(task))
			return;

	SAS_DPRINTK("%s: Could not kill task!\n", __FUNCTION__);
}

EXPORT_SYMBOL_GPL(sas_queuecommand);
EXPORT_SYMBOL_GPL(sas_target_alloc);
EXPORT_SYMBOL_GPL(sas_slave_configure);
EXPORT_SYMBOL_GPL(sas_slave_destroy);
EXPORT_SYMBOL_GPL(sas_change_queue_depth);
EXPORT_SYMBOL_GPL(sas_change_queue_type);
EXPORT_SYMBOL_GPL(sas_bios_param);
EXPORT_SYMBOL_GPL(sas_task_abort);
EXPORT_SYMBOL_GPL(sas_phy_reset);
