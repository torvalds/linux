/*
 *  scsi_error.c Copyright (C) 1997 Eric Youngdale
 *
 *  SCSI error/timeout handling
 *      Initial versions: Eric Youngdale.  Based upon conversations with
 *                        Leonard Zubkoff and David Miller at Linux Expo,
 *                        ideas originating from all over the place.
 *
 *	Restructured scsi_unjam_host and associated functions.
 *	September 04, 2002 Mike Anderson (andmike@us.ibm.com)
 *
 *	Forward port of Russell King's (rmk@arm.linux.org.uk) changes and
 *	minor cleanups.
 *	September 30, 2002 Mike Anderson (andmike@us.ibm.com)
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/gfp.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_common.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_dh.h>
#include <scsi/sg.h>

#include "scsi_priv.h"
#include "scsi_logging.h"
#include "scsi_transport_api.h"

#include <trace/events/scsi.h>

static void scsi_eh_done(struct scsi_cmnd *scmd);

/*
 * These should *probably* be handled by the host itself.
 * Since it is allowed to sleep, it probably should.
 */
#define BUS_RESET_SETTLE_TIME   (10)
#define HOST_RESET_SETTLE_TIME  (10)

static int scsi_eh_try_stu(struct scsi_cmnd *scmd);
static int scsi_try_to_abort_cmd(struct scsi_host_template *,
				 struct scsi_cmnd *);

/* called with shost->host_lock held */
void scsi_eh_wakeup(struct Scsi_Host *shost)
{
	if (atomic_read(&shost->host_busy) == shost->host_failed) {
		trace_scsi_eh_wakeup(shost);
		wake_up_process(shost->ehandler);
		SCSI_LOG_ERROR_RECOVERY(5, shost_printk(KERN_INFO, shost,
			"Waking error handler thread\n"));
	}
}

/**
 * scsi_schedule_eh - schedule EH for SCSI host
 * @shost:	SCSI host to invoke error handling on.
 *
 * Schedule SCSI EH without scmd.
 */
void scsi_schedule_eh(struct Scsi_Host *shost)
{
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);

	if (scsi_host_set_state(shost, SHOST_RECOVERY) == 0 ||
	    scsi_host_set_state(shost, SHOST_CANCEL_RECOVERY) == 0) {
		shost->host_eh_scheduled++;
		scsi_eh_wakeup(shost);
	}

	spin_unlock_irqrestore(shost->host_lock, flags);
}
EXPORT_SYMBOL_GPL(scsi_schedule_eh);

static int scsi_host_eh_past_deadline(struct Scsi_Host *shost)
{
	if (!shost->last_reset || shost->eh_deadline == -1)
		return 0;

	/*
	 * 32bit accesses are guaranteed to be atomic
	 * (on all supported architectures), so instead
	 * of using a spinlock we can as well double check
	 * if eh_deadline has been set to 'off' during the
	 * time_before call.
	 */
	if (time_before(jiffies, shost->last_reset + shost->eh_deadline) &&
	    shost->eh_deadline > -1)
		return 0;

	return 1;
}

/**
 * scmd_eh_abort_handler - Handle command aborts
 * @work:	command to be aborted.
 */
void
scmd_eh_abort_handler(struct work_struct *work)
{
	struct scsi_cmnd *scmd =
		container_of(work, struct scsi_cmnd, abort_work.work);
	struct scsi_device *sdev = scmd->device;
	int rtn;

	if (scsi_host_eh_past_deadline(sdev->host)) {
		SCSI_LOG_ERROR_RECOVERY(3,
			scmd_printk(KERN_INFO, scmd,
				    "eh timeout, not aborting\n"));
	} else {
		SCSI_LOG_ERROR_RECOVERY(3,
			scmd_printk(KERN_INFO, scmd,
				    "aborting command\n"));
		rtn = scsi_try_to_abort_cmd(sdev->host->hostt, scmd);
		if (rtn == SUCCESS) {
			set_host_byte(scmd, DID_TIME_OUT);
			if (scsi_host_eh_past_deadline(sdev->host)) {
				SCSI_LOG_ERROR_RECOVERY(3,
					scmd_printk(KERN_INFO, scmd,
						    "eh timeout, not retrying "
						    "aborted command\n"));
			} else if (!scsi_noretry_cmd(scmd) &&
			    (++scmd->retries <= scmd->allowed)) {
				SCSI_LOG_ERROR_RECOVERY(3,
					scmd_printk(KERN_WARNING, scmd,
						    "retry aborted command\n"));
				scsi_queue_insert(scmd, SCSI_MLQUEUE_EH_RETRY);
				return;
			} else {
				SCSI_LOG_ERROR_RECOVERY(3,
					scmd_printk(KERN_WARNING, scmd,
						    "finish aborted command\n"));
				scsi_finish_command(scmd);
				return;
			}
		} else {
			SCSI_LOG_ERROR_RECOVERY(3,
				scmd_printk(KERN_INFO, scmd,
					    "cmd abort %s\n",
					    (rtn == FAST_IO_FAIL) ?
					    "not send" : "failed"));
		}
	}

	if (!scsi_eh_scmd_add(scmd, 0)) {
		SCSI_LOG_ERROR_RECOVERY(3,
			scmd_printk(KERN_WARNING, scmd,
				    "terminate aborted command\n"));
		set_host_byte(scmd, DID_TIME_OUT);
		scsi_finish_command(scmd);
	}
}

/**
 * scsi_abort_command - schedule a command abort
 * @scmd:	scmd to abort.
 *
 * We only need to abort commands after a command timeout
 */
static int
scsi_abort_command(struct scsi_cmnd *scmd)
{
	struct scsi_device *sdev = scmd->device;
	struct Scsi_Host *shost = sdev->host;
	unsigned long flags;

	if (scmd->eh_eflags & SCSI_EH_ABORT_SCHEDULED) {
		/*
		 * Retry after abort failed, escalate to next level.
		 */
		scmd->eh_eflags &= ~SCSI_EH_ABORT_SCHEDULED;
		SCSI_LOG_ERROR_RECOVERY(3,
			scmd_printk(KERN_INFO, scmd,
				    "previous abort failed\n"));
		BUG_ON(delayed_work_pending(&scmd->abort_work));
		return FAILED;
	}

	/*
	 * Do not try a command abort if
	 * SCSI EH has already started.
	 */
	spin_lock_irqsave(shost->host_lock, flags);
	if (scsi_host_in_recovery(shost)) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		SCSI_LOG_ERROR_RECOVERY(3,
			scmd_printk(KERN_INFO, scmd,
				    "not aborting, host in recovery\n"));
		return FAILED;
	}

	if (shost->eh_deadline != -1 && !shost->last_reset)
		shost->last_reset = jiffies;
	spin_unlock_irqrestore(shost->host_lock, flags);

	scmd->eh_eflags |= SCSI_EH_ABORT_SCHEDULED;
	SCSI_LOG_ERROR_RECOVERY(3,
		scmd_printk(KERN_INFO, scmd, "abort scheduled\n"));
	queue_delayed_work(shost->tmf_work_q, &scmd->abort_work, HZ / 100);
	return SUCCESS;
}

/**
 * scsi_eh_scmd_add - add scsi cmd to error handling.
 * @scmd:	scmd to run eh on.
 * @eh_flag:	optional SCSI_EH flag.
 *
 * Return value:
 *	0 on failure.
 */
int scsi_eh_scmd_add(struct scsi_cmnd *scmd, int eh_flag)
{
	struct Scsi_Host *shost = scmd->device->host;
	unsigned long flags;
	int ret = 0;

	if (!shost->ehandler)
		return 0;

	spin_lock_irqsave(shost->host_lock, flags);
	if (scsi_host_set_state(shost, SHOST_RECOVERY))
		if (scsi_host_set_state(shost, SHOST_CANCEL_RECOVERY))
			goto out_unlock;

	if (shost->eh_deadline != -1 && !shost->last_reset)
		shost->last_reset = jiffies;

	ret = 1;
	if (scmd->eh_eflags & SCSI_EH_ABORT_SCHEDULED)
		eh_flag &= ~SCSI_EH_CANCEL_CMD;
	scmd->eh_eflags |= eh_flag;
	list_add_tail(&scmd->eh_entry, &shost->eh_cmd_q);
	shost->host_failed++;
	scsi_eh_wakeup(shost);
 out_unlock:
	spin_unlock_irqrestore(shost->host_lock, flags);
	return ret;
}

/**
 * scsi_times_out - Timeout function for normal scsi commands.
 * @req:	request that is timing out.
 *
 * Notes:
 *     We do not need to lock this.  There is the potential for a race
 *     only in that the normal completion handling might run, but if the
 *     normal completion function determines that the timer has already
 *     fired, then it mustn't do anything.
 */
enum blk_eh_timer_return scsi_times_out(struct request *req)
{
	struct scsi_cmnd *scmd = req->special;
	enum blk_eh_timer_return rtn = BLK_EH_NOT_HANDLED;
	struct Scsi_Host *host = scmd->device->host;

	trace_scsi_dispatch_cmd_timeout(scmd);
	scsi_log_completion(scmd, TIMEOUT_ERROR);

	if (host->eh_deadline != -1 && !host->last_reset)
		host->last_reset = jiffies;

	if (host->transportt->eh_timed_out)
		rtn = host->transportt->eh_timed_out(scmd);
	else if (host->hostt->eh_timed_out)
		rtn = host->hostt->eh_timed_out(scmd);

	if (rtn == BLK_EH_NOT_HANDLED) {
		if (!host->hostt->no_async_abort &&
		    scsi_abort_command(scmd) == SUCCESS)
			return BLK_EH_NOT_HANDLED;

		set_host_byte(scmd, DID_TIME_OUT);
		if (!scsi_eh_scmd_add(scmd, SCSI_EH_CANCEL_CMD))
			rtn = BLK_EH_HANDLED;
	}

	return rtn;
}

/**
 * scsi_block_when_processing_errors - Prevent cmds from being queued.
 * @sdev:	Device on which we are performing recovery.
 *
 * Description:
 *     We block until the host is out of error recovery, and then check to
 *     see whether the host or the device is offline.
 *
 * Return value:
 *     0 when dev was taken offline by error recovery. 1 OK to proceed.
 */
int scsi_block_when_processing_errors(struct scsi_device *sdev)
{
	int online;

	wait_event(sdev->host->host_wait, !scsi_host_in_recovery(sdev->host));

	online = scsi_device_online(sdev);

	SCSI_LOG_ERROR_RECOVERY(5, sdev_printk(KERN_INFO, sdev,
		"%s: rtn: %d\n", __func__, online));

	return online;
}
EXPORT_SYMBOL(scsi_block_when_processing_errors);

