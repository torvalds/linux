// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/string_choices.h>
#include <linux/uuid.h>

#include "common.h"

/*
 * 79234640-9e10-4fea-a5c1-b5aa8b19756f
 * This _DSM GUID returns information about the GPIO lines mapped to a
 * discrete INT3472 device. Function number 1 returns a count of the GPIO
 * lines that are mapped. Subsequent functions return 32 bit ints encoding
 * information about the GPIO line, including its purpose.
 */
static const guid_t int3472_gpio_guid =
	GUID_INIT(0x79234640, 0x9e10, 0x4fea,
		  0xa5, 0xc1, 0xb5, 0xaa, 0x8b, 0x19, 0x75, 0x6f);

#define INT3472_GPIO_DSM_TYPE				GENMASK(7, 0)
#define INT3472_GPIO_DSM_PIN				GENMASK(15, 8)
#define INT3472_GPIO_DSM_SENSOR_ON_VAL			GENMASK(31, 24)

/*
 * 822ace8f-2814-4174-a56b-5f029fe079ee
 * This _DSM GUID returns a string from the sensor device, which acts as a
 * module identifier.
 */
static const guid_t cio2_sensor_module_guid =
	GUID_INIT(0x822ace8f, 0x2814, 0x4174,
		  0xa5, 0x6b, 0x5f, 0x02, 0x9f, 0xe0, 0x79, 0xee);

static void skl_int3472_log_sensor_module_name(struct int3472_discrete_device *int3472)
{
	union acpi_object *obj;

	obj = acpi_evaluate_dsm_typed(int3472->sensor->handle,
				      &cio2_sensor_module_guid, 0x00,
				      0x01, NULL, ACPI_TYPE_STRING);
	if (obj) {
		dev_dbg(int3472->dev, "Sensor module id: '%s'\n", obj->string.pointer);
		ACPI_FREE(obj);
	}
}

static int skl_int3472_fill_gpiod_lookup(struct gpiod_lookup *table_entry,
					 struct acpi_resource_gpio *agpio,
					 const char *func, u32 polarity)
{
	char *path = agpio->resource_source.string_ptr;
	struct acpi_device *adev;
	acpi_handle handle;
	acpi_status status;

	status = acpi_get_handle(NULL, path, &handle);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	adev = acpi_fetch_acpi_dev(handle);
	if (!adev)
		return -ENODEV;

	*table_entry = GPIO_LOOKUP(acpi_dev_name(adev), agpio->pin_table[0], func, polarity);

	return 0;
}

static int skl_int3472_map_gpio_to_sensor(struct int3472_discrete_device *int3472,
					  struct acpi_resource_gpio *agpio,
					  const char *func, u32 polarity)
{
	int ret;

	if (int3472->n_sensor_gpios >= INT3472_MAX_SENSOR_GPIOS) {
		dev_warn(int3472->dev, "Too many GPIOs mapped\n");
		return -EINVAL;
	}

	ret = skl_int3472_fill_gpiod_lookup(&int3472->gpios.table[int3472->n_sensor_gpios],
					    agpio, func, polarity);
	if (ret)
		return ret;

	int3472->n_sensor_gpios++;

	return 0;
}

/* This should *really* only be used when there's no other way... */
static struct gpio_desc *
skl_int3472_gpiod_get_from_temp_lookup(struct int3472_discrete_device *int3472,
				       struct acpi_resource_gpio *agpio,
				       const char *func, u32 polarity)
{
	struct gpio_desc *desc;
	int ret;

	struct gpiod_lookup_table *lookup __free(kfree) =
			kzalloc(struct_size(lookup, table, 2), GFP_KERNEL);
	if (!lookup)
		return ERR_PTR(-ENOMEM);

	lookup->dev_id = dev_name(int3472->dev);
	ret = skl_int3472_fill_gpiod_lookup(&lookup->table[0], agpio, func, polarity);
	if (ret)
		return ERR_PTR(ret);

	gpiod_add_lookup_table(lookup);
	desc = devm_gpiod_get(int3472->dev, func, GPIOD_OUT_LOW);
	gpiod_remove_lookup_table(lookup);

	return desc;
}

