/*
 *  Copyright (C) 2004 Bernd Porr, Bernd.Porr@f2s.com
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
 */

/*
 * I must give credit here to Chris Baugher who
 * wrote the driver for AT-MIO-16d. I used some parts of this
 * driver. I also must give credits to David Brownell
 * who supported me with the USB development.
 *
 * Bernd Porr
 *
 *
 * Revision history:
 * 0.9: Dropping the first data packet which seems to be from the last transfer.
 *      Buffer overflows in the FX2 are handed over to comedi.
 * 0.92: Dropping now 4 packets. The quad buffer has to be emptied.
 *       Added insn command basically for testing. Sample rate is
 *       1MHz/16ch=62.5kHz
 * 0.99: Ian Abbott pointed out a bug which has been corrected. Thanks!
 * 0.99a: added external trigger.
 * 1.00: added firmware kernel request to the driver which fixed
 *       udev coldplug problem
 */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/fcntl.h>
#include <linux/compiler.h>
#include "comedi_fc.h"
#include "../comedidev.h"

/*
 * timeout for the USB-transfer
 */
#define EZTIMEOUT	30

/*
 * constants for "firmware" upload and download
 */
#define FIRMWARE		"usbduxfast_firmware.bin"
#define USBDUXFASTSUB_FIRMWARE	0xA0
#define VENDOR_DIR_IN		0xC0
#define VENDOR_DIR_OUT		0x40

/*
 * internal addresses of the 8051 processor
 */
#define USBDUXFASTSUB_CPUCS	0xE600

/*
 * max lenghth of the transfer-buffer for software upload
 */
#define TB_LEN	0x2000

/*
 * input endpoint number
 */
#define BULKINEP	6

/*
 * endpoint for the A/D channellist: bulk OUT
 */
#define CHANNELLISTEP	4

/*
 * number of channels
 */
#define NUMCHANNELS	32

/*
 * size of the waveform descriptor
 */
#define WAVESIZE	0x20

/*
 * size of one A/D value
 */
#define SIZEADIN	(sizeof(int16_t))

/*
 * size of the input-buffer IN BYTES
 */
#define SIZEINBUF	512

/*
 * 16 bytes
 */
#define SIZEINSNBUF	512

/*
 * size of the buffer for the dux commands in bytes
 */
#define SIZEOFDUXBUFFER	256

/*
 * number of in-URBs which receive the data: min=5
 */
#define NUMOFINBUFFERSHIGH	10

/*
 * total number of usbduxfast devices
 */
#define NUMUSBDUXFAST	16

/*
 * analogue in subdevice
 */
#define SUBDEV_AD	0

/*
 * min delay steps for more than one channel
 * basically when the mux gives up ;-)
 *
 * steps at 30MHz in the FX2
 */
#define MIN_SAMPLING_PERIOD	9

/*
 * max number of 1/30MHz delay steps
 */
#define MAX_SAMPLING_PERIOD	500

/*
 * number of received packets to ignore before we start handing data
 * over to comedi, it's quad buffering and we have to ignore 4 packets
 */
#define PACKETS_TO_IGNORE	4

/*
 * comedi constants
 */
static const struct comedi_lrange range_usbduxfast_ai_range = {
	2, {BIP_RANGE(0.75), BIP_RANGE(0.5)}
};

/*
 * private structure of one subdevice
 *
 * this is the structure which holds all the data of this driver
 * one sub device just now: A/D
 */
struct usbduxfastsub_s {
	int attached;		/* is attached? */
	int probed;		/* is it associated with a subdevice? */
	struct usb_device *usbdev;	/* pointer to the usb-device */
	struct urb *urbIn;	/* BULK-transfer handling: urb */
	int8_t *transfer_buffer;
	int16_t *insnBuffer;	/* input buffer for single insn */
	int ifnum;		/* interface number */
	struct usb_interface *interface;	/* interface structure */
	/* comedi device for the interrupt context */
	struct comedi_device *comedidev;
	short int ai_cmd_running;	/* asynchronous command is running */
	short int ai_continous;	/* continous acquisition */
	long int ai_sample_count;	/* number of samples to acquire */
	uint8_t *dux_commands;	/* commands */
	int ignore;		/* counter which ignores the first
				   buffers */
	struct semaphore sem;
};

/*
 * The pointer to the private usb-data of the driver
 * is also the private data for the comedi-device.
 * This has to be global as the usb subsystem needs
 * global variables. The other reason is that this
 * structure must be there _before_ any comedi
 * command is issued. The usb subsystem must be
 * initialised before comedi can access it.
 */
static struct usbduxfastsub_s usbduxfastsub[NUMUSBDUXFAST];

static DEFINE_SEMAPHORE(start_stop_sem);

/*
 * bulk transfers to usbduxfast
 */
#define SENDADCOMMANDS            0
#define SENDINITEP6               1

static int send_dux_commands(struct usbduxfastsub_s *udfs, int cmd_type)
{
	int tmp, nsent;

	udfs->dux_commands[0] = cmd_type;

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi%d: usbduxfast: dux_commands: ",
	       udfs->comedidev->minor);
	for (tmp = 0; tmp < SIZEOFDUXBUFFER; tmp++)
		printk(" %02x", udfs->dux_commands[tmp]);
	printk("\n");
#endif

	tmp = usb_bulk_msg(udfs->usbdev,
			   usb_sndbulkpipe(udfs->usbdev, CHANNELLISTEP),
			   udfs->dux_commands, SIZEOFDUXBUFFER, &nsent, 10000);
	if (tmp < 0)
		printk(KERN_ERR "comedi%d: could not transmit dux_commands to"
		       "the usb-device, err=%d\n", udfs->comedidev->minor, tmp);
	return tmp;
}

/*
 * Stops the data acquision.
 * It should be safe to call this function from any context.
 */
static int usbduxfastsub_unlink_InURBs(struct usbduxfastsub_s *udfs)
{
	int j = 0;
	int err = 0;

	if (udfs && udfs->urbIn) {
		udfs->ai_cmd_running = 0;
		/* waits until a running transfer is over */
		usb_kill_urb(udfs->urbIn);
		j = 0;
	}
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi: usbduxfast: unlinked InURB: res=%d\n", j);
#endif
	return err;
}

/*
 * This will stop a running acquisition operation.
 * Is called from within this driver from both the
 * interrupt context and from comedi.
 */
static int usbduxfast_ai_stop(struct usbduxfastsub_s *udfs, int do_unlink)
{
	int ret = 0;

	if (!udfs) {
		printk(KERN_ERR "comedi?: usbduxfast_ai_stop: udfs=NULL!\n");
		return -EFAULT;
	}
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi: usbduxfast_ai_stop\n");
#endif

	udfs->ai_cmd_running = 0;

	if (do_unlink)
		/* stop aquistion */
		ret = usbduxfastsub_unlink_InURBs(udfs);

	return ret;
}

