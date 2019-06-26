/*
 *
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/bitops.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include "mmci.h"

/* Registers */
#define DML_CONFIG			0x00
#define PRODUCER_CRCI_MSK		GENMASK(1, 0)
#define PRODUCER_CRCI_DISABLE		0
#define PRODUCER_CRCI_X_SEL		BIT(0)
#define PRODUCER_CRCI_Y_SEL		BIT(1)
#define CONSUMER_CRCI_MSK		GENMASK(3, 2)
#define CONSUMER_CRCI_DISABLE		0
#define CONSUMER_CRCI_X_SEL		BIT(2)
#define CONSUMER_CRCI_Y_SEL		BIT(3)
#define PRODUCER_TRANS_END_EN		BIT(4)
#define BYPASS				BIT(16)
#define DIRECT_MODE			BIT(17)
#define INFINITE_CONS_TRANS		BIT(18)

#define DML_SW_RESET			0x08
#define DML_PRODUCER_START		0x0c
#define DML_CONSUMER_START		0x10
#define DML_PRODUCER_PIPE_LOGICAL_SIZE	0x14
#define DML_CONSUMER_PIPE_LOGICAL_SIZE	0x18
#define DML_PIPE_ID			0x1c
#define PRODUCER_PIPE_ID_SHFT		0
#define PRODUCER_PIPE_ID_MSK		GENMASK(4, 0)
#define CONSUMER_PIPE_ID_SHFT		16
#define CONSUMER_PIPE_ID_MSK		GENMASK(20, 16)

#define DML_PRODUCER_BAM_BLOCK_SIZE	0x24
#define DML_PRODUCER_BAM_TRANS_SIZE	0x28

/* other definitions */
#define PRODUCER_PIPE_LOGICAL_SIZE	4096
#define CONSUMER_PIPE_LOGICAL_SIZE	4096

#define DML_OFFSET			0x800

void dml_start_xfer(struct mmci_host *host, struct mmc_data *data)
{
	u32 config;
	void __iomem *base = host->base + DML_OFFSET;

	if (data->flags & MMC_DATA_READ) {
		/* Read operation: configure DML for producer operation */
		/* Set producer CRCI-x and disable consumer CRCI */
		config = readl_relaxed(base + DML_CONFIG);
		config = (config & ~PRODUCER_CRCI_MSK) | PRODUCER_CRCI_X_SEL;
		config = (config & ~CONSUMER_CRCI_MSK) | CONSUMER_CRCI_DISABLE;
		writel_relaxed(config, base + DML_CONFIG);

		/* Set the Producer BAM block size */
		writel_relaxed(data->blksz, base + DML_PRODUCER_BAM_BLOCK_SIZE);

		/* Set Producer BAM Transaction size */
		writel_relaxed(data->blocks * data->blksz,
			       base + DML_PRODUCER_BAM_TRANS_SIZE);
		/* Set Producer Transaction End bit */
		config = readl_relaxed(base + DML_CONFIG);
		config |= PRODUCER_TRANS_END_EN;
		writel_relaxed(config, base + DML_CONFIG);
		/* Trigger producer */
		writel_relaxed(1, base + DML_PRODUCER_START);
	} else {
		/* Write operation: configure DML for consumer operation */
		/* Set consumer CRCI-x and disable producer CRCI*/
		config = readl_relaxed(base + DML_CONFIG);
		config = (config & ~CONSUMER_CRCI_MSK) | CONSUMER_CRCI_X_SEL;
		config = (config & ~PRODUCER_CRCI_MSK) | PRODUCER_CRCI_DISABLE;
		writel_relaxed(config, base + DML_CONFIG);
		/* Clear Producer Transaction End bit */
		config = readl_relaxed(base + DML_CONFIG);
		config &= ~PRODUCER_TRANS_END_EN;
		writel_relaxed(config, base + DML_CONFIG);
		/* Trigger consumer */
		writel_relaxed(1, base + DML_CONSUMER_START);
	}

	/* make sure the dml is configured before dma is triggered */
	wmb();
}

static int of_get_dml_pipe_index(struct device_node *np, const char *name)
{
	int index;
	struct of_phandle_args	dma_spec;

	index = of_property_match_string(np, "dma-names", name);

	if (index < 0)
		return -ENODEV;

	if (of_parse_phandle_with_args(np, "dmas", "#dma-cells", index,
				       &dma_spec))
		return -ENODEV;

	if (dma_spec.args_count)
		return dma_spec.args[0];

	return -ENODEV;
}

/* Initialize the dml hardware connected to SD Card controller */
static int qcom_dma_setup(struct mmci_host *host)
{
	u32 config;
	void __iomem *base;
	int consumer_id, producer_id;
	struct device_node *np = host->mmc->parent->of_node;

	if (mmci_dmae_setup(host))
		return -EINVAL;

	consumer_id = of_get_dml_pipe_index(np, "tx");
	producer_id = of_get_dml_pipe_index(np, "rx");

	if (producer_id < 0 || consumer_id < 0) {
		host->variant->qcom_dml = false;
		mmci_dmae_release(host);
		return -EINVAL;
	}

	base = host->base + DML_OFFSET;

	/* Reset the DML block */
	writel_relaxed(1, base + DML_SW_RESET);

	/* Disable the producer and consumer CRCI */
	config = (PRODUCER_CRCI_DISABLE | CONSUMER_CRCI_DISABLE);
	/*
	 * Disable the bypass mode. Bypass mode will only be used
	 * if data transfer is to happen in PIO mode and don't
	 * want the BAM interface to connect with SDCC-DML.
	 */
	config &= ~BYPASS;
	/*
	 * Disable direct mode as we don't DML to MASTER the AHB bus.
	 * BAM connected with DML should MASTER the AHB bus.
	 */
	config &= ~DIRECT_MODE;
	/*
	 * Disable infinite mode transfer as we won't be doing any
	 * infinite size data transfers. All data transfer will be
	 * of finite data size.
	 */
	config &= ~INFINITE_CONS_TRANS;
	writel_relaxed(config, base + DML_CONFIG);

	/*
	 * Initialize the logical BAM pipe size for producer
	 * and consumer.
	 */
	writel_relaxed(PRODUCER_PIPE_LOGICAL_SIZE,
		       base + DML_PRODUCER_PIPE_LOGICAL_SIZE);
	writel_relaxed(CONSUMER_PIPE_LOGICAL_SIZE,
		       base + DML_CONSUMER_PIPE_LOGICAL_SIZE);

	/* Initialize Producer/consumer pipe id */
	writel_relaxed(producer_id | (consumer_id << CONSUMER_PIPE_ID_SHFT),
		       base + DML_PIPE_ID);

	/* Make sure dml initialization is finished */
	mb();

	return 0;
}

static struct mmci_host_ops qcom_variant_ops = {
	.prep_data = mmci_dmae_prep_data,
	.unprep_data = mmci_dmae_unprep_data,
	.get_next_data = mmci_dmae_get_next_data,
	.dma_setup = qcom_dma_setup,
	.dma_release = mmci_dmae_release,
	.dma_start = mmci_dmae_start,
	.dma_finalize = mmci_dmae_finalize,
	.dma_error = mmci_dmae_error,
};

void qcom_variant_init(struct mmci_host *host)
{
	host->ops = &qcom_variant_ops;
}
