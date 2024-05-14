// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2008-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 */
#include <linux/export.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <video/imx-ipu-v3.h>

#include "ipu-prv.h"

struct ipu_smfc {
	struct ipu_smfc_priv *priv;
	int chno;
	bool inuse;
};

struct ipu_smfc_priv {
	void __iomem *base;
	spinlock_t lock;
	struct ipu_soc *ipu;
	struct ipu_smfc channel[4];
	int use_count;
};

/*SMFC Registers */
#define SMFC_MAP	0x0000
#define SMFC_WMC	0x0004
#define SMFC_BS		0x0008

int ipu_smfc_set_burstsize(struct ipu_smfc *smfc, int burstsize)
{
	struct ipu_smfc_priv *priv = smfc->priv;
	unsigned long flags;
	u32 val, shift;

	spin_lock_irqsave(&priv->lock, flags);

	shift = smfc->chno * 4;
	val = readl(priv->base + SMFC_BS);
	val &= ~(0xf << shift);
	val |= burstsize << shift;
	writel(val, priv->base + SMFC_BS);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_smfc_set_burstsize);

int ipu_smfc_map_channel(struct ipu_smfc *smfc, int csi_id, int mipi_id)
{
	struct ipu_smfc_priv *priv = smfc->priv;
	unsigned long flags;
	u32 val, shift;

	spin_lock_irqsave(&priv->lock, flags);

	shift = smfc->chno * 3;
	val = readl(priv->base + SMFC_MAP);
	val &= ~(0x7 << shift);
	val |= ((csi_id << 2) | mipi_id) << shift;
	writel(val, priv->base + SMFC_MAP);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_smfc_map_channel);

int ipu_smfc_set_watermark(struct ipu_smfc *smfc, u32 set_level, u32 clr_level)
{
	struct ipu_smfc_priv *priv = smfc->priv;
	unsigned long flags;
	u32 val, shift;

	spin_lock_irqsave(&priv->lock, flags);

	shift = smfc->chno * 6 + (smfc->chno > 1 ? 4 : 0);
	val = readl(priv->base + SMFC_WMC);
	val &= ~(0x3f << shift);
	val |= ((clr_level << 3) | set_level) << shift;
	writel(val, priv->base + SMFC_WMC);

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_smfc_set_watermark);

int ipu_smfc_enable(struct ipu_smfc *smfc)
{
	struct ipu_smfc_priv *priv = smfc->priv;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	if (!priv->use_count)
		ipu_module_enable(priv->ipu, IPU_CONF_SMFC_EN);

	priv->use_count++;

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_smfc_enable);

int ipu_smfc_disable(struct ipu_smfc *smfc)
{
	struct ipu_smfc_priv *priv = smfc->priv;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	priv->use_count--;

	if (!priv->use_count)
		ipu_module_disable(priv->ipu, IPU_CONF_SMFC_EN);

	if (priv->use_count < 0)
		priv->use_count = 0;

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_smfc_disable);

struct ipu_smfc *ipu_smfc_get(struct ipu_soc *ipu, unsigned int chno)
{
	struct ipu_smfc_priv *priv = ipu->smfc_priv;
	struct ipu_smfc *smfc, *ret;
	unsigned long flags;

	if (chno >= 4)
		return ERR_PTR(-EINVAL);

	smfc = &priv->channel[chno];
	ret = smfc;

	spin_lock_irqsave(&priv->lock, flags);

	if (smfc->inuse) {
		ret = ERR_PTR(-EBUSY);
		goto unlock;
	}

	smfc->inuse = true;
unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(ipu_smfc_get);

void ipu_smfc_put(struct ipu_smfc *smfc)
{
	struct ipu_smfc_priv *priv = smfc->priv;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	smfc->inuse = false;
	spin_unlock_irqrestore(&priv->lock, flags);
}
EXPORT_SYMBOL_GPL(ipu_smfc_put);

int ipu_smfc_init(struct ipu_soc *ipu, struct device *dev,
		  unsigned long base)
{
	struct ipu_smfc_priv *priv;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ipu->smfc_priv = priv;
	spin_lock_init(&priv->lock);
	priv->ipu = ipu;

	priv->base = devm_ioremap(dev, base, PAGE_SIZE);
	if (!priv->base)
		return -ENOMEM;

	for (i = 0; i < 4; i++) {
		priv->channel[i].priv = priv;
		priv->channel[i].chno = i;
	}

	pr_debug("%s: ioremap 0x%08lx -> %p\n", __func__, base, priv->base);

	return 0;
}

void ipu_smfc_exit(struct ipu_soc *ipu)
{
}