/*
 * This will cancel a running acquisition operation.
 * This is called by comedi but never from inside the driver.
 */
static int usbduxfast_ai_cancel(struct comedi_device *dev,
				struct comedi_subdevice *s)
{
	struct usbduxfastsub_s *udfs;
	int ret;

	/* force unlink of all urbs */
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi: usbduxfast_ai_cancel\n");
#endif
	udfs = dev->private;
	if (!udfs) {
		printk(KERN_ERR "comedi: usbduxfast_ai_cancel: udfs=NULL\n");
		return -EFAULT;
	}
	down(&udfs->sem);
	if (!udfs->probed) {
		up(&udfs->sem);
		return -ENODEV;
	}
	/* unlink */
	ret = usbduxfast_ai_stop(udfs, 1);
	up(&udfs->sem);

	return ret;
}

/*
 * analogue IN
 * interrupt service routine
 */
static void usbduxfastsub_ai_Irq(struct urb *urb)
{
	int n, err;
	struct usbduxfastsub_s *udfs;
	struct comedi_device *this_comedidev;
	struct comedi_subdevice *s;
	uint16_t *p;

	/* sanity checks - is the urb there? */
	if (!urb) {
		printk(KERN_ERR "comedi_: usbduxfast_: ao int-handler called "
		       "with urb=NULL!\n");
		return;
	}
	/* the context variable points to the subdevice */
	this_comedidev = urb->context;
	if (!this_comedidev) {
		printk(KERN_ERR "comedi_: usbduxfast_: urb context is a NULL "
		       "pointer!\n");
		return;
	}
	/* the private structure of the subdevice is usbduxfastsub_s */
	udfs = this_comedidev->private;
	if (!udfs) {
		printk(KERN_ERR "comedi_: usbduxfast_: private of comedi "
		       "subdev is a NULL pointer!\n");
		return;
	}
	/* are we running a command? */
	if (unlikely(!udfs->ai_cmd_running)) {
		/*
		 * not running a command
		 * do not continue execution if no asynchronous command
		 * is running in particular not resubmit
		 */
		return;
	}

	if (unlikely(!udfs->attached)) {
		/* no comedi device there */
		return;
	}
	/* subdevice which is the AD converter */
	s = &this_comedidev->subdevices[SUBDEV_AD];

	/* first we test if something unusual has just happened */
	switch (urb->status) {
	case 0:
		break;

		/*
		 * happens after an unlink command or when the device
		 * is plugged out
		 */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -ECONNABORTED:
		/* tell this comedi */
		s->async->events |= COMEDI_CB_EOA;
		s->async->events |= COMEDI_CB_ERROR;
		comedi_event(udfs->comedidev, s);
		/* stop the transfer w/o unlink */
		usbduxfast_ai_stop(udfs, 0);
		return;

	default:
		printk("comedi%d: usbduxfast: non-zero urb status received in "
		       "ai intr context: %d\n",
		       udfs->comedidev->minor, urb->status);
		s->async->events |= COMEDI_CB_EOA;
		s->async->events |= COMEDI_CB_ERROR;
		comedi_event(udfs->comedidev, s);
		usbduxfast_ai_stop(udfs, 0);
		return;
	}

	p = urb->transfer_buffer;
	if (!udfs->ignore) {
		if (!udfs->ai_continous) {
			/* not continuous, fixed number of samples */
			n = urb->actual_length / sizeof(uint16_t);
			if (unlikely(udfs->ai_sample_count < n)) {
				/*
				 * we have send only a fraction of the bytes
				 * received
				 */
				cfc_write_array_to_buffer(s,
							  urb->transfer_buffer,
							  udfs->ai_sample_count
							  * sizeof(uint16_t));
				usbduxfast_ai_stop(udfs, 0);
				/* tell comedi that the acquistion is over */
				s->async->events |= COMEDI_CB_EOA;
				comedi_event(udfs->comedidev, s);
				return;
			}
			udfs->ai_sample_count -= n;
		}
		/* write the full buffer to comedi */
		err = cfc_write_array_to_buffer(s, urb->transfer_buffer,
						urb->actual_length);
		if (unlikely(err == 0)) {
			/* buffer overflow */
			usbduxfast_ai_stop(udfs, 0);
			return;
		}

		/* tell comedi that data is there */
		comedi_event(udfs->comedidev, s);

	} else {
		/* ignore this packet */
		udfs->ignore--;
	}

	/*
	 * command is still running
	 * resubmit urb for BULK transfer
	 */
	urb->dev = udfs->usbdev;
	urb->status = 0;
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		printk(KERN_ERR "comedi%d: usbduxfast: urb resubm failed: %d",
		       udfs->comedidev->minor, err);
		s->async->events |= COMEDI_CB_EOA;
		s->async->events |= COMEDI_CB_ERROR;
		comedi_event(udfs->comedidev, s);
		usbduxfast_ai_stop(udfs, 0);
	}
}

static int usbduxfastsub_start(struct usbduxfastsub_s *udfs)
{
	int ret;
	unsigned char local_transfer_buffer[16];

	/* 7f92 to zero */
	local_transfer_buffer[0] = 0;
	/* bRequest, "Firmware" */
	ret = usb_control_msg(udfs->usbdev, usb_sndctrlpipe(udfs->usbdev, 0),
			      USBDUXFASTSUB_FIRMWARE,
			      VENDOR_DIR_OUT,	  /* bmRequestType */
			      USBDUXFASTSUB_CPUCS,    /* Value */
			      0x0000,	/* Index */
			      /* address of the transfer buffer */
			      local_transfer_buffer,
			      1,      /* Length */
			      EZTIMEOUT);    /* Timeout */
	if (ret < 0) {
		printk("comedi_: usbduxfast_: control msg failed (start)\n");
		return ret;
	}

	return 0;
}

static int usbduxfastsub_stop(struct usbduxfastsub_s *udfs)
{
	int ret;
	unsigned char local_transfer_buffer[16];

	/* 7f92 to one */
	local_transfer_buffer[0] = 1;
	/* bRequest, "Firmware" */
	ret = usb_control_msg(udfs->usbdev, usb_sndctrlpipe(udfs->usbdev, 0),
			      USBDUXFASTSUB_FIRMWARE,
			      VENDOR_DIR_OUT,	/* bmRequestType */
			      USBDUXFASTSUB_CPUCS,	/* Value */
			      0x0000,	/* Index */
			      local_transfer_buffer, 1,	/* Length */
			      EZTIMEOUT);	/* Timeout */
	if (ret < 0) {
		printk(KERN_ERR "comedi_: usbduxfast: control msg failed "
		       "(stop)\n");
		return ret;
	}

	return 0;
}

