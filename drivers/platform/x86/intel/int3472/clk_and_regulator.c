// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/acpi.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>

#include "common.h"

/*
 * 82c0d13a-78c5-4244-9bb1-eb8b539a8d11
 * This _DSM GUID allows controlling the sensor clk when it is not controlled
 * through a GPIO.
 */
static const guid_t img_clk_guid =
	GUID_INIT(0x82c0d13a, 0x78c5, 0x4244,
		  0x9b, 0xb1, 0xeb, 0x8b, 0x53, 0x9a, 0x8d, 0x11);

static void skl_int3472_enable_clk(struct int3472_clock *clk, int enable)
{
	struct int3472_discrete_device *int3472 = to_int3472_device(clk);
	union acpi_object args[3];
	union acpi_object argv4;

	if (clk->ena_gpio) {
		gpiod_set_value_cansleep(clk->ena_gpio, enable);
		return;
	}

	args[0].integer.type = ACPI_TYPE_INTEGER;
	args[0].integer.value = clk->imgclk_index;
	args[1].integer.type = ACPI_TYPE_INTEGER;
	args[1].integer.value = enable;
	args[2].integer.type = ACPI_TYPE_INTEGER;
	args[2].integer.value = 1;

	argv4.type = ACPI_TYPE_PACKAGE;
	argv4.package.count = 3;
	argv4.package.elements = args;

	acpi_evaluate_dsm(acpi_device_handle(int3472->adev), &img_clk_guid,
			  0, 1, &argv4);
}

/*
 * The regulators have to have .ops to be valid, but the only ops we actually
 * support are .enable and .disable which are handled via .ena_gpiod. Pass an
 * empty struct to clear the check without lying about capabilities.
 */
static const struct regulator_ops int3472_gpio_regulator_ops;

static int skl_int3472_clk_prepare(struct clk_hw *hw)
{
	skl_int3472_enable_clk(to_int3472_clk(hw), 1);
	return 0;
}

static void skl_int3472_clk_unprepare(struct clk_hw *hw)
{
	skl_int3472_enable_clk(to_int3472_clk(hw), 0);
}

static int skl_int3472_clk_enable(struct clk_hw *hw)
{
	/*
	 * We're just turning a GPIO on to enable the clock, which operation
	 * has the potential to sleep. Given .enable() cannot sleep, but
	 * .prepare() can, we toggle the GPIO in .prepare() instead. Thus,
	 * nothing to do here.
	 */
	return 0;
}

static void skl_int3472_clk_disable(struct clk_hw *hw)
{
	/* Likewise, nothing to do here... */
}

static unsigned int skl_int3472_get_clk_frequency(struct int3472_discrete_device *int3472)
{
	union acpi_object *obj;
	unsigned int freq;

	obj = skl_int3472_get_acpi_buffer(int3472->sensor, "SSDB");
	if (IS_ERR(obj))
		return 0; /* report rate as 0 on error */

	if (obj->buffer.length < CIO2_SENSOR_SSDB_MCLKSPEED_OFFSET + sizeof(u32)) {
		dev_err(int3472->dev, "The buffer is too small\n");
		kfree(obj);
		return 0;
	}

	freq = *(u32 *)(obj->buffer.pointer + CIO2_SENSOR_SSDB_MCLKSPEED_OFFSET);

	kfree(obj);
	return freq;
}

static unsigned long skl_int3472_clk_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct int3472_clock *clk = to_int3472_clk(hw);

	return clk->frequency;
}

static const struct clk_ops skl_int3472_clock_ops = {
	.prepare = skl_int3472_clk_prepare,
	.unprepare = skl_int3472_clk_unprepare,
	.enable = skl_int3472_clk_enable,
	.disable = skl_int3472_clk_disable,
	.recalc_rate = skl_int3472_clk_recalc_rate,
};

