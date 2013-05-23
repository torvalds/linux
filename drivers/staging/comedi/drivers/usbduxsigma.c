/*
   comedi/drivers/usbdux.c
   Copyright (C) 2011 Bernd Porr, Bernd.Porr@f2s.com

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
Driver: usbduxsigma
Description: University of Stirling USB DAQ & INCITE Technology Limited
Devices: [ITL] USB-DUX (usbduxsigma.o)
Author: Bernd Porr <BerndPorr@f2s.com>
Updated: 8 Nov 2011
Status: testing
*/
/*
 * I must give credit here to Chris Baugher who
 * wrote the driver for AT-MIO-16d. I used some parts of this
 * driver. I also must give credits to David Brownell
 * who supported me with the USB development.
 *
 * Note: the raw data from the A/D converter is 24 bit big endian
 * anything else is little endian to/from the dux board
 *
 *
 * Revision history:
 *   0.1: initial version
 *   0.2: all basic functions implemented, digital I/O only for one port
 *   0.3: proper vendor ID and driver name
 *   0.4: fixed D/A voltage range
 *   0.5: various bug fixes, health check at startup
 *   0.6: corrected wrong input range
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/fcntl.h>
#include <linux/compiler.h>

#include "comedi_fc.h"
#include "../comedidev.h"

/* timeout for the USB-transfer in ms*/
#define BULK_TIMEOUT 1000

/* constants for "firmware" upload and download */
#define FIRMWARE		"usbduxsigma_firmware.bin"
#define FIRMWARE_MAX_LEN	0x4000
#define USBDUXSUB_FIRMWARE	0xa0
#define VENDOR_DIR_IN		0xc0
#define VENDOR_DIR_OUT		0x40

/* internal addresses of the 8051 processor */
#define USBDUXSUB_CPUCS 0xE600

/*
 * the minor device number, major is 180 only for debugging purposes and to
 * upload special firmware (programming the eeprom etc) which is not
 * compatible with the comedi framwork
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

/* Number of channels (16 AD and offset)*/
#define NUMCHANNELS 16

#define USBDUXSIGMA_NUM_AO_CHAN		4

/* Size of one A/D value */
#define SIZEADIN          ((sizeof(int32_t)))

/*
 * Size of the async input-buffer IN BYTES, the DIO state is transmitted
 * as the first byte.
 */
#define SIZEINBUF         (((NUMCHANNELS+1)*SIZEADIN))

/* 16 bytes. */
#define SIZEINSNBUF       16

/* Number of DA channels */
#define NUMOUTCHANNELS    8

/* size of one value for the D/A converter: channel and value */
#define SIZEDAOUT          ((sizeof(uint8_t)+sizeof(int16_t)))

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

/* bulk transfer commands to usbduxsigma */
#define USBBUXSIGMA_AD_CMD		0
#define USBDUXSIGMA_DA_CMD		1
#define USBDUXSIGMA_DIO_CFG_CMD		2
#define USBDUXSIGMA_DIO_BITS_CMD	3
#define USBDUXSIGMA_SINGLE_AD_CMD	4
#define USBDUXSIGMA_PWM_ON_CMD		7
#define USBDUXSIGMA_PWM_OFF_CMD		8

/**************************************************/
/* comedi constants */
static const struct comedi_lrange range_usbdux_ai_range = { 1, {
								BIP_RANGE
								(2.65/2.0)
								}
};

struct usbduxsigma_private {
	/* actual number of in-buffers */
	int numOfInBuffers;
	/* actual number of out-buffers */
	int numOfOutBuffers;
	/* ISO-transfer handling: buffers */
	struct urb **urbIn;
	struct urb **urbOut;
	/* pwm-transfer handling */
	struct urb *urbPwm;
	/* PWM period */
	unsigned int pwmPeriod;
	/* PWM internal delay for the GPIF in the FX2 */
	uint8_t pwmDelay;
	/* size of the PWM buffer which holds the bit pattern */
	int sizePwmBuf;
	/* input buffer for the ISO-transfer */
	int32_t *inBuffer;
	/* input buffer for single insn */
	int8_t *insnBuffer;

	unsigned int ao_readback[USBDUXSIGMA_NUM_AO_CHAN];

	unsigned high_speed:1;
	unsigned ai_cmd_running:1;
	unsigned ai_continuous:1;
	unsigned ao_cmd_running:1;
	unsigned ao_continuous:1;
	unsigned pwm_cmd_running:1;

	/* number of samples to acquire */
	int ai_sample_count;
	int ao_sample_count;
	/* time between samples in units of the timer */
	unsigned int ai_timer;
	unsigned int ao_timer;
	/* counter between acquisitions */
	unsigned int ai_counter;
	unsigned int ao_counter;
	/* interval in frames/uframes */
	unsigned int ai_interval;
	/* D/A commands */
	uint8_t *dac_commands;
	/* commands */
	uint8_t *dux_commands;
	struct semaphore sem;
};

static void usbdux_ai_stop(struct usbduxsigma_private *devpriv, int do_unlink)
{
	if (do_unlink) {
		int i;

		for (i = 0; i < devpriv->numOfInBuffers; i++) {
			if (devpriv->urbIn[i])
				usb_kill_urb(devpriv->urbIn[i]);
		}
	}

	devpriv->ai_cmd_running = 0;
}

static int usbduxsigma_ai_cancel(struct comedi_device *dev,
				 struct comedi_subdevice *s)
{
	struct usbduxsigma_private *devpriv = dev->private;

	down(&devpriv->sem);
	/* unlink only if it is really running */
	usbdux_ai_stop(devpriv, devpriv->ai_cmd_running);
	up(&devpriv->sem);

	return 0;
}

