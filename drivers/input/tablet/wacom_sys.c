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
#include <linux/hid.h>

#define HID_HDESC_USAGE_UNDEFINED	0x00
#define HID_HDESC_USAGE_PAGE		0x05
#define HID_HDESC_USAGE			0x09
#define HID_HDESC_COLLECTION		0xa1
#define HID_HDESC_COLLECTION_LOGICAL	0x02
#define HID_HDESC_COLLECTION_END	0xc0

#define WAC_MSG_RETRIES		5

#define WAC_CMD_LED_CONTROL	0x20
#define WAC_CMD_ICON_START	0x21
#define WAC_CMD_ICON_XFER	0x23
#define WAC_CMD_RETRIES		10

static int wacom_get_report(struct hid_device *hdev, u8 type, u8 id,
			    void *buf, size_t size, unsigned int retries)
{
	int retval;

	do {
		retval = hid_hw_raw_request(hdev, id, buf, size, type,
				HID_REQ_GET_REPORT);
	} while ((retval == -ETIMEDOUT || retval == -EPIPE) && --retries);

	return retval;
}

static int wacom_set_report(struct hid_device *hdev, u8 type, u8 id,
			    void *buf, size_t size, unsigned int retries)
{
	int retval;

	do {
		retval = hid_hw_raw_request(hdev, id, buf, size, type,
				HID_REQ_SET_REPORT);
	} while ((retval == -ETIMEDOUT || retval == -EPIPE) && --retries);

	return retval;
}

static int wacom_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *raw_data, int size)
{
	struct wacom *wacom = hid_get_drvdata(hdev);

	if (size > WACOM_PKGLEN_MAX)
		return 1;

	memcpy(wacom->wacom_wac.data, raw_data, size);

	wacom_wac_irq(&wacom->wacom_wac, size);

	return 0;
}

static int wacom_open(struct input_dev *dev)
{
	struct wacom *wacom = input_get_drvdata(dev);
	int retval;

	mutex_lock(&wacom->lock);
	retval = hid_hw_open(wacom->hdev);
	mutex_unlock(&wacom->lock);

	return retval;
}

static void wacom_close(struct input_dev *dev)
{
	struct wacom *wacom = input_get_drvdata(dev);

	mutex_lock(&wacom->lock);
	hid_hw_close(wacom->hdev);
	mutex_unlock(&wacom->lock);
}

/*
 * Calculate the resolution of the X or Y axis, given appropriate HID data.
 * This function is little more than hidinput_calc_abs_res stripped down.
 */
static int wacom_calc_hid_res(int logical_extents, int physical_extents,
                              unsigned char unit, unsigned char exponent)
{
	int prev, unit_exponent;

	/* Check if the extents are sane */
	if (logical_extents <= 0 || physical_extents <= 0)
		return 0;

	/* Get signed value of nybble-sized twos-compliment exponent */
	unit_exponent = exponent;
	if (unit_exponent > 7)
		unit_exponent -= 16;

	/* Convert physical_extents to millimeters */
	if (unit == 0x11) {		/* If centimeters */
		unit_exponent += 1;
	} else if (unit == 0x13) {	/* If inches */
		prev = physical_extents;
		physical_extents *= 254;
		if (physical_extents < prev)
			return 0;
		unit_exponent -= 1;
	} else {
		return 0;
	}

	/* Apply negative unit exponent */
	for (; unit_exponent < 0; unit_exponent++) {
		prev = logical_extents;
		logical_extents *= 10;
		if (logical_extents < prev)
			return 0;
	}
	/* Apply positive unit exponent */
	for (; unit_exponent > 0; unit_exponent--) {
		prev = physical_extents;
		physical_extents *= 10;
		if (physical_extents < prev)
			return 0;
	}

	/* Calculate resolution */
	return logical_extents / physical_extents;
}

static int wacom_parse_logical_collection(unsigned char *report,
					  struct wacom_features *features)
{
	int length = 0;

	if (features->type == BAMBOO_PT) {

		/* Logical collection is only used by 3rd gen Bamboo Touch */
		features->device_type = BTN_TOOL_FINGER;

		features->x_max = features->y_max =
			get_unaligned_le16(&report[10]);

		length = 11;
	}
	return length;
}

