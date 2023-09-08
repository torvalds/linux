// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for STMicroelectronics Multi-Function eXpander (STMFX) core
 *
 * Copyright (C) 2019 STMicroelectronics
 * Author(s): Amelie Delaunay <amelie.delaunay@st.com>.
 */
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/mfd/stmfx.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

static bool stmfx_reg_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STMFX_REG_SYS_CTRL:
	case STMFX_REG_IRQ_SRC_EN:
	case STMFX_REG_IRQ_PENDING:
	case STMFX_REG_IRQ_GPI_PENDING1:
	case STMFX_REG_IRQ_GPI_PENDING2:
	case STMFX_REG_IRQ_GPI_PENDING3:
	case STMFX_REG_GPIO_STATE1:
	case STMFX_REG_GPIO_STATE2:
	case STMFX_REG_GPIO_STATE3:
	case STMFX_REG_IRQ_GPI_SRC1:
	case STMFX_REG_IRQ_GPI_SRC2:
	case STMFX_REG_IRQ_GPI_SRC3:
	case STMFX_REG_GPO_SET1:
	case STMFX_REG_GPO_SET2:
	case STMFX_REG_GPO_SET3:
	case STMFX_REG_GPO_CLR1:
	case STMFX_REG_GPO_CLR2:
	case STMFX_REG_GPO_CLR3:
		return true;
	default:
		return false;
	}
}

static bool stmfx_reg_writeable(struct device *dev, unsigned int reg)
{
	return (reg >= STMFX_REG_SYS_CTRL);
}

static const struct regmap_config stmfx_regmap_config = {
	.reg_bits	= 8,
	.reg_stride	= 1,
	.val_bits	= 8,
	.max_register	= STMFX_REG_MAX,
	.volatile_reg	= stmfx_reg_volatile,
	.writeable_reg	= stmfx_reg_writeable,
	.cache_type	= REGCACHE_RBTREE,
};

static const struct resource stmfx_pinctrl_resources[] = {
	DEFINE_RES_IRQ(STMFX_REG_IRQ_SRC_EN_GPIO),
};

static const struct resource stmfx_idd_resources[] = {
	DEFINE_RES_IRQ(STMFX_REG_IRQ_SRC_EN_IDD),
	DEFINE_RES_IRQ(STMFX_REG_IRQ_SRC_EN_ERROR),
};

static const struct resource stmfx_ts_resources[] = {
	DEFINE_RES_IRQ(STMFX_REG_IRQ_SRC_EN_TS_DET),
	DEFINE_RES_IRQ(STMFX_REG_IRQ_SRC_EN_TS_NE),
	DEFINE_RES_IRQ(STMFX_REG_IRQ_SRC_EN_TS_TH),
	DEFINE_RES_IRQ(STMFX_REG_IRQ_SRC_EN_TS_FULL),
	DEFINE_RES_IRQ(STMFX_REG_IRQ_SRC_EN_TS_OVF),
};

static struct mfd_cell stmfx_cells[] = {
	{
		.of_compatible = "st,stmfx-0300-pinctrl",
		.name = "stmfx-pinctrl",
		.resources = stmfx_pinctrl_resources,
		.num_resources = ARRAY_SIZE(stmfx_pinctrl_resources),
	},
	{
		.of_compatible = "st,stmfx-0300-idd",
		.name = "stmfx-idd",
		.resources = stmfx_idd_resources,
		.num_resources = ARRAY_SIZE(stmfx_idd_resources),
	},
	{
		.of_compatible = "st,stmfx-0300-ts",
		.name = "stmfx-ts",
		.resources = stmfx_ts_resources,
		.num_resources = ARRAY_SIZE(stmfx_ts_resources),
	},
};

