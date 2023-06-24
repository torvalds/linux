// SPDX-License-Identifier: GPL-2.0
//
// Driver for the Winmate FM07 front-panel keys
//
// Author: Daniel Beer <daniel.beer@tirotech.co.nz>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <linux/io.h>

#define DRV_NAME	"winmate-fm07keys"

#define PORT_CMD	0x6c
#define PORT_DATA	0x68

#define EC_ADDR_KEYS	0x3b
#define EC_CMD_READ	0x80

#define BASE_KEY	KEY_F13
#define NUM_KEYS	5

/* Typically we're done in fewer than 10 iterations */
#define LOOP_TIMEOUT	1000

static void fm07keys_poll(struct input_dev *input)
{
	uint8_t k;
	int i;

	/* Flush output buffer */
	i = 0;
	while (inb(PORT_CMD) & 0x01) {
		if (++i >= LOOP_TIMEOUT)
			goto timeout;
		inb(PORT_DATA);
	}

	/* Send request and wait for write completion */
	outb(EC_CMD_READ, PORT_CMD);
	i = 0;
	while (inb(PORT_CMD) & 0x02)
		if (++i >= LOOP_TIMEOUT)
			goto timeout;

	outb(EC_ADDR_KEYS, PORT_DATA);
	i = 0;
	while (inb(PORT_CMD) & 0x02)
		if (++i >= LOOP_TIMEOUT)
			goto timeout;

	/* Wait for data ready */
	i = 0;
	while (!(inb(PORT_CMD) & 0x01))
		if (++i >= LOOP_TIMEOUT)
			goto timeout;
	k = inb(PORT_DATA);

	/* Notify of new key states */
	for (i = 0; i < NUM_KEYS; i++) {
		input_report_key(input, BASE_KEY + i, (~k) & 1);
		k >>= 1;
	}

	input_sync(input);
	return;

timeout:
	dev_warn_ratelimited(&input->dev, "timeout polling IO memory\n");
}

static int fm07keys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct input_dev *input;
	int ret;
	int i;

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "no memory for input device\n");
		return -ENOMEM;
	}

	if (!devm_request_region(dev, PORT_CMD, 1, "Winmate FM07 EC"))
		return -EBUSY;
	if (!devm_request_region(dev, PORT_DATA, 1, "Winmate FM07 EC"))
		return -EBUSY;

	input->name = "Winmate FM07 front-panel keys";
	input->phys = DRV_NAME "/input0";

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	__set_bit(EV_KEY, input->evbit);

	for (i = 0; i < NUM_KEYS; i++)
		__set_bit(BASE_KEY + i, input->keybit);

	ret = input_setup_polling(input, fm07keys_poll);
	if (ret) {
		dev_err(dev, "unable to set up polling, err=%d\n", ret);
		return ret;
	}

	/* These are silicone buttons. They can't be pressed in rapid
	 * succession too quickly, and 50 Hz seems to be an adequate
	 * sampling rate without missing any events when tested.
	 */
	input_set_poll_interval(input, 20);

	ret = input_register_device(input);
	if (ret) {
		dev_err(dev, "unable to register polled device, err=%d\n",
			ret);
		return ret;
	}

	input_sync(input);
	return 0;
}

static struct platform_driver fm07keys_driver = {
	.probe		= fm07keys_probe,
	.driver		= {
		.name	= DRV_NAME
	},
};

static struct platform_device *dev;

static const struct dmi_system_id fm07keys_dmi_table[] __initconst = {
	{
		/* FM07 and FM07P */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Winmate Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "IP30"),
		},
	},
	{ }
};

MODULE_DEVICE_TABLE(dmi, fm07keys_dmi_table);

static int __init fm07keys_init(void)
{
	int ret;

	if (!dmi_check_system(fm07keys_dmi_table))
		return -ENODEV;

	ret = platform_driver_register(&fm07keys_driver);
	if (ret) {
		pr_err("fm07keys: failed to register driver, err=%d\n", ret);
		return ret;
	}

	dev = platform_device_register_simple(DRV_NAME, PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		pr_err("fm07keys: failed to allocate device, err = %d\n", ret);
		goto fail_register;
	}

	return 0;

fail_register:
	platform_driver_unregister(&fm07keys_driver);
	return ret;
}

static void __exit fm07keys_exit(void)
{
	platform_driver_unregister(&fm07keys_driver);
	platform_device_unregister(dev);
}

module_init(fm07keys_init);
module_exit(fm07keys_exit);

MODULE_AUTHOR("Daniel Beer <daniel.beer@tirotech.co.nz>");
MODULE_DESCRIPTION("Winmate FM07 front-panel keys driver");
MODULE_LICENSE("GPL");
