/*
 *  video.c - ACPI Video Driver ($Revision:$)
 *
 *  Copyright (C) 2004 Luming Yu <luming.yu@intel.com>
 *  Copyright (C) 2004 Bruno Ducrot <ducrot@poupinou.org>
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
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define ACPI_VIDEO_COMPONENT		0x08000000
#define ACPI_VIDEO_CLASS		"video"
#define ACPI_VIDEO_DRIVER_NAME		"ACPI Video Driver"
#define ACPI_VIDEO_BUS_NAME		"Video Bus"
#define ACPI_VIDEO_DEVICE_NAME		"Video Device"
#define ACPI_VIDEO_NOTIFY_SWITCH	0x80
#define ACPI_VIDEO_NOTIFY_PROBE		0x81
#define ACPI_VIDEO_NOTIFY_CYCLE		0x82
#define ACPI_VIDEO_NOTIFY_NEXT_OUTPUT	0x83
#define ACPI_VIDEO_NOTIFY_PREV_OUTPUT	0x84

#define ACPI_VIDEO_NOTIFY_CYCLE_BRIGHTNESS	0x82
#define	ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS	0x83
#define ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS	0x84
#define ACPI_VIDEO_NOTIFY_ZERO_BRIGHTNESS	0x85
#define ACPI_VIDEO_NOTIFY_DISPLAY_OFF		0x86

#define ACPI_VIDEO_HEAD_INVALID		(~0u - 1)
#define ACPI_VIDEO_HEAD_END		(~0u)

#define _COMPONENT		ACPI_VIDEO_COMPONENT
ACPI_MODULE_NAME("acpi_video")

    MODULE_AUTHOR("Bruno Ducrot");
MODULE_DESCRIPTION(ACPI_VIDEO_DRIVER_NAME);
MODULE_LICENSE("GPL");

static int acpi_video_bus_add(struct acpi_device *device);
static int acpi_video_bus_remove(struct acpi_device *device, int type);
static int acpi_video_bus_match(struct acpi_device *device,
				struct acpi_driver *driver);

static struct acpi_driver acpi_video_bus = {
	.name = ACPI_VIDEO_DRIVER_NAME,
	.class = ACPI_VIDEO_CLASS,
	.ops = {
		.add = acpi_video_bus_add,
		.remove = acpi_video_bus_remove,
		.match = acpi_video_bus_match,
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
	u32 display_port_attachment:4;	/*This field differenates displays type */
	u32 display_type:4;	/*Describe the specific type in use */
	u32 vendor_specific:4;	/*Chipset Vendor Specifi */
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
	acpi_handle handle;
	u8 dos_setting;
	struct acpi_video_enumerated_device *attached_array;
	u8 attached_count;
	struct acpi_video_bus_cap cap;
	struct acpi_video_bus_flags flags;
	struct semaphore sem;
	struct list_head video_device_list;
	struct proc_dir_entry *dir;
};

struct acpi_video_device_flags {
	u8 crt:1;
	u8 lcd:1;
	u8 tvout:1;
	u8 bios:1;
	u8 unknown:1;
	u8 reserved:3;
};

struct acpi_video_device_cap {
	u8 _ADR:1;		/*Return the unique ID */
	u8 _BCL:1;		/*Query list of brightness control levels supported */
	u8 _BCM:1;		/*Set the brightness level */
	u8 _DDC:1;		/*Return the EDID for this device */
	u8 _DCS:1;		/*Return status of output device */
	u8 _DGS:1;		/*Query graphics state */
	u8 _DSS:1;		/*Device state set */
	u8 _reserved:1;
};

struct acpi_video_device_brightness {
	int curr;
	int count;
	int *levels;
};

struct acpi_video_device {
	acpi_handle handle;
	unsigned long device_id;
	struct acpi_video_device_flags flags;
	struct acpi_video_device_cap cap;
	struct list_head entry;
	struct acpi_video_bus *video;
	struct acpi_device *dev;
	struct acpi_video_device_brightness *brightness;
};

/* bus */
static int acpi_video_bus_info_open_fs(struct inode *inode, struct file *file);
static struct file_operations acpi_video_bus_info_fops = {
	.open = acpi_video_bus_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_bus_ROM_open_fs(struct inode *inode, struct file *file);
static struct file_operations acpi_video_bus_ROM_fops = {
	.open = acpi_video_bus_ROM_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_bus_POST_info_open_fs(struct inode *inode,
					    struct file *file);
static struct file_operations acpi_video_bus_POST_info_fops = {
	.open = acpi_video_bus_POST_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_bus_POST_open_fs(struct inode *inode, struct file *file);
static struct file_operations acpi_video_bus_POST_fops = {
	.open = acpi_video_bus_POST_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_bus_DOS_open_fs(struct inode *inode, struct file *file);
static struct file_operations acpi_video_bus_DOS_fops = {
	.open = acpi_video_bus_DOS_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* device */
static int acpi_video_device_info_open_fs(struct inode *inode,
					  struct file *file);
static struct file_operations acpi_video_device_info_fops = {
	.open = acpi_video_device_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_device_state_open_fs(struct inode *inode,
					   struct file *file);
static struct file_operations acpi_video_device_state_fops = {
	.open = acpi_video_device_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_device_brightness_open_fs(struct inode *inode,
						struct file *file);
static struct file_operations acpi_video_device_brightness_fops = {
	.open = acpi_video_device_brightness_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int acpi_video_device_EDID_open_fs(struct inode *inode,
					  struct file *file);
static struct file_operations acpi_video_device_EDID_fops = {
	.open = acpi_video_device_EDID_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static char device_decode[][30] = {
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
static int acpi_video_switch_output(struct acpi_video_bus *video, int event);
static int acpi_video_get_next_level(struct acpi_video_device *device,
				     u32 level_current, u32 event);
static void acpi_video_switch_brightness(struct acpi_video_device *device,
					 int event);

/* --------------------------------------------------------------------------
                               Video Management
   -------------------------------------------------------------------------- */

/* device */

static int
acpi_video_device_query(struct acpi_video_device *device, unsigned long *state)
{
	int status;
	ACPI_FUNCTION_TRACE("acpi_video_device_query");
	status = acpi_evaluate_integer(device->handle, "_DGS", NULL, state);

	return_VALUE(status);
}

static int
acpi_video_device_get_state(struct acpi_video_device *device,
			    unsigned long *state)
{
	int status;

	ACPI_FUNCTION_TRACE("acpi_video_device_get_state");

	status = acpi_evaluate_integer(device->handle, "_DCS", NULL, state);

	return_VALUE(status);
}

static int
acpi_video_device_set_state(struct acpi_video_device *device, int state)
{
	int status;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };
	unsigned long ret;

	ACPI_FUNCTION_TRACE("acpi_video_device_set_state");

	arg0.integer.value = state;
	status = acpi_evaluate_integer(device->handle, "_DSS", &args, &ret);

	return_VALUE(status);
}

static int
acpi_video_device_lcd_query_levels(struct acpi_video_device *device,
				   union acpi_object **levels)
{
	int status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;