static void wacom_retrieve_report_data(struct hid_device *hdev,
				       struct wacom_features *features)
{
	int result = 0;
	unsigned char *rep_data;

	rep_data = kmalloc(2, GFP_KERNEL);
	if (rep_data) {

		rep_data[0] = 12;
		result = wacom_get_report(hdev, HID_FEATURE_REPORT,
					  rep_data[0], rep_data, 2,
					  WAC_MSG_RETRIES);

		if (result >= 0 && rep_data[1] > 2)
			features->touch_max = rep_data[1];

		kfree(rep_data);
	}
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
 *
 * Intuos5 touch interface does not contain useful data. We deal with
 * this after returning from this function.
 */
static int wacom_parse_hid(struct hid_device *hdev,
			   struct wacom_features *features)
{
	/* result has to be defined as int for some devices */
	int result = 0, touch_max = 0;
	int i = 0, page = 0, finger = 0, pen = 0;
	unsigned char *report = hdev->rdesc;

	for (i = 0; i < hdev->rsize; i++) {

		switch (report[i]) {
		case HID_HDESC_USAGE_PAGE:
			page = report[i + 1];
			i++;
			break;

		case HID_HDESC_USAGE:
			switch (page << 16 | report[i + 1]) {
			case HID_GD_X:
				if (finger) {
					features->device_type = BTN_TOOL_FINGER;
					/* touch device at least supports one touch point */
					touch_max = 1;

					switch (features->type) {
					case BAMBOO_PT:
						features->x_phy =
							get_unaligned_le16(&report[i + 5]);
						features->x_max =
							get_unaligned_le16(&report[i + 8]);
						i += 15;
						break;

					case WACOM_24HDT:
						features->x_max =
							get_unaligned_le16(&report[i + 3]);
						features->x_phy =
							get_unaligned_le16(&report[i + 8]);
						features->unit = report[i - 1];
						features->unitExpo = report[i - 3];
						i += 12;
						break;

					case MTTPC_B:
						features->x_max =
							get_unaligned_le16(&report[i + 3]);
						features->x_phy =
							get_unaligned_le16(&report[i + 6]);
						features->unit = report[i - 5];
						features->unitExpo = report[i - 3];
						i += 9;
						break;

					default:
						features->x_max =
							get_unaligned_le16(&report[i + 3]);
						features->x_phy =
							get_unaligned_le16(&report[i + 6]);
						features->unit = report[i + 9];
						features->unitExpo = report[i + 11];
						i += 12;
						break;
					}
				} else if (pen) {
					/* penabled only accepts exact bytes of data */
					features->device_type = BTN_TOOL_PEN;
					features->x_max =
						get_unaligned_le16(&report[i + 3]);
					i += 4;
				}
				break;

			case HID_GD_Y:
				if (finger) {
					switch (features->type) {
					case TABLETPC2FG:
					case MTSCREEN:
					case MTTPC:
						features->y_max =
							get_unaligned_le16(&report[i + 3]);
						features->y_phy =
							get_unaligned_le16(&report[i + 6]);
						i += 7;
						break;

					case WACOM_24HDT:
						features->y_max =
							get_unaligned_le16(&report[i + 3]);
						features->y_phy =
							get_unaligned_le16(&report[i - 2]);
						i += 7;
						break;

					case BAMBOO_PT:
						features->y_phy =
							get_unaligned_le16(&report[i + 3]);
						features->y_max =
							get_unaligned_le16(&report[i + 6]);
						i += 12;
						break;

					case MTTPC_B:
						features->y_max =
							get_unaligned_le16(&report[i + 3]);
						features->y_phy =
							get_unaligned_le16(&report[i + 6]);
						i += 9;
						break;

					default:
						features->y_max =
							features->x_max;
						features->y_phy =
							get_unaligned_le16(&report[i + 3]);
						i += 4;
						break;
					}
				} else if (pen) {
					features->y_max =
						get_unaligned_le16(&report[i + 3]);
					i += 4;
				}
				break;

			case HID_DG_FINGER:
				finger = 1;
				i++;
				break;

			/*
			 * Requiring Stylus Usage will ignore boot mouse
			 * X/Y values and some cases of invalid Digitizer X/Y
			 * values commonly reported.
			 */
			case HID_DG_STYLUS:
				pen = 1;
				i++;
				break;

			case HID_DG_CONTACTMAX:
				/* leave touch_max as is if predefined */
				if (!features->touch_max)
					wacom_retrieve_report_data(hdev, features);
				i++;
				break;

			case HID_DG_TIPPRESSURE:
				if (pen) {
					features->pressure_max =
						get_unaligned_le16(&report[i + 3]);
					i += 4;
				}
				break;
			}
			break;

		case HID_HDESC_COLLECTION_END:
			/* reset UsagePage and Finger */
			finger = page = 0;
			break;

		case HID_HDESC_COLLECTION:
			i++;
			switch (report[i]) {
			case HID_HDESC_COLLECTION_LOGICAL:
				i += wacom_parse_logical_collection(&report[i],
								    features);
				break;
			}
			break;
		}
	}

