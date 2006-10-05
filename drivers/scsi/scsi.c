/*
 *  scsi.c Copyright (C) 1992 Drew Eckhardt
 *         Copyright (C) 1993, 1994, 1995, 1999 Eric Youngdale
 *         Copyright (C) 2002, 2003 Christoph Hellwig
 *
 *  generic mid-level SCSI driver
 *      Initial versions: Drew Eckhardt
 *      Subsequent revisions: Eric Youngdale
 *
 *  <drew@colorado.edu>
 *
 *  Bug correction thanks go to :
 *      Rik Faith <faith@cs.unc.edu>
 *      Tommy Thorn <tthorn>
 *      Thomas Wuensche <tw@fgb1.fgb.mw.tu-muenchen.de>
 *
 *  Modified by Eric Youngdale eric@andante.org or ericy@gnu.ai.mit.edu to
 *  add scatter-gather, multiple outstanding request, and other
 *  enhancements.
 *
 *  Native multichannel, wide scsi, /proc/scsi and hot plugging
 *  support added by Michael Neuffer <mike@i-connect.net>
 *
 *  Added request_module("scsi_hostadapter") for kerneld:
 *  (Put an "alias scsi_hostadapter your_hostadapter" in /etc/modprobe.conf)
 *  Bjorn Ekwall  <bj0rn@blox.se>
 *  (changed to kmod)
 *
 *  Major improvements to the timeout, abort, and reset processing,
 *  as well as performance modifications for large queue depths by
 *  Leonard N. Zubkoff <lnz@dandelion.com>
 *
 *  Converted cli() code to spinlocks, Ingo Molnar
 *
 *  Jiffies wrap fixes (host->resetting), 3 Dec 1998 Andrea Arcangeli
 *
 *  out_of_space hacks, D. Gilbert (dpg) 990608
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/unistd.h>
#include <linux/spinlock.h>
#include <linux/kmod.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/mutex.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#include "scsi_priv.h"
#include "scsi_logging.h"

static void scsi_done(struct scsi_cmnd *cmd);

/*
 * Definitions and constants.
 */

#define MIN_RESET_DELAY (2*HZ)

/* Do not call reset on error if we just did a reset within 15 sec. */
#define MIN_RESET_PERIOD (15*HZ)

/*
 * Macro to determine the size of SCSI command. This macro takes vendor
 * unique commands into account. SCSI commands in groups 6 and 7 are
 * vendor unique and we will depend upon the command length being
 * supplied correctly in cmd_len.
 */
#define CDB_SIZE(cmd)	(((((cmd)->cmnd[0] >> 5) & 7) < 6) ? \
				COMMAND_SIZE((cmd)->cmnd[0]) : (cmd)->cmd_len)

/*
 * Note - the initial logging level can be set here to log events at boot time.
 * After the system is up, you may enable logging via the /proc interface.
 */
unsigned int scsi_logging_level;
#if defined(CONFIG_SCSI_LOGGING)
EXPORT_SYMBOL(scsi_logging_level);
#endif

/* NB: These are exposed through /proc/scsi/scsi and form part of the ABI.
 * You may not alter any existing entry (although adding new ones is
 * encouraged once assigned by ANSI/INCITS T10
 */
static const char *const scsi_device_types[] = {
	"Direct-Access    ",
	"Sequential-Access",
	"Printer          ",
	"Processor        ",
	"WORM             ",
	"CD-ROM           ",
	"Scanner          ",
	"Optical Device   ",
	"Medium Changer   ",
	"Communications   ",
	"ASC IT8          ",
	"ASC IT8          ",
	"RAID             ",
	"Enclosure        ",
	"Direct-Access-RBC",
	"Optical card     ",
	"Bridge controller",
	"Object storage   ",
	"Automation/Drive ",
};

const char * scsi_device_type(unsigned type)
{
	if (type == 0x1e)
		return "Well-known LUN   ";
	if (type == 0x1f)
		return "No Device        ";
	if (type >= ARRAY_SIZE(scsi_device_types))
		return "Unknown          ";
	return scsi_device_types[type];
}

EXPORT_SYMBOL(scsi_device_type);

struct scsi_host_cmd_pool {
	kmem_cache_t	*slab;
	unsigned int	users;
	char		*name;
	unsigned int	slab_flags;
	gfp_t		gfp_mask;
};

static struct scsi_host_cmd_pool scsi_cmd_pool = {
	.name		= "scsi_cmd_cache",
	.slab_flags	= SLAB_HWCACHE_ALIGN,
};

