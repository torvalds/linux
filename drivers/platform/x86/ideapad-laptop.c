/*
 *  ideapad-laptop.c - Lenovo IdeaPad ACPI Extras
 *
 *  Copyright © 2010 Intel Corporation
 *  Copyright © 2010 David Woodhouse <dwmw2@infradead.org>
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
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <linux/rfkill.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/i8042.h>

#define IDEAPAD_RFKILL_DEV_NUM	(3)

#define CFG_BT_BIT	(16)
#define CFG_3G_BIT	(17)
#define CFG_WIFI_BIT	(18)
#define CFG_CAMERA_BIT	(19)

enum {
	VPCCMD_R_VPC1 = 0x10,
	VPCCMD_R_BL_MAX,
	VPCCMD_R_BL,
	VPCCMD_W_BL,
	VPCCMD_R_WIFI,
	VPCCMD_W_WIFI,
	VPCCMD_R_BT,
	VPCCMD_W_BT,
	VPCCMD_R_BL_POWER,
	VPCCMD_R_NOVO,
	VPCCMD_R_VPC2,
	VPCCMD_R_TOUCHPAD,
	VPCCMD_W_TOUCHPAD,
	VPCCMD_R_CAMERA,
	VPCCMD_W_CAMERA,
	VPCCMD_R_3G,
	VPCCMD_W_3G,
	VPCCMD_R_ODD, /* 0x21 */
	VPCCMD_W_FAN,
	VPCCMD_R_RF,
	VPCCMD_W_RF,
	VPCCMD_R_FAN = 0x2B,
	VPCCMD_R_SPECIAL_BUTTONS = 0x31,
	VPCCMD_W_BL_POWER = 0x33,
};

struct ideapad_private {
	struct rfkill *rfk[IDEAPAD_RFKILL_DEV_NUM];
	struct platform_device *platform_device;
	struct input_dev *inputdev;
	struct backlight_device *blightdev;
	struct dentry *debug;
	unsigned long cfg;
};

static acpi_handle ideapad_handle;
static struct ideapad_private *ideapad_priv;
static bool no_bt_rfkill;
module_param(no_bt_rfkill, bool, 0444);
MODULE_PARM_DESC(no_bt_rfkill, "No rfkill for bluetooth.");

/*
 * ACPI Helpers
 */
#define IDEAPAD_EC_TIMEOUT (100) /* in ms */

static int read_method_int(acpi_handle handle, const char *method, int *val)
{
	acpi_status status;
	unsigned long long result;

	status = acpi_evaluate_integer(handle, (char *)method, NULL, &result);
	if (ACPI_FAILURE(status)) {
		*val = -1;
		return -1;
	} else {
		*val = result;
		return 0;
	}
}

static int method_vpcr(acpi_handle handle, int cmd, int *ret)
{
	acpi_status status;
	unsigned long long result;
	struct acpi_object_list params;
	union acpi_object in_obj;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = cmd;

	status = acpi_evaluate_integer(handle, "VPCR", &params, &result);

	if (ACPI_FAILURE(status)) {
		*ret = -1;
		return -1;
	} else {
		*ret = result;
		return 0;
	}
}

static int method_vpcw(acpi_handle handle, int cmd, int data)
{
	struct acpi_object_list params;
	union acpi_object in_obj[2];
	acpi_status status;

	params.count = 2;
	params.pointer = in_obj;
	in_obj[0].type = ACPI_TYPE_INTEGER;
	in_obj[0].integer.value = cmd;
	in_obj[1].type = ACPI_TYPE_INTEGER;
	in_obj[1].integer.value = data;

	status = acpi_evaluate_object(handle, "VPCW", &params, NULL);
	if (status != AE_OK)
		return -1;
	return 0;
}

