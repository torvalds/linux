/*
 * USB Attached SCSI
 * Note that this is not the same as the USB Mass Storage driver
 *
 * Copyright Matthew Wilcox for Intel Corp, 2010
 * Copyright Sarah Sharp for Intel Corp, 2010
 *
 * Distributed under the terms of the GNU GPL, version two.
 */

#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/usb/storage.h>

#include <scsi/scsi.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>

/* Common header for all IUs */
struct iu {
	__u8 iu_id;
	__u8 rsvd1;
	__be16 tag;
};

enum {
	IU_ID_COMMAND		= 0x01,
	IU_ID_STATUS		= 0x03,
	IU_ID_RESPONSE		= 0x04,
	IU_ID_TASK_MGMT		= 0x05,
	IU_ID_READ_READY	= 0x06,
	IU_ID_WRITE_READY	= 0x07,
};

struct command_iu {
	__u8 iu_id;
	__u8 rsvd1;
	__be16 tag;
	__u8 prio_attr;
	__u8 rsvd5;
	__u8 len;
	__u8 rsvd7;
	struct scsi_lun lun;
	__u8 cdb[16];	/* XXX: Overflow-checking tools may misunderstand */
};

struct sense_iu {
	__u8 iu_id;
	__u8 rsvd1;
	__be16 tag;
	__be16 status_qual;
	__u8 status;
	__u8 service_response;
	__u8 rsvd8[6];
	__be16 len;
	__u8 sense[SCSI_SENSE_BUFFERSIZE];
};

/*
 * The r00-r01c specs define this version of the SENSE IU data structure.
 * It's still in use by several different firmware releases.
 */
struct sense_iu_old {
	__u8 iu_id;
	__u8 rsvd1;
	__be16 tag;
	__be16 len;
	__u8 status;
	__u8 service_response;
	__u8 sense[SCSI_SENSE_BUFFERSIZE];
};

enum {
	CMD_PIPE_ID		= 1,
	STATUS_PIPE_ID		= 2,
	DATA_IN_PIPE_ID		= 3,
	DATA_OUT_PIPE_ID	= 4,

	UAS_SIMPLE_TAG		= 0,
	UAS_HEAD_TAG		= 1,
	UAS_ORDERED_TAG		= 2,
	UAS_ACA			= 4,
};

struct uas_dev_info {
	struct usb_interface *intf;
	struct usb_device *udev;
	int qdepth;
	unsigned cmd_pipe, status_pipe, data_in_pipe, data_out_pipe;
	unsigned use_streams:1;
	unsigned uas_sense_old:1;
};

enum {
	ALLOC_SENSE_URB		= (1 << 0),
	SUBMIT_SENSE_URB	= (1 << 1),
	ALLOC_DATA_IN_URB	= (1 << 2),
	SUBMIT_DATA_IN_URB	= (1 << 3),
	ALLOC_DATA_OUT_URB	= (1 << 4),
	SUBMIT_DATA_OUT_URB	= (1 << 5),
	ALLOC_CMD_URB		= (1 << 6),
	SUBMIT_CMD_URB		= (1 << 7),
};

/* Overrides scsi_pointer */
struct uas_cmd_info {
	unsigned int state;
	unsigned int stream;
	struct urb *cmd_urb;
	struct urb *sense_urb;
	struct urb *data_in_urb;
	struct urb *data_out_urb;
	struct list_head list;
};

/* I hate forward declarations, but I actually have a loop */
static int uas_submit_urbs(struct scsi_cmnd *cmnd,
				struct uas_dev_info *devinfo, gfp_t gfp);

static DEFINE_SPINLOCK(uas_work_lock);
static LIST_HEAD(uas_work_list);

static void uas_do_work(struct work_struct *work)
{
	struct uas_cmd_info *cmdinfo;
	struct list_head list;

	spin_lock_irq(&uas_work_lock);
	list_replace_init(&uas_work_list, &list);
	spin_unlock_irq(&uas_work_lock);

	list_for_each_entry(cmdinfo, &list, list) {
		struct scsi_pointer *scp = (void *)cmdinfo;
		struct scsi_cmnd *cmnd = container_of(scp,
							struct scsi_cmnd, SCp);
		uas_submit_urbs(cmnd, cmnd->device->hostdata, GFP_KERNEL);
	}
}

