/*
 * PowerNV LED Driver
 *
 * Copyright IBM Corp. 2015
 *
 * Author: Vasant Hegde <hegdevasant@linux.vnet.ibm.com>
 * Author: Anshuman Khandual <khandual@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/opal.h>

/* Map LED type to description. */
struct led_type_map {
	const int	type;
	const char	*desc;
};
static const struct led_type_map led_type_map[] = {
	{OPAL_SLOT_LED_TYPE_ID,		"identify"},
	{OPAL_SLOT_LED_TYPE_FAULT,	"fault"},
	{OPAL_SLOT_LED_TYPE_ATTN,	"attention"},
	{-1,				NULL},
};

struct powernv_led_common {
	/*
	 * By default unload path resets all the LEDs. But on PowerNV
	 * platform we want to retain LED state across reboot as these
	 * are controlled by firmware. Also service processor can modify
	 * the LEDs independent of OS. Hence avoid resetting LEDs in
	 * unload path.
	 */
	bool		led_disabled;

	/* Max supported LED type */
	__be64		max_led_type;

	/* glabal lock */
	struct mutex	lock;
};

/* PowerNV LED data */
struct powernv_led_data {
	struct led_classdev	cdev;
	char			*loc_code;	/* LED location code */
	int			led_type;	/* OPAL_SLOT_LED_TYPE_* */

	struct powernv_led_common *common;
};


/* Returns OPAL_SLOT_LED_TYPE_* for given led type string */
static int powernv_get_led_type(const char *led_type_desc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(led_type_map); i++)
		if (!strcmp(led_type_map[i].desc, led_type_desc))
			return led_type_map[i].type;

	return -1;
}

/*
 * This commits the state change of the requested LED through an OPAL call.
 * This function is called from work queue task context when ever it gets
 * scheduled. This function can sleep at opal_async_wait_response call.
 */
static void powernv_led_set(struct powernv_led_data *powernv_led,
			    enum led_brightness value)
{
	int rc, token;
	u64 led_mask, led_value = 0;
	__be64 max_type;
	struct opal_msg msg;
	struct device *dev = powernv_led->cdev.dev;
	struct powernv_led_common *powernv_led_common = powernv_led->common;

	/* Prepare for the OPAL call */
	max_type = powernv_led_common->max_led_type;
	led_mask = OPAL_SLOT_LED_STATE_ON << powernv_led->led_type;
	if (value)
		led_value = led_mask;

	/* OPAL async call */
	token = opal_async_get_token_interruptible();
	if (token < 0) {
		if (token != -ERESTARTSYS)
			dev_err(dev, "%s: Couldn't get OPAL async token\n",
				__func__);
		return;
	}

	rc = opal_leds_set_ind(token, powernv_led->loc_code,
			       led_mask, led_value, &max_type);
	if (rc != OPAL_ASYNC_COMPLETION) {
		dev_err(dev, "%s: OPAL set LED call failed for %s [rc=%d]\n",
			__func__, powernv_led->loc_code, rc);
		goto out_token;
	}

	rc = opal_async_wait_response(token, &msg);
	if (rc) {
		dev_err(dev,
			"%s: Failed to wait for the async response [rc=%d]\n",
			__func__, rc);
		goto out_token;
	}

	rc = be64_to_cpu(msg.params[1]);
	if (rc != OPAL_SUCCESS)
		dev_err(dev, "%s : OAPL async call returned failed [rc=%d]\n",
			__func__, rc);

out_token:
	opal_async_release_token(token);
}

/*
 * This function fetches the LED state for a given LED type for
 * mentioned LED classdev structure.
 */
static enum led_brightness powernv_led_get(struct powernv_led_data *powernv_led)
{
	int rc;
	__be64 mask, value, max_type;
	u64 led_mask, led_value;
	struct device *dev = powernv_led->cdev.dev;
	struct powernv_led_common *powernv_led_common = powernv_led->common;

	/* Fetch all LED status */
	mask = cpu_to_be64(0);
	value = cpu_to_be64(0);
	max_type = powernv_led_common->max_led_type;

	rc = opal_leds_get_ind(powernv_led->loc_code,
			       &mask, &value, &max_type);
	if (rc != OPAL_SUCCESS && rc != OPAL_PARTIAL) {
		dev_err(dev, "%s: OPAL get led call failed [rc=%d]\n",
			__func__, rc);
		return LED_OFF;
	}

	led_mask = be64_to_cpu(mask);
	led_value = be64_to_cpu(value);

	/* LED status available */
	if (!((led_mask >> powernv_led->led_type) & OPAL_SLOT_LED_STATE_ON)) {
		dev_err(dev, "%s: LED status not available for %s\n",
			__func__, powernv_led->cdev.name);
		return LED_OFF;
	}

	/* LED status value */
	if ((led_value >> powernv_led->led_type) & OPAL_SLOT_LED_STATE_ON)
		return LED_FULL;

	return LED_OFF;
}

/*
 * LED classdev 'brightness_get' function. This schedules work
 * to update LED state.
 */
