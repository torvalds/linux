/*
 * A driver for the Griffin Technology, Inc. "PowerMate" USB controller dial.
 *
 * v1.1, (c)2002 William R Sowerbutts <will@sowerbutts.com>
 *
 * This device is a anodised aluminium knob which connects over USB. It can measure
 * clockwise and anticlockwise rotation. The dial also acts as a pushbutton with
 * a spring for automatic release. The base contains a pair of LEDs which illuminate
 * the translucent base. It rotates without limit and reports its relative rotation
 * back to the host when polled by the USB controller.
 *
 * Testing with the knob I have has shown that it measures approximately 94 "clicks"
 * for one full rotation. Testing with my High Speed Rotation Actuator (ok, it was
 * a variable speed cordless electric drill) has shown that the device can measure
 * speeds of up to 7 clicks either clockwise or anticlockwise between pollings from
 * the host. If it counts more than 7 clicks before it is polled, it will wrap back
 * to zero and start counting again. This was at quite high speed, however, almost
 * certainly faster than the human hand could turn it. Griffin say that it loses a
 * pulse or two on a direction change; the granularity is so fine that I never
 * noticed this in practice.
 *
 * The device's microcontroller can be programmed to set the LED to either a constant
 * intensity, or to a rhythmic pulsing. Several patterns and speeds are available.
 *
 * Griffin were very happy to provide documentation and free hardware for development.
 *
 * Some userspace tools are available on the web: http://sowerbutts.com/powermate/
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/usb/input.h>

#define POWERMATE_VENDOR	0x077d	/* Griffin Technology, Inc. */
#define POWERMATE_PRODUCT_NEW	0x0410	/* Griffin PowerMate */
#define POWERMATE_PRODUCT_OLD	0x04AA	/* Griffin soundKnob */

#define CONTOUR_VENDOR		0x05f3	/* Contour Design, Inc. */
#define CONTOUR_JOG		0x0240	/* Jog and Shuttle */

/* these are the command codes we send to the device */
#define SET_STATIC_BRIGHTNESS  0x01
#define SET_PULSE_ASLEEP       0x02
#define SET_PULSE_AWAKE        0x03
#define SET_PULSE_MODE         0x04

/* these refer to bits in the powermate_device's requires_update field. */
#define UPDATE_STATIC_BRIGHTNESS (1<<0)
#define UPDATE_PULSE_ASLEEP      (1<<1)
#define UPDATE_PULSE_AWAKE       (1<<2)
#define UPDATE_PULSE_MODE        (1<<3)

/* at least two versions of the hardware exist, with differing payload
   sizes. the first three bytes always contain the "interesting" data in
   the relevant format. */
#define POWERMATE_PAYLOAD_SIZE_MAX 6
#define POWERMATE_PAYLOAD_SIZE_MIN 3
struct powermate_device {
	signed char *data;
	dma_addr_t data_dma;
	struct urb *irq, *config;
	struct usb_ctrlrequest *configcr;
	struct usb_device *udev;
	struct usb_interface *intf;
	struct input_dev *input;
	spinlock_t lock;
	int static_brightness;
	int pulse_speed;
	int pulse_table;
	int pulse_asleep;
	int pulse_awake;
	int requires_update; // physical settings which are out of sync
	char phys[64];
};

static char pm_name_powermate[] = "Griffin PowerMate";
static char pm_name_soundknob[] = "Griffin SoundKnob";

static void powermate_config_complete(struct urb *urb);

/* Callback for data arriving from the PowerMate over the USB interrupt pipe */
static void powermate_irq(struct urb *urb)
{
	struct powermate_device *pm = urb->context;
	struct device *dev = &pm->intf->dev;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(dev, "%s - urb shutting down with status: %d\n",
			__func__, urb->status);
		return;
	default:
		dev_dbg(dev, "%s - nonzero urb status received: %d\n",
			__func__, urb->status);
		goto exit;
	}

	/* handle updates to device state */
	input_report_key(pm->input, BTN_0, pm->data[0] & 0x01);
	input_report_rel(pm->input, REL_DIAL, pm->data[1]);
	input_sync(pm->input);

exit:
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		dev_err(dev, "%s - usb_submit_urb failed with result: %d\n",
			__func__, retval);
}

