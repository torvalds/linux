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

static struct kmem_cache *scsi_sdb_cache;
static struct kmem_cache *scsi_sense_cache;
static struct kmem_cache *scsi_sense_isadma_cache;
static DEFINE_MUTEX(scsi_sense_cache_mutex);

static void scsi_mq_uninit_cmd(struct scsi_cmnd *cmd);

static inline struct kmem_cache *
scsi_select_sense_cache(bool unchecked_isa_dma)
{
	return unchecked_isa_dma ? scsi_sense_isadma_cache : scsi_sense_cache;
}

static void scsi_free_sense_buffer(bool unchecked_isa_dma,
				   unsigned char *sense_buffer)
{
	kmem_cache_free(scsi_select_sense_cache(unchecked_isa_dma),
			sense_buffer);
}

static unsigned char *scsi_alloc_sense_buffer(bool unchecked_isa_dma,
	gfp_t gfp_mask, int numa_node)
{
	return kmem_cache_alloc_node(scsi_select_sense_cache(unchecked_isa_dma),
				     gfp_mask, numa_node);
}

int scsi_init_sense_cache(struct Scsi_Host *shost)
{
	struct kmem_cache *cache;
	int ret = 0;

	cache = scsi_select_sense_cache(shost->unchecked_isa_dma);
	if (cache)
		return 0;

	mutex_lock(&scsi_sense_cache_mutex);
	if (shost->unchecked_isa_dma) {
		scsi_sense_isadma_cache =
			kmem_cache_create("scsi_sense_cache(DMA)",
			SCSI_SENSE_BUFFERSIZE, 0,
			SLAB_HWCACHE_ALIGN | SLAB_CACHE_DMA, NULL);
		if (!scsi_sense_isadma_cache)
			ret = -ENOMEM;
	} else {
		scsi_sense_cache =
			kmem_cache_create("scsi_sense_cache",
			SCSI_SENSE_BUFFERSIZE, 0, SLAB_HWCACHE_ALIGN, NULL);
		if (!scsi_sense_cache)
			ret = -ENOMEM;
	}

	mutex_unlock(&scsi_sense_cache_mutex);
	return ret;
}

/*
 * When to reinvoke queueing after a resource shortage. It's 3 msecs to
 * not change behaviour from the previous unplug mechanism, experimentation
 * may prove this needs changing.
 */
#define SCSI_QUEUE_DELAY	3

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

