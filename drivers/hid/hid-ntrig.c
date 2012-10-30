/*
 *  HID driver for N-Trig touchscreens
 *
 *  Copyright (c) 2008-2010 Rafi Rubin
 *  Copyright (c) 2009-2010 Stephane Chatty
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include "usbhid/usbhid.h"
#include <linux/module.h>
#include <linux/slab.h>

#include "hid-ids.h"

#define NTRIG_DUPLICATE_USAGES	0x001

static unsigned int min_width;
module_param(min_width, uint, 0644);
MODULE_PARM_DESC(min_width, "Minimum touch contact width to accept.");

static unsigned int min_height;
module_param(min_height, uint, 0644);
MODULE_PARM_DESC(min_height, "Minimum touch contact height to accept.");

static unsigned int activate_slack = 1;
module_param(activate_slack, uint, 0644);
MODULE_PARM_DESC(activate_slack, "Number of touch frames to ignore at "
		 "the start of touch input.");

static unsigned int deactivate_slack = 4;
module_param(deactivate_slack, uint, 0644);
MODULE_PARM_DESC(deactivate_slack, "Number of empty frames to ignore before "
		 "deactivating touch.");

static unsigned int activation_width = 64;
module_param(activation_width, uint, 0644);
MODULE_PARM_DESC(activation_width, "Width threshold to immediately start "
		 "processing touch events.");

static unsigned int activation_height = 32;
module_param(activation_height, uint, 0644);
MODULE_PARM_DESC(activation_height, "Height threshold to immediately start "
		 "processing touch events.");

struct ntrig_data {
	/* Incoming raw values for a single contact */
	__u16 x, y, w, h;
	__u16 id;

	bool tipswitch;
	bool confidence;
	bool first_contact_touch;

	bool reading_mt;

	__u8 mt_footer[4];
	__u8 mt_foot_count;

	/* The current activation state. */
	__s8 act_state;

	/* Empty frames to ignore before recognizing the end of activity */
	__s8 deactivate_slack;

	/* Frames to ignore before acknowledging the start of activity */
	__s8 activate_slack;

	/* Minimum size contact to accept */
	__u16 min_width;
	__u16 min_height;

	/* Threshold to override activation slack */
	__u16 activation_width;
	__u16 activation_height;

	__u16 sensor_logical_width;
	__u16 sensor_logical_height;
	__u16 sensor_physical_width;
	__u16 sensor_physical_height;
};


/*
 * This function converts the 4 byte raw firmware code into
 * a string containing 5 comma separated numbers.
 */
static int ntrig_version_string(unsigned char *raw, char *buf)
{
	__u8 a =  (raw[1] & 0x0e) >> 1;
	__u8 b =  (raw[0] & 0x3c) >> 2;
	__u8 c = ((raw[0] & 0x03) << 3) | ((raw[3] & 0xe0) >> 5);
	__u8 d = ((raw[3] & 0x07) << 3) | ((raw[2] & 0xe0) >> 5);
	__u8 e =   raw[2] & 0x07;

	/*
	 * As yet unmapped bits:
	 * 0b11000000 0b11110001 0b00011000 0b00011000
	 */

	return sprintf(buf, "%u.%u.%u.%u.%u", a, b, c, d, e);
}

static inline int ntrig_get_mode(struct hid_device *hdev)
{
	struct hid_report *report = hdev->report_enum[HID_FEATURE_REPORT].
				    report_id_hash[0x0d];

	if (!report)
		return -EINVAL;

	usbhid_submit_report(hdev, report, USB_DIR_IN);
	usbhid_wait_io(hdev);
	return (int)report->field[0]->value[0];
}

static inline void ntrig_set_mode(struct hid_device *hdev, const int mode)
{
	struct hid_report *report;
	__u8 mode_commands[4] = { 0xe, 0xf, 0x1b, 0x10 };

	if (mode < 0 || mode > 3)
		return;

	report = hdev->report_enum[HID_FEATURE_REPORT].
		 report_id_hash[mode_commands[mode]];

	if (!report)
		return;

	usbhid_submit_report(hdev, report, USB_DIR_IN);
}

