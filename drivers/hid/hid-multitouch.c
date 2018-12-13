/*
 *  HID driver for multitouch panels
 *
 *  Copyright (c) 2010-2012 Stephane Chatty <chatty@enac.fr>
 *  Copyright (c) 2010-2013 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 *  Copyright (c) 2010-2012 Ecole Nationale de l'Aviation Civile, France
 *  Copyright (c) 2012-2013 Red Hat, Inc
 *
 *  This code is partly based on hid-egalax.c:
 *
 *  Copyright (c) 2010 Stephane Chatty <chatty@enac.fr>
 *  Copyright (c) 2010 Henrik Rydberg <rydberg@euromail.se>
 *  Copyright (c) 2010 Canonical, Ltd.
 *
 *  This code is partly based on hid-3m-pct.c:
 *
 *  Copyright (c) 2009-2010 Stephane Chatty <chatty@enac.fr>
 *  Copyright (c) 2010      Henrik Rydberg <rydberg@euromail.se>
 *  Copyright (c) 2010      Canonical, Ltd.
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * This driver is regularly tested thanks to the test suite in hid-tools[1].
 * Please run these regression tests before patching this module so that
 * your patch won't break existing known devices.
 *
 * [1] https://gitlab.freedesktop.org/libevdev/hid-tools
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input/mt.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/timer.h>


MODULE_AUTHOR("Stephane Chatty <chatty@enac.fr>");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_DESCRIPTION("HID multitouch panels");
MODULE_LICENSE("GPL");

#include "hid-ids.h"

/* quirks to control the device */
#define MT_QUIRK_NOT_SEEN_MEANS_UP	BIT(0)
#define MT_QUIRK_SLOT_IS_CONTACTID	BIT(1)
#define MT_QUIRK_CYPRESS		BIT(2)
#define MT_QUIRK_SLOT_IS_CONTACTNUMBER	BIT(3)
#define MT_QUIRK_ALWAYS_VALID		BIT(4)
#define MT_QUIRK_VALID_IS_INRANGE	BIT(5)
#define MT_QUIRK_VALID_IS_CONFIDENCE	BIT(6)
#define MT_QUIRK_CONFIDENCE		BIT(7)
#define MT_QUIRK_SLOT_IS_CONTACTID_MINUS_ONE	BIT(8)
#define MT_QUIRK_NO_AREA		BIT(9)
#define MT_QUIRK_IGNORE_DUPLICATES	BIT(10)
#define MT_QUIRK_HOVERING		BIT(11)
#define MT_QUIRK_CONTACT_CNT_ACCURATE	BIT(12)
#define MT_QUIRK_FORCE_GET_FEATURE	BIT(13)
#define MT_QUIRK_FIX_CONST_CONTACT_ID	BIT(14)
#define MT_QUIRK_TOUCH_SIZE_SCALING	BIT(15)
#define MT_QUIRK_STICKY_FINGERS		BIT(16)
#define MT_QUIRK_ASUS_CUSTOM_UP		BIT(17)
#define MT_QUIRK_WIN8_PTP_BUTTONS	BIT(18)

#define MT_INPUTMODE_TOUCHSCREEN	0x02
#define MT_INPUTMODE_TOUCHPAD		0x03

#define MT_BUTTONTYPE_CLICKPAD		0

enum latency_mode {
	HID_LATENCY_NORMAL = 0,
	HID_LATENCY_HIGH = 1,
};

#define MT_IO_FLAGS_RUNNING		0
#define MT_IO_FLAGS_ACTIVE_SLOTS	1
#define MT_IO_FLAGS_PENDING_SLOTS	2

static const bool mtrue = true;		/* default for true */
static const bool mfalse;		/* default for false */
static const __s32 mzero;		/* default for 0 */

#define DEFAULT_TRUE	((void *)&mtrue)
#define DEFAULT_FALSE	((void *)&mfalse)
#define DEFAULT_ZERO	((void *)&mzero)

struct mt_usages {
	struct list_head list;
	__s32 *x, *y, *cx, *cy, *p, *w, *h, *a;
	__s32 *contactid;	/* the device ContactID assigned to this slot */
	bool *tip_state;	/* is the touch valid? */
	bool *inrange_state;	/* is the finger in proximity of the sensor? */
	bool *confidence_state;	/* is the touch made by a finger? */
};

struct mt_application {
	struct list_head list;
	unsigned int application;
	struct list_head mt_usages;	/* mt usages list */

	__s32 quirks;

	__s32 *scantime;		/* scantime reported */
	__s32 scantime_logical_max;	/* max value for raw scantime */

	__s32 *raw_cc;			/* contact count in the report */
	int left_button_state;		/* left button state */
	unsigned int mt_flags;		/* flags to pass to input-mt */

	unsigned long *pending_palm_slots;	/* slots where we reported palm
						 * and need to release */

	__u8 num_received;	/* how many contacts we received */
	__u8 num_expected;	/* expected last contact index */
	__u8 buttons_count;	/* number of physical buttons per touchpad */
	__u8 touches_by_report;	/* how many touches are present in one report:
				 * 1 means we should use a serial protocol
				 * > 1 means hybrid (multitouch) protocol
				 */

	__s32 dev_time;		/* the scan time provided by the device */
	unsigned long jiffies;	/* the frame's jiffies */
	int timestamp;		/* the timestamp to be sent */
	int prev_scantime;		/* scantime reported previously */

	bool have_contact_count;
};

struct mt_class {
	__s32 name;	/* MT_CLS */
	__s32 quirks;
	__s32 sn_move;	/* Signal/noise ratio for move events */
	__s32 sn_width;	/* Signal/noise ratio for width events */
	__s32 sn_height;	/* Signal/noise ratio for height events */
	__s32 sn_pressure;	/* Signal/noise ratio for pressure events */
	__u8 maxcontacts;
	bool is_indirect;	/* true for touchpads */
	bool export_all_inputs;	/* do not ignore mouse, keyboards, etc... */
};

struct mt_report_data {
	struct list_head list;
	struct hid_report *report;
	struct mt_application *application;
	bool is_mt_collection;
};

struct mt_device {
	struct mt_class mtclass;	/* our mt device class */
	struct timer_list release_timer;	/* to release sticky fingers */
	struct hid_device *hdev;	/* hid_device we're attached to */
	unsigned long mt_io_flags;	/* mt flags (MT_IO_FLAGS_*) */
	__u8 inputmode_value;	/* InputMode HID feature value */
	__u8 maxcontacts;
	bool is_buttonpad;	/* is this device a button pad? */
	bool serial_maybe;	/* need to check for serial protocol */

	struct list_head applications;
	struct list_head reports;
};

static void mt_post_parse_default_settings(struct mt_device *td,
					   struct mt_application *app);
static void mt_post_parse(struct mt_device *td, struct mt_application *app);

/* classes of device behavior */
#define MT_CLS_DEFAULT				0x0001

#define MT_CLS_SERIAL				0x0002
#define MT_CLS_CONFIDENCE			0x0003
#define MT_CLS_CONFIDENCE_CONTACT_ID		0x0004
#define MT_CLS_CONFIDENCE_MINUS_ONE		0x0005
#define MT_CLS_DUAL_INRANGE_CONTACTID		0x0006
#define MT_CLS_DUAL_INRANGE_CONTACTNUMBER	0x0007
/* reserved					0x0008 */
#define MT_CLS_INRANGE_CONTACTNUMBER		0x0009
#define MT_CLS_NSMU				0x000a
/* reserved					0x0010 */
/* reserved					0x0011 */
#define MT_CLS_WIN_8				0x0012
#define MT_CLS_EXPORT_ALL_INPUTS		0x0013
#define MT_CLS_WIN_8_DUAL			0x0014

/* vendor specific classes */
#define MT_CLS_3M				0x0101
/* reserved					0x0102 */
#define MT_CLS_EGALAX				0x0103
#define MT_CLS_EGALAX_SERIAL			0x0104
#define MT_CLS_TOPSEED				0x0105
#define MT_CLS_PANASONIC			0x0106
#define MT_CLS_FLATFROG				0x0107
#define MT_CLS_GENERALTOUCH_TWOFINGERS		0x0108
#define MT_CLS_GENERALTOUCH_PWT_TENFINGERS	0x0109
#define MT_CLS_LG				0x010a
#define MT_CLS_ASUS				0x010b
#define MT_CLS_VTL				0x0110
#define MT_CLS_GOOGLE				0x0111
#define MT_CLS_RAZER_BLADE_STEALTH		0x0112

#define MT_DEFAULT_MAXCONTACT	10
#define MT_MAX_MAXCONTACT	250

/*
 * Resync device and local timestamps after that many microseconds without
 * receiving data.
 */
#define MAX_TIMESTAMP_INTERVAL	1000000

#define MT_USB_DEVICE(v, p)	HID_DEVICE(BUS_USB, HID_GROUP_MULTITOUCH, v, p)
#define MT_BT_DEVICE(v, p)	HID_DEVICE(BUS_BLUETOOTH, HID_GROUP_MULTITOUCH, v, p)