static void scsi_mq_requeue_cmd(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;

	if (cmd->request->rq_flags & RQF_DONTPREP) {
		cmd->request->rq_flags &= ~RQF_DONTPREP;
		scsi_mq_uninit_cmd(cmd);
	} else {
		WARN_ON_ONCE(true);
	}
	blk_mq_requeue_request(cmd->request, true);
	put_device(&sdev->sdev_gendev);
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
static void __scsi_queue_insert(struct scsi_cmnd *cmd, int reason, int unbusy)
{
	struct scsi_device *device = cmd->device;
	struct request_queue *q = device->request_queue;
	unsigned long flags;

	SCSI_LOG_MLQUEUE(1, scmd_printk(KERN_INFO, cmd,
		"Inserting command %p into mlqueue\n", cmd));

	scsi_set_blocked(cmd, reason);

	/*
	 * Decrement the counters, since these commands are no longer
	 * active on the host/device.
	 */
	if (unbusy)
		scsi_device_unbusy(device);

	/*
	 * Requeue this command.  It will go before all other commands
	 * that are already in the queue. Schedule requeue work under
	 * lock such that the kblockd_schedule_work() call happens
	 * before blk_cleanup_queue() finishes.
	 */
	cmd->result = 0;
	if (q->mq_ops) {
		scsi_mq_requeue_cmd(cmd);
		return;
	}
	spin_lock_irqsave(q->queue_lock, flags);
	blk_requeue_request(q, cmd->request);
	kblockd_schedule_work(&device->requeue_work);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/*
 * Function:    scsi_queue_insert()
 *
 * Purpose:     Insert a command in the midlevel queue.
 *
 * Arguments:   cmd    - command that we are adding to queue.
 *              reason - why we are inserting command to queue.
 *
 * Lock status: Assumed that lock is not held upon entry.
 *
 * Returns:     Nothing.
 *
 * Notes:       We do this for one of two cases.  Either the host is busy
 *              and it cannot accept any more commands for the time being,
 *              or the device returned QUEUE_FULL and can accept no more
 *              commands.
 * Notes:       This could be called either from an interrupt context or a
 *              normal process context.
 */
void scsi_queue_insert(struct scsi_cmnd *cmd, int reason)
{
	__scsi_queue_insert(cmd, reason, 1);
}


/**
 * scsi_execute - insert request and wait for the result
 * @sdev:	scsi device
 * @cmd:	scsi command
 * @data_direction: data direction
 * @buffer:	data buffer
 * @bufflen:	len of buffer
 * @sense:	optional sense buffer
 * @sshdr:	optional decoded sense header
 * @timeout:	request timeout in seconds
 * @retries:	number of times to retry request
 * @flags:	flags for ->cmd_flags
 * @rq_flags:	flags for ->rq_flags
 * @resid:	optional residual length
 *
 * Returns the scsi_cmnd result field if a command was executed, or a negative
 * Linux error code if we didn't get that far.
 */
int scsi_execute(struct scsi_device *sdev, const unsigned char *cmd,
		 int data_direction, void *buffer, unsigned bufflen,
		 unsigned char *sense, struct scsi_sense_hdr *sshdr,
		 int timeout, int retries, u64 flags, req_flags_t rq_flags,
		 int *resid)
{
	struct request *req;
	struct scsi_request *rq;
	int ret = DRIVER_ERROR << 24;

	req = blk_get_request(sdev->request_queue,
			data_direction == DMA_TO_DEVICE ?
			REQ_OP_SCSI_OUT : REQ_OP_SCSI_IN, __GFP_RECLAIM);
	if (IS_ERR(req))
		return ret;
	rq = scsi_req(req);

	if (bufflen &&	blk_rq_map_kern(sdev->request_queue, req,
					buffer, bufflen, __GFP_RECLAIM))
		goto out;

	rq->cmd_len = COMMAND_SIZE(cmd[0]);
	memcpy(rq->cmd, cmd, rq->cmd_len);
	rq->retries = retries;
	req->timeout = timeout;
	req->cmd_flags |= flags;
	req->rq_flags |= rq_flags | RQF_QUIET | RQF_PREEMPT;

	/*
	 * head injection *required* here otherwise quiesce won't work
	 */
	blk_execute_rq(req->q, NULL, req, 1);

	/*
	 * Some devices (USB mass-storage in particular) may transfer
	 * garbage data together with a residue indicating that the data
	 * is invalid.  Prevent the garbage from being misinterpreted
	 * and prevent security leaks by zeroing out the excess data.
	 */
	if (unlikely(rq->resid_len > 0 && rq->resid_len <= bufflen))
		memset(buffer + (bufflen - rq->resid_len), 0, rq->resid_len);

	if (resid)
		*resid = rq->resid_len;
	if (sense && rq->sense_len)
		memcpy(sense, rq->sense, SCSI_SENSE_BUFFERSIZE);
	if (sshdr)
		scsi_normalize_sense(rq->sense, rq->sense_len, sshdr);
	ret = rq->result;
 out:
	blk_put_request(req);

	return ret;
}
EXPORT_SYMBOL(scsi_execute);

/*
 * Function:    scsi_init_cmd_errh()
 *
 * Purpose:     Initialize cmd fields related to error handling.
 *
 * Arguments:   cmd	- command that is ready to be queued.
 *
 * Notes:       This function has the job of initializing a number of
 *              fields related to error handling.   Typically this will
 *              be called once for each command, as required.
 */
static void scsi_init_cmd_errh(struct scsi_cmnd *cmd)
{
	cmd->serial_number = 0;
	scsi_set_resid(cmd, 0);
	memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
	if (cmd->cmd_len == 0)
		cmd->cmd_len = scsi_command_size(cmd->cmnd);
}

/*
 * Decrement the host_busy counter and wake up the error handler if necessary.
 * Avoid as follows that the error handler is not woken up if shost->host_busy
 * == shost->host_failed: use call_rcu() in scsi_eh_scmd_add() in combination
 * with an RCU read lock in this function to ensure that this function in its
 * entirety either finishes before scsi_eh_scmd_add() increases the
 * host_failed counter or that it notices the shost state change made by
 * scsi_eh_scmd_add().
 */
static void scsi_dec_host_busy(struct Scsi_Host *shost)
{
	unsigned long flags;

	rcu_read_lock();
	atomic_dec(&shost->host_busy);
	if (unlikely(scsi_host_in_recovery(shost))) {
		spin_lock_irqsave(shost->host_lock, flags);
		if (shost->host_failed || shost->host_eh_scheduled)
			scsi_eh_wakeup(shost);
		spin_unlock_irqrestore(shost->host_lock, flags);
	}
	rcu_read_unlock();
}

void scsi_device_unbusy(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	struct scsi_target *starget = scsi_target(sdev);

	scsi_dec_host_busy(shost);

	if (starget->can_queue > 0)
		atomic_dec(&starget->target_busy);

	atomic_dec(&sdev->device_busy);
}

static void scsi_kick_queue(struct request_queue *q)
{
	if (q->mq_ops)
		blk_mq_start_hw_queues(q);
	else
		blk_run_queue(q);
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
	if (atomic_read(&sdev->device_busy) >= sdev->queue_depth)
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
	if (shost->can_queue > 0 &&
	    atomic_read(&shost->host_busy) >= shost->can_queue)
		return true;
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
		 * blk_cleanup_queue() before the queue is run from this
		 * function then blk_run_queue() will return immediately since
		 * blk_cleanup_queue() marks the queue with QUEUE_FLAG_DYING.
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

/*
 * Function:   scsi_run_queue()
 *
 * Purpose:    Select a proper request queue to serve next
 *
 * Arguments:  q       - last request's queue
 *
 * Returns:     Nothing
 *
 * Notes:      The previous command was completely finished, start
 *             a new one if possible.
 */
static void scsi_run_queue(struct request_queue *q)
{
	struct scsi_device *sdev = q->queuedata;

	if (scsi_target(sdev)->single_lun)
		scsi_single_lun_run(sdev);
	if (!list_empty(&sdev->host->starved_list))
		scsi_starved_list_run(sdev->host);

	if (q->mq_ops)
		blk_mq_run_hw_queues(q, false);
	else
		blk_run_queue(q);
}

void scsi_requeue_run_queue(struct work_struct *work)
{
	struct scsi_device *sdev;
	struct request_queue *q;

	sdev = container_of(work, struct scsi_device, requeue_work);
	q = sdev->request_queue;
	scsi_run_queue(q);
}

/*
 * Function:	scsi_requeue_command()
 *
 * Purpose:	Handle post-processing of completed commands.
 *
 * Arguments:	q	- queue to operate on
 *		cmd	- command that may need to be requeued.
 *
 * Returns:	Nothing
 *
 * Notes:	After command completion, there may be blocks left
 *		over which weren't finished by the previous command
 *		this can be for a number of reasons - the main one is
 *		I/O errors in the middle of the request, in which case
 *		we need to request the blocks that come after the bad
 *		sector.
 * Notes:	Upon return, cmd is a stale pointer.
 */
static void scsi_requeue_command(struct request_queue *q, struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct request *req = cmd->request;
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	blk_unprep_request(req);
	req->special = NULL;
	scsi_put_command(cmd);
	blk_requeue_request(q, req);
	spin_unlock_irqrestore(q->queue_lock, flags);

	scsi_run_queue(q);

	put_device(&sdev->sdev_gendev);
}

void scsi_run_host_queues(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;

	shost_for_each_device(sdev, shost)
		scsi_run_queue(sdev->request_queue);
}

static void scsi_uninit_cmd(struct scsi_cmnd *cmd)
{
	if (!blk_rq_is_passthrough(cmd->request)) {
		struct scsi_driver *drv = scsi_cmd_to_driver(cmd);

		if (drv->uninit_command)
			drv->uninit_command(cmd);
	}
}

static void scsi_mq_free_sgtables(struct scsi_cmnd *cmd)
{
	struct scsi_data_buffer *sdb;

	if (cmd->sdb.table.nents)
		sg_free_table_chained(&cmd->sdb.table, true);
	if (cmd->request->next_rq) {
		sdb = cmd->request->next_rq->special;
		if (sdb)
			sg_free_table_chained(&sdb->table, true);
	}
	if (scsi_prot_sg_count(cmd))
		sg_free_table_chained(&cmd->prot_sdb->table, true);
}

static void scsi_mq_uninit_cmd(struct scsi_cmnd *cmd)
{
	scsi_mq_free_sgtables(cmd);
	scsi_uninit_cmd(cmd);
	scsi_del_cmd_from_list(cmd);
}

/*
 * Function:    scsi_release_buffers()
 *
 * Purpose:     Free resources allocate for a scsi_command.
 *
 * Arguments:   cmd	- command that we are bailing.
 *
 * Lock status: Assumed that no lock is held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       In the event that an upper level driver rejects a
 *		command, we must release resources allocated during
 *		the __init_io() function.  Primarily this would involve
 *		the scatter-gather table.
 */
static void scsi_release_buffers(struct scsi_cmnd *cmd)
{
	if (cmd->sdb.table.nents)
		sg_free_table_chained(&cmd->sdb.table, false);

	memset(&cmd->sdb, 0, sizeof(cmd->sdb));

	if (scsi_prot_sg_count(cmd))
		sg_free_table_chained(&cmd->prot_sdb->table, false);
}

static void scsi_release_bidi_buffers(struct scsi_cmnd *cmd)
{
	struct scsi_data_buffer *bidi_sdb = cmd->request->next_rq->special;

	sg_free_table_chained(&bidi_sdb->table, false);
	kmem_cache_free(scsi_sdb_cache, bidi_sdb);
	cmd->request->next_rq->special = NULL;
}

static bool scsi_end_request(struct request *req, blk_status_t error,
		unsigned int bytes, unsigned int bidi_bytes)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);
	struct scsi_device *sdev = cmd->device;
	struct request_queue *q = sdev->request_queue;

	if (blk_update_request(req, error, bytes))
		return true;

	/* Bidi request must be completed as a whole */
	if (unlikely(bidi_bytes) &&
	    blk_update_request(req->next_rq, error, bidi_bytes))
		return true;

	if (blk_queue_add_random(q))
		add_disk_randomness(req->rq_disk);

	if (!blk_rq_is_scsi(req)) {
		WARN_ON_ONCE(!(cmd->flags & SCMD_INITIALIZED));
		cmd->flags &= ~SCMD_INITIALIZED;
		destroy_rcu_head(&cmd->rcu);
	}

	if (req->mq_ctx) {
		/*
		 * In the MQ case the command gets freed by __blk_mq_end_request,
		 * so we have to do all cleanup that depends on it earlier.
		 *
		 * We also can't kick the queues from irq context, so we
		 * will have to defer it to a workqueue.
		 */
		scsi_mq_uninit_cmd(cmd);

		__blk_mq_end_request(req, error);

		if (scsi_target(sdev)->single_lun ||
		    !list_empty(&sdev->host->starved_list))
			kblockd_schedule_work(&sdev->requeue_work);
		else
			blk_mq_run_hw_queues(q, true);
	} else {
		unsigned long flags;

		if (bidi_bytes)
			scsi_release_bidi_buffers(cmd);
		scsi_release_buffers(cmd);
		scsi_put_command(cmd);

		spin_lock_irqsave(q->queue_lock, flags);
		blk_finish_request(req, error);
		spin_unlock_irqrestore(q->queue_lock, flags);

		scsi_run_queue(q);
	}

	put_device(&sdev->sdev_gendev);
	return false;
}

/**
 * __scsi_error_from_host_byte - translate SCSI error code into errno
 * @cmd:	SCSI command (unused)
 * @result:	scsi error code
 *
 * Translate SCSI error code into block errors.
 */
static blk_status_t __scsi_error_from_host_byte(struct scsi_cmnd *cmd,
		int result)
{
	switch (host_byte(result)) {
	case DID_TRANSPORT_FAILFAST:
		return BLK_STS_TRANSPORT;
	case DID_TARGET_FAILURE:
		set_host_byte(cmd, DID_OK);
		return BLK_STS_TARGET;
	case DID_NEXUS_FAILURE:
		return BLK_STS_NEXUS;
	case DID_ALLOC_FAILURE:
		set_host_byte(cmd, DID_OK);
		return BLK_STS_NOSPC;
	case DID_MEDIUM_ERROR:
		set_host_byte(cmd, DID_OK);
		return BLK_STS_MEDIUM;
	default:
		return BLK_STS_IOERR;
	}
}

/*
 * Function:    scsi_io_completion()
 *
 * Purpose:     Completion processing for block device I/O requests.
 *
 * Arguments:   cmd   - command that is finished.
 *
 * Lock status: Assumed that no lock is held upon entry.
 *
 * Returns:     Nothing
 *
 * Notes:       We will finish off the specified number of sectors.  If we
 *		are done, the command block will be released and the queue
 *		function will be goosed.  If we are not done then we have to
 *		figure out what to do next:
 *
 *		a) We can call scsi_requeue_command().  The request
 *		   will be unprepared and put back on the queue.  Then
 *		   a new command will be created for it.  This should
 *		   be used if we made forward progress, or if we want
 *		   to switch from READ(10) to READ(6) for example.
 *
 *		b) We can call __scsi_queue_insert().  The request will
 *		   be put back on the queue and retried using the same
 *		   command as before, possibly after a delay.
 *
 *		c) We can call scsi_end_request() with -EIO to fail
 *		   the remainder of the request.
 */
void scsi_io_completion(struct scsi_cmnd *cmd, unsigned int good_bytes)
{
	int result = cmd->result;
	struct request_queue *q = cmd->device->request_queue;
	struct request *req = cmd->request;
	blk_status_t error = BLK_STS_OK;
	struct scsi_sense_hdr sshdr;
	bool sense_valid = false;
	int sense_deferred = 0, level = 0;
	enum {ACTION_FAIL, ACTION_REPREP, ACTION_RETRY,
	      ACTION_DELAYED_RETRY} action;
	unsigned long wait_for = (cmd->allowed + 1) * req->timeout;

	if (result) {
		sense_valid = scsi_command_normalize_sense(cmd, &sshdr);
		if (sense_valid)
			sense_deferred = scsi_sense_is_deferred(&sshdr);
	}

	if (blk_rq_is_passthrough(req)) {
		if (result) {
			if (sense_valid) {
				/*
				 * SG_IO wants current and deferred errors
				 */
				scsi_req(req)->sense_len =
					min(8 + cmd->sense_buffer[7],
					    SCSI_SENSE_BUFFERSIZE);
			}
			if (!sense_deferred)
				error = __scsi_error_from_host_byte(cmd, result);
		}
		/*
		 * __scsi_error_from_host_byte may have reset the host_byte
		 */
		scsi_req(req)->result = cmd->result;
		scsi_req(req)->resid_len = scsi_get_resid(cmd);

		if (scsi_bidi_cmnd(cmd)) {
			/*
			 * Bidi commands Must be complete as a whole,
			 * both sides at once.
			 */
			scsi_req(req->next_rq)->resid_len = scsi_in(cmd)->resid;
			if (scsi_end_request(req, BLK_STS_OK, blk_rq_bytes(req),
					blk_rq_bytes(req->next_rq)))
				BUG();
			return;
		}
	} else if (blk_rq_bytes(req) == 0 && result && !sense_deferred) {
		/*
		 * Flush commands do not transfers any data, and thus cannot use
		 * good_bytes != blk_rq_bytes(req) as the signal for an error.
		 * This sets the error explicitly for the problem case.
		 */
		error = __scsi_error_from_host_byte(cmd, result);
	}

	/* no bidi support for !blk_rq_is_passthrough yet */
	BUG_ON(blk_bidi_rq(req));

	/*
	 * Next deal with any sectors which we were able to correctly
	 * handle.
	 */
	SCSI_LOG_HLCOMPLETE(1, scmd_printk(KERN_INFO, cmd,
		"%u sectors total, %d bytes done.\n",
		blk_rq_sectors(req), good_bytes));

	/*
	 * Recovered errors need reporting, but they're always treated as
	 * success, so fiddle the result code here.  For passthrough requests
	 * we already took a copy of the original into sreq->result which
	 * is what gets returned to the user
	 */
	if (sense_valid && (sshdr.sense_key == RECOVERED_ERROR)) {
		/* if ATA PASS-THROUGH INFORMATION AVAILABLE skip
		 * print since caller wants ATA registers. Only occurs on
		 * SCSI ATA PASS_THROUGH commands when CK_COND=1
		 */
		if ((sshdr.asc == 0x0) && (sshdr.ascq == 0x1d))
			;
		else if (!(req->rq_flags & RQF_QUIET))
			scsi_print_sense(cmd);
		result = 0;
		/* for passthrough error may be set */
		error = BLK_STS_OK;
	}

	/*
	 * special case: failed zero length commands always need to
	 * drop down into the retry code. Otherwise, if we finished
	 * all bytes in the request we are done now.
	 */
	if (!(blk_rq_bytes(req) == 0 && error) &&
	    !scsi_end_request(req, error, good_bytes, 0))
		return;

	/*
	 * Kill remainder if no retrys.
	 */
	if (error && scsi_noretry_cmd(cmd)) {
		if (scsi_end_request(req, error, blk_rq_bytes(req), 0))
			BUG();
		return;
	}

	/*
	 * If there had been no error, but we have leftover bytes in the
	 * requeues just queue the command up again.
	 */
	if (result == 0)
		goto requeue;

	error = __scsi_error_from_host_byte(cmd, result);

	if (host_byte(result) == DID_RESET) {
		/* Third party bus reset or reset for error recovery
		 * reasons.  Just retry the command and see what
		 * happens.
		 */
		action = ACTION_RETRY;
	} else if (sense_valid && !sense_deferred) {
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
				error = BLK_STS_PROTECTION;
			/* INVALID COMMAND OPCODE or INVALID FIELD IN CDB */
			} else if (sshdr.asc == 0x20 || sshdr.asc == 0x24) {
				action = ACTION_FAIL;
				error = BLK_STS_TARGET;
			} else
				action = ACTION_FAIL;
			break;
		case ABORTED_COMMAND:
			action = ACTION_FAIL;
			if (sshdr.asc == 0x10) /* DIF */
				error = BLK_STS_PROTECTION;
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
				case 0x14: /* space allocation in progress */
					action = ACTION_DELAYED_RETRY;
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
		default:
			action = ACTION_FAIL;
			break;
		}
	} else
		action = ACTION_FAIL;

	if (action != ACTION_FAIL &&
	    time_before(cmd->jiffies_at_alloc + wait_for, jiffies))
		action = ACTION_FAIL;

	switch (action) {
	case ACTION_FAIL:
		/* Give up and fail the remainder of the request */
		if (!(req->rq_flags & RQF_QUIET)) {
			static DEFINE_RATELIMIT_STATE(_rs,
					DEFAULT_RATELIMIT_INTERVAL,
					DEFAULT_RATELIMIT_BURST);

			if (unlikely(scsi_logging_level))
				level = SCSI_LOG_LEVEL(SCSI_LOG_MLCOMPLETE_SHIFT,
						       SCSI_LOG_MLCOMPLETE_BITS);

			/*
			 * if logging is enabled the failure will be printed
			 * in scsi_log_completion(), so avoid duplicate messages
			 */
			if (!level && __ratelimit(&_rs)) {
				scsi_print_result(cmd, NULL, FAILED);
				if (driver_byte(result) & DRIVER_SENSE)
					scsi_print_sense(cmd);
				scsi_print_command(cmd);
			}
		}
		if (!scsi_end_request(req, error, blk_rq_err_bytes(req), 0))
			return;
		/*FALLTHRU*/
	case ACTION_REPREP:
	requeue:
		/* Unprep the request and put it back at the head of the queue.
		 * A new command will be prepared and issued.
		 */
		if (q->mq_ops) {
			scsi_mq_requeue_cmd(cmd);
		} else {
			scsi_release_buffers(cmd);
			scsi_requeue_command(q, cmd);
		}
		break;
	case ACTION_RETRY:
		/* Retry the same command immediately */
		__scsi_queue_insert(cmd, SCSI_MLQUEUE_EH_RETRY, 0);
		break;
	case ACTION_DELAYED_RETRY:
		/* Retry the same command after a delay */
		__scsi_queue_insert(cmd, SCSI_MLQUEUE_DEVICE_BUSY, 0);
		break;
	}
}

