/*
 * TerraTec Cinergy T²/qanu USB2 DVB-T adapter.
 *
 * Copyright (C) 2004 Daniel Mack <daniel@qanu.de> and
 *		    Holger Waechtler <holger@qanu.de>
 *
 *  Protocol Spec published on http://qanu.de/specs/terratec_cinergyT2.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/pci.h>
#include <linux/input.h>
#include <linux/dvb/frontend.h>
#include <linux/mutex.h>

#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_net.h"

#ifdef CONFIG_DVB_CINERGYT2_TUNING
	#define STREAM_URB_COUNT (CONFIG_DVB_CINERGYT2_STREAM_URB_COUNT)
	#define STREAM_BUF_SIZE (CONFIG_DVB_CINERGYT2_STREAM_BUF_SIZE)
	#define QUERY_INTERVAL (CONFIG_DVB_CINERGYT2_QUERY_INTERVAL)
	#ifdef CONFIG_DVB_CINERGYT2_ENABLE_RC_INPUT_DEVICE
		#define RC_QUERY_INTERVAL (CONFIG_DVB_CINERGYT2_RC_QUERY_INTERVAL)
		#define ENABLE_RC (1)
	#endif
#else
	#define STREAM_URB_COUNT (32)
	#define STREAM_BUF_SIZE (512)	/* bytes */
	#define ENABLE_RC (1)
	#define RC_QUERY_INTERVAL (50)	/* milliseconds */
	#define QUERY_INTERVAL (333)	/* milliseconds */
#endif

#define DRIVER_NAME "TerraTec/qanu USB2.0 Highspeed DVB-T Receiver"

