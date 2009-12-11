/*
 * Hardware event => input event mapping:
 *
 * BTN_TOOL_PEN            0x140 black
 * BTN_TOOL_RUBBER         0x141 blue
 * BTN_TOOL_BRUSH          0x142 green
 * BTN_TOOL_PENCIL         0x143 red
 * BTN_TOOL_AIRBRUSH       0x144 eraser
 * BTN_TOOL_FINGER         0x145 small eraser
 * BTN_TOOL_MOUSE          0x146 mimio interactive
 * BTN_TOOL_LENS           0x147 mimio interactive but1
 * LOCALBTN_TOOL_EXTRA1    0x14a mimio interactive but2 == BTN_TOUCH
 * LOCALBTN_TOOL_EXTRA2    0x14b mimio extra pens (orange, brown, yellow,
 *				 purple) == BTN_STYLUS
 * LOCALBTN_TOOL_EXTRA3    0x14c unused == BTN_STYLUS2
 * BTN_TOOL_DOUBLETAP      0x14d unused
 * BTN_TOOL_TRIPLETAP      0x14e unused
 *
 * MIMIO_EV_PENDOWN(MIMIO_PEN_K)	=> EV_KEY BIT(BTN_TOOL_PEN)
 * MIMIO_EV_PENDOWN(MIMIO_PEN_B)	=> EV_KEY BIT(BTN_TOOL_RUBBER)
 * MIMIO_EV_PENDOWN(MIMIO_PEN_G)	=> EV_KEY BIT(BTN_TOOL_BRUSH)
 * MIMIO_EV_PENDOWN(MIMIO_PEN_R)	=> EV_KEY BIT(BTN_TOOL_PENCIL)
 * MIMIO_EV_PENDOWN(MIMIO_PEN_E)	=> EV_KEY BIT(BTN_TOOL_AIRBRUSH)
 * MIMIO_EV_PENDOWN(MIMIO_PEN_ES)	=> EV_KEY BIT(BTN_TOOL_FINGER)
 * MIMIO_EV_PENDOWN(MIMIO_PEN_I)	=> EV_KEY BIT(BTN_TOOL_MOUSE)
 * MIMIO_EV_PENDOWN(MIMIO_PEN_IL)	=> EV_KEY BIT(BTN_TOOL_LENS)
 * MIMIO_EV_PENDOWN(MIMIO_PEN_IR)	=> EV_KEY BIT(BTN_TOOL_DOUBLETAP)
 * MIMIO_EV_PENDOWN(MIMIO_PEN_EX)	=> EV_KEY BIT(BTN_TOOL_TRIPLETAP)
 * MIMIO_EV_PENDATA		=> EV_ABS BIT(ABS_X), BIT(ABS_Y)
 * MIMIO_EV_MEMRESET		=> EV_KEY BIT(BTN_0)
 * MIMIO_EV_ACC(ACC_NEWPAGE)	=> EV_KEY BIT(BTN_1)
 * MIMIO_EV_ACC(ACC_TAGPAGE)	=> EV_KEY BIT(BTN_2)
 * MIMIO_EV_ACC(ACC_PRINTPAGE)	=> EV_KEY BIT(BTN_3)
 * MIMIO_EV_ACC(ACC_MAXIMIZE)	=> EV_KEY BIT(BTN_4)
 * MIMIO_EV_ACC(ACC_FINDCTLPNL)	=> EV_KEY BIT(BTN_5)
 *
 * open issues:
 * - cold-load of data captured when mimio in standalone mode not yet
 *   supported; need to snoop Win32 box to see datastream for this.
 * - mimio mouse not yet supported; need to snoop Win32 box to see the
 *   datastream for this.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/input.h>
#include <linux/usb.h>

#define DRIVER_VERSION		"v0.031"
#define DRIVER_AUTHOR		"mwilder@cs.nmsu.edu"
#define DRIVER_DESC		"USB mimio-xi driver"

enum {UPVALUE, DOWNVALUE, MOVEVALUE};

#define MIMIO_XRANGE_MAX	9600
#define MIMIO_YRANGE_MAX	4800

#define LOCALBTN_TOOL_EXTRA1	BTN_TOUCH
#define LOCALBTN_TOOL_EXTRA2	BTN_STYLUS
#define LOCALBTN_TOOL_EXTRA3	BTN_STYLUS2

#define MIMIO_VENDOR_ID		0x08d3
#define MIMIO_PRODUCT_ID	0x0001
#define MIMIO_MAXPAYLOAD	(8)
#define MIMIO_MAXNAMELEN	(64)
#define MIMIO_TXWAIT		(1)
#define MIMIO_TXDONE		(2)

#define MIMIO_EV_PENDOWN	(0x22)
#define MIMIO_EV_PENDATA	(0x24)
#define MIMIO_EV_PENUP		(0x51)
#define MIMIO_EV_MEMRESET	(0x45)
#define MIMIO_EV_ACC		(0xb2)

#define MIMIO_PEN_K		(1)	/* black pen */
#define MIMIO_PEN_B		(2)	/* blue pen */
#define MIMIO_PEN_G		(3)	/* green pen */
#define MIMIO_PEN_R		(4)	/* red pen */
/* 5, 6, 7, 8 are extra pens */
#define MIMIO_PEN_E		(9)	/* big eraser */
#define MIMIO_PEN_ES		(10)	/* lil eraser */
#define MIMIO_PENJUMP_START	(10)
#define MIMIO_PENJUMP		(6)
#define MIMIO_PEN_I		(17)	/* mimio interactive */
#define MIMIO_PEN_IL		(18)	/* mimio interactive button 1 */
#define MIMIO_PEN_IR		(19)	/* mimio interactive button 2 */

