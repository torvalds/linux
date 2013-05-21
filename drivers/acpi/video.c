/*
 *  video.c - ACPI Video Driver ($Revision:$)
 *
 *  Copyright (C) 2004 Luming Yu <luming.yu@intel.com>
 *  Copyright (C) 2004 Bruno Ducrot <ducrot@poupinou.org>
 *  Copyright (C) 2006 Thomas Tuttle <linux-kernel@ttuttle.net>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/input.h>
#include <linux/backlight.h>
#include <linux/thermal.h>
#include <linux/sort.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/dmi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <linux/suspend.h>
#include <acpi/video.h>

#define PREFIX "ACPI: "

#define ACPI_VIDEO_BUS_NAME		"Video Bus"
#define ACPI_VIDEO_DEVICE_NAME		"Video Device"
#define ACPI_VIDEO_NOTIFY_SWITCH	0x80
#define ACPI_VIDEO_NOTIFY_PROBE		0x81
#define ACPI_VIDEO_NOTIFY_CYCLE		0x82
#define ACPI_VIDEO_NOTIFY_NEXT_OUTPUT	0x83
#define ACPI_VIDEO_NOTIFY_PREV_OUTPUT	0x84

#define ACPI_VIDEO_NOTIFY_CYCLE_BRIGHTNESS	0x85
#define	ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS	0x86
#define ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS	0x87
#define ACPI_VIDEO_NOTIFY_ZERO_BRIGHTNESS	0x88
#define ACPI_VIDEO_NOTIFY_DISPLAY_OFF		0x89

#define MAX_NAME_LEN	20

#define _COMPONENT		ACPI_VIDEO_COMPONENT
ACPI_MODULE_NAME("video");

MODULE_AUTHOR("Bruno Ducrot");
MODULE_DESCRIPTION("ACPI Video Driver");
MODULE_LICENSE("GPL");

static bool brightness_switch_enabled = 1;
module_param(brightness_switch_enabled, bool, 0644);

/*
 * By default, we don't allow duplicate ACPI video bus devices
 * under the same VGA controller
 */
static bool allow_duplicates;
module_param(allow_duplicates, bool, 0644);

/*
 * Some BIOSes claim they use minimum backlight at boot,
 * and this may bring dimming screen after boot
 */
static bool use_bios_initial_backlight = 1;
module_param(use_bios_initial_backlight, bool, 0644);

static int register_count = 0;
static int acpi_video_bus_add(struct acpi_device *device);
static int acpi_video_bus_remove(struct acpi_device *device);
static void acpi_video_bus_notify(struct acpi_device *device, u32 event);

