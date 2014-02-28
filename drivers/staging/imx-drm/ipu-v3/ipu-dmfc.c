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

#include "imx-ipu-v3.h"
#include "ipu-prv.h"

#define DMFC_RD_CHAN		0x0000
#define DMFC_WR_CHAN		0x0004
#define DMFC_WR_CHAN_DEF	0x0008
#define DMFC_DP_CHAN		0x000c
#define DMFC_DP_CHAN_DEF	0x0010
#define DMFC_GENERAL1		0x0014
#define DMFC_GENERAL2		0x0018
#define DMFC_IC_CTRL		0x001c
#define DMFC_STAT		0x0020

#define DMFC_WR_CHAN_1_28		0
#define DMFC_WR_CHAN_2_41		8
#define DMFC_WR_CHAN_1C_42		16
#define DMFC_WR_CHAN_2C_43		24

#define DMFC_DP_CHAN_5B_23		0
#define DMFC_DP_CHAN_5F_27		8
#define DMFC_DP_CHAN_6B_24		16
#define DMFC_DP_CHAN_6F_29		24

#define DMFC_FIFO_SIZE_64		(3 << 3)
#define DMFC_FIFO_SIZE_128		(2 << 3)
#define DMFC_FIFO_SIZE_256		(1 << 3)
#define DMFC_FIFO_SIZE_512		(0 << 3)

#define DMFC_SEGMENT(x)			((x & 0x7) << 0)
#define DMFC_BURSTSIZE_128		(0 << 6)
#define DMFC_BURSTSIZE_64		(1 << 6)
#define DMFC_BURSTSIZE_32		(2 << 6)
#define DMFC_BURSTSIZE_16		(3 << 6)

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
	unsigned			slotmask;
	unsigned			segment;
	int				burstsize;
	struct ipu_soc			*ipu;
	struct ipu_dmfc_priv		*priv;
	const struct dmfc_channel_data	*data;
};

