// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP Remote Processor driver
 *
 * Copyright (C) 2011-2020 Texas Instruments Incorporated - http://www.ti.com/
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 * Fernando Guzman Lugo <fernando.lugo@ti.com>
 * Mark Grosen <mgrosen@ti.com>
 * Suman Anna <s-anna@ti.com>
 * Hari Kanigeri <h-kanigeri2@ti.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/remoteproc.h>
#include <linux/mailbox_client.h>
#include <linux/omap-mailbox.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/reset.h>

#include "omap_remoteproc.h"
#include "remoteproc_internal.h"

/**
 * struct omap_rproc_boot_data - boot data structure for the DSP omap rprocs
 * @syscon: regmap handle for the system control configuration module
 * @boot_reg: boot register offset within the @syscon regmap
 */
struct omap_rproc_boot_data {
	struct regmap *syscon;
	unsigned int boot_reg;
};

/**
 * struct omap_rproc - omap remote processor state
 * @mbox: mailbox channel handle
 * @client: mailbox client to request the mailbox channel
 * @boot_data: boot data structure for setting processor boot address
 * @rproc: rproc handle
 * @reset: reset handle
 */
struct omap_rproc {
	struct mbox_chan *mbox;
	struct mbox_client client;
	struct omap_rproc_boot_data *boot_data;
	struct rproc *rproc;
	struct reset_control *reset;
};

/**
 * struct omap_rproc_dev_data - device data for the omap remote processor
 * @device_name: device name of the remote processor
 */
struct omap_rproc_dev_data {
	const char *device_name;
};

/**
 * omap_rproc_mbox_callback() - inbound mailbox message handler
 * @client: mailbox client pointer used for requesting the mailbox channel
 * @data: mailbox payload
 *
 * This handler is invoked by omap's mailbox driver whenever a mailbox
 * message is received. Usually, the mailbox payload simply contains
 * the index of the virtqueue that is kicked by the remote processor,
 * and we let remoteproc core handle it.
 *
 * In addition to virtqueue indices, we also have some out-of-band values
 * that indicates different events. Those values are deliberately very
 * big so they don't coincide with virtqueue indices.
 */
static void omap_rproc_mbox_callback(struct mbox_client *client, void *data)
{
	struct omap_rproc *oproc = container_of(client, struct omap_rproc,
						client);
	struct device *dev = oproc->rproc->dev.parent;
	const char *name = oproc->rproc->name;
	u32 msg = (u32)data;

	dev_dbg(dev, "mbox msg: 0x%x\n", msg);

	switch (msg) {
	case RP_MBOX_CRASH:
		/* just log this for now. later, we'll also do recovery */
		dev_err(dev, "omap rproc %s crashed\n", name);
		break;
	case RP_MBOX_ECHO_REPLY:
		dev_info(dev, "received echo reply from %s\n", name);
		break;
	default:
		/* msg contains the index of the triggered vring */
		if (rproc_vq_interrupt(oproc->rproc, msg) == IRQ_NONE)
			dev_dbg(dev, "no message was found in vqid %d\n", msg);
	}
}

/* kick a virtqueue */
static void omap_rproc_kick(struct rproc *rproc, int vqid)
{
	struct omap_rproc *oproc = rproc->priv;
	struct device *dev = rproc->dev.parent;
	int ret;

	/* send the index of the triggered virtqueue in the mailbox payload */
	ret = mbox_send_message(oproc->mbox, (void *)vqid);
	if (ret < 0)
		dev_err(dev, "failed to send mailbox message, status = %d\n",
			ret);
}

/**
 * omap_rproc_write_dsp_boot_addr() - set boot address for DSP remote processor
 * @rproc: handle of a remote processor
 *
 * Set boot address for a supported DSP remote processor.
 */
static void omap_rproc_write_dsp_boot_addr(struct rproc *rproc)
{
	struct omap_rproc *oproc = rproc->priv;
	struct omap_rproc_boot_data *bdata = oproc->boot_data;
	u32 offset = bdata->boot_reg;

	regmap_write(bdata->syscon, offset, rproc->bootaddr);
}

/*
 * Power up the remote processor.
 *
 * This function will be invoked only after the firmware for this rproc
 * was loaded, parsed successfully, and all of its resource requirements
 * were met.
 */
static int omap_rproc_start(struct rproc *rproc)
{
	struct omap_rproc *oproc = rproc->priv;
	struct device *dev = rproc->dev.parent;
	int ret;
	struct mbox_client *client = &oproc->client;

	if (oproc->boot_data)
		omap_rproc_write_dsp_boot_addr(rproc);

	client->dev = dev;
	client->tx_done = NULL;
	client->rx_callback = omap_rproc_mbox_callback;
	client->tx_block = false;
	client->knows_txdone = false;

	oproc->mbox = mbox_request_channel(client, 0);
	if (IS_ERR(oproc->mbox)) {
		ret = -EBUSY;
		dev_err(dev, "mbox_request_channel failed: %ld\n",
			PTR_ERR(oproc->mbox));
		return ret;
	}

	/*
	 * Ping the remote processor. this is only for sanity-sake;
	 * there is no functional effect whatsoever.
	 *
	 * Note that the reply will _not_ arrive immediately: this message
	 * will wait in the mailbox fifo until the remote processor is booted.
	 */
	ret = mbox_send_message(oproc->mbox, (void *)RP_MBOX_ECHO_REQUEST);
	if (ret < 0) {
		dev_err(dev, "mbox_send_message failed: %d\n", ret);
		goto put_mbox;
	}

	ret = reset_control_deassert(oproc->reset);
	if (ret) {
		dev_err(dev, "reset control deassert failed: %d\n", ret);
		goto put_mbox;
	}

	return 0;

put_mbox:
	mbox_free_channel(oproc->mbox);
	return ret;
}

