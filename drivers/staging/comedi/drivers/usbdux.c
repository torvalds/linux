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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/fcntl.h>
#include <linux/compiler.h>

#include "../comedidev.h"

#include "comedi_fc.h"

/* timeout for the USB-transfer in ms*/
#define BULK_TIMEOUT 1000

/* constants for "firmware" upload and download */
#define FIRMWARE "usbdux_firmware.bin"
#define USBDUXSUB_FIRMWARE 0xA0
#define VENDOR_DIR_IN  0xC0
#define VENDOR_DIR_OUT 0x40

/* internal addresses of the 8051 processor */
#define USBDUXSUB_CPUCS 0xE600

/*
 * the minor device number, major is 180 only for debugging purposes and to
 * upload special firmware (programming the eeprom etc) which is not compatible
 * with the comedi framwork
 */
#define USBDUXSUB_MINOR 32

/* max lenghth of the transfer-buffer for software upload */
#define TB_LEN 0x2000

/* Input endpoint number: ISO/IRQ */
#define ISOINEP           6

/* Output endpoint number: ISO/IRQ */
#define ISOOUTEP          2

/* This EP sends DUX commands to USBDUX */
#define COMMAND_OUT_EP     1

/* This EP receives the DUX commands from USBDUX */
#define COMMAND_IN_EP        8

/* Output endpoint for PWM */
#define PWM_EP         4

/* 300Hz max frequ under PWM */
#define MIN_PWM_PERIOD  ((long)(1E9/300))

/* Default PWM frequency */
#define PWM_DEFAULT_PERIOD ((long)(1E9/100))

/* Number of channels */
#define NUMCHANNELS       8

/* Size of one A/D value */
#define SIZEADIN          ((sizeof(int16_t)))

/*
 * Size of the input-buffer IN BYTES
 * Always multiple of 8 for 8 microframes which is needed in the highspeed mode
 */
#define SIZEINBUF         ((8*SIZEADIN))

/* 16 bytes. */
#define SIZEINSNBUF       16

/* Number of DA channels */
#define NUMOUTCHANNELS    8

/* size of one value for the D/A converter: channel and value */
#define SIZEDAOUT          ((sizeof(int8_t)+sizeof(int16_t)))

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
	int num_in_buffers;
	/* actual number of out-buffers */
	int num_out_buffers;
	/* ISO-transfer handling: buffers */
	struct urb **urb_in;
	struct urb **urb_out;
	/* pwm-transfer handling */
	struct urb *urb_pwm;
	/* PWM period */
	unsigned int pwm_period;
	/* PWM internal delay for the GPIF in the FX2 */
	int8_t pwn_delay;
	/* size of the PWM buffer which holds the bit pattern */
	int size_pwm_buf;
	/* input buffer for the ISO-transfer */
	int16_t *in_buffer;
	/* input buffer for single insn */
	int16_t *insn_buffer;
	/* output buffer for single DA outputs */
	int16_t *out_buffer;

	unsigned int high_speed:1;
	unsigned int ai_cmd_running:1;
	unsigned int ai_continous:1;
	unsigned int ao_cmd_running:1;
	unsigned int ao_continous:1;
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
	/* D/A commands */
	int8_t *dac_commands;
	/* commands */
	int8_t *dux_commands;
	struct semaphore sem;
};

/*
 * Stops the data acquision
 * It should be safe to call this function from any context
 */
static int usbduxsub_unlink_inurbs(struct usbdux_private *usbduxsub_tmp)
{
	int i = 0;
	int err = 0;

	if (usbduxsub_tmp && usbduxsub_tmp->urb_in) {
		for (i = 0; i < usbduxsub_tmp->num_in_buffers; i++) {
			if (usbduxsub_tmp->urb_in[i]) {
				/* We wait here until all transfers have been
				 * cancelled. */
				usb_kill_urb(usbduxsub_tmp->urb_in[i]);
			}
		}
	}
	return err;
}

static int usbdux_ai_stop(struct comedi_device *dev, int do_unlink)
{
	struct usbdux_private *devpriv = dev->private;
	int ret = 0;

	if (do_unlink)
		ret = usbduxsub_unlink_inurbs(devpriv);

	devpriv->ai_cmd_running = 0;

	return ret;
}