	ACPI_FUNCTION_TRACE("acpi_video_device_lcd_query_levels");

	*levels = NULL;

	status = acpi_evaluate_object(device->handle, "_BCL", NULL, &buffer);
	if (!ACPI_SUCCESS(status))
		return_VALUE(status);
	obj = (union acpi_object *)buffer.pointer;
	if (!obj && (obj->type != ACPI_TYPE_PACKAGE)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _BCL data\n"));
		status = -EFAULT;
		goto err;
	}

	*levels = obj;

	return_VALUE(0);

      err:
	kfree(buffer.pointer);

	return_VALUE(status);
}

static int
acpi_video_device_lcd_set_level(struct acpi_video_device *device, int level)
{
	int status;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };

	ACPI_FUNCTION_TRACE("acpi_video_device_lcd_set_level");

	arg0.integer.value = level;
	status = acpi_evaluate_object(device->handle, "_BCM", &args, NULL);

	printk(KERN_DEBUG "set_level status: %x\n", status);
	return_VALUE(status);
}

static int
acpi_video_device_lcd_get_level_current(struct acpi_video_device *device,
					unsigned long *level)
{
	int status;
	ACPI_FUNCTION_TRACE("acpi_video_device_lcd_get_level_current");

	status = acpi_evaluate_integer(device->handle, "_BQC", NULL, level);

	return_VALUE(status);
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

	ACPI_FUNCTION_TRACE("acpi_video_device_get_EDID");

	*edid = NULL;

	if (!device)
		return_VALUE(-ENODEV);
	if (length == 128)
		arg0.integer.value = 1;
	else if (length == 256)
		arg0.integer.value = 2;
	else
		return_VALUE(-EINVAL);

	status = acpi_evaluate_object(device->handle, "_DDC", &args, &buffer);
	if (ACPI_FAILURE(status))
		return_VALUE(-ENODEV);

	obj = (union acpi_object *)buffer.pointer;

	if (obj && obj->type == ACPI_TYPE_BUFFER)
		*edid = obj;
	else {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _DDC data\n"));
		status = -EFAULT;
		kfree(obj);
	}

	return_VALUE(status);
}

/* bus */

static int
acpi_video_bus_set_POST(struct acpi_video_bus *video, unsigned long option)
{
	int status;
	unsigned long tmp;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };

	ACPI_FUNCTION_TRACE("acpi_video_bus_set_POST");

	arg0.integer.value = option;

	status = acpi_evaluate_integer(video->handle, "_SPD", &args, &tmp);
	if (ACPI_SUCCESS(status))
		status = tmp ? (-EINVAL) : (AE_OK);

	return_VALUE(status);
}

static int
acpi_video_bus_get_POST(struct acpi_video_bus *video, unsigned long *id)
{
	int status;

	ACPI_FUNCTION_TRACE("acpi_video_bus_get_POST");

	status = acpi_evaluate_integer(video->handle, "_GPD", NULL, id);

	return_VALUE(status);
}

static int
acpi_video_bus_POST_options(struct acpi_video_bus *video,
			    unsigned long *options)
{
	int status;
	ACPI_FUNCTION_TRACE("acpi_video_bus_POST_options");

	status = acpi_evaluate_integer(video->handle, "_VPO", NULL, options);
	*options &= 3;

	return_VALUE(status);
}

/*
 *  Arg:
 *  	video		: video bus device pointer
 *	bios_flag	: 
 *		0.	The system BIOS should NOT automatically switch(toggle)
 *			the active display output.
 *		1.	The system BIOS should automatically switch (toggle) the
 *			active display output. No swich event.
 *		2.	The _DGS value should be locked.
 *		3.	The system BIOS should not automatically switch (toggle) the
 *			active display output, but instead generate the display switch
 *			event notify code.
 *	lcd_flag	:
 *		0.	The system BIOS should automatically control the brightness level
 *			of the LCD, when the power changes from AC to DC
 *		1. 	The system BIOS should NOT automatically control the brightness 
 *			level of the LCD, when the power changes from AC to DC.
 * Return Value:
 * 		-1	wrong arg.
 */

static int
acpi_video_bus_DOS(struct acpi_video_bus *video, int bios_flag, int lcd_flag)
{
	acpi_integer status = 0;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };

	ACPI_FUNCTION_TRACE("acpi_video_bus_DOS");