/* power off the remote processor */
static int omap_rproc_stop(struct rproc *rproc)
{
	struct omap_rproc *oproc = rproc->priv;
	int ret;

	ret = reset_control_assert(oproc->reset);
	if (ret)
		return ret;

	mbox_free_channel(oproc->mbox);

	return 0;
}

static const struct rproc_ops omap_rproc_ops = {
	.start		= omap_rproc_start,
	.stop		= omap_rproc_stop,
	.kick		= omap_rproc_kick,
};

static const struct omap_rproc_dev_data omap4_dsp_dev_data = {
	.device_name	= "dsp",
};

static const struct omap_rproc_dev_data omap4_ipu_dev_data = {
	.device_name	= "ipu",
};

static const struct omap_rproc_dev_data omap5_dsp_dev_data = {
	.device_name	= "dsp",
};

static const struct omap_rproc_dev_data omap5_ipu_dev_data = {
	.device_name	= "ipu",
};

static const struct of_device_id omap_rproc_of_match[] = {
	{
		.compatible     = "ti,omap4-dsp",
		.data           = &omap4_dsp_dev_data,
	},
	{
		.compatible     = "ti,omap4-ipu",
		.data           = &omap4_ipu_dev_data,
	},
	{
		.compatible     = "ti,omap5-dsp",
		.data           = &omap5_dsp_dev_data,
	},
	{
		.compatible     = "ti,omap5-ipu",
		.data           = &omap5_ipu_dev_data,
	},
	{
		/* end */
	},
};
MODULE_DEVICE_TABLE(of, omap_rproc_of_match);

static const char *omap_rproc_get_firmware(struct platform_device *pdev)
{
	const char *fw_name;
	int ret;

	ret = of_property_read_string(pdev->dev.of_node, "firmware-name",
				      &fw_name);
	if (ret)
		return ERR_PTR(ret);

	return fw_name;
}

static int omap_rproc_get_boot_data(struct platform_device *pdev,
				    struct rproc *rproc)
{
	struct device_node *np = pdev->dev.of_node;
	struct omap_rproc *oproc = rproc->priv;
	const struct omap_rproc_dev_data *data;
	int ret;

	data = of_device_get_match_data(&pdev->dev);
	if (!data)
		return -ENODEV;

	if (!of_property_read_bool(np, "ti,bootreg"))
		return 0;

	oproc->boot_data = devm_kzalloc(&pdev->dev, sizeof(*oproc->boot_data),
					GFP_KERNEL);
	if (!oproc->boot_data)
		return -ENOMEM;

	oproc->boot_data->syscon =
			syscon_regmap_lookup_by_phandle(np, "ti,bootreg");
	if (IS_ERR(oproc->boot_data->syscon)) {
		ret = PTR_ERR(oproc->boot_data->syscon);
		return ret;
	}

	if (of_property_read_u32_index(np, "ti,bootreg", 1,
				       &oproc->boot_data->boot_reg)) {
		dev_err(&pdev->dev, "couldn't get the boot register\n");
		return -EINVAL;
	}

	return 0;
}

static int omap_rproc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct omap_rproc *oproc;
	struct rproc *rproc;
	const char *firmware;
	int ret;
	struct reset_control *reset;

	if (!np) {
		dev_err(&pdev->dev, "only DT-based devices are supported\n");
		return -ENODEV;
	}

	reset = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	firmware = omap_rproc_get_firmware(pdev);
	if (IS_ERR(firmware))
		return PTR_ERR(firmware);

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		return ret;
	}

	rproc = rproc_alloc(&pdev->dev, dev_name(&pdev->dev), &omap_rproc_ops,
			    firmware, sizeof(*oproc));
	if (!rproc)
		return -ENOMEM;

	oproc = rproc->priv;
	oproc->rproc = rproc;
	oproc->reset = reset;
	/* All existing OMAP IPU and DSP processors have an MMU */
	rproc->has_iommu = true;

	ret = omap_rproc_get_boot_data(pdev, rproc);
	if (ret)
		goto free_rproc;

	platform_set_drvdata(pdev, rproc);

	ret = rproc_add(rproc);
	if (ret)
		goto free_rproc;

	return 0;

free_rproc:
	rproc_free(rproc);
	return ret;
}

static int omap_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);
	rproc_free(rproc);

	return 0;
}

static struct platform_driver omap_rproc_driver = {
	.probe = omap_rproc_probe,
	.remove = omap_rproc_remove,
	.driver = {
		.name = "omap-rproc",
		.of_match_table = omap_rproc_of_match,
	},
};

module_platform_driver(omap_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OMAP Remote Processor control driver");
