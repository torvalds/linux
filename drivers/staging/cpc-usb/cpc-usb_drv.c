/*
 * CPC-USB CAN Interface Kernel Driver
 *
 * Copyright (C) 2004-2009 EMS Dr. Thomas Wuensche
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/usb.h>

#include <linux/version.h>

#include <linux/proc_fs.h>

#include "cpc.h"

#include "cpc_int.h"
#include "cpcusb.h"

#include "sja2m16c.h"

/* Version Information */
#define DRIVER_AUTHOR  "Sebastian Haas <haas@ems-wuensche.com>"
#define DRIVER_DESC    "CPC-USB Driver for Linux Kernel 2.6"
#define DRIVER_VERSION CPC_DRIVER_VERSION

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");

/* Define these values to match your devices */
#define USB_CPCUSB_VENDOR_ID	0x12D6

#define USB_CPCUSB_M16C_PRODUCT_ID    0x0888
#define USB_CPCUSB_LPC2119_PRODUCT_ID 0x0444

#define CPC_USB_PROC_DIR     CPC_PROC_DIR "cpc-usb"

static struct proc_dir_entry *procDir;
static struct proc_dir_entry *procEntry;

/* Module parameters */
static int debug;
module_param(debug, int, S_IRUGO);

/* table of devices that work with this driver */
static struct usb_device_id cpcusb_table[] = {
	{USB_DEVICE(USB_CPCUSB_VENDOR_ID, USB_CPCUSB_M16C_PRODUCT_ID)},
	{USB_DEVICE(USB_CPCUSB_VENDOR_ID, USB_CPCUSB_LPC2119_PRODUCT_ID)},
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, cpcusb_table);

/* use to prevent kernel panic if driver is unloaded
 * while a programm has still open the device
 */
DECLARE_WAIT_QUEUE_HEAD(rmmodWq);
atomic_t useCount;

static CPC_USB_T *CPCUSB_Table[CPC_USB_CARD_CNT] = { 0 };
static unsigned int CPCUsbCnt;

/* prevent races between open() and disconnect() */
static DECLARE_MUTEX(disconnect_sem);

/* local function prototypes */
static ssize_t cpcusb_read(struct file *file, char *buffer, size_t count,
			   loff_t *ppos);
static ssize_t cpcusb_write(struct file *file, const char *buffer,
			    size_t count, loff_t *ppos);
static unsigned int cpcusb_poll(struct file *file, poll_table * wait);
static int cpcusb_open(struct inode *inode, struct file *file);
static int cpcusb_release(struct inode *inode, struct file *file);

static int cpcusb_probe(struct usb_interface *interface,
			const struct usb_device_id *id);
static void cpcusb_disconnect(struct usb_interface *interface);

static void cpcusb_read_bulk_callback(struct urb *urb);
static void cpcusb_write_bulk_callback(struct urb *urb);
static void cpcusb_read_interrupt_callback(struct urb *urb);

static int cpcusb_setup_intrep(CPC_USB_T *card);

static struct file_operations cpcusb_fops = {
	/*
	 * The owner field is part of the module-locking
	 * mechanism. The idea is that the kernel knows
	 * which module to increment the use-counter of
	 * BEFORE it calls the device's open() function.
	 * This also means that the kernel can decrement
	 * the use-counter again before calling release()
	 * or should the open() function fail.
	 */
	.owner = THIS_MODULE,

