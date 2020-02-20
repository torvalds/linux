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
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include "internal.h"

static int parse_ev_cfg(struct mhi_controller *mhi_cntrl,
			struct mhi_controller_config *config)
{
	struct mhi_event *mhi_event;
	struct mhi_event_config *event_cfg;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	int i, num;

	num = config->num_events;
	mhi_cntrl->total_ev_rings = num;
	mhi_cntrl->mhi_event = kcalloc(num, sizeof(*mhi_cntrl->mhi_event),
				       GFP_KERNEL);
	if (!mhi_cntrl->mhi_event)
		return -ENOMEM;

	/* Populate event ring */
	mhi_event = mhi_cntrl->mhi_event;
	for (i = 0; i < num; i++) {
		event_cfg = &config->event_cfg[i];

		mhi_event->er_index = i;
		mhi_event->ring.elements = event_cfg->num_elements;
		mhi_event->intmod = event_cfg->irq_moderation_ms;
		mhi_event->irq = event_cfg->irq;

		if (event_cfg->channel != U32_MAX) {
			/* This event ring has a dedicated channel */
			mhi_event->chan = event_cfg->channel;
			if (mhi_event->chan >= mhi_cntrl->max_chan) {
				dev_err(dev,
					"Event Ring channel not available\n");
				goto error_ev_cfg;
			}

			mhi_event->mhi_chan =
				&mhi_cntrl->mhi_chan[mhi_event->chan];
		}

		/* Priority is fixed to 1 for now */
		mhi_event->priority = 1;

		mhi_event->db_cfg.brstmode = event_cfg->mode;
		if (MHI_INVALID_BRSTMODE(mhi_event->db_cfg.brstmode))
			goto error_ev_cfg;

		mhi_event->data_type = event_cfg->data_type;

		mhi_event->hw_ring = event_cfg->hardware_event;
		if (mhi_event->hw_ring)
			mhi_cntrl->hw_ev_rings++;
		else
			mhi_cntrl->sw_ev_rings++;

		mhi_event->cl_manage = event_cfg->client_managed;
		mhi_event->offload_ev = event_cfg->offload_channel;
		mhi_event++;
	}

	/* We need IRQ for each event ring + additional one for BHI */
	mhi_cntrl->nr_irqs_req = mhi_cntrl->total_ev_rings + 1;

	return 0;

error_ev_cfg:

	kfree(mhi_cntrl->mhi_event);
	return -EINVAL;
}

static int parse_ch_cfg(struct mhi_controller *mhi_cntrl,
			struct mhi_controller_config *config)
{
	struct mhi_channel_config *ch_cfg;
	struct device *dev = &mhi_cntrl->mhi_dev->dev;
	int i;
	u32 chan;

	mhi_cntrl->max_chan = config->max_channels;

	/*
	 * The allocation of MHI channels can exceed 32KB in some scenarios,
	 * so to avoid any memory possible allocation failures, vzalloc is
	 * used here
	 */
	mhi_cntrl->mhi_chan = vzalloc(mhi_cntrl->max_chan *
				      sizeof(*mhi_cntrl->mhi_chan));
	if (!mhi_cntrl->mhi_chan)
		return -ENOMEM;

	INIT_LIST_HEAD(&mhi_cntrl->lpm_chans);

