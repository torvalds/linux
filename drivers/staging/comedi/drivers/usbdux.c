/*
   comedi/drivers/usbdux.c
   Copyright (C) 2003-2007 Bernd Porr, Bernd.Porr@f2s.com

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 */
/*
Driver: usbdux
Description: University of Stirling USB DAQ & INCITE Technology Limited
Devices: [ITL] USB-DUX (usbdux.o)
Author: Bernd Porr <BerndPorr@f2s.com>
Updated: 8 Dec 2008
Status: Stable
Configuration options:
  You have to upload firmware with the -i option. The
  firmware is usually installed under /usr/share/usb or
  /usr/local/share/usb or /lib/firmware.

Connection scheme for the counter at the digital port:
  0=/CLK0, 1=UP/DOWN0, 2=RESET0, 4=/CLK1, 5=UP/DOWN1, 6=RESET1.
  The sampling rate of the counter is approximately 500Hz.

Please note that under USB2.0 the length of the channel list determines
the max sampling rate. If you sample only one channel you get 8kHz
sampling rate. If you sample two channels you get 4kHz and so on.
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
 * 0.94: D/A output should work now with any channel list combinations
 * 0.95: .owner commented out for kernel vers below 2.4.19
 *       sanity checks in ai/ao_cmd
 * 0.96: trying to get it working with 2.6, moved all memory alloc to comedi's
 *       attach final USB IDs
 *       moved memory allocation completely to the corresponding comedi
 *       functions firmware upload is by fxload and no longer by comedi (due to
 *       enumeration)
 * 0.97: USB IDs received, adjusted table
 * 0.98: SMP, locking, memory alloc: moved all usb memory alloc
 *       to the usb subsystem and moved all comedi related memory
 *       alloc to comedi.
 *       | kernel | registration | usbdux-usb | usbdux-comedi | comedi |
 * 0.99: USB 2.0: changed protocol to isochronous transfer
 *                IRQ transfer is too buggy and too risky in 2.0
 *                for the high speed ISO transfer is now a working version
 *                available
 * 0.99b: Increased the iso transfer buffer for high sp.to 10 buffers. Some VIA
 *        chipsets miss out IRQs. Deeper buffering is needed.
 * 1.00: full USB 2.0 support for the A/D converter. Now: max 8kHz sampling
 *       rate.
 *       Firmware vers 1.00 is needed for this.
 *       Two 16 bit up/down/reset counter with a sampling rate of 1kHz
 *       And loads of cleaning up, in particular streamlining the
 *       bulk transfers.
 * 1.1:  moved EP4 transfers to EP1 to make space for a PWM output on EP4
 * 1.2:  added PWM support via EP4
 * 2.0:  PWM seems to be stable and is not interfering with the other functions
 * 2.1:  changed PWM API
 * 2.2:  added firmware kernel request to fix an udev problem
 * 2.3:  corrected a bug in bulk timeouts which were far too short
 * 2.4:  fixed a bug which causes the driver to hang when it ran out of data.
 *       Thanks to Jan-Matthias Braun and Ian to spot the bug and fix it.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/fcntl.h>
#include <linux/compiler.h>

#include "../comedidev.h"

#include "comedi_fc.h"

/* constants for firmware upload and download */
#define USBDUX_FIRMWARE		"usbdux_firmware.bin"
#define USBDUX_FIRMWARE_MAX_LEN	0x2000
#define USBDUX_FIRMWARE_CMD	0xa0
#define VENDOR_DIR_IN		0xc0
#define VENDOR_DIR_OUT		0x40
#define USBDUX_CPU_CS		0xe600

/* usbdux bulk transfer commands */
#define USBDUX_CMD_MULT_AI	0
#define USBDUX_CMD_AO		1
#define USBDUX_CMD_DIO_CFG	2
#define USBDUX_CMD_DIO_BITS	3
#define USBDUX_CMD_SINGLE_AI	4
#define USBDUX_CMD_TIMER_RD	5
#define USBDUX_CMD_TIMER_WR	6
#define USBDUX_CMD_PWM_ON	7
#define USBDUX_CMD_PWM_OFF	8

/* timeout for the USB-transfer in ms */
#define BULK_TIMEOUT		1000

/* 300Hz max frequ under PWM */
#define MIN_PWM_PERIOD  ((long)(1E9/300))

/* Default PWM frequency */
#define PWM_DEFAULT_PERIOD ((long)(1E9/100))

/* Size of one A/D value */
#define SIZEADIN          ((sizeof(uint16_t)))

/*
 * Size of the input-buffer IN BYTES
 * Always multiple of 8 for 8 microframes which is needed in the highspeed mode
 */
#define SIZEINBUF         ((8*SIZEADIN))

/* 16 bytes. */
#define SIZEINSNBUF       16

/* size of one value for the D/A converter: channel and value */
#define SIZEDAOUT          ((sizeof(uint8_t)+sizeof(uint16_t)))

/*
 * Size of the output-buffer in bytes
 * Actually only the first 4 triplets are used but for the
 * high speed mode we need to pad it to 8 (microframes).
 */
#define SIZEOUTBUF         ((8*SIZEDAOUT))

/*
 * Size of the buffer for the dux commands: just now max size is determined
 * by the analogue out + command byte + panic bytes...
 */
#define SIZEOFDUXBUFFER    ((8*SIZEDAOUT+2))

/* Number of in-URBs which receive the data: min=2 */
#define NUMOFINBUFFERSFULL     5

/* Number of out-URBs which send the data: min=2 */
#define NUMOFOUTBUFFERSFULL    5

/* Number of in-URBs which receive the data: min=5 */
/* must have more buffers due to buggy USB ctr */
#define NUMOFINBUFFERSHIGH     10

/* Number of out-URBs which send the data: min=5 */
/* must have more buffers due to buggy USB ctr */
#define NUMOFOUTBUFFERSHIGH    10

/* number of retries to get the right dux command */
#define RETRIES 10

static const struct comedi_lrange range_usbdux_ai_range = {
	4, {
		BIP_RANGE(4.096),
		BIP_RANGE(4.096 / 2),
		UNI_RANGE(4.096),
		UNI_RANGE(4.096 / 2)
	}
};