static int usbdux_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct usbdux_private *devpriv = dev->private;
	int ret = 0;

	/* prevent other CPUs from submitting new commands just now */
	down(&devpriv->sem);
	/* unlink only if the urb really has been submitted */
	ret = usbdux_ai_stop(dev, devpriv->ai_cmd_running);
	up(&devpriv->sem);

	return ret;
}

/* analogue IN - interrupt service routine */
static void usbduxsub_ai_isoc_irq(struct urb *urb)
{
	struct comedi_device *dev = urb->context;
	struct comedi_subdevice *s = dev->read_subdev;
	struct usbdux_private *devpriv = dev->private;
	int i, err, n;

	/* first we test if something unusual has just happened */
	switch (urb->status) {
	case 0:
		/* copy the result in the transfer buffer */
		memcpy(devpriv->in_buffer, urb->transfer_buffer, SIZEINBUF);
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
	if (!devpriv->ai_continous) {
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
	n = s->async->cmd.chanlist_len;
	for (i = 0; i < n; i++) {
		/* transfer data */
		if (CR_RANGE(s->async->cmd.chanlist[i]) <= 1) {
			err = comedi_buf_put(s->async,
			     le16_to_cpu(devpriv->in_buffer[i]) ^ 0x800);
		} else {
			err = comedi_buf_put(s->async,
			     le16_to_cpu(devpriv->in_buffer[i]));
		}
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

static int usbduxsub_unlink_outurbs(struct usbdux_private *usbduxsub_tmp)
{
	int i = 0;
	int err = 0;

	if (usbduxsub_tmp && usbduxsub_tmp->urb_out) {
		for (i = 0; i < usbduxsub_tmp->num_out_buffers; i++) {
			if (usbduxsub_tmp->urb_out[i])
				usb_kill_urb(usbduxsub_tmp->urb_out[i]);
		}
	}
	return err;
}

static int usbdux_ao_stop(struct comedi_device *dev, int do_unlink)
{
	struct usbdux_private *devpriv = dev->private;
	int ret = 0;

	if (do_unlink)
		ret = usbduxsub_unlink_outurbs(devpriv);

	devpriv->ao_cmd_running = 0;

	return ret;
}

static int usbdux_ao_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct usbdux_private *devpriv = dev->private;
	int ret = 0;

	/* prevent other CPUs from submitting a command just now */
	down(&devpriv->sem);
	/* unlink only if it is really running */
	ret = usbdux_ao_stop(dev, devpriv->ao_cmd_running);
	up(&devpriv->sem);

	return ret;
}

static void usbduxsub_ao_isoc_irq(struct urb *urb)
{
	struct comedi_device *dev = urb->context;
	struct comedi_subdevice *s = dev->write_subdev;
	struct usbdux_private *devpriv = dev->private;
	int i, ret;
	int8_t *datap;

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
		if (!devpriv->ao_continous) {
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
		((uint8_t *) (urb->transfer_buffer))[0] =
		    s->async->cmd.chanlist_len;
		for (i = 0; i < s->async->cmd.chanlist_len; i++) {
			short temp;
			if (i >= NUMOUTCHANNELS)
				break;

			/* pointer to the DA */
			datap =
			    (&(((int8_t *) urb->transfer_buffer)[i * 3 + 1]));
			/* get the data from comedi */
			ret = comedi_buf_get(s->async, &temp);
			datap[0] = temp;
			datap[1] = temp >> 8;
			datap[2] = devpriv->dac_commands[i];
			if (ret < 0) {
				dev_err(&urb->dev->dev,
					"comedi: buffer underflow\n");
				s->async->events |= COMEDI_CB_EOA;
				s->async->events |= COMEDI_CB_OVERFLOW;
			}
			/* transmit data to comedi */
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
			if (ret == EL2NSYNC)
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

#define FIRMWARE_MAX_LEN 0x2000

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

	if (size > FIRMWARE_MAX_LEN) {
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
			      USBDUXSUB_FIRMWARE,
			      VENDOR_DIR_OUT,
			      USBDUXSUB_CPUCS, 0x0000,
			      tmp, 1,
			      BULK_TIMEOUT);
	if (ret < 0) {
		dev_err(dev->class_dev, "can not stop firmware\n");
		goto done;
	}

	/* upload the new firmware to the device */
	ret = usb_control_msg(usb, usb_sndctrlpipe(usb, 0),
			      USBDUXSUB_FIRMWARE,
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
			      USBDUXSUB_FIRMWARE,
			      VENDOR_DIR_OUT,
			      USBDUXSUB_CPUCS, 0x0000,
			      tmp, 1,
			      BULK_TIMEOUT);
	if (ret < 0)
		dev_err(dev->class_dev, "can not start firmware\n");

done:
	kfree(tmp);
	kfree(buf);
	return ret;
}

static int usbduxsub_submit_inurbs(struct comedi_device *dev)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	struct urb *urb;
	int ret;
	int i;

	/* Submit all URBs and start the transfer on the bus */
	for (i = 0; i < devpriv->num_in_buffers; i++) {
		urb = devpriv->urb_in[i];

		/* in case of a resubmission after an unlink... */
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

static int usbduxsub_submit_outurbs(struct comedi_device *dev)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	struct urb *urb;
	int ret;
	int i;

	for (i = 0; i < devpriv->num_out_buffers; i++) {
		urb = devpriv->urb_out[i];

		/* in case of a resubmission after an unlink... */
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

	if (cmd->stop_src == TRIG_COUNT) {
		/* any count is allowed */
	} else {
		/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
	}

	if (err)
		return 3;

	return 0;
}

/*
 * creates the ADC command for the MAX1271
 * range is the range value from comedi
 */
static int8_t create_adc_command(unsigned int chan, int range)
{
	int8_t p = (range <= 1);
	int8_t r = ((range % 2) == 0);
	return (chan << 4) | ((p == 1) << 2) | ((r == 1) << 3);
}

/* bulk transfers to usbdux */

#define SENDADCOMMANDS            0
#define SENDDACOMMANDS            1
#define SENDDIOCONFIGCOMMAND      2
#define SENDDIOBITSCOMMAND        3
#define SENDSINGLEAD              4
#define READCOUNTERCOMMAND        5
#define WRITECOUNTERCOMMAND       6
#define SENDPWMON                 7
#define SENDPWMOFF                8

static int send_dux_commands(struct comedi_device *dev, int cmd_type)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	int nsent;

	devpriv->dux_commands[0] = cmd_type;

	return usb_bulk_msg(usb, usb_sndbulkpipe(usb, COMMAND_OUT_EP),
			    devpriv->dux_commands, SIZEOFDUXBUFFER,
			    &nsent, BULK_TIMEOUT);
}

static int receive_dux_commands(struct comedi_device *dev, int command)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	int ret;
	int nrec;
	int i;

	for (i = 0; i < RETRIES; i++) {
		ret = usb_bulk_msg(usb, usb_rcvbulkpipe(usb, COMMAND_IN_EP),
				      devpriv->insn_buffer, SIZEINSNBUF,
				      &nrec, BULK_TIMEOUT);
		if (ret < 0)
			return ret;
		if (le16_to_cpu(devpriv->insn_buffer[0]) == command)
			return ret;
	}
	/* command not received */
	return -EFAULT;
}

static int usbdux_ai_inttrig(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     unsigned int trignum)
{
	struct usbdux_private *devpriv = dev->private;
	int ret = -EINVAL;

	down(&devpriv->sem);

	if (trignum != 0)
		goto ai_trig_exit;

	if (!devpriv->ai_cmd_running) {
		devpriv->ai_cmd_running = 1;
		ret = usbduxsub_submit_inurbs(dev);
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

		if (i >= NUMCHANNELS)
			break;

		devpriv->dux_commands[i + 2] = create_adc_command(chan, range);
	}

	ret = send_dux_commands(dev, SENDADCOMMANDS);
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
		devpriv->ai_continous = 0;
	} else {
		/* continous acquisition */
		devpriv->ai_continous = 1;
		devpriv->ai_sample_count = 0;
	}

	if (cmd->start_src == TRIG_NOW) {
		/* enable this acquisition operation */
		devpriv->ai_cmd_running = 1;
		ret = usbduxsub_submit_inurbs(dev);
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
	ret = send_dux_commands(dev, SENDSINGLEAD);
	if (ret < 0)
		goto ai_read_exit;

	for (i = 0; i < insn->n; i++) {
		ret = receive_dux_commands(dev, SENDSINGLEAD);
		if (ret < 0)
			goto ai_read_exit;

		val = le16_to_cpu(devpriv->insn_buffer[1]);
		if (range <= 1)
			val ^= 0x800;

		data[i] = val;
	}

ai_read_exit:
	up(&devpriv->sem);

	return ret ? ret : insn->n;
}

/************************************/
/* analog out */

static int usbdux_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);
	struct usbdux_private *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);
	for (i = 0; i < insn->n; i++)
		data[i] = this_usbduxsub->out_buffer[chan];

	up(&this_usbduxsub->sem);
	return i;
}

static int usbdux_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int i, err;
	int chan = CR_CHAN(insn->chanspec);
	struct usbdux_private *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);
	if (this_usbduxsub->ao_cmd_running) {
		up(&this_usbduxsub->sem);
		return 0;
	}

	for (i = 0; i < insn->n; i++) {
		/* number of channels: 1 */
		this_usbduxsub->dux_commands[1] = 1;
		/* one 16 bit value */
		*((int16_t *) (this_usbduxsub->dux_commands + 2)) =
		    cpu_to_le16(data[i]);
		this_usbduxsub->out_buffer[chan] = data[i];
		/* channel number */
		this_usbduxsub->dux_commands[4] = (chan << 6);
		err = send_dux_commands(dev, SENDDACOMMANDS);
		if (err < 0) {
			up(&this_usbduxsub->sem);
			return err;
		}
	}
	up(&this_usbduxsub->sem);

	return i;
}

