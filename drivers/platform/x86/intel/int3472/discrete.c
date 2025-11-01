// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/platform_data/x86/int3472.h>
#include <linux/platform_device.h>
#include <linux/string_choices.h>
#include <linux/uuid.h>

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
					 const char *con_id, unsigned long gpio_flags)
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

	*table_entry = GPIO_LOOKUP(acpi_dev_name(adev), agpio->pin_table[0], con_id, gpio_flags);

	return 0;
}

static int skl_int3472_map_gpio_to_sensor(struct int3472_discrete_device *int3472,
					  struct acpi_resource_gpio *agpio,
					  const char *con_id, unsigned long gpio_flags)
{
	int ret;

	if (int3472->n_sensor_gpios >= INT3472_MAX_SENSOR_GPIOS) {
		dev_warn(int3472->dev, "Too many GPIOs mapped\n");
		return -EINVAL;
	}

	ret = skl_int3472_fill_gpiod_lookup(&int3472->gpios.table[int3472->n_sensor_gpios],
					    agpio, con_id, gpio_flags);
	if (ret)
		return ret;

	int3472->n_sensor_gpios++;

	return 0;
}

/* This should *really* only be used when there's no other way... */
static struct gpio_desc *
skl_int3472_gpiod_get_from_temp_lookup(struct int3472_discrete_device *int3472,
				       struct acpi_resource_gpio *agpio,
				       const char *con_id, unsigned long gpio_flags)
{
	struct gpio_desc *desc;
	int ret;

	struct gpiod_lookup_table *lookup __free(kfree) =
			kzalloc(struct_size(lookup, table, 2), GFP_KERNEL);
	if (!lookup)
		return ERR_PTR(-ENOMEM);

	lookup->dev_id = dev_name(int3472->dev);
	ret = skl_int3472_fill_gpiod_lookup(&lookup->table[0], agpio, con_id, gpio_flags);
	if (ret)
		return ERR_PTR(ret);

	gpiod_add_lookup_table(lookup);
	desc = gpiod_get(int3472->dev, con_id, GPIOD_OUT_LOW);
	gpiod_remove_lookup_table(lookup);

	return desc;
}

/**
 * struct int3472_gpio_map - Map GPIOs to whatever is expected by the
 * sensor driver (as in DT bindings)
 * @hid: The ACPI HID of the device without the instance number e.g. INT347E
 * @type_from: The GPIO type from ACPI ?SDT
 * @type_to: The assigned GPIO type, typically same as @type_from
 * @enable_time_us: Enable time in usec for GPIOs mapped to regulators
 * @con_id: The name of the GPIO for the device
 * @polarity_low: GPIO_ACTIVE_LOW true if the @polarity_low is true,
 * GPIO_ACTIVE_HIGH otherwise
 */
struct int3472_gpio_map {
	const char *hid;
	u8 type_from;
	u8 type_to;
	bool polarity_low;
	unsigned int enable_time_us;
	const char *con_id;
};

static const struct int3472_gpio_map int3472_gpio_map[] = {
	{	/* mt9m114 designs declare a powerdown pin which controls the regulators */
		.hid = "INT33F0",
		.type_from = INT3472_GPIO_TYPE_POWERDOWN,
		.type_to = INT3472_GPIO_TYPE_POWER_ENABLE,
		.con_id = "vdd",
		.enable_time_us = GPIO_REGULATOR_ENABLE_TIME,
	},
	{	/* ov7251 driver / DT-bindings expect "enable" as con_id for reset */
		.hid = "INT347E",
		.type_from = INT3472_GPIO_TYPE_RESET,
		.type_to = INT3472_GPIO_TYPE_RESET,
		.con_id = "enable",
	},
	{	/* ov08x40's handshake pin needs a 45 ms delay on some HP laptops */
		.hid = "OVTI08F4",
		.type_from = INT3472_GPIO_TYPE_HANDSHAKE,
		.type_to = INT3472_GPIO_TYPE_HANDSHAKE,
		.con_id = "dvdd",
		.enable_time_us = 45 * USEC_PER_MSEC,
	},
};

