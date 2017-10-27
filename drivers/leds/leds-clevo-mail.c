#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>

#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/leds.h>

#include <linux/io.h>
#include <linux/dmi.h>

#include <linux/i8042.h>

#define CLEVO_MAIL_LED_OFF		0x0084
#define CLEVO_MAIL_LED_BLINK_1HZ	0x008A
#define CLEVO_MAIL_LED_BLINK_0_5HZ	0x0083

MODULE_AUTHOR("Márton Németh <nm127@freemail.hu>");
MODULE_DESCRIPTION("Clevo mail LED driver");
MODULE_LICENSE("GPL");

static bool nodetect;
module_param_named(nodetect, nodetect, bool, 0);
MODULE_PARM_DESC(nodetect, "Skip DMI hardware detection");

static struct platform_device *pdev;

static int __init clevo_mail_led_dmi_callback(const struct dmi_system_id *id)
{
	pr_info("'%s' found\n", id->ident);
	return 1;
}

/*
 * struct clevo_mail_led_dmi_table - List of known good models
 *
 * Contains the known good models this driver is compatible with.
 * When adding a new model try to be as strict as possible. This
 * makes it possible to keep the false positives (the model is
 * detected as working, but in reality it is not) as low as
 * possible.
 */
static const struct dmi_system_id clevo_mail_led_dmi_table[] __initconst = {
	{
		.callback = clevo_mail_led_dmi_callback,
		.ident = "Clevo D410J",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "VIA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "K8N800"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "VT8204B")
		}
	},
	{
		.callback = clevo_mail_led_dmi_callback,
		.ident = "Clevo M5x0N",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "CLEVO Co."),
			DMI_MATCH(DMI_PRODUCT_NAME, "M5x0N")
		}
	},
	{
		.callback = clevo_mail_led_dmi_callback,
		.ident = "Clevo M5x0V",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "CLEVO Co. "),
			DMI_MATCH(DMI_BOARD_NAME, "M5X0V "),
			DMI_MATCH(DMI_PRODUCT_VERSION, "VT6198")
		}
	},
	{
		.callback = clevo_mail_led_dmi_callback,
		.ident = "Clevo D400P",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Clevo"),
			DMI_MATCH(DMI_BOARD_NAME, "D400P"),
			DMI_MATCH(DMI_BOARD_VERSION, "Rev.A"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "0106")
		}
	},
	{
		.callback = clevo_mail_led_dmi_callback,
		.ident = "Clevo D410V",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Clevo, Co."),
			DMI_MATCH(DMI_BOARD_NAME, "D400V/D470V"),
			DMI_MATCH(DMI_BOARD_VERSION, "SS78B"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Rev. A1")
		}
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, clevo_mail_led_dmi_table);

static void clevo_mail_led_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	i8042_lock_chip();

	if (value == LED_OFF)
		i8042_command(NULL, CLEVO_MAIL_LED_OFF);
	else if (value <= LED_HALF)
		i8042_command(NULL, CLEVO_MAIL_LED_BLINK_0_5HZ);
	else
		i8042_command(NULL, CLEVO_MAIL_LED_BLINK_1HZ);

	i8042_unlock_chip();

}

static int clevo_mail_led_blink(struct led_classdev *led_cdev,
				unsigned long *delay_on,
				unsigned long *delay_off)
{
	int status = -EINVAL;

	i8042_lock_chip();

	if (*delay_on == 0 /* ms */ && *delay_off == 0 /* ms */) {
		/* Special case: the leds subsystem requested us to
		 * chose one user friendly blinking of the LED, and
		 * start it. Let's blink the led slowly (0.5Hz).
		 */
		*delay_on = 1000; /* ms */
		*delay_off = 1000; /* ms */
		i8042_command(NULL, CLEVO_MAIL_LED_BLINK_0_5HZ);
		status = 0;

	} else if (*delay_on == 500 /* ms */ && *delay_off == 500 /* ms */) {
		/* blink the led with 1Hz */
		i8042_command(NULL, CLEVO_MAIL_LED_BLINK_1HZ);
		status = 0;

	} else if (*delay_on == 1000 /* ms */ && *delay_off == 1000 /* ms */) {
		/* blink the led with 0.5Hz */
		i8042_command(NULL, CLEVO_MAIL_LED_BLINK_0_5HZ);
		status = 0;

	} else {
		pr_debug("clevo_mail_led_blink(..., %lu, %lu),"
		       " returning -EINVAL (unsupported)\n",
		       *delay_on, *delay_off);
	}

	i8042_unlock_chip();

	return status;
}

static struct led_classdev clevo_mail_led = {
	.name			= "clevo::mail",
	.brightness_set		= clevo_mail_led_set,
	.blink_set		= clevo_mail_led_blink,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static int __init clevo_mail_led_probe(struct platform_device *pdev)
{
	return led_classdev_register(&pdev->dev, &clevo_mail_led);
}

static int clevo_mail_led_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&clevo_mail_led);
	return 0;
}

static struct platform_driver clevo_mail_led_driver = {
	.remove		= clevo_mail_led_remove,
	.driver		= {
		.name		= KBUILD_MODNAME,
	},
};

static int __init clevo_mail_led_init(void)
{
	int error = 0;
	int count = 0;

	/* Check with the help of DMI if we are running on supported hardware */
	if (!nodetect) {
		count = dmi_check_system(clevo_mail_led_dmi_table);
	} else {
		count = 1;
		pr_err("Skipping DMI detection. "
		       "If the driver works on your hardware please "
		       "report model and the output of dmidecode in tracker "
		       "at http://sourceforge.net/projects/clevo-mailled/\n");
	}

	if (!count)
		return -ENODEV;

	pdev = platform_device_register_simple(KBUILD_MODNAME, -1, NULL, 0);
	if (!IS_ERR(pdev)) {
		error = platform_driver_probe(&clevo_mail_led_driver,
					      clevo_mail_led_probe);
		if (error) {
			pr_err("Can't probe platform driver\n");
			platform_device_unregister(pdev);
		}
	} else
		error = PTR_ERR(pdev);

	return error;
}

static void __exit clevo_mail_led_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&clevo_mail_led_driver);

	clevo_mail_led_set(NULL, LED_OFF);
}

module_init(clevo_mail_led_init);
module_exit(clevo_mail_led_exit);
