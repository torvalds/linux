/*
 *  toshiba_acpi.c - Toshiba Laptop ACPI Extras
 *
 *
 *  Copyright (C) 2002-2004 John Belmonte
 *  Copyright (C) 2008 Philip Langdale
 *  Copyright (C) 2010 Pierre Ducroquet
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

#define TOSHIBA_ACPI_VERSION	"0.19"
#define PROC_INTERFACE_VERSION	1

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/leds.h>
#include <linux/slab.h>

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
#define TOSH_INTERFACE_1	"\\_SB_.VALD"
#define TOSH_INTERFACE_2	"\\_SB_.VALZ"
#define METHOD_VIDEO_OUT	"\\_SB_.VALX.DSSX"
#define GHCI_METHOD		".GHCI"

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
#define HCI_WIRELESS			0x0056

/* field definitions */
#define HCI_LCD_BRIGHTNESS_BITS		3
#define HCI_LCD_BRIGHTNESS_SHIFT	(16-HCI_LCD_BRIGHTNESS_BITS)
#define HCI_LCD_BRIGHTNESS_LEVELS	(1 << HCI_LCD_BRIGHTNESS_BITS)
#define HCI_VIDEO_OUT_LCD		0x1
#define HCI_VIDEO_OUT_CRT		0x2
#define HCI_VIDEO_OUT_TV		0x4
#define HCI_WIRELESS_KILL_SWITCH	0x01
#define HCI_WIRELESS_BT_PRESENT		0x0f
#define HCI_WIRELESS_BT_ATTACH		0x40
#define HCI_WIRELESS_BT_POWER		0x80