static struct scsi_host_cmd_pool scsi_cmd_dma_pool = {
	.name		= "scsi_cmd_cache(DMA)",
	.slab_flags	= SLAB_HWCACHE_ALIGN|SLAB_CACHE_DMA,
	.gfp_mask	= __GFP_DMA,
};

static DEFINE_MUTEX(host_cmd_pool_mutex);

static struct scsi_cmnd *__scsi_get_command(struct Scsi_Host *shost,
					    gfp_t gfp_mask)
{
	struct scsi_cmnd *cmd;

	cmd = kmem_cache_alloc(shost->cmd_pool->slab,
			gfp_mask | shost->cmd_pool->gfp_mask);

	if (unlikely(!cmd)) {
		unsigned long flags;

		spin_lock_irqsave(&shost->free_list_lock, flags);
		if (likely(!list_empty(&shost->free_list))) {
			cmd = list_entry(shost->free_list.next,
					 struct scsi_cmnd, list);
			list_del_init(&cmd->list);
		}
		spin_unlock_irqrestore(&shost->free_list_lock, flags);
	}

	return cmd;
}

/*
 * Function:	scsi_get_command()
 *
 * Purpose:	Allocate and setup a scsi command block
 *
 * Arguments:	dev	- parent scsi device
 *		gfp_mask- allocator flags
 *
 * Returns:	The allocated scsi command structure.
 */
struct scsi_cmnd *scsi_get_command(struct scsi_device *dev, gfp_t gfp_mask)
{
	struct scsi_cmnd *cmd;

	/* Bail if we can't get a reference to the device */
	if (!get_device(&dev->sdev_gendev))
		return NULL;

	cmd = __scsi_get_command(dev->host, gfp_mask);

	if (likely(cmd != NULL)) {
		unsigned long flags;

		memset(cmd, 0, sizeof(*cmd));
		cmd->device = dev;
		init_timer(&cmd->eh_timeout);
		INIT_LIST_HEAD(&cmd->list);
		spin_lock_irqsave(&dev->list_lock, flags);
		list_add_tail(&cmd->list, &dev->cmd_list);
		spin_unlock_irqrestore(&dev->list_lock, flags);
		cmd->jiffies_at_alloc = jiffies;
	} else
		put_device(&dev->sdev_gendev);

	return cmd;
}				
EXPORT_SYMBOL(scsi_get_command);

/*
 * Function:	scsi_put_command()
 *
 * Purpose:	Free a scsi command block
 *
 * Arguments:	cmd	- command block to free
 *
 * Returns:	Nothing.
 *
 * Notes:	The command must not belong to any lists.
 */
void scsi_put_command(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct Scsi_Host *shost = sdev->host;
	unsigned long flags;
	
	/* serious error if the command hasn't come from a device list */
	spin_lock_irqsave(&cmd->device->list_lock, flags);
	BUG_ON(list_empty(&cmd->list));
	list_del_init(&cmd->list);
	spin_unlock(&cmd->device->list_lock);
	/* changing locks here, don't need to restore the irq state */
	spin_lock(&shost->free_list_lock);
	if (unlikely(list_empty(&shost->free_list))) {
		list_add(&cmd->list, &shost->free_list);
		cmd = NULL;
	}
	spin_unlock_irqrestore(&shost->free_list_lock, flags);

	if (likely(cmd != NULL))
		kmem_cache_free(shost->cmd_pool->slab, cmd);

	put_device(&sdev->sdev_gendev);
}
EXPORT_SYMBOL(scsi_put_command);

/*
 * Function:	scsi_setup_command_freelist()
 *
 * Purpose:	Setup the command freelist for a scsi host.
 *
 * Arguments:	shost	- host to allocate the freelist for.
 *
 * Returns:	Nothing.
 */
int scsi_setup_command_freelist(struct Scsi_Host *shost)
{
	struct scsi_host_cmd_pool *pool;
	struct scsi_cmnd *cmd;

	spin_lock_init(&shost->free_list_lock);
	INIT_LIST_HEAD(&shost->free_list);

	/*
	 * Select a command slab for this host and create it if not
	 * yet existant.
	 */
	mutex_lock(&host_cmd_pool_mutex);
	pool = (shost->unchecked_isa_dma ? &scsi_cmd_dma_pool : &scsi_cmd_pool);
	if (!pool->users) {
		pool->slab = kmem_cache_create(pool->name,
				sizeof(struct scsi_cmnd), 0,
				pool->slab_flags, NULL, NULL);
		if (!pool->slab)
			goto fail;
	}

	pool->users++;
	shost->cmd_pool = pool;
	mutex_unlock(&host_cmd_pool_mutex);

	/*
	 * Get one backup command for this host.
	 */
	cmd = kmem_cache_alloc(shost->cmd_pool->slab,
			GFP_KERNEL | shost->cmd_pool->gfp_mask);
	if (!cmd)
		goto fail2;
	list_add(&cmd->list, &shost->free_list);		
	return 0;

 fail2:
	if (!--pool->users)
		kmem_cache_destroy(pool->slab);
	return -ENOMEM;
 fail:
	mutex_unlock(&host_cmd_pool_mutex);
	return -ENOMEM;

}

