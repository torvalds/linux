// SPDX-License-Identifier: GPL-2.0
/*
 *  R-Car THS/TSC thermal sensor driver
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

#include "thermal_hwmon.h"

#define IDLE_INTERVAL	5000

#define COMMON_STR	0x00
#define COMMON_ENR	0x04
#define COMMON_INTMSK	0x0c

#define REG_POSNEG	0x20
#define REG_FILONOFF	0x28
#define REG_THSCR	0x2c
#define REG_THSSR	0x30
#define REG_INTCTRL	0x34

/* THSCR */
#define CPCTL	(1 << 12)

/* THSSR */
#define CTEMP	0x3f

struct rcar_thermal_common {
	void __iomem *base;
	struct device *dev;
	struct list_head head;
	spinlock_t lock;
};

struct rcar_thermal_chip {
	unsigned int use_of_thermal : 1;
	unsigned int has_filonoff : 1;
	unsigned int irq_per_ch : 1;
	unsigned int needs_suspend_resume : 1;
	unsigned int nirqs;
	unsigned int ctemp_bands;
};

static const struct rcar_thermal_chip rcar_thermal = {
	.use_of_thermal = 0,
	.has_filonoff = 1,
	.irq_per_ch = 0,
	.needs_suspend_resume = 0,
	.nirqs = 1,
	.ctemp_bands = 1,
};

static const struct rcar_thermal_chip rcar_gen2_thermal = {
	.use_of_thermal = 1,
	.has_filonoff = 1,
	.irq_per_ch = 0,
	.needs_suspend_resume = 0,
	.nirqs = 1,
	.ctemp_bands = 1,
};

static const struct rcar_thermal_chip rcar_gen3_thermal = {
	.use_of_thermal = 1,
	.has_filonoff = 0,
	.irq_per_ch = 1,
	.needs_suspend_resume = 1,
	/*
	 * The Gen3 chip has 3 interrupts, but this driver uses only 2
	 * interrupts to detect a temperature change, rise or fall.
	 */
	.nirqs = 2,
	.ctemp_bands = 2,
};

struct rcar_thermal_priv {
	void __iomem *base;
	struct rcar_thermal_common *common;
	struct thermal_zone_device *zone;
	const struct rcar_thermal_chip *chip;
	struct delayed_work work;
	struct mutex lock;
	struct list_head list;
	int id;
};

#define rcar_thermal_for_each_priv(pos, common)	\
	list_for_each_entry(pos, &common->head, list)

#define MCELSIUS(temp)			((temp) * 1000)
#define rcar_priv_to_dev(priv)		((priv)->common->dev)
#define rcar_has_irq_support(priv)	((priv)->common->base)
#define rcar_id_to_shift(priv)		((priv)->id * 8)

static const struct of_device_id rcar_thermal_dt_ids[] = {
	{
		.compatible = "renesas,rcar-thermal",
		.data = &rcar_thermal,
	},
	{
		.compatible = "renesas,rcar-gen2-thermal",
		 .data = &rcar_gen2_thermal,
	},
	{
		.compatible = "renesas,thermal-r8a774c0",
		.data = &rcar_gen3_thermal,
	},
	{
		.compatible = "renesas,thermal-r8a77970",
		.data = &rcar_gen3_thermal,
	},
	{
		.compatible = "renesas,thermal-r8a77990",
		.data = &rcar_gen3_thermal,
	},
	{
		.compatible = "renesas,thermal-r8a77995",
		.data = &rcar_gen3_thermal,
	},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_thermal_dt_ids);

/*
 *		basic functions
 */
