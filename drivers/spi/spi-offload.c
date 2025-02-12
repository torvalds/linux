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
#include <linux/dmaengine.h>
#include <linux/export.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/spi/offload/consumer.h>
#include <linux/spi/offload/provider.h>
#include <linux/spi/offload/types.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

struct spi_controller_and_offload {
	struct spi_controller *controller;
	struct spi_offload *offload;
};

struct spi_offload_trigger {
	struct list_head list;
	struct kref ref;
	struct fwnode_handle *fwnode;
	/* synchronizes calling ops and driver registration */
	struct mutex lock;
	/*
	 * If the provider goes away while the consumer still has a reference,
	 * ops and priv will be set to NULL and all calls will fail with -ENODEV.
	 */
	const struct spi_offload_trigger_ops *ops;
	void *priv;
};

static LIST_HEAD(spi_offload_triggers);
static DEFINE_MUTEX(spi_offload_triggers_lock);

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
	struct spi_offload *offload;
	int ret;

	if (!spi || !config)
		return ERR_PTR(-EINVAL);

	if (!spi->controller->get_offload)
		return ERR_PTR(-ENODEV);

	resource = kzalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		return ERR_PTR(-ENOMEM);

	offload = spi->controller->get_offload(spi, config);
	if (IS_ERR(offload)) {
		kfree(resource);
		return offload;
	}

	resource->controller = spi->controller;
	resource->offload = offload;

	ret = devm_add_action_or_reset(dev, spi_offload_put, resource);
	if (ret)
		return ERR_PTR(ret);

	return offload;
}
EXPORT_SYMBOL_GPL(devm_spi_offload_get);

static void spi_offload_trigger_free(struct kref *ref)
{
	struct spi_offload_trigger *trigger =
		container_of(ref, struct spi_offload_trigger, ref);

	mutex_destroy(&trigger->lock);
	fwnode_handle_put(trigger->fwnode);
	kfree(trigger);
}

static void spi_offload_trigger_put(void *data)
{
	struct spi_offload_trigger *trigger = data;

	scoped_guard(mutex, &trigger->lock)
		if (trigger->ops && trigger->ops->release)
			trigger->ops->release(trigger);

	kref_put(&trigger->ref, spi_offload_trigger_free);
}

static struct spi_offload_trigger
*spi_offload_trigger_get(enum spi_offload_trigger_type type,
			 struct fwnode_reference_args *args)
{
	struct spi_offload_trigger *trigger;
	bool match = false;
	int ret;

	guard(mutex)(&spi_offload_triggers_lock);

	list_for_each_entry(trigger, &spi_offload_triggers, list) {
		if (trigger->fwnode != args->fwnode)
			continue;

		match = trigger->ops->match(trigger, type, args->args, args->nargs);
		if (match)
			break;
	}

	if (!match)
		return ERR_PTR(-EPROBE_DEFER);

	guard(mutex)(&trigger->lock);

	if (!trigger->ops)
		return ERR_PTR(-ENODEV);

	if (trigger->ops->request) {
		ret = trigger->ops->request(trigger, type, args->args, args->nargs);
		if (ret)
			return ERR_PTR(ret);
	}

	kref_get(&trigger->ref);

	return trigger;
}

/**
 * devm_spi_offload_trigger_get() - Get an offload trigger instance
 * @dev: Device for devm purposes.
 * @offload: Offload instance connected to a trigger.
 * @type: Trigger type to get.
 *
 * Return: Offload trigger instance or error on failure.
 */
struct spi_offload_trigger
*devm_spi_offload_trigger_get(struct device *dev,
			      struct spi_offload *offload,
			      enum spi_offload_trigger_type type)
{
	struct spi_offload_trigger *trigger;
	struct fwnode_reference_args args;
	int ret;

	ret = fwnode_property_get_reference_args(dev_fwnode(offload->provider_dev),
						 "trigger-sources",
						 "#trigger-source-cells", 0, 0,
						 &args);
	if (ret)
		return ERR_PTR(ret);