/*
 * Function:	scsi_destroy_command_freelist()
 *
 * Purpose:	Release the command freelist for a scsi host.
 *
 * Arguments:	shost	- host that's freelist is going to be destroyed
 */
void scsi_destroy_command_freelist(struct Scsi_Host *shost)
{
	while (!list_empty(&shost->free_list)) {
		struct scsi_cmnd *cmd;

		cmd = list_entry(shost->free_list.next, struct scsi_cmnd, list);
		list_del_init(&cmd->list);
		kmem_cache_free(shost->cmd_pool->slab, cmd);
	}

	mutex_lock(&host_cmd_pool_mutex);
	if (!--shost->cmd_pool->users)
		kmem_cache_destroy(shost->cmd_pool->slab);
	mutex_unlock(&host_cmd_pool_mutex);
}

#ifdef CONFIG_SCSI_LOGGING
void scsi_log_send(struct scsi_cmnd *cmd)
{
	unsigned int level;
	struct scsi_device *sdev;

	/*
	 * If ML QUEUE log level is greater than or equal to:
	 *
	 * 1: nothing (match completion)
	 *
	 * 2: log opcode + command of all commands
	 *
	 * 3: same as 2 plus dump cmd address
	 *
	 * 4: same as 3 plus dump extra junk
	 */
	if (unlikely(scsi_logging_level)) {
		level = SCSI_LOG_LEVEL(SCSI_LOG_MLQUEUE_SHIFT,
				       SCSI_LOG_MLQUEUE_BITS);
		if (level > 1) {
			sdev = cmd->device;
			sdev_printk(KERN_INFO, sdev, "send ");
			if (level > 2)
				printk("0x%p ", cmd);
			/*
			 * spaces to match disposition and cmd->result
			 * output in scsi_log_completion.
			 */
			printk("                 ");
			scsi_print_command(cmd);
			if (level > 3) {
				printk(KERN_INFO "buffer = 0x%p, bufflen = %d,"
				       " done = 0x%p, queuecommand 0x%p\n",
					cmd->request_buffer, cmd->request_bufflen,
					cmd->done,
					sdev->host->hostt->queuecommand);

			}
		}
	}
}

void scsi_log_completion(struct scsi_cmnd *cmd, int disposition)
{
	unsigned int level;
	struct scsi_device *sdev;

	/*
	 * If ML COMPLETE log level is greater than or equal to:
	 *
	 * 1: log disposition, result, opcode + command, and conditionally
	 * sense data for failures or non SUCCESS dispositions.
	 *
	 * 2: same as 1 but for all command completions.
	 *
	 * 3: same as 2 plus dump cmd address
	 *
	 * 4: same as 3 plus dump extra junk
	 */
	if (unlikely(scsi_logging_level)) {
		level = SCSI_LOG_LEVEL(SCSI_LOG_MLCOMPLETE_SHIFT,
				       SCSI_LOG_MLCOMPLETE_BITS);
		if (((level > 0) && (cmd->result || disposition != SUCCESS)) ||
		    (level > 1)) {
			sdev = cmd->device;
			sdev_printk(KERN_INFO, sdev, "done ");
			if (level > 2)
				printk("0x%p ", cmd);
			/*
			 * Dump truncated values, so we usually fit within
			 * 80 chars.
			 */
			switch (disposition) {
			case SUCCESS:
				printk("SUCCESS");
				break;
			case NEEDS_RETRY:
				printk("RETRY  ");
				break;
			case ADD_TO_MLQUEUE:
				printk("MLQUEUE");
				break;
			case FAILED:
				printk("FAILED ");
				break;
			case TIMEOUT_ERROR:
				/* 
				 * If called via scsi_times_out.
				 */
				printk("TIMEOUT");
				break;
			default:
				printk("UNKNOWN");
			}
			printk(" %8x ", cmd->result);
			scsi_print_command(cmd);
			if (status_byte(cmd->result) & CHECK_CONDITION) {
				/*
				 * XXX The scsi_print_sense formatting/prefix
				 * doesn't match this function.
				 */
				scsi_print_sense("", cmd);
			}
			if (level > 3) {
				printk(KERN_INFO "scsi host busy %d failed %d\n",
				       sdev->host->host_busy,
				       sdev->host->host_failed);
			}
		}
	}
}
#endif