static int debug;
module_param_named(debug, debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define dprintk(level, args...)						\
do {									\
	if ((debug & level)) {						\
		printk("%s: %s(): ", KBUILD_MODNAME,			\
		       __FUNCTION__);					\
		printk(args); }						\
} while (0)

enum cinergyt2_ep1_cmd {
	CINERGYT2_EP1_PID_TABLE_RESET		= 0x01,
	CINERGYT2_EP1_PID_SETUP			= 0x02,
	CINERGYT2_EP1_CONTROL_STREAM_TRANSFER	= 0x03,
	CINERGYT2_EP1_SET_TUNER_PARAMETERS	= 0x04,
	CINERGYT2_EP1_GET_TUNER_STATUS		= 0x05,
	CINERGYT2_EP1_START_SCAN		= 0x06,
	CINERGYT2_EP1_CONTINUE_SCAN		= 0x07,
	CINERGYT2_EP1_GET_RC_EVENTS		= 0x08,
	CINERGYT2_EP1_SLEEP_MODE		= 0x09
};

struct dvbt_set_parameters_msg {
	uint8_t cmd;
	uint32_t freq;
	uint8_t bandwidth;
	uint16_t tps;
	uint8_t flags;
} __attribute__((packed));

struct dvbt_get_status_msg {
	uint32_t freq;
	uint8_t bandwidth;
	uint16_t tps;
	uint8_t flags;
	uint16_t gain;
	uint8_t snr;
	uint32_t viterbi_error_rate;
	uint32_t rs_error_rate;
	uint32_t uncorrected_block_count;
	uint8_t lock_bits;
	uint8_t prev_lock_bits;
} __attribute__((packed));

static struct dvb_frontend_info cinergyt2_fe_info = {
	.name = DRIVER_NAME,
	.type = FE_OFDM,
	.frequency_min = 174000000,
	.frequency_max = 862000000,
	.frequency_stepsize = 166667,
	.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
		FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
		FE_CAN_FEC_AUTO |
		FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
		FE_CAN_HIERARCHY_AUTO | FE_CAN_RECOVER | FE_CAN_MUTE_TS
};

struct cinergyt2 {
	struct dvb_demux demux;
	struct usb_device *udev;
	struct mutex sem;
	struct dvb_adapter adapter;
	struct dvb_device *fedev;
	struct dmxdev dmxdev;
	struct dvb_net dvbnet;

	int streaming;
	int sleeping;

	struct dvbt_set_parameters_msg param;
	struct dvbt_get_status_msg status;
	struct work_struct query_work;

	wait_queue_head_t poll_wq;
	int pending_fe_events;
	int disconnect_pending;
	atomic_t inuse;

	void *streambuf;
	dma_addr_t streambuf_dmahandle;
	struct urb *stream_urb [STREAM_URB_COUNT];

#ifdef ENABLE_RC
	struct input_dev *rc_input_dev;
	char phys[64];
	struct work_struct rc_query_work;
	int rc_input_event;
	u32 rc_last_code;
	unsigned long last_event_jiffies;
#endif
};

enum {
	CINERGYT2_RC_EVENT_TYPE_NONE = 0x00,
	CINERGYT2_RC_EVENT_TYPE_NEC  = 0x01,
	CINERGYT2_RC_EVENT_TYPE_RC5  = 0x02
};

struct cinergyt2_rc_event {
	char type;
	uint32_t value;
} __attribute__((packed));

static const uint32_t rc_keys[] = {
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xfe01eb04,	KEY_POWER,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xfd02eb04,	KEY_1,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xfc03eb04,	KEY_2,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xfb04eb04,	KEY_3,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xfa05eb04,	KEY_4,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf906eb04,	KEY_5,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf807eb04,	KEY_6,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf708eb04,	KEY_7,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf609eb04,	KEY_8,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf50aeb04,	KEY_9,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf30ceb04,	KEY_0,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf40beb04,	KEY_VIDEO,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf20deb04,	KEY_REFRESH,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf10eeb04,	KEY_SELECT,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xf00feb04,	KEY_EPG,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xef10eb04,	KEY_UP,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xeb14eb04,	KEY_DOWN,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xee11eb04,	KEY_LEFT,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xec13eb04,	KEY_RIGHT,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xed12eb04,	KEY_OK,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xea15eb04,	KEY_TEXT,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe916eb04,	KEY_INFO,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe817eb04,	KEY_RED,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe718eb04,	KEY_GREEN,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe619eb04,	KEY_YELLOW,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe51aeb04,	KEY_BLUE,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe31ceb04,	KEY_VOLUMEUP,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe11eeb04,	KEY_VOLUMEDOWN,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe21deb04,	KEY_MUTE,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe41beb04,	KEY_CHANNELUP,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xe01feb04,	KEY_CHANNELDOWN,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xbf40eb04,	KEY_PAUSE,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xb34ceb04,	KEY_PLAY,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xa758eb04,	KEY_RECORD,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xab54eb04,	KEY_PREVIOUS,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xb748eb04,	KEY_STOP,
	CINERGYT2_RC_EVENT_TYPE_NEC,	0xa35ceb04,	KEY_NEXT
};

static int cinergyt2_command (struct cinergyt2 *cinergyt2,
			      char *send_buf, int send_buf_len,
			      char *recv_buf, int recv_buf_len)
{
	int actual_len;
	char dummy;
	int ret;

	ret = usb_bulk_msg(cinergyt2->udev, usb_sndbulkpipe(cinergyt2->udev, 1),
			   send_buf, send_buf_len, &actual_len, 1000);

	if (ret)
		dprintk(1, "usb_bulk_msg (send) failed, err %i\n", ret);

	if (!recv_buf)
		recv_buf = &dummy;

	ret = usb_bulk_msg(cinergyt2->udev, usb_rcvbulkpipe(cinergyt2->udev, 1),
			   recv_buf, recv_buf_len, &actual_len, 1000);

	if (ret)
		dprintk(1, "usb_bulk_msg (read) failed, err %i\n", ret);

	return ret ? ret : actual_len;
}

static void cinergyt2_control_stream_transfer (struct cinergyt2 *cinergyt2, int enable)
{
	char buf [] = { CINERGYT2_EP1_CONTROL_STREAM_TRANSFER, enable ? 1 : 0 };
	cinergyt2_command(cinergyt2, buf, sizeof(buf), NULL, 0);
}

static void cinergyt2_sleep (struct cinergyt2 *cinergyt2, int sleep)
{
	char buf [] = { CINERGYT2_EP1_SLEEP_MODE, sleep ? 1 : 0 };
	cinergyt2_command(cinergyt2, buf, sizeof(buf), NULL, 0);
	cinergyt2->sleeping = sleep;
}

static void cinergyt2_stream_irq (struct urb *urb, struct pt_regs *regs);

static int cinergyt2_submit_stream_urb (struct cinergyt2 *cinergyt2, struct urb *urb)
{
	int err;

	usb_fill_bulk_urb(urb,
			  cinergyt2->udev,
			  usb_rcvbulkpipe(cinergyt2->udev, 0x2),
			  urb->transfer_buffer,
			  STREAM_BUF_SIZE,
			  cinergyt2_stream_irq,
			  cinergyt2);

	if ((err = usb_submit_urb(urb, GFP_ATOMIC)))
		dprintk(1, "urb submission failed (err = %i)!\n", err);

	return err;
}

static void cinergyt2_stream_irq (struct urb *urb, struct pt_regs *regs)
{
	struct cinergyt2 *cinergyt2 = urb->context;

	if (urb->actual_length > 0)
		dvb_dmx_swfilter(&cinergyt2->demux,
				 urb->transfer_buffer, urb->actual_length);

	if (cinergyt2->streaming)
		cinergyt2_submit_stream_urb(cinergyt2, urb);
}

static void cinergyt2_free_stream_urbs (struct cinergyt2 *cinergyt2)
{
	int i;

	for (i=0; i<STREAM_URB_COUNT; i++)
		if (cinergyt2->stream_urb[i])
			usb_free_urb(cinergyt2->stream_urb[i]);

	usb_buffer_free(cinergyt2->udev, STREAM_URB_COUNT*STREAM_BUF_SIZE,
			    cinergyt2->streambuf, cinergyt2->streambuf_dmahandle);
}

static int cinergyt2_alloc_stream_urbs (struct cinergyt2 *cinergyt2)
{
	int i;

	cinergyt2->streambuf = usb_buffer_alloc(cinergyt2->udev, STREAM_URB_COUNT*STREAM_BUF_SIZE,
					      SLAB_KERNEL, &cinergyt2->streambuf_dmahandle);
	if (!cinergyt2->streambuf) {
		dprintk(1, "failed to alloc consistent stream memory area, bailing out!\n");
		return -ENOMEM;
	}

	memset(cinergyt2->streambuf, 0, STREAM_URB_COUNT*STREAM_BUF_SIZE);

	for (i=0; i<STREAM_URB_COUNT; i++) {
		struct urb *urb;

		if (!(urb = usb_alloc_urb(0, GFP_ATOMIC))) {
			dprintk(1, "failed to alloc consistent stream urbs, bailing out!\n");
			cinergyt2_free_stream_urbs(cinergyt2);
			return -ENOMEM;
		}

		urb->transfer_buffer = cinergyt2->streambuf + i * STREAM_BUF_SIZE;
		urb->transfer_buffer_length = STREAM_BUF_SIZE;

		cinergyt2->stream_urb[i] = urb;
	}

	return 0;
}

static void cinergyt2_stop_stream_xfer (struct cinergyt2 *cinergyt2)
{
	int i;

	cinergyt2_control_stream_transfer(cinergyt2, 0);

	for (i=0; i<STREAM_URB_COUNT; i++)
		if (cinergyt2->stream_urb[i])
			usb_kill_urb(cinergyt2->stream_urb[i]);
}

static int cinergyt2_start_stream_xfer (struct cinergyt2 *cinergyt2)
{
	int i, err;

	for (i=0; i<STREAM_URB_COUNT; i++) {
		if ((err = cinergyt2_submit_stream_urb(cinergyt2, cinergyt2->stream_urb[i]))) {
			cinergyt2_stop_stream_xfer(cinergyt2);
			dprintk(1, "failed urb submission (%i: err = %i)!\n", i, err);
			return err;
		}
	}

	cinergyt2_control_stream_transfer(cinergyt2, 1);
	return 0;
}

static int cinergyt2_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *demux = dvbdmxfeed->demux;
	struct cinergyt2 *cinergyt2 = demux->priv;

	if (cinergyt2->disconnect_pending || mutex_lock_interruptible(&cinergyt2->sem))
		return -ERESTARTSYS;

	if (cinergyt2->streaming == 0)
		cinergyt2_start_stream_xfer(cinergyt2);

	cinergyt2->streaming++;
	mutex_unlock(&cinergyt2->sem);
	return 0;
}

