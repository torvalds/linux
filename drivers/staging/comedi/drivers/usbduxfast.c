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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
struct usbduxfast_private {
	struct urb *urb;	/* BULK-transfer handling: urb */
	int8_t *transfer_buffer;
	short int ai_cmd_running;	/* asynchronous command is running */
	short int ai_continous;	/* continous acquisition */
	long int ai_sample_count;	/* number of samples to acquire */
	uint8_t *dux_commands;	/* commands */
	int ignore;		/* counter which ignores the first
				   buffers */
	struct semaphore sem;
};

/*
 * bulk transfers to usbduxfast
 */
#define SENDADCOMMANDS            0
#define SENDINITEP6               1

static int send_dux_commands(struct comedi_device *dev, int cmd_type)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = interface_to_usbdev(intf);
	struct usbduxfast_private *devpriv = dev->private;
	int tmp, nsent;

	devpriv->dux_commands[0] = cmd_type;

	tmp = usb_bulk_msg(usb, usb_sndbulkpipe(usb, CHANNELLISTEP),
			   devpriv->dux_commands,
			   SIZEOFDUXBUFFER, &nsent, 10000);
	if (tmp < 0)
		dev_err(dev->class_dev,
			"could not transmit dux_commands to the usb-device, err=%d\n",
			tmp);
	return tmp;
}

/*
 * Stops the data acquision.
 * It should be safe to call this function from any context.
 */
static int usbduxfastsub_unlink_InURBs(struct comedi_device *dev)
{
	struct usbduxfast_private *devpriv = dev->private;
	int j = 0;
	int err = 0;

	if (devpriv && devpriv->urb) {
		devpriv->ai_cmd_running = 0;
		/* waits until a running transfer is over */
		usb_kill_urb(devpriv->urb);
		j = 0;
	}
	return err;
}

/*
 * This will stop a running acquisition operation.
 * Is called from within this driver from both the
 * interrupt context and from comedi.
 */
static int usbduxfast_ai_stop(struct comedi_device *dev,
			      int do_unlink)
{
	struct usbduxfast_private *devpriv = dev->private;
	int ret = 0;

	if (!devpriv) {
		pr_err("%s: devpriv=NULL!\n", __func__);
		return -EFAULT;
	}

	devpriv->ai_cmd_running = 0;

	if (do_unlink)
		/* stop aquistion */
		ret = usbduxfastsub_unlink_InURBs(dev);

	return ret;
}

/*
 * This will cancel a running acquisition operation.
 * This is called by comedi but never from inside the driver.
 */
static int usbduxfast_ai_cancel(struct comedi_device *dev,
				struct comedi_subdevice *s)
{
	struct usbduxfast_private *devpriv = dev->private;
	int ret;

	/* force unlink of all urbs */
	if (!devpriv) {
		dev_err(dev->class_dev, "%s: devpriv=NULL\n", __func__);
		return -EFAULT;
	}
	down(&devpriv->sem);
	/* unlink */
	ret = usbduxfast_ai_stop(dev, 1);
	up(&devpriv->sem);

	return ret;
}

/*
 * analogue IN
 * interrupt service routine
 */