static void usbduxsigma_ai_urb_complete(struct urb *urb)
{
	struct comedi_device *dev = urb->context;
	struct usbduxsigma_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int dio_state;
	int32_t val;
	int ret;
	int i;

	/* first we test if something unusual has just happened */
	switch (urb->status) {
	case 0:
		/* copy the result in the transfer buffer */
		memcpy(devpriv->inBuffer, urb->transfer_buffer, SIZEINBUF);
		break;
	case -EILSEQ:
		/*
		 * error in the ISOchronous data
		 * we don't copy the data into the transfer buffer
		 * and recycle the last data byte
		 */
		dev_dbg(dev->class_dev,"CRC error in ISO IN stream\n");

		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -ECONNABORTED:
		/* happens after an unlink command */
		if (devpriv->ai_cmd_running) {
			usbdux_ai_stop(devpriv, 0);	/* w/o unlink */
			/* we are still running a command, tell comedi */
			s->async->events |= (COMEDI_CB_EOA | COMEDI_CB_ERROR);
			comedi_event(dev, s);
		}
		return;

	default:
		/*
		 * a real error on the bus
		 * pass error to comedi if we are really running a command
		 */
		if (devpriv->ai_cmd_running) {
			dev_err(dev->class_dev,
				"%s: non-zero urb status (%d)\n",
				__func__, urb->status);
			usbdux_ai_stop(devpriv, 0);	/* w/o unlink */
			s->async->events |= (COMEDI_CB_EOA | COMEDI_CB_ERROR);
			comedi_event(dev, s);
		}
		return;
	}

	if (unlikely(!devpriv->ai_cmd_running))
		return;

	urb->dev = comedi_to_usb_dev(dev);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(ret < 0)) {
		dev_err(dev->class_dev, "%s: urb resubmit failed (%d)\n",
			__func__, ret);
		if (ret == -EL2NSYNC)
			dev_err(dev->class_dev,
				"buggy USB host controller or bug in IRQ handler\n");
		usbdux_ai_stop(devpriv, 0);	/* w/o unlink */
		s->async->events |= (COMEDI_CB_EOA | COMEDI_CB_ERROR);
		comedi_event(dev, s);
		return;
	}

	/* get the state of the dio pins to allow external trigger */
	dio_state = be32_to_cpu(devpriv->inBuffer[0]);

	devpriv->ai_counter--;
	if (likely(devpriv->ai_counter > 0))
		return;

	/* timer zero, transfer measurements to comedi */
	devpriv->ai_counter = devpriv->ai_timer;

	if (!devpriv->ai_continuous) {
		/* not continuous, fixed number of samples */
		devpriv->ai_sample_count--;
		if (devpriv->ai_sample_count < 0) {
			usbdux_ai_stop(devpriv, 0);	/* w/o unlink */
			/* acquistion is over, tell comedi */
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(dev, s);
			return;
		}
	}

	/* get the data from the USB bus and hand it over to comedi */
	for (i = 0; i < s->async->cmd.chanlist_len; i++) {
		/* transfer data, note first byte is the DIO state */
		val = be32_to_cpu(devpriv->inBuffer[i+1]);
		val &= 0x00ffffff;	/* strip status byte */
		val ^= 0x00800000;	/* convert to unsigned */

		ret = cfc_write_array_to_buffer(s, &val, sizeof(uint32_t));
		if (unlikely(ret == 0)) {
			/* buffer overflow */
			usbdux_ai_stop(devpriv, 0);	/* w/o unlink */
			return;
		}
	}
	/* tell comedi that data is there */
	s->async->events |= (COMEDI_CB_BLOCK | COMEDI_CB_EOS);
	comedi_event(dev, s);
}

static void usbdux_ao_stop(struct usbduxsigma_private *devpriv, int do_unlink)
{
	if (do_unlink) {
		int i;

		for (i = 0; i < devpriv->numOfOutBuffers; i++) {
			if (devpriv->urbOut[i])
				usb_kill_urb(devpriv->urbOut[i]);
		}
	}

	devpriv->ao_cmd_running = 0;
}

static int usbduxsigma_ao_cancel(struct comedi_device *dev,
				 struct comedi_subdevice *s)
{
	struct usbduxsigma_private *devpriv = dev->private;

	down(&devpriv->sem);
	/* unlink only if it is really running */
	usbdux_ao_stop(devpriv, devpriv->ao_cmd_running);
	up(&devpriv->sem);

	return 0;
}