#ifdef CONFIG_SCSI_LOGGING
/**
 * scsi_eh_prt_fail_stats - Log info on failures.
 * @shost:	scsi host being recovered.
 * @work_q:	Queue of scsi cmds to process.
 */
static inline void scsi_eh_prt_fail_stats(struct Scsi_Host *shost,
					  struct list_head *work_q)
{
	struct scsi_cmnd *scmd;
	struct scsi_device *sdev;
	int total_failures = 0;
	int cmd_failed = 0;
	int cmd_cancel = 0;
	int devices_failed = 0;

	shost_for_each_device(sdev, shost) {
		list_for_each_entry(scmd, work_q, eh_entry) {
			if (scmd->device == sdev) {
				++total_failures;
				if (scmd->eh_eflags & SCSI_EH_CANCEL_CMD)
					++cmd_cancel;
				else
					++cmd_failed;
			}
		}

		if (cmd_cancel || cmd_failed) {
			SCSI_LOG_ERROR_RECOVERY(3,
				shost_printk(KERN_INFO, shost,
					    "%s: cmds failed: %d, cancel: %d\n",
					    __func__, cmd_failed,
					    cmd_cancel));
			cmd_cancel = 0;
			cmd_failed = 0;
			++devices_failed;
		}
	}

	SCSI_LOG_ERROR_RECOVERY(2, shost_printk(KERN_INFO, shost,
				   "Total of %d commands on %d"
				   " devices require eh work\n",
				   total_failures, devices_failed));
}
#endif

 /**
 * scsi_report_lun_change - Set flag on all *other* devices on the same target
 *                          to indicate that a UNIT ATTENTION is expected.
 * @sdev:	Device reporting the UNIT ATTENTION
 */
static void scsi_report_lun_change(struct scsi_device *sdev)
{
	sdev->sdev_target->expecting_lun_change = 1;
}

/**
 * scsi_report_sense - Examine scsi sense information and log messages for
 *		       certain conditions, also issue uevents for some of them.
 * @sdev:	Device reporting the sense code
 * @sshdr:	sshdr to be examined
 */
static void scsi_report_sense(struct scsi_device *sdev,
			      struct scsi_sense_hdr *sshdr)
{
	enum scsi_device_event evt_type = SDEV_EVT_MAXBITS;	/* i.e. none */

	if (sshdr->sense_key == UNIT_ATTENTION) {
		if (sshdr->asc == 0x3f && sshdr->ascq == 0x03) {
			evt_type = SDEV_EVT_INQUIRY_CHANGE_REPORTED;
			sdev_printk(KERN_WARNING, sdev,
				    "Inquiry data has changed");
		} else if (sshdr->asc == 0x3f && sshdr->ascq == 0x0e) {
			evt_type = SDEV_EVT_LUN_CHANGE_REPORTED;
			scsi_report_lun_change(sdev);
			sdev_printk(KERN_WARNING, sdev,
				    "Warning! Received an indication that the "
				    "LUN assignments on this target have "
				    "changed. The Linux SCSI layer does not "
				    "automatically remap LUN assignments.\n");
		} else if (sshdr->asc == 0x3f)
			sdev_printk(KERN_WARNING, sdev,
				    "Warning! Received an indication that the "
				    "operating parameters on this target have "
				    "changed. The Linux SCSI layer does not "
				    "automatically adjust these parameters.\n");

		if (sshdr->asc == 0x38 && sshdr->ascq == 0x07) {
			evt_type = SDEV_EVT_SOFT_THRESHOLD_REACHED_REPORTED;
			sdev_printk(KERN_WARNING, sdev,
				    "Warning! Received an indication that the "
				    "LUN reached a thin provisioning soft "
				    "threshold.\n");
		}

		if (sshdr->asc == 0x2a && sshdr->ascq == 0x01) {
			evt_type = SDEV_EVT_MODE_PARAMETER_CHANGE_REPORTED;
			sdev_printk(KERN_WARNING, sdev,
				    "Mode parameters changed");
		} else if (sshdr->asc == 0x2a && sshdr->ascq == 0x06) {
			evt_type = SDEV_EVT_ALUA_STATE_CHANGE_REPORTED;
			sdev_printk(KERN_WARNING, sdev,
				    "Asymmetric access state changed");
		} else if (sshdr->asc == 0x2a && sshdr->ascq == 0x09) {
			evt_type = SDEV_EVT_CAPACITY_CHANGE_REPORTED;
			sdev_printk(KERN_WARNING, sdev,
				    "Capacity data has changed");
		} else if (sshdr->asc == 0x2a)
			sdev_printk(KERN_WARNING, sdev,
				    "Parameters changed");
	}

	if (evt_type != SDEV_EVT_MAXBITS) {
		set_bit(evt_type, sdev->pending_events);
		schedule_work(&sdev->event_work);
	}
}

/**
 * scsi_check_sense - Examine scsi cmd sense
 * @scmd:	Cmd to have sense checked.
 *
 * Return value:
 *	SUCCESS or FAILED or NEEDS_RETRY or ADD_TO_MLQUEUE
 *
 * Notes:
 *	When a deferred error is detected the current command has
 *	not been executed and needs retrying.
 */
static int scsi_check_sense(struct scsi_cmnd *scmd)
{
	struct scsi_device *sdev = scmd->device;
	struct scsi_sense_hdr sshdr;

	if (! scsi_command_normalize_sense(scmd, &sshdr))
		return FAILED;	/* no valid sense data */

	scsi_report_sense(sdev, &sshdr);

	if (scsi_sense_is_deferred(&sshdr))
		return NEEDS_RETRY;

	if (sdev->handler && sdev->handler->check_sense) {
		int rc;

		rc = sdev->handler->check_sense(sdev, &sshdr);
		if (rc != SCSI_RETURN_NOT_HANDLED)
			return rc;
		/* handler does not care. Drop down to default handling */
	}

	if (scmd->cmnd[0] == TEST_UNIT_READY && scmd->scsi_done != scsi_eh_done)
		/*
		 * nasty: for mid-layer issued TURs, we need to return the
		 * actual sense data without any recovery attempt.  For eh
		 * issued ones, we need to try to recover and interpret
		 */
		return SUCCESS;

	/*
	 * Previous logic looked for FILEMARK, EOM or ILI which are
	 * mainly associated with tapes and returned SUCCESS.
	 */
	if (sshdr.response_code == 0x70) {
		/* fixed format */
		if (scmd->sense_buffer[2] & 0xe0)
			return SUCCESS;
	} else {
		/*
		 * descriptor format: look for "stream commands sense data
		 * descriptor" (see SSC-3). Assume single sense data
		 * descriptor. Ignore ILI from SBC-2 READ LONG and WRITE LONG.
		 */
		if ((sshdr.additional_length > 3) &&
		    (scmd->sense_buffer[8] == 0x4) &&
		    (scmd->sense_buffer[11] & 0xe0))
			return SUCCESS;
	}

	switch (sshdr.sense_key) {
	case NO_SENSE:
		return SUCCESS;
	case RECOVERED_ERROR:
		return /* soft_error */ SUCCESS;

	case ABORTED_COMMAND:
		if (sshdr.asc == 0x10) /* DIF */
			return SUCCESS;

		return NEEDS_RETRY;
	case NOT_READY:
	case UNIT_ATTENTION:
		/*
		 * if we are expecting a cc/ua because of a bus reset that we
		 * performed, treat this just as a retry.  otherwise this is
		 * information that we should pass up to the upper-level driver
		 * so that we can deal with it there.
		 */
		if (scmd->device->expecting_cc_ua) {
			/*
			 * Because some device does not queue unit
			 * attentions correctly, we carefully check
			 * additional sense code and qualifier so as
			 * not to squash media change unit attention.
			 */
			if (sshdr.asc != 0x28 || sshdr.ascq != 0x00) {
				scmd->device->expecting_cc_ua = 0;
				return NEEDS_RETRY;
			}
		}
		/*
		 * we might also expect a cc/ua if another LUN on the target
		 * reported a UA with an ASC/ASCQ of 3F 0E -
		 * REPORTED LUNS DATA HAS CHANGED.
		 */
		if (scmd->device->sdev_target->expecting_lun_change &&
		    sshdr.asc == 0x3f && sshdr.ascq == 0x0e)
			return NEEDS_RETRY;
		/*
		 * if the device is in the process of becoming ready, we
		 * should retry.
		 */
		if ((sshdr.asc == 0x04) && (sshdr.ascq == 0x01))
			return NEEDS_RETRY;
		/*
		 * if the device is not started, we need to wake
		 * the error handler to start the motor
		 */
		if (scmd->device->allow_restart &&
		    (sshdr.asc == 0x04) && (sshdr.ascq == 0x02))
			return FAILED;
		/*
		 * Pass the UA upwards for a determination in the completion
		 * functions.
		 */
		return SUCCESS;

		/* these are not supported */
	case DATA_PROTECT:
		if (sshdr.asc == 0x27 && sshdr.ascq == 0x07) {
			/* Thin provisioning hard threshold reached */
			set_host_byte(scmd, DID_ALLOC_FAILURE);
			return SUCCESS;
		}
	case COPY_ABORTED:
	case VOLUME_OVERFLOW:
	case MISCOMPARE:
	case BLANK_CHECK:
		set_host_byte(scmd, DID_TARGET_FAILURE);
		return SUCCESS;

	case MEDIUM_ERROR:
		if (sshdr.asc == 0x11 || /* UNRECOVERED READ ERR */
		    sshdr.asc == 0x13 || /* AMNF DATA FIELD */
		    sshdr.asc == 0x14) { /* RECORD NOT FOUND */
			set_host_byte(scmd, DID_MEDIUM_ERROR);
			return SUCCESS;
		}
		return NEEDS_RETRY;

	case HARDWARE_ERROR:
		if (scmd->device->retry_hwerror)
			return ADD_TO_MLQUEUE;
		else
			set_host_byte(scmd, DID_TARGET_FAILURE);

	case ILLEGAL_REQUEST:
		if (sshdr.asc == 0x20 || /* Invalid command operation code */
		    sshdr.asc == 0x21 || /* Logical block address out of range */
		    sshdr.asc == 0x24 || /* Invalid field in cdb */
		    sshdr.asc == 0x26) { /* Parameter value invalid */
			set_host_byte(scmd, DID_TARGET_FAILURE);
		}
		return SUCCESS;

	default:
		return SUCCESS;
	}
}