	.read = cpcusb_read,
	.write = cpcusb_write,
	.poll = cpcusb_poll,
	.open = cpcusb_open,
	.release = cpcusb_release,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with devfs and the driver core
 */
static struct usb_class_driver cpcusb_class = {
	.name = "usb/cpc_usb%d",
	.fops = &cpcusb_fops,
	.minor_base = CPC_USB_BASE_MNR,
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver cpcusb_driver = {
	.name = "cpc-usb",
	.probe = cpcusb_probe,
	.disconnect = cpcusb_disconnect,
	.id_table = cpcusb_table,
};

static int cpcusb_create_info_output(char *buf)
{
	int i = 0, j;

	for (j = 0; j < CPC_USB_CARD_CNT; j++) {
		if (CPCUSB_Table[j]) {
			CPC_USB_T *card = CPCUSB_Table[j];
			CPC_CHAN_T *chan = card->chan;

			/* MINOR CHANNELNO BUSNO SLOTNO */
			i += sprintf(&buf[i], "%d %s\n", chan->minor,
				     card->serialNumber);
		}
	}

	return i;
}

static int cpcusb_proc_read_info(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len = cpcusb_create_info_output(page);

	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

/*
 * Remove CPC-USB and cleanup
 */
static inline void cpcusb_delete(CPC_USB_T *card)
{
	if (card) {
		if (card->chan) {
			if (card->chan->buf)
				vfree(card->chan->buf);

			if (card->chan->CPCWait_q)
				kfree(card->chan->CPCWait_q);

			kfree(card->chan);
		}

		CPCUSB_Table[card->idx] = NULL;
		kfree(card);
	}
}

/*
 * setup the interrupt IN endpoint of a specific CPC-USB device
 */
static int cpcusb_setup_intrep(CPC_USB_T *card)
{
	int retval = 0;
	struct usb_endpoint_descriptor *ep;

	ep = &card->interface->altsetting[0].endpoint[card->num_intr_in].desc;

	card->intr_in_buffer[0] = 0;
	card->free_slots = 15;	/* initial size */

	/* setup the urb */
	usb_fill_int_urb(card->intr_in_urb, card->udev,
			 usb_rcvintpipe(card->udev, card->num_intr_in),
			 card->intr_in_buffer,
			 sizeof(card->intr_in_buffer),
			 cpcusb_read_interrupt_callback,
			 card,
			 ep->bInterval);

	card->intr_in_urb->status = 0;	/* needed! */

	/* submit the urb */
	retval = usb_submit_urb(card->intr_in_urb, GFP_KERNEL);

	if (retval)
		err("%s - failed submitting intr urb, error %d", __func__,
		    retval);

	return retval;
}

static int cpcusb_open(struct inode *inode, struct file *file)
{
	CPC_USB_T *card = NULL;
	struct usb_interface *interface;
	int subminor;
	int j, retval = 0;

	subminor = iminor(inode);

	/* prevent disconnects */
	down(&disconnect_sem);

	interface = usb_find_interface(&cpcusb_driver, subminor);
	if (!interface) {
		err("%s - error, can't find device for minor %d",
				__func__, subminor);
		retval = CPC_ERR_NO_INTERFACE_PRESENT;
		goto exit_no_device;
	}

	card = usb_get_intfdata(interface);
	if (!card) {
		retval = CPC_ERR_NO_INTERFACE_PRESENT;
		goto exit_no_device;
	}

	/* lock this device */
	down(&card->sem);

	/* increment our usage count for the driver */
	if (card->open) {
		dbg("device already opened");
		retval = CPC_ERR_CHANNEL_ALREADY_OPEN;
		goto exit_on_error;
	}

	/* save our object in the file's private structure */
	file->private_data = card;
	for (j = 0; j < CPC_USB_URB_CNT; j++) {
		usb_fill_bulk_urb(card->urbs[j].urb, card->udev,
				  usb_rcvbulkpipe(card->udev, card->num_bulk_in),
				  card->urbs[j].buffer, card->urbs[j].size,
				  cpcusb_read_bulk_callback, card);

		retval = usb_submit_urb(card->urbs[j].urb, GFP_KERNEL);

		if (retval) {
			err("%s - failed submitting read urb, error %d",
			    __func__, retval);
			retval = CPC_ERR_TRANSMISSION_FAILED;
			goto exit_on_error;
		}
	}

	info("%s - %d URB's submitted", __func__, j);

	ResetBuffer(card->chan);

	cpcusb_setup_intrep(card);
	card->open = 1;

	atomic_inc(&useCount);

exit_on_error:
	/* unlock this device */
	up(&card->sem);

exit_no_device:
	up(&disconnect_sem);

	return retval;
}

static unsigned int cpcusb_poll(struct file *file, poll_table * wait)
{
	CPC_USB_T *card = (CPC_USB_T *) file->private_data;
	unsigned int retval = 0;

	if (!card) {
		err("%s - device object lost", __func__);
		return -EIO;
	}

	poll_wait(file, card->chan->CPCWait_q, wait);

	if (IsBufferNotEmpty(card->chan) || !(card->present))
		retval |= (POLLIN | POLLRDNORM);

	if (card->free_slots)
		retval |= (POLLOUT | POLLWRNORM);

	return retval;
}

static int cpcusb_release(struct inode *inode, struct file *file)
{
	CPC_USB_T *card = (CPC_USB_T *) file->private_data;
	int j, retval = 0;

	if (card == NULL) {
		dbg("%s - object is NULL", __func__);
		return CPC_ERR_NO_INTERFACE_PRESENT;
	}

	/* lock our device */
	down(&card->sem);

	if (!card->open) {
		dbg("%s - device not opened", __func__);
		retval = CPC_ERR_NO_INTERFACE_PRESENT;
		goto exit_not_opened;
	}

	/* if device wasn't unplugged kill all urbs */
	if (card->present) {
		/* kill read urbs */
		for (j = 0; j < CPC_USB_URB_CNT; j++) {
			usb_kill_urb(card->urbs[j].urb);
		}

		/* kill irq urb */
		usb_kill_urb(card->intr_in_urb);

		/* kill write urbs */
		for (j = 0; j < CPC_USB_URB_CNT; j++) {
			if (atomic_read(&card->wrUrbs[j].busy)) {
				usb_kill_urb(card->wrUrbs[j].urb);
				wait_for_completion(&card->wrUrbs[j].finished);
			}
		}
	}

	atomic_dec(&useCount);

	/* last process detached */
	if (atomic_read(&useCount) == 0) {
		wake_up(&rmmodWq);
	}

	if (!card->present && card->open) {
		/* the device was unplugged before the file was released */
		up(&card->sem);
		cpcusb_delete(card);
		return 0;
	}

	card->open = 0;

exit_not_opened:
	up(&card->sem);

	return 0;
}

static ssize_t cpcusb_read(struct file *file, char *buffer, size_t count,
			   loff_t *ppos)
{
	CPC_USB_T *card = (CPC_USB_T *) file->private_data;
	CPC_CHAN_T *chan;
	int retval = 0;

	if (count < sizeof(CPC_MSG_T))
		return CPC_ERR_UNKNOWN;

	/* check if can read from the given address */
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return CPC_ERR_UNKNOWN;

	/* lock this object */
	down(&card->sem);

	/* verify that the device wasn't unplugged */
	if (!card->present) {
		up(&card->sem);
		return CPC_ERR_NO_INTERFACE_PRESENT;
	}

	if (IsBufferEmpty(card->chan)) {
		retval = 0;
	} else {
		chan = card->chan;

#if 0
		/* convert LPC2119 params back to SJA1000 params */
		if (card->deviceRevision >= 0x0200
		    && chan->buf[chan->oidx].type == CPC_MSG_T_CAN_PRMS) {
			LPC2119_TO_SJA1000_Params(&chan->buf[chan->oidx]);
		}
#endif

		if (copy_to_user(buffer, &chan->buf[chan->oidx], count) != 0) {
			retval = CPC_ERR_IO_TRANSFER;
		} else {
			chan->oidx = (chan->oidx + 1) % CPC_MSG_BUF_CNT;
			chan->WnR = 1;
			retval = sizeof(CPC_MSG_T);
		}
	}
/*	spin_unlock_irqrestore(&card->slock, flags); */

	/* unlock the device */
	up(&card->sem);

	return retval;
}

#define SHIFT  1
static inline void cpcusb_align_buffer_alignment(unsigned char *buf)
{
	/* CPC-USB uploads packed bytes. */
	CPC_MSG_T *cpc = (CPC_MSG_T *) buf;
	unsigned int i;

	for (i = 0; i < cpc->length + (2 * sizeof(unsigned long)); i++) {
		((unsigned char *) &cpc->msgid)[1 + i] =
		    ((unsigned char *) &cpc->msgid)[1 + SHIFT + i];
	}
}

static int cpc_get_buffer_count(CPC_CHAN_T *chan)
{
	/* check the buffer parameters */
	if (chan->iidx == chan->oidx)
		return !chan->WnR ? CPC_MSG_BUF_CNT : 0;
	else if (chan->iidx >= chan->oidx)
		return (chan->iidx - chan->oidx) % CPC_MSG_BUF_CNT;

	return (chan->iidx + CPC_MSG_BUF_CNT - chan->oidx) % CPC_MSG_BUF_CNT;
}

static ssize_t cpcusb_write(struct file *file, const char *buffer,
			    size_t count, loff_t *ppos)
{
	CPC_USB_T *card = (CPC_USB_T *) file->private_data;
	CPC_USB_WRITE_URB_T *wrUrb = NULL;

	ssize_t bytes_written = 0;
	int retval = 0;
	int j;

	unsigned char *obuf = NULL;
	unsigned char type = 0;
	CPC_MSG_T *info = NULL;

	dbg("%s - entered minor %d, count = %d, present = %d",
	    __func__, card->minor, count, card->present);

	if (count > sizeof(CPC_MSG_T))
		return CPC_ERR_UNKNOWN;

	/* check if can read from the given address */
	if (!access_ok(VERIFY_READ, buffer, count))
		return CPC_ERR_UNKNOWN;

	/* lock this object */
	down(&card->sem);

	/* verify that the device wasn't unplugged */
	if (!card->present) {
		retval = CPC_ERR_NO_INTERFACE_PRESENT;
		goto exit;
	}

	/* verify that we actually have some data to write */
	if (count == 0) {
		dbg("%s - write request of 0 bytes", __func__);
		goto exit;
	}

	if (card->free_slots <= 5) {
		info = (CPC_MSG_T *) buffer;

		if (info->type != CPC_CMD_T_CLEAR_CMD_QUEUE
		    || card->free_slots <= 0) {
			dbg("%s - send buffer full please try again %d",
			    __func__, card->free_slots);
			retval = CPC_ERR_CAN_NO_TRANSMIT_BUF;
			goto exit;
		}
	}

	/* Find a free write urb */
	for (j = 0; j < CPC_USB_URB_CNT; j++) {
		if (!atomic_read(&card->wrUrbs[j].busy)) {
			wrUrb = &card->wrUrbs[j];	/* remember found URB */
			atomic_set(&wrUrb->busy, 1);	/* lock this URB      */
			init_completion(&wrUrb->finished);	/* init completion    */
			dbg("WR URB no. %d started", j);
			break;
		}
	}

	/* don't found write urb say error */
	if (!wrUrb) {
		dbg("%s - no free send urb available", __func__);
		retval = CPC_ERR_CAN_NO_TRANSMIT_BUF;
		goto exit;
	}
	dbg("URB write req");

	obuf = (unsigned char *) wrUrb->urb->transfer_buffer;

	/* copy the data from userspace into our transfer buffer;
	 * this is the only copy required.
	 */
	if (copy_from_user(&obuf[4], buffer, count) != 0) {
		atomic_set(&wrUrb->busy, 0);	/* release urb */
		retval = CPC_ERR_IO_TRANSFER;
		goto exit;
	}

	/* check if it is a DRIVER information message, so we can
	 * response to that message and not the USB
	 */
	info = (CPC_MSG_T *) &obuf[4];

	bytes_written = 11 + info->length;
	if (bytes_written >= wrUrb->size) {
		retval = CPC_ERR_IO_TRANSFER;
		goto exit;
	}

	switch (info->type) {
	case CPC_CMD_T_CLEAR_MSG_QUEUE:
		ResetBuffer(card->chan);
		break;

	case CPC_CMD_T_INQ_MSG_QUEUE_CNT:
		retval = cpc_get_buffer_count(card->chan);
		atomic_set(&wrUrb->busy, 0);

		goto exit;

	case CPC_CMD_T_INQ_INFO:
		if (info->msg.info.source == CPC_INFOMSG_T_DRIVER) {
			/* release urb cause we'll use it for driver
			 * information
			 */
			atomic_set(&wrUrb->busy, 0);
			if (IsBufferFull(card->chan)) {
				retval = CPC_ERR_IO_TRANSFER;
				goto exit;
			}

			/* it is a driver information request message and we have
			 * free rx slots to store the response
			 */
			type = info->msg.info.type;
			info = &card->chan->buf[card->chan->iidx];

			info->type = CPC_MSG_T_INFO;
			info->msg.info.source = CPC_INFOMSG_T_DRIVER;
			info->msg.info.type = type;

			switch (type) {
			case CPC_INFOMSG_T_VERSION:
				info->length = strlen(CPC_DRIVER_VERSION) + 2;
				sprintf(info->msg.info.msg, "%s\n",
					CPC_DRIVER_VERSION);
				break;

			case CPC_INFOMSG_T_SERIAL:
				info->length = strlen(CPC_DRIVER_SERIAL) + 2;
				sprintf(info->msg.info.msg, "%s\n",
					CPC_DRIVER_SERIAL);
				break;

			default:
				info->length = 2;
				info->msg.info.type =
				    CPC_INFOMSG_T_UNKNOWN_TYPE;
			}

			card->chan->WnR = 0;
			card->chan->iidx =
			    (card->chan->iidx + 1) % CPC_MSG_BUF_CNT;

			retval = info->length;
			goto exit;
		}
		break;
	case CPC_CMD_T_CAN_PRMS:
		/* Check the controller type. If it's the new CPC-USB, make sure if these are SJA1000 params */
		if (info->msg.canparams.cc_type != SJA1000
		    && info->msg.canparams.cc_type != M16C_BASIC
		    && (card->productId == USB_CPCUSB_LPC2119_PRODUCT_ID
			&& info->msg.canparams.cc_type != SJA1000)) {
			/* don't forget to release the urb */
			atomic_set(&wrUrb->busy, 0);
			retval = CPC_ERR_WRONG_CONTROLLER_TYPE;
			goto exit;
		}
		break;
	}

	/* just convert the params if it is an old CPC-USB with M16C controller */
	if (card->productId == USB_CPCUSB_M16C_PRODUCT_ID) {
		/* if it is a parameter message convert it from SJA1000 controller
		 * settings to M16C Basic controller settings
		 */
		SJA1000_TO_M16C_BASIC_Params((CPC_MSG_T *) &obuf[4]);
	}

	/* don't forget the byte alignment */
	cpcusb_align_buffer_alignment(&obuf[4]);

	/* setup a the 4 byte header */
	obuf[0] = obuf[1] = obuf[2] = obuf[3] = 0;

	/* this urb was already set up, except for this write size */
	wrUrb->urb->transfer_buffer_length = bytes_written + 4;

	/* send the data out the bulk port */
	/* a character device write uses GFP_KERNEL,
	   unless a spinlock is held */
	retval = usb_submit_urb(wrUrb->urb, GFP_KERNEL);
	if (retval) {
		atomic_set(&wrUrb->busy, 0);	/* release urb */
		err("%s - failed submitting write urb, error %d",
		    __func__, retval);
	} else {
		retval = bytes_written;
	}

exit:
	/* unlock the device */
	up(&card->sem);

	dbg("%s - leaved", __func__);

	return retval;
}

/*
 * callback for interrupt IN urb
 */
static void cpcusb_read_interrupt_callback(struct urb *urb)
{
	CPC_USB_T *card = (CPC_USB_T *) urb->context;
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&card->slock, flags);

	if (!card->present) {
		spin_unlock_irqrestore(&card->slock, flags);
		info("%s - no such device", __func__);
		return;
	}

	switch (urb->status) {
	case 0: /* success */
		card->free_slots = card->intr_in_buffer[1];
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* urb was killed */
		spin_unlock_irqrestore(&card->slock, flags);
		dbg("%s - intr urb killed", __func__);
		return;
	default:
		info("%s - nonzero urb status %d", __func__, urb->status);
		break;
	}

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval) {
		err("%s - failed resubmitting intr urb, error %d",
		    __func__, retval);
	}