static int cinergyt2_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *demux = dvbdmxfeed->demux;
	struct cinergyt2 *cinergyt2 = demux->priv;

	if (cinergyt2->disconnect_pending || mutex_lock_interruptible(&cinergyt2->sem))
		return -ERESTARTSYS;

	if (--cinergyt2->streaming == 0)
		cinergyt2_stop_stream_xfer(cinergyt2);

	mutex_unlock(&cinergyt2->sem);
	return 0;
}

/**
 *  convert linux-dvb frontend parameter set into TPS.
 *  See ETSI ETS-300744, section 4.6.2, table 9 for details.
 *
 *  This function is probably reusable and may better get placed in a support
 *  library.
 *
 *  We replace errornous fields by default TPS fields (the ones with value 0).
 */
static uint16_t compute_tps (struct dvb_frontend_parameters *p)
{
	struct dvb_ofdm_parameters *op = &p->u.ofdm;
	uint16_t tps = 0;

	switch (op->code_rate_HP) {
		case FEC_2_3:
			tps |= (1 << 7);
			break;
		case FEC_3_4:
			tps |= (2 << 7);
			break;
		case FEC_5_6:
			tps |= (3 << 7);
			break;
		case FEC_7_8:
			tps |= (4 << 7);
			break;
		case FEC_1_2:
		case FEC_AUTO:
		default:
			/* tps |= (0 << 7) */;
	}

	switch (op->code_rate_LP) {
		case FEC_2_3:
			tps |= (1 << 4);
			break;
		case FEC_3_4:
			tps |= (2 << 4);
			break;
		case FEC_5_6:
			tps |= (3 << 4);
			break;
		case FEC_7_8:
			tps |= (4 << 4);
			break;
		case FEC_1_2:
		case FEC_AUTO:
		default:
			/* tps |= (0 << 4) */;
	}

	switch (op->constellation) {
		case QAM_16:
			tps |= (1 << 13);
			break;
		case QAM_64:
			tps |= (2 << 13);
			break;
		case QPSK:
		default:
			/* tps |= (0 << 13) */;
	}

	switch (op->transmission_mode) {
		case TRANSMISSION_MODE_8K:
			tps |= (1 << 0);
			break;
		case TRANSMISSION_MODE_2K:
		default:
			/* tps |= (0 << 0) */;
	}

	switch (op->guard_interval) {
		case GUARD_INTERVAL_1_16:
			tps |= (1 << 2);
			break;
		case GUARD_INTERVAL_1_8:
			tps |= (2 << 2);
			break;
		case GUARD_INTERVAL_1_4:
			tps |= (3 << 2);
			break;
		case GUARD_INTERVAL_1_32:
		default:
			/* tps |= (0 << 2) */;
	}

	switch (op->hierarchy_information) {
		case HIERARCHY_1:
			tps |= (1 << 10);
			break;
		case HIERARCHY_2:
			tps |= (2 << 10);
			break;
		case HIERARCHY_4:
			tps |= (3 << 10);
			break;
		case HIERARCHY_NONE:
		default:
			/* tps |= (0 << 10) */;
	}

	return tps;
}

