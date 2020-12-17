// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device tree helpers for DMA request / controller
 *
 * Based on of_gpio.c
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_dma.h>

#include "dmaengine.h"

static LIST_HEAD(of_dma_list);
static DEFINE_MUTEX(of_dma_lock);

/**
 * of_dma_find_controller - Get a DMA controller in DT DMA helpers list
 * @dma_spec:	pointer to DMA specifier as found in the device tree
 *
 * Finds a DMA controller with matching device node and number for dma cells
 * in a list of registered DMA controllers. If a match is found a valid pointer
 * to the DMA data stored is retuned. A NULL pointer is returned if no match is
 * found.
 */
static struct of_dma *of_dma_find_controller(struct of_phandle_args *dma_spec)
{
	struct of_dma *ofdma;

	list_for_each_entry(ofdma, &of_dma_list, of_dma_controllers)
		if (ofdma->of_node == dma_spec->np)
			return ofdma;

	pr_debug("%s: can't find DMA controller %pOF\n", __func__,
		 dma_spec->np);

	return NULL;
}

/**
 * of_dma_router_xlate - translation function for router devices
 * @dma_spec:	pointer to DMA specifier as found in the device tree
 * @ofdma:	pointer to DMA controller data (router information)
 *
 * The function creates new dma_spec to be passed to the router driver's
 * of_dma_route_allocate() function to prepare a dma_spec which will be used
 * to request channel from the real DMA controller.
 */
static struct dma_chan *of_dma_router_xlate(struct of_phandle_args *dma_spec,
					    struct of_dma *ofdma)
{
	struct dma_chan		*chan;
	struct of_dma		*ofdma_target;
	struct of_phandle_args	dma_spec_target;
	void			*route_data;

	/* translate the request for the real DMA controller */
	memcpy(&dma_spec_target, dma_spec, sizeof(dma_spec_target));
	route_data = ofdma->of_dma_route_allocate(&dma_spec_target, ofdma);
	if (IS_ERR(route_data))
		return NULL;

	ofdma_target = of_dma_find_controller(&dma_spec_target);
	if (!ofdma_target)
		return NULL;

	chan = ofdma_target->of_dma_xlate(&dma_spec_target, ofdma_target);
	if (IS_ERR_OR_NULL(chan)) {
		ofdma->dma_router->route_free(ofdma->dma_router->dev,
					      route_data);
	} else {
		int ret = 0;

		chan->router = ofdma->dma_router;
		chan->route_data = route_data;

		if (chan->device->device_router_config)
			ret = chan->device->device_router_config(chan);

		if (ret) {
			dma_release_channel(chan);
			chan = ERR_PTR(ret);
		}
	}

	/*
	 * Need to put the node back since the ofdma->of_dma_route_allocate
	 * has taken it for generating the new, translated dma_spec
	 */
	of_node_put(dma_spec_target.np);
	return chan;
}

/**
 * of_dma_controller_register - Register a DMA controller to DT DMA helpers
 * @np:			device node of DMA controller
 * @of_dma_xlate:	translation function which converts a phandle
 *			arguments list into a dma_chan structure
 * @data:		pointer to controller specific data to be used by
 *			translation function
 *
 * Returns 0 on success or appropriate errno value on error.
 *
 * Allocated memory should be freed with appropriate of_dma_controller_free()
 * call.
 */