static int usbduxfastsub_upload(struct usbduxfastsub_s *udfs,
				unsigned char *local_transfer_buffer,
				unsigned int startAddr, unsigned int len)
{
	int ret;

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi: usbduxfast: uploading %d bytes", len);
	printk(KERN_DEBUG " to addr %d, first byte=%d.\n",
	       startAddr, local_transfer_buffer[0]);
#endif
	/* brequest, firmware */
	ret = usb_control_msg(udfs->usbdev, usb_sndctrlpipe(udfs->usbdev, 0),
			      USBDUXFASTSUB_FIRMWARE,
			      VENDOR_DIR_OUT,	/* bmRequestType */
			      startAddr,	/* value */
			      0x0000,	 /* index */
			      /* our local safe buffer */
			      local_transfer_buffer,
			      len,	/* length */
			      EZTIMEOUT);      /* timeout */

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi_: usbduxfast: result=%d\n", ret);
#endif

	if (ret < 0) {
		printk(KERN_ERR "comedi_: usbduxfast: uppload failed\n");
		return ret;
	}

	return 0;
}

static int usbduxfastsub_submit_InURBs(struct usbduxfastsub_s *udfs)
{
	int ret;

	if (!udfs)
		return -EFAULT;

	usb_fill_bulk_urb(udfs->urbIn, udfs->usbdev,
			  usb_rcvbulkpipe(udfs->usbdev, BULKINEP),
			  udfs->transfer_buffer,
			  SIZEINBUF, usbduxfastsub_ai_Irq, udfs->comedidev);

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi%d: usbduxfast: submitting in-urb: "
	       "0x%p,0x%p\n", udfs->comedidev->minor, udfs->urbIn->context,
	       udfs->urbIn->dev);
#endif
	ret = usb_submit_urb(udfs->urbIn, GFP_ATOMIC);
	if (ret) {
		printk(KERN_ERR "comedi_: usbduxfast: ai: usb_submit_urb error"
		       " %d\n", ret);
		return ret;
	}
	return 0;
}

static int usbduxfast_ai_cmdtest(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_cmd *cmd)
{
	struct usbduxfastsub_s *udfs = dev->private;
	int err = 0;
	long int steps, tmp;
	int minSamplPer;

	if (!udfs->probed)
		return -ENODEV;

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi%d: usbduxfast_ai_cmdtest\n", dev->minor);
	printk(KERN_DEBUG "comedi%d: usbduxfast: convert_arg=%u "
	       "scan_begin_arg=%u\n",
	       dev->minor, cmd->convert_arg, cmd->scan_begin_arg);
#endif
	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src,
					TRIG_NOW | TRIG_EXT | TRIG_INT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	/* can't have external stop and start triggers at once */
	if (cmd->start_src == TRIG_EXT && cmd->stop_src == TRIG_EXT)
		err |= -EINVAL;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_src == TRIG_NOW && cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	if (!cmd->chanlist_len)
		err++;

	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	if (cmd->chanlist_len == 1)
		minSamplPer = 1;
	else
		minSamplPer = MIN_SAMPLING_PERIOD;

	if (cmd->convert_src == TRIG_TIMER) {
		steps = cmd->convert_arg * 30;
		if (steps < (minSamplPer * 1000))
			steps = minSamplPer * 1000;

		if (steps > (MAX_SAMPLING_PERIOD * 1000))
			steps = MAX_SAMPLING_PERIOD * 1000;

		/* calc arg again */
		tmp = steps / 30;
		if (cmd->convert_arg != tmp) {
			cmd->convert_arg = tmp;
			err++;
		}
	}

	if (cmd->scan_begin_src == TRIG_TIMER)
		err++;

	/* stop source */
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		if (!cmd->stop_arg) {
			cmd->stop_arg = 1;
			err++;
		}
		break;
	case TRIG_NONE:
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
		break;
		/*
		 * TRIG_EXT doesn't care since it doesn't trigger
		 * off a numbered channel
		 */
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	return 0;

}

static int usbduxfast_ai_inttrig(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 unsigned int trignum)
{
	int ret;
	struct usbduxfastsub_s *udfs = dev->private;

	if (!udfs)
		return -EFAULT;

	down(&udfs->sem);
	if (!udfs->probed) {
		up(&udfs->sem);
		return -ENODEV;
	}
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi%d: usbduxfast_ai_inttrig\n", dev->minor);
#endif

	if (trignum != 0) {
		printk(KERN_ERR "comedi%d: usbduxfast_ai_inttrig: invalid"
		       " trignum\n", dev->minor);
		up(&udfs->sem);
		return -EINVAL;
	}
	if (!udfs->ai_cmd_running) {
		udfs->ai_cmd_running = 1;
		ret = usbduxfastsub_submit_InURBs(udfs);
		if (ret < 0) {
			printk(KERN_ERR "comedi%d: usbduxfast_ai_inttrig: "
			       "urbSubmit: err=%d\n", dev->minor, ret);
			udfs->ai_cmd_running = 0;
			up(&udfs->sem);
			return ret;
		}
		s->async->inttrig = NULL;
	} else {
		printk(KERN_ERR "comedi%d: ai_inttrig but acqu is already"
		       " running\n", dev->minor);
	}
	up(&udfs->sem);
	return 1;
}

/*
 * offsets for the GPIF bytes
 * the first byte is the command byte
 */
#define LENBASE	(1+0x00)
#define OPBASE	(1+0x08)
#define OUTBASE	(1+0x10)
#define LOGBASE	(1+0x18)

