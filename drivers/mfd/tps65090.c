/*
 * Core driver for TI TPS65090 PMIC family
 *
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.

 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.

 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tps65090.h>
#include <linux/regmap.h>
#include <linux/err.h>

#define NUM_INT_REG 2
#define TOTAL_NUM_REG 0x18

/* interrupt status registers */
#define TPS65090_INT_STS	0x0
#define TPS65090_INT_STS2	0x1

/* interrupt mask registers */
#define TPS65090_INT_MSK	0x2
#define TPS65090_INT_MSK2	0x3

struct tps65090_irq_data {
	u8		mask_reg;
	u8		mask_pos;
};

#define TPS65090_IRQ(_reg, _mask_pos)		\
	{					\
		.mask_reg	= (_reg),	\
		.mask_pos	= (_mask_pos),	\
	}

static const struct tps65090_irq_data tps65090_irqs[] = {
	[0]		= TPS65090_IRQ(0, 0),
	[1]		= TPS65090_IRQ(0, 1),
	[2]		= TPS65090_IRQ(0, 2),
	[3]		= TPS65090_IRQ(0, 3),
	[4]		= TPS65090_IRQ(0, 4),
	[5]		= TPS65090_IRQ(0, 5),
	[6]		= TPS65090_IRQ(0, 6),
	[7]		= TPS65090_IRQ(0, 7),
	[8]		= TPS65090_IRQ(1, 0),
	[9]		= TPS65090_IRQ(1, 1),
	[10]		= TPS65090_IRQ(1, 2),
	[11]		= TPS65090_IRQ(1, 3),
	[12]		= TPS65090_IRQ(1, 4),
	[13]		= TPS65090_IRQ(1, 5),
	[14]		= TPS65090_IRQ(1, 6),
	[15]		= TPS65090_IRQ(1, 7),
};

static struct mfd_cell tps65090s[] = {
	{
		.name = "tps65090-pmic",
	},
	{
		.name = "tps65090-charger",
	},
};

int tps65090_write(struct device *dev, int reg, uint8_t val)
{
	struct tps65090 *tps = dev_get_drvdata(dev);
	return regmap_write(tps->rmap, reg, val);
}
EXPORT_SYMBOL_GPL(tps65090_write);

int tps65090_read(struct device *dev, int reg, uint8_t *val)
{
	struct tps65090 *tps = dev_get_drvdata(dev);
	unsigned int temp_val;
	int ret;
	ret = regmap_read(tps->rmap, reg, &temp_val);
	if (!ret)
		*val = temp_val;
	return ret;
}
EXPORT_SYMBOL_GPL(tps65090_read);

int tps65090_set_bits(struct device *dev, int reg, uint8_t bit_num)
{
	struct tps65090 *tps = dev_get_drvdata(dev);
	return regmap_update_bits(tps->rmap, reg, BIT(bit_num), ~0u);
}
EXPORT_SYMBOL_GPL(tps65090_set_bits);

int tps65090_clr_bits(struct device *dev, int reg, uint8_t bit_num)
{
	struct tps65090 *tps = dev_get_drvdata(dev);
	return regmap_update_bits(tps->rmap, reg, BIT(bit_num), 0u);
}
EXPORT_SYMBOL_GPL(tps65090_clr_bits);

static void tps65090_irq_lock(struct irq_data *data)
{
	struct tps65090 *tps65090 = irq_data_get_irq_chip_data(data);

	mutex_lock(&tps65090->irq_lock);
}

static void tps65090_irq_mask(struct irq_data *irq_data)
{
	struct tps65090 *tps65090 = irq_data_get_irq_chip_data(irq_data);
	unsigned int __irq = irq_data->hwirq;
	const struct tps65090_irq_data *data = &tps65090_irqs[__irq];

	tps65090_set_bits(tps65090->dev, (TPS65090_INT_MSK + data->mask_reg),
		data->mask_pos);
}

static void tps65090_irq_unmask(struct irq_data *irq_data)
{
	struct tps65090 *tps65090 = irq_data_get_irq_chip_data(irq_data);
	unsigned int __irq = irq_data->irq - tps65090->irq_base;
	const struct tps65090_irq_data *data = &tps65090_irqs[__irq];

	tps65090_clr_bits(tps65090->dev, (TPS65090_INT_MSK + data->mask_reg),
		data->mask_pos);
}

static void tps65090_irq_sync_unlock(struct irq_data *data)
{
	struct tps65090 *tps65090 = irq_data_get_irq_chip_data(data);

	mutex_unlock(&tps65090->irq_lock);
}

static irqreturn_t tps65090_irq(int irq, void *data)
{
	struct tps65090 *tps65090 = data;
	int ret = 0;
	u8 status, mask;
	unsigned long int acks = 0;
	int i;

	for (i = 0; i < NUM_INT_REG; i++) {
		ret = tps65090_read(tps65090->dev, TPS65090_INT_MSK + i, &mask);
		if (ret < 0) {
			dev_err(tps65090->dev,
				"failed to read mask reg [addr:%d]\n",
				TPS65090_INT_MSK + i);
			return IRQ_NONE;
		}
		ret = tps65090_read(tps65090->dev, TPS65090_INT_STS + i,
			&status);
		if (ret < 0) {
			dev_err(tps65090->dev,
				"failed to read status reg [addr:%d]\n",
				 TPS65090_INT_STS + i);
			return IRQ_NONE;
		}
		if (status) {
			/* Ack only those interrupts which are not masked */
			status &= (~mask);
			ret = tps65090_write(tps65090->dev,
					TPS65090_INT_STS + i, status);
			if (ret < 0) {
				dev_err(tps65090->dev,
					"failed to write interrupt status\n");
				return IRQ_NONE;
			}
			acks |= (status << (i * 8));
		}
	}

	for_each_set_bit(i, &acks, ARRAY_SIZE(tps65090_irqs))
		handle_nested_irq(tps65090->irq_base + i);
	return acks ? IRQ_HANDLED : IRQ_NONE;
}