	if (bios_flag < 0 || bios_flag > 3 || lcd_flag < 0 || lcd_flag > 1) {
		status = -1;
		goto Failed;
	}
	arg0.integer.value = (lcd_flag << 2) | bios_flag;
	video->dos_setting = arg0.integer.value;
	acpi_evaluate_object(video->handle, "_DOS", &args, NULL);

      Failed:
	return_VALUE(status);
}

/*
 *  Arg:	
 *  	device	: video output device (LCD, CRT, ..)
 *
 *  Return Value:
 *  	None
 *
 *  Find out all required AML method defined under the output
 *  device.
 */

static void acpi_video_device_find_cap(struct acpi_video_device *device)
{
	acpi_integer status;
	acpi_handle h_dummy1;
	int i;
	union acpi_object *obj = NULL;
	struct acpi_video_device_brightness *br = NULL;

	ACPI_FUNCTION_TRACE("acpi_video_device_find_cap");

	memset(&device->cap, 0, 4);

	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_ADR", &h_dummy1))) {
		device->cap._ADR = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_BCL", &h_dummy1))) {
		device->cap._BCL = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_BCM", &h_dummy1))) {
		device->cap._BCM = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_DDC", &h_dummy1))) {
		device->cap._DDC = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_DCS", &h_dummy1))) {
		device->cap._DCS = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_DGS", &h_dummy1))) {
		device->cap._DGS = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_DSS", &h_dummy1))) {
		device->cap._DSS = 1;
	}

	status = acpi_video_device_lcd_query_levels(device, &obj);

	if (obj && obj->type == ACPI_TYPE_PACKAGE && obj->package.count >= 2) {
		int count = 0;
		union acpi_object *o;

		br = kmalloc(sizeof(*br), GFP_KERNEL);
		if (!br) {
			printk(KERN_ERR "can't allocate memory\n");
		} else {
			memset(br, 0, sizeof(*br));
			br->levels = kmalloc(obj->package.count *
					     sizeof *(br->levels), GFP_KERNEL);
			if (!br->levels)
				goto out;

			for (i = 0; i < obj->package.count; i++) {
				o = (union acpi_object *)&obj->package.
				    elements[i];
				if (o->type != ACPI_TYPE_INTEGER) {
					ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
							  "Invalid data\n"));
					continue;
				}
				br->levels[count] = (u32) o->integer.value;
				count++;
			}
		      out:
			if (count < 2) {
				kfree(br->levels);
				kfree(br);
			} else {
				br->count = count;
				device->brightness = br;
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "found %d brightness levels\n",
						  count));
			}
		}
	}

	kfree(obj);

	return_VOID;
}

/*
 *  Arg:	
 *  	device	: video output device (VGA)
 *
 *  Return Value:
 *  	None
 *
 *  Find out all required AML method defined under the video bus device.
 */

static void acpi_video_bus_find_cap(struct acpi_video_bus *video)
{
	acpi_handle h_dummy1;

	memset(&video->cap, 0, 4);
	if (ACPI_SUCCESS(acpi_get_handle(video->handle, "_DOS", &h_dummy1))) {
		video->cap._DOS = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->handle, "_DOD", &h_dummy1))) {
		video->cap._DOD = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->handle, "_ROM", &h_dummy1))) {
		video->cap._ROM = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->handle, "_GPD", &h_dummy1))) {
		video->cap._GPD = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->handle, "_SPD", &h_dummy1))) {
		video->cap._SPD = 1;
	}
	if (ACPI_SUCCESS(acpi_get_handle(video->handle, "_VPO", &h_dummy1))) {
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

	ACPI_FUNCTION_TRACE("acpi_video_bus_check");

	if (!video)
		return_VALUE(-EINVAL);

	/* Since there is no HID, CID and so on for VGA driver, we have
	 * to check well known required nodes.
	 */

	/* Does this device able to support video switching ? */
	if (video->cap._DOS) {
		video->flags.multihead = 1;
		status = 0;
	}

	/* Does this device able to retrieve a retrieve a video ROM ? */
	if (video->cap._ROM) {
		video->flags.rom = 1;
		status = 0;
	}

	/* Does this device able to configure which video device to POST ? */
	if (video->cap._GPD && video->cap._SPD && video->cap._VPO) {
		video->flags.post = 1;
		status = 0;
	}

	return_VALUE(status);
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_video_dir;

/* video devices */

static int acpi_video_device_info_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_device *dev =
	    (struct acpi_video_device *)seq->private;

	ACPI_FUNCTION_TRACE("acpi_video_device_info_seq_show");

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
	else
		seq_printf(seq, "UNKNOWN\n");

	seq_printf(seq, "known by bios: %s\n", dev->flags.bios ? "yes" : "no");

      end:
	return_VALUE(0);
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
	struct acpi_video_device *dev =
	    (struct acpi_video_device *)seq->private;
	unsigned long state;

	ACPI_FUNCTION_TRACE("acpi_video_device_state_seq_show");

	if (!dev)
		goto end;

	status = acpi_video_device_get_state(dev, &state);
	seq_printf(seq, "state:     ");
	if (ACPI_SUCCESS(status))
		seq_printf(seq, "0x%02lx\n", state);
	else
		seq_printf(seq, "<not supported>\n");

	status = acpi_video_device_query(dev, &state);
	seq_printf(seq, "query:     ");
	if (ACPI_SUCCESS(status))
		seq_printf(seq, "0x%02lx\n", state);
	else
		seq_printf(seq, "<not supported>\n");

      end:
	return_VALUE(0);
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
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct acpi_video_device *dev = (struct acpi_video_device *)m->private;
	char str[12] = { 0 };
	u32 state = 0;

	ACPI_FUNCTION_TRACE("acpi_video_device_write_state");

	if (!dev || count + 1 > sizeof str)
		return_VALUE(-EINVAL);

	if (copy_from_user(str, buffer, count))
		return_VALUE(-EFAULT);

	str[count] = 0;
	state = simple_strtoul(str, NULL, 0);
	state &= ((1ul << 31) | (1ul << 30) | (1ul << 0));

	status = acpi_video_device_set_state(dev, state);

	if (status)
		return_VALUE(-EFAULT);

	return_VALUE(count);
}