static const struct acpi_device_id toshiba_device_ids[] = {
	{"TOS6200", 0},
	{"TOS6208", 0},
	{"TOS1900", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, toshiba_device_ids);

static const struct key_entry toshiba_acpi_keymap[] __initconst = {
	{ KE_KEY, 0x101, { KEY_MUTE } },
	{ KE_KEY, 0x102, { KEY_ZOOMOUT } },
	{ KE_KEY, 0x103, { KEY_ZOOMIN } },
	{ KE_KEY, 0x13b, { KEY_COFFEE } },
	{ KE_KEY, 0x13c, { KEY_BATTERY } },
	{ KE_KEY, 0x13d, { KEY_SLEEP } },
	{ KE_KEY, 0x13e, { KEY_SUSPEND } },
	{ KE_KEY, 0x13f, { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 0x140, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY, 0x141, { KEY_BRIGHTNESSUP } },
	{ KE_KEY, 0x142, { KEY_WLAN } },
	{ KE_KEY, 0x143, { KEY_PROG1 } },
	{ KE_KEY, 0xb05, { KEY_PROG2 } },
	{ KE_KEY, 0xb06, { KEY_WWW } },
	{ KE_KEY, 0xb07, { KEY_MAIL } },
	{ KE_KEY, 0xb30, { KEY_STOP } },
	{ KE_KEY, 0xb31, { KEY_PREVIOUSSONG } },
	{ KE_KEY, 0xb32, { KEY_NEXTSONG } },
	{ KE_KEY, 0xb33, { KEY_PLAYPAUSE } },
	{ KE_KEY, 0xb5a, { KEY_MEDIA } },
	{ KE_END, 0 },
};

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

	params.count = ARRAY_SIZE(in_objs);
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

/* common hci tasks (get or set one or two value)
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

static acpi_status hci_write2(u32 reg, u32 in1, u32 in2, u32 *result)
{
	u32 in[HCI_WORDS] = { HCI_SET, reg, in1, in2, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status = hci_raw(in, out);
	*result = (status == AE_OK) ? out[0] : HCI_FAILURE;
	return status;
}

static acpi_status hci_read2(u32 reg, u32 *out1, u32 *out2, u32 *result)
{
	u32 in[HCI_WORDS] = { HCI_GET, reg, *out1, *out2, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status = hci_raw(in, out);
	*out1 = out[2];
	*out2 = out[3];
	*result = (status == AE_OK) ? out[0] : HCI_FAILURE;
	return status;
}

struct toshiba_acpi_dev {
	struct platform_device *p_dev;
	struct rfkill *bt_rfk;
	struct input_dev *hotkey_dev;
	int illumination_installed;
	acpi_handle handle;

	const char *bt_name;

	struct mutex mutex;
};

/* Illumination support */
static int toshiba_illumination_available(void)
{
	u32 in[HCI_WORDS] = { 0, 0, 0, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status;

	in[0] = 0xf100;
	status = hci_raw(in, out);
	if (ACPI_FAILURE(status)) {
		printk(MY_INFO "Illumination device not available\n");
		return 0;
	}
	in[0] = 0xf400;
	status = hci_raw(in, out);
	return 1;
}

static void toshiba_illumination_set(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	u32 in[HCI_WORDS] = { 0, 0, 0, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status;

	/* First request : initialize communication. */
	in[0] = 0xf100;
	status = hci_raw(in, out);
	if (ACPI_FAILURE(status)) {
		printk(MY_INFO "Illumination device not available\n");
		return;
	}

	if (brightness) {
		/* Switch the illumination on */
		in[0] = 0xf400;
		in[1] = 0x14e;
		in[2] = 1;
		status = hci_raw(in, out);
		if (ACPI_FAILURE(status)) {
			printk(MY_INFO "ACPI call for illumination failed.\n");
			return;
		}
	} else {
		/* Switch the illumination off */
		in[0] = 0xf400;
		in[1] = 0x14e;
		in[2] = 0;
		status = hci_raw(in, out);
		if (ACPI_FAILURE(status)) {
			printk(MY_INFO "ACPI call for illumination failed.\n");
			return;
		}
	}

	/* Last request : close communication. */
	in[0] = 0xf200;
	in[1] = 0;
	in[2] = 0;
	hci_raw(in, out);
}

static enum led_brightness toshiba_illumination_get(struct led_classdev *cdev)
{
	u32 in[HCI_WORDS] = { 0, 0, 0, 0, 0, 0 };
	u32 out[HCI_WORDS];
	acpi_status status;
	enum led_brightness result;

	/* First request : initialize communication. */
	in[0] = 0xf100;
	status = hci_raw(in, out);
	if (ACPI_FAILURE(status)) {
		printk(MY_INFO "Illumination device not available\n");
		return LED_OFF;
	}

	/* Check the illumination */
	in[0] = 0xf300;
	in[1] = 0x14e;
	status = hci_raw(in, out);
	if (ACPI_FAILURE(status)) {
		printk(MY_INFO "ACPI call for illumination failed.\n");
		return LED_OFF;
	}

	result = out[2] ? LED_FULL : LED_OFF;

	/* Last request : close communication. */
	in[0] = 0xf200;
	in[1] = 0;
	in[2] = 0;
	hci_raw(in, out);

	return result;
}

static struct led_classdev toshiba_led = {
	.name           = "toshiba::illumination",
	.max_brightness = 1,
	.brightness_set = toshiba_illumination_set,
	.brightness_get = toshiba_illumination_get,
};

static struct toshiba_acpi_dev toshiba_acpi = {
	.bt_name = "Toshiba Bluetooth",
};

/* Bluetooth rfkill handlers */

static u32 hci_get_bt_present(bool *present)
{
	u32 hci_result;
	u32 value, value2;

	value = 0;
	value2 = 0;
	hci_read2(HCI_WIRELESS, &value, &value2, &hci_result);
	if (hci_result == HCI_SUCCESS)
		*present = (value & HCI_WIRELESS_BT_PRESENT) ? true : false;

	return hci_result;
}

static u32 hci_get_radio_state(bool *radio_state)
{
	u32 hci_result;
	u32 value, value2;

	value = 0;
	value2 = 0x0001;
	hci_read2(HCI_WIRELESS, &value, &value2, &hci_result);

	*radio_state = value & HCI_WIRELESS_KILL_SWITCH;
	return hci_result;
}

static int bt_rfkill_set_block(void *data, bool blocked)
{
	struct toshiba_acpi_dev *dev = data;
	u32 result1, result2;
	u32 value;
	int err;
	bool radio_state;

	value = (blocked == false);

	mutex_lock(&dev->mutex);
	if (hci_get_radio_state(&radio_state) != HCI_SUCCESS) {
		err = -EBUSY;
		goto out;
	}

	if (!radio_state) {
		err = 0;
		goto out;
	}

	hci_write2(HCI_WIRELESS, value, HCI_WIRELESS_BT_POWER, &result1);
	hci_write2(HCI_WIRELESS, value, HCI_WIRELESS_BT_ATTACH, &result2);

	if (result1 != HCI_SUCCESS || result2 != HCI_SUCCESS)
		err = -EBUSY;
	else
		err = 0;
 out:
	mutex_unlock(&dev->mutex);
	return err;
}

static void bt_rfkill_poll(struct rfkill *rfkill, void *data)
{
	bool new_rfk_state;
	bool value;
	u32 hci_result;
	struct toshiba_acpi_dev *dev = data;

	mutex_lock(&dev->mutex);

	hci_result = hci_get_radio_state(&value);
	if (hci_result != HCI_SUCCESS) {
		/* Can't do anything useful */
		mutex_unlock(&dev->mutex);
		return;
	}

	new_rfk_state = value;

	mutex_unlock(&dev->mutex);

	if (rfkill_set_hw_state(rfkill, !new_rfk_state))
		bt_rfkill_set_block(data, true);
}

static const struct rfkill_ops toshiba_rfk_ops = {
	.set_block = bt_rfkill_set_block,
	.poll = bt_rfkill_poll,
};

static struct proc_dir_entry *toshiba_proc_dir /*= 0*/ ;
static struct backlight_device *toshiba_backlight_device;
static int force_fan;
static int last_key_event;
static int key_event_valid;

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

static int lcd_proc_show(struct seq_file *m, void *v)
{
	int value = get_lcd(NULL);

	if (value >= 0) {
		seq_printf(m, "brightness:              %d\n", value);
		seq_printf(m, "brightness_levels:       %d\n",
			     HCI_LCD_BRIGHTNESS_LEVELS);
	} else {
		printk(MY_ERR "Error reading LCD brightness\n");
	}

	return 0;
}

static int lcd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, lcd_proc_show, NULL);
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
	return set_lcd(bd->props.brightness);
}

static ssize_t lcd_proc_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	char cmd[42];
	size_t len;
	int value;
	int ret;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " brightness : %i", &value) == 1 &&
	    value >= 0 && value < HCI_LCD_BRIGHTNESS_LEVELS) {
		ret = set_lcd(value);
		if (ret == 0)
			ret = count;
	} else {
		ret = -EINVAL;
	}
	return ret;
}