static int cinergyt2_open (struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct cinergyt2 *cinergyt2 = dvbdev->priv;
	int err = -ERESTARTSYS;

	if (cinergyt2->disconnect_pending || mutex_lock_interruptible(&cinergyt2->sem))
		return -ERESTARTSYS;

	if ((err = dvb_generic_open(inode, file))) {
		mutex_unlock(&cinergyt2->sem);
		return err;
	}


	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		cinergyt2_sleep(cinergyt2, 0);
		schedule_delayed_work(&cinergyt2->query_work, HZ/2);
	}

	atomic_inc(&cinergyt2->inuse);

	mutex_unlock(&cinergyt2->sem);
	return 0;
}

static void cinergyt2_unregister(struct cinergyt2 *cinergyt2)
{
	dvb_net_release(&cinergyt2->dvbnet);
	dvb_dmxdev_release(&cinergyt2->dmxdev);
	dvb_dmx_release(&cinergyt2->demux);
	dvb_unregister_device(cinergyt2->fedev);
	dvb_unregister_adapter(&cinergyt2->adapter);

	cinergyt2_free_stream_urbs(cinergyt2);
	kfree(cinergyt2);
}

static int cinergyt2_release (struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct cinergyt2 *cinergyt2 = dvbdev->priv;

	if (mutex_lock_interruptible(&cinergyt2->sem))
		return -ERESTARTSYS;

	if (!cinergyt2->disconnect_pending && (file->f_flags & O_ACCMODE) != O_RDONLY) {
		cancel_delayed_work(&cinergyt2->query_work);
		flush_scheduled_work();
		cinergyt2_sleep(cinergyt2, 1);
	}

	mutex_unlock(&cinergyt2->sem);

	if (atomic_dec_and_test(&cinergyt2->inuse) && cinergyt2->disconnect_pending) {
		warn("delayed unregister in release");
		cinergyt2_unregister(cinergyt2);
	}

	return dvb_generic_release(inode, file);
}

static unsigned int cinergyt2_poll (struct file *file, struct poll_table_struct *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct cinergyt2 *cinergyt2 = dvbdev->priv;

	if (cinergyt2->disconnect_pending || mutex_lock_interruptible(&cinergyt2->sem))
		return -ERESTARTSYS;

	poll_wait(file, &cinergyt2->poll_wq, wait);

	mutex_unlock(&cinergyt2->sem);

	return (POLLIN | POLLRDNORM | POLLPRI);
}


