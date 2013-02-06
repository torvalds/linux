/*
 * exynos4_tmu.c - Samsung EXYNOS4 TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Donggeun Kim <dg77.kim@samsung.com>
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
#include <linux/delay.h>

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#include <linux/platform_data/exynos4_tmu.h>
#ifdef CONFIG_SAMSUNG_THERMAL_INTERFACE
#include <linux/exynos_thermal.h>
#endif

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

#define EXYNOS4X12_TMU_REG_TRIMINFO_CONROL	0x14
#define EXYNOS4X12_TMU_REG_TRESHOLD_TEMP_RISE	0x50
#define EXYNOS4X12_TMU_REG_TRESHOLD_TEMP_FALL	0x54
#define EXYNOS4X12_TMU_REG_PAST_TMEP3_0		0x60
#define EXYNOS4X12_TMU_REG_PAST_TMEP7_4		0x64
#define EXYNOS4X12_TMU_REG_PAST_TMEP11_8	0x68
#define EXYNOS4X12_TMU_REG_PAST_TMEP15_12	0x6C
#define EXYNOS4X12_TMU_REG_EMUL_CON		0x80

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
#define EXYNOS4X12_TMU_INTCLEAR_VAL	0x111111

#define EXYNOS4X12_TMU_DEF_CODE_TO_TEMP_OFFSET	25
#define EXYNOS4X12_TMU_RELOAD		(1 << 0)
#define EXYNOS4X12_TMU_ACTIME		(1 << 0)

#define EXYNOS4X12_THERM_TRIP_ENABLE	(1 << 12)
#define EXYNOS4X12_THERM_TRIP_MODE_000	(0 << 13)
#define EXYNOS4X12_THERM_TRIP_MODE_100	(4 << 13)
#define EXYNOS4X12_THERM_TRIP_MODE_101	(5 << 13)
#define EXYNOS4X12_THERM_TRIP_MODE_110	(6 << 13)
#define EXYNOS4X12_THERM_TRIP_MODE_111	(7 << 13)

#define EXYNOS4X12_THRES_LEVEL_RISE0_SHIFT	(1 << 0)
#define EXYNOS4X12_THRES_LEVEL_RISE1_SHIFT	(1 << 8)
#define EXYNOS4X12_THRES_LEVEL_RISE2_SHIFT	(1 << 16)
#define EXYNOS4X12_THRES_LEVEL_RISE3_SHIFT	(1 << 24)

#define EXYNOS4X12_THRES_LEVEL_FALL0_SHIFT	(1 << 0)
#define EXYNOS4X12_THRES_LEVEL_FALL1_SHIFT	(1 << 8)
#define EXYNOS4X12_THRES_LEVEL_FALL2_SHIFT	(1 << 16)

#define EXYNOS4X12_INTEN_RISE0			(1 << 0)
#define EXYNOS4X12_INTEN_RISE1			(1 << 4)
#define EXYNOS4X12_INTEN_RISE2			(1 << 8)
#define EXYNOS4X12_INTEN_FALL0			(1 << 16)
#define EXYNOS4X12_INTEN_FALL1			(1 << 20)
#define EXYNOS4X12_INTEN_FALL2			(1 << 24)

#define EXYNOS4X12_INTSTAT_RISE0		(1 << 0)
#define EXYNOS4X12_INTSTAT_RISE1		(1 << 4)
#define EXYNOS4X12_INTSTAT_RISE2		(1 << 8)
#define EXYNOS4X12_INTSTAT_FALL0		(1 << 16)
#define EXYNOS4X12_INTSTAT_FALL1		(1 << 20)
#define EXYNOS4X12_INTSTAT_FALL2		(1 << 24)

#define EXYNOS4X12_INTCLEAR_RISE0		(1 << 0)
#define EXYNOS4X12_INTCLEAR_RISE1		(1 << 4)
#define EXYNOS4X12_INTCLEAR_RISE2		(1 << 8)
#define EXYNOS4X12_INTCLEAR_FALL0		(1 << 12)
#define EXYNOS4X12_INTCLEAR_FALL1		(1 << 16)
#define EXYNOS4X12_INTCLEAR_FALL2		(1 << 20)

struct exynos4_tmu_data {
	struct exynos4_tmu_platform_data *pdata;
	struct device *hwmon_dev;
	struct resource *mem;
	void __iomem *base;
	int irq;
	struct work_struct irq_work;
	struct mutex lock;
	struct clk *clk;
	u8 temp_error1, temp_error2;
	enum tmu_type type;
};

/*
 * TMU treats temperature as a mapped temperature code.
 * The temperature is converted differently depending on the calibration type.
 */
