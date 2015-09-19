/*
 * Device driver for MTCMOS DRIVER in hi6220 SOC
 *
 * Copyright (c) 2015 Hisilicon Co. Ltd
 *
 * Fei Wang  <w.f@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/delay.h>
#include <linux/time.h>

enum {
	HI6220_MTCMOS1,
	HI6220_MTCMOS2,
	HI6220_RG_MAX,
};

struct hi6220_mtcmos_ctrl_regs {
	unsigned int enable_reg;
	unsigned int disable_reg;
	unsigned int status_reg;
};

struct hi6220_mtcmos_ctrl_data {
	int shift;
	unsigned int mask;
};

struct hi6220_mtcmos_info {
	struct regulator_desc rdesc;
	struct hi6220_mtcmos_ctrl_regs ctrl_regs;
	struct hi6220_mtcmos_ctrl_data ctrl_data;
};

struct hi6220_mtcmos {
	struct regulator_dev *rdev[HI6220_RG_MAX];
	void __iomem *sc_on_regs;
	int mtcmos_steady_time;
	spinlock_t mtcmos_spin_lock;
};

static int hi6220_mtcmos_is_on(struct hi6220_mtcmos *mtcmos,
				unsigned int regs, unsigned int mask, int shift)
{
	unsigned int ret;
	unsigned long mtcmos_spin_flag = 0;

	spin_lock_irqsave(&mtcmos->mtcmos_spin_lock, mtcmos_spin_flag);
	ret = readl(mtcmos->sc_on_regs + regs);
	spin_unlock_irqrestore(&mtcmos->mtcmos_spin_lock, mtcmos_spin_flag);

	ret &= (mask << shift);
	return !!ret;
}

int hi6220_mtcmos_on(struct hi6220_mtcmos *mtcmos,
				unsigned int regs, unsigned int mask, int shift)
{
	unsigned long mtcmos_spin_flag = 0;

	spin_lock_irqsave(&mtcmos->mtcmos_spin_lock, mtcmos_spin_flag);
	writel(mask << shift, mtcmos->sc_on_regs + regs);
	udelay(mtcmos->mtcmos_steady_time);
	spin_unlock_irqrestore(&mtcmos->mtcmos_spin_lock, mtcmos_spin_flag);

	return 0;
}

int hi6220_mtcmos_off(struct hi6220_mtcmos *mtcmos,
			unsigned int regs, unsigned int mask, int shift)
{
	unsigned long mtcmos_spin_flag = 0;

	spin_lock_irqsave(&mtcmos->mtcmos_spin_lock, mtcmos_spin_flag);
	writel(mask << shift, mtcmos->sc_on_regs + regs);
	udelay(mtcmos->mtcmos_steady_time);
	spin_unlock_irqrestore(&mtcmos->mtcmos_spin_lock,
		mtcmos_spin_flag);

	return 0;
}

static int hi6220_regulator_mtcmos_is_enabled(struct regulator_dev *rdev)
{
	int ret;
	struct hi6220_mtcmos_info *sreg = rdev_get_drvdata(rdev);
	struct platform_device *pdev =
		container_of(rdev->dev.parent, struct platform_device, dev);
	struct hi6220_mtcmos *mtcmos = platform_get_drvdata(pdev);
	struct hi6220_mtcmos_ctrl_regs *ctrl_regs = &(sreg->ctrl_regs);
	struct hi6220_mtcmos_ctrl_data *ctrl_data = &(sreg->ctrl_data);

	ret = hi6220_mtcmos_is_on(mtcmos, ctrl_regs->status_reg,
					ctrl_data->mask, ctrl_data->shift);
	return ret;
}

static int hi6220_regulator_mtcmos_enabled(struct regulator_dev *rdev)
{
	int ret;
	struct hi6220_mtcmos_info *sreg = rdev_get_drvdata(rdev);
	struct platform_device *pdev =
		container_of(rdev->dev.parent, struct platform_device, dev);
	struct hi6220_mtcmos *mtcmos = platform_get_drvdata(pdev);
	struct hi6220_mtcmos_ctrl_regs *ctrl_regs = &(sreg->ctrl_regs);
	struct hi6220_mtcmos_ctrl_data *ctrl_data = &(sreg->ctrl_data);

	ret =  hi6220_mtcmos_on(mtcmos, ctrl_regs->enable_reg,
					ctrl_data->mask, ctrl_data->shift);
	if (0 == hi6220_mtcmos_is_on(mtcmos, ctrl_regs->status_reg,
					ctrl_data->mask, ctrl_data->shift)) {
		return -1;
	}
	return ret;
}

static int hi6220_regulator_mtcmos_disabled(struct regulator_dev *rdev)
{
	int ret;
	struct hi6220_mtcmos_info *sreg = rdev_get_drvdata(rdev);
	struct platform_device *pdev =
		container_of(rdev->dev.parent, struct platform_device, dev);
	struct hi6220_mtcmos *mtcmos = platform_get_drvdata(pdev);
	struct hi6220_mtcmos_ctrl_regs  *ctrl_regs = &(sreg->ctrl_regs);
	struct hi6220_mtcmos_ctrl_data  *ctrl_data = &(sreg->ctrl_data);

	ret = hi6220_mtcmos_off(mtcmos, ctrl_regs->disable_reg,
					ctrl_data->mask, ctrl_data->shift);

	return ret;
}

static struct regulator_ops hi6220_mtcmos_mtcmos_rops = {
	.is_enabled = hi6220_regulator_mtcmos_is_enabled,
	.enable = hi6220_regulator_mtcmos_enabled,
	.disable = hi6220_regulator_mtcmos_disabled,
};

#define HI6220_MTCMOS(vreg) \
{								\
	.rdesc = {					\
		.name = #vreg,			\
		.ops	= &hi6220_mtcmos_mtcmos_rops, \
		.type = REGULATOR_VOLTAGE,			\
		.owner = THIS_MODULE,		\
	},							\
}

static struct hi6220_mtcmos_info hi6220_mtcmos_info[] = {
	HI6220_MTCMOS(MTCMOS1),
	HI6220_MTCMOS(MTCMOS2),
};

static struct of_regulator_match hi6220_mtcmos_matches[] = {
	{ .name = "mtcmos1",
		.driver_data = &hi6220_mtcmos_info[HI6220_MTCMOS1], },
	{ .name = "mtcmos2",
		.driver_data = &hi6220_mtcmos_info[HI6220_MTCMOS2], },
};

static int hi6220_mtcmos_probe(struct platform_device *pdev)
{
	int ret;
	struct hi6220_mtcmos *mtcmos;
	const __be32 *sc_on_regs = NULL;
	void __iomem	*regs;
	struct device *dev;
	struct device_node *np, *child;
	int count, i;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	struct hi6220_mtcmos_info *sreg;

	dev = &pdev->dev;
	np = dev->of_node;
	mtcmos = devm_kzalloc(dev,
		sizeof(struct hi6220_mtcmos), GFP_KERNEL);
	if (!mtcmos)
		return -ENOMEM;

	spin_lock_init((spinlock_t *)&mtcmos->mtcmos_spin_lock);
	sc_on_regs = of_get_property(np, "hisilicon,mtcmos-sc-on-base", NULL);
	if (sc_on_regs) {
		regs = ioremap(be32_to_cpu(*sc_on_regs), 0x1000);
		mtcmos->sc_on_regs = regs;
	}
	ret = of_property_read_u32(np, "hisilicon,mtcmos-steady-us",
						&mtcmos->mtcmos_steady_time);

	count = of_regulator_match(&pdev->dev, np,
				 hi6220_mtcmos_matches,
				 ARRAY_SIZE(hi6220_mtcmos_matches));

	for (i = 0; i < HI6220_RG_MAX; i++) {
		init_data = hi6220_mtcmos_matches[i].init_data;
		if (!init_data)
			continue;
		sreg = hi6220_mtcmos_matches[i].driver_data;
		config.dev = &pdev->dev;
		config.init_data = init_data;
		config.driver_data = sreg;
		config.of_node = hi6220_mtcmos_matches[i].of_node;
		child = config.of_node;

		ret = of_property_read_u32_array(child, "hisilicon,ctrl-regs",
				(unsigned int *)(&sreg->ctrl_regs), 0x3);
		ret = of_property_read_u32_array(child, "hisilicon,ctrl-data",
				(unsigned int *)(&sreg->ctrl_data), 0x2);

		mtcmos->rdev[i] = regulator_register(&sreg->rdesc, &config);
		if (IS_ERR(mtcmos->rdev[i])) {
			ret = PTR_ERR(mtcmos->rdev[i]);
			dev_err(&pdev->dev, "failed to register mtcmos %s\n",
					sreg->rdesc.name);
			while (--i >= 0)
				regulator_unregister(mtcmos->rdev[i]);

			return ret;
		}
	}

	platform_set_drvdata(pdev, mtcmos);

	return 0;
}

static const struct of_device_id of_hi6220_mtcmos_match_tbl[] = {
	{ .compatible = "hisilicon,hi6220-mtcmos-driver", },
	{}
};

static struct platform_driver mtcmos_driver = {
	.driver = {
		.name = "hisi_hi6220_mtcmos",
		.owner = THIS_MODULE,
		.of_match_table = of_hi6220_mtcmos_match_tbl,
	},
	.probe = hi6220_mtcmos_probe,
};

static int __init hi6220_mtcmos_init(void)
{
	return platform_driver_register(&mtcmos_driver);
}

static void __exit hi6220_mtcmos_exit(void)
{
	platform_driver_unregister(&mtcmos_driver);
}

fs_initcall(hi6220_mtcmos_init);
module_exit(hi6220_mtcmos_exit);

MODULE_AUTHOR("Fei Wang <w.f@huawei.com>");
MODULE_DESCRIPTION("Hi6220 mtcmo interface driver");
MODULE_LICENSE("GPL v2");
