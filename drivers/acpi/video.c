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
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <linux/backlight.h>
#include <linux/thermal.h>
#include <linux/video_output.h>
#include <linux/sort.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define ACPI_VIDEO_CLASS		"video"
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

#define ACPI_VIDEO_DISPLAY_CRT	1
#define ACPI_VIDEO_DISPLAY_TV	2
#define ACPI_VIDEO_DISPLAY_DVI	3
#define ACPI_VIDEO_DISPLAY_LCD	4

#define _COMPONENT		ACPI_VIDEO_COMPONENT
ACPI_MODULE_NAME("video");

MODULE_AUTHOR("Bruno Ducrot");
MODULE_DESCRIPTION("ACPI Video Driver");
MODULE_LICENSE("GPL");

static int brightness_switch_enabled = 1;
module_param(brightness_switch_enabled, bool, 0644);

static int acpi_video_bus_add(struct acpi_device *device);
static int acpi_video_bus_remove(struct acpi_device *device, int type);
static int acpi_video_resume(struct acpi_device *device);
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
		.resume = acpi_video_resume,
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
	struct proc_dir_entry *dir;
	struct input_dev *input;
	char phys[32];	/* for input device */
};

struct acpi_video_device_flags {
	u8 crt:1;
	u8 lcd:1;
	u8 tvout:1;
	u8 dvi:1;
	u8 bios:1;
	u8 unknown:1;
	u8 reserved:2;
};

struct acpi_video_device_cap {
	u8 _ADR:1;		/*Return the unique ID */
	u8 _BCL:1;		/*Query list of brightness control levels supported */
	u8 _BCM:1;		/*Set the brightness level */
	u8 _BQC:1;		/* Get current brightness level */
	u8 _BCQ:1;		/* Some buggy BIOS uses _BCQ instead of _BQC */
	u8 _DDC:1;		/*Return the EDID for this device */
	u8 _DCS:1;		/*Return status of output device */
	u8 _DGS:1;		/*Query graphics state */
	u8 _DSS:1;		/*Device state set */
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
	struct thermal_cooling_device *cdev;
	struct output_device *output_dev;
};

