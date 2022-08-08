// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dell privacy notification driver
 *
 * Copyright (C) 2021 Dell Inc. All Rights Reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/list.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/wmi.h>

#include "dell-wmi-privacy.h"

#define DELL_PRIVACY_GUID "6932965F-1671-4CEB-B988-D3AB0A901919"
#define MICROPHONE_STATUS		BIT(0)
#define CAMERA_STATUS		        BIT(1)
#define DELL_PRIVACY_AUDIO_EVENT  0x1
#define DELL_PRIVACY_CAMERA_EVENT 0x2
#define led_to_priv(c)       container_of(c, struct privacy_wmi_data, cdev)

/*
 * The wmi_list is used to store the privacy_priv struct with mutex protecting
 */
static LIST_HEAD(wmi_list);
static DEFINE_MUTEX(list_mutex);

struct privacy_wmi_data {
	struct input_dev *input_dev;
	struct wmi_device *wdev;
	struct list_head list;
	struct led_classdev cdev;
	u32 features_present;
	u32 last_status;
};

/* DELL Privacy Type */
enum dell_hardware_privacy_type {
	DELL_PRIVACY_TYPE_AUDIO = 0,
	DELL_PRIVACY_TYPE_CAMERA,
	DELL_PRIVACY_TYPE_SCREEN,
	DELL_PRIVACY_TYPE_MAX,
};

static const char * const privacy_types[DELL_PRIVACY_TYPE_MAX] = {
	[DELL_PRIVACY_TYPE_AUDIO] = "Microphone",
	[DELL_PRIVACY_TYPE_CAMERA] = "Camera Shutter",
	[DELL_PRIVACY_TYPE_SCREEN] = "ePrivacy Screen",
};

/*
 * Keymap for WMI privacy events of type 0x0012
 */
static const struct key_entry dell_wmi_keymap_type_0012[] = {
	/* privacy mic mute */
	{ KE_KEY, 0x0001, { KEY_MICMUTE } },
	/* privacy camera mute */
	{ KE_SW,  0x0002, { SW_CAMERA_LENS_COVER } },
	{ KE_END, 0},
};

bool dell_privacy_has_mic_mute(void)
{
	struct privacy_wmi_data *priv;

	mutex_lock(&list_mutex);
	priv = list_first_entry_or_null(&wmi_list,
			struct privacy_wmi_data,
			list);
	mutex_unlock(&list_mutex);

	return priv && (priv->features_present & BIT(DELL_PRIVACY_TYPE_AUDIO));
}
EXPORT_SYMBOL_GPL(dell_privacy_has_mic_mute);

/*
 * The flow of privacy event:
 * 1) User presses key. HW does stuff with this key (timeout is started)
 * 2) WMI event is emitted from BIOS
 * 3) WMI event is received by dell-privacy
 * 4) KEY_MICMUTE emitted from dell-privacy
 * 5) Userland picks up key and modifies kcontrol for SW mute
 * 6) Codec kernel driver catches and calls ledtrig_audio_set which will call
 *    led_set_brightness() on the LED registered by dell_privacy_leds_setup()
 * 7) dell-privacy notifies EC, the timeout is cancelled and the HW mute activates.
 *    If the EC is not notified then the HW mic mute will activate when the timeout
 *    triggers, just a bit later than with the active ack.
 */
bool dell_privacy_process_event(int type, int code, int status)
{
	struct privacy_wmi_data *priv;
	const struct key_entry *key;
	bool ret = false;

	mutex_lock(&list_mutex);
	priv = list_first_entry_or_null(&wmi_list,
			struct privacy_wmi_data,
			list);
	if (!priv)
		goto error;

	key = sparse_keymap_entry_from_scancode(priv->input_dev, (type << 16) | code);
	if (!key) {
		dev_warn(&priv->wdev->dev, "Unknown key with type 0x%04x and code 0x%04x pressed\n",
			type, code);
		goto error;
	}
	dev_dbg(&priv->wdev->dev, "Key with type 0x%04x and code 0x%04x pressed\n", type, code);

	switch (code) {
	case DELL_PRIVACY_AUDIO_EVENT: /* Mic mute */
	case DELL_PRIVACY_CAMERA_EVENT: /* Camera mute */
		priv->last_status = status;
		sparse_keymap_report_entry(priv->input_dev, key, 1, true);
		ret = true;
		break;
	default:
		dev_dbg(&priv->wdev->dev, "unknown event type 0x%04x 0x%04x\n", type, code);
	}

error:
	mutex_unlock(&list_mutex);
	return ret;
}

