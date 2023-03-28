/* SPDX-License-Identifier: GPL-2.0 */
/* Author: Dan Scally <djrscally@gmail.com> */

#ifndef _INTEL_SKL_INT3472_H
#define _INTEL_SKL_INT3472_H

#include <linux/clk-provider.h>
#include <linux/gpio/machine.h>
#include <linux/leds.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/types.h>

/* FIXME drop this once the I2C_DEV_NAME_FORMAT macro has been added to include/linux/i2c.h */
#ifndef I2C_DEV_NAME_FORMAT
#define I2C_DEV_NAME_FORMAT					"i2c-%s"
#endif

/* PMIC GPIO Types */
#define INT3472_GPIO_TYPE_RESET					0x00
#define INT3472_GPIO_TYPE_POWERDOWN				0x01
#define INT3472_GPIO_TYPE_POWER_ENABLE				0x0b
#define INT3472_GPIO_TYPE_CLK_ENABLE				0x0c
#define INT3472_GPIO_TYPE_PRIVACY_LED				0x0d

#define INT3472_PDEV_MAX_NAME_LEN				23
#define INT3472_MAX_SENSOR_GPIOS				3

#define GPIO_REGULATOR_NAME_LENGTH				21
#define GPIO_REGULATOR_SUPPLY_NAME_LENGTH			9

#define INT3472_LED_MAX_NAME_LEN				32

#define CIO2_SENSOR_SSDB_MCLKSPEED_OFFSET			86

#define INT3472_REGULATOR(_name, _supply, _ops)			\
	(const struct regulator_desc) {				\
		.name = _name,					\
		.supply_name = _supply,				\
		.type = REGULATOR_VOLTAGE,			\
		.ops = _ops,					\
		.owner = THIS_MODULE,				\
	}

#define to_int3472_clk(hw)					\
	container_of(hw, struct int3472_gpio_clock, clk_hw)

#define to_int3472_device(clk)					\
	container_of(clk, struct int3472_discrete_device, clock)

struct acpi_device;
struct i2c_client;
struct platform_device;

struct int3472_cldb {
	u8 version;
	/*
	 * control logic type
	 * 0: UNKNOWN
	 * 1: DISCRETE(CRD-D)
	 * 2: PMIC TPS68470
	 * 3: PMIC uP6641
	 */
	u8 control_logic_type;
	u8 control_logic_id;
	u8 sensor_card_sku;
	u8 reserved[28];
};

struct int3472_gpio_function_remap {
	const char *documented;
	const char *actual;
};

struct int3472_sensor_config {
	const char *sensor_module_name;
	struct regulator_consumer_supply supply_map;
	const struct int3472_gpio_function_remap *function_maps;
};

struct int3472_discrete_device {
	struct acpi_device *adev;
	struct device *dev;
	struct acpi_device *sensor;
	const char *sensor_name;

	const struct int3472_sensor_config *sensor_config;

	struct int3472_gpio_regulator {
		char regulator_name[GPIO_REGULATOR_NAME_LENGTH];
		char supply_name[GPIO_REGULATOR_SUPPLY_NAME_LENGTH];
		struct gpio_desc *gpio;
		struct regulator_dev *rdev;
		struct regulator_desc rdesc;
	} regulator;

	struct int3472_gpio_clock {
		struct clk *clk;
		struct clk_hw clk_hw;
		struct clk_lookup *cl;
		struct gpio_desc *ena_gpio;
		u32 frequency;
	} clock;

	struct int3472_pled {
		struct led_classdev classdev;
		struct led_lookup_data lookup;
		char name[INT3472_LED_MAX_NAME_LEN];
		struct gpio_desc *gpio;
	} pled;

	unsigned int ngpios; /* how many GPIOs have we seen */
	unsigned int n_sensor_gpios; /* how many have we mapped to sensor */
	struct gpiod_lookup_table gpios;
};

union acpi_object *skl_int3472_get_acpi_buffer(struct acpi_device *adev,
					       char *id);
int skl_int3472_fill_cldb(struct acpi_device *adev, struct int3472_cldb *cldb);
int skl_int3472_get_sensor_adev_and_name(struct device *dev,
					 struct acpi_device **sensor_adev_ret,
					 const char **name_ret);

int skl_int3472_register_clock(struct int3472_discrete_device *int3472,
			       struct acpi_resource_gpio *agpio, u32 polarity);
void skl_int3472_unregister_clock(struct int3472_discrete_device *int3472);

int skl_int3472_register_regulator(struct int3472_discrete_device *int3472,
				   struct acpi_resource_gpio *agpio);
void skl_int3472_unregister_regulator(struct int3472_discrete_device *int3472);

int skl_int3472_register_pled(struct int3472_discrete_device *int3472,
			      struct acpi_resource_gpio *agpio, u32 polarity);
void skl_int3472_unregister_pled(struct int3472_discrete_device *int3472);

#endif