/* bus */
static int acpi_video_bus_info_open_fs(struct inode *inode, struct file *file);
static const struct file_operations acpi_video_bus_info_fops = {
	.owner = THIS_MODULE,
	.open = acpi_video_bus_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_bus_ROM_open_fs(struct inode *inode, struct file *file);
static const struct file_operations acpi_video_bus_ROM_fops = {
	.owner = THIS_MODULE,
	.open = acpi_video_bus_ROM_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_bus_POST_info_open_fs(struct inode *inode,
					    struct file *file);
static const struct file_operations acpi_video_bus_POST_info_fops = {
	.owner = THIS_MODULE,
	.open = acpi_video_bus_POST_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_bus_POST_open_fs(struct inode *inode, struct file *file);
static ssize_t acpi_video_bus_write_POST(struct file *file,
	const char __user *buffer, size_t count, loff_t *data);
static const struct file_operations acpi_video_bus_POST_fops = {
	.owner = THIS_MODULE,
	.open = acpi_video_bus_POST_open_fs,
	.read = seq_read,
	.write = acpi_video_bus_write_POST,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_bus_DOS_open_fs(struct inode *inode, struct file *file);
static ssize_t acpi_video_bus_write_DOS(struct file *file,
	const char __user *buffer, size_t count, loff_t *data);
static const struct file_operations acpi_video_bus_DOS_fops = {
	.owner = THIS_MODULE,
	.open = acpi_video_bus_DOS_open_fs,
	.read = seq_read,
	.write = acpi_video_bus_write_DOS,
	.llseek = seq_lseek,
	.release = single_release,
};

/* device */
static int acpi_video_device_info_open_fs(struct inode *inode,
					  struct file *file);
static const struct file_operations acpi_video_device_info_fops = {
	.owner = THIS_MODULE,
	.open = acpi_video_device_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_device_state_open_fs(struct inode *inode,
					   struct file *file);
static ssize_t acpi_video_device_write_state(struct file *file,
	const char __user *buffer, size_t count, loff_t *data);
static const struct file_operations acpi_video_device_state_fops = {
	.owner = THIS_MODULE,
	.open = acpi_video_device_state_open_fs,
	.read = seq_read,
	.write = acpi_video_device_write_state,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_device_brightness_open_fs(struct inode *inode,
						struct file *file);
static ssize_t acpi_video_device_write_brightness(struct file *file,
	const char __user *buffer, size_t count, loff_t *data);
static struct file_operations acpi_video_device_brightness_fops = {
	.owner = THIS_MODULE,
	.open = acpi_video_device_brightness_open_fs,
	.read = seq_read,
	.write = acpi_video_device_write_brightness,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_device_EDID_open_fs(struct inode *inode,
					  struct file *file);
static const struct file_operations acpi_video_device_EDID_fops = {
	.owner = THIS_MODULE,
	.open = acpi_video_device_EDID_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
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
			unsigned long long *level);
static int acpi_video_get_next_level(struct acpi_video_device *device,
				     u32 level_current, u32 event);
static int acpi_video_switch_brightness(struct acpi_video_device *device,
					 int event);
static int acpi_video_device_get_state(struct acpi_video_device *device,
			    unsigned long long *state);
static int acpi_video_output_get(struct output_device *od);
static int acpi_video_device_set_state(struct acpi_video_device *device, int state);

/*backlight device sysfs support*/
static int acpi_video_get_brightness(struct backlight_device *bd)
{
	unsigned long long cur_level;
	int i;
	struct acpi_video_device *vd =
		(struct acpi_video_device *)bl_get_data(bd);

	if (acpi_video_device_lcd_get_level_current(vd, &cur_level))
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

static struct backlight_ops acpi_backlight_ops = {
	.get_brightness = acpi_video_get_brightness,
	.update_status  = acpi_video_set_brightness,
};

/*video output device sysfs support*/
static int acpi_video_output_get(struct output_device *od)
{
	unsigned long long state;
	struct acpi_video_device *vd =
		(struct acpi_video_device *)dev_get_drvdata(&od->dev);
	acpi_video_device_get_state(vd, &state);
	return (int)state;
}

static int acpi_video_output_set(struct output_device *od)
{
	unsigned long state = od->request_state;
	struct acpi_video_device *vd=
		(struct acpi_video_device *)dev_get_drvdata(&od->dev);
	return acpi_video_device_set_state(vd, state);
}

static struct output_properties acpi_output_properties = {
	.set_state = acpi_video_output_set,
	.get_status = acpi_video_output_get,
};


/* thermal cooling device callbacks */
static int video_get_max_state(struct thermal_cooling_device *cdev, unsigned
			       long *state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_video_device *video = acpi_driver_data(device);

	*state = video->brightness->count - 3;
	return 0;
}

static int video_get_cur_state(struct thermal_cooling_device *cdev, unsigned
			       long *state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_video_device *video = acpi_driver_data(device);
	unsigned long long level;
	int offset;

	if (acpi_video_device_lcd_get_level_current(video, &level))
		return -EINVAL;
	for (offset = 2; offset < video->brightness->count; offset++)
		if (level == video->brightness->levels[offset]) {
			*state = video->brightness->count - offset - 1;
			return 0;
		}

	return -EINVAL;
}

static int
video_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct acpi_device *device = cdev->devdata;
	struct acpi_video_device *video = acpi_driver_data(device);
	int level;

	if ( state >= video->brightness->count - 2)
		return -EINVAL;

	state = video->brightness->count - state;
	level = video->brightness->levels[state -1];
	return acpi_video_device_lcd_set_level(video, level);
}

static struct thermal_cooling_device_ops video_cooling_ops = {
	.get_max_state = video_get_max_state,
	.get_cur_state = video_get_cur_state,
	.set_cur_state = video_set_cur_state,
};

/* --------------------------------------------------------------------------
                               Video Management
   -------------------------------------------------------------------------- */

/* device */

static int
acpi_video_device_query(struct acpi_video_device *device, unsigned long long *state)
{
	int status;

	status = acpi_evaluate_integer(device->dev->handle, "_DGS", NULL, state);

	return status;
}

static int
acpi_video_device_get_state(struct acpi_video_device *device,
			    unsigned long long *state)
{
	int status;

	status = acpi_evaluate_integer(device->dev->handle, "_DCS", NULL, state);

	return status;
}

static int
acpi_video_device_set_state(struct acpi_video_device *device, int state)
{
	int status;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };
	unsigned long long ret;


	arg0.integer.value = state;
	status = acpi_evaluate_integer(device->dev->handle, "_DSS", &args, &ret);

	return status;
}

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

static int
acpi_video_device_lcd_get_level_current(struct acpi_video_device *device,
					unsigned long long *level)
{
	acpi_status status = AE_OK;

	if (device->cap._BQC || device->cap._BCQ) {
		char *buf = device->cap._BQC ? "_BQC" : "_BCQ";

		status = acpi_evaluate_integer(device->dev->handle, buf,
						NULL, level);
		if (ACPI_SUCCESS(status)) {
			if (device->brightness->flags._BQC_use_index) {
				if (device->brightness->flags._BCL_reversed)
					*level = device->brightness->count
								 - 3 - (*level);
				*level = device->brightness->levels[*level + 2];

			}
			device->brightness->curr = *level;
			return 0;
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

static int
acpi_video_bus_set_POST(struct acpi_video_bus *video, unsigned long option)
{
	int status;
	unsigned long long tmp;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };


	arg0.integer.value = option;

	status = acpi_evaluate_integer(video->device->handle, "_SPD", &args, &tmp);
	if (ACPI_SUCCESS(status))
		status = tmp ? (-EINVAL) : (AE_OK);

	return status;
}

static int
acpi_video_bus_get_POST(struct acpi_video_bus *video, unsigned long long *id)
{
	int status;

	status = acpi_evaluate_integer(video->device->handle, "_GPD", NULL, id);

	return status;
}

static int
acpi_video_bus_POST_options(struct acpi_video_bus *video,
			    unsigned long long *options)
{
	int status;

	status = acpi_evaluate_integer(video->device->handle, "_VPO", NULL, options);
	*options &= 3;

	return status;
}

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
 * 		-1	wrong arg.
 */

static int
acpi_video_bus_DOS(struct acpi_video_bus *video, int bios_flag, int lcd_flag)
{
	acpi_integer status = 0;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };


	if (bios_flag < 0 || bios_flag > 3 || lcd_flag < 0 || lcd_flag > 1) {
		status = -1;
		goto Failed;
	}
	arg0.integer.value = (lcd_flag << 2) | bios_flag;
	video->dos_setting = arg0.integer.value;
	acpi_evaluate_object(video->device->handle, "_DOS", &args, NULL);

      Failed:
	return status;
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
	for (i = 2; i < count; i++)
		if (br->levels[i] == br->levels[0] ||
		    br->levels[i] == br->levels[1])
			level_ac_battery++;

	if (level_ac_battery < 2) {
		level_ac_battery = 2 - level_ac_battery;
		br->flags._BCL_no_ac_battery_levels = 1;
		for (i = (count - 1 + level_ac_battery); i >= 2; i--)
			br->levels[i] = br->levels[i - level_ac_battery];
		count += level_ac_battery;
	} else if (level_ac_battery > 2)
		ACPI_ERROR((AE_INFO, "Too many duplicates in _BCL package\n"));

	/* Check if the _BCL package is in a reversed order */
	if (max_level == br->levels[2]) {
		br->flags._BCL_reversed = 1;
		sort(&br->levels[2], count - 2, sizeof(br->levels[2]),
			acpi_video_cmp_level, NULL);
	} else if (max_level != br->levels[count - 1])
		ACPI_ERROR((AE_INFO,
			    "Found unordered _BCL package\n"));

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
	br->curr = max_level;
	result = acpi_video_device_lcd_get_level_current(device, &level_old);
	if (result)
		goto out_free_levels;

	result = acpi_video_device_lcd_set_level(device, br->curr);
	if (result)
		goto out_free_levels;

	result = acpi_video_device_lcd_get_level_current(device, &level);
	if (result)
		goto out_free_levels;

	if ((level != level_old) && !br->flags._BCM_use_index) {
		/* Note:
		 * This piece of code does not work correctly if the current
		 * brightness levels is 0.
		 * But I guess boxes that boot with such a dark screen are rare
		 * and no more code is needed to cover this specifial case.
		 */

		if (level_ac_battery != 2) {
			/*
			 * For now, we don't support the _BCL like this:
			 * 16, 15, 0, 1, 2, 3, ..., 14, 15, 16
			 * because we may mess up the index returned by _BQC.
			 * Plus: we have not got a box like this.
			 */
			ACPI_ERROR((AE_INFO, "_BCL not supported\n"));
		}
		br->flags._BQC_use_index = 1;
	}

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


	memset(&device->cap, 0, sizeof(device->cap));

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
	if (ACPI_SUCCESS(acpi_get_handle(device->dev->handle, "_DCS", &h_dummy1))) {
		device->cap._DCS = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->dev->handle, "_DGS", &h_dummy1))) {
		device->cap._DGS = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->dev->handle, "_DSS", &h_dummy1))) {
		device->cap._DSS = 1;
	}

	if (acpi_video_backlight_support()) {
		int result;
		static int count = 0;
		char *name;

		result = acpi_video_init_brightness(device);
		if (result)
			return;
		name = kzalloc(MAX_NAME_LEN, GFP_KERNEL);
		if (!name)
			return;

		sprintf(name, "acpi_video%d", count++);
		device->backlight = backlight_device_register(name,
			NULL, device, &acpi_backlight_ops);
		device->backlight->props.max_brightness = device->brightness->count-3;
		kfree(name);

		device->cdev = thermal_cooling_device_register("LCD",
					device->dev, &video_cooling_ops);
		if (IS_ERR(device->cdev))
			return;

		dev_info(&device->dev->dev, "registered as cooling_device%d\n",
			 device->cdev->id);
		result = sysfs_create_link(&device->dev->dev.kobj,
				&device->cdev->device.kobj,
				"thermal_cooling");
		if (result)
			printk(KERN_ERR PREFIX "Create sysfs link\n");
		result = sysfs_create_link(&device->cdev->device.kobj,
				&device->dev->dev.kobj, "device");
		if (result)
			printk(KERN_ERR PREFIX "Create sysfs link\n");

	}

	if (acpi_video_display_switch_support()) {

		if (device->cap._DCS && device->cap._DSS) {
			static int count;
			char *name;
			name = kzalloc(MAX_NAME_LEN, GFP_KERNEL);
			if (!name)
				return;
			sprintf(name, "acpi_video%d", count++);
			device->output_dev = video_output_register(name,
					NULL, device, &acpi_output_properties);
			kfree(name);
		}
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

	memset(&video->cap, 0, sizeof(video->cap));
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
	struct device *dev;

	if (!video)
		return -EINVAL;

	dev = acpi_get_physical_pci_device(video->device->handle);
	if (!dev)
		return -ENODEV;
	put_device(dev);

	/* Since there is no HID, CID and so on for VGA driver, we have
	 * to check well known required nodes.
	 */

	/* Does this device support video switching? */
	if (video->cap._DOS) {
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
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_video_dir;

/* video devices */

static int acpi_video_device_info_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_device *dev = seq->private;


	if (!dev)
		goto end;

	seq_printf(seq, "device_id:    0x%04x\n", (u32) dev->device_id);
	seq_printf(seq, "type:         ");
	if (dev->flags.crt)
		seq_printf(seq, "CRT\n");
	else if (dev->flags.lcd)
		seq_printf(seq, "LCD\n");
	else if (dev->flags.tvout)
		seq_printf(seq, "TVOUT\n");
	else if (dev->flags.dvi)
		seq_printf(seq, "DVI\n");
	else
		seq_printf(seq, "UNKNOWN\n");

	seq_printf(seq, "known by bios: %s\n", dev->flags.bios ? "yes" : "no");

      end:
	return 0;
}

static int
acpi_video_device_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_device_info_seq_show,
			   PDE(inode)->data);
}

static int acpi_video_device_state_seq_show(struct seq_file *seq, void *offset)
{
	int status;
	struct acpi_video_device *dev = seq->private;
	unsigned long long state;


	if (!dev)
		goto end;

	status = acpi_video_device_get_state(dev, &state);
	seq_printf(seq, "state:     ");
	if (ACPI_SUCCESS(status))
		seq_printf(seq, "0x%02llx\n", state);
	else
		seq_printf(seq, "<not supported>\n");

	status = acpi_video_device_query(dev, &state);
	seq_printf(seq, "query:     ");
	if (ACPI_SUCCESS(status))
		seq_printf(seq, "0x%02llx\n", state);
	else
		seq_printf(seq, "<not supported>\n");

      end:
	return 0;
}

static int
acpi_video_device_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_device_state_seq_show,
			   PDE(inode)->data);
}

static ssize_t
acpi_video_device_write_state(struct file *file,
			      const char __user * buffer,
			      size_t count, loff_t * data)
{
	int status;
	struct seq_file *m = file->private_data;
	struct acpi_video_device *dev = m->private;
	char str[12] = { 0 };
	u32 state = 0;


	if (!dev || count + 1 > sizeof str)
		return -EINVAL;

	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	str[count] = 0;
	state = simple_strtoul(str, NULL, 0);
	state &= ((1ul << 31) | (1ul << 30) | (1ul << 0));

	status = acpi_video_device_set_state(dev, state);

	if (status)
		return -EFAULT;

	return count;
}

static int
acpi_video_device_brightness_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_device *dev = seq->private;
	int i;


	if (!dev || !dev->brightness) {
		seq_printf(seq, "<not supported>\n");
		return 0;
	}

	seq_printf(seq, "levels: ");
	for (i = 2; i < dev->brightness->count; i++)
		seq_printf(seq, " %d", dev->brightness->levels[i]);
	seq_printf(seq, "\ncurrent: %d\n", dev->brightness->curr);

	return 0;
}

