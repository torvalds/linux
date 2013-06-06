/*
 * Battery charger driver for TI's tps65090
 *
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.

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
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/mfd/tps65090.h>

#define TPS65090_REG_INTR_STS	0x00
#define TPS65090_REG_INTR_MASK	0x02
#define TPS65090_REG_CG_CTRL0	0x04
#define TPS65090_REG_CG_CTRL1	0x05
#define TPS65090_REG_CG_CTRL2	0x06
#define TPS65090_REG_CG_CTRL3	0x07
#define TPS65090_REG_CG_CTRL4	0x08
#define TPS65090_REG_CG_CTRL5	0x09
#define TPS65090_REG_CG_STATUS1	0x0a
#define TPS65090_REG_CG_STATUS2	0x0b

#define TPS65090_CHARGER_ENABLE	BIT(0)
#define TPS65090_VACG		BIT(1)
#define TPS65090_NOITERM	BIT(5)

struct tps65090_charger {
	struct	device	*dev;
	int	ac_online;
	int	prev_ac_online;
	int	irq;
	struct power_supply	ac;
	struct tps65090_platform_data *pdata;
};

static enum power_supply_property tps65090_ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int tps65090_low_chrg_current(struct tps65090_charger *charger)
{
	int ret;

	ret = tps65090_write(charger->dev->parent, TPS65090_REG_CG_CTRL5,
			TPS65090_NOITERM);
	if (ret < 0) {
		dev_err(charger->dev, "%s(): error reading in register 0x%x\n",
			__func__, TPS65090_REG_CG_CTRL5);
		return ret;
	}
	return 0;
}

static int tps65090_enable_charging(struct tps65090_charger *charger)
{
	int ret;
	uint8_t ctrl0 = 0;

	ret = tps65090_read(charger->dev->parent, TPS65090_REG_CG_CTRL0,
			    &ctrl0);
	if (ret < 0) {
		dev_err(charger->dev, "%s(): error reading in register 0x%x\n",
				__func__, TPS65090_REG_CG_CTRL0);
		return ret;
	}

	ret = tps65090_write(charger->dev->parent, TPS65090_REG_CG_CTRL0,
				(ctrl0 | TPS65090_CHARGER_ENABLE));
	if (ret < 0) {
		dev_err(charger->dev, "%s(): error writing in register 0x%x\n",
				__func__, TPS65090_REG_CG_CTRL0);
		return ret;
	}
	return 0;
}

static int tps65090_config_charger(struct tps65090_charger *charger)
{
	uint8_t intrmask = 0;
	int ret;

	if (charger->pdata->enable_low_current_chrg) {
		ret = tps65090_low_chrg_current(charger);
		if (ret < 0) {
			dev_err(charger->dev,
				"error configuring low charge current\n");
			return ret;
		}
	}

	/* Enable the VACG interrupt for AC power detect */
	ret = tps65090_read(charger->dev->parent, TPS65090_REG_INTR_MASK,
			    &intrmask);
	if (ret < 0) {
		dev_err(charger->dev, "%s(): error reading in register 0x%x\n",
			__func__, TPS65090_REG_INTR_MASK);
		return ret;
	}

	ret = tps65090_write(charger->dev->parent, TPS65090_REG_INTR_MASK,
			     (intrmask | TPS65090_VACG));
	if (ret < 0) {
		dev_err(charger->dev, "%s(): error writing in register 0x%x\n",
			__func__, TPS65090_REG_CG_CTRL0);
		return ret;
	}

	return 0;
}

static int tps65090_ac_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct tps65090_charger *charger = container_of(psy,
					struct tps65090_charger, ac);

	if (psp == POWER_SUPPLY_PROP_ONLINE) {
		val->intval = charger->ac_online;
		charger->prev_ac_online = charger->ac_online;
		return 0;
	}
	return -EINVAL;
}

static irqreturn_t tps65090_charger_isr(int irq, void *dev_id)
{
	struct tps65090_charger *charger = dev_id;
	int ret;
	uint8_t status1 = 0;
	uint8_t intrsts = 0;

	ret = tps65090_read(charger->dev->parent, TPS65090_REG_CG_STATUS1,
			    &status1);
	if (ret < 0) {
		dev_err(charger->dev, "%s(): Error in reading reg 0x%x\n",
				__func__, TPS65090_REG_CG_STATUS1);
		return IRQ_HANDLED;
	}
	msleep(75);
	ret = tps65090_read(charger->dev->parent, TPS65090_REG_INTR_STS,
			    &intrsts);
	if (ret < 0) {
		dev_err(charger->dev, "%s(): Error in reading reg 0x%x\n",
				__func__, TPS65090_REG_INTR_STS);
		return IRQ_HANDLED;
	}

	if (intrsts & TPS65090_VACG) {
		ret = tps65090_enable_charging(charger);
		if (ret < 0)
			return IRQ_HANDLED;
		charger->ac_online = 1;
	} else {
		charger->ac_online = 0;
	}

	/* Clear interrupts. */
	ret = tps65090_write(charger->dev->parent, TPS65090_REG_INTR_STS, 0x00);
	if (ret < 0) {
		dev_err(charger->dev, "%s(): Error in writing reg 0x%x\n",
				__func__, TPS65090_REG_INTR_STS);
	}

	if (charger->prev_ac_online != charger->ac_online)
		power_supply_changed(&charger->ac);

	return IRQ_HANDLED;
}