#define MIMIO_PEN_MAX		(MIMIO_PEN_IR)

#define ACC_DONE		(0)
#define ACC_NEWPAGE		(1)
#define ACC_TAGPAGE		(2)
#define ACC_PRINTPAGE		(4)
#define ACC_MAXIMIZE		(8)
#define ACC_FINDCTLPNL		(16)

#define isvalidtxsize(n)	((n) > 0 && (n) <= MIMIO_MAXPAYLOAD)


struct pktbuf {
	unsigned char instr;
	unsigned char buf[16];
	unsigned char *p;
	unsigned char *q;
};

struct usbintendpt {
	dma_addr_t dma;
	struct urb *urb;
	unsigned char *buf;
	struct usb_endpoint_descriptor *desc;
};

struct mimio {
	struct input_dev *idev;
	struct usb_device *udev;
	struct usb_interface *uifc;
	int open;
	int present;
	int greeted;
	int txflags;
	char phys[MIMIO_MAXNAMELEN];
	struct usbintendpt in;
	struct usbintendpt out;
	struct pktbuf pktbuf;
	unsigned char minor;
	wait_queue_head_t waitq;
	spinlock_t txlock;
	void (*rxhandler)(struct mimio *, unsigned char *, unsigned int);
	int last_pen_down;
};

static void mimio_close(struct input_dev *);
static void mimio_dealloc(struct mimio *);
static void mimio_disconnect(struct usb_interface *);
static int mimio_greet(struct mimio *);
static void mimio_irq_in(struct urb *);
static void mimio_irq_out(struct urb *);
static int mimio_open(struct input_dev *);
static int mimio_probe(struct usb_interface *, const struct usb_device_id *);
static void mimio_rx_handler(struct mimio *, unsigned char *, unsigned int);
static int mimio_tx(struct mimio *, const char *, int);

static char mimio_name[] = "VirtualInk mimio-Xi";
static struct usb_device_id mimio_table[] = {
	{ USB_DEVICE(MIMIO_VENDOR_ID, MIMIO_PRODUCT_ID) },
	{ USB_DEVICE(0x0525, 0xa4a0) }, /* gadget zero firmware */
	{ }
};

MODULE_DEVICE_TABLE(usb, mimio_table);

static struct usb_driver mimio_driver = {
	.name = "mimio",
	.probe = mimio_probe,
	.disconnect = mimio_disconnect,
	.id_table = mimio_table,
};

static DECLARE_MUTEX(disconnect_sem);

static void mimio_close(struct input_dev *idev)
{
	struct mimio *mimio;

	mimio = input_get_drvdata(idev);
	if (!mimio) {
		dev_err(&idev->dev, "null mimio attached to input device\n");
		return;
	}

	if (mimio->open <= 0)
		dev_err(&idev->dev, "mimio not open.\n");
	else
		mimio->open--;

	if (mimio->present == 0 && mimio->open == 0)
		mimio_dealloc(mimio);
}