static const struct file_operations lcd_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= lcd_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= lcd_proc_write,
};

static int video_proc_show(struct seq_file *m, void *v)
{
	u32 hci_result;
	u32 value;

	hci_read1(HCI_VIDEO_OUT, &value, &hci_result);
	if (hci_result == HCI_SUCCESS) {
		int is_lcd = (value & HCI_VIDEO_OUT_LCD) ? 1 : 0;
		int is_crt = (value & HCI_VIDEO_OUT_CRT) ? 1 : 0;
		int is_tv = (value & HCI_VIDEO_OUT_TV) ? 1 : 0;
		seq_printf(m, "lcd_out:                 %d\n", is_lcd);
		seq_printf(m, "crt_out:                 %d\n", is_crt);
		seq_printf(m, "tv_out:                  %d\n", is_tv);
	} else {
		printk(MY_ERR "Error reading video out status\n");
	}

	return 0;
}

static int video_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, video_proc_show, NULL);
}

static ssize_t video_proc_write(struct file *file, const char __user *buf,
				size_t count, loff_t *pos)
{
	char *cmd, *buffer;
	int value;
	int remain = count;
	int lcd_out = -1;
	int crt_out = -1;
	int tv_out = -1;
	u32 hci_result;
	u32 video_out;

	cmd = kmalloc(count + 1, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;
	if (copy_from_user(cmd, buf, count)) {
		kfree(cmd);
		return -EFAULT;
	}
	cmd[count] = '\0';

	buffer = cmd;

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

	kfree(cmd);

	hci_read1(HCI_VIDEO_OUT, &video_out, &hci_result);
	if (hci_result == HCI_SUCCESS) {
		unsigned int new_video_out = video_out;
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

static const struct file_operations video_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= video_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= video_proc_write,
};

static int fan_proc_show(struct seq_file *m, void *v)
{
	u32 hci_result;
	u32 value;

	hci_read1(HCI_FAN, &value, &hci_result);
	if (hci_result == HCI_SUCCESS) {
		seq_printf(m, "running:                 %d\n", (value > 0));
		seq_printf(m, "force_on:                %d\n", force_fan);
	} else {
		printk(MY_ERR "Error reading fan status\n");
	}

	return 0;
}

static int fan_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, fan_proc_show, NULL);
}