static void usbduxsigma_ao_urb_complete(struct urb *urb)
{
	struct comedi_device *dev = urb->context;
	struct usbduxsigma_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->write_subdev;
	uint8_t *datap;
	int len;
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
		/* happens after an unlink command */
		if (devpriv->ao_cmd_running) {
			usbdux_ao_stop(devpriv, 0);	/* w/o unlink */
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(dev, s);
		}
		return;

	default:
		/* a real error */
		if (devpriv->ao_cmd_running) {
			dev_err(dev->class_dev,
				"%s: non-zero urb status (%d)\n",
				__func__, urb->status);
			usbdux_ao_stop(devpriv, 0);	/* w/o unlink */
			s->async->events |= (COMEDI_CB_ERROR | COMEDI_CB_EOA);
			comedi_event(dev, s);
		}
		return;
	}

	if (!devpriv->ao_cmd_running)
		return;

	devpriv->ao_counter--;
	if ((int)devpriv->ao_counter <= 0) {
		/* timer zero, transfer from comedi */
		devpriv->ao_counter = devpriv->ao_timer;

		if (!devpriv->ao_continuous) {
			/* not continuous, fixed number of samples */
			devpriv->ao_sample_count--;
			if (devpriv->ao_sample_count < 0) {
				usbdux_ao_stop(devpriv, 0);	/* w/o unlink */
				/* acquistion is over, tell comedi */
				s->async->events |= COMEDI_CB_EOA;
				comedi_event(dev, s);
				return;
			}
		}

		/* transmit data to the USB bus */
		datap = urb->transfer_buffer;
		len = s->async->cmd.chanlist_len;
		*datap++ = len;
		for (i = 0; i < len; i++) {
			unsigned int chan = devpriv->dac_commands[i];
			short val;

			ret = comedi_buf_get(s->async, &val);
			if (ret < 0) {
				dev_err(dev->class_dev, "buffer underflow\n");
				s->async->events |= (COMEDI_CB_EOA |
						     COMEDI_CB_OVERFLOW);
			}
			*datap++ = val;
			*datap++ = chan;
			devpriv->ao_readback[chan] = val;

			s->async->events |= COMEDI_CB_BLOCK;
			comedi_event(dev, s);
		}
	}

	urb->transfer_buffer_length = SIZEOUTBUF;
	urb->dev = comedi_to_usb_dev(dev);
	urb->status = 0;
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
			"%s: urb resubmit failed (%d)\n",
			__func__, ret);
		if (ret == EL2NSYNC)
			dev_err(dev->class_dev,
				"buggy USB host controller or bug in IRQ handler\n");
		usbdux_ao_stop(devpriv, 0);	/* w/o unlink */
		s->async->events |= (COMEDI_CB_EOA | COMEDI_CB_ERROR);
		comedi_event(dev, s);
	}
}

static int usbduxsigma_submit_urbs(struct comedi_device *dev,
				   struct urb **urbs, int num_urbs,
				   int input_urb)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbduxsigma_private *devpriv = dev->private;
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

static int chanToInterval(int nChannels)
{
	if (nChannels <= 2)
		/* 4kHz */
		return 2;
	if (nChannels <= 8)
		/* 2kHz */
		return 4;
	/* 1kHz */
	return 8;
}

static int usbduxsigma_ai_cmdtest(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_cmd *cmd)
{
	struct usbduxsigma_private *devpriv = dev->private;
	int high_speed = devpriv->high_speed;
	int interval = chanToInterval(cmd->chanlist_len);
	int err = 0;

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
		unsigned int tmp;

		if (high_speed) {
			/*
			 * In high speed mode microframes are possible.
			 * However, during one microframe we can roughly
			 * sample two channels. Thus, the more channels
			 * are in the channel list the more time we need.
			 */
			err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						(1000000 / 8 * interval));

			tmp = (cmd->scan_begin_arg / 125000) * 125000;
		} else {
			/* full speed */
			/* 1kHz scans every USB frame */
			err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
							 1000000);

			tmp = (cmd->scan_begin_arg / 1000000) * 1000000;
		}
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, tmp);
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

	/* Step 4: fix up any arguments */

	if (high_speed) {
		/*
		 * every 2 channels get a time window of 125us. Thus, if we
		 * sample all 16 channels we need 1ms. If we sample only one
		 * channel we need only 125us
		 */
		devpriv->ai_interval = interval;
		devpriv->ai_timer = cmd->scan_begin_arg / (125000 * interval);
	} else {
		/* interval always 1ms */
		devpriv->ai_interval = 1;
		devpriv->ai_timer = cmd->scan_begin_arg / 1000000;
	}
	if (devpriv->ai_timer < 1)
		err |= -EINVAL;

	if (cmd->stop_src == TRIG_COUNT) {
		/* data arrives as one packet */
		devpriv->ai_sample_count = cmd->stop_arg;
		devpriv->ai_continuous = 0;
	} else {
		/* continuous acquisition */
		devpriv->ai_continuous = 1;
		devpriv->ai_sample_count = 0;
	}

	if (err)
		return 4;

	return 0;
}

/*
 * creates the ADC command for the MAX1271
 * range is the range value from comedi
 */
static void create_adc_command(unsigned int chan,
			       uint8_t *muxsg0,
			       uint8_t *muxsg1)
{
	if (chan < 8)
		(*muxsg0) = (*muxsg0) | (1 << chan);
	else if (chan < 16)
		(*muxsg1) = (*muxsg1) | (1 << (chan-8));
}

static int usbbuxsigma_send_cmd(struct comedi_device *dev, int cmd_type)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbduxsigma_private *devpriv = dev->private;
	int nsent;

	devpriv->dux_commands[0] = cmd_type;

	return usb_bulk_msg(usb, usb_sndbulkpipe(usb, COMMAND_OUT_EP),
			    devpriv->dux_commands, SIZEOFDUXBUFFER,
			    &nsent, BULK_TIMEOUT);
}

static int usbduxsigma_receive_cmd(struct comedi_device *dev, int command)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbduxsigma_private *devpriv = dev->private;
	int nrec;
	int ret;
	int i;

	for (i = 0; i < RETRIES; i++) {
		ret = usb_bulk_msg(usb, usb_rcvbulkpipe(usb, COMMAND_IN_EP),
				   devpriv->insnBuffer, SIZEINSNBUF,
				   &nrec, BULK_TIMEOUT);
		if (ret < 0)
			return ret;

		if (devpriv->insnBuffer[0] == command)
			return 0;
	}
	/*
	 * This is only reached if the data has been requested a
	 * couple of times and the command was not received.
	 */
	return -EFAULT;
}

static int usbduxsigma_ai_inttrig(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned int trignum)
{
	struct usbduxsigma_private *devpriv= dev->private;
	int ret;

	if (trignum != 0)
		return -EINVAL;

	down(&devpriv->sem);
	if (!devpriv->ai_cmd_running) {
		ret = usbduxsigma_submit_urbs(dev, devpriv->urbIn,
					      devpriv->numOfInBuffers, 1);
		if (ret < 0) {
			up(&devpriv->sem);
			return ret;
		}
		devpriv->ai_cmd_running = 1;
		s->async->inttrig = NULL;
	}
	up(&devpriv->sem);

	return 1;
}

