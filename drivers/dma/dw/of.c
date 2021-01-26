// SPDX-License-Identifier: GPL-2.0
/*
 * Platform driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2007-2008 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 * Copyright (C) 2013 Intel Corporation
 */

#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>

#include "internal.h"

static struct dma_chan *dw_dma_of_xlate(struct of_phandle_args *dma_spec,
					struct of_dma *ofdma)
{
	struct dw_dma *dw = ofdma->of_dma_data;
	struct dw_dma_slave slave = {
		.dma_dev = dw->dma.dev,
	};
	dma_cap_mask_t cap;

	if (dma_spec->args_count < 3 || dma_spec->args_count > 4)
		return NULL;

	slave.src_id = dma_spec->args[0];
	slave.dst_id = dma_spec->args[0];
	slave.m_master = dma_spec->args[1];
	slave.p_master = dma_spec->args[2];
	if (dma_spec->args_count >= 4)
		slave.channels = dma_spec->args[3];

	if (WARN_ON(slave.src_id >= DW_DMA_MAX_NR_REQUESTS ||
		    slave.dst_id >= DW_DMA_MAX_NR_REQUESTS ||
		    slave.m_master >= dw->pdata->nr_masters ||
		    slave.p_master >= dw->pdata->nr_masters ||
		    slave.channels >= BIT(dw->pdata->nr_channels)))
		return NULL;

	dma_cap_zero(cap);
	dma_cap_set(DMA_SLAVE, cap);

	/* TODO: there should be a simpler way to do this */
	return dma_request_channel(cap, dw_dma_filter, &slave);
}

struct dw_dma_platform_data *dw_dma_parse_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dw_dma_platform_data *pdata;
	u32 tmp, arr[DW_DMA_MAX_NR_MASTERS], mb[DW_DMA_MAX_NR_CHANNELS];
	u32 nr_masters;
	u32 nr_channels;

	if (!np) {
		dev_err(&pdev->dev, "Missing DT data\n");
		return NULL;
	}

	if (of_property_read_u32(np, "dma-masters", &nr_masters))
		return NULL;
	if (nr_masters < 1 || nr_masters > DW_DMA_MAX_NR_MASTERS)
		return NULL;

	if (of_property_read_u32(np, "dma-channels", &nr_channels))
		return NULL;
	if (nr_channels > DW_DMA_MAX_NR_CHANNELS)
		return NULL;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	pdata->nr_masters = nr_masters;
	pdata->nr_channels = nr_channels;

	if (!of_property_read_u32(np, "chan_allocation_order", &tmp))
		pdata->chan_allocation_order = (unsigned char)tmp;

	if (!of_property_read_u32(np, "chan_priority", &tmp))
		pdata->chan_priority = tmp;

	if (!of_property_read_u32(np, "block_size", &tmp))
		pdata->block_size = tmp;

	if (!of_property_read_u32_array(np, "data-width", arr, nr_masters)) {
		for (tmp = 0; tmp < nr_masters; tmp++)
			pdata->data_width[tmp] = arr[tmp];
	} else if (!of_property_read_u32_array(np, "data_width", arr, nr_masters)) {
		for (tmp = 0; tmp < nr_masters; tmp++)
			pdata->data_width[tmp] = BIT(arr[tmp] & 0x07);
	}

	if (!of_property_read_u32_array(np, "multi-block", mb, nr_channels)) {
		for (tmp = 0; tmp < nr_channels; tmp++)
			pdata->multi_block[tmp] = mb[tmp];
	} else {
		for (tmp = 0; tmp < nr_channels; tmp++)
			pdata->multi_block[tmp] = 1;
	}

	if (of_property_read_u32_array(np, "snps,max-burst-len", pdata->max_burst,
				       nr_channels)) {
		memset32(pdata->max_burst, DW_DMA_MAX_BURST, nr_channels);
	}

	if (!of_property_read_u32(np, "snps,dma-protection-control", &tmp)) {
		if (tmp > CHAN_PROTCTL_MASK)
			return NULL;
		pdata->protctl = tmp;
	}

	return pdata;
}

void dw_dma_of_controller_register(struct dw_dma *dw)
{
	struct device *dev = dw->dma.dev;
	int ret;

	if (!dev->of_node)
		return;

	ret = of_dma_controller_register(dev->of_node, dw_dma_of_xlate, dw);
	if (ret)
		dev_err(dev, "could not register of_dma_controller\n");
}

void dw_dma_of_controller_free(struct dw_dma *dw)
{
	struct device *dev = dw->dma.dev;

	if (!dev->of_node)
		return;

	of_dma_controller_free(dev->of_node);
}
