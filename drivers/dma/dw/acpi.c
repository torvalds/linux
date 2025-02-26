// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2013,2019 Intel Corporation

#include <linux/acpi.h>
#include <linux/acpi_dma.h>

#include "internal.h"

static bool dw_dma_acpi_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma *dw = to_dw_dma(chan->device);
	struct dw_dma_chip_pdata *data = dev_get_drvdata(dw->dma.dev);
	struct acpi_dma_spec *dma_spec = param;
	struct dw_dma_slave slave = {
		.dma_dev = dma_spec->dev,
		.src_id = dma_spec->slave_id,
		.dst_id = dma_spec->slave_id,
		.m_master = data->m_master,
		.p_master = data->p_master,
	};

	return dw_dma_filter(chan, &slave);
}

void dw_dma_acpi_controller_register(struct dw_dma *dw)
{
	struct device *dev = dw->dma.dev;
	struct acpi_dma_filter_info *info;
	int ret;

	if (!has_acpi_companion(dev))
		return;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return;

	dma_cap_zero(info->dma_cap);
	dma_cap_set(DMA_SLAVE, info->dma_cap);
	info->filter_fn = dw_dma_acpi_filter;

	ret = acpi_dma_controller_register(dev, acpi_dma_simple_xlate, info);
	if (ret)
		dev_err(dev, "could not register acpi_dma_controller\n");
}
EXPORT_SYMBOL_GPL(dw_dma_acpi_controller_register);

void dw_dma_acpi_controller_free(struct dw_dma *dw)
{
	struct device *dev = dw->dma.dev;

	if (!has_acpi_companion(dev))
		return;

	acpi_dma_controller_free(dev);
}
EXPORT_SYMBOL_GPL(dw_dma_acpi_controller_free);