static void mimio_dealloc(struct mimio *mimio)
{
	if (mimio == NULL)
		return;

	usb_kill_urb(mimio->in.urb);

	usb_kill_urb(mimio->out.urb);

	if (mimio->idev) {
		input_unregister_device(mimio->idev);
		if (mimio->idev->grab)
			input_close_device(mimio->idev->grab);
		else
			dev_dbg(&mimio->idev->dev, "mimio->idev->grab == NULL"
				" -- didn't call input_close_device\n");
	}

	usb_free_urb(mimio->in.urb);

	usb_free_urb(mimio->out.urb);

	if (mimio->in.buf) {
		usb_buffer_free(mimio->udev, MIMIO_MAXPAYLOAD, mimio->in.buf,
				mimio->in.dma);
	}

	if (mimio->out.buf)
		usb_buffer_free(mimio->udev, MIMIO_MAXPAYLOAD, mimio->out.buf,
				mimio->out.dma);

	if (mimio->idev)
		input_free_device(mimio->idev);

	kfree(mimio);
}

static void mimio_disconnect(struct usb_interface *ifc)
{
	struct mimio *mimio;

	down(&disconnect_sem);

	mimio = usb_get_intfdata(ifc);
	usb_set_intfdata(ifc, NULL);
	dev_dbg(&mimio->idev->dev, "disconnect\n");

	if (mimio) {
		mimio->present = 0;

		if (mimio->open <= 0)
			mimio_dealloc(mimio);
	}

	up(&disconnect_sem);
}

static int mimio_greet(struct mimio *mimio)
{
	const struct grtpkt {
		int nbytes;
		unsigned delay;
		char data[8];
	} grtpkts[] = {
		{ 3, 0, { 0x11, 0x55, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00 } },
		{ 5, 0, { 0x53, 0x55, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00 } },
		{ 5, 0, { 0x43, 0x55, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00 } },
		{ 5, 0, { 0x33, 0x55, 0x00, 0x00, 0x66, 0x00, 0x00, 0x00 } },
		{ 5, 0, { 0x13, 0x00, 0x5e, 0x02, 0x4f, 0x00, 0x00, 0x00 } },
		{ 5, 0, { 0x13, 0x00, 0x04, 0x03, 0x14, 0x00, 0x00, 0x00 } },
		{ 5, 2, { 0x13, 0x00, 0x00, 0x04, 0x17, 0x00, 0x00, 0x00 } },
		{ 5, 0, { 0x13, 0x00, 0x0d, 0x08, 0x16, 0x00, 0x00, 0x00 } },
		{ 5, 0, { 0x13, 0x00, 0x4d, 0x01, 0x5f, 0x00, 0x00, 0x00 } },
		{ 3, 0, { 0xf1, 0x55, 0xa4, 0x00, 0x00, 0x00, 0x00, 0x00 } },
		{ 7, 2, { 0x52, 0x55, 0x00, 0x07, 0x31, 0x55, 0x64, 0x00 } },
		{ 0, 0, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
	};
	int rslt;
	const struct grtpkt *pkt;

	for (pkt = grtpkts; pkt->nbytes; pkt++) {
		rslt = mimio_tx(mimio, pkt->data, pkt->nbytes);
		if (rslt)
			return rslt;
		if (pkt->delay)
			msleep(pkt->delay);
	}

	return 0;
}

static void mimio_irq_in(struct urb *urb)
{
	int rslt;
	char *data;
	const char *reason = "going down";
	struct mimio *mimio;

	mimio = urb->context;

	if (mimio == NULL)
		/* paranoia */
		return;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ETIMEDOUT:
		reason = "timeout -- unplugged?";
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		dev_dbg(&mimio->idev->dev, "%s.\n", reason);
		return;
	default:
		dev_dbg(&mimio->idev->dev, "unknown urb-status: %d.\n",
			urb->status);
		goto exit;
	}
	data = mimio->in.buf;

	if (mimio->rxhandler)
		mimio->rxhandler(mimio, data, urb->actual_length);
exit:
	/*
	 * Keep listening to device on same urb.
	 */
	rslt = usb_submit_urb(urb, GFP_ATOMIC);
	if (rslt)
		dev_err(&mimio->idev->dev, "usb_submit_urb failure: %d.\n",
			rslt);
}

