// SPDX-License-Identifier: GPL-2.0
/* Author: Dan Scally <djrscally@gmail.com> */

#include <linux/acpi.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>

#include "common.h"

/*
 * The regulators have to have .ops to be valid, but the only ops we actually
 * support are .enable and .disable which are handled via .ena_gpiod. Pass an
 * empty struct to clear the check without lying about capabilities.
 */
static const struct regulator_ops int3472_gpio_regulator_ops;

static int skl_int3472_clk_prepare(struct clk_hw *hw)
{
	struct int3472_gpio_clock *clk = to_int3472_clk(hw);

	gpiod_set_value_cansleep(clk->ena_gpio, 1);
	return 0;
}

static void skl_int3472_clk_unprepare(struct clk_hw *hw)
{
	struct int3472_gpio_clock *clk = to_int3472_clk(hw);

	gpiod_set_value_cansleep(clk->ena_gpio, 0);
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
	struct int3472_gpio_clock *clk = to_int3472_clk(hw);

	return clk->frequency;
}

static const struct clk_ops skl_int3472_clock_ops = {
	.prepare = skl_int3472_clk_prepare,
	.unprepare = skl_int3472_clk_unprepare,
	.enable = skl_int3472_clk_enable,
	.disable = skl_int3472_clk_disable,
	.recalc_rate = skl_int3472_clk_recalc_rate,
};

int skl_int3472_register_clock(struct int3472_discrete_device *int3472,
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

int skl_int3472_register_regulator(struct int3472_discrete_device *int3472,
				   struct acpi_resource_gpio *agpio)
{
	const struct int3472_sensor_config *sensor_config;
	char *path = agpio->resource_source.string_ptr;
	struct regulator_consumer_supply supply_map;
	struct regulator_init_data init_data = { };
	struct regulator_config cfg = { };
	int ret;

	sensor_config = int3472->sensor_config;
	if (IS_ERR(sensor_config)) {
		dev_err(int3472->dev, "No sensor module config\n");
		return PTR_ERR(sensor_config);
	}

	if (!sensor_config->supply_map.supply) {
		dev_err(int3472->dev, "No supply name defined\n");
		return -ENODEV;
	}

	init_data.constraints.valid_ops_mask = REGULATOR_CHANGE_STATUS;
	init_data.num_consumer_supplies = 1;
	supply_map = sensor_config->supply_map;
	supply_map.dev_name = int3472->sensor_name;
	init_data.consumer_supplies = &supply_map;

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
