// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 1999 Eric Youngdale
 * Copyright (C) 2014 Christoph Hellwig
 *
 *  SCSI queueing library.
 *      Initial versions: Eric Youngdale (eric@andante.org).
 *                        Based upon conversations with large numbers
 *                        of people at Linux Expo.
 */

#include <linux/bio.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/scatterlist.h>
#include <linux/blk-mq.h>
#include <linux/blk-integrity.h>
#include <linux/ratelimit.h>
#include <asm/unaligned.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h> /* __scsi_init_queue() */
#include <scsi/scsi_dh.h>

#include <trace/events/scsi.h>

#include "scsi_debugfs.h"
#include "scsi_priv.h"
#include "scsi_logging.h"

/*
 * Size of integrity metadata is usually small, 1 inline sg should
 * cover normal cases.
 */
#ifdef CONFIG_ARCH_NO_SG_CHAIN
#define  SCSI_INLINE_PROT_SG_CNT  0
#define  SCSI_INLINE_SG_CNT  0
#else
#define  SCSI_INLINE_PROT_SG_CNT  1
#define  SCSI_INLINE_SG_CNT  2
#endif

static struct kmem_cache *scsi_sense_cache;
static DEFINE_MUTEX(scsi_sense_cache_mutex);

static void scsi_mq_uninit_cmd(struct scsi_cmnd *cmd);

int scsi_init_sense_cache(struct Scsi_Host *shost)
{
	int ret = 0;

	mutex_lock(&scsi_sense_cache_mutex);
	if (!scsi_sense_cache) {
		scsi_sense_cache =
			kmem_cache_create_usercopy("scsi_sense_cache",
				SCSI_SENSE_BUFFERSIZE, 0, SLAB_HWCACHE_ALIGN,
				0, SCSI_SENSE_BUFFERSIZE, NULL);
		if (!scsi_sense_cache)
			ret = -ENOMEM;
	}
	mutex_unlock(&scsi_sense_cache_mutex);
	return ret;
}

static void
scsi_set_blocked(struct scsi_cmnd *cmd, int reason)
{
	struct Scsi_Host *host = cmd->device->host;
	struct scsi_device *device = cmd->device;
	struct scsi_target *starget = scsi_target(device);

	/*
	 * Set the appropriate busy bit for the device/host.
	 *
	 * If the host/device isn't busy, assume that something actually
	 * completed, and that we should be able to queue a command now.
	 *
	 * Note that the prior mid-layer assumption that any host could
	 * always queue at least one command is now broken.  The mid-layer
	 * will implement a user specifiable stall (see
	 * scsi_host.max_host_blocked and scsi_device.max_device_blocked)
	 * if a command is requeued with no other commands outstanding
	 * either for the device or for the host.
	 */
	switch (reason) {
	case SCSI_MLQUEUE_HOST_BUSY:
		atomic_set(&host->host_blocked, host->max_host_blocked);
		break;
	case SCSI_MLQUEUE_DEVICE_BUSY:
	case SCSI_MLQUEUE_EH_RETRY:
		atomic_set(&device->device_blocked,
			   device->max_device_blocked);
		break;
	case SCSI_MLQUEUE_TARGET_BUSY:
		atomic_set(&starget->target_blocked,
			   starget->max_target_blocked);
		break;
	}
}

static void scsi_mq_requeue_cmd(struct scsi_cmnd *cmd, unsigned long msecs)
{
	struct request *rq = scsi_cmd_to_rq(cmd);

	if (rq->rq_flags & RQF_DONTPREP) {
		rq->rq_flags &= ~RQF_DONTPREP;
		scsi_mq_uninit_cmd(cmd);
	} else {
		WARN_ON_ONCE(true);
	}

	if (msecs) {
		blk_mq_requeue_request(rq, false);
		blk_mq_delay_kick_requeue_list(rq->q, msecs);
	} else
		blk_mq_requeue_request(rq, true);
}

/**
 * __scsi_queue_insert - private queue insertion
 * @cmd: The SCSI command being requeued
 * @reason:  The reason for the requeue
 * @unbusy: Whether the queue should be unbusied
 *
 * This is a private queue insertion.  The public interface
 * scsi_queue_insert() always assumes the queue should be unbusied
 * because it's always called before the completion.  This function is
 * for a requeue after completion, which should only occur in this
 * file.
 */
static void __scsi_queue_insert(struct scsi_cmnd *cmd, int reason, bool unbusy)
{
	struct scsi_device *device = cmd->device;

	SCSI_LOG_MLQUEUE(1, scmd_printk(KERN_INFO, cmd,
		"Inserting command %p into mlqueue\n", cmd));

	scsi_set_blocked(cmd, reason);

	/*
	 * Decrement the counters, since these commands are no longer
	 * active on the host/device.
	 */
	if (unbusy)
		scsi_device_unbusy(device, cmd);

	/*
	 * Requeue this command.  It will go before all other commands
	 * that are already in the queue. Schedule requeue work under
	 * lock such that the kblockd_schedule_work() call happens
	 * before blk_mq_destroy_queue() finishes.
	 */
	cmd->result = 0;

	blk_mq_requeue_request(scsi_cmd_to_rq(cmd), true);
}

/**
 * scsi_queue_insert - Reinsert a command in the queue.
 * @cmd:    command that we are adding to queue.
 * @reason: why we are inserting command to queue.
 *
 * We do this for one of two cases. Either the host is busy and it cannot accept
 * any more commands for the time being, or the device returned QUEUE_FULL and
 * can accept no more commands.
 *
 * Context: This could be called either from an interrupt context or a normal
 * process context.
 */
void scsi_queue_insert(struct scsi_cmnd *cmd, int reason)
{
	__scsi_queue_insert(cmd, reason, true);
}


/**
 * __scsi_execute - insert request and wait for the result
 * @sdev:	scsi device
 * @cmd:	scsi command
 * @data_direction: data direction
 * @buffer:	data buffer
 * @bufflen:	len of buffer
 * @sense:	optional sense buffer
 * @sshdr:	optional decoded sense header
 * @timeout:	request timeout in HZ
 * @retries:	number of times to retry request
 * @flags:	flags for ->cmd_flags
 * @rq_flags:	flags for ->rq_flags
 * @resid:	optional residual length
 *
 * Returns the scsi_cmnd result field if a command was executed, or a negative
 * Linux error code if we didn't get that far.
 */
int __scsi_execute(struct scsi_device *sdev, const unsigned char *cmd,
		 int data_direction, void *buffer, unsigned bufflen,
		 unsigned char *sense, struct scsi_sense_hdr *sshdr,
		 int timeout, int retries, blk_opf_t flags,
		 req_flags_t rq_flags, int *resid)
{
	struct request *req;
	struct scsi_cmnd *scmd;
	int ret;

	req = scsi_alloc_request(sdev->request_queue,
			data_direction == DMA_TO_DEVICE ?
			REQ_OP_DRV_OUT : REQ_OP_DRV_IN,
			rq_flags & RQF_PM ? BLK_MQ_REQ_PM : 0);
	if (IS_ERR(req))
		return PTR_ERR(req);

	if (bufflen) {
		ret = blk_rq_map_kern(sdev->request_queue, req,
				      buffer, bufflen, GFP_NOIO);
		if (ret)
			goto out;
	}
	scmd = blk_mq_rq_to_pdu(req);
	scmd->cmd_len = COMMAND_SIZE(cmd[0]);
	memcpy(scmd->cmnd, cmd, scmd->cmd_len);
	scmd->allowed = retries;
	req->timeout = timeout;
	req->cmd_flags |= flags;
	req->rq_flags |= rq_flags | RQF_QUIET;

	/*
	 * head injection *required* here otherwise quiesce won't work
	 */
	blk_execute_rq(req, true);

	/*
	 * Some devices (USB mass-storage in particular) may transfer
	 * garbage data together with a residue indicating that the data
	 * is invalid.  Prevent the garbage from being misinterpreted
	 * and prevent security leaks by zeroing out the excess data.
	 */
	if (unlikely(scmd->resid_len > 0 && scmd->resid_len <= bufflen))
		memset(buffer + bufflen - scmd->resid_len, 0, scmd->resid_len);

	if (resid)
		*resid = scmd->resid_len;
	if (sense && scmd->sense_len)
		memcpy(sense, scmd->sense_buffer, SCSI_SENSE_BUFFERSIZE);
	if (sshdr)
		scsi_normalize_sense(scmd->sense_buffer, scmd->sense_len,
				     sshdr);
	ret = scmd->result;
 out:
	blk_mq_free_request(req);

	return ret;
}
EXPORT_SYMBOL(__scsi_execute);

/*
 * Wake up the error handler if necessary. Avoid as follows that the error
 * handler is not woken up if host in-flight requests number ==
 * shost->host_failed: use call_rcu() in scsi_eh_scmd_add() in combination
 * with an RCU read lock in this function to ensure that this function in
 * its entirety either finishes before scsi_eh_scmd_add() increases the
 * host_failed counter or that it notices the shost state change made by
 * scsi_eh_scmd_add().
 */
static void scsi_dec_host_busy(struct Scsi_Host *shost, struct scsi_cmnd *cmd)
{
	unsigned long flags;

	rcu_read_lock();
	__clear_bit(SCMD_STATE_INFLIGHT, &cmd->state);
	if (unlikely(scsi_host_in_recovery(shost))) {
		unsigned int busy = scsi_host_busy(shost);

		spin_lock_irqsave(shost->host_lock, flags);
		if (shost->host_failed || shost->host_eh_scheduled)
			scsi_eh_wakeup(shost, busy);
		spin_unlock_irqrestore(shost->host_lock, flags);
	}
	rcu_read_unlock();
}

void scsi_device_unbusy(struct scsi_device *sdev, struct scsi_cmnd *cmd)
{
	struct Scsi_Host *shost = sdev->host;
	struct scsi_target *starget = scsi_target(sdev);

	scsi_dec_host_busy(shost, cmd);

	if (starget->can_queue > 0)
		atomic_dec(&starget->target_busy);

	sbitmap_put(&sdev->budget_map, cmd->budget_token);
	cmd->budget_token = -1;
}

static void scsi_kick_queue(struct request_queue *q)
{
	blk_mq_run_hw_queues(q, false);
}

/*
 * Called for single_lun devices on IO completion. Clear starget_sdev_user,
 * and call blk_run_queue for all the scsi_devices on the target -
 * including current_sdev first.
 *
 * Called with *no* scsi locks held.
 */
static void scsi_single_lun_run(struct scsi_device *current_sdev)
{
	struct Scsi_Host *shost = current_sdev->host;
	struct scsi_device *sdev, *tmp;
	struct scsi_target *starget = scsi_target(current_sdev);
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	starget->starget_sdev_user = NULL;
	spin_unlock_irqrestore(shost->host_lock, flags);

	/*
	 * Call blk_run_queue for all LUNs on the target, starting with
	 * current_sdev. We race with others (to set starget_sdev_user),
	 * but in most cases, we will be first. Ideally, each LU on the
	 * target would get some limited time or requests on the target.
	 */
	scsi_kick_queue(current_sdev->request_queue);

	spin_lock_irqsave(shost->host_lock, flags);
	if (starget->starget_sdev_user)
		goto out;
	list_for_each_entry_safe(sdev, tmp, &starget->devices,
			same_target_siblings) {
		if (sdev == current_sdev)
			continue;
		if (scsi_device_get(sdev))
			continue;

		spin_unlock_irqrestore(shost->host_lock, flags);
		scsi_kick_queue(sdev->request_queue);
		spin_lock_irqsave(shost->host_lock, flags);

		scsi_device_put(sdev);
	}
 out:
	spin_unlock_irqrestore(shost->host_lock, flags);
}

static inline bool scsi_device_is_busy(struct scsi_device *sdev)
{
	if (scsi_device_busy(sdev) >= sdev->queue_depth)
		return true;
	if (atomic_read(&sdev->device_blocked) > 0)
		return true;
	return false;
}

static inline bool scsi_target_is_busy(struct scsi_target *starget)
{
	if (starget->can_queue > 0) {
		if (atomic_read(&starget->target_busy) >= starget->can_queue)
			return true;
		if (atomic_read(&starget->target_blocked) > 0)
			return true;
	}
	return false;
}

static inline bool scsi_host_is_busy(struct Scsi_Host *shost)
{
	if (atomic_read(&shost->host_blocked) > 0)
		return true;
	if (shost->host_self_blocked)
		return true;
	return false;
}