static void scsi_handle_queue_ramp_up(struct scsi_device *sdev)
{
	struct scsi_host_template *sht = sdev->host->hostt;
	struct scsi_device *tmp_sdev;

	if (!sht->track_queue_depth ||
	    sdev->queue_depth >= sdev->max_queue_depth)
		return;

	if (time_before(jiffies,
	    sdev->last_queue_ramp_up + sdev->queue_ramp_up_period))
		return;

	if (time_before(jiffies,
	    sdev->last_queue_full_time + sdev->queue_ramp_up_period))
		return;

	/*
	 * Walk all devices of a target and do
	 * ramp up on them.
	 */
	shost_for_each_device(tmp_sdev, sdev->host) {
		if (tmp_sdev->channel != sdev->channel ||
		    tmp_sdev->id != sdev->id ||
		    tmp_sdev->queue_depth == sdev->max_queue_depth)
			continue;

		scsi_change_queue_depth(tmp_sdev, tmp_sdev->queue_depth + 1);
		sdev->last_queue_ramp_up = jiffies;
	}
}

static void scsi_handle_queue_full(struct scsi_device *sdev)
{
	struct scsi_host_template *sht = sdev->host->hostt;
	struct scsi_device *tmp_sdev;

	if (!sht->track_queue_depth)
		return;

	shost_for_each_device(tmp_sdev, sdev->host) {
		if (tmp_sdev->channel != sdev->channel ||
		    tmp_sdev->id != sdev->id)
			continue;
		/*
		 * We do not know the number of commands that were at
		 * the device when we got the queue full so we start
		 * from the highest possible value and work our way down.
		 */
		scsi_track_queue_full(tmp_sdev, tmp_sdev->queue_depth - 1);
	}
}

/**
 * scsi_eh_completed_normally - Disposition a eh cmd on return from LLD.
 * @scmd:	SCSI cmd to examine.
 *
 * Notes:
 *    This is *only* called when we are examining the status of commands
 *    queued during error recovery.  the main difference here is that we
 *    don't allow for the possibility of retries here, and we are a lot
 *    more restrictive about what we consider acceptable.
 */
static int scsi_eh_completed_normally(struct scsi_cmnd *scmd)
{
	/*
	 * first check the host byte, to see if there is anything in there
	 * that would indicate what we need to do.
	 */
	if (host_byte(scmd->result) == DID_RESET) {
		/*
		 * rats.  we are already in the error handler, so we now
		 * get to try and figure out what to do next.  if the sense
		 * is valid, we have a pretty good idea of what to do.
		 * if not, we mark it as FAILED.
		 */
		return scsi_check_sense(scmd);
	}
	if (host_byte(scmd->result) != DID_OK)
		return FAILED;

	/*
	 * next, check the message byte.
	 */
	if (msg_byte(scmd->result) != COMMAND_COMPLETE)
		return FAILED;

	/*
	 * now, check the status byte to see if this indicates
	 * anything special.
	 */
	switch (status_byte(scmd->result)) {
	case GOOD:
		scsi_handle_queue_ramp_up(scmd->device);
	case COMMAND_TERMINATED:
		return SUCCESS;
	case CHECK_CONDITION:
		return scsi_check_sense(scmd);
	case CONDITION_GOOD:
	case INTERMEDIATE_GOOD:
	case INTERMEDIATE_C_GOOD:
		/*
		 * who knows?  FIXME(eric)
		 */
		return SUCCESS;
	case RESERVATION_CONFLICT:
		if (scmd->cmnd[0] == TEST_UNIT_READY)
			/* it is a success, we probed the device and
			 * found it */
			return SUCCESS;
		/* otherwise, we failed to send the command */
		return FAILED;
	case QUEUE_FULL:
		scsi_handle_queue_full(scmd->device);
		/* fall through */
	case BUSY:
		return NEEDS_RETRY;
	default:
		return FAILED;
	}
	return FAILED;
}

/**
 * scsi_eh_done - Completion function for error handling.
 * @scmd:	Cmd that is done.
 */
static void scsi_eh_done(struct scsi_cmnd *scmd)
{
	struct completion *eh_action;

	SCSI_LOG_ERROR_RECOVERY(3, scmd_printk(KERN_INFO, scmd,
			"%s result: %x\n", __func__, scmd->result));

	eh_action = scmd->device->host->eh_action;
	if (eh_action)
		complete(eh_action);
}

/**
 * scsi_try_host_reset - ask host adapter to reset itself
 * @scmd:	SCSI cmd to send host reset.
 */
static int scsi_try_host_reset(struct scsi_cmnd *scmd)
{
	unsigned long flags;
	int rtn;
	struct Scsi_Host *host = scmd->device->host;
	struct scsi_host_template *hostt = host->hostt;

	SCSI_LOG_ERROR_RECOVERY(3,
		shost_printk(KERN_INFO, host, "Snd Host RST\n"));

	if (!hostt->eh_host_reset_handler)
		return FAILED;

	rtn = hostt->eh_host_reset_handler(scmd);

	if (rtn == SUCCESS) {
		if (!hostt->skip_settle_delay)
			ssleep(HOST_RESET_SETTLE_TIME);
		spin_lock_irqsave(host->host_lock, flags);
		scsi_report_bus_reset(host, scmd_channel(scmd));
		spin_unlock_irqrestore(host->host_lock, flags);
	}

	return rtn;
}

/**
 * scsi_try_bus_reset - ask host to perform a bus reset
 * @scmd:	SCSI cmd to send bus reset.
 */
static int scsi_try_bus_reset(struct scsi_cmnd *scmd)
{
	unsigned long flags;
	int rtn;
	struct Scsi_Host *host = scmd->device->host;
	struct scsi_host_template *hostt = host->hostt;

	SCSI_LOG_ERROR_RECOVERY(3, scmd_printk(KERN_INFO, scmd,
		"%s: Snd Bus RST\n", __func__));

	if (!hostt->eh_bus_reset_handler)
		return FAILED;

	rtn = hostt->eh_bus_reset_handler(scmd);

	if (rtn == SUCCESS) {
		if (!hostt->skip_settle_delay)
			ssleep(BUS_RESET_SETTLE_TIME);
		spin_lock_irqsave(host->host_lock, flags);
		scsi_report_bus_reset(host, scmd_channel(scmd));
		spin_unlock_irqrestore(host->host_lock, flags);
	}

	return rtn;
}

static void __scsi_report_device_reset(struct scsi_device *sdev, void *data)
{
	sdev->was_reset = 1;
	sdev->expecting_cc_ua = 1;
}

/**
 * scsi_try_target_reset - Ask host to perform a target reset
 * @scmd:	SCSI cmd used to send a target reset
 *
 * Notes:
 *    There is no timeout for this operation.  if this operation is
 *    unreliable for a given host, then the host itself needs to put a
 *    timer on it, and set the host back to a consistent state prior to
 *    returning.
 */
static int scsi_try_target_reset(struct scsi_cmnd *scmd)
{
	unsigned long flags;
	int rtn;
	struct Scsi_Host *host = scmd->device->host;
	struct scsi_host_template *hostt = host->hostt;

	if (!hostt->eh_target_reset_handler)
		return FAILED;

	rtn = hostt->eh_target_reset_handler(scmd);
	if (rtn == SUCCESS) {
		spin_lock_irqsave(host->host_lock, flags);
		__starget_for_each_device(scsi_target(scmd->device), NULL,
					  __scsi_report_device_reset);
		spin_unlock_irqrestore(host->host_lock, flags);
	}

	return rtn;
}

/**
 * scsi_try_bus_device_reset - Ask host to perform a BDR on a dev
 * @scmd:	SCSI cmd used to send BDR
 *
 * Notes:
 *    There is no timeout for this operation.  if this operation is
 *    unreliable for a given host, then the host itself needs to put a
 *    timer on it, and set the host back to a consistent state prior to
 *    returning.
 */
static int scsi_try_bus_device_reset(struct scsi_cmnd *scmd)
{
	int rtn;
	struct scsi_host_template *hostt = scmd->device->host->hostt;

	if (!hostt->eh_device_reset_handler)
		return FAILED;

	rtn = hostt->eh_device_reset_handler(scmd);
	if (rtn == SUCCESS)
		__scsi_report_device_reset(scmd->device, NULL);
	return rtn;
}

/**
 * scsi_try_to_abort_cmd - Ask host to abort a SCSI command
 * @hostt:	SCSI driver host template
 * @scmd:	SCSI cmd used to send a target reset
 *
 * Return value:
 *	SUCCESS, FAILED, or FAST_IO_FAIL
 *
 * Notes:
 *    SUCCESS does not necessarily indicate that the command
 *    has been aborted; it only indicates that the LLDDs
 *    has cleared all references to that command.
 *    LLDDs should return FAILED only if an abort was required
 *    but could not be executed. LLDDs should return FAST_IO_FAIL
 *    if the device is temporarily unavailable (eg due to a
 *    link down on FibreChannel)
 */
static int scsi_try_to_abort_cmd(struct scsi_host_template *hostt,
				 struct scsi_cmnd *scmd)
{
	if (!hostt->eh_abort_handler)
		return FAILED;

	return hostt->eh_abort_handler(scmd);
}

static void scsi_abort_eh_cmnd(struct scsi_cmnd *scmd)
{
	if (scsi_try_to_abort_cmd(scmd->device->host->hostt, scmd) != SUCCESS)
		if (scsi_try_bus_device_reset(scmd) != SUCCESS)
			if (scsi_try_target_reset(scmd) != SUCCESS)
				if (scsi_try_bus_reset(scmd) != SUCCESS)
					scsi_try_host_reset(scmd);
}

/**
 * scsi_eh_prep_cmnd  - Save a scsi command info as part of error recovery
 * @scmd:       SCSI command structure to hijack
 * @ses:        structure to save restore information
 * @cmnd:       CDB to send. Can be NULL if no new cmnd is needed
 * @cmnd_size:  size in bytes of @cmnd (must be <= BLK_MAX_CDB)
 * @sense_bytes: size of sense data to copy. or 0 (if != 0 @cmnd is ignored)
 *
 * This function is used to save a scsi command information before re-execution
 * as part of the error recovery process.  If @sense_bytes is 0 the command
 * sent must be one that does not transfer any data.  If @sense_bytes != 0
 * @cmnd is ignored and this functions sets up a REQUEST_SENSE command
 * and cmnd buffers to read @sense_bytes into @scmd->sense_buffer.
 */