int skl_int3472_register_dsm_clock(struct int3472_discrete_device *int3472)
{
	struct acpi_device *adev = int3472->adev;
	struct clk_init_data init = {
		.ops = &skl_int3472_clock_ops,
		.flags = CLK_GET_RATE_NOCACHE,
	};
	int ret;

	if (int3472->clock.cl)
		return 0; /* A GPIO controlled clk has already been registered */

	if (!acpi_check_dsm(adev->handle, &img_clk_guid, 0, BIT(1)))
		return 0; /* DSM clock control is not available */

	init.name = kasprintf(GFP_KERNEL, "%s-clk", acpi_dev_name(adev));
	if (!init.name)
		return -ENOMEM;

	int3472->clock.frequency = skl_int3472_get_clk_frequency(int3472);
	int3472->clock.clk_hw.init = &init;
	int3472->clock.clk = clk_register(&adev->dev, &int3472->clock.clk_hw);
	if (IS_ERR(int3472->clock.clk)) {
		ret = PTR_ERR(int3472->clock.clk);
		goto out_free_init_name;
	}

	int3472->clock.cl = clkdev_create(int3472->clock.clk, NULL, int3472->sensor_name);
	if (!int3472->clock.cl) {
		ret = -ENOMEM;
		goto err_unregister_clk;
	}

	kfree(init.name);
	return 0;

err_unregister_clk:
	clk_unregister(int3472->clock.clk);
out_free_init_name:
	kfree(init.name);
	return ret;
}

int skl_int3472_register_gpio_clock(struct int3472_discrete_device *int3472,
				    struct acpi_resource_gpio *agpio, u32 polarity)
{
	char *path = agpio->resource_source.string_ptr;
	struct clk_init_data init = {
		.ops = &skl_int3472_clock_ops,
		.flags = CLK_GET_RATE_NOCACHE,
	};
	int ret;

	if (int3472->clock.cl)
		return -EBUSY;

	int3472->clock.ena_gpio = acpi_get_and_request_gpiod(path, agpio->pin_table[0],
							     "int3472,clk-enable");
	if (IS_ERR(int3472->clock.ena_gpio)) {
		ret = PTR_ERR(int3472->clock.ena_gpio);
		int3472->clock.ena_gpio = NULL;
		return dev_err_probe(int3472->dev, ret, "getting clk-enable GPIO\n");
	}

	if (polarity == GPIO_ACTIVE_LOW)
		gpiod_toggle_active_low(int3472->clock.ena_gpio);

	/* Ensure the pin is in output mode and non-active state */
	gpiod_direction_output(int3472->clock.ena_gpio, 0);

	init.name = kasprintf(GFP_KERNEL, "%s-clk",
			      acpi_dev_name(int3472->adev));
	if (!init.name) {
		ret = -ENOMEM;
		goto out_put_gpio;
	}

	int3472->clock.frequency = skl_int3472_get_clk_frequency(int3472);

	int3472->clock.clk_hw.init = &init;
	int3472->clock.clk = clk_register(&int3472->adev->dev,
					  &int3472->clock.clk_hw);
	if (IS_ERR(int3472->clock.clk)) {
		ret = PTR_ERR(int3472->clock.clk);
		goto out_free_init_name;
	}

	int3472->clock.cl = clkdev_create(int3472->clock.clk, NULL,
					  int3472->sensor_name);
	if (!int3472->clock.cl) {
		ret = -ENOMEM;
		goto err_unregister_clk;
	}

	kfree(init.name);
	return 0;

err_unregister_clk:
	clk_unregister(int3472->clock.clk);
out_free_init_name:
	kfree(init.name);
out_put_gpio:
	gpiod_put(int3472->clock.ena_gpio);

	return ret;
}

void skl_int3472_unregister_clock(struct int3472_discrete_device *int3472)
{
	if (!int3472->clock.cl)
		return;

	clkdev_drop(int3472->clock.cl);
	clk_unregister(int3472->clock.clk);
	gpiod_put(int3472->clock.ena_gpio);
}

/*
 * The INT3472 device is going to be the only supplier of a regulator for
 * the sensor device. But unlike the clk framework the regulator framework
 * does not allow matching by consumer-device-name only.
 *
 * Ideally all sensor drivers would use "avdd" as supply-id. But for drivers
 * where this cannot be changed because another supply-id is already used in
 * e.g. DeviceTree files an alias for the other supply-id can be added here.
 *
 * Do not forget to update GPIO_REGULATOR_SUPPLY_MAP_COUNT when changing this.
 */
