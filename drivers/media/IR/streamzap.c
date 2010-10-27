/*
 * Streamzap Remote Control driver
 *
 * Copyright (c) 2005 Christoph Bartelmus <lirc@bartelmus.de>
 * Copyright (c) 2010 Jarod Wilson <jarod@wilsonet.com>
 *
 * This driver was based on the work of Greg Wickham and Adrian
 * Dewhurst. It was substantially rewritten to support correct signal
 * gaps and now maintains a delay buffer, which is used to present
 * consistent timing behaviour to user space applications. Without the
 * delay buffer an ugly hack would be required in lircd, which can
 * cause sluggish signal decoding in certain situations.
 *
 * Ported to in-kernel ir-core interface by Jarod Wilson
 *
 * This driver is based on the USB skeleton driver packaged with the
 * kernel; copyright (C) 2001-2003 Greg Kroah-Hartman (greg@kroah.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <media/ir-core.h>

#define DRIVER_VERSION	"1.60"
#define DRIVER_NAME	"streamzap"
#define DRIVER_DESC	"Streamzap Remote Control driver"

#ifdef CONFIG_USB_DEBUG
static int debug = 1;
#else
static int debug;
#endif

#define USB_STREAMZAP_VENDOR_ID		0x0e9c
#define USB_STREAMZAP_PRODUCT_ID	0x0000

/* table of devices that work with this driver */
static struct usb_device_id streamzap_table[] = {
	/* Streamzap Remote Control */
	{ USB_DEVICE(USB_STREAMZAP_VENDOR_ID, USB_STREAMZAP_PRODUCT_ID) },
	/* Terminating entry */
	{ }
};

MODULE_DEVICE_TABLE(usb, streamzap_table);

#define STREAMZAP_PULSE_MASK 0xf0
#define STREAMZAP_SPACE_MASK 0x0f
#define STREAMZAP_TIMEOUT    0xff
#define STREAMZAP_RESOLUTION 256

/* number of samples buffered */
#define SZ_BUF_LEN 128

enum StreamzapDecoderState {
	PulseSpace,
	FullPulse,
	FullSpace,
	IgnorePulse
};

/* structure to hold our device specific stuff */
struct streamzap_ir {

	/* ir-core */
	struct ir_dev_props *props;
	struct ir_raw_event rawir;

	/* core device info */
	struct device *dev;
	struct input_dev *idev;

	/* usb */
	struct usb_device	*usbdev;
	struct usb_interface	*interface;
	struct usb_endpoint_descriptor *endpoint;
	struct urb		*urb_in;

	/* buffer & dma */
	unsigned char		*buf_in;
	dma_addr_t		dma_in;
	unsigned int		buf_in_len;

	/* timer used to support delay buffering */
	struct timer_list	delay_timer;
	bool			timer_running;
	spinlock_t		timer_lock;
	struct timer_list	flush_timer;
	bool			flush;

	/* delay buffer */
	struct kfifo fifo;
	bool fifo_initialized;

	/* track what state we're in */
	enum StreamzapDecoderState decoder_state;
	/* tracks whether we are currently receiving some signal */
	bool			idle;
	/* sum of signal lengths received since signal start */
	unsigned long		sum;
	/* start time of signal; necessary for gap tracking */
	struct timeval		signal_last;
	struct timeval		signal_start;
	/* bool			timeout_enabled; */

	char			name[128];
	char			phys[64];
};


/* local function prototypes */
static int streamzap_probe(struct usb_interface *interface,
			   const struct usb_device_id *id);
static void streamzap_disconnect(struct usb_interface *interface);
static void streamzap_callback(struct urb *urb);
static int streamzap_suspend(struct usb_interface *intf, pm_message_t message);
static int streamzap_resume(struct usb_interface *intf);

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver streamzap_driver = {
	.name =		DRIVER_NAME,
	.probe =	streamzap_probe,
	.disconnect =	streamzap_disconnect,
	.suspend =	streamzap_suspend,
	.resume =	streamzap_resume,
	.id_table =	streamzap_table,
};

