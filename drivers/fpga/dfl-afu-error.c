// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Accelerated Function Unit (AFU) Error Reporting
 *
 * Copyright 2019 Intel Corporation, Inc.
 *
 * Authors:
 *   Wu Hao <hao.wu@linux.intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Mitchel Henry <henry.mitchel@intel.com>
 */

#include <linux/fpga-dfl.h>
#include <linux/uaccess.h>

#include "dfl-afu.h"

#define PORT_ERROR_MASK		0x8
#define PORT_ERROR		0x10
#define PORT_FIRST_ERROR	0x18
#define PORT_MALFORMED_REQ0	0x20
#define PORT_MALFORMED_REQ1	0x28

#define ERROR_MASK		GENMASK_ULL(63, 0)

/* mask or unmask port errors by the error mask register. */
static void __afu_port_err_mask(struct device *dev, bool mask)
{
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);

	writeq(mask ? ERROR_MASK : 0, base + PORT_ERROR_MASK);
}

static void afu_port_err_mask(struct device *dev, bool mask)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);

	mutex_lock(&pdata->lock);
	__afu_port_err_mask(dev, mask);
	mutex_unlock(&pdata->lock);
}

/* clear port errors. */
static int afu_port_err_clear(struct device *dev, u64 err)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	void __iomem *base_err, *base_hdr;
	int enable_ret = 0, ret = -EBUSY;
	u64 v;

	base_err = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);
	base_hdr = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_HEADER);

	mutex_lock(&pdata->lock);

	/*
	 * clear Port Errors
	 *
	 * - Check for AP6 State
	 * - Halt Port by keeping Port in reset
	 * - Set PORT Error mask to all 1 to mask errors
	 * - Clear all errors
	 * - Set Port mask to all 0 to enable errors
	 * - All errors start capturing new errors
	 * - Enable Port by pulling the port out of reset
	 */

	/* if device is still in AP6 power state, can not clear any error. */
	v = readq(base_hdr + PORT_HDR_STS);
	if (FIELD_GET(PORT_STS_PWR_STATE, v) == PORT_STS_PWR_STATE_AP6) {
		dev_err(dev, "Could not clear errors, device in AP6 state.\n");
		goto done;
	}

	/* Halt Port by keeping Port in reset */
	ret = __afu_port_disable(pdev);
	if (ret)
		goto done;

	/* Mask all errors */
	__afu_port_err_mask(dev, true);

	/* Clear errors if err input matches with current port errors.*/
	v = readq(base_err + PORT_ERROR);

	if (v == err) {
		writeq(v, base_err + PORT_ERROR);

		v = readq(base_err + PORT_FIRST_ERROR);
		writeq(v, base_err + PORT_FIRST_ERROR);
	} else {
		dev_warn(dev, "%s: received 0x%llx, expected 0x%llx\n",
			 __func__, v, err);
		ret = -EINVAL;
	}

	/* Clear mask */
	__afu_port_err_mask(dev, false);

	/* Enable the Port by clearing the reset */
	enable_ret = __afu_port_enable(pdev);

done:
	mutex_unlock(&pdata->lock);
	return enable_ret ? enable_ret : ret;
}

static ssize_t errors_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	void __iomem *base;
	u64 error;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);

	mutex_lock(&pdata->lock);
	error = readq(base + PORT_ERROR);
	mutex_unlock(&pdata->lock);

	return sprintf(buf, "0x%llx\n", (unsigned long long)error);
}

static ssize_t errors_store(struct device *dev, struct device_attribute *attr,
			    const char *buff, size_t count)
{
	u64 value;
	int ret;

	if (kstrtou64(buff, 0, &value))
		return -EINVAL;

	ret = afu_port_err_clear(dev, value);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(errors);

static ssize_t first_error_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	void __iomem *base;
	u64 error;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);

	mutex_lock(&pdata->lock);
	error = readq(base + PORT_FIRST_ERROR);
	mutex_unlock(&pdata->lock);

	return sprintf(buf, "0x%llx\n", (unsigned long long)error);
}
static DEVICE_ATTR_RO(first_error);

static ssize_t first_malformed_req_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	void __iomem *base;
	u64 req0, req1;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_ERROR);

	mutex_lock(&pdata->lock);
	req0 = readq(base + PORT_MALFORMED_REQ0);
	req1 = readq(base + PORT_MALFORMED_REQ1);
	mutex_unlock(&pdata->lock);

	return sprintf(buf, "0x%016llx%016llx\n",
		       (unsigned long long)req1, (unsigned long long)req0);
}
static DEVICE_ATTR_RO(first_malformed_req);

static struct attribute *port_err_attrs[] = {
	&dev_attr_errors.attr,
	&dev_attr_first_error.attr,
	&dev_attr_first_malformed_req.attr,
	NULL,
};

static umode_t port_err_attrs_visible(struct kobject *kobj,
				      struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);

	/*
	 * sysfs entries are visible only if related private feature is
	 * enumerated.
	 */
	if (!dfl_get_feature_by_id(dev, PORT_FEATURE_ID_ERROR))
		return 0;

	return attr->mode;
}

const struct attribute_group port_err_group = {
	.name       = "errors",
	.attrs      = port_err_attrs,
	.is_visible = port_err_attrs_visible,
};

static int port_err_init(struct platform_device *pdev,
			 struct dfl_feature *feature)
{
	afu_port_err_mask(&pdev->dev, false);

	return 0;
}

static void port_err_uinit(struct platform_device *pdev,
			   struct dfl_feature *feature)
{
	afu_port_err_mask(&pdev->dev, true);
}

static long
port_err_ioctl(struct platform_device *pdev, struct dfl_feature *feature,
	       unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case DFL_FPGA_PORT_ERR_GET_IRQ_NUM:
		return dfl_feature_ioctl_get_num_irqs(pdev, feature, arg);
	case DFL_FPGA_PORT_ERR_SET_IRQ:
		return dfl_feature_ioctl_set_irq(pdev, feature, arg);
	default:
		dev_dbg(&pdev->dev, "%x cmd not handled", cmd);
		return -ENODEV;
	}
}

const struct dfl_feature_id port_err_id_table[] = {
	{.id = PORT_FEATURE_ID_ERROR,},
	{0,}
};

const struct dfl_feature_ops port_err_ops = {
	.init = port_err_init,
	.uinit = port_err_uinit,
	.ioctl = port_err_ioctl,
};