static int usbduxfast_ai_cmd(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int chan, gain, rngmask = 0xff;
	int i, j, ret;
	struct usbduxfastsub_s *udfs;
	int result;
	long steps, steps_tmp;

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi%d: usbduxfast_ai_cmd\n", dev->minor);
#endif
	udfs = dev->private;
	if (!udfs)
		return -EFAULT;

	down(&udfs->sem);
	if (!udfs->probed) {
		up(&udfs->sem);
		return -ENODEV;
	}
	if (udfs->ai_cmd_running) {
		printk(KERN_ERR "comedi%d: ai_cmd not possible. Another ai_cmd"
		       " is running.\n", dev->minor);
		up(&udfs->sem);
		return -EBUSY;
	}
	/* set current channel of the running acquisition to zero */
	s->async->cur_chan = 0;

	/*
	 * ignore the first buffers from the device if there
	 * is an error condition
	 */
	udfs->ignore = PACKETS_TO_IGNORE;

	if (cmd->chanlist_len > 0) {
		gain = CR_RANGE(cmd->chanlist[0]);
		for (i = 0; i < cmd->chanlist_len; ++i) {
			chan = CR_CHAN(cmd->chanlist[i]);
			if (chan != i) {
				printk(KERN_ERR "comedi%d: cmd is accepting "
				       "only consecutive channels.\n",
				       dev->minor);
				up(&udfs->sem);
				return -EINVAL;
			}
			if ((gain != CR_RANGE(cmd->chanlist[i]))
			    && (cmd->chanlist_len > 3)) {
				printk(KERN_ERR "comedi%d: the gain must be"
				       " the same for all channels.\n",
				       dev->minor);
				up(&udfs->sem);
				return -EINVAL;
			}
			if (i >= NUMCHANNELS) {
				printk(KERN_ERR "comedi%d: channel list too"
				       " long\n", dev->minor);
				break;
			}
		}
	}
	steps = 0;
	if (cmd->scan_begin_src == TRIG_TIMER) {
		printk(KERN_ERR "comedi%d: usbduxfast: "
		       "scan_begin_src==TRIG_TIMER not valid.\n", dev->minor);
		up(&udfs->sem);
		return -EINVAL;
	}
	if (cmd->convert_src == TRIG_TIMER)
		steps = (cmd->convert_arg * 30) / 1000;

	if ((steps < MIN_SAMPLING_PERIOD) && (cmd->chanlist_len != 1)) {
		printk(KERN_ERR "comedi%d: usbduxfast: ai_cmd: steps=%ld, "
		       "scan_begin_arg=%d. Not properly tested by cmdtest?\n",
		       dev->minor, steps, cmd->scan_begin_arg);
		up(&udfs->sem);
		return -EINVAL;
	}
	if (steps > MAX_SAMPLING_PERIOD) {
		printk(KERN_ERR "comedi%d: usbduxfast: ai_cmd: sampling rate "
		       "too low.\n", dev->minor);
		up(&udfs->sem);
		return -EINVAL;
	}
	if ((cmd->start_src == TRIG_EXT) && (cmd->chanlist_len != 1)
	    && (cmd->chanlist_len != 16)) {
		printk(KERN_ERR "comedi%d: usbduxfast: ai_cmd: TRIG_EXT only"
		       " with 1 or 16 channels possible.\n", dev->minor);
		up(&udfs->sem);
		return -EINVAL;
	}
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi%d: usbduxfast: steps=%ld, convert_arg=%u\n",
	       dev->minor, steps, cmd->convert_arg);
