// SPDX-License-Identifier: GPL-2.0
/*
 * Platform driver for Lenovo Yoga Book YB1-X90F/L tablets (Android model)
 * WMI driver for Lenovo Yoga Book YB1-X91F/L tablets (Windows model)
 *
 * The keyboard half of the YB1 models can function as both a capacitive
 * touch keyboard or as a Wacom digitizer, but not at the same time.
 *
 * This driver takes care of switching between the 2 functions.
 *
 * Copyright 2023 Hans de Goede <hansg@kernel.org>
 */

#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/wmi.h>
#include <linux/workqueue.h>

#define YB_MBTN_EVENT_GUID	"243FEC1D-1963-41C1-8100-06A9D82A94B4"

#define YB_KBD_BL_DEFAULT	128
#define YB_KBD_BL_MAX		255
#define YB_KBD_BL_PWM_PERIOD	13333

#define YB_PDEV_NAME		"yogabook-touch-kbd-digitizer-switch"

/* flags */
enum {
	YB_KBD_IS_ON,
	YB_DIGITIZER_IS_ON,
	YB_DIGITIZER_MODE,
	YB_TABLET_MODE,
	YB_SUSPENDED,
};

struct yogabook_data {
	struct device *dev;
	struct acpi_device *kbd_adev;
	struct acpi_device *dig_adev;
	struct device *kbd_dev;
	struct device *dig_dev;
	struct led_classdev *pen_led;
	struct gpio_desc *pen_touch_event;
	struct gpio_desc *kbd_bl_led_enable;
	struct gpio_desc *backside_hall_gpio;
	struct pwm_device *kbd_bl_pwm;
	int (*set_kbd_backlight)(struct yogabook_data *data, uint8_t level);
	int pen_touch_irq;
	int backside_hall_irq;
	struct work_struct work;
	struct led_classdev kbd_bl_led;
	unsigned long flags;
	uint8_t brightness;
};

static void yogabook_work(struct work_struct *work)
{
	struct yogabook_data *data = container_of(work, struct yogabook_data, work);
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
		data->set_kbd_backlight(data, 0);
		device_release_driver(data->kbd_dev);
		clear_bit(YB_KBD_IS_ON, &data->flags);
	}

	if (!digitizer_on && test_bit(YB_DIGITIZER_IS_ON, &data->flags)) {
		led_set_brightness(data->pen_led, LED_OFF);
		device_release_driver(data->dig_dev);
		clear_bit(YB_DIGITIZER_IS_ON, &data->flags);
	}

	if (kbd_on && !test_bit(YB_KBD_IS_ON, &data->flags)) {
		r = device_reprobe(data->kbd_dev);
		if (r)
			dev_warn(data->dev, "Reprobe of keyboard touchscreen failed: %d\n", r);

		data->set_kbd_backlight(data, data->brightness);
		set_bit(YB_KBD_IS_ON, &data->flags);
	}

	if (digitizer_on && !test_bit(YB_DIGITIZER_IS_ON, &data->flags)) {
		r = device_reprobe(data->dig_dev);
		if (r)
			dev_warn(data->dev, "Reprobe of digitizer failed: %d\n", r);

		led_set_brightness(data->pen_led, LED_FULL);
		set_bit(YB_DIGITIZER_IS_ON, &data->flags);
	}
}

static void yogabook_toggle_digitizer_mode(struct yogabook_data *data)
{
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
	struct yogabook_data *data = _data;

	if (gpiod_get_value(data->backside_hall_gpio))
		set_bit(YB_TABLET_MODE, &data->flags);
	else
		clear_bit(YB_TABLET_MODE, &data->flags);

	schedule_work(&data->work);

	return IRQ_HANDLED;
}

#define kbd_led_to_yogabook(cdev) container_of(cdev, struct yogabook_data, kbd_bl_led)

static enum led_brightness kbd_brightness_get(struct led_classdev *cdev)
{
	struct yogabook_data *data = kbd_led_to_yogabook(cdev);

	return data->brightness;
}

static int kbd_brightness_set(struct led_classdev *cdev,
			      enum led_brightness value)
{
	struct yogabook_data *data = kbd_led_to_yogabook(cdev);

	if ((value < 0) || (value > YB_KBD_BL_MAX))
		return -EINVAL;

	data->brightness = value;

	if (!test_bit(YB_KBD_IS_ON, &data->flags))
		return 0;