static DECLARE_WORK(uas_work, uas_do_work);

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
	if (sdev->current_cmnd)
		sdev->current_cmnd = NULL;
	cmnd->scsi_done(cmnd);
	usb_free_urb(urb);
}

static void uas_sense_old(struct urb *urb, struct scsi_cmnd *cmnd)
{
	struct sense_iu_old *sense_iu = urb->transfer_buffer;
	struct scsi_device *sdev = cmnd->device;

	if (urb->actual_length > 8) {
		unsigned len = be16_to_cpup(&sense_iu->len) - 2;
		if (len + 8 != urb->actual_length) {
			int newlen = min(len + 8, urb->actual_length) - 8;
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
	if (sdev->current_cmnd)
		sdev->current_cmnd = NULL;
	cmnd->scsi_done(cmnd);
	usb_free_urb(urb);
}

static void uas_xfer_data(struct urb *urb, struct scsi_cmnd *cmnd,
							unsigned direction)
{
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	int err;

	cmdinfo->state = direction | SUBMIT_SENSE_URB;
	err = uas_submit_urbs(cmnd, cmnd->device->hostdata, GFP_ATOMIC);
	if (err) {
		spin_lock(&uas_work_lock);
		list_add_tail(&cmdinfo->list, &uas_work_list);
		spin_unlock(&uas_work_lock);
		schedule_work(&uas_work);
	}
}

static void uas_stat_cmplt(struct urb *urb)
{
	struct iu *iu = urb->transfer_buffer;
	struct scsi_device *sdev = urb->context;
	struct uas_dev_info *devinfo = sdev->hostdata;
	struct scsi_cmnd *cmnd;
	u16 tag;

	if (urb->status) {
		dev_err(&urb->dev->dev, "URB BAD STATUS %d\n", urb->status);
		usb_free_urb(urb);
		return;
	}

	tag = be16_to_cpup(&iu->tag) - 1;
	if (sdev->current_cmnd)
		cmnd = sdev->current_cmnd;
	else
		cmnd = scsi_find_tag(sdev, tag);
	if (!cmnd)
		return;

	switch (iu->iu_id) {
	case IU_ID_STATUS:
		if (urb->actual_length < 16)
			devinfo->uas_sense_old = 1;
		if (devinfo->uas_sense_old)
			uas_sense_old(urb, cmnd);
		else
			uas_sense(urb, cmnd);
		break;
	case IU_ID_READ_READY:
		uas_xfer_data(urb, cmnd, SUBMIT_DATA_IN_URB);
		break;
	case IU_ID_WRITE_READY:
		uas_xfer_data(urb, cmnd, SUBMIT_DATA_OUT_URB);
		break;
	default:
		scmd_printk(KERN_ERR, cmnd,
			"Bogus IU (%d) received on status pipe\n", iu->iu_id);
	}
}

static void uas_data_cmplt(struct urb *urb)
{
	struct scsi_data_buffer *sdb = urb->context;
	sdb->resid = sdb->length - urb->actual_length;
	usb_free_urb(urb);
}

static struct urb *uas_alloc_data_urb(struct uas_dev_info *devinfo, gfp_t gfp,
				unsigned int pipe, u16 stream_id,
				struct scsi_data_buffer *sdb,
				enum dma_data_direction dir)
{
	struct usb_device *udev = devinfo->udev;
	struct urb *urb = usb_alloc_urb(0, gfp);

	if (!urb)
		goto out;
	usb_fill_bulk_urb(urb, udev, pipe, NULL, sdb->length, uas_data_cmplt,
									sdb);
	if (devinfo->use_streams)
		urb->stream_id = stream_id;
	urb->num_sgs = udev->bus->sg_tablesize ? sdb->table.nents : 0;
	urb->sg = sdb->table.sgl;
 out:
	return urb;
}

static struct urb *uas_alloc_sense_urb(struct uas_dev_info *devinfo, gfp_t gfp,
					struct scsi_cmnd *cmnd, u16 stream_id)
{
	struct usb_device *udev = devinfo->udev;
	struct urb *urb = usb_alloc_urb(0, gfp);
	struct sense_iu *iu;

	if (!urb)
		goto out;

	iu = kmalloc(sizeof(*iu), gfp);
	if (!iu)
		goto free;

	usb_fill_bulk_urb(urb, udev, devinfo->status_pipe, iu, sizeof(*iu),
						uas_stat_cmplt, cmnd->device);
	urb->stream_id = stream_id;
	urb->transfer_flags |= URB_FREE_BUFFER;
 out:
	return urb;
 free:
	usb_free_urb(urb);
	return NULL;
}

static struct urb *uas_alloc_cmd_urb(struct uas_dev_info *devinfo, gfp_t gfp,
					struct scsi_cmnd *cmnd, u16 stream_id)
{
	struct usb_device *udev = devinfo->udev;
	struct scsi_device *sdev = cmnd->device;
	struct urb *urb = usb_alloc_urb(0, gfp);
	struct command_iu *iu;
	int len;

	if (!urb)
		goto out;

	len = cmnd->cmd_len - 16;
	if (len < 0)
		len = 0;
	len = ALIGN(len, 4);
	iu = kmalloc(sizeof(*iu) + len, gfp);
	if (!iu)
		goto free;

	iu->iu_id = IU_ID_COMMAND;
	iu->tag = cpu_to_be16(stream_id);
	iu->prio_attr = UAS_SIMPLE_TAG;
	iu->len = len;
	int_to_scsilun(sdev->lun, &iu->lun);
	memcpy(iu->cdb, cmnd->cmnd, cmnd->cmd_len);

	usb_fill_bulk_urb(urb, udev, devinfo->cmd_pipe, iu, sizeof(*iu) + len,
							usb_free_urb, NULL);
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

static int uas_submit_urbs(struct scsi_cmnd *cmnd,
					struct uas_dev_info *devinfo, gfp_t gfp)
{
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;

	if (cmdinfo->state & ALLOC_SENSE_URB) {
		cmdinfo->sense_urb = uas_alloc_sense_urb(devinfo, gfp, cmnd,
							cmdinfo->stream);
		if (!cmdinfo->sense_urb)
			return SCSI_MLQUEUE_DEVICE_BUSY;
		cmdinfo->state &= ~ALLOC_SENSE_URB;
	}

	if (cmdinfo->state & SUBMIT_SENSE_URB) {
		if (usb_submit_urb(cmdinfo->sense_urb, gfp)) {
			scmd_printk(KERN_INFO, cmnd,
					"sense urb submission failure\n");
			return SCSI_MLQUEUE_DEVICE_BUSY;
		}
		cmdinfo->state &= ~SUBMIT_SENSE_URB;
	}

	if (cmdinfo->state & ALLOC_DATA_IN_URB) {
		cmdinfo->data_in_urb = uas_alloc_data_urb(devinfo, gfp,
					devinfo->data_in_pipe, cmdinfo->stream,
					scsi_in(cmnd), DMA_FROM_DEVICE);
		if (!cmdinfo->data_in_urb)
			return SCSI_MLQUEUE_DEVICE_BUSY;
		cmdinfo->state &= ~ALLOC_DATA_IN_URB;
	}

	if (cmdinfo->state & SUBMIT_DATA_IN_URB) {
		if (usb_submit_urb(cmdinfo->data_in_urb, gfp)) {
			scmd_printk(KERN_INFO, cmnd,
					"data in urb submission failure\n");
			return SCSI_MLQUEUE_DEVICE_BUSY;
		}
		cmdinfo->state &= ~SUBMIT_DATA_IN_URB;
	}

	if (cmdinfo->state & ALLOC_DATA_OUT_URB) {
		cmdinfo->data_out_urb = uas_alloc_data_urb(devinfo, gfp,
					devinfo->data_out_pipe, cmdinfo->stream,
					scsi_out(cmnd), DMA_TO_DEVICE);
		if (!cmdinfo->data_out_urb)
			return SCSI_MLQUEUE_DEVICE_BUSY;
		cmdinfo->state &= ~ALLOC_DATA_OUT_URB;
	}

	if (cmdinfo->state & SUBMIT_DATA_OUT_URB) {
		if (usb_submit_urb(cmdinfo->data_out_urb, gfp)) {
			scmd_printk(KERN_INFO, cmnd,
					"data out urb submission failure\n");
			return SCSI_MLQUEUE_DEVICE_BUSY;
		}
		cmdinfo->state &= ~SUBMIT_DATA_OUT_URB;
	}

	if (cmdinfo->state & ALLOC_CMD_URB) {
		cmdinfo->cmd_urb = uas_alloc_cmd_urb(devinfo, gfp, cmnd,
							cmdinfo->stream);
		if (!cmdinfo->cmd_urb)
			return SCSI_MLQUEUE_DEVICE_BUSY;
		cmdinfo->state &= ~ALLOC_CMD_URB;
	}

	if (cmdinfo->state & SUBMIT_CMD_URB) {
		if (usb_submit_urb(cmdinfo->cmd_urb, gfp)) {
			scmd_printk(KERN_INFO, cmnd,
					"cmd urb submission failure\n");
			return SCSI_MLQUEUE_DEVICE_BUSY;
		}
		cmdinfo->state &= ~SUBMIT_CMD_URB;
	}

	return 0;
}

static int uas_queuecommand_lck(struct scsi_cmnd *cmnd,
					void (*done)(struct scsi_cmnd *))
{
	struct scsi_device *sdev = cmnd->device;
	struct uas_dev_info *devinfo = sdev->hostdata;
	struct uas_cmd_info *cmdinfo = (void *)&cmnd->SCp;
	int err;

	BUILD_BUG_ON(sizeof(struct uas_cmd_info) > sizeof(struct scsi_pointer));

	if (!cmdinfo->sense_urb && sdev->current_cmnd)
		return SCSI_MLQUEUE_DEVICE_BUSY;

	if (blk_rq_tagged(cmnd->request)) {
		cmdinfo->stream = cmnd->request->tag + 1;
	} else {
		sdev->current_cmnd = cmnd;
		cmdinfo->stream = 1;
	}

	cmnd->scsi_done = done;

	cmdinfo->state = ALLOC_SENSE_URB | SUBMIT_SENSE_URB |
			ALLOC_CMD_URB | SUBMIT_CMD_URB;

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

	if (!devinfo->use_streams) {
		cmdinfo->state &= ~(SUBMIT_DATA_IN_URB | SUBMIT_DATA_OUT_URB);
		cmdinfo->stream = 0;
	}

	err = uas_submit_urbs(cmnd, devinfo, GFP_ATOMIC);
	if (err) {
		/* If we did nothing, give up now */
		if (cmdinfo->state & SUBMIT_SENSE_URB) {
			usb_free_urb(cmdinfo->sense_urb);
			return SCSI_MLQUEUE_DEVICE_BUSY;
		}
		spin_lock(&uas_work_lock);
		list_add_tail(&cmdinfo->list, &uas_work_list);
		spin_unlock(&uas_work_lock);
		schedule_work(&uas_work);
	}

	return 0;
}

static DEF_SCSI_QCMD(uas_queuecommand)

static int uas_eh_abort_handler(struct scsi_cmnd *cmnd)
{
	struct scsi_device *sdev = cmnd->device;
	sdev_printk(KERN_INFO, sdev, "%s tag %d\n", __func__,
							cmnd->request->tag);

/* XXX: Send ABORT TASK Task Management command */
	return FAILED;
}

static int uas_eh_device_reset_handler(struct scsi_cmnd *cmnd)
{
	struct scsi_device *sdev = cmnd->device;
	sdev_printk(KERN_INFO, sdev, "%s tag %d\n", __func__,
							cmnd->request->tag);

/* XXX: Send LOGICAL UNIT RESET Task Management command */
	return FAILED;
}

static int uas_eh_target_reset_handler(struct scsi_cmnd *cmnd)
{
	struct scsi_device *sdev = cmnd->device;
	sdev_printk(KERN_INFO, sdev, "%s tag %d\n", __func__,
							cmnd->request->tag);

/* XXX: Can we reset just the one USB interface?
 * Would calling usb_set_interface() have the right effect?
 */
	return FAILED;
}

static int uas_eh_bus_reset_handler(struct scsi_cmnd *cmnd)
{
	struct scsi_device *sdev = cmnd->device;
	struct uas_dev_info *devinfo = sdev->hostdata;
	struct usb_device *udev = devinfo->udev;

	sdev_printk(KERN_INFO, sdev, "%s tag %d\n", __func__,
							cmnd->request->tag);

	if (usb_reset_device(udev))
		return SUCCESS;

	return FAILED;
}

static int uas_slave_alloc(struct scsi_device *sdev)
{
	sdev->hostdata = (void *)sdev->host->hostdata[0];
	return 0;
}

static int uas_slave_configure(struct scsi_device *sdev)
{
	struct uas_dev_info *devinfo = sdev->hostdata;
	scsi_set_tag_type(sdev, MSG_ORDERED_TAG);
	scsi_activate_tcq(sdev, devinfo->qdepth - 1);
	return 0;
}

static struct scsi_host_template uas_host_template = {
	.module = THIS_MODULE,
	.name = "uas",
	.queuecommand = uas_queuecommand,
	.slave_alloc = uas_slave_alloc,
	.slave_configure = uas_slave_configure,
	.eh_abort_handler = uas_eh_abort_handler,
	.eh_device_reset_handler = uas_eh_device_reset_handler,
	.eh_target_reset_handler = uas_eh_target_reset_handler,
	.eh_bus_reset_handler = uas_eh_bus_reset_handler,
	.can_queue = 65536,	/* Is there a limit on the _host_ ? */
	.this_id = -1,
	.sg_tablesize = SG_NONE,
	.cmd_per_lun = 1,	/* until we override it */
	.skip_settle_delay = 1,
	.ordered_tag = 1,
};

static struct usb_device_id uas_usb_ids[] = {
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, USB_SC_SCSI, USB_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, USB_SC_SCSI, USB_PR_UAS) },
	/* 0xaa is a prototype device I happen to have access to */
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, USB_SC_SCSI, 0xaa) },
	{ }
};
MODULE_DEVICE_TABLE(usb, uas_usb_ids);

