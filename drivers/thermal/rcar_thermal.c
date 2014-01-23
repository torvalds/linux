/*
 *  R-Car THS/TSC thermal sensor driver
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

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

struct rcar_thermal_priv {
	void __iomem *base;
	struct rcar_thermal_common *common;
	struct thermal_zone_device *zone;
	struct delayed_work work;
	struct mutex lock;
	struct list_head list;
	int id;
	int ctemp;
};

#define rcar_thermal_for_each_priv(pos, common)	\
	list_for_each_entry(pos, &common->head, list)

#define MCELSIUS(temp)			((temp) * 1000)
#define rcar_zone_to_priv(zone)		((zone)->devdata)
#define rcar_priv_to_dev(priv)		((priv)->common->dev)
#define rcar_has_irq_support(priv)	((priv)->common->base)
#define rcar_id_to_shift(priv)		((priv)->id * 8)

#ifdef DEBUG
# define rcar_force_update_temp(priv)	1
#else
# define rcar_force_update_temp(priv)	0
#endif

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
	int i;
	int ctemp, old, new;
	int ret = -EINVAL;

	mutex_lock(&priv->lock);

	/*
	 * TSC decides a value of CPTAP automatically,
	 * and this is the conditions which validate interrupt.
	 */
	rcar_thermal_bset(priv, THSCR, CPCTL, CPCTL);

	ctemp = 0;
	old = ~0;
	for (i = 0; i < 128; i++) {
		/*
		 * we need to wait 300us after changing comparator offset
		 * to get stable temperature.
		 * see "Usage Notes" on datasheet
		 */
		udelay(300);

		new = rcar_thermal_read(priv, THSSR) & CTEMP;
		if (new == old) {
			ctemp = new;
			break;
		}
		old = new;
	}

	if (!ctemp) {
		dev_err(dev, "thermal sensor was broken\n");
		goto err_out_unlock;
	}

	/*
	 * enable IRQ
	 */
	if (rcar_has_irq_support(priv)) {
		rcar_thermal_write(priv, FILONOFF, 0);

		/* enable Rising/Falling edge interrupt */
		rcar_thermal_write(priv, POSNEG,  0x1);
		rcar_thermal_write(priv, INTCTRL, (((ctemp - 0) << 8) |
						   ((ctemp - 1) << 0)));
	}

	dev_dbg(dev, "thermal%d  %d -> %d\n", priv->id, priv->ctemp, ctemp);

	priv->ctemp = ctemp;
	ret = 0;
err_out_unlock:
	mutex_unlock(&priv->lock);
	return ret;
}

static int rcar_thermal_get_temp(struct thermal_zone_device *zone,
				 unsigned long *temp)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);

	if (!rcar_has_irq_support(priv) || rcar_force_update_temp(priv))
		rcar_thermal_update_temp(priv);

	mutex_lock(&priv->lock);
	*temp =  MCELSIUS((priv->ctemp * 5) - 65);
	mutex_unlock(&priv->lock);

	return 0;
}

static int rcar_thermal_get_trip_type(struct thermal_zone_device *zone,
				      int trip, enum thermal_trip_type *type)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);
	struct device *dev = rcar_priv_to_dev(priv);

	/* see rcar_thermal_get_temp() */
	switch (trip) {
	case 0: /* +90 <= temp */
		*type = THERMAL_TRIP_CRITICAL;
		break;
	default:
		dev_err(dev, "rcar driver trip error\n");
		return -EINVAL;
	}

	return 0;
}

static int rcar_thermal_get_trip_temp(struct thermal_zone_device *zone,
				      int trip, unsigned long *temp)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);
	struct device *dev = rcar_priv_to_dev(priv);

	/* see rcar_thermal_get_temp() */
	switch (trip) {
	case 0: /* +90 <= temp */
		*temp = MCELSIUS(90);
		break;
	default:
		dev_err(dev, "rcar driver trip error\n");
		return -EINVAL;
	}

	return 0;
}

static int rcar_thermal_notify(struct thermal_zone_device *zone,
			       int trip, enum thermal_trip_type type)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);
	struct device *dev = rcar_priv_to_dev(priv);

	switch (type) {
	case THERMAL_TRIP_CRITICAL:
		/* FIXME */
		dev_warn(dev, "Thermal reached to critical temperature\n");
		break;
	default:
		break;
	}

	return 0;
}

static struct thermal_zone_device_ops rcar_thermal_zone_ops = {
	.get_temp	= rcar_thermal_get_temp,
	.get_trip_type	= rcar_thermal_get_trip_type,
	.get_trip_temp	= rcar_thermal_get_trip_temp,
	.notify		= rcar_thermal_notify,
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

	spin_lock_irqsave(&common->lock, flags);

	rcar_thermal_common_bset(common, INTMSK, mask, enable ? 0 : mask);

	spin_unlock_irqrestore(&common->lock, flags);
}