static void int3472_get_func_and_polarity(u8 type, const char **func, u32 *polarity)
{
	switch (type) {
	case INT3472_GPIO_TYPE_RESET:
		*func = "reset";
		*polarity = GPIO_ACTIVE_LOW;
		break;
	case INT3472_GPIO_TYPE_POWERDOWN:
		*func = "powerdown";
		*polarity = GPIO_ACTIVE_LOW;
		break;
	case INT3472_GPIO_TYPE_CLK_ENABLE:
		*func = "clk-enable";
		*polarity = GPIO_ACTIVE_HIGH;
		break;
	case INT3472_GPIO_TYPE_PRIVACY_LED:
		*func = "privacy-led";
		*polarity = GPIO_ACTIVE_HIGH;
		break;
	case INT3472_GPIO_TYPE_POWER_ENABLE:
		*func = "power-enable";
		*polarity = GPIO_ACTIVE_HIGH;
		break;
	default:
		*func = "unknown";
		*polarity = GPIO_ACTIVE_HIGH;
		break;
	}
}

/**
 * skl_int3472_handle_gpio_resources: Map PMIC resources to consuming sensor
 * @ares: A pointer to a &struct acpi_resource
 * @data: A pointer to a &struct int3472_discrete_device
 *
 * This function handles GPIO resources that are against an INT3472
 * ACPI device, by checking the value of the corresponding _DSM entry.
 * This will return a 32bit int, where the lowest byte represents the
 * function of the GPIO pin:
 *
 * 0x00 Reset
 * 0x01 Power down
 * 0x0b Power enable
 * 0x0c Clock enable
 * 0x0d Privacy LED
 *
 * There are some known platform specific quirks where that does not quite
 * hold up; for example where a pin with type 0x01 (Power down) is mapped to
 * a sensor pin that performs a reset function or entries in _CRS and _DSM that
 * do not actually correspond to a physical connection. These will be handled
 * by the mapping sub-functions.
 *
 * GPIOs will either be mapped directly to the sensor device or else used
 * to create clocks and regulators via the usual frameworks.
 *
 * Return:
 * * 1		- To continue the loop
 * * 0		- When all resources found are handled properly.
 * * -EINVAL	- If the resource is not a GPIO IO resource
 * * -ENODEV	- If the resource has no corresponding _DSM entry
 * * -Other	- Errors propagated from one of the sub-functions.
 */
static int skl_int3472_handle_gpio_resources(struct acpi_resource *ares,
					     void *data)
{
	struct int3472_discrete_device *int3472 = data;
	struct acpi_resource_gpio *agpio;
	u8 active_value, pin, type;
	union acpi_object *obj;
	struct gpio_desc *gpio;
	const char *err_msg;
	const char *func;
	u32 polarity;
	int ret;

	if (!acpi_gpio_get_io_resource(ares, &agpio))
		return 1;

	/*
	 * ngpios + 2 because the index of this _DSM function is 1-based and
	 * the first function is just a count.
	 */
	obj = acpi_evaluate_dsm_typed(int3472->adev->handle,
				      &int3472_gpio_guid, 0x00,
				      int3472->ngpios + 2,
				      NULL, ACPI_TYPE_INTEGER);

	if (!obj) {
		dev_warn(int3472->dev, "No _DSM entry for GPIO pin %u\n",
			 agpio->pin_table[0]);
		return 1;
	}

	type = FIELD_GET(INT3472_GPIO_DSM_TYPE, obj->integer.value);

	int3472_get_func_and_polarity(type, &func, &polarity);

	pin = FIELD_GET(INT3472_GPIO_DSM_PIN, obj->integer.value);
	if (pin != agpio->pin_table[0])
		dev_warn(int3472->dev, "%s %s pin number mismatch _DSM %d resource %d\n",
			 func, agpio->resource_source.string_ptr, pin,
			 agpio->pin_table[0]);

	active_value = FIELD_GET(INT3472_GPIO_DSM_SENSOR_ON_VAL, obj->integer.value);
	if (!active_value)
		polarity ^= GPIO_ACTIVE_LOW;

	dev_dbg(int3472->dev, "%s %s pin %d active-%s\n", func,
		agpio->resource_source.string_ptr, agpio->pin_table[0],
		str_high_low(polarity == GPIO_ACTIVE_HIGH));

	switch (type) {
	case INT3472_GPIO_TYPE_RESET:
	case INT3472_GPIO_TYPE_POWERDOWN:
		ret = skl_int3472_map_gpio_to_sensor(int3472, agpio, func, polarity);
		if (ret)
			err_msg = "Failed to map GPIO pin to sensor\n";

		break;
	case INT3472_GPIO_TYPE_CLK_ENABLE:
	case INT3472_GPIO_TYPE_PRIVACY_LED:
	case INT3472_GPIO_TYPE_POWER_ENABLE:
		gpio = skl_int3472_gpiod_get_from_temp_lookup(int3472, agpio, func, polarity);
		if (IS_ERR(gpio)) {
			ret = PTR_ERR(gpio);
			err_msg = "Failed to get GPIO\n";
			break;
		}

		switch (type) {
		case INT3472_GPIO_TYPE_CLK_ENABLE:
			ret = skl_int3472_register_gpio_clock(int3472, gpio);
			if (ret)
				err_msg = "Failed to register clock\n";

			break;
		case INT3472_GPIO_TYPE_PRIVACY_LED:
			ret = skl_int3472_register_pled(int3472, gpio);
			if (ret)
				err_msg = "Failed to register LED\n";

			break;
		case INT3472_GPIO_TYPE_POWER_ENABLE:
			ret = skl_int3472_register_regulator(int3472, gpio);
			if (ret)
				err_msg = "Failed to map regulator to sensor\n";

			break;
		default: /* Never reached */
			ret = -EINVAL;
			break;
		}
		break;
	default:
		dev_warn(int3472->dev,
			 "GPIO type 0x%02x unknown; the sensor may not work\n",
			 type);
		ret = 1;
		break;
	}

	int3472->ngpios++;
	ACPI_FREE(obj);

	if (ret < 0)
		return dev_err_probe(int3472->dev, ret, err_msg);

	return ret;
}