static u8 stmfx_func_to_mask(u32 func)
{
	u8 mask = 0;

	if (func & STMFX_FUNC_GPIO)
		mask |= STMFX_REG_SYS_CTRL_GPIO_EN;

	if ((func & STMFX_FUNC_ALTGPIO_LOW) || (func & STMFX_FUNC_ALTGPIO_HIGH))
		mask |= STMFX_REG_SYS_CTRL_ALTGPIO_EN;

	if (func & STMFX_FUNC_TS)
		mask |= STMFX_REG_SYS_CTRL_TS_EN;

	if (func & STMFX_FUNC_IDD)
		mask |= STMFX_REG_SYS_CTRL_IDD_EN;

	return mask;
}

int stmfx_function_enable(struct stmfx *stmfx, u32 func)
{
	u32 sys_ctrl;
	u8 mask;
	int ret;

	ret = regmap_read(stmfx->map, STMFX_REG_SYS_CTRL, &sys_ctrl);
	if (ret)
		return ret;

	/*
	 * IDD and TS have priority in STMFX FW, so if IDD and TS are enabled,
	 * ALTGPIO function is disabled by STMFX FW. If IDD or TS is enabled,
	 * the number of aGPIO available decreases. To avoid GPIO management
	 * disturbance, abort IDD or TS function enable in this case.
	 */
	if (((func & STMFX_FUNC_IDD) || (func & STMFX_FUNC_TS)) &&
	    (sys_ctrl & STMFX_REG_SYS_CTRL_ALTGPIO_EN)) {
		dev_err(stmfx->dev, "ALTGPIO function already enabled\n");
		return -EBUSY;
	}

	/* If TS is enabled, aGPIO[3:0] cannot be used */
	if ((func & STMFX_FUNC_ALTGPIO_LOW) &&
	    (sys_ctrl & STMFX_REG_SYS_CTRL_TS_EN)) {
		dev_err(stmfx->dev, "TS in use, aGPIO[3:0] unavailable\n");
		return -EBUSY;
	}

	/* If IDD is enabled, aGPIO[7:4] cannot be used */
	if ((func & STMFX_FUNC_ALTGPIO_HIGH) &&
	    (sys_ctrl & STMFX_REG_SYS_CTRL_IDD_EN)) {
		dev_err(stmfx->dev, "IDD in use, aGPIO[7:4] unavailable\n");
		return -EBUSY;
	}

	mask = stmfx_func_to_mask(func);

	return regmap_update_bits(stmfx->map, STMFX_REG_SYS_CTRL, mask, mask);
}
EXPORT_SYMBOL_GPL(stmfx_function_enable);

int stmfx_function_disable(struct stmfx *stmfx, u32 func)
{
	u8 mask = stmfx_func_to_mask(func);

	return regmap_update_bits(stmfx->map, STMFX_REG_SYS_CTRL, mask, 0);
}
EXPORT_SYMBOL_GPL(stmfx_function_disable);

static void stmfx_irq_bus_lock(struct irq_data *data)
{
	struct stmfx *stmfx = irq_data_get_irq_chip_data(data);

	mutex_lock(&stmfx->lock);
}

static void stmfx_irq_bus_sync_unlock(struct irq_data *data)
{
	struct stmfx *stmfx = irq_data_get_irq_chip_data(data);

	regmap_write(stmfx->map, STMFX_REG_IRQ_SRC_EN, stmfx->irq_src);

	mutex_unlock(&stmfx->lock);
}

static void stmfx_irq_mask(struct irq_data *data)
{
	struct stmfx *stmfx = irq_data_get_irq_chip_data(data);

	stmfx->irq_src &= ~BIT(data->hwirq % 8);
}

static void stmfx_irq_unmask(struct irq_data *data)
{
	struct stmfx *stmfx = irq_data_get_irq_chip_data(data);

	stmfx->irq_src |= BIT(data->hwirq % 8);
}