#endif

	switch (cmd->chanlist_len) {
	case 1:
		/*
		 * one channel
		 */

		if (CR_RANGE(cmd->chanlist[0]) > 0)
			rngmask = 0xff - 0x04;
		else
			rngmask = 0xff;

		/*
		 * for external trigger: looping in this state until
		 * the RDY0 pin becomes zero
		 */

		/* we loop here until ready has been set */
		if (cmd->start_src == TRIG_EXT) {
			/* branch back to state 0 */
			udfs->dux_commands[LENBASE + 0] = 0x01;
			/* deceision state w/o data */
			udfs->dux_commands[OPBASE + 0] = 0x01;
			udfs->dux_commands[OUTBASE + 0] = 0xFF & rngmask;
			/* RDY0 = 0 */
			udfs->dux_commands[LOGBASE + 0] = 0x00;
		} else {	/* we just proceed to state 1 */
			udfs->dux_commands[LENBASE + 0] = 1;
			udfs->dux_commands[OPBASE + 0] = 0;
			udfs->dux_commands[OUTBASE + 0] = 0xFF & rngmask;
			udfs->dux_commands[LOGBASE + 0] = 0;
		}

		if (steps < MIN_SAMPLING_PERIOD) {
			/* for fast single channel aqu without mux */
			if (steps <= 1) {
				/*
				 * we just stay here at state 1 and rexecute
				 * the same state this gives us 30MHz sampling
				 * rate
				 */

				/* branch back to state 1 */
				udfs->dux_commands[LENBASE + 1] = 0x89;
				/* deceision state with data */
				udfs->dux_commands[OPBASE + 1] = 0x03;
				udfs->dux_commands[OUTBASE + 1] =
				    0xFF & rngmask;
				/* doesn't matter */
				udfs->dux_commands[LOGBASE + 1] = 0xFF;
			} else {
				/*
				 * we loop through two states: data and delay
				 * max rate is 15MHz
				 */
				udfs->dux_commands[LENBASE + 1] = steps - 1;
				/* data */
				udfs->dux_commands[OPBASE + 1] = 0x02;
				udfs->dux_commands[OUTBASE + 1] =
				    0xFF & rngmask;
				/* doesn't matter */
				udfs->dux_commands[LOGBASE + 1] = 0;
				/* branch back to state 1 */
				udfs->dux_commands[LENBASE + 2] = 0x09;
				/* deceision state w/o data */
				udfs->dux_commands[OPBASE + 2] = 0x01;
				udfs->dux_commands[OUTBASE + 2] =
				    0xFF & rngmask;
				/* doesn't matter */
				udfs->dux_commands[LOGBASE + 2] = 0xFF;
			}
		} else {
			/*
			 * we loop through 3 states: 2x delay and 1x data
			 * this gives a min sampling rate of 60kHz
			 */

			/* we have 1 state with duration 1 */
			steps = steps - 1;

			/* do the first part of the delay */
			udfs->dux_commands[LENBASE + 1] = steps / 2;
			udfs->dux_commands[OPBASE + 1] = 0;
			udfs->dux_commands[OUTBASE + 1] = 0xFF & rngmask;
			udfs->dux_commands[LOGBASE + 1] = 0;

			/* and the second part */
			udfs->dux_commands[LENBASE + 2] = steps - steps / 2;
			udfs->dux_commands[OPBASE + 2] = 0;
			udfs->dux_commands[OUTBASE + 2] = 0xFF & rngmask;
			udfs->dux_commands[LOGBASE + 2] = 0;

			/* get the data and branch back */

			/* branch back to state 1 */
			udfs->dux_commands[LENBASE + 3] = 0x09;
			/* deceision state w data */
			udfs->dux_commands[OPBASE + 3] = 0x03;
			udfs->dux_commands[OUTBASE + 3] = 0xFF & rngmask;
			/* doesn't matter */
			udfs->dux_commands[LOGBASE + 3] = 0xFF;
		}
		break;

	case 2:
		/*
		 * two channels
		 * commit data to the FIFO
		 */

		if (CR_RANGE(cmd->chanlist[0]) > 0)
			rngmask = 0xff - 0x04;
		else
			rngmask = 0xff;

		udfs->dux_commands[LENBASE + 0] = 1;
		/* data */
		udfs->dux_commands[OPBASE + 0] = 0x02;
		udfs->dux_commands[OUTBASE + 0] = 0xFF & rngmask;
		udfs->dux_commands[LOGBASE + 0] = 0;

		/* we have 1 state with duration 1: state 0 */
		steps_tmp = steps - 1;

		if (CR_RANGE(cmd->chanlist[1]) > 0)
			rngmask = 0xff - 0x04;
		else
			rngmask = 0xff;

		/* do the first part of the delay */
		udfs->dux_commands[LENBASE + 1] = steps_tmp / 2;
		udfs->dux_commands[OPBASE + 1] = 0;
		/* count */
		udfs->dux_commands[OUTBASE + 1] = 0xFE & rngmask;
		udfs->dux_commands[LOGBASE + 1] = 0;

		/* and the second part */
		udfs->dux_commands[LENBASE + 2] = steps_tmp - steps_tmp / 2;
		udfs->dux_commands[OPBASE + 2] = 0;
		udfs->dux_commands[OUTBASE + 2] = 0xFF & rngmask;
		udfs->dux_commands[LOGBASE + 2] = 0;

		udfs->dux_commands[LENBASE + 3] = 1;
		/* data */
		udfs->dux_commands[OPBASE + 3] = 0x02;
		udfs->dux_commands[OUTBASE + 3] = 0xFF & rngmask;
		udfs->dux_commands[LOGBASE + 3] = 0;

		/*
		 * we have 2 states with duration 1: step 6 and
		 * the IDLE state
		 */
		steps_tmp = steps - 2;

		if (CR_RANGE(cmd->chanlist[0]) > 0)
			rngmask = 0xff - 0x04;
		else
			rngmask = 0xff;

		/* do the first part of the delay */
		udfs->dux_commands[LENBASE + 4] = steps_tmp / 2;
		udfs->dux_commands[OPBASE + 4] = 0;
		/* reset */
		udfs->dux_commands[OUTBASE + 4] = (0xFF - 0x02) & rngmask;
		udfs->dux_commands[LOGBASE + 4] = 0;

		/* and the second part */
		udfs->dux_commands[LENBASE + 5] = steps_tmp - steps_tmp / 2;
		udfs->dux_commands[OPBASE + 5] = 0;
		udfs->dux_commands[OUTBASE + 5] = 0xFF & rngmask;
		udfs->dux_commands[LOGBASE + 5] = 0;

		udfs->dux_commands[LENBASE + 6] = 1;
		udfs->dux_commands[OPBASE + 6] = 0;
		udfs->dux_commands[OUTBASE + 6] = 0xFF & rngmask;
		udfs->dux_commands[LOGBASE + 6] = 0;
		break;

	case 3:
		/*
		 * three channels
		 */
		for (j = 0; j < 1; j++) {
			if (CR_RANGE(cmd->chanlist[j]) > 0)
				rngmask = 0xff - 0x04;
			else
				rngmask = 0xff;
			/*
			 * commit data to the FIFO and do the first part
			 * of the delay
			 */
			udfs->dux_commands[LENBASE + j * 2] = steps / 2;
			/* data */
			udfs->dux_commands[OPBASE + j * 2] = 0x02;
			/* no change */
			udfs->dux_commands[OUTBASE + j * 2] = 0xFF & rngmask;
			udfs->dux_commands[LOGBASE + j * 2] = 0;

			if (CR_RANGE(cmd->chanlist[j + 1]) > 0)
				rngmask = 0xff - 0x04;
			else
				rngmask = 0xff;

			/* do the second part of the delay */
			udfs->dux_commands[LENBASE + j * 2 + 1] =
			    steps - steps / 2;
			/* no data */
			udfs->dux_commands[OPBASE + j * 2 + 1] = 0;
			/* count */
			udfs->dux_commands[OUTBASE + j * 2 + 1] =
			    0xFE & rngmask;
			udfs->dux_commands[LOGBASE + j * 2 + 1] = 0;
		}

		/* 2 steps with duration 1: the idele step and step 6: */
		steps_tmp = steps - 2;

		/* commit data to the FIFO and do the first part of the delay */
		udfs->dux_commands[LENBASE + 4] = steps_tmp / 2;
		/* data */
		udfs->dux_commands[OPBASE + 4] = 0x02;
		udfs->dux_commands[OUTBASE + 4] = 0xFF & rngmask;
		udfs->dux_commands[LOGBASE + 4] = 0;

		if (CR_RANGE(cmd->chanlist[0]) > 0)
			rngmask = 0xff - 0x04;
		else
			rngmask = 0xff;

		/* do the second part of the delay */
		udfs->dux_commands[LENBASE + 5] = steps_tmp - steps_tmp / 2;
		/* no data */
		udfs->dux_commands[OPBASE + 5] = 0;
		/* reset */
		udfs->dux_commands[OUTBASE + 5] = (0xFF - 0x02) & rngmask;
		udfs->dux_commands[LOGBASE + 5] = 0;

		udfs->dux_commands[LENBASE + 6] = 1;
		udfs->dux_commands[OPBASE + 6] = 0;
		udfs->dux_commands[OUTBASE + 6] = 0xFF & rngmask;
		udfs->dux_commands[LOGBASE + 6] = 0;

	case 16:
		if (CR_RANGE(cmd->chanlist[0]) > 0)
			rngmask = 0xff - 0x04;
		else
			rngmask = 0xff;

		if (cmd->start_src == TRIG_EXT) {
			/*
			 * we loop here until ready has been set
			 */

			/* branch back to state 0 */
			udfs->dux_commands[LENBASE + 0] = 0x01;
			/* deceision state w/o data */
			udfs->dux_commands[OPBASE + 0] = 0x01;
			/* reset */
			udfs->dux_commands[OUTBASE + 0] =
			    (0xFF - 0x02) & rngmask;
			/* RDY0 = 0 */
			udfs->dux_commands[LOGBASE + 0] = 0x00;
		} else {
			/*
			 * we just proceed to state 1
			 */

			/* 30us reset pulse */
			udfs->dux_commands[LENBASE + 0] = 255;
			udfs->dux_commands[OPBASE + 0] = 0;
			/* reset */
			udfs->dux_commands[OUTBASE + 0] =
			    (0xFF - 0x02) & rngmask;
			udfs->dux_commands[LOGBASE + 0] = 0;
		}

		/* commit data to the FIFO */
		udfs->dux_commands[LENBASE + 1] = 1;
		/* data */
		udfs->dux_commands[OPBASE + 1] = 0x02;
		udfs->dux_commands[OUTBASE + 1] = 0xFF & rngmask;
		udfs->dux_commands[LOGBASE + 1] = 0;

		/* we have 2 states with duration 1 */
		steps = steps - 2;

		/* do the first part of the delay */
		udfs->dux_commands[LENBASE + 2] = steps / 2;
		udfs->dux_commands[OPBASE + 2] = 0;
		udfs->dux_commands[OUTBASE + 2] = 0xFE & rngmask;
		udfs->dux_commands[LOGBASE + 2] = 0;

		/* and the second part */
		udfs->dux_commands[LENBASE + 3] = steps - steps / 2;
		udfs->dux_commands[OPBASE + 3] = 0;
		udfs->dux_commands[OUTBASE + 3] = 0xFF & rngmask;
		udfs->dux_commands[LOGBASE + 3] = 0;

		/* branch back to state 1 */
		udfs->dux_commands[LENBASE + 4] = 0x09;
		/* deceision state w/o data */
		udfs->dux_commands[OPBASE + 4] = 0x01;
		udfs->dux_commands[OUTBASE + 4] = 0xFF & rngmask;
		/* doesn't matter */
		udfs->dux_commands[LOGBASE + 4] = 0xFF;

		break;

	default:
		printk(KERN_ERR "comedi %d: unsupported combination of "
		       "channels\n", dev->minor);
		up(&udfs->sem);
		return -EFAULT;
	}

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi %d: sending commands to the usb device\n",
	       dev->minor);