static void uas_configure_endpoints(struct uas_dev_info *devinfo)
{
	struct usb_host_endpoint *eps[4] = { };
	struct usb_interface *intf = devinfo->intf;
	struct usb_device *udev = devinfo->udev;
	struct usb_host_endpoint *endpoint = intf->cur_altsetting->endpoint;
	unsigned i, n_endpoints = intf->cur_altsetting->desc.bNumEndpoints;

	devinfo->uas_sense_old = 0;

	for (i = 0; i < n_endpoints; i++) {
		unsigned char *extra = endpoint[i].extra;
		int len = endpoint[i].extralen;
		while (len > 1) {
			if (extra[1] == USB_DT_PIPE_USAGE) {
				unsigned pipe_id = extra[2];
				if (pipe_id > 0 && pipe_id < 5)
					eps[pipe_id - 1] = &endpoint[i];
				break;
			}
			len -= extra[0];
			extra += extra[0];
		}
	}

	/*
	 * Assume that if we didn't find a control pipe descriptor, we're
	 * using a device with old firmware that happens to be set up like
	 * this.
	 */
	if (!eps[0]) {
		devinfo->cmd_pipe = usb_sndbulkpipe(udev, 1);
		devinfo->status_pipe = usb_rcvbulkpipe(udev, 1);
		devinfo->data_in_pipe = usb_rcvbulkpipe(udev, 2);
		devinfo->data_out_pipe = usb_sndbulkpipe(udev, 2);

		eps[1] = usb_pipe_endpoint(udev, devinfo->status_pipe);
		eps[2] = usb_pipe_endpoint(udev, devinfo->data_in_pipe);
		eps[3] = usb_pipe_endpoint(udev, devinfo->data_out_pipe);
	} else {
		devinfo->cmd_pipe = usb_sndbulkpipe(udev,
						eps[0]->desc.bEndpointAddress);
		devinfo->status_pipe = usb_rcvbulkpipe(udev,
						eps[1]->desc.bEndpointAddress);
		devinfo->data_in_pipe = usb_rcvbulkpipe(udev,
						eps[2]->desc.bEndpointAddress);
		devinfo->data_out_pipe = usb_sndbulkpipe(udev,
						eps[3]->desc.bEndpointAddress);
	}

	devinfo->qdepth = usb_alloc_streams(devinfo->intf, eps + 1, 3, 256,
								GFP_KERNEL);
	if (devinfo->qdepth < 0) {
		devinfo->qdepth = 256;
		devinfo->use_streams = 0;
	} else {
		devinfo->use_streams = 1;
	}
}