static void ntrig_report_version(struct hid_device *hdev)
{
	int ret;
	char buf[20];
	struct usb_device *usb_dev = hid_to_usb_dev(hdev);
	unsigned char *data = kmalloc(8, GFP_KERNEL);

	if (!data)
		goto err_free;

	ret = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			      USB_REQ_CLEAR_FEATURE,
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE |
			      USB_DIR_IN,
			      0x30c, 1, data, 8,
			      USB_CTRL_SET_TIMEOUT);

	if (ret == 8) {
		ret = ntrig_version_string(&data[2], buf);

		hid_info(hdev, "Firmware version: %s (%02x%02x %02x%02x)\n",
			 buf, data[2], data[3], data[4], data[5]);
	}

err_free:
	kfree(data);
}

static ssize_t show_phys_width(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", nd->sensor_physical_width);
}

static DEVICE_ATTR(sensor_physical_width, S_IRUGO, show_phys_width, NULL);

static ssize_t show_phys_height(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", nd->sensor_physical_height);
}

static DEVICE_ATTR(sensor_physical_height, S_IRUGO, show_phys_height, NULL);

static ssize_t show_log_width(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", nd->sensor_logical_width);
}

static DEVICE_ATTR(sensor_logical_width, S_IRUGO, show_log_width, NULL);

static ssize_t show_log_height(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", nd->sensor_logical_height);
}

static DEVICE_ATTR(sensor_logical_height, S_IRUGO, show_log_height, NULL);

static ssize_t show_min_width(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", nd->min_width *
				    nd->sensor_physical_width /
				    nd->sensor_logical_width);
}

static ssize_t set_min_width(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if (val > nd->sensor_physical_width)
		return -EINVAL;

	nd->min_width = val * nd->sensor_logical_width /
			      nd->sensor_physical_width;

	return count;
}

static DEVICE_ATTR(min_width, S_IWUSR | S_IRUGO, show_min_width, set_min_width);

static ssize_t show_min_height(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", nd->min_height *
				    nd->sensor_physical_height /
				    nd->sensor_logical_height);
}

static ssize_t set_min_height(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if (val > nd->sensor_physical_height)
		return -EINVAL;

	nd->min_height = val * nd->sensor_logical_height /
			       nd->sensor_physical_height;

	return count;
}

static DEVICE_ATTR(min_height, S_IWUSR | S_IRUGO, show_min_height,
		   set_min_height);

static ssize_t show_activate_slack(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", nd->activate_slack);
}

static ssize_t set_activate_slack(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if (val > 0x7f)
		return -EINVAL;

	nd->activate_slack = val;

	return count;
}

static DEVICE_ATTR(activate_slack, S_IWUSR | S_IRUGO, show_activate_slack,
		   set_activate_slack);

static ssize_t show_activation_width(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", nd->activation_width *
				    nd->sensor_physical_width /
				    nd->sensor_logical_width);
}

static ssize_t set_activation_width(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if (val > nd->sensor_physical_width)
		return -EINVAL;

	nd->activation_width = val * nd->sensor_logical_width /
				     nd->sensor_physical_width;

	return count;
}

static DEVICE_ATTR(activation_width, S_IWUSR | S_IRUGO, show_activation_width,
		   set_activation_width);

static ssize_t show_activation_height(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", nd->activation_height *
				    nd->sensor_physical_height /
				    nd->sensor_logical_height);
}

static ssize_t set_activation_height(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	if (val > nd->sensor_physical_height)
		return -EINVAL;

	nd->activation_height = val * nd->sensor_logical_height /
				      nd->sensor_physical_height;

	return count;
}

static DEVICE_ATTR(activation_height, S_IWUSR | S_IRUGO,
		   show_activation_height, set_activation_height);

static ssize_t show_deactivate_slack(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	return sprintf(buf, "%d\n", -nd->deactivate_slack);
}

static ssize_t set_deactivate_slack(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	unsigned long val;

	if (strict_strtoul(buf, 0, &val))
		return -EINVAL;

	/*
	 * No more than 8 terminal frames have been observed so far
	 * and higher slack is highly likely to leave the single
	 * touch emulation stuck down.
	 */
	if (val > 7)
		return -EINVAL;

	nd->deactivate_slack = -val;

	return count;
}