static int
acpi_video_device_brightness_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_device *dev =
	    (struct acpi_video_device *)seq->private;
	int i;

	ACPI_FUNCTION_TRACE("acpi_video_device_brightness_seq_show");

	if (!dev || !dev->brightness) {
		seq_printf(seq, "<not supported>\n");
		return_VALUE(0);
	}

	seq_printf(seq, "levels: ");
	for (i = 0; i < dev->brightness->count; i++)
		seq_printf(seq, " %d", dev->brightness->levels[i]);
	seq_printf(seq, "\ncurrent: %d\n", dev->brightness->curr);

	return_VALUE(0);
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
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct acpi_video_device *dev = (struct acpi_video_device *)m->private;
	char str[4] = { 0 };
	unsigned int level = 0;
	int i;

	ACPI_FUNCTION_TRACE("acpi_video_device_write_brightness");

	if (!dev || !dev->brightness || count + 1 > sizeof str)
		return_VALUE(-EINVAL);

	if (copy_from_user(str, buffer, count))
		return_VALUE(-EFAULT);

	str[count] = 0;
	level = simple_strtoul(str, NULL, 0);

	if (level > 100)
		return_VALUE(-EFAULT);

	/* validate though the list of available levels */
	for (i = 0; i < dev->brightness->count; i++)
		if (level == dev->brightness->levels[i]) {
			if (ACPI_SUCCESS
			    (acpi_video_device_lcd_set_level(dev, level)))
				dev->brightness->curr = level;
			break;
		}

	return_VALUE(count);
}

static int acpi_video_device_EDID_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_device *dev =
	    (struct acpi_video_device *)seq->private;
	int status;
	int i;
	union acpi_object *edid = NULL;

	ACPI_FUNCTION_TRACE("acpi_video_device_EDID_seq_show");

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

	return_VALUE(0);
}

static int
acpi_video_device_EDID_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_device_EDID_seq_show,
			   PDE(inode)->data);
}

static int acpi_video_device_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;
	struct acpi_video_device *vid_dev;

	ACPI_FUNCTION_TRACE("acpi_video_device_add_fs");

	if (!device)
		return_VALUE(-ENODEV);

	vid_dev = (struct acpi_video_device *)acpi_driver_data(device);
	if (!vid_dev)
		return_VALUE(-ENODEV);

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
						     vid_dev->video->dir);
		if (!acpi_device_dir(device))
			return_VALUE(-ENODEV);
		acpi_device_dir(device)->owner = THIS_MODULE;
	}

	/* 'info' [R] */
	entry = create_proc_entry("info", S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create 'info' fs entry\n"));
	else {
		entry->proc_fops = &acpi_video_device_info_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'state' [R/W] */
	entry =
	    create_proc_entry("state", S_IFREG | S_IRUGO | S_IWUSR,
			      acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create 'state' fs entry\n"));
	else {
		entry->proc_fops = &acpi_video_device_state_fops;
		entry->proc_fops->write = acpi_video_device_write_state;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'brightness' [R/W] */
	entry =
	    create_proc_entry("brightness", S_IFREG | S_IRUGO | S_IWUSR,
			      acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create 'brightness' fs entry\n"));
	else {
		entry->proc_fops = &acpi_video_device_brightness_fops;
		entry->proc_fops->write = acpi_video_device_write_brightness;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'EDID' [R] */
	entry = create_proc_entry("EDID", S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create 'brightness' fs entry\n"));
	else {
		entry->proc_fops = &acpi_video_device_EDID_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	return_VALUE(0);
}

static int acpi_video_device_remove_fs(struct acpi_device *device)
{
	struct acpi_video_device *vid_dev;
	ACPI_FUNCTION_TRACE("acpi_video_device_remove_fs");

	vid_dev = (struct acpi_video_device *)acpi_driver_data(device);
	if (!vid_dev || !vid_dev->video || !vid_dev->video->dir)
		return_VALUE(-ENODEV);

	if (acpi_device_dir(device)) {
		remove_proc_entry("info", acpi_device_dir(device));
		remove_proc_entry("state", acpi_device_dir(device));
		remove_proc_entry("brightness", acpi_device_dir(device));
		remove_proc_entry("EDID", acpi_device_dir(device));
		remove_proc_entry(acpi_device_bid(device), vid_dev->video->dir);
		acpi_device_dir(device) = NULL;
	}

	return_VALUE(0);
}

/* video bus */
static int acpi_video_bus_info_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = (struct acpi_video_bus *)seq->private;

	ACPI_FUNCTION_TRACE("acpi_video_bus_info_seq_show");

	if (!video)
		goto end;

	seq_printf(seq, "Switching heads:              %s\n",
		   video->flags.multihead ? "yes" : "no");
	seq_printf(seq, "Video ROM:                    %s\n",
		   video->flags.rom ? "yes" : "no");
	seq_printf(seq, "Device to be POSTed on boot:  %s\n",
		   video->flags.post ? "yes" : "no");

      end:
	return_VALUE(0);
}

static int acpi_video_bus_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_bus_info_seq_show,
			   PDE(inode)->data);
}

static int acpi_video_bus_ROM_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = (struct acpi_video_bus *)seq->private;

	ACPI_FUNCTION_TRACE("acpi_video_bus_ROM_seq_show");

	if (!video)
		goto end;

	printk(KERN_INFO PREFIX "Please implement %s\n", __FUNCTION__);
	seq_printf(seq, "<TODO>\n");

      end:
	return_VALUE(0);
}

static int acpi_video_bus_ROM_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_bus_ROM_seq_show, PDE(inode)->data);
}

static int acpi_video_bus_POST_info_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = (struct acpi_video_bus *)seq->private;
	unsigned long options;
	int status;

	ACPI_FUNCTION_TRACE("acpi_video_bus_POST_info_seq_show");

	if (!video)
		goto end;

	status = acpi_video_bus_POST_options(video, &options);
	if (ACPI_SUCCESS(status)) {
		if (!(options & 1)) {
			printk(KERN_WARNING PREFIX
			       "The motherboard VGA device is not listed as a possible POST device.\n");
			printk(KERN_WARNING PREFIX
			       "This indicate a BIOS bug.  Please contact the manufacturer.\n");
		}
		printk("%lx\n", options);
		seq_printf(seq, "can POST: <intgrated video>");
		if (options & 2)
			seq_printf(seq, " <PCI video>");
		if (options & 4)
			seq_printf(seq, " <AGP video>");
		seq_putc(seq, '\n');
	} else
		seq_printf(seq, "<not supported>\n");
      end:
	return_VALUE(0);
}