static void usbduxfast_ai_interrupt(struct urb *urb)
{
	struct comedi_device *dev = urb->context;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = interface_to_usbdev(intf);
	struct usbduxfast_private *devpriv = dev->private;
	int n, err;

	/* are we running a command? */
	if (unlikely(!devpriv->ai_cmd_running)) {
		/*
		 * not running a command
		 * do not continue execution if no asynchronous command
		 * is running in particular not resubmit
		 */
		return;
	}

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
		async->events |= COMEDI_CB_EOA;
		async->events |= COMEDI_CB_ERROR;
		comedi_event(dev, s);
		/* stop the transfer w/o unlink */
		usbduxfast_ai_stop(dev, 0);
		return;

	default:
		pr_err("non-zero urb status received in ai intr context: %d\n",
		       urb->status);
		async->events |= COMEDI_CB_EOA;
		async->events |= COMEDI_CB_ERROR;
		comedi_event(dev, s);
		usbduxfast_ai_stop(dev, 0);
		return;
	}

	if (!devpriv->ignore) {
		if (!devpriv->ai_continous) {
			/* not continuous, fixed number of samples */
			n = urb->actual_length / sizeof(uint16_t);
			if (unlikely(devpriv->ai_sample_count < n)) {
				/*
				 * we have send only a fraction of the bytes
				 * received
				 */
				cfc_write_array_to_buffer(s,
							  urb->transfer_buffer,
							  devpriv->ai_sample_count
							  * sizeof(uint16_t));
				usbduxfast_ai_stop(dev, 0);
				/* tell comedi that the acquistion is over */
				async->events |= COMEDI_CB_EOA;
				comedi_event(dev, s);
				return;
			}
			devpriv->ai_sample_count -= n;
		}
		/* write the full buffer to comedi */
		err = cfc_write_array_to_buffer(s, urb->transfer_buffer,
						urb->actual_length);
		if (unlikely(err == 0)) {
			/* buffer overflow */
			usbduxfast_ai_stop(dev, 0);
			return;
		}

		/* tell comedi that data is there */
		comedi_event(dev, s);
	} else {
		/* ignore this packet */
		devpriv->ignore--;
	}

	/*
	 * command is still running
	 * resubmit urb for BULK transfer
	 */
	urb->dev = usb;
	urb->status = 0;
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		dev_err(dev->class_dev,
			"urb resubm failed: %d", err);
		async->events |= COMEDI_CB_EOA;
		async->events |= COMEDI_CB_ERROR;
		comedi_event(dev, s);
		usbduxfast_ai_stop(dev, 0);
	}
}

static int usbduxfastsub_start(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = interface_to_usbdev(intf);
	unsigned char *local_transfer_buffer;
	int ret;

	local_transfer_buffer = kmalloc(1, GFP_KERNEL);
	if (!local_transfer_buffer)
		return -ENOMEM;

	/* 7f92 to zero */
	*local_transfer_buffer = 0;
	/* bRequest, "Firmware" */
	ret = usb_control_msg(usb, usb_sndctrlpipe(usb, 0),
			      USBDUXFASTSUB_FIRMWARE,
			      VENDOR_DIR_OUT,	  /* bmRequestType */
			      USBDUXFASTSUB_CPUCS,    /* Value */
			      0x0000,	/* Index */
			      /* address of the transfer buffer */
			      local_transfer_buffer,
			      1,      /* Length */
			      EZTIMEOUT);    /* Timeout */
	if (ret < 0)
		dev_err(dev->class_dev, "control msg failed (start)\n");

	kfree(local_transfer_buffer);
	return ret;
}

static int usbduxfastsub_stop(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = interface_to_usbdev(intf);
	unsigned char *local_transfer_buffer;
	int ret;

	local_transfer_buffer = kmalloc(1, GFP_KERNEL);
	if (!local_transfer_buffer)
		return -ENOMEM;

	/* 7f92 to one */
	*local_transfer_buffer = 1;
	/* bRequest, "Firmware" */
	ret = usb_control_msg(usb, usb_sndctrlpipe(usb, 0),
			      USBDUXFASTSUB_FIRMWARE,
			      VENDOR_DIR_OUT,	/* bmRequestType */
			      USBDUXFASTSUB_CPUCS,	/* Value */
			      0x0000,	/* Index */
			      local_transfer_buffer, 1,	/* Length */
			      EZTIMEOUT);	/* Timeout */
	if (ret < 0)
		dev_err(dev->class_dev, "control msg failed (stop)\n");

	kfree(local_transfer_buffer);
	return ret;
}

static int usbduxfastsub_upload(struct comedi_device *dev,
				unsigned char *local_transfer_buffer,
				unsigned int startAddr, unsigned int len)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = interface_to_usbdev(intf);
	int ret;

	/* brequest, firmware */
	ret = usb_control_msg(usb, usb_sndctrlpipe(usb, 0),
			      USBDUXFASTSUB_FIRMWARE,
			      VENDOR_DIR_OUT,	/* bmRequestType */
			      startAddr,	/* value */
			      0x0000,	 /* index */
			      /* our local safe buffer */
			      local_transfer_buffer,
			      len,	/* length */
			      EZTIMEOUT);      /* timeout */
	if (ret < 0) {
		dev_err(dev->class_dev, "uppload failed\n");
		return ret;
	}

	return 0;
}

static int usbduxfastsub_submit_InURBs(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = interface_to_usbdev(intf);
	struct usbduxfast_private *devpriv = dev->private;
	int ret;

	if (!devpriv)
		return -EFAULT;

	usb_fill_bulk_urb(devpriv->urb, usb, usb_rcvbulkpipe(usb, BULKINEP),
			  devpriv->transfer_buffer, SIZEINBUF,
			  usbduxfast_ai_interrupt, dev);

	ret = usb_submit_urb(devpriv->urb, GFP_ATOMIC);
	if (ret) {
		dev_err(dev->class_dev, "usb_submit_urb error %d\n", ret);
		return ret;
	}
	return 0;
}