	trigger = spi_offload_trigger_get(type, &args);
	fwnode_handle_put(args.fwnode);
	if (IS_ERR(trigger))
		return trigger;

	ret = devm_add_action_or_reset(dev, spi_offload_trigger_put, trigger);
	if (ret)
		return ERR_PTR(ret);

	return trigger;
}
EXPORT_SYMBOL_GPL(devm_spi_offload_trigger_get);

/**
 * spi_offload_trigger_validate - Validate the requested trigger
 * @trigger: Offload trigger instance
 * @config: Trigger config to validate
 *
 * On success, @config may be modifed to reflect what the hardware can do.
 * For example, the frequency of a periodic trigger may be adjusted to the
 * nearest supported value.
 *
 * Callers will likely need to do additional validation of the modified trigger
 * parameters.
 *
 * Return: 0 on success, negative error code on failure.
 */
int spi_offload_trigger_validate(struct spi_offload_trigger *trigger,
				 struct spi_offload_trigger_config *config)
{
	guard(mutex)(&trigger->lock);

	if (!trigger->ops)
		return -ENODEV;

	if (!trigger->ops->validate)
		return -EOPNOTSUPP;

	return trigger->ops->validate(trigger, config);
}
EXPORT_SYMBOL_GPL(spi_offload_trigger_validate);

/**
 * spi_offload_trigger_enable - enables trigger for offload
 * @offload: Offload instance
 * @trigger: Offload trigger instance
 * @config: Trigger config to validate
 *
 * There must be a prepared offload instance with the specified ID (i.e.
 * spi_optimize_message() was called with the same offload assigned to the
 * message). This will also reserve the bus for exclusive use by the offload
 * instance until the trigger is disabled. Any other attempts to send a
 * transfer or lock the bus will fail with -EBUSY during this time.
 *
 * Calls must be balanced with spi_offload_trigger_disable().
 *
 * Context: can sleep
 * Return: 0 on success, else a negative error code.
 */
int spi_offload_trigger_enable(struct spi_offload *offload,
			       struct spi_offload_trigger *trigger,
			       struct spi_offload_trigger_config *config)
{
	int ret;

	guard(mutex)(&trigger->lock);

	if (!trigger->ops)
		return -ENODEV;

	if (offload->ops && offload->ops->trigger_enable) {
		ret = offload->ops->trigger_enable(offload);
		if (ret)
			return ret;
	}

