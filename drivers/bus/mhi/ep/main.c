// SPDX-License-Identifier: GPL-2.0
/*
 * MHI Endpoint bus stack
 *
 * Copyright (C) 2022 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mhi_ep.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include "internal.h"

static DEFINE_IDA(mhi_ep_cntrl_ida);

static void mhi_ep_release_device(struct device *dev)
{
	struct mhi_ep_device *mhi_dev = to_mhi_ep_device(dev);

	if (mhi_dev->dev_type == MHI_DEVICE_CONTROLLER)
		mhi_dev->mhi_cntrl->mhi_dev = NULL;

	/*
	 * We need to set the mhi_chan->mhi_dev to NULL here since the MHI
	 * devices for the channels will only get created in mhi_ep_create_device()
	 * if the mhi_dev associated with it is NULL.
	 */
	if (mhi_dev->ul_chan)
		mhi_dev->ul_chan->mhi_dev = NULL;

	if (mhi_dev->dl_chan)
		mhi_dev->dl_chan->mhi_dev = NULL;

	kfree(mhi_dev);
}

static struct mhi_ep_device *mhi_ep_alloc_device(struct mhi_ep_cntrl *mhi_cntrl,
						 enum mhi_device_type dev_type)
{
	struct mhi_ep_device *mhi_dev;
	struct device *dev;

	mhi_dev = kzalloc(sizeof(*mhi_dev), GFP_KERNEL);
	if (!mhi_dev)
		return ERR_PTR(-ENOMEM);

	dev = &mhi_dev->dev;
	device_initialize(dev);
	dev->bus = &mhi_ep_bus_type;
	dev->release = mhi_ep_release_device;

	/* Controller device is always allocated first */
	if (dev_type == MHI_DEVICE_CONTROLLER)
		/* for MHI controller device, parent is the bus device (e.g. PCI EPF) */
		dev->parent = mhi_cntrl->cntrl_dev;
	else
		/* for MHI client devices, parent is the MHI controller device */
		dev->parent = &mhi_cntrl->mhi_dev->dev;

	mhi_dev->mhi_cntrl = mhi_cntrl;
	mhi_dev->dev_type = dev_type;

	return mhi_dev;
}

static int mhi_ep_chan_init(struct mhi_ep_cntrl *mhi_cntrl,
			    const struct mhi_ep_cntrl_config *config)
{
	const struct mhi_ep_channel_config *ch_cfg;
	struct device *dev = mhi_cntrl->cntrl_dev;
	u32 chan, i;
	int ret = -EINVAL;

	mhi_cntrl->max_chan = config->max_channels;

	/*
	 * Allocate max_channels supported by the MHI endpoint and populate
	 * only the defined channels
	 */
	mhi_cntrl->mhi_chan = kcalloc(mhi_cntrl->max_chan, sizeof(*mhi_cntrl->mhi_chan),
				      GFP_KERNEL);
	if (!mhi_cntrl->mhi_chan)
		return -ENOMEM;

	for (i = 0; i < config->num_channels; i++) {
		struct mhi_ep_chan *mhi_chan;

		ch_cfg = &config->ch_cfg[i];

		chan = ch_cfg->num;
		if (chan >= mhi_cntrl->max_chan) {
			dev_err(dev, "Channel (%u) exceeds maximum available channels (%u)\n",
				chan, mhi_cntrl->max_chan);
			goto error_chan_cfg;
		}

		/* Bi-directional and direction less channels are not supported */
		if (ch_cfg->dir == DMA_BIDIRECTIONAL || ch_cfg->dir == DMA_NONE) {
			dev_err(dev, "Invalid direction (%u) for channel (%u)\n",
				ch_cfg->dir, chan);
			goto error_chan_cfg;
		}

		mhi_chan = &mhi_cntrl->mhi_chan[chan];
		mhi_chan->name = ch_cfg->name;
		mhi_chan->chan = chan;
		mhi_chan->dir = ch_cfg->dir;
		mutex_init(&mhi_chan->lock);
	}

	return 0;

error_chan_cfg:
	kfree(mhi_cntrl->mhi_chan);

	return ret;
}

/*
 * Allocate channel and command rings here. Event rings will be allocated
 * in mhi_ep_power_up() as the config comes from the host.
 */
int mhi_ep_register_controller(struct mhi_ep_cntrl *mhi_cntrl,
				const struct mhi_ep_cntrl_config *config)
{
	struct mhi_ep_device *mhi_dev;
	int ret;

	if (!mhi_cntrl || !mhi_cntrl->cntrl_dev)
		return -EINVAL;

	ret = mhi_ep_chan_init(mhi_cntrl, config);
	if (ret)
		return ret;

	mhi_cntrl->mhi_cmd = kcalloc(NR_OF_CMD_RINGS, sizeof(*mhi_cntrl->mhi_cmd), GFP_KERNEL);
	if (!mhi_cntrl->mhi_cmd) {
		ret = -ENOMEM;
		goto err_free_ch;
	}

	/* Set controller index */
	ret = ida_alloc(&mhi_ep_cntrl_ida, GFP_KERNEL);
	if (ret < 0)
		goto err_free_cmd;

	mhi_cntrl->index = ret;

	/* Allocate the controller device */
	mhi_dev = mhi_ep_alloc_device(mhi_cntrl, MHI_DEVICE_CONTROLLER);
	if (IS_ERR(mhi_dev)) {
		dev_err(mhi_cntrl->cntrl_dev, "Failed to allocate controller device\n");
		ret = PTR_ERR(mhi_dev);
		goto err_ida_free;
	}

	dev_set_name(&mhi_dev->dev, "mhi_ep%u", mhi_cntrl->index);
	mhi_dev->name = dev_name(&mhi_dev->dev);
	mhi_cntrl->mhi_dev = mhi_dev;

	ret = device_add(&mhi_dev->dev);
	if (ret)
		goto err_put_dev;

	dev_dbg(&mhi_dev->dev, "MHI EP Controller registered\n");

	return 0;

err_put_dev:
	put_device(&mhi_dev->dev);
err_ida_free:
	ida_free(&mhi_ep_cntrl_ida, mhi_cntrl->index);
err_free_cmd:
	kfree(mhi_cntrl->mhi_cmd);
err_free_ch:
	kfree(mhi_cntrl->mhi_chan);

	return ret;
}
EXPORT_SYMBOL_GPL(mhi_ep_register_controller);