/* Decide if we need to issue a control message and do so. Must be called with pm->lock taken */
static void powermate_sync_state(struct powermate_device *pm)
{
	if (pm->requires_update == 0)
		return; /* no updates are required */
	if (pm->config->status == -EINPROGRESS)
		return; /* an update is already in progress; it'll issue this update when it completes */

	if (pm->requires_update & UPDATE_PULSE_ASLEEP){
		pm->configcr->wValue = cpu_to_le16( SET_PULSE_ASLEEP );
		pm->configcr->wIndex = cpu_to_le16( pm->pulse_asleep ? 1 : 0 );
		pm->requires_update &= ~UPDATE_PULSE_ASLEEP;
	}else if (pm->requires_update & UPDATE_PULSE_AWAKE){
		pm->configcr->wValue = cpu_to_le16( SET_PULSE_AWAKE );
		pm->configcr->wIndex = cpu_to_le16( pm->pulse_awake ? 1 : 0 );
		pm->requires_update &= ~UPDATE_PULSE_AWAKE;
	}else if (pm->requires_update & UPDATE_PULSE_MODE){
		int op, arg;
		/* the powermate takes an operation and an argument for its pulse algorithm.
		   the operation can be:
		   0: divide the speed
		   1: pulse at normal speed
		   2: multiply the speed
		   the argument only has an effect for operations 0 and 2, and ranges between
		   1 (least effect) to 255 (maximum effect).

		   thus, several states are equivalent and are coalesced into one state.

		   we map this onto a range from 0 to 510, with:
		   0 -- 254    -- use divide (0 = slowest)
		   255         -- use normal speed
		   256 -- 510  -- use multiple (510 = fastest).

		   Only values of 'arg' quite close to 255 are particularly useful/spectacular.
		*/
		if (pm->pulse_speed < 255) {
			op = 0;                   // divide
			arg = 255 - pm->pulse_speed;
		} else if (pm->pulse_speed > 255) {
			op = 2;                   // multiply
			arg = pm->pulse_speed - 255;
		} else {
			op = 1;                   // normal speed
			arg = 0;                  // can be any value
		}
		pm->configcr->wValue = cpu_to_le16( (pm->pulse_table << 8) | SET_PULSE_MODE );
		pm->configcr->wIndex = cpu_to_le16( (arg << 8) | op );
		pm->requires_update &= ~UPDATE_PULSE_MODE;
	} else if (pm->requires_update & UPDATE_STATIC_BRIGHTNESS) {
		pm->configcr->wValue = cpu_to_le16( SET_STATIC_BRIGHTNESS );
		pm->configcr->wIndex = cpu_to_le16( pm->static_brightness );
		pm->requires_update &= ~UPDATE_STATIC_BRIGHTNESS;
	} else {
		printk(KERN_ERR "powermate: unknown update required");
		pm->requires_update = 0; /* fudge the bug */
		return;
	}

/*	printk("powermate: %04x %04x\n", pm->configcr->wValue, pm->configcr->wIndex); */

	pm->configcr->bRequestType = 0x41; /* vendor request */
	pm->configcr->bRequest = 0x01;
	pm->configcr->wLength = 0;

	usb_fill_control_urb(pm->config, pm->udev, usb_sndctrlpipe(pm->udev, 0),
			     (void *) pm->configcr, NULL, 0,
			     powermate_config_complete, pm);

	if (usb_submit_urb(pm->config, GFP_ATOMIC))
		printk(KERN_ERR "powermate: usb_submit_urb(config) failed");
}

/* Called when our asynchronous control message completes. We may need to issue another immediately */
static void powermate_config_complete(struct urb *urb)
{
	struct powermate_device *pm = urb->context;
	unsigned long flags;

	if (urb->status)
		printk(KERN_ERR "powermate: config urb returned %d\n", urb->status);

	spin_lock_irqsave(&pm->lock, flags);
	powermate_sync_state(pm);
	spin_unlock_irqrestore(&pm->lock, flags);
}

