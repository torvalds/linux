// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/interrupt.h>
#include "hbg_irq.h"
#include "hbg_hw.h"

static void hbg_irq_handle_err(struct hbg_priv *priv,
			       struct hbg_irq_info *irq_info)
{
	if (irq_info->need_print)
		dev_err(&priv->pdev->dev,
			"receive error interrupt: %s\n", irq_info->name);
}

static void hbg_irq_handle_tx(struct hbg_priv *priv,
			      struct hbg_irq_info *irq_info)
{
	napi_schedule(&priv->tx_ring.napi);
}

static void hbg_irq_handle_rx(struct hbg_priv *priv,
			      struct hbg_irq_info *irq_info)
{
	napi_schedule(&priv->rx_ring.napi);
}

#define HBG_TXRX_IRQ_I(name, handle) \
	{#name, HBG_INT_MSK_##name##_B, false, false, 0, handle}
#define HBG_ERR_IRQ_I(name, need_print) \
	{#name, HBG_INT_MSK_##name##_B, true, need_print, 0, hbg_irq_handle_err}

static struct hbg_irq_info hbg_irqs[] = {
	HBG_TXRX_IRQ_I(RX, hbg_irq_handle_rx),
	HBG_TXRX_IRQ_I(TX, hbg_irq_handle_tx),
	HBG_ERR_IRQ_I(MAC_MII_FIFO_ERR, true),
	HBG_ERR_IRQ_I(MAC_PCS_RX_FIFO_ERR, true),
	HBG_ERR_IRQ_I(MAC_PCS_TX_FIFO_ERR, true),
	HBG_ERR_IRQ_I(MAC_APP_RX_FIFO_ERR, true),
	HBG_ERR_IRQ_I(MAC_APP_TX_FIFO_ERR, true),
	HBG_ERR_IRQ_I(SRAM_PARITY_ERR, true),
	HBG_ERR_IRQ_I(TX_AHB_ERR, true),
	HBG_ERR_IRQ_I(RX_BUF_AVL, false),
	HBG_ERR_IRQ_I(REL_BUF_ERR, true),
	HBG_ERR_IRQ_I(TXCFG_AVL, false),
	HBG_ERR_IRQ_I(TX_DROP, false),
	HBG_ERR_IRQ_I(RX_DROP, false),
	HBG_ERR_IRQ_I(RX_AHB_ERR, true),
	HBG_ERR_IRQ_I(MAC_FIFO_ERR, false),
	HBG_ERR_IRQ_I(RBREQ_ERR, false),
	HBG_ERR_IRQ_I(WE_ERR, false),
};

static irqreturn_t hbg_irq_handle(int irq_num, void *p)
{
	struct hbg_irq_info *info;
	struct hbg_priv *priv = p;
	u32 status;
	u32 i;

	status = hbg_hw_get_irq_status(priv);
	for (i = 0; i < priv->vectors.info_array_len; i++) {
		info = &priv->vectors.info_array[i];
		if (status & info->mask) {
			if (!hbg_hw_irq_is_enabled(priv, info->mask))
				continue;

			hbg_hw_irq_enable(priv, info->mask, false);
			hbg_hw_irq_clear(priv, info->mask);

			info->count++;
			if (info->irq_handle)
				info->irq_handle(priv, info);

			if (info->re_enable)
				hbg_hw_irq_enable(priv, info->mask, true);
		}
	}

	return IRQ_HANDLED;
}

static const char *irq_names_map[HBG_VECTOR_NUM] = { "tx", "rx",
						     "err", "mdio" };

int hbg_irq_init(struct hbg_priv *priv)
{
	struct hbg_vector *vectors = &priv->vectors;
	struct device *dev = &priv->pdev->dev;
	int ret, id;
	u32 i;

	/* used pcim_enable_device(),  so the vectors become device managed */
	ret = pci_alloc_irq_vectors(priv->pdev, HBG_VECTOR_NUM, HBG_VECTOR_NUM,
				    PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to allocate vectors\n");

	if (ret != HBG_VECTOR_NUM)
		return dev_err_probe(dev, -EINVAL,
				     "requested %u MSI, but allocated %d MSI\n",
				     HBG_VECTOR_NUM, ret);

	/* mdio irq not requested, so the number of requested interrupts
	 * is HBG_VECTOR_NUM - 1.
	 */
	for (i = 0; i < HBG_VECTOR_NUM - 1; i++) {
		id = pci_irq_vector(priv->pdev, i);
		if (id < 0)
			return dev_err_probe(dev, id, "failed to get irq id\n");

		snprintf(vectors->name[i], sizeof(vectors->name[i]), "%s-%s-%s",
			 dev_driver_string(dev), pci_name(priv->pdev),
			 irq_names_map[i]);

		ret = devm_request_irq(dev, id, hbg_irq_handle, 0,
				       vectors->name[i], priv);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to request irq: %s\n",
					     irq_names_map[i]);
	}

	vectors->info_array = hbg_irqs;
	vectors->info_array_len = ARRAY_SIZE(hbg_irqs);
	return 0;
}
