// SPDX-License-Identifier: GPL-2.0-only
/*
 * CE4100 PCI-I2C glue code for PXA's driver
 * Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * The CE4100's I2C device is more or less the same one as found on PXA.
 * It does not support slave mode, the register slightly moved. This PCI
 * device provides three bars, every contains a single I2C controller.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/platform_data/i2c-pxa.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

#define CE4100_PCI_I2C_DEVS	3

struct ce4100_devices {
	struct platform_device *pdev[CE4100_PCI_I2C_DEVS];
};

static struct platform_device *add_i2c_device(struct pci_dev *dev, int bar)
{
	struct platform_device *pdev;
	struct i2c_pxa_platform_data pdata;
	struct resource res[2];
	struct device_node *child;
	static int devnum;
	int ret;

	memset(&pdata, 0, sizeof(struct i2c_pxa_platform_data));
	memset(&res, 0, sizeof(res));

	res[0].flags = IORESOURCE_MEM;
	res[0].start = pci_resource_start(dev, bar);
	res[0].end = pci_resource_end(dev, bar);

	res[1].flags = IORESOURCE_IRQ;
	res[1].start = dev->irq;
	res[1].end = dev->irq;

	for_each_child_of_node(dev->dev.of_node, child) {
		const void *prop;
		struct resource r;
		int ret;

		ret = of_address_to_resource(child, 0, &r);
		if (ret < 0)
			continue;
		if (r.start != res[0].start)
			continue;
		if (r.end != res[0].end)
			continue;
		if (r.flags != res[0].flags)
			continue;

		prop = of_get_property(child, "fast-mode", NULL);
		if (prop)
			pdata.fast_mode = 1;

		break;
	}

	if (!child) {
		dev_err(&dev->dev, "failed to match a DT node for bar %d.\n",
				bar);
		ret = -EINVAL;
		goto out;
	}

	pdev = platform_device_alloc("ce4100-i2c", devnum);
	if (!pdev) {
		of_node_put(child);
		ret = -ENOMEM;
		goto out;
	}
	pdev->dev.parent = &dev->dev;
	pdev->dev.of_node = child;

	ret = platform_device_add_resources(pdev, res, ARRAY_SIZE(res));
	if (ret)
		goto err;

	ret = platform_device_add_data(pdev, &pdata, sizeof(pdata));
	if (ret)
		goto err;

	ret = platform_device_add(pdev);
	if (ret)
		goto err;
	devnum++;
	return pdev;
err:
	platform_device_put(pdev);
out:
	return ERR_PTR(ret);
}

static int ce4100_i2c_probe(struct pci_dev *dev,
		const struct pci_device_id *ent)
{
	int ret;
	int i;
	struct ce4100_devices *sds;

	ret = pci_enable_device_mem(dev);
	if (ret)
		return ret;

	if (!dev->dev.of_node) {
		dev_err(&dev->dev, "Missing device tree node.\n");
		return -EINVAL;
	}
	sds = kzalloc(sizeof(*sds), GFP_KERNEL);
	if (!sds) {
		ret = -ENOMEM;
		goto err_mem;
	}

	for (i = 0; i < ARRAY_SIZE(sds->pdev); i++) {
		sds->pdev[i] = add_i2c_device(dev, i);
		if (IS_ERR(sds->pdev[i])) {
			ret = PTR_ERR(sds->pdev[i]);
			while (--i >= 0)
				platform_device_unregister(sds->pdev[i]);
			goto err_dev_add;
		}
	}
	pci_set_drvdata(dev, sds);
	return 0;

err_dev_add:
	kfree(sds);
err_mem:
	pci_disable_device(dev);
	return ret;
}

static const struct pci_device_id ce4100_i2c_devices[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2e68)},
	{ },
};

static struct pci_driver ce4100_i2c_driver = {
	.driver = {
		.suppress_bind_attrs = true,
	},
	.name           = "ce4100_i2c",
	.id_table       = ce4100_i2c_devices,
	.probe          = ce4100_i2c_probe,
};
builtin_pci_driver(ce4100_i2c_driver);