/*
 * these device-dependent functions determine what slot corresponds
 * to a valid contact that was just read.
 */

static int cypress_compute_slot(struct mt_application *application,
				struct mt_usages *slot)
{
	if (*slot->contactid != 0 || application->num_received == 0)
		return *slot->contactid;
	else
		return -1;
}

static const struct mt_class mt_classes[] = {
	{ .name = MT_CLS_DEFAULT,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_CONTACT_CNT_ACCURATE },
	{ .name = MT_CLS_NSMU,
		.quirks = MT_QUIRK_NOT_SEEN_MEANS_UP },
	{ .name = MT_CLS_SERIAL,
		.quirks = MT_QUIRK_ALWAYS_VALID},
	{ .name = MT_CLS_CONFIDENCE,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE },
	{ .name = MT_CLS_CONFIDENCE_CONTACT_ID,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE |
			MT_QUIRK_SLOT_IS_CONTACTID },
	{ .name = MT_CLS_CONFIDENCE_MINUS_ONE,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE |
			MT_QUIRK_SLOT_IS_CONTACTID_MINUS_ONE },
	{ .name = MT_CLS_DUAL_INRANGE_CONTACTID,
		.quirks = MT_QUIRK_VALID_IS_INRANGE |
			MT_QUIRK_SLOT_IS_CONTACTID,
		.maxcontacts = 2 },
	{ .name = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		.quirks = MT_QUIRK_VALID_IS_INRANGE |
			MT_QUIRK_SLOT_IS_CONTACTNUMBER,
		.maxcontacts = 2 },
	{ .name = MT_CLS_INRANGE_CONTACTNUMBER,
		.quirks = MT_QUIRK_VALID_IS_INRANGE |
			MT_QUIRK_SLOT_IS_CONTACTNUMBER },
	{ .name = MT_CLS_WIN_8,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_IGNORE_DUPLICATES |
			MT_QUIRK_HOVERING |
			MT_QUIRK_CONTACT_CNT_ACCURATE |
			MT_QUIRK_STICKY_FINGERS |
			MT_QUIRK_WIN8_PTP_BUTTONS },
	{ .name = MT_CLS_EXPORT_ALL_INPUTS,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_CONTACT_CNT_ACCURATE,
		.export_all_inputs = true },
	{ .name = MT_CLS_WIN_8_DUAL,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_IGNORE_DUPLICATES |
			MT_QUIRK_HOVERING |
			MT_QUIRK_CONTACT_CNT_ACCURATE |
			MT_QUIRK_WIN8_PTP_BUTTONS,
		.export_all_inputs = true },

	/*
	 * vendor specific classes
	 */
	{ .name = MT_CLS_3M,
		.quirks = MT_QUIRK_VALID_IS_CONFIDENCE |
			MT_QUIRK_SLOT_IS_CONTACTID |
			MT_QUIRK_TOUCH_SIZE_SCALING,
		.sn_move = 2048,
		.sn_width = 128,
		.sn_height = 128,
		.maxcontacts = 60,
	},
	{ .name = MT_CLS_EGALAX,
		.quirks =  MT_QUIRK_SLOT_IS_CONTACTID |
			MT_QUIRK_VALID_IS_INRANGE,
		.sn_move = 4096,
		.sn_pressure = 32,
	},
	{ .name = MT_CLS_EGALAX_SERIAL,
		.quirks =  MT_QUIRK_SLOT_IS_CONTACTID |
			MT_QUIRK_ALWAYS_VALID,
		.sn_move = 4096,
		.sn_pressure = 32,
	},
	{ .name = MT_CLS_TOPSEED,
		.quirks = MT_QUIRK_ALWAYS_VALID,
		.is_indirect = true,
		.maxcontacts = 2,
	},
	{ .name = MT_CLS_PANASONIC,
		.quirks = MT_QUIRK_NOT_SEEN_MEANS_UP,
		.maxcontacts = 4 },
	{ .name	= MT_CLS_GENERALTOUCH_TWOFINGERS,
		.quirks	= MT_QUIRK_NOT_SEEN_MEANS_UP |
			MT_QUIRK_VALID_IS_INRANGE |
			MT_QUIRK_SLOT_IS_CONTACTID,
		.maxcontacts = 2
	},
	{ .name	= MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		.quirks	= MT_QUIRK_NOT_SEEN_MEANS_UP |
			MT_QUIRK_SLOT_IS_CONTACTID
	},

	{ .name = MT_CLS_FLATFROG,
		.quirks = MT_QUIRK_NOT_SEEN_MEANS_UP |
			MT_QUIRK_NO_AREA,
		.sn_move = 2048,
		.maxcontacts = 40,
	},
	{ .name = MT_CLS_LG,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_FIX_CONST_CONTACT_ID |
			MT_QUIRK_IGNORE_DUPLICATES |
			MT_QUIRK_HOVERING |
			MT_QUIRK_CONTACT_CNT_ACCURATE },
	{ .name = MT_CLS_ASUS,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_CONTACT_CNT_ACCURATE |
			MT_QUIRK_ASUS_CUSTOM_UP },
	{ .name = MT_CLS_VTL,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_CONTACT_CNT_ACCURATE |
			MT_QUIRK_FORCE_GET_FEATURE,
	},
	{ .name = MT_CLS_GOOGLE,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_CONTACT_CNT_ACCURATE |
			MT_QUIRK_SLOT_IS_CONTACTID |
			MT_QUIRK_HOVERING
	},
	{ .name = MT_CLS_RAZER_BLADE_STEALTH,
		.quirks = MT_QUIRK_ALWAYS_VALID |
			MT_QUIRK_IGNORE_DUPLICATES |
			MT_QUIRK_HOVERING |
			MT_QUIRK_CONTACT_CNT_ACCURATE |
			MT_QUIRK_WIN8_PTP_BUTTONS,
	},
	{ }
};

static ssize_t mt_show_quirks(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct mt_device *td = hid_get_drvdata(hdev);

	return sprintf(buf, "%u\n", td->mtclass.quirks);
}

static ssize_t mt_set_quirks(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct mt_device *td = hid_get_drvdata(hdev);
	struct mt_application *application;

	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	td->mtclass.quirks = val;

	list_for_each_entry(application, &td->applications, list) {
		application->quirks = val;
		if (!application->have_contact_count)
			application->quirks &= ~MT_QUIRK_CONTACT_CNT_ACCURATE;
	}

	return count;
}

static DEVICE_ATTR(quirks, S_IWUSR | S_IRUGO, mt_show_quirks, mt_set_quirks);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_quirks.attr,
	NULL
};

static const struct attribute_group mt_attribute_group = {
	.attrs = sysfs_attrs
};

static void mt_get_feature(struct hid_device *hdev, struct hid_report *report)
{
	int ret;
	u32 size = hid_report_len(report);
	u8 *buf;

	/*
	 * Do not fetch the feature report if the device has been explicitly
	 * marked as non-capable.
	 */
	if (hdev->quirks & HID_QUIRK_NO_INIT_REPORTS)
		return;

	buf = hid_alloc_report_buf(report, GFP_KERNEL);
	if (!buf)
		return;

	ret = hid_hw_raw_request(hdev, report->id, buf, size,
				 HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0) {
		dev_warn(&hdev->dev, "failed to fetch feature %d\n",
			 report->id);
	} else {
		ret = hid_report_raw_event(hdev, HID_FEATURE_REPORT, buf,
					   size, 0);
		if (ret)
			dev_warn(&hdev->dev, "failed to report feature\n");
	}

	kfree(buf);
}

static void mt_feature_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct mt_device *td = hid_get_drvdata(hdev);

	switch (usage->hid) {
	case HID_DG_CONTACTMAX:
		mt_get_feature(hdev, field->report);

		td->maxcontacts = field->value[0];
		if (!td->maxcontacts &&
		    field->logical_maximum <= MT_MAX_MAXCONTACT)
			td->maxcontacts = field->logical_maximum;
		if (td->mtclass.maxcontacts)
			/* check if the maxcontacts is given by the class */
			td->maxcontacts = td->mtclass.maxcontacts;

		break;
	case HID_DG_BUTTONTYPE:
		if (usage->usage_index >= field->report_count) {
			dev_err(&hdev->dev, "HID_DG_BUTTONTYPE out of range\n");
			break;
		}

		mt_get_feature(hdev, field->report);
		if (field->value[usage->usage_index] == MT_BUTTONTYPE_CLICKPAD)
			td->is_buttonpad = true;

		break;
	case 0xff0000c5:
		/* Retrieve the Win8 blob once to enable some devices */
		if (usage->usage_index == 0)
			mt_get_feature(hdev, field->report);
		break;
	}
}

static void set_abs(struct input_dev *input, unsigned int code,
		struct hid_field *field, int snratio)
{
	int fmin = field->logical_minimum;
	int fmax = field->logical_maximum;
	int fuzz = snratio ? (fmax - fmin) / snratio : 0;
	input_set_abs_params(input, code, fmin, fmax, fuzz, 0);
	input_abs_set_res(input, code, hidinput_calc_abs_res(field, code));
}