static int scsi_init_sgtable(struct request *req, struct scsi_data_buffer *sdb)
{
	int count;

	/*
	 * If sg table allocation fails, requeue request later.
	 */
	if (unlikely(sg_alloc_table_chained(&sdb->table,
			blk_rq_nr_phys_segments(req), sdb->table.sgl)))
		return BLKPREP_DEFER;

	/* 
	 * Next, walk the list, and fill in the addresses and sizes of
	 * each segment.
	 */
	count = blk_rq_map_sg(req->q, req, sdb->table.sgl);
	BUG_ON(count > sdb->table.nents);
	sdb->table.nents = count;
	sdb->length = blk_rq_payload_bytes(req);
	return BLKPREP_OK;
}

/*
 * Function:    scsi_init_io()
 *
 * Purpose:     SCSI I/O initialize function.
 *
 * Arguments:   cmd   - Command descriptor we wish to initialize
 *
 * Returns:     0 on success
 *		BLKPREP_DEFER if the failure is retryable
 *		BLKPREP_KILL if the failure is fatal
 */
int scsi_init_io(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct request *rq = cmd->request;
	bool is_mq = (rq->mq_ctx != NULL);
	int error = BLKPREP_KILL;

	if (WARN_ON_ONCE(!blk_rq_nr_phys_segments(rq)))
		goto err_exit;

	error = scsi_init_sgtable(rq, &cmd->sdb);
	if (error)
		goto err_exit;

	if (blk_bidi_rq(rq)) {
		if (!rq->q->mq_ops) {
			struct scsi_data_buffer *bidi_sdb =
				kmem_cache_zalloc(scsi_sdb_cache, GFP_ATOMIC);
			if (!bidi_sdb) {
				error = BLKPREP_DEFER;
				goto err_exit;
			}

			rq->next_rq->special = bidi_sdb;
		}

		error = scsi_init_sgtable(rq->next_rq, rq->next_rq->special);
		if (error)
			goto err_exit;
	}

	if (blk_integrity_rq(rq)) {
		struct scsi_data_buffer *prot_sdb = cmd->prot_sdb;
		int ivecs, count;

		if (prot_sdb == NULL) {
			/*
			 * This can happen if someone (e.g. multipath)
			 * queues a command to a device on an adapter
			 * that does not support DIX.
			 */
			WARN_ON_ONCE(1);
			error = BLKPREP_KILL;
			goto err_exit;
		}

		ivecs = blk_rq_count_integrity_sg(rq->q, rq->bio);

		if (sg_alloc_table_chained(&prot_sdb->table, ivecs,
				prot_sdb->table.sgl)) {
			error = BLKPREP_DEFER;
			goto err_exit;
		}

		count = blk_rq_map_integrity_sg(rq->q, rq->bio,
						prot_sdb->table.sgl);
		BUG_ON(unlikely(count > ivecs));
		BUG_ON(unlikely(count > queue_max_integrity_segments(rq->q)));

		cmd->prot_sdb = prot_sdb;
		cmd->prot_sdb->table.nents = count;
	}

	return BLKPREP_OK;
err_exit:
	if (is_mq) {
		scsi_mq_free_sgtables(cmd);
	} else {
		scsi_release_buffers(cmd);
		cmd->request->special = NULL;
		scsi_put_command(cmd);
		put_device(&sdev->sdev_gendev);
	}
	return error;
}
EXPORT_SYMBOL(scsi_init_io);