static const struct comedi_lrange range_usbdux_ao_range = {
	2, {
		BIP_RANGE(4.096),
		UNI_RANGE(4.096)
	}
};

struct usbdux_private {
	/* actual number of in-buffers */
	int n_ai_urbs;
	/* actual number of out-buffers */
	int n_ao_urbs;
	/* ISO-transfer handling: buffers */
	struct urb **ai_urbs;
	struct urb **ao_urbs;
	/* pwm-transfer handling */
	struct urb *pwm_urb;
	/* PWM period */
	unsigned int pwm_period;
	/* PWM internal delay for the GPIF in the FX2 */
	uint8_t pwm_delay;
	/* size of the PWM buffer which holds the bit pattern */
	int pwm_buf_sz;
	/* input buffer for the ISO-transfer */
	__le16 *in_buf;
	/* input buffer for single insn */
	__le16 *insn_buf;

	unsigned int high_speed:1;
	unsigned int ai_cmd_running:1;
	unsigned int ao_cmd_running:1;
	unsigned int pwm_cmd_running:1;

	/* number of samples to acquire */
	int ai_sample_count;
	int ao_sample_count;
	/* time between samples in units of the timer */
	unsigned int ai_timer;
	unsigned int ao_timer;
	/* counter between aquisitions */
	unsigned int ai_counter;
	unsigned int ao_counter;
	/* interval in frames/uframes */
	unsigned int ai_interval;
	/* commands */
	uint8_t *dux_commands;
	struct semaphore sem;
};

static void usbdux_unlink_urbs(struct urb **urbs, int num_urbs)
{
	int i;

	for (i = 0; i < num_urbs; i++)
		usb_kill_urb(urbs[i]);
}

static void usbdux_ai_stop(struct comedi_device *dev, int do_unlink)
{
	struct usbdux_private *devpriv = dev->private;

	if (do_unlink && devpriv->ai_urbs)
		usbdux_unlink_urbs(devpriv->ai_urbs, devpriv->n_ai_urbs);

	devpriv->ai_cmd_running = 0;
}

static int usbdux_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct usbdux_private *devpriv = dev->private;

	/* prevent other CPUs from submitting new commands just now */
	down(&devpriv->sem);
	/* unlink only if the urb really has been submitted */
	usbdux_ai_stop(dev, devpriv->ai_cmd_running);
	up(&devpriv->sem);

	return 0;
}

/* analogue IN - interrupt service routine */
static void usbduxsub_ai_isoc_irq(struct urb *urb)
{
	struct comedi_device *dev = urb->context;
	struct comedi_subdevice *s = dev->read_subdev;
	struct usbdux_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int i, err;

	/* first we test if something unusual has just happened */
	switch (urb->status) {
	case 0:
		/* copy the result in the transfer buffer */
		memcpy(devpriv->in_buf, urb->transfer_buffer, SIZEINBUF);
		break;
	case -EILSEQ:
		/* error in the ISOchronous data */
		/* we don't copy the data into the transfer buffer */
		/* and recycle the last data byte */
		dev_dbg(dev->class_dev, "CRC error in ISO IN stream\n");
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -ECONNABORTED:
		/* happens after an unlink command */
		if (devpriv->ai_cmd_running) {
			s->async->events |= COMEDI_CB_EOA;
			s->async->events |= COMEDI_CB_ERROR;
			comedi_event(dev, s);
			/* stop the transfer w/o unlink */
			usbdux_ai_stop(dev, 0);
		}
		return;

	default:
		/* a real error on the bus */
		/* pass error to comedi if we are really running a command */
		if (devpriv->ai_cmd_running) {
			dev_err(dev->class_dev,
				"Non-zero urb status received in ai intr context: %d\n",
				urb->status);
			s->async->events |= COMEDI_CB_EOA;
			s->async->events |= COMEDI_CB_ERROR;
			comedi_event(dev, s);
			/* don't do an unlink here */
			usbdux_ai_stop(dev, 0);
		}
		return;
	}

	/*
	 * at this point we are reasonably sure that nothing dodgy has happened
	 * are we running a command?
	 */
	if (unlikely(!devpriv->ai_cmd_running)) {
		/*
		 * not running a command, do not continue execution if no
		 * asynchronous command is running in particular not resubmit
		 */
		return;
	}

	urb->dev = comedi_to_usb_dev(dev);

	/* resubmit the urb */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(err < 0)) {
		dev_err(dev->class_dev,
			"urb resubmit failed in int-context! err=%d\n", err);
		if (err == -EL2NSYNC)
			dev_err(dev->class_dev,
				"buggy USB host controller or bug in IRQ handler!\n");
		s->async->events |= COMEDI_CB_EOA;
		s->async->events |= COMEDI_CB_ERROR;
		comedi_event(dev, s);
		/* don't do an unlink here */
		usbdux_ai_stop(dev, 0);
		return;
	}

	devpriv->ai_counter--;
	if (likely(devpriv->ai_counter > 0))
		return;

	/* timer zero, transfer measurements to comedi */
	devpriv->ai_counter = devpriv->ai_timer;

	/* test, if we transmit only a fixed number of samples */
	if (cmd->stop_src == TRIG_COUNT) {
		/* not continuous, fixed number of samples */
		devpriv->ai_sample_count--;
		/* all samples received? */
		if (devpriv->ai_sample_count < 0) {
			/* prevent a resubmit next time */
			usbdux_ai_stop(dev, 0);
			/* say comedi that the acquistion is over */
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(dev, s);
			return;
		}
	}
	/* get the data from the USB bus and hand it over to comedi */
	for (i = 0; i < cmd->chanlist_len; i++) {
		unsigned int range = CR_RANGE(cmd->chanlist[i]);
		uint16_t val = le16_to_cpu(devpriv->in_buf[i]);

		/* bipolar data is two's-complement */
		if (comedi_range_is_bipolar(s, range))
			val ^= ((s->maxdata + 1) >> 1);

		/* transfer data */
		err = comedi_buf_put(s, val);
		if (unlikely(err == 0)) {
			/* buffer overflow */
			usbdux_ai_stop(dev, 0);
			return;
		}
	}
	/* tell comedi that data is there */
	s->async->events |= COMEDI_CB_BLOCK | COMEDI_CB_EOS;
	comedi_event(dev, s);
}

