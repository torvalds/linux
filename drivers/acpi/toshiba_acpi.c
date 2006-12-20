/*
 *  toshiba_acpi.c - Toshiba Laptop ACPI Extras
 *
 *
 *  Copyright (C) 2002-2004 John Belmonte
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *  The devolpment page for this driver is located at
 *  http://memebeam.org/toys/ToshibaAcpiDriver.
 *
 *  Credits:
 *	Jonathan A. Buzzard - Toshiba HCI info, and critical tips on reverse
 *		engineering the Windows drivers
 *	Yasushi Nagato - changes for linux kernel 2.4 -> 2.5
 *	Rob Miller - TV out and hotkeys help
 *
 *
 *  TODO
 *
 */

#define TOSHIBA_ACPI_VERSION	"0.18"
#define PROC_INTERFACE_VERSION	1

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/backlight.h>

#include <asm/uaccess.h>

#include <acpi/acpi_drivers.h>

MODULE_AUTHOR("John Belmonte");
MODULE_DESCRIPTION("Toshiba Laptop ACPI Extras Driver");
MODULE_LICENSE("GPL");

#define MY_LOGPREFIX "toshiba_acpi: "
#define MY_ERR KERN_ERR MY_LOGPREFIX
#define MY_NOTICE KERN_NOTICE MY_LOGPREFIX
#define MY_INFO KERN_INFO MY_LOGPREFIX

/* Toshiba ACPI method paths */
#define METHOD_LCD_BRIGHTNESS	"\\_SB_.PCI0.VGA_.LCD_._BCM"
#define METHOD_HCI_1		"\\_SB_.VALD.GHCI"
#define METHOD_HCI_2		"\\_SB_.VALZ.GHCI"
#define METHOD_VIDEO_OUT	"\\_SB_.VALX.DSSX"

/* Toshiba HCI interface definitions
 *
 * HCI is Toshiba's "Hardware Control Interface" which is supposed to
 * be uniform across all their models.  Ideally we would just call
 * dedicated ACPI methods instead of using this primitive interface.
 * However the ACPI methods seem to be incomplete in some areas (for
 * example they allow setting, but not reading, the LCD brightness value),
 * so this is still useful.
 */

#define HCI_WORDS			6

/* operations */
#define HCI_SET				0xff00
#define HCI_GET				0xfe00

/* return codes */
#define HCI_SUCCESS			0x0000
#define HCI_FAILURE			0x1000
#define HCI_NOT_SUPPORTED		0x8000
#define HCI_EMPTY			0x8c00

/* registers */
#define HCI_FAN				0x0004
#define HCI_SYSTEM_EVENT		0x0016
#define HCI_VIDEO_OUT			0x001c
#define HCI_HOTKEY_EVENT		0x001e
#define HCI_LCD_BRIGHTNESS		0x002a

/* field definitions */
#define HCI_LCD_BRIGHTNESS_BITS		3
#define HCI_LCD_BRIGHTNESS_SHIFT	(16-HCI_LCD_BRIGHTNESS_BITS)
#define HCI_LCD_BRIGHTNESS_LEVELS	(1 << HCI_LCD_BRIGHTNESS_BITS)
#define HCI_VIDEO_OUT_LCD		0x1
#define HCI_VIDEO_OUT_CRT		0x2
#define HCI_VIDEO_OUT_TV		0x4

/* utility
 */

static __inline__ void _set_bit(u32 * word, u32 mask, int value)
{
	*word = (*word & ~mask) | (mask * value);
}

/* acpi interface wrappers
 */

static int is_valid_acpi_path(const char *methodName)
{
	acpi_handle handle;
	acpi_status status;

	status = acpi_get_handle(NULL, (char *)methodName, &handle);
	return !ACPI_FAILURE(status);
}

static int write_acpi_int(const char *methodName, int val)
{
	struct acpi_object_list params;
	union acpi_object in_objs[1];
	acpi_status status;

	params.count = sizeof(in_objs) / sizeof(in_objs[0]);
	params.pointer = in_objs;
	in_objs[0].type = ACPI_TYPE_INTEGER;
	in_objs[0].integer.value = val;

	status = acpi_evaluate_object(NULL, (char *)methodName, &params, NULL);
	return (status == AE_OK);
}

#if 0
static int read_acpi_int(const char *methodName, int *pVal)
{
	struct acpi_buffer results;
	union acpi_object out_objs[1];
	acpi_status status;

	results.length = sizeof(out_objs);
	results.pointer = out_objs;

	status = acpi_evaluate_object(0, (char *)methodName, 0, &results);
	*pVal = out_objs[0].integer.value;

	return (status == AE_OK) && (out_objs[0].type == ACPI_TYPE_INTEGER);
}
#endif

