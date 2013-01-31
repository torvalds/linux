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
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/thermal.h>

#define IDLE_INTERVAL	5000

#define THSCR	0x2c
#define THSSR	0x30

/* THSCR */
#define CPCTL	(1 << 12)

/* THSSR */
#define CTEMP	0x3f


struct rcar_thermal_priv {
	void __iomem *base;
	struct device *dev;
	struct mutex lock;
};

#define MCELSIUS(temp)			((temp) * 1000)
#define rcar_zone_to_priv(zone)		((zone)->devdata)
#define rcar_priv_to_dev(priv)		((priv)->dev)

/*
 *		basic functions
 */
static u32 rcar_thermal_read(struct rcar_thermal_priv *priv, u32 reg)
{
	return ioread32(priv->base + reg);
}

#if 0 /* no user at this point */
static void rcar_thermal_write(struct rcar_thermal_priv *priv,
			       u32 reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}
#endif

static void rcar_thermal_bset(struct rcar_thermal_priv *priv, u32 reg,
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
static int rcar_thermal_get_temp(struct thermal_zone_device *zone,
			   unsigned long *temp)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);
	struct device *dev = rcar_priv_to_dev(priv);
	int i;
	int ctemp, old, new;

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
		return -EINVAL;
	}

	*temp = MCELSIUS((ctemp * 5) - 65);

	mutex_unlock(&priv->lock);

	return 0;
}

static int rcar_thermal_get_trip_type(struct thermal_zone_device *zone,
				      int trip, enum thermal_trip_type *type)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);

	/* see rcar_thermal_get_temp() */
	switch (trip) {
	case 0: /* +90 <= temp */
		*type = THERMAL_TRIP_CRITICAL;
		break;
	default:
		dev_err(priv->dev, "rcar driver trip error\n");
		return -EINVAL;
	}

	return 0;
}

static int rcar_thermal_get_trip_temp(struct thermal_zone_device *zone,
				      int trip, unsigned long *temp)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);

	/* see rcar_thermal_get_temp() */
	switch (trip) {
	case 0: /* +90 <= temp */
		*temp = MCELSIUS(90);
		break;
	default:
		dev_err(priv->dev, "rcar driver trip error\n");
		return -EINVAL;
	}

	return 0;
}

static int rcar_thermal_notify(struct thermal_zone_device *zone,
			       int trip, enum thermal_trip_type type)
{
	struct rcar_thermal_priv *priv = rcar_zone_to_priv(zone);

	switch (type) {
	case THERMAL_TRIP_CRITICAL:
		/* FIXME */
		dev_warn(priv->dev,
			 "Thermal reached to critical temperature\n");
		machine_power_off();
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

	priv->dev = &pdev->dev;
	mutex_init(&priv->lock);
	priv->base = devm_ioremap_nocache(&pdev->dev,
					  res->start, resource_size(res));
	if (!priv->base) {
		dev_err(&pdev->dev, "Unable to ioremap thermal register\n");
		return -ENOMEM;
	}

	zone = thermal_zone_device_register("rcar_thermal", 1, 0, priv,
					    &rcar_thermal_zone_ops, NULL, 0,
					    IDLE_INTERVAL);
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