static int usbduxsigma_ai_cmd(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct usbduxsigma_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int len = cmd->chanlist_len;
	uint8_t muxsg0 = 0;
	uint8_t muxsg1 = 0;
	uint8_t sysred = 0;
	int ret;
	int i;

	down(&devpriv->sem);

	/* set current channel of the running acquisition to zero */
	s->async->cur_chan = 0;
	for (i = 0; i < len; i++) {
		unsigned int chan  = CR_CHAN(cmd->chanlist[i]);

		create_adc_command(chan, &muxsg0, &muxsg1);
	}

	devpriv->dux_commands[1] = len;  /* num channels per time step */
	devpriv->dux_commands[2] = 0x12; /* CONFIG0 */
	devpriv->dux_commands[3] = 0x03; /* CONFIG1: 23kHz sample, delay 0us */
	devpriv->dux_commands[4] = 0x00; /* CONFIG3: diff. channels off */
	devpriv->dux_commands[5] = muxsg0;
	devpriv->dux_commands[6] = muxsg1;
	devpriv->dux_commands[7] = sysred;

	ret = usbbuxsigma_send_cmd(dev, USBBUXSIGMA_AD_CMD);
	if (ret < 0) {
		up(&devpriv->sem);
		return ret;
	}

	devpriv->ai_counter = devpriv->ai_timer;

	if (cmd->start_src == TRIG_NOW) {
		/* enable this acquisition operation */
		ret = usbduxsigma_submit_urbs(dev, devpriv->urbIn,
					      devpriv->numOfInBuffers, 1);
		if (ret < 0) {
			up(&devpriv->sem);
			return ret;
		}
		s->async->inttrig = NULL;
		devpriv->ai_cmd_running = 1;
	} else {	/* TRIG_INT */
		/* wait for an internal signal and submit the urbs later */
		s->async->inttrig = usbduxsigma_ai_inttrig;
	}

	up(&devpriv->sem);

	return 0;
}

static int usbduxsigma_ai_insn_read(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct usbduxsigma_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	uint8_t muxsg0 = 0;
	uint8_t muxsg1 = 0;
	uint8_t sysred = 0;
	int ret;
	int i;

	down(&devpriv->sem);
	if (devpriv->ai_cmd_running) {
		up(&devpriv->sem);
		return -EBUSY;
	}

	create_adc_command(chan, &muxsg0, &muxsg1);

	/* Mode 0 is used to get a single conversion on demand */
	devpriv->dux_commands[1] = 0x16; /* CONFIG0: chopper on */
	devpriv->dux_commands[2] = 0x80; /* CONFIG1: 2kHz sampling rate */
	devpriv->dux_commands[3] = 0x00; /* CONFIG3: diff. channels off */
	devpriv->dux_commands[4] = muxsg0;
	devpriv->dux_commands[5] = muxsg1;
	devpriv->dux_commands[6] = sysred;

	/* adc commands */
	ret = usbbuxsigma_send_cmd(dev, USBDUXSIGMA_SINGLE_AD_CMD);
	if (ret < 0) {
		up(&devpriv->sem);
		return ret;
	}

	for (i = 0; i < insn->n; i++) {
		int32_t val;

		ret = usbduxsigma_receive_cmd(dev, USBDUXSIGMA_SINGLE_AD_CMD);
		if (ret < 0) {
			up(&devpriv->sem);
			return ret;
		}

		/* 32 bits big endian from the A/D converter */
		val = be32_to_cpu(*((int32_t *)((devpriv->insnBuffer) + 1)));
		val &= 0x00ffffff;	/* strip status byte */
		val ^= 0x00800000;	/* convert to unsigned */

		data[i] = val;
	}
	up(&devpriv->sem);

	return insn->n;
}

static int usbduxsigma_ao_insn_read(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct usbduxsigma_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	down(&devpriv->sem);
	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];
	up(&devpriv->sem);

	return insn->n;
}

static int usbduxsigma_ao_insn_write(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct usbduxsigma_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int ret;
	int i;

	down(&devpriv->sem);
	if (devpriv->ao_cmd_running) {
		up(&devpriv->sem);
		return -EBUSY;
	}

	for (i = 0; i < insn->n; i++) {
		devpriv->dux_commands[1] = 1;		/* num channels */
		devpriv->dux_commands[2] = data[i];	/* value */
		devpriv->dux_commands[3] = chan;	/* channel number */
		ret = usbbuxsigma_send_cmd(dev, USBDUXSIGMA_DA_CMD);
		if (ret < 0) {
			up(&devpriv->sem);
			return ret;
		}
		devpriv->ao_readback[chan] = data[i];
	}
	up(&devpriv->sem);

	return insn->n;
}

static int usbduxsigma_ao_inttrig(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned int trignum)
{
	struct usbduxsigma_private *devpriv = dev->private;
	int ret;

	if (trignum != 0)
		return -EINVAL;

	down(&devpriv->sem);
	if (!devpriv->ao_cmd_running) {
		ret = usbduxsigma_submit_urbs(dev, devpriv->urbOut,
					      devpriv->numOfOutBuffers, 0);
		if (ret < 0) {
			up(&devpriv->sem);
			return ret;
		}
		devpriv->ao_cmd_running = 1;
		s->async->inttrig = NULL;
	}
	up(&devpriv->sem);

	return 1;
}