/* Set the LED up as described and begin the sync with the hardware if required */
static void powermate_pulse_led(struct powermate_device *pm, int static_brightness, int pulse_speed,
				int pulse_table, int pulse_asleep, int pulse_awake)
{
	unsigned long flags;

	if (pulse_speed < 0)
		pulse_speed = 0;
	if (pulse_table < 0)
		pulse_table = 0;
	if (pulse_speed > 510)
		pulse_speed = 510;
	if (pulse_table > 2)
		pulse_table = 2;

	pulse_asleep = !!pulse_asleep;
	pulse_awake = !!pulse_awake;


	spin_lock_irqsave(&pm->lock, flags);

	/* mark state updates which are required */
	if (static_brightness != pm->static_brightness) {
		pm->static_brightness = static_brightness;
		pm->requires_update |= UPDATE_STATIC_BRIGHTNESS;
	}
	if (pulse_asleep != pm->pulse_asleep) {
		pm->pulse_asleep = pulse_asleep;
		pm->requires_update |= (UPDATE_PULSE_ASLEEP | UPDATE_STATIC_BRIGHTNESS);
	}
	if (pulse_awake != pm->pulse_awake) {
		pm->pulse_awake = pulse_awake;
		pm->requires_update |= (UPDATE_PULSE_AWAKE | UPDATE_STATIC_BRIGHTNESS);
	}
	if (pulse_speed != pm->pulse_speed || pulse_table != pm->pulse_table) {
		pm->pulse_speed = pulse_speed;
		pm->pulse_table = pulse_table;
		pm->requires_update |= UPDATE_PULSE_MODE;
	}

	powermate_sync_state(pm);

	spin_unlock_irqrestore(&pm->lock, flags);
}

/* Callback from the Input layer when an event arrives from userspace to configure the LED */
static int powermate_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int _value)
{
	unsigned int command = (unsigned int)_value;
	struct powermate_device *pm = input_get_drvdata(dev);

	if (type == EV_MSC && code == MSC_PULSELED){
		/*
		    bits  0- 7: 8 bits: LED brightness
		    bits  8-16: 9 bits: pulsing speed modifier (0 ... 510); 0-254 = slower, 255 = standard, 256-510 = faster.
		    bits 17-18: 2 bits: pulse table (0, 1, 2 valid)
		    bit     19: 1 bit : pulse whilst asleep?
		    bit     20: 1 bit : pulse constantly?
		*/
		int static_brightness = command & 0xFF;   // bits 0-7
		int pulse_speed = (command >> 8) & 0x1FF; // bits 8-16
		int pulse_table = (command >> 17) & 0x3;  // bits 17-18
		int pulse_asleep = (command >> 19) & 0x1; // bit 19
		int pulse_awake  = (command >> 20) & 0x1; // bit 20

		powermate_pulse_led(pm, static_brightness, pulse_speed, pulse_table, pulse_asleep, pulse_awake);
	}

	return 0;
}

static int powermate_alloc_buffers(struct usb_device *udev, struct powermate_device *pm)
{
	pm->data = usb_alloc_coherent(udev, POWERMATE_PAYLOAD_SIZE_MAX,
				      GFP_ATOMIC, &pm->data_dma);
	if (!pm->data)
		return -1;

	pm->configcr = kmalloc(sizeof(*(pm->configcr)), GFP_KERNEL);
	if (!pm->configcr)
		return -ENOMEM;

	return 0;
}

static void powermate_free_buffers(struct usb_device *udev, struct powermate_device *pm)
{
	usb_free_coherent(udev, POWERMATE_PAYLOAD_SIZE_MAX,
			  pm->data, pm->data_dma);
	kfree(pm->configcr);
}