static struct mt_usages *mt_allocate_usage(struct hid_device *hdev,
					   struct mt_application *application)
{
	struct mt_usages *usage;

	usage = devm_kzalloc(&hdev->dev, sizeof(*usage), GFP_KERNEL);
	if (!usage)
		return NULL;

	/* set some defaults so we do not need to check for null pointers */
	usage->x = DEFAULT_ZERO;
	usage->y = DEFAULT_ZERO;
	usage->cx = DEFAULT_ZERO;
	usage->cy = DEFAULT_ZERO;
	usage->p = DEFAULT_ZERO;
	usage->w = DEFAULT_ZERO;
	usage->h = DEFAULT_ZERO;
	usage->a = DEFAULT_ZERO;
	usage->contactid = DEFAULT_ZERO;
	usage->tip_state = DEFAULT_FALSE;
	usage->inrange_state = DEFAULT_FALSE;
	usage->confidence_state = DEFAULT_TRUE;

	list_add_tail(&usage->list, &application->mt_usages);

	return usage;
}

static struct mt_application *mt_allocate_application(struct mt_device *td,
						      unsigned int application)
{
	struct mt_application *mt_application;

	mt_application = devm_kzalloc(&td->hdev->dev, sizeof(*mt_application),
				      GFP_KERNEL);
	if (!mt_application)
		return NULL;

	mt_application->application = application;
	INIT_LIST_HEAD(&mt_application->mt_usages);

	if (application == HID_DG_TOUCHSCREEN)
		mt_application->mt_flags |= INPUT_MT_DIRECT;

	/*
	 * Model touchscreens providing buttons as touchpads.
	 */
	if (application == HID_DG_TOUCHPAD) {
		mt_application->mt_flags |= INPUT_MT_POINTER;
		td->inputmode_value = MT_INPUTMODE_TOUCHPAD;
	}

	mt_application->scantime = DEFAULT_ZERO;
	mt_application->raw_cc = DEFAULT_ZERO;
	mt_application->quirks = td->mtclass.quirks;

	list_add_tail(&mt_application->list, &td->applications);

	return mt_application;
}

static struct mt_application *mt_find_application(struct mt_device *td,
						  unsigned int application)
{
	struct mt_application *tmp, *mt_application = NULL;

	list_for_each_entry(tmp, &td->applications, list) {
		if (application == tmp->application) {
			mt_application = tmp;
			break;
		}
	}

	if (!mt_application)
		mt_application = mt_allocate_application(td, application);

	return mt_application;
}

static struct mt_report_data *mt_allocate_report_data(struct mt_device *td,
						      struct hid_report *report)
{
	struct mt_report_data *rdata;
	struct hid_field *field;
	int r, n;

	rdata = devm_kzalloc(&td->hdev->dev, sizeof(*rdata), GFP_KERNEL);
	if (!rdata)
		return NULL;

	rdata->report = report;
	rdata->application = mt_find_application(td, report->application);

	if (!rdata->application) {
		devm_kfree(&td->hdev->dev, rdata);
		return NULL;
	}

	for (r = 0; r < report->maxfield; r++) {
		field = report->field[r];

		if (!(HID_MAIN_ITEM_VARIABLE & field->flags))
			continue;

		for (n = 0; n < field->report_count; n++) {
			if (field->usage[n].hid == HID_DG_CONTACTID)
				rdata->is_mt_collection = true;
		}
	}

	list_add_tail(&rdata->list, &td->reports);

	return rdata;
}

static struct mt_report_data *mt_find_report_data(struct mt_device *td,
						  struct hid_report *report)
{
	struct mt_report_data *tmp, *rdata = NULL;

	list_for_each_entry(tmp, &td->reports, list) {
		if (report == tmp->report) {
			rdata = tmp;
			break;
		}
	}

	if (!rdata)
		rdata = mt_allocate_report_data(td, report);

	return rdata;
}

static void mt_store_field(struct hid_device *hdev,
			   struct mt_application *application,
			   __s32 *value,
			   size_t offset)
{
	struct mt_usages *usage;
	__s32 **target;

	if (list_empty(&application->mt_usages))
		usage = mt_allocate_usage(hdev, application);
	else
		usage = list_last_entry(&application->mt_usages,
					struct mt_usages,
					list);

	if (!usage)
		return;

	target = (__s32 **)((char *)usage + offset);

	/* the value has already been filled, create a new slot */
	if (*target != DEFAULT_TRUE &&
	    *target != DEFAULT_FALSE &&
	    *target != DEFAULT_ZERO) {
		usage = mt_allocate_usage(hdev, application);
		if (!usage)
			return;

		target = (__s32 **)((char *)usage + offset);
	}

	*target = value;
}

#define MT_STORE_FIELD(__name)						\
	mt_store_field(hdev, app,					\
		       &field->value[usage->usage_index],		\
		       offsetof(struct mt_usages, __name))

static int mt_touch_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max, struct mt_application *app)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct mt_class *cls = &td->mtclass;
	int code;
	struct hid_usage *prev_usage = NULL;

	/*
	 * Model touchscreens providing buttons as touchpads.
	 */
	if (field->application == HID_DG_TOUCHSCREEN &&
	    (usage->hid & HID_USAGE_PAGE) == HID_UP_BUTTON) {
		app->mt_flags |= INPUT_MT_POINTER;
		td->inputmode_value = MT_INPUTMODE_TOUCHPAD;
	}

	/* count the buttons on touchpads */
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_BUTTON)
		app->buttons_count++;

	if (usage->usage_index)
		prev_usage = &field->usage[usage->usage_index - 1];

	switch (usage->hid & HID_USAGE_PAGE) {

	case HID_UP_GENDESK:
		switch (usage->hid) {
		case HID_GD_X:
			if (prev_usage && (prev_usage->hid == usage->hid)) {
				code = ABS_MT_TOOL_X;
				MT_STORE_FIELD(cx);
			} else {
				code = ABS_MT_POSITION_X;
				MT_STORE_FIELD(x);
			}

			set_abs(hi->input, code, field, cls->sn_move);

			/*
			 * A system multi-axis that exports X and Y has a high
			 * chance of being used directly on a surface
			 */
			if (field->application == HID_GD_SYSTEM_MULTIAXIS) {
				__set_bit(INPUT_PROP_DIRECT,
					  hi->input->propbit);
				input_set_abs_params(hi->input,
						     ABS_MT_TOOL_TYPE,
						     MT_TOOL_DIAL,
						     MT_TOOL_DIAL, 0, 0);
			}

			return 1;
		case HID_GD_Y:
			if (prev_usage && (prev_usage->hid == usage->hid)) {
				code = ABS_MT_TOOL_Y;
				MT_STORE_FIELD(cy);
			} else {
				code = ABS_MT_POSITION_Y;
				MT_STORE_FIELD(y);
			}

			set_abs(hi->input, code, field, cls->sn_move);

			return 1;
		}
		return 0;

	case HID_UP_DIGITIZER:
		switch (usage->hid) {
		case HID_DG_INRANGE:
			if (app->quirks & MT_QUIRK_HOVERING) {
				input_set_abs_params(hi->input,
					ABS_MT_DISTANCE, 0, 1, 0, 0);
			}
			MT_STORE_FIELD(inrange_state);
			return 1;
		case HID_DG_CONFIDENCE:
			if ((cls->name == MT_CLS_WIN_8 ||
				cls->name == MT_CLS_WIN_8_DUAL) &&
				(field->application == HID_DG_TOUCHPAD ||
				 field->application == HID_DG_TOUCHSCREEN))
				app->quirks |= MT_QUIRK_CONFIDENCE;

			if (app->quirks & MT_QUIRK_CONFIDENCE)
				input_set_abs_params(hi->input,
						     ABS_MT_TOOL_TYPE,
						     MT_TOOL_FINGER,
						     MT_TOOL_PALM, 0, 0);

			MT_STORE_FIELD(confidence_state);
			return 1;
		case HID_DG_TIPSWITCH:
			if (field->application != HID_GD_SYSTEM_MULTIAXIS)
				input_set_capability(hi->input,
						     EV_KEY, BTN_TOUCH);
			MT_STORE_FIELD(tip_state);
			return 1;
		case HID_DG_CONTACTID:
			MT_STORE_FIELD(contactid);
			app->touches_by_report++;
			return 1;
		case HID_DG_WIDTH:
			if (!(app->quirks & MT_QUIRK_NO_AREA))
				set_abs(hi->input, ABS_MT_TOUCH_MAJOR, field,
					cls->sn_width);
			MT_STORE_FIELD(w);
			return 1;
		case HID_DG_HEIGHT:
			if (!(app->quirks & MT_QUIRK_NO_AREA)) {
				set_abs(hi->input, ABS_MT_TOUCH_MINOR, field,
					cls->sn_height);

				/*
				 * Only set ABS_MT_ORIENTATION if it is not
				 * already set by the HID_DG_AZIMUTH usage.
				 */
				if (!test_bit(ABS_MT_ORIENTATION,
						hi->input->absbit))
					input_set_abs_params(hi->input,
						ABS_MT_ORIENTATION, 0, 1, 0, 0);
			}
			MT_STORE_FIELD(h);
			return 1;
		case HID_DG_TIPPRESSURE:
			set_abs(hi->input, ABS_MT_PRESSURE, field,
				cls->sn_pressure);
			MT_STORE_FIELD(p);
			return 1;
		case HID_DG_SCANTIME:
			input_set_capability(hi->input, EV_MSC, MSC_TIMESTAMP);
			app->scantime = &field->value[usage->usage_index];
			app->scantime_logical_max = field->logical_maximum;
			return 1;
		case HID_DG_CONTACTCOUNT:
			app->have_contact_count = true;
			app->raw_cc = &field->value[usage->usage_index];
			return 1;
		case HID_DG_AZIMUTH:
			/*
			 * Azimuth has the range of [0, MAX) representing a full
			 * revolution. Set ABS_MT_ORIENTATION to a quarter of
			 * MAX according the definition of ABS_MT_ORIENTATION
			 */
			input_set_abs_params(hi->input, ABS_MT_ORIENTATION,
				-field->logical_maximum / 4,
				field->logical_maximum / 4,
				cls->sn_move ?
				field->logical_maximum / cls->sn_move : 0, 0);
			MT_STORE_FIELD(a);
			return 1;
		case HID_DG_CONTACTMAX:
			/* contact max are global to the report */
			return -1;
		case HID_DG_TOUCH:
			/* Legacy devices use TIPSWITCH and not TOUCH.
			 * Let's just ignore this field. */
			return -1;
		}
		/* let hid-input decide for the others */
		return 0;

	case HID_UP_BUTTON:
		code = BTN_MOUSE + ((usage->hid - 1) & HID_USAGE);
		/*
		 * MS PTP spec says that external buttons left and right have
		 * usages 2 and 3.
		 */
		if ((app->quirks & MT_QUIRK_WIN8_PTP_BUTTONS) &&
		    field->application == HID_DG_TOUCHPAD &&
		    (usage->hid & HID_USAGE) > 1)
			code--;

		if (field->application == HID_GD_SYSTEM_MULTIAXIS)
			code = BTN_0  + ((usage->hid - 1) & HID_USAGE);

		hid_map_usage(hi, usage, bit, max, EV_KEY, code);
		input_set_capability(hi->input, EV_KEY, code);
		return 1;

	case 0xff000000:
		/* we do not want to map these: no input-oriented meaning */
		return -1;
	}

	return 0;
}