static void streamzap_stop_timer(struct streamzap_ir *sz)
{
	unsigned long flags;

	spin_lock_irqsave(&sz->timer_lock, flags);
	if (sz->timer_running) {
		sz->timer_running = false;
		spin_unlock_irqrestore(&sz->timer_lock, flags);
		del_timer_sync(&sz->delay_timer);
	} else {
		spin_unlock_irqrestore(&sz->timer_lock, flags);
	}
}

static void streamzap_flush_timeout(unsigned long arg)
{
	struct streamzap_ir *sz = (struct streamzap_ir *)arg;

	dev_info(sz->dev, "%s: callback firing\n", __func__);

	/* finally start accepting data */
	sz->flush = false;
}

static void streamzap_delay_timeout(unsigned long arg)
{
	struct streamzap_ir *sz = (struct streamzap_ir *)arg;
	struct ir_raw_event rawir = { .pulse = false, .duration = 0 };
	unsigned long flags;
	int len, ret;
	static unsigned long delay;
	bool wake = false;

	/* deliver data every 10 ms */
	delay = msecs_to_jiffies(10);

	spin_lock_irqsave(&sz->timer_lock, flags);

	if (kfifo_len(&sz->fifo) > 0) {
		ret = kfifo_out(&sz->fifo, &rawir, sizeof(rawir));
		if (ret != sizeof(rawir))
			dev_err(sz->dev, "Problem w/kfifo_out...\n");
		ir_raw_event_store(sz->idev, &rawir);
		wake = true;
	}

	len = kfifo_len(&sz->fifo);
	if (len > 0) {
		while ((len < SZ_BUF_LEN / 2) &&
		       (len < SZ_BUF_LEN * sizeof(int))) {
			ret = kfifo_out(&sz->fifo, &rawir, sizeof(rawir));
			if (ret != sizeof(rawir))
				dev_err(sz->dev, "Problem w/kfifo_out...\n");
			ir_raw_event_store(sz->idev, &rawir);
			wake = true;
			len = kfifo_len(&sz->fifo);
		}
		if (sz->timer_running)
			mod_timer(&sz->delay_timer, jiffies + delay);

	} else {
		sz->timer_running = false;
	}

	if (wake)
		ir_raw_event_handle(sz->idev);

	spin_unlock_irqrestore(&sz->timer_lock, flags);
}

static void streamzap_flush_delay_buffer(struct streamzap_ir *sz)
{
	struct ir_raw_event rawir = { .pulse = false, .duration = 0 };
	bool wake = false;
	int ret;

	while (kfifo_len(&sz->fifo) > 0) {
		ret = kfifo_out(&sz->fifo, &rawir, sizeof(rawir));
		if (ret != sizeof(rawir))
			dev_err(sz->dev, "Problem w/kfifo_out...\n");
		ir_raw_event_store(sz->idev, &rawir);
		wake = true;
	}

	if (wake)
		ir_raw_event_handle(sz->idev);
}

static void sz_push(struct streamzap_ir *sz)
{
	struct ir_raw_event rawir = { .pulse = false, .duration = 0 };
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sz->timer_lock, flags);
	if (kfifo_len(&sz->fifo) >= sizeof(int) * SZ_BUF_LEN) {
		ret = kfifo_out(&sz->fifo, &rawir, sizeof(rawir));
		if (ret != sizeof(rawir))
			dev_err(sz->dev, "Problem w/kfifo_out...\n");
		ir_raw_event_store(sz->idev, &rawir);
	}

	kfifo_in(&sz->fifo, &sz->rawir, sizeof(rawir));

	if (!sz->timer_running) {
		sz->delay_timer.expires = jiffies + (HZ / 10);
		add_timer(&sz->delay_timer);
		sz->timer_running = true;
	}

	spin_unlock_irqrestore(&sz->timer_lock, flags);
}

