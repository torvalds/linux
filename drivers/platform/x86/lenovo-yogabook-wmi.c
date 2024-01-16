// SPDX-License-Identifier: GPL-2.0
/* WMI driver for Lenovo Yoga Book YB1-X90* / -X91* tablets */

#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/wmi.h>
#include <linux/workqueue.h>

#define YB_MBTN_EVENT_GUID	"243FEC1D-1963-41C1-8100-06A9D82A94B4"
#define YB_MBTN_METHOD_GUID	"742B0CA1-0B20-404B-9CAA-AEFCABF30CE0"

#define YB_PAD_ENABLE	1
#define YB_PAD_DISABLE	2
#define YB_LIGHTUP_BTN	3

#define YB_KBD_BL_DEFAULT 128

/* flags */
enum {
	YB_KBD_IS_ON,
	YB_DIGITIZER_IS_ON,
	YB_DIGITIZER_MODE,
	YB_TABLET_MODE,
	YB_SUSPENDED,
};

struct yogabook_wmi {
	struct wmi_device *wdev;
	struct acpi_device *kbd_adev;
	struct acpi_device *dig_adev;
	struct device *kbd_dev;
	struct device *dig_dev;
	struct gpio_desc *backside_hall_gpio;
	int backside_hall_irq;
	struct work_struct work;
	struct led_classdev kbd_bl_led;
	unsigned long flags;
	uint8_t brightness;
};

static int yogabook_wmi_do_action(struct wmi_device *wdev, int action)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer input;
	acpi_status status;
	u32 dummy_arg = 0;

	dev_dbg(&wdev->dev, "Do action: %d\n", action);

	input.pointer = &dummy_arg;
	input.length = sizeof(dummy_arg);

	status = wmi_evaluate_method(YB_MBTN_METHOD_GUID, 0, action, &input,
				     &output);
	if (ACPI_FAILURE(status)) {
		dev_err(&wdev->dev, "Calling WMI method failure: 0x%x\n",
			status);
		return status;
	}

	kfree(output.pointer);

	return 0;
}

/*
 * To control keyboard backlight, call the method KBLC() of the TCS1 ACPI
 * device (Goodix touchpad acts as virtual sensor keyboard).
 */
static int yogabook_wmi_set_kbd_backlight(struct wmi_device *wdev,
					  uint8_t level)
{
	struct yogabook_wmi *data = dev_get_drvdata(&wdev->dev);
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input;
	union acpi_object param;
	acpi_status status;

	if (data->kbd_adev->power.state != ACPI_STATE_D0) {
		dev_warn(&wdev->dev, "keyboard touchscreen not in D0, cannot set brightness\n");
		return -ENXIO;
	}

	dev_dbg(&wdev->dev, "Set KBLC level to %u\n", level);

	input.count = 1;
	input.pointer = &param;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = 255 - level;

	status = acpi_evaluate_object(acpi_device_handle(data->kbd_adev), "KBLC",
				      &input, &output);
	if (ACPI_FAILURE(status)) {
		dev_err(&wdev->dev, "Failed to call KBLC method: 0x%x\n", status);
		return status;
	}

	kfree(output.pointer);
	return 0;
}

static void yogabook_wmi_work(struct work_struct *work)
{
	struct yogabook_wmi *data = container_of(work, struct yogabook_wmi, work);
	struct device *dev = &data->wdev->dev;
	bool kbd_on, digitizer_on;
	int r;

	if (test_bit(YB_SUSPENDED, &data->flags))
		return;

	if (test_bit(YB_TABLET_MODE, &data->flags)) {
		kbd_on = false;
		digitizer_on = false;
	} else if (test_bit(YB_DIGITIZER_MODE, &data->flags)) {
		digitizer_on = true;
		kbd_on = false;
	} else {
		kbd_on = true;
		digitizer_on = false;
	}

	if (!kbd_on && test_bit(YB_KBD_IS_ON, &data->flags)) {
		/*
		 * Must be done before releasing the keyboard touchscreen driver,
		 * so that the keyboard touchscreen dev is still in D0.
		 */
		yogabook_wmi_set_kbd_backlight(data->wdev, 0);
		device_release_driver(data->kbd_dev);
		clear_bit(YB_KBD_IS_ON, &data->flags);
	}

	if (!digitizer_on && test_bit(YB_DIGITIZER_IS_ON, &data->flags)) {
		yogabook_wmi_do_action(data->wdev, YB_PAD_DISABLE);
		device_release_driver(data->dig_dev);
		clear_bit(YB_DIGITIZER_IS_ON, &data->flags);
	}

	if (kbd_on && !test_bit(YB_KBD_IS_ON, &data->flags)) {
		r = device_reprobe(data->kbd_dev);
		if (r)
			dev_warn(dev, "Reprobe of keyboard touchscreen failed: %d\n", r);

		yogabook_wmi_set_kbd_backlight(data->wdev, data->brightness);
		set_bit(YB_KBD_IS_ON, &data->flags);
	}

	if (digitizer_on && !test_bit(YB_DIGITIZER_IS_ON, &data->flags)) {
		r = device_reprobe(data->dig_dev);
		if (r)
			dev_warn(dev, "Reprobe of digitizer failed: %d\n", r);

		yogabook_wmi_do_action(data->wdev, YB_PAD_ENABLE);
		set_bit(YB_DIGITIZER_IS_ON, &data->flags);
	}
}