static int usbdux_ao_inttrig(struct comedi_device *dev,
			     struct comedi_subdevice *s, unsigned int trignum)
{
	int ret;
	struct usbdux_private *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);
	if (trignum != 0) {
		up(&this_usbduxsub->sem);
		return -EINVAL;
	}
	if (!(this_usbduxsub->ao_cmd_running)) {
		this_usbduxsub->ao_cmd_running = 1;
		ret = usbduxsub_submit_outurbs(dev);
		if (ret < 0) {
			this_usbduxsub->ao_cmd_running = 0;
			up(&this_usbduxsub->sem);
			return ret;
		}
		s->async->inttrig = NULL;
	}
	up(&this_usbduxsub->sem);
	return 1;
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

	if (cmd->stop_src == TRIG_COUNT) {
		/* any count is allowed */
	} else {
		/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
	}

	if (err)
		return 3;

	return 0;
}

static int usbdux_ao_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int chan, gain;
	int i, ret;
	struct usbdux_private *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);

	/* set current channel of the running acquisition to zero */
	s->async->cur_chan = 0;
	for (i = 0; i < cmd->chanlist_len; ++i) {
		chan = CR_CHAN(cmd->chanlist[i]);
		gain = CR_RANGE(cmd->chanlist[i]);
		if (i >= NUMOUTCHANNELS)
			break;
		this_usbduxsub->dac_commands[i] = (chan << 6);
	}

	/* we count in steps of 1ms (125us) */
	/* 125us mode not used yet */
	if (0) {		/* (this_usbduxsub->high_speed) */
		/* 125us */
		/* timing of the conversion itself: every 125 us */
		this_usbduxsub->ao_timer = cmd->convert_arg / 125000;
	} else {
		/* 1ms */
		/* timing of the scan: we get all channels at once */
		this_usbduxsub->ao_timer = cmd->scan_begin_arg / 1000000;
		if (this_usbduxsub->ao_timer < 1) {
			up(&this_usbduxsub->sem);
			return -EINVAL;
		}
	}
	this_usbduxsub->ao_counter = this_usbduxsub->ao_timer;

	if (cmd->stop_src == TRIG_COUNT) {
		/* not continuous */
		/* counter */
		/* high speed also scans everything at once */
		if (0) {	/* (this_usbduxsub->high_speed) */
			this_usbduxsub->ao_sample_count =
			    (cmd->stop_arg) * (cmd->scan_end_arg);
		} else {
			/* there's no scan as the scan has been */
			/* perf inside the FX2 */
			/* data arrives as one packet */
			this_usbduxsub->ao_sample_count = cmd->stop_arg;
		}
		this_usbduxsub->ao_continous = 0;
	} else {
		/* continous acquisition */
		this_usbduxsub->ao_continous = 1;
		this_usbduxsub->ao_sample_count = 0;
	}

	if (cmd->start_src == TRIG_NOW) {
		/* enable this acquisition operation */
		this_usbduxsub->ao_cmd_running = 1;
		ret = usbduxsub_submit_outurbs(dev);
		if (ret < 0) {
			this_usbduxsub->ao_cmd_running = 0;
			/* fixme: unlink here?? */
			up(&this_usbduxsub->sem);
			return ret;
		}
		s->async->inttrig = NULL;
	} else {
		/* TRIG_INT */
		/* submit the urbs later */
		/* wait for an internal signal */
		s->async->inttrig = usbdux_ao_inttrig;
	}

	up(&this_usbduxsub->sem);
	return 0;
}

