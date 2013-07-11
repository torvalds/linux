/*
 * fpga bridge driver
 *
 *  Copyright (C) 2013 Altera Corporation, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include "fpga-bridge.h"

static DEFINE_IDA(fpga_bridge_ida);
static struct class *fpga_bridge_class;

#define FPGA_MAX_DEVICES	256

/*
 * class attributes
 */
static ssize_t fpga_bridge_enable_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct fpga_bridge *bridge = dev_get_drvdata(dev);
	int enabled;

	enabled = bridge->br_ops->enable_show(bridge);

	return sprintf(buf, "%d\n", enabled);
}

static ssize_t fpga_bridge_enable_set(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct fpga_bridge *bridge = dev_get_drvdata(dev);
	bool enable;

	if ((count != 1) && (count != 2))
		return -EINVAL;

	if ((count == 2) && (buf[1] != '\n'))
		return -EINVAL;

	if ((buf[0] != '0') && (buf[0] != '1'))
		return -EINVAL;

	enable = (buf[0] == '1');
	bridge->br_ops->enable_set(bridge, enable);

	return count;
}

static struct device_attribute fpga_bridge_attrs[] = {
	__ATTR(enable, S_IRUGO | S_IWUSR, fpga_bridge_enable_show, fpga_bridge_enable_set),
	__ATTR_NULL
};

static int fpga_bridge_alloc_id(struct fpga_bridge *bridge, int request_nr)
{
	int nr, start;

	/* check specified minor number */
	if (request_nr >= FPGA_MAX_DEVICES) {
		dev_err(bridge->parent,
			"Out of device ids (%d)\n", request_nr);
		return -ENODEV;
	}

	/*
	 * If request_nr == -1, dynamically allocate number.
	 * If request_nr >= 0, attempt to get specific number.
	 */
	if (request_nr == -1)
		start = 0;
	else
		start = request_nr;

	nr = ida_simple_get(&fpga_bridge_ida, start, FPGA_MAX_DEVICES,
			    GFP_KERNEL);

	/* return error code */
	if (nr < 0)
		return nr;

	if ((request_nr != -1) && (request_nr != nr)) {
		dev_err(bridge->parent,
			"Could not get requested device number (%d)\n", nr);
		ida_simple_remove(&fpga_bridge_ida, nr);
		return -ENODEV;
	}

	bridge->nr = nr;

	return 0;
}

static void fpga_bridge_free_id(int nr)
{
	ida_simple_remove(&fpga_bridge_ida, nr);
}

int register_fpga_bridge(struct platform_device *pdev,
			struct fpga_bridge_ops *br_ops, char *name, void *priv)
{
	struct fpga_bridge *bridge;
	const char *dt_label;
	int enable, ret;

	if (!br_ops || !br_ops->enable_set || !br_ops->enable_show) {
		dev_err(&pdev->dev,
			"Attempt to register without fpga_bridge_ops\n");
		return -EINVAL;
	}
	if (!name || (name[0] == '\0')) {
		dev_err(&pdev->dev, "Attempt to register with no name!\n");
		return -EINVAL;
	}

	bridge = kzalloc(sizeof(struct fpga_bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;

	platform_set_drvdata(pdev, bridge);
	bridge->br_ops = br_ops;
	bridge->np = pdev->dev.of_node;
	bridge->parent = get_device(&pdev->dev);
	bridge->priv = priv;

	strlcpy(bridge->name, name, sizeof(bridge->name));

	ret = fpga_bridge_alloc_id(bridge, pdev->id);
	if (ret)
		goto error_kfree;

	dt_label = of_get_property(bridge->np, "label", NULL);
	if (dt_label)
		snprintf(bridge->label, sizeof(bridge->label), "%s", dt_label);
	else
		snprintf(bridge->label, sizeof(bridge->label),
			 "br%d", bridge->nr);

	bridge->dev = device_create(fpga_bridge_class, bridge->parent, 0,
				    bridge, bridge->label);
	if (IS_ERR(bridge->dev)) {
		ret = PTR_ERR(bridge->dev);
		goto error_device;
	}

	if (!of_property_read_u32(bridge->np, "enable", &enable))
		br_ops->enable_set(bridge, enable);

	dev_info(bridge->parent, "fpga bridge [%s] registered as device %s\n",
		 bridge->name, bridge->label);

	return 0;

error_device:
	fpga_bridge_free_id(bridge->nr);
error_kfree:
	put_device(bridge->parent);
	kfree(bridge);
	return ret;
}
EXPORT_SYMBOL_GPL(register_fpga_bridge);

void remove_fpga_bridge(struct platform_device *pdev)
{
	struct fpga_bridge *bridge = platform_get_drvdata(pdev);

	if (bridge && bridge->br_ops && bridge->br_ops->fpga_bridge_remove)
		bridge->br_ops->fpga_bridge_remove(bridge);

	platform_set_drvdata(pdev, NULL);
	device_unregister(bridge->dev);
	fpga_bridge_free_id(bridge->nr);
	put_device(bridge->parent);
	kfree(bridge);
}
EXPORT_SYMBOL_GPL(remove_fpga_bridge);

static int __init fpga_bridge_dev_init(void)
{
	pr_info("fpga bridge driver\n");

	fpga_bridge_class = class_create(THIS_MODULE, "fpga-bridge");
	if (IS_ERR(fpga_bridge_class))
		return PTR_ERR(fpga_bridge_class);

	fpga_bridge_class->dev_attrs = fpga_bridge_attrs;

	return 0;
}

static void __exit fpga_bridge_dev_exit(void)
{
	class_destroy(fpga_bridge_class);
	ida_destroy(&fpga_bridge_ida);
}

MODULE_DESCRIPTION("FPGA Bridge Driver");
MODULE_AUTHOR("Alan Tull <atull@altera.com>");
MODULE_LICENSE("GPL v2");

subsys_initcall(fpga_bridge_dev_init);
module_exit(fpga_bridge_dev_exit);
