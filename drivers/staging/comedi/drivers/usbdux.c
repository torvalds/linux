#define DRIVER_VERSION "v2.2"
#define DRIVER_AUTHOR "Bernd Porr, BerndPorr@f2s.com"
#define DRIVER_DESC "Stirling/ITL USB-DUX -- Bernd.Porr@f2s.com"
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

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

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
 * 0.98: SMP, locking, memroy alloc: moved all usb memory alloc
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
 * 1.2:  added PWM suport via EP4
 * 2.0:  PWM seems to be stable and is not interfering with the other functions
 * 2.1:  changed PWM API
 * 2.2:  added firmware kernel request to fix an udev problem
 *
 */

/* generates loads of debug info */
/* #define NOISY_DUX_DEBUGBUG */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/usb.h>
#include <linux/smp_lock.h>
#include <linux/fcntl.h>
#include <linux/compiler.h>
#include <linux/firmware.h>

#include "../comedidev.h"

#define BOARDNAME "usbdux"

/* timeout for the USB-transfer */
#define EZTIMEOUT 30

/* constants for "firmware" upload and download */
#define USBDUXSUB_FIRMWARE 0xA0
#define VENDOR_DIR_IN  0xC0
#define VENDOR_DIR_OUT 0x40

/* internal adresses of the 8051 processor */
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

/* Total number of usbdux devices */
#define NUMUSBDUX             16

/* Analogue in subdevice */
#define SUBDEV_AD             0

/* Analogue out subdevice */
#define SUBDEV_DA             1

/* Digital I/O */
#define SUBDEV_DIO            2

/* counter */
#define SUBDEV_COUNTER        3

/* timer aka pwm output */
#define SUBDEV_PWM            4

/* number of retries to get the right dux command */
#define RETRIES 10

/**************************************************/
/* comedi constants */
static const struct comedi_lrange range_usbdux_ai_range = { 4, {
			BIP_RANGE(4.096),
			BIP_RANGE(4.096 / 2),
			UNI_RANGE(4.096),
			UNI_RANGE(4.096 / 2)
	}
};

static const struct comedi_lrange range_usbdux_ao_range = { 2, {
			BIP_RANGE(4.096),
			UNI_RANGE(4.096),
	}
};

/*
 * private structure of one subdevice
 */

/*
 * This is the structure which holds all the data of
 * this driver one sub device just now: A/D
 */
struct usbduxsub {
	/* attached? */
	int attached;
	/* is it associated with a subdevice? */
	int probed;
	/* pointer to the usb-device */
	struct usb_device *usbdev;
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
	int8_t pwmDelay;
	/* size of the PWM buffer which holds the bit pattern */
	int sizePwmBuf;
	/* input buffer for the ISO-transfer */
	int16_t *inBuffer;
	/* input buffer for single insn */
	int16_t *insnBuffer;
	/* output buffer for single DA outputs */
	int16_t *outBuffer;
	/* interface number */
	int ifnum;
	/* interface structure in 2.6 */
	struct usb_interface *interface;
	/* comedi device for the interrupt context */
	struct comedi_device *comedidev;
	/* is it USB_SPEED_HIGH or not? */
	short int high_speed;
	/* asynchronous command is running */
	short int ai_cmd_running;
	short int ao_cmd_running;
	/* pwm is running */
	short int pwm_cmd_running;
	/* continous aquisition */
	short int ai_continous;
	short int ao_continous;
	/* number of samples to aquire */
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
 * The pointer to the private usb-data of the driver is also the private data
 * for the comedi-device.  This has to be global as the usb subsystem needs
 * global variables. The other reason is that this structure must be there
 * _before_ any comedi command is issued. The usb subsystem must be initialised
 * before comedi can access it.
 */
static struct usbduxsub usbduxsub[NUMUSBDUX];

static DECLARE_MUTEX(start_stop_sem);

/*
 * Stops the data acquision
 * It should be safe to call this function from any context
 */
static int usbduxsub_unlink_InURBs(struct usbduxsub *usbduxsub_tmp)
{
	int i = 0;
	int err = 0;

	if (usbduxsub_tmp && usbduxsub_tmp->urbIn) {
		for (i = 0; i < usbduxsub_tmp->numOfInBuffers; i++) {
			if (usbduxsub_tmp->urbIn[i]) {
				/* We wait here until all transfers have been
				 * cancelled. */
				usb_kill_urb(usbduxsub_tmp->urbIn[i]);
			}
			dev_dbg(&usbduxsub_tmp->interface->dev,
				"comedi: usbdux: unlinked InURB %d, err=%d\n",
				i, err);
		}
	}
	return err;
}

/*
 * This will stop a running acquisition operation
 * Is called from within this driver from both the
 * interrupt context and from comedi
 */
static int usbdux_ai_stop(struct usbduxsub *this_usbduxsub, int do_unlink)
{
	int ret = 0;

	if (!this_usbduxsub) {
		dev_err(&this_usbduxsub->interface->dev,
			"comedi?: usbdux_ai_stop: this_usbduxsub=NULL!\n");
		return -EFAULT;
	}
	dev_dbg(&this_usbduxsub->interface->dev, "comedi: usbdux_ai_stop\n");

	if (do_unlink) {
		/* stop aquistion */
		ret = usbduxsub_unlink_InURBs(this_usbduxsub);
	}

	this_usbduxsub->ai_cmd_running = 0;

	return ret;
}

/*
 * This will cancel a running acquisition operation.
 * This is called by comedi but never from inside the driver.
 */
static int usbdux_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct usbduxsub *this_usbduxsub;
	int res = 0;

	/* force unlink of all urbs */
	this_usbduxsub = dev->private;
	if (!this_usbduxsub)
		return -EFAULT;

	dev_dbg(&this_usbduxsub->interface->dev, "comedi: usbdux_ai_cancel\n");

	/* prevent other CPUs from submitting new commands just now */
	down(&this_usbduxsub->sem);
	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}
	/* unlink only if the urb really has been submitted */
	res = usbdux_ai_stop(this_usbduxsub, this_usbduxsub->ai_cmd_running);
	up(&this_usbduxsub->sem);
	return res;
}

/* analogue IN - interrupt service routine */
static void usbduxsub_ai_IsocIrq(struct urb *urb)
{
	int i, err, n;
	struct usbduxsub *this_usbduxsub;
	struct comedi_device *this_comedidev;
	struct comedi_subdevice *s;

	/* the context variable points to the subdevice */
	this_comedidev = urb->context;
	/* the private structure of the subdevice is struct usbduxsub */
	this_usbduxsub = this_comedidev->private;
	/* subdevice which is the AD converter */
	s = this_comedidev->subdevices + SUBDEV_AD;

	/* first we test if something unusual has just happened */
	switch (urb->status) {
	case 0:
		/* copy the result in the transfer buffer */
		memcpy(this_usbduxsub->inBuffer,
			urb->transfer_buffer, SIZEINBUF);
		break;
	case -EILSEQ:
		/* error in the ISOchronous data */
		/* we don't copy the data into the transfer buffer */
		/* and recycle the last data byte */
		dev_dbg(&urb->dev->dev,
			"comedi%d: usbdux: CRC error in ISO IN stream.\n",
			this_usbduxsub->comedidev->minor);

		break;

	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
	case -ECONNABORTED:
		/* happens after an unlink command */
		if (this_usbduxsub->ai_cmd_running) {
			/* we are still running a command */
			/* tell this comedi */
			s->async->events |= COMEDI_CB_EOA;
			s->async->events |= COMEDI_CB_ERROR;
			comedi_event(this_usbduxsub->comedidev, s);
			/* stop the transfer w/o unlink */
			usbdux_ai_stop(this_usbduxsub, 0);
		}
		return;

	default:
		/* a real error on the bus */
		/* pass error to comedi if we are really running a command */
		if (this_usbduxsub->ai_cmd_running) {
			dev_err(&urb->dev->dev,
				"Non-zero urb status received in ai intr "
				"context: %d\n", urb->status);
			s->async->events |= COMEDI_CB_EOA;
			s->async->events |= COMEDI_CB_ERROR;
			comedi_event(this_usbduxsub->comedidev, s);
			/* don't do an unlink here */
			usbdux_ai_stop(this_usbduxsub, 0);
		}
		return;
	}

	/*
	 * at this point we are reasonably sure that nothing dodgy has happened
	 * are we running a command?
	 */
	if (unlikely((!(this_usbduxsub->ai_cmd_running)))) {
		/*
		 * not running a command, do not continue execution if no
		 * asynchronous command is running in particular not resubmit
		 */
		return;
	}

	urb->dev = this_usbduxsub->usbdev;

	/* resubmit the urb */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(err < 0)) {
		dev_err(&urb->dev->dev,
			"comedi_: urb resubmit failed in int-context! err=%d\n",
			err);
		if (err == -EL2NSYNC)
			dev_err(&urb->dev->dev,
				"buggy USB host controller or bug in IRQ "
				"handler!\n");
		s->async->events |= COMEDI_CB_EOA;
		s->async->events |= COMEDI_CB_ERROR;
		comedi_event(this_usbduxsub->comedidev, s);
		/* don't do an unlink here */
		usbdux_ai_stop(this_usbduxsub, 0);
		return;
	}

	this_usbduxsub->ai_counter--;
	if (likely(this_usbduxsub->ai_counter > 0))
		return;

	/* timer zero, transfer measurements to comedi */
	this_usbduxsub->ai_counter = this_usbduxsub->ai_timer;

	/* test, if we transmit only a fixed number of samples */
	if (!(this_usbduxsub->ai_continous)) {
		/* not continous, fixed number of samples */
		this_usbduxsub->ai_sample_count--;
		/* all samples received? */
		if (this_usbduxsub->ai_sample_count < 0) {
			/* prevent a resubmit next time */
			usbdux_ai_stop(this_usbduxsub, 0);
			/* say comedi that the acquistion is over */
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(this_usbduxsub->comedidev, s);
			return;
		}
	}
	/* get the data from the USB bus and hand it over to comedi */
	n = s->async->cmd.chanlist_len;
	for (i = 0; i < n; i++) {
		/* transfer data */
		if (CR_RANGE(s->async->cmd.chanlist[i]) <= 1) {
			comedi_buf_put
				(s->async,
				le16_to_cpu(this_usbduxsub->
					inBuffer[i]) ^ 0x800);
		} else {
			comedi_buf_put
				(s->async,
				le16_to_cpu(this_usbduxsub->inBuffer[i]));
		}
	}
	/* tell comedi that data is there */
	comedi_event(this_usbduxsub->comedidev, s);
}

