// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Intel CHT Whiskey Cove PMIC I2C Master driver
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on various non upstream patches to support the CHT Whiskey Cove PMIC:
 * Copyright (C) 2011 - 2014 Intel Corporation. All rights reserved.
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
#include <linux/power/bq25890_charger.h>
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
	if (reg & CHT_WC_EXTCHGRIRQ_CLIENT_IRQ)
		generic_handle_irq_safe(adap->client_irq);

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

/*
 * We are an i2c-adapter which itself is part of an i2c-client. This means that
 * transfers done through us take adapter->bus_lock twice, once for our parent
 * i2c-adapter and once to take our own bus_lock. Lockdep does not like this
 * nested locking, to make lockdep happy in the case of busses with muxes, the
 * i2c-core's i2c_adapter_lock_bus function calls:
 * rt_mutex_lock_nested(&adapter->bus_lock, i2c_adapter_depth(adapter));
 *
 * But i2c_adapter_depth only works when the direct parent of the adapter is
 * another adapter, as it is only meant for muxes. In our case there is an
 * i2c-client and MFD instantiated platform_device in the parent->child chain
 * between the 2 devices.
 *
 * So we override the default i2c_lock_operations and pass a hardcoded
 * depth of 1 to rt_mutex_lock_nested, to make lockdep happy.
 *
 * Note that if there were to be a mux attached to our adapter, this would
 * break things again since the i2c-mux code expects the root-adapter to have
 * a locking depth of 0. But we always have only 1 client directly attached
 * in the form of the Charger IC paired with the CHT Whiskey Cove PMIC.
 */
static void cht_wc_i2c_adap_lock_bus(struct i2c_adapter *adapter,
				 unsigned int flags)
{
	rt_mutex_lock_nested(&adapter->bus_lock, 1);
}

static int cht_wc_i2c_adap_trylock_bus(struct i2c_adapter *adapter,
				   unsigned int flags)
{
	return rt_mutex_trylock(&adapter->bus_lock);
}

static void cht_wc_i2c_adap_unlock_bus(struct i2c_adapter *adapter,
				   unsigned int flags)
{
	rt_mutex_unlock(&adapter->bus_lock);
}

static const struct i2c_lock_operations cht_wc_i2c_adap_lock_ops = {
	.lock_bus =    cht_wc_i2c_adap_lock_bus,
	.trylock_bus = cht_wc_i2c_adap_trylock_bus,
	.unlock_bus =  cht_wc_i2c_adap_unlock_bus,
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

/********** GPD Win / Pocket charger IC settings **********/
static const char * const bq24190_suppliers[] = {
	"tcpm-source-psy-i2c-fusb302" };

static const struct property_entry bq24190_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", bq24190_suppliers),
	PROPERTY_ENTRY_BOOL("omit-battery-class"),
	PROPERTY_ENTRY_BOOL("disable-reset"),
	{ }
};

static const struct software_node bq24190_node = {
	.properties = bq24190_props,
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

static struct i2c_board_info gpd_win_board_info = {
	.type = "bq24190",
	.addr = 0x6b,
	.dev_name = "bq24190",
	.swnode = &bq24190_node,
	.platform_data = &bq24190_pdata,
};

/********** Xiaomi Mi Pad 2 charger IC settings  **********/
static struct regulator_consumer_supply bq2589x_vbus_consumer = {
	.supply = "vbus",
	.dev_name = "cht_wcove_pwrsrc",
};

static const struct regulator_init_data bq2589x_vbus_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.consumer_supplies = &bq2589x_vbus_consumer,
	.num_consumer_supplies = 1,
};

static struct bq25890_platform_data bq2589x_pdata = {
	.regulator_init_data = &bq2589x_vbus_init_data,
};

static const struct property_entry xiaomi_mipad2_props[] = {
	PROPERTY_ENTRY_BOOL("linux,skip-reset"),
	PROPERTY_ENTRY_BOOL("linux,read-back-settings"),
	{ }
};

static const struct software_node xiaomi_mipad2_node = {
	.properties = xiaomi_mipad2_props,
};

static struct i2c_board_info xiaomi_mipad2_board_info = {
	.type = "bq25890",
	.addr = 0x6a,
	.dev_name = "bq25890",
	.swnode = &xiaomi_mipad2_node,
	.platform_data = &bq2589x_pdata,
};

/********** Lenovo Yogabook YB1-X90F/-X91F/-X91L charger settings **********/
static const char * const lenovo_yb1_bq25892_suppliers[] = { "cht_wcove_pwrsrc" };

static const struct property_entry lenovo_yb1_bq25892_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from",
				    lenovo_yb1_bq25892_suppliers),
	PROPERTY_ENTRY_U32("linux,pump-express-vbus-max", 12000000),
	PROPERTY_ENTRY_BOOL("linux,skip-reset"),
	/*
	 * The firmware sets everything to the defaults, which leads to a
	 * somewhat low charge-current of 2048mA and worse to a battery-voltage
	 * of 4.2V instead of 4.35V (when booted without a charger connected).
	 * Use our own values instead of "linux,read-back-settings" to fix this.
	 */
	PROPERTY_ENTRY_U32("ti,charge-current", 4224000),
	PROPERTY_ENTRY_U32("ti,battery-regulation-voltage", 4352000),
	PROPERTY_ENTRY_U32("ti,termination-current", 256000),
	PROPERTY_ENTRY_U32("ti,precharge-current", 128000),
	PROPERTY_ENTRY_U32("ti,minimum-sys-voltage", 3500000),
	PROPERTY_ENTRY_U32("ti,boost-voltage", 4998000),
	PROPERTY_ENTRY_U32("ti,boost-max-current", 1400000),
	PROPERTY_ENTRY_BOOL("ti,use-ilim-pin"),
	{ }
};

