/*
 * Intel CHT Whiskey Cove PMIC I2C Master driver
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on various non upstream patches to support the CHT Whiskey Cove PMIC:
 * Copyright (C) 2011 - 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/acpi.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power/bq24190_charger.h>
#include <linux/slab.h>

#define CHT_WC_I2C_CTRL			0x5e24
#define CHT_WC_I2C_CTRL_WR		BIT(0)
#define CHT_WC_I2C_CTRL_RD		BIT(1)
#define CHT_WC_I2C_CLIENT_ADDR		0x5e25
#define CHT_WC_I2C_REG_OFFSET		0x5e26
#define CHT_WC_I2C_WRDATA		0x5e27
#define CHT_WC_I2C_RDDATA		0x5e28

#define CHT_WC_EXTCHGRIRQ		0x6e0a
#define CHT_WC_EXTCHGRIRQ_CLIENT_IRQ	BIT(0)
#define CHT_WC_EXTCHGRIRQ_WRITE_IRQ	BIT(1)
#define CHT_WC_EXTCHGRIRQ_READ_IRQ	BIT(2)
#define CHT_WC_EXTCHGRIRQ_NACK_IRQ	BIT(3)
#define CHT_WC_EXTCHGRIRQ_ADAP_IRQMASK	((u8)GENMASK(3, 1))
#define CHT_WC_EXTCHGRIRQ_MSK		0x6e17

struct cht_wc_i2c_adap {
	struct i2c_adapter adapter;
	wait_queue_head_t wait;
	struct irq_chip irqchip;
	struct mutex adap_lock;
	struct mutex irqchip_lock;
	struct regmap *regmap;
	struct irq_domain *irq_domain;
	struct i2c_client *client;
	int client_irq;
	u8 irq_mask;
	u8 old_irq_mask;
	int read_data;
	bool io_error;
	bool done;
};

static irqreturn_t cht_wc_i2c_adap_thread_handler(int id, void *data)
{
	struct cht_wc_i2c_adap *adap = data;
	int ret, reg;

	mutex_lock(&adap->adap_lock);

	/* Read IRQs */
	ret = regmap_read(adap->regmap, CHT_WC_EXTCHGRIRQ, &reg);
	if (ret) {
		dev_err(&adap->adapter.dev, "Error reading extchgrirq reg\n");
		mutex_unlock(&adap->adap_lock);
		return IRQ_NONE;
	}

	reg &= ~adap->irq_mask;

	/* Reads must be acked after reading the received data. */
	ret = regmap_read(adap->regmap, CHT_WC_I2C_RDDATA, &adap->read_data);
	if (ret)
		adap->io_error = true;

	/*
	 * Immediately ack IRQs, so that if new IRQs arrives while we're
	 * handling the previous ones our irq will re-trigger when we're done.
	 */
	ret = regmap_write(adap->regmap, CHT_WC_EXTCHGRIRQ, reg);
	if (ret)
		dev_err(&adap->adapter.dev, "Error writing extchgrirq reg\n");

	if (reg & CHT_WC_EXTCHGRIRQ_ADAP_IRQMASK) {
		adap->io_error |= !!(reg & CHT_WC_EXTCHGRIRQ_NACK_IRQ);
		adap->done = true;
	}

	mutex_unlock(&adap->adap_lock);

	if (reg & CHT_WC_EXTCHGRIRQ_ADAP_IRQMASK)
		wake_up(&adap->wait);

	/*
	 * Do NOT use handle_nested_irq here, the client irq handler will
	 * likely want to do i2c transfers and the i2c controller uses this
	 * interrupt handler as well, so running the client irq handler from
	 * this thread will cause things to lock up.
	 */
	if (reg & CHT_WC_EXTCHGRIRQ_CLIENT_IRQ) {
		/*
		 * generic_handle_irq expects local IRQs to be disabled
		 * as normally it is called from interrupt context.
		 */
		local_irq_disable();
		generic_handle_irq(adap->client_irq);
		local_irq_enable();
	}

	return IRQ_HANDLED;
}

static u32 cht_wc_i2c_adap_master_func(struct i2c_adapter *adap)
{
	/* This i2c adapter only supports SMBUS byte transfers */
	return I2C_FUNC_SMBUS_BYTE_DATA;
}

static int cht_wc_i2c_adap_smbus_xfer(struct i2c_adapter *_adap, u16 addr,
				      unsigned short flags, char read_write,
				      u8 command, int size,
				      union i2c_smbus_data *data)
{
	struct cht_wc_i2c_adap *adap = i2c_get_adapdata(_adap);
	int ret;

	mutex_lock(&adap->adap_lock);
	adap->io_error = false;
	adap->done = false;
	mutex_unlock(&adap->adap_lock);