/* 
 * Assign a serial number and pid to the request for error recovery
 * and debugging purposes.  Protected by the Host_Lock of host.
 */
static inline void scsi_cmd_get_serial(struct Scsi_Host *host, struct scsi_cmnd *cmd)
{
	cmd->serial_number = host->cmd_serial_number++;
	if (cmd->serial_number == 0) 
		cmd->serial_number = host->cmd_serial_number++;
	
	cmd->pid = host->cmd_pid++;
	if (cmd->pid == 0)
		cmd->pid = host->cmd_pid++;
}

/*
 * Function:    scsi_dispatch_command
 *
 * Purpose:     Dispatch a command to the low-level driver.
 *
 * Arguments:   cmd - command block we are dispatching.
 *
 * Notes:
 */
int scsi_dispatch_cmd(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *host = cmd->device->host;
	unsigned long flags = 0;
	unsigned long timeout;
	int rtn = 0;

	/* check if the device is still usable */
	if (unlikely(cmd->device->sdev_state == SDEV_DEL)) {
		/* in SDEV_DEL we error all commands. DID_NO_CONNECT
		 * returns an immediate error upwards, and signals
		 * that the device is no longer present */
		cmd->result = DID_NO_CONNECT << 16;
		atomic_inc(&cmd->device->iorequest_cnt);
		__scsi_done(cmd);
		/* return 0 (because the command has been processed) */
		goto out;
	}

	/* Check to see if the scsi lld put this device into state SDEV_BLOCK. */
	if (unlikely(cmd->device->sdev_state == SDEV_BLOCK)) {
		/* 
		 * in SDEV_BLOCK, the command is just put back on the device
		 * queue.  The suspend state has already blocked the queue so
		 * future requests should not occur until the device 
		 * transitions out of the suspend state.
		 */
		scsi_queue_insert(cmd, SCSI_MLQUEUE_DEVICE_BUSY);

		SCSI_LOG_MLQUEUE(3, printk("queuecommand : device blocked \n"));

		/*
		 * NOTE: rtn is still zero here because we don't need the
		 * queue to be plugged on return (it's already stopped)
		 */
		goto out;
	}

	/* 
	 * If SCSI-2 or lower, store the LUN value in cmnd.
	 */
	if (cmd->device->scsi_level <= SCSI_2 &&
	    cmd->device->scsi_level != SCSI_UNKNOWN) {
		cmd->cmnd[1] = (cmd->cmnd[1] & 0x1f) |
			       (cmd->device->lun << 5 & 0xe0);
	}

	/*
	 * We will wait MIN_RESET_DELAY clock ticks after the last reset so
	 * we can avoid the drive not being ready.
	 */
	timeout = host->last_reset + MIN_RESET_DELAY;

	if (host->resetting && time_before(jiffies, timeout)) {
		int ticks_remaining = timeout - jiffies;
		/*
		 * NOTE: This may be executed from within an interrupt
		 * handler!  This is bad, but for now, it'll do.  The irq
		 * level of the interrupt handler has been masked out by the
		 * platform dependent interrupt handling code already, so the
		 * sti() here will not cause another call to the SCSI host's
		 * interrupt handler (assuming there is one irq-level per
		 * host).
		 */
		while (--ticks_remaining >= 0)
			mdelay(1 + 999 / HZ);
		host->resetting = 0;
	}

	/* 
	 * AK: unlikely race here: for some reason the timer could
	 * expire before the serial number is set up below.
	 */
	scsi_add_timer(cmd, cmd->timeout_per_command, scsi_times_out);

	scsi_log_send(cmd);

	/*
	 * We will use a queued command if possible, otherwise we will
	 * emulate the queuing and calling of completion function ourselves.
	 */
	atomic_inc(&cmd->device->iorequest_cnt);

	/*
	 * Before we queue this command, check if the command
	 * length exceeds what the host adapter can handle.
	 */
	if (CDB_SIZE(cmd) > cmd->device->host->max_cmd_len) {
		SCSI_LOG_MLQUEUE(3,
				printk("queuecommand : command too long.\n"));
		cmd->result = (DID_ABORT << 16);

		scsi_done(cmd);
		goto out;
	}

	spin_lock_irqsave(host->host_lock, flags);
	scsi_cmd_get_serial(host, cmd); 

	if (unlikely(host->shost_state == SHOST_DEL)) {
		cmd->result = (DID_NO_CONNECT << 16);
		scsi_done(cmd);
	} else {
		rtn = host->hostt->queuecommand(cmd, scsi_done);
	}
	spin_unlock_irqrestore(host->host_lock, flags);
	if (rtn) {
		if (scsi_delete_timer(cmd)) {
			atomic_inc(&cmd->device->iodone_cnt);
			scsi_queue_insert(cmd,
					  (rtn == SCSI_MLQUEUE_DEVICE_BUSY) ?
					  rtn : SCSI_MLQUEUE_HOST_BUSY);
		}
		SCSI_LOG_MLQUEUE(3,
		    printk("queuecommand : request rejected\n"));
	}

 out:
	SCSI_LOG_MLQUEUE(3, printk("leaving scsi_dispatch_cmnd()\n"));
	return rtn;
}