	if (!features->touch_max && touch_max)
		features->touch_max = touch_max;
	result = 0;
	return result;
}

static int wacom_set_device_mode(struct hid_device *hdev, int report_id,
		int length, int mode)
{
	unsigned char *rep_data;
	int error = -ENOMEM, limit = 0;

	rep_data = kzalloc(length, GFP_KERNEL);
	if (!rep_data)
		return error;

	do {
		rep_data[0] = report_id;
		rep_data[1] = mode;

		error = wacom_set_report(hdev, HID_FEATURE_REPORT,
		                         report_id, rep_data, length, 1);
		if (error >= 0)
			error = wacom_get_report(hdev, HID_FEATURE_REPORT,
			                         report_id, rep_data, length, 1);
	} while ((error < 0 || rep_data[1] != mode) && limit++ < WAC_MSG_RETRIES);

	kfree(rep_data);

	return error < 0 ? error : 0;
}

/*
 * Switch the tablet into its most-capable mode. Wacom tablets are
 * typically configured to power-up in a mode which sends mouse-like
 * reports to the OS. To get absolute position, pressure data, etc.
 * from the tablet, it is necessary to switch the tablet out of this
 * mode and into one which sends the full range of tablet data.
 */
static int wacom_query_tablet_data(struct hid_device *hdev,
		struct wacom_features *features)
{
	if (features->device_type == BTN_TOOL_FINGER) {
		if (features->type > TABLETPC) {
			/* MT Tablet PC touch */
			return wacom_set_device_mode(hdev, 3, 4, 4);
		}
		else if (features->type == WACOM_24HDT || features->type == CINTIQ_HYBRID) {
			return wacom_set_device_mode(hdev, 18, 3, 2);
		}
	} else if (features->device_type == BTN_TOOL_PEN) {
		if (features->type <= BAMBOO_PT && features->type != WIRELESS) {
			return wacom_set_device_mode(hdev, 2, 2, 2);
		}
	}

	return 0;
}

static int wacom_retrieve_hid_descriptor(struct hid_device *hdev,
					 struct wacom_features *features)
{
	int error = 0;
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct usb_interface *intf = wacom->intf;

	/* default features */
	features->device_type = BTN_TOOL_PEN;
	features->x_fuzz = 4;
	features->y_fuzz = 4;
	features->pressure_fuzz = 0;
	features->distance_fuzz = 0;

	/*
	 * The wireless device HID is basic and layout conflicts with
	 * other tablets (monitor and touch interface can look like pen).
	 * Skip the query for this type and modify defaults based on
	 * interface number.
	 */
	if (features->type == WIRELESS) {
		if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
			features->device_type = 0;
		} else if (intf->cur_altsetting->desc.bInterfaceNumber == 2) {
			features->device_type = BTN_TOOL_FINGER;
			features->pktlen = WACOM_PKGLEN_BBTOUCH3;
		}
	}

	/* only devices that support touch need to retrieve the info */
	if (features->type < BAMBOO_PT) {
		goto out;
	}

	error = wacom_parse_hid(hdev, features);

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

static struct usb_device *wacom_get_sibling(struct usb_device *dev, int vendor, int product)
{
	int port1;
	struct usb_device *sibling;

	if (vendor == 0 && product == 0)
		return dev;

	if (dev->parent == NULL)
		return NULL;

	usb_hub_for_each_child(dev->parent, port1, sibling) {
		struct usb_device_descriptor *d;
		if (sibling == NULL)
			continue;

		d = &sibling->descriptor;
		if (d->idVendor == vendor && d->idProduct == product)
			return sibling;
	}