static struct irq_chip stmfx_irq_chip = {
	.name			= "stmfx-core",
	.irq_bus_lock		= stmfx_irq_bus_lock,
	.irq_bus_sync_unlock	= stmfx_irq_bus_sync_unlock,
	.irq_mask		= stmfx_irq_mask,
	.irq_unmask		= stmfx_irq_unmask,
};

static irqreturn_t stmfx_irq_handler(int irq, void *data)
{
	struct stmfx *stmfx = data;
	unsigned long bits;
	u32 pending, ack;
	int n, ret;

	ret = regmap_read(stmfx->map, STMFX_REG_IRQ_PENDING, &pending);
	if (ret)
		return IRQ_NONE;

	/*
	 * There is no ACK for GPIO, MFX_REG_IRQ_PENDING_GPIO is a logical OR
	 * of MFX_REG_IRQ_GPI _PENDING1/_PENDING2/_PENDING3
	 */
	ack = pending & ~BIT(STMFX_REG_IRQ_SRC_EN_GPIO);
	if (ack) {
		ret = regmap_write(stmfx->map, STMFX_REG_IRQ_ACK, ack);
		if (ret)
			return IRQ_NONE;
	}

	bits = pending;
	for_each_set_bit(n, &bits, STMFX_REG_IRQ_SRC_MAX)
		handle_nested_irq(irq_find_mapping(stmfx->irq_domain, n));

	return IRQ_HANDLED;
}

static int stmfx_irq_map(struct irq_domain *d, unsigned int virq,
			 irq_hw_number_t hwirq)
{
	irq_set_chip_data(virq, d->host_data);
	irq_set_chip_and_handler(virq, &stmfx_irq_chip, handle_simple_irq);
	irq_set_nested_thread(virq, 1);
	irq_set_noprobe(virq);

	return 0;
}

static void stmfx_irq_unmap(struct irq_domain *d, unsigned int virq)
{
	irq_set_chip_and_handler(virq, NULL, NULL);
	irq_set_chip_data(virq, NULL);
}

static const struct irq_domain_ops stmfx_irq_ops = {
	.map	= stmfx_irq_map,
	.unmap	= stmfx_irq_unmap,
};

static void stmfx_irq_exit(struct i2c_client *client)
{
	struct stmfx *stmfx = i2c_get_clientdata(client);
	int hwirq;

	for (hwirq = 0; hwirq < STMFX_REG_IRQ_SRC_MAX; hwirq++)
		irq_dispose_mapping(irq_find_mapping(stmfx->irq_domain, hwirq));

	irq_domain_remove(stmfx->irq_domain);
}

static int stmfx_irq_init(struct i2c_client *client)
{
	struct stmfx *stmfx = i2c_get_clientdata(client);
	u32 irqoutpin = 0, irqtrigger;
	int ret;

	stmfx->irq_domain = irq_domain_add_simple(stmfx->dev->of_node,
						  STMFX_REG_IRQ_SRC_MAX, 0,
						  &stmfx_irq_ops, stmfx);
	if (!stmfx->irq_domain) {
		dev_err(stmfx->dev, "Failed to create IRQ domain\n");
		return -EINVAL;
	}

	if (!of_property_read_bool(stmfx->dev->of_node, "drive-open-drain"))
		irqoutpin |= STMFX_REG_IRQ_OUT_PIN_TYPE;

	irqtrigger = irq_get_trigger_type(client->irq);
	if ((irqtrigger & IRQ_TYPE_EDGE_RISING) ||
	    (irqtrigger & IRQ_TYPE_LEVEL_HIGH))
		irqoutpin |= STMFX_REG_IRQ_OUT_PIN_POL;

	ret = regmap_write(stmfx->map, STMFX_REG_IRQ_OUT_PIN, irqoutpin);
	if (ret)
		goto irq_exit;

	ret = devm_request_threaded_irq(stmfx->dev, client->irq,
					NULL, stmfx_irq_handler,
					irqtrigger | IRQF_ONESHOT,
					"stmfx", stmfx);
	if (ret)
		goto irq_exit;

	stmfx->irq = client->irq;

	return 0;

irq_exit:
	stmfx_irq_exit(client);

	return ret;
}