static void rcar_thermal_work(struct work_struct *work)
{
	struct rcar_thermal_priv *priv;

	priv = container_of(work, struct rcar_thermal_priv, work.work);

	rcar_thermal_update_temp(priv);
	rcar_thermal_irq_enable(priv);
	thermal_zone_device_update(priv->zone);
}

static u32 rcar_thermal_had_changed(struct rcar_thermal_priv *priv, u32 status)
{
	struct device *dev = rcar_priv_to_dev(priv);

	status = (status >> rcar_id_to_shift(priv)) & 0x3;

	if (status & 0x3) {
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
	unsigned long flags;
	u32 status, mask;

	spin_lock_irqsave(&common->lock, flags);

	mask	= rcar_thermal_common_read(common, INTMSK);
	status	= rcar_thermal_common_read(common, STR);
	rcar_thermal_common_write(common, STR, 0x000F0F0F & mask);

	spin_unlock_irqrestore(&common->lock, flags);

	status = status & ~mask;

	/*
	 * check the status
	 */
	rcar_thermal_for_each_priv(priv, common) {
		if (rcar_thermal_had_changed(priv, status)) {
			rcar_thermal_irq_disable(priv);
			schedule_delayed_work(&priv->work,
					      msecs_to_jiffies(300));
		}
	}

	return IRQ_HANDLED;
}

/*
 *		platform functions
 */
static int rcar_thermal_probe(struct platform_device *pdev)
{
	struct rcar_thermal_common *common;
	struct rcar_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource *res, *irq;
	int mres = 0;
	int i;
	int ret = -ENODEV;
	int idle = IDLE_INTERVAL;

	common = devm_kzalloc(dev, sizeof(*common), GFP_KERNEL);
	if (!common) {
		dev_err(dev, "Could not allocate common\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&common->head);
	spin_lock_init(&common->lock);
	common->dev = dev;

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq) {
		int ret;

		/*
		 * platform has IRQ support.
		 * Then, drier use common register
		 */

		ret = devm_request_irq(dev, irq->start, rcar_thermal_irq, 0,
				       dev_name(dev), common);
		if (ret) {
			dev_err(dev, "irq request failed\n ");
			return ret;
		}

		/*
		 * rcar_has_irq_support() will be enabled
		 */
		res = platform_get_resource(pdev, IORESOURCE_MEM, mres++);
		common->base = devm_ioremap_resource(dev, res);
		if (IS_ERR(common->base))
			return PTR_ERR(common->base);

		/* enable temperature comparation */
		rcar_thermal_common_write(common, ENR, 0x00030303);

		idle = 0; /* polling delay is not needed */
	}

	for (i = 0;; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, mres++);
		if (!res)
			break;

		priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
		if (!priv) {
			dev_err(dev, "Could not allocate priv\n");
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
		mutex_init(&priv->lock);
		INIT_LIST_HEAD(&priv->list);
		INIT_DELAYED_WORK(&priv->work, rcar_thermal_work);
		rcar_thermal_update_temp(priv);

		priv->zone = thermal_zone_device_register("rcar_thermal",
						1, 0, priv,
						&rcar_thermal_zone_ops, NULL, 0,
						idle);
		if (IS_ERR(priv->zone)) {
			dev_err(dev, "can't register thermal zone\n");
			ret = PTR_ERR(priv->zone);
			goto error_unregister;
		}

		if (rcar_has_irq_support(priv))
			rcar_thermal_irq_enable(priv);

		list_move_tail(&priv->list, &common->head);
	}

	platform_set_drvdata(pdev, common);

	dev_info(dev, "%d sensor probed\n", i);

	return 0;

error_unregister:
	rcar_thermal_for_each_priv(priv, common) {
		thermal_zone_device_unregister(priv->zone);
		if (rcar_has_irq_support(priv))
			rcar_thermal_irq_disable(priv);
	}

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return ret;
}

static int rcar_thermal_remove(struct platform_device *pdev)
{
	struct rcar_thermal_common *common = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	struct rcar_thermal_priv *priv;

	rcar_thermal_for_each_priv(priv, common) {
		thermal_zone_device_unregister(priv->zone);
		if (rcar_has_irq_support(priv))
			rcar_thermal_irq_disable(priv);
	}

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return 0;
}

static const struct of_device_id rcar_thermal_dt_ids[] = {
	{ .compatible = "renesas,rcar-thermal", },
	{},
};
MODULE_DEVICE_TABLE(of, rcar_thermal_dt_ids);

static struct platform_driver rcar_thermal_driver = {
	.driver	= {
		.name	= "rcar_thermal",
		.of_match_table = rcar_thermal_dt_ids,
	},
	.probe		= rcar_thermal_probe,
	.remove		= rcar_thermal_remove,
};
module_platform_driver(rcar_thermal_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R-Car THS/TSC thermal sensor driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
