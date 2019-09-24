// SPDX-License-Identifier: GPL-2.0
/*
 *  Huawei WMI laptop extras driver
 *
 *  Copyright (C) 2018	      Ayman Bagabas <ayman.bagabas@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/wmi.h>

/*
 * Huawei WMI GUIDs
 */
#define HWMI_EVENT_GUID "ABBC0F5C-8EA1-11D1-A000-C90629100000"

/* Legacy GUIDs */
#define WMI0_EXPENSIVE_GUID "39142400-C6A3-40fa-BADB-8A2652834100"
#define WMI0_EVENT_GUID "59142400-C6A3-40fa-BADB-8A2652834100"

struct huawei_wmi {
	struct input_dev *idev[2];
	struct led_classdev cdev;
	struct platform_device *pdev;
	acpi_handle handle;
	char *acpi_method;
};

struct huawei_wmi *huawei_wmi;

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
	// Keyboard backlit
	{ KE_IGNORE, 0x293, { KEY_KBDILLUMTOGGLE } },
	{ KE_IGNORE, 0x294, { KEY_KBDILLUMUP } },
	{ KE_IGNORE, 0x295, { KEY_KBDILLUMUP } },
	{ KE_END,	 0 }
};

static int huawei_wmi_micmute_led_set(struct led_classdev *led_cdev,
		enum led_brightness brightness)
{
	struct huawei_wmi *huawei = dev_get_drvdata(led_cdev->dev->parent);
	acpi_status status;
	union acpi_object args[3];
	struct acpi_object_list arg_list = {
		.pointer = args,
		.count = ARRAY_SIZE(args),
	};

	args[0].type = args[1].type = args[2].type = ACPI_TYPE_INTEGER;
	args[1].integer.value = 0x04;

	if (strcmp(huawei->acpi_method, "SPIN") == 0) {
		args[0].integer.value = 0;
		args[2].integer.value = brightness ? 1 : 0;
	} else if (strcmp(huawei->acpi_method, "WPIN") == 0) {
		args[0].integer.value = 1;
		args[2].integer.value = brightness ? 0 : 1;
	} else {
		return -EINVAL;
	}

	status = acpi_evaluate_object(huawei->handle, huawei->acpi_method, &arg_list, NULL);
	if (ACPI_FAILURE(status))
		return -ENXIO;

	return 0;
}

static void huawei_wmi_leds_setup(struct device *dev)
{
	struct huawei_wmi *huawei = dev_get_drvdata(dev);

	huawei->handle = ec_get_handle();
	if (!huawei->handle)
		return;

	if (acpi_has_method(huawei->handle, "SPIN"))
		huawei->acpi_method = "SPIN";
	else if (acpi_has_method(huawei->handle, "WPIN"))
		huawei->acpi_method = "WPIN";
	else
		return;

	huawei->cdev.name = "platform::micmute";
	huawei->cdev.max_brightness = 1;
	huawei->cdev.brightness_set_blocking = &huawei_wmi_micmute_led_set;
	huawei->cdev.default_trigger = "audio-micmute";
	huawei->cdev.brightness = ledtrig_audio_get(LED_AUDIO_MICMUTE);
	huawei->cdev.dev = dev;
	huawei->cdev.flags = LED_CORE_SUSPENDRESUME;

	devm_led_classdev_register(dev, &huawei->cdev);
}

/* Input */

static void huawei_wmi_process_key(struct input_dev *idev, int code)
{
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

	key = sparse_keymap_entry_from_scancode(idev, code);
	if (!key) {
		dev_info(&idev->dev, "Unknown key pressed, code: 0x%04x\n", code);
		return;
	}

	sparse_keymap_report_entry(idev, key, 1, true);
}

static void huawei_wmi_input_notify(u32 value, void *context)
{
	struct input_dev *idev = (struct input_dev *)context;
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = wmi_get_event_data(value, &response);
	if (ACPI_FAILURE(status)) {
		dev_err(&idev->dev, "Unable to get event data\n");
		return;
	}

	obj = (union acpi_object *)response.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER)
		huawei_wmi_process_key(idev, obj->integer.value);
	else
		dev_err(&idev->dev, "Bad response type\n");

	kfree(response.pointer);
}

static int huawei_wmi_input_setup(struct device *dev,
		const char *guid,
		struct input_dev **idev)
{
	*idev = devm_input_allocate_device(dev);
	if (!*idev)
		return -ENOMEM;

	(*idev)->name = "Huawei WMI hotkeys";
	(*idev)->phys = "wmi/input0";
	(*idev)->id.bustype = BUS_HOST;
	(*idev)->dev.parent = dev;

	return sparse_keymap_setup(*idev, huawei_wmi_keymap, NULL) ||
		input_register_device(*idev) ||
		wmi_install_notify_handler(guid, huawei_wmi_input_notify,
				*idev);
}

static void huawei_wmi_input_exit(struct device *dev, const char *guid)
{
	wmi_remove_notify_handler(guid);
}

/* Huawei driver */

static const struct wmi_device_id huawei_wmi_events_id_table[] = {
	{ .guid_string = WMI0_EVENT_GUID },
	{  }
};

static int huawei_wmi_probe(struct platform_device *pdev)
{
	const struct wmi_device_id *guid = huawei_wmi_events_id_table;
	int err;

	platform_set_drvdata(pdev, huawei_wmi);
	huawei_wmi->pdev = pdev;

	while (*guid->guid_string) {
		struct input_dev *idev = *huawei_wmi->idev;

		if (wmi_has_guid(guid->guid_string)) {
			err = huawei_wmi_input_setup(&pdev->dev, guid->guid_string, &idev);
			if (err) {
				dev_err(&pdev->dev, "Failed to setup input on %s\n", guid->guid_string);
				return err;
			}
		}

		idev++;
		guid++;
	}

	huawei_wmi_leds_setup(&pdev->dev);
	return 0;
}

static int huawei_wmi_remove(struct platform_device *pdev)
{
	const struct wmi_device_id *guid = huawei_wmi_events_id_table;

	while (*guid->guid_string) {
		if (wmi_has_guid(guid->guid_string))
			huawei_wmi_input_exit(&pdev->dev, guid->guid_string);

		guid++;
	}

	return 0;
}

static struct platform_driver huawei_wmi_driver = {
	.driver = {
		.name = "huawei-wmi",
	},
	.probe = huawei_wmi_probe,
	.remove = huawei_wmi_remove,
};

static __init int huawei_wmi_init(void)
{
	struct platform_device *pdev;
	int err;

	huawei_wmi = kzalloc(sizeof(struct huawei_wmi), GFP_KERNEL);
	if (!huawei_wmi)
		return -ENOMEM;

	err = platform_driver_register(&huawei_wmi_driver);
	if (err)
		goto pdrv_err;

	pdev = platform_device_register_simple("huawei-wmi", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		err = PTR_ERR(pdev);
		goto pdev_err;
	}

	return 0;

pdev_err:
	platform_driver_unregister(&huawei_wmi_driver);
pdrv_err:
	kfree(huawei_wmi);
	return err;
}

static __exit void huawei_wmi_exit(void)
{
	platform_device_unregister(huawei_wmi->pdev);
	platform_driver_unregister(&huawei_wmi_driver);
}

module_init(huawei_wmi_init);
module_exit(huawei_wmi_exit);

MODULE_DEVICE_TABLE(wmi, huawei_wmi_events_id_table);
MODULE_AUTHOR("Ayman Bagabas <ayman.bagabas@gmail.com>");
MODULE_DESCRIPTION("Huawei WMI laptop extras driver");
MODULE_LICENSE("GPL v2");
