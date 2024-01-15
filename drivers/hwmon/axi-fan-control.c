// SPDX-License-Identifier: GPL-2.0
/*
 * Fan Control HDL CORE driver
 *
 * Copyright 2019 Analog Devices Inc.
 */
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/fpga/adi-axi-common.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/* register map */
#define ADI_REG_RSTN		0x0080
#define ADI_REG_PWM_WIDTH	0x0084
#define ADI_REG_TACH_PERIOD	0x0088
#define ADI_REG_TACH_TOLERANCE	0x008c
#define ADI_REG_PWM_PERIOD	0x00c0
#define ADI_REG_TACH_MEASUR	0x00c4
#define ADI_REG_TEMPERATURE	0x00c8
#define ADI_REG_TEMP_00_H	0x0100
#define ADI_REG_TEMP_25_L	0x0104
#define ADI_REG_TEMP_25_H	0x0108
#define ADI_REG_TEMP_50_L	0x010c
#define ADI_REG_TEMP_50_H	0x0110
#define ADI_REG_TEMP_75_L	0x0114
#define ADI_REG_TEMP_75_H	0x0118
#define ADI_REG_TEMP_100_L	0x011c

#define ADI_REG_IRQ_MASK	0x0040
#define ADI_REG_IRQ_PENDING	0x0044
#define ADI_REG_IRQ_SRC		0x0048

/* IRQ sources */
#define ADI_IRQ_SRC_PWM_CHANGED		BIT(0)
#define ADI_IRQ_SRC_TACH_ERR		BIT(1)
#define ADI_IRQ_SRC_TEMP_INCREASE	BIT(2)
#define ADI_IRQ_SRC_NEW_MEASUR		BIT(3)
#define ADI_IRQ_SRC_MASK		GENMASK(3, 0)
#define ADI_IRQ_MASK_OUT_ALL		0xFFFFFFFFU

#define SYSFS_PWM_MAX			255

struct axi_fan_control_data {
	void __iomem *base;
	struct device *hdev;
	unsigned long clk_rate;
	int irq;
	/* pulses per revolution */
	u32 ppr;
	bool hw_pwm_req;
	bool update_tacho_params;
	u8 fan_fault;
};

static inline void axi_iowrite(const u32 val, const u32 reg,
			       const struct axi_fan_control_data *ctl)
{
	iowrite32(val, ctl->base + reg);
}

static inline u32 axi_ioread(const u32 reg,
			     const struct axi_fan_control_data *ctl)
{
	return ioread32(ctl->base + reg);
}

/*
 * The core calculates the temperature as:
 *	T = /raw * 509.3140064 / 65535) - 280.2308787
 */
static ssize_t axi_fan_control_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct axi_fan_control_data *ctl = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	u32 temp = axi_ioread(attr->index, ctl);

	temp = DIV_ROUND_CLOSEST_ULL(temp * 509314ULL, 65535) - 280230;

	return sprintf(buf, "%u\n", temp);
}

static ssize_t axi_fan_control_store(struct device *dev, struct device_attribute *da,
				     const char *buf, size_t count)
{
	struct axi_fan_control_data *ctl = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	u32 temp;
	int ret;

	ret = kstrtou32(buf, 10, &temp);
	if (ret)
		return ret;

	temp = DIV_ROUND_CLOSEST_ULL((temp + 280230) * 65535ULL, 509314);
	axi_iowrite(temp, attr->index, ctl);

	return count;
}

static long axi_fan_control_get_pwm_duty(const struct axi_fan_control_data *ctl)
{
	u32 pwm_width = axi_ioread(ADI_REG_PWM_WIDTH, ctl);
	u32 pwm_period = axi_ioread(ADI_REG_PWM_PERIOD, ctl);
	/*
	 * PWM_PERIOD is a RO register set by the core. It should never be 0.
	 * For now we are trusting the HW...
	 */
	return DIV_ROUND_CLOSEST(pwm_width * SYSFS_PWM_MAX, pwm_period);
}

static int axi_fan_control_set_pwm_duty(const long val,
					struct axi_fan_control_data *ctl)
{
	u32 pwm_period = axi_ioread(ADI_REG_PWM_PERIOD, ctl);
	u32 new_width;
	long __val = clamp_val(val, 0, SYSFS_PWM_MAX);