	return data->set_kbd_backlight(data, data->brightness);
}

static struct gpiod_lookup_table yogabook_gpios = {
	.table = {
		GPIO_LOOKUP("INT33FF:02", 18, "backside_hall_sw", GPIO_ACTIVE_LOW),
		{}
	},
};

static struct led_lookup_data yogabook_pen_led = {
	.provider = "platform::indicator",
	.con_id = "pen-icon-led",
};

static int yogabook_probe(struct device *dev, struct yogabook_data *data,
			  const char *kbd_bl_led_name)
{
	int r;

	data->dev = dev;
	data->brightness = YB_KBD_BL_DEFAULT;
	set_bit(YB_KBD_IS_ON, &data->flags);
	set_bit(YB_DIGITIZER_IS_ON, &data->flags);
	INIT_WORK(&data->work, yogabook_work);

	yogabook_pen_led.dev_id = dev_name(dev);
	led_add_lookup(&yogabook_pen_led);
	data->pen_led = devm_led_get(dev, "pen-icon-led");
	led_remove_lookup(&yogabook_pen_led);

	if (IS_ERR(data->pen_led))
		return dev_err_probe(dev, PTR_ERR(data->pen_led), "Getting pen icon LED\n");

	yogabook_gpios.dev_id = dev_name(dev);
	gpiod_add_lookup_table(&yogabook_gpios);
	data->backside_hall_gpio = devm_gpiod_get(dev, "backside_hall_sw", GPIOD_IN);
	gpiod_remove_lookup_table(&yogabook_gpios);

	if (IS_ERR(data->backside_hall_gpio))
		return dev_err_probe(dev, PTR_ERR(data->backside_hall_gpio),
				     "Getting backside_hall_sw GPIO\n");

	r = gpiod_to_irq(data->backside_hall_gpio);
	if (r < 0)
		return dev_err_probe(dev, r, "Getting backside_hall_sw IRQ\n");

	data->backside_hall_irq = r;

	/* Set default brightness before enabling the IRQ */
	data->set_kbd_backlight(data, YB_KBD_BL_DEFAULT);

	r = request_irq(data->backside_hall_irq, yogabook_backside_hall_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"backside_hall_sw", data);
	if (r)
		return dev_err_probe(dev, r, "Requesting backside_hall_sw IRQ\n");

	schedule_work(&data->work);

	data->kbd_bl_led.name = kbd_bl_led_name;
	data->kbd_bl_led.brightness_set_blocking = kbd_brightness_set;
	data->kbd_bl_led.brightness_get = kbd_brightness_get;
	data->kbd_bl_led.max_brightness = YB_KBD_BL_MAX;

	r = devm_led_classdev_register(dev, &data->kbd_bl_led);
	if (r < 0) {
		dev_err_probe(dev, r, "Registering backlight LED device\n");
		goto error_free_irq;
	}

	dev_set_drvdata(dev, data);
	return 0;

error_free_irq:
	free_irq(data->backside_hall_irq, data);
	cancel_work_sync(&data->work);
	return r;
}

static void yogabook_remove(struct yogabook_data *data)
{
	int r = 0;

	free_irq(data->backside_hall_irq, data);
	cancel_work_sync(&data->work);

	if (!test_bit(YB_KBD_IS_ON, &data->flags))
		r |= device_reprobe(data->kbd_dev);

	if (!test_bit(YB_DIGITIZER_IS_ON, &data->flags))
		r |= device_reprobe(data->dig_dev);

	if (r)
		dev_warn(data->dev, "Reprobe of devices failed\n");
}

static int yogabook_suspend(struct device *dev)
{
	struct yogabook_data *data = dev_get_drvdata(dev);

	set_bit(YB_SUSPENDED, &data->flags);
	flush_work(&data->work);

	if (test_bit(YB_KBD_IS_ON, &data->flags))
		data->set_kbd_backlight(data, 0);

	return 0;
}