static const char *method_hci /*= 0*/ ;

/* Perform a raw HCI call.  Here we don't care about input or output buffer
 * format.
 */
static acpi_status hci_raw(const u32 in[HCI_WORDS], u32 out[HCI_WORDS])
{
	struct acpi_object_list params;
	union acpi_object in_objs[HCI_WORDS];
	struct acpi_buffer results;
	union acpi_object out_objs[HCI_WORDS + 1];
	acpi_status status;
	int i;

	params.count = HCI_WORDS;
	params.pointer = in_objs;
	for (i = 0; i < HCI_WORDS; ++i) {
		in_objs[i].type = ACPI_TYPE_INTEGER;
		in_objs[i].integer.value = in[i];
	}

	results.length = sizeof(out_objs);
	results.pointer = out_objs;

	status = acpi_evaluate_object(NULL, (char *)method_hci, &params,
				      &results);
	if ((status == AE_OK) && (out_objs->package.count <= HCI_WORDS)) {
		for (i = 0; i < out_objs->package.count; ++i) {
			out[i] = out_objs->package.elements[i].integer.value;
		}
	}

	return status;
}

/* common hci tasks (get or set one value)
 *
 * In addition to the ACPI status, the HCI system returns a result which
 * may be useful (such as "not supported").
 */