static int cinergyt2_ioctl (struct inode *inode, struct file *file,
		     unsigned cmd, unsigned long arg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct cinergyt2 *cinergyt2 = dvbdev->priv;
	struct dvbt_get_status_msg *stat = &cinergyt2->status;
	fe_status_t status = 0;

	switch (cmd) {
	case FE_GET_INFO:
		return copy_to_user((void __user*) arg, &cinergyt2_fe_info,
				    sizeof(struct dvb_frontend_info));

	case FE_READ_STATUS:
		if (0xffff - le16_to_cpu(stat->gain) > 30)
			status |= FE_HAS_SIGNAL;
		if (stat->lock_bits & (1 << 6))
			status |= FE_HAS_LOCK;
		if (stat->lock_bits & (1 << 5))
			status |= FE_HAS_SYNC;
		if (stat->lock_bits & (1 << 4))
			status |= FE_HAS_CARRIER;
		if (stat->lock_bits & (1 << 1))
			status |= FE_HAS_VITERBI;

		return copy_to_user((void  __user*) arg, &status, sizeof(status));

	case FE_READ_BER:
		return put_user(le32_to_cpu(stat->viterbi_error_rate),
				(__u32 __user *) arg);

	case FE_READ_SIGNAL_STRENGTH:
		return put_user(0xffff - le16_to_cpu(stat->gain),
				(__u16 __user *) arg);

	case FE_READ_SNR:
		return put_user((stat->snr << 8) | stat->snr,
				(__u16 __user *) arg);

	case FE_READ_UNCORRECTED_BLOCKS:
	{
		uint32_t unc_count;

		unc_count = stat->uncorrected_block_count;
		stat->uncorrected_block_count = 0;

		/* UNC are already converted to host byte order... */
		return put_user(unc_count,(__u32 __user *) arg);
	}
	case FE_SET_FRONTEND:
	{
		struct dvbt_set_parameters_msg *param = &cinergyt2->param;
		struct dvb_frontend_parameters p;
		int err;

		if ((file->f_flags & O_ACCMODE) == O_RDONLY)
			return -EPERM;

		if (copy_from_user(&p, (void  __user*) arg, sizeof(p)))
			return -EFAULT;

		if (cinergyt2->disconnect_pending || mutex_lock_interruptible(&cinergyt2->sem))
			return -ERESTARTSYS;

		param->cmd = CINERGYT2_EP1_SET_TUNER_PARAMETERS;
		param->tps = cpu_to_le16(compute_tps(&p));
		param->freq = cpu_to_le32(p.frequency / 1000);
		param->bandwidth = 8 - p.u.ofdm.bandwidth - BANDWIDTH_8_MHZ;

		stat->lock_bits = 0;
		cinergyt2->pending_fe_events++;
		wake_up_interruptible(&cinergyt2->poll_wq);

		err = cinergyt2_command(cinergyt2,
					(char *) param, sizeof(*param),
					NULL, 0);

		mutex_unlock(&cinergyt2->sem);

		return (err < 0) ? err : 0;
	}

	case FE_GET_FRONTEND:
		/**
		 *  trivial to implement (see struct dvbt_get_status_msg).
		 *  equivalent to FE_READ ioctls, but needs
		 *  TPS -> linux-dvb parameter set conversion. Feel free
		 *  to implement this and send us a patch if you need this
		 *  functionality.
		 */
		break;

	case FE_GET_EVENT:
	{
		/**
		 *  for now we only fill the status field. the parameters
		 *  are trivial to fill as soon FE_GET_FRONTEND is done.
		 */
		struct dvb_frontend_event __user *e = (void __user *) arg;
		if (cinergyt2->pending_fe_events == 0) {
			if (file->f_flags & O_NONBLOCK)
				return -EWOULDBLOCK;
			wait_event_interruptible(cinergyt2->poll_wq,
						 cinergyt2->pending_fe_events > 0);
		}
		cinergyt2->pending_fe_events = 0;
		return cinergyt2_ioctl(inode, file, FE_READ_STATUS,
					(unsigned long) &e->status);
	}

	default:
		;
	}

	return -EINVAL;
}

static int cinergyt2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct dvb_device *dvbdev = file->private_data;
	struct cinergyt2 *cinergyt2 = dvbdev->priv;
	int ret = 0;

	lock_kernel();

	if (vma->vm_flags & (VM_WRITE | VM_EXEC)) {
		ret = -EPERM;
		goto bailout;
	}

	if (vma->vm_end > vma->vm_start + STREAM_URB_COUNT * STREAM_BUF_SIZE) {
		ret = -EINVAL;
		goto bailout;
	}

	vma->vm_flags |= (VM_IO | VM_DONTCOPY);
	vma->vm_file = file;

	ret = remap_pfn_range(vma, vma->vm_start,
			      virt_to_phys(cinergyt2->streambuf) >> PAGE_SHIFT,
			      vma->vm_end - vma->vm_start,
			      vma->vm_page_prot) ? -EAGAIN : 0;