int of_dma_controller_register(struct device_node *np,
				struct dma_chan *(*of_dma_xlate)
				(struct of_phandle_args *, struct of_dma *),
				void *data)
{
	struct of_dma	*ofdma;

	if (!np || !of_dma_xlate) {
		pr_err("%s: not enough information provided\n", __func__);
		return -EINVAL;
	}

	ofdma = kzalloc(sizeof(*ofdma), GFP_KERNEL);
	if (!ofdma)
		return -ENOMEM;

	ofdma->of_node = np;
	ofdma->of_dma_xlate = of_dma_xlate;
	ofdma->of_dma_data = data;

	/* Now queue of_dma controller structure in list */
	mutex_lock(&of_dma_lock);
	list_add_tail(&ofdma->of_dma_controllers, &of_dma_list);
	mutex_unlock(&of_dma_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(of_dma_controller_register);

/**
 * of_dma_controller_free - Remove a DMA controller from DT DMA helpers list
 * @np:		device node of DMA controller
 *
 * Memory allocated by of_dma_controller_register() is freed here.
 */
void of_dma_controller_free(struct device_node *np)
{
	struct of_dma *ofdma;

	mutex_lock(&of_dma_lock);

	list_for_each_entry(ofdma, &of_dma_list, of_dma_controllers)
		if (ofdma->of_node == np) {
			list_del(&ofdma->of_dma_controllers);
			kfree(ofdma);
			break;
		}

	mutex_unlock(&of_dma_lock);
}
EXPORT_SYMBOL_GPL(of_dma_controller_free);

/**
 * of_dma_router_register - Register a DMA router to DT DMA helpers as a
 *			    controller
 * @np:				device node of DMA router
 * @of_dma_route_allocate:	setup function for the router which need to
 *				modify the dma_spec for the DMA controller to
 *				use and to set up the requested route.
 * @dma_router:			pointer to dma_router structure to be used when
 *				the route need to be free up.
 *
 * Returns 0 on success or appropriate errno value on error.
 *
 * Allocated memory should be freed with appropriate of_dma_controller_free()
 * call.
 */
int of_dma_router_register(struct device_node *np,
			   void *(*of_dma_route_allocate)
			   (struct of_phandle_args *, struct of_dma *),
			   struct dma_router *dma_router)
{
	struct of_dma	*ofdma;

	if (!np || !of_dma_route_allocate || !dma_router) {
		pr_err("%s: not enough information provided\n", __func__);
		return -EINVAL;
	}

	ofdma = kzalloc(sizeof(*ofdma), GFP_KERNEL);
	if (!ofdma)
		return -ENOMEM;

	ofdma->of_node = np;
	ofdma->of_dma_xlate = of_dma_router_xlate;
	ofdma->of_dma_route_allocate = of_dma_route_allocate;
	ofdma->dma_router = dma_router;

	/* Now queue of_dma controller structure in list */
	mutex_lock(&of_dma_lock);
	list_add_tail(&ofdma->of_dma_controllers, &of_dma_list);
	mutex_unlock(&of_dma_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(of_dma_router_register);

/**
 * of_dma_match_channel - Check if a DMA specifier matches name
 * @np:		device node to look for DMA channels
 * @name:	channel name to be matched
 * @index:	index of DMA specifier in list of DMA specifiers
 * @dma_spec:	pointer to DMA specifier as found in the device tree
 *
 * Check if the DMA specifier pointed to by the index in a list of DMA
 * specifiers, matches the name provided. Returns 0 if the name matches and
 * a valid pointer to the DMA specifier is found. Otherwise returns -ENODEV.
 */
static int of_dma_match_channel(struct device_node *np, const char *name,
				int index, struct of_phandle_args *dma_spec)
{
	const char *s;

	if (of_property_read_string_index(np, "dma-names", index, &s))
		return -ENODEV;

	if (strcmp(name, s))
		return -ENODEV;

	if (of_parse_phandle_with_args(np, "dmas", "#dma-cells", index,
				       dma_spec))
		return -ENODEV;

	return 0;
}

/**
 * of_dma_request_slave_channel - Get the DMA slave channel
 * @np:		device node to get DMA request from
 * @name:	name of desired channel
 *
 * Returns pointer to appropriate DMA channel on success or an error pointer.
 */
struct dma_chan *of_dma_request_slave_channel(struct device_node *np,
					      const char *name)
{
	struct of_phandle_args	dma_spec;
	struct of_dma		*ofdma;
	struct dma_chan		*chan;
	int			count, i, start;
	int			ret_no_channel = -ENODEV;
	static atomic_t		last_index;

	if (!np || !name) {
		pr_err("%s: not enough information provided\n", __func__);
		return ERR_PTR(-ENODEV);
	}

	/* Silently fail if there is not even the "dmas" property */
	if (!of_find_property(np, "dmas", NULL))
		return ERR_PTR(-ENODEV);

	count = of_property_count_strings(np, "dma-names");
	if (count < 0) {
		pr_err("%s: dma-names property of node '%pOF' missing or empty\n",
			__func__, np);
		return ERR_PTR(-ENODEV);
	}

	/*
	 * approximate an average distribution across multiple
	 * entries with the same name
	 */
	start = atomic_inc_return(&last_index);
	for (i = 0; i < count; i++) {
		if (of_dma_match_channel(np, name,
					 (i + start) % count,
					 &dma_spec))
			continue;

		mutex_lock(&of_dma_lock);
		ofdma = of_dma_find_controller(&dma_spec);

		if (ofdma) {
			chan = ofdma->of_dma_xlate(&dma_spec, ofdma);
		} else {
			ret_no_channel = -EPROBE_DEFER;
			chan = NULL;
		}

		mutex_unlock(&of_dma_lock);

		of_node_put(dma_spec.np);

		if (chan)
			return chan;
	}

	return ERR_PTR(ret_no_channel);
}
EXPORT_SYMBOL_GPL(of_dma_request_slave_channel);

/**
 * of_dma_simple_xlate - Simple DMA engine translation function
 * @dma_spec:	pointer to DMA specifier as found in the device tree
 * @ofdma:	pointer to DMA controller data
 *
 * A simple translation function for devices that use a 32-bit value for the
 * filter_param when calling the DMA engine dma_request_channel() function.
 * Note that this translation function requires that #dma-cells is equal to 1
 * and the argument of the dma specifier is the 32-bit filter_param. Returns
 * pointer to appropriate dma channel on success or NULL on error.
 */
struct dma_chan *of_dma_simple_xlate(struct of_phandle_args *dma_spec,
						struct of_dma *ofdma)
{
	int count = dma_spec->args_count;
	struct of_dma_filter_info *info = ofdma->of_dma_data;

	if (!info || !info->filter_fn)
		return NULL;

	if (count != 1)
		return NULL;

	return __dma_request_channel(&info->dma_cap, info->filter_fn,
				     &dma_spec->args[0], dma_spec->np);
}
EXPORT_SYMBOL_GPL(of_dma_simple_xlate);

/**
 * of_dma_xlate_by_chan_id - Translate dt property to DMA channel by channel id
 * @dma_spec:	pointer to DMA specifier as found in the device tree
 * @ofdma:	pointer to DMA controller data
 *
 * This function can be used as the of xlate callback for DMA driver which wants
 * to match the channel based on the channel id. When using this xlate function
 * the #dma-cells propety of the DMA controller dt node needs to be set to 1.
 * The data parameter of of_dma_controller_register must be a pointer to the
 * dma_device struct the function should match upon.
 *
 * Returns pointer to appropriate dma channel on success or NULL on error.
 */
struct dma_chan *of_dma_xlate_by_chan_id(struct of_phandle_args *dma_spec,
					 struct of_dma *ofdma)
{
	struct dma_device *dev = ofdma->of_dma_data;
	struct dma_chan *chan, *candidate = NULL;

	if (!dev || dma_spec->args_count != 1)
		return NULL;

	list_for_each_entry(chan, &dev->channels, device_node)
		if (chan->chan_id == dma_spec->args[0]) {
			candidate = chan;
			break;
		}

	if (!candidate)
		return NULL;

	return dma_get_slave_channel(candidate);
}
EXPORT_SYMBOL_GPL(of_dma_xlate_by_chan_id);