static int
acpi_video_device_brightness_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_device_brightness_seq_show,
			   PDE(inode)->data);
}

static ssize_t
acpi_video_device_write_brightness(struct file *file,
				   const char __user * buffer,
				   size_t count, loff_t * data)
{
	struct seq_file *m = file->private_data;
	struct acpi_video_device *dev = m->private;
	char str[5] = { 0 };
	unsigned int level = 0;
	int i;


	if (!dev || !dev->brightness || count + 1 > sizeof str)
		return -EINVAL;

	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	str[count] = 0;
	level = simple_strtoul(str, NULL, 0);

	if (level > 100)
		return -EFAULT;

	/* validate through the list of available levels */
	for (i = 2; i < dev->brightness->count; i++)
		if (level == dev->brightness->levels[i]) {
			if (!acpi_video_device_lcd_set_level(dev, level))
				return count;
			break;
		}

	return -EINVAL;
}

static int acpi_video_device_EDID_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_device *dev = seq->private;
	int status;
	int i;
	union acpi_object *edid = NULL;


	if (!dev)
		goto out;

	status = acpi_video_device_EDID(dev, &edid, 128);
	if (ACPI_FAILURE(status)) {
		status = acpi_video_device_EDID(dev, &edid, 256);
	}

	if (ACPI_FAILURE(status)) {
		goto out;
	}

	if (edid && edid->type == ACPI_TYPE_BUFFER) {
		for (i = 0; i < edid->buffer.length; i++)
			seq_putc(seq, edid->buffer.pointer[i]);
	}

      out:
	if (!edid)
		seq_printf(seq, "<not supported>\n");
	else
		kfree(edid);

	return 0;
}