	new_width = DIV_ROUND_CLOSEST(__val * pwm_period, SYSFS_PWM_MAX);

	axi_iowrite(new_width, ADI_REG_PWM_WIDTH, ctl);

	return 0;
}

static long axi_fan_control_get_fan_rpm(const struct axi_fan_control_data *ctl)
{
	const u32 tach = axi_ioread(ADI_REG_TACH_MEASUR, ctl);

	if (tach == 0)
		/* should we return error, EAGAIN maybe? */
		return 0;
	/*
	 * The tacho period should be:
	 *      TACH = 60/(ppr * rpm), where rpm is revolutions per second
	 *      and ppr is pulses per revolution.
	 * Given the tacho period, we can multiply it by the input clock
	 * so that we know how many clocks we need to have this period.
	 * From this, we can derive the RPM value.
	 */
	return DIV_ROUND_CLOSEST(60 * ctl->clk_rate, ctl->ppr * tach);
}

static int axi_fan_control_read_temp(struct device *dev, u32 attr, long *val)
{
	struct axi_fan_control_data *ctl = dev_get_drvdata(dev);
	long raw_temp;

	switch (attr) {
	case hwmon_temp_input:
		raw_temp = axi_ioread(ADI_REG_TEMPERATURE, ctl);
		/*
		 * The formula for the temperature is:
		 *      T = (ADC * 501.3743 / 2^bits) - 273.6777
		 * It's multiplied by 1000 to have millidegrees as
		 * specified by the hwmon sysfs interface.
		 */
		*val = ((raw_temp * 501374) >> 16) - 273677;
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int axi_fan_control_read_fan(struct device *dev, u32 attr, long *val)
{
	struct axi_fan_control_data *ctl = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_fan_fault:
		*val = ctl->fan_fault;
		/* clear it now */
		ctl->fan_fault = 0;
		return 0;
	case hwmon_fan_input:
		*val = axi_fan_control_get_fan_rpm(ctl);
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int axi_fan_control_read_pwm(struct device *dev, u32 attr, long *val)
{
	struct axi_fan_control_data *ctl = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_pwm_input:
		*val = axi_fan_control_get_pwm_duty(ctl);
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int axi_fan_control_write_pwm(struct device *dev, u32 attr, long val)
{
	struct axi_fan_control_data *ctl = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_pwm_input:
		return axi_fan_control_set_pwm_duty(val, ctl);
	default:
		return -ENOTSUPP;
	}
}

static int axi_fan_control_read_labels(struct device *dev,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_fan:
		*str = "FAN";
		return 0;
	case hwmon_temp:
		*str = "SYSMON4";
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int axi_fan_control_read(struct device *dev,
				enum hwmon_sensor_types type,
				u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_fan:
		return axi_fan_control_read_fan(dev, attr, val);
	case hwmon_pwm:
		return axi_fan_control_read_pwm(dev, attr, val);
	case hwmon_temp:
		return axi_fan_control_read_temp(dev, attr, val);
	default:
		return -ENOTSUPP;
	}
}

static int axi_fan_control_write(struct device *dev,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_pwm:
		return axi_fan_control_write_pwm(dev, attr, val);
	default:
		return -ENOTSUPP;
	}
}

static umode_t axi_fan_control_fan_is_visible(const u32 attr)
{
	switch (attr) {
	case hwmon_fan_input:
	case hwmon_fan_fault:
	case hwmon_fan_label:
		return 0444;
	default:
		return 0;
	}
}

static umode_t axi_fan_control_pwm_is_visible(const u32 attr)
{
	switch (attr) {
	case hwmon_pwm_input:
		return 0644;
	default:
		return 0;
	}
}

static umode_t axi_fan_control_temp_is_visible(const u32 attr)
{
	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_label:
		return 0444;
	default:
		return 0;
	}
}

static umode_t axi_fan_control_is_visible(const void *data,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		return axi_fan_control_fan_is_visible(attr);
	case hwmon_pwm:
		return axi_fan_control_pwm_is_visible(attr);
	case hwmon_temp:
		return axi_fan_control_temp_is_visible(attr);
	default:
		return 0;
	}
}