	return NULL;
}

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
	int retval;

	buf = kzalloc(9, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (wacom->wacom_wac.features.type >= INTUOS5S &&
	    wacom->wacom_wac.features.type <= INTUOSPL) {
		/*
		 * Touch Ring and crop mark LED luminance may take on
		 * one of four values:
		 *    0 = Low; 1 = Medium; 2 = High; 3 = Off
		 */
		int ring_led = wacom->led.select[0] & 0x03;
		int ring_lum = (((wacom->led.llv & 0x60) >> 5) - 1) & 0x03;
		int crop_lum = 0;

		buf[0] = WAC_CMD_LED_CONTROL;
		buf[1] = (crop_lum << 4) | (ring_lum << 2) | (ring_led);
	}
	else {
		int led = wacom->led.select[0] | 0x4;

		if (wacom->wacom_wac.features.type == WACOM_21UX2 ||
		    wacom->wacom_wac.features.type == WACOM_24HD)
			led |= (wacom->led.select[1] << 4) | 0x40;

		buf[0] = WAC_CMD_LED_CONTROL;
		buf[1] = led;
		buf[2] = wacom->led.llv;
		buf[3] = wacom->led.hlv;
		buf[4] = wacom->led.img_lum;
	}

	retval = wacom_set_report(wacom->hdev, HID_FEATURE_REPORT,
				  WAC_CMD_LED_CONTROL, buf, 9, WAC_CMD_RETRIES);
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
	retval = wacom_set_report(wacom->hdev, HID_FEATURE_REPORT,
				  WAC_CMD_ICON_START, buf, 2, WAC_CMD_RETRIES);
	if (retval < 0)
		goto out;

	buf[0] = WAC_CMD_ICON_XFER;
	buf[1] = button_id & 0x07;
	for (i = 0; i < 4; i++) {
		buf[2] = i;
		memcpy(buf + 3, img + i * 256, 256);

		retval = wacom_set_report(wacom->hdev, HID_FEATURE_REPORT,
					  WAC_CMD_ICON_XFER,
					  buf, 259, WAC_CMD_RETRIES);
		if (retval < 0)
			break;
	}

	/* Send 'stop' */
	buf[0] = WAC_CMD_ICON_START;
	buf[1] = 0;
	wacom_set_report(wacom->hdev, HID_FEATURE_REPORT, WAC_CMD_ICON_START,
			 buf, 2, WAC_CMD_RETRIES);

out:
	kfree(buf);
	return retval;
}

static ssize_t wacom_led_select_store(struct device *dev, int set_id,
				      const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct wacom *wacom = hid_get_drvdata(hdev);
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
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);\
	struct wacom *wacom = hid_get_drvdata(hdev);			\
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
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);\
	struct wacom *wacom = hid_get_drvdata(hdev);			\
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
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct wacom *wacom = hid_get_drvdata(hdev);
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

static struct attribute *intuos5_led_attrs[] = {
	&dev_attr_status0_luminance.attr,
	&dev_attr_status_led0_select.attr,
	NULL
};

static struct attribute_group intuos5_led_attr_group = {
	.name = "wacom_led",
	.attrs = intuos5_led_attrs,
};

static int wacom_initialize_leds(struct wacom *wacom)
{
	int error;

	/* Initialize default values */
	switch (wacom->wacom_wac.features.type) {
	case INTUOS4S:
	case INTUOS4:
	case INTUOS4L:
		wacom->led.select[0] = 0;
		wacom->led.select[1] = 0;
		wacom->led.llv = 10;
		wacom->led.hlv = 20;
		wacom->led.img_lum = 10;
		error = sysfs_create_group(&wacom->hdev->dev.kobj,
					   &intuos4_led_attr_group);
		break;

	case WACOM_24HD:
	case WACOM_21UX2:
		wacom->led.select[0] = 0;
		wacom->led.select[1] = 0;
		wacom->led.llv = 0;
		wacom->led.hlv = 0;
		wacom->led.img_lum = 0;

		error = sysfs_create_group(&wacom->hdev->dev.kobj,
					   &cintiq_led_attr_group);
		break;

	case INTUOS5S:
	case INTUOS5:
	case INTUOS5L:
	case INTUOSPS:
	case INTUOSPM:
	case INTUOSPL:
		if (wacom->wacom_wac.features.device_type == BTN_TOOL_PEN) {
			wacom->led.select[0] = 0;
			wacom->led.select[1] = 0;
			wacom->led.llv = 32;
			wacom->led.hlv = 0;
			wacom->led.img_lum = 0;

			error = sysfs_create_group(&wacom->hdev->dev.kobj,
						  &intuos5_led_attr_group);
		} else
			return 0;
		break;

	default:
		return 0;
	}

	if (error) {
		hid_err(wacom->hdev,
			"cannot create sysfs group err: %d\n", error);
		return error;
	}
	wacom_led_control(wacom);

	return 0;
}

