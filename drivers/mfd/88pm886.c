// SPDX-License-Identifier: GPL-2.0-only
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>

#include <linux/mfd/88pm886.h>

static const struct regmap_config pm886_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PM886_REG_RTC_SPARE6,
};

static struct regmap_irq pm886_regmap_irqs[] = {
	REGMAP_IRQ_REG(PM886_IRQ_ONKEY, 0, PM886_INT_ENA1_ONKEY),
};

static struct regmap_irq_chip pm886_regmap_irq_chip = {
	.name = "88pm886",
	.irqs = pm886_regmap_irqs,
	.num_irqs = ARRAY_SIZE(pm886_regmap_irqs),
	.num_regs = 4,
	.status_base = PM886_REG_INT_STATUS1,
	.ack_base = PM886_REG_INT_STATUS1,
	.unmask_base = PM886_REG_INT_ENA_1,
};

static struct resource pm886_onkey_resources[] = {
	DEFINE_RES_IRQ_NAMED(PM886_IRQ_ONKEY, "88pm886-onkey"),
};

static struct mfd_cell pm886_devs[] = {
	MFD_CELL_RES("88pm886-onkey", pm886_onkey_resources),
	MFD_CELL_NAME("88pm886-regulator"),
	MFD_CELL_NAME("88pm886-rtc"),
};

static int pm886_power_off_handler(struct sys_off_data *sys_off_data)
{
	struct pm886_chip *chip = sys_off_data->cb_data;
	struct regmap *regmap = chip->regmap;
	struct device *dev = &chip->client->dev;
	int err;

	err = regmap_update_bits(regmap, PM886_REG_MISC_CONFIG1, PM886_SW_PDOWN, PM886_SW_PDOWN);
	if (err) {
		dev_err(dev, "Failed to power off the device: %d\n", err);
		return NOTIFY_BAD;
	}
	return NOTIFY_DONE;
}

static int pm886_setup_irq(struct pm886_chip *chip,
		struct regmap_irq_chip_data **irq_data)
{
	struct regmap *regmap = chip->regmap;
	struct device *dev = &chip->client->dev;
	int err;

	/* Set interrupt clearing mode to clear on write. */
	err = regmap_update_bits(regmap, PM886_REG_MISC_CONFIG2,
			PM886_INT_INV | PM886_INT_CLEAR | PM886_INT_MASK_MODE,
			PM886_INT_WC);
	if (err) {
		dev_err(dev, "Failed to set interrupt clearing mode: %d\n", err);
		return err;
	}

	err = devm_regmap_add_irq_chip(dev, regmap, chip->client->irq,
					IRQF_ONESHOT, 0, &pm886_regmap_irq_chip,
					irq_data);
	if (err) {
		dev_err(dev, "Failed to request IRQ: %d\n", err);
		return err;
	}

	return 0;
}

static int pm886_probe(struct i2c_client *client)
{
	struct regmap_irq_chip_data *irq_data;
	struct device *dev = &client->dev;
	struct pm886_chip *chip;
	struct regmap *regmap;
	unsigned int chip_id;
	int err;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->chip_id = (uintptr_t)device_get_match_data(dev);
	i2c_set_clientdata(client, chip);

	regmap = devm_regmap_init_i2c(client, &pm886_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to initialize regmap\n");
	chip->regmap = regmap;

	err = regmap_read(regmap, PM886_REG_ID, &chip_id);
	if (err)
		return dev_err_probe(dev, err, "Failed to read chip ID\n");

	if (chip->chip_id != chip_id)
		return dev_err_probe(dev, -EINVAL, "Unsupported chip: 0x%x\n", chip_id);

	err = pm886_setup_irq(chip, &irq_data);
	if (err)
		return err;

	err = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, pm886_devs, ARRAY_SIZE(pm886_devs),
				NULL, 0, regmap_irq_get_domain(irq_data));
	if (err)
		return dev_err_probe(dev, err, "Failed to add devices\n");

	err = devm_register_power_off_handler(dev, pm886_power_off_handler, chip);
	if (err)
		return dev_err_probe(dev, err, "Failed to register power off handler\n");

	device_init_wakeup(dev, device_property_read_bool(dev, "wakeup-source"));

	return 0;
}

static const struct of_device_id pm886_of_match[] = {
	{ .compatible = "marvell,88pm886-a1", .data = (void *)PM886_A1_CHIP_ID },
	{ }
};
MODULE_DEVICE_TABLE(of, pm886_of_match);

static struct i2c_driver pm886_i2c_driver = {
	.driver = {
		.name = "88pm886",
		.of_match_table = pm886_of_match,
	},
	.probe = pm886_probe,
};
module_i2c_driver(pm886_i2c_driver);

MODULE_DESCRIPTION("Marvell 88PM886 PMIC driver");
MODULE_AUTHOR("Karel Balej <balejk@matfyz.cz>");
MODULE_LICENSE("GPL");
