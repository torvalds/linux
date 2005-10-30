#include <linux/config.h>
#if !defined (DEBUG) && defined (CONFIG_USB_DEBUG)
#   define DEBUG
#endif
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/scatterlist.h>

#include <linux/usb.h>


/*-------------------------------------------------------------------------*/

// FIXME make these public somewhere; usbdevfs.h?
//
struct usbtest_param {
	// inputs
	unsigned		test_num;	/* 0..(TEST_CASES-1) */
	unsigned		iterations;
	unsigned		length;
	unsigned		vary;
	unsigned		sglen;

	// outputs
	struct timeval		duration;
};
#define USBTEST_REQUEST	_IOWR('U', 100, struct usbtest_param)

/*-------------------------------------------------------------------------*/

#define	GENERIC		/* let probe() bind using module params */

/* Some devices that can be used for testing will have "real" drivers.
 * Entries for those need to be enabled here by hand, after disabling
 * that "real" driver.
 */
//#define	IBOT2		/* grab iBOT2 webcams */
//#define	KEYSPAN_19Qi	/* grab un-renumerated serial adapter */

/*-------------------------------------------------------------------------*/

struct usbtest_info {
	const char		*name;
	u8			ep_in;		/* bulk/intr source */
	u8			ep_out;		/* bulk/intr sink */
	unsigned		autoconf : 1;
	unsigned		ctrl_out : 1;
	unsigned		iso : 1;	/* try iso in/out */
	int			alt;
};

/* this is accessed only through usbfs ioctl calls.
 * one ioctl to issue a test ... one lock per device.
 * tests create other threads if they need them.
 * urbs and buffers are allocated dynamically,
 * and data generated deterministically.
 */
struct usbtest_dev {
	struct usb_interface	*intf;
	struct usbtest_info	*info;
	int			in_pipe;
	int			out_pipe;
	int			in_iso_pipe;
	int			out_iso_pipe;
	struct usb_endpoint_descriptor	*iso_in, *iso_out;
	struct semaphore	sem;

#define TBUF_SIZE	256
	u8			*buf;
};

static struct usb_device *testdev_to_usbdev (struct usbtest_dev *test)
{
	return interface_to_usbdev (test->intf);
}

/* set up all urbs so they can be used with either bulk or interrupt */
#define	INTERRUPT_RATE		1	/* msec/transfer */

