
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

static unsigned int __initdata nodetect;
module_param_named(nodetect, nodetect, bool, 0);
MODULE_PARM_DESC(nodetect, "Skip DMI hardware detection");

static struct platform_device *pdev;

static int __init clevo_mail_led_dmi_callback(const struct dmi_system_id *id)
{
	printk(KERN_INFO KBUILD_MODNAME ": '%s' found\n", id->ident);
	return 1;
}

/*
 * struct mail_led_whitelist - List of known good models
 *
 * Contains the known good models this driver is compatible with.
 * When adding a new model try to be as strict as possible. This
 * makes it possible to keep the false positives (the model is
 * detected as working, but in reality it is not) as low as
 * possible.
 */
static struct dmi_system_id __initdata mail_led_whitelist[] = {
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
		.ident = "Positivo Mobile",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "CLEVO Co. "),
			DMI_MATCH(DMI_BOARD_NAME, "M5X0V "),
			DMI_MATCH(DMI_PRODUCT_NAME, "Positivo Mobile"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "VT6198")
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

static void clevo_mail_led_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	if (value == LED_OFF)
		i8042_command(NULL, CLEVO_MAIL_LED_OFF);
	else if (value <= LED_HALF)
		i8042_command(NULL, CLEVO_MAIL_LED_BLINK_0_5HZ);
	else
		i8042_command(NULL, CLEVO_MAIL_LED_BLINK_1HZ);

}

static struct led_classdev clevo_mail_led = {
	.name			= "clevo",
	.brightness_set		= clevo_mail_led_set,
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

#ifdef CONFIG_PM
static int clevo_mail_led_suspend(struct platform_device *dev,
				  pm_message_t state)
{
	led_classdev_suspend(&clevo_mail_led);
	return 0;
}

static int clevo_mail_led_resume(struct platform_device *dev)
{
	led_classdev_resume(&clevo_mail_led);
	return 0;
}
#else
#define clevo_mail_led_suspend    NULL
#define clevo_mail_led_resume     NULL
#endif

static struct platform_driver clevo_mail_led_driver = {
	.probe		= clevo_mail_led_probe,
	.remove		= clevo_mail_led_remove,
	.suspend	= clevo_mail_led_suspend,
	.resume		= clevo_mail_led_resume,
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
		count = dmi_check_system(mail_led_whitelist);
	} else {
		count = 1;
		printk(KERN_ERR KBUILD_MODNAME ": Skipping DMI detection. "
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
			printk(KERN_ERR KBUILD_MODNAME
			       ": Can't probe platform driver\n");
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
