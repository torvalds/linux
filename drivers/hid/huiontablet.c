/*
 *  Copyright (c) 2013 Tan Huang,Shenzhen Huion 
 *
 *  USB HID Tablet support 
 */



#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>
#define DEVICE_NAME "huiontablet"

/* for apple IDs */
#ifdef CONFIG_USB_HID_MODULE
#include "./hid-ids.h"
#endif

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "Tan Huang <tanhuang@huion.cn>"
#define DRIVER_DESC "USB HID Boot Protocol tablet driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

struct usb_mouse {
	char name[128];
	char phys[64];
	struct usb_device *usbdev;
	struct input_dev *dev;
	struct urb *irq;

	signed char *data;
	dma_addr_t data_dma;
};



unsigned short int pos_x=0,pos_y=0,pos_z=0;

//static struct miscdevice misc = {
//	.minor		= MISC_DYNAMIC_MINOR,
//	.name		= DEVICE_NAME,
//	.fops		= &dev_fops,
//};


static void usb_mouse_irq(struct urb *urb)
{
	struct usb_mouse *mouse = urb->context;
	signed char *data = mouse->data;
	struct input_dev *dev = mouse->dev;
	int status;
        //unsigned short int pos_x=0,pos_y=0,pos_z=0;
       // printk(KERN_NOTICE "enter usb_mouse_irq function\n");
	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	default:		/* error */
		goto resubmit;
	}

        pos_x =(data[3] & 0x0FF)<<8;
        pos_x |=(data[2] & 0x0FF);
        pos_y =(data[5] & 0x0FF)<<8;
        pos_y |=(data[4] & 0x0FF);
        pos_z =(data[7] & 0x0FF)<<8;
        pos_z |=(data[6] & 0x0FF);
        printk(KERN_NOTICE "pos_x=%x,pos_y=%x,pos_z=%x,button=%x\n",pos_x,pos_y,pos_z,(data[1] & 0x0F));
        input_report_abs(dev, ABS_X, pos_x);
        input_report_abs(dev, ABS_Y, pos_y);
        input_report_key(dev, BTN_TOOL_PEN,1);
        input_report_abs(dev, ABS_PRESSURE,pos_z);
        input_report_key(dev, BTN_LEFT,data[1] & 0x01); 
        input_report_key(dev, BTN_TOUCH,data[1] & 0x01);
        input_report_key(dev, BTN_MIDDLE,data[1] & 0x02); 
        input_report_key(dev, BTN_RIGHT,data[1] & 0x04); 

	input_sync(dev);
resubmit:
	status = usb_submit_urb (urb, GFP_ATOMIC);
	/*  Not sure what the function 'err' would have been, but holy shit this code is UGLY.
		Do people really still use the goto statement?
		if (status)
			err ("can't resubmit intr, %s-%s/input0, status %d",
					mouse->usbdev->bus->bus_name,
					mouse->usbdev->devpath, status);
   	    // printk(KERN_NOTICE "quit usb_mouse_irq function\n");
	*/
}

static int usb_mouse_open(struct input_dev *dev)
{
	struct usb_mouse *mouse = input_get_drvdata(dev);
     //   printk(KERN_NOTICE "enter usinput_report_key(dev, BTN_TOUCH,(pos_z>0)?1:0); b_mouse_open function\n");
	mouse->irq->dev = mouse->usbdev;
	if (usb_submit_urb(mouse->irq, GFP_KERNEL))
		return -EIO;
      //  printk(KERN_NOTICE "quit usb_mouse_open function\n");
	return 0;
}

static void usb_mouse_close(struct input_dev *dev)
{
	struct usb_mouse *mouse = input_get_drvdata(dev);
       // printk(KERN_NOTICE "enter usb_mouse_close function\n");
	usb_kill_urb(mouse->irq);
      //  printk(KERN_NOTICE "quit usb_mouse_close function\n");
}