/**
 * scsi_req_abort_cmd -- Request command recovery for the specified command
 * cmd: pointer to the SCSI command of interest
 *
 * This function requests that SCSI Core start recovery for the
 * command by deleting the timer and adding the command to the eh
 * queue.  It can be called by either LLDDs or SCSI Core.  LLDDs who
 * implement their own error recovery MAY ignore the timeout event if
 * they generated scsi_req_abort_cmd.
 */
void scsi_req_abort_cmd(struct scsi_cmnd *cmd)
{
	if (!scsi_delete_timer(cmd))
		return;
	scsi_times_out(cmd);
}
EXPORT_SYMBOL(scsi_req_abort_cmd);

/**
 * scsi_done - Enqueue the finished SCSI command into the done queue.
 * @cmd: The SCSI Command for which a low-level device driver (LLDD) gives
 * ownership back to SCSI Core -- i.e. the LLDD has finished with it.
 *
 * This function is the mid-level's (SCSI Core) interrupt routine, which
 * regains ownership of the SCSI command (de facto) from a LLDD, and enqueues
 * the command to the done queue for further processing.
 *
 * This is the producer of the done queue who enqueues at the tail.
 *
 * This function is interrupt context safe.
 */
static void scsi_done(struct scsi_cmnd *cmd)
{
	/*
	 * We don't have to worry about this one timing out any more.
	 * If we are unable to remove the timer, then the command
	 * has already timed out.  In which case, we have no choice but to
	 * let the timeout function run, as we have no idea where in fact
	 * that function could really be.  It might be on another processor,
	 * etc, etc.
	 */
	if (!scsi_delete_timer(cmd))
		return;
	__scsi_done(cmd);
}

/* Private entry to scsi_done() to complete a command when the timer
 * isn't running --- used by scsi_times_out */
void __scsi_done(struct scsi_cmnd *cmd)
{
	struct request *rq = cmd->request;

	/*
	 * Set the serial numbers back to zero
	 */
	cmd->serial_number = 0;

	atomic_inc(&cmd->device->iodone_cnt);
	if (cmd->result)
		atomic_inc(&cmd->device->ioerr_cnt);

	BUG_ON(!rq);

	/*
	 * The uptodate/nbytes values don't matter, as we allow partial
	 * completes and thus will check this in the softirq callback
	 */
	rq->completion_data = cmd;
	blk_complete_request(rq);
}

/*
 * Function:    scsi_retry_command
 *
 * Purpose:     Send a command back to the low level to be retried.
 *
 * Notes:       This command is always executed in the context of the
 *              bottom half handler, or the error handler thread. Low
 *              level drivers should not become re-entrant as a result of
 *              this.
 */
int scsi_retry_command(struct scsi_cmnd *cmd)
{
        /*
         * Zero the sense information from the last time we tried
         * this command.
         */
	memset(cmd->sense_buffer, 0, sizeof(cmd->sense_buffer));

	return scsi_queue_insert(cmd, SCSI_MLQUEUE_EH_RETRY);
}

/*
 * Function:    scsi_finish_command
 *
 * Purpose:     Pass command off to upper layer for finishing of I/O
 *              request, waking processes that are waiting on results,
 *              etc.
 */
void scsi_finish_command(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct Scsi_Host *shost = sdev->host;

	scsi_device_unbusy(sdev);

        /*
         * Clear the flags which say that the device/host is no longer
         * capable of accepting new commands.  These are set in scsi_queue.c
         * for both the queue full condition on a device, and for a
         * host full condition on the host.
	 *
	 * XXX(hch): What about locking?
         */
        shost->host_blocked = 0;
        sdev->device_blocked = 0;

	/*
	 * If we have valid sense information, then some kind of recovery
	 * must have taken place.  Make a note of this.
	 */
	if (SCSI_SENSE_VALID(cmd))
		cmd->result |= (DRIVER_SENSE << 24);

	SCSI_LOG_MLCOMPLETE(4, sdev_printk(KERN_INFO, sdev,
				"Notifying upper driver of completion "
				"(result %x)\n", cmd->result));

	cmd->done(cmd);
}
EXPORT_SYMBOL(scsi_finish_command);