/**
 * scsi_initialize_rq - initialize struct scsi_cmnd partially
 * @rq: Request associated with the SCSI command to be initialized.
 *
 * This function initializes the members of struct scsi_cmnd that must be
 * initialized before request processing starts and that won't be
 * reinitialized if a SCSI command is requeued.
 *
 * Called from inside blk_get_request() for pass-through requests and from
 * inside scsi_init_command() for filesystem requests.
 */
void scsi_initialize_rq(struct request *rq)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);

	scsi_req_init(&cmd->req);
	init_rcu_head(&cmd->rcu);
	cmd->jiffies_at_alloc = jiffies;
	cmd->retries = 0;
}
EXPORT_SYMBOL(scsi_initialize_rq);

/* Add a command to the list used by the aacraid and dpt_i2o drivers */
void scsi_add_cmd_to_list(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct Scsi_Host *shost = sdev->host;
	unsigned long flags;

	if (shost->use_cmd_list) {
		spin_lock_irqsave(&sdev->list_lock, flags);
		list_add_tail(&cmd->list, &sdev->cmd_list);
		spin_unlock_irqrestore(&sdev->list_lock, flags);
	}
}

/* Remove a command from the list used by the aacraid and dpt_i2o drivers */
void scsi_del_cmd_from_list(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct Scsi_Host *shost = sdev->host;
	unsigned long flags;

	if (shost->use_cmd_list) {
		spin_lock_irqsave(&sdev->list_lock, flags);
		BUG_ON(list_empty(&cmd->list));
		list_del_init(&cmd->list);
		spin_unlock_irqrestore(&sdev->list_lock, flags);
	}
}

/* Called after a request has been started. */
void scsi_init_command(struct scsi_device *dev, struct scsi_cmnd *cmd)
{
	void *buf = cmd->sense_buffer;
	void *prot = cmd->prot_sdb;
	struct request *rq = blk_mq_rq_from_pdu(cmd);
	unsigned int flags = cmd->flags & SCMD_PRESERVED_FLAGS;
	unsigned long jiffies_at_alloc;
	int retries;

	if (!blk_rq_is_scsi(rq) && !(flags & SCMD_INITIALIZED)) {
		flags |= SCMD_INITIALIZED;
		scsi_initialize_rq(rq);
	}

	jiffies_at_alloc = cmd->jiffies_at_alloc;
	retries = cmd->retries;
	/* zero out the cmd, except for the embedded scsi_request */
	memset((char *)cmd + sizeof(cmd->req), 0,
		sizeof(*cmd) - sizeof(cmd->req) + dev->host->hostt->cmd_size);

	cmd->device = dev;
	cmd->sense_buffer = buf;
	cmd->prot_sdb = prot;
	cmd->flags = flags;
	INIT_DELAYED_WORK(&cmd->abort_work, scmd_eh_abort_handler);
	cmd->jiffies_at_alloc = jiffies_at_alloc;
	cmd->retries = retries;

	scsi_add_cmd_to_list(cmd);
}

static int scsi_setup_scsi_cmnd(struct scsi_device *sdev, struct request *req)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);

	/*
	 * Passthrough requests may transfer data, in which case they must
	 * a bio attached to them.  Or they might contain a SCSI command
	 * that does not transfer data, in which case they may optionally
	 * submit a request without an attached bio.
	 */
	if (req->bio) {
		int ret = scsi_init_io(cmd);
		if (unlikely(ret))
			return ret;
	} else {
		BUG_ON(blk_rq_bytes(req));

		memset(&cmd->sdb, 0, sizeof(cmd->sdb));
	}

	cmd->cmd_len = scsi_req(req)->cmd_len;
	cmd->cmnd = scsi_req(req)->cmd;
	cmd->transfersize = blk_rq_bytes(req);
	cmd->allowed = scsi_req(req)->retries;
	return BLKPREP_OK;
}

/*
 * Setup a normal block command.  These are simple request from filesystems
 * that still need to be translated to SCSI CDBs from the ULD.
 */
static int scsi_setup_fs_cmnd(struct scsi_device *sdev, struct request *req)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);

	if (unlikely(sdev->handler && sdev->handler->prep_fn)) {
		int ret = sdev->handler->prep_fn(sdev, req);
		if (ret != BLKPREP_OK)
			return ret;
	}

	cmd->cmnd = scsi_req(req)->cmd = scsi_req(req)->__cmd;
	memset(cmd->cmnd, 0, BLK_MAX_CDB);
	return scsi_cmd_to_driver(cmd)->init_command(cmd);
}

static int scsi_setup_cmnd(struct scsi_device *sdev, struct request *req)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);

	if (!blk_rq_bytes(req))
		cmd->sc_data_direction = DMA_NONE;
	else if (rq_data_dir(req) == WRITE)
		cmd->sc_data_direction = DMA_TO_DEVICE;
	else
		cmd->sc_data_direction = DMA_FROM_DEVICE;

	if (blk_rq_is_scsi(req))
		return scsi_setup_scsi_cmnd(sdev, req);
	else
		return scsi_setup_fs_cmnd(sdev, req);
}

static int
scsi_prep_state_check(struct scsi_device *sdev, struct request *req)
{
	int ret = BLKPREP_OK;

	/*
	 * If the device is not in running state we will reject some
	 * or all commands.
	 */
	if (unlikely(sdev->sdev_state != SDEV_RUNNING)) {
		switch (sdev->sdev_state) {
		case SDEV_OFFLINE:
		case SDEV_TRANSPORT_OFFLINE:
			/*
			 * If the device is offline we refuse to process any
			 * commands.  The device must be brought online
			 * before trying any recovery commands.
			 */
			sdev_printk(KERN_ERR, sdev,
				    "rejecting I/O to offline device\n");
			ret = BLKPREP_KILL;
			break;
		case SDEV_DEL:
			/*
			 * If the device is fully deleted, we refuse to
			 * process any commands as well.
			 */
			sdev_printk(KERN_ERR, sdev,
				    "rejecting I/O to dead device\n");
			ret = BLKPREP_KILL;
			break;
		case SDEV_BLOCK:
		case SDEV_CREATED_BLOCK:
			ret = BLKPREP_DEFER;
			break;
		case SDEV_QUIESCE:
			/*
			 * If the devices is blocked we defer normal commands.
			 */
			if (!(req->rq_flags & RQF_PREEMPT))
				ret = BLKPREP_DEFER;
			break;
		default:
			/*
			 * For any other not fully online state we only allow
			 * special commands.  In particular any user initiated
			 * command is not allowed.
			 */
			if (!(req->rq_flags & RQF_PREEMPT))
				ret = BLKPREP_KILL;
			break;
		}
	}
	return ret;
}

static int
scsi_prep_return(struct request_queue *q, struct request *req, int ret)
{
	struct scsi_device *sdev = q->queuedata;

	switch (ret) {
	case BLKPREP_KILL:
	case BLKPREP_INVALID:
		scsi_req(req)->result = DID_NO_CONNECT << 16;
		/* release the command and kill it */
		if (req->special) {
			struct scsi_cmnd *cmd = req->special;
			scsi_release_buffers(cmd);
			scsi_put_command(cmd);
			put_device(&sdev->sdev_gendev);
			req->special = NULL;
		}
		break;
	case BLKPREP_DEFER:
		/*
		 * If we defer, the blk_peek_request() returns NULL, but the
		 * queue must be restarted, so we schedule a callback to happen
		 * shortly.
		 */
		if (atomic_read(&sdev->device_busy) == 0)
			blk_delay_queue(q, SCSI_QUEUE_DELAY);
		break;
	default:
		req->rq_flags |= RQF_DONTPREP;
	}

	return ret;
}

static int scsi_prep_fn(struct request_queue *q, struct request *req)
{
	struct scsi_device *sdev = q->queuedata;
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);
	int ret;

	ret = scsi_prep_state_check(sdev, req);
	if (ret != BLKPREP_OK)
		goto out;

	if (!req->special) {
		/* Bail if we can't get a reference to the device */
		if (unlikely(!get_device(&sdev->sdev_gendev))) {
			ret = BLKPREP_DEFER;
			goto out;
		}

		scsi_init_command(sdev, cmd);
		req->special = cmd;
	}

	cmd->tag = req->tag;
	cmd->request = req;
	cmd->prot_op = SCSI_PROT_NORMAL;

	ret = scsi_setup_cmnd(sdev, req);