static const struct acpi_device_id video_device_ids[] = {
	{ACPI_VIDEO_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, video_device_ids);

static struct acpi_driver acpi_video_bus = {
	.name = "video",
	.class = ACPI_VIDEO_CLASS,
	.ids = video_device_ids,
	.ops = {
		.add = acpi_video_bus_add,
		.remove = acpi_video_bus_remove,
		.notify = acpi_video_bus_notify,
		},
};

struct acpi_video_bus_flags {
	u8 multihead:1;		/* can switch video heads */
	u8 rom:1;		/* can retrieve a video rom */
	u8 post:1;		/* can configure the head to */
	u8 reserved:5;
};

struct acpi_video_bus_cap {
	u8 _DOS:1;		/*Enable/Disable output switching */
	u8 _DOD:1;		/*Enumerate all devices attached to display adapter */
	u8 _ROM:1;		/*Get ROM Data */
	u8 _GPD:1;		/*Get POST Device */
	u8 _SPD:1;		/*Set POST Device */
	u8 _VPO:1;		/*Video POST Options */
	u8 reserved:2;
};

struct acpi_video_device_attrib {
	u32 display_index:4;	/* A zero-based instance of the Display */
	u32 display_port_attachment:4;	/*This field differentiates the display type */
	u32 display_type:4;	/*Describe the specific type in use */
	u32 vendor_specific:4;	/*Chipset Vendor Specific */
	u32 bios_can_detect:1;	/*BIOS can detect the device */
	u32 depend_on_vga:1;	/*Non-VGA output device whose power is related to 
				   the VGA device. */
	u32 pipe_id:3;		/*For VGA multiple-head devices. */
	u32 reserved:10;	/*Must be 0 */
	u32 device_id_scheme:1;	/*Device ID Scheme */
};

struct acpi_video_enumerated_device {
	union {
		u32 int_val;
		struct acpi_video_device_attrib attrib;
	} value;
	struct acpi_video_device *bind_info;
};

struct acpi_video_bus {
	struct acpi_device *device;
	u8 dos_setting;
	struct acpi_video_enumerated_device *attached_array;
	u8 attached_count;
	struct acpi_video_bus_cap cap;
	struct acpi_video_bus_flags flags;
	struct list_head video_device_list;
	struct mutex device_list_lock;	/* protects video_device_list */
	struct input_dev *input;
	char phys[32];	/* for input device */
	struct notifier_block pm_nb;
};

struct acpi_video_device_flags {
	u8 crt:1;
	u8 lcd:1;
	u8 tvout:1;
	u8 dvi:1;
	u8 bios:1;
	u8 unknown:1;
	u8 notify:1;
	u8 reserved:1;
};

struct acpi_video_device_cap {
	u8 _ADR:1;		/*Return the unique ID */
	u8 _BCL:1;		/*Query list of brightness control levels supported */
	u8 _BCM:1;		/*Set the brightness level */
	u8 _BQC:1;		/* Get current brightness level */
	u8 _BCQ:1;		/* Some buggy BIOS uses _BCQ instead of _BQC */
	u8 _DDC:1;		/*Return the EDID for this device */
};

struct acpi_video_brightness_flags {
	u8 _BCL_no_ac_battery_levels:1;	/* no AC/Battery levels in _BCL */
	u8 _BCL_reversed:1;		/* _BCL package is in a reversed order*/
	u8 _BCL_use_index:1;		/* levels in _BCL are index values */
	u8 _BCM_use_index:1;		/* input of _BCM is an index value */
	u8 _BQC_use_index:1;		/* _BQC returns an index value */
};

struct acpi_video_device_brightness {
	int curr;
	int count;
	int *levels;
	struct acpi_video_brightness_flags flags;
};

struct acpi_video_device {
	unsigned long device_id;
	struct acpi_video_device_flags flags;
	struct acpi_video_device_cap cap;
	struct list_head entry;
	struct acpi_video_bus *video;
	struct acpi_device *dev;
	struct acpi_video_device_brightness *brightness;
	struct backlight_device *backlight;
	struct thermal_cooling_device *cooling_dev;
};

static const char device_decode[][30] = {
	"motherboard VGA device",
	"PCI VGA device",
	"AGP VGA device",
	"UNKNOWN",
};

static void acpi_video_device_notify(acpi_handle handle, u32 event, void *data);
static void acpi_video_device_rebind(struct acpi_video_bus *video);
static void acpi_video_device_bind(struct acpi_video_bus *video,
				   struct acpi_video_device *device);
static int acpi_video_device_enumerate(struct acpi_video_bus *video);
static int acpi_video_device_lcd_set_level(struct acpi_video_device *device,
			int level);
static int acpi_video_device_lcd_get_level_current(
			struct acpi_video_device *device,
			unsigned long long *level, bool raw);
static int acpi_video_get_next_level(struct acpi_video_device *device,
				     u32 level_current, u32 event);
static int acpi_video_switch_brightness(struct acpi_video_device *device,
					 int event);

/*backlight device sysfs support*/
static int acpi_video_get_brightness(struct backlight_device *bd)
{
	unsigned long long cur_level;
	int i;
	struct acpi_video_device *vd =
		(struct acpi_video_device *)bl_get_data(bd);

	if (acpi_video_device_lcd_get_level_current(vd, &cur_level, false))
		return -EINVAL;
	for (i = 2; i < vd->brightness->count; i++) {
		if (vd->brightness->levels[i] == cur_level)
			/* The first two entries are special - see page 575
			   of the ACPI spec 3.0 */
			return i-2;
	}
	return 0;
}

static int acpi_video_set_brightness(struct backlight_device *bd)
{
	int request_level = bd->props.brightness + 2;
	struct acpi_video_device *vd =
		(struct acpi_video_device *)bl_get_data(bd);

	return acpi_video_device_lcd_set_level(vd,
				vd->brightness->levels[request_level]);
}

static const struct backlight_ops acpi_backlight_ops = {
	.get_brightness = acpi_video_get_brightness,
	.update_status  = acpi_video_set_brightness,
};

/* thermal cooling device callbacks */
static int video_get_max_state(struct thermal_cooling_device *cooling_dev, unsigned
			       long *state)
{
	struct acpi_device *device = cooling_dev->devdata;
	struct acpi_video_device *video = acpi_driver_data(device);

	*state = video->brightness->count - 3;
	return 0;
}

static int video_get_cur_state(struct thermal_cooling_device *cooling_dev, unsigned
			       long *state)
{
	struct acpi_device *device = cooling_dev->devdata;
	struct acpi_video_device *video = acpi_driver_data(device);
	unsigned long long level;
	int offset;

	if (acpi_video_device_lcd_get_level_current(video, &level, false))
		return -EINVAL;
	for (offset = 2; offset < video->brightness->count; offset++)
		if (level == video->brightness->levels[offset]) {
			*state = video->brightness->count - offset - 1;
			return 0;
		}

	return -EINVAL;
}

static int
video_set_cur_state(struct thermal_cooling_device *cooling_dev, unsigned long state)
{
	struct acpi_device *device = cooling_dev->devdata;
	struct acpi_video_device *video = acpi_driver_data(device);
	int level;

	if ( state >= video->brightness->count - 2)
		return -EINVAL;

	state = video->brightness->count - state;
	level = video->brightness->levels[state -1];
	return acpi_video_device_lcd_set_level(video, level);
}

static const struct thermal_cooling_device_ops video_cooling_ops = {
	.get_max_state = video_get_max_state,
	.get_cur_state = video_get_cur_state,
	.set_cur_state = video_set_cur_state,
};

/* --------------------------------------------------------------------------
                               Video Management
   -------------------------------------------------------------------------- */

static int
acpi_video_device_lcd_query_levels(struct acpi_video_device *device,
				   union acpi_object **levels)
{
	int status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;


	*levels = NULL;

	status = acpi_evaluate_object(device->dev->handle, "_BCL", NULL, &buffer);
	if (!ACPI_SUCCESS(status))
		return status;
	obj = (union acpi_object *)buffer.pointer;
	if (!obj || (obj->type != ACPI_TYPE_PACKAGE)) {
		printk(KERN_ERR PREFIX "Invalid _BCL data\n");
		status = -EFAULT;
		goto err;
	}

	*levels = obj;

	return 0;

      err:
	kfree(buffer.pointer);

	return status;
}

static int
acpi_video_device_lcd_set_level(struct acpi_video_device *device, int level)
{
	int status;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };
	int state;

	arg0.integer.value = level;

	status = acpi_evaluate_object(device->dev->handle, "_BCM",
				      &args, NULL);
	if (ACPI_FAILURE(status)) {
		ACPI_ERROR((AE_INFO, "Evaluating _BCM failed"));
		return -EIO;
	}

	device->brightness->curr = level;
	for (state = 2; state < device->brightness->count; state++)
		if (level == device->brightness->levels[state]) {
			if (device->backlight)
				device->backlight->props.brightness = state - 2;
			return 0;
		}

	ACPI_ERROR((AE_INFO, "Current brightness invalid"));
	return -EINVAL;
}

/*
 * For some buggy _BQC methods, we need to add a constant value to
 * the _BQC return value to get the actual current brightness level
 */

static int bqc_offset_aml_bug_workaround;
static int __init video_set_bqc_offset(const struct dmi_system_id *d)
{
	bqc_offset_aml_bug_workaround = 9;
	return 0;
}

static int video_ignore_initial_backlight(const struct dmi_system_id *d)
{
	use_bios_initial_backlight = 0;
	return 0;
}

static struct dmi_system_id video_dmi_table[] __initdata = {
	/*
	 * Broken _BQC workaround http://bugzilla.kernel.org/show_bug.cgi?id=13121
	 */
	{
	 .callback = video_set_bqc_offset,
	 .ident = "Acer Aspire 5720",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5720"),
		},
	},
	{
	 .callback = video_set_bqc_offset,
	 .ident = "Acer Aspire 5710Z",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5710Z"),
		},
	},
	{
	 .callback = video_set_bqc_offset,
	 .ident = "eMachines E510",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "EMACHINES"),
		DMI_MATCH(DMI_PRODUCT_NAME, "eMachines E510"),
		},
	},
	{
	 .callback = video_set_bqc_offset,
	 .ident = "Acer Aspire 5315",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5315"),
		},
	},
	{
	 .callback = video_set_bqc_offset,
	 .ident = "Acer Aspire 7720",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 7720"),
		},
	},
	{
	 .callback = video_ignore_initial_backlight,
	 .ident = "HP Folio 13-2000",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP Folio 13 - 2000 Notebook PC"),
		},
	},
	{
	 .callback = video_ignore_initial_backlight,
	 .ident = "HP Pavilion dm4",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP Pavilion dm4 Notebook PC"),
		},
	},
	{
	 .callback = video_ignore_initial_backlight,
	 .ident = "HP 1000 Notebook PC",
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP 1000 Notebook PC"),
		},
	},
	{}
};

