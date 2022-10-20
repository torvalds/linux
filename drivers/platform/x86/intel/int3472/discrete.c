// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/acpi.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
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

/*
 * 822ace8f-2814-4174-a56b-5f029fe079ee
 * This _DSM GUID returns a string from the sensor device, which acts as a
 * module identifier.
 */
static const guid_t cio2_sensor_module_guid =
	GUID_INIT(0x822ace8f, 0x2814, 0x4174,
		  0xa5, 0x6b, 0x5f, 0x02, 0x9f, 0xe0, 0x79, 0xee);

/*
 * Here follows platform specific mapping information that we can pass to
 * the functions mapping resources to the sensors. Where the sensors have
 * a power enable pin defined in DSDT we need to provide a supply name so
 * the sensor drivers can find the regulator. The device name will be derived
 * from the sensor's ACPI device within the code. Optionally, we can provide a
 * NULL terminated array of function name mappings to deal with any platform
 * specific deviations from the documented behaviour of GPIOs.
 *
 * Map a GPIO function name to NULL to prevent the driver from mapping that
 * GPIO at all.
 */

static const struct int3472_gpio_function_remap ov2680_gpio_function_remaps[] = {
	{ "reset", NULL },
	{ "powerdown", "reset" },
	{ }
};

static const struct int3472_sensor_config int3472_sensor_configs[] = {
	/* Lenovo Miix 510-12ISK - OV2680, Front */
	{ "GNDF140809R", { 0 }, ov2680_gpio_function_remaps },
	/* Lenovo Miix 510-12ISK - OV5648, Rear */
	{ "GEFF150023R", REGULATOR_SUPPLY("avdd", NULL), NULL },
	/* Surface Go 1&2 - OV5693, Front */
	{ "YHCU", REGULATOR_SUPPLY("avdd", NULL), NULL },
};

static const struct int3472_sensor_config *
skl_int3472_get_sensor_module_config(struct int3472_discrete_device *int3472)
{
	union acpi_object *obj;
	unsigned int i;

	obj = acpi_evaluate_dsm_typed(int3472->sensor->handle,
				      &cio2_sensor_module_guid, 0x00,
				      0x01, NULL, ACPI_TYPE_STRING);

	if (!obj) {
		dev_err(int3472->dev,
			"Failed to get sensor module string from _DSM\n");
		return ERR_PTR(-ENODEV);
	}

	if (obj->string.type != ACPI_TYPE_STRING) {
		dev_err(int3472->dev,
			"Sensor _DSM returned a non-string value\n");

		ACPI_FREE(obj);
		return ERR_PTR(-EINVAL);
	}

	for (i = 0; i < ARRAY_SIZE(int3472_sensor_configs); i++) {
		if (!strcmp(int3472_sensor_configs[i].sensor_module_name,
			    obj->string.pointer))
			break;
	}

	ACPI_FREE(obj);

	if (i >= ARRAY_SIZE(int3472_sensor_configs))
		return ERR_PTR(-EINVAL);

	return &int3472_sensor_configs[i];
}

static int skl_int3472_map_gpio_to_sensor(struct int3472_discrete_device *int3472,
					  struct acpi_resource_gpio *agpio,
					  const char *func, u32 polarity)
{
	const struct int3472_sensor_config *sensor_config;
	char *path = agpio->resource_source.string_ptr;
	struct gpiod_lookup *table_entry;
	struct acpi_device *adev;
	acpi_handle handle;
	acpi_status status;

	if (int3472->n_sensor_gpios >= INT3472_MAX_SENSOR_GPIOS) {
		dev_warn(int3472->dev, "Too many GPIOs mapped\n");
		return -EINVAL;
	}

	sensor_config = int3472->sensor_config;
	if (!IS_ERR(sensor_config) && sensor_config->function_maps) {
		const struct int3472_gpio_function_remap *remap;

		for (remap = sensor_config->function_maps; remap->documented; remap++) {
			if (!strcmp(func, remap->documented)) {
				func = remap->actual;
				break;
			}
		}
	}

	/* Functions mapped to NULL should not be mapped to the sensor */
	if (!func)
		return 0;

	status = acpi_get_handle(NULL, path, &handle);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	adev = acpi_fetch_acpi_dev(handle);
	if (!adev)
		return -ENODEV;

	table_entry = &int3472->gpios.table[int3472->n_sensor_gpios];
	table_entry->key = acpi_dev_name(adev);
	table_entry->chip_hwnum = agpio->pin_table[0];
	table_entry->con_id = func;
	table_entry->idx = 0;
	table_entry->flags = polarity;

	int3472->n_sensor_gpios++;

	return 0;
}

