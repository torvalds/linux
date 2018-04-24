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
#include <linux/input/mt.h>

#define WAC_MSG_RETRIES		5
#define WAC_CMD_RETRIES		10

#define DEV_ATTR_RW_PERM (S_IRUGO | S_IWUSR | S_IWGRP)
#define DEV_ATTR_WO_PERM (S_IWUSR | S_IWGRP)
#define DEV_ATTR_RO_PERM (S_IRUSR | S_IRGRP)

static int wacom_get_report(struct hid_device *hdev, u8 type, u8 *buf,
			    size_t size, unsigned int retries)
{
	int retval;

	do {
		retval = hid_hw_raw_request(hdev, buf[0], buf, size, type,
				HID_REQ_GET_REPORT);
	} while ((retval == -ETIMEDOUT || retval == -EAGAIN) && --retries);

	if (retval < 0)
		hid_err(hdev, "wacom_get_report: ran out of retries "
			"(last error = %d)\n", retval);

	return retval;
}

static int wacom_set_report(struct hid_device *hdev, u8 type, u8 *buf,
			    size_t size, unsigned int retries)
{
	int retval;

	do {
		retval = hid_hw_raw_request(hdev, buf[0], buf, size, type,
				HID_REQ_SET_REPORT);
	} while ((retval == -ETIMEDOUT || retval == -EAGAIN) && --retries);

	if (retval < 0)
		hid_err(hdev, "wacom_set_report: ran out of retries "
			"(last error = %d)\n", retval);

	return retval;
}

static void wacom_wac_queue_insert(struct hid_device *hdev,
				   struct kfifo_rec_ptr_2 *fifo,
				   u8 *raw_data, int size)
{
	bool warned = false;

	while (kfifo_avail(fifo) < size) {
		if (!warned)
			hid_warn(hdev, "%s: kfifo has filled, starting to drop events\n", __func__);
		warned = true;

		kfifo_skip(fifo);
	}

	kfifo_in(fifo, raw_data, size);
}

static void wacom_wac_queue_flush(struct hid_device *hdev,
				  struct kfifo_rec_ptr_2 *fifo)
{
	while (!kfifo_is_empty(fifo)) {
		u8 buf[WACOM_PKGLEN_MAX];
		int size;
		int err;

		size = kfifo_out(fifo, buf, sizeof(buf));
		err = hid_report_raw_event(hdev, HID_INPUT_REPORT, buf, size, false);
		if (err) {
			hid_warn(hdev, "%s: unable to flush event due to error %d\n",
				 __func__, err);
		}
	}
}

static int wacom_wac_pen_serial_enforce(struct hid_device *hdev,
		struct hid_report *report, u8 *raw_data, int size)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;
	bool flush = false;
	bool insert = false;
	int i, j;

	if (wacom_wac->serial[0] || !(features->quirks & WACOM_QUIRK_TOOLSERIAL))
		return 0;

	/* Queue events which have invalid tool type or serial number */
	for (i = 0; i < report->maxfield; i++) {
		for (j = 0; j < report->field[i]->maxusage; j++) {
			struct hid_field *field = report->field[i];
			struct hid_usage *usage = &field->usage[j];
			unsigned int equivalent_usage = wacom_equivalent_usage(usage->hid);
			unsigned int offset;
			unsigned int size;
			unsigned int value;

			if (equivalent_usage != HID_DG_INRANGE &&
			    equivalent_usage != HID_DG_TOOLSERIALNUMBER &&
			    equivalent_usage != WACOM_HID_WD_SERIALHI &&
			    equivalent_usage != WACOM_HID_WD_TOOLTYPE)
				continue;

			offset = field->report_offset;
			size = field->report_size;
			value = hid_field_extract(hdev, raw_data+1, offset + j * size, size);

			/* If we go out of range, we need to flush the queue ASAP */
			if (equivalent_usage == HID_DG_INRANGE)
				value = !value;

			if (value) {
				flush = true;
				switch (equivalent_usage) {
				case HID_DG_TOOLSERIALNUMBER:
					wacom_wac->serial[0] = value;
					break;

				case WACOM_HID_WD_SERIALHI:
					wacom_wac->serial[0] |= ((__u64)value) << 32;
					break;

				case WACOM_HID_WD_TOOLTYPE:
					wacom_wac->id[0] = value;
					break;
				}
			}
			else {
				insert = true;
			}
		}
	}

	if (flush)
		wacom_wac_queue_flush(hdev, &wacom_wac->pen_fifo);
	else if (insert)
		wacom_wac_queue_insert(hdev, &wacom_wac->pen_fifo, raw_data, size);

	return insert && !flush;
}

static int wacom_raw_event(struct hid_device *hdev, struct hid_report *report,
		u8 *raw_data, int size)
{
	struct wacom *wacom = hid_get_drvdata(hdev);

	if (size > WACOM_PKGLEN_MAX)
		return 1;

	if (wacom_wac_pen_serial_enforce(hdev, report, raw_data, size))
		return -1;

	memcpy(wacom->wacom_wac.data, raw_data, size);

	wacom_wac_irq(&wacom->wacom_wac, size);

	return 0;
}

static int wacom_open(struct input_dev *dev)
{
	struct wacom *wacom = input_get_drvdata(dev);

	return hid_hw_open(wacom->hdev);
}

static void wacom_close(struct input_dev *dev)
{
	struct wacom *wacom = input_get_drvdata(dev);

	/*
	 * wacom->hdev should never be null, but surprisingly, I had the case
	 * once while unplugging the Wacom Wireless Receiver.
	 */
	if (wacom->hdev)
		hid_hw_close(wacom->hdev);
}

/*
 * Calculate the resolution of the X or Y axis using hidinput_calc_abs_res.
 */
static int wacom_calc_hid_res(int logical_extents, int physical_extents,
			       unsigned unit, int exponent)
{
	struct hid_field field = {
		.logical_maximum = logical_extents,
		.physical_maximum = physical_extents,
		.unit = unit,
		.unit_exponent = exponent,
	};

	return hidinput_calc_abs_res(&field, ABS_X);
}

static void wacom_feature_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_features *features = &wacom->wacom_wac.features;
	struct hid_data *hid_data = &wacom->wacom_wac.hid_data;
	unsigned int equivalent_usage = wacom_equivalent_usage(usage->hid);
	u8 *data;
	int ret;
	u32 n;

	switch (equivalent_usage) {
	case HID_DG_CONTACTMAX:
		/* leave touch_max as is if predefined */
		if (!features->touch_max) {
			/* read manually */
			data = kzalloc(2, GFP_KERNEL);
			if (!data)
				break;
			data[0] = field->report->id;
			ret = wacom_get_report(hdev, HID_FEATURE_REPORT,
						data, 2, WAC_CMD_RETRIES);
			if (ret == 2) {
				features->touch_max = data[1];
			} else {
				features->touch_max = 16;
				hid_warn(hdev, "wacom_feature_mapping: "
					 "could not get HID_DG_CONTACTMAX, "
					 "defaulting to %d\n",
					  features->touch_max);
			}
			kfree(data);
		}
		break;
	case HID_DG_INPUTMODE:
		/* Ignore if value index is out of bounds. */
		if (usage->usage_index >= field->report_count) {
			dev_err(&hdev->dev, "HID_DG_INPUTMODE out of range\n");
			break;
		}

		hid_data->inputmode = field->report->id;
		hid_data->inputmode_index = usage->usage_index;
		break;

	case HID_UP_DIGITIZER:
		if (field->report->id == 0x0B &&
		    (field->application == WACOM_HID_G9_PEN ||
		     field->application == WACOM_HID_G11_PEN)) {
			wacom->wacom_wac.mode_report = field->report->id;
			wacom->wacom_wac.mode_value = 0;
		}
		break;

	case WACOM_HID_WD_DATAMODE:
		wacom->wacom_wac.mode_report = field->report->id;
		wacom->wacom_wac.mode_value = 2;
		break;

	case WACOM_HID_UP_G9:
	case WACOM_HID_UP_G11:
		if (field->report->id == 0x03 &&
		    (field->application == WACOM_HID_G9_TOUCHSCREEN ||
		     field->application == WACOM_HID_G11_TOUCHSCREEN)) {
			wacom->wacom_wac.mode_report = field->report->id;
			wacom->wacom_wac.mode_value = 0;
		}
		break;
	case WACOM_HID_WD_OFFSETLEFT:
	case WACOM_HID_WD_OFFSETTOP:
	case WACOM_HID_WD_OFFSETRIGHT:
	case WACOM_HID_WD_OFFSETBOTTOM:
		/* read manually */
		n = hid_report_len(field->report);
		data = hid_alloc_report_buf(field->report, GFP_KERNEL);
		if (!data)
			break;
		data[0] = field->report->id;
		ret = wacom_get_report(hdev, HID_FEATURE_REPORT,
					data, n, WAC_CMD_RETRIES);
		if (ret == n) {
			ret = hid_report_raw_event(hdev, HID_FEATURE_REPORT,
						   data, n, 0);
		} else {
			hid_warn(hdev, "%s: could not retrieve sensor offsets\n",
				 __func__);
		}
		kfree(data);
		break;
	}

	if (hdev->vendor == USB_VENDOR_ID_WACOM &&
	    hdev->product == 0x4200 /* Dell Canvas 27 */ &&
	    field->application == HID_UP_MSVENDOR) {
		wacom->wacom_wac.mode_report = field->report->id;
		wacom->wacom_wac.mode_value = 2;
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
 * Intuos5 touch interface and 3rd gen Bamboo Touch do not contain useful
 * data. We deal with them after returning from this function.
 */
static void wacom_usage_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_features *features = &wacom->wacom_wac.features;
	bool finger = WACOM_FINGER_FIELD(field);
	bool pen = WACOM_PEN_FIELD(field);

	/*
	* Requiring Stylus Usage will ignore boot mouse
	* X/Y values and some cases of invalid Digitizer X/Y
	* values commonly reported.
	*/
	if (pen)
		features->device_type |= WACOM_DEVICETYPE_PEN;
	else if (finger)
		features->device_type |= WACOM_DEVICETYPE_TOUCH;
	else
		return;

	/*
	 * Bamboo models do not support HID_DG_CONTACTMAX.
	 * And, Bamboo Pen only descriptor contains touch.
	 */
	if (features->type > BAMBOO_PT) {
		/* ISDv4 touch devices at least supports one touch point */
		if (finger && !features->touch_max)
			features->touch_max = 1;
	}

	/*
	 * ISDv4 devices which predate HID's adoption of the
	 * HID_DG_BARELSWITCH2 usage use 0x000D0000 in its
	 * position instead. We can accurately detect if a
	 * usage with that value should be HID_DG_BARRELSWITCH2
	 * based on the surrounding usages, which have remained
	 * constant across generations.
	 */
	if (features->type == HID_GENERIC &&
	    usage->hid == 0x000D0000 &&
	    field->application == HID_DG_PEN &&
	    field->physical == HID_DG_STYLUS) {
		int i = usage->usage_index;

		if (i-4 >= 0 && i+1 < field->maxusage &&
		    field->usage[i-4].hid == HID_DG_TIPSWITCH &&
		    field->usage[i-3].hid == HID_DG_BARRELSWITCH &&
		    field->usage[i-2].hid == HID_DG_ERASER &&
		    field->usage[i-1].hid == HID_DG_INVERT &&
		    field->usage[i+1].hid == HID_DG_INRANGE) {
			usage->hid = HID_DG_BARRELSWITCH2;
		}
	}

	switch (usage->hid) {
	case HID_GD_X:
		features->x_max = field->logical_maximum;
		if (finger) {
			features->x_phy = field->physical_maximum;
			if ((features->type != BAMBOO_PT) &&
			    (features->type != BAMBOO_TOUCH)) {
				features->unit = field->unit;
				features->unitExpo = field->unit_exponent;
			}
		}
		break;
	case HID_GD_Y:
		features->y_max = field->logical_maximum;
		if (finger) {
			features->y_phy = field->physical_maximum;
			if ((features->type != BAMBOO_PT) &&
			    (features->type != BAMBOO_TOUCH)) {
				features->unit = field->unit;
				features->unitExpo = field->unit_exponent;
			}
		}
		break;
	case HID_DG_TIPPRESSURE:
		if (pen)
			features->pressure_max = field->logical_maximum;
		break;
	}

	if (features->type == HID_GENERIC)
		wacom_wac_usage_mapping(hdev, field, usage);
}