static const struct software_node lenovo_yb1_bq25892_node = {
	.properties = lenovo_yb1_bq25892_props,
};

static struct i2c_board_info lenovo_yogabook1_board_info = {
	.type = "bq25892",
	.addr = 0x6b,
	.dev_name = "bq25892",
	.swnode = &lenovo_yb1_bq25892_node,
	.platform_data = &bq2589x_pdata,
};

/********** Lenovo Yogabook YT3-X90F charger settings **********/
static const char * const lenovo_yt3_bq25892_1_suppliers[] = { "cht_wcove_pwrsrc" };

/*
 * bq25892 charger settings for the round li-ion cells in the hinge,
 * this is the main / biggest battery.
 */
static const struct property_entry lenovo_yt3_bq25892_1_props[] = {
	PROPERTY_ENTRY_STRING_ARRAY("supplied-from", lenovo_yt3_bq25892_1_suppliers),
	PROPERTY_ENTRY_STRING("linux,secondary-charger-name", "bq25890-charger-0"),
	PROPERTY_ENTRY_U32("linux,iinlim-percentage", 60),
	PROPERTY_ENTRY_U32("linux,pump-express-vbus-max", 12000000),
	PROPERTY_ENTRY_BOOL("linux,skip-reset"),
	/*
	 * The firmware sets everything to the defaults, leading to a low(ish)
	 * charge-current and battery-voltage of 2048mA resp 4.2V. Use the
	 * Android values instead of "linux,read-back-settings" to fix this.
	 */
	PROPERTY_ENTRY_U32("ti,charge-current", 3072000),
	PROPERTY_ENTRY_U32("ti,battery-regulation-voltage", 4352000),
	PROPERTY_ENTRY_U32("ti,termination-current", 128000),
	PROPERTY_ENTRY_U32("ti,precharge-current", 128000),
	PROPERTY_ENTRY_U32("ti,minimum-sys-voltage", 3700000),
	PROPERTY_ENTRY_BOOL("ti,use-ilim-pin"),
	/* Set 5V boost current-limit to 1.2A (MAX/POR values are 2.45A/1.4A) */
	PROPERTY_ENTRY_U32("ti,boost-voltage", 4998000),
	PROPERTY_ENTRY_U32("ti,boost-max-current", 1200000),
	{ }
};

static const struct software_node lenovo_yt3_bq25892_1_node = {
	.properties = lenovo_yt3_bq25892_1_props,
};

/* bq25892 charger for the round li-ion cells in the hinge */
static struct i2c_board_info lenovo_yoga_tab3_board_info = {
	.type = "bq25892",
	.addr = 0x6b,
	.dev_name = "bq25892_1",
	.swnode = &lenovo_yt3_bq25892_1_node,
	.platform_data = &bq2589x_pdata,
};

static int cht_wc_i2c_adap_i2c_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct i2c_board_info *board_info = NULL;
	struct cht_wc_i2c_adap *adap;
	int ret, reg, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

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
	adap->adapter.lock_ops = &cht_wc_i2c_adap_lock_ops;
	strscpy(adap->adapter.name, "PMIC I2C Adapter",
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
	adap->irq_domain = irq_domain_add_linear(NULL, 1, &irq_domain_simple_ops, NULL);
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

	switch (pmic->cht_wc_model) {
	case INTEL_CHT_WC_GPD_WIN_POCKET:
		board_info = &gpd_win_board_info;
		break;
	case INTEL_CHT_WC_XIAOMI_MIPAD2:
		board_info = &xiaomi_mipad2_board_info;
		break;
	case INTEL_CHT_WC_LENOVO_YOGABOOK1:
		board_info = &lenovo_yogabook1_board_info;
		break;
	case INTEL_CHT_WC_LENOVO_YT3_X90:
		board_info = &lenovo_yoga_tab3_board_info;
		break;
	default:
		dev_warn(&pdev->dev, "Unknown model, not instantiating charger device\n");
		break;
	}

	if (board_info) {
		board_info->irq = adap->client_irq;
		adap->client = i2c_new_client_device(&adap->adapter, board_info);
		if (IS_ERR(adap->client)) {
			ret = PTR_ERR(adap->client);
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

static void cht_wc_i2c_adap_i2c_remove(struct platform_device *pdev)
{
	struct cht_wc_i2c_adap *adap = platform_get_drvdata(pdev);

	i2c_unregister_device(adap->client);
	i2c_del_adapter(&adap->adapter);
	irq_domain_remove(adap->irq_domain);
}

static const struct platform_device_id cht_wc_i2c_adap_id_table[] = {
	{ .name = "cht_wcove_ext_chgr" },
	{},
};
MODULE_DEVICE_TABLE(platform, cht_wc_i2c_adap_id_table);

static struct platform_driver cht_wc_i2c_adap_driver = {
	.probe = cht_wc_i2c_adap_i2c_probe,
	.remove_new = cht_wc_i2c_adap_i2c_remove,
	.driver = {
		.name = "cht_wcove_ext_chgr",
	},
	.id_table = cht_wc_i2c_adap_id_table,
};
module_platform_driver(cht_wc_i2c_adap_driver);

MODULE_DESCRIPTION("Intel CHT Whiskey Cove PMIC I2C Master driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