static void powernv_brightness_set(struct led_classdev *led_cdev,
				   enum led_brightness value)
{
	struct powernv_led_data *powernv_led =
		container_of(led_cdev, struct powernv_led_data, cdev);
	struct powernv_led_common *powernv_led_common = powernv_led->common;

	/* Do not modify LED in unload path */
	if (powernv_led_common->led_disabled)
		return;

	mutex_lock(&powernv_led_common->lock);
	powernv_led_set(powernv_led, value);
	mutex_unlock(&powernv_led_common->lock);
}

/* LED classdev 'brightness_get' function */
static enum led_brightness powernv_brightness_get(struct led_classdev *led_cdev)
{
	struct powernv_led_data *powernv_led =
		container_of(led_cdev, struct powernv_led_data, cdev);

	return powernv_led_get(powernv_led);
}

/*
 * This function registers classdev structure for any given type of LED on
 * a given child LED device node.
 */
static int powernv_led_create(struct device *dev,
			      struct powernv_led_data *powernv_led,
			      const char *led_type_desc)
{
	int rc;

	/* Make sure LED type is supported */
	powernv_led->led_type = powernv_get_led_type(led_type_desc);
	if (powernv_led->led_type == -1) {
		dev_warn(dev, "%s: No support for led type : %s\n",
			 __func__, led_type_desc);
		return -EINVAL;
	}

	/* Create the name for classdev */
	powernv_led->cdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s:%s",
						powernv_led->loc_code,
						led_type_desc);
	if (!powernv_led->cdev.name) {
		dev_err(dev,
			"%s: Memory allocation failed for classdev name\n",
			__func__);
		return -ENOMEM;
	}

	powernv_led->cdev.brightness_set = powernv_brightness_set;
	powernv_led->cdev.brightness_get = powernv_brightness_get;
	powernv_led->cdev.brightness = LED_OFF;
	powernv_led->cdev.max_brightness = LED_FULL;

	/* Register the classdev */
	rc = devm_led_classdev_register(dev, &powernv_led->cdev);
	if (rc) {
		dev_err(dev, "%s: Classdev registration failed for %s\n",
			__func__, powernv_led->cdev.name);
	}

	return rc;
}

/* Go through LED device tree node and register LED classdev structure */
static int powernv_led_classdev(struct platform_device *pdev,
				struct device_node *led_node,
				struct powernv_led_common *powernv_led_common)
{
	const char *cur = NULL;
	int rc = -1;
	struct property *p;
	struct device_node *np;
	struct powernv_led_data *powernv_led;
	struct device *dev = &pdev->dev;

	for_each_child_of_node(led_node, np) {
		p = of_find_property(np, "led-types", NULL);
		if (!p)
			continue;

		while ((cur = of_prop_next_string(p, cur)) != NULL) {
			powernv_led = devm_kzalloc(dev, sizeof(*powernv_led),
						   GFP_KERNEL);
			if (!powernv_led)
				return -ENOMEM;

			powernv_led->common = powernv_led_common;
			powernv_led->loc_code = (char *)np->name;

			rc = powernv_led_create(dev, powernv_led, cur);
			if (rc)
				return rc;
		} /* while end */
	}

	return rc;
}

/* Platform driver probe */
static int powernv_led_probe(struct platform_device *pdev)
{
	struct device_node *led_node;
	struct powernv_led_common *powernv_led_common;
	struct device *dev = &pdev->dev;

	led_node = of_find_node_by_path("/ibm,opal/leds");
	if (!led_node) {
		dev_err(dev, "%s: LED parent device node not found\n",
			__func__);
		return -EINVAL;
	}

	powernv_led_common = devm_kzalloc(dev, sizeof(*powernv_led_common),
					  GFP_KERNEL);
	if (!powernv_led_common)
		return -ENOMEM;

	mutex_init(&powernv_led_common->lock);
	powernv_led_common->max_led_type = cpu_to_be64(OPAL_SLOT_LED_TYPE_MAX);

	platform_set_drvdata(pdev, powernv_led_common);

	return powernv_led_classdev(pdev, led_node, powernv_led_common);
}

/* Platform driver remove */
static int powernv_led_remove(struct platform_device *pdev)
{
	struct powernv_led_common *powernv_led_common;

	/* Disable LED operation */
	powernv_led_common = platform_get_drvdata(pdev);
	powernv_led_common->led_disabled = true;

	/* Destroy lock */
	mutex_destroy(&powernv_led_common->lock);

	dev_info(&pdev->dev, "PowerNV led module unregistered\n");
	return 0;
}

/* Platform driver property match */
static const struct of_device_id powernv_led_match[] = {
	{
		.compatible	= "ibm,opal-v3-led",
	},
	{},
};
MODULE_DEVICE_TABLE(of, powernv_led_match);

static struct platform_driver powernv_led_driver = {
	.probe	= powernv_led_probe,
	.remove = powernv_led_remove,
	.driver = {
		.name = "powernv-led-driver",
		.of_match_table = powernv_led_match,
	},
};

module_platform_driver(powernv_led_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PowerNV LED driver");
MODULE_AUTHOR("Vasant Hegde <hegdevasant@linux.vnet.ibm.com>");