static void wacom_post_parse_hid(struct hid_device *hdev,
				 struct wacom_features *features)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;

	if (features->type == HID_GENERIC) {
		/* Any last-minute generic device setup */
		if (wacom_wac->has_mode_change) {
			if (wacom_wac->is_direct_mode)
				features->device_type |= WACOM_DEVICETYPE_DIRECT;
			else
				features->device_type &= ~WACOM_DEVICETYPE_DIRECT;
		}

		if (features->touch_max > 1) {
			if (features->device_type & WACOM_DEVICETYPE_DIRECT)
				input_mt_init_slots(wacom_wac->touch_input,
						    wacom_wac->features.touch_max,
						    INPUT_MT_DIRECT);
			else
				input_mt_init_slots(wacom_wac->touch_input,
						    wacom_wac->features.touch_max,
						    INPUT_MT_POINTER);
		}
	}
}

static void wacom_parse_hid(struct hid_device *hdev,
			   struct wacom_features *features)
{
	struct hid_report_enum *rep_enum;
	struct hid_report *hreport;
	int i, j;

	/* check features first */
	rep_enum = &hdev->report_enum[HID_FEATURE_REPORT];
	list_for_each_entry(hreport, &rep_enum->report_list, list) {
		for (i = 0; i < hreport->maxfield; i++) {
			/* Ignore if report count is out of bounds. */
			if (hreport->field[i]->report_count < 1)
				continue;

			for (j = 0; j < hreport->field[i]->maxusage; j++) {
				wacom_feature_mapping(hdev, hreport->field[i],
						hreport->field[i]->usage + j);
			}
		}
	}

	/* now check the input usages */
	rep_enum = &hdev->report_enum[HID_INPUT_REPORT];
	list_for_each_entry(hreport, &rep_enum->report_list, list) {

		if (!hreport->maxfield)
			continue;

		for (i = 0; i < hreport->maxfield; i++)
			for (j = 0; j < hreport->field[i]->maxusage; j++)
				wacom_usage_mapping(hdev, hreport->field[i],
						hreport->field[i]->usage + j);
	}

	wacom_post_parse_hid(hdev, features);
}

static int wacom_hid_set_device_mode(struct hid_device *hdev)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct hid_data *hid_data = &wacom->wacom_wac.hid_data;
	struct hid_report *r;
	struct hid_report_enum *re;

	if (hid_data->inputmode < 0)
		return 0;

	re = &(hdev->report_enum[HID_FEATURE_REPORT]);
	r = re->report_id_hash[hid_data->inputmode];
	if (r) {
		r->field[0]->value[hid_data->inputmode_index] = 2;
		hid_hw_request(hdev, r, HID_REQ_SET_REPORT);
	}
	return 0;
}

static int wacom_set_device_mode(struct hid_device *hdev,
				 struct wacom_wac *wacom_wac)
{
	u8 *rep_data;
	struct hid_report *r;
	struct hid_report_enum *re;
	u32 length;
	int error = -ENOMEM, limit = 0;

	if (wacom_wac->mode_report < 0)
		return 0;

	re = &(hdev->report_enum[HID_FEATURE_REPORT]);
	r = re->report_id_hash[wacom_wac->mode_report];
	if (!r)
		return -EINVAL;

	rep_data = hid_alloc_report_buf(r, GFP_KERNEL);
	if (!rep_data)
		return -ENOMEM;

	length = hid_report_len(r);

	do {
		rep_data[0] = wacom_wac->mode_report;
		rep_data[1] = wacom_wac->mode_value;

		error = wacom_set_report(hdev, HID_FEATURE_REPORT, rep_data,
					 length, 1);
		if (error >= 0)
			error = wacom_get_report(hdev, HID_FEATURE_REPORT,
			                         rep_data, length, 1);
	} while (error >= 0 &&
		 rep_data[1] != wacom_wac->mode_report &&
		 limit++ < WAC_MSG_RETRIES);

	kfree(rep_data);

	return error < 0 ? error : 0;
}

static int wacom_bt_query_tablet_data(struct hid_device *hdev, u8 speed,
		struct wacom_features *features)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	int ret;
	u8 rep_data[2];

	switch (features->type) {
	case GRAPHIRE_BT:
		rep_data[0] = 0x03;
		rep_data[1] = 0x00;
		ret = wacom_set_report(hdev, HID_FEATURE_REPORT, rep_data, 2,
					3);

		if (ret >= 0) {
			rep_data[0] = speed == 0 ? 0x05 : 0x06;
			rep_data[1] = 0x00;

			ret = wacom_set_report(hdev, HID_FEATURE_REPORT,
						rep_data, 2, 3);

			if (ret >= 0) {
				wacom->wacom_wac.bt_high_speed = speed;
				return 0;
			}
		}

		/*
		 * Note that if the raw queries fail, it's not a hard failure
		 * and it is safe to continue
		 */
		hid_warn(hdev, "failed to poke device, command %d, err %d\n",
			 rep_data[0], ret);
		break;
	case INTUOS4WL:
		if (speed == 1)
			wacom->wacom_wac.bt_features &= ~0x20;
		else
			wacom->wacom_wac.bt_features |= 0x20;

		rep_data[0] = 0x03;
		rep_data[1] = wacom->wacom_wac.bt_features;

		ret = wacom_set_report(hdev, HID_FEATURE_REPORT, rep_data, 2,
					1);
		if (ret >= 0)
			wacom->wacom_wac.bt_high_speed = speed;
		break;
	}

	return 0;
}

/*
 * Switch the tablet into its most-capable mode. Wacom tablets are
 * typically configured to power-up in a mode which sends mouse-like
 * reports to the OS. To get absolute position, pressure data, etc.
 * from the tablet, it is necessary to switch the tablet out of this
 * mode and into one which sends the full range of tablet data.
 */
static int _wacom_query_tablet_data(struct wacom *wacom)
{
	struct hid_device *hdev = wacom->hdev;
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;

	if (hdev->bus == BUS_BLUETOOTH)
		return wacom_bt_query_tablet_data(hdev, 1, features);

	if (features->type != HID_GENERIC) {
		if (features->device_type & WACOM_DEVICETYPE_TOUCH) {
			if (features->type > TABLETPC) {
				/* MT Tablet PC touch */
				wacom_wac->mode_report = 3;
				wacom_wac->mode_value = 4;
			} else if (features->type == WACOM_24HDT) {
				wacom_wac->mode_report = 18;
				wacom_wac->mode_value = 2;
			} else if (features->type == WACOM_27QHDT) {
				wacom_wac->mode_report = 131;
				wacom_wac->mode_value = 2;
			} else if (features->type == BAMBOO_PAD) {
				wacom_wac->mode_report = 2;
				wacom_wac->mode_value = 2;
			}
		} else if (features->device_type & WACOM_DEVICETYPE_PEN) {
			if (features->type <= BAMBOO_PT) {
				wacom_wac->mode_report = 2;
				wacom_wac->mode_value = 2;
			}
		}
	}

	wacom_set_device_mode(hdev, wacom_wac);

	if (features->type == HID_GENERIC)
		return wacom_hid_set_device_mode(hdev);

	return 0;
}