void scsi_eh_prep_cmnd(struct scsi_cmnd *scmd, struct scsi_eh_save *ses,
			unsigned char *cmnd, int cmnd_size, unsigned sense_bytes)
{
	struct scsi_device *sdev = scmd->device;

	/*
	 * We need saved copies of a number of fields - this is because
	 * error handling may need to overwrite these with different values
	 * to run different commands, and once error handling is complete,
	 * we will need to restore these values prior to running the actual
	 * command.
	 */
	ses->cmd_len = scmd->cmd_len;
	ses->cmnd = scmd->cmnd;
	ses->data_direction = scmd->sc_data_direction;
	ses->sdb = scmd->sdb;
	ses->next_rq = scmd->request->next_rq;
	ses->result = scmd->result;
	ses->underflow = scmd->underflow;
	ses->prot_op = scmd->prot_op;

	scmd->prot_op = SCSI_PROT_NORMAL;
	scmd->eh_eflags = 0;
	scmd->cmnd = ses->eh_cmnd;
	memset(scmd->cmnd, 0, BLK_MAX_CDB);
	memset(&scmd->sdb, 0, sizeof(scmd->sdb));
	scmd->request->next_rq = NULL;
	scmd->result = 0;

	if (sense_bytes) {
		scmd->sdb.length = min_t(unsigned, SCSI_SENSE_BUFFERSIZE,
					 sense_bytes);
		sg_init_one(&ses->sense_sgl, scmd->sense_buffer,
			    scmd->sdb.length);
		scmd->sdb.table.sgl = &ses->sense_sgl;
		scmd->sc_data_direction = DMA_FROM_DEVICE;
		scmd->sdb.table.nents = scmd->sdb.table.orig_nents = 1;
		scmd->cmnd[0] = REQUEST_SENSE;
		scmd->cmnd[4] = scmd->sdb.length;
		scmd->cmd_len = COMMAND_SIZE(scmd->cmnd[0]);
	} else {
		scmd->sc_data_direction = DMA_NONE;
		if (cmnd) {
			BUG_ON(cmnd_size > BLK_MAX_CDB);
			memcpy(scmd->cmnd, cmnd, cmnd_size);
			scmd->cmd_len = COMMAND_SIZE(scmd->cmnd[0]);
		}
	}

	scmd->underflow = 0;

	if (sdev->scsi_level <= SCSI_2 && sdev->scsi_level != SCSI_UNKNOWN)
		scmd->cmnd[1] = (scmd->cmnd[1] & 0x1f) |
			(sdev->lun << 5 & 0xe0);

	/*
	 * Zero the sense buffer.  The scsi spec mandates that any
	 * untransferred sense data should be interpreted as being zero.
	 */
	memset(scmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
}
EXPORT_SYMBOL(scsi_eh_prep_cmnd);

/**
 * scsi_eh_restore_cmnd  - Restore a scsi command info as part of error recovery
 * @scmd:       SCSI command structure to restore
 * @ses:        saved information from a coresponding call to scsi_eh_prep_cmnd
 *
 * Undo any damage done by above scsi_eh_prep_cmnd().
 */
void scsi_eh_restore_cmnd(struct scsi_cmnd* scmd, struct scsi_eh_save *ses)
{
	/*
	 * Restore original data
	 */
	scmd->cmd_len = ses->cmd_len;
	scmd->cmnd = ses->cmnd;
	scmd->sc_data_direction = ses->data_direction;
	scmd->sdb = ses->sdb;
	scmd->request->next_rq = ses->next_rq;
	scmd->result = ses->result;
	scmd->underflow = ses->underflow;
	scmd->prot_op = ses->prot_op;
}
EXPORT_SYMBOL(scsi_eh_restore_cmnd);

/**
 * scsi_send_eh_cmnd  - submit a scsi command as part of error recovery
 * @scmd:       SCSI command structure to hijack
 * @cmnd:       CDB to send
 * @cmnd_size:  size in bytes of @cmnd
 * @timeout:    timeout for this request
 * @sense_bytes: size of sense data to copy or 0
 *
 * This function is used to send a scsi command down to a target device
 * as part of the error recovery process. See also scsi_eh_prep_cmnd() above.
 *
 * Return value:
 *    SUCCESS or FAILED or NEEDS_RETRY
 */
static int scsi_send_eh_cmnd(struct scsi_cmnd *scmd, unsigned char *cmnd,
			     int cmnd_size, int timeout, unsigned sense_bytes)
{
	struct scsi_device *sdev = scmd->device;
	struct Scsi_Host *shost = sdev->host;
	DECLARE_COMPLETION_ONSTACK(done);
	unsigned long timeleft = timeout;
	struct scsi_eh_save ses;
	const unsigned long stall_for = msecs_to_jiffies(100);
	int rtn;

retry:
	scsi_eh_prep_cmnd(scmd, &ses, cmnd, cmnd_size, sense_bytes);
	shost->eh_action = &done;

	scsi_log_send(scmd);
	scmd->scsi_done = scsi_eh_done;
	rtn = shost->hostt->queuecommand(shost, scmd);
	if (rtn) {
		if (timeleft > stall_for) {
			scsi_eh_restore_cmnd(scmd, &ses);
			timeleft -= stall_for;
			msleep(jiffies_to_msecs(stall_for));
			goto retry;
		}
		/* signal not to enter either branch of the if () below */
		timeleft = 0;
		rtn = FAILED;
	} else {
		timeleft = wait_for_completion_timeout(&done, timeout);
		rtn = SUCCESS;
	}

	shost->eh_action = NULL;

	scsi_log_completion(scmd, rtn);

	SCSI_LOG_ERROR_RECOVERY(3, scmd_printk(KERN_INFO, scmd,
			"%s timeleft: %ld\n",
			__func__, timeleft));

	/*
	 * If there is time left scsi_eh_done got called, and we will examine
	 * the actual status codes to see whether the command actually did
	 * complete normally, else if we have a zero return and no time left,
	 * the command must still be pending, so abort it and return FAILED.
	 * If we never actually managed to issue the command, because
	 * ->queuecommand() kept returning non zero, use the rtn = FAILED
	 * value above (so don't execute either branch of the if)
	 */
	if (timeleft) {
		rtn = scsi_eh_completed_normally(scmd);
		SCSI_LOG_ERROR_RECOVERY(3, scmd_printk(KERN_INFO, scmd,
			"%s: scsi_eh_completed_normally %x\n", __func__, rtn));

		switch (rtn) {
		case SUCCESS:
		case NEEDS_RETRY:
		case FAILED:
			break;
		case ADD_TO_MLQUEUE:
			rtn = NEEDS_RETRY;
			break;
		default:
			rtn = FAILED;
			break;
		}
	} else if (rtn != FAILED) {
		scsi_abort_eh_cmnd(scmd);
		rtn = FAILED;
	}

	scsi_eh_restore_cmnd(scmd, &ses);

	return rtn;
}

/**
 * scsi_request_sense - Request sense data from a particular target.
 * @scmd:	SCSI cmd for request sense.
 *
 * Notes:
 *    Some hosts automatically obtain this information, others require
 *    that we obtain it on our own. This function will *not* return until
 *    the command either times out, or it completes.
 */
static int scsi_request_sense(struct scsi_cmnd *scmd)
{
	return scsi_send_eh_cmnd(scmd, NULL, 0, scmd->device->eh_timeout, ~0);
}

static int scsi_eh_action(struct scsi_cmnd *scmd, int rtn)
{
	if (scmd->request->cmd_type != REQ_TYPE_BLOCK_PC) {
		struct scsi_driver *sdrv = scsi_cmd_to_driver(scmd);
		if (sdrv->eh_action)
			rtn = sdrv->eh_action(scmd, rtn);
	}
	return rtn;
}

/**
 * scsi_eh_finish_cmd - Handle a cmd that eh is finished with.
 * @scmd:	Original SCSI cmd that eh has finished.
 * @done_q:	Queue for processed commands.
 *
 * Notes:
 *    We don't want to use the normal command completion while we are are
 *    still handling errors - it may cause other commands to be queued,
 *    and that would disturb what we are doing.  Thus we really want to
 *    keep a list of pending commands for final completion, and once we
 *    are ready to leave error handling we handle completion for real.
 */
void scsi_eh_finish_cmd(struct scsi_cmnd *scmd, struct list_head *done_q)
{
	scmd->device->host->host_failed--;
	scmd->eh_eflags = 0;
	list_move_tail(&scmd->eh_entry, done_q);
}
EXPORT_SYMBOL(scsi_eh_finish_cmd);

/**
 * scsi_eh_get_sense - Get device sense data.
 * @work_q:	Queue of commands to process.
 * @done_q:	Queue of processed commands.
 *
 * Description:
 *    See if we need to request sense information.  if so, then get it
 *    now, so we have a better idea of what to do.
 *
 * Notes:
 *    This has the unfortunate side effect that if a shost adapter does
 *    not automatically request sense information, we end up shutting
 *    it down before we request it.
 *
 *    All drivers should request sense information internally these days,
 *    so for now all I have to say is tough noogies if you end up in here.
 *
 *    XXX: Long term this code should go away, but that needs an audit of
 *         all LLDDs first.
 */
int scsi_eh_get_sense(struct list_head *work_q,
		      struct list_head *done_q)
{
	struct scsi_cmnd *scmd, *next;
	struct Scsi_Host *shost;
	int rtn;

	/*
	 * If SCSI_EH_ABORT_SCHEDULED has been set, it is timeout IO,
	 * should not get sense.
	 */
	list_for_each_entry_safe(scmd, next, work_q, eh_entry) {
		if ((scmd->eh_eflags & SCSI_EH_CANCEL_CMD) ||
		    (scmd->eh_eflags & SCSI_EH_ABORT_SCHEDULED) ||
		    SCSI_SENSE_VALID(scmd))
			continue;

		shost = scmd->device->host;
		if (scsi_host_eh_past_deadline(shost)) {
			SCSI_LOG_ERROR_RECOVERY(3,
				scmd_printk(KERN_INFO, scmd,
					    "%s: skip request sense, past eh deadline\n",
					     current->comm));
			break;
		}
		if (status_byte(scmd->result) != CHECK_CONDITION)
			/*
			 * don't request sense if there's no check condition
			 * status because the error we're processing isn't one
			 * that has a sense code (and some devices get
			 * confused by sense requests out of the blue)
			 */
			continue;

		SCSI_LOG_ERROR_RECOVERY(2, scmd_printk(KERN_INFO, scmd,
						  "%s: requesting sense\n",
						  current->comm));
		rtn = scsi_request_sense(scmd);
		if (rtn != SUCCESS)
			continue;

		SCSI_LOG_ERROR_RECOVERY(3, scmd_printk(KERN_INFO, scmd,
			"sense requested, result %x\n", scmd->result));
		SCSI_LOG_ERROR_RECOVERY(3, scsi_print_sense(scmd));

		rtn = scsi_decide_disposition(scmd);

		/*
		 * if the result was normal, then just pass it along to the
		 * upper level.
		 */
		if (rtn == SUCCESS)
			/* we don't want this command reissued, just
			 * finished with the sense data, so set
			 * retries to the max allowed to ensure it
			 * won't get reissued */
			scmd->retries = scmd->allowed;
		else if (rtn != NEEDS_RETRY)
			continue;

		scsi_eh_finish_cmd(scmd, done_q);
	}

	return list_empty(work_q);
}
EXPORT_SYMBOL_GPL(scsi_eh_get_sense);

/**
 * scsi_eh_tur - Send TUR to device.
 * @scmd:	&scsi_cmnd to send TUR
 *
 * Return value:
 *    0 - Device is ready. 1 - Device NOT ready.
 */
static int scsi_eh_tur(struct scsi_cmnd *scmd)
{
	static unsigned char tur_command[6] = {TEST_UNIT_READY, 0, 0, 0, 0, 0};
	int retry_cnt = 1, rtn;

retry_tur:
	rtn = scsi_send_eh_cmnd(scmd, tur_command, 6,
				scmd->device->eh_timeout, 0);

	SCSI_LOG_ERROR_RECOVERY(3, scmd_printk(KERN_INFO, scmd,
		"%s return: %x\n", __func__, rtn));

	switch (rtn) {
	case NEEDS_RETRY:
		if (retry_cnt--)
			goto retry_tur;
		/*FALLTHRU*/
	case SUCCESS:
		return 0;
	default:
		return 1;
	}
}

/**
 * scsi_eh_test_devices - check if devices are responding from error recovery.
 * @cmd_list:	scsi commands in error recovery.
 * @work_q:	queue for commands which still need more error recovery
 * @done_q:	queue for commands which are finished
 * @try_stu:	boolean on if a STU command should be tried in addition to TUR.
 *
 * Decription:
 *    Tests if devices are in a working state.  Commands to devices now in
 *    a working state are sent to the done_q while commands to devices which
 *    are still failing to respond are returned to the work_q for more
 *    processing.
 **/
static int scsi_eh_test_devices(struct list_head *cmd_list,
				struct list_head *work_q,
				struct list_head *done_q, int try_stu)
{
	struct scsi_cmnd *scmd, *next;
	struct scsi_device *sdev;
	int finish_cmds;

	while (!list_empty(cmd_list)) {
		scmd = list_entry(cmd_list->next, struct scsi_cmnd, eh_entry);
		sdev = scmd->device;

		if (!try_stu) {
			if (scsi_host_eh_past_deadline(sdev->host)) {
				/* Push items back onto work_q */
				list_splice_init(cmd_list, work_q);
				SCSI_LOG_ERROR_RECOVERY(3,
					sdev_printk(KERN_INFO, sdev,
						    "%s: skip test device, past eh deadline",
						    current->comm));
				break;
			}
		}

		finish_cmds = !scsi_device_online(scmd->device) ||
			(try_stu && !scsi_eh_try_stu(scmd) &&
			 !scsi_eh_tur(scmd)) ||
			!scsi_eh_tur(scmd);

		list_for_each_entry_safe(scmd, next, cmd_list, eh_entry)
			if (scmd->device == sdev) {
				if (finish_cmds &&
				    (try_stu ||
				     scsi_eh_action(scmd, SUCCESS) == SUCCESS))
					scsi_eh_finish_cmd(scmd, done_q);
				else
					list_move_tail(&scmd->eh_entry, work_q);
			}
	}
	return list_empty(work_q);
}


/**
 * scsi_eh_abort_cmds - abort pending commands.
 * @work_q:	&list_head for pending commands.
 * @done_q:	&list_head for processed commands.
 *
 * Decription:
 *    Try and see whether or not it makes sense to try and abort the
 *    running command.  This only works out to be the case if we have one
 *    command that has timed out.  If the command simply failed, it makes
 *    no sense to try and abort the command, since as far as the shost
 *    adapter is concerned, it isn't running.
 */
static int scsi_eh_abort_cmds(struct list_head *work_q,
			      struct list_head *done_q)
{
	struct scsi_cmnd *scmd, *next;
	LIST_HEAD(check_list);
	int rtn;
	struct Scsi_Host *shost;

	list_for_each_entry_safe(scmd, next, work_q, eh_entry) {
		if (!(scmd->eh_eflags & SCSI_EH_CANCEL_CMD))
			continue;
		shost = scmd->device->host;
		if (scsi_host_eh_past_deadline(shost)) {
			list_splice_init(&check_list, work_q);
			SCSI_LOG_ERROR_RECOVERY(3,
				scmd_printk(KERN_INFO, scmd,
					    "%s: skip aborting cmd, past eh deadline\n",
					    current->comm));
			return list_empty(work_q);
		}
		SCSI_LOG_ERROR_RECOVERY(3,
			scmd_printk(KERN_INFO, scmd,
				     "%s: aborting cmd\n", current->comm));
		rtn = scsi_try_to_abort_cmd(shost->hostt, scmd);
		if (rtn == FAILED) {
			SCSI_LOG_ERROR_RECOVERY(3,
				scmd_printk(KERN_INFO, scmd,
					    "%s: aborting cmd failed\n",
					     current->comm));
			list_splice_init(&check_list, work_q);
			return list_empty(work_q);
		}
		scmd->eh_eflags &= ~SCSI_EH_CANCEL_CMD;
		if (rtn == FAST_IO_FAIL)
			scsi_eh_finish_cmd(scmd, done_q);
		else
			list_move_tail(&scmd->eh_entry, &check_list);
	}

	return scsi_eh_test_devices(&check_list, work_q, done_q, 0);
}

/**
 * scsi_eh_try_stu - Send START_UNIT to device.
 * @scmd:	&scsi_cmnd to send START_UNIT
 *
 * Return value:
 *    0 - Device is ready. 1 - Device NOT ready.
 */
static int scsi_eh_try_stu(struct scsi_cmnd *scmd)
{
	static unsigned char stu_command[6] = {START_STOP, 0, 0, 0, 1, 0};

	if (scmd->device->allow_restart) {
		int i, rtn = NEEDS_RETRY;

		for (i = 0; rtn == NEEDS_RETRY && i < 2; i++)
			rtn = scsi_send_eh_cmnd(scmd, stu_command, 6, scmd->device->request_queue->rq_timeout, 0);

		if (rtn == SUCCESS)
			return 0;
	}

	return 1;
}

 /**
 * scsi_eh_stu - send START_UNIT if needed
 * @shost:	&scsi host being recovered.
 * @work_q:	&list_head for pending commands.
 * @done_q:	&list_head for processed commands.
 *
 * Notes:
 *    If commands are failing due to not ready, initializing command required,
 *	try revalidating the device, which will end up sending a start unit.
 */
static int scsi_eh_stu(struct Scsi_Host *shost,
			      struct list_head *work_q,
			      struct list_head *done_q)
{
	struct scsi_cmnd *scmd, *stu_scmd, *next;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, shost) {
		if (scsi_host_eh_past_deadline(shost)) {
			SCSI_LOG_ERROR_RECOVERY(3,
				sdev_printk(KERN_INFO, sdev,
					    "%s: skip START_UNIT, past eh deadline\n",
					    current->comm));
			break;
		}
		stu_scmd = NULL;
		list_for_each_entry(scmd, work_q, eh_entry)
			if (scmd->device == sdev && SCSI_SENSE_VALID(scmd) &&
			    scsi_check_sense(scmd) == FAILED ) {
				stu_scmd = scmd;
				break;
			}

		if (!stu_scmd)
			continue;

		SCSI_LOG_ERROR_RECOVERY(3,
			sdev_printk(KERN_INFO, sdev,
				     "%s: Sending START_UNIT\n",
				    current->comm));

		if (!scsi_eh_try_stu(stu_scmd)) {
			if (!scsi_device_online(sdev) ||
			    !scsi_eh_tur(stu_scmd)) {
				list_for_each_entry_safe(scmd, next,
							  work_q, eh_entry) {
					if (scmd->device == sdev &&
					    scsi_eh_action(scmd, SUCCESS) == SUCCESS)
						scsi_eh_finish_cmd(scmd, done_q);
				}
			}
		} else {
			SCSI_LOG_ERROR_RECOVERY(3,
				sdev_printk(KERN_INFO, sdev,
					    "%s: START_UNIT failed\n",
					    current->comm));
		}
	}

	return list_empty(work_q);
}