static void mimio_irq_out(struct urb *urb)
{
	unsigned long flags;
	struct mimio *mimio;

	mimio = urb->context;

	if (urb->status)
		dev_dbg(&mimio->idev->dev, "urb-status: %d.\n", urb->status);

	spin_lock_irqsave(&mimio->txlock, flags);
	mimio->txflags |= MIMIO_TXDONE;
	spin_unlock_irqrestore(&mimio->txlock, flags);
	wmb();
	wake_up(&mimio->waitq);
}

static int mimio_open(struct input_dev *idev)
{
	int rslt;
	struct mimio *mimio;

	rslt = 0;
	down(&disconnect_sem);
	mimio = input_get_drvdata(idev);
	dev_dbg(&idev->dev, "mimio_open\n");

	if (mimio == NULL) {
		dev_err(&idev->dev, "null mimio.\n");
		rslt = -ENODEV;
		goto exit;
	}

	if (mimio->open++)
		goto exit;

	if (mimio->present && !mimio->greeted) {
		struct urb *urb = mimio->in.urb;
		mimio->in.urb->dev = mimio->udev;
		rslt = usb_submit_urb(mimio->in.urb, GFP_KERNEL);
		if (rslt) {
			dev_err(&idev->dev, "usb_submit_urb failure "
					"(res = %d: ", rslt);
			if (!urb)
				dev_err(&idev->dev, "urb is NULL");
			else if (urb->hcpriv)
				dev_err(&idev->dev, "urb->hcpriv is non-NULL");
			else if (!urb->complete)
				dev_err(&idev->dev, "urb is not complete");
			else if (urb->number_of_packets <= 0)
				dev_err(&idev->dev, "urb has no packets");
			else if (urb->interval <= 0)
				dev_err(&idev->dev, "urb interval too small");
			else
				dev_err(&idev->dev, "urb interval too large " \
						"or some other error");
			dev_err(&idev->dev, "). Not greeting.\n");
			rslt = -EIO;
			goto exit;
		}
		rslt = mimio_greet(mimio);
		if (rslt == 0) {
			dev_dbg(&idev->dev, "Mimio greeted OK.\n");
			mimio->greeted = 1;
		} else {
			dev_dbg(&idev->dev, "Mimio greet Failure (%d)\n",
				rslt);
		}
	}

exit:
	up(&disconnect_sem);
	return rslt;
}