static void scsi_starved_list_run(struct Scsi_Host *shost)
{
	LIST_HEAD(starved_list);
	struct scsi_device *sdev;
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	list_splice_init(&shost->starved_list, &starved_list);

	while (!list_empty(&starved_list)) {
		struct request_queue *slq;

		/*
		 * As long as shost is accepting commands and we have
		 * starved queues, call blk_run_queue. scsi_request_fn
		 * drops the queue_lock and can add us back to the
		 * starved_list.
		 *
		 * host_lock protects the starved_list and starved_entry.
		 * scsi_request_fn must get the host_lock before checking
		 * or modifying starved_list or starved_entry.
		 */
		if (scsi_host_is_busy(shost))
			break;

		sdev = list_entry(starved_list.next,
				  struct scsi_device, starved_entry);
		list_del_init(&sdev->starved_entry);
		if (scsi_target_is_busy(scsi_target(sdev))) {
			list_move_tail(&sdev->starved_entry,
				       &shost->starved_list);
			continue;
		}

		/*
		 * Once we drop the host lock, a racing scsi_remove_device()
		 * call may remove the sdev from the starved list and destroy
		 * it and the queue.  Mitigate by taking a reference to the
		 * queue and never touching the sdev again after we drop the
		 * host lock.  Note: if __scsi_remove_device() invokes
		 * blk_mq_destroy_queue() before the queue is run from this
		 * function then blk_run_queue() will return immediately since
		 * blk_mq_destroy_queue() marks the queue with QUEUE_FLAG_DYING.
		 */
		slq = sdev->request_queue;
		if (!blk_get_queue(slq))
			continue;
		spin_unlock_irqrestore(shost->host_lock, flags);

		scsi_kick_queue(slq);
		blk_put_queue(slq);

		spin_lock_irqsave(shost->host_lock, flags);
	}
	/* put any unprocessed entries back */
	list_splice(&starved_list, &shost->starved_list);
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/**
 * scsi_run_queue - Select a proper request queue to serve next.
 * @q:  last request's queue
 *
 * The previous command was completely finished, start a new one if possible.
 */
static void scsi_run_queue(struct request_queue *q)
{
	struct scsi_device *sdev = q->queuedata;

	if (scsi_target(sdev)->single_lun)
		scsi_single_lun_run(sdev);
	if (!list_empty(&sdev->host->starved_list))
		scsi_starved_list_run(sdev->host);

	blk_mq_run_hw_queues(q, false);
}

void scsi_requeue_run_queue(struct work_struct *work)
{
	struct scsi_device *sdev;
	struct request_queue *q;

	sdev = container_of(work, struct scsi_device, requeue_work);
	q = sdev->request_queue;
	scsi_run_queue(q);
}

void scsi_run_host_queues(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;

	shost_for_each_device(sdev, shost)
		scsi_run_queue(sdev->request_queue);
}

static void scsi_uninit_cmd(struct scsi_cmnd *cmd)
{
	if (!blk_rq_is_passthrough(scsi_cmd_to_rq(cmd))) {
		struct scsi_driver *drv = scsi_cmd_to_driver(cmd);

		if (drv->uninit_command)
			drv->uninit_command(cmd);
	}
}

void scsi_free_sgtables(struct scsi_cmnd *cmd)
{
	if (cmd->sdb.table.nents)
		sg_free_table_chained(&cmd->sdb.table,
				SCSI_INLINE_SG_CNT);
	if (scsi_prot_sg_count(cmd))
		sg_free_table_chained(&cmd->prot_sdb->table,
				SCSI_INLINE_PROT_SG_CNT);
}
EXPORT_SYMBOL_GPL(scsi_free_sgtables);

static void scsi_mq_uninit_cmd(struct scsi_cmnd *cmd)
{
	scsi_free_sgtables(cmd);
	scsi_uninit_cmd(cmd);
}

static void scsi_run_queue_async(struct scsi_device *sdev)
{
	if (scsi_target(sdev)->single_lun ||
	    !list_empty(&sdev->host->starved_list)) {
		kblockd_schedule_work(&sdev->requeue_work);
	} else {
		/*
		 * smp_mb() present in sbitmap_queue_clear() or implied in
		 * .end_io is for ordering writing .device_busy in
		 * scsi_device_unbusy() and reading sdev->restarts.
		 */
		int old = atomic_read(&sdev->restarts);

		/*
		 * ->restarts has to be kept as non-zero if new budget
		 *  contention occurs.
		 *
		 *  No need to run queue when either another re-run
		 *  queue wins in updating ->restarts or a new budget
		 *  contention occurs.
		 */
		if (old && atomic_cmpxchg(&sdev->restarts, old, 0) == old)
			blk_mq_run_hw_queues(sdev->request_queue, true);
	}
}

/* Returns false when no more bytes to process, true if there are more */
static bool scsi_end_request(struct request *req, blk_status_t error,
		unsigned int bytes)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);
	struct scsi_device *sdev = cmd->device;
	struct request_queue *q = sdev->request_queue;

	if (blk_update_request(req, error, bytes))
		return true;

	// XXX:
	if (blk_queue_add_random(q))
		add_disk_randomness(req->q->disk);

	if (!blk_rq_is_passthrough(req)) {
		WARN_ON_ONCE(!(cmd->flags & SCMD_INITIALIZED));
		cmd->flags &= ~SCMD_INITIALIZED;
	}

	/*
	 * Calling rcu_barrier() is not necessary here because the
	 * SCSI error handler guarantees that the function called by
	 * call_rcu() has been called before scsi_end_request() is
	 * called.
	 */
	destroy_rcu_head(&cmd->rcu);

	/*
	 * In the MQ case the command gets freed by __blk_mq_end_request,
	 * so we have to do all cleanup that depends on it earlier.
	 *
	 * We also can't kick the queues from irq context, so we
	 * will have to defer it to a workqueue.
	 */
	scsi_mq_uninit_cmd(cmd);

	/*
	 * queue is still alive, so grab the ref for preventing it
	 * from being cleaned up during running queue.
	 */
	percpu_ref_get(&q->q_usage_counter);

	__blk_mq_end_request(req, error);

	scsi_run_queue_async(sdev);

	percpu_ref_put(&q->q_usage_counter);
	return false;
}

static inline u8 get_scsi_ml_byte(int result)
{
	return (result >> 8) & 0xff;
}

/**
 * scsi_result_to_blk_status - translate a SCSI result code into blk_status_t
 * @result:	scsi error code
 *
 * Translate a SCSI result code into a blk_status_t value.
 */
static blk_status_t scsi_result_to_blk_status(int result)
{
	/*
	 * Check the scsi-ml byte first in case we converted a host or status
	 * byte.
	 */
	switch (get_scsi_ml_byte(result)) {
	case SCSIML_STAT_OK:
		break;
	case SCSIML_STAT_RESV_CONFLICT:
		return BLK_STS_NEXUS;
	case SCSIML_STAT_NOSPC:
		return BLK_STS_NOSPC;
	case SCSIML_STAT_MED_ERROR:
		return BLK_STS_MEDIUM;
	case SCSIML_STAT_TGT_FAILURE:
		return BLK_STS_TARGET;
	}

	switch (host_byte(result)) {
	case DID_OK:
		if (scsi_status_is_good(result))
			return BLK_STS_OK;
		return BLK_STS_IOERR;
	case DID_TRANSPORT_FAILFAST:
	case DID_TRANSPORT_MARGINAL:
		return BLK_STS_TRANSPORT;
	default:
		return BLK_STS_IOERR;
	}
}

/**
 * scsi_rq_err_bytes - determine number of bytes till the next failure boundary
 * @rq: request to examine
 *
 * Description:
 *     A request could be merge of IOs which require different failure
 *     handling.  This function determines the number of bytes which
 *     can be failed from the beginning of the request without
 *     crossing into area which need to be retried further.
 *
 * Return:
 *     The number of bytes to fail.
 */
static unsigned int scsi_rq_err_bytes(const struct request *rq)
{
	blk_opf_t ff = rq->cmd_flags & REQ_FAILFAST_MASK;
	unsigned int bytes = 0;
	struct bio *bio;

	if (!(rq->rq_flags & RQF_MIXED_MERGE))
		return blk_rq_bytes(rq);

	/*
	 * Currently the only 'mixing' which can happen is between
	 * different fastfail types.  We can safely fail portions
	 * which have all the failfast bits that the first one has -
	 * the ones which are at least as eager to fail as the first
	 * one.
	 */
	for (bio = rq->bio; bio; bio = bio->bi_next) {
		if ((bio->bi_opf & ff) != ff)
			break;
		bytes += bio->bi_iter.bi_size;
	}

	/* this could lead to infinite loop */
	BUG_ON(blk_rq_bytes(rq) && !bytes);
	return bytes;
}

static bool scsi_cmd_runtime_exceeced(struct scsi_cmnd *cmd)
{
	struct request *req = scsi_cmd_to_rq(cmd);
	unsigned long wait_for;

	if (cmd->allowed == SCSI_CMD_RETRIES_NO_LIMIT)
		return false;

	wait_for = (cmd->allowed + 1) * req->timeout;
	if (time_before(cmd->jiffies_at_alloc + wait_for, jiffies)) {
		scmd_printk(KERN_ERR, cmd, "timing out command, waited %lus\n",
			    wait_for/HZ);
		return true;
	}
	return false;
}

/*
 * When ALUA transition state is returned, reprep the cmd to
 * use the ALUA handler's transition timeout. Delay the reprep
 * 1 sec to avoid aggressive retries of the target in that
 * state.
 */
#define ALUA_TRANSITION_REPREP_DELAY	1000

/* Helper for scsi_io_completion() when special action required. */
static void scsi_io_completion_action(struct scsi_cmnd *cmd, int result)
{
	struct request *req = scsi_cmd_to_rq(cmd);
	int level = 0;
	enum {ACTION_FAIL, ACTION_REPREP, ACTION_DELAYED_REPREP,
	      ACTION_RETRY, ACTION_DELAYED_RETRY} action;
	struct scsi_sense_hdr sshdr;
	bool sense_valid;
	bool sense_current = true;      /* false implies "deferred sense" */
	blk_status_t blk_stat;

	sense_valid = scsi_command_normalize_sense(cmd, &sshdr);
	if (sense_valid)
		sense_current = !scsi_sense_is_deferred(&sshdr);

	blk_stat = scsi_result_to_blk_status(result);

	if (host_byte(result) == DID_RESET) {
		/* Third party bus reset or reset for error recovery
		 * reasons.  Just retry the command and see what
		 * happens.
		 */
		action = ACTION_RETRY;
	} else if (sense_valid && sense_current) {
		switch (sshdr.sense_key) {
		case UNIT_ATTENTION:
			if (cmd->device->removable) {
				/* Detected disc change.  Set a bit
				 * and quietly refuse further access.
				 */
				cmd->device->changed = 1;
				action = ACTION_FAIL;
			} else {
				/* Must have been a power glitch, or a
				 * bus reset.  Could not have been a
				 * media change, so we just retry the
				 * command and see what happens.
				 */
				action = ACTION_RETRY;
			}
			break;
		case ILLEGAL_REQUEST:
			/* If we had an ILLEGAL REQUEST returned, then
			 * we may have performed an unsupported
			 * command.  The only thing this should be
			 * would be a ten byte read where only a six
			 * byte read was supported.  Also, on a system
			 * where READ CAPACITY failed, we may have
			 * read past the end of the disk.
			 */
			if ((cmd->device->use_10_for_rw &&
			    sshdr.asc == 0x20 && sshdr.ascq == 0x00) &&
			    (cmd->cmnd[0] == READ_10 ||
			     cmd->cmnd[0] == WRITE_10)) {
				/* This will issue a new 6-byte command. */
				cmd->device->use_10_for_rw = 0;
				action = ACTION_REPREP;
			} else if (sshdr.asc == 0x10) /* DIX */ {
				action = ACTION_FAIL;
				blk_stat = BLK_STS_PROTECTION;
			/* INVALID COMMAND OPCODE or INVALID FIELD IN CDB */
			} else if (sshdr.asc == 0x20 || sshdr.asc == 0x24) {
				action = ACTION_FAIL;
				blk_stat = BLK_STS_TARGET;
			} else
				action = ACTION_FAIL;
			break;
		case ABORTED_COMMAND:
			action = ACTION_FAIL;
			if (sshdr.asc == 0x10) /* DIF */
				blk_stat = BLK_STS_PROTECTION;
			break;
		case NOT_READY:
			/* If the device is in the process of becoming
			 * ready, or has a temporary blockage, retry.
			 */
			if (sshdr.asc == 0x04) {
				switch (sshdr.ascq) {
				case 0x01: /* becoming ready */
				case 0x04: /* format in progress */
				case 0x05: /* rebuild in progress */
				case 0x06: /* recalculation in progress */
				case 0x07: /* operation in progress */
				case 0x08: /* Long write in progress */
				case 0x09: /* self test in progress */
				case 0x11: /* notify (enable spinup) required */
				case 0x14: /* space allocation in progress */
				case 0x1a: /* start stop unit in progress */
				case 0x1b: /* sanitize in progress */
				case 0x1d: /* configuration in progress */
				case 0x24: /* depopulation in progress */
					action = ACTION_DELAYED_RETRY;
					break;
				case 0x0a: /* ALUA state transition */
					action = ACTION_DELAYED_REPREP;
					break;
				default:
					action = ACTION_FAIL;
					break;
				}
			} else
				action = ACTION_FAIL;
			break;
		case VOLUME_OVERFLOW:
			/* See SSC3rXX or current. */
			action = ACTION_FAIL;
			break;
		case DATA_PROTECT:
			action = ACTION_FAIL;
			if ((sshdr.asc == 0x0C && sshdr.ascq == 0x12) ||
			    (sshdr.asc == 0x55 &&
			     (sshdr.ascq == 0x0E || sshdr.ascq == 0x0F))) {
				/* Insufficient zone resources */
				blk_stat = BLK_STS_ZONE_OPEN_RESOURCE;
			}
			break;
		default:
			action = ACTION_FAIL;
			break;
		}
	} else
		action = ACTION_FAIL;

	if (action != ACTION_FAIL && scsi_cmd_runtime_exceeced(cmd))
		action = ACTION_FAIL;

	switch (action) {
	case ACTION_FAIL:
		/* Give up and fail the remainder of the request */
		if (!(req->rq_flags & RQF_QUIET)) {
			static DEFINE_RATELIMIT_STATE(_rs,
					DEFAULT_RATELIMIT_INTERVAL,
					DEFAULT_RATELIMIT_BURST);

			if (unlikely(scsi_logging_level))
				level =
				     SCSI_LOG_LEVEL(SCSI_LOG_MLCOMPLETE_SHIFT,
						    SCSI_LOG_MLCOMPLETE_BITS);

			/*
			 * if logging is enabled the failure will be printed
			 * in scsi_log_completion(), so avoid duplicate messages
			 */
			if (!level && __ratelimit(&_rs)) {
				scsi_print_result(cmd, NULL, FAILED);
				if (sense_valid)
					scsi_print_sense(cmd);
				scsi_print_command(cmd);
			}
		}
		if (!scsi_end_request(req, blk_stat, scsi_rq_err_bytes(req)))
			return;
		fallthrough;
	case ACTION_REPREP:
		scsi_mq_requeue_cmd(cmd, 0);
		break;
	case ACTION_DELAYED_REPREP:
		scsi_mq_requeue_cmd(cmd, ALUA_TRANSITION_REPREP_DELAY);
		break;
	case ACTION_RETRY:
		/* Retry the same command immediately */
		__scsi_queue_insert(cmd, SCSI_MLQUEUE_EH_RETRY, false);
		break;
	case ACTION_DELAYED_RETRY:
		/* Retry the same command after a delay */
		__scsi_queue_insert(cmd, SCSI_MLQUEUE_DEVICE_BUSY, false);
		break;
	}
}