static ssize_t fan_proc_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	char cmd[42];
	size_t len;
	int value;
	u32 hci_result;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " force_on : %i", &value) == 1 &&
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

static const struct file_operations fan_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= fan_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= fan_proc_write,
};

static int keys_proc_show(struct seq_file *m, void *v)
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

	seq_printf(m, "hotkey_ready:            %d\n", key_event_valid);
	seq_printf(m, "hotkey:                  0x%04x\n", last_key_event);
end:
	return 0;
}

static int keys_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, keys_proc_show, NULL);
}

static ssize_t keys_proc_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *pos)
{
	char cmd[42];
	size_t len;
	int value;

	len = min(count, sizeof(cmd) - 1);
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	if (sscanf(cmd, " hotkey_ready : %i", &value) == 1 && value == 0) {
		key_event_valid = 0;
	} else {
		return -EINVAL;
	}

	return count;
}

static const struct file_operations keys_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= keys_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= keys_proc_write,
};

static int version_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "driver:                  %s\n", TOSHIBA_ACPI_VERSION);
	seq_printf(m, "proc_interface:          %d\n", PROC_INTERFACE_VERSION);
	return 0;
}

static int version_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, version_proc_show, PDE(inode)->data);
}

static const struct file_operations version_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= version_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* proc and module init
 */

#define PROC_TOSHIBA		"toshiba"

static void __init create_toshiba_proc_entries(void)
{
	proc_create("lcd", S_IRUGO | S_IWUSR, toshiba_proc_dir, &lcd_proc_fops);
	proc_create("video", S_IRUGO | S_IWUSR, toshiba_proc_dir, &video_proc_fops);
	proc_create("fan", S_IRUGO | S_IWUSR, toshiba_proc_dir, &fan_proc_fops);
	proc_create("keys", S_IRUGO | S_IWUSR, toshiba_proc_dir, &keys_proc_fops);
	proc_create("version", S_IRUGO, toshiba_proc_dir, &version_proc_fops);
}

static void remove_toshiba_proc_entries(void)
{
	remove_proc_entry("lcd", toshiba_proc_dir);
	remove_proc_entry("video", toshiba_proc_dir);
	remove_proc_entry("fan", toshiba_proc_dir);
	remove_proc_entry("keys", toshiba_proc_dir);
	remove_proc_entry("version", toshiba_proc_dir);
}

static struct backlight_ops toshiba_backlight_data = {
        .get_brightness = get_lcd,
        .update_status  = set_lcd_status,
};