/*
 * This core has two main ways of changing the PWM duty cycle. It is done,
 * either by a request from userspace (writing on pwm1_input) or by the
 * core itself. When the change is done by the core, it will use predefined
 * parameters to evaluate the tach signal and, on that case we cannot set them.
 * On the other hand, when the request is done by the user, with some arbitrary
 * value that the core does not now about, we have to provide the tach
 * parameters so that, the core can evaluate the signal. On the IRQ handler we
 * distinguish this by using the ADI_IRQ_SRC_TEMP_INCREASE interrupt. This tell
 * us that the CORE requested a new duty cycle. After this, there is 5s delay
 * on which the core waits for the fan rotation speed to stabilize. After this
 * we get ADI_IRQ_SRC_PWM_CHANGED irq where we will decide if we need to set
 * the tach parameters or not on the next tach measurement cycle (corresponding
 * already to the ney duty cycle) based on the %ctl->hw_pwm_req flag.
 */
static irqreturn_t axi_fan_control_irq_handler(int irq, void *data)
{
	struct axi_fan_control_data *ctl = (struct axi_fan_control_data *)data;
	u32 irq_pending = axi_ioread(ADI_REG_IRQ_PENDING, ctl);
	u32 clear_mask;

	if (irq_pending & ADI_IRQ_SRC_TEMP_INCREASE)
		/* hardware requested a new pwm */
		ctl->hw_pwm_req = true;

	if (irq_pending & ADI_IRQ_SRC_PWM_CHANGED) {
		/*
		 * if the pwm changes on behalf of software,
		 * we need to provide new tacho parameters to the core.
		 * Wait for the next measurement for that...
		 */
		if (!ctl->hw_pwm_req) {
			ctl->update_tacho_params = true;
		} else {
			ctl->hw_pwm_req = false;
			hwmon_notify_event(ctl->hdev, hwmon_pwm,
					   hwmon_pwm_input, 0);
		}
	}

	if (irq_pending & ADI_IRQ_SRC_NEW_MEASUR) {
		if (ctl->update_tacho_params) {
			u32 new_tach = axi_ioread(ADI_REG_TACH_MEASUR, ctl);
			/* get 25% tolerance */
			u32 tach_tol = DIV_ROUND_CLOSEST(new_tach * 25, 100);

			/* set new tacho parameters */
			axi_iowrite(new_tach, ADI_REG_TACH_PERIOD, ctl);
			axi_iowrite(tach_tol, ADI_REG_TACH_TOLERANCE, ctl);
			ctl->update_tacho_params = false;
		}
	}

	if (irq_pending & ADI_IRQ_SRC_TACH_ERR)
		ctl->fan_fault = 1;

	/* clear all interrupts */
	clear_mask = irq_pending & ADI_IRQ_SRC_MASK;
	axi_iowrite(clear_mask, ADI_REG_IRQ_PENDING, ctl);

	return IRQ_HANDLED;
}

static int axi_fan_control_init(struct axi_fan_control_data *ctl,
				const struct device_node *np)
{
	int ret;

	/* get fan pulses per revolution */
	ret = of_property_read_u32(np, "pulses-per-revolution", &ctl->ppr);
	if (ret)
		return ret;

	/* 1, 2 and 4 are the typical and accepted values */
	if (ctl->ppr != 1 && ctl->ppr != 2 && ctl->ppr != 4)
		return -EINVAL;
	/*
	 * Enable all IRQs
	 */
	axi_iowrite(ADI_IRQ_MASK_OUT_ALL &
		    ~(ADI_IRQ_SRC_NEW_MEASUR | ADI_IRQ_SRC_TACH_ERR |
		      ADI_IRQ_SRC_PWM_CHANGED | ADI_IRQ_SRC_TEMP_INCREASE),
		    ADI_REG_IRQ_MASK, ctl);

	/* bring the device out of reset */
	axi_iowrite(0x01, ADI_REG_RSTN, ctl);

	return ret;
}