#define xprintk(tdev,level,fmt,args...) \
	dev_printk(level ,  &(tdev)->intf->dev ,  fmt ,  ## args)

#ifdef DEBUG
#define DBG(dev,fmt,args...) \
	xprintk(dev , KERN_DEBUG , fmt , ## args)
#else
#define DBG(dev,fmt,args...) \
	do { } while (0)
#endif /* DEBUG */

#ifdef VERBOSE
#define VDBG DBG
#else
#define VDBG(dev,fmt,args...) \
	do { } while (0)
#endif	/* VERBOSE */

#define ERROR(dev,fmt,args...) \
	xprintk(dev , KERN_ERR , fmt , ## args)
#define WARN(dev,fmt,args...) \
	xprintk(dev , KERN_WARNING , fmt , ## args)
#define INFO(dev,fmt,args...) \
	xprintk(dev , KERN_INFO , fmt , ## args)

/*-------------------------------------------------------------------------*/

static int
get_endpoints (struct usbtest_dev *dev, struct usb_interface *intf)
{
	int				tmp;
	struct usb_host_interface	*alt;
	struct usb_host_endpoint	*in, *out;
	struct usb_host_endpoint	*iso_in, *iso_out;
	struct usb_device		*udev;

	for (tmp = 0; tmp < intf->num_altsetting; tmp++) {
		unsigned	ep;

		in = out = NULL;
		iso_in = iso_out = NULL;
		alt = intf->altsetting + tmp;

		/* take the first altsetting with in-bulk + out-bulk;
		 * ignore other endpoints and altsetttings.
		 */
		for (ep = 0; ep < alt->desc.bNumEndpoints; ep++) {
			struct usb_host_endpoint	*e;

			e = alt->endpoint + ep;
			switch (e->desc.bmAttributes) {
			case USB_ENDPOINT_XFER_BULK:
				break;
			case USB_ENDPOINT_XFER_ISOC:
				if (dev->info->iso)
					goto try_iso;
				// FALLTHROUGH
			default:
				continue;
			}
			if (e->desc.bEndpointAddress & USB_DIR_IN) {
				if (!in)
					in = e;
			} else {
				if (!out)
					out = e;
			}
			continue;
try_iso:
			if (e->desc.bEndpointAddress & USB_DIR_IN) {
				if (!iso_in)
					iso_in = e;
			} else {
				if (!iso_out)
					iso_out = e;
			}
		}
		if ((in && out)  ||  (iso_in && iso_out))
			goto found;
	}
	return -EINVAL;

found:
	udev = testdev_to_usbdev (dev);
	if (alt->desc.bAlternateSetting != 0) {
		tmp = usb_set_interface (udev,
				alt->desc.bInterfaceNumber,
				alt->desc.bAlternateSetting);
		if (tmp < 0)
			return tmp;
	}

	if (in) {
		dev->in_pipe = usb_rcvbulkpipe (udev,
			in->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
		dev->out_pipe = usb_sndbulkpipe (udev,
			out->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	}
	if (iso_in) {
		dev->iso_in = &iso_in->desc;
		dev->in_iso_pipe = usb_rcvisocpipe (udev,
				iso_in->desc.bEndpointAddress
					& USB_ENDPOINT_NUMBER_MASK);
		dev->iso_out = &iso_out->desc;
		dev->out_iso_pipe = usb_sndisocpipe (udev,
				iso_out->desc.bEndpointAddress
					& USB_ENDPOINT_NUMBER_MASK);
	}
	return 0;
}

/*-------------------------------------------------------------------------*/

/* Support for testing basic non-queued I/O streams.
 *
 * These just package urbs as requests that can be easily canceled.
 * Each urb's data buffer is dynamically allocated; callers can fill
 * them with non-zero test data (or test for it) when appropriate.
 */

static void simple_callback (struct urb *urb, struct pt_regs *regs)
{
	complete ((struct completion *) urb->context);
}

static struct urb *simple_alloc_urb (
	struct usb_device	*udev,
	int			pipe,
	unsigned long		bytes
)
{
	struct urb		*urb;

	if (bytes < 0)
		return NULL;
	urb = usb_alloc_urb (0, SLAB_KERNEL);
	if (!urb)
		return urb;
	usb_fill_bulk_urb (urb, udev, pipe, NULL, bytes, simple_callback, NULL);
	urb->interval = (udev->speed == USB_SPEED_HIGH)
			? (INTERRUPT_RATE << 3)
			: INTERRUPT_RATE;
	urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
	if (usb_pipein (pipe))
		urb->transfer_flags |= URB_SHORT_NOT_OK;
	urb->transfer_buffer = usb_buffer_alloc (udev, bytes, SLAB_KERNEL,
			&urb->transfer_dma);
	if (!urb->transfer_buffer) {
		usb_free_urb (urb);
		urb = NULL;
	} else
		memset (urb->transfer_buffer, 0, bytes);
	return urb;
}

static unsigned pattern = 0;
module_param (pattern, uint, S_IRUGO);
// MODULE_PARM_DESC (pattern, "i/o pattern (0 == zeroes)");

static inline void simple_fill_buf (struct urb *urb)
{
	unsigned	i;
	u8		*buf = urb->transfer_buffer;
	unsigned	len = urb->transfer_buffer_length;

	switch (pattern) {
	default:
		// FALLTHROUGH
	case 0:
		memset (buf, 0, len);
		break;
	case 1:			/* mod63 */
		for (i = 0; i < len; i++)
			*buf++ = (u8) (i % 63);
		break;
	}
}

static inline int simple_check_buf (struct urb *urb)
{
	unsigned	i;
	u8		expected;
	u8		*buf = urb->transfer_buffer;
	unsigned	len = urb->actual_length;

	for (i = 0; i < len; i++, buf++) {
		switch (pattern) {
		/* all-zeroes has no synchronization issues */
		case 0:
			expected = 0;
			break;
		/* mod63 stays in sync with short-terminated transfers,
		 * or otherwise when host and gadget agree on how large
		 * each usb transfer request should be.  resync is done
		 * with set_interface or set_config.
		 */
		case 1:			/* mod63 */
			expected = i % 63;
			break;
		/* always fail unsupported patterns */
		default:
			expected = !*buf;
			break;
		}
		if (*buf == expected)
			continue;
		dbg ("buf[%d] = %d (not %d)", i, *buf, expected);
		return -EINVAL;
	}
	return 0;
}

static void simple_free_urb (struct urb *urb)
{
	usb_buffer_free (urb->dev, urb->transfer_buffer_length,
			urb->transfer_buffer, urb->transfer_dma);
	usb_free_urb (urb);
}

static int simple_io (
	struct urb		*urb,
	int			iterations,
	int			vary,
	int			expected,
	const char		*label
)
{
	struct usb_device	*udev = urb->dev;
	int			max = urb->transfer_buffer_length;
	struct completion	completion;
	int			retval = 0;

	urb->context = &completion;
	while (retval == 0 && iterations-- > 0) {
		init_completion (&completion);
		if (usb_pipeout (urb->pipe))
			simple_fill_buf (urb);
		if ((retval = usb_submit_urb (urb, SLAB_KERNEL)) != 0)
			break;

		/* NOTE:  no timeouts; can't be broken out of by interrupt */
		wait_for_completion (&completion);
		retval = urb->status;
		urb->dev = udev;
		if (retval == 0 && usb_pipein (urb->pipe))
			retval = simple_check_buf (urb);

		if (vary) {
			int	len = urb->transfer_buffer_length;

			len += vary;
			len %= max;
			if (len == 0)
				len = (vary < max) ? vary : max;
			urb->transfer_buffer_length = len;
		}

		/* FIXME if endpoint halted, clear halt (and log) */
	}
	urb->transfer_buffer_length = max;

	if (expected != retval)
		dev_dbg (&udev->dev,
			"%s failed, iterations left %d, status %d (not %d)\n",
				label, iterations, retval, expected);
	return retval;
}


/*-------------------------------------------------------------------------*/

/* We use scatterlist primitives to test queued I/O.
 * Yes, this also tests the scatterlist primitives.
 */

static void free_sglist (struct scatterlist *sg, int nents)
{
	unsigned		i;
	
	if (!sg)
		return;
	for (i = 0; i < nents; i++) {
		if (!sg [i].page)
			continue;
		kfree (page_address (sg [i].page) + sg [i].offset);
	}
	kfree (sg);
}

static struct scatterlist *
alloc_sglist (int nents, int max, int vary)
{
	struct scatterlist	*sg;
	unsigned		i;
	unsigned		size = max;

	sg = kmalloc (nents * sizeof *sg, SLAB_KERNEL);
	if (!sg)
		return NULL;

	for (i = 0; i < nents; i++) {
		char		*buf;

		buf = kmalloc (size, SLAB_KERNEL);
		if (!buf) {
			free_sglist (sg, i);
			return NULL;
		}
		memset (buf, 0, size);

		/* kmalloc pages are always physically contiguous! */
		sg_init_one(&sg[i], buf, size);

		if (vary) {
			size += vary;
			size %= max;
			if (size == 0)
				size = (vary < max) ? vary : max;
		}
	}

	return sg;
}

static int perform_sglist (
	struct usb_device	*udev,
	unsigned		iterations,
	int			pipe,
	struct usb_sg_request	*req,
	struct scatterlist	*sg,
	int			nents
)
{
	int			retval = 0;

	while (retval == 0 && iterations-- > 0) {
		retval = usb_sg_init (req, udev, pipe,
				(udev->speed == USB_SPEED_HIGH)
					? (INTERRUPT_RATE << 3)
					: INTERRUPT_RATE,
				sg, nents, 0, SLAB_KERNEL);
		
		if (retval)
			break;
		usb_sg_wait (req);
		retval = req->status;

		/* FIXME if endpoint halted, clear halt (and log) */
	}

	// FIXME for unlink or fault handling tests, don't report
	// failure if retval is as we expected ...

	if (retval)
		dbg ("perform_sglist failed, iterations left %d, status %d",
				iterations, retval);
	return retval;
}


/*-------------------------------------------------------------------------*/

/* unqueued control message testing
 *
 * there's a nice set of device functional requirements in chapter 9 of the
 * usb 2.0 spec, which we can apply to ANY device, even ones that don't use
 * special test firmware.
 *
 * we know the device is configured (or suspended) by the time it's visible
 * through usbfs.  we can't change that, so we won't test enumeration (which
 * worked 'well enough' to get here, this time), power management (ditto),
 * or remote wakeup (which needs human interaction).
 */

static unsigned realworld = 1;
module_param (realworld, uint, 0);
MODULE_PARM_DESC (realworld, "clear to demand stricter spec compliance");

static int get_altsetting (struct usbtest_dev *dev)
{
	struct usb_interface	*iface = dev->intf;
	struct usb_device	*udev = interface_to_usbdev (iface);
	int			retval;

	retval = usb_control_msg (udev, usb_rcvctrlpipe (udev, 0),
			USB_REQ_GET_INTERFACE, USB_DIR_IN|USB_RECIP_INTERFACE,
			0, iface->altsetting [0].desc.bInterfaceNumber,
			dev->buf, 1, USB_CTRL_GET_TIMEOUT);
	switch (retval) {
	case 1:
		return dev->buf [0];
	case 0:
		retval = -ERANGE;
		// FALLTHROUGH
	default:
		return retval;
	}
}

static int set_altsetting (struct usbtest_dev *dev, int alternate)
{
	struct usb_interface		*iface = dev->intf;
	struct usb_device		*udev;

	if (alternate < 0 || alternate >= 256)
		return -EINVAL;

	udev = interface_to_usbdev (iface);
	return usb_set_interface (udev,
			iface->altsetting [0].desc.bInterfaceNumber,
			alternate);
}

static int is_good_config (char *buf, int len)
{
	struct usb_config_descriptor	*config;
	
	if (len < sizeof *config)
		return 0;
	config = (struct usb_config_descriptor *) buf;

	switch (config->bDescriptorType) {
	case USB_DT_CONFIG:
	case USB_DT_OTHER_SPEED_CONFIG:
		if (config->bLength != 9) {
			dbg ("bogus config descriptor length");
			return 0;
		}
		/* this bit 'must be 1' but often isn't */
		if (!realworld && !(config->bmAttributes & 0x80)) {
			dbg ("high bit of config attributes not set");
			return 0;
		}
		if (config->bmAttributes & 0x1f) {	/* reserved == 0 */
			dbg ("reserved config bits set");
			return 0;
		}
		break;
	default:
		return 0;
	}

	if (le16_to_cpu(config->wTotalLength) == len)		/* read it all */
		return 1;
	if (le16_to_cpu(config->wTotalLength) >= TBUF_SIZE)		/* max partial read */
		return 1;
	dbg ("bogus config descriptor read size");
	return 0;
}

/* sanity test for standard requests working with usb_control_mesg() and some
 * of the utility functions which use it.
 *
 * this doesn't test how endpoint halts behave or data toggles get set, since
 * we won't do I/O to bulk/interrupt endpoints here (which is how to change
 * halt or toggle).  toggle testing is impractical without support from hcds.
 *
 * this avoids failing devices linux would normally work with, by not testing
 * config/altsetting operations for devices that only support their defaults.
 * such devices rarely support those needless operations.
 *
 * NOTE that since this is a sanity test, it's not examining boundary cases
 * to see if usbcore, hcd, and device all behave right.  such testing would
 * involve varied read sizes and other operation sequences.
 */
static int ch9_postconfig (struct usbtest_dev *dev)
{
	struct usb_interface	*iface = dev->intf;
	struct usb_device	*udev = interface_to_usbdev (iface);
	int			i, alt, retval;

	/* [9.2.3] if there's more than one altsetting, we need to be able to
	 * set and get each one.  mostly trusts the descriptors from usbcore.
	 */
	for (i = 0; i < iface->num_altsetting; i++) {

		/* 9.2.3 constrains the range here */
		alt = iface->altsetting [i].desc.bAlternateSetting;
		if (alt < 0 || alt >= iface->num_altsetting) {
			dev_dbg (&iface->dev,
					"invalid alt [%d].bAltSetting = %d\n",
					i, alt);
		}

		/* [real world] get/set unimplemented if there's only one */
		if (realworld && iface->num_altsetting == 1)
			continue;

		/* [9.4.10] set_interface */
		retval = set_altsetting (dev, alt);
		if (retval) {
			dev_dbg (&iface->dev, "can't set_interface = %d, %d\n",
					alt, retval);
			return retval;
		}

		/* [9.4.4] get_interface always works */
		retval = get_altsetting (dev);
		if (retval != alt) {
			dev_dbg (&iface->dev, "get alt should be %d, was %d\n",
					alt, retval);
			return (retval < 0) ? retval : -EDOM;
		}

	}

	/* [real world] get_config unimplemented if there's only one */
	if (!realworld || udev->descriptor.bNumConfigurations != 1) {
		int	expected = udev->actconfig->desc.bConfigurationValue;

		/* [9.4.2] get_configuration always works
		 * ... although some cheap devices (like one TI Hub I've got)
		 * won't return config descriptors except before set_config.
		 */
		retval = usb_control_msg (udev, usb_rcvctrlpipe (udev, 0),
				USB_REQ_GET_CONFIGURATION,
				USB_DIR_IN | USB_RECIP_DEVICE,
				0, 0, dev->buf, 1, USB_CTRL_GET_TIMEOUT);
		if (retval != 1 || dev->buf [0] != expected) {
			dev_dbg (&iface->dev, "get config --> %d %d (1 %d)\n",
				retval, dev->buf[0], expected);
			return (retval < 0) ? retval : -EDOM;
		}
	}

	/* there's always [9.4.3] a device descriptor [9.6.1] */
	retval = usb_get_descriptor (udev, USB_DT_DEVICE, 0,
			dev->buf, sizeof udev->descriptor);
	if (retval != sizeof udev->descriptor) {
		dev_dbg (&iface->dev, "dev descriptor --> %d\n", retval);
		return (retval < 0) ? retval : -EDOM;
	}

	/* there's always [9.4.3] at least one config descriptor [9.6.3] */
	for (i = 0; i < udev->descriptor.bNumConfigurations; i++) {
		retval = usb_get_descriptor (udev, USB_DT_CONFIG, i,
				dev->buf, TBUF_SIZE);
		if (!is_good_config (dev->buf, retval)) {
			dev_dbg (&iface->dev,
					"config [%d] descriptor --> %d\n",
					i, retval);
			return (retval < 0) ? retval : -EDOM;
		}

		// FIXME cross-checking udev->config[i] to make sure usbcore
		// parsed it right (etc) would be good testing paranoia
	}

	/* and sometimes [9.2.6.6] speed dependent descriptors */
	if (le16_to_cpu(udev->descriptor.bcdUSB) == 0x0200) {
		struct usb_qualifier_descriptor		*d = NULL;

		/* device qualifier [9.6.2] */
		retval = usb_get_descriptor (udev,
				USB_DT_DEVICE_QUALIFIER, 0, dev->buf,
				sizeof (struct usb_qualifier_descriptor));
		if (retval == -EPIPE) {
			if (udev->speed == USB_SPEED_HIGH) {
				dev_dbg (&iface->dev,
						"hs dev qualifier --> %d\n",
						retval);
				return (retval < 0) ? retval : -EDOM;
			}
			/* usb2.0 but not high-speed capable; fine */
		} else if (retval != sizeof (struct usb_qualifier_descriptor)) {
			dev_dbg (&iface->dev, "dev qualifier --> %d\n", retval);
			return (retval < 0) ? retval : -EDOM;
		} else
			d = (struct usb_qualifier_descriptor *) dev->buf;

		/* might not have [9.6.2] any other-speed configs [9.6.4] */
		if (d) {
			unsigned max = d->bNumConfigurations;
			for (i = 0; i < max; i++) {
				retval = usb_get_descriptor (udev,
					USB_DT_OTHER_SPEED_CONFIG, i,
					dev->buf, TBUF_SIZE);
				if (!is_good_config (dev->buf, retval)) {
					dev_dbg (&iface->dev,
						"other speed config --> %d\n",
						retval);
					return (retval < 0) ? retval : -EDOM;
				}
			}
		}
	}
	// FIXME fetch strings from at least the device descriptor

	/* [9.4.5] get_status always works */
	retval = usb_get_status (udev, USB_RECIP_DEVICE, 0, dev->buf);
	if (retval != 2) {
		dev_dbg (&iface->dev, "get dev status --> %d\n", retval);
		return (retval < 0) ? retval : -EDOM;
	}

	// FIXME configuration.bmAttributes says if we could try to set/clear
	// the device's remote wakeup feature ... if we can, test that here

	retval = usb_get_status (udev, USB_RECIP_INTERFACE,
			iface->altsetting [0].desc.bInterfaceNumber, dev->buf);
	if (retval != 2) {
		dev_dbg (&iface->dev, "get interface status --> %d\n", retval);
		return (retval < 0) ? retval : -EDOM;
	}
	// FIXME get status for each endpoint in the interface
	
	return 0;
}

/*-------------------------------------------------------------------------*/

/* use ch9 requests to test whether:
 *   (a) queues work for control, keeping N subtests queued and
 *       active (auto-resubmit) for M loops through the queue.
 *   (b) protocol stalls (control-only) will autorecover.
 *       it's not like bulk/intr; no halt clearing.
 *   (c) short control reads are reported and handled.
 *   (d) queues are always processed in-order
 */

struct ctrl_ctx {
	spinlock_t		lock;
	struct usbtest_dev	*dev;
	struct completion	complete;
	unsigned		count;
	unsigned		pending;
	int			status;
	struct urb		**urb;
	struct usbtest_param	*param;
	int			last;
};

#define NUM_SUBCASES	15		/* how many test subcases here? */

struct subcase {
	struct usb_ctrlrequest	setup;
	int			number;
	int			expected;
};

static void ctrl_complete (struct urb *urb, struct pt_regs *regs)
{
	struct ctrl_ctx		*ctx = urb->context;
	struct usb_ctrlrequest	*reqp;
	struct subcase		*subcase;
	int			status = urb->status;

	reqp = (struct usb_ctrlrequest *)urb->setup_packet;
	subcase = container_of (reqp, struct subcase, setup);

	spin_lock (&ctx->lock);
	ctx->count--;
	ctx->pending--;

	/* queue must transfer and complete in fifo order, unless
	 * usb_unlink_urb() is used to unlink something not at the
	 * physical queue head (not tested).
	 */
	if (subcase->number > 0) {
		if ((subcase->number - ctx->last) != 1) {
			dbg ("subcase %d completed out of order, last %d",
					subcase->number, ctx->last);
			status = -EDOM;
			ctx->last = subcase->number;
			goto error;
		}
	}
	ctx->last = subcase->number;

	/* succeed or fault in only one way? */
	if (status == subcase->expected)
		status = 0;

	/* async unlink for cleanup? */
	else if (status != -ECONNRESET) {

		/* some faults are allowed, not required */
		if (subcase->expected > 0 && (
			  ((urb->status == -subcase->expected	/* happened */
			   || urb->status == 0))))		/* didn't */
			status = 0;
		/* sometimes more than one fault is allowed */
		else if (subcase->number == 12 && status == -EPIPE)
			status = 0;
		else
			dbg ("subtest %d error, status %d",
					subcase->number, status);
	}

	/* unexpected status codes mean errors; ideally, in hardware */
	if (status) {
error:
		if (ctx->status == 0) {
			int		i;

			ctx->status = status;
			info ("control queue %02x.%02x, err %d, %d left",
					reqp->bRequestType, reqp->bRequest,
					status, ctx->count);

			/* FIXME this "unlink everything" exit route should
			 * be a separate test case.
			 */

			/* unlink whatever's still pending */
			for (i = 1; i < ctx->param->sglen; i++) {
				struct urb	*u = ctx->urb [
	(i + subcase->number) % ctx->param->sglen];

				if (u == urb || !u->dev)
					continue;
				status = usb_unlink_urb (u);
				switch (status) {
				case -EINPROGRESS:
				case -EBUSY:
				case -EIDRM:
					continue;
				default:
					dbg ("urb unlink --> %d", status);
				}
			}
			status = ctx->status;
		}
	}

	/* resubmit if we need to, else mark this as done */
	if ((status == 0) && (ctx->pending < ctx->count)) {
		if ((status = usb_submit_urb (urb, SLAB_ATOMIC)) != 0) {
			dbg ("can't resubmit ctrl %02x.%02x, err %d",
				reqp->bRequestType, reqp->bRequest, status);
			urb->dev = NULL;
		} else
			ctx->pending++;
	} else
		urb->dev = NULL;
	
	/* signal completion when nothing's queued */
	if (ctx->pending == 0)
		complete (&ctx->complete);
	spin_unlock (&ctx->lock);
}

static int
test_ctrl_queue (struct usbtest_dev *dev, struct usbtest_param *param)
{
	struct usb_device	*udev = testdev_to_usbdev (dev);
	struct urb		**urb;
	struct ctrl_ctx		context;
	int			i;

	spin_lock_init (&context.lock);
	context.dev = dev;
	init_completion (&context.complete);
	context.count = param->sglen * param->iterations;
	context.pending = 0;
	context.status = -ENOMEM;
	context.param = param;
	context.last = -1;

	/* allocate and init the urbs we'll queue.
	 * as with bulk/intr sglists, sglen is the queue depth; it also
	 * controls which subtests run (more tests than sglen) or rerun.
	 */
	urb = kmalloc (param->sglen * sizeof (struct urb *), SLAB_KERNEL);
	if (!urb)
		return -ENOMEM;
	memset (urb, 0, param->sglen * sizeof (struct urb *));
	for (i = 0; i < param->sglen; i++) {
		int			pipe = usb_rcvctrlpipe (udev, 0);
		unsigned		len;
		struct urb		*u;
		struct usb_ctrlrequest	req;
		struct subcase		*reqp;
		int			expected = 0;

		/* requests here are mostly expected to succeed on any
		 * device, but some are chosen to trigger protocol stalls
		 * or short reads.
		 */
		memset (&req, 0, sizeof req);
		req.bRequest = USB_REQ_GET_DESCRIPTOR;
		req.bRequestType = USB_DIR_IN|USB_RECIP_DEVICE;

		switch (i % NUM_SUBCASES) {
		case 0:		// get device descriptor
			req.wValue = cpu_to_le16 (USB_DT_DEVICE << 8);
			len = sizeof (struct usb_device_descriptor);
			break;
		case 1:		// get first config descriptor (only)
			req.wValue = cpu_to_le16 ((USB_DT_CONFIG << 8) | 0);
			len = sizeof (struct usb_config_descriptor);
			break;
		case 2:		// get altsetting (OFTEN STALLS)
			req.bRequest = USB_REQ_GET_INTERFACE;
			req.bRequestType = USB_DIR_IN|USB_RECIP_INTERFACE;
			// index = 0 means first interface
			len = 1;
			expected = EPIPE;
			break;
		case 3:		// get interface status
			req.bRequest = USB_REQ_GET_STATUS;
			req.bRequestType = USB_DIR_IN|USB_RECIP_INTERFACE;
			// interface 0
			len = 2;
			break;
		case 4:		// get device status
			req.bRequest = USB_REQ_GET_STATUS;
			req.bRequestType = USB_DIR_IN|USB_RECIP_DEVICE;
			len = 2;
			break;
		case 5:		// get device qualifier (MAY STALL)
			req.wValue = cpu_to_le16 (USB_DT_DEVICE_QUALIFIER << 8);
			len = sizeof (struct usb_qualifier_descriptor);
			if (udev->speed != USB_SPEED_HIGH)
				expected = EPIPE;
			break;
		case 6:		// get first config descriptor, plus interface
			req.wValue = cpu_to_le16 ((USB_DT_CONFIG << 8) | 0);
			len = sizeof (struct usb_config_descriptor);
			len += sizeof (struct usb_interface_descriptor);
			break;
		case 7:		// get interface descriptor (ALWAYS STALLS)
			req.wValue = cpu_to_le16 (USB_DT_INTERFACE << 8);
			// interface == 0
			len = sizeof (struct usb_interface_descriptor);
			expected = EPIPE;
			break;
		// NOTE: two consecutive stalls in the queue here.
		// that tests fault recovery a bit more aggressively.
		case 8:		// clear endpoint halt (USUALLY STALLS)
			req.bRequest = USB_REQ_CLEAR_FEATURE;
			req.bRequestType = USB_RECIP_ENDPOINT;
			// wValue 0 == ep halt
			// wIndex 0 == ep0 (shouldn't halt!)
			len = 0;
			pipe = usb_sndctrlpipe (udev, 0);
			expected = EPIPE;
			break;
		case 9:		// get endpoint status
			req.bRequest = USB_REQ_GET_STATUS;
			req.bRequestType = USB_DIR_IN|USB_RECIP_ENDPOINT;
			// endpoint 0
			len = 2;
			break;
		case 10:	// trigger short read (EREMOTEIO)
			req.wValue = cpu_to_le16 ((USB_DT_CONFIG << 8) | 0);
			len = 1024;
			expected = -EREMOTEIO;
			break;
		// NOTE: two consecutive _different_ faults in the queue.
		case 11:	// get endpoint descriptor (ALWAYS STALLS)
			req.wValue = cpu_to_le16 (USB_DT_ENDPOINT << 8);
			// endpoint == 0
			len = sizeof (struct usb_interface_descriptor);
			expected = EPIPE;
			break;
		// NOTE: sometimes even a third fault in the queue!
		case 12:	// get string 0 descriptor (MAY STALL)
			req.wValue = cpu_to_le16 (USB_DT_STRING << 8);
			// string == 0, for language IDs
			len = sizeof (struct usb_interface_descriptor);
			// may succeed when > 4 languages
			expected = EREMOTEIO;	// or EPIPE, if no strings
			break;
		case 13:	// short read, resembling case 10
			req.wValue = cpu_to_le16 ((USB_DT_CONFIG << 8) | 0);
			// last data packet "should" be DATA1, not DATA0
			len = 1024 - udev->descriptor.bMaxPacketSize0;
			expected = -EREMOTEIO;
			break;
		case 14:	// short read; try to fill the last packet
			req.wValue = cpu_to_le16 ((USB_DT_DEVICE << 8) | 0);
			// device descriptor size == 18 bytes 
			len = udev->descriptor.bMaxPacketSize0;
			switch (len) {
			case 8:		len = 24; break;
			case 16:	len = 32; break;
			}
			expected = -EREMOTEIO;
			break;
		default:
			err ("bogus number of ctrl queue testcases!");
			context.status = -EINVAL;
			goto cleanup;
		}
		req.wLength = cpu_to_le16 (len);
		urb [i] = u = simple_alloc_urb (udev, pipe, len);
		if (!u)
			goto cleanup;

		reqp = usb_buffer_alloc (udev, sizeof *reqp, SLAB_KERNEL,
				&u->setup_dma);
		if (!reqp)
			goto cleanup;
		reqp->setup = req;
		reqp->number = i % NUM_SUBCASES;
		reqp->expected = expected;
		u->setup_packet = (char *) &reqp->setup;
		u->transfer_flags |= URB_NO_SETUP_DMA_MAP;

		u->context = &context;
		u->complete = ctrl_complete;
	}

	/* queue the urbs */
	context.urb = urb;
	spin_lock_irq (&context.lock);
	for (i = 0; i < param->sglen; i++) {
		context.status = usb_submit_urb (urb [i], SLAB_ATOMIC);
		if (context.status != 0) {
			dbg ("can't submit urb[%d], status %d",
					i, context.status);
			context.count = context.pending;
			break;
		}
		context.pending++;
	}
	spin_unlock_irq (&context.lock);

	/* FIXME  set timer and time out; provide a disconnect hook */

	/* wait for the last one to complete */
	if (context.pending > 0)
		wait_for_completion (&context.complete);

cleanup:
	for (i = 0; i < param->sglen; i++) {
		if (!urb [i])
			continue;
		urb [i]->dev = udev;
		if (urb [i]->setup_packet)
			usb_buffer_free (udev, sizeof (struct usb_ctrlrequest),
					urb [i]->setup_packet,
					urb [i]->setup_dma);
		simple_free_urb (urb [i]);
	}
	kfree (urb);
	return context.status;
}
#undef NUM_SUBCASES


/*-------------------------------------------------------------------------*/

static void unlink1_callback (struct urb *urb, struct pt_regs *regs)
{
	int	status = urb->status;

	// we "know" -EPIPE (stall) never happens
	if (!status)
		status = usb_submit_urb (urb, SLAB_ATOMIC);
	if (status) {
		urb->status = status;
		complete ((struct completion *) urb->context);
	}
}

static int unlink1 (struct usbtest_dev *dev, int pipe, int size, int async)
{
	struct urb		*urb;
	struct completion	completion;
	int			retval = 0;

	init_completion (&completion);
	urb = simple_alloc_urb (testdev_to_usbdev (dev), pipe, size);
	if (!urb)
		return -ENOMEM;
	urb->context = &completion;
	urb->complete = unlink1_callback;

	/* keep the endpoint busy.  there are lots of hc/hcd-internal
	 * states, and testing should get to all of them over time.
	 *
	 * FIXME want additional tests for when endpoint is STALLing
	 * due to errors, or is just NAKing requests.
	 */
	if ((retval = usb_submit_urb (urb, SLAB_KERNEL)) != 0) {
		dev_dbg (&dev->intf->dev, "submit fail %d\n", retval);
		return retval;
	}

	/* unlinking that should always work.  variable delay tests more
	 * hcd states and code paths, even with little other system load.
	 */
	msleep (jiffies % (2 * INTERRUPT_RATE));
	if (async) {
retry:
		retval = usb_unlink_urb (urb);
		if (retval == -EBUSY || retval == -EIDRM) {
			/* we can't unlink urbs while they're completing.
			 * or if they've completed, and we haven't resubmitted.
			 * "normal" drivers would prevent resubmission, but
			 * since we're testing unlink paths, we can't.
			 */
			dev_dbg (&dev->intf->dev, "unlink retry\n");
			goto retry;
		}
	} else
		usb_kill_urb (urb);
	if (!(retval == 0 || retval == -EINPROGRESS)) {
		dev_dbg (&dev->intf->dev, "unlink fail %d\n", retval);
		return retval;
	}

	wait_for_completion (&completion);
	retval = urb->status;
	simple_free_urb (urb);

	if (async)
		return (retval == -ECONNRESET) ? 0 : retval - 1000;
	else
		return (retval == -ENOENT || retval == -EPERM) ?
				0 : retval - 2000;
}

static int unlink_simple (struct usbtest_dev *dev, int pipe, int len)
{
	int			retval = 0;

	/* test sync and async paths */
	retval = unlink1 (dev, pipe, len, 1);
	if (!retval)
		retval = unlink1 (dev, pipe, len, 0);
	return retval;
}

/*-------------------------------------------------------------------------*/

static int verify_not_halted (int ep, struct urb *urb)
{
	int	retval;
	u16	status;

	/* shouldn't look or act halted */
	retval = usb_get_status (urb->dev, USB_RECIP_ENDPOINT, ep, &status);
	if (retval < 0) {
		dbg ("ep %02x couldn't get no-halt status, %d", ep, retval);
		return retval;
	}
	if (status != 0) {
		dbg ("ep %02x bogus status: %04x != 0", ep, status);
		return -EINVAL;
	}
	retval = simple_io (urb, 1, 0, 0, __FUNCTION__);
	if (retval != 0)
		return -EINVAL;
	return 0;
}

static int verify_halted (int ep, struct urb *urb)
{
	int	retval;
	u16	status;

	/* should look and act halted */
	retval = usb_get_status (urb->dev, USB_RECIP_ENDPOINT, ep, &status);
	if (retval < 0) {
		dbg ("ep %02x couldn't get halt status, %d", ep, retval);
		return retval;
	}
	if (status != 1) {
		dbg ("ep %02x bogus status: %04x != 1", ep, status);
		return -EINVAL;
	}
	retval = simple_io (urb, 1, 0, -EPIPE, __FUNCTION__);
	if (retval != -EPIPE)
		return -EINVAL;
	retval = simple_io (urb, 1, 0, -EPIPE, "verify_still_halted");
	if (retval != -EPIPE)
		return -EINVAL;
	return 0;
}

static int test_halt (int ep, struct urb *urb)
{
	int	retval;

	/* shouldn't look or act halted now */
	retval = verify_not_halted (ep, urb);
	if (retval < 0)
		return retval;

	/* set halt (protocol test only), verify it worked */
	retval = usb_control_msg (urb->dev, usb_sndctrlpipe (urb->dev, 0),
			USB_REQ_SET_FEATURE, USB_RECIP_ENDPOINT,
			USB_ENDPOINT_HALT, ep,
			NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (retval < 0) {
		dbg ("ep %02x couldn't set halt, %d", ep, retval);
		return retval;
	}
	retval = verify_halted (ep, urb);
	if (retval < 0)
		return retval;

	/* clear halt (tests API + protocol), verify it worked */
	retval = usb_clear_halt (urb->dev, urb->pipe);
	if (retval < 0) {
		dbg ("ep %02x couldn't clear halt, %d", ep, retval);
		return retval;
	}
	retval = verify_not_halted (ep, urb);
	if (retval < 0)
		return retval;

	/* NOTE:  could also verify SET_INTERFACE clear halts ... */

	return 0;
}

static int halt_simple (struct usbtest_dev *dev)
{
	int		ep;
	int		retval = 0;
	struct urb	*urb;

	urb = simple_alloc_urb (testdev_to_usbdev (dev), 0, 512);
	if (urb == NULL)
		return -ENOMEM;

	if (dev->in_pipe) {
		ep = usb_pipeendpoint (dev->in_pipe) | USB_DIR_IN;
		urb->pipe = dev->in_pipe;
		retval = test_halt (ep, urb);
		if (retval < 0)
			goto done;
	}

	if (dev->out_pipe) {
		ep = usb_pipeendpoint (dev->out_pipe);
		urb->pipe = dev->out_pipe;
		retval = test_halt (ep, urb);
	}
done:
	simple_free_urb (urb);
	return retval;
}

/*-------------------------------------------------------------------------*/

/* Control OUT tests use the vendor control requests from Intel's
 * USB 2.0 compliance test device:  write a buffer, read it back.
 *
 * Intel's spec only _requires_ that it work for one packet, which
 * is pretty weak.   Some HCDs place limits here; most devices will
 * need to be able to handle more than one OUT data packet.  We'll
 * try whatever we're told to try.
 */
static int ctrl_out (struct usbtest_dev *dev,
		unsigned count, unsigned length, unsigned vary)
{
	unsigned		i, j, len, retval;
	u8			*buf;
	char			*what = "?";
	struct usb_device	*udev;
	
	if (length < 1 || length > 0xffff || vary >= length)
		return -EINVAL;

	buf = kmalloc(length, SLAB_KERNEL);
	if (!buf)
		return -ENOMEM;

	udev = testdev_to_usbdev (dev);
	len = length;
	retval = 0;

	/* NOTE:  hardware might well act differently if we pushed it
	 * with lots back-to-back queued requests.
	 */
	for (i = 0; i < count; i++) {
		/* write patterned data */
		for (j = 0; j < len; j++)
			buf [j] = i + j;
		retval = usb_control_msg (udev, usb_sndctrlpipe (udev,0),
				0x5b, USB_DIR_OUT|USB_TYPE_VENDOR,
				0, 0, buf, len, USB_CTRL_SET_TIMEOUT);
		if (retval != len) {
			what = "write";
			if (retval >= 0) {
				INFO(dev, "ctrl_out, wlen %d (expected %d)\n",
						retval, len);
				retval = -EBADMSG;
			}
			break;
		}

		/* read it back -- assuming nothing intervened!!  */
		retval = usb_control_msg (udev, usb_rcvctrlpipe (udev,0),
				0x5c, USB_DIR_IN|USB_TYPE_VENDOR,
				0, 0, buf, len, USB_CTRL_GET_TIMEOUT);
		if (retval != len) {
			what = "read";
			if (retval >= 0) {
				INFO(dev, "ctrl_out, rlen %d (expected %d)\n",
						retval, len);
				retval = -EBADMSG;
			}
			break;
		}

		/* fail if we can't verify */
		for (j = 0; j < len; j++) {
			if (buf [j] != (u8) (i + j)) {
				INFO (dev, "ctrl_out, byte %d is %d not %d\n",
					j, buf [j], (u8) i + j);
				retval = -EBADMSG;
				break;
			}
		}
		if (retval < 0) {
			what = "verify";
			break;
		}

		len += vary;

		/* [real world] the "zero bytes IN" case isn't really used.
		 * hardware can easily trip up in this wierd case, since its
		 * status stage is IN, not OUT like other ep0in transfers.
		 */
		if (len > length)
			len = realworld ? 1 : 0;
	}

	if (retval < 0)
		INFO (dev, "ctrl_out %s failed, code %d, count %d\n",
			what, retval, i);

	kfree (buf);
	return retval;
}

/*-------------------------------------------------------------------------*/

/* ISO tests ... mimics common usage
 *  - buffer length is split into N packets (mostly maxpacket sized)
 *  - multi-buffers according to sglen
 */

struct iso_context {
	unsigned		count;
	unsigned		pending;
	spinlock_t		lock;
	struct completion	done;
	unsigned long		errors;
	struct usbtest_dev	*dev;
};

static void iso_callback (struct urb *urb, struct pt_regs *regs)
{
	struct iso_context	*ctx = urb->context;

	spin_lock(&ctx->lock);
	ctx->count--;

	if (urb->error_count > 0)
		ctx->errors += urb->error_count;

	if (urb->status == 0 && ctx->count > (ctx->pending - 1)) {
		int status = usb_submit_urb (urb, GFP_ATOMIC);
		switch (status) {
		case 0:
			goto done;
		default:
			dev_dbg (&ctx->dev->intf->dev,
					"iso resubmit err %d\n",
					status);
			/* FALLTHROUGH */
		case -ENODEV:			/* disconnected */
			break;
		}
	}
	simple_free_urb (urb);

	ctx->pending--;
	if (ctx->pending == 0) {
		if (ctx->errors)
			dev_dbg (&ctx->dev->intf->dev,
				"iso test, %lu errors\n",
				ctx->errors);
		complete (&ctx->done);
	}
done:
	spin_unlock(&ctx->lock);
}

static struct urb *iso_alloc_urb (
	struct usb_device	*udev,
	int			pipe,
	struct usb_endpoint_descriptor	*desc,
	long			bytes
)
{
	struct urb		*urb;
	unsigned		i, maxp, packets;

	if (bytes < 0 || !desc)
		return NULL;
	maxp = 0x7ff & le16_to_cpu(desc->wMaxPacketSize);
	maxp *= 1 + (0x3 & (le16_to_cpu(desc->wMaxPacketSize) >> 11));
	packets = (bytes + maxp - 1) / maxp;

	urb = usb_alloc_urb (packets, SLAB_KERNEL);
	if (!urb)
		return urb;
	urb->dev = udev;
	urb->pipe = pipe;

	urb->number_of_packets = packets;
	urb->transfer_buffer_length = bytes;
	urb->transfer_buffer = usb_buffer_alloc (udev, bytes, SLAB_KERNEL,
			&urb->transfer_dma);
	if (!urb->transfer_buffer) {
		usb_free_urb (urb);
		return NULL;
	}
	memset (urb->transfer_buffer, 0, bytes);
	for (i = 0; i < packets; i++) {
		/* here, only the last packet will be short */
		urb->iso_frame_desc[i].length = min ((unsigned) bytes, maxp);
		bytes -= urb->iso_frame_desc[i].length;

		urb->iso_frame_desc[i].offset = maxp * i;
	}

	urb->complete = iso_callback;
	// urb->context = SET BY CALLER
	urb->interval = 1 << (desc->bInterval - 1);
	urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
	return urb;
}

static int
test_iso_queue (struct usbtest_dev *dev, struct usbtest_param *param,
		int pipe, struct usb_endpoint_descriptor *desc)
{
	struct iso_context	context;
	struct usb_device	*udev;
	unsigned		i;
	unsigned long		packets = 0;
	int			status;
	struct urb		*urbs[10];	/* FIXME no limit */

	if (param->sglen > 10)
		return -EDOM;

	context.count = param->iterations * param->sglen;
	context.pending = param->sglen;
	context.errors = 0;
	context.dev = dev;
	init_completion (&context.done);
	spin_lock_init (&context.lock);

	memset (urbs, 0, sizeof urbs);
	udev = testdev_to_usbdev (dev);
	dev_dbg (&dev->intf->dev,
		"... iso period %d %sframes, wMaxPacket %04x\n",
		1 << (desc->bInterval - 1),
		(udev->speed == USB_SPEED_HIGH) ? "micro" : "",
		le16_to_cpu(desc->wMaxPacketSize));

	for (i = 0; i < param->sglen; i++) {
		urbs [i] = iso_alloc_urb (udev, pipe, desc,
				param->length);
		if (!urbs [i]) {
			status = -ENOMEM;
			goto fail;
		}
		packets += urbs[i]->number_of_packets;
		urbs [i]->context = &context;
	}
	packets *= param->iterations;
	dev_dbg (&dev->intf->dev,
		"... total %lu msec (%lu packets)\n",
		(packets * (1 << (desc->bInterval - 1)))
			/ ((udev->speed == USB_SPEED_HIGH) ? 8 : 1),
		packets);

	spin_lock_irq (&context.lock);
	for (i = 0; i < param->sglen; i++) {
		status = usb_submit_urb (urbs [i], SLAB_ATOMIC);
		if (status < 0) {
			ERROR (dev, "submit iso[%d], error %d\n", i, status);
			if (i == 0) {
				spin_unlock_irq (&context.lock);
				goto fail;
			}

			simple_free_urb (urbs [i]);
			context.pending--;
		}
	}
	spin_unlock_irq (&context.lock);

	wait_for_completion (&context.done);
	return 0;

fail:
	for (i = 0; i < param->sglen; i++) {
		if (urbs [i])
			simple_free_urb (urbs [i]);
	}
	return status;
}

/*-------------------------------------------------------------------------*/

/* We only have this one interface to user space, through usbfs.
 * User mode code can scan usbfs to find N different devices (maybe on
 * different busses) to use when testing, and allocate one thread per
 * test.  So discovery is simplified, and we have no device naming issues.
 *
 * Don't use these only as stress/load tests.  Use them along with with
 * other USB bus activity:  plugging, unplugging, mousing, mp3 playback,
 * video capture, and so on.  Run different tests at different times, in
 * different sequences.  Nothing here should interact with other devices,
 * except indirectly by consuming USB bandwidth and CPU resources for test
 * threads and request completion.  But the only way to know that for sure
 * is to test when HC queues are in use by many devices.
 */

static int
usbtest_ioctl (struct usb_interface *intf, unsigned int code, void *buf)
{
	struct usbtest_dev	*dev = usb_get_intfdata (intf);
	struct usb_device	*udev = testdev_to_usbdev (dev);
	struct usbtest_param	*param = buf;
	int			retval = -EOPNOTSUPP;
	struct urb		*urb;
	struct scatterlist	*sg;
	struct usb_sg_request	req;
	struct timeval		start;
	unsigned		i;

	// FIXME USBDEVFS_CONNECTINFO doesn't say how fast the device is.

	if (code != USBTEST_REQUEST)
		return -EOPNOTSUPP;

	if (param->iterations <= 0 || param->length < 0
			|| param->sglen < 0 || param->vary < 0)
		return -EINVAL;

	if (down_interruptible (&dev->sem))
		return -ERESTARTSYS;

	if (intf->dev.power.power_state.event != PM_EVENT_ON) {
		up (&dev->sem);
		return -EHOSTUNREACH;
	}

	/* some devices, like ez-usb default devices, need a non-default
	 * altsetting to have any active endpoints.  some tests change
	 * altsettings; force a default so most tests don't need to check.
	 */
	if (dev->info->alt >= 0) {
	    	int	res;

		if (intf->altsetting->desc.bInterfaceNumber) {
			up (&dev->sem);
			return -ENODEV;
		}
		res = set_altsetting (dev, dev->info->alt);
		if (res) {
			dev_err (&intf->dev,
					"set altsetting to %d failed, %d\n",
					dev->info->alt, res);
			up (&dev->sem);
			return res;
		}
	}

	/*
	 * Just a bunch of test cases that every HCD is expected to handle.
	 *
	 * Some may need specific firmware, though it'd be good to have
	 * one firmware image to handle all the test cases.
	 *
	 * FIXME add more tests!  cancel requests, verify the data, control
	 * queueing, concurrent read+write threads, and so on.
	 */
	do_gettimeofday (&start);
	switch (param->test_num) {

	case 0:
		dev_dbg (&intf->dev, "TEST 0:  NOP\n");
		retval = 0;
		break;

	/* Simple non-queued bulk I/O tests */
	case 1:
		if (dev->out_pipe == 0)
			break;
		dev_dbg (&intf->dev,
				"TEST 1:  write %d bytes %u times\n",
				param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->out_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = simple_io (urb, param->iterations, 0, 0, "test1");
		simple_free_urb (urb);
		break;
	case 2:
		if (dev->in_pipe == 0)
			break;
		dev_dbg (&intf->dev,
				"TEST 2:  read %d bytes %u times\n",
				param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->in_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = simple_io (urb, param->iterations, 0, 0, "test2");
		simple_free_urb (urb);
		break;
	case 3:
		if (dev->out_pipe == 0 || param->vary == 0)
			break;
		dev_dbg (&intf->dev,
				"TEST 3:  write/%d 0..%d bytes %u times\n",
				param->vary, param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->out_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = simple_io (urb, param->iterations, param->vary,
					0, "test3");
		simple_free_urb (urb);
		break;
	case 4:
		if (dev->in_pipe == 0 || param->vary == 0)
			break;
		dev_dbg (&intf->dev,
				"TEST 4:  read/%d 0..%d bytes %u times\n",
				param->vary, param->length, param->iterations);
		urb = simple_alloc_urb (udev, dev->in_pipe, param->length);
		if (!urb) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = simple_io (urb, param->iterations, param->vary,
					0, "test4");
		simple_free_urb (urb);
		break;

	/* Queued bulk I/O tests */
	case 5:
		if (dev->out_pipe == 0 || param->sglen == 0)
			break;
		dev_dbg (&intf->dev,
			"TEST 5:  write %d sglists %d entries of %d bytes\n",
				param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, 0);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = perform_sglist (udev, param->iterations, dev->out_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;

	case 6:
		if (dev->in_pipe == 0 || param->sglen == 0)
			break;
		dev_dbg (&intf->dev,
			"TEST 6:  read %d sglists %d entries of %d bytes\n",
				param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, 0);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = perform_sglist (udev, param->iterations, dev->in_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;
	case 7:
		if (dev->out_pipe == 0 || param->sglen == 0 || param->vary == 0)
			break;
		dev_dbg (&intf->dev,
			"TEST 7:  write/%d %d sglists %d entries 0..%d bytes\n",
				param->vary, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, param->vary);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk sink (maybe accepts short writes)
		retval = perform_sglist (udev, param->iterations, dev->out_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;
	case 8:
		if (dev->in_pipe == 0 || param->sglen == 0 || param->vary == 0)
			break;
		dev_dbg (&intf->dev,
			"TEST 8:  read/%d %d sglists %d entries 0..%d bytes\n",
				param->vary, param->iterations,
				param->sglen, param->length);
		sg = alloc_sglist (param->sglen, param->length, param->vary);
		if (!sg) {
			retval = -ENOMEM;
			break;
		}
		// FIRMWARE:  bulk source (maybe generates short writes)
		retval = perform_sglist (udev, param->iterations, dev->in_pipe,
				&req, sg, param->sglen);
		free_sglist (sg, param->sglen);
		break;

	/* non-queued sanity tests for control (chapter 9 subset) */
	case 9:
		retval = 0;
		dev_dbg (&intf->dev,
			"TEST 9:  ch9 (subset) control tests, %d times\n",
				param->iterations);
		for (i = param->iterations; retval == 0 && i--; /* NOP */)
			retval = ch9_postconfig (dev);
		if (retval)
			dbg ("ch9 subset failed, iterations left %d", i);
		break;

	/* queued control messaging */
	case 10:
		if (param->sglen == 0)
			break;
		retval = 0;
		dev_dbg (&intf->dev,
				"TEST 10:  queue %d control calls, %d times\n",
				param->sglen,
				param->iterations);
		retval = test_ctrl_queue (dev, param);
		break;

	/* simple non-queued unlinks (ring with one urb) */
	case 11:
		if (dev->in_pipe == 0 || !param->length)
			break;
		retval = 0;
		dev_dbg (&intf->dev, "TEST 11:  unlink %d reads of %d\n",
				param->iterations, param->length);
		for (i = param->iterations; retval == 0 && i--; /* NOP */)
			retval = unlink_simple (dev, dev->in_pipe,
						param->length);
		if (retval)
			dev_dbg (&intf->dev, "unlink reads failed %d, "
				"iterations left %d\n", retval, i);
		break;
	case 12:
		if (dev->out_pipe == 0 || !param->length)
			break;
		retval = 0;
		dev_dbg (&intf->dev, "TEST 12:  unlink %d writes of %d\n",
				param->iterations, param->length);
		for (i = param->iterations; retval == 0 && i--; /* NOP */)
			retval = unlink_simple (dev, dev->out_pipe,
						param->length);
		if (retval)
			dev_dbg (&intf->dev, "unlink writes failed %d, "
				"iterations left %d\n", retval, i);
		break;

	/* ep halt tests */
	case 13:
		if (dev->out_pipe == 0 && dev->in_pipe == 0)
			break;
		retval = 0;
		dev_dbg (&intf->dev, "TEST 13:  set/clear %d halts\n",
				param->iterations);
		for (i = param->iterations; retval == 0 && i--; /* NOP */)
			retval = halt_simple (dev);
		
		if (retval)
			DBG (dev, "halts failed, iterations left %d\n", i);
		break;

	/* control write tests */
	case 14:
		if (!dev->info->ctrl_out)
			break;
		dev_dbg (&intf->dev, "TEST 14:  %d ep0out, %d..%d vary %d\n",
				param->iterations,
				realworld ? 1 : 0, param->length,
				param->vary);
		retval = ctrl_out (dev, param->iterations, 
				param->length, param->vary);
		break;

	/* iso write tests */
	case 15:
		if (dev->out_iso_pipe == 0 || param->sglen == 0)
			break;
		dev_dbg (&intf->dev, 
			"TEST 15:  write %d iso, %d entries of %d bytes\n",
				param->iterations,
				param->sglen, param->length);
		// FIRMWARE:  iso sink
		retval = test_iso_queue (dev, param,
				dev->out_iso_pipe, dev->iso_out);
		break;

	/* iso read tests */
	case 16:
		if (dev->in_iso_pipe == 0 || param->sglen == 0)
			break;
		dev_dbg (&intf->dev,
			"TEST 16:  read %d iso, %d entries of %d bytes\n",
				param->iterations,
				param->sglen, param->length);
		// FIRMWARE:  iso source
		retval = test_iso_queue (dev, param,
				dev->in_iso_pipe, dev->iso_in);
		break;

	// FIXME unlink from queue (ring with N urbs)

	// FIXME scatterlist cancel (needs helper thread)

	}
	do_gettimeofday (&param->duration);
	param->duration.tv_sec -= start.tv_sec;
	param->duration.tv_usec -= start.tv_usec;
	if (param->duration.tv_usec < 0) {
		param->duration.tv_usec += 1000 * 1000;
		param->duration.tv_sec -= 1;
	}
	up (&dev->sem);
	return retval;
}

/*-------------------------------------------------------------------------*/

static unsigned force_interrupt = 0;
module_param (force_interrupt, uint, 0);
MODULE_PARM_DESC (force_interrupt, "0 = test default; else interrupt");

#ifdef	GENERIC
static unsigned short vendor;
module_param(vendor, ushort, 0);
MODULE_PARM_DESC (vendor, "vendor code (from usb-if)");

static unsigned short product;
module_param(product, ushort, 0);
MODULE_PARM_DESC (product, "product code (from vendor)");
#endif

static int
usbtest_probe (struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device	*udev;
	struct usbtest_dev	*dev;
	struct usbtest_info	*info;
	char			*rtest, *wtest;
	char			*irtest, *iwtest;

	udev = interface_to_usbdev (intf);

#ifdef	GENERIC
	/* specify devices by module parameters? */
	if (id->match_flags == 0) {
		/* vendor match required, product match optional */
		if (!vendor || le16_to_cpu(udev->descriptor.idVendor) != (u16)vendor)
			return -ENODEV;
		if (product && le16_to_cpu(udev->descriptor.idProduct) != (u16)product)
			return -ENODEV;
		dbg ("matched module params, vend=0x%04x prod=0x%04x",
				le16_to_cpu(udev->descriptor.idVendor),
				le16_to_cpu(udev->descriptor.idProduct));
	}
#endif

	dev = kmalloc (sizeof *dev, SLAB_KERNEL);
	if (!dev)
		return -ENOMEM;
	memset (dev, 0, sizeof *dev);
	info = (struct usbtest_info *) id->driver_info;
	dev->info = info;
	init_MUTEX (&dev->sem);

	dev->intf = intf;

	/* cacheline-aligned scratch for i/o */
	if ((dev->buf = kmalloc (TBUF_SIZE, SLAB_KERNEL)) == NULL) {
		kfree (dev);
		return -ENOMEM;
	}

	/* NOTE this doesn't yet test the handful of difference that are
	 * visible with high speed interrupts:  bigger maxpacket (1K) and
	 * "high bandwidth" modes (up to 3 packets/uframe).
	 */
	rtest = wtest = "";
	irtest = iwtest = "";
	if (force_interrupt || udev->speed == USB_SPEED_LOW) {
		if (info->ep_in) {
			dev->in_pipe = usb_rcvintpipe (udev, info->ep_in);
			rtest = " intr-in";
		}
		if (info->ep_out) {
			dev->out_pipe = usb_sndintpipe (udev, info->ep_out);
			wtest = " intr-out";
		}
	} else {
		if (info->autoconf) {
			int status;

			status = get_endpoints (dev, intf);
			if (status < 0) {
				dbg ("couldn't get endpoints, %d\n", status);
				return status;
			}
			/* may find bulk or ISO pipes */
		} else {
			if (info->ep_in)
				dev->in_pipe = usb_rcvbulkpipe (udev,
							info->ep_in);
			if (info->ep_out)
				dev->out_pipe = usb_sndbulkpipe (udev,
							info->ep_out);
		}
		if (dev->in_pipe)
			rtest = " bulk-in";
		if (dev->out_pipe)
			wtest = " bulk-out";
		if (dev->in_iso_pipe)
			irtest = " iso-in";
		if (dev->out_iso_pipe)
			iwtest = " iso-out";
	}

	usb_set_intfdata (intf, dev);
	dev_info (&intf->dev, "%s\n", info->name);
	dev_info (&intf->dev, "%s speed {control%s%s%s%s%s} tests%s\n",
			({ char *tmp;
			switch (udev->speed) {
			case USB_SPEED_LOW: tmp = "low"; break;
			case USB_SPEED_FULL: tmp = "full"; break;
			case USB_SPEED_HIGH: tmp = "high"; break;
			default: tmp = "unknown"; break;
			}; tmp; }),
			info->ctrl_out ? " in/out" : "",
			rtest, wtest,
			irtest, iwtest,
			info->alt >= 0 ? " (+alt)" : "");
	return 0;
}

static int usbtest_suspend (struct usb_interface *intf, pm_message_t message)
{
	return 0;
}

static int usbtest_resume (struct usb_interface *intf)
{
	return 0;
}


static void usbtest_disconnect (struct usb_interface *intf)
{
	struct usbtest_dev	*dev = usb_get_intfdata (intf);

	down (&dev->sem);

	usb_set_intfdata (intf, NULL);
	dev_dbg (&intf->dev, "disconnect\n");
	kfree (dev);
}

/* Basic testing only needs a device that can source or sink bulk traffic.
 * Any device can test control transfers (default with GENERIC binding).
 *
 * Several entries work with the default EP0 implementation that's built
 * into EZ-USB chips.  There's a default vendor ID which can be overridden
 * by (very) small config EEPROMS, but otherwise all these devices act
 * identically until firmware is loaded:  only EP0 works.  It turns out
 * to be easy to make other endpoints work, without modifying that EP0
 * behavior.  For now, we expect that kind of firmware.
 */

/* an21xx or fx versions of ez-usb */
static struct usbtest_info ez1_info = {
	.name		= "EZ-USB device",
	.ep_in		= 2,
	.ep_out		= 2,
	.alt		= 1,
};

/* fx2 version of ez-usb */
static struct usbtest_info ez2_info = {
	.name		= "FX2 device",
	.ep_in		= 6,
	.ep_out		= 2,
	.alt		= 1,
};

/* ezusb family device with dedicated usb test firmware,
 */
static struct usbtest_info fw_info = {
	.name		= "usb test device",
	.ep_in		= 2,
	.ep_out		= 2,
	.alt		= 1,
	.autoconf	= 1,		// iso and ctrl_out need autoconf
	.ctrl_out	= 1,
	.iso		= 1,		// iso_ep's are #8 in/out
};

/* peripheral running Linux and 'zero.c' test firmware, or
 * its user-mode cousin. different versions of this use
 * different hardware with the same vendor/product codes.
 * host side MUST rely on the endpoint descriptors.
 */
static struct usbtest_info gz_info = {
	.name		= "Linux gadget zero",
	.autoconf	= 1,
	.ctrl_out	= 1,
	.alt		= 0,
};

static struct usbtest_info um_info = {
	.name		= "Linux user mode test driver",
	.autoconf	= 1,
	.alt		= -1,
};

static struct usbtest_info um2_info = {
	.name		= "Linux user mode ISO test driver",
	.autoconf	= 1,
	.iso		= 1,
	.alt		= -1,
};

#ifdef IBOT2
/* this is a nice source of high speed bulk data;
 * uses an FX2, with firmware provided in the device
 */
static struct usbtest_info ibot2_info = {
	.name		= "iBOT2 webcam",
	.ep_in		= 2,
	.alt		= -1,
};
#endif

#ifdef GENERIC
/* we can use any device to test control traffic */
static struct usbtest_info generic_info = {
	.name		= "Generic USB device",
	.alt		= -1,
};
#endif

// FIXME remove this 
static struct usbtest_info hact_info = {
	.name		= "FX2/hact",
	//.ep_in		= 6,
	.ep_out		= 2,
	.alt		= -1,
};


static struct usb_device_id id_table [] = {

	{ USB_DEVICE (0x0547, 0x1002),
		.driver_info = (unsigned long) &hact_info,
		},

	/*-------------------------------------------------------------*/

	/* EZ-USB devices which download firmware to replace (or in our
	 * case augment) the default device implementation.
	 */

	/* generic EZ-USB FX controller */
	{ USB_DEVICE (0x0547, 0x2235),
		.driver_info = (unsigned long) &ez1_info,
		},

	/* CY3671 development board with EZ-USB FX */
	{ USB_DEVICE (0x0547, 0x0080),
		.driver_info = (unsigned long) &ez1_info,
		},

	/* generic EZ-USB FX2 controller (or development board) */
	{ USB_DEVICE (0x04b4, 0x8613),
		.driver_info = (unsigned long) &ez2_info,
		},

	/* re-enumerated usb test device firmware */
	{ USB_DEVICE (0xfff0, 0xfff0),
		.driver_info = (unsigned long) &fw_info,
		},

	/* "Gadget Zero" firmware runs under Linux */
	{ USB_DEVICE (0x0525, 0xa4a0),
		.driver_info = (unsigned long) &gz_info,
		},

	/* so does a user-mode variant */
	{ USB_DEVICE (0x0525, 0xa4a4),
		.driver_info = (unsigned long) &um_info,
		},

	/* ... and a user-mode variant that talks iso */
	{ USB_DEVICE (0x0525, 0xa4a3),
		.driver_info = (unsigned long) &um2_info,
		},

#ifdef KEYSPAN_19Qi
	/* Keyspan 19qi uses an21xx (original EZ-USB) */
	// this does not coexist with the real Keyspan 19qi driver!
	{ USB_DEVICE (0x06cd, 0x010b),
		.driver_info = (unsigned long) &ez1_info,
		},
#endif

	/*-------------------------------------------------------------*/

#ifdef IBOT2
	/* iBOT2 makes a nice source of high speed bulk-in data */
	// this does not coexist with a real iBOT2 driver!
	{ USB_DEVICE (0x0b62, 0x0059),
		.driver_info = (unsigned long) &ibot2_info,
		},
#endif

	/*-------------------------------------------------------------*/

#ifdef GENERIC
	/* module params can specify devices to use for control tests */
	{ .driver_info = (unsigned long) &generic_info, },
#endif

	/*-------------------------------------------------------------*/

	{ }
};
MODULE_DEVICE_TABLE (usb, id_table);

static struct usb_driver usbtest_driver = {
	.owner =	THIS_MODULE,
	.name =		"usbtest",
	.id_table =	id_table,
	.probe =	usbtest_probe,
	.ioctl =	usbtest_ioctl,
	.disconnect =	usbtest_disconnect,
	.suspend =	usbtest_suspend,
	.resume =	usbtest_resume,
};

/*-------------------------------------------------------------------------*/

static int __init usbtest_init (void)
{
#ifdef GENERIC
	if (vendor)
		dbg ("params: vend=0x%04x prod=0x%04x", vendor, product);
#endif
	return usb_register (&usbtest_driver);
}
module_init (usbtest_init);

static void __exit usbtest_exit (void)
{
	usb_deregister (&usbtest_driver);
}
module_exit (usbtest_exit);

MODULE_DESCRIPTION ("USB Core/HCD Testing Driver");
MODULE_LICENSE ("GPL");

