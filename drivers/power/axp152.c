/*
 * Regulator driver for the axp152 PMIC
 *
 * Copyright 2013 Hans de Goede <hdegoede@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>
#include <mach/irqs.h>
#include <plat/sys_config.h>

#define AXP152_I2C_ADDR		  48
#define AXP20x_I2C_ADDR		  52
#define AXP152_REGULATOR_COUNT	   5

#define AXP152_REG_CHIP_VERSION	0x03
#define AXP152_REG_OUTPUT_CTRL	0x12
#define AXP152_REG_DCDC2	0x23
#define AXP152_REG_DCDC1	0x26
#define AXP152_REG_DCDC3	0x27
#define AXP152_REG_DLDO2	0x2a
#define AXP152_REG_DCDC4	0x2b
#define AXP152_REG_POWER	0x32
#define AXP152_REG_INTEN1	0x40
#define AXP152_REG_INTEN2	0x41
#define AXP152_REG_INTEN3	0x42
#define AXP152_REG_INTSTS1	0x48
#define AXP152_REG_INTSTS2	0x49
#define AXP152_REG_INTSTS3	0x4a

/* Bits inside AXP152_REG_OUTPUT_CTRL */
#define AXP152_DLDO2_ENABLE	0x01
#define AXP152_DCDC4_ENABLE	0x10
#define AXP152_DCDC3_ENABLE	0x20
#define AXP152_DCDC2_ENABLE	0x40
#define AXP152_DCDC1_ENABLE	0x80

/* Bits inside AXP152_REG_POWER */
#define AXP152_POWER_OFF	0x80

/* interrupt bits */
#define AXP152_IRQ_PEKLONG	(1 << 8)
#define AXP152_IRQ_PEKSHORT	(1 << 9)

static const struct i2c_device_id axp152_id_table[] = {
	{ "axp152", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, axp152_id_table);

static const int axp152_dcdc1_mvolt[16] = { 1700, 1800, 1900, 2000, 2100,
	2400, 2500, 2600, 2700, 2800, 3000, 3100, 3200, 3300, 3400, 3500 };

enum axp_regulator_ids {
	axp152_dcdc1,
	axp152_dcdc2,
	axp152_dcdc3,
	axp152_dcdc4,
	axp152_dldo2
};

struct axp152_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct regulator_dev *regulator[AXP152_REGULATOR_COUNT];
	struct mutex mutex;
	struct work_struct irq_work;
	int got_irq;
	u8 regs[256]; /* Register cache to avoid slow i2c transfers */
};

static struct axp152_data *pm_power_axp152;
static DEFINE_MUTEX(pm_power_axp152_mutex);

static int axp152_read_reg(struct axp152_data *axp152, int reg)
{
	int ret = i2c_smbus_read_byte_data(axp152->client, reg);
	if (ret < 0) {
		dev_err(&axp152->client->dev,
			"failed to read reg 0x%02x (%d)\n", reg, ret);
		return ret;
	}
	axp152->regs[reg] = ret;
	return 0;
}

static int axp152_write_reg(struct axp152_data *axp152, int reg, int val)
{
	int ret;

	if (axp152->regs[reg] == val)
		return 0;

	ret = i2c_smbus_write_byte_data(axp152->client, reg, val);
	if (ret < 0) {
		dev_err(&axp152->client->dev,
			"failed to write 0x%02x to 0x%02x (%d)\n",
			val, reg, ret);
		return ret;
	}
	axp152->regs[reg] = val;
	return 0;
}

static int axp152_read_interrupts(struct axp152_data *axp152, uint8_t start)
{
	int ret;
	uint8_t v[3] = { 0, 0, 0, };

	ret = i2c_smbus_read_i2c_block_data(axp152->client, start, 3, v);
	if (ret < 0) {
		dev_err(&axp152->client->dev, "interrupt read 0x%02x error\n",
			start);
		return ret;
	}
	return (v[2] << 16) | (v[1] << 8) | v[0];
}