static int yogabook_resume(struct device *dev)
{
	struct yogabook_data *data = dev_get_drvdata(dev);

	if (test_bit(YB_KBD_IS_ON, &data->flags))
		data->set_kbd_backlight(data, data->brightness);

	clear_bit(YB_SUSPENDED, &data->flags);

	/* Check for YB_TABLET_MODE changes made during suspend */
	schedule_work(&data->work);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(yogabook_pm_ops, yogabook_suspend, yogabook_resume);

/********** WMI driver code **********/

/*
 * To control keyboard backlight, call the method KBLC() of the TCS1 ACPI
 * device (Goodix touchpad acts as virtual sensor keyboard).
 */
static int yogabook_wmi_set_kbd_backlight(struct yogabook_data *data,
					  uint8_t level)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input;
	union acpi_object param;
	acpi_status status;

	dev_dbg(data->dev, "Set KBLC level to %u\n", level);

	/* Ensure keyboard touchpad is on before we call KBLC() */
	acpi_device_set_power(data->kbd_adev, ACPI_STATE_D0);

	input.count = 1;
	input.pointer = &param;

	param.type = ACPI_TYPE_INTEGER;
	param.integer.value = YB_KBD_BL_MAX - level;

	status = acpi_evaluate_object(acpi_device_handle(data->kbd_adev), "KBLC",
				      &input, &output);
	if (ACPI_FAILURE(status)) {
		dev_err(data->dev, "Failed to call KBLC method: 0x%x\n", status);
		return status;
	}

	kfree(output.pointer);
	return 0;
}

static int yogabook_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct device *dev = &wdev->dev;
	struct yogabook_data *data;
	int r;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->kbd_adev = acpi_dev_get_first_match_dev("GDIX1001", NULL, -1);
	if (!data->kbd_adev)
		return dev_err_probe(dev, -ENODEV, "Cannot find the touchpad device in ACPI tables\n");

	data->dig_adev = acpi_dev_get_first_match_dev("WCOM0019", NULL, -1);
	if (!data->dig_adev) {
		r = dev_err_probe(dev, -ENODEV, "Cannot find the digitizer device in ACPI tables\n");
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

	data->set_kbd_backlight = yogabook_wmi_set_kbd_backlight;

	r = yogabook_probe(dev, data, "ybwmi::kbd_backlight");
	if (r)
		goto error_put_devs;

	return 0;

error_put_devs:
	put_device(data->dig_dev);
	put_device(data->kbd_dev);
	acpi_dev_put(data->dig_adev);
	acpi_dev_put(data->kbd_adev);
	return r;
}

static void yogabook_wmi_remove(struct wmi_device *wdev)
{
	struct yogabook_data *data = dev_get_drvdata(&wdev->dev);

	yogabook_remove(data);

	put_device(data->dig_dev);
	put_device(data->kbd_dev);
	acpi_dev_put(data->dig_adev);
	acpi_dev_put(data->kbd_adev);
}

static void yogabook_wmi_notify(struct wmi_device *wdev, union acpi_object *dummy)
{
	yogabook_toggle_digitizer_mode(dev_get_drvdata(&wdev->dev));
}

static const struct wmi_device_id yogabook_wmi_id_table[] = {
	{
		.guid_string = YB_MBTN_EVENT_GUID,
	},
	{ } /* Terminating entry */
};
MODULE_DEVICE_TABLE(wmi, yogabook_wmi_id_table);

static struct wmi_driver yogabook_wmi_driver = {
	.driver = {
		.name = "yogabook-wmi",
		.pm = pm_sleep_ptr(&yogabook_pm_ops),
	},
	.no_notify_data = true,
	.id_table = yogabook_wmi_id_table,
	.probe = yogabook_wmi_probe,
	.remove = yogabook_wmi_remove,
	.notify = yogabook_wmi_notify,
};

/********** platform driver code **********/

static struct gpiod_lookup_table yogabook_pdev_gpios = {
	.dev_id = YB_PDEV_NAME,
	.table = {
		GPIO_LOOKUP("INT33FF:00", 95, "pen_touch_event", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("INT33FF:03", 52, "enable_keyboard_led", GPIO_ACTIVE_HIGH),
		{}
	},
};

static int yogabook_pdev_set_kbd_backlight(struct yogabook_data *data, u8 level)
{
	struct pwm_state state = {
		.period = YB_KBD_BL_PWM_PERIOD,
		.duty_cycle = YB_KBD_BL_PWM_PERIOD * level / YB_KBD_BL_MAX,
		.enabled = level,
	};

	pwm_apply_might_sleep(data->kbd_bl_pwm, &state);
	gpiod_set_value(data->kbd_bl_led_enable, level ? 1 : 0);
	return 0;
}

static irqreturn_t yogabook_pen_touch_irq(int irq, void *data)
{
	yogabook_toggle_digitizer_mode(data);
	return IRQ_HANDLED;
}

static int yogabook_pdev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct yogabook_data *data;
	int r;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	data->kbd_dev = bus_find_device_by_name(&i2c_bus_type, NULL, "i2c-goodix_ts");
	if (!data->kbd_dev || !data->kbd_dev->driver) {
		r = -EPROBE_DEFER;
		goto error_put_devs;
	}

