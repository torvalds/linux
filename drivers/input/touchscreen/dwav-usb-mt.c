//[*]-------------------------------------------------------------------------[*]
//
// D-WAV Scientific USB(HID) MultiTouch Screen Driver(Based on usbtouchscreen.c)
// Hardkernel : 2015/09/17
//
//[*]-------------------------------------------------------------------------[*]
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

#include <linux/input/mt.h>

//[*]-------------------------------------------------------------------------[*]
#define DWAV_TOUCH_MAX_X            800
#define DWAV_TOUCH_MAX_Y            480
#define DWAV_TOUCH_MAX_ID           5
#define DWAV_TOUCH_MAX_PRESSURE     255

//[*]-------------------------------------------------------------------------[*]
#define USB_VENDOR_ID_DWAV		        0x0eef
#define USB_DEVICE_ID_DWAV_MULTITOUCH   0x0005

static const struct usb_device_id dwav_usb_mt_devices[] = {
	{USB_DEVICE(USB_VENDOR_ID_DWAV, USB_DEVICE_ID_DWAV_MULTITOUCH), 0},
	{}
};

//[*]-------------------------------------------------------------------------[*]
struct dwav_raw {               /* Total 25 bytes */
    unsigned char   header;     /* frame header 0xAA*/
    unsigned char   press;      /* Touch flag (1:valid touch data, 0:touch finished) */
    unsigned short  x1;         /* 1st x */
    unsigned short  y1;         /* 1st y */
    unsigned char   end;        /* 1st touch finish flags 0xBB, RPI only uses the first 7 bytes */
    unsigned char   ids;        /* touch ID(bit field) */ 
    unsigned short  y2;
    unsigned short  x2;
    unsigned short  y3;
    unsigned short  x3;
    unsigned short  y4;
    unsigned short  x4;
    unsigned short  y5;
    unsigned short  x5;
    unsigned char   tail;       /* frame end 0xCC */
};

//[*]-------------------------------------------------------------------------[*]
// Touch Event type define
//[*]-------------------------------------------------------------------------[*]
#define	TS_EVENT_UNKNOWN	0x00
#define	TS_EVENT_PRESS		0x01
#define	TS_EVENT_RELEASE	0x02

//[*]-------------------------------------------------------------------------[*]
typedef	struct	finger__t	{
	unsigned int	status;			// ts event type
	unsigned int	x;				// ts data x
	unsigned int	y;				// ts data y
}	__attribute__ ((packed))	finger_t;

//[*]-------------------------------------------------------------------------[*]
struct dwav_usb_mt  {
	char            name[128], phys[64];

    // for URB Data DMA
	dma_addr_t      data_dma;
	unsigned char   *data;
	int             data_size;

	struct urb              *irq;
	struct usb_interface    *interface;
	struct input_dev        *input;

	finger_t        *finger;
};

//[*]-------------------------------------------------------------------------[*]
static void dwav_usb_mt_report(struct dwav_usb_mt *dwav_usb_mt)
{
	int		id;

	for(id = 0; id < DWAV_TOUCH_MAX_ID; id++)	{

		if(dwav_usb_mt->finger[id].status == TS_EVENT_UNKNOWN)  continue;

		if( dwav_usb_mt->finger[id].x >= DWAV_TOUCH_MAX_X ||
			dwav_usb_mt->finger[id].y >= DWAV_TOUCH_MAX_Y  )	continue;

    	input_mt_slot(dwav_usb_mt->input, id);

		if(dwav_usb_mt->finger[id].status != TS_EVENT_RELEASE)	{
            input_mt_report_slot_state(dwav_usb_mt->input, MT_TOOL_FINGER, true);
			input_report_abs(dwav_usb_mt->input, ABS_MT_POSITION_X,  dwav_usb_mt->finger[id].x);
			input_report_abs(dwav_usb_mt->input, ABS_MT_POSITION_Y,	 dwav_usb_mt->finger[id].y);
			input_report_abs(dwav_usb_mt->input, ABS_MT_PRESSURE, DWAV_TOUCH_MAX_PRESSURE);
		}
		else	{
            input_mt_report_slot_state(dwav_usb_mt->input, MT_TOOL_FINGER, false);
		    dwav_usb_mt->finger[id].status = TS_EVENT_UNKNOWN;
		}
		input_mt_report_pointer_emulation(dwav_usb_mt->input, true);
	    input_sync(dwav_usb_mt->input);
	}
}