out:
	return scsi_prep_return(q, req, ret);
}

static void scsi_unprep_fn(struct request_queue *q, struct request *req)
{
	scsi_uninit_cmd(blk_mq_rq_to_pdu(req));
}

/*
 * scsi_dev_queue_ready: if we can send requests to sdev, return 1 else
 * return 0.
 *
 * Called with the queue_lock held.
 */
static inline int scsi_dev_queue_ready(struct request_queue *q,
				  struct scsi_device *sdev)
{
	unsigned int busy;

	busy = atomic_inc_return(&sdev->device_busy) - 1;
	if (atomic_read(&sdev->device_blocked)) {
		if (busy)
			goto out_dec;

		/*
		 * unblock after device_blocked iterates to zero
		 */
		if (atomic_dec_return(&sdev->device_blocked) > 0) {
			/*
			 * For the MQ case we take care of this in the caller.
			 */
			if (!q->mq_ops)
				blk_delay_queue(q, SCSI_QUEUE_DELAY);
			goto out_dec;
		}
		SCSI_LOG_MLQUEUE(3, sdev_printk(KERN_INFO, sdev,
				   "unblocking device at zero depth\n"));
	}

	if (busy >= sdev->queue_depth)
		goto out_dec;

	return 1;
out_dec:
	atomic_dec(&sdev->device_busy);
	return 0;
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
				   struct scsi_device *sdev)
{
	unsigned int busy;

	if (scsi_host_in_recovery(shost))
		return 0;

	busy = atomic_inc_return(&shost->host_busy) - 1;
	if (atomic_read(&shost->host_blocked) > 0) {
		if (busy)
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

	if (shost->can_queue > 0 && busy >= shost->can_queue)
		goto starved;
	if (shost->host_self_blocked)
		goto starved;

	/* We're OK to process the command, so we can't be starved */
	if (!list_empty(&sdev->starved_entry)) {
		spin_lock_irq(shost->host_lock);
		if (!list_empty(&sdev->starved_entry))
			list_del_init(&sdev->starved_entry);
		spin_unlock_irq(shost->host_lock);
	}

	return 1;

starved:
	spin_lock_irq(shost->host_lock);
	if (list_empty(&sdev->starved_entry))
		list_add_tail(&sdev->starved_entry, &shost->starved_list);
	spin_unlock_irq(shost->host_lock);
out_dec:
	scsi_dec_host_busy(shost);
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
static int scsi_lld_busy(struct request_queue *q)
{
	struct scsi_device *sdev = q->queuedata;
	struct Scsi_Host *shost;

	if (blk_queue_dying(q))
		return 0;

	shost = sdev->host;

	/*
	 * Ignore host/starget busy state.
	 * Since block layer does not have a concept of fairness across
	 * multiple queues, congestion of host/starget needs to be handled
	 * in SCSI layer.
	 */
	if (scsi_host_in_recovery(shost) || scsi_device_is_busy(sdev))
		return 1;

	return 0;
}

/*
 * Kill a request for a dead device
 */
static void scsi_kill_request(struct request *req, struct request_queue *q)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);
	struct scsi_device *sdev;
	struct scsi_target *starget;
	struct Scsi_Host *shost;

	blk_start_request(req);

	scmd_printk(KERN_INFO, cmd, "killing request\n");

	sdev = cmd->device;
	starget = scsi_target(sdev);
	shost = sdev->host;
	scsi_init_cmd_errh(cmd);
	cmd->result = DID_NO_CONNECT << 16;
	atomic_inc(&cmd->device->iorequest_cnt);

	/*
	 * SCSI request completion path will do scsi_device_unbusy(),
	 * bump busy counts.  To bump the counters, we need to dance
	 * with the locks as normal issue path does.
	 */
	atomic_inc(&sdev->device_busy);
	atomic_inc(&shost->host_busy);
	if (starget->can_queue > 0)
		atomic_inc(&starget->target_busy);

	blk_complete_request(req);
}

static void scsi_softirq_done(struct request *rq)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);
	unsigned long wait_for = (cmd->allowed + 1) * rq->timeout;
	int disposition;

	INIT_LIST_HEAD(&cmd->eh_entry);

	atomic_inc(&cmd->device->iodone_cnt);
	if (cmd->result)
		atomic_inc(&cmd->device->ioerr_cnt);

	disposition = scsi_decide_disposition(cmd);
	if (disposition != SUCCESS &&
	    time_before(cmd->jiffies_at_alloc + wait_for, jiffies)) {
		sdev_printk(KERN_ERR, cmd->device,
			    "timing out command, waited %lus\n",
			    wait_for/HZ);
		disposition = SUCCESS;
	}

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
 * scsi_dispatch_command - Dispatch a command to the low-level driver.
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
		trace_scsi_dispatch_cmd_error(cmd, rtn);
		if (rtn != SCSI_MLQUEUE_DEVICE_BUSY &&
		    rtn != SCSI_MLQUEUE_TARGET_BUSY)
			rtn = SCSI_MLQUEUE_HOST_BUSY;

		SCSI_LOG_MLQUEUE(3, scmd_printk(KERN_INFO, cmd,
			"queuecommand : request rejected\n"));
	}

	return rtn;
 done:
	cmd->scsi_done(cmd);
	return 0;
}

/**
 * scsi_done - Invoke completion on finished SCSI command.
 * @cmd: The SCSI Command for which a low-level device driver (LLDD) gives
 * ownership back to SCSI Core -- i.e. the LLDD has finished with it.
 *
 * Description: This function is the mid-level's (SCSI Core) interrupt routine,
 * which regains ownership of the SCSI command (de facto) from a LLDD, and
 * calls blk_complete_request() for further processing.
 *
 * This function is interrupt context safe.
 */
static void scsi_done(struct scsi_cmnd *cmd)
{
	trace_scsi_dispatch_cmd_done(cmd);
	blk_complete_request(cmd->request);
}

/*
 * Function:    scsi_request_fn()
 *
 * Purpose:     Main strategy routine for SCSI.
 *
 * Arguments:   q       - Pointer to actual queue.
 *
 * Returns:     Nothing
 *
 * Lock status: IO request lock assumed to be held when called.
 */
static void scsi_request_fn(struct request_queue *q)
	__releases(q->queue_lock)
	__acquires(q->queue_lock)
{
	struct scsi_device *sdev = q->queuedata;
	struct Scsi_Host *shost;
	struct scsi_cmnd *cmd;
	struct request *req;

	/*
	 * To start with, we keep looping until the queue is empty, or until
	 * the host is no longer able to accept any more requests.
	 */
	shost = sdev->host;
	for (;;) {
		int rtn;
		/*
		 * get next queueable request.  We do this early to make sure
		 * that the request is fully prepared even if we cannot
		 * accept it.
		 */
		req = blk_peek_request(q);
		if (!req)
			break;

		if (unlikely(!scsi_device_online(sdev))) {
			sdev_printk(KERN_ERR, sdev,
				    "rejecting I/O to offline device\n");
			scsi_kill_request(req, q);
			continue;
		}

		if (!scsi_dev_queue_ready(q, sdev))
			break;

		/*
		 * Remove the request from the request list.
		 */
		if (!(blk_queue_tagged(q) && !blk_queue_start_tag(q, req)))
			blk_start_request(req);

		spin_unlock_irq(q->queue_lock);
		cmd = blk_mq_rq_to_pdu(req);
		if (cmd != req->special) {
			printk(KERN_CRIT "impossible request in %s.\n"
					 "please mail a stack trace to "
					 "linux-scsi@vger.kernel.org\n",
					 __func__);
			blk_dump_rq_flags(req, "foo");
			BUG();
		}

		/*
		 * We hit this when the driver is using a host wide
		 * tag map. For device level tag maps the queue_depth check
		 * in the device ready fn would prevent us from trying
		 * to allocate a tag. Since the map is a shared host resource
		 * we add the dev to the starved list so it eventually gets
		 * a run when a tag is freed.
		 */
		if (blk_queue_tagged(q) && !(req->rq_flags & RQF_QUEUED)) {
			spin_lock_irq(shost->host_lock);
			if (list_empty(&sdev->starved_entry))
				list_add_tail(&sdev->starved_entry,
					      &shost->starved_list);
			spin_unlock_irq(shost->host_lock);
			goto not_ready;
		}

		if (!scsi_target_queue_ready(shost, sdev))
			goto not_ready;

		if (!scsi_host_queue_ready(q, shost, sdev))
			goto host_not_ready;
	
		if (sdev->simple_tags)
			cmd->flags |= SCMD_TAGGED;
		else
			cmd->flags &= ~SCMD_TAGGED;

		/*
		 * Finally, initialize any error handling parameters, and set up
		 * the timers for timeouts.
		 */
		scsi_init_cmd_errh(cmd);

		/*
		 * Dispatch the command to the low-level driver.
		 */
		cmd->scsi_done = scsi_done;
		rtn = scsi_dispatch_cmd(cmd);
		if (rtn) {
			scsi_queue_insert(cmd, rtn);
			spin_lock_irq(q->queue_lock);
			goto out_delay;
		}
		spin_lock_irq(q->queue_lock);
	}

	return;

 host_not_ready:
	if (scsi_target(sdev)->can_queue > 0)
		atomic_dec(&scsi_target(sdev)->target_busy);
 not_ready:
	/*
	 * lock q, handle tag, requeue req, and decrement device_busy. We
	 * must return with queue_lock held.
	 *
	 * Decrementing device_busy without checking it is OK, as all such
	 * cases (host limits or settings) should run the queue at some
	 * later time.
	 */
	spin_lock_irq(q->queue_lock);
	blk_requeue_request(q, req);
	atomic_dec(&sdev->device_busy);
out_delay:
	if (!atomic_read(&sdev->device_busy) && !scsi_device_blocked(sdev))
		blk_delay_queue(q, SCSI_QUEUE_DELAY);
}