static int read_ec_data(acpi_handle handle, int cmd, unsigned long *data)
{
	int val;
	unsigned long int end_jiffies;

	if (method_vpcw(handle, 1, cmd))
		return -1;

	for (end_jiffies = jiffies+(HZ)*IDEAPAD_EC_TIMEOUT/1000+1;
	     time_before(jiffies, end_jiffies);) {
		schedule();
		if (method_vpcr(handle, 1, &val))
			return -1;
		if (val == 0) {
			if (method_vpcr(handle, 0, &val))
				return -1;
			*data = val;
			return 0;
		}
	}
	pr_err("timeout in read_ec_cmd\n");
	return -1;
}

static int write_ec_cmd(acpi_handle handle, int cmd, unsigned long data)
{
	int val;
	unsigned long int end_jiffies;

	if (method_vpcw(handle, 0, data))
		return -1;
	if (method_vpcw(handle, 1, cmd))
		return -1;

	for (end_jiffies = jiffies+(HZ)*IDEAPAD_EC_TIMEOUT/1000+1;
	     time_before(jiffies, end_jiffies);) {
		schedule();
		if (method_vpcr(handle, 1, &val))
			return -1;
		if (val == 0)
			return 0;
	}
	pr_err("timeout in write_ec_cmd\n");
	return -1;
}

/*
 * debugfs
 */
static int debugfs_status_show(struct seq_file *s, void *data)
{
	unsigned long value;

	if (!read_ec_data(ideapad_handle, VPCCMD_R_BL_MAX, &value))
		seq_printf(s, "Backlight max:\t%lu\n", value);
	if (!read_ec_data(ideapad_handle, VPCCMD_R_BL, &value))
		seq_printf(s, "Backlight now:\t%lu\n", value);
	if (!read_ec_data(ideapad_handle, VPCCMD_R_BL_POWER, &value))
		seq_printf(s, "BL power value:\t%s\n", value ? "On" : "Off");
	seq_printf(s, "=====================\n");

	if (!read_ec_data(ideapad_handle, VPCCMD_R_RF, &value))
		seq_printf(s, "Radio status:\t%s(%lu)\n",
			   value ? "On" : "Off", value);
	if (!read_ec_data(ideapad_handle, VPCCMD_R_WIFI, &value))
		seq_printf(s, "Wifi status:\t%s(%lu)\n",
			   value ? "On" : "Off", value);
	if (!read_ec_data(ideapad_handle, VPCCMD_R_BT, &value))
		seq_printf(s, "BT status:\t%s(%lu)\n",
			   value ? "On" : "Off", value);
	if (!read_ec_data(ideapad_handle, VPCCMD_R_3G, &value))
		seq_printf(s, "3G status:\t%s(%lu)\n",
			   value ? "On" : "Off", value);
	seq_printf(s, "=====================\n");

	if (!read_ec_data(ideapad_handle, VPCCMD_R_TOUCHPAD, &value))
		seq_printf(s, "Touchpad status:%s(%lu)\n",
			   value ? "On" : "Off", value);
	if (!read_ec_data(ideapad_handle, VPCCMD_R_CAMERA, &value))
		seq_printf(s, "Camera status:\t%s(%lu)\n",
			   value ? "On" : "Off", value);

	return 0;
}

static int debugfs_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_status_show, NULL);
}

