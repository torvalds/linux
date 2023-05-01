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
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sys_soc.h>
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
#define REG_GEN3_CTSR		0x20
#define REG_GEN3_THCTR		0x20
#define REG_GEN3_TEMP		0x28
#define REG_GEN3_THCODE1	0x50
#define REG_GEN3_THCODE2	0x54
#define REG_GEN3_THCODE3	0x58
#define REG_GEN3_PTAT1		0x5c
#define REG_GEN3_PTAT2		0x60
#define REG_GEN3_PTAT3		0x64
#define REG_GEN3_THSCP		0x68

/* IRQ{STR,MSK,EN} bits */
#define IRQ_TEMP1		BIT(0)
#define IRQ_TEMP2		BIT(1)
#define IRQ_TEMP3		BIT(2)
#define IRQ_TEMPD1		BIT(3)
#define IRQ_TEMPD2		BIT(4)
#define IRQ_TEMPD3		BIT(5)

/* CTSR bits */
#define CTSR_PONM	BIT(8)
#define CTSR_AOUT	BIT(7)
#define CTSR_THBGR	BIT(5)
#define CTSR_VMEN	BIT(4)
#define CTSR_VMST	BIT(1)
#define CTSR_THSST	BIT(0)

/* THCTR bits */
#define THCTR_PONM	BIT(6)
#define THCTR_THSST	BIT(0)

/* THSCP bits */
#define THSCP_COR_PARA_VLD	(BIT(15) | BIT(14))

#define CTEMP_MASK	0xFFF

#define MCELSIUS(temp)	((temp) * 1000)
#define GEN3_FUSE_MASK	0xFFF

#define TSC_MAX_NUM	5

/* Structure for thermal temperature calculation */
struct equation_coefs {
	int a1;
	int b1;
	int a2;
	int b2;
};

struct rcar_gen3_thermal_tsc {
	void __iomem *base;
	struct thermal_zone_device *zone;
	struct equation_coefs coef;
	int tj_t;
	int thcode[3];
};

struct rcar_gen3_thermal_priv {
	struct rcar_gen3_thermal_tsc *tscs[TSC_MAX_NUM];
	struct thermal_zone_device_ops ops;
	unsigned int num_tscs;
	void (*thermal_init)(struct rcar_gen3_thermal_priv *priv,
			     struct rcar_gen3_thermal_tsc *tsc);
	int ptat[3];
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
 * [reg] = [temp] * a + b => [temp] = ([reg] - b) / a
 *
 * The constants a and b are calculated using two triplets of int values PTAT
 * and THCODE. PTAT and THCODE can either be read from hardware or use hard
 * coded values from driver. The formula to calculate a and b are taken from
 * BSP and sparsely documented and understood.
 *
 * Examining the linear formula and the formula used to calculate constants a
 * and b while knowing that the span for PTAT and THCODE values are between
 * 0x000 and 0xfff the largest integer possible is 0xfff * 0xfff == 0xffe001.
 * Integer also needs to be signed so that leaves 7 bits for binary
 * fixed point scaling.
 */

#define FIXPT_SHIFT 7
#define FIXPT_INT(_x) ((_x) << FIXPT_SHIFT)
#define INT_FIXPT(_x) ((_x) >> FIXPT_SHIFT)
#define FIXPT_DIV(_a, _b) DIV_ROUND_CLOSEST(((_a) << FIXPT_SHIFT), (_b))
#define FIXPT_TO_MCELSIUS(_x) ((_x) * 1000 >> FIXPT_SHIFT)

#define RCAR3_THERMAL_GRAN 500 /* mili Celsius */

/* no idea where these constants come from */
#define TJ_3 -41

static void rcar_gen3_thermal_calc_coefs(struct rcar_gen3_thermal_priv *priv,
					 struct rcar_gen3_thermal_tsc *tsc,
					 int ths_tj_1)
{
	/* TODO: Find documentation and document constant calculation formula */

	/*
	 * Division is not scaled in BSP and if scaled it might overflow
	 * the dividend (4095 * 4095 << 14 > INT_MAX) so keep it unscaled
	 */
	tsc->tj_t = (FIXPT_INT((priv->ptat[1] - priv->ptat[2]) * (ths_tj_1 - TJ_3))
		     / (priv->ptat[0] - priv->ptat[2])) + FIXPT_INT(TJ_3);

	tsc->coef.a1 = FIXPT_DIV(FIXPT_INT(tsc->thcode[1] - tsc->thcode[2]),
				 tsc->tj_t - FIXPT_INT(TJ_3));
	tsc->coef.b1 = FIXPT_INT(tsc->thcode[2]) - tsc->coef.a1 * TJ_3;

	tsc->coef.a2 = FIXPT_DIV(FIXPT_INT(tsc->thcode[1] - tsc->thcode[0]),
				 tsc->tj_t - FIXPT_INT(ths_tj_1));
	tsc->coef.b2 = FIXPT_INT(tsc->thcode[0]) - tsc->coef.a2 * ths_tj_1;
}

static int rcar_gen3_thermal_round(int temp)
{
	int result, round_offs;

	round_offs = temp >= 0 ? RCAR3_THERMAL_GRAN / 2 :
		-RCAR3_THERMAL_GRAN / 2;
	result = (temp + round_offs) / RCAR3_THERMAL_GRAN;
	return result * RCAR3_THERMAL_GRAN;
}

static int rcar_gen3_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct rcar_gen3_thermal_tsc *tsc = tz->devdata;
	int mcelsius, val;
	int reg;