static acpi_status hci_write1(u32 reg, u32 in1, u32 * result)
{
	u32 in[HCI_WORDS] = { HCI_SET, reg, in1, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status = hci_raw(in, out);
	*result = (status == AE_OK) ? out[0] : HCI_FAILURE;
	return status;
}

static acpi_status hci_read1(u32 reg, u32 * out1, u32 * result)
{
	u32 in[HCI_WORDS] = { HCI_GET, reg, 0, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status = hci_raw(in, out);
	*out1 = out[2];
	*result = (status == AE_OK) ? out[0] : HCI_FAILURE;
	return status;
}

static struct proc_dir_entry *toshiba_proc_dir /*= 0*/ ;
static struct backlight_device *toshiba_backlight_device;
static int force_fan;
static int last_key_event;
static int key_event_valid;

typedef struct _ProcItem {
	const char *name;
	char *(*read_func) (char *);
	unsigned long (*write_func) (const char *, unsigned long);
} ProcItem;

/* proc file handlers
 */

static int
dispatch_read(char *page, char **start, off_t off, int count, int *eof,
	      ProcItem * item)
{
	char *p = page;
	int len;

	if (off == 0)
		p = item->read_func(p);

	/* ISSUE: I don't understand this code */
	len = (p - page);
	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int
dispatch_write(struct file *file, const char __user * buffer,
	       unsigned long count, ProcItem * item)
{
	int result;
	char *tmp_buffer;

	/* Arg buffer points to userspace memory, which can't be accessed
	 * directly.  Since we're making a copy, zero-terminate the
	 * destination so that sscanf can be used on it safely.
	 */
	tmp_buffer = kmalloc(count + 1, GFP_KERNEL);
	if (!tmp_buffer)
		return -ENOMEM;

	if (copy_from_user(tmp_buffer, buffer, count)) {
		result = -EFAULT;
	} else {
		tmp_buffer[count] = 0;
		result = item->write_func(tmp_buffer, count);
	}
	kfree(tmp_buffer);
	return result;
}

static int get_lcd(struct backlight_device *bd)
{
	u32 hci_result;
	u32 value;

	hci_read1(HCI_LCD_BRIGHTNESS, &value, &hci_result);
	if (hci_result == HCI_SUCCESS) {
		return (value >> HCI_LCD_BRIGHTNESS_SHIFT);
	} else
		return -EFAULT;
}

static char *read_lcd(char *p)
{
	int value = get_lcd(NULL);

	if (value >= 0) {
		p += sprintf(p, "brightness:              %d\n", value);
		p += sprintf(p, "brightness_levels:       %d\n",
			     HCI_LCD_BRIGHTNESS_LEVELS);
	} else {
		printk(MY_ERR "Error reading LCD brightness\n");
	}

	return p;
}

static int set_lcd(int value)
{
	u32 hci_result;

	value = value << HCI_LCD_BRIGHTNESS_SHIFT;
	hci_write1(HCI_LCD_BRIGHTNESS, value, &hci_result);
	if (hci_result != HCI_SUCCESS)
		return -EFAULT;

	return 0;
}

static int set_lcd_status(struct backlight_device *bd)
{
	return set_lcd(bd->props->brightness);
}

static unsigned long write_lcd(const char *buffer, unsigned long count)
{
	int value;
	int ret = count;

	if (sscanf(buffer, " brightness : %i", &value) == 1 &&
	    value >= 0 && value < HCI_LCD_BRIGHTNESS_LEVELS)
		ret = set_lcd(value);
	else
		ret = -EINVAL;
	return ret;
}

static char *read_video(char *p)
{
	u32 hci_result;
	u32 value;

	hci_read1(HCI_VIDEO_OUT, &value, &hci_result);
	if (hci_result == HCI_SUCCESS) {
		int is_lcd = (value & HCI_VIDEO_OUT_LCD) ? 1 : 0;
		int is_crt = (value & HCI_VIDEO_OUT_CRT) ? 1 : 0;
		int is_tv = (value & HCI_VIDEO_OUT_TV) ? 1 : 0;
		p += sprintf(p, "lcd_out:                 %d\n", is_lcd);
		p += sprintf(p, "crt_out:                 %d\n", is_crt);
		p += sprintf(p, "tv_out:                  %d\n", is_tv);
	} else {
		printk(MY_ERR "Error reading video out status\n");
	}

	return p;
}

static unsigned long write_video(const char *buffer, unsigned long count)
{
	int value;
	int remain = count;
	int lcd_out = -1;
	int crt_out = -1;
	int tv_out = -1;
	u32 hci_result;
	int video_out;

	/* scan expression.  Multiple expressions may be delimited with ;
	 *
	 *  NOTE: to keep scanning simple, invalid fields are ignored
	 */
	while (remain) {
		if (sscanf(buffer, " lcd_out : %i", &value) == 1)
			lcd_out = value & 1;
		else if (sscanf(buffer, " crt_out : %i", &value) == 1)
			crt_out = value & 1;
		else if (sscanf(buffer, " tv_out : %i", &value) == 1)
			tv_out = value & 1;
		/* advance to one character past the next ; */
		do {
			++buffer;
			--remain;
		}
		while (remain && *(buffer - 1) != ';');
	}

	hci_read1(HCI_VIDEO_OUT, &video_out, &hci_result);
	if (hci_result == HCI_SUCCESS) {
		int new_video_out = video_out;
		if (lcd_out != -1)
			_set_bit(&new_video_out, HCI_VIDEO_OUT_LCD, lcd_out);
		if (crt_out != -1)
			_set_bit(&new_video_out, HCI_VIDEO_OUT_CRT, crt_out);
		if (tv_out != -1)
			_set_bit(&new_video_out, HCI_VIDEO_OUT_TV, tv_out);
		/* To avoid unnecessary video disruption, only write the new
		 * video setting if something changed. */
		if (new_video_out != video_out)
			write_acpi_int(METHOD_VIDEO_OUT, new_video_out);
	} else {
		return -EFAULT;
	}

	return count;
}

static char *read_fan(char *p)
{
	u32 hci_result;
	u32 value;

	hci_read1(HCI_FAN, &value, &hci_result);
	if (hci_result == HCI_SUCCESS) {
		p += sprintf(p, "running:                 %d\n", (value > 0));
		p += sprintf(p, "force_on:                %d\n", force_fan);
	} else {
		printk(MY_ERR "Error reading fan status\n");
	}

	return p;
}

static unsigned long write_fan(const char *buffer, unsigned long count)
{
	int value;
	u32 hci_result;

	if (sscanf(buffer, " force_on : %i", &value) == 1 &&
	    value >= 0 && value <= 1) {
		hci_write1(HCI_FAN, value, &hci_result);
		if (hci_result != HCI_SUCCESS)
			return -EFAULT;
		else
			force_fan = value;
	} else {
		return -EINVAL;
	}

	return count;
}

static char *read_keys(char *p)
{
	u32 hci_result;
	u32 value;

	if (!key_event_valid) {
		hci_read1(HCI_SYSTEM_EVENT, &value, &hci_result);
		if (hci_result == HCI_SUCCESS) {
			key_event_valid = 1;
			last_key_event = value;
		} else if (hci_result == HCI_EMPTY) {
			/* better luck next time */
		} else if (hci_result == HCI_NOT_SUPPORTED) {
			/* This is a workaround for an unresolved issue on
			 * some machines where system events sporadically
			 * become disabled. */
			hci_write1(HCI_SYSTEM_EVENT, 1, &hci_result);
			printk(MY_NOTICE "Re-enabled hotkeys\n");
		} else {
			printk(MY_ERR "Error reading hotkey status\n");
			goto end;
		}
	}

	p += sprintf(p, "hotkey_ready:            %d\n", key_event_valid);
	p += sprintf(p, "hotkey:                  0x%04x\n", last_key_event);

      end:
	return p;
}

static unsigned long write_keys(const char *buffer, unsigned long count)
{
	int value;

	if (sscanf(buffer, " hotkey_ready : %i", &value) == 1 && value == 0) {
		key_event_valid = 0;
	} else {
		return -EINVAL;
	}

	return count;
}

static char *read_version(char *p)
{
	p += sprintf(p, "driver:                  %s\n", TOSHIBA_ACPI_VERSION);
	p += sprintf(p, "proc_interface:          %d\n",
		     PROC_INTERFACE_VERSION);
	return p;
}

/* proc and module init
 */

#define PROC_TOSHIBA		"toshiba"

static ProcItem proc_items[] = {
	{"lcd", read_lcd, write_lcd},
	{"video", read_video, write_video},
	{"fan", read_fan, write_fan},
	{"keys", read_keys, write_keys},
	{"version", read_version, NULL},
	{NULL}
};

static acpi_status __init add_device(void)
{
	struct proc_dir_entry *proc;
	ProcItem *item;

	for (item = proc_items; item->name; ++item) {
		proc = create_proc_read_entry(item->name,
					      S_IFREG | S_IRUGO | S_IWUSR,
					      toshiba_proc_dir,
					      (read_proc_t *) dispatch_read,
					      item);
		if (proc)
			proc->owner = THIS_MODULE;
		if (proc && item->write_func)
			proc->write_proc = (write_proc_t *) dispatch_write;
	}

	return AE_OK;
}

static acpi_status __exit remove_device(void)
{
	ProcItem *item;

	for (item = proc_items; item->name; ++item)
		remove_proc_entry(item->name, toshiba_proc_dir);
	return AE_OK;
}

static struct backlight_properties toshiba_backlight_data = {
        .owner          = THIS_MODULE,
        .get_brightness = get_lcd,
        .update_status  = set_lcd_status,
        .max_brightness = HCI_LCD_BRIGHTNESS_LEVELS - 1,
};

static void __exit toshiba_acpi_exit(void)
{
	if (toshiba_backlight_device)
		backlight_device_unregister(toshiba_backlight_device);

	remove_device();

	if (toshiba_proc_dir)
		remove_proc_entry(PROC_TOSHIBA, acpi_root_dir);

	return;
}

static int __init toshiba_acpi_init(void)
{
	acpi_status status = AE_OK;
	u32 hci_result;

	if (acpi_disabled)
		return -ENODEV;

	if (!acpi_specific_hotkey_enabled) {
		printk(MY_INFO "Using generic hotkey driver\n");
		return -ENODEV;
	}
	/* simple device detection: look for HCI method */
	if (is_valid_acpi_path(METHOD_HCI_1))
		method_hci = METHOD_HCI_1;
	else if (is_valid_acpi_path(METHOD_HCI_2))
		method_hci = METHOD_HCI_2;
	else
		return -ENODEV;

	printk(MY_INFO "Toshiba Laptop ACPI Extras version %s\n",
	       TOSHIBA_ACPI_VERSION);
	printk(MY_INFO "    HCI method: %s\n", method_hci);

	force_fan = 0;
	key_event_valid = 0;

	/* enable event fifo */
	hci_write1(HCI_SYSTEM_EVENT, 1, &hci_result);

	toshiba_proc_dir = proc_mkdir(PROC_TOSHIBA, acpi_root_dir);
	if (!toshiba_proc_dir) {
		status = AE_ERROR;
	} else {
		toshiba_proc_dir->owner = THIS_MODULE;
		status = add_device();
		if (ACPI_FAILURE(status))
			remove_proc_entry(PROC_TOSHIBA, acpi_root_dir);
	}

	toshiba_backlight_device = backlight_device_register("toshiba",NULL,
						NULL,
						&toshiba_backlight_data);
        if (IS_ERR(toshiba_backlight_device)) {
		printk(KERN_ERR "Could not register toshiba backlight device\n");
		toshiba_backlight_device = NULL;
		toshiba_acpi_exit();
	}

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}

module_init(toshiba_acpi_init);
module_exit(toshiba_acpi_exit);
