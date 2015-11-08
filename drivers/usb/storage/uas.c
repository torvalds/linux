/*
 * USB Attached SCSI
 * Note that this is not the same as the USB Mass Storage driver
 *
 * Copyright Hans de Goede <hdegoede@redhat.com> for Red Hat, Inc. 2013 - 2014
 * Copyright Matthew Wilcox for Intel Corp, 2010
 * Copyright Sarah Sharp for Intel Corp, 2010
 *
 * Distributed under the terms of the GNU GPL, version two.
 */

#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/usb/hcd.h>
#include <linux/usb/storage.h>
#include <linux/usb/uas.h>

#include <scsi/scsi.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

#include "uas-detect.h"
#include "scsiglue.h"

#define MAX_CMNDS 256

struct uas_dev_info {
	struct usb_interface *intf;
	struct usb_device *udev;
	struct usb_anchor cmd_urbs;
	struct usb_anchor sense_urbs;
	struct usb_anchor data_urbs;
	unsigned long flags;
	int qdepth, resetting;
	unsigned cmd_pipe, status_pipe, data_in_pipe, data_out_pipe;
	unsigned use_streams:1;
	unsigned shutdown:1;
	struct scsi_cmnd *cmnd[MAX_CMNDS];
	spinlock_t lock;
	struct work_struct work;
};

enum {
	SUBMIT_STATUS_URB	= (1 << 1),
	ALLOC_DATA_IN_URB	= (1 << 2),
	SUBMIT_DATA_IN_URB	= (1 << 3),
	ALLOC_DATA_OUT_URB	= (1 << 4),
	SUBMIT_DATA_OUT_URB	= (1 << 5),
	ALLOC_CMD_URB		= (1 << 6),
	SUBMIT_CMD_URB		= (1 << 7),
	COMMAND_INFLIGHT        = (1 << 8),
	DATA_IN_URB_INFLIGHT    = (1 << 9),
	DATA_OUT_URB_INFLIGHT   = (1 << 10),
	COMMAND_ABORTED         = (1 << 11),
	IS_IN_WORK_LIST         = (1 << 12),
};

/* Overrides scsi_pointer */
struct uas_cmd_info {
	unsigned int state;
	unsigned int uas_tag;
	struct urb *cmd_urb;
	struct urb *data_in_urb;
	struct urb *data_out_urb;
};

/* I hate forward declarations, but I actually have a loop */
static int uas_submit_urbs(struct scsi_cmnd *cmnd,
				struct uas_dev_info *devinfo, gfp_t gfp);
static void uas_do_work(struct work_struct *work);
static int uas_try_complete(struct scsi_cmnd *cmnd, const char *caller);
static void uas_free_streams(struct uas_dev_info *devinfo);
static void uas_log_cmd_state(struct scsi_cmnd *cmnd, const char *prefix,
				int status);

static void uas_do_work(struct work_struct *work)
{
	struct uas_dev_info *devinfo =
		container_of(work, struct uas_dev_info, work);
	struct uas_cmd_info *cmdinfo;
	struct scsi_cmnd *cmnd;
	unsigned long flags;
	int i, err;

	spin_lock_irqsave(&devinfo->lock, flags);

	if (devinfo->resetting)
		goto out;

	for (i = 0; i < devinfo->qdepth; i++) {
		if (!devinfo->cmnd[i])
			continue;

		cmnd = devinfo->cmnd[i];
		cmdinfo = (void *)&cmnd->SCp;

		if (!(cmdinfo->state & IS_IN_WORK_LIST))
			continue;

		err = uas_submit_urbs(cmnd, cmnd->device->hostdata, GFP_ATOMIC);
		if (!err)
			cmdinfo->state &= ~IS_IN_WORK_LIST;
		else
			schedule_work(&devinfo->work);
	}
out:
	spin_unlock_irqrestore(&devinfo->lock, flags);
}

static void uas_add_work(struct uas_cmd_info *cmdinfo)
{
	struct scsi_pointer *scp = (void *)cmdinfo;
	struct scsi_cmnd *cmnd = container_of(scp, struct scsi_cmnd, SCp);
	struct uas_dev_info *devinfo = cmnd->device->hostdata;

	lockdep_assert_held(&devinfo->lock);
	cmdinfo->state |= IS_IN_WORK_LIST;
	schedule_work(&devinfo->work);
}

static void uas_zap_pending(struct uas_dev_info *devinfo, int result)
{
	struct uas_cmd_info *cmdinfo;
	struct scsi_cmnd *cmnd;
	unsigned long flags;
	int i, err;

	spin_lock_irqsave(&devinfo->lock, flags);
	for (i = 0; i < devinfo->qdepth; i++) {
		if (!devinfo->cmnd[i])
			continue;

		cmnd = devinfo->cmnd[i];
		cmdinfo = (void *)&cmnd->SCp;
		uas_log_cmd_state(cmnd, __func__, 0);
		/* Sense urbs were killed, clear COMMAND_INFLIGHT manually */
		cmdinfo->state &= ~COMMAND_INFLIGHT;
		cmnd->result = result << 16;
		err = uas_try_complete(cmnd, __func__);
		WARN_ON(err != 0);
	}
	spin_unlock_irqrestore(&devinfo->lock, flags);
}