static int usbduxsigma_ao_cmdtest(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_cmd *cmd)
{
	struct usbduxsigma_private *devpriv = dev->private;
	int err = 0;
	int high_speed;
	unsigned int flags;

	/* high speed conversions are not used yet */
	high_speed = 0; 	/* (devpriv->high_speed) */

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);

	if (high_speed) {
		/*
		 * start immediately a new scan
		 * the sampling rate is set by the coversion rate
		 */
		flags = TRIG_FOLLOW;
	} else {
		/* start a new scan (output at once) with a timer */
		flags = TRIG_TIMER;
	}
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, flags);

	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err) {
		up(&devpriv->sem);
		return 1;
	}

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

	/* Step 4: fix up any arguments */

	/* we count in timer steps */
	if (high_speed) {
		/* timing of the conversion itself: every 125 us */
		devpriv->ao_timer = cmd->convert_arg / 125000;
	} else {
		/*
		 * timing of the scan: every 1ms
		 * we get all channels at once
		 */
		devpriv->ao_timer = cmd->scan_begin_arg / 1000000;
	}
	if (devpriv->ao_timer < 1)
		err |= -EINVAL;

	if (cmd->stop_src == TRIG_COUNT) {
		/* not continuous, use counter */
		if (high_speed) {
			/* high speed also scans everything at once */
			devpriv->ao_sample_count = cmd->stop_arg *
						   cmd->scan_end_arg;
		} else {
			/*
			 * There's no scan as the scan has been
			 * handled inside the FX2. Data arrives as
			 * one packet.
			 */
			devpriv->ao_sample_count = cmd->stop_arg;
		}
		devpriv->ao_continuous = 0;
	} else {
		/* continuous acquisition */
		devpriv->ao_continuous = 1;
		devpriv->ao_sample_count = 0;
	}

	if (err)
		return 4;

	return 0;
}

static int usbduxsigma_ao_cmd(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct usbduxsigma_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int ret;
	int i;

	down(&devpriv->sem);

	/* set current channel of the running acquisition to zero */
	s->async->cur_chan = 0;
	for (i = 0; i < cmd->chanlist_len; ++i)
		devpriv->dac_commands[i] = CR_CHAN(cmd->chanlist[i]);

	devpriv->ao_counter = devpriv->ao_timer;

	if (cmd->start_src == TRIG_NOW) {
		/* enable this acquisition operation */
		ret = usbduxsigma_submit_urbs(dev, devpriv->urbOut,
					      devpriv->numOfOutBuffers, 0);
		if (ret < 0) {
			up(&devpriv->sem);
			return ret;
		}
		s->async->inttrig = NULL;
		devpriv->ao_cmd_running = 1;
	} else {	/* TRIG_INT */
		/* wait for an internal signal and submit the urbs later */
		s->async->inttrig = usbduxsigma_ao_inttrig;
	}

	up(&devpriv->sem);

	return 0;
}

static int usbduxsigma_dio_insn_config(struct comedi_device *dev,
				       struct comedi_subdevice *s,
				       struct comedi_insn *insn,
				       unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask = 1 << chan;

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= mask;
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~mask;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & mask) ? COMEDI_OUTPUT : COMEDI_INPUT;
		break;
	default:
		return -EINVAL;
		break;
	}

	/*
	 * We don't tell the firmware here as it would take 8 frames
	 * to submit the information. We do it in the (*insn_bits).
	 */
	return insn->n;
}

static int usbduxsigma_dio_insn_bits(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct usbduxsigma_private *devpriv = dev->private;
	unsigned int mask = data[0];
	unsigned int bits = data[1];
	int ret;

	down(&devpriv->sem);

	s->state &= ~mask;
	s->state |= (bits & mask);

	devpriv->dux_commands[1] = s->io_bits & 0xff;
	devpriv->dux_commands[4] = s->state & 0xff;
	devpriv->dux_commands[2] = (s->io_bits >> 8) & 0xff;
	devpriv->dux_commands[5] = (s->state >> 8) & 0xff;
	devpriv->dux_commands[3] = (s->io_bits >> 16) & 0xff;
	devpriv->dux_commands[6] = (s->state >> 16) & 0xff;

	ret = usbbuxsigma_send_cmd(dev, USBDUXSIGMA_DIO_BITS_CMD);
	if (ret < 0)
		goto done;
	ret = usbduxsigma_receive_cmd(dev, USBDUXSIGMA_DIO_BITS_CMD);
	if (ret < 0)
		goto done;

	s->state = devpriv->insnBuffer[1] |
		   (devpriv->insnBuffer[2] << 8) |
		   (devpriv->insnBuffer[3] << 16);

	data[1] = s->state;
	ret = insn->n;

done:
	up(&devpriv->sem);

	return ret;
}

static void usbduxsigma_pwm_stop(struct comedi_device *dev, int do_unlink)
{
	struct usbduxsigma_private *devpriv = dev->private;

	if (do_unlink) {
		if (devpriv->urbPwm)
			usb_kill_urb(devpriv->urbPwm);
	}

	devpriv->pwm_cmd_running = 0;
}

static int usbduxsigma_pwm_cancel(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	struct usbduxsigma_private *devpriv = dev->private;

	/* unlink only if it is really running */
	usbduxsigma_pwm_stop(dev, devpriv->pwm_cmd_running);

	return usbbuxsigma_send_cmd(dev, USBDUXSIGMA_PWM_OFF_CMD);
}

