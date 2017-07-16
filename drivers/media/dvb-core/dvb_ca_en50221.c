/*
 * dvb_ca.c: generic DVB functions for EN50221 CAM interfaces
 *
 * Copyright (C) 2004 Andrew de Quincey
 *
 * Parts of this file were based on sources as follows:
 *
 * Copyright (C) 2003 Ralph Metzler <rjkm@metzlerbros.de>
 *
 * based on code:
 *
 * Copyright (C) 1999-2002 Ralph  Metzler
 *                       & Marcus Metzler for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 */

#define pr_fmt(fmt) "dvb_ca_en50221: " fmt

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/sched/signal.h>
#include <linux/kthread.h>

#include "dvb_ca_en50221.h"
#include "dvb_ringbuffer.h"

static int dvb_ca_en50221_debug;

module_param_named(cam_debug, dvb_ca_en50221_debug, int, 0644);
MODULE_PARM_DESC(cam_debug, "enable verbose debug messages");

#define dprintk(fmt, arg...) do {					\
	if (dvb_ca_en50221_debug)					\
		printk(KERN_DEBUG pr_fmt("%s: " fmt), __func__, ##arg);\
} while (0)

#define INIT_TIMEOUT_SECS 10

#define HOST_LINK_BUF_SIZE 0x200

#define RX_BUFFER_SIZE 65535

#define MAX_RX_PACKETS_PER_ITERATION 10

#define CTRLIF_DATA      0
#define CTRLIF_COMMAND   1
#define CTRLIF_STATUS    1
#define CTRLIF_SIZE_LOW  2
#define CTRLIF_SIZE_HIGH 3

#define CMDREG_HC        1	/* Host control */
#define CMDREG_SW        2	/* Size write */
#define CMDREG_SR        4	/* Size read */
#define CMDREG_RS        8	/* Reset interface */
#define CMDREG_FRIE   0x40	/* Enable FR interrupt */
#define CMDREG_DAIE   0x80	/* Enable DA interrupt */
#define IRQEN (CMDREG_DAIE)

#define STATUSREG_RE     1	/* read error */
#define STATUSREG_WE     2	/* write error */
#define STATUSREG_FR  0x40	/* module free */
#define STATUSREG_DA  0x80	/* data available */
#define STATUSREG_TXERR (STATUSREG_RE|STATUSREG_WE)	/* general transfer error */


#define DVB_CA_SLOTSTATE_NONE           0
#define DVB_CA_SLOTSTATE_UNINITIALISED  1
#define DVB_CA_SLOTSTATE_RUNNING        2
#define DVB_CA_SLOTSTATE_INVALID        3
#define DVB_CA_SLOTSTATE_WAITREADY      4
#define DVB_CA_SLOTSTATE_VALIDATE       5
#define DVB_CA_SLOTSTATE_WAITFR         6
#define DVB_CA_SLOTSTATE_LINKINIT       7


/* Information on a CA slot */
struct dvb_ca_slot {

	/* current state of the CAM */
	int slot_state;

	/* mutex used for serializing access to one CI slot */
	struct mutex slot_lock;

	/* Number of CAMCHANGES that have occurred since last processing */
	atomic_t camchange_count;

	/* Type of last CAMCHANGE */
	int camchange_type;

	/* base address of CAM config */
	u32 config_base;

	/* value to write into Config Control register */
	u8 config_option;

	/* if 1, the CAM supports DA IRQs */
	u8 da_irq_supported:1;

	/* size of the buffer to use when talking to the CAM */
	int link_buf_size;

	/* buffer for incoming packets */
	struct dvb_ringbuffer rx_buffer;

	/* timer used during various states of the slot */
	unsigned long timeout;
};

/* Private CA-interface information */
struct dvb_ca_private {
	struct kref refcount;

	/* pointer back to the public data structure */
	struct dvb_ca_en50221 *pub;

	/* the DVB device */
	struct dvb_device *dvbdev;

	/* Flags describing the interface (DVB_CA_FLAG_*) */
	u32 flags;

	/* number of slots supported by this CA interface */
	unsigned int slot_count;

	/* information on each slot */
	struct dvb_ca_slot *slot_info;

	/* wait queues for read() and write() operations */
	wait_queue_head_t wait_queue;

	/* PID of the monitoring thread */
	struct task_struct *thread;

	/* Flag indicating if the CA device is open */
	unsigned int open:1;

	/* Flag indicating the thread should wake up now */
	unsigned int wakeup:1;

	/* Delay the main thread should use */
	unsigned long delay;

	/* Slot to start looking for data to read from in the next user-space read operation */
	int next_read_slot;

	/* mutex serializing ioctls */
	struct mutex ioctl_mutex;
};

static void dvb_ca_private_free(struct dvb_ca_private *ca)
{
	unsigned int i;

	dvb_free_device(ca->dvbdev);
	for (i = 0; i < ca->slot_count; i++)
		vfree(ca->slot_info[i].rx_buffer.data);

	kfree(ca->slot_info);
	kfree(ca);
}

static void dvb_ca_private_release(struct kref *ref)
{
	struct dvb_ca_private *ca = container_of(ref, struct dvb_ca_private, refcount);
	dvb_ca_private_free(ca);
}

static void dvb_ca_private_get(struct dvb_ca_private *ca)
{
	kref_get(&ca->refcount);
}

static void dvb_ca_private_put(struct dvb_ca_private *ca)
{
	kref_put(&ca->refcount, dvb_ca_private_release);
}

static void dvb_ca_en50221_thread_wakeup(struct dvb_ca_private *ca);
static int dvb_ca_en50221_read_data(struct dvb_ca_private *ca, int slot,
				    u8 *ebuf, int ecount);
static int dvb_ca_en50221_write_data(struct dvb_ca_private *ca, int slot,
				     u8 *ebuf, int ecount);


/**
 * Safely find needle in haystack.
 *
 * @haystack: Buffer to look in.
 * @hlen: Number of bytes in haystack.
 * @needle: Buffer to find.
 * @nlen: Number of bytes in needle.
 * @return Pointer into haystack needle was found at, or NULL if not found.
 */
static char *findstr(char *haystack, int hlen, char *needle, int nlen)
{
	int i;

	if (hlen < nlen)
		return NULL;

	for (i = 0; i <= hlen - nlen; i++) {
		if (!strncmp(haystack + i, needle, nlen))
			return haystack + i;
	}

	return NULL;
}



/* ******************************************************************************** */
/* EN50221 physical interface functions */


/**
 * dvb_ca_en50221_check_camstatus - Check CAM status.
 */
static int dvb_ca_en50221_check_camstatus(struct dvb_ca_private *ca, int slot)
{
	int slot_status;
	int cam_present_now;
	int cam_changed;

	/* IRQ mode */
	if (ca->flags & DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE) {
		return (atomic_read(&ca->slot_info[slot].camchange_count) != 0);
	}

	/* poll mode */
	slot_status = ca->pub->poll_slot_status(ca->pub, slot, ca->open);

	cam_present_now = (slot_status & DVB_CA_EN50221_POLL_CAM_PRESENT) ? 1 : 0;
	cam_changed = (slot_status & DVB_CA_EN50221_POLL_CAM_CHANGED) ? 1 : 0;
	if (!cam_changed) {
		int cam_present_old = (ca->slot_info[slot].slot_state != DVB_CA_SLOTSTATE_NONE);
		cam_changed = (cam_present_now != cam_present_old);
	}

	if (cam_changed) {
		if (!cam_present_now) {
			ca->slot_info[slot].camchange_type = DVB_CA_EN50221_CAMCHANGE_REMOVED;
		} else {
			ca->slot_info[slot].camchange_type = DVB_CA_EN50221_CAMCHANGE_INSERTED;
		}
		atomic_set(&ca->slot_info[slot].camchange_count, 1);
	} else {
		if ((ca->slot_info[slot].slot_state == DVB_CA_SLOTSTATE_WAITREADY) &&
		    (slot_status & DVB_CA_EN50221_POLL_CAM_READY)) {
			// move to validate state if reset is completed
			ca->slot_info[slot].slot_state = DVB_CA_SLOTSTATE_VALIDATE;
		}
	}

	return cam_changed;
}