#define rcar_thermal_common_read(c, r) \
	_rcar_thermal_common_read(c, COMMON_ ##r)
static u32 _rcar_thermal_common_read(struct rcar_thermal_common *common,
				     u32 reg)
{
	return ioread32(common->base + reg);
}

#define rcar_thermal_common_write(c, r, d) \
	_rcar_thermal_common_write(c, COMMON_ ##r, d)
static void _rcar_thermal_common_write(struct rcar_thermal_common *common,
				       u32 reg, u32 data)
{
	iowrite32(data, common->base + reg);
}

#define rcar_thermal_common_bset(c, r, m, d) \
	_rcar_thermal_common_bset(c, COMMON_ ##r, m, d)
static void _rcar_thermal_common_bset(struct rcar_thermal_common *common,
				      u32 reg, u32 mask, u32 data)
{
	u32 val;

	val = ioread32(common->base + reg);
	val &= ~mask;
	val |= (data & mask);
	iowrite32(val, common->base + reg);
}

#define rcar_thermal_read(p, r) _rcar_thermal_read(p, REG_ ##r)
static u32 _rcar_thermal_read(struct rcar_thermal_priv *priv, u32 reg)
{
	return ioread32(priv->base + reg);
}

#define rcar_thermal_write(p, r, d) _rcar_thermal_write(p, REG_ ##r, d)
static void _rcar_thermal_write(struct rcar_thermal_priv *priv,
				u32 reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

#define rcar_thermal_bset(p, r, m, d) _rcar_thermal_bset(p, REG_ ##r, m, d)
static void _rcar_thermal_bset(struct rcar_thermal_priv *priv, u32 reg,
			       u32 mask, u32 data)
{
	u32 val;

	val = ioread32(priv->base + reg);
	val &= ~mask;
	val |= (data & mask);
	iowrite32(val, priv->base + reg);
}

/*
 *		zone device functions
 */
static int rcar_thermal_update_temp(struct rcar_thermal_priv *priv)
{
	struct device *dev = rcar_priv_to_dev(priv);
	int old, new, ctemp = -EINVAL;
	unsigned int i;

	mutex_lock(&priv->lock);

	/*
	 * TSC decides a value of CPTAP automatically,
	 * and this is the conditions which validate interrupt.
	 */
	rcar_thermal_bset(priv, THSCR, CPCTL, CPCTL);

	old = ~0;
	for (i = 0; i < 128; i++) {
		/*
		 * we need to wait 300us after changing comparator offset
		 * to get stable temperature.
		 * see "Usage Notes" on datasheet
		 */
		usleep_range(300, 400);

		new = rcar_thermal_read(priv, THSSR) & CTEMP;
		if (new == old) {
			ctemp = new;
			break;
		}
		old = new;
	}

	if (ctemp < 0) {
		dev_err(dev, "thermal sensor was broken\n");
		goto err_out_unlock;
	}

	/*
	 * enable IRQ
	 */
	if (rcar_has_irq_support(priv)) {
		if (priv->chip->has_filonoff)
			rcar_thermal_write(priv, FILONOFF, 0);

		/* enable Rising/Falling edge interrupt */
		rcar_thermal_write(priv, POSNEG,  0x1);
		rcar_thermal_write(priv, INTCTRL, (((ctemp - 0) << 8) |
						   ((ctemp - 1) << 0)));
	}

err_out_unlock:
	mutex_unlock(&priv->lock);

	return ctemp;
}

static int rcar_thermal_get_current_temp(struct rcar_thermal_priv *priv,
					 int *temp)
{
	int ctemp;

	ctemp = rcar_thermal_update_temp(priv);
	if (ctemp < 0)
		return ctemp;

	/* Guaranteed operating range is -45C to 125C. */

	if (priv->chip->ctemp_bands == 1)
		*temp = MCELSIUS((ctemp * 5) - 65);
	else if (ctemp < 24)
		*temp = MCELSIUS(((ctemp * 55) - 720) / 10);
	else
		*temp = MCELSIUS((ctemp * 5) - 60);

	return 0;
}

static int rcar_thermal_get_temp(struct thermal_zone_device *zone, int *temp)
{
	struct rcar_thermal_priv *priv = thermal_zone_device_priv(zone);

	return rcar_thermal_get_current_temp(priv, temp);
}

static struct thermal_zone_device_ops rcar_thermal_zone_ops = {
	.get_temp	= rcar_thermal_get_temp,
};

static struct thermal_trip trips[] = {
	{ .type = THERMAL_TRIP_CRITICAL, .temperature = 90000 }
};

/*
 *		interrupt
 */
#define rcar_thermal_irq_enable(p)	_rcar_thermal_irq_ctrl(p, 1)
#define rcar_thermal_irq_disable(p)	_rcar_thermal_irq_ctrl(p, 0)
static void _rcar_thermal_irq_ctrl(struct rcar_thermal_priv *priv, int enable)
{
	struct rcar_thermal_common *common = priv->common;
	unsigned long flags;
	u32 mask = 0x3 << rcar_id_to_shift(priv); /* enable Rising/Falling */

	if (!rcar_has_irq_support(priv))
		return;

	spin_lock_irqsave(&common->lock, flags);

	rcar_thermal_common_bset(common, INTMSK, mask, enable ? 0 : mask);

	spin_unlock_irqrestore(&common->lock, flags);
}

static void rcar_thermal_work(struct work_struct *work)
{
	struct rcar_thermal_priv *priv;
	int ret;

	priv = container_of(work, struct rcar_thermal_priv, work.work);

	ret = rcar_thermal_update_temp(priv);
	if (ret < 0)
		return;

	rcar_thermal_irq_enable(priv);

	thermal_zone_device_update(priv->zone, THERMAL_EVENT_UNSPECIFIED);
}

static u32 rcar_thermal_had_changed(struct rcar_thermal_priv *priv, u32 status)
{
	struct device *dev = rcar_priv_to_dev(priv);

	status = (status >> rcar_id_to_shift(priv)) & 0x3;

	if (status) {
		dev_dbg(dev, "thermal%d %s%s\n",
			priv->id,
			(status & 0x2) ? "Rising " : "",
			(status & 0x1) ? "Falling" : "");
	}

	return status;
}

static irqreturn_t rcar_thermal_irq(int irq, void *data)
{
	struct rcar_thermal_common *common = data;
	struct rcar_thermal_priv *priv;
	u32 status, mask;

	spin_lock(&common->lock);

	mask	= rcar_thermal_common_read(common, INTMSK);
	status	= rcar_thermal_common_read(common, STR);
	rcar_thermal_common_write(common, STR, 0x000F0F0F & mask);

	spin_unlock(&common->lock);

	status = status & ~mask;

	/*
	 * check the status
	 */
	rcar_thermal_for_each_priv(priv, common) {
		if (rcar_thermal_had_changed(priv, status)) {
			rcar_thermal_irq_disable(priv);
			queue_delayed_work(system_freezable_wq, &priv->work,
					   msecs_to_jiffies(300));
		}
	}

	return IRQ_HANDLED;
}

/*
 *		platform functions
 */
static void rcar_thermal_remove(struct platform_device *pdev)
{
	struct rcar_thermal_common *common = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct rcar_thermal_priv *priv;

	rcar_thermal_for_each_priv(priv, common) {
		rcar_thermal_irq_disable(priv);
		cancel_delayed_work_sync(&priv->work);
		if (priv->chip->use_of_thermal)
			thermal_remove_hwmon_sysfs(priv->zone);
		else
			thermal_zone_device_unregister(priv->zone);
	}

	pm_runtime_put(dev);
	pm_runtime_disable(dev);
}

static int rcar_thermal_probe(struct platform_device *pdev)
{
	struct rcar_thermal_common *common;
	struct rcar_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	const struct rcar_thermal_chip *chip = of_device_get_match_data(dev);
	int mres = 0;
	int i;
	int ret = -ENODEV;
	int idle = IDLE_INTERVAL;
	u32 enr_bits = 0;

	common = devm_kzalloc(dev, sizeof(*common), GFP_KERNEL);
	if (!common)
		return -ENOMEM;

	platform_set_drvdata(pdev, common);

	INIT_LIST_HEAD(&common->head);
	spin_lock_init(&common->lock);
	common->dev = dev;

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	for (i = 0; i < chip->nirqs; i++) {
		int irq;

		ret = platform_get_irq_optional(pdev, i);
		if (ret < 0 && ret != -ENXIO)
			goto error_unregister;
		if (ret > 0)
			irq = ret;
		else
			break;

		if (!common->base) {
			/*
			 * platform has IRQ support.
			 * Then, driver uses common registers
			 * rcar_has_irq_support() will be enabled
			 */
			res = platform_get_resource(pdev, IORESOURCE_MEM,
						    mres++);
			common->base = devm_ioremap_resource(dev, res);
			if (IS_ERR(common->base)) {
				ret = PTR_ERR(common->base);
				goto error_unregister;
			}

			idle = 0; /* polling delay is not needed */
		}

		ret = devm_request_irq(dev, irq, rcar_thermal_irq,
				       IRQF_SHARED, dev_name(dev), common);
		if (ret) {
			dev_err(dev, "irq request failed\n ");
			goto error_unregister;
		}

		/* update ENR bits */
		if (chip->irq_per_ch)
			enr_bits |= 1 << i;
	}

	for (i = 0;; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, mres++);
		if (!res)
			break;

		priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
		if (!priv) {
			ret = -ENOMEM;
			goto error_unregister;
		}

		priv->base = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->base)) {
			ret = PTR_ERR(priv->base);
			goto error_unregister;
		}

		priv->common = common;
		priv->id = i;
		priv->chip = chip;
		mutex_init(&priv->lock);
		INIT_LIST_HEAD(&priv->list);
		INIT_DELAYED_WORK(&priv->work, rcar_thermal_work);
		ret = rcar_thermal_update_temp(priv);
		if (ret < 0)
			goto error_unregister;

		if (chip->use_of_thermal) {
			priv->zone = devm_thermal_of_zone_register(
						dev, i, priv,
						&rcar_thermal_zone_ops);
		} else {
			priv->zone = thermal_zone_device_register_with_trips(
				"rcar_thermal", trips, ARRAY_SIZE(trips), 0, priv,
						&rcar_thermal_zone_ops, NULL, 0,
						idle);

			ret = thermal_zone_device_enable(priv->zone);
			if (ret) {
				thermal_zone_device_unregister(priv->zone);
				priv->zone = ERR_PTR(ret);
			}
		}
		if (IS_ERR(priv->zone)) {
			dev_err(dev, "can't register thermal zone\n");
			ret = PTR_ERR(priv->zone);
			priv->zone = NULL;
			goto error_unregister;
		}

		if (chip->use_of_thermal) {
			ret = thermal_add_hwmon_sysfs(priv->zone);
			if (ret)
				goto error_unregister;
		}

		rcar_thermal_irq_enable(priv);

		list_move_tail(&priv->list, &common->head);

		/* update ENR bits */
		if (!chip->irq_per_ch)
			enr_bits |= 3 << (i * 8);
	}

	if (common->base && enr_bits)
		rcar_thermal_common_write(common, ENR, enr_bits);

	dev_info(dev, "%d sensor probed\n", i);

	return 0;

error_unregister:
	rcar_thermal_remove(pdev);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int rcar_thermal_suspend(struct device *dev)
{
	struct rcar_thermal_common *common = dev_get_drvdata(dev);
	struct rcar_thermal_priv *priv = list_first_entry(&common->head,
							  typeof(*priv), list);

	if (priv->chip->needs_suspend_resume) {
		rcar_thermal_common_write(common, ENR, 0);
		rcar_thermal_irq_disable(priv);
		rcar_thermal_bset(priv, THSCR, CPCTL, 0);
	}

	return 0;
}

static int rcar_thermal_resume(struct device *dev)
{
	struct rcar_thermal_common *common = dev_get_drvdata(dev);
	struct rcar_thermal_priv *priv = list_first_entry(&common->head,
							  typeof(*priv), list);
	int ret;

	if (priv->chip->needs_suspend_resume) {
		ret = rcar_thermal_update_temp(priv);
		if (ret < 0)
			return ret;
		rcar_thermal_irq_enable(priv);
		rcar_thermal_common_write(common, ENR, 0x03);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(rcar_thermal_pm_ops, rcar_thermal_suspend,
			 rcar_thermal_resume);

static struct platform_driver rcar_thermal_driver = {
	.driver	= {
		.name	= "rcar_thermal",
		.pm = &rcar_thermal_pm_ops,
		.of_match_table = rcar_thermal_dt_ids,
	},
	.probe		= rcar_thermal_probe,
	.remove_new	= rcar_thermal_remove,
};
module_platform_driver(rcar_thermal_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("R-Car THS/TSC thermal sensor driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