static void usbdux_ao_stop(struct comedi_device *dev, int do_unlink)
{
	struct usbdux_private *devpriv = dev->private;

	if (do_unlink && devpriv->ao_urbs)
		usbdux_unlink_urbs(devpriv->ao_urbs, devpriv->n_ao_urbs);

	devpriv->ao_cmd_running = 0;
}

static int usbdux_ao_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct usbdux_private *devpriv = dev->private;

	/* prevent other CPUs from submitting a command just now */
	down(&devpriv->sem);
	/* unlink only if it is really running */
	usbdux_ao_stop(dev, devpriv->ao_cmd_running);
	up(&devpriv->sem);

	return 0;
}

static void usbduxsub_ao_isoc_irq(struct urb *urb)
{
	struct comedi_device *dev = urb->context;
	struct comedi_subdevice *s = dev->write_subdev;
	struct usbdux_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	uint8_t *datap;
	int ret;
	int i;

	switch (urb->status) {
	case 0:
		/* success */
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -ECONNABORTED:
		/* after an unlink command, unplug, ... etc */
		/* no unlink needed here. Already shutting down. */
		if (devpriv->ao_cmd_running) {
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(dev, s);
			usbdux_ao_stop(dev, 0);
		}
		return;

	default:
		/* a real error */
		if (devpriv->ao_cmd_running) {
			dev_err(dev->class_dev,
				"Non-zero urb status received in ao intr context: %d\n",
				urb->status);
			s->async->events |= COMEDI_CB_ERROR;
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(dev, s);
			/* we do an unlink if we are in the high speed mode */
			usbdux_ao_stop(dev, 0);
		}
		return;
	}

	/* are we actually running? */
	if (!devpriv->ao_cmd_running)
		return;

	/* normal operation: executing a command in this subdevice */
	devpriv->ao_counter--;
	if ((int)devpriv->ao_counter <= 0) {
		/* timer zero */
		devpriv->ao_counter = devpriv->ao_timer;

		/* handle non continous acquisition */
		if (cmd->stop_src == TRIG_COUNT) {
			/* fixed number of samples */
			devpriv->ao_sample_count--;
			if (devpriv->ao_sample_count < 0) {
				/* all samples transmitted */
				usbdux_ao_stop(dev, 0);
				s->async->events |= COMEDI_CB_EOA;
				comedi_event(dev, s);
				/* no resubmit of the urb */
				return;
			}
		}

		/* transmit data to the USB bus */
		datap = urb->transfer_buffer;
		*datap++ = cmd->chanlist_len;
		for (i = 0; i < cmd->chanlist_len; i++) {
			unsigned int chan = CR_CHAN(cmd->chanlist[i]);
			unsigned short val;

			ret = comedi_buf_get(s, &val);
			if (ret < 0) {
				dev_err(dev->class_dev, "buffer underflow\n");
				s->async->events |= (COMEDI_CB_EOA |
						     COMEDI_CB_OVERFLOW);
			}
			/* pointer to the DA */
			*datap++ = val & 0xff;
			*datap++ = (val >> 8) & 0xff;
			*datap++ = chan << 6;
			s->readback[chan] = val;

			s->async->events |= COMEDI_CB_BLOCK;
			comedi_event(dev, s);
		}
	}
	urb->transfer_buffer_length = SIZEOUTBUF;
	urb->dev = comedi_to_usb_dev(dev);
	urb->status = 0;
	if (devpriv->ao_cmd_running) {
		if (devpriv->high_speed)
			urb->interval = 8;	/* uframes */
		else
			urb->interval = 1;	/* frames */
		urb->number_of_packets = 1;
		urb->iso_frame_desc[0].offset = 0;
		urb->iso_frame_desc[0].length = SIZEOUTBUF;
		urb->iso_frame_desc[0].status = 0;
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret < 0) {
			dev_err(dev->class_dev,
				"ao urb resubm failed in int-cont. ret=%d",
				ret);
			if (ret == -EL2NSYNC)
				dev_err(dev->class_dev,
					"buggy USB host controller or bug in IRQ handling!\n");

			s->async->events |= COMEDI_CB_EOA;
			s->async->events |= COMEDI_CB_ERROR;
			comedi_event(dev, s);
			/* don't do an unlink here */
			usbdux_ao_stop(dev, 0);
		}
	}
}

static int usbdux_submit_urbs(struct comedi_device *dev,
			      struct urb **urbs, int num_urbs,
			      int input_urb)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	struct urb *urb;
	int ret;
	int i;

	/* Submit all URBs and start the transfer on the bus */
	for (i = 0; i < num_urbs; i++) {
		urb = urbs[i];

		/* in case of a resubmission after an unlink... */
		if (input_urb)
			urb->interval = devpriv->ai_interval;
		urb->context = dev;
		urb->dev = usb;
		urb->status = 0;
		urb->transfer_flags = URB_ISO_ASAP;

		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret)
			return ret;
	}
	return 0;
}

static int usbdux_ai_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	struct usbdux_private *this_usbduxsub = dev->private;
	int err = 0, i;
	unsigned int tmp_timer;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_TIMER);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_FOLLOW)	/* internal trigger */
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (this_usbduxsub->high_speed) {
			/*
			 * In high speed mode microframes are possible.
			 * However, during one microframe we can roughly
			 * sample one channel. Thus, the more channels
			 * are in the channel list the more time we need.
			 */
			i = 1;
			/* find a power of 2 for the number of channels */
			while (i < (cmd->chanlist_len))
				i = i * 2;

			err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
							 1000000 / 8 * i);
			/* now calc the real sampling rate with all the
			 * rounding errors */
			tmp_timer =
			    ((unsigned int)(cmd->scan_begin_arg / 125000)) *
			    125000;
		} else {
			/* full speed */
			/* 1kHz scans every USB frame */
			err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
							 1000000);
			/*
			 * calc the real sampling rate with the rounding errors
			 */
			tmp_timer = ((unsigned int)(cmd->scan_begin_arg /
						   1000000)) * 1000000;
		}
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg,
						tmp_timer);
	}

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	return 0;
}