static int temp_to_code(struct exynos4_tmu_data *data, u8 temp)
{
	struct exynos4_tmu_platform_data *pdata = data->pdata;
	int temp_code = 0;

	/* temp should range between 25 and 125 */
	if (temp < 25 || temp > 125) {
		temp_code = -EINVAL;
		goto out;
	}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		if (data->type == TYPE_EXYNOS4210) {
			temp_code = (temp - 25) *
			    (data->temp_error2 - data->temp_error1) /
			    (85 - 25) + data->temp_error1;
		}
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp_code = temp + data->temp_error1 - 25;
		break;
	default:
		if (data->type == TYPE_EXYNOS4210)
			temp_code = temp + EXYNOS4_TMU_DEF_CODE_TO_TEMP_OFFSET;
		else
			temp_code = temp +
			       EXYNOS4X12_TMU_DEF_CODE_TO_TEMP_OFFSET;

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
	int temp = 0;

	if (data->type == TYPE_EXYNOS4210) {
		/* temp_code should range between 75 and 175 */
		if (temp_code < 75 || temp_code > 175) {
			temp = -ENODATA;
			goto out;
		}
	} else {
		/* temp_code should range between 49 and 151 */
		if (temp_code < 49 || temp_code > 151) {
			temp = -ENODATA;
			goto out;
		}
	}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		if (data->type == TYPE_EXYNOS4210) {
			temp = (temp_code - data->temp_error1) * (85 - 25) /
			    (data->temp_error2 - data->temp_error1) + 25;
		}
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp = temp_code - data->temp_error1 + 25;
		break;
	default:
		if (data->type == TYPE_EXYNOS4210)
			temp = temp_code - EXYNOS4_TMU_DEF_CODE_TO_TEMP_OFFSET;
		else
			temp = temp_code -
			       EXYNOS4X12_TMU_DEF_CODE_TO_TEMP_OFFSET;
		break;
	}
out:
	return temp;
}

static int exynos4210_tmu_initialize(struct platform_device *pdev)
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

static int exynos4x12_tmu_initialize(struct platform_device *pdev)
{
	struct exynos4_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos4_tmu_platform_data *pdata = data->pdata;
	unsigned int trim_info;
	int ret = 0, threshold_code[4], interrupt_code, tmp;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	tmp = readl(data->base + EXYNOS4X12_TMU_REG_TRIMINFO_CONROL);
	tmp |= EXYNOS4X12_TMU_RELOAD;
	writel(tmp, data->base + EXYNOS4X12_TMU_REG_TRIMINFO_CONROL);

	mdelay(1);

	/* Save trimming info in order to perform calibration */
	trim_info = readl(data->base + EXYNOS4_TMU_REG_TRIMINFO);
	data->temp_error1 = trim_info & EXYNOS4_TMU_TRIM_TEMP_MASK;

	/* In case of non e-fusing chip, s/w workaround */
	if (trim_info == 0)
		data->temp_error1 = 0x37;

	pr_debug("%s: triminfo reg = 0x%08x, value = %d\n", __func__,
			trim_info, data->temp_error1);

	/* Write temperature code for threshold */
	threshold_code[0] = temp_to_code(data,
		pdata->threshold + pdata->trigger_levels[0]);
	if (threshold_code[0] < 0) {
		ret = threshold_code[0];
		goto out;
	}
	threshold_code[1] = temp_to_code(data,
		pdata->threshold + pdata->trigger_levels[1]);
	if (threshold_code[1] < 0) {
		ret = threshold_code[1];
		goto out;
	}
	threshold_code[2] = temp_to_code(data,
		pdata->threshold + pdata->trigger_levels[2]);
	if (threshold_code[2] < 0) {
		ret = threshold_code[2];
		goto out;
	}
	threshold_code[3] = temp_to_code(data,
		pdata->threshold + pdata->trigger_levels[3]);
	if (threshold_code[3] < 0) {
		ret = threshold_code[3];
		goto out;
	}

	/* Set interrupt trigger level */
	interrupt_code = ((threshold_code[3] << 24) |
		(threshold_code[2] << 16) | (threshold_code[1] << 8) |
		(threshold_code[0] << 0));
	writel(interrupt_code,
		data->base + EXYNOS4X12_TMU_REG_TRESHOLD_TEMP_RISE);
	mdelay(50);

	writel(EXYNOS4X12_TMU_INTCLEAR_VAL,
		data->base + EXYNOS4_TMU_REG_INTCLEAR);
out:
	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return ret;
}