static int
acpi_video_bus_POST_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_video_bus_POST_info_seq_show,
			   PDE(inode)->data);
}

static int acpi_video_bus_POST_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = (struct acpi_video_bus *)seq->private;
	int status;
	unsigned long id;

	ACPI_FUNCTION_TRACE("acpi_video_bus_POST_seq_show");

	if (!video)
		goto end;

	status = acpi_video_bus_get_POST(video, &id);
	if (!ACPI_SUCCESS(status)) {
		seq_printf(seq, "<not supported>\n");
		goto end;
	}
	seq_printf(seq, "device posted is <%s>\n", device_decode[id & 3]);

      end:
	return_VALUE(0);
}

static int acpi_video_bus_DOS_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_video_bus *video = (struct acpi_video_bus *)seq->private;

	ACPI_FUNCTION_TRACE("acpi_video_bus_DOS_seq_show");

	seq_printf(seq, "DOS setting: <%d>\n", video->dos_setting);

	return_VALUE(0);
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
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct acpi_video_bus *video = (struct acpi_video_bus *)m->private;
	char str[12] = { 0 };
	unsigned long opt, options;

	ACPI_FUNCTION_TRACE("acpi_video_bus_write_POST");

	if (!video || count + 1 > sizeof str)
		return_VALUE(-EINVAL);

	status = acpi_video_bus_POST_options(video, &options);
	if (!ACPI_SUCCESS(status))
		return_VALUE(-EINVAL);

	if (copy_from_user(str, buffer, count))
		return_VALUE(-EFAULT);

	str[count] = 0;
	opt = strtoul(str, NULL, 0);
	if (opt > 3)
		return_VALUE(-EFAULT);

	/* just in case an OEM 'forget' the motherboard... */
	options |= 1;

	if (options & (1ul << opt)) {
		status = acpi_video_bus_set_POST(video, opt);
		if (!ACPI_SUCCESS(status))
			return_VALUE(-EFAULT);

	}

	return_VALUE(count);
}

static ssize_t
acpi_video_bus_write_DOS(struct file *file,
			 const char __user * buffer,
			 size_t count, loff_t * data)
{
	int status;
	struct seq_file *m = (struct seq_file *)file->private_data;
	struct acpi_video_bus *video = (struct acpi_video_bus *)m->private;
	char str[12] = { 0 };
	unsigned long opt;

	ACPI_FUNCTION_TRACE("acpi_video_bus_write_DOS");

	if (!video || count + 1 > sizeof str)
		return_VALUE(-EINVAL);

	if (copy_from_user(str, buffer, count))
		return_VALUE(-EFAULT);

	str[count] = 0;
	opt = strtoul(str, NULL, 0);
	if (opt > 7)
		return_VALUE(-EFAULT);

	status = acpi_video_bus_DOS(video, opt & 0x3, (opt & 0x4) >> 2);

	if (!ACPI_SUCCESS(status))
		return_VALUE(-EFAULT);

	return_VALUE(count);
}