static int usbdux_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);

	/* The input or output configuration of each digital line is
	 * configured by a special insn_config instruction.  chanspec
	 * contains the channel to be changed, and data[0] contains the
	 * value COMEDI_INPUT or COMEDI_OUTPUT. */

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= 1 << chan;	/* 1 means Out */
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~(1 << chan);
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		break;
	default:
		return -EINVAL;
		break;
	}
	/* we don't tell the firmware here as it would take 8 frames */
	/* to submit the information. We do it in the insn_bits. */
	return insn->n;
}

static int usbdux_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{

	struct usbdux_private *this_usbduxsub = dev->private;
	int err;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	s->state &= ~data[0];
	s->state |= data[0] & data[1];
	this_usbduxsub->dux_commands[1] = s->io_bits;
	this_usbduxsub->dux_commands[2] = s->state;

	/* This command also tells the firmware to return */
	/* the digital input lines */
	err = send_dux_commands(dev, SENDDIOBITSCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}
	err = receive_dux_commands(dev, SENDDIOBITSCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}

	data[1] = le16_to_cpu(this_usbduxsub->insn_buffer[1]);
	up(&this_usbduxsub->sem);
	return insn->n;
}

/* reads the 4 counters, only two are used just now */
static int usbdux_counter_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct usbdux_private *this_usbduxsub = dev->private;
	int chan = insn->chanspec;
	int err;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);
	err = send_dux_commands(dev, READCOUNTERCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}

	err = receive_dux_commands(dev, READCOUNTERCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}

	data[0] = le16_to_cpu(this_usbduxsub->insn_buffer[chan + 1]);
	up(&this_usbduxsub->sem);
	return 1;
}