static void usbduxsigma_pwm_urb_complete(struct urb *urb)
{
	struct comedi_device *dev = urb->context;
	struct usbduxsigma_private *devpriv = dev->private;
	int ret;

	switch (urb->status) {
	case 0:
		/* success */
		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -ECONNABORTED:
		/* happens after an unlink command */
		if (devpriv->pwm_cmd_running)
			usbduxsigma_pwm_stop(dev, 0);	/* w/o unlink */
		return;

	default:
		/* a real error */
		if (devpriv->pwm_cmd_running) {
			dev_err(dev->class_dev,
				"%s: non-zero urb status (%d)\n",
				__func__, urb->status);
			usbduxsigma_pwm_stop(dev, 0);	/* w/o unlink */
		}
		return;
	}

	if (!devpriv->pwm_cmd_running)
		return;

	urb->transfer_buffer_length = devpriv->sizePwmBuf;
	urb->dev = comedi_to_usb_dev(dev);
	urb->status = 0;
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret < 0) {
		dev_err(dev->class_dev, "%s: urb resubmit failed (%d)\n",
			__func__, ret);
		if (ret == EL2NSYNC)
			dev_err(dev->class_dev,
				"buggy USB host controller or bug in IRQ handler\n");
		usbduxsigma_pwm_stop(dev, 0);	/* w/o unlink */
	}
}

static int usbduxsigma_submit_pwm_urb(struct comedi_device *dev)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbduxsigma_private *devpriv = dev->private;
	struct urb *urb = devpriv->urbPwm;

	/* in case of a resubmission after an unlink... */
	usb_fill_bulk_urb(urb, usb, usb_sndbulkpipe(usb, PWM_EP),
			  urb->transfer_buffer, devpriv->sizePwmBuf,
			  usbduxsigma_pwm_urb_complete, dev);

	return usb_submit_urb(urb, GFP_ATOMIC);
}

static int usbduxsigma_pwm_period(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned int period)
{
	struct usbduxsigma_private *devpriv = dev->private;
	int fx2delay = 255;

	if (period < MIN_PWM_PERIOD) {
		return -EAGAIN;
	} else {
		fx2delay = (period / (6 * 512 * 1000 / 33)) - 6;
		if (fx2delay > 255)
			return -EAGAIN;
	}
	devpriv->pwmDelay = fx2delay;
	devpriv->pwmPeriod = period;
	return 0;
}

static int usbduxsigma_pwm_start(struct comedi_device *dev,
				 struct comedi_subdevice *s)
{
	struct usbduxsigma_private *devpriv = dev->private;
	int ret;

	if (devpriv->pwm_cmd_running)
		return 0;

	devpriv->dux_commands[1] = devpriv->pwmDelay;
	ret = usbbuxsigma_send_cmd(dev, USBDUXSIGMA_PWM_ON_CMD);
	if (ret < 0)
		return ret;

	memset(devpriv->urbPwm->transfer_buffer, 0, devpriv->sizePwmBuf);

	ret = usbduxsigma_submit_pwm_urb(dev);
	if (ret < 0)
		return ret;
	devpriv->pwm_cmd_running = 1;

	return 0;
}

static int usbduxsigma_pwm_pattern(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   unsigned int chan,
				   unsigned int value,
				   unsigned int sign)
{
	struct usbduxsigma_private *devpriv = dev->private;
	char pwm_mask = (1 << chan);	/* DIO bit for the PWM data */
	char sgn_mask = (16 << chan);	/* DIO bit for the sign */
	char *buf = (char *)(devpriv->urbPwm->transfer_buffer);
	int szbuf = devpriv->sizePwmBuf;
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
	return 1;
}

static int usbduxsigma_pwm_write(struct comedi_device *dev,
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
	return usbduxsigma_pwm_pattern(dev, s, chan, data[0], 0);
}

static int usbduxsigma_pwm_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct usbduxsigma_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);

	switch (data[0]) {
	case INSN_CONFIG_ARM:
		/*
		 * if not zero the PWM is limited to a certain time which is
		 * not supported here
		 */
		if (data[1] != 0)
			return -EINVAL;
		return usbduxsigma_pwm_start(dev, s);
	case INSN_CONFIG_DISARM:
		return usbduxsigma_pwm_cancel(dev, s);
	case INSN_CONFIG_GET_PWM_STATUS:
		data[1] = devpriv->pwm_cmd_running;
		return 0;
	case INSN_CONFIG_PWM_SET_PERIOD:
		return usbduxsigma_pwm_period(dev, s, data[1]);
	case INSN_CONFIG_PWM_GET_PERIOD:
		data[1] = devpriv->pwmPeriod;
		return 0;
	case INSN_CONFIG_PWM_SET_H_BRIDGE:
		/*
		 * data[1] = value
		 * data[2] = sign (for a relay)
		 */
		return usbduxsigma_pwm_pattern(dev, s, chan,
					       data[1], (data[2] != 0));
	case INSN_CONFIG_PWM_GET_H_BRIDGE:
		/* values are not kept in this driver, nothing to return */
		return -EINVAL;
	}
	return -EINVAL;
}

static int usbduxsigma_getstatusinfo(struct comedi_device *dev, int chan)
{
	struct usbduxsigma_private *devpriv = dev->private;
	uint8_t sysred;
	uint32_t val;
	int ret;

	switch (chan) {
	default:
	case 0:
		sysred = 0;		/* ADC zero */
		break;
	case 1:
		sysred = 1;		/* ADC offset */
		break;
	case 2:
		sysred = 4;		/* VCC */
		break;
	case 3:
		sysred = 8;		/* temperature */
		break;
	case 4:
		sysred = 16;		/* gain */
		break;
	case 5:
		sysred =  32;		/* ref */
		break;
	}

	devpriv->dux_commands[1] = 0x12; /* CONFIG0 */
	devpriv->dux_commands[2] = 0x80; /* CONFIG1: 2kHz sampling rate */
	devpriv->dux_commands[3] = 0x00; /* CONFIG3: diff. channels off */
	devpriv->dux_commands[4] = 0;
	devpriv->dux_commands[5] = 0;
	devpriv->dux_commands[6] = sysred;
	ret = usbbuxsigma_send_cmd(dev, USBDUXSIGMA_SINGLE_AD_CMD);
	if (ret < 0)
		return ret;

	ret = usbduxsigma_receive_cmd(dev, USBDUXSIGMA_SINGLE_AD_CMD);
	if (ret < 0)
		return ret;

	/* 32 bits big endian from the A/D converter */
	val = be32_to_cpu(*((int32_t *)((devpriv->insnBuffer)+1)));
	val &= 0x00ffffff;	/* strip status byte */
	val ^= 0x00800000;	/* convert to unsigned */

	return (int)val;
}