static void toshiba_acpi_notify(acpi_handle handle, u32 event, void *context)
{
	u32 hci_result, value;

	if (event != 0x80)
		return;
	do {
		hci_read1(HCI_SYSTEM_EVENT, &value, &hci_result);
		if (hci_result == HCI_SUCCESS) {
			if (value == 0x100)
				continue;
			/* act on key press; ignore key release */
			if (value & 0x80)
				continue;

			if (!sparse_keymap_report_event(toshiba_acpi.hotkey_dev,
							value, 1, true)) {
				printk(MY_INFO "Unknown key %x\n",
				       value);
			}
		} else if (hci_result == HCI_NOT_SUPPORTED) {
			/* This is a workaround for an unresolved issue on
			 * some machines where system events sporadically
			 * become disabled. */
			hci_write1(HCI_SYSTEM_EVENT, 1, &hci_result);
			printk(MY_NOTICE "Re-enabled hotkeys\n");
		}
	} while (hci_result != HCI_EMPTY);
}

static int __init toshiba_acpi_setup_keyboard(char *device)
{
	acpi_status status;
	int error;

	status = acpi_get_handle(NULL, device, &toshiba_acpi.handle);
	if (ACPI_FAILURE(status)) {
		printk(MY_INFO "Unable to get notification device\n");
		return -ENODEV;
	}

	toshiba_acpi.hotkey_dev = input_allocate_device();
	if (!toshiba_acpi.hotkey_dev) {
		printk(MY_INFO "Unable to register input device\n");
		return -ENOMEM;
	}

	toshiba_acpi.hotkey_dev->name = "Toshiba input device";
	toshiba_acpi.hotkey_dev->phys = device;
	toshiba_acpi.hotkey_dev->id.bustype = BUS_HOST;

	error = sparse_keymap_setup(toshiba_acpi.hotkey_dev,
				    toshiba_acpi_keymap, NULL);
	if (error)
		goto err_free_dev;

	status = acpi_install_notify_handler(toshiba_acpi.handle,
				ACPI_DEVICE_NOTIFY, toshiba_acpi_notify, NULL);
	if (ACPI_FAILURE(status)) {
		printk(MY_INFO "Unable to install hotkey notification\n");
		error = -ENODEV;
		goto err_free_keymap;
	}

	status = acpi_evaluate_object(toshiba_acpi.handle, "ENAB", NULL, NULL);
	if (ACPI_FAILURE(status)) {
		printk(MY_INFO "Unable to enable hotkeys\n");
		error = -ENODEV;
		goto err_remove_notify;
	}

	error = input_register_device(toshiba_acpi.hotkey_dev);
	if (error) {
		printk(MY_INFO "Unable to register input device\n");
		goto err_remove_notify;
	}

	return 0;

 err_remove_notify:
	acpi_remove_notify_handler(toshiba_acpi.handle,
				   ACPI_DEVICE_NOTIFY, toshiba_acpi_notify);
 err_free_keymap:
	sparse_keymap_free(toshiba_acpi.hotkey_dev);
 err_free_dev:
	input_free_device(toshiba_acpi.hotkey_dev);
	toshiba_acpi.hotkey_dev = NULL;
	return error;
}

static void toshiba_acpi_exit(void)
{
	if (toshiba_acpi.hotkey_dev) {
		acpi_remove_notify_handler(toshiba_acpi.handle,
				ACPI_DEVICE_NOTIFY, toshiba_acpi_notify);
		sparse_keymap_free(toshiba_acpi.hotkey_dev);
		input_unregister_device(toshiba_acpi.hotkey_dev);
	}

	if (toshiba_acpi.bt_rfk) {
		rfkill_unregister(toshiba_acpi.bt_rfk);
		rfkill_destroy(toshiba_acpi.bt_rfk);
	}

	if (toshiba_backlight_device)
		backlight_device_unregister(toshiba_backlight_device);

	remove_toshiba_proc_entries();

	if (toshiba_proc_dir)
		remove_proc_entry(PROC_TOSHIBA, acpi_root_dir);

	if (toshiba_acpi.illumination_installed)
		led_classdev_unregister(&toshiba_led);

	platform_device_unregister(toshiba_acpi.p_dev);

	return;
}