/*
 * creates the ADC command for the MAX1271
 * range is the range value from comedi
 */
static uint8_t create_adc_command(unsigned int chan, unsigned int range)
{
	uint8_t p = (range <= 1);
	uint8_t r = ((range % 2) == 0);

	return (chan << 4) | ((p == 1) << 2) | ((r == 1) << 3);
}

static int send_dux_commands(struct comedi_device *dev, unsigned int cmd_type)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	int nsent;

	devpriv->dux_commands[0] = cmd_type;

	return usb_bulk_msg(usb, usb_sndbulkpipe(usb, 1),
			    devpriv->dux_commands, SIZEOFDUXBUFFER,
			    &nsent, BULK_TIMEOUT);
}

static int receive_dux_commands(struct comedi_device *dev, unsigned int command)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	int ret;
	int nrec;
	int i;

	for (i = 0; i < RETRIES; i++) {
		ret = usb_bulk_msg(usb, usb_rcvbulkpipe(usb, 8),
				      devpriv->insn_buf, SIZEINSNBUF,
				      &nrec, BULK_TIMEOUT);
		if (ret < 0)
			return ret;
		if (le16_to_cpu(devpriv->insn_buf[0]) == command)
			return ret;
	}
	/* command not received */
	return -EFAULT;
}

static int usbdux_ai_inttrig(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     unsigned int trig_num)
{
	struct usbdux_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int ret;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	down(&devpriv->sem);

	if (!devpriv->ai_cmd_running) {
		devpriv->ai_cmd_running = 1;
		ret = usbdux_submit_urbs(dev, devpriv->ai_urbs,
					 devpriv->n_ai_urbs, 1);
		if (ret < 0) {
			devpriv->ai_cmd_running = 0;
			goto ai_trig_exit;
		}
		s->async->inttrig = NULL;
	} else {
		ret = -EBUSY;
	}

ai_trig_exit:
	up(&devpriv->sem);
	return ret;
}

static int usbdux_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct usbdux_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int len = cmd->chanlist_len;
	int ret = -EBUSY;
	int i;

	/* block other CPUs from starting an ai_cmd */
	down(&devpriv->sem);

	if (devpriv->ai_cmd_running)
		goto ai_cmd_exit;

	/* set current channel of the running acquisition to zero */
	s->async->cur_chan = 0;

	devpriv->dux_commands[1] = len;
	for (i = 0; i < len; ++i) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);
		unsigned int range = CR_RANGE(cmd->chanlist[i]);

		devpriv->dux_commands[i + 2] = create_adc_command(chan, range);
	}

	ret = send_dux_commands(dev, USBDUX_CMD_MULT_AI);
	if (ret < 0)
		goto ai_cmd_exit;

	if (devpriv->high_speed) {
		/*
		 * every channel gets a time window of 125us. Thus, if we
		 * sample all 8 channels we need 1ms. If we sample only one
		 * channel we need only 125us
		 */
		devpriv->ai_interval = 1;
		/* find a power of 2 for the interval */
		while (devpriv->ai_interval < len)
			devpriv->ai_interval *= 2;

		devpriv->ai_timer = cmd->scan_begin_arg /
				    (125000 * devpriv->ai_interval);
	} else {
		/* interval always 1ms */
		devpriv->ai_interval = 1;
		devpriv->ai_timer = cmd->scan_begin_arg / 1000000;
	}
	if (devpriv->ai_timer < 1) {
		ret = -EINVAL;
		goto ai_cmd_exit;
	}

	devpriv->ai_counter = devpriv->ai_timer;

	if (cmd->stop_src == TRIG_COUNT) {
		/* data arrives as one packet */
		devpriv->ai_sample_count = cmd->stop_arg;
	} else {
		/* continous acquisition */
		devpriv->ai_sample_count = 0;
	}

	if (cmd->start_src == TRIG_NOW) {
		/* enable this acquisition operation */
		devpriv->ai_cmd_running = 1;
		ret = usbdux_submit_urbs(dev, devpriv->ai_urbs,
					 devpriv->n_ai_urbs, 1);
		if (ret < 0) {
			devpriv->ai_cmd_running = 0;
			/* fixme: unlink here?? */
			goto ai_cmd_exit;
		}
		s->async->inttrig = NULL;
	} else {
		/* TRIG_INT */
		/* don't enable the acquision operation */
		/* wait for an internal signal */
		s->async->inttrig = usbdux_ai_inttrig;
	}

ai_cmd_exit:
	up(&devpriv->sem);

	return ret;
}

/* Mode 0 is used to get a single conversion on demand */
static int usbdux_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct usbdux_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	int ret = -EBUSY;
	int i;

	down(&devpriv->sem);

	if (devpriv->ai_cmd_running)
		goto ai_read_exit;

	/* set command for the first channel */
	devpriv->dux_commands[1] = create_adc_command(chan, range);

	/* adc commands */
	ret = send_dux_commands(dev, USBDUX_CMD_SINGLE_AI);
	if (ret < 0)
		goto ai_read_exit;

	for (i = 0; i < insn->n; i++) {
		ret = receive_dux_commands(dev, USBDUX_CMD_SINGLE_AI);
		if (ret < 0)
			goto ai_read_exit;

		val = le16_to_cpu(devpriv->insn_buf[1]);

		/* bipolar data is two's-complement */
		if (comedi_range_is_bipolar(s, range))
			val ^= ((s->maxdata + 1) >> 1);

		data[i] = val;
	}

ai_read_exit:
	up(&devpriv->sem);

	return ret ? ret : insn->n;
}

static int usbdux_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct usbdux_private *devpriv = dev->private;
	int ret;

	down(&devpriv->sem);
	ret = comedi_readback_insn_read(dev, s, insn, data);
	up(&devpriv->sem);

	return ret;
}