static ssize_t dell_privacy_supported_type_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct privacy_wmi_data *priv = dev_get_drvdata(dev);
	enum dell_hardware_privacy_type type;
	u32 privacy_list;
	int len = 0;

	privacy_list = priv->features_present;
	for (type = DELL_PRIVACY_TYPE_AUDIO; type < DELL_PRIVACY_TYPE_MAX; type++) {
		if (privacy_list & BIT(type))
			len += sysfs_emit_at(buf, len, "[%s] [supported]\n", privacy_types[type]);
		else
			len += sysfs_emit_at(buf, len, "[%s] [unsupported]\n", privacy_types[type]);
	}

	return len;
}

static ssize_t dell_privacy_current_state_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct privacy_wmi_data *priv = dev_get_drvdata(dev);
	u32 privacy_supported = priv->features_present;
	enum dell_hardware_privacy_type type;
	u32 privacy_state = priv->last_status;
	int len = 0;

	for (type = DELL_PRIVACY_TYPE_AUDIO; type < DELL_PRIVACY_TYPE_MAX; type++) {
		if (privacy_supported & BIT(type)) {
			if (privacy_state & BIT(type))
				len += sysfs_emit_at(buf, len, "[%s] [unmuted]\n", privacy_types[type]);
			else
				len += sysfs_emit_at(buf, len, "[%s] [muted]\n", privacy_types[type]);
		}
	}

	return len;
}

static DEVICE_ATTR_RO(dell_privacy_supported_type);
static DEVICE_ATTR_RO(dell_privacy_current_state);

static struct attribute *privacy_attributes[] = {
	&dev_attr_dell_privacy_supported_type.attr,
	&dev_attr_dell_privacy_current_state.attr,
	NULL,
};

static const struct attribute_group privacy_attribute_group = {
	.attrs = privacy_attributes
};

/*
 * Describes the Device State class exposed by BIOS which can be consumed by
 * various applications interested in knowing the Privacy feature capabilities.
 * class DeviceState
 * {
 *  [key, read] string InstanceName;
 *  [read] boolean ReadOnly;
 *
 *  [WmiDataId(1), read] uint32 DevicesSupported;
 *   0 - None; 0x1 - Microphone; 0x2 - Camera; 0x4 - ePrivacy  Screen
 *
 *  [WmiDataId(2), read] uint32 CurrentState;
 *   0 - Off; 1 - On; Bit0 - Microphone; Bit1 - Camera; Bit2 - ePrivacyScreen
 * };
 */
static int get_current_status(struct wmi_device *wdev)
{
	struct privacy_wmi_data *priv = dev_get_drvdata(&wdev->dev);
	union acpi_object *obj_present;
	u32 *buffer;
	int ret = 0;

	if (!priv) {
		dev_err(&wdev->dev, "dell privacy priv is NULL\n");
		return -EINVAL;
	}
	/* check privacy support features and device states */
	obj_present = wmidev_block_query(wdev, 0);
	if (!obj_present) {
		dev_err(&wdev->dev, "failed to read Binary MOF\n");
		return -EIO;
	}

	if (obj_present->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Binary MOF is not a buffer!\n");
		ret = -EIO;
		goto obj_free;
	}
	/*  Although it's not technically a failure, this would lead to
	 *  unexpected behavior
	 */
	if (obj_present->buffer.length != 8) {
		dev_err(&wdev->dev, "Dell privacy buffer has unexpected length (%d)!\n",
				obj_present->buffer.length);
		ret = -EINVAL;
		goto obj_free;
	}
	buffer = (u32 *)obj_present->buffer.pointer;
	priv->features_present = buffer[0];
	priv->last_status = buffer[1];

obj_free:
	kfree(obj_present);
	return ret;
}

static int dell_privacy_micmute_led_set(struct led_classdev *led_cdev,
					enum led_brightness brightness)
{
	struct privacy_wmi_data *priv = led_to_priv(led_cdev);
	static char *acpi_method = (char *)"ECAK";
	acpi_status status;
	acpi_handle handle;

	handle = ec_get_handle();
	if (!handle)
		return -EIO;

	if (!acpi_has_method(handle, acpi_method))
		return -EIO;