bailout:
	unlock_kernel();
	return ret;
}

static struct file_operations cinergyt2_fops = {
	.owner          = THIS_MODULE,
	.ioctl		= cinergyt2_ioctl,
	.poll           = cinergyt2_poll,
	.open           = cinergyt2_open,
	.release        = cinergyt2_release,
	.mmap		= cinergyt2_mmap
};

static struct dvb_device cinergyt2_fe_template = {
	.users = ~0,
	.writers = 1,
	.readers = (~0)-1,
	.fops = &cinergyt2_fops
};

#ifdef ENABLE_RC

static void cinergyt2_query_rc (void *data)
{
	struct cinergyt2 *cinergyt2 = data;
	char buf[1] = { CINERGYT2_EP1_GET_RC_EVENTS };
	struct cinergyt2_rc_event rc_events[12];
	int n, len, i;

	if (cinergyt2->disconnect_pending || mutex_lock_interruptible(&cinergyt2->sem))
		return;

	len = cinergyt2_command(cinergyt2, buf, sizeof(buf),
				(char *) rc_events, sizeof(rc_events));
	if (len < 0)
		goto out;
	if (len == 0) {
		if (time_after(jiffies, cinergyt2->last_event_jiffies +
			       msecs_to_jiffies(150))) {
			/* stop key repeat */
			if (cinergyt2->rc_input_event != KEY_MAX) {
				dprintk(1, "rc_input_event=%d Up\n", cinergyt2->rc_input_event);
				input_report_key(cinergyt2->rc_input_dev,
						 cinergyt2->rc_input_event, 0);
				cinergyt2->rc_input_event = KEY_MAX;
			}
			cinergyt2->rc_last_code = ~0;
		}
		goto out;
	}
	cinergyt2->last_event_jiffies = jiffies;

	for (n = 0; n < (len / sizeof(rc_events[0])); n++) {
		dprintk(1, "rc_events[%d].value = %x, type=%x\n",
			n, le32_to_cpu(rc_events[n].value), rc_events[n].type);

		if (rc_events[n].type == CINERGYT2_RC_EVENT_TYPE_NEC &&
		    rc_events[n].value == ~0) {
			/* keyrepeat bit -> just repeat last rc_input_event */
		} else {
			cinergyt2->rc_input_event = KEY_MAX;
			for (i = 0; i < ARRAY_SIZE(rc_keys); i += 3) {
				if (rc_keys[i + 0] == rc_events[n].type &&
				    rc_keys[i + 1] == le32_to_cpu(rc_events[n].value)) {
					cinergyt2->rc_input_event = rc_keys[i + 2];
					break;
				}
			}
		}

		if (cinergyt2->rc_input_event != KEY_MAX) {
			if (rc_events[n].value == cinergyt2->rc_last_code &&
			    cinergyt2->rc_last_code != ~0) {
				/* emit a key-up so the double event is recognized */
				dprintk(1, "rc_input_event=%d UP\n", cinergyt2->rc_input_event);
				input_report_key(cinergyt2->rc_input_dev,
						 cinergyt2->rc_input_event, 0);
			}
			dprintk(1, "rc_input_event=%d\n", cinergyt2->rc_input_event);
			input_report_key(cinergyt2->rc_input_dev,
					 cinergyt2->rc_input_event, 1);
			cinergyt2->rc_last_code = rc_events[n].value;
		}
	}

out:
	schedule_delayed_work(&cinergyt2->rc_query_work,
			      msecs_to_jiffies(RC_QUERY_INTERVAL));

	mutex_unlock(&cinergyt2->sem);
}

static int cinergyt2_register_rc(struct cinergyt2 *cinergyt2)
{
	struct input_dev *input_dev;
	int i;

	cinergyt2->rc_input_dev = input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	usb_make_path(cinergyt2->udev, cinergyt2->phys, sizeof(cinergyt2->phys));
	strlcat(cinergyt2->phys, "/input0", sizeof(cinergyt2->phys));
	cinergyt2->rc_input_event = KEY_MAX;
	cinergyt2->rc_last_code = ~0;
	INIT_WORK(&cinergyt2->rc_query_work, cinergyt2_query_rc, cinergyt2);

	input_dev->name = DRIVER_NAME " remote control";
	input_dev->phys = cinergyt2->phys;
	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REP);
	for (i = 0; i < ARRAY_SIZE(rc_keys); i += 3)
		set_bit(rc_keys[i + 2], input_dev->keybit);
	input_dev->keycodesize = 0;
	input_dev->keycodemax = 0;

	input_register_device(cinergyt2->rc_input_dev);
	schedule_delayed_work(&cinergyt2->rc_query_work, HZ/2);

	return 0;
}