/*
 * XXX: What I'd like to do here is register a SCSI host for each USB host in
 * the system.  Follow usb-storage's design of registering a SCSI host for
 * each USB device for the moment.  Can implement this by walking up the
 * USB hierarchy until we find a USB host.
 */
static int uas_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int result;
	struct Scsi_Host *shost;
	struct uas_dev_info *devinfo;
	struct usb_device *udev = interface_to_usbdev(intf);

	if (id->bInterfaceProtocol == 0x50) {
		int ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
/* XXX: Shouldn't assume that 1 is the alternative we want */
		int ret = usb_set_interface(udev, ifnum, 1);
		if (ret)
			return -ENODEV;
	}

	devinfo = kmalloc(sizeof(struct uas_dev_info), GFP_KERNEL);
	if (!devinfo)
		return -ENOMEM;

	result = -ENOMEM;
	shost = scsi_host_alloc(&uas_host_template, sizeof(void *));
	if (!shost)
		goto free;

	shost->max_cmd_len = 16 + 252;
	shost->max_id = 1;
	shost->sg_tablesize = udev->bus->sg_tablesize;

	result = scsi_add_host(shost, &intf->dev);
	if (result)
		goto free;
	shost->hostdata[0] = (unsigned long)devinfo;

	devinfo->intf = intf;
	devinfo->udev = udev;
	uas_configure_endpoints(devinfo);

	scsi_scan_host(shost);
	usb_set_intfdata(intf, shost);
	return result;
 free:
	kfree(devinfo);
	if (shost)
		scsi_host_put(shost);
	return result;
}