	if (trigger->ops->enable) {
		ret = trigger->ops->enable(trigger, config);
		if (ret) {
			if (offload->ops->trigger_disable)
				offload->ops->trigger_disable(offload);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(spi_offload_trigger_enable);

/**
 * spi_offload_trigger_disable - disables hardware trigger for offload
 * @offload: Offload instance
 * @trigger: Offload trigger instance
 *
 * Disables the hardware trigger for the offload instance with the specified ID
 * and releases the bus for use by other clients.
 *
 * Context: can sleep
 */
void spi_offload_trigger_disable(struct spi_offload *offload,
				 struct spi_offload_trigger *trigger)
{
	if (offload->ops && offload->ops->trigger_disable)
		offload->ops->trigger_disable(offload);

	guard(mutex)(&trigger->lock);

	if (!trigger->ops)
		return;

	if (trigger->ops->disable)
		trigger->ops->disable(trigger);
}
EXPORT_SYMBOL_GPL(spi_offload_trigger_disable);

static void spi_offload_release_dma_chan(void *chan)
{
	dma_release_channel(chan);
}

/**
 * devm_spi_offload_tx_stream_request_dma_chan - Get the DMA channel info for the TX stream
 * @dev: Device for devm purposes.
 * @offload: Offload instance
 *
 * This is the DMA channel that will provide data to transfers that use the
 * %SPI_OFFLOAD_XFER_TX_STREAM offload flag.
 *
 * Return: Pointer to DMA channel info, or negative error code
 */
struct dma_chan
*devm_spi_offload_tx_stream_request_dma_chan(struct device *dev,
					     struct spi_offload *offload)
{
	struct dma_chan *chan;
	int ret;

	if (!offload->ops || !offload->ops->tx_stream_request_dma_chan)
		return ERR_PTR(-EOPNOTSUPP);

	chan = offload->ops->tx_stream_request_dma_chan(offload);
	if (IS_ERR(chan))
		return chan;

	ret = devm_add_action_or_reset(dev, spi_offload_release_dma_chan, chan);
	if (ret)
		return ERR_PTR(ret);

	return chan;
}
EXPORT_SYMBOL_GPL(devm_spi_offload_tx_stream_request_dma_chan);

/**
 * devm_spi_offload_rx_stream_request_dma_chan - Get the DMA channel info for the RX stream
 * @dev: Device for devm purposes.
 * @offload: Offload instance
 *
 * This is the DMA channel that will receive data from transfers that use the
 * %SPI_OFFLOAD_XFER_RX_STREAM offload flag.
 *
 * Return: Pointer to DMA channel info, or negative error code
 */
struct dma_chan
*devm_spi_offload_rx_stream_request_dma_chan(struct device *dev,
					     struct spi_offload *offload)
{
	struct dma_chan *chan;
	int ret;

	if (!offload->ops || !offload->ops->rx_stream_request_dma_chan)
		return ERR_PTR(-EOPNOTSUPP);

	chan = offload->ops->rx_stream_request_dma_chan(offload);
	if (IS_ERR(chan))
		return chan;

	ret = devm_add_action_or_reset(dev, spi_offload_release_dma_chan, chan);
	if (ret)
		return ERR_PTR(ret);

	return chan;
}
EXPORT_SYMBOL_GPL(devm_spi_offload_rx_stream_request_dma_chan);

/* Triggers providers */

static void spi_offload_trigger_unregister(void *data)
{
	struct spi_offload_trigger *trigger = data;

	scoped_guard(mutex, &spi_offload_triggers_lock)
		list_del(&trigger->list);

	scoped_guard(mutex, &trigger->lock) {
		trigger->priv = NULL;
		trigger->ops = NULL;
	}

	kref_put(&trigger->ref, spi_offload_trigger_free);
}

/**
 * devm_spi_offload_trigger_register() - Allocate and register an offload trigger
 * @dev: Device for devm purposes.
 * @info: Provider-specific trigger info.
 *
 * Return: 0 on success, else a negative error code.
 */
int devm_spi_offload_trigger_register(struct device *dev,
				      struct spi_offload_trigger_info *info)
{
	struct spi_offload_trigger *trigger;

	if (!info->fwnode || !info->ops)
		return -EINVAL;

	trigger = kzalloc(sizeof(*trigger), GFP_KERNEL);
	if (!trigger)
		return -ENOMEM;

	kref_init(&trigger->ref);
	mutex_init(&trigger->lock);
	trigger->fwnode = fwnode_handle_get(info->fwnode);
	trigger->ops = info->ops;
	trigger->priv = info->priv;

	scoped_guard(mutex, &spi_offload_triggers_lock)
		list_add_tail(&trigger->list, &spi_offload_triggers);

	return devm_add_action_or_reset(dev, spi_offload_trigger_unregister, trigger);
}
EXPORT_SYMBOL_GPL(devm_spi_offload_trigger_register);

/**
 * spi_offload_trigger_get_priv() - Get the private data for the trigger
 *
 * @trigger: Offload trigger instance.
 *
 * Return: Private data for the trigger.
 */
void *spi_offload_trigger_get_priv(struct spi_offload_trigger *trigger)
{
	return trigger->priv;
}
EXPORT_SYMBOL_GPL(spi_offload_trigger_get_priv);