static void yogabook_wmi_notify(struct wmi_device *wdev, union acpi_object *dummy)
{
	struct yogabook_wmi *data = dev_get_drvdata(&wdev->dev);

	if (test_bit(YB_SUSPENDED, &data->flags))
		return;

	if (test_bit(YB_DIGITIZER_MODE, &data->flags))
		clear_bit(YB_DIGITIZER_MODE, &data->flags);
	else
		set_bit(YB_DIGITIZER_MODE, &data->flags);

	/*
	 * We are called from the ACPI core and the driver [un]binding which is
	 * done also needs ACPI functions, use a workqueue to avoid deadlocking.
	 */
	schedule_work(&data->work);
}

static irqreturn_t yogabook_backside_hall_irq(int irq, void *_data)
{
	struct yogabook_wmi *data = _data;

	if (gpiod_get_value(data->backside_hall_gpio))
		set_bit(YB_TABLET_MODE, &data->flags);
	else
		clear_bit(YB_TABLET_MODE, &data->flags);

	schedule_work(&data->work);

	return IRQ_HANDLED;
}

static enum led_brightness kbd_brightness_get(struct led_classdev *cdev)
{
	struct yogabook_wmi *data =
		container_of(cdev, struct yogabook_wmi, kbd_bl_led);

	return data->brightness;
}

static int kbd_brightness_set(struct led_classdev *cdev,
			      enum led_brightness value)
{
	struct yogabook_wmi *data =
		container_of(cdev, struct yogabook_wmi, kbd_bl_led);
	struct wmi_device *wdev = data->wdev;

	if ((value < 0) || (value > 255))
		return -EINVAL;

	data->brightness = value;

	if (data->kbd_adev->power.state != ACPI_STATE_D0)
		return 0;

	return yogabook_wmi_set_kbd_backlight(wdev, data->brightness);
}

static struct gpiod_lookup_table yogabook_wmi_gpios = {
	.dev_id		= "243FEC1D-1963-41C1-8100-06A9D82A94B4",
	.table		= {
		GPIO_LOOKUP("INT33FF:02", 18, "backside_hall_sw", GPIO_ACTIVE_LOW),
		{}
	},
};

static void yogabook_wmi_rm_gpio_lookup(void *unused)
{
	gpiod_remove_lookup_table(&yogabook_wmi_gpios);
}

static int yogabook_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct yogabook_wmi *data;
	int r;

	data = devm_kzalloc(&wdev->dev, sizeof(struct yogabook_wmi), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, data);

	data->wdev = wdev;
	data->brightness = YB_KBD_BL_DEFAULT;
	set_bit(YB_KBD_IS_ON, &data->flags);
	set_bit(YB_DIGITIZER_IS_ON, &data->flags);
	INIT_WORK(&data->work, yogabook_wmi_work);

	data->kbd_adev = acpi_dev_get_first_match_dev("GDIX1001", NULL, -1);
	if (!data->kbd_adev) {
		dev_err(&wdev->dev, "Cannot find the touchpad device in ACPI tables\n");
		return -ENODEV;
	}

	data->dig_adev = acpi_dev_get_first_match_dev("WCOM0019", NULL, -1);
	if (!data->dig_adev) {
		dev_err(&wdev->dev, "Cannot find the digitizer device in ACPI tables\n");
		r = -ENODEV;
		goto error_put_devs;
	}

	data->kbd_dev = get_device(acpi_get_first_physical_node(data->kbd_adev));
	if (!data->kbd_dev || !data->kbd_dev->driver) {
		r = -EPROBE_DEFER;
		goto error_put_devs;
	}

	data->dig_dev = get_device(acpi_get_first_physical_node(data->dig_adev));
	if (!data->dig_dev || !data->dig_dev->driver) {
		r = -EPROBE_DEFER;
		goto error_put_devs;
	}

	gpiod_add_lookup_table(&yogabook_wmi_gpios);

	r = devm_add_action_or_reset(&wdev->dev, yogabook_wmi_rm_gpio_lookup, NULL);
	if (r)
		goto error_put_devs;

	data->backside_hall_gpio =
		devm_gpiod_get(&wdev->dev, "backside_hall_sw", GPIOD_IN);
	if (IS_ERR(data->backside_hall_gpio)) {
		r = PTR_ERR(data->backside_hall_gpio);
		dev_err_probe(&wdev->dev, r, "Getting backside_hall_sw GPIO\n");
		goto error_put_devs;
	}

	r = gpiod_to_irq(data->backside_hall_gpio);
	if (r < 0) {
		dev_err_probe(&wdev->dev, r, "Getting backside_hall_sw IRQ\n");
		goto error_put_devs;
	}
	data->backside_hall_irq = r;

	/* Set default brightness before enabling the IRQ */
	yogabook_wmi_set_kbd_backlight(data->wdev, YB_KBD_BL_DEFAULT);

	r = request_irq(data->backside_hall_irq, yogabook_backside_hall_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"backside_hall_sw", data);
	if (r) {
		dev_err_probe(&wdev->dev, r, "Requesting backside_hall_sw IRQ\n");
		goto error_put_devs;
	}

	schedule_work(&data->work);

	data->kbd_bl_led.name = "ybwmi::kbd_backlight";
	data->kbd_bl_led.brightness_set_blocking = kbd_brightness_set;
	data->kbd_bl_led.brightness_get = kbd_brightness_get;
	data->kbd_bl_led.max_brightness = 255;

	r = devm_led_classdev_register(&wdev->dev, &data->kbd_bl_led);
	if (r < 0) {
		dev_err_probe(&wdev->dev, r, "Registering backlight LED device\n");
		goto error_free_irq;
	}

	return 0;