/**
 * dvb_ca_en50221_wait_if_status - Wait for flags to become set on the STATUS
 *	 register on a CAM interface, checking for errors and timeout.
 *
 * @ca: CA instance.
 * @slot: Slot on interface.
 * @waitfor: Flags to wait for.
 * @timeout_ms: Timeout in milliseconds.
 *
 * @return 0 on success, nonzero on error.
 */
static int dvb_ca_en50221_wait_if_status(struct dvb_ca_private *ca, int slot,
					 u8 waitfor, int timeout_hz)
{
	unsigned long timeout;
	unsigned long start;

	dprintk("%s\n", __func__);

	/* loop until timeout elapsed */
	start = jiffies;
	timeout = jiffies + timeout_hz;
	while (1) {
		/* read the status and check for error */
		int res = ca->pub->read_cam_control(ca->pub, slot, CTRLIF_STATUS);
		if (res < 0)
			return -EIO;

		/* if we got the flags, it was successful! */
		if (res & waitfor) {
			dprintk("%s succeeded timeout:%lu\n",
				__func__, jiffies - start);
			return 0;
		}

		/* check for timeout */
		if (time_after(jiffies, timeout)) {
			break;
		}

		/* wait for a bit */
		msleep(1);
	}

	dprintk("%s failed timeout:%lu\n", __func__, jiffies - start);

	/* if we get here, we've timed out */
	return -ETIMEDOUT;
}


/**
 * dvb_ca_en50221_link_init - Initialise the link layer connection to a CAM.
 *
 * @ca: CA instance.
 * @slot: Slot id.
 *
 * @return 0 on success, nonzero on failure.
 */
static int dvb_ca_en50221_link_init(struct dvb_ca_private *ca, int slot)
{
	int ret;
	int buf_size;
	u8 buf[2];

	dprintk("%s\n", __func__);

	/* we'll be determining these during this function */
	ca->slot_info[slot].da_irq_supported = 0;

	/* set the host link buffer size temporarily. it will be overwritten with the
	 * real negotiated size later. */
	ca->slot_info[slot].link_buf_size = 2;

	/* read the buffer size from the CAM */
	if ((ret = ca->pub->write_cam_control(ca->pub, slot, CTRLIF_COMMAND, IRQEN | CMDREG_SR)) != 0)
		return ret;
	ret = dvb_ca_en50221_wait_if_status(ca, slot, STATUSREG_DA, HZ);
	if (ret != 0)
		return ret;
	if ((ret = dvb_ca_en50221_read_data(ca, slot, buf, 2)) != 2)
		return -EIO;
	if ((ret = ca->pub->write_cam_control(ca->pub, slot, CTRLIF_COMMAND, IRQEN)) != 0)
		return ret;

	/* store it, and choose the minimum of our buffer and the CAM's buffer size */
	buf_size = (buf[0] << 8) | buf[1];
	if (buf_size > HOST_LINK_BUF_SIZE)
		buf_size = HOST_LINK_BUF_SIZE;
	ca->slot_info[slot].link_buf_size = buf_size;
	buf[0] = buf_size >> 8;
	buf[1] = buf_size & 0xff;
	dprintk("Chosen link buffer size of %i\n", buf_size);

	/* write the buffer size to the CAM */
	if ((ret = ca->pub->write_cam_control(ca->pub, slot, CTRLIF_COMMAND, IRQEN | CMDREG_SW)) != 0)
		return ret;
	if ((ret = dvb_ca_en50221_wait_if_status(ca, slot, STATUSREG_FR, HZ / 10)) != 0)
		return ret;
	if ((ret = dvb_ca_en50221_write_data(ca, slot, buf, 2)) != 2)
		return -EIO;
	if ((ret = ca->pub->write_cam_control(ca->pub, slot, CTRLIF_COMMAND, IRQEN)) != 0)
		return ret;

	/* success */
	return 0;
}

/**
 * dvb_ca_en50221_read_tuple - Read a tuple from attribute memory.
 *
 * @ca: CA instance.
 * @slot: Slot id.
 * @address: Address to read from. Updated.
 * @tupleType: Tuple id byte. Updated.
 * @tupleLength: Tuple length. Updated.
 * @tuple: Dest buffer for tuple (must be 256 bytes). Updated.
 *
 * @return 0 on success, nonzero on error.
 */
static int dvb_ca_en50221_read_tuple(struct dvb_ca_private *ca, int slot,
				     int *address, int *tupleType,
				     int *tupleLength, u8 *tuple)
{
	int i;
	int _tupleType;
	int _tupleLength;
	int _address = *address;

	/* grab the next tuple length and type */
	if ((_tupleType = ca->pub->read_attribute_mem(ca->pub, slot, _address)) < 0)
		return _tupleType;
	if (_tupleType == 0xff) {
		dprintk("END OF CHAIN TUPLE type:0x%x\n", _tupleType);
		*address += 2;
		*tupleType = _tupleType;
		*tupleLength = 0;
		return 0;
	}
	if ((_tupleLength = ca->pub->read_attribute_mem(ca->pub, slot, _address + 2)) < 0)
		return _tupleLength;
	_address += 4;

	dprintk("TUPLE type:0x%x length:%i\n", _tupleType, _tupleLength);

	/* read in the whole tuple */
	for (i = 0; i < _tupleLength; i++) {
		tuple[i] = ca->pub->read_attribute_mem(ca->pub, slot, _address + (i * 2));
		dprintk("  0x%02x: 0x%02x %c\n",
			i, tuple[i] & 0xff,
			((tuple[i] > 31) && (tuple[i] < 127)) ? tuple[i] : '.');
	}
	_address += (_tupleLength * 2);

	// success
	*tupleType = _tupleType;
	*tupleLength = _tupleLength;
	*address = _address;
	return 0;
}


/**
 * dvb_ca_en50221_parse_attributes - Parse attribute memory of a CAM module,
 *	extracting Config register, and checking it is a DVB CAM module.
 *
 * @ca: CA instance.
 * @slot: Slot id.
 *
 * @return 0 on success, <0 on failure.
 */