	/* Read register and convert to mili Celsius */
	reg = rcar_gen3_thermal_read(tsc, REG_GEN3_TEMP) & CTEMP_MASK;

	if (reg <= tsc->thcode[1])
		val = FIXPT_DIV(FIXPT_INT(reg) - tsc->coef.b1,
				tsc->coef.a1);
	else
		val = FIXPT_DIV(FIXPT_INT(reg) - tsc->coef.b2,
				tsc->coef.a2);
	mcelsius = FIXPT_TO_MCELSIUS(val);

	/* Guaranteed operating range is -40C to 125C. */

	/* Round value to device granularity setting */
	*temp = rcar_gen3_thermal_round(mcelsius);

	return 0;
}

static int rcar_gen3_thermal_mcelsius_to_temp(struct rcar_gen3_thermal_tsc *tsc,
					      int mcelsius)
{
	int celsius, val;

	celsius = DIV_ROUND_CLOSEST(mcelsius, 1000);
	if (celsius <= INT_FIXPT(tsc->tj_t))
		val = celsius * tsc->coef.a1 + tsc->coef.b1;
	else
		val = celsius * tsc->coef.a2 + tsc->coef.b2;

	return INT_FIXPT(val);
}

static int rcar_gen3_thermal_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	struct rcar_gen3_thermal_tsc *tsc = tz->devdata;
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

static const struct soc_device_attribute r8a7795es1[] = {
	{ .soc_id = "r8a7795", .revision = "ES1.*" },
	{ /* sentinel */ }
};

static bool rcar_gen3_thermal_read_fuses(struct rcar_gen3_thermal_priv *priv)
{
	unsigned int i;
	u32 thscp;

	/* If fuses are not set, fallback to pseudo values. */
	thscp = rcar_gen3_thermal_read(priv->tscs[0], REG_GEN3_THSCP);
	if ((thscp & THSCP_COR_PARA_VLD) != THSCP_COR_PARA_VLD) {
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

	return true;
}

static void rcar_gen3_thermal_init_r8a7795es1(struct rcar_gen3_thermal_priv *priv,
					      struct rcar_gen3_thermal_tsc *tsc)
{
	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR,  CTSR_THBGR);
	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR,  0x0);

	usleep_range(1000, 2000);

	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR, CTSR_PONM);

	rcar_gen3_thermal_write(tsc, REG_GEN3_IRQCTL, 0x3F);
	rcar_gen3_thermal_write(tsc, REG_GEN3_IRQMSK, 0);
	if (priv->ops.set_trips)
		rcar_gen3_thermal_write(tsc, REG_GEN3_IRQEN,
					IRQ_TEMPD1 | IRQ_TEMP2);

	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR,
				CTSR_PONM | CTSR_AOUT | CTSR_THBGR | CTSR_VMEN);

	usleep_range(100, 200);

	rcar_gen3_thermal_write(tsc, REG_GEN3_CTSR,
				CTSR_PONM | CTSR_AOUT | CTSR_THBGR | CTSR_VMEN |
				CTSR_VMST | CTSR_THSST);

	usleep_range(1000, 2000);
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