static int usbduxsub_unlink_OutURBs(struct usbduxsub *usbduxsub_tmp)
{
	int i = 0;
	int err = 0;

	if (usbduxsub_tmp && usbduxsub_tmp->urbOut) {
		for (i = 0; i < usbduxsub_tmp->numOfOutBuffers; i++) {
			if (usbduxsub_tmp->urbOut[i])
				usb_kill_urb(usbduxsub_tmp->urbOut[i]);

			dev_dbg(&usbduxsub_tmp->interface->dev,
				"comedi: usbdux: unlinked OutURB %d: res=%d\n",
				i, err);
		}
	}
	return err;
}

/* This will cancel a running acquisition operation
 * in any context.
 */
static int usbdux_ao_stop(struct usbduxsub *this_usbduxsub, int do_unlink)
{
	int ret = 0;

	if (!this_usbduxsub)
		return -EFAULT;
	dev_dbg(&this_usbduxsub->interface->dev, "comedi: usbdux_ao_cancel\n");

	if (do_unlink)
		ret = usbduxsub_unlink_OutURBs(this_usbduxsub);

	this_usbduxsub->ao_cmd_running = 0;

	return ret;
}

/* force unlink, is called by comedi */
static int usbdux_ao_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct usbduxsub *this_usbduxsub = dev->private;
	int res = 0;

	if (!this_usbduxsub)
		return -EFAULT;

	/* prevent other CPUs from submitting a command just now */
	down(&this_usbduxsub->sem);
	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}
	/* unlink only if it is really running */
	res = usbdux_ao_stop(this_usbduxsub, this_usbduxsub->ao_cmd_running);
	up(&this_usbduxsub->sem);
	return res;
}

static void usbduxsub_ao_IsocIrq(struct urb *urb)
{
	int i, ret;
	int8_t *datap;
	struct usbduxsub *this_usbduxsub;
	struct comedi_device *this_comedidev;
	struct comedi_subdevice *s;

	/* the context variable points to the subdevice */
	this_comedidev = urb->context;
	/* the private structure of the subdevice is struct usbduxsub */
	this_usbduxsub = this_comedidev->private;

	s = this_comedidev->subdevices + SUBDEV_DA;

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
		if (this_usbduxsub->ao_cmd_running) {
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(this_usbduxsub->comedidev, s);
			usbdux_ao_stop(this_usbduxsub, 0);
		}
		return;

	default:
		/* a real error */
		if (this_usbduxsub->ao_cmd_running) {
			dev_err(&urb->dev->dev,
				"comedi_: Non-zero urb status received in ao "
				"intr context: %d\n", urb->status);
			s->async->events |= COMEDI_CB_ERROR;
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(this_usbduxsub->comedidev, s);
			/* we do an unlink if we are in the high speed mode */
			usbdux_ao_stop(this_usbduxsub, 0);
		}
		return;
	}

	/* are we actually running? */
	if (!(this_usbduxsub->ao_cmd_running))
		return;

	/* normal operation: executing a command in this subdevice */
	this_usbduxsub->ao_counter--;
	if (this_usbduxsub->ao_counter <= 0) {
		/* timer zero */
		this_usbduxsub->ao_counter = this_usbduxsub->ao_timer;

		/* handle non continous aquisition */
		if (!(this_usbduxsub->ao_continous)) {
			/* fixed number of samples */
			this_usbduxsub->ao_sample_count--;
			if (this_usbduxsub->ao_sample_count < 0) {
				/* all samples transmitted */
				usbdux_ao_stop(this_usbduxsub, 0);
				s->async->events |= COMEDI_CB_EOA;
				comedi_event(this_usbduxsub->comedidev, s);
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
			       (&(((int8_t *)urb->transfer_buffer)[i * 3 + 1]));
			/* get the data from comedi */
			ret = comedi_buf_get(s->async, &temp);
			datap[0] = temp;
			datap[1] = temp >> 8;
			datap[2] = this_usbduxsub->dac_commands[i];
			/* printk("data[0]=%x, data[1]=%x, data[2]=%x\n", */
			/* datap[0],datap[1],datap[2]); */
			if (ret < 0) {
				dev_err(&urb->dev->dev,
					"comedi: buffer underflow\n");
				s->async->events |= COMEDI_CB_EOA;
				s->async->events |= COMEDI_CB_OVERFLOW;
			}
			/* transmit data to comedi */
			s->async->events |= COMEDI_CB_BLOCK;
			comedi_event(this_usbduxsub->comedidev, s);
		}
	}
	urb->transfer_buffer_length = SIZEOUTBUF;
	urb->dev = this_usbduxsub->usbdev;
	urb->status = 0;
	if (this_usbduxsub->ao_cmd_running) {
		if (this_usbduxsub->high_speed) {
			/* uframes */
			urb->interval = 8;
		} else {
			/* frames */
			urb->interval = 1;
		}
		urb->number_of_packets = 1;
		urb->iso_frame_desc[0].offset = 0;
		urb->iso_frame_desc[0].length = SIZEOUTBUF;
		urb->iso_frame_desc[0].status = 0;
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret < 0) {
			dev_err(&urb->dev->dev,
				"comedi_: ao urb resubm failed in int-cont. "
				"ret=%d", ret);
			if (ret == EL2NSYNC)
				dev_err(&urb->dev->dev,
					"buggy USB host controller or bug in "
					"IRQ handling!\n");

			s->async->events |= COMEDI_CB_EOA;
			s->async->events |= COMEDI_CB_ERROR;
			comedi_event(this_usbduxsub->comedidev, s);
			/* don't do an unlink here */
			usbdux_ao_stop(this_usbduxsub, 0);
		}
	}
}

static int usbduxsub_start(struct usbduxsub *usbduxsub)
{
	int errcode = 0;
	uint8_t local_transfer_buffer[16];

	/* 7f92 to zero */
	local_transfer_buffer[0] = 0;
	errcode = usb_control_msg(usbduxsub->usbdev,
				  /* create a pipe for a control transfer */
				  usb_sndctrlpipe(usbduxsub->usbdev, 0),
				  /* bRequest, "Firmware" */
				  USBDUXSUB_FIRMWARE,
				  /* bmRequestType */
				  VENDOR_DIR_OUT,
				  /* Value */
				  USBDUXSUB_CPUCS,
				  /* Index */
				  0x0000,
				  /* address of the transfer buffer */
				  local_transfer_buffer,
				  /* Length */
				  1,
				  /* Timeout */
				  EZTIMEOUT);
	if (errcode < 0) {
		dev_err(&usbduxsub->interface->dev,
			"comedi_: control msg failed (start)\n");
		return errcode;
	}
	return 0;
}

static int usbduxsub_stop(struct usbduxsub *usbduxsub)
{
	int errcode = 0;

	uint8_t local_transfer_buffer[16];

	/* 7f92 to one */
	local_transfer_buffer[0] = 1;
	errcode = usb_control_msg(usbduxsub->usbdev,
				  usb_sndctrlpipe(usbduxsub->usbdev, 0),
				  /* bRequest, "Firmware" */
				  USBDUXSUB_FIRMWARE,
				  /* bmRequestType */
				  VENDOR_DIR_OUT,
				  /* Value */
				  USBDUXSUB_CPUCS,
				  /* Index */
				  0x0000, local_transfer_buffer,
				  /* Length */
				  1,
				  /* Timeout */
				  EZTIMEOUT);
	if (errcode < 0) {
		dev_err(&usbduxsub->interface->dev,
			"comedi_: control msg failed (stop)\n");
		return errcode;
	}
	return 0;
}

static int usbduxsub_upload(struct usbduxsub *usbduxsub,
			    uint8_t *local_transfer_buffer,
			    unsigned int startAddr, unsigned int len)
{
	int errcode;

	errcode = usb_control_msg(usbduxsub->usbdev,
			usb_sndctrlpipe(usbduxsub->usbdev, 0),
			/* brequest, firmware */
			USBDUXSUB_FIRMWARE,
			/* bmRequestType */
			VENDOR_DIR_OUT,
			/* value */
			startAddr,
			/* index */
			0x0000,
			/* our local safe buffer */
			local_transfer_buffer,
			/* length */
			len,
			/* timeout */
			EZTIMEOUT);
	dev_dbg(&usbduxsub->interface->dev,
		"comedi_: result=%d\n", errcode);
	if (errcode < 0) {
		dev_err(&usbduxsub->interface->dev,
		"comedi_: upload failed\n");
		return errcode;
	}
	return 0;
}

static int firmwareUpload(struct usbduxsub *usbduxsub, uint8_t *firmwareBinary,
			  int sizeFirmware)
{
	int ret;

	if (!firmwareBinary)
		return 0;

	ret = usbduxsub_stop(usbduxsub);
	if (ret < 0) {
		dev_err(&usbduxsub->interface->dev,
			"comedi_: can not stop firmware\n");
		return ret;
	}
	ret = usbduxsub_upload(usbduxsub, firmwareBinary, 0, sizeFirmware);
	if (ret < 0) {
		dev_err(&usbduxsub->interface->dev,
			"comedi_: firmware upload failed\n");
		return ret;
	}
	ret = usbduxsub_start(usbduxsub);
	if (ret < 0) {
		dev_err(&usbduxsub->interface->dev,
			"comedi_: can not start firmware\n");
		return ret;
	}
	return 0;
}