static int mimio_probe(struct usb_interface *ifc,
		       const struct usb_device_id *id)
{
	char path[64];
	int pipe, maxp;
	struct mimio *mimio;
	struct usb_device *udev;
	struct usb_host_interface *hostifc;
	struct input_dev *input_dev;
	int res = 0;
	int i;

	udev = interface_to_usbdev(ifc);

	mimio = kzalloc(sizeof(struct mimio), GFP_KERNEL);
	if (!mimio)
		return -ENOMEM;

	input_dev = input_allocate_device();
	if (!input_dev) {
		mimio_dealloc(mimio);
		return -ENOMEM;
	}

	mimio->uifc = ifc;
	mimio->udev = udev;
	mimio->pktbuf.p = mimio->pktbuf.buf;
	mimio->pktbuf.q = mimio->pktbuf.buf;
	/* init_input_dev(mimio->idev); */
	mimio->idev = input_dev;
	init_waitqueue_head(&mimio->waitq);
	spin_lock_init(&mimio->txlock);
	hostifc = ifc->cur_altsetting;

	if (hostifc->desc.bNumEndpoints != 2) {
		dev_err(&udev->dev, "Unexpected endpoint count: %d.\n",
			hostifc->desc.bNumEndpoints);
		mimio_dealloc(mimio);
		return -ENODEV;
	}

	mimio->in.desc = &(hostifc->endpoint[0].desc);
	mimio->out.desc = &(hostifc->endpoint[1].desc);

	mimio->in.buf = usb_buffer_alloc(udev, MIMIO_MAXPAYLOAD, GFP_KERNEL,
					 &mimio->in.dma);
	mimio->out.buf = usb_buffer_alloc(udev, MIMIO_MAXPAYLOAD, GFP_KERNEL,
					  &mimio->out.dma);

	if (mimio->in.buf == NULL || mimio->out.buf == NULL) {
		dev_err(&udev->dev, "usb_buffer_alloc failure.\n");
		mimio_dealloc(mimio);
		return -ENOMEM;
	}

	mimio->in.urb = usb_alloc_urb(0, GFP_KERNEL);
	mimio->out.urb = usb_alloc_urb(0, GFP_KERNEL);

	if (mimio->in.urb == NULL || mimio->out.urb == NULL) {
		dev_err(&udev->dev, "usb_alloc_urb failure.\n");
		mimio_dealloc(mimio);
		return -ENOMEM;
	}

	/*
	 * Build the input urb.
	 */
	pipe = usb_rcvintpipe(udev, mimio->in.desc->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));
	if (maxp > MIMIO_MAXPAYLOAD)
		maxp = MIMIO_MAXPAYLOAD;
	usb_fill_int_urb(mimio->in.urb, udev, pipe, mimio->in.buf, maxp,
			 mimio_irq_in, mimio, mimio->in.desc->bInterval);
	mimio->in.urb->transfer_dma = mimio->in.dma;
	mimio->in.urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/*
	 * Build the output urb.
	 */
	pipe = usb_sndintpipe(udev, mimio->out.desc->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));
	if (maxp > MIMIO_MAXPAYLOAD)
		maxp = MIMIO_MAXPAYLOAD;
	usb_fill_int_urb(mimio->out.urb, udev, pipe, mimio->out.buf, maxp,
			 mimio_irq_out, mimio, mimio->out.desc->bInterval);
	mimio->out.urb->transfer_dma = mimio->out.dma;
	mimio->out.urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/*
	 * Build input device info
	 */
	usb_make_path(udev, path, 64);
	snprintf(mimio->phys, MIMIO_MAXNAMELEN, "%s/input0", path);
	input_set_drvdata(input_dev, mimio);
	/* input_dev->dev = &ifc->dev; */
	input_dev->open = mimio_open;
	input_dev->close = mimio_close;
	input_dev->name = mimio_name;
	input_dev->phys = mimio->phys;
	input_dev->dev.parent = &ifc->dev;

	input_dev->id.bustype = BUS_USB;
	input_dev->id.vendor = le16_to_cpu(udev->descriptor.idVendor);
	input_dev->id.product = le16_to_cpu(udev->descriptor.idProduct);
	input_dev->id.version = le16_to_cpu(udev->descriptor.bcdDevice);

	input_dev->evbit[0] |= BIT(EV_KEY) | BIT(EV_ABS);
	for (i = BTN_TOOL_PEN; i <= LOCALBTN_TOOL_EXTRA2; ++i)
		set_bit(i, input_dev->keybit);

	input_dev->keybit[BIT_WORD(BTN_MISC)] |= BIT_MASK(BTN_0) |
						 BIT_MASK(BTN_1) |
						 BIT_MASK(BTN_2) |
						 BIT_MASK(BTN_3) |
						 BIT_MASK(BTN_4) |
						 BIT_MASK(BTN_5);
	/*   input_dev->keybit[BTN_MOUSE] |= BIT(BTN_LEFT); */
	input_dev->absbit[0] |= BIT_MASK(ABS_X) | BIT_MASK(ABS_Y);
	input_set_abs_params(input_dev, ABS_X, 0, MIMIO_XRANGE_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MIMIO_YRANGE_MAX, 0, 0);
	input_dev->absbit[BIT_WORD(ABS_MISC)] |= BIT_MASK(ABS_MISC);

	/*
	 * Register the input device.
	 */
	res = input_register_device(mimio->idev);
	if (res) {
		dev_err(&udev->dev, "input_register_device failure (%d)\n",
			res);
		mimio_dealloc(mimio);
		return -EIO;
	}
	dev_dbg(&mimio->idev->dev, "input: %s on %s (res = %d).\n",
		input_dev->name, input_dev->phys, res);

	usb_set_intfdata(ifc, mimio);
	mimio->present = 1;

	/*
	 * Submit the input urb to the usb subsystem.
	 */
	mimio->in.urb->dev = mimio->udev;
	res = usb_submit_urb(mimio->in.urb, GFP_KERNEL);
	if (res) {
		dev_err(&mimio->idev->dev, "usb_submit_urb failure (%d)\n",
			res);
		mimio_dealloc(mimio);
		return -EIO;
	}

	/*
	 * Attempt to greet the mimio after giving
	 * it some post-init settling time.
	 *
	 * note: sometimes this sleep interval isn't
	 * long enough to permit the device to re-init
	 * after a hot-swap; maybe need to bump it up.
	 *
	 * As it is, this probably breaks module unloading support!
	 */
	msleep(1024);

	res = mimio_greet(mimio);
	if (res == 0) {
		dev_dbg(&mimio->idev->dev, "Mimio greeted OK.\n");
		mimio->greeted = 1;
		mimio->rxhandler = mimio_rx_handler;
	} else {
		dev_dbg(&mimio->idev->dev, "Mimio greet Failure (%d)\n", res);
	}

	return 0;
}