static int mt_compute_slot(struct mt_device *td, struct mt_application *app,
			   struct mt_usages *slot,
			   struct input_dev *input)
{
	__s32 quirks = app->quirks;

	if (quirks & MT_QUIRK_SLOT_IS_CONTACTID)
		return *slot->contactid;

	if (quirks & MT_QUIRK_CYPRESS)
		return cypress_compute_slot(app, slot);

	if (quirks & MT_QUIRK_SLOT_IS_CONTACTNUMBER)
		return app->num_received;

	if (quirks & MT_QUIRK_SLOT_IS_CONTACTID_MINUS_ONE)
		return *slot->contactid - 1;

	return input_mt_get_slot_by_key(input, *slot->contactid);
}

static void mt_release_pending_palms(struct mt_device *td,
				     struct mt_application *app,
				     struct input_dev *input)
{
	int slotnum;
	bool need_sync = false;

	for_each_set_bit(slotnum, app->pending_palm_slots, td->maxcontacts) {
		clear_bit(slotnum, app->pending_palm_slots);

		input_mt_slot(input, slotnum);
		input_mt_report_slot_state(input, MT_TOOL_PALM, false);

		need_sync = true;
	}

	if (need_sync) {
		input_mt_sync_frame(input);
		input_sync(input);
	}
}

/*
 * this function is called when a whole packet has been received and processed,
 * so that it can decide what to send to the input layer.
 */
static void mt_sync_frame(struct mt_device *td, struct mt_application *app,
			  struct input_dev *input)
{
	if (app->quirks & MT_QUIRK_WIN8_PTP_BUTTONS)
		input_event(input, EV_KEY, BTN_LEFT, app->left_button_state);

	input_mt_sync_frame(input);
	input_event(input, EV_MSC, MSC_TIMESTAMP, app->timestamp);
	input_sync(input);

	mt_release_pending_palms(td, app, input);

	app->num_received = 0;
	app->left_button_state = 0;

	if (test_bit(MT_IO_FLAGS_ACTIVE_SLOTS, &td->mt_io_flags))
		set_bit(MT_IO_FLAGS_PENDING_SLOTS, &td->mt_io_flags);
	else
		clear_bit(MT_IO_FLAGS_PENDING_SLOTS, &td->mt_io_flags);
	clear_bit(MT_IO_FLAGS_ACTIVE_SLOTS, &td->mt_io_flags);
}

static int mt_compute_timestamp(struct mt_application *app, __s32 value)
{
	long delta = value - app->prev_scantime;
	unsigned long jdelta = jiffies_to_usecs(jiffies - app->jiffies);

	app->jiffies = jiffies;

	if (delta < 0)
		delta += app->scantime_logical_max;

	/* HID_DG_SCANTIME is expressed in 100us, we want it in us. */
	delta *= 100;

	if (jdelta > MAX_TIMESTAMP_INTERVAL)
		/* No data received for a while, resync the timestamp. */
		return 0;
	else
		return app->timestamp + delta;
}

static int mt_touch_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	/* we will handle the hidinput part later, now remains hiddev */
	if (hid->claimed & HID_CLAIMED_HIDDEV && hid->hiddev_hid_event)
		hid->hiddev_hid_event(hid, field, usage, value);

	return 1;
}

static int mt_process_slot(struct mt_device *td, struct input_dev *input,
			    struct mt_application *app,
			    struct mt_usages *slot)
{
	struct input_mt *mt = input->mt;
	__s32 quirks = app->quirks;
	bool valid = true;
	bool confidence_state = true;
	bool inrange_state = false;
	int active;
	int slotnum;
	int tool = MT_TOOL_FINGER;

	if (!slot)
		return -EINVAL;

	if ((quirks & MT_QUIRK_CONTACT_CNT_ACCURATE) &&
	    app->num_received >= app->num_expected)
		return -EAGAIN;

	if (!(quirks & MT_QUIRK_ALWAYS_VALID)) {
		if (quirks & MT_QUIRK_VALID_IS_INRANGE)
			valid = *slot->inrange_state;
		if (quirks & MT_QUIRK_NOT_SEEN_MEANS_UP)
			valid = *slot->tip_state;
		if (quirks & MT_QUIRK_VALID_IS_CONFIDENCE)
			valid = *slot->confidence_state;

		if (!valid)
			return 0;
	}

	slotnum = mt_compute_slot(td, app, slot, input);
	if (slotnum < 0 || slotnum >= td->maxcontacts)
		return 0;

	if ((quirks & MT_QUIRK_IGNORE_DUPLICATES) && mt) {
		struct input_mt_slot *i_slot = &mt->slots[slotnum];

		if (input_mt_is_active(i_slot) &&
		    input_mt_is_used(mt, i_slot))
			return -EAGAIN;
	}

	if (quirks & MT_QUIRK_CONFIDENCE)
		confidence_state = *slot->confidence_state;

	if (quirks & MT_QUIRK_HOVERING)
		inrange_state = *slot->inrange_state;

	active = *slot->tip_state || inrange_state;

	if (app->application == HID_GD_SYSTEM_MULTIAXIS)
		tool = MT_TOOL_DIAL;
	else if (unlikely(!confidence_state)) {
		tool = MT_TOOL_PALM;
		if (!active &&
		    input_mt_is_active(&mt->slots[slotnum])) {
			/*
			 * The non-confidence was reported for
			 * previously valid contact that is also no
			 * longer valid. We can't simply report
			 * lift-off as userspace will not be aware
			 * of non-confidence, so we need to split
			 * it into 2 events: active MT_TOOL_PALM
			 * and a separate liftoff.
			 */
			active = true;
			set_bit(slotnum, app->pending_palm_slots);
		}
	}