static void wacom_retrieve_hid_descriptor(struct hid_device *hdev,
					 struct wacom_features *features)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct usb_interface *intf = wacom->intf;

	/* default features */
	features->x_fuzz = 4;
	features->y_fuzz = 4;
	features->pressure_fuzz = 0;
	features->distance_fuzz = 1;
	features->tilt_fuzz = 1;

	/*
	 * The wireless device HID is basic and layout conflicts with
	 * other tablets (monitor and touch interface can look like pen).
	 * Skip the query for this type and modify defaults based on
	 * interface number.
	 */
	if (features->type == WIRELESS) {
		if (intf->cur_altsetting->desc.bInterfaceNumber == 0)
			features->device_type = WACOM_DEVICETYPE_WL_MONITOR;
		else
			features->device_type = WACOM_DEVICETYPE_NONE;
		return;
	}

	wacom_parse_hid(hdev, features);
}

struct wacom_hdev_data {
	struct list_head list;
	struct kref kref;
	struct hid_device *dev;
	struct wacom_shared shared;
};

static LIST_HEAD(wacom_udev_list);
static DEFINE_MUTEX(wacom_udev_list_lock);

static bool compare_device_paths(struct hid_device *hdev_a,
		struct hid_device *hdev_b, char separator)
{
	int n1 = strrchr(hdev_a->phys, separator) - hdev_a->phys;
	int n2 = strrchr(hdev_b->phys, separator) - hdev_b->phys;

	if (n1 != n2 || n1 <= 0 || n2 <= 0)
		return false;

	return !strncmp(hdev_a->phys, hdev_b->phys, n1);
}

static bool wacom_are_sibling(struct hid_device *hdev,
		struct hid_device *sibling)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_features *features = &wacom->wacom_wac.features;
	struct wacom *sibling_wacom = hid_get_drvdata(sibling);
	struct wacom_features *sibling_features = &sibling_wacom->wacom_wac.features;
	__u32 oVid = features->oVid ? features->oVid : hdev->vendor;
	__u32 oPid = features->oPid ? features->oPid : hdev->product;

	/* The defined oVid/oPid must match that of the sibling */
	if (features->oVid != HID_ANY_ID && sibling->vendor != oVid)
		return false;
	if (features->oPid != HID_ANY_ID && sibling->product != oPid)
		return false;

	/*
	 * Devices with the same VID/PID must share the same physical
	 * device path, while those with different VID/PID must share
	 * the same physical parent device path.
	 */
	if (hdev->vendor == sibling->vendor && hdev->product == sibling->product) {
		if (!compare_device_paths(hdev, sibling, '/'))
			return false;
	} else {
		if (!compare_device_paths(hdev, sibling, '.'))
			return false;
	}

	/* Skip the remaining heuristics unless you are a HID_GENERIC device */
	if (features->type != HID_GENERIC)
		return true;

	/*
	 * Direct-input devices may not be siblings of indirect-input
	 * devices.
	 */
	if ((features->device_type & WACOM_DEVICETYPE_DIRECT) &&
	    !(sibling_features->device_type & WACOM_DEVICETYPE_DIRECT))
		return false;

	/*
	 * Indirect-input devices may not be siblings of direct-input
	 * devices.
	 */
	if (!(features->device_type & WACOM_DEVICETYPE_DIRECT) &&
	    (sibling_features->device_type & WACOM_DEVICETYPE_DIRECT))
		return false;

	/* Pen devices may only be siblings of touch devices */
	if ((features->device_type & WACOM_DEVICETYPE_PEN) &&
	    !(sibling_features->device_type & WACOM_DEVICETYPE_TOUCH))
		return false;

	/* Touch devices may only be siblings of pen devices */
	if ((features->device_type & WACOM_DEVICETYPE_TOUCH) &&
	    !(sibling_features->device_type & WACOM_DEVICETYPE_PEN))
		return false;

	/*
	 * No reason could be found for these two devices to NOT be
	 * siblings, so there's a good chance they ARE siblings
	 */
	return true;
}

static struct wacom_hdev_data *wacom_get_hdev_data(struct hid_device *hdev)
{
	struct wacom_hdev_data *data;

	/* Try to find an already-probed interface from the same device */
	list_for_each_entry(data, &wacom_udev_list, list) {
		if (compare_device_paths(hdev, data->dev, '/')) {
			kref_get(&data->kref);
			return data;
		}
	}

	/* Fallback to finding devices that appear to be "siblings" */
	list_for_each_entry(data, &wacom_udev_list, list) {
		if (wacom_are_sibling(hdev, data->dev)) {
			kref_get(&data->kref);
			return data;
		}
	}

	return NULL;
}

static void wacom_release_shared_data(struct kref *kref)
{
	struct wacom_hdev_data *data =
		container_of(kref, struct wacom_hdev_data, kref);

	mutex_lock(&wacom_udev_list_lock);
	list_del(&data->list);
	mutex_unlock(&wacom_udev_list_lock);

	kfree(data);
}

static void wacom_remove_shared_data(void *res)
{
	struct wacom *wacom = res;
	struct wacom_hdev_data *data;
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;

	if (wacom_wac->shared) {
		data = container_of(wacom_wac->shared, struct wacom_hdev_data,
				    shared);

		if (wacom_wac->shared->touch == wacom->hdev)
			wacom_wac->shared->touch = NULL;
		else if (wacom_wac->shared->pen == wacom->hdev)
			wacom_wac->shared->pen = NULL;

		kref_put(&data->kref, wacom_release_shared_data);
		wacom_wac->shared = NULL;
	}
}

static int wacom_add_shared_data(struct hid_device *hdev)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_hdev_data *data;
	int retval = 0;

	mutex_lock(&wacom_udev_list_lock);

	data = wacom_get_hdev_data(hdev);
	if (!data) {
		data = kzalloc(sizeof(struct wacom_hdev_data), GFP_KERNEL);
		if (!data) {
			retval = -ENOMEM;
			goto out;
		}

		kref_init(&data->kref);
		data->dev = hdev;
		list_add_tail(&data->list, &wacom_udev_list);
	}

	wacom_wac->shared = &data->shared;

	retval = devm_add_action(&hdev->dev, wacom_remove_shared_data, wacom);
	if (retval) {
		mutex_unlock(&wacom_udev_list_lock);
		wacom_remove_shared_data(wacom);
		return retval;
	}

	if (wacom_wac->features.device_type & WACOM_DEVICETYPE_TOUCH)
		wacom_wac->shared->touch = hdev;
	else if (wacom_wac->features.device_type & WACOM_DEVICETYPE_PEN)
		wacom_wac->shared->pen = hdev;

out:
	mutex_unlock(&wacom_udev_list_lock);
	return retval;
}

static int wacom_led_control(struct wacom *wacom)
{
	unsigned char *buf;
	int retval;
	unsigned char report_id = WAC_CMD_LED_CONTROL;
	int buf_size = 9;

	if (!wacom->led.groups)
		return -ENOTSUPP;

	if (wacom->wacom_wac.features.type == REMOTE)
		return -ENOTSUPP;

	if (wacom->wacom_wac.pid) { /* wireless connected */
		report_id = WAC_CMD_WL_LED_CONTROL;
		buf_size = 13;
	}
	else if (wacom->wacom_wac.features.type == INTUOSP2_BT) {
		report_id = WAC_CMD_WL_INTUOSP2;
		buf_size = 51;
	}
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (wacom->wacom_wac.features.type == HID_GENERIC) {
		buf[0] = WAC_CMD_LED_CONTROL_GENERIC;
		buf[1] = wacom->led.llv;
		buf[2] = wacom->led.groups[0].select & 0x03;

	} else if ((wacom->wacom_wac.features.type >= INTUOS5S &&
	    wacom->wacom_wac.features.type <= INTUOSPL)) {
		/*
		 * Touch Ring and crop mark LED luminance may take on
		 * one of four values:
		 *    0 = Low; 1 = Medium; 2 = High; 3 = Off
		 */
		int ring_led = wacom->led.groups[0].select & 0x03;
		int ring_lum = (((wacom->led.llv & 0x60) >> 5) - 1) & 0x03;
		int crop_lum = 0;
		unsigned char led_bits = (crop_lum << 4) | (ring_lum << 2) | (ring_led);

		buf[0] = report_id;
		if (wacom->wacom_wac.pid) {
			wacom_get_report(wacom->hdev, HID_FEATURE_REPORT,
					 buf, buf_size, WAC_CMD_RETRIES);
			buf[0] = report_id;
			buf[4] = led_bits;
		} else
			buf[1] = led_bits;
	}
	else if (wacom->wacom_wac.features.type == INTUOSP2_BT) {
		buf[0] = report_id;
		buf[4] = 100; // Power Connection LED (ORANGE)
		buf[5] = 100; // BT Connection LED (BLUE)
		buf[6] = 100; // Paper Mode (RED?)
		buf[7] = 100; // Paper Mode (GREEN?)
		buf[8] = 100; // Paper Mode (BLUE?)
		buf[9] = wacom->led.llv;
		buf[10] = wacom->led.groups[0].select & 0x03;
	}
	else {
		int led = wacom->led.groups[0].select | 0x4;

		if (wacom->wacom_wac.features.type == WACOM_21UX2 ||
		    wacom->wacom_wac.features.type == WACOM_24HD)
			led |= (wacom->led.groups[1].select << 4) | 0x40;

		buf[0] = report_id;
		buf[1] = led;
		buf[2] = wacom->led.llv;
		buf[3] = wacom->led.hlv;
		buf[4] = wacom->led.img_lum;
	}

	retval = wacom_set_report(wacom->hdev, HID_FEATURE_REPORT, buf, buf_size,
				  WAC_CMD_RETRIES);
	kfree(buf);

	return retval;
}

