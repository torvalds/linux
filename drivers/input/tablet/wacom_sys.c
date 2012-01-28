/*
 * drivers/input/tablet/wacom_sys.c
 *
 *  USB Wacom tablet support - system specific code
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "wacom_wac.h"
#include "wacom.h"

/* defines to get HID report descriptor */
#define HID_DEVICET_HID		(USB_TYPE_CLASS | 0x01)
#define HID_DEVICET_REPORT	(USB_TYPE_CLASS | 0x02)
#define HID_USAGE_UNDEFINED		0x00
#define HID_USAGE_PAGE			0x05
#define HID_USAGE_PAGE_DIGITIZER	0x0d
#define HID_USAGE_PAGE_DESKTOP		0x01
#define HID_USAGE			0x09
#define HID_USAGE_X			0x30
#define HID_USAGE_Y			0x31
#define HID_USAGE_X_TILT		0x3d
#define HID_USAGE_Y_TILT		0x3e
#define HID_USAGE_FINGER		0x22
#define HID_USAGE_STYLUS		0x20
#define HID_COLLECTION			0xa1
#define HID_COLLECTION_LOGICAL		0x02
#define HID_COLLECTION_END		0xc0

enum {
	WCM_UNDEFINED = 0,
	WCM_DESKTOP,
	WCM_DIGITIZER,
};

struct hid_descriptor {
	struct usb_descriptor_header header;
	__le16   bcdHID;
	u8       bCountryCode;
	u8       bNumDescriptors;
	u8       bDescriptorType;
	__le16   wDescriptorLength;
} __attribute__ ((packed));

/* defines to get/set USB message */
#define USB_REQ_GET_REPORT	0x01
#define USB_REQ_SET_REPORT	0x09

#define WAC_HID_FEATURE_REPORT	0x03
#define WAC_MSG_RETRIES		5

#define WAC_CMD_LED_CONTROL	0x20
#define WAC_CMD_ICON_START	0x21
#define WAC_CMD_ICON_XFER	0x23
#define WAC_CMD_RETRIES		10

static int wacom_get_report(struct usb_interface *intf, u8 type, u8 id,
			    void *buf, size_t size, unsigned int retries)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	int retval;

	do {
		retval = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				USB_REQ_GET_REPORT,
				USB_DIR_IN | USB_TYPE_CLASS |
				USB_RECIP_INTERFACE,
				(type << 8) + id,
				intf->altsetting[0].desc.bInterfaceNumber,
				buf, size, 100);
	} while ((retval == -ETIMEDOUT || retval == -EPIPE) && --retries);

	return retval;
}

static int wacom_set_report(struct usb_interface *intf, u8 type, u8 id,
			    void *buf, size_t size, unsigned int retries)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	int retval;

	do {
		retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				USB_REQ_SET_REPORT,
				USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				(type << 8) + id,
				intf->altsetting[0].desc.bInterfaceNumber,
				buf, size, 1000);
	} while ((retval == -ETIMEDOUT || retval == -EPIPE) && --retries);

	return retval;
}

static void wacom_sys_irq(struct urb *urb)
{
	struct wacom *wacom = urb->context;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __func__, urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __func__, urb->status);
		goto exit;
	}

	wacom_wac_irq(&wacom->wacom_wac, urb->actual_length);

 exit:
	usb_mark_last_busy(wacom->usbdev);
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		err ("%s - usb_submit_urb failed with result %d",
		     __func__, retval);
}

static int wacom_open(struct input_dev *dev)
{
	struct wacom *wacom = input_get_drvdata(dev);
	int retval = 0;

	if (usb_autopm_get_interface(wacom->intf) < 0)
		return -EIO;

	mutex_lock(&wacom->lock);

	if (usb_submit_urb(wacom->irq, GFP_KERNEL)) {
		retval = -EIO;
		goto out;
	}

	wacom->open = true;
	wacom->intf->needs_remote_wakeup = 1;

out:
	mutex_unlock(&wacom->lock);
	usb_autopm_put_interface(wacom->intf);
	return retval;
}

static void wacom_close(struct input_dev *dev)
{
	struct wacom *wacom = input_get_drvdata(dev);
	int autopm_error;

	autopm_error = usb_autopm_get_interface(wacom->intf);

	mutex_lock(&wacom->lock);
	usb_kill_urb(wacom->irq);
	wacom->open = false;
	wacom->intf->needs_remote_wakeup = 0;
	mutex_unlock(&wacom->lock);

	if (!autopm_error)
		usb_autopm_put_interface(wacom->intf);
}