	input_mt_slot(input, slotnum);
	input_mt_report_slot_state(input, tool, active);
	if (active) {
		/* this finger is in proximity of the sensor */
		int wide = (*slot->w > *slot->h);
		int major = max(*slot->w, *slot->h);
		int minor = min(*slot->w, *slot->h);
		int orientation = wide;
		int max_azimuth;
		int azimuth;

		if (slot->a != DEFAULT_ZERO) {
			/*
			 * Azimuth is counter-clockwise and ranges from [0, MAX)
			 * (a full revolution). Convert it to clockwise ranging
			 * [-MAX/2, MAX/2].
			 *
			 * Note that ABS_MT_ORIENTATION require us to report
			 * the limit of [-MAX/4, MAX/4], but the value can go
			 * out of range to [-MAX/2, MAX/2] to report an upside
			 * down ellipsis.
			 */
			azimuth = *slot->a;
			max_azimuth = input_abs_get_max(input,
							ABS_MT_ORIENTATION);
			if (azimuth > max_azimuth * 2)
				azimuth -= max_azimuth * 4;
			orientation = -azimuth;
		}

		if (quirks & MT_QUIRK_TOUCH_SIZE_SCALING) {
			/*
			 * divided by two to match visual scale of touch
			 * for devices with this quirk
			 */
			major = major >> 1;
			minor = minor >> 1;
		}

		input_event(input, EV_ABS, ABS_MT_POSITION_X, *slot->x);
		input_event(input, EV_ABS, ABS_MT_POSITION_Y, *slot->y);
		input_event(input, EV_ABS, ABS_MT_TOOL_X, *slot->cx);
		input_event(input, EV_ABS, ABS_MT_TOOL_Y, *slot->cy);
		input_event(input, EV_ABS, ABS_MT_DISTANCE, !*slot->tip_state);
		input_event(input, EV_ABS, ABS_MT_ORIENTATION, orientation);
		input_event(input, EV_ABS, ABS_MT_PRESSURE, *slot->p);
		input_event(input, EV_ABS, ABS_MT_TOUCH_MAJOR, major);
		input_event(input, EV_ABS, ABS_MT_TOUCH_MINOR, minor);

		set_bit(MT_IO_FLAGS_ACTIVE_SLOTS, &td->mt_io_flags);
	}

	return 0;
}

static void mt_process_mt_event(struct hid_device *hid,
				struct mt_application *app,
				struct hid_field *field,
				struct hid_usage *usage,
				__s32 value,
				bool first_packet)
{
	__s32 quirks = app->quirks;
	struct input_dev *input = field->hidinput->input;

	if (!usage->type || !(hid->claimed & HID_CLAIMED_INPUT))
		return;

	if (quirks & MT_QUIRK_WIN8_PTP_BUTTONS) {

		/*
		 * For Win8 PTP touchpads we should only look at
		 * non finger/touch events in the first_packet of a
		 * (possible) multi-packet frame.
		 */
		if (!first_packet)
			return;

		/*
		 * For Win8 PTP touchpads we map both the clickpad click
		 * and any "external" left buttons to BTN_LEFT if a
		 * device claims to have both we need to report 1 for
		 * BTN_LEFT if either is pressed, so we or all values
		 * together and report the result in mt_sync_frame().
		 */
		if (usage->type == EV_KEY && usage->code == BTN_LEFT) {
			app->left_button_state |= value;
			return;
		}
	}

	input_event(input, usage->type, usage->code, value);
}

static void mt_touch_report(struct hid_device *hid,
			    struct mt_report_data *rdata)
{
	struct mt_device *td = hid_get_drvdata(hid);
	struct hid_report *report = rdata->report;
	struct mt_application *app = rdata->application;
	struct hid_field *field;
	struct input_dev *input;
	struct mt_usages *slot;
	bool first_packet;
	unsigned count;
	int r, n;
	int scantime = 0;
	int contact_count = -1;

	/* sticky fingers release in progress, abort */
	if (test_and_set_bit(MT_IO_FLAGS_RUNNING, &td->mt_io_flags))
		return;

	scantime = *app->scantime;
	app->timestamp = mt_compute_timestamp(app, scantime);
	if (app->raw_cc != DEFAULT_ZERO)
		contact_count = *app->raw_cc;

	/*
	 * Includes multi-packet support where subsequent
	 * packets are sent with zero contactcount.
	 */
	if (contact_count >= 0) {
		/*
		 * For Win8 PTPs the first packet (td->num_received == 0) may
		 * have a contactcount of 0 if there only is a button event.
		 * We double check that this is not a continuation packet
		 * of a possible multi-packet frame be checking that the
		 * timestamp has changed.
		 */
		if ((app->quirks & MT_QUIRK_WIN8_PTP_BUTTONS) &&
		    app->num_received == 0 &&
		    app->prev_scantime != scantime)
			app->num_expected = contact_count;
		/* A non 0 contact count always indicates a first packet */
		else if (contact_count)
			app->num_expected = contact_count;
	}
	app->prev_scantime = scantime;

	first_packet = app->num_received == 0;

	input = report->field[0]->hidinput->input;

	list_for_each_entry(slot, &app->mt_usages, list) {
		if (!mt_process_slot(td, input, app, slot))
			app->num_received++;
	}

	for (r = 0; r < report->maxfield; r++) {
		field = report->field[r];
		count = field->report_count;

		if (!(HID_MAIN_ITEM_VARIABLE & field->flags))
			continue;

		for (n = 0; n < count; n++)
			mt_process_mt_event(hid, app, field,
					    &field->usage[n], field->value[n],
					    first_packet);
	}

	if (app->num_received >= app->num_expected)
		mt_sync_frame(td, app, input);

	/*
	 * Windows 8 specs says 2 things:
	 * - once a contact has been reported, it has to be reported in each
	 *   subsequent report
	 * - the report rate when fingers are present has to be at least
	 *   the refresh rate of the screen, 60 or 120 Hz
	 *
	 * I interprete this that the specification forces a report rate of
	 * at least 60 Hz for a touchscreen to be certified.
	 * Which means that if we do not get a report whithin 16 ms, either
	 * something wrong happens, either the touchscreen forgets to send
	 * a release. Taking a reasonable margin allows to remove issues
	 * with USB communication or the load of the machine.
	 *
	 * Given that Win 8 devices are forced to send a release, this will
	 * only affect laggish machines and the ones that have a firmware
	 * defect.
	 */
	if (app->quirks & MT_QUIRK_STICKY_FINGERS) {
		if (test_bit(MT_IO_FLAGS_PENDING_SLOTS, &td->mt_io_flags))
			mod_timer(&td->release_timer,
				  jiffies + msecs_to_jiffies(100));
		else
			del_timer(&td->release_timer);
	}

	clear_bit(MT_IO_FLAGS_RUNNING, &td->mt_io_flags);
}

static int mt_touch_input_configured(struct hid_device *hdev,
				     struct hid_input *hi,
				     struct mt_application *app)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct mt_class *cls = &td->mtclass;
	struct input_dev *input = hi->input;
	int ret;

	if (!td->maxcontacts)
		td->maxcontacts = MT_DEFAULT_MAXCONTACT;

	mt_post_parse(td, app);
	if (td->serial_maybe)
		mt_post_parse_default_settings(td, app);

	if (cls->is_indirect)
		app->mt_flags |= INPUT_MT_POINTER;

	if (app->quirks & MT_QUIRK_NOT_SEEN_MEANS_UP)
		app->mt_flags |= INPUT_MT_DROP_UNUSED;

	/* check for clickpads */
	if ((app->mt_flags & INPUT_MT_POINTER) &&
	    (app->buttons_count == 1))
		td->is_buttonpad = true;

	if (td->is_buttonpad)
		__set_bit(INPUT_PROP_BUTTONPAD, input->propbit);

	app->pending_palm_slots = devm_kcalloc(&hi->input->dev,
					       BITS_TO_LONGS(td->maxcontacts),
					       sizeof(long),
					       GFP_KERNEL);
	if (!app->pending_palm_slots)
		return -ENOMEM;

	ret = input_mt_init_slots(input, td->maxcontacts, app->mt_flags);
	if (ret)
		return ret;

	app->mt_flags = 0;
	return 0;
}

#define mt_map_key_clear(c)	hid_map_usage_clear(hi, usage, bit, \
						    max, EV_KEY, (c))