/**
 * scsi_eh_bus_device_reset - send bdr if needed
 * @shost:	scsi host being recovered.
 * @work_q:	&list_head for pending commands.
 * @done_q:	&list_head for processed commands.
 *
 * Notes:
 *    Try a bus device reset.  Still, look to see whether we have multiple
 *    devices that are jammed or not - if we have multiple devices, it
 *    makes no sense to try bus_device_reset - we really would need to try
 *    a bus_reset instead.
 */
static int scsi_eh_bus_device_reset(struct Scsi_Host *shost,
				    struct list_head *work_q,
				    struct list_head *done_q)
{
	struct scsi_cmnd *scmd, *bdr_scmd, *next;
	struct scsi_device *sdev;
	int rtn;

	shost_for_each_device(sdev, shost) {
		if (scsi_host_eh_past_deadline(shost)) {
			SCSI_LOG_ERROR_RECOVERY(3,
				sdev_printk(KERN_INFO, sdev,
					    "%s: skip BDR, past eh deadline\n",
					     current->comm));
			break;
		}
		bdr_scmd = NULL;
		list_for_each_entry(scmd, work_q, eh_entry)
			if (scmd->device == sdev) {
				bdr_scmd = scmd;
				break;
			}

		if (!bdr_scmd)
			continue;

		SCSI_LOG_ERROR_RECOVERY(3,
			sdev_printk(KERN_INFO, sdev,
				     "%s: Sending BDR\n", current->comm));
		rtn = scsi_try_bus_device_reset(bdr_scmd);
		if (rtn == SUCCESS || rtn == FAST_IO_FAIL) {
			if (!scsi_device_online(sdev) ||
			    rtn == FAST_IO_FAIL ||
			    !scsi_eh_tur(bdr_scmd)) {
				list_for_each_entry_safe(scmd, next,
							 work_q, eh_entry) {
					if (scmd->device == sdev &&
					    scsi_eh_action(scmd, rtn) != FAILED)
						scsi_eh_finish_cmd(scmd,
								   done_q);
				}
			}
		} else {
			SCSI_LOG_ERROR_RECOVERY(3,
				sdev_printk(KERN_INFO, sdev,
					    "%s: BDR failed\n", current->comm));
		}
	}

	return list_empty(work_q);
}

/**
 * scsi_eh_target_reset - send target reset if needed
 * @shost:	scsi host being recovered.
 * @work_q:	&list_head for pending commands.
 * @done_q:	&list_head for processed commands.
 *
 * Notes:
 *    Try a target reset.
 */