static int axp152_write_interrupts(struct axp152_data *axp152, uint8_t start,
				   int irqs)
{
	int ret;
	uint8_t v[5];

	v[0] = irqs;
	v[1] = start + 1;
	v[2] = irqs >> 8;
	v[3] = start + 2;
	v[4] = irqs >> 16;

	ret = i2c_smbus_write_i2c_block_data(axp152->client, start, 3, v);
	if (ret < 0) {
		dev_err(&axp152->client->dev, "interrupt write 0x%02x error\n",
			start);
		return ret;
	}
	return 0;
}

static void axp152_irq_work(struct work_struct *work)
{
	struct axp152_data *axp152 =
		container_of(work, struct axp152_data, irq_work);
	int irqs;

	/* read interrupts */
	irqs = axp152_read_interrupts(axp152, AXP152_REG_INTSTS1);
	if (irqs < 0) {
		dev_err(&axp152->client->dev,
			"interrupt read error leaving interrupts disabled\n");
		return;
	}

	/* process interrupts */
	if (irqs & (AXP152_IRQ_PEKLONG | AXP152_IRQ_PEKSHORT)) {
		input_report_key(axp152->input, KEY_POWER, 1);
		input_sync(axp152->input);
		input_report_key(axp152->input, KEY_POWER, 0);
		input_sync(axp152->input);
	}

	/* clear interrupts */
	irqs = axp152_write_interrupts(axp152, AXP152_REG_INTSTS1, irqs);
	if (irqs < 0) {
		dev_err(&axp152->client->dev,
			"interrupt write error leaving interrupts disabled\n");
		return;
	}
	enable_irq(axp152->client->irq);
}

static irqreturn_t axp152_irq_handler(int irq, void *data)
{
	struct axp152_data *axp152 = data;

	disable_irq_nosync(irq);
	schedule_work(&axp152->irq_work);

	return IRQ_HANDLED;
}

static void axp152_power_off(void)
{
	axp152_write_reg(pm_power_axp152, AXP152_REG_POWER,
		pm_power_axp152->regs[AXP152_REG_POWER] | AXP152_POWER_OFF);
}

static int axp152_list_voltage(struct regulator_dev *rdev, unsigned selector)
{
	switch (rdev_get_id(rdev)) {
	case axp152_dcdc1:
		return axp152_dcdc1_mvolt[selector] * 1000;
	case axp152_dcdc2:
	case axp152_dcdc4:
		return 700000 + selector * 25000;
	case axp152_dcdc3:
		return 700000 + selector * 50000;
	case axp152_dldo2:
		return 700000 + selector * 100000;
	default:
		return -EINVAL;
	}
}

static int axp152_set_voltage(struct regulator_dev *rdev, int val)
{
	struct axp152_data *axp152 = rdev_get_drvdata(rdev);
	int reg;

	switch (rdev_get_id(rdev)) {
	case axp152_dcdc1: reg = AXP152_REG_DCDC1; break;
	case axp152_dcdc2: reg = AXP152_REG_DCDC2; break;
	case axp152_dcdc3: reg = AXP152_REG_DCDC3; break;
	case axp152_dcdc4: reg = AXP152_REG_DCDC4; break;
	case axp152_dldo2: reg = AXP152_REG_DLDO2; break;
	default:
		return -EINVAL;
	}

	if (val == -1)
		return axp152->regs[reg];
	else
		return axp152_write_reg(axp152, reg, val);
}

static int axp152_set_output(struct regulator_dev *rdev, int val)
{
	struct axp152_data *axp152 = rdev_get_drvdata(rdev);
	int mask, ret;

	switch (rdev_get_id(rdev)) {
	case axp152_dcdc1: mask = AXP152_DCDC1_ENABLE; break;
	case axp152_dcdc2: mask = AXP152_DCDC2_ENABLE; break;
	case axp152_dcdc3: mask = AXP152_DCDC3_ENABLE; break;
	case axp152_dcdc4: mask = AXP152_DCDC4_ENABLE; break;
	case axp152_dldo2: mask = AXP152_DLDO2_ENABLE; break;
	default:
		return -EINVAL;
	}

	/* This uses 1 register shared by all regulators, so we need to lock */
	mutex_lock(&axp152->mutex);
	if (val == -1)
		ret = (axp152->regs[AXP152_REG_OUTPUT_CTRL] & mask) ? 1 : 0;
	else if (val)
		ret = axp152_write_reg(axp152, AXP152_REG_OUTPUT_CTRL,
			axp152->regs[AXP152_REG_OUTPUT_CTRL] | mask);
	else
		ret = axp152_write_reg(axp152, AXP152_REG_OUTPUT_CTRL,
			axp152->regs[AXP152_REG_OUTPUT_CTRL] & ~mask);
	mutex_unlock(&axp152->mutex);

	return ret;
}