static int acpi_video_bus_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;
	struct acpi_video_bus *video;

	ACPI_FUNCTION_TRACE("acpi_video_bus_add_fs");

	video = (struct acpi_video_bus *)acpi_driver_data(device);

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
						     acpi_video_dir);
		if (!acpi_device_dir(device))
			return_VALUE(-ENODEV);
		video->dir = acpi_device_dir(device);
		acpi_device_dir(device)->owner = THIS_MODULE;
	}

	/* 'info' [R] */
	entry = create_proc_entry("info", S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create 'info' fs entry\n"));
	else {
		entry->proc_fops = &acpi_video_bus_info_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'ROM' [R] */
	entry = create_proc_entry("ROM", S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create 'ROM' fs entry\n"));
	else {
		entry->proc_fops = &acpi_video_bus_ROM_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'POST_info' [R] */
	entry =
	    create_proc_entry("POST_info", S_IRUGO, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create 'POST_info' fs entry\n"));
	else {
		entry->proc_fops = &acpi_video_bus_POST_info_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'POST' [R/W] */
	entry =
	    create_proc_entry("POST", S_IFREG | S_IRUGO | S_IRUSR,
			      acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create 'POST' fs entry\n"));
	else {
		entry->proc_fops = &acpi_video_bus_POST_fops;
		entry->proc_fops->write = acpi_video_bus_write_POST;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'DOS' [R/W] */
	entry =
	    create_proc_entry("DOS", S_IFREG | S_IRUGO | S_IRUSR,
			      acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Unable to create 'DOS' fs entry\n"));
	else {
		entry->proc_fops = &acpi_video_bus_DOS_fops;
		entry->proc_fops->write = acpi_video_bus_write_DOS;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	return_VALUE(0);
}

static int acpi_video_bus_remove_fs(struct acpi_device *device)
{
	struct acpi_video_bus *video;

	ACPI_FUNCTION_TRACE("acpi_video_bus_remove_fs");

	video = (struct acpi_video_bus *)acpi_driver_data(device);

	if (acpi_device_dir(device)) {
		remove_proc_entry("info", acpi_device_dir(device));
		remove_proc_entry("ROM", acpi_device_dir(device));
		remove_proc_entry("POST_info", acpi_device_dir(device));
		remove_proc_entry("POST", acpi_device_dir(device));
		remove_proc_entry("DOS", acpi_device_dir(device));
		remove_proc_entry(acpi_device_bid(device), acpi_video_dir);
		acpi_device_dir(device) = NULL;
	}

	return_VALUE(0);
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

/* device interface */

static int
acpi_video_bus_get_one_device(struct acpi_device *device,
			      struct acpi_video_bus *video)
{
	unsigned long device_id;
	int status, result;
	struct acpi_video_device *data;

	ACPI_FUNCTION_TRACE("acpi_video_bus_get_one_device");

	if (!device || !video)
		return_VALUE(-EINVAL);

	status =
	    acpi_evaluate_integer(device->handle, "_ADR", NULL, &device_id);
	if (ACPI_SUCCESS(status)) {

		data = kmalloc(sizeof(struct acpi_video_device), GFP_KERNEL);
		if (!data)
			return_VALUE(-ENOMEM);

		memset(data, 0, sizeof(struct acpi_video_device));

		data->handle = device->handle;
		strcpy(acpi_device_name(device), ACPI_VIDEO_DEVICE_NAME);
		strcpy(acpi_device_class(device), ACPI_VIDEO_CLASS);
		acpi_driver_data(device) = data;

		data->device_id = device_id;
		data->video = video;
		data->dev = device;

		switch (device_id & 0xffff) {
		case 0x0100:
			data->flags.crt = 1;
			break;
		case 0x0400:
			data->flags.lcd = 1;
			break;
		case 0x0200:
			data->flags.tvout = 1;
			break;
		default:
			data->flags.unknown = 1;
			break;
		}

		acpi_video_device_bind(video, data);
		acpi_video_device_find_cap(data);

		status = acpi_install_notify_handler(data->handle,
						     ACPI_DEVICE_NOTIFY,
						     acpi_video_device_notify,
						     data);
		if (ACPI_FAILURE(status)) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Error installing notify handler\n"));
			result = -ENODEV;
			goto end;
		}

		down(&video->sem);
		list_add_tail(&data->entry, &video->video_device_list);
		up(&video->sem);

		acpi_video_device_add_fs(device);

		return_VALUE(0);
	}

      end:
	return_VALUE(-ENOENT);
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
	struct list_head *node, *next;
	list_for_each_safe(node, next, &video->video_device_list) {
		struct acpi_video_device *dev =
		    container_of(node, struct acpi_video_device, entry);
		acpi_video_device_bind(video, dev);
	}
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
	int i;
	ACPI_FUNCTION_TRACE("acpi_video_device_bind");

#define IDS_VAL(i) video->attached_array[i].value.int_val
#define IDS_BIND(i) video->attached_array[i].bind_info

	for (i = 0; IDS_VAL(i) != ACPI_VIDEO_HEAD_INVALID &&
	     i < video->attached_count; i++) {
		if (device->device_id == (IDS_VAL(i) & 0xffff)) {
			IDS_BIND(i) = device;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "device_bind %d\n", i));
		}
	}
#undef IDS_VAL
#undef IDS_BIND
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
	struct acpi_video_enumerated_device *active_device_list;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *dod = NULL;
	union acpi_object *obj;

	ACPI_FUNCTION_TRACE("acpi_video_device_enumerate");

	status = acpi_evaluate_object(video->handle, "_DOD", NULL, &buffer);
	if (!ACPI_SUCCESS(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _DOD\n"));
		return_VALUE(status);
	}

	dod = (union acpi_object *)buffer.pointer;
	if (!dod || (dod->type != ACPI_TYPE_PACKAGE)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _DOD data\n"));
		status = -EFAULT;
		goto out;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %d video heads in _DOD\n",
			  dod->package.count));

	active_device_list = kmalloc((1 +
				      dod->package.count) *
				     sizeof(struct
					    acpi_video_enumerated_device),
				     GFP_KERNEL);

	if (!active_device_list) {
		status = -ENOMEM;
		goto out;
	}

	count = 0;
	for (i = 0; i < dod->package.count; i++) {
		obj = (union acpi_object *)&dod->package.elements[i];

		if (obj->type != ACPI_TYPE_INTEGER) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Invalid _DOD data\n"));
			active_device_list[i].value.int_val =
			    ACPI_VIDEO_HEAD_INVALID;
		}
		active_device_list[i].value.int_val = obj->integer.value;
		active_device_list[i].bind_info = NULL;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "dod element[%d] = %d\n", i,
				  (int)obj->integer.value));
		count++;
	}
	active_device_list[count].value.int_val = ACPI_VIDEO_HEAD_END;

	kfree(video->attached_array);

	video->attached_array = active_device_list;
	video->attached_count = count;
      out:
	acpi_os_free(buffer.pointer);
	return_VALUE(status);
}

/*
 *  Arg:
 *  	video	: video bus device 
 *  	event	: Nontify Event
 *
 *  Return:
 *  	< 0	: error
 *  
 *	1. Find out the current active output device.
 *	2. Identify the next output device to switch
 *	3. call _DSS to do actual switch.
 */

static int acpi_video_switch_output(struct acpi_video_bus *video, int event)
{
	struct list_head *node, *next;
	struct acpi_video_device *dev = NULL;
	struct acpi_video_device *dev_next = NULL;
	struct acpi_video_device *dev_prev = NULL;
	unsigned long state;
	int status = 0;

	ACPI_FUNCTION_TRACE("acpi_video_switch_output");

	list_for_each_safe(node, next, &video->video_device_list) {
		dev = container_of(node, struct acpi_video_device, entry);
		status = acpi_video_device_get_state(dev, &state);
		if (state & 0x2) {
			dev_next =
			    container_of(node->next, struct acpi_video_device,
					 entry);
			dev_prev =
			    container_of(node->prev, struct acpi_video_device,
					 entry);
			goto out;
		}
	}
	dev_next = container_of(node->next, struct acpi_video_device, entry);
	dev_prev = container_of(node->prev, struct acpi_video_device, entry);
      out:
	switch (event) {
	case ACPI_VIDEO_NOTIFY_CYCLE:
	case ACPI_VIDEO_NOTIFY_NEXT_OUTPUT:
		acpi_video_device_set_state(dev, 0);
		acpi_video_device_set_state(dev_next, 0x80000001);
		break;
	case ACPI_VIDEO_NOTIFY_PREV_OUTPUT:
		acpi_video_device_set_state(dev, 0);
		acpi_video_device_set_state(dev_prev, 0x80000001);
	default:
		break;
	}

	return_VALUE(status);
}

static int
acpi_video_get_next_level(struct acpi_video_device *device,
			  u32 level_current, u32 event)
{
	/*Fix me */
	return level_current;
}

static void
acpi_video_switch_brightness(struct acpi_video_device *device, int event)
{
	unsigned long level_current, level_next;
	acpi_video_device_lcd_get_level_current(device, &level_current);
	level_next = acpi_video_get_next_level(device, level_current, event);
	acpi_video_device_lcd_set_level(device, level_next);
}

static int
acpi_video_bus_get_devices(struct acpi_video_bus *video,
			   struct acpi_device *device)
{
	int status = 0;
	struct list_head *node, *next;

	ACPI_FUNCTION_TRACE("acpi_video_get_devices");

	acpi_video_device_enumerate(video);

	list_for_each_safe(node, next, &device->children) {
		struct acpi_device *dev =
		    list_entry(node, struct acpi_device, node);

		if (!dev)
			continue;

		status = acpi_video_bus_get_one_device(dev, video);
		if (ACPI_FAILURE(status)) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "Cant attach device\n"));
			continue;
		}

	}
	return_VALUE(status);
}

static int acpi_video_bus_put_one_device(struct acpi_video_device *device)
{
	acpi_status status;
	struct acpi_video_bus *video;

	ACPI_FUNCTION_TRACE("acpi_video_bus_put_one_device");

	if (!device || !device->video)
		return_VALUE(-ENOENT);

	video = device->video;

	down(&video->sem);
	list_del(&device->entry);
	up(&video->sem);
	acpi_video_device_remove_fs(device->dev);

	status = acpi_remove_notify_handler(device->handle,
					    ACPI_DEVICE_NOTIFY,
					    acpi_video_device_notify);
	if (ACPI_FAILURE(status))
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error removing notify handler\n"));

	return_VALUE(0);
}

static int acpi_video_bus_put_devices(struct acpi_video_bus *video)
{
	int status;
	struct list_head *node, *next;

	ACPI_FUNCTION_TRACE("acpi_video_bus_put_devices");

	list_for_each_safe(node, next, &video->video_device_list) {
		struct acpi_video_device *data =
		    list_entry(node, struct acpi_video_device, entry);
		if (!data)
			continue;

		status = acpi_video_bus_put_one_device(data);
		if (ACPI_FAILURE(status))
			printk(KERN_WARNING PREFIX
			       "hhuuhhuu bug in acpi video driver.\n");

		kfree(data->brightness);

		kfree(data);
	}

	return_VALUE(0);
}

/* acpi_video interface */

static int acpi_video_bus_start_devices(struct acpi_video_bus *video)
{
	return acpi_video_bus_DOS(video, 1, 0);
}

static int acpi_video_bus_stop_devices(struct acpi_video_bus *video)
{
	return acpi_video_bus_DOS(video, 0, 1);
}

static void acpi_video_bus_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_video_bus *video = (struct acpi_video_bus *)data;
	struct acpi_device *device = NULL;

	ACPI_FUNCTION_TRACE("acpi_video_bus_notify");
	printk("video bus notify\n");

	if (!video)
		return_VOID;

	if (acpi_bus_get_device(handle, &device))
		return_VOID;

	switch (event) {
	case ACPI_VIDEO_NOTIFY_SWITCH:	/* User request that a switch occur,
					 * most likely via hotkey. */
		acpi_bus_generate_event(device, event, 0);
		break;

	case ACPI_VIDEO_NOTIFY_PROBE:	/* User plug or remove a video
					 * connector. */
		acpi_video_device_enumerate(video);
		acpi_video_device_rebind(video);
		acpi_video_switch_output(video, event);
		acpi_bus_generate_event(device, event, 0);
		break;

	case ACPI_VIDEO_NOTIFY_CYCLE:	/* Cycle Display output hotkey pressed. */
	case ACPI_VIDEO_NOTIFY_NEXT_OUTPUT:	/* Next Display output hotkey pressed. */
	case ACPI_VIDEO_NOTIFY_PREV_OUTPUT:	/* previous Display output hotkey pressed. */
		acpi_video_switch_output(video, event);
		acpi_bus_generate_event(device, event, 0);
		break;

	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	return_VOID;
}

static void acpi_video_device_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_video_device *video_device =
	    (struct acpi_video_device *)data;
	struct acpi_device *device = NULL;

	ACPI_FUNCTION_TRACE("acpi_video_device_notify");

	printk("video device notify\n");
	if (!video_device)
		return_VOID;

	if (acpi_bus_get_device(handle, &device))
		return_VOID;

	switch (event) {
	case ACPI_VIDEO_NOTIFY_SWITCH:	/* change in status (cycle output device) */
	case ACPI_VIDEO_NOTIFY_PROBE:	/* change in status (output device status) */
		acpi_bus_generate_event(device, event, 0);
		break;
	case ACPI_VIDEO_NOTIFY_CYCLE_BRIGHTNESS:	/* Cycle brightness */
	case ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS:	/* Increase brightness */
	case ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS:	/* Decrease brightness */
	case ACPI_VIDEO_NOTIFY_ZERO_BRIGHTNESS:	/* zero brightnesss */
	case ACPI_VIDEO_NOTIFY_DISPLAY_OFF:	/* display device off */
		acpi_video_switch_brightness(video_device, event);
		acpi_bus_generate_event(device, event, 0);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}
	return_VOID;
}