static int
acpi_video_device_EDID_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_device_EDID_seq_show,
			   PDE(inode)->data);
}

static int acpi_video_device_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry, *device_dir;
	struct acpi_video_device *vid_dev;

	vid_dev = acpi_driver_data(device);
	if (!vid_dev)
		return -ENODEV;

	device_dir = proc_mkdir(acpi_device_bid(device),
				vid_dev->video->dir);
	if (!device_dir)
		return -ENOMEM;

	/* 'info' [R] */
	entry = proc_create_data("info", S_IRUGO, device_dir,
			&acpi_video_device_info_fops, acpi_driver_data(device));
	if (!entry)
		goto err_remove_dir;

	/* 'state' [R/W] */
	entry = proc_create_data("state", S_IFREG | S_IRUGO | S_IWUSR,
				 device_dir,
				 &acpi_video_device_state_fops,
				 acpi_driver_data(device));
	if (!entry)
		goto err_remove_info;

	/* 'brightness' [R/W] */
	entry = proc_create_data("brightness", S_IFREG | S_IRUGO | S_IWUSR,
				 device_dir,
				 &acpi_video_device_brightness_fops,
				 acpi_driver_data(device));
	if (!entry)
		goto err_remove_state;

	/* 'EDID' [R] */
	entry = proc_create_data("EDID", S_IRUGO, device_dir,
				 &acpi_video_device_EDID_fops,
				 acpi_driver_data(device));
	if (!entry)
		goto err_remove_brightness;

	acpi_device_dir(device) = device_dir;

	return 0;

 err_remove_brightness:
	remove_proc_entry("brightness", device_dir);
 err_remove_state:
	remove_proc_entry("state", device_dir);
 err_remove_info:
	remove_proc_entry("info", device_dir);
 err_remove_dir:
	remove_proc_entry(acpi_device_bid(device), vid_dev->video->dir);
	return -ENOMEM;
}