static void wacom_destroy_leds(struct wacom *wacom)
{
	switch (wacom->wacom_wac.features.type) {
	case INTUOS4S:
	case INTUOS4:
	case INTUOS4L:
		sysfs_remove_group(&wacom->hdev->dev.kobj,
				   &intuos4_led_attr_group);
		break;

	case WACOM_24HD:
	case WACOM_21UX2:
		sysfs_remove_group(&wacom->hdev->dev.kobj,
				   &cintiq_led_attr_group);
		break;

	case INTUOS5S:
	case INTUOS5:
	case INTUOS5L:
	case INTUOSPS:
	case INTUOSPM:
	case INTUOSPL:
		if (wacom->wacom_wac.features.device_type == BTN_TOOL_PEN)
			sysfs_remove_group(&wacom->hdev->dev.kobj,
					   &intuos5_led_attr_group);
		break;
	}
}

static enum power_supply_property wacom_battery_props[] = {
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_CAPACITY
};

static int wacom_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct wacom *wacom = container_of(psy, struct wacom, battery);
	int ret = 0;

	switch (psp) {
		case POWER_SUPPLY_PROP_SCOPE:
			val->intval = POWER_SUPPLY_SCOPE_DEVICE;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval =
				wacom->wacom_wac.battery_capacity * 100 / 31;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

static int wacom_initialize_battery(struct wacom *wacom)
{
	int error = 0;

	if (wacom->wacom_wac.features.quirks & WACOM_QUIRK_MONITOR) {
		wacom->battery.properties = wacom_battery_props;
		wacom->battery.num_properties = ARRAY_SIZE(wacom_battery_props);
		wacom->battery.get_property = wacom_battery_get_property;
		wacom->battery.name = "wacom_battery";
		wacom->battery.type = POWER_SUPPLY_TYPE_BATTERY;
		wacom->battery.use_for_apm = 0;

		error = power_supply_register(&wacom->usbdev->dev,
					      &wacom->battery);

		if (!error)
			power_supply_powers(&wacom->battery,
					    &wacom->usbdev->dev);
	}

	return error;
}

static void wacom_destroy_battery(struct wacom *wacom)
{
	if (wacom->wacom_wac.features.quirks & WACOM_QUIRK_MONITOR &&
	    wacom->battery.dev) {
		power_supply_unregister(&wacom->battery);
		wacom->battery.dev = NULL;
	}
}

static struct input_dev *wacom_allocate_input(struct wacom *wacom)
{
	struct input_dev *input_dev;
	struct hid_device *hdev = wacom->hdev;
	struct wacom_wac *wacom_wac = &(wacom->wacom_wac);

	input_dev = input_allocate_device();
	if (!input_dev)
		return NULL;

	input_dev->name = wacom_wac->name;
	input_dev->phys = hdev->phys;
	input_dev->dev.parent = &hdev->dev;
	input_dev->open = wacom_open;
	input_dev->close = wacom_close;
	input_dev->uniq = hdev->uniq;
	input_dev->id.bustype = hdev->bus;
	input_dev->id.vendor  = hdev->vendor;
	input_dev->id.product = hdev->product;
	input_dev->id.version = hdev->version;
	input_set_drvdata(input_dev, wacom);

	return input_dev;
}

static void wacom_unregister_inputs(struct wacom *wacom)
{
	if (wacom->wacom_wac.input)
		input_unregister_device(wacom->wacom_wac.input);
	if (wacom->wacom_wac.pad_input)
		input_unregister_device(wacom->wacom_wac.pad_input);
	wacom->wacom_wac.input = NULL;
	wacom->wacom_wac.pad_input = NULL;
}

static int wacom_register_inputs(struct wacom *wacom)
{
	struct input_dev *input_dev, *pad_input_dev;
	struct wacom_wac *wacom_wac = &(wacom->wacom_wac);
	int error;

	input_dev = wacom_allocate_input(wacom);
	pad_input_dev = wacom_allocate_input(wacom);
	if (!input_dev || !pad_input_dev) {
		error = -ENOMEM;
		goto fail1;
	}

	wacom_wac->input = input_dev;
	wacom_wac->pad_input = pad_input_dev;
	wacom_wac->pad_input->name = wacom_wac->pad_name;

	error = wacom_setup_input_capabilities(input_dev, wacom_wac);
	if (error)
		goto fail2;

	error = input_register_device(input_dev);
	if (error)
		goto fail2;

	error = wacom_setup_pad_input_capabilities(pad_input_dev, wacom_wac);
	if (error) {
		/* no pad in use on this interface */
		input_free_device(pad_input_dev);
		wacom_wac->pad_input = NULL;
		pad_input_dev = NULL;
	} else {
		error = input_register_device(pad_input_dev);
		if (error)
			goto fail3;
	}

	return 0;

fail3:
	input_unregister_device(input_dev);
	input_dev = NULL;
fail2:
	wacom_wac->input = NULL;
	wacom_wac->pad_input = NULL;
fail1:
	if (input_dev)
		input_free_device(input_dev);
	if (pad_input_dev)
		input_free_device(pad_input_dev);
	return error;
}

static void wacom_wireless_work(struct work_struct *work)
{
	struct wacom *wacom = container_of(work, struct wacom, work);
	struct usb_device *usbdev = wacom->usbdev;
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct hid_device *hdev1, *hdev2;
	struct wacom *wacom1, *wacom2;
	struct wacom_wac *wacom_wac1, *wacom_wac2;
	int error;

	/*
	 * Regardless if this is a disconnect or a new tablet,
	 * remove any existing input and battery devices.
	 */

	wacom_destroy_battery(wacom);

	/* Stylus interface */
	hdev1 = usb_get_intfdata(usbdev->config->interface[1]);
	wacom1 = hid_get_drvdata(hdev1);
	wacom_wac1 = &(wacom1->wacom_wac);
	wacom_unregister_inputs(wacom1);

	/* Touch interface */
	hdev2 = usb_get_intfdata(usbdev->config->interface[2]);
	wacom2 = hid_get_drvdata(hdev2);
	wacom_wac2 = &(wacom2->wacom_wac);
	wacom_unregister_inputs(wacom2);

	if (wacom_wac->pid == 0) {
		dev_info(&wacom->intf->dev, "wireless tablet disconnected\n");
	} else {
		const struct hid_device_id *id = wacom_ids;

		dev_info(&wacom->intf->dev,
			 "wireless tablet connected with PID %x\n",
			 wacom_wac->pid);

		while (id->bus) {
			if (id->vendor == USB_VENDOR_ID_WACOM &&
			    id->product == wacom_wac->pid)
				break;
			id++;
		}

		if (!id->bus) {
			dev_info(&wacom->intf->dev,
				 "ignoring unknown PID.\n");
			return;
		}

		/* Stylus interface */
		wacom_wac1->features =
			*((struct wacom_features *)id->driver_data);
		wacom_wac1->features.device_type = BTN_TOOL_PEN;
		snprintf(wacom_wac1->name, WACOM_NAME_MAX, "%s (WL) Pen",
			 wacom_wac1->features.name);
		snprintf(wacom_wac1->pad_name, WACOM_NAME_MAX, "%s (WL) Pad",
			 wacom_wac1->features.name);
		wacom_wac1->shared->touch_max = wacom_wac1->features.touch_max;
		wacom_wac1->shared->type = wacom_wac1->features.type;
		error = wacom_register_inputs(wacom1);
		if (error)
			goto fail;

		/* Touch interface */
		if (wacom_wac1->features.touch_max ||
		    wacom_wac1->features.type == INTUOSHT) {
			wacom_wac2->features =
				*((struct wacom_features *)id->driver_data);
			wacom_wac2->features.pktlen = WACOM_PKGLEN_BBTOUCH3;
			wacom_wac2->features.device_type = BTN_TOOL_FINGER;
			wacom_wac2->features.x_max = wacom_wac2->features.y_max = 4096;
			if (wacom_wac2->features.touch_max)
				snprintf(wacom_wac2->name, WACOM_NAME_MAX,
					 "%s (WL) Finger",wacom_wac2->features.name);
			else
				snprintf(wacom_wac2->name, WACOM_NAME_MAX,
					 "%s (WL) Pad",wacom_wac2->features.name);
			snprintf(wacom_wac2->pad_name, WACOM_NAME_MAX,
				 "%s (WL) Pad", wacom_wac2->features.name);
			error = wacom_register_inputs(wacom2);
			if (error)
				goto fail;

			if (wacom_wac1->features.type == INTUOSHT &&
			    wacom_wac1->features.touch_max)
				wacom_wac->shared->touch_input = wacom_wac2->input;
		}

		error = wacom_initialize_battery(wacom);
		if (error)
			goto fail;
	}

	return;

fail:
	wacom_unregister_inputs(wacom1);
	wacom_unregister_inputs(wacom2);
	return;
}

/*
 * Not all devices report physical dimensions from HID.
 * Compute the default from hardcoded logical dimension
 * and resolution before driver overwrites them.
 */
static void wacom_set_default_phy(struct wacom_features *features)
{
	if (features->x_resolution) {
		features->x_phy = (features->x_max * 100) /
					features->x_resolution;
		features->y_phy = (features->y_max * 100) /
					features->y_resolution;
	}
}

static void wacom_calculate_res(struct wacom_features *features)
{
	features->x_resolution = wacom_calc_hid_res(features->x_max,
						    features->x_phy,
						    features->unit,
						    features->unitExpo);
	features->y_resolution = wacom_calc_hid_res(features->y_max,
						    features->y_phy,
						    features->unit,
						    features->unitExpo);
}

static int wacom_hid_report_len(struct hid_report *report)
{
	/* equivalent to DIV_ROUND_UP(report->size, 8) + !!(report->id > 0) */
	return ((report->size - 1) >> 3) + 1 + (report->id > 0);
}

static size_t wacom_compute_pktlen(struct hid_device *hdev)
{
	struct hid_report_enum *report_enum;
	struct hid_report *report;
	size_t size = 0;

	report_enum = hdev->report_enum + HID_INPUT_REPORT;

	list_for_each_entry(report, &report_enum->report_list, list) {
		size_t report_size = wacom_hid_report_len(report);
		if (report_size > size)
			size = report_size;
	}

	return size;
}

static int wacom_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *dev = interface_to_usbdev(intf);
	struct wacom *wacom;
	struct wacom_wac *wacom_wac;
	struct wacom_features *features;
	int error;

	if (!id->driver_data)
		return -EINVAL;

	wacom = kzalloc(sizeof(struct wacom), GFP_KERNEL);
	if (!wacom)
		return -ENOMEM;

	hid_set_drvdata(hdev, wacom);
	wacom->hdev = hdev;

	/* ask for the report descriptor to be loaded by HID */
	error = hid_parse(hdev);
	if (error) {
		hid_err(hdev, "parse failed\n");
		goto fail1;
	}

	wacom_wac = &wacom->wacom_wac;
	wacom_wac->features = *((struct wacom_features *)id->driver_data);
	features = &wacom_wac->features;
	features->pktlen = wacom_compute_pktlen(hdev);
	if (features->pktlen > WACOM_PKGLEN_MAX) {
		error = -EINVAL;
		goto fail1;
	}

	if (features->check_for_hid_type && features->hid_type != hdev->type) {
		error = -ENODEV;
		goto fail1;
	}

	wacom->usbdev = dev;
	wacom->intf = intf;
	mutex_init(&wacom->lock);
	INIT_WORK(&wacom->work, wacom_wireless_work);

	/* set the default size in case we do not get them from hid */
	wacom_set_default_phy(features);

	/* Retrieve the physical and logical size for touch devices */
	error = wacom_retrieve_hid_descriptor(hdev, features);
	if (error)
		goto fail1;

	/*
	 * Intuos5 has no useful data about its touch interface in its
	 * HID descriptor. If this is the touch interface (PacketSize
	 * of WACOM_PKGLEN_BBTOUCH3), override the table values.
	 */
	if (features->type >= INTUOS5S && features->type <= INTUOSHT) {
		if (features->pktlen == WACOM_PKGLEN_BBTOUCH3) {
			features->device_type = BTN_TOOL_FINGER;

			features->x_max = 4096;
			features->y_max = 4096;
		} else {
			features->device_type = BTN_TOOL_PEN;
		}
	}

	wacom_setup_device_quirks(features);

	/* set unit to "100th of a mm" for devices not reported by HID */
	if (!features->unit) {
		features->unit = 0x11;
		features->unitExpo = 16 - 3;
	}
	wacom_calculate_res(features);

	strlcpy(wacom_wac->name, features->name, sizeof(wacom_wac->name));
	snprintf(wacom_wac->pad_name, sizeof(wacom_wac->pad_name),
		"%s Pad", features->name);

	if (features->quirks & WACOM_QUIRK_MULTI_INPUT) {
		struct usb_device *other_dev;

		/* Append the device type to the name */
		if (features->device_type != BTN_TOOL_FINGER)
			strlcat(wacom_wac->name, " Pen", WACOM_NAME_MAX);
		else if (features->touch_max)
			strlcat(wacom_wac->name, " Finger", WACOM_NAME_MAX);
		else
			strlcat(wacom_wac->name, " Pad", WACOM_NAME_MAX);

		other_dev = wacom_get_sibling(dev, features->oVid, features->oPid);
		if (other_dev == NULL || wacom_get_usbdev_data(other_dev) == NULL)
			other_dev = dev;
		error = wacom_add_shared_data(wacom_wac, other_dev);
		if (error)
			goto fail1;
	}

	error = wacom_initialize_leds(wacom);
	if (error)
		goto fail2;

	if (!(features->quirks & WACOM_QUIRK_NO_INPUT)) {
		error = wacom_register_inputs(wacom);
		if (error)
			goto fail3;
	}

	/* Note that if query fails it is not a hard failure */
	wacom_query_tablet_data(hdev, features);

	/* Regular HID work starts now */
	error = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (error) {
		hid_err(hdev, "hw start failed\n");
		goto fail4;
	}

	if (features->quirks & WACOM_QUIRK_MONITOR)
		error = hid_hw_open(hdev);

	if (wacom_wac->features.type == INTUOSHT && wacom_wac->features.touch_max) {
		if (wacom_wac->features.device_type == BTN_TOOL_FINGER)
			wacom_wac->shared->touch_input = wacom_wac->input;
	}

	return 0;

 fail4:	wacom_unregister_inputs(wacom);
 fail3:	wacom_destroy_leds(wacom);
 fail2:	wacom_remove_shared_data(wacom_wac);
 fail1:	kfree(wacom);
	hid_set_drvdata(hdev, NULL);
	return error;
}