static int acpi_video_bus_add(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = 0;
	struct acpi_video_bus *video = NULL;

	ACPI_FUNCTION_TRACE("acpi_video_bus_add");

	if (!device)
		return_VALUE(-EINVAL);

	video = kmalloc(sizeof(struct acpi_video_bus), GFP_KERNEL);
	if (!video)
		return_VALUE(-ENOMEM);
	memset(video, 0, sizeof(struct acpi_video_bus));

	video->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_VIDEO_BUS_NAME);
	strcpy(acpi_device_class(device), ACPI_VIDEO_CLASS);
	acpi_driver_data(device) = video;

	acpi_video_bus_find_cap(video);
	result = acpi_video_bus_check(video);
	if (result)
		goto end;

	result = acpi_video_bus_add_fs(device);
	if (result)
		goto end;

	init_MUTEX(&video->sem);
	INIT_LIST_HEAD(&video->video_device_list);

	acpi_video_bus_get_devices(video, device);
	acpi_video_bus_start_devices(video);

	status = acpi_install_notify_handler(video->handle,
					     ACPI_DEVICE_NOTIFY,
					     acpi_video_bus_notify, video);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error installing notify handler\n"));
		result = -ENODEV;
		goto end;
	}

	printk(KERN_INFO PREFIX "%s [%s] (multi-head: %s  rom: %s  post: %s)\n",
	       ACPI_VIDEO_DEVICE_NAME, acpi_device_bid(device),
	       video->flags.multihead ? "yes" : "no",
	       video->flags.rom ? "yes" : "no",
	       video->flags.post ? "yes" : "no");

      end:
	if (result) {
		acpi_video_bus_remove_fs(device);
		kfree(video);
	}

	return_VALUE(result);
}