/*
 * Helper for scsi_io_completion() when cmd->result is non-zero. Returns a
 * new result that may suppress further error checking. Also modifies
 * *blk_statp in some cases.
 */
static int scsi_io_completion_nz_result(struct scsi_cmnd *cmd, int result,
					blk_status_t *blk_statp)
{
	bool sense_valid;
	bool sense_current = true;	/* false implies "deferred sense" */
	struct request *req = scsi_cmd_to_rq(cmd);
	struct scsi_sense_hdr sshdr;

	sense_valid = scsi_command_normalize_sense(cmd, &sshdr);
	if (sense_valid)
		sense_current = !scsi_sense_is_deferred(&sshdr);

	if (blk_rq_is_passthrough(req)) {
		if (sense_valid) {
			/*
			 * SG_IO wants current and deferred errors
			 */
			cmd->sense_len = min(8 + cmd->sense_buffer[7],
					     SCSI_SENSE_BUFFERSIZE);
		}
		if (sense_current)
			*blk_statp = scsi_result_to_blk_status(result);
	} else if (blk_rq_bytes(req) == 0 && sense_current) {
		/*
		 * Flush commands do not transfers any data, and thus cannot use
		 * good_bytes != blk_rq_bytes(req) as the signal for an error.
		 * This sets *blk_statp explicitly for the problem case.
		 */
		*blk_statp = scsi_result_to_blk_status(result);
	}
	/*
	 * Recovered errors need reporting, but they're always treated as
	 * success, so fiddle the result code here.  For passthrough requests
	 * we already took a copy of the original into sreq->result which
	 * is what gets returned to the user
	 */
	if (sense_valid && (sshdr.sense_key == RECOVERED_ERROR)) {
		bool do_print = true;
		/*
		 * if ATA PASS-THROUGH INFORMATION AVAILABLE [0x0, 0x1d]
		 * skip print since caller wants ATA registers. Only occurs
		 * on SCSI ATA PASS_THROUGH commands when CK_COND=1
		 */
		if ((sshdr.asc == 0x0) && (sshdr.ascq == 0x1d))
			do_print = false;
		else if (req->rq_flags & RQF_QUIET)
			do_print = false;
		if (do_print)
			scsi_print_sense(cmd);
		result = 0;
		/* for passthrough, *blk_statp may be set */
		*blk_statp = BLK_STS_OK;
	}
	/*
	 * Another corner case: the SCSI status byte is non-zero but 'good'.
	 * Example: PRE-FETCH command returns SAM_STAT_CONDITION_MET when
	 * it is able to fit nominated LBs in its cache (and SAM_STAT_GOOD
	 * if it can't fit). Treat SAM_STAT_CONDITION_MET and the related
	 * intermediate statuses (both obsolete in SAM-4) as good.
	 */
	if ((result & 0xff) && scsi_status_is_good(result)) {
		result = 0;
		*blk_statp = BLK_STS_OK;
	}
	return result;
}

/**
 * scsi_io_completion - Completion processing for SCSI commands.
 * @cmd:	command that is finished.
 * @good_bytes:	number of processed bytes.
 *
 * We will finish off the specified number of sectors. If we are done, the
 * command block will be released and the queue function will be goosed. If we
 * are not done then we have to figure out what to do next:
 *
 *   a) We can call scsi_mq_requeue_cmd().  The request will be
 *	unprepared and put back on the queue.  Then a new command will
 *	be created for it.  This should be used if we made forward
 *	progress, or if we want to switch from READ(10) to READ(6) for
 *	example.
 *
 *   b) We can call scsi_io_completion_action().  The request will be
 *	put back on the queue and retried using the same command as
 *	before, possibly after a delay.
 *
 *   c) We can call scsi_end_request() with blk_stat other than
 *	BLK_STS_OK, to fail the remainder of the request.
 */
void scsi_io_completion(struct scsi_cmnd *cmd, unsigned int good_bytes)
{
	int result = cmd->result;
	struct request *req = scsi_cmd_to_rq(cmd);
	blk_status_t blk_stat = BLK_STS_OK;

	if (unlikely(result))	/* a nz result may or may not be an error */
		result = scsi_io_completion_nz_result(cmd, result, &blk_stat);

	/*
	 * Next deal with any sectors which we were able to correctly
	 * handle.
	 */
	SCSI_LOG_HLCOMPLETE(1, scmd_printk(KERN_INFO, cmd,
		"%u sectors total, %d bytes done.\n",
		blk_rq_sectors(req), good_bytes));

	/*
	 * Failed, zero length commands always need to drop down
	 * to retry code. Fast path should return in this block.
	 */
	if (likely(blk_rq_bytes(req) > 0 || blk_stat == BLK_STS_OK)) {
		if (likely(!scsi_end_request(req, blk_stat, good_bytes)))
			return; /* no bytes remaining */
	}

	/* Kill remainder if no retries. */
	if (unlikely(blk_stat && scsi_noretry_cmd(cmd))) {
		if (scsi_end_request(req, blk_stat, blk_rq_bytes(req)))
			WARN_ONCE(true,
			    "Bytes remaining after failed, no-retry command");
		return;
	}

	/*
	 * If there had been no error, but we have leftover bytes in the
	 * request just queue the command up again.
	 */
	if (likely(result == 0))
		scsi_mq_requeue_cmd(cmd, 0);
	else
		scsi_io_completion_action(cmd, result);
}

static inline bool scsi_cmd_needs_dma_drain(struct scsi_device *sdev,
		struct request *rq)
{
	return sdev->dma_drain_len && blk_rq_is_passthrough(rq) &&
	       !op_is_write(req_op(rq)) &&
	       sdev->host->hostt->dma_need_drain(rq);
}

/**
 * scsi_alloc_sgtables - Allocate and initialize data and integrity scatterlists
 * @cmd: SCSI command data structure to initialize.
 *
 * Initializes @cmd->sdb and also @cmd->prot_sdb if data integrity is enabled
 * for @cmd.
 *
 * Returns:
 * * BLK_STS_OK       - on success
 * * BLK_STS_RESOURCE - if the failure is retryable
 * * BLK_STS_IOERR    - if the failure is fatal
 */
blk_status_t scsi_alloc_sgtables(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct request *rq = scsi_cmd_to_rq(cmd);
	unsigned short nr_segs = blk_rq_nr_phys_segments(rq);
	struct scatterlist *last_sg = NULL;
	blk_status_t ret;
	bool need_drain = scsi_cmd_needs_dma_drain(sdev, rq);
	int count;

	if (WARN_ON_ONCE(!nr_segs))
		return BLK_STS_IOERR;

	/*
	 * Make sure there is space for the drain.  The driver must adjust
	 * max_hw_segments to be prepared for this.
	 */
	if (need_drain)
		nr_segs++;

	/*
	 * If sg table allocation fails, requeue request later.
	 */
	if (unlikely(sg_alloc_table_chained(&cmd->sdb.table, nr_segs,
			cmd->sdb.table.sgl, SCSI_INLINE_SG_CNT)))
		return BLK_STS_RESOURCE;

	/*
	 * Next, walk the list, and fill in the addresses and sizes of
	 * each segment.
	 */
	count = __blk_rq_map_sg(rq->q, rq, cmd->sdb.table.sgl, &last_sg);

	if (blk_rq_bytes(rq) & rq->q->dma_pad_mask) {
		unsigned int pad_len =
			(rq->q->dma_pad_mask & ~blk_rq_bytes(rq)) + 1;

		last_sg->length += pad_len;
		cmd->extra_len += pad_len;
	}

	if (need_drain) {
		sg_unmark_end(last_sg);
		last_sg = sg_next(last_sg);
		sg_set_buf(last_sg, sdev->dma_drain_buf, sdev->dma_drain_len);
		sg_mark_end(last_sg);

		cmd->extra_len += sdev->dma_drain_len;
		count++;
	}

	BUG_ON(count > cmd->sdb.table.nents);
	cmd->sdb.table.nents = count;
	cmd->sdb.length = blk_rq_payload_bytes(rq);

	if (blk_integrity_rq(rq)) {
		struct scsi_data_buffer *prot_sdb = cmd->prot_sdb;
		int ivecs;

		if (WARN_ON_ONCE(!prot_sdb)) {
			/*
			 * This can happen if someone (e.g. multipath)
			 * queues a command to a device on an adapter
			 * that does not support DIX.
			 */
			ret = BLK_STS_IOERR;
			goto out_free_sgtables;
		}

		ivecs = blk_rq_count_integrity_sg(rq->q, rq->bio);

		if (sg_alloc_table_chained(&prot_sdb->table, ivecs,
				prot_sdb->table.sgl,
				SCSI_INLINE_PROT_SG_CNT)) {
			ret = BLK_STS_RESOURCE;
			goto out_free_sgtables;
		}

		count = blk_rq_map_integrity_sg(rq->q, rq->bio,
						prot_sdb->table.sgl);
		BUG_ON(count > ivecs);
		BUG_ON(count > queue_max_integrity_segments(rq->q));

		cmd->prot_sdb = prot_sdb;
		cmd->prot_sdb->table.nents = count;
	}

	return BLK_STS_OK;
out_free_sgtables:
	scsi_free_sgtables(cmd);
	return ret;
}
EXPORT_SYMBOL(scsi_alloc_sgtables);

/**
 * scsi_initialize_rq - initialize struct scsi_cmnd partially
 * @rq: Request associated with the SCSI command to be initialized.
 *
 * This function initializes the members of struct scsi_cmnd that must be
 * initialized before request processing starts and that won't be
 * reinitialized if a SCSI command is requeued.
 */
static void scsi_initialize_rq(struct request *rq)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);

	memset(cmd->cmnd, 0, sizeof(cmd->cmnd));
	cmd->cmd_len = MAX_COMMAND_SIZE;
	cmd->sense_len = 0;
	init_rcu_head(&cmd->rcu);
	cmd->jiffies_at_alloc = jiffies;
	cmd->retries = 0;
}

struct request *scsi_alloc_request(struct request_queue *q, blk_opf_t opf,
				   blk_mq_req_flags_t flags)
{
	struct request *rq;

	rq = blk_mq_alloc_request(q, opf, flags);
	if (!IS_ERR(rq))
		scsi_initialize_rq(rq);
	return rq;
}
EXPORT_SYMBOL_GPL(scsi_alloc_request);

/*
 * Only called when the request isn't completed by SCSI, and not freed by
 * SCSI
 */
static void scsi_cleanup_rq(struct request *rq)
{
	if (rq->rq_flags & RQF_DONTPREP) {
		scsi_mq_uninit_cmd(blk_mq_rq_to_pdu(rq));
		rq->rq_flags &= ~RQF_DONTPREP;
	}
}

/* Called before a request is prepared. See also scsi_mq_prep_fn(). */
void scsi_init_command(struct scsi_device *dev, struct scsi_cmnd *cmd)
{
	struct request *rq = scsi_cmd_to_rq(cmd);

	if (!blk_rq_is_passthrough(rq) && !(cmd->flags & SCMD_INITIALIZED)) {
		cmd->flags |= SCMD_INITIALIZED;
		scsi_initialize_rq(rq);
	}

	cmd->device = dev;
	INIT_LIST_HEAD(&cmd->eh_entry);
	INIT_DELAYED_WORK(&cmd->abort_work, scmd_eh_abort_handler);
}

static blk_status_t scsi_setup_scsi_cmnd(struct scsi_device *sdev,
		struct request *req)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);

	/*
	 * Passthrough requests may transfer data, in which case they must
	 * a bio attached to them.  Or they might contain a SCSI command
	 * that does not transfer data, in which case they may optionally
	 * submit a request without an attached bio.
	 */
	if (req->bio) {
		blk_status_t ret = scsi_alloc_sgtables(cmd);
		if (unlikely(ret != BLK_STS_OK))
			return ret;
	} else {
		BUG_ON(blk_rq_bytes(req));

		memset(&cmd->sdb, 0, sizeof(cmd->sdb));
	}

	cmd->transfersize = blk_rq_bytes(req);
	return BLK_STS_OK;
}