static int axp152_set_voltage_sel(struct regulator_dev *rdev,
				  unsigned selector)
{
	return axp152_set_voltage(rdev, selector);
}

static int axp152_get_voltage_sel(struct regulator_dev *rdev)
{
	return axp152_set_voltage(rdev, -1);
}

static int axp152_enable(struct regulator_dev *rdev)
{
	return axp152_set_output(rdev, 1);
}

static int axp152_disable(struct regulator_dev *rdev)
{
	return axp152_set_output(rdev, 0);
}

static int axp152_is_enabled(struct regulator_dev *rdev)
{
	return axp152_set_output(rdev, -1);
}

static struct regulator_ops axp152_ops = {
	.list_voltage		= axp152_list_voltage,
	.set_voltage_sel	= axp152_set_voltage_sel,
	.get_voltage_sel	= axp152_get_voltage_sel,
	.enable			= axp152_enable,
	.disable		= axp152_disable,
	.is_enabled		= axp152_is_enabled,
};

static struct regulator_desc axp152_desc[AXP152_REGULATOR_COUNT] = {
	{
		.name		= "axp152_dcdc1",
		.id		= axp152_dcdc1,
		.n_voltages	= 16,
		.ops		= &axp152_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	} , {
		.name		= "axp152_dcdc2",
		.id		= axp152_dcdc2,
		.n_voltages	= 64,
		.ops		= &axp152_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	} , {
		.name		= "axp152_dcdc3",
		.id		= axp152_dcdc3,
		.n_voltages	= 57,
		.ops		= &axp152_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	} , {
		.name		= "axp152_dcdc4",
		.id		= axp152_dcdc4,
		.n_voltages	= 113,
		.ops		= &axp152_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	} , {
		.name		= "axp152_dldo2",
		.id		= axp152_dldo2,
		.n_voltages	= 29,
		.ops		= &axp152_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
	}
};

static int axp152_remove(struct i2c_client *client)
{
	struct axp152_data *axp152 = i2c_get_clientdata(client);
	int i;

	mutex_lock(&pm_power_axp152_mutex);
	if (pm_power_axp152 == axp152) {
		pm_power_axp152 = NULL;
		pm_power_off = NULL;
	}
	mutex_unlock(&pm_power_axp152_mutex);

	for (i = 0; i < AXP152_REGULATOR_COUNT; i++)
		if (axp152->regulator[i])
			regulator_unregister(axp152->regulator[i]);

	if (axp152->got_irq)
		free_irq(client->irq, axp152);

	cancel_work_sync(&axp152->irq_work);

	if (axp152->input)
		input_unregister_device(axp152->input);

	i2c_set_clientdata(client, NULL);
	kfree(axp152);
	return 0;
}