static void sz_push_full_pulse(struct streamzap_ir *sz,
			       unsigned char value)
{
	if (sz->idle) {
		long deltv;

		sz->signal_last = sz->signal_start;
		do_gettimeofday(&sz->signal_start);

		deltv = sz->signal_start.tv_sec - sz->signal_last.tv_sec;
		sz->rawir.pulse = false;
		if (deltv > 15) {
			/* really long time */
			sz->rawir.duration = IR_MAX_DURATION;
		} else {
			sz->rawir.duration = (int)(deltv * 1000000 +
				sz->signal_start.tv_usec -
				sz->signal_last.tv_usec);
			sz->rawir.duration -= sz->sum;
			sz->rawir.duration *= 1000;
			sz->rawir.duration &= IR_MAX_DURATION;
		}
		dev_dbg(sz->dev, "ls %u\n", sz->rawir.duration);
		sz_push(sz);

		sz->idle = 0;
		sz->sum = 0;
	}

	sz->rawir.pulse = true;
	sz->rawir.duration = ((int) value) * STREAMZAP_RESOLUTION;
	sz->rawir.duration += STREAMZAP_RESOLUTION / 2;
	sz->sum += sz->rawir.duration;
	sz->rawir.duration *= 1000;
	sz->rawir.duration &= IR_MAX_DURATION;
	dev_dbg(sz->dev, "p %u\n", sz->rawir.duration);
	sz_push(sz);
}

static void sz_push_half_pulse(struct streamzap_ir *sz,
			       unsigned char value)
{
	sz_push_full_pulse(sz, (value & STREAMZAP_PULSE_MASK) >> 4);
}

static void sz_push_full_space(struct streamzap_ir *sz,
			       unsigned char value)
{
	sz->rawir.pulse = false;
	sz->rawir.duration = ((int) value) * STREAMZAP_RESOLUTION;
	sz->rawir.duration += STREAMZAP_RESOLUTION / 2;
	sz->sum += sz->rawir.duration;
	sz->rawir.duration *= 1000;
	dev_dbg(sz->dev, "s %u\n", sz->rawir.duration);
	sz_push(sz);
}

static void sz_push_half_space(struct streamzap_ir *sz,
			       unsigned long value)
{
	sz_push_full_space(sz, value & STREAMZAP_SPACE_MASK);
}

/**
 * streamzap_callback - usb IRQ handler callback
 *
 * This procedure is invoked on reception of data from
 * the usb remote.
 */
static void streamzap_callback(struct urb *urb)
{
	struct streamzap_ir *sz;
	unsigned int i;
	int len;
	#if 0
	static int timeout = (((STREAMZAP_TIMEOUT * STREAMZAP_RESOLUTION) &
				IR_MAX_DURATION) | 0x03000000);
	#endif

	if (!urb)
		return;

	sz = urb->context;
	len = urb->actual_length;

	switch (urb->status) {
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/*
		 * this urb is terminated, clean up.
		 * sz might already be invalid at this point
		 */
		dev_err(sz->dev, "urb terminated, status: %d\n", urb->status);
		return;
	default:
		break;
	}

	dev_dbg(sz->dev, "%s: received urb, len %d\n", __func__, len);
	if (!sz->flush) {
		for (i = 0; i < urb->actual_length; i++) {
			dev_dbg(sz->dev, "%d: %x\n", i,
				(unsigned char)sz->buf_in[i]);
			switch (sz->decoder_state) {
			case PulseSpace:
				if ((sz->buf_in[i] & STREAMZAP_PULSE_MASK) ==
				    STREAMZAP_PULSE_MASK) {
					sz->decoder_state = FullPulse;
					continue;
				} else if ((sz->buf_in[i] & STREAMZAP_SPACE_MASK)
					   == STREAMZAP_SPACE_MASK) {
					sz_push_half_pulse(sz, sz->buf_in[i]);
					sz->decoder_state = FullSpace;
					continue;
				} else {
					sz_push_half_pulse(sz, sz->buf_in[i]);
					sz_push_half_space(sz, sz->buf_in[i]);
				}
				break;
			case FullPulse:
				sz_push_full_pulse(sz, sz->buf_in[i]);
				sz->decoder_state = IgnorePulse;
				break;
			case FullSpace:
				if (sz->buf_in[i] == STREAMZAP_TIMEOUT) {
					sz->idle = 1;
					streamzap_stop_timer(sz);
					#if 0
					if (sz->timeout_enabled) {
						sz->rawir.pulse = false;
						sz->rawir.duration = timeout;
						sz->rawir.duration *= 1000;
						sz_push(sz);
					}
					#endif
					streamzap_flush_delay_buffer(sz);
				} else
					sz_push_full_space(sz, sz->buf_in[i]);
				sz->decoder_state = PulseSpace;
				break;
			case IgnorePulse:
				if ((sz->buf_in[i]&STREAMZAP_SPACE_MASK) ==
				    STREAMZAP_SPACE_MASK) {
					sz->decoder_state = FullSpace;
					continue;
				}
				sz_push_half_space(sz, sz->buf_in[i]);
				sz->decoder_state = PulseSpace;
				break;
			}
		}
	}

	usb_submit_urb(urb, GFP_ATOMIC);

	return;
}