	spin_unlock_irqrestore(&card->slock, flags);
	wake_up_interruptible(card->chan->CPCWait_q);

	return;
}

#define UN_SHIFT  1
#define CPCMSG_HEADER_LEN_FIRMWARE   11
static inline int cpcusb_unalign_and_copy_buffy(unsigned char *out,
						unsigned char *in)
{
	unsigned int i, j;

	for (i = 0; i < 3; i++)
		out[i] = in[i];

	for (j = 0; j < (in[1] + (CPCMSG_HEADER_LEN_FIRMWARE - 3)); j++)
		out[j + i + UN_SHIFT] = in[j + i];

	return i + j;
}

/*
 * callback for bulk IN urb
 */
static void cpcusb_read_bulk_callback(struct urb *urb)
{
	CPC_USB_T *card = (CPC_USB_T *) urb->context;
	CPC_CHAN_T *chan;
	unsigned char *ibuf = urb->transfer_buffer;
	int retval, msgCnt, start, again = 0;
	unsigned long flags;

	if (!card) {
		err("%s - device object lost", __func__);
		return;
	}

	spin_lock_irqsave(&card->slock, flags);

	if (!card->present) {
		spin_unlock_irqrestore(&card->slock, flags);
		info("%s - no such device", __func__);
		return;
	}

	switch (urb->status) {
	case 0:		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* urb was killed */
		spin_unlock_irqrestore(&card->slock, flags);
		dbg("%s - read urb killed", __func__);
		return;
	default:
		info("%s - nonzero urb status %d", __func__, urb->status);
		break;
	}

	if (urb->actual_length) {
		msgCnt = ibuf[0] & ~0x80;
		again = ibuf[0] & 0x80;

		/* we have a 4 byte header */
		start = 4;
		chan = card->chan;
		while (msgCnt) {
			if (!(IsBufferFull(card->chan))) {
				start +=
				    cpcusb_unalign_and_copy_buffy((unsigned char *)
							  &chan->buf[chan->iidx], &ibuf[start]);

				if (start > urb->transfer_buffer_length) {
					err("%d > %d", start, urb->transfer_buffer_length);
					break;
				}

				chan->WnR = 0;
				chan->iidx = (chan->iidx + 1) % CPC_MSG_BUF_CNT;
				msgCnt--;
			} else {
				break;
			}
		}
	}

	usb_fill_bulk_urb(urb, card->udev,
			  usb_rcvbulkpipe(card->udev, card->num_bulk_in),
			  urb->transfer_buffer,
			  urb->transfer_buffer_length,
			  cpcusb_read_bulk_callback, card);

	retval = usb_submit_urb(urb, GFP_ATOMIC);

	if (retval) {
		err("%s - failed resubmitting read urb, error %d", __func__, retval);
	}

	spin_unlock_irqrestore(&card->slock, flags);

	wake_up_interruptible(card->chan->CPCWait_q);
}

/*
 * callback for bulk IN urb
 */
static void cpcusb_write_bulk_callback(struct urb *urb)
{
	CPC_USB_T *card = (CPC_USB_T *) urb->context;
	unsigned long flags;
	int j;

	spin_lock_irqsave(&card->slock, flags);

	/* find this urb */
	for (j = 0; j < CPC_USB_URB_CNT; j++) {
		if (card->wrUrbs[j].urb == urb) {
			dbg("URB found no. %d", j);
			/* notify anyone waiting that the write has finished */
			complete(&card->wrUrbs[j].finished);
			atomic_set(&card->wrUrbs[j].busy, 0);
			break;
		}
	}

	switch (urb->status) {
	case 0:		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* urb was killed */
		spin_unlock_irqrestore(&card->slock, flags);
		dbg("%s - write urb no. %d killed", __func__, j);
		return;
	default:
		info("%s - nonzero urb status %d", __func__, urb->status);
		break;
	}

	spin_unlock_irqrestore(&card->slock, flags);

	wake_up_interruptible(card->chan->CPCWait_q);
}

static inline int cpcusb_get_free_slot(void)
{
	int i;

	for (i = 0; i < CPC_USB_CARD_CNT; i++) {
		if (!CPCUSB_Table[i])
			return i;
	}

	return -1;
}

/*
 * probe function for new CPC-USB devices
 */
static int cpcusb_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	CPC_USB_T *card = NULL;
	CPC_CHAN_T *chan = NULL;