static int scsi_eh_target_reset(struct Scsi_Host *shost,
				struct list_head *work_q,
				struct list_head *done_q)
{
	LIST_HEAD(tmp_list);
	LIST_HEAD(check_list);

	list_splice_init(work_q, &tmp_list);

	while (!list_empty(&tmp_list)) {
		struct scsi_cmnd *next, *scmd;
		int rtn;
		unsigned int id;

		if (scsi_host_eh_past_deadline(shost)) {
			/* push back on work queue for further processing */
			list_splice_init(&check_list, work_q);
			list_splice_init(&tmp_list, work_q);
			SCSI_LOG_ERROR_RECOVERY(3,
				shost_printk(KERN_INFO, shost,
					    "%s: Skip target reset, past eh deadline\n",
					     current->comm));
			return list_empty(work_q);
		}

		scmd = list_entry(tmp_list.next, struct scsi_cmnd, eh_entry);
		id = scmd_id(scmd);

		SCSI_LOG_ERROR_RECOVERY(3,
			shost_printk(KERN_INFO, shost,
				     "%s: Sending target reset to target %d\n",
				     current->comm, id));
		rtn = scsi_try_target_reset(scmd);
		if (rtn != SUCCESS && rtn != FAST_IO_FAIL)
			SCSI_LOG_ERROR_RECOVERY(3,
				shost_printk(KERN_INFO, shost,
					     "%s: Target reset failed"
					     " target: %d\n",
					     current->comm, id));
		list_for_each_entry_safe(scmd, next, &tmp_list, eh_entry) {
			if (scmd_id(scmd) != id)
				continue;

			if (rtn == SUCCESS)
				list_move_tail(&scmd->eh_entry, &check_list);
			else if (rtn == FAST_IO_FAIL)
				scsi_eh_finish_cmd(scmd, done_q);
			else
				/* push back on work queue for further processing */
				list_move(&scmd->eh_entry, work_q);
		}
	}

	return scsi_eh_test_devices(&check_list, work_q, done_q, 0);
}

/**
 * scsi_eh_bus_reset - send a bus reset
 * @shost:	&scsi host being recovered.
 * @work_q:	&list_head for pending commands.
 * @done_q:	&list_head for processed commands.
 */
static int scsi_eh_bus_reset(struct Scsi_Host *shost,
			     struct list_head *work_q,
			     struct list_head *done_q)
{
	struct scsi_cmnd *scmd, *chan_scmd, *next;
	LIST_HEAD(check_list);
	unsigned int channel;
	int rtn;

	/*
	 * we really want to loop over the various channels, and do this on
	 * a channel by channel basis.  we should also check to see if any
	 * of the failed commands are on soft_reset devices, and if so, skip
	 * the reset.
	 */

	for (channel = 0; channel <= shost->max_channel; channel++) {
		if (scsi_host_eh_past_deadline(shost)) {
			list_splice_init(&check_list, work_q);
			SCSI_LOG_ERROR_RECOVERY(3,
				shost_printk(KERN_INFO, shost,
					    "%s: skip BRST, past eh deadline\n",
					     current->comm));
			return list_empty(work_q);
		}

		chan_scmd = NULL;
		list_for_each_entry(scmd, work_q, eh_entry) {
			if (channel == scmd_channel(scmd)) {
				chan_scmd = scmd;
				break;
				/*
				 * FIXME add back in some support for
				 * soft_reset devices.
				 */
			}
		}

		if (!chan_scmd)
			continue;
		SCSI_LOG_ERROR_RECOVERY(3,
			shost_printk(KERN_INFO, shost,
				     "%s: Sending BRST chan: %d\n",
				     current->comm, channel));
		rtn = scsi_try_bus_reset(chan_scmd);
		if (rtn == SUCCESS || rtn == FAST_IO_FAIL) {
			list_for_each_entry_safe(scmd, next, work_q, eh_entry) {
				if (channel == scmd_channel(scmd)) {
					if (rtn == FAST_IO_FAIL)
						scsi_eh_finish_cmd(scmd,
								   done_q);
					else
						list_move_tail(&scmd->eh_entry,
							       &check_list);
				}
			}
		} else {
			SCSI_LOG_ERROR_RECOVERY(3,
				shost_printk(KERN_INFO, shost,
					     "%s: BRST failed chan: %d\n",
					     current->comm, channel));
		}
	}
	return scsi_eh_test_devices(&check_list, work_q, done_q, 0);
}

/**
 * scsi_eh_host_reset - send a host reset
 * @shost:	host to be reset.
 * @work_q:	&list_head for pending commands.
 * @done_q:	&list_head for processed commands.
 */
static int scsi_eh_host_reset(struct Scsi_Host *shost,
			      struct list_head *work_q,
			      struct list_head *done_q)
{
	struct scsi_cmnd *scmd, *next;
	LIST_HEAD(check_list);
	int rtn;

	if (!list_empty(work_q)) {
		scmd = list_entry(work_q->next,
				  struct scsi_cmnd, eh_entry);

		SCSI_LOG_ERROR_RECOVERY(3,
			shost_printk(KERN_INFO, shost,
				     "%s: Sending HRST\n",
				     current->comm));

		rtn = scsi_try_host_reset(scmd);
		if (rtn == SUCCESS) {
			list_splice_init(work_q, &check_list);
		} else if (rtn == FAST_IO_FAIL) {
			list_for_each_entry_safe(scmd, next, work_q, eh_entry) {
					scsi_eh_finish_cmd(scmd, done_q);
			}
		} else {
			SCSI_LOG_ERROR_RECOVERY(3,
				shost_printk(KERN_INFO, shost,
					     "%s: HRST failed\n",
					     current->comm));
		}
	}
	return scsi_eh_test_devices(&check_list, work_q, done_q, 1);
}

/**
 * scsi_eh_offline_sdevs - offline scsi devices that fail to recover
 * @work_q:	&list_head for pending commands.
 * @done_q:	&list_head for processed commands.
 */
static void scsi_eh_offline_sdevs(struct list_head *work_q,
				  struct list_head *done_q)
{
	struct scsi_cmnd *scmd, *next;

	list_for_each_entry_safe(scmd, next, work_q, eh_entry) {
		sdev_printk(KERN_INFO, scmd->device, "Device offlined - "
			    "not ready after error recovery\n");
		scsi_device_set_state(scmd->device, SDEV_OFFLINE);
		if (scmd->eh_eflags & SCSI_EH_CANCEL_CMD) {
			/*
			 * FIXME: Handle lost cmds.
			 */
		}
		scsi_eh_finish_cmd(scmd, done_q);
	}
	return;
}

/**
 * scsi_noretry_cmd - determine if command should be failed fast
 * @scmd:	SCSI cmd to examine.
 */
int scsi_noretry_cmd(struct scsi_cmnd *scmd)
{
	switch (host_byte(scmd->result)) {
	case DID_OK:
		break;
	case DID_TIME_OUT:
		goto check_type;
	case DID_BUS_BUSY:
		return (scmd->request->cmd_flags & REQ_FAILFAST_TRANSPORT);
	case DID_PARITY:
		return (scmd->request->cmd_flags & REQ_FAILFAST_DEV);
	case DID_ERROR:
		if (msg_byte(scmd->result) == COMMAND_COMPLETE &&
		    status_byte(scmd->result) == RESERVATION_CONFLICT)
			return 0;
		/* fall through */
	case DID_SOFT_ERROR:
		return (scmd->request->cmd_flags & REQ_FAILFAST_DRIVER);
	}

	if (status_byte(scmd->result) != CHECK_CONDITION)
		return 0;

check_type:
	/*
	 * assume caller has checked sense and determined
	 * the check condition was retryable.
	 */
	if (scmd->request->cmd_flags & REQ_FAILFAST_DEV ||
	    scmd->request->cmd_type == REQ_TYPE_BLOCK_PC)
		return 1;
	else
		return 0;
}

/**
 * scsi_decide_disposition - Disposition a cmd on return from LLD.
 * @scmd:	SCSI cmd to examine.
 *
 * Notes:
 *    This is *only* called when we are examining the status after sending
 *    out the actual data command.  any commands that are queued for error
 *    recovery (e.g. test_unit_ready) do *not* come through here.
 *
 *    When this routine returns failed, it means the error handler thread
 *    is woken.  In cases where the error code indicates an error that
 *    doesn't require the error handler read (i.e. we don't need to
 *    abort/reset), this function should return SUCCESS.
 */