static int __devinit axp152_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct regulator_init_data *init_data = client->dev.platform_data;
	struct axp152_data *axp152;
	int i, ret;

	ret = i2c_smbus_read_byte_data(client, AXP152_REG_CHIP_VERSION);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read reg 0x03 (%d)\n", ret);
		return ret;
	}
	if (ret != 0x05) {
		dev_err(&client->dev,
			"unexpected chip-version 0x%02x != 0x05\n", ret);
		return -ENODEV;
	}

	axp152 = kzalloc(sizeof(*axp152), GFP_KERNEL);
	if (!axp152)
		return -ENOMEM;

	mutex_init(&axp152->mutex);
	INIT_WORK(&axp152->irq_work, axp152_irq_work);
	axp152->client = client;
	i2c_set_clientdata(client, axp152);

	/* Cache used registers */
	ret = 0;
	ret |= axp152_read_reg(axp152, AXP152_REG_OUTPUT_CTRL);
	ret |= axp152_read_reg(axp152, AXP152_REG_DCDC1);
	ret |= axp152_read_reg(axp152, AXP152_REG_DCDC2);
	ret |= axp152_read_reg(axp152, AXP152_REG_DCDC3);
	ret |= axp152_read_reg(axp152, AXP152_REG_DCDC4);
	ret |= axp152_read_reg(axp152, AXP152_REG_DLDO2);
	ret |= axp152_read_reg(axp152, AXP152_REG_POWER);

	/* Enable the interrupts we're interested in */
	if (client->irq != -1) {
		ret |= axp152_write_interrupts(axp152, AXP152_REG_INTSTS1,
				AXP152_IRQ_PEKLONG | AXP152_IRQ_PEKSHORT);
		ret |= axp152_write_interrupts(axp152, AXP152_REG_INTEN1,
				AXP152_IRQ_PEKLONG | AXP152_IRQ_PEKSHORT);
	}

	/* Check initialization was successful */
	if (ret) {
		axp152_remove(client);
		return -EIO;
	}

	if (client->irq != -1) {
		axp152->input = input_allocate_device();
		if (!axp152->input) {
			axp152_remove(client);
			return -ENOMEM;
		}

		axp152->input->name = client->name;
		axp152->input->phys = client->adapter->name;
		axp152->input->id.bustype = BUS_I2C;
		axp152->input->dev.parent = &client->dev;
		set_bit(EV_KEY, axp152->input->evbit);
		set_bit(KEY_POWER, axp152->input->keybit);

		ret = input_register_device(axp152->input);
		if (ret) {
			dev_err(&client->dev, "failed to register input\n");
			axp152->input->dev.parent = NULL;
			input_free_device(axp152->input);
			axp152->input = NULL;
			axp152_remove(client);
			return ret;
		}

		ret = request_irq(client->irq, axp152_irq_handler,
				  IRQF_DISABLED, "axp152", axp152);
		if (ret) {
			dev_err(&client->dev, "failed to request irq %d\n",
				client->irq);
			axp152_remove(client);
			return -EIO;
		}
		axp152->got_irq = 1;
	}

	for (i = 0; i < AXP152_REGULATOR_COUNT; i++) {
		axp152->regulator[i] = regulator_register(&axp152_desc[i],
			&client->dev, &init_data[i], axp152, NULL);
		if (IS_ERR(axp152->regulator[i])) {
			dev_err(&client->dev,
				"failed to register axp152 %s regulator\n",
				axp152_desc[i].supply_name);
			axp152->regulator[i] = NULL;
			axp152_remove(client);
			return PTR_ERR(axp152->regulator[i]);
		}
	}

	mutex_lock(&pm_power_axp152_mutex);
	if (!pm_power_axp152) {
		pm_power_axp152 = axp152;
		pm_power_off = axp152_power_off;
	}
	mutex_unlock(&pm_power_axp152_mutex);

	return 0;
}

static struct i2c_driver axp152_driver = {
	.driver	= {
		.name	= "axp152",
		.owner	= THIS_MODULE,
	},
	.probe		= axp152_probe,
	.remove		= axp152_remove,
	.id_table	= axp152_id_table,
};

module_i2c_driver(axp152_driver);


/* Below is Allwinner fex file integraton stuff, not for upstream */

static struct regulator_consumer_supply axp152_dcdc1_supply = {
	.supply = "Vio",
};

static struct regulator_consumer_supply axp152_dcdc2_supply = {
	.supply = "Vcore",
};

static struct regulator_consumer_supply axp152_dcdc3_supply = {
	.supply = "Vddr",
};

static struct regulator_consumer_supply axp152_dcdc4_supply = {
	.supply = "Vcpu",
};

static struct regulator_consumer_supply axp152_dldo2_supply = {
	.supply = "Vdldo2", /* No clue what dldo2 is used for */
};