#endif
	/* 0 means that the AD commands are sent */
	result = send_dux_commands(udfs, SENDADCOMMANDS);
	if (result < 0) {
		printk(KERN_ERR "comedi%d: adc command could not be submitted."
		       "Aborting...\n", dev->minor);
		up(&udfs->sem);
		return result;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		udfs->ai_sample_count = cmd->stop_arg * cmd->scan_end_arg;
		if (udfs->ai_sample_count < 1) {
			printk(KERN_ERR "comedi%d: "
			       "(cmd->stop_arg)*(cmd->scan_end_arg)<1, "
			       "aborting.\n", dev->minor);
			up(&udfs->sem);
			return -EFAULT;
		}
		udfs->ai_continous = 0;
	} else {
		/* continous acquisition */
		udfs->ai_continous = 1;
		udfs->ai_sample_count = 0;
	}

	if ((cmd->start_src == TRIG_NOW) || (cmd->start_src == TRIG_EXT)) {
		/* enable this acquisition operation */
		udfs->ai_cmd_running = 1;
		ret = usbduxfastsub_submit_InURBs(udfs);
		if (ret < 0) {
			udfs->ai_cmd_running = 0;
			/* fixme: unlink here?? */
			up(&udfs->sem);
			return ret;
		}
		s->async->inttrig = NULL;
	} else {
		/*
		 * TRIG_INT
		 * don't enable the acquision operation
		 * wait for an internal signal
		 */
		s->async->inttrig = usbduxfast_ai_inttrig;
	}
	up(&udfs->sem);

	return 0;
}

/*
 * Mode 0 is used to get a single conversion on demand.
 */
static int usbduxfast_ai_insn_read(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	int i, j, n, actual_length;
	int chan, range, rngmask;
	int err;
	struct usbduxfastsub_s *udfs;

	udfs = dev->private;
	if (!udfs) {
		printk(KERN_ERR "comedi%d: ai_insn_read: no usb dev.\n",
		       dev->minor);
		return -ENODEV;
	}
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi%d: ai_insn_read, insn->n=%d, "
	       "insn->subdev=%d\n", dev->minor, insn->n, insn->subdev);
#endif
	down(&udfs->sem);
	if (!udfs->probed) {
		up(&udfs->sem);
		return -ENODEV;
	}
	if (udfs->ai_cmd_running) {
		printk(KERN_ERR "comedi%d: ai_insn_read not possible. Async "
		       "Command is running.\n", dev->minor);
		up(&udfs->sem);
		return -EBUSY;
	}
	/* sample one channel */
	chan = CR_CHAN(insn->chanspec);
	range = CR_RANGE(insn->chanspec);
	/* set command for the first channel */

	if (range > 0)
		rngmask = 0xff - 0x04;
	else
		rngmask = 0xff;

	/* commit data to the FIFO */
	udfs->dux_commands[LENBASE + 0] = 1;
	/* data */
	udfs->dux_commands[OPBASE + 0] = 0x02;
	udfs->dux_commands[OUTBASE + 0] = 0xFF & rngmask;
	udfs->dux_commands[LOGBASE + 0] = 0;

	/* do the first part of the delay */
	udfs->dux_commands[LENBASE + 1] = 12;
	udfs->dux_commands[OPBASE + 1] = 0;
	udfs->dux_commands[OUTBASE + 1] = 0xFE & rngmask;
	udfs->dux_commands[LOGBASE + 1] = 0;

	udfs->dux_commands[LENBASE + 2] = 1;
	udfs->dux_commands[OPBASE + 2] = 0;
	udfs->dux_commands[OUTBASE + 2] = 0xFE & rngmask;
	udfs->dux_commands[LOGBASE + 2] = 0;

	udfs->dux_commands[LENBASE + 3] = 1;
	udfs->dux_commands[OPBASE + 3] = 0;
	udfs->dux_commands[OUTBASE + 3] = 0xFE & rngmask;
	udfs->dux_commands[LOGBASE + 3] = 0;

	udfs->dux_commands[LENBASE + 4] = 1;
	udfs->dux_commands[OPBASE + 4] = 0;
	udfs->dux_commands[OUTBASE + 4] = 0xFE & rngmask;
	udfs->dux_commands[LOGBASE + 4] = 0;

	/* second part */
	udfs->dux_commands[LENBASE + 5] = 12;
	udfs->dux_commands[OPBASE + 5] = 0;
	udfs->dux_commands[OUTBASE + 5] = 0xFF & rngmask;
	udfs->dux_commands[LOGBASE + 5] = 0;

	udfs->dux_commands[LENBASE + 6] = 1;
	udfs->dux_commands[OPBASE + 6] = 0;
	udfs->dux_commands[OUTBASE + 6] = 0xFF & rngmask;
	udfs->dux_commands[LOGBASE + 0] = 0;

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi %d: sending commands to the usb device\n",
	       dev->minor);
#endif
	/* 0 means that the AD commands are sent */
	err = send_dux_commands(udfs, SENDADCOMMANDS);
	if (err < 0) {
		printk(KERN_ERR "comedi%d: adc command could not be submitted."
		       "Aborting...\n", dev->minor);
		up(&udfs->sem);
		return err;
	}
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi%d: usbduxfast: submitting in-urb: "
	       "0x%p,0x%p\n", udfs->comedidev->minor, udfs->urbIn->context,
	       udfs->urbIn->dev);