	status = acpi_evaluate_object(handle, acpi_method, NULL, NULL);
	if (ACPI_FAILURE(status)) {
		dev_err(&priv->wdev->dev, "Error setting privacy EC ack value: %s\n",
				acpi_format_exception(status));
		return -EIO;
	}

	return 0;
}

/*
 * Pressing the mute key activates a time delayed circuit to physically cut
 * off the mute. The LED is in the same circuit, so it reflects the true
 * state of the HW mute.  The reason for the EC "ack" is so that software
 * can first invoke a SW mute before the HW circuit is cut off.  Without SW
 * cutting this off first does not affect the time delayed muting or status
 * of the LED but there is a possibility of a "popping" noise.
 *
 * If the EC receives the SW ack, the circuit will be activated before the
 * delay completed.
 *
 * Exposing as an LED device allows the codec drivers notification path to
 * EC ACK to work
 */
static int dell_privacy_leds_setup(struct device *dev)
{
	struct privacy_wmi_data *priv = dev_get_drvdata(dev);

	priv->cdev.name = "dell-privacy::micmute";
	priv->cdev.max_brightness = 1;
	priv->cdev.brightness_set_blocking = dell_privacy_micmute_led_set;
	priv->cdev.default_trigger = "audio-micmute";
	priv->cdev.brightness = ledtrig_audio_get(LED_AUDIO_MICMUTE);
	return devm_led_classdev_register(dev, &priv->cdev);
}

static int dell_privacy_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct privacy_wmi_data *priv;
	struct key_entry *keymap;
	int ret, i;

	ret = wmi_has_guid(DELL_PRIVACY_GUID);
	if (!ret)
		pr_debug("Unable to detect available Dell privacy devices!\n");

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, priv);
	priv->wdev = wdev;
	/* create evdev passing interface */
	priv->input_dev = devm_input_allocate_device(&wdev->dev);
	if (!priv->input_dev)
		return -ENOMEM;

	/* remap the wmi keymap event to new keymap */
	keymap = kcalloc(ARRAY_SIZE(dell_wmi_keymap_type_0012),
			sizeof(struct key_entry), GFP_KERNEL);
	if (!keymap)
		return -ENOMEM;

	/* remap the keymap code with Dell privacy key type 0x12 as prefix
	 * KEY_MICMUTE scancode will be reported as 0x120001
	 */
	for (i = 0; i < ARRAY_SIZE(dell_wmi_keymap_type_0012); i++) {
		keymap[i] = dell_wmi_keymap_type_0012[i];
		keymap[i].code |= (0x0012 << 16);
	}
	ret = sparse_keymap_setup(priv->input_dev, keymap, NULL);
	kfree(keymap);
	if (ret)
		return ret;

	priv->input_dev->dev.parent = &wdev->dev;
	priv->input_dev->name = "Dell Privacy Driver";
	priv->input_dev->id.bustype = BUS_HOST;

	ret = input_register_device(priv->input_dev);
	if (ret)
		return ret;

	ret = get_current_status(priv->wdev);
	if (ret)
		return ret;

	ret = devm_device_add_group(&wdev->dev, &privacy_attribute_group);
	if (ret)
		return ret;

	if (priv->features_present & BIT(DELL_PRIVACY_TYPE_AUDIO)) {
		ret = dell_privacy_leds_setup(&priv->wdev->dev);
		if (ret)
			return ret;
	}
	mutex_lock(&list_mutex);
	list_add_tail(&priv->list, &wmi_list);
	mutex_unlock(&list_mutex);
	return 0;
}

static void dell_privacy_wmi_remove(struct wmi_device *wdev)
{
	struct privacy_wmi_data *priv = dev_get_drvdata(&wdev->dev);

	mutex_lock(&list_mutex);
	list_del(&priv->list);
	mutex_unlock(&list_mutex);
}

static const struct wmi_device_id dell_wmi_privacy_wmi_id_table[] = {
	{ .guid_string = DELL_PRIVACY_GUID },
	{ },
};

static struct wmi_driver dell_privacy_wmi_driver = {
	.driver = {
		.name = "dell-privacy",
	},
	.probe = dell_privacy_wmi_probe,
	.remove = dell_privacy_wmi_remove,
	.id_table = dell_wmi_privacy_wmi_id_table,
};

int dell_privacy_register_driver(void)
{
	return wmi_driver_register(&dell_privacy_wmi_driver);
}

void dell_privacy_unregister_driver(void)
{
	wmi_driver_unregister(&dell_privacy_wmi_driver);
}
