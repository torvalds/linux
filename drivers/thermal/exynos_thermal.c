/*
 * exynos_thermal.c - Samsung EXYNOS TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Donggeun Kim <dg77.kim@samsung.com>
 *  Amit Daniel Kachhap <amit.kachhap@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/io.h>
#include <linux/mutex.h>

#include <linux/platform_data/exynos_thermal.h>

#define EXYNOS4_TMU_REG_TRIMINFO	0x0
#define EXYNOS4_TMU_REG_CONTROL		0x20
#define EXYNOS4_TMU_REG_STATUS		0x28
#define EXYNOS4_TMU_REG_CURRENT_TEMP	0x40
#define EXYNOS4_TMU_REG_THRESHOLD_TEMP	0x44
#define EXYNOS4_TMU_REG_TRIG_LEVEL0	0x50
#define EXYNOS4_TMU_REG_TRIG_LEVEL1	0x54
#define EXYNOS4_TMU_REG_TRIG_LEVEL2	0x58
#define EXYNOS4_TMU_REG_TRIG_LEVEL3	0x5C
#define EXYNOS4_TMU_REG_PAST_TEMP0	0x60
#define EXYNOS4_TMU_REG_PAST_TEMP1	0x64
#define EXYNOS4_TMU_REG_PAST_TEMP2	0x68
#define EXYNOS4_TMU_REG_PAST_TEMP3	0x6C
#define EXYNOS4_TMU_REG_INTEN		0x70
#define EXYNOS4_TMU_REG_INTSTAT		0x74
#define EXYNOS4_TMU_REG_INTCLEAR	0x78

#define EXYNOS4_TMU_GAIN_SHIFT		8
#define EXYNOS4_TMU_REF_VOLTAGE_SHIFT	24

#define EXYNOS4_TMU_TRIM_TEMP_MASK	0xff
#define EXYNOS4_TMU_CORE_ON	3
#define EXYNOS4_TMU_CORE_OFF	2
#define EXYNOS4_TMU_DEF_CODE_TO_TEMP_OFFSET	50
#define EXYNOS4_TMU_TRIG_LEVEL0_MASK	0x1
#define EXYNOS4_TMU_TRIG_LEVEL1_MASK	0x10
#define EXYNOS4_TMU_TRIG_LEVEL2_MASK	0x100
#define EXYNOS4_TMU_TRIG_LEVEL3_MASK	0x1000
#define EXYNOS4_TMU_INTCLEAR_VAL	0x1111

struct exynos4_tmu_data {
	struct exynos4_tmu_platform_data *pdata;
	struct resource *mem;
	void __iomem *base;
	int irq;
	struct work_struct irq_work;
	struct mutex lock;
	struct clk *clk;
	u8 temp_error1, temp_error2;
};

/*
 * TMU treats temperature as a mapped temperature code.
 * The temperature is converted differently depending on the calibration type.
 */
static int temp_to_code(struct exynos4_tmu_data *data, u8 temp)
{
	struct exynos4_tmu_platform_data *pdata = data->pdata;
	int temp_code;

	/* temp should range between 25 and 125 */
	if (temp < 25 || temp > 125) {
		temp_code = -EINVAL;
		goto out;
	}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp_code = (temp - 25) *
		    (data->temp_error2 - data->temp_error1) /
		    (85 - 25) + data->temp_error1;
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp_code = temp + data->temp_error1 - 25;
		break;
	default:
		temp_code = temp + EXYNOS4_TMU_DEF_CODE_TO_TEMP_OFFSET;
		break;
	}
out:
	return temp_code;
}

/*
 * Calculate a temperature value from a temperature code.
 * The unit of the temperature is degree Celsius.
 */
static int code_to_temp(struct exynos4_tmu_data *data, u8 temp_code)
{
	struct exynos4_tmu_platform_data *pdata = data->pdata;
	int temp;

	/* temp_code should range between 75 and 175 */
	if (temp_code < 75 || temp_code > 175) {
		temp = -ENODATA;
		goto out;
	}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp = (temp_code - data->temp_error1) * (85 - 25) /
		    (data->temp_error2 - data->temp_error1) + 25;
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp = temp_code - data->temp_error1 + 25;
		break;
	default:
		temp = temp_code - EXYNOS4_TMU_DEF_CODE_TO_TEMP_OFFSET;
		break;
	}
out:
	return temp;
}

static int exynos4_tmu_initialize(struct platform_device *pdev)
{
	struct exynos4_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos4_tmu_platform_data *pdata = data->pdata;
	unsigned int status, trim_info;
	int ret = 0, threshold_code;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	status = readb(data->base + EXYNOS4_TMU_REG_STATUS);
	if (!status) {
		ret = -EBUSY;
		goto out;
	}

	/* Save trimming info in order to perform calibration */
	trim_info = readl(data->base + EXYNOS4_TMU_REG_TRIMINFO);
	data->temp_error1 = trim_info & EXYNOS4_TMU_TRIM_TEMP_MASK;
	data->temp_error2 = ((trim_info >> 8) & EXYNOS4_TMU_TRIM_TEMP_MASK);

	/* Write temperature code for threshold */
	threshold_code = temp_to_code(data, pdata->threshold);
	if (threshold_code < 0) {
		ret = threshold_code;
		goto out;
	}
	writeb(threshold_code,
		data->base + EXYNOS4_TMU_REG_THRESHOLD_TEMP);

	writeb(pdata->trigger_levels[0],
		data->base + EXYNOS4_TMU_REG_TRIG_LEVEL0);
	writeb(pdata->trigger_levels[1],
		data->base + EXYNOS4_TMU_REG_TRIG_LEVEL1);
	writeb(pdata->trigger_levels[2],
		data->base + EXYNOS4_TMU_REG_TRIG_LEVEL2);
	writeb(pdata->trigger_levels[3],
		data->base + EXYNOS4_TMU_REG_TRIG_LEVEL3);

	writel(EXYNOS4_TMU_INTCLEAR_VAL,
		data->base + EXYNOS4_TMU_REG_INTCLEAR);
out:
	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return ret;
}