static struct input_dev *streamzap_init_input_dev(struct streamzap_ir *sz)
{
	struct input_dev *idev;
	struct ir_dev_props *props;
	struct device *dev = sz->dev;
	int ret;

	idev = input_allocate_device();
	if (!idev) {
		dev_err(dev, "remote input dev allocation failed\n");
		goto idev_alloc_failed;
	}

	props = kzalloc(sizeof(struct ir_dev_props), GFP_KERNEL);
	if (!props) {
		dev_err(dev, "remote ir dev props allocation failed\n");
		goto props_alloc_failed;
	}

	snprintf(sz->name, sizeof(sz->name), "Streamzap PC Remote Infrared "
		 "Receiver (%04x:%04x)",
		 le16_to_cpu(sz->usbdev->descriptor.idVendor),
		 le16_to_cpu(sz->usbdev->descriptor.idProduct));

	idev->name = sz->name;
	usb_make_path(sz->usbdev, sz->phys, sizeof(sz->phys));
	strlcat(sz->phys, "/input0", sizeof(sz->phys));
	idev->phys = sz->phys;

	props->priv = sz;
	props->driver_type = RC_DRIVER_IR_RAW;
	/* FIXME: not sure about supported protocols, check on this */
	props->allowed_protos = IR_TYPE_RC5 | IR_TYPE_RC6;

	sz->props = props;

	ret = ir_input_register(idev, RC_MAP_RC5_STREAMZAP, props, DRIVER_NAME);
	if (ret < 0) {
		dev_err(dev, "remote input device register failed\n");
		goto irdev_failed;
	}

	return idev;

irdev_failed:
	kfree(props);
props_alloc_failed:
	input_free_device(idev);
idev_alloc_failed:
	return NULL;
}

static int streamzap_delay_buf_init(struct streamzap_ir *sz)
{
	int ret;

	ret = kfifo_alloc(&sz->fifo, sizeof(int) * SZ_BUF_LEN,
			  GFP_KERNEL);
	if (ret == 0)
		sz->fifo_initialized = 1;

	return ret;
}

static void streamzap_start_flush_timer(struct streamzap_ir *sz)
{
	sz->flush_timer.expires = jiffies + HZ;
	sz->flush = true;
	add_timer(&sz->flush_timer);

	sz->urb_in->dev = sz->usbdev;
	if (usb_submit_urb(sz->urb_in, GFP_ATOMIC))
		dev_err(sz->dev, "urb submit failed\n");
}