/*
 * Function:	scsi_adjust_queue_depth()
 *
 * Purpose:	Allow low level drivers to tell us to change the queue depth
 * 		on a specific SCSI device
 *
 * Arguments:	sdev	- SCSI Device in question
 * 		tagged	- Do we use tagged queueing (non-0) or do we treat
 * 			  this device as an untagged device (0)
 * 		tags	- Number of tags allowed if tagged queueing enabled,
 * 			  or number of commands the low level driver can
 * 			  queue up in non-tagged mode (as per cmd_per_lun).
 *
 * Returns:	Nothing
 *
 * Lock Status:	None held on entry
 *
 * Notes:	Low level drivers may call this at any time and we will do
 * 		the right thing depending on whether or not the device is
 * 		currently active and whether or not it even has the
 * 		command blocks built yet.
 */
void scsi_adjust_queue_depth(struct scsi_device *sdev, int tagged, int tags)
{
	unsigned long flags;

	/*
	 * refuse to set tagged depth to an unworkable size
	 */
	if (tags <= 0)
		return;

	spin_lock_irqsave(sdev->request_queue->queue_lock, flags);

	/* Check to see if the queue is managed by the block layer
	 * if it is, and we fail to adjust the depth, exit */
	if (blk_queue_tagged(sdev->request_queue) &&
	    blk_queue_resize_tags(sdev->request_queue, tags) != 0)
		goto out;

	sdev->queue_depth = tags;
	switch (tagged) {
		case MSG_ORDERED_TAG:
			sdev->ordered_tags = 1;
			sdev->simple_tags = 1;
			break;
		case MSG_SIMPLE_TAG:
			sdev->ordered_tags = 0;
			sdev->simple_tags = 1;
			break;
		default:
			sdev_printk(KERN_WARNING, sdev,
				    "scsi_adjust_queue_depth, bad queue type, "
				    "disabled\n");
		case 0:
			sdev->ordered_tags = sdev->simple_tags = 0;
			sdev->queue_depth = tags;
			break;
	}
 out:
	spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);
}
EXPORT_SYMBOL(scsi_adjust_queue_depth);

/*
 * Function:	scsi_track_queue_full()
 *
 * Purpose:	This function will track successive QUEUE_FULL events on a
 * 		specific SCSI device to determine if and when there is a
 * 		need to adjust the queue depth on the device.
 *
 * Arguments:	sdev	- SCSI Device in question
 * 		depth	- Current number of outstanding SCSI commands on
 * 			  this device, not counting the one returned as
 * 			  QUEUE_FULL.
 *
 * Returns:	0 - No change needed
 * 		>0 - Adjust queue depth to this new depth
 * 		-1 - Drop back to untagged operation using host->cmd_per_lun
 * 			as the untagged command depth
 *
 * Lock Status:	None held on entry
 *
 * Notes:	Low level drivers may call this at any time and we will do
 * 		"The Right Thing."  We are interrupt context safe.
 */
int scsi_track_queue_full(struct scsi_device *sdev, int depth)
{
	if ((jiffies >> 4) == sdev->last_queue_full_time)
		return 0;

	sdev->last_queue_full_time = (jiffies >> 4);
	if (sdev->last_queue_full_depth != depth) {
		sdev->last_queue_full_count = 1;
		sdev->last_queue_full_depth = depth;
	} else {
		sdev->last_queue_full_count++;
	}

	if (sdev->last_queue_full_count <= 10)
		return 0;
	if (sdev->last_queue_full_depth < 8) {
		/* Drop back to untagged */
		scsi_adjust_queue_depth(sdev, 0, sdev->host->cmd_per_lun);
		return -1;
	}
	
	if (sdev->ordered_tags)
		scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, depth);
	else
		scsi_adjust_queue_depth(sdev, MSG_SIMPLE_TAG, depth);
	return depth;
}
EXPORT_SYMBOL(scsi_track_queue_full);

/**
 * scsi_device_get  -  get an addition reference to a scsi_device
 * @sdev:	device to get a reference to
 *
 * Gets a reference to the scsi_device and increments the use count
 * of the underlying LLDD module.  You must hold host_lock of the
 * parent Scsi_Host or already have a reference when calling this.
 */