static int handle_mimio_rx_penupdown(struct mimio *mimio,
				     int down,
				     const char *const instr[],
				     const int instr_ofst[])
{
	int penid, x;
	if (mimio->pktbuf.q - mimio->pktbuf.p < (down ? 4 : 3))
		return 1; 		/* partial pkt */

	if (down) {
		x = *mimio->pktbuf.p ^ *(mimio->pktbuf.p + 1) ^
			*(mimio->pktbuf.p + 2);
		if (x != *(mimio->pktbuf.p + 3)) {
			dev_dbg(&mimio->idev->dev, "EV_PEN%s: bad xsum.\n",
				down ? "DOWN" : "UP");
			/* skip this event data */
			mimio->pktbuf.p += 4;
			/* decode any remaining events */
			return 0;
		}
		penid = mimio->pktbuf.instr = *(mimio->pktbuf.p + 2);
		if (penid > MIMIO_PEN_MAX) {
			dev_dbg(&mimio->idev->dev,
				"Unmapped penID (not in [0, %d]): %d\n",
				MIMIO_PEN_MAX, (int)mimio->pktbuf.instr);
			penid = mimio->pktbuf.instr = 0;
		}
		mimio->last_pen_down = penid;
	} else {
		penid = mimio->last_pen_down;
	}
	dev_dbg(&mimio->idev->dev, "%s (id %d, code %d) %s.\n", instr[penid],
		instr_ofst[penid], penid, down ? "down" : "up");

	if (instr_ofst[penid] >= 0) {
		int code = BTN_TOOL_PEN + instr_ofst[penid];
		int value = down ? DOWNVALUE : UPVALUE;
		if (code > KEY_MAX)
			dev_dbg(&mimio->idev->dev, "input_event will ignore "
				"-- code (%d) > KEY_MAX\n", code);
		if (!test_bit(code, mimio->idev->keybit))
			dev_dbg(&mimio->idev->dev, "input_event will ignore "
				"-- bit for code (%d) not enabled\n", code);
		if (!!test_bit(code, mimio->idev->key) == value)
			dev_dbg(&mimio->idev->dev, "input_event will ignore "
				"-- bit for code (%d) already set to %d\n",
				code, value);
		if (value != DOWNVALUE) {
			/* input_regs(mimio->idev, regs); */
			input_report_key(mimio->idev, code, value);
			input_sync(mimio->idev);
		} else {
			/* wait until we get some coordinates */
		}
	} else {
		dev_dbg(&mimio->idev->dev, "penID offset[%d] == %d is < 0 "
			"- not sending\n", penid, instr_ofst[penid]);
	}
	mimio->pktbuf.p += down ? 4 : 3; /* 3 for up, 4 for down */
	return 0;
}

/*
 * Stay tuned for partial-packet excitement.
 *
 * This routine buffers data packets received from the mimio device
 * in the mimio's data space.  This buffering is necessary because
 * the mimio's in endpoint can serve us partial packets of data, and
 * we want the driver to support the servicing of multiple mimios.
 * Empirical evidence gathered so far suggests that the method of
 * buffering packet data in the mimio's data space works.  Previous
 * versions of this driver did not buffer packet data in each mimio's
 * data-space, and were therefore not able to service multiple mimios.
 * Note that since the caller of this routine is running in interrupt
 * context, care needs to be taken to ensure that this routine does not
 * become bloated, and it may be that another spinlock is needed in each
 * mimio to guard the buffered packet data properly.
 */