static const struct file_operations debugfs_status_fops = {
	.owner = THIS_MODULE,
	.open = debugfs_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int debugfs_cfg_show(struct seq_file *s, void *data)
{
	if (!ideapad_priv) {
		seq_printf(s, "cfg: N/A\n");
	} else {
		seq_printf(s, "cfg: 0x%.8lX\n\nCapability: ",
			   ideapad_priv->cfg);
		if (test_bit(CFG_BT_BIT, &ideapad_priv->cfg))
			seq_printf(s, "Bluetooth ");
		if (test_bit(CFG_3G_BIT, &ideapad_priv->cfg))
			seq_printf(s, "3G ");
		if (test_bit(CFG_WIFI_BIT, &ideapad_priv->cfg))
			seq_printf(s, "Wireless ");
		if (test_bit(CFG_CAMERA_BIT, &ideapad_priv->cfg))
			seq_printf(s, "Camera ");
		seq_printf(s, "\nGraphic: ");
		switch ((ideapad_priv->cfg)&0x700) {
		case 0x100:
			seq_printf(s, "Intel");
			break;
		case 0x200:
			seq_printf(s, "ATI");
			break;
		case 0x300:
			seq_printf(s, "Nvidia");
			break;
		case 0x400:
			seq_printf(s, "Intel and ATI");
			break;
		case 0x500:
			seq_printf(s, "Intel and Nvidia");
			break;
		}
		seq_printf(s, "\n");
	}
	return 0;
}

static int debugfs_cfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_cfg_show, NULL);
}

static const struct file_operations debugfs_cfg_fops = {
	.owner = THIS_MODULE,
	.open = debugfs_cfg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __devinit ideapad_debugfs_init(struct ideapad_private *priv)
{
	struct dentry *node;

	priv->debug = debugfs_create_dir("ideapad", NULL);
	if (priv->debug == NULL) {
		pr_err("failed to create debugfs directory");
		goto errout;
	}

	node = debugfs_create_file("cfg", S_IRUGO, priv->debug, NULL,
				   &debugfs_cfg_fops);
	if (!node) {
		pr_err("failed to create cfg in debugfs");
		goto errout;
	}

	node = debugfs_create_file("status", S_IRUGO, priv->debug, NULL,
				   &debugfs_status_fops);
	if (!node) {
		pr_err("failed to create status in debugfs");
		goto errout;
	}

	return 0;

errout:
	return -ENOMEM;
}

static void ideapad_debugfs_exit(struct ideapad_private *priv)
{
	debugfs_remove_recursive(priv->debug);
	priv->debug = NULL;
}

/*
 * sysfs
 */
static ssize_t show_ideapad_cam(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	unsigned long result;

	if (read_ec_data(ideapad_handle, VPCCMD_R_CAMERA, &result))
		return sprintf(buf, "-1\n");
	return sprintf(buf, "%lu\n", result);
}

static ssize_t store_ideapad_cam(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int ret, state;

	if (!count)
		return 0;
	if (sscanf(buf, "%i", &state) != 1)
		return -EINVAL;
	ret = write_ec_cmd(ideapad_handle, VPCCMD_W_CAMERA, state);
	if (ret < 0)
		return -EIO;
	return count;
}

static DEVICE_ATTR(camera_power, 0644, show_ideapad_cam, store_ideapad_cam);

static ssize_t show_ideapad_fan(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	unsigned long result;

	if (read_ec_data(ideapad_handle, VPCCMD_R_FAN, &result))
		return sprintf(buf, "-1\n");
	return sprintf(buf, "%lu\n", result);
}

static ssize_t store_ideapad_fan(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	int ret, state;

	if (!count)
		return 0;
	if (sscanf(buf, "%i", &state) != 1)
		return -EINVAL;
	if (state < 0 || state > 4 || state == 3)
		return -EINVAL;
	ret = write_ec_cmd(ideapad_handle, VPCCMD_W_FAN, state);
	if (ret < 0)
		return -EIO;
	return count;
}

static DEVICE_ATTR(fan_mode, 0644, show_ideapad_fan, store_ideapad_fan);

static struct attribute *ideapad_attributes[] = {
	&dev_attr_camera_power.attr,
	&dev_attr_fan_mode.attr,
	NULL
};

static umode_t ideapad_is_visible(struct kobject *kobj,
				 struct attribute *attr,
				 int idx)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct ideapad_private *priv = dev_get_drvdata(dev);
	bool supported;

	if (attr == &dev_attr_camera_power.attr)
		supported = test_bit(CFG_CAMERA_BIT, &(priv->cfg));
	else if (attr == &dev_attr_fan_mode.attr) {
		unsigned long value;
		supported = !read_ec_data(ideapad_handle, VPCCMD_R_FAN, &value);
	} else
		supported = true;

	return supported ? attr->mode : 0;
}