static int usbduxfast_ai_cmdtest(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_cmd *cmd)
{
	int err = 0;
	long int steps, tmp;
	int minSamplPer;

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

	/* Step 3: check if arguments are trivially valid */

	if (cmd->start_src == TRIG_NOW)
		err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (!cmd->chanlist_len)
		err |= -EINVAL;

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

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
		err |= cfc_check_trigger_arg_is(&cmd->convert_arg, tmp);
	}

	if (cmd->scan_begin_src == TRIG_TIMER)
		err |= -EINVAL;

	/* stop source */
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
		break;
	case TRIG_NONE:
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
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
	struct usbduxfast_private *devpriv = dev->private;
	int ret;

	if (!devpriv)
		return -EFAULT;

	down(&devpriv->sem);

	if (trignum != 0) {
		dev_err(dev->class_dev, "invalid trignum\n");
		up(&devpriv->sem);
		return -EINVAL;
	}
	if (!devpriv->ai_cmd_running) {
		devpriv->ai_cmd_running = 1;
		ret = usbduxfastsub_submit_InURBs(dev);
		if (ret < 0) {
			dev_err(dev->class_dev, "urbSubmit: err=%d\n", ret);
			devpriv->ai_cmd_running = 0;
			up(&devpriv->sem);
			return ret;
		}
		s->async->inttrig = NULL;
	} else {
		dev_err(dev->class_dev, "ai is already running\n");
	}
	up(&devpriv->sem);
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
	struct usbduxfast_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int chan, gain, rngmask = 0xff;
	int i, j, ret;
	int result;
	long steps, steps_tmp;

	if (!devpriv)
		return -EFAULT;

	down(&devpriv->sem);
	if (devpriv->ai_cmd_running) {
		dev_err(dev->class_dev, "ai_cmd not possible\n");
		up(&devpriv->sem);
		return -EBUSY;
	}
	/* set current channel of the running acquisition to zero */
	s->async->cur_chan = 0;

	/*
	 * ignore the first buffers from the device if there
	 * is an error condition
	 */
	devpriv->ignore = PACKETS_TO_IGNORE;

	if (cmd->chanlist_len > 0) {
		gain = CR_RANGE(cmd->chanlist[0]);
		for (i = 0; i < cmd->chanlist_len; ++i) {
			chan = CR_CHAN(cmd->chanlist[i]);
			if (chan != i) {
				dev_err(dev->class_dev,
					"channels are not consecutive\n");
				up(&devpriv->sem);
				return -EINVAL;
			}
			if ((gain != CR_RANGE(cmd->chanlist[i]))
			    && (cmd->chanlist_len > 3)) {
				dev_err(dev->class_dev,
					"gain must be the same for all channels\n");
				up(&devpriv->sem);
				return -EINVAL;
			}
			if (i >= NUMCHANNELS) {
				dev_err(dev->class_dev, "chanlist too long\n");
				break;
			}
		}
	}
	steps = 0;
	if (cmd->scan_begin_src == TRIG_TIMER) {
		dev_err(dev->class_dev,
			"scan_begin_src==TRIG_TIMER not valid\n");
		up(&devpriv->sem);
		return -EINVAL;
	}
	if (cmd->convert_src == TRIG_TIMER)
		steps = (cmd->convert_arg * 30) / 1000;

	if ((steps < MIN_SAMPLING_PERIOD) && (cmd->chanlist_len != 1)) {
		dev_err(dev->class_dev,
			"steps=%ld, scan_begin_arg=%d. Not properly tested by cmdtest?\n",
			steps, cmd->scan_begin_arg);
		up(&devpriv->sem);
		return -EINVAL;
	}
	if (steps > MAX_SAMPLING_PERIOD) {
		dev_err(dev->class_dev, "sampling rate too low\n");
		up(&devpriv->sem);
		return -EINVAL;
	}
	if ((cmd->start_src == TRIG_EXT) && (cmd->chanlist_len != 1)
	    && (cmd->chanlist_len != 16)) {
		dev_err(dev->class_dev,
			"TRIG_EXT only with 1 or 16 channels possible\n");
		up(&devpriv->sem);
		return -EINVAL;
	}

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
			devpriv->dux_commands[LENBASE + 0] = 0x01;
			/* deceision state w/o data */
			devpriv->dux_commands[OPBASE + 0] = 0x01;
			devpriv->dux_commands[OUTBASE + 0] = 0xFF & rngmask;
			/* RDY0 = 0 */
			devpriv->dux_commands[LOGBASE + 0] = 0x00;
		} else {	/* we just proceed to state 1 */
			devpriv->dux_commands[LENBASE + 0] = 1;
			devpriv->dux_commands[OPBASE + 0] = 0;
			devpriv->dux_commands[OUTBASE + 0] = 0xFF & rngmask;
			devpriv->dux_commands[LOGBASE + 0] = 0;
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
				devpriv->dux_commands[LENBASE + 1] = 0x89;
				/* deceision state with data */
				devpriv->dux_commands[OPBASE + 1] = 0x03;
				devpriv->dux_commands[OUTBASE + 1] =
				    0xFF & rngmask;
				/* doesn't matter */
				devpriv->dux_commands[LOGBASE + 1] = 0xFF;
			} else {
				/*
				 * we loop through two states: data and delay
				 * max rate is 15MHz
				 */
				devpriv->dux_commands[LENBASE + 1] = steps - 1;
				/* data */
				devpriv->dux_commands[OPBASE + 1] = 0x02;
				devpriv->dux_commands[OUTBASE + 1] =
				    0xFF & rngmask;
				/* doesn't matter */
				devpriv->dux_commands[LOGBASE + 1] = 0;
				/* branch back to state 1 */
				devpriv->dux_commands[LENBASE + 2] = 0x09;
				/* deceision state w/o data */
				devpriv->dux_commands[OPBASE + 2] = 0x01;
				devpriv->dux_commands[OUTBASE + 2] =
				    0xFF & rngmask;
				/* doesn't matter */
				devpriv->dux_commands[LOGBASE + 2] = 0xFF;
			}
		} else {
			/*
			 * we loop through 3 states: 2x delay and 1x data
			 * this gives a min sampling rate of 60kHz
			 */

			/* we have 1 state with duration 1 */
			steps = steps - 1;

			/* do the first part of the delay */
			devpriv->dux_commands[LENBASE + 1] = steps / 2;
			devpriv->dux_commands[OPBASE + 1] = 0;
			devpriv->dux_commands[OUTBASE + 1] = 0xFF & rngmask;
			devpriv->dux_commands[LOGBASE + 1] = 0;

			/* and the second part */
			devpriv->dux_commands[LENBASE + 2] = steps - steps / 2;
			devpriv->dux_commands[OPBASE + 2] = 0;
			devpriv->dux_commands[OUTBASE + 2] = 0xFF & rngmask;
			devpriv->dux_commands[LOGBASE + 2] = 0;

			/* get the data and branch back */

			/* branch back to state 1 */
			devpriv->dux_commands[LENBASE + 3] = 0x09;
			/* deceision state w data */
			devpriv->dux_commands[OPBASE + 3] = 0x03;
			devpriv->dux_commands[OUTBASE + 3] = 0xFF & rngmask;
			/* doesn't matter */
			devpriv->dux_commands[LOGBASE + 3] = 0xFF;
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

		devpriv->dux_commands[LENBASE + 0] = 1;
		/* data */
		devpriv->dux_commands[OPBASE + 0] = 0x02;
		devpriv->dux_commands[OUTBASE + 0] = 0xFF & rngmask;
		devpriv->dux_commands[LOGBASE + 0] = 0;

		/* we have 1 state with duration 1: state 0 */
		steps_tmp = steps - 1;

		if (CR_RANGE(cmd->chanlist[1]) > 0)
			rngmask = 0xff - 0x04;
		else
			rngmask = 0xff;

		/* do the first part of the delay */
		devpriv->dux_commands[LENBASE + 1] = steps_tmp / 2;
		devpriv->dux_commands[OPBASE + 1] = 0;
		/* count */
		devpriv->dux_commands[OUTBASE + 1] = 0xFE & rngmask;
		devpriv->dux_commands[LOGBASE + 1] = 0;

		/* and the second part */
		devpriv->dux_commands[LENBASE + 2] = steps_tmp - steps_tmp / 2;
		devpriv->dux_commands[OPBASE + 2] = 0;
		devpriv->dux_commands[OUTBASE + 2] = 0xFF & rngmask;
		devpriv->dux_commands[LOGBASE + 2] = 0;

		devpriv->dux_commands[LENBASE + 3] = 1;
		/* data */
		devpriv->dux_commands[OPBASE + 3] = 0x02;
		devpriv->dux_commands[OUTBASE + 3] = 0xFF & rngmask;
		devpriv->dux_commands[LOGBASE + 3] = 0;

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
		devpriv->dux_commands[LENBASE + 4] = steps_tmp / 2;
		devpriv->dux_commands[OPBASE + 4] = 0;
		/* reset */
		devpriv->dux_commands[OUTBASE + 4] = (0xFF - 0x02) & rngmask;
		devpriv->dux_commands[LOGBASE + 4] = 0;

		/* and the second part */
		devpriv->dux_commands[LENBASE + 5] = steps_tmp - steps_tmp / 2;
		devpriv->dux_commands[OPBASE + 5] = 0;
		devpriv->dux_commands[OUTBASE + 5] = 0xFF & rngmask;
		devpriv->dux_commands[LOGBASE + 5] = 0;

		devpriv->dux_commands[LENBASE + 6] = 1;
		devpriv->dux_commands[OPBASE + 6] = 0;
		devpriv->dux_commands[OUTBASE + 6] = 0xFF & rngmask;
		devpriv->dux_commands[LOGBASE + 6] = 0;
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
			devpriv->dux_commands[LENBASE + j * 2] = steps / 2;
			/* data */
			devpriv->dux_commands[OPBASE + j * 2] = 0x02;
			/* no change */
			devpriv->dux_commands[OUTBASE + j * 2] = 0xFF & rngmask;
			devpriv->dux_commands[LOGBASE + j * 2] = 0;

			if (CR_RANGE(cmd->chanlist[j + 1]) > 0)
				rngmask = 0xff - 0x04;
			else
				rngmask = 0xff;

			/* do the second part of the delay */
			devpriv->dux_commands[LENBASE + j * 2 + 1] =
			    steps - steps / 2;
			/* no data */
			devpriv->dux_commands[OPBASE + j * 2 + 1] = 0;
			/* count */
			devpriv->dux_commands[OUTBASE + j * 2 + 1] =
			    0xFE & rngmask;
			devpriv->dux_commands[LOGBASE + j * 2 + 1] = 0;
		}

		/* 2 steps with duration 1: the idele step and step 6: */
		steps_tmp = steps - 2;

		/* commit data to the FIFO and do the first part of the delay */
		devpriv->dux_commands[LENBASE + 4] = steps_tmp / 2;
		/* data */
		devpriv->dux_commands[OPBASE + 4] = 0x02;
		devpriv->dux_commands[OUTBASE + 4] = 0xFF & rngmask;
		devpriv->dux_commands[LOGBASE + 4] = 0;

		if (CR_RANGE(cmd->chanlist[0]) > 0)
			rngmask = 0xff - 0x04;
		else
			rngmask = 0xff;

		/* do the second part of the delay */
		devpriv->dux_commands[LENBASE + 5] = steps_tmp - steps_tmp / 2;
		/* no data */
		devpriv->dux_commands[OPBASE + 5] = 0;
		/* reset */
		devpriv->dux_commands[OUTBASE + 5] = (0xFF - 0x02) & rngmask;
		devpriv->dux_commands[LOGBASE + 5] = 0;

		devpriv->dux_commands[LENBASE + 6] = 1;
		devpriv->dux_commands[OPBASE + 6] = 0;
		devpriv->dux_commands[OUTBASE + 6] = 0xFF & rngmask;
		devpriv->dux_commands[LOGBASE + 6] = 0;

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
			devpriv->dux_commands[LENBASE + 0] = 0x01;
			/* deceision state w/o data */
			devpriv->dux_commands[OPBASE + 0] = 0x01;
			/* reset */
			devpriv->dux_commands[OUTBASE + 0] =
			    (0xFF - 0x02) & rngmask;
			/* RDY0 = 0 */
			devpriv->dux_commands[LOGBASE + 0] = 0x00;
		} else {
			/*
			 * we just proceed to state 1
			 */

			/* 30us reset pulse */
			devpriv->dux_commands[LENBASE + 0] = 255;
			devpriv->dux_commands[OPBASE + 0] = 0;
			/* reset */
			devpriv->dux_commands[OUTBASE + 0] =
			    (0xFF - 0x02) & rngmask;
			devpriv->dux_commands[LOGBASE + 0] = 0;
		}

		/* commit data to the FIFO */
		devpriv->dux_commands[LENBASE + 1] = 1;
		/* data */
		devpriv->dux_commands[OPBASE + 1] = 0x02;
		devpriv->dux_commands[OUTBASE + 1] = 0xFF & rngmask;
		devpriv->dux_commands[LOGBASE + 1] = 0;

		/* we have 2 states with duration 1 */
		steps = steps - 2;

		/* do the first part of the delay */
		devpriv->dux_commands[LENBASE + 2] = steps / 2;
		devpriv->dux_commands[OPBASE + 2] = 0;
		devpriv->dux_commands[OUTBASE + 2] = 0xFE & rngmask;
		devpriv->dux_commands[LOGBASE + 2] = 0;

		/* and the second part */
		devpriv->dux_commands[LENBASE + 3] = steps - steps / 2;
		devpriv->dux_commands[OPBASE + 3] = 0;
		devpriv->dux_commands[OUTBASE + 3] = 0xFF & rngmask;
		devpriv->dux_commands[LOGBASE + 3] = 0;

		/* branch back to state 1 */
		devpriv->dux_commands[LENBASE + 4] = 0x09;
		/* deceision state w/o data */
		devpriv->dux_commands[OPBASE + 4] = 0x01;
		devpriv->dux_commands[OUTBASE + 4] = 0xFF & rngmask;
		/* doesn't matter */
		devpriv->dux_commands[LOGBASE + 4] = 0xFF;

		break;

	default:
		dev_err(dev->class_dev, "unsupported combination of channels\n");
		up(&devpriv->sem);
		return -EFAULT;
	}

	/* 0 means that the AD commands are sent */
	result = send_dux_commands(dev, SENDADCOMMANDS);
	if (result < 0) {
		dev_err(dev->class_dev,
			"adc command could not be submitted, aborting\n");
		up(&devpriv->sem);
		return result;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		devpriv->ai_sample_count = cmd->stop_arg * cmd->scan_end_arg;
		if (devpriv->ai_sample_count < 1) {
			dev_err(dev->class_dev,
				"(cmd->stop_arg)*(cmd->scan_end_arg)<1, aborting\n");
			up(&devpriv->sem);
			return -EFAULT;
		}
		devpriv->ai_continous = 0;
	} else {
		/* continous acquisition */
		devpriv->ai_continous = 1;
		devpriv->ai_sample_count = 0;
	}

	if ((cmd->start_src == TRIG_NOW) || (cmd->start_src == TRIG_EXT)) {
		/* enable this acquisition operation */
		devpriv->ai_cmd_running = 1;
		ret = usbduxfastsub_submit_InURBs(dev);
		if (ret < 0) {
			devpriv->ai_cmd_running = 0;
			/* fixme: unlink here?? */
			up(&devpriv->sem);
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
	up(&devpriv->sem);

	return 0;
}

/*
 * Mode 0 is used to get a single conversion on demand.
 */
static int usbduxfast_ai_insn_read(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = interface_to_usbdev(intf);
	struct usbduxfast_private *devpriv = dev->private;
	int i, j, n, actual_length;
	int chan, range, rngmask;
	int err;

	if (!devpriv) {
		dev_err(dev->class_dev, "no usb dev\n");
		return -ENODEV;
	}

	down(&devpriv->sem);
	if (devpriv->ai_cmd_running) {
		dev_err(dev->class_dev,
			"ai_insn_read not possible, async cmd is running\n");
		up(&devpriv->sem);
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
	devpriv->dux_commands[LENBASE + 0] = 1;
	/* data */
	devpriv->dux_commands[OPBASE + 0] = 0x02;
	devpriv->dux_commands[OUTBASE + 0] = 0xFF & rngmask;
	devpriv->dux_commands[LOGBASE + 0] = 0;

	/* do the first part of the delay */
	devpriv->dux_commands[LENBASE + 1] = 12;
	devpriv->dux_commands[OPBASE + 1] = 0;
	devpriv->dux_commands[OUTBASE + 1] = 0xFE & rngmask;
	devpriv->dux_commands[LOGBASE + 1] = 0;

	devpriv->dux_commands[LENBASE + 2] = 1;
	devpriv->dux_commands[OPBASE + 2] = 0;
	devpriv->dux_commands[OUTBASE + 2] = 0xFE & rngmask;
	devpriv->dux_commands[LOGBASE + 2] = 0;

	devpriv->dux_commands[LENBASE + 3] = 1;
	devpriv->dux_commands[OPBASE + 3] = 0;
	devpriv->dux_commands[OUTBASE + 3] = 0xFE & rngmask;
	devpriv->dux_commands[LOGBASE + 3] = 0;

	devpriv->dux_commands[LENBASE + 4] = 1;
	devpriv->dux_commands[OPBASE + 4] = 0;
	devpriv->dux_commands[OUTBASE + 4] = 0xFE & rngmask;
	devpriv->dux_commands[LOGBASE + 4] = 0;

	/* second part */
	devpriv->dux_commands[LENBASE + 5] = 12;
	devpriv->dux_commands[OPBASE + 5] = 0;
	devpriv->dux_commands[OUTBASE + 5] = 0xFF & rngmask;
	devpriv->dux_commands[LOGBASE + 5] = 0;

	devpriv->dux_commands[LENBASE + 6] = 1;
	devpriv->dux_commands[OPBASE + 6] = 0;
	devpriv->dux_commands[OUTBASE + 6] = 0xFF & rngmask;
	devpriv->dux_commands[LOGBASE + 0] = 0;

	/* 0 means that the AD commands are sent */
	err = send_dux_commands(dev, SENDADCOMMANDS);
	if (err < 0) {
		dev_err(dev->class_dev,
			"adc command could not be submitted, aborting\n");
		up(&devpriv->sem);
		return err;
	}

	for (i = 0; i < PACKETS_TO_IGNORE; i++) {
		err = usb_bulk_msg(usb, usb_rcvbulkpipe(usb, BULKINEP),
				   devpriv->transfer_buffer, SIZEINBUF,
				   &actual_length, 10000);
		if (err < 0) {
			dev_err(dev->class_dev, "insn timeout, no data\n");
			up(&devpriv->sem);
			return err;
		}
	}
	/* data points */
	for (i = 0; i < insn->n;) {
		err = usb_bulk_msg(usb, usb_rcvbulkpipe(usb, BULKINEP),
				   devpriv->transfer_buffer, SIZEINBUF,
				   &actual_length, 10000);
		if (err < 0) {
			dev_err(dev->class_dev, "insn data error: %d\n", err);
			up(&devpriv->sem);
			return err;
		}
		n = actual_length / sizeof(uint16_t);
		if ((n % 16) != 0) {
			dev_err(dev->class_dev, "insn data packet corrupted\n");
			up(&devpriv->sem);
			return -EINVAL;
		}
		for (j = chan; (j < n) && (i < insn->n); j = j + 16) {
			data[i] = ((uint16_t *) (devpriv->transfer_buffer))[j];
			i++;
		}
	}
	up(&devpriv->sem);
	return i;
}

#define FIRMWARE_MAX_LEN 0x2000

static int firmwareUpload(struct comedi_device *dev,
			  const u8 *firmwareBinary, int sizeFirmware)
{
	uint8_t *fwBuf;
	int ret;

	if (!firmwareBinary)
		return 0;

	if (sizeFirmware > FIRMWARE_MAX_LEN) {
		dev_err(dev->class_dev, "firmware binary too large for FX2\n");
		return -ENOMEM;
	}

	/* we generate a local buffer for the firmware */
	fwBuf = kmemdup(firmwareBinary, sizeFirmware, GFP_KERNEL);
	if (!fwBuf) {
		dev_err(dev->class_dev, "mem alloc for firmware failed\n");
		return -ENOMEM;
	}

	ret = usbduxfastsub_stop(dev);
	if (ret < 0) {
		dev_err(dev->class_dev, "can not stop firmware\n");
		kfree(fwBuf);
		return ret;
	}

	ret = usbduxfastsub_upload(dev, fwBuf, 0, sizeFirmware);
	if (ret < 0) {
		dev_err(dev->class_dev, "firmware upload failed\n");
		kfree(fwBuf);
		return ret;
	}
	ret = usbduxfastsub_start(dev);
	if (ret < 0) {
		dev_err(dev->class_dev, "can not start firmware\n");
		kfree(fwBuf);
		return ret;
	}
	kfree(fwBuf);
	return 0;
}

static int usbduxfast_attach_common(struct comedi_device *dev)
{
	struct usbduxfast_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	int ret;

	down(&devpriv->sem);

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret) {
		up(&devpriv->sem);
		return ret;
	}

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_CMD_READ;
	s->n_chan	= 16;
	s->len_chanlist	= 16;
	s->insn_read	= usbduxfast_ai_insn_read;
	s->do_cmdtest	= usbduxfast_ai_cmdtest;
	s->do_cmd	= usbduxfast_ai_cmd;
	s->cancel	= usbduxfast_ai_cancel;
	s->maxdata	= 0x1000;
	s->range_table	= &range_usbduxfast_ai_range;

	up(&devpriv->sem);

	return 0;
}

static int usbduxfast_request_firmware(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = interface_to_usbdev(intf);
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, FIRMWARE, &usb->dev);
	if (ret)
		return ret;

	ret = firmwareUpload(dev, fw->data, fw->size);
	release_firmware(fw);

	return ret;
}