#endif
	for (i = 0; i < PACKETS_TO_IGNORE; i++) {
		err = usb_bulk_msg(udfs->usbdev,
				   usb_rcvbulkpipe(udfs->usbdev, BULKINEP),
				   udfs->transfer_buffer, SIZEINBUF,
				   &actual_length, 10000);
		if (err < 0) {
			printk(KERN_ERR "comedi%d: insn timeout. No data.\n",
			       dev->minor);
			up(&udfs->sem);
			return err;
		}
	}
	/* data points */
	for (i = 0; i < insn->n;) {
		err = usb_bulk_msg(udfs->usbdev,
				   usb_rcvbulkpipe(udfs->usbdev, BULKINEP),
				   udfs->transfer_buffer, SIZEINBUF,
				   &actual_length, 10000);
		if (err < 0) {
			printk(KERN_ERR "comedi%d: insn data error: %d\n",
			       dev->minor, err);
			up(&udfs->sem);
			return err;
		}
		n = actual_length / sizeof(uint16_t);
		if ((n % 16) != 0) {
			printk(KERN_ERR "comedi%d: insn data packet "
			       "corrupted.\n", dev->minor);
			up(&udfs->sem);
			return -EINVAL;
		}
		for (j = chan; (j < n) && (i < insn->n); j = j + 16) {
			data[i] = ((uint16_t *) (udfs->transfer_buffer))[j];
			i++;
		}
	}
	up(&udfs->sem);
	return i;
}

#define FIRMWARE_MAX_LEN 0x2000

static int firmwareUpload(struct usbduxfastsub_s *usbduxfastsub,
			  const u8 *firmwareBinary, int sizeFirmware)
{
	int ret;
	uint8_t *fwBuf;

	if (!firmwareBinary)
		return 0;

	if (sizeFirmware > FIRMWARE_MAX_LEN) {
		dev_err(&usbduxfastsub->interface->dev,
			"comedi_: usbduxfast firmware binary it too large for FX2.\n");
		return -ENOMEM;
	}

	/* we generate a local buffer for the firmware */
	fwBuf = kmemdup(firmwareBinary, sizeFirmware, GFP_KERNEL);
	if (!fwBuf) {
		dev_err(&usbduxfastsub->interface->dev,
			"comedi_: mem alloc for firmware failed\n");
		return -ENOMEM;
	}

	ret = usbduxfastsub_stop(usbduxfastsub);
	if (ret < 0) {
		dev_err(&usbduxfastsub->interface->dev,
			"comedi_: can not stop firmware\n");
		kfree(fwBuf);
		return ret;
	}

	ret = usbduxfastsub_upload(usbduxfastsub, fwBuf, 0, sizeFirmware);
	if (ret < 0) {
		dev_err(&usbduxfastsub->interface->dev,
			"comedi_: firmware upload failed\n");
		kfree(fwBuf);
		return ret;
	}
	ret = usbduxfastsub_start(usbduxfastsub);
	if (ret < 0) {
		dev_err(&usbduxfastsub->interface->dev,
			"comedi_: can not start firmware\n");
		kfree(fwBuf);
		return ret;
	}
	kfree(fwBuf);
	return 0;
}

static void tidy_up(struct usbduxfastsub_s *udfs)
{
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi_: usbduxfast: tiding up\n");
#endif

	if (!udfs)
		return;

	/* shows the usb subsystem that the driver is down */
	if (udfs->interface)
		usb_set_intfdata(udfs->interface, NULL);

	udfs->probed = 0;

	if (udfs->urbIn) {
		/* waits until a running transfer is over */
		usb_kill_urb(udfs->urbIn);

		kfree(udfs->transfer_buffer);
		udfs->transfer_buffer = NULL;

		usb_free_urb(udfs->urbIn);
		udfs->urbIn = NULL;
	}

	kfree(udfs->insnBuffer);
	udfs->insnBuffer = NULL;

	kfree(udfs->dux_commands);
	udfs->dux_commands = NULL;

	udfs->ai_cmd_running = 0;
}

static int usbduxfast_attach_common(struct comedi_device *dev,
				    struct usbduxfastsub_s *udfs)
{
	int ret;
	struct comedi_subdevice *s;

	down(&udfs->sem);
	/* pointer back to the corresponding comedi device */
	udfs->comedidev = dev;
	dev->board_name = "usbduxfast";
	ret = comedi_alloc_subdevices(dev, 1);
	if (ret) {
		up(&udfs->sem);
		return ret;
	}
	/* private structure is also simply the usb-structure */
	dev->private = udfs;
	/* the first subdevice is the A/D converter */
	s = &dev->subdevices[SUBDEV_AD];
	/*
	 * the URBs get the comedi subdevice which is responsible for reading
	 * this is the subdevice which reads data
	 */
	dev->read_subdev = s;
	/* the subdevice receives as private structure the usb-structure */
	s->private = NULL;
	/* analog input */
	s->type = COMEDI_SUBD_AI;
	/* readable and ref is to ground */
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_CMD_READ;
	/* 16 channels */
	s->n_chan = 16;
	/* length of the channellist */
	s->len_chanlist = 16;
	/* callback functions */
	s->insn_read = usbduxfast_ai_insn_read;
	s->do_cmdtest = usbduxfast_ai_cmdtest;
	s->do_cmd = usbduxfast_ai_cmd;
	s->cancel = usbduxfast_ai_cancel;
	/* max value from the A/D converter (12bit+1 bit for overflow) */
	s->maxdata = 0x1000;
	/* range table to convert to physical units */
	s->range_table = &range_usbduxfast_ai_range;
	/* finally decide that it's attached */
	udfs->attached = 1;
	up(&udfs->sem);
	dev_info(dev->class_dev, "successfully attached to usbduxfast.\n");
	return 0;
}

static int usbduxfast_attach_usb(struct comedi_device *dev,
				 struct usb_interface *uinterf)
{
	int ret;
	struct usbduxfastsub_s *udfs;

	dev->private = NULL;
	down(&start_stop_sem);
	udfs = usb_get_intfdata(uinterf);
	if (!udfs || !udfs->probed) {
		dev_err(dev->class_dev,
			"usbduxfast: error: attach_usb failed, not connected\n");
		ret = -ENODEV;
	} else if (udfs->attached) {
		dev_err(dev->class_dev,
		       "usbduxfast: error: attach_usb failed, already attached\n");
		ret = -ENODEV;
	} else
		ret = usbduxfast_attach_common(dev, udfs);
	up(&start_stop_sem);
	return ret;
}

static void usbduxfast_detach(struct comedi_device *dev)
{
	struct usbduxfastsub_s *usb = dev->private;

	if (usb) {
		down(&usb->sem);
		down(&start_stop_sem);
		dev->private = NULL;
		usb->attached = 0;
		usb->comedidev = NULL;
		up(&start_stop_sem);
		up(&usb->sem);
	}
}

static struct comedi_driver usbduxfast_driver = {
	.driver_name	= "usbduxfast",
	.module		= THIS_MODULE,
	.attach_usb	= usbduxfast_attach_usb,
	.detach		= usbduxfast_detach,
};

