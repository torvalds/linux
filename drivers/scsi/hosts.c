// SPDX-License-Identifier: GPL-2.0-only
/*
 *  hosts.c Copyright (C) 1992 Drew Eckhardt
 *          Copyright (C) 1993, 1994, 1995 Eric Youngdale
 *          Copyright (C) 2002-2003 Christoph Hellwig
 *
 *  mid to lowlevel SCSI driver interface
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 *  Added QLOGIC QLA1280 SCSI controller kernel host support. 
 *     August 4, 1999 Fred Lewis, Intel DuPont
 *
 *  Updated to reflect the new initialization scheme for the higher 
 *  level of scsi drivers (sd/sr/st)
 *  September 17, 2000 Torben Mathiasen <tmm@image.dk>
 *
 *  Restructured scsi_host lists and associated functions.
 *  September 04, 2002 Mike Anderson (andmike@us.ibm.com)
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/transport_class.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/idr.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_cmnd.h>

#include "scsi_priv.h"
#include "scsi_logging.h"


static int shost_eh_deadline = -1;

module_param_named(eh_deadline, shost_eh_deadline, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(eh_deadline,
		 "SCSI EH timeout in seconds (should be between 0 and 2^31-1)");

static DEFINE_IDA(host_index_ida);


static void scsi_host_cls_release(struct device *dev)
{
	put_device(&class_to_shost(dev)->shost_gendev);
}

static struct class shost_class = {
	.name		= "scsi_host",
	.dev_release	= scsi_host_cls_release,
};

/**
 *	scsi_host_set_state - Take the given host through the host state model.
 *	@shost:	scsi host to change the state of.
 *	@state:	state to change to.
 *
 *	Returns zero if unsuccessful or an error if the requested
 *	transition is illegal.
 **/