static int usbdux_counter_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	struct usbdux_private *this_usbduxsub = dev->private;
	int err;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);
	this_usbduxsub->dux_commands[1] = insn->chanspec;
	*((int16_t *) (this_usbduxsub->dux_commands + 2)) = cpu_to_le16(*data);

	err = send_dux_commands(dev, WRITECOUNTERCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}

	up(&this_usbduxsub->sem);

	return 1;
}

static int usbdux_counter_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	/* nothing to do so far */
	return 2;
}

/***********************************/
/* PWM */

static int usbduxsub_unlink_pwm_urbs(struct usbdux_private *usbduxsub_tmp)
{
	int err = 0;

	if (usbduxsub_tmp && usbduxsub_tmp->urb_pwm) {
		if (usbduxsub_tmp->urb_pwm)
			usb_kill_urb(usbduxsub_tmp->urb_pwm);
	}
	return err;
}

/* This cancels a running acquisition operation
 * in any context.
 */
static int usbdux_pwm_stop(struct usbdux_private *this_usbduxsub, int do_unlink)
{
	int ret = 0;

	if (!this_usbduxsub)
		return -EFAULT;

	if (do_unlink)
		ret = usbduxsub_unlink_pwm_urbs(this_usbduxsub);

	this_usbduxsub->pwm_cmd_running = 0;

	return ret;
}