static void uas_sense(struct urb *urb, struct scsi_cmnd *cmnd)
{
	struct sense_iu *sense_iu = urb->transfer_buffer;
	struct scsi_device *sdev = cmnd->device;

	if (urb->actual_length > 16) {
		unsigned len = be16_to_cpup(&sense_iu->len);
		if (len + 16 != urb->actual_length) {
			int newlen = min(len + 16, urb->actual_length) - 16;
			if (newlen < 0)
				newlen = 0;
			sdev_printk(KERN_INFO, sdev, "%s: urb length %d "
				"disagrees with IU sense data length %d, "
				"using %d bytes of sense data\n", __func__,
					urb->actual_length, len, newlen);
			len = newlen;
		}
		memcpy(cmnd->sense_buffer, sense_iu->sense, len);
	}

	cmnd->result = sense_iu->status;
}

static void uas_log_cmd_state(struct scsi_cmnd *cmnd, const char *prefix,
			      int status)
{
	struct uas_cmd_info *ci = (void *)&cmnd->SCp;
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;

	scmd_printk(KERN_INFO, cmnd,
		    "%s %d uas-tag %d inflight:%s%s%s%s%s%s%s%s%s%s%s%s ",
		    prefix, status, cmdinfo->uas_tag,
		    (ci->state & SUBMIT_STATUS_URB)     ? " s-st"  : "",
		    (ci->state & ALLOC_DATA_IN_URB)     ? " a-in"  : "",
		    (ci->state & SUBMIT_DATA_IN_URB)    ? " s-in"  : "",
		    (ci->state & ALLOC_DATA_OUT_URB)    ? " a-out" : "",
		    (ci->state & SUBMIT_DATA_OUT_URB)   ? " s-out" : "",
		    (ci->state & ALLOC_CMD_URB)         ? " a-cmd" : "",
		    (ci->state & SUBMIT_CMD_URB)        ? " s-cmd" : "",
		    (ci->state & COMMAND_INFLIGHT)      ? " CMD"   : "",
		    (ci->state & DATA_IN_URB_INFLIGHT)  ? " IN"    : "",
		    (ci->state & DATA_OUT_URB_INFLIGHT) ? " OUT"   : "",
		    (ci->state & COMMAND_ABORTED)       ? " abort" : "",
		    (ci->state & IS_IN_WORK_LIST)       ? " work"  : "");
	scsi_print_command(cmnd);
}

static void uas_free_unsubmitted_urbs(struct scsi_cmnd *cmnd)
{
	struct uas_cmd_info *cmdinfo;

	if (!cmnd)
		return;

	cmdinfo = (void *)&cmnd->SCp;

	if (cmdinfo->state & SUBMIT_CMD_URB)
		usb_free_urb(cmdinfo->cmd_urb);

	/* data urbs may have never gotten their submit flag set */
	if (!(cmdinfo->state & DATA_IN_URB_INFLIGHT))
		usb_free_urb(cmdinfo->data_in_urb);
	if (!(cmdinfo->state & DATA_OUT_URB_INFLIGHT))
		usb_free_urb(cmdinfo->data_out_urb);
}

static int uas_try_complete(struct scsi_cmnd *cmnd, const char *caller)
{
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	struct uas_dev_info *devinfo = (void *)cmnd->device->hostdata;

	lockdep_assert_held(&devinfo->lock);
	if (cmdinfo->state & (COMMAND_INFLIGHT |
			      DATA_IN_URB_INFLIGHT |
			      DATA_OUT_URB_INFLIGHT |
			      COMMAND_ABORTED))
		return -EBUSY;
	devinfo->cmnd[cmdinfo->uas_tag - 1] = NULL;
	uas_free_unsubmitted_urbs(cmnd);
	cmnd->scsi_done(cmnd);
	return 0;
}

static void uas_xfer_data(struct urb *urb, struct scsi_cmnd *cmnd,
			  unsigned direction)
{
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	int err;

	cmdinfo->state |= direction | SUBMIT_STATUS_URB;
	err = uas_submit_urbs(cmnd, cmnd->device->hostdata, GFP_ATOMIC);
	if (err) {
		uas_add_work(cmdinfo);
	}
}

