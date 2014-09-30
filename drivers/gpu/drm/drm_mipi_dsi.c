/*
 * MIPI DSI Bus
 *
 * Copyright (C) 2012-2013, Samsung Electronics, Co., Ltd.
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drm_mipi_dsi.h>

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <video/mipi_display.h>

static int mipi_dsi_device_match(struct device *dev, struct device_driver *drv)
{
	return of_driver_match_device(dev, drv);
}

static const struct dev_pm_ops mipi_dsi_device_pm_ops = {
	.runtime_suspend = pm_generic_runtime_suspend,
	.runtime_resume = pm_generic_runtime_resume,
	.suspend = pm_generic_suspend,
	.resume = pm_generic_resume,
	.freeze = pm_generic_freeze,
	.thaw = pm_generic_thaw,
	.poweroff = pm_generic_poweroff,
	.restore = pm_generic_restore,
};

static struct bus_type mipi_dsi_bus_type = {
	.name = "mipi-dsi",
	.match = mipi_dsi_device_match,
	.pm = &mipi_dsi_device_pm_ops,
};

static void mipi_dsi_dev_release(struct device *dev)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	of_node_put(dev->of_node);
	kfree(dsi);
}

static const struct device_type mipi_dsi_device_type = {
	.release = mipi_dsi_dev_release,
};

static struct mipi_dsi_device *mipi_dsi_device_alloc(struct mipi_dsi_host *host)
{
	struct mipi_dsi_device *dsi;

	dsi = kzalloc(sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return ERR_PTR(-ENOMEM);

	dsi->host = host;
	dsi->dev.bus = &mipi_dsi_bus_type;
	dsi->dev.parent = host->dev;
	dsi->dev.type = &mipi_dsi_device_type;

	device_initialize(&dsi->dev);

	return dsi;
}

static int mipi_dsi_device_add(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_host *host = dsi->host;

	dev_set_name(&dsi->dev, "%s.%d", dev_name(host->dev),  dsi->channel);

	return device_add(&dsi->dev);
}

static struct mipi_dsi_device *
of_mipi_dsi_device_add(struct mipi_dsi_host *host, struct device_node *node)
{
	struct mipi_dsi_device *dsi;
	struct device *dev = host->dev;
	int ret;
	u32 reg;

	ret = of_property_read_u32(node, "reg", &reg);
	if (ret) {
		dev_err(dev, "device node %s has no valid reg property: %d\n",
			node->full_name, ret);
		return ERR_PTR(-EINVAL);
	}

	if (reg > 3) {
		dev_err(dev, "device node %s has invalid reg property: %u\n",
			node->full_name, reg);
		return ERR_PTR(-EINVAL);
	}

	dsi = mipi_dsi_device_alloc(host);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to allocate DSI device %s: %ld\n",
			node->full_name, PTR_ERR(dsi));
		return dsi;
	}

	dsi->dev.of_node = of_node_get(node);
	dsi->channel = reg;

	ret = mipi_dsi_device_add(dsi);
	if (ret) {
		dev_err(dev, "failed to add DSI device %s: %d\n",
			node->full_name, ret);
		kfree(dsi);
		return ERR_PTR(ret);
	}

	return dsi;
}

int mipi_dsi_host_register(struct mipi_dsi_host *host)
{
	struct device_node *node;

	for_each_available_child_of_node(host->dev->of_node, node) {
		/* skip nodes without reg property */
		if (!of_find_property(node, "reg", NULL))
			continue;
		of_mipi_dsi_device_add(host, node);
	}

	return 0;
}
EXPORT_SYMBOL(mipi_dsi_host_register);

static int mipi_dsi_remove_device_fn(struct device *dev, void *priv)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	device_unregister(&dsi->dev);

	return 0;
}

void mipi_dsi_host_unregister(struct mipi_dsi_host *host)
{
	device_for_each_child(host->dev, NULL, mipi_dsi_remove_device_fn);
}
EXPORT_SYMBOL(mipi_dsi_host_unregister);

/**
 * mipi_dsi_attach - attach a DSI device to its DSI host
 * @dsi: DSI peripheral
 */