static blk_status_t
scsi_device_state_check(struct scsi_device *sdev, struct request *req)
{
	switch (sdev->sdev_state) {
	case SDEV_CREATED:
		return BLK_STS_OK;
	case SDEV_OFFLINE:
	case SDEV_TRANSPORT_OFFLINE:
		/*
		 * If the device is offline we refuse to process any
		 * commands.  The device must be brought online
		 * before trying any recovery commands.
		 */
		if (!sdev->offline_already) {
			sdev->offline_already = true;
			sdev_printk(KERN_ERR, sdev,
				    "rejecting I/O to offline device\n");
		}
		return BLK_STS_IOERR;
	case SDEV_DEL:
		/*
		 * If the device is fully deleted, we refuse to
		 * process any commands as well.
		 */
		sdev_printk(KERN_ERR, sdev,
			    "rejecting I/O to dead device\n");
		return BLK_STS_IOERR;
	case SDEV_BLOCK:
	case SDEV_CREATED_BLOCK:
		return BLK_STS_RESOURCE;
	case SDEV_QUIESCE:
		/*
		 * If the device is blocked we only accept power management
		 * commands.
		 */
		if (req && WARN_ON_ONCE(!(req->rq_flags & RQF_PM)))
			return BLK_STS_RESOURCE;
		return BLK_STS_OK;
	default:
		/*
		 * For any other not fully online state we only allow
		 * power management commands.
		 */
		if (req && !(req->rq_flags & RQF_PM))
			return BLK_STS_OFFLINE;
		return BLK_STS_OK;
	}
}

/*
 * scsi_dev_queue_ready: if we can send requests to sdev, assign one token
 * and return the token else return -1.
 */
static inline int scsi_dev_queue_ready(struct request_queue *q,
				  struct scsi_device *sdev)
{
	int token;

	token = sbitmap_get(&sdev->budget_map);
	if (atomic_read(&sdev->device_blocked)) {
		if (token < 0)
			goto out;

		if (scsi_device_busy(sdev) > 1)
			goto out_dec;

		/*
		 * unblock after device_blocked iterates to zero
		 */
		if (atomic_dec_return(&sdev->device_blocked) > 0)
			goto out_dec;
		SCSI_LOG_MLQUEUE(3, sdev_printk(KERN_INFO, sdev,
				   "unblocking device at zero depth\n"));
	}

	return token;
out_dec:
	if (token >= 0)
		sbitmap_put(&sdev->budget_map, token);
out:
	return -1;
}

/*
 * scsi_target_queue_ready: checks if there we can send commands to target
 * @sdev: scsi device on starget to check.
 */
static inline int scsi_target_queue_ready(struct Scsi_Host *shost,
					   struct scsi_device *sdev)
{
	struct scsi_target *starget = scsi_target(sdev);
	unsigned int busy;

	if (starget->single_lun) {
		spin_lock_irq(shost->host_lock);
		if (starget->starget_sdev_user &&
		    starget->starget_sdev_user != sdev) {
			spin_unlock_irq(shost->host_lock);
			return 0;
		}
		starget->starget_sdev_user = sdev;
		spin_unlock_irq(shost->host_lock);
	}

	if (starget->can_queue <= 0)
		return 1;

	busy = atomic_inc_return(&starget->target_busy) - 1;
	if (atomic_read(&starget->target_blocked) > 0) {
		if (busy)
			goto starved;

		/*
		 * unblock after target_blocked iterates to zero
		 */
		if (atomic_dec_return(&starget->target_blocked) > 0)
			goto out_dec;

		SCSI_LOG_MLQUEUE(3, starget_printk(KERN_INFO, starget,
				 "unblocking target at zero depth\n"));
	}

	if (busy >= starget->can_queue)
		goto starved;

	return 1;

starved:
	spin_lock_irq(shost->host_lock);
	list_move_tail(&sdev->starved_entry, &shost->starved_list);
	spin_unlock_irq(shost->host_lock);
out_dec:
	if (starget->can_queue > 0)
		atomic_dec(&starget->target_busy);
	return 0;
}

/*
 * scsi_host_queue_ready: if we can send requests to shost, return 1 else
 * return 0. We must end up running the queue again whenever 0 is
 * returned, else IO can hang.
 */
static inline int scsi_host_queue_ready(struct request_queue *q,
				   struct Scsi_Host *shost,
				   struct scsi_device *sdev,
				   struct scsi_cmnd *cmd)
{
	if (scsi_host_in_recovery(shost))
		return 0;

	if (atomic_read(&shost->host_blocked) > 0) {
		if (scsi_host_busy(shost) > 0)
			goto starved;

		/*
		 * unblock after host_blocked iterates to zero
		 */
		if (atomic_dec_return(&shost->host_blocked) > 0)
			goto out_dec;

		SCSI_LOG_MLQUEUE(3,
			shost_printk(KERN_INFO, shost,
				     "unblocking host at zero depth\n"));
	}

	if (shost->host_self_blocked)
		goto starved;

	/* We're OK to process the command, so we can't be starved */
	if (!list_empty(&sdev->starved_entry)) {
		spin_lock_irq(shost->host_lock);
		if (!list_empty(&sdev->starved_entry))
			list_del_init(&sdev->starved_entry);
		spin_unlock_irq(shost->host_lock);
	}

	__set_bit(SCMD_STATE_INFLIGHT, &cmd->state);

	return 1;

starved:
	spin_lock_irq(shost->host_lock);
	if (list_empty(&sdev->starved_entry))
		list_add_tail(&sdev->starved_entry, &shost->starved_list);
	spin_unlock_irq(shost->host_lock);
out_dec:
	scsi_dec_host_busy(shost, cmd);
	return 0;
}

/*
 * Busy state exporting function for request stacking drivers.
 *
 * For efficiency, no lock is taken to check the busy state of
 * shost/starget/sdev, since the returned value is not guaranteed and
 * may be changed after request stacking drivers call the function,
 * regardless of taking lock or not.
 *
 * When scsi can't dispatch I/Os anymore and needs to kill I/Os scsi
 * needs to return 'not busy'. Otherwise, request stacking drivers
 * may hold requests forever.
 */
static bool scsi_mq_lld_busy(struct request_queue *q)
{
	struct scsi_device *sdev = q->queuedata;
	struct Scsi_Host *shost;

	if (blk_queue_dying(q))
		return false;

	shost = sdev->host;

	/*
	 * Ignore host/starget busy state.
	 * Since block layer does not have a concept of fairness across
	 * multiple queues, congestion of host/starget needs to be handled
	 * in SCSI layer.
	 */
	if (scsi_host_in_recovery(shost) || scsi_device_is_busy(sdev))
		return true;

	return false;
}

/*
 * Block layer request completion callback. May be called from interrupt
 * context.
 */
static void scsi_complete(struct request *rq)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);
	enum scsi_disposition disposition;

	INIT_LIST_HEAD(&cmd->eh_entry);

	atomic_inc(&cmd->device->iodone_cnt);
	if (cmd->result)
		atomic_inc(&cmd->device->ioerr_cnt);

	disposition = scsi_decide_disposition(cmd);
	if (disposition != SUCCESS && scsi_cmd_runtime_exceeced(cmd))
		disposition = SUCCESS;

	scsi_log_completion(cmd, disposition);

	switch (disposition) {
	case SUCCESS:
		scsi_finish_command(cmd);
		break;
	case NEEDS_RETRY:
		scsi_queue_insert(cmd, SCSI_MLQUEUE_EH_RETRY);
		break;
	case ADD_TO_MLQUEUE:
		scsi_queue_insert(cmd, SCSI_MLQUEUE_DEVICE_BUSY);
		break;
	default:
		scsi_eh_scmd_add(cmd);
		break;
	}
}

/**
 * scsi_dispatch_cmd - Dispatch a command to the low-level driver.
 * @cmd: command block we are dispatching.
 *
 * Return: nonzero return request was rejected and device's queue needs to be
 * plugged.
 */
static int scsi_dispatch_cmd(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	int rtn = 0;

	atomic_inc(&cmd->device->iorequest_cnt);

	/* check if the device is still usable */
	if (unlikely(cmd->device->sdev_state == SDEV_DEL)) {
		/* in SDEV_DEL we error all commands. DID_NO_CONNECT
		 * returns an immediate error upwards, and signals
		 * that the device is no longer present */
		cmd->result = DID_NO_CONNECT << 16;
		goto done;
	}

	/* Check to see if the scsi lld made this device blocked. */
	if (unlikely(scsi_device_blocked(cmd->device))) {
		/*
		 * in blocked state, the command is just put back on
		 * the device queue.  The suspend state has already
		 * blocked the queue so future requests should not
		 * occur until the device transitions out of the
		 * suspend state.
		 */
		SCSI_LOG_MLQUEUE(3, scmd_printk(KERN_INFO, cmd,
			"queuecommand : device blocked\n"));
		atomic_dec(&cmd->device->iorequest_cnt);
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	/* Store the LUN value in cmnd, if needed. */
	if (cmd->device->lun_in_cdb)
		cmd->cmnd[1] = (cmd->cmnd[1] & 0x1f) |
			       (cmd->device->lun << 5 & 0xe0);

	scsi_log_send(cmd);

	/*
	 * Before we queue this command, check if the command
	 * length exceeds what the host adapter can handle.
	 */
	if (cmd->cmd_len > cmd->device->host->max_cmd_len) {
		SCSI_LOG_MLQUEUE(3, scmd_printk(KERN_INFO, cmd,
			       "queuecommand : command too long. "
			       "cdb_size=%d host->max_cmd_len=%d\n",
			       cmd->cmd_len, cmd->device->host->max_cmd_len));
		cmd->result = (DID_ABORT << 16);
		goto done;
	}

	if (unlikely(host->shost_state == SHOST_DEL)) {
		cmd->result = (DID_NO_CONNECT << 16);
		goto done;

	}

	trace_scsi_dispatch_cmd_start(cmd);
	rtn = host->hostt->queuecommand(host, cmd);
	if (rtn) {
		atomic_dec(&cmd->device->iorequest_cnt);
		trace_scsi_dispatch_cmd_error(cmd, rtn);
		if (rtn != SCSI_MLQUEUE_DEVICE_BUSY &&
		    rtn != SCSI_MLQUEUE_TARGET_BUSY)
			rtn = SCSI_MLQUEUE_HOST_BUSY;

		SCSI_LOG_MLQUEUE(3, scmd_printk(KERN_INFO, cmd,
			"queuecommand : request rejected\n"));
	}

	return rtn;
 done:
	scsi_done(cmd);
	return 0;
}

/* Size in bytes of the sg-list stored in the scsi-mq command-private data. */
static unsigned int scsi_mq_inline_sgl_size(struct Scsi_Host *shost)
{
	return min_t(unsigned int, shost->sg_tablesize, SCSI_INLINE_SG_CNT) *
		sizeof(struct scatterlist);
}

static blk_status_t scsi_prepare_cmd(struct request *req)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);
	struct scsi_device *sdev = req->q->queuedata;
	struct Scsi_Host *shost = sdev->host;
	bool in_flight = test_bit(SCMD_STATE_INFLIGHT, &cmd->state);
	struct scatterlist *sg;

	scsi_init_command(sdev, cmd);

	cmd->eh_eflags = 0;
	cmd->prot_type = 0;
	cmd->prot_flags = 0;
	cmd->submitter = 0;
	memset(&cmd->sdb, 0, sizeof(cmd->sdb));
	cmd->underflow = 0;
	cmd->transfersize = 0;
	cmd->host_scribble = NULL;
	cmd->result = 0;
	cmd->extra_len = 0;
	cmd->state = 0;
	if (in_flight)
		__set_bit(SCMD_STATE_INFLIGHT, &cmd->state);

	/*
	 * Only clear the driver-private command data if the LLD does not supply
	 * a function to initialize that data.
	 */
	if (!shost->hostt->init_cmd_priv)
		memset(cmd + 1, 0, shost->hostt->cmd_size);

	cmd->prot_op = SCSI_PROT_NORMAL;
	if (blk_rq_bytes(req))
		cmd->sc_data_direction = rq_dma_dir(req);
	else
		cmd->sc_data_direction = DMA_NONE;

	sg = (void *)cmd + sizeof(struct scsi_cmnd) + shost->hostt->cmd_size;
	cmd->sdb.table.sgl = sg;

	if (scsi_host_get_prot(shost)) {
		memset(cmd->prot_sdb, 0, sizeof(struct scsi_data_buffer));

		cmd->prot_sdb->table.sgl =
			(struct scatterlist *)(cmd->prot_sdb + 1);
	}

	/*
	 * Special handling for passthrough commands, which don't go to the ULP
	 * at all:
	 */
	if (blk_rq_is_passthrough(req))
		return scsi_setup_scsi_cmnd(sdev, req);

	if (sdev->handler && sdev->handler->prep_fn) {
		blk_status_t ret = sdev->handler->prep_fn(sdev, req);

		if (ret != BLK_STS_OK)
			return ret;
	}

	/* Usually overridden by the ULP */
	cmd->allowed = 0;
	memset(cmd->cmnd, 0, sizeof(cmd->cmnd));
	return scsi_cmd_to_driver(cmd)->init_command(cmd);
}

static void scsi_done_internal(struct scsi_cmnd *cmd, bool complete_directly)
{
	struct request *req = scsi_cmd_to_rq(cmd);

	switch (cmd->submitter) {
	case SUBMITTED_BY_BLOCK_LAYER:
		break;
	case SUBMITTED_BY_SCSI_ERROR_HANDLER:
		return scsi_eh_done(cmd);
	case SUBMITTED_BY_SCSI_RESET_IOCTL:
		return;
	}

	if (unlikely(blk_should_fake_timeout(scsi_cmd_to_rq(cmd)->q)))
		return;
	if (unlikely(test_and_set_bit(SCMD_STATE_COMPLETE, &cmd->state)))
		return;
	trace_scsi_dispatch_cmd_done(cmd);

	if (complete_directly)
		blk_mq_complete_request_direct(req, scsi_complete);
	else
		blk_mq_complete_request(req);
}