static void cinergyt2_unregister_rc(struct cinergyt2 *cinergyt2)
{
	cancel_delayed_work(&cinergyt2->rc_query_work);
	input_unregister_device(cinergyt2->rc_input_dev);
}

static inline void cinergyt2_suspend_rc(struct cinergyt2 *cinergyt2)
{
	cancel_delayed_work(&cinergyt2->rc_query_work);
}

static inline void cinergyt2_resume_rc(struct cinergyt2 *cinergyt2)
{
	schedule_delayed_work(&cinergyt2->rc_query_work, HZ/2);
}

#else

static inline int cinergyt2_register_rc(struct cinergyt2 *cinergyt2) { return 0; }
static inline void cinergyt2_unregister_rc(struct cinergyt2 *cinergyt2) { }
static inline void cinergyt2_suspend_rc(struct cinergyt2 *cinergyt2) { }
static inline void cinergyt2_resume_rc(struct cinergyt2 *cinergyt2) { }

#endif /* ENABLE_RC */

static void cinergyt2_query (void *data)
{
	struct cinergyt2 *cinergyt2 = (struct cinergyt2 *) data;
	char cmd [] = { CINERGYT2_EP1_GET_TUNER_STATUS };
	struct dvbt_get_status_msg *s = &cinergyt2->status;
	uint8_t lock_bits;
	uint32_t unc;

	if (cinergyt2->disconnect_pending || mutex_lock_interruptible(&cinergyt2->sem))
		return;

	unc = s->uncorrected_block_count;
	lock_bits = s->lock_bits;

	cinergyt2_command(cinergyt2, cmd, sizeof(cmd), (char *) s, sizeof(*s));

	unc += le32_to_cpu(s->uncorrected_block_count);
	s->uncorrected_block_count = unc;

	if (lock_bits != s->lock_bits) {
		wake_up_interruptible(&cinergyt2->poll_wq);
		cinergyt2->pending_fe_events++;
	}

	schedule_delayed_work(&cinergyt2->query_work,
			      msecs_to_jiffies(QUERY_INTERVAL));

	mutex_unlock(&cinergyt2->sem);
}

static int cinergyt2_probe (struct usb_interface *intf,
		  const struct usb_device_id *id)
{
	struct cinergyt2 *cinergyt2;
	int err;

	if (!(cinergyt2 = kmalloc (sizeof(struct cinergyt2), GFP_KERNEL))) {
		dprintk(1, "out of memory?!?\n");
		return -ENOMEM;
	}

	memset (cinergyt2, 0, sizeof (struct cinergyt2));
	usb_set_intfdata (intf, (void *) cinergyt2);

	mutex_init(&cinergyt2->sem);
	init_waitqueue_head (&cinergyt2->poll_wq);
	INIT_WORK(&cinergyt2->query_work, cinergyt2_query, cinergyt2);

	cinergyt2->udev = interface_to_usbdev(intf);
	cinergyt2->param.cmd = CINERGYT2_EP1_SET_TUNER_PARAMETERS;

	if (cinergyt2_alloc_stream_urbs (cinergyt2) < 0) {
		dprintk(1, "unable to allocate stream urbs\n");
		kfree(cinergyt2);
		return -ENOMEM;
	}

	if ((err = dvb_register_adapter(&cinergyt2->adapter, DRIVER_NAME, THIS_MODULE)) < 0) {
		kfree(cinergyt2);
		return err;
	}

	cinergyt2->demux.priv = cinergyt2;
	cinergyt2->demux.filternum = 256;
	cinergyt2->demux.feednum = 256;
	cinergyt2->demux.start_feed = cinergyt2_start_feed;
	cinergyt2->demux.stop_feed = cinergyt2_stop_feed;
	cinergyt2->demux.dmx.capabilities = DMX_TS_FILTERING |
					    DMX_SECTION_FILTERING |
					    DMX_MEMORY_BASED_FILTERING;

	if ((err = dvb_dmx_init(&cinergyt2->demux)) < 0) {
		dprintk(1, "dvb_dmx_init() failed (err = %d)\n", err);
		goto bailout;
	}

	cinergyt2->dmxdev.filternum = cinergyt2->demux.filternum;
	cinergyt2->dmxdev.demux = &cinergyt2->demux.dmx;
	cinergyt2->dmxdev.capabilities = 0;

	if ((err = dvb_dmxdev_init(&cinergyt2->dmxdev, &cinergyt2->adapter)) < 0) {
		dprintk(1, "dvb_dmxdev_init() failed (err = %d)\n", err);
		goto bailout;
	}

	if (dvb_net_init(&cinergyt2->adapter, &cinergyt2->dvbnet, &cinergyt2->demux.dmx))
		dprintk(1, "dvb_net_init() failed!\n");

	dvb_register_device(&cinergyt2->adapter, &cinergyt2->fedev,
			    &cinergyt2_fe_template, cinergyt2,
			    DVB_DEVICE_FRONTEND);

	err = cinergyt2_register_rc(cinergyt2);
	if (err)
		goto bailout;

	return 0;

bailout:
	dvb_net_release(&cinergyt2->dvbnet);
	dvb_dmxdev_release(&cinergyt2->dmxdev);
	dvb_dmx_release(&cinergyt2->demux);
	dvb_unregister_adapter(&cinergyt2->adapter);
	cinergyt2_free_stream_urbs(cinergyt2);
	kfree(cinergyt2);
	return -ENOMEM;
}