	struct usb_device *udev = interface_to_usbdev(interface);
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;

	int i, j, retval = -ENOMEM, slot;

	slot = cpcusb_get_free_slot();
	if (slot < 0) {
		info("No more devices supported");
		return -ENOMEM;
	}

	/* allocate memory for our device state and initialize it */
	card = kzalloc(sizeof(CPC_USB_T), GFP_KERNEL);
	if (!card) {
		err("Out of memory");
		return -ENOMEM;
	}
	CPCUSB_Table[slot] = card;

	/* allocate and initialize the channel struct */
	card->chan = kmalloc(sizeof(CPC_CHAN_T), GFP_KERNEL);
	if (!card->chan) {
		kfree(card);
		err("Out of memory");
		return -ENOMEM;
	}

	chan = card->chan;
	memset(chan, 0, sizeof(CPC_CHAN_T));
	ResetBuffer(chan);

	init_MUTEX(&card->sem);
	spin_lock_init(&card->slock);

	card->udev = udev;
	card->interface = interface;
	if (udev->descriptor.iSerialNumber) {
		usb_string(udev, udev->descriptor.iSerialNumber, card->serialNumber,
				   128);
		info("Serial %s", card->serialNumber);
	}

	card->productId = udev->descriptor.idProduct;
	info("Product %s",
	     card->productId == USB_CPCUSB_LPC2119_PRODUCT_ID ?
			 "CPC-USB/ARM7" : "CPC-USB/M16C");