void scsi_done(struct scsi_cmnd *cmd)
{
	scsi_done_internal(cmd, false);
}
EXPORT_SYMBOL(scsi_done);

void scsi_done_direct(struct scsi_cmnd *cmd)
{
	scsi_done_internal(cmd, true);
}
EXPORT_SYMBOL(scsi_done_direct);

static void scsi_mq_put_budget(struct request_queue *q, int budget_token)
{
	struct scsi_device *sdev = q->queuedata;

	sbitmap_put(&sdev->budget_map, budget_token);
}

/*
 * When to reinvoke queueing after a resource shortage. It's 3 msecs to
 * not change behaviour from the previous unplug mechanism, experimentation
 * may prove this needs changing.
 */
#define SCSI_QUEUE_DELAY 3

static int scsi_mq_get_budget(struct request_queue *q)
{
	struct scsi_device *sdev = q->queuedata;
	int token = scsi_dev_queue_ready(q, sdev);

	if (token >= 0)
		return token;

	atomic_inc(&sdev->restarts);

	/*
	 * Orders atomic_inc(&sdev->restarts) and atomic_read(&sdev->device_busy).
	 * .restarts must be incremented before .device_busy is read because the
	 * code in scsi_run_queue_async() depends on the order of these operations.
	 */
	smp_mb__after_atomic();

	/*
	 * If all in-flight requests originated from this LUN are completed
	 * before reading .device_busy, sdev->device_busy will be observed as
	 * zero, then blk_mq_delay_run_hw_queues() will dispatch this request
	 * soon. Otherwise, completion of one of these requests will observe
	 * the .restarts flag, and the request queue will be run for handling
	 * this request, see scsi_end_request().
	 */
	if (unlikely(scsi_device_busy(sdev) == 0 &&
				!scsi_device_blocked(sdev)))
		blk_mq_delay_run_hw_queues(sdev->request_queue, SCSI_QUEUE_DELAY);
	return -1;
}

static void scsi_mq_set_rq_budget_token(struct request *req, int token)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);

	cmd->budget_token = token;
}

static int scsi_mq_get_rq_budget_token(struct request *req)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);

	return cmd->budget_token;
}

static blk_status_t scsi_queue_rq(struct blk_mq_hw_ctx *hctx,
			 const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	struct request_queue *q = req->q;
	struct scsi_device *sdev = q->queuedata;
	struct Scsi_Host *shost = sdev->host;
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);
	blk_status_t ret;
	int reason;

	WARN_ON_ONCE(cmd->budget_token < 0);

	/*
	 * If the device is not in running state we will reject some or all
	 * commands.
	 */
	if (unlikely(sdev->sdev_state != SDEV_RUNNING)) {
		ret = scsi_device_state_check(sdev, req);
		if (ret != BLK_STS_OK)
			goto out_put_budget;
	}

	ret = BLK_STS_RESOURCE;
	if (!scsi_target_queue_ready(shost, sdev))
		goto out_put_budget;
	if (!scsi_host_queue_ready(q, shost, sdev, cmd))
		goto out_dec_target_busy;

	if (!(req->rq_flags & RQF_DONTPREP)) {
		ret = scsi_prepare_cmd(req);
		if (ret != BLK_STS_OK)
			goto out_dec_host_busy;
		req->rq_flags |= RQF_DONTPREP;
	} else {
		clear_bit(SCMD_STATE_COMPLETE, &cmd->state);
	}

	cmd->flags &= SCMD_PRESERVED_FLAGS;
	if (sdev->simple_tags)
		cmd->flags |= SCMD_TAGGED;
	if (bd->last)
		cmd->flags |= SCMD_LAST;

	scsi_set_resid(cmd, 0);
	memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
	cmd->submitter = SUBMITTED_BY_BLOCK_LAYER;

	blk_mq_start_request(req);
	reason = scsi_dispatch_cmd(cmd);
	if (reason) {
		scsi_set_blocked(cmd, reason);
		ret = BLK_STS_RESOURCE;
		goto out_dec_host_busy;
	}

	return BLK_STS_OK;

out_dec_host_busy:
	scsi_dec_host_busy(shost, cmd);
out_dec_target_busy:
	if (scsi_target(sdev)->can_queue > 0)
		atomic_dec(&scsi_target(sdev)->target_busy);
out_put_budget:
	scsi_mq_put_budget(q, cmd->budget_token);
	cmd->budget_token = -1;
	switch (ret) {
	case BLK_STS_OK:
		break;
	case BLK_STS_RESOURCE:
	case BLK_STS_ZONE_RESOURCE:
		if (scsi_device_blocked(sdev))
			ret = BLK_STS_DEV_RESOURCE;
		break;
	case BLK_STS_AGAIN:
		cmd->result = DID_BUS_BUSY << 16;
		if (req->rq_flags & RQF_DONTPREP)
			scsi_mq_uninit_cmd(cmd);
		break;
	default:
		if (unlikely(!scsi_device_online(sdev)))
			cmd->result = DID_NO_CONNECT << 16;
		else
			cmd->result = DID_ERROR << 16;
		/*
		 * Make sure to release all allocated resources when
		 * we hit an error, as we will never see this command
		 * again.
		 */
		if (req->rq_flags & RQF_DONTPREP)
			scsi_mq_uninit_cmd(cmd);
		scsi_run_queue_async(sdev);
		break;
	}
	return ret;
}

static int scsi_mq_init_request(struct blk_mq_tag_set *set, struct request *rq,
				unsigned int hctx_idx, unsigned int numa_node)
{
	struct Scsi_Host *shost = set->driver_data;
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);
	struct scatterlist *sg;
	int ret = 0;

	cmd->sense_buffer =
		kmem_cache_alloc_node(scsi_sense_cache, GFP_KERNEL, numa_node);
	if (!cmd->sense_buffer)
		return -ENOMEM;

	if (scsi_host_get_prot(shost)) {
		sg = (void *)cmd + sizeof(struct scsi_cmnd) +
			shost->hostt->cmd_size;
		cmd->prot_sdb = (void *)sg + scsi_mq_inline_sgl_size(shost);
	}

	if (shost->hostt->init_cmd_priv) {
		ret = shost->hostt->init_cmd_priv(shost, cmd);
		if (ret < 0)
			kmem_cache_free(scsi_sense_cache, cmd->sense_buffer);
	}

	return ret;
}

static void scsi_mq_exit_request(struct blk_mq_tag_set *set, struct request *rq,
				 unsigned int hctx_idx)
{
	struct Scsi_Host *shost = set->driver_data;
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);

	if (shost->hostt->exit_cmd_priv)
		shost->hostt->exit_cmd_priv(shost, cmd);
	kmem_cache_free(scsi_sense_cache, cmd->sense_buffer);
}


static int scsi_mq_poll(struct blk_mq_hw_ctx *hctx, struct io_comp_batch *iob)
{
	struct Scsi_Host *shost = hctx->driver_data;

	if (shost->hostt->mq_poll)
		return shost->hostt->mq_poll(shost, hctx->queue_num);

	return 0;
}

static int scsi_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
			  unsigned int hctx_idx)
{
	struct Scsi_Host *shost = data;

	hctx->driver_data = shost;
	return 0;
}

static void scsi_map_queues(struct blk_mq_tag_set *set)
{
	struct Scsi_Host *shost = container_of(set, struct Scsi_Host, tag_set);

	if (shost->hostt->map_queues)
		return shost->hostt->map_queues(shost);
	blk_mq_map_queues(&set->map[HCTX_TYPE_DEFAULT]);
}

void __scsi_init_queue(struct Scsi_Host *shost, struct request_queue *q)
{
	struct device *dev = shost->dma_dev;

	/*
	 * this limit is imposed by hardware restrictions
	 */
	blk_queue_max_segments(q, min_t(unsigned short, shost->sg_tablesize,
					SG_MAX_SEGMENTS));

	if (scsi_host_prot_dma(shost)) {
		shost->sg_prot_tablesize =
			min_not_zero(shost->sg_prot_tablesize,
				     (unsigned short)SCSI_MAX_PROT_SG_SEGMENTS);
		BUG_ON(shost->sg_prot_tablesize < shost->sg_tablesize);
		blk_queue_max_integrity_segments(q, shost->sg_prot_tablesize);
	}

	blk_queue_max_hw_sectors(q, shost->max_sectors);
	blk_queue_segment_boundary(q, shost->dma_boundary);
	dma_set_seg_boundary(dev, shost->dma_boundary);

	blk_queue_max_segment_size(q, shost->max_segment_size);
	blk_queue_virt_boundary(q, shost->virt_boundary_mask);
	dma_set_max_seg_size(dev, queue_max_segment_size(q));

	/*
	 * Set a reasonable default alignment:  The larger of 32-byte (dword),
	 * which is a common minimum for HBAs, and the minimum DMA alignment,
	 * which is set by the platform.
	 *
	 * Devices that require a bigger alignment can increase it later.
	 */
	blk_queue_dma_alignment(q, max(4, dma_get_cache_alignment()) - 1);
}
EXPORT_SYMBOL_GPL(__scsi_init_queue);

static const struct blk_mq_ops scsi_mq_ops_no_commit = {
	.get_budget	= scsi_mq_get_budget,
	.put_budget	= scsi_mq_put_budget,
	.queue_rq	= scsi_queue_rq,
	.complete	= scsi_complete,
	.timeout	= scsi_timeout,
#ifdef CONFIG_BLK_DEBUG_FS
	.show_rq	= scsi_show_rq,
#endif
	.init_request	= scsi_mq_init_request,
	.exit_request	= scsi_mq_exit_request,
	.cleanup_rq	= scsi_cleanup_rq,
	.busy		= scsi_mq_lld_busy,
	.map_queues	= scsi_map_queues,
	.init_hctx	= scsi_init_hctx,
	.poll		= scsi_mq_poll,
	.set_rq_budget_token = scsi_mq_set_rq_budget_token,
	.get_rq_budget_token = scsi_mq_get_rq_budget_token,
};


static void scsi_commit_rqs(struct blk_mq_hw_ctx *hctx)
{
	struct Scsi_Host *shost = hctx->driver_data;

	shost->hostt->commit_rqs(shost, hctx->queue_num);
}

static const struct blk_mq_ops scsi_mq_ops = {
	.get_budget	= scsi_mq_get_budget,
	.put_budget	= scsi_mq_put_budget,
	.queue_rq	= scsi_queue_rq,
	.commit_rqs	= scsi_commit_rqs,
	.complete	= scsi_complete,
	.timeout	= scsi_timeout,
#ifdef CONFIG_BLK_DEBUG_FS
	.show_rq	= scsi_show_rq,
#endif
	.init_request	= scsi_mq_init_request,
	.exit_request	= scsi_mq_exit_request,
	.cleanup_rq	= scsi_cleanup_rq,
	.busy		= scsi_mq_lld_busy,
	.map_queues	= scsi_map_queues,
	.init_hctx	= scsi_init_hctx,
	.poll		= scsi_mq_poll,
	.set_rq_budget_token = scsi_mq_set_rq_budget_token,
	.get_rq_budget_token = scsi_mq_get_rq_budget_token,
};

int scsi_mq_setup_tags(struct Scsi_Host *shost)
{
	unsigned int cmd_size, sgl_size;
	struct blk_mq_tag_set *tag_set = &shost->tag_set;

	sgl_size = max_t(unsigned int, sizeof(struct scatterlist),
				scsi_mq_inline_sgl_size(shost));
	cmd_size = sizeof(struct scsi_cmnd) + shost->hostt->cmd_size + sgl_size;
	if (scsi_host_get_prot(shost))
		cmd_size += sizeof(struct scsi_data_buffer) +
			sizeof(struct scatterlist) * SCSI_INLINE_PROT_SG_CNT;

	memset(tag_set, 0, sizeof(*tag_set));
	if (shost->hostt->commit_rqs)
		tag_set->ops = &scsi_mq_ops;
	else
		tag_set->ops = &scsi_mq_ops_no_commit;
	tag_set->nr_hw_queues = shost->nr_hw_queues ? : 1;
	tag_set->nr_maps = shost->nr_maps ? : 1;
	tag_set->queue_depth = shost->can_queue;
	tag_set->cmd_size = cmd_size;
	tag_set->numa_node = dev_to_node(shost->dma_dev);
	tag_set->flags = BLK_MQ_F_SHOULD_MERGE;
	tag_set->flags |=
		BLK_ALLOC_POLICY_TO_MQ_FLAG(shost->hostt->tag_alloc_policy);
	tag_set->driver_data = shost;
	if (shost->host_tagset)
		tag_set->flags |= BLK_MQ_F_TAG_HCTX_SHARED;

	return blk_mq_alloc_tag_set(tag_set);
}

void scsi_mq_free_tags(struct kref *kref)
{
	struct Scsi_Host *shost = container_of(kref, typeof(*shost),
					       tagset_refcnt);

	blk_mq_free_tag_set(&shost->tag_set);
	complete(&shost->tagset_freed);
}

/**
 * scsi_device_from_queue - return sdev associated with a request_queue
 * @q: The request queue to return the sdev from
 *
 * Return the sdev associated with a request queue or NULL if the
 * request_queue does not reference a SCSI device.
 */
struct scsi_device *scsi_device_from_queue(struct request_queue *q)
{
	struct scsi_device *sdev = NULL;