	/* Populate channel configurations */
	for (i = 0; i < config->num_channels; i++) {
		struct mhi_chan *mhi_chan;

		ch_cfg = &config->ch_cfg[i];

		chan = ch_cfg->num;
		if (chan >= mhi_cntrl->max_chan) {
			dev_err(dev, "Channel %d not available\n", chan);
			goto error_chan_cfg;
		}

		mhi_chan = &mhi_cntrl->mhi_chan[chan];
		mhi_chan->name = ch_cfg->name;
		mhi_chan->chan = chan;

		mhi_chan->tre_ring.elements = ch_cfg->num_elements;
		if (!mhi_chan->tre_ring.elements)
			goto error_chan_cfg;

		/*
		 * For some channels, local ring length should be bigger than
		 * the transfer ring length due to internal logical channels
		 * in device. So host can queue much more buffers than transfer
		 * ring length. Example, RSC channels should have a larger local
		 * channel length than transfer ring length.
		 */
		mhi_chan->buf_ring.elements = ch_cfg->local_elements;
		if (!mhi_chan->buf_ring.elements)
			mhi_chan->buf_ring.elements = mhi_chan->tre_ring.elements;
		mhi_chan->er_index = ch_cfg->event_ring;
		mhi_chan->dir = ch_cfg->dir;

		/*
		 * For most channels, chtype is identical to channel directions.
		 * So, if it is not defined then assign channel direction to
		 * chtype
		 */
		mhi_chan->type = ch_cfg->type;
		if (!mhi_chan->type)
			mhi_chan->type = (enum mhi_ch_type)mhi_chan->dir;

		mhi_chan->ee_mask = ch_cfg->ee_mask;
		mhi_chan->db_cfg.pollcfg = ch_cfg->pollcfg;
		mhi_chan->lpm_notify = ch_cfg->lpm_notify;
		mhi_chan->offload_ch = ch_cfg->offload_channel;
		mhi_chan->db_cfg.reset_req = ch_cfg->doorbell_mode_switch;
		mhi_chan->pre_alloc = ch_cfg->auto_queue;
		mhi_chan->auto_start = ch_cfg->auto_start;

		/*
		 * If MHI host allocates buffers, then the channel direction
		 * should be DMA_FROM_DEVICE
		 */
		if (mhi_chan->pre_alloc && mhi_chan->dir != DMA_FROM_DEVICE) {
			dev_err(dev, "Invalid channel configuration\n");
			goto error_chan_cfg;
		}

		/*
		 * Bi-directional and direction less channel must be an
		 * offload channel
		 */
		if ((mhi_chan->dir == DMA_BIDIRECTIONAL ||
		     mhi_chan->dir == DMA_NONE) && !mhi_chan->offload_ch) {
			dev_err(dev, "Invalid channel configuration\n");
			goto error_chan_cfg;
		}

		if (!mhi_chan->offload_ch) {
			mhi_chan->db_cfg.brstmode = ch_cfg->doorbell;
			if (MHI_INVALID_BRSTMODE(mhi_chan->db_cfg.brstmode)) {
				dev_err(dev, "Invalid Door bell mode\n");
				goto error_chan_cfg;
			}
		}

		mhi_chan->configured = true;

		if (mhi_chan->lpm_notify)
			list_add_tail(&mhi_chan->node, &mhi_cntrl->lpm_chans);
	}

	return 0;

error_chan_cfg:
	vfree(mhi_cntrl->mhi_chan);

	return -EINVAL;
}

static int parse_config(struct mhi_controller *mhi_cntrl,
			struct mhi_controller_config *config)
{
	int ret;

	/* Parse MHI channel configuration */
	ret = parse_ch_cfg(mhi_cntrl, config);
	if (ret)
		return ret;

	/* Parse MHI event configuration */
	ret = parse_ev_cfg(mhi_cntrl, config);
	if (ret)
		goto error_ev_cfg;

	mhi_cntrl->timeout_ms = config->timeout_ms;
	if (!mhi_cntrl->timeout_ms)
		mhi_cntrl->timeout_ms = MHI_TIMEOUT_MS;

	mhi_cntrl->bounce_buf = config->use_bounce_buf;
	mhi_cntrl->buffer_len = config->buf_len;
	if (!mhi_cntrl->buffer_len)
		mhi_cntrl->buffer_len = MHI_MAX_MTU;

	return 0;

error_ev_cfg:
	vfree(mhi_cntrl->mhi_chan);

	return ret;
}

int mhi_register_controller(struct mhi_controller *mhi_cntrl,
			    struct mhi_controller_config *config)
{
	int ret;
	int i;
	struct mhi_event *mhi_event;
	struct mhi_chan *mhi_chan;
	struct mhi_cmd *mhi_cmd;
	struct mhi_device *mhi_dev;

	if (!mhi_cntrl)
		return -EINVAL;

	if (!mhi_cntrl->runtime_get || !mhi_cntrl->runtime_put)
		return -EINVAL;

	if (!mhi_cntrl->status_cb || !mhi_cntrl->link_status)
		return -EINVAL;

	ret = parse_config(mhi_cntrl, config);
	if (ret)
		return -EINVAL;

	mhi_cntrl->mhi_cmd = kcalloc(NR_OF_CMD_RINGS,
				     sizeof(*mhi_cntrl->mhi_cmd), GFP_KERNEL);
	if (!mhi_cntrl->mhi_cmd) {
		ret = -ENOMEM;
		goto error_alloc_cmd;
	}

	INIT_LIST_HEAD(&mhi_cntrl->transition_list);
	spin_lock_init(&mhi_cntrl->transition_lock);
	spin_lock_init(&mhi_cntrl->wlock);
	init_waitqueue_head(&mhi_cntrl->state_event);