static int dvb_ca_en50221_parse_attributes(struct dvb_ca_private *ca, int slot)
{
	int address = 0;
	int tupleLength;
	int tupleType;
	u8 tuple[257];
	char *dvb_str;
	int rasz;
	int status;
	int got_cftableentry = 0;
	int end_chain = 0;
	int i;
	u16 manfid = 0;
	u16 devid = 0;


	// CISTPL_DEVICE_0A
	if ((status =
	     dvb_ca_en50221_read_tuple(ca, slot, &address, &tupleType, &tupleLength, tuple)) < 0)
		return status;
	if (tupleType != 0x1D)
		return -EINVAL;



	// CISTPL_DEVICE_0C
	if ((status =
	     dvb_ca_en50221_read_tuple(ca, slot, &address, &tupleType, &tupleLength, tuple)) < 0)
		return status;
	if (tupleType != 0x1C)
		return -EINVAL;



	// CISTPL_VERS_1
	if ((status =
	     dvb_ca_en50221_read_tuple(ca, slot, &address, &tupleType, &tupleLength, tuple)) < 0)
		return status;
	if (tupleType != 0x15)
		return -EINVAL;



	// CISTPL_MANFID
	if ((status = dvb_ca_en50221_read_tuple(ca, slot, &address, &tupleType,
						&tupleLength, tuple)) < 0)
		return status;
	if (tupleType != 0x20)
		return -EINVAL;
	if (tupleLength != 4)
		return -EINVAL;
	manfid = (tuple[1] << 8) | tuple[0];
	devid = (tuple[3] << 8) | tuple[2];



	// CISTPL_CONFIG
	if ((status = dvb_ca_en50221_read_tuple(ca, slot, &address, &tupleType,
						&tupleLength, tuple)) < 0)
		return status;
	if (tupleType != 0x1A)
		return -EINVAL;
	if (tupleLength < 3)
		return -EINVAL;

	/* extract the configbase */
	rasz = tuple[0] & 3;
	if (tupleLength < (3 + rasz + 14))
		return -EINVAL;
	ca->slot_info[slot].config_base = 0;
	for (i = 0; i < rasz + 1; i++) {
		ca->slot_info[slot].config_base |= (tuple[2 + i] << (8 * i));
	}

	/* check it contains the correct DVB string */
	dvb_str = findstr((char *)tuple, tupleLength, "DVB_CI_V", 8);
	if (dvb_str == NULL)
		return -EINVAL;
	if (tupleLength < ((dvb_str - (char *) tuple) + 12))
		return -EINVAL;

	/* is it a version we support? */
	if (strncmp(dvb_str + 8, "1.00", 4)) {
		pr_err("dvb_ca adapter %d: Unsupported DVB CAM module version %c%c%c%c\n",
		       ca->dvbdev->adapter->num, dvb_str[8], dvb_str[9],
		       dvb_str[10], dvb_str[11]);
		return -EINVAL;
	}

	/* process the CFTABLE_ENTRY tuples, and any after those */
	while ((!end_chain) && (address < 0x1000)) {
		if ((status = dvb_ca_en50221_read_tuple(ca, slot, &address, &tupleType,
							&tupleLength, tuple)) < 0)
			return status;
		switch (tupleType) {
		case 0x1B:	// CISTPL_CFTABLE_ENTRY
			if (tupleLength < (2 + 11 + 17))
				break;

			/* if we've already parsed one, just use it */
			if (got_cftableentry)
				break;

			/* get the config option */
			ca->slot_info[slot].config_option = tuple[0] & 0x3f;

			/* OK, check it contains the correct strings */
			if ((findstr((char *)tuple, tupleLength, "DVB_HOST", 8) == NULL) ||
			    (findstr((char *)tuple, tupleLength, "DVB_CI_MODULE", 13) == NULL))
				break;

			got_cftableentry = 1;
			break;

		case 0x14:	// CISTPL_NO_LINK
			break;

		case 0xFF:	// CISTPL_END
			end_chain = 1;
			break;

		default:	/* Unknown tuple type - just skip this tuple and move to the next one */
			dprintk("dvb_ca: Skipping unknown tuple type:0x%x length:0x%x\n",
				tupleType, tupleLength);
			break;
		}
	}

	if ((address > 0x1000) || (!got_cftableentry))
		return -EINVAL;

	dprintk("Valid DVB CAM detected MANID:%x DEVID:%x CONFIGBASE:0x%x CONFIGOPTION:0x%x\n",
		manfid, devid, ca->slot_info[slot].config_base,
		ca->slot_info[slot].config_option);

	// success!
	return 0;
}


/**
 * dvb_ca_en50221_set_configoption - Set CAM's configoption correctly.
 *
 * @ca: CA instance.
 * @slot: Slot containing the CAM.
 */
static int dvb_ca_en50221_set_configoption(struct dvb_ca_private *ca, int slot)
{
	int configoption;

	dprintk("%s\n", __func__);

	/* set the config option */
	ca->pub->write_attribute_mem(ca->pub, slot,
				     ca->slot_info[slot].config_base,
				     ca->slot_info[slot].config_option);

	/* check it */
	configoption = ca->pub->read_attribute_mem(ca->pub, slot, ca->slot_info[slot].config_base);
	dprintk("Set configoption 0x%x, read configoption 0x%x\n",
		ca->slot_info[slot].config_option, configoption & 0x3f);

	/* fine! */
	return 0;

}


/**
 * dvb_ca_en50221_read_data - This function talks to an EN50221 CAM control
 *	interface. It reads a buffer of data from the CAM. The data can either
 *	be stored in a supplied buffer, or automatically be added to the slot's
 *	rx_buffer.
 *
 * @ca: CA instance.
 * @slot: Slot to read from.
 * @ebuf: If non-NULL, the data will be written to this buffer. If NULL,
 * the data will be added into the buffering system as a normal fragment.
 * @ecount: Size of ebuf. Ignored if ebuf is NULL.
 *
 * @return Number of bytes read, or < 0 on error
 */
static int dvb_ca_en50221_read_data(struct dvb_ca_private *ca, int slot,
				    u8 *ebuf, int ecount)
{
	int bytes_read;
	int status;
	u8 buf[HOST_LINK_BUF_SIZE];
	int i;

	dprintk("%s\n", __func__);

	/* check if we have space for a link buf in the rx_buffer */
	if (ebuf == NULL) {
		int buf_free;

		if (ca->slot_info[slot].rx_buffer.data == NULL) {
			status = -EIO;
			goto exit;
		}
		buf_free = dvb_ringbuffer_free(&ca->slot_info[slot].rx_buffer);

		if (buf_free < (ca->slot_info[slot].link_buf_size +
				DVB_RINGBUFFER_PKTHDRSIZE)) {
			status = -EAGAIN;
			goto exit;
		}
	}

	if (ca->pub->read_data &&
	    (ca->slot_info[slot].slot_state != DVB_CA_SLOTSTATE_LINKINIT)) {
		if (ebuf == NULL)
			status = ca->pub->read_data(ca->pub, slot, buf,
						    sizeof(buf));
		else
			status = ca->pub->read_data(ca->pub, slot, buf, ecount);
		if (status < 0)
			return status;
		bytes_read =  status;
		if (status == 0)
			goto exit;
	} else {

		/* check if there is data available */
		status = ca->pub->read_cam_control(ca->pub, slot,
						   CTRLIF_STATUS);
		if (status < 0)
			goto exit;
		if (!(status & STATUSREG_DA)) {
			/* no data */
			status = 0;
			goto exit;
		}

		/* read the amount of data */
		status = ca->pub->read_cam_control(ca->pub, slot,
						   CTRLIF_SIZE_HIGH);
		if (status < 0)
			goto exit;
		bytes_read = status << 8;
		status = ca->pub->read_cam_control(ca->pub, slot,
						   CTRLIF_SIZE_LOW);
		if (status < 0)
			goto exit;
		bytes_read |= status;

		/* check it will fit */
		if (ebuf == NULL) {
			if (bytes_read > ca->slot_info[slot].link_buf_size) {
				pr_err("dvb_ca adapter %d: CAM tried to send a buffer larger than the link buffer size (%i > %i)!\n",
				       ca->dvbdev->adapter->num, bytes_read,
				       ca->slot_info[slot].link_buf_size);
				ca->slot_info[slot].slot_state =
						     DVB_CA_SLOTSTATE_LINKINIT;
				status = -EIO;
				goto exit;
			}
			if (bytes_read < 2) {
				pr_err("dvb_ca adapter %d: CAM sent a buffer that was less than 2 bytes!\n",
				       ca->dvbdev->adapter->num);
				ca->slot_info[slot].slot_state =
						     DVB_CA_SLOTSTATE_LINKINIT;
				status = -EIO;
				goto exit;
			}
		} else {
			if (bytes_read > ecount) {
				pr_err("dvb_ca adapter %d: CAM tried to send a buffer larger than the ecount size!\n",
				       ca->dvbdev->adapter->num);
				status = -EIO;
				goto exit;
			}
		}

		/* fill the buffer */
		for (i = 0; i < bytes_read; i++) {
			/* read byte and check */
			status = ca->pub->read_cam_control(ca->pub, slot,
							   CTRLIF_DATA);
			if (status < 0)
				goto exit;

			/* OK, store it in the buffer */
			buf[i] = status;
		}

		/* check for read error (RE should now be 0) */
		status = ca->pub->read_cam_control(ca->pub, slot,
						   CTRLIF_STATUS);
		if (status < 0)
			goto exit;
		if (status & STATUSREG_RE) {
			ca->slot_info[slot].slot_state =
						     DVB_CA_SLOTSTATE_LINKINIT;
			status = -EIO;
			goto exit;
		}
	}

	/* OK, add it to the receive buffer, or copy into external buffer if supplied */
	if (ebuf == NULL) {
		if (ca->slot_info[slot].rx_buffer.data == NULL) {
			status = -EIO;
			goto exit;
		}
		dvb_ringbuffer_pkt_write(&ca->slot_info[slot].rx_buffer, buf, bytes_read);
	} else {
		memcpy(ebuf, buf, bytes_read);
	}

	dprintk("Received CA packet for slot %i connection id 0x%x last_frag:%i size:0x%x\n", slot,
		buf[0], (buf[1] & 0x80) == 0, bytes_read);

	/* wake up readers when a last_fragment is received */
	if ((buf[1] & 0x80) == 0x00) {
		wake_up_interruptible(&ca->wait_queue);
	}
	status = bytes_read;

exit:
	return status;
}