static DEVICE_ATTR(deactivate_slack, S_IWUSR | S_IRUGO, show_deactivate_slack,
		   set_deactivate_slack);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_sensor_physical_width.attr,
	&dev_attr_sensor_physical_height.attr,
	&dev_attr_sensor_logical_width.attr,
	&dev_attr_sensor_logical_height.attr,
	&dev_attr_min_height.attr,
	&dev_attr_min_width.attr,
	&dev_attr_activate_slack.attr,
	&dev_attr_activation_width.attr,
	&dev_attr_activation_height.attr,
	&dev_attr_deactivate_slack.attr,
	NULL
};

static struct attribute_group ntrig_attribute_group = {
	.attrs = sysfs_attrs
};

/*
 * this driver is aimed at two firmware versions in circulation:
 *  - dual pen/finger single touch
 *  - finger multitouch, pen not working
 */

static int ntrig_input_mapping(struct hid_device *hdev, struct hid_input *hi,
			       struct hid_field *field, struct hid_usage *usage,
			       unsigned long **bit, int *max)
{
	struct ntrig_data *nd = hid_get_drvdata(hdev);

	/* No special mappings needed for the pen and single touch */
	if (field->physical)
		return 0;

	switch (usage->hid & HID_USAGE_PAGE) {
	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_X);
			input_set_abs_params(hi->input, ABS_X,
					field->logical_minimum,
					field->logical_maximum, 0, 0);

			if (!nd->sensor_logical_width) {
				nd->sensor_logical_width =
					field->logical_maximum -
					field->logical_minimum;
				nd->sensor_physical_width =
					field->physical_maximum -
					field->physical_minimum;
				nd->activation_width = activation_width *
					nd->sensor_logical_width /
					nd->sensor_physical_width;
				nd->min_width = min_width *
					nd->sensor_logical_width /
					nd->sensor_physical_width;
			}
			return 1;
		case HID_GD_Y:
			hid_map_usage(hi, usage, bit, max,
					EV_ABS, ABS_MT_POSITION_Y);
			input_set_abs_params(hi->input, ABS_Y,
					field->logical_minimum,
					field->logical_maximum, 0, 0);

			if (!nd->sensor_logical_height) {
				nd->sensor_logical_height =
					field->logical_maximum -
					field->logical_minimum;
				nd->sensor_physical_height =
					field->physical_maximum -
					field->physical_minimum;
				nd->activation_height = activation_height *
					nd->sensor_logical_height /
					nd->sensor_physical_height;
				nd->min_height = min_height *
					nd->sensor_logical_height /
					nd->sensor_physical_height;
			}
			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		/* we do not want to map these for now */
		case HID_DG_CONTACTID: /* Not trustworthy, squelch for now */
		case HID_DG_INPUTMODE:
		case HID_DG_DEVICEINDEX:
		case HID_DG_CONTACTMAX:
			return -1;

		/* width/height mapped on TouchMajor/TouchMinor/Orientation */
		case HID_DG_WIDTH:
			hid_map_usage(hi, usage, bit, max,
				      EV_ABS, ABS_MT_TOUCH_MAJOR);
			return 1;
		case HID_DG_HEIGHT:
			hid_map_usage(hi, usage, bit, max,
				      EV_ABS, ABS_MT_TOUCH_MINOR);
			input_set_abs_params(hi->input, ABS_MT_ORIENTATION,
					     0, 1, 0, 0);
			return 1;
		}
		return 0;

	case 0xff000000:
		/* we do not want to map these: no input-oriented meaning */
		return -1;
	}

	return 0;
}

static int ntrig_input_mapped(struct hid_device *hdev, struct hid_input *hi,
			      struct hid_field *field, struct hid_usage *usage,
			      unsigned long **bit, int *max)
{
	/* No special mappings needed for the pen and single touch */
	if (field->physical)
		return 0;

	if (usage->type == EV_KEY || usage->type == EV_REL
			|| usage->type == EV_ABS)
		clear_bit(usage->code, *bit);

	return 0;
}