static int wacom_parse_logical_collection(unsigned char *report,
					  struct wacom_features *features)
{
	int length = 0;

	if (features->type == BAMBOO_PT) {

		/* Logical collection is only used by 3rd gen Bamboo Touch */
		features->pktlen = WACOM_PKGLEN_BBTOUCH3;
		features->device_type = BTN_TOOL_DOUBLETAP;

		/*
		 * Stylus and Touch have same active area
		 * so compute physical size based on stylus
		 * data before its overwritten.
		 */
		features->x_phy =
			(features->x_max * features->x_resolution) / 100;
		features->y_phy =
			(features->y_max * features->y_resolution) / 100;

		features->x_max = features->y_max =
			get_unaligned_le16(&report[10]);

		length = 11;
	}
	return length;
}

/*
 * Interface Descriptor of wacom devices can be incomplete and
 * inconsistent so wacom_features table is used to store stylus
 * device's packet lengths, various maximum values, and tablet
 * resolution based on product ID's.
 *
 * For devices that contain 2 interfaces, wacom_features table is
 * inaccurate for the touch interface.  Since the Interface Descriptor
 * for touch interfaces has pretty complete data, this function exists
 * to query tablet for this missing information instead of hard coding in
 * an additional table.
 *
 * A typical Interface Descriptor for a stylus will contain a
 * boot mouse application collection that is not of interest and this
 * function will ignore it.
 *
 * It also contains a digitizer application collection that also is not
 * of interest since any information it contains would be duplicate
 * of what is in wacom_features. Usually it defines a report of an array
 * of bytes that could be used as max length of the stylus packet returned.
 * If it happens to define a Digitizer-Stylus Physical Collection then
 * the X and Y logical values contain valid data but it is ignored.
 *
 * A typical Interface Descriptor for a touch interface will contain a
 * Digitizer-Finger Physical Collection which will define both logical
 * X/Y maximum as well as the physical size of tablet. Since touch
 * interfaces haven't supported pressure or distance, this is enough
 * information to override invalid values in the wacom_features table.
 *
 * 3rd gen Bamboo Touch no longer define a Digitizer-Finger Pysical
 * Collection. Instead they define a Logical Collection with a single
 * Logical Maximum for both X and Y.
 */