static const int rcar_gen3_ths_tj_1 = 126;
static const int rcar_gen3_ths_tj_1_m3_w = 116;
static const struct of_device_id rcar_gen3_thermal_dt_ids[] = {
	{
		.compatible = "renesas,r8a774a1-thermal",
		.data = &rcar_gen3_ths_tj_1_m3_w,
	},
	{
		.compatible = "renesas,r8a774b1-thermal",
		.data = &rcar_gen3_ths_tj_1,
	},
	{
		.compatible = "renesas,r8a774e1-thermal",
		.data = &rcar_gen3_ths_tj_1,
	},
	{
		.compatible = "renesas,r8a7795-thermal",
		.data = &rcar_gen3_ths_tj_1,
	},
	{
		.compatible = "renesas,r8a7796-thermal",
		.data = &rcar_gen3_ths_tj_1_m3_w,
	},
	{
		.compatible = "renesas,r8a77961-thermal",
		.data = &rcar_gen3_ths_tj_1_m3_w,
	},
	{
		.compatible = "renesas,r8a77965-thermal",
		.data = &rcar_gen3_ths_tj_1,
	},
	{
		.compatible = "renesas,r8a77980-thermal",
		.data = &rcar_gen3_ths_tj_1,
	},
	{
		.compatible = "renesas,r8a779a0-thermal",
		.data = &rcar_gen3_ths_tj_1,
	},
	{
		.compatible = "renesas,r8a779f0-thermal",
		.data = &rcar_gen3_ths_tj_1,
	},
	{
		.compatible = "renesas,r8a779g0-thermal",
		.data = &rcar_gen3_ths_tj_1,
	},
	{},
};
MODULE_DEVICE_TABLE(of, rcar_gen3_thermal_dt_ids);

static int rcar_gen3_thermal_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pm_runtime_put(dev);
	pm_runtime_disable(dev);

	return 0;
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
	const int *ths_tj_1 = of_device_get_match_data(dev);
	struct resource *res;
	struct thermal_zone_device *zone;
	unsigned int i;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ops = rcar_gen3_tz_of_ops;
	priv->thermal_init = rcar_gen3_thermal_init;
	if (soc_device_match(r8a7795es1))
		priv->thermal_init = rcar_gen3_thermal_init_r8a7795es1;

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

	for (i = 0; i < priv->num_tscs; i++) {
		struct rcar_gen3_thermal_tsc *tsc = priv->tscs[i];

		priv->thermal_init(priv, tsc);
		rcar_gen3_thermal_calc_coefs(priv, tsc, *ths_tj_1);

		zone = devm_thermal_of_zone_register(dev, i, tsc, &priv->ops);
		if (IS_ERR(zone)) {
			dev_err(dev, "Sensor %u: Can't register thermal zone\n", i);
			ret = PTR_ERR(zone);
			goto error_unregister;
		}
		tsc->zone = zone;

		tsc->zone->tzp->no_hwmon = false;
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

		priv->thermal_init(priv, tsc);
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
	.remove		= rcar_gen3_thermal_remove,
};
module_platform_driver(rcar_gen3_thermal_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("R-Car Gen3 THS thermal sensor driver");
MODULE_AUTHOR("Wolfram Sang <wsa+renesas@sang-engineering.com>");
