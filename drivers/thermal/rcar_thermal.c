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
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

#define THSCR	0x2c
#define THSSR	0x30

/* THSCR */
#define CPTAP	0xf

/* THSSR */
#define CTEMP	0x3f


struct rcar_thermal_priv {
	void __iomem *base;
	struct device *dev;
	spinlock_t lock;
	u32 comp;
};

#define MCELSIUS(temp)			((temp) * 1000)
#define rcar_zone_to_priv(zone)		(zone->devdata)

/*
 *		basic functions
 */
static u32 rcar_thermal_read(struct rcar_thermal_priv *priv, u32 reg)
{
	unsigned long flags;
	u32 ret;

	spin_lock_irqsave(&priv->lock, flags);

	ret = ioread32(priv->base + reg);

	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

#if 0 /* no user at this point */
static void rcar_thermal_write(struct rcar_thermal_priv *priv,
			       u32 reg, u32 data)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	iowrite32(data, priv->base + reg);

	spin_unlock_irqrestore(&priv->lock, flags);
}
#endif

static void rcar_thermal_bset(struct rcar_thermal_priv *priv, u32 reg,
			      u32 mask, u32 data)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&priv->lock, flags);

	val = ioread32(priv->base + reg);
	val &= ~mask;
	val |= (data & mask);
	iowrite32(val, priv->base + reg);

	spin_unlock_irqrestore(&priv->lock, flags);
}

/*
 *		zone device functions
 */
static int rcar_thermal_get_temp(struct thermal_zone_device *zone,
			   unsigned long *temp)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);
	int val, min, max, tmp;

	tmp = -200; /* default */
	while (1) {
		if (priv->comp < 1 || priv->comp > 12) {
			dev_err(priv->dev,
				"THSSR invalid data (%d)\n", priv->comp);
			priv->comp = 4; /* for next thermal */
			return -EINVAL;
		}

		/*
		 * THS comparator offset and the reference temperature
		 *
		 * Comparator	| reference	| Temperature field
		 * offset	| temperature	| measurement
		 *		| (degrees C)	| (degrees C)
		 * -------------+---------------+-------------------
		 *  1		|  -45		|  -45 to  -30
		 *  2		|  -30		|  -30 to  -15
		 *  3		|  -15		|  -15 to    0
		 *  4		|    0		|    0 to  +15
		 *  5		|  +15		|  +15 to  +30
		 *  6		|  +30		|  +30 to  +45
		 *  7		|  +45		|  +45 to  +60
		 *  8		|  +60		|  +60 to  +75
		 *  9		|  +75		|  +75 to  +90
		 * 10		|  +90		|  +90 to +105
		 * 11		| +105		| +105 to +120
		 * 12		| +120		| +120 to +135
		 */

		/* calculate thermal limitation */
		min = (priv->comp * 15) - 60;
		max = min + 15;

		/*
		 * we need to wait 300us after changing comparator offset
		 * to get stable temperature.
		 * see "Usage Notes" on datasheet
		 */
		rcar_thermal_bset(priv, THSCR, CPTAP, priv->comp);
		udelay(300);

		/* calculate current temperature */
		val = rcar_thermal_read(priv, THSSR) & CTEMP;
		val = (val * 5) - 65;

		dev_dbg(priv->dev, "comp/min/max/val = %d/%d/%d/%d\n",
			priv->comp, min, max, val);

		/*
		 * If val is same as min/max, then,
		 * it should try again on next comparator.
		 * But the val might be correct temperature.
		 * Keep it on "tmp" and compare with next val.
		 */
		if (tmp == val)
			break;

		if (val <= min) {
			tmp = min;
			priv->comp--; /* try again */
		} else if (val >= max) {
			tmp = max;
			priv->comp++; /* try again */
		} else {
			tmp = val;
			break;
		}
	}

	*temp = MCELSIUS(tmp);
	return 0;
}

static struct thermal_zone_device_ops rcar_thermal_zone_ops = {
	.get_temp = rcar_thermal_get_temp,
};

/*
 *		platform functions
 */
static int rcar_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *zone;
	struct rcar_thermal_priv *priv;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Could not get platform resource\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "Could not allocate priv\n");
		return -ENOMEM;
	}

	priv->comp = 4; /* basic setup */
	priv->dev = &pdev->dev;
	spin_lock_init(&priv->lock);
	priv->base = devm_ioremap_nocache(&pdev->dev,
					  res->start, resource_size(res));
	if (!priv->base) {
		dev_err(&pdev->dev, "Unable to ioremap thermal register\n");
		return -ENOMEM;
	}

	zone = thermal_zone_device_register("rcar_thermal", 0, 0, priv,
				    &rcar_thermal_zone_ops, NULL, 0, 0);
	if (IS_ERR(zone)) {
		dev_err(&pdev->dev, "thermal zone device is NULL\n");
		return PTR_ERR(zone);
	}

	platform_set_drvdata(pdev, zone);

	dev_info(&pdev->dev, "proved\n");

	return 0;
}

static int rcar_thermal_remove(struct platform_device *pdev)
{
	struct thermal_zone_device *zone = platform_get_drvdata(pdev);

	thermal_zone_device_unregister(zone);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver rcar_thermal_driver = {
	.driver	= {
		.name	= "rcar_thermal",
	},
	.probe		= rcar_thermal_probe,
	.remove		= rcar_thermal_remove,
};
module_platform_driver(rcar_thermal_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("R-Car THS/TSC thermal sensor driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