int mipi_dsi_attach(struct mipi_dsi_device *dsi)
{
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;

	if (!ops || !ops->attach)
		return -ENOSYS;

	return ops->attach(dsi->host, dsi);
}
EXPORT_SYMBOL(mipi_dsi_attach);

/**
 * mipi_dsi_detach - detach a DSI device from its DSI host
 * @dsi: DSI peripheral
 */
int mipi_dsi_detach(struct mipi_dsi_device *dsi)
{
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;

	if (!ops || !ops->detach)
		return -ENOSYS;

	return ops->detach(dsi->host, dsi);
}
EXPORT_SYMBOL(mipi_dsi_detach);

/**
 * mipi_dsi_dcs_write - send DCS write command
 * @dsi: DSI device
 * @data: pointer to the command followed by parameters
 * @len: length of @data
 */
ssize_t mipi_dsi_dcs_write(struct mipi_dsi_device *dsi, const void *data,
			    size_t len)
{
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.tx_buf = data,
		.tx_len = len
	};

	if (!ops || !ops->transfer)
		return -ENOSYS;

	switch (len) {
	case 0:
		return -EINVAL;
	case 1:
		msg.type = MIPI_DSI_DCS_SHORT_WRITE;
		break;
	case 2:
		msg.type = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
		break;
	default:
		msg.type = MIPI_DSI_DCS_LONG_WRITE;
		break;
	}

	return ops->transfer(dsi->host, &msg);
}
EXPORT_SYMBOL(mipi_dsi_dcs_write);

/**
 * mipi_dsi_dcs_read - send DCS read request command
 * @dsi: DSI device
 * @cmd: DCS read command
 * @data: pointer to read buffer
 * @len: length of @data
 *
 * Function returns number of read bytes or error code.
 */
ssize_t mipi_dsi_dcs_read(struct mipi_dsi_device *dsi, u8 cmd, void *data,
			  size_t len)
{
	const struct mipi_dsi_host_ops *ops = dsi->host->ops;
	struct mipi_dsi_msg msg = {
		.channel = dsi->channel,
		.type = MIPI_DSI_DCS_READ,
		.tx_buf = &cmd,
		.tx_len = 1,
		.rx_buf = data,
		.rx_len = len
	};

	if (!ops || !ops->transfer)
		return -ENOSYS;

	return ops->transfer(dsi->host, &msg);
}
EXPORT_SYMBOL(mipi_dsi_dcs_read);

static int mipi_dsi_drv_probe(struct device *dev)
{
	struct mipi_dsi_driver *drv = to_mipi_dsi_driver(dev->driver);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	return drv->probe(dsi);
}

static int mipi_dsi_drv_remove(struct device *dev)
{
	struct mipi_dsi_driver *drv = to_mipi_dsi_driver(dev->driver);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	return drv->remove(dsi);
}

static void mipi_dsi_drv_shutdown(struct device *dev)
{
	struct mipi_dsi_driver *drv = to_mipi_dsi_driver(dev->driver);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);

	drv->shutdown(dsi);
}

/**
 * mipi_dsi_driver_register - register a driver for DSI devices
 * @drv: DSI driver structure
 */
int mipi_dsi_driver_register(struct mipi_dsi_driver *drv)
{
	drv->driver.bus = &mipi_dsi_bus_type;
	if (drv->probe)
		drv->driver.probe = mipi_dsi_drv_probe;
	if (drv->remove)
		drv->driver.remove = mipi_dsi_drv_remove;
	if (drv->shutdown)
		drv->driver.shutdown = mipi_dsi_drv_shutdown;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(mipi_dsi_driver_register);

/**
 * mipi_dsi_driver_unregister - unregister a driver for DSI devices
 * @drv: DSI driver structure
 */
void mipi_dsi_driver_unregister(struct mipi_dsi_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(mipi_dsi_driver_unregister);

static int __init mipi_dsi_bus_init(void)
{
	return bus_register(&mipi_dsi_bus_type);
}
postcore_initcall(mipi_dsi_bus_init);

MODULE_AUTHOR("Andrzej Hajda <a.hajda@samsung.com>");
MODULE_DESCRIPTION("MIPI DSI Bus");
MODULE_LICENSE("GPL and additional rights");