struct ipu_dmfc_priv {
	struct ipu_soc *ipu;
	struct device *dev;
	struct dmfc_channel channels[DMFC_NUM_CHANNELS];
	struct mutex mutex;
	unsigned long bandwidth_per_slot;
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

void ipu_dmfc_disable_channel(struct dmfc_channel *dmfc)
{
	struct ipu_dmfc_priv *priv = dmfc->priv;

	mutex_lock(&priv->mutex);

	priv->use_count--;

	if (!priv->use_count)
		ipu_module_disable(priv->ipu, IPU_CONF_DMFC_EN);

	if (priv->use_count < 0)
		priv->use_count = 0;

	mutex_unlock(&priv->mutex);
}
EXPORT_SYMBOL_GPL(ipu_dmfc_disable_channel);

static int ipu_dmfc_setup_channel(struct dmfc_channel *dmfc, int slots,
		int segment, int burstsize)
{
	struct ipu_dmfc_priv *priv = dmfc->priv;
	u32 val, field;

	dev_dbg(priv->dev,
			"dmfc: using %d slots starting from segment %d for IPU channel %d\n",
			slots, segment, dmfc->data->ipu_channel);

	switch (slots) {
	case 1:
		field = DMFC_FIFO_SIZE_64;
		break;
	case 2:
		field = DMFC_FIFO_SIZE_128;
		break;
	case 4:
		field = DMFC_FIFO_SIZE_256;
		break;
	case 8:
		field = DMFC_FIFO_SIZE_512;
		break;
	default:
		return -EINVAL;
	}

	switch (burstsize) {
	case 16:
		field |= DMFC_BURSTSIZE_16;
		break;
	case 32:
		field |= DMFC_BURSTSIZE_32;
		break;
	case 64:
		field |= DMFC_BURSTSIZE_64;
		break;
	case 128:
		field |= DMFC_BURSTSIZE_128;
		break;
	}

	field |= DMFC_SEGMENT(segment);

	val = readl(priv->base + dmfc->data->channel_reg);

	val &= ~(0xff << dmfc->data->shift);
	val |= field << dmfc->data->shift;

	writel(val, priv->base + dmfc->data->channel_reg);

	dmfc->slots = slots;
	dmfc->segment = segment;
	dmfc->burstsize = burstsize;
	dmfc->slotmask = ((1 << slots) - 1) << segment;

	return 0;
}

static int dmfc_bandwidth_to_slots(struct ipu_dmfc_priv *priv,
		unsigned long bandwidth)
{
	int slots = 1;

	while (slots * priv->bandwidth_per_slot < bandwidth)
		slots *= 2;

	return slots;
}

static int dmfc_find_slots(struct ipu_dmfc_priv *priv, int slots)
{
	unsigned slotmask_need, slotmask_used = 0;
	int i, segment = 0;

	slotmask_need = (1 << slots) - 1;

	for (i = 0; i < DMFC_NUM_CHANNELS; i++)
		slotmask_used |= priv->channels[i].slotmask;

	while (slotmask_need <= 0xff) {
		if (!(slotmask_used & slotmask_need))
			return segment;

		slotmask_need <<= 1;
		segment++;
	}

	return -EBUSY;
}

void ipu_dmfc_free_bandwidth(struct dmfc_channel *dmfc)
{
	struct ipu_dmfc_priv *priv = dmfc->priv;
	int i;

	dev_dbg(priv->dev, "dmfc: freeing %d slots starting from segment %d\n",
			dmfc->slots, dmfc->segment);

	mutex_lock(&priv->mutex);

	if (!dmfc->slots)
		goto out;

	dmfc->slotmask = 0;
	dmfc->slots = 0;
	dmfc->segment = 0;

	for (i = 0; i < DMFC_NUM_CHANNELS; i++)
		priv->channels[i].slotmask = 0;

	for (i = 0; i < DMFC_NUM_CHANNELS; i++) {
		if (priv->channels[i].slots > 0) {
			priv->channels[i].segment =
				dmfc_find_slots(priv, priv->channels[i].slots);
			priv->channels[i].slotmask =
				((1 << priv->channels[i].slots) - 1) <<
				priv->channels[i].segment;
		}
	}

	for (i = 0; i < DMFC_NUM_CHANNELS; i++) {
		if (priv->channels[i].slots > 0)
			ipu_dmfc_setup_channel(&priv->channels[i],
					priv->channels[i].slots,
					priv->channels[i].segment,
					priv->channels[i].burstsize);
	}
out:
	mutex_unlock(&priv->mutex);
}
EXPORT_SYMBOL_GPL(ipu_dmfc_free_bandwidth);

int ipu_dmfc_alloc_bandwidth(struct dmfc_channel *dmfc,
		unsigned long bandwidth_pixel_per_second, int burstsize)
{
	struct ipu_dmfc_priv *priv = dmfc->priv;
	int slots = dmfc_bandwidth_to_slots(priv, bandwidth_pixel_per_second);
	int segment = -1, ret = 0;

	dev_dbg(priv->dev, "dmfc: trying to allocate %ldMpixel/s for IPU channel %d\n",
			bandwidth_pixel_per_second / 1000000,
			dmfc->data->ipu_channel);

	ipu_dmfc_free_bandwidth(dmfc);

	mutex_lock(&priv->mutex);

	if (slots > 8) {
		ret = -EBUSY;
		goto out;
	}

	/* For the MEM_BG channel, first try to allocate twice the slots */
	if (dmfc->data->ipu_channel == IPUV3_CHANNEL_MEM_BG_SYNC)
		segment = dmfc_find_slots(priv, slots * 2);
	else if (slots < 2)
		/* Always allocate at least 128*4 bytes (2 slots) */
		slots = 2;

	if (segment >= 0)
		slots *= 2;
	else
		segment = dmfc_find_slots(priv, slots);
	if (segment < 0) {
		ret = -EBUSY;
		goto out;
	}

	ipu_dmfc_setup_channel(dmfc, slots, segment, burstsize);

out:
	mutex_unlock(&priv->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(ipu_dmfc_alloc_bandwidth);

int ipu_dmfc_init_channel(struct dmfc_channel *dmfc, int width)
{
	struct ipu_dmfc_priv *priv = dmfc->priv;
	u32 dmfc_gen1;

	dmfc_gen1 = readl(priv->base + DMFC_GENERAL1);

	if ((dmfc->slots * 64 * 4) / width > dmfc->data->max_fifo_lines)
		dmfc_gen1 |= 1 << dmfc->data->eot_shift;
	else
		dmfc_gen1 &= ~(1 << dmfc->data->eot_shift);

	writel(dmfc_gen1, priv->base + DMFC_GENERAL1);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_dmfc_init_channel);

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
	ipu_dmfc_free_bandwidth(dmfc);
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
	}

	writel(0x0, priv->base + DMFC_WR_CHAN);
	writel(0x0, priv->base + DMFC_DP_CHAN);

	/*
	 * We have a total bandwidth of clkrate * 4pixel divided
	 * into 8 slots.
	 */
	priv->bandwidth_per_slot = clk_get_rate(ipu_clk) * 4 / 8;

	dev_dbg(dev, "dmfc: 8 slots with %ldMpixel/s bandwidth each\n",
			priv->bandwidth_per_slot / 1000000);

	writel(0x202020f6, priv->base + DMFC_WR_CHAN_DEF);
	writel(0x2020f6f6, priv->base + DMFC_DP_CHAN_DEF);
	writel(0x00000003, priv->base + DMFC_GENERAL1);

	return 0;
}

void ipu_dmfc_exit(struct ipu_soc *ipu)
{
}