int scsi_decide_disposition(struct scsi_cmnd *scmd)
{
	int rtn;

	/*
	 * if the device is offline, then we clearly just pass the result back
	 * up to the top level.
	 */
	if (!scsi_device_online(scmd->device)) {
		SCSI_LOG_ERROR_RECOVERY(5, scmd_printk(KERN_INFO, scmd,
			"%s: device offline - report as SUCCESS\n", __func__));
		return SUCCESS;
	}

	/*
	 * first check the host byte, to see if there is anything in there
	 * that would indicate what we need to do.
	 */
	switch (host_byte(scmd->result)) {
	case DID_PASSTHROUGH:
		/*
		 * no matter what, pass this through to the upper layer.
		 * nuke this special code so that it looks like we are saying
		 * did_ok.
		 */
		scmd->result &= 0xff00ffff;
		return SUCCESS;
	case DID_OK:
		/*
		 * looks good.  drop through, and check the next byte.
		 */
		break;
	case DID_ABORT:
		if (scmd->eh_eflags & SCSI_EH_ABORT_SCHEDULED) {
			set_host_byte(scmd, DID_TIME_OUT);
			return SUCCESS;
		}
	case DID_NO_CONNECT:
	case DID_BAD_TARGET:
		/*
		 * note - this means that we just report the status back
		 * to the top level driver, not that we actually think
		 * that it indicates SUCCESS.
		 */
		return SUCCESS;
		/*
		 * when the low level driver returns did_soft_error,
		 * it is responsible for keeping an internal retry counter
		 * in order to avoid endless loops (db)
		 *
		 * actually this is a bug in this function here.  we should
		 * be mindful of the maximum number of retries specified
		 * and not get stuck in a loop.
		 */
	case DID_SOFT_ERROR:
		goto maybe_retry;
	case DID_IMM_RETRY:
		return NEEDS_RETRY;

	case DID_REQUEUE:
		return ADD_TO_MLQUEUE;
	case DID_TRANSPORT_DISRUPTED:
		/*
		 * LLD/transport was disrupted during processing of the IO.
		 * The transport class is now blocked/blocking,
		 * and the transport will decide what to do with the IO
		 * based on its timers and recovery capablilities if
		 * there are enough retries.
		 */
		goto maybe_retry;
	case DID_TRANSPORT_FAILFAST:
		/*
		 * The transport decided to failfast the IO (most likely
		 * the fast io fail tmo fired), so send IO directly upwards.
		 */
		return SUCCESS;
	case DID_ERROR:
		if (msg_byte(scmd->result) == COMMAND_COMPLETE &&
		    status_byte(scmd->result) == RESERVATION_CONFLICT)
			/*
			 * execute reservation conflict processing code
			 * lower down
			 */
			break;
		/* fallthrough */
	case DID_BUS_BUSY:
	case DID_PARITY:
		goto maybe_retry;
	case DID_TIME_OUT:
		/*
		 * when we scan the bus, we get timeout messages for
		 * these commands if there is no device available.
		 * other hosts report did_no_connect for the same thing.
		 */
		if ((scmd->cmnd[0] == TEST_UNIT_READY ||
		     scmd->cmnd[0] == INQUIRY)) {
			return SUCCESS;
		} else {
			return FAILED;
		}
	case DID_RESET:
		return SUCCESS;
	default:
		return FAILED;
	}

	/*
	 * next, check the message byte.
	 */
	if (msg_byte(scmd->result) != COMMAND_COMPLETE)
		return FAILED;

	/*
	 * check the status byte to see if this indicates anything special.
	 */
	switch (status_byte(scmd->result)) {
	case QUEUE_FULL:
		scsi_handle_queue_full(scmd->device);
		/*
		 * the case of trying to send too many commands to a
		 * tagged queueing device.
		 */
	case BUSY:
		/*
		 * device can't talk to us at the moment.  Should only
		 * occur (SAM-3) when the task queue is empty, so will cause
		 * the empty queue handling to trigger a stall in the
		 * device.
		 */
		return ADD_TO_MLQUEUE;
	case GOOD:
		if (scmd->cmnd[0] == REPORT_LUNS)
			scmd->device->sdev_target->expecting_lun_change = 0;
		scsi_handle_queue_ramp_up(scmd->device);
	case COMMAND_TERMINATED:
		return SUCCESS;
	case TASK_ABORTED:
		goto maybe_retry;
	case CHECK_CONDITION:
		rtn = scsi_check_sense(scmd);
		if (rtn == NEEDS_RETRY)
			goto maybe_retry;
		/* if rtn == FAILED, we have no sense information;
		 * returning FAILED will wake the error handler thread
		 * to collect the sense and redo the decide
		 * disposition */
		return rtn;
	case CONDITION_GOOD:
	case INTERMEDIATE_GOOD:
	case INTERMEDIATE_C_GOOD:
	case ACA_ACTIVE:
		/*
		 * who knows?  FIXME(eric)
		 */
		return SUCCESS;

	case RESERVATION_CONFLICT:
		sdev_printk(KERN_INFO, scmd->device,
			    "reservation conflict\n");
		set_host_byte(scmd, DID_NEXUS_FAILURE);
		return SUCCESS; /* causes immediate i/o error */
	default:
		return FAILED;
	}
	return FAILED;

      maybe_retry:

	/* we requeue for retry because the error was retryable, and
	 * the request was not marked fast fail.  Note that above,
	 * even if the request is marked fast fail, we still requeue
	 * for queue congestion conditions (QUEUE_FULL or BUSY) */
	if ((++scmd->retries) <= scmd->allowed
	    && !scsi_noretry_cmd(scmd)) {
		return NEEDS_RETRY;
	} else {
		/*
		 * no more retries - report this one back to upper level.
		 */
		return SUCCESS;
	}
}

static void eh_lock_door_done(struct request *req, int uptodate)
{
	__blk_put_request(req->q, req);
}

/**
 * scsi_eh_lock_door - Prevent medium removal for the specified device
 * @sdev:	SCSI device to prevent medium removal
 *
 * Locking:
 * 	We must be called from process context.
 *
 * Notes:
 * 	We queue up an asynchronous "ALLOW MEDIUM REMOVAL" request on the
 * 	head of the devices request queue, and continue.
 */
static void scsi_eh_lock_door(struct scsi_device *sdev)
{
	struct request *req;

	/*
	 * blk_get_request with GFP_KERNEL (__GFP_RECLAIM) sleeps until a
	 * request becomes available
	 */
	req = blk_get_request(sdev->request_queue, READ, GFP_KERNEL);
	if (IS_ERR(req))
		return;

	blk_rq_set_block_pc(req);

	req->cmd[0] = ALLOW_MEDIUM_REMOVAL;
	req->cmd[1] = 0;
	req->cmd[2] = 0;
	req->cmd[3] = 0;
	req->cmd[4] = SCSI_REMOVAL_PREVENT;
	req->cmd[5] = 0;

	req->cmd_len = COMMAND_SIZE(req->cmd[0]);

	req->cmd_flags |= REQ_QUIET;
	req->timeout = 10 * HZ;
	req->retries = 5;

	blk_execute_rq_nowait(req->q, NULL, req, 1, eh_lock_door_done);
}

/**
 * scsi_restart_operations - restart io operations to the specified host.
 * @shost:	Host we are restarting.
 *
 * Notes:
 *    When we entered the error handler, we blocked all further i/o to
 *    this device.  we need to 'reverse' this process.
 */
static void scsi_restart_operations(struct Scsi_Host *shost)
{
	struct scsi_device *sdev;
	unsigned long flags;

	/*
	 * If the door was locked, we need to insert a door lock request
	 * onto the head of the SCSI request queue for the device.  There
	 * is no point trying to lock the door of an off-line device.
	 */
	shost_for_each_device(sdev, shost) {
		if (scsi_device_online(sdev) && sdev->was_reset && sdev->locked) {
			scsi_eh_lock_door(sdev);
			sdev->was_reset = 0;
		}
	}

	/*
	 * next free up anything directly waiting upon the host.  this
	 * will be requests for character device operations, and also for
	 * ioctls to queued block devices.
	 */
	SCSI_LOG_ERROR_RECOVERY(3,
		shost_printk(KERN_INFO, shost, "waking up host to restart\n"));

	spin_lock_irqsave(shost->host_lock, flags);
	if (scsi_host_set_state(shost, SHOST_RUNNING))
		if (scsi_host_set_state(shost, SHOST_CANCEL))
			BUG_ON(scsi_host_set_state(shost, SHOST_DEL));
	spin_unlock_irqrestore(shost->host_lock, flags);

	wake_up(&shost->host_wait);

	/*
	 * finally we need to re-initiate requests that may be pending.  we will
	 * have had everything blocked while error handling is taking place, and
	 * now that error recovery is done, we will need to ensure that these
	 * requests are started.
	 */
	scsi_run_host_queues(shost);

	/*
	 * if eh is active and host_eh_scheduled is pending we need to re-run
	 * recovery.  we do this check after scsi_run_host_queues() to allow
	 * everything pent up since the last eh run a chance to make forward
	 * progress before we sync again.  Either we'll immediately re-run
	 * recovery or scsi_device_unbusy() will wake us again when these
	 * pending commands complete.
	 */
	spin_lock_irqsave(shost->host_lock, flags);
	if (shost->host_eh_scheduled)
		if (scsi_host_set_state(shost, SHOST_RECOVERY))
			WARN_ON(scsi_host_set_state(shost, SHOST_CANCEL_RECOVERY));
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/**
 * scsi_eh_ready_devs - check device ready state and recover if not.
 * @shost:	host to be recovered.
 * @work_q:	&list_head for pending commands.
 * @done_q:	&list_head for processed commands.
 */
void scsi_eh_ready_devs(struct Scsi_Host *shost,
			struct list_head *work_q,
			struct list_head *done_q)
{
	if (!scsi_eh_stu(shost, work_q, done_q))
		if (!scsi_eh_bus_device_reset(shost, work_q, done_q))
			if (!scsi_eh_target_reset(shost, work_q, done_q))
				if (!scsi_eh_bus_reset(shost, work_q, done_q))
					if (!scsi_eh_host_reset(shost, work_q, done_q))
						scsi_eh_offline_sdevs(work_q,
								      done_q);
}
EXPORT_SYMBOL_GPL(scsi_eh_ready_devs);

/**
 * scsi_eh_flush_done_q - finish processed commands or retry them.
 * @done_q:	list_head of processed commands.
 */
void scsi_eh_flush_done_q(struct list_head *done_q)
{
	struct scsi_cmnd *scmd, *next;

	list_for_each_entry_safe(scmd, next, done_q, eh_entry) {
		list_del_init(&scmd->eh_entry);
		if (scsi_device_online(scmd->device) &&
		    !scsi_noretry_cmd(scmd) &&
		    (++scmd->retries <= scmd->allowed)) {
			SCSI_LOG_ERROR_RECOVERY(3,
				scmd_printk(KERN_INFO, scmd,
					     "%s: flush retry cmd\n",
					     current->comm));
				scsi_queue_insert(scmd, SCSI_MLQUEUE_EH_RETRY);
		} else {
			/*
			 * If just we got sense for the device (called
			 * scsi_eh_get_sense), scmd->result is already
			 * set, do not set DRIVER_TIMEOUT.
			 */
			if (!scmd->result)
				scmd->result |= (DRIVER_TIMEOUT << 24);
			SCSI_LOG_ERROR_RECOVERY(3,
				scmd_printk(KERN_INFO, scmd,
					     "%s: flush finish cmd\n",
					     current->comm));
			scsi_finish_command(scmd);
		}
	}
}
EXPORT_SYMBOL(scsi_eh_flush_done_q);

/**
 * scsi_unjam_host - Attempt to fix a host which has a cmd that failed.
 * @shost:	Host to unjam.
 *
 * Notes:
 *    When we come in here, we *know* that all commands on the bus have
 *    either completed, failed or timed out.  we also know that no further
 *    commands are being sent to the host, so things are relatively quiet
 *    and we have freedom to fiddle with things as we wish.
 *
 *    This is only the *default* implementation.  it is possible for
 *    individual drivers to supply their own version of this function, and
 *    if the maintainer wishes to do this, it is strongly suggested that
 *    this function be taken as a template and modified.  this function
 *    was designed to correctly handle problems for about 95% of the
 *    different cases out there, and it should always provide at least a
 *    reasonable amount of error recovery.
 *
 *    Any command marked 'failed' or 'timeout' must eventually have
 *    scsi_finish_cmd() called for it.  we do all of the retry stuff
 *    here, so when we restart the host after we return it should have an
 *    empty queue.
 */
static void scsi_unjam_host(struct Scsi_Host *shost)
{
	unsigned long flags;
	LIST_HEAD(eh_work_q);
	LIST_HEAD(eh_done_q);

	spin_lock_irqsave(shost->host_lock, flags);
	list_splice_init(&shost->eh_cmd_q, &eh_work_q);
	spin_unlock_irqrestore(shost->host_lock, flags);

	SCSI_LOG_ERROR_RECOVERY(1, scsi_eh_prt_fail_stats(shost, &eh_work_q));

	if (!scsi_eh_get_sense(&eh_work_q, &eh_done_q))
		if (!scsi_eh_abort_cmds(&eh_work_q, &eh_done_q))
			scsi_eh_ready_devs(shost, &eh_work_q, &eh_done_q);

	spin_lock_irqsave(shost->host_lock, flags);
	if (shost->eh_deadline != -1)
		shost->last_reset = 0;
	spin_unlock_irqrestore(shost->host_lock, flags);
	scsi_eh_flush_done_q(&eh_done_q);
}

/**
 * scsi_error_handler - SCSI error handler thread
 * @data:	Host for which we are running.
 *
 * Notes:
 *    This is the main error handling loop.  This is run as a kernel thread
 *    for every SCSI host and handles all error handling activity.
 */
int scsi_error_handler(void *data)
{
	struct Scsi_Host *shost = data;

	/*
	 * We use TASK_INTERRUPTIBLE so that the thread is not
	 * counted against the load average as a running process.
	 * We never actually get interrupted because kthread_run
	 * disables signal delivery for the created thread.
	 */
	while (true) {
		/*
		 * The sequence in kthread_stop() sets the stop flag first
		 * then wakes the process.  To avoid missed wakeups, the task
		 * should always be in a non running state before the stop
		 * flag is checked
		 */
		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop())
			break;

		if ((shost->host_failed == 0 && shost->host_eh_scheduled == 0) ||
		    shost->host_failed != atomic_read(&shost->host_busy)) {
			SCSI_LOG_ERROR_RECOVERY(1,
				shost_printk(KERN_INFO, shost,
					     "scsi_eh_%d: sleeping\n",
					     shost->host_no));
			schedule();
			continue;
		}

		__set_current_state(TASK_RUNNING);
		SCSI_LOG_ERROR_RECOVERY(1,
			shost_printk(KERN_INFO, shost,
				     "scsi_eh_%d: waking up %d/%d/%d\n",
				     shost->host_no, shost->host_eh_scheduled,
				     shost->host_failed,
				     atomic_read(&shost->host_busy)));

		/*
		 * We have a host that is failing for some reason.  Figure out
		 * what we need to do to get it up and online again (if we can).
		 * If we fail, we end up taking the thing offline.
		 */
		if (!shost->eh_noresume && scsi_autopm_get_host(shost) != 0) {
			SCSI_LOG_ERROR_RECOVERY(1,
				shost_printk(KERN_ERR, shost,
					     "scsi_eh_%d: unable to autoresume\n",
					     shost->host_no));
			continue;
		}

		if (shost->transportt->eh_strategy_handler)
			shost->transportt->eh_strategy_handler(shost);
		else
			scsi_unjam_host(shost);

		/*
		 * Note - if the above fails completely, the action is to take
		 * individual devices offline and flush the queue of any
		 * outstanding requests that may have been pending.  When we
		 * restart, we restart any I/O to any other devices on the bus
		 * which are still online.
		 */
		scsi_restart_operations(shost);
		if (!shost->eh_noresume)
			scsi_autopm_put_host(shost);
	}
	__set_current_state(TASK_RUNNING);

	SCSI_LOG_ERROR_RECOVERY(1,
		shost_printk(KERN_INFO, shost,
			     "Error handler scsi_eh_%d exiting\n",
			     shost->host_no));
	shost->ehandler = NULL;
	return 0;
}

