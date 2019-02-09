/*
 * Copyright (C) 2016 NVIDIA CORPORATION, All Rights Reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

struct gic_clk_data {
	unsigned int num_clocks;
	const char *const *clocks;
};

static int gic_runtime_resume(struct device *dev)
{
	struct gic_chip_data *gic = dev_get_drvdata(dev);
	int ret;

	ret = pm_clk_resume(dev);
	if (ret)
		return ret;

	/*
	 * On the very first resume, the pointer to the driver data
	 * will be NULL and this is intentional, because we do not
	 * want to restore the GIC on the very first resume. So if
	 * the pointer is not valid just return.
	 */
	if (!gic)
		return 0;

	gic_dist_restore(gic);
	gic_cpu_restore(gic);

	return 0;
}

static int gic_runtime_suspend(struct device *dev)
{
	struct gic_chip_data *gic = dev_get_drvdata(dev);

	gic_dist_save(gic);
	gic_cpu_save(gic);

	return pm_clk_suspend(dev);
}

static int gic_get_clocks(struct device *dev, const struct gic_clk_data *data)
{
	unsigned int i;
	int ret;

	if (!dev || !data)
		return -EINVAL;

	ret = pm_clk_create(dev);
	if (ret)
		return ret;

	for (i = 0; i < data->num_clocks; i++) {
		ret = of_pm_clk_add_clk(dev, data->clocks[i]);
		if (ret) {
			dev_err(dev, "failed to add clock %s\n",
				data->clocks[i]);
			pm_clk_destroy(dev);
			return ret;
		}
	}

	return 0;
}

static int gic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct gic_clk_data *data;
	struct gic_chip_data *gic;
	int ret, irq;

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "no device match found\n");
		return -ENODEV;
	}

	irq = irq_of_parse_and_map(dev->of_node, 0);
	if (!irq) {
		dev_err(dev, "no parent interrupt found!\n");
		return -EINVAL;
	}

	ret = gic_get_clocks(dev, data);
	if (ret)
		goto irq_dispose;

	pm_runtime_enable(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		goto rpm_disable;

	ret = gic_of_init_child(dev, &gic, irq);
	if (ret)
		goto rpm_put;

	platform_set_drvdata(pdev, gic);

	pm_runtime_put(dev);

	dev_info(dev, "GIC IRQ controller registered\n");

	return 0;

rpm_put:
	pm_runtime_put_sync(dev);
rpm_disable:
	pm_runtime_disable(dev);
	pm_clk_destroy(dev);
irq_dispose:
	irq_dispose_mapping(irq);

	return ret;
}

static const struct dev_pm_ops gic_pm_ops = {
	SET_RUNTIME_PM_OPS(gic_runtime_suspend,
			   gic_runtime_resume, NULL)
};

static const char * const gic400_clocks[] = {
	"clk",
};

static const struct gic_clk_data gic400_data = {
	.num_clocks = ARRAY_SIZE(gic400_clocks),
	.clocks = gic400_clocks,
};

static const struct of_device_id gic_match[] = {
	{ .compatible = "nvidia,tegra210-agic",	.data = &gic400_data },
	{},
};
MODULE_DEVICE_TABLE(of, gic_match);

static struct platform_driver gic_driver = {
	.probe		= gic_probe,
	.driver		= {
		.name	= "gic",
		.of_match_table	= gic_match,
		.pm	= &gic_pm_ops,
	}
};

builtin_platform_driver(gic_driver);