static int wacom_parse_hid(struct usb_interface *intf,
			   struct hid_descriptor *hid_desc,
			   struct wacom_features *features)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	char limit = 0;
	/* result has to be defined as int for some devices */
	int result = 0;
	int i = 0, usage = WCM_UNDEFINED, finger = 0, pen = 0;
	unsigned char *report;

	report = kzalloc(hid_desc->wDescriptorLength, GFP_KERNEL);
	if (!report)
		return -ENOMEM;

	/* retrive report descriptors */
	do {
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			USB_REQ_GET_DESCRIPTOR,
			USB_RECIP_INTERFACE | USB_DIR_IN,
			HID_DEVICET_REPORT << 8,
			intf->altsetting[0].desc.bInterfaceNumber, /* interface */
			report,
			hid_desc->wDescriptorLength,
			5000); /* 5 secs */
	} while (result < 0 && limit++ < WAC_MSG_RETRIES);

	/* No need to parse the Descriptor. It isn't an error though */
	if (result < 0)
		goto out;

	for (i = 0; i < hid_desc->wDescriptorLength; i++) {

		switch (report[i]) {
		case HID_USAGE_PAGE:
			switch (report[i + 1]) {
			case HID_USAGE_PAGE_DIGITIZER:
				usage = WCM_DIGITIZER;
				i++;
				break;

			case HID_USAGE_PAGE_DESKTOP:
				usage = WCM_DESKTOP;
				i++;
				break;
			}
			break;

		case HID_USAGE:
			switch (report[i + 1]) {
			case HID_USAGE_X:
				if (usage == WCM_DESKTOP) {
					if (finger) {
						features->device_type = BTN_TOOL_FINGER;
						if (features->type == TABLETPC2FG) {
							/* need to reset back */
							features->pktlen = WACOM_PKGLEN_TPC2FG;
							features->device_type = BTN_TOOL_DOUBLETAP;
						}
						if (features->type == BAMBOO_PT) {
							/* need to reset back */
							features->pktlen = WACOM_PKGLEN_BBTOUCH;
							features->device_type = BTN_TOOL_DOUBLETAP;
							features->x_phy =
								get_unaligned_le16(&report[i + 5]);
							features->x_max =
								get_unaligned_le16(&report[i + 8]);
							i += 15;
						} else {
							features->x_max =
								get_unaligned_le16(&report[i + 3]);
							features->x_phy =
								get_unaligned_le16(&report[i + 6]);
							features->unit = report[i + 9];
							features->unitExpo = report[i + 11];
							i += 12;
						}
					} else if (pen) {
						/* penabled only accepts exact bytes of data */
						if (features->type == TABLETPC2FG)
							features->pktlen = WACOM_PKGLEN_GRAPHIRE;
						features->device_type = BTN_TOOL_PEN;
						features->x_max =
							get_unaligned_le16(&report[i + 3]);
						i += 4;
					}
				}
				break;

			case HID_USAGE_Y:
				if (usage == WCM_DESKTOP) {
					if (finger) {
						features->device_type = BTN_TOOL_FINGER;
						if (features->type == TABLETPC2FG) {
							/* need to reset back */
							features->pktlen = WACOM_PKGLEN_TPC2FG;
							features->device_type = BTN_TOOL_DOUBLETAP;
							features->y_max =
								get_unaligned_le16(&report[i + 3]);
							features->y_phy =
								get_unaligned_le16(&report[i + 6]);
							i += 7;
						} else if (features->type == BAMBOO_PT) {
							/* need to reset back */
							features->pktlen = WACOM_PKGLEN_BBTOUCH;
							features->device_type = BTN_TOOL_DOUBLETAP;
							features->y_phy =
								get_unaligned_le16(&report[i + 3]);
							features->y_max =
								get_unaligned_le16(&report[i + 6]);
							i += 12;
						} else {
							features->y_max =
								features->x_max;
							features->y_phy =
								get_unaligned_le16(&report[i + 3]);
							i += 4;
						}
					} else if (pen) {
						/* penabled only accepts exact bytes of data */
						if (features->type == TABLETPC2FG)
							features->pktlen = WACOM_PKGLEN_GRAPHIRE;
						features->device_type = BTN_TOOL_PEN;
						features->y_max =
							get_unaligned_le16(&report[i + 3]);
						i += 4;
					}
				}
				break;

			case HID_USAGE_FINGER:
				finger = 1;
				i++;
				break;

			/*
			 * Requiring Stylus Usage will ignore boot mouse
			 * X/Y values and some cases of invalid Digitizer X/Y
			 * values commonly reported.
			 */
			case HID_USAGE_STYLUS:
				pen = 1;
				i++;
				break;
			}
			break;

		case HID_COLLECTION_END:
			/* reset UsagePage and Finger */
			finger = usage = 0;
			break;

		case HID_COLLECTION:
			i++;
			switch (report[i]) {
			case HID_COLLECTION_LOGICAL:
				i += wacom_parse_logical_collection(&report[i],
								    features);
				break;
			}
			break;
		}
	}

 out:
	result = 0;
	kfree(report);
	return result;
}

static int wacom_query_tablet_data(struct usb_interface *intf, struct wacom_features *features)
{
	unsigned char *rep_data;
	int limit = 0, report_id = 2;
	int error = -ENOMEM;

	rep_data = kmalloc(4, GFP_KERNEL);
	if (!rep_data)
		return error;

	/* ask to report tablet data if it is MT Tablet PC or
	 * not a Tablet PC */
	if (features->type == TABLETPC2FG) {
		do {
			rep_data[0] = 3;
			rep_data[1] = 4;
			rep_data[2] = 0;
			rep_data[3] = 0;
			report_id = 3;
			error = wacom_set_report(intf, WAC_HID_FEATURE_REPORT,
						 report_id, rep_data, 4, 1);
			if (error >= 0)
				error = wacom_get_report(intf,
						WAC_HID_FEATURE_REPORT,
						report_id, rep_data, 4, 1);
		} while ((error < 0 || rep_data[1] != 4) && limit++ < WAC_MSG_RETRIES);
	} else if (features->type != TABLETPC &&
		   features->device_type == BTN_TOOL_PEN) {
		do {
			rep_data[0] = 2;
			rep_data[1] = 2;
			error = wacom_set_report(intf, WAC_HID_FEATURE_REPORT,
						 report_id, rep_data, 2, 1);
			if (error >= 0)
				error = wacom_get_report(intf,
						WAC_HID_FEATURE_REPORT,
						report_id, rep_data, 2, 1);
		} while ((error < 0 || rep_data[1] != 2) && limit++ < WAC_MSG_RETRIES);
	}

	kfree(rep_data);

	return error < 0 ? error : 0;
}