static int usbdux_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct usbdux_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	__le16 *p = (__le16 *)&devpriv->dux_commands[2];
	int ret = -EBUSY;
	int i;

	down(&devpriv->sem);

	if (devpriv->ao_cmd_running)
		goto ao_write_exit;

	/* number of channels: 1 */
	devpriv->dux_commands[1] = 1;
	/* channel number */
	devpriv->dux_commands[4] = chan << 6;

	for (i = 0; i < insn->n; i++) {
		val = data[i];

		/* one 16 bit value */
		*p = cpu_to_le16(val);

		ret = send_dux_commands(dev, USBDUX_CMD_AO);
		if (ret < 0)
			goto ao_write_exit;

		s->readback[chan] = val;
	}

ao_write_exit:
	up(&devpriv->sem);

	return ret ? ret : insn->n;
}

static int usbdux_ao_inttrig(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     unsigned int trig_num)
{
	struct usbdux_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int ret;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	down(&devpriv->sem);

	if (!devpriv->ao_cmd_running) {
		devpriv->ao_cmd_running = 1;
		ret = usbdux_submit_urbs(dev, devpriv->ao_urbs,
					 devpriv->n_ao_urbs, 0);
		if (ret < 0) {
			devpriv->ao_cmd_running = 0;
			goto ao_trig_exit;
		}
		s->async->inttrig = NULL;
	} else {
		ret = -EBUSY;
	}

ao_trig_exit:
	up(&devpriv->sem);
	return ret;
}

static int usbdux_ao_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	struct usbdux_private *this_usbduxsub = dev->private;
	int err = 0;
	unsigned int flags;

	if (!this_usbduxsub)
		return -EFAULT;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);

	if (0) {		/* (this_usbduxsub->high_speed) */
		/* the sampling rate is set by the coversion rate */
		flags = TRIG_FOLLOW;
	} else {
		/* start a new scan (output at once) with a timer */
		flags = TRIG_TIMER;
	}
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, flags);

	if (0) {		/* (this_usbduxsub->high_speed) */
		/*
		 * in usb-2.0 only one conversion it transmitted
		 * but with 8kHz/n
		 */
		flags = TRIG_TIMER;
	} else {
		/*
		 * all conversion events happen simultaneously with
		 * a rate of 1kHz/n
		 */
		flags = TRIG_NOW;
	}
	err |= cfc_check_trigger_src(&cmd->convert_src, flags);

	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_FOLLOW)	/* internal trigger */
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	if (cmd->scan_begin_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						 1000000);

	/* not used now, is for later use */
	if (cmd->convert_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg, 125000);

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	return 0;
}

static int usbdux_ao_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct usbdux_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int ret = -EBUSY;

	down(&devpriv->sem);

	if (devpriv->ao_cmd_running)
		goto ao_cmd_exit;

	/* set current channel of the running acquisition to zero */
	s->async->cur_chan = 0;

	/* we count in steps of 1ms (125us) */
	/* 125us mode not used yet */
	if (0) {		/* (devpriv->high_speed) */
		/* 125us */
		/* timing of the conversion itself: every 125 us */
		devpriv->ao_timer = cmd->convert_arg / 125000;
	} else {
		/* 1ms */
		/* timing of the scan: we get all channels at once */
		devpriv->ao_timer = cmd->scan_begin_arg / 1000000;
		if (devpriv->ao_timer < 1) {
			ret = -EINVAL;
			goto ao_cmd_exit;
		}
	}

	devpriv->ao_counter = devpriv->ao_timer;

	if (cmd->stop_src == TRIG_COUNT) {
		/* not continuous */
		/* counter */
		/* high speed also scans everything at once */
		if (0) {	/* (devpriv->high_speed) */
			devpriv->ao_sample_count = cmd->stop_arg *
						   cmd->scan_end_arg;
		} else {
			/* there's no scan as the scan has been */
			/* perf inside the FX2 */
			/* data arrives as one packet */
			devpriv->ao_sample_count = cmd->stop_arg;
		}
	} else {
		/* continous acquisition */
		devpriv->ao_sample_count = 0;
	}

	if (cmd->start_src == TRIG_NOW) {
		/* enable this acquisition operation */
		devpriv->ao_cmd_running = 1;
		ret = usbdux_submit_urbs(dev, devpriv->ao_urbs,
					 devpriv->n_ao_urbs, 0);
		if (ret < 0) {
			devpriv->ao_cmd_running = 0;
			/* fixme: unlink here?? */
			goto ao_cmd_exit;
		}
		s->async->inttrig = NULL;
	} else {
		/* TRIG_INT */
		/* submit the urbs later */
		/* wait for an internal signal */
		s->async->inttrig = usbdux_ao_inttrig;
	}

ao_cmd_exit:
	up(&devpriv->sem);

	return ret;
}

static int usbdux_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	/*
	 * We don't tell the firmware here as it would take 8 frames
	 * to submit the information. We do it in the insn_bits.
	 */
	return insn->n;
}

static int usbdux_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{

	struct usbdux_private *devpriv = dev->private;
	int ret;

	down(&devpriv->sem);

	comedi_dio_update_state(s, data);

	/* Always update the hardware. See the (*insn_config). */
	devpriv->dux_commands[1] = s->io_bits;
	devpriv->dux_commands[2] = s->state;

	/*
	 * This command also tells the firmware to return
	 * the digital input lines.
	 */
	ret = send_dux_commands(dev, USBDUX_CMD_DIO_BITS);
	if (ret < 0)
		goto dio_exit;
	ret = receive_dux_commands(dev, USBDUX_CMD_DIO_BITS);
	if (ret < 0)
		goto dio_exit;

	data[1] = le16_to_cpu(devpriv->insn_buf[1]);

dio_exit:
	up(&devpriv->sem);

	return ret ? ret : insn->n;
}

static int usbdux_counter_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct usbdux_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int ret = 0;
	int i;

	down(&devpriv->sem);

	for (i = 0; i < insn->n; i++) {
		ret = send_dux_commands(dev, USBDUX_CMD_TIMER_RD);
		if (ret < 0)
			goto counter_read_exit;
		ret = receive_dux_commands(dev, USBDUX_CMD_TIMER_RD);
		if (ret < 0)
			goto counter_read_exit;

		data[i] = le16_to_cpu(devpriv->insn_buf[chan + 1]);
	}