static unsigned long long
acpi_video_bqc_value_to_level(struct acpi_video_device *device,
			      unsigned long long bqc_value)
{
	unsigned long long level;

	if (device->brightness->flags._BQC_use_index) {
		/*
		 * _BQC returns an index that doesn't account for
		 * the first 2 items with special meaning, so we need
		 * to compensate for that by offsetting ourselves
		 */
		if (device->brightness->flags._BCL_reversed)
			bqc_value = device->brightness->count - 3 - bqc_value;

		level = device->brightness->levels[bqc_value + 2];
	} else {
		level = bqc_value;
	}

	level += bqc_offset_aml_bug_workaround;

	return level;
}

static int
acpi_video_device_lcd_get_level_current(struct acpi_video_device *device,
					unsigned long long *level, bool raw)
{
	acpi_status status = AE_OK;
	int i;

	if (device->cap._BQC || device->cap._BCQ) {
		char *buf = device->cap._BQC ? "_BQC" : "_BCQ";

		status = acpi_evaluate_integer(device->dev->handle, buf,
						NULL, level);
		if (ACPI_SUCCESS(status)) {
			if (raw) {
				/*
				 * Caller has indicated he wants the raw
				 * value returned by _BQC, so don't furtherly
				 * mess with the value.
				 */
				return 0;
			}

			*level = acpi_video_bqc_value_to_level(device, *level);

			for (i = 2; i < device->brightness->count; i++)
				if (device->brightness->levels[i] == *level) {
					device->brightness->curr = *level;
					return 0;
			}
			/*
			 * BQC returned an invalid level.
			 * Stop using it.
			 */
			ACPI_WARNING((AE_INFO,
				      "%s returned an invalid level",
				      buf));
			device->cap._BQC = device->cap._BCQ = 0;
		} else {
			/* Fixme:
			 * should we return an error or ignore this failure?
			 * dev->brightness->curr is a cached value which stores
			 * the correct current backlight level in most cases.
			 * ACPI video backlight still works w/ buggy _BQC.
			 * http://bugzilla.kernel.org/show_bug.cgi?id=12233
			 */
			ACPI_WARNING((AE_INFO, "Evaluating %s failed", buf));
			device->cap._BQC = device->cap._BCQ = 0;
		}
	}

	*level = device->brightness->curr;
	return 0;
}

static int
acpi_video_device_EDID(struct acpi_video_device *device,
		       union acpi_object **edid, ssize_t length)
{
	int status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };


	*edid = NULL;

	if (!device)
		return -ENODEV;
	if (length == 128)
		arg0.integer.value = 1;
	else if (length == 256)
		arg0.integer.value = 2;
	else
		return -EINVAL;

	status = acpi_evaluate_object(device->dev->handle, "_DDC", &args, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	obj = buffer.pointer;

	if (obj && obj->type == ACPI_TYPE_BUFFER)
		*edid = obj;
	else {
		printk(KERN_ERR PREFIX "Invalid _DDC data\n");
		status = -EFAULT;
		kfree(obj);
	}

	return status;
}

/* bus */

/*
 *  Arg:
 *  	video		: video bus device pointer
 *	bios_flag	: 
 *		0.	The system BIOS should NOT automatically switch(toggle)
 *			the active display output.
 *		1.	The system BIOS should automatically switch (toggle) the
 *			active display output. No switch event.
 *		2.	The _DGS value should be locked.
 *		3.	The system BIOS should not automatically switch (toggle) the
 *			active display output, but instead generate the display switch
 *			event notify code.
 *	lcd_flag	:
 *		0.	The system BIOS should automatically control the brightness level
 *			of the LCD when the power changes from AC to DC
 *		1. 	The system BIOS should NOT automatically control the brightness 
 *			level of the LCD when the power changes from AC to DC.
 * Return Value:
 *		-EINVAL	wrong arg.
 */

static int
acpi_video_bus_DOS(struct acpi_video_bus *video, int bios_flag, int lcd_flag)
{
	acpi_status status;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };

	if (!video->cap._DOS)
		return 0;

	if (bios_flag < 0 || bios_flag > 3 || lcd_flag < 0 || lcd_flag > 1)
		return -EINVAL;
	arg0.integer.value = (lcd_flag << 2) | bios_flag;
	video->dos_setting = arg0.integer.value;
	status = acpi_evaluate_object(video->device->handle, "_DOS",
		&args, NULL);
	if (ACPI_FAILURE(status))
		return -EIO;

	return 0;
}

/*
 * Simple comparison function used to sort backlight levels.
 */