static int usbduxsigma_attach_common(struct comedi_device *dev)
{
	struct usbduxsigma_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	int n_subdevs;
	int offset;
	int ret;

	down(&devpriv->sem);

	if (devpriv->high_speed)
		n_subdevs = 4;	/* with pwm */
	else
		n_subdevs = 3;	/* without pwm */
	ret = comedi_alloc_subdevices(dev, n_subdevs);
	if (ret) {
		up(&devpriv->sem);
		return ret;
	}

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_CMD_READ | SDF_LSAMPL;
	s->n_chan	= NUMCHANNELS;
	s->len_chanlist	= NUMCHANNELS;
	s->maxdata	= 0x00ffffff;
	s->range_table	= &range_usbdux_ai_range;
	s->insn_read	= usbduxsigma_ai_insn_read;
	s->do_cmdtest	= usbduxsigma_ai_cmdtest;
	s->do_cmd	= usbduxsigma_ai_cmd;
	s->cancel	= usbduxsigma_ai_cancel;

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	dev->write_subdev = s;
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE | SDF_GROUND | SDF_CMD_WRITE;
	s->n_chan	= USBDUXSIGMA_NUM_AO_CHAN;
	s->len_chanlist	= s->n_chan;
	s->maxdata	= 0x00ff;
	s->range_table	= &range_unipolar2_5;
	s->insn_write	= usbduxsigma_ao_insn_write;
	s->insn_read	= usbduxsigma_ao_insn_read;
	s->do_cmdtest	= usbduxsigma_ao_cmdtest;
	s->do_cmd	= usbduxsigma_ao_cmd;
	s->cancel	= usbduxsigma_ao_cancel;

	/* Digital I/O subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 24;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= usbduxsigma_dio_insn_bits;
	s->insn_config	= usbduxsigma_dio_insn_config;

	if (devpriv->high_speed) {
		/* Timer / pwm subdevice */
		s = &dev->subdevices[3];
		s->type		= COMEDI_SUBD_PWM;
		s->subdev_flags	= SDF_WRITABLE | SDF_PWM_HBRIDGE;
		s->n_chan	= 8;
		s->maxdata	= devpriv->sizePwmBuf;
		s->insn_write	= usbduxsigma_pwm_write;
		s->insn_config	= usbduxsigma_pwm_config;

		usbduxsigma_pwm_period(dev, s, PWM_DEFAULT_PERIOD);
	}

	up(&devpriv->sem);

	offset = usbduxsigma_getstatusinfo(dev, 0);
	if (offset < 0)
		dev_err(dev->class_dev,
			"Communication to USBDUXSIGMA failed! Check firmware and cabling\n");

	dev_info(dev->class_dev, "attached, ADC_zero = %x\n", offset);

	return 0;
}

