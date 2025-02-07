// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Analog Devices Inc.
 * Copyright (C) 2024 BayLibre, SAS
 */

/*
 * SPI Offloading support.
 *
 * Some SPI controllers support offloading of SPI transfers. Essentially, this
 * is the ability for a SPI controller to perform SPI transfers with minimal
 * or even no CPU intervention, e.g. via a specialized SPI controller with a
 * hardware trigger or via a conventional SPI controller using a non-Linux MCU
 * processor core to offload the work.
 */

#define DEFAULT_SYMBOL_NAMESPACE "SPI_OFFLOAD"

#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/offload/provider.h>
#include <linux/spi/offload/types.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

struct spi_controller_and_offload {
	struct spi_controller *controller;
	struct spi_offload *offload;
};

/**
 * devm_spi_offload_alloc() - Allocate offload instance
 * @dev: Device for devm purposes and assigned to &struct spi_offload.provider_dev
 * @priv_size: Size of private data to allocate
 *
 * Offload providers should use this to allocate offload instances.
 *
 * Return: Pointer to new offload instance or error on failure.
 */
struct spi_offload *devm_spi_offload_alloc(struct device *dev,
					   size_t priv_size)
{
	struct spi_offload *offload;
	void *priv;

	offload = devm_kzalloc(dev, sizeof(*offload), GFP_KERNEL);
	if (!offload)
		return ERR_PTR(-ENOMEM);

	priv = devm_kzalloc(dev, priv_size, GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	offload->provider_dev = dev;
	offload->priv = priv;

	return offload;
}
EXPORT_SYMBOL_GPL(devm_spi_offload_alloc);

static void spi_offload_put(void *data)
{
	struct spi_controller_and_offload *resource = data;

	resource->controller->put_offload(resource->offload);
	kfree(resource);
}

/**
 * devm_spi_offload_get() - Get an offload instance
 * @dev: Device for devm purposes
 * @spi: SPI device to use for the transfers
 * @config: Offload configuration
 *
 * Peripheral drivers call this function to get an offload instance that meets
 * the requirements specified in @config. If no suitable offload instance is
 * available, -ENODEV is returned.
 *
 * Return: Offload instance or error on failure.
 */
struct spi_offload *devm_spi_offload_get(struct device *dev,
					 struct spi_device *spi,
					 const struct spi_offload_config *config)
{
	struct spi_controller_and_offload *resource;
	int ret;

	if (!spi || !config)
		return ERR_PTR(-EINVAL);

	if (!spi->controller->get_offload)
		return ERR_PTR(-ENODEV);

	resource = kzalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		return ERR_PTR(-ENOMEM);

	resource->controller = spi->controller;
	resource->offload = spi->controller->get_offload(spi, config);
	if (IS_ERR(resource->offload)) {
		kfree(resource);
		return resource->offload;
	}

	ret = devm_add_action_or_reset(dev, spi_offload_put, resource);
	if (ret)
		return ERR_PTR(ret);

	return resource->offload;
}
EXPORT_SYMBOL_GPL(devm_spi_offload_get);