/* force unlink - is called by comedi */
static int usbdux_pwm_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct usbdux_private *this_usbduxsub = dev->private;
	int res = 0;

	/* unlink only if it is really running */
	res = usbdux_pwm_stop(this_usbduxsub, this_usbduxsub->pwm_cmd_running);

	return send_dux_commands(dev, SENDPWMOFF);
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
			usbdux_pwm_stop(devpriv, 0);

		return;

	default:
		/* a real error */
		if (devpriv->pwm_cmd_running) {
			dev_err(dev->class_dev,
				"Non-zero urb status received in pwm intr context: %d\n",
				urb->status);
			usbdux_pwm_stop(devpriv, 0);
		}
		return;
	}

	/* are we actually running? */
	if (!devpriv->pwm_cmd_running)
		return;

	urb->transfer_buffer_length = devpriv->size_pwm_buf;
	urb->dev = comedi_to_usb_dev(dev);
	urb->status = 0;
	if (devpriv->pwm_cmd_running) {
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret < 0) {
			dev_err(dev->class_dev,
				"pwm urb resubm failed in int-cont. ret=%d",
				ret);
			if (ret == EL2NSYNC)
				dev_err(dev->class_dev,
					"buggy USB host controller or bug in IRQ handling!\n");

			/* don't do an unlink here */
			usbdux_pwm_stop(devpriv, 0);
		}
	}
}

static int usbduxsub_submit_pwm_urbs(struct comedi_device *dev)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	struct urb *urb = devpriv->urb_pwm;

	/* in case of a resubmission after an unlink... */
	usb_fill_bulk_urb(urb, usb, usb_sndbulkpipe(usb, PWM_EP),
			  urb->transfer_buffer,
			  devpriv->size_pwm_buf,
			  usbduxsub_pwm_irq,
			  dev);

	return usb_submit_urb(urb, GFP_ATOMIC);
}

static int usbdux_pwm_period(struct comedi_device *dev,
			     struct comedi_subdevice *s, unsigned int period)
{
	struct usbdux_private *this_usbduxsub = dev->private;
	int fx2delay = 255;

	if (period < MIN_PWM_PERIOD) {
		return -EAGAIN;
	} else {
		fx2delay = period / ((int)(6 * 512 * (1.0 / 0.033))) - 6;
		if (fx2delay > 255)
			return -EAGAIN;
	}
	this_usbduxsub->pwn_delay = fx2delay;
	this_usbduxsub->pwm_period = period;

	return 0;
}

/* is called from insn so there's no need to do all the sanity checks */
static int usbdux_pwm_start(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	int ret, i;
	struct usbdux_private *this_usbduxsub = dev->private;

	if (this_usbduxsub->pwm_cmd_running) {
		/* already running */
		return 0;
	}

	this_usbduxsub->dux_commands[1] = ((int8_t) this_usbduxsub->pwn_delay);
	ret = send_dux_commands(dev, SENDPWMON);
	if (ret < 0)
		return ret;

	/* initialise the buffer */
	for (i = 0; i < this_usbduxsub->size_pwm_buf; i++)
		((char *)(this_usbduxsub->urb_pwm->transfer_buffer))[i] = 0;

	this_usbduxsub->pwm_cmd_running = 1;
	ret = usbduxsub_submit_pwm_urbs(dev);
	if (ret < 0) {
		this_usbduxsub->pwm_cmd_running = 0;
		return ret;
	}
	return 0;
}

/* generates the bit pattern for PWM with the optional sign bit */
static int usbdux_pwm_pattern(struct comedi_device *dev,
			      struct comedi_subdevice *s, int channel,
			      unsigned int value, unsigned int sign)
{
	struct usbdux_private *this_usbduxsub = dev->private;
	int i, szbuf;
	char *p_buf;
	char pwm_mask;
	char sgn_mask;
	char c;

	if (!this_usbduxsub)
		return -EFAULT;

	/* this is the DIO bit which carries the PWM data */
	pwm_mask = (1 << channel);
	/* this is the DIO bit which carries the optional direction bit */
	sgn_mask = (16 << channel);
	/* this is the buffer which will be filled with the with bit */
	/* pattern for one period */
	szbuf = this_usbduxsub->size_pwm_buf;
	p_buf = (char *)(this_usbduxsub->urb_pwm->transfer_buffer);
	for (i = 0; i < szbuf; i++) {
		c = *p_buf;
		/* reset bits */
		c = c & (~pwm_mask);
		/* set the bit as long as the index is lower than the value */
		if (i < value)
			c = c | pwm_mask;
		/* set the optional sign bit for a relay */
		if (!sign) {
			/* positive value */
			c = c & (~sgn_mask);
		} else {
			/* negative value */
			c = c | sgn_mask;
		}
		*(p_buf++) = c;
	}
	return 1;
}