static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_mouse *mouse;
	struct input_dev *input_dev;
	int pipe, maxp;
	int error = -ENOMEM;
       // printk(KERN_NOTICE "enter usb_mouse_probe function\n");
	interface = intf->cur_altsetting;

	if (interface->desc.bNumEndpoints != 1)
		return -ENODEV;

	endpoint = &interface->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(endpoint))
		return -ENODEV;

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	mouse = kzalloc(sizeof(struct usb_mouse), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!mouse || !input_dev)
		goto fail1;

	mouse->data = usb_alloc_coherent(dev, 8, GFP_ATOMIC, &mouse->data_dma);
	if (!mouse->data)
		goto fail1;

	mouse->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!mouse->irq)
		goto fail2;

	mouse->usbdev = dev;
	mouse->dev = input_dev;

	if (dev->manufacturer)
		strlcpy(mouse->name, dev->manufacturer, sizeof(mouse->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(mouse->name, " ", sizeof(mouse->name));
		strlcat(mouse->name, dev->product, sizeof(mouse->name));
	}

	if (!strlen(mouse->name))
		snprintf(mouse->name, sizeof(mouse->name),
			 "USB HIDBP Mouse %04x:%04x",
			 le16_to_cpu(dev->descriptor.idVendor),
			 le16_to_cpu(dev->descriptor.idProduct));

	usb_make_path(dev, mouse->phys, sizeof(mouse->phys));
	strlcat(mouse->phys, "/input0", sizeof(mouse->phys));

	input_dev->name = mouse->name;
	input_dev->phys = mouse->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

        set_bit(EV_MSC, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_ABS, input_dev->evbit);
        set_bit(ABS_X, input_dev->absbit);
        set_bit(ABS_Y, input_dev->absbit);
        set_bit(ABS_PRESSURE, input_dev->absbit);
	set_bit(BTN_TOUCH, input_dev->keybit);
        set_bit(BTN_TOOL_PEN, input_dev->keybit);
        set_bit(BTN_LEFT, input_dev->keybit);
        set_bit(BTN_MIDDLE, input_dev->keybit);
        set_bit(BTN_RIGHT, input_dev->keybit);
        set_bit(MSC_SERIAL, input_dev->mscbit);
       
	input_set_drvdata(input_dev, mouse);

        input_set_abs_params(input_dev, ABS_X, 0, 0x7ff, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 0x7ff, 0, 0);
        input_set_abs_params(input_dev, ABS_PRESSURE, 0, 1024, 0, 0);

	input_dev->open = usb_mouse_open;
	input_dev->close = usb_mouse_close;

	usb_fill_int_urb(mouse->irq, dev, pipe, mouse->data,
			 (maxp > 8 ? 8 : maxp),
			 usb_mouse_irq, mouse, endpoint->bInterval);
	mouse->irq->transfer_dma = mouse->data_dma;
	mouse->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(mouse->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, mouse);
	return 0;

fail3:	
	usb_free_urb(mouse->irq);
fail2:	
	usb_free_coherent(dev, 8, mouse->data, mouse->data_dma);
fail1:	
	input_free_device(input_dev);
	kfree(mouse);
      //  printk(KERN_NOTICE "quit usb_mouse_probe function\n");
	return error;
}

static void usb_mouse_disconnect(struct usb_interface *intf)
{
	struct usb_mouse *mouse = usb_get_intfdata (intf);
        //printk(KERN_NOTICE "enter usb_mouse_disconnect function\n");
	usb_set_intfdata(intf, NULL);
	if (mouse) {
		usb_kill_urb(mouse->irq);
		input_unregister_device(mouse->dev);
		usb_free_urb(mouse->irq);
		usb_free_coherent(interface_to_usbdev(intf), 8, mouse->data, mouse->data_dma);
		kfree(mouse);
	}
 //       misc_deregister(&misc);
       // printk(KERN_NOTICE "quit usb_mouse_disconnect function\n");
}

static struct usb_device_id usb_mouse_id_table [] = {
	//{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,USB_INTERFACE_PROTOCOL_MOUSE) },
        { USB_DEVICE(0x256C, 0x0005) },
        { USB_DEVICE(0x256C, 0x006E) },
        { USB_DEVICE(0x5543, 0x0005) },
        { USB_DEVICE(0x5543, 0x006E) },
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_mouse_id_table);


static struct usb_driver usb_mouse_driver = {
	.name		= "usbmouse",
	.probe		= usb_mouse_probe,
	.disconnect	= usb_mouse_disconnect,
	.id_table	= usb_mouse_id_table,
};

static int __init usb_mouse_init(void)
{
	int retval = usb_register(&usb_mouse_driver);
	if (retval == 0)
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
				DRIVER_DESC "\n");
	return retval;
}

static void __exit usb_mouse_exit(void)
{
	usb_deregister(&usb_mouse_driver);
}

module_init(usb_mouse_init);
module_exit(usb_mouse_exit);