static int skl_int3472_map_gpio_to_clk(struct int3472_discrete_device *int3472,
				       struct acpi_resource_gpio *agpio, u8 type)
{
	char *path = agpio->resource_source.string_ptr;
	u16 pin = agpio->pin_table[0];
	struct gpio_desc *gpio;

	switch (type) {
	case INT3472_GPIO_TYPE_CLK_ENABLE:
		gpio = acpi_get_and_request_gpiod(path, pin, "int3472,clk-enable");
		if (IS_ERR(gpio))
			return (PTR_ERR(gpio));

		int3472->clock.ena_gpio = gpio;
		break;
	case INT3472_GPIO_TYPE_PRIVACY_LED:
		gpio = acpi_get_and_request_gpiod(path, pin, "int3472,privacy-led");
		if (IS_ERR(gpio))
			return (PTR_ERR(gpio));

		int3472->clock.led_gpio = gpio;
		break;
	default:
		dev_err(int3472->dev, "Invalid GPIO type 0x%02x for clock\n", type);
		break;
	}

	return 0;
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
	union acpi_object *obj;
	const char *err_msg;
	int ret;
	u8 type;

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

	type = obj->integer.value & 0xff;

	switch (type) {
	case INT3472_GPIO_TYPE_RESET:
		ret = skl_int3472_map_gpio_to_sensor(int3472, agpio, "reset",
						     GPIO_ACTIVE_LOW);
		if (ret)
			err_msg = "Failed to map reset pin to sensor\n";

		break;
	case INT3472_GPIO_TYPE_POWERDOWN:
		ret = skl_int3472_map_gpio_to_sensor(int3472, agpio, "powerdown",
						     GPIO_ACTIVE_LOW);
		if (ret)
			err_msg = "Failed to map powerdown pin to sensor\n";

		break;
	case INT3472_GPIO_TYPE_CLK_ENABLE:
	case INT3472_GPIO_TYPE_PRIVACY_LED:
		ret = skl_int3472_map_gpio_to_clk(int3472, agpio, type);
		if (ret)
			err_msg = "Failed to map GPIO to clock\n";

		break;
	case INT3472_GPIO_TYPE_POWER_ENABLE:
		ret = skl_int3472_register_regulator(int3472, agpio);
		if (ret)
			err_msg = "Failed to map regulator to sensor\n";

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

	/*
	 * No error check, because not having a sensor config is not necessarily
	 * a failure mode.
	 */
	int3472->sensor_config = skl_int3472_get_sensor_module_config(int3472);

	ret = acpi_dev_get_resources(int3472->adev, &resource_list,
				     skl_int3472_handle_gpio_resources,
				     int3472);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&resource_list);

	/*
	 * If we find no clock enable GPIO pin then the privacy LED won't work.
	 * We've never seen that situation, but it's possible. Warn the user so
	 * it's clear what's happened.
	 */
	if (int3472->clock.ena_gpio) {
		ret = skl_int3472_register_clock(int3472);
		if (ret)
			return ret;
	} else {
		if (int3472->clock.led_gpio)
			dev_warn(int3472->dev,
				 "No clk GPIO. The privacy LED won't work\n");
	}

	int3472->gpios.dev_id = int3472->sensor_name;
	gpiod_add_lookup_table(&int3472->gpios);

	return 0;
}

static int skl_int3472_discrete_remove(struct platform_device *pdev)
{
	struct int3472_discrete_device *int3472 = platform_get_drvdata(pdev);

	gpiod_remove_lookup_table(&int3472->gpios);

	if (int3472->clock.cl)
		skl_int3472_unregister_clock(int3472);

	gpiod_put(int3472->clock.ena_gpio);
	gpiod_put(int3472->clock.led_gpio);

	skl_int3472_unregister_regulator(int3472);

	return 0;
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
