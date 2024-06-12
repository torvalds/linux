// SPDX-License-Identifier: GPL-2.0
/*
 *  R-Car Gen3 THS thermal sensor driver
 *  Based on rcar_thermal.c and work from Hien Dang and Khiem Nguyen.
 *
 * Copyright (C) 2016 Renesas Electronics Corporation.
 * Copyright (C) 2016 Sang Engineering
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/thermal.h>

#include "thermal_hwmon.h"

/* Register offsets */
#define REG_GEN3_IRQSTR		0x04
#define REG_GEN3_IRQMSK		0x08
#define REG_GEN3_IRQCTL		0x0C
#define REG_GEN3_IRQEN		0x10
#define REG_GEN3_IRQTEMP1	0x14
#define REG_GEN3_IRQTEMP2	0x18
#define REG_GEN3_IRQTEMP3	0x1C
#define REG_GEN3_THCTR		0x20
#define REG_GEN3_TEMP		0x28
#define REG_GEN3_THCODE1	0x50
#define REG_GEN3_THCODE2	0x54
#define REG_GEN3_THCODE3	0x58
#define REG_GEN3_PTAT1		0x5c
#define REG_GEN3_PTAT2		0x60
#define REG_GEN3_PTAT3		0x64
#define REG_GEN3_THSCP		0x68
#define REG_GEN4_THSFMON00	0x180
#define REG_GEN4_THSFMON01	0x184
#define REG_GEN4_THSFMON02	0x188
#define REG_GEN4_THSFMON15	0x1BC
#define REG_GEN4_THSFMON16	0x1C0
#define REG_GEN4_THSFMON17	0x1C4

/* IRQ{STR,MSK,EN} bits */
#define IRQ_TEMP1		BIT(0)
#define IRQ_TEMP2		BIT(1)
#define IRQ_TEMP3		BIT(2)
#define IRQ_TEMPD1		BIT(3)
#define IRQ_TEMPD2		BIT(4)
#define IRQ_TEMPD3		BIT(5)

/* THCTR bits */
#define THCTR_PONM	BIT(6)
#define THCTR_THSST	BIT(0)

/* THSCP bits */
#define THSCP_COR_PARA_VLD	(BIT(15) | BIT(14))

#define CTEMP_MASK	0xFFF

#define MCELSIUS(temp)	((temp) * 1000)
#define GEN3_FUSE_MASK	0xFFF
#define GEN4_FUSE_MASK	0xFFF

#define TSC_MAX_NUM	5

struct rcar_gen3_thermal_priv;

struct rcar_thermal_info {
	int scale;
	int adj_below;
	int adj_above;
	void (*read_fuses)(struct rcar_gen3_thermal_priv *priv);
};

struct equation_set_coef {
	int a;
	int b;
};

struct rcar_gen3_thermal_tsc {
	struct rcar_gen3_thermal_priv *priv;
	void __iomem *base;
	struct thermal_zone_device *zone;
	/* Different coefficients are used depending on a threshold. */
	struct {
		struct equation_set_coef below;
		struct equation_set_coef above;
	} coef;
	int thcode[3];
};

struct rcar_gen3_thermal_priv {
	struct rcar_gen3_thermal_tsc *tscs[TSC_MAX_NUM];
	struct thermal_zone_device_ops ops;
	unsigned int num_tscs;
	int ptat[3];
	int tj_t;
	const struct rcar_thermal_info *info;
};

static inline u32 rcar_gen3_thermal_read(struct rcar_gen3_thermal_tsc *tsc,
					 u32 reg)
{
	return ioread32(tsc->base + reg);
}

static inline void rcar_gen3_thermal_write(struct rcar_gen3_thermal_tsc *tsc,
					   u32 reg, u32 data)
{
	iowrite32(data, tsc->base + reg);
}

/*
 * Linear approximation for temperature
 *
 * [temp] = ((thadj - [reg]) * a) / b + adj
 * [reg] = thadj - ([temp] - adj) * b / a
 *
 * The constants a and b are calculated using two triplets of int values PTAT
 * and THCODE. PTAT and THCODE can either be read from hardware or use hard
 * coded values from the driver. The formula to calculate a and b are taken from
 * the datasheet. Different calculations are needed for a and b depending on
 * if the input variables ([temp] or [reg]) are above or below a threshold. The
 * threshold is also calculated from PTAT and THCODE using formulas from the
 * datasheet.
 *
 * The constant thadj is one of the THCODE values, which one to use depends on
 * the threshold and input value.
 *
 * The constants adj is taken verbatim from the datasheet. Two values exists,
 * which one to use depends on the input value and the calculated threshold.
 * Furthermore different SoC models supported by the driver have different sets
 * of values. The values for each model are stored in the device match data.
 */