static void mimio_rx_handler(struct mimio *mimio,
			     unsigned char *data,
			     unsigned int nbytes)
{
	struct device *dev = &mimio->idev->dev;
	unsigned int x;
	unsigned int y;
	static const char * const instr[] = {
		"?0",
		"black pen", "blue pen", "green pen", "red pen",
		"brown pen", "orange pen", "purple pen", "yellow pen",
		"big eraser", "lil eraser",
		"?11", "?12", "?13", "?14", "?15", "?16",
		"mimio interactive", "interactive button1",
		"interactive button2"
	};

	/* Mimio Interactive gives:
	 * down: [0x22 0x01 0x11 0x32 0x24]
	 * b1  : [0x22 0x01 0x12 0x31 0x24]
	 * b2  : [0x22 0x01 0x13 0x30 0x24]
	 */
	static const int instr_ofst[] = {
		-1,
		0, 1, 2, 3,
		9, 9, 9, 9,
		4, 5,
		-1, -1, -1, -1, -1, -1,
		6, 7, 8,
	};

	memcpy(mimio->pktbuf.q, data, nbytes);
	mimio->pktbuf.q += nbytes;

	while (mimio->pktbuf.p < mimio->pktbuf.q) {
		int t = *mimio->pktbuf.p;
		switch (t) {
		case MIMIO_EV_PENUP:
		case MIMIO_EV_PENDOWN:
			if (handle_mimio_rx_penupdown(mimio,
						      t == MIMIO_EV_PENDOWN,
						      instr, instr_ofst))
				return; /* partial packet */
			break;

		case MIMIO_EV_PENDATA:
			if (mimio->pktbuf.q - mimio->pktbuf.p < 6)
				/* partial pkt */
				return;
			x = *mimio->pktbuf.p ^ *(mimio->pktbuf.p + 1) ^
				*(mimio->pktbuf.p + 2) ^
				*(mimio->pktbuf.p + 3) ^
				*(mimio->pktbuf.p + 4);
			if (x != *(mimio->pktbuf.p + 5)) {
				dev_dbg(dev, "EV_PENDATA: bad xsum.\n");
				mimio->pktbuf.p += 6; /* skip this event data */
				break; /* decode any remaining events */
			}
			x = *(mimio->pktbuf.p + 1);
			x <<= 8;
			x |= *(mimio->pktbuf.p + 2);
			y = *(mimio->pktbuf.p + 3);
			y <<= 8;
			y |= *(mimio->pktbuf.p + 4);
			dev_dbg(dev, "coord: (%d, %d)\n", x, y);
			if (instr_ofst[mimio->pktbuf.instr] >= 0) {
				int code = BTN_TOOL_PEN +
					   instr_ofst[mimio->last_pen_down];
				/* input_regs(mimio->idev, regs); */
				input_report_abs(mimio->idev, ABS_X, x);
				input_report_abs(mimio->idev, ABS_Y, y);
				/* fake a penup */
				change_bit(code, mimio->idev->key);
				input_report_key(mimio->idev,
						 code,
						 DOWNVALUE);
				/* always sync here */
				mimio->idev->sync = 0;
				input_sync(mimio->idev);
			}
			mimio->pktbuf.p += 6;
			break;
		case MIMIO_EV_MEMRESET:
			if (mimio->pktbuf.q - mimio->pktbuf.p < 7)
				/* partial pkt */
				return;
			dev_dbg(dev, "mem-reset.\n");
			/* input_regs(mimio->idev, regs); */
			input_event(mimio->idev, EV_KEY, BTN_0, 1);
			input_event(mimio->idev, EV_KEY, BTN_0, 0);
			input_sync(mimio->idev);
			mimio->pktbuf.p += 7;
			break;
		case MIMIO_EV_ACC:
			if (mimio->pktbuf.q - mimio->pktbuf.p < 4)
				/* partial pkt */
				return;
			x = *mimio->pktbuf.p ^ *(mimio->pktbuf.p + 1) ^
				*(mimio->pktbuf.p + 2);
			if (x != *(mimio->pktbuf.p + 3)) {
				dev_dbg(dev, "EV_ACC: bad xsum.\n");
				mimio->pktbuf.p += 4; /* skip this event data */
				break; /* decode any remaining events */
			}
			switch (*(mimio->pktbuf.p + 2)) {
			case ACC_NEWPAGE:
				dev_dbg(&mimio->idev->dev, "new-page.\n");
				/* input_regs(mimio->idev, regs); */
				input_event(mimio->idev, EV_KEY, BTN_1, 1);
				input_event(mimio->idev, EV_KEY, BTN_1, 0);
				input_sync(mimio->idev);
				break;
			case ACC_TAGPAGE:
				dev_dbg(&mimio->idev->dev, "tag-page.\n");
				/* input_regs(mimio->idev, regs); */
				input_event(mimio->idev, EV_KEY, BTN_2, 1);
				input_event(mimio->idev, EV_KEY, BTN_2, 0);
				input_sync(mimio->idev);
				break;
			case ACC_PRINTPAGE:
				dev_dbg(&mimio->idev->dev, "print-page.\n");
				/* input_regs(mimio->idev, regs);*/
				input_event(mimio->idev, EV_KEY, BTN_3, 1);
				input_event(mimio->idev, EV_KEY, BTN_3, 0);
				input_sync(mimio->idev);
				break;
			case ACC_MAXIMIZE:
				dev_dbg(&mimio->idev->dev,
					"maximize-window.\n");
				/* input_regs(mimio->idev, regs); */
				input_event(mimio->idev, EV_KEY, BTN_4, 1);
				input_event(mimio->idev, EV_KEY, BTN_4, 0);
				input_sync(mimio->idev);
				break;
			case ACC_FINDCTLPNL:
				dev_dbg(&mimio->idev->dev, "find-ctl-panel.\n");
				/* input_regs(mimio->idev, regs); */
				input_event(mimio->idev, EV_KEY, BTN_5, 1);
				input_event(mimio->idev, EV_KEY, BTN_5, 0);
				input_sync(mimio->idev);
				break;
			case ACC_DONE:
				dev_dbg(&mimio->idev->dev, "acc-done.\n");
				/* no event is dispatched to the input
				 * subsystem for this device event.
				 */
				break;
			default:
				dev_dbg(dev, "unknown acc event.\n");
				break;
			}
			mimio->pktbuf.p += 4;
			break;
		default:
			mimio->pktbuf.p++;
			break;
		}
	}

	/*
	 * No partial event was received, so reset mimio's pktbuf ptrs.
	 */
	mimio->pktbuf.p = mimio->pktbuf.q = mimio->pktbuf.buf;
}