static int
acpi_video_cmp_level(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

/*
 * Decides if _BQC/_BCQ for this system is usable
 *
 * We do this by changing the level first and then read out the current
 * brightness level, if the value does not match, find out if it is using
 * index. If not, clear the _BQC/_BCQ capability.
 */
static int acpi_video_bqc_quirk(struct acpi_video_device *device,
				int max_level, int current_level)
{
	struct acpi_video_device_brightness *br = device->brightness;
	int result;
	unsigned long long level;
	int test_level;

	/* don't mess with existing known broken systems */
	if (bqc_offset_aml_bug_workaround)
		return 0;

	/*
	 * Some systems always report current brightness level as maximum
	 * through _BQC, we need to test another value for them.
	 */
	test_level = current_level == max_level ? br->levels[2] : max_level;

	result = acpi_video_device_lcd_set_level(device, test_level);
	if (result)
		return result;

	result = acpi_video_device_lcd_get_level_current(device, &level, true);
	if (result)
		return result;

	if (level != test_level) {
		/* buggy _BQC found, need to find out if it uses index */
		if (level < br->count) {
			if (br->flags._BCL_reversed)
				level = br->count - 3 - level;
			if (br->levels[level + 2] == test_level)
				br->flags._BQC_use_index = 1;
		}

		if (!br->flags._BQC_use_index)
			device->cap._BQC = device->cap._BCQ = 0;
	}

	return 0;
}


/*
 *  Arg:	
 *  	device	: video output device (LCD, CRT, ..)
 *
 *  Return Value:
 *	Maximum brightness level
 *
 *  Allocate and initialize device->brightness.
 */

static int
acpi_video_init_brightness(struct acpi_video_device *device)
{
	union acpi_object *obj = NULL;
	int i, max_level = 0, count = 0, level_ac_battery = 0;
	unsigned long long level, level_old;
	union acpi_object *o;
	struct acpi_video_device_brightness *br = NULL;
	int result = -EINVAL;

	if (!ACPI_SUCCESS(acpi_video_device_lcd_query_levels(device, &obj))) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Could not query available "
						"LCD brightness level\n"));
		goto out;
	}

	if (obj->package.count < 2)
		goto out;

	br = kzalloc(sizeof(*br), GFP_KERNEL);
	if (!br) {
		printk(KERN_ERR "can't allocate memory\n");
		result = -ENOMEM;
		goto out;
	}

	br->levels = kmalloc((obj->package.count + 2) * sizeof *(br->levels),
				GFP_KERNEL);
	if (!br->levels) {
		result = -ENOMEM;
		goto out_free;
	}

	for (i = 0; i < obj->package.count; i++) {
		o = (union acpi_object *)&obj->package.elements[i];
		if (o->type != ACPI_TYPE_INTEGER) {
			printk(KERN_ERR PREFIX "Invalid data\n");
			continue;
		}
		br->levels[count] = (u32) o->integer.value;

		if (br->levels[count] > max_level)
			max_level = br->levels[count];
		count++;
	}

	/*
	 * some buggy BIOS don't export the levels
	 * when machine is on AC/Battery in _BCL package.
	 * In this case, the first two elements in _BCL packages
	 * are also supported brightness levels that OS should take care of.
	 */
	for (i = 2; i < count; i++) {
		if (br->levels[i] == br->levels[0])
			level_ac_battery++;
		if (br->levels[i] == br->levels[1])
			level_ac_battery++;
	}

	if (level_ac_battery < 2) {
		level_ac_battery = 2 - level_ac_battery;
		br->flags._BCL_no_ac_battery_levels = 1;
		for (i = (count - 1 + level_ac_battery); i >= 2; i--)
			br->levels[i] = br->levels[i - level_ac_battery];
		count += level_ac_battery;
	} else if (level_ac_battery > 2)
		ACPI_ERROR((AE_INFO, "Too many duplicates in _BCL package"));

	/* Check if the _BCL package is in a reversed order */
	if (max_level == br->levels[2]) {
		br->flags._BCL_reversed = 1;
		sort(&br->levels[2], count - 2, sizeof(br->levels[2]),
			acpi_video_cmp_level, NULL);
	} else if (max_level != br->levels[count - 1])
		ACPI_ERROR((AE_INFO,
			    "Found unordered _BCL package"));

	br->count = count;
	device->brightness = br;

	/* Check the input/output of _BQC/_BCL/_BCM */
	if ((max_level < 100) && (max_level <= (count - 2)))
		br->flags._BCL_use_index = 1;

	/*
	 * _BCM is always consistent with _BCL,
	 * at least for all the laptops we have ever seen.
	 */
	br->flags._BCM_use_index = br->flags._BCL_use_index;

	/* _BQC uses INDEX while _BCL uses VALUE in some laptops */
	br->curr = level = max_level;

	if (!device->cap._BQC)
		goto set_level;

	result = acpi_video_device_lcd_get_level_current(device,
							 &level_old, true);
	if (result)
		goto out_free_levels;

	result = acpi_video_bqc_quirk(device, max_level, level_old);
	if (result)
		goto out_free_levels;
	/*
	 * cap._BQC may get cleared due to _BQC is found to be broken
	 * in acpi_video_bqc_quirk, so check again here.
	 */
	if (!device->cap._BQC)
		goto set_level;

	if (use_bios_initial_backlight) {
		level = acpi_video_bqc_value_to_level(device, level_old);
		/*
		 * On some buggy laptops, _BQC returns an uninitialized
		 * value when invoked for the first time, i.e.
		 * level_old is invalid (no matter whether it's a level
		 * or an index). Set the backlight to max_level in this case.
		 */
		for (i = 2; i < br->count; i++)
			if (level_old == br->levels[i])
				break;
		if (i == br->count)
			level = max_level;
	}

set_level:
	result = acpi_video_device_lcd_set_level(device, level);
	if (result)
		goto out_free_levels;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "found %d brightness levels\n", count - 2));
	kfree(obj);
	return result;

out_free_levels:
	kfree(br->levels);
out_free:
	kfree(br);
out:
	device->brightness = NULL;
	kfree(obj);
	return result;
}

/*
 *  Arg:
 *	device	: video output device (LCD, CRT, ..)
 *
 *  Return Value:
 *  	None
 *
 *  Find out all required AML methods defined under the output
 *  device.
 */

static void acpi_video_device_find_cap(struct acpi_video_device *device)
{
	acpi_handle h_dummy1;

	if (ACPI_SUCCESS(acpi_get_handle(device->dev->handle, "_ADR", &h_dummy1))) {
		device->cap._ADR = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->dev->handle, "_BCL", &h_dummy1))) {
		device->cap._BCL = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->dev->handle, "_BCM", &h_dummy1))) {
		device->cap._BCM = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->dev->handle,"_BQC",&h_dummy1)))
		device->cap._BQC = 1;
	else if (ACPI_SUCCESS(acpi_get_handle(device->dev->handle, "_BCQ",
				&h_dummy1))) {
		printk(KERN_WARNING FW_BUG "_BCQ is used instead of _BQC\n");
		device->cap._BCQ = 1;
	}

	if (ACPI_SUCCESS(acpi_get_handle(device->dev->handle, "_DDC", &h_dummy1))) {
		device->cap._DDC = 1;
	}

	if (acpi_video_backlight_support()) {
		struct backlight_properties props;
		struct pci_dev *pdev;
		acpi_handle acpi_parent;
		struct device *parent = NULL;
		int result;
		static int count = 0;
		char *name;

		result = acpi_video_init_brightness(device);
		if (result)
			return;
		name = kasprintf(GFP_KERNEL, "acpi_video%d", count);
		if (!name)
			return;
		count++;

		acpi_get_parent(device->dev->handle, &acpi_parent);

		pdev = acpi_get_pci_dev(acpi_parent);
		if (pdev) {
			parent = &pdev->dev;
			pci_dev_put(pdev);
		}

		memset(&props, 0, sizeof(struct backlight_properties));
		props.type = BACKLIGHT_FIRMWARE;
		props.max_brightness = device->brightness->count - 3;
		device->backlight = backlight_device_register(name,
							      parent,
							      device,
							      &acpi_backlight_ops,
							      &props);
		kfree(name);
		if (IS_ERR(device->backlight))
			return;

		/*
		 * Save current brightness level in case we have to restore it
		 * before acpi_video_device_lcd_set_level() is called next time.
		 */
		device->backlight->props.brightness =
				acpi_video_get_brightness(device->backlight);

		device->cooling_dev = thermal_cooling_device_register("LCD",
					device->dev, &video_cooling_ops);
		if (IS_ERR(device->cooling_dev)) {
			/*
			 * Set cooling_dev to NULL so we don't crash trying to
			 * free it.
			 * Also, why the hell we are returning early and
			 * not attempt to register video output if cooling
			 * device registration failed?
			 * -- dtor
			 */
			device->cooling_dev = NULL;
			return;
		}

		dev_info(&device->dev->dev, "registered as cooling_device%d\n",
			 device->cooling_dev->id);
		result = sysfs_create_link(&device->dev->dev.kobj,
				&device->cooling_dev->device.kobj,
				"thermal_cooling");
		if (result)
			printk(KERN_ERR PREFIX "Create sysfs link\n");
		result = sysfs_create_link(&device->cooling_dev->device.kobj,
				&device->dev->dev.kobj, "device");
		if (result)
			printk(KERN_ERR PREFIX "Create sysfs link\n");

	}
}