static int skl_int3472_parse_crs(struct int3472_discrete_device *int3472)
{
	LIST_HEAD(resource_list);
	int ret;

	skl_int3472_log_sensor_module_name(int3472);

	ret = acpi_dev_get_resources(int3472->adev, &resource_list,
				     skl_int3472_handle_gpio_resources,
				     int3472);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&resource_list);

	/* Register _DSM based clock (no-op if a GPIO clock was already registered) */
	ret = skl_int3472_register_dsm_clock(int3472);
	if (ret < 0)
		return ret;

	int3472->gpios.dev_id = int3472->sensor_name;
	gpiod_add_lookup_table(&int3472->gpios);

	return 0;
}

static void skl_int3472_discrete_remove(struct platform_device *pdev)
{
	struct int3472_discrete_device *int3472 = platform_get_drvdata(pdev);

	gpiod_remove_lookup_table(&int3472->gpios);

	skl_int3472_unregister_clock(int3472);
	skl_int3472_unregister_pled(int3472);
	skl_int3472_unregister_regulator(int3472);
}

static int skl_int3472_discrete_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct int3472_discrete_device *int3472;
	struct int3472_cldb cldb;
	int ret;

	ret = skl_int3472_fill_cldb(adev, &cldb);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't fill CLDB structure\n");
		return ret;
	}

	if (cldb.control_logic_type != 1) {
		dev_err(&pdev->dev, "Unsupported control logic type %u\n",
			cldb.control_logic_type);
		return -EINVAL;
	}

	/* Max num GPIOs we've seen plus a terminator */
	int3472 = devm_kzalloc(&pdev->dev, struct_size(int3472, gpios.table,
			       INT3472_MAX_SENSOR_GPIOS + 1), GFP_KERNEL);
	if (!int3472)
		return -ENOMEM;

	int3472->adev = adev;
	int3472->dev = &pdev->dev;
	platform_set_drvdata(pdev, int3472);
	int3472->clock.imgclk_index = cldb.clock_source;

	ret = skl_int3472_get_sensor_adev_and_name(&pdev->dev, &int3472->sensor,
						   &int3472->sensor_name);
	if (ret)
		return ret;

	/*
	 * Initialising this list means we can call gpiod_remove_lookup_table()
	 * in failure paths without issue.
	 */
	INIT_LIST_HEAD(&int3472->gpios.list);

	ret = skl_int3472_parse_crs(int3472);
	if (ret) {
		skl_int3472_discrete_remove(pdev);
		return ret;
	}

	acpi_dev_clear_dependencies(adev);
	return 0;
}

static const struct acpi_device_id int3472_device_id[] = {
	{ "INT3472", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, int3472_device_id);

static struct platform_driver int3472_discrete = {
	.driver = {
		.name = "int3472-discrete",
		.acpi_match_table = int3472_device_id,
	},
	.probe = skl_int3472_discrete_probe,
	.remove = skl_int3472_discrete_remove,
};
module_platform_driver(int3472_discrete);

MODULE_DESCRIPTION("Intel SkyLake INT3472 ACPI Discrete Device Driver");
MODULE_AUTHOR("Daniel Scally <djrscally@gmail.com>");
MODULE_LICENSE("GPL v2");