counter_read_exit:
	up(&devpriv->sem);

	return ret ? ret : insn->n;
}

static int usbdux_counter_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct usbdux_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	__le16 *p = (__le16 *)&devpriv->dux_commands[2];
	int ret = 0;
	int i;

	down(&devpriv->sem);

	devpriv->dux_commands[1] = chan;

	for (i = 0; i < insn->n; i++) {
		*p = cpu_to_le16(data[i]);

		ret = send_dux_commands(dev, USBDUX_CMD_TIMER_WR);
		if (ret < 0)
			break;
	}

	up(&devpriv->sem);

	return ret ? ret : insn->n;
}

static int usbdux_counter_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	/* nothing to do so far */
	return 2;
}

static void usbduxsub_unlink_pwm_urbs(struct comedi_device *dev)
{
	struct usbdux_private *devpriv = dev->private;

	usb_kill_urb(devpriv->pwm_urb);
}

static void usbdux_pwm_stop(struct comedi_device *dev, int do_unlink)
{
	struct usbdux_private *devpriv = dev->private;

	if (do_unlink)
		usbduxsub_unlink_pwm_urbs(dev);

	devpriv->pwm_cmd_running = 0;
}

static int usbdux_pwm_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct usbdux_private *devpriv = dev->private;
	int ret;

	down(&devpriv->sem);
	/* unlink only if it is really running */
	usbdux_pwm_stop(dev, devpriv->pwm_cmd_running);
	ret = send_dux_commands(dev, USBDUX_CMD_PWM_OFF);
	up(&devpriv->sem);

	return ret;
}

static void usbduxsub_pwm_irq(struct urb *urb)
{
	struct comedi_device *dev = urb->context;
	struct usbdux_private *devpriv = dev->private;
	int ret;

	switch (urb->status) {
	case 0:
		/* success */
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -ECONNABORTED:
		/*
		 * after an unlink command, unplug, ... etc
		 * no unlink needed here. Already shutting down.
		 */
		if (devpriv->pwm_cmd_running)
			usbdux_pwm_stop(dev, 0);

		return;

	default:
		/* a real error */
		if (devpriv->pwm_cmd_running) {
			dev_err(dev->class_dev,
				"Non-zero urb status received in pwm intr context: %d\n",
				urb->status);
			usbdux_pwm_stop(dev, 0);
		}
		return;
	}

	/* are we actually running? */
	if (!devpriv->pwm_cmd_running)
		return;

	urb->transfer_buffer_length = devpriv->pwm_buf_sz;
	urb->dev = comedi_to_usb_dev(dev);
	urb->status = 0;
	if (devpriv->pwm_cmd_running) {
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret < 0) {
			dev_err(dev->class_dev,
				"pwm urb resubm failed in int-cont. ret=%d",
				ret);
			if (ret == -EL2NSYNC)
				dev_err(dev->class_dev,
					"buggy USB host controller or bug in IRQ handling!\n");

			/* don't do an unlink here */
			usbdux_pwm_stop(dev, 0);
		}
	}
}

static int usbduxsub_submit_pwm_urbs(struct comedi_device *dev)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	struct urb *urb = devpriv->pwm_urb;

	/* in case of a resubmission after an unlink... */
	usb_fill_bulk_urb(urb, usb, usb_sndbulkpipe(usb, 4),
			  urb->transfer_buffer,
			  devpriv->pwm_buf_sz,
			  usbduxsub_pwm_irq,
			  dev);

	return usb_submit_urb(urb, GFP_ATOMIC);
}

static int usbdux_pwm_period(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     unsigned int period)
{
	struct usbdux_private *devpriv = dev->private;
	int fx2delay = 255;

	if (period < MIN_PWM_PERIOD)
		return -EAGAIN;

	fx2delay = (period / (6 * 512 * 1000 / 33)) - 6;
	if (fx2delay > 255)
		return -EAGAIN;

	devpriv->pwm_delay = fx2delay;
	devpriv->pwm_period = period;

	return 0;
}

static int usbdux_pwm_start(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct usbdux_private *devpriv = dev->private;
	int ret = 0;

	down(&devpriv->sem);

	if (devpriv->pwm_cmd_running)
		goto pwm_start_exit;

	devpriv->dux_commands[1] = devpriv->pwm_delay;
	ret = send_dux_commands(dev, USBDUX_CMD_PWM_ON);
	if (ret < 0)
		goto pwm_start_exit;

	/* initialise the buffer */
	memset(devpriv->pwm_urb->transfer_buffer, 0, devpriv->pwm_buf_sz);

	devpriv->pwm_cmd_running = 1;
	ret = usbduxsub_submit_pwm_urbs(dev);
	if (ret < 0)
		devpriv->pwm_cmd_running = 0;

pwm_start_exit:
	up(&devpriv->sem);

	return ret;
}

static void usbdux_pwm_pattern(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       unsigned int chan,
			       unsigned int value,
			       unsigned int sign)
{
	struct usbdux_private *devpriv = dev->private;
	char pwm_mask = (1 << chan);	/* DIO bit for the PWM data */
	char sgn_mask = (16 << chan);	/* DIO bit for the sign */
	char *buf = (char *)(devpriv->pwm_urb->transfer_buffer);
	int szbuf = devpriv->pwm_buf_sz;
	int i;

	for (i = 0; i < szbuf; i++) {
		char c = *buf;

		c &= ~pwm_mask;
		if (i < value)
			c |= pwm_mask;
		if (!sign)
			c &= ~sgn_mask;
		else
			c |= sgn_mask;
		*buf++ = c;
	}
}

static int usbdux_pwm_write(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn,
			    unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);

	/*
	 * It doesn't make sense to support more than one value here
	 * because it would just overwrite the PWM buffer.
	 */
	if (insn->n != 1)
		return -EINVAL;

	/*
	 * The sign is set via a special INSN only, this gives us 8 bits
	 * for normal operation, sign is 0 by default.
	 */
	usbdux_pwm_pattern(dev, s, chan, data[0], 0);

	return insn->n;
}