static void rcar_gen3_thermal_shared_coefs(struct rcar_gen3_thermal_priv *priv)
{
	priv->tj_t =
		DIV_ROUND_CLOSEST((priv->ptat[1] - priv->ptat[2]) * priv->info->scale,
				  priv->ptat[0] - priv->ptat[2])
		+ priv->info->adj_below;
}
static void rcar_gen3_thermal_tsc_coefs(struct rcar_gen3_thermal_priv *priv,
					struct rcar_gen3_thermal_tsc *tsc)
{
	tsc->coef.below.a = priv->info->scale * (priv->ptat[2] - priv->ptat[1]);
	tsc->coef.above.a = priv->info->scale * (priv->ptat[0] - priv->ptat[1]);

	tsc->coef.below.b = (priv->ptat[2] - priv->ptat[0]) * (tsc->thcode[2] - tsc->thcode[1]);
	tsc->coef.above.b = (priv->ptat[0] - priv->ptat[2]) * (tsc->thcode[1] - tsc->thcode[0]);
}

static int rcar_gen3_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct rcar_gen3_thermal_tsc *tsc = thermal_zone_device_priv(tz);
	struct rcar_gen3_thermal_priv *priv = tsc->priv;
	const struct equation_set_coef *coef;
	int adj, decicelsius, reg, thcode;

	/* Read register and convert to mili Celsius */
	reg = rcar_gen3_thermal_read(tsc, REG_GEN3_TEMP) & CTEMP_MASK;

	if (reg < tsc->thcode[1]) {
		adj = priv->info->adj_below;
		coef = &tsc->coef.below;
		thcode = tsc->thcode[2];
	} else {
		adj = priv->info->adj_above;
		coef = &tsc->coef.above;
		thcode = tsc->thcode[0];
	}

	/*
	 * The dividend can't be grown as it might overflow, instead shorten the
	 * divisor to convert to decidegree Celsius. If we convert after the
	 * division precision is lost as we will scale up from whole degrees
	 * Celsius.
	 */
	decicelsius = DIV_ROUND_CLOSEST(coef->a * (thcode - reg), coef->b / 10);

	/* Guaranteed operating range is -40C to 125C. */

	/* Reporting is done in millidegree Celsius */
	*temp = decicelsius * 100 + adj * 1000;

	return 0;
}

static int rcar_gen3_thermal_mcelsius_to_temp(struct rcar_gen3_thermal_tsc *tsc,
					      int mcelsius)
{
	struct rcar_gen3_thermal_priv *priv = tsc->priv;
	const struct equation_set_coef *coef;
	int adj, celsius, thcode;

	celsius = DIV_ROUND_CLOSEST(mcelsius, 1000);
	if (celsius < priv->tj_t) {
		coef = &tsc->coef.below;
		adj = priv->info->adj_below;
		thcode = tsc->thcode[2];
	} else {
		coef = &tsc->coef.above;
		adj = priv->info->adj_above;
		thcode = tsc->thcode[0];
	}

	return thcode - DIV_ROUND_CLOSEST((celsius - adj) * coef->b, coef->a);
}

static int rcar_gen3_thermal_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct rcar_gen3_thermal_tsc *tsc = thermal_zone_device_priv(tz);
	u32 irqmsk = 0;

	if (low != -INT_MAX) {
		irqmsk |= IRQ_TEMPD1;
		rcar_gen3_thermal_write(tsc, REG_GEN3_IRQTEMP1,
					rcar_gen3_thermal_mcelsius_to_temp(tsc, low));
	}

	if (high != INT_MAX) {
		irqmsk |= IRQ_TEMP2;
		rcar_gen3_thermal_write(tsc, REG_GEN3_IRQTEMP2,
					rcar_gen3_thermal_mcelsius_to_temp(tsc, high));
	}

	rcar_gen3_thermal_write(tsc, REG_GEN3_IRQMSK, irqmsk);

	return 0;
}