	data->dig_dev = bus_find_device_by_name(&i2c_bus_type, NULL, "i2c-wacom");
	if (!data->dig_dev || !data->dig_dev->driver) {
		r = -EPROBE_DEFER;
		goto error_put_devs;
	}

	gpiod_add_lookup_table(&yogabook_pdev_gpios);
	data->pen_touch_event = devm_gpiod_get(dev, "pen_touch_event", GPIOD_IN);
	data->kbd_bl_led_enable = devm_gpiod_get(dev, "enable_keyboard_led", GPIOD_OUT_HIGH);
	gpiod_remove_lookup_table(&yogabook_pdev_gpios);

	if (IS_ERR(data->pen_touch_event)) {
		r = dev_err_probe(dev, PTR_ERR(data->pen_touch_event),
				  "Getting pen_touch_event GPIO\n");
		goto error_put_devs;
	}

	if (IS_ERR(data->kbd_bl_led_enable)) {
		r = dev_err_probe(dev, PTR_ERR(data->kbd_bl_led_enable),
				  "Getting enable_keyboard_led GPIO\n");
		goto error_put_devs;
	}

	data->kbd_bl_pwm = devm_pwm_get(dev, "pwm_soc_lpss_2");
	if (IS_ERR(data->kbd_bl_pwm)) {
		r = dev_err_probe(dev, PTR_ERR(data->kbd_bl_pwm),
				  "Getting keyboard backlight PWM\n");
		goto error_put_devs;
	}

	r = gpiod_to_irq(data->pen_touch_event);
	if (r < 0) {
		dev_err_probe(dev, r, "Getting pen_touch_event IRQ\n");
		goto error_put_devs;
	}
	data->pen_touch_irq = r;

	r = request_irq(data->pen_touch_irq, yogabook_pen_touch_irq, IRQF_TRIGGER_FALLING,
			"pen_touch_event", data);
	if (r) {
		dev_err_probe(dev, r, "Requesting pen_touch_event IRQ\n");
		goto error_put_devs;
	}

	data->set_kbd_backlight = yogabook_pdev_set_kbd_backlight;

	r = yogabook_probe(dev, data, "yogabook::kbd_backlight");
	if (r)
		goto error_free_irq;

	return 0;

error_free_irq:
	free_irq(data->pen_touch_irq, data);
	cancel_work_sync(&data->work);
error_put_devs:
	put_device(data->dig_dev);
	put_device(data->kbd_dev);
	return r;
}

static void yogabook_pdev_remove(struct platform_device *pdev)
{
	struct yogabook_data *data = platform_get_drvdata(pdev);

	yogabook_remove(data);
	free_irq(data->pen_touch_irq, data);
	cancel_work_sync(&data->work);
	put_device(data->dig_dev);
	put_device(data->kbd_dev);
}

static struct platform_driver yogabook_pdev_driver = {
	.probe = yogabook_pdev_probe,
	.remove = yogabook_pdev_remove,
	.driver = {
		.name = YB_PDEV_NAME,
		.pm = pm_sleep_ptr(&yogabook_pm_ops),
	},
};

static int __init yogabook_module_init(void)
{
	int r;

	r = wmi_driver_register(&yogabook_wmi_driver);
	if (r)
		return r;

	r = platform_driver_register(&yogabook_pdev_driver);
	if (r)
		wmi_driver_unregister(&yogabook_wmi_driver);

	return r;
}

static void __exit yogabook_module_exit(void)
{
	platform_driver_unregister(&yogabook_pdev_driver);
	wmi_driver_unregister(&yogabook_wmi_driver);
}

module_init(yogabook_module_init);
module_exit(yogabook_module_exit);

MODULE_ALIAS("platform:" YB_PDEV_NAME);
MODULE_AUTHOR("Yauhen Kharuzhy");
MODULE_DESCRIPTION("Lenovo Yoga Book driver");
MODULE_LICENSE("GPL v2");