static int wacom_led_putimage(struct wacom *wacom, int button_id, u8 xfer_id,
		const unsigned len, const void *img)
{
	unsigned char *buf;
	int i, retval;
	const unsigned chunk_len = len / 4; /* 4 chunks are needed to be sent */

	buf = kzalloc(chunk_len + 3 , GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Send 'start' command */
	buf[0] = WAC_CMD_ICON_START;
	buf[1] = 1;
	retval = wacom_set_report(wacom->hdev, HID_FEATURE_REPORT, buf, 2,
				  WAC_CMD_RETRIES);
	if (retval < 0)
		goto out;

	buf[0] = xfer_id;
	buf[1] = button_id & 0x07;
	for (i = 0; i < 4; i++) {
		buf[2] = i;
		memcpy(buf + 3, img + i * chunk_len, chunk_len);

		retval = wacom_set_report(wacom->hdev, HID_FEATURE_REPORT,
					  buf, chunk_len + 3, WAC_CMD_RETRIES);
		if (retval < 0)
			break;
	}

	/* Send 'stop' */
	buf[0] = WAC_CMD_ICON_START;
	buf[1] = 0;
	wacom_set_report(wacom->hdev, HID_FEATURE_REPORT, buf, 2,
			 WAC_CMD_RETRIES);

out:
	kfree(buf);
	return retval;
}

static ssize_t wacom_led_select_store(struct device *dev, int set_id,
				      const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct wacom *wacom = hid_get_drvdata(hdev);
	unsigned int id;
	int err;

	err = kstrtouint(buf, 10, &id);
	if (err)
		return err;

	mutex_lock(&wacom->lock);

	wacom->led.groups[set_id].select = id & 0x3;
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
	struct hid_device *hdev = to_hid_device(dev);\
	struct wacom *wacom = hid_get_drvdata(hdev);			\
	return scnprintf(buf, PAGE_SIZE, "%d\n",			\
			 wacom->led.groups[SET_ID].select);		\
}									\
static DEVICE_ATTR(status_led##SET_ID##_select, DEV_ATTR_RW_PERM,	\
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
	struct hid_device *hdev = to_hid_device(dev);\
	struct wacom *wacom = hid_get_drvdata(hdev);			\
									\
	return wacom_luminance_store(wacom, &wacom->led.field,		\
				     buf, count);			\
}									\
static ssize_t wacom_##name##_luminance_show(struct device *dev,	\
	struct device_attribute *attr, char *buf)			\
{									\
	struct wacom *wacom = dev_get_drvdata(dev);			\
	return scnprintf(buf, PAGE_SIZE, "%d\n", wacom->led.field);	\
}									\
static DEVICE_ATTR(name##_luminance, DEV_ATTR_RW_PERM,			\
		   wacom_##name##_luminance_show,			\
		   wacom_##name##_luminance_store)

DEVICE_LUMINANCE_ATTR(status0, llv);
DEVICE_LUMINANCE_ATTR(status1, hlv);
DEVICE_LUMINANCE_ATTR(buttons, img_lum);

static ssize_t wacom_button_image_store(struct device *dev, int button_id,
					const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct wacom *wacom = hid_get_drvdata(hdev);
	int err;
	unsigned len;
	u8 xfer_id;

	if (hdev->bus == BUS_BLUETOOTH) {
		len = 256;
		xfer_id = WAC_CMD_ICON_BT_XFER;
	} else {
		len = 1024;
		xfer_id = WAC_CMD_ICON_XFER;
	}

	if (count != len)
		return -EINVAL;

	mutex_lock(&wacom->lock);

	err = wacom_led_putimage(wacom, button_id, xfer_id, len, buf);

	mutex_unlock(&wacom->lock);

	return err < 0 ? err : count;
}

#define DEVICE_BTNIMG_ATTR(BUTTON_ID)					\
static ssize_t wacom_btnimg##BUTTON_ID##_store(struct device *dev,	\
	struct device_attribute *attr, const char *buf, size_t count)	\
{									\
	return wacom_button_image_store(dev, BUTTON_ID, buf, count);	\
}									\
static DEVICE_ATTR(button##BUTTON_ID##_rawimg, DEV_ATTR_WO_PERM,	\
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

static struct attribute *generic_led_attrs[] = {
	&dev_attr_status0_luminance.attr,
	&dev_attr_status_led0_select.attr,
	NULL
};

static struct attribute_group generic_led_attr_group = {
	.name = "wacom_led",
	.attrs = generic_led_attrs,
};

struct wacom_sysfs_group_devres {
	struct attribute_group *group;
	struct kobject *root;
};

static void wacom_devm_sysfs_group_release(struct device *dev, void *res)
{
	struct wacom_sysfs_group_devres *devres = res;
	struct kobject *kobj = devres->root;

	dev_dbg(dev, "%s: dropping reference to %s\n",
		__func__, devres->group->name);
	sysfs_remove_group(kobj, devres->group);
}

static int __wacom_devm_sysfs_create_group(struct wacom *wacom,
					   struct kobject *root,
					   struct attribute_group *group)
{
	struct wacom_sysfs_group_devres *devres;
	int error;

	devres = devres_alloc(wacom_devm_sysfs_group_release,
			      sizeof(struct wacom_sysfs_group_devres),
			      GFP_KERNEL);
	if (!devres)
		return -ENOMEM;

	devres->group = group;
	devres->root = root;

	error = sysfs_create_group(devres->root, group);
	if (error) {
		devres_free(devres);
		return error;
	}

	devres_add(&wacom->hdev->dev, devres);

	return 0;
}

static int wacom_devm_sysfs_create_group(struct wacom *wacom,
					 struct attribute_group *group)
{
	return __wacom_devm_sysfs_create_group(wacom, &wacom->hdev->dev.kobj,
					       group);
}

enum led_brightness wacom_leds_brightness_get(struct wacom_led *led)
{
	struct wacom *wacom = led->wacom;

	if (wacom->led.max_hlv)
		return led->hlv * LED_FULL / wacom->led.max_hlv;

	if (wacom->led.max_llv)
		return led->llv * LED_FULL / wacom->led.max_llv;

	/* device doesn't support brightness tuning */
	return LED_FULL;
}

static enum led_brightness __wacom_led_brightness_get(struct led_classdev *cdev)
{
	struct wacom_led *led = container_of(cdev, struct wacom_led, cdev);
	struct wacom *wacom = led->wacom;

	if (wacom->led.groups[led->group].select != led->id)
		return LED_OFF;

	return wacom_leds_brightness_get(led);
}

static int wacom_led_brightness_set(struct led_classdev *cdev,
				    enum led_brightness brightness)
{
	struct wacom_led *led = container_of(cdev, struct wacom_led, cdev);
	struct wacom *wacom = led->wacom;
	int error;

	mutex_lock(&wacom->lock);

	if (!wacom->led.groups || (brightness == LED_OFF &&
	    wacom->led.groups[led->group].select != led->id)) {
		error = 0;
		goto out;
	}

	led->llv = wacom->led.llv = wacom->led.max_llv * brightness / LED_FULL;
	led->hlv = wacom->led.hlv = wacom->led.max_hlv * brightness / LED_FULL;

	wacom->led.groups[led->group].select = led->id;

	error = wacom_led_control(wacom);

out:
	mutex_unlock(&wacom->lock);

	return error;
}

static void wacom_led_readonly_brightness_set(struct led_classdev *cdev,
					       enum led_brightness brightness)
{
}

static int wacom_led_register_one(struct device *dev, struct wacom *wacom,
				  struct wacom_led *led, unsigned int group,
				  unsigned int id, bool read_only)
{
	int error;
	char *name;

	name = devm_kasprintf(dev, GFP_KERNEL,
			      "%s::wacom-%d.%d",
			      dev_name(dev),
			      group,
			      id);
	if (!name)
		return -ENOMEM;

	if (!read_only) {
		led->trigger.name = name;
		error = devm_led_trigger_register(dev, &led->trigger);
		if (error) {
			hid_err(wacom->hdev,
				"failed to register LED trigger %s: %d\n",
				led->cdev.name, error);
			return error;
		}
	}

	led->group = group;
	led->id = id;
	led->wacom = wacom;
	led->llv = wacom->led.llv;
	led->hlv = wacom->led.hlv;
	led->cdev.name = name;
	led->cdev.max_brightness = LED_FULL;
	led->cdev.flags = LED_HW_PLUGGABLE;
	led->cdev.brightness_get = __wacom_led_brightness_get;
	if (!read_only) {
		led->cdev.brightness_set_blocking = wacom_led_brightness_set;
		led->cdev.default_trigger = led->cdev.name;
	} else {
		led->cdev.brightness_set = wacom_led_readonly_brightness_set;
	}

	error = devm_led_classdev_register(dev, &led->cdev);
	if (error) {
		hid_err(wacom->hdev,
			"failed to register LED %s: %d\n",
			led->cdev.name, error);
		led->cdev.name = NULL;
		return error;
	}

	return 0;
}

static void wacom_led_groups_release_one(void *data)
{
	struct wacom_group_leds *group = data;

	devres_release_group(group->dev, group);
}

static int wacom_led_groups_alloc_and_register_one(struct device *dev,
						   struct wacom *wacom,
						   int group_id, int count,
						   bool read_only)
{
	struct wacom_led *leds;
	int i, error;

	if (group_id >= wacom->led.count || count <= 0)
		return -EINVAL;

	if (!devres_open_group(dev, &wacom->led.groups[group_id], GFP_KERNEL))
		return -ENOMEM;

	leds = devm_kzalloc(dev, sizeof(struct wacom_led) * count, GFP_KERNEL);
	if (!leds) {
		error = -ENOMEM;
		goto err;
	}

	wacom->led.groups[group_id].leds = leds;
	wacom->led.groups[group_id].count = count;

	for (i = 0; i < count; i++) {
		error = wacom_led_register_one(dev, wacom, &leds[i],
					       group_id, i, read_only);
		if (error)
			goto err;
	}

	wacom->led.groups[group_id].dev = dev;

	devres_close_group(dev, &wacom->led.groups[group_id]);

	/*
	 * There is a bug (?) in devm_led_classdev_register() in which its
	 * increments the refcount of the parent. If the parent is an input
	 * device, that means the ref count never reaches 0 when
	 * devm_input_device_release() gets called.
	 * This means that the LEDs are still there after disconnect.
	 * Manually force the release of the group so that the leds are released
	 * once we are done using them.
	 */
	error = devm_add_action_or_reset(&wacom->hdev->dev,
					 wacom_led_groups_release_one,
					 &wacom->led.groups[group_id]);
	if (error)
		return error;

	return 0;

err:
	devres_release_group(dev, &wacom->led.groups[group_id]);
	return error;
}

struct wacom_led *wacom_led_find(struct wacom *wacom, unsigned int group_id,
				 unsigned int id)
{
	struct wacom_group_leds *group;

	if (group_id >= wacom->led.count)
		return NULL;

	group = &wacom->led.groups[group_id];

	if (!group->leds)
		return NULL;

	id %= group->count;

	return &group->leds[id];
}

/**
 * wacom_led_next: gives the next available led with a wacom trigger.
 *
 * returns the next available struct wacom_led which has its default trigger
 * or the current one if none is available.
 */
struct wacom_led *wacom_led_next(struct wacom *wacom, struct wacom_led *cur)
{
	struct wacom_led *next_led;
	int group, next;

	if (!wacom || !cur)
		return NULL;

	group = cur->group;
	next = cur->id;

	do {
		next_led = wacom_led_find(wacom, group, ++next);
		if (!next_led || next_led == cur)
			return next_led;
	} while (next_led->cdev.trigger != &next_led->trigger);

	return next_led;
}

static void wacom_led_groups_release(void *data)
{
	struct wacom *wacom = data;

	wacom->led.groups = NULL;
	wacom->led.count = 0;
}

static int wacom_led_groups_allocate(struct wacom *wacom, int count)
{
	struct device *dev = &wacom->hdev->dev;
	struct wacom_group_leds *groups;
	int error;

	groups = devm_kzalloc(dev, sizeof(struct wacom_group_leds) * count,
			      GFP_KERNEL);
	if (!groups)
		return -ENOMEM;

	error = devm_add_action_or_reset(dev, wacom_led_groups_release, wacom);
	if (error)
		return error;

	wacom->led.groups = groups;
	wacom->led.count = count;

	return 0;
}

static int wacom_leds_alloc_and_register(struct wacom *wacom, int group_count,
					 int led_per_group, bool read_only)
{
	struct device *dev;
	int i, error;

	if (!wacom->wacom_wac.pad_input)
		return -EINVAL;

	dev = &wacom->wacom_wac.pad_input->dev;

	error = wacom_led_groups_allocate(wacom, group_count);
	if (error)
		return error;

	for (i = 0; i < group_count; i++) {
		error = wacom_led_groups_alloc_and_register_one(dev, wacom, i,
								led_per_group,
								read_only);
		if (error)
			return error;
	}

	return 0;
}

int wacom_initialize_leds(struct wacom *wacom)
{
	int error;

	if (!(wacom->wacom_wac.features.device_type & WACOM_DEVICETYPE_PAD))
		return 0;

	/* Initialize default values */
	switch (wacom->wacom_wac.features.type) {
	case HID_GENERIC:
		if (!wacom->generic_has_leds)
			return 0;
		wacom->led.llv = 100;
		wacom->led.max_llv = 100;

		error = wacom_leds_alloc_and_register(wacom, 1, 4, false);
		if (error) {
			hid_err(wacom->hdev,
				"cannot create leds err: %d\n", error);
			return error;
		}

		error = wacom_devm_sysfs_create_group(wacom,
						      &generic_led_attr_group);
		break;

	case INTUOS4S:
	case INTUOS4:
	case INTUOS4WL:
	case INTUOS4L:
		wacom->led.llv = 10;
		wacom->led.hlv = 20;
		wacom->led.max_llv = 127;
		wacom->led.max_hlv = 127;
		wacom->led.img_lum = 10;

		error = wacom_leds_alloc_and_register(wacom, 1, 4, false);
		if (error) {
			hid_err(wacom->hdev,
				"cannot create leds err: %d\n", error);
			return error;
		}

		error = wacom_devm_sysfs_create_group(wacom,
						      &intuos4_led_attr_group);
		break;

	case WACOM_24HD:
	case WACOM_21UX2:
		wacom->led.llv = 0;
		wacom->led.hlv = 0;
		wacom->led.img_lum = 0;

		error = wacom_leds_alloc_and_register(wacom, 2, 4, false);
		if (error) {
			hid_err(wacom->hdev,
				"cannot create leds err: %d\n", error);
			return error;
		}

		error = wacom_devm_sysfs_create_group(wacom,
						      &cintiq_led_attr_group);
		break;

	case INTUOS5S:
	case INTUOS5:
	case INTUOS5L:
	case INTUOSPS:
	case INTUOSPM:
	case INTUOSPL:
		wacom->led.llv = 32;
		wacom->led.max_llv = 96;

		error = wacom_leds_alloc_and_register(wacom, 1, 4, false);
		if (error) {
			hid_err(wacom->hdev,
				"cannot create leds err: %d\n", error);
			return error;
		}

		error = wacom_devm_sysfs_create_group(wacom,
						      &intuos5_led_attr_group);
		break;

	case INTUOSP2_BT:
		wacom->led.llv = 50;
		wacom->led.max_llv = 100;
		error = wacom_leds_alloc_and_register(wacom, 1, 4, false);
		if (error) {
			hid_err(wacom->hdev,
				"cannot create leds err: %d\n", error);
			return error;
		}
		return 0;

	case REMOTE:
		wacom->led.llv = 255;
		wacom->led.max_llv = 255;
		error = wacom_led_groups_allocate(wacom, 5);
		if (error) {
			hid_err(wacom->hdev,
				"cannot create leds err: %d\n", error);
			return error;
		}
		return 0;

	default:
		return 0;
	}

	if (error) {
		hid_err(wacom->hdev,
			"cannot create sysfs group err: %d\n", error);
		return error;
	}

	return 0;
}

static void wacom_init_work(struct work_struct *work)
{
	struct wacom *wacom = container_of(work, struct wacom, init_work.work);

	_wacom_query_tablet_data(wacom);
	wacom_led_control(wacom);
}

static void wacom_query_tablet_data(struct wacom *wacom)
{
	schedule_delayed_work(&wacom->init_work, msecs_to_jiffies(1000));
}

static enum power_supply_property wacom_battery_props[] = {
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_CAPACITY
};

static int wacom_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct wacom_battery *battery = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
		case POWER_SUPPLY_PROP_MODEL_NAME:
			val->strval = battery->wacom->wacom_wac.name;
			break;
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = battery->bat_connected;
			break;
		case POWER_SUPPLY_PROP_SCOPE:
			val->intval = POWER_SUPPLY_SCOPE_DEVICE;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = battery->battery_capacity;
			break;
		case POWER_SUPPLY_PROP_STATUS:
			if (battery->bat_status != WACOM_POWER_SUPPLY_STATUS_AUTO)
				val->intval = battery->bat_status;
			else if (battery->bat_charging)
				val->intval = POWER_SUPPLY_STATUS_CHARGING;
			else if (battery->battery_capacity == 100 &&
				    battery->ps_connected)
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else if (battery->ps_connected)
				val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

static int __wacom_initialize_battery(struct wacom *wacom,
				      struct wacom_battery *battery)
{
	static atomic_t battery_no = ATOMIC_INIT(0);
	struct device *dev = &wacom->hdev->dev;
	struct power_supply_config psy_cfg = { .drv_data = battery, };
	struct power_supply *ps_bat;
	struct power_supply_desc *bat_desc = &battery->bat_desc;
	unsigned long n;
	int error;

	if (!devres_open_group(dev, bat_desc, GFP_KERNEL))
		return -ENOMEM;

	battery->wacom = wacom;

	n = atomic_inc_return(&battery_no) - 1;

	bat_desc->properties = wacom_battery_props;
	bat_desc->num_properties = ARRAY_SIZE(wacom_battery_props);
	bat_desc->get_property = wacom_battery_get_property;
	sprintf(battery->bat_name, "wacom_battery_%ld", n);
	bat_desc->name = battery->bat_name;
	bat_desc->type = POWER_SUPPLY_TYPE_USB;
	bat_desc->use_for_apm = 0;

	ps_bat = devm_power_supply_register(dev, bat_desc, &psy_cfg);
	if (IS_ERR(ps_bat)) {
		error = PTR_ERR(ps_bat);
		goto err;
	}

	power_supply_powers(ps_bat, &wacom->hdev->dev);

	battery->battery = ps_bat;

	devres_close_group(dev, bat_desc);
	return 0;

err:
	devres_release_group(dev, bat_desc);
	return error;
}

static int wacom_initialize_battery(struct wacom *wacom)
{
	if (wacom->wacom_wac.features.quirks & WACOM_QUIRK_BATTERY)
		return __wacom_initialize_battery(wacom, &wacom->battery);

	return 0;
}

static void wacom_destroy_battery(struct wacom *wacom)
{
	if (wacom->battery.battery) {
		devres_release_group(&wacom->hdev->dev,
				     &wacom->battery.bat_desc);
		wacom->battery.battery = NULL;
	}
}

static ssize_t wacom_show_speed(struct device *dev,
				struct device_attribute
				*attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct wacom *wacom = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%i\n", wacom->wacom_wac.bt_high_speed);
}

static ssize_t wacom_store_speed(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct wacom *wacom = hid_get_drvdata(hdev);
	u8 new_speed;

	if (kstrtou8(buf, 0, &new_speed))
		return -EINVAL;

	if (new_speed != 0 && new_speed != 1)
		return -EINVAL;

	wacom_bt_query_tablet_data(hdev, new_speed, &wacom->wacom_wac.features);

	return count;
}

static DEVICE_ATTR(speed, DEV_ATTR_RW_PERM,
		wacom_show_speed, wacom_store_speed);


static ssize_t wacom_show_remote_mode(struct kobject *kobj,
				      struct kobj_attribute *kattr,
				      char *buf, int index)
{
	struct device *dev = kobj_to_dev(kobj->parent);
	struct hid_device *hdev = to_hid_device(dev);
	struct wacom *wacom = hid_get_drvdata(hdev);
	u8 mode;

	mode = wacom->led.groups[index].select;
	return sprintf(buf, "%d\n", mode < 3 ? mode : -1);
}

#define DEVICE_EKR_ATTR_GROUP(SET_ID)					\
static ssize_t wacom_show_remote##SET_ID##_mode(struct kobject *kobj,	\
			       struct kobj_attribute *kattr, char *buf)	\
{									\
	return wacom_show_remote_mode(kobj, kattr, buf, SET_ID);	\
}									\
static struct kobj_attribute remote##SET_ID##_mode_attr = {		\
	.attr = {.name = "remote_mode",					\
		.mode = DEV_ATTR_RO_PERM},				\
	.show = wacom_show_remote##SET_ID##_mode,			\
};									\
static struct attribute *remote##SET_ID##_serial_attrs[] = {		\
	&remote##SET_ID##_mode_attr.attr,				\
	NULL								\
};									\
static struct attribute_group remote##SET_ID##_serial_group = {		\
	.name = NULL,							\
	.attrs = remote##SET_ID##_serial_attrs,				\
}