/*
 *  Arg:	
 *  	device	: video output device (VGA)
 *
 *  Return Value:
 *  	None
 *
 *  Find out all required AML methods defined under the video bus device.
 */

static void acpi_video_bus_find_cap(struct acpi_video_bus *video)
{
	acpi_handle h_dummy1;

	if (ACPI_SUCCESS(acpi_get_handle(video->device->handle, "_DOS", &h_dummy1))) {
		video->cap._DOS = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->device->handle, "_DOD", &h_dummy1))) {
		video->cap._DOD = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->device->handle, "_ROM", &h_dummy1))) {
		video->cap._ROM = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->device->handle, "_GPD", &h_dummy1))) {
		video->cap._GPD = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->device->handle, "_SPD", &h_dummy1))) {
		video->cap._SPD = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->device->handle, "_VPO", &h_dummy1))) {
		video->cap._VPO = 1;
	}
}

/*
 * Check whether the video bus device has required AML method to
 * support the desired features
 */

static int acpi_video_bus_check(struct acpi_video_bus *video)
{
	acpi_status status = -ENOENT;
	struct pci_dev *dev;

	if (!video)
		return -EINVAL;

	dev = acpi_get_pci_dev(video->device->handle);
	if (!dev)
		return -ENODEV;
	pci_dev_put(dev);

	/* Since there is no HID, CID and so on for VGA driver, we have
	 * to check well known required nodes.
	 */

	/* Does this device support video switching? */
	if (video->cap._DOS || video->cap._DOD) {
		if (!video->cap._DOS) {
			printk(KERN_WARNING FW_BUG
				"ACPI(%s) defines _DOD but not _DOS\n",
				acpi_device_bid(video->device));
		}
		video->flags.multihead = 1;
		status = 0;
	}

	/* Does this device support retrieving a video ROM? */
	if (video->cap._ROM) {
		video->flags.rom = 1;
		status = 0;
	}

	/* Does this device support configuring which video device to POST? */
	if (video->cap._GPD && video->cap._SPD && video->cap._VPO) {
		video->flags.post = 1;
		status = 0;
	}

	return status;
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

/* device interface */
static struct acpi_video_device_attrib*
acpi_video_get_device_attr(struct acpi_video_bus *video, unsigned long device_id)
{
	struct acpi_video_enumerated_device *ids;
	int i;

	for (i = 0; i < video->attached_count; i++) {
		ids = &video->attached_array[i];
		if ((ids->value.int_val & 0xffff) == device_id)
			return &ids->value.attrib;
	}

	return NULL;
}

static int
acpi_video_get_device_type(struct acpi_video_bus *video,
			   unsigned long device_id)
{
	struct acpi_video_enumerated_device *ids;
	int i;

	for (i = 0; i < video->attached_count; i++) {
		ids = &video->attached_array[i];
		if ((ids->value.int_val & 0xffff) == device_id)
			return ids->value.int_val;
	}

	return 0;
}

static int
acpi_video_bus_get_one_device(struct acpi_device *device,
			      struct acpi_video_bus *video)
{
	unsigned long long device_id;
	int status, device_type;
	struct acpi_video_device *data;
	struct acpi_video_device_attrib* attribute;

	status =
	    acpi_evaluate_integer(device->handle, "_ADR", NULL, &device_id);
	/* Some device omits _ADR, we skip them instead of fail */
	if (ACPI_FAILURE(status))
		return 0;

	data = kzalloc(sizeof(struct acpi_video_device), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	strcpy(acpi_device_name(device), ACPI_VIDEO_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_VIDEO_CLASS);
	device->driver_data = data;

	data->device_id = device_id;
	data->video = video;
	data->dev = device;

	attribute = acpi_video_get_device_attr(video, device_id);

	if((attribute != NULL) && attribute->device_id_scheme) {
		switch (attribute->display_type) {
		case ACPI_VIDEO_DISPLAY_CRT:
			data->flags.crt = 1;
			break;
		case ACPI_VIDEO_DISPLAY_TV:
			data->flags.tvout = 1;
			break;
		case ACPI_VIDEO_DISPLAY_DVI:
			data->flags.dvi = 1;
			break;
		case ACPI_VIDEO_DISPLAY_LCD:
			data->flags.lcd = 1;
			break;
		default:
			data->flags.unknown = 1;
			break;
		}
		if(attribute->bios_can_detect)
			data->flags.bios = 1;
	} else {
		/* Check for legacy IDs */
		device_type = acpi_video_get_device_type(video, device_id);
		/* Ignore bits 16 and 18-20 */
		switch (device_type & 0xffe2ffff) {
			case ACPI_VIDEO_DISPLAY_LEGACY_MONITOR:
				data->flags.crt = 1;
				break;
			case ACPI_VIDEO_DISPLAY_LEGACY_PANEL:
				data->flags.lcd = 1;
				break;
			case ACPI_VIDEO_DISPLAY_LEGACY_TV:
				data->flags.tvout = 1;
				break;
			default:
				data->flags.unknown = 1;
		}
	}

	acpi_video_device_bind(video, data);
	acpi_video_device_find_cap(data);

	status = acpi_install_notify_handler(device->handle, ACPI_DEVICE_NOTIFY,
					     acpi_video_device_notify, data);
	if (ACPI_FAILURE(status))
		dev_err(&device->dev, "Error installing notify handler\n");
	else
		data->flags.notify = 1;

	mutex_lock(&video->device_list_lock);
	list_add_tail(&data->entry, &video->video_device_list);
	mutex_unlock(&video->device_list_lock);

	return status;
}

/*
 *  Arg:
 *  	video	: video bus device 
 *
 *  Return:
 *  	none
 *  
 *  Enumerate the video device list of the video bus, 
 *  bind the ids with the corresponding video devices
 *  under the video bus.
 */

static void acpi_video_device_rebind(struct acpi_video_bus *video)
{
	struct acpi_video_device *dev;

	mutex_lock(&video->device_list_lock);

	list_for_each_entry(dev, &video->video_device_list, entry)
		acpi_video_device_bind(video, dev);

	mutex_unlock(&video->device_list_lock);
}

/*
 *  Arg:
 *  	video	: video bus device 
 *  	device	: video output device under the video 
 *  		bus
 *
 *  Return:
 *  	none
 *  
 *  Bind the ids with the corresponding video devices
 *  under the video bus.
 */

static void
acpi_video_device_bind(struct acpi_video_bus *video,
		       struct acpi_video_device *device)
{
	struct acpi_video_enumerated_device *ids;
	int i;

	for (i = 0; i < video->attached_count; i++) {
		ids = &video->attached_array[i];
		if (device->device_id == (ids->value.int_val & 0xffff)) {
			ids->bind_info = device;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "device_bind %d\n", i));
		}
	}
}