static void uas_stat_cmplt(struct urb *urb)
{
	struct iu *iu = urb->transfer_buffer;
	struct Scsi_Host *shost = urb->context;
	struct uas_dev_info *devinfo = (struct uas_dev_info *)shost->hostdata;
	struct urb *data_in_urb = NULL;
	struct urb *data_out_urb = NULL;
	struct scsi_cmnd *cmnd;
	struct uas_cmd_info *cmdinfo;
	unsigned long flags;
	unsigned int idx;
	int status = urb->status;

	spin_lock_irqsave(&devinfo->lock, flags);

	if (devinfo->resetting)
		goto out;

	if (status) {
		if (status != -ENOENT && status != -ECONNRESET && status != -ESHUTDOWN)
			dev_err(&urb->dev->dev, "stat urb: status %d\n", status);
		goto out;
	}

	idx = be16_to_cpup(&iu->tag) - 1;
	if (idx >= MAX_CMNDS || !devinfo->cmnd[idx]) {
		dev_err(&urb->dev->dev,
			"stat urb: no pending cmd for uas-tag %d\n", idx + 1);
		goto out;
	}

	cmnd = devinfo->cmnd[idx];
	cmdinfo = (void *)&cmnd->SCp;

	if (!(cmdinfo->state & COMMAND_INFLIGHT)) {
		uas_log_cmd_state(cmnd, "unexpected status cmplt", 0);
		goto out;
	}

	switch (iu->iu_id) {
	case IU_ID_STATUS:
		uas_sense(urb, cmnd);
		if (cmnd->result != 0) {
			/* cancel data transfers on error */
			data_in_urb = usb_get_urb(cmdinfo->data_in_urb);
			data_out_urb = usb_get_urb(cmdinfo->data_out_urb);
		}
		cmdinfo->state &= ~COMMAND_INFLIGHT;
		uas_try_complete(cmnd, __func__);
		break;
	case IU_ID_READ_READY:
		if (!cmdinfo->data_in_urb ||
				(cmdinfo->state & DATA_IN_URB_INFLIGHT)) {
			uas_log_cmd_state(cmnd, "unexpected read rdy", 0);
			break;
		}
		uas_xfer_data(urb, cmnd, SUBMIT_DATA_IN_URB);
		break;
	case IU_ID_WRITE_READY:
		if (!cmdinfo->data_out_urb ||
				(cmdinfo->state & DATA_OUT_URB_INFLIGHT)) {
			uas_log_cmd_state(cmnd, "unexpected write rdy", 0);
			break;
		}
		uas_xfer_data(urb, cmnd, SUBMIT_DATA_OUT_URB);
		break;
	case IU_ID_RESPONSE:
		uas_log_cmd_state(cmnd, "unexpected response iu",
				  ((struct response_iu *)iu)->response_code);
		/* Error, cancel data transfers */
		data_in_urb = usb_get_urb(cmdinfo->data_in_urb);
		data_out_urb = usb_get_urb(cmdinfo->data_out_urb);
		cmdinfo->state &= ~COMMAND_INFLIGHT;
		cmnd->result = DID_ERROR << 16;
		uas_try_complete(cmnd, __func__);
		break;
	default:
		uas_log_cmd_state(cmnd, "bogus IU", iu->iu_id);
	}
out:
	usb_free_urb(urb);
	spin_unlock_irqrestore(&devinfo->lock, flags);

	/* Unlinking of data urbs must be done without holding the lock */
	if (data_in_urb) {
		usb_unlink_urb(data_in_urb);
		usb_put_urb(data_in_urb);
	}
	if (data_out_urb) {
		usb_unlink_urb(data_out_urb);
		usb_put_urb(data_out_urb);
	}
}

static void uas_data_cmplt(struct urb *urb)
{
	struct scsi_cmnd *cmnd = urb->context;
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	struct uas_dev_info *devinfo = (void *)cmnd->device->hostdata;
	struct scsi_data_buffer *sdb = NULL;
	unsigned long flags;
	int status = urb->status;

	spin_lock_irqsave(&devinfo->lock, flags);

	if (cmdinfo->data_in_urb == urb) {
		sdb = scsi_in(cmnd);
		cmdinfo->state &= ~DATA_IN_URB_INFLIGHT;
		cmdinfo->data_in_urb = NULL;
	} else if (cmdinfo->data_out_urb == urb) {
		sdb = scsi_out(cmnd);
		cmdinfo->state &= ~DATA_OUT_URB_INFLIGHT;
		cmdinfo->data_out_urb = NULL;
	}
	if (sdb == NULL) {
		WARN_ON_ONCE(1);
		goto out;
	}

	if (devinfo->resetting)
		goto out;

	/* Data urbs should not complete before the cmd urb is submitted */
	if (cmdinfo->state & SUBMIT_CMD_URB) {
		uas_log_cmd_state(cmnd, "unexpected data cmplt", 0);
		goto out;
	}

	if (status) {
		if (status != -ENOENT && status != -ECONNRESET && status != -ESHUTDOWN)
			uas_log_cmd_state(cmnd, "data cmplt err", status);
		/* error: no data transfered */
		sdb->resid = sdb->length;
	} else {
		sdb->resid = sdb->length - urb->actual_length;
	}
	uas_try_complete(cmnd, __func__);
out:
	usb_free_urb(urb);
	spin_unlock_irqrestore(&devinfo->lock, flags);
}

static void uas_cmd_cmplt(struct urb *urb)
{
	if (urb->status)
		dev_err(&urb->dev->dev, "cmd cmplt err %d\n", urb->status);

	usb_free_urb(urb);
}

static struct urb *uas_alloc_data_urb(struct uas_dev_info *devinfo, gfp_t gfp,
				      struct scsi_cmnd *cmnd,
				      enum dma_data_direction dir)
{
	struct usb_device *udev = devinfo->udev;
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	struct urb *urb = usb_alloc_urb(0, gfp);
	struct scsi_data_buffer *sdb = (dir == DMA_FROM_DEVICE)
		? scsi_in(cmnd) : scsi_out(cmnd);
	unsigned int pipe = (dir == DMA_FROM_DEVICE)
		? devinfo->data_in_pipe : devinfo->data_out_pipe;