static int wacom_retrieve_hid_descriptor(struct usb_interface *intf,
		struct wacom_features *features)
{
	int error = 0;
	struct usb_host_interface *interface = intf->cur_altsetting;
	struct hid_descriptor *hid_desc;

	/* default features */
	features->device_type = BTN_TOOL_PEN;
	features->x_fuzz = 4;
	features->y_fuzz = 4;
	features->pressure_fuzz = 0;
	features->distance_fuzz = 0;

	/* only Tablet PCs and Bamboo P&T need to retrieve the info */
	if ((features->type != TABLETPC) && (features->type != TABLETPC2FG) &&
	    (features->type != BAMBOO_PT))
		goto out;

	if (usb_get_extra_descriptor(interface, HID_DEVICET_HID, &hid_desc)) {
		if (usb_get_extra_descriptor(&interface->endpoint[0],
				HID_DEVICET_REPORT, &hid_desc)) {
			printk("wacom: can not retrieve extra class descriptor\n");
			error = 1;
			goto out;
		}
	}
	error = wacom_parse_hid(intf, hid_desc, features);
	if (error)
		goto out;

 out:
	return error;
}

struct wacom_usbdev_data {
	struct list_head list;
	struct kref kref;
	struct usb_device *dev;
	struct wacom_shared shared;
};

static LIST_HEAD(wacom_udev_list);
static DEFINE_MUTEX(wacom_udev_list_lock);

static struct wacom_usbdev_data *wacom_get_usbdev_data(struct usb_device *dev)
{
	struct wacom_usbdev_data *data;

	list_for_each_entry(data, &wacom_udev_list, list) {
		if (data->dev == dev) {
			kref_get(&data->kref);
			return data;
		}
	}

	return NULL;
}

static int wacom_add_shared_data(struct wacom_wac *wacom,
				 struct usb_device *dev)
{
	struct wacom_usbdev_data *data;
	int retval = 0;

	mutex_lock(&wacom_udev_list_lock);

	data = wacom_get_usbdev_data(dev);
	if (!data) {
		data = kzalloc(sizeof(struct wacom_usbdev_data), GFP_KERNEL);
		if (!data) {
			retval = -ENOMEM;
			goto out;
		}

		kref_init(&data->kref);
		data->dev = dev;
		list_add_tail(&data->list, &wacom_udev_list);
	}

	wacom->shared = &data->shared;

out:
	mutex_unlock(&wacom_udev_list_lock);
	return retval;
}

static void wacom_release_shared_data(struct kref *kref)
{
	struct wacom_usbdev_data *data =
		container_of(kref, struct wacom_usbdev_data, kref);

	mutex_lock(&wacom_udev_list_lock);
	list_del(&data->list);
	mutex_unlock(&wacom_udev_list_lock);

	kfree(data);
}

static void wacom_remove_shared_data(struct wacom_wac *wacom)
{
	struct wacom_usbdev_data *data;

	if (wacom->shared) {
		data = container_of(wacom->shared, struct wacom_usbdev_data, shared);
		kref_put(&data->kref, wacom_release_shared_data);
		wacom->shared = NULL;
	}
}

