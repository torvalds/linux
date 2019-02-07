// SPDX-License-Identifier: GPL-2.0
/*
 *  Huawei WMI hotkeys
 *
 *  Copyright (C) 2018	      Ayman Bagabas <ayman.bagabas@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/wmi.h>

/*
 * Huawei WMI GUIDs
 */
#define WMI0_EVENT_GUID "59142400-C6A3-40fa-BADB-8A2652834100"
#define AMW0_EVENT_GUID "ABBC0F5C-8EA1-11D1-A000-C90629100000"

#define WMI0_EXPENSIVE_GUID "39142400-C6A3-40fa-BADB-8A2652834100"

struct huawei_wmi_priv {
	struct input_dev *idev;
	struct led_classdev cdev;
	acpi_handle handle;
	char *acpi_method;
};

static const struct key_entry huawei_wmi_keymap[] = {
	{ KE_KEY,    0x281, { KEY_BRIGHTNESSDOWN } },
	{ KE_KEY,    0x282, { KEY_BRIGHTNESSUP } },
	{ KE_KEY,    0x284, { KEY_MUTE } },
	{ KE_KEY,    0x285, { KEY_VOLUMEDOWN } },
	{ KE_KEY,    0x286, { KEY_VOLUMEUP } },
	{ KE_KEY,    0x287, { KEY_MICMUTE } },
	{ KE_KEY,    0x289, { KEY_WLAN } },
	// Huawei |M| key
	{ KE_KEY,    0x28a, { KEY_CONFIG } },
	// Keyboard backlight
	{ KE_IGNORE, 0x293, { KEY_KBDILLUMTOGGLE } },
	{ KE_IGNORE, 0x294, { KEY_KBDILLUMUP } },
	{ KE_IGNORE, 0x295, { KEY_KBDILLUMUP } },
	{ KE_END,	 0 }
};

static int huawei_wmi_micmute_led_set(struct led_classdev *led_cdev,
		enum led_brightness brightness)
{
	struct huawei_wmi_priv *priv = dev_get_drvdata(led_cdev->dev->parent);
	acpi_status status;
	union acpi_object args[3];
	struct acpi_object_list arg_list = {
		.pointer = args,
		.count = ARRAY_SIZE(args),
	};

	args[0].type = args[1].type = args[2].type = ACPI_TYPE_INTEGER;
	args[1].integer.value = 0x04;

	if (strcmp(priv->acpi_method, "SPIN") == 0) {
		args[0].integer.value = 0;
		args[2].integer.value = brightness ? 1 : 0;
	} else if (strcmp(priv->acpi_method, "WPIN") == 0) {
		args[0].integer.value = 1;
		args[2].integer.value = brightness ? 0 : 1;
	} else {
		return -EINVAL;
	}

	status = acpi_evaluate_object(priv->handle, priv->acpi_method, &arg_list, NULL);
	if (ACPI_FAILURE(status))
		return -ENXIO;

	return 0;
}

static int huawei_wmi_leds_setup(struct wmi_device *wdev)
{
	struct huawei_wmi_priv *priv = dev_get_drvdata(&wdev->dev);

	priv->handle = ec_get_handle();
	if (!priv->handle)
		return 0;

	if (acpi_has_method(priv->handle, "SPIN"))
		priv->acpi_method = "SPIN";
	else if (acpi_has_method(priv->handle, "WPIN"))
		priv->acpi_method = "WPIN";
	else
		return 0;

	priv->cdev.name = "platform::micmute";
	priv->cdev.max_brightness = 1;
	priv->cdev.brightness_set_blocking = huawei_wmi_micmute_led_set;
	priv->cdev.default_trigger = "audio-micmute";
	priv->cdev.brightness = ledtrig_audio_get(LED_AUDIO_MICMUTE);
	priv->cdev.dev = &wdev->dev;
	priv->cdev.flags = LED_CORE_SUSPENDRESUME;

	return devm_led_classdev_register(&wdev->dev, &priv->cdev);
}

static void huawei_wmi_process_key(struct wmi_device *wdev, int code)
{
	struct huawei_wmi_priv *priv = dev_get_drvdata(&wdev->dev);
	const struct key_entry *key;

	/*
	 * WMI0 uses code 0x80 to indicate a hotkey event.
	 * The actual key is fetched from the method WQ00
	 * using WMI0_EXPENSIVE_GUID.
	 */
	if (code == 0x80) {
		struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
		union acpi_object *obj;
		acpi_status status;

		status = wmi_query_block(WMI0_EXPENSIVE_GUID, 0, &response);
		if (ACPI_FAILURE(status))
			return;

		obj = (union acpi_object *)response.pointer;
		if (obj && obj->type == ACPI_TYPE_INTEGER)
			code = obj->integer.value;

		kfree(response.pointer);
	}

	key = sparse_keymap_entry_from_scancode(priv->idev, code);
	if (!key) {
		dev_info(&wdev->dev, "Unknown key pressed, code: 0x%04x\n", code);
		return;
	}

	sparse_keymap_report_entry(priv->idev, key, 1, true);
}

static void huawei_wmi_notify(struct wmi_device *wdev,
		union acpi_object *obj)
{
	if (obj->type == ACPI_TYPE_INTEGER)
		huawei_wmi_process_key(wdev, obj->integer.value);
	else
		dev_info(&wdev->dev, "Bad response type %d\n", obj->type);
}

static int huawei_wmi_input_setup(struct wmi_device *wdev)
{
	struct huawei_wmi_priv *priv = dev_get_drvdata(&wdev->dev);
	int err;

	priv->idev = devm_input_allocate_device(&wdev->dev);
	if (!priv->idev)
		return -ENOMEM;

	priv->idev->name = "Huawei WMI hotkeys";
	priv->idev->phys = "wmi/input0";
	priv->idev->id.bustype = BUS_HOST;
	priv->idev->dev.parent = &wdev->dev;

	err = sparse_keymap_setup(priv->idev, huawei_wmi_keymap, NULL);
	if (err)
		return err;

	return input_register_device(priv->idev);
}

static int huawei_wmi_probe(struct wmi_device *wdev)
{
	struct huawei_wmi_priv *priv;
	int err;

	priv = devm_kzalloc(&wdev->dev, sizeof(struct huawei_wmi_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, priv);

	err = huawei_wmi_input_setup(wdev);
	if (err)
		return err;

	return huawei_wmi_leds_setup(wdev);
}

static const struct wmi_device_id huawei_wmi_id_table[] = {
	{ .guid_string = WMI0_EVENT_GUID },
	{ .guid_string = AMW0_EVENT_GUID },
	{  }
};

static struct wmi_driver huawei_wmi_driver = {
	.driver = {
		.name = "huawei-wmi",
	},
	.id_table = huawei_wmi_id_table,
	.probe = huawei_wmi_probe,
	.notify = huawei_wmi_notify,
};

module_wmi_driver(huawei_wmi_driver);

MODULE_ALIAS("wmi:"WMI0_EVENT_GUID);
MODULE_ALIAS("wmi:"AMW0_EVENT_GUID);
MODULE_AUTHOR("Ayman Bagabas <ayman.bagabas@gmail.com>");
MODULE_DESCRIPTION("Huawei WMI hotkeys");
MODULE_LICENSE("GPL v2");