static int usbduxsub_submit_InURBs(struct usbduxsub *usbduxsub)
{
	int i, errFlag;

	if (!usbduxsub)
		return -EFAULT;

	/* Submit all URBs and start the transfer on the bus */
	for (i = 0; i < usbduxsub->numOfInBuffers; i++) {
		/* in case of a resubmission after an unlink... */
		usbduxsub->urbIn[i]->interval = usbduxsub->ai_interval;
		usbduxsub->urbIn[i]->context = usbduxsub->comedidev;
		usbduxsub->urbIn[i]->dev = usbduxsub->usbdev;
		usbduxsub->urbIn[i]->status = 0;
		usbduxsub->urbIn[i]->transfer_flags = URB_ISO_ASAP;
		dev_dbg(&usbduxsub->interface->dev,
			"comedi%d: submitting in-urb[%d]: %p,%p intv=%d\n",
			usbduxsub->comedidev->minor, i,
			(usbduxsub->urbIn[i]->context),
			(usbduxsub->urbIn[i]->dev),
			(usbduxsub->urbIn[i]->interval));
		errFlag = usb_submit_urb(usbduxsub->urbIn[i], GFP_ATOMIC);
		if (errFlag) {
			dev_err(&usbduxsub->interface->dev,
				"comedi_: ai: usb_submit_urb(%d) error %d\n",
				i, errFlag);
			return errFlag;
		}
	}
	return 0;
}

static int usbduxsub_submit_OutURBs(struct usbduxsub *usbduxsub)
{
	int i, errFlag;

	if (!usbduxsub)
		return -EFAULT;

	for (i = 0; i < usbduxsub->numOfOutBuffers; i++) {
		dev_dbg(&usbduxsub->interface->dev,
			"comedi_: submitting out-urb[%d]\n", i);
		/* in case of a resubmission after an unlink... */
		usbduxsub->urbOut[i]->context = usbduxsub->comedidev;
		usbduxsub->urbOut[i]->dev = usbduxsub->usbdev;
		usbduxsub->urbOut[i]->status = 0;
		usbduxsub->urbOut[i]->transfer_flags = URB_ISO_ASAP;
		errFlag = usb_submit_urb(usbduxsub->urbOut[i], GFP_ATOMIC);
		if (errFlag) {
			dev_err(&usbduxsub->interface->dev,
				"comedi_: ao: usb_submit_urb(%d) error %d\n",
				i, errFlag);
			return errFlag;
		}
	}
	return 0;
}

static int usbdux_ai_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
			     struct comedi_cmd *cmd)
{
	int err = 0, tmp, i;
	unsigned int tmpTimer;
	struct usbduxsub *this_usbduxsub = dev->private;

	if (!(this_usbduxsub->probed))
		return -ENODEV;

	dev_dbg(&this_usbduxsub->interface->dev,
		"comedi%d: usbdux_ai_cmdtest\n", dev->minor);

	/* make sure triggers are valid */
	/* Only immediate triggers are allowed */
	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW | TRIG_INT;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	/* trigger should happen timed */
	tmp = cmd->scan_begin_src;
	/* start a new _scan_ with a timer */
	cmd->scan_begin_src &= TRIG_TIMER;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	/* scanning is continous */
	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_NOW;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	/* issue a trigger when scan is finished and start a new scan */
	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	/* trigger at the end of count events or not, stop condition or not */
	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/*
	 * step 2: make sure trigger sources are unique and mutually compatible
	 * note that mutual compatiblity is not an issue here
	 */
	if (cmd->scan_begin_src != TRIG_FOLLOW &&
		cmd->scan_begin_src != TRIG_EXT &&
		cmd->scan_begin_src != TRIG_TIMER)
		err++;
	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */
	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		/* internal trigger */
		if (cmd->scan_begin_arg != 0) {
			cmd->scan_begin_arg = 0;
			err++;
		}
	}

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

			if (cmd->scan_begin_arg < (1000000 / 8 * i)) {
				cmd->scan_begin_arg = 1000000 / 8 * i;
				err++;
			}
			/* now calc the real sampling rate with all the
			 * rounding errors */
			tmpTimer =
				((unsigned int)(cmd->scan_begin_arg / 125000)) *
				125000;
			if (cmd->scan_begin_arg != tmpTimer) {
				cmd->scan_begin_arg = tmpTimer;
				err++;
			}
		} else {
			/* full speed */
			/* 1kHz scans every USB frame */
			if (cmd->scan_begin_arg < 1000000) {
				cmd->scan_begin_arg = 1000000;
				err++;
			}
			/*
			 * calc the real sampling rate with the rounding errors
			 */
			tmpTimer = ((unsigned int)(cmd->scan_begin_arg /
					1000000)) * 1000000;
			if (cmd->scan_begin_arg != tmpTimer) {
				cmd->scan_begin_arg = tmpTimer;
				err++;
			}
		}
	}
	/* the same argument */
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	if (cmd->stop_src == TRIG_COUNT) {
		/* any count is allowed */
	} else {
		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
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

static int send_dux_commands(struct usbduxsub *this_usbduxsub, int cmd_type)
{
	int result, nsent;

	this_usbduxsub->dux_commands[0] = cmd_type;
#ifdef NOISY_DUX_DEBUGBUG
	printk(KERN_DEBUG "comedi%d: usbdux: dux_commands: ",
		this_usbduxsub->comedidev->minor);
	for (result = 0; result < SIZEOFDUXBUFFER; result++)
		printk(" %02x", this_usbduxsub->dux_commands[result]);
	printk("\n");
#endif
	result = usb_bulk_msg(this_usbduxsub->usbdev,
			      usb_sndbulkpipe(this_usbduxsub->usbdev,
					      COMMAND_OUT_EP),
			      this_usbduxsub->dux_commands, SIZEOFDUXBUFFER,
			      &nsent, 10);
	if (result < 0)
		dev_err(&this_usbduxsub->interface->dev, "comedi%d: "
			"could not transmit dux_command to the usb-device, "
			"err=%d\n", this_usbduxsub->comedidev->minor, result);

	return result;
}

static int receive_dux_commands(struct usbduxsub *this_usbduxsub, int command)
{
	int result = (-EFAULT);
	int nrec;
	int i;

	for (i = 0; i < RETRIES; i++) {
		result = usb_bulk_msg(this_usbduxsub->usbdev,
				      usb_rcvbulkpipe(this_usbduxsub->usbdev,
						      COMMAND_IN_EP),
				      this_usbduxsub->insnBuffer, SIZEINSNBUF,
				      &nrec, 1);
		if (result < 0) {
			dev_err(&this_usbduxsub->interface->dev, "comedi%d: "
				"insn: USB error %d while receiving DUX command"
				"\n", this_usbduxsub->comedidev->minor, result);
			return result;
		}
		if (le16_to_cpu(this_usbduxsub->insnBuffer[0]) == command)
			return result;
	}
	/* this is only reached if the data has been requested a couple of
	 * times */
	dev_err(&this_usbduxsub->interface->dev, "comedi%d: insn: "
		"wrong data returned from firmware: want cmd %d, got cmd %d.\n",
		this_usbduxsub->comedidev->minor, command,
		le16_to_cpu(this_usbduxsub->insnBuffer[0]));
	return -EFAULT;
}

static int usbdux_ai_inttrig(struct comedi_device *dev, struct comedi_subdevice *s,
			     unsigned int trignum)
{
	int ret;
	struct usbduxsub *this_usbduxsub = dev->private;
	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);
	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}
	dev_dbg(&this_usbduxsub->interface->dev,
		"comedi%d: usbdux_ai_inttrig\n", dev->minor);

	if (trignum != 0) {
		dev_err(&this_usbduxsub->interface->dev,
			"comedi%d: usbdux_ai_inttrig: invalid trignum\n",
			dev->minor);
		up(&this_usbduxsub->sem);
		return -EINVAL;
	}
	if (!(this_usbduxsub->ai_cmd_running)) {
		this_usbduxsub->ai_cmd_running = 1;
		ret = usbduxsub_submit_InURBs(this_usbduxsub);
		if (ret < 0) {
			dev_err(&this_usbduxsub->interface->dev,
				"comedi%d: usbdux_ai_inttrig: "
				"urbSubmit: err=%d\n", dev->minor, ret);
			this_usbduxsub->ai_cmd_running = 0;
			up(&this_usbduxsub->sem);
			return ret;
		}
		s->async->inttrig = NULL;
	} else {
		dev_err(&this_usbduxsub->interface->dev,
			"comedi%d: ai_inttrig but acqu is already running\n",
			dev->minor);
	}
	up(&this_usbduxsub->sem);
	return 1;
}

static int usbdux_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int chan, range;
	int i, ret;
	struct usbduxsub *this_usbduxsub = dev->private;
	int result;

	if (!this_usbduxsub)
		return -EFAULT;

	dev_dbg(&this_usbduxsub->interface->dev,
		"comedi%d: usbdux_ai_cmd\n", dev->minor);

	/* block other CPUs from starting an ai_cmd */
	down(&this_usbduxsub->sem);

	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}
	if (this_usbduxsub->ai_cmd_running) {
		dev_err(&this_usbduxsub->interface->dev, "comedi%d: "
			"ai_cmd not possible. Another ai_cmd is running.\n",
			dev->minor);
		up(&this_usbduxsub->sem);
		return -EBUSY;
	}
	/* set current channel of the running aquisition to zero */
	s->async->cur_chan = 0;

	this_usbduxsub->dux_commands[1] = cmd->chanlist_len;
	for (i = 0; i < cmd->chanlist_len; ++i) {
		chan = CR_CHAN(cmd->chanlist[i]);
		range = CR_RANGE(cmd->chanlist[i]);
		if (i >= NUMCHANNELS) {
			dev_err(&this_usbduxsub->interface->dev,
				"comedi%d: channel list too long\n",
				dev->minor);
			break;
		}
		this_usbduxsub->dux_commands[i + 2] =
			create_adc_command(chan, range);
	}

	dev_dbg(&this_usbduxsub->interface->dev,
		"comedi %d: sending commands to the usb device: size=%u\n",
		dev->minor, NUMCHANNELS);

	result = send_dux_commands(this_usbduxsub, SENDADCOMMANDS);
	if (result < 0) {
		up(&this_usbduxsub->sem);
		return result;
	}

	if (this_usbduxsub->high_speed) {
		/*
		 * every channel gets a time window of 125us. Thus, if we
		 * sample all 8 channels we need 1ms. If we sample only one
		 * channel we need only 125us
		 */
		this_usbduxsub->ai_interval = 1;
		/* find a power of 2 for the interval */
		while ((this_usbduxsub->ai_interval) < (cmd->chanlist_len)) {
			this_usbduxsub->ai_interval =
				(this_usbduxsub->ai_interval) * 2;
		}
		this_usbduxsub->ai_timer = cmd->scan_begin_arg / (125000 *
			(this_usbduxsub->ai_interval));
	} else {
		/* interval always 1ms */
		this_usbduxsub->ai_interval = 1;
		this_usbduxsub->ai_timer = cmd->scan_begin_arg / 1000000;
	}
	if (this_usbduxsub->ai_timer < 1) {
		dev_err(&this_usbduxsub->interface->dev, "comedi%d: ai_cmd: "
			"timer=%d, scan_begin_arg=%d. "
			"Not properly tested by cmdtest?\n", dev->minor,
			this_usbduxsub->ai_timer, cmd->scan_begin_arg);
		up(&this_usbduxsub->sem);
		return -EINVAL;
	}
	this_usbduxsub->ai_counter = this_usbduxsub->ai_timer;

	if (cmd->stop_src == TRIG_COUNT) {
		/* data arrives as one packet */
		this_usbduxsub->ai_sample_count = cmd->stop_arg;
		this_usbduxsub->ai_continous = 0;
	} else {
		/* continous aquisition */
		this_usbduxsub->ai_continous = 1;
		this_usbduxsub->ai_sample_count = 0;
	}

	if (cmd->start_src == TRIG_NOW) {
		/* enable this acquisition operation */
		this_usbduxsub->ai_cmd_running = 1;
		ret = usbduxsub_submit_InURBs(this_usbduxsub);
		if (ret < 0) {
			this_usbduxsub->ai_cmd_running = 0;
			/* fixme: unlink here?? */
			up(&this_usbduxsub->sem);
			return ret;
		}
		s->async->inttrig = NULL;
	} else {
		/* TRIG_INT */
		/* don't enable the acquision operation */
		/* wait for an internal signal */
		s->async->inttrig = usbdux_ai_inttrig;
	}
	up(&this_usbduxsub->sem);
	return 0;
}