static int acpi_video_bus_remove(struct acpi_device *device, int type)
{
	acpi_status status = 0;
	struct acpi_video_bus *video = NULL;

	ACPI_FUNCTION_TRACE("acpi_video_bus_remove");

	if (!device || !acpi_driver_data(device))
		return_VALUE(-EINVAL);

	video = (struct acpi_video_bus *)acpi_driver_data(device);

	acpi_video_bus_stop_devices(video);

	status = acpi_remove_notify_handler(video->handle,
					    ACPI_DEVICE_NOTIFY,
					    acpi_video_bus_notify);
	if (ACPI_FAILURE(status))
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "Error removing notify handler\n"));

	acpi_video_bus_put_devices(video);
	acpi_video_bus_remove_fs(device);

	kfree(video->attached_array);
	kfree(video);

	return_VALUE(0);
}

static int
acpi_video_bus_match(struct acpi_device *device, struct acpi_driver *driver)
{
	acpi_handle h_dummy1;
	acpi_handle h_dummy2;
	acpi_handle h_dummy3;

	ACPI_FUNCTION_TRACE("acpi_video_bus_match");

	if (!device || !driver)
		return_VALUE(-EINVAL);

	/* Since there is no HID, CID for ACPI Video drivers, we have
	 * to check well known required nodes for each feature we support.
	 */

	/* Does this device able to support video switching ? */
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_DOD", &h_dummy1)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "_DOS", &h_dummy2)))
		return_VALUE(0);

	/* Does this device able to retrieve a video ROM ? */
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_ROM", &h_dummy1)))
		return_VALUE(0);

	/* Does this device able to configure which video head to be POSTed ? */
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_VPO", &h_dummy1)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "_GPD", &h_dummy2)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "_SPD", &h_dummy3)))
		return_VALUE(0);

	return_VALUE(-ENODEV);
}

static int __init acpi_video_init(void)
{
	int result = 0;

	ACPI_FUNCTION_TRACE("acpi_video_init");

	/*
	   acpi_dbg_level = 0xFFFFFFFF;
	   acpi_dbg_layer = 0x08000000;
	 */

	acpi_video_dir = proc_mkdir(ACPI_VIDEO_CLASS, acpi_root_dir);
	if (!acpi_video_dir)
		return_VALUE(-ENODEV);
	acpi_video_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&acpi_video_bus);
	if (result < 0) {
		remove_proc_entry(ACPI_VIDEO_CLASS, acpi_root_dir);
		return_VALUE(-ENODEV);
	}

	return_VALUE(0);
}

static void __exit acpi_video_exit(void)
{
	ACPI_FUNCTION_TRACE("acpi_video_exit");

	acpi_bus_unregister_driver(&acpi_video_bus);

	remove_proc_entry(ACPI_VIDEO_CLASS, acpi_root_dir);

	return_VOID;
}

module_init(acpi_video_init);
module_exit(acpi_video_exit);