static int usbduxfast_auto_attach(struct comedi_device *dev,
				  unsigned long context_unused)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = interface_to_usbdev(intf);
	struct usbduxfast_private *devpriv;
	int ret;

	if (usb->speed != USB_SPEED_HIGH) {
		dev_err(dev->class_dev,
			"This driver needs USB 2.0 to operate. Aborting...\n");
		return -ENODEV;
	}

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	sema_init(&devpriv->sem, 1);
	usb_set_intfdata(intf, devpriv);

	devpriv->dux_commands = kmalloc(SIZEOFDUXBUFFER, GFP_KERNEL);
	if (!devpriv->dux_commands)
		return -ENOMEM;

	ret = usb_set_interface(usb,
				intf->altsetting->desc.bInterfaceNumber, 1);
	if (ret < 0) {
		dev_err(dev->class_dev,
			"could not switch to alternate setting 1\n");
		return -ENODEV;
	}

	devpriv->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!devpriv->urb) {
		dev_err(dev->class_dev, "Could not alloc. urb\n");
		return -ENOMEM;
	}

	devpriv->transfer_buffer = kmalloc(SIZEINBUF, GFP_KERNEL);
	if (!devpriv->transfer_buffer)
		return -ENOMEM;

	/*
	 * Request, and upload, the firmware so we can
	 * complete the comedi_driver (*auto_attach).
	 */
	ret = usbduxfast_request_firmware(dev);
	if (ret) {
		dev_err(dev->class_dev, "could not load firmware (err=%d)\n",
			ret);
		return ret;
	}

	return usbduxfast_attach_common(dev);
}