static int uas_pre_reset(struct usb_interface *intf)
{
/* XXX: Need to return 1 if it's not our device in error handling */
	return 0;
}

static int uas_post_reset(struct usb_interface *intf)
{
/* XXX: Need to return 1 if it's not our device in error handling */
	return 0;
}

static void uas_disconnect(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_endpoint *eps[3];
	struct Scsi_Host *shost = usb_get_intfdata(intf);
	struct uas_dev_info *devinfo = (void *)shost->hostdata[0];

	scsi_remove_host(shost);

	eps[0] = usb_pipe_endpoint(udev, devinfo->status_pipe);
	eps[1] = usb_pipe_endpoint(udev, devinfo->data_in_pipe);
	eps[2] = usb_pipe_endpoint(udev, devinfo->data_out_pipe);
	usb_free_streams(intf, eps, 3, GFP_KERNEL);

	kfree(devinfo);
}

/*
 * XXX: Should this plug into libusual so we can auto-upgrade devices from
 * Bulk-Only to UAS?
 */
static struct usb_driver uas_driver = {
	.name = "uas",
	.probe = uas_probe,
	.disconnect = uas_disconnect,
	.pre_reset = uas_pre_reset,
	.post_reset = uas_post_reset,
	.id_table = uas_usb_ids,
};

static int uas_init(void)
{
	return usb_register(&uas_driver);
}

static void uas_exit(void)
{
	usb_deregister(&uas_driver);
}

module_init(uas_init);
module_exit(uas_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matthew Wilcox and Sarah Sharp");
