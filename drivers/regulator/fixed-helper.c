#include <linux/slab.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>

struct fixed_regulator_data {
	struct fixed_voltage_config cfg;
	struct regulator_init_data init_data;
	struct platform_device pdev;
};

static void regulator_fixed_release(struct device *dev)
{
	struct fixed_regulator_data *data = container_of(dev,
			struct fixed_regulator_data, pdev.dev);
	kfree(data->cfg.supply_name);
	kfree(data);
}

/**
 * regulator_register_fixed_name - register a no-op fixed regulator
 * @id: platform device id
 * @name: name to be used for the regulator
 * @supplies: consumers for this regulator
 * @num_supplies: number of consumers
 */
struct platform_device *regulator_register_always_on(int id, const char *name,
		struct regulator_consumer_supply *supplies, int num_supplies)
{
	struct fixed_regulator_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->cfg.supply_name = kstrdup(name, GFP_KERNEL);
	if (!data->cfg.supply_name) {
		kfree(data);
		return NULL;
	}

	data->cfg.microvolts = 0;
	data->cfg.gpio = -EINVAL;
	data->cfg.enabled_at_boot = 1;
	data->cfg.init_data = &data->init_data;

	data->init_data.constraints.always_on = 1;
	data->init_data.consumer_supplies = supplies;
	data->init_data.num_consumer_supplies = num_supplies;

	data->pdev.name = "reg-fixed-voltage";
	data->pdev.id = id;
	data->pdev.dev.platform_data = &data->cfg;
	data->pdev.dev.release = regulator_fixed_release;

	platform_device_register(&data->pdev);

	return &data->pdev;
}