static int mt_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct mt_application *application;
	struct mt_report_data *rdata;

	rdata = mt_find_report_data(td, field->report);
	if (!rdata) {
		hid_err(hdev, "failed to allocate data for report\n");
		return 0;
	}

	application = rdata->application;

	/*
	 * If mtclass.export_all_inputs is not set, only map fields from
	 * TouchScreen or TouchPad collections. We need to ignore fields
	 * that belong to other collections such as Mouse that might have
	 * the same GenericDesktop usages.
	 */
	if (!td->mtclass.export_all_inputs &&
	    field->application != HID_DG_TOUCHSCREEN &&
	    field->application != HID_DG_PEN &&
	    field->application != HID_DG_TOUCHPAD &&
	    field->application != HID_GD_KEYBOARD &&
	    field->application != HID_GD_SYSTEM_CONTROL &&
	    field->application != HID_CP_CONSUMER_CONTROL &&
	    field->application != HID_GD_WIRELESS_RADIO_CTLS &&
	    field->application != HID_GD_SYSTEM_MULTIAXIS &&
	    !(field->application == HID_VD_ASUS_CUSTOM_MEDIA_KEYS &&
	      application->quirks & MT_QUIRK_ASUS_CUSTOM_UP))
		return -1;

	/*
	 * Some Asus keyboard+touchpad devices have the hotkeys defined in the
	 * touchpad report descriptor. We need to treat these as an array to
	 * map usages to input keys.
	 */
	if (field->application == HID_VD_ASUS_CUSTOM_MEDIA_KEYS &&
	    application->quirks & MT_QUIRK_ASUS_CUSTOM_UP &&
	    (usage->hid & HID_USAGE_PAGE) == HID_UP_CUSTOM) {
		set_bit(EV_REP, hi->input->evbit);
		if (field->flags & HID_MAIN_ITEM_VARIABLE)
			field->flags &= ~HID_MAIN_ITEM_VARIABLE;
		switch (usage->hid & HID_USAGE) {
		case 0x10: mt_map_key_clear(KEY_BRIGHTNESSDOWN);	break;
		case 0x20: mt_map_key_clear(KEY_BRIGHTNESSUP);		break;
		case 0x35: mt_map_key_clear(KEY_DISPLAY_OFF);		break;
		case 0x6b: mt_map_key_clear(KEY_F21);			break;
		case 0x6c: mt_map_key_clear(KEY_SLEEP);			break;
		default:
			return -1;
		}
		return 1;
	}

	if (rdata->is_mt_collection)
		return mt_touch_input_mapping(hdev, hi, field, usage, bit, max,
					      application);

	/* let hid-core decide for the others */
	return 0;
}

static int mt_input_mapped(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct mt_report_data *rdata;

	rdata = mt_find_report_data(td, field->report);
	if (rdata && rdata->is_mt_collection) {
		/* We own these mappings, tell hid-input to ignore them */
		return -1;
	}

	/* let hid-core decide for the others */
	return 0;
}

static int mt_event(struct hid_device *hid, struct hid_field *field,
				struct hid_usage *usage, __s32 value)
{
	struct mt_device *td = hid_get_drvdata(hid);
	struct mt_report_data *rdata;

	rdata = mt_find_report_data(td, field->report);
	if (rdata && rdata->is_mt_collection)
		return mt_touch_event(hid, field, usage, value);

	return 0;
}

static void mt_report(struct hid_device *hid, struct hid_report *report)
{
	struct mt_device *td = hid_get_drvdata(hid);
	struct hid_field *field = report->field[0];
	struct mt_report_data *rdata;

	if (!(hid->claimed & HID_CLAIMED_INPUT))
		return;

	rdata = mt_find_report_data(td, report);
	if (rdata && rdata->is_mt_collection)
		return mt_touch_report(hid, rdata);

	if (field && field->hidinput && field->hidinput->input)
		input_sync(field->hidinput->input);
}

static bool mt_need_to_apply_feature(struct hid_device *hdev,
				     struct hid_field *field,
				     struct hid_usage *usage,
				     enum latency_mode latency,
				     bool surface_switch,
				     bool button_switch,
				     bool *inputmode_found)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	struct mt_class *cls = &td->mtclass;
	struct hid_report *report = field->report;
	unsigned int index = usage->usage_index;
	char *buf;
	u32 report_len;
	int max;

	switch (usage->hid) {
	case HID_DG_INPUTMODE:
		/*
		 * Some elan panels wrongly declare 2 input mode features,
		 * and silently ignore when we set the value in the second
		 * field. Skip the second feature and hope for the best.
		 */
		if (*inputmode_found)
			return false;

		if (cls->quirks & MT_QUIRK_FORCE_GET_FEATURE) {
			report_len = hid_report_len(report);
			buf = hid_alloc_report_buf(report, GFP_KERNEL);
			if (!buf) {
				hid_err(hdev,
					"failed to allocate buffer for report\n");
				return false;
			}
			hid_hw_raw_request(hdev, report->id, buf, report_len,
					   HID_FEATURE_REPORT,
					   HID_REQ_GET_REPORT);
			kfree(buf);
		}

		field->value[index] = td->inputmode_value;
		*inputmode_found = true;
		return true;

	case HID_DG_CONTACTMAX:
		if (cls->maxcontacts) {
			max = min_t(int, field->logical_maximum,
				    cls->maxcontacts);
			if (field->value[index] != max) {
				field->value[index] = max;
				return true;
			}
		}
		break;

	case HID_DG_LATENCYMODE:
		field->value[index] = latency;
		return true;

	case HID_DG_SURFACESWITCH:
		field->value[index] = surface_switch;
		return true;

	case HID_DG_BUTTONSWITCH:
		field->value[index] = button_switch;
		return true;
	}

	return false; /* no need to update the report */
}

static void mt_set_modes(struct hid_device *hdev, enum latency_mode latency,
			 bool surface_switch, bool button_switch)
{
	struct hid_report_enum *rep_enum;
	struct hid_report *rep;
	struct hid_usage *usage;
	int i, j;
	bool update_report;
	bool inputmode_found = false;

	rep_enum = &hdev->report_enum[HID_FEATURE_REPORT];
	list_for_each_entry(rep, &rep_enum->report_list, list) {
		update_report = false;

		for (i = 0; i < rep->maxfield; i++) {
			/* Ignore if report count is out of bounds. */
			if (rep->field[i]->report_count < 1)
				continue;

			for (j = 0; j < rep->field[i]->maxusage; j++) {
				usage = &rep->field[i]->usage[j];

				if (mt_need_to_apply_feature(hdev,
							     rep->field[i],
							     usage,
							     latency,
							     surface_switch,
							     button_switch,
							     &inputmode_found))
					update_report = true;
			}
		}

		if (update_report)
			hid_hw_request(hdev, rep, HID_REQ_SET_REPORT);
	}
}

static void mt_post_parse_default_settings(struct mt_device *td,
					   struct mt_application *app)
{
	__s32 quirks = app->quirks;

	/* unknown serial device needs special quirks */
	if (list_is_singular(&app->mt_usages)) {
		quirks |= MT_QUIRK_ALWAYS_VALID;
		quirks &= ~MT_QUIRK_NOT_SEEN_MEANS_UP;
		quirks &= ~MT_QUIRK_VALID_IS_INRANGE;
		quirks &= ~MT_QUIRK_VALID_IS_CONFIDENCE;
		quirks &= ~MT_QUIRK_CONTACT_CNT_ACCURATE;
	}

	app->quirks = quirks;
}

static void mt_post_parse(struct mt_device *td, struct mt_application *app)
{
	if (!app->have_contact_count)
		app->quirks &= ~MT_QUIRK_CONTACT_CNT_ACCURATE;
}

static int mt_input_configured(struct hid_device *hdev, struct hid_input *hi)
{
	struct mt_device *td = hid_get_drvdata(hdev);
	char *name;
	const char *suffix = NULL;
	unsigned int application = 0;
	struct mt_report_data *rdata;
	struct mt_application *mt_application = NULL;
	struct hid_report *report;
	int ret;

	list_for_each_entry(report, &hi->reports, hidinput_list) {
		application = report->application;
		rdata = mt_find_report_data(td, report);
		if (!rdata) {
			hid_err(hdev, "failed to allocate data for report\n");
			return -ENOMEM;
		}

		mt_application = rdata->application;

		if (rdata->is_mt_collection) {
			ret = mt_touch_input_configured(hdev, hi,
							mt_application);
			if (ret)
				return ret;
		}

		/*
		 * some egalax touchscreens have "application == DG_TOUCHSCREEN"
		 * for the stylus. Check this first, and then rely on
		 * the application field.
		 */
		if (report->field[0]->physical == HID_DG_STYLUS) {
			suffix = "Pen";
			/* force BTN_STYLUS to allow tablet matching in udev */
			__set_bit(BTN_STYLUS, hi->input->keybit);
		}
	}

	if (!suffix) {
		switch (application) {
		case HID_GD_KEYBOARD:
		case HID_GD_KEYPAD:
		case HID_GD_MOUSE:
		case HID_DG_TOUCHPAD:
		case HID_GD_SYSTEM_CONTROL:
		case HID_CP_CONSUMER_CONTROL:
		case HID_GD_WIRELESS_RADIO_CTLS:
		case HID_GD_SYSTEM_MULTIAXIS:
			/* already handled by hid core */
			break;
		case HID_DG_TOUCHSCREEN:
			/* we do not set suffix = "Touchscreen" */
			hi->input->name = hdev->name;
			break;
		case HID_DG_STYLUS:
			/* force BTN_STYLUS to allow tablet matching in udev */
			__set_bit(BTN_STYLUS, hi->input->keybit);
			break;
		case HID_VD_ASUS_CUSTOM_MEDIA_KEYS:
			suffix = "Custom Media Keys";
			break;
		default:
			suffix = "UNKNOWN";
			break;
		}
	}

	if (suffix) {
		name = devm_kzalloc(&hi->input->dev,
				    strlen(hdev->name) + strlen(suffix) + 2,
				    GFP_KERNEL);
		if (name) {
			sprintf(name, "%s %s", hdev->name, suffix);
			hi->input->name = name;
		}
	}

	return 0;
}