static void usbduxfast_detach(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usbduxfast_private *devpriv = dev->private;

	if (!devpriv)
		return;

	down(&devpriv->sem);

	usb_set_intfdata(intf, NULL);

	if (devpriv->urb) {
		/* waits until a running transfer is over */
		usb_kill_urb(devpriv->urb);

		kfree(devpriv->transfer_buffer);
		devpriv->transfer_buffer = NULL;

		usb_free_urb(devpriv->urb);
		devpriv->urb = NULL;
	}

	kfree(devpriv->dux_commands);
	devpriv->dux_commands = NULL;

	devpriv->ai_cmd_running = 0;

	up(&devpriv->sem);
}

static struct comedi_driver usbduxfast_driver = {
	.driver_name	= "usbduxfast",
	.module		= THIS_MODULE,
	.auto_attach	= usbduxfast_auto_attach,
	.detach		= usbduxfast_detach,
};

static int usbduxfast_usb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	return comedi_usb_auto_config(intf, &usbduxfast_driver, 0);
}

static const struct usb_device_id usbduxfast_usb_table[] = {
	/* { USB_DEVICE(0x4b4, 0x8613) }, testing */
	{ USB_DEVICE(0x13d8, 0x0010) },	/* real ID */
	{ USB_DEVICE(0x13d8, 0x0011) },	/* real ID */
	{ }
};
MODULE_DEVICE_TABLE(usb, usbduxfast_usb_table);

static struct usb_driver usbduxfast_usb_driver = {
	.name		= "usbduxfast",
	.probe		= usbduxfast_usb_probe,
	.disconnect	= comedi_usb_auto_unconfig,
	.id_table	= usbduxfast_usb_table,
};
module_comedi_usb_driver(usbduxfast_driver, usbduxfast_usb_driver);

MODULE_AUTHOR("Bernd Porr, BerndPorr@f2s.com");
MODULE_DESCRIPTION("USB-DUXfast, BerndPorr@f2s.com");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE);