/* Called whenever a USB device matching one in our supported devices table is connected */
static int powermate_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev (intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct powermate_device *pm;
	struct input_dev *input_dev;
	int pipe, maxp;
	int error = -ENOMEM;

	interface = intf->cur_altsetting;
	endpoint = &interface->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(endpoint))
		return -EIO;

	usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
		0x0a, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		0, interface->desc.bInterfaceNumber, NULL, 0,
		USB_CTRL_SET_TIMEOUT);

	pm = kzalloc(sizeof(struct powermate_device), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!pm || !input_dev)
		goto fail1;

	if (powermate_alloc_buffers(udev, pm))
		goto fail2;

	pm->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!pm->irq)
		goto fail2;

	pm->config = usb_alloc_urb(0, GFP_KERNEL);
	if (!pm->config)
		goto fail3;

	pm->udev = udev;
	pm->intf = intf;
	pm->input = input_dev;

	usb_make_path(udev, pm->phys, sizeof(pm->phys));
	strlcat(pm->phys, "/input0", sizeof(pm->phys));

	spin_lock_init(&pm->lock);

	switch (le16_to_cpu(udev->descriptor.idProduct)) {
	case POWERMATE_PRODUCT_NEW:
		input_dev->name = pm_name_powermate;
		break;
	case POWERMATE_PRODUCT_OLD:
		input_dev->name = pm_name_soundknob;
		break;
	default:
		input_dev->name = pm_name_soundknob;
		printk(KERN_WARNING "powermate: unknown product id %04x\n",
		       le16_to_cpu(udev->descriptor.idProduct));
	}

	input_dev->phys = pm->phys;
	usb_to_input_id(udev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, pm);

	input_dev->event = powermate_input_event;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL) |
		BIT_MASK(EV_MSC);
	input_dev->keybit[BIT_WORD(BTN_0)] = BIT_MASK(BTN_0);
	input_dev->relbit[BIT_WORD(REL_DIAL)] = BIT_MASK(REL_DIAL);
	input_dev->mscbit[BIT_WORD(MSC_PULSELED)] = BIT_MASK(MSC_PULSELED);

	/* get a handle to the interrupt data pipe */
	pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(udev, pipe, usb_pipeout(pipe));

	if (maxp < POWERMATE_PAYLOAD_SIZE_MIN || maxp > POWERMATE_PAYLOAD_SIZE_MAX) {
		printk(KERN_WARNING "powermate: Expected payload of %d--%d bytes, found %d bytes!\n",
			POWERMATE_PAYLOAD_SIZE_MIN, POWERMATE_PAYLOAD_SIZE_MAX, maxp);
		maxp = POWERMATE_PAYLOAD_SIZE_MAX;
	}

	usb_fill_int_urb(pm->irq, udev, pipe, pm->data,
			 maxp, powermate_irq,
			 pm, endpoint->bInterval);
	pm->irq->transfer_dma = pm->data_dma;
	pm->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* register our interrupt URB with the USB system */
	if (usb_submit_urb(pm->irq, GFP_KERNEL)) {
		error = -EIO;
		goto fail4;
	}

	error = input_register_device(pm->input);
	if (error)
		goto fail5;


	/* force an update of everything */
	pm->requires_update = UPDATE_PULSE_ASLEEP | UPDATE_PULSE_AWAKE | UPDATE_PULSE_MODE | UPDATE_STATIC_BRIGHTNESS;
	powermate_pulse_led(pm, 0x80, 255, 0, 1, 0); // set default pulse parameters

	usb_set_intfdata(intf, pm);
	return 0;

 fail5:	usb_kill_urb(pm->irq);
 fail4:	usb_free_urb(pm->config);
 fail3:	usb_free_urb(pm->irq);
 fail2:	powermate_free_buffers(udev, pm);
 fail1:	input_free_device(input_dev);
	kfree(pm);
	return error;
}

/* Called when a USB device we've accepted ownership of is removed */
static void powermate_disconnect(struct usb_interface *intf)
{
	struct powermate_device *pm = usb_get_intfdata (intf);

	usb_set_intfdata(intf, NULL);
	if (pm) {
		pm->requires_update = 0;
		usb_kill_urb(pm->irq);
		input_unregister_device(pm->input);
		usb_free_urb(pm->irq);
		usb_free_urb(pm->config);
		powermate_free_buffers(interface_to_usbdev(intf), pm);

		kfree(pm);
	}
}

static struct usb_device_id powermate_devices [] = {
	{ USB_DEVICE(POWERMATE_VENDOR, POWERMATE_PRODUCT_NEW) },
	{ USB_DEVICE(POWERMATE_VENDOR, POWERMATE_PRODUCT_OLD) },
	{ USB_DEVICE(CONTOUR_VENDOR, CONTOUR_JOG) },
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, powermate_devices);

static struct usb_driver powermate_driver = {
        .name =         "powermate",
        .probe =        powermate_probe,
        .disconnect =   powermate_disconnect,
        .id_table =     powermate_devices,
};

module_usb_driver(powermate_driver);

MODULE_AUTHOR( "William R Sowerbutts" );
MODULE_DESCRIPTION( "Griffin Technology, Inc PowerMate driver" );
MODULE_LICENSE("GPL");