static inline blk_status_t prep_to_mq(int ret)
{
	switch (ret) {
	case BLKPREP_OK:
		return BLK_STS_OK;
	case BLKPREP_DEFER:
		return BLK_STS_RESOURCE;
	default:
		return BLK_STS_IOERR;
	}
}

/* Size in bytes of the sg-list stored in the scsi-mq command-private data. */
static unsigned int scsi_mq_sgl_size(struct Scsi_Host *shost)
{
	return min_t(unsigned int, shost->sg_tablesize, SG_CHUNK_SIZE) *
		sizeof(struct scatterlist);
}

static int scsi_mq_prep_fn(struct request *req)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(req);
	struct scsi_device *sdev = req->q->queuedata;
	struct Scsi_Host *shost = sdev->host;
	struct scatterlist *sg;

	scsi_init_command(sdev, cmd);

	req->special = cmd;

	cmd->request = req;

	cmd->tag = req->tag;
	cmd->prot_op = SCSI_PROT_NORMAL;

	sg = (void *)cmd + sizeof(struct scsi_cmnd) + shost->hostt->cmd_size;
	cmd->sdb.table.sgl = sg;

	if (scsi_host_get_prot(shost)) {
		memset(cmd->prot_sdb, 0, sizeof(struct scsi_data_buffer));

		cmd->prot_sdb->table.sgl =
			(struct scatterlist *)(cmd->prot_sdb + 1);
	}

	if (blk_bidi_rq(req)) {
		struct request *next_rq = req->next_rq;
		struct scsi_data_buffer *bidi_sdb = blk_mq_rq_to_pdu(next_rq);

		memset(bidi_sdb, 0, sizeof(struct scsi_data_buffer));
		bidi_sdb->table.sgl =
			(struct scatterlist *)(bidi_sdb + 1);

		next_rq->special = bidi_sdb;
	}

	blk_mq_start_request(req);

	return scsi_setup_cmnd(sdev, req);
}

static void scsi_mq_done(struct scsi_cmnd *cmd)
{
	trace_scsi_dispatch_cmd_done(cmd);
	blk_mq_complete_request(cmd->request);
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

	ret = prep_to_mq(scsi_prep_state_check(sdev, req));
	if (ret != BLK_STS_OK)
		goto out;

	ret = BLK_STS_RESOURCE;
	if (!get_device(&sdev->sdev_gendev))
		goto out;

	if (!scsi_dev_queue_ready(q, sdev))
		goto out_put_device;
	if (!scsi_target_queue_ready(shost, sdev))
		goto out_dec_device_busy;
	if (!scsi_host_queue_ready(q, shost, sdev))
		goto out_dec_target_busy;

	if (!(req->rq_flags & RQF_DONTPREP)) {
		ret = prep_to_mq(scsi_mq_prep_fn(req));
		if (ret != BLK_STS_OK)
			goto out_dec_host_busy;
		req->rq_flags |= RQF_DONTPREP;
	} else {
		blk_mq_start_request(req);
	}

	if (sdev->simple_tags)
		cmd->flags |= SCMD_TAGGED;
	else
		cmd->flags &= ~SCMD_TAGGED;

	scsi_init_cmd_errh(cmd);
	cmd->scsi_done = scsi_mq_done;

	reason = scsi_dispatch_cmd(cmd);
	if (reason) {
		scsi_set_blocked(cmd, reason);
		ret = BLK_STS_RESOURCE;
		goto out_dec_host_busy;
	}

	return BLK_STS_OK;

out_dec_host_busy:
	scsi_dec_host_busy(shost);
out_dec_target_busy:
	if (scsi_target(sdev)->can_queue > 0)
		atomic_dec(&scsi_target(sdev)->target_busy);
out_dec_device_busy:
	atomic_dec(&sdev->device_busy);
out_put_device:
	put_device(&sdev->sdev_gendev);
out:
	switch (ret) {
	case BLK_STS_OK:
		break;
	case BLK_STS_RESOURCE:
		if (atomic_read(&sdev->device_busy) == 0 &&
		    !scsi_device_blocked(sdev))
			blk_mq_delay_run_hw_queue(hctx, SCSI_QUEUE_DELAY);
		break;
	default:
		/*
		 * Make sure to release all allocated ressources when
		 * we hit an error, as we will never see this command
		 * again.
		 */
		if (req->rq_flags & RQF_DONTPREP)
			scsi_mq_uninit_cmd(cmd);
		break;
	}
	return ret;
}

static enum blk_eh_timer_return scsi_timeout(struct request *req,
		bool reserved)
{
	if (reserved)
		return BLK_EH_RESET_TIMER;
	return scsi_times_out(req);
}

static int scsi_mq_init_request(struct blk_mq_tag_set *set, struct request *rq,
				unsigned int hctx_idx, unsigned int numa_node)
{
	struct Scsi_Host *shost = set->driver_data;
	const bool unchecked_isa_dma = shost->unchecked_isa_dma;
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);
	struct scatterlist *sg;

	if (unchecked_isa_dma)
		cmd->flags |= SCMD_UNCHECKED_ISA_DMA;
	cmd->sense_buffer = scsi_alloc_sense_buffer(unchecked_isa_dma,
						    GFP_KERNEL, numa_node);
	if (!cmd->sense_buffer)
		return -ENOMEM;
	cmd->req.sense = cmd->sense_buffer;

	if (scsi_host_get_prot(shost)) {
		sg = (void *)cmd + sizeof(struct scsi_cmnd) +
			shost->hostt->cmd_size;
		cmd->prot_sdb = (void *)sg + scsi_mq_sgl_size(shost);
	}

	return 0;
}

static void scsi_mq_exit_request(struct blk_mq_tag_set *set, struct request *rq,
				 unsigned int hctx_idx)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);

	scsi_free_sense_buffer(cmd->flags & SCMD_UNCHECKED_ISA_DMA,
			       cmd->sense_buffer);
}

static int scsi_map_queues(struct blk_mq_tag_set *set)
{
	struct Scsi_Host *shost = container_of(set, struct Scsi_Host, tag_set);

	if (shost->hostt->map_queues)
		return shost->hostt->map_queues(shost);
	return blk_mq_map_queues(set);
}

static u64 scsi_calculate_bounce_limit(struct Scsi_Host *shost)
{
	struct device *host_dev;
	u64 bounce_limit = 0xffffffff;

	if (shost->unchecked_isa_dma)
		return BLK_BOUNCE_ISA;
	/*
	 * Platforms with virtual-DMA translation
	 * hardware have no practical limit.
	 */
	if (!PCI_DMA_BUS_IS_PHYS)
		return BLK_BOUNCE_ANY;

	host_dev = scsi_get_device(shost);
	if (host_dev && host_dev->dma_mask)
		bounce_limit = (u64)dma_max_pfn(host_dev) << PAGE_SHIFT;

	return bounce_limit;
}

void __scsi_init_queue(struct Scsi_Host *shost, struct request_queue *q)
{
	struct device *dev = shost->dma_dev;

	queue_flag_set_unlocked(QUEUE_FLAG_SCSI_PASSTHROUGH, q);

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
	blk_queue_bounce_limit(q, scsi_calculate_bounce_limit(shost));
	blk_queue_segment_boundary(q, shost->dma_boundary);
	dma_set_seg_boundary(dev, shost->dma_boundary);

	blk_queue_max_segment_size(q, dma_get_max_seg_size(dev));

	if (!shost->use_clustering)
		q->limits.cluster = 0;

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

static int scsi_old_init_rq(struct request_queue *q, struct request *rq,
			    gfp_t gfp)
{
	struct Scsi_Host *shost = q->rq_alloc_data;
	const bool unchecked_isa_dma = shost->unchecked_isa_dma;
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);

	memset(cmd, 0, sizeof(*cmd));

	if (unchecked_isa_dma)
		cmd->flags |= SCMD_UNCHECKED_ISA_DMA;
	cmd->sense_buffer = scsi_alloc_sense_buffer(unchecked_isa_dma, gfp,
						    NUMA_NO_NODE);
	if (!cmd->sense_buffer)
		goto fail;
	cmd->req.sense = cmd->sense_buffer;

	if (scsi_host_get_prot(shost) >= SHOST_DIX_TYPE0_PROTECTION) {
		cmd->prot_sdb = kmem_cache_zalloc(scsi_sdb_cache, gfp);
		if (!cmd->prot_sdb)
			goto fail_free_sense;
	}

	return 0;

fail_free_sense:
	scsi_free_sense_buffer(unchecked_isa_dma, cmd->sense_buffer);
fail:
	return -ENOMEM;
}