static int mimio_tx(struct mimio *mimio, const char *buf, int nbytes)
{
	int rslt;
	int timeout;
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);

	if (!(isvalidtxsize(nbytes))) {
		dev_err(&mimio->idev->dev, "invalid arg: nbytes: %d.\n",
			nbytes);
		return -EINVAL;
	}

	/*
	 * Init the out urb and copy the data to send.
	 */
	mimio->out.urb->dev = mimio->udev;
	mimio->out.urb->transfer_buffer_length = nbytes;
	memcpy(mimio->out.urb->transfer_buffer, buf, nbytes);

	/*
	 * Send the data.
	 */
	spin_lock_irqsave(&mimio->txlock, flags);
	mimio->txflags = MIMIO_TXWAIT;
	rslt = usb_submit_urb(mimio->out.urb, GFP_ATOMIC);
	spin_unlock_irqrestore(&mimio->txlock, flags);
	dev_dbg(&mimio->idev->dev, "rslt: %d.\n", rslt);

	if (rslt) {
		dev_err(&mimio->idev->dev, "usb_submit_urb failure: %d.\n",
			rslt);
		return rslt;
	}

	/*
	 * Wait for completion to be signalled (the mimio_irq_out
	 * completion routine will or MIMIO_TXDONE in with txflags).
	 */
	timeout = HZ;
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&mimio->waitq, &wait);

	while (timeout && ((mimio->txflags & MIMIO_TXDONE) == 0)) {
		timeout = schedule_timeout(timeout);
		rmb();
	}

	if ((mimio->txflags & MIMIO_TXDONE) == 0)
		dev_dbg(&mimio->idev->dev, "tx timed out.\n");

	/*
	 * Now that completion has been signalled,
	 * unlink the urb so that it can be recycled.
	 */
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&mimio->waitq, &wait);
	usb_unlink_urb(mimio->out.urb);

	return rslt;
}

static int __init mimio_init(void)
{
	int rslt;

	rslt = usb_register(&mimio_driver);
	if (rslt != 0) {
		err("%s: usb_register failure: %d", __func__, rslt);
		return rslt;
	}

	printk(KERN_INFO KBUILD_MODNAME ":"
	       DRIVER_DESC " " DRIVER_VERSION "\n");
	return rslt;
}

static void __exit mimio_exit(void)
{
	usb_deregister(&mimio_driver);
}

module_init(mimio_init);
module_exit(mimio_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