int scsi_device_get(struct scsi_device *sdev)
{
	if (sdev->sdev_state == SDEV_DEL)
		return -ENXIO;
	if (!get_device(&sdev->sdev_gendev))
		return -ENXIO;
	/* We can fail this if we're doing SCSI operations
	 * from module exit (like cache flush) */
	try_module_get(sdev->host->hostt->module);

	return 0;
}
EXPORT_SYMBOL(scsi_device_get);

/**
 * scsi_device_put  -  release a reference to a scsi_device
 * @sdev:	device to release a reference on.
 *
 * Release a reference to the scsi_device and decrements the use count
 * of the underlying LLDD module.  The device is freed once the last
 * user vanishes.
 */
void scsi_device_put(struct scsi_device *sdev)
{
	struct module *module = sdev->host->hostt->module;

#ifdef CONFIG_MODULE_UNLOAD
	/* The module refcount will be zero if scsi_device_get()
	 * was called from a module removal routine */
	if (module && module_refcount(module) != 0)
		module_put(module);
#endif
	put_device(&sdev->sdev_gendev);
}
EXPORT_SYMBOL(scsi_device_put);

/* helper for shost_for_each_device, thus not documented */
struct scsi_device *__scsi_iterate_devices(struct Scsi_Host *shost,
					   struct scsi_device *prev)
{
	struct list_head *list = (prev ? &prev->siblings : &shost->__devices);
	struct scsi_device *next = NULL;
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	while (list->next != &shost->__devices) {
		next = list_entry(list->next, struct scsi_device, siblings);
		/* skip devices that we can't get a reference to */
		if (!scsi_device_get(next))
			break;
		next = NULL;
		list = list->next;
	}
	spin_unlock_irqrestore(shost->host_lock, flags);

	if (prev)
		scsi_device_put(prev);
	return next;
}
EXPORT_SYMBOL(__scsi_iterate_devices);

/**
 * starget_for_each_device  -  helper to walk all devices of a target
 * @starget:	target whose devices we want to iterate over.
 *
 * This traverses over each devices of @shost.  The devices have
 * a reference that must be released by scsi_host_put when breaking
 * out of the loop.
 */
void starget_for_each_device(struct scsi_target *starget, void * data,
		     void (*fn)(struct scsi_device *, void *))
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct scsi_device *sdev;

	shost_for_each_device(sdev, shost) {
		if ((sdev->channel == starget->channel) &&
		    (sdev->id == starget->id))
			fn(sdev, data);
	}
}
EXPORT_SYMBOL(starget_for_each_device);

/**
 * __scsi_device_lookup_by_target - find a device given the target (UNLOCKED)
 * @starget:	SCSI target pointer
 * @lun:	SCSI Logical Unit Number
 *
 * Looks up the scsi_device with the specified @lun for a give
 * @starget. The returned scsi_device does not have an additional
 * reference.  You must hold the host's host_lock over this call and
 * any access to the returned scsi_device.
 *
 * Note:  The only reason why drivers would want to use this is because
 * they're need to access the device list in irq context.  Otherwise you
 * really want to use scsi_device_lookup_by_target instead.
 **/
struct scsi_device *__scsi_device_lookup_by_target(struct scsi_target *starget,
						   uint lun)
{
	struct scsi_device *sdev;

	list_for_each_entry(sdev, &starget->devices, same_target_siblings) {
		if (sdev->lun ==lun)
			return sdev;
	}

	return NULL;
}
EXPORT_SYMBOL(__scsi_device_lookup_by_target);

/**
 * scsi_device_lookup_by_target - find a device given the target
 * @starget:	SCSI target pointer
 * @lun:	SCSI Logical Unit Number
 *
 * Looks up the scsi_device with the specified @channel, @id, @lun for a
 * give host.  The returned scsi_device has an additional reference that
 * needs to be release with scsi_host_put once you're done with it.
 **/
struct scsi_device *scsi_device_lookup_by_target(struct scsi_target *starget,
						 uint lun)
{
	struct scsi_device *sdev;
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	sdev = __scsi_device_lookup_by_target(starget, lun);
	if (sdev && scsi_device_get(sdev))
		sdev = NULL;
	spin_unlock_irqrestore(shost->host_lock, flags);

	return sdev;
}
EXPORT_SYMBOL(scsi_device_lookup_by_target);

/**
 * scsi_device_lookup - find a device given the host (UNLOCKED)
 * @shost:	SCSI host pointer
 * @channel:	SCSI channel (zero if only one channel)
 * @pun:	SCSI target number (physical unit number)
 * @lun:	SCSI Logical Unit Number
 *
 * Looks up the scsi_device with the specified @channel, @id, @lun for a
 * give host. The returned scsi_device does not have an additional reference.
 * You must hold the host's host_lock over this call and any access to the
 * returned scsi_device.
 *
 * Note:  The only reason why drivers would want to use this is because
 * they're need to access the device list in irq context.  Otherwise you
 * really want to use scsi_device_lookup instead.
 **/