/*
 *  Arg:
 *  	video	: video bus device 
 *
 *  Return:
 *  	< 0	: error
 *  
 *  Call _DOD to enumerate all devices attached to display adapter
 *
 */

static int acpi_video_device_enumerate(struct acpi_video_bus *video)
{
	int status;
	int count;
	int i;
	struct acpi_video_enumerated_device *active_list;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *dod = NULL;
	union acpi_object *obj;

	status = acpi_evaluate_object(video->device->handle, "_DOD", NULL, &buffer);
	if (!ACPI_SUCCESS(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _DOD"));
		return status;
	}

	dod = buffer.pointer;
	if (!dod || (dod->type != ACPI_TYPE_PACKAGE)) {
		ACPI_EXCEPTION((AE_INFO, status, "Invalid _DOD data"));
		status = -EFAULT;
		goto out;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %d video heads in _DOD\n",
			  dod->package.count));

	active_list = kcalloc(1 + dod->package.count,
			      sizeof(struct acpi_video_enumerated_device),
			      GFP_KERNEL);
	if (!active_list) {
		status = -ENOMEM;
		goto out;
	}

	count = 0;
	for (i = 0; i < dod->package.count; i++) {
		obj = &dod->package.elements[i];

		if (obj->type != ACPI_TYPE_INTEGER) {
			printk(KERN_ERR PREFIX
				"Invalid _DOD data in element %d\n", i);
			continue;
		}

		active_list[count].value.int_val = obj->integer.value;
		active_list[count].bind_info = NULL;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "dod element[%d] = %d\n", i,
				  (int)obj->integer.value));
		count++;
	}

	kfree(video->attached_array);

	video->attached_array = active_list;
	video->attached_count = count;

 out:
	kfree(buffer.pointer);
	return status;
}

static int
acpi_video_get_next_level(struct acpi_video_device *device,
			  u32 level_current, u32 event)
{
	int min, max, min_above, max_below, i, l, delta = 255;
	max = max_below = 0;
	min = min_above = 255;
	/* Find closest level to level_current */
	for (i = 2; i < device->brightness->count; i++) {
		l = device->brightness->levels[i];
		if (abs(l - level_current) < abs(delta)) {
			delta = l - level_current;
			if (!delta)
				break;
		}
	}
	/* Ajust level_current to closest available level */
	level_current += delta;
	for (i = 2; i < device->brightness->count; i++) {
		l = device->brightness->levels[i];
		if (l < min)
			min = l;
		if (l > max)
			max = l;
		if (l < min_above && l > level_current)
			min_above = l;
		if (l > max_below && l < level_current)
			max_below = l;
	}

	switch (event) {
	case ACPI_VIDEO_NOTIFY_CYCLE_BRIGHTNESS:
		return (level_current < max) ? min_above : min;
	case ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS:
		return (level_current < max) ? min_above : max;
	case ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS:
		return (level_current > min) ? max_below : min;
	case ACPI_VIDEO_NOTIFY_ZERO_BRIGHTNESS:
	case ACPI_VIDEO_NOTIFY_DISPLAY_OFF:
		return 0;
	default:
		return level_current;
	}
}

static int
acpi_video_switch_brightness(struct acpi_video_device *device, int event)
{
	unsigned long long level_current, level_next;
	int result = -EINVAL;

	/* no warning message if acpi_backlight=vendor is used */
	if (!acpi_video_backlight_support())
		return 0;

	if (!device->brightness)
		goto out;

	result = acpi_video_device_lcd_get_level_current(device,
							 &level_current,
							 false);
	if (result)
		goto out;

	level_next = acpi_video_get_next_level(device, level_current, event);

	result = acpi_video_device_lcd_set_level(device, level_next);

	if (!result)
		backlight_force_update(device->backlight,
				       BACKLIGHT_UPDATE_HOTKEY);

out:
	if (result)
		printk(KERN_ERR PREFIX "Failed to switch the brightness\n");

	return result;
}

int acpi_video_get_edid(struct acpi_device *device, int type, int device_id,
			void **edid)
{
	struct acpi_video_bus *video;
	struct acpi_video_device *video_device;
	union acpi_object *buffer = NULL;
	acpi_status status;
	int i, length;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	video = acpi_driver_data(device);

	for (i = 0; i < video->attached_count; i++) {
		video_device = video->attached_array[i].bind_info;
		length = 256;

		if (!video_device)
			continue;

		if (!video_device->cap._DDC)
			continue;

		if (type) {
			switch (type) {
			case ACPI_VIDEO_DISPLAY_CRT:
				if (!video_device->flags.crt)
					continue;
				break;
			case ACPI_VIDEO_DISPLAY_TV:
				if (!video_device->flags.tvout)
					continue;
				break;
			case ACPI_VIDEO_DISPLAY_DVI:
				if (!video_device->flags.dvi)
					continue;
				break;
			case ACPI_VIDEO_DISPLAY_LCD:
				if (!video_device->flags.lcd)
					continue;
				break;
			}
		} else if (video_device->device_id != device_id) {
			continue;
		}

		status = acpi_video_device_EDID(video_device, &buffer, length);

		if (ACPI_FAILURE(status) || !buffer ||
		    buffer->type != ACPI_TYPE_BUFFER) {
			length = 128;
			status = acpi_video_device_EDID(video_device, &buffer,
							length);
			if (ACPI_FAILURE(status) || !buffer ||
			    buffer->type != ACPI_TYPE_BUFFER) {
				continue;
			}
		}

		*edid = buffer->buffer.pointer;
		return length;
	}

	return -ENODEV;
}
EXPORT_SYMBOL(acpi_video_get_edid);

static int
acpi_video_bus_get_devices(struct acpi_video_bus *video,
			   struct acpi_device *device)
{
	int status = 0;
	struct acpi_device *dev;