static int usbduxsigma_firmware_upload(struct comedi_device *dev,
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
		dev_err(dev->class_dev, "firmware binary too large for FX2\n");
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

static int usbduxsigma_alloc_usb_buffers(struct comedi_device *dev)
{
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbduxsigma_private *devpriv = dev->private;
	struct urb *urb;
	int i;

	devpriv->dac_commands = kzalloc(NUMOUTCHANNELS, GFP_KERNEL);
	devpriv->dux_commands = kzalloc(SIZEOFDUXBUFFER, GFP_KERNEL);
	devpriv->inBuffer = kzalloc(SIZEINBUF, GFP_KERNEL);
	devpriv->insnBuffer = kzalloc(SIZEINSNBUF, GFP_KERNEL);
	devpriv->urbIn = kcalloc(devpriv->numOfInBuffers, sizeof(*urb),
				 GFP_KERNEL);
	devpriv->urbOut = kcalloc(devpriv->numOfOutBuffers, sizeof(*urb),
				  GFP_KERNEL);
	if (!devpriv->dac_commands || !devpriv->dux_commands ||
	    !devpriv->inBuffer || !devpriv->insnBuffer ||
	    !devpriv->urbIn || !devpriv->urbOut)
		return -ENOMEM;

	for (i = 0; i < devpriv->numOfInBuffers; i++) {
		/* one frame: 1ms */
		urb = usb_alloc_urb(1, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		devpriv->urbIn[i] = urb;
		urb->dev = usb;
		/* will be filled later with a pointer to the comedi-device */
		/* and ONLY then the urb should be submitted */
		urb->context = NULL;
		urb->pipe = usb_rcvisocpipe(usb, ISOINEP);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = kzalloc(SIZEINBUF, GFP_KERNEL);
		if (!urb->transfer_buffer)
			return -ENOMEM;
		urb->complete = usbduxsigma_ai_urb_complete;
		urb->number_of_packets = 1;
		urb->transfer_buffer_length = SIZEINBUF;
		urb->iso_frame_desc[0].offset = 0;
		urb->iso_frame_desc[0].length = SIZEINBUF;
	}

	for (i = 0; i < devpriv->numOfOutBuffers; i++) {
		/* one frame: 1ms */
		urb = usb_alloc_urb(1, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		devpriv->urbOut[i] = urb;
		urb->dev = usb;
		/* will be filled later with a pointer to the comedi-device */
		/* and ONLY then the urb should be submitted */
		urb->context = NULL;
		urb->pipe = usb_sndisocpipe(usb, ISOOUTEP);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->transfer_buffer = kzalloc(SIZEOUTBUF, GFP_KERNEL);
		if (!urb->transfer_buffer)
			return -ENOMEM;
		urb->complete = usbduxsigma_ao_urb_complete;
		urb->number_of_packets = 1;
		urb->transfer_buffer_length = SIZEOUTBUF;
		urb->iso_frame_desc[0].offset = 0;
		urb->iso_frame_desc[0].length = SIZEOUTBUF;
		if (devpriv->high_speed)
			urb->interval = 8;	/* uframes */
		else
			urb->interval = 1;	/* frames */
	}

	if (devpriv->high_speed) {
		/* max bulk ep size in high speed */
		devpriv->sizePwmBuf = 512;
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb)
			return -ENOMEM;
		devpriv->urbPwm = urb;
		urb->transfer_buffer = kzalloc(devpriv->sizePwmBuf, GFP_KERNEL);
		if (!urb->transfer_buffer)
			return -ENOMEM;
	} else {
		devpriv->urbPwm = NULL;
		devpriv->sizePwmBuf = 0;
	}

	return 0;
}

static void usbduxsigma_free_usb_buffers(struct comedi_device *dev)
{
	struct usbduxsigma_private *devpriv = dev->private;
	struct urb *urb;
	int i;

	/* force unlink all urbs */
	usbdux_ai_stop(devpriv, 1);
	usbdux_ao_stop(devpriv, 1);
	usbduxsigma_pwm_stop(dev, 1);

	urb = devpriv->urbPwm;
	if (urb) {
		kfree(urb->transfer_buffer);
		usb_free_urb(urb);
	}
	if (devpriv->urbOut) {
		for (i = 0; i < devpriv->numOfOutBuffers; i++) {
			urb = devpriv->urbOut[i];
			if (urb) {
				kfree(urb->transfer_buffer);
				usb_free_urb(urb);
			}
		}
		kfree(devpriv->urbOut);
	}
	if (devpriv->urbIn) {
		for (i = 0; i < devpriv->numOfInBuffers; i++) {
			urb = devpriv->urbIn[i];
			if (urb) {
				kfree(urb->transfer_buffer);
				usb_free_urb(urb);
			}
		}
		kfree(devpriv->urbIn);
	}
	kfree(devpriv->insnBuffer);
	kfree(devpriv->inBuffer);
	kfree(devpriv->dux_commands);
	kfree(devpriv->dac_commands);
}

static int usbduxsigma_auto_attach(struct comedi_device *dev,
				   unsigned long context_unused)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usb_device *usb = comedi_to_usb_dev(dev);
	struct usbduxsigma_private *devpriv;
	int ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	sema_init(&devpriv->sem, 1);
	usb_set_intfdata(intf, devpriv);

	ret = usb_set_interface(usb,
				intf->altsetting->desc.bInterfaceNumber, 3);
	if (ret < 0) {
		dev_err(dev->class_dev,
			"could not set alternate setting 3 in high speed\n");
		return -ENODEV;
	}

	/* test if it is high speed (USB 2.0) */
	devpriv->high_speed = (usb->speed == USB_SPEED_HIGH);
	if (devpriv->high_speed) {
		devpriv->numOfInBuffers = NUMOFINBUFFERSHIGH;
		devpriv->numOfOutBuffers = NUMOFOUTBUFFERSHIGH;
	} else {
		devpriv->numOfInBuffers = NUMOFINBUFFERSFULL;
		devpriv->numOfOutBuffers = NUMOFOUTBUFFERSFULL;
	}

	ret = usbduxsigma_alloc_usb_buffers(dev);
	if (ret)
		return ret;

	ret = comedi_load_firmware(dev, &usb->dev, FIRMWARE,
				   usbduxsigma_firmware_upload, 0);
	if (ret)
		return ret;

	return usbduxsigma_attach_common(dev);
}

static void usbduxsigma_detach(struct comedi_device *dev)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	struct usbduxsigma_private *devpriv = dev->private;

	if (!devpriv)
		return;

	usb_set_intfdata(intf, NULL);

	down(&devpriv->sem);
	usbduxsigma_free_usb_buffers(dev);
	up(&devpriv->sem);
}

static struct comedi_driver usbduxsigma_driver = {
	.driver_name	= "usbduxsigma",
	.module		= THIS_MODULE,
	.auto_attach	= usbduxsigma_auto_attach,
	.detach		= usbduxsigma_detach,
};

static int usbduxsigma_usb_probe(struct usb_interface *intf,
				 const struct usb_device_id *id)
{
	return comedi_usb_auto_config(intf, &usbduxsigma_driver, 0);
}

static const struct usb_device_id usbduxsigma_usb_table[] = {
	{ USB_DEVICE(0x13d8, 0x0020) },
	{ USB_DEVICE(0x13d8, 0x0021) },
	{ USB_DEVICE(0x13d8, 0x0022) },
	{ }
};
MODULE_DEVICE_TABLE(usb, usbduxsigma_usb_table);

static struct usb_driver usbduxsigma_usb_driver = {
	.name		= "usbduxsigma",
	.probe		= usbduxsigma_usb_probe,
	.disconnect	= comedi_usb_auto_unconfig,
	.id_table	= usbduxsigma_usb_table,
};
module_comedi_usb_driver(usbduxsigma_driver, usbduxsigma_usb_driver);

MODULE_AUTHOR("Bernd Porr, BerndPorr@f2s.com");
MODULE_DESCRIPTION("Stirling/ITL USB-DUX SIGMA -- Bernd.Porr@f2s.com");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE);