error_free_irq:
	free_irq(data->backside_hall_irq, data);
	cancel_work_sync(&data->work);
error_put_devs:
	put_device(data->dig_dev);
	put_device(data->kbd_dev);
	acpi_dev_put(data->dig_adev);
	acpi_dev_put(data->kbd_adev);
	return r;
}

static void yogabook_wmi_remove(struct wmi_device *wdev)
{
	struct yogabook_wmi *data = dev_get_drvdata(&wdev->dev);
	int r = 0;

	free_irq(data->backside_hall_irq, data);
	cancel_work_sync(&data->work);

	if (!test_bit(YB_KBD_IS_ON, &data->flags))
		r |= device_reprobe(data->kbd_dev);

	if (!test_bit(YB_DIGITIZER_IS_ON, &data->flags))
		r |= device_reprobe(data->dig_dev);

	if (r)
		dev_warn(&wdev->dev, "Reprobe of devices failed\n");

	put_device(data->dig_dev);
	put_device(data->kbd_dev);
	acpi_dev_put(data->dig_adev);
	acpi_dev_put(data->kbd_adev);
}

static int __maybe_unused yogabook_wmi_suspend(struct device *dev)
{
	struct wmi_device *wdev = container_of(dev, struct wmi_device, dev);
	struct yogabook_wmi *data = dev_get_drvdata(dev);

	set_bit(YB_SUSPENDED, &data->flags);

	flush_work(&data->work);

	/* Turn off the pen button at sleep */
	if (test_bit(YB_DIGITIZER_IS_ON, &data->flags))
		yogabook_wmi_do_action(wdev, YB_PAD_DISABLE);

	return 0;
}

static int __maybe_unused yogabook_wmi_resume(struct device *dev)
{
	struct wmi_device *wdev = container_of(dev, struct wmi_device, dev);
	struct yogabook_wmi *data = dev_get_drvdata(dev);

	if (test_bit(YB_KBD_IS_ON, &data->flags)) {
		/* Ensure keyboard touchpad is on before we call KBLC() */
		acpi_device_set_power(data->kbd_adev, ACPI_STATE_D0);
		yogabook_wmi_set_kbd_backlight(wdev, data->brightness);
	}

	if (test_bit(YB_DIGITIZER_IS_ON, &data->flags))
		yogabook_wmi_do_action(wdev, YB_PAD_ENABLE);

	clear_bit(YB_SUSPENDED, &data->flags);

	/* Check for YB_TABLET_MODE changes made during suspend */
	schedule_work(&data->work);

	return 0;
}

static const struct wmi_device_id yogabook_wmi_id_table[] = {
	{
		.guid_string = YB_MBTN_EVENT_GUID,
	},
	{ } /* Terminating entry */
};

static SIMPLE_DEV_PM_OPS(yogabook_wmi_pm_ops,
			 yogabook_wmi_suspend, yogabook_wmi_resume);

static struct wmi_driver yogabook_wmi_driver = {
	.driver = {
		.name = "yogabook-wmi",
		.pm = &yogabook_wmi_pm_ops,
	},
	.no_notify_data = true,
	.id_table = yogabook_wmi_id_table,
	.probe = yogabook_wmi_probe,
	.remove = yogabook_wmi_remove,
	.notify = yogabook_wmi_notify,
};
module_wmi_driver(yogabook_wmi_driver);

MODULE_DEVICE_TABLE(wmi, yogabook_wmi_id_table);
MODULE_AUTHOR("Yauhen Kharuzhy");
MODULE_DESCRIPTION("Lenovo Yoga Book WMI driver");
MODULE_LICENSE("GPL v2");