	mhi_cmd = mhi_cntrl->mhi_cmd;
	for (i = 0; i < NR_OF_CMD_RINGS; i++, mhi_cmd++)
		spin_lock_init(&mhi_cmd->lock);

	mhi_event = mhi_cntrl->mhi_event;
	for (i = 0; i < mhi_cntrl->total_ev_rings; i++, mhi_event++) {
		/* Skip for offload events */
		if (mhi_event->offload_ev)
			continue;

		mhi_event->mhi_cntrl = mhi_cntrl;
		spin_lock_init(&mhi_event->lock);
	}

	mhi_chan = mhi_cntrl->mhi_chan;
	for (i = 0; i < mhi_cntrl->max_chan; i++, mhi_chan++) {
		mutex_init(&mhi_chan->mutex);
		init_completion(&mhi_chan->completion);
		rwlock_init(&mhi_chan->lock);
	}

	/* Register controller with MHI bus */
	mhi_dev = mhi_alloc_device(mhi_cntrl);
	if (IS_ERR(mhi_dev)) {
		dev_err(mhi_cntrl->cntrl_dev, "Failed to allocate MHI device\n");
		ret = PTR_ERR(mhi_dev);
		goto error_alloc_dev;
	}

	mhi_dev->dev_type = MHI_DEVICE_CONTROLLER;
	mhi_dev->mhi_cntrl = mhi_cntrl;
	dev_set_name(&mhi_dev->dev, "%s", dev_name(mhi_cntrl->cntrl_dev));

	/* Init wakeup source */
	device_init_wakeup(&mhi_dev->dev, true);

	ret = device_add(&mhi_dev->dev);
	if (ret)
		goto error_add_dev;

	mhi_cntrl->mhi_dev = mhi_dev;

	return 0;

error_add_dev:
	put_device(&mhi_dev->dev);

error_alloc_dev:
	kfree(mhi_cntrl->mhi_cmd);

error_alloc_cmd:
	vfree(mhi_cntrl->mhi_chan);
	kfree(mhi_cntrl->mhi_event);

	return ret;
}
EXPORT_SYMBOL_GPL(mhi_register_controller);

void mhi_unregister_controller(struct mhi_controller *mhi_cntrl)
{
	struct mhi_device *mhi_dev = mhi_cntrl->mhi_dev;
	struct mhi_chan *mhi_chan = mhi_cntrl->mhi_chan;
	unsigned int i;

	kfree(mhi_cntrl->mhi_cmd);
	kfree(mhi_cntrl->mhi_event);

	/* Drop the references to MHI devices created for channels */
	for (i = 0; i < mhi_cntrl->max_chan; i++, mhi_chan++) {
		if (!mhi_chan->mhi_dev)
			continue;

		put_device(&mhi_chan->mhi_dev->dev);
	}
	vfree(mhi_cntrl->mhi_chan);

	device_del(&mhi_dev->dev);
	put_device(&mhi_dev->dev);
}
EXPORT_SYMBOL_GPL(mhi_unregister_controller);

static void mhi_release_device(struct device *dev)
{
	struct mhi_device *mhi_dev = to_mhi_device(dev);

	kfree(mhi_dev);
}

struct mhi_device *mhi_alloc_device(struct mhi_controller *mhi_cntrl)
{
	struct mhi_device *mhi_dev;
	struct device *dev;

	mhi_dev = kzalloc(sizeof(*mhi_dev), GFP_KERNEL);
	if (!mhi_dev)
		return ERR_PTR(-ENOMEM);

	dev = &mhi_dev->dev;
	device_initialize(dev);
	dev->bus = &mhi_bus_type;
	dev->release = mhi_release_device;
	dev->parent = mhi_cntrl->cntrl_dev;
	mhi_dev->mhi_cntrl = mhi_cntrl;
	mhi_dev->dev_wake = 0;

	return mhi_dev;
}

static int mhi_match(struct device *dev, struct device_driver *drv)
{
	return 0;
};

struct bus_type mhi_bus_type = {
	.name = "mhi",
	.dev_name = "mhi",
	.match = mhi_match,
};

static int __init mhi_init(void)
{
	return bus_register(&mhi_bus_type);
}

static void __exit mhi_exit(void)
{
	bus_unregister(&mhi_bus_type);
}

postcore_initcall(mhi_init);
module_exit(mhi_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MHI Host Interface");