static void int3472_get_con_id_and_polarity(struct int3472_discrete_device *int3472, u8 *type,
					    const char **con_id, unsigned long *gpio_flags,
					    unsigned int *enable_time_us)
{
	struct acpi_device *adev = int3472->sensor;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(int3472_gpio_map); i++) {
		/*
		 * Map the firmware-provided GPIO to whatever a driver expects
		 * (as in DT bindings). First check if the type matches with the
		 * GPIO map, then further check that the device _HID matches.
		 */
		if (*type != int3472_gpio_map[i].type_from)
			continue;

		if (!acpi_dev_hid_uid_match(adev, int3472_gpio_map[i].hid, NULL))
			continue;

		dev_dbg(int3472->dev, "mapping type 0x%02x pin to 0x%02x %s\n",
			*type, int3472_gpio_map[i].type_to, int3472_gpio_map[i].con_id);

		*type = int3472_gpio_map[i].type_to;
		*gpio_flags = int3472_gpio_map[i].polarity_low ?
			      GPIO_ACTIVE_LOW : GPIO_ACTIVE_HIGH;
		*con_id = int3472_gpio_map[i].con_id;
		*enable_time_us = int3472_gpio_map[i].enable_time_us;
		return;
	}

	*enable_time_us = GPIO_REGULATOR_ENABLE_TIME;

	switch (*type) {
	case INT3472_GPIO_TYPE_RESET:
		*con_id = "reset";
		*gpio_flags = GPIO_ACTIVE_LOW;
		break;
	case INT3472_GPIO_TYPE_POWERDOWN:
		*con_id = "powerdown";
		*gpio_flags = GPIO_ACTIVE_LOW;
		break;
	case INT3472_GPIO_TYPE_CLK_ENABLE:
		*con_id = "clk-enable";
		*gpio_flags = GPIO_ACTIVE_HIGH;
		break;
	case INT3472_GPIO_TYPE_PRIVACY_LED:
		*con_id = "privacy-led";
		*gpio_flags = GPIO_ACTIVE_HIGH;
		break;
	case INT3472_GPIO_TYPE_HOTPLUG_DETECT:
		*con_id = "hpd";
		*gpio_flags = GPIO_ACTIVE_HIGH;
		break;
	case INT3472_GPIO_TYPE_POWER_ENABLE:
		*con_id = "avdd";
		*gpio_flags = GPIO_ACTIVE_HIGH;
		break;
	case INT3472_GPIO_TYPE_HANDSHAKE:
		*con_id = "dvdd";
		*gpio_flags = GPIO_ACTIVE_HIGH;
		/* Setups using a handshake pin need 25 ms enable delay */
		*enable_time_us = 25 * USEC_PER_MSEC;
		break;
	default:
		*con_id = "unknown";
		*gpio_flags = GPIO_ACTIVE_HIGH;
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
 * 0x13 Hotplug detect
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
 * * 1		- Continue the loop without adding a copy of the resource to
 * *		  the list passed to acpi_dev_get_resources()
 * * 0		- Continue the loop after adding a copy of the resource to
 * *		  the list passed to acpi_dev_get_resources()
 * * -errno	- Error, break loop
 */
static int skl_int3472_handle_gpio_resources(struct acpi_resource *ares,
					     void *data)
{
	struct int3472_discrete_device *int3472 = data;
	const char *second_sensor = NULL;
	struct acpi_resource_gpio *agpio;
	unsigned int enable_time_us;
	u8 active_value, pin, type;
	unsigned long gpio_flags;
	union acpi_object *obj;
	struct gpio_desc *gpio;
	const char *err_msg;
	const char *con_id;
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

	int3472_get_con_id_and_polarity(int3472, &type, &con_id, &gpio_flags, &enable_time_us);

	pin = FIELD_GET(INT3472_GPIO_DSM_PIN, obj->integer.value);
	/* Pin field is not really used under Windows and wraps around at 8 bits */
	if (pin != (agpio->pin_table[0] & 0xff))
		dev_dbg(int3472->dev, FW_BUG "%s %s pin number mismatch _DSM %d resource %d\n",
			con_id, agpio->resource_source.string_ptr, pin, agpio->pin_table[0]);

	active_value = FIELD_GET(INT3472_GPIO_DSM_SENSOR_ON_VAL, obj->integer.value);
	if (!active_value)
		gpio_flags ^= GPIO_ACTIVE_LOW;

	dev_dbg(int3472->dev, "%s %s pin %d active-%s\n", con_id,
		agpio->resource_source.string_ptr, agpio->pin_table[0],
		str_high_low(gpio_flags == GPIO_ACTIVE_HIGH));

	switch (type) {
	case INT3472_GPIO_TYPE_RESET:
	case INT3472_GPIO_TYPE_POWERDOWN:
	case INT3472_GPIO_TYPE_HOTPLUG_DETECT:
		ret = skl_int3472_map_gpio_to_sensor(int3472, agpio, con_id, gpio_flags);
		if (ret)
			err_msg = "Failed to map GPIO pin to sensor\n";

		break;
	case INT3472_GPIO_TYPE_CLK_ENABLE:
	case INT3472_GPIO_TYPE_PRIVACY_LED:
	case INT3472_GPIO_TYPE_POWER_ENABLE:
	case INT3472_GPIO_TYPE_HANDSHAKE:
		gpio = skl_int3472_gpiod_get_from_temp_lookup(int3472, agpio, con_id, gpio_flags);
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
			second_sensor = int3472->quirks.avdd_second_sensor;
			fallthrough;
		case INT3472_GPIO_TYPE_HANDSHAKE:
			ret = skl_int3472_register_regulator(int3472, gpio, enable_time_us,
							     con_id, second_sensor);
			if (ret)
				err_msg = "Failed to register regulator\n";

			break;
		default: /* Never reached */
			ret = -EINVAL;
			break;
		}

		if (ret)
			gpiod_put(gpio);

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

	/* Tell acpi_dev_get_resources() to not make a copy of the resource */
	return 1;
}

int int3472_discrete_parse_crs(struct int3472_discrete_device *int3472)
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
EXPORT_SYMBOL_NS_GPL(int3472_discrete_parse_crs, "INTEL_INT3472_DISCRETE");

void int3472_discrete_cleanup(struct int3472_discrete_device *int3472)
{
	gpiod_remove_lookup_table(&int3472->gpios);

	skl_int3472_unregister_clock(int3472);
	skl_int3472_unregister_pled(int3472);
	skl_int3472_unregister_regulator(int3472);
}
EXPORT_SYMBOL_NS_GPL(int3472_discrete_cleanup, "INTEL_INT3472_DISCRETE");

static void skl_int3472_discrete_remove(struct platform_device *pdev)
{
	int3472_discrete_cleanup(platform_get_drvdata(pdev));
}

static int skl_int3472_discrete_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	const struct int3472_discrete_quirks *quirks = NULL;
	struct int3472_discrete_device *int3472;
	const struct dmi_system_id *id;
	struct int3472_cldb cldb;
	int ret;

	if (!adev)
		return -ENODEV;

	id = dmi_first_match(skl_int3472_discrete_quirks);
	if (id)
		quirks = id->driver_data;

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

	if (quirks)
		int3472->quirks = *quirks;

	ret = skl_int3472_get_sensor_adev_and_name(&pdev->dev, &int3472->sensor,
						   &int3472->sensor_name);
	if (ret)
		return ret;

	/*
	 * Initialising this list means we can call gpiod_remove_lookup_table()
	 * in failure paths without issue.
	 */
	INIT_LIST_HEAD(&int3472->gpios.list);

	ret = int3472_discrete_parse_crs(int3472);
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
MODULE_IMPORT_NS("INTEL_INT3472");