DEVICE_EKR_ATTR_GROUP(0);
DEVICE_EKR_ATTR_GROUP(1);
DEVICE_EKR_ATTR_GROUP(2);
DEVICE_EKR_ATTR_GROUP(3);
DEVICE_EKR_ATTR_GROUP(4);

static int wacom_remote_create_attr_group(struct wacom *wacom, __u32 serial,
					  int index)
{
	int error = 0;
	struct wacom_remote *remote = wacom->remote;

	remote->remotes[index].group.name = devm_kasprintf(&wacom->hdev->dev,
							  GFP_KERNEL,
							  "%d", serial);
	if (!remote->remotes[index].group.name)
		return -ENOMEM;

	error = __wacom_devm_sysfs_create_group(wacom, remote->remote_dir,
						&remote->remotes[index].group);
	if (error) {
		remote->remotes[index].group.name = NULL;
		hid_err(wacom->hdev,
			"cannot create sysfs group err: %d\n", error);
		return error;
	}

	return 0;
}

static int wacom_cmd_unpair_remote(struct wacom *wacom, unsigned char selector)
{
	const size_t buf_size = 2;
	unsigned char *buf;
	int retval;

	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = WAC_CMD_DELETE_PAIRING;
	buf[1] = selector;

	retval = wacom_set_report(wacom->hdev, HID_OUTPUT_REPORT, buf,
				  buf_size, WAC_CMD_RETRIES);
	kfree(buf);

	return retval;
}