static const struct thermal_zone_device_ops rcar_gen3_tz_of_ops = {
	.get_temp	= rcar_gen3_thermal_get_temp,
	.set_trips	= rcar_gen3_thermal_set_trips,
};

static irqreturn_t rcar_gen3_thermal_irq(int irq, void *data)
{
	struct rcar_gen3_thermal_priv *priv = data;
	unsigned int i;
	u32 status;

	for (i = 0; i < priv->num_tscs; i++) {
		status = rcar_gen3_thermal_read(priv->tscs[i], REG_GEN3_IRQSTR);
		rcar_gen3_thermal_write(priv->tscs[i], REG_GEN3_IRQSTR, 0);
		if (status && priv->tscs[i]->zone)
			thermal_zone_device_update(priv->tscs[i]->zone,
						   THERMAL_EVENT_UNSPECIFIED);
	}

	return IRQ_HANDLED;
}

static void rcar_gen3_thermal_read_fuses_gen3(struct rcar_gen3_thermal_priv *priv)
{
	unsigned int i;

	/*
	 * Set the pseudo calibration points with fused values.
	 * PTAT is shared between all TSCs but only fused for the first
	 * TSC while THCODEs are fused for each TSC.
	 */
	priv->ptat[0] = rcar_gen3_thermal_read(priv->tscs[0], REG_GEN3_PTAT1) &
		GEN3_FUSE_MASK;
	priv->ptat[1] = rcar_gen3_thermal_read(priv->tscs[0], REG_GEN3_PTAT2) &
		GEN3_FUSE_MASK;
	priv->ptat[2] = rcar_gen3_thermal_read(priv->tscs[0], REG_GEN3_PTAT3) &
		GEN3_FUSE_MASK;

	for (i = 0; i < priv->num_tscs; i++) {
		struct rcar_gen3_thermal_tsc *tsc = priv->tscs[i];

		tsc->thcode[0] = rcar_gen3_thermal_read(tsc, REG_GEN3_THCODE1) &
			GEN3_FUSE_MASK;
		tsc->thcode[1] = rcar_gen3_thermal_read(tsc, REG_GEN3_THCODE2) &
			GEN3_FUSE_MASK;
		tsc->thcode[2] = rcar_gen3_thermal_read(tsc, REG_GEN3_THCODE3) &
			GEN3_FUSE_MASK;
	}
}

static void rcar_gen3_thermal_read_fuses_gen4(struct rcar_gen3_thermal_priv *priv)
{
	unsigned int i;

	/*
	 * Set the pseudo calibration points with fused values.
	 * PTAT is shared between all TSCs but only fused for the first
	 * TSC while THCODEs are fused for each TSC.
	 */
	priv->ptat[0] = rcar_gen3_thermal_read(priv->tscs[0], REG_GEN4_THSFMON16) &
		GEN4_FUSE_MASK;
	priv->ptat[1] = rcar_gen3_thermal_read(priv->tscs[0], REG_GEN4_THSFMON17) &
		GEN4_FUSE_MASK;
	priv->ptat[2] = rcar_gen3_thermal_read(priv->tscs[0], REG_GEN4_THSFMON15) &
		GEN4_FUSE_MASK;

	for (i = 0; i < priv->num_tscs; i++) {
		struct rcar_gen3_thermal_tsc *tsc = priv->tscs[i];

		tsc->thcode[0] = rcar_gen3_thermal_read(tsc, REG_GEN4_THSFMON01) &
			GEN4_FUSE_MASK;
		tsc->thcode[1] = rcar_gen3_thermal_read(tsc, REG_GEN4_THSFMON02) &
			GEN4_FUSE_MASK;
		tsc->thcode[2] = rcar_gen3_thermal_read(tsc, REG_GEN4_THSFMON00) &
			GEN4_FUSE_MASK;
	}
}