/*
 * Function:    scsi_report_bus_reset()
 *
 * Purpose:     Utility function used by low-level drivers to report that
 *		they have observed a bus reset on the bus being handled.
 *
 * Arguments:   shost       - Host in question
 *		channel     - channel on which reset was observed.
 *
 * Returns:     Nothing
 *
 * Lock status: Host lock must be held.
 *
 * Notes:       This only needs to be called if the reset is one which
 *		originates from an unknown location.  Resets originated
 *		by the mid-level itself don't need to call this, but there
 *		should be no harm.
 *
 *		The main purpose of this is to make sure that a CHECK_CONDITION
 *		is properly treated.
 */
void scsi_report_bus_reset(struct Scsi_Host *shost, int channel)
{
	struct scsi_device *sdev;

	__shost_for_each_device(sdev, shost) {
		if (channel == sdev_channel(sdev))
			__scsi_report_device_reset(sdev, NULL);
	}
}
EXPORT_SYMBOL(scsi_report_bus_reset);

/*
 * Function:    scsi_report_device_reset()
 *
 * Purpose:     Utility function used by low-level drivers to report that
 *		they have observed a device reset on the device being handled.
 *
 * Arguments:   shost       - Host in question
 *		channel     - channel on which reset was observed
 *		target	    - target on which reset was observed
 *
 * Returns:     Nothing
 *
 * Lock status: Host lock must be held
 *
 * Notes:       This only needs to be called if the reset is one which
 *		originates from an unknown location.  Resets originated
 *		by the mid-level itself don't need to call this, but there
 *		should be no harm.
 *
 *		The main purpose of this is to make sure that a CHECK_CONDITION
 *		is properly treated.
 */
void scsi_report_device_reset(struct Scsi_Host *shost, int channel, int target)
{
	struct scsi_device *sdev;

	__shost_for_each_device(sdev, shost) {
		if (channel == sdev_channel(sdev) &&
		    target == sdev_id(sdev))
			__scsi_report_device_reset(sdev, NULL);
	}
}
EXPORT_SYMBOL(scsi_report_device_reset);

static void
scsi_reset_provider_done_command(struct scsi_cmnd *scmd)
{
}

/**
 * scsi_ioctl_reset: explicitly reset a host/bus/target/device
 * @dev:	scsi_device to operate on
 * @arg:	reset type (see sg.h)
 */
int
scsi_ioctl_reset(struct scsi_device *dev, int __user *arg)
{
	struct scsi_cmnd *scmd;
	struct Scsi_Host *shost = dev->host;
	struct request req;
	unsigned long flags;
	int error = 0, rtn, val;

	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
		return -EACCES;

	error = get_user(val, arg);
	if (error)
		return error;

	if (scsi_autopm_get_host(shost) < 0)
		return -EIO;

	error = -EIO;
	scmd = scsi_get_command(dev, GFP_KERNEL);
	if (!scmd)
		goto out_put_autopm_host;

	blk_rq_init(NULL, &req);
	scmd->request = &req;

	scmd->cmnd = req.cmd;

	scmd->scsi_done		= scsi_reset_provider_done_command;
	memset(&scmd->sdb, 0, sizeof(scmd->sdb));

	scmd->cmd_len			= 0;

	scmd->sc_data_direction		= DMA_BIDIRECTIONAL;

	spin_lock_irqsave(shost->host_lock, flags);
	shost->tmf_in_progress = 1;
	spin_unlock_irqrestore(shost->host_lock, flags);

	switch (val & ~SG_SCSI_RESET_NO_ESCALATE) {
	case SG_SCSI_RESET_NOTHING:
		rtn = SUCCESS;
		break;
	case SG_SCSI_RESET_DEVICE:
		rtn = scsi_try_bus_device_reset(scmd);
		if (rtn == SUCCESS || (val & SG_SCSI_RESET_NO_ESCALATE))
			break;
		/* FALLTHROUGH */
	case SG_SCSI_RESET_TARGET:
		rtn = scsi_try_target_reset(scmd);
		if (rtn == SUCCESS || (val & SG_SCSI_RESET_NO_ESCALATE))
			break;
		/* FALLTHROUGH */
	case SG_SCSI_RESET_BUS:
		rtn = scsi_try_bus_reset(scmd);
		if (rtn == SUCCESS || (val & SG_SCSI_RESET_NO_ESCALATE))
			break;
		/* FALLTHROUGH */
	case SG_SCSI_RESET_HOST:
		rtn = scsi_try_host_reset(scmd);
		if (rtn == SUCCESS)
			break;
	default:
		/* FALLTHROUGH */
		rtn = FAILED;
		break;
	}

	error = (rtn == SUCCESS) ? 0 : -EIO;

	spin_lock_irqsave(shost->host_lock, flags);
	shost->tmf_in_progress = 0;
	spin_unlock_irqrestore(shost->host_lock, flags);

	/*
	 * be sure to wake up anyone who was sleeping or had their queue
	 * suspended while we performed the TMF.
	 */
	SCSI_LOG_ERROR_RECOVERY(3,
		shost_printk(KERN_INFO, shost,
			     "waking up host to restart after TMF\n"));

	wake_up(&shost->host_wait);
	scsi_run_host_queues(shost);

	scsi_put_command(scmd);

out_put_autopm_host:
	scsi_autopm_put_host(shost);
	return error;
}
EXPORT_SYMBOL(scsi_ioctl_reset);

bool scsi_command_normalize_sense(const struct scsi_cmnd *cmd,
				  struct scsi_sense_hdr *sshdr)
{
	return scsi_normalize_sense(cmd->sense_buffer,
			SCSI_SENSE_BUFFERSIZE, sshdr);
}
EXPORT_SYMBOL(scsi_command_normalize_sense);

/**
 * scsi_get_sense_info_fld - get information field from sense data (either fixed or descriptor format)
 * @sense_buffer:	byte array of sense data
 * @sb_len:		number of valid bytes in sense_buffer
 * @info_out:		pointer to 64 integer where 8 or 4 byte information
 *			field will be placed if found.
 *
 * Return value:
 *	1 if information field found, 0 if not found.
 */
int scsi_get_sense_info_fld(const u8 * sense_buffer, int sb_len,
			    u64 * info_out)
{
	int j;
	const u8 * ucp;
	u64 ull;

	if (sb_len < 7)
		return 0;
	switch (sense_buffer[0] & 0x7f) {
	case 0x70:
	case 0x71:
		if (sense_buffer[0] & 0x80) {
			*info_out = (sense_buffer[3] << 24) +
				    (sense_buffer[4] << 16) +
				    (sense_buffer[5] << 8) + sense_buffer[6];
			return 1;
		} else
			return 0;
	case 0x72:
	case 0x73:
		ucp = scsi_sense_desc_find(sense_buffer, sb_len,
					   0 /* info desc */);
		if (ucp && (0xa == ucp[1])) {
			ull = 0;
			for (j = 0; j < 8; ++j) {
				if (j > 0)
					ull <<= 8;
				ull |= ucp[4 + j];
			}
			*info_out = ull;
			return 1;
		} else
			return 0;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(scsi_get_sense_info_fld);