static int usbdux_pwm_config(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	struct usbdux_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);

	switch (data[0]) {
	case INSN_CONFIG_ARM:
		/*
		 * if not zero the PWM is limited to a certain time which is
		 * not supported here
		 */
		if (data[1] != 0)
			return -EINVAL;
		return usbdux_pwm_start(dev, s);
	case INSN_CONFIG_DISARM:
		return usbdux_pwm_cancel(dev, s);
	case INSN_CONFIG_GET_PWM_STATUS:
		data[1] = devpriv->pwm_cmd_running;
		return 0;
	case INSN_CONFIG_PWM_SET_PERIOD:
		return usbdux_pwm_period(dev, s, data[1]);
	case INSN_CONFIG_PWM_GET_PERIOD:
		data[1] = devpriv->pwm_period;
		return 0;
	case INSN_CONFIG_PWM_SET_H_BRIDGE:
		/*
		 * data[1] = value
		 * data[2] = sign (for a relay)
		 */
		usbdux_pwm_pattern(dev, s, chan, data[1], (data[2] != 0));
		return 0;
	case INSN_CONFIG_PWM_GET_H_BRIDGE:
		/* values are not kept in this driver, nothing to return here */
		return -EINVAL;
	}
	return -EINVAL;
}

static int usbdux_firmware_upload(struct comedi_device *dev,
				  const u8 *data, size_t size,
				  unsigned long context)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	uint8_t *buf;
	uint8_t *tmp;
	int ret;

	if (!data)
		return 0;

	if (size > USBDUX_FIRMWARE_MAX_LEN) {
		dev_err(dev->class_dev,
			"usbdux firmware binary it too large for FX2.\n");
		return -ENOMEM;
	}

	/* we generate a local buffer for the firmware */
	buf = kmemdup(data, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* we need a malloc'ed buffer for usb_control_msg() */
	tmp = kmalloc(1, GFP_KERNEL);
	if (!tmp) {
		kfree(buf);
		return -ENOMEM;
	}

	/* stop the current firmware on the device */
	*tmp = 1;	/* 7f92 to one */
	ret = usb_control_msg(usb, usb_sndctrlpipe(usb, 0),
			      USBDUX_FIRMWARE_CMD,
			      VENDOR_DIR_OUT,
			      USBDUX_CPU_CS, 0x0000,
			      tmp, 1,
			      BULK_TIMEOUT);
	if (ret < 0) {
		dev_err(dev->class_dev, "can not stop firmware\n");
		goto done;
	}

	/* upload the new firmware to the device */
	ret = usb_control_msg(usb, usb_sndctrlpipe(usb, 0),
			      USBDUX_FIRMWARE_CMD,
			      VENDOR_DIR_OUT,
			      0, 0x0000,
			      buf, size,
			      BULK_TIMEOUT);
	if (ret < 0) {
		dev_err(dev->class_dev, "firmware upload failed\n");
		goto done;
	}

	/* start the new firmware on the device */
	*tmp = 0;	/* 7f92 to zero */
	ret = usb_control_msg(usb, usb_sndctrlpipe(usb, 0),
			      USBDUX_FIRMWARE_CMD,
			      VENDOR_DIR_OUT,
			      USBDUX_CPU_CS, 0x0000,
			      tmp, 1,
			      BULK_TIMEOUT);
	if (ret < 0)
		dev_err(dev->class_dev, "can not start firmware\n");

done:
	kfree(tmp);
	kfree(buf);
	return ret;
}

static int usbdux_alloc_usb_buffers(struct comedi_device *dev)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	struct urb *urb;
	int i;

	devpriv->dux_commands = kzalloc(SIZEOFDUXBUFFER, GFP_KERNEL);
	devpriv->in_buf = kzalloc(SIZEINBUF, GFP_KERNEL);
	devpriv->insn_buf = kzalloc(SIZEINSNBUF, GFP_KERNEL);
	devpriv->ai_urbs = kcalloc(devpriv->n_ai_urbs, sizeof(void *),
				   GFP_KERNEL);
	devpriv->ao_urbs = kcalloc(devpriv->n_ao_urbs, sizeof(void *),
				   GFP_KERNEL);
	if (!devpriv->dux_commands || !devpriv->in_buf || !devpriv->insn_buf ||
	    !devpriv->ai_urbs || !devpriv->ao_urbs)
		return -ENOMEM;

	for (i = 0; i < devpriv->n_ai_urbs; i++) {
		/* one frame: 1ms */
		urb = usb_alloc_urb(1, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		devpriv->ai_urbs[i] = urb;

		urb->dev = usb;
		urb->context = dev;
		urb->pipe = usb_rcvisocpipe(usb, 6);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = kzalloc(SIZEINBUF, GFP_KERNEL);
		if (!urb->transfer_buffer)
			return -ENOMEM;

		urb->complete = usbduxsub_ai_isoc_irq;
		urb->number_of_packets = 1;
		urb->transfer_buffer_length = SIZEINBUF;
		urb->iso_frame_desc[0].offset = 0;
		urb->iso_frame_desc[0].length = SIZEINBUF;
	}

	for (i = 0; i < devpriv->n_ao_urbs; i++) {
		/* one frame: 1ms */
		urb = usb_alloc_urb(1, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		devpriv->ao_urbs[i] = urb;

		urb->dev = usb;
		urb->context = dev;
		urb->pipe = usb_sndisocpipe(usb, 2);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = kzalloc(SIZEOUTBUF, GFP_KERNEL);
		if (!urb->transfer_buffer)
			return -ENOMEM;

		urb->complete = usbduxsub_ao_isoc_irq;
		urb->number_of_packets = 1;
		urb->transfer_buffer_length = SIZEOUTBUF;
		urb->iso_frame_desc[0].offset = 0;
		urb->iso_frame_desc[0].length = SIZEOUTBUF;
		if (devpriv->high_speed)
			urb->interval = 8;	/* uframes */
		else
			urb->interval = 1;	/* frames */
	}

	/* pwm */
	if (devpriv->pwm_buf_sz) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		devpriv->pwm_urb = urb;

		/* max bulk ep size in high speed */
		urb->transfer_buffer = kzalloc(devpriv->pwm_buf_sz,
					       GFP_KERNEL);
		if (!urb->transfer_buffer)
			return -ENOMEM;
	}

	return 0;
}