//[*]-------------------------------------------------------------------------[*]
static void dwav_usb_mt_process(struct dwav_usb_mt *dwav_usb_mt,
                                   unsigned char *pkt, int len)
{
    struct  dwav_raw    *dwav_raw = (struct dwav_raw *)pkt;
    unsigned char       bit_mask, cnt;

    for(cnt = 0, bit_mask = 0x01; cnt < DWAV_TOUCH_MAX_ID; cnt++, bit_mask <<= 1)   {
        if((dwav_raw->ids & bit_mask) && dwav_raw->press)    {
            dwav_usb_mt->finger[cnt].status = TS_EVENT_PRESS;
            switch(cnt) {
                case    0:
                    dwav_usb_mt->finger[cnt].x = cpu_to_be16(dwav_raw->x1);
                    dwav_usb_mt->finger[cnt].y = cpu_to_be16(dwav_raw->y1);
                    break;
                case    1:
                    dwav_usb_mt->finger[cnt].x = cpu_to_be16(dwav_raw->x2);
                    dwav_usb_mt->finger[cnt].y = cpu_to_be16(dwav_raw->y2);
                    break;
                case    2:
                    dwav_usb_mt->finger[cnt].x = cpu_to_be16(dwav_raw->x3);
                    dwav_usb_mt->finger[cnt].y = cpu_to_be16(dwav_raw->y3);
                    break;
                case    3:
                    dwav_usb_mt->finger[cnt].x = cpu_to_be16(dwav_raw->x4);
                    dwav_usb_mt->finger[cnt].y = cpu_to_be16(dwav_raw->y4);
                    break;
                case    4:
                    dwav_usb_mt->finger[cnt].x = cpu_to_be16(dwav_raw->x5);
                    dwav_usb_mt->finger[cnt].y = cpu_to_be16(dwav_raw->y5);
                    break;
                default :
                    break;
            }
        }
        else    {
            if(dwav_usb_mt->finger[cnt].status == TS_EVENT_PRESS)
                dwav_usb_mt->finger[cnt].status = TS_EVENT_RELEASE;
            else
                dwav_usb_mt->finger[cnt].status = TS_EVENT_UNKNOWN;
        }
    }
    dwav_usb_mt_report(dwav_usb_mt);
}

//[*]-------------------------------------------------------------------------[*]
static void dwav_usb_mt_irq(struct urb *urb)
{
	struct dwav_usb_mt *dwav_usb_mt = urb->context;
	struct device *dev = &dwav_usb_mt->interface->dev;
	int retval;

	switch (urb->status) {
    	case 0:
    		/* success */
    		break;
    	case -ETIME:
    		/* this urb is timing out */
    		dev_dbg(dev, "%s - urb timed out - was the device unplugged?\n", __func__);
    		return;
    	case -ECONNRESET:
    	case -ENOENT:
    	case -ESHUTDOWN:
    	case -EPIPE:
    		/* this urb is terminated, clean up */
    		dev_dbg(dev, "%s - urb shutting down with status: %d\n", __func__, urb->status);
    		return;
    	default:
    		dev_dbg(dev, "%s - nonzero urb status received: %d\n", __func__, urb->status);
    		goto exit;
	}

    dwav_usb_mt_process(dwav_usb_mt, dwav_usb_mt->data, urb->actual_length);

exit:
	usb_mark_last_busy(interface_to_usbdev(dwav_usb_mt->interface));
	if ((retval = usb_submit_urb(urb, GFP_ATOMIC))) {
		dev_err(dev, "%s - usb_submit_urb failed with result: %d\n", __func__, retval);
	}
}

//[*]-------------------------------------------------------------------------[*]
static int dwav_usb_mt_open (struct input_dev *input)
{
	struct dwav_usb_mt *dwav_usb_mt = input_get_drvdata(input);
	int r;

	dwav_usb_mt->irq->dev = interface_to_usbdev(dwav_usb_mt->interface);

	r = usb_autopm_get_interface(dwav_usb_mt->interface) ? -EIO : 0;
	if (r < 0)
		goto out;

	if (usb_submit_urb(dwav_usb_mt->irq, GFP_KERNEL)) {
		r = -EIO;
		goto out_put;
	}

	dwav_usb_mt->interface->needs_remote_wakeup = 1;
out_put:
	usb_autopm_put_interface(dwav_usb_mt->interface);
out:
	return r;
}

//[*]-------------------------------------------------------------------------[*]
static void dwav_usb_mt_close   (struct input_dev *input)
{
	struct dwav_usb_mt *dwav_usb_mt = input_get_drvdata(input);
	int r;

	usb_kill_urb(dwav_usb_mt->irq);

	r = usb_autopm_get_interface(dwav_usb_mt->interface);

	dwav_usb_mt->interface->needs_remote_wakeup = 0;
	if (!r)
		usb_autopm_put_interface(dwav_usb_mt->interface);
}