static struct attribute_group ideapad_attribute_group = {
	.is_visible = ideapad_is_visible,
	.attrs = ideapad_attributes
};

/*
 * Rfkill
 */
struct ideapad_rfk_data {
	char *name;
	int cfgbit;
	int opcode;
	int type;
};

const struct ideapad_rfk_data ideapad_rfk_data[] = {
	{ "ideapad_wlan",    CFG_WIFI_BIT, VPCCMD_W_WIFI, RFKILL_TYPE_WLAN },
	{ "ideapad_bluetooth", CFG_BT_BIT, VPCCMD_W_BT, RFKILL_TYPE_BLUETOOTH },
	{ "ideapad_3g",        CFG_3G_BIT, VPCCMD_W_3G, RFKILL_TYPE_WWAN },
};

static int ideapad_rfk_set(void *data, bool blocked)
{
	unsigned long opcode = (unsigned long)data;

	return write_ec_cmd(ideapad_handle, opcode, !blocked);
}

static struct rfkill_ops ideapad_rfk_ops = {
	.set_block = ideapad_rfk_set,
};

static void ideapad_sync_rfk_state(struct ideapad_private *priv)
{
	unsigned long hw_blocked;
	int i;

	if (read_ec_data(ideapad_handle, VPCCMD_R_RF, &hw_blocked))
		return;
	hw_blocked = !hw_blocked;

	for (i = 0; i < IDEAPAD_RFKILL_DEV_NUM; i++)
		if (priv->rfk[i])
			rfkill_set_hw_state(priv->rfk[i], hw_blocked);
}

static int __devinit ideapad_register_rfkill(struct acpi_device *adevice,
					     int dev)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	int ret;
	unsigned long sw_blocked;

	if (no_bt_rfkill &&
	    (ideapad_rfk_data[dev].type == RFKILL_TYPE_BLUETOOTH)) {
		/* Force to enable bluetooth when no_bt_rfkill=1 */
		write_ec_cmd(ideapad_handle,
			     ideapad_rfk_data[dev].opcode, 1);
		return 0;
	}

	priv->rfk[dev] = rfkill_alloc(ideapad_rfk_data[dev].name, &adevice->dev,
				      ideapad_rfk_data[dev].type, &ideapad_rfk_ops,
				      (void *)(long)dev);
	if (!priv->rfk[dev])
		return -ENOMEM;

	if (read_ec_data(ideapad_handle, ideapad_rfk_data[dev].opcode-1,
			 &sw_blocked)) {
		rfkill_init_sw_state(priv->rfk[dev], 0);
	} else {
		sw_blocked = !sw_blocked;
		rfkill_init_sw_state(priv->rfk[dev], sw_blocked);
	}

	ret = rfkill_register(priv->rfk[dev]);
	if (ret) {
		rfkill_destroy(priv->rfk[dev]);
		return ret;
	}
	return 0;
}

static void ideapad_unregister_rfkill(struct acpi_device *adevice, int dev)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);

	if (!priv->rfk[dev])
		return;

	rfkill_unregister(priv->rfk[dev]);
	rfkill_destroy(priv->rfk[dev]);
}

/*
 * Platform device
 */
static int __devinit ideapad_platform_init(struct ideapad_private *priv)
{
	int result;

	priv->platform_device = platform_device_alloc("ideapad", -1);
	if (!priv->platform_device)
		return -ENOMEM;
	platform_set_drvdata(priv->platform_device, priv);

	result = platform_device_add(priv->platform_device);
	if (result)
		goto fail_platform_device;

	result = sysfs_create_group(&priv->platform_device->dev.kobj,
				    &ideapad_attribute_group);
	if (result)
		goto fail_sysfs;
	return 0;

fail_sysfs:
	platform_device_del(priv->platform_device);
fail_platform_device:
	platform_device_put(priv->platform_device);
	return result;
}