	if (!urb)
		goto out;
	usb_fill_bulk_urb(urb, udev, pipe, NULL, sdb->length,
			  uas_data_cmplt, cmnd);
	if (devinfo->use_streams)
		urb->stream_id = cmdinfo->uas_tag;
	urb->num_sgs = udev->bus->sg_tablesize ? sdb->table.nents : 0;
	urb->sg = sdb->table.sgl;
 out:
	return urb;
}

static struct urb *uas_alloc_sense_urb(struct uas_dev_info *devinfo, gfp_t gfp,
				       struct scsi_cmnd *cmnd)
{
	struct usb_device *udev = devinfo->udev;
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	struct urb *urb = usb_alloc_urb(0, gfp);
	struct sense_iu *iu;

	if (!urb)
		goto out;

	iu = kzalloc(sizeof(*iu), gfp);
	if (!iu)
		goto free;

	usb_fill_bulk_urb(urb, udev, devinfo->status_pipe, iu, sizeof(*iu),
			  uas_stat_cmplt, cmnd->device->host);
	if (devinfo->use_streams)
		urb->stream_id = cmdinfo->uas_tag;
	urb->transfer_flags |= URB_FREE_BUFFER;
 out:
	return urb;
 free:
	usb_free_urb(urb);
	return NULL;
}

static struct urb *uas_alloc_cmd_urb(struct uas_dev_info *devinfo, gfp_t gfp,
					struct scsi_cmnd *cmnd)
{
	struct usb_device *udev = devinfo->udev;
	struct scsi_device *sdev = cmnd->device;
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	struct urb *urb = usb_alloc_urb(0, gfp);
	struct command_iu *iu;
	int len;

	if (!urb)
		goto out;

	len = cmnd->cmd_len - 16;
	if (len < 0)
		len = 0;
	len = ALIGN(len, 4);
	iu = kzalloc(sizeof(*iu) + len, gfp);
	if (!iu)
		goto free;

	iu->iu_id = IU_ID_COMMAND;
	iu->tag = cpu_to_be16(cmdinfo->uas_tag);
	iu->prio_attr = UAS_SIMPLE_TAG;
	iu->len = len;
	int_to_scsilun(sdev->lun, &iu->lun);
	memcpy(iu->cdb, cmnd->cmnd, cmnd->cmd_len);

	usb_fill_bulk_urb(urb, udev, devinfo->cmd_pipe, iu, sizeof(*iu) + len,
							uas_cmd_cmplt, NULL);
	urb->transfer_flags |= URB_FREE_BUFFER;
 out:
	return urb;
 free:
	usb_free_urb(urb);
	return NULL;
}

/*
 * Why should I request the Status IU before sending the Command IU?  Spec
 * says to, but also says the device may receive them in any order.  Seems
 * daft to me.
 */

static struct urb *uas_submit_sense_urb(struct scsi_cmnd *cmnd, gfp_t gfp)
{
	struct uas_dev_info *devinfo = cmnd->device->hostdata;
	struct urb *urb;
	int err;

	urb = uas_alloc_sense_urb(devinfo, gfp, cmnd);
	if (!urb)
		return NULL;
	usb_anchor_urb(urb, &devinfo->sense_urbs);
	err = usb_submit_urb(urb, gfp);
	if (err) {
		usb_unanchor_urb(urb);
		uas_log_cmd_state(cmnd, "sense submit err", err);
		usb_free_urb(urb);
		return NULL;
	}
	return urb;
}

static int uas_submit_urbs(struct scsi_cmnd *cmnd,
			   struct uas_dev_info *devinfo, gfp_t gfp)
{
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	struct urb *urb;
	int err;

	lockdep_assert_held(&devinfo->lock);
	if (cmdinfo->state & SUBMIT_STATUS_URB) {
		urb = uas_submit_sense_urb(cmnd, gfp);
		if (!urb)
			return SCSI_MLQUEUE_DEVICE_BUSY;
		cmdinfo->state &= ~SUBMIT_STATUS_URB;
	}

	if (cmdinfo->state & ALLOC_DATA_IN_URB) {
		cmdinfo->data_in_urb = uas_alloc_data_urb(devinfo, gfp,
							cmnd, DMA_FROM_DEVICE);
		if (!cmdinfo->data_in_urb)
			return SCSI_MLQUEUE_DEVICE_BUSY;
		cmdinfo->state &= ~ALLOC_DATA_IN_URB;
	}

	if (cmdinfo->state & SUBMIT_DATA_IN_URB) {
		usb_anchor_urb(cmdinfo->data_in_urb, &devinfo->data_urbs);
		err = usb_submit_urb(cmdinfo->data_in_urb, gfp);
		if (err) {
			usb_unanchor_urb(cmdinfo->data_in_urb);
			uas_log_cmd_state(cmnd, "data in submit err", err);
			return SCSI_MLQUEUE_DEVICE_BUSY;
		}
		cmdinfo->state &= ~SUBMIT_DATA_IN_URB;
		cmdinfo->state |= DATA_IN_URB_INFLIGHT;
	}

	if (cmdinfo->state & ALLOC_DATA_OUT_URB) {
		cmdinfo->data_out_urb = uas_alloc_data_urb(devinfo, gfp,
							cmnd, DMA_TO_DEVICE);
		if (!cmdinfo->data_out_urb)
			return SCSI_MLQUEUE_DEVICE_BUSY;
		cmdinfo->state &= ~ALLOC_DATA_OUT_URB;
	}