/* Mode 0 is used to get a single conversion on demand */
static int usbdux_ai_insn_read(struct comedi_device *dev, struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int i;
	unsigned int one = 0;
	int chan, range;
	int err;
	struct usbduxsub *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return 0;

	dev_dbg(&this_usbduxsub->interface->dev,
		"comedi%d: ai_insn_read, insn->n=%d, insn->subdev=%d\n",
		dev->minor, insn->n, insn->subdev);

	down(&this_usbduxsub->sem);
	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}
	if (this_usbduxsub->ai_cmd_running) {
		dev_err(&this_usbduxsub->interface->dev,
			"comedi%d: ai_insn_read not possible. "
			"Async Command is running.\n", dev->minor);
		up(&this_usbduxsub->sem);
		return 0;
	}

	/* sample one channel */
	chan = CR_CHAN(insn->chanspec);
	range = CR_RANGE(insn->chanspec);
	/* set command for the first channel */
	this_usbduxsub->dux_commands[1] = create_adc_command(chan, range);

	/* adc commands */
	err = send_dux_commands(this_usbduxsub, SENDSINGLEAD);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}

	for (i = 0; i < insn->n; i++) {
		err = receive_dux_commands(this_usbduxsub, SENDSINGLEAD);
		if (err < 0) {
			up(&this_usbduxsub->sem);
			return 0;
		}
		one = le16_to_cpu(this_usbduxsub->insnBuffer[1]);
		if (CR_RANGE(insn->chanspec) <= 1)
			one = one ^ 0x800;

		data[i] = one;
	}
	up(&this_usbduxsub->sem);
	return i;
}

/************************************/
/* analog out */

static int usbdux_ao_insn_read(struct comedi_device *dev, struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);
	struct usbduxsub *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);
	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}
	for (i = 0; i < insn->n; i++)
		data[i] = this_usbduxsub->outBuffer[chan];

	up(&this_usbduxsub->sem);
	return i;
}

static int usbdux_ao_insn_write(struct comedi_device *dev, struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int i, err;
	int chan = CR_CHAN(insn->chanspec);
	struct usbduxsub *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	dev_dbg(&this_usbduxsub->interface->dev,
		"comedi%d: ao_insn_write\n", dev->minor);

	down(&this_usbduxsub->sem);
	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}
	if (this_usbduxsub->ao_cmd_running) {
		dev_err(&this_usbduxsub->interface->dev,
			"comedi%d: ao_insn_write: "
			"ERROR: asynchronous ao_cmd is running\n", dev->minor);
		up(&this_usbduxsub->sem);
		return 0;
	}

	for (i = 0; i < insn->n; i++) {
		dev_dbg(&this_usbduxsub->interface->dev,
			"comedi%d: ao_insn_write: data[chan=%d,i=%d]=%d\n",
			dev->minor, chan, i, data[i]);

		/* number of channels: 1 */
		this_usbduxsub->dux_commands[1] = 1;
		/* one 16 bit value */
		*((int16_t *) (this_usbduxsub->dux_commands + 2)) =
			cpu_to_le16(data[i]);
		this_usbduxsub->outBuffer[chan] = data[i];
		/* channel number */
		this_usbduxsub->dux_commands[4] = (chan << 6);
		err = send_dux_commands(this_usbduxsub, SENDDACOMMANDS);
		if (err < 0) {
			up(&this_usbduxsub->sem);
			return err;
		}
	}
	up(&this_usbduxsub->sem);

	return i;
}

static int usbdux_ao_inttrig(struct comedi_device *dev, struct comedi_subdevice *s,
			     unsigned int trignum)
{
	int ret;
	struct usbduxsub *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);
	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}
	if (trignum != 0) {
		dev_err(&this_usbduxsub->interface->dev,
			"comedi%d: usbdux_ao_inttrig: invalid trignum\n",
			dev->minor);
		return -EINVAL;
	}
	if (!(this_usbduxsub->ao_cmd_running)) {
		this_usbduxsub->ao_cmd_running = 1;
		ret = usbduxsub_submit_OutURBs(this_usbduxsub);
		if (ret < 0) {
			dev_err(&this_usbduxsub->interface->dev,
				"comedi%d: usbdux_ao_inttrig: submitURB: "
				"err=%d\n", dev->minor, ret);
			this_usbduxsub->ao_cmd_running = 0;
			up(&this_usbduxsub->sem);
			return ret;
		}
		s->async->inttrig = NULL;
	} else {
		dev_err(&this_usbduxsub->interface->dev,
			"comedi%d: ao_inttrig but acqu is already running.\n",
			dev->minor);
	}
	up(&this_usbduxsub->sem);
	return 1;
}

static int usbdux_ao_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
			     struct comedi_cmd *cmd)
{
	int err = 0, tmp;
	struct usbduxsub *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	if (!(this_usbduxsub->probed))
		return -ENODEV;

	dev_dbg(&this_usbduxsub->interface->dev,
		"comedi%d: usbdux_ao_cmdtest\n", dev->minor);

	/* make sure triggers are valid */
	/* Only immediate triggers are allowed */
	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW | TRIG_INT;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	/* trigger should happen timed */
	tmp = cmd->scan_begin_src;
	/* just now we scan also in the high speed mode every frame */
	/* this is due to ehci driver limitations */
	if (0) {		/* (this_usbduxsub->high_speed) */
		/* start immidiately a new scan */
		/* the sampling rate is set by the coversion rate */
		cmd->scan_begin_src &= TRIG_FOLLOW;
	} else {
		/* start a new scan (output at once) with a timer */
		cmd->scan_begin_src &= TRIG_TIMER;
	}
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	/* scanning is continous */
	tmp = cmd->convert_src;
	/* we always output at 1kHz just now all channels at once */
	if (0) {		/* (this_usbduxsub->high_speed) */
		/*
		 * in usb-2.0 only one conversion it tranmitted but with 8kHz/n
		 */
		cmd->convert_src &= TRIG_TIMER;
	} else {
		/* all conversion events happen simultaneously with a rate of
		 * 1kHz/n */
		cmd->convert_src &= TRIG_NOW;
	}
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	/* issue a trigger when scan is finished and start a new scan */
	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	/* trigger at the end of count events or not, stop condition or not */
	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/*
	 * step 2: make sure trigger sources are unique and mutually compatible
	 * note that mutual compatiblity is not an issue here
	 */
	if (cmd->scan_begin_src != TRIG_FOLLOW &&
		cmd->scan_begin_src != TRIG_EXT &&
		cmd->scan_begin_src != TRIG_TIMER)
		err++;
	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		/* internal trigger */
		if (cmd->scan_begin_arg != 0) {
			cmd->scan_begin_arg = 0;
			err++;
		}
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		/* timer */
		if (cmd->scan_begin_arg < 1000000) {
			cmd->scan_begin_arg = 1000000;
			err++;
		}
	}
	/* not used now, is for later use */
	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < 125000) {
			cmd->convert_arg = 125000;
			err++;
		}
	}

	/* the same argument */
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	if (cmd->stop_src == TRIG_COUNT) {
		/* any count is allowed */
	} else {
		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	dev_dbg(&this_usbduxsub->interface->dev, "comedi%d: err=%d, "
		"scan_begin_src=%d, scan_begin_arg=%d, convert_src=%d, "
		"convert_arg=%d\n", dev->minor, err, cmd->scan_begin_src,
		cmd->scan_begin_arg, cmd->convert_src, cmd->convert_arg);

	if (err)
		return 3;

	return 0;
}