static void scsi_old_exit_rq(struct request_queue *q, struct request *rq)
{
	struct scsi_cmnd *cmd = blk_mq_rq_to_pdu(rq);

	if (cmd->prot_sdb)
		kmem_cache_free(scsi_sdb_cache, cmd->prot_sdb);
	scsi_free_sense_buffer(cmd->flags & SCMD_UNCHECKED_ISA_DMA,
			       cmd->sense_buffer);
}

struct request_queue *scsi_old_alloc_queue(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	struct request_queue *q;

	q = blk_alloc_queue_node(GFP_KERNEL, NUMA_NO_NODE);
	if (!q)
		return NULL;
	q->cmd_size = sizeof(struct scsi_cmnd) + shost->hostt->cmd_size;
	q->rq_alloc_data = shost;
	q->request_fn = scsi_request_fn;
	q->init_rq_fn = scsi_old_init_rq;
	q->exit_rq_fn = scsi_old_exit_rq;
	q->initialize_rq_fn = scsi_initialize_rq;

	if (blk_init_allocated_queue(q) < 0) {
		blk_cleanup_queue(q);
		return NULL;
	}

	__scsi_init_queue(shost, q);
	blk_queue_prep_rq(q, scsi_prep_fn);
	blk_queue_unprep_rq(q, scsi_unprep_fn);
	blk_queue_softirq_done(q, scsi_softirq_done);
	blk_queue_rq_timed_out(q, scsi_times_out);
	blk_queue_lld_busy(q, scsi_lld_busy);
	return q;
}

static const struct blk_mq_ops scsi_mq_ops = {
	.queue_rq	= scsi_queue_rq,
	.complete	= scsi_softirq_done,
	.timeout	= scsi_timeout,
#ifdef CONFIG_BLK_DEBUG_FS
	.show_rq	= scsi_show_rq,
#endif
	.init_request	= scsi_mq_init_request,
	.exit_request	= scsi_mq_exit_request,
	.initialize_rq_fn = scsi_initialize_rq,
	.map_queues	= scsi_map_queues,
};

struct request_queue *scsi_mq_alloc_queue(struct scsi_device *sdev)
{
	sdev->request_queue = blk_mq_init_queue(&sdev->host->tag_set);
	if (IS_ERR(sdev->request_queue))
		return NULL;

	sdev->request_queue->queuedata = sdev;
	__scsi_init_queue(sdev->host, sdev->request_queue);
	return sdev->request_queue;
}

int scsi_mq_setup_tags(struct Scsi_Host *shost)
{
	unsigned int cmd_size, sgl_size;

	sgl_size = scsi_mq_sgl_size(shost);
	cmd_size = sizeof(struct scsi_cmnd) + shost->hostt->cmd_size + sgl_size;
	if (scsi_host_get_prot(shost))
		cmd_size += sizeof(struct scsi_data_buffer) + sgl_size;

	memset(&shost->tag_set, 0, sizeof(shost->tag_set));
	shost->tag_set.ops = &scsi_mq_ops;
	shost->tag_set.nr_hw_queues = shost->nr_hw_queues ? : 1;
	shost->tag_set.queue_depth = shost->can_queue;
	shost->tag_set.cmd_size = cmd_size;
	shost->tag_set.numa_node = NUMA_NO_NODE;
	shost->tag_set.flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_SG_MERGE;
	shost->tag_set.flags |=
		BLK_ALLOC_POLICY_TO_MQ_FLAG(shost->hostt->tag_alloc_policy);
	shost->tag_set.driver_data = shost;

	return blk_mq_alloc_tag_set(&shost->tag_set);
}

void scsi_mq_destroy_tags(struct Scsi_Host *shost)
{
	blk_mq_free_tag_set(&shost->tag_set);
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

	if (q->mq_ops) {
		if (q->mq_ops == &scsi_mq_ops)
			sdev = q->queuedata;
	} else if (q->request_fn == scsi_request_fn)
		sdev = q->queuedata;
	if (!sdev || !get_device(&sdev->sdev_gendev))
		sdev = NULL;

	return sdev;
}
EXPORT_SYMBOL_GPL(scsi_device_from_queue);

/*
 * Function:    scsi_block_requests()
 *
 * Purpose:     Utility function used by low-level drivers to prevent further
 *		commands from being queued to the device.
 *
 * Arguments:   shost       - Host in question
 *
 * Returns:     Nothing
 *
 * Lock status: No locks are assumed held.
 *
 * Notes:       There is no timer nor any other means by which the requests
 *		get unblocked other than the low-level driver calling
 *		scsi_unblock_requests().
 */
void scsi_block_requests(struct Scsi_Host *shost)
{
	shost->host_self_blocked = 1;
}
EXPORT_SYMBOL(scsi_block_requests);

/*
 * Function:    scsi_unblock_requests()
 *
 * Purpose:     Utility function used by low-level drivers to allow further
 *		commands from being queued to the device.
 *
 * Arguments:   shost       - Host in question
 *
 * Returns:     Nothing
 *
 * Lock status: No locks are assumed held.
 *
 * Notes:       There is no timer nor any other means by which the requests
 *		get unblocked other than the low-level driver calling
 *		scsi_unblock_requests().
 *
 *		This is done as an API function so that changes to the
 *		internals of the scsi mid-layer won't require wholesale
 *		changes to drivers that use this feature.
 */
void scsi_unblock_requests(struct Scsi_Host *shost)
{
	shost->host_self_blocked = 0;
	scsi_run_host_queues(shost);
}
EXPORT_SYMBOL(scsi_unblock_requests);

int __init scsi_init_queue(void)
{
	scsi_sdb_cache = kmem_cache_create("scsi_data_buffer",
					   sizeof(struct scsi_data_buffer),
					   0, 0, NULL);
	if (!scsi_sdb_cache) {
		printk(KERN_ERR "SCSI: can't init scsi sdb cache\n");
		return -ENOMEM;
	}

	return 0;
}

void scsi_exit_queue(void)
{
	kmem_cache_destroy(scsi_sense_cache);
	kmem_cache_destroy(scsi_sense_isadma_cache);
	kmem_cache_destroy(scsi_sdb_cache);
}

/**
 *	scsi_mode_select - issue a mode select
 *	@sdev:	SCSI device to be queried
 *	@pf:	Page format bit (1 == standard, 0 == vendor specific)
 *	@sp:	Save page bit (0 == don't save, 1 == save)
 *	@modepage: mode page being requested
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
int
scsi_mode_select(struct scsi_device *sdev, int pf, int sp, int modepage,
		 unsigned char *buffer, int len, int timeout, int retries,
		 struct scsi_mode_data *data, struct scsi_sense_hdr *sshdr)
{
	unsigned char cmd[10];
	unsigned char *real_buffer;
	int ret;

	memset(cmd, 0, sizeof(cmd));
	cmd[1] = (pf ? 0x10 : 0) | (sp ? 0x01 : 0);

	if (sdev->use_10_for_ms) {
		if (len > 65535)
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
		real_buffer[6] = data->block_descriptor_length >> 8;
		real_buffer[7] = data->block_descriptor_length;

		cmd[0] = MODE_SELECT_10;
		cmd[7] = len >> 8;
		cmd[8] = len;
	} else {
		if (len > 255 || data->block_descriptor_length > 255 ||
		    data->longlba)
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
 *	@dbd:	set if mode sense will allow block descriptors to be returned
 *	@modepage: mode page being requested
 *	@buffer: request buffer (may not be smaller than eight bytes)
 *	@len:	length of request buffer.
 *	@timeout: command timeout
 *	@retries: number of retries before failing
 *	@data: returns a structure abstracting the mode header data
 *	@sshdr: place to put sense data (or NULL if no sense to be collected).
 *		must be SCSI_SENSE_BUFFERSIZE big.
 *
 *	Returns zero if unsuccessful, or the header offset (either 4
 *	or 8 depending on whether a six or ten byte command was
 *	issued) if successful.
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
	cmd[1] = dbd & 0x18;	/* allows DBD and LLBA bits */
	cmd[2] = modepage;

	/* caller might not be interested in sense, but we need it */
	if (!sshdr)
		sshdr = &my_sshdr;

 retry:
	use_10_for_ms = sdev->use_10_for_ms;

	if (use_10_for_ms) {
		if (len < 8)
			len = 8;

		cmd[0] = MODE_SENSE_10;
		cmd[8] = len;
		header_length = 8;
	} else {
		if (len < 4)
			len = 4;

		cmd[0] = MODE_SENSE;
		cmd[4] = len;
		header_length = 4;
	}

	memset(buffer, 0, len);

	result = scsi_execute_req(sdev, cmd, DMA_FROM_DEVICE, buffer, len,
				  sshdr, timeout, retries, NULL);

	/* This code looks awful: what it's doing is making sure an
	 * ILLEGAL REQUEST sense return identifies the actual command
	 * byte as the problem.  MODE_SENSE commands can return
	 * ILLEGAL REQUEST if the code page isn't supported */

	if (use_10_for_ms && !scsi_status_is_good(result) &&
	    (driver_byte(result) & DRIVER_SENSE)) {
		if (scsi_sense_valid(sshdr)) {
			if ((sshdr->sense_key == ILLEGAL_REQUEST) &&
			    (sshdr->asc == 0x20) && (sshdr->ascq == 0)) {
				/* 
				 * Invalid command operation code
				 */
				sdev->use_10_for_ms = 0;
				goto retry;
			}
		}
	}

	if(scsi_status_is_good(result)) {
		if (unlikely(buffer[0] == 0x86 && buffer[1] == 0x0b &&
			     (modepage == 6 || modepage == 8))) {
			/* Initio breakage? */
			header_length = 0;
			data->length = 13;
			data->medium_type = 0;
			data->device_specific = 0;
			data->longlba = 0;
			data->block_descriptor_length = 0;
		} else if(use_10_for_ms) {
			data->length = buffer[0]*256 + buffer[1] + 2;
			data->medium_type = buffer[2];
			data->device_specific = buffer[3];
			data->longlba = buffer[4] & 0x01;
			data->block_descriptor_length = buffer[6]*256
				+ buffer[7];
		} else {
			data->length = buffer[0] + 1;
			data->medium_type = buffer[1];
			data->device_specific = buffer[2];
			data->block_descriptor_length = buffer[3];
		}
		data->header_length = header_length;
	} else if ((status_byte(result) == CHECK_CONDITION) &&
		   scsi_sense_valid(sshdr) &&
		   sshdr->sense_key == UNIT_ATTENTION && retry_count) {
		retry_count--;
		goto retry;
	}

	return result;
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
					  timeout, retries, NULL);
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
 * 	sdev_evt_emit - emit a single SCSI device uevent
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
		scsi_rescan_device(&sdev->sdev_gendev);
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
	default:
		/* do nothing */
		break;
	}

	envp[idx++] = NULL;

	kobject_uevent_env(&sdev->sdev_gendev.kobj, KOBJ_CHANGE, envp);
}