	if (cmdinfo->state & SUBMIT_DATA_OUT_URB) {
		usb_anchor_urb(cmdinfo->data_out_urb, &devinfo->data_urbs);
		err = usb_submit_urb(cmdinfo->data_out_urb, gfp);
		if (err) {
			usb_unanchor_urb(cmdinfo->data_out_urb);
			uas_log_cmd_state(cmnd, "data out submit err", err);
			return SCSI_MLQUEUE_DEVICE_BUSY;
		}
		cmdinfo->state &= ~SUBMIT_DATA_OUT_URB;
		cmdinfo->state |= DATA_OUT_URB_INFLIGHT;
	}

	if (cmdinfo->state & ALLOC_CMD_URB) {
		cmdinfo->cmd_urb = uas_alloc_cmd_urb(devinfo, gfp, cmnd);
		if (!cmdinfo->cmd_urb)
			return SCSI_MLQUEUE_DEVICE_BUSY;
		cmdinfo->state &= ~ALLOC_CMD_URB;
	}

	if (cmdinfo->state & SUBMIT_CMD_URB) {
		usb_anchor_urb(cmdinfo->cmd_urb, &devinfo->cmd_urbs);
		err = usb_submit_urb(cmdinfo->cmd_urb, gfp);
		if (err) {
			usb_unanchor_urb(cmdinfo->cmd_urb);
			uas_log_cmd_state(cmnd, "cmd submit err", err);
			return SCSI_MLQUEUE_DEVICE_BUSY;
		}
		cmdinfo->cmd_urb = NULL;
		cmdinfo->state &= ~SUBMIT_CMD_URB;
		cmdinfo->state |= COMMAND_INFLIGHT;
	}

	return 0;
}

static int uas_queuecommand_lck(struct scsi_cmnd *cmnd,
					void (*done)(struct scsi_cmnd *))
{
	struct scsi_device *sdev = cmnd->device;
	struct uas_dev_info *devinfo = sdev->hostdata;
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	unsigned long flags;
	int idx, err;

	BUILD_BUG_ON(sizeof(struct uas_cmd_info) > sizeof(struct scsi_pointer));

	/* Re-check scsi_block_requests now that we've the host-lock */
	if (cmnd->device->host->host_self_blocked)
		return SCSI_MLQUEUE_DEVICE_BUSY;

	if ((devinfo->flags & US_FL_NO_ATA_1X) &&
			(cmnd->cmnd[0] == ATA_12 || cmnd->cmnd[0] == ATA_16)) {
		memcpy(cmnd->sense_buffer, usb_stor_sense_invalidCDB,
		       sizeof(usb_stor_sense_invalidCDB));
		cmnd->result = SAM_STAT_CHECK_CONDITION;
		cmnd->scsi_done(cmnd);
		return 0;
	}

	spin_lock_irqsave(&devinfo->lock, flags);

	if (devinfo->resetting) {
		cmnd->result = DID_ERROR << 16;
		cmnd->scsi_done(cmnd);
		spin_unlock_irqrestore(&devinfo->lock, flags);
		return 0;
	}

	/* Find a free uas-tag */
	for (idx = 0; idx < devinfo->qdepth; idx++) {
		if (!devinfo->cmnd[idx])
			break;
	}
	if (idx == devinfo->qdepth) {
		spin_unlock_irqrestore(&devinfo->lock, flags);
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}

	cmnd->scsi_done = done;

	memset(cmdinfo, 0, sizeof(*cmdinfo));
	cmdinfo->uas_tag = idx + 1; /* uas-tag == usb-stream-id, so 1 based */
	cmdinfo->state = SUBMIT_STATUS_URB | ALLOC_CMD_URB | SUBMIT_CMD_URB;

	switch (cmnd->sc_data_direction) {
	case DMA_FROM_DEVICE:
		cmdinfo->state |= ALLOC_DATA_IN_URB | SUBMIT_DATA_IN_URB;
		break;
	case DMA_BIDIRECTIONAL:
		cmdinfo->state |= ALLOC_DATA_IN_URB | SUBMIT_DATA_IN_URB;
	case DMA_TO_DEVICE:
		cmdinfo->state |= ALLOC_DATA_OUT_URB | SUBMIT_DATA_OUT_URB;
	case DMA_NONE:
		break;
	}

	if (!devinfo->use_streams)
		cmdinfo->state &= ~(SUBMIT_DATA_IN_URB | SUBMIT_DATA_OUT_URB);

	err = uas_submit_urbs(cmnd, devinfo, GFP_ATOMIC);
	if (err) {
		/* If we did nothing, give up now */
		if (cmdinfo->state & SUBMIT_STATUS_URB) {
			spin_unlock_irqrestore(&devinfo->lock, flags);
			return SCSI_MLQUEUE_DEVICE_BUSY;
		}
		uas_add_work(cmdinfo);
	}

	devinfo->cmnd[idx] = cmnd;
	spin_unlock_irqrestore(&devinfo->lock, flags);
	return 0;
}

static DEF_SCSI_QCMD(uas_queuecommand)

/*
 * For now we do not support actually sending an abort to the device, so
 * this eh always fails. Still we must define it to make sure that we've
 * dropped all references to the cmnd in question once this function exits.
 */