static void mt_fix_const_field(struct hid_field *field, unsigned int usage)
{
	if (field->usage[0].hid != usage ||
	    !(field->flags & HID_MAIN_ITEM_CONSTANT))
		return;

	field->flags &= ~HID_MAIN_ITEM_CONSTANT;
	field->flags |= HID_MAIN_ITEM_VARIABLE;
}

static void mt_fix_const_fields(struct hid_device *hdev, unsigned int usage)
{
	struct hid_report *report;
	int i;

	list_for_each_entry(report,
			    &hdev->report_enum[HID_INPUT_REPORT].report_list,
			    list) {

		if (!report->maxfield)
			continue;

		for (i = 0; i < report->maxfield; i++)
			if (report->field[i]->maxusage >= 1)
				mt_fix_const_field(report->field[i], usage);
	}
}

static void mt_release_contacts(struct hid_device *hid)
{
	struct hid_input *hidinput;
	struct mt_application *application;
	struct mt_device *td = hid_get_drvdata(hid);

	list_for_each_entry(hidinput, &hid->inputs, list) {
		struct input_dev *input_dev = hidinput->input;
		struct input_mt *mt = input_dev->mt;
		int i;

		if (mt) {
			for (i = 0; i < mt->num_slots; i++) {
				input_mt_slot(input_dev, i);
				input_mt_report_slot_state(input_dev,
							   MT_TOOL_FINGER,
							   false);
			}
			input_mt_sync_frame(input_dev);
			input_sync(input_dev);
		}
	}

	list_for_each_entry(application, &td->applications, list) {
		application->num_received = 0;
	}
}

static void mt_expired_timeout(struct timer_list *t)
{
	struct mt_device *td = from_timer(td, t, release_timer);
	struct hid_device *hdev = td->hdev;

	/*
	 * An input report came in just before we release the sticky fingers,
	 * it will take care of the sticky fingers.
	 */
	if (test_and_set_bit(MT_IO_FLAGS_RUNNING, &td->mt_io_flags))
		return;
	if (test_bit(MT_IO_FLAGS_PENDING_SLOTS, &td->mt_io_flags))
		mt_release_contacts(hdev);
	clear_bit(MT_IO_FLAGS_RUNNING, &td->mt_io_flags);
}

static int mt_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret, i;
	struct mt_device *td;
	const struct mt_class *mtclass = mt_classes; /* MT_CLS_DEFAULT */

	for (i = 0; mt_classes[i].name ; i++) {
		if (id->driver_data == mt_classes[i].name) {
			mtclass = &(mt_classes[i]);
			break;
		}
	}

	td = devm_kzalloc(&hdev->dev, sizeof(struct mt_device), GFP_KERNEL);
	if (!td) {
		dev_err(&hdev->dev, "cannot allocate multitouch data\n");
		return -ENOMEM;
	}
	td->hdev = hdev;
	td->mtclass = *mtclass;
	td->inputmode_value = MT_INPUTMODE_TOUCHSCREEN;
	hid_set_drvdata(hdev, td);

	INIT_LIST_HEAD(&td->applications);
	INIT_LIST_HEAD(&td->reports);

	if (id->vendor == HID_ANY_ID && id->product == HID_ANY_ID)
		td->serial_maybe = true;

	/* This allows the driver to correctly support devices
	 * that emit events over several HID messages.
	 */
	hdev->quirks |= HID_QUIRK_NO_INPUT_SYNC;

	/*
	 * This allows the driver to handle different input sensors
	 * that emits events through different applications on the same HID
	 * device.
	 */
	hdev->quirks |= HID_QUIRK_INPUT_PER_APP;

	if (id->group != HID_GROUP_MULTITOUCH_WIN_8)
		hdev->quirks |= HID_QUIRK_MULTI_INPUT;

	timer_setup(&td->release_timer, mt_expired_timeout, 0);

	ret = hid_parse(hdev);
	if (ret != 0)
		return ret;

	if (mtclass->quirks & MT_QUIRK_FIX_CONST_CONTACT_ID)
		mt_fix_const_fields(hdev, HID_DG_CONTACTID);

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret)
		return ret;

	ret = sysfs_create_group(&hdev->dev.kobj, &mt_attribute_group);
	if (ret)
		dev_warn(&hdev->dev, "Cannot allocate sysfs group for %s\n",
				hdev->name);

	mt_set_modes(hdev, HID_LATENCY_NORMAL, true, true);

	return 0;
}

#ifdef CONFIG_PM
static int mt_reset_resume(struct hid_device *hdev)
{
	mt_release_contacts(hdev);
	mt_set_modes(hdev, HID_LATENCY_NORMAL, true, true);
	return 0;
}

static int mt_resume(struct hid_device *hdev)
{
	/* Some Elan legacy devices require SET_IDLE to be set on resume.
	 * It should be safe to send it to other devices too.
	 * Tested on 3M, Stantum, Cypress, Zytronic, eGalax, and Elan panels. */

	hid_hw_idle(hdev, 0, 0, HID_REQ_SET_IDLE);

	return 0;
}
#endif

static void mt_remove(struct hid_device *hdev)
{
	struct mt_device *td = hid_get_drvdata(hdev);

	del_timer_sync(&td->release_timer);

	sysfs_remove_group(&hdev->dev.kobj, &mt_attribute_group);
	hid_hw_stop(hdev);
}

/*
 * This list contains only:
 * - VID/PID of products not working with the default multitouch handling
 * - 2 generic rules.
 * So there is no point in adding here any device with MT_CLS_DEFAULT.
 */