void mhi_ep_unregister_controller(struct mhi_ep_cntrl *mhi_cntrl)
{
	struct mhi_ep_device *mhi_dev = mhi_cntrl->mhi_dev;

	kfree(mhi_cntrl->mhi_cmd);
	kfree(mhi_cntrl->mhi_chan);

	device_del(&mhi_dev->dev);
	put_device(&mhi_dev->dev);

	ida_free(&mhi_ep_cntrl_ida, mhi_cntrl->index);
}
EXPORT_SYMBOL_GPL(mhi_ep_unregister_controller);

static int mhi_ep_driver_probe(struct device *dev)
{
	struct mhi_ep_device *mhi_dev = to_mhi_ep_device(dev);
	struct mhi_ep_driver *mhi_drv = to_mhi_ep_driver(dev->driver);
	struct mhi_ep_chan *ul_chan = mhi_dev->ul_chan;
	struct mhi_ep_chan *dl_chan = mhi_dev->dl_chan;

	ul_chan->xfer_cb = mhi_drv->ul_xfer_cb;
	dl_chan->xfer_cb = mhi_drv->dl_xfer_cb;

	return mhi_drv->probe(mhi_dev, mhi_dev->id);
}

static int mhi_ep_driver_remove(struct device *dev)
{
	struct mhi_ep_device *mhi_dev = to_mhi_ep_device(dev);
	struct mhi_ep_driver *mhi_drv = to_mhi_ep_driver(dev->driver);
	struct mhi_result result = {};
	struct mhi_ep_chan *mhi_chan;
	int dir;

	/* Skip if it is a controller device */
	if (mhi_dev->dev_type == MHI_DEVICE_CONTROLLER)
		return 0;

	/* Disconnect the channels associated with the driver */
	for (dir = 0; dir < 2; dir++) {
		mhi_chan = dir ? mhi_dev->ul_chan : mhi_dev->dl_chan;

		if (!mhi_chan)
			continue;

		mutex_lock(&mhi_chan->lock);
		/* Send channel disconnect status to the client driver */
		if (mhi_chan->xfer_cb) {
			result.transaction_status = -ENOTCONN;
			result.bytes_xferd = 0;
			mhi_chan->xfer_cb(mhi_chan->mhi_dev, &result);
		}

		mhi_chan->state = MHI_CH_STATE_DISABLED;
		mhi_chan->xfer_cb = NULL;
		mutex_unlock(&mhi_chan->lock);
	}

	/* Remove the client driver now */
	mhi_drv->remove(mhi_dev);

	return 0;
}

int __mhi_ep_driver_register(struct mhi_ep_driver *mhi_drv, struct module *owner)
{
	struct device_driver *driver = &mhi_drv->driver;

	if (!mhi_drv->probe || !mhi_drv->remove)
		return -EINVAL;

	/* Client drivers should have callbacks defined for both channels */
	if (!mhi_drv->ul_xfer_cb || !mhi_drv->dl_xfer_cb)
		return -EINVAL;

	driver->bus = &mhi_ep_bus_type;
	driver->owner = owner;
	driver->probe = mhi_ep_driver_probe;
	driver->remove = mhi_ep_driver_remove;

	return driver_register(driver);
}
EXPORT_SYMBOL_GPL(__mhi_ep_driver_register);

void mhi_ep_driver_unregister(struct mhi_ep_driver *mhi_drv)
{
	driver_unregister(&mhi_drv->driver);
}
EXPORT_SYMBOL_GPL(mhi_ep_driver_unregister);

static int mhi_ep_match(struct device *dev, struct device_driver *drv)
{
	struct mhi_ep_device *mhi_dev = to_mhi_ep_device(dev);
	struct mhi_ep_driver *mhi_drv = to_mhi_ep_driver(drv);
	const struct mhi_device_id *id;

	/*
	 * If the device is a controller type then there is no client driver
	 * associated with it
	 */
	if (mhi_dev->dev_type == MHI_DEVICE_CONTROLLER)
		return 0;

	for (id = mhi_drv->id_table; id->chan[0]; id++)
		if (!strcmp(mhi_dev->name, id->chan)) {
			mhi_dev->id = id;
			return 1;
		}

	return 0;
};

struct bus_type mhi_ep_bus_type = {
	.name = "mhi_ep",
	.dev_name = "mhi_ep",
	.match = mhi_ep_match,
};

static int __init mhi_ep_init(void)
{
	return bus_register(&mhi_ep_bus_type);
}

static void __exit mhi_ep_exit(void)
{
	bus_unregister(&mhi_ep_bus_type);
}

postcore_initcall(mhi_ep_init);
module_exit(mhi_ep_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MHI Bus Endpoint stack");
MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