static void exynos4_tmu_control(struct platform_device *pdev, bool on)
{
	struct exynos4_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos4_tmu_platform_data *pdata = data->pdata;
	unsigned int con, interrupt_en;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	con = pdata->reference_voltage << EXYNOS4_TMU_REF_VOLTAGE_SHIFT |
		pdata->gain << EXYNOS4_TMU_GAIN_SHIFT;
	if (on) {
		con |= EXYNOS4_TMU_CORE_ON;
		interrupt_en = pdata->trigger_level3_en << 12 |
			pdata->trigger_level2_en << 8 |
			pdata->trigger_level1_en << 4 |
			pdata->trigger_level0_en;
	} else {
		con |= EXYNOS4_TMU_CORE_OFF;
		interrupt_en = 0; /* Disable all interrupts */
	}
	writel(interrupt_en, data->base + EXYNOS4_TMU_REG_INTEN);
	writel(con, data->base + EXYNOS4_TMU_REG_CONTROL);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
}

static int exynos4_tmu_read(struct exynos4_tmu_data *data)
{
	u8 temp_code;
	int temp;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	temp_code = readb(data->base + EXYNOS4_TMU_REG_CURRENT_TEMP);
	temp = code_to_temp(data, temp_code);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return temp;
}

static void exynos4_tmu_work(struct work_struct *work)
{
	struct exynos4_tmu_data *data = container_of(work,
			struct exynos4_tmu_data, irq_work);

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	writel(EXYNOS4_TMU_INTCLEAR_VAL, data->base + EXYNOS4_TMU_REG_INTCLEAR);

	enable_irq(data->irq);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
}

static irqreturn_t exynos4_tmu_irq(int irq, void *id)
{
	struct exynos4_tmu_data *data = id;

	disable_irq_nosync(irq);
	schedule_work(&data->irq_work);

	return IRQ_HANDLED;
}

static int __devinit exynos4_tmu_probe(struct platform_device *pdev)
{
	struct exynos4_tmu_data *data;
	struct exynos4_tmu_platform_data *pdata = pdev->dev.platform_data;
	int ret;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}

	data = kzalloc(sizeof(struct exynos4_tmu_data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate driver structure\n");
		return -ENOMEM;
	}

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		ret = data->irq;
		dev_err(&pdev->dev, "Failed to get platform irq\n");
		goto err_free;
	}

	INIT_WORK(&data->irq_work, exynos4_tmu_work);

	data->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!data->mem) {
		ret = -ENOENT;
		dev_err(&pdev->dev, "Failed to get platform resource\n");
		goto err_free;
	}

	data->mem = request_mem_region(data->mem->start,
			resource_size(data->mem), pdev->name);
	if (!data->mem) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "Failed to request memory region\n");
		goto err_free;
	}

	data->base = ioremap(data->mem->start, resource_size(data->mem));
	if (!data->base) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "Failed to ioremap memory\n");
		goto err_mem_region;
	}

	ret = request_irq(data->irq, exynos4_tmu_irq,
		IRQF_TRIGGER_RISING,
		"exynos4-tmu", data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", data->irq);
		goto err_io_remap;
	}

	data->clk = clk_get(NULL, "tmu_apbif");
	if (IS_ERR(data->clk)) {
		ret = PTR_ERR(data->clk);
		dev_err(&pdev->dev, "Failed to get clock\n");
		goto err_irq;
	}

	data->pdata = pdata;
	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);

	ret = exynos4_tmu_initialize(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize TMU\n");
		goto err_clk;
	}

	exynos4_tmu_control(pdev, true);

	return 0;
err_clk:
	platform_set_drvdata(pdev, NULL);
	clk_put(data->clk);
err_irq:
	free_irq(data->irq, data);
err_io_remap:
	iounmap(data->base);
err_mem_region:
	release_mem_region(data->mem->start, resource_size(data->mem));
err_free:
	kfree(data);

	return ret;
}

static int __devexit exynos4_tmu_remove(struct platform_device *pdev)
{
	struct exynos4_tmu_data *data = platform_get_drvdata(pdev);

	exynos4_tmu_control(pdev, false);

	clk_put(data->clk);

	free_irq(data->irq, data);

	iounmap(data->base);
	release_mem_region(data->mem->start, resource_size(data->mem));

	platform_set_drvdata(pdev, NULL);

	kfree(data);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos4_tmu_suspend(struct device *dev)
{
	exynos4_tmu_control(to_platform_device(dev), false);

	return 0;
}

static int exynos4_tmu_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	exynos4_tmu_initialize(pdev);
	exynos4_tmu_control(pdev, true);

	return 0;
}

static SIMPLE_DEV_PM_OPS(exynos4_tmu_pm,
			 exynos4_tmu_suspend, exynos4_tmu_resume);
#define EXYNOS4_TMU_PM	(&exynos4_tmu_pm)
#else
#define EXYNOS4_TMU_PM	NULL
#endif

static struct platform_driver exynos4_tmu_driver = {
	.driver = {
		.name   = "exynos4-tmu",
		.owner  = THIS_MODULE,
		.pm     = EXYNOS4_TMU_PM,
	},
	.probe = exynos4_tmu_probe,
	.remove	= __devexit_p(exynos4_tmu_remove),
};

module_platform_driver(exynos4_tmu_driver);

MODULE_DESCRIPTION("EXYNOS4 TMU Driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:exynos4-tmu");