static int wacom_led_control(struct wacom *wacom)
{
	unsigned char *buf;
	int retval, led = 0;

	buf = kzalloc(9, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (wacom->wacom_wac.features.type == WACOM_21UX2 ||
	    wacom->wacom_wac.features.type == WACOM_24HD)
		led = (wacom->led.select[1] << 4) | 0x40;

	led |=  wacom->led.select[0] | 0x4;

	buf[0] = WAC_CMD_LED_CONTROL;
	buf[1] = led;
	buf[2] = wacom->led.llv;
	buf[3] = wacom->led.hlv;
	buf[4] = wacom->led.img_lum;

	retval = wacom_set_report(wacom->intf, 0x03, WAC_CMD_LED_CONTROL,
				  buf, 9, WAC_CMD_RETRIES);
	kfree(buf);

	return retval;
}

static int wacom_led_putimage(struct wacom *wacom, int button_id, const void *img)
{
	unsigned char *buf;
	int i, retval;

	buf = kzalloc(259, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Send 'start' command */
	buf[0] = WAC_CMD_ICON_START;
	buf[1] = 1;
	retval = wacom_set_report(wacom->intf, 0x03, WAC_CMD_ICON_START,
				  buf, 2, WAC_CMD_RETRIES);
	if (retval < 0)
		goto out;

	buf[0] = WAC_CMD_ICON_XFER;
	buf[1] = button_id & 0x07;
	for (i = 0; i < 4; i++) {
		buf[2] = i;
		memcpy(buf + 3, img + i * 256, 256);

		retval = wacom_set_report(wacom->intf, 0x03, WAC_CMD_ICON_XFER,
					  buf, 259, WAC_CMD_RETRIES);
		if (retval < 0)
			break;
	}

	/* Send 'stop' */
	buf[0] = WAC_CMD_ICON_START;
	buf[1] = 0;
	wacom_set_report(wacom->intf, 0x03, WAC_CMD_ICON_START,
			 buf, 2, WAC_CMD_RETRIES);

out:
	kfree(buf);
	return retval;
}

static ssize_t wacom_led_select_store(struct device *dev, int set_id,
				      const char *buf, size_t count)
{
	struct wacom *wacom = dev_get_drvdata(dev);
	unsigned int id;
	int err;

	err = kstrtouint(buf, 10, &id);
	if (err)
		return err;

	mutex_lock(&wacom->lock);

	wacom->led.select[set_id] = id & 0x3;
	err = wacom_led_control(wacom);

	mutex_unlock(&wacom->lock);

	return err < 0 ? err : count;
}

#define DEVICE_LED_SELECT_ATTR(SET_ID)					\
static ssize_t wacom_led##SET_ID##_select_store(struct device *dev,	\
	struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	return wacom_led_select_store(dev, SET_ID, buf, count);		\
}									\
static ssize_t wacom_led##SET_ID##_select_show(struct device *dev,	\
	struct device_attribute *attr, char *buf)			\
{									\
	struct wacom *wacom = dev_get_drvdata(dev);			\
	return snprintf(buf, 2, "%d\n", wacom->led.select[SET_ID]);	\
}									\
static DEVICE_ATTR(status_led##SET_ID##_select, S_IWUSR | S_IRUSR,	\
		    wacom_led##SET_ID##_select_show,			\
		    wacom_led##SET_ID##_select_store)

DEVICE_LED_SELECT_ATTR(0);
DEVICE_LED_SELECT_ATTR(1);

static ssize_t wacom_luminance_store(struct wacom *wacom, u8 *dest,
				     const char *buf, size_t count)
{
	unsigned int value;
	int err;

	err = kstrtouint(buf, 10, &value);
	if (err)
		return err;

	mutex_lock(&wacom->lock);

	*dest = value & 0x7f;
	err = wacom_led_control(wacom);

	mutex_unlock(&wacom->lock);

	return err < 0 ? err : count;
}

#define DEVICE_LUMINANCE_ATTR(name, field)				\
static ssize_t wacom_##name##_luminance_store(struct device *dev,	\
	struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	struct wacom *wacom = dev_get_drvdata(dev);			\
									\
	return wacom_luminance_store(wacom, &wacom->led.field,		\
				     buf, count);			\
}									\
static DEVICE_ATTR(name##_luminance, S_IWUSR,				\
		   NULL, wacom_##name##_luminance_store)

DEVICE_LUMINANCE_ATTR(status0, llv);
DEVICE_LUMINANCE_ATTR(status1, hlv);
DEVICE_LUMINANCE_ATTR(buttons, img_lum);

static ssize_t wacom_button_image_store(struct device *dev, int button_id,
					const char *buf, size_t count)
{
	struct wacom *wacom = dev_get_drvdata(dev);
	int err;

	if (count != 1024)
		return -EINVAL;

	mutex_lock(&wacom->lock);

	err = wacom_led_putimage(wacom, button_id, buf);

	mutex_unlock(&wacom->lock);

	return err < 0 ? err : count;
}

#define DEVICE_BTNIMG_ATTR(BUTTON_ID)					\
static ssize_t wacom_btnimg##BUTTON_ID##_store(struct device *dev,	\
	struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	return wacom_button_image_store(dev, BUTTON_ID, buf, count);	\
}									\
static DEVICE_ATTR(button##BUTTON_ID##_rawimg, S_IWUSR,			\
		   NULL, wacom_btnimg##BUTTON_ID##_store)

DEVICE_BTNIMG_ATTR(0);
DEVICE_BTNIMG_ATTR(1);
DEVICE_BTNIMG_ATTR(2);
DEVICE_BTNIMG_ATTR(3);
DEVICE_BTNIMG_ATTR(4);
DEVICE_BTNIMG_ATTR(5);
DEVICE_BTNIMG_ATTR(6);
DEVICE_BTNIMG_ATTR(7);

static struct attribute *cintiq_led_attrs[] = {
	&dev_attr_status_led0_select.attr,
	&dev_attr_status_led1_select.attr,
	NULL
};

static struct attribute_group cintiq_led_attr_group = {
	.name = "wacom_led",
	.attrs = cintiq_led_attrs,
};

static struct attribute *intuos4_led_attrs[] = {
	&dev_attr_status0_luminance.attr,
	&dev_attr_status1_luminance.attr,
	&dev_attr_status_led0_select.attr,
	&dev_attr_buttons_luminance.attr,
	&dev_attr_button0_rawimg.attr,
	&dev_attr_button1_rawimg.attr,
	&dev_attr_button2_rawimg.attr,
	&dev_attr_button3_rawimg.attr,
	&dev_attr_button4_rawimg.attr,
	&dev_attr_button5_rawimg.attr,
	&dev_attr_button6_rawimg.attr,
	&dev_attr_button7_rawimg.attr,
	NULL
};

static struct attribute_group intuos4_led_attr_group = {
	.name = "wacom_led",
	.attrs = intuos4_led_attrs,
};

static int wacom_initialize_leds(struct wacom *wacom)
{
	int error;

	/* Initialize default values */
	switch (wacom->wacom_wac.features.type) {
	case INTUOS4:
	case INTUOS4L:
		wacom->led.select[0] = 0;
		wacom->led.select[1] = 0;
		wacom->led.llv = 10;
		wacom->led.hlv = 20;
		wacom->led.img_lum = 10;
		error = sysfs_create_group(&wacom->intf->dev.kobj,
					   &intuos4_led_attr_group);
		break;

	case WACOM_24HD:
	case WACOM_21UX2:
		wacom->led.select[0] = 0;
		wacom->led.select[1] = 0;
		wacom->led.llv = 0;
		wacom->led.hlv = 0;
		wacom->led.img_lum = 0;

		error = sysfs_create_group(&wacom->intf->dev.kobj,
					   &cintiq_led_attr_group);
		break;

	default:
		return 0;
	}

	if (error) {
		dev_err(&wacom->intf->dev,
			"cannot create sysfs group err: %d\n", error);
		return error;
	}
	wacom_led_control(wacom);

	return 0;
}

static void wacom_destroy_leds(struct wacom *wacom)
{
	switch (wacom->wacom_wac.features.type) {
	case INTUOS4:
	case INTUOS4L:
		sysfs_remove_group(&wacom->intf->dev.kobj,
				   &intuos4_led_attr_group);
		break;

	case WACOM_24HD:
	case WACOM_21UX2:
		sysfs_remove_group(&wacom->intf->dev.kobj,
				   &cintiq_led_attr_group);
		break;
	}
}

static int wacom_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct wacom *wacom;
	struct wacom_wac *wacom_wac;
	struct wacom_features *features;
	struct input_dev *input_dev;
	int error;

	if (!id->driver_info)
		return -EINVAL;

	wacom = kzalloc(sizeof(struct wacom), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!wacom || !input_dev) {
		error = -ENOMEM;
		goto fail1;
	}

	wacom_wac = &wacom->wacom_wac;
	wacom_wac->features = *((struct wacom_features *)id->driver_info);
	features = &wacom_wac->features;
	if (features->pktlen > WACOM_PKGLEN_MAX) {
		error = -EINVAL;
		goto fail1;
	}

	wacom_wac->data = usb_alloc_coherent(dev, WACOM_PKGLEN_MAX,
					     GFP_KERNEL, &wacom->data_dma);
	if (!wacom_wac->data) {
		error = -ENOMEM;
		goto fail1;
	}

	wacom->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!wacom->irq) {
		error = -ENOMEM;
		goto fail2;
	}

	wacom->usbdev = dev;
	wacom->intf = intf;
	mutex_init(&wacom->lock);
	usb_make_path(dev, wacom->phys, sizeof(wacom->phys));
	strlcat(wacom->phys, "/input0", sizeof(wacom->phys));

	wacom_wac->input = input_dev;

	endpoint = &intf->cur_altsetting->endpoint[0].desc;

	/* Retrieve the physical and logical size for OEM devices */
	error = wacom_retrieve_hid_descriptor(intf, features);
	if (error)
		goto fail3;

	wacom_setup_device_quirks(features);

	strlcpy(wacom_wac->name, features->name, sizeof(wacom_wac->name));

	if (features->quirks & WACOM_QUIRK_MULTI_INPUT) {
		/* Append the device type to the name */
		strlcat(wacom_wac->name,
			features->device_type == BTN_TOOL_PEN ?
				" Pen" : " Finger",
			sizeof(wacom_wac->name));

		error = wacom_add_shared_data(wacom_wac, dev);
		if (error)
			goto fail3;
	}

	input_dev->name = wacom_wac->name;
	input_dev->dev.parent = &intf->dev;
	input_dev->open = wacom_open;
	input_dev->close = wacom_close;
	usb_to_input_id(dev, &input_dev->id);
	input_set_drvdata(input_dev, wacom);

	wacom_setup_input_capabilities(input_dev, wacom_wac);

	usb_fill_int_urb(wacom->irq, dev,
			 usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			 wacom_wac->data, features->pktlen,
			 wacom_sys_irq, wacom, endpoint->bInterval);
	wacom->irq->transfer_dma = wacom->data_dma;
	wacom->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = wacom_initialize_leds(wacom);
	if (error)
		goto fail4;

	error = input_register_device(input_dev);
	if (error)
		goto fail5;

	/* Note that if query fails it is not a hard failure */
	wacom_query_tablet_data(intf, features);

	usb_set_intfdata(intf, wacom);
	return 0;

 fail5: wacom_destroy_leds(wacom);
 fail4:	wacom_remove_shared_data(wacom_wac);
 fail3:	usb_free_urb(wacom->irq);
 fail2:	usb_free_coherent(dev, WACOM_PKGLEN_MAX, wacom_wac->data, wacom->data_dma);
 fail1:	input_free_device(input_dev);
	kfree(wacom);
	return error;
}

