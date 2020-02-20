// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mhi.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include "internal.h"

int mhi_destroy_device(struct device *dev, void *data)
{
	struct mhi_device *mhi_dev;
	struct mhi_controller *mhi_cntrl;

	if (dev->bus != &mhi_bus_type)
		return 0;

	mhi_dev = to_mhi_device(dev);
	mhi_cntrl = mhi_dev->mhi_cntrl;

	/* Only destroy virtual devices thats attached to bus */
	if (mhi_dev->dev_type == MHI_DEVICE_CONTROLLER)
		return 0;

	dev_dbg(&mhi_cntrl->mhi_dev->dev, "destroy device for chan:%s\n",
		 mhi_dev->chan_name);

	/* Notify the client and remove the device from MHI bus */
	device_del(dev);
	put_device(dev);

	return 0;
}

static void mhi_notify(struct mhi_device *mhi_dev, enum mhi_callback cb_reason)
{
	struct mhi_driver *mhi_drv;

	if (!mhi_dev->dev.driver)
		return;

	mhi_drv = to_mhi_driver(mhi_dev->dev.driver);

	if (mhi_drv->status_cb)
		mhi_drv->status_cb(mhi_dev, cb_reason);
}

/* Bind MHI channels to MHI devices */
void mhi_create_devices(struct mhi_controller *mhi_cntrl)
{
	struct mhi_chan *mhi_chan;
	struct mhi_device *mhi_dev;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	int i, ret;

	mhi_chan = mhi_cntrl->mhi_chan;
	for (i = 0; i < mhi_cntrl->max_chan; i++, mhi_chan++) {
		if (!mhi_chan->configured || mhi_chan->mhi_dev ||
		    !(mhi_chan->ee_mask & BIT(mhi_cntrl->ee)))
			continue;
		mhi_dev = mhi_alloc_device(mhi_cntrl);
		if (!mhi_dev)
			return;

		mhi_dev->dev_type = MHI_DEVICE_XFER;
		switch (mhi_chan->dir) {
		case DMA_TO_DEVICE:
			mhi_dev->ul_chan = mhi_chan;
			mhi_dev->ul_chan_id = mhi_chan->chan;
			break;
		case DMA_FROM_DEVICE:
			/* We use dl_chan as offload channels */
			mhi_dev->dl_chan = mhi_chan;
			mhi_dev->dl_chan_id = mhi_chan->chan;
			break;
		default:
			dev_err(dev, "Direction not supported\n");
			put_device(&mhi_dev->dev);
			return;
		}

		get_device(&mhi_dev->dev);
		mhi_chan->mhi_dev = mhi_dev;

		/* Check next channel if it matches */
		if ((i + 1) < mhi_cntrl->max_chan && mhi_chan[1].configured) {
			if (!strcmp(mhi_chan[1].name, mhi_chan->name)) {
				i++;
				mhi_chan++;
				if (mhi_chan->dir == DMA_TO_DEVICE) {
					mhi_dev->ul_chan = mhi_chan;
					mhi_dev->ul_chan_id = mhi_chan->chan;
				} else {
					mhi_dev->dl_chan = mhi_chan;
					mhi_dev->dl_chan_id = mhi_chan->chan;
				}
				get_device(&mhi_dev->dev);
				mhi_chan->mhi_dev = mhi_dev;
			}
		}

		/* Channel name is same for both UL and DL */
		mhi_dev->chan_name = mhi_chan->name;
		dev_set_name(&mhi_dev->dev, "%04x_%s", mhi_chan->chan,
			     mhi_dev->chan_name);

		/* Init wakeup source if available */
		if (mhi_dev->dl_chan && mhi_dev->dl_chan->wake_capable)
			device_init_wakeup(&mhi_dev->dev, true);

		ret = device_add(&mhi_dev->dev);
		if (ret)
			put_device(&mhi_dev->dev);
	}
}