	ret = regmap_write(adap->regmap, CHT_WC_I2C_CLIENT_ADDR, addr);
	if (ret)
		return ret;

	if (read_write == I2C_SMBUS_WRITE) {
		ret = regmap_write(adap->regmap, CHT_WC_I2C_WRDATA, data->byte);
		if (ret)
			return ret;
	}

	ret = regmap_write(adap->regmap, CHT_WC_I2C_REG_OFFSET, command);
	if (ret)
		return ret;

	ret = regmap_write(adap->regmap, CHT_WC_I2C_CTRL,
			   (read_write == I2C_SMBUS_WRITE) ?
			   CHT_WC_I2C_CTRL_WR : CHT_WC_I2C_CTRL_RD);
	if (ret)
		return ret;

	ret = wait_event_timeout(adap->wait, adap->done, msecs_to_jiffies(30));
	if (ret == 0) {
		/*
		 * The CHT GPIO controller serializes all IRQs, sometimes
		 * causing significant delays, check status manually.
		 */
		cht_wc_i2c_adap_thread_handler(0, adap);
		if (!adap->done)
			return -ETIMEDOUT;
	}

	ret = 0;
	mutex_lock(&adap->adap_lock);
	if (adap->io_error)
		ret = -EIO;
	else if (read_write == I2C_SMBUS_READ)
		data->byte = adap->read_data;
	mutex_unlock(&adap->adap_lock);

	return ret;
}

static const struct i2c_algorithm cht_wc_i2c_adap_algo = {
	.functionality = cht_wc_i2c_adap_master_func,
	.smbus_xfer = cht_wc_i2c_adap_smbus_xfer,
};

/**** irqchip for the client connected to the extchgr i2c adapter ****/
static void cht_wc_i2c_irq_lock(struct irq_data *data)
{
	struct cht_wc_i2c_adap *adap = irq_data_get_irq_chip_data(data);

	mutex_lock(&adap->irqchip_lock);
}

static void cht_wc_i2c_irq_sync_unlock(struct irq_data *data)
{
	struct cht_wc_i2c_adap *adap = irq_data_get_irq_chip_data(data);
	int ret;

	if (adap->irq_mask != adap->old_irq_mask) {
		ret = regmap_write(adap->regmap, CHT_WC_EXTCHGRIRQ_MSK,
				   adap->irq_mask);
		if (ret == 0)
			adap->old_irq_mask = adap->irq_mask;
		else
			dev_err(&adap->adapter.dev, "Error writing EXTCHGRIRQ_MSK\n");
	}

	mutex_unlock(&adap->irqchip_lock);
}

static void cht_wc_i2c_irq_enable(struct irq_data *data)
{
	struct cht_wc_i2c_adap *adap = irq_data_get_irq_chip_data(data);

	adap->irq_mask &= ~CHT_WC_EXTCHGRIRQ_CLIENT_IRQ;
}

static void cht_wc_i2c_irq_disable(struct irq_data *data)
{
	struct cht_wc_i2c_adap *adap = irq_data_get_irq_chip_data(data);

	adap->irq_mask |= CHT_WC_EXTCHGRIRQ_CLIENT_IRQ;
}

static const struct irq_chip cht_wc_i2c_irq_chip = {
	.irq_bus_lock		= cht_wc_i2c_irq_lock,
	.irq_bus_sync_unlock	= cht_wc_i2c_irq_sync_unlock,
	.irq_disable		= cht_wc_i2c_irq_disable,
	.irq_enable		= cht_wc_i2c_irq_enable,
	.name			= "cht_wc_ext_chrg_irq_chip",
};

static const char * const bq24190_suppliers[] = { "fusb302-typec-source" };

static const struct property_entry bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq24190_suppliers),
	PROPERTY_ENTRY_BOOL("omit-battery-class"),
	PROPERTY_ENTRY_BOOL("disable-reset"),
	{ }
};

static struct regulator_consumer_supply fusb302_consumer = {
	.supply = "vbus",
	/* Must match fusb302 dev_name in intel_cht_int33fe.c */
	.dev_name = "i2c-fusb302",
};