static void ideapad_platform_exit(struct ideapad_private *priv)
{
	sysfs_remove_group(&priv->platform_device->dev.kobj,
			   &ideapad_attribute_group);
	platform_device_unregister(priv->platform_device);
}

/*
 * input device
 */
static const struct key_entry ideapad_keymap[] = {
	{ KE_KEY, 6,  { KEY_SWITCHVIDEOMODE } },
	{ KE_KEY, 7,  { KEY_CAMERA } },
	{ KE_KEY, 11, { KEY_F16 } },
	{ KE_KEY, 13, { KEY_WLAN } },
	{ KE_KEY, 16, { KEY_PROG1 } },
	{ KE_KEY, 17, { KEY_PROG2 } },
	{ KE_KEY, 64, { KEY_PROG3 } },
	{ KE_KEY, 65, { KEY_PROG4 } },
	{ KE_KEY, 66, { KEY_TOUCHPAD_OFF } },
	{ KE_KEY, 67, { KEY_TOUCHPAD_ON } },
	{ KE_END, 0 },
};

static int __devinit ideapad_input_init(struct ideapad_private *priv)
{
	struct input_dev *inputdev;
	int error;

	inputdev = input_allocate_device();
	if (!inputdev) {
		pr_info("Unable to allocate input device\n");
		return -ENOMEM;
	}

	inputdev->name = "Ideapad extra buttons";
	inputdev->phys = "ideapad/input0";
	inputdev->id.bustype = BUS_HOST;
	inputdev->dev.parent = &priv->platform_device->dev;

	error = sparse_keymap_setup(inputdev, ideapad_keymap, NULL);
	if (error) {
		pr_err("Unable to setup input device keymap\n");
		goto err_free_dev;
	}

	error = input_register_device(inputdev);
	if (error) {
		pr_err("Unable to register input device\n");
		goto err_free_keymap;
	}

	priv->inputdev = inputdev;
	return 0;

err_free_keymap:
	sparse_keymap_free(inputdev);
err_free_dev:
	input_free_device(inputdev);
	return error;
}

static void ideapad_input_exit(struct ideapad_private *priv)
{
	sparse_keymap_free(priv->inputdev);
	input_unregister_device(priv->inputdev);
	priv->inputdev = NULL;
}

static void ideapad_input_report(struct ideapad_private *priv,
				 unsigned long scancode)
{
	sparse_keymap_report_event(priv->inputdev, scancode, 1, true);
}

static void ideapad_input_novokey(struct ideapad_private *priv)
{
	unsigned long long_pressed;

	if (read_ec_data(ideapad_handle, VPCCMD_R_NOVO, &long_pressed))
		return;
	if (long_pressed)
		ideapad_input_report(priv, 17);
	else
		ideapad_input_report(priv, 16);
}

static void ideapad_check_special_buttons(struct ideapad_private *priv)
{
	unsigned long bit, value;

	read_ec_data(ideapad_handle, VPCCMD_R_SPECIAL_BUTTONS, &value);

	for (bit = 0; bit < 16; bit++) {
		if (test_bit(bit, &value)) {
			switch (bit) {
			case 6:
				/* Thermal Management button */
				ideapad_input_report(priv, 65);
				break;
			case 1:
				/* OneKey Theater button */
				ideapad_input_report(priv, 64);
				break;
			}
		}
	}
}

/*
 * backlight
 */
static int ideapad_backlight_get_brightness(struct backlight_device *blightdev)
{
	unsigned long now;

	if (read_ec_data(ideapad_handle, VPCCMD_R_BL, &now))
		return -EIO;
	return now;
}