static int usbdux_pwm_write(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct usbdux_private *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	if ((insn->n) != 1) {
		/*
		 * doesn't make sense to have more than one value here because
		 * it would just overwrite the PWM buffer a couple of times
		 */
		return -EINVAL;
	}

	/*
	 * the sign is set via a special INSN only, this gives us 8 bits for
	 * normal operation
	 * relay sign 0 by default
	 */
	return usbdux_pwm_pattern(dev, s, CR_CHAN(insn->chanspec), data[0], 0);
}

static int usbdux_pwm_read(struct comedi_device *x1,
			   struct comedi_subdevice *x2, struct comedi_insn *x3,
			   unsigned int *x4)
{
	/* not needed */
	return -EINVAL;
};

/* switches on/off PWM */
static int usbdux_pwm_config(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	struct usbdux_private *this_usbduxsub = dev->private;
	switch (data[0]) {
	case INSN_CONFIG_ARM:
		/* switch it on */
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
		/*
		 * to check if the USB transmission has failed or in case PWM
		 * was limited to n cycles to check if it has terminated
		 */
		data[1] = this_usbduxsub->pwm_cmd_running;
		return 0;
	case INSN_CONFIG_PWM_SET_PERIOD:
		return usbdux_pwm_period(dev, s, data[1]);
	case INSN_CONFIG_PWM_GET_PERIOD:
		data[1] = this_usbduxsub->pwm_period;
		return 0;
	case INSN_CONFIG_PWM_SET_H_BRIDGE:
		/* value in the first byte and the sign in the second for a
		   relay */
		return usbdux_pwm_pattern(dev, s,
					  /* the channel number */
					  CR_CHAN(insn->chanspec),
					  /* actual PWM data */
					  data[1],
					  /* just a sign */
					  (data[2] != 0));
	case INSN_CONFIG_PWM_GET_H_BRIDGE:
		/* values are not kept in this driver, nothing to return here */
		return -EINVAL;
	}
	return -EINVAL;
}

/* end of PWM */
/*****************************************************************/