/**
 * dvb_ca_en50221_write_data - This function talks to an EN50221 CAM control
 *				interface. It writes a buffer of data to a CAM.
 *
 * @ca: CA instance.
 * @slot: Slot to write to.
 * @ebuf: The data in this buffer is treated as a complete link-level packet to
 * be written.
 * @count: Size of ebuf.
 *
 * @return Number of bytes written, or < 0 on error.
 */
static int dvb_ca_en50221_write_data(struct dvb_ca_private *ca, int slot,
				     u8 *buf, int bytes_write)
{
	int status;
	int i;

	dprintk("%s\n", __func__);


	/* sanity check */
	if (bytes_write > ca->slot_info[slot].link_buf_size)
		return -EINVAL;

	if (ca->pub->write_data &&
	    (ca->slot_info[slot].slot_state != DVB_CA_SLOTSTATE_LINKINIT))
		return ca->pub->write_data(ca->pub, slot, buf, bytes_write);

	/* it is possible we are dealing with a single buffer implementation,
	   thus if there is data available for read or if there is even a read
	   already in progress, we do nothing but awake the kernel thread to
	   process the data if necessary. */
	if ((status = ca->pub->read_cam_control(ca->pub, slot, CTRLIF_STATUS)) < 0)
		goto exitnowrite;
	if (status & (STATUSREG_DA | STATUSREG_RE)) {
		if (status & STATUSREG_DA)
			dvb_ca_en50221_thread_wakeup(ca);

		status = -EAGAIN;
		goto exitnowrite;
	}

	/* OK, set HC bit */
	if ((status = ca->pub->write_cam_control(ca->pub, slot, CTRLIF_COMMAND,
						 IRQEN | CMDREG_HC)) != 0)
		goto exit;

	/* check if interface is still free */
	if ((status = ca->pub->read_cam_control(ca->pub, slot, CTRLIF_STATUS)) < 0)
		goto exit;
	if (!(status & STATUSREG_FR)) {
		/* it wasn't free => try again later */
		status = -EAGAIN;
		goto exit;
	}

	/*
	 * It may need some time for the CAM to settle down, or there might
	 * be a race condition between the CAM, writing HC and our last
	 * check for DA. This happens, if the CAM asserts DA, just after
	 * checking DA before we are setting HC. In this case it might be
	 * a bug in the CAM to keep the FR bit, the lower layer/HW
	 * communication requires a longer timeout or the CAM needs more
	 * time internally. But this happens in reality!
	 * We need to read the status from the HW again and do the same
	 * we did for the previous check for DA
	 */
	status = ca->pub->read_cam_control(ca->pub, slot, CTRLIF_STATUS);
	if (status < 0)
		goto exit;

	if (status & (STATUSREG_DA | STATUSREG_RE)) {
		if (status & STATUSREG_DA)
			dvb_ca_en50221_thread_wakeup(ca);

		status = -EAGAIN;
		goto exit;
	}

	/* send the amount of data */
	if ((status = ca->pub->write_cam_control(ca->pub, slot, CTRLIF_SIZE_HIGH, bytes_write >> 8)) != 0)
		goto exit;
	if ((status = ca->pub->write_cam_control(ca->pub, slot, CTRLIF_SIZE_LOW,
						 bytes_write & 0xff)) != 0)
		goto exit;

	/* send the buffer */
	for (i = 0; i < bytes_write; i++) {
		if ((status = ca->pub->write_cam_control(ca->pub, slot, CTRLIF_DATA, buf[i])) != 0)
			goto exit;
	}

	/* check for write error (WE should now be 0) */
	if ((status = ca->pub->read_cam_control(ca->pub, slot, CTRLIF_STATUS)) < 0)
		goto exit;
	if (status & STATUSREG_WE) {
		ca->slot_info[slot].slot_state = DVB_CA_SLOTSTATE_LINKINIT;
		status = -EIO;
		goto exit;
	}
	status = bytes_write;

	dprintk("Wrote CA packet for slot %i, connection id 0x%x last_frag:%i size:0x%x\n", slot,
		buf[0], (buf[1] & 0x80) == 0, bytes_write);

exit:
	ca->pub->write_cam_control(ca->pub, slot, CTRLIF_COMMAND, IRQEN);

exitnowrite:
	return status;
}



/* ******************************************************************************** */
/* EN50221 higher level functions */


/**
 * dvb_ca_en50221_slot_shutdown - A CAM has been removed => shut it down.
 *
 * @ca: CA instance.
 * @slot: Slot to shut down.
 */
static int dvb_ca_en50221_slot_shutdown(struct dvb_ca_private *ca, int slot)
{
	dprintk("%s\n", __func__);

	ca->pub->slot_shutdown(ca->pub, slot);
	ca->slot_info[slot].slot_state = DVB_CA_SLOTSTATE_NONE;

	/* need to wake up all processes to check if they're now
	   trying to write to a defunct CAM */
	wake_up_interruptible(&ca->wait_queue);

	dprintk("Slot %i shutdown\n", slot);

	/* success */
	return 0;
}


/**
 * dvb_ca_en50221_camchange_irq - A CAMCHANGE IRQ has occurred.
 *
 * @ca: CA instance.
 * @slot: Slot concerned.
 * @change_type: One of the DVB_CA_CAMCHANGE_* values.
 */