	/* set up the endpoint information */
	/* check out the endpoints */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = &interface->altsetting[0];
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!card->num_intr_in &&
		    (endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		     == USB_ENDPOINT_XFER_INT)) {
			card->intr_in_urb = usb_alloc_urb(0, GFP_KERNEL);
			card->num_intr_in = 1;

			if (!card->intr_in_urb) {
				err("No free urbs available");
				goto error;
			}

			dbg("intr_in urb %d", card->num_intr_in);
		}

		if (!card->num_bulk_in &&
		    (endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		     == USB_ENDPOINT_XFER_BULK)) {
			card->num_bulk_in = 2;
			for (j = 0; j < CPC_USB_URB_CNT; j++) {
				card->urbs[j].size = endpoint->wMaxPacketSize;
				card->urbs[j].urb = usb_alloc_urb(0, GFP_KERNEL);
				if (!card->urbs[j].urb) {
					err("No free urbs available");
					goto error;
				}
				card->urbs[j].buffer =
				    usb_buffer_alloc(udev,
						     card->urbs[j].size,
						     GFP_KERNEL,
						     &card->urbs[j].urb->transfer_dma);
				if (!card->urbs[j].buffer) {
					err("Couldn't allocate bulk_in_buffer");
					goto error;
				}
			}
			info("%s - %d reading URB's allocated",
			     __func__, CPC_USB_URB_CNT);
		}

		if (!card->num_bulk_out &&
		    !(endpoint->bEndpointAddress & USB_DIR_IN) &&
		    ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
		     == USB_ENDPOINT_XFER_BULK)) {

			card->num_bulk_out = 2;

			for (j = 0; j < CPC_USB_URB_CNT; j++) {
				card->wrUrbs[j].size =
				    endpoint->wMaxPacketSize;
				card->wrUrbs[j].urb =
				    usb_alloc_urb(0, GFP_KERNEL);
				if (!card->wrUrbs[j].urb) {
					err("No free urbs available");
					goto error;
				}
				card->wrUrbs[j].buffer = usb_buffer_alloc(udev,
							       card->wrUrbs[j].size, GFP_KERNEL,
							       &card->wrUrbs[j].urb->transfer_dma);

				if (!card->wrUrbs[j].buffer) {
					err("Couldn't allocate bulk_out_buffer");
					goto error;
				}

				usb_fill_bulk_urb(card->wrUrbs[j].urb, udev,
						usb_sndbulkpipe(udev, endpoint->bEndpointAddress),
						card->wrUrbs[j].buffer,
						card->wrUrbs[j].size,
						cpcusb_write_bulk_callback,
						card);
			}

			info("%s - %d writing URB's allocated", __func__, CPC_USB_URB_CNT);
		}
	}

	if (!(card->num_bulk_in && card->num_bulk_out)) {
		err("Couldn't find both bulk-in and bulk-out endpoints");
		goto error;
	}

	/* allow device read, write and ioctl */
	card->present = 1;

	/* we can register the device now, as it is ready */
	usb_set_intfdata(interface, card);
	retval = usb_register_dev(interface, &cpcusb_class);

	if (retval) {
		/* something prevented us from registering this driver */
		err("Not able to get a minor for this device.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	card->chan->minor = card->minor = interface->minor;

	chan->buf = vmalloc(sizeof(CPC_MSG_T) * CPC_MSG_BUF_CNT);
	if (chan->buf == NULL) {
		err("Out of memory");
		retval = -ENOMEM;
		goto error;
	}
	info("Allocated memory for %d messages (%d kbytes)",
	     CPC_MSG_BUF_CNT, (sizeof(CPC_MSG_T) * CPC_MSG_BUF_CNT) / 1000);
	memset(chan->buf, 0, sizeof(CPC_MSG_T) * CPC_MSG_BUF_CNT);

	ResetBuffer(chan);

	card->chan->CPCWait_q = kmalloc(sizeof(wait_queue_head_t), GFP_KERNEL);
	if (!card->chan->CPCWait_q) {
		err("Out of memory");
		retval = -ENOMEM;
		goto error;
	}
	init_waitqueue_head(card->chan->CPCWait_q);

	CPCUSB_Table[slot] = card;
	card->idx = slot;
	CPCUsbCnt++;

	/* let the user know what node this device is now attached to */
	info("Device now attached to USB-%d", card->minor);
	return 0;

error:
	for (j = 0; j < CPC_USB_URB_CNT; j++) {
		if (card->urbs[j].buffer) {
			usb_buffer_free(card->udev, card->urbs[j].size,
					card->urbs[j].buffer,
					card->urbs[j].urb->transfer_dma);
			card->urbs[j].buffer = NULL;
		}
		if (card->urbs[j].urb) {
			usb_free_urb(card->urbs[j].urb);
			card->urbs[j].urb = NULL;
		}
	}

	cpcusb_delete(card);
	return retval;
}

/*
 * called by the usb core when the device is removed from the system
 */
static void cpcusb_disconnect(struct usb_interface *interface)
{
	CPC_USB_T *card = NULL;
	int minor, j;

	/* prevent races with open() */
	down(&disconnect_sem);

	card = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	down(&card->sem);

	/* prevent device read, write and ioctl */
	card->present = 0;

	minor = card->minor;

	/* free all urbs and their buffers */
	for (j = 0; j < CPC_USB_URB_CNT; j++) {
		/* terminate an ongoing write */
		if (atomic_read(&card->wrUrbs[j].busy)) {
			usb_kill_urb(card->wrUrbs[j].urb);
			wait_for_completion(&card->wrUrbs[j].finished);
		}
		usb_buffer_free(card->udev, card->wrUrbs[j].size,
				card->wrUrbs[j].buffer,
				card->wrUrbs[j].urb->transfer_dma);
		usb_free_urb(card->wrUrbs[j].urb);
	}
	info("%d write URBs freed", CPC_USB_URB_CNT);

	/* free all urbs and their buffers */
	for (j = 0; j < CPC_USB_URB_CNT; j++) {
		usb_buffer_free(card->udev, card->urbs[j].size,
				card->urbs[j].buffer,
				card->urbs[j].urb->transfer_dma);
		usb_free_urb(card->urbs[j].urb);
	}
	info("%d read URBs freed", CPC_USB_URB_CNT);
	usb_free_urb(card->intr_in_urb);

	/* give back our minor */
	usb_deregister_dev(interface, &cpcusb_class);

	up(&card->sem);

	/* if the device is opened, cpcusb_release will clean this up */
	if (!card->open)
		cpcusb_delete(card);
	else
		wake_up_interruptible(card->chan->CPCWait_q);

	up(&disconnect_sem);

	CPCUsbCnt--;
	info("USB-%d now disconnected", minor);
}

static int __init CPCUsb_Init(void)
{
	int result, i;

	info(DRIVER_DESC " v" DRIVER_VERSION);
	info("Build on " __DATE__ " at " __TIME__);

	for (i = 0; i < CPC_USB_CARD_CNT; i++)
		CPCUSB_Table[i] = 0;

	/* register this driver with the USB subsystem */
	result = usb_register(&cpcusb_driver);
	if (result) {
		err("usb_register failed. Error number %d", result);
		return result;
	}

	procDir = proc_mkdir(CPC_USB_PROC_DIR, NULL);
	if (!procDir) {
		err("Could not create proc entry");
	} else {
		procEntry = create_proc_read_entry("info", 0444, procDir,
						   cpcusb_proc_read_info,
						   NULL);
		if (!procEntry) {
			err("Could not create proc entry %s", CPC_USB_PROC_DIR "/info");
			remove_proc_entry(CPC_USB_PROC_DIR, NULL);
			procDir = NULL;
		}
	}

	return 0;
}

static void __exit CPCUsb_Exit(void)
{
	wait_event(rmmodWq, !atomic_read(&useCount));

	/* deregister this driver with the USB subsystem */
	usb_deregister(&cpcusb_driver);

	if (procDir) {
		if (procEntry)
			remove_proc_entry("info", procDir);
		remove_proc_entry(CPC_USB_PROC_DIR, NULL);
	}
}

module_init(CPCUsb_Init);
module_exit(CPCUsb_Exit);