static int ideapad_backlight_update_status(struct backlight_device *blightdev)
{
	if (write_ec_cmd(ideapad_handle, VPCCMD_W_BL,
			 blightdev->props.brightness))
		return -EIO;
	if (write_ec_cmd(ideapad_handle, VPCCMD_W_BL_POWER,
			 blightdev->props.power == FB_BLANK_POWERDOWN ? 0 : 1))
		return -EIO;

	return 0;
}

static const struct backlight_ops ideapad_backlight_ops = {
	.get_brightness = ideapad_backlight_get_brightness,
	.update_status = ideapad_backlight_update_status,
};

static int ideapad_backlight_init(struct ideapad_private *priv)
{
	struct backlight_device *blightdev;
	struct backlight_properties props;
	unsigned long max, now, power;

	if (read_ec_data(ideapad_handle, VPCCMD_R_BL_MAX, &max))
		return -EIO;
	if (read_ec_data(ideapad_handle, VPCCMD_R_BL, &now))
		return -EIO;
	if (read_ec_data(ideapad_handle, VPCCMD_R_BL_POWER, &power))
		return -EIO;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = max;
	props.type = BACKLIGHT_PLATFORM;
	blightdev = backlight_device_register("ideapad",
					      &priv->platform_device->dev,
					      priv,
					      &ideapad_backlight_ops,
					      &props);
	if (IS_ERR(blightdev)) {
		pr_err("Could not register backlight device\n");
		return PTR_ERR(blightdev);
	}

	priv->blightdev = blightdev;
	blightdev->props.brightness = now;
	blightdev->props.power = power ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
	backlight_update_status(blightdev);

	return 0;
}

static void ideapad_backlight_exit(struct ideapad_private *priv)
{
	if (priv->blightdev)
		backlight_device_unregister(priv->blightdev);
	priv->blightdev = NULL;
}

static void ideapad_backlight_notify_power(struct ideapad_private *priv)
{
	unsigned long power;
	struct backlight_device *blightdev = priv->blightdev;

	if (!blightdev)
		return;
	if (read_ec_data(ideapad_handle, VPCCMD_R_BL_POWER, &power))
		return;
	blightdev->props.power = power ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
}

static void ideapad_backlight_notify_brightness(struct ideapad_private *priv)
{
	unsigned long now;

	/* if we control brightness via acpi video driver */
	if (priv->blightdev == NULL) {
		read_ec_data(ideapad_handle, VPCCMD_R_BL, &now);
		return;
	}

	backlight_force_update(priv->blightdev, BACKLIGHT_UPDATE_HOTKEY);
}

/*
 * module init/exit
 */
static const struct acpi_device_id ideapad_device_ids[] = {
	{ "VPC2004", 0},
	{ "", 0},
};
MODULE_DEVICE_TABLE(acpi, ideapad_device_ids);

static void ideapad_sync_touchpad_state(struct acpi_device *adevice)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	unsigned long value;

	/* Without reading from EC touchpad LED doesn't switch state */
	if (!read_ec_data(adevice->handle, VPCCMD_R_TOUCHPAD, &value)) {
		/* Some IdeaPads don't really turn off touchpad - they only
		 * switch the LED state. We (de)activate KBC AUX port to turn
		 * touchpad off and on. We send KEY_TOUCHPAD_OFF and
		 * KEY_TOUCHPAD_ON to not to get out of sync with LED */
		unsigned char param;
		i8042_command(&param, value ? I8042_CMD_AUX_ENABLE :
			      I8042_CMD_AUX_DISABLE);
		ideapad_input_report(priv, value ? 67 : 66);
	}
}