void dvb_ca_en50221_camchange_irq(struct dvb_ca_en50221 *pubca, int slot, int change_type)
{
	struct dvb_ca_private *ca = pubca->private;

	dprintk("CAMCHANGE IRQ slot:%i change_type:%i\n", slot, change_type);

	switch (change_type) {
	case DVB_CA_EN50221_CAMCHANGE_REMOVED:
	case DVB_CA_EN50221_CAMCHANGE_INSERTED:
		break;

	default:
		return;
	}

	ca->slot_info[slot].camchange_type = change_type;
	atomic_inc(&ca->slot_info[slot].camchange_count);
	dvb_ca_en50221_thread_wakeup(ca);
}
EXPORT_SYMBOL(dvb_ca_en50221_camchange_irq);


/**
 * dvb_ca_en50221_camready_irq - A CAMREADY IRQ has occurred.
 *
 * @ca: CA instance.
 * @slot: Slot concerned.
 */
void dvb_ca_en50221_camready_irq(struct dvb_ca_en50221 *pubca, int slot)
{
	struct dvb_ca_private *ca = pubca->private;

	dprintk("CAMREADY IRQ slot:%i\n", slot);

	if (ca->slot_info[slot].slot_state == DVB_CA_SLOTSTATE_WAITREADY) {
		ca->slot_info[slot].slot_state = DVB_CA_SLOTSTATE_VALIDATE;
		dvb_ca_en50221_thread_wakeup(ca);
	}
}
EXPORT_SYMBOL(dvb_ca_en50221_camready_irq);


/**
 * dvb_ca_en50221_frda_irq - An FR or DA IRQ has occurred.
 *
 * @ca: CA instance.
 * @slot: Slot concerned.
 */
void dvb_ca_en50221_frda_irq(struct dvb_ca_en50221 *pubca, int slot)
{
	struct dvb_ca_private *ca = pubca->private;
	int flags;

	dprintk("FR/DA IRQ slot:%i\n", slot);

	switch (ca->slot_info[slot].slot_state) {
	case DVB_CA_SLOTSTATE_LINKINIT:
		flags = ca->pub->read_cam_control(pubca, slot, CTRLIF_STATUS);
		if (flags & STATUSREG_DA) {
			dprintk("CAM supports DA IRQ\n");
			ca->slot_info[slot].da_irq_supported = 1;
		}
		break;

	case DVB_CA_SLOTSTATE_RUNNING:
		if (ca->open)
			dvb_ca_en50221_thread_wakeup(ca);
		break;
	}
}
EXPORT_SYMBOL(dvb_ca_en50221_frda_irq);


/* ******************************************************************************** */
/* EN50221 thread functions */

/**
 * Wake up the DVB CA thread
 *
 * @ca: CA instance.
 */
static void dvb_ca_en50221_thread_wakeup(struct dvb_ca_private *ca)
{

	dprintk("%s\n", __func__);

	ca->wakeup = 1;
	mb();
	wake_up_process(ca->thread);
}

/**
 * Update the delay used by the thread.
 *
 * @ca: CA instance.
 */
static void dvb_ca_en50221_thread_update_delay(struct dvb_ca_private *ca)
{
	int delay;
	int curdelay = 100000000;
	int slot;

	/* Beware of too high polling frequency, because one polling
	 * call might take several hundred milliseconds until timeout!
	 */
	for (slot = 0; slot < ca->slot_count; slot++) {
		switch (ca->slot_info[slot].slot_state) {
		default:
		case DVB_CA_SLOTSTATE_NONE:
			delay = HZ * 60;  /* 60s */
			if (!(ca->flags & DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE))
				delay = HZ * 5;  /* 5s */
			break;
		case DVB_CA_SLOTSTATE_INVALID:
			delay = HZ * 60;  /* 60s */
			if (!(ca->flags & DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE))
				delay = HZ / 10;  /* 100ms */
			break;

		case DVB_CA_SLOTSTATE_UNINITIALISED:
		case DVB_CA_SLOTSTATE_WAITREADY:
		case DVB_CA_SLOTSTATE_VALIDATE:
		case DVB_CA_SLOTSTATE_WAITFR:
		case DVB_CA_SLOTSTATE_LINKINIT:
			delay = HZ / 10;  /* 100ms */
			break;

		case DVB_CA_SLOTSTATE_RUNNING:
			delay = HZ * 60;  /* 60s */
			if (!(ca->flags & DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE))
				delay = HZ / 10;  /* 100ms */
			if (ca->open) {
				if ((!ca->slot_info[slot].da_irq_supported) ||
				    (!(ca->flags & DVB_CA_EN50221_FLAG_IRQ_DA)))
					delay = HZ / 10;  /* 100ms */
			}
			break;
		}

		if (delay < curdelay)
			curdelay = delay;
	}

	ca->delay = curdelay;
}

/**
 * Thread state machine for one CA slot to perform the data transfer.
 *
 * @ca: CA instance.
 * @slot: Slot to process.
 */