static bool rcar_gen3_thermal_read_fuses(struct rcar_gen3_thermal_priv *priv)
{
	unsigned int i;
	u32 thscp;

	/* If fuses are not set, fallback to pseudo values. */
	thscp = rcar_gen3_thermal_read(priv->tscs[0], REG_GEN3_THSCP);
	if (!priv->info->read_fuses ||
	    (thscp & THSCP_COR_PARA_VLD) != THSCP_COR_PARA_VLD) {
		/* Default THCODE values in case FUSEs are not set. */
		static const int thcodes[TSC_MAX_NUM][3] = {
			{ 3397, 2800, 2221 },
			{ 3393, 2795, 2216 },
			{ 3389, 2805, 2237 },
			{ 3415, 2694, 2195 },
			{ 3356, 2724, 2244 },
		};

		priv->ptat[0] = 2631;
		priv->ptat[1] = 1509;
		priv->ptat[2] = 435;

		for (i = 0; i < priv->num_tscs; i++) {
			struct rcar_gen3_thermal_tsc *tsc = priv->tscs[i];

			tsc->thcode[0] = thcodes[i][0];
			tsc->thcode[1] = thcodes[i][1];
			tsc->thcode[2] = thcodes[i][2];
		}

		return false;
	}

	priv->info->read_fuses(priv);
	return true;
}

static void rcar_gen3_thermal_init(struct rcar_gen3_thermal_priv *priv,
				   struct rcar_gen3_thermal_tsc *tsc)
{
	u32 reg_val;

	reg_val = rcar_gen3_thermal_read(tsc, REG_GEN3_THCTR);
	reg_val &= ~THCTR_PONM;
	rcar_gen3_thermal_write(tsc, REG_GEN3_THCTR, reg_val);

	usleep_range(1000, 2000);

	rcar_gen3_thermal_write(tsc, REG_GEN3_IRQCTL, 0);
	rcar_gen3_thermal_write(tsc, REG_GEN3_IRQMSK, 0);
	if (priv->ops.set_trips)
		rcar_gen3_thermal_write(tsc, REG_GEN3_IRQEN,
					IRQ_TEMPD1 | IRQ_TEMP2);

	reg_val = rcar_gen3_thermal_read(tsc, REG_GEN3_THCTR);
	reg_val |= THCTR_THSST;
	rcar_gen3_thermal_write(tsc, REG_GEN3_THCTR, reg_val);

	usleep_range(1000, 2000);
}

static const struct rcar_thermal_info rcar_m3w_thermal_info = {
	.scale = 157,
	.adj_below = -41,
	.adj_above = 116,
	.read_fuses = rcar_gen3_thermal_read_fuses_gen3,
};

static const struct rcar_thermal_info rcar_gen3_thermal_info = {
	.scale = 167,
	.adj_below = -41,
	.adj_above = 126,
	.read_fuses = rcar_gen3_thermal_read_fuses_gen3,
};

static const struct rcar_thermal_info rcar_gen4_thermal_info = {
	.scale = 167,
	.adj_below = -41,
	.adj_above = 126,
	.read_fuses = rcar_gen3_thermal_read_fuses_gen4,
};

static const struct of_device_id rcar_gen3_thermal_dt_ids[] = {
	{
		.compatible = "renesas,r8a774a1-thermal",
		.data = &rcar_m3w_thermal_info,
	},
	{
		.compatible = "renesas,r8a774b1-thermal",
		.data = &rcar_gen3_thermal_info,
	},
	{
		.compatible = "renesas,r8a774e1-thermal",
		.data = &rcar_gen3_thermal_info,
	},
	{
		.compatible = "renesas,r8a7795-thermal",
		.data = &rcar_gen3_thermal_info,
	},
	{
		.compatible = "renesas,r8a7796-thermal",
		.data = &rcar_m3w_thermal_info,
	},
	{
		.compatible = "renesas,r8a77961-thermal",
		.data = &rcar_m3w_thermal_info,
	},
	{
		.compatible = "renesas,r8a77965-thermal",
		.data = &rcar_gen3_thermal_info,
	},
	{
		.compatible = "renesas,r8a77980-thermal",
		.data = &rcar_gen3_thermal_info,
	},
	{
		.compatible = "renesas,r8a779a0-thermal",
		.data = &rcar_gen3_thermal_info,
	},
	{
		.compatible = "renesas,r8a779f0-thermal",
		.data = &rcar_gen4_thermal_info,
	},
	{
		.compatible = "renesas,r8a779g0-thermal",
		.data = &rcar_gen4_thermal_info,
	},
	{
		.compatible = "renesas,r8a779h0-thermal",
		.data = &rcar_gen4_thermal_info,
	},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_gen3_thermal_dt_ids);

static void rcar_gen3_thermal_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pm_runtime_put(dev);
	pm_runtime_disable(dev);
}

static void rcar_gen3_hwmon_action(void *data)
{
	struct thermal_zone_device *zone = data;

	thermal_remove_hwmon_sysfs(zone);
}