static void usbduxfast_firmware_request_complete_handler(const struct firmware
							 *fw, void *context)
{
	struct usbduxfastsub_s *usbduxfastsub_tmp = context;
	struct usb_interface *uinterf = usbduxfastsub_tmp->interface;
	int ret;

	if (fw == NULL)
		return;

	/*
	 * we need to upload the firmware here because fw will be
	 * freed once we've left this function
	 */
	ret = firmwareUpload(usbduxfastsub_tmp, fw->data, fw->size);

	if (ret) {
		dev_err(&uinterf->dev,
			"Could not upload firmware (err=%d)\n", ret);
		goto out;
	}

	comedi_usb_auto_config(uinterf, &usbduxfast_driver);
 out:
	release_firmware(fw);
}

static int usbduxfast_usb_probe(struct usb_interface *uinterf,
				const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(uinterf);
	int i;
	int index;
	int ret;

	if (udev->speed != USB_SPEED_HIGH) {
		printk(KERN_ERR "comedi_: usbduxfast_: This driver needs"
		       "USB 2.0 to operate. Aborting...\n");
		return -ENODEV;
	}
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi_: usbduxfast_: finding a free structure for "
	       "the usb-device\n");
#endif
	down(&start_stop_sem);
	/* look for a free place in the usbduxfast array */
	index = -1;
	for (i = 0; i < NUMUSBDUXFAST; i++) {
		if (!usbduxfastsub[i].probed) {
			index = i;
			break;
		}
	}

	/* no more space */
	if (index == -1) {
		printk(KERN_ERR "Too many usbduxfast-devices connected.\n");
		up(&start_stop_sem);
		return -EMFILE;
	}
#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi_: usbduxfast: usbduxfastsub[%d] is ready to "
	       "connect to comedi.\n", index);
#endif

	sema_init(&(usbduxfastsub[index].sem), 1);
	/* save a pointer to the usb device */
	usbduxfastsub[index].usbdev = udev;

	/* save the interface itself */
	usbduxfastsub[index].interface = uinterf;
	/* get the interface number from the interface */
	usbduxfastsub[index].ifnum = uinterf->altsetting->desc.bInterfaceNumber;
	/*
	 * hand the private data over to the usb subsystem
	 * will be needed for disconnect
	 */
	usb_set_intfdata(uinterf, &(usbduxfastsub[index]));

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi_: usbduxfast: ifnum=%d\n",
	       usbduxfastsub[index].ifnum);
#endif
	/* create space for the commands going to the usb device */
	usbduxfastsub[index].dux_commands = kmalloc(SIZEOFDUXBUFFER,
						    GFP_KERNEL);
	if (!usbduxfastsub[index].dux_commands) {
		printk(KERN_ERR "comedi_: usbduxfast: error alloc space for "
		       "dac commands\n");
		tidy_up(&(usbduxfastsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	/* create space of the instruction buffer */
	usbduxfastsub[index].insnBuffer = kmalloc(SIZEINSNBUF, GFP_KERNEL);
	if (!usbduxfastsub[index].insnBuffer) {
		printk(KERN_ERR "comedi_: usbduxfast: could not alloc space "
		       "for insnBuffer\n");
		tidy_up(&(usbduxfastsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	/* setting to alternate setting 1: enabling bulk ep */
	i = usb_set_interface(usbduxfastsub[index].usbdev,
			      usbduxfastsub[index].ifnum, 1);
	if (i < 0) {
		printk(KERN_ERR "comedi_: usbduxfast%d: could not switch to "
		       "alternate setting 1.\n", index);
		tidy_up(&(usbduxfastsub[index]));
		up(&start_stop_sem);
		return -ENODEV;
	}
	usbduxfastsub[index].urbIn = usb_alloc_urb(0, GFP_KERNEL);
	if (!usbduxfastsub[index].urbIn) {
		printk(KERN_ERR "comedi_: usbduxfast%d: Could not alloc."
		       "urb\n", index);
		tidy_up(&(usbduxfastsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	usbduxfastsub[index].transfer_buffer = kmalloc(SIZEINBUF, GFP_KERNEL);
	if (!usbduxfastsub[index].transfer_buffer) {
		printk(KERN_ERR "comedi_: usbduxfast%d: could not alloc. "
		       "transb.\n", index);
		tidy_up(&(usbduxfastsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	/* we've reached the bottom of the function */
	usbduxfastsub[index].probed = 1;
	up(&start_stop_sem);

	ret = request_firmware_nowait(THIS_MODULE,
				      FW_ACTION_HOTPLUG,
				      FIRMWARE,
				      &udev->dev,
				      GFP_KERNEL,
				      usbduxfastsub + index,
				      usbduxfast_firmware_request_complete_handler);

	if (ret) {
		dev_err(&udev->dev, "could not load firmware (err=%d)\n", ret);
		return ret;
	}

	printk(KERN_INFO "comedi_: usbduxfast%d has been successfully "
	       "initialized.\n", index);
	/* success */
	return 0;
}

static void usbduxfast_usb_disconnect(struct usb_interface *intf)
{
	struct usbduxfastsub_s *udfs = usb_get_intfdata(intf);
	struct usb_device *udev = interface_to_usbdev(intf);

	if (!udfs) {
		printk(KERN_ERR "comedi_: usbduxfast: disconnect called with "
		       "null pointer.\n");
		return;
	}
	if (udfs->usbdev != udev) {
		printk(KERN_ERR "comedi_: usbduxfast: BUG! called with wrong "
		       "ptr!!!\n");
		return;
	}

	comedi_usb_auto_unconfig(intf);

	down(&start_stop_sem);
	down(&udfs->sem);
	tidy_up(udfs);
	up(&udfs->sem);
	up(&start_stop_sem);

#ifdef CONFIG_COMEDI_DEBUG
	printk(KERN_DEBUG "comedi_: usbduxfast: disconnected from the usb\n");
#endif
}

static const struct usb_device_id usbduxfast_usb_table[] = {
	/* { USB_DEVICE(0x4b4, 0x8613) }, testing */
	{ USB_DEVICE(0x13d8, 0x0010) },	/* real ID */
	{ USB_DEVICE(0x13d8, 0x0011) },	/* real ID */
	{ }
};
MODULE_DEVICE_TABLE(usb, usbduxfast_usb_table);

static struct usb_driver usbduxfast_usb_driver = {
#ifdef COMEDI_HAVE_USB_DRIVER_OWNER
	.owner		= THIS_MODULE,
#endif
	.name		= "usbduxfast",
	.probe		= usbduxfast_usb_probe,
	.disconnect	= usbduxfast_usb_disconnect,
	.id_table	= usbduxfast_usb_table,
};
module_comedi_usb_driver(usbduxfast_driver, usbduxfast_usb_driver);

MODULE_AUTHOR("Bernd Porr, BerndPorr@f2s.com");
MODULE_DESCRIPTION("USB-DUXfast, BerndPorr@f2s.com");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE);