/*
 * this function is called upon all reports
 * so that we can filter contact point information,
 * decide whether we are in multi or single touch mode
 * and call input_mt_sync after each point if necessary
 */
static int ntrig_event (struct hid_device *hid, struct hid_field *field,
			struct hid_usage *usage, __s32 value)
{
	struct ntrig_data *nd = hid_get_drvdata(hid);
	struct input_dev *input;

	/* Skip processing if not a claimed input */
	if (!(hid->claimed & HID_CLAIMED_INPUT))
		goto not_claimed_input;

	/* This function is being called before the structures are fully
	 * initialized */
	if(!(field->hidinput && field->hidinput->input))
		return -EINVAL;

	input = field->hidinput->input;

	/* No special handling needed for the pen */
	if (field->application == HID_DG_PEN)
		return 0;

	switch (usage->hid) {
	case 0xff000001:
		/* Tag indicating the start of a multitouch group */
		nd->reading_mt = 1;
		nd->first_contact_touch = 0;
		break;
	case HID_DG_TIPSWITCH:
		nd->tipswitch = value;
		/* Prevent emission of touch until validated */
		return 1;
	case HID_DG_CONFIDENCE:
		nd->confidence = value;
		break;
	case HID_GD_X:
		nd->x = value;
		/* Clear the contact footer */
		nd->mt_foot_count = 0;
		break;
	case HID_GD_Y:
		nd->y = value;
		break;
	case HID_DG_CONTACTID:
		nd->id = value;
		break;
	case HID_DG_WIDTH:
		nd->w = value;
		break;
	case HID_DG_HEIGHT:
		nd->h = value;
		/*
		 * when in single touch mode, this is the last
		 * report received in a finger event. We want
		 * to emit a normal (X, Y) position
		 */
		if (!nd->reading_mt) {
			/*
			 * TipSwitch indicates the presence of a
			 * finger in single touch mode.
			 */
			input_report_key(input, BTN_TOUCH,
					 nd->tipswitch);
			input_report_key(input, BTN_TOOL_DOUBLETAP,
					 nd->tipswitch);
			input_event(input, EV_ABS, ABS_X, nd->x);
			input_event(input, EV_ABS, ABS_Y, nd->y);
		}
		break;
	case 0xff000002:
		/*
		 * we receive this when the device is in multitouch
		 * mode. The first of the three values tagged with
		 * this usage tells if the contact point is real
		 * or a placeholder
		 */

		/* Shouldn't get more than 4 footer packets, so skip */
		if (nd->mt_foot_count >= 4)
			break;

		nd->mt_footer[nd->mt_foot_count++] = value;

		/* if the footer isn't complete break */
		if (nd->mt_foot_count != 4)
			break;

		/* Pen activity signal. */
		if (nd->mt_footer[2]) {
			/*
			 * When the pen deactivates touch, we see a
			 * bogus frame with ContactCount > 0.
			 * We can
			 * save a bit of work by ensuring act_state < 0
			 * even if deactivation slack is turned off.
			 */
			nd->act_state = deactivate_slack - 1;
			nd->confidence = 0;
			break;
		}

		/*
		 * The first footer value indicates the presence of a
		 * finger.
		 */
		if (nd->mt_footer[0]) {
			/*
			 * We do not want to process contacts under
			 * the size threshold, but do not want to
			 * ignore them for activation state
			 */
			if (nd->w < nd->min_width ||
			    nd->h < nd->min_height)
				nd->confidence = 0;
		} else
			break;

		if (nd->act_state > 0) {
			/*
			 * Contact meets the activation size threshold
			 */
			if (nd->w >= nd->activation_width &&
			    nd->h >= nd->activation_height) {
				if (nd->id)
					/*
					 * first contact, activate now
					 */
					nd->act_state = 0;
				else {
					/*
					 * avoid corrupting this frame
					 * but ensure next frame will
					 * be active
					 */
					nd->act_state = 1;
					break;
				}
			} else
				/*
				 * Defer adjusting the activation state
				 * until the end of the frame.
				 */
				break;
		}

		/* Discarding this contact */
		if (!nd->confidence)
			break;

		/* emit a normal (X, Y) for the first point only */
		if (nd->id == 0) {
			/*
			 * TipSwitch is superfluous in multitouch
			 * mode.  The footer events tell us
			 * if there is a finger on the screen or
			 * not.
			 */
			nd->first_contact_touch = nd->confidence;
			input_event(input, EV_ABS, ABS_X, nd->x);
			input_event(input, EV_ABS, ABS_Y, nd->y);
		}

		/* Emit MT events */
		input_event(input, EV_ABS, ABS_MT_POSITION_X, nd->x);
		input_event(input, EV_ABS, ABS_MT_POSITION_Y, nd->y);

		/*
		 * Translate from height and width to size
		 * and orientation.
		 */
		if (nd->w > nd->h) {
			input_event(input, EV_ABS,
					ABS_MT_ORIENTATION, 1);
			input_event(input, EV_ABS,
					ABS_MT_TOUCH_MAJOR, nd->w);
			input_event(input, EV_ABS,
					ABS_MT_TOUCH_MINOR, nd->h);
		} else {
			input_event(input, EV_ABS,
					ABS_MT_ORIENTATION, 0);
			input_event(input, EV_ABS,
					ABS_MT_TOUCH_MAJOR, nd->h);
			input_event(input, EV_ABS,
					ABS_MT_TOUCH_MINOR, nd->w);
		}
		input_mt_sync(field->hidinput->input);
		break;

	case HID_DG_CONTACTCOUNT: /* End of a multitouch group */
		if (!nd->reading_mt) /* Just to be sure */
			break;

		nd->reading_mt = 0;


		/*
		 * Activation state machine logic:
		 *
		 * Fundamental states:
		 *	state >  0: Inactive
		 *	state <= 0: Active
		 *	state <  -deactivate_slack:
		 *		 Pen termination of touch
		 *
		 * Specific values of interest
		 *	state == activate_slack
		 *		 no valid input since the last reset
		 *
		 *	state == 0
		 *		 general operational state
		 *
		 *	state == -deactivate_slack
		 *		 read sufficient empty frames to accept
		 *		 the end of input and reset
		 */

		if (nd->act_state > 0) { /* Currently inactive */
			if (value)
				/*
				 * Consider each live contact as
				 * evidence of intentional activity.
				 */
				nd->act_state = (nd->act_state > value)
						? nd->act_state - value
						: 0;
			else
				/*
				 * Empty frame before we hit the
				 * activity threshold, reset.
				 */
				nd->act_state = nd->activate_slack;

			/*
			 * Entered this block inactive and no
			 * coordinates sent this frame, so hold off
			 * on button state.
			 */
			break;
		} else { /* Currently active */
			if (value && nd->act_state >=
				     nd->deactivate_slack)
				/*
				 * Live point: clear accumulated
				 * deactivation count.
				 */
				nd->act_state = 0;
			else if (nd->act_state <= nd->deactivate_slack)
				/*
				 * We've consumed the deactivation
				 * slack, time to deactivate and reset.
				 */
				nd->act_state =
					nd->activate_slack;
			else { /* Move towards deactivation */
				nd->act_state--;
				break;
			}
		}

		if (nd->first_contact_touch && nd->act_state <= 0) {
			/*
			 * Check to see if we're ready to start
			 * emitting touch events.
			 *
			 * Note: activation slack will decrease over
			 * the course of the frame, and it will be
			 * inconsistent from the start to the end of
			 * the frame.  However if the frame starts
			 * with slack, first_contact_touch will still
			 * be 0 and we will not get to this point.
			 */
			input_report_key(input, BTN_TOOL_DOUBLETAP, 1);
			input_report_key(input, BTN_TOUCH, 1);
		} else {
			input_report_key(input, BTN_TOOL_DOUBLETAP, 0);
			input_report_key(input, BTN_TOUCH, 0);
		}
		break;

	default:
		/* fall-back to the generic hidinput handling */
		return 0;
	}

not_claimed_input:

	/* we have handled the hidinput part, now remains hiddev */
	if ((hid->claimed & HID_CLAIMED_HIDDEV) && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
}

static int ntrig_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct ntrig_data *nd;
	struct hid_input *hidinput;
	struct input_dev *input;
	struct hid_report *report;

	if (id->driver_data)
		hdev->quirks |= HID_QUIRK_MULTI_INPUT
				| HID_QUIRK_NO_INIT_REPORTS;

	nd = kmalloc(sizeof(struct ntrig_data), GFP_KERNEL);
	if (!nd) {
		hid_err(hdev, "cannot allocate N-Trig data\n");
		return -ENOMEM;
	}

	nd->reading_mt = 0;
	nd->min_width = 0;
	nd->min_height = 0;
	nd->activate_slack = activate_slack;
	nd->act_state = activate_slack;
	nd->deactivate_slack = -deactivate_slack;
	nd->sensor_logical_width = 1;
	nd->sensor_logical_height = 1;
	nd->sensor_physical_width = 1;
	nd->sensor_physical_height = 1;

	hid_set_drvdata(hdev, nd);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err_free;
	}


	list_for_each_entry(hidinput, &hdev->inputs, list) {
		if (hidinput->report->maxfield < 1)
			continue;

		input = hidinput->input;
		switch (hidinput->report->field[0]->application) {
		case HID_DG_PEN:
			input->name = "N-Trig Pen";
			break;
		case HID_DG_TOUCHSCREEN:
			/* These keys are redundant for fingers, clear them
			 * to prevent incorrect identification */
			__clear_bit(BTN_TOOL_PEN, input->keybit);
			__clear_bit(BTN_TOOL_FINGER, input->keybit);
			__clear_bit(BTN_0, input->keybit);
			__set_bit(BTN_TOOL_DOUBLETAP, input->keybit);
			/*
			 * The physical touchscreen (single touch)
			 * input has a value for physical, whereas
			 * the multitouch only has logical input
			 * fields.
			 */
			input->name =
				(hidinput->report->field[0]
				 ->physical) ?
				"N-Trig Touchscreen" :
				"N-Trig MultiTouch";
			break;
		}
	}

	/* This is needed for devices with more recent firmware versions */
	report = hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[0x0a];
	if (report) {
		/* Let the device settle to ensure the wakeup message gets
		 * through */
		usbhid_wait_io(hdev);
		usbhid_submit_report(hdev, report, USB_DIR_IN);

		/*
		 * Sanity check: if the current mode is invalid reset it to
		 * something reasonable.
		 */
		if (ntrig_get_mode(hdev) >= 4)
			ntrig_set_mode(hdev, 3);
	}

	ntrig_report_version(hdev);

	ret = sysfs_create_group(&hdev->dev.kobj,
			&ntrig_attribute_group);

	return 0;