static int __devinit tps65090_irq_init(struct tps65090 *tps65090, int irq,
	int irq_base)
{
	int i, ret;

	if (!irq_base) {
		dev_err(tps65090->dev, "IRQ base not set\n");
		return -EINVAL;
	}

	mutex_init(&tps65090->irq_lock);

	for (i = 0; i < NUM_INT_REG; i++)
		tps65090_write(tps65090->dev, TPS65090_INT_MSK + i, 0xFF);

	for (i = 0; i < NUM_INT_REG; i++)
		tps65090_write(tps65090->dev, TPS65090_INT_STS + i, 0xff);

	tps65090->irq_base = irq_base;
	tps65090->irq_chip.name = "tps65090";
	tps65090->irq_chip.irq_mask = tps65090_irq_mask;
	tps65090->irq_chip.irq_unmask = tps65090_irq_unmask;
	tps65090->irq_chip.irq_bus_lock = tps65090_irq_lock;
	tps65090->irq_chip.irq_bus_sync_unlock = tps65090_irq_sync_unlock;

	for (i = 0; i < ARRAY_SIZE(tps65090_irqs); i++) {
		int __irq = i + tps65090->irq_base;
		irq_set_chip_data(__irq, tps65090);
		irq_set_chip_and_handler(__irq, &tps65090->irq_chip,
					 handle_simple_irq);
		irq_set_nested_thread(__irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(__irq, IRQF_VALID);
#endif
	}

	ret = request_threaded_irq(irq, NULL, tps65090_irq, IRQF_ONESHOT,
				"tps65090", tps65090);
	if (!ret) {
		device_init_wakeup(tps65090->dev, 1);
		enable_irq_wake(irq);
	}

	return ret;
}

static bool is_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg == TPS65090_INT_STS)
		return true;
	else
		return false;
}

static const struct regmap_config tps65090_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TOTAL_NUM_REG,
	.num_reg_defaults_raw = TOTAL_NUM_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = is_volatile_reg,
};

static int __devinit tps65090_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct tps65090_platform_data *pdata = client->dev.platform_data;
	struct tps65090 *tps65090;
	int ret;

	if (!pdata) {
		dev_err(&client->dev, "tps65090 requires platform data\n");
		return -EINVAL;
	}

	tps65090 = devm_kzalloc(&client->dev, sizeof(*tps65090), GFP_KERNEL);
	if (!tps65090) {
		dev_err(&client->dev, "mem alloc for tps65090 failed\n");
		return -ENOMEM;
	}

	tps65090->dev = &client->dev;
	i2c_set_clientdata(client, tps65090);

	if (client->irq) {
		ret = tps65090_irq_init(tps65090, client->irq, pdata->irq_base);
		if (ret) {
			dev_err(&client->dev, "IRQ init failed with err: %d\n",
				ret);
			goto err_exit;
		}
	}

	tps65090->rmap = devm_regmap_init_i2c(client, &tps65090_regmap_config);
	if (IS_ERR(tps65090->rmap)) {
		ret = PTR_ERR(tps65090->rmap);
		dev_err(&client->dev, "regmap_init failed with err: %d\n", ret);
		goto err_irq_exit;
	}

	ret = mfd_add_devices(tps65090->dev, -1, tps65090s,
			      ARRAY_SIZE(tps65090s), NULL, 0, NULL);
	if (ret) {
		dev_err(&client->dev, "add mfd devices failed with err: %d\n",
			ret);
		goto err_irq_exit;
	}

	return 0;

err_irq_exit:
	if (client->irq)
		free_irq(client->irq, tps65090);
err_exit:
	return ret;
}

static int __devexit tps65090_i2c_remove(struct i2c_client *client)
{
	struct tps65090 *tps65090 = i2c_get_clientdata(client);

	mfd_remove_devices(tps65090->dev);
	if (client->irq)
		free_irq(client->irq, tps65090);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tps65090_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	if (client->irq)
		disable_irq(client->irq);
	return 0;
}

static int tps65090_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	if (client->irq)
		enable_irq(client->irq);
	return 0;
}
#endif

static const struct dev_pm_ops tps65090_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tps65090_suspend, tps65090_resume)
};

static const struct i2c_device_id tps65090_id_table[] = {
	{ "tps65090", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps65090_id_table);

static struct i2c_driver tps65090_driver = {
	.driver	= {
		.name	= "tps65090",
		.owner	= THIS_MODULE,
		.pm	= &tps65090_pm_ops,
	},
	.probe		= tps65090_i2c_probe,
	.remove		= __devexit_p(tps65090_i2c_remove),
	.id_table	= tps65090_id_table,
};

static int __init tps65090_init(void)
{
	return i2c_add_driver(&tps65090_driver);
}
subsys_initcall(tps65090_init);

static void __exit tps65090_exit(void)
{
	i2c_del_driver(&tps65090_driver);
}
module_exit(tps65090_exit);

MODULE_DESCRIPTION("TPS65090 core driver");
MODULE_AUTHOR("Venu Byravarasu <vbyravarasu@nvidia.com>");
MODULE_LICENSE("GPL v2");