static void dvb_ca_en50221_thread_state_machine(struct dvb_ca_private *ca,
						int slot)
{
	struct dvb_ca_slot *sl = &ca->slot_info[slot];
	int flags;
	int status;
	int pktcount;
	void *rxbuf;

	mutex_lock(&sl->slot_lock);

	/* check the cam status + deal with CAMCHANGEs */
	while (dvb_ca_en50221_check_camstatus(ca, slot)) {
		/* clear down an old CI slot if necessary */
		if (sl->slot_state != DVB_CA_SLOTSTATE_NONE)
			dvb_ca_en50221_slot_shutdown(ca, slot);

		/* if a CAM is NOW present, initialise it */
		if (sl->camchange_type == DVB_CA_EN50221_CAMCHANGE_INSERTED)
			sl->slot_state = DVB_CA_SLOTSTATE_UNINITIALISED;

		/* we've handled one CAMCHANGE */
		dvb_ca_en50221_thread_update_delay(ca);
		atomic_dec(&sl->camchange_count);
	}

	/* CAM state machine */
	switch (sl->slot_state) {
	case DVB_CA_SLOTSTATE_NONE:
	case DVB_CA_SLOTSTATE_INVALID:
		/* no action needed */
		break;

	case DVB_CA_SLOTSTATE_UNINITIALISED:
		sl->slot_state = DVB_CA_SLOTSTATE_WAITREADY;
		ca->pub->slot_reset(ca->pub, slot);
		sl->timeout = jiffies + (INIT_TIMEOUT_SECS * HZ);
		break;

	case DVB_CA_SLOTSTATE_WAITREADY:
		if (time_after(jiffies, sl->timeout)) {
			pr_err("dvb_ca adaptor %d: PC card did not respond :(\n",
			       ca->dvbdev->adapter->num);
			sl->slot_state = DVB_CA_SLOTSTATE_INVALID;
			dvb_ca_en50221_thread_update_delay(ca);
			break;
		}
		/*
		 * no other action needed; will automatically change state when
		 * ready
		 */
		break;

	case DVB_CA_SLOTSTATE_VALIDATE:
		if (dvb_ca_en50221_parse_attributes(ca, slot) != 0) {
			/*
			 * we need this extra check for annoying interfaces like
			 * the budget-av
			 */
			if ((!(ca->flags & DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE))
			    && (ca->pub->poll_slot_status)) {
				status = ca->pub->poll_slot_status(ca->pub,
								   slot, 0);
				if (!(status &
				      DVB_CA_EN50221_POLL_CAM_PRESENT)) {
					sl->slot_state = DVB_CA_SLOTSTATE_NONE;
					dvb_ca_en50221_thread_update_delay(ca);
					break;
				}
			}

			pr_err("dvb_ca adapter %d: Invalid PC card inserted :(\n",
			       ca->dvbdev->adapter->num);
			sl->slot_state = DVB_CA_SLOTSTATE_INVALID;
			dvb_ca_en50221_thread_update_delay(ca);
			break;
		}
		if (dvb_ca_en50221_set_configoption(ca, slot) != 0) {
			pr_err("dvb_ca adapter %d: Unable to initialise CAM :(\n",
			       ca->dvbdev->adapter->num);
			sl->slot_state = DVB_CA_SLOTSTATE_INVALID;
			dvb_ca_en50221_thread_update_delay(ca);
			break;
		}
		if (ca->pub->write_cam_control(ca->pub, slot,
					       CTRLIF_COMMAND,
					       CMDREG_RS) != 0) {
			pr_err("dvb_ca adapter %d: Unable to reset CAM IF\n",
			       ca->dvbdev->adapter->num);
			sl->slot_state = DVB_CA_SLOTSTATE_INVALID;
			dvb_ca_en50221_thread_update_delay(ca);
			break;
		}
		dprintk("DVB CAM validated successfully\n");

		sl->timeout = jiffies + (INIT_TIMEOUT_SECS * HZ);
		sl->slot_state = DVB_CA_SLOTSTATE_WAITFR;
		ca->wakeup = 1;
		break;

	case DVB_CA_SLOTSTATE_WAITFR:
		if (time_after(jiffies, sl->timeout)) {
			pr_err("dvb_ca adapter %d: DVB CAM did not respond :(\n",
			       ca->dvbdev->adapter->num);
			sl->slot_state = DVB_CA_SLOTSTATE_INVALID;
			dvb_ca_en50221_thread_update_delay(ca);
			break;
		}

		flags = ca->pub->read_cam_control(ca->pub, slot, CTRLIF_STATUS);
		if (flags & STATUSREG_FR) {
			sl->slot_state = DVB_CA_SLOTSTATE_LINKINIT;
			ca->wakeup = 1;
		}
		break;

	case DVB_CA_SLOTSTATE_LINKINIT:
		if (dvb_ca_en50221_link_init(ca, slot) != 0) {
			/*
			 * we need this extra check for annoying interfaces like
			 * the budget-av
			 */
			if ((!(ca->flags & DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE))
			    && (ca->pub->poll_slot_status)) {
				status = ca->pub->poll_slot_status(ca->pub,
								   slot, 0);
				if (!(status &
					DVB_CA_EN50221_POLL_CAM_PRESENT)) {
					sl->slot_state = DVB_CA_SLOTSTATE_NONE;
					dvb_ca_en50221_thread_update_delay(ca);
					break;
				}
			}

			pr_err("dvb_ca adapter %d: DVB CAM link initialisation failed :(\n",
			       ca->dvbdev->adapter->num);
			sl->slot_state = DVB_CA_SLOTSTATE_UNINITIALISED;
			dvb_ca_en50221_thread_update_delay(ca);
			break;
		}

		if (!sl->rx_buffer.data) {
			rxbuf = vmalloc(RX_BUFFER_SIZE);
			if (!rxbuf) {
				pr_err("dvb_ca adapter %d: Unable to allocate CAM rx buffer :(\n",
				       ca->dvbdev->adapter->num);
				sl->slot_state = DVB_CA_SLOTSTATE_INVALID;
				dvb_ca_en50221_thread_update_delay(ca);
				break;
			}
			dvb_ringbuffer_init(&sl->rx_buffer, rxbuf,
					    RX_BUFFER_SIZE);
		}

		ca->pub->slot_ts_enable(ca->pub, slot);
		sl->slot_state = DVB_CA_SLOTSTATE_RUNNING;
		dvb_ca_en50221_thread_update_delay(ca);
		pr_err("dvb_ca adapter %d: DVB CAM detected and initialised successfully\n",
		       ca->dvbdev->adapter->num);
		break;

	case DVB_CA_SLOTSTATE_RUNNING:
		if (!ca->open)
			break;

		/* poll slots for data */
		pktcount = 0;
		while (dvb_ca_en50221_read_data(ca, slot, NULL, 0) > 0) {
			if (!ca->open)
				break;

			/*
			 * if a CAMCHANGE occurred at some point, do not do any
			 * more processing of this slot
			 */
			if (dvb_ca_en50221_check_camstatus(ca, slot)) {
				/*
				 * we dont want to sleep on the next iteration
				 * so we can handle the cam change
				 */
				ca->wakeup = 1;
				break;
			}

			/* check if we've hit our limit this time */
			if (++pktcount >= MAX_RX_PACKETS_PER_ITERATION) {
				/*
				 * dont sleep; there is likely to be more data
				 * to read
				 */
				ca->wakeup = 1;
				break;
			}
		}
		break;
	}

	mutex_unlock(&sl->slot_lock);
}

/**
 * Kernel thread which monitors CA slots for CAM changes, and performs data
 * transfers.
 */
static int dvb_ca_en50221_thread(void *data)
{
	struct dvb_ca_private *ca = data;
	int slot;

	dprintk("%s\n", __func__);

	/* choose the correct initial delay */
	dvb_ca_en50221_thread_update_delay(ca);

	/* main loop */
	while (!kthread_should_stop()) {
		/* sleep for a bit */
		if (!ca->wakeup) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(ca->delay);
			if (kthread_should_stop())
				return 0;
		}
		ca->wakeup = 0;

		/* go through all the slots processing them */
		for (slot = 0; slot < ca->slot_count; slot++)
			dvb_ca_en50221_thread_state_machine(ca, slot);
	}

	return 0;
}



/* ******************************************************************************** */
/* EN50221 IO interface functions */

/**
 * Real ioctl implementation.
 * NOTE: CA_SEND_MSG/CA_GET_MSG ioctls have userspace buffers passed to them.
 *
 * @inode: Inode concerned.
 * @file: File concerned.
 * @cmd: IOCTL command.
 * @arg: Associated argument.
 *
 * @return 0 on success, <0 on error.
 */
static int dvb_ca_en50221_io_do_ioctl(struct file *file,
				      unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_ca_private *ca = dvbdev->priv;
	int err = 0;
	int slot;

	dprintk("%s\n", __func__);

	if (mutex_lock_interruptible(&ca->ioctl_mutex))
		return -ERESTARTSYS;

	switch (cmd) {
	case CA_RESET:
		for (slot = 0; slot < ca->slot_count; slot++) {
			mutex_lock(&ca->slot_info[slot].slot_lock);
			if (ca->slot_info[slot].slot_state != DVB_CA_SLOTSTATE_NONE) {
				dvb_ca_en50221_slot_shutdown(ca, slot);
				if (ca->flags & DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE)
					dvb_ca_en50221_camchange_irq(ca->pub,
								     slot,
								     DVB_CA_EN50221_CAMCHANGE_INSERTED);
			}
			mutex_unlock(&ca->slot_info[slot].slot_lock);
		}
		ca->next_read_slot = 0;
		dvb_ca_en50221_thread_wakeup(ca);
		break;

	case CA_GET_CAP: {
		struct ca_caps *caps = parg;

		caps->slot_num = ca->slot_count;
		caps->slot_type = CA_CI_LINK;
		caps->descr_num = 0;
		caps->descr_type = 0;
		break;
	}

	case CA_GET_SLOT_INFO: {
		struct ca_slot_info *info = parg;

		if ((info->num > ca->slot_count) || (info->num < 0)) {
			err = -EINVAL;
			goto out_unlock;
		}

		info->type = CA_CI_LINK;
		info->flags = 0;
		if ((ca->slot_info[info->num].slot_state != DVB_CA_SLOTSTATE_NONE)
			&& (ca->slot_info[info->num].slot_state != DVB_CA_SLOTSTATE_INVALID)) {
			info->flags = CA_CI_MODULE_PRESENT;
		}
		if (ca->slot_info[info->num].slot_state == DVB_CA_SLOTSTATE_RUNNING) {
			info->flags |= CA_CI_MODULE_READY;
		}
		break;
	}

	default:
		err = -EINVAL;
		break;
	}

out_unlock:
	mutex_unlock(&ca->ioctl_mutex);
	return err;
}