static int usbdux_alloc_usb_buffers(struct comedi_device *dev)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbdux_private *devpriv = dev->private;
	struct urb *urb;
	int i;

	/* create space for the commands of the DA converter */
	devpriv->dac_commands = kzalloc(NUMOUTCHANNELS, GFP_KERNEL);
	if (!devpriv->dac_commands)
		return -ENOMEM;

	/* create space for the commands going to the usb device */
	devpriv->dux_commands = kzalloc(SIZEOFDUXBUFFER, GFP_KERNEL);
	if (!devpriv->dux_commands)
		return -ENOMEM;

	/* create space for the in buffer and set it to zero */
	devpriv->in_buffer = kzalloc(SIZEINBUF, GFP_KERNEL);
	if (!devpriv->in_buffer)
		return -ENOMEM;

	/* create space of the instruction buffer */
	devpriv->insn_buffer = kzalloc(SIZEINSNBUF, GFP_KERNEL);
	if (!devpriv->insn_buffer)
		return -ENOMEM;

	/* create space for the outbuffer */
	devpriv->out_buffer = kzalloc(SIZEOUTBUF, GFP_KERNEL);
	if (!devpriv->out_buffer)
		return -ENOMEM;

	/* in urbs */
	devpriv->urb_in = kcalloc(devpriv->num_in_buffers, sizeof(*urb),
				  GFP_KERNEL);
	if (!devpriv->urb_in)
		return -ENOMEM;

	for (i = 0; i < devpriv->num_in_buffers; i++) {
		/* one frame: 1ms */
		urb = usb_alloc_urb(1, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		devpriv->urb_in[i] = urb;

		urb->dev = usb;
		/* will be filled later with a pointer to the comedi-device */
		/* and ONLY then the urb should be submitted */
		urb->context = NULL;
		urb->pipe = usb_rcvisocpipe(usb, ISOINEP);
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

	/* out urbs */
	devpriv->urb_out = kcalloc(devpriv->num_out_buffers, sizeof(*urb),
				   GFP_KERNEL);
	if (!devpriv->urb_out)
		return -ENOMEM;

	for (i = 0; i < devpriv->num_out_buffers; i++) {
		/* one frame: 1ms */
		urb = usb_alloc_urb(1, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		devpriv->urb_out[i] = urb;

		urb->dev = usb;
		/* will be filled later with a pointer to the comedi-device */
		/* and ONLY then the urb should be submitted */
		urb->context = NULL;
		urb->pipe = usb_sndisocpipe(usb, ISOOUTEP);
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
	if (devpriv->size_pwm_buf) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		devpriv->urb_pwm = urb;

		/* max bulk ep size in high speed */
		urb->transfer_buffer = kzalloc(devpriv->size_pwm_buf,
					       GFP_KERNEL);
		if (!urb->transfer_buffer)
			return -ENOMEM;
	}

	return 0;
}

static void usbdux_free_usb_buffers(struct usbdux_private *devpriv)
{
	struct urb *urb;
	int i;

	urb = devpriv->urb_pwm;
	if (urb) {
		kfree(urb->transfer_buffer);
		usb_kill_urb(urb);
		usb_free_urb(urb);
	}
	if (devpriv->urb_out) {
		for (i = 0; i < devpriv->num_out_buffers; i++) {
			urb = devpriv->urb_out[i];
			if (urb) {
				kfree(urb->transfer_buffer);
				usb_kill_urb(urb);
				usb_free_urb(urb);
			}
		}
		kfree(devpriv->urb_out);
	}
	if (devpriv->urb_in) {
		for (i = 0; i < devpriv->num_in_buffers; i++) {
			urb = devpriv->urb_in[i];
			if (urb) {
				kfree(urb->transfer_buffer);
				usb_kill_urb(urb);
				usb_free_urb(urb);
			}
		}
		kfree(devpriv->urb_in);
	}
	kfree(devpriv->out_buffer);
	kfree(devpriv->insn_buffer);
	kfree(devpriv->in_buffer);
	kfree(devpriv->dux_commands);
	kfree(devpriv->dac_commands);
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
		devpriv->num_in_buffers = NUMOFINBUFFERSHIGH;
		devpriv->num_out_buffers = NUMOFOUTBUFFERSHIGH;
		devpriv->size_pwm_buf = 512;
	} else {
		devpriv->num_in_buffers = NUMOFINBUFFERSFULL;
		devpriv->num_out_buffers = NUMOFOUTBUFFERSFULL;
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

	ret = comedi_load_firmware(dev, &usb->dev, FIRMWARE,
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
	s->len_chanlist	= 4;
	s->range_table	= &range_usbdux_ao_range;
	s->do_cmdtest	= usbdux_ao_cmdtest;
	s->do_cmd	= usbdux_ao_cmd;
	s->cancel	= usbdux_ao_cancel;
	s->insn_read	= usbdux_ao_insn_read;
	s->insn_write	= usbdux_ao_insn_write;

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
		s->maxdata	= devpriv->size_pwm_buf;
		s->insn_write	= usbdux_pwm_write;
		s->insn_read	= usbdux_pwm_read;
		s->insn_config	= usbdux_pwm_config;

		usbdux_pwm_period(dev, s, PWM_DEFAULT_PERIOD);
	}

	return 0;
}

static void usbdux_detach(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usbdux_private *devpriv = dev->private;

	if (devpriv) {
		down(&devpriv->sem);

		usb_set_intfdata(intf, NULL);

		if (devpriv->pwm_cmd_running)
			usbduxsub_unlink_pwm_urbs(devpriv);
		if (devpriv->ao_cmd_running)
			usbduxsub_unlink_outurbs(devpriv);
		if (devpriv->ai_cmd_running)
			usbduxsub_unlink_inurbs(devpriv);

		usbdux_free_usb_buffers(devpriv);

		up(&devpriv->sem);
	}
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
MODULE_FIRMWARE(FIRMWARE);