static struct regulator_init_data regl_init_data[AXP152_REGULATOR_COUNT] = {
	[axp152_dcdc1] = { /* Vio, power on 3.3V, Android hardcoded 3.3V */
		.num_consumer_supplies = 1,
		.consumer_supplies = &axp152_dcdc1_supply,
		.constraints = {
			.min_uV =  3300 * 1000,
			.max_uV =  3300 * 1000,
			.always_on = 1,
		}
	},
	[axp152_dcdc2] = { /* Vcore, power on 1.25V, cpu-freq controlled */
		.num_consumer_supplies = 1,
		.consumer_supplies = &axp152_dcdc2_supply,
		.constraints = {
			.min_uV =  1000 * 1000,
			.max_uV =  1600 * 1000,
			.always_on = 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		}
	},
	[axp152_dcdc3] = { /* Vddr, power on 1.5V, Android from fex */
		.num_consumer_supplies = 1,
		.consumer_supplies = &axp152_dcdc3_supply,
		.constraints = {
			.min_uV =  1500 * 1000,
			.max_uV =  1500 * 1000,
			.always_on = 1,
			.apply_uV = 1,
			.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
		}
	},
	[axp152_dcdc4] = { /* Vcpu, power on 1.25V, Android from fex */
		.num_consumer_supplies = 1,
		.consumer_supplies = &axp152_dcdc4_supply,
		.constraints = {
			.min_uV =  1250 * 1000,
			.max_uV =  1250 * 1000,
			.always_on = 1,
			.apply_uV = 1,
		}
	},
	[axp152_dldo2] = { /* Power on 1.8V, Android hardcoded 3.0V */
		.num_consumer_supplies = 1,
		.consumer_supplies = &axp152_dldo2_supply,
		.constraints = {
			.min_uV =  3000 * 1000,
			.max_uV =  3000 * 1000,
			.always_on = 1,
			.apply_uV = 1,
		}
	},
};

static struct i2c_board_info __initdata axp_mfd_i2c_board_info = {
	.type = "axp152",
	.addr = AXP152_I2C_ADDR,
	.irq  = -1,
	.platform_data = regl_init_data,
};

static int __init axp_board_init(void)
{
	int ret, val, i2c_bus;

	ret = script_parser_fetch("pmu_para", "pmu_used", &val, sizeof(int));
	if (ret) {
		pr_err("Error no pmu_used in pmu_para section of fex\n");
		return -1;
	}
	if (!val)
		return 0;

	ret = script_parser_fetch("pmu_para", "pmu_twi_id",
				  &i2c_bus, sizeof(int));
	if (ret) {
		pr_err("Error no pmu_twi_id in pmu_para section of fex\n");
		return -1;
	}

	ret = script_parser_fetch("pmu_para", "pmu_twi_addr",
				  &val, sizeof(int));
	if (ret) {
		pr_err("Error no pmu_twi_addr in pmu_para section of fex\n");
		return -1;
	}

	if (val != AXP152_I2C_ADDR) {
		if (val == AXP20x_I2C_ADDR) {
			/* Board uses AXP20x, ignore */
			return 0;
		}
		pr_err("Error invalid pmu_twi_addr (%d) in fex\n", val);
		return -1;
	}

	ret = script_parser_fetch("pmu_para", "pmu_irq_id", &val, sizeof(int));
	if (ret == 0)
		axp_mfd_i2c_board_info.irq = val;

	/* Note we ignore the dcdc2_vol key as dcdc2 is set by the dvfs code */

	ret = script_parser_fetch("target", "dcdc3_vol", &val, sizeof(int));
	if (ret == 0) {
		regl_init_data[axp152_dcdc3].constraints.min_uV = val * 1000;
		regl_init_data[axp152_dcdc3].constraints.max_uV = val * 1000;
	}

	ret = script_parser_fetch("target", "dcdc4_vol", &val, sizeof(int));
	if (ret == 0) {
		regl_init_data[axp152_dcdc4].constraints.min_uV = val * 1000;
		regl_init_data[axp152_dcdc4].constraints.max_uV = val * 1000;
	}

	return i2c_register_board_info(i2c_bus, &axp_mfd_i2c_board_info, 1);
}
fs_initcall(axp_board_init);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("axp152 PMIC regulator driver");
MODULE_LICENSE("GPL");