	if (q->mq_ops == &scsi_mq_ops_no_commit ||
	    q->mq_ops == &scsi_mq_ops)
		sdev = q->queuedata;
	if (!sdev || !get_device(&sdev->sdev_gendev))
		sdev = NULL;

	return sdev;
}
/*
 * pktcdvd should have been integrated into the SCSI layers, but for historical
 * reasons like the old IDE driver it isn't.  This export allows it to safely
 * probe if a given device is a SCSI one and only attach to that.
 */
#ifdef CONFIG_CDROM_PKTCDVD_MODULE
EXPORT_SYMBOL_GPL(scsi_device_from_queue);
#endif

/**
 * scsi_block_requests - Utility function used by low-level drivers to prevent
 * further commands from being queued to the device.
 * @shost:  host in question
 *
 * There is no timer nor any other means by which the requests get unblocked
 * other than the low-level driver calling scsi_unblock_requests().
 */
void scsi_block_requests(struct Scsi_Host *shost)
{
	shost->host_self_blocked = 1;
}
EXPORT_SYMBOL(scsi_block_requests);

/**
 * scsi_unblock_requests - Utility function used by low-level drivers to allow
 * further commands to be queued to the device.
 * @shost:  host in question
 *
 * There is no timer nor any other means by which the requests get unblocked
 * other than the low-level driver calling scsi_unblock_requests(). This is done
 * as an API function so that changes to the internals of the scsi mid-layer
 * won't require wholesale changes to drivers that use this feature.
 */
void scsi_unblock_requests(struct Scsi_Host *shost)
{
	shost->host_self_blocked = 0;
	scsi_run_host_queues(shost);
}
EXPORT_SYMBOL(scsi_unblock_requests);

void scsi_exit_queue(void)
{
	kmem_cache_destroy(scsi_sense_cache);
}

/**
 *	scsi_mode_select - issue a mode select
 *	@sdev:	SCSI device to be queried
 *	@pf:	Page format bit (1 == standard, 0 == vendor specific)
 *	@sp:	Save page bit (0 == don't save, 1 == save)
 *	@buffer: request buffer (may not be smaller than eight bytes)
 *	@len:	length of request buffer.
 *	@timeout: command timeout
 *	@retries: number of retries before failing
 *	@data: returns a structure abstracting the mode header data
 *	@sshdr: place to put sense data (or NULL if no sense to be collected).
 *		must be SCSI_SENSE_BUFFERSIZE big.
 *
 *	Returns zero if successful; negative error number or scsi
 *	status on error
 *
 */
int scsi_mode_select(struct scsi_device *sdev, int pf, int sp,
		     unsigned char *buffer, int len, int timeout, int retries,
		     struct scsi_mode_data *data, struct scsi_sense_hdr *sshdr)
{
	unsigned char cmd[10];
	unsigned char *real_buffer;
	int ret;

	memset(cmd, 0, sizeof(cmd));
	cmd[1] = (pf ? 0x10 : 0) | (sp ? 0x01 : 0);

	/*
	 * Use MODE SELECT(10) if the device asked for it or if the mode page
	 * and the mode select header cannot fit within the maximumm 255 bytes
	 * of the MODE SELECT(6) command.
	 */
	if (sdev->use_10_for_ms ||
	    len + 4 > 255 ||
	    data->block_descriptor_length > 255) {
		if (len > 65535 - 8)
			return -EINVAL;
		real_buffer = kmalloc(8 + len, GFP_KERNEL);
		if (!real_buffer)
			return -ENOMEM;
		memcpy(real_buffer + 8, buffer, len);
		len += 8;
		real_buffer[0] = 0;
		real_buffer[1] = 0;
		real_buffer[2] = data->medium_type;
		real_buffer[3] = data->device_specific;
		real_buffer[4] = data->longlba ? 0x01 : 0;
		real_buffer[5] = 0;
		put_unaligned_be16(data->block_descriptor_length,
				   &real_buffer[6]);

		cmd[0] = MODE_SELECT_10;
		put_unaligned_be16(len, &cmd[7]);
	} else {
		if (data->longlba)
			return -EINVAL;

		real_buffer = kmalloc(4 + len, GFP_KERNEL);
		if (!real_buffer)
			return -ENOMEM;
		memcpy(real_buffer + 4, buffer, len);
		len += 4;
		real_buffer[0] = 0;
		real_buffer[1] = data->medium_type;
		real_buffer[2] = data->device_specific;
		real_buffer[3] = data->block_descriptor_length;

		cmd[0] = MODE_SELECT;
		cmd[4] = len;
	}

	ret = scsi_execute_req(sdev, cmd, DMA_TO_DEVICE, real_buffer, len,
			       sshdr, timeout, retries, NULL);
	kfree(real_buffer);
	return ret;
}
EXPORT_SYMBOL_GPL(scsi_mode_select);

/**
 *	scsi_mode_sense - issue a mode sense, falling back from 10 to six bytes if necessary.
 *	@sdev:	SCSI device to be queried
 *	@dbd:	set to prevent mode sense from returning block descriptors
 *	@modepage: mode page being requested
 *	@buffer: request buffer (may not be smaller than eight bytes)
 *	@len:	length of request buffer.
 *	@timeout: command timeout
 *	@retries: number of retries before failing
 *	@data: returns a structure abstracting the mode header data
 *	@sshdr: place to put sense data (or NULL if no sense to be collected).
 *		must be SCSI_SENSE_BUFFERSIZE big.
 *
 *	Returns zero if successful, or a negative error number on failure
 */
int
scsi_mode_sense(struct scsi_device *sdev, int dbd, int modepage,
		  unsigned char *buffer, int len, int timeout, int retries,
		  struct scsi_mode_data *data, struct scsi_sense_hdr *sshdr)
{
	unsigned char cmd[12];
	int use_10_for_ms;
	int header_length;
	int result, retry_count = retries;
	struct scsi_sense_hdr my_sshdr;

	memset(data, 0, sizeof(*data));
	memset(&cmd[0], 0, 12);

	dbd = sdev->set_dbd_for_ms ? 8 : dbd;
	cmd[1] = dbd & 0x18;	/* allows DBD and LLBA bits */
	cmd[2] = modepage;

	/* caller might not be interested in sense, but we need it */
	if (!sshdr)
		sshdr = &my_sshdr;

 retry:
	use_10_for_ms = sdev->use_10_for_ms || len > 255;

	if (use_10_for_ms) {
		if (len < 8 || len > 65535)
			return -EINVAL;

		cmd[0] = MODE_SENSE_10;
		put_unaligned_be16(len, &cmd[7]);
		header_length = 8;
	} else {
		if (len < 4)
			return -EINVAL;

		cmd[0] = MODE_SENSE;
		cmd[4] = len;
		header_length = 4;
	}

	memset(buffer, 0, len);

	result = scsi_execute_req(sdev, cmd, DMA_FROM_DEVICE, buffer, len,
				  sshdr, timeout, retries, NULL);
	if (result < 0)
		return result;

	/* This code looks awful: what it's doing is making sure an
	 * ILLEGAL REQUEST sense return identifies the actual command
	 * byte as the problem.  MODE_SENSE commands can return
	 * ILLEGAL REQUEST if the code page isn't supported */

	if (!scsi_status_is_good(result)) {
		if (scsi_sense_valid(sshdr)) {
			if ((sshdr->sense_key == ILLEGAL_REQUEST) &&
			    (sshdr->asc == 0x20) && (sshdr->ascq == 0)) {
				/*
				 * Invalid command operation code: retry using
				 * MODE SENSE(6) if this was a MODE SENSE(10)
				 * request, except if the request mode page is
				 * too large for MODE SENSE single byte
				 * allocation length field.
				 */
				if (use_10_for_ms) {
					if (len > 255)
						return -EIO;
					sdev->use_10_for_ms = 0;
					goto retry;
				}
			}
			if (scsi_status_is_check_condition(result) &&
			    sshdr->sense_key == UNIT_ATTENTION &&
			    retry_count) {
				retry_count--;
				goto retry;
			}
		}
		return -EIO;
	}
	if (unlikely(buffer[0] == 0x86 && buffer[1] == 0x0b &&
		     (modepage == 6 || modepage == 8))) {
		/* Initio breakage? */
		header_length = 0;
		data->length = 13;
		data->medium_type = 0;
		data->device_specific = 0;
		data->longlba = 0;
		data->block_descriptor_length = 0;
	} else if (use_10_for_ms) {
		data->length = get_unaligned_be16(&buffer[0]) + 2;
		data->medium_type = buffer[2];
		data->device_specific = buffer[3];
		data->longlba = buffer[4] & 0x01;
		data->block_descriptor_length = get_unaligned_be16(&buffer[6]);
	} else {
		data->length = buffer[0] + 1;
		data->medium_type = buffer[1];
		data->device_specific = buffer[2];
		data->block_descriptor_length = buffer[3];
	}
	data->header_length = header_length;

	return 0;
}
EXPORT_SYMBOL(scsi_mode_sense);

/**
 *	scsi_test_unit_ready - test if unit is ready
 *	@sdev:	scsi device to change the state of.
 *	@timeout: command timeout
 *	@retries: number of retries before failing
 *	@sshdr: outpout pointer for decoded sense information.
 *
 *	Returns zero if unsuccessful or an error if TUR failed.  For
 *	removable media, UNIT_ATTENTION sets ->changed flag.
 **/
int
scsi_test_unit_ready(struct scsi_device *sdev, int timeout, int retries,
		     struct scsi_sense_hdr *sshdr)
{
	char cmd[] = {
		TEST_UNIT_READY, 0, 0, 0, 0, 0,
	};
	int result;

	/* try to eat the UNIT_ATTENTION if there are enough retries */
	do {
		result = scsi_execute_req(sdev, cmd, DMA_NONE, NULL, 0, sshdr,
					  timeout, 1, NULL);
		if (sdev->removable && scsi_sense_valid(sshdr) &&
		    sshdr->sense_key == UNIT_ATTENTION)
			sdev->changed = 1;
	} while (scsi_sense_valid(sshdr) &&
		 sshdr->sense_key == UNIT_ATTENTION && --retries);

	return result;
}
EXPORT_SYMBOL(scsi_test_unit_ready);

/**
 *	scsi_device_set_state - Take the given device through the device state model.
 *	@sdev:	scsi device to change the state of.
 *	@state:	state to change to.
 *
 *	Returns zero if successful or an error if the requested
 *	transition is illegal.
 */
int
scsi_device_set_state(struct scsi_device *sdev, enum scsi_device_state state)
{
	enum scsi_device_state oldstate = sdev->sdev_state;

	if (state == oldstate)
		return 0;

	switch (state) {
	case SDEV_CREATED:
		switch (oldstate) {
		case SDEV_CREATED_BLOCK:
			break;
		default:
			goto illegal;
		}
		break;

	case SDEV_RUNNING:
		switch (oldstate) {
		case SDEV_CREATED:
		case SDEV_OFFLINE:
		case SDEV_TRANSPORT_OFFLINE:
		case SDEV_QUIESCE:
		case SDEV_BLOCK:
			break;
		default:
			goto illegal;
		}
		break;

	case SDEV_QUIESCE:
		switch (oldstate) {
		case SDEV_RUNNING:
		case SDEV_OFFLINE:
		case SDEV_TRANSPORT_OFFLINE:
			break;
		default:
			goto illegal;
		}
		break;

	case SDEV_OFFLINE:
	case SDEV_TRANSPORT_OFFLINE:
		switch (oldstate) {
		case SDEV_CREATED:
		case SDEV_RUNNING:
		case SDEV_QUIESCE:
		case SDEV_BLOCK:
			break;
		default:
			goto illegal;
		}
		break;

	case SDEV_BLOCK:
		switch (oldstate) {
		case SDEV_RUNNING:
		case SDEV_CREATED_BLOCK:
		case SDEV_QUIESCE:
		case SDEV_OFFLINE:
			break;
		default:
			goto illegal;
		}
		break;

	case SDEV_CREATED_BLOCK:
		switch (oldstate) {
		case SDEV_CREATED:
			break;
		default:
			goto illegal;
		}
		break;

	case SDEV_CANCEL:
		switch (oldstate) {
		case SDEV_CREATED:
		case SDEV_RUNNING:
		case SDEV_QUIESCE:
		case SDEV_OFFLINE:
		case SDEV_TRANSPORT_OFFLINE:
			break;
		default:
			goto illegal;
		}
		break;

	case SDEV_DEL:
		switch (oldstate) {
		case SDEV_CREATED:
		case SDEV_RUNNING:
		case SDEV_OFFLINE:
		case SDEV_TRANSPORT_OFFLINE:
		case SDEV_CANCEL:
		case SDEV_BLOCK:
		case SDEV_CREATED_BLOCK:
			break;
		default:
			goto illegal;
		}
		break;

	}
	sdev->offline_already = false;
	sdev->sdev_state = state;
	return 0;

 illegal:
	SCSI_LOG_ERROR_RECOVERY(1,
				sdev_printk(KERN_ERR, sdev,
					    "Illegal state transition %s->%s",
					    scsi_device_state_name(oldstate),
					    scsi_device_state_name(state))
				);
	return -EINVAL;
}
EXPORT_SYMBOL(scsi_device_set_state);

/**
 *	scsi_evt_emit - emit a single SCSI device uevent
 *	@sdev: associated SCSI device
 *	@evt: event to emit
 *
 *	Send a single uevent (scsi_event) to the associated scsi_device.
 */