static int uas_eh_abort_handler(struct scsi_cmnd *cmnd)
{
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	struct uas_dev_info *devinfo = (void *)cmnd->device->hostdata;
	struct urb *data_in_urb = NULL;
	struct urb *data_out_urb = NULL;
	unsigned long flags;

	spin_lock_irqsave(&devinfo->lock, flags);

	uas_log_cmd_state(cmnd, __func__, 0);

	/* Ensure that try_complete does not call scsi_done */
	cmdinfo->state |= COMMAND_ABORTED;

	/* Drop all refs to this cmnd, kill data urbs to break their ref */
	devinfo->cmnd[cmdinfo->uas_tag - 1] = NULL;
	if (cmdinfo->state & DATA_IN_URB_INFLIGHT)
		data_in_urb = usb_get_urb(cmdinfo->data_in_urb);
	if (cmdinfo->state & DATA_OUT_URB_INFLIGHT)
		data_out_urb = usb_get_urb(cmdinfo->data_out_urb);

	uas_free_unsubmitted_urbs(cmnd);

	spin_unlock_irqrestore(&devinfo->lock, flags);

	if (data_in_urb) {
		usb_kill_urb(data_in_urb);
		usb_put_urb(data_in_urb);
	}
	if (data_out_urb) {
		usb_kill_urb(data_out_urb);
		usb_put_urb(data_out_urb);
	}

	return FAILED;
}

static int uas_eh_bus_reset_handler(struct scsi_cmnd *cmnd)
{
	struct scsi_device *sdev = cmnd->device;
	struct uas_dev_info *devinfo = sdev->hostdata;
	struct usb_device *udev = devinfo->udev;
	unsigned long flags;
	int err;

	err = usb_lock_device_for_reset(udev, devinfo->intf);
	if (err) {
		shost_printk(KERN_ERR, sdev->host,
			     "%s FAILED to get lock err %d\n", __func__, err);
		return FAILED;
	}

	shost_printk(KERN_INFO, sdev->host, "%s start\n", __func__);

	spin_lock_irqsave(&devinfo->lock, flags);
	devinfo->resetting = 1;
	spin_unlock_irqrestore(&devinfo->lock, flags);

	usb_kill_anchored_urbs(&devinfo->cmd_urbs);
	usb_kill_anchored_urbs(&devinfo->sense_urbs);
	usb_kill_anchored_urbs(&devinfo->data_urbs);
	uas_zap_pending(devinfo, DID_RESET);

	err = usb_reset_device(udev);

	spin_lock_irqsave(&devinfo->lock, flags);
	devinfo->resetting = 0;
	spin_unlock_irqrestore(&devinfo->lock, flags);

	usb_unlock_device(udev);

	if (err) {
		shost_printk(KERN_INFO, sdev->host, "%s FAILED err %d\n",
			     __func__, err);
		return FAILED;
	}

	shost_printk(KERN_INFO, sdev->host, "%s success\n", __func__);
	return SUCCESS;
}

static int uas_slave_alloc(struct scsi_device *sdev)
{
	struct uas_dev_info *devinfo =
		(struct uas_dev_info *)sdev->host->hostdata;

	sdev->hostdata = devinfo;

	/* USB has unusual DMA-alignment requirements: Although the
	 * starting address of each scatter-gather element doesn't matter,
	 * the length of each element except the last must be divisible
	 * by the Bulk maxpacket value.  There's currently no way to
	 * express this by block-layer constraints, so we'll cop out
	 * and simply require addresses to be aligned at 512-byte
	 * boundaries.  This is okay since most block I/O involves
	 * hardware sectors that are multiples of 512 bytes in length,
	 * and since host controllers up through USB 2.0 have maxpacket
	 * values no larger than 512.
	 *
	 * But it doesn't suffice for Wireless USB, where Bulk maxpacket
	 * values can be as large as 2048.  To make that work properly
	 * will require changes to the block layer.
	 */
	blk_queue_update_dma_alignment(sdev->request_queue, (512 - 1));

	if (devinfo->flags & US_FL_MAX_SECTORS_64)
		blk_queue_max_hw_sectors(sdev->request_queue, 64);
	else if (devinfo->flags & US_FL_MAX_SECTORS_240)
		blk_queue_max_hw_sectors(sdev->request_queue, 240);

	return 0;
}

static int uas_slave_configure(struct scsi_device *sdev)
{
	struct uas_dev_info *devinfo = sdev->hostdata;

	if (devinfo->flags & US_FL_NO_REPORT_OPCODES)
		sdev->no_report_opcodes = 1;

	scsi_change_queue_depth(sdev, devinfo->qdepth - 2);
	return 0;
}

static struct scsi_host_template uas_host_template = {
	.module = THIS_MODULE,
	.name = "uas",
	.queuecommand = uas_queuecommand,
	.slave_alloc = uas_slave_alloc,
	.slave_configure = uas_slave_configure,
	.eh_abort_handler = uas_eh_abort_handler,
	.eh_bus_reset_handler = uas_eh_bus_reset_handler,
	.can_queue = 65536,	/* Is there a limit on the _host_ ? */
	.this_id = -1,
	.sg_tablesize = SG_NONE,
	.skip_settle_delay = 1,
	.use_blk_tags = 1,
};

#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName, useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax), \
	.driver_info = (flags) }

