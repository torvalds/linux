// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/device.h>
#include <linux/devm-helpers.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/extcon-provider.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regmap.h>

/* I2C addresses of MUIC internal registers */
#define MAX14526_DEVICE_ID	0x00
#define MAX14526_ID		0x02

/* CONTROL_1 register masks */
#define MAX14526_CONTROL_1	0x01
#define   ID_2P2		BIT(6)
#define   ID_620		BIT(5)
#define   ID_200		BIT(4)
#define   VLDO			BIT(3)
#define   SEMREN		BIT(2)
#define   ADC_EN		BIT(1)
#define   CP_EN			BIT(0)

/* CONTROL_2 register masks */
#define MAX14526_CONTROL_2	0x02
#define   INTPOL		BIT(7)
#define   INT_EN		BIT(6)
#define   MIC_LP		BIT(5)
#define   CP_AUD		BIT(4)
#define   CHG_TYPE		BIT(1)
#define   USB_DET_DIS		BIT(0)

/* SW_CONTROL register masks */
#define MAX14526_SW_CONTROL	0x03
#define   SW_DATA		0x00
#define   SW_UART		0x01
#define   SW_AUDIO		0x02
#define   SW_OPEN		0x07

/* INT_STATUS register masks */
#define MAX14526_INT_STAT	0x04
#define   CHGDET		BIT(7)
#define   MR_COMP		BIT(6)
#define   SENDEND		BIT(5)
#define   V_VBUS		BIT(4)

/* STATUS register masks */
#define MAX14526_STATUS		0x05
#define   CPORT			BIT(7)
#define   CHPORT		BIT(6)
#define   C1COMP		BIT(0)

enum max14526_idno_resistance {
	MAX14526_GND,
	MAX14526_24KOHM,
	MAX14526_56KOHM,
	MAX14526_100KOHM,
	MAX14526_130KOHM,
	MAX14526_180KOHM,
	MAX14526_240KOHM,
	MAX14526_330KOHM,
	MAX14526_430KOHM,
	MAX14526_620KOHM,
	MAX14526_910KOHM,
	MAX14526_OPEN
};

enum max14526_field_idx {
	VENDOR_ID, CHIP_REV,		/* DEVID */
	DM, DP,				/* SW_CONTROL */
	MAX14526_N_REGMAP_FIELDS
};

static const struct reg_field max14526_reg_field[MAX14526_N_REGMAP_FIELDS] = {
	[VENDOR_ID] = REG_FIELD(MAX14526_DEVICE_ID,  4, 7),
	[CHIP_REV]  = REG_FIELD(MAX14526_DEVICE_ID,  0, 3),
	[DM]        = REG_FIELD(MAX14526_SW_CONTROL, 0, 2),
	[DP]        = REG_FIELD(MAX14526_SW_CONTROL, 3, 5),
};

struct max14526_data {
	struct i2c_client *client;
	struct extcon_dev *edev;

	struct regmap *regmap;
	struct regmap_field *rfield[MAX14526_N_REGMAP_FIELDS];

	int last_state;
	int cable;
};

enum max14526_muic_modes {
	MAX14526_OTG     = MAX14526_GND, /* no power */
	MAX14526_MHL     = MAX14526_56KOHM, /* no power */
	MAX14526_OTG_Y   = MAX14526_GND | V_VBUS,
	MAX14526_MHL_CHG = MAX14526_GND | V_VBUS | CHGDET,
	MAX14526_NONE    = MAX14526_OPEN,
	MAX14526_USB     = MAX14526_OPEN | V_VBUS,
	MAX14526_CHG     = MAX14526_OPEN | V_VBUS | CHGDET,
};

static const unsigned int max14526_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_FAST,
	EXTCON_DISP_MHL,
	EXTCON_NONE,
};

static int max14526_ap_usb_mode(struct max14526_data *priv)
{
	struct device *dev = &priv->client->dev;
	int ret;

	/* Enable USB Path */
	ret = regmap_field_write(priv->rfield[DM], SW_DATA);
	if (ret)
		return ret;

	ret = regmap_field_write(priv->rfield[DP], SW_DATA);
	if (ret)
		return ret;

	/* Enable 200K, Charger Pump and ADC */
	ret = regmap_write(priv->regmap, MAX14526_CONTROL_1,
			   ID_200 | ADC_EN | CP_EN);
	if (ret)
		return ret;

	dev_dbg(dev, "AP USB mode set\n");

	return 0;
}