static void scsi_evt_emit(struct scsi_device *sdev, struct scsi_event *evt)
{
	int idx = 0;
	char *envp[3];

	switch (evt->evt_type) {
	case SDEV_EVT_MEDIA_CHANGE:
		envp[idx++] = "SDEV_MEDIA_CHANGE=1";
		break;
	case SDEV_EVT_INQUIRY_CHANGE_REPORTED:
		scsi_rescan_device(sdev);
		envp[idx++] = "SDEV_UA=INQUIRY_DATA_HAS_CHANGED";
		break;
	case SDEV_EVT_CAPACITY_CHANGE_REPORTED:
		envp[idx++] = "SDEV_UA=CAPACITY_DATA_HAS_CHANGED";
		break;
	case SDEV_EVT_SOFT_THRESHOLD_REACHED_REPORTED:
	       envp[idx++] = "SDEV_UA=THIN_PROVISIONING_SOFT_THRESHOLD_REACHED";
		break;
	case SDEV_EVT_MODE_PARAMETER_CHANGE_REPORTED:
		envp[idx++] = "SDEV_UA=MODE_PARAMETERS_CHANGED";
		break;
	case SDEV_EVT_LUN_CHANGE_REPORTED:
		envp[idx++] = "SDEV_UA=REPORTED_LUNS_DATA_HAS_CHANGED";
		break;
	case SDEV_EVT_ALUA_STATE_CHANGE_REPORTED:
		envp[idx++] = "SDEV_UA=ASYMMETRIC_ACCESS_STATE_CHANGED";
		break;
	case SDEV_EVT_POWER_ON_RESET_OCCURRED:
		envp[idx++] = "SDEV_UA=POWER_ON_RESET_OCCURRED";
		break;
	default:
		/* do nothing */
		break;
	}

	envp[idx++] = NULL;

	kobject_uevent_env(&sdev->sdev_gendev.kobj, KOBJ_CHANGE, envp);
}

/**
 *	scsi_evt_thread - send a uevent for each scsi event
 *	@work: work struct for scsi_device
 *
 *	Dispatch queued events to their associated scsi_device kobjects
 *	as uevents.
 */
void scsi_evt_thread(struct work_struct *work)
{
	struct scsi_device *sdev;
	enum scsi_device_event evt_type;
	LIST_HEAD(event_list);

	sdev = container_of(work, struct scsi_device, event_work);

	for (evt_type = SDEV_EVT_FIRST; evt_type <= SDEV_EVT_LAST; evt_type++)
		if (test_and_clear_bit(evt_type, sdev->pending_events))
			sdev_evt_send_simple(sdev, evt_type, GFP_KERNEL);

	while (1) {
		struct scsi_event *evt;
		struct list_head *this, *tmp;
		unsigned long flags;

		spin_lock_irqsave(&sdev->list_lock, flags);
		list_splice_init(&sdev->event_list, &event_list);
		spin_unlock_irqrestore(&sdev->list_lock, flags);

		if (list_empty(&event_list))
			break;

		list_for_each_safe(this, tmp, &event_list) {
			evt = list_entry(this, struct scsi_event, node);
			list_del(&evt->node);
			scsi_evt_emit(sdev, evt);
			kfree(evt);
		}
	}
}

/**
 * 	sdev_evt_send - send asserted event to uevent thread
 *	@sdev: scsi_device event occurred on
 *	@evt: event to send
 *
 *	Assert scsi device event asynchronously.
 */
void sdev_evt_send(struct scsi_device *sdev, struct scsi_event *evt)
{
	unsigned long flags;

#if 0
	/* FIXME: currently this check eliminates all media change events
	 * for polled devices.  Need to update to discriminate between AN
	 * and polled events */
	if (!test_bit(evt->evt_type, sdev->supported_events)) {
		kfree(evt);
		return;
	}
#endif

	spin_lock_irqsave(&sdev->list_lock, flags);
	list_add_tail(&evt->node, &sdev->event_list);
	schedule_work(&sdev->event_work);
	spin_unlock_irqrestore(&sdev->list_lock, flags);
}
EXPORT_SYMBOL_GPL(sdev_evt_send);

/**
 * 	sdev_evt_alloc - allocate a new scsi event
 *	@evt_type: type of event to allocate
 *	@gfpflags: GFP flags for allocation
 *
 *	Allocates and returns a new scsi_event.
 */
struct scsi_event *sdev_evt_alloc(enum scsi_device_event evt_type,
				  gfp_t gfpflags)
{
	struct scsi_event *evt = kzalloc(sizeof(struct scsi_event), gfpflags);
	if (!evt)
		return NULL;

	evt->evt_type = evt_type;
	INIT_LIST_HEAD(&evt->node);

	/* evt_type-specific initialization, if any */
	switch (evt_type) {
	case SDEV_EVT_MEDIA_CHANGE:
	case SDEV_EVT_INQUIRY_CHANGE_REPORTED:
	case SDEV_EVT_CAPACITY_CHANGE_REPORTED:
	case SDEV_EVT_SOFT_THRESHOLD_REACHED_REPORTED:
	case SDEV_EVT_MODE_PARAMETER_CHANGE_REPORTED:
	case SDEV_EVT_LUN_CHANGE_REPORTED:
	case SDEV_EVT_ALUA_STATE_CHANGE_REPORTED:
	case SDEV_EVT_POWER_ON_RESET_OCCURRED:
	default:
		/* do nothing */
		break;
	}

	return evt;
}
EXPORT_SYMBOL_GPL(sdev_evt_alloc);

/**
 * 	sdev_evt_send_simple - send asserted event to uevent thread
 *	@sdev: scsi_device event occurred on
 *	@evt_type: type of event to send
 *	@gfpflags: GFP flags for allocation
 *
 *	Assert scsi device event asynchronously, given an event type.
 */
void sdev_evt_send_simple(struct scsi_device *sdev,
			  enum scsi_device_event evt_type, gfp_t gfpflags)
{
	struct scsi_event *evt = sdev_evt_alloc(evt_type, gfpflags);
	if (!evt) {
		sdev_printk(KERN_ERR, sdev, "event %d eaten due to OOM\n",
			    evt_type);
		return;
	}

	sdev_evt_send(sdev, evt);
}
EXPORT_SYMBOL_GPL(sdev_evt_send_simple);

/**
 *	scsi_device_quiesce - Block all commands except power management.
 *	@sdev:	scsi device to quiesce.
 *
 *	This works by trying to transition to the SDEV_QUIESCE state
 *	(which must be a legal transition).  When the device is in this
 *	state, only power management requests will be accepted, all others will
 *	be deferred.
 *
 *	Must be called with user context, may sleep.
 *
 *	Returns zero if unsuccessful or an error if not.
 */
int
scsi_device_quiesce(struct scsi_device *sdev)
{
	struct request_queue *q = sdev->request_queue;
	int err;

	/*
	 * It is allowed to call scsi_device_quiesce() multiple times from
	 * the same context but concurrent scsi_device_quiesce() calls are
	 * not allowed.
	 */
	WARN_ON_ONCE(sdev->quiesced_by && sdev->quiesced_by != current);

	if (sdev->quiesced_by == current)
		return 0;

	blk_set_pm_only(q);

	blk_mq_freeze_queue(q);
	/*
	 * Ensure that the effect of blk_set_pm_only() will be visible
	 * for percpu_ref_tryget() callers that occur after the queue
	 * unfreeze even if the queue was already frozen before this function
	 * was called. See also https://lwn.net/Articles/573497/.
	 */
	synchronize_rcu();
	blk_mq_unfreeze_queue(q);

	mutex_lock(&sdev->state_mutex);
	err = scsi_device_set_state(sdev, SDEV_QUIESCE);
	if (err == 0)
		sdev->quiesced_by = current;
	else
		blk_clear_pm_only(q);
	mutex_unlock(&sdev->state_mutex);

	return err;
}
EXPORT_SYMBOL(scsi_device_quiesce);

/**
 *	scsi_device_resume - Restart user issued commands to a quiesced device.
 *	@sdev:	scsi device to resume.
 *
 *	Moves the device from quiesced back to running and restarts the
 *	queues.
 *
 *	Must be called with user context, may sleep.
 */
void scsi_device_resume(struct scsi_device *sdev)
{
	/* check if the device state was mutated prior to resume, and if
	 * so assume the state is being managed elsewhere (for example
	 * device deleted during suspend)
	 */
	mutex_lock(&sdev->state_mutex);
	if (sdev->sdev_state == SDEV_QUIESCE)
		scsi_device_set_state(sdev, SDEV_RUNNING);
	if (sdev->quiesced_by) {
		sdev->quiesced_by = NULL;
		blk_clear_pm_only(sdev->request_queue);
	}
	mutex_unlock(&sdev->state_mutex);
}
EXPORT_SYMBOL(scsi_device_resume);

static void
device_quiesce_fn(struct scsi_device *sdev, void *data)
{
	scsi_device_quiesce(sdev);
}

void
scsi_target_quiesce(struct scsi_target *starget)
{
	starget_for_each_device(starget, NULL, device_quiesce_fn);
}
EXPORT_SYMBOL(scsi_target_quiesce);

static void
device_resume_fn(struct scsi_device *sdev, void *data)
{
	scsi_device_resume(sdev);
}

void
scsi_target_resume(struct scsi_target *starget)
{
	starget_for_each_device(starget, NULL, device_resume_fn);
}
EXPORT_SYMBOL(scsi_target_resume);

static int __scsi_internal_device_block_nowait(struct scsi_device *sdev)
{
	if (scsi_device_set_state(sdev, SDEV_BLOCK))
		return scsi_device_set_state(sdev, SDEV_CREATED_BLOCK);

	return 0;
}

void scsi_start_queue(struct scsi_device *sdev)
{
	if (cmpxchg(&sdev->queue_stopped, 1, 0))
		blk_mq_unquiesce_queue(sdev->request_queue);
}

static void scsi_stop_queue(struct scsi_device *sdev, bool nowait)
{
	/*
	 * The atomic variable of ->queue_stopped covers that
	 * blk_mq_quiesce_queue* is balanced with blk_mq_unquiesce_queue.
	 *
	 * However, we still need to wait until quiesce is done
	 * in case that queue has been stopped.
	 */
	if (!cmpxchg(&sdev->queue_stopped, 0, 1)) {
		if (nowait)
			blk_mq_quiesce_queue_nowait(sdev->request_queue);
		else
			blk_mq_quiesce_queue(sdev->request_queue);
	} else {
		if (!nowait)
			blk_mq_wait_quiesce_done(sdev->request_queue);
	}
}

/**
 * scsi_internal_device_block_nowait - try to transition to the SDEV_BLOCK state
 * @sdev: device to block
 *
 * Pause SCSI command processing on the specified device. Does not sleep.
 *
 * Returns zero if successful or a negative error code upon failure.
 *
 * Notes:
 * This routine transitions the device to the SDEV_BLOCK state (which must be
 * a legal transition). When the device is in this state, command processing
 * is paused until the device leaves the SDEV_BLOCK state. See also
 * scsi_internal_device_unblock_nowait().
 */
int scsi_internal_device_block_nowait(struct scsi_device *sdev)
{
	int ret = __scsi_internal_device_block_nowait(sdev);

	/*
	 * The device has transitioned to SDEV_BLOCK.  Stop the
	 * block layer from calling the midlayer with this device's
	 * request queue.
	 */
	if (!ret)
		scsi_stop_queue(sdev, true);
	return ret;
}
EXPORT_SYMBOL_GPL(scsi_internal_device_block_nowait);

/**
 * scsi_internal_device_block - try to transition to the SDEV_BLOCK state
 * @sdev: device to block
 *
 * Pause SCSI command processing on the specified device and wait until all
 * ongoing scsi_request_fn() / scsi_queue_rq() calls have finished. May sleep.
 *
 * Returns zero if successful or a negative error code upon failure.
 *
 * Note:
 * This routine transitions the device to the SDEV_BLOCK state (which must be
 * a legal transition). When the device is in this state, command processing
 * is paused until the device leaves the SDEV_BLOCK state. See also
 * scsi_internal_device_unblock().
 */
static int scsi_internal_device_block(struct scsi_device *sdev)
{
	int err;

	mutex_lock(&sdev->state_mutex);
	err = __scsi_internal_device_block_nowait(sdev);
	if (err == 0)
		scsi_stop_queue(sdev, false);
	mutex_unlock(&sdev->state_mutex);

	return err;
}

/**
 * scsi_internal_device_unblock_nowait - resume a device after a block request
 * @sdev:	device to resume
 * @new_state:	state to set the device to after unblocking
 *
 * Restart the device queue for a previously suspended SCSI device. Does not
 * sleep.
 *
 * Returns zero if successful or a negative error code upon failure.
 *
 * Notes:
 * This routine transitions the device to the SDEV_RUNNING state or to one of
 * the offline states (which must be a legal transition) allowing the midlayer
 * to goose the queue for this device.
 */