	/*
	 * There are systems where video module known to work fine regardless
	 * of broken _DOD and ignoring returned value here doesn't cause
	 * any issues later.
	 */
	acpi_video_device_enumerate(video);

	list_for_each_entry(dev, &device->children, node) {

		status = acpi_video_bus_get_one_device(dev, video);
		if (status) {
			dev_err(&dev->dev, "Can't attach device\n");
			break;
		}
	}
	return status;
}

static int acpi_video_bus_put_one_device(struct acpi_video_device *device)
{
	acpi_status status;

	if (!device || !device->video)
		return -ENOENT;

	if (device->flags.notify) {
		status = acpi_remove_notify_handler(device->dev->handle,
				ACPI_DEVICE_NOTIFY, acpi_video_device_notify);
		if (ACPI_FAILURE(status))
			dev_err(&device->dev->dev,
					"Can't remove video notify handler\n");
	}

	if (device->backlight) {
		backlight_device_unregister(device->backlight);
		device->backlight = NULL;
	}
	if (device->cooling_dev) {
		sysfs_remove_link(&device->dev->dev.kobj,
				  "thermal_cooling");
		sysfs_remove_link(&device->cooling_dev->device.kobj,
				  "device");
		thermal_cooling_device_unregister(device->cooling_dev);
		device->cooling_dev = NULL;
	}

	return 0;
}

static int acpi_video_bus_put_devices(struct acpi_video_bus *video)
{
	int status;
	struct acpi_video_device *dev, *next;

	mutex_lock(&video->device_list_lock);

	list_for_each_entry_safe(dev, next, &video->video_device_list, entry) {

		status = acpi_video_bus_put_one_device(dev);
		if (ACPI_FAILURE(status))
			printk(KERN_WARNING PREFIX
			       "hhuuhhuu bug in acpi video driver.\n");

		if (dev->brightness) {
			kfree(dev->brightness->levels);
			kfree(dev->brightness);
		}
		list_del(&dev->entry);
		kfree(dev);
	}

	mutex_unlock(&video->device_list_lock);

	return 0;
}

/* acpi_video interface */

static int acpi_video_bus_start_devices(struct acpi_video_bus *video)
{
	return acpi_video_bus_DOS(video, 0, 0);
}

static int acpi_video_bus_stop_devices(struct acpi_video_bus *video)
{
	return acpi_video_bus_DOS(video, 0, 1);
}

static void acpi_video_bus_notify(struct acpi_device *device, u32 event)
{
	struct acpi_video_bus *video = acpi_driver_data(device);
	struct input_dev *input;
	int keycode = 0;

	if (!video)
		return;

	input = video->input;

	switch (event) {
	case ACPI_VIDEO_NOTIFY_SWITCH:	/* User requested a switch,
					 * most likely via hotkey. */
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_SWITCHVIDEOMODE;
		break;

	case ACPI_VIDEO_NOTIFY_PROBE:	/* User plugged in or removed a video
					 * connector. */
		acpi_video_device_enumerate(video);
		acpi_video_device_rebind(video);
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_SWITCHVIDEOMODE;
		break;

	case ACPI_VIDEO_NOTIFY_CYCLE:	/* Cycle Display output hotkey pressed. */
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_SWITCHVIDEOMODE;
		break;
	case ACPI_VIDEO_NOTIFY_NEXT_OUTPUT:	/* Next Display output hotkey pressed. */
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_VIDEO_NEXT;
		break;
	case ACPI_VIDEO_NOTIFY_PREV_OUTPUT:	/* previous Display output hotkey pressed. */
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_VIDEO_PREV;
		break;

	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	if (acpi_notifier_call_chain(device, event, 0))
		/* Something vetoed the keypress. */
		keycode = 0;

	if (keycode) {
		input_report_key(input, keycode, 1);
		input_sync(input);
		input_report_key(input, keycode, 0);
		input_sync(input);
	}

	return;
}

static void acpi_video_device_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_video_device *video_device = data;
	struct acpi_device *device = NULL;
	struct acpi_video_bus *bus;
	struct input_dev *input;
	int keycode = 0;

	if (!video_device)
		return;

	device = video_device->dev;
	bus = video_device->video;
	input = bus->input;

	switch (event) {
	case ACPI_VIDEO_NOTIFY_CYCLE_BRIGHTNESS:	/* Cycle brightness */
		if (brightness_switch_enabled)
			acpi_video_switch_brightness(video_device, event);
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_BRIGHTNESS_CYCLE;
		break;
	case ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS:	/* Increase brightness */
		if (brightness_switch_enabled)
			acpi_video_switch_brightness(video_device, event);
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_BRIGHTNESSUP;
		break;
	case ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS:	/* Decrease brightness */
		if (brightness_switch_enabled)
			acpi_video_switch_brightness(video_device, event);
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_BRIGHTNESSDOWN;
		break;
	case ACPI_VIDEO_NOTIFY_ZERO_BRIGHTNESS:	/* zero brightness */
		if (brightness_switch_enabled)
			acpi_video_switch_brightness(video_device, event);
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_BRIGHTNESS_ZERO;
		break;
	case ACPI_VIDEO_NOTIFY_DISPLAY_OFF:	/* display device off */
		if (brightness_switch_enabled)
			acpi_video_switch_brightness(video_device, event);
		acpi_bus_generate_proc_event(device, event, 0);
		keycode = KEY_DISPLAY_OFF;
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	acpi_notifier_call_chain(device, event, 0);

	if (keycode) {
		input_report_key(input, keycode, 1);
		input_sync(input);
		input_report_key(input, keycode, 0);
		input_sync(input);
	}

	return;
}

static int acpi_video_resume(struct notifier_block *nb,
				unsigned long val, void *ign)
{
	struct acpi_video_bus *video;
	struct acpi_video_device *video_device;
	int i;

	switch (val) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
	case PM_RESTORE_PREPARE:
		return NOTIFY_DONE;
	}

	video = container_of(nb, struct acpi_video_bus, pm_nb);

	dev_info(&video->device->dev, "Restoring backlight state\n");

	for (i = 0; i < video->attached_count; i++) {
		video_device = video->attached_array[i].bind_info;
		if (video_device && video_device->backlight)
			acpi_video_set_brightness(video_device->backlight);
	}

	return NOTIFY_OK;
}

static acpi_status
acpi_video_bus_match(acpi_handle handle, u32 level, void *context,
			void **return_value)
{
	struct acpi_device *device = context;
	struct acpi_device *sibling;
	int result;

	if (handle == device->handle)
		return AE_CTRL_TERMINATE;

	result = acpi_bus_get_device(handle, &sibling);
	if (result)
		return AE_OK;

	if (!strcmp(acpi_device_name(sibling), ACPI_VIDEO_BUS_NAME))
			return AE_ALREADY_EXISTS;

	return AE_OK;
}