static int acpi_video_device_remove_fs(struct acpi_device *device)
{
	struct acpi_video_device *vid_dev;
	struct proc_dir_entry *device_dir;

	vid_dev = acpi_driver_data(device);
	if (!vid_dev || !vid_dev->video || !vid_dev->video->dir)
		return -ENODEV;

	device_dir = acpi_device_dir(device);
	if (device_dir) {
		remove_proc_entry("info", device_dir);
		remove_proc_entry("state", device_dir);
		remove_proc_entry("brightness", device_dir);
		remove_proc_entry("EDID", device_dir);
		remove_proc_entry(acpi_device_bid(device), vid_dev->video->dir);
		acpi_device_dir(device) = NULL;
	}

	return 0;
}

/* video bus */
static int acpi_video_bus_info_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = seq->private;


	if (!video)
		goto end;

	seq_printf(seq, "Switching heads:              %s\n",
		   video->flags.multihead ? "yes" : "no");
	seq_printf(seq, "Video ROM:                    %s\n",
		   video->flags.rom ? "yes" : "no");
	seq_printf(seq, "Device to be POSTed on boot:  %s\n",
		   video->flags.post ? "yes" : "no");

      end:
	return 0;
}

static int acpi_video_bus_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_bus_info_seq_show,
			   PDE(inode)->data);
}

