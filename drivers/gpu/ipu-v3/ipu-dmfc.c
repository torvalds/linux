/*
 * Copyright (c) 2010 Sascha Hauer <s.hauer@pengutronix.de>
 * Copyright (C) 2005-2009 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#include <linux/export.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/io.h>

#include <video/imx-ipu-v3.h>
#include "ipu-prv.h"

#define DMFC_RD_CHAN		0x0000
#define DMFC_WR_CHAN		0x0004
#define DMFC_WR_CHAN_DEF	0x0008
#define DMFC_DP_CHAN		0x000c
#define DMFC_DP_CHAN_DEF	0x0010
#define DMFC_GENERAL1		0x0014
#define DMFC_GENERAL2		0x0018
#define DMFC_IC_CTRL		0x001c
#define DMFC_WR_CHAN_ALT	0x0020
#define DMFC_WR_CHAN_DEF_ALT	0x0024
#define DMFC_DP_CHAN_ALT	0x0028
#define DMFC_DP_CHAN_DEF_ALT	0x002c
#define DMFC_GENERAL1_ALT	0x0030
#define DMFC_STAT		0x0034

#define DMFC_WR_CHAN_1_28		0
#define DMFC_WR_CHAN_2_41		8
#define DMFC_WR_CHAN_1C_42		16
#define DMFC_WR_CHAN_2C_43		24

#define DMFC_DP_CHAN_5B_23		0
#define DMFC_DP_CHAN_5F_27		8
#define DMFC_DP_CHAN_6B_24		16
#define DMFC_DP_CHAN_6F_29		24

struct dmfc_channel_data {
	int		ipu_channel;
	unsigned long	channel_reg;
	unsigned long	shift;
	unsigned	eot_shift;
	unsigned	max_fifo_lines;
};

static const struct dmfc_channel_data dmfcdata[] = {
	{
		.ipu_channel	= IPUV3_CHANNEL_MEM_BG_SYNC,
		.channel_reg	= DMFC_DP_CHAN,
		.shift		= DMFC_DP_CHAN_5B_23,
		.eot_shift	= 20,
		.max_fifo_lines	= 3,
	}, {
		.ipu_channel	= 24,
		.channel_reg	= DMFC_DP_CHAN,
		.shift		= DMFC_DP_CHAN_6B_24,
		.eot_shift	= 22,
		.max_fifo_lines	= 1,
	}, {
		.ipu_channel	= IPUV3_CHANNEL_MEM_FG_SYNC,
		.channel_reg	= DMFC_DP_CHAN,
		.shift		= DMFC_DP_CHAN_5F_27,
		.eot_shift	= 21,
		.max_fifo_lines	= 2,
	}, {
		.ipu_channel	= IPUV3_CHANNEL_MEM_DC_SYNC,
		.channel_reg	= DMFC_WR_CHAN,
		.shift		= DMFC_WR_CHAN_1_28,
		.eot_shift	= 16,
		.max_fifo_lines	= 2,
	}, {
		.ipu_channel	= 29,
		.channel_reg	= DMFC_DP_CHAN,
		.shift		= DMFC_DP_CHAN_6F_29,
		.eot_shift	= 23,
		.max_fifo_lines	= 1,
	},
};

#define DMFC_NUM_CHANNELS	ARRAY_SIZE(dmfcdata)

struct ipu_dmfc_priv;

struct dmfc_channel {
	unsigned			slots;
	struct ipu_soc			*ipu;
	struct ipu_dmfc_priv		*priv;
	const struct dmfc_channel_data	*data;
};

struct ipu_dmfc_priv {
	struct ipu_soc *ipu;
	struct device *dev;
	struct dmfc_channel channels[DMFC_NUM_CHANNELS];
	struct mutex mutex;
	void __iomem *base;
	int use_count;
};

int ipu_dmfc_enable_channel(struct dmfc_channel *dmfc)
{
	struct ipu_dmfc_priv *priv = dmfc->priv;
	mutex_lock(&priv->mutex);

	if (!priv->use_count)
		ipu_module_enable(priv->ipu, IPU_CONF_DMFC_EN);

	priv->use_count++;

	mutex_unlock(&priv->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_dmfc_enable_channel);

static void ipu_dmfc_wait_fifos(struct ipu_dmfc_priv *priv)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	while ((readl(priv->base + DMFC_STAT) & 0x02fff000) != 0x02fff000) {
		if (time_after(jiffies, timeout)) {
			dev_warn(priv->dev,
				 "Timeout waiting for DMFC FIFOs to clear\n");
			break;
		}
		cpu_relax();
	}
}

void ipu_dmfc_disable_channel(struct dmfc_channel *dmfc)
{
	struct ipu_dmfc_priv *priv = dmfc->priv;

	mutex_lock(&priv->mutex);

	priv->use_count--;

	if (!priv->use_count) {
		ipu_dmfc_wait_fifos(priv);
		ipu_module_disable(priv->ipu, IPU_CONF_DMFC_EN);
	}

	if (priv->use_count < 0)
		priv->use_count = 0;

	mutex_unlock(&priv->mutex);
}
EXPORT_SYMBOL_GPL(ipu_dmfc_disable_channel);

void ipu_dmfc_config_wait4eot(struct dmfc_channel *dmfc, int width)
{
	struct ipu_dmfc_priv *priv = dmfc->priv;
	u32 dmfc_gen1;

	mutex_lock(&priv->mutex);

	dmfc_gen1 = readl(priv->base + DMFC_GENERAL1);

	if ((dmfc->slots * 64 * 4) / width > dmfc->data->max_fifo_lines)
		dmfc_gen1 |= 1 << dmfc->data->eot_shift;
	else
		dmfc_gen1 &= ~(1 << dmfc->data->eot_shift);

	writel(dmfc_gen1, priv->base + DMFC_GENERAL1);

	mutex_unlock(&priv->mutex);
}
EXPORT_SYMBOL_GPL(ipu_dmfc_config_wait4eot);

struct dmfc_channel *ipu_dmfc_get(struct ipu_soc *ipu, int ipu_channel)
{
	struct ipu_dmfc_priv *priv = ipu->dmfc_priv;
	int i;

	for (i = 0; i < DMFC_NUM_CHANNELS; i++)
		if (dmfcdata[i].ipu_channel == ipu_channel)
			return &priv->channels[i];
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(ipu_dmfc_get);

void ipu_dmfc_put(struct dmfc_channel *dmfc)
{
}
EXPORT_SYMBOL_GPL(ipu_dmfc_put);

int ipu_dmfc_init(struct ipu_soc *ipu, struct device *dev, unsigned long base,
		struct clk *ipu_clk)
{
	struct ipu_dmfc_priv *priv;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_ioremap(dev, base, PAGE_SIZE);
	if (!priv->base)
		return -ENOMEM;

	priv->dev = dev;
	priv->ipu = ipu;
	mutex_init(&priv->mutex);

	ipu->dmfc_priv = priv;

	for (i = 0; i < DMFC_NUM_CHANNELS; i++) {
		priv->channels[i].priv = priv;
		priv->channels[i].ipu = ipu;
		priv->channels[i].data = &dmfcdata[i];

		if (dmfcdata[i].ipu_channel == IPUV3_CHANNEL_MEM_BG_SYNC ||
		    dmfcdata[i].ipu_channel == IPUV3_CHANNEL_MEM_FG_SYNC ||
		    dmfcdata[i].ipu_channel == IPUV3_CHANNEL_MEM_DC_SYNC)
			priv->channels[i].slots = 2;
	}

	writel(0x00000050, priv->base + DMFC_WR_CHAN);
	writel(0x00005654, priv->base + DMFC_DP_CHAN);
	writel(0x202020f6, priv->base + DMFC_WR_CHAN_DEF);
	writel(0x2020f6f6, priv->base + DMFC_DP_CHAN_DEF);
	writel(0x00000003, priv->base + DMFC_GENERAL1);

	return 0;
}

void ipu_dmfc_exit(struct ipu_soc *ipu)
{
}