int scsi_internal_device_unblock_nowait(struct scsi_device *sdev,
					enum scsi_device_state new_state)
{
	switch (new_state) {
	case SDEV_RUNNING:
	case SDEV_TRANSPORT_OFFLINE:
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Try to transition the scsi device to SDEV_RUNNING or one of the
	 * offlined states and goose the device queue if successful.
	 */
	switch (sdev->sdev_state) {
	case SDEV_BLOCK:
	case SDEV_TRANSPORT_OFFLINE:
		sdev->sdev_state = new_state;
		break;
	case SDEV_CREATED_BLOCK:
		if (new_state == SDEV_TRANSPORT_OFFLINE ||
		    new_state == SDEV_OFFLINE)
			sdev->sdev_state = new_state;
		else
			sdev->sdev_state = SDEV_CREATED;
		break;
	case SDEV_CANCEL:
	case SDEV_OFFLINE:
		break;
	default:
		return -EINVAL;
	}
	scsi_start_queue(sdev);

	return 0;
}
EXPORT_SYMBOL_GPL(scsi_internal_device_unblock_nowait);

/**
 * scsi_internal_device_unblock - resume a device after a block request
 * @sdev:	device to resume
 * @new_state:	state to set the device to after unblocking
 *
 * Restart the device queue for a previously suspended SCSI device. May sleep.
 *
 * Returns zero if successful or a negative error code upon failure.
 *
 * Notes:
 * This routine transitions the device to the SDEV_RUNNING state or to one of
 * the offline states (which must be a legal transition) allowing the midlayer
 * to goose the queue for this device.
 */
static int scsi_internal_device_unblock(struct scsi_device *sdev,
					enum scsi_device_state new_state)
{
	int ret;

	mutex_lock(&sdev->state_mutex);
	ret = scsi_internal_device_unblock_nowait(sdev, new_state);
	mutex_unlock(&sdev->state_mutex);

	return ret;
}

static void
device_block(struct scsi_device *sdev, void *data)
{
	int ret;

	ret = scsi_internal_device_block(sdev);

	WARN_ONCE(ret, "scsi_internal_device_block(%s) failed: ret = %d\n",
		  dev_name(&sdev->sdev_gendev), ret);
}

static int
target_block(struct device *dev, void *data)
{
	if (scsi_is_target_device(dev))
		starget_for_each_device(to_scsi_target(dev), NULL,
					device_block);
	return 0;
}

void
scsi_target_block(struct device *dev)
{
	if (scsi_is_target_device(dev))
		starget_for_each_device(to_scsi_target(dev), NULL,
					device_block);
	else
		device_for_each_child(dev, NULL, target_block);
}
EXPORT_SYMBOL_GPL(scsi_target_block);

static void
device_unblock(struct scsi_device *sdev, void *data)
{
	scsi_internal_device_unblock(sdev, *(enum scsi_device_state *)data);
}

static int
target_unblock(struct device *dev, void *data)
{
	if (scsi_is_target_device(dev))
		starget_for_each_device(to_scsi_target(dev), data,
					device_unblock);
	return 0;
}

void
scsi_target_unblock(struct device *dev, enum scsi_device_state new_state)
{
	if (scsi_is_target_device(dev))
		starget_for_each_device(to_scsi_target(dev), &new_state,
					device_unblock);
	else
		device_for_each_child(dev, &new_state, target_unblock);
}
EXPORT_SYMBOL_GPL(scsi_target_unblock);

int
scsi_host_block(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;
	int ret = 0;

	/*
	 * Call scsi_internal_device_block_nowait so we can avoid
	 * calling synchronize_rcu() for each LUN.
	 */
	shost_for_each_device(sdev, shost) {
		mutex_lock(&sdev->state_mutex);
		ret = scsi_internal_device_block_nowait(sdev);
		mutex_unlock(&sdev->state_mutex);
		if (ret) {
			scsi_device_put(sdev);
			break;
		}
	}

	/*
	 * SCSI never enables blk-mq's BLK_MQ_F_BLOCKING flag so
	 * calling synchronize_rcu() once is enough.
	 */
	WARN_ON_ONCE(shost->tag_set.flags & BLK_MQ_F_BLOCKING);

	if (!ret)
		synchronize_rcu();

	return ret;
}
EXPORT_SYMBOL_GPL(scsi_host_block);

int
scsi_host_unblock(struct Scsi_Host *shost, int new_state)
{
	struct scsi_device *sdev;
	int ret = 0;

	shost_for_each_device(sdev, shost) {
		ret = scsi_internal_device_unblock(sdev, new_state);
		if (ret) {
			scsi_device_put(sdev);
			break;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(scsi_host_unblock);

/**
 * scsi_kmap_atomic_sg - find and atomically map an sg-elemnt
 * @sgl:	scatter-gather list
 * @sg_count:	number of segments in sg
 * @offset:	offset in bytes into sg, on return offset into the mapped area
 * @len:	bytes to map, on return number of bytes mapped
 *
 * Returns virtual address of the start of the mapped page
 */
void *scsi_kmap_atomic_sg(struct scatterlist *sgl, int sg_count,
			  size_t *offset, size_t *len)
{
	int i;
	size_t sg_len = 0, len_complete = 0;
	struct scatterlist *sg;
	struct page *page;

	WARN_ON(!irqs_disabled());

	for_each_sg(sgl, sg, sg_count, i) {
		len_complete = sg_len; /* Complete sg-entries */
		sg_len += sg->length;
		if (sg_len > *offset)
			break;
	}

	if (unlikely(i == sg_count)) {
		printk(KERN_ERR "%s: Bytes in sg: %zu, requested offset %zu, "
			"elements %d\n",
		       __func__, sg_len, *offset, sg_count);
		WARN_ON(1);
		return NULL;
	}

	/* Offset starting from the beginning of first page in this sg-entry */
	*offset = *offset - len_complete + sg->offset;

	/* Assumption: contiguous pages can be accessed as "page + i" */
	page = nth_page(sg_page(sg), (*offset >> PAGE_SHIFT));
	*offset &= ~PAGE_MASK;

	/* Bytes in this sg-entry from *offset to the end of the page */
	sg_len = PAGE_SIZE - *offset;
	if (*len > sg_len)
		*len = sg_len;

	return kmap_atomic(page);
}
EXPORT_SYMBOL(scsi_kmap_atomic_sg);

/**
 * scsi_kunmap_atomic_sg - atomically unmap a virtual address, previously mapped with scsi_kmap_atomic_sg
 * @virt:	virtual address to be unmapped
 */
void scsi_kunmap_atomic_sg(void *virt)
{
	kunmap_atomic(virt);
}
EXPORT_SYMBOL(scsi_kunmap_atomic_sg);

void sdev_disable_disk_events(struct scsi_device *sdev)
{
	atomic_inc(&sdev->disk_events_disable_depth);
}
EXPORT_SYMBOL(sdev_disable_disk_events);

void sdev_enable_disk_events(struct scsi_device *sdev)
{
	if (WARN_ON_ONCE(atomic_read(&sdev->disk_events_disable_depth) <= 0))
		return;
	atomic_dec(&sdev->disk_events_disable_depth);
}
EXPORT_SYMBOL(sdev_enable_disk_events);

static unsigned char designator_prio(const unsigned char *d)
{
	if (d[1] & 0x30)
		/* not associated with LUN */
		return 0;

	if (d[3] == 0)
		/* invalid length */
		return 0;

	/*
	 * Order of preference for lun descriptor:
	 * - SCSI name string
	 * - NAA IEEE Registered Extended
	 * - EUI-64 based 16-byte
	 * - EUI-64 based 12-byte
	 * - NAA IEEE Registered
	 * - NAA IEEE Extended
	 * - EUI-64 based 8-byte
	 * - SCSI name string (truncated)
	 * - T10 Vendor ID
	 * as longer descriptors reduce the likelyhood
	 * of identification clashes.
	 */

	switch (d[1] & 0xf) {
	case 8:
		/* SCSI name string, variable-length UTF-8 */
		return 9;
	case 3:
		switch (d[4] >> 4) {
		case 6:
			/* NAA registered extended */
			return 8;
		case 5:
			/* NAA registered */
			return 5;
		case 4:
			/* NAA extended */
			return 4;
		case 3:
			/* NAA locally assigned */
			return 1;
		default:
			break;
		}
		break;
	case 2:
		switch (d[3]) {
		case 16:
			/* EUI64-based, 16 byte */
			return 7;
		case 12:
			/* EUI64-based, 12 byte */
			return 6;
		case 8:
			/* EUI64-based, 8 byte */
			return 3;
		default:
			break;
		}
		break;
	case 1:
		/* T10 vendor ID */
		return 1;
	default:
		break;
	}

	return 0;
}

/**
 * scsi_vpd_lun_id - return a unique device identification
 * @sdev: SCSI device
 * @id:   buffer for the identification
 * @id_len:  length of the buffer
 *
 * Copies a unique device identification into @id based
 * on the information in the VPD page 0x83 of the device.
 * The string will be formatted as a SCSI name string.
 *
 * Returns the length of the identification or error on failure.
 * If the identifier is longer than the supplied buffer the actual
 * identifier length is returned and the buffer is not zero-padded.
 */
int scsi_vpd_lun_id(struct scsi_device *sdev, char *id, size_t id_len)
{
	u8 cur_id_prio = 0;
	u8 cur_id_size = 0;
	const unsigned char *d, *cur_id_str;
	const struct scsi_vpd *vpd_pg83;
	int id_size = -EINVAL;

	rcu_read_lock();
	vpd_pg83 = rcu_dereference(sdev->vpd_pg83);
	if (!vpd_pg83) {
		rcu_read_unlock();
		return -ENXIO;
	}

	/* The id string must be at least 20 bytes + terminating NULL byte */
	if (id_len < 21) {
		rcu_read_unlock();
		return -EINVAL;
	}

	memset(id, 0, id_len);
	for (d = vpd_pg83->data + 4;
	     d < vpd_pg83->data + vpd_pg83->len;
	     d += d[3] + 4) {
		u8 prio = designator_prio(d);

		if (prio == 0 || cur_id_prio > prio)
			continue;

		switch (d[1] & 0xf) {
		case 0x1:
			/* T10 Vendor ID */
			if (cur_id_size > d[3])
				break;
			cur_id_prio = prio;
			cur_id_size = d[3];
			if (cur_id_size + 4 > id_len)
				cur_id_size = id_len - 4;
			cur_id_str = d + 4;
			id_size = snprintf(id, id_len, "t10.%*pE",
					   cur_id_size, cur_id_str);
			break;
		case 0x2:
			/* EUI-64 */
			cur_id_prio = prio;
			cur_id_size = d[3];
			cur_id_str = d + 4;
			switch (cur_id_size) {
			case 8:
				id_size = snprintf(id, id_len,
						   "eui.%8phN",
						   cur_id_str);
				break;
			case 12:
				id_size = snprintf(id, id_len,
						   "eui.%12phN",
						   cur_id_str);
				break;
			case 16:
				id_size = snprintf(id, id_len,
						   "eui.%16phN",
						   cur_id_str);
				break;
			default:
				break;
			}
			break;
		case 0x3:
			/* NAA */
			cur_id_prio = prio;
			cur_id_size = d[3];
			cur_id_str = d + 4;
			switch (cur_id_size) {
			case 8:
				id_size = snprintf(id, id_len,
						   "naa.%8phN",
						   cur_id_str);
				break;
			case 16:
				id_size = snprintf(id, id_len,
						   "naa.%16phN",
						   cur_id_str);
				break;
			default:
				break;
			}
			break;
		case 0x8:
			/* SCSI name string */
			if (cur_id_size > d[3])
				break;
			/* Prefer others for truncated descriptor */
			if (d[3] > id_len) {
				prio = 2;
				if (cur_id_prio > prio)
					break;
			}
			cur_id_prio = prio;
			cur_id_size = id_size = d[3];
			cur_id_str = d + 4;
			if (cur_id_size >= id_len)
				cur_id_size = id_len - 1;
			memcpy(id, cur_id_str, cur_id_size);
			break;
		default:
			break;
		}
	}
	rcu_read_unlock();

	return id_size;
}
EXPORT_SYMBOL(scsi_vpd_lun_id);

/*
 * scsi_vpd_tpg_id - return a target port group identifier
 * @sdev: SCSI device
 *
 * Returns the Target Port Group identifier from the information
 * froom VPD page 0x83 of the device.
 *
 * Returns the identifier or error on failure.
 */
int scsi_vpd_tpg_id(struct scsi_device *sdev, int *rel_id)
{
	const unsigned char *d;
	const struct scsi_vpd *vpd_pg83;
	int group_id = -EAGAIN, rel_port = -1;

	rcu_read_lock();
	vpd_pg83 = rcu_dereference(sdev->vpd_pg83);
	if (!vpd_pg83) {
		rcu_read_unlock();
		return -ENXIO;
	}

	d = vpd_pg83->data + 4;
	while (d < vpd_pg83->data + vpd_pg83->len) {
		switch (d[1] & 0xf) {
		case 0x4:
			/* Relative target port */
			rel_port = get_unaligned_be16(&d[6]);
			break;
		case 0x5:
			/* Target port group */
			group_id = get_unaligned_be16(&d[6]);
			break;
		default:
			break;
		}
		d += d[3] + 4;
	}
	rcu_read_unlock();

	if (group_id >= 0 && rel_id && rel_port != -1)
		*rel_id = rel_port;

	return group_id;
}
EXPORT_SYMBOL(scsi_vpd_tpg_id);

/**
 * scsi_build_sense - build sense data for a command
 * @scmd:	scsi command for which the sense should be formatted
 * @desc:	Sense format (non-zero == descriptor format,
 *              0 == fixed format)
 * @key:	Sense key
 * @asc:	Additional sense code
 * @ascq:	Additional sense code qualifier
 *
 **/
void scsi_build_sense(struct scsi_cmnd *scmd, int desc, u8 key, u8 asc, u8 ascq)
{
	scsi_build_sense_buffer(desc, scmd->sense_buffer, key, asc, ascq);
	scmd->result = SAM_STAT_CHECK_CONDITION;
}
EXPORT_SYMBOL_GPL(scsi_build_sense);