static ssize_t wacom_store_unpair_remote(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 const char *buf, size_t count)
{
	unsigned char selector = 0;
	struct device *dev = kobj_to_dev(kobj->parent);
	struct hid_device *hdev = to_hid_device(dev);
	struct wacom *wacom = hid_get_drvdata(hdev);
	int err;

	if (!strncmp(buf, "*\n", 2)) {
		selector = WAC_CMD_UNPAIR_ALL;
	} else {
		hid_info(wacom->hdev, "remote: unrecognized unpair code: %s\n",
			 buf);
		return -1;
	}

	mutex_lock(&wacom->lock);

	err = wacom_cmd_unpair_remote(wacom, selector);
	mutex_unlock(&wacom->lock);

	return err < 0 ? err : count;
}

static struct kobj_attribute unpair_remote_attr = {
	.attr = {.name = "unpair_remote", .mode = 0200},
	.store = wacom_store_unpair_remote,
};

static const struct attribute *remote_unpair_attrs[] = {
	&unpair_remote_attr.attr,
	NULL
};

static void wacom_remotes_destroy(void *data)
{
	struct wacom *wacom = data;
	struct wacom_remote *remote = wacom->remote;

	if (!remote)
		return;

	kobject_put(remote->remote_dir);
	kfifo_free(&remote->remote_fifo);
	wacom->remote = NULL;
}

static int wacom_initialize_remotes(struct wacom *wacom)
{
	int error = 0;
	struct wacom_remote *remote;
	int i;

	if (wacom->wacom_wac.features.type != REMOTE)
		return 0;

	remote = devm_kzalloc(&wacom->hdev->dev, sizeof(*wacom->remote),
			      GFP_KERNEL);
	if (!remote)
		return -ENOMEM;

	wacom->remote = remote;

	spin_lock_init(&remote->remote_lock);

	error = kfifo_alloc(&remote->remote_fifo,
			5 * sizeof(struct wacom_remote_data),
			GFP_KERNEL);
	if (error) {
		hid_err(wacom->hdev, "failed allocating remote_fifo\n");
		return -ENOMEM;
	}

	remote->remotes[0].group = remote0_serial_group;
	remote->remotes[1].group = remote1_serial_group;
	remote->remotes[2].group = remote2_serial_group;
	remote->remotes[3].group = remote3_serial_group;
	remote->remotes[4].group = remote4_serial_group;

	remote->remote_dir = kobject_create_and_add("wacom_remote",
						    &wacom->hdev->dev.kobj);
	if (!remote->remote_dir)
		return -ENOMEM;

	error = sysfs_create_files(remote->remote_dir, remote_unpair_attrs);

	if (error) {
		hid_err(wacom->hdev,
			"cannot create sysfs group err: %d\n", error);
		return error;
	}

	for (i = 0; i < WACOM_MAX_REMOTES; i++) {
		wacom->led.groups[i].select = WACOM_STATUS_UNKNOWN;
		remote->remotes[i].serial = 0;
	}

	error = devm_add_action_or_reset(&wacom->hdev->dev,
					 wacom_remotes_destroy, wacom);
	if (error)
		return error;

	return 0;
}

static struct input_dev *wacom_allocate_input(struct wacom *wacom)
{
	struct input_dev *input_dev;
	struct hid_device *hdev = wacom->hdev;
	struct wacom_wac *wacom_wac = &(wacom->wacom_wac);

	input_dev = devm_input_allocate_device(&hdev->dev);
	if (!input_dev)
		return NULL;

	input_dev->name = wacom_wac->features.name;
	input_dev->phys = hdev->phys;
	input_dev->dev.parent = &hdev->dev;
	input_dev->open = wacom_open;
	input_dev->close = wacom_close;
	input_dev->uniq = hdev->uniq;
	input_dev->id.bustype = hdev->bus;
	input_dev->id.vendor  = hdev->vendor;
	input_dev->id.product = wacom_wac->pid ? wacom_wac->pid : hdev->product;
	input_dev->id.version = hdev->version;
	input_set_drvdata(input_dev, wacom);

	return input_dev;
}

static int wacom_allocate_inputs(struct wacom *wacom)
{
	struct wacom_wac *wacom_wac = &(wacom->wacom_wac);

	wacom_wac->pen_input = wacom_allocate_input(wacom);
	wacom_wac->touch_input = wacom_allocate_input(wacom);
	wacom_wac->pad_input = wacom_allocate_input(wacom);
	if (!wacom_wac->pen_input ||
	    !wacom_wac->touch_input ||
	    !wacom_wac->pad_input)
		return -ENOMEM;

	wacom_wac->pen_input->name = wacom_wac->pen_name;
	wacom_wac->touch_input->name = wacom_wac->touch_name;
	wacom_wac->pad_input->name = wacom_wac->pad_name;

	return 0;
}

static int wacom_register_inputs(struct wacom *wacom)
{
	struct input_dev *pen_input_dev, *touch_input_dev, *pad_input_dev;
	struct wacom_wac *wacom_wac = &(wacom->wacom_wac);
	int error = 0;

	pen_input_dev = wacom_wac->pen_input;
	touch_input_dev = wacom_wac->touch_input;
	pad_input_dev = wacom_wac->pad_input;

	if (!pen_input_dev || !touch_input_dev || !pad_input_dev)
		return -EINVAL;

	error = wacom_setup_pen_input_capabilities(pen_input_dev, wacom_wac);
	if (error) {
		/* no pen in use on this interface */
		input_free_device(pen_input_dev);
		wacom_wac->pen_input = NULL;
		pen_input_dev = NULL;
	} else {
		error = input_register_device(pen_input_dev);
		if (error)
			goto fail;
	}

	error = wacom_setup_touch_input_capabilities(touch_input_dev, wacom_wac);
	if (error) {
		/* no touch in use on this interface */
		input_free_device(touch_input_dev);
		wacom_wac->touch_input = NULL;
		touch_input_dev = NULL;
	} else {
		error = input_register_device(touch_input_dev);
		if (error)
			goto fail;
	}

	error = wacom_setup_pad_input_capabilities(pad_input_dev, wacom_wac);
	if (error) {
		/* no pad in use on this interface */
		input_free_device(pad_input_dev);
		wacom_wac->pad_input = NULL;
		pad_input_dev = NULL;
	} else {
		error = input_register_device(pad_input_dev);
		if (error)
			goto fail;
	}

	return 0;

fail:
	wacom_wac->pad_input = NULL;
	wacom_wac->touch_input = NULL;
	wacom_wac->pen_input = NULL;
	return error;
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
	/* set unit to "100th of a mm" for devices not reported by HID */
	if (!features->unit) {
		features->unit = 0x11;
		features->unitExpo = -3;
	}

	features->x_resolution = wacom_calc_hid_res(features->x_max,
						    features->x_phy,
						    features->unit,
						    features->unitExpo);
	features->y_resolution = wacom_calc_hid_res(features->y_max,
						    features->y_phy,
						    features->unit,
						    features->unitExpo);
}

void wacom_battery_work(struct work_struct *work)
{
	struct wacom *wacom = container_of(work, struct wacom, battery_work);

	if ((wacom->wacom_wac.features.quirks & WACOM_QUIRK_BATTERY) &&
	     !wacom->battery.battery) {
		wacom_initialize_battery(wacom);
	}
	else if (!(wacom->wacom_wac.features.quirks & WACOM_QUIRK_BATTERY) &&
		 wacom->battery.battery) {
		wacom_destroy_battery(wacom);
	}
}