static struct usb_device_id uas_usb_ids[] = {
#	include "unusual_uas.h"
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, USB_SC_SCSI, USB_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, USB_SC_SCSI, USB_PR_UAS) },
	{ }
};
MODULE_DEVICE_TABLE(usb, uas_usb_ids);

#undef UNUSUAL_DEV

static int uas_switch_interface(struct usb_device *udev,
				struct usb_interface *intf)
{
	int alt;

	alt = uas_find_uas_alt_setting(intf);
	if (alt < 0)
		return alt;

	return usb_set_interface(udev,
			intf->altsetting[0].desc.bInterfaceNumber, alt);
}

static int uas_configure_endpoints(struct uas_dev_info *devinfo)
{
	struct usb_host_endpoint *eps[4] = { };
	struct usb_device *udev = devinfo->udev;
	int r;

	r = uas_find_endpoints(devinfo->intf->cur_altsetting, eps);
	if (r)
		return r;

	devinfo->cmd_pipe = usb_sndbulkpipe(udev,
					    usb_endpoint_num(&eps[0]->desc));
	devinfo->status_pipe = usb_rcvbulkpipe(udev,
					    usb_endpoint_num(&eps[1]->desc));
	devinfo->data_in_pipe = usb_rcvbulkpipe(udev,
					    usb_endpoint_num(&eps[2]->desc));
	devinfo->data_out_pipe = usb_sndbulkpipe(udev,
					    usb_endpoint_num(&eps[3]->desc));

	if (udev->speed < USB_SPEED_SUPER) {
		devinfo->qdepth = 32;
		devinfo->use_streams = 0;
	} else {
		devinfo->qdepth = usb_alloc_streams(devinfo->intf, eps + 1,
						    3, MAX_CMNDS, GFP_NOIO);
		if (devinfo->qdepth < 0)
			return devinfo->qdepth;
		devinfo->use_streams = 1;
	}

	return 0;
}

static void uas_free_streams(struct uas_dev_info *devinfo)
{
	struct usb_device *udev = devinfo->udev;
	struct usb_host_endpoint *eps[3];

	eps[0] = usb_pipe_endpoint(udev, devinfo->status_pipe);
	eps[1] = usb_pipe_endpoint(udev, devinfo->data_in_pipe);
	eps[2] = usb_pipe_endpoint(udev, devinfo->data_out_pipe);
	usb_free_streams(devinfo->intf, eps, 3, GFP_NOIO);
}

static int uas_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int result = -ENOMEM;
	struct Scsi_Host *shost = NULL;
	struct uas_dev_info *devinfo;
	struct usb_device *udev = interface_to_usbdev(intf);
	unsigned long dev_flags;

	if (!uas_use_uas_driver(intf, id, &dev_flags))
		return -ENODEV;

	if (uas_switch_interface(udev, intf))
		return -ENODEV;

	shost = scsi_host_alloc(&uas_host_template,
				sizeof(struct uas_dev_info));
	if (!shost)
		goto set_alt0;

	shost->max_cmd_len = 16 + 252;
	shost->max_id = 1;
	shost->max_lun = 256;
	shost->max_channel = 0;
	shost->sg_tablesize = udev->bus->sg_tablesize;

	devinfo = (struct uas_dev_info *)shost->hostdata;
	devinfo->intf = intf;
	devinfo->udev = udev;
	devinfo->resetting = 0;
	devinfo->shutdown = 0;
	devinfo->flags = dev_flags;
	init_usb_anchor(&devinfo->cmd_urbs);
	init_usb_anchor(&devinfo->sense_urbs);
	init_usb_anchor(&devinfo->data_urbs);
	spin_lock_init(&devinfo->lock);
	INIT_WORK(&devinfo->work, uas_do_work);

	result = uas_configure_endpoints(devinfo);
	if (result)
		goto set_alt0;

	result = scsi_init_shared_tag_map(shost, devinfo->qdepth - 2);
	if (result)
		goto free_streams;

	usb_set_intfdata(intf, shost);
	result = scsi_add_host(shost, &intf->dev);
	if (result)
		goto free_streams;

	scsi_scan_host(shost);
	return result;

free_streams:
	uas_free_streams(devinfo);
	usb_set_intfdata(intf, NULL);
set_alt0:
	usb_set_interface(udev, intf->altsetting[0].desc.bInterfaceNumber, 0);
	if (shost)
		scsi_host_put(shost);
	return result;
}

static int uas_cmnd_list_empty(struct uas_dev_info *devinfo)
{
	unsigned long flags;
	int i, r = 1;

	spin_lock_irqsave(&devinfo->lock, flags);

	for (i = 0; i < devinfo->qdepth; i++) {
		if (devinfo->cmnd[i]) {
			r = 0; /* Not empty */
			break;
		}
	}

	spin_unlock_irqrestore(&devinfo->lock, flags);

	return r;
}

/*
 * Wait for any pending cmnds to complete, on usb-2 sense_urbs may temporarily
 * get empty while there still is more work to do due to sense-urbs completing
 * with a READ/WRITE_READY iu code, so keep waiting until the list gets empty.
 */