/**
 *	streamzap_probe
 *
 *	Called by usb-core to associated with a candidate device
 *	On any failure the return value is the ERROR
 *	On success return 0
 */
static int __devinit streamzap_probe(struct usb_interface *intf,
				     const struct usb_device_id *id)
{
	struct usb_device *usbdev = interface_to_usbdev(intf);
	struct usb_host_interface *iface_host;
	struct streamzap_ir *sz = NULL;
	char buf[63], name[128] = "";
	int retval = -ENOMEM;
	int pipe, maxp;

	/* Allocate space for device driver specific data */
	sz = kzalloc(sizeof(struct streamzap_ir), GFP_KERNEL);
	if (!sz)
		return -ENOMEM;

	sz->usbdev = usbdev;
	sz->interface = intf;

	/* Check to ensure endpoint information matches requirements */
	iface_host = intf->cur_altsetting;

	if (iface_host->desc.bNumEndpoints != 1) {
		dev_err(&intf->dev, "%s: Unexpected desc.bNumEndpoints (%d)\n",
			__func__, iface_host->desc.bNumEndpoints);
		retval = -ENODEV;
		goto free_sz;
	}

	sz->endpoint = &(iface_host->endpoint[0].desc);
	if ((sz->endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
	    != USB_DIR_IN) {
		dev_err(&intf->dev, "%s: endpoint doesn't match input device "
			"02%02x\n", __func__, sz->endpoint->bEndpointAddress);
		retval = -ENODEV;
		goto free_sz;
	}

	if ((sz->endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	    != USB_ENDPOINT_XFER_INT) {
		dev_err(&intf->dev, "%s: endpoint attributes don't match xfer "
			"02%02x\n", __func__, sz->endpoint->bmAttributes);
		retval = -ENODEV;
		goto free_sz;
	}

	pipe = usb_rcvintpipe(usbdev, sz->endpoint->bEndpointAddress);
	maxp = usb_maxpacket(usbdev, pipe, usb_pipeout(pipe));

	if (maxp == 0) {
		dev_err(&intf->dev, "%s: endpoint Max Packet Size is 0!?!\n",
			__func__);
		retval = -ENODEV;
		goto free_sz;
	}

	/* Allocate the USB buffer and IRQ URB */
	sz->buf_in = usb_alloc_coherent(usbdev, maxp, GFP_ATOMIC, &sz->dma_in);
	if (!sz->buf_in)
		goto free_sz;

	sz->urb_in = usb_alloc_urb(0, GFP_KERNEL);
	if (!sz->urb_in)
		goto free_buf_in;

	sz->dev = &intf->dev;
	sz->buf_in_len = maxp;

	if (usbdev->descriptor.iManufacturer
	    && usb_string(usbdev, usbdev->descriptor.iManufacturer,
			  buf, sizeof(buf)) > 0)
		strlcpy(name, buf, sizeof(name));

	if (usbdev->descriptor.iProduct
	    && usb_string(usbdev, usbdev->descriptor.iProduct,
			  buf, sizeof(buf)) > 0)
		snprintf(name + strlen(name), sizeof(name) - strlen(name),
			 " %s", buf);

	retval = streamzap_delay_buf_init(sz);
	if (retval) {
		dev_err(&intf->dev, "%s: delay buffer init failed\n", __func__);
		goto free_urb_in;
	}

	sz->idev = streamzap_init_input_dev(sz);
	if (!sz->idev)
		goto input_dev_fail;

	sz->idle = true;
	sz->decoder_state = PulseSpace;
	#if 0
	/* not yet supported, depends on patches from maxim */
	/* see also: LIRC_GET_REC_RESOLUTION and LIRC_SET_REC_TIMEOUT */
	sz->timeout_enabled = false;
	sz->min_timeout = STREAMZAP_TIMEOUT * STREAMZAP_RESOLUTION * 1000;
	sz->max_timeout = STREAMZAP_TIMEOUT * STREAMZAP_RESOLUTION * 1000;
	#endif

	init_timer(&sz->delay_timer);
	sz->delay_timer.function = streamzap_delay_timeout;
	sz->delay_timer.data = (unsigned long)sz;
	spin_lock_init(&sz->timer_lock);

	init_timer(&sz->flush_timer);
	sz->flush_timer.function = streamzap_flush_timeout;
	sz->flush_timer.data = (unsigned long)sz;

	do_gettimeofday(&sz->signal_start);

	/* Complete final initialisations */
	usb_fill_int_urb(sz->urb_in, usbdev, pipe, sz->buf_in,
			 maxp, (usb_complete_t)streamzap_callback,
			 sz, sz->endpoint->bInterval);
	sz->urb_in->transfer_dma = sz->dma_in;
	sz->urb_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	usb_set_intfdata(intf, sz);

	streamzap_start_flush_timer(sz);

	dev_info(sz->dev, "Registered %s on usb%d:%d\n", name,
		 usbdev->bus->busnum, usbdev->devnum);

	return 0;

input_dev_fail:
	kfifo_free(&sz->fifo);
free_urb_in:
	usb_free_urb(sz->urb_in);
free_buf_in:
	usb_free_coherent(usbdev, maxp, sz->buf_in, sz->dma_in);
free_sz:
	kfree(sz);

	return retval;
}

/**
 * streamzap_disconnect
 *
 * Called by the usb core when the device is removed from the system.
 *
 * This routine guarantees that the driver will not submit any more urbs
 * by clearing dev->usbdev.  It is also supposed to terminate any currently
 * active urbs.  Unfortunately, usb_bulk_msg(), used in streamzap_read(),
 * does not provide any way to do this.
 */
static void streamzap_disconnect(struct usb_interface *interface)
{
	struct streamzap_ir *sz = usb_get_intfdata(interface);
	struct usb_device *usbdev = interface_to_usbdev(interface);

	usb_set_intfdata(interface, NULL);

	if (!sz)
		return;

	if (sz->flush) {
		sz->flush = false;
		del_timer_sync(&sz->flush_timer);
	}

	streamzap_stop_timer(sz);

	sz->usbdev = NULL;
	ir_input_unregister(sz->idev);
	usb_kill_urb(sz->urb_in);
	usb_free_urb(sz->urb_in);
	usb_free_coherent(usbdev, sz->buf_in_len, sz->buf_in, sz->dma_in);

	kfree(sz);
}

static int streamzap_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct streamzap_ir *sz = usb_get_intfdata(intf);

	if (sz->flush) {
		sz->flush = false;
		del_timer_sync(&sz->flush_timer);
	}

	streamzap_stop_timer(sz);

	usb_kill_urb(sz->urb_in);

	return 0;
}

static int streamzap_resume(struct usb_interface *intf)
{
	struct streamzap_ir *sz = usb_get_intfdata(intf);

	if (sz->fifo_initialized)
		kfifo_reset(&sz->fifo);

	sz->flush_timer.expires = jiffies + HZ;
	sz->flush = true;
	add_timer(&sz->flush_timer);

	if (usb_submit_urb(sz->urb_in, GFP_ATOMIC)) {
		dev_err(sz->dev, "Error sumbiting urb\n");
		return -EIO;
	}

	return 0;
}

/**
 *	streamzap_init
 */
static int __init streamzap_init(void)
{
	int ret;

	/* register this driver with the USB subsystem */
	ret = usb_register(&streamzap_driver);
	if (ret < 0)
		printk(KERN_ERR DRIVER_NAME ": usb register failed, "
		       "result = %d\n", ret);

	return ret;
}

/**
 *	streamzap_exit
 */
static void __exit streamzap_exit(void)
{
	usb_deregister(&streamzap_driver);
}


module_init(streamzap_init);
module_exit(streamzap_exit);

MODULE_AUTHOR("Jarod Wilson <jarod@wilsonet.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging messages");