static int rcar_gen3_thermal_request_irqs(struct rcar_gen3_thermal_priv *priv,
					  struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	unsigned int i;
	char *irqname;
	int ret, irq;

	for (i = 0; i < 2; i++) {
		irq = platform_get_irq_optional(pdev, i);
		if (irq < 0)
			return irq;

		irqname = devm_kasprintf(dev, GFP_KERNEL, "%s:ch%d",
					 dev_name(dev), i);
		if (!irqname)
			return -ENOMEM;

		ret = devm_request_threaded_irq(dev, irq, NULL,
						rcar_gen3_thermal_irq,
						IRQF_ONESHOT, irqname, priv);
		if (ret)
			return ret;
	}

	return 0;
}

static int rcar_gen3_thermal_probe(struct platform_device *pdev)
{
	struct rcar_gen3_thermal_priv *priv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct thermal_zone_device *zone;
	unsigned int i;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ops = rcar_gen3_tz_of_ops;

	priv->info = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, priv);

	if (rcar_gen3_thermal_request_irqs(priv, pdev))
		priv->ops.set_trips = NULL;

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	for (i = 0; i < TSC_MAX_NUM; i++) {
		struct rcar_gen3_thermal_tsc *tsc;

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;

		tsc = devm_kzalloc(dev, sizeof(*tsc), GFP_KERNEL);
		if (!tsc) {
			ret = -ENOMEM;
			goto error_unregister;
		}

		tsc->priv = priv;
		tsc->base = devm_ioremap_resource(dev, res);
		if (IS_ERR(tsc->base)) {
			ret = PTR_ERR(tsc->base);
			goto error_unregister;
		}

		priv->tscs[i] = tsc;
	}

	priv->num_tscs = i;

	if (!rcar_gen3_thermal_read_fuses(priv))
		dev_info(dev, "No calibration values fused, fallback to driver values\n");

	rcar_gen3_thermal_shared_coefs(priv);

	for (i = 0; i < priv->num_tscs; i++) {
		struct rcar_gen3_thermal_tsc *tsc = priv->tscs[i];

		rcar_gen3_thermal_init(priv, tsc);
		rcar_gen3_thermal_tsc_coefs(priv, tsc);

		zone = devm_thermal_of_zone_register(dev, i, tsc, &priv->ops);
		if (IS_ERR(zone)) {
			dev_err(dev, "Sensor %u: Can't register thermal zone\n", i);
			ret = PTR_ERR(zone);
			goto error_unregister;
		}
		tsc->zone = zone;

		ret = thermal_add_hwmon_sysfs(tsc->zone);
		if (ret)
			goto error_unregister;

		ret = devm_add_action_or_reset(dev, rcar_gen3_hwmon_action, zone);
		if (ret)
			goto error_unregister;

		ret = thermal_zone_get_num_trips(tsc->zone);
		if (ret < 0)
			goto error_unregister;

		dev_info(dev, "Sensor %u: Loaded %d trip points\n", i, ret);
	}

	if (!priv->num_tscs) {
		ret = -ENODEV;
		goto error_unregister;
	}

	return 0;

error_unregister:
	rcar_gen3_thermal_remove(pdev);

	return ret;
}

static int __maybe_unused rcar_gen3_thermal_resume(struct device *dev)
{
	struct rcar_gen3_thermal_priv *priv = dev_get_drvdata(dev);
	unsigned int i;

	for (i = 0; i < priv->num_tscs; i++) {
		struct rcar_gen3_thermal_tsc *tsc = priv->tscs[i];

		rcar_gen3_thermal_init(priv, tsc);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(rcar_gen3_thermal_pm_ops, NULL,
			 rcar_gen3_thermal_resume);

static struct platform_driver rcar_gen3_thermal_driver = {
	.driver	= {
		.name	= "rcar_gen3_thermal",
		.pm = &rcar_gen3_thermal_pm_ops,
		.of_match_table = rcar_gen3_thermal_dt_ids,
	},
	.probe		= rcar_gen3_thermal_probe,
	.remove_new	= rcar_gen3_thermal_remove,
};
module_platform_driver(rcar_gen3_thermal_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("R-Car Gen3 THS thermal sensor driver");
MODULE_AUTHOR("Wolfram Sang <wsa+renesas@sang-engineering.com>");