/**
 * 	sdev_evt_thread - send a uevent for each scsi event
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
 * scsi_request_fn_active() - number of kernel threads inside scsi_request_fn()
 * @sdev: SCSI device to count the number of scsi_request_fn() callers for.
 */
static int scsi_request_fn_active(struct scsi_device *sdev)
{
	struct request_queue *q = sdev->request_queue;
	int request_fn_active;

	WARN_ON_ONCE(sdev->host->use_blk_mq);

	spin_lock_irq(q->queue_lock);
	request_fn_active = q->request_fn_active;
	spin_unlock_irq(q->queue_lock);

	return request_fn_active;
}

/**
 * scsi_wait_for_queuecommand() - wait for ongoing queuecommand() calls
 * @sdev: SCSI device pointer.
 *
 * Wait until the ongoing shost->hostt->queuecommand() calls that are
 * invoked from scsi_request_fn() have finished.
 */
static void scsi_wait_for_queuecommand(struct scsi_device *sdev)
{
	WARN_ON_ONCE(sdev->host->use_blk_mq);

	while (scsi_request_fn_active(sdev))
		msleep(20);
}

/**
 *	scsi_device_quiesce - Block user issued commands.
 *	@sdev:	scsi device to quiesce.
 *
 *	This works by trying to transition to the SDEV_QUIESCE state
 *	(which must be a legal transition).  When the device is in this
 *	state, only special requests will be accepted, all others will
 *	be deferred.  Since special requests may also be requeued requests,
 *	a successful return doesn't guarantee the device will be 
 *	totally quiescent.
 *
 *	Must be called with user context, may sleep.
 *
 *	Returns zero if unsuccessful or an error if not.
 */
int
scsi_device_quiesce(struct scsi_device *sdev)
{
	int err;

	mutex_lock(&sdev->state_mutex);
	err = scsi_device_set_state(sdev, SDEV_QUIESCE);
	mutex_unlock(&sdev->state_mutex);

	if (err)
		return err;

	scsi_run_queue(sdev->request_queue);
	while (atomic_read(&sdev->device_busy)) {
		msleep_interruptible(200);
		scsi_run_queue(sdev->request_queue);
	}
	return 0;
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
	if (sdev->sdev_state == SDEV_QUIESCE &&
	    scsi_device_set_state(sdev, SDEV_RUNNING) == 0)
		scsi_run_queue(sdev->request_queue);
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
	struct request_queue *q = sdev->request_queue;
	unsigned long flags;
	int err = 0;

	err = scsi_device_set_state(sdev, SDEV_BLOCK);
	if (err) {
		err = scsi_device_set_state(sdev, SDEV_CREATED_BLOCK);

		if (err)
			return err;
	}

	/* 
	 * The device has transitioned to SDEV_BLOCK.  Stop the
	 * block layer from calling the midlayer with this device's
	 * request queue. 
	 */
	if (q->mq_ops) {
		blk_mq_quiesce_queue_nowait(q);
	} else {
		spin_lock_irqsave(q->queue_lock, flags);
		blk_stop_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}

	return 0;
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
 *
 * To do: avoid that scsi_send_eh_cmnd() calls queuecommand() after
 * scsi_internal_device_block() has blocked a SCSI device and also
 * remove the rport mutex lock and unlock calls from srp_queuecommand().
 */
static int scsi_internal_device_block(struct scsi_device *sdev)
{
	struct request_queue *q = sdev->request_queue;
	int err;

	mutex_lock(&sdev->state_mutex);
	err = scsi_internal_device_block_nowait(sdev);
	if (err == 0) {
		if (q->mq_ops)
			blk_mq_quiesce_queue(q);
		else
			scsi_wait_for_queuecommand(sdev);
	}
	mutex_unlock(&sdev->state_mutex);

	return err;
}
 
void scsi_start_queue(struct scsi_device *sdev)
{
	struct request_queue *q = sdev->request_queue;
	unsigned long flags;

	if (q->mq_ops) {
		blk_mq_unquiesce_queue(q);
	} else {
		spin_lock_irqsave(q->queue_lock, flags);
		blk_start_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
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
	scsi_internal_device_block(sdev);
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
	u8 cur_id_type = 0xff;
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

	/*
	 * Look for the correct descriptor.
	 * Order of preference for lun descriptor:
	 * - SCSI name string
	 * - NAA IEEE Registered Extended
	 * - EUI-64 based 16-byte
	 * - EUI-64 based 12-byte
	 * - NAA IEEE Registered
	 * - NAA IEEE Extended
	 * - T10 Vendor ID
	 * as longer descriptors reduce the likelyhood
	 * of identification clashes.
	 */

	/* The id string must be at least 20 bytes + terminating NULL byte */
	if (id_len < 21) {
		rcu_read_unlock();
		return -EINVAL;
	}

	memset(id, 0, id_len);
	d = vpd_pg83->data + 4;
	while (d < vpd_pg83->data + vpd_pg83->len) {
		/* Skip designators not referring to the LUN */
		if ((d[1] & 0x30) != 0x00)
			goto next_desig;

		switch (d[1] & 0xf) {
		case 0x1:
			/* T10 Vendor ID */
			if (cur_id_size > d[3])
				break;
			/* Prefer anything */
			if (cur_id_type > 0x01 && cur_id_type != 0xff)
				break;
			cur_id_size = d[3];
			if (cur_id_size + 4 > id_len)
				cur_id_size = id_len - 4;
			cur_id_str = d + 4;
			cur_id_type = d[1] & 0xf;
			id_size = snprintf(id, id_len, "t10.%*pE",
					   cur_id_size, cur_id_str);
			break;
		case 0x2:
			/* EUI-64 */
			if (cur_id_size > d[3])
				break;
			/* Prefer NAA IEEE Registered Extended */
			if (cur_id_type == 0x3 &&
			    cur_id_size == d[3])
				break;
			cur_id_size = d[3];
			cur_id_str = d + 4;
			cur_id_type = d[1] & 0xf;
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
				cur_id_size = 0;
				break;
			}
			break;
		case 0x3:
			/* NAA */
			if (cur_id_size > d[3])
				break;
			cur_id_size = d[3];
			cur_id_str = d + 4;
			cur_id_type = d[1] & 0xf;
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
				cur_id_size = 0;
				break;
			}
			break;
		case 0x8:
			/* SCSI name string */
			if (cur_id_size + 4 > d[3])
				break;
			/* Prefer others for truncated descriptor */
			if (cur_id_size && d[3] > id_len)
				break;
			cur_id_size = id_size = d[3];
			cur_id_str = d + 4;
			cur_id_type = d[1] & 0xf;
			if (cur_id_size >= id_len)
				cur_id_size = id_len - 1;
			memcpy(id, cur_id_str, cur_id_size);
			/* Decrease priority for truncated descriptor */
			if (cur_id_size != id_size)
				cur_id_size = 6;
			break;
		default:
			break;
		}
next_desig:
		d += d[3] + 4;
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