static int __devinit ideapad_acpi_add(struct acpi_device *adevice)
{
	int ret, i;
	int cfg;
	struct ideapad_private *priv;

	if (read_method_int(adevice->handle, "_CFG", &cfg))
		return -ENODEV;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_set_drvdata(&adevice->dev, priv);
	ideapad_priv = priv;
	ideapad_handle = adevice->handle;
	priv->cfg = cfg;

	ret = ideapad_platform_init(priv);
	if (ret)
		goto platform_failed;

	ret = ideapad_debugfs_init(priv);
	if (ret)
		goto debugfs_failed;

	ret = ideapad_input_init(priv);
	if (ret)
		goto input_failed;

	for (i = 0; i < IDEAPAD_RFKILL_DEV_NUM; i++) {
		if (test_bit(ideapad_rfk_data[i].cfgbit, &priv->cfg))
			ideapad_register_rfkill(adevice, i);
		else
			priv->rfk[i] = NULL;
	}
	ideapad_sync_rfk_state(priv);
	ideapad_sync_touchpad_state(adevice);

	if (!acpi_video_backlight_support()) {
		ret = ideapad_backlight_init(priv);
		if (ret && ret != -ENODEV)
			goto backlight_failed;
	}

	return 0;

backlight_failed:
	for (i = 0; i < IDEAPAD_RFKILL_DEV_NUM; i++)
		ideapad_unregister_rfkill(adevice, i);
	ideapad_input_exit(priv);
input_failed:
	ideapad_debugfs_exit(priv);
debugfs_failed:
	ideapad_platform_exit(priv);
platform_failed:
	kfree(priv);
	return ret;
}

static int __devexit ideapad_acpi_remove(struct acpi_device *adevice, int type)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	int i;

	ideapad_backlight_exit(priv);
	for (i = 0; i < IDEAPAD_RFKILL_DEV_NUM; i++)
		ideapad_unregister_rfkill(adevice, i);
	ideapad_input_exit(priv);
	ideapad_debugfs_exit(priv);
	ideapad_platform_exit(priv);
	dev_set_drvdata(&adevice->dev, NULL);
	kfree(priv);

	return 0;
}

static void ideapad_acpi_notify(struct acpi_device *adevice, u32 event)
{
	struct ideapad_private *priv = dev_get_drvdata(&adevice->dev);
	acpi_handle handle = adevice->handle;
	unsigned long vpc1, vpc2, vpc_bit;

	if (read_ec_data(handle, VPCCMD_R_VPC1, &vpc1))
		return;
	if (read_ec_data(handle, VPCCMD_R_VPC2, &vpc2))
		return;

	vpc1 = (vpc2 << 8) | vpc1;
	for (vpc_bit = 0; vpc_bit < 16; vpc_bit++) {
		if (test_bit(vpc_bit, &vpc1)) {
			switch (vpc_bit) {
			case 9:
				ideapad_sync_rfk_state(priv);
				break;
			case 13:
			case 11:
			case 7:
			case 6:
				ideapad_input_report(priv, vpc_bit);
				break;
			case 5:
				ideapad_sync_touchpad_state(adevice);
				break;
			case 4:
				ideapad_backlight_notify_brightness(priv);
				break;
			case 3:
				ideapad_input_novokey(priv);
				break;
			case 2:
				ideapad_backlight_notify_power(priv);
				break;
			case 0:
				ideapad_check_special_buttons(priv);
				break;
			default:
				pr_info("Unknown event: %lu\n", vpc_bit);
			}
		}
	}
}

static int ideapad_acpi_resume(struct device *device)
{
	ideapad_sync_rfk_state(ideapad_priv);
	ideapad_sync_touchpad_state(to_acpi_device(device));
	return 0;
}

static SIMPLE_DEV_PM_OPS(ideapad_pm, NULL, ideapad_acpi_resume);

static struct acpi_driver ideapad_acpi_driver = {
	.name = "ideapad_acpi",
	.class = "IdeaPad",
	.ids = ideapad_device_ids,
	.ops.add = ideapad_acpi_add,
	.ops.remove = ideapad_acpi_remove,
	.ops.notify = ideapad_acpi_notify,
	.drv.pm = &ideapad_pm,
	.owner = THIS_MODULE,
};
module_acpi_driver(ideapad_acpi_driver);

MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("IdeaPad ACPI Extras");
MODULE_LICENSE("GPL");