static size_t wacom_compute_pktlen(struct hid_device *hdev)
{
	struct hid_report_enum *report_enum;
	struct hid_report *report;
	size_t size = 0;

	report_enum = hdev->report_enum + HID_INPUT_REPORT;

	list_for_each_entry(report, &report_enum->report_list, list) {
		size_t report_size = hid_report_len(report);
		if (report_size > size)
			size = report_size;
	}

	return size;
}

static void wacom_update_name(struct wacom *wacom, const char *suffix)
{
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;
	char name[WACOM_NAME_MAX];

	/* Generic devices name unspecified */
	if ((features->type == HID_GENERIC) && !strcmp("Wacom HID", features->name)) {
		char *product_name = wacom->hdev->name;

		if (hid_is_using_ll_driver(wacom->hdev, &usb_hid_driver)) {
			struct usb_interface *intf = to_usb_interface(wacom->hdev->dev.parent);
			struct usb_device *dev = interface_to_usbdev(intf);
			product_name = dev->product;
		}

		if (wacom->hdev->bus == BUS_I2C) {
			snprintf(name, sizeof(name), "%s %X",
				 features->name, wacom->hdev->product);
		} else if (strstr(product_name, "Wacom") ||
			   strstr(product_name, "wacom") ||
			   strstr(product_name, "WACOM")) {
			strlcpy(name, product_name, sizeof(name));
		} else {
			snprintf(name, sizeof(name), "Wacom %s", product_name);
		}

		/* strip out excess whitespaces */
		while (1) {
			char *gap = strstr(name, "  ");
			if (gap == NULL)
				break;
			/* shift everything including the terminator */
			memmove(gap, gap+1, strlen(gap));
		}

		/* get rid of trailing whitespace */
		if (name[strlen(name)-1] == ' ')
			name[strlen(name)-1] = '\0';
	} else {
		strlcpy(name, features->name, sizeof(name));
	}

	snprintf(wacom_wac->name, sizeof(wacom_wac->name), "%s%s",
		 name, suffix);

	/* Append the device type to the name */
	snprintf(wacom_wac->pen_name, sizeof(wacom_wac->pen_name),
		"%s%s Pen", name, suffix);
	snprintf(wacom_wac->touch_name, sizeof(wacom_wac->touch_name),
		"%s%s Finger", name, suffix);
	snprintf(wacom_wac->pad_name, sizeof(wacom_wac->pad_name),
		"%s%s Pad", name, suffix);
}

static void wacom_release_resources(struct wacom *wacom)
{
	struct hid_device *hdev = wacom->hdev;

	if (!wacom->resources)
		return;

	devres_release_group(&hdev->dev, wacom);

	wacom->resources = false;

	wacom->wacom_wac.pen_input = NULL;
	wacom->wacom_wac.touch_input = NULL;
	wacom->wacom_wac.pad_input = NULL;
}

static void wacom_set_shared_values(struct wacom_wac *wacom_wac)
{
	if (wacom_wac->features.device_type & WACOM_DEVICETYPE_TOUCH) {
		wacom_wac->shared->type = wacom_wac->features.type;
		wacom_wac->shared->touch_input = wacom_wac->touch_input;
	}

	if (wacom_wac->has_mute_touch_switch) {
		wacom_wac->shared->has_mute_touch_switch = true;
		wacom_wac->shared->is_touch_on = true;
	}

	if (wacom_wac->shared->has_mute_touch_switch &&
	    wacom_wac->shared->touch_input) {
		set_bit(EV_SW, wacom_wac->shared->touch_input->evbit);
		input_set_capability(wacom_wac->shared->touch_input, EV_SW,
				     SW_MUTE_DEVICE);
	}
}

static int wacom_parse_and_register(struct wacom *wacom, bool wireless)
{
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;
	struct hid_device *hdev = wacom->hdev;
	int error;
	unsigned int connect_mask = HID_CONNECT_HIDRAW;

	features->pktlen = wacom_compute_pktlen(hdev);
	if (features->pktlen > WACOM_PKGLEN_MAX)
		return -EINVAL;

	if (!devres_open_group(&hdev->dev, wacom, GFP_KERNEL))
		return -ENOMEM;

	wacom->resources = true;

	error = wacom_allocate_inputs(wacom);
	if (error)
		goto fail;

	/*
	 * Bamboo Pad has a generic hid handling for the Pen, and we switch it
	 * into debug mode for the touch part.
	 * We ignore the other interfaces.
	 */
	if (features->type == BAMBOO_PAD) {
		if (features->pktlen == WACOM_PKGLEN_PENABLED) {
			features->type = HID_GENERIC;
		} else if ((features->pktlen != WACOM_PKGLEN_BPAD_TOUCH) &&
			   (features->pktlen != WACOM_PKGLEN_BPAD_TOUCH_USB)) {
			error = -ENODEV;
			goto fail;
		}
	}

	/* set the default size in case we do not get them from hid */
	wacom_set_default_phy(features);

	/* Retrieve the physical and logical size for touch devices */
	wacom_retrieve_hid_descriptor(hdev, features);
	wacom_setup_device_quirks(wacom);

	if (features->device_type == WACOM_DEVICETYPE_NONE &&
	    features->type != WIRELESS) {
		error = features->type == HID_GENERIC ? -ENODEV : 0;

		dev_warn(&hdev->dev, "Unknown device_type for '%s'. %s.",
			 hdev->name,
			 error ? "Ignoring" : "Assuming pen");

		if (error)
			goto fail;

		features->device_type |= WACOM_DEVICETYPE_PEN;
	}

	wacom_calculate_res(features);

	wacom_update_name(wacom, wireless ? " (WL)" : "");

	/* pen only Bamboo neither support touch nor pad */
	if ((features->type == BAMBOO_PEN) &&
	    ((features->device_type & WACOM_DEVICETYPE_TOUCH) ||
	    (features->device_type & WACOM_DEVICETYPE_PAD))) {
		error = -ENODEV;
		goto fail;
	}

	error = wacom_add_shared_data(hdev);
	if (error)
		goto fail;

	if (!(features->device_type & WACOM_DEVICETYPE_WL_MONITOR) &&
	     (features->quirks & WACOM_QUIRK_BATTERY)) {
		error = wacom_initialize_battery(wacom);
		if (error)
			goto fail;
	}

	error = wacom_register_inputs(wacom);
	if (error)
		goto fail;

	if (wacom->wacom_wac.features.device_type & WACOM_DEVICETYPE_PAD) {
		error = wacom_initialize_leds(wacom);
		if (error)
			goto fail;

		error = wacom_initialize_remotes(wacom);
		if (error)
			goto fail;
	}

	if (features->type == HID_GENERIC)
		connect_mask |= HID_CONNECT_DRIVER;

	/* Regular HID work starts now */
	error = hid_hw_start(hdev, connect_mask);
	if (error) {
		hid_err(hdev, "hw start failed\n");
		goto fail;
	}

	if (!wireless) {
		/* Note that if query fails it is not a hard failure */
		wacom_query_tablet_data(wacom);
	}

	/* touch only Bamboo doesn't support pen */
	if ((features->type == BAMBOO_TOUCH) &&
	    (features->device_type & WACOM_DEVICETYPE_PEN)) {
		cancel_delayed_work_sync(&wacom->init_work);
		_wacom_query_tablet_data(wacom);
		error = -ENODEV;
		goto fail_quirks;
	}

	if (features->device_type & WACOM_DEVICETYPE_WL_MONITOR)
		error = hid_hw_open(hdev);

	wacom_set_shared_values(wacom_wac);
	devres_close_group(&hdev->dev, wacom);

	return 0;

fail_quirks:
	hid_hw_stop(hdev);
fail:
	wacom_release_resources(wacom);
	return error;
}

static void wacom_wireless_work(struct work_struct *work)
{
	struct wacom *wacom = container_of(work, struct wacom, wireless_work);
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
	wacom_release_resources(wacom1);

	/* Touch interface */
	hdev2 = usb_get_intfdata(usbdev->config->interface[2]);
	wacom2 = hid_get_drvdata(hdev2);
	wacom_wac2 = &(wacom2->wacom_wac);
	wacom_release_resources(wacom2);

	if (wacom_wac->pid == 0) {
		hid_info(wacom->hdev, "wireless tablet disconnected\n");
	} else {
		const struct hid_device_id *id = wacom_ids;

		hid_info(wacom->hdev, "wireless tablet connected with PID %x\n",
			 wacom_wac->pid);

		while (id->bus) {
			if (id->vendor == USB_VENDOR_ID_WACOM &&
			    id->product == wacom_wac->pid)
				break;
			id++;
		}

		if (!id->bus) {
			hid_info(wacom->hdev, "ignoring unknown PID.\n");
			return;
		}

		/* Stylus interface */
		wacom_wac1->features =
			*((struct wacom_features *)id->driver_data);

		wacom_wac1->pid = wacom_wac->pid;
		hid_hw_stop(hdev1);
		error = wacom_parse_and_register(wacom1, true);
		if (error)
			goto fail;

		/* Touch interface */
		if (wacom_wac1->features.touch_max ||
		    (wacom_wac1->features.type >= INTUOSHT &&
		    wacom_wac1->features.type <= BAMBOO_PT)) {
			wacom_wac2->features =
				*((struct wacom_features *)id->driver_data);
			wacom_wac2->pid = wacom_wac->pid;
			hid_hw_stop(hdev2);
			error = wacom_parse_and_register(wacom2, true);
			if (error)
				goto fail;
		}

		strlcpy(wacom_wac->name, wacom_wac1->name,
			sizeof(wacom_wac->name));
		error = wacom_initialize_battery(wacom);
		if (error)
			goto fail;
	}

	return;

fail:
	wacom_release_resources(wacom1);
	wacom_release_resources(wacom2);
	return;
}