int scsi_host_set_state(struct Scsi_Host *shost, enum scsi_host_state state)
{
	enum scsi_host_state oldstate = shost->shost_state;

	if (state == oldstate)
		return 0;

	switch (state) {
	case SHOST_CREATED:
		/* There are no legal states that come back to
		 * created.  This is the manually initialised start
		 * state */
		goto illegal;

	case SHOST_RUNNING:
		switch (oldstate) {
		case SHOST_CREATED:
		case SHOST_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_RECOVERY:
		switch (oldstate) {
		case SHOST_RUNNING:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_CANCEL:
		switch (oldstate) {
		case SHOST_CREATED:
		case SHOST_RUNNING:
		case SHOST_CANCEL_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_DEL:
		switch (oldstate) {
		case SHOST_CANCEL:
		case SHOST_DEL_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_CANCEL_RECOVERY:
		switch (oldstate) {
		case SHOST_CANCEL:
		case SHOST_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;

	case SHOST_DEL_RECOVERY:
		switch (oldstate) {
		case SHOST_CANCEL_RECOVERY:
			break;
		default:
			goto illegal;
		}
		break;
	}
	shost->shost_state = state;
	return 0;

 illegal:
	SCSI_LOG_ERROR_RECOVERY(1,
				shost_printk(KERN_ERR, shost,
					     "Illegal host state transition"
					     "%s->%s\n",
					     scsi_host_state_name(oldstate),
					     scsi_host_state_name(state)));
	return -EINVAL;
}

/**
 * scsi_remove_host - remove a scsi host
 * @shost:	a pointer to a scsi host to remove
 **/
void scsi_remove_host(struct Scsi_Host *shost)
{
	unsigned long flags;

	mutex_lock(&shost->scan_mutex);
	spin_lock_irqsave(shost->host_lock, flags);
	if (scsi_host_set_state(shost, SHOST_CANCEL))
		if (scsi_host_set_state(shost, SHOST_CANCEL_RECOVERY)) {
			spin_unlock_irqrestore(shost->host_lock, flags);
			mutex_unlock(&shost->scan_mutex);
			return;
		}
	spin_unlock_irqrestore(shost->host_lock, flags);

	scsi_autopm_get_host(shost);
	flush_workqueue(shost->tmf_work_q);
	scsi_forget_host(shost);
	mutex_unlock(&shost->scan_mutex);
	scsi_proc_host_rm(shost);

	spin_lock_irqsave(shost->host_lock, flags);
	if (scsi_host_set_state(shost, SHOST_DEL))
		BUG_ON(scsi_host_set_state(shost, SHOST_DEL_RECOVERY));
	spin_unlock_irqrestore(shost->host_lock, flags);

	transport_unregister_device(&shost->shost_gendev);
	device_unregister(&shost->shost_dev);
	device_del(&shost->shost_gendev);
}
EXPORT_SYMBOL(scsi_remove_host);

/**
 * scsi_add_host_with_dma - add a scsi host with dma device
 * @shost:	scsi host pointer to add
 * @dev:	a struct device of type scsi class
 * @dma_dev:	dma device for the host
 *
 * Note: You rarely need to worry about this unless you're in a
 * virtualised host environments, so use the simpler scsi_add_host()
 * function instead.
 *
 * Return value: 
 * 	0 on success / != 0 for error
 **/
int scsi_add_host_with_dma(struct Scsi_Host *shost, struct device *dev,
			   struct device *dma_dev)
{
	struct scsi_host_template *sht = shost->hostt;
	int error = -EINVAL;

	shost_printk(KERN_INFO, shost, "%s\n",
			sht->info ? sht->info(shost) : sht->name);

	if (!shost->can_queue) {
		shost_printk(KERN_ERR, shost,
			     "can_queue = 0 no longer supported\n");
		goto fail;
	}

	/* Use min_t(int, ...) in case shost->can_queue exceeds SHRT_MAX */
	shost->cmd_per_lun = min_t(int, shost->cmd_per_lun,
				   shost->can_queue);

	error = scsi_init_sense_cache(shost);
	if (error)
		goto fail;

	error = scsi_mq_setup_tags(shost);
	if (error)
		goto fail;

	if (!shost->shost_gendev.parent)
		shost->shost_gendev.parent = dev ? dev : &platform_bus;
	if (!dma_dev)
		dma_dev = shost->shost_gendev.parent;

	shost->dma_dev = dma_dev;

	/*
	 * Increase usage count temporarily here so that calling
	 * scsi_autopm_put_host() will trigger runtime idle if there is
	 * nothing else preventing suspending the device.
	 */
	pm_runtime_get_noresume(&shost->shost_gendev);
	pm_runtime_set_active(&shost->shost_gendev);
	pm_runtime_enable(&shost->shost_gendev);
	device_enable_async_suspend(&shost->shost_gendev);

	error = device_add(&shost->shost_gendev);
	if (error)
		goto out_disable_runtime_pm;

	scsi_host_set_state(shost, SHOST_RUNNING);
	get_device(shost->shost_gendev.parent);

	device_enable_async_suspend(&shost->shost_dev);

	get_device(&shost->shost_gendev);
	error = device_add(&shost->shost_dev);
	if (error)
		goto out_del_gendev;

	if (shost->transportt->host_size) {
		shost->shost_data = kzalloc(shost->transportt->host_size,
					 GFP_KERNEL);
		if (shost->shost_data == NULL) {
			error = -ENOMEM;
			goto out_del_dev;
		}
	}

	if (shost->transportt->create_work_queue) {
		snprintf(shost->work_q_name, sizeof(shost->work_q_name),
			 "scsi_wq_%d", shost->host_no);
		shost->work_q = alloc_workqueue("%s",
			WQ_SYSFS | __WQ_LEGACY | WQ_MEM_RECLAIM | WQ_UNBOUND,
			1, shost->work_q_name);

		if (!shost->work_q) {
			error = -EINVAL;
			goto out_del_dev;
		}
	}

	error = scsi_sysfs_add_host(shost);
	if (error)
		goto out_del_dev;

	scsi_proc_host_add(shost);
	scsi_autopm_put_host(shost);
	return error;

	/*
	 * Any host allocation in this function will be freed in
	 * scsi_host_dev_release().
	 */
 out_del_dev:
	device_del(&shost->shost_dev);
 out_del_gendev:
	/*
	 * Host state is SHOST_RUNNING so we have to explicitly release
	 * ->shost_dev.
	 */
	put_device(&shost->shost_dev);
	device_del(&shost->shost_gendev);
 out_disable_runtime_pm:
	device_disable_async_suspend(&shost->shost_gendev);
	pm_runtime_disable(&shost->shost_gendev);
	pm_runtime_set_suspended(&shost->shost_gendev);
	pm_runtime_put_noidle(&shost->shost_gendev);
 fail:
	return error;
}
EXPORT_SYMBOL(scsi_add_host_with_dma);

static void scsi_host_dev_release(struct device *dev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);
	struct device *parent = dev->parent;

	scsi_proc_hostdir_rm(shost->hostt);

	/* Wait for functions invoked through call_rcu(&scmd->rcu, ...) */
	rcu_barrier();

	if (shost->tmf_work_q)
		destroy_workqueue(shost->tmf_work_q);
	if (shost->ehandler)
		kthread_stop(shost->ehandler);
	if (shost->work_q)
		destroy_workqueue(shost->work_q);

	if (shost->shost_state == SHOST_CREATED) {
		/*
		 * Free the shost_dev device name here if scsi_host_alloc()
		 * and scsi_host_put() have been called but neither
		 * scsi_host_add() nor scsi_host_remove() has been called.
		 * This avoids that the memory allocated for the shost_dev
		 * name is leaked.
		 */
		kfree(dev_name(&shost->shost_dev));
	}

	if (shost->tag_set.tags)
		scsi_mq_destroy_tags(shost);

	kfree(shost->shost_data);

	ida_simple_remove(&host_index_ida, shost->host_no);

	if (shost->shost_state != SHOST_CREATED)
		put_device(parent);
	kfree(shost);
}

static struct device_type scsi_host_type = {
	.name =		"scsi_host",
	.release =	scsi_host_dev_release,
};

/**
 * scsi_host_alloc - register a scsi host adapter instance.
 * @sht:	pointer to scsi host template
 * @privsize:	extra bytes to allocate for driver
 *
 * Note:
 * 	Allocate a new Scsi_Host and perform basic initialization.
 * 	The host is not published to the scsi midlayer until scsi_add_host
 * 	is called.
 *
 * Return value:
 * 	Pointer to a new Scsi_Host
 **/
struct Scsi_Host *scsi_host_alloc(struct scsi_host_template *sht, int privsize)
{
	struct Scsi_Host *shost;
	int index;

	shost = kzalloc(sizeof(struct Scsi_Host) + privsize, GFP_KERNEL);
	if (!shost)
		return NULL;

	shost->host_lock = &shost->default_lock;
	spin_lock_init(shost->host_lock);
	shost->shost_state = SHOST_CREATED;
	INIT_LIST_HEAD(&shost->__devices);
	INIT_LIST_HEAD(&shost->__targets);
	INIT_LIST_HEAD(&shost->eh_cmd_q);
	INIT_LIST_HEAD(&shost->starved_list);
	init_waitqueue_head(&shost->host_wait);
	mutex_init(&shost->scan_mutex);

	index = ida_simple_get(&host_index_ida, 0, 0, GFP_KERNEL);
	if (index < 0) {
		kfree(shost);
		return NULL;
	}
	shost->host_no = index;

	shost->dma_channel = 0xff;

	/* These three are default values which can be overridden */
	shost->max_channel = 0;
	shost->max_id = 8;
	shost->max_lun = 8;

	/* Give each shost a default transportt */
	shost->transportt = &blank_transport_template;

	/*
	 * All drivers right now should be able to handle 12 byte
	 * commands.  Every so often there are requests for 16 byte
	 * commands, but individual low-level drivers need to certify that
	 * they actually do something sensible with such commands.
	 */
	shost->max_cmd_len = 12;
	shost->hostt = sht;
	shost->this_id = sht->this_id;
	shost->can_queue = sht->can_queue;
	shost->sg_tablesize = sht->sg_tablesize;
	shost->sg_prot_tablesize = sht->sg_prot_tablesize;
	shost->cmd_per_lun = sht->cmd_per_lun;
	shost->no_write_same = sht->no_write_same;
	shost->host_tagset = sht->host_tagset;

	if (shost_eh_deadline == -1 || !sht->eh_host_reset_handler)
		shost->eh_deadline = -1;
	else if ((ulong) shost_eh_deadline * HZ > INT_MAX) {
		shost_printk(KERN_WARNING, shost,
			     "eh_deadline %u too large, setting to %u\n",
			     shost_eh_deadline, INT_MAX / HZ);
		shost->eh_deadline = INT_MAX;
	} else
		shost->eh_deadline = shost_eh_deadline * HZ;

	if (sht->supported_mode == MODE_UNKNOWN)
		/* means we didn't set it ... default to INITIATOR */
		shost->active_mode = MODE_INITIATOR;
	else
		shost->active_mode = sht->supported_mode;

	if (sht->max_host_blocked)
		shost->max_host_blocked = sht->max_host_blocked;
	else
		shost->max_host_blocked = SCSI_DEFAULT_HOST_BLOCKED;

	/*
	 * If the driver imposes no hard sector transfer limit, start at
	 * machine infinity initially.
	 */
	if (sht->max_sectors)
		shost->max_sectors = sht->max_sectors;
	else
		shost->max_sectors = SCSI_DEFAULT_MAX_SECTORS;

	if (sht->max_segment_size)
		shost->max_segment_size = sht->max_segment_size;
	else
		shost->max_segment_size = BLK_MAX_SEGMENT_SIZE;

	/*
	 * assume a 4GB boundary, if not set
	 */
	if (sht->dma_boundary)
		shost->dma_boundary = sht->dma_boundary;
	else
		shost->dma_boundary = 0xffffffff;

	if (sht->virt_boundary_mask)
		shost->virt_boundary_mask = sht->virt_boundary_mask;

	device_initialize(&shost->shost_gendev);
	dev_set_name(&shost->shost_gendev, "host%d", shost->host_no);
	shost->shost_gendev.bus = &scsi_bus_type;
	shost->shost_gendev.type = &scsi_host_type;

	device_initialize(&shost->shost_dev);
	shost->shost_dev.parent = &shost->shost_gendev;
	shost->shost_dev.class = &shost_class;
	dev_set_name(&shost->shost_dev, "host%d", shost->host_no);
	shost->shost_dev.groups = scsi_sysfs_shost_attr_groups;

	shost->ehandler = kthread_run(scsi_error_handler, shost,
			"scsi_eh_%d", shost->host_no);
	if (IS_ERR(shost->ehandler)) {
		shost_printk(KERN_WARNING, shost,
			"error handler thread failed to spawn, error = %ld\n",
			PTR_ERR(shost->ehandler));
		shost->ehandler = NULL;
		goto fail;
	}

	shost->tmf_work_q = alloc_workqueue("scsi_tmf_%d",
					WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_SYSFS,
					   1, shost->host_no);
	if (!shost->tmf_work_q) {
		shost_printk(KERN_WARNING, shost,
			     "failed to create tmf workq\n");
		goto fail;
	}
	scsi_proc_hostdir_add(shost->hostt);
	return shost;
 fail:
	/*
	 * Host state is still SHOST_CREATED and that is enough to release
	 * ->shost_gendev. scsi_host_dev_release() will free
	 * dev_name(&shost->shost_dev).
	 */
	put_device(&shost->shost_gendev);

	return NULL;
}
EXPORT_SYMBOL(scsi_host_alloc);

static int __scsi_host_match(struct device *dev, const void *data)
{
	struct Scsi_Host *p;
	const unsigned short *hostnum = data;

	p = class_to_shost(dev);
	return p->host_no == *hostnum;
}

/**
 * scsi_host_lookup - get a reference to a Scsi_Host by host no
 * @hostnum:	host number to locate
 *
 * Return value:
 *	A pointer to located Scsi_Host or NULL.
 *
 *	The caller must do a scsi_host_put() to drop the reference
 *	that scsi_host_get() took. The put_device() below dropped
 *	the reference from class_find_device().
 **/
struct Scsi_Host *scsi_host_lookup(unsigned short hostnum)
{
	struct device *cdev;
	struct Scsi_Host *shost = NULL;

	cdev = class_find_device(&shost_class, NULL, &hostnum,
				 __scsi_host_match);
	if (cdev) {
		shost = scsi_host_get(class_to_shost(cdev));
		put_device(cdev);
	}
	return shost;
}
EXPORT_SYMBOL(scsi_host_lookup);

/**
 * scsi_host_get - inc a Scsi_Host ref count
 * @shost:	Pointer to Scsi_Host to inc.
 **/
struct Scsi_Host *scsi_host_get(struct Scsi_Host *shost)
{
	if ((shost->shost_state == SHOST_DEL) ||
		!get_device(&shost->shost_gendev))
		return NULL;
	return shost;
}
EXPORT_SYMBOL(scsi_host_get);

static bool scsi_host_check_in_flight(struct request *rq, void *data,
				      bool reserved)
{
	int *count = data;
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);

	if (test_bit(SCMD_STATE_INFLIGHT, &cmd->state))
		(*count)++;

	return true;
}

/**
 * scsi_host_busy - Return the host busy counter
 * @shost:	Pointer to Scsi_Host to inc.
 **/
int scsi_host_busy(struct Scsi_Host *shost)
{
	int cnt = 0;

	blk_mq_tagset_busy_iter(&shost->tag_set,
				scsi_host_check_in_flight, &cnt);
	return cnt;
}
EXPORT_SYMBOL(scsi_host_busy);

/**
 * scsi_host_put - dec a Scsi_Host ref count
 * @shost:	Pointer to Scsi_Host to dec.
 **/
void scsi_host_put(struct Scsi_Host *shost)
{
	put_device(&shost->shost_gendev);
}
EXPORT_SYMBOL(scsi_host_put);

int scsi_init_hosts(void)
{
	return class_register(&shost_class);
}

void scsi_exit_hosts(void)
{
	class_unregister(&shost_class);
	ida_destroy(&host_index_ida);
}

int scsi_is_host_device(const struct device *dev)
{
	return dev->type == &scsi_host_type;
}
EXPORT_SYMBOL(scsi_is_host_device);

/**
 * scsi_queue_work - Queue work to the Scsi_Host workqueue.
 * @shost:	Pointer to Scsi_Host.
 * @work:	Work to queue for execution.
 *
 * Return value:
 * 	1 - work queued for execution
 *	0 - work is already queued
 *	-EINVAL - work queue doesn't exist
 **/
int scsi_queue_work(struct Scsi_Host *shost, struct work_struct *work)
{
	if (unlikely(!shost->work_q)) {
		shost_printk(KERN_ERR, shost,
			"ERROR: Scsi host '%s' attempted to queue scsi-work, "
			"when no workqueue created.\n", shost->hostt->name);
		dump_stack();

		return -EINVAL;
	}

	return queue_work(shost->work_q, work);
}
EXPORT_SYMBOL_GPL(scsi_queue_work);

/**
 * scsi_flush_work - Flush a Scsi_Host's workqueue.
 * @shost:	Pointer to Scsi_Host.
 **/
void scsi_flush_work(struct Scsi_Host *shost)
{
	if (!shost->work_q) {
		shost_printk(KERN_ERR, shost,
			"ERROR: Scsi host '%s' attempted to flush scsi-work, "
			"when no workqueue created.\n", shost->hostt->name);
		dump_stack();
		return;
	}

	flush_workqueue(shost->work_q);
}
EXPORT_SYMBOL_GPL(scsi_flush_work);

static bool complete_all_cmds_iter(struct request *rq, void *data, bool rsvd)
{
	struct scsi_cmnd *scmd = blk_mq_rq_to_pdu(rq);
	enum scsi_host_status status = *(enum scsi_host_status *)data;

	scsi_dma_unmap(scmd);
	scmd->result = 0;
	set_host_byte(scmd, status);
	scmd->scsi_done(scmd);
	return true;
}

/**
 * scsi_host_complete_all_commands - Terminate all running commands
 * @shost:	Scsi Host on which commands should be terminated
 * @status:	Status to be set for the terminated commands
 *
 * There is no protection against modification of the number
 * of outstanding commands. It is the responsibility of the
 * caller to ensure that concurrent I/O submission and/or
 * completion is stopped when calling this function.
 */
void scsi_host_complete_all_commands(struct Scsi_Host *shost,
				     enum scsi_host_status status)
{
	blk_mq_tagset_busy_iter(&shost->tag_set, complete_all_cmds_iter,
				&status);
}
EXPORT_SYMBOL_GPL(scsi_host_complete_all_commands);

struct scsi_host_busy_iter_data {
	bool (*fn)(struct scsi_cmnd *, void *, bool);
	void *priv;
};

static bool __scsi_host_busy_iter_fn(struct request *req, void *priv,
				   bool reserved)
{
	struct scsi_host_busy_iter_data *iter_data = priv;
	struct scsi_cmnd *sc = blk_mq_rq_to_pdu(req);

	return iter_data->fn(sc, iter_data->priv, reserved);
}

/**
 * scsi_host_busy_iter - Iterate over all busy commands
 * @shost:	Pointer to Scsi_Host.
 * @fn:		Function to call on each busy command
 * @priv:	Data pointer passed to @fn
 *
 * If locking against concurrent command completions is required
 * ithas to be provided by the caller
 **/
void scsi_host_busy_iter(struct Scsi_Host *shost,
			 bool (*fn)(struct scsi_cmnd *, void *, bool),
			 void *priv)
{
	struct scsi_host_busy_iter_data iter_data = {
		.fn = fn,
		.priv = priv,
	};

	blk_mq_tagset_busy_iter(&shost->tag_set, __scsi_host_busy_iter_fn,
				&iter_data);
}
EXPORT_SYMBOL_GPL(scsi_host_busy_iter);