static int usbdux_ao_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int chan, gain;
	int i, ret;
	struct usbduxsub *this_usbduxsub = dev->private;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);
	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}
	dev_dbg(&this_usbduxsub->interface->dev,
		"comedi%d: %s\n", dev->minor, __func__);

	/* set current channel of the running aquisition to zero */
	s->async->cur_chan = 0;
	for (i = 0; i < cmd->chanlist_len; ++i) {
		chan = CR_CHAN(cmd->chanlist[i]);
		gain = CR_RANGE(cmd->chanlist[i]);
		if (i >= NUMOUTCHANNELS) {
			dev_err(&this_usbduxsub->interface->dev,
				"comedi%d: %s: channel list too long\n",
				dev->minor, __func__);
			break;
		}
		this_usbduxsub->dac_commands[i] = (chan << 6);
		dev_dbg(&this_usbduxsub->interface->dev,
			"comedi%d: dac command for ch %d is %x\n",
			dev->minor, i, this_usbduxsub->dac_commands[i]);
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
		dev_dbg(&this_usbduxsub->interface->dev,
			"comedi%d: scan_begin_src=%d, scan_begin_arg=%d, "
			"convert_src=%d, convert_arg=%d\n", dev->minor,
			cmd->scan_begin_src, cmd->scan_begin_arg,
			cmd->convert_src, cmd->convert_arg);
		dev_dbg(&this_usbduxsub->interface->dev,
			"comedi%d: ao_timer=%d (ms)\n",
			dev->minor, this_usbduxsub->ao_timer);
		if (this_usbduxsub->ao_timer < 1) {
			dev_err(&this_usbduxsub->interface->dev,
				"comedi%d: usbdux: ao_timer=%d, "
				"scan_begin_arg=%d. "
				"Not properly tested by cmdtest?\n",
				dev->minor, this_usbduxsub->ao_timer,
				cmd->scan_begin_arg);
			up(&this_usbduxsub->sem);
			return -EINVAL;
		}
	}
	this_usbduxsub->ao_counter = this_usbduxsub->ao_timer;

	if (cmd->stop_src == TRIG_COUNT) {
		/* not continous */
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
		/* continous aquisition */
		this_usbduxsub->ao_continous = 1;
		this_usbduxsub->ao_sample_count = 0;
	}

	if (cmd->start_src == TRIG_NOW) {
		/* enable this acquisition operation */
		this_usbduxsub->ao_cmd_running = 1;
		ret = usbduxsub_submit_OutURBs(this_usbduxsub);
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

static int usbdux_dio_insn_config(struct comedi_device *dev, struct comedi_subdevice *s,
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
			(s->
			io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		break;
	default:
		return -EINVAL;
		break;
	}
	/* we don't tell the firmware here as it would take 8 frames */
	/* to submit the information. We do it in the insn_bits. */
	return insn->n;
}

static int usbdux_dio_insn_bits(struct comedi_device *dev, struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{

	struct usbduxsub *this_usbduxsub = dev->private;
	int err;

	if (!this_usbduxsub)
		return -EFAULT;


	if (insn->n != 2)
		return -EINVAL;

	down(&this_usbduxsub->sem);

	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	s->state &= ~data[0];
	s->state |= data[0] & data[1];
	this_usbduxsub->dux_commands[1] = s->io_bits;
	this_usbduxsub->dux_commands[2] = s->state;

	/* This command also tells the firmware to return */
	/* the digital input lines */
	err = send_dux_commands(this_usbduxsub, SENDDIOBITSCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}
	err = receive_dux_commands(this_usbduxsub, SENDDIOBITSCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}

	data[1] = le16_to_cpu(this_usbduxsub->insnBuffer[1]);
	up(&this_usbduxsub->sem);
	return 2;
}

/* reads the 4 counters, only two are used just now */
static int usbdux_counter_read(struct comedi_device *dev, struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct usbduxsub *this_usbduxsub = dev->private;
	int chan = insn->chanspec;
	int err;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);

	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}

	err = send_dux_commands(this_usbduxsub, READCOUNTERCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}

	err = receive_dux_commands(this_usbduxsub, READCOUNTERCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}

	data[0] = le16_to_cpu(this_usbduxsub->insnBuffer[chan + 1]);
	up(&this_usbduxsub->sem);
	return 1;
}

static int usbdux_counter_write(struct comedi_device *dev, struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	struct usbduxsub *this_usbduxsub = dev->private;
	int err;

	if (!this_usbduxsub)
		return -EFAULT;

	down(&this_usbduxsub->sem);

	if (!(this_usbduxsub->probed)) {
		up(&this_usbduxsub->sem);
		return -ENODEV;
	}

	this_usbduxsub->dux_commands[1] = insn->chanspec;
	*((int16_t *) (this_usbduxsub->dux_commands + 2)) = cpu_to_le16(*data);

	err = send_dux_commands(this_usbduxsub, WRITECOUNTERCOMMAND);
	if (err < 0) {
		up(&this_usbduxsub->sem);
		return err;
	}

	up(&this_usbduxsub->sem);

	return 1;
}

static int usbdux_counter_config(struct comedi_device *dev, struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	/* nothing to do so far */
	return 2;
}

/***********************************/
/* PWM */

static int usbduxsub_unlink_PwmURBs(struct usbduxsub *usbduxsub_tmp)
{
	int err = 0;

	if (usbduxsub_tmp && usbduxsub_tmp->urbPwm) {
		if (usbduxsub_tmp->urbPwm)
			usb_kill_urb(usbduxsub_tmp->urbPwm);
		dev_dbg(&usbduxsub_tmp->interface->dev,
			"comedi: unlinked PwmURB: res=%d\n", err);
	}
	return err;
}

/* This cancels a running acquisition operation
 * in any context.
 */
static int usbdux_pwm_stop(struct usbduxsub *this_usbduxsub, int do_unlink)
{
	int ret = 0;

	if (!this_usbduxsub)
		return -EFAULT;

	dev_dbg(&this_usbduxsub->interface->dev, "comedi: %s\n", __func__);
	if (do_unlink)
		ret = usbduxsub_unlink_PwmURBs(this_usbduxsub);


	this_usbduxsub->pwm_cmd_running = 0;

	return ret;
}

/* force unlink - is called by comedi */
static int usbdux_pwm_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct usbduxsub *this_usbduxsub = dev->private;
	int res = 0;

	/* unlink only if it is really running */
	res = usbdux_pwm_stop(this_usbduxsub, this_usbduxsub->pwm_cmd_running);

	dev_dbg(&this_usbduxsub->interface->dev,
		"comedi %d: sending pwm off command to the usb device.\n",
		dev->minor);
	res = send_dux_commands(this_usbduxsub, SENDPWMOFF);
	if (res < 0)
		return res;

	return res;
}

static void usbduxsub_pwm_irq(struct urb *urb)
{
	int ret;
	struct usbduxsub *this_usbduxsub;
	struct comedi_device *this_comedidev;
	struct comedi_subdevice *s;

	/* printk(KERN_DEBUG "PWM: IRQ\n"); */

	/* the context variable points to the subdevice */
	this_comedidev = urb->context;
	/* the private structure of the subdevice is struct usbduxsub */
	this_usbduxsub = this_comedidev->private;

	s = this_comedidev->subdevices + SUBDEV_DA;

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
		if (this_usbduxsub->pwm_cmd_running)
			usbdux_pwm_stop(this_usbduxsub, 0);

		return;

	default:
		/* a real error */
		if (this_usbduxsub->pwm_cmd_running) {
			dev_err(&this_usbduxsub->interface->dev,
				"comedi_: Non-zero urb status received in "
				"pwm intr context: %d\n", urb->status);
			usbdux_pwm_stop(this_usbduxsub, 0);
		}
		return;
	}

	/* are we actually running? */
	if (!(this_usbduxsub->pwm_cmd_running))
		return;

	urb->transfer_buffer_length = this_usbduxsub->sizePwmBuf;
	urb->dev = this_usbduxsub->usbdev;
	urb->status = 0;
	if (this_usbduxsub->pwm_cmd_running) {
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if (ret < 0) {
			dev_err(&this_usbduxsub->interface->dev,
				"comedi_: pwm urb resubm failed in int-cont. "
				"ret=%d", ret);
			if (ret == EL2NSYNC)
				dev_err(&this_usbduxsub->interface->dev,
					"buggy USB host controller or bug in "
					"IRQ handling!\n");

			/* don't do an unlink here */
			usbdux_pwm_stop(this_usbduxsub, 0);
		}
	}
}

static int usbduxsub_submit_PwmURBs(struct usbduxsub *usbduxsub)
{
	int errFlag;

	if (!usbduxsub)
		return -EFAULT;

	dev_dbg(&usbduxsub->interface->dev, "comedi_: submitting pwm-urb\n");

	/* in case of a resubmission after an unlink... */
	usb_fill_bulk_urb(usbduxsub->urbPwm,
		usbduxsub->usbdev,
		usb_sndbulkpipe(usbduxsub->usbdev, PWM_EP),
		usbduxsub->urbPwm->transfer_buffer,
		usbduxsub->sizePwmBuf, usbduxsub_pwm_irq, usbduxsub->comedidev);

	errFlag = usb_submit_urb(usbduxsub->urbPwm, GFP_ATOMIC);
	if (errFlag) {
		dev_err(&usbduxsub->interface->dev,
			"comedi_: usbdux: pwm: usb_submit_urb error %d\n",
			errFlag);
		return errFlag;
	}
	return 0;
}

static int usbdux_pwm_period(struct comedi_device *dev, struct comedi_subdevice *s,
			     unsigned int period)
{
	struct usbduxsub *this_usbduxsub = dev->private;
	int fx2delay = 255;

	if (period < MIN_PWM_PERIOD) {
		dev_err(&this_usbduxsub->interface->dev,
			"comedi%d: illegal period setting for pwm.\n",
			dev->minor);
		return -EAGAIN;
	} else {
		fx2delay = period / ((int)(6*512*(1.0/0.033))) - 6;
		if (fx2delay > 255) {
			dev_err(&this_usbduxsub->interface->dev,
				"comedi%d: period %d for pwm is too low.\n",
			       dev->minor, period);
			return -EAGAIN;
		}
	}
	this_usbduxsub->pwmDelay = fx2delay;
	this_usbduxsub->pwmPeriod = period;
	dev_dbg(&this_usbduxsub->interface->dev, "%s: frequ=%d, period=%d\n",
		__func__, period, fx2delay);
	return 0;
}

/* is called from insn so there's no need to do all the sanity checks */
static int usbdux_pwm_start(struct comedi_device *dev, struct comedi_subdevice *s)
{
	int ret, i;
	struct usbduxsub *this_usbduxsub = dev->private;

	dev_dbg(&this_usbduxsub->interface->dev, "comedi%d: %s\n",
		dev->minor, __func__);

	if (this_usbduxsub->pwm_cmd_running) {
		/* already running */
		return 0;
	}

	this_usbduxsub->dux_commands[1] = ((int8_t) this_usbduxsub->pwmDelay);
	ret = send_dux_commands(this_usbduxsub, SENDPWMON);
	if (ret < 0)
		return ret;

	/* initalise the buffer */
	for (i = 0; i < this_usbduxsub->sizePwmBuf; i++)
		((char *)(this_usbduxsub->urbPwm->transfer_buffer))[i] = 0;

	this_usbduxsub->pwm_cmd_running = 1;
	ret = usbduxsub_submit_PwmURBs(this_usbduxsub);
	if (ret < 0) {
		this_usbduxsub->pwm_cmd_running = 0;
		return ret;
	}
	return 0;
}