/**
 * Wrapper for ioctl implementation.
 *
 * @inode: Inode concerned.
 * @file: File concerned.
 * @cmd: IOCTL command.
 * @arg: Associated argument.
 *
 * @return 0 on success, <0 on error.
 */
static long dvb_ca_en50221_io_ioctl(struct file *file,
				    unsigned int cmd, unsigned long arg)
{
	return dvb_usercopy(file, cmd, arg, dvb_ca_en50221_io_do_ioctl);
}


/**
 * Implementation of write() syscall.
 *
 * @file: File structure.
 * @buf: Source buffer.
 * @count: Size of source buffer.
 * @ppos: Position in file (ignored).
 *
 * @return Number of bytes read, or <0 on error.
 */
static ssize_t dvb_ca_en50221_io_write(struct file *file,
				       const char __user *buf, size_t count,
				       loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_ca_private *ca = dvbdev->priv;
	u8 slot, connection_id;
	int status;
	u8 fragbuf[HOST_LINK_BUF_SIZE];
	int fragpos = 0;
	int fraglen;
	unsigned long timeout;
	int written;

	dprintk("%s\n", __func__);

	/* Incoming packet has a 2 byte header. hdr[0] = slot_id, hdr[1] = connection_id */
	if (count < 2)
		return -EINVAL;

	/* extract slot & connection id */
	if (copy_from_user(&slot, buf, 1))
		return -EFAULT;
	if (copy_from_user(&connection_id, buf + 1, 1))
		return -EFAULT;
	buf += 2;
	count -= 2;

	/* check if the slot is actually running */
	if (ca->slot_info[slot].slot_state != DVB_CA_SLOTSTATE_RUNNING)
		return -EINVAL;

	/* fragment the packets & store in the buffer */
	while (fragpos < count) {
		fraglen = ca->slot_info[slot].link_buf_size - 2;
		if (fraglen < 0)
			break;
		if (fraglen > HOST_LINK_BUF_SIZE - 2)
			fraglen = HOST_LINK_BUF_SIZE - 2;
		if ((count - fragpos) < fraglen)
			fraglen = count - fragpos;

		fragbuf[0] = connection_id;
		fragbuf[1] = ((fragpos + fraglen) < count) ? 0x80 : 0x00;
		status = copy_from_user(fragbuf + 2, buf + fragpos, fraglen);
		if (status) {
			status = -EFAULT;
			goto exit;
		}

		timeout = jiffies + HZ / 2;
		written = 0;
		while (!time_after(jiffies, timeout)) {
			/* check the CAM hasn't been removed/reset in the meantime */
			if (ca->slot_info[slot].slot_state != DVB_CA_SLOTSTATE_RUNNING) {
				status = -EIO;
				goto exit;
			}

			mutex_lock(&ca->slot_info[slot].slot_lock);
			status = dvb_ca_en50221_write_data(ca, slot, fragbuf, fraglen + 2);
			mutex_unlock(&ca->slot_info[slot].slot_lock);
			if (status == (fraglen + 2)) {
				written = 1;
				break;
			}
			if (status != -EAGAIN)
				goto exit;

			msleep(1);
		}
		if (!written) {
			status = -EIO;
			goto exit;
		}

		fragpos += fraglen;
	}
	status = count + 2;

exit:
	return status;
}


/**
 * Condition for waking up in dvb_ca_en50221_io_read_condition
 */
static int dvb_ca_en50221_io_read_condition(struct dvb_ca_private *ca,
					    int *result, int *_slot)
{
	int slot;
	int slot_count = 0;
	int idx;
	size_t fraglen;
	int connection_id = -1;
	int found = 0;
	u8 hdr[2];

	slot = ca->next_read_slot;
	while ((slot_count < ca->slot_count) && (!found)) {
		if (ca->slot_info[slot].slot_state != DVB_CA_SLOTSTATE_RUNNING)
			goto nextslot;

		if (ca->slot_info[slot].rx_buffer.data == NULL) {
			return 0;
		}

		idx = dvb_ringbuffer_pkt_next(&ca->slot_info[slot].rx_buffer, -1, &fraglen);
		while (idx != -1) {
			dvb_ringbuffer_pkt_read(&ca->slot_info[slot].rx_buffer, idx, 0, hdr, 2);
			if (connection_id == -1)
				connection_id = hdr[0];
			if ((hdr[0] == connection_id) && ((hdr[1] & 0x80) == 0)) {
				*_slot = slot;
				found = 1;
				break;
			}

			idx = dvb_ringbuffer_pkt_next(&ca->slot_info[slot].rx_buffer, idx, &fraglen);
		}

nextslot:
		slot = (slot + 1) % ca->slot_count;
		slot_count++;
	}

	ca->next_read_slot = slot;
	return found;
}


/**
 * Implementation of read() syscall.
 *
 * @file: File structure.
 * @buf: Destination buffer.
 * @count: Size of destination buffer.
 * @ppos: Position in file (ignored).
 *
 * @return Number of bytes read, or <0 on error.
 */
static ssize_t dvb_ca_en50221_io_read(struct file *file, char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_ca_private *ca = dvbdev->priv;
	int status;
	int result = 0;
	u8 hdr[2];
	int slot;
	int connection_id = -1;
	size_t idx, idx2;
	int last_fragment = 0;
	size_t fraglen;
	int pktlen;
	int dispose = 0;

	dprintk("%s\n", __func__);

	/* Outgoing packet has a 2 byte header. hdr[0] = slot_id, hdr[1] = connection_id */
	if (count < 2)
		return -EINVAL;

	/* wait for some data */
	if ((status = dvb_ca_en50221_io_read_condition(ca, &result, &slot)) == 0) {

		/* if we're in nonblocking mode, exit immediately */
		if (file->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;

		/* wait for some data */
		status = wait_event_interruptible(ca->wait_queue,
						  dvb_ca_en50221_io_read_condition
						  (ca, &result, &slot));
	}
	if ((status < 0) || (result < 0)) {
		if (result)
			return result;
		return status;
	}

	idx = dvb_ringbuffer_pkt_next(&ca->slot_info[slot].rx_buffer, -1, &fraglen);
	pktlen = 2;
	do {
		if (idx == -1) {
			pr_err("dvb_ca adapter %d: BUG: read packet ended before last_fragment encountered\n",
			       ca->dvbdev->adapter->num);
			status = -EIO;
			goto exit;
		}

		dvb_ringbuffer_pkt_read(&ca->slot_info[slot].rx_buffer, idx, 0, hdr, 2);
		if (connection_id == -1)
			connection_id = hdr[0];
		if (hdr[0] == connection_id) {
			if (pktlen < count) {
				if ((pktlen + fraglen - 2) > count) {
					fraglen = count - pktlen;
				} else {
					fraglen -= 2;
				}

				if ((status = dvb_ringbuffer_pkt_read_user(&ca->slot_info[slot].rx_buffer, idx, 2,
								      buf + pktlen, fraglen)) < 0) {
					goto exit;
				}
				pktlen += fraglen;
			}

			if ((hdr[1] & 0x80) == 0)
				last_fragment = 1;
			dispose = 1;
		}

		idx2 = dvb_ringbuffer_pkt_next(&ca->slot_info[slot].rx_buffer, idx, &fraglen);
		if (dispose)
			dvb_ringbuffer_pkt_dispose(&ca->slot_info[slot].rx_buffer, idx);
		idx = idx2;
		dispose = 0;
	} while (!last_fragment);

	hdr[0] = slot;
	hdr[1] = connection_id;
	status = copy_to_user(buf, hdr, 2);
	if (status) {
		status = -EFAULT;
		goto exit;
	}
	status = pktlen;

exit:
	return status;
}


/**
 * Implementation of file open syscall.
 *
 * @inode: Inode concerned.
 * @file: File concerned.
 *
 * @return 0 on success, <0 on failure.
 */
static int dvb_ca_en50221_io_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_ca_private *ca = dvbdev->priv;
	int err;
	int i;

	dprintk("%s\n", __func__);

	if (!try_module_get(ca->pub->owner))
		return -EIO;

	err = dvb_generic_open(inode, file);
	if (err < 0) {
		module_put(ca->pub->owner);
		return err;
	}

	for (i = 0; i < ca->slot_count; i++) {

		if (ca->slot_info[i].slot_state == DVB_CA_SLOTSTATE_RUNNING) {
			if (ca->slot_info[i].rx_buffer.data != NULL) {
				/* it is safe to call this here without locks because
				 * ca->open == 0. Data is not read in this case */
				dvb_ringbuffer_flush(&ca->slot_info[i].rx_buffer);
			}
		}
	}

	ca->open = 1;
	dvb_ca_en50221_thread_update_delay(ca);
	dvb_ca_en50221_thread_wakeup(ca);

	dvb_ca_private_get(ca);

	return 0;
}