static int acpi_video_bus_ROM_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = seq->private;


	if (!video)
		goto end;

	printk(KERN_INFO PREFIX "Please implement %s\n", __func__);
	seq_printf(seq, "<TODO>\n");

      end:
	return 0;
}

static int acpi_video_bus_ROM_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_bus_ROM_seq_show, PDE(inode)->data);
}

static int acpi_video_bus_POST_info_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = seq->private;
	unsigned long long options;
	int status;


	if (!video)
		goto end;

	status = acpi_video_bus_POST_options(video, &options);
	if (ACPI_SUCCESS(status)) {
		if (!(options & 1)) {
			printk(KERN_WARNING PREFIX
			       "The motherboard VGA device is not listed as a possible POST device.\n");
			printk(KERN_WARNING PREFIX
			       "This indicates a BIOS bug. Please contact the manufacturer.\n");
		}
		printk(KERN_WARNING "%llx\n", options);
		seq_printf(seq, "can POST: <integrated video>");
		if (options & 2)
			seq_printf(seq, " <PCI video>");
		if (options & 4)
			seq_printf(seq, " <AGP video>");
		seq_putc(seq, '\n');
	} else
		seq_printf(seq, "<not supported>\n");
      end:
	return 0;
}

static int
acpi_video_bus_POST_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_bus_POST_info_seq_show,
			   PDE(inode)->data);
}

static int acpi_video_bus_POST_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = seq->private;
	int status;
	unsigned long long id;


	if (!video)
		goto end;

	status = acpi_video_bus_get_POST(video, &id);
	if (!ACPI_SUCCESS(status)) {
		seq_printf(seq, "<not supported>\n");
		goto end;
	}
	seq_printf(seq, "device POSTed is <%s>\n", device_decode[id & 3]);

      end:
	return 0;
}

static int acpi_video_bus_DOS_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = seq->private;


	seq_printf(seq, "DOS setting: <%d>\n", video->dos_setting);

	return 0;
}

static int acpi_video_bus_POST_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_bus_POST_seq_show,
			   PDE(inode)->data);
}

static int acpi_video_bus_DOS_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_bus_DOS_seq_show, PDE(inode)->data);
}

static ssize_t
acpi_video_bus_write_POST(struct file *file,
			  const char __user * buffer,
			  size_t count, loff_t * data)
{
	int status;
	struct seq_file *m = file->private_data;
	struct acpi_video_bus *video = m->private;
	char str[12] = { 0 };
	unsigned long long opt, options;


	if (!video || count + 1 > sizeof str)
		return -EINVAL;

	status = acpi_video_bus_POST_options(video, &options);
	if (!ACPI_SUCCESS(status))
		return -EINVAL;

	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	str[count] = 0;
	opt = strtoul(str, NULL, 0);
	if (opt > 3)
		return -EFAULT;

	/* just in case an OEM 'forgot' the motherboard... */
	options |= 1;

	if (options & (1ul << opt)) {
		status = acpi_video_bus_set_POST(video, opt);
		if (!ACPI_SUCCESS(status))
			return -EFAULT;

	}

	return count;
}

static ssize_t
acpi_video_bus_write_DOS(struct file *file,
			 const char __user * buffer,
			 size_t count, loff_t * data)
{
	int status;
	struct seq_file *m = file->private_data;
	struct acpi_video_bus *video = m->private;
	char str[12] = { 0 };
	unsigned long opt;


	if (!video || count + 1 > sizeof str)
		return -EINVAL;

	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	str[count] = 0;
	opt = strtoul(str, NULL, 0);
	if (opt > 7)
		return -EFAULT;

	status = acpi_video_bus_DOS(video, opt & 0x3, (opt & 0x4) >> 2);

	if (!ACPI_SUCCESS(status))
		return -EFAULT;

	return count;
}