static void wacom_remote_destroy_one(struct wacom *wacom, unsigned int index)
{
	struct wacom_remote *remote = wacom->remote;
	u32 serial = remote->remotes[index].serial;
	int i;
	unsigned long flags;

	for (i = 0; i < WACOM_MAX_REMOTES; i++) {
		if (remote->remotes[i].serial == serial) {

			spin_lock_irqsave(&remote->remote_lock, flags);
			remote->remotes[i].registered = false;
			spin_unlock_irqrestore(&remote->remote_lock, flags);

			if (remote->remotes[i].battery.battery)
				devres_release_group(&wacom->hdev->dev,
						     &remote->remotes[i].battery.bat_desc);

			if (remote->remotes[i].group.name)
				devres_release_group(&wacom->hdev->dev,
						     &remote->remotes[i]);

			remote->remotes[i].serial = 0;
			remote->remotes[i].group.name = NULL;
			remote->remotes[i].battery.battery = NULL;
			wacom->led.groups[i].select = WACOM_STATUS_UNKNOWN;
		}
	}
}

static int wacom_remote_create_one(struct wacom *wacom, u32 serial,
				   unsigned int index)
{
	struct wacom_remote *remote = wacom->remote;
	struct device *dev = &wacom->hdev->dev;
	int error, k;

	/* A remote can pair more than once with an EKR,
	 * check to make sure this serial isn't already paired.
	 */
	for (k = 0; k < WACOM_MAX_REMOTES; k++) {
		if (remote->remotes[k].serial == serial)
			break;
	}

	if (k < WACOM_MAX_REMOTES) {
		remote->remotes[index].serial = serial;
		return 0;
	}

	if (!devres_open_group(dev, &remote->remotes[index], GFP_KERNEL))
		return -ENOMEM;

	error = wacom_remote_create_attr_group(wacom, serial, index);
	if (error)
		goto fail;

	remote->remotes[index].input = wacom_allocate_input(wacom);
	if (!remote->remotes[index].input) {
		error = -ENOMEM;
		goto fail;
	}
	remote->remotes[index].input->uniq = remote->remotes[index].group.name;
	remote->remotes[index].input->name = wacom->wacom_wac.pad_name;

	if (!remote->remotes[index].input->name) {
		error = -EINVAL;
		goto fail;
	}

	error = wacom_setup_pad_input_capabilities(remote->remotes[index].input,
						   &wacom->wacom_wac);
	if (error)
		goto fail;

	remote->remotes[index].serial = serial;

	error = input_register_device(remote->remotes[index].input);
	if (error)
		goto fail;

	error = wacom_led_groups_alloc_and_register_one(
					&remote->remotes[index].input->dev,
					wacom, index, 3, true);
	if (error)
		goto fail;

	remote->remotes[index].registered = true;

	devres_close_group(dev, &remote->remotes[index]);
	return 0;

fail:
	devres_release_group(dev, &remote->remotes[index]);
	remote->remotes[index].serial = 0;
	return error;
}

static int wacom_remote_attach_battery(struct wacom *wacom, int index)
{
	struct wacom_remote *remote = wacom->remote;
	int error;

	if (!remote->remotes[index].registered)
		return 0;

	if (remote->remotes[index].battery.battery)
		return 0;

	if (wacom->led.groups[index].select == WACOM_STATUS_UNKNOWN)
		return 0;

	error = __wacom_initialize_battery(wacom,
					&wacom->remote->remotes[index].battery);
	if (error)
		return error;

	return 0;
}

static void wacom_remote_work(struct work_struct *work)
{
	struct wacom *wacom = container_of(work, struct wacom, remote_work);
	struct wacom_remote *remote = wacom->remote;
	struct wacom_remote_data data;
	unsigned long flags;
	unsigned int count;
	u32 serial;
	int i;

	spin_lock_irqsave(&remote->remote_lock, flags);

	count = kfifo_out(&remote->remote_fifo, &data, sizeof(data));

	if (count != sizeof(data)) {
		hid_err(wacom->hdev,
			"workitem triggered without status available\n");
		spin_unlock_irqrestore(&remote->remote_lock, flags);
		return;
	}

	if (!kfifo_is_empty(&remote->remote_fifo))
		wacom_schedule_work(&wacom->wacom_wac, WACOM_WORKER_REMOTE);

	spin_unlock_irqrestore(&remote->remote_lock, flags);

	for (i = 0; i < WACOM_MAX_REMOTES; i++) {
		serial = data.remote[i].serial;
		if (data.remote[i].connected) {

			if (remote->remotes[i].serial == serial) {
				wacom_remote_attach_battery(wacom, i);
				continue;
			}

			if (remote->remotes[i].serial)
				wacom_remote_destroy_one(wacom, i);

			wacom_remote_create_one(wacom, serial, i);

		} else if (remote->remotes[i].serial) {
			wacom_remote_destroy_one(wacom, i);
		}
	}
}

static void wacom_mode_change_work(struct work_struct *work)
{
	struct wacom *wacom = container_of(work, struct wacom, mode_change_work);
	struct wacom_shared *shared = wacom->wacom_wac.shared;
	struct wacom *wacom1 = NULL;
	struct wacom *wacom2 = NULL;
	bool is_direct = wacom->wacom_wac.is_direct_mode;
	int error = 0;

	if (shared->pen) {
		wacom1 = hid_get_drvdata(shared->pen);
		wacom_release_resources(wacom1);
		hid_hw_stop(wacom1->hdev);
		wacom1->wacom_wac.has_mode_change = true;
		wacom1->wacom_wac.is_direct_mode = is_direct;
	}

	if (shared->touch) {
		wacom2 = hid_get_drvdata(shared->touch);
		wacom_release_resources(wacom2);
		hid_hw_stop(wacom2->hdev);
		wacom2->wacom_wac.has_mode_change = true;
		wacom2->wacom_wac.is_direct_mode = is_direct;
	}

	if (wacom1) {
		error = wacom_parse_and_register(wacom1, false);
		if (error)
			return;
	}

	if (wacom2) {
		error = wacom_parse_and_register(wacom2, false);
		if (error)
			return;
	}

	return;
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

	hdev->quirks |= HID_QUIRK_NO_INIT_REPORTS;

	/* hid-core sets this quirk for the boot interface */
	hdev->quirks &= ~HID_QUIRK_NOGET;

	wacom = devm_kzalloc(&hdev->dev, sizeof(struct wacom), GFP_KERNEL);
	if (!wacom)
		return -ENOMEM;

	hid_set_drvdata(hdev, wacom);
	wacom->hdev = hdev;

	wacom_wac = &wacom->wacom_wac;
	wacom_wac->features = *((struct wacom_features *)id->driver_data);
	features = &wacom_wac->features;

	if (features->check_for_hid_type && features->hid_type != hdev->type) {
		error = -ENODEV;
		goto fail;
	}

	error = kfifo_alloc(&wacom_wac->pen_fifo, WACOM_PKGLEN_MAX, GFP_KERNEL);
	if (error)
		goto fail;

	wacom_wac->hid_data.inputmode = -1;
	wacom_wac->mode_report = -1;

	wacom->usbdev = dev;
	wacom->intf = intf;
	mutex_init(&wacom->lock);
	INIT_DELAYED_WORK(&wacom->init_work, wacom_init_work);
	INIT_WORK(&wacom->wireless_work, wacom_wireless_work);
	INIT_WORK(&wacom->battery_work, wacom_battery_work);
	INIT_WORK(&wacom->remote_work, wacom_remote_work);
	INIT_WORK(&wacom->mode_change_work, wacom_mode_change_work);

	/* ask for the report descriptor to be loaded by HID */
	error = hid_parse(hdev);
	if (error) {
		hid_err(hdev, "parse failed\n");
		goto fail;
	}

	error = wacom_parse_and_register(wacom, false);
	if (error)
		goto fail;

	if (hdev->bus == BUS_BLUETOOTH) {
		error = device_create_file(&hdev->dev, &dev_attr_speed);
		if (error)
			hid_warn(hdev,
				 "can't create sysfs speed attribute err: %d\n",
				 error);
	}

	return 0;

fail:
	hid_set_drvdata(hdev, NULL);
	return error;
}

static void wacom_remove(struct hid_device *hdev)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;

	if (features->device_type & WACOM_DEVICETYPE_WL_MONITOR)
		hid_hw_close(hdev);

	hid_hw_stop(hdev);

	cancel_delayed_work_sync(&wacom->init_work);
	cancel_work_sync(&wacom->wireless_work);
	cancel_work_sync(&wacom->battery_work);
	cancel_work_sync(&wacom->remote_work);
	cancel_work_sync(&wacom->mode_change_work);
	if (hdev->bus == BUS_BLUETOOTH)
		device_remove_file(&hdev->dev, &dev_attr_speed);

	/* make sure we don't trigger the LEDs */
	wacom_led_groups_release(wacom);

	if (wacom->wacom_wac.features.type != REMOTE)
		wacom_release_resources(wacom);

	kfifo_free(&wacom_wac->pen_fifo);

	hid_set_drvdata(hdev, NULL);
}

#ifdef CONFIG_PM
static int wacom_resume(struct hid_device *hdev)
{
	struct wacom *wacom = hid_get_drvdata(hdev);

	mutex_lock(&wacom->lock);

	/* switch to wacom mode first */
	_wacom_query_tablet_data(wacom);
	wacom_led_control(wacom);

	mutex_unlock(&wacom->lock);

	return 0;
}

static int wacom_reset_resume(struct hid_device *hdev)
{
	return wacom_resume(hdev);
}
#endif /* CONFIG_PM */

static struct hid_driver wacom_driver = {
	.name =		"wacom",
	.id_table =	wacom_ids,
	.probe =	wacom_probe,
	.remove =	wacom_remove,
	.report =	wacom_wac_report,
#ifdef CONFIG_PM
	.resume =	wacom_resume,
	.reset_resume =	wacom_reset_resume,
#endif
	.raw_event =	wacom_raw_event,
};
module_hid_driver(wacom_driver);

MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