static int __init toshiba_acpi_init(void)
{
	u32 hci_result;
	bool bt_present;
	int ret = 0;
	struct backlight_properties props;

	if (acpi_disabled)
		return -ENODEV;

	/* simple device detection: look for HCI method */
	if (is_valid_acpi_path(TOSH_INTERFACE_1 GHCI_METHOD)) {
		method_hci = TOSH_INTERFACE_1 GHCI_METHOD;
		if (toshiba_acpi_setup_keyboard(TOSH_INTERFACE_1))
			printk(MY_INFO "Unable to activate hotkeys\n");
	} else if (is_valid_acpi_path(TOSH_INTERFACE_2 GHCI_METHOD)) {
		method_hci = TOSH_INTERFACE_2 GHCI_METHOD;
		if (toshiba_acpi_setup_keyboard(TOSH_INTERFACE_2))
			printk(MY_INFO "Unable to activate hotkeys\n");
	} else
		return -ENODEV;

	printk(MY_INFO "Toshiba Laptop ACPI Extras version %s\n",
	       TOSHIBA_ACPI_VERSION);
	printk(MY_INFO "    HCI method: %s\n", method_hci);

	mutex_init(&toshiba_acpi.mutex);

	toshiba_acpi.p_dev = platform_device_register_simple("toshiba_acpi",
							      -1, NULL, 0);
	if (IS_ERR(toshiba_acpi.p_dev)) {
		ret = PTR_ERR(toshiba_acpi.p_dev);
		printk(MY_ERR "unable to register platform device\n");
		toshiba_acpi.p_dev = NULL;
		toshiba_acpi_exit();
		return ret;
	}

	force_fan = 0;
	key_event_valid = 0;

	/* enable event fifo */
	hci_write1(HCI_SYSTEM_EVENT, 1, &hci_result);

	toshiba_proc_dir = proc_mkdir(PROC_TOSHIBA, acpi_root_dir);
	if (!toshiba_proc_dir) {
		toshiba_acpi_exit();
		return -ENODEV;
	} else {
		create_toshiba_proc_entries();
	}

	props.max_brightness = HCI_LCD_BRIGHTNESS_LEVELS - 1;
	toshiba_backlight_device = backlight_device_register("toshiba",
							     &toshiba_acpi.p_dev->dev,
							     NULL,
							     &toshiba_backlight_data,
							     &props);
        if (IS_ERR(toshiba_backlight_device)) {
		ret = PTR_ERR(toshiba_backlight_device);

		printk(KERN_ERR "Could not register toshiba backlight device\n");
		toshiba_backlight_device = NULL;
		toshiba_acpi_exit();
		return ret;
	}

	/* Register rfkill switch for Bluetooth */
	if (hci_get_bt_present(&bt_present) == HCI_SUCCESS && bt_present) {
		toshiba_acpi.bt_rfk = rfkill_alloc(toshiba_acpi.bt_name,
						   &toshiba_acpi.p_dev->dev,
						   RFKILL_TYPE_BLUETOOTH,
						   &toshiba_rfk_ops,
						   &toshiba_acpi);
		if (!toshiba_acpi.bt_rfk) {
			printk(MY_ERR "unable to allocate rfkill device\n");
			toshiba_acpi_exit();
			return -ENOMEM;
		}

		ret = rfkill_register(toshiba_acpi.bt_rfk);
		if (ret) {
			printk(MY_ERR "unable to register rfkill device\n");
			rfkill_destroy(toshiba_acpi.bt_rfk);
			toshiba_acpi_exit();
			return ret;
		}
	}

	toshiba_acpi.illumination_installed = 0;
	if (toshiba_illumination_available()) {
		if (!led_classdev_register(&(toshiba_acpi.p_dev->dev),
					   &toshiba_led))
			toshiba_acpi.illumination_installed = 1;
	}

	return 0;
}

module_init(toshiba_acpi_init);
module_exit(toshiba_acpi_exit);