static int acpi_video_bus_add_fs(struct acpi_device *device)
{
	struct acpi_video_bus *video = acpi_driver_data(device);
	struct proc_dir_entry *device_dir;
	struct proc_dir_entry *entry;

	device_dir = proc_mkdir(acpi_device_bid(device), acpi_video_dir);
	if (!device_dir)
		return -ENOMEM;

	/* 'info' [R] */
	entry = proc_create_data("info", S_IRUGO, device_dir,
				 &acpi_video_bus_info_fops,
				 acpi_driver_data(device));
	if (!entry)
		goto err_remove_dir;

	/* 'ROM' [R] */
	entry = proc_create_data("ROM", S_IRUGO, device_dir,
				 &acpi_video_bus_ROM_fops,
				 acpi_driver_data(device));
	if (!entry)
		goto err_remove_info;

	/* 'POST_info' [R] */
	entry = proc_create_data("POST_info", S_IRUGO, device_dir,
				 &acpi_video_bus_POST_info_fops,
				 acpi_driver_data(device));
	if (!entry)
		goto err_remove_rom;

	/* 'POST' [R/W] */
	entry = proc_create_data("POST", S_IFREG | S_IRUGO | S_IWUSR,
				 device_dir,
				 &acpi_video_bus_POST_fops,
				 acpi_driver_data(device));
	if (!entry)
		goto err_remove_post_info;

	/* 'DOS' [R/W] */
	entry = proc_create_data("DOS", S_IFREG | S_IRUGO | S_IWUSR,
				 device_dir,
				 &acpi_video_bus_DOS_fops,
				 acpi_driver_data(device));
	if (!entry)
		goto err_remove_post;

	video->dir = acpi_device_dir(device) = device_dir;
	return 0;

 err_remove_post:
	remove_proc_entry("POST", device_dir);
 err_remove_post_info:
	remove_proc_entry("POST_info", device_dir);
 err_remove_rom:
	remove_proc_entry("ROM", device_dir);
 err_remove_info:
	remove_proc_entry("info", device_dir);
 err_remove_dir:
	remove_proc_entry(acpi_device_bid(device), acpi_video_dir);
	return -ENOMEM;
}

static int acpi_video_bus_remove_fs(struct acpi_device *device)
{
	struct proc_dir_entry *device_dir = acpi_device_dir(device);

	if (device_dir) {
		remove_proc_entry("info", device_dir);
		remove_proc_entry("ROM", device_dir);
		remove_proc_entry("POST_info", device_dir);
		remove_proc_entry("POST", device_dir);
		remove_proc_entry("DOS", device_dir);
		remove_proc_entry(acpi_device_bid(device), acpi_video_dir);
		acpi_device_dir(device) = NULL;
	}

	return 0;
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
acpi_video_bus_get_one_device(struct acpi_device *device,
			      struct acpi_video_bus *video)
{
	unsigned long long device_id;
	int status;
	struct acpi_video_device *data;
	struct acpi_video_device_attrib* attribute;

	if (!device || !video)
		return -EINVAL;

	status =
	    acpi_evaluate_integer(device->handle, "_ADR", NULL, &device_id);
	if (ACPI_SUCCESS(status)) {

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
		} else
			data->flags.unknown = 1;

		acpi_video_device_bind(video, data);
		acpi_video_device_find_cap(data);

		status = acpi_install_notify_handler(device->handle,
						     ACPI_DEVICE_NOTIFY,
						     acpi_video_device_notify,
						     data);
		if (ACPI_FAILURE(status)) {
			printk(KERN_ERR PREFIX
					  "Error installing notify handler\n");
			if(data->brightness)
				kfree(data->brightness->levels);
			kfree(data->brightness);
			kfree(data);
			return -ENODEV;
		}

		mutex_lock(&video->device_list_lock);
		list_add_tail(&data->entry, &video->video_device_list);
		mutex_unlock(&video->device_list_lock);

		acpi_video_device_add_fs(device);

		return 0;
	}

	return -ENOENT;
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

	if (!device->brightness)
		goto out;

	result = acpi_video_device_lcd_get_level_current(device,
							 &level_current);
	if (result)
		goto out;

	level_next = acpi_video_get_next_level(device, level_current, event);

	result = acpi_video_device_lcd_set_level(device, level_next);

out:
	if (result)
		printk(KERN_ERR PREFIX "Failed to switch the brightness\n");

	return result;
}

static int
acpi_video_bus_get_devices(struct acpi_video_bus *video,
			   struct acpi_device *device)
{
	int status = 0;
	struct acpi_device *dev;

	acpi_video_device_enumerate(video);

	list_for_each_entry(dev, &device->children, node) {

		status = acpi_video_bus_get_one_device(dev, video);
		if (ACPI_FAILURE(status)) {
			printk(KERN_WARNING PREFIX
					"Cant attach device");
			continue;
		}
	}
	return status;
}

static int acpi_video_bus_put_one_device(struct acpi_video_device *device)
{
	acpi_status status;
	struct acpi_video_bus *video;


	if (!device || !device->video)
		return -ENOENT;

	video = device->video;

	acpi_video_device_remove_fs(device->dev);

	status = acpi_remove_notify_handler(device->dev->handle,
					    ACPI_DEVICE_NOTIFY,
					    acpi_video_device_notify);
	backlight_device_unregister(device->backlight);
	if (device->cdev) {
		sysfs_remove_link(&device->dev->dev.kobj,
				  "thermal_cooling");
		sysfs_remove_link(&device->cdev->device.kobj,
				  "device");
		thermal_cooling_device_unregister(device->cdev);
		device->cdev = NULL;
	}
	video_output_unregister(device->output_dev);

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
	int keycode;

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
		keycode = KEY_UNKNOWN;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	acpi_notifier_call_chain(device, event, 0);
	input_report_key(input, keycode, 1);
	input_sync(input);
	input_report_key(input, keycode, 0);
	input_sync(input);

	return;
}