//[*]-------------------------------------------------------------------------[*]
static int dwav_usb_mt_suspend  (struct usb_interface *intf, pm_message_t message)
{
	struct dwav_usb_mt *dwav_usb_mt = usb_get_intfdata(intf);

	usb_kill_urb(dwav_usb_mt->irq);

	return 0;
}

//[*]-------------------------------------------------------------------------[*]
static int dwav_usb_mt_resume   (struct usb_interface *intf)
{
	struct dwav_usb_mt *dwav_usb_mt = usb_get_intfdata(intf);
	struct input_dev *input = dwav_usb_mt->input;
	int result = 0;

	mutex_lock(&input->mutex);
	if (input->users)
		result = usb_submit_urb(dwav_usb_mt->irq, GFP_NOIO);
	mutex_unlock(&input->mutex);

	return result;
}

//[*]-------------------------------------------------------------------------[*]
static int dwav_usb_mt_reset_resume   (struct usb_interface *intf)
{
	struct dwav_usb_mt *dwav_usb_mt = usb_get_intfdata(intf);
	struct input_dev *input = dwav_usb_mt->input;
	int err = 0;

	/* restart IO if needed */
	mutex_lock(&input->mutex);
	if (input->users)
		err = usb_submit_urb(dwav_usb_mt->irq, GFP_NOIO);
	mutex_unlock(&input->mutex);

	return err;
}

//[*]-------------------------------------------------------------------------[*]
static void dwav_usb_mt_free_buffers(struct usb_device *udev,
				  struct dwav_usb_mt *dwav_usb_mt)
{
	usb_free_coherent(udev, dwav_usb_mt->data_size,
			  dwav_usb_mt->data, dwav_usb_mt->data_dma);
}

//[*]-------------------------------------------------------------------------[*]
static struct usb_endpoint_descriptor *
    dwav_usb_mt_get_input_endpoint(struct usb_host_interface *interface)
{
	int i;

	for (i = 0; i < interface->desc.bNumEndpoints; i++) {
		if (usb_endpoint_dir_in(&interface->endpoint[i].desc))
			return &interface->endpoint[i].desc;
	}

	return NULL;
}

//[*]-------------------------------------------------------------------------[*]
static int dwav_usb_mt_init (struct dwav_usb_mt *dwav_usb_mt, void *dev)
{
    int err;
    struct input_dev *input_dev = (struct input_dev *)dev;

	input_dev->name = dwav_usb_mt->name;
	input_dev->phys = dwav_usb_mt->phys;

	input_set_drvdata(input_dev, dwav_usb_mt);

	input_dev->open     = dwav_usb_mt_open;
	input_dev->close    = dwav_usb_mt_close;

	input_dev->id.bustype 	= BUS_USB;

    // single touch
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, DWAV_TOUCH_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, DWAV_TOUCH_MAX_Y, 0, 0);

	/* multi touch */
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, DWAV_TOUCH_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, DWAV_TOUCH_MAX_Y, 0, 0);
	input_mt_init_slots (input_dev, DWAV_TOUCH_MAX_ID, 0);

	if ((err = input_register_device(input_dev))) {
		pr_err("%s - input_register_device failed, err: %d\n", __func__, err);
        return  err;
	}

	dwav_usb_mt->input = input_dev;

    return  0;
}