err_free:
	kfree(nd);
	return ret;
}

static void ntrig_remove(struct hid_device *hdev)
{
	sysfs_remove_group(&hdev->dev.kobj,
			   &ntrig_attribute_group);
	hid_hw_stop(hdev);
	kfree(hid_get_drvdata(hdev));
}

static const struct hid_device_id ntrig_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_1),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_2),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_3),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_4),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_5),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_6),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_7),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_8),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_9),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_10),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_11),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_12),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_13),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_14),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_15),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_16),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_17),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ HID_USB_DEVICE(USB_VENDOR_ID_NTRIG, USB_DEVICE_ID_NTRIG_TOUCH_SCREEN_18),
		.driver_data = NTRIG_DUPLICATE_USAGES },
	{ }
};
MODULE_DEVICE_TABLE(hid, ntrig_devices);

static const struct hid_usage_id ntrig_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1 }
};

static struct hid_driver ntrig_driver = {
	.name = "ntrig",
	.id_table = ntrig_devices,
	.probe = ntrig_probe,
	.remove = ntrig_remove,
	.input_mapping = ntrig_input_mapping,
	.input_mapped = ntrig_input_mapped,
	.usage_table = ntrig_grabbed_usages,
	.event = ntrig_event,
};

static int __init ntrig_init(void)
{
	return hid_register_driver(&ntrig_driver);
}

static void __exit ntrig_exit(void)
{
	hid_unregister_driver(&ntrig_driver);
}

module_init(ntrig_init);
module_exit(ntrig_exit);
MODULE_LICENSE("GPL");