static const char * const skl_int3472_regulator_map_supplies[] = {
	"avdd",
	"AVDD",
};

static_assert(ARRAY_SIZE(skl_int3472_regulator_map_supplies) ==
	      GPIO_REGULATOR_SUPPLY_MAP_COUNT);

/*
 * On some models there is a single GPIO regulator which is shared between
 * sensors and only listed in the ACPI resources of one sensor.
 * This DMI table contains the name of the second sensor. This is used to add
 * entries for the second sensor to the supply_map.
 */
static const struct dmi_system_id skl_int3472_regulator_second_sensor[] = {
	{
		/* Lenovo Miix 510-12IKB */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "MIIX 510-12IKB"),
		},
		.driver_data = "i2c-OVTI2680:00",
	},
	{ }
};

int skl_int3472_register_regulator(struct int3472_discrete_device *int3472,
				   struct acpi_resource_gpio *agpio)
{
	char *path = agpio->resource_source.string_ptr;
	struct regulator_init_data init_data = { };
	struct regulator_config cfg = { };
	const char *second_sensor = NULL;
	const struct dmi_system_id *id;
	int i, j, ret;

	id = dmi_first_match(skl_int3472_regulator_second_sensor);
	if (id)
		second_sensor = id->driver_data;

	for (i = 0, j = 0; i < ARRAY_SIZE(skl_int3472_regulator_map_supplies); i++) {
		int3472->regulator.supply_map[j].supply = skl_int3472_regulator_map_supplies[i];
		int3472->regulator.supply_map[j].dev_name = int3472->sensor_name;
		j++;

		if (second_sensor) {
			int3472->regulator.supply_map[j].supply =
				skl_int3472_regulator_map_supplies[i];
			int3472->regulator.supply_map[j].dev_name = second_sensor;
			j++;
		}
	}

	init_data.constraints.valid_ops_mask = REGULATOR_CHANGE_STATUS;
	init_data.consumer_supplies = int3472->regulator.supply_map;
	init_data.num_consumer_supplies = j;

	snprintf(int3472->regulator.regulator_name,
		 sizeof(int3472->regulator.regulator_name), "%s-regulator",
		 acpi_dev_name(int3472->adev));
	snprintf(int3472->regulator.supply_name,
		 GPIO_REGULATOR_SUPPLY_NAME_LENGTH, "supply-0");

	int3472->regulator.rdesc = INT3472_REGULATOR(
						int3472->regulator.regulator_name,
						int3472->regulator.supply_name,
						&int3472_gpio_regulator_ops);

	int3472->regulator.gpio = acpi_get_and_request_gpiod(path, agpio->pin_table[0],
							     "int3472,regulator");
	if (IS_ERR(int3472->regulator.gpio)) {
		ret = PTR_ERR(int3472->regulator.gpio);
		int3472->regulator.gpio = NULL;
		return dev_err_probe(int3472->dev, ret, "getting regulator GPIO\n");
	}

	/* Ensure the pin is in output mode and non-active state */
	gpiod_direction_output(int3472->regulator.gpio, 0);

	cfg.dev = &int3472->adev->dev;
	cfg.init_data = &init_data;
	cfg.ena_gpiod = int3472->regulator.gpio;

	int3472->regulator.rdev = regulator_register(int3472->dev,
						     &int3472->regulator.rdesc,
						     &cfg);
	if (IS_ERR(int3472->regulator.rdev)) {
		ret = PTR_ERR(int3472->regulator.rdev);
		goto err_free_gpio;
	}

	return 0;

err_free_gpio:
	gpiod_put(int3472->regulator.gpio);

	return ret;
}

void skl_int3472_unregister_regulator(struct int3472_discrete_device *int3472)
{
	regulator_unregister(int3472->regulator.rdev);
	gpiod_put(int3472->regulator.gpio);
}