static const struct hwmon_channel_info * const axi_fan_control_info[] = {
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_FAULT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_ops axi_fan_control_hwmon_ops = {
	.is_visible = axi_fan_control_is_visible,
	.read = axi_fan_control_read,
	.write = axi_fan_control_write,
	.read_string = axi_fan_control_read_labels,
};

static const struct hwmon_chip_info axi_chip_info = {
	.ops = &axi_fan_control_hwmon_ops,
	.info = axi_fan_control_info,
};

/* temperature threshold below which PWM should be 0% */
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point1_temp_hyst, axi_fan_control, ADI_REG_TEMP_00_H);
/* temperature threshold above which PWM should be 25% */
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point1_temp, axi_fan_control, ADI_REG_TEMP_25_L);
/* temperature threshold below which PWM should be 25% */
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point2_temp_hyst, axi_fan_control, ADI_REG_TEMP_25_H);
/* temperature threshold above which PWM should be 50% */
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point2_temp, axi_fan_control, ADI_REG_TEMP_50_L);
/* temperature threshold below which PWM should be 50% */
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point3_temp_hyst, axi_fan_control, ADI_REG_TEMP_50_H);
/* temperature threshold above which PWM should be 75% */
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point3_temp, axi_fan_control, ADI_REG_TEMP_75_L);
/* temperature threshold below which PWM should be 75% */
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point4_temp_hyst, axi_fan_control, ADI_REG_TEMP_75_H);
/* temperature threshold above which PWM should be 100% */
static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point4_temp, axi_fan_control, ADI_REG_TEMP_100_L);

static struct attribute *axi_fan_control_attrs[] = {
	&sensor_dev_attr_pwm1_auto_point1_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_temp.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(axi_fan_control);

static const u32 version_1_0_0 = ADI_AXI_PCORE_VER(1, 0, 'a');

static const struct of_device_id axi_fan_control_of_match[] = {
	{ .compatible = "adi,axi-fan-control-1.00.a",
		.data = (void *)&version_1_0_0},
	{},
};
MODULE_DEVICE_TABLE(of, axi_fan_control_of_match);

static int axi_fan_control_probe(struct platform_device *pdev)
{
	struct axi_fan_control_data *ctl;
	struct clk *clk;
	const struct of_device_id *id;
	const char *name = "axi_fan_control";
	u32 version;
	int ret;

	id = of_match_node(axi_fan_control_of_match, pdev->dev.of_node);
	if (!id)
		return -EINVAL;

	ctl = devm_kzalloc(&pdev->dev, sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;

	ctl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ctl->base))
		return PTR_ERR(ctl->base);

	clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "clk_get failed with %ld\n", PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	ctl->clk_rate = clk_get_rate(clk);
	if (!ctl->clk_rate)
		return -EINVAL;

	version = axi_ioread(ADI_AXI_REG_VERSION, ctl);
	if (ADI_AXI_PCORE_VER_MAJOR(version) !=
	    ADI_AXI_PCORE_VER_MAJOR((*(u32 *)id->data))) {
		dev_err(&pdev->dev, "Major version mismatch. Expected %d.%.2d.%c, Reported %d.%.2d.%c\n",
			ADI_AXI_PCORE_VER_MAJOR((*(u32 *)id->data)),
			ADI_AXI_PCORE_VER_MINOR((*(u32 *)id->data)),
			ADI_AXI_PCORE_VER_PATCH((*(u32 *)id->data)),
			ADI_AXI_PCORE_VER_MAJOR(version),
			ADI_AXI_PCORE_VER_MINOR(version),
			ADI_AXI_PCORE_VER_PATCH(version));
		return -ENODEV;
	}

	ret = axi_fan_control_init(ctl, pdev->dev.of_node);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize device\n");
		return ret;
	}

	ctl->hdev = devm_hwmon_device_register_with_info(&pdev->dev,
							 name,
							 ctl,
							 &axi_chip_info,
							 axi_fan_control_groups);

	if (IS_ERR(ctl->hdev))
		return PTR_ERR(ctl->hdev);

	ctl->irq = platform_get_irq(pdev, 0);
	if (ctl->irq < 0)
		return ctl->irq;

	ret = devm_request_threaded_irq(&pdev->dev, ctl->irq, NULL,
					axi_fan_control_irq_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
					pdev->driver_override, ctl);
	if (ret) {
		dev_err(&pdev->dev, "failed to request an irq, %d", ret);
		return ret;
	}

	return 0;
}

static struct platform_driver axi_fan_control_driver = {
	.driver = {
		.name = "axi_fan_control_driver",
		.of_match_table = axi_fan_control_of_match,
	},
	.probe = axi_fan_control_probe,
};
module_platform_driver(axi_fan_control_driver);

MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("Analog Devices Fan Control HDL CORE driver");
MODULE_LICENSE("GPL");