struct scsi_device *__scsi_device_lookup(struct Scsi_Host *shost,
		uint channel, uint id, uint lun)
{
	struct scsi_device *sdev;

	list_for_each_entry(sdev, &shost->__devices, siblings) {
		if (sdev->channel == channel && sdev->id == id &&
				sdev->lun ==lun)
			return sdev;
	}

	return NULL;
}
EXPORT_SYMBOL(__scsi_device_lookup);

/**
 * scsi_device_lookup - find a device given the host
 * @shost:	SCSI host pointer
 * @channel:	SCSI channel (zero if only one channel)
 * @id:		SCSI target number (physical unit number)
 * @lun:	SCSI Logical Unit Number
 *
 * Looks up the scsi_device with the specified @channel, @id, @lun for a
 * give host.  The returned scsi_device has an additional reference that
 * needs to be release with scsi_host_put once you're done with it.
 **/
struct scsi_device *scsi_device_lookup(struct Scsi_Host *shost,
		uint channel, uint id, uint lun)
{
	struct scsi_device *sdev;
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	sdev = __scsi_device_lookup(shost, channel, id, lun);
	if (sdev && scsi_device_get(sdev))
		sdev = NULL;
	spin_unlock_irqrestore(shost->host_lock, flags);

	return sdev;
}
EXPORT_SYMBOL(scsi_device_lookup);

/**
 * scsi_device_cancel - cancel outstanding IO to this device
 * @sdev:	Pointer to struct scsi_device
 * @recovery:	Boolean instructing function to recover device or not.
 *
 **/
int scsi_device_cancel(struct scsi_device *sdev, int recovery)
{
	struct scsi_cmnd *scmd;
	LIST_HEAD(active_list);
	struct list_head *lh, *lh_sf;
	unsigned long flags;

	scsi_device_set_state(sdev, SDEV_CANCEL);

	spin_lock_irqsave(&sdev->list_lock, flags);
	list_for_each_entry(scmd, &sdev->cmd_list, list) {
		if (scmd->request) {
			/*
			 * If we are unable to remove the timer, it means
			 * that the command has already timed out or
			 * finished.
			 */
			if (!scsi_delete_timer(scmd))
				continue;
			list_add_tail(&scmd->eh_entry, &active_list);
		}
	}
	spin_unlock_irqrestore(&sdev->list_lock, flags);

	if (!list_empty(&active_list)) {
		list_for_each_safe(lh, lh_sf, &active_list) {
			scmd = list_entry(lh, struct scsi_cmnd, eh_entry);
			list_del_init(lh);
			if (recovery &&
			    !scsi_eh_scmd_add(scmd, SCSI_EH_CANCEL_CMD)) {
				scmd->result = (DID_ABORT << 16);
				scsi_finish_command(scmd);
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL(scsi_device_cancel);

MODULE_DESCRIPTION("SCSI core");
MODULE_LICENSE("GPL");

module_param(scsi_logging_level, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(scsi_logging_level, "a bit mask of logging levels");

static int __init init_scsi(void)
{
	int error;

	error = scsi_init_queue();
	if (error)
		return error;
	error = scsi_init_procfs();
	if (error)
		goto cleanup_queue;
	error = scsi_init_devinfo();
	if (error)
		goto cleanup_procfs;
	error = scsi_init_hosts();
	if (error)
		goto cleanup_devlist;
	error = scsi_init_sysctl();
	if (error)
		goto cleanup_hosts;
	error = scsi_sysfs_register();
	if (error)
		goto cleanup_sysctl;

	scsi_netlink_init();

	printk(KERN_NOTICE "SCSI subsystem initialized\n");
	return 0;

cleanup_sysctl:
	scsi_exit_sysctl();
cleanup_hosts:
	scsi_exit_hosts();
cleanup_devlist:
	scsi_exit_devinfo();
cleanup_procfs:
	scsi_exit_procfs();
cleanup_queue:
	scsi_exit_queue();
	printk(KERN_ERR "SCSI subsystem failed to initialize, error = %d\n",
	       -error);
	return error;
}

static void __exit exit_scsi(void)
{
	scsi_netlink_exit();
	scsi_sysfs_unregister();
	scsi_exit_sysctl();
	scsi_exit_hosts();
	scsi_exit_devinfo();
	scsi_exit_procfs();
	scsi_exit_queue();
}

subsys_initcall(init_scsi);
module_exit(exit_scsi);