static void wacom_remove(struct hid_device *hdev)
{
	struct wacom *wacom = hid_get_drvdata(hdev);

	hid_hw_stop(hdev);

	cancel_work_sync(&wacom->work);
	wacom_unregister_inputs(wacom);
	wacom_destroy_battery(wacom);
	wacom_destroy_leds(wacom);
	wacom_remove_shared_data(&wacom->wacom_wac);

	hid_set_drvdata(hdev, NULL);
	kfree(wacom);
}

static int wacom_resume(struct hid_device *hdev)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_features *features = &wacom->wacom_wac.features;

	mutex_lock(&wacom->lock);

	/* switch to wacom mode first */
	wacom_query_tablet_data(hdev, features);
	wacom_led_control(wacom);

	mutex_unlock(&wacom->lock);

	return 0;
}

static int wacom_reset_resume(struct hid_device *hdev)
{
	return wacom_resume(hdev);
}

static struct hid_driver wacom_driver = {
	.name =		"wacom",
	.id_table =	wacom_ids,
	.probe =	wacom_probe,
	.remove =	wacom_remove,
#ifdef CONFIG_PM
	.resume =	wacom_resume,
	.reset_resume =	wacom_reset_resume,
#endif
	.raw_event =	wacom_raw_event,
};
module_hid_driver(wacom_driver);