static irqreturn_t max14526_interrupt(int irq, void *dev_id)
{
	struct max14526_data *priv = dev_id;
	struct device *dev = &priv->client->dev;
	int state, ret;

	/*
	 * Upon an MUIC IRQ (MUIC_INT_N falls), wait at least 70ms
	 * before reading INT_STAT and STATUS. After the reads,
	 * MUIC_INT_N returns to high (but the INT_STAT and STATUS
	 * contents will be held).
	 */
	msleep(100);

	ret = regmap_read(priv->regmap, MAX14526_INT_STAT, &state);
	if (ret)
		dev_err(dev, "failed to read MUIC state %d\n", ret);

	if (state == priv->last_state)
		return IRQ_HANDLED;

	/* Detach previous device */
	extcon_set_state_sync(priv->edev, priv->cable, false);

	switch (state) {
	case MAX14526_USB:
		priv->cable = EXTCON_USB;
		break;

	case MAX14526_CHG:
		priv->cable = EXTCON_CHG_USB_FAST;
		break;

	case MAX14526_OTG:
	case MAX14526_OTG_Y:
		priv->cable = EXTCON_USB_HOST;
		break;

	case MAX14526_MHL:
	case MAX14526_MHL_CHG:
		priv->cable = EXTCON_DISP_MHL;
		break;

	case MAX14526_NONE:
	default:
		priv->cable = EXTCON_NONE;
		break;
	}

	extcon_set_state_sync(priv->edev, priv->cable, true);

	priv->last_state = state;

	return IRQ_HANDLED;
}

static const struct regmap_config max14526_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX14526_STATUS,
};

static int max14526_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max14526_data *priv;
	int ret, dev_id, rev, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	i2c_set_clientdata(client, priv);

	priv->regmap = devm_regmap_init_i2c(client, &max14526_regmap_config);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap), "cannot allocate regmap\n");

	for (i = 0; i < MAX14526_N_REGMAP_FIELDS; i++) {
		priv->rfield[i] = devm_regmap_field_alloc(dev, priv->regmap,
							  max14526_reg_field[i]);
		if (IS_ERR(priv->rfield[i]))
			return dev_err_probe(dev, PTR_ERR(priv->rfield[i]),
					     "cannot allocate regmap field\n");
	}

	/* Detect if MUIC version is supported */
	ret = regmap_field_read(priv->rfield[VENDOR_ID], &dev_id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read MUIC ID\n");

	regmap_field_read(priv->rfield[CHIP_REV], &rev);

	if (dev_id == MAX14526_ID)
		dev_info(dev, "detected MAX14526 MUIC with id 0x%x, rev 0x%x\n", dev_id, rev);
	else
		dev_err_probe(dev, -EINVAL, "MUIC vendor id 0x%X is not recognized\n", dev_id);

	priv->edev = devm_extcon_dev_allocate(dev, max14526_extcon_cable);
	if (IS_ERR(priv->edev))
		return dev_err_probe(dev, (IS_ERR(priv->edev)),
				     "failed to allocate extcon device\n");

	ret = devm_extcon_dev_register(dev, priv->edev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to register extcon device\n");

	ret = max14526_ap_usb_mode(priv);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to set AP USB mode\n");

	regmap_write_bits(priv->regmap, MAX14526_CONTROL_2, INT_EN, INT_EN);
	regmap_write_bits(priv->regmap, MAX14526_CONTROL_2, USB_DET_DIS, (u32)~USB_DET_DIS);

	ret = devm_request_threaded_irq(dev, client->irq, NULL, &max14526_interrupt,
					IRQF_ONESHOT | IRQF_SHARED, client->name, priv);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register IRQ\n");

	irq_wake_thread(client->irq, priv);

	return 0;
}

static int max14526_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max14526_data *priv = i2c_get_clientdata(client);

	irq_wake_thread(client->irq, priv);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(max14526_pm_ops, NULL, max14526_resume);

static const struct of_device_id max14526_match[] = {
	{ .compatible = "maxim,max14526" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, max14526_match);

static const struct i2c_device_id max14526_id[] = {
	{ "max14526" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max14526_id);

static struct i2c_driver max14526_driver = {
	.driver = {
		.name = "max14526",
		.of_match_table = max14526_match,
		.pm = &max14526_pm_ops,
	},
	.probe = max14526_probe,
	.id_table = max14526_id,
};
module_i2c_driver(max14526_driver);

MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("MAX14526 extcon driver to support MUIC");
MODULE_LICENSE("GPL");