static void cinergyt2_disconnect (struct usb_interface *intf)
{
	struct cinergyt2 *cinergyt2 = usb_get_intfdata (intf);

	flush_scheduled_work();

	cinergyt2_unregister_rc(cinergyt2);

	cancel_delayed_work(&cinergyt2->query_work);
	wake_up_interruptible(&cinergyt2->poll_wq);

	cinergyt2->demux.dmx.close(&cinergyt2->demux.dmx);
	cinergyt2->disconnect_pending = 1;

	if (!atomic_read(&cinergyt2->inuse))
		cinergyt2_unregister(cinergyt2);
}

static int cinergyt2_suspend (struct usb_interface *intf, pm_message_t state)
{
	struct cinergyt2 *cinergyt2 = usb_get_intfdata (intf);

	if (cinergyt2->disconnect_pending || mutex_lock_interruptible(&cinergyt2->sem))
		return -ERESTARTSYS;

	if (state.event > PM_EVENT_ON) {
		struct cinergyt2 *cinergyt2 = usb_get_intfdata (intf);

		cinergyt2_suspend_rc(cinergyt2);
		cancel_delayed_work(&cinergyt2->query_work);
		if (cinergyt2->streaming)
			cinergyt2_stop_stream_xfer(cinergyt2);
		flush_scheduled_work();
		cinergyt2_sleep(cinergyt2, 1);
	}

	mutex_unlock(&cinergyt2->sem);
	return 0;
}

static int cinergyt2_resume (struct usb_interface *intf)
{
	struct cinergyt2 *cinergyt2 = usb_get_intfdata (intf);
	struct dvbt_set_parameters_msg *param = &cinergyt2->param;

	if (cinergyt2->disconnect_pending || mutex_lock_interruptible(&cinergyt2->sem))
		return -ERESTARTSYS;

	if (!cinergyt2->sleeping) {
		cinergyt2_sleep(cinergyt2, 0);
		cinergyt2_command(cinergyt2, (char *) param, sizeof(*param), NULL, 0);
		if (cinergyt2->streaming)
			cinergyt2_start_stream_xfer(cinergyt2);
		schedule_delayed_work(&cinergyt2->query_work, HZ/2);
	}

	cinergyt2_resume_rc(cinergyt2);

	mutex_unlock(&cinergyt2->sem);
	return 0;
}

static const struct usb_device_id cinergyt2_table [] __devinitdata = {
	{ USB_DEVICE(0x0ccd, 0x0038) },
	{ 0 }
};

MODULE_DEVICE_TABLE(usb, cinergyt2_table);

static struct usb_driver cinergyt2_driver = {
	.name	= "cinergyT2",
	.probe	= cinergyt2_probe,
	.disconnect	= cinergyt2_disconnect,
	.suspend	= cinergyt2_suspend,
	.resume		= cinergyt2_resume,
	.id_table	= cinergyt2_table
};

static int __init cinergyt2_init (void)
{
	int err;

	if ((err = usb_register(&cinergyt2_driver)) < 0)
		dprintk(1, "usb_register() failed! (err %i)\n", err);

	return err;
}

static void __exit cinergyt2_exit (void)
{
	usb_deregister(&cinergyt2_driver);
}

module_init (cinergyt2_init);
module_exit (cinergyt2_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Holger Waechtler, Daniel Mack");