static int exynos4_tmu_initialize(struct platform_device *pdev)
{
	int ret;

	if (pdev->id_entry->driver_data == TYPE_EXYNOS4210)
		ret = exynos4210_tmu_initialize(pdev);
	else
		ret = exynos4x12_tmu_initialize(pdev);

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
		if (data->type == TYPE_EXYNOS4210)
			con |= EXYNOS4_TMU_CORE_ON;
		else
			con |= EXYNOS4_TMU_CORE_ON | (0x6 << 20);
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

	if (data->type == TYPE_EXYNOS4210)
		writel(EXYNOS4_TMU_INTCLEAR_VAL,
		       data->base + EXYNOS4_TMU_REG_INTCLEAR);
	else
		writel(EXYNOS4X12_TMU_INTCLEAR_VAL,
		       data->base + EXYNOS4_TMU_REG_INTCLEAR);

	kobject_uevent(&data->hwmon_dev->kobj, KOBJ_CHANGE);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
#ifdef CONFIG_SAMSUNG_THERMAL_INTERFACE
	exynos4_report_trigger();
#endif
	enable_irq(data->irq);
}

static irqreturn_t exynos4_tmu_irq(int irq, void *id)
{
	struct exynos4_tmu_data *data = id;

	disable_irq_nosync(irq);
	schedule_work(&data->irq_work);

	return IRQ_HANDLED;
}

static ssize_t exynos4_tmu_show_name(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "exynos4-tmu\n");
}

static ssize_t exynos4_tmu_show_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct exynos4_tmu_data *data = dev_get_drvdata(dev);
	int ret;

	ret = exynos4_tmu_read(data);
	if (ret < 0)
		return ret;

	/* convert from degree Celsius to millidegree Celsius */
	return sprintf(buf, "%d\n", ret * 1000);
}

static ssize_t exynos4_tmu_show_alarm(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct exynos4_tmu_data *data = dev_get_drvdata(dev);
	struct exynos4_tmu_platform_data *pdata = data->pdata;
	int temp;
	unsigned int trigger_level;

	temp = exynos4_tmu_read(data);
	if (temp < 0)
		return temp;

	trigger_level = pdata->threshold + pdata->trigger_levels[attr->index];

	return sprintf(buf, "%d\n", !!(temp > trigger_level));
}

static ssize_t exynos4_tmu_show_level(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct exynos4_tmu_data *data = dev_get_drvdata(dev);
	struct exynos4_tmu_platform_data *pdata = data->pdata;
	unsigned int temp = pdata->threshold +
			pdata->trigger_levels[attr->index];

	return sprintf(buf, "%u\n", temp * 1000);
}

static DEVICE_ATTR(name, S_IRUGO, exynos4_tmu_show_name, NULL);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, exynos4_tmu_show_temp, NULL, 0);

static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO,
		exynos4_tmu_show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO,
		exynos4_tmu_show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp1_emergency_alarm, S_IRUGO,
		exynos4_tmu_show_alarm, NULL, 3);

static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, exynos4_tmu_show_level, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, exynos4_tmu_show_level, NULL, 2);
static SENSOR_DEVICE_ATTR(temp1_emergency, S_IRUGO,
		exynos4_tmu_show_level, NULL, 3);