/* generates the bit pattern for PWM with the optional sign bit */
static int usbdux_pwm_pattern(struct comedi_device *dev, struct comedi_subdevice *s,
			      int channel, unsigned int value, unsigned int sign)
{
	struct usbduxsub *this_usbduxsub = dev->private;
	int i, szbuf;
	char *pBuf;
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
	szbuf = this_usbduxsub->sizePwmBuf;
	pBuf = (char *)(this_usbduxsub->urbPwm->transfer_buffer);
	for (i = 0; i < szbuf; i++) {
		c = *pBuf;
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
		*(pBuf++) = c;
	}
	return 1;
}

static int usbdux_pwm_write(struct comedi_device *dev, struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct usbduxsub *this_usbduxsub = dev->private;

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
	return usbdux_pwm_pattern(dev, s, CR_CHAN(insn->chanspec),
				  data[0], 0);
}

static int usbdux_pwm_read(struct comedi_device *x1, struct comedi_subdevice *x2,
			   struct comedi_insn *x3, unsigned int *x4)
{
	/* not needed */
	return -EINVAL;
};

/* switches on/off PWM */
static int usbdux_pwm_config(struct comedi_device *dev, struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	struct usbduxsub *this_usbduxsub = dev->private;
	switch (data[0]) {
	case INSN_CONFIG_ARM:
		/* switch it on */
		dev_dbg(&this_usbduxsub->interface->dev,
			"comedi%d: %s: pwm on\n", dev->minor, __func__);
		/*
		 * if not zero the PWM is limited to a certain time which is
		 * not supported here
		 */
		if (data[1] != 0)
			return -EINVAL;
		return usbdux_pwm_start(dev, s);
	case INSN_CONFIG_DISARM:
		dev_dbg(&this_usbduxsub->interface->dev,
			"comedi%d: %s: pwm off\n", dev->minor, __func__);
		return usbdux_pwm_cancel(dev, s);
	case INSN_CONFIG_GET_PWM_STATUS:
		/*
		 * to check if the USB transmission has failed or in case PWM
		 * was limited to n cycles to check if it has terminated
		 */
		data[1] = this_usbduxsub->pwm_cmd_running;
		return 0;
	case INSN_CONFIG_PWM_SET_PERIOD:
		dev_dbg(&this_usbduxsub->interface->dev,
			"comedi%d: %s: setting period\n", dev->minor, __func__);
		return usbdux_pwm_period(dev, s, data[1]);
	case INSN_CONFIG_PWM_GET_PERIOD:
		data[1] = this_usbduxsub->pwmPeriod;
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

static void tidy_up(struct usbduxsub *usbduxsub_tmp)
{
	int i;

	if (!usbduxsub_tmp)
		return;
	dev_dbg(&usbduxsub_tmp->interface->dev, "comedi_: tiding up\n");

	/* shows the usb subsystem that the driver is down */
	if (usbduxsub_tmp->interface)
		usb_set_intfdata(usbduxsub_tmp->interface, NULL);

	usbduxsub_tmp->probed = 0;

	if (usbduxsub_tmp->urbIn) {
		if (usbduxsub_tmp->ai_cmd_running) {
			usbduxsub_tmp->ai_cmd_running = 0;
			usbduxsub_unlink_InURBs(usbduxsub_tmp);
		}
		for (i = 0; i < usbduxsub_tmp->numOfInBuffers; i++) {
			kfree(usbduxsub_tmp->urbIn[i]->transfer_buffer);
			usbduxsub_tmp->urbIn[i]->transfer_buffer = NULL;
			usb_kill_urb(usbduxsub_tmp->urbIn[i]);
			usb_free_urb(usbduxsub_tmp->urbIn[i]);
			usbduxsub_tmp->urbIn[i] = NULL;
		}
		kfree(usbduxsub_tmp->urbIn);
		usbduxsub_tmp->urbIn = NULL;
	}
	if (usbduxsub_tmp->urbOut) {
		if (usbduxsub_tmp->ao_cmd_running) {
			usbduxsub_tmp->ao_cmd_running = 0;
			usbduxsub_unlink_OutURBs(usbduxsub_tmp);
		}
		for (i = 0; i < usbduxsub_tmp->numOfOutBuffers; i++) {
			if (usbduxsub_tmp->urbOut[i]->transfer_buffer) {
				kfree(usbduxsub_tmp->urbOut[i]->
					transfer_buffer);
				usbduxsub_tmp->urbOut[i]->transfer_buffer =
					NULL;
			}
			if (usbduxsub_tmp->urbOut[i]) {
				usb_kill_urb(usbduxsub_tmp->urbOut[i]);
				usb_free_urb(usbduxsub_tmp->urbOut[i]);
				usbduxsub_tmp->urbOut[i] = NULL;
			}
		}
		kfree(usbduxsub_tmp->urbOut);
		usbduxsub_tmp->urbOut = NULL;
	}
	if (usbduxsub_tmp->urbPwm) {
		if (usbduxsub_tmp->pwm_cmd_running) {
			usbduxsub_tmp->pwm_cmd_running = 0;
			usbduxsub_unlink_PwmURBs(usbduxsub_tmp);
		}
		kfree(usbduxsub_tmp->urbPwm->transfer_buffer);
		usbduxsub_tmp->urbPwm->transfer_buffer = NULL;
		usb_kill_urb(usbduxsub_tmp->urbPwm);
		usb_free_urb(usbduxsub_tmp->urbPwm);
		usbduxsub_tmp->urbPwm = NULL;
	}
	kfree(usbduxsub_tmp->inBuffer);
	usbduxsub_tmp->inBuffer = NULL;
	kfree(usbduxsub_tmp->insnBuffer);
	usbduxsub_tmp->insnBuffer = NULL;
	kfree(usbduxsub_tmp->inBuffer);
	usbduxsub_tmp->inBuffer = NULL;
	kfree(usbduxsub_tmp->dac_commands);
	usbduxsub_tmp->dac_commands = NULL;
	kfree(usbduxsub_tmp->dux_commands);
	usbduxsub_tmp->dux_commands = NULL;
	usbduxsub_tmp->ai_cmd_running = 0;
	usbduxsub_tmp->ao_cmd_running = 0;
	usbduxsub_tmp->pwm_cmd_running = 0;
}

static unsigned hex2unsigned(char *h)
{
	unsigned hi, lo;

	if (h[0] > '9')
		hi = h[0] - 'A' + 0x0a;
	else
		hi = h[0] - '0';

	if (h[1] > '9')
		lo = h[1] - 'A' + 0x0a;
	else
		lo = h[1] - '0';

	return hi * 0x10 + lo;
}

/* for FX2 */
#define FIRMWARE_MAX_LEN 0x2000

/* taken from David Brownell's fxload and adjusted for this driver */
static int read_firmware(struct usbduxsub *usbduxsub, const void *firmwarePtr,
			 long size)
{
	struct device *dev = &usbduxsub->interface->dev;
	int i = 0;
	unsigned char *fp = (char *)firmwarePtr;
	unsigned char *firmwareBinary;
	int res = 0;
	int maxAddr = 0;

	firmwareBinary = kzalloc(FIRMWARE_MAX_LEN, GFP_KERNEL);
	if (!firmwareBinary) {
		dev_err(dev, "comedi_: mem alloc for firmware failed\n");
		return -ENOMEM;
	}

	for (;;) {
		char buf[256], *cp;
		char type;
		int len;
		int idx, off;
		int j = 0;

		/* get one line */
		while ((i < size) && (fp[i] != 13) && (fp[i] != 10)) {
			buf[j] = fp[i];
			i++;
			j++;
			if (j >= sizeof(buf)) {
				dev_err(dev, "comedi_: bogus firmware file!\n");
				kfree(firmwareBinary);
				return -1;
			}
		}
		/* get rid of LF/CR/... */
		while ((i < size) && ((fp[i] == 13) || (fp[i] == 10)
				|| (fp[i] == 0))) {
			i++;
		}

		buf[j] = 0;
		/* dev_dbg(dev, "comedi_: buf=%s\n", buf); */

		/*
		 * EXTENSION:
		 * "# comment-till-end-of-line", for copyrights etc
		 */
		if (buf[0] == '#')
			continue;

		if (buf[0] != ':') {
			dev_err(dev, "comedi_: upload: not an ihex record: %s",
				buf);
			kfree(firmwareBinary);
			return -EFAULT;
		}

		/* Read the length field (up to 16 bytes) */
		len = hex2unsigned(buf + 1);

		/* Read the target offset */
		off = (hex2unsigned(buf + 3) * 0x0100) + hex2unsigned(buf + 5);

		if ((off + len) > maxAddr)
			maxAddr = off + len;


		if (maxAddr >= FIRMWARE_MAX_LEN) {
			dev_err(dev, "comedi_: firmware upload goes "
				"beyond FX2 RAM boundaries.\n");
			kfree(firmwareBinary);
			return -EFAULT;
		}
		/* dev_dbg(dev, "comedi_: off=%x, len=%x:\n", off, len); */

		/* Read the record type */
		type = hex2unsigned(buf + 7);

		/* If this is an EOF record, then make it so. */
		if (type == 1)
			break;


		if (type != 0) {
			dev_err(dev, "comedi_: unsupported record type: %u\n",
				type);
			kfree(firmwareBinary);
			return -EFAULT;
		}

		for (idx = 0, cp = buf + 9; idx < len; idx += 1, cp += 2) {
			firmwareBinary[idx + off] = hex2unsigned(cp);
			/*printk("%02x ",firmwareBinary[idx+off]); */
		}
		/*printk("\n"); */

		if (i >= size) {
			dev_err(dev, "comedi_: unexpected end of hex file\n");
			break;
		}

	}
	res = firmwareUpload(usbduxsub, firmwareBinary, maxAddr + 1);
	kfree(firmwareBinary);
	return res;
}

static void usbdux_firmware_request_complete_handler(const struct firmware *fw,
						     void *context)
{
	struct usbduxsub *usbduxsub_tmp = context;
	struct usb_device *usbdev = usbduxsub_tmp->usbdev;
	int ret;

	if (fw == NULL) {
		dev_err(&usbdev->dev,
			"Firmware complete handler without firmware!\n");
		return;
	}

	/*
	 * we need to upload the firmware here because fw will be
	 * freed once we've left this function
	 */
	ret = read_firmware(usbduxsub_tmp, fw->data, fw->size);

	if (ret) {
		dev_err(&usbdev->dev,
			"Could not upload firmware (err=%d)\n",
			ret);
		return;
	}
	comedi_usb_auto_config(usbdev, BOARDNAME);
}

/* allocate memory for the urbs and initialise them */
static int usbduxsub_probe(struct usb_interface *uinterf,
			   const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(uinterf);
	struct device *dev = &uinterf->dev;
	int i;
	int index;
	int ret;

	dev_dbg(dev, "comedi_: usbdux_: "
		"finding a free structure for the usb-device\n");

	down(&start_stop_sem);
	/* look for a free place in the usbdux array */
	index = -1;
	for (i = 0; i < NUMUSBDUX; i++) {
		if (!(usbduxsub[i].probed)) {
			index = i;
			break;
		}
	}

	/* no more space */
	if (index == -1) {
		dev_err(dev, "Too many usbdux-devices connected.\n");
		up(&start_stop_sem);
		return -EMFILE;
	}
	dev_dbg(dev, "comedi_: usbdux: "
		"usbduxsub[%d] is ready to connect to comedi.\n", index);

	init_MUTEX(&(usbduxsub[index].sem));
	/* save a pointer to the usb device */
	usbduxsub[index].usbdev = udev;

	/* 2.6: save the interface itself */
	usbduxsub[index].interface = uinterf;
	/* get the interface number from the interface */
	usbduxsub[index].ifnum = uinterf->altsetting->desc.bInterfaceNumber;
	/* hand the private data over to the usb subsystem */
	/* will be needed for disconnect */
	usb_set_intfdata(uinterf, &(usbduxsub[index]));

	dev_dbg(dev, "comedi_: usbdux: ifnum=%d\n", usbduxsub[index].ifnum);

	/* test if it is high speed (USB 2.0) */
	usbduxsub[index].high_speed =
		(usbduxsub[index].usbdev->speed == USB_SPEED_HIGH);

	/* create space for the commands of the DA converter */
	usbduxsub[index].dac_commands = kzalloc(NUMOUTCHANNELS, GFP_KERNEL);
	if (!usbduxsub[index].dac_commands) {
		dev_err(dev, "comedi_: usbdux: "
			"error alloc space for dac commands\n");
		tidy_up(&(usbduxsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	/* create space for the commands going to the usb device */
	usbduxsub[index].dux_commands = kzalloc(SIZEOFDUXBUFFER, GFP_KERNEL);
	if (!usbduxsub[index].dux_commands) {
		dev_err(dev, "comedi_: usbdux: "
			"error alloc space for dac commands\n");
		tidy_up(&(usbduxsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	/* create space for the in buffer and set it to zero */
	usbduxsub[index].inBuffer = kzalloc(SIZEINBUF, GFP_KERNEL);
	if (!(usbduxsub[index].inBuffer)) {
		dev_err(dev, "comedi_: usbdux: "
			"could not alloc space for inBuffer\n");
		tidy_up(&(usbduxsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	/* create space of the instruction buffer */
	usbduxsub[index].insnBuffer = kzalloc(SIZEINSNBUF, GFP_KERNEL);
	if (!(usbduxsub[index].insnBuffer)) {
		dev_err(dev, "comedi_: usbdux: "
			"could not alloc space for insnBuffer\n");
		tidy_up(&(usbduxsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	/* create space for the outbuffer */
	usbduxsub[index].outBuffer = kzalloc(SIZEOUTBUF, GFP_KERNEL);
	if (!(usbduxsub[index].outBuffer)) {
		dev_err(dev, "comedi_: usbdux: "
			"could not alloc space for outBuffer\n");
		tidy_up(&(usbduxsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	/* setting to alternate setting 3: enabling iso ep and bulk ep. */
	i = usb_set_interface(usbduxsub[index].usbdev,
			      usbduxsub[index].ifnum, 3);
	if (i < 0) {
		dev_err(dev, "comedi_: usbdux%d: "
			"could not set alternate setting 3 in high speed.\n",
			index);
		tidy_up(&(usbduxsub[index]));
		up(&start_stop_sem);
		return -ENODEV;
	}
	if (usbduxsub[index].high_speed)
		usbduxsub[index].numOfInBuffers = NUMOFINBUFFERSHIGH;
	else
		usbduxsub[index].numOfInBuffers = NUMOFINBUFFERSFULL;

	usbduxsub[index].urbIn =
		kzalloc(sizeof(struct urb *) * usbduxsub[index].numOfInBuffers,
		GFP_KERNEL);
	if (!(usbduxsub[index].urbIn)) {
		dev_err(dev, "comedi_: usbdux: Could not alloc. urbIn array\n");
		tidy_up(&(usbduxsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	for (i = 0; i < usbduxsub[index].numOfInBuffers; i++) {
		/* one frame: 1ms */
		usbduxsub[index].urbIn[i] = usb_alloc_urb(1, GFP_KERNEL);
		if (usbduxsub[index].urbIn[i] == NULL) {
			dev_err(dev, "comedi_: usbdux%d: "
				"Could not alloc. urb(%d)\n", index, i);
			tidy_up(&(usbduxsub[index]));
			up(&start_stop_sem);
			return -ENOMEM;
		}
		usbduxsub[index].urbIn[i]->dev = usbduxsub[index].usbdev;
		/* will be filled later with a pointer to the comedi-device */
		/* and ONLY then the urb should be submitted */
		usbduxsub[index].urbIn[i]->context = NULL;
		usbduxsub[index].urbIn[i]->pipe =
			usb_rcvisocpipe(usbduxsub[index].usbdev, ISOINEP);
		usbduxsub[index].urbIn[i]->transfer_flags = URB_ISO_ASAP;
		usbduxsub[index].urbIn[i]->transfer_buffer =
			kzalloc(SIZEINBUF, GFP_KERNEL);
		if (!(usbduxsub[index].urbIn[i]->transfer_buffer)) {
			dev_err(dev, "comedi_: usbdux%d: "
				"could not alloc. transb.\n", index);
			tidy_up(&(usbduxsub[index]));
			up(&start_stop_sem);
			return -ENOMEM;
		}
		usbduxsub[index].urbIn[i]->complete = usbduxsub_ai_IsocIrq;
		usbduxsub[index].urbIn[i]->number_of_packets = 1;
		usbduxsub[index].urbIn[i]->transfer_buffer_length = SIZEINBUF;
		usbduxsub[index].urbIn[i]->iso_frame_desc[0].offset = 0;
		usbduxsub[index].urbIn[i]->iso_frame_desc[0].length = SIZEINBUF;
	}

	/* out */
	if (usbduxsub[index].high_speed)
		usbduxsub[index].numOfOutBuffers = NUMOFOUTBUFFERSHIGH;
	else
		usbduxsub[index].numOfOutBuffers = NUMOFOUTBUFFERSFULL;

	usbduxsub[index].urbOut =
		kzalloc(sizeof(struct urb *) * usbduxsub[index].numOfOutBuffers,
		GFP_KERNEL);
	if (!(usbduxsub[index].urbOut)) {
		dev_err(dev, "comedi_: usbdux: "
			"Could not alloc. urbOut array\n");
		tidy_up(&(usbduxsub[index]));
		up(&start_stop_sem);
		return -ENOMEM;
	}
	for (i = 0; i < usbduxsub[index].numOfOutBuffers; i++) {
		/* one frame: 1ms */
		usbduxsub[index].urbOut[i] = usb_alloc_urb(1, GFP_KERNEL);
		if (usbduxsub[index].urbOut[i] == NULL) {
			dev_err(dev, "comedi_: usbdux%d: "
				"Could not alloc. urb(%d)\n", index, i);
			tidy_up(&(usbduxsub[index]));
			up(&start_stop_sem);
			return -ENOMEM;
		}
		usbduxsub[index].urbOut[i]->dev = usbduxsub[index].usbdev;
		/* will be filled later with a pointer to the comedi-device */
		/* and ONLY then the urb should be submitted */
		usbduxsub[index].urbOut[i]->context = NULL;
		usbduxsub[index].urbOut[i]->pipe =
			usb_sndisocpipe(usbduxsub[index].usbdev, ISOOUTEP);
		usbduxsub[index].urbOut[i]->transfer_flags = URB_ISO_ASAP;
		usbduxsub[index].urbOut[i]->transfer_buffer =
			kzalloc(SIZEOUTBUF, GFP_KERNEL);
		if (!(usbduxsub[index].urbOut[i]->transfer_buffer)) {
			dev_err(dev, "comedi_: usbdux%d: "
				"could not alloc. transb.\n", index);
			tidy_up(&(usbduxsub[index]));
			up(&start_stop_sem);
			return -ENOMEM;
		}
		usbduxsub[index].urbOut[i]->complete = usbduxsub_ao_IsocIrq;
		usbduxsub[index].urbOut[i]->number_of_packets = 1;
		usbduxsub[index].urbOut[i]->transfer_buffer_length = SIZEOUTBUF;
		usbduxsub[index].urbOut[i]->iso_frame_desc[0].offset = 0;
		usbduxsub[index].urbOut[i]->iso_frame_desc[0].length =
			SIZEOUTBUF;
		if (usbduxsub[index].high_speed) {
			/* uframes */
			usbduxsub[index].urbOut[i]->interval = 8;
		} else {
			/* frames */
			usbduxsub[index].urbOut[i]->interval = 1;
		}
	}

	/* pwm */
	if (usbduxsub[index].high_speed) {
		/* max bulk ep size in high speed */
		usbduxsub[index].sizePwmBuf = 512;
		usbduxsub[index].urbPwm = usb_alloc_urb(0, GFP_KERNEL);
		if (usbduxsub[index].urbPwm == NULL) {
			dev_err(dev, "comedi_: usbdux%d: "
				"Could not alloc. pwm urb\n", index);
			tidy_up(&(usbduxsub[index]));
			up(&start_stop_sem);
			return -ENOMEM;
		}
		usbduxsub[index].urbPwm->transfer_buffer =
			kzalloc(usbduxsub[index].sizePwmBuf, GFP_KERNEL);
		if (!(usbduxsub[index].urbPwm->transfer_buffer)) {
			dev_err(dev, "comedi_: usbdux%d: "
				"could not alloc. transb. for pwm\n", index);
			tidy_up(&(usbduxsub[index]));
			up(&start_stop_sem);
			return -ENOMEM;
		}
	} else {
		usbduxsub[index].urbPwm = NULL;
		usbduxsub[index].sizePwmBuf = 0;
	}

	usbduxsub[index].ai_cmd_running = 0;
	usbduxsub[index].ao_cmd_running = 0;
	usbduxsub[index].pwm_cmd_running = 0;

	/* we've reached the bottom of the function */
	usbduxsub[index].probed = 1;
	up(&start_stop_sem);

	ret = request_firmware_nowait(THIS_MODULE,
				      FW_ACTION_HOTPLUG,
				      "usbdux_firmware.hex",
				      &udev->dev,
				      usbduxsub + index,
				      usbdux_firmware_request_complete_handler);

	if (ret) {
		dev_err(dev, "Could not load firmware (err=%d)\n", ret);
		return ret;
	}

	dev_info(dev, "comedi_: usbdux%d "
		 "has been successfully initialised.\n", index);
	/* success */
	return 0;
}

static void usbduxsub_disconnect(struct usb_interface *intf)
{
	struct usbduxsub *usbduxsub_tmp = usb_get_intfdata(intf);
	struct usb_device *udev = interface_to_usbdev(intf);

	if (!usbduxsub_tmp) {
		dev_err(&intf->dev,
			"comedi_: disconnect called with null pointer.\n");
		return;
	}
	if (usbduxsub_tmp->usbdev != udev) {
		dev_err(&intf->dev,
			"comedi_: BUG! called with wrong ptr!!!\n");
		return;
	}
	comedi_usb_auto_unconfig(udev);
	down(&start_stop_sem);
	down(&usbduxsub_tmp->sem);
	tidy_up(usbduxsub_tmp);
	up(&usbduxsub_tmp->sem);
	up(&start_stop_sem);
	dev_dbg(&intf->dev, "comedi_: disconnected from the usb\n");
}

/* is called when comedi-config is called */
static int usbdux_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	int ret;
	int index;
	int i;
	struct usbduxsub *udev;

	struct comedi_subdevice *s = NULL;
	dev->private = NULL;

	down(&start_stop_sem);
	/* find a valid device which has been detected by the probe function of
	 * the usb */
	index = -1;
	for (i = 0; i < NUMUSBDUX; i++) {
		if ((usbduxsub[i].probed) && (!usbduxsub[i].attached)) {
			index = i;
			break;
		}
	}

	if (index < 0) {
		printk(KERN_ERR "comedi%d: usbdux: error: attach failed, no "
		       "usbdux devs connected to the usb bus.\n", dev->minor);
		up(&start_stop_sem);
		return -ENODEV;
	}

	udev = &usbduxsub[index];
	down(&udev->sem);
	/* pointer back to the corresponding comedi device */
	udev->comedidev = dev;

	/* trying to upload the firmware into the chip */
	if (comedi_aux_data(it->options, 0) &&
		it->options[COMEDI_DEVCONF_AUX_DATA_LENGTH]) {
		read_firmware(udev, comedi_aux_data(it->options, 0),
			      it->options[COMEDI_DEVCONF_AUX_DATA_LENGTH]);
	}

	dev->board_name = BOARDNAME;

	/* set number of subdevices */
	if (udev->high_speed) {
		/* with pwm */
		dev->n_subdevices = 5;
	} else {
		/* without pwm */
		dev->n_subdevices = 4;
	}

	/* allocate space for the subdevices */
	ret = alloc_subdevices(dev, dev->n_subdevices);
	if (ret < 0) {
		dev_err(&udev->interface->dev,
			"comedi%d: error alloc space for subdev\n", dev->minor);
		up(&start_stop_sem);
		return ret;
	}

	dev_info(&udev->interface->dev,
		"comedi%d: usb-device %d is attached to comedi.\n",
		dev->minor, index);
	/* private structure is also simply the usb-structure */
	dev->private = udev;

	/* the first subdevice is the A/D converter */
	s = dev->subdevices + SUBDEV_AD;
	/* the URBs get the comedi subdevice */
	/* which is responsible for reading */
	/* this is the subdevice which reads data */
	dev->read_subdev = s;
	/* the subdevice receives as private structure the */
	/* usb-structure */
	s->private = NULL;
	/* analog input */
	s->type = COMEDI_SUBD_AI;
	/* readable and ref is to ground */
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_CMD_READ;
	/* 8 channels */
	s->n_chan = 8;
	/* length of the channellist */
	s->len_chanlist = 8;
	/* callback functions */
	s->insn_read = usbdux_ai_insn_read;
	s->do_cmdtest = usbdux_ai_cmdtest;
	s->do_cmd = usbdux_ai_cmd;
	s->cancel = usbdux_ai_cancel;
	/* max value from the A/D converter (12bit) */
	s->maxdata = 0xfff;
	/* range table to convert to physical units */
	s->range_table = (&range_usbdux_ai_range);

	/* analog out */
	s = dev->subdevices + SUBDEV_DA;
	/* analog out */
	s->type = COMEDI_SUBD_AO;
	/* backward pointer */
	dev->write_subdev = s;
	/* the subdevice receives as private structure the */
	/* usb-structure */
	s->private = NULL;
	/* are writable */
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_CMD_WRITE;
	/* 4 channels */
	s->n_chan = 4;
	/* length of the channellist */
	s->len_chanlist = 4;
	/* 12 bit resolution */
	s->maxdata = 0x0fff;
	/* bipolar range */
	s->range_table = (&range_usbdux_ao_range);
	/* callback */
	s->do_cmdtest = usbdux_ao_cmdtest;
	s->do_cmd = usbdux_ao_cmd;
	s->cancel = usbdux_ao_cancel;
	s->insn_read = usbdux_ao_insn_read;
	s->insn_write = usbdux_ao_insn_write;

	/* digital I/O */
	s = dev->subdevices + SUBDEV_DIO;
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = (&range_digital);
	s->insn_bits = usbdux_dio_insn_bits;
	s->insn_config = usbdux_dio_insn_config;
	/* we don't use it */
	s->private = NULL;

	/* counter */
	s = dev->subdevices + SUBDEV_COUNTER;
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 4;
	s->maxdata = 0xFFFF;
	s->insn_read = usbdux_counter_read;
	s->insn_write = usbdux_counter_write;
	s->insn_config = usbdux_counter_config;

	if (udev->high_speed) {
		/* timer / pwm */
		s = dev->subdevices + SUBDEV_PWM;
		s->type = COMEDI_SUBD_PWM;
		s->subdev_flags = SDF_WRITABLE | SDF_PWM_HBRIDGE;
		s->n_chan = 8;
		/* this defines the max duty cycle resolution */
		s->maxdata = udev->sizePwmBuf;
		s->insn_write = usbdux_pwm_write;
		s->insn_read = usbdux_pwm_read;
		s->insn_config = usbdux_pwm_config;
		usbdux_pwm_period(dev, s, PWM_DEFAULT_PERIOD);
	}
	/* finally decide that it's attached */
	udev->attached = 1;

	up(&udev->sem);

	up(&start_stop_sem);

	dev_info(&udev->interface->dev, "comedi%d: attached to usbdux.\n",
		 dev->minor);

	return 0;
}

static int usbdux_detach(struct comedi_device *dev)
{
	struct usbduxsub *usbduxsub_tmp;

	if (!dev) {
		printk(KERN_ERR
			"comedi?: usbdux: detach without dev variable...\n");
		return -EFAULT;
	}

	usbduxsub_tmp = dev->private;
	if (!usbduxsub_tmp) {
		printk(KERN_ERR
			"comedi?: usbdux: detach without ptr to usbduxsub[]\n");
		return -EFAULT;
	}

	dev_dbg(&usbduxsub_tmp->interface->dev, "comedi%d: detach usb device\n",
		dev->minor);

	down(&usbduxsub_tmp->sem);
	/* Don't allow detach to free the private structure */
	/* It's one entry of of usbduxsub[] */
	dev->private = NULL;
	usbduxsub_tmp->attached = 0;
	usbduxsub_tmp->comedidev = NULL;
	dev_dbg(&usbduxsub_tmp->interface->dev,
		"comedi%d: detach: successfully removed\n", dev->minor);
	up(&usbduxsub_tmp->sem);
	return 0;
}

/* main driver struct */
static struct comedi_driver driver_usbdux = {
      .driver_name =	"usbdux",
      .module =		THIS_MODULE,
      .attach =		usbdux_attach,
      .detach =		usbdux_detach,
};

/* Table with the USB-devices: just now only testing IDs */
static struct usb_device_id usbduxsub_table[] = {
	{USB_DEVICE(0x13d8, 0x0001) },
	{USB_DEVICE(0x13d8, 0x0002) },
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, usbduxsub_table);

/* The usbduxsub-driver */
static struct usb_driver usbduxsub_driver = {
      .name =		BOARDNAME,
      .probe =		usbduxsub_probe,
      .disconnect =	usbduxsub_disconnect,
      .id_table =	usbduxsub_table,
};

/* Can't use the nice macro as I have also to initialise the USB */
/* subsystem: */
/* registering the usb-system _and_ the comedi-driver */
static int __init init_usbdux(void)
{
	printk(KERN_INFO KBUILD_MODNAME ": "
	       DRIVER_VERSION ":" DRIVER_DESC "\n");
	usb_register(&usbduxsub_driver);
	comedi_driver_register(&driver_usbdux);
	return 0;
}

/* deregistering the comedi driver and the usb-subsystem */
static void __exit exit_usbdux(void)
{
	comedi_driver_unregister(&driver_usbdux);
	usb_deregister(&usbduxsub_driver);
}

module_init(init_usbdux);
module_exit(exit_usbdux);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