static int uas_wait_for_pending_cmnds(struct uas_dev_info *devinfo)
{
	unsigned long start_time;
	int r;

	start_time = jiffies;
	do {
		flush_work(&devinfo->work);

		r = usb_wait_anchor_empty_timeout(&devinfo->sense_urbs, 5000);
		if (r == 0)
			return -ETIME;

		r = usb_wait_anchor_empty_timeout(&devinfo->data_urbs, 500);
		if (r == 0)
			return -ETIME;

		if (time_after(jiffies, start_time + 5 * HZ))
			return -ETIME;
	} while (!uas_cmnd_list_empty(devinfo));

	return 0;
}

static int uas_pre_reset(struct usb_interface *intf)
{
	struct Scsi_Host *shost = usb_get_intfdata(intf);
	struct uas_dev_info *devinfo = (struct uas_dev_info *)shost->hostdata;
	unsigned long flags;

	if (devinfo->shutdown)
		return 0;

	/* Block new requests */
	spin_lock_irqsave(shost->host_lock, flags);
	scsi_block_requests(shost);
	spin_unlock_irqrestore(shost->host_lock, flags);

	if (uas_wait_for_pending_cmnds(devinfo) != 0) {
		shost_printk(KERN_ERR, shost, "%s: timed out\n", __func__);
		scsi_unblock_requests(shost);
		return 1;
	}

	uas_free_streams(devinfo);

	return 0;
}

static int uas_post_reset(struct usb_interface *intf)
{
	struct Scsi_Host *shost = usb_get_intfdata(intf);
	struct uas_dev_info *devinfo = (struct uas_dev_info *)shost->hostdata;
	unsigned long flags;
	int err;

	if (devinfo->shutdown)
		return 0;

	err = uas_configure_endpoints(devinfo);
	if (err) {
		shost_printk(KERN_ERR, shost,
			     "%s: alloc streams error %d after reset",
			     __func__, err);
		return 1;
	}

	spin_lock_irqsave(shost->host_lock, flags);
	scsi_report_bus_reset(shost, 0);
	spin_unlock_irqrestore(shost->host_lock, flags);

	scsi_unblock_requests(shost);

	return 0;
}

static int uas_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct Scsi_Host *shost = usb_get_intfdata(intf);
	struct uas_dev_info *devinfo = (struct uas_dev_info *)shost->hostdata;

	if (uas_wait_for_pending_cmnds(devinfo) != 0) {
		shost_printk(KERN_ERR, shost, "%s: timed out\n", __func__);
		return -ETIME;
	}

	return 0;
}

static int uas_resume(struct usb_interface *intf)
{
	return 0;
}

static int uas_reset_resume(struct usb_interface *intf)
{
	struct Scsi_Host *shost = usb_get_intfdata(intf);
	struct uas_dev_info *devinfo = (struct uas_dev_info *)shost->hostdata;
	unsigned long flags;
	int err;

	err = uas_configure_endpoints(devinfo);
	if (err) {
		shost_printk(KERN_ERR, shost,
			     "%s: alloc streams error %d after reset",
			     __func__, err);
		return -EIO;
	}

	spin_lock_irqsave(shost->host_lock, flags);
	scsi_report_bus_reset(shost, 0);
	spin_unlock_irqrestore(shost->host_lock, flags);

	return 0;
}

static void uas_disconnect(struct usb_interface *intf)
{
	struct Scsi_Host *shost = usb_get_intfdata(intf);
	struct uas_dev_info *devinfo = (struct uas_dev_info *)shost->hostdata;
	unsigned long flags;

	spin_lock_irqsave(&devinfo->lock, flags);
	devinfo->resetting = 1;
	spin_unlock_irqrestore(&devinfo->lock, flags);

	cancel_work_sync(&devinfo->work);
	usb_kill_anchored_urbs(&devinfo->cmd_urbs);
	usb_kill_anchored_urbs(&devinfo->sense_urbs);
	usb_kill_anchored_urbs(&devinfo->data_urbs);
	uas_zap_pending(devinfo, DID_NO_CONNECT);

	scsi_remove_host(shost);
	uas_free_streams(devinfo);
	scsi_host_put(shost);
}

/*
 * Put the device back in usb-storage mode on shutdown, as some BIOS-es
 * hang on reboot when the device is still in uas mode. Note the reset is
 * necessary as some devices won't revert to usb-storage mode without it.
 */
static void uas_shutdown(struct device *dev)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct usb_device *udev = interface_to_usbdev(intf);
	struct Scsi_Host *shost = usb_get_intfdata(intf);
	struct uas_dev_info *devinfo = (struct uas_dev_info *)shost->hostdata;

	if (system_state != SYSTEM_RESTART)
		return;

	devinfo->shutdown = 1;
	uas_free_streams(devinfo);
	usb_set_interface(udev, intf->altsetting[0].desc.bInterfaceNumber, 0);
	usb_reset_device(udev);
}

static struct usb_driver uas_driver = {
	.name = "uas",
	.probe = uas_probe,
	.disconnect = uas_disconnect,
	.pre_reset = uas_pre_reset,
	.post_reset = uas_post_reset,
	.suspend = uas_suspend,
	.resume = uas_resume,
	.reset_resume = uas_reset_resume,
	.drvwrap.driver.shutdown = uas_shutdown,
	.id_table = uas_usb_ids,
};

module_usb_driver(uas_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(
	"Hans de Goede <hdegoede@redhat.com>, Matthew Wilcox and Sarah Sharp");