#if defined(CONFIG_OF)

#include <linux/of_device.h>

static struct tps65090_platform_data *
		tps65090_parse_dt_charger_data(struct platform_device *pdev)
{
	struct tps65090_platform_data *pdata;
	struct device_node *np = pdev->dev.of_node;
	unsigned int prop;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "Memory alloc for tps65090_pdata failed\n");
		return NULL;
	}

	prop = of_property_read_bool(np, "ti,enable-low-current-chrg");
	pdata->enable_low_current_chrg = prop;

	pdata->irq_base = -1;

	return pdata;

}
#else
static struct tps65090_platform_data *
		tps65090_parse_dt_charger_data(struct platform_device *pdev)
{
	return NULL;
}
#endif

static int tps65090_charger_probe(struct platform_device *pdev)
{
	struct tps65090_charger *cdata;
	struct tps65090_platform_data *pdata;
	uint8_t status1 = 0;
	int ret;
	int irq;

	pdata = dev_get_platdata(pdev->dev.parent);

	if (!pdata && pdev->dev.of_node)
		pdata = tps65090_parse_dt_charger_data(pdev);

	if (!pdata) {
		dev_err(&pdev->dev, "%s():no platform data available\n",
				__func__);
		return -ENODEV;
	}

	cdata = devm_kzalloc(&pdev->dev, sizeof(*cdata), GFP_KERNEL);
	if (!cdata) {
		dev_err(&pdev->dev, "failed to allocate memory status\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, cdata);

	cdata->dev			= &pdev->dev;
	cdata->pdata			= pdata;

	cdata->ac.name			= "tps65090-ac";
	cdata->ac.type			= POWER_SUPPLY_TYPE_MAINS;
	cdata->ac.get_property		= tps65090_ac_get_property;
	cdata->ac.properties		= tps65090_ac_props;
	cdata->ac.num_properties	= ARRAY_SIZE(tps65090_ac_props);
	cdata->ac.supplied_to		= pdata->supplied_to;
	cdata->ac.num_supplicants	= pdata->num_supplicants;

	ret = power_supply_register(&pdev->dev, &cdata->ac);
	if (ret) {
		dev_err(&pdev->dev, "failed: power supply register\n");
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_warn(&pdev->dev, "Unable to get charger irq = %d\n", irq);
		ret = irq;
		goto fail_unregister_supply;
	}

	cdata->irq = irq;

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
		tps65090_charger_isr, 0, "tps65090-charger", cdata);
	if (ret) {
		dev_err(cdata->dev, "Unable to register irq %d err %d\n", irq,
			ret);
		goto fail_free_irq;
	}

	ret = tps65090_config_charger(cdata);
	if (ret < 0) {
		dev_err(&pdev->dev, "charger config failed, err %d\n", ret);
		goto fail_free_irq;
	}

	/* Check for charger presence */
	ret = tps65090_read(cdata->dev->parent, TPS65090_REG_CG_STATUS1,
			&status1);
	if (ret < 0) {
		dev_err(cdata->dev, "%s(): Error in reading reg 0x%x", __func__,
			TPS65090_REG_CG_STATUS1);
		goto fail_free_irq;
	}

	if (status1 != 0) {
		ret = tps65090_enable_charging(cdata);
		if (ret < 0) {
			dev_err(cdata->dev, "error enabling charger\n");
			goto fail_free_irq;
		}
		cdata->ac_online = 1;
		power_supply_changed(&cdata->ac);
	}

	return 0;

fail_free_irq:
	devm_free_irq(cdata->dev, irq, cdata);
fail_unregister_supply:
	power_supply_unregister(&cdata->ac);

	return ret;
}

static int tps65090_charger_remove(struct platform_device *pdev)
{
	struct tps65090_charger *cdata = platform_get_drvdata(pdev);

	devm_free_irq(cdata->dev, cdata->irq, cdata);
	power_supply_unregister(&cdata->ac);

	return 0;
}

static struct of_device_id of_tps65090_charger_match[] = {
	{ .compatible = "ti,tps65090-charger", },
	{ /* end */ }
};

static struct platform_driver tps65090_charger_driver = {
	.driver	= {
		.name	= "tps65090-charger",
		.of_match_table = of_tps65090_charger_match,
		.owner	= THIS_MODULE,
	},
	.probe	= tps65090_charger_probe,
	.remove = tps65090_charger_remove,
};
module_platform_driver(tps65090_charger_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Syed Rafiuddin <srafiuddin@nvidia.com>");
MODULE_DESCRIPTION("tps65090 battery charger driver");