static void usbdux_free_usb_buffers(struct comedi_device *dev)
{
	struct usbdux_private *devpriv = dev->private;
	struct urb *urb;
	int i;

	urb = devpriv->pwm_urb;
	if (urb) {
		kfree(urb->transfer_buffer);
		usb_free_urb(urb);
	}
	if (devpriv->ao_urbs) {
		for (i = 0; i < devpriv->n_ao_urbs; i++) {
			urb = devpriv->ao_urbs[i];
			if (urb) {
				kfree(urb->transfer_buffer);
				usb_free_urb(urb);
			}
		}
		kfree(devpriv->ao_urbs);
	}
	if (devpriv->ai_urbs) {
		for (i = 0; i < devpriv->n_ai_urbs; i++) {
			urb = devpriv->ai_urbs[i];
			if (urb) {
				kfree(urb->transfer_buffer);
				usb_free_urb(urb);
			}
		}
		kfree(devpriv->ai_urbs);
	}
	kfree(devpriv->insn_buf);
	kfree(devpriv->in_buf);
	kfree(devpriv->dux_commands);
}

static int usbdux_auto_attach(struct comedi_device *dev,
			      unsigned long context_unused)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	sema_init(&devpriv->sem, 1);

	usb_set_intfdata(intf, devpriv);

	devpriv->high_speed = (usb->speed == USB_SPEED_HIGH);
	if (devpriv->high_speed) {
		devpriv->n_ai_urbs = NUMOFINBUFFERSHIGH;
		devpriv->n_ao_urbs = NUMOFOUTBUFFERSHIGH;
		devpriv->pwm_buf_sz = 512;
	} else {
		devpriv->n_ai_urbs = NUMOFINBUFFERSFULL;
		devpriv->n_ao_urbs = NUMOFOUTBUFFERSFULL;
	}

	ret = usbdux_alloc_usb_buffers(dev);
	if (ret)
		return ret;

	/* setting to alternate setting 3: enabling iso ep and bulk ep. */
	ret = usb_set_interface(usb, intf->altsetting->desc.bInterfaceNumber,
				3);
	if (ret < 0) {
		dev_err(dev->class_dev,
			"could not set alternate setting 3 in high speed\n");
		return ret;
	}

	ret = comedi_load_firmware(dev, &usb->dev, USBDUX_FIRMWARE,
				   usbdux_firmware_upload, 0);
	if (ret < 0)
		return ret;

	ret = comedi_alloc_subdevices(dev, (devpriv->high_speed) ? 5 : 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_CMD_READ;
	s->n_chan	= 8;
	s->maxdata	= 0x0fff;
	s->len_chanlist	= 8;
	s->range_table	= &range_usbdux_ai_range;
	s->insn_read	= usbdux_ai_insn_read;
	s->do_cmdtest	= usbdux_ai_cmdtest;
	s->do_cmd	= usbdux_ai_cmd;
	s->cancel	= usbdux_ai_cancel;

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	dev->write_subdev = s;
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE | SDF_GROUND | SDF_CMD_WRITE;
	s->n_chan	= 4;
	s->maxdata	= 0x0fff;
	s->len_chanlist	= s->n_chan;
	s->range_table	= &range_usbdux_ao_range;
	s->do_cmdtest	= usbdux_ao_cmdtest;
	s->do_cmd	= usbdux_ao_cmd;
	s->cancel	= usbdux_ao_cancel;
	s->insn_read	= usbdux_ao_insn_read;
	s->insn_write	= usbdux_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital I/O subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= usbdux_dio_insn_bits;
	s->insn_config	= usbdux_dio_insn_config;

	/* Counter subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE;
	s->n_chan	= 4;
	s->maxdata	= 0xffff;
	s->insn_read	= usbdux_counter_read;
	s->insn_write	= usbdux_counter_write;
	s->insn_config	= usbdux_counter_config;

	if (devpriv->high_speed) {
		/* PWM subdevice */
		s = &dev->subdevices[4];
		s->type		= COMEDI_SUBD_PWM;
		s->subdev_flags	= SDF_WRITABLE | SDF_PWM_HBRIDGE;
		s->n_chan	= 8;
		s->maxdata	= devpriv->pwm_buf_sz;
		s->insn_write	= usbdux_pwm_write;
		s->insn_config	= usbdux_pwm_config;

		usbdux_pwm_period(dev, s, PWM_DEFAULT_PERIOD);
	}

	return 0;
}

static void usbdux_detach(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usbdux_private *devpriv = dev->private;

	usb_set_intfdata(intf, NULL);

	if (!devpriv)
		return;

	down(&devpriv->sem);

	/* force unlink all urbs */
	usbdux_pwm_stop(dev, 1);
	usbdux_ao_stop(dev, 1);
	usbdux_ai_stop(dev, 1);

	usbdux_free_usb_buffers(dev);

	up(&devpriv->sem);
}

static struct comedi_driver usbdux_driver = {
	.driver_name	= "usbdux",
	.module		= THIS_MODULE,
	.auto_attach	= usbdux_auto_attach,
	.detach		= usbdux_detach,
};

static int usbdux_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	return comedi_usb_auto_config(intf, &usbdux_driver, 0);
}

static const struct usb_device_id usbdux_usb_table[] = {
	{ USB_DEVICE(0x13d8, 0x0001) },
	{ USB_DEVICE(0x13d8, 0x0002) },
	{ }
};
MODULE_DEVICE_TABLE(usb, usbdux_usb_table);

static struct usb_driver usbdux_usb_driver = {
	.name		= "usbdux",
	.probe		= usbdux_usb_probe,
	.disconnect	= comedi_usb_auto_unconfig,
	.id_table	= usbdux_usb_table,
};
module_comedi_usb_driver(usbdux_driver, usbdux_usb_driver);

MODULE_AUTHOR("Bernd Porr, BerndPorr@f2s.com");
MODULE_DESCRIPTION("Stirling/ITL USB-DUX -- Bernd.Porr@f2s.com");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(USBDUX_FIRMWARE);