static void wacom_disconnect(struct usb_interface *intf)
{
	struct wacom *wacom = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	usb_kill_urb(wacom->irq);
	input_unregister_device(wacom->wacom_wac.input);
	wacom_destroy_leds(wacom);
	usb_free_urb(wacom->irq);
	usb_free_coherent(interface_to_usbdev(intf), WACOM_PKGLEN_MAX,
			wacom->wacom_wac.data, wacom->data_dma);
	wacom_remove_shared_data(&wacom->wacom_wac);
	kfree(wacom);
}

static int wacom_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct wacom *wacom = usb_get_intfdata(intf);

	mutex_lock(&wacom->lock);
	usb_kill_urb(wacom->irq);
	mutex_unlock(&wacom->lock);

	return 0;
}

static int wacom_resume(struct usb_interface *intf)
{
	struct wacom *wacom = usb_get_intfdata(intf);
	struct wacom_features *features = &wacom->wacom_wac.features;
	int rv = 0;

	mutex_lock(&wacom->lock);

	/* switch to wacom mode first */
	wacom_query_tablet_data(intf, features);
	wacom_led_control(wacom);

	if (wacom->open && usb_submit_urb(wacom->irq, GFP_NOIO) < 0)
		rv = -EIO;

	mutex_unlock(&wacom->lock);

	return rv;
}

static int wacom_reset_resume(struct usb_interface *intf)
{
	return wacom_resume(intf);
}

static struct usb_driver wacom_driver = {
	.name =		"wacom",
	.id_table =	wacom_ids,
	.probe =	wacom_probe,
	.disconnect =	wacom_disconnect,
	.suspend =	wacom_suspend,
	.resume =	wacom_resume,
	.reset_resume =	wacom_reset_resume,
	.supports_autosuspend = 1,
};

module_usb_driver(wacom_driver);