static struct attribute *exynos4_tmu_attributes[] = {
	&dev_attr_name.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_emergency_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_emergency.dev_attr.attr,
	NULL,
};

static const struct attribute_group exynos4_tmu_attr_group = {
	.attrs = exynos4_tmu_attributes,
};

#ifdef CONFIG_SAMSUNG_THERMAL_INTERFACE
static struct thermal_sensor_conf exynos4_sensor_conf = {
	.name			= "exynos4-therm",
	.read_temperature	= (int (*)(void *))exynos4_tmu_read,
};
#endif
/*CONFIG_SAMSUNG_THERMAL_INTERFACE*/

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

	data->clk = clk_get(NULL, "tmu");
	if (IS_ERR(data->clk)) {
		ret = PTR_ERR(data->clk);
		dev_err(&pdev->dev, "Failed to get clock\n");
		goto err_irq;
	}

	data->pdata = pdata;
	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);

	data->type = pdev->id_entry->driver_data;
	ret = exynos4_tmu_initialize(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize TMU\n");
		goto err_clk;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &exynos4_tmu_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs group\n");
		goto err_clk;
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		dev_err(&pdev->dev, "Failed to register hwmon device\n");
		goto err_create_group;
	}

#ifdef CONFIG_SAMSUNG_THERMAL_INTERFACE
	(&exynos4_sensor_conf)->private_data = data;
	(&exynos4_sensor_conf)->sensor_data = pdata;
	ret = exynos4_register_thermal(&exynos4_sensor_conf);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register thermal interface\n");
		goto err_hwmon_device;
	}
#endif
	hwmon_register_properties(data->hwmon_dev, &exynos4_tmu_attr_group);

	exynos4_tmu_control(pdev, true);

	return 0;

#ifdef CONFIG_SAMSUNG_THERMAL_INTERFACE
err_hwmon_device:
	hwmon_device_unregister(data->hwmon_dev);
#endif
err_create_group:
	sysfs_remove_group(&pdev->dev.kobj, &exynos4_tmu_attr_group);
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

#ifdef CONFIG_SAMSUNG_THERMAL_INTERFACE
	exynos4_unregister_thermal();
#endif
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &exynos4_tmu_attr_group);

	clk_put(data->clk);

	free_irq(data->irq, data);

	iounmap(data->base);
	release_mem_region(data->mem->start, resource_size(data->mem));

	platform_set_drvdata(pdev, NULL);

	kfree(data);

	return 0;
}

#ifdef CONFIG_PM
static int exynos4_tmu_suspend(struct platform_device *pdev, pm_message_t state)
{
	exynos4_tmu_control(pdev, false);

	return 0;
}

static int exynos4_tmu_resume(struct platform_device *pdev)
{
	exynos4_tmu_initialize(pdev);
	exynos4_tmu_control(pdev, true);

	return 0;
}
#else
#define exynos4_tmu_suspend NULL
#define exynos4_tmu_resume NULL
#endif

static const struct platform_device_id exynos4_tmu_id[] = {
	{ "exynos4210-tmu", TYPE_EXYNOS4210 },
	{ "exynos4x12-tmu", TYPE_EXYNOS4X12 },
	{ },
};

static struct platform_driver exynos4_tmu_driver = {
	.driver = {
		.name   = "exynos4-tmu",
		.owner  = THIS_MODULE,
	},
	.probe = exynos4_tmu_probe,
	.remove	= __devexit_p(exynos4_tmu_remove),
	.suspend = exynos4_tmu_suspend,
	.resume = exynos4_tmu_resume,
	.id_table = exynos4_tmu_id,
};

static int __init exynos4_tmu_driver_init(void)
{
	return platform_driver_register(&exynos4_tmu_driver);
}
module_init(exynos4_tmu_driver_init);

static void __exit exynos4_tmu_driver_exit(void)
{
	platform_driver_unregister(&exynos4_tmu_driver);
}
module_exit(exynos4_tmu_driver_exit);

MODULE_DESCRIPTION("EXYNOS4 TMU Driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:exynos4-tmu");