/**
 * Implementation of file close syscall.
 *
 * @inode: Inode concerned.
 * @file: File concerned.
 *
 * @return 0 on success, <0 on failure.
 */
static int dvb_ca_en50221_io_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_ca_private *ca = dvbdev->priv;
	int err;

	dprintk("%s\n", __func__);

	/* mark the CA device as closed */
	ca->open = 0;
	dvb_ca_en50221_thread_update_delay(ca);

	err = dvb_generic_release(inode, file);

	module_put(ca->pub->owner);

	dvb_ca_private_put(ca);

	return err;
}


/**
 * Implementation of poll() syscall.
 *
 * @file: File concerned.
 * @wait: poll wait table.
 *
 * @return Standard poll mask.
 */
static unsigned int dvb_ca_en50221_io_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_ca_private *ca = dvbdev->priv;
	unsigned int mask = 0;
	int slot;
	int result = 0;

	dprintk("%s\n", __func__);

	if (dvb_ca_en50221_io_read_condition(ca, &result, &slot) == 1) {
		mask |= POLLIN;
	}

	/* if there is something, return now */
	if (mask)
		return mask;

	/* wait for something to happen */
	poll_wait(file, &ca->wait_queue, wait);

	if (dvb_ca_en50221_io_read_condition(ca, &result, &slot) == 1) {
		mask |= POLLIN;
	}

	return mask;
}
EXPORT_SYMBOL(dvb_ca_en50221_init);


static const struct file_operations dvb_ca_fops = {
	.owner = THIS_MODULE,
	.read = dvb_ca_en50221_io_read,
	.write = dvb_ca_en50221_io_write,
	.unlocked_ioctl = dvb_ca_en50221_io_ioctl,
	.open = dvb_ca_en50221_io_open,
	.release = dvb_ca_en50221_io_release,
	.poll = dvb_ca_en50221_io_poll,
	.llseek = noop_llseek,
};

static const struct dvb_device dvbdev_ca = {
	.priv = NULL,
	.users = 1,
	.readers = 1,
	.writers = 1,
#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
	.name = "dvb-ca-en50221",
#endif
	.fops = &dvb_ca_fops,
};

/* ******************************************************************************** */
/* Initialisation/shutdown functions */


/**
 * Initialise a new DVB CA EN50221 interface device.
 *
 * @dvb_adapter: DVB adapter to attach the new CA device to.
 * @ca: The dvb_ca instance.
 * @flags: Flags describing the CA device (DVB_CA_FLAG_*).
 * @slot_count: Number of slots supported.
 *
 * @return 0 on success, nonzero on failure
 */
int dvb_ca_en50221_init(struct dvb_adapter *dvb_adapter,
			struct dvb_ca_en50221 *pubca, int flags, int slot_count)
{
	int ret;
	struct dvb_ca_private *ca = NULL;
	int i;

	dprintk("%s\n", __func__);

	if (slot_count < 1)
		return -EINVAL;

	/* initialise the system data */
	if ((ca = kzalloc(sizeof(struct dvb_ca_private), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto exit;
	}
	kref_init(&ca->refcount);
	ca->pub = pubca;
	ca->flags = flags;
	ca->slot_count = slot_count;
	if ((ca->slot_info = kcalloc(slot_count, sizeof(struct dvb_ca_slot), GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto free_ca;
	}
	init_waitqueue_head(&ca->wait_queue);
	ca->open = 0;
	ca->wakeup = 0;
	ca->next_read_slot = 0;
	pubca->private = ca;

	/* register the DVB device */
	ret = dvb_register_device(dvb_adapter, &ca->dvbdev, &dvbdev_ca, ca, DVB_DEVICE_CA, 0);
	if (ret)
		goto free_slot_info;

	/* now initialise each slot */
	for (i = 0; i < slot_count; i++) {
		memset(&ca->slot_info[i], 0, sizeof(struct dvb_ca_slot));
		ca->slot_info[i].slot_state = DVB_CA_SLOTSTATE_NONE;
		atomic_set(&ca->slot_info[i].camchange_count, 0);
		ca->slot_info[i].camchange_type = DVB_CA_EN50221_CAMCHANGE_REMOVED;
		mutex_init(&ca->slot_info[i].slot_lock);
	}

	mutex_init(&ca->ioctl_mutex);

	if (signal_pending(current)) {
		ret = -EINTR;
		goto unregister_device;
	}
	mb();

	/* create a kthread for monitoring this CA device */
	ca->thread = kthread_run(dvb_ca_en50221_thread, ca, "kdvb-ca-%i:%i",
				 ca->dvbdev->adapter->num, ca->dvbdev->id);
	if (IS_ERR(ca->thread)) {
		ret = PTR_ERR(ca->thread);
		pr_err("dvb_ca_init: failed to start kernel_thread (%d)\n",
		       ret);
		goto unregister_device;
	}
	return 0;

unregister_device:
	dvb_unregister_device(ca->dvbdev);
free_slot_info:
	kfree(ca->slot_info);
free_ca:
	kfree(ca);
exit:
	pubca->private = NULL;
	return ret;
}
EXPORT_SYMBOL(dvb_ca_en50221_release);



/**
 * Release a DVB CA EN50221 interface device.
 *
 * @ca_dev: The dvb_device_t instance for the CA device.
 * @ca: The associated dvb_ca instance.
 */
void dvb_ca_en50221_release(struct dvb_ca_en50221 *pubca)
{
	struct dvb_ca_private *ca = pubca->private;
	int i;

	dprintk("%s\n", __func__);

	/* shutdown the thread if there was one */
	kthread_stop(ca->thread);

	for (i = 0; i < ca->slot_count; i++) {
		dvb_ca_en50221_slot_shutdown(ca, i);
	}
	dvb_remove_device(ca->dvbdev);
	dvb_ca_private_put(ca);
	pubca->private = NULL;
}