static int stmfx_chip_reset(struct stmfx *stmfx)
{
	int ret;

	ret = regmap_write(stmfx->map, STMFX_REG_SYS_CTRL,
			   STMFX_REG_SYS_CTRL_SWRST);
	if (ret)
		return ret;

	msleep(STMFX_BOOT_TIME_MS);

	return ret;
}

static int stmfx_chip_init(struct i2c_client *client)
{
	struct stmfx *stmfx = i2c_get_clientdata(client);
	u32 id;
	u8 version[2];
	int ret;

	stmfx->vdd = devm_regulator_get_optional(&client->dev, "vdd");
	ret = PTR_ERR_OR_ZERO(stmfx->vdd);
	if (ret) {
		if (ret == -ENODEV)
			stmfx->vdd = NULL;
		else
			return dev_err_probe(&client->dev, ret, "Failed to get VDD regulator\n");
	}

	if (stmfx->vdd) {
		ret = regulator_enable(stmfx->vdd);
		if (ret) {
			dev_err(&client->dev, "VDD enable failed: %d\n", ret);
			return ret;
		}
	}

	ret = regmap_read(stmfx->map, STMFX_REG_CHIP_ID, &id);
	if (ret) {
		dev_err(&client->dev, "Error reading chip ID: %d\n", ret);
		goto err;
	}

	/*
	 * Check that ID is the complement of the I2C address:
	 * STMFX I2C address follows the 7-bit format (MSB), that's why
	 * client->addr is shifted.
	 *
	 * STMFX_I2C_ADDR|       STMFX         |        Linux
	 *   input pin   | I2C device address  | I2C device address
	 *---------------------------------------------------------
	 *       0       | b: 1000 010x h:0x84 |       0x42
	 *       1       | b: 1000 011x h:0x86 |       0x43
	 */
	if (FIELD_GET(STMFX_REG_CHIP_ID_MASK, ~id) != (client->addr << 1)) {
		dev_err(&client->dev, "Unknown chip ID: %#x\n", id);
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_bulk_read(stmfx->map, STMFX_REG_FW_VERSION_MSB,
			       version, ARRAY_SIZE(version));
	if (ret) {
		dev_err(&client->dev, "Error reading FW version: %d\n", ret);
		goto err;
	}

	dev_info(&client->dev, "STMFX id: %#x, fw version: %x.%02x\n",
		 id, version[0], version[1]);

	ret = stmfx_chip_reset(stmfx);
	if (ret) {
		dev_err(&client->dev, "Failed to reset chip: %d\n", ret);
		goto err;
	}

	return 0;

err:
	if (stmfx->vdd)
		return regulator_disable(stmfx->vdd);

	return ret;
}

static void stmfx_chip_exit(struct i2c_client *client)
{
	struct stmfx *stmfx = i2c_get_clientdata(client);

	regmap_write(stmfx->map, STMFX_REG_IRQ_SRC_EN, 0);
	regmap_write(stmfx->map, STMFX_REG_SYS_CTRL, 0);

	if (stmfx->vdd) {
		int ret;

		ret = regulator_disable(stmfx->vdd);
		if (ret)
			dev_err(&client->dev,
				"Failed to disable vdd regulator: %pe\n",
				ERR_PTR(ret));
	}
}

static int stmfx_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct stmfx *stmfx;
	int ret;

	stmfx = devm_kzalloc(dev, sizeof(*stmfx), GFP_KERNEL);
	if (!stmfx)
		return -ENOMEM;

	i2c_set_clientdata(client, stmfx);

	stmfx->dev = dev;

	stmfx->map = devm_regmap_init_i2c(client, &stmfx_regmap_config);
	if (IS_ERR(stmfx->map)) {
		ret = PTR_ERR(stmfx->map);
		dev_err(dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	mutex_init(&stmfx->lock);

	ret = stmfx_chip_init(client);
	if (ret) {
		if (ret == -ETIMEDOUT)
			return -EPROBE_DEFER;
		return ret;
	}

	if (client->irq < 0) {
		dev_err(dev, "Failed to get IRQ: %d\n", client->irq);
		ret = client->irq;
		goto err_chip_exit;
	}

	ret = stmfx_irq_init(client);
	if (ret)
		goto err_chip_exit;

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				   stmfx_cells, ARRAY_SIZE(stmfx_cells), NULL,
				   0, stmfx->irq_domain);
	if (ret)
		goto err_irq_exit;

	return 0;

err_irq_exit:
	stmfx_irq_exit(client);
err_chip_exit:
	stmfx_chip_exit(client);

	return ret;
}

static void stmfx_remove(struct i2c_client *client)
{
	stmfx_irq_exit(client);

	stmfx_chip_exit(client);
}

static int stmfx_suspend(struct device *dev)
{
	struct stmfx *stmfx = dev_get_drvdata(dev);
	int ret;

	ret = regmap_raw_read(stmfx->map, STMFX_REG_SYS_CTRL,
			      &stmfx->bkp_sysctrl, sizeof(stmfx->bkp_sysctrl));
	if (ret)
		return ret;

	ret = regmap_raw_read(stmfx->map, STMFX_REG_IRQ_OUT_PIN,
			      &stmfx->bkp_irqoutpin,
			      sizeof(stmfx->bkp_irqoutpin));
	if (ret)
		return ret;

	disable_irq(stmfx->irq);

	if (stmfx->vdd)
		return regulator_disable(stmfx->vdd);

	return 0;
}

static int stmfx_resume(struct device *dev)
{
	struct stmfx *stmfx = dev_get_drvdata(dev);
	int ret;

	if (stmfx->vdd) {
		ret = regulator_enable(stmfx->vdd);
		if (ret) {
			dev_err(stmfx->dev,
				"VDD enable failed: %d\n", ret);
			return ret;
		}
	}

	/* Reset STMFX - supply has been stopped during suspend */
	ret = stmfx_chip_reset(stmfx);
	if (ret) {
		dev_err(stmfx->dev, "Failed to reset chip: %d\n", ret);
		return ret;
	}

	ret = regmap_raw_write(stmfx->map, STMFX_REG_SYS_CTRL,
			       &stmfx->bkp_sysctrl, sizeof(stmfx->bkp_sysctrl));
	if (ret)
		return ret;

	ret = regmap_raw_write(stmfx->map, STMFX_REG_IRQ_OUT_PIN,
			       &stmfx->bkp_irqoutpin,
			       sizeof(stmfx->bkp_irqoutpin));
	if (ret)
		return ret;

	ret = regmap_raw_write(stmfx->map, STMFX_REG_IRQ_SRC_EN,
			       &stmfx->irq_src, sizeof(stmfx->irq_src));
	if (ret)
		return ret;

	enable_irq(stmfx->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(stmfx_dev_pm_ops, stmfx_suspend, stmfx_resume);

static const struct of_device_id stmfx_of_match[] = {
	{ .compatible = "st,stmfx-0300", },
	{},
};
MODULE_DEVICE_TABLE(of, stmfx_of_match);

static struct i2c_driver stmfx_driver = {
	.driver = {
		.name = "stmfx-core",
		.of_match_table = stmfx_of_match,
		.pm = pm_sleep_ptr(&stmfx_dev_pm_ops),
	},
	.probe_new = stmfx_probe,
	.remove = stmfx_remove,
};
module_i2c_driver(stmfx_driver);

MODULE_DESCRIPTION("STMFX core driver");
MODULE_AUTHOR("Amelie Delaunay <amelie.delaunay@st.com>");
MODULE_LICENSE("GPL v2");