static const struct hid_device_id mt_devices[] = {

	/* 3M panels */
	{ .driver_data = MT_CLS_3M,
		MT_USB_DEVICE(USB_VENDOR_ID_3M,
			USB_DEVICE_ID_3M1968) },
	{ .driver_data = MT_CLS_3M,
		MT_USB_DEVICE(USB_VENDOR_ID_3M,
			USB_DEVICE_ID_3M2256) },
	{ .driver_data = MT_CLS_3M,
		MT_USB_DEVICE(USB_VENDOR_ID_3M,
			USB_DEVICE_ID_3M3266) },

	/* Alps devices */
	{ .driver_data = MT_CLS_WIN_8_DUAL,
		HID_DEVICE(BUS_I2C, HID_GROUP_MULTITOUCH_WIN_8,
			USB_VENDOR_ID_ALPS_JP,
			HID_DEVICE_ID_ALPS_U1_DUAL_PTP) },
	{ .driver_data = MT_CLS_WIN_8_DUAL,
		HID_DEVICE(BUS_I2C, HID_GROUP_MULTITOUCH_WIN_8,
			USB_VENDOR_ID_ALPS_JP,
			HID_DEVICE_ID_ALPS_U1_DUAL_3BTN_PTP) },

	/* Lenovo X1 TAB Gen 2 */
	{ .driver_data = MT_CLS_WIN_8_DUAL,
		HID_DEVICE(BUS_USB, HID_GROUP_MULTITOUCH_WIN_8,
			   USB_VENDOR_ID_LENOVO,
			   USB_DEVICE_ID_LENOVO_X1_TAB) },

	/* Anton devices */
	{ .driver_data = MT_CLS_EXPORT_ALL_INPUTS,
		MT_USB_DEVICE(USB_VENDOR_ID_ANTON,
			USB_DEVICE_ID_ANTON_TOUCH_PAD) },

	/* Asus T304UA */
	{ .driver_data = MT_CLS_ASUS,
		HID_DEVICE(BUS_USB, HID_GROUP_MULTITOUCH_WIN_8,
			USB_VENDOR_ID_ASUSTEK,
			USB_DEVICE_ID_ASUSTEK_T304_KEYBOARD) },

	/* Atmel panels */
	{ .driver_data = MT_CLS_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_ATMEL,
			USB_DEVICE_ID_ATMEL_MXT_DIGITIZER) },

	/* Baanto multitouch devices */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_BAANTO,
			USB_DEVICE_ID_BAANTO_MT_190W2) },

	/* Cando panels */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH) },
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_MULTI_TOUCH_15_6) },

	/* Chunghwa Telecom touch panels */
	{  .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_CHUNGHWAT,
			USB_DEVICE_ID_CHUNGHWAT_MULTITOUCH) },

	/* Cirque devices */
	{ .driver_data = MT_CLS_WIN_8_DUAL,
		HID_DEVICE(BUS_I2C, HID_GROUP_MULTITOUCH_WIN_8,
			I2C_VENDOR_ID_CIRQUE,
			I2C_PRODUCT_ID_CIRQUE_121F) },

	/* CJTouch panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_CJTOUCH,
			USB_DEVICE_ID_CJTOUCH_MULTI_TOUCH_0020) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_CJTOUCH,
			USB_DEVICE_ID_CJTOUCH_MULTI_TOUCH_0040) },

	/* CVTouch panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_CVTOUCH,
			USB_DEVICE_ID_CVTOUCH_SCREEN) },

	/* eGalax devices (resistive) */
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_480D) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_480E) },

	/* eGalax devices (capacitive) */
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7207) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_720C) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7224) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_722A) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_725E) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7262) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_726B) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72A1) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72AA) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72C4) },
	{ .driver_data = MT_CLS_EGALAX,
		HID_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72D0) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_72FA) },
	{ .driver_data = MT_CLS_EGALAX,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7302) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_7349) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_73F7) },
	{ .driver_data = MT_CLS_EGALAX_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_DWAV,
			USB_DEVICE_ID_DWAV_EGALAX_MULTITOUCH_A001) },

	/* Elitegroup panel */
	{ .driver_data = MT_CLS_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_ELITEGROUP,
			USB_DEVICE_ID_ELITEGROUP_05D8) },

	/* Flatfrog Panels */
	{ .driver_data = MT_CLS_FLATFROG,
		MT_USB_DEVICE(USB_VENDOR_ID_FLATFROG,
			USB_DEVICE_ID_MULTITOUCH_3200) },

	/* FocalTech Panels */
	{ .driver_data = MT_CLS_SERIAL,
		MT_USB_DEVICE(USB_VENDOR_ID_CYGNAL,
			USB_DEVICE_ID_FOCALTECH_FTXXXX_MULTITOUCH) },

	/* GeneralTouch panel */
	{ .driver_data = MT_CLS_GENERALTOUCH_TWOFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN7_TWOFINGERS) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PWT_TENFINGERS) },
	{ .driver_data = MT_CLS_GENERALTOUCH_TWOFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_0101) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_0102) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_0106) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_010A) },
	{ .driver_data = MT_CLS_GENERALTOUCH_PWT_TENFINGERS,
		MT_USB_DEVICE(USB_VENDOR_ID_GENERAL_TOUCH,
			USB_DEVICE_ID_GENERAL_TOUCH_WIN8_PIT_E100) },

	/* Gametel game controller */
	{ .driver_data = MT_CLS_NSMU,
		MT_BT_DEVICE(USB_VENDOR_ID_FRUCTEL,
			USB_DEVICE_ID_GAMETEL_MT_MODE) },

	/* GoodTouch panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_GOODTOUCH,
			USB_DEVICE_ID_GOODTOUCH_000f) },

	/* Hanvon panels */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTID,
		MT_USB_DEVICE(USB_VENDOR_ID_HANVON_ALT,
			USB_DEVICE_ID_HANVON_ALT_MULTITOUCH) },

	/* Ilitek dual touch panel */
	{  .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_ILITEK,
			USB_DEVICE_ID_ILITEK_MULTITOUCH) },

	/* LG Melfas panel */
	{ .driver_data = MT_CLS_LG,
		HID_USB_DEVICE(USB_VENDOR_ID_LG,
			USB_DEVICE_ID_LG_MELFAS_MT) },

	/* MosArt panels */
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		MT_USB_DEVICE(USB_VENDOR_ID_ASUS,
			USB_DEVICE_ID_ASUS_T91MT)},
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		MT_USB_DEVICE(USB_VENDOR_ID_ASUS,
			USB_DEVICE_ID_ASUSTEK_MULTITOUCH_YFO) },
	{ .driver_data = MT_CLS_CONFIDENCE_MINUS_ONE,
		MT_USB_DEVICE(USB_VENDOR_ID_TURBOX,
			USB_DEVICE_ID_TURBOX_TOUCHSCREEN_MOSART) },

	/* Novatek Panel */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_NOVATEK,
			USB_DEVICE_ID_NOVATEK_PCT) },

	/* Ntrig Panel */
	{ .driver_data = MT_CLS_NSMU,
		HID_DEVICE(BUS_I2C, HID_GROUP_MULTITOUCH_WIN_8,
			USB_VENDOR_ID_NTRIG, 0x1b05) },

	/* Panasonic panels */
	{ .driver_data = MT_CLS_PANASONIC,
		MT_USB_DEVICE(USB_VENDOR_ID_PANASONIC,
			USB_DEVICE_ID_PANABOARD_UBT780) },
	{ .driver_data = MT_CLS_PANASONIC,
		MT_USB_DEVICE(USB_VENDOR_ID_PANASONIC,
			USB_DEVICE_ID_PANABOARD_UBT880) },

	/* PixArt optical touch screen */
	{ .driver_data = MT_CLS_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_PIXART,
			USB_DEVICE_ID_PIXART_OPTICAL_TOUCH_SCREEN) },
	{ .driver_data = MT_CLS_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_PIXART,
			USB_DEVICE_ID_PIXART_OPTICAL_TOUCH_SCREEN1) },
	{ .driver_data = MT_CLS_INRANGE_CONTACTNUMBER,
		MT_USB_DEVICE(USB_VENDOR_ID_PIXART,
			USB_DEVICE_ID_PIXART_OPTICAL_TOUCH_SCREEN2) },

	/* PixCir-based panels */
	{ .driver_data = MT_CLS_DUAL_INRANGE_CONTACTID,
		MT_USB_DEVICE(USB_VENDOR_ID_CANDO,
			USB_DEVICE_ID_CANDO_PIXCIR_MULTI_TOUCH) },

	/* Quanta-based panels */
	{ .driver_data = MT_CLS_CONFIDENCE_CONTACT_ID,
		MT_USB_DEVICE(USB_VENDOR_ID_QUANTA,
			USB_DEVICE_ID_QUANTA_OPTICAL_TOUCH_3001) },

	/* Razer touchpads */
	{ .driver_data = MT_CLS_RAZER_BLADE_STEALTH,
		HID_DEVICE(BUS_I2C, HID_GROUP_MULTITOUCH_WIN_8,
			USB_VENDOR_ID_SYNAPTICS, 0x8323) },

	/* Stantum panels */
	{ .driver_data = MT_CLS_CONFIDENCE,
		MT_USB_DEVICE(USB_VENDOR_ID_STANTUM_STM,
			USB_DEVICE_ID_MTP_STM)},

	/* TopSeed panels */
	{ .driver_data = MT_CLS_TOPSEED,
		MT_USB_DEVICE(USB_VENDOR_ID_TOPSEED2,
			USB_DEVICE_ID_TOPSEED2_PERIPAD_701) },

	/* Touch International panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_TOUCH_INTL,
			USB_DEVICE_ID_TOUCH_INTL_MULTI_TOUCH) },

	/* Unitec panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_UNITEC,
			USB_DEVICE_ID_UNITEC_USB_TOUCH_0709) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_UNITEC,
			USB_DEVICE_ID_UNITEC_USB_TOUCH_0A19) },

	/* VTL panels */
	{ .driver_data = MT_CLS_VTL,
		MT_USB_DEVICE(USB_VENDOR_ID_VTL,
			USB_DEVICE_ID_VTL_MULTITOUCH_FF3F) },

	/* Wistron panels */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_WISTRON,
			USB_DEVICE_ID_WISTRON_OPTICAL_TOUCH) },

	/* XAT */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XAT,
			USB_DEVICE_ID_XAT_CSR) },

	/* Xiroku */
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_SPX) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_MPX) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_CSR) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_SPX1) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_MPX1) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_CSR1) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_SPX2) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_MPX2) },
	{ .driver_data = MT_CLS_NSMU,
		MT_USB_DEVICE(USB_VENDOR_ID_XIROKU,
			USB_DEVICE_ID_XIROKU_CSR2) },

	/* Google MT devices */
	{ .driver_data = MT_CLS_GOOGLE,
		HID_DEVICE(HID_BUS_ANY, HID_GROUP_ANY, USB_VENDOR_ID_GOOGLE,
			USB_DEVICE_ID_GOOGLE_TOUCH_ROSE) },

	/* Generic MT device */
	{ HID_DEVICE(HID_BUS_ANY, HID_GROUP_MULTITOUCH, HID_ANY_ID, HID_ANY_ID) },

	/* Generic Win 8 certified MT device */
	{  .driver_data = MT_CLS_WIN_8,
		HID_DEVICE(HID_BUS_ANY, HID_GROUP_MULTITOUCH_WIN_8,
			HID_ANY_ID, HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, mt_devices);

static const struct hid_usage_id mt_grabbed_usages[] = {
	{ HID_ANY_ID, HID_ANY_ID, HID_ANY_ID },
	{ HID_ANY_ID - 1, HID_ANY_ID - 1, HID_ANY_ID - 1}
};

static struct hid_driver mt_driver = {
	.name = "hid-multitouch",
	.id_table = mt_devices,
	.probe = mt_probe,
	.remove = mt_remove,
	.input_mapping = mt_input_mapping,
	.input_mapped = mt_input_mapped,
	.input_configured = mt_input_configured,
	.feature_mapping = mt_feature_mapping,
	.usage_table = mt_grabbed_usages,
	.event = mt_event,
	.report = mt_report,
#ifdef CONFIG_PM
	.reset_resume = mt_reset_resume,
	.resume = mt_resume,
#endif
};
module_hid_driver(mt_driver);