static const struct regulator_init_data bq24190_vbus_init_data = {
	.constraints = {
		/* The name is used in intel_cht_int33fe.c do not change. */
		.name = "cht_wc_usb_typec_vbus",
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.consumer_supplies = &fusb302_consumer,
	.num_consumer_supplies = 1,
};

static struct bq24190_platform_data bq24190_pdata = {
	.regulator_init_data = &bq24190_vbus_init_data,
};

static int cht_wc_i2c_adap_i2c_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct cht_wc_i2c_adap *adap;
	struct i2c_board_info board_info = {
		.type = "bq24190",
		.addr = 0x6b,
		.dev_name = "bq24190",
		.properties = bq24190_props,
		.platform_data = &bq24190_pdata,
	};
	int ret, reg, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Error missing irq resource\n");
		return -EINVAL;
	}

	adap = devm_kzalloc(&pdev->dev, sizeof(*adap), GFP_KERNEL);
	if (!adap)
		return -ENOMEM;

	init_waitqueue_head(&adap->wait);
	mutex_init(&adap->adap_lock);
	mutex_init(&adap->irqchip_lock);
	adap->irqchip = cht_wc_i2c_irq_chip;
	adap->regmap = pmic->regmap;
	adap->adapter.owner = THIS_MODULE;
	adap->adapter.class = I2C_CLASS_HWMON;
	adap->adapter.algo = &cht_wc_i2c_adap_algo;
	strlcpy(adap->adapter.name, "PMIC I2C Adapter",
		sizeof(adap->adapter.name));
	adap->adapter.dev.parent = &pdev->dev;

	/* Clear and activate i2c-adapter interrupts, disable client IRQ */
	adap->old_irq_mask = adap->irq_mask = ~CHT_WC_EXTCHGRIRQ_ADAP_IRQMASK;

	ret = regmap_read(adap->regmap, CHT_WC_I2C_RDDATA, &reg);
	if (ret)
		return ret;

	ret = regmap_write(adap->regmap, CHT_WC_EXTCHGRIRQ, ~adap->irq_mask);
	if (ret)
		return ret;

	ret = regmap_write(adap->regmap, CHT_WC_EXTCHGRIRQ_MSK, adap->irq_mask);
	if (ret)
		return ret;

	/* Alloc and register client IRQ */
	adap->irq_domain = irq_domain_add_linear(pdev->dev.of_node, 1,
						 &irq_domain_simple_ops, NULL);
	if (!adap->irq_domain)
		return -ENOMEM;

	adap->client_irq = irq_create_mapping(adap->irq_domain, 0);
	if (!adap->client_irq) {
		ret = -ENOMEM;
		goto remove_irq_domain;
	}

	irq_set_chip_data(adap->client_irq, adap);
	irq_set_chip_and_handler(adap->client_irq, &adap->irqchip,
				 handle_simple_irq);

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					cht_wc_i2c_adap_thread_handler,
					IRQF_ONESHOT, "PMIC I2C Adapter", adap);
	if (ret)
		goto remove_irq_domain;

	i2c_set_adapdata(&adap->adapter, adap);
	ret = i2c_add_adapter(&adap->adapter);
	if (ret)
		goto remove_irq_domain;

	/*
	 * Normally the Whiskey Cove PMIC is paired with a TI bq24292i charger,
	 * connected to this i2c bus, and a max17047 fuel-gauge and a fusb302
	 * USB Type-C controller connected to another i2c bus. In this setup
	 * the max17047 and fusb302 devices are enumerated through an INT33FE
	 * ACPI device. If this device is present register an i2c-client for
	 * the TI bq24292i charger.
	 */
	if (acpi_dev_present("INT33FE", NULL, -1)) {
		board_info.irq = adap->client_irq;
		adap->client = i2c_new_device(&adap->adapter, &board_info);
		if (!adap->client) {
			ret = -ENOMEM;
			goto del_adapter;
		}
	}

	platform_set_drvdata(pdev, adap);
	return 0;

del_adapter:
	i2c_del_adapter(&adap->adapter);
remove_irq_domain:
	irq_domain_remove(adap->irq_domain);
	return ret;
}

static int cht_wc_i2c_adap_i2c_remove(struct platform_device *pdev)
{
	struct cht_wc_i2c_adap *adap = platform_get_drvdata(pdev);

	if (adap->client)
		i2c_unregister_device(adap->client);
	i2c_del_adapter(&adap->adapter);
	irq_domain_remove(adap->irq_domain);

	return 0;
}

static const struct platform_device_id cht_wc_i2c_adap_id_table[] = {
	{ .name = "cht_wcove_ext_chgr" },
	{},
};
MODULE_DEVICE_TABLE(platform, cht_wc_i2c_adap_id_table);

static struct platform_driver cht_wc_i2c_adap_driver = {
	.probe = cht_wc_i2c_adap_i2c_probe,
	.remove = cht_wc_i2c_adap_i2c_remove,
	.driver = {
		.name = "cht_wcove_ext_chgr",
	},
	.id_table = cht_wc_i2c_adap_id_table,
};
module_platform_driver(cht_wc_i2c_adap_driver);

MODULE_DESCRIPTION("Intel CHT Whiskey Cove PMIC I2C Master driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