static int instance;

static int acpi_video_bus_add(struct acpi_device *device)
{
	struct acpi_video_bus *video;
	struct input_dev *input;
	int error;
	acpi_status status;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE,
				device->parent->handle, 1,
				acpi_video_bus_match, NULL,
				device, NULL);
	if (status == AE_ALREADY_EXISTS) {
		printk(KERN_WARNING FW_BUG
			"Duplicate ACPI video bus devices for the"
			" same VGA controller, please try module "
			"parameter \"video.allow_duplicates=1\""
			"if the current driver doesn't work.\n");
		if (!allow_duplicates)
			return -ENODEV;
	}

	video = kzalloc(sizeof(struct acpi_video_bus), GFP_KERNEL);
	if (!video)
		return -ENOMEM;

	/* a hack to fix the duplicate name "VID" problem on T61 */
	if (!strcmp(device->pnp.bus_id, "VID")) {
		if (instance)
			device->pnp.bus_id[3] = '0' + instance;
		instance ++;
	}
	/* a hack to fix the duplicate name "VGA" problem on Pa 3553 */
	if (!strcmp(device->pnp.bus_id, "VGA")) {
		if (instance)
			device->pnp.bus_id[3] = '0' + instance;
		instance++;
	}

	video->device = device;
	strcpy(acpi_device_name(device), ACPI_VIDEO_BUS_NAME);
	strcpy(acpi_device_class(device), ACPI_VIDEO_CLASS);
	device->driver_data = video;

	acpi_video_bus_find_cap(video);
	error = acpi_video_bus_check(video);
	if (error)
		goto err_free_video;

	mutex_init(&video->device_list_lock);
	INIT_LIST_HEAD(&video->video_device_list);

	error = acpi_video_bus_get_devices(video, device);
	if (error)
		goto err_put_video;

	video->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_put_video;
	}

	error = acpi_video_bus_start_devices(video);
	if (error)
		goto err_free_input_dev;

	snprintf(video->phys, sizeof(video->phys),
		"%s/video/input0", acpi_device_hid(video->device));

	input->name = acpi_device_name(video->device);
	input->phys = video->phys;
	input->id.bustype = BUS_HOST;
	input->id.product = 0x06;
	input->dev.parent = &device->dev;
	input->evbit[0] = BIT(EV_KEY);
	set_bit(KEY_SWITCHVIDEOMODE, input->keybit);
	set_bit(KEY_VIDEO_NEXT, input->keybit);
	set_bit(KEY_VIDEO_PREV, input->keybit);
	set_bit(KEY_BRIGHTNESS_CYCLE, input->keybit);
	set_bit(KEY_BRIGHTNESSUP, input->keybit);
	set_bit(KEY_BRIGHTNESSDOWN, input->keybit);
	set_bit(KEY_BRIGHTNESS_ZERO, input->keybit);
	set_bit(KEY_DISPLAY_OFF, input->keybit);

	printk(KERN_INFO PREFIX "%s [%s] (multi-head: %s  rom: %s  post: %s)\n",
	       ACPI_VIDEO_DEVICE_NAME, acpi_device_bid(device),
	       video->flags.multihead ? "yes" : "no",
	       video->flags.rom ? "yes" : "no",
	       video->flags.post ? "yes" : "no");

	video->pm_nb.notifier_call = acpi_video_resume;
	video->pm_nb.priority = 0;
	error = register_pm_notifier(&video->pm_nb);
	if (error)
		goto err_stop_video;

	error = input_register_device(input);
	if (error)
		goto err_unregister_pm_notifier;

	return 0;

 err_unregister_pm_notifier:
	unregister_pm_notifier(&video->pm_nb);
 err_stop_video:
	acpi_video_bus_stop_devices(video);
 err_free_input_dev:
	input_free_device(input);
 err_put_video:
	acpi_video_bus_put_devices(video);
	kfree(video->attached_array);
 err_free_video:
	kfree(video);
	device->driver_data = NULL;

	return error;
}

static int acpi_video_bus_remove(struct acpi_device *device)
{
	struct acpi_video_bus *video = NULL;


	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	video = acpi_driver_data(device);

	unregister_pm_notifier(&video->pm_nb);

	acpi_video_bus_stop_devices(video);
	acpi_video_bus_put_devices(video);

	input_unregister_device(video->input);
	kfree(video->attached_array);
	kfree(video);

	return 0;
}

static int __init is_i740(struct pci_dev *dev)
{
	if (dev->device == 0x00D1)
		return 1;
	if (dev->device == 0x7000)
		return 1;
	return 0;
}

static int __init intel_opregion_present(void)
{
	int opregion = 0;
	struct pci_dev *dev = NULL;
	u32 address;

	for_each_pci_dev(dev) {
		if ((dev->class >> 8) != PCI_CLASS_DISPLAY_VGA)
			continue;
		if (dev->vendor != PCI_VENDOR_ID_INTEL)
			continue;
		/* We don't want to poke around undefined i740 registers */
		if (is_i740(dev))
			continue;
		pci_read_config_dword(dev, 0xfc, &address);
		if (!address)
			continue;
		opregion = 1;
	}
	return opregion;
}

int acpi_video_register(void)
{
	int result = 0;
	if (register_count) {
		/*
		 * if the function of acpi_video_register is already called,
		 * don't register the acpi_vide_bus again and return no error.
		 */
		return 0;
	}

	result = acpi_bus_register_driver(&acpi_video_bus);
	if (result < 0)
		return -ENODEV;

	/*
	 * When the acpi_video_bus is loaded successfully, increase
	 * the counter reference.
	 */
	register_count = 1;

	return 0;
}
EXPORT_SYMBOL(acpi_video_register);

void acpi_video_unregister(void)
{
	if (!register_count) {
		/*
		 * If the acpi video bus is already unloaded, don't
		 * unload it again and return directly.
		 */
		return;
	}
	acpi_bus_unregister_driver(&acpi_video_bus);

	register_count = 0;

	return;
}
EXPORT_SYMBOL(acpi_video_unregister);

/*
 * This is kind of nasty. Hardware using Intel chipsets may require
 * the video opregion code to be run first in order to initialise
 * state before any ACPI video calls are made. To handle this we defer
 * registration of the video class until the opregion code has run.
 */

static int __init acpi_video_init(void)
{
	dmi_check_system(video_dmi_table);

	if (intel_opregion_present())
		return 0;

	return acpi_video_register();
}

static void __exit acpi_video_exit(void)
{
	acpi_video_unregister();

	return;
}

module_init(acpi_video_init);
module_exit(acpi_video_exit);