//[*]-------------------------------------------------------------------------[*]
static int dwav_usb_mt_probe(struct usb_interface *intf,
			  const struct usb_device_id *id)
{
	struct dwav_usb_mt *dwav_usb_mt = NULL;
	struct input_dev *input_dev = NULL;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *udev = interface_to_usbdev(intf);

	int err = 0;

    if(!(endpoint = dwav_usb_mt_get_input_endpoint(intf->cur_altsetting)))
        return  -ENXIO;

    if(!(dwav_usb_mt = kzalloc(sizeof(struct dwav_usb_mt), GFP_KERNEL)))
        return  -ENOMEM;

	if(!(dwav_usb_mt->finger = kzalloc(sizeof(finger_t) * DWAV_TOUCH_MAX_ID , GFP_KERNEL)))
        goto    err_free_mem;

    if(!(input_dev = input_allocate_device()))
        goto    err_free_mem;

    dwav_usb_mt->data_size  = sizeof(struct dwav_raw);
	dwav_usb_mt->data       = usb_alloc_coherent(udev, dwav_usb_mt->data_size,
					                                GFP_KERNEL, &dwav_usb_mt->data_dma);
	if (!dwav_usb_mt->data)
		goto err_free_mem;

	if (!(dwav_usb_mt->irq = usb_alloc_urb(0, GFP_KERNEL))) {
		dev_dbg(&intf->dev, "%s - usb_alloc_urb failed: usbtouch->irq\n", __func__);
		goto err_free_buffers;
	}

	if (usb_endpoint_type(endpoint) == USB_ENDPOINT_XFER_INT)   {
		usb_fill_int_urb(dwav_usb_mt->irq, udev,
		    usb_rcvintpipe(udev, endpoint->bEndpointAddress),
		    dwav_usb_mt->data, dwav_usb_mt->data_size,
		    dwav_usb_mt_irq, dwav_usb_mt, endpoint->bInterval);
	}
	else    {
		usb_fill_bulk_urb(dwav_usb_mt->irq, udev,
			 usb_rcvbulkpipe(udev, endpoint->bEndpointAddress),
			 dwav_usb_mt->data, dwav_usb_mt->data_size,
			 dwav_usb_mt_irq, dwav_usb_mt);
	}

	dwav_usb_mt->irq->dev = udev;
	dwav_usb_mt->irq->transfer_dma = dwav_usb_mt->data_dma;
	dwav_usb_mt->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	dwav_usb_mt->interface  = intf;

	if (udev->manufacturer) {
		strlcpy(dwav_usb_mt->name, udev->manufacturer, sizeof(dwav_usb_mt->name));
	}

	if (udev->product) {
		if (udev->manufacturer) strlcat(dwav_usb_mt->name, " ", sizeof(dwav_usb_mt->name));

		strlcat(dwav_usb_mt->name, udev->product, sizeof(dwav_usb_mt->name));
	}

	if (!strlen(dwav_usb_mt->name)) {
		snprintf(dwav_usb_mt->name, sizeof(dwav_usb_mt->name),
        			"D-WAV Scientific MultiTouch %04x:%04x",
        			 le16_to_cpu(udev->descriptor.idVendor),
        			 le16_to_cpu(udev->descriptor.idProduct));
	}

	usb_make_path(udev, dwav_usb_mt->phys, sizeof(dwav_usb_mt->phys));
	strlcat(dwav_usb_mt->phys, "/input0", sizeof(dwav_usb_mt->phys));

	usb_to_input_id(udev, &input_dev->id);

	input_dev->dev.parent = &intf->dev;

    if((err = dwav_usb_mt_init (dwav_usb_mt, (void *)input_dev)))
        goto    err_free_urb;

	usb_set_intfdata(intf, dwav_usb_mt);

	return 0;

err_free_urb:
	usb_free_urb(dwav_usb_mt->irq);

err_free_buffers:
    dwav_usb_mt_free_buffers(udev, dwav_usb_mt);

err_free_mem:
    if(input_dev)   input_free_device(input_dev);
	if(dwav_usb_mt) kfree(dwav_usb_mt);

	return err;
}

//[*]-------------------------------------------------------------------------[*]
static void dwav_usb_mt_disconnect  (struct usb_interface *intf)
{
	struct dwav_usb_mt *dwav_usb_mt = usb_get_intfdata(intf);

	if (!dwav_usb_mt)   return;

	dev_dbg(&intf->dev, "%s - dwav_usb_mt is initialized, cleaning up\n", __func__);

	usb_set_intfdata(intf, NULL);

	/* this will stop IO via close */
	input_unregister_device(dwav_usb_mt->input);

	usb_free_urb(dwav_usb_mt->irq);

	dwav_usb_mt_free_buffers(interface_to_usbdev(intf), dwav_usb_mt);

	kfree(dwav_usb_mt);
}

//[*]-------------------------------------------------------------------------[*]
MODULE_DEVICE_TABLE(usb, dwav_usb_mt_devices);

//[*]-------------------------------------------------------------------------[*]
static struct usb_driver dwav_usb_mt_driver = {
	.name		    = "dwav_usb_mt",
	.probe		    = dwav_usb_mt_probe,
	.disconnect	    = dwav_usb_mt_disconnect,
	.suspend	    = dwav_usb_mt_suspend,
	.resume		    = dwav_usb_mt_resume,
	.reset_resume	= dwav_usb_mt_reset_resume,
	.id_table	    = dwav_usb_mt_devices,
	.supports_autosuspend = 1,
};

//[*]-------------------------------------------------------------------------[*]
module_usb_driver(dwav_usb_mt_driver);

//[*]-------------------------------------------------------------------------[*]
MODULE_AUTHOR("Hardkernel Co.,Ltd");
MODULE_DESCRIPTION("D-WAV USB(HID) MultiTouch Driver");
MODULE_LICENSE("GPL");

MODULE_ALIAS("dwav_usb_mt");

//[*]-------------------------------------------------------------------------[*]