static void acpi_video_device_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_video_device *video_device = data;
	struct acpi_device *device = NULL;
	struct acpi_video_bus *bus;
	struct input_dev *input;
	int keycode;

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
	case ACPI_VIDEO_NOTIFY_ZERO_BRIGHTNESS:	/* zero brightnesss */
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
		keycode = KEY_UNKNOWN;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	acpi_notifier_call_chain(device, event, 0);
	input_report_key(input, keycode, 1);
	input_sync(input);
	input_report_key(input, keycode, 0);
	input_sync(input);

	return;
}

static int instance;
static int acpi_video_resume(struct acpi_device *device)
{
	struct acpi_video_bus *video;
	struct acpi_video_device *video_device;
	int i;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	video = acpi_driver_data(device);

	for (i = 0; i < video->attached_count; i++) {
		video_device = video->attached_array[i].bind_info;
		if (video_device && video_device->backlight)
			acpi_video_set_brightness(video_device->backlight);
	}
	return AE_OK;
}

static int acpi_video_bus_add(struct acpi_device *device)
{
	struct acpi_video_bus *video;
	struct input_dev *input;
	int error;

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

	error = acpi_video_bus_add_fs(device);
	if (error)
		goto err_free_video;

	mutex_init(&video->device_list_lock);
	INIT_LIST_HEAD(&video->video_device_list);

	acpi_video_bus_get_devices(video, device);
	acpi_video_bus_start_devices(video);

	video->input = input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_stop_video;
	}

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
	set_bit(KEY_UNKNOWN, input->keybit);

	error = input_register_device(input);
	if (error)
		goto err_free_input_dev;

	printk(KERN_INFO PREFIX "%s [%s] (multi-head: %s  rom: %s  post: %s)\n",
	       ACPI_VIDEO_DEVICE_NAME, acpi_device_bid(device),
	       video->flags.multihead ? "yes" : "no",
	       video->flags.rom ? "yes" : "no",
	       video->flags.post ? "yes" : "no");

	return 0;

 err_free_input_dev:
	input_free_device(input);
 err_stop_video:
	acpi_video_bus_stop_devices(video);
	acpi_video_bus_put_devices(video);
	kfree(video->attached_array);
	acpi_video_bus_remove_fs(device);
 err_free_video:
	kfree(video);
	device->driver_data = NULL;

	return error;
}

static int acpi_video_bus_remove(struct acpi_device *device, int type)
{
	struct acpi_video_bus *video = NULL;


	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	video = acpi_driver_data(device);

	acpi_video_bus_stop_devices(video);
	acpi_video_bus_put_devices(video);
	acpi_video_bus_remove_fs(device);

	input_unregister_device(video->input);
	kfree(video->attached_array);
	kfree(video);

	return 0;
}

static int __init intel_opregion_present(void)
{
#if defined(CONFIG_DRM_I915) || defined(CONFIG_DRM_I915_MODULE)
	struct pci_dev *dev = NULL;
	u32 address;

	for_each_pci_dev(dev) {
		if ((dev->class >> 8) != PCI_CLASS_DISPLAY_VGA)
			continue;
		if (dev->vendor != PCI_VENDOR_ID_INTEL)
			continue;
		pci_read_config_dword(dev, 0xfc, &address);
		if (!address)
			continue;
		return 1;
	}
#endif
	return 0;
}

int acpi_video_register(void)
{
	int result = 0;

	acpi_video_dir = proc_mkdir(ACPI_VIDEO_CLASS, acpi_root_dir);
	if (!acpi_video_dir)
		return -ENODEV;

	result = acpi_bus_register_driver(&acpi_video_bus);
	if (result < 0) {
		remove_proc_entry(ACPI_VIDEO_CLASS, acpi_root_dir);
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL(acpi_video_register);

/*
 * This is kind of nasty. Hardware using Intel chipsets may require
 * the video opregion code to be run first in order to initialise
 * state before any ACPI video calls are made. To handle this we defer
 * registration of the video class until the opregion code has run.
 */

static int __init acpi_video_init(void)
{
	if (intel_opregion_present())
		return 0;

	return acpi_video_register();
}

static void __exit acpi_video_exit(void)
{

	acpi_bus_unregister_driver(&acpi_video_bus);

	remove_proc_entry(ACPI_VIDEO_CLASS, acpi_root_dir);

	return;
}

module_init(acpi_video_init);
module_exit(acpi_video_exit);
